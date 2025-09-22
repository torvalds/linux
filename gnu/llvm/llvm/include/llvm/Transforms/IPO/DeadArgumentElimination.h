//===- DeadArgumentElimination.h - Eliminate Dead Args ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass deletes dead arguments from internal functions.  Dead argument
// elimination removes arguments which are directly dead, as well as arguments
// only passed into function calls as dead arguments of other functions.  This
// pass also deletes dead return values in a similar way.
//
// This pass is often useful as a cleanup pass to run after aggressive
// interprocedural passes, which add possibly-dead arguments or return values.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_DEADARGUMENTELIMINATION_H
#define LLVM_TRANSFORMS_IPO_DEADARGUMENTELIMINATION_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include <map>
#include <set>
#include <string>
#include <tuple>

namespace llvm {

class Module;
class Use;
class Value;

/// Eliminate dead arguments (and return values) from functions.
class DeadArgumentEliminationPass
    : public PassInfoMixin<DeadArgumentEliminationPass> {
public:
  /// Struct that represents (part of) either a return value or a function
  /// argument.  Used so that arguments and return values can be used
  /// interchangeably.
  struct RetOrArg {
    const Function *F;
    unsigned Idx;
    bool IsArg;

    RetOrArg(const Function *F, unsigned Idx, bool IsArg)
        : F(F), Idx(Idx), IsArg(IsArg) {}

    /// Make RetOrArg comparable, so we can put it into a map.
    bool operator<(const RetOrArg &O) const {
      return std::tie(F, Idx, IsArg) < std::tie(O.F, O.Idx, O.IsArg);
    }

    /// Make RetOrArg comparable, so we can easily iterate the multimap.
    bool operator==(const RetOrArg &O) const {
      return F == O.F && Idx == O.Idx && IsArg == O.IsArg;
    }

    std::string getDescription() const {
      return (Twine(IsArg ? "Argument #" : "Return value #") + Twine(Idx) +
              " of function " + F->getName())
          .str();
    }
  };

  /// During our initial pass over the program, we determine that things are
  /// either alive or maybe alive. We don't mark anything explicitly dead (even
  /// if we know they are), since anything not alive with no registered uses
  /// (in Uses) will never be marked alive and will thus become dead in the end.
  enum Liveness { Live, MaybeLive };

  DeadArgumentEliminationPass(bool ShouldHackArguments = false)
      : ShouldHackArguments(ShouldHackArguments) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &);

  /// Convenience wrapper
  RetOrArg createRet(const Function *F, unsigned Idx) {
    return RetOrArg(F, Idx, false);
  }

  /// Convenience wrapper
  RetOrArg createArg(const Function *F, unsigned Idx) {
    return RetOrArg(F, Idx, true);
  }

  using UseMap = std::multimap<RetOrArg, RetOrArg>;

  /// This maps a return value or argument to any MaybeLive return values or
  /// arguments it uses. This allows the MaybeLive values to be marked live
  /// when any of its users is marked live.
  /// For example (indices are left out for clarity):
  ///  - Uses[ret F] = ret G
  ///    This means that F calls G, and F returns the value returned by G.
  ///  - Uses[arg F] = ret G
  ///    This means that some function calls G and passes its result as an
  ///    argument to F.
  ///  - Uses[ret F] = arg F
  ///    This means that F returns one of its own arguments.
  ///  - Uses[arg F] = arg G
  ///    This means that G calls F and passes one of its own (G's) arguments
  ///    directly to F.
  UseMap Uses;

  using LiveSet = std::set<RetOrArg>;
  using LiveFuncSet = std::set<const Function *>;

  /// This set contains all values that have been determined to be live.
  LiveSet LiveValues;

  /// This set contains all values that are cannot be changed in any way.
  LiveFuncSet LiveFunctions;

  using UseVector = SmallVector<RetOrArg, 5>;

  /// This allows this pass to do double-duty as the dead arg hacking pass
  /// (used only by bugpoint).
  bool ShouldHackArguments = false;

private:
  Liveness markIfNotLive(RetOrArg Use, UseVector &MaybeLiveUses);
  Liveness surveyUse(const Use *U, UseVector &MaybeLiveUses,
                     unsigned RetValNum = -1U);
  Liveness surveyUses(const Value *V, UseVector &MaybeLiveUses);

  void surveyFunction(const Function &F);
  bool isLive(const RetOrArg &RA);
  void markValue(const RetOrArg &RA, Liveness L,
                 const UseVector &MaybeLiveUses);
  void markLive(const RetOrArg &RA);
  void markLive(const Function &F);
  void propagateLiveness(const RetOrArg &RA);
  bool removeDeadStuffFromFunction(Function *F);
  bool deleteDeadVarargs(Function &F);
  bool removeDeadArgumentsFromCallers(Function &F);
  void propagateVirtMustcallLiveness(const Module &M);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_DEADARGUMENTELIMINATION_H
