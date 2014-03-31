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
/// Declarations for OperationRecreateIndexTables.
/// This file contains declarations for OperationRecreateIndexTables, an
/// Operation class for temporarily suspending maintenance on a table.

#ifndef Hypertable_Master_OperationRecreateIndexTables_h
#define Hypertable_Master_OperationRecreateIndexTables_h

#include "Operation.h"

#include <Hypertable/Lib/TableParts.h>

#include <Common/String.h>

namespace Hypertable {

  /// @addtogroup Master
  /// @{

  /// Temporarily suspends maintenance for a table
  class OperationRecreateIndexTables : public Operation {
  public:

    OperationRecreateIndexTables(ContextPtr &context, std::string table_name);

    /// Constructor for constructing object from %MetaLog entry.
    /// @param context %Master context
    /// @param header %MetaLog header
    OperationRecreateIndexTables(ContextPtr &context,
                                 const MetaLog::EntityHeader &header);

    /// Constructor for constructing object from client request.
    /// Initializes base class constructor, decodes request from
    /// <code>event</code> payload.
    /// @param context %Master context
    /// @param event %Event received from AsyncComm from client request
    OperationRecreateIndexTables(ContextPtr &context, EventPtr &event);

    
    /// Destructor. */
    virtual ~OperationRecreateIndexTables() { }

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

    /// Returns name of operation ("OperationRecreateIndexTables")
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

    virtual void decode_request(const uint8_t **bufp, size_t *remainp);

  private:

    std::string sub_op_dependency_string(Operation *op) {
      return format("RECREATE INDEX TABLES subop %lld", (Lld)op->hash_code());
    }

    std::string sub_op_dependency_string(OperationPtr &op) {
      return sub_op_dependency_string(op.get());
    }

    bool fetch_schema(std::string &schema);

    bool fetch_and_validate_subop(vector<Entity *> &entities);

    void stage_subop(Operation *operation);

    /// Full pathname of table name to compact
    std::string m_table_name;

    TableParts m_table_parts {0};

    int64_t m_subop_hash_code {};
  };

  /// @}

} // namespace Hypertable

#endif // Hypertable_Master_OperationRecreateIndexTables_h
