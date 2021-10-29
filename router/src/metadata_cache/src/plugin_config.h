/*
  Copyright (c) 2016, 2021, Oracle and/or its affiliates.

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
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef METADATA_CACHE_PLUGIN_CONFIG_INCLUDED
#define METADATA_CACHE_PLUGIN_CONFIG_INCLUDED

#include "mysqlrouter/metadata_cache_export.h"

#include "mysqlrouter/metadata_cache.h"

#include <chrono>
#include <map>
#include <string>
#include <vector>

#include "mysql/harness/config_parser.h"
#include "mysql/harness/plugin.h"
#include "mysqlrouter/cluster_metadata_dynamic_state.h"
#include "mysqlrouter/plugin_config.h"
#include "tcp_address.h"

extern "C" {
extern mysql_harness::Plugin METADATA_CACHE_EXPORT
    harness_plugin_metadata_cache;
}

class MetadataCachePluginConfig final : public mysqlrouter::BasePluginConfig {
 public:
  /** @brief Constructor
   *
   * @param section from configuration file provided as ConfigSection
   */
  MetadataCachePluginConfig(const mysql_harness::ConfigSection *section)
      : BasePluginConfig(section),
        metadata_cache_dynamic_state(get_dynamic_state(section)),
        metadata_servers_addresses(get_metadata_servers(
            section, metadata_cache::kDefaultMetadataPort)),
        user(get_option_string(section, "user")),
        ttl(get_option_milliseconds(section, "ttl", 0.0, 3600.0)),
        auth_cache_ttl(
            get_option_milliseconds(section, "auth_cache_ttl", -1, 3600.0)),
        auth_cache_refresh_interval(get_option_milliseconds(
            section, "auth_cache_refresh_interval", 0.001, 3600.0)),
        cluster_name(get_option_string(section, "metadata_cluster")),
        connect_timeout(
            get_uint_option<uint16_t>(section, "connect_timeout", 1)),
        read_timeout(get_uint_option<uint16_t>(section, "read_timeout", 1)),
        thread_stack_size(
            get_uint_option<uint32_t>(section, "thread_stack_size", 1, 65535)),
        use_gr_notifications(get_uint_option<uint16_t>(
                                 section, "use_gr_notifications", 0, 1) == 1),
        cluster_type(get_cluster_type(section)),
        router_id(get_uint_option<uint32_t>(section, "router_id")) {
    if (cluster_type == mysqlrouter::ClusterType::RS_V2 &&
        section->has("use_gr_notifications")) {
      throw std::invalid_argument(
          "option 'use_gr_notifications' is not valid for cluster type 'rs'");
    }
    if (auth_cache_ttl > std::chrono::seconds(-1) &&
        auth_cache_ttl < std::chrono::milliseconds(1)) {
      throw std::invalid_argument(
          "'auth_cache_ttl' option value '" +
          get_option_string(section, "auth_cache_ttl") +
          "' should be in range 0.001 and 3600 inclusive or -1 for "
          "auth_cache_ttl disabled");
    }
  }

  /**
   * @param option name of the option
   */
  std::string get_default(const std::string &option) const override;
  bool is_required(const std::string &option) const override;

  mutable std::unique_ptr<ClusterMetadataDynamicState>
      metadata_cache_dynamic_state;
  /** @brief MySQL Metadata hosts to connect with */
  const metadata_cache::metadata_servers_list_t metadata_servers_addresses;
  /** @brief User used for authenticating with MySQL Metadata */
  const std::string user;
  /** @brief TTL used for storing data in the cache */
  const std::chrono::milliseconds ttl;
  /** @brief TTL used for limiting the lifetime of the rest user authentication
   * data stored in the metadata */
  const std::chrono::milliseconds auth_cache_ttl;
  /** @brief Refresh rate of the rest user authentication data stored in the
   * cache */
  const std::chrono::milliseconds auth_cache_refresh_interval;
  /** @brief Name of the Cluster this Router instance was bootstrapped to use.
   */
  const std::string cluster_name;
  /** @brief connect_timeout The time in seconds after which trying to connect
   * to metadata server timeouts */
  const unsigned int connect_timeout;
  /** @brief read_timeout The time in seconds after which read from metadata
   * server timeouts */
  const unsigned int read_timeout;
  /** @brief memory in kilobytes allocated for thread's stack */
  const unsigned int thread_stack_size;
  /** @brief  Whether we should listen to GR notifications from the cluster
   * nodes. */
  const bool use_gr_notifications;
  /** @brief  Type of the cluster this configuration was bootstrap against. */
  const mysqlrouter::ClusterType cluster_type;
  /** @brief  Id of the router in the metadata. */
  const unsigned int router_id;

  /** @brief Gets (Group Replication ID for GR cluster or cluster_id for
   * ReplicaSet cluster) if preset in the dynamic configuration.
   *
   * @note  If there is no dynamic configuration (backward compatibility) it
   * returns empty string.
   */
  std::string get_cluster_type_specific_id() const;

  std::string get_clusterset_id() const;

  /** @brief Gets last know ReplicaSet cluster metadata view_id stored in the
   * dynamic state file . */
  uint64_t get_view_id() const;

 private:
  /** @brief Gets a list of metadata servers.
   *
   *
   * Throws std::invalid_argument on errors.
   *
   * @param section Instance of ConfigSection
   * @param default_port Use this port when none was provided
   * @return std::vector<mysql_harness::TCPAddress>
   */
  std::vector<mysql_harness::TCPAddress> get_metadata_servers(
      const mysql_harness::ConfigSection *section, uint16_t default_port) const;

  mysqlrouter::ClusterType get_cluster_type(
      const mysql_harness::ConfigSection *section);

  std::unique_ptr<ClusterMetadataDynamicState> get_dynamic_state(
      const mysql_harness::ConfigSection *section);
};

#endif  // METADATA_CACHE_PLUGIN_CONFIG_INCLUDED
