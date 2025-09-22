//=== WebAssemblyLowerRefTypesIntPtrConv.cpp -
//                     Lower IntToPtr and PtrToInt on Reference Types   ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Lowers IntToPtr and PtrToInt instructions on reference types to
/// Trap instructions since they have been allowed to operate
/// on non-integral pointers.
///
//===----------------------------------------------------------------------===//

#include "Utils/WebAssemblyTypeUtilities.h"
#include "WebAssembly.h"
#include "WebAssemblySubtarget.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Pass.h"
#include <set>

using namespace llvm;

#define DEBUG_TYPE "wasm-lower-reftypes-intptr-conv"

namespace {
class WebAssemblyLowerRefTypesIntPtrConv final : public FunctionPass {
  StringRef getPassName() const override {
    return "WebAssembly Lower RefTypes Int-Ptr Conversions";
  }

  bool runOnFunction(Function &MF) override;

public:
  static char ID; // Pass identification
  WebAssemblyLowerRefTypesIntPtrConv() : FunctionPass(ID) {}
};
} // end anonymous namespace

char WebAssemblyLowerRefTypesIntPtrConv::ID = 0;
INITIALIZE_PASS(WebAssemblyLowerRefTypesIntPtrConv, DEBUG_TYPE,
                "WebAssembly Lower RefTypes Int-Ptr Conversions", false, false)

FunctionPass *llvm::createWebAssemblyLowerRefTypesIntPtrConv() {
  return new WebAssemblyLowerRefTypesIntPtrConv();
}

bool WebAssemblyLowerRefTypesIntPtrConv::runOnFunction(Function &F) {
  LLVM_DEBUG(dbgs() << "********** Lower RefTypes IntPtr Convs **********\n"
                       "********** Function: "
                    << F.getName() << '\n');

  // This function will check for uses of ptrtoint and inttoptr on reference
  // types and replace them with a trap instruction.
  //
  // We replace the instruction by a trap instruction
  // and its uses by null in the case of inttoptr and 0 in the
  // case of ptrtoint.
  std::set<Instruction *> worklist;

  for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
    PtrToIntInst *PTI = dyn_cast<PtrToIntInst>(&*I);
    IntToPtrInst *ITP = dyn_cast<IntToPtrInst>(&*I);
    if (!(PTI && WebAssembly::isWebAssemblyReferenceType(
                     PTI->getPointerOperand()->getType())) &&
        !(ITP && WebAssembly::isWebAssemblyReferenceType(ITP->getDestTy())))
      continue;

    UndefValue *U = UndefValue::get(I->getType());
    I->replaceAllUsesWith(U);

    Function *TrapIntrin =
        Intrinsic::getDeclaration(F.getParent(), Intrinsic::debugtrap);
    CallInst::Create(TrapIntrin, {}, "", I->getIterator());

    worklist.insert(&*I);
  }

  // erase each instruction replaced by trap
  for (Instruction *I : worklist)
    I->eraseFromParent();

  return !worklist.empty();
}
