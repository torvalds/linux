//===- lib/TextAPI/SymbolSet.cpp - TAPI Symbol Set ------------*- C++-*----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/TextAPI/SymbolSet.h"

using namespace llvm;
using namespace llvm::MachO;

Symbol *SymbolSet::addGlobalImpl(EncodeKind Kind, StringRef Name,
                                 SymbolFlags Flags) {
  Name = copyString(Name);
  auto Result = Symbols.try_emplace(SymbolsMapKey{Kind, Name}, nullptr);
  if (Result.second)
    Result.first->second =
        new (Allocator) Symbol{Kind, Name, TargetList(), Flags};
  return Result.first->second;
}

Symbol *SymbolSet::addGlobal(EncodeKind Kind, StringRef Name, SymbolFlags Flags,
                             const Target &Targ) {
  auto *Sym = addGlobalImpl(Kind, Name, Flags);
  Sym->addTarget(Targ);
  return Sym;
}

const Symbol *SymbolSet::findSymbol(EncodeKind Kind, StringRef Name,
                                    ObjCIFSymbolKind ObjCIF) const {
  if (auto result = Symbols.lookup({Kind, Name}))
    return result;
  if ((ObjCIF == ObjCIFSymbolKind::None) || (ObjCIF > ObjCIFSymbolKind::EHType))
    return nullptr;
  assert(ObjCIF <= ObjCIFSymbolKind::EHType &&
         "expected single ObjCIFSymbolKind enum value");
  // Non-complete ObjC Interfaces are represented as global symbols.
  if (ObjCIF == ObjCIFSymbolKind::Class)
    return Symbols.lookup(
        {EncodeKind::GlobalSymbol, (ObjC2ClassNamePrefix + Name).str()});
  if (ObjCIF == ObjCIFSymbolKind::MetaClass)
    return Symbols.lookup(
        {EncodeKind::GlobalSymbol, (ObjC2MetaClassNamePrefix + Name).str()});
  return Symbols.lookup(
      {EncodeKind::GlobalSymbol, (ObjC2EHTypePrefix + Name).str()});
}
