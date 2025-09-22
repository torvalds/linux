//===-- RISCVISAUtils.cpp - RISC-V ISA Utilities --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Utilities shared by TableGen and RISCVISAInfo.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/RISCVISAUtils.h"
#include "llvm/ADT/StringExtras.h"
#include <cassert>

using namespace llvm;

// We rank extensions in the following order:
// -Single letter extensions in canonical order.
// -Unknown single letter extensions in alphabetical order.
// -Multi-letter extensions starting with 'z' sorted by canonical order of
//  the second letter then sorted alphabetically.
// -Multi-letter extensions starting with 's' in alphabetical order.
// -(TODO) Multi-letter extensions starting with 'zxm' in alphabetical order.
// -X extensions in alphabetical order.
// -Unknown multi-letter extensions in alphabetical order.
// These flags are used to indicate the category. The first 6 bits store the
// single letter extension rank for single letter and multi-letter extensions
// starting with 'z'.
enum RankFlags {
  RF_Z_EXTENSION = 1 << 6,
  RF_S_EXTENSION = 2 << 6,
  RF_X_EXTENSION = 3 << 6,
  RF_UNKNOWN_MULTILETTER_EXTENSION = 4 << 6,
};

// Get the rank for single-letter extension, lower value meaning higher
// priority.
static unsigned singleLetterExtensionRank(char Ext) {
  assert(isLower(Ext));
  switch (Ext) {
  case 'i':
    return 0;
  case 'e':
    return 1;
  }

  size_t Pos = RISCVISAUtils::AllStdExts.find(Ext);
  if (Pos != StringRef::npos)
    return Pos + 2; // Skip 'e' and 'i' from above.

  // If we got an unknown extension letter, then give it an alphabetical
  // order, but after all known standard extensions.
  return 2 + RISCVISAUtils::AllStdExts.size() + (Ext - 'a');
}

// Get the rank for multi-letter extension, lower value meaning higher
// priority/order in canonical order.
static unsigned getExtensionRank(const std::string &ExtName) {
  assert(ExtName.size() >= 1);
  switch (ExtName[0]) {
  case 's':
    return RF_S_EXTENSION;
  case 'z':
    assert(ExtName.size() >= 2);
    // `z` extension must be sorted by canonical order of second letter.
    // e.g. zmx has higher rank than zax.
    return RF_Z_EXTENSION | singleLetterExtensionRank(ExtName[1]);
  case 'x':
    return RF_X_EXTENSION;
  default:
    if (ExtName.size() == 1)
      return singleLetterExtensionRank(ExtName[0]);
    return RF_UNKNOWN_MULTILETTER_EXTENSION;
  }
}

// Compare function for extension.
// Only compare the extension name, ignore version comparison.
bool llvm::RISCVISAUtils::compareExtension(const std::string &LHS,
                                           const std::string &RHS) {
  unsigned LHSRank = getExtensionRank(LHS);
  unsigned RHSRank = getExtensionRank(RHS);

  // If the ranks differ, pick the lower rank.
  if (LHSRank != RHSRank)
    return LHSRank < RHSRank;

  // If the rank is same, it must be sorted by lexicographic order.
  return LHS < RHS;
}
