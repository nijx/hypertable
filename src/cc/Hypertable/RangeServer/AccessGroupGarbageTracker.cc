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
/// Definitions for AccessGroupGarbageTracker.
/// This file contains type definitions for AccessGroupGarbageTracker, a class
/// that estimates how much garbage has accumulated in an access group and
/// signals when collection is needed.

#include <Common/Compat.h>
#include "AccessGroupGarbageTracker.h"

#include <Hypertable/RangeServer/Global.h>

#include <Common/Config.h>
#include <Common/Logger.h>

#include <ctime>

using namespace Hypertable;
using namespace Config;

AccessGroupGarbageTracker::AccessGroupGarbageTracker(PropertiesPtr &props,
               CellCacheManagerPtr &cell_cache_manager, Schema::AccessGroup *ag)
  : m_cell_cache_manager(cell_cache_manager) {
  SubProperties cfg(props, "Hypertable.RangeServer.");
  m_garbage_threshold 
    = cfg.get_i32("AccessGroup.GarbageThreshold.Percentage") / 100.0;
  m_accum_data_target = cfg.get_i64("Range.SplitSize") / 10;
  m_accum_data_target_minimum = m_accum_data_target / 2;
  update_schema(ag);
}


void AccessGroupGarbageTracker::update_schema(Schema::AccessGroup *ag) {
  m_have_max_versions = false;
  m_min_ttl = m_max_ttl = 0;
  m_in_memory = ag->in_memory;
  foreach_ht(Schema::ColumnFamily *cf, ag->columns) {
    if (cf->max_versions > 0)
      m_have_max_versions = true;
    if (cf->ttl > 0) {
      if (m_min_ttl == 0) {
        HT_ASSERT(m_max_ttl == 0);
        m_min_ttl = m_max_ttl = cf->ttl;
      }
      else if (cf->ttl < m_min_ttl)
        m_min_ttl = cf->ttl;
      else if (cf->ttl > m_max_ttl)
        m_max_ttl = cf->ttl;
    }
  }
  m_elapsed_target_minimum = m_elapsed_target = m_min_ttl/10;
}

void AccessGroupGarbageTracker::update_cellstore_info(std::vector<CellStoreInfo> &stores) {
  m_stored_deletes = 0;
  m_stored_expirable = 0;
  m_current_disk_usage = 0;
  for (auto csi : stores) {
    m_stored_expirable += csi.expirable_data();
    m_stored_deletes += csi.delete_count();
    m_current_disk_usage += csi.cs->disk_usage() / csi.cs->compression_ratio();
  }
  if (m_in_memory)
    m_current_disk_usage = m_cell_cache_manager->logical_size();
}

void AccessGroupGarbageTracker::reset(time_t t) {
  m_last_reset_time = t;
  m_last_reset_disk_usage = m_current_disk_usage;
}

bool AccessGroupGarbageTracker::check_needed(time_t now) {
  if (m_last_reset_time)
    return check_needed_deletes() || check_needed_ttl(now);
  return false;
}


void
AccessGroupGarbageTracker::adjust_targets(time_t now, MergeScanner *mscanner) {
  uint64_t input, output;
  mscanner->get_io_accounting_data(&input, &output);
  adjust_targets(now, input, input-output);
}

void
AccessGroupGarbageTracker::adjust_targets(time_t now, double total,
                                          double garbage) {

  HT_ASSERT(m_last_reset_time);

  double garbage_pct = garbage / total;
  bool gc_needed = garbage_pct >= m_garbage_threshold;
  bool check_deletes = check_needed_deletes();
  bool check_ttl = check_needed_ttl(now);

  // If check matches actual need, targets are ok so just return
  if (gc_needed == (check_deletes||check_ttl))
    return;

  // Recompute DATA target
  bool have_garbage {m_have_max_versions || compute_delete_count() > 0};
  if (have_garbage && check_deletes != gc_needed) {
    if (garbage_pct > 0) {
      int64_t new_accum_data_target =
        (total_accumulated_since_reset() * m_garbage_threshold) / garbage_pct;
      if (new_accum_data_target < m_accum_data_target_minimum)
        m_accum_data_target = m_accum_data_target_minimum;
      else if (new_accum_data_target > (m_accum_data_target*2))
        m_accum_data_target *= 2;
    }
    else
      m_accum_data_target *= 2;
  }

  // Recompute ELAPSED target
  if (m_min_ttl > 0 && check_ttl != gc_needed) {
    if (garbage_pct > 0) {
      time_t new_elapsed_target = 
        ((now-m_last_reset_time) * m_garbage_threshold) / garbage_pct;
      if (new_elapsed_target < m_elapsed_target_minimum)
        m_elapsed_target = m_elapsed_target_minimum;
      else if (new_elapsed_target > (m_elapsed_target*2))
        m_elapsed_target *= 2;
    }
    else
      m_elapsed_target *= 2;
  }
}

int64_t AccessGroupGarbageTracker::memory_accumulated_since_reset() {
  int64_t accum;
  if (m_cell_cache_manager->immutable_cache())
    accum = m_cell_cache_manager->immutable_cache()->logical_size();
  else
    accum = m_cell_cache_manager->logical_size();
  if (m_in_memory)
    accum -= m_last_reset_disk_usage;
  HT_ASSERT(accum >= 0);
  return accum;
}

int64_t AccessGroupGarbageTracker::total_accumulated_since_reset() {
  int64_t accum {memory_accumulated_since_reset()};
  if (m_current_disk_usage > m_last_reset_disk_usage)
    accum += m_current_disk_usage - m_last_reset_disk_usage;
  return accum;
}

int64_t AccessGroupGarbageTracker::compute_delete_count() {
  int64_t delete_count {m_stored_deletes};
  if (m_cell_cache_manager->immutable_cache())
    delete_count += m_cell_cache_manager->immutable_cache()->delete_count();
  else
    delete_count += m_cell_cache_manager->delete_count();
  return delete_count;
}

bool AccessGroupGarbageTracker::check_needed_deletes() {
  if ((m_have_max_versions || compute_delete_count() > 0) &&
      total_accumulated_since_reset() >= m_accum_data_target)
    return true;
  return false;
}

bool AccessGroupGarbageTracker::check_needed_ttl(time_t now) {
  int64_t memory_accum {memory_accumulated_since_reset()};
  int64_t total_size {m_current_disk_usage + memory_accum};
  double possible_garbage = m_stored_expirable + memory_accum;
  double possible_garbage_pct = possible_garbage / total_size;
  time_t elapsed {now - m_last_reset_time};
  if (m_min_ttl > 0 && possible_garbage_pct >= m_garbage_threshold &&
      elapsed >= m_elapsed_target)
    return true;
  return false;
}
