//===- Comdat.cpp - Implement Metadata classes ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Comdat class (including the C bindings).
//
//===----------------------------------------------------------------------===//

#include "llvm-c/Comdat.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringMapEntry.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Value.h"

using namespace llvm;

Comdat::Comdat(Comdat &&C) : Name(C.Name), SK(C.SK) {}

Comdat::Comdat() = default;

StringRef Comdat::getName() const { return Name->first(); }

void Comdat::addUser(GlobalObject *GO) { Users.insert(GO); }

void Comdat::removeUser(GlobalObject *GO) { Users.erase(GO); }

LLVMComdatRef LLVMGetOrInsertComdat(LLVMModuleRef M, const char *Name) {
  return wrap(unwrap(M)->getOrInsertComdat(Name));
}

LLVMComdatRef LLVMGetComdat(LLVMValueRef V) {
  GlobalObject *G = unwrap<GlobalObject>(V);
  return wrap(G->getComdat());
}

void LLVMSetComdat(LLVMValueRef V, LLVMComdatRef C) {
  GlobalObject *G = unwrap<GlobalObject>(V);
  G->setComdat(unwrap(C));
}

LLVMComdatSelectionKind LLVMGetComdatSelectionKind(LLVMComdatRef C) {
  switch (unwrap(C)->getSelectionKind()) {
  case Comdat::Any:
    return LLVMAnyComdatSelectionKind;
  case Comdat::ExactMatch:
    return LLVMExactMatchComdatSelectionKind;
  case Comdat::Largest:
    return LLVMLargestComdatSelectionKind;
  case Comdat::NoDeduplicate:
    return LLVMNoDeduplicateComdatSelectionKind;
  case Comdat::SameSize:
    return LLVMSameSizeComdatSelectionKind;
  }
  llvm_unreachable("Invalid Comdat SelectionKind!");
}

void LLVMSetComdatSelectionKind(LLVMComdatRef C, LLVMComdatSelectionKind kind) {
  Comdat *Cd = unwrap(C);
  switch (kind) {
  case LLVMAnyComdatSelectionKind:
    Cd->setSelectionKind(Comdat::Any);
    break;
  case LLVMExactMatchComdatSelectionKind:
    Cd->setSelectionKind(Comdat::ExactMatch);
    break;
  case LLVMLargestComdatSelectionKind:
    Cd->setSelectionKind(Comdat::Largest);
    break;
  case LLVMNoDeduplicateComdatSelectionKind:
    Cd->setSelectionKind(Comdat::NoDeduplicate);
    break;
  case LLVMSameSizeComdatSelectionKind:
    Cd->setSelectionKind(Comdat::SameSize);
    break;
  }
}
