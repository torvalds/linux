//===-- PPCGenScalarMASSEntries.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This transformation converts standard math functions into their
// corresponding MASS (scalar) entries for PowerPC targets.
// Following are examples of such conversion:
//     tanh ---> __xl_tanh_finite
// Such lowering is legal under the fast-math option.
//
//===----------------------------------------------------------------------===//

#include "PPC.h"
#include "PPCSubtarget.h"
#include "PPCTargetMachine.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

#define DEBUG_TYPE "ppc-gen-scalar-mass"

using namespace llvm;

namespace {

class PPCGenScalarMASSEntries : public ModulePass {
public:
  static char ID;

  PPCGenScalarMASSEntries() : ModulePass(ID) {
    ScalarMASSFuncs = {
#define TLI_DEFINE_SCALAR_MASS_FUNCS
#include "llvm/Analysis/ScalarFuncs.def"
    };
  }

  bool runOnModule(Module &M) override;

  StringRef getPassName() const override {
    return "PPC Generate Scalar MASS Entries";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<TargetTransformInfoWrapperPass>();
  }

private:
  std::map<StringRef, StringRef> ScalarMASSFuncs;
  bool isCandidateSafeToLower(const CallInst &CI) const;
  bool isFiniteCallSafe(const CallInst &CI) const;
  bool createScalarMASSCall(StringRef MASSEntry, CallInst &CI,
                            Function &Func) const;
};

} // namespace

// Returns true if 'afn' flag exists on the call instruction with the math
// function
bool PPCGenScalarMASSEntries::isCandidateSafeToLower(const CallInst &CI) const {
  // skip functions with no scalar or vector FP type (like cosisin)
  if (!isa<FPMathOperator>(CI))
    return false;

  return CI.hasApproxFunc();
}

// Returns true if 'nnan', 'ninf' and 'nsz' flags exist on the call instruction
// with the math function
bool PPCGenScalarMASSEntries::isFiniteCallSafe(const CallInst &CI) const {
  // skip functions with no scalar or vector FP type (like cosisin)
  if (!isa<FPMathOperator>(CI))
    return false;

  // FIXME: no-errno and trapping-math need to be set for MASS converstion
  // but they don't have IR representation.
  return CI.hasNoNaNs() && CI.hasNoInfs() && CI.hasNoSignedZeros();
}

/// Lowers scalar math functions to scalar MASS functions.
///     e.g.: tanh         --> __xl_tanh_finite or __xl_tanh
/// Both function prototype and its callsite is updated during lowering.
bool PPCGenScalarMASSEntries::createScalarMASSCall(StringRef MASSEntry,
                                                   CallInst &CI,
                                                   Function &Func) const {
  if (CI.use_empty())
    return false;

  Module *M = Func.getParent();
  assert(M && "Expecting a valid Module");

  std::string MASSEntryStr = MASSEntry.str();
  if (isFiniteCallSafe(CI))
    MASSEntryStr += "_finite";

  FunctionCallee FCache = M->getOrInsertFunction(
      MASSEntryStr, Func.getFunctionType(), Func.getAttributes());

  CI.setCalledFunction(FCache);

  return true;
}

bool PPCGenScalarMASSEntries::runOnModule(Module &M) {
  bool Changed = false;

  auto *TPC = getAnalysisIfAvailable<TargetPassConfig>();
  if (!TPC || skipModule(M))
    return false;

  for (Function &Func : M) {
    if (!Func.isDeclaration())
      continue;

    auto Iter = ScalarMASSFuncs.find(Func.getName());
    if (Iter == ScalarMASSFuncs.end())
      continue;

    // The call to createScalarMASSCall() invalidates the iterator over users
    // upon replacing the users. Precomputing the current list of users allows
    // us to replace all the call sites.
    SmallVector<User *, 4> TheUsers;
    for (auto *User : Func.users())
      TheUsers.push_back(User);

    for (auto *User : TheUsers)
      if (auto *CI = dyn_cast_or_null<CallInst>(User)) {
        if (isCandidateSafeToLower(*CI))
          Changed |= createScalarMASSCall(Iter->second, *CI, Func);
      }
  }

  return Changed;
}

char PPCGenScalarMASSEntries::ID = 0;

char &llvm::PPCGenScalarMASSEntriesID = PPCGenScalarMASSEntries::ID;

INITIALIZE_PASS(PPCGenScalarMASSEntries, DEBUG_TYPE,
                "Generate Scalar MASS entries", false, false)

ModulePass *llvm::createPPCGenScalarMASSEntriesPass() {
  return new PPCGenScalarMASSEntries();
}
