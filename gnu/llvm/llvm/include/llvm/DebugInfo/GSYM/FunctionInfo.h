//===- FunctionInfo.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_GSYM_FUNCTIONINFO_H
#define LLVM_DEBUGINFO_GSYM_FUNCTIONINFO_H

#include "llvm/ADT/SmallString.h"
#include "llvm/DebugInfo/GSYM/ExtractRanges.h"
#include "llvm/DebugInfo/GSYM/InlineInfo.h"
#include "llvm/DebugInfo/GSYM/LineTable.h"
#include "llvm/DebugInfo/GSYM/LookupResult.h"
#include "llvm/DebugInfo/GSYM/StringTable.h"
#include <cstdint>

namespace llvm {
class raw_ostream;

namespace gsym {

class GsymReader;
/// Function information in GSYM files encodes information for one contiguous
/// address range. If a function has discontiguous address ranges, they will
/// need to be encoded using multiple FunctionInfo objects.
///
/// ENCODING
///
/// The function information gets the function start address as an argument
/// to the FunctionInfo::decode(...) function. This information is calculated
/// from the GSYM header and an address offset from the GSYM address offsets
/// table. The encoded FunctionInfo information must be aligned to a 4 byte
/// boundary.
///
/// The encoded data for a FunctionInfo starts with fixed data that all
/// function info objects have:
///
/// ENCODING  NAME        DESCRIPTION
/// ========= =========== ====================================================
/// uint32_t  Size        The size in bytes of this function.
/// uint32_t  Name        The string table offset of the function name.
///
/// The optional data in a FunctionInfo object follows this fixed information
/// and consists of a stream of tuples that consist of:
///
/// ENCODING  NAME        DESCRIPTION
/// ========= =========== ====================================================
/// uint32_t  InfoType    An "InfoType" enumeration that describes the type
///                       of optional data that is encoded.
/// uint32_t  InfoLength  The size in bytes of the encoded data that
///                       immediately follows this length if this value is
///                       greater than zero.
/// uint8_t[] InfoData    Encoded bytes that represent the data for the
///                       "InfoType". These bytes are only present if
///                       "InfoLength" is greater than zero.
///
/// The "InfoType" is an enumeration:
///
///   enum InfoType {
///     EndOfList = 0u,
///     LineTableInfo = 1u,
///     InlineInfo = 2u
///   };
///
/// This stream of tuples is terminated by a "InfoType" whose value is
/// InfoType::EndOfList and a zero for "InfoLength". This signifies the end of
/// the optional information list. This format allows us to add new optional
/// information data to a FunctionInfo object over time and allows older
/// clients to still parse the format and skip over any data that they don't
/// understand or want to parse.
///
/// So the function information encoding essientially looks like:
///
/// struct {
///   uint32_t Size;
///   uint32_t Name;
///   struct {
///     uint32_t InfoType;
///     uint32_t InfoLength;
///     uint8_t InfoData[InfoLength];
///   }[N];
/// }
///
/// Where "N" is the number of tuples.
struct FunctionInfo {
  AddressRange Range;
  uint32_t Name; ///< String table offset in the string table.
  std::optional<LineTable> OptLineTable;
  std::optional<InlineInfo> Inline;
  /// If we encode a FunctionInfo during segmenting so we know its size, we can
  /// cache that encoding here so we don't need to re-encode it when saving the
  /// GSYM file.
  SmallString<32> EncodingCache;

  FunctionInfo(uint64_t Addr = 0, uint64_t Size = 0, uint32_t N = 0)
      : Range(Addr, Addr + Size), Name(N) {}

  /// Query if a FunctionInfo has rich debug info.
  ///
  /// \returns A bool that indicates if this object has something else than
  /// range and name. When converting information from a symbol table and from
  /// debug info, we might end up with multiple FunctionInfo objects for the
  /// same range and we need to be able to tell which one is the better object
  /// to use.
  bool hasRichInfo() const { return OptLineTable || Inline; }

  /// Query if a FunctionInfo object is valid.
  ///
  /// Address and size can be zero and there can be no line entries for a
  /// symbol so the only indication this entry is valid is if the name is
  /// not zero. This can happen when extracting information from symbol
  /// tables that do not encode symbol sizes. In that case only the
  /// address and name will be filled in.
  ///
  /// \returns A boolean indicating if this FunctionInfo is valid.
  bool isValid() const {
    return Name != 0;
  }

  /// Decode an object from a binary data stream.
  ///
  /// \param Data The binary stream to read the data from. This object must
  /// have the data for the object starting at offset zero. The data
  /// can contain more data than needed.
  ///
  /// \param BaseAddr The FunctionInfo's start address and will be used as the
  /// base address when decoding any contained information like the line table
  /// and the inline info.
  ///
  /// \returns An FunctionInfo or an error describing the issue that was
  /// encountered during decoding.
  static llvm::Expected<FunctionInfo> decode(DataExtractor &Data,
                                             uint64_t BaseAddr);

  /// Encode this object into FileWriter stream.
  ///
  /// \param O The binary stream to write the data to at the current file
  /// position.
  ///
  /// \returns An error object that indicates failure or the offset of the
  /// function info that was successfully written into the stream.
  llvm::Expected<uint64_t> encode(FileWriter &O) const;

  /// Encode this function info into the internal byte cache and return the size
  /// in bytes.
  ///
  /// When segmenting GSYM files we need to know how big each FunctionInfo will
  /// encode into so we can generate segments of the right size. We don't want
  /// to have to encode a FunctionInfo twice, so we can cache the encoded bytes
  /// and re-use then when calling FunctionInfo::encode(...).
  ///
  /// \returns The size in bytes of the FunctionInfo if it were to be encoded
  /// into a byte stream.
  uint64_t cacheEncoding();

  /// Lookup an address within a FunctionInfo object's data stream.
  ///
  /// Instead of decoding an entire FunctionInfo object when doing lookups,
  /// we can decode only the information we need from the FunctionInfo's data
  /// for the specific address. The lookup result information is returned as
  /// a LookupResult.
  ///
  /// \param Data The binary stream to read the data from. This object must
  /// have the data for the object starting at offset zero. The data
  /// can contain more data than needed.
  ///
  /// \param GR The GSYM reader that contains the string and file table that
  /// will be used to fill in information in the returned result.
  ///
  /// \param FuncAddr The function start address decoded from the GsymReader.
  ///
  /// \param Addr The address to lookup.
  ///
  /// \returns An LookupResult or an error describing the issue that was
  /// encountered during decoding. An error should only be returned if the
  /// address is not contained in the FunctionInfo or if the data is corrupted.
  static llvm::Expected<LookupResult> lookup(DataExtractor &Data,
                                             const GsymReader &GR,
                                             uint64_t FuncAddr,
                                             uint64_t Addr);

  uint64_t startAddress() const { return Range.start(); }
  uint64_t endAddress() const { return Range.end(); }
  uint64_t size() const { return Range.size(); }

  void clear() {
    Range = {0, 0};
    Name = 0;
    OptLineTable = std::nullopt;
    Inline = std::nullopt;
  }
};

inline bool operator==(const FunctionInfo &LHS, const FunctionInfo &RHS) {
  return LHS.Range == RHS.Range && LHS.Name == RHS.Name &&
         LHS.OptLineTable == RHS.OptLineTable && LHS.Inline == RHS.Inline;
}
inline bool operator!=(const FunctionInfo &LHS, const FunctionInfo &RHS) {
  return !(LHS == RHS);
}
/// This sorting will order things consistently by address range first, but
/// then followed by increasing levels of debug info like inline information
/// and line tables. We might end up with a FunctionInfo from debug info that
/// will have the same range as one from the symbol table, but we want to
/// quickly be able to sort and use the best version when creating the final
/// GSYM file. This function compares the inline information as we have seen
/// cases where LTO can generate a wide array of differing inline information,
/// mostly due to messing up the address ranges for inlined functions, so the
/// inline information with the most entries will appeear last. If the inline
/// information match, either by both function infos not having any or both
/// being exactly the same, we will then compare line tables. Comparing line
/// tables allows the entry with the most line entries to appear last. This
/// ensures we are able to save the FunctionInfo with the most debug info into
/// the GSYM file.
inline bool operator<(const FunctionInfo &LHS, const FunctionInfo &RHS) {
  // First sort by address range
  if (LHS.Range != RHS.Range)
    return LHS.Range < RHS.Range;
  if (LHS.Inline == RHS.Inline)
    return LHS.OptLineTable < RHS.OptLineTable;
  return LHS.Inline < RHS.Inline;
}

raw_ostream &operator<<(raw_ostream &OS, const FunctionInfo &R);

} // namespace gsym
} // namespace llvm

#endif // LLVM_DEBUGINFO_GSYM_FUNCTIONINFO_H
