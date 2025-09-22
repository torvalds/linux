//===- X86DiscriminateMemOps.cpp - Unique IDs for Mem Ops -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// This pass aids profile-driven cache prefetch insertion by ensuring all
/// instructions that have a memory operand are distinguishible from each other.
///
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrBuilder.h"
#include "X86InstrInfo.h"
#include "X86MachineFunctionInfo.h"
#include "X86Subtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/ProfileData/SampleProf.h"
#include "llvm/ProfileData/SampleProfReader.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/IPO/SampleProfile.h"
#include <optional>
using namespace llvm;

#define DEBUG_TYPE "x86-discriminate-memops"

static cl::opt<bool> EnableDiscriminateMemops(
    DEBUG_TYPE, cl::init(false),
    cl::desc("Generate unique debug info for each instruction with a memory "
             "operand. Should be enabled for profile-driven cache prefetching, "
             "both in the build of the binary being profiled, as well as in "
             "the build of the binary consuming the profile."),
    cl::Hidden);

static cl::opt<bool> BypassPrefetchInstructions(
    "x86-bypass-prefetch-instructions", cl::init(true),
    cl::desc("When discriminating instructions with memory operands, ignore "
             "prefetch instructions. This ensures the other memory operand "
             "instructions have the same identifiers after inserting "
             "prefetches, allowing for successive insertions."),
    cl::Hidden);

namespace {

using Location = std::pair<StringRef, unsigned>;

Location diToLocation(const DILocation *Loc) {
  return std::make_pair(Loc->getFilename(), Loc->getLine());
}

/// Ensure each instruction having a memory operand has a distinct <LineNumber,
/// Discriminator> pair.
void updateDebugInfo(MachineInstr *MI, const DILocation *Loc) {
  DebugLoc DL(Loc);
  MI->setDebugLoc(DL);
}

class X86DiscriminateMemOps : public MachineFunctionPass {
  bool runOnMachineFunction(MachineFunction &MF) override;
  StringRef getPassName() const override {
    return "X86 Discriminate Memory Operands";
  }

public:
  static char ID;

  /// Default construct and initialize the pass.
  X86DiscriminateMemOps();
};

bool IsPrefetchOpcode(unsigned Opcode) {
  return Opcode == X86::PREFETCHNTA || Opcode == X86::PREFETCHT0 ||
         Opcode == X86::PREFETCHT1 || Opcode == X86::PREFETCHT2 ||
         Opcode == X86::PREFETCHIT0 || Opcode == X86::PREFETCHIT1;
}
} // end anonymous namespace

//===----------------------------------------------------------------------===//
//            Implementation
//===----------------------------------------------------------------------===//

char X86DiscriminateMemOps::ID = 0;

/// Default construct and initialize the pass.
X86DiscriminateMemOps::X86DiscriminateMemOps() : MachineFunctionPass(ID) {}

bool X86DiscriminateMemOps::runOnMachineFunction(MachineFunction &MF) {
  if (!EnableDiscriminateMemops)
    return false;

  DISubprogram *FDI = MF.getFunction().getSubprogram();
  if (!FDI || !FDI->getUnit()->getDebugInfoForProfiling())
    return false;

  // Have a default DILocation, if we find instructions with memops that don't
  // have any debug info.
  const DILocation *ReferenceDI =
      DILocation::get(FDI->getContext(), FDI->getLine(), 0, FDI);
  assert(ReferenceDI && "ReferenceDI should not be nullptr");
  DenseMap<Location, unsigned> MemOpDiscriminators;
  MemOpDiscriminators[diToLocation(ReferenceDI)] = 0;

  // Figure out the largest discriminator issued for each Location. When we
  // issue new discriminators, we can thus avoid issuing discriminators
  // belonging to instructions that don't have memops. This isn't a requirement
  // for the goals of this pass, however, it avoids unnecessary ambiguity.
  for (auto &MBB : MF) {
    for (auto &MI : MBB) {
      const auto &DI = MI.getDebugLoc();
      if (!DI)
        continue;
      if (BypassPrefetchInstructions && IsPrefetchOpcode(MI.getDesc().Opcode))
        continue;
      Location Loc = diToLocation(DI);
      MemOpDiscriminators[Loc] =
          std::max(MemOpDiscriminators[Loc], DI->getBaseDiscriminator());
    }
  }

  // Keep track of the discriminators seen at each Location. If an instruction's
  // DebugInfo has a Location and discriminator we've already seen, replace its
  // discriminator with a new one, to guarantee uniqueness.
  DenseMap<Location, DenseSet<unsigned>> Seen;

  bool Changed = false;
  for (auto &MBB : MF) {
    for (auto &MI : MBB) {
      if (X86II::getMemoryOperandNo(MI.getDesc().TSFlags) < 0)
        continue;
      if (BypassPrefetchInstructions && IsPrefetchOpcode(MI.getDesc().Opcode))
        continue;
      const DILocation *DI = MI.getDebugLoc();
      bool HasDebug = DI;
      if (!HasDebug) {
        DI = ReferenceDI;
      }
      Location L = diToLocation(DI);
      DenseSet<unsigned> &Set = Seen[L];
      const std::pair<DenseSet<unsigned>::iterator, bool> TryInsert =
          Set.insert(DI->getBaseDiscriminator());
      if (!TryInsert.second || !HasDebug) {
        unsigned BF, DF, CI = 0;
        DILocation::decodeDiscriminator(DI->getDiscriminator(), BF, DF, CI);
        std::optional<unsigned> EncodedDiscriminator =
            DILocation::encodeDiscriminator(MemOpDiscriminators[L] + 1, DF, CI);

        if (!EncodedDiscriminator) {
          // FIXME(mtrofin): The assumption is that this scenario is infrequent/OK
          // not to support. If evidence points otherwise, we can explore synthesizeing
          // unique DIs by adding fake line numbers, or by constructing 64 bit
          // discriminators.
          LLVM_DEBUG(dbgs() << "Unable to create a unique discriminator "
                     "for instruction with memory operand in: "
                     << DI->getFilename() << " Line: " << DI->getLine()
                     << " Column: " << DI->getColumn()
                     << ". This is likely due to a large macro expansion. \n");
          continue;
        }
        // Since we were able to encode, bump the MemOpDiscriminators.
        ++MemOpDiscriminators[L];
        DI = DI->cloneWithDiscriminator(*EncodedDiscriminator);
        assert(DI && "DI should not be nullptr");
        updateDebugInfo(&MI, DI);
        Changed = true;
        std::pair<DenseSet<unsigned>::iterator, bool> MustInsert =
            Set.insert(DI->getBaseDiscriminator());
        (void)MustInsert; // Silence warning in release build.
        assert(MustInsert.second && "New discriminator shouldn't be present in set");
      }

      // Bump the reference DI to avoid cramming discriminators on line 0.
      // FIXME(mtrofin): pin ReferenceDI on blocks or first instruction with DI
      // in a block. It's more consistent than just relying on the last memop
      // instruction we happened to see.
      ReferenceDI = DI;
    }
  }
  return Changed;
}

FunctionPass *llvm::createX86DiscriminateMemOpsPass() {
  return new X86DiscriminateMemOps();
}
