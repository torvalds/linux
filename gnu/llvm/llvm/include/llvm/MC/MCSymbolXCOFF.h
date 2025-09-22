//===- MCSymbolXCOFF.h -  ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_MC_MCSYMBOLXCOFF_H
#define LLVM_MC_MCSYMBOLXCOFF_H

#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/XCOFF.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolTableEntry.h"

namespace llvm {

class MCSectionXCOFF;

class MCSymbolXCOFF : public MCSymbol {

  enum XCOFFSymbolFlags : uint16_t { SF_EHInfo = 0x0001 };

public:
  MCSymbolXCOFF(const MCSymbolTableEntry *Name, bool isTemporary)
      : MCSymbol(SymbolKindXCOFF, Name, isTemporary) {}

  static bool classof(const MCSymbol *S) { return S->isXCOFF(); }

  enum CodeModel : uint8_t { CM_Small, CM_Large };

  static StringRef getUnqualifiedName(StringRef Name) {
    if (Name.back() == ']') {
      StringRef Lhs, Rhs;
      std::tie(Lhs, Rhs) = Name.rsplit('[');
      assert(!Rhs.empty() && "Invalid SMC format in XCOFF symbol.");
      return Lhs;
    }
    return Name;
  }

  void setStorageClass(XCOFF::StorageClass SC) {
    StorageClass = SC;
  };

  XCOFF::StorageClass getStorageClass() const {
    assert(StorageClass && "StorageClass not set on XCOFF MCSymbol.");
    return *StorageClass;
  }

  StringRef getUnqualifiedName() const { return getUnqualifiedName(getName()); }

  MCSectionXCOFF *getRepresentedCsect() const;

  void setRepresentedCsect(MCSectionXCOFF *C);

  void setVisibilityType(XCOFF::VisibilityType SVT) { VisibilityType = SVT; };

  XCOFF::VisibilityType getVisibilityType() const { return VisibilityType; }

  bool hasRename() const { return HasRename; }

  void setSymbolTableName(StringRef STN) {
    SymbolTableName = STN;
    HasRename = true;
  }

  StringRef getSymbolTableName() const {
    if (hasRename())
      return SymbolTableName;
    return getUnqualifiedName();
  }

  bool isEHInfo() const { return getFlags() & SF_EHInfo; }

  void setEHInfo() const { modifyFlags(SF_EHInfo, SF_EHInfo); }

  bool hasPerSymbolCodeModel() const { return PerSymbolCodeModel.has_value(); }

  CodeModel getPerSymbolCodeModel() const {
    assert(hasPerSymbolCodeModel() &&
           "Requested code model for symbol without one");
    return *PerSymbolCodeModel;
  }

  void setPerSymbolCodeModel(MCSymbolXCOFF::CodeModel Model) {
    PerSymbolCodeModel = Model;
  }

private:
  std::optional<XCOFF::StorageClass> StorageClass;
  std::optional<CodeModel> PerSymbolCodeModel;

  MCSectionXCOFF *RepresentedCsect = nullptr;
  XCOFF::VisibilityType VisibilityType = XCOFF::SYM_V_UNSPECIFIED;
  StringRef SymbolTableName;
  bool HasRename = false;
};

} // end namespace llvm

#endif // LLVM_MC_MCSYMBOLXCOFF_H
