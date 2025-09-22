//===- PDBSymbolFunc.cpp - --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/PDBSymbolFunc.h"

#include "llvm/DebugInfo/PDB/ConcreteSymbolEnumerator.h"
#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/IPDBLineNumber.h"
#include "llvm/DebugInfo/PDB/IPDBSession.h"
#include "llvm/DebugInfo/PDB/Native/NativeTypeFunctionSig.h"
#include "llvm/DebugInfo/PDB/PDBSymDumper.h"
#include "llvm/DebugInfo/PDB/PDBSymbolData.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeFunctionSig.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

#include <unordered_set>
#include <utility>
#include <vector>

using namespace llvm;
using namespace llvm::pdb;

namespace {
class FunctionArgEnumerator : public IPDBEnumChildren<PDBSymbolData> {
public:
  typedef ConcreteSymbolEnumerator<PDBSymbolData> ArgEnumeratorType;

  FunctionArgEnumerator(const IPDBSession &PDBSession,
                        const PDBSymbolFunc &PDBFunc)
      : Session(PDBSession), Func(PDBFunc) {
    // Arguments can appear multiple times if they have live range
    // information, so we only take the first occurrence.
    std::unordered_set<std::string> SeenNames;
    auto DataChildren = Func.findAllChildren<PDBSymbolData>();
    while (auto Child = DataChildren->getNext()) {
      if (Child->getDataKind() == PDB_DataKind::Param) {
        std::string Name = Child->getName();
        if (SeenNames.find(Name) != SeenNames.end())
          continue;
        Args.push_back(std::move(Child));
        SeenNames.insert(Name);
      }
    }
    reset();
  }

  uint32_t getChildCount() const override { return Args.size(); }

  std::unique_ptr<PDBSymbolData>
  getChildAtIndex(uint32_t Index) const override {
    if (Index >= Args.size())
      return nullptr;

    return Session.getConcreteSymbolById<PDBSymbolData>(
        Args[Index]->getSymIndexId());
  }

  std::unique_ptr<PDBSymbolData> getNext() override {
    if (CurIter == Args.end())
      return nullptr;
    const auto &Result = **CurIter;
    ++CurIter;
    return Session.getConcreteSymbolById<PDBSymbolData>(Result.getSymIndexId());
  }

  void reset() override { CurIter = Args.empty() ? Args.end() : Args.begin(); }

private:
  typedef std::vector<std::unique_ptr<PDBSymbolData>> ArgListType;
  const IPDBSession &Session;
  const PDBSymbolFunc &Func;
  ArgListType Args;
  ArgListType::const_iterator CurIter;
};
}

std::unique_ptr<IPDBEnumChildren<PDBSymbolData>>
PDBSymbolFunc::getArguments() const {
  return std::make_unique<FunctionArgEnumerator>(Session, *this);
}

void PDBSymbolFunc::dump(PDBSymDumper &Dumper) const { Dumper.dump(*this); }

bool PDBSymbolFunc::isDestructor() const {
  std::string Name = getName();
  if (Name.empty())
    return false;
  if (Name[0] == '~')
    return true;
  if (Name == "__vecDelDtor")
    return true;
  return false;
}

std::unique_ptr<IPDBEnumLineNumbers> PDBSymbolFunc::getLineNumbers() const {
  auto Len = RawSymbol->getLength();
  return Session.findLineNumbersByAddress(RawSymbol->getVirtualAddress(),
                                          Len ? Len : 1);
}

uint32_t PDBSymbolFunc::getCompilandId() const {
  if (auto Lines = getLineNumbers()) {
    if (auto FirstLine = Lines->getNext()) {
      return FirstLine->getCompilandId();
    }
  }
  return 0;
}
