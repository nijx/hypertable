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
/// Declarations for AccessGroupGarbageTracker.
/// This file contains type declarations for AccessGroupGarbageTracker, a class
/// that estimates how much garbage has accumulated in an access group and
/// signals when collection is needed.

#ifndef HYPERTABLE_ACCESSGROUPGARBAGETRACKER_H
#define HYPERTABLE_ACCESSGROUPGARBAGETRACKER_H

#include <Hypertable/RangeServer/CellCacheManager.h>
#include <Hypertable/RangeServer/CellStoreInfo.h>
#include <Hypertable/RangeServer/MergeScanner.h>

#include <Hypertable/Lib/Schema.h>

#include <Common/Properties.h>

extern "C" {
#include <time.h>
}

namespace Hypertable {

  /// @addtogroup RangeServer
  /// @{

  /// Tracks garbage build-up in an access group.
  /// This class is used to estimate how much garbage has accumulated in an
  /// access group and signal when collection is needed.
  class AccessGroupGarbageTracker {
  public:

    /// Constructor.
    /// Initializes #m_data_target and #m_data_target_minimum to 10% of
    /// <code>Hypertable.RangeServer.Range.SplitSize</code> property,
    /// initializes #m_last_clear_time to current time, and calls
    /// update_schema().
    /// @param ag Access group schema object
    AccessGroupGarbageTracker(PropertiesPtr &props,
                              CellCacheManagerPtr &cell_cache_manager,
                              Schema::AccessGroup *ag);

    /// Initializes GC control variables from access group schema definition.
    /// This method sets #m_have_max_versions to <i>true</i> if any of the
    /// column families in the schema has non-zero max_versions, and sets
    /// #m_min_ttl and #m_max_ttl to the minimum and maximum ttl values found
    /// in the column families, and sets #m_elapsed_target_minimum and
    /// #m_elapsed_target to 10% of the minimum ttl encountered.
    /// @param ag Access group schema definition
    void update_schema(Schema::AccessGroup *ag);

    void update_cellstore_info(std::vector<CellStoreInfo> &stores);

    void reset(time_t t);

    /// Determines if there is likelihood of needed garbage collection.
    /// For the purposes of the check, accumulated deletes is equal to
    /// #m_accumulated_deletes plus <code>additional_deletes</code>,
    /// accumulated data is equal to #m_accumulated_data plus
    /// <code>additional_data</code>, and accumulated expirable is equal to
    /// #m_accumulated_expirable plus <code>additional_data</code>.  The
    /// function will return <i>true</i> under the following circumstances:
    ///   - #m_have_max_versions is <i>true</i> or any deletes have been
    ///     accumulated, <b>and</b> the accumulated data is greater than or
    ///     equal to #m_data_target.
    ///   - #m_min_ttl is greater than zero and accumulated expirable is greater
    ///     than or equal to #m_data_target_minimum and the elapsed time since
    ///     the last clear (<code>now</code> - #m_last_clear_time) is greater
    ///     than or equal to #m_elapsed_target.
    /// @param additional_deletes Additional deletes to use in calculation
    /// @param additional_data Additional accumulated cell cache data used in TTL
    /// garbage estimate
    /// @param now Current time (seconds since epoch)
    /// @return <i>true</i> if garbage collection is likely needed, <i>false</i>
    /// otherwise
    bool check_needed(time_t now);

    void adjust_targets(time_t now, MergeScanner *mscanner);

    /**
     * Sets the garbage statistics measured from a merge scan over
     * all of the CellStores and the CellCache.  This information
     * is used to determine if garbage collection (i.e. major
     * compaction) is necessary and to update the data_target.
     * The data_target represents the amount of data to accumulate
     * before doing garbage collection.  <i>total</i> minus <i>valid</i>
     * is equal to the amount of garbage that has accumulated.
     * 
     * @param total total amount of data read from the merge scan sources
     * @param valid amount of data that was returned by the merge scan
     * @param now current time (seconds since epoch)
     */
    void adjust_targets(time_t now, double total, double garbage);

    bool collection_needed(double total, double garbage) {
      return (garbage / total) >= m_garbage_threshold;
    }

  private:

    int64_t memory_accumulated_since_reset();

    int64_t total_accumulated_since_reset();

    /// Computes number of delete records in access group
    /// This method computes the number of delete records that exist by adding
    /// #m_stored_deletes with the deletes from the immutable cache, if it
    /// exists, or all deletes reported by the cell cache manager, otherwise.
    /// @return number of deletes records in access group
    int64_t compute_delete_count();

    /// Checks if GC may be needed due to MAX_VERSIONS or deletes
    /// This method computes the amount of data that has accumulated since
    /// the last call to reset() by adding the data accumulated on disk
    /// (#m_current_disk_usage - #m_last_reset_disk_usage) with the in memory
    /// data accumulated determined with a call to compute_memory_accumulated().
    /// It determines if GC may be needed if #m_have_max_versions is <i>true</i>
    /// or compute_delete_count() returns a non-zero value, <b>and</b> the amount
    /// of data that has accumulated since the last call to reset() is
    /// greater than or equal to #m_accum_data_target.
    /// @return <i>true</i> if GC may be needed, <i>false</i> otherwise
    bool check_needed_deletes();
    
    bool check_needed_ttl(time_t now);
    
    /// &Cell cache manager
    CellCacheManagerPtr m_cell_cache_manager;

    /// Fraction of accumulated garbage that triggers collection
    double m_garbage_threshold;

    /// Elapsed seconds required before signaling TTL GC required (adaptive)
    time_t m_elapsed_target {};

    /// Minimum elapsed seconds required before signaling TTL GC required
    time_t m_elapsed_target_minimum {};

    /// Last time reset() was called
    time_t m_last_reset_time {0};

    /// Number of delete records accumulated
    uint32_t m_stored_deletes {};

    /// Amount of data accumulated that could expire due to TTL
    int64_t m_stored_expirable {};

    /// Disk usage after last <i>major</i> or <i>in memory</i> compaction
    int64_t m_last_reset_disk_usage {};

    /// Disk usage after last compaction
    int64_t m_current_disk_usage {};

    /// Amount of data accummulated before signaling GC (adaptive)
    int64_t m_accum_data_target {};

    /// Minimum amount of data accummulated before signaling GC
    int64_t m_accum_data_target_minimum {};

    /// Minimum TTL found in access group schema
    int64_t m_min_ttl {};

    /// Maximum TTL found in access group schema
    int64_t m_max_ttl {};

    /// <i>true</i> if any column families have non-zero MAX_VERSIONS
    bool m_have_max_versions {};

    /// <i>true</i> if access group is IN_MEMORY
    bool m_in_memory {};
  };

  /// @}

} // namespace Hypertable

#endif // HYPERTABLE_ACCESSGROUPGARBAGETRACKER_H
