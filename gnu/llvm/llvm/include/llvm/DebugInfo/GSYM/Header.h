//===- Header.h -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_GSYM_HEADER_H
#define LLVM_DEBUGINFO_GSYM_HEADER_H

#include "llvm/Support/Error.h"

#include <cstddef>
#include <cstdint>

namespace llvm {
class raw_ostream;
class DataExtractor;

namespace gsym {
class FileWriter;

constexpr uint32_t GSYM_MAGIC = 0x4753594d; // 'GSYM'
constexpr uint32_t GSYM_CIGAM = 0x4d595347; // 'MYSG'
constexpr uint32_t GSYM_VERSION = 1;
constexpr size_t GSYM_MAX_UUID_SIZE = 20;

/// The GSYM header.
///
/// The GSYM header is found at the start of a stand alone GSYM file, or as
/// the first bytes in a section when GSYM is contained in a section of an
/// executable file (ELF, mach-o, COFF).
///
/// The structure is encoded exactly as it appears in the structure definition
/// with no gaps between members. Alignment should not change from system to
/// system as the members were laid out so that they shouldn't align
/// differently on different architectures.
///
/// When endianness of the system loading a GSYM file matches, the file can
/// be mmap'ed in and a pointer to the header can be cast to the first bytes
/// of the file (stand alone GSYM file) or section data (GSYM in a section).
/// When endianness is swapped, the Header::decode() function should be used to
/// decode the header.
struct Header {
  /// The magic bytes should be set to GSYM_MAGIC. This helps detect if a file
  /// is a GSYM file by scanning the first 4 bytes of a file or section.
  /// This value might appear byte swapped
  uint32_t Magic;
  /// The version can number determines how the header is decoded and how each
  /// InfoType in FunctionInfo is encoded/decoded. As version numbers increase,
  /// "Magic" and "Version" members should always appear at offset zero and 4
  /// respectively to ensure clients figure out if they can parse the format.
  uint16_t Version;
  /// The size in bytes of each address offset in the address offsets table.
  uint8_t AddrOffSize;
  /// The size in bytes of the UUID encoded in the "UUID" member.
  uint8_t UUIDSize;
  /// The 64 bit base address that all address offsets in the address offsets
  /// table are relative to. Storing a full 64 bit address allows our address
  /// offsets table to be smaller on disk.
  uint64_t BaseAddress;
  /// The number of addresses stored in the address offsets table.
  uint32_t NumAddresses;
  /// The file relative offset of the start of the string table for strings
  /// contained in the GSYM file. If the GSYM in contained in a stand alone
  /// file this will be the file offset of the start of the string table. If
  /// the GSYM is contained in a section within an executable file, this can
  /// be the offset of the first string used in the GSYM file and can possibly
  /// span one or more executable string tables. This allows the strings to
  /// share string tables in an ELF or mach-o file.
  uint32_t StrtabOffset;
  /// The size in bytes of the string table. For a stand alone GSYM file, this
  /// will be the exact size in bytes of the string table. When the GSYM data
  /// is in a section within an executable file, this size can span one or more
  /// sections that contains strings. This allows any strings that are already
  /// stored in the executable file to be re-used, and any extra strings could
  /// be added to another string table and the string table offset and size
  /// can be set to span all needed string tables.
  uint32_t StrtabSize;
  /// The UUID of the original executable file. This is stored to allow
  /// matching a GSYM file to an executable file when symbolication is
  /// required. Only the first "UUIDSize" bytes of the UUID are valid. Any
  /// bytes in the UUID value that appear after the first UUIDSize bytes should
  /// be set to zero.
  uint8_t UUID[GSYM_MAX_UUID_SIZE];

  /// Check if a header is valid and return an error if anything is wrong.
  ///
  /// This function can be used prior to encoding a header to ensure it is
  /// valid, or after decoding a header to ensure it is valid and supported.
  ///
  /// Check a correctly byte swapped header for errors:
  ///   - check magic value
  ///   - check that version number is supported
  ///   - check that the address offset size is supported
  ///   - check that the UUID size is valid
  ///
  /// \returns An error if anything is wrong in the header, or Error::success()
  /// if there are no errors.
  llvm::Error checkForError() const;

  /// Decode an object from a binary data stream.
  ///
  /// \param Data The binary stream to read the data from. This object must
  /// have the data for the object starting at offset zero. The data
  /// can contain more data than needed.
  ///
  /// \returns A Header or an error describing the issue that was
  /// encountered during decoding.
  static llvm::Expected<Header> decode(DataExtractor &Data);

  /// Encode this object into FileWriter stream.
  ///
  /// \param O The binary stream to write the data to at the current file
  /// position.
  ///
  /// \returns An error object that indicates success or failure of the
  /// encoding process.
  llvm::Error encode(FileWriter &O) const;
};

bool operator==(const Header &LHS, const Header &RHS);
raw_ostream &operator<<(raw_ostream &OS, const llvm::gsym::Header &H);

} // namespace gsym
} // namespace llvm

#endif // LLVM_DEBUGINFO_GSYM_HEADER_H
