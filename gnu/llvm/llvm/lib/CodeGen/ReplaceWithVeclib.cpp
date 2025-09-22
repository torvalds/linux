//=== ReplaceWithVeclib.cpp - Replace vector intrinsics with veclib calls -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Replaces calls to LLVM Intrinsics with matching calls to functions from a
// vector library (e.g libmvec, SVML) using TargetLibraryInfo interface.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/ReplaceWithVeclib.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/DemandedBits.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/VFABIDemangler.h"
#include "llvm/Support/TypeSize.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;

#define DEBUG_TYPE "replace-with-veclib"

STATISTIC(NumCallsReplaced,
          "Number of calls to intrinsics that have been replaced.");

STATISTIC(NumTLIFuncDeclAdded,
          "Number of vector library function declarations added.");

STATISTIC(NumFuncUsedAdded,
          "Number of functions added to `llvm.compiler.used`");

/// Returns a vector Function that it adds to the Module \p M. When an \p
/// ScalarFunc is not null, it copies its attributes to the newly created
/// Function.
Function *getTLIFunction(Module *M, FunctionType *VectorFTy,
                         const StringRef TLIName,
                         Function *ScalarFunc = nullptr) {
  Function *TLIFunc = M->getFunction(TLIName);
  if (!TLIFunc) {
    TLIFunc =
        Function::Create(VectorFTy, Function::ExternalLinkage, TLIName, *M);
    if (ScalarFunc)
      TLIFunc->copyAttributesFrom(ScalarFunc);

    LLVM_DEBUG(dbgs() << DEBUG_TYPE << ": Added vector library function `"
                      << TLIName << "` of type `" << *(TLIFunc->getType())
                      << "` to module.\n");

    ++NumTLIFuncDeclAdded;
    // Add the freshly created function to llvm.compiler.used, similar to as it
    // is done in InjectTLIMappings.
    appendToCompilerUsed(*M, {TLIFunc});
    LLVM_DEBUG(dbgs() << DEBUG_TYPE << ": Adding `" << TLIName
                      << "` to `@llvm.compiler.used`.\n");
    ++NumFuncUsedAdded;
  }
  return TLIFunc;
}

/// Replace the intrinsic call \p II to \p TLIVecFunc, which is the
/// corresponding function from the vector library.
static void replaceWithTLIFunction(IntrinsicInst *II, VFInfo &Info,
                                   Function *TLIVecFunc) {
  IRBuilder<> IRBuilder(II);
  SmallVector<Value *> Args(II->args());
  if (auto OptMaskpos = Info.getParamIndexForOptionalMask()) {
    auto *MaskTy =
        VectorType::get(Type::getInt1Ty(II->getContext()), Info.Shape.VF);
    Args.insert(Args.begin() + OptMaskpos.value(),
                Constant::getAllOnesValue(MaskTy));
  }

  // Preserve the operand bundles.
  SmallVector<OperandBundleDef, 1> OpBundles;
  II->getOperandBundlesAsDefs(OpBundles);

  auto *Replacement = IRBuilder.CreateCall(TLIVecFunc, Args, OpBundles);
  II->replaceAllUsesWith(Replacement);
  // Preserve fast math flags for FP math.
  if (isa<FPMathOperator>(Replacement))
    Replacement->copyFastMathFlags(II);
}

/// Returns true when successfully replaced \p II, which is a call to a
/// vectorized intrinsic, with a suitable function taking vector arguments,
/// based on available mappings in the \p TLI.
static bool replaceWithCallToVeclib(const TargetLibraryInfo &TLI,
                                    IntrinsicInst *II) {
  assert(II != nullptr && "Intrinsic cannot be null");
  // At the moment VFABI assumes the return type is always widened unless it is
  // a void type.
  auto *VTy = dyn_cast<VectorType>(II->getType());
  ElementCount EC(VTy ? VTy->getElementCount() : ElementCount::getFixed(0));
  // Compute the argument types of the corresponding scalar call and check that
  // all vector operands match the previously found EC.
  SmallVector<Type *, 8> ScalarArgTypes;
  Intrinsic::ID IID = II->getIntrinsicID();
  for (auto Arg : enumerate(II->args())) {
    auto *ArgTy = Arg.value()->getType();
    if (isVectorIntrinsicWithScalarOpAtArg(IID, Arg.index())) {
      ScalarArgTypes.push_back(ArgTy);
    } else if (auto *VectorArgTy = dyn_cast<VectorType>(ArgTy)) {
      ScalarArgTypes.push_back(VectorArgTy->getElementType());
      // When return type is void, set EC to the first vector argument, and
      // disallow vector arguments with different ECs.
      if (EC.isZero())
        EC = VectorArgTy->getElementCount();
      else if (EC != VectorArgTy->getElementCount())
        return false;
    } else
      // Exit when it is supposed to be a vector argument but it isn't.
      return false;
  }

  // Try to reconstruct the name for the scalar version of the instruction,
  // using scalar argument types.
  std::string ScalarName =
      Intrinsic::isOverloaded(IID)
          ? Intrinsic::getName(IID, ScalarArgTypes, II->getModule())
          : Intrinsic::getName(IID).str();

  // Try to find the mapping for the scalar version of this intrinsic and the
  // exact vector width of the call operands in the TargetLibraryInfo. First,
  // check with a non-masked variant, and if that fails try with a masked one.
  const VecDesc *VD =
      TLI.getVectorMappingInfo(ScalarName, EC, /*Masked*/ false);
  if (!VD && !(VD = TLI.getVectorMappingInfo(ScalarName, EC, /*Masked*/ true)))
    return false;

  LLVM_DEBUG(dbgs() << DEBUG_TYPE << ": Found TLI mapping from: `" << ScalarName
                    << "` and vector width " << EC << " to: `"
                    << VD->getVectorFnName() << "`.\n");

  // Replace the call to the intrinsic with a call to the vector library
  // function.
  Type *ScalarRetTy = II->getType()->getScalarType();
  FunctionType *ScalarFTy =
      FunctionType::get(ScalarRetTy, ScalarArgTypes, /*isVarArg*/ false);
  const std::string MangledName = VD->getVectorFunctionABIVariantString();
  auto OptInfo = VFABI::tryDemangleForVFABI(MangledName, ScalarFTy);
  if (!OptInfo)
    return false;

  // There is no guarantee that the vectorized instructions followed the VFABI
  // specification when being created, this is why we need to add extra check to
  // make sure that the operands of the vector function obtained via VFABI match
  // the operands of the original vector instruction.
  for (auto &VFParam : OptInfo->Shape.Parameters) {
    if (VFParam.ParamKind == VFParamKind::GlobalPredicate)
      continue;

    // tryDemangleForVFABI must return valid ParamPos, otherwise it could be
    // a bug in the VFABI parser.
    assert(VFParam.ParamPos < II->arg_size() && "ParamPos has invalid range");
    Type *OrigTy = II->getArgOperand(VFParam.ParamPos)->getType();
    if (OrigTy->isVectorTy() != (VFParam.ParamKind == VFParamKind::Vector)) {
      LLVM_DEBUG(dbgs() << DEBUG_TYPE << ": Will not replace: " << ScalarName
                        << ". Wrong type at index " << VFParam.ParamPos << ": "
                        << *OrigTy << "\n");
      return false;
    }
  }

  FunctionType *VectorFTy = VFABI::createFunctionType(*OptInfo, ScalarFTy);
  if (!VectorFTy)
    return false;

  Function *TLIFunc =
      getTLIFunction(II->getModule(), VectorFTy, VD->getVectorFnName(),
                     II->getCalledFunction());
  replaceWithTLIFunction(II, *OptInfo, TLIFunc);
  LLVM_DEBUG(dbgs() << DEBUG_TYPE << ": Replaced call to `" << ScalarName
                    << "` with call to `" << TLIFunc->getName() << "`.\n");
  ++NumCallsReplaced;
  return true;
}

static bool runImpl(const TargetLibraryInfo &TLI, Function &F) {
  SmallVector<Instruction *> ReplacedCalls;
  for (auto &I : instructions(F)) {
    // Process only intrinsic calls that return void or a vector.
    if (auto *II = dyn_cast<IntrinsicInst>(&I)) {
      if (!II->getType()->isVectorTy() && !II->getType()->isVoidTy())
        continue;

      if (replaceWithCallToVeclib(TLI, II))
        ReplacedCalls.push_back(&I);
    }
  }
  // Erase any intrinsic calls that were replaced with vector library calls.
  for (auto *I : ReplacedCalls)
    I->eraseFromParent();
  return !ReplacedCalls.empty();
}

////////////////////////////////////////////////////////////////////////////////
// New pass manager implementation.
////////////////////////////////////////////////////////////////////////////////
PreservedAnalyses ReplaceWithVeclib::run(Function &F,
                                         FunctionAnalysisManager &AM) {
  const TargetLibraryInfo &TLI = AM.getResult<TargetLibraryAnalysis>(F);
  auto Changed = runImpl(TLI, F);
  if (Changed) {
    LLVM_DEBUG(dbgs() << "Intrinsic calls replaced with vector libraries: "
                      << NumCallsReplaced << "\n");

    PreservedAnalyses PA;
    PA.preserveSet<CFGAnalyses>();
    PA.preserve<TargetLibraryAnalysis>();
    PA.preserve<ScalarEvolutionAnalysis>();
    PA.preserve<LoopAccessAnalysis>();
    PA.preserve<DemandedBitsAnalysis>();
    PA.preserve<OptimizationRemarkEmitterAnalysis>();
    return PA;
  }

  // The pass did not replace any calls, hence it preserves all analyses.
  return PreservedAnalyses::all();
}

////////////////////////////////////////////////////////////////////////////////
// Legacy PM Implementation.
////////////////////////////////////////////////////////////////////////////////
bool ReplaceWithVeclibLegacy::runOnFunction(Function &F) {
  const TargetLibraryInfo &TLI =
      getAnalysis<TargetLibraryInfoWrapperPass>().getTLI(F);
  return runImpl(TLI, F);
}

void ReplaceWithVeclibLegacy::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesCFG();
  AU.addRequired<TargetLibraryInfoWrapperPass>();
  AU.addPreserved<TargetLibraryInfoWrapperPass>();
  AU.addPreserved<ScalarEvolutionWrapperPass>();
  AU.addPreserved<AAResultsWrapperPass>();
  AU.addPreserved<OptimizationRemarkEmitterWrapperPass>();
  AU.addPreserved<GlobalsAAWrapperPass>();
}

////////////////////////////////////////////////////////////////////////////////
// Legacy Pass manager initialization
////////////////////////////////////////////////////////////////////////////////
char ReplaceWithVeclibLegacy::ID = 0;

INITIALIZE_PASS_BEGIN(ReplaceWithVeclibLegacy, DEBUG_TYPE,
                      "Replace intrinsics with calls to vector library", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_END(ReplaceWithVeclibLegacy, DEBUG_TYPE,
                    "Replace intrinsics with calls to vector library", false,
                    false)

FunctionPass *llvm::createReplaceWithVeclibLegacyPass() {
  return new ReplaceWithVeclibLegacy();
}
