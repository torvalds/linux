//===- llvm/CodeGen/RegAllocRegistry.h --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation for register allocator function
// pass registry (RegisterRegAlloc).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_REGALLOCREGISTRY_H
#define LLVM_CODEGEN_REGALLOCREGISTRY_H

#include "llvm/CodeGen/RegAllocCommon.h"
#include "llvm/CodeGen/MachinePassRegistry.h"

namespace llvm {

class FunctionPass;

//===----------------------------------------------------------------------===//
///
/// RegisterRegAllocBase class - Track the registration of register allocators.
///
//===----------------------------------------------------------------------===//
template <class SubClass>
class RegisterRegAllocBase : public MachinePassRegistryNode<FunctionPass *(*)()> {
public:
  using FunctionPassCtor = FunctionPass *(*)();

  static MachinePassRegistry<FunctionPassCtor> Registry;

  RegisterRegAllocBase(const char *N, const char *D, FunctionPassCtor C)
      : MachinePassRegistryNode(N, D, C) {
    Registry.Add(this);
  }

  ~RegisterRegAllocBase() { Registry.Remove(this); }

  // Accessors.
  SubClass *getNext() const {
    return static_cast<SubClass *>(MachinePassRegistryNode::getNext());
  }

  static SubClass *getList() {
    return static_cast<SubClass *>(Registry.getList());
  }

  static FunctionPassCtor getDefault() { return Registry.getDefault(); }

  static void setDefault(FunctionPassCtor C) { Registry.setDefault(C); }

  static void setListener(MachinePassRegistryListener<FunctionPassCtor> *L) {
    Registry.setListener(L);
  }
};

class RegisterRegAlloc : public RegisterRegAllocBase<RegisterRegAlloc> {
public:
  RegisterRegAlloc(const char *N, const char *D, FunctionPassCtor C)
    : RegisterRegAllocBase(N, D, C) {}
};

/// RegisterRegAlloc's global Registry tracks allocator registration.
template <class T>
MachinePassRegistry<typename RegisterRegAllocBase<T>::FunctionPassCtor>
RegisterRegAllocBase<T>::Registry;

} // end namespace llvm

#endif // LLVM_CODEGEN_REGALLOCREGISTRY_H
