/* Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA */

#ifndef RPL_RLI_H
#define RPL_RLI_H

#include "sql_priv.h"
#include "rpl_info.h"
#include "rpl_utility.h"
#include "rpl_tblmap.h"
#include "rpl_reporting.h"
#include "rpl_utility.h"
#include "log.h"                         /* LOG_INFO */
#include "binlog.h"                      /* MYSQL_BIN_LOG */
#include "sql_class.h"                   /* THD */

struct RPL_TABLE_LIST;
class Master_info;
extern uint sql_slave_skip_counter;

/*******************************************************************************
Replication SQL Thread

Relay_log_info contains:
  - the current relay log
  - the current relay log offset
  - master log name
  - master log sequence corresponding to the last update
  - misc information specific to the SQL thread

Relay_log_info is initialized from a repository, e.g. table or file, if there is
one. Otherwise, data members are intialized with defaults by calling
init_relay_log_info(). Currently, only files are available as repositories.
*******************************************************************************/
class Relay_log_info : public Rpl_info
{
public:
  /**
     Flags for the state of the replication.
   */
  enum enum_state_flag {
    /** The replication thread is inside a statement */
    IN_STMT,

    /** Flag counter.  Should always be last */
    STATE_FLAGS_COUNT
  };

  /*
    The SQL thread owns one Relay_log_info, and each client that has
    executed a BINLOG statement owns one Relay_log_info. This function
    returns zero for the Relay_log_info object that belongs to the SQL
    thread and nonzero for Relay_log_info objects that belong to
    clients.
  */
  inline bool belongs_to_client()
  {
    DBUG_ASSERT(info_thd);
    return !info_thd->slave_thread;
  }

  /*
    If true, events with the same server id should be replicated. This
    field is set on creation of a relay log info structure by copying
    the value of ::replicate_same_server_id and can be overridden if
    necessary. For example of when this is done, check sql_binlog.cc,
    where the BINLOG statement can be used to execute "raw" events.
   */
  bool replicate_same_server_id;

  /*** The following variables can only be read when protect by data lock ****/
  /*
    cur_log_fd - file descriptor of the current read  relay log
  */
  File cur_log_fd;
  /*
    Protected with internal locks.
    Must get data_lock when resetting the logs.
  */
  MYSQL_BIN_LOG relay_log;
  LOG_INFO linfo;

  /*
   cur_log
     Pointer that either points at relay_log.get_log_file() or
     &rli->cache_buf, depending on whether the log is hot or there was
     the need to open a cold relay_log.

   cache_buf 
     IO_CACHE used when opening cold relay logs.
   */
  IO_CACHE cache_buf,*cur_log;

  /*
    Identifies when the recovery process is going on.
    See sql/slave.cc:init_recovery for further details.
  */
  bool is_relay_log_recovery;

  /* The following variables are safe to read any time */

  /*
    When we restart slave thread we need to have access to the previously
    created temporary tables. Modified only on init/end and by the SQL
    thread, read only by SQL thread.
  */
  TABLE *save_temporary_tables;

  /* parent Master_info structure */
  Master_info *mi;

  /*
    Needed to deal properly with cur_log getting closed and re-opened with
    a different log under our feet
  */
  uint32 cur_log_old_open_count;

  /*
    Let's call a group (of events) :
      - a transaction
      or
      - an autocommiting query + its associated events (INSERT_ID,
    TIMESTAMP...)
    We need these rli coordinates :
    - relay log name and position of the beginning of the group we currently are
    executing. Needed to know where we have to restart when replication has
    stopped in the middle of a group (which has been rolled back by the slave).
    - relay log name and position just after the event we have just
    executed. This event is part of the current group.
    Formerly we only had the immediately above coordinates, plus a 'pending'
    variable, but this dealt wrong with the case of a transaction starting on a
    relay log and finishing (commiting) on another relay log. Case which can
    happen when, for example, the relay log gets rotated because of
    max_binlog_size.
  */
protected:
  char group_relay_log_name[FN_REFLEN];
  ulonglong group_relay_log_pos;
  char event_relay_log_name[FN_REFLEN];
  ulonglong event_relay_log_pos;
  ulonglong future_event_relay_log_pos;

  /*
     Original log name and position of the group we're currently executing
     (whose coordinates are group_relay_log_name/pos in the relay log)
     in the master's binlog. These concern the *group*, because in the master's
     binlog the log_pos that comes with each event is the position of the
     beginning of the group.

    Note: group_master_log_name, group_master_log_pos must only be
    written from the thread owning the Relay_log_info (SQL thread if
    !belongs_to_client(); client thread executing BINLOG statement if
    belongs_to_client()).
  */
  char group_master_log_name[FN_REFLEN];
  volatile my_off_t group_master_log_pos;

  /*
    When it commits, InnoDB internally stores the master log position it has
    processed so far; the position to store is the one of the end of the
    committing event (the COMMIT query event, or the event if in autocommit
    mode).
  */
#if MYSQL_VERSION_ID < 40100
  ulonglong future_master_log_pos;
#else
  ulonglong future_group_master_log_pos;
#endif

public:
  int init_relay_log_pos(const char* log,
                         ulonglong pos, bool need_data_lock,
                         const char** errmsg,
                         bool look_for_description_event);

  /*
    Handling of the relay_log_space_limit optional constraint.
    ignore_log_space_limit is used to resolve a deadlock between I/O and SQL
    threads, the SQL thread sets it to unblock the I/O thread and make it
    temporarily forget about the constraint.
  */
  ulonglong log_space_limit,log_space_total;
  bool ignore_log_space_limit;

  time_t last_master_timestamp;

  void clear_until_condition();

  /**
    Reset the delay.
    This is used by RESET SLAVE to clear the delay.
  */
  void clear_sql_delay()
  {
    sql_delay= 0;
  }

  /*
    Needed for problems when slave stops and we want to restart it
    skipping one or more events in the master log that have caused
    errors, and have been manually applied by DBA already.
  */
  volatile uint32 slave_skip_counter;
  volatile ulong abort_pos_wait;	/* Incremented on change master */
  mysql_mutex_t log_space_lock;
  mysql_cond_t log_space_cond;

  /*
     Condition and its parameters from START SLAVE UNTIL clause.
     
     UNTIL condition is tested with is_until_satisfied() method that is
     called by exec_relay_log_event(). is_until_satisfied() caches the result
     of the comparison of log names because log names don't change very often;
     this cache is invalidated by parts of code which change log names with
     notify_*_log_name_updated() methods. (They need to be called only if SQL
     thread is running).
   */
  enum {UNTIL_NONE= 0, UNTIL_MASTER_POS, UNTIL_RELAY_POS} until_condition;
  char until_log_name[FN_REFLEN];
  ulonglong until_log_pos;
  /* extension extracted from log_name and converted to int */
  ulong until_log_name_extension;   
  /* 
     Cached result of comparison of until_log_name and current log name
     -2 means unitialised, -1,0,1 are comarison results 
  */
  enum 
  { 
    UNTIL_LOG_NAMES_CMP_UNKNOWN= -2, UNTIL_LOG_NAMES_CMP_LESS= -1,
    UNTIL_LOG_NAMES_CMP_EQUAL= 0, UNTIL_LOG_NAMES_CMP_GREATER= 1
  } until_log_names_cmp_result;

  char cached_charset[6];
  /*
    trans_retries varies between 0 to slave_transaction_retries and counts how
    many times the slave has retried the present transaction; gets reset to 0
    when the transaction finally succeeds. retried_trans is a cumulative
    counter: how many times the slave has retried a transaction (any) since
    slave started.
  */
  ulong trans_retries, retried_trans;

  /*
    If the end of the hot relay log is made of master's events ignored by the
    slave I/O thread, these two keep track of the coords (in the master's
    binlog) of the last of these events seen by the slave I/O thread. If not,
    ign_master_log_name_end[0] == 0.
    As they are like a Rotate event read/written from/to the relay log, they
    are both protected by rli->relay_log.LOCK_log.
  */
  char ign_master_log_name_end[FN_REFLEN];
  ulonglong ign_master_log_pos_end;

  /* 
    Indentifies where the SQL Thread should create temporary files for the
    LOAD DATA INFILE. This is used for security reasons.
   */ 
  char slave_patternload_file[FN_REFLEN]; 
  size_t slave_patternload_file_size;  

  Relay_log_info(bool is_slave_recovery,
                 PSI_mutex_key *param_key_info_run_lock,
                 PSI_mutex_key *param_key_info_data_lock,
                 PSI_mutex_key *param_key_info_data_cond,
                 PSI_mutex_key *param_key_info_start_cond,
                 PSI_mutex_key *param_key_info_stop_cond);
  virtual ~Relay_log_info();

  /**
    Invalidates cached until_log_name and group_relay_log_name comparison
    result. Should be called after any update of group_realy_log_name if
    there chances that sql_thread is running.
  */
  inline void notify_group_relay_log_name_update()
  {
    if (until_condition==UNTIL_RELAY_POS)
      until_log_names_cmp_result= UNTIL_LOG_NAMES_CMP_UNKNOWN;
  }

  /**
    The same as @c notify_group_relay_log_name_update but for
    @c group_master_log_name.
  */
  inline void notify_group_master_log_name_update()
  {
    if (until_condition==UNTIL_MASTER_POS)
      until_log_names_cmp_result= UNTIL_LOG_NAMES_CMP_UNKNOWN;
  }
  
  inline void inc_event_relay_log_pos()
  {
    event_relay_log_pos= future_event_relay_log_pos;
  }

  void inc_group_relay_log_pos(ulonglong log_pos,
			       bool skip_lock= FALSE);

  int wait_for_pos(THD* thd, String* log_name, longlong log_pos, 
		   longlong timeout);
  void close_temporary_tables();

  /* Check if UNTIL condition is satisfied. See slave.cc for more. */
  bool is_until_satisfied(THD *thd, Log_event *ev);
  inline ulonglong until_pos()
  {
    return ((until_condition == UNTIL_MASTER_POS) ? group_master_log_pos :
	    group_relay_log_pos);
  }

  RPL_TABLE_LIST *tables_to_lock;           /* RBR: Tables to lock  */
  uint tables_to_lock_count;        /* RBR: Count of tables to lock */
  table_mapping m_table_map;      /* RBR: Mapping table-id to table */
  /* RBR: Record Rows_query log event */
  Rows_query_log_event* rows_query_ev;

  bool get_table_data(TABLE *table_arg, table_def **tabledef_var, TABLE **conv_table_var) const
  {
    DBUG_ASSERT(tabledef_var && conv_table_var);
    for (TABLE_LIST *ptr= tables_to_lock ; ptr != NULL ; ptr= ptr->next_global)
      if (ptr->table == table_arg)
      {
        *tabledef_var= &static_cast<RPL_TABLE_LIST*>(ptr)->m_tabledef;
        *conv_table_var= static_cast<RPL_TABLE_LIST*>(ptr)->m_conv_table;
        DBUG_PRINT("debug", ("Fetching table data for table %s.%s:"
                             " tabledef: %p, conv_table: %p",
                             table_arg->s->db.str, table_arg->s->table_name.str,
                             *tabledef_var, *conv_table_var));
        return true;
      }
    return false;
  }

  /**
    Last charset (6 bytes) seen by slave SQL thread is cached here; it helps
    the thread save 3 @c get_charset() per @c Query_log_event if the charset is not
    changing from event to event (common situation).
    When the 6 bytes are equal to 0 is used to mean "cache is invalidated".
  */
  void cached_charset_invalidate();
  bool cached_charset_compare(char *charset) const;

  void cleanup_context(THD *, bool);
  void slave_close_thread_tables(THD *);
  void clear_tables_to_lock();
  int purge_relay_logs(THD *thd, bool just_reset, const char** errmsg);

  /*
    Used to defer stopping the SQL thread to give it a chance
    to finish up the current group of events.
    The timestamp is set and reset in @c sql_slave_killed().
  */
  time_t last_event_start_time;

  /**
    Helper function to do after statement completion.

    This function is called from an event to complete the group by
    either stepping the group position, if the "statement" is not
    inside a transaction; or increase the event position, if the
    "statement" is inside a transaction.

    @param event_log_pos
    Master log position of the event. The position is recorded in the
    relay log info and used to produce information for <code>SHOW
    SLAVE STATUS</code>.
  */
  void stmt_done(my_off_t event_log_pos);


  /**
     Set the value of a replication state flag.

     @param flag Flag to set
   */
  void set_flag(enum_state_flag flag)
  {
    m_flags |= (1UL << flag);
  }

  /**
     Get the value of a replication state flag.

     @param flag Flag to get value of

     @return @c true if the flag was set, @c false otherwise.
   */
  bool get_flag(enum_state_flag flag)
  {
    return m_flags & (1UL << flag);
  }

  /**
     Clear the value of a replication state flag.

     @param flag Flag to clear
   */
  void clear_flag(enum_state_flag flag)
  {
    m_flags &= ~(1UL << flag);
  }

  /**
     Is the replication inside a group?

     Replication is inside a group if either:
     - The OPTION_BEGIN flag is set, meaning we're inside a transaction
     - The RLI_IN_STMT flag is set, meaning we're inside a statement

     @retval true Replication thread is currently inside a group
     @retval false Replication thread is currently not inside a group
   */
  bool is_in_group() const {
    return (info_thd->variables.option_bits & OPTION_BEGIN) ||
      (m_flags & (1UL << IN_STMT));
  }

  int count_relay_log_space();

  int init_info();
  void end_info();
  int flush_info(bool force= FALSE);
  int flush_current_log();
  void set_master_info(Master_info *info);

  inline ulonglong get_future_event_relay_log_pos() { return future_event_relay_log_pos; }
  inline void set_future_event_relay_log_pos(ulonglong log_pos)
  {
    future_event_relay_log_pos= log_pos;
  }

  inline const char* get_group_master_log_name() { return group_master_log_name; }
  inline ulonglong get_group_master_log_pos() { return group_master_log_pos; }
  inline void set_group_master_log_name(const char *log_file_name)
  {
     strmake(group_master_log_name,log_file_name, sizeof(group_master_log_name)-1);
  }
  inline void set_group_master_log_pos(ulonglong log_pos)
  {
    group_master_log_pos= log_pos;
  }

  inline const char* get_group_relay_log_name() { return group_relay_log_name; }
  inline ulonglong get_group_relay_log_pos() { return group_relay_log_pos; }
  inline void set_group_relay_log_name(const char *log_file_name)
  {
     strmake(group_relay_log_name,log_file_name, sizeof(group_relay_log_name)-1);
  }
  inline void set_group_relay_log_name(const char *log_file_name, size_t len)
  {
     strmake(group_relay_log_name, log_file_name, len);
  }
  inline void set_group_relay_log_pos(ulonglong log_pos)
  {
    group_relay_log_pos= log_pos;
  }

  inline const char* get_event_relay_log_name() { return event_relay_log_name; }
  inline ulonglong get_event_relay_log_pos() { return event_relay_log_pos; }
  inline void set_event_relay_log_name(const char *log_file_name)
  {
     strmake(event_relay_log_name,log_file_name, sizeof(event_relay_log_name)-1);
  }
  inline void set_event_relay_log_name(const char *log_file_name, size_t len)
  {
     strmake(event_relay_log_name,log_file_name, len);
  }
  inline void set_event_relay_log_pos(ulonglong log_pos)
  {
    event_relay_log_pos= log_pos;
  }
  inline const char* get_rpl_log_name()
  {
    return (group_master_log_name[0] ? group_master_log_name : "FIRST");
  }

#if MYSQL_VERSION_ID < 40100
  inline ulonglong get_future_master_log_pos() { return future_master_log_pos; }
#else
  inline ulonglong get_future_group_master_log_pos() { return future_group_master_log_pos; }
  inline void set_future_group_master_log_pos(ulonglong log_pos)
  {
    future_group_master_log_pos= log_pos;
  }
#endif

  size_t get_number_info_rli_fields();

  /**
    Text used in THD::proc_info when the slave SQL thread is delaying.
  */
  static const char *const state_delaying_string;

  /**
    Indicate that a delay starts.

    This does not actually sleep; it only sets the state of this
    Relay_log_info object to delaying so that the correct state can be
    reported by SHOW SLAVE STATUS and SHOW PROCESSLIST.

    Requires rli->data_lock.

    @param delay_end The time when the delay shall end.
  */
  void start_sql_delay(time_t delay_end)
  {
    mysql_mutex_assert_owner(&data_lock);
    sql_delay_end= delay_end;
    thd_proc_info(info_thd, state_delaying_string);
  }

  int32 get_sql_delay() { return sql_delay; }
  void set_sql_delay(time_t _sql_delay) { sql_delay= _sql_delay; }
  time_t get_sql_delay_end() { return sql_delay_end; }

private:

  /**
    Delay slave SQL thread by this amount, compared to master (in
    seconds). This is set with CHANGE MASTER TO MASTER_DELAY=X.

    Guarded by data_lock.  Initialized by the client thread executing
    START SLAVE.  Written by client threads executing CHANGE MASTER TO
    MASTER_DELAY=X.  Read by SQL thread and by client threads
    executing SHOW SLAVE STATUS.  Note: must not be written while the
    slave SQL thread is running, since the SQL thread reads it without
    a lock when executing flush_info().
  */
  int sql_delay;

  /**
    During a delay, specifies the point in time when the delay ends.

    This is used for the SQL_Remaining_Delay column in SHOW SLAVE STATUS.

    Guarded by data_lock. Written by the sql thread.  Read by client
    threads executing SHOW SLAVE STATUS.
  */
  time_t sql_delay_end;

  /*
    Before the MASTER_DELAY parameter was added (WL#344), relay_log.info
    had 4 lines. Now it has 5 lines.
  */
  static const int LINES_IN_RELAY_LOG_INFO_WITH_DELAY= 5;

  bool read_info(Rpl_info_handler *from);
  bool write_info(Rpl_info_handler *to, bool force);

  Relay_log_info& operator=(const Relay_log_info& info);
  Relay_log_info(const Relay_log_info& info);

  uint32 m_flags;
};

bool mysql_show_relaylog_events(THD* thd);
#endif /* RPL_RLI_H */
