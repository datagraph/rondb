#include "plugin.h"
typedef void * MYSQL_PLUGIN;
struct st_mysql_xid {
  long formatID;
  long gtrid_length;
  long bqual_length;
  char data[128];
};
typedef struct st_mysql_xid MYSQL_XID;
enum enum_mysql_show_type
{
  SHOW_UNDEF, SHOW_BOOL,
  SHOW_INT,
  SHOW_LONG,
  SHOW_LONGLONG,
  SHOW_CHAR, SHOW_CHAR_PTR,
  SHOW_ARRAY, SHOW_FUNC, SHOW_DOUBLE
};
enum enum_mysql_show_scope
{
  SHOW_SCOPE_UNDEF,
  SHOW_SCOPE_GLOBAL
};
struct st_mysql_show_var
{
  const char *name;
  char *value;
  enum enum_mysql_show_type type;
  enum enum_mysql_show_scope scope;
};
typedef int (*mysql_show_var_func)(void*, struct st_mysql_show_var*, char *);
struct st_mysql_sys_var;
struct st_mysql_value;
typedef int (*mysql_var_check_func)(void* thd,
                                    struct st_mysql_sys_var *var,
                                    void *save, struct st_mysql_value *value);
typedef void (*mysql_var_update_func)(void* thd,
                                      struct st_mysql_sys_var *var,
                                      void *var_ptr, const void *save);
struct st_mysql_plugin
{
  int type;
  void *info;
  const char *name;
  const char *author;
  const char *descr;
  int license;
  int (*init)(MYSQL_PLUGIN);
  int (*deinit)(MYSQL_PLUGIN);
  unsigned int version;
  struct st_mysql_show_var *status_vars;
  struct st_mysql_sys_var **system_vars;
  void * __reserved1;
  unsigned long flags;
};
struct st_mysql_daemon
{
  int interface_version;
};
struct st_mysql_information_schema
{
  int interface_version;
};
struct st_mysql_storage_engine
{
  int interface_version;
};
struct handlerton;
 struct Mysql_replication {
   int interface_version;
 };
struct st_mysql_value
{
  int (*value_type)(struct st_mysql_value *);
  const char *(*val_str)(struct st_mysql_value *, char *buffer, int *length);
  int (*val_real)(struct st_mysql_value *, double *realbuf);
  int (*val_int)(struct st_mysql_value *, long long *intbuf);
  int (*is_unsigned)(struct st_mysql_value *);
};
int thd_in_lock_tables(const void* thd);
int thd_tablespace_op(const void* thd);
long long thd_test_options(const void* thd, long long test_options);
int thd_sql_command(const void* thd);
const char *set_thd_proc_info(void* thd, const char *info,
                              const char *calling_func,
                              const char *calling_file,
                              const unsigned int calling_line);
void **thd_ha_data(const void* thd, const struct handlerton *hton);
void thd_storage_lock_wait(void* thd, long long value);
int thd_tx_isolation(const void* thd);
int thd_tx_is_read_only(const void* thd);
void* thd_tx_arbitrate(void* requestor, void* holder);
int thd_tx_priority(const void* thd);
int thd_tx_is_dd_trx(const void* thd);
char *thd_security_context(void* thd, char *buffer, size_t length,
                           size_t max_query_len);
void thd_inc_row_count(void* thd);
int thd_allow_batch(void* thd);
void thd_mark_transaction_to_rollback(void* thd, int all);
int mysql_tmpfile(const char *prefix);
int thd_killed(const void* thd);
void thd_set_kill_status(const void* thd);
void thd_binlog_pos(const void* thd,
                    const char **file_var,
                    unsigned long long *pos_var);
unsigned long thd_get_thread_id(const void* thd);
void thd_get_xid(const void* thd, MYSQL_XID *xid);
void mysql_query_cache_invalidate4(void* thd,
                                   const char *key, unsigned int key_length,
                                   int using_trx);
void *thd_get_ha_data(const void* thd, const struct handlerton *hton);
void thd_set_ha_data(void* thd, const struct handlerton *hton,
                     const void *ha_data);
#include "mysql/mysql_lex_string.h"
struct st_mysql_lex_string
{
  char *str;
  size_t length;
};
typedef struct st_mysql_lex_string MYSQL_LEX_STRING;
struct st_mysql_const_lex_string
{
  const char *str;
  size_t length;
};
typedef struct st_mysql_const_lex_string MYSQL_LEX_CSTRING;
#include "my_command.h"
enum enum_server_command
{
  COM_SLEEP,
  COM_QUIT,
  COM_INIT_DB,
  COM_QUERY,
  COM_FIELD_LIST,
  COM_CREATE_DB,
  COM_DROP_DB,
  COM_REFRESH,
  COM_DEPRECATED_1,
  COM_STATISTICS,
  COM_PROCESS_INFO,
  COM_CONNECT,
  COM_PROCESS_KILL,
  COM_DEBUG,
  COM_PING,
  COM_TIME,
  COM_DELAYED_INSERT,
  COM_CHANGE_USER,
  COM_BINLOG_DUMP,
  COM_TABLE_DUMP,
  COM_CONNECT_OUT,
  COM_REGISTER_SLAVE,
  COM_STMT_PREPARE,
  COM_STMT_EXECUTE,
  COM_STMT_SEND_LONG_DATA,
  COM_STMT_CLOSE,
  COM_STMT_RESET,
  COM_SET_OPTION,
  COM_STMT_FETCH,
  COM_DAEMON,
  COM_BINLOG_DUMP_GTID,
  COM_RESET_CONNECTION,
  COM_END
};
#include "my_sqlcommand.h"
enum enum_sql_command {
  SQLCOM_SELECT,
  SQLCOM_CREATE_TABLE,
  SQLCOM_CREATE_INDEX,
  SQLCOM_ALTER_TABLE,
  SQLCOM_UPDATE,
  SQLCOM_INSERT,
  SQLCOM_INSERT_SELECT,
  SQLCOM_DELETE,
  SQLCOM_TRUNCATE,
  SQLCOM_DROP_TABLE,
  SQLCOM_DROP_INDEX,
  SQLCOM_SHOW_DATABASES,
  SQLCOM_SHOW_TABLES,
  SQLCOM_SHOW_FIELDS,
  SQLCOM_SHOW_KEYS,
  SQLCOM_SHOW_VARIABLES,
  SQLCOM_SHOW_STATUS,
  SQLCOM_SHOW_ENGINE_LOGS,
  SQLCOM_SHOW_ENGINE_STATUS,
  SQLCOM_SHOW_ENGINE_MUTEX,
  SQLCOM_SHOW_PROCESSLIST,
  SQLCOM_SHOW_MASTER_STAT,
  SQLCOM_SHOW_SLAVE_STAT,
  SQLCOM_SHOW_GRANTS,
  SQLCOM_SHOW_CREATE,
  SQLCOM_SHOW_CHARSETS,
  SQLCOM_SHOW_COLLATIONS,
  SQLCOM_SHOW_CREATE_DB,
  SQLCOM_SHOW_TABLE_STATUS,
  SQLCOM_SHOW_TRIGGERS,
  SQLCOM_LOAD,
  SQLCOM_SET_OPTION,
  SQLCOM_LOCK_TABLES,
  SQLCOM_UNLOCK_TABLES,
  SQLCOM_GRANT,
  SQLCOM_CHANGE_DB,
  SQLCOM_CREATE_DB,
  SQLCOM_DROP_DB,
  SQLCOM_ALTER_DB,
  SQLCOM_REPAIR,
  SQLCOM_REPLACE,
  SQLCOM_REPLACE_SELECT,
  SQLCOM_CREATE_FUNCTION,
  SQLCOM_DROP_FUNCTION,
  SQLCOM_REVOKE,
  SQLCOM_OPTIMIZE,
  SQLCOM_CHECK,
  SQLCOM_ASSIGN_TO_KEYCACHE,
  SQLCOM_PRELOAD_KEYS,
  SQLCOM_FLUSH,
  SQLCOM_KILL,
  SQLCOM_ANALYZE,
  SQLCOM_ROLLBACK,
  SQLCOM_ROLLBACK_TO_SAVEPOINT,
  SQLCOM_COMMIT,
  SQLCOM_SAVEPOINT,
  SQLCOM_RELEASE_SAVEPOINT,
  SQLCOM_SLAVE_START,
  SQLCOM_SLAVE_STOP,
  SQLCOM_START_GROUP_REPLICATION,
  SQLCOM_STOP_GROUP_REPLICATION,
  SQLCOM_BEGIN,
  SQLCOM_CHANGE_MASTER,
  SQLCOM_CHANGE_REPLICATION_FILTER,
  SQLCOM_RENAME_TABLE,
  SQLCOM_RESET,
  SQLCOM_PURGE,
  SQLCOM_PURGE_BEFORE,
  SQLCOM_SHOW_BINLOGS,
  SQLCOM_SHOW_OPEN_TABLES,
  SQLCOM_HA_OPEN,
  SQLCOM_HA_CLOSE,
  SQLCOM_HA_READ,
  SQLCOM_SHOW_SLAVE_HOSTS,
  SQLCOM_DELETE_MULTI,
  SQLCOM_UPDATE_MULTI,
  SQLCOM_SHOW_BINLOG_EVENTS,
  SQLCOM_DO,
  SQLCOM_SHOW_WARNS,
  SQLCOM_EMPTY_QUERY,
  SQLCOM_SHOW_ERRORS,
  SQLCOM_SHOW_STORAGE_ENGINES,
  SQLCOM_SHOW_PRIVILEGES,
  SQLCOM_HELP,
  SQLCOM_CREATE_USER,
  SQLCOM_DROP_USER,
  SQLCOM_RENAME_USER,
  SQLCOM_REVOKE_ALL,
  SQLCOM_CHECKSUM,
  SQLCOM_CREATE_PROCEDURE,
  SQLCOM_CREATE_SPFUNCTION,
  SQLCOM_CALL,
  SQLCOM_DROP_PROCEDURE,
  SQLCOM_ALTER_PROCEDURE,
  SQLCOM_ALTER_FUNCTION,
  SQLCOM_SHOW_CREATE_PROC,
  SQLCOM_SHOW_CREATE_FUNC,
  SQLCOM_SHOW_STATUS_PROC,
  SQLCOM_SHOW_STATUS_FUNC,
  SQLCOM_PREPARE,
  SQLCOM_EXECUTE,
  SQLCOM_DEALLOCATE_PREPARE,
  SQLCOM_CREATE_VIEW,
  SQLCOM_DROP_VIEW,
  SQLCOM_CREATE_TRIGGER,
  SQLCOM_DROP_TRIGGER,
  SQLCOM_XA_START,
  SQLCOM_XA_END,
  SQLCOM_XA_PREPARE,
  SQLCOM_XA_COMMIT,
  SQLCOM_XA_ROLLBACK,
  SQLCOM_XA_RECOVER,
  SQLCOM_SHOW_PROC_CODE,
  SQLCOM_SHOW_FUNC_CODE,
  SQLCOM_ALTER_TABLESPACE,
  SQLCOM_INSTALL_PLUGIN,
  SQLCOM_UNINSTALL_PLUGIN,
  SQLCOM_BINLOG_BASE64_EVENT,
  SQLCOM_SHOW_PLUGINS,
  SQLCOM_CREATE_SERVER,
  SQLCOM_DROP_SERVER,
  SQLCOM_ALTER_SERVER,
  SQLCOM_CREATE_EVENT,
  SQLCOM_ALTER_EVENT,
  SQLCOM_DROP_EVENT,
  SQLCOM_SHOW_CREATE_EVENT,
  SQLCOM_SHOW_EVENTS,
  SQLCOM_SHOW_CREATE_TRIGGER,
  SQLCOM_SHOW_PROFILE,
  SQLCOM_SHOW_PROFILES,
  SQLCOM_SIGNAL,
  SQLCOM_RESIGNAL,
  SQLCOM_SHOW_RELAYLOG_EVENTS,
  SQLCOM_GET_DIAGNOSTICS,
  SQLCOM_ALTER_USER,
  SQLCOM_EXPLAIN_OTHER,
  SQLCOM_SHOW_CREATE_USER,
  SQLCOM_SHUTDOWN,
  SQLCOM_SET_PASSWORD,
  SQLCOM_ALTER_INSTANCE,
  SQLCOM_END
};
typedef enum
{
  MYSQL_AUDIT_GENERAL_CLASS = 0,
  MYSQL_AUDIT_CONNECTION_CLASS = 1,
  MYSQL_AUDIT_PARSE_CLASS = 2,
  MYSQL_AUDIT_AUTHORIZATION_CLASS = 3,
  MYSQL_AUDIT_TABLE_ACCESS_CLASS = 4,
  MYSQL_AUDIT_GLOBAL_VARIABLE_CLASS = 5,
  MYSQL_AUDIT_SERVER_STARTUP_CLASS = 6,
  MYSQL_AUDIT_SERVER_SHUTDOWN_CLASS = 7,
  MYSQL_AUDIT_COMMAND_CLASS = 8,
  MYSQL_AUDIT_QUERY_CLASS = 9,
  MYSQL_AUDIT_STORED_PROGRAM_CLASS = 10,
  MYSQL_AUDIT_CLASS_MASK_SIZE
} mysql_event_class_t;
struct st_mysql_audit
{
  int interface_version;
  void (*release_thd)(void*);
  int (*event_notify)(void*, mysql_event_class_t, const void *);
  unsigned long class_mask[MYSQL_AUDIT_CLASS_MASK_SIZE];
};
typedef enum enum_sql_command enum_sql_command_t;
typedef enum
{
  MYSQL_AUDIT_GENERAL_LOG = 1 << 0,
  MYSQL_AUDIT_GENERAL_ERROR = 1 << 1,
  MYSQL_AUDIT_GENERAL_RESULT = 1 << 2,
  MYSQL_AUDIT_GENERAL_STATUS = 1 << 3
} mysql_event_general_subclass_t;
struct mysql_event_general
{
  mysql_event_general_subclass_t event_subclass;
  int general_error_code;
  unsigned long general_thread_id;
  MYSQL_LEX_CSTRING general_user;
  MYSQL_LEX_CSTRING general_command;
  MYSQL_LEX_CSTRING general_query;
  struct charset_info_st *general_charset;
  unsigned long long general_time;
  unsigned long long general_rows;
  MYSQL_LEX_CSTRING general_host;
  MYSQL_LEX_CSTRING general_sql_command;
  MYSQL_LEX_CSTRING general_external_user;
  MYSQL_LEX_CSTRING general_ip;
};
typedef enum
{
  MYSQL_AUDIT_CONNECTION_CONNECT = 1 << 0,
  MYSQL_AUDIT_CONNECTION_DISCONNECT = 1 << 1,
  MYSQL_AUDIT_CONNECTION_CHANGE_USER = 1 << 2,
  MYSQL_AUDIT_CONNECTION_PRE_AUTHENTICATE = 1 << 3
} mysql_event_connection_subclass_t;
struct mysql_event_connection
{
  mysql_event_connection_subclass_t event_subclass;
  int status;
  unsigned long connection_id;
  MYSQL_LEX_CSTRING user;
  MYSQL_LEX_CSTRING priv_user;
  MYSQL_LEX_CSTRING external_user;
  MYSQL_LEX_CSTRING proxy_user;
  MYSQL_LEX_CSTRING host;
  MYSQL_LEX_CSTRING ip;
  MYSQL_LEX_CSTRING database;
  int connection_type;
};
typedef enum
{
  MYSQL_AUDIT_PARSE_PREPARSE = 1 << 0,
  MYSQL_AUDIT_PARSE_POSTPARSE = 1 << 1
} mysql_event_parse_subclass_t;
typedef enum
{
  MYSQL_AUDIT_PARSE_REWRITE_PLUGIN_NONE = 0,
  MYSQL_AUDIT_PARSE_REWRITE_PLUGIN_QUERY_REWRITTEN = 1 << 0,
  MYSQL_AUDIT_PARSE_REWRITE_PLUGIN_IS_PREPARED_STATEMENT = 1 << 1
} mysql_event_parse_rewrite_plugin_flag;
struct mysql_event_parse
{
  mysql_event_parse_subclass_t event_subclass;
  mysql_event_parse_rewrite_plugin_flag *flags;
  MYSQL_LEX_CSTRING query;
  MYSQL_LEX_CSTRING *rewritten_query;
};
typedef enum
{
  MYSQL_AUDIT_AUTHORIZATION_USER = 1 << 0,
  MYSQL_AUDIT_AUTHORIZATION_DB = 1 << 1,
  MYSQL_AUDIT_AUTHORIZATION_TABLE = 1 << 2,
  MYSQL_AUDIT_AUTHORIZATION_COLUMN = 1 << 3,
  MYSQL_AUDIT_AUTHORIZATION_PROCEDURE = 1 << 4,
  MYSQL_AUDIT_AUTHORIZATION_PROXY = 1 << 5
} mysql_event_authorization_subclass_t;
struct mysql_event_authorization
{
  mysql_event_authorization_subclass_t event_subclass;
  int status;
  unsigned int connection_id;
  enum_sql_command_t sql_command_id;
  MYSQL_LEX_CSTRING query;
  const struct charset_info_st *query_charset;
  MYSQL_LEX_CSTRING database;
  MYSQL_LEX_CSTRING table;
  MYSQL_LEX_CSTRING object;
  unsigned long requested_privilege;
  unsigned long granted_privilege;
};
enum mysql_event_table_access_subclass_t
{
  MYSQL_AUDIT_TABLE_ACCESS_READ = 1 << 0,
  MYSQL_AUDIT_TABLE_ACCESS_INSERT = 1 << 1,
  MYSQL_AUDIT_TABLE_ACCESS_UPDATE = 1 << 2,
  MYSQL_AUDIT_TABLE_ACCESS_DELETE = 1 << 3
};
typedef enum mysql_event_table_access_subclass_t mysql_event_table_access_subclass_t;
struct mysql_event_table_access
{
  mysql_event_table_access_subclass_t event_subclass;
  unsigned long connection_id;
  enum_sql_command_t sql_command_id;
  MYSQL_LEX_CSTRING query;
  const struct charset_info_st *query_charset;
  MYSQL_LEX_CSTRING table_database;
  MYSQL_LEX_CSTRING table_name;
};
typedef enum
{
  MYSQL_AUDIT_GLOBAL_VARIABLE_GET = 1 << 0,
  MYSQL_AUDIT_GLOBAL_VARIABLE_SET = 1 << 1
} mysql_event_global_variable_subclass_t;
struct mysql_event_global_variable
{
  mysql_event_global_variable_subclass_t event_subclass;
  unsigned long connection_id;
  enum_sql_command_t sql_command_id;
  MYSQL_LEX_CSTRING variable_name;
  MYSQL_LEX_CSTRING variable_value;
};
typedef enum
{
  MYSQL_AUDIT_SERVER_STARTUP_STARTUP = 1 << 0
} mysql_event_server_startup_subclass_t;
struct mysql_event_server_startup
{
  mysql_event_server_startup_subclass_t event_subclass;
  const char **argv;
  unsigned int argc;
};
typedef enum
{
  MYSQL_AUDIT_SERVER_SHUTDOWN_SHUTDOWN = 1 << 0
} mysql_event_server_shutdown_subclass_t;
typedef enum
{
  MYSQL_AUDIT_SERVER_SHUTDOWN_REASON_SHUTDOWN,
  MYSQL_AUDIT_SERVER_SHUTDOWN_REASON_ABORT
} mysql_server_shutdown_reason_t;
struct mysql_event_server_shutdown
{
  mysql_event_server_shutdown_subclass_t event_subclass;
  int exit_code;
  mysql_server_shutdown_reason_t reason;
};
typedef enum
{
  MYSQL_AUDIT_COMMAND_START = 1 << 0,
  MYSQL_AUDIT_COMMAND_END = 1 << 1
} mysql_event_command_subclass_t;
typedef enum enum_server_command enum_server_command_t;
struct mysql_event_command
{
  mysql_event_command_subclass_t event_subclass;
  int status;
  unsigned long connection_id;
  enum_server_command_t command_id;
};
typedef enum
{
  MYSQL_AUDIT_QUERY_START = 1 << 0,
  MYSQL_AUDIT_QUERY_NESTED_START = 1 << 1,
  MYSQL_AUDIT_QUERY_STATUS_END = 1 << 2,
  MYSQL_AUDIT_QUERY_NESTED_STATUS_END = 1 << 3
} mysql_event_query_subclass_t;
struct mysql_event_query
{
  mysql_event_query_subclass_t event_subclass;
  int status;
  unsigned long connection_id;
  enum_sql_command_t sql_command_id;
  MYSQL_LEX_CSTRING query;
  const struct charset_info_st *query_charset;
};
typedef enum
{
  MYSQL_AUDIT_STORED_PROGRAM_EXECUTE = 1 << 0
} mysql_event_stored_program_subclass_t;
struct mysql_event_stored_program
{
  mysql_event_stored_program_subclass_t event_subclass;
  unsigned long connection_id;
  enum_sql_command_t sql_command_id;
  MYSQL_LEX_CSTRING query;
  const struct charset_info_st *query_charset;
  MYSQL_LEX_CSTRING database;
  MYSQL_LEX_CSTRING name;
  void *parameters;
};
