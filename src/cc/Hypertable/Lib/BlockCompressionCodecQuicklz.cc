/* -*- c++ -*-
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
/// Definitions for BlockCompressionCodecQuicklz.
/// This file contains definitions for BlockCompressionCodecQuicklz, a class
/// for compressing blocks using the QUICKLZ compression algorithm.

#include <Common/Compat.h>

#include "BlockCompressionCodecQuicklz.h"

#include <Common/Checksum.h>
#include <Common/DynamicBuffer.h>
#include <Common/Error.h>
#include <Common/Logger.h>
#include <Common/String.h>
#include <Common/Thread.h>

using namespace Hypertable;


void
BlockCompressionCodecQuicklz::deflate(const DynamicBuffer &input,
    DynamicBuffer &output, BlockHeader &header, size_t reserve) {
  uint32_t avail_out = input.fill() + 400;
  size_t len;

  output.clear();
  output.reserve(header.encoded_length() + avail_out + reserve);

  // compress
  len = qlz_compress((char *)input.base, (char *)output.base+header.encoded_length(),
                     input.fill(), &m_compress);

  if (len == 0)
    HT_FATALF("Problem deflating block of size %lld", (Lld)input.fill());

  /* check for an incompressible block */
  if (len >= input.fill()) {
    header.set_compression_type(NONE);
    memcpy(output.base+header.encoded_length(), input.base, input.fill());
    header.set_data_length(input.fill());
    header.set_data_zlength(input.fill());
  }
  else {
    header.set_compression_type(QUICKLZ);
    header.set_data_length(input.fill());
    header.set_data_zlength(len);
  }
  header.set_data_checksum(fletcher32(output.base + header.encoded_length(),
                           header.get_data_zlength()));

  output.ptr = output.base;
  header.encode(&output.ptr);
  output.ptr += header.get_data_zlength();
}


void BlockCompressionCodecQuicklz::inflate(const DynamicBuffer &input,
    DynamicBuffer &output, BlockHeader &header) {
  const uint8_t *msg_ptr = input.base;
  size_t remaining = input.fill();

  header.decode(&msg_ptr, &remaining);

  if (header.get_data_zlength() > remaining)
    HT_THROWF(Error::BLOCK_COMPRESSOR_BAD_HEADER, "Block decompression error, "
              "header zlength = %lu, actual = %lu",
              (Lu)header.get_data_zlength(), (Lu)remaining);

  uint32_t checksum = fletcher32(msg_ptr, header.get_data_zlength());

  if (checksum != header.get_data_checksum())
    HT_THROWF(Error::BLOCK_COMPRESSOR_CHECKSUM_MISMATCH, "Compressed block "
              "checksum mismatch header=%lx, computed=%lx",
              (Lu)header.get_data_checksum(), (Lu)checksum);

  output.reserve(header.get_data_length());

   // check compress type
  if (header.get_compression_type() == NONE)
    memcpy(output.base, msg_ptr, header.get_data_length());
  else {
    size_t len;
    // decompress
    len = qlz_decompress((char *)msg_ptr, (char *)output.base, &m_decompress);

    HT_ASSERT(len == header.get_data_length());
  }
  output.ptr = output.base + header.get_data_length();
}
