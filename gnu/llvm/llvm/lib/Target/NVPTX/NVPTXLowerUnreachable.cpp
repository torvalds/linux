//===-- NVPTXLowerUnreachable.cpp - Lower unreachables to exit =====--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// PTX does not have a notion of `unreachable`, which results in emitted basic
// blocks having an edge to the next block:
//
//   block1:
//     call @does_not_return();
//     // unreachable
//   block2:
//     // ptxas will create a CFG edge from block1 to block2
//
// This may result in significant changes to the control flow graph, e.g., when
// LLVM moves unreachable blocks to the end of the function. That's a problem
// in the context of divergent control flow, as `ptxas` uses the CFG to
// determine divergent regions, and some intructions may not be executed
// divergently.
//
// For example, `bar.sync` is not allowed to be executed divergently on Pascal
// or earlier. If we start with the following:
//
//   entry:
//     // start of divergent region
//     @%p0 bra cont;
//     @%p1 bra unlikely;
//     ...
//     bra.uni cont;
//   unlikely:
//     ...
//     // unreachable
//   cont:
//     // end of divergent region
//     bar.sync 0;
//     bra.uni exit;
//   exit:
//     ret;
//
// it is transformed by the branch-folder and block-placement passes to:
//
//   entry:
//     // start of divergent region
//     @%p0 bra cont;
//     @%p1 bra unlikely;
//     ...
//     bra.uni cont;
//   cont:
//     bar.sync 0;
//     bra.uni exit;
//   unlikely:
//     ...
//     // unreachable
//   exit:
//     // end of divergent region
//     ret;
//
// After moving the `unlikely` block to the end of the function, it has an edge
// to the `exit` block, which widens the divergent region and makes the
// `bar.sync` instruction happen divergently.
//
// To work around this, we add an `exit` instruction before every `unreachable`,
// as `ptxas` understands that exit terminates the CFG. We do only do this if
// `unreachable` is not lowered to `trap`, which has the same effect (although
// with current versions of `ptxas` only because it is emited as `trap; exit;`).
//
//===----------------------------------------------------------------------===//

#include "NVPTX.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/Pass.h"

using namespace llvm;

namespace llvm {
void initializeNVPTXLowerUnreachablePass(PassRegistry &);
}

namespace {
class NVPTXLowerUnreachable : public FunctionPass {
  StringRef getPassName() const override;
  bool runOnFunction(Function &F) override;
  bool isLoweredToTrap(const UnreachableInst &I) const;

public:
  static char ID; // Pass identification, replacement for typeid
  NVPTXLowerUnreachable(bool TrapUnreachable, bool NoTrapAfterNoreturn)
      : FunctionPass(ID), TrapUnreachable(TrapUnreachable),
        NoTrapAfterNoreturn(NoTrapAfterNoreturn) {}

private:
  bool TrapUnreachable;
  bool NoTrapAfterNoreturn;
};
} // namespace

char NVPTXLowerUnreachable::ID = 1;

INITIALIZE_PASS(NVPTXLowerUnreachable, "nvptx-lower-unreachable",
                "Lower Unreachable", false, false)

StringRef NVPTXLowerUnreachable::getPassName() const {
  return "add an exit instruction before every unreachable";
}

// =============================================================================
// Returns whether a `trap` intrinsic should be emitted before I.
//
// This is a copy of the logic in SelectionDAGBuilder::visitUnreachable().
// =============================================================================
bool NVPTXLowerUnreachable::isLoweredToTrap(const UnreachableInst &I) const {
  if (!TrapUnreachable)
    return false;
  if (!NoTrapAfterNoreturn)
    return true;
  const CallInst *Call = dyn_cast_or_null<CallInst>(I.getPrevNode());
  return Call && Call->doesNotReturn();
}

// =============================================================================
// Main function for this pass.
// =============================================================================
bool NVPTXLowerUnreachable::runOnFunction(Function &F) {
  if (skipFunction(F))
    return false;
  // Early out iff isLoweredToTrap() always returns true.
  if (TrapUnreachable && !NoTrapAfterNoreturn)
    return false;

  LLVMContext &C = F.getContext();
  FunctionType *ExitFTy = FunctionType::get(Type::getVoidTy(C), false);
  InlineAsm *Exit = InlineAsm::get(ExitFTy, "exit;", "", true);

  bool Changed = false;
  for (auto &BB : F)
    for (auto &I : BB) {
      if (auto unreachableInst = dyn_cast<UnreachableInst>(&I)) {
        if (isLoweredToTrap(*unreachableInst))
          continue; // trap is emitted as `trap; exit;`.
        CallInst::Create(ExitFTy, Exit, "", unreachableInst->getIterator());
        Changed = true;
      }
    }
  return Changed;
}

FunctionPass *llvm::createNVPTXLowerUnreachablePass(bool TrapUnreachable,
                                                    bool NoTrapAfterNoreturn) {
  return new NVPTXLowerUnreachable(TrapUnreachable, NoTrapAfterNoreturn);
}
