//===- lib/MC/MCSectionCOFF.cpp - COFF Code Section Representation --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCSectionCOFF.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

using namespace llvm;

// shouldOmitSectionDirective - Decides whether a '.section' directive
// should be printed before the section name
bool MCSectionCOFF::shouldOmitSectionDirective(StringRef Name,
                                               const MCAsmInfo &MAI) const {
  if (COMDATSymbol)
    return false;

  // FIXME: Does .section .bss/.data/.text work everywhere??
  if (Name == ".text" || Name == ".data" || Name == ".bss")
    return true;

  return false;
}

void MCSectionCOFF::setSelection(int Selection) const {
  assert(Selection != 0 && "invalid COMDAT selection type");
  this->Selection = Selection;
  Characteristics |= COFF::IMAGE_SCN_LNK_COMDAT;
}

void MCSectionCOFF::printSwitchToSection(const MCAsmInfo &MAI, const Triple &T,
                                         raw_ostream &OS,
                                         uint32_t Subsection) const {
  // standard sections don't require the '.section'
  if (shouldOmitSectionDirective(getName(), MAI)) {
    OS << '\t' << getName() << '\n';
    return;
  }

  OS << "\t.section\t" << getName() << ",\"";
  if (getCharacteristics() & COFF::IMAGE_SCN_CNT_INITIALIZED_DATA)
    OS << 'd';
  if (getCharacteristics() & COFF::IMAGE_SCN_CNT_UNINITIALIZED_DATA)
    OS << 'b';
  if (getCharacteristics() & COFF::IMAGE_SCN_MEM_EXECUTE)
    OS << 'x';
  if (getCharacteristics() & COFF::IMAGE_SCN_MEM_WRITE)
    OS << 'w';
  else if (getCharacteristics() & COFF::IMAGE_SCN_MEM_READ)
    OS << 'r';
  else
    OS << 'y';
  if (getCharacteristics() & COFF::IMAGE_SCN_LNK_REMOVE)
    OS << 'n';
  if (getCharacteristics() & COFF::IMAGE_SCN_MEM_SHARED)
    OS << 's';
  if ((getCharacteristics() & COFF::IMAGE_SCN_MEM_DISCARDABLE) &&
      !isImplicitlyDiscardable(getName()))
    OS << 'D';
  if (getCharacteristics() & COFF::IMAGE_SCN_LNK_INFO)
    OS << 'i';
  OS << '"';

  if (getCharacteristics() & COFF::IMAGE_SCN_LNK_COMDAT) {
    if (COMDATSymbol)
      OS << ",";
    else
      OS << "\n\t.linkonce\t";
    switch (Selection) {
      case COFF::IMAGE_COMDAT_SELECT_NODUPLICATES:
        OS << "one_only";
        break;
      case COFF::IMAGE_COMDAT_SELECT_ANY:
        OS << "discard";
        break;
      case COFF::IMAGE_COMDAT_SELECT_SAME_SIZE:
        OS << "same_size";
        break;
      case COFF::IMAGE_COMDAT_SELECT_EXACT_MATCH:
        OS << "same_contents";
        break;
      case COFF::IMAGE_COMDAT_SELECT_ASSOCIATIVE:
        OS << "associative";
        break;
      case COFF::IMAGE_COMDAT_SELECT_LARGEST:
        OS << "largest";
        break;
      case COFF::IMAGE_COMDAT_SELECT_NEWEST:
        OS << "newest";
        break;
      default:
        assert(false && "unsupported COFF selection type");
        break;
    }
    if (COMDATSymbol) {
      OS << ",";
      COMDATSymbol->print(OS, &MAI);
    }
  }
  OS << '\n';
}

bool MCSectionCOFF::useCodeAlign() const { return isText(); }

StringRef MCSectionCOFF::getVirtualSectionKind() const {
  return "IMAGE_SCN_CNT_UNINITIALIZED_DATA";
}
