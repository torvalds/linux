//===-- CrossDSOCFI.cpp - Externalize this module's CFI checks ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass exports all llvm.bitset's found in the module in the form of a
// __cfi_check function, which can be used to verify cross-DSO call targets.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/CrossDSOCFI.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/IPO.h"

using namespace llvm;

#define DEBUG_TYPE "cross-dso-cfi"

STATISTIC(NumTypeIds, "Number of unique type identifiers");

namespace {

struct CrossDSOCFI {
  MDNode *VeryLikelyWeights;

  ConstantInt *extractNumericTypeId(MDNode *MD);
  void buildCFICheck(Module &M);
  bool runOnModule(Module &M);
};

} // anonymous namespace

/// Extracts a numeric type identifier from an MDNode containing type metadata.
ConstantInt *CrossDSOCFI::extractNumericTypeId(MDNode *MD) {
  // This check excludes vtables for classes inside anonymous namespaces.
  auto TM = dyn_cast<ValueAsMetadata>(MD->getOperand(1));
  if (!TM)
    return nullptr;
  auto C = dyn_cast_or_null<ConstantInt>(TM->getValue());
  if (!C) return nullptr;
  // We are looking for i64 constants.
  if (C->getBitWidth() != 64) return nullptr;

  return C;
}

/// buildCFICheck - emits __cfi_check for the current module.
void CrossDSOCFI::buildCFICheck(Module &M) {
  // FIXME: verify that __cfi_check ends up near the end of the code section,
  // but before the jump slots created in LowerTypeTests.
  SetVector<uint64_t> TypeIds;
  SmallVector<MDNode *, 2> Types;
  for (GlobalObject &GO : M.global_objects()) {
    Types.clear();
    GO.getMetadata(LLVMContext::MD_type, Types);
    for (MDNode *Type : Types)
      if (ConstantInt *TypeId = extractNumericTypeId(Type))
        TypeIds.insert(TypeId->getZExtValue());
  }

  NamedMDNode *CfiFunctionsMD = M.getNamedMetadata("cfi.functions");
  if (CfiFunctionsMD) {
    for (auto *Func : CfiFunctionsMD->operands()) {
      assert(Func->getNumOperands() >= 2);
      for (unsigned I = 2; I < Func->getNumOperands(); ++I)
        if (ConstantInt *TypeId =
                extractNumericTypeId(cast<MDNode>(Func->getOperand(I).get())))
          TypeIds.insert(TypeId->getZExtValue());
    }
  }

  LLVMContext &Ctx = M.getContext();
  FunctionCallee C = M.getOrInsertFunction(
      "__cfi_check", Type::getVoidTy(Ctx), Type::getInt64Ty(Ctx),
      PointerType::getUnqual(Ctx), PointerType::getUnqual(Ctx));
  Function *F = cast<Function>(C.getCallee());
  // Take over the existing function. The frontend emits a weak stub so that the
  // linker knows about the symbol; this pass replaces the function body.
  F->deleteBody();
  F->setAlignment(Align(4096));

  Triple T(M.getTargetTriple());
  if (T.isARM() || T.isThumb())
    F->addFnAttr("target-features", "+thumb-mode");

  auto args = F->arg_begin();
  Value &CallSiteTypeId = *(args++);
  CallSiteTypeId.setName("CallSiteTypeId");
  Value &Addr = *(args++);
  Addr.setName("Addr");
  Value &CFICheckFailData = *(args++);
  CFICheckFailData.setName("CFICheckFailData");
  assert(args == F->arg_end());

  BasicBlock *BB = BasicBlock::Create(Ctx, "entry", F);
  BasicBlock *ExitBB = BasicBlock::Create(Ctx, "exit", F);

  BasicBlock *TrapBB = BasicBlock::Create(Ctx, "fail", F);
  IRBuilder<> IRBFail(TrapBB);
  FunctionCallee CFICheckFailFn = M.getOrInsertFunction(
      "__cfi_check_fail", Type::getVoidTy(Ctx), PointerType::getUnqual(Ctx),
      PointerType::getUnqual(Ctx));
  IRBFail.CreateCall(CFICheckFailFn, {&CFICheckFailData, &Addr});
  IRBFail.CreateBr(ExitBB);

  IRBuilder<> IRBExit(ExitBB);
  IRBExit.CreateRetVoid();

  IRBuilder<> IRB(BB);
  SwitchInst *SI = IRB.CreateSwitch(&CallSiteTypeId, TrapBB, TypeIds.size());
  for (uint64_t TypeId : TypeIds) {
    ConstantInt *CaseTypeId = ConstantInt::get(Type::getInt64Ty(Ctx), TypeId);
    BasicBlock *TestBB = BasicBlock::Create(Ctx, "test", F);
    IRBuilder<> IRBTest(TestBB);
    Function *BitsetTestFn = Intrinsic::getDeclaration(&M, Intrinsic::type_test);

    Value *Test = IRBTest.CreateCall(
        BitsetTestFn, {&Addr, MetadataAsValue::get(
                                  Ctx, ConstantAsMetadata::get(CaseTypeId))});
    BranchInst *BI = IRBTest.CreateCondBr(Test, ExitBB, TrapBB);
    BI->setMetadata(LLVMContext::MD_prof, VeryLikelyWeights);

    SI->addCase(CaseTypeId, TestBB);
    ++NumTypeIds;
  }
}

bool CrossDSOCFI::runOnModule(Module &M) {
  VeryLikelyWeights = MDBuilder(M.getContext()).createLikelyBranchWeights();
  if (M.getModuleFlag("Cross-DSO CFI") == nullptr)
    return false;
  buildCFICheck(M);
  return true;
}

PreservedAnalyses CrossDSOCFIPass::run(Module &M, ModuleAnalysisManager &AM) {
  CrossDSOCFI Impl;
  bool Changed = Impl.runOnModule(M);
  if (!Changed)
    return PreservedAnalyses::all();
  return PreservedAnalyses::none();
}
