/* -*- c++ -*-
 * Copyright (C) 2007-2013 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 3 of the
 * License, or any later version.
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

/** @file
 * Declarations for RangeServerHyperspaceCallback.
 * This file contains declarations for RangeServerHyperspaceCallback, a callback
 * class registered with Hyperspace on range server lock files.
 */

#ifndef HYPERTABLE_RANGESERVERHYPERSPACECALLBACK_H
#define HYPERTABLE_RANGESERVERHYPERSPACECALLBACK_H

#include "Hyperspace/Session.h"

#include "Context.h"
#include "OperationProcessor.h"
#include "OperationRecover.h"
#include "OperationTimedBarrier.h"
#include "OperationRegisterServerBlocker.h"
#include "RangeServerConnection.h"

namespace Hypertable {

  /** @addtogroup Master
   *  @{
   */

  /** Callback class for range server lock files.
   * An object of this class is registered with each range server lock file in
   * Hyperspace and is used to handle the disconnect and reconnect of a range
   * server.  It specifically takes measures to ensure that a range server
   * does not get re-registered until it has received a <i>lock released</i>
   * event for the server from Hyperspace.
   */
  class RangeServerHyperspaceCallback : public Hyperspace::HandleCallback {
  public:

    /** Constructor.
     * Initializes object by passing Hyperspace::EVENT_MASK_LOCK_ACQUIRED and
     * Hyperspace::EVENT_MASK_LOCK_RELEASED into the parent constructor.
     * @param context %Master context
     * @param rsc %Range server connection object
     */
    RangeServerHyperspaceCallback(ContextPtr &context, RangeServerConnectionPtr &rsc)
      : Hyperspace::HandleCallback(Hyperspace::EVENT_MASK_LOCK_ACQUIRED|
                                   Hyperspace::EVENT_MASK_LOCK_RELEASED),
      m_context(context), m_rsc(rsc) { }

    /** Responds to lock release event.
     * This method performs the following actions:
     *   1. Removes the server from list of available servers with a call to
     *      Context::remove_available_server()
     *   2. If the server is connected, it advances the recovery barrier
     *      (<code>m_context->recovery_barrier_op</code> to
     *      <code>Hypertable.Failover.GracePeriod</code> milliseconds in the
     *      future and then creates an OperationRecover for the server and
     *      adds it to the OperationProcessor.
     *   3. Unblocks operations with obstruction label "<server-name> register"
     *      to allow any pending register server operation for this server to
     *      proceed.
     */
    virtual void lock_released() {
      HT_INFOF("%s hyperspace lock file released", m_rsc->location().c_str());
      m_context->remove_available_server(m_rsc->location());
      if (m_rsc->connected()) {
        uint32_t millis = m_context->props->get_i32("Hypertable.Failover.GracePeriod");
        m_context->recovery_barrier_op->advance_into_future(millis);
        OperationPtr operation = new OperationRecover(m_context, m_rsc);
        try {
          m_context->op->add_operation(operation);
        }
        catch (Exception &e) {
          HT_INFOF("%s - %s", Error::get_text(e.code()), e.what());
        }
      }
      m_context->op->unblock(String("RegisterServerBlocker ") + m_rsc->location());
    }

    /** Responds to lock acquired event.
     * This method will be called when the range server identified by #m_rsc
     * acquires the lock on its Hyperspace lock file.  It exists to avoid a race
     * condition where a range server 1) dies and 2) restarts triggering a new
     * register server operation, <b>before</b> the <i>lock released</i> event
     * is delivered for #1.  It handles this by creating an
     * OperationRegisterServerBlocker for the server which prevents it from
     * executing until the <i>lock released</i> event is received.
     * @param mode the mode in which the lock was acquired
     */
    virtual void lock_acquired(uint32_t mode) {
      HT_INFOF("%s hyperspace lock file acquired", m_rsc->location().c_str());
      OperationPtr op =
        new OperationRegisterServerBlocker(m_context, m_rsc->location());
      try {
        m_context->op->add_operation(op);
      }
      catch (Exception &e) {
        HT_FATALF("%s - %s", Error::get_text(e.code()), e.what());
      }
    }

  private:

    /// %Master context
    ContextPtr m_context;

    /// %Range server connection
    RangeServerConnectionPtr m_rsc;
  };

  /** @} */
}

#endif // HYPERTABLE_RANGESERVERHYPERSPACECALLBACK_H
