//== SimpleConstraintManager.h ----------------------------------*- C++ -*--==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Simplified constraint manager backend.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_SIMPLECONSTRAINTMANAGER_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_SIMPLECONSTRAINTMANAGER_H

#include "clang/StaticAnalyzer/Core/PathSensitive/ConstraintManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"

namespace clang {

namespace ento {

class SimpleConstraintManager : public ConstraintManager {
  ExprEngine *EE;
  SValBuilder &SVB;

public:
  SimpleConstraintManager(ExprEngine *exprengine, SValBuilder &SB)
      : EE(exprengine), SVB(SB) {}

  ~SimpleConstraintManager() override;

  //===------------------------------------------------------------------===//
  // Implementation for interface from ConstraintManager.
  //===------------------------------------------------------------------===//

protected:
  //===------------------------------------------------------------------===//
  // Interface that subclasses must implement.
  //===------------------------------------------------------------------===//

  /// Given a symbolic expression that can be reasoned about, assume that it is
  /// true/false and generate the new program state.
  virtual ProgramStateRef assumeSym(ProgramStateRef State, SymbolRef Sym,
                                    bool Assumption) = 0;

  /// Given a symbolic expression within the range [From, To], assume that it is
  /// true/false and generate the new program state.
  /// This function is used to handle case ranges produced by a language
  /// extension for switch case statements.
  virtual ProgramStateRef assumeSymInclusiveRange(ProgramStateRef State,
                                                  SymbolRef Sym,
                                                  const llvm::APSInt &From,
                                                  const llvm::APSInt &To,
                                                  bool InRange) = 0;

  /// Given a symbolic expression that cannot be reasoned about, assume that
  /// it is zero/nonzero and add it directly to the solver state.
  virtual ProgramStateRef assumeSymUnsupported(ProgramStateRef State,
                                               SymbolRef Sym,
                                               bool Assumption) = 0;

  //===------------------------------------------------------------------===//
  // Internal implementation.
  //===------------------------------------------------------------------===//

  /// Ensures that the DefinedSVal conditional is expressed as a NonLoc by
  /// creating boolean casts to handle Loc's.
  ProgramStateRef assumeInternal(ProgramStateRef State, DefinedSVal Cond,
                                 bool Assumption) override;

  ProgramStateRef assumeInclusiveRangeInternal(ProgramStateRef State,
                                               NonLoc Value,
                                               const llvm::APSInt &From,
                                               const llvm::APSInt &To,
                                               bool InRange) override;

  SValBuilder &getSValBuilder() const { return SVB; }
  BasicValueFactory &getBasicVals() const { return SVB.getBasicValueFactory(); }
  SymbolManager &getSymbolManager() const { return SVB.getSymbolManager(); }

private:
  ProgramStateRef assume(ProgramStateRef State, NonLoc Cond, bool Assumption);

  ProgramStateRef assumeAux(ProgramStateRef State, NonLoc Cond,
                            bool Assumption);
};

} // end namespace ento

} // end namespace clang

#endif
