//===-- Support/DJB.cpp ---DJB Hash -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for the DJ Bernstein hash function.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/DJB.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Unicode.h"

using namespace llvm;

static UTF32 chopOneUTF32(StringRef &Buffer) {
  UTF32 C;
  const UTF8 *const Begin8Const =
      reinterpret_cast<const UTF8 *>(Buffer.begin());
  const UTF8 *Begin8 = Begin8Const;
  UTF32 *Begin32 = &C;

  // In lenient mode we will always end up with a "reasonable" value in C for
  // non-empty input.
  assert(!Buffer.empty());
  ConvertUTF8toUTF32(&Begin8, reinterpret_cast<const UTF8 *>(Buffer.end()),
                     &Begin32, &C + 1, lenientConversion);
  Buffer = Buffer.drop_front(Begin8 - Begin8Const);
  return C;
}

static StringRef toUTF8(UTF32 C, MutableArrayRef<UTF8> Storage) {
  const UTF32 *Begin32 = &C;
  UTF8 *Begin8 = Storage.begin();

  // The case-folded output should always be a valid unicode character, so use
  // strict mode here.
  ConversionResult CR = ConvertUTF32toUTF8(&Begin32, &C + 1, &Begin8,
                                           Storage.end(), strictConversion);
  assert(CR == conversionOK && "Case folding produced invalid char?");
  (void)CR;
  return StringRef(reinterpret_cast<char *>(Storage.begin()),
                   Begin8 - Storage.begin());
}

static UTF32 foldCharDwarf(UTF32 C) {
  // DWARF v5 addition to the unicode folding rules.
  // Fold "Latin Small Letter Dotless I" and "Latin Capital Letter I With Dot
  // Above" into "i".
  if (C == 0x130 || C == 0x131)
    return 'i';
  return sys::unicode::foldCharSimple(C);
}

static std::optional<uint32_t> fastCaseFoldingDjbHash(StringRef Buffer,
                                                      uint32_t H) {
  bool AllASCII = true;
  for (unsigned char C : Buffer) {
    H = H * 33 + ('A' <= C && C <= 'Z' ? C - 'A' + 'a' : C);
    AllASCII &= C <= 0x7f;
  }
  if (AllASCII)
    return H;
  return std::nullopt;
}

uint32_t llvm::caseFoldingDjbHash(StringRef Buffer, uint32_t H) {
  if (std::optional<uint32_t> Result = fastCaseFoldingDjbHash(Buffer, H))
    return *Result;

  std::array<UTF8, UNI_MAX_UTF8_BYTES_PER_CODE_POINT> Storage;
  while (!Buffer.empty()) {
    UTF32 C = foldCharDwarf(chopOneUTF32(Buffer));
    StringRef Folded = toUTF8(C, Storage);
    H = djbHash(Folded, H);
  }
  return H;
}
