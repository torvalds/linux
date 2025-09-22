//===- NVVMIntrRange.cpp - Set range attributes for NVVM intrinsics -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass adds appropriate range attributes for calls to NVVM
// intrinsics that return a limited range of values.
//
//===----------------------------------------------------------------------===//

#include "NVPTX.h"
#include "NVPTXUtilities.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsNVPTX.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/CommandLine.h"
#include <cstdint>

using namespace llvm;

#define DEBUG_TYPE "nvvm-intr-range"

namespace llvm { void initializeNVVMIntrRangePass(PassRegistry &); }

namespace {
class NVVMIntrRange : public FunctionPass {
public:
  static char ID;
  NVVMIntrRange() : FunctionPass(ID) {

    initializeNVVMIntrRangePass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &) override;
};
} // namespace

FunctionPass *llvm::createNVVMIntrRangePass() { return new NVVMIntrRange(); }

char NVVMIntrRange::ID = 0;
INITIALIZE_PASS(NVVMIntrRange, "nvvm-intr-range",
                "Add !range metadata to NVVM intrinsics.", false, false)

// Adds the passed-in [Low,High) range information as metadata to the
// passed-in call instruction.
static bool addRangeAttr(uint64_t Low, uint64_t High, IntrinsicInst *II) {
  if (II->getMetadata(LLVMContext::MD_range))
    return false;

  const uint64_t BitWidth = II->getType()->getIntegerBitWidth();
  ConstantRange Range(APInt(BitWidth, Low), APInt(BitWidth, High));

  if (auto CurrentRange = II->getRange())
    Range = Range.intersectWith(CurrentRange.value());

  II->addRangeRetAttr(Range);
  return true;
}

static bool runNVVMIntrRange(Function &F) {
  struct {
    unsigned x, y, z;
  } MaxBlockSize, MaxGridSize;

  const unsigned MetadataNTID = getReqNTID(F).value_or(
      getMaxNTID(F).value_or(std::numeric_limits<unsigned>::max()));

  MaxBlockSize.x = std::min(1024u, MetadataNTID);
  MaxBlockSize.y = std::min(1024u, MetadataNTID);
  MaxBlockSize.z = std::min(64u, MetadataNTID);

  MaxGridSize.x = 0x7fffffff;
  MaxGridSize.y = 0xffff;
  MaxGridSize.z = 0xffff;

  // Go through the calls in this function.
  bool Changed = false;
  for (Instruction &I : instructions(F)) {
    IntrinsicInst *II = dyn_cast<IntrinsicInst>(&I);
    if (!II)
      continue;

    switch (II->getIntrinsicID()) {
    // Index within block
    case Intrinsic::nvvm_read_ptx_sreg_tid_x:
      Changed |= addRangeAttr(0, MaxBlockSize.x, II);
      break;
    case Intrinsic::nvvm_read_ptx_sreg_tid_y:
      Changed |= addRangeAttr(0, MaxBlockSize.y, II);
      break;
    case Intrinsic::nvvm_read_ptx_sreg_tid_z:
      Changed |= addRangeAttr(0, MaxBlockSize.z, II);
      break;

    // Block size
    case Intrinsic::nvvm_read_ptx_sreg_ntid_x:
      Changed |= addRangeAttr(1, MaxBlockSize.x + 1, II);
      break;
    case Intrinsic::nvvm_read_ptx_sreg_ntid_y:
      Changed |= addRangeAttr(1, MaxBlockSize.y + 1, II);
      break;
    case Intrinsic::nvvm_read_ptx_sreg_ntid_z:
      Changed |= addRangeAttr(1, MaxBlockSize.z + 1, II);
      break;

    // Index within grid
    case Intrinsic::nvvm_read_ptx_sreg_ctaid_x:
      Changed |= addRangeAttr(0, MaxGridSize.x, II);
      break;
    case Intrinsic::nvvm_read_ptx_sreg_ctaid_y:
      Changed |= addRangeAttr(0, MaxGridSize.y, II);
      break;
    case Intrinsic::nvvm_read_ptx_sreg_ctaid_z:
      Changed |= addRangeAttr(0, MaxGridSize.z, II);
      break;

    // Grid size
    case Intrinsic::nvvm_read_ptx_sreg_nctaid_x:
      Changed |= addRangeAttr(1, MaxGridSize.x + 1, II);
      break;
    case Intrinsic::nvvm_read_ptx_sreg_nctaid_y:
      Changed |= addRangeAttr(1, MaxGridSize.y + 1, II);
      break;
    case Intrinsic::nvvm_read_ptx_sreg_nctaid_z:
      Changed |= addRangeAttr(1, MaxGridSize.z + 1, II);
      break;

    // warp size is constant 32.
    case Intrinsic::nvvm_read_ptx_sreg_warpsize:
      Changed |= addRangeAttr(32, 32 + 1, II);
      break;

    // Lane ID is [0..warpsize)
    case Intrinsic::nvvm_read_ptx_sreg_laneid:
      Changed |= addRangeAttr(0, 32, II);
      break;

    default:
      break;
    }
  }

  return Changed;
}

bool NVVMIntrRange::runOnFunction(Function &F) { return runNVVMIntrRange(F); }

PreservedAnalyses NVVMIntrRangePass::run(Function &F,
                                         FunctionAnalysisManager &AM) {
  return runNVVMIntrRange(F) ? PreservedAnalyses::none()
                             : PreservedAnalyses::all();
}
