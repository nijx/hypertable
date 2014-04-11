/**
 * Copyright (C) 2007-2012 Hypertable, Inc.
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

#include "Common/Compat.h"
#include <iostream>
#include <vector>

extern "C" {
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
}

#include "Common/Error.h"
#include "Common/Logger.h"

#include "CommandCopyFromLocal.h"
#include "CommandCopyToLocal.h"
#include "CommandRemove.h"
#include "CommandLength.h"
#include "CommandMkdirs.h"
#include "fsTestThreadFunction.h"

using namespace Hypertable;
using namespace std;

/**
 *
 */
void DfsTestThreadFunction::operator()() {
  vector<const char *> args;
  int64_t origsz, dfssz1, dfssz2;
  CommandCopyFromLocal cmd_cp_from_local(m_client);
  CommandCopyToLocal cmd_cp_to_local(m_client);
  CommandRemove cmd_rm(m_client);

  try {

    cmd_cp_from_local.push_arg(m_input_file, "");
    cmd_cp_from_local.push_arg(m_dfs_file, "");
    cmd_cp_from_local.run();

    cmd_cp_to_local.push_arg(m_dfs_file, "");
    cmd_cp_to_local.push_arg(m_output_file, "");
    cmd_cp_to_local.run();

    // Determine original file size
    struct stat statbuf;
    if (stat(m_input_file.c_str(), &statbuf) != 0)
      HT_THROW(Error::EXTERNAL, (std::string)"Unable to stat file '"
               + m_input_file + "' - " + strerror(errno));

    origsz = statbuf.st_size;

    // Make sure file exists
    HT_ASSERT(m_client->exists(m_dfs_file));

    // Determine DFS file size
    dfssz1 = m_client->length(m_dfs_file, false);
    dfssz2 = m_client->length(m_dfs_file, true);

    if (origsz != dfssz1) {
      HT_ERRORF("Length mismatch: %lld != %lld", (Lld)origsz, (Lld)dfssz1);
      exit(1);
    }
    if (origsz != dfssz2) {
      HT_ERRORF("Length mismatch: %lld != %lld", (Lld)origsz, (Lld)dfssz2);
      exit(1);
    }
  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    _exit(1);
  }

  /**
  cmd_rm.push_arg(m_dfs_file, "");
  if (cmd_rm.run() != 0)
    exit(1);
  **/
}
