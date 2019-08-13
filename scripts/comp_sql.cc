/* Copyright (c) 2004, 2019, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is also distributed with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have included with MySQL.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

/*
  Converts a SQL file into a C file that can be compiled and linked
  into other programs
*/

#include "my_config.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "my_compiler.h"
#include "mysql/psi/mysql_file.h"
#include "sql/sql_bootstrap.h"
#include "welcome_copyright_notice.h"
/*
  This is an internal tool used during the build process only,
  - do not make a library just for this,
    which would make the Makefiles and the server link
    more complex than necessary,
  - do not duplicate the code either.
 so just add the sql_bootstrap.cc code as is.
*/
#include "sql/sql_bootstrap.cc"

FILE *in;
FILE *out;

static void die(const char *fmt, ...) MY_ATTRIBUTE((noreturn))
    MY_ATTRIBUTE((format(printf, 1, 2)));

static void die(const char *fmt, ...) {
  va_list args;

  /* Print the error message */
  fprintf(stderr, "FATAL ERROR: ");
  if (fmt) {
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
  } else
    fprintf(stderr, "unknown error");
  fprintf(stderr, "\n");
  fflush(stderr);

  /* Close any open files */
  if (in) fclose(in);
  if (out) fclose(out);

  exit(1);
}

static void parser_die(const char *message) { die("%s", message); }

static char *fgets_fn(char *buffer, size_t size, MYSQL_FILE *input,
                      int *error) {
  FILE *real_in = input->m_file;
  char *line = fgets(buffer, (int)size, real_in);
  if (error) {
    *error = (line == NULL) ? ferror(real_in) : 0;
  }
  return line;
}

static void print_query(FILE *out, const char *query) {
  const char *ptr = query;
  int column = 0;

  fprintf(out, "\"");
  while (*ptr) {
    /* utf-8 encoded characters are always >= 0x80 unsigned */
    if (column >= 120 && static_cast<uint8_t>(*ptr) < 0x80) {
      /* Wrap to the next line, tabulated. */
      fprintf(out, "\"\n  \"");
      column = 3;
    }
    switch (*ptr) {
      case '\n':
        /*
          Preserve the \n character in the query text,
          and wrap to the next line, tabulated.
        */
        fprintf(out, "\\n\"\n  \"");
        column = 3;
        break;
      case '\r':
        /* Skipped */
        break;
      case '\"':
        fprintf(out, "\\\"");
        column += 2;
        break;
      case '\\':
        fprintf(out, "\\\\");
        column++;
        break;
      default:
        putc(*ptr, out);
        column++;
        break;
    }
    ptr++;
  }
  fprintf(out, "\\n\",\n");
}

int main(int argc, char *argv[]) {
  char query[MAX_BOOTSTRAP_QUERY_SIZE];
  char *struct_name = argv[1];
  char *infile_name = argv[2];
  char *outfile_name = argv[3];
  int rc;
  size_t query_length = 0;
  bootstrap_parser_state parser_state;

  if (argc != 4)
    die("Usage: comp_sql <struct_name> <sql_filename> <c_filename>");

  /* Open input and output file */
  if (!(in = fopen(infile_name, "r")))
    die("Failed to open SQL file '%s'", infile_name);
  if (!(out = fopen(outfile_name, "w")))
    die("Failed to open output file '%s'", outfile_name);

  fprintf(out, ORACLE_GPL_COPYRIGHT_NOTICE("2004"));
  fprintf(out, "/*\n");
  fprintf(out,
          "  Do not edit this file, it is automatically generated from:\n");
  fprintf(out, "  <%s>\n", infile_name);
  fprintf(out, "*/\n");
  fprintf(out, "const char* %s[]={\n", struct_name);

  parser_state.init(infile_name);

  /* Craft an non instrumented MYSQL_FILE. */
  MYSQL_FILE fake_in;
  fake_in.m_file = in;
  fake_in.m_psi = nullptr;

  for (;;) {
    rc = read_bootstrap_query(query, &query_length, &fake_in, fgets_fn,
                              &parser_state);

    if (rc == READ_BOOTSTRAP_EOF) {
      break;
    }

    if (rc != READ_BOOTSTRAP_SUCCESS) {
      parser_state.report_error_details(parser_die);
    }

    print_query(out, query);
  }

  fprintf(out, "NULL\n};\n");

  fclose(in);
  fclose(out);

  exit(0);
}
