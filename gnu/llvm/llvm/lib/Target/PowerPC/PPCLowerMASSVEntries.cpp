//===-- PPCLowerMASSVEntries.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements lowering of MASSV (SIMD) entries for specific PowerPC
// subtargets.
// Following is an example of a conversion specific to Power9 subtarget:
// __sind2_massv ---> __sind2_P9
//
//===----------------------------------------------------------------------===//

#include "PPC.h"
#include "PPCSubtarget.h"
#include "PPCTargetMachine.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

#define DEBUG_TYPE "ppc-lower-massv-entries"

using namespace llvm;

namespace {

static StringRef MASSVFuncs[] = {
#define TLI_DEFINE_MASSV_VECFUNCS
#define TLI_DEFINE_VECFUNC(SCAL, VEC, VF, VABI_PREFIX) VEC,
#include "llvm/Analysis/VecFuncs.def"
#undef TLI_DEFINE_MASSV_VECFUNCS
};

class PPCLowerMASSVEntries : public ModulePass {
public:
  static char ID;

  PPCLowerMASSVEntries() : ModulePass(ID) {}

  bool runOnModule(Module &M) override;

  StringRef getPassName() const override { return "PPC Lower MASS Entries"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<TargetTransformInfoWrapperPass>();
  }

private:
  static bool isMASSVFunc(StringRef Name);
  static StringRef getCPUSuffix(const PPCSubtarget *Subtarget);
  static std::string createMASSVFuncName(Function &Func,
                                         const PPCSubtarget *Subtarget);
  bool handlePowSpecialCases(CallInst *CI, Function &Func, Module &M);
  bool lowerMASSVCall(CallInst *CI, Function &Func, Module &M,
                      const PPCSubtarget *Subtarget);
};

} // namespace

/// Checks if the specified function name represents an entry in the MASSV
/// library.
bool PPCLowerMASSVEntries::isMASSVFunc(StringRef Name) {
  return llvm::is_contained(MASSVFuncs, Name);
}

// FIXME:
/// Returns a string corresponding to the specified PowerPC subtarget. e.g.:
/// "_P8" for Power8, "_P9" for Power9. The string is used as a suffix while
/// generating subtarget-specific MASSV library functions. Current support
/// includes minimum subtarget Power8 for Linux and Power7 for AIX.
StringRef PPCLowerMASSVEntries::getCPUSuffix(const PPCSubtarget *Subtarget) {
  // Assume generic when Subtarget is unavailable.
  if (!Subtarget)
    return "";
  // TODO: add _P10 enties to Linux MASS lib and remove the check for AIX
  if (Subtarget->isAIXABI() && Subtarget->hasP10Vector())
    return "_P10";
  if (Subtarget->hasP9Vector())
    return "_P9";
  if (Subtarget->hasP8Vector())
    return "_P8";
  if (Subtarget->isAIXABI())
    return "_P7";

  report_fatal_error(
      "Mininum subtarget for -vector-library=MASSV option is Power8 on Linux "
      "and Power7 on AIX when vectorization is not disabled.");
}

/// Creates PowerPC subtarget-specific name corresponding to the specified
/// generic MASSV function, and the PowerPC subtarget.
std::string
PPCLowerMASSVEntries::createMASSVFuncName(Function &Func,
                                          const PPCSubtarget *Subtarget) {
  StringRef Suffix = getCPUSuffix(Subtarget);
  auto GenericName = Func.getName().str();
  std::string MASSVEntryName = GenericName + Suffix.str();
  return MASSVEntryName;
}

/// If there are proper fast-math flags, this function creates llvm.pow
/// intrinsics when the exponent is 0.25 or 0.75.
bool PPCLowerMASSVEntries::handlePowSpecialCases(CallInst *CI, Function &Func,
                                                 Module &M) {
  if (Func.getName() != "__powf4" && Func.getName() != "__powd2")
    return false;

  if (Constant *Exp = dyn_cast<Constant>(CI->getArgOperand(1)))
    if (ConstantFP *CFP = dyn_cast_or_null<ConstantFP>(Exp->getSplatValue())) {
      // If the argument is 0.75 or 0.25 it is cheaper to turn it into pow
      // intrinsic so that it could be optimzed as sequence of sqrt's.
      if (!CI->hasNoInfs() || !CI->hasApproxFunc())
        return false;

      if (!CFP->isExactlyValue(0.75) && !CFP->isExactlyValue(0.25))
        return false;

      if (CFP->isExactlyValue(0.25) && !CI->hasNoSignedZeros())
        return false;

      CI->setCalledFunction(
          Intrinsic::getDeclaration(&M, Intrinsic::pow, CI->getType()));
      return true;
    }

  return false;
}

/// Lowers generic MASSV entries to PowerPC subtarget-specific MASSV entries.
/// e.g.: __sind2_massv --> __sind2_P9 for a Power9 subtarget.
/// Both function prototypes and their callsites are updated during lowering.
bool PPCLowerMASSVEntries::lowerMASSVCall(CallInst *CI, Function &Func,
                                          Module &M,
                                          const PPCSubtarget *Subtarget) {
  if (CI->use_empty())
    return false;

  // Handling pow(x, 0.25), pow(x, 0.75), powf(x, 0.25), powf(x, 0.75)
  if (handlePowSpecialCases(CI, Func, M))
    return true;

  std::string MASSVEntryName = createMASSVFuncName(Func, Subtarget);
  FunctionCallee FCache = M.getOrInsertFunction(
      MASSVEntryName, Func.getFunctionType(), Func.getAttributes());

  CI->setCalledFunction(FCache);  

  return true;
}

bool PPCLowerMASSVEntries::runOnModule(Module &M) {
  bool Changed = false;

  auto *TPC = getAnalysisIfAvailable<TargetPassConfig>();
  if (!TPC)
    return Changed;

  auto &TM = TPC->getTM<PPCTargetMachine>();
  const PPCSubtarget *Subtarget;

  for (Function &Func : M) {
    if (!Func.isDeclaration())
      continue;

    if (!isMASSVFunc(Func.getName()))
      continue;

    // Call to lowerMASSVCall() invalidates the iterator over users upon
    // replacing the users. Precomputing the current list of users allows us to
    // replace all the call sites.
    SmallVector<User *, 4> MASSVUsers(Func.users());
    
    for (auto *User : MASSVUsers) {
      auto *CI = dyn_cast<CallInst>(User);
      if (!CI)
        continue;

      Subtarget = &TM.getSubtarget<PPCSubtarget>(*CI->getParent()->getParent());
      Changed |= lowerMASSVCall(CI, Func, M, Subtarget);
    }
  }

  return Changed;
}

char PPCLowerMASSVEntries::ID = 0;

char &llvm::PPCLowerMASSVEntriesID = PPCLowerMASSVEntries::ID;

INITIALIZE_PASS(PPCLowerMASSVEntries, DEBUG_TYPE, "Lower MASSV entries", false,
                false)

ModulePass *llvm::createPPCLowerMASSVEntriesPass() {
  return new PPCLowerMASSVEntries();
}
