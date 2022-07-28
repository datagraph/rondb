/*
  Copyright (c) 2015, 2022, Oracle and/or its affiliates.

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

#ifndef PRIVILEGE_INCLUDED
#define PRIVILEGE_INCLUDED

#include <string>

#include "client/dump/abstract_plain_sql_object_dump_task.h"
#include "my_inttypes.h"

namespace Mysql {
namespace Tools {
namespace Dump {

class Privilege : public Abstract_plain_sql_object_dump_task {
 public:
  Privilege(uint64 id, const std::string &name,
            const std::string &sql_formatted_definition);
};

}  // namespace Dump
}  // namespace Tools
}  // namespace Mysql

#endif
