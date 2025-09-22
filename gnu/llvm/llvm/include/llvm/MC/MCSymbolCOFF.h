//===- MCSymbolCOFF.h -  ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSYMBOLCOFF_H
#define LLVM_MC_MCSYMBOLCOFF_H

#include "llvm/BinaryFormat/COFF.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolTableEntry.h"
#include <cstdint>

namespace llvm {

class MCSymbolCOFF : public MCSymbol {
  /// This corresponds to the e_type field of the COFF symbol.
  mutable uint16_t Type = 0;

  enum SymbolFlags : uint16_t {
    SF_ClassMask = 0x00FF,
    SF_ClassShift = 0,

    SF_SafeSEH = 0x0100,
    SF_WeakExternalCharacteristicsMask = 0x0E00,
    SF_WeakExternalCharacteristicsShift = 9,
  };

public:
  MCSymbolCOFF(const MCSymbolTableEntry *Name, bool isTemporary)
      : MCSymbol(SymbolKindCOFF, Name, isTemporary) {}

  uint16_t getType() const {
    return Type;
  }
  void setType(uint16_t Ty) const {
    Type = Ty;
  }

  uint16_t getClass() const {
    return (getFlags() & SF_ClassMask) >> SF_ClassShift;
  }
  void setClass(uint16_t StorageClass) const {
    modifyFlags(StorageClass << SF_ClassShift, SF_ClassMask);
  }

  COFF::WeakExternalCharacteristics getWeakExternalCharacteristics() const {
    return static_cast<COFF::WeakExternalCharacteristics>((getFlags() & SF_WeakExternalCharacteristicsMask) >>
           SF_WeakExternalCharacteristicsShift);
  }
  void setWeakExternalCharacteristics(COFF::WeakExternalCharacteristics Characteristics) const {
    modifyFlags(Characteristics << SF_WeakExternalCharacteristicsShift,
                SF_WeakExternalCharacteristicsMask);
  }
  void setIsWeakExternal(bool WeakExt) const {
    IsWeakExternal = WeakExt;
  }

  bool isSafeSEH() const {
    return getFlags() & SF_SafeSEH;
  }
  void setIsSafeSEH() const {
    modifyFlags(SF_SafeSEH, SF_SafeSEH);
  }

  static bool classof(const MCSymbol *S) { return S->isCOFF(); }
};

} // end namespace llvm

#endif // LLVM_MC_MCSYMBOLCOFF_H
