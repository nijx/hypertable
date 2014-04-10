/**
 * Copyright (C) 2007-2012 Hypertable, Inc.
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
#ifndef HYPERTABLE_CLIENTBUFFEREDREADERHANDLER_H
#define HYPERTABLE_CLIENTBUFFEREDREADERHANDLER_H

#include <queue>

#include <boost/thread/condition.hpp>

#include "Common/Mutex.h"
#include "Common/String.h"
#include "AsyncComm/DispatchHandler.h"

namespace Hypertable {

  namespace FsBroker {
    class Client;
  }

  class ClientBufferedReaderHandler : public DispatchHandler {

  public:
    ClientBufferedReaderHandler(FsBroker::Client *client, uint32_t fd,
        uint32_t buf_size, uint32_t outstanding, uint64_t start_offset,
        uint64_t end_offset);

    virtual ~ClientBufferedReaderHandler();

    virtual void handle(EventPtr &event_ptr);

    size_t read(void *buf, size_t len);

  private:

    void read_ahead();

    Mutex                m_mutex;
    boost::condition     m_cond;
    std::queue<EventPtr> m_queue;
    FsBroker::Client   *m_client;
    uint32_t             m_fd;
    uint32_t             m_max_outstanding;
    uint32_t             m_read_size;
    uint32_t             m_outstanding;
    bool                 m_eof;
    int                  m_error;
    std::string          m_error_msg;
    uint8_t             *m_ptr;
    uint8_t             *m_end_ptr;
    uint64_t             m_end_offset;
    uint64_t             m_outstanding_offset;
    uint64_t             m_actual_offset;
  };

}

#endif // HYPERTABLE_CLIENTBUFFEREDREADERHANDLER_H

