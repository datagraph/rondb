/*
   Copyright (c) 2005, 2014, Oracle and/or its affiliates. All rights reserved.

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
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/

/* 
 *  ndbapi_simple.cpp: Using synchronous transactions in NDB API
 *
 *  Correct output from this program is:
 *
 *  ATTR1 ATTR2
 *    0    10
 *    1     1
 *    2    12
 *  Detected that deleted tuple doesn't exist!
 *    4    14
 *    5     5
 *    6    16
 *    7     7
 *    8    18
 *    9     9
 *
 */

#include <mysql.h>
#include <mysqld_error.h>
#include <NdbApi.hpp>
#include <stdlib.h>
// Used for cout
#include <stdio.h>
#include <iostream>

static void run_application(MYSQL &, Ndb_cluster_connection &);

#define PRINT_ERROR(code,msg) \
  std::cout << "Error in " << __FILE__ << ", line: " << __LINE__ \
            << ", code: " << code \
            << ", msg: " << msg << "." << std::endl
#define MYSQLERROR(mysql) { \
  PRINT_ERROR(mysql_errno(&mysql),mysql_error(&mysql)); \
  exit(-1); }
#define APIERROR(error) { \
  PRINT_ERROR(error.code,error.message); \
  exit(-1); }

int main(int argc, char** argv)
{
  if (argc != 3)
  {
    std::cout << "Arguments are <socket mysqld> <connect_string cluster>.\n";
    exit(-1);
  }
  // ndb_init must be called first
  ndb_init();

  // connect to mysql server and cluster and run application
  {
    char * mysqld_sock  = argv[1];
    const char *connectstring = argv[2];
    // Object representing the cluster
    Ndb_cluster_connection cluster_connection(connectstring);

    // Connect to cluster management server (ndb_mgmd)
    if (cluster_connection.connect(4 /* retries               */,
				   5 /* delay between retries */,
				   1 /* verbose               */))
    {
      std::cout << "Cluster management server was not ready within 30 secs.\n";
      exit(-1);
    }

    // Optionally connect and wait for the storage nodes (ndbd's)
    if (cluster_connection.wait_until_ready(30,0) < 0)
    {
      std::cout << "Cluster was not ready within 30 secs.\n";
      exit(-1);
    }

    // connect to mysql server
    MYSQL mysql;
    if ( !mysql_init(&mysql) ) {
      std::cout << "mysql_init failed\n";
      exit(-1);
    }
    if ( !mysql_real_connect(&mysql, "localhost", "root", "", "",
			     0, mysqld_sock, 0) )
      MYSQLERROR(mysql);
    
    // run the application code
    run_application(mysql, cluster_connection);
  }

  ndb_end(0);

  return 0;
}

static void do_scan(Ndb &);

static void run_application(MYSQL &mysql,
			    Ndb_cluster_connection &cluster_connection)
{
  /********************************************
   * Connect to database via mysql-c          *ndb_examples
   ********************************************/
  //mysql_query(&mysql, "CREATE DATABASE ndb_examples");
  if (mysql_query(&mysql, "USE mgr") != 0) MYSQLERROR(mysql);

  /********************************************
   * Connect to database via NdbApi           *
   ********************************************/
  // Object representing the database
  Ndb myNdb( &cluster_connection, "mgr" );
  if (myNdb.init()) APIERROR(myNdb.getNdbError());

  /*
   * Do different operations on database
   */
  do_scan(myNdb);
}

/*
 * create table as:
 *   create table test
 *          (s int unsigned not null, p int unsigned not null,
 *           o int unsigned not null, g int unsigned not null,
 *           index gspo (g,s,p,o), index gpos (g,p,o,s), index gosp (g,o,s,p),
 *           index spog (s,p,o,g), index posg (p,o,s,g), index ospg (o,s,p,g));
 *
 * load data with:
 *    load data infile '/path/to/data.tsv' into table test;
*/

struct Quad
{
  Uint32 s;
  Uint32 p;
  Uint32 o;
  Uint32 g;
};

static void do_scan(Ndb &myNdb)
{
  NdbTransaction *myTransaction= myNdb.startTransaction();
  if (myTransaction == NULL)
    APIERROR(myNdb.getNdbError());

  const NdbDictionary::Dictionary *myDict= myNdb.getDictionary();
  const NdbDictionary::Table *test = myDict->getTable("test");
  const NdbDictionary::Index *mySIndex= myDict->getIndex("gspo", test->getName());

  if (mySIndex == NULL)
    APIERROR(myDict->getNdbError());
    
  NdbIndexScanOperation *ixScan = 
    myTransaction->scanIndex(mySIndex->getDefaultRecord(),
                             test->getDefaultRecord());

  /*
  NdbScanOperation::ScanOptions options;
  options.optionsPresent=NdbScanOperation::ScanOptions::SO_SCANFLAGS;
  Uint32 scanFlags = NdbScanOperation::SF_OrderBy | NdbScanOperation::SF_MultiRange; 
  options.scan_flags=scanFlags;
  NdbIndexScanOperation *ixScan = 
    myTransaction->scanIndex(mySIndex->getDefaultRecord(),
                             test->getDefaultRecord(),
                                    NdbOperation::LM_Read,
                                    NULL, // mask
                                    NULL, // bound
                                    &options,
                                    sizeof(NdbScanOperation::ScanOptions));
  */

  if (ixScan == NULL)
    APIERROR(myTransaction->getNdbError());

  // Quad low={662743, 2219900, (Uint32)NULL, (Uint32)NULL};
  // Quad high={662743, 3000000, (Uint32)NULL, (Uint32)NULL};
  Quad low={662743, 2000000, (Uint32)NULL, (Uint32)NULL};
  Quad high={662743, 2200000, (Uint32)NULL, (Uint32)NULL};
  //Quad low={1, (Uint32)NULL, (Uint32)NULL, (Uint32)NULL};
  //Quad high={662743, (Uint32)NULL, (Uint32)NULL, (Uint32)NULL};
  NdbIndexScanOperation::IndexBound bound;
  bound.low_key= (char*)&low;
  bound.low_key_count= 2;
  bound.low_inclusive= true;
  bound.high_key= (char*)&high;
  bound.high_key_count= 2;
  bound.high_inclusive= true;
  bound.range_no= 0;

  /*
  Quad val={662743, (Uint32)NULL, (Uint32)NULL, (Uint32)NULL};
  NdbIndexScanOperation::IndexBound bound;
  bound.low_key= (char*)&val;
  bound.low_key_count= 1;
  bound.low_inclusive= true;
  bound.high_key= (char*)&val;
  bound.high_key_count= 1;
  bound.high_inclusive= true;
  bound.range_no= 0;
  */
  
  // Quad low2 = {1106,1105,1105,638};
  // Quad high2 = {1109,1105,1106,1108};
  // /*
  // Uint32 low2 = 283392;
  // Uint32 high2 = 283392;
  // //  Uint32 high = (Uint32)NULL;
  // printf("low: %u, high %u\n", low.s, high.s);
  // printf("low: %u, high %u\n", low.s, *((Uint32*)&high));
  // printf("size: %zu, %u, %zu, %u\n", sizeof(low), low.s, sizeof(low2), low2);
  // */
  // NdbIndexScanOperation::IndexBound bound2;
  // bound2.low_key= (char*)&low2;
  // bound2.low_key_count= 4;
  // bound2.low_inclusive= true;
  // bound2.high_key= (char*)&high2;
  // bound2.high_key_count= 4;
  // bound2.high_inclusive= false;
  // bound2.range_no= 1;

  if (ixScan->setBound(mySIndex->getDefaultRecord(), bound))
     APIERROR(myTransaction->getNdbError());
  //if (ixScan->setBound(mySIndex->getDefaultRecord(), bound2))
  //  APIERROR(myTransaction->getNdbError());

  printf("Start execute\n");
  if(myTransaction->execute( NdbTransaction::NoCommit ) != 0)
    APIERROR(myTransaction->getNdbError());

  // Check rc anyway
  if (myTransaction->getNdbError().status != NdbError::Success)
    APIERROR(myTransaction->getNdbError());

  printf("Done executed\n");

  printf("Start printing\n");
  Quad *prowData; // Ptr to point to our data

  int rc=0;

  //size_t foo = sizeof(Uint32);
  //printf("sife of Uint32: %zu\n", foo);

  while ((rc = ixScan->nextResult((const char**) &prowData,
                                  true,
                                  false)) == 0)
    {
      // printf(" PTR : %d\n", (int) prowData);
      // printf(" %u    %u    %u    %u    Range no : %2d\n", 
      //        prowData->s,
      //        prowData->p,
      //        prowData->o,
      //        prowData->g,
      //        ixScan->get_range_no());
      printf("%u\t%u\t%u\t%u\n", prowData->s, prowData->p, prowData->o, prowData->g);

      // access with pointer arithmetic of Uint32 struct members:
      //Uint32 *s = &(prowData->s);
      //printf("%u\t%u\t%u\t%u\n", *(s+0), *(s+1), *(s+2), *(s+3));
      // access with pointer arithmetic of struct members as byte (= unsigned char) pointers:
      //unsigned char *s = (unsigned char *)&(prowData->s);
      //printf("%u\t%u\t%u\t%u\n", *(Uint32*)(s+0), *(Uint32*)(s+4), *(Uint32*)(s+8), *(Uint32*)(s+12));

      // padding of first element in struct: same address location = no padding
      //printf("p: %zu\t%zu\n", (long unsigned int)(prowData), (long unsigned int)&(prowData->s));

      // direct access of struct members via Uint32 pointer arithmetic:
      //Uint32 *s = (Uint32*)prowData;
      //printf("%u\t%u\t%u\t%u\n", *(s+0), *(s+1), *(s+2), *(s+3));

      // direct access of struct members via byte (= unsigned char) pointer arithmetic:
      //unsigned char *s = (unsigned char *)prowData;
      //printf("%u\t%u\t%u\t%u\n", *(Uint32*)(s+4*0), *(Uint32*)(s+4*1), *(Uint32*)(s+4*2), *(Uint32*)(s+4*3));

    }

  if (rc != 1)  APIERROR(myTransaction->getNdbError());

  ixScan->close(true);
  printf("Done printing\n");
}
