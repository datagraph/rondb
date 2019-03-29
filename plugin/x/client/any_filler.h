/*
 * Copyright (c) 2019, Oracle and/or its affiliates. All rights reserved.
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

#ifndef PLUGIN_X_CLIENT_ANY_FILLER_H_
#define PLUGIN_X_CLIENT_ANY_FILLER_H_

#include <string>

#include "plugin/x/client/mysqlxclient/xargument.h"
#include "plugin/x/client/mysqlxclient/xmessage.h"

namespace xcl {

class Any_filler : public Argument_value::Argument_visitor {
 public:
  explicit Any_filler(::Mysqlx::Datatypes::Any *any) : m_any(any) {}

 private:
  ::Mysqlx::Datatypes::Any *m_any;

  void visit() override {
    m_any->set_type(::Mysqlx::Datatypes::Any_Type_SCALAR);
    m_any->mutable_scalar()->set_type(::Mysqlx::Datatypes::Scalar_Type_V_NULL);
  }

  void visit(const int64_t value) override {
    m_any->set_type(::Mysqlx::Datatypes::Any_Type_SCALAR);
    m_any->mutable_scalar()->set_type(::Mysqlx::Datatypes::Scalar_Type_V_SINT);
    m_any->mutable_scalar()->set_v_signed_int(value);
  }

  void visit(const uint64_t value) override {
    m_any->set_type(::Mysqlx::Datatypes::Any_Type_SCALAR);
    m_any->mutable_scalar()->set_type(::Mysqlx::Datatypes::Scalar_Type_V_UINT);
    m_any->mutable_scalar()->set_v_unsigned_int(value);
  }

  void visit(const double value) override {
    m_any->set_type(::Mysqlx::Datatypes::Any_Type_SCALAR);
    m_any->mutable_scalar()->set_type(
        ::Mysqlx::Datatypes::Scalar_Type_V_DOUBLE);
    m_any->mutable_scalar()->set_v_double(value);
  }

  void visit(const float value) override {
    m_any->set_type(::Mysqlx::Datatypes::Any_Type_SCALAR);
    m_any->mutable_scalar()->set_type(::Mysqlx::Datatypes::Scalar_Type_V_FLOAT);
    m_any->mutable_scalar()->set_v_float(value);
  }

  void visit(const bool value) override {
    m_any->set_type(::Mysqlx::Datatypes::Any_Type_SCALAR);
    m_any->mutable_scalar()->set_type(::Mysqlx::Datatypes::Scalar_Type_V_BOOL);
    m_any->mutable_scalar()->set_v_bool(value);
  }

  void visit(const Object &obj) override {
    m_any->set_type(::Mysqlx::Datatypes::Any_Type_OBJECT);
    auto any_object = m_any->mutable_obj();

    for (const auto &key_value : obj) {
      auto fld = any_object->add_fld();
      Any_filler filler(fld->mutable_value());

      fld->set_key(key_value.first);
      key_value.second.accept(&filler);
    }
  }

  void visit(const Argument_object &obj) override {
    m_any->set_type(::Mysqlx::Datatypes::Any_Type_OBJECT);
    auto any_object = m_any->mutable_obj();

    for (const auto &key_value : obj) {
      auto fld = any_object->add_fld();
      Any_filler filler(fld->mutable_value());

      fld->set_key(key_value.first);
      key_value.second.accept(&filler);
    }
  }

  void visit(const Arguments &values) override {
    m_any->set_type(::Mysqlx::Datatypes::Any_Type_ARRAY);
    auto any_array = m_any->mutable_array();

    for (const auto &value : values) {
      Any_filler filler(any_array->add_value());
      value.accept(&filler);
    }
  }

  void visit(const std::string &value,
             const Argument_value::String_type st) override {
    m_any->set_type(::Mysqlx::Datatypes::Any_Type_SCALAR);

    switch (st) {
      case Argument_value::String_type::TString:
      case Argument_value::String_type::TDecimal:
        m_any->mutable_scalar()->set_type(
            ::Mysqlx::Datatypes::Scalar_Type_V_STRING);
        m_any->mutable_scalar()->mutable_v_string()->set_value(value);
        break;

      case Argument_value::String_type::TOctets:
        m_any->mutable_scalar()->set_type(
            ::Mysqlx::Datatypes::Scalar_Type_V_OCTETS);
        m_any->mutable_scalar()->mutable_v_octets()->set_value(value);
        break;
    }
  }
};

}  // namespace xcl

#endif  // PLUGIN_X_CLIENT_ANY_FILLER_H_
