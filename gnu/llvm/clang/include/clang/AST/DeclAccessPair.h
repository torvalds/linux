//===--- DeclAccessPair.h - A decl bundled with its path access -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the DeclAccessPair class, which provides an
//  efficient representation of a pair of a NamedDecl* and an
//  AccessSpecifier.  Generally the access specifier gives the
//  natural access of a declaration when named in a class, as
//  defined in C++ [class.access.base]p1.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_DECLACCESSPAIR_H
#define LLVM_CLANG_AST_DECLACCESSPAIR_H

#include "clang/Basic/Specifiers.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/Endian.h"

namespace clang {

class NamedDecl;

/// A POD class for pairing a NamedDecl* with an access specifier.
/// Can be put into unions.
class DeclAccessPair {
  /// Use the lower 2 bit to store AccessSpecifier. Use the higher
  /// 61 bit to store the pointer to a NamedDecl or the DeclID to
  /// a NamedDecl. If the 3rd bit is set, storing the DeclID, otherwise
  /// storing the pointer.
  llvm::support::detail::packed_endian_specific_integral<
      uint64_t, llvm::endianness::native, alignof(void *)>
      Ptr;

  enum { ASMask = 0x3, Mask = 0x7 };

  bool isDeclID() const { return (Ptr >> 2) & 0x1; }

public:
  static DeclAccessPair make(NamedDecl *D, AccessSpecifier AS) {
    DeclAccessPair p;
    p.set(D, AS);
    return p;
  }

  static DeclAccessPair makeLazy(uint64_t ID, AccessSpecifier AS) {
    DeclAccessPair p;
    p.Ptr = (ID << 3) | (0x1 << 2) | uint64_t(AS);
    return p;
  }

  uint64_t getDeclID() const {
    assert(isDeclID());
    return (~Mask & Ptr) >> 3;
  }

  NamedDecl *getDecl() const {
    assert(!isDeclID());
    return reinterpret_cast<NamedDecl*>(~Mask & Ptr);
  }
  AccessSpecifier getAccess() const { return AccessSpecifier(ASMask & Ptr); }

  void setDecl(NamedDecl *D) {
    set(D, getAccess());
  }
  void setAccess(AccessSpecifier AS) {
    set(getDecl(), AS);
  }
  void set(NamedDecl *D, AccessSpecifier AS) {
    Ptr = uint64_t(AS) | reinterpret_cast<uint64_t>(D);
  }

  operator NamedDecl*() const { return getDecl(); }
  NamedDecl *operator->() const { return getDecl(); }
};

// Make sure DeclAccessPair is pointer-aligned types.
static_assert(alignof(DeclAccessPair) == alignof(void *));
// Make sure DeclAccessPair is still POD.
static_assert(std::is_standard_layout_v<DeclAccessPair> &&
              std::is_trivial_v<DeclAccessPair>);
}

#endif
