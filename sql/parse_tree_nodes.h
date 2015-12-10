/* Copyright (c) 2013, 2015, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#ifndef PARSE_TREE_NODES_INCLUDED
#define PARSE_TREE_NODES_INCLUDED

#include "my_global.h"
#include "parse_tree_helpers.h"      // PT_item_list
#include "parse_tree_hints.h"
#include "sp_head.h"                 // sp_head
#include "sql_class.h"               // THD
#include "sql_lex.h"                 // LEX
#include "sql_parse.h"               // add_join_natural
#include "sql_update.h"              // Sql_cmd_update
#include "sql_admin.h"               // Sql_cmd_shutdown etc.


template<enum_parsing_context Context> class PTI_context;


class PT_statement : public Parse_tree_node
{
public:
  virtual Sql_cmd *make_cmd(THD *thd)= 0;
};


class PT_select_lex : public Parse_tree_node
{
public:
  SELECT_LEX *value;
};


/**
  Convenience function that calls Parse_tree_node::contextualize() on the node
  if it's non-NULL. It also does some sanity checks.
*/
inline bool contextualize_safe(Parse_context *pc, Parse_tree_node *node)
{
  if (node == NULL)
    return false;

  return node->contextualize(pc);
}


/**
  Convenience function that calls Item::itemize() on the item if it's
  non-NULL.
*/
inline bool itemize_safe(Parse_context *pc, Item **item)
{
  if (*item == NULL)
    return false;
  return (*item)->itemize(pc, item);
}



class PT_order_expr : public Parse_tree_node, public ORDER
{
  typedef Parse_tree_node super;

public:
  PT_order_expr(Item *item_arg, bool is_asc)
  {
    item_ptr= item_arg;
    direction= is_asc ? ORDER::ORDER_ASC : ORDER::ORDER_DESC;
  }

  virtual bool contextualize(Parse_context *pc)
  {
    return super::contextualize(pc) || item_ptr->itemize(pc, &item_ptr);
  }
};


class PT_order_list : public Parse_tree_node
{
  typedef Parse_tree_node super;

public:
  SQL_I_List<ORDER> value;

public:
  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc))
      return true;
    for (ORDER *o= value.first; o != NULL; o= o->next)
    {
      if (static_cast<PT_order_expr *>(o)->contextualize(pc))
        return true;
    }
    return false;
  }

  void push_back(PT_order_expr *order)
  {
    order->item= &order->item_ptr;
    order->used_alias= false;
    order->used= 0;
    order->is_position= false;
    value.link_in_list(order, &order->next);
  }
};


class PT_gorder_list : public PT_order_list
{
  typedef PT_order_list super;

public:
  virtual bool contextualize(Parse_context *pc)
  {
    SELECT_LEX *sel= pc->select;
    if (sel->linkage != GLOBAL_OPTIONS_TYPE &&
        sel->olap != UNSPECIFIED_OLAP_TYPE &&
        (sel->linkage != UNION_TYPE || sel->braces))
    {
      my_error(ER_WRONG_USAGE, MYF(0),
               "CUBE/ROLLUP", "ORDER BY");
      return true;
    }

    return super::contextualize(pc);
  }
};


class PT_select_item_list : public PT_item_list
{
  typedef PT_item_list super;

public:
  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc))
      return true;

    pc->select->item_list= value;
    return false;
  }
};


class PT_limit_clause : public Parse_tree_node
{
  typedef Parse_tree_node super;

  Limit_options limit_options;

public:
  PT_limit_clause(const Limit_options &limit_options_arg)
  : limit_options(limit_options_arg)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc))
      return true;

    if (pc->select->master_unit()->is_union() && !pc->select->braces)
    {
      pc->select= pc->select->master_unit()->fake_select_lex;
      DBUG_ASSERT(pc->select != NULL);
    }

    if (limit_options.is_offset_first && limit_options.opt_offset != NULL &&
        limit_options.opt_offset->itemize(pc, &limit_options.opt_offset))
      return true;

    if (limit_options.limit->itemize(pc, &limit_options.limit))
      return true;

    if (!limit_options.is_offset_first && limit_options.opt_offset != NULL &&
        limit_options.opt_offset->itemize(pc, &limit_options.opt_offset))
      return true;

    pc->select->select_limit= limit_options.limit;
    pc->select->offset_limit= limit_options.opt_offset;
    pc->select->explicit_limit= true;

    pc->thd->lex->set_stmt_unsafe(LEX::BINLOG_STMT_UNSAFE_LIMIT);
    return false;
  }
};


class PT_table_ref_join_table;
class PT_table_reference : public Parse_tree_node
{
public:
  TABLE_LIST *value;

  virtual void nest() {}

  /**
    Lets us build a parse tree top-down, which is necessary due to the
    context-dependent nature of the join syntax. This function adds
    the <table_ref> cross join as the left-most leaf in this join tree.

    This function may only be called if this PT_table_reference is a join.
  */
  virtual PT_table_reference *add_cross_join(PT_table_ref_join_table *cj)
  {
    DBUG_ASSERT(false);
  }

  /**
    If this PT_table_reference is a join, returns the PT_join_table. This is used
    for building a parse tree top-down.
  */
  virtual PT_join_table *get_join_table() { return NULL; }
};


class PT_table_factor : public PT_table_reference
{
public:
  virtual PT_table_reference *add_cross_join(PT_table_ref_join_table *cj);
};

class PT_table_factor_table_ident : public PT_table_factor
{
  typedef PT_table_reference super;

  Table_ident *table_ident;
  List<String> *opt_use_partition;
  LEX_STRING *opt_table_alias;
  List<Index_hint> *opt_key_definition;

public:
  PT_table_factor_table_ident(Table_ident *table_ident_arg,
                              List<String> *opt_use_partition_arg,
                              LEX_STRING *opt_table_alias_arg,
                              List<Index_hint> *opt_key_definition_arg)
  : table_ident(table_ident_arg),
    opt_use_partition(opt_use_partition_arg),
    opt_table_alias(opt_table_alias_arg),
    opt_key_definition(opt_key_definition_arg)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc))
      return true;
    
    THD *thd= pc->thd;
    Yacc_state *yyps= &thd->m_parser_state->m_yacc;

    value= pc->select->add_table_to_list(thd, table_ident, opt_table_alias, 0,
                                         yyps->m_lock_type,
                                         yyps->m_mdl_type,
                                         opt_key_definition,
                                         opt_use_partition);
    if (value == NULL)
      return true;
    pc->select->add_joined_table(value);
    return false;
  }
};


class PT_derived_table_list;
class PT_nested_derived_table_list : public PT_table_reference
{
public:
  PT_nested_derived_table_list(PT_derived_table_list *derived_table_list) :
  m_derived_table_list(derived_table_list)
  {}

  virtual bool contextualize(Parse_context *pc);

private:
  PT_derived_table_list *m_derived_table_list;
};


#ifdef __GNUC__
#define IS_SINGLE_FLAG(X) (__builtin_popcount(X) == 1)
#else
#define IS_SINGLE_FLAG(X) true
#endif // __GNUC__


class PT_join_table : public Parse_tree_node
{
  typedef Parse_tree_node super;

protected:
  PT_table_reference *tab1_node;
  POS join_pos;
  PT_join_table_type m_type;
  PT_table_reference *tab2_node;

  TABLE_LIST *tr1;
  TABLE_LIST *tr2;


public:
  PT_join_table(PT_table_reference *tab1_node_arg, const POS &join_pos_arg,
                PT_join_table_type type, PT_table_reference *tab2_node_arg)
  : tab1_node(tab1_node_arg),
    join_pos(join_pos_arg),
    m_type(type),
    tab2_node(tab2_node_arg),
    tr1(NULL), tr2(NULL)
  {
    compile_time_assert(IS_SINGLE_FLAG(JTT_INNER));
    compile_time_assert(IS_SINGLE_FLAG(JTT_STRAIGHT));
    compile_time_assert(IS_SINGLE_FLAG(JTT_NATURAL));
    compile_time_assert(IS_SINGLE_FLAG(JTT_LEFT));
    compile_time_assert(IS_SINGLE_FLAG(JTT_RIGHT));

    DBUG_ASSERT(type == JTT_INNER ||
                type == JTT_STRAIGHT_INNER ||
                type == JTT_NATURAL_INNER ||
                type == JTT_NATURAL_LEFT ||
                type == JTT_NATURAL_RIGHT ||
                type == JTT_LEFT ||
                type == JTT_RIGHT);
  }

  PT_table_reference *add_cross_join(PT_table_ref_join_table* cj)
  {
    tab1_node= tab1_node->add_cross_join(cj);
    return tab1_node;
  }

  /**
    Adds the table as the right-hand side of this join. May only be done if
    that table is NULL.
  */
  void add_rhs(PT_table_factor *table)
  {
    DBUG_ASSERT(tab2_node == NULL);
    tab2_node= table;
  }

  bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc) || contextualize_tabs(pc))
      return true;

    if (m_type & (JTT_LEFT | JTT_RIGHT))
    {
      if (m_type & JTT_LEFT)
        tr2->outer_join|= JOIN_TYPE_LEFT;
      else
      {
        TABLE_LIST *inner_table= pc->select->convert_right_join();
        /* swap tr1 and tr2 */
        DBUG_ASSERT(inner_table == tr1);
        tr1= tr2;
        tr2= inner_table;
      }
    }

    if (m_type & JTT_NATURAL)
      add_join_natural(tr1, tr2, NULL, pc->select);
    
    if (m_type & JTT_STRAIGHT)
      tr2->straight= true;

    return false;
  }

  ~PT_join_table() = 0;

protected:
  bool contextualize_tabs(Parse_context *pc)
  {
    if (tr1 != NULL)
      return false; // already done
      
    if (tab1_node->contextualize(pc) || tab2_node->contextualize(pc))
      return true;

    tr1= tab1_node->value;
    tr2= tab2_node->value;

    if (tr1 == NULL || tr2 == NULL)
    {
      error(pc, join_pos);
      return true;
    }
    return false;
  }
};



class PT_table_factor_joined_table : public PT_table_reference
{
public:
  PT_table_factor_joined_table(PT_join_table *joined_table) :
  m_joined_table(joined_table)
  {}

  virtual bool contextualize(Parse_context *pc);

  virtual PT_table_reference *add_cross_join(PT_table_ref_join_table* cj)
  {
    m_joined_table->add_cross_join(cj);
    return this;
  }

private:
  PT_join_table *m_joined_table;
};


class PT_cross_join : public PT_join_table
{
public:

  PT_cross_join(PT_table_reference *tab1_node_arg, const POS &join_pos_arg,
                PT_join_table_type Type_arg, PT_table_reference *tab2_node_arg)
    : PT_join_table(tab1_node_arg, join_pos_arg, Type_arg, tab2_node_arg) {}

  void add_rhs(PT_table_reference *rhs)
  {
    DBUG_ASSERT(tab2_node == NULL);
    tab2_node= rhs;
  }
};


class PT_join_table_on : public PT_join_table
{
  Item *on;

public:
  PT_join_table_on(PT_table_reference *tab1_node_arg, const POS &join_pos_arg,
                   PT_join_table_type type, PT_table_reference *tab2_node_arg,
                   Item *on_arg)
    : PT_join_table(tab1_node_arg, join_pos_arg, type, tab2_node_arg), on(on_arg)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (this->contextualize_tabs(pc))
      return true;

    if (push_new_name_resolution_context(pc, this->tr1, this->tr2))
    {
      this->error(pc, this->join_pos);
      return true;
    }

    SELECT_LEX *sel= pc->select;
    sel->parsing_place= CTX_ON;

    if (PT_join_table::contextualize(pc) || on->itemize(pc, &on))
      return true;
    DBUG_ASSERT(sel == pc->select);

    add_join_on(this->tr2, on);
    pc->thd->lex->pop_context();
    DBUG_ASSERT(sel->parsing_place == CTX_ON);
    sel->parsing_place= CTX_NONE;
    return false;
  }
};


class PT_join_table_using : public PT_join_table
{
  List<String> *using_fields;

public:
  PT_join_table_using(PT_table_reference *tab1_node_arg,
                      const POS &join_pos_arg,
                      PT_join_table_type type,
                      PT_table_reference *tab2_node_arg,
                      List<String> *using_fields_arg)
    : PT_join_table(tab1_node_arg, join_pos_arg, type, tab2_node_arg),
      using_fields(using_fields_arg)
  {}

  /// A PT_join_table_using without a list of columns denotes a natural join.
  PT_join_table_using(PT_table_reference *tab1_node_arg,
                      const POS &join_pos_arg,
                      PT_join_table_type type,
                      PT_table_reference *tab2_node_arg)
    : PT_join_table(tab1_node_arg, join_pos_arg, type, tab2_node_arg),
      using_fields(NULL)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (PT_join_table::contextualize(pc))
      return true;

    add_join_natural(this->tr1, this->tr2, using_fields, pc->select);
    return false;
  }
};


class PT_table_ref_join_table : public PT_table_reference
{
  typedef PT_table_reference super;

  PT_join_table *join_table;

public:
  explicit PT_table_ref_join_table(PT_join_table *join_table_arg)
  : join_table(join_table_arg)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc) || join_table->contextualize(pc))
      return true;

    value= pc->select->nest_last_join(pc->thd);
    return value == NULL;
  }

  PT_table_reference *add_cross_join(PT_table_ref_join_table *cj)
  {
    join_table->add_cross_join(cj);
    return this;
  }

  virtual PT_join_table *get_join_table() { return join_table; }

  void add_rhs(PT_table_factor *table)
  {
    join_table->add_rhs(table);
  }
};


class PT_select_part2_derived : public Parse_tree_node
{
  typedef Parse_tree_node super;

  ulonglong opt_query_spec_options;
  PT_item_list *select_item_list;

public:
  PT_select_part2_derived(ulonglong opt_query_spec_options_arg,
                          PT_item_list *select_item_list_arg)
  : opt_query_spec_options(opt_query_spec_options_arg),
    select_item_list(select_item_list_arg)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc))
      return true;

    THD *thd= pc->thd;
    SELECT_LEX *select= pc->select;

    select->parsing_place= CTX_SELECT_LIST;

    if (select->validate_base_options(thd->lex, opt_query_spec_options))
      return true;
    select->set_base_options(opt_query_spec_options);
    if (opt_query_spec_options & SELECT_HIGH_PRIORITY)
    {
      Yacc_state *yyps= &thd->m_parser_state->m_yacc;
      yyps->m_lock_type= TL_READ_HIGH_PRIORITY;
      yyps->m_mdl_type= MDL_SHARED_READ;
    }

    if (select_item_list->contextualize(pc))
      return true;
    DBUG_ASSERT(select == pc->select);

    // Ensure we're resetting parsing place of the right select
    DBUG_ASSERT(select->parsing_place == CTX_SELECT_LIST);
    select->parsing_place= CTX_NONE;
    return false;
  }
};


class PT_group : public Parse_tree_node
{
  typedef Parse_tree_node super;

  PT_order_list *group_list;
  olap_type olap;

public:
  PT_group(PT_order_list *group_list_arg, olap_type olap_arg)
  : group_list(group_list_arg), olap(olap_arg)
  {}

  virtual bool contextualize(Parse_context *pc);
};


class PT_order : public Parse_tree_node
{
  typedef Parse_tree_node super;

  PT_order_list *order_list;

public:

  explicit PT_order(PT_order_list *order_list_arg)
  : order_list(order_list_arg)
  {}

  virtual bool contextualize(Parse_context *pc);
};


class PT_procedure_analyse : public Parse_tree_node
{
  typedef Parse_tree_node super;

  Proc_analyse_params params;

public:
  PT_procedure_analyse(const Proc_analyse_params &params_arg)
  : params(params_arg)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc))
      return true;
          
    THD *thd= pc->thd;
    LEX *lex= thd->lex;

    if (!lex->parsing_options.allows_select_procedure)
    {
      my_error(ER_VIEW_SELECT_CLAUSE, MYF(0), "PROCEDURE");
      return true;
    }

    if (lex->select_lex != pc->select)
    {
      my_error(ER_WRONG_USAGE, MYF(0), "PROCEDURE", "subquery");
      return true;
    }

    lex->proc_analyse= &params;
    lex->set_uncacheable(pc->select, UNCACHEABLE_SIDEEFFECT);
    return false;
  }
};


class PT_order_or_limit_order : public Parse_tree_node
{
  typedef Parse_tree_node super;

  PT_order *order;
  PT_limit_clause *opt_limit;

public:
  PT_order_or_limit_order(PT_order *order_arg, PT_limit_clause *opt_limit_arg)
  : order(order_arg), opt_limit(opt_limit_arg)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    return super::contextualize(pc) || order->contextualize(pc) ||
           (opt_limit != NULL && opt_limit->contextualize(pc));
  }
};


class PT_union_order_or_limit : public Parse_tree_node
{
  typedef Parse_tree_node super;

  Parse_tree_node *order_or_limit;

public:
  PT_union_order_or_limit(Parse_tree_node *order_or_limit_arg)
  : order_or_limit(order_or_limit_arg)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc))
      return true;

    DBUG_ASSERT(pc->select->linkage != GLOBAL_OPTIONS_TYPE);
    SELECT_LEX *fake= pc->select->master_unit()->fake_select_lex;
    if (fake)
    {
      fake->no_table_names_allowed= true;
      pc->select= fake;
    }
    pc->thd->where= "global ORDER clause";

    if (order_or_limit->contextualize(pc))
      return true;

    pc->select->no_table_names_allowed= 0;
    pc->thd->where= THD::DEFAULT_WHERE;
    return false;
  }
};


class PT_table_expression : public Parse_tree_node
{
  typedef Parse_tree_node super;

  Parse_tree_node *opt_from_clause;
  Item *opt_where;
  PT_group *opt_group;
  Item *opt_having;
  PT_order *opt_order;
  PT_limit_clause *opt_limit;
  PT_procedure_analyse *opt_procedure_analyse;
  Select_lock_type opt_select_lock_type;

public:
  PT_table_expression(Parse_tree_node *opt_from_clause_arg,
                      Item *opt_where_arg,
                      PT_group *opt_group_arg,
                      Item *opt_having_arg,
                      PT_order *opt_order_arg,
                      PT_limit_clause *opt_limit_arg,
                      PT_procedure_analyse *opt_procedure_analyse_arg,
                      const Select_lock_type &opt_select_lock_type_arg)
   : opt_from_clause(opt_from_clause_arg),
     opt_where(opt_where_arg),
     opt_group(opt_group_arg),
     opt_having(opt_having_arg),
     opt_order(opt_order_arg),
     opt_limit(opt_limit_arg),
     opt_procedure_analyse(opt_procedure_analyse_arg),
     opt_select_lock_type(opt_select_lock_type_arg)
    {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc) ||
        (opt_from_clause != NULL && opt_from_clause->contextualize(pc)) ||
        (opt_where != NULL && opt_where->itemize(pc, &opt_where)) ||
        (opt_group != NULL && opt_group->contextualize(pc)) ||
        (opt_having != NULL && opt_having->itemize(pc, &opt_having)))
      return true;

    pc->select->set_where_cond(opt_where);
    pc->select->set_having_cond(opt_having);

    if ((opt_order != NULL && opt_order->contextualize(pc)) ||
        (opt_limit != NULL && opt_limit->contextualize(pc)) ||
        (opt_procedure_analyse != NULL &&
         opt_procedure_analyse->contextualize(pc)))
      return true;

    if (opt_select_lock_type.is_set)
    {
      pc->select->set_lock_for_tables(opt_select_lock_type.lock_type);
      pc->thd->lex->safe_to_cache_query=
        opt_select_lock_type.is_safe_to_cache_query;
    }
    return false;
  }
};


class PT_select_derived_union_select : public PT_table_reference
{
  typedef PT_table_reference super;

  PT_table_reference *select_derived;
  Parse_tree_node *opt_union_order_or_limit;
  POS union_or_limit_pos;

public:
  PT_select_derived_union_select(PT_table_reference *select_derived_arg,
                                 Parse_tree_node *opt_union_order_or_limit_arg,
                                 const POS &union_or_limit_pos_arg)
  : select_derived(select_derived_arg),
    opt_union_order_or_limit(opt_union_order_or_limit_arg),
    union_or_limit_pos(union_or_limit_pos_arg)
  {}


  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc) ||
        select_derived->contextualize(pc) ||
        (opt_union_order_or_limit != NULL &&
         opt_union_order_or_limit->contextualize(pc)))
      return true;

    if (select_derived->value != NULL && opt_union_order_or_limit != NULL)
    {
      error(pc, union_or_limit_pos);
      return true;
    }

    value= select_derived->value;
    return false;
  }
};


class PT_table_factor_parenthesis : public PT_table_reference
{
  typedef PT_table_reference super;

  PT_table_reference *select_derived_union;
  LEX_STRING *opt_table_alias;
  POS alias_pos;

public:

  PT_table_factor_parenthesis(PT_table_reference *select_derived_union_arg,
                              LEX_STRING *opt_table_alias_arg,
                              const POS &alias_pos_arg)
  : select_derived_union(select_derived_union_arg),
    opt_table_alias(opt_table_alias_arg),
    alias_pos(alias_pos_arg)
  {}

  virtual bool contextualize(Parse_context *pc);
};


class PT_derived_table_list : public PT_table_factor
{
  typedef PT_table_reference super;

  POS pos;


  /**
    If the list has more than one element, the sub-list is found here, not in
    'tail' as one would perhaps expect.
  */
  PT_table_reference *head;


  /**
    Note that the 'tail' of this list is actually found in 'head'. since Bison
    constructs the list it is a left-deep tree. This element is always an
    <esc_table_ref>.
  */
  PT_table_reference *tail;
  bool m_is_nested;

public:
  PT_derived_table_list(const POS &pos,
                        PT_table_reference *head_arg,
                        PT_table_reference *tail_arg)
  : pos(pos), head(head_arg), tail(tail_arg), m_is_nested(false)
  {}

  void nest()
  {
    // See comment to this member.
    head->nest();
    m_is_nested= true;
  }

  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc) ||
        head->contextualize(pc) || tail->contextualize(pc))
      return true;

    if (head->value == NULL || tail->value == NULL)
    {
      error(pc, pos);
      return true;
    }
    if (m_is_nested)
      value= pc->select->nest_last_join(pc->thd);
    else
      value= tail->value;
    return false;
  }
};


class PT_table_reference_list : public PT_table_reference
{
  typedef Parse_tree_node super;

  POS m_pos;
  PT_table_reference *m_derived_table_list;

public:
  PT_table_reference_list(const POS &pos,
                          PT_table_reference *derived_table_list)
  : m_pos(pos), m_derived_table_list(derived_table_list)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc) || m_derived_table_list->contextualize(pc))
      return true;

    value= m_derived_table_list->value;
    SELECT_LEX *sel= pc->select;
    sel->context.table_list=
      sel->context.first_name_resolution_table=
        sel->table_list.first;
    return false;
  }
};


class PT_select_paren_derived : public Parse_tree_node
{
  typedef Parse_tree_node super;

  PT_hint_list *opt_hint_list;
  PT_select_part2_derived *select_part2_derived;
  PT_table_expression *table_expression;

public:
  PT_select_paren_derived(PT_hint_list *opt_hint_list_arg,
                          PT_select_part2_derived *select_part2_derived_arg,
                          PT_table_expression *table_expression_arg)
  : opt_hint_list(opt_hint_list_arg),
    select_part2_derived(select_part2_derived_arg),
    table_expression(table_expression_arg)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc))
      return true;

    pc->select->set_braces(true);

    if (select_part2_derived->contextualize(pc) ||
        table_expression->contextualize(pc))
      return true;

    if (setup_select_in_parentheses(pc->select))
      return true;

    if (opt_hint_list != NULL && opt_hint_list->contextualize(pc))
      return true;

    return false;
  }
};


class PT_query_expression_body : public Parse_tree_node
{
public:
  virtual bool is_union() const = 0;
  virtual void set_containing_qe(PT_query_expression *qe) {}
  virtual bool has_into_clause() const = 0;
};


class PT_internal_variable_name : public Parse_tree_node
{
public:
  sys_var_with_base value;
};


class PT_internal_variable_name_1d : public PT_internal_variable_name
{
  typedef PT_internal_variable_name super;

  LEX_STRING ident;

public:
  PT_internal_variable_name_1d(const LEX_STRING &ident_arg)
  : ident(ident_arg)
  {}

  virtual bool contextualize(Parse_context *pc);
};


/**
  Parse tree node class for 2-dimentional variable names (example: \@global.x)
*/
class PT_internal_variable_name_2d : public PT_internal_variable_name
{
  typedef PT_internal_variable_name super;

  POS pos;
  LEX_STRING ident1;
  LEX_STRING ident2;

public:
  PT_internal_variable_name_2d(const POS &pos,
                                const LEX_STRING &ident1_arg,
                                const LEX_STRING &ident2_arg)
  : pos(pos), ident1(ident1_arg), ident2(ident2_arg)
  {}

  virtual bool contextualize(Parse_context *pc);
};


class PT_internal_variable_name_default : public PT_internal_variable_name
{
  typedef PT_internal_variable_name super;

  LEX_STRING ident;

public:
  PT_internal_variable_name_default(const LEX_STRING &ident_arg)
  : ident(ident_arg)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc))
      return true;

    sys_var *tmp=find_sys_var(pc->thd, ident.str, ident.length);
    if (!tmp)
      return true;
    if (!tmp->is_struct())
    {
      my_error(ER_VARIABLE_IS_NOT_STRUCT, MYF(0), ident.str);
      return true;
    }
    value.var= tmp;
    value.base_name.str=    (char*) "default";
    value.base_name.length= 7;
    return false;
  }
};


class PT_option_value_following_option_type : public Parse_tree_node
{
  typedef Parse_tree_node super;

  POS pos;
  PT_internal_variable_name *name;
  Item *opt_expr;

public:
  PT_option_value_following_option_type(const POS &pos,
                                        PT_internal_variable_name *name_arg,
                                        Item *opt_expr_arg)
  : pos(pos), name(name_arg), opt_expr(opt_expr_arg)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc) || name->contextualize(pc) ||
        (opt_expr != NULL && opt_expr->itemize(pc, &opt_expr)))
      return true;

    if (name->value.var && name->value.var != trg_new_row_fake_var)
    {
      /* It is a system variable. */
      if (set_system_variable(pc->thd, &name->value, pc->thd->lex->option_type,
                              opt_expr))
        return true;
    }
    else
    {
      /*
        Not in trigger assigning value to new row,
        and option_type preceding local variable is illegal.
      */
      error(pc, pos);
      return true;
    }
    return false;
  }
};


class PT_option_value_no_option_type : public Parse_tree_node {};


class PT_option_value_no_option_type_internal :
  public PT_option_value_no_option_type
{
  typedef PT_option_value_no_option_type super;

  PT_internal_variable_name *name;
  Item *opt_expr;
  POS expr_pos;

public:
  PT_option_value_no_option_type_internal(PT_internal_variable_name *name_arg,
                                          Item *opt_expr_arg,
                                          const POS &expr_pos_arg)
  : name(name_arg), opt_expr(opt_expr_arg), expr_pos(expr_pos_arg)
  {}

  virtual bool contextualize(Parse_context *pc);
};


class PT_option_value_no_option_type_user_var :
  public PT_option_value_no_option_type
{
  typedef PT_option_value_no_option_type super;

  LEX_STRING name;
  Item *expr;

public:
  PT_option_value_no_option_type_user_var(const LEX_STRING &name_arg,
                                          Item *expr_arg)
  : name(name_arg), expr(expr_arg)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc) || expr->itemize(pc, &expr))
      return true;

    THD *thd= pc->thd;
    Item_func_set_user_var *item;
    item= new (pc->mem_root) Item_func_set_user_var(name, expr, false);
    if (item == NULL)
      return true;
    set_var_user *var= new set_var_user(item);
    if (var == NULL)
      return true;
    thd->lex->var_list.push_back(var);
    return false;
  }
};


class PT_option_value_no_option_type_sys_var :
  public PT_option_value_no_option_type
{
  typedef PT_option_value_no_option_type super;

  enum_var_type type;
  PT_internal_variable_name *name;
  Item *opt_expr;

public:
  PT_option_value_no_option_type_sys_var(enum_var_type type_arg,
                                          PT_internal_variable_name *name_arg,
                                          Item *opt_expr_arg)
  : type(type_arg), name(name_arg), opt_expr(opt_expr_arg)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc) || name->contextualize(pc) ||
        (opt_expr != NULL && opt_expr->itemize(pc, &opt_expr)))
      return true;

    THD *thd= pc->thd;
    struct sys_var_with_base tmp= name->value;
    /* Lookup if necessary: must be a system variable. */
    if (tmp.var == NULL)
    {
      if (find_sys_var_null_base(thd, &tmp))
        return true;
    }
    if (set_system_variable(thd, &tmp, type, opt_expr))
      return true;
    return false;
  }
};


class PT_option_value_no_option_type_charset :
  public PT_option_value_no_option_type
{
  typedef PT_option_value_no_option_type super;

  const CHARSET_INFO *opt_charset;

public:
  PT_option_value_no_option_type_charset(const CHARSET_INFO *opt_charset_arg)
  : opt_charset(opt_charset_arg)
  {}

  virtual bool contextualize(Parse_context *pc);
};


class PT_option_value_no_option_type_names :
  public PT_option_value_no_option_type
{
  typedef PT_option_value_no_option_type super;

  POS pos;

public:
  explicit PT_option_value_no_option_type_names(const POS &pos) : pos(pos) {}

  virtual bool contextualize(Parse_context *pc);
};


class PT_option_value_no_option_type_names_charset :
  public PT_option_value_no_option_type
{
  typedef PT_option_value_no_option_type super;

  const CHARSET_INFO *opt_charset;
  const CHARSET_INFO *opt_collation;

public:
  PT_option_value_no_option_type_names_charset(
    const CHARSET_INFO *opt_charset_arg,
    const CHARSET_INFO *opt_collation_arg)
  : opt_charset(opt_charset_arg), opt_collation(opt_collation_arg)
  {}

  virtual bool contextualize(Parse_context *pc);
};


class PT_start_option_value_list : public Parse_tree_node {};


class PT_option_value_no_option_type_password :
  public PT_start_option_value_list
{
  typedef PT_start_option_value_list super;

  const char *password;
  POS expr_pos;

public:
  explicit PT_option_value_no_option_type_password(const char *password_arg,
                                                   const POS &expr_pos_arg)
  : password(password_arg), expr_pos(expr_pos_arg)
  {}

  virtual bool contextualize(Parse_context *pc);
};


class PT_option_value_no_option_type_password_for :
  public PT_start_option_value_list 
{
  typedef PT_start_option_value_list super;

  LEX_USER *user;
  const char *password;
  POS expr_pos;

public:
  PT_option_value_no_option_type_password_for(LEX_USER *user_arg,
                                              const char *password_arg,
                                              const POS &expr_pos_arg)
  : user(user_arg), password(password_arg), expr_pos(expr_pos_arg)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc))
      return true;

    THD *thd= pc->thd;
    LEX *lex= thd->lex;
    set_var_password *var;

    /*
      In case of anonymous user, user->user is set to empty string with
      length 0. But there might be case when user->user.str could be NULL.
      For Ex: "set password for current_user() = password('xyz');".
      In this case, set user information as of the current user.
    */
    if (!user->user.str)
    {
      LEX_CSTRING sctx_priv_user= thd->security_context()->priv_user();
      DBUG_ASSERT(sctx_priv_user.str);
      user->user.str= sctx_priv_user.str;
      user->user.length= sctx_priv_user.length;
    }
    if (!user->host.str)
    {
      LEX_CSTRING sctx_priv_host= thd->security_context()->priv_host();
      DBUG_ASSERT(sctx_priv_host.str);
      user->host.str= (char *) sctx_priv_host.str;
      user->host.length= sctx_priv_host.length;
    }

    var= new set_var_password(user, const_cast<char *>(password));
    if (var == NULL)
      return true;
    lex->var_list.push_back(var);
    lex->sql_command= SQLCOM_SET_PASSWORD;
    if (lex->sphead)
      lex->sphead->m_flags|= sp_head::HAS_SET_AUTOCOMMIT_STMT;
    if (sp_create_assignment_instr(pc->thd, expr_pos.raw.end))
      return true;
    return false;
  }
};


class PT_option_value_type : public Parse_tree_node
{
  typedef Parse_tree_node super;

  enum_var_type type;
  PT_option_value_following_option_type *value;

public:
  PT_option_value_type(enum_var_type type_arg,
                        PT_option_value_following_option_type *value_arg)
  : type(type_arg), value(value_arg)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    pc->thd->lex->option_type= type;
    return super::contextualize(pc) || value->contextualize(pc);
  }
};


class PT_option_value_list_head : public Parse_tree_node
{
  typedef Parse_tree_node super;

  POS delimiter_pos;
  Parse_tree_node *value;
  POS value_pos;

public:
  PT_option_value_list_head(const POS &delimiter_pos_arg,
                            Parse_tree_node *value_arg,
                            const POS &value_pos_arg)
  : delimiter_pos(delimiter_pos_arg), value(value_arg), value_pos(value_pos_arg)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc))
      return true;

    THD *thd= pc->thd;
#ifndef DBUG_OFF
    LEX *old_lex= thd->lex;
#endif//DBUG_OFF

    sp_create_assignment_lex(thd, delimiter_pos.raw.end);
    DBUG_ASSERT(thd->lex->select_lex == thd->lex->current_select());
    Parse_context inner_pc(pc->thd, thd->lex->select_lex);

    if (value->contextualize(&inner_pc))
      return true;

    if (sp_create_assignment_instr(pc->thd, value_pos.raw.end))
      return true;
    DBUG_ASSERT(thd->lex == old_lex &&
                thd->lex->current_select() == pc->select);

    return false;
  }
};


class PT_option_value_list : public PT_option_value_list_head
{
  typedef PT_option_value_list_head super;

  PT_option_value_list_head *head;

public:
  PT_option_value_list(PT_option_value_list_head *head_arg,
                       const POS &delimiter_pos_arg,
                       Parse_tree_node *tail, const POS &tail_pos)
  : super(delimiter_pos_arg, tail, tail_pos), head(head_arg)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    return head->contextualize(pc) || super::contextualize(pc);
  }
};


class PT_start_option_value_list_no_type : public PT_start_option_value_list
{
  typedef PT_start_option_value_list super;

  PT_option_value_no_option_type *head;
  POS head_pos;
  PT_option_value_list_head *tail;

public:
  PT_start_option_value_list_no_type(PT_option_value_no_option_type *head_arg,
                                     const POS &head_pos_arg,
                                     PT_option_value_list_head *tail_arg)
  : head(head_arg), head_pos(head_pos_arg), tail(tail_arg)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc) || head->contextualize(pc))
      return true;

    if (sp_create_assignment_instr(pc->thd, head_pos.raw.end))
      return true;
    DBUG_ASSERT(pc->thd->lex->select_lex == pc->thd->lex->current_select());
    pc->select= pc->thd->lex->select_lex;

    if (tail != NULL && tail->contextualize(pc))
      return true;

    return false;
  }
};


class PT_transaction_characteristic : public Parse_tree_node
{
  typedef Parse_tree_node super;

  const char *name;
  int32 value;

public:
  PT_transaction_characteristic(const char *name_arg, int32 value_arg)
  : name(name_arg), value(value_arg)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc))
      return true;

    THD *thd= pc->thd;
    LEX *lex= thd->lex;
    Item *item= new (pc->mem_root) Item_int(value);
    if (item == NULL)
      return true;
    set_var *var= new set_var(lex->option_type,
                              find_sys_var(thd, name),
                              &null_lex_str,
                              item);
    if (var == NULL)
      return true;
    lex->var_list.push_back(var);
    return false;
  }

};


class PT_transaction_access_mode : public PT_transaction_characteristic
{
  typedef PT_transaction_characteristic super;

public:
  explicit PT_transaction_access_mode(bool is_read_only)
  : super("tx_read_only", (int32) is_read_only)
  {}
};


class PT_isolation_level : public PT_transaction_characteristic
{
  typedef PT_transaction_characteristic super;

public:
  explicit PT_isolation_level(enum_tx_isolation level)
  : super("tx_isolation", (int32) level)
  {}
};


class PT_transaction_characteristics : public Parse_tree_node
{
  typedef Parse_tree_node super;

  PT_transaction_characteristic *head;
  PT_transaction_characteristic *opt_tail;

public:
  PT_transaction_characteristics(PT_transaction_characteristic *head_arg,
                                 PT_transaction_characteristic *opt_tail_arg)
  : head(head_arg), opt_tail(opt_tail_arg)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    return (super::contextualize(pc) || head->contextualize(pc) ||
            (opt_tail != NULL && opt_tail->contextualize(pc)));
  }
};


class PT_start_option_value_list_transaction :
  public PT_start_option_value_list
{
  typedef PT_start_option_value_list super;

  PT_transaction_characteristics * characteristics;
  POS end_pos;

public:
  PT_start_option_value_list_transaction(
    PT_transaction_characteristics * characteristics_arg,
    const POS &end_pos_arg)
  : characteristics(characteristics_arg), end_pos(end_pos_arg)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc))
      return true;
  
    THD *thd= pc->thd;
    thd->lex->option_type= OPT_DEFAULT;
    if (characteristics->contextualize(pc))
      return true;

    if (sp_create_assignment_instr(thd, end_pos.raw.end))
      return true;
    DBUG_ASSERT(pc->thd->lex->select_lex == pc->thd->lex->current_select());
    pc->select= pc->thd->lex->select_lex;

    return false;
  }
};


class PT_start_option_value_list_following_option_type :
  public Parse_tree_node
{};


class PT_start_option_value_list_following_option_type_eq :
  public PT_start_option_value_list_following_option_type
{
  typedef PT_start_option_value_list_following_option_type super;

  PT_option_value_following_option_type *head;
  POS head_pos;
  PT_option_value_list_head *opt_tail;

public:
  PT_start_option_value_list_following_option_type_eq(
    PT_option_value_following_option_type *head_arg,
    const POS &head_pos_arg,
    PT_option_value_list_head *opt_tail_arg)
  : head(head_arg), head_pos(head_pos_arg), opt_tail(opt_tail_arg)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc) || head->contextualize(pc))
      return true;

    if (sp_create_assignment_instr(pc->thd, head_pos.raw.end))
      return true; 
    DBUG_ASSERT(pc->thd->lex->select_lex == pc->thd->lex->current_select());
    pc->select= pc->thd->lex->select_lex;

    if (opt_tail != NULL && opt_tail->contextualize(pc))
      return true;

    return false;
  }
};


class PT_start_option_value_list_following_option_type_transaction :
  public PT_start_option_value_list_following_option_type
{
  typedef PT_start_option_value_list_following_option_type super;

  PT_transaction_characteristics *characteristics;
  POS characteristics_pos;

public:
  PT_start_option_value_list_following_option_type_transaction(
    PT_transaction_characteristics *characteristics_arg,
    const POS &characteristics_pos_arg)
  : characteristics(characteristics_arg),
    characteristics_pos(characteristics_pos_arg)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc) || characteristics->contextualize(pc))
      return true;

    if (sp_create_assignment_instr(pc->thd, characteristics_pos.raw.end))
      return true; 
    DBUG_ASSERT(pc->thd->lex->select_lex == pc->thd->lex->current_select());
    pc->select= pc->thd->lex->select_lex;

    return false;
  }
};


class PT_start_option_value_list_type : public PT_start_option_value_list
{
  typedef PT_start_option_value_list super;

  enum_var_type type;
  PT_start_option_value_list_following_option_type *list;

public:
  PT_start_option_value_list_type(
    enum_var_type type_arg,
    PT_start_option_value_list_following_option_type *list_arg)
  : type(type_arg), list(list_arg)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    pc->thd->lex->option_type= type;
    return super::contextualize(pc) || list->contextualize(pc);
  }
};


class PT_set : public Parse_tree_node
{
  typedef Parse_tree_node super;

  POS set_pos;
  PT_start_option_value_list *list;

public:
  PT_set(const POS &set_pos_arg, PT_start_option_value_list *list_arg)
  : set_pos(set_pos_arg), list(list_arg)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc))
      return true;
  
    THD *thd= pc->thd;
    LEX *lex= thd->lex;
    lex->sql_command= SQLCOM_SET_OPTION;
    lex->option_type= OPT_SESSION;
    lex->var_list.empty();
    lex->autocommit= false;

    sp_create_assignment_lex(thd, set_pos.raw.end);
    DBUG_ASSERT(pc->thd->lex->select_lex == pc->thd->lex->current_select());
    pc->select= pc->thd->lex->select_lex;

    return list->contextualize(pc);
  }
};


class PT_select_init : public Parse_tree_node {};


class PT_union_list : public Parse_tree_node
{
  typedef Parse_tree_node super;

  bool is_distinct;
  PT_select_init *select_init;

public:
  PT_union_list(bool is_distinct_arg, PT_select_init *select_init_arg)
  : is_distinct(is_distinct_arg), select_init(select_init_arg)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc))
      return true;
  
    pc->select= pc->thd->lex->new_union_query(pc->select, is_distinct);
    if (pc->select == NULL)
      return true;

    if (select_init->contextualize(pc))
      return true;
    /*
      Remove from the name resolution context stack the context of the
      last query block in the union.
    */
    pc->thd->lex->pop_context();
    return false;
  }
};


class PT_into_destination : public Parse_tree_node
{
  typedef Parse_tree_node super;

public:
  PT_into_destination(const POS &pos) : m_pos(pos) {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc))
      return true;

    LEX *lex= pc->thd->lex;
    if (!pc->thd->lex->parsing_options.allows_select_into)
    {
      if (lex->sql_command == SQLCOM_SHOW_CREATE ||
          lex->sql_command == SQLCOM_CREATE_VIEW)
        my_error(ER_VIEW_SELECT_CLAUSE, MYF(0), "INTO");
      else
        error(pc, m_pos);
      return true;
    }
    return false;
  }

private:
  POS m_pos;
};


class PT_into_destination_outfile : public PT_into_destination
{
  typedef PT_into_destination super;

  const char *file_name;
  const CHARSET_INFO *charset;
  const Field_separators field_term;
  const Line_separators line_term;

public:
  PT_into_destination_outfile(const POS &pos,
                              const LEX_STRING &file_name_arg,
                              const CHARSET_INFO *charset_arg,
                              const Field_separators &field_term_arg,
                              const Line_separators &line_term_arg)
    : PT_into_destination(pos),
      file_name(file_name_arg.str),
      charset(charset_arg),
      field_term(field_term_arg),
      line_term(line_term_arg)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc))
      return true;

    LEX *lex= pc->thd->lex;
    lex->set_uncacheable(pc->select, UNCACHEABLE_SIDEEFFECT);
    if (!(lex->exchange= new sql_exchange(file_name, 0)) ||
        !(lex->result= new Query_result_export(pc->thd, lex->exchange)))
      return true;

    lex->exchange->cs= charset;
    lex->exchange->field.merge_field_separators(field_term);
    lex->exchange->line.merge_line_separators(line_term);
    return false;
  }
};


class PT_into_destination_dumpfile : public PT_into_destination
{
  typedef PT_into_destination super;

  const char *file_name;

public:
  explicit PT_into_destination_dumpfile(const POS &pos,
                                        const LEX_STRING &file_name_arg)
    : PT_into_destination(pos),
      file_name(file_name_arg.str)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc))
      return true;

    LEX *lex= pc->thd->lex;
    if (!lex->describe)
    {
      lex->set_uncacheable(pc->select, UNCACHEABLE_SIDEEFFECT);
      if (!(lex->exchange= new sql_exchange(file_name, 1)))
        return true;
      if (!(lex->result= new Query_result_dump(pc->thd, lex->exchange)))
        return true;
    }
    return false;
  }
};


class PT_select_var : public Parse_tree_node
{
public:
  const LEX_STRING name;

  explicit PT_select_var(const LEX_STRING &name_arg) : name(name_arg) {}

  virtual bool is_local() const { return false; }
  virtual uint get_offset() const { DBUG_ASSERT(0); return 0; }
};


class PT_select_sp_var : public PT_select_var
{
  typedef PT_select_var super;

  uint offset;

#ifndef DBUG_OFF
  /*
    Routine to which this Item_splocal belongs. Used for checking if correct
    runtime context is used for variable handling.
  */
  sp_head *sp;
#endif

public:
  PT_select_sp_var(const LEX_STRING &name_arg) : super(name_arg) {}

  virtual bool is_local() const { return true; }
  virtual uint get_offset() const { return offset; }

  virtual bool contextualize(Parse_context *pc);
};


class PT_select_var_list : public PT_into_destination
{
  typedef PT_into_destination super;

public:
  PT_select_var_list(const POS &pos) : PT_into_destination(pos) {}

  List<PT_select_var> value;

  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc))
      return true;

    List_iterator<PT_select_var> it(value);
    PT_select_var *var;
    while ((var= it++))
    {
      if (var->contextualize(pc))
        return true;
    }

    LEX * const lex= pc->thd->lex;
    if (lex->describe)
      return false;

    Query_dumpvar *dumpvar= new (pc->mem_root) Query_dumpvar(pc->thd);
    if (dumpvar == NULL)
      return true;

    dumpvar->var_list= value;
    lex->result= dumpvar;
    lex->set_uncacheable(pc->select, UNCACHEABLE_SIDEEFFECT);
  
    return false;
  }

  bool push_back(PT_select_var *var) { return value.push_back(var); }
};


class PT_select_options_and_item_list : public Parse_tree_node
{
  typedef Parse_tree_node super;

  Query_options options;
  PT_item_list *item_list;

public:
  PT_select_options_and_item_list(const Query_options &options_arg,
                                  PT_item_list *item_list_arg)
  : options(options_arg), item_list(item_list_arg)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc))
      return true;

    pc->select->parsing_place= CTX_SELECT_LIST;

    if (options.query_spec_options & SELECT_HIGH_PRIORITY)
    {
      Yacc_state *yyps= &pc->thd->m_parser_state->m_yacc;
      yyps->m_lock_type= TL_READ_HIGH_PRIORITY;
      yyps->m_mdl_type= MDL_SHARED_READ;
    }
    if (options.save_to(pc))
      return true;

    if (item_list->contextualize(pc))
      return true;

    // Ensure we're resetting parsing place of the right select
    DBUG_ASSERT(pc->select->parsing_place == CTX_SELECT_LIST);
    pc->select->parsing_place= CTX_NONE;
    return false;
  }
};


class PT_select_part2 : public Parse_tree_node
{
  /// Adds a default constructor to select_lock_type.
  class Default_constructible_locking_clause : public Select_lock_type
  {
  public:
    Default_constructible_locking_clause() { is_set= false; }

    Default_constructible_locking_clause(const Select_lock_type &slt)
    {
      is_set= slt.is_set;
      lock_type= slt.lock_type;
      is_safe_to_cache_query= slt.is_safe_to_cache_query;
    }
  };

  typedef Parse_tree_node super;

  PT_select_options_and_item_list *select_options_and_item_list;
  PT_into_destination *opt_into1;
  PT_table_reference_list *from_clause; // actually is optional (NULL) for DUAL
  Item *opt_where_clause;
  PT_group *opt_group_clause;
  Item *opt_having_clause;
  PT_order *opt_order_clause;
  PT_limit_clause *opt_limit_clause;
  PT_procedure_analyse *opt_procedure_analyse_clause;
  Default_constructible_locking_clause opt_select_lock_type;

public:
  PT_select_part2(
    PT_select_options_and_item_list *select_options_and_item_list_arg,
    PT_into_destination *opt_into1_arg,
    PT_table_reference_list *from_clause_arg,
    Item *opt_where_clause_arg,
    PT_group *opt_group_clause_arg,
    Item *opt_having_clause_arg,
    PT_order *opt_order_clause_arg,
    PT_limit_clause *opt_limit_clause_arg,
    PT_procedure_analyse *opt_procedure_analyse_clause_arg,
    const Select_lock_type &opt_select_lock_type_arg)
  : select_options_and_item_list(select_options_and_item_list_arg),
    opt_into1(opt_into1_arg),
    from_clause(from_clause_arg),
    opt_where_clause(opt_where_clause_arg),
    opt_group_clause(opt_group_clause_arg),
    opt_having_clause(opt_having_clause_arg),
    opt_order_clause(opt_order_clause_arg),
    opt_limit_clause(opt_limit_clause_arg),
    opt_procedure_analyse_clause(opt_procedure_analyse_clause_arg),
    opt_select_lock_type(opt_select_lock_type_arg)
  {}

  PT_select_part2(
    PT_select_options_and_item_list *select_options_and_item_list_arg,
    PT_into_destination *opt_into1_arg,
    PT_table_reference_list *from_clause_arg,
    Item *opt_where_clause_arg,
    PT_group *opt_group_clause_arg,
    Item *opt_having_clause_arg)
  : select_options_and_item_list(select_options_and_item_list_arg),
    opt_into1(opt_into1_arg),
    from_clause(from_clause_arg),
    opt_where_clause(opt_where_clause_arg),
    opt_group_clause(opt_group_clause_arg),
    opt_having_clause(opt_having_clause_arg),
    opt_order_clause(NULL),
    opt_limit_clause(NULL),
    opt_procedure_analyse_clause(NULL),
    opt_select_lock_type()
  {}

  PT_select_part2(
    PT_select_options_and_item_list *select_options_and_item_list_arg,
    PT_table_reference_list *from_clause_arg,
    Item *opt_where_clause_arg,
    PT_group *opt_group_clause_arg,
    Item *opt_having_clause_arg)
  : select_options_and_item_list(select_options_and_item_list_arg),
    opt_into1(NULL),
    from_clause(from_clause_arg),
    opt_where_clause(opt_where_clause_arg),
    opt_group_clause(opt_group_clause_arg),
    opt_having_clause(opt_having_clause_arg),
    opt_order_clause(NULL),
    opt_limit_clause(NULL),
    opt_procedure_analyse_clause(NULL),
    opt_select_lock_type()
  {}

  PT_select_part2(
    PT_select_options_and_item_list *select_options_and_item_list_arg,
    PT_table_reference_list *from_clause_arg,
    Item *opt_where_clause_arg)
  : select_options_and_item_list(select_options_and_item_list_arg),
    opt_into1(NULL),
    from_clause(from_clause_arg),
    opt_where_clause(opt_where_clause_arg),
    opt_group_clause(NULL),
    opt_having_clause(NULL),
    opt_order_clause(NULL),
    opt_limit_clause(NULL),
    opt_procedure_analyse_clause(NULL),
    opt_select_lock_type()
  {}

  explicit PT_select_part2(
    PT_select_options_and_item_list *select_options_and_item_list_arg)
  : select_options_and_item_list(select_options_and_item_list_arg),
    opt_into1(NULL),
    from_clause(NULL),
    opt_where_clause(NULL),
    opt_group_clause(NULL),
    opt_having_clause(NULL),
    opt_order_clause(NULL),
    opt_limit_clause(NULL),
    opt_procedure_analyse_clause(NULL),
    opt_select_lock_type()
  {}

  PT_limit_clause *limit_clause() const { return opt_limit_clause; }
  PT_order *order_clause() const { return opt_order_clause; }

  virtual PT_order *remove_order_clause()
  {
    PT_order *order= opt_order_clause;
    opt_order_clause= NULL;
    return order;
  }

  virtual PT_limit_clause *remove_limit_clause()
  {
    PT_limit_clause *limit= opt_limit_clause;
    opt_limit_clause= NULL;
    return limit;
  }

  virtual bool contextualize(Parse_context *pc);

  virtual bool has_into_clause() const { return opt_into1 != NULL; }
};


class PT_query_expression : public Parse_tree_node
{
public:

  PT_query_expression(PT_query_expression_body *body,
                      PT_order *order,
                      const POS &order_pos,
                      PT_limit_clause *limit,
                      PT_procedure_analyse *procedure_analyse,
                      Select_lock_type &lock_type)
    : contextualized(false),
      m_body(body),
      m_order(order),
      m_order_pos(order_pos),
      m_limit(limit),
      m_procedure_analyse(procedure_analyse),
      m_lock_type(lock_type),
      m_parentheses(false)
  {}

  PT_query_expression(PT_query_expression *qe,
                      PT_order *order,
                      const POS &order_pos,
                      PT_limit_clause *limit,
                      PT_procedure_analyse *procedure_analyse,
                      Select_lock_type &lock_type)
    : contextualized(false),
      m_body(qe->m_body),
      m_order(order),
      m_order_pos(order_pos),
      m_limit(limit),
      m_procedure_analyse(procedure_analyse),
      m_lock_type(lock_type),
      m_parentheses(false)
  {}

  PT_query_expression(PT_query_expression_body *body,
                      const Select_lock_type &lock_type)
    : contextualized(false),
      m_body(body),
      m_order(NULL),
      m_limit(NULL),
      m_procedure_analyse(NULL),
      m_lock_type(lock_type),
      m_parentheses(false)
  {}

  PT_query_expression(PT_query_expression_body *body)
    : contextualized(false),
      m_body(body),
      m_order(NULL),
      m_limit(NULL),
      m_procedure_analyse(NULL),
      m_parentheses(false)
  {
    m_lock_type.is_set= false;
  }

  virtual bool contextualize(Parse_context *pc)
  {
    pc->select->set_braces(m_parentheses || pc->select->braces);
    m_body->set_containing_qe(this);
    if (Parse_tree_node::contextualize(pc) ||
        m_body->contextualize(pc))
      return true;

    if (!contextualized)
      if (contextualize_order_and_limit(pc))
        return true;

    if (contextualize_safe(pc, m_procedure_analyse))
      return true;

    if (m_procedure_analyse && pc->select->master_unit()->outer_select() != NULL)
      my_error(ER_WRONG_USAGE, MYF(0), "PROCEDURE", "subquery");

    if (m_lock_type.is_set)
    {
      pc->select->set_lock_for_tables(m_lock_type.lock_type);
      pc->thd->lex->safe_to_cache_query= m_lock_type.is_safe_to_cache_query;
    }

    return false;
  }

  bool has_procedure() const { return m_procedure_analyse != NULL; }

  bool has_order() const { return m_order != NULL; }

  bool has_limit() const { return m_limit != NULL; }

  bool is_union() const { return m_body->is_union(); }

  bool has_into_clause() const { return m_body->has_into_clause(); }

  /**
    Callback for deeper nested query expressions.
  */
  bool contextualize_order_and_limit(Parse_context *pc)
  {
    contextualized= true;

    pc->thd->where= "global ORDER clause";
    if (contextualize_safe(pc, m_order) || contextualize_safe(pc, m_limit))
      return true;

    pc->thd->where= THD::DEFAULT_WHERE;
    return false;
  }

  void set_parentheses() { m_parentheses= true; }

  void remove_parentheses() { m_parentheses= false; }

  /**
    Called by the parser when it has decided that this query expression may
    not contain order or limit clauses because it is part of a union. For
    historical reasons, these clauses are not allowed in non-last branches of
    union expressions.
  */
  void ban_order_and_limit() const
  {
    if (m_order != NULL)
      my_error(ER_WRONG_USAGE, MYF(0), "UNION", "ORDER BY");
    if (m_limit != NULL)
      my_error(ER_WRONG_USAGE, MYF(0), "UNION", "LIMIT");
  }

  ~PT_query_expression() { DBUG_ASSERT(contextualized); }

private:
  bool contextualized;
  PT_query_expression_body *m_body;
  PT_order *m_order;
  POS m_order_pos;
  PT_limit_clause *m_limit;
  PT_procedure_analyse *m_procedure_analyse;
  Select_lock_type m_lock_type;
  bool m_parentheses;
};


class PT_subquery : public Parse_tree_node
{
public:
  PT_subquery(POS pos, PT_query_expression *query_expression)
    : m_query_expression(query_expression), m_pos(pos),
      m_is_derived_table(false)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (Parse_tree_node::contextualize(pc))
      return true;

    LEX *lex= pc->thd->lex;
    if (!lex->expr_allows_subselect ||
       lex->sql_command == (int)SQLCOM_PURGE)
    {
      error(pc, m_pos);
      return true;
    }

    /*
      We are making a "derived table" for the parenthesis
      as we need to have a lex level to fit the union
      after the parenthesis, e.g.
      (SELECT .. ) UNION ...  becomes
      SELECT * FROM ((SELECT ...) UNION ...)
    */
    SELECT_LEX *child= lex->new_query(pc->select);
    if (child == NULL)
      return true;

    Parse_context inner_pc(pc->thd, child);

    if (m_is_derived_table)
      child->linkage= DERIVED_TABLE_TYPE;

    if (m_query_expression->contextualize(&inner_pc))
      return true;

    m_select_lex= inner_pc.select->master_unit()->first_select();

    lex->pop_context();
    pc->select->n_child_sum_items += child->n_sum_items;

    /*
      A subselect can add fields to an outer select. Reserve space for
      them.
    */
    pc->select->select_n_where_fields+= child->select_n_where_fields;
    pc->select->select_n_having_items+= child->select_n_having_items;

    return false;
  }

  void remove_parentheses() { m_query_expression->remove_parentheses(); }

  bool is_union() { return m_query_expression->is_union(); }

  SELECT_LEX *value() { return m_select_lex; }

private:
  PT_query_expression *m_query_expression;
  POS m_pos;
  SELECT_LEX *m_select_lex;
public:
  bool m_is_derived_table;
};


class PT_derived_table : public PT_table_factor
{
public:
  PT_derived_table(PT_subquery *subquery, LEX_STRING *table_alias) :
    m_subquery(subquery),
    m_table_alias(table_alias)
  {
    m_subquery->m_is_derived_table= true;
  }

  virtual bool contextualize(Parse_context *pc);

private:
  PT_subquery *m_subquery;
  LEX_STRING *m_table_alias;
};

class PT_query_primary : public Parse_tree_node
{
public:
  virtual bool has_into_clause() const = 0;
};

class PT_query_specification : public PT_query_primary
{
public:
  PT_query_specification(PT_hint_list *opt_hint_list_arg,
                         PT_select_part2 *select_part2_arg)
    : m_hints(opt_hint_list_arg),
      m_select_part2(select_part2_arg)
  {}

  PT_query_specification(PT_select_part2 *select_part2_arg)
    : m_hints(NULL),
      m_select_part2(select_part2_arg)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (Parse_tree_node::contextualize(pc) ||
        m_select_part2->contextualize(pc) ||
        contextualize_safe(pc, m_hints))
      return true;
    return false;
  }

  PT_order *order_clause() { return m_select_part2->order_clause(); }
  PT_limit_clause *limit_clause() { return m_select_part2->limit_clause(); }
  PT_order *remove_order_clause()
  {
    return m_select_part2->remove_order_clause();
  }
  PT_limit_clause *remove_limit_clause()
  {
    return m_select_part2->remove_limit_clause();
  }

  virtual bool has_into_clause() const
  {
    return m_select_part2->has_into_clause();
  }

private:
  PT_hint_list *m_hints;
  PT_select_part2 *m_select_part2;
};

class PT_query_expression_body_primary : public PT_query_expression_body
{
public:
  PT_query_expression_body_primary(PT_query_primary *query_primary)
    : m_query_primary(query_primary)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (PT_query_expression_body::contextualize(pc) ||
        m_query_primary->contextualize(pc))
      return true;
    return false;
  }

  virtual bool is_union() const { return false; }

  virtual bool has_into_clause() const
  {
    return m_query_primary->has_into_clause();
  }

private:
  PT_query_primary *m_query_primary;
};

class PT_union : public PT_query_expression_body
{
public:
  PT_union(PT_query_expression *lhs, bool is_distinct,
           PT_query_primary *rhs) :
    m_lhs(lhs),
    m_is_distinct(is_distinct),
    m_rhs(rhs),
    m_containing_qe(NULL)
  {}

  virtual void set_containing_qe(PT_query_expression *qe) {
    m_containing_qe= qe;
  }

  virtual bool contextualize(Parse_context *pc)
  {
    if (PT_query_expression_body::contextualize(pc))
      return true;

    if (m_lhs->contextualize(pc))
      return true;

    pc->select=
      pc->thd->lex->new_union_query(pc->select, m_is_distinct, false);

    if (pc->select == NULL || m_rhs->contextualize(pc))
      return true;

    THD *thd= pc->thd;

    SELECT_LEX_UNIT *unit= pc->select->master_unit();
    if (unit->fake_select_lex == NULL && unit->add_fake_select_lex(thd))
      return true;

    SELECT_LEX *select_lex= pc->select;
    pc->select= unit->fake_select_lex;
    pc->select->no_table_names_allowed= true;

    if (m_containing_qe != NULL &&
        m_containing_qe->contextualize_order_and_limit(pc))
      return true;

    pc->select->no_table_names_allowed= false;
    pc->select= select_lex;

    pc->thd->lex->pop_context();

    return false;
  }

  virtual bool is_union() const { return true; }

  virtual bool is_nested() const { return false; }

  virtual bool has_into_clause() const
  {
    return m_lhs->has_into_clause() || m_rhs->has_into_clause();
  }

private:
  PT_query_expression *m_lhs;
  bool m_is_distinct;
  PT_query_primary *m_rhs;
  PT_into_destination *m_into;
  PT_query_expression *m_containing_qe;
};


class PT_nested_query_expression: public PT_query_primary
{
public:

  PT_nested_query_expression(PT_query_expression *qe) : m_qe(qe) {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (PT_query_primary::contextualize(pc))
      return true;

    pc->select->set_braces(true);
    bool result= m_qe->contextualize(pc);

    return result;
  }

  bool has_into_clause() const { return m_qe->has_into_clause(); }

private:
  PT_query_expression *m_qe;
};


class PT_select_paren : public Parse_tree_node
{
  typedef Parse_tree_node super;

  PT_hint_list *opt_hint_list;
  PT_select_part2 *select_part2;

public:
  PT_select_paren(PT_hint_list *opt_hint_list_arg,
                  PT_select_part2 *select_part2_arg)
  : opt_hint_list(opt_hint_list_arg), select_part2(select_part2_arg)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc))
      return true;

    /*
      In order to correctly process UNION's global ORDER BY we need to
      set braces before parsing the clause.
    */
    pc->select->set_braces(true);

    if (select_part2->contextualize(pc))
      return true;

    if (setup_select_in_parentheses(pc->select))
      return true;

    if (opt_hint_list != NULL && opt_hint_list->contextualize(pc))
      return true;

    return false;
  }
};


class PT_select_init_parenthesis : public PT_select_init
{
  typedef PT_select_init super;

  PT_select_paren *select_paren;
  Parse_tree_node *union_opt;

public:
  PT_select_init_parenthesis(PT_select_paren *select_paren_arg,
                             Parse_tree_node *union_opt_arg)
  : select_paren(select_paren_arg), union_opt(union_opt_arg)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    return (super::contextualize(pc) || select_paren->contextualize(pc) ||
            (union_opt != NULL && union_opt->contextualize(pc)));
  }
};


class PT_select_init2 : public PT_select_init
{
  typedef PT_select_init super;

  PT_hint_list *opt_hint_list;
  PT_select_part2 *select_part2;
  PT_union_list *opt_union_clause;

public:
  PT_select_init2(PT_hint_list *opt_hint_list_arg,
                  PT_select_part2 *select_part2_arg,
                  PT_union_list *opt_union_clause_arg)
  : opt_hint_list(opt_hint_list_arg),
    select_part2(select_part2_arg),
    opt_union_clause(opt_union_clause_arg)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc) ||
        select_part2->contextualize(pc))
      return true;

    // Parentheses carry no meaning here.
    pc->select->set_braces(false);

    if (opt_hint_list != NULL && opt_hint_list->contextualize(pc))
      return true;

    if (opt_union_clause != NULL && opt_union_clause->contextualize(pc))
      return true;

    return false;
  }
};


class PT_select : public Parse_tree_node
{
  typedef Parse_tree_node super;

public:
  /**
    @param qe The query expression.
    @param sql_command The type of SQL command.
  */
  PT_select(enum_sql_command sql_command, PT_query_expression *qe)
    : m_sql_command(sql_command),
      m_select_init(qe),
      m_into(NULL)
  {}

  /**
    Creates a SELECT command. Only SELECT commands can have into.

    @param qe The query expression.
    @param into The trailing INTO destination.
  */
  PT_select(PT_query_expression *qe, PT_into_destination *into)
    : m_sql_command(SQLCOM_SELECT),
      m_select_init(qe),
      m_into(into)
  {}

  PT_select(PT_query_expression *qe)
    : m_sql_command(SQLCOM_SELECT),
      m_select_init(qe),
      m_into(NULL)
  {}

  virtual bool contextualize(Parse_context *pc)
  {
    if (super::contextualize(pc))
      return true;

    pc->thd->lex->sql_command= m_sql_command;

    return m_select_init->contextualize(pc) ||
      contextualize_safe(pc, m_into);
  }

private:
  enum_sql_command m_sql_command;
  PT_query_expression *m_select_init;
  PT_into_destination *m_into;
};


class PT_delete : public PT_statement
{
  typedef PT_statement super;

  PT_hint_list *opt_hints;
  const int opt_delete_options;
  Table_ident *table_ident;
  Mem_root_array_YY<Table_ident *> table_list;
  List<String> *opt_use_partition;
  PT_table_reference_list *join_table_list;
  Item *opt_where_clause;
  PT_order *opt_order_clause;
  Item *opt_delete_limit_clause;

public:
  // single-table DELETE node constructor:
  PT_delete(MEM_ROOT *mem_root,
            PT_hint_list *opt_hints_arg,
            int opt_delete_options_arg,
            Table_ident *table_ident_arg,
            List<String> *opt_use_partition_arg,
            Item *opt_where_clause_arg,
            PT_order *opt_order_clause_arg,
            Item *opt_delete_limit_clause_arg)
  : opt_hints(opt_hints_arg),
    opt_delete_options(opt_delete_options_arg),
    table_ident(table_ident_arg),
    opt_use_partition(opt_use_partition_arg),
    join_table_list(NULL),
    opt_where_clause(opt_where_clause_arg),
    opt_order_clause(opt_order_clause_arg),
    opt_delete_limit_clause(opt_delete_limit_clause_arg)
  {
    table_list.init(mem_root);
  }

  // multi-table DELETE node constructor:
  PT_delete(PT_hint_list *opt_hints_arg,
            int opt_delete_options_arg,
            const Mem_root_array_YY<Table_ident *> &table_list_arg,
            PT_table_reference_list *join_table_list_arg,
            Item *opt_where_clause_arg)
  : opt_hints(opt_hints_arg),
    opt_delete_options(opt_delete_options_arg),
    table_ident(NULL),
    table_list(table_list_arg),
    opt_use_partition(NULL),
    join_table_list(join_table_list_arg),
    opt_where_clause(opt_where_clause_arg),
    opt_order_clause(NULL),
    opt_delete_limit_clause(NULL)
  {}

  virtual bool contextualize(Parse_context *pc);

  virtual Sql_cmd *make_cmd(THD *thd);

  bool is_multitable() const
  {
    DBUG_ASSERT((table_ident != NULL) ^ (table_list.size() > 0));
    return table_ident == NULL;
  }

private:
  bool add_table(Parse_context *pc, Table_ident *table);
};


class PT_update : public PT_statement
{
  typedef PT_statement super;

  PT_hint_list *opt_hints;
  thr_lock_type opt_low_priority;
  bool opt_ignore;
  Parse_tree_node *join_table_list;
  PT_item_list *column_list;
  PT_item_list *value_list;
  Item *opt_where_clause;
  PT_order *opt_order_clause;
  Item *opt_limit_clause;

  Sql_cmd_update sql_cmd;

public:
  PT_update(PT_hint_list *opt_hints_arg,
            thr_lock_type opt_low_priority_arg,
            bool opt_ignore_arg,
            Parse_tree_node *join_table_list_arg,
            PT_item_list *column_list_arg,
            PT_item_list *value_list_arg,
            Item *opt_where_clause_arg,
            PT_order *opt_order_clause_arg,
            Item *opt_limit_clause_arg)
  : opt_hints(opt_hints_arg),
    opt_low_priority(opt_low_priority_arg),
    opt_ignore(opt_ignore_arg),
    join_table_list(join_table_list_arg),
    column_list(column_list_arg),
    value_list(value_list_arg),
    opt_where_clause(opt_where_clause_arg),
    opt_order_clause(opt_order_clause_arg),
    opt_limit_clause(opt_limit_clause_arg)
  {}

  virtual bool contextualize(Parse_context *pc);

  virtual Sql_cmd *make_cmd(THD *thd);
};


class PT_create_select : public Parse_tree_node
{
  typedef Parse_tree_node super;

  PT_hint_list *opt_hints;
  Query_options options;
  PT_item_list *item_list;
  PT_table_expression *table_expression;

public:
  PT_create_select(PT_hint_list *opt_hints_arg,
                   const Query_options &options_arg,
                   PT_item_list *item_list_arg,
                   PT_table_expression *table_expression_arg)
  : opt_hints(opt_hints_arg),
    options(options_arg),
    item_list(item_list_arg),
    table_expression(table_expression_arg)
  {}

  virtual bool contextualize(Parse_context *pc);
};


class PT_insert_values_list : public Parse_tree_node
{
  typedef Parse_tree_node super;

  List<List_item> many_values;

public:
  virtual bool contextualize(Parse_context *pc);

  bool push_back(List<Item> *x) { return many_values.push_back(x); }

  virtual List<List_item> &get_many_values()
  {
    DBUG_ASSERT(is_contextualized());
    return many_values;
  }
};


class PT_insert_query_expression : public Parse_tree_node
{
  typedef Parse_tree_node super;

  bool braces;
  PT_create_select *create_select;
  Parse_tree_node * opt_union;

public:
  PT_insert_query_expression(bool braces_arg,
                             PT_create_select *create_select_arg,
                             Parse_tree_node * opt_union_arg)
  : braces(braces_arg),
    create_select(create_select_arg),
    opt_union(opt_union_arg)
  {}

  virtual bool contextualize(Parse_context *pc);
};


class PT_insert : public PT_statement
{
  typedef PT_statement super;

  const bool is_replace;
  PT_hint_list *opt_hints;
  const thr_lock_type lock_option;
  const bool ignore;
  Table_ident * const table_ident;
  List<String> * const opt_use_partition;
  PT_item_list * const column_list;
  PT_insert_values_list * const row_value_list;
  PT_insert_query_expression * const insert_query_expression;
  PT_item_list * const opt_on_duplicate_column_list;
  PT_item_list * const opt_on_duplicate_value_list;

public:
  PT_insert(bool is_replace_arg,
            PT_hint_list *opt_hints_arg,
            thr_lock_type lock_option_arg,
            bool ignore_arg,
            Table_ident *table_ident_arg,
            List<String> *opt_use_partition_arg,
            PT_item_list *column_list_arg,
	    PT_insert_values_list *row_value_list_arg,
            PT_insert_query_expression *insert_query_expression_arg,
            PT_item_list *opt_on_duplicate_column_list_arg,
            PT_item_list *opt_on_duplicate_value_list_arg)
  : is_replace(is_replace_arg),
    opt_hints(opt_hints_arg),
    lock_option(lock_option_arg),
    ignore(ignore_arg),
    table_ident(table_ident_arg),
    opt_use_partition(opt_use_partition_arg),
    column_list(column_list_arg),
    row_value_list(row_value_list_arg),
    insert_query_expression(insert_query_expression_arg),
    opt_on_duplicate_column_list(opt_on_duplicate_column_list_arg),
    opt_on_duplicate_value_list(opt_on_duplicate_value_list_arg)
  {
    // REPLACE statement can't have IGNORE flag:
    DBUG_ASSERT(!is_replace || !ignore);
    // REPLACE statement can't have ON DUPLICATE KEY UPDATE clause:
    DBUG_ASSERT(!is_replace || opt_on_duplicate_column_list == NULL);
    // INSERT/REPLACE ... SELECT can't have VALUES clause:
    DBUG_ASSERT((row_value_list != NULL) ^ (insert_query_expression != NULL));
    // ON DUPLICATE KEY UPDATE: column and value arrays must have same sizes:
    DBUG_ASSERT((opt_on_duplicate_column_list == NULL &&
                 opt_on_duplicate_value_list == NULL) ||
                (opt_on_duplicate_column_list->elements() ==
                 opt_on_duplicate_value_list->elements()));
  }

  virtual bool contextualize(Parse_context *pc);

  virtual Sql_cmd *make_cmd(THD *thd);

private:
  bool has_select() const { return insert_query_expression != NULL; }
};


class PT_shutdown : public PT_statement
{
  Sql_cmd_shutdown sql_cmd;

public:
  virtual Sql_cmd *make_cmd(THD *) { return &sql_cmd; }
};

#endif /* PARSE_TREE_NODES_INCLUDED */
