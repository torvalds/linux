//===- LineTable.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_GSYM_LINETABLE_H
#define LLVM_DEBUGINFO_GSYM_LINETABLE_H

#include "llvm/DebugInfo/GSYM/LineEntry.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <vector>

namespace llvm {
namespace gsym {

struct FunctionInfo;
class FileWriter;

/// LineTable class contains deserialized versions of line tables for each
/// function's address ranges.
///
/// When saved to disk, the line table is encoded using a modified version of
/// the DWARF line tables that only tracks address to source file and line.
///
/// ENCODING
///
/// The line table starts with a small prolog that contains the following
/// values:
///
/// ENCODING NAME        DESCRIPTION
/// ======== =========== ====================================================
/// SLEB     MinDelta    The min line delta for special opcodes that  advance
///                      the address and line number.
/// SLEB     MaxDelta    The max line delta for single byte opcodes that
///                      advance the address and line number.
/// ULEB     FirstLine   The value of the first source line number to
///                      initialize the LineEntry with.
///
/// Once these prolog items are read, we initialize a LineEntry struct with
/// the start address of the function from the FunctionInfo's address range,
/// a default file index of 1, and the line number set to "FirstLine" from
/// the prolog above:
///
///   LineEntry Row(BaseAddr, 1, FirstLine);
///
/// The line table state machine is now initialized and ready to be parsed.
/// The stream that follows this encodes the line entries in a compact
/// form. Some opcodes cause "Row" to be modified and some opcodes may also
/// push "Row" onto the end of the "LineTable.Lines" vector. The end result
/// is a vector of LineEntry structs that is sorted in ascending address
/// order.
///
/// NORMAL OPCODES
///
/// The opcodes 0 through 3 are normal in opcodes. Their encoding and
/// descriptions are listed below:
///
/// ENCODING ENUMERATION       VALUE DESCRIPTION
/// ======== ================  ===== ========================================
///          LTOC_EndSequence  0x00  Parsing is done.
/// ULEB     LTOC_SetFile      0x01  Row.File = ULEB
/// ULEB     LTOC_AdvancePC    0x02  Row.Addr += ULEB, push "Row".
/// SLEB     LTOC_AdvanceLine  0x03  Row.Line += SLEB
///          LTOC_FirstSpecial 0x04  First special opcode (see SPECIAL
///                                  OPCODES below).
///
/// SPECIAL OPCODES
///
/// Opcodes LTOC_FirstSpecial through 255 are special opcodes that always
/// increment both the Row.Addr and Row.Line and push "Row" onto the
/// LineEntry.Lines array. They do this by using some of the bits to
/// increment/decrement the source line number, and some of the bits to
/// increment the address. Line numbers can go up or down when making line
/// tables, where addresses always only increase since line tables are sorted
/// by address.
///
/// In order to calculate the amount to increment the line and address for
/// these special opcodes, we calculate the number of values reserved for the
/// line increment/decrement using the "MinDelta" and "MaxDelta" from the
/// prolog:
///
///     const int64_t LineRange = MaxDelta - MinDelta + 1;
///
/// Then we can adjust the opcode to not include any of the normal opcodes:
///
///     const uint8_t AdjustedOp = Opcode - LTOC_FirstSpecial;
///
/// And we can calculate the line offset, and address offset:
///
///     const int64_t LineDelta = MinDelta + (AdjustedOp % LineRange);
///     const uint64_t AddrDelta = (AdjustedOp / LineRange);
///
/// And use these to modify our "Row":
///
///     Row.Line += LineDelta;
///     Row.Addr += AddrDelta;
///
/// And push a row onto the line table:
///
///     Lines.push_back(Row);
///
/// This is verify similar to the way that DWARF encodes its line tables. The
/// only difference is the DWARF line tables have more normal opcodes and the
/// "Row" contains more members, like source column number, bools for end of
/// prologue, beginnging of epilogue, is statement and many others. There are
/// also more complex rules that happen for the extra normal opcodes. By
/// leaving these extra opcodes out, we leave more bits for the special
/// opcodes that allows us to encode line tables in fewer bytes than standard
/// DWARF encodings.
///
/// Opcodes that will push "Row" onto the LineEntry.Lines include the
/// LTOC_AdvancePC opcode and all special opcodes. All other opcodes
/// only modify the current "Row", or cause the line table to end.
class LineTable {
  typedef std::vector<gsym::LineEntry> Collection;
  Collection Lines; ///< All line entries in the line table.
public:
  /// Lookup a single address within a line table's data.
  ///
  /// Clients have the option to decode an entire line table using
  /// LineTable::decode() or just find a single matching entry using this
  /// function. The benefit of using this function is that parsed LineEntry
  /// objects that do not match will not be stored in an array. This will avoid
  /// memory allocation costs and parsing can stop once a match has been found.
  ///
  /// \param Data The binary stream to read the data from. This object must
  /// have the data for the LineTable object starting at offset zero. The data
  /// can contain more data than needed.
  ///
  /// \param BaseAddr The base address to use when decoding the line table.
  /// This will be the FunctionInfo's start address and will be used to
  /// initialize the line table row prior to parsing any opcodes.
  ///
  /// \returns An LineEntry object if a match is found, error otherwise.
  static Expected<LineEntry> lookup(DataExtractor &Data, uint64_t BaseAddr,
                                    uint64_t Addr);

  /// Decode an LineTable object from a binary data stream.
  ///
  /// \param Data The binary stream to read the data from. This object must
  /// have the data for the LineTable object starting at offset zero. The data
  /// can contain more data than needed.
  ///
  /// \param BaseAddr The base address to use when decoding the line table.
  /// This will be the FunctionInfo's start address and will be used to
  /// initialize the line table row prior to parsing any opcodes.
  ///
  /// \returns An LineTable or an error describing the issue that was
  /// encountered during decoding.
  static llvm::Expected<LineTable> decode(DataExtractor &Data,
                                          uint64_t BaseAddr);
  /// Encode this LineTable object into FileWriter stream.
  ///
  /// \param O The binary stream to write the data to at the current file
  /// position.
  ///
  /// \param BaseAddr The base address to use when decoding the line table.
  /// This will be the FunctionInfo's start address.
  ///
  /// \returns An error object that indicates success or failure or the
  /// encoding process.
  llvm::Error encode(FileWriter &O, uint64_t BaseAddr) const;
  bool empty() const { return Lines.empty(); }
  void clear() { Lines.clear(); }
  /// Return the first line entry if the line table isn't empty.
  ///
  /// \returns An optional line entry with the first line entry if the line
  /// table isn't empty, or std::nullopt if the line table is emtpy.
  std::optional<LineEntry> first() const {
    if (Lines.empty())
      return std::nullopt;
    return Lines.front();
  }
  /// Return the last line entry if the line table isn't empty.
  ///
  /// \returns An optional line entry with the last line entry if the line
  /// table isn't empty, or std::nullopt if the line table is emtpy.
  std::optional<LineEntry> last() const {
    if (Lines.empty())
      return std::nullopt;
    return Lines.back();
  }
  void push(const LineEntry &LE) {
    Lines.push_back(LE);
  }
  size_t isValid() const {
    return !Lines.empty();
  }
  size_t size() const {
    return Lines.size();
  }
  LineEntry &get(size_t i) {
    assert(i < Lines.size());
    return Lines[i];
  }
  const LineEntry &get(size_t i) const {
    assert(i < Lines.size());
    return Lines[i];
  }
  LineEntry &operator[](size_t i) {
    return get(i);
  }
  const LineEntry &operator[](size_t i) const {
    return get(i);
  }
  bool operator==(const LineTable &RHS) const {
    return Lines == RHS.Lines;
  }
  bool operator!=(const LineTable &RHS) const {
    return Lines != RHS.Lines;
  }
  bool operator<(const LineTable &RHS) const {
    const auto LHSSize = Lines.size();
    const auto RHSSize = RHS.Lines.size();
    if (LHSSize == RHSSize)
      return Lines < RHS.Lines;
    return LHSSize < RHSSize;
  }
  Collection::const_iterator begin() const { return Lines.begin(); }
  Collection::const_iterator end() const { return Lines.end(); }

};

raw_ostream &operator<<(raw_ostream &OS, const gsym::LineTable &LT);

} // namespace gsym
} // namespace llvm

#endif // LLVM_DEBUGINFO_GSYM_LINETABLE_H
