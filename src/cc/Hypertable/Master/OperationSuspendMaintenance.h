/* -*- c++ -*-
 * Copyright (C) 2007-2014 Hypertable, Inc.
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

/// @file
/// Declarations for OperationSuspendMaintenance.
/// This file contains declarations for OperationSuspendMaintenance, an
/// Operation class for temporarily suspending maintenance on a table.

#ifndef Hypertable_Master_OperationSuspendMaintenance_h
#define Hypertable_Master_OperationSuspendMaintenance_h

#include "Operation.h"

namespace Hypertable {

  /// @addtogroup Master
  /// @{

  /// Temporarily suspends maintenance for a table
  class OperationSuspendMaintenance : public Operation {
  public:

    /// Constructor.
    /// @param context %Master context
    /// @param label Label identifying parent operation
    /// @param table_name Full table pathname
    OperationSuspendMaintenance(ContextPtr &context,
                                const std::string &label,
                                const std::string &table_name);

    /// Constructor for constructing object from %MetaLog entry.
    /// @param context %Master context
    /// @param header %MetaLog header
    OperationSuspendMaintenance(ContextPtr &context,
                                const MetaLog::EntityHeader &header);
    
    /// Destructor. */
    virtual ~OperationSuspendMaintenance() { }

    /// Carries out the manual compaction operation.
    /// This method carries out the operation via the following states:
    ///
    /// <table>
    /// <tr>
    /// <th>State</th>
    /// <th>Description</th>
    /// </tr>
    /// <tr>
    /// <td>INITIAL</td>
    /// <td><ul>
    /// <li>If a table name was supplied, it maps it to a table identifier (#m_id)</li>
    /// <li>If supplied table name not found in name map, completes with
    /// Error::TABLE_NOT_FOUND</li>
    /// <li>Otherwise, sets dependencies to Dependency::METADATA and transitions
    /// to SCAN_METADATA</li>
    /// </ul></td>
    /// </tr>
    /// <tr>
    /// <td>SCAN_METADATA</td>
    /// <td><ul>
    /// <li>Scans the METADATA table and populates #m_servers to hold the set
    /// of servers that hold the table to be compacted which are not in the
    /// #m_completed set.  If no table name was supplied, then #m_servers is
    /// set to all available servers which are not in the #m_completed set</li>
    /// <li>Dependencies are set to server names in #m_servers</li>
    /// <li>Transitions to the ISSUE_REQUESTS state</li>
    /// <li>Persists operation to MML and returns</li>
    /// </ul></td>
    /// </tr>
    /// <tr>
    /// <td>ISSUE_REQUESTS</td>
    /// <td><ul>
    /// <li>Issues a compact request to all servers in #m_servers and waits
    /// for their completion</li>
    /// <li>If there are any errors, for each server that was successful or
    /// returned with Error::TABLE_NOT_FOUND,
    /// the server name is added to #m_completed.  Dependencies are then set back
    /// to just Dependency::METADATA, the state is reset back to SCAN_METADATA,
    /// the operation is persisted to the MML, and the method returns</li>
    /// <li>Otherwise state is transitioned to COMPLETED</li>
    /// </ul></td>
    /// </tr>
    /// </table>

    virtual void execute();

    /// Returns name of operation ("OperationSuspendMaintenance")
    /// @return %Operation name

    virtual const String name();

    /// Returns descriptive label for operation
    /// @return Descriptive label for operation

    virtual const String label();

    /// Writes human readable representation of object to output stream.
    /// @param os Output stream

    virtual void display_state(std::ostream &os);

    virtual uint16_t encoding_version() const;

    /// Returns serialized state length.
    /// This method returns the length of the serialized representation of the
    /// object state.  See encode() for a description of the serialized format.
    /// @return Serialized length

    virtual size_t encoded_state_length() const;

    /// Writes serialized encoding of object state.
    /// This method writes a serialized encoding of object state to the memory
    /// location pointed to by <code>*bufp</code>.  The encoding has the
    /// following format:
    /// <table>
    ///   <tr>
    ///   <th>Encoding</th><th>Description</th>
    ///   </tr>
    ///   <tr>
    ///   <td>vstr</td><td>Pathname of table to compact</td>
    ///   </tr>
    ///   <tr>
    ///   <td>vstr</td><td>Row key of range to compact</td>
    ///   </tr>
    ///   <tr>
    ///   <td>i32</td><td>Compaction flags (see RangeServerProtocol::CompactionFlags)</td>
    ///   </tr>
    ///   <tr>
    ///   <td>vstr</td><td>%Table identifier</td>
    ///   </tr>
    ///   <tr>
    ///   <td>i32</td><td>Size of #m_completed</td>
    ///   </tr>
    ///   <tr>
    ///   <td>vstr</td><td><b>Foreach server</b> in #m_completed, server name</td>
    ///   </tr>
    ///   <tr>
    ///   <td>i32</td><td>[VERSION 2] Size of #m_servers</td>
    ///   </tr>
    ///   <tr>
    ///   <td>vstr</td><td>[VERSION 2] <b>Foreach server</b> in #m_servers, server name</td>
    ///   </tr>
    /// </table>
    /// @param bufp Address of destination buffer pointer (advanced by call)
    virtual void encode_state(uint8_t **bufp) const;

    /// Reads serialized encoding of object state.
    /// This method restores the state of the object by decoding a serialized
    /// representation of the state from the memory location pointed to by
    /// <code>*bufp</code>.  See encode() for a description of the
    /// serialized format.
    /// @param bufp Address of source buffer pointer (advanced by call)
    /// @param remainp Amount of remaining buffer pointed to by <code>*bufp</code>
    /// (decremented by call)
    virtual void decode_state(const uint8_t **bufp, size_t *remainp);

    std::string suspend_dependency() {
      return std::string("SUSPEND MAINTENANCE ") + m_table_name + " " + m_label;
    }

    std::string critical_section_dependency() {
      return std::string("CRITICAL SECTION ") + m_table_name + " " + m_label;
    }

    std::string resume_dependency() {
      return std::string("RESUME MAINTENANCE ") + m_table_name + " " + m_label;
    }

  private:

    void prepare_for_metadata_scan(int state);

    void populate_servers_for_table();

    bool issue_rangeserver_requests(bool enable);

    /// Label identifying parent operation
    std::string m_label;

    /// Full pathname of table name to compact
    std::string m_table_name;

    /// Full pathname of table ID to compact
    std::string m_table_id;

    /// Set of range servers participating in suspend/resume
    std::set<std::string> m_servers;

    /// Set of range servers that have completed suspend/resume
    std::set<std::string> m_completed;
  };

  /// @}

} // namespace Hypertable

#endif // Hypertable_Master_OperationSuspendMaintenance_h
