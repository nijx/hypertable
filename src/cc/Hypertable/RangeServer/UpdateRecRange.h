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

#ifndef Hypertable_RangeServer_UpdateRecRange_h
#define Hypertable_RangeServer_UpdateRecRange_h

#include <Hypertable/RangeServer/Range.h>
#include <Hypertable/RangeServer/UpdateRequest.h>

#include <Hypertable/Lib/CommitLog.h>

#include <Common/DynamicBuffer.h>

#include <vector>

namespace Hypertable {

  class UpdateRecRange {
  public:
    DynamicBuffer *bufp;
    uint64_t offset;
    uint64_t len;
  };

  class UpdateRecRangeList {
  public:
    void reset_updates(UpdateRequest *request) {
      if (request == last_request) {
        if (starting_update_count < updates.size())
          updates.resize(starting_update_count);
        transfer_buf.ptr = transfer_buf.base + transfer_buf_reset_offset;
      }
    }
    void add_update(UpdateRequest *request, UpdateRecRange &update) {
      if (request != last_request) {
        starting_update_count = updates.size();
        last_request = request;
        transfer_buf_reset_offset = transfer_buf.empty() ? 0 : transfer_buf.fill();
      }
      if (update.len)
        updates.push_back(update);
    }
    RangePtr range;
    std::vector<UpdateRecRange> updates;
    UpdateRequest *last_request {};
    DynamicBuffer transfer_buf;
    CommitLogPtr transfer_log;
    int64_t latest_transfer_revision {TIMESTAMP_MIN};
    size_t starting_update_count {};
    uint32_t transfer_buf_reset_offset {};
    bool range_blocked {};
  };

}

#endif // Hypertable_RangeServer_UpdateRecRange_h
