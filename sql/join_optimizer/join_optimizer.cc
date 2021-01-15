/* Copyright (c) 2020, 2021, Oracle and/or its affiliates.

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

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <algorithm>
#include <array>
#include <initializer_list>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "my_alloc.h"
#include "my_base.h"
#include "my_bit.h"
#include "my_inttypes.h"
#include "my_sqlcommand.h"
#include "my_sys.h"
#include "my_table_map.h"
#include "mysql/components/services/bits/psi_bits.h"
#include "mysqld_error.h"
#include "prealloced_array.h"
#include "sql/filesort.h"
#include "sql/handler.h"
#include "sql/item.h"
#include "sql/item_cmpfunc.h"
#include "sql/item_sum.h"
#include "sql/join_optimizer/access_path.h"
#include "sql/join_optimizer/bit_utils.h"
#include "sql/join_optimizer/estimate_filter_cost.h"
#include "sql/join_optimizer/estimate_selectivity.h"
#include "sql/join_optimizer/explain_access_path.h"
#include "sql/join_optimizer/hypergraph.h"
#include "sql/join_optimizer/interesting_orders.h"
#include "sql/join_optimizer/join_optimizer.h"
#include "sql/join_optimizer/make_join_hypergraph.h"
#include "sql/join_optimizer/print_utils.h"
#include "sql/join_optimizer/relational_expression.h"
#include "sql/join_optimizer/subgraph_enumeration.h"
#include "sql/join_optimizer/walk_access_paths.h"
#include "sql/mem_root_array.h"
#include "sql/opt_range.h"
#include "sql/query_options.h"
#include "sql/sql_class.h"
#include "sql/sql_cmd.h"
#include "sql/sql_const.h"
#include "sql/sql_executor.h"
#include "sql/sql_insert.h"
#include "sql/sql_lex.h"
#include "sql/sql_list.h"
#include "sql/sql_optimizer.h"
#include "sql/sql_planner.h"
#include "sql/sql_select.h"
#include "sql/sql_tmp_table.h"
#include "sql/table.h"
#include "sql/table_function.h"

using hypergraph::Hyperedge;
using hypergraph::Hypergraph;
using hypergraph::NodeMap;
using std::array;
using std::min;
using std::string;
using std::swap;
using std::vector;

namespace {

// These are extremely arbitrary cost model constants. We should revise them
// based on actual query times (possibly using linear regression?), and then
// put them into the cost model to make them user-tunable. However, until
// we've fixed some glaring omissions such as lack of understanding of initial
// cost, any such estimation will be dominated by outliers/noise.
constexpr double kApplyOneFilterCost = 0.1;
constexpr double kAggregateOneRowCost = 0.1;
constexpr double kSortOneRowCost = 0.1;
constexpr double kHashBuildOneRowCost = 0.1;
constexpr double kHashProbeOneRowCost = 0.1;
constexpr double kMaterializeOneRowCost = 0.1;

void ReplaceOrderItemsWithTempTableFields(THD *thd, ORDER *order);

TABLE *CreateTemporaryTableFromSelectList(
    THD *thd, Query_block *query_block,
    Temp_table_param **temp_table_param_arg);

void ReplaceSelectListWithTempTableFields(THD *thd, JOIN *join,
                                          Temp_table_param *temp_table_param);

AccessPath *CreateMaterializationPath(THD *thd, JOIN *join, AccessPath *path,
                                      TABLE *temp_table,
                                      Temp_table_param *temp_table_param);

// An ordering that we could be doing sort-ahead by; typically either an
// interesting ordering or an ordering homogenized from one.
struct SortAheadOrdering {
  // Pointer to an ordering in LogicalOrderings.
  int ordering_idx;

  // Which tables must be present in the join before one can apply
  // this sort (usually because the elements we sort by are contained
  // in these tables).
  NodeMap required_nodes;

  // The ordering expressed in a form that filesort can use.
  ORDER *order;
};

/**
  CostingReceiver contains the main join planning logic, selecting access paths
  based on cost. It receives subplans from DPhyp (see enumerate_subgraph.h),
  assigns them costs based on a cost model, and keeps the ones that are
  cheapest. In the end, this means it will be left with a root access path that
  gives the lowest total cost for joining the tables in the query block, ie.,
  without ORDER BY etc.

  Currently, besides the expected number of produced rows (which is the same no
  matter how we access the table) we keep only a single value per subplan
  (total cost), and thus also only a single best access path. In the future,
  we will have more dimensions to worry about, such as initial cost versus total
  cost (relevant for LIMIT), ordering properties, and so on. At that point,
  there is not necessarily a single “best” access path anymore, and we will need
  to keep multiple ones around, and test all of them as candidates when building
  larger subplans.
 */
class CostingReceiver {
 public:
  CostingReceiver(
      THD *thd, Query_block *query_block, const JoinHypergraph &graph,
      const LogicalOrderings *orderings,
      const Mem_root_array<SortAheadOrdering> *sort_ahead_orderings,
      bool need_rowid, uint64_t supported_access_path_types,
      secondary_engine_modify_access_path_cost_t secondary_engine_cost_hook,
      string *trace)
      : m_thd(thd),
        m_query_block(query_block),
        m_graph(graph),
        m_orderings(orderings),
        m_sort_ahead_orderings(sort_ahead_orderings),
        m_need_rowid(need_rowid),
        m_supported_access_path_types(supported_access_path_types),
        m_secondary_engine_cost_hook(secondary_engine_cost_hook),
        m_trace(trace) {
    // At least one join type must be supported.
    assert(Overlaps(supported_access_path_types,
                    AccessPathTypeBitmap(AccessPath::HASH_JOIN,
                                         AccessPath::NESTED_LOOP_JOIN)));
  }

  bool HasSeen(NodeMap subgraph) const {
    return m_access_paths.count(subgraph) != 0;
  }

  bool FoundSingleNode(int node_idx);

  // Called EmitCsgCmp() in the DPhyp paper.
  bool FoundSubgraphPair(NodeMap left, NodeMap right, int edge_idx);

  const Prealloced_array<AccessPath *, 4> &root_candidates() {
    const auto it = m_access_paths.find(TablesBetween(0, m_graph.nodes.size()));
    assert(it != m_access_paths.end());
    return it->second.paths;
  }

  size_t num_access_paths() const { return m_access_paths.size(); }

  void ProposeAccessPath(AccessPath *path,
                         Prealloced_array<AccessPath *, 4> *existing_paths,
                         const char *description_for_trace) const;

  bool HasSecondaryEngineCostHook() const {
    return m_secondary_engine_cost_hook != nullptr;
  }

 private:
  THD *m_thd;

  /// The query block we are planning.
  Query_block *m_query_block;

  /**
    Besides the access paths for a set of nodes (see m_access_paths),
    AccessPathSet contains information that is common between all access
    paths for that set. One would believe num_output_rows would be such
    a member (a set of tables should produce the same number of ouptut
    rows no matter the join order), but due to parametrized paths,
    different access paths could have different outputs. delayed_predicates
    is another, but currently, it's already efficiently hidden space-wise
    due to the use of a union.
   */
  struct AccessPathSet {
    Prealloced_array<AccessPath *, 4> paths;
    FunctionalDependencySet active_functional_dependencies{0};
  };

  /**
    For each subset of tables that are connected in the join hypergraph,
    keeps the current best access paths for producing said subset.
    There can be several that are best in different ways; see comments
    on ProposeAccessPath().

    Also used for communicating connectivity information back to DPhyp
    (in HasSeen()); if there's an entry here, that subset will induce
    a connected subgraph of the join hypergraph.
   */
  std::unordered_map<NodeMap, AccessPathSet> m_access_paths;

  /// The graph we are running over.
  const JoinHypergraph &m_graph;

  /// Keeps track of interesting orderings in this query block.
  /// See LogicalOrderings for more information.
  const LogicalOrderings *m_orderings;

  /// List of all orderings that are candidates for sort-ahead
  /// (because they are, or may eventually become, an interesting ordering).
  const Mem_root_array<SortAheadOrdering> *m_sort_ahead_orderings;

  /// Whether we will be needing row IDs from our tables, typically for
  /// a later sort. If this happens, derived tables cannot use streaming,
  /// but need an actual materialization, since filesort expects to be
  /// able to go back and ask for a given row. (This is different from
  /// when we need row IDs for weedout, which doesn't preclude streaming.
  /// The hypergraph optimizer does not use weedout.)
  bool m_need_rowid;

  /// The supported access path types. Access paths of types not in
  /// this set should not be created. It is currently only used to
  /// limit which join types to use, so any bit that does not
  /// represent a join access path, is ignored for now.
  uint64_t m_supported_access_path_types;

  /// Pointer to a function that modifies the cost estimates of an access path
  /// for execution in a secondary storage engine, or nullptr otherwise.
  secondary_engine_modify_access_path_cost_t m_secondary_engine_cost_hook;

  /// If not nullptr, we store human-readable optimizer trace information here.
  string *m_trace;

  /// A map of tables that can never be on the right side of any join,
  /// ie., they have to be leftmost in the tree. This only affects recursive
  /// table references (ie., when WITH RECURSIVE is in use); they work by
  /// continuously tailing new records, which wouldn't work if we were to
  /// scan them multiple times or put them in a hash table. Naturally,
  /// there must be zero or one bit here; the common case is zero.
  NodeMap forced_leftmost_table = 0;

  /// For trace use only.
  std::string PrintSet(NodeMap x) {
    std::string ret = "{";
    bool first = true;
    for (size_t node_idx : BitsSetIn(x)) {
      if (!first) {
        ret += ",";
      }
      first = false;
      ret += m_graph.nodes[node_idx].table->alias;
    }
    return ret + "}";
  }

  /// Is the given access path type supported?
  bool SupportedAccessPathType(AccessPath::Type type) const {
    return Overlaps(AccessPathTypeBitmap(type), m_supported_access_path_types);
  }

  void ProposeAccessPathWithOrderings(NodeMap nodes,
                                      FunctionalDependencySet fd_set,
                                      AccessPath *path,
                                      const char *description_for_trace);
  bool ProposeTableScan(TABLE *table, int node_idx,
                        bool is_recursive_reference);
  bool ProposeRefAccess(TABLE *table, int node_idx, KEY *key, unsigned key_idx,
                        table_map allowed_parameter_tables);
  void ProposeNestedLoopJoin(NodeMap left, NodeMap right, AccessPath *left_path,
                             AccessPath *right_path, const JoinPredicate *edge,
                             FunctionalDependencySet new_fd_set);
  void ProposeHashJoin(NodeMap left, NodeMap right, AccessPath *left_path,
                       AccessPath *right_path, const JoinPredicate *edge,
                       FunctionalDependencySet new_fd_set, bool *wrote_trace);
  void ApplyPredicatesForBaseTable(int node_idx, uint64_t applied_predicates,
                                   uint64_t subsumed_predicates,
                                   bool materialize_subqueries,
                                   AccessPath *path,
                                   FunctionalDependencySet *new_fd_set);
  void ApplyDelayedPredicatesAfterJoin(NodeMap left, NodeMap right,
                                       const AccessPath *left_path,
                                       const AccessPath *right_path,
                                       bool materialize_subqueries,
                                       AccessPath *join_path,
                                       FunctionalDependencySet *new_fd_set);
};

/// Finds the set of supported access path types.
uint64_t SupportedAccessPathTypes(const THD *thd) {
  const handlerton *secondary_engine = thd->lex->m_sql_cmd->secondary_engine();
  if (secondary_engine != nullptr) {
    return secondary_engine->secondary_engine_supported_access_paths;
  }

  // Outside of secondary storage engines, all access path types are supported.
  return ~uint64_t{0};
}

/// Gets the secondary storage engine cost modification function, if any.
secondary_engine_modify_access_path_cost_t SecondaryEngineCostHook(
    const THD *thd) {
  const handlerton *secondary_engine = thd->lex->m_sql_cmd->secondary_engine();
  if (secondary_engine == nullptr) {
    return nullptr;
  } else {
    return secondary_engine->secondary_engine_modify_access_path_cost;
  }
}

/**
  Called for each table in the query block, at some arbitrary point before we
  start seeing subsets where it's joined to other tables.

  We support table scans and ref access, so we create access paths for both
  (where possible) and cost them. In this context, “tables” in a query block
  also includes virtual tables such as derived tables, so we need to figure out
  if there is a cost for materializing them.
 */
bool CostingReceiver::FoundSingleNode(int node_idx) {
  if (m_thd->is_error()) return true;

  TABLE *table = m_graph.nodes[node_idx].table;
  TABLE_LIST *tl = table->pos_in_table_list;

  // Ask the storage engine to update stats.records, if needed.
  // NOTE: ha_archive breaks without this call! (That is probably a bug in
  // ha_archive, though.)
  tl->fetch_number_of_rows();

  if (ProposeTableScan(table, node_idx, tl->is_recursive_reference())) {
    return true;
  }

  if (!Overlaps(table->file->ha_table_flags(), HA_NO_INDEX_ACCESS) &&
      !tl->is_recursive_reference()) {
    for (unsigned key_idx = 0; key_idx < table->s->keys; ++key_idx) {
      // Propose ref access using only sargable predicates that reference no
      // other table.
      if (ProposeRefAccess(table, node_idx, &table->key_info[key_idx], key_idx,
                           /*allowed_parameter_tables=*/0)) {
        return true;
      }

      // Propose ref access using all sargable predicates that also refer to
      // other tables (e.g. t1.x = t2.x). Such access paths can only be used on
      // the inner side of a nested loop join, where all the other referenced
      // tables are among the outer tables of the join. Such path is called a
      // parametrized path.
      //
      // Since indexes can have multiple parts, the access path can also end up
      // being parametrized on multiple outer tables. However, since
      // parametrized paths are less flexible in joining than non-parametrized
      // ones, it can be advantageous to not use all parts of the index; it's
      // impossible to say locally. Thus, we enumerate all possible subsets of
      // table parameters that may be useful, to make sure we don't miss any
      // such paths.
      table_map want_parameter_tables = 0;
      for (unsigned pred_idx = 0;
           pred_idx < m_graph.nodes[node_idx].sargable_predicates.size();
           ++pred_idx) {
        const SargablePredicate &sp =
            m_graph.nodes[node_idx].sargable_predicates[pred_idx];
        if (sp.field->table == table && sp.field->part_of_key.is_set(key_idx) &&
            !Overlaps(sp.other_side->used_tables(),
                      PSEUDO_TABLE_BITS | table->pos_in_table_list->map())) {
          want_parameter_tables |= sp.other_side->used_tables();
        }
      }
      for (table_map allowed_parameter_tables :
           NonzeroSubsetsOf(want_parameter_tables)) {
        if (ProposeRefAccess(table, node_idx, &table->key_info[key_idx],
                             key_idx, allowed_parameter_tables)) {
          return true;
        }
      }
    }
  }

  return false;
}

// Specifies a mapping in a TABLE_REF between an index keypart and a condition,
// with the intention to satisfy the condition with the index keypart (ref
// access). Roughly comparable to Key_use in the non-hypergraph optimizer.
struct KeypartForRef {
  // The condition we are pushing down (e.g. t1.f1 = 3).
  Item *condition;

  // The field that is to be matched (e.g. t1.f1).
  Field *field;

  // The value we are matching against (e.g. 3). Could be another field.
  Item *val;

  // Whether this condition would never match if either side is NULL.
  bool null_rejecting;

  // Tables used by the condition. Necessarily includes the table “field”
  // is part of.
  table_map used_tables;
};

int WasPushedDownToRef(Item *condition, const KeypartForRef *keyparts,
                       unsigned num_keyparts) {
  for (unsigned keypart_idx = 0; keypart_idx < num_keyparts; keypart_idx++) {
    if (condition->eq(keyparts[keypart_idx].condition,
                      /*binary_cmp=*/true)) {
      return keypart_idx;
    }
  }
  return -1;
}

bool ContainsSubqueries(Item *item_arg) {
  // Nearly the same as item_arg->has_subquery(), but different for
  // Item_func_not_all, which we currently do not support.
  return WalkItem(item_arg, enum_walk::POSTFIX, [](Item *item) {
    return item->type() == Item::SUBSELECT_ITEM;
  });
}

bool CostingReceiver::ProposeRefAccess(TABLE *table, int node_idx, KEY *key,
                                       unsigned key_idx,
                                       table_map allowed_parameter_tables) {
  // NOTE: visible_index claims to contain “visible and enabled” indexes,
  // but we still need to check keys_in_use to ignore disabled indexes.
  if (!table->keys_in_use_for_query.is_set(key_idx) ||
      (key->flags & HA_FULLTEXT)) {
    return false;
  }

  // Go through each of the sargable predicates and see how many key parts
  // we can match.
  unsigned matched_keyparts = 0;
  unsigned length = 0;
  const unsigned usable_keyparts = actual_key_parts(key);
  KeypartForRef keyparts[MAX_REF_PARTS];
  table_map parameter_tables = 0;

  if (PopulationCount(allowed_parameter_tables) >
      static_cast<int>(usable_keyparts)) {
    // It is inevitable that we fail the (parameter_tables ==
    // allowed_parameter_tables) test below, so error out earlier.
    return false;
  }

  for (unsigned keypart_idx = 0;
       keypart_idx < usable_keyparts && keypart_idx < MAX_REF_PARTS;
       ++keypart_idx) {
    const KEY_PART_INFO &keyinfo = key->key_part[keypart_idx];
    bool matched_this_keypart = false;

    for (const SargablePredicate &sp :
         m_graph.nodes[node_idx].sargable_predicates) {
      if (!sp.field->part_of_key.is_set(key_idx)) {
        // Quick reject.
        continue;
      }
      Item_func_eq *item = down_cast<Item_func_eq *>(
          m_graph.predicates[sp.predicate_index].condition);
      if (sp.field->eq(keyinfo.field) &&
          comparable_in_index(item, sp.field, Field::itRAW, item->functype(),
                              sp.other_side) &&
          !(sp.field->cmp_type() == STRING_RESULT &&
            sp.field->match_collation_to_optimize_range() &&
            sp.field->charset() != item->compare_collation())) {
        // x = const. (And true const or an outer reference,
        // just not const_for_execution(); so no execution
        // of queries during optimization.)
        if (sp.other_side->const_item() ||
            sp.other_side->used_tables() == OUTER_REF_TABLE_BIT) {
          matched_this_keypart = true;
          keyparts[keypart_idx].field = sp.field;
          keyparts[keypart_idx].condition = item;
          keyparts[keypart_idx].val = sp.other_side;
          keyparts[keypart_idx].null_rejecting = true;
          keyparts[keypart_idx].used_tables = item->used_tables();
          ++matched_keyparts;
          length += keyinfo.store_length;
          break;
        }

        // x = other_table.field.
        if (sp.other_side->type() == Item::FIELD_ITEM &&
            IsSubset(sp.other_side->used_tables(), allowed_parameter_tables)) {
          parameter_tables |= sp.other_side->used_tables();
          matched_this_keypart = true;
          keyparts[keypart_idx].field = sp.field;
          keyparts[keypart_idx].condition = item;
          keyparts[keypart_idx].val = sp.other_side;
          keyparts[keypart_idx].null_rejecting = true;
          keyparts[keypart_idx].used_tables = item->used_tables();
          ++matched_keyparts;
          length += keyinfo.store_length;
          break;
        }
      }
    }
    if (!matched_this_keypart) {
      break;
    }
  }
  if (matched_keyparts == 0) {
    return false;
  }
  if (parameter_tables != allowed_parameter_tables) {
    // We've already seen this before, with a more lenient subset,
    // so don't try it again.
    return false;
  }

  if (matched_keyparts < usable_keyparts &&
      (table->file->index_flags(key_idx, 0, false) & HA_ONLY_WHOLE_INDEX)) {
    if (m_trace != nullptr) {
      *m_trace += StringPrintf(
          " - %s is whole-key only, and we could only match %d/%d "
          "key parts for ref access\n",
          key->name, matched_keyparts, usable_keyparts);
    }
    return false;
  }

  if (m_trace != nullptr) {
    if (matched_keyparts < usable_keyparts) {
      *m_trace += StringPrintf(
          " - %s is applicable for ref access (using %d/%d key parts only)\n",
          key->name, matched_keyparts, usable_keyparts);
    } else {
      *m_trace +=
          StringPrintf(" - %s is applicable for ref access\n", key->name);
    }
  }

  // Create TABLE_REF for this ref, and set it up based on the chosen keyparts.
  TABLE_REF *ref = new (m_thd->mem_root) TABLE_REF;
  if (init_ref(m_thd, matched_keyparts, length, key_idx, ref)) {
    return true;
  }

  uchar *key_buff = ref->key_buff;
  uchar *null_ref_key = nullptr;
  bool null_rejecting_key = true;
  for (unsigned keypart_idx = 0; keypart_idx < matched_keyparts;
       keypart_idx++) {
    KeypartForRef *keypart = &keyparts[keypart_idx];
    const KEY_PART_INFO *keyinfo = &key->key_part[keypart_idx];

    if (init_ref_part(m_thd, keypart_idx, keypart->val, /*cond_guard=*/nullptr,
                      keypart->null_rejecting, /*const_tables=*/0,
                      keypart->used_tables, keyinfo->null_bit, keyinfo,
                      key_buff, ref)) {
      return true;
    }
    // TODO(sgunders): When we get support for REF_OR_NULL,
    // set null_ref_key = key_buff here if appropriate.
    /*
      The selected key will reject matches on NULL values if:
       - the key field is nullable, and
       - predicate rejects NULL values (keypart->null_rejecting is true), or
       - JT_REF_OR_NULL is not effective.
    */
    if ((keyinfo->field->is_nullable() || table->is_nullable()) &&
        (!keypart->null_rejecting || null_ref_key != nullptr)) {
      null_rejecting_key = false;
    }
    key_buff += keyinfo->store_length;
  }

  double num_output_rows = table->file->stats.records;

  uint64_t applied_predicates = 0;
  uint64_t subsumed_predicates = 0;
  for (size_t i = 0; i < m_graph.predicates.size(); ++i) {
    int keypart_idx = WasPushedDownToRef(m_graph.predicates[i].condition,
                                         keyparts, matched_keyparts);
    if (keypart_idx == -1) {
      continue;
    }

    num_output_rows *= m_graph.predicates[i].selectivity;
    applied_predicates |= uint64_t{1} << i;

    const KeypartForRef &keypart = keyparts[keypart_idx];
    if (ref_lookup_subsumes_comparison(keypart.field, keypart.val)) {
      if (m_trace != nullptr) {
        *m_trace +=
            StringPrintf(" - %s is subsumed by ref access on %s.%s\n",
                         ItemToString(m_graph.predicates[i].condition).c_str(),
                         table->alias, keypart.field->field_name);
      }
      subsumed_predicates |= uint64_t{1} << i;
    } else {
      if (m_trace != nullptr) {
        *m_trace += StringPrintf(
            " - %s is not fully subsumed by ref access on %s.%s, keeping\n",
            ItemToString(m_graph.predicates[i].condition).c_str(), table->alias,
            keypart.field->field_name);
      }
    }
  }

  // We are guaranteed to get a single row back if all of these hold:
  //
  //  - The index must be unique.
  //  - We can never query it with NULL (ie., no keyparts are nullable,
  //    or our condition is already NULL-rejecting), since NULL is
  //    an exception for unique indexes.
  //  - We use all key parts.
  //
  // This matches the logic in create_ref_for_key().
  const bool single_row = Overlaps(actual_key_flags(key), HA_NOSAME) &&
                          (!Overlaps(actual_key_flags(key), HA_NULL_PART_KEY) ||
                           null_rejecting_key) &&
                          matched_keyparts == usable_keyparts;
  if (single_row) {
    num_output_rows = std::min(num_output_rows, 1.0);
  }

  // When asking the cost model for costs, the API takes in a double,
  // but truncates it to an unsigned integer. This means that if we
  // expect an index lookup to give e.g. 0.9 rows on average, the cost
  // model will assume we get back 0 -- and even worse, InnoDB's
  // cost model gives a cost of exactly zero for this case, ignoring
  // entirely the startup cost! Obviously, a cost of zero would make
  // it very attractive to line up a bunch of such lookups in a nestloop
  // and nestloop-join against them, crowding out pretty much any other
  // way to do a join, so to counteract both of these issues, we
  // explicitly round up here. This can be removed if InnoDB's
  // cost model is tuned better for this case.
  const double hacked_num_output_rows = ceil(num_output_rows);

  const double table_scan_cost = table->file->table_scan_cost().total_cost();
  const double worst_seeks = find_worst_seeks(
      table->cost_model(), hacked_num_output_rows, table_scan_cost);
  const double cost = find_cost_for_ref(m_thd, table, key_idx,
                                        hacked_num_output_rows, worst_seeks);

  AccessPath path;
  if (single_row) {
    path.type = AccessPath::EQ_REF;
    path.eq_ref().table = table;
    path.eq_ref().ref = ref;
    path.eq_ref().use_order = false;
  } else {
    path.type = AccessPath::REF;
    path.ref().table = table;
    path.ref().ref = ref;
    path.ref().use_order = false;
    path.ref().reverse = false;
  }
  path.ordering_state = 0;
  path.num_output_rows_before_filter = num_output_rows;
  path.cost_before_filter = cost;
  path.init_cost = path.init_once_cost = 0.0;
  path.parameter_tables = GetNodeMapFromTableMap(
      parameter_tables & ~table->pos_in_table_list->map(),
      m_graph.table_num_to_node_num);

  for (bool materialize_subqueries : {false, true}) {
    FunctionalDependencySet new_fd_set;
    ApplyPredicatesForBaseTable(node_idx, applied_predicates,
                                subsumed_predicates, materialize_subqueries,
                                &path, &new_fd_set);
    path.ordering_state =
        m_orderings->ApplyFDs(path.ordering_state, new_fd_set);
    path.applied_sargable_join_predicates |=
        applied_predicates & ~BitsBetween(0, m_graph.num_where_predicates);
    path.subsumed_sargable_join_predicates |=
        subsumed_predicates & ~BitsBetween(0, m_graph.num_where_predicates);

    ProposeAccessPathWithOrderings(
        TableBitmap(node_idx), new_fd_set, &path,
        materialize_subqueries ? "mat. subq" : key->name);

    if (!Overlaps(path.filter_predicates, m_graph.materializable_predicates)) {
      // Nothing to try to materialize.
      break;
    }
  }

  return false;
}

bool CostingReceiver::ProposeTableScan(TABLE *table, int node_idx,
                                       bool is_recursive_reference) {
  AccessPath path;
  if (is_recursive_reference) {
    path.type = AccessPath::FOLLOW_TAIL;
    path.follow_tail().table = table;
    assert(forced_leftmost_table == 0);  // There can only be one, naturally.
    forced_leftmost_table = NodeMap{1} << node_idx;

    // This will obviously grow, and it is zero now, so force a fairly arbitrary
    // minimum.
    // TODO(sgunders): We should probably go into the CTE and look at its number
    // of expected output rows, which is another minimum.
    table->file->stats.records =
        std::max<ha_rows>(table->file->stats.records, 1000);
  } else {
    path.type = AccessPath::TABLE_SCAN;
    path.table_scan().table = table;
  }
  path.count_examined_rows = true;
  path.ordering_state = 0;

  // Doing at least one table scan (this one), so mark the query as such.
  // TODO(sgunders): Move out when we get more types and this access path could
  // be replaced by something else.
  m_thd->set_status_no_index_used();

  double num_output_rows = table->file->stats.records;
  double cost = table->file->table_scan_cost().total_cost();

  path.num_output_rows_before_filter = num_output_rows;
  path.init_cost = path.init_once_cost = 0.0;
  path.cost_before_filter = path.cost = cost;

  if (m_trace != nullptr) {
    *m_trace +=
        StringPrintf("\nFound node %s [rows=%.0f]\n",
                     m_graph.nodes[node_idx].table->alias, num_output_rows);
    for (int pred_idx : BitsSetIn(path.filter_predicates)) {
      *m_trace += StringPrintf(
          " - applied predicate %s\n",
          ItemToString(m_graph.predicates[pred_idx].condition).c_str());
    }
  }

  // See if this is an information schema table that must be filled in before
  // we scan.
  TABLE_LIST *tl = table->pos_in_table_list;
  if (tl->schema_table != nullptr && tl->schema_table->fill_table) {
    // TODO(sgunders): We don't need to allocate materialize_path on the
    // MEM_ROOT.
    AccessPath *new_path = new (m_thd->mem_root) AccessPath(path);
    AccessPath *materialize_path =
        NewMaterializeInformationSchemaTableAccessPath(m_thd, new_path, tl,
                                                       /*condition=*/nullptr);

    materialize_path->num_output_rows = path.num_output_rows;
    materialize_path->num_output_rows_before_filter =
        path.num_output_rows_before_filter;
    materialize_path->init_cost = path.cost;       // Rudimentary.
    materialize_path->init_once_cost = path.cost;  // Rudimentary.
    materialize_path->cost_before_filter = path.cost;
    materialize_path->cost = path.cost;
    materialize_path->filter_predicates = path.filter_predicates;
    materialize_path->delayed_predicates = path.delayed_predicates;
    new_path->filter_predicates = new_path->delayed_predicates = 0;

    // Some information schema tables have zero as estimate, which can lead
    // to completely wild plans. Add a placeholder to make sure we have
    // _something_ to work with.
    if (materialize_path->num_output_rows_before_filter == 0) {
      new_path->num_output_rows = 1000;
      new_path->num_output_rows_before_filter = 1000;
      materialize_path->num_output_rows = 1000;
      materialize_path->num_output_rows_before_filter = 1000;
    }

    assert(!tl->uses_materialization());
    path = *materialize_path;
    assert(path.cost >= 0.0);
  } else if (tl->uses_materialization()) {
    // Move the path to stable storage, since we'll be referring to it.
    AccessPath *stable_path = new (m_thd->mem_root) AccessPath(path);

    // TODO(sgunders): We don't need to allocate materialize_path on the
    // MEM_ROOT.
    AccessPath *materialize_path;
    if (tl->is_table_function()) {
      materialize_path = NewMaterializedTableFunctionAccessPath(
          m_thd, table, tl->table_function, stable_path);
      CopyBasicProperties(*stable_path, materialize_path);
      materialize_path->cost_before_filter = materialize_path->init_cost =
          materialize_path->init_once_cost = materialize_path->cost;
      materialize_path->num_output_rows_before_filter =
          materialize_path->num_output_rows;

      if (materialize_path->num_output_rows_before_filter <= 0.0) {
        materialize_path->num_output_rows = 1000.0;
        materialize_path->num_output_rows_before_filter = 1000.0;
      }

      materialize_path->parameter_tables = GetNodeMapFromTableMap(
          tl->table_function->used_tables() & ~PSEUDO_TABLE_BITS,
          m_graph.table_num_to_node_num);
      if (Overlaps(tl->table_function->used_tables(),
                   OUTER_REF_TABLE_BIT | RAND_TABLE_BIT)) {
        // Make sure the table function is never hashed, ever.
        materialize_path->parameter_tables |= RAND_TABLE_BIT;
      }
    } else {
      bool rematerialize = tl->derived_query_expression()->uncacheable != 0;
      if (tl->common_table_expr()) {
        // Handled in clear_corr_derived_tmp_tables(), not here.
        rematerialize = false;
      }
      materialize_path = GetAccessPathForDerivedTable(
          m_thd, tl, table, rematerialize,
          /*invalidators=*/nullptr, m_need_rowid, stable_path);
      // Handle LATERAL.
      materialize_path->parameter_tables =
          GetNodeMapFromTableMap(tl->derived_query_expression()->m_lateral_deps,
                                 m_graph.table_num_to_node_num);
    }

    materialize_path->filter_predicates = path.filter_predicates;
    materialize_path->delayed_predicates = path.delayed_predicates;
    stable_path->filter_predicates = stable_path->delayed_predicates = 0;
    path = *materialize_path;
    assert(path.cost >= 0.0);
  }
  assert(path.cost >= 0.0);

  for (bool materialize_subqueries : {false, true}) {
    FunctionalDependencySet new_fd_set;
    ApplyPredicatesForBaseTable(node_idx, /*applied_predicates=*/0,
                                /*subsumed_predicates=*/0,
                                materialize_subqueries, &path, &new_fd_set);
    path.ordering_state =
        m_orderings->ApplyFDs(path.ordering_state, new_fd_set);
    ProposeAccessPathWithOrderings(TableBitmap(node_idx), new_fd_set, &path,
                                   materialize_subqueries ? "mat. subq" : "");

    if (!Overlaps(path.filter_predicates, m_graph.materializable_predicates)) {
      // Nothing to try to materialize.
      break;
    }
  }  // namespace

  return false;
}

/**
  See which predicates that apply to this table. Some can be applied
  right away, some require other tables first and must be delayed.

  @param node_idx Index of the base table in the nodes array.
  @param applied_predicates Bitmap of predicates that are already
    applied by means of ref access, and should not be recalculated selectivity
    for.
  @param subsumed_predicates Bitmap of predicates that are applied
    by means of ref access and do not need to rechecked. Overrides
    applied_predicates.
  @param materialize_subqueries If true, any subqueries in the
    predicate should be materialized. (If there are multiple ones,
    this is an all-or-nothing decision, for simplicity.)
  @param [in,out] path The access path to apply the predicates to.
    Note that if materialize_subqueries is true, a FILTER access path
    will be inserted (overwriting "path", although a copy of it will
    be set as a child), as AccessPath::filter_predicates always assumes
    non-materialized subqueries.
 */
void CostingReceiver::ApplyPredicatesForBaseTable(
    int node_idx, uint64_t applied_predicates, uint64_t subsumed_predicates,
    bool materialize_subqueries, AccessPath *path,
    FunctionalDependencySet *new_fd_set) {
  double materialize_cost = 0.0;

  const NodeMap my_map = TableBitmap(node_idx);
  path->num_output_rows = path->num_output_rows_before_filter;
  path->cost = path->cost_before_filter;
  path->filter_predicates = 0;
  path->delayed_predicates = 0;
  new_fd_set->reset();
  for (size_t i = 0; i < m_graph.num_where_predicates; ++i) {
    if (subsumed_predicates & (uint64_t{1} << i)) {
      // Apply functional dependencies for the base table, but no others;
      // this ensures we get the same functional dependencies set no matter what
      // access path we choose. (The ones that refer to multiple tables,
      // which are fairly rare, are not really relevant before the other
      // table(s) have been joined in.)
      if (m_graph.predicates[i].total_eligibility_set == my_map) {
        *new_fd_set |= m_graph.predicates[i].functional_dependencies;
      } else {
        // We have a WHERE predicate that refers to multiple tables,
        // that we can subsume as if it were a join condition
        // (perhaps because it was identical to an actual join condition).
        // The other side of the join will mark it as delayed, so we
        // need to do so, too. Otherwise, we would never apply the
        // associated functional dependency at the right time.
        path->delayed_predicates |= uint64_t{1} << i;
      }
      continue;
    }
    if (m_graph.predicates[i].total_eligibility_set == my_map) {
      path->filter_predicates |= uint64_t{1} << i;
      FilterCost cost =
          EstimateFilterCost(m_thd, path->num_output_rows,
                             m_graph.predicates[i].condition, m_query_block);
      if (materialize_subqueries) {
        path->cost += cost.cost_if_materialized;
        materialize_cost += cost.cost_to_materialize;
      } else {
        path->cost += cost.cost_if_not_materialized;
      }
      if (applied_predicates & (uint64_t{1} << i)) {
        // We already factored in this predicate when calculating
        // the selectivity of the ref access, so don't do it again.
      } else {
        path->num_output_rows *= m_graph.predicates[i].selectivity;
      }
      *new_fd_set |= m_graph.predicates[i].functional_dependencies;
    } else if (Overlaps(m_graph.predicates[i].total_eligibility_set, my_map)) {
      path->delayed_predicates |= uint64_t{1} << i;
    }
  }

  if (materialize_subqueries) {
    ExpandSingleFilterAccessPath(m_thd, path, m_graph.predicates,
                                 m_graph.num_where_predicates);
    assert(path->type == AccessPath::FILTER);
    path->filter().materialize_subqueries = true;
    path->cost += materialize_cost;  // Will be subtracted back for rescans.
    path->init_cost += materialize_cost;
    path->init_once_cost += materialize_cost;
  }
}

/**
  Called to signal that it's possible to connect the non-overlapping
  table subsets “left” and “right” through the edge given by “edge_idx”
  (which corresponds to an index in m_graph.edges), ie., we have found
  a legal subplan for joining (left ∪ right). Assign it a cost based on
  the cost of the children and the join method we use. (Currently, there
  is only one -- hash join.)

  There may be multiple such calls for the same subplan; e.g. for
  inner-joining {t1,t2,t3}, we will get calls for both {t1}/{t2,t3}
  and {t1,t2}/{t3}, and need to assign costs to both and keep the
  cheapest one. However, we will not get calls with the two subsets
  in reversed order.
 */
bool CostingReceiver::FoundSubgraphPair(NodeMap left, NodeMap right,
                                        int edge_idx) {
  if (m_thd->is_error()) return true;

  assert(left != 0);
  assert(right != 0);
  assert((left & right) == 0);

  const JoinPredicate *edge = &m_graph.edges[edge_idx];
  if (!PassesConflictRules(left | right, edge->expr)) {
    return false;
  }

  bool is_commutative = OperatorIsCommutative(*edge->expr);

  // Enforce that recursive references need to be leftmost.
  if (Overlaps(right, forced_leftmost_table)) {
    if (!is_commutative) {
      assert(IsSingleBitSet(forced_leftmost_table));
      const int node_idx = FindLowestBitSet(forced_leftmost_table);
      my_error(ER_CTE_RECURSIVE_FORBIDDEN_JOIN_ORDER, MYF(0),
               m_graph.nodes[node_idx].table->alias);
      return true;
    }
    swap(left, right);
  }
  if (Overlaps(left, forced_leftmost_table)) {
    is_commutative = false;
  }

  auto left_it = m_access_paths.find(left);
  assert(left_it != m_access_paths.end());
  auto right_it = m_access_paths.find(right);
  assert(right_it != m_access_paths.end());

  const FunctionalDependencySet new_fd_set =
      left_it->second.active_functional_dependencies |
      right_it->second.active_functional_dependencies |
      edge->functional_dependencies;

  bool wrote_trace = false;

  for (AccessPath *left_path : left_it->second.paths) {
    for (AccessPath *right_path : right_it->second.paths) {
      // For inner joins and full outer joins, the order does not matter.
      // In lieu of a more precise cost model, always keep the one that hashes
      // the fewest amount of rows. (This has lower initial cost, and the same
      // cost.) When cost estimates are supplied by the secondary engine,
      // explore both orders, since the secondary engine might unilaterally
      // decide to prefer or reject one particular order.
      if (is_commutative && m_secondary_engine_cost_hook == nullptr) {
        if (left_path->num_output_rows < right_path->num_output_rows) {
          ProposeHashJoin(right, left, right_path, left_path, edge, new_fd_set,
                          &wrote_trace);
        } else {
          ProposeHashJoin(left, right, left_path, right_path, edge, new_fd_set,
                          &wrote_trace);
        }
      } else {
        ProposeHashJoin(left, right, left_path, right_path, edge, new_fd_set,
                        &wrote_trace);
        if (is_commutative) {
          ProposeHashJoin(right, left, right_path, left_path, edge, new_fd_set,
                          &wrote_trace);
        }
      }

      ProposeNestedLoopJoin(left, right, left_path, right_path, edge,
                            new_fd_set);
      if (is_commutative) {
        ProposeNestedLoopJoin(right, left, right_path, left_path, edge,
                              new_fd_set);
      }

      if (m_access_paths.size() > 100000) {
        // Bail out; we're going to be needing graph simplification
        // (a separate worklog).
        return true;
      }
    }
  }
  return false;
}

double FindOutputRowsForJoin(AccessPath *left_path, AccessPath *right_path,
                             const JoinPredicate *edge,
                             double already_applied_selectivity) {
  const double outer_rows = left_path->num_output_rows;
  const double inner_rows = right_path->num_output_rows;
  const double selectivity = edge->selectivity / already_applied_selectivity;
  if (edge->expr->type == RelationalExpression::ANTIJOIN) {
    return outer_rows * (1.0 - selectivity);
  } else if (edge->expr->type == RelationalExpression::SEMIJOIN) {
    return outer_rows * selectivity;
  } else {
    double num_output_rows = outer_rows * inner_rows * selectivity;
    if (edge->expr->type == RelationalExpression::LEFT_JOIN) {
      num_output_rows = std::max(num_output_rows, outer_rows);
    }
    return num_output_rows;
  }
}

void CostingReceiver::ProposeHashJoin(NodeMap left, NodeMap right,
                                      AccessPath *left_path,
                                      AccessPath *right_path,
                                      const JoinPredicate *edge,
                                      FunctionalDependencySet new_fd_set,
                                      bool *wrote_trace) {
  if (!SupportedAccessPathType(AccessPath::HASH_JOIN)) return;

  if (Overlaps(left_path->parameter_tables, right) ||
      right_path->parameter_tables != 0) {
    // Parametrized paths must be solved by nested loop.
    // We can still have parameters from outside the join,
    // but only on the outer side.
    return;
  }

  AccessPath join_path;
  join_path.type = AccessPath::HASH_JOIN;
  join_path.parameter_tables =
      (left_path->parameter_tables | right_path->parameter_tables) &
      ~(left | right);
  join_path.hash_join().outer = left_path;
  join_path.hash_join().inner = right_path;
  join_path.hash_join().join_predicate = edge;
  join_path.hash_join().store_rowids = false;
  join_path.hash_join().tables_to_get_rowid_for = 0;
  join_path.hash_join().allow_spill_to_disk = true;

  double num_output_rows = FindOutputRowsForJoin(
      left_path, right_path, edge, /*already_applied_selectivity=*/1.0);

  // TODO(sgunders): Add estimates for spill-to-disk costs.
  const double build_cost =
      right_path->cost + right_path->num_output_rows * kHashBuildOneRowCost;
  double cost =
      left_path->cost + build_cost +
      (left_path->num_output_rows + num_output_rows) * kHashProbeOneRowCost;

  // Note: This isn't strictly correct if the non-equijoin conditions
  // have selectivities far from 1.0; the cost should be calculated
  // on the number of rows after the equijoin conditions, but before
  // the non-equijoin conditions.
  cost += num_output_rows * edge->expr->join_conditions.size() *
          kApplyOneFilterCost;

  join_path.num_output_rows_before_filter = num_output_rows;
  join_path.cost_before_filter = cost;
  join_path.num_output_rows = num_output_rows;
  join_path.init_cost = build_cost + left_path->init_cost;

  const double hash_memory_used_bytes =
      edge->estimated_bytes_per_row * right_path->num_output_rows;
  if (hash_memory_used_bytes <= m_thd->variables.join_buff_size * 0.9) {
    // Fits in memory (with 10% estimation margin), so the hash table can be
    // reused.
    join_path.init_once_cost = build_cost + left_path->init_once_cost;
  } else {
    join_path.init_once_cost =
        left_path->init_once_cost + right_path->init_once_cost;
  }
  join_path.cost = cost;

  // Only trace once; the rest ought to be identical.
  if (m_trace != nullptr && !*wrote_trace) {
    *m_trace += StringPrintf(
        "\nFound sets %s and %s, connected by condition %s [rows=%.0f]\n",
        PrintSet(left).c_str(), PrintSet(right).c_str(),
        GenerateExpressionLabel(edge->expr).c_str(), join_path.num_output_rows);
    for (int pred_idx : BitsSetIn(join_path.filter_predicates)) {
      *m_trace += StringPrintf(
          " - applied (delayed) predicate %s\n",
          ItemToString(m_graph.predicates[pred_idx].condition).c_str());
    }
    *wrote_trace = true;
  }

  {
    FunctionalDependencySet filter_fd_set;
    ApplyDelayedPredicatesAfterJoin(left, right, left_path, right_path,
                                    /*materialize_subqueries=*/false,
                                    &join_path, &filter_fd_set);
    // Hash join destroys all ordering information (even from the left side,
    // since we may have spill-to-disk).
    join_path.ordering_state = m_orderings->ApplyFDs(
        m_orderings->SetOrder(0), new_fd_set | filter_fd_set);
    ProposeAccessPathWithOrderings(left | right, new_fd_set | filter_fd_set,
                                   &join_path, "hash join");
  }

  if (Overlaps(join_path.filter_predicates,
               m_graph.materializable_predicates)) {
    FunctionalDependencySet filter_fd_set;
    ApplyDelayedPredicatesAfterJoin(left, right, left_path, right_path,
                                    /*materialize_subqueries=*/true, &join_path,
                                    &filter_fd_set);
    // Hash join destroys all ordering information (even from the left side,
    // since we may have spill-to-disk).
    join_path.ordering_state = m_orderings->ApplyFDs(
        m_orderings->SetOrder(0), new_fd_set | filter_fd_set);
    ProposeAccessPathWithOrderings(left | right, new_fd_set | filter_fd_set,
                                   &join_path, "hash join, mat. subq");
  }
}

// Of all delayed predicates, see which ones we can apply now, and which
// ones that need to be delayed further.
void CostingReceiver::ApplyDelayedPredicatesAfterJoin(
    NodeMap left, NodeMap right, const AccessPath *left_path,
    const AccessPath *right_path, bool materialize_subqueries,
    AccessPath *join_path, FunctionalDependencySet *new_fd_set) {
  // We build up a new FD set each time; it should be the same for the same
  // left/right pair, so it is somewhat redundant, but it allows us to verify
  // that property through the assert in ProposeAccessPathWithOrderings().
  new_fd_set->reset();

  double materialize_cost = 0.0;

  // Keep the information about applied_sargable_join_predicates,
  // but reset the one pertaining to filter_predicates.
  join_path->applied_sargable_join_predicates =
      (left_path->applied_sargable_join_predicates |
       right_path->applied_sargable_join_predicates) &
      ~TablesBetween(0, m_graph.num_where_predicates);
  join_path->delayed_predicates =
      left_path->delayed_predicates ^ right_path->delayed_predicates;
  const NodeMap ready_tables = left | right;
  for (int pred_idx : BitsSetIn(left_path->delayed_predicates &
                                right_path->delayed_predicates)) {
    if (IsSubset(m_graph.predicates[pred_idx].total_eligibility_set,
                 ready_tables)) {
      join_path->filter_predicates |= uint64_t{1} << pred_idx;
      FilterCost cost = EstimateFilterCost(
          m_thd, join_path->num_output_rows,
          m_graph.predicates[pred_idx].condition, m_query_block);
      if (materialize_subqueries) {
        join_path->cost += cost.cost_if_materialized;
        materialize_cost += cost.cost_to_materialize;
      } else {
        join_path->cost += cost.cost_if_not_materialized;
      }
      join_path->num_output_rows *= m_graph.predicates[pred_idx].selectivity;
      *new_fd_set |= m_graph.predicates[pred_idx].functional_dependencies;
    } else {
      join_path->delayed_predicates |= uint64_t{1} << pred_idx;
    }
  }

  if (materialize_subqueries) {
    ExpandSingleFilterAccessPath(m_thd, join_path, m_graph.predicates,
                                 m_graph.num_where_predicates);
    assert(join_path->type == AccessPath::FILTER);
    join_path->filter().materialize_subqueries = true;
    join_path->cost +=
        materialize_cost;  // Will be subtracted back for rescans.
    join_path->init_cost += materialize_cost;
    join_path->init_once_cost += materialize_cost;
  }
}

static string PrintCost(const AccessPath &path, const JoinHypergraph &graph,
                        const char *description_for_trace);

void CostingReceiver::ProposeNestedLoopJoin(
    NodeMap left, NodeMap right, AccessPath *left_path, AccessPath *right_path,
    const JoinPredicate *edge, FunctionalDependencySet new_fd_set) {
  if (!SupportedAccessPathType(AccessPath::NESTED_LOOP_JOIN)) return;

  if (Overlaps(left_path->parameter_tables, right)) {
    // The outer table cannot pick up values from the inner,
    // only the other way around.
    return;
  }

  AccessPath join_path;
  join_path.type = AccessPath::NESTED_LOOP_JOIN;
  join_path.parameter_tables =
      (left_path->parameter_tables | right_path->parameter_tables) &
      ~(left | right);
  join_path.nested_loop_join().outer = left_path;
  join_path.nested_loop_join().inner = right_path;
  if (edge->expr->type == RelationalExpression::STRAIGHT_INNER_JOIN) {
    join_path.nested_loop_join().join_type = JoinType::INNER;
  } else {
    join_path.nested_loop_join().join_type =
        static_cast<JoinType>(edge->expr->type);
  }
  join_path.nested_loop_join().pfs_batch_mode = false;

  const uint64_t applied_sargable_join_predicates =
      left_path->applied_sargable_join_predicates |
      right_path->applied_sargable_join_predicates;
  const uint64_t subsumed_sargable_join_predicates =
      left_path->subsumed_sargable_join_predicates |
      right_path->subsumed_sargable_join_predicates;

  double already_applied_selectivity = 1.0;
  if (edge->expr->equijoin_conditions.size() != 0 ||
      edge->expr->join_conditions.size() != 0) {
    // Apply join filters. Don't update num_output_rows, as the join's
    // selectivity will already be applied in FindOutputRowsForJoin().
    // NOTE(sgunders): We don't model the effect of short-circuiting filters on
    // the cost here.
    AccessPath filter_path;
    filter_path.type = AccessPath::FILTER;
    filter_path.filter().child = right_path;

    // We don't bother trying to materialize subqueries in join conditions,
    // since they should be very rare.
    filter_path.filter().materialize_subqueries = false;

    CopyBasicProperties(*right_path, &filter_path);

    // num_output_rows is only for cost calculation and display purposes;
    // we hard-code the use of edge->selectivity below, so that we're
    // seeing the same number of rows as for hash join. This might throw
    // the filtering cost off slightly.
    List<Item> items;
    for (Item_func_eq *condition : edge->expr->equijoin_conditions) {
      const auto it = m_graph.sargable_join_predicates.find(condition);
      bool subsumed = false;
      if (it != m_graph.sargable_join_predicates.end() &&
          Overlaps(applied_sargable_join_predicates,
                   uint64_t{1} << it->second)) {
        // This predicate was already applied as a ref access earlier.
        // Make sure not to double-count its selectivity, and also
        // that we don't reapply it if it was subsumed by the ref access.
        already_applied_selectivity *=
            m_graph.predicates[it->second].selectivity;
        subsumed = Overlaps(subsumed_sargable_join_predicates,
                            uint64_t{1} << it->second);
      }
      if (!subsumed) {
        items.push_back(condition);
        filter_path.cost +=
            EstimateFilterCost(m_thd, filter_path.num_output_rows, condition,
                               m_query_block)
                .cost_if_not_materialized;
        filter_path.num_output_rows *=
            EstimateSelectivity(m_thd, condition, m_trace);
      }
    }
    for (Item *condition : edge->expr->join_conditions) {
      items.push_back(condition);
      filter_path.cost += EstimateFilterCost(m_thd, filter_path.num_output_rows,
                                             condition, m_query_block)
                              .cost_if_not_materialized;
      filter_path.num_output_rows *=
          EstimateSelectivity(m_thd, condition, m_trace);
    }
    if (items.is_empty()) {
      // Everything was subsumed, so no filter needed after all.
    } else {
      Item *condition;
      if (items.size() == 1) {
        condition = items.head();
      } else {
        condition = new Item_cond_and(items);
        condition->quick_fix_field();
        condition->update_used_tables();
        condition->apply_is_true();
      }
      filter_path.filter().condition = condition;

      join_path.nested_loop_join().inner =
          new (m_thd->mem_root) AccessPath(filter_path);
    }
  }

  // Ignores the row count from filter_path; see above.
  join_path.num_output_rows_before_filter = join_path.num_output_rows =
      FindOutputRowsForJoin(left_path, right_path, edge,
                            already_applied_selectivity);
  const AccessPath *inner = join_path.nested_loop_join().inner;
  double inner_rescan_cost = inner->cost - inner->init_once_cost;
  join_path.init_cost = left_path->init_cost;
  join_path.cost_before_filter = join_path.cost =
      left_path->cost + inner->init_cost +
      inner_rescan_cost * left_path->num_output_rows;

  // Nested-loop preserves any ordering from the outer side. Note that actually,
  // the two orders are _concatenated_ (if you nested-loop join something
  // ordered on (a,b) with something joined on (c,d), the order will be
  // (a,b,c,d)), but the state machine has no way of representing that.
  join_path.ordering_state =
      m_orderings->ApplyFDs(left_path->ordering_state, new_fd_set);

  {
    FunctionalDependencySet filter_fd_set;
    ApplyDelayedPredicatesAfterJoin(left, right, left_path, right_path,
                                    /*materialize_subqueries=*/false,
                                    &join_path, &filter_fd_set);
    join_path.ordering_state = m_orderings->ApplyFDs(
        join_path.ordering_state, new_fd_set | filter_fd_set);
    ProposeAccessPathWithOrderings(left | right, new_fd_set | filter_fd_set,
                                   &join_path, "nested loop");
  }

  if (Overlaps(join_path.filter_predicates,
               m_graph.materializable_predicates)) {
    FunctionalDependencySet filter_fd_set;
    ApplyDelayedPredicatesAfterJoin(left, right, left_path, right_path,
                                    /*materialize_subqueries=*/true, &join_path,
                                    &filter_fd_set);
    join_path.ordering_state = m_orderings->ApplyFDs(
        join_path.ordering_state, new_fd_set | filter_fd_set);
    ProposeAccessPathWithOrderings(left | right, new_fd_set | filter_fd_set,
                                   &join_path, "nested loop, mat. subq");
  }
}

enum class PathComparisonResult {
  FIRST_DOMINATES,
  SECOND_DOMINATES,
  DIFFERENT_STRENGTHS,
  IDENTICAL,
};

// See if one access path is better than the other across all cost dimensions
// (if so, we say it dominates the other one). If not, we return
// DIFFERENT_STRENGTHS so that both must be kept.
//
// TODO(sgunders): If one path is better than the other in cost, and only
// slightly worse (e.g. 1%) in a less important metric such as init_cost,
// consider pruning the latter.
//
// TODO(sgunders): Support turning off certain cost dimensions; e.g., init_cost
// only matters if we have a LIMIT or nested loop semijoin somewhere in the
// query, and it might not matter for secondary engine.
static inline PathComparisonResult CompareAccessPaths(
    const LogicalOrderings &orderings, const AccessPath &a,
    const AccessPath &b) {
  bool a_is_better = false, b_is_better = false;
  if (a.cost < b.cost) {
    a_is_better = true;
  } else if (b.cost < a.cost) {
    b_is_better = true;
  }

  if (a.init_cost < b.init_cost) {
    a_is_better = true;
  } else if (b.init_cost < a.init_cost) {
    b_is_better = true;
  }

  if (a.init_once_cost < b.init_once_cost) {
    a_is_better = true;
  } else if (b.init_once_cost < a.init_once_cost) {
    b_is_better = true;
  }

  if (a.parameter_tables != b.parameter_tables) {
    if (!IsSubset(a.parameter_tables, b.parameter_tables)) {
      b_is_better = true;
    }
    if (!IsSubset(b.parameter_tables, a.parameter_tables)) {
      a_is_better = true;
    }
  }

  // If we have a parametrized path, this means that at some point, it _must_
  // be on the right side of a nested-loop join. This destroys ordering
  // information (at least in our implementation -- see comment in
  // NestedLoopJoin()), so in this situation, consider all orderings as equal.
  // (This is a trick borrowed from Postgres to keep the number of unique access
  // paths down in such situations.)
  const int a_ordering_state = (a.parameter_tables == 0) ? a.ordering_state : 0;
  const int b_ordering_state = (b.parameter_tables == 0) ? b.ordering_state : 0;
  if (orderings.MoreOrderedThan(a_ordering_state, b_ordering_state)) {
    a_is_better = true;
  }
  if (orderings.MoreOrderedThan(b_ordering_state, a_ordering_state)) {
    b_is_better = true;
  }

  // Normally, two access paths for the same subplan should have the same
  // number of output rows. However, for parametrized paths, this need not
  // be the case; due to pushdown of sargable conditions into indexes;
  // some filters may be applied earlier, causing fewer rows to be
  // carried around temporarily (until the parametrization is resolved).
  // This can have an advantage in causing less work later even if it's
  // non-optimal now, e.g. by saving on filtering work, or having less work
  // done in other joins. Thus, we need to keep it around as an extra
  // cost dimension.
  if (a.num_output_rows < b.num_output_rows) {
    a_is_better = true;
  } else if (b.num_output_rows < a.num_output_rows) {
    b_is_better = true;
  }

  if (!a_is_better && !b_is_better) {
    return PathComparisonResult::IDENTICAL;
  } else if (a_is_better && !b_is_better) {
    return PathComparisonResult::FIRST_DOMINATES;
  } else if (!a_is_better && b_is_better) {
    return PathComparisonResult::SECOND_DOMINATES;
  } else {
    return PathComparisonResult::DIFFERENT_STRENGTHS;
  }
}

static string PrintCost(const AccessPath &path, const JoinHypergraph &graph,
                        const char *description_for_trace) {
  string str =
      StringPrintf("{cost=%.1f, init_cost=%.1f", path.cost, path.init_cost);
  if (path.init_once_cost != 0.0) {
    str += StringPrintf(", init_once_cost=%.1f", path.init_once_cost);
  }
  str += StringPrintf(", rows=%.1f", path.num_output_rows);

  // Print parameter tables, if any.
  if (path.parameter_tables != 0) {
    str += ", parm={";
    bool first = true;
    for (size_t node_idx : BitsSetIn(path.parameter_tables)) {
      if (!first) {
        str += ", ";
      }
      if ((uint64_t{1} << node_idx) == RAND_TABLE_BIT) {
        str += "<random>";
      } else {
        str += graph.nodes[node_idx].table->alias;
      }
      first = false;
    }
    str += "}";
  }

  if (path.ordering_state != 0) {
    str += StringPrintf(", order=%d", path.ordering_state);
  }

  if (strcmp(description_for_trace, "") == 0) {
    return str + "}";
  } else {
    return str + "} [" + description_for_trace + "]";
  }
}

void EstimateSortCost(AccessPath *path) {
  AccessPath *child = path->sort().child;
  const double num_rows = child->num_output_rows;
  double sort_cost;
  if (num_rows <= 1.0) {
    // Avoid NaNs from log2().
    sort_cost = kSortOneRowCost;
  } else {
    sort_cost = kSortOneRowCost * num_rows * std::max(log2(num_rows), 1.0);
  }

  path->num_output_rows = num_rows;
  path->cost = path->init_cost = child->cost + sort_cost;
  path->init_once_cost = 0.0;
  path->num_output_rows_before_filter = path->num_output_rows;
  path->cost_before_filter = path->cost;
}

/**
  Propose the given access path as an alternative to the existing access paths
  for the same task (assuming any exist at all), and hold a “tournament” to find
  whether it is better than the others. Only the best alternatives are kept,
  as defined by CompareAccessPaths(); a given access path is kept only if
  it is not dominated by any other path in the group (ie., the Pareto frontier
  is computed). This means that the following are all possible outcomes of the
  tournament:

   - The path is discarded, without ever being inserted in the list
     (dominated by at least one existing entry).
   - The path is inserted as a new alternative in the list (dominates none
     but it also not dominated by any -- or the list was empty), leaving it with
     N+1 entries.
   - The path is inserted as a new alternative in the list, but replaces one
     or more entries (dominates them).
   - The path replaces all existing alternatives, and becomes the sole entry
     in the list.

  “description_for_trace” is a short description of the inserted path
  to distinguish it in optimizer trace, if active. For instance, one might
  write “hash join” when proposing a hash join access path. It may be
  the empty string.
 */
void CostingReceiver::ProposeAccessPath(
    AccessPath *path, Prealloced_array<AccessPath *, 4> *existing_paths,
    const char *description_for_trace) const {
  if (m_secondary_engine_cost_hook != nullptr) {
    // If an error was raised by a previous invocation of the hook, reject all
    // paths.
    if (m_thd->is_error()) {
      return;
    }

    if (m_secondary_engine_cost_hook(m_thd, m_graph, path)) {
      // Rejected by the secondary engine.
      return;
    }
    assert(!m_thd->is_error());
    assert(path->init_cost <= path->cost);
    if (path->filter_predicates != 0) {
      assert(path->num_output_rows <= path->num_output_rows_before_filter);
      assert(path->cost_before_filter <= path->cost);
      assert(path->init_cost <= path->cost_before_filter);
    }
  }

  if (existing_paths->empty()) {
    if (m_trace != nullptr) {
      *m_trace += " - " + PrintCost(*path, m_graph, description_for_trace) +
                  " is first alternative, keeping\n";
    }
    AccessPath *insert_position = new (m_thd->mem_root) AccessPath(*path);
    existing_paths->push_back(insert_position);
    return;
  }

  AccessPath *insert_position = nullptr;
  int num_dominated = 0;
  for (size_t i = 0; i < existing_paths->size(); ++i) {
    PathComparisonResult result =
        CompareAccessPaths(*m_orderings, *path, *((*existing_paths)[i]));
    if (result == PathComparisonResult::DIFFERENT_STRENGTHS) {
      continue;
    }
    if (result == PathComparisonResult::IDENTICAL ||
        result == PathComparisonResult::SECOND_DOMINATES) {
      assert(insert_position == nullptr);
      if (m_trace != nullptr) {
        *m_trace += " - " + PrintCost(*path, m_graph, description_for_trace) +
                    " is not better than existing path " +
                    PrintCost(*(*existing_paths)[i], m_graph, "") +
                    ", discarding\n";
      }
      return;
    }
    if (result == PathComparisonResult::FIRST_DOMINATES) {
      ++num_dominated;
      if (insert_position == nullptr) {
        // Replace this path by the new, better one. We continue to search for
        // other paths to dominate. Note that we don't overwrite just yet,
        // because we might want to print out the old one in optimizer trace
        // below.
        insert_position = (*existing_paths)[i];
      } else {
        // The new path is better than the old one, but we don't need to insert
        // it again. Delete the old one by moving the last one into its place
        // (this may be a no-op) and then chopping one off the end.
        (*existing_paths)[i] = existing_paths->back();
        existing_paths->pop_back();
        --i;
      }
    }
  }

  if (insert_position == nullptr) {
    if (m_trace != nullptr) {
      *m_trace += " - " + PrintCost(*path, m_graph, description_for_trace) +
                  " is potential alternative, appending to existing list: (";
      bool first = true;
      for (const AccessPath *other_path : *existing_paths) {
        if (!first) {
          *m_trace += ", ";
        }
        *m_trace += PrintCost(*other_path, m_graph, "");
        first = false;
      }
      *m_trace += ")\n";
    }
    insert_position = new (m_thd->mem_root) AccessPath(*path);
    existing_paths->emplace_back(insert_position);
    return;
  }

  if (m_trace != nullptr) {
    if (existing_paths->size() == 1) {  // Only one left.
      if (num_dominated == 1) {
        *m_trace += " - " + PrintCost(*path, m_graph, description_for_trace) +
                    " is better than previous " +
                    PrintCost(*insert_position, m_graph, "") + ", replacing\n";
      } else {
        *m_trace +=
            " - " + PrintCost(*path, m_graph, description_for_trace) +
            " is better than all previous alternatives, replacing all\n";
      }
    } else {
      assert(num_dominated > 0);
      *m_trace += StringPrintf(
          " - %s is better than %d others, replacing them, remaining are: ",
          PrintCost(*path, m_graph, description_for_trace).c_str(),
          num_dominated);
      bool first = true;
      for (const AccessPath *other_path : *existing_paths) {
        if (other_path == insert_position) {
          // Will be replaced by ourselves momentarily, so don't print it.
          continue;
        }
        if (!first) {
          *m_trace += ", ";
        }
        *m_trace += PrintCost(*other_path, m_graph, "");
        first = false;
      }
      *m_trace += ")\n";
    }
  }
  *insert_position = *path;
  return;
}

void CostingReceiver::ProposeAccessPathWithOrderings(
    NodeMap nodes, FunctionalDependencySet fd_set, AccessPath *path,
    const char *description_for_trace) {
  // Insert an empty array if none exists.
  auto it_and_inserted = m_access_paths.emplace(
      nodes,
      AccessPathSet{Prealloced_array<AccessPath *, 4>{PSI_NOT_INSTRUMENTED},
                    fd_set});
  if (!it_and_inserted.second) {
    assert(fd_set ==
           it_and_inserted.first->second.active_functional_dependencies);
  }

  ProposeAccessPath(path, &it_and_inserted.first->second.paths,
                    description_for_trace);

  // Don't bother trying sort-ahead if we are done joining;
  // there's no longer anything to be ahead of, so the regular
  // sort operations will take care of it.
  if (nodes == TablesBetween(0, m_graph.nodes.size())) {
    return;
  }

  // Don't try to sort-ahead parametrized paths; see the comment in
  // CompareAccessPaths for why.
  if (path->parameter_tables != 0) {
    return;
  }

  // Try sort-ahead for all interesting orderings.
  // (For the final sort, this might not be so much _ahead_, but still
  // potentially useful, if there are multiple orderings where one is a
  // superset of the other.)
  bool path_is_on_heap = false;
  for (const SortAheadOrdering &sort_ahead_ordering : *m_sort_ahead_orderings) {
    if (!IsSubset(sort_ahead_ordering.required_nodes, nodes)) {
      continue;
    }

    LogicalOrderings::StateIndex new_state = m_orderings->ApplyFDs(
        m_orderings->SetOrder(sort_ahead_ordering.ordering_idx), fd_set);
    if (!m_orderings->MoreOrderedThan(new_state, path->ordering_state)) {
      continue;
    }

    if (!path_is_on_heap) {
      path = new (m_thd->mem_root) AccessPath(*path);
      path_is_on_heap = true;
    }

    AccessPath sort_path;
    sort_path.type = AccessPath::SORT;
    sort_path.ordering_state = new_state;
    sort_path.applied_sargable_join_predicates =
        path->applied_sargable_join_predicates &
        ~BitsBetween(0, m_graph.num_where_predicates);
    sort_path.delayed_predicates = path->delayed_predicates;
    sort_path.count_examined_rows = false;
    sort_path.sort().child = path;
    sort_path.sort().filesort = nullptr;
    sort_path.sort().tables_to_get_rowid_for = 0;
    sort_path.sort().order = sort_ahead_ordering.order;
    EstimateSortCost(&sort_path);

    char buf[256];
    if (m_trace != nullptr) {
      if (description_for_trace[0] == '\0') {
        snprintf(buf, sizeof(buf), "sort(%d)",
                 sort_ahead_ordering.ordering_idx);
      } else {
        snprintf(buf, sizeof(buf), "%s, sort(%d)", description_for_trace,
                 sort_ahead_ordering.ordering_idx);
      }
    }
    ProposeAccessPath(&sort_path, &it_and_inserted.first->second.paths, buf);
  }
}

/**
  Create a table array from a table bitmap.
 */
Mem_root_array<TABLE *> CollectTables(const Query_block *query_block,
                                      table_map tmap) {
  Mem_root_array<TABLE *> tables(query_block->join->thd->mem_root);
  for (TABLE_LIST *tl = query_block->leaf_tables; tl != nullptr;
       tl = tl->next_leaf) {
    if (tl->map() & tmap) {
      tables.push_back(tl->table);
    }
  }
  return tables;
}

bool CheckSupportedQuery(THD *thd, JOIN *join) {
  if (join->query_block->has_ft_funcs()) {
    my_error(ER_HYPERGRAPH_NOT_SUPPORTED_YET, MYF(0), "fulltext search");
    return true;
  }
  if (thd->lex->m_sql_cmd->using_secondary_storage_engine() &&
      SupportedAccessPathTypes(thd) == 0) {
    my_error(ER_HYPERGRAPH_NOT_SUPPORTED_YET, MYF(0),
             "the secondary engine in use");
    return true;
  }
  if (join->query_block->has_windows()) {
    my_error(ER_HYPERGRAPH_NOT_SUPPORTED_YET, MYF(0), "windowing functions");
    return true;
  }
  return false;
}

/**
  If we have both ORDER BY and GROUP BY, we need a materialization step
  after the grouping -- although in most cases, we only need to
  materialize one row at a time (streaming), so the performance loss
  should be very slight. This is because when filesort only really deals
  with fields, not values; when it is to “output” a row, it puts back the
  contents of the sorted table's (or tables') row buffer(s). For
  expressions that only depend on the current row, such as (f1 + 1),
  this is fine, but aggregate functions (Item_sum) depend on multiple
  rows, so we need a field where filesort can put back its value
  (and of course, subsequent readers need to read from that field
  instead of trying to evaluate the Item_sum). A temporary table provides
  just that, so we create one based on the current field list;
  StreamingIterator (or MaterializeIterator, if we actually need to
  materialize) will evaluate all the Items in turn and put their values
  into the temporary table's fields.

  For simplicity, we materialize all items in the SELECT list, even those
  that are not aggregate functions. This is a tiny performance loss,
  but makes things simpler.

  Note that we cannot set up an access path for this temporary table yet;
  that needs to wait until we know whether the sort decided to use row IDs
  or not, and Filesort cannot be set up until it knows what tables to sort.
  Thus, that job is deferred to CreateMaterializationPathForSortingAggregates().
 */
TABLE *CreateTemporaryTableForSortingAggregates(
    THD *thd, Query_block *query_block, Temp_table_param **temp_table_param) {
  TABLE *temp_table =
      CreateTemporaryTableFromSelectList(thd, query_block, temp_table_param);

  ReplaceOrderItemsWithTempTableFields(thd, query_block->order_list.first);

  return temp_table;
}

/**
  Replaces field references in an ON DUPLICATE KEY UPDATE clause with references
  to corresponding fields in a temporary table. The changes will be rolled back
  at the end of execution and will have to be redone during optimization in the
  next execution.
 */
void ReplaceUpdateValuesWithTempTableFields(
    Sql_cmd_insert_select *sql_cmd, Query_block *query_block,
    const mem_root_deque<Item *> &original_fields,
    const mem_root_deque<Item *> &temp_table_fields) {
  assert(CountVisibleFields(original_fields) ==
         CountVisibleFields(temp_table_fields));

  if (sql_cmd->update_value_list.empty()) return;

  auto tmp_field_it = VisibleFields(temp_table_fields).begin();
  for (Item *orig_field : VisibleFields(original_fields)) {
    Item *tmp_field = *tmp_field_it++;
    if (orig_field->type() == Item::FIELD_ITEM) {
      Item::Item_field_replacement replacement(
          down_cast<Item_field *>(orig_field)->field,
          down_cast<Item_field *>(tmp_field), query_block);
      for (Item *&orig_item : sql_cmd->update_value_list) {
        uchar *dummy;
        Item *new_item = orig_item->compile(
            &Item::visit_all_analyzer, &dummy, &Item::replace_item_field,
            pointer_cast<uchar *>(&replacement));
        if (new_item != orig_item) {
          query_block->join->thd->change_item_tree(&orig_item, new_item);
        }
      }
    }
  }
}

/**
  Creates a temporary table with columns matching the SELECT list of the given
  query block. The SELECT list of the query block is updated to point to the
  fields in the temporary table, and the same is done for the ON DUPLICATE KEY
  UPDATE clause of INSERT SELECT statements, if they have one.

  This function is used for materializing the query result, either as an
  intermediate step before sorting the final result if the sort requires the
  rows to come from a single table instead of a join, or as the last step if the
  SQL_BUFFER_RESULT query option has been specified.
 */
TABLE *CreateTemporaryTableFromSelectList(
    THD *thd, Query_block *query_block,
    Temp_table_param **temp_table_param_arg) {
  JOIN *join = query_block->join;

  Temp_table_param *temp_table_param = new (thd->mem_root) Temp_table_param;
  *temp_table_param_arg = temp_table_param;
  assert(!temp_table_param->precomputed_group_by);
  assert(!temp_table_param->skip_create_table);
  temp_table_param->hidden_field_count = CountHiddenFields(*join->fields);
  count_field_types(query_block, temp_table_param, *join->fields,
                    /*reset_with_sum_func=*/true, /*save_sum_fields=*/true);

  TABLE *temp_table = create_tmp_table(
      thd, temp_table_param, *join->fields,
      /*group=*/nullptr, /*distinct=*/false, /*save_sum_fields=*/true,
      query_block->active_options(), /*rows_limit=*/HA_POS_ERROR, "");
  temp_table->alias = "<temporary>";

  const mem_root_deque<Item *> *original_fields = join->fields;

  // Replace the SELECT list with items that read from our temporary table.
  ReplaceSelectListWithTempTableFields(thd, join, temp_table_param);

  // In case we have an INSERT SELECT statement with an ON DUPLICATE KEY UPDATE
  // clause, we also need to update the references in ODKU.
  if (thd->lex->sql_command == SQLCOM_INSERT_SELECT) {
    ReplaceUpdateValuesWithTempTableFields(
        down_cast<Sql_cmd_insert_select *>(thd->lex->m_sql_cmd), query_block,
        *original_fields, *join->fields);
  }

  // We made a new table, so make sure it gets properly cleaned up
  // at the end of execution.
  join->temp_tables.push_back(
      JOIN::TemporaryTableToCleanup{temp_table, temp_table_param});

  return temp_table;
}

/**
  Replaces the items in the SELECT list with items that point to fields in a
  temporary table.
 */
void ReplaceSelectListWithTempTableFields(THD *thd, JOIN *join,
                                          Temp_table_param *temp_table_param) {
  auto fields = new (thd->mem_root) mem_root_deque<Item *>(thd->mem_root);
  for (Item *item : *join->fields) {
    Field *field = item->get_tmp_table_field();
    Item *temp_table_item;
    if (field == nullptr) {
      assert(item->const_for_execution());
      temp_table_item = item;
    } else {
      temp_table_item = new Item_field(field);
      // Field items have already been turned into copy_fields entries in
      // create_tmp_table(), and most non-field items have been added to
      // items_to_copy in create_tmp_field(). Aggregate functions have not been
      // added, so add them here.
      if (item->type() == Item::SUM_FUNC_ITEM) {
        temp_table_param->items_to_copy->push_back(Func_ptr{item});
      }

      // Verify that all non-field items have been added to items_to_copy.
      assert(item->real_item()->type() == Item::FIELD_ITEM ||
             std::any_of(
                 temp_table_param->items_to_copy->begin(),
                 temp_table_param->items_to_copy->end(),
                 [item](const Func_ptr &ptr) { return ptr.func() == item; }));
    }
    temp_table_item->hidden = item->hidden;
    fields->push_back(temp_table_item);
  }
  join->fields = fields;
}

void ReplaceOrderItemsWithTempTableFields(THD *thd, ORDER *order) {
  // Change all items in the ORDER list to point to the temporary table.
  // This isn't important for streaming (the items would get the correct
  // value anyway -- although possibly with some extra calculations),
  // but it is for materialization.
  for (; order != nullptr; order = order->next) {
    Field *field = (*order->item)->get_tmp_table_field();
    if (field != nullptr) {
      Item_field *temp_field_item = new Item_field(field);

      // *order->item points into a memory area (the “base ref slice”)
      // where HAVING might expect to find items _not_ pointing into the
      // temporary table (if there is true materialization, it should run
      // before it to minimize the size of the sorted input), so in order to
      // not disturb it, we create a whole new place for the Item pointer
      // to live.
      //
      // TODO(sgunders): When we get rid of slices altogether,
      // we can skip this.
      thd->change_item_tree(pointer_cast<Item **>(&order->item),
                            pointer_cast<Item *>(new (thd->mem_root) Item *));
      thd->change_item_tree(order->item, temp_field_item);
    }
  }
}

/**
  Set up an access path for streaming or materializing between grouping
  and sorting. See CreateTemporaryTableForSortingAggregates() for details.
 */
AccessPath *CreateMaterializationPathForSortingAggregates(
    THD *thd, JOIN *join, AccessPath *path, TABLE *temp_table,
    Temp_table_param *temp_table_param, bool using_addon_fields) {
  if (using_addon_fields) {
    // The common case; we can use streaming.
    AccessPath *stream_path =
        NewStreamingAccessPath(thd, path, join, temp_table_param, temp_table,
                               /*ref_slice=*/-1);
    stream_path->num_output_rows = path->num_output_rows;
    stream_path->cost = path->cost;
    stream_path->init_cost = path->init_cost;
    stream_path->init_once_cost =
        0.0;  // Never recoverable across query blocks.
    stream_path->num_output_rows_before_filter = stream_path->num_output_rows;
    stream_path->cost_before_filter = stream_path->cost;
    stream_path->ordering_state = path->ordering_state;
    return stream_path;
  } else {
    // Filesort needs sort by row ID, possibly because large blobs are
    // involved, so we need to actually materialize. (If we wanted a
    // smaller temporary table at the expense of more seeks, we could
    // materialize only aggregate functions and do a multi-table sort
    // by docid, but this situation is rare, so we go for simplicity.)
    return CreateMaterializationPath(thd, join, path, temp_table,
                                     temp_table_param);
  }
}

/**
  Sets up an access path for materializing the results returned from a path in a
  temporary table.
 */
AccessPath *CreateMaterializationPath(THD *thd, JOIN *join, AccessPath *path,
                                      TABLE *temp_table,
                                      Temp_table_param *temp_table_param) {
  AccessPath *table_path =
      NewTableScanAccessPath(thd, temp_table, /*count_examined_rows=*/false);
  AccessPath *materialize_path = NewMaterializeAccessPath(
      thd,
      SingleMaterializeQueryBlock(thd, path, /*select_number=*/-1, join,
                                  /*copy_fields_and_items=*/true,
                                  temp_table_param),
      /*invalidators=*/nullptr, temp_table, table_path, /*cte=*/nullptr,
      /*unit=*/nullptr, /*ref_slice=*/-1, /*rematerialize=*/true,
      /*limit_rows=*/HA_POS_ERROR, /*reject_multiple_rows=*/false);

  EstimateMaterializeCost(materialize_path);
  materialize_path->ordering_state = path->ordering_state;
  return materialize_path;
}

bool IsMaterializationPath(const AccessPath *path) {
  switch (path->type) {
    case AccessPath::MATERIALIZE:
    case AccessPath::MATERIALIZED_TABLE_FUNCTION:
    case AccessPath::MATERIALIZE_INFORMATION_SCHEMA_TABLE:
      return true;
    default:
      return false;
  }
}

// Estimate the width of each row produced by “query_block”,
// for temporary table materialization.
//
// See EstimateRowWidth() in make_join_hypergraph.cc.
size_t EstimateRowWidth(const Query_block &query_block) {
  size_t ret = 0;
  for (const Item *item : query_block.fields) {
    ret += min<size_t>(item->max_length, 4096);
  }
  return ret;
}

}  // namespace

FilterCost EstimateFilterCost(THD *thd, double num_rows, Item *condition,
                              Query_block *outer_query_block) {
  FilterCost cost{0.0, 0.0, 0.0};
  cost.cost_if_not_materialized = num_rows * kApplyOneFilterCost;
  cost.cost_if_materialized = num_rows * kApplyOneFilterCost;
  WalkItem(condition, enum_walk::POSTFIX,
           [thd, num_rows, outer_query_block, &cost](Item *item) {
             if (!IsItemInSubSelect(item)) {
               return false;
             }
             Item_in_subselect *item_subs =
                 down_cast<Item_in_subselect *>(item);

             // TODO(sgunders): Respect subquery hints, which can force the
             // strategy to be materialize.
             Query_block *query_block = item_subs->unit->first_query_block();
             const bool materializeable =
                 item_subs->subquery_allows_materialization(
                     thd, query_block, outer_query_block) &&
                 query_block->subquery_strategy(thd) ==
                     Subquery_strategy::CANDIDATE_FOR_IN2EXISTS_OR_MAT;

             AccessPath *path = item_subs->unit->root_access_path();
             if (path == nullptr) {
               // In rare situations involving IN subqueries on the left side of
               // other IN subqueries, the query block may not be part of the
               // parent query block's list of inner query blocks. If so, it has
               // not been optimized here. Since this is a rare case, we'll just
               // skip it and assign it zero cost.
               return false;
             }

             cost.cost_if_not_materialized += num_rows * path->cost;
             if (materializeable) {
               // We can't ask the handler for costs at this stage, since that
               // requires an actual TABLE, and we don't want to be creating
               // them every time we're evaluating a cost. Thus, instead,
               // we ask the cost model for an estimate. Longer-term, these two
               // estimates should really be guaranteed to be the same somehow.
               Cost_model_server::enum_tmptable_type tmp_table_type;
               if (EstimateRowWidth(*query_block) * num_rows <
                   thd->variables.max_heap_table_size) {
                 tmp_table_type = Cost_model_server::MEMORY_TMPTABLE;
               } else {
                 tmp_table_type = Cost_model_server::DISK_TMPTABLE;
               }
               cost.cost_if_materialized +=
                   thd->cost_model()->tmptable_readwrite_cost(
                       tmp_table_type, /*write_rows=*/0,
                       /*read_rows=*/num_rows);
               cost.cost_to_materialize +=
                   path->cost + kMaterializeOneRowCost * path->num_output_rows;
             } else {
               cost.cost_if_materialized += num_rows * path->cost;
             }
             return false;
           });
  return cost;
}

// Very rudimentary (assuming no deduplication; it's better to overestimate
// than to understimate), so that we get something that isn't “unknown”.
void EstimateMaterializeCost(AccessPath *path) {
  AccessPath *table_path = path->materialize().table_path;

  path->cost = 0;
  path->num_output_rows = 0;
  double cost_for_cacheable = 0.0;
  for (const MaterializePathParameters::QueryBlock &block :
       path->materialize().param->query_blocks) {
    if (block.subquery_path->num_output_rows >= 0.0) {
      path->num_output_rows += block.subquery_path->num_output_rows;
      path->cost += block.subquery_path->cost;
      if (block.join != nullptr && block.join->query_block->is_cacheable()) {
        cost_for_cacheable += block.subquery_path->cost;
      }
    }
  }
  path->cost += kMaterializeOneRowCost * path->num_output_rows;

  // Try to get usable estimates. Ignored by InnoDB, but used by
  // TempTable.
  if (table_path->type == AccessPath::TABLE_SCAN) {
    TABLE *temp_table = table_path->table_scan().table;
    temp_table->file->stats.records = path->num_output_rows;

    table_path->num_output_rows = path->num_output_rows;
    table_path->init_cost = table_path->init_once_cost = 0.0;
    table_path->cost = temp_table->file->table_scan_cost().total_cost();
  }

  path->init_cost = path->cost + std::max(table_path->init_cost, 0.0);
  path->init_once_cost = cost_for_cacheable;
  path->cost = path->cost + std::max(table_path->cost, 0.0);
}

void EstimateAggregateCost(AccessPath *path) {
  AccessPath *child = path->aggregate().child;

  // TODO(sgunders): How do we estimate how many rows aggregation
  // will be reducing the output by?
  path->num_output_rows = child->num_output_rows;
  path->init_cost = child->init_cost;
  path->init_once_cost = child->init_once_cost;
  path->cost = child->cost + kAggregateOneRowCost * child->num_output_rows;
  path->num_output_rows_before_filter = path->num_output_rows;
  path->cost_before_filter = path->cost;
  path->ordering_state = child->ordering_state;
}

JoinHypergraph::Node *FindNodeWithTable(JoinHypergraph *graph, TABLE *table) {
  for (JoinHypergraph::Node &node : graph->nodes) {
    if (node.table == table) {
      return &node;
    }
  }
  return nullptr;
}

Prealloced_array<AccessPath *, 4> ApplyDistinctAndOrder(
    THD *thd, const CostingReceiver &receiver,
    const LogicalOrderings &orderings, int order_by_ordering_idx,
    Query_block *query_block, Prealloced_array<AccessPath *, 4> root_candidates,
    string *trace) {
  JOIN *join = query_block->join;
  assert(join->select_distinct || query_block->is_ordered());

  if (root_candidates.empty()) {
    // Nothing to do if the secondary engine has rejected all candidates.
    assert(receiver.HasSecondaryEngineCostHook());
    return root_candidates;
  }

  TABLE *temp_table = nullptr;
  Temp_table_param *temp_table_param = nullptr;

  Mem_root_array<TABLE *> tables = CollectTables(
      query_block,
      GetUsedTables(root_candidates[0]));  // Should be same for all paths.

  // If we have grouping followed by a sort, we need to bounce via
  // the buffers of a temporary table. See the comments on
  // CreateTemporaryTableForSortingAggregates().
  if (query_block->is_grouped()) {
    temp_table = CreateTemporaryTableForSortingAggregates(thd, query_block,
                                                          &temp_table_param);
    // Filesort now only needs to worry about one input -- this temporary
    // table. This holds whether we are actually materializing or just
    // using streaming.
    tables.clear();
    tables.push_back(temp_table);
  }

  // Set up the filesort objects. They have some interactions
  // around addon fields vs. sort by row ID, so we need to do this
  // before we decide on iterators.
  bool need_rowid = false;
  Filesort *filesort = nullptr;
  if (query_block->is_ordered()) {
    Mem_root_array<TABLE *> table_copy(thd->mem_root, tables.begin(),
                                       tables.end());
    filesort = new (thd->mem_root)
        Filesort(thd, std::move(table_copy), /*keep_buffers=*/false,
                 query_block->order_list.first,
                 /*limit_arg=*/HA_POS_ERROR, /*force_stable_sort=*/false,
                 /*remove_duplicates=*/false, /*force_sort_positions=*/false,
                 /*unwrap_rollup=*/false);
    join->filesorts_to_cleanup.push_back(filesort);
    need_rowid = !filesort->using_addon_fields();
  }
  Filesort *filesort_for_distinct = nullptr;
  bool order_by_subsumed_by_distinct = false;
  if (join->select_distinct) {
    bool force_sort_positions = false;
    if (filesort != nullptr && !filesort->using_addon_fields()) {
      // We have the rather unusual situation here that we have two sorts
      // directly after each other, with no temporary table in-between,
      // and filesort expects to be able to refer to rows by their position.
      // Usually, the sort for DISTINCT would be a superset of the sort for
      // ORDER BY, but not always (e.g. when sorting by some expression),
      // so we could end up in a situation where the first sort is by addon
      // fields and the second one is by positions.
      //
      // Thus, in this case, we force the first sort to be by positions,
      // so that the result comes from SortFileIndirectIterator or
      // SortBufferIndirectIterator. These will both position the cursor
      // on the underlying temporary table correctly before returning it,
      // so that the successive filesort will save the right position
      // for the row.
      if (trace != nullptr) {
        *trace +=
            "Forcing DISTINCT to sort row IDs, since ORDER BY sort does\n";
      }
      force_sort_positions = true;
    }

    // If there's an ORDER BY on the query, it needs to be heeded in the
    // re-sort for DISTINCT.
    ORDER *desired_order =
        query_block->is_ordered() ? query_block->order_list.first : nullptr;

    ORDER *order = create_order_from_distinct(
        thd, Ref_item_array(), desired_order, join->fields,
        /*skip_aggregates=*/false, /*convert_bit_fields_to_long=*/false,
        &order_by_subsumed_by_distinct);

    if (order != nullptr) {
      filesort_for_distinct = new (thd->mem_root)
          Filesort(thd, std::move(tables),
                   /*keep_buffers=*/false, order, HA_POS_ERROR,
                   /*force_stable_sort=*/false,
                   /*remove_duplicates=*/true, force_sort_positions,
                   /*unwrap_rollup=*/false);
      join->filesorts_to_cleanup.push_back(filesort_for_distinct);
      if (!filesort_for_distinct->using_addon_fields()) {
        need_rowid = true;
      }
    }
  }

  // Apply streaming between grouping and us, if needed (see above).
  // NOTE: If we elide the sort due to interesting orderings, this might
  // be redundant. It is fairly harmless, though.
  if (temp_table != nullptr) {
    Prealloced_array<AccessPath *, 4> new_root_candidates(PSI_NOT_INSTRUMENTED);
    for (AccessPath *root_path : root_candidates) {
      root_path = CreateMaterializationPathForSortingAggregates(
          thd, join, root_path, temp_table, temp_table_param,
          /*using_addon_fields=*/!need_rowid);
      receiver.ProposeAccessPath(root_path, &new_root_candidates, "");
    }
    root_candidates = std::move(new_root_candidates);
  }

  // Now create iterators for DISTINCT, if applicable.
  if (join->select_distinct) {
    if (trace != nullptr) {
      *trace += "Applying sort for DISTINCT\n";
    }

    Prealloced_array<AccessPath *, 4> new_root_candidates(PSI_NOT_INSTRUMENTED);
    for (AccessPath *root_path : root_candidates) {
      if (filesort_for_distinct == nullptr) {
        // Only const fields.
        AccessPath *limit_path = NewLimitOffsetAccessPath(
            thd, root_path, /*limit=*/1, /*offset=*/0, join->calc_found_rows,
            /*reject_multiple_rows=*/false,
            /*send_records_override=*/nullptr);
        receiver.ProposeAccessPath(limit_path, &new_root_candidates, "");
      } else {
        AccessPath *sort_path =
            NewSortAccessPath(thd, root_path, filesort_for_distinct,
                              /*count_examined_rows=*/false);
        EstimateSortCost(sort_path);

        if (!filesort_for_distinct->using_addon_fields()) {
          FindTablesToGetRowidFor(sort_path);
        }
        receiver.ProposeAccessPath(sort_path, &new_root_candidates, "");
      }
    }
    root_candidates = std::move(new_root_candidates);
  }

  // Apply ORDER BY, if applicable.
  if (query_block->is_ordered() && order_by_subsumed_by_distinct) {
    // The ordering for DISTINCT already gave us the right sort order,
    // so no need to sort again.
    //
    // TODO(sgunders): If there are elements in desired_order that are not
    // in fields_list, consider whether it would be cheaper to add them on
    // the end to avoid the second sort, even though it would make the
    // first one more expensive. See e.g. main.distinct for a case.
    if (trace != nullptr) {
      *trace += "ORDER BY subsumed by sort for DISTINCT, ignoring\n";
    }
  } else if (query_block->is_ordered() && !order_by_subsumed_by_distinct) {
    if (trace != nullptr) {
      *trace += "Applying sort for ORDER BY\n";
    }

    Prealloced_array<AccessPath *, 4> new_root_candidates(PSI_NOT_INSTRUMENTED);
    for (AccessPath *root_path : root_candidates) {
      if (order_by_ordering_idx != -1 &&
          orderings.DoesFollowOrder(root_path->ordering_state,
                                    order_by_ordering_idx)) {
        receiver.ProposeAccessPath(root_path, &new_root_candidates,
                                   "sort elided");
      } else {
        AccessPath *sort_path =
            NewSortAccessPath(thd, root_path, filesort,
                              /*count_examined_rows=*/false);
        EstimateSortCost(sort_path);

        if (!filesort->using_addon_fields()) {
          FindTablesToGetRowidFor(sort_path);
        }
        receiver.ProposeAccessPath(sort_path, &new_root_candidates, "");
      }
    }
    root_candidates = std::move(new_root_candidates);
  }
  return root_candidates;
}

/**
  Find out whether “item” is a sargable condition; if so, add it to:

   - The list of sargable predicate for the tables (hypergraph nodes)
     the condition touches. For a regular condition, this will typically
     be one table; for a join condition, it will typically be two.
     If “force_table” is non-nullptr, only that table will be considered
     (this is used for join conditions, to ensure that we do not push
     down predicates that cannot, e.g. to the outer side of left joins).

   - The graph's global list of predicates, if it is not already present
     (predicate_index = -1). This will never happen for WHERE conditions,
     only for join conditions.
 */
static void PossiblyAddSargableCondition(THD *thd, Item *item,
                                         TABLE *force_table,
                                         int predicate_index,
                                         bool is_join_condition,
                                         JoinHypergraph *graph, string *trace) {
  if (item->type() != Item::FUNC_ITEM ||
      down_cast<Item_func *>(item)->functype() != Item_bool_func2::EQ_FUNC) {
    return;
  }
  Item_func_eq *eq_item = down_cast<Item_func_eq *>(item);
  if (eq_item->get_comparator()->get_child_comparator_count() >= 2) {
    return;
  }
  for (unsigned arg_idx = 0; arg_idx < 2; ++arg_idx) {
    Item *left = eq_item->arguments()[arg_idx];
    Item *right = eq_item->arguments()[1 - arg_idx];
    if (left->type() != Item::FIELD_ITEM) {
      continue;
    }
    Field *field = down_cast<Item_field *>(left)->field;
    if (force_table != nullptr && force_table != field->table) {
      continue;
    }
    if (field->part_of_key.is_clear_all()) {
      // Not part of any key, so not sargable. (It could be part of a prefix
      // keys, though, but we include them for now.)
      continue;
    }
    JoinHypergraph::Node *node = FindNodeWithTable(graph, field->table);
    if (node == nullptr) {
      // A field in a different query block, so not sargable for us.
      continue;
    }

    if (trace != nullptr) {
      if (is_join_condition) {
        *trace += "Found sargable join condition " + ItemToString(item) +
                  " on " + node->table->alias + "\n";
      } else {
        *trace += "Found sargable condition " + ItemToString(item) + "\n";
      }
    }

    if (predicate_index == -1) {
      // This predicate is not already registered as a predicate
      // (which means in practice that it's a join predicate,
      // not a WHERE predicate), so add it so that we can refer
      // to it in bitmaps.
      Predicate p;
      p.condition = eq_item;
      p.selectivity = EstimateSelectivity(thd, eq_item, trace);
      p.total_eligibility_set =
          ~0;  // Should never be applied as a WHERE predicate.
      p.functional_dependencies_idx.init(thd->mem_root);
      graph->predicates.push_back(std::move(p));
      predicate_index = graph->predicates.size() - 1;
      graph->sargable_join_predicates.emplace(eq_item, predicate_index);
    }

    node->sargable_predicates.push_back({predicate_index, field, right});
  }
}

/**
  Helper for CollectFunctionalDependenciesFromPredicates(); also used for
  non-equijoin predicates in CollectFunctionalDependenciesFromJoins().
 */
static int AddFunctionalDependencyFromCondition(THD *thd, Item *condition,
                                                LogicalOrderings *orderings) {
  if (condition->type() != Item::FUNC_ITEM) {
    return -1;
  }

  // We treat IS NULL as item = const.
  if (down_cast<Item_func *>(condition)->functype() == Item_func::ISNULL_FUNC) {
    Item_func_isnull *isnull = down_cast<Item_func_isnull *>(condition);

    FunctionalDependency fd;
    fd.type = FunctionalDependency::FD;
    fd.head = Bounds_checked_array<ItemHandle>();
    fd.tail = orderings->GetHandle(isnull->arguments()[0]);

    return orderings->AddFunctionalDependency(thd, fd);
  }

  if (down_cast<Item_func *>(condition)->functype() != Item_func::EQ_FUNC) {
    // We only deal with equalities.
    return -1;
  }
  Item_func_eq *eq = down_cast<Item_func_eq *>(condition);
  Item *left = eq->arguments()[0];
  Item *right = eq->arguments()[1];
  if (left->const_for_execution()) {
    if (right->const_for_execution()) {
      // Ignore const = const.
      return -1;
    }
    swap(left, right);
  }
  if (equality_determines_uniqueness(eq, left, right)) {
    // item = const.
    FunctionalDependency fd;
    fd.type = FunctionalDependency::FD;
    fd.head = Bounds_checked_array<ItemHandle>();
    fd.tail = orderings->GetHandle(left);

    return orderings->AddFunctionalDependency(thd, fd);
  } else {
    // item = item.
    FunctionalDependency fd;
    fd.type = FunctionalDependency::EQUIVALENCE;
    ItemHandle head = orderings->GetHandle(left);
    fd.head = Bounds_checked_array<ItemHandle>(&head, 1);
    fd.tail = orderings->GetHandle(right);

    // Takes a copy if needed, so the stack reference is safe.
    return orderings->AddFunctionalDependency(thd, fd);
  }
}

/**
  Collect functional dependencies from joins. Currently, we apply
  item = item only, and only on inner joins and semijoins. Outer joins do not
  enforce their equivalences unconditionally (e.g. with an outer join on
  t1.a = t2.b, t1.a = t2.b does not hold afterwards; t2.b could be NULL).
  Semijoins do, and even though the attributes from the inner side are
  inaccessible afterwards, there could still be interesting constant FDs
  that are applicable to the outer side after equivalences.

  It is possible to generate a weaker form of FDs for outer joins,
  as described in sql/aggregate_check.h (and done for GROUP BY);
  e.g. from the join condition t1.x=t2.x AND t1.y=t2.y, one can infer a
  functional dependency {t1.x,t1.y} → t2.x and similar for t2.y.
  However, do note the comment about FD propagation in the calling function.
 */
static void CollectFunctionalDependenciesFromJoins(
    THD *thd, JoinHypergraph *graph, LogicalOrderings *orderings) {
  for (JoinPredicate &pred : graph->edges) {
    const RelationalExpression *expr = pred.expr;
    if (expr->type != RelationalExpression::INNER_JOIN &&
        expr->type != RelationalExpression::STRAIGHT_INNER_JOIN &&
        expr->type != RelationalExpression::SEMIJOIN) {
      continue;
    }
    pred.functional_dependencies_idx.init(thd->mem_root);
    pred.functional_dependencies_idx.reserve(expr->equijoin_conditions.size() +
                                             expr->join_conditions.size());
    for (Item_func_eq *join_condition : expr->equijoin_conditions) {
      int fd_idx =
          AddFunctionalDependencyFromCondition(thd, join_condition, orderings);
      if (fd_idx != -1) {
        pred.functional_dependencies_idx.push_back(fd_idx);
      }
    }
    for (Item *join_condition : expr->join_conditions) {
      int fd_idx =
          AddFunctionalDependencyFromCondition(thd, join_condition, orderings);
      if (fd_idx != -1) {
        pred.functional_dependencies_idx.push_back(fd_idx);
      }
    }
  }
}

/**
  Collect functional dependencies from non-join predicates.
  Again, we only do item = item, and more interesting; we only take the
  raw items, where we could have been much more sophisticated.
  Imagine a predicate like a = b + c; we will add a FD saying exactly
  that (which may or may not be useful, if b + c shows up in ORDER BY),
  but we should probably also have added {b,c} → a, if b and c could
  be generated somehow.

  However, we _do_ special-case item = const, since they are so useful;
  they become {} → item instead.
 */
static void CollectFunctionalDependenciesFromPredicates(
    THD *thd, JoinHypergraph *graph, LogicalOrderings *orderings) {
  for (Predicate &pred : graph->predicates) {
    int fd_idx =
        AddFunctionalDependencyFromCondition(thd, pred.condition, orderings);
    if (fd_idx != -1) {
      pred.functional_dependencies_idx.push_back(fd_idx);
    }
  }
}

static void CollectFunctionalDependenciesFromUniqueIndexes(
    THD *thd, JoinHypergraph *graph, LogicalOrderings *orderings) {
  // Collect functional dependencies from unique indexes.
  for (JoinHypergraph::Node &node : graph->nodes) {
    TABLE *table = node.table;
    for (unsigned key_idx = 0; key_idx < table->s->keys; ++key_idx) {
      KEY *key = &table->key_info[key_idx];
      if (!Overlaps(actual_key_flags(key), HA_NOSAME)) {
        // Not a unique index.
        continue;
      }
      if (Overlaps(actual_key_flags(key), HA_NULL_PART_KEY)) {
        // Some part of the index could be NULL,
        // with special semantics; so ignore it.
        continue;
      }

      FunctionalDependency fd;
      fd.type = FunctionalDependency::FD;
      fd.head = Bounds_checked_array<ItemHandle>::Alloc(thd->mem_root,
                                                        actual_key_parts(key));
      for (unsigned keypart_idx = 0; keypart_idx < actual_key_parts(key);
           ++keypart_idx) {
        fd.head[keypart_idx] = orderings->GetHandle(
            new Item_field(key->key_part[keypart_idx].field));
      }
      fd.always_active = true;

      // Add a FD for each field in the table that is not part of the key.
      for (unsigned field_idx = 0; field_idx < table->s->fields; ++field_idx) {
        Field *field = table->field[field_idx];
        bool in_key = false;
        for (unsigned keypart_idx = 0; keypart_idx < actual_key_parts(key);
             ++keypart_idx) {
          if (field->eq(key->key_part[keypart_idx].field)) {
            in_key = true;
            break;
          }
        }
        if (!in_key) {
          fd.tail = orderings->GetHandle(new Item_field(field));
          orderings->AddFunctionalDependency(thd, fd);
        }
      }
    }
  }
}

static Ordering CollectInterestingOrder(THD *thd,
                                        const SQL_I_List<ORDER> &order_list,
                                        LogicalOrderings *orderings,
                                        table_map *used_tables) {
  Ordering ordering = Ordering::Alloc(thd->mem_root, order_list.size());
  int i = 0;
  *used_tables = 0;
  for (ORDER *order = order_list.first; order != nullptr;
       order = order->next, ++i) {
    Item *item = *order->item;
    ordering[i].item = orderings->GetHandle(item);
    ordering[i].direction = order->direction;
    *used_tables |= item->used_tables();
  }
  return ordering;
}

// Build an ORDER * that we can give to Filesort. It is only suitable for
// sort-ahead, since it assumes no temporary tables have been inserted.
static ORDER *BuildSortAheadOrdering(THD *thd,
                                     const LogicalOrderings *orderings,
                                     Ordering ordering) {
  ORDER *order = nullptr;
  ORDER *last_order = nullptr;
  for (OrderElement element : ordering) {
    ORDER *new_ptr = new (thd->mem_root) ORDER;
    new_ptr->item_initial = orderings->item(element.item);
    new_ptr->item = &new_ptr->item_initial;
    new_ptr->direction = element.direction;

    if (order == nullptr) {
      order = new_ptr;
    }
    if (last_order != nullptr) {
      last_order->next = new_ptr;
    }
    last_order = new_ptr;
  }
  return order;
}

/**
  Build all structures we need for keeping track of interesting orders.
  We collect the actual relevant orderings (e.g. from ORDER BY) and any
  functional dependencies we can find, then ask LogicalOrderings to create
  its state machine. The result is said state machine and a list of
  potential sort-ahead orderings.
 */
static void BuildInterestingOrders(
    THD *thd, JoinHypergraph *graph, Query_block *query_block,
    LogicalOrderings *orderings,
    Mem_root_array<SortAheadOrdering> *sort_ahead_orderings,
    int *order_by_ordering_idx, string *trace) {
  // Collect ordering from ORDER BY.
  if (query_block->is_ordered()) {
    table_map used_tables;
    Ordering ordering = CollectInterestingOrder(thd, query_block->order_list,
                                                orderings, &used_tables);

    NodeMap required_nodes = GetNodeMapFromTableMap(
        used_tables & ~PSEUDO_TABLE_BITS, graph->table_num_to_node_num);
    if (Overlaps(used_tables, RAND_TABLE_BIT)) {
      // We can never sort-ahead with nondeterministic functions.
    } else {
      *order_by_ordering_idx = orderings->AddOrdering(thd, ordering);

      // We cannot reuse query_block->order_list, because if a temporary table
      // is inserted, its fields may be rewritten to match that table, which is
      // the wrong thing for sort-ahead.
      ORDER *order = BuildSortAheadOrdering(thd, orderings, ordering);
      sort_ahead_orderings->push_back(
          SortAheadOrdering{*order_by_ordering_idx, required_nodes, order});
    }
  }

  // Early exit if we don't have any orderings.
  if (orderings->num_orderings() <= 1) {
    if (trace != nullptr) {
      *trace +=
          "\nNo interesting orders found. Not collecting functional "
          "dependencies.\n\n";
    }
    orderings->Build(thd, trace);
    return;
  }

  // Collect functional dependencies. Currently, there are many kinds
  // we don't do; see sql/aggregate_check.h. In particular, we don't
  // collect FDs from:
  //
  //  - Deterministic functions ({x} → f(x) for relevant items f(x)).
  //  - Unique indexes that are nullable, but that are made non-nullable
  //    by WHERE predicates.
  //  - Generated columns. [*]
  //  - Join conditions from outer joins. [*]
  //  - Non-merged derived tables (including views and CTEs). [*]
  //
  // Note that the points marked with [*] introduce special problems related
  // to propagation of FDs; aggregate_check.h contains more details around
  // so-called “NULL-friendly functional dependencies”. If we include any
  // of them, we need to take more care about propagating them through joins.
  //
  // We liberally insert FDs here, even if they are not obviously related
  // to interesting orders; they may be useful at a later stage, when
  // other FDs can use them as a stepping stone. Optimization in Build()
  // will remove them if they are indeed not useful.
  CollectFunctionalDependenciesFromJoins(thd, graph, orderings);
  CollectFunctionalDependenciesFromPredicates(thd, graph, orderings);
  CollectFunctionalDependenciesFromUniqueIndexes(thd, graph, orderings);

  orderings->Build(thd, trace);

  for (JoinPredicate &pred : graph->edges) {
    for (int fd_idx : pred.functional_dependencies_idx) {
      pred.functional_dependencies |= orderings->GetFDSet(fd_idx);
    }
  }
  for (Predicate &pred : graph->predicates) {
    for (int fd_idx : pred.functional_dependencies_idx) {
      pred.functional_dependencies |= orderings->GetFDSet(fd_idx);
    }
  }

  // After Build(), there may be more interesting orders that we can try
  // as sort-ahead; in particular homogenized orderings. (The ones we already
  // added will not have moved around, as per the contract.) Scan for them,
  // create orders that filesort can use, and add them to the list.
  for (int ordering_idx = sort_ahead_orderings->size();
       ordering_idx < orderings->num_orderings(); ++ordering_idx) {
    table_map used_tables = 0;
    for (OrderElement element : orderings->ordering(ordering_idx)) {
      used_tables |= orderings->item(element.item)->used_tables();
    }
    NodeMap required_nodes = GetNodeMapFromTableMap(
        used_tables & ~PSEUDO_TABLE_BITS, graph->table_num_to_node_num);

    ORDER *order = BuildSortAheadOrdering(thd, orderings,
                                          orderings->ordering(ordering_idx));
    sort_ahead_orderings->push_back(
        SortAheadOrdering{ordering_idx, required_nodes, order});
  }
}

AccessPath *FindBestQueryPlan(THD *thd, Query_block *query_block,
                              string *trace) {
  JOIN *join = query_block->join;
  if (CheckSupportedQuery(thd, join)) return nullptr;

  // In the case of rollup (only): After the base slice list was made, we may
  // have modified the field list to add rollup group items and sum switchers.
  // The resolver also takes care to update these in query_block->order_list.
  // However, even though the hypergraph join optimizer doesn't use slices,
  // setup_order() later modifies order->item to point into the base slice,
  // where the rollup group items are _not_ updated. Thus, we need to refresh
  // the base slice before we do anything.
  //
  // It would be better to have rollup resolving update the base slice directly,
  // but this would break HAVING in the old join optimizer (see the other call
  // to refresh_base_slice(), in JOIN::make_tmp_tables_info()).
  if (join->rollup_state != JOIN::RollupState::NONE) {
    join->refresh_base_slice();
  }

  // NOTE: Normally, we'd expect join->temp_tables and
  // join->filesorts_to_cleanup to be empty, but since we can get called twice
  // for materialized subqueries, there may already be data there that we must
  // keep.

  // Convert the join structures into a hypergraph.
  JoinHypergraph graph(thd->mem_root, query_block);
  if (MakeJoinHypergraph(thd, trace, &graph)) {
    return nullptr;
  }

  // Find sargable predicates, ie., those that we can push down into indexes.
  // See add_key_field().
  //
  // TODO(sgunders): Include x=y OR NULL predicates, <=> and IS NULL predicates,
  // and the special case of COLLATION accepted in add_key_field().
  //
  // TODO(sgunders): Integrate with the range optimizer, or find some other way
  // of accepting <, >, <= and >= predicates.
  if (trace != nullptr) {
    *trace += "\n";
  }
  for (unsigned i = 0; i < graph.num_where_predicates; ++i) {
    if (IsSingleBitSet(graph.predicates[i].total_eligibility_set)) {
      PossiblyAddSargableCondition(thd, graph.predicates[i].condition,
                                   /*force_table=*/nullptr, i,
                                   /*is_join_condition=*/false, &graph, trace);
    }
  }
  for (JoinHypergraph::Node &node : graph.nodes) {
    for (Item *cond : node.join_conditions_pushable_to_this) {
      const auto it = graph.sargable_join_predicates.find(cond);
      int predicate_index =
          (it == graph.sargable_join_predicates.end()) ? -1 : it->second;
      PossiblyAddSargableCondition(thd, cond, node.table, predicate_index,
                                   /*is_join_condition=*/true, &graph, trace);
    }
  }

  // Figure out if any later sort will need row IDs.
  bool need_rowid = false;
  if (query_block->is_explicitly_grouped() || query_block->is_ordered() ||
      join->select_distinct) {
    for (TABLE_LIST *tl = query_block->leaf_tables; tl != nullptr;
         tl = tl->next_leaf) {
      if (SortWillBeOnRowId(tl->table)) {
        need_rowid = true;
        break;
      }
    }
  }

  // Find out which predicates contain subqueries.
  graph.materializable_predicates = 0;
  for (unsigned i = 0; i < graph.predicates.size(); ++i) {
    if (ContainsSubqueries(graph.predicates[i].condition)) {
      graph.materializable_predicates |= uint64_t{1} << i;
    }
  }

  // Collect interesting orders; for now, this means ORDER BY only
  // (later: GROUP BY).
  LogicalOrderings orderings(thd);
  Mem_root_array<SortAheadOrdering> sort_ahead_orderings(thd->mem_root);
  int order_by_ordering_idx = -1;
  BuildInterestingOrders(thd, &graph, query_block, &orderings,
                         &sort_ahead_orderings, &order_by_ordering_idx, trace);

  // Run the actual join optimizer algorithm. This creates an access path
  // for the join as a whole (with lowest possible cost, and thus also
  // hopefully optimal execution time), with all pushable predicates applied.
  if (trace != nullptr) {
    *trace += "\nEnumerating subplans:\n";
  }
  for (const JoinHypergraph::Node &node : graph.nodes) {
    node.table->init_cost_model(thd->cost_model());
  }
  const secondary_engine_modify_access_path_cost_t secondary_engine_cost_hook =
      SecondaryEngineCostHook(thd);
  CostingReceiver receiver(
      thd, query_block, graph, &orderings, &sort_ahead_orderings, need_rowid,
      SupportedAccessPathTypes(thd), secondary_engine_cost_hook, trace);
  if (EnumerateAllConnectedPartitions(graph.graph, &receiver) &&
      !thd->is_error()) {
    my_error(ER_HYPERGRAPH_NOT_SUPPORTED_YET, MYF(0), "large join graphs");
    return nullptr;
  }
  if (thd->is_error()) return nullptr;

  // Get the root candidates. If there is a secondary engine cost hook, there
  // may be no candidates, as the hook may have rejected so many access paths
  // that we could not build a complete plan. Otherwise, expect at least one
  // candidate.
  if (secondary_engine_cost_hook != nullptr &&
      (!receiver.HasSeen(TablesBetween(0, graph.nodes.size())) ||
       receiver.root_candidates().empty())) {
    my_error(ER_SECONDARY_ENGINE, MYF(0),
             "All plans were rejected by the secondary storage engine.");
    return nullptr;
  }
  Prealloced_array<AccessPath *, 4> root_candidates =
      receiver.root_candidates();
  assert(!root_candidates.empty());
  thd->m_current_query_partial_plans += receiver.num_access_paths();
  if (trace != nullptr) {
    *trace += StringPrintf(
        "\nEnumerated %zu subplans, got %zu candidate(s) to finalize:\n",
        receiver.num_access_paths(), root_candidates.size());
  }

  // Now we have one or more access paths representing joining all the tables
  // together. (There may be multiple ones because they can be better at
  // different metrics.) We apply the post-join operations to all of them in
  // turn, and then finally pick out the one with the lowest total cost,
  // because at the end, other metrics don't really matter any more.
  //
  // We could have stopped caring about e.g. init_cost after LIMIT
  // has been applied (after which it no longer matters), so that we'd get
  // fewer candidates in each step, but this part is so cheap that it's
  // unlikely to be worth it. We go through ProposeAccessPath() mainly
  // because it gives us better tracing.
  if (trace != nullptr) {
    *trace += "Adding final predicates\n";
  }
  {
    Prealloced_array<AccessPath *, 4> new_root_candidates(PSI_NOT_INSTRUMENTED);
    for (const AccessPath *root_path : root_candidates) {
      for (bool materialize_subqueries : {false, true}) {
        AccessPath path = *root_path;
        double init_once_cost = 0.0;

        // Apply any predicates that don't belong to any
        // specific table, or which are nondeterministic.
        for (size_t i = 0; i < graph.num_where_predicates; ++i) {
          if (!Overlaps(graph.predicates[i].total_eligibility_set,
                        TablesBetween(0, graph.nodes.size())) ||
              Overlaps(graph.predicates[i].total_eligibility_set,
                       RAND_TABLE_BIT)) {
            path.filter_predicates |= uint64_t{1} << i;
            FilterCost cost =
                EstimateFilterCost(thd, root_path->num_output_rows,
                                   graph.predicates[i].condition, query_block);
            if (materialize_subqueries) {
              path.cost += cost.cost_if_materialized;
              init_once_cost += cost.cost_to_materialize;
            } else {
              path.cost += cost.cost_if_not_materialized;
            }
            path.num_output_rows *= graph.predicates[i].selectivity;
          }
        }

        const bool contains_subqueries =
            Overlaps(path.filter_predicates, graph.materializable_predicates);

        // Now that we have decided on a full plan, expand all
        // the applied filter maps into proper FILTER nodes
        // for execution. This is a no-op in the second
        // iteration.
        ExpandFilterAccessPaths(thd, &path, join, graph.predicates,
                                graph.num_where_predicates);

        if (materialize_subqueries) {
          assert(path.type == AccessPath::FILTER);
          path.filter().materialize_subqueries = true;
          path.cost += init_once_cost;  // Will be subtracted
                                        // back for rescans.
          path.init_cost += init_once_cost;
          path.init_once_cost += init_once_cost;
        }

        receiver.ProposeAccessPath(&path, &new_root_candidates,
                                   materialize_subqueries ? "mat. subq" : "");

        if (!contains_subqueries) {
          // Nothing to try to materialize.
          break;
        }
      }
    }
    root_candidates = std::move(new_root_candidates);
  }

  // Apply GROUP BY, if applicable. We currently always do this by sorting
  // first and then using streaming aggregation.
  if (query_block->is_grouped()) {
    if (join->make_sum_func_list(*join->fields, /*before_group_by=*/true))
      return nullptr;

    if (trace != nullptr) {
      *trace += "Applying aggregation for GROUP BY\n";
    }

    Prealloced_array<AccessPath *, 4> new_root_candidates(PSI_NOT_INSTRUMENTED);
    for (AccessPath *root_path : root_candidates) {
      if (query_block->is_explicitly_grouped()) {
        Mem_root_array<TABLE *> tables =
            CollectTables(query_block, GetUsedTables(root_path));
        Filesort *filesort = new (thd->mem_root) Filesort(
            thd, std::move(tables), /*keep_buffers=*/false,
            query_block->group_list.first,
            /*limit_arg=*/HA_POS_ERROR, /*force_stable_sort=*/false,
            /*remove_duplicates=*/false, /*force_sort_positions=*/false,
            /*unwrap_rollup=*/true);
        join->filesorts_to_cleanup.push_back(filesort);
        AccessPath *sort_path =
            NewSortAccessPath(thd, root_path, filesort,
                              /*count_examined_rows=*/false);
        EstimateSortCost(sort_path);

        root_path = sort_path;

        if (!filesort->using_addon_fields()) {
          FindTablesToGetRowidFor(sort_path);
        }
      }

      // TODO(sgunders): We don't need to allocate this on the MEM_ROOT.
      const bool rollup = (join->rollup_state != JOIN::RollupState::NONE);
      AccessPath *aggregate_path =
          NewAggregateAccessPath(thd, root_path, rollup);
      EstimateAggregateCost(aggregate_path);

      receiver.ProposeAccessPath(aggregate_path, &new_root_candidates, "");
    }
    root_candidates = std::move(new_root_candidates);

    Item_sum **func_ptr = join->sum_funcs;
    Item_sum *func;
    bool need_distinct = true;  // We don't support loose index scan yet.
    while ((func = *(func_ptr++))) {
      Aggregator::Aggregator_type type =
          need_distinct && func->has_with_distinct()
              ? Aggregator::DISTINCT_AGGREGATOR
              : Aggregator::SIMPLE_AGGREGATOR;
      if (func->set_aggregator(type) || func->aggregator_setup(thd)) {
        return nullptr;
      }
    }
    if (make_group_fields(join, join)) {
      return nullptr;
    }
  }

  // Apply HAVING, if applicable.
  if (join->having_cond != nullptr) {
    if (trace != nullptr) {
      *trace += "Applying filter for HAVING\n";
    }

    Prealloced_array<AccessPath *, 4> new_root_candidates(PSI_NOT_INSTRUMENTED);
    for (AccessPath *root_path : root_candidates) {
      AccessPath filter_path;
      filter_path.type = AccessPath::FILTER;
      filter_path.filter().child = root_path;
      filter_path.filter().condition = join->having_cond;
      // We don't currently bother with materializing subqueries
      // in HAVING, as they should be rare.
      filter_path.filter().materialize_subqueries = false;
      filter_path.num_output_rows =
          root_path->num_output_rows *
          EstimateSelectivity(thd, join->having_cond, trace);
      filter_path.init_cost = root_path->init_cost;
      filter_path.init_once_cost = root_path->init_once_cost;
      filter_path.cost =
          root_path->cost + EstimateFilterCost(thd, root_path->num_output_rows,
                                               join->having_cond, query_block)
                                .cost_if_not_materialized;
      filter_path.num_output_rows_before_filter = filter_path.num_output_rows;
      filter_path.cost_before_filter = filter_path.cost;
      receiver.ProposeAccessPath(&filter_path, &new_root_candidates, "");
    }
    root_candidates = std::move(new_root_candidates);
  }

  if (join->select_distinct || query_block->is_ordered()) {
    root_candidates =
        ApplyDistinctAndOrder(thd, receiver, orderings, order_by_ordering_idx,
                              query_block, std::move(root_candidates), trace);
  }

  // Apply LIMIT, if applicable.
  Query_expression *query_expression = join->query_expression();
  if (query_expression->select_limit_cnt != HA_POS_ERROR ||
      query_expression->offset_limit_cnt != 0) {
    if (trace != nullptr) {
      *trace += "Applying LIMIT\n";
    }
    Prealloced_array<AccessPath *, 4> new_root_candidates(PSI_NOT_INSTRUMENTED);
    for (AccessPath *root_path : root_candidates) {
      AccessPath *limit_path = NewLimitOffsetAccessPath(
          thd, root_path, query_expression->select_limit_cnt,
          query_expression->offset_limit_cnt, join->calc_found_rows,
          /*reject_multiple_rows=*/false,
          /*send_records_override=*/nullptr);
      receiver.ProposeAccessPath(limit_path, &new_root_candidates, "");
    }
    root_candidates = std::move(new_root_candidates);
  }

  if (thd->is_error()) return nullptr;

  if (root_candidates.empty()) {
    // The secondary engine has rejected so many of the post-processing paths
    // (e.g., sorting, limit, grouping) that we could not build a complete plan.
    assert(secondary_engine_cost_hook != nullptr);
    my_error(ER_SECONDARY_ENGINE, MYF(0),
             "All plans were rejected by the secondary storage engine.");
    return nullptr;
  }

  // TODO(sgunders): If we are part of e.g. a derived table and are streamed,
  // we might want to keep multiple root paths around for future use, e.g.,
  // if there is a LIMIT higher up.
  AccessPath *root_path =
      *std::min_element(root_candidates.begin(), root_candidates.end(),
                        [](const AccessPath *a, const AccessPath *b) {
                          return a->cost < b->cost;
                        });

  // Materialize the result if a top-level query block has the SQL_BUFFER_RESULT
  // option, and the chosen root path isn't already a materialization path.
  if (query_block->active_options() & OPTION_BUFFER_RESULT &&
      query_block->outer_query_block() == nullptr &&
      !IsMaterializationPath(root_path)) {
    if (trace != nullptr) {
      *trace += "Adding temporary table for SQL_BUFFER_RESULT.\n";
    }

    Temp_table_param *temp_table_param = nullptr;
    TABLE *temp_table =
        CreateTemporaryTableFromSelectList(thd, query_block, &temp_table_param);
    root_path = CreateMaterializationPath(thd, join, root_path, temp_table,
                                          temp_table_param);
  }

  if (trace != nullptr) {
    *trace += StringPrintf("Final cost is %.1f.\n", root_path->cost);
  }

#ifndef NDEBUG
  WalkAccessPaths(root_path, join, WalkAccessPathPolicy::ENTIRE_QUERY_BLOCK,
                  [&](const AccessPath *path, const JOIN *) {
                    assert(path->cost >= path->init_cost);
                    assert(path->init_cost >= path->init_once_cost);
                    return false;
                  });
#endif

  // Create deferred Filesort objects.
  WalkAccessPaths(root_path, join, WalkAccessPathPolicy::ENTIRE_QUERY_BLOCK,
                  [thd, query_block](AccessPath *path, const JOIN *) {
                    if (path->type == AccessPath::SORT &&
                        path->sort().filesort == nullptr) {
                      Mem_root_array<TABLE *> tables =
                          CollectTables(query_block, GetUsedTables(path));
                      path->sort().filesort = new (thd->mem_root)
                          Filesort(thd, std::move(tables),
                                   /*keep_buffers=*/false, path->sort().order,
                                   /*limit_arg=*/HA_POS_ERROR,
                                   /*force_stable_sort=*/false,
                                   /*remove_duplicates=*/false,
                                   /*force_sort_positions=*/false,
                                   /*unwrap_rollup=*/true);
                      query_block->join->filesorts_to_cleanup.push_back(
                          path->sort().filesort);
                      if (!path->sort().filesort->using_addon_fields()) {
                        FindTablesToGetRowidFor(path);
                      }
                    }
                    return false;
                  });

  join->best_rowcount = lrint(root_path->num_output_rows);
  join->best_read = root_path->cost;

  // 0 or 1 rows has a special meaning; it means a _guarantee_ we have no more
  // than one (so-called “const tables”). Make sure we don't give that
  // guarantee unless we have a LIMIT.
  if (join->best_rowcount <= 1 &&
      query_expression->select_limit_cnt - query_expression->offset_limit_cnt >
          1) {
    join->best_rowcount = PLACEHOLDER_TABLE_ROW_ESTIMATE;
  }

  return root_path;
}
