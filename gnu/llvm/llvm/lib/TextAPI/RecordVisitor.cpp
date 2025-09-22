//===- RecordVisitor.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// Implements the TAPI Record Visitor.
///
//===----------------------------------------------------------------------===//

#include "llvm/TextAPI/RecordVisitor.h"

using namespace llvm;
using namespace llvm::MachO;

RecordVisitor::~RecordVisitor() {}
void RecordVisitor::visitObjCInterface(const ObjCInterfaceRecord &) {}
void RecordVisitor::visitObjCCategory(const ObjCCategoryRecord &) {}

static bool shouldSkipRecord(const Record &R, const bool RecordUndefs) {
  if (R.isExported())
    return false;

  // Skip non exported symbols unless for flat namespace libraries.
  return !(RecordUndefs && R.isUndefined());
}

void SymbolConverter::visitGlobal(const GlobalRecord &GR) {
  auto [SymName, SymKind, InterfaceType] = parseSymbol(GR.getName());
  if (shouldSkipRecord(GR, RecordUndefs))
    return;
  Symbols->addGlobal(SymKind, SymName, GR.getFlags(), Targ);

  if (InterfaceType == ObjCIFSymbolKind::None) {
    Symbols->addGlobal(SymKind, SymName, GR.getFlags(), Targ);
    return;
  }

  // It is impossible to hold a complete ObjCInterface with a single
  // GlobalRecord, so continue to treat this symbol a generic global.
  Symbols->addGlobal(EncodeKind::GlobalSymbol, GR.getName(), GR.getFlags(),
                     Targ);
}

void SymbolConverter::addIVars(const ArrayRef<ObjCIVarRecord *> IVars,
                               StringRef ContainerName) {
  for (auto *IV : IVars) {
    if (shouldSkipRecord(*IV, RecordUndefs))
      continue;
    std::string Name =
        ObjCIVarRecord::createScopedName(ContainerName, IV->getName());
    Symbols->addGlobal(EncodeKind::ObjectiveCInstanceVariable, Name,
                       IV->getFlags(), Targ);
  }
}

void SymbolConverter::visitObjCInterface(const ObjCInterfaceRecord &ObjCR) {
  if (!shouldSkipRecord(ObjCR, RecordUndefs)) {
    if (ObjCR.isCompleteInterface()) {
      Symbols->addGlobal(EncodeKind::ObjectiveCClass, ObjCR.getName(),
                         ObjCR.getFlags(), Targ);
      if (ObjCR.hasExceptionAttribute())
        Symbols->addGlobal(EncodeKind::ObjectiveCClassEHType, ObjCR.getName(),
                           ObjCR.getFlags(), Targ);
    } else {
      // Because there is not a complete interface, visit individual symbols
      // instead.
      if (ObjCR.isExportedSymbol(ObjCIFSymbolKind::EHType))
        Symbols->addGlobal(EncodeKind::GlobalSymbol,
                           (ObjC2EHTypePrefix + ObjCR.getName()).str(),
                           ObjCR.getFlags(), Targ);
      if (ObjCR.isExportedSymbol(ObjCIFSymbolKind::Class))
        Symbols->addGlobal(EncodeKind::GlobalSymbol,
                           (ObjC2ClassNamePrefix + ObjCR.getName()).str(),
                           ObjCR.getFlags(), Targ);
      if (ObjCR.isExportedSymbol(ObjCIFSymbolKind::MetaClass))
        Symbols->addGlobal(EncodeKind::GlobalSymbol,
                           (ObjC2MetaClassNamePrefix + ObjCR.getName()).str(),
                           ObjCR.getFlags(), Targ);
    }
  }

  addIVars(ObjCR.getObjCIVars(), ObjCR.getName());
  for (const auto *Cat : ObjCR.getObjCCategories())
    addIVars(Cat->getObjCIVars(), ObjCR.getName());
}

void SymbolConverter::visitObjCCategory(const ObjCCategoryRecord &Cat) {
  addIVars(Cat.getObjCIVars(), Cat.getSuperClassName());
}
