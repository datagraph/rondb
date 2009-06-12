/*
   Copyright (C) 2003 MySQL AB
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

#include "ConsoleLogHandler.hpp"

ConsoleLogHandler::ConsoleLogHandler(const NdbOut& out)
 : LogHandler(), _out(out)
{
}

ConsoleLogHandler::~ConsoleLogHandler()
{

}

bool
ConsoleLogHandler::open()
{
  return true;
}

bool
ConsoleLogHandler::close()
{
  return true;
}

bool
ConsoleLogHandler::is_open()
{
  return true;
}

//
// PROTECTED
//
void 
ConsoleLogHandler::writeHeader(const char* pCategory, Logger::LoggerLevel level)
{
  char str[LogHandler::MAX_HEADER_LENGTH];
  ndbout << getDefaultHeader(str, pCategory, level);	
}

void 
ConsoleLogHandler::writeMessage(const char* pMsg)
{
  ndbout << pMsg;	
}

void 
ConsoleLogHandler::writeFooter()
{
  ndbout << getDefaultFooter() << flush;
}

  
bool
ConsoleLogHandler::setParam(const BaseString &param, const BaseString &value) {
  return false;
}
