/*
   Copyright (c) 2003, 2022, Oracle and/or its affiliates.

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

#include "API.hpp"
#include <NdbScanFilter.hpp>
#include <Vector.hpp>
#include <NdbOut.hpp>
#include <Interpreter.hpp>
#include <signaldata/AttrInfo.hpp>

#ifdef VM_TRACE
#ifdef NDB_USE_GET_ENV
#include <NdbEnv.h>
#define INT_DEBUG(x) \
  { const char* tmp = NdbEnv_GetEnv("INT_DEBUG", (char*)0, 0); \
  if (tmp != 0 && strlen(tmp) != 0) { ndbout << "INT:"; ndbout_c x; } }
#else
#define INT_DEBUG(x)
#endif
#else
#define INT_DEBUG(x)
#endif

class NdbScanFilterImpl {
public:
  NdbScanFilterImpl()
    : m_sql_cmp_semantics(false),
      m_label(0),
      m_current{(NdbScanFilter::Group)0, 0, 0, ~0U, ~0U},
      m_negate(false),
      m_stack(),
      m_stack2(),
      m_code(NULL),
      m_error(),
      m_associated_op(NULL)
  {}

  void reset()
  {
    m_label = 0;
    m_current = {(NdbScanFilter::Group)0, 0, 0, ~0U, ~0U};
    m_negate = false;
    m_stack.clear();
    m_stack2.clear();
    m_error.code = 0;

    // Discard any NdbInterpretedCode built so far
    m_code->reset();
  }

  struct State {
    NdbScanFilter::Group m_group;  // AND or OR
    Uint32 m_popCount;
    Uint32 m_ownLabel;
    Uint32 m_trueLabel;
    Uint32 m_falseLabel;
  };

  bool m_sql_cmp_semantics;
  int m_label;
  State m_current;
  bool m_negate;    //used for translating NAND/NOR to AND/OR
  Vector<State> m_stack;
  Vector<bool> m_stack2;    //to store info of m_negate
  NdbInterpretedCode * m_code;
  // Allow update error from const methods
  mutable NdbError m_error;

  /* Members for supporting old Api */
  NdbScanOperation *m_associated_op;

  int cond_col(Interpreter::UnaryCondition, Uint32 attrId);
  
  int cond_col_const(Interpreter::BinaryCondition, Uint32 attrId, 
		     const void * value, Uint32 len);

  int cond_col_col(Interpreter::BinaryCondition,
                   Uint32 attrId1, Uint32 attrId2);

  int cond_col_param(Interpreter::BinaryCondition,
                     Uint32 attrId, Uint32 paramId);

  /* This method propagates an error code from NdbInterpretedCode
   * back to the NdbScanFilter object
   */
  int propagateErrorFromCode()
  {
    NdbError codeError= m_code->getNdbError();

    /* Map interpreted code's 'Too many instructions in 
     * interpreted program' to FilterTooLarge error for 
     * NdbScanFilter
     */
    if (codeError.code == 4518) 
      m_error.code = NdbScanFilter::FilterTooLarge;
    else
      m_error.code = codeError.code;

    return -1;
  }

  /* This method performs any steps required once the
   * filter definition is complete
   */
  int handleFilterDefined()
  {
    /* Finalise the interpreted program */
    if (m_code->finalise() != 0)
      return propagateErrorFromCode();

    /* For old Api support, we set the passed-in operation's
     * interpreted code to be the code generated by the
     * scanfilter
     */
    if (m_associated_op != NULL)
    {
      m_associated_op->setInterpretedCode(m_code);
    }

    return 0;
  }

};


NdbScanFilter::NdbScanFilter(NdbInterpretedCode* code) :
  m_impl(* new NdbScanFilterImpl())
{
  DBUG_ENTER("NdbScanFilter::NdbScanFilter(NdbInterpretedCode)");
  if (unlikely(code == NULL))
  {
    /* NdbInterpretedCode not supported for operation type */
    m_impl.m_error.code = 4539; 
  }
  m_impl.m_code = code;
  DBUG_VOID_RETURN;
}

NdbScanFilter::NdbScanFilter(class NdbOperation * op) :
  m_impl(* new NdbScanFilterImpl())
{
  DBUG_ENTER("NdbScanFilter::NdbScanFilter(NdbOperation)");
  
  const NdbOperation::Type opType= op->getType();

  if (likely((opType == NdbOperation::TableScan) || 
             (opType == NdbOperation::OrderedIndexScan)))
  {
    NdbScanOperation* scanOp = (NdbScanOperation*)op;
    
    /* We ask the NdbScanOperation to allocate an InterpretedCode
     * object for us.  It will look after freeing it when 
     * necessary.  This allows the InterpretedCode object to 
     * survive after the NdbScanFilter has gone out of scope
     */
    NdbInterpretedCode* code= scanOp->allocInterpretedCodeOldApi();
    if (likely(code != NULL))
    {
      m_impl.m_code = code;
      m_impl.m_associated_op = scanOp;
      DBUG_VOID_RETURN;
    }
  }
  
  /* Fall through: NdbInterpretedCode not supported for operation type */
  m_impl.m_error.code = 4539;
  DBUG_VOID_RETURN;
}

NdbScanFilter::~NdbScanFilter()
{
  delete &m_impl;
}

void
NdbScanFilter::setSqlCmpSemantics()
{
  m_impl.m_sql_cmp_semantics = true;
}

void
NdbScanFilter::reset()
{
  m_impl.reset();
}

int
NdbScanFilter::begin(Group group){
  if (m_impl.m_error.code != 0) return -1;

  if (m_impl.m_stack2.push_back(m_impl.m_negate))
  {
    /* Memory allocation problem */
    m_impl.m_error.code= 4000;
    return -1;
  }

  /**
   * Note that even if NAND and NOR may be specified as input arg,
   * it is never stored in m_group. (Converted to negated AND/OR)
   */
  switch(group){
  case NdbScanFilter::AND:
    INT_DEBUG(("Begin(AND)"));
    if(m_impl.m_negate){
      group = NdbScanFilter::OR;
    }
    break;
  case NdbScanFilter::OR:
    INT_DEBUG(("Begin(OR)"));
    if(m_impl.m_negate){
      group = NdbScanFilter::AND;
    }
    break;
  /* Below is the only place we should ever see a NAND/NOR group */
  case NdbScanFilter::NAND:
    INT_DEBUG(("Begin(NAND)"));
    if(m_impl.m_negate){
      group = NdbScanFilter::AND;
    }else{
      group = NdbScanFilter::OR;
    }
    m_impl.m_negate = !m_impl.m_negate;
    break;
  case NdbScanFilter::NOR:
    INT_DEBUG(("Begin(NOR)"));
    if(m_impl.m_negate){
      group = NdbScanFilter::OR;
    }else{
      group = NdbScanFilter::AND;
    }
    m_impl.m_negate = !m_impl.m_negate;
    break;
  }

  assert(m_impl.m_current.m_group == 0 ||  // Initial cond
         m_impl.m_current.m_group == NdbScanFilter::AND ||
         m_impl.m_current.m_group == NdbScanFilter::OR);

  if(group == m_impl.m_current.m_group){
    switch(group){
    case NdbScanFilter::AND:
    case NdbScanFilter::OR:
      m_impl.m_current.m_popCount++;
      return 0;
    default:
      // NAND / NOR not expected, will be converted to AND/OR + negate
      assert(group != NdbScanFilter::NAND);
      assert(group != NdbScanFilter::NOR);
      break;
    }
  }

  NdbScanFilterImpl::State tmp = m_impl.m_current;
  if (m_impl.m_stack.push_back(m_impl.m_current))
  {
    /* Memory allocation problem */
    m_impl.m_error.code= 4000;
    return -1;
  }
  m_impl.m_current.m_group = group;
  m_impl.m_current.m_ownLabel = m_impl.m_label++;
  m_impl.m_current.m_popCount = 0;
  
  switch(group){
  case NdbScanFilter::AND:
    m_impl.m_current.m_falseLabel = m_impl.m_current.m_ownLabel;
    m_impl.m_current.m_trueLabel = tmp.m_trueLabel;
    if (m_impl.m_sql_cmp_semantics) {
      // An unknown in an AND-group makes entire group unknown -> branch out
      m_impl.m_code->set_sql_null_semantics(NdbInterpretedCode::BranchIfUnknown);
    }
    break;
  case NdbScanFilter::OR:
    m_impl.m_current.m_falseLabel = tmp.m_falseLabel;
    m_impl.m_current.m_trueLabel = m_impl.m_current.m_ownLabel;
    if (m_impl.m_sql_cmp_semantics) {
      // An unknown in an OR-group need us to continue looking for a 'true'.
      m_impl.m_code->set_sql_null_semantics(NdbInterpretedCode::ContinueIfUnknown);
    }
    break;
  default: 
    assert(group != NdbScanFilter::NAND); // Impossible
    assert(group != NdbScanFilter::NOR);  // Impossible
    /* Operator is not defined in NdbScanFilter::Group  */
    m_impl.m_error.code= 4260;
    return -1;
  }
  
  return 0;
}

int
NdbScanFilter::end(){
  if (m_impl.m_error.code != 0) return -1;

  if(m_impl.m_stack2.size() == 0){
    /* Invalid set of range scan bounds */
    m_impl.m_error.code= 4259;
    return -1;
  }
  m_impl.m_negate = m_impl.m_stack2.back();
  m_impl.m_stack2.erase(m_impl.m_stack2.size() - 1);

  switch(m_impl.m_current.m_group){
  case NdbScanFilter::AND:
    INT_DEBUG(("End(AND pc=%d)", m_impl.m_current.m_popCount));
    break;
  case NdbScanFilter::OR:
    INT_DEBUG(("End(OR pc=%d)", m_impl.m_current.m_popCount));
    break;
  default:
    assert(m_impl.m_current.m_group != NdbScanFilter::NAND); // Impossible
    assert(m_impl.m_current.m_group != NdbScanFilter::NOR);  // Impossible
    break;
  }

  if(m_impl.m_current.m_popCount > 0){
    m_impl.m_current.m_popCount--;
    return 0;
  }
  
  NdbScanFilterImpl::State tmp = m_impl.m_current;  
  if(m_impl.m_stack.size() == 0){
    /* Invalid set of range scan bounds */
    m_impl.m_error.code= 4259;
    return -1;
  }
  m_impl.m_current = m_impl.m_stack.back();
  m_impl.m_stack.erase(m_impl.m_stack.size() - 1);

  /**
   * Only AND/OR is possible, as NAND/NOR is converted to
   * negated OR/AND in begin().
   */
  switch(tmp.m_group){
  case NdbScanFilter::AND:
    if(tmp.m_trueLabel == (Uint32)~0){
      if (m_impl.m_code->interpret_exit_ok() == -1)
        return m_impl.propagateErrorFromCode();
    } else {
      assert(m_impl.m_current.m_group == NdbScanFilter::OR);
      if (m_impl.m_sql_cmp_semantics)
        m_impl.m_code->set_sql_null_semantics(NdbInterpretedCode::ContinueIfUnknown);
      if (m_impl.m_code->branch_label(tmp.m_trueLabel) == -1)
        return m_impl.propagateErrorFromCode();
    }
    break;
  case NdbScanFilter::OR:
    if(tmp.m_falseLabel == (Uint32)~0){
      if (m_impl.m_code->interpret_exit_nok() == -1)
        return m_impl.propagateErrorFromCode();
    } else {
      assert(m_impl.m_current.m_group == NdbScanFilter::AND);
      if (m_impl.m_sql_cmp_semantics)
        m_impl.m_code->set_sql_null_semantics(NdbInterpretedCode::BranchIfUnknown);
      if (m_impl.m_code->branch_label(tmp.m_falseLabel) == -1)
        return m_impl.propagateErrorFromCode();
    }
    break;
  default:
    assert(tmp.m_group != NdbScanFilter::NAND); // Impossible
    assert(tmp.m_group != NdbScanFilter::NOR);  // Impossible
    /* Operator is not defined in NdbScanFilter::Group */
    m_impl.m_error.code= 4260;
    return -1;
  }

  if (m_impl.m_code->def_label(tmp.m_ownLabel) == -1)
    return m_impl.propagateErrorFromCode();

  if(m_impl.m_stack.size() == 0){
    switch(tmp.m_group){
    case NdbScanFilter::AND:
      if (m_impl.m_code->interpret_exit_nok() == -1)
        return m_impl.propagateErrorFromCode();
      break;
    case NdbScanFilter::OR:
      if (m_impl.m_code->interpret_exit_ok() == -1)
        return m_impl.propagateErrorFromCode();
      break;
    default:
      assert(tmp.m_group != NdbScanFilter::NAND); // Impossible
      assert(tmp.m_group != NdbScanFilter::NOR);  // Impossible
      /* Operator is not defined in NdbScanFilter::Group */
      m_impl.m_error.code= 4260;
      return -1;
    }

    /* Handle the completion of the filter definition */
    return m_impl.handleFilterDefined();
  }
  return 0;
}

int
NdbScanFilter::istrue(){
  if(m_impl.m_error.code != 0) return -1;

  if (m_impl.m_current.m_group != NdbScanFilter::AND &&
      m_impl.m_current.m_group != NdbScanFilter::OR) {
    /* Operator is not defined in NdbScanFilter::Group */
    m_impl.m_error.code= 4260;
    return -1;
  }

  if(m_impl.m_current.m_trueLabel == (Uint32)~0){
    if (m_impl.m_code->interpret_exit_ok() == -1)
      return m_impl.propagateErrorFromCode();
  } else {
    if (m_impl.m_code->branch_label(m_impl.m_current.m_trueLabel) == -1)
      return m_impl.propagateErrorFromCode();
  }

  return 0;
}

int
NdbScanFilter::isfalse(){
  if (m_impl.m_error.code != 0) return -1;

  if (m_impl.m_current.m_group != NdbScanFilter::AND &&
      m_impl.m_current.m_group != NdbScanFilter::OR) {
    /* Operator is not defined in NdbScanFilter::Group */
    m_impl.m_error.code= 4260;
    return -1;
  }
  
  if(m_impl.m_current.m_falseLabel == (Uint32)~0){
    if (m_impl.m_code->interpret_exit_nok() == -1)
      return m_impl.propagateErrorFromCode();
  } else {
    if (m_impl.m_code->branch_label(m_impl.m_current.m_falseLabel) == -1)
      return m_impl.propagateErrorFromCode();
  }

  return 0;
}

/* One argument branch definition method signature */
typedef int (NdbInterpretedCode:: * Branch1)(Uint32 a1, Uint32 label);

/* Two argument branch definition method signature,
 * Compare the column with either a string value,
 * another column or a parameter (in attrInfo).
 */
typedef int (NdbInterpretedCode:: * StrBranch2)(const void *val, Uint32 len,
                                                Uint32 a1, Uint32 label);
typedef int (NdbInterpretedCode:: * Branch2Col)(Uint32 a1, Uint32 a2,
                                                Uint32 label);
typedef int (NdbInterpretedCode:: * ParamBranch2)(Uint32 paramNo,
                                                  Uint32 a1, Uint32 label);

/**
 * Table indexed by the BinaryCondition or UnaryCondition,
 * containing its negated condition. Used to find the correct
 * variant of branch_* in table*[] set up below.
 */
static constexpr Interpreter::BinaryCondition negateBinary[] {
  Interpreter::NE,           // EQ
  Interpreter::EQ,           // NE
  Interpreter::GE,           // LT
  Interpreter::GT,           // LE
  Interpreter::LE,           // GT
  Interpreter::LT,           // GE
  Interpreter::NOT_LIKE,     // LIKE
  Interpreter::LIKE,         // NOT LIKE
  Interpreter::AND_NE_MASK,  // AND EQ MASK
  Interpreter::AND_EQ_MASK,  // AND NE MASK
  Interpreter::AND_NE_ZERO,  // AND EQ ZERO
  Interpreter::AND_EQ_ZERO,  // AND NE ZERO
};

static constexpr Interpreter::UnaryCondition negateUnary[] {
  Interpreter::IS_NOT_NULL,  // IS NULL
  Interpreter::IS_NULL,      // IS NOT NULL
};

/**
 * General comment for branch tables table2[]..table5[]:
 *
 * Table of branch methods to use for the condition type
 * which the table is indexed by. (see comments in each line)
 * Table contain the branch function which will branch on
 * a found equality match, i.e the 'true' branch if the condition
 * is part of a set of OR'ed condition.
 *
 * OTOH, if the condition is part of a set of AND'ed conditions,
 * we need to branch to a 'false' outcome on a failed match.
 * In these cases we find the negated form of the condition in the
 * negateUnary/negateBinary arrays above and use this to index
 * the 'branch on false'-function in the branch tables.
 *
 * 'm_negate' is handled in a similar way as AND. If both a 'AND'
 * and a 'm_negate' is in effect, it is a double negation which
 * cancel out each other.
 *
 * NAND or NOR groups are not a concern here as they are converted
 * to a negated AND/OR in begin().
 */

static constexpr Branch1 table2[] = {
  &NdbInterpretedCode::branch_col_eq_null,  // IS NULL
  &NdbInterpretedCode::branch_col_ne_null,  // IS NOT NULL
};

static constexpr int tab2_sz = sizeof(table2)/sizeof(table2[0]);

int
NdbScanFilterImpl::cond_col(Interpreter::UnaryCondition op, Uint32 AttrId){
  
  if (m_error.code != 0) return -1;

  if((int)op < 0 || (int)op >= tab2_sz){
    /* Condition is out of bounds */
    m_error.code= 4262;
    return -1;
  }
  
  /**
   * Only AND/OR is possible, as NAND/NOR is converted to
   * negated OR/AND in begin().
   */
  if (m_current.m_group != NdbScanFilter::AND &&
      m_current.m_group != NdbScanFilter::OR) {
    /* Operator is not defined in NdbScanFilter::Group */
    m_error.code= 4260;
    return -1;
  }

  /**
   * Find the operation to branch to a conclusive true/false outcome.
   * Note that both AND and negate implies an inverted condition, having
   * both of them is a double negation, canceling out each other.
   */
  Interpreter::UnaryCondition branchOp;
  if ((m_current.m_group == NdbScanFilter::AND) != (m_negate))
    branchOp = negateUnary[op];
  else
    branchOp = op;

  Branch1 branch = table2[branchOp];
  if ((m_code->* branch)(AttrId, m_current.m_ownLabel) == -1)
    return propagateErrorFromCode();

  return 0;
}

int
NdbScanFilter::isnull(int AttrId){
  return m_impl.cond_col(Interpreter::IS_NULL, AttrId);
}

int
NdbScanFilter::isnotnull(int AttrId){
  return m_impl.cond_col(Interpreter::IS_NOT_NULL, AttrId);
}

/**
 * Branch table for comparing a column with a literal value.
 *
 * Note that there is an old legacy here, where TUP-execution of
 * the interpreter cmp-code has been implemented backwards, such that
 * it branch on non-matches :-( Thus we need to set up the array
 * of branch methods below, such that it test for the opposite of
 * the compare operators '>', '>=', '<' and '<='
 */
static constexpr StrBranch2 table3[] = {
  &NdbInterpretedCode::branch_col_eq,                // EQ
  &NdbInterpretedCode::branch_col_ne,                // NE
  &NdbInterpretedCode::branch_col_gt,                // LT
  &NdbInterpretedCode::branch_col_ge,                // LE
  &NdbInterpretedCode::branch_col_lt,                // GT
  &NdbInterpretedCode::branch_col_le,                // GE
  &NdbInterpretedCode::branch_col_like,              // LIKE
  &NdbInterpretedCode::branch_col_notlike,           // NOT LIKE
  &NdbInterpretedCode::branch_col_and_mask_eq_mask,  // AND EQ MASK
  &NdbInterpretedCode::branch_col_and_mask_ne_mask,  // AND NE MASK
  &NdbInterpretedCode::branch_col_and_mask_eq_zero,  // AND EQ ZERO
  &NdbInterpretedCode::branch_col_and_mask_ne_zero,  // AND NE ZERO
};

static constexpr int tab3_sz = sizeof(table3)/sizeof(table3[0]);

int
NdbScanFilterImpl::cond_col_const(Interpreter::BinaryCondition op,
                                  Uint32 AttrId,
                                  const void * value, Uint32 len) {
  if (m_error.code != 0) return -1;

  if(op < 0 || op >= tab3_sz){
    /* Condition is out of bounds */
    m_error.code= 4262;
    return -1;
  }

  /**
   * Only AND/OR is possible, as NAND/NOR is converted to
   * negated OR/AND in begin().
   */
  if (m_current.m_group != NdbScanFilter::AND &&
      m_current.m_group != NdbScanFilter::OR) {
    /* Operator is not defined in NdbScanFilter::Group */
    m_error.code= 4260;
    return -1;
  }

  const NdbDictionary::Table * table = m_code->getTable();
  if (table == nullptr) {
    /* NdbInterpretedCode instruction requires that table is set */
    m_error.code=4538;
    return -1;
  }

  const NdbDictionary::Column * col = table->getColumn(AttrId);
  if (col == nullptr) {
    /* Column is NULL */
    m_error.code= 4261;
    return -1;
  }

  /**
   * Find the operation to branch to a conclusive true/false outcome.
   * Note that both AND and negate implies an inverted condition, having
   * both of them is a double negation, canceling out each other.
   */
  Interpreter::BinaryCondition branchOp;
  if ((m_current.m_group == NdbScanFilter::AND) != (m_negate))
    branchOp = negateBinary[op];
  else
    branchOp = op;

  const StrBranch2 branch = table3[branchOp];
  if ((m_code->* branch)(value, len, AttrId, m_current.m_ownLabel) == -1)
    return propagateErrorFromCode();

  return 0;
}


/**
 * Branch table for comparing a column with another column.
 *
 * See comment for table3[] wrt. the confusing usage of branch
 * methods for LT, LE, GT and GE
 */
static constexpr Branch2Col table4[] = {
  &NdbInterpretedCode::branch_col_eq,      // EQ
  &NdbInterpretedCode::branch_col_ne,      // NE
  &NdbInterpretedCode::branch_col_gt,      // LT
  &NdbInterpretedCode::branch_col_ge,      // LE
  &NdbInterpretedCode::branch_col_lt,      // GT
  &NdbInterpretedCode::branch_col_le,      // GE
};

static constexpr int tab4_sz = sizeof(table4)/sizeof(table4[0]);

int
NdbScanFilterImpl::cond_col_col(Interpreter::BinaryCondition op,
                                Uint32 attrId1, Uint32 attrId2) {
  if (m_error.code != 0) return -1;

  if (op < 0 || op >= tab4_sz) {
    /* Condition is out of bounds */
    m_error.code= 4262;
    return -1;
  }

  /**
   * Only AND/OR is possible, as NAND/NOR is converted to
   * negated OR/AND in begin().
   */
  if (m_current.m_group != NdbScanFilter::AND &&
      m_current.m_group != NdbScanFilter::OR) {
    /* Operator is not defined in NdbScanFilter::Group */
    m_error.code= 4260;
    return -1;
  }

  const NdbDictionary::Table * table = m_code->getTable();
  if (table == nullptr) {
    /* NdbInterpretedCode instruction requires that table is set */
    m_error.code=4538;
    return -1;
  }

  const NdbDictionary::Column * col1 =  table->getColumn(attrId1);
  const NdbDictionary::Column * col2 =  table->getColumn(attrId2);
  if (col1 == nullptr || col2 == nullptr) {
    /* Column is NULL */
    m_error.code= 4261;
    return -1;
  }

  /**
   * Find the operation to branch to a conclusive true/false outcome.
   * Note that both AND and negate implies an inverted condition, having
   * both of them is a double negation, canceling out each other.
   */
  Interpreter::BinaryCondition branchOp;
  if ((m_current.m_group == NdbScanFilter::AND) != (m_negate))
    branchOp = negateBinary[op];
  else
    branchOp = op;

  const Branch2Col branch = table4[branchOp];
  if ((m_code->* branch)(attrId1, attrId2, m_current.m_ownLabel) == -1)
    return propagateErrorFromCode();

  return 0;
}

/**
 * Branch table for comparing a column with parameter (in attrInfo).
 *
 * See comment for table3[] wrt. the confusing usage of branch
 * methods for LT, LE, GT and GE
 */
static constexpr ParamBranch2 table5[] = {
  &NdbInterpretedCode::branch_col_eq_param,     // EQ
  &NdbInterpretedCode::branch_col_ne_param,     // NE
  &NdbInterpretedCode::branch_col_gt_param,     // LT
  &NdbInterpretedCode::branch_col_ge_param,     // LE
  &NdbInterpretedCode::branch_col_lt_param,     // GT
  &NdbInterpretedCode::branch_col_le_param,     // GE
};

static constexpr int tab5_sz = sizeof(table5)/sizeof(table5[0]);

int
NdbScanFilterImpl::cond_col_param(Interpreter::BinaryCondition op,
                                  Uint32 attrId, Uint32 paramId) {
  if (m_error.code != 0) return -1;

  if (op < 0 || op >= tab5_sz) {
    /* Condition is out of bounds */
    m_error.code= 4262;
    return -1;
  }

  /**
   * Only AND/OR is possible, as NAND/NOR is converted to
   * negated OR/AND in begin().
   */
  if (m_current.m_group != NdbScanFilter::AND &&
      m_current.m_group != NdbScanFilter::OR) {
    /* Operator is not defined in NdbScanFilter::Group */
    m_error.code= 4260;
    return -1;
  }

  const NdbDictionary::Table * table = m_code->getTable();
  if (table == nullptr) {
    /* NdbInterpretedCode instruction requires that table is set */
    m_error.code= 4538;
    return -1;
  }

  const NdbDictionary::Column * col = table->getColumn(attrId);
  if (col == nullptr ) {
    /* Column is NULL */
    m_error.code= 4261;
    return -1;
  }

  /**
   * Find the operation to branch to a conclusive true/false outcome.
   * Note that both AND and negate implies an inverted condition, having
   * both of them is a double negation, canceling out each other.
   */
  Interpreter::BinaryCondition branchOp;
  if ((m_current.m_group == NdbScanFilter::AND) != (m_negate))
    branchOp = negateBinary[op];
  else
    branchOp = op;

  const ParamBranch2 branch = table5[branchOp];
  if ((m_code->* branch)(attrId, paramId, m_current.m_ownLabel) == -1)
    return propagateErrorFromCode();

  return 0;
}

int
NdbScanFilter::cmp(BinaryCondition cond, int ColId, 
		   const void *val, Uint32 len)
{
  switch(cond){
  case COND_LE:
    return m_impl.cond_col_const(Interpreter::LE, ColId, val, len);
  case COND_LT:
    return m_impl.cond_col_const(Interpreter::LT, ColId, val, len);
  case COND_GE:
    return m_impl.cond_col_const(Interpreter::GE, ColId, val, len);
  case COND_GT:
    return m_impl.cond_col_const(Interpreter::GT, ColId, val, len);
  case COND_EQ:
    return m_impl.cond_col_const(Interpreter::EQ, ColId, val, len);
  case COND_NE:
    return m_impl.cond_col_const(Interpreter::NE, ColId, val, len);
  case COND_LIKE:
    return m_impl.cond_col_const(Interpreter::LIKE, ColId, val, len);
  case COND_NOT_LIKE:
    return m_impl.cond_col_const(Interpreter::NOT_LIKE, ColId, val, len);
  case COND_AND_EQ_MASK:
    return m_impl.cond_col_const(Interpreter::AND_EQ_MASK, ColId, val, len);
  case COND_AND_NE_MASK:
    return m_impl.cond_col_const(Interpreter::AND_NE_MASK, ColId, val, len);
  case COND_AND_EQ_ZERO:
    return m_impl.cond_col_const(Interpreter::AND_EQ_ZERO, ColId, val, len);
  case COND_AND_NE_ZERO:
    return m_impl.cond_col_const(Interpreter::AND_NE_ZERO, ColId, val, len);
  }
  return -1;
}

int
NdbScanFilter::cmp_param(BinaryCondition cond, int ColId, int ParamId)
{
  switch(cond){
  case COND_LE:
    return m_impl.cond_col_param(Interpreter::LE, ColId, ParamId);
  case COND_LT:
    return m_impl.cond_col_param(Interpreter::LT, ColId, ParamId);
  case COND_GE:
    return m_impl.cond_col_param(Interpreter::GE, ColId, ParamId);
  case COND_GT:
    return m_impl.cond_col_param(Interpreter::GT, ColId, ParamId);
  case COND_EQ:
    return m_impl.cond_col_param(Interpreter::EQ, ColId, ParamId);
  case COND_NE:
    return m_impl.cond_col_param(Interpreter::NE, ColId, ParamId);
  default:
    /* Condition is out of bounds */
    m_impl.m_error.code= 4262;
    return -1;
  }
  return -1;
}

int
NdbScanFilter::cmp(BinaryCondition cond, int ColId1, int ColId2)
{
  switch(cond){
  case COND_LE:
    return m_impl.cond_col_col(Interpreter::LE, ColId1, ColId2);
  case COND_LT:
    return m_impl.cond_col_col(Interpreter::LT, ColId1, ColId2);
  case COND_GE:
    return m_impl.cond_col_col(Interpreter::GE, ColId1, ColId2);
  case COND_GT:
    return m_impl.cond_col_col(Interpreter::GT, ColId1, ColId2);
  case COND_EQ:
    return m_impl.cond_col_col(Interpreter::EQ, ColId1, ColId2);
  case COND_NE:
    return m_impl.cond_col_col(Interpreter::NE, ColId1, ColId2);
  default:
    /* Condition is out of bounds */
    m_impl.m_error.code= 4262;
    return -1;
  }
}

static void update(NdbError& _err)
{
  NdbError & error = (NdbError &) _err;
  ndberror_struct ndberror = (ndberror_struct)error;
  ndberror_update(&ndberror);
  error = NdbError(ndberror);
}

const NdbError &
NdbScanFilter::getNdbError() const
{
  update(m_impl.m_error);
  return m_impl.m_error;
}

const NdbInterpretedCode*
NdbScanFilter::getInterpretedCode() const
{
  /* Return nothing if this is an old-style
   * ScanFilter as the InterpretedCode is 
   * entirely encapsulated
   */
  if (m_impl.m_associated_op != NULL)
    return NULL;

  return m_impl.m_code;
}

NdbOperation*
NdbScanFilter::getNdbOperation() const
{
  /* Return associated NdbOperation (or NULL
   * if we don't have one)
   */
  return m_impl.m_associated_op;
}

#if 0
int
main(void){
  if(0)
  {
    ndbout << "a > 7 AND b < 9 AND c = 4" << endl;
    NdbScanFilter f(0);
    f.begin(NdbScanFilter::AND);
    f.gt(0, 7);
    f.lt(1, 9);
    f.eq(2, 4);
    f.end();
    ndbout << endl;
  }

  if(0)
  {
    ndbout << "a > 7 OR b < 9 OR c = 4" << endl;
    NdbScanFilter f(0);
    f.begin(NdbScanFilter::OR);
    f.gt(0, 7);
    f.lt(1, 9);
    f.eq(2, 4);
    f.end();
    ndbout << endl;
  }

  if(0)
  {
    ndbout << "a > 7 AND (b < 9 OR c = 4)" << endl;
    NdbScanFilter f(0);
    f.begin(NdbScanFilter::AND);
    f.gt(0, 7);
    f.begin(NdbScanFilter::OR);
    f.lt(1, 9);
    f.eq(2, 4);
    f.end();
    f.end();
    ndbout << endl;
  }

  if(0)
  {
    ndbout << "a > 7 AND (b < 9 AND c = 4)" << endl;
    NdbScanFilter f(0);
    f.begin(NdbScanFilter::AND);
    f.gt(0, 7);
    f.begin(NdbScanFilter::AND);
    f.lt(1, 9);
    f.eq(2, 4);
    f.end();
    f.end();
    ndbout << endl;
  }

  if(0)
  {
    ndbout << "(a > 7 AND b < 9) AND c = 4" << endl;
    NdbScanFilter f(0);
    f.begin(NdbScanFilter::AND);
    f.begin(NdbScanFilter::AND);
    f.gt(0, 7);
    f.lt(1, 9);
    f.end();
    f.eq(2, 4);
    f.end();
    ndbout << endl;
  }

  if(1)
  {
    ndbout << "(a > 7 OR b < 9) AND (c = 4 OR c = 5)" << endl;
    NdbScanFilter f(0);
    f.begin(NdbScanFilter::AND);
    f.begin(NdbScanFilter::OR);
    f.gt(0, 7);
    f.lt(1, 9);
    f.end();
    f.begin(NdbScanFilter::OR);    
    f.eq(2, 4);
    f.eq(2, 5);
    f.end();
    f.end();
    ndbout << endl;
  }

  if(1)
  {
    ndbout << "(a > 7 AND b < 9) OR (c = 4 AND c = 5)" << endl;
    NdbScanFilter f(0);
    f.begin(NdbScanFilter::OR);
    f.begin(NdbScanFilter::AND);
    f.gt(0, 7);
    f.lt(1, 9);
    f.end();
    f.begin(NdbScanFilter::AND);    
    f.eq(2, 4);
    f.eq(2, 5);
    f.end();
    f.end();
    ndbout << endl;
  }

  if(1)
  {
    ndbout << 
      "((a > 7 AND b < 9) OR (c = 4 AND d = 5)) AND " 
      "((e > 6 AND f < 8) OR (g = 2 AND h = 3)) "  << endl;
    NdbScanFilter f(0);
    f.begin(NdbScanFilter::AND);
    f.begin(NdbScanFilter::OR);
    f.begin(NdbScanFilter::AND);
    f.gt(0, 7);
    f.lt(1, 9);
    f.end();
    f.begin(NdbScanFilter::AND);    
    f.eq(2, 4);
    f.eq(3, 5);
    f.end();
    f.end();

    f.begin(NdbScanFilter::OR);
    f.begin(NdbScanFilter::AND);
    f.gt(4, 6);
    f.lt(5, 8);
    f.end();
    f.begin(NdbScanFilter::AND);    
    f.eq(6, 2);
    f.eq(7, 3);
    f.end();
    f.end();
    f.end();
  }
  
  return 0;
}
#endif

template class Vector<NdbScanFilterImpl::State>;

