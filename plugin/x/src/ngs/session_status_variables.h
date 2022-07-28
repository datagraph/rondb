/*
 * Copyright (c) 2015, 2022, Oracle and/or its affiliates.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.0,
 * as published by the Free Software Foundation.
 *
 * This program is also distributed with certain software (including
 * but not limited to OpenSSL) that is licensed under separate terms,
 * as designated in a particular file or component or in included license
 * documentation.  The authors of MySQL hereby grant you an additional
 * permission to link the program and your derivative works with the
 * separately licensed software that they have included with MySQL.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License, version 2.0, for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
 */

#ifndef PLUGIN_X_SRC_NGS_SESSION_STATUS_VARIABLES_H_
#define PLUGIN_X_SRC_NGS_SESSION_STATUS_VARIABLES_H_

#include "plugin/x/src/ngs/common_status_variables.h"

namespace ngs {

class Session_status_variables : public Common_status_variables {
 public:
  Session_status_variables() = default;
  Variable m_fatal_errors_sent;

 private:
  Session_status_variables(const Session_status_variables &) = delete;
};

}  // namespace ngs

#endif  // PLUGIN_X_SRC_NGS_SESSION_STATUS_VARIABLES_H_
