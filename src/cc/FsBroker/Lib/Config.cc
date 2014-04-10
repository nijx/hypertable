/** -*- C++ -*-
 * Copyright (C) 2007-2012 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License.
 *
 * Hypertable is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include "Common/Compat.h"
#include "Common/InetAddr.h"
#include "Config.h"

namespace Hypertable { namespace Config {

void init_fs_client_options() {
  cmdline_desc().add_options()
    ("dfs", str(),
        "FS client endpoint in <host:port> format")
    ("dfs-timeout", i32(),
        "Timeout in milliseconds for FS client connections")
    ;
  alias("dfs-timeout", "DfsBroker.Timeout");
  // hidden aliases
  alias("dfs-host", "DfsBroker.Host");
  alias("dfs-port", "DfsBroker.Port");
}

void init_fs_client() {
  // prepare hidden aliases to be synced
  if (properties->has("dfs")) {
    Endpoint e = InetAddr::parse_endpoint(get_str("dfs"));
    bool defaulted = properties->defaulted("dfs");
    properties->set("dfs-host", e.host, defaulted);
    properties->set("dfs-port", e.port, !e.port || defaulted);
  }
}

void init_fs_broker_options() {
  cmdline_desc().add_options()
    ("port", i16(), "Listening port")
    ("pidfile", str(), "File to contain the process id")
    ;
}

}} // namespace Hypertable::Config
