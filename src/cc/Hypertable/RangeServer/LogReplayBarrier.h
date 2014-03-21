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

#ifndef Hypertable_RangeServer_LogReplayBarrier_h
#define Hypertable_RangeServer_LogReplayBarrier_h

#include <Hypertable/Lib/Types.h>

#include <Common/Mutex.h>

#include <boost/thread/condition.hpp>
#include <boost/thread/xtime.hpp>

#include <memory>

namespace Hypertable {

  class LogReplayBarrier {
  public:
    void set_root_complete();
    void set_metadata_complete();
    void set_system_complete();
    void set_user_complete();

    bool wait_for_root(boost::xtime expire_time);
    bool wait_for_metadata(boost::xtime expire_time);
    bool wait_for_system(boost::xtime expire_time);
    bool wait_for_user(boost::xtime expire_time);
    bool wait(boost::xtime expire_time,
              const TableIdentifier *table, const RangeSpec *range=0);

    bool user_complete();

  private:
    Mutex m_mutex;
    boost::condition m_root_complete_cond;
    boost::condition m_metadata_complete_cond;
    boost::condition m_system_complete_cond;
    boost::condition m_user_complete_cond;
    bool m_root_complete {};
    bool m_metadata_complete {};
    bool m_system_complete {};
    bool m_user_complete {};
  };

  typedef std::shared_ptr<LogReplayBarrier> LogReplayBarrierPtr;
  

} // namespace Hypertable

#endif // Hypertable_RangeServer_LogReplayBarrier_h
