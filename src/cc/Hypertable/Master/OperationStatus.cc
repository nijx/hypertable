/*
 * Copyright (C) 2007-2013 Hypertable, Inc.
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
#include "Common/Error.h"
#include "Common/FailureInducer.h"
#include "Common/Serialization.h"
#include "Common/StringExt.h"

#include "OperationStatus.h"

using namespace Hypertable;

OperationStatus::OperationStatus(ContextPtr &context, EventPtr &event) 
  : OperationEphemeral(context, event, MetaLog::EntityType::OPERATION_STATUS) {
  HT_INFOF("Status-%lld", (Lld)header.id);
}

void OperationStatus::execute() {
  complete_ok();
}

const String OperationStatus::name() {
  return "OperationStatus";
}

const String OperationStatus::label() {
  return String("Status");
}
