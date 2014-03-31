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

#include "Common/Compat.h"
#include <cassert>
#include <cstdlib>

extern "C" {
#include <poll.h>
}

#include <boost/algorithm/string.hpp>

#include "AsyncComm/Comm.h"
#include "AsyncComm/ReactorFactory.h"

#include "Common/Init.h"
#include "Common/Error.h"
#include "Common/InetAddr.h"
#include "Common/Logger.h"
#include "Common/ScopeGuard.h"
#include "Common/StringExt.h"
#include "Common/System.h"
#include "Common/Timer.h"

#include "Hyperspace/DirEntry.h"
#include "Hypertable/Lib/Config.h"
#include "Hypertable/Lib/Types.h"

#include "Client.h"
#include "HqlCommandInterpreter.h"

using namespace std;
using namespace Hypertable;
using namespace Hyperspace;
using namespace Config;


Namespace::Namespace(const String &name, const String &id, PropertiesPtr &props,
    ConnectionManagerPtr &conn_manager, Hyperspace::SessionPtr &hyperspace,
    ApplicationQueueInterfacePtr &app_queue, NameIdMapperPtr &namemap, MasterClientPtr &master_client,
    RangeLocatorPtr &range_locator, TableCachePtr &table_cache, 
    uint32_t timeout, Client *client)
  : m_name(name), m_id(id), m_props(props),
    m_comm(conn_manager->get_comm()), m_conn_manager(conn_manager), m_hyperspace(hyperspace),
    m_app_queue(app_queue), m_namemap(namemap), m_master_client(master_client),
    m_range_locator(range_locator), m_table_cache(table_cache), 
    m_timeout_ms(timeout), m_client(client) {

  HT_ASSERT(m_props && conn_manager && m_hyperspace && m_app_queue && m_namemap &&
            m_master_client && m_range_locator && m_table_cache);

  m_hyperspace_reconnect = m_props->get_bool("Hyperspace.Session.Reconnect");
  m_toplevel_dir = m_props->get_str("Hypertable.Directory");
  canonicalize(&m_toplevel_dir);
  m_toplevel_dir = (String) "/" + m_toplevel_dir;
}

String Namespace::get_full_name(const String &sub_name) {
  // remove leading/trailing '/' and ' '
  String full_name = sub_name;
  boost::trim_if(full_name, boost::is_any_of("/ "));
  full_name = m_name + '/' + full_name;
  canonicalize(&full_name);
  return full_name;
}

void Namespace::canonicalize(String *original) {
  String output;
  boost::char_separator<char> sep("/");

  if (original == NULL)
    return;

  Tokenizer tokens(*original, sep);
  for (Tokenizer::iterator tok_iter = tokens.begin();
       tok_iter != tokens.end(); ++tok_iter)
    if (tok_iter->size() > 0)
      output += *tok_iter + "/";

  // remove leading/trailing '/' and ' '
  boost::trim_if(output, boost::is_any_of("/ "));
  *original = output;
}

void Namespace::compact(const String &name, const String &row, uint32_t flags) {
  if (name.empty())
    m_master_client->compact("", "", flags);
  else {
    String full_name = get_full_name(name);
    m_master_client->compact(full_name, row, flags);
  }
}

void Namespace::create_table(const String &table_name, const String &schema) {
  String name = Filesystem::basename(table_name);
  if (name.size() && name[0]=='^')
    HT_THROW(Error::SYNTAX_ERROR, (String)"Invalid table name character '^'");

  // Parse and validate schema
  {
    SchemaPtr schema_ptr = Schema::new_instance(schema.c_str(), schema.length());
    if (!schema_ptr->is_valid())
      HT_THROW(Error::BAD_SCHEMA, schema_ptr->get_error_string());
  }

  String full_name = get_full_name(table_name);
  m_master_client->create_table(full_name, schema);
}

void Namespace::alter_table(const String &table_name, SchemaPtr &alter_schema) {
  String name = Filesystem::basename(table_name);
  if (name.size() && name[0]=='^')
    HT_THROW(Error::SYNTAX_ERROR, (String)"Invalid table name character '^'");

  // Construct a new schema which is a merge of the existing schema
  // and the desired alterations.
  String full_name = get_full_name(table_name);

  Schema::AccessGroup *final_ag;
  Schema::ColumnFamily *final_cf;
  String final_schema_str;
  TablePtr table = open_table(table_name, Table::OPEN_FLAG_BYPASS_TABLE_CACHE);

  SchemaPtr schema = table->schema();

  SchemaPtr final_schema = new Schema(*(schema.get()));
  final_schema->incr_generation();

  foreach_ht(Schema::AccessGroup *alter_ag, alter_schema->get_access_groups()) {
    // create a new access group if needed
    if(!final_schema->access_group_exists(alter_ag->name)) {
      final_ag = new Schema::AccessGroup();
      final_ag->name = alter_ag->name;
      final_ag->in_memory = alter_ag->in_memory;
      final_ag->blocksize = alter_ag->blocksize;
      final_ag->compressor = alter_ag->compressor;
      final_ag->bloom_filter = alter_ag->bloom_filter;
      if (!final_schema->add_access_group(final_ag)) {
        String error_msg = final_schema->get_error_string();
        delete final_ag;
        HT_THROW(Error::BAD_SCHEMA, error_msg);
      }
    }
    else {
      final_ag = final_schema->get_access_group(alter_ag->name);
    }
  }

  // go through each column family to be altered
  foreach_ht(Schema::ColumnFamily *alter_cf, alter_schema->get_column_families()) {
    if (alter_cf->deleted) {
      if (!final_schema->drop_column_family(alter_cf->name))
        HT_THROW(Error::BAD_SCHEMA, final_schema->get_error_string());
    }
    else if (alter_cf->renamed) {
      if (!final_schema->rename_column_family(alter_cf->name, alter_cf->new_name))
        HT_THROW(Error::BAD_SCHEMA, final_schema->get_error_string());
    }
    else {
      // Check to see if column family exists, if so replace
      Schema::ColumnFamily *existing_cf 
        = final_schema->get_column_family(alter_cf->name, true);
      if (existing_cf) {

        if (!alter_cf->modification)
          HT_THROWF(Error::INVALID_OPERATION,
                    "Attempt to add column %s which already exists",
                    existing_cf->name.c_str());

        if (existing_cf->deleted)
          HT_THROWF(Error::UNSUPPORTED_OPERATION,
                    "Attempt to %s column %s which has been deleted",
                    alter_cf->modification ? "modify" : "add",
                    existing_cf->name.c_str());

        if (existing_cf->time_order_desc != alter_cf->time_order_desc)
          HT_THROWF(Error::UNSUPPORTED_OPERATION,
                    "Changing value of TIME_ORDER on column %s is not supported",
                    existing_cf->name.c_str());

        if (existing_cf->counter != alter_cf->counter)
          HT_THROWF(Error::UNSUPPORTED_OPERATION,
                    "Changing COUNTER attribute of column %s is not supported",
                    existing_cf->name.c_str());

        existing_cf->max_versions = alter_cf->max_versions;
        existing_cf->ttl = alter_cf->ttl;
        existing_cf->generation = final_schema->get_generation();
      }
      else {

        // add column family
        if(final_schema->get_max_column_family_id() >= Schema::ms_max_column_id)
          HT_THROW(Error::TOO_MANY_COLUMNS,
                   format("Attempting to add > %d column families to table",
                          (int)Schema::ms_max_column_id));
        final_schema->incr_max_column_family_id();
        final_cf = new Schema::ColumnFamily(*alter_cf);
        final_cf->id = (uint32_t) final_schema->get_max_column_family_id();
        final_cf->generation = final_schema->get_generation();

        if(!final_schema->add_column_family(final_cf)) {
          String error_msg = final_schema->get_error_string();
          delete final_cf;
          HT_THROW(Error::BAD_SCHEMA, error_msg);
        }
      }
    }
  }

  final_schema->render(final_schema_str, true);
  m_master_client->alter_table(full_name, final_schema_str);
}

void Namespace::alter_table(const String &table_name, const String &alter_schema_str) {
  SchemaPtr alter_schema = Schema::new_instance(alter_schema_str, 
                                                alter_schema_str.length());
  if (!alter_schema->is_valid())
    HT_THROW(Error::BAD_SCHEMA, alter_schema->get_error_string());
  alter_table(table_name, alter_schema);
}


TablePtr Namespace::open_table(const String &table_name, int32_t flags) {
  Locker<TableCache> lock(*m_table_cache);
  String full_name = get_full_name(table_name);
  TablePtr table = _open_table(full_name, flags);

  if (table->needs_index_table() && !table->has_index_table()) {
    full_name = get_full_name( get_index_table_name(table_name) );
    table->set_index_table(_open_table(full_name, 0));
  }

  if (table->needs_qualifier_index_table() 
      && !table->has_qualifier_index_table()) {
    full_name = get_full_name( get_qualifier_index_table_name(table_name) );
    table->set_qualifier_index_table(_open_table(full_name, 0));
  }

  return table;
}

TablePtr Namespace::_open_table(const String &full_name, int32_t flags) {
  TablePtr t;
  if (flags & Table::OPEN_FLAG_BYPASS_TABLE_CACHE)
    t=new Table(m_props, m_range_locator, m_conn_manager, m_hyperspace,
                     m_app_queue, m_namemap, full_name, flags, m_timeout_ms);
  else
    t=m_table_cache->get_unlocked(full_name, flags);
  t->set_namespace(this);
  return (t);
}

void Namespace::refresh_table(const String &table_name) {
  open_table(table_name, Table::OPEN_FLAG_REFRESH_TABLE_CACHE);
}

bool Namespace::exists_table(const String &table_name) {
  String full_name = get_full_name(table_name);

  String table_id;
  bool is_namespace = false;

  if (!m_namemap->name_to_id(full_name, table_id, &is_namespace) ||
      is_namespace)
    return false;

  // TODO: issue 11

  String table_file = m_toplevel_dir + "/tables/" + table_id;

  try {
    if (!m_hyperspace->attr_exists(table_file, "x"))
      return false;
  }
  catch(Exception &e) {
    if (e.code() == Error::HYPERSPACE_FILE_NOT_FOUND ||
        e.code() == Error::HYPERSPACE_BAD_PATHNAME)
      return false;
    HT_THROW(e.code(), e.what());
  }
  return true;
}


String Namespace::get_table_id(const String &table_name) {
  String full_name = get_full_name(table_name);
  String table_id;
  bool is_namespace;

  if (!m_namemap->name_to_id(full_name, table_id, &is_namespace) ||
      is_namespace)
    HT_THROW(Error::TABLE_NOT_FOUND, full_name);
  return table_id;
}

String Namespace::get_schema_str(const String &table_name, bool with_ids) {
  String schema;
  String full_name = get_full_name(table_name);

  refresh_table(table_name);
  if (!m_table_cache->get_schema_str(full_name, schema, with_ids))
    HT_THROW(Error::TABLE_NOT_FOUND, full_name);
  return schema;
}

SchemaPtr Namespace::get_schema(const String &table_name) {
  SchemaPtr schema;
  String full_name = get_full_name(table_name);

  refresh_table(table_name);
  if (!m_table_cache->get_schema(full_name, schema))
    HT_THROW(Error::TABLE_NOT_FOUND, full_name);
  return schema;
}

void Namespace::rename_table(const String &old_name, const String &new_name) {
  String name = Filesystem::basename(old_name);
  if (name.size() && name[0]=='^')
    HT_THROW(Error::SYNTAX_ERROR, (String)"Invalid table name character '^'");
  name = Filesystem::basename(new_name);
  if (name.size() && name[0]=='^')
    HT_THROW(Error::SYNTAX_ERROR, (String)"Invalid table name character '^'");
  String full_old_name = get_full_name(old_name);
  String full_new_name = get_full_name(new_name);

  m_master_client->rename_table(full_old_name, full_new_name);

  m_table_cache->remove(full_old_name);

  // also remove the index table from the cache
  String index_name=get_index_table_name(old_name);
  m_table_cache->remove(get_full_name(index_name));
  index_name=get_qualifier_index_table_name(old_name);
  m_table_cache->remove(get_full_name(index_name));
}

void Namespace::drop_table(const String &table_name, bool if_exists) {
  String name = Filesystem::basename(table_name);
  if (name.size() && name[0]=='^')
    HT_THROW(Error::SYNTAX_ERROR, (String)"Invalid table name character '^'");

  String full_name = get_full_name(table_name);

  m_master_client->drop_table(full_name, if_exists);
  m_table_cache->remove(full_name);

  // also remove the index tables from the cache
  String index_name=get_index_table_name(table_name);
  m_table_cache->remove(get_full_name(index_name));
  index_name=get_qualifier_index_table_name(table_name);
  m_table_cache->remove(get_full_name(index_name));
}

void Namespace::rebuild_indices(const std::string &table_name,
                                TableParts table_parts) {

  String full_name = get_full_name(table_name);

  m_master_client->recreate_index_tables(full_name, table_parts);

  // Rebuild indices
  {
    TablePtr table = open_table(table_name);
    ScanSpec scan_spec;
    scan_spec.rebuild_indices = table_parts;
    TableScannerPtr scanner = table->create_scanner(scan_spec);
    Cell cell;
    while (scanner->next(cell))
      HT_ASSERT(!"Rebuild index scan returned a cell");
  }    

  // also remove the index tables from the cache
  String index_name=get_index_table_name(table_name);
  m_table_cache->remove(get_full_name(index_name));
  index_name=get_qualifier_index_table_name(table_name);
  m_table_cache->remove(get_full_name(index_name));
}

void Namespace::get_listing(bool include_sub_entries, std::vector<NamespaceListing> &listing) {
  m_namemap->id_to_sublisting(m_id, include_sub_entries, listing);
}


void Namespace::get_table_splits(const String &table_name, TableSplitsContainer &splits) {
  TablePtr table;
  TableIdentifierManaged tid;
  SchemaPtr schema;
  char start_row[16];
  char end_row[16];
  TableScannerPtr scanner_ptr;
  ScanSpec scan_spec;
  Cell cell;
  String str;
  Hypertable::RowInterval ri;
  String last_row;
  TableSplitBuilder tsbuilder(splits.arena());
  ProxyMapT proxy_map;
  String full_name = get_full_name(table_name);

  try_again:

  table = open_table(table_name);

  table->get(tid, schema);

  {
    Locker<TableCache> lock(*m_table_cache);
    table = _open_table(TableIdentifier::METADATA_NAME);
  }

  sprintf(start_row, "%s:", tid.id);
  sprintf(end_row, "%s:%s", tid.id, Key::END_ROW_MARKER);

  scan_spec.clear();
  scan_spec.row_limit = 0;
  scan_spec.max_versions = 1;
  scan_spec.columns.clear();
  scan_spec.columns.push_back("Location");
  scan_spec.columns.push_back("StartRow");

  ri.start = start_row;
  ri.end = end_row;
  scan_spec.row_intervals.push_back(ri);

  scanner_ptr = table->create_scanner(scan_spec);

  m_comm->get_proxy_map(proxy_map);

  bool refresh=true;
  tsbuilder.clear();
  splits.clear();
  last_row.clear();
  while (scanner_ptr->next(cell)) {
    refresh = false;
    if (strcmp(last_row.c_str(), cell.row_key) && last_row != "") {
      const char *ptr = strchr(last_row.c_str(), ':');
      HT_ASSERT(ptr);
      tsbuilder.set_end_row(ptr+1);
      splits.push_back(tsbuilder.get());
      tsbuilder.clear();
    }
    if (!strcmp(cell.column_family, "Location")) {
      str = String((const char *)cell.value, cell.value_len);
      if (str == "!") {
        refresh = true;
        break;
      }
      boost::trim(str);
      tsbuilder.set_location(str);
      ProxyMapT::iterator pmiter = proxy_map.find(str);
      if (pmiter != proxy_map.end()) {
        tsbuilder.set_ip_address( (*pmiter).second.addr.format_ipaddress() );
        tsbuilder.set_hostname( (*pmiter).second.hostname );
      }
    }
    else if (!strcmp(cell.column_family, "StartRow")) {
      str = String((const char *)cell.value, cell.value_len);
      boost::trim(str);
      tsbuilder.set_start_row(str);
    }
    else
      HT_FATALF("Unexpected column family - %s", cell.column_family);
    last_row = cell.row_key;
  }
  if (refresh) {
    refresh_table(table_name);
    goto try_again;
  }
  tsbuilder.set_end_row(Key::END_ROW_MARKER);
  splits.push_back(tsbuilder.get());
}
