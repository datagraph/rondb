/*
 Copyright (c) 2013, Oracle and/or its affiliates. All rights
 reserved.
 
 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; version 2 of
 the License.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 02110-1301  USA
*/


/**********************
  This is the standard TypeConverter class used with DATETIME columns 
  in the Ndb Adapter.
  
  DATETIME columns are read and written using a MySQLTime structure,
  which provides lossless interchange.
  
  This Converter converts between MySQLTime and standard JavaScript Date.  

  While convenient, such a conversion can result in lost precision. 
  JavaSCript Date supports millisecond precision, while MySQL DATETIME
  can support up to microsecond precision.

  An application can override this converter and use MySQLTime directly:
    TODO FILL ME IN
  
  Or replace this converter with a custom one:
      
************************/


var MySQLTime = require("./MySQLTime.js"),
    unified_debug = require(path.join(api_dir, "unified_debug")),
    udebug = unified_debug.getLogger("NdbDatetimeConverter.js");


exports.toDB = function(jsdate) {
udebug.log("toDB", jsdate);
  var mysqlTime = new MySQLTime().initializeFromJsDate(jsdate);
  return mysqlTime;
};

exports.fromDB = function(mysqlTime) {
  // Date() constructor uses local time, but mysqlTime is UTC
  var jsdate = new Date(mysqlTime.year, mysqlTime.month - 1, mysqlTime.day);
  jsdate.setUTCHours(mysqlTime.hour);
  jsdate.setUTCMinutes(mysqlTime.minute);
  jsdate.setUTCSeconds(mysqlTime.second);
  jsdate.setUTCMilliseconds(mysqlTime.microsec / 1000);
  return jsdate;
};

