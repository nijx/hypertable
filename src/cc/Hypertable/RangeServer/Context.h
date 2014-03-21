/* -*- c++ -*-
 * Copyright (C) 2007-2014 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License, or any later version.
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

#ifndef Hypertable_RangeServer_Context_h
#define Hypertable_RangeServer_Context_h

#include <Hypertable/RangeServer/ServerState.h>
#include <Hypertable/RangeServer/TableInfoMap.h>

#include <AsyncComm/Comm.h>

#include <Common/Properties.h>

#include <memory>

namespace Hypertable {

  class Context {
  public:
    PropertiesPtr props;
    Comm *comm {};
    std::shared_ptr<ServerState> server_state;
    TableInfoMapPtr live_map;
  };

  typedef std::shared_ptr<Context> ContextPtr;

}

#endif // Hypertable_RangeServer_Context_h
