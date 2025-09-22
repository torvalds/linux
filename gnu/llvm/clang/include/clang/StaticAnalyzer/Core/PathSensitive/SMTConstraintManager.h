//== SMTConstraintManager.h -------------------------------------*- C++ -*--==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines a SMT generic API, which will be the base class for
//  every SMT solver specific class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_SMTCONSTRAINTMANAGER_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_SMTCONSTRAINTMANAGER_H

#include "clang/Basic/JsonSupport.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/RangedConstraintManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SMTConv.h"
#include <optional>

typedef llvm::ImmutableSet<
    std::pair<clang::ento::SymbolRef, const llvm::SMTExpr *>>
    ConstraintSMTType;
REGISTER_TRAIT_WITH_PROGRAMSTATE(ConstraintSMT, ConstraintSMTType)

namespace clang {
namespace ento {

class SMTConstraintManager : public clang::ento::SimpleConstraintManager {
  mutable llvm::SMTSolverRef Solver = llvm::CreateZ3Solver();

public:
  SMTConstraintManager(clang::ento::ExprEngine *EE,
                       clang::ento::SValBuilder &SB)
      : SimpleConstraintManager(EE, SB) {
    Solver->setBoolParam("model", true); // Enable model finding
    Solver->setUnsignedParam("timeout", 15000 /*milliseconds*/);
  }
  virtual ~SMTConstraintManager() = default;

  //===------------------------------------------------------------------===//
  // Implementation for interface from SimpleConstraintManager.
  //===------------------------------------------------------------------===//

  ProgramStateRef assumeSym(ProgramStateRef State, SymbolRef Sym,
                            bool Assumption) override {
    ASTContext &Ctx = getBasicVals().getContext();

    QualType RetTy;
    bool hasComparison;

    llvm::SMTExprRef Exp =
        SMTConv::getExpr(Solver, Ctx, Sym, &RetTy, &hasComparison);

    // Create zero comparison for implicit boolean cast, with reversed
    // assumption
    if (!hasComparison && !RetTy->isBooleanType())
      return assumeExpr(
          State, Sym,
          SMTConv::getZeroExpr(Solver, Ctx, Exp, RetTy, !Assumption));

    return assumeExpr(State, Sym, Assumption ? Exp : Solver->mkNot(Exp));
  }

  ProgramStateRef assumeSymInclusiveRange(ProgramStateRef State, SymbolRef Sym,
                                          const llvm::APSInt &From,
                                          const llvm::APSInt &To,
                                          bool InRange) override {
    ASTContext &Ctx = getBasicVals().getContext();
    return assumeExpr(
        State, Sym, SMTConv::getRangeExpr(Solver, Ctx, Sym, From, To, InRange));
  }

  ProgramStateRef assumeSymUnsupported(ProgramStateRef State, SymbolRef Sym,
                                       bool Assumption) override {
    // Skip anything that is unsupported
    return State;
  }

  //===------------------------------------------------------------------===//
  // Implementation for interface from ConstraintManager.
  //===------------------------------------------------------------------===//

  ConditionTruthVal checkNull(ProgramStateRef State, SymbolRef Sym) override {
    ASTContext &Ctx = getBasicVals().getContext();

    QualType RetTy;
    // The expression may be casted, so we cannot call getZ3DataExpr() directly
    llvm::SMTExprRef VarExp = SMTConv::getExpr(Solver, Ctx, Sym, &RetTy);
    llvm::SMTExprRef Exp =
        SMTConv::getZeroExpr(Solver, Ctx, VarExp, RetTy, /*Assumption=*/true);

    // Negate the constraint
    llvm::SMTExprRef NotExp =
        SMTConv::getZeroExpr(Solver, Ctx, VarExp, RetTy, /*Assumption=*/false);

    ConditionTruthVal isSat = checkModel(State, Sym, Exp);
    ConditionTruthVal isNotSat = checkModel(State, Sym, NotExp);

    // Zero is the only possible solution
    if (isSat.isConstrainedTrue() && isNotSat.isConstrainedFalse())
      return true;

    // Zero is not a solution
    if (isSat.isConstrainedFalse() && isNotSat.isConstrainedTrue())
      return false;

    // Zero may be a solution
    return ConditionTruthVal();
  }

  const llvm::APSInt *getSymVal(ProgramStateRef State,
                                SymbolRef Sym) const override {
    BasicValueFactory &BVF = getBasicVals();
    ASTContext &Ctx = BVF.getContext();

    if (const SymbolData *SD = dyn_cast<SymbolData>(Sym)) {
      QualType Ty = Sym->getType();
      assert(!Ty->isRealFloatingType());
      llvm::APSInt Value(Ctx.getTypeSize(Ty),
                         !Ty->isSignedIntegerOrEnumerationType());

      // TODO: this should call checkModel so we can use the cache, however,
      // this method tries to get the interpretation (the actual value) from
      // the solver, which is currently not cached.

      llvm::SMTExprRef Exp = SMTConv::fromData(Solver, Ctx, SD);

      Solver->reset();
      addStateConstraints(State);

      // Constraints are unsatisfiable
      std::optional<bool> isSat = Solver->check();
      if (!isSat || !*isSat)
        return nullptr;

      // Model does not assign interpretation
      if (!Solver->getInterpretation(Exp, Value))
        return nullptr;

      // A value has been obtained, check if it is the only value
      llvm::SMTExprRef NotExp = SMTConv::fromBinOp(
          Solver, Exp, BO_NE,
          Ty->isBooleanType() ? Solver->mkBoolean(Value.getBoolValue())
                              : Solver->mkBitvector(Value, Value.getBitWidth()),
          /*isSigned=*/false);

      Solver->addConstraint(NotExp);

      std::optional<bool> isNotSat = Solver->check();
      if (!isNotSat || *isNotSat)
        return nullptr;

      // This is the only solution, store it
      return &BVF.getValue(Value);
    }

    if (const SymbolCast *SC = dyn_cast<SymbolCast>(Sym)) {
      SymbolRef CastSym = SC->getOperand();
      QualType CastTy = SC->getType();
      // Skip the void type
      if (CastTy->isVoidType())
        return nullptr;

      const llvm::APSInt *Value;
      if (!(Value = getSymVal(State, CastSym)))
        return nullptr;
      return &BVF.Convert(SC->getType(), *Value);
    }

    if (const BinarySymExpr *BSE = dyn_cast<BinarySymExpr>(Sym)) {
      const llvm::APSInt *LHS, *RHS;
      if (const SymIntExpr *SIE = dyn_cast<SymIntExpr>(BSE)) {
        LHS = getSymVal(State, SIE->getLHS());
        RHS = &SIE->getRHS();
      } else if (const IntSymExpr *ISE = dyn_cast<IntSymExpr>(BSE)) {
        LHS = &ISE->getLHS();
        RHS = getSymVal(State, ISE->getRHS());
      } else if (const SymSymExpr *SSM = dyn_cast<SymSymExpr>(BSE)) {
        // Early termination to avoid expensive call
        LHS = getSymVal(State, SSM->getLHS());
        RHS = LHS ? getSymVal(State, SSM->getRHS()) : nullptr;
      } else {
        llvm_unreachable("Unsupported binary expression to get symbol value!");
      }

      if (!LHS || !RHS)
        return nullptr;

      llvm::APSInt ConvertedLHS, ConvertedRHS;
      QualType LTy, RTy;
      std::tie(ConvertedLHS, LTy) = SMTConv::fixAPSInt(Ctx, *LHS);
      std::tie(ConvertedRHS, RTy) = SMTConv::fixAPSInt(Ctx, *RHS);
      SMTConv::doIntTypeConversion<llvm::APSInt, &SMTConv::castAPSInt>(
          Solver, Ctx, ConvertedLHS, LTy, ConvertedRHS, RTy);
      return BVF.evalAPSInt(BSE->getOpcode(), ConvertedLHS, ConvertedRHS);
    }

    llvm_unreachable("Unsupported expression to get symbol value!");
  }

  ProgramStateRef removeDeadBindings(ProgramStateRef State,
                                     SymbolReaper &SymReaper) override {
    auto CZ = State->get<ConstraintSMT>();
    auto &CZFactory = State->get_context<ConstraintSMT>();

    for (const auto &Entry : CZ) {
      if (SymReaper.isDead(Entry.first))
        CZ = CZFactory.remove(CZ, Entry);
    }

    return State->set<ConstraintSMT>(CZ);
  }

  void printJson(raw_ostream &Out, ProgramStateRef State, const char *NL = "\n",
                 unsigned int Space = 0, bool IsDot = false) const override {
    ConstraintSMTType Constraints = State->get<ConstraintSMT>();

    Indent(Out, Space, IsDot) << "\"constraints\": ";
    if (Constraints.isEmpty()) {
      Out << "null," << NL;
      return;
    }

    ++Space;
    Out << '[' << NL;
    for (ConstraintSMTType::iterator I = Constraints.begin();
         I != Constraints.end(); ++I) {
      Indent(Out, Space, IsDot)
          << "{ \"symbol\": \"" << I->first << "\", \"range\": \"";
      I->second->print(Out);
      Out << "\" }";

      if (std::next(I) != Constraints.end())
        Out << ',';
      Out << NL;
    }

    --Space;
    Indent(Out, Space, IsDot) << "],";
  }

  bool haveEqualConstraints(ProgramStateRef S1,
                            ProgramStateRef S2) const override {
    return S1->get<ConstraintSMT>() == S2->get<ConstraintSMT>();
  }

  bool canReasonAbout(SVal X) const override {
    const TargetInfo &TI = getBasicVals().getContext().getTargetInfo();

    std::optional<nonloc::SymbolVal> SymVal = X.getAs<nonloc::SymbolVal>();
    if (!SymVal)
      return true;

    const SymExpr *Sym = SymVal->getSymbol();
    QualType Ty = Sym->getType();

    // Complex types are not modeled
    if (Ty->isComplexType() || Ty->isComplexIntegerType())
      return false;

    // Non-IEEE 754 floating-point types are not modeled
    if ((Ty->isSpecificBuiltinType(BuiltinType::LongDouble) &&
         (&TI.getLongDoubleFormat() == &llvm::APFloat::x87DoubleExtended() ||
          &TI.getLongDoubleFormat() == &llvm::APFloat::PPCDoubleDouble())))
      return false;

    if (Ty->isRealFloatingType())
      return Solver->isFPSupported();

    if (isa<SymbolData>(Sym))
      return true;

    SValBuilder &SVB = getSValBuilder();

    if (const SymbolCast *SC = dyn_cast<SymbolCast>(Sym))
      return canReasonAbout(SVB.makeSymbolVal(SC->getOperand()));

    if (const BinarySymExpr *BSE = dyn_cast<BinarySymExpr>(Sym)) {
      if (const SymIntExpr *SIE = dyn_cast<SymIntExpr>(BSE))
        return canReasonAbout(SVB.makeSymbolVal(SIE->getLHS()));

      if (const IntSymExpr *ISE = dyn_cast<IntSymExpr>(BSE))
        return canReasonAbout(SVB.makeSymbolVal(ISE->getRHS()));

      if (const SymSymExpr *SSE = dyn_cast<SymSymExpr>(BSE))
        return canReasonAbout(SVB.makeSymbolVal(SSE->getLHS())) &&
               canReasonAbout(SVB.makeSymbolVal(SSE->getRHS()));
    }

    llvm_unreachable("Unsupported expression to reason about!");
  }

  /// Dumps SMT formula
  LLVM_DUMP_METHOD void dump() const { Solver->dump(); }

protected:
  // Check whether a new model is satisfiable, and update the program state.
  virtual ProgramStateRef assumeExpr(ProgramStateRef State, SymbolRef Sym,
                                     const llvm::SMTExprRef &Exp) {
    // Check the model, avoid simplifying AST to save time
    if (checkModel(State, Sym, Exp).isConstrainedTrue())
      return State->add<ConstraintSMT>(std::make_pair(Sym, Exp));

    return nullptr;
  }

  /// Given a program state, construct the logical conjunction and add it to
  /// the solver
  virtual void addStateConstraints(ProgramStateRef State) const {
    // TODO: Don't add all the constraints, only the relevant ones
    auto CZ = State->get<ConstraintSMT>();
    auto I = CZ.begin(), IE = CZ.end();

    // Construct the logical AND of all the constraints
    if (I != IE) {
      std::vector<llvm::SMTExprRef> ASTs;

      llvm::SMTExprRef Constraint = I++->second;
      while (I != IE) {
        Constraint = Solver->mkAnd(Constraint, I++->second);
      }

      Solver->addConstraint(Constraint);
    }
  }

  // Generate and check a Z3 model, using the given constraint.
  ConditionTruthVal checkModel(ProgramStateRef State, SymbolRef Sym,
                               const llvm::SMTExprRef &Exp) const {
    ProgramStateRef NewState =
        State->add<ConstraintSMT>(std::make_pair(Sym, Exp));

    llvm::FoldingSetNodeID ID;
    NewState->get<ConstraintSMT>().Profile(ID);

    unsigned hash = ID.ComputeHash();
    auto I = Cached.find(hash);
    if (I != Cached.end())
      return I->second;

    Solver->reset();
    addStateConstraints(NewState);

    std::optional<bool> res = Solver->check();
    if (!res)
      Cached[hash] = ConditionTruthVal();
    else
      Cached[hash] = ConditionTruthVal(*res);

    return Cached[hash];
  }

  // Cache the result of an SMT query (true, false, unknown). The key is the
  // hash of the constraints in a state
  mutable llvm::DenseMap<unsigned, ConditionTruthVal> Cached;
}; // end class SMTConstraintManager

} // namespace ento
} // namespace clang

#endif
