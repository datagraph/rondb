/*
   Copyright (C) 2003 MySQL AB
    All rights reserved. Use is subject to license terms.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/


#include <ndb_global.h>

#include <NdbOut.hpp>

#include <NdbApi.hpp>
#include <NdbMain.h>
#include <NDBT.hpp> 
#include <NdbSleep.h>
#include <getarg.h>
#include "Bank.hpp"
 

int main(int argc, const char** argv){
  ndb_init();
  int _help = 0;
  int _wait = 30;
  const char * _database="BANK";
  
  struct getargs args[] = {
    { "wait", 'w', arg_integer, &_wait, "Max time to wait between days", "secs" },
    { "database", 'd', arg_string, &_database, "Database name", ""}, 
    { "usage", '?', arg_flag, &_help, "Print help", "" }
  };
  int num_args = sizeof(args) / sizeof(args[0]);
  int optind = 0;
  char desc[] = 
    "This program will increase time in the bank\n";
  
  if(getarg(args, num_args, argc, argv, &optind) ||  _help) {
    arg_printusage(args, num_args, argv[0], desc);
    return NDBT_ProgramExit(NDBT_WRONGARGS);
  }

  Ndb_cluster_connection con;
  if(con.connect(12, 5, 1) != 0)
  {
    return NDBT_ProgramExit(NDBT_FAILED);
  }

  Bank bank(con,_database);

  if (bank.performIncreaseTime(_wait) != 0)
    return NDBT_ProgramExit(NDBT_FAILED);
  
  return NDBT_ProgramExit(NDBT_OK);

}



