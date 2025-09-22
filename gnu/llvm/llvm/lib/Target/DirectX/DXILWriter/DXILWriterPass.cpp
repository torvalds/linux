//===- DXILWriterPass.cpp - Bitcode writing pass --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// DXILWriterPass implementation.
//
//===----------------------------------------------------------------------===//

#include "DXILWriterPass.h"
#include "DXILBitcodeWriter.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/ModuleSummaryAnalysis.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;
using namespace llvm::dxil;

namespace {
class WriteDXILPass : public llvm::ModulePass {
  raw_ostream &OS; // raw_ostream to print on

public:
  static char ID; // Pass identification, replacement for typeid
  WriteDXILPass() : ModulePass(ID), OS(dbgs()) {
    initializeWriteDXILPassPass(*PassRegistry::getPassRegistry());
  }

  explicit WriteDXILPass(raw_ostream &o) : ModulePass(ID), OS(o) {
    initializeWriteDXILPassPass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override { return "Bitcode Writer"; }

  bool runOnModule(Module &M) override {
    WriteDXILToFile(M, OS);
    return false;
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }
};

class EmbedDXILPass : public llvm::ModulePass {
public:
  static char ID; // Pass identification, replacement for typeid
  EmbedDXILPass() : ModulePass(ID) {
    initializeEmbedDXILPassPass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override { return "DXIL Embedder"; }

  bool runOnModule(Module &M) override {
    std::string Data;
    llvm::raw_string_ostream OS(Data);

    const std::string OriginalTriple = M.getTargetTriple();
    // Set to DXIL triple when write to bitcode.
    // Only the output bitcode need to be DXIL triple.
    M.setTargetTriple("dxil-ms-dx");

    WriteDXILToFile(M, OS);

    // Recover triple.
    M.setTargetTriple(OriginalTriple);

    Constant *ModuleConstant =
        ConstantDataArray::get(M.getContext(), arrayRefFromStringRef(Data));
    auto *GV = new llvm::GlobalVariable(M, ModuleConstant->getType(), true,
                                        GlobalValue::PrivateLinkage,
                                        ModuleConstant, "dx.dxil");
    GV->setSection("DXIL");
    GV->setAlignment(Align(4));
    appendToCompilerUsed(M, {GV});
    return true;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }
};
} // namespace

char WriteDXILPass::ID = 0;
INITIALIZE_PASS_BEGIN(WriteDXILPass, "dxil-write-bitcode", "Write Bitcode",
                      false, true)
INITIALIZE_PASS_DEPENDENCY(ModuleSummaryIndexWrapperPass)
INITIALIZE_PASS_END(WriteDXILPass, "dxil-write-bitcode", "Write Bitcode", false,
                    true)

ModulePass *llvm::createDXILWriterPass(raw_ostream &Str) {
  return new WriteDXILPass(Str);
}

char EmbedDXILPass::ID = 0;
INITIALIZE_PASS(EmbedDXILPass, "dxil-embed", "Embed DXIL", false, true)

ModulePass *llvm::createDXILEmbedderPass() { return new EmbedDXILPass(); }
