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

#ifndef Hypertable_RangeServer_UpdatePipeline_h
#define Hypertable_RangeServer_UpdatePipeline_h

#include <Hypertable/RangeServer/Context.h>
#include <Hypertable/RangeServer/QueryCache.h>
#include <Hypertable/RangeServer/TimerHandler.h>
#include <Hypertable/RangeServer/UpdateContext.h>

#include <Hypertable/Lib/KeySpec.h>

#include <Common/ByteString.h>
#include <Common/DynamicBuffer.h>

#include <memory>
#include <thread>

namespace Hypertable {

  /**
   */
  class UpdatePipeline {
  public:

    UpdatePipeline(ContextPtr &context, QueryCachePtr &query_cache,
                   TimerHandlerPtr &timer_handler);

    void add(UpdateContext *uc);

    void shutdown();

  private:

    void qualify_and_transform();
    void commit();
    void add_and_respond();

    void transform_key(ByteString &bskey, DynamicBuffer *dest_bufp,
                       int64_t revision, int64_t *revisionp,
                       bool timeorder_desc);

    std::shared_ptr<Context> m_context;
    QueryCachePtr m_query_cache;
    TimerHandlerPtr m_timer_handler;
    Mutex                      m_qualify_queue_mutex;
    boost::condition           m_qualify_queue_cond;
    std::list<UpdateContext *> m_qualify_queue;
    Mutex                      m_commit_queue_mutex;
    boost::condition           m_commit_queue_cond;
    int32_t                    m_commit_queue_count {};
    std::list<UpdateContext *> m_commit_queue;
    Mutex                      m_response_queue_mutex;
    boost::condition           m_response_queue_cond;
    std::list<UpdateContext *> m_response_queue;
    std::vector<std::thread>   m_threads;
    int64_t m_last_revision {TIMESTAMP_MIN};
    uint64_t m_update_coalesce_limit {};
    int32_t m_maintenance_pause_interval {};
    uint32_t m_update_delay {};
    int32_t m_max_clock_skew {};
    bool m_shutdown {};
  };

  typedef std::shared_ptr<UpdatePipeline> UpdatePipelinePtr;

}

#endif // Hypertable_RangeServer_UpdatePipeline_h
