//===-- WebAssemblyFixFunctionBitcasts.cpp - Fix function bitcasts --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Fix bitcasted functions.
///
/// WebAssembly requires caller and callee signatures to match, however in LLVM,
/// some amount of slop is vaguely permitted. Detect mismatch by looking for
/// bitcasts of functions and rewrite them to use wrapper functions instead.
///
/// This doesn't catch all cases, such as when a function's address is taken in
/// one place and casted in another, but it works for many common cases.
///
/// Note that LLVM already optimizes away function bitcasts in common cases by
/// dropping arguments as needed, so this pass only ends up getting used in less
/// common cases.
///
//===----------------------------------------------------------------------===//

#include "WebAssembly.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-fix-function-bitcasts"

namespace {
class FixFunctionBitcasts final : public ModulePass {
  StringRef getPassName() const override {
    return "WebAssembly Fix Function Bitcasts";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    ModulePass::getAnalysisUsage(AU);
  }

  bool runOnModule(Module &M) override;

public:
  static char ID;
  FixFunctionBitcasts() : ModulePass(ID) {}
};
} // End anonymous namespace

char FixFunctionBitcasts::ID = 0;
INITIALIZE_PASS(FixFunctionBitcasts, DEBUG_TYPE,
                "Fix mismatching bitcasts for WebAssembly", false, false)

ModulePass *llvm::createWebAssemblyFixFunctionBitcasts() {
  return new FixFunctionBitcasts();
}

// Recursively descend the def-use lists from V to find non-bitcast users of
// bitcasts of V.
static void findUses(Value *V, Function &F,
                     SmallVectorImpl<std::pair<CallBase *, Function *>> &Uses) {
  for (User *U : V->users()) {
    if (auto *BC = dyn_cast<BitCastOperator>(U))
      findUses(BC, F, Uses);
    else if (auto *A = dyn_cast<GlobalAlias>(U))
      findUses(A, F, Uses);
    else if (auto *CB = dyn_cast<CallBase>(U)) {
      Value *Callee = CB->getCalledOperand();
      if (Callee != V)
        // Skip calls where the function isn't the callee
        continue;
      if (CB->getFunctionType() == F.getValueType())
        // Skip uses that are immediately called
        continue;
      Uses.push_back(std::make_pair(CB, &F));
    }
  }
}

// Create a wrapper function with type Ty that calls F (which may have a
// different type). Attempt to support common bitcasted function idioms:
//  - Call with more arguments than needed: arguments are dropped
//  - Call with fewer arguments than needed: arguments are filled in with undef
//  - Return value is not needed: drop it
//  - Return value needed but not present: supply an undef
//
// If the all the argument types of trivially castable to one another (i.e.
// I32 vs pointer type) then we don't create a wrapper at all (return nullptr
// instead).
//
// If there is a type mismatch that we know would result in an invalid wasm
// module then generate wrapper that contains unreachable (i.e. abort at
// runtime).  Such programs are deep into undefined behaviour territory,
// but we choose to fail at runtime rather than generate and invalid module
// or fail at compiler time.  The reason we delay the error is that we want
// to support the CMake which expects to be able to compile and link programs
// that refer to functions with entirely incorrect signatures (this is how
// CMake detects the existence of a function in a toolchain).
//
// For bitcasts that involve struct types we don't know at this stage if they
// would be equivalent at the wasm level and so we can't know if we need to
// generate a wrapper.
static Function *createWrapper(Function *F, FunctionType *Ty) {
  Module *M = F->getParent();

  Function *Wrapper = Function::Create(Ty, Function::PrivateLinkage,
                                       F->getName() + "_bitcast", M);
  BasicBlock *BB = BasicBlock::Create(M->getContext(), "body", Wrapper);
  const DataLayout &DL = BB->getDataLayout();

  // Determine what arguments to pass.
  SmallVector<Value *, 4> Args;
  Function::arg_iterator AI = Wrapper->arg_begin();
  Function::arg_iterator AE = Wrapper->arg_end();
  FunctionType::param_iterator PI = F->getFunctionType()->param_begin();
  FunctionType::param_iterator PE = F->getFunctionType()->param_end();
  bool TypeMismatch = false;
  bool WrapperNeeded = false;

  Type *ExpectedRtnType = F->getFunctionType()->getReturnType();
  Type *RtnType = Ty->getReturnType();

  if ((F->getFunctionType()->getNumParams() != Ty->getNumParams()) ||
      (F->getFunctionType()->isVarArg() != Ty->isVarArg()) ||
      (ExpectedRtnType != RtnType))
    WrapperNeeded = true;

  for (; AI != AE && PI != PE; ++AI, ++PI) {
    Type *ArgType = AI->getType();
    Type *ParamType = *PI;

    if (ArgType == ParamType) {
      Args.push_back(&*AI);
    } else {
      if (CastInst::isBitOrNoopPointerCastable(ArgType, ParamType, DL)) {
        Instruction *PtrCast =
            CastInst::CreateBitOrPointerCast(AI, ParamType, "cast");
        PtrCast->insertInto(BB, BB->end());
        Args.push_back(PtrCast);
      } else if (ArgType->isStructTy() || ParamType->isStructTy()) {
        LLVM_DEBUG(dbgs() << "createWrapper: struct param type in bitcast: "
                          << F->getName() << "\n");
        WrapperNeeded = false;
      } else {
        LLVM_DEBUG(dbgs() << "createWrapper: arg type mismatch calling: "
                          << F->getName() << "\n");
        LLVM_DEBUG(dbgs() << "Arg[" << Args.size() << "] Expected: "
                          << *ParamType << " Got: " << *ArgType << "\n");
        TypeMismatch = true;
        break;
      }
    }
  }

  if (WrapperNeeded && !TypeMismatch) {
    for (; PI != PE; ++PI)
      Args.push_back(UndefValue::get(*PI));
    if (F->isVarArg())
      for (; AI != AE; ++AI)
        Args.push_back(&*AI);

    CallInst *Call = CallInst::Create(F, Args, "", BB);

    Type *ExpectedRtnType = F->getFunctionType()->getReturnType();
    Type *RtnType = Ty->getReturnType();
    // Determine what value to return.
    if (RtnType->isVoidTy()) {
      ReturnInst::Create(M->getContext(), BB);
    } else if (ExpectedRtnType->isVoidTy()) {
      LLVM_DEBUG(dbgs() << "Creating dummy return: " << *RtnType << "\n");
      ReturnInst::Create(M->getContext(), UndefValue::get(RtnType), BB);
    } else if (RtnType == ExpectedRtnType) {
      ReturnInst::Create(M->getContext(), Call, BB);
    } else if (CastInst::isBitOrNoopPointerCastable(ExpectedRtnType, RtnType,
                                                    DL)) {
      Instruction *Cast =
          CastInst::CreateBitOrPointerCast(Call, RtnType, "cast");
      Cast->insertInto(BB, BB->end());
      ReturnInst::Create(M->getContext(), Cast, BB);
    } else if (RtnType->isStructTy() || ExpectedRtnType->isStructTy()) {
      LLVM_DEBUG(dbgs() << "createWrapper: struct return type in bitcast: "
                        << F->getName() << "\n");
      WrapperNeeded = false;
    } else {
      LLVM_DEBUG(dbgs() << "createWrapper: return type mismatch calling: "
                        << F->getName() << "\n");
      LLVM_DEBUG(dbgs() << "Expected: " << *ExpectedRtnType
                        << " Got: " << *RtnType << "\n");
      TypeMismatch = true;
    }
  }

  if (TypeMismatch) {
    // Create a new wrapper that simply contains `unreachable`.
    Wrapper->eraseFromParent();
    Wrapper = Function::Create(Ty, Function::PrivateLinkage,
                               F->getName() + "_bitcast_invalid", M);
    BasicBlock *BB = BasicBlock::Create(M->getContext(), "body", Wrapper);
    new UnreachableInst(M->getContext(), BB);
    Wrapper->setName(F->getName() + "_bitcast_invalid");
  } else if (!WrapperNeeded) {
    LLVM_DEBUG(dbgs() << "createWrapper: no wrapper needed: " << F->getName()
                      << "\n");
    Wrapper->eraseFromParent();
    return nullptr;
  }
  LLVM_DEBUG(dbgs() << "createWrapper: " << F->getName() << "\n");
  return Wrapper;
}

// Test whether a main function with type FuncTy should be rewritten to have
// type MainTy.
static bool shouldFixMainFunction(FunctionType *FuncTy, FunctionType *MainTy) {
  // Only fix the main function if it's the standard zero-arg form. That way,
  // the standard cases will work as expected, and users will see signature
  // mismatches from the linker for non-standard cases.
  return FuncTy->getReturnType() == MainTy->getReturnType() &&
         FuncTy->getNumParams() == 0 &&
         !FuncTy->isVarArg();
}

bool FixFunctionBitcasts::runOnModule(Module &M) {
  LLVM_DEBUG(dbgs() << "********** Fix Function Bitcasts **********\n");

  Function *Main = nullptr;
  CallInst *CallMain = nullptr;
  SmallVector<std::pair<CallBase *, Function *>, 0> Uses;

  // Collect all the places that need wrappers.
  for (Function &F : M) {
    // Skip to fix when the function is swiftcc because swiftcc allows
    // bitcast type difference for swiftself and swifterror.
    if (F.getCallingConv() == CallingConv::Swift)
      continue;
    findUses(&F, F, Uses);

    // If we have a "main" function, and its type isn't
    // "int main(int argc, char *argv[])", create an artificial call with it
    // bitcasted to that type so that we generate a wrapper for it, so that
    // the C runtime can call it.
    if (F.getName() == "main") {
      Main = &F;
      LLVMContext &C = M.getContext();
      Type *MainArgTys[] = {Type::getInt32Ty(C), PointerType::get(C, 0)};
      FunctionType *MainTy = FunctionType::get(Type::getInt32Ty(C), MainArgTys,
                                               /*isVarArg=*/false);
      if (shouldFixMainFunction(F.getFunctionType(), MainTy)) {
        LLVM_DEBUG(dbgs() << "Found `main` function with incorrect type: "
                          << *F.getFunctionType() << "\n");
        Value *Args[] = {UndefValue::get(MainArgTys[0]),
                         UndefValue::get(MainArgTys[1])};
        CallMain = CallInst::Create(MainTy, Main, Args, "call_main");
        Uses.push_back(std::make_pair(CallMain, &F));
      }
    }
  }

  DenseMap<std::pair<Function *, FunctionType *>, Function *> Wrappers;

  for (auto &UseFunc : Uses) {
    CallBase *CB = UseFunc.first;
    Function *F = UseFunc.second;
    FunctionType *Ty = CB->getFunctionType();

    auto Pair = Wrappers.insert(std::make_pair(std::make_pair(F, Ty), nullptr));
    if (Pair.second)
      Pair.first->second = createWrapper(F, Ty);

    Function *Wrapper = Pair.first->second;
    if (!Wrapper)
      continue;

    CB->setCalledOperand(Wrapper);
  }

  // If we created a wrapper for main, rename the wrapper so that it's the
  // one that gets called from startup.
  if (CallMain) {
    Main->setName("__original_main");
    auto *MainWrapper =
        cast<Function>(CallMain->getCalledOperand()->stripPointerCasts());
    delete CallMain;
    if (Main->isDeclaration()) {
      // The wrapper is not needed in this case as we don't need to export
      // it to anyone else.
      MainWrapper->eraseFromParent();
    } else {
      // Otherwise give the wrapper the same linkage as the original main
      // function, so that it can be called from the same places.
      MainWrapper->setName("main");
      MainWrapper->setLinkage(Main->getLinkage());
      MainWrapper->setVisibility(Main->getVisibility());
    }
  }

  return true;
}
