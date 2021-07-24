/*
   Copyright (c) 2012, 2021, Oracle and/or its affiliates.
   Copyright (c) 2021, 2021, Logical Clocks AB and/or its affiliates.

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

#ifdef TEST_COUNTINGPOOL

#include <ndb_global.h>
#include <NdbTap.hpp>
#include "CountingPool.hpp"
#include "Pool.hpp"
#include "RWPool.hpp"
#include "test_context.hpp"

#define JAM_FILE_ID 304

struct record
{
  int dummy;
};

template class CountingPool<RecordPool<RWPool<record> > >;

TAPTEST(CountingPool)
{
  (void)test_context(100);
  // Only compile test. See template instantiations above.

  OK(true);

  return 1;
}

#endif

