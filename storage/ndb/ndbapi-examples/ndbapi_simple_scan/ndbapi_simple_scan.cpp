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

static void init_ndbrecord_info(Ndb &);
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

  init_ndbrecord_info(myNdb);
  /*
   * Do different operations on database
   */
  do_scan(myNdb);
}

struct QuadGSPO
{
  Uint32 g;
  Uint32 s;
  Uint32 p;
  Uint32 o;
};

struct Quad
{
  Uint32 s;
  Uint32 p;
  Uint32 o;
  Uint32 g;
};
struct Triple
{
  Uint32 s;
  Uint32 p;
  Uint32 o;
};
struct Tuple
{
  Uint32 s;
  Uint32 p;
};
struct Single
{
  Uint32 s;
};

/* Clunky statics for shared NdbRecord stuff */
static const NdbDictionary::Column *pattr1Col;
static const NdbDictionary::Column *pattr2Col;
static const NdbDictionary::Column *pattr3Col;
static const NdbDictionary::Column *pattr4Col;

static const NdbRecord *pallColsRecord;
static const NdbRecord *psecondaryIndexRecord;

static int attr1ColNum;
static int attr2ColNum;
static int attr3ColNum;
static int attr4ColNum;

/**************************************************************
 * Initialise NdbRecord structures for table and index access *
 **************************************************************/
static void init_ndbrecord_info(Ndb &myNdb)
{
  /* Here we create various NdbRecord structures for accessing
   * data using the tables and indexes on api_recattr_vs_record
   * We could use the default NdbRecord structures, but then
   * we wouldn't have the nice ability to read and write rows
   * to and from the RowData and IndexRow structs
   */
  NdbDictionary::Dictionary* myDict= myNdb.getDictionary();
  const NdbDictionary::Table *myTable= myDict->getTable("test");
  
  NdbDictionary::RecordSpecification recordSpec[4];

  if (myTable == NULL) 
    APIERROR(myDict->getNdbError());

  pattr1Col = myTable->getColumn("s");
  if (pattr1Col == NULL) APIERROR(myDict->getNdbError());
  pattr2Col = myTable->getColumn("p");
  if (pattr2Col == NULL) APIERROR(myDict->getNdbError());
  pattr3Col = myTable->getColumn("o");
  if (pattr3Col == NULL) APIERROR(myDict->getNdbError());
  pattr4Col = myTable->getColumn("g");
  if (pattr4Col == NULL) APIERROR(myDict->getNdbError());
  
  attr1ColNum = pattr1Col->getColumnNo();
  attr2ColNum = pattr2Col->getColumnNo();
  attr3ColNum = pattr3Col->getColumnNo();
  attr4ColNum = pattr4Col->getColumnNo();
  printf("attr: %d %d %d %d\n", attr1ColNum,attr2ColNum,attr3ColNum,attr4ColNum);
  printf("off: %zu %zu %zu %zu \n",
         offsetof(Quad, s), offsetof(Quad, p), offsetof(Quad, o), offsetof(Quad, g));
    
  // s = ATTR 1
  recordSpec[0].column = pattr1Col;
  recordSpec[0].offset = offsetof(Quad, s);
  recordSpec[0].nullbit_byte_offset = 0; // Not nullable 
  recordSpec[0].nullbit_bit_in_byte = 0;  
        
  // p = ATTR 2
  recordSpec[1].column = pattr2Col;
  recordSpec[1].offset = offsetof(Quad, p);
  recordSpec[1].nullbit_byte_offset = 0;   // Not nullable
  recordSpec[1].nullbit_bit_in_byte = 0;   

  // o = ATTR 3
  recordSpec[2].column = pattr3Col;
  recordSpec[2].offset = offsetof(Quad, o);
  recordSpec[2].nullbit_byte_offset = 0;   // Not nullable
  recordSpec[2].nullbit_bit_in_byte = 0;

  // g = ATTR 4
  recordSpec[3].column = pattr4Col;
  recordSpec[3].offset = offsetof(Quad, g);
  recordSpec[3].nullbit_byte_offset = 0;   // Not nullable
  recordSpec[3].nullbit_bit_in_byte = 0;

  printf("attr: %d %d %d %d\n",
         recordSpec[0].column->getColumnNo(),
         recordSpec[1].column->getColumnNo(),
         recordSpec[2].column->getColumnNo(),
         recordSpec[3].column->getColumnNo());
  printf("off: %u %u %u %u \n",
         recordSpec[0].offset,
         recordSpec[1].offset,
         recordSpec[2].offset,
         recordSpec[3].offset);

  /* Create table record with all the columns */
  pallColsRecord = 
    myDict->createRecord(myTable, recordSpec, 4, sizeof(recordSpec[0]));
        
  if (pallColsRecord == NULL) APIERROR(myDict->getNdbError());

  /* Create Index NdbRecord for secondary index access
   * Note that we use the columns from the table to define the index
   * access record
   */
  const NdbDictionary::Index *mySIndex= myDict->getIndex("gspo", "test");

  recordSpec[0].column= pattr4Col;
  recordSpec[0].offset= offsetof(QuadGSPO, g);
  recordSpec[0].nullbit_byte_offset=0;
  recordSpec[0].nullbit_bit_in_byte=0;

  recordSpec[1].column= pattr1Col;
  recordSpec[1].offset= offsetof(QuadGSPO, s);
  recordSpec[1].nullbit_byte_offset=0;
  recordSpec[1].nullbit_bit_in_byte=1;

  recordSpec[2].column= pattr2Col;
  recordSpec[2].offset= offsetof(QuadGSPO, p);
  recordSpec[2].nullbit_byte_offset=0;
  recordSpec[2].nullbit_bit_in_byte=1;

  recordSpec[3].column= pattr3Col;
  recordSpec[3].offset= offsetof(QuadGSPO, o);
  recordSpec[3].nullbit_byte_offset=0;
  recordSpec[3].nullbit_bit_in_byte=1;

  /* Create NdbRecord for accessing via secondary index */
  psecondaryIndexRecord = 
    myDict->createRecord(mySIndex, 
                         recordSpec, 
                         4, 
                         sizeof(recordSpec[0]));


  if (psecondaryIndexRecord == NULL) 
    APIERROR(myDict->getNdbError());

}

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
    
/*
  NdbIndexScanOperation *ixScan = 
    myTransaction->scanIndex(mySIndex->getDefaultRecord(),
                             test->getDefaultRecord());
*/
  NdbIndexScanOperation *ixScan = 
    myTransaction->scanIndex(psecondaryIndexRecord,
                             pallColsRecord);
  /*
  NdbScanOperation::ScanOptions options;
  options.optionsPresent=NdbScanOperation::ScanOptions::SO_SCANFLAGS;
  Uint32 scanFlags = NdbScanOperation::SF_OrderBy; 
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

  //  Tuple low={662743, 2219900};
  // Tuple high={662743, 3000000};
  Tuple low={1106, 2000000};
  Tuple high={662743, 2200000};
  // Tuple low={1, NULL};
  // Tuple high={662743, NULL};
  NdbIndexScanOperation::IndexBound bound;
  bound.low_key= (char*)&low;
  bound.low_key_count= 2;
  bound.low_inclusive= true;
  bound.high_key= (char*)&high;
  bound.high_key_count= 2;
  bound.high_inclusive= true;
  bound.range_no= 0;

  /*
  Single val={662743};
  NdbIndexScanOperation::IndexBound bound;
  bound.low_key= (char*)&val;
  bound.low_key_count= 1;
  bound.low_inclusive= true;
  bound.high_key= (char*)&val;
  bound.high_key_count= 1;
  bound.high_inclusive= true;
  bound.range_no= 0;
  */
  
  // Single low = {1106};
  // Single high = {1109};
  // /*
  // Uint32 low2 = 283392;
  // Uint32 high2 = 283392;
  // //  Uint32 high = (Uint32)NULL;
  // printf("low: %u, high %u\n", low.s, high.s);
  // printf("low: %u, high %u\n", low.s, *((Uint32*)&high));
  // printf("size: %zu, %u, %zu, %u\n", sizeof(low), low.s, sizeof(low2), low2);
  // */
  // NdbIndexScanOperation::IndexBound bound;
  // bound.low_key= (char*)&low;
  // bound.low_key_count= 1;
  // bound.low_inclusive= true;
  // bound.high_key= (char*)&high;
  // bound.high_key_count= 1;
  // bound.high_inclusive= true;
  // bound.range_no= 0;

  if (ixScan->setBound(mySIndex->getDefaultRecord(), bound))
    APIERROR(myTransaction->getNdbError());

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
    }

  if (rc != 1)  APIERROR(myTransaction->getNdbError());

  ixScan->close(true);
  printf("Done printing\n");
}
