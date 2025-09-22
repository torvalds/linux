//===- Use.h ----------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Sandbox IR Use.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SANDBOXIR_USE_H
#define LLVM_SANDBOXIR_USE_H

#include "llvm/IR/Use.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm::sandboxir {

class Context;
class Value;
class User;

/// Represents a Def-use/Use-def edge in SandboxIR.
/// NOTE: Unlike llvm::Use, this is not an integral part of the use-def chains.
/// It is also not uniqued and is currently passed by value, so you can have
/// more than one sandboxir::Use objects for the same use-def edge.
class Use {
  llvm::Use *LLVMUse;
  User *Usr;
  Context *Ctx;

  /// Don't allow the user to create a sandboxir::Use directly.
  Use(llvm::Use *LLVMUse, User *Usr, Context &Ctx)
      : LLVMUse(LLVMUse), Usr(Usr), Ctx(&Ctx) {}
  Use() : LLVMUse(nullptr), Ctx(nullptr) {}

  friend class Value;              // For constructor
  friend class User;               // For constructor
  friend class OperandUseIterator; // For constructor
  friend class UserUseIterator;    // For accessing members

public:
  operator Value *() const { return get(); }
  Value *get() const;
  void set(Value *V);
  class User *getUser() const { return Usr; }
  unsigned getOperandNo() const;
  Context *getContext() const { return Ctx; }
  bool operator==(const Use &Other) const {
    assert(Ctx == Other.Ctx && "Contexts differ!");
    return LLVMUse == Other.LLVMUse && Usr == Other.Usr;
  }
  bool operator!=(const Use &Other) const { return !(*this == Other); }
#ifndef NDEBUG
  void dump(raw_ostream &OS) const;
  void dump() const;
#endif // NDEBUG
};

} // namespace llvm::sandboxir

#endif // LLVM_SANDBOXIR_USE_H
