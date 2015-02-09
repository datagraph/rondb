/* Copyright (c) 2014, Oracle and/or its affiliates. All rights reserved.

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

#include "my_config.h"
#include <gtest/gtest.h>
#include <item.h>

#include "test_utils.h"

namespace item_param_unittest {
using my_testing::Server_initializer;

class ItemParamTest : public ::testing::Test
{
protected:
  virtual void SetUp()
  {
    m_initializer.SetUp();
    // An Item expects to be owned by current_thd->free_list, so allocate with
    // new, and do not delete it.
    m_item_param= new Item_param(POS(), 1);
  }

  virtual void TearDown()
  {
    m_initializer.TearDown();
  }

  Server_initializer m_initializer;
  Item_param *m_item_param;
};

TEST_F(ItemParamTest, convert_str_value)
{
  m_item_param->state= Item_param::LONG_DATA_VALUE;
  m_item_param->value.cs_info.final_character_set_of_str_value= NULL;
  EXPECT_TRUE(m_item_param->convert_str_value(m_initializer.thd()));
}
}
