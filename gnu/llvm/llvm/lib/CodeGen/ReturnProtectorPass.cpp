//===- ReturnProtectorPass.cpp - Set up rteurn protectors -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass sets up functions for return protectors.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/Passes.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "return-protector"

STATISTIC(NumSymbols, "Counts number of cookie symbols added");

namespace {
  struct ReturnProtector : public FunctionPass {
    static char ID;
    ReturnProtector() : FunctionPass(ID) {}

    bool runOnFunction(Function &F) override {
      if (F.hasFnAttribute("ret-protector")) {
        // Create a symbol for the cookie
        Module *M = F.getParent();
        std::hash<std::string> hasher;
        std::string hash = std::to_string(hasher((M->getName() + F.getName()).str()) % 4000);
        std::string cookiename = "__retguard_" + hash;
        Type *cookietype = PointerType::getUnqual(M->getContext());
        GlobalVariable *cookie = dyn_cast_or_null<GlobalVariable>(
            M->getOrInsertGlobal(cookiename, cookietype));
        cookie->setInitializer(Constant::getNullValue(cookietype));
        cookie->setLinkage(GlobalVariable::LinkOnceAnyLinkage);
        cookie->setVisibility(GlobalValue::HiddenVisibility);
        cookie->setComdat(M->getOrInsertComdat(cookiename));
        cookie->setSection(".openbsd.randomdata.retguard." + hash);
        cookie->setExternallyInitialized(true);
        F.addFnAttr("ret-protector-cookie", cookiename);
        NumSymbols++;
      }
      return false;
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesCFG();
    }
  };
}

char ReturnProtector::ID = 0;
INITIALIZE_PASS(ReturnProtector, "return-protector", "Return Protector Pass",
                false, false)
FunctionPass *llvm::createReturnProtectorPass() { return new ReturnProtector(); }
