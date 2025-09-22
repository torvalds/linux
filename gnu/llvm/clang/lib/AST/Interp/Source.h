//===--- Source.h - Source location provider for the VM  --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines a program which organises and links multiple bytecode functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_INTERP_SOURCE_H
#define LLVM_CLANG_AST_INTERP_SOURCE_H

#include "PrimType.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/Stmt.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/Support/Endian.h"

namespace clang {
class Expr;
class SourceLocation;
class SourceRange;
namespace interp {
class Function;

/// Pointer into the code segment.
class CodePtr final {
public:
  CodePtr() : Ptr(nullptr) {}

  CodePtr &operator+=(int32_t Offset) {
    Ptr += Offset;
    return *this;
  }

  int32_t operator-(const CodePtr &RHS) const {
    assert(Ptr != nullptr && RHS.Ptr != nullptr && "Invalid code pointer");
    return Ptr - RHS.Ptr;
  }

  CodePtr operator-(size_t RHS) const {
    assert(Ptr != nullptr && "Invalid code pointer");
    return CodePtr(Ptr - RHS);
  }

  bool operator!=(const CodePtr &RHS) const { return Ptr != RHS.Ptr; }
  const std::byte *operator*() const { return Ptr; }

  operator bool() const { return Ptr; }

  /// Reads data and advances the pointer.
  template <typename T> std::enable_if_t<!std::is_pointer<T>::value, T> read() {
    assert(aligned(Ptr));
    using namespace llvm::support;
    T Value = endian::read<T, llvm::endianness::native>(Ptr);
    Ptr += align(sizeof(T));
    return Value;
  }

private:
  friend class Function;
  /// Constructor used by Function to generate pointers.
  CodePtr(const std::byte *Ptr) : Ptr(Ptr) {}
  /// Pointer into the code owned by a function.
  const std::byte *Ptr;
};

/// Describes the statement/declaration an opcode was generated from.
class SourceInfo final {
public:
  SourceInfo() {}
  SourceInfo(const Stmt *E) : Source(E) {}
  SourceInfo(const Decl *D) : Source(D) {}

  SourceLocation getLoc() const;
  SourceRange getRange() const;

  const Stmt *asStmt() const { return Source.dyn_cast<const Stmt *>(); }
  const Decl *asDecl() const { return Source.dyn_cast<const Decl *>(); }
  const Expr *asExpr() const;

  operator bool() const { return !Source.isNull(); }

private:
  llvm::PointerUnion<const Decl *, const Stmt *> Source;
};

using SourceMap = std::vector<std::pair<unsigned, SourceInfo>>;

/// Interface for classes which map locations to sources.
class SourceMapper {
public:
  virtual ~SourceMapper() {}

  /// Returns source information for a given PC in a function.
  virtual SourceInfo getSource(const Function *F, CodePtr PC) const = 0;

  /// Returns the expression if an opcode belongs to one, null otherwise.
  const Expr *getExpr(const Function *F, CodePtr PC) const;
  /// Returns the location from which an opcode originates.
  SourceLocation getLocation(const Function *F, CodePtr PC) const;
  SourceRange getRange(const Function *F, CodePtr PC) const;
};

} // namespace interp
} // namespace clang

#endif
