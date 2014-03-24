/*
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
/// Definitions for OperationRecreateIndexTables.
/// This file contains definitions for OperationRecreateIndexTables, an Operation
/// class for recreating index tables.

#include <Common/Compat.h>
#include "OperationRecreateIndexTables.h"

#include <Hypertable/Master/OperationCreateTable.h>
#include <Hypertable/Master/OperationDropTable.h>
#include <Hypertable/Master/OperationToggleTableMaintenance.h>
#include <Hypertable/Master/ReferenceManager.h>
#include <Hypertable/Master/Utility.h>

#include <Hypertable/Lib/Key.h>

#include <Hyperspace/Session.h>

#include <Common/Error.h>
#include <Common/FailureInducer.h>
#include <Common/ScopeGuard.h>
#include <Common/Serialization.h>

#include <boost/algorithm/string.hpp>

#include <poll.h>

using namespace Hypertable;
using namespace Hyperspace;
using namespace std;

OperationRecreateIndexTables::OperationRecreateIndexTables(ContextPtr &context,
                                                       std::string table_name) :
  Operation(context, MetaLog::EntityType::OPERATION_RECREATE_INDEX_TABLES),
  m_table_name(table_name) {
  Utility::canonicalize_pathname(m_table_name);
  m_exclusivities.insert(m_table_name);
}


OperationRecreateIndexTables::OperationRecreateIndexTables(ContextPtr &context,
                                                           const MetaLog::EntityHeader &header)
  : Operation(context, header) {
}

OperationRecreateIndexTables::OperationRecreateIndexTables(ContextPtr &context, EventPtr &event)
  : Operation(context, event, MetaLog::EntityType::OPERATION_RECREATE_INDEX_TABLES) {
  const uint8_t *ptr = event->payload;
  size_t remaining = event->payload_len;
  decode_request(&ptr, &remaining);
  m_exclusivities.insert(m_table_name);
}

void OperationRecreateIndexTables::execute() {
  std::vector<Entity *> entities;
  OperationPtr sub_op;
  Operation *op;
  int32_t state = get_state();

  HT_INFOF("Entering RecreateIndexTables-%lld (table=%s, parts=%s) state=%s",
           (Lld)header.id, m_table_name.c_str(),
           m_table_parts.to_string().c_str(), OperationState::get_text(state));

  if (m_subop_hash_code)
    sub_op = m_context->reference_manager->get(m_subop_hash_code);

  switch (state) {

  case OperationState::INITIAL:
    {
      string schema_str;
      if (!fetch_schema(schema_str))
        break;
      SchemaPtr schema = Schema::new_instance(schema_str);
      if (!schema->is_valid()) {
        complete_error(Error::SCHEMA_PARSE_ERROR, schema->get_error_string());
        break;
      }
      uint8_t parts = 0;
      for (auto cf : schema->get_column_families()) {
        if (cf->has_index)
          parts |= TableParts::VALUE_INDEX;
        if (cf->has_qualifier_index)
          parts |= TableParts::QUALIFIER_INDEX;
      }
      if (parts == 0) {
        complete_error(Error::TABLE_DOES_NOT_HAVE_INDICES, m_table_name);
        break;
      }
      m_table_parts = TableParts(parts);
    }
    set_state(OperationState::SUSPEND_TABLE_MAINTENANCE);
    m_context->mml_writer->record_state(this);
    HT_MAYBE_FAIL("recreate-index-tables-INITIAL");

    // drop through ...

  case OperationState::SUSPEND_TABLE_MAINTENANCE:
    op = new OperationToggleTableMaintenance(m_context, m_table_name,
                                             TableMaintenance::OFF);
    op->set_remove_approval_mask(0x01);
    m_context->reference_manager->add(op);
    {
      ScopedLock lock(m_mutex);
      op->add_obstruction(sub_op_dependency_string(op));
      m_dependencies.insert(sub_op_dependency_string(op));
      m_subop_hash_code = op->hash_code();
      m_state = OperationState::DROP_INDICES;
      m_sub_ops.push_back(op);
    }
    entities.push_back(this);
    entities.push_back(op);
    m_context->mml_writer->record_state(entities);
    break;

  case OperationState::DROP_INDICES:
    sub_op->mark_for_removal();
    if (sub_op->get_error()) {
      complete_error(sub_op->get_error(), sub_op->get_error_msg(), sub_op.get());
      m_context->reference_manager->remove(sub_op);
      break;
    }
    op = new OperationDropTable(m_context, m_table_name, true, m_table_parts);
    op->set_remove_approval_mask(0x01);
    m_context->reference_manager->add(op);
    {
      ScopedLock lock(m_mutex);
      op->add_obstruction(sub_op_dependency_string(op));
      m_dependencies.insert(sub_op_dependency_string(op));
      m_dependencies.erase(sub_op_dependency_string(sub_op));
      m_subop_hash_code = op->hash_code();
      m_state = OperationState::CREATE_INDICES;
      m_sub_ops.push_back(op);
    }
    entities.push_back(this);
    entities.push_back(op);
    entities.push_back(sub_op.get());
    m_context->mml_writer->record_state(entities);
    m_context->reference_manager->remove(sub_op);
    break;

  case OperationState::CREATE_INDICES:
    sub_op->mark_for_removal();
    if (sub_op->get_error()) {
      complete_error(sub_op->get_error(), sub_op->get_error_msg(), sub_op.get());
      m_context->reference_manager->remove(sub_op);
      break;
    }
    {
      string schema;
      if (!fetch_schema(schema))
        break;
      op = new OperationCreateTable(m_context, m_table_name, schema, m_table_parts);
    }
    op->set_remove_approval_mask(0x01);
    m_context->reference_manager->add(op);
    {
      ScopedLock lock(m_mutex);
      op->add_obstruction(sub_op_dependency_string(op));
      m_dependencies.insert(sub_op_dependency_string(op));
      m_dependencies.erase(sub_op_dependency_string(sub_op));
      m_subop_hash_code = op->hash_code();
      m_state = OperationState::RESUME_TABLE_MAINTENANCE;
      m_sub_ops.push_back(op);
    }
    entities.push_back(this);
    entities.push_back(op);
    entities.push_back(sub_op.get());
    m_context->mml_writer->record_state(entities);
    m_context->reference_manager->remove(sub_op);
    break;

  case OperationState::RESUME_TABLE_MAINTENANCE:
    sub_op->mark_for_removal();
    if (sub_op->get_error()) {
      complete_error(sub_op->get_error(), sub_op->get_error_msg(), sub_op.get());
      m_context->reference_manager->remove(sub_op);
      break;
    }
    op = new OperationToggleTableMaintenance(m_context, m_table_name,
                                             TableMaintenance::ON);
    op->set_remove_approval_mask(0x01);
    m_context->reference_manager->add(op);
    {
      ScopedLock lock(m_mutex);
      op->add_obstruction(sub_op_dependency_string(op));
      m_dependencies.insert(sub_op_dependency_string(op));
      m_dependencies.erase(sub_op_dependency_string(sub_op));
      m_subop_hash_code = op->hash_code();
      m_state = OperationState::FINALIZE;
      m_sub_ops.push_back(op);
    }
    entities.push_back(this);
    entities.push_back(op);
    entities.push_back(sub_op.get());
    m_context->mml_writer->record_state(entities);
    m_context->reference_manager->remove(sub_op);
    break;

  case OperationState::FINALIZE:
    sub_op->mark_for_removal();
    if (sub_op->get_error()) {
      complete_error(sub_op->get_error(), sub_op->get_error_msg(), sub_op.get());
      m_context->reference_manager->remove(sub_op);
      break;
    }
    complete_ok(sub_op.get());
    m_context->reference_manager->remove(sub_op);
    break;

  default:
    HT_FATALF("Unrecognized state %d", state);
  }

  HT_INFOF("Leaving RecreateIndexTables-%lld (table=%s, parts=%s) state=%s",
           (Lld)header.id, m_table_name.c_str(),
           m_table_parts.to_string().c_str(), OperationState::get_text(state));
}

void OperationRecreateIndexTables::display_state(std::ostream &os) {
  os << " table_name=" << m_table_name
     << " parts=" << m_table_parts.to_string()
     << " sub_op="  << m_subop_hash_code;
}

#define OPERATION_RECREATE_INDEX_TABLES_VERSION 1

uint16_t OperationRecreateIndexTables::encoding_version() const {
  return OPERATION_RECREATE_INDEX_TABLES_VERSION;
}

size_t OperationRecreateIndexTables::encoded_state_length() const {
  return Serialization::encoded_length_vstr(m_table_name) +
    m_table_parts.encoded_length() + 8;
}

void OperationRecreateIndexTables::encode_state(uint8_t **bufp) const {
  Serialization::encode_vstr(bufp, m_table_name);
  m_table_parts.encode(bufp);
  Serialization::encode_i64(bufp, m_subop_hash_code);
}

void OperationRecreateIndexTables::decode_state(const uint8_t **bufp, size_t *remainp) {
  m_table_name = Serialization::decode_vstr(bufp, remainp);
  m_table_parts.decode(bufp, remainp);
  m_subop_hash_code = Serialization::decode_i64(bufp, remainp);
}

void OperationRecreateIndexTables::decode_request(const uint8_t **bufp, size_t *remainp) {
  m_table_name = Serialization::decode_vstr(bufp, remainp);
  Utility::canonicalize_pathname(m_table_name);
}

const String OperationRecreateIndexTables::name() {
  return "OperationRecreateIndexTables";
}

const String OperationRecreateIndexTables::label() {
  return format("Recreate Index Tables (table=%s, parts=%s)",
                m_table_name.c_str(), m_table_parts.to_string().c_str());
}

bool OperationRecreateIndexTables::fetch_schema(std::string &schema) {
  string table_id;
  bool is_namespace;
  if (m_context->namemap->name_to_id(m_table_name, table_id, &is_namespace)) {
    if (is_namespace) {
      complete_error(Error::TABLE_NOT_FOUND, format("%s is a namespace", m_table_name.c_str()));
      return false;
    }
  }
  else {
    complete_error(Error::TABLE_NOT_FOUND, m_table_name);
    return false;
  }
  DynamicBuffer value_buf;
  string filename = m_context->toplevel_dir + "/tables/" + table_id;
  m_context->hyperspace->attr_get(filename, "schema", value_buf);
  schema = string((char *)value_buf.base, strlen((char *)value_buf.base));
  return true;
}
