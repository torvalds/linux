//===- StmtIterator.h - Iterators for Statements ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the StmtIterator and ConstStmtIterator classes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_STMTITERATOR_H
#define LLVM_CLANG_AST_STMTITERATOR_H

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace clang {

class Decl;
class Stmt;
class VariableArrayType;

class StmtIteratorBase {
protected:
  enum {
    StmtMode = 0x0,
    SizeOfTypeVAMode = 0x1,
    DeclGroupMode = 0x2,
    Flags = 0x3
  };

  union {
    Stmt **stmt;
    Decl **DGI;
  };
  uintptr_t RawVAPtr = 0;
  Decl **DGE;

  StmtIteratorBase(Stmt **s) : stmt(s) {}
  StmtIteratorBase(const VariableArrayType *t);
  StmtIteratorBase(Decl **dgi, Decl **dge);
  StmtIteratorBase() : stmt(nullptr) {}

  bool inDeclGroup() const {
    return (RawVAPtr & Flags) == DeclGroupMode;
  }

  bool inSizeOfTypeVA() const {
    return (RawVAPtr & Flags) == SizeOfTypeVAMode;
  }

  bool inStmt() const {
    return (RawVAPtr & Flags) == StmtMode;
  }

  const VariableArrayType *getVAPtr() const {
    return reinterpret_cast<const VariableArrayType*>(RawVAPtr & ~Flags);
  }

  void setVAPtr(const VariableArrayType *P) {
    assert(inDeclGroup() || inSizeOfTypeVA());
    RawVAPtr = reinterpret_cast<uintptr_t>(P) | (RawVAPtr & Flags);
  }

  void NextDecl(bool ImmediateAdvance = true);
  bool HandleDecl(Decl* D);
  void NextVA();

  Stmt*& GetDeclExpr() const;
};

template <typename DERIVED, typename REFERENCE>
class StmtIteratorImpl : public StmtIteratorBase {
protected:
  StmtIteratorImpl(const StmtIteratorBase& RHS) : StmtIteratorBase(RHS) {}

public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = REFERENCE;
  using difference_type = std::ptrdiff_t;
  using pointer = REFERENCE;
  using reference = REFERENCE;

  StmtIteratorImpl() = default;
  StmtIteratorImpl(Stmt **s) : StmtIteratorBase(s) {}
  StmtIteratorImpl(Decl **dgi, Decl **dge) : StmtIteratorBase(dgi, dge) {}
  StmtIteratorImpl(const VariableArrayType *t) : StmtIteratorBase(t) {}

  DERIVED& operator++() {
    if (inStmt())
      ++stmt;
    else if (getVAPtr())
      NextVA();
    else
      NextDecl();

    return static_cast<DERIVED&>(*this);
  }

  DERIVED operator++(int) {
    DERIVED tmp = static_cast<DERIVED&>(*this);
    operator++();
    return tmp;
  }

  friend bool operator==(const DERIVED &LHS, const DERIVED &RHS) {
    return LHS.stmt == RHS.stmt && LHS.DGI == RHS.DGI &&
           LHS.RawVAPtr == RHS.RawVAPtr;
  }

  friend bool operator!=(const DERIVED &LHS, const DERIVED &RHS) {
    return !(LHS == RHS);
  }

  REFERENCE operator*() const {
    return inStmt() ? *stmt : GetDeclExpr();
  }

  REFERENCE operator->() const { return operator*(); }
};

struct ConstStmtIterator;

struct StmtIterator : public StmtIteratorImpl<StmtIterator, Stmt*&> {
  explicit StmtIterator() = default;
  StmtIterator(Stmt** S) : StmtIteratorImpl<StmtIterator, Stmt*&>(S) {}
  StmtIterator(Decl** dgi, Decl** dge)
      : StmtIteratorImpl<StmtIterator, Stmt*&>(dgi, dge) {}
  StmtIterator(const VariableArrayType *t)
      : StmtIteratorImpl<StmtIterator, Stmt*&>(t) {}

private:
  StmtIterator(const StmtIteratorBase &RHS)
      : StmtIteratorImpl<StmtIterator, Stmt *&>(RHS) {}

  inline friend StmtIterator
  cast_away_const(const ConstStmtIterator &RHS);
};

struct ConstStmtIterator : public StmtIteratorImpl<ConstStmtIterator,
                                                   const Stmt*> {
  explicit ConstStmtIterator() = default;
  ConstStmtIterator(const StmtIterator& RHS)
      : StmtIteratorImpl<ConstStmtIterator, const Stmt*>(RHS) {}

  ConstStmtIterator(Stmt * const *S)
      : StmtIteratorImpl<ConstStmtIterator, const Stmt *>(
            const_cast<Stmt **>(S)) {}
};

inline StmtIterator cast_away_const(const ConstStmtIterator &RHS) {
  return RHS;
}

} // namespace clang

#endif // LLVM_CLANG_AST_STMTITERATOR_H
