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

#ifndef HYPERTABLE_REQUESTHANDLERSTATUS_H
#define HYPERTABLE_REQUESTHANDLERSTATUS_H

#include "AsyncComm/ApplicationHandler.h"
#include "AsyncComm/Comm.h"
#include "AsyncComm/Event.h"

#include "Broker.h"


namespace Hypertable {

  namespace FsBroker {

    class RequestHandlerStatus : public ApplicationHandler {
    public:
      RequestHandlerStatus(Comm *comm, Broker *broker, EventPtr &event_ptr)
        : ApplicationHandler(event_ptr), m_comm(comm), m_broker(broker) { }

      virtual void run();

    private:
      Comm   *m_comm;
      Broker *m_broker;
    };

  }

}

#endif // HYPERTABLE_REQUESTHANDLERSTATUS_H
