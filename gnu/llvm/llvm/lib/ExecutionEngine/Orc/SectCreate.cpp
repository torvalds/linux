//===--------- SectCreate.cpp - Emulate ld64's -sectcreate option ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/SectCreate.h"

#define DEBUG_TYPE "orc"

using namespace llvm::jitlink;

namespace llvm::orc {

void SectCreateMaterializationUnit::materialize(
    std::unique_ptr<MaterializationResponsibility> R) {
  auto G = std::make_unique<LinkGraph>(
      "orc_sectcreate_" + SectName,
      ObjLinkingLayer.getExecutionSession().getTargetTriple(),
      getGenericEdgeKindName);

  auto &Sect = G->createSection(SectName, MP);
  auto Content = G->allocateContent(
      ArrayRef<char>(Data->getBuffer().data(), Data->getBuffer().size()));
  auto &B = G->createContentBlock(Sect, Content, ExecutorAddr(), Alignment, 0);

  for (auto &[Name, Info] : ExtraSymbols) {
    auto L = Info.Flags.isStrong() ? Linkage::Strong : Linkage::Weak;
    auto S = Info.Flags.isExported() ? Scope::Default : Scope::Hidden;
    G->addDefinedSymbol(B, Info.Offset, *Name, 0, L, S, Info.Flags.isCallable(),
                        true);
  }

  ObjLinkingLayer.emit(std::move(R), std::move(G));
}

void SectCreateMaterializationUnit::discard(const JITDylib &JD,
                                            const SymbolStringPtr &Name) {
  ExtraSymbols.erase(Name);
}

MaterializationUnit::Interface SectCreateMaterializationUnit::getInterface(
    const ExtraSymbolsMap &ExtraSymbols) {
  SymbolFlagsMap SymbolFlags;
  for (auto &[Name, Info] : ExtraSymbols)
    SymbolFlags[Name] = Info.Flags;
  return {std::move(SymbolFlags), nullptr};
}

} // End namespace llvm::orc.
