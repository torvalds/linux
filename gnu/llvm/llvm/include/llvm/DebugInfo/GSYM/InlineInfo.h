//===- InlineInfo.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_GSYM_INLINEINFO_H
#define LLVM_DEBUGINFO_GSYM_INLINEINFO_H

#include "llvm/DebugInfo/GSYM/ExtractRanges.h"
#include "llvm/DebugInfo/GSYM/LineEntry.h"
#include "llvm/DebugInfo/GSYM/LookupResult.h"
#include "llvm/Support/Error.h"
#include <stdint.h>
#include <vector>

namespace llvm {
class raw_ostream;

namespace gsym {

class GsymReader;
/// Inline information stores the name of the inline function along with
/// an array of address ranges. It also stores the call file and call line
/// that called this inline function. This allows us to unwind inline call
/// stacks back to the inline or concrete function that called this
/// function. Inlined functions contained in this function are stored in the
/// "Children" variable. All address ranges must be sorted and all address
/// ranges of all children must be contained in the ranges of this function.
/// Any clients that encode information will need to ensure the ranges are
/// all contined correctly or lookups could fail. Add ranges in these objects
/// must be contained in the top level FunctionInfo address ranges as well.
///
/// ENCODING
///
/// When saved to disk, the inline info encodes all ranges to be relative to
/// a parent address range. This will be the FunctionInfo's start address if
/// the InlineInfo is directly contained in a FunctionInfo, or a the start
/// address of the containing parent InlineInfo's first "Ranges" member. This
/// allows address ranges to be efficiently encoded using ULEB128 encodings as
/// we encode the offset and size of each range instead of full addresses. This
/// also makes any encoded addresses easy to relocate as we just need to
/// relocate the FunctionInfo's start address.
///
/// - The AddressRanges member "Ranges" is encoded using an appropriate base
///   address as described above.
/// - UINT8 boolean value that specifies if the InlineInfo object has children.
/// - UINT32 string table offset that points to the name of the inline
///   function.
/// - ULEB128 integer that specifies the file of the call site that called
///   this function.
/// - ULEB128 integer that specifies the source line of the call site that
///   called this function.
/// - if this object has children, enocode each child InlineInfo using the
///   the first address range's start address as the base address.
///
struct InlineInfo {

  uint32_t Name; ///< String table offset in the string table.
  uint32_t CallFile; ///< 1 based file index in the file table.
  uint32_t CallLine; ///< Source line number.
  AddressRanges Ranges;
  std::vector<InlineInfo> Children;
  InlineInfo() : Name(0), CallFile(0), CallLine(0) {}
  void clear() {
    Name = 0;
    CallFile = 0;
    CallLine = 0;
    Ranges.clear();
    Children.clear();
  }
  bool isValid() const { return !Ranges.empty(); }

  using InlineArray = std::vector<const InlineInfo *>;

  /// Lookup a single address within the inline info data.
  ///
  /// Clients have the option to decode an entire InlineInfo object (using
  /// InlineInfo::decode() ) or just find the matching inline info using this
  /// function. The benefit of using this function is that only the information
  /// needed for the lookup will be extracted, other info can be skipped and
  /// parsing can stop as soon as the deepest match is found. This allows
  /// symbolication tools to be fast and efficient and avoid allocation costs
  /// when doing lookups.
  ///
  /// This function will augment the SourceLocations array \a SrcLocs with any
  /// inline information that pertains to \a Addr. If no inline information
  /// exists for \a Addr, then \a SrcLocs will be left untouched. If there is
  /// inline information for \a Addr, then \a SrcLocs will be modifiied to
  /// contain the deepest most inline function's SourceLocation at index zero
  /// in the array and proceed up the concrete function source file and
  /// line at the end of the array.
  ///
  /// \param GR The GSYM reader that contains the string and file table that
  /// will be used to fill in the source locations.
  ///
  /// \param Data The binary stream to read the data from. This object must
  /// have the data for the LineTable object starting at offset zero. The data
  /// can contain more data than needed.
  ///
  /// \param BaseAddr The base address to use when decoding the line table.
  /// This will be the FunctionInfo's start address and will be used to
  /// decode the correct addresses for the inline information.
  ///
  /// \param Addr The address to lookup.
  ///
  /// \param SrcLocs The inline source locations that matches \a Addr. This
  ///                array must be initialized with the matching line entry
  ///                from the line table upon entry. The name of the concrete
  ///                function must be supplied since it will get pushed to
  ///                the last SourceLocation entry and the inline information
  ///                will fill in the source file and line from the inline
  ///                information.
  ///
  /// \returns An error if the inline information is corrupt, or
  ///          Error::success() for all other cases, even when no information
  ///          is added to \a SrcLocs.
  static llvm::Error lookup(const GsymReader &GR, DataExtractor &Data,
                            uint64_t BaseAddr, uint64_t Addr,
                            SourceLocations &SrcLocs);

  /// Lookup an address in the InlineInfo object
  ///
  /// This function is used to symbolicate an inline call stack and can
  /// turn one address in the program into one or more inline call stacks
  /// and have the stack trace show the original call site from
  /// non-inlined code.
  ///
  /// \param Addr the address to lookup
  ///
  /// \returns optional vector of InlineInfo objects that describe the
  /// inline call stack for a given address, false otherwise.
  std::optional<InlineArray> getInlineStack(uint64_t Addr) const;

  /// Decode an InlineInfo object from a binary data stream.
  ///
  /// \param Data The binary stream to read the data from. This object must
  /// have the data for the InlineInfo object starting at offset zero. The data
  /// can contain more data than needed.
  ///
  /// \param BaseAddr The base address to use when decoding all address ranges.
  /// This will be the FunctionInfo's start address if this object is directly
  /// contained in a FunctionInfo object, or the start address of the first
  /// address range in an InlineInfo object of this object is a child of
  /// another InlineInfo object.
  /// \returns An InlineInfo or an error describing the issue that was
  /// encountered during decoding.
  static llvm::Expected<InlineInfo> decode(DataExtractor &Data,
                                           uint64_t BaseAddr);

  /// Encode this InlineInfo object into FileWriter stream.
  ///
  /// \param O The binary stream to write the data to at the current file
  /// position.
  ///
  /// \param BaseAddr The base address to use when encoding all address ranges.
  /// This will be the FunctionInfo's start address if this object is directly
  /// contained in a FunctionInfo object, or the start address of the first
  /// address range in an InlineInfo object of this object is a child of
  /// another InlineInfo object.
  ///
  /// \returns An error object that indicates success or failure or the
  /// encoding process.
  llvm::Error encode(FileWriter &O, uint64_t BaseAddr) const;

  /// Compare InlineInfo objects.
  ///
  /// When comparing InlineInfo objects the item with the most inline functions
  /// wins. If we have two FunctionInfo objects that both have the same address
  /// range and both have valid InlineInfo objects, we want the one with the
  /// most inline functions to win so we save the most information possible
  /// to the GSYM file. We have seen cases where LTO messes up the inline
  /// function information for the same address range, so this helps ensure we
  /// get the most descriptive information we can for an address range.
  bool operator<(const InlineInfo &RHS) const;
};

inline bool operator==(const InlineInfo &LHS, const InlineInfo &RHS) {
  return LHS.Name == RHS.Name && LHS.CallFile == RHS.CallFile &&
         LHS.CallLine == RHS.CallLine && LHS.Ranges == RHS.Ranges &&
         LHS.Children == RHS.Children;
}

raw_ostream &operator<<(raw_ostream &OS, const InlineInfo &FI);

} // namespace gsym
} // namespace llvm

#endif // LLVM_DEBUGINFO_GSYM_INLINEINFO_H
