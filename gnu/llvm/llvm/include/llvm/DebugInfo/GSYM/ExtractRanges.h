//===- ExtractRanges.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_GSYM_EXTRACTRANGES_H
#define LLVM_DEBUGINFO_GSYM_EXTRACTRANGES_H

#include "llvm/ADT/AddressRanges.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include <stdint.h>

#define HEX8(v) llvm::format_hex(v, 4)
#define HEX16(v) llvm::format_hex(v, 6)
#define HEX32(v) llvm::format_hex(v, 10)
#define HEX64(v) llvm::format_hex(v, 18)

namespace llvm {
class DataExtractor;
class raw_ostream;

namespace gsym {

class FileWriter;

/// AddressRange objects are encoded and decoded to be relative to a base
/// address. This will be the FunctionInfo's start address if the AddressRange
/// is directly contained in a FunctionInfo, or a base address of the
/// containing parent AddressRange or AddressRanges. This allows address
/// ranges to be efficiently encoded using ULEB128 encodings as we encode the
/// offset and size of each range instead of full addresses. This also makes
/// encoded addresses easy to relocate as we just need to relocate one base
/// address.
/// @{
AddressRange decodeRange(DataExtractor &Data, uint64_t BaseAddr,
                         uint64_t &Offset);
void encodeRange(const AddressRange &Range, FileWriter &O, uint64_t BaseAddr);
/// @}

/// Skip an address range object in the specified data a the specified
/// offset.
///
/// \param Data The binary stream to read the data from.
///
/// \param Offset The byte offset within \a Data.
void skipRange(DataExtractor &Data, uint64_t &Offset);

/// Address ranges are decoded and encoded to be relative to a base address.
/// See the AddressRange comment for the encode and decode methods for full
/// details.
/// @{
void decodeRanges(AddressRanges &Ranges, DataExtractor &Data, uint64_t BaseAddr,
                  uint64_t &Offset);
void encodeRanges(const AddressRanges &Ranges, FileWriter &O,
                  uint64_t BaseAddr);
/// @}

/// Skip an address range object in the specified data a the specified
/// offset.
///
/// \param Data The binary stream to read the data from.
///
/// \param Offset The byte offset within \a Data.
///
/// \returns The number of address ranges that were skipped.
uint64_t skipRanges(DataExtractor &Data, uint64_t &Offset);

} // namespace gsym

raw_ostream &operator<<(raw_ostream &OS, const AddressRange &R);

raw_ostream &operator<<(raw_ostream &OS, const AddressRanges &AR);

} // namespace llvm

#endif // LLVM_DEBUGINFO_GSYM_EXTRACTRANGES_H
