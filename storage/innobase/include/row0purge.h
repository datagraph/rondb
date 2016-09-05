/*****************************************************************************

Copyright (c) 1997, 2016, Oracle and/or its affiliates. All Rights Reserved.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA

*****************************************************************************/

/**************************************************//**
@file include/row0purge.h
Purge obsolete records

Created 3/14/1997 Heikki Tuuri
*******************************************************/

#ifndef row0purge_h
#define row0purge_h

#include "univ.i"
#include "data0data.h"
#include "btr0types.h"
#include "btr0pcur.h"
#include "dict0types.h"
#include "trx0types.h"
#include "que0types.h"
#include "row0types.h"
#include "ut0vec.h"

/** Create a purge node to a query graph.
@param[in]	parent	parent node, i.e., a thr node
@param[in]	heap	memory heap where created
@return own: purge node */
purge_node_t*
row_purge_node_create(
	que_thr_t*	parent,
	mem_heap_t*	heap)
	MY_ATTRIBUTE((warn_unused_result));

/***********************************************************//**
Determines if it is possible to remove a secondary index entry.
Removal is possible if the secondary index entry does not refer to any
not delete marked version of a clustered index record where DB_TRX_ID
is newer than the purge view.

NOTE: This function should only be called by the purge thread, only
while holding a latch on the leaf page of the secondary index entry
(or keeping the buffer pool watch on the page).  It is possible that
this function first returns true and then false, if a user transaction
inserts a record that the secondary index entry would refer to.
However, in that case, the user transaction would also re-insert the
secondary index entry after purge has removed it and released the leaf
page latch.
@return true if the secondary index record can be purged */
bool
row_purge_poss_sec(
/*===============*/
	purge_node_t*	node,	/*!< in/out: row purge node */
	dict_index_t*	index,	/*!< in: secondary index */
	const dtuple_t*	entry)	/*!< in: secondary index entry */
	MY_ATTRIBUTE((warn_unused_result));
/***************************************************************
Does the purge operation for a single undo log record. This is a high-level
function used in an SQL execution graph.
@return query thread to run next or NULL */
que_thr_t*
row_purge_step(
/*===========*/
	que_thr_t*	thr)	/*!< in: query thread */
	MY_ATTRIBUTE((warn_unused_result));

/* Purge node structure */

struct purge_node_t {

	/** Info required to purge a record */
	struct rec_t {

		/** Record to purge */
		trx_undo_rec_t*	undo_rec;

		/** File pointer to UNDO record */
		roll_ptr_t	roll_ptr;
	};

	using Recs = std::vector<rec_t, mem_heap_allocator<rec_t>>;

	/** node type: QUE_NODE_PURGE */
	que_common_t		common;

	/* Local storage for this graph node */

	/** roll pointer to undo log record */
	roll_ptr_t		roll_ptr;

	/** undo number of the record */
	undo_no_t		undo_no;

	/** undo log record type: TRX_UNDO_INSERT_REC, ... */
	ulint			rec_type;

	/** table where purge is done */
	dict_table_t*		table;

	/** MDL ticket for the table name */
        MDL_ticket*             mdl;

        /** MySQL table instance, or NULL if !table->has_index_on_virtual() */
        TABLE*                  mysql_table;

	/** compiler analysis info of an update */
	ulint			cmpl_info;

	/** update vector for a clustered index record */
	upd_t*			update;

	/** NULL, or row reference to the next row to handle */
	dtuple_t*		ref;

	/** NULL, or a copy (also fields copied to heap) of the indexed
	fields of the row to handle */
	dtuple_t*		row;

	/** NULL, or the next index whose record should be handled */
	dict_index_t*		index;

	/** The heap is owned by purge_sys and is reset after a purge
	batch has completed. */
	mem_heap_t*		heap;

	/** true if the clustered index record determined by ref was
	found in the clustered index, and we were able to position pcur on it */
	bool			found_clust;

	/** persistent cursor used in searching the clustered index record */
	btr_pcur_t		pcur;

	/** Debug flag */
	bool			done;

	/** trx id for this purging record */
	trx_id_t		trx_id;

	/** Undo recs to purge */
	Recs*			recs;

#ifdef UNIV_DEBUG
	/***********************************************************//**
	Validate the persisent cursor. The purge node has two references
	to the clustered index record - one via the ref member, and the
	other via the persistent cursor.  These two references must match
	each other if the found_clust flag is set.
	@return true if the persistent cursor is consistent with
	the ref member.*/
	bool	validate_pcur();
#endif
};

#include "row0purge.ic"

#endif
