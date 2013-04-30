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

"use strict";

var adapter            = require(path.join(build_dir, "ndb_adapter.node")),
    NdbInterpretedCode = adapter.ndb.ndbapi.NdbInterpretedCode,
    NdbScanFilter      = adapter.ndb.ndbapi.NdbScanFilter,
    udebug             = unified_debug.getLogger("NdbScanFilter.js");

function blah() {
  console.log("BLAH");
  console.log.apply(null, arguments);
  process.exit();
}

/* ParamRecordSpec describes how a parameter value is encoded into a buffer
*/
function ParamRecordSpec(column, offset, paramName) {
  this.column = column;
  this.offset = offset;
  this.param  = paramName;
}

/* ConstRecordSpec describes the encoding of query parameter constants
*/
function ConstRecordSpec(column, offset, value) {
  this.column = column;
  this.offset = offset;
  this.value  = value;
}

/* FilterSpec describes filter implementation; will be stored in QueryHandler
*/
function FilterSpec(predicate) {
  this.predicate       = predicate;  /* Query Predicate for this filter */
  this.isConst         = true;  /* true if filter uses constants only */
  this.constFilter = {          /* If no params are needed for filter, */
    ndbInterpretedCode : null,  /* isConst=true, and the filter is built */
    ndbScanFilter      : null   /* just once and stored here. */
  };
  this.constBuffer     = null;  /* Buffer for encoded literal constants; */
  this.constBufferSize = 0;     /* encoded just once in advance, */
  this.constLayout     = null;  /* according to this layout. */
  this.paramBufferSize = 0;     /* Buffer for encoded execution parameters. */
  this.paramLayout     = null;  /* Array of ParamRecordSpec in buffer order */
  this.dbTable         = null;  /* NdbDictionary::Table for this filter */   
}


/* Make a note in a node of the predicate tree.
   The note will be used to store all NDB-related analysis.
   Copy the nodes operator or comparator code into the ndb section.
   This should be called in the first-pass visitor.
*/
function markNode(node) {
  var opcode = null;
  if(typeof node.operationCode !== 'undefined') {
    opcode = node.operationCode;
  }
  node.ndb = {
    "opcode"     : opcode,
    "layout"     : null,
    "intervals"  : {}
  };
}


/*************************************** BufferManagerVisitor ************
 *
 * This is the first pass, run when the operation is declared.
 * 
 * Visit nodes, marking them for NdbScanFilter.  Calculate buffer layout 
 * and needed space.  End product will be used to define FilterSpec.
 */ 
function BufferManagerVisitor(dbTable) {
  this.dbTable         = dbTable;
  this.paramBufferSize = 0;
  this.paramLayout     = [];
  this.constBufferSize = 0;
  this.constLayout     = [];
}

/** Handle nodes QueryAnd, QueryOr */
BufferManagerVisitor.prototype.visitQueryNaryPredicate = function(node) {
  var i;
  markNode(node);
  for(i = 0 ; i < node.predicates.length ; i++) {
    node.predicates[i].visit(this);
  }
};

/** Handle nodes QueryEq, QueryNe, QueryLt, QueryLe, QueryGt, QueryGe */
BufferManagerVisitor.prototype.visitQueryComparator = function(node) {
  markNode(node);
  var colId = node.queryField.field.columnNumber;
  var col = this.dbTable.columns[colId];
  var spec;
  // if param
    spec = new ParamRecordSpec(col, this.paramBufferSize, node.parameter.name);
    this.paramBufferSize += col.columnSpace;
    this.paramLayout.push(spec);
  // else if constant
  node.ndb.layout = spec;   // store the layout in the query node
};

/** Handle node QueryNot */
BufferManagerVisitor.prototype.visitQueryUnaryPredicate = function(node) {
  markNode(node);
  node.predicates[0].visit(this);
};

/** Handle node QueryBetween */
BufferManagerVisitor.prototype.visitQueryBetweenOperator = function(node) {
  markNode(node);
  blah("visitQueryBetweenOperator", node);
};

/** Handle nodes QueryIsNull, QueryIsNotNull */
BufferManagerVisitor.prototype.visitQueryUnaryOperator = function(node) {
  markNode(node);
  blah("visitQueryUnaryOperator", node);
};


/************************************** FilterBuildingVisitor ************
 *
 * This is the second pass, run each time the operation is executed.
 * 
 * Visit nodes and build NdbScanFilter using actual parameters.
 */ 
function FilterBuildingVisitor(filterSpec, paramBuffer) {
  this.filterSpec         = filterSpec;
  this.paramBuffer        = paramBuffer;
  this.ndbInterpretedCode = NdbInterpretedCode.create(filterSpec.dbTable);
  this.ndbScanFilter      = NdbScanFilter.create(this.ndbInterpretedCode);
}

/** Handle nodes QueryAnd, QueryOr */
FilterBuildingVisitor.prototype.visitQueryNaryPredicate = function(node) {
  var i = 0;
  this.ndbScanFilter.begin(node.ndb.opcode);
  for(i = 0 ; i < node.predicates.length ; i++) {
    node.predicates[i].visit(this);
  }
  this.ndbScanFilter.end();
};

/** Handle nodes QueryEq, QueryNe, QueryLt, QueryLe, QueryGt, QueryGe */
FilterBuildingVisitor.prototype.visitQueryComparator = function(node) {
  var opcode = node.ndb.opcode;
  var layout = node.ndb.layout;
  this.ndbScanFilter.cmp(opcode, layout.column.columnNumber, 
                         this.paramBuffer, layout.offset, 
                         layout.column.columnSpace);
  // TODO: constants
};

/** Handle nodes QueryNot */
FilterBuildingVisitor.prototype.visitQueryUnaryPredicate = function(node) {
  this.ndbScanFilter.begin(node.ndb.opcode);  // A 1-member NAND group
  node.predicates[0].visit(this);
  this.ndbScanFilter.end();
};

/** Handle nodes QueryIsNull, QueryIsNotNull */
FilterBuildingVisitor.prototype.visitQueryUnaryOperator = function(node) {
  blah("visitQueryUnaryOperator", node);
};

/** Handle node QueryBetween */
FilterBuildingVisitor.prototype.visitQueryBetweenOperator = function(node) {
  blah("visitQueryBetweenOperator", node);
};


/*************************************************/

function prepareFilterSpec(queryHandler) {
  if(queryHandler.ndbFilterSpec) return;

  var i, v;
  var spec = new FilterSpec(queryHandler.predicate);
  
  /* 1st pass.  Mark table and calculate buffer sizes. */
  spec.dbTable = queryHandler.dbTableHandler.dbTable;
  var bufferManager = new BufferManagerVisitor(spec.dbTable);
  spec.predicate.visit(bufferManager);

  /* Encode buffer for constant parameters */
  if(bufferManager.constBufferSize > 0) {
    spec.constBufferSize = bufferManager.constBufferSize;
    spec.constLayout     = bufferManager.constLayout;
    spec.constBuffer     = new Buffer(spec.constBufferSize);
    for(i = 0 ; i < spec.constLayout.length ; i++) {
      v = spec.constLayout[i];
      adapter.ndb.impl.encoderWrite(v.column, v.value, spec.constBuffer, v.offset);
    }
  }

  /* Assembly */
  if(bufferManager.paramBufferSize === 0) {
    var filterBuildingVisitor = new FilterBuildingVisitor(this, null);
    this.predicate.visit(filterBuildingVisitor);
    this.constFilter.ndbScanFilter = filterBuildingVisitor.ndbScanFilter;
    this.constFilter.ndbInterpretedCode = filterBuildingVisitor.ndbInterpretedCode;
  }
  else {
    spec.isConst         = false;
    spec.paramBufferSize = bufferManager.paramBufferSize;
    spec.paramLayout     = bufferManager.paramLayout;
  }

  /* Attach the FilterSpec to the QueryHandler */
  queryHandler.ndbFilterSpec = spec;
}

function encodeParameters(filterSpec, params) {
  var i, f;
  var buffer = null;
  if(filterSpec.paramBufferSize) {
    buffer = new Buffer(filterSpec.paramBufferSize);
    for(i = 0; i < filterSpec.paramLayout.length ; i++) {
      f = filterSpec.paramLayout[i];
      adapter.ndb.impl.encoderWrite(f.column, params[f.param], buffer, f.offset);
    }
  }
  return buffer;
}

FilterSpec.prototype.getScanFilterCode = function(params) {
  if(this.isConst) {
    return this.constFilter.ndbInterpretedCode;
  }

  /* Encode the parameters */
  var paramBuffer = encodeParameters(this, params);

  /* Build the NdbScanFilter for this operation */
  var filterBuildingVisitor = new FilterBuildingVisitor(this, paramBuffer);
  this.predicate.visit(filterBuildingVisitor);
  
  return filterBuildingVisitor.ndbInterpretedCode;
};


exports.prepareFilterSpec = prepareFilterSpec;
