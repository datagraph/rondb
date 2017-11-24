/* Copyright (c) 2016, 2017, Oracle and/or its affiliates. All rights reserved.

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

#ifndef KEYS_CONTAINER_INCLUDED
#define KEYS_CONTAINER_INCLUDED

#include <sys/types.h>

#include "m_ctype.h"
#include "map_helpers.h"
#include "my_inttypes.h"
#include "my_sharedlib.h"
#include "plugin/keyring/common/i_keyring_io.h"
#include "plugin/keyring/common/i_keys_container.h"
#include "plugin/keyring/common/keyring_key.h"
#include "plugin/keyring/common/keyring_memory.h"
#include "plugin/keyring/common/logger.h"
#include "sql/sys_vars_shared.h" //For PolyLock, AutoWLock, AutoRLock
#include <vector>

namespace keyring {

class Keys_container : public IKeys_container
{
private:
  bool remove_keys_metadata(IKey *key);
  void store_keys_metadata(IKey *key);
public:
  Keys_container(ILogger* logger);
  bool init(IKeyring_io* keyring_io, std::string keyring_storage_url);
  bool store_key(IKey *key);
  IKey* fetch_key(IKey *key);
  bool remove_key(IKey *key);
  std::string get_keyring_storage_url();
  void set_keyring_io(IKeyring_io *keyring_io);
  std::vector<Key_metadata> get_keys_metadata()
  {
    return keys_metadata;
  }

  ~Keys_container();

  ulong get_number_of_keys()
  {
    return keys_hash->size();
  };

protected:
  Keys_container(const Keys_container &);

  virtual void allocate_and_set_data_for_key(IKey *key,
                                             std::string *source_key_type,
                                             uchar *source_key_data,
                                             size_t source_key_data_size);
  bool load_keys_from_keyring_storage();
  void free_keys_hash();
  IKey *get_key_from_hash(IKey *key);
  bool store_key_in_hash(IKey *key);
  bool remove_key_from_hash(IKey *key);
  virtual bool flush_to_backup();
  virtual bool flush_to_storage(IKey *key, Key_operation operation);

  using Key_hash= collation_unordered_map<std::string, std::unique_ptr<IKey>>;
  std::unique_ptr<Key_hash> keys_hash;
  std::vector<Key_metadata> keys_metadata;
  ILogger *logger;
  IKeyring_io *keyring_io;
  std::string keyring_storage_url;
};

} //namespace keyring

#endif //KEYS_CONTAINER_INCLUDED
