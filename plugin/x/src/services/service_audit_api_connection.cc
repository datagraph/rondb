/* Copyright (c) 2020, Oracle and/or its affiliates. All rights reserved.

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

#include "plugin/x/src/services/service_audit_api_connection.h"

#include "plugin/x/src/interface/service_audit_api_connection.h"
#include "plugin/x/src/interface/service_registry.h"

namespace xpl {

Service_audit_api_connection::Service_audit_api_connection(
    iface::Service_registry *registry)
    : m_registry(registry) {
  m_audit_api =
      reinterpret_cast<SERVICE_TYPE_NO_CONST(mysql_audit_api_connection) *>(
          m_registry->acquire("mysql_audit_api_connection"));
}

Service_audit_api_connection::~Service_audit_api_connection() {
  m_registry->release(reinterpret_cast<my_h_service>(m_audit_api));
}

int Service_audit_api_connection::emit(void *thd,
                                       mysql_event_connection_subclass_t type) {
  return m_audit_api->emit(thd, type);
}

bool Service_audit_api_connection::is_valid() const {
  return nullptr != m_audit_api;
}

}  // namespace xpl
