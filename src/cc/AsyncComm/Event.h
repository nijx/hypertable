/* -*- c++ -*-
 * Copyright (C) 2007-2013 Hypertable, Inc.
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

/** @file
 * Declarations for Event.
 * This file contains type declarations for Event, a class for representing
 * a network communication event.
 */

#ifndef HYPERTABLE_EVENT_H
#define HYPERTABLE_EVENT_H

#include <iostream>
#include <time.h>

#include "Common/Error.h"
#include "Common/InetAddr.h"
#include "Common/String.h"
#include "Common/ReferenceCount.h"
#include "Common/Time.h"

#include "CommHeader.h"

namespace Hypertable {

  /** @addtogroup AsyncComm
   *  @{
   */

  /**
   * Network communication event.
   * Objects of this type get passed up to the application through dispatch
   * handlers (see DispatchHandler).
   */
  class Event : public ReferenceCount {

  public:

    /** Enumeration for event types.
     */
    enum Type { 
      CONNECTION_ESTABLISHED, ///< Connection established event
      DISCONNECT, ///< Connection disconnected event
      MESSAGE,    ///< Request/response message event
      ERROR,      ///< %Error event
      TIMER       ///< %Timer event
    };

    /** Constructor initializing with InetAddr.
     *
     * @param type_ Type of event
     * @param addr_ Remote address from which event originated
     * @param error_ Error code associated with this event
     */
    Event(Type type_, const InetAddr &addr_, int error_=Error::OK)
      : type(type_), addr(addr_), proxy_buf(0), error(error_), payload(0),
        payload_len(0), payload_aligned(false), group_id(0), arrival_time(0) {
      proxy = 0;
    }

    /** Constructor initializing with InetAddr and proxy name.
     *
     * @param type_ Type of event
     * @param addr_ Remote address from which event originated
     * @param proxy_ Proxy name
     * @param error_ Error code associated with this event
     */
    Event(Type type_, const sockaddr_in &addr_, const String &proxy_,
          int error_=Error::OK) 
      : type(type_), addr(addr_), proxy_buf(0), error(error_), payload(0),
        payload_len(0), payload_aligned(false), group_id(0), arrival_time(0) {
      set_proxy(proxy_);
    }

    /** Constructor initializing with empty address.
     *
     * @param type_ Type of event
     * @param error_ Error code associated with this event
     */
    Event(Type type_, int error_=Error::OK) 
      : type(type_), proxy_buf(0), error(error_), payload(0), payload_len(0),
        payload_aligned(false), group_id(0), arrival_time(0) {
      proxy = 0;
    }

    /** Constructor initialized with proxy name.
     *
     * @param type_ Type of event
     * @param proxy_ Proxy name
     * @param error_ Error code associated with this event
     */
    Event(Type type_, const String &proxy_, int error_=0) 
      : type(type_), proxy_buf(0), error(error_), payload(0), payload_len(0),
        payload_aligned(false), group_id(0), arrival_time(0) {
      set_proxy(proxy_);
    }

    /** Destructor.  Deallocates message payload buffer and proxy name buffer
     */
    ~Event() {
      if (payload_aligned)
        free((void *)payload);
      else
        delete [] payload;
      if (proxy_buf != proxy_buf_static)
        delete [] proxy_buf;
    }

    /** Loads header object from serialized message buffer.  This method
     * also sets the group_id member.
     *
     * @param buf Buffer containing serialized header
     * @param len Length of buffer
     */
    void load_message_header(const uint8_t *buf, size_t len) {
      header.decode(&buf, &len);
      group_id = header.gid;
    }

    /** Sets the address proxy name from which this event was generated.
     * If the proxy name is less than 32 characters, then it is copied into
     * #proxy_buf_static, otherwise a buffer is allocated to hold it.  #proxy
     * is set to point to whichever buffer is chosen.
     */
    void set_proxy(const String &p) {
      if (p.length() == 0)
	proxy = 0;
      else {
	if (p.length() < 32)
	  proxy = proxy_buf_static;
	else {
	  proxy_buf = new char [ p.length() + 1 ];
	  proxy = proxy_buf;
	}
	strcpy((char *)proxy, p.c_str());
      }
    }

    /** Expiration time of request message from which this Event was
     * initialized.
     * @return Absolute expiration time
     */
    boost::xtime expiration_time() {
      boost::xtime expire_time;
      boost::xtime_get(&expire_time, boost::TIME_UTC_);
      expire_time.sec += header.timeout_ms/1000;
      return expire_time;
    }

    /** Type of event.  Can take one of values CONNECTION_ESTABLISHED,
     * DISCONNECT, MESSAGE, ERROR, or TIMER
     */
    Type type;

    /// Remote address from which event was generated.
    InetAddr addr;

    /// Address proxy name
    const char *proxy;

    /// Pointer to allocated proxy name buffer
    char *proxy_buf;

    /// Static proxy name buffer
    char proxy_buf_static[32];

    /// Local address to which event was delivered.
    InetAddr local_addr;

    /** Error code associated with this event.  DISCONNECT and
     * ERROR events set this value
     */
    int error;

    /// Comm layer header for MESSAGE events
    CommHeader header;

    /// Points to a buffer containing the message payload
    const uint8_t *payload;

    /// Length of the message
    size_t payload_len;

    /// Flag indicating if payload was allocated with posix_memalign
    bool payload_aligned;

    /** Thread group to which this message belongs.  Used to serialize
     * messages destined for the same object.  This value is created in
     * the constructor and is the combination of the socked descriptor from
     * which the message was read and the gid field in the message header:
     * <pre>
     * group_id = ((uint64_t)sd << 32) | header->gid;
     * </pre>
     * If the gid is zero, then the group_id member is also set to zero
     */
    uint64_t group_id;

    /// time (seconds since epoch) when message arrived
    time_t arrival_time;

    /** Generates a one-line string representation of the event.  For example:
     * <pre>
     *   Event: type=MESSAGE id=2 gid=0 header_len=16 total_len=20 \
     *   from=127.0.0.1:15861 ...
     * </pre>
     */
    String to_str() const;

    /** Displays a one-line string representation of the event to stdout.
     * @see to_str
     */
    void display() { std::cerr << to_str() << std::endl; }
  };

  /// Smart pointer to Event
  typedef boost::intrusive_ptr<Event> EventPtr;
  /** @}*/
} // namespace Hypertable

#endif // HYPERTABLE_EVENT_H
