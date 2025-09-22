//===-- Instrumentation.cpp - TransformUtils Infrastructure ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the common initialization infrastructure for the
// Instrumentation library.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Instrumentation.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

/// Moves I before IP. Returns new insert point.
static BasicBlock::iterator moveBeforeInsertPoint(BasicBlock::iterator I, BasicBlock::iterator IP) {
  // If I is IP, move the insert point down.
  if (I == IP) {
    ++IP;
  } else {
    // Otherwise, move I before IP and return IP.
    I->moveBefore(&*IP);
  }
  return IP;
}

/// Instrumentation passes often insert conditional checks into entry blocks.
/// Call this function before splitting the entry block to move instructions
/// that must remain in the entry block up before the split point. Static
/// allocas and llvm.localescape calls, for example, must remain in the entry
/// block.
BasicBlock::iterator llvm::PrepareToSplitEntryBlock(BasicBlock &BB,
                                                    BasicBlock::iterator IP) {
  assert(&BB.getParent()->getEntryBlock() == &BB);
  for (auto I = IP, E = BB.end(); I != E; ++I) {
    bool KeepInEntry = false;
    if (auto *AI = dyn_cast<AllocaInst>(I)) {
      if (AI->isStaticAlloca())
        KeepInEntry = true;
    } else if (auto *II = dyn_cast<IntrinsicInst>(I)) {
      if (II->getIntrinsicID() == llvm::Intrinsic::localescape)
        KeepInEntry = true;
    }
    if (KeepInEntry)
      IP = moveBeforeInsertPoint(I, IP);
  }
  return IP;
}

// Create a constant for Str so that we can pass it to the run-time lib.
GlobalVariable *llvm::createPrivateGlobalForString(Module &M, StringRef Str,
                                                   bool AllowMerging,
                                                   const char *NamePrefix) {
  Constant *StrConst = ConstantDataArray::getString(M.getContext(), Str);
  // We use private linkage for module-local strings. If they can be merged
  // with another one, we set the unnamed_addr attribute.
  GlobalVariable *GV =
      new GlobalVariable(M, StrConst->getType(), true,
                         GlobalValue::PrivateLinkage, StrConst, NamePrefix);
  if (AllowMerging)
    GV->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
  GV->setAlignment(Align(1)); // Strings may not be merged w/o setting
                              // alignment explicitly.
  return GV;
}

Comdat *llvm::getOrCreateFunctionComdat(Function &F, Triple &T) {
  if (auto Comdat = F.getComdat()) return Comdat;
  assert(F.hasName());
  Module *M = F.getParent();

  // Make a new comdat for the function. Use the "no duplicates" selection kind
  // if the object file format supports it. For COFF we restrict it to non-weak
  // symbols.
  Comdat *C = M->getOrInsertComdat(F.getName());
  if (T.isOSBinFormatELF() || (T.isOSBinFormatCOFF() && !F.isWeakForLinker()))
    C->setSelectionKind(Comdat::NoDeduplicate);
  F.setComdat(C);
  return C;
}

void llvm::setGlobalVariableLargeSection(const Triple &TargetTriple,
                                         GlobalVariable &GV) {
  // Limit to x86-64 ELF.
  if (TargetTriple.getArch() != Triple::x86_64 ||
      TargetTriple.getObjectFormat() != Triple::ELF)
    return;
  // Limit to medium/large code models.
  std::optional<CodeModel::Model> CM = GV.getParent()->getCodeModel();
  if (!CM || (*CM != CodeModel::Medium && *CM != CodeModel::Large))
    return;
  GV.setCodeModel(CodeModel::Large);
}
