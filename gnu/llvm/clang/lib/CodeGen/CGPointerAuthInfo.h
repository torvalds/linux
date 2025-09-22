//===----- CGPointerAuthInfo.h -  -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Pointer auth info class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_CGPOINTERAUTHINFO_H
#define LLVM_CLANG_LIB_CODEGEN_CGPOINTERAUTHINFO_H

#include "clang/AST/Type.h"
#include "clang/Basic/LangOptions.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"

namespace clang {
namespace CodeGen {

class CGPointerAuthInfo {
private:
  PointerAuthenticationMode AuthenticationMode : 2;
  unsigned IsIsaPointer : 1;
  unsigned AuthenticatesNullValues : 1;
  unsigned Key : 2;
  llvm::Value *Discriminator;

public:
  CGPointerAuthInfo()
      : AuthenticationMode(PointerAuthenticationMode::None),
        IsIsaPointer(false), AuthenticatesNullValues(false), Key(0),
        Discriminator(nullptr) {}
  CGPointerAuthInfo(unsigned Key, PointerAuthenticationMode AuthenticationMode,
                    bool IsIsaPointer, bool AuthenticatesNullValues,
                    llvm::Value *Discriminator)
      : AuthenticationMode(AuthenticationMode), IsIsaPointer(IsIsaPointer),
        AuthenticatesNullValues(AuthenticatesNullValues), Key(Key),
        Discriminator(Discriminator) {
    assert(!Discriminator || Discriminator->getType()->isIntegerTy() ||
           Discriminator->getType()->isPointerTy());
  }

  explicit operator bool() const { return isSigned(); }

  bool isSigned() const {
    return AuthenticationMode != PointerAuthenticationMode::None;
  }

  unsigned getKey() const {
    assert(isSigned());
    return Key;
  }
  llvm::Value *getDiscriminator() const {
    assert(isSigned());
    return Discriminator;
  }

  PointerAuthenticationMode getAuthenticationMode() const {
    return AuthenticationMode;
  }

  bool isIsaPointer() const { return IsIsaPointer; }

  bool authenticatesNullValues() const { return AuthenticatesNullValues; }

  bool shouldStrip() const {
    return AuthenticationMode == PointerAuthenticationMode::Strip ||
           AuthenticationMode == PointerAuthenticationMode::SignAndStrip;
  }

  bool shouldSign() const {
    return AuthenticationMode == PointerAuthenticationMode::SignAndStrip ||
           AuthenticationMode == PointerAuthenticationMode::SignAndAuth;
  }

  bool shouldAuth() const {
    return AuthenticationMode == PointerAuthenticationMode::SignAndAuth;
  }

  friend bool operator!=(const CGPointerAuthInfo &LHS,
                         const CGPointerAuthInfo &RHS) {
    return LHS.Key != RHS.Key || LHS.Discriminator != RHS.Discriminator ||
           LHS.AuthenticationMode != RHS.AuthenticationMode;
  }

  friend bool operator==(const CGPointerAuthInfo &LHS,
                         const CGPointerAuthInfo &RHS) {
    return !(LHS != RHS);
  }
};

} // end namespace CodeGen
} // end namespace clang

#endif
