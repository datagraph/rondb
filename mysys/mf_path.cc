/* Copyright (c) 2000, 2016, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

/**
  @file mysys/mf_path.cc
*/

#include "my_config.h"

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "m_string.h"
#include "my_dbug.h"
#include "my_inttypes.h"
#include "my_io.h"
#include "my_static.h"
#include "my_sys.h"

static char *find_file_in_path(char *to,const char *name);

	/* Finds where program can find it's files.
	   pre_pathname is found by first locking at progname (argv[0]).
	   if progname contains path the path is returned.
	   else if progname is found in path, return it
	   else if progname is given and POSIX environment variable "_" is set
	   then path is taken from "_".
	   If filename doesn't contain a path append MY_BASEDIR_VERSION or
	   MY_BASEDIR if defined, else append "/my/running".
	   own_path_name_part is concatinated to result.
	   my_path puts result in to and returns to */

char * my_path(char * to, const char *progname,
               const char *own_pathname_part)
{
  char *start, *end, *prog;
  size_t to_length;
  DBUG_ENTER("my_path");

  start=to;					/* Return this */
  if (progname && (dirname_part(to, progname, &to_length) ||
		   find_file_in_path(to,progname) ||
		   ((prog=getenv("_")) != 0 &&
                    dirname_part(to, prog, &to_length))))
  {
    (void) intern_filename(to,to);
    if (!test_if_hard_path(to))
    {
      if (!my_getwd(curr_dir,FN_REFLEN,MYF(0)))
	bchange((uchar*) to, 0, (uchar*) curr_dir, strlen(curr_dir), strlen(to)+1);
    }
  }
  else
  {
    if ((end = getenv("MY_BASEDIR_VERSION")) == 0 &&
	(end = getenv("MY_BASEDIR")) == 0)
    {
#ifdef DEFAULT_BASEDIR
      end= (char*) DEFAULT_BASEDIR;
#else
      end= (char*) "/my/";
#endif
    }
    (void) intern_filename(to,end);
    to=strend(to);
    if (to != start && to[-1] != FN_LIBCHAR)
      *to++ = FN_LIBCHAR;
    (void) my_stpcpy(to,own_pathname_part);
  }
  DBUG_PRINT("exit",("to: '%s'",start));
  DBUG_RETURN(start);
} /* my_path */


	/* test if file without filename is found in path */
	/* Returns to if found and to has dirpart if found, else NullS */

#if defined(_WIN32)
#define F_OK 0
#define PATH_SEP ';'
#define PROGRAM_EXTENSION ".exe"
#else
#define PATH_SEP ':'
#endif

static char *find_file_in_path(char *to, const char *name)
{
  char *path,*pos,dir[2];
  const char *ext="";

  if (!(path=getenv("PATH")))
    return NullS;
  dir[0]=FN_LIBCHAR; dir[1]=0;
#ifdef PROGRAM_EXTENSION
  if (!fn_ext(name)[0])
    ext=PROGRAM_EXTENSION;
#endif

  for (pos=path ; (pos=strchr(pos,PATH_SEP)) ; path= ++pos)
  {
    if (path != pos)
    {
      strxmov(my_stpnmov(to,path,(uint) (pos-path)),dir,name,ext,NullS);
      if (!access(to,F_OK))
      {
	to[(uint) (pos-path)+1]=0;	/* Return path only */
	return to;
      }
    }
  }
#ifdef _WIN32
  to[0]=FN_CURLIB;
  strxmov(to+1,dir,name,ext,NullS);
  if (!access(to,F_OK))			/* Test in current dir */
  {
    to[2]=0;				/* Leave ".\" */
    return to;
  }
#endif
  return NullS;				/* File not found */
}
