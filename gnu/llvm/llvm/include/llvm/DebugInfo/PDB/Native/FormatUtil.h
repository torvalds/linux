//===- FormatUtil.h ------------------------------------------- *- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_FORMATUTIL_H
#define LLVM_DEBUGINFO_PDB_NATIVE_FORMATUTIL_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/FormatAdapters.h"
#include "llvm/Support/FormatVariadic.h"

#include <string>
#include <type_traits>

namespace llvm {
namespace pdb {

#define PUSH_MASKED_FLAG(Enum, Mask, TheOpt, Value, Text)                      \
  if (Enum::TheOpt == (Value & Mask))                                          \
    Opts.push_back(Text);

#define PUSH_FLAG(Enum, TheOpt, Value, Text)                                   \
  PUSH_MASKED_FLAG(Enum, Enum::TheOpt, TheOpt, Value, Text)

#define RETURN_CASE(Enum, X, Ret)                                              \
  case Enum::X:                                                                \
    return Ret;

template <typename T> std::string formatUnknownEnum(T Value) {
  return formatv("unknown ({0})", llvm::to_underlying(Value)).str();
}

std::string formatSegmentOffset(uint16_t Segment, uint32_t Offset);

enum class CharacteristicStyle {
  HeaderDefinition, // format as windows header definition
  Descriptive,      // format as human readable words
};
std::string formatSectionCharacteristics(
    uint32_t IndentLevel, uint32_t C, uint32_t FlagsPerLine,
    StringRef Separator,
    CharacteristicStyle Style = CharacteristicStyle::HeaderDefinition);

std::string typesetItemList(ArrayRef<std::string> Opts, uint32_t IndentLevel,
                            uint32_t GroupSize, StringRef Sep);

std::string typesetStringList(uint32_t IndentLevel,
                              ArrayRef<StringRef> Strings);

std::string formatChunkKind(codeview::DebugSubsectionKind Kind,
                            bool Friendly = true);
std::string formatSymbolKind(codeview::SymbolKind K);
std::string formatTypeLeafKind(codeview::TypeLeafKind K);

/// Returns the number of digits in the given integer.
inline int NumDigits(uint64_t N) {
  if (N < 10ULL)
    return 1;
  if (N < 100ULL)
    return 2;
  if (N < 1000ULL)
    return 3;
  if (N < 10000ULL)
    return 4;
  if (N < 100000ULL)
    return 5;
  if (N < 1000000ULL)
    return 6;
  if (N < 10000000ULL)
    return 7;
  if (N < 100000000ULL)
    return 8;
  if (N < 1000000000ULL)
    return 9;
  if (N < 10000000000ULL)
    return 10;
  if (N < 100000000000ULL)
    return 11;
  if (N < 1000000000000ULL)
    return 12;
  if (N < 10000000000000ULL)
    return 13;
  if (N < 100000000000000ULL)
    return 14;
  if (N < 1000000000000000ULL)
    return 15;
  if (N < 10000000000000000ULL)
    return 16;
  if (N < 100000000000000000ULL)
    return 17;
  if (N < 1000000000000000000ULL)
    return 18;
  if (N < 10000000000000000000ULL)
    return 19;
  return 20;
}

namespace detail {
template <typename T>
struct EndianAdapter final
    : public FormatAdapter<support::detail::packed_endian_specific_integral<
          T, llvm::endianness::little, support::unaligned>> {
  using EndianType = support::detail::packed_endian_specific_integral<
      T, llvm::endianness::little, support::unaligned>;

  explicit EndianAdapter(EndianType &&Item)
      : FormatAdapter<EndianType>(std::move(Item)) {}

  void format(llvm::raw_ostream &Stream, StringRef Style) override {
    format_provider<T>::format(static_cast<T>(this->Item), Stream, Style);
  }
};
} // namespace detail

template <typename T>
detail::EndianAdapter<T> fmtle(support::detail::packed_endian_specific_integral<
                               T, llvm::endianness::little, support::unaligned>
                                   Value) {
  return detail::EndianAdapter<T>(std::move(Value));
}
} // namespace pdb
} // namespace llvm
#endif
