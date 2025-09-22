//===--- Encoding.h - Format C++ code ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Contains functions for text encoding manipulation. Supports UTF-8,
/// 8-bit encodings and escape sequences in C++ string literals.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_FORMAT_ENCODING_H
#define LLVM_CLANG_LIB_FORMAT_ENCODING_H

#include "clang/Basic/LLVM.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Unicode.h"

namespace clang {
namespace format {
namespace encoding {

enum Encoding {
  Encoding_UTF8,
  Encoding_Unknown // We treat all other encodings as 8-bit encodings.
};

/// Detects encoding of the Text. If the Text can be decoded using UTF-8,
/// it is considered UTF8, otherwise we treat it as some 8-bit encoding.
inline Encoding detectEncoding(StringRef Text) {
  const llvm::UTF8 *Ptr = reinterpret_cast<const llvm::UTF8 *>(Text.begin());
  const llvm::UTF8 *BufEnd = reinterpret_cast<const llvm::UTF8 *>(Text.end());
  if (llvm::isLegalUTF8String(&Ptr, BufEnd))
    return Encoding_UTF8;
  return Encoding_Unknown;
}

/// Returns the number of columns required to display the \p Text on a
/// generic Unicode-capable terminal. Text is assumed to use the specified
/// \p Encoding.
inline unsigned columnWidth(StringRef Text, Encoding Encoding) {
  if (Encoding == Encoding_UTF8) {
    int ContentWidth = llvm::sys::unicode::columnWidthUTF8(Text);
    // FIXME: Figure out the correct way to handle this in the presence of both
    // printable and unprintable multi-byte UTF-8 characters. Falling back to
    // returning the number of bytes may cause problems, as columnWidth suddenly
    // becomes non-additive.
    if (ContentWidth >= 0)
      return ContentWidth;
  }
  return Text.size();
}

/// Returns the number of columns required to display the \p Text,
/// starting from the \p StartColumn on a terminal with the \p TabWidth. The
/// text is assumed to use the specified \p Encoding.
inline unsigned columnWidthWithTabs(StringRef Text, unsigned StartColumn,
                                    unsigned TabWidth, Encoding Encoding) {
  unsigned TotalWidth = 0;
  StringRef Tail = Text;
  for (;;) {
    StringRef::size_type TabPos = Tail.find('\t');
    if (TabPos == StringRef::npos)
      return TotalWidth + columnWidth(Tail, Encoding);
    TotalWidth += columnWidth(Tail.substr(0, TabPos), Encoding);
    if (TabWidth)
      TotalWidth += TabWidth - (TotalWidth + StartColumn) % TabWidth;
    Tail = Tail.substr(TabPos + 1);
  }
}

/// Gets the number of bytes in a sequence representing a single
/// codepoint and starting with FirstChar in the specified Encoding.
inline unsigned getCodePointNumBytes(char FirstChar, Encoding Encoding) {
  switch (Encoding) {
  case Encoding_UTF8:
    return llvm::getNumBytesForUTF8(FirstChar);
  default:
    return 1;
  }
}

inline bool isOctDigit(char c) { return '0' <= c && c <= '7'; }

inline bool isHexDigit(char c) {
  return ('0' <= c && c <= '9') || ('a' <= c && c <= 'f') ||
         ('A' <= c && c <= 'F');
}

/// Gets the length of an escape sequence inside a C++ string literal.
/// Text should span from the beginning of the escape sequence (starting with a
/// backslash) to the end of the string literal.
inline unsigned getEscapeSequenceLength(StringRef Text) {
  assert(Text[0] == '\\');
  if (Text.size() < 2)
    return 1;

  switch (Text[1]) {
  case 'u':
    return 6;
  case 'U':
    return 10;
  case 'x': {
    unsigned I = 2; // Point after '\x'.
    while (I < Text.size() && isHexDigit(Text[I]))
      ++I;
    return I;
  }
  default:
    if (isOctDigit(Text[1])) {
      unsigned I = 1;
      while (I < Text.size() && I < 4 && isOctDigit(Text[I]))
        ++I;
      return I;
    }
    return 1 + llvm::getNumBytesForUTF8(Text[1]);
  }
}

} // namespace encoding
} // namespace format
} // namespace clang

#endif
