/*
   Copyright (C) 2009 Sun Microsystems Inc.
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

#ifndef NdbInfoRecAttr_H
#define NdbInfoRecAttr_H

class NdbInfoRecAttr {
public:
  Uint32 u_32_value() const {
    assert(m_len == sizeof(Uint32));
    return *((Uint32 *) m_data);
  }

  Uint64 u_64_value() const {
    assert(m_len == sizeof(Uint64));
    return *((Uint64 *) m_data);
  }

  const char* c_str() const {
    assert(m_len > 0);
    return m_data;
  }

  Uint32 length() const {
    return m_len;
  }

protected:
  friend class NdbInfoScanOperation;
  NdbInfoRecAttr() : m_data(NULL), m_len(0) {};
  ~NdbInfoRecAttr() {};
private:
  const char* m_data;
  Uint32 m_len;
};

#endif
