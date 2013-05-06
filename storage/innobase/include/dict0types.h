/******************************************************
Data dictionary global types

(c) 1996 Innobase Oy

Created 1/8/1996 Heikki Tuuri
*******************************************************/

#ifndef dict0types_h
#define dict0types_h

typedef struct dict_sys_struct		dict_sys_t;
typedef struct dict_col_struct		dict_col_t;
typedef struct dict_field_struct	dict_field_t;
typedef struct dict_index_struct	dict_index_t;
typedef struct dict_table_struct	dict_table_t;
typedef struct dict_foreign_struct	dict_foreign_t;

typedef struct ind_node_struct		ind_node_t;
typedef struct tab_node_struct		tab_node_t;

#define TEMP_TABLE_PREFIX                "#sql"
#define TEMP_TABLE_PATH_PREFIX           "/" TEMP_TABLE_PREFIX

#endif
