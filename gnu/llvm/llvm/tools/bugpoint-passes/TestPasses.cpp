//===- TestPasses.cpp - "buggy" passes used to test bugpoint --------------===//
//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains "buggy" passes that are used to test bugpoint, to check
// that it is narrowing down testcases correctly.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/Pass.h"

#include "llvm/IR/PatternMatch.h"
using namespace llvm::PatternMatch;
using namespace llvm;

namespace {
/// CrashOnCalls - This pass is used to test bugpoint.  It intentionally
/// crashes on any call instructions.
class CrashOnCalls : public FunctionPass {
public:
  static char ID; // Pass ID, replacement for typeid
  CrashOnCalls() : FunctionPass(ID) {}

private:
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

  bool runOnFunction(Function &F) override {
    for (auto &BB : F)
      for (BasicBlock::iterator I = BB.begin(), E = BB.end(); I != E; ++I)
        if (isa<CallInst>(*I))
          abort();

    return false;
  }
};
}

char CrashOnCalls::ID = 0;
static RegisterPass<CrashOnCalls>
  X("bugpoint-crashcalls",
    "BugPoint Test Pass - Intentionally crash on CallInsts");

namespace {
/// DeleteCalls - This pass is used to test bugpoint.  It intentionally
/// deletes some call instructions, "misoptimizing" the program.
class DeleteCalls : public FunctionPass {
public:
  static char ID; // Pass ID, replacement for typeid
  DeleteCalls() : FunctionPass(ID) {}

private:
  bool runOnFunction(Function &F) override {
    for (auto &BB : F)
      for (BasicBlock::iterator I = BB.begin(), E = BB.end(); I != E; ++I)
        if (CallInst *CI = dyn_cast<CallInst>(I)) {
          if (!CI->use_empty())
            CI->replaceAllUsesWith(Constant::getNullValue(CI->getType()));
          CI->eraseFromParent();
          break;
        }
    return false;
  }
};
}

char DeleteCalls::ID = 0;
static RegisterPass<DeleteCalls>
  Y("bugpoint-deletecalls",
    "BugPoint Test Pass - Intentionally 'misoptimize' CallInsts");

namespace {
  /// CrashOnDeclFunc - This pass is used to test bugpoint.  It intentionally
/// crashes if the module has an undefined function (ie a function that is
/// defined in an external module).
class CrashOnDeclFunc : public ModulePass {
public:
  static char ID; // Pass ID, replacement for typeid
  CrashOnDeclFunc() : ModulePass(ID) {}

private:
  bool runOnModule(Module &M) override {
    for (auto &F : M.functions()) {
      if (F.isDeclaration())
        abort();
    }
    return false;
  }
  };
}

char CrashOnDeclFunc::ID = 0;
static RegisterPass<CrashOnDeclFunc>
  Z("bugpoint-crash-decl-funcs",
    "BugPoint Test Pass - Intentionally crash on declared functions");

namespace {
/// CrashOnOneCU - This pass is used to test bugpoint. It intentionally
/// crashes if the Module has two or more compile units
class CrashOnTooManyCUs : public ModulePass {
public:
  static char ID;
  CrashOnTooManyCUs() : ModulePass(ID) {}

private:
  bool runOnModule(Module &M) override {
    NamedMDNode *CU_Nodes = M.getNamedMetadata("llvm.dbg.cu");
    if (!CU_Nodes)
      return false;
    if (CU_Nodes->getNumOperands() >= 2)
      abort();
    return false;
  }
};
}

char CrashOnTooManyCUs::ID = 0;
static RegisterPass<CrashOnTooManyCUs>
    A("bugpoint-crash-too-many-cus",
      "BugPoint Test Pass - Intentionally crash on too many CUs");

namespace {
class CrashOnFunctionAttribute : public FunctionPass {
public:
  static char ID; // Pass ID, replacement for typeid
  CrashOnFunctionAttribute() : FunctionPass(ID) {}

private:
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

  bool runOnFunction(Function &F) override {
    AttributeSet A = F.getAttributes().getFnAttrs();
    if (A.hasAttribute("bugpoint-crash"))
      abort();
    return false;
  }
};
} // namespace

char CrashOnFunctionAttribute::ID = 0;
static RegisterPass<CrashOnFunctionAttribute>
    B("bugpoint-crashfuncattr", "BugPoint Test Pass - Intentionally crash on "
                                "function attribute 'bugpoint-crash'");

namespace {
class CrashOnMetadata : public FunctionPass {
public:
  static char ID; // Pass ID, replacement for typeid
  CrashOnMetadata() : FunctionPass(ID) {}

private:
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }

  // Crash on fabs calls with fpmath metdata and an fadd as argument. This
  // ensures the fadd instruction sticks around and we can check that the
  // metadata there is dropped correctly.
  bool runOnFunction(Function &F) override {
    for (Instruction &I : instructions(F))
      if (match(&I, m_FAbs(m_FAdd(m_Value(), m_Value()))) &&
          I.hasMetadata("fpmath"))
        abort();
    return false;
  }
};
} // namespace

char CrashOnMetadata::ID = 0;
static RegisterPass<CrashOnMetadata>
    C("bugpoint-crashmetadata",
      "BugPoint Test Pass - Intentionally crash on "
      "fabs calls with fpmath metadata and an fadd as argument");
