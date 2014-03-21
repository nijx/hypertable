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
/// Declarations for OperationCreateTable.
/// This file contains declarations for OperationCreateTable, an Operation class
/// for creating a table.

#ifndef HYPERTABLE_OPERATIONCREATETABLE_H
#define HYPERTABLE_OPERATIONCREATETABLE_H

#include <Hypertable/Master/Operation.h>

#include <Hypertable/Lib/TableParts.h>

namespace Hypertable {

  /// @addtogroup Master
  /// @{

  /// Carries out a <i>create table</i> operation.
  /// This class is responsible for creating a new table, which involves,
  /// creating the table in Hyperspace, loading the initial range for the table,
  /// and optionally creating the associated value and qualifier index tables if
  /// required.
  class OperationCreateTable : public Operation {
  public:

    /// Constructor.
    /// Initializes member variables and then calls initialize_dependencies()
    /// @param context %Master context
    /// @param name Full pathname of table to create
    /// @param schema %Table schema
    /// @param parts Which parts of the table to create
    OperationCreateTable(ContextPtr &context, const String &name,
                         const String &schema, TableParts parts);

    /// Constructor for constructing object from %MetaLog entry.
    /// @param context %Master context
    /// @param header %MetaLog header
    OperationCreateTable(ContextPtr &context,
                         const MetaLog::EntityHeader &header) :
      Operation(context, header) { }

    /// Constructor for constructing object from client request.
    /// Initializes base class constructor, decodes request from
    /// <code>event</code> payload, and then calls initialize_dependencies().
    /// @param context %Master context
    /// @param event %Event received from AsyncComm from client request
    OperationCreateTable(ContextPtr &context, EventPtr &event);

    /// Destructor.
    virtual ~OperationCreateTable() { }

    virtual void execute();

    /// Returns name of operation
    /// Returns name of operation (<code>OperationCreateTable</code>)
    /// @return Name of operation
    virtual const String name();

    /// Returns label for operation
    /// Returns string "CreateTable <tablename>" 
    /// Label for operation
    virtual const String label();

    
    virtual void display_state(std::ostream &os);
    virtual uint16_t encoding_version() const;
    virtual size_t encoded_state_length() const;
    virtual void encode_state(uint8_t **bufp) const;
    virtual void decode_state(const uint8_t **bufp, size_t *remainp);
    virtual void decode_request(const uint8_t **bufp, size_t *remainp);

  private:
    void initialize_dependencies();

    void requires_indices(bool &needs_index, bool &needs_qualifier_index);

    /// Pathtname of table to create
    String m_name;

    /// %Schema for the table
    String m_schema;

    /// %Table identifier
    TableIdentifierManaged m_table;

    /// %Proxy name of server to hold initial range
    String m_location;

    /// Which parts of table to create
    TableParts m_parts {TableParts::ALL};
  };

  /// @}

} // namespace Hypertable

#endif // HYPERTABLE_OPERATIONCREATETABLE_H
