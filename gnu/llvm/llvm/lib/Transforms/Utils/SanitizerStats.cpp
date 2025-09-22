//===- SanitizerStats.cpp - Sanitizer statistics gathering ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements code generation for sanitizer statistics gathering.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/SanitizerStats.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;

SanitizerStatReport::SanitizerStatReport(Module *M) : M(M) {
  StatTy = ArrayType::get(PointerType::getUnqual(M->getContext()), 2);
  EmptyModuleStatsTy = makeModuleStatsTy();

  ModuleStatsGV = new GlobalVariable(*M, EmptyModuleStatsTy, false,
                                     GlobalValue::InternalLinkage, nullptr);
}

ArrayType *SanitizerStatReport::makeModuleStatsArrayTy() {
  return ArrayType::get(StatTy, Inits.size());
}

StructType *SanitizerStatReport::makeModuleStatsTy() {
  return StructType::get(M->getContext(),
                         {PointerType::getUnqual(M->getContext()),
                          Type::getInt32Ty(M->getContext()),
                          makeModuleStatsArrayTy()});
}

void SanitizerStatReport::create(IRBuilder<> &B, SanitizerStatKind SK) {
  Function *F = B.GetInsertBlock()->getParent();
  Module *M = F->getParent();
  PointerType *PtrTy = B.getPtrTy();
  IntegerType *IntPtrTy = B.getIntPtrTy(M->getDataLayout());
  ArrayType *StatTy = ArrayType::get(PtrTy, 2);

  Inits.push_back(ConstantArray::get(
      StatTy,
      {Constant::getNullValue(PtrTy),
       ConstantExpr::getIntToPtr(
           ConstantInt::get(IntPtrTy, uint64_t(SK) << (IntPtrTy->getBitWidth() -
                                                       kSanitizerStatKindBits)),
           PtrTy)}));

  FunctionType *StatReportTy = FunctionType::get(B.getVoidTy(), PtrTy, false);
  FunctionCallee StatReport =
      M->getOrInsertFunction("__sanitizer_stat_report", StatReportTy);

  auto InitAddr = ConstantExpr::getGetElementPtr(
      EmptyModuleStatsTy, ModuleStatsGV,
      ArrayRef<Constant *>{
          ConstantInt::get(IntPtrTy, 0), ConstantInt::get(B.getInt32Ty(), 2),
          ConstantInt::get(IntPtrTy, Inits.size() - 1),
      });
  B.CreateCall(StatReport, InitAddr);
}

void SanitizerStatReport::finish() {
  if (Inits.empty()) {
    ModuleStatsGV->eraseFromParent();
    return;
  }

  PointerType *Int8PtrTy = PointerType::getUnqual(M->getContext());
  IntegerType *Int32Ty = Type::getInt32Ty(M->getContext());
  Type *VoidTy = Type::getVoidTy(M->getContext());

  // Create a new ModuleStatsGV to replace the old one. We can't just set the
  // old one's initializer because its type is different.
  auto NewModuleStatsGV = new GlobalVariable(
      *M, makeModuleStatsTy(), false, GlobalValue::InternalLinkage,
      ConstantStruct::getAnon(
          {Constant::getNullValue(Int8PtrTy),
           ConstantInt::get(Int32Ty, Inits.size()),
           ConstantArray::get(makeModuleStatsArrayTy(), Inits)}));
  ModuleStatsGV->replaceAllUsesWith(NewModuleStatsGV);
  ModuleStatsGV->eraseFromParent();

  // Create a global constructor to register NewModuleStatsGV.
  auto F = Function::Create(FunctionType::get(VoidTy, false),
                            GlobalValue::InternalLinkage, "", M);
  auto BB = BasicBlock::Create(M->getContext(), "", F);
  IRBuilder<> B(BB);

  FunctionType *StatInitTy = FunctionType::get(VoidTy, Int8PtrTy, false);
  FunctionCallee StatInit =
      M->getOrInsertFunction("__sanitizer_stat_init", StatInitTy);

  B.CreateCall(StatInit, NewModuleStatsGV);
  B.CreateRetVoid();

  appendToGlobalCtors(*M, F, 0);
}
