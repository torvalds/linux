//===---- SectCreate.h -- Emulates ld64's -sectcreate option ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Emulates ld64's -sectcreate option.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SECTCREATE_H
#define LLVM_EXECUTIONENGINE_ORC_SECTCREATE_H

#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h"

#include <utility>
#include <vector>

namespace llvm::orc {

class SectCreateMaterializationUnit : public MaterializationUnit {
public:
  struct ExtraSymbolInfo {
    JITSymbolFlags Flags;
    size_t Offset = 0;
  };

  using ExtraSymbolsMap = DenseMap<SymbolStringPtr, ExtraSymbolInfo>;

  SectCreateMaterializationUnit(
      ObjectLinkingLayer &ObjLinkingLayer, std::string SectName, MemProt MP,
      uint64_t Alignment, std::unique_ptr<MemoryBuffer> Data,
      ExtraSymbolsMap ExtraSymbols = ExtraSymbolsMap())
      : MaterializationUnit(getInterface(ExtraSymbols)),
        ObjLinkingLayer(ObjLinkingLayer), SectName(std::move(SectName)), MP(MP),
        Alignment(Alignment), Data(std::move(Data)),
        ExtraSymbols(std::move(ExtraSymbols)) {}

  StringRef getName() const override { return "SectCreate"; }

  void materialize(std::unique_ptr<MaterializationResponsibility> R) override;

private:
  void discard(const JITDylib &JD, const SymbolStringPtr &Name) override;

  static Interface getInterface(const ExtraSymbolsMap &ExtraSymbols);

  ObjectLinkingLayer &ObjLinkingLayer;
  std::string SectName;
  MemProt MP;
  uint64_t Alignment;
  std::unique_ptr<MemoryBuffer> Data;
  ExtraSymbolsMap ExtraSymbols;
};

} // namespace llvm::orc

#endif // LLVM_EXECUTIONENGINE_ORC_SECTCREATE_H
