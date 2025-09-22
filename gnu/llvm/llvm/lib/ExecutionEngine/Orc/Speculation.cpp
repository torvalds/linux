//===---------- speculation.cpp - Utilities for Speculation ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/Speculation.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"

namespace llvm {

namespace orc {

// ImplSymbolMap methods
void ImplSymbolMap::trackImpls(SymbolAliasMap ImplMaps, JITDylib *SrcJD) {
  assert(SrcJD && "Tracking on Null Source .impl dylib");
  std::lock_guard<std::mutex> Lockit(ConcurrentAccess);
  for (auto &I : ImplMaps) {
    auto It = Maps.insert({I.first, {I.second.Aliasee, SrcJD}});
    // check rationale when independent dylibs have same symbol name?
    assert(It.second && "ImplSymbols are already tracked for this Symbol?");
    (void)(It);
  }
}

// Trigger Speculative Compiles.
void Speculator::speculateForEntryPoint(Speculator *Ptr, uint64_t StubId) {
  assert(Ptr && " Null Address Received in orc_speculate_for ");
  Ptr->speculateFor(ExecutorAddr(StubId));
}

Error Speculator::addSpeculationRuntime(JITDylib &JD,
                                        MangleAndInterner &Mangle) {
  ExecutorSymbolDef ThisPtr(ExecutorAddr::fromPtr(this),
                            JITSymbolFlags::Exported);
  ExecutorSymbolDef SpeculateForEntryPtr(
      ExecutorAddr::fromPtr(&speculateForEntryPoint), JITSymbolFlags::Exported);
  return JD.define(absoluteSymbols({
      {Mangle("__orc_speculator"), ThisPtr},                // Data Symbol
      {Mangle("__orc_speculate_for"), SpeculateForEntryPtr} // Callable Symbol
  }));
}

// If two modules, share the same LLVMContext, different threads must
// not access them concurrently without locking the associated LLVMContext
// this implementation follows this contract.
void IRSpeculationLayer::emit(std::unique_ptr<MaterializationResponsibility> R,
                              ThreadSafeModule TSM) {

  assert(TSM && "Speculation Layer received Null Module ?");
  assert(TSM.getContext().getContext() != nullptr &&
         "Module with null LLVMContext?");

  // Instrumentation of runtime calls, lock the Module
  TSM.withModuleDo([this, &R](Module &M) {
    auto &MContext = M.getContext();
    auto SpeculatorVTy = StructType::create(MContext, "Class.Speculator");
    auto RuntimeCallTy = FunctionType::get(
        Type::getVoidTy(MContext),
        {PointerType::getUnqual(MContext), Type::getInt64Ty(MContext)}, false);
    auto RuntimeCall =
        Function::Create(RuntimeCallTy, Function::LinkageTypes::ExternalLinkage,
                         "__orc_speculate_for", &M);
    auto SpeclAddr = new GlobalVariable(
        M, SpeculatorVTy, false, GlobalValue::LinkageTypes::ExternalLinkage,
        nullptr, "__orc_speculator");

    IRBuilder<> Mutator(MContext);

    // QueryAnalysis allowed to transform the IR source, one such example is
    // Simplify CFG helps the static branch prediction heuristics!
    for (auto &Fn : M.getFunctionList()) {
      if (!Fn.isDeclaration()) {

        auto IRNames = QueryAnalysis(Fn);
        // Instrument and register if Query has result
        if (IRNames) {

          // Emit globals for each function.
          auto LoadValueTy = Type::getInt8Ty(MContext);
          auto SpeculatorGuard = new GlobalVariable(
              M, LoadValueTy, false, GlobalValue::LinkageTypes::InternalLinkage,
              ConstantInt::get(LoadValueTy, 0),
              "__orc_speculate.guard.for." + Fn.getName());
          SpeculatorGuard->setAlignment(Align(1));
          SpeculatorGuard->setUnnamedAddr(GlobalValue::UnnamedAddr::Local);

          BasicBlock &ProgramEntry = Fn.getEntryBlock();
          // Create BasicBlocks before the program's entry basicblock
          BasicBlock *SpeculateBlock = BasicBlock::Create(
              MContext, "__orc_speculate.block", &Fn, &ProgramEntry);
          BasicBlock *SpeculateDecisionBlock = BasicBlock::Create(
              MContext, "__orc_speculate.decision.block", &Fn, SpeculateBlock);

          assert(SpeculateDecisionBlock == &Fn.getEntryBlock() &&
                 "SpeculateDecisionBlock not updated?");
          Mutator.SetInsertPoint(SpeculateDecisionBlock);

          auto LoadGuard =
              Mutator.CreateLoad(LoadValueTy, SpeculatorGuard, "guard.value");
          // if just loaded value equal to 0,return true.
          auto CanSpeculate =
              Mutator.CreateICmpEQ(LoadGuard, ConstantInt::get(LoadValueTy, 0),
                                   "compare.to.speculate");
          Mutator.CreateCondBr(CanSpeculate, SpeculateBlock, &ProgramEntry);

          Mutator.SetInsertPoint(SpeculateBlock);
          auto ImplAddrToUint =
              Mutator.CreatePtrToInt(&Fn, Type::getInt64Ty(MContext));
          Mutator.CreateCall(RuntimeCallTy, RuntimeCall,
                             {SpeclAddr, ImplAddrToUint});
          Mutator.CreateStore(ConstantInt::get(LoadValueTy, 1),
                              SpeculatorGuard);
          Mutator.CreateBr(&ProgramEntry);

          assert(Mutator.GetInsertBlock()->getParent() == &Fn &&
                 "IR builder association mismatch?");
          S.registerSymbols(internToJITSymbols(*IRNames),
                            &R->getTargetJITDylib());
        }
      }
    }
  });

  assert(!TSM.withModuleDo([](const Module &M) { return verifyModule(M); }) &&
         "Speculation Instrumentation breaks IR?");

  NextLayer.emit(std::move(R), std::move(TSM));
}

} // namespace orc
} // namespace llvm
