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
/// Definitions for OperationSuspendMaintenance.
/// This file contains definitions for OperationSuspendMaintenance, an Operation
/// class for temporarily suspending maintenance on a table.

#include <Common/Compat.h>
#include "OperationSuspendMaintenance.h"

#include <Hypertable/Master/DispatchHandlerOperationSuspendMaintenance.h>
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

OperationSuspendMaintenance::OperationSuspendMaintenance(ContextPtr &context,
                                                         const std::string &label,
                                                         const std::string &table_name)
  : Operation(context, MetaLog::EntityType::OPERATION_SUSPEND_MAINTENANCE),
    m_label(label), m_table_name(table_name) {
  Utility::canonicalize_pathname(m_table_name);
  add_dependency(Dependency::INIT);
  add_obstruction(suspend_dependency());
  add_exclusivity(m_table_name);
}

OperationSuspendMaintenance::OperationSuspendMaintenance(ContextPtr &context,
                                   const MetaLog::EntityHeader &header)
  : Operation(context, header) {
}


void OperationSuspendMaintenance::execute() {
  bool is_namespace;
  int32_t state = get_state();

  HT_INFOF("Entering SuspendMaintenance-%lld (label=%s, table=%s) state=%s",
           (Lld)header.id, m_label.c_str(), m_table_name.c_str(),
           OperationState::get_text(state));

  switch (state) {

  case OperationState::INITIAL:
    if (m_context->namemap->name_to_id(m_table_name, m_table_id, &is_namespace)) {
      if (is_namespace) {
        complete_error(Error::TABLE_NOT_FOUND, format("%s is a namespace", m_table_name.c_str()));
        break;
      }
    }
    else {
      complete_error(Error::TABLE_NOT_FOUND, m_table_name);
      break;
    }
    set_state(OperationState::SUSPEND_MAINTENANCE_HYPERSPACE);
    m_context->mml_writer->record_state(this);
    HT_MAYBE_FAIL("suspend-maintenance-INITIAL");

    // drop through ...

  case OperationState::SUSPEND_MAINTENANCE_HYPERSPACE:
    try {
      String tablefile = m_context->toplevel_dir + "/tables/" + m_table_id;
      m_context->hyperspace->attr_set(tablefile, "maintenance_disabled", "1", 1);
    }
    catch (Exception &e) {
      HT_ERRORF("Problem setting 'maintenance_disabled' attr for %s (%s, %s)",
                m_table_id.c_str(), Error::get_text(e.code()), e.what());
      complete_error(e);
      break;
    }
    prepare_for_metadata_scan(OperationState::SUSPEND_SCAN_METADATA);
    HT_MAYBE_FAIL("suspend-maintenance-SUSPEND_MAINTENANCE_HYPERSPACE");
    break;

  case OperationState::SUSPEND_SCAN_METADATA:
    populate_servers_for_table();
    set_state(OperationState::SUSPEND_MAINTENANCE_RANGESERVER);
    m_context->mml_writer->record_state(this);
    HT_MAYBE_FAIL("suspend-maintenance-SUSPEND_SCAN_METADATA");
    break;

  case OperationState::SUSPEND_MAINTENANCE_RANGESERVER:
    if (!issue_rangeserver_requests(true)) {
      poll(0, 0, 5000);
      set_state(OperationState::SUSPEND_SCAN_METADATA);
      m_context->mml_writer->record_state(this);
      break;
    }
    {
      ScopedLock lock(m_mutex);
      m_dependencies.clear();
      m_dependencies.insert(critical_section_dependency());
      m_obstructions.clear();
      m_obstructions.insert(resume_dependency());
      m_state = OperationState::RESUME_MAINTENANCE_HYPERSPACE;
    }
    m_context->mml_writer->record_state(this);
    HT_MAYBE_FAIL("suspend-maintenance-SUSPEND_MAINTENANCE_RANGESERVER");
    break;

  case OperationState::RESUME_MAINTENANCE_HYPERSPACE:
    try {
      String tablefile = m_context->toplevel_dir + "/tables/" + m_table_id;
      uint64_t handle = 0;
      HT_ON_SCOPE_EXIT(&Hyperspace::close_handle_ptr, m_context->hyperspace, &handle);
      handle = m_context->hyperspace->open(tablefile, OPEN_FLAG_READ);
      m_context->hyperspace->attr_del(handle, "maintenance_disabled");
    }
    catch (Exception &e) {
      HT_ERRORF("Problem deleting 'maintenance_disabled' attr for %s (%s, %s)",
                m_table_id.c_str(), Error::get_text(e.code()), e.what());
      complete_error(e);
      break;
    }
    prepare_for_metadata_scan(OperationState::RESUME_SCAN_METADATA);
    HT_MAYBE_FAIL("suspend-maintenance-RESUME_MAINTENANCE_HYPERSPACE");
    break;

  case OperationState::RESUME_SCAN_METADATA:
    populate_servers_for_table();
    set_state(OperationState::RESUME_MAINTENANCE_RANGESERVER);
    m_context->mml_writer->record_state(this);
    HT_MAYBE_FAIL("suspend-maintenance-RESUME_SCAN_METADATA");
    break;

  case OperationState::RESUME_MAINTENANCE_RANGESERVER:
    if (!issue_rangeserver_requests(true)) {
      poll(0, 0, 5000);
      set_state(OperationState::RESUME_SCAN_METADATA);
      m_context->mml_writer->record_state(this);
      break;
    }
    HT_MAYBE_FAIL("suspend-maintenance-RESUME_MAINTENANCE_RANGESERVER");
    complete_ok();
    break;

  default:
    HT_FATALF("Unrecognized state %d", state);
  }

  HT_INFOF("Leaving SuspendMaintenance-%lld (label=%s, table=%s) state=%s",
           (Lld)header.id, m_label.c_str(), m_table_name.c_str(),
           OperationState::get_text(state));

}

void OperationSuspendMaintenance::prepare_for_metadata_scan(int state) {
  ScopedLock lock(m_mutex);
  m_dependencies.clear();
  m_dependencies.insert(Dependency::METADATA);
  m_dependencies.insert(m_table_id + " move range");
  m_state = state;
}

void OperationSuspendMaintenance::populate_servers_for_table() {
  std::set<std::string> servers;
  if (!m_context->test_mode)
    Utility::get_table_server_set(m_context, m_table_id, "", servers);
  else
    m_context->get_available_servers(servers);
  {
    ScopedLock lock(m_mutex);
    m_servers.clear();
    m_dependencies.clear();
    for (auto &server : servers) {
      if (m_completed.count(server) == 0) {
        m_dependencies.insert(server);
        m_servers.insert(server);
      }
    }
  }
}

bool OperationSuspendMaintenance::issue_rangeserver_requests(bool enable) {

  if (m_context->test_mode)
    return true;

  TableIdentifier table;
  table.id = m_table_id.c_str();
  table.generation = 0;
  DispatchHandlerOperationPtr op_handler =
    new DispatchHandlerOperationSuspendMaintenance(m_context, table, enable);
  op_handler->start(m_servers);
  if (!op_handler->wait_for_completion()) {
    std::set<DispatchHandlerOperation::Result> results;
    op_handler->get_results(results);
    for (auto &result : results) {
      if (result.error == Error::OK) {
        ScopedLock lock(m_mutex);
        m_completed.insert(result.location);
      }
      else
        HT_WARNF("Error at %s - %s (%s)", result.location.c_str(),
                 Error::get_text(result.error), result.msg.c_str());
    }

    {
      ScopedLock lock(m_mutex);
      m_servers.clear();
      m_dependencies.clear();
      m_dependencies.insert(Dependency::METADATA);
    }
    return false;
  }
  return true;
}


void OperationSuspendMaintenance::display_state(std::ostream &os) {
  os << " table_name=" << m_table_name << " table_id=" << m_table_id
     << " label="  << m_label;
}

#define OPERATION_SUSPEND_MAINTENANCE_VERSION 1

uint16_t OperationSuspendMaintenance::encoding_version() const {
  return OPERATION_SUSPEND_MAINTENANCE_VERSION;
}

size_t OperationSuspendMaintenance::encoded_state_length() const {
  size_t length = Serialization::encoded_length_vstr(m_table_name) +
    Serialization::encoded_length_vstr(m_table_id) +
    Serialization::encoded_length_vstr(m_label);
  length += 4;
  foreach_ht (const String &location, m_servers)
    length += Serialization::encoded_length_vstr(location);
  length += 4;
  foreach_ht (const String &location, m_completed)
    length += Serialization::encoded_length_vstr(location);
  return length;
}

void OperationSuspendMaintenance::encode_state(uint8_t **bufp) const {
  Serialization::encode_vstr(bufp, m_table_name);
  Serialization::encode_vstr(bufp, m_table_id);
  Serialization::encode_vstr(bufp, m_label);
  Serialization::encode_i32(bufp, m_servers.size());
  foreach_ht (const String &location, m_servers)
    Serialization::encode_vstr(bufp, location);
  Serialization::encode_i32(bufp, m_completed.size());
  foreach_ht (const String &location, m_completed)
    Serialization::encode_vstr(bufp, location);
}

void OperationSuspendMaintenance::decode_state(const uint8_t **bufp, size_t *remainp) {
  m_table_name = Serialization::decode_vstr(bufp, remainp);
  m_table_id = Serialization::decode_vstr(bufp, remainp);
  m_label = Serialization::decode_vstr(bufp, remainp);
  size_t length = Serialization::decode_i32(bufp, remainp);
  for (size_t i=0; i<length; i++)
    m_servers.insert( Serialization::decode_vstr(bufp, remainp) );
  length = Serialization::decode_i32(bufp, remainp);
  for (size_t i=0; i<length; i++)
    m_completed.insert( Serialization::decode_vstr(bufp, remainp) );
}

const String OperationSuspendMaintenance::name() {
  return "OperationSuspendMaintenance";
}

const String OperationSuspendMaintenance::label() {
  return format("Suspend Maintenance (table=%s, label=%s)",
                m_table_name.c_str(), m_label.c_str());
}
