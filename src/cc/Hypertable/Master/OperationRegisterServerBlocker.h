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
 * Declarations for OperationRegisterServerBlocker.
 * This file contains declarations for OperationRegisterServerBlocker, an
 * Operation class for blocking server registration until a lock
 * release notificaiton for the server's file has been delivered from
 * Hyperspace.
 */

#ifndef HYPERTABLE_OPERATIONRECOVERYBLOCKER_H
#define HYPERTABLE_OPERATIONRECOVERYBLOCKER_H

#include "OperationEphemeral.h"

namespace Hypertable {

  /** @addtogroup Master
   *  @{
   */

  /** Blocks range server registration. */
  class OperationRegisterServerBlocker : public OperationEphemeral {
  public:
    
    OperationRegisterServerBlocker(ContextPtr &context, const String &location);

    /** Destructor. */
    virtual ~OperationRegisterServerBlocker() { }

    virtual void execute();
    virtual const String name();
    virtual const String label();

    virtual void display_state(std::ostream &os) { }
    virtual void decode_request(const uint8_t **bufp, size_t *remainp) { }

  private:
    /// 
    String m_location;
  };

  /// Smart pointer to OperationRegisterServerBlocker
  typedef intrusive_ptr<OperationRegisterServerBlocker> OperationRegisterServerBlockerPtr;

  /** @} */

} // namespace Hypertable

#endif // HYPERTABLE_OPERATIONRECOVERYBLOCKER_H
