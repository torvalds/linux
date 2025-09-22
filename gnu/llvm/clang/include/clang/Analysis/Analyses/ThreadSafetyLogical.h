//===- ThreadSafetyLogical.h -----------------------------------*- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This file defines a representation for logical expressions with SExpr leaves
// that are used as part of fact-checking capability expressions.
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETYLOGICAL_H
#define LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETYLOGICAL_H

#include "clang/Analysis/Analyses/ThreadSafetyTIL.h"

namespace clang {
namespace threadSafety {
namespace lexpr {

class LExpr {
public:
  enum Opcode {
    Terminal,
    And,
    Or,
    Not
  };
  Opcode kind() const { return Kind; }

  /// Logical implication. Returns true if the LExpr implies RHS, i.e. if
  /// the LExpr holds, then RHS must hold. For example, (A & B) implies A.
  inline bool implies(const LExpr *RHS) const;

protected:
  LExpr(Opcode Kind) : Kind(Kind) {}

private:
  Opcode Kind;
};

class Terminal : public LExpr {
  til::SExpr *Expr;

public:
  Terminal(til::SExpr *Expr) : LExpr(LExpr::Terminal), Expr(Expr) {}

  const til::SExpr *expr() const { return Expr; }
  til::SExpr *expr() { return Expr; }

  static bool classof(const LExpr *E) { return E->kind() == LExpr::Terminal; }
};

class BinOp : public LExpr {
  LExpr *LHS, *RHS;

protected:
  BinOp(LExpr *LHS, LExpr *RHS, Opcode Code) : LExpr(Code), LHS(LHS), RHS(RHS) {}

public:
  const LExpr *left() const { return LHS; }
  LExpr *left() { return LHS; }

  const LExpr *right() const { return RHS; }
  LExpr *right() { return RHS; }
};

class And : public BinOp {
public:
  And(LExpr *LHS, LExpr *RHS) : BinOp(LHS, RHS, LExpr::And) {}

  static bool classof(const LExpr *E) { return E->kind() == LExpr::And; }
};

class Or : public BinOp {
public:
  Or(LExpr *LHS, LExpr *RHS) : BinOp(LHS, RHS, LExpr::Or) {}

  static bool classof(const LExpr *E) { return E->kind() == LExpr::Or; }
};

class Not : public LExpr {
  LExpr *Exp;

public:
  Not(LExpr *Exp) : LExpr(LExpr::Not), Exp(Exp) {}

  const LExpr *exp() const { return Exp; }
  LExpr *exp() { return Exp; }

  static bool classof(const LExpr *E) { return E->kind() == LExpr::Not; }
};

/// Logical implication. Returns true if LHS implies RHS, i.e. if LHS
/// holds, then RHS must hold. For example, (A & B) implies A.
bool implies(const LExpr *LHS, const LExpr *RHS);

bool LExpr::implies(const LExpr *RHS) const {
  return lexpr::implies(this, RHS);
}

}
}
}

#endif

