/*
 * Copyright (c) 2017, Oracle and/or its affiliates. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301  USA
 */


#ifndef _NGS_CAPABILITY_CLIENT_INTERACTIVE_H_
#define _NGS_CAPABILITY_CLIENT_INTERACTIVE_H_


#include "plugin/x/ngs/include/ngs/capabilities/handler.h"
#include "plugin/x/src/xpl_client.h"

namespace ngs {

class Client_interface;

class Capability_client_interactive : public Capability_handler {
public:
  Capability_client_interactive(Client_interface &client);

  virtual const std::string name() const { return "client.interactive"; }
  virtual bool is_supported() const { return true; }

  virtual void get(::Mysqlx::Datatypes::Any &any);
  virtual bool set(const ::Mysqlx::Datatypes::Any &any);

  virtual void commit();

private:
  Client_interface &m_client;
  bool m_value;
};

} // namespace ngs


#endif // _NGS_CAPABILITY_CLIENT_INTERACTIVE_H_
