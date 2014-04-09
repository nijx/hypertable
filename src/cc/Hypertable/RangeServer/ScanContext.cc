/*
 * Copyright (C) 2007-2012 Hypertable, Inc.
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
/// Definitions for ScanContext.
/// This file contains the type definitions for the ScanContext, a class that
/// provides context for a scan.

#include <Common/Compat.h>
#include "ScanContext.h"

#include <Hypertable/RangeServer/Global.h>

#include <Hypertable/Lib/Key.h>

#include <Common/Logger.h>

#include <algorithm>
#include <cassert>
#include <re2/re2.h>


using namespace std;
using namespace Hypertable;


void
ScanContext::initialize(int64_t rev, const ScanSpec *ss,
    const RangeSpec *range_spec, SchemaPtr &sp) {
  Schema::ColumnFamily *cf;
  uint32_t max_versions = 0;
  boost::xtime xtnow;
  int64_t now;
  String family;
  const char *qualifier;
  size_t qualifier_len;
  size_t id = 0;
  bool is_regexp, is_prefix;

  boost::xtime_get(&xtnow, boost::TIME_UTC_);
  now = ((int64_t)xtnow.sec * 1000000000LL) + (int64_t)xtnow.nsec;

  revision = (rev == TIMESTAMP_NULL) ? TIMESTAMP_MAX : rev;

  // set time interval
  if (ss) {
    time_interval.first = ss->time_interval.first;
    time_interval.second = ss->time_interval.second;
  }
  else {
    time_interval.first = TIMESTAMP_MIN;
    time_interval.second = TIMESTAMP_MAX;
  }

  spec = ss;
  range = range_spec;

  if (spec == 0)
    memset(family_mask, true, 256*sizeof(bool));
  else {
    memset(family_mask, false, 256*sizeof(bool));
    max_versions = spec->max_versions;
  }

  if (sp) {
    schema = sp;

    if (spec && spec->columns.size() > 0) {
      bool has_qualifier;

      foreach_ht(const char *cfstr, spec->columns) {

        cfstr = (const char *)arena.dup(cfstr);

        ScanSpec::parse_column(cfstr, family, &qualifier, &qualifier_len,
                               &has_qualifier, &is_regexp, &is_prefix);
        cf = schema->get_column_family(family.c_str());

        if (cf == 0)
          HT_THROW(Error::RANGESERVER_INVALID_COLUMNFAMILY, cfstr);

        family_mask[cf->id] = true;
        if (has_qualifier) {
          ColumnPredicate cp;
          cp.operation = 
            is_regexp ? ColumnPredicate::QUALIFIER_REGEX_MATCH : 
            (is_prefix ? ColumnPredicate::QUALIFIER_PREFIX_MATCH :
             ColumnPredicate::QUALIFIER_EXACT_MATCH);
          cp.column_qualifier = qualifier;
          cp.column_qualifier_len = qualifier_len;
          cell_predicates[cf->id].add_column_predicate(cp, id++);
        }

        if (cf->ttl == 0)
          cell_predicates[cf->id].cutoff_time = TIMESTAMP_MIN;
        else
          cell_predicates[cf->id].cutoff_time = now
            - ((int64_t)cf->ttl * 1000000000LL);
        if (max_versions == 0)
          cell_predicates[cf->id].max_versions = cf->max_versions;
        else {
          if (cf->max_versions == 0)
            cell_predicates[cf->id].max_versions = max_versions;
          else
            cell_predicates[cf->id].max_versions = max_versions < cf->max_versions
              ?  max_versions : cf->max_versions;
        }
        if (cf->counter)
          cell_predicates[cf->id].counter = true;
        cell_predicates[cf->id].indexed = cf->has_index || cf->has_qualifier_index;
      }
    }
    else {
      Schema::AccessGroups &aglist = schema->get_access_groups();

      family_mask[0] = true;  // ROW_DELETE records have 0 column family, so
      // this allows them to pass through
      for (Schema::AccessGroups::iterator ag_it = aglist.begin();
          ag_it != aglist.end(); ++ag_it) {
        for (Schema::ColumnFamilies::iterator cf_it = (*ag_it)->columns.begin();
            cf_it != (*ag_it)->columns.end(); ++cf_it) {
          if ((*cf_it)->id == 0)
            HT_THROWF(Error::RANGESERVER_SCHEMA_INVALID_CFID,
                "Bad ID for Column Family '%s'", (*cf_it)->name.c_str());
          if ((*cf_it)->deleted) {
            family_mask[(*cf_it)->id] = false;
            continue;
          }
          family_mask[(*cf_it)->id] = true;
          if ((*cf_it)->ttl == 0)
            cell_predicates[(*cf_it)->id].cutoff_time = TIMESTAMP_MIN;
          else
            cell_predicates[(*cf_it)->id].cutoff_time = now
              - ((int64_t)(*cf_it)->ttl * 1000000000LL);

          if (max_versions == 0)
            cell_predicates[(*cf_it)->id].max_versions = (*cf_it)->max_versions;
          else {
            if ((*cf_it)->max_versions == 0)
              cell_predicates[(*cf_it)->id].max_versions = max_versions;
            else
              cell_predicates[(*cf_it)->id].max_versions =
                (max_versions < (*cf_it)->max_versions)
                ? max_versions : (*cf_it)->max_versions;
          }
          if ((*cf_it)->counter)
            cell_predicates[(*cf_it)->id].counter = true;
          cell_predicates[(*cf_it)->id].indexed = (*cf_it)->has_index || (*cf_it)->has_qualifier_index;
        }
      }
    }
  }

  /**
   * Create Start Key and End Key
   */

  single_row = false;
  has_cell_interval = false;
  has_start_cf_qualifier = false;
  start_inclusive = end_inclusive = true;
  restricted_range = true;

  if (spec) {
    const char *ptr = 0;

    if (!spec->row_intervals.empty()) {
      // start row
      start_row = spec->row_intervals.front().start;
      start_inclusive = spec->row_intervals.front().start_inclusive;

      // end row (if scan_and_filter_rows the rows are ordered ascending, otherwise only one row interval)
      if (spec->row_intervals.back().end[0] == 0)
        end_row = Key::END_ROW_MARKER;
      else {
        end_row = spec->row_intervals.back().end;
        end_inclusive = spec->row_intervals.back().end_inclusive;

        if (!strcmp(spec->row_intervals.front().start, spec->row_intervals.back().end))
          single_row = true;
      }

      if (spec->scan_and_filter_rows) {
        foreach_ht (const RowInterval& ri, spec->row_intervals) {
          rowset.insert(arena.dup(ri.start)); // ri.end is set to "" in order to safe space
        }
        end_row = *rowset.rbegin();
        end_inclusive = true;
        single_row = rowset.size() == 1;
      }
    }
    else if (!spec->cell_intervals.empty()) {
      String column_family_str;
      Schema::ColumnFamily *cf;

      has_cell_interval = true;

      if (*spec->cell_intervals[0].start_column) {
        ptr = strchr(spec->cell_intervals[0].start_column, ':');
        if (ptr == 0) {
          ptr = spec->cell_intervals[0].start_column
            + strlen(spec->cell_intervals[0].start_column);
          start_qualifier = "";
        }
        else {
          start_qualifier = ptr+1;
          start_key.column_qualifier = start_qualifier.c_str();
          start_key.column_qualifier_len = start_qualifier.length();
          has_start_cf_qualifier = true;
        }
        column_family_str = String(spec->cell_intervals[0].start_column,
            ptr - spec->cell_intervals[0].start_column);
        if ((cf = schema->get_column_family(column_family_str)) == 0)
          HT_THROW(Error::RANGESERVER_BAD_SCAN_SPEC,
              format("Bad column family (%s)", column_family_str.c_str()));

        start_key.column_family_code = cf->id;

        start_row = spec->cell_intervals[0].start_row;
        start_inclusive = spec->cell_intervals[0].start_inclusive;
      }
      else {
        start_row = "";
        start_qualifier = "";
        start_inclusive = true;
      }

      if (*spec->cell_intervals[0].end_column) {
        ptr = strchr(spec->cell_intervals[0].end_column, ':');
        if (ptr == 0) {
          ptr = spec->cell_intervals[0].end_column
            + strlen(spec->cell_intervals[0].end_column);
          end_qualifier = "";
        }
        else {
          end_qualifier = ptr+1;
          end_key.column_qualifier = end_qualifier.c_str();
          end_key.column_qualifier_len = end_qualifier.length();
        }

        column_family_str = String(spec->cell_intervals[0].end_column,
            ptr - spec->cell_intervals[0].end_column);
        if ((cf = schema->get_column_family(column_family_str)) == 0)
          HT_THROWF(Error::RANGESERVER_BAD_SCAN_SPEC, "Bad column family (%s)",
              column_family_str.c_str());

        end_key.column_family_code = cf->id;

        end_row = spec->cell_intervals[0].end_row;
        end_inclusive = spec->cell_intervals[0].end_inclusive;
      }
      else {
        end_row = Key::END_ROW_MARKER;
        end_qualifier = "";
      }

      if (!strcmp(spec->cell_intervals[0].start_row,
            spec->cell_intervals[0].end_row))
        single_row = true;

      if (single_row && ((end_key.column_family_code == start_key.column_family_code
              && start_qualifier.compare(end_qualifier) > 0)
            || start_key.column_family_code > end_key.column_family_code))
        HT_THROW(Error::RANGESERVER_BAD_SCAN_SPEC, "start_cell > end_cell");

    }
    else {
      start_row = "";
      end_row = Key::END_ROW_MARKER;
    }

    if (start_row.compare(end_row) > 0)
      HT_THROW(Error::RANGESERVER_BAD_SCAN_SPEC, "start_row > end_row");
  }
  else {
    start_row = "";
    end_row = Key::END_ROW_MARKER;
  }

  if (start_row == "" && end_row == Key::END_ROW_MARKER)
    restricted_range = false;

  assert(start_row <= end_row);

  start_key.row = start_row.c_str();
  start_key.row_len = start_row.length();

  end_key.row = end_row.c_str();
  end_key.row_len = end_row.length();

  dbuf.reserve(start_row.length() + start_qualifier.length()
      + end_row.length() + end_qualifier.length() + 64);

  String tmp_str;

  if (spec && !spec->cell_intervals.empty()) {
    if (start_inclusive)
      // DELETE_ROW and DELETE_CF will be handled by the scanner
      create_key_and_append(dbuf, FLAG_DELETE_CELL, start_key.row, start_key.column_family_code,
          start_key.column_qualifier, TIMESTAMP_MAX, revision);
    else {
      if (start_key.column_qualifier == 0)
        tmp_str = Key::END_ROW_MARKER;
      else {
        tmp_str = start_key.column_qualifier;
        tmp_str.append(1, 1);
      }
      // DELETE_ROW and DELETE_CF will be handled by the scanner
      create_key_and_append(dbuf, FLAG_DELETE_CELL, start_key.row,
          start_key.column_family_code,
          tmp_str.c_str(), TIMESTAMP_MAX, revision);
    }
    start_serkey.ptr = dbuf.base;
    end_serkey.ptr = dbuf.ptr;

    if (!end_inclusive)
      create_key_and_append(dbuf, 0, end_key.row, end_key.column_family_code,
          end_key.column_qualifier, TIMESTAMP_MAX, revision);
    else {
      if (end_key.column_qualifier == 0)
        tmp_str = Key::END_ROW_MARKER;
      else {
        tmp_str = end_key.column_qualifier;
        tmp_str.append(1, 1);
      }
      create_key_and_append(dbuf, 0, end_key.row, end_key.column_family_code,
          tmp_str.c_str(), TIMESTAMP_MAX, revision);
    }
  }
  else {
    if (start_inclusive || start_key.row_len == 0)
      create_key_and_append(dbuf, 0, start_key.row, 0, "", TIMESTAMP_MAX, revision);
    else {
      tmp_str = start_key.row;
      tmp_str.append(1, 1);
      create_key_and_append(dbuf, 0, tmp_str.c_str(), 0, "", TIMESTAMP_MAX, revision);
    }
    start_serkey.ptr = dbuf.base;
    end_serkey.ptr = dbuf.ptr;
    if (!end_inclusive)
      create_key_and_append(dbuf, 0, end_key.row, 0, "", TIMESTAMP_MAX, revision);
    else {
      tmp_str = end_key.row;
      tmp_str.append(1, 1);
      create_key_and_append(dbuf, 0, tmp_str.c_str(), 0, "", TIMESTAMP_MAX, revision);
    }
  }

  /** Get row, value regexps and row set **/
  if (spec) {
    if (spec->row_regexp && *spec->row_regexp != 0) {
      row_regexp = new RE2(spec->row_regexp);
      if (!row_regexp->ok()) {
        HT_THROW(Error::BAD_SCAN_SPEC, (String)"Can't convert row_regexp "
            + spec->row_regexp + " to regexp -" + row_regexp->error_arg());
      }
    }
    if (spec->value_regexp && *spec->value_regexp != 0) {
      value_regexp = new RE2(spec->value_regexp);
      if (!value_regexp->ok()) {
        HT_THROW(Error::BAD_SCAN_SPEC, (String)"Can't convert value_regexp "
            + spec->value_regexp + " to regexp -" + value_regexp->error_arg());
      }
    }

    foreach_ht (const ColumnPredicate& cp, spec->column_predicates) {
      if (cp.column_family && *cp.column_family) {
        cf = schema->get_column_family(cp.column_family);
        if (cf == 0) {
          HT_THROW(Error::RANGESERVER_INVALID_COLUMNFAMILY,
                   format("Invalid column family '%s'", cp.column_family).c_str() );
        }
        if (cf->id == 0) {
          HT_THROW(Error::RANGESERVER_SCHEMA_INVALID_CFID,
                   format("Bad id for column family '%s'", cf->name.c_str()).c_str() );
        }
        if (cf->counter) {
          HT_THROW(Error::BAD_SCAN_SPEC, "Counters are not supported for column predicates" );
        }
        cell_predicates[cf->id].add_column_predicate(cp, id++);
        cell_predicates[cf->id].indexed = cf->has_index || cf->has_qualifier_index;
      }
    }
  }
}
