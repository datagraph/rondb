/*
  Copyright (c) 2016, 2018, Oracle and/or its affiliates. All rights reserved.

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

#include <memory>

#include "mock_metadata.h"

std::shared_ptr<MetaData> meta_data;

/**
 * Create an instance of the mock metadata.
 * @param cluster_type type of the cluster the metadata cache object will
 *                     represent (GR or ReplicaSet)
 * @param user The user name used to authenticate to the metadata server.
 * @param password The password used to authenticate to the metadata server.
 * @param connect_timeout The time after which trying to connect to the
 *                        metadata server should timeout.
 * @param read_timeout The time after which read from the metadata server should
 *                     timeout.
 * @param connection_attempts The number of times a connection to metadata must
 * be attempted, when a connection attempt fails.
 * @param ssl_options ssl options
 * @param use_cluster_notifications Flag indicating if the metadata cache
 *                                  should use cluster notifications as an
 *                                  additional trigger for metadata refresh
 *                                  (only available for GR cluster type)
 * @param view_id last known view_id of the cluster metadata (only relevant
 *                for ReplicaSet cluster)
 */
std::shared_ptr<MetaData> get_instance(
    mysqlrouter::ClusterType /*cluster_type*/, const std::string &user,
    const std::string &password, int connect_timeout, int read_timeout,
    int connection_attempts, const mysqlrouter::SSLOptions &ssl_options,
    const bool use_cluster_notifications, unsigned /*view_id*/ = 0) {
  meta_data.reset(new MockNG(user, password, connect_timeout, read_timeout,
                             connection_attempts, ssl_options,
                             use_cluster_notifications));
  return meta_data;
}
