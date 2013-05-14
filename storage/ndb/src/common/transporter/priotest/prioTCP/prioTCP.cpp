/* Copyright (c) 2003, 2005 MySQL AB


   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA */



#include <prioTransporterTest.hpp>
#include <NdbMain.h>

NDB_COMMAND(prioTCP, "prioTCP", "prioTCP", "Test the TCP Transporter", 65535)
{
  basePortTCP = 17000;
  return prioTransporterTest(TestTCP, "prioTCP", argc, argv);
}

