/*
   Copyright (c) 2017, Oracle and/or its affiliates. All rights reserved.

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

// Implements the functions defined in ndb_dd.h
#include "sql/ndb_dd.h"

// Using ndb_dd_client
#include "sql/ndb_dd_client.h"

#include "sql/sql_class.h"
#include "sql/transaction.h"


#include "sql/dd/dd.h"
#include "sql/dd/types/schema.h"
#include "sql/dd/types/table.h"
#include "sql/dd/cache/dictionary_client.h" // dd::Dictionary_client
#include "sql/dd/impl/sdi.h"           // dd::deserialize
#include "sql/dd/dd_table.h"

bool ndb_sdi_serialize(THD *thd,
                       const dd::Table *table_def,
                       const char* schema_name,
                       dd::sdi_t& sdi)
{
  // Require the table to be visible or else have temporary name
  DBUG_ASSERT(table_def->hidden() == dd::Abstract_table::HT_VISIBLE ||
              is_prefix(table_def->name().c_str(), tmp_file_prefix));

  // Make a copy of the table definition to allow it to
  // be modified before serialization
  std::unique_ptr<dd::Table> table_def_clone(
        dynamic_cast<dd::Table*>(table_def->clone()));

  // Don't include the se_private_id in the serialized table def.
  table_def_clone->set_se_private_id(dd::INVALID_OBJECT_ID);

  // Don't include any se_private_data properties in the
  // serialized table def.
  table_def_clone->se_private_data().clear();

  sdi = dd::serialize(thd, *table_def_clone, dd::String_type(schema_name));
  if (sdi.empty())
    return false; // Failed to serialize

  return true; // OK
}


/*
  Workaround for BUG#25657041

  During inplace alter table, the table has a temporary
  tablename and is also marked as hidden. Since the temporary
  name and hidden status is part of the serialized table
  definition, there's a mismatch down the line when this is
  stored as extra metadata in the NDB dictionary.

  The workaround for now involves setting the table as a user
  visible table and restoring the original table name
*/

void ndb_dd_fix_inplace_alter_table_def(dd::Table* table_def,
                                        const char* proper_table_name)
{
  DBUG_ENTER("ndb_dd_fix_inplace_alter_table_def");
  DBUG_PRINT("enter", ("table_name: %s", table_def->name().c_str()));
  DBUG_PRINT("enter", ("proper_table_name: %s", proper_table_name));

  // Check that the proper_table_name is not a temporary name
  DBUG_ASSERT(!is_prefix(proper_table_name, tmp_file_prefix));

  table_def->set_name(proper_table_name);
  table_def->set_hidden(dd::Abstract_table::HT_VISIBLE);

  DBUG_VOID_RETURN;
}


bool ndb_dd_does_table_exist(class THD *thd,
                             const char* schema_name,
                             const char* table_name,
                             int& table_id,
                             int& table_version)

{
  DBUG_ENTER("ndb_dd_does_table_exist");

  Ndb_dd_client dd_client(thd);

  // First acquire MDL locks on schema and table
  if (!dd_client.mdl_locks_acquire(schema_name, table_name))
  {
    DBUG_RETURN(false);
  }

  {
    dd::cache::Dictionary_client* client= thd->dd_client();
    dd::cache::Dictionary_client::Auto_releaser ar{client};

    const dd::Schema *schema= nullptr;
    if (client->acquire(schema_name, &schema))
    {
      DBUG_RETURN(false);
    }
    if (schema == nullptr)
    {
      DBUG_ASSERT(false); // Database does not exist
      DBUG_RETURN(false);
    }

    const dd::Table *existing= nullptr;
    if (client->acquire(schema->name(), table_name, &existing))
    {
      DBUG_RETURN(false);
    }

    if (existing == nullptr)
    {
      // Table does not exist in DD
      DBUG_RETURN(false);
    }

    ndb_dd_table_get_object_id_and_version(existing, table_id, table_version);

    trans_commit_stmt(thd);
    trans_commit(thd);
  }

  DBUG_RETURN(true); // OK!
}


bool
ndb_dd_install_table(class THD *thd,
                     const char* schema_name,
                     const char* table_name,
                     const dd::sdi_t &sdi,
                     int ndb_table_id, int ndb_table_version,
                     bool force_overwrite)
{

  DBUG_ENTER("ndb_dd_install_table");

  Ndb_dd_client dd_client(thd);

  // First acquire exclusive MDL locks on schema and table
  if (!dd_client.mdl_locks_acquire_exclusive(schema_name, table_name))
  {
    DBUG_RETURN(false);
  }

  {
    dd::cache::Dictionary_client* client= thd->dd_client();
    dd::cache::Dictionary_client::Auto_releaser ar{client};

    const dd::Schema *schema= nullptr;

    if (client->acquire(schema_name, &schema))
    {
      DBUG_RETURN(false);
    }
    if (schema == nullptr)
    {
      DBUG_ASSERT(false); // Database does not exist
      DBUG_RETURN(false);
    }

    std::unique_ptr<dd::Table> table_object{dd::create_object<dd::Table>()};
    if (dd::deserialize(thd, sdi, table_object.get()))
    {
      DBUG_RETURN(false);
    }

    // Verfiy that table defintion unpacked from NDB
    // does not have any se_private fields set, those will be set
    // from the NDB table metadata
    DBUG_ASSERT(table_object->se_private_id() == dd::INVALID_OBJECT_ID);
    DBUG_ASSERT(table_object->se_private_data().raw_string() == "");

    // Assign the id of the schema to the table_object
    table_object->set_schema_id(schema->id());

    // Asign NDB id and version of the table
    ndb_dd_table_set_object_id_and_version(table_object.get(),
                                           ndb_table_id, ndb_table_version);

    const dd::Table *existing= nullptr;
    if (client->acquire(schema->name(), table_object->name(), &existing))
    {
      DBUG_RETURN(false);
    }

    if (existing != nullptr)
    {
      // Table already exists
      if (!force_overwrite)
      {
        // Don't overwrite existing table
        DBUG_ASSERT(false);
        DBUG_RETURN(false);
      }

      // Continue and remove the old table before
      // installing the new
      DBUG_PRINT("info", ("dropping existing table"));
      if (client->drop(existing))
      {
        // Failed to drop existing
        DBUG_ASSERT(false); // Catch in debug, unexpected error
        DBUG_RETURN(false);
      }

    }

    if (client->store(table_object.get()))
    {
      // trans_rollback...
      DBUG_ASSERT(false); // Failed to store
      DBUG_RETURN(false);
    }

    trans_commit_stmt(thd);
    trans_commit(thd);
  }

  DBUG_RETURN(true); // OK!
}


bool
ndb_dd_drop_table(THD *thd,
                  const char *schema_name, const char *table_name)
{
  DBUG_ENTER("ndb_dd_drop_table");

  Ndb_dd_client dd_client(thd);

  {
    dd::cache::Dictionary_client* client= thd->dd_client();
    dd::cache::Dictionary_client::Auto_releaser releaser{client};

    const dd::Table *existing= nullptr;
    if (client->acquire(schema_name, table_name, &existing))
    {
      DBUG_RETURN(false);
    }

    if (existing == nullptr)
    {
      // Table does not exist
      DBUG_RETURN(false);
    }

    DBUG_PRINT("info", ("dropping existing table"));
    if (client->drop(existing))
    {
      // Failed to drop existing
      DBUG_ASSERT(false); // Catch in debug, unexpected error
      DBUG_RETURN(false);
    }

    trans_commit_stmt(thd);
    trans_commit(thd);
  }

  DBUG_RETURN(true); // OK
}


bool
ndb_dd_rename_table(THD *thd,
                    const char *old_schema_name, const char *old_table_name,
                    const char *new_schema_name, const char *new_table_name)
{
  DBUG_ENTER("ndb_dd_rename_table");
  DBUG_PRINT("enter", ("old: '%s'.'%s'  new: '%s'.'%s'",
                       old_schema_name, old_table_name,
                       new_schema_name, new_table_name));

  Ndb_dd_client dd_client(thd);

  dd::cache::Dictionary_client* client= thd->dd_client();
  dd::cache::Dictionary_client::Auto_releaser releaser(client);

  // Read new schema from DD
  const dd::Schema *new_schema= nullptr;
  if (client->acquire(new_schema_name, &new_schema))
  {
    DBUG_RETURN(false);
  }
  if (new_schema == nullptr)
  {
    // Database does not exist, unexpected
    DBUG_ASSERT(false);
    DBUG_RETURN(false);
  }

  // Read table from DD
  dd::Table *to_table_def= NULL;
  if (client->acquire_for_modification(old_schema_name, old_table_name,
                                       &to_table_def))
    DBUG_RETURN(false);


  // Set schema id and table name
  to_table_def->set_schema_id(new_schema->id());
  to_table_def->set_name(new_table_name);

  // Rename foreign keys
  if (dd::rename_foreign_keys(old_table_name, to_table_def))
  {
    // Failed to rename foreign keys or commit/rollback, unexpected
    DBUG_ASSERT(false);
    DBUG_RETURN(false);
  }

  // Save table in DD
  if (client->update(to_table_def))
  {
    // Failed to save, unexpected
    DBUG_ASSERT(false);
    DBUG_RETURN(false);
  }

  trans_commit_stmt(thd);
  trans_commit(thd);

  DBUG_RETURN(true); // OK
}


bool
ndb_dd_table_get_engine(THD *thd,
                        const char *schema_name,
                        const char *table_name,
                        dd::String_type* engine)
{
  DBUG_ENTER("ndb_dd_table_get_engine");

  Ndb_dd_client dd_client(thd);

  {
    dd::cache::Dictionary_client* client= thd->dd_client();
    dd::cache::Dictionary_client::Auto_releaser releaser{client};

    const dd::Table *existing= nullptr;
    if (client->acquire(schema_name, table_name, &existing))
    {
      DBUG_RETURN(false);
    }

    if (existing == nullptr)
    {
      // Table does not exist
      DBUG_RETURN(false);
    }

    DBUG_PRINT("info", (("table '%s.%s' exists in DD, engine: '%s'"),
                        schema_name, table_name, existing->engine().c_str()));
    *engine = existing->engine();

    trans_commit_stmt(thd);
    trans_commit(thd);
  }

  DBUG_RETURN(true); // Table exist
}

#ifndef FIXED_BUG26723442
// The key used to _temporarily_ store the NDB tables object version in the
// se_private_data field of DD
static const char* object_id_key = "object_id";
#endif

// The key used to store the NDB tables object version in the
// se_private_data field of DD
static const char* object_version_key = "object_version";

void
ndb_dd_table_set_object_id_and_version(dd::Table* table_def,
                                       int object_id, int object_version)
{
  DBUG_ENTER("ndb_dd_table_set_object_id_and_version");
  DBUG_PRINT("enter", ("object_id: %d, object_version: %d",
                       object_id, object_version));

#ifndef FIXED_BUG26723442
  // Temporarily save the object id in se_private_data
  table_def->se_private_data().set_int32(object_id_key,
                                         object_id);
#else
  table_def->set_se_private_id(object_id);
#endif
  table_def->se_private_data().set_int32(object_version_key,
                                         object_version);
  DBUG_VOID_RETURN;
}


bool
ndb_dd_table_get_object_id_and_version(const dd::Table* table_def,
                                       int& object_id, int& object_version)
{
  DBUG_ENTER("ndb_dd_table_get_object_id_and_version");

#ifndef FIXED_BUG26723442
  // Temporarily read the object id from se_private_data
  if (!table_def->se_private_data().exists(object_id_key))
  {
    DBUG_PRINT("error", ("Table definition didn't contain "
                         "property '%s'", object_id_key));
    DBUG_RETURN(false);
  }

  if (table_def->se_private_data().get_int32(object_id_key,
                                             &object_id))
  {
    DBUG_PRINT("error", ("Table definition didn't have a valid number "
                         "for '%s'", object_id_key));
    DBUG_RETURN(false);
  }

#else
  if (table_def->se_private_id() == dd::INVALID_OBJECT_ID)
  {
    DBUG_PRINT("error", ("Table definition contained an invalid object id"));
    DBUG_RETURN(false);
  }
  object_id = table_def->se_private_id();
#endif

  if (!table_def->se_private_data().exists(object_version_key))
  {
    DBUG_PRINT("error", ("Table definition didn't contain property '%s'",
                         object_version_key));
    DBUG_RETURN(false);
  }

  if (table_def->se_private_data().get_int32(object_version_key,
                                             &object_version))
  {
    DBUG_PRINT("error", ("Table definition didn't have a valid number for '%s'",
                         object_version_key));
    DBUG_RETURN(false);
  }

  DBUG_PRINT("exit", ("object_id: %d, object_version: %d",
                      object_id, object_version));

  DBUG_RETURN(true);
}

