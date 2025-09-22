//===- llvm/CodeGen/MachineModuleInfoImpls.cpp ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements object-file format specific implementations of
// MachineModuleInfoImpl.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineModuleInfoImpls.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/MC/MCSymbol.h"

using namespace llvm;

//===----------------------------------------------------------------------===//
// MachineModuleInfoMachO
//===----------------------------------------------------------------------===//

// Out of line virtual method.
void MachineModuleInfoMachO::anchor() {}
void MachineModuleInfoELF::anchor() {}
void MachineModuleInfoCOFF::anchor() {}
void MachineModuleInfoWasm::anchor() {}

using PairTy = std::pair<MCSymbol *, MachineModuleInfoImpl::StubValueTy>;
static int SortSymbolPair(const PairTy *LHS, const PairTy *RHS) {
  return LHS->first->getName().compare(RHS->first->getName());
}

MachineModuleInfoImpl::SymbolListTy MachineModuleInfoImpl::getSortedStubs(
    DenseMap<MCSymbol *, MachineModuleInfoImpl::StubValueTy> &Map) {
  MachineModuleInfoImpl::SymbolListTy List(Map.begin(), Map.end());

  array_pod_sort(List.begin(), List.end(), SortSymbolPair);

  Map.clear();
  return List;
}

using ExprStubPairTy = std::pair<MCSymbol *, const MCExpr *>;
static int SortAuthStubPair(const ExprStubPairTy *LHS,
                            const ExprStubPairTy *RHS) {
  return LHS->first->getName().compare(RHS->first->getName());
}

MachineModuleInfoImpl::ExprStubListTy MachineModuleInfoImpl::getSortedExprStubs(
    DenseMap<MCSymbol *, const MCExpr *> &ExprStubs) {
  MachineModuleInfoImpl::ExprStubListTy List(ExprStubs.begin(),
                                             ExprStubs.end());

  array_pod_sort(List.begin(), List.end(), SortAuthStubPair);

  ExprStubs.clear();
  return List;
}
