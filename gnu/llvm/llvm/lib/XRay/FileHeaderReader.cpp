//===- FileHeaderReader.cpp - XRay File Header Reader  --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "llvm/XRay/FileHeaderReader.h"

namespace llvm {
namespace xray {

// Populates the FileHeader reference by reading the first 32 bytes of the file.
Expected<XRayFileHeader> readBinaryFormatHeader(DataExtractor &HeaderExtractor,
                                                uint64_t &OffsetPtr) {
  // FIXME: Maybe deduce whether the data is little or big-endian using some
  // magic bytes in the beginning of the file?

  // First 32 bytes of the file will always be the header. We assume a certain
  // format here:
  //
  //   (2)   uint16 : version
  //   (2)   uint16 : type
  //   (4)   uint32 : bitfield
  //   (8)   uint64 : cycle frequency
  //   (16)  -      : padding
  XRayFileHeader FileHeader;
  auto PreReadOffset = OffsetPtr;
  FileHeader.Version = HeaderExtractor.getU16(&OffsetPtr);
  if (OffsetPtr == PreReadOffset)
    return createStringError(
        std::make_error_code(std::errc::invalid_argument),
        "Failed reading version from file header at offset %" PRId64 ".",
        OffsetPtr);

  PreReadOffset = OffsetPtr;
  FileHeader.Type = HeaderExtractor.getU16(&OffsetPtr);
  if (OffsetPtr == PreReadOffset)
    return createStringError(
        std::make_error_code(std::errc::invalid_argument),
        "Failed reading file type from file header at offset %" PRId64 ".",
        OffsetPtr);

  PreReadOffset = OffsetPtr;
  uint32_t Bitfield = HeaderExtractor.getU32(&OffsetPtr);
  if (OffsetPtr == PreReadOffset)
    return createStringError(
        std::make_error_code(std::errc::invalid_argument),
        "Failed reading flag bits from file header at offset %" PRId64 ".",
        OffsetPtr);

  FileHeader.ConstantTSC = Bitfield & 1uL;
  FileHeader.NonstopTSC = Bitfield & 1uL << 1;
  PreReadOffset = OffsetPtr;
  FileHeader.CycleFrequency = HeaderExtractor.getU64(&OffsetPtr);
  if (OffsetPtr == PreReadOffset)
    return createStringError(
        std::make_error_code(std::errc::invalid_argument),
        "Failed reading cycle frequency from file header at offset %" PRId64
        ".",
        OffsetPtr);

  std::memcpy(&FileHeader.FreeFormData,
              HeaderExtractor.getData().bytes_begin() + OffsetPtr, 16);

  // Manually advance the offset pointer 16 bytes, after getting a raw memcpy
  // from the underlying data.
  OffsetPtr += 16;
  return std::move(FileHeader);
}

} // namespace xray
} // namespace llvm
