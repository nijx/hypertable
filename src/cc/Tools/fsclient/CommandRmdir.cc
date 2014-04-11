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
#include "Common/Error.h"

#include "CommandRmdir.h"

using namespace std;
using namespace Hypertable;

const char *CommandRmdir::ms_usage[] = {
  "rmdir <dir>",
  "",
  "  This command sends a RMDIR request for the DFS directory <dir>",
  "  to the FsBroker.  The RMDIR FsBroker request is recursive.",
  (const char *)0
};


void CommandRmdir::run() {

  if (m_args.size() < 1)
    HT_THROW(Error::COMMAND_PARSE_ERROR, "No filename supplied.");

  m_client->rmdir(m_args[0].first);

}

