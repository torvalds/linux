//===-- ZipFile.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/ZipFile.h"
#include "lldb/Utility/DataBuffer.h"
#include "lldb/Utility/FileSpec.h"
#include "llvm/Support/Endian.h"

using namespace lldb_private;
using namespace llvm::support;

namespace {

// Zip headers.
// https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT

// The end of central directory record.
struct EocdRecord {
  static constexpr char kSignature[] = {0x50, 0x4b, 0x05, 0x06};
  char signature[sizeof(kSignature)];
  unaligned_uint16_t disks;
  unaligned_uint16_t cd_start_disk;
  unaligned_uint16_t cds_on_this_disk;
  unaligned_uint16_t cd_records;
  unaligned_uint32_t cd_size;
  unaligned_uint32_t cd_offset;
  unaligned_uint16_t comment_length;
};

// Logical find limit for the end of central directory record.
const size_t kEocdRecordFindLimit =
    sizeof(EocdRecord) +
    std::numeric_limits<decltype(EocdRecord::comment_length)>::max();

// Central directory record.
struct CdRecord {
  static constexpr char kSignature[] = {0x50, 0x4b, 0x01, 0x02};
  char signature[sizeof(kSignature)];
  unaligned_uint16_t version_made_by;
  unaligned_uint16_t version_needed_to_extract;
  unaligned_uint16_t general_purpose_bit_flag;
  unaligned_uint16_t compression_method;
  unaligned_uint16_t last_modification_time;
  unaligned_uint16_t last_modification_date;
  unaligned_uint32_t crc32;
  unaligned_uint32_t compressed_size;
  unaligned_uint32_t uncompressed_size;
  unaligned_uint16_t file_name_length;
  unaligned_uint16_t extra_field_length;
  unaligned_uint16_t comment_length;
  unaligned_uint16_t file_start_disk;
  unaligned_uint16_t internal_file_attributes;
  unaligned_uint32_t external_file_attributes;
  unaligned_uint32_t local_file_header_offset;
};
// Immediately after CdRecord,
// - file name (file_name_length)
// - extra field (extra_field_length)
// - comment (comment_length)

// Local file header.
struct LocalFileHeader {
  static constexpr char kSignature[] = {0x50, 0x4b, 0x03, 0x04};
  char signature[sizeof(kSignature)];
  unaligned_uint16_t version_needed_to_extract;
  unaligned_uint16_t general_purpose_bit_flag;
  unaligned_uint16_t compression_method;
  unaligned_uint16_t last_modification_time;
  unaligned_uint16_t last_modification_date;
  unaligned_uint32_t crc32;
  unaligned_uint32_t compressed_size;
  unaligned_uint32_t uncompressed_size;
  unaligned_uint16_t file_name_length;
  unaligned_uint16_t extra_field_length;
};
// Immediately after LocalFileHeader,
// - file name (file_name_length)
// - extra field (extra_field_length)
// - file data (should be compressed_size == uncompressed_size, page aligned)

const EocdRecord *FindEocdRecord(lldb::DataBufferSP zip_data) {
  // Find backward the end of central directory record from the end of the zip
  // file to the find limit.
  const uint8_t *zip_data_end = zip_data->GetBytes() + zip_data->GetByteSize();
  const uint8_t *find_limit = zip_data_end - kEocdRecordFindLimit;
  const uint8_t *p = zip_data_end - sizeof(EocdRecord);
  for (; p >= zip_data->GetBytes() && p >= find_limit; p--) {
    auto eocd = reinterpret_cast<const EocdRecord *>(p);
    if (::memcmp(eocd->signature, EocdRecord::kSignature,
                 sizeof(EocdRecord::kSignature)) == 0) {
      // Found the end of central directory. Sanity check the values.
      if (eocd->cd_records * sizeof(CdRecord) > eocd->cd_size ||
          zip_data->GetBytes() + eocd->cd_offset + eocd->cd_size > p)
        return nullptr;

      // This is a valid end of central directory record.
      return eocd;
    }
  }
  return nullptr;
}

bool GetFile(lldb::DataBufferSP zip_data, uint32_t local_file_header_offset,
             lldb::offset_t &file_offset, lldb::offset_t &file_size) {
  auto local_file_header = reinterpret_cast<const LocalFileHeader *>(
      zip_data->GetBytes() + local_file_header_offset);
  // The signature should match.
  if (::memcmp(local_file_header->signature, LocalFileHeader::kSignature,
               sizeof(LocalFileHeader::kSignature)) != 0)
    return false;

  auto file_data = reinterpret_cast<const uint8_t *>(local_file_header + 1) +
                   local_file_header->file_name_length +
                   local_file_header->extra_field_length;
  // File should be uncompressed.
  if (local_file_header->compressed_size !=
      local_file_header->uncompressed_size)
    return false;

  // This file is valid. Return the file offset and size.
  file_offset = file_data - zip_data->GetBytes();
  file_size = local_file_header->uncompressed_size;
  return true;
}

bool FindFile(lldb::DataBufferSP zip_data, const EocdRecord *eocd,
              const llvm::StringRef file_path, lldb::offset_t &file_offset,
              lldb::offset_t &file_size) {
  // Find the file from the central directory records.
  auto cd = reinterpret_cast<const CdRecord *>(zip_data->GetBytes() +
                                               eocd->cd_offset);
  size_t cd_records = eocd->cd_records;
  for (size_t i = 0; i < cd_records; i++) {
    // The signature should match.
    if (::memcmp(cd->signature, CdRecord::kSignature,
                 sizeof(CdRecord::kSignature)) != 0)
      return false;

    // Sanity check the file name values.
    auto file_name = reinterpret_cast<const char *>(cd + 1);
    size_t file_name_length = cd->file_name_length;
    if (file_name + file_name_length >= reinterpret_cast<const char *>(eocd) ||
        file_name_length == 0)
      return false;

    // Compare the file name.
    if (file_path == llvm::StringRef(file_name, file_name_length)) {
      // Found the file.
      return GetFile(zip_data, cd->local_file_header_offset, file_offset,
                     file_size);
    } else {
      // Skip to the next central directory record.
      cd = reinterpret_cast<const CdRecord *>(
          reinterpret_cast<const char *>(cd) + sizeof(CdRecord) +
          cd->file_name_length + cd->extra_field_length + cd->comment_length);
      // Sanity check the pointer.
      if (reinterpret_cast<const char *>(cd) >=
          reinterpret_cast<const char *>(eocd))
        return false;
    }
  }

  return false;
}

} // end anonymous namespace

bool ZipFile::Find(lldb::DataBufferSP zip_data, const llvm::StringRef file_path,
                   lldb::offset_t &file_offset, lldb::offset_t &file_size) {
  const EocdRecord *eocd = FindEocdRecord(zip_data);
  if (!eocd)
    return false;

  return FindFile(zip_data, eocd, file_path, file_offset, file_size);
}
