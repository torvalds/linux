//===- CSKYConstantIslandPass.cpp - Emit PC Relative loads ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
// Loading constants inline is expensive on CSKY and it's in general better
// to place the constant nearby in code space and then it can be loaded with a
// simple 16/32 bit load instruction like lrw.
//
// The constants can be not just numbers but addresses of functions and labels.
// This can be particularly helpful in static relocation mode for embedded
// non-linux targets.
//
//===----------------------------------------------------------------------===//

#include "CSKY.h"
#include "CSKYConstantPoolValue.h"
#include "CSKYMachineFunctionInfo.h"
#include "CSKYSubtarget.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "CSKY-constant-islands"

STATISTIC(NumCPEs, "Number of constpool entries");
STATISTIC(NumSplit, "Number of uncond branches inserted");
STATISTIC(NumCBrFixed, "Number of cond branches fixed");
STATISTIC(NumUBrFixed, "Number of uncond branches fixed");

namespace {

using Iter = MachineBasicBlock::iterator;
using ReverseIter = MachineBasicBlock::reverse_iterator;

/// CSKYConstantIslands - Due to limited PC-relative displacements, CSKY
/// requires constant pool entries to be scattered among the instructions
/// inside a function.  To do this, it completely ignores the normal LLVM
/// constant pool; instead, it places constants wherever it feels like with
/// special instructions.
///
/// The terminology used in this pass includes:
///   Islands - Clumps of constants placed in the function.
///   Water   - Potential places where an island could be formed.
///   CPE     - A constant pool entry that has been placed somewhere, which
///             tracks a list of users.

class CSKYConstantIslands : public MachineFunctionPass {
  /// BasicBlockInfo - Information about the offset and size of a single
  /// basic block.
  struct BasicBlockInfo {
    /// Offset - Distance from the beginning of the function to the beginning
    /// of this basic block.
    ///
    /// Offsets are computed assuming worst case padding before an aligned
    /// block. This means that subtracting basic block offsets always gives a
    /// conservative estimate of the real distance which may be smaller.
    ///
    /// Because worst case padding is used, the computed offset of an aligned
    /// block may not actually be aligned.
    unsigned Offset = 0;

    /// Size - Size of the basic block in bytes.  If the block contains
    /// inline assembly, this is a worst case estimate.
    ///
    /// The size does not include any alignment padding whether from the
    /// beginning of the block, or from an aligned jump table at the end.
    unsigned Size = 0;

    BasicBlockInfo() = default;

    unsigned postOffset() const { return Offset + Size; }
  };

  std::vector<BasicBlockInfo> BBInfo;

  /// WaterList - A sorted list of basic blocks where islands could be placed
  /// (i.e. blocks that don't fall through to the following block, due
  /// to a return, unreachable, or unconditional branch).
  std::vector<MachineBasicBlock *> WaterList;

  /// NewWaterList - The subset of WaterList that was created since the
  /// previous iteration by inserting unconditional branches.
  SmallSet<MachineBasicBlock *, 4> NewWaterList;

  using water_iterator = std::vector<MachineBasicBlock *>::iterator;

  /// CPUser - One user of a constant pool, keeping the machine instruction
  /// pointer, the constant pool being referenced, and the max displacement
  /// allowed from the instruction to the CP.  The HighWaterMark records the
  /// highest basic block where a new CPEntry can be placed.  To ensure this
  /// pass terminates, the CP entries are initially placed at the end of the
  /// function and then move monotonically to lower addresses.  The
  /// exception to this rule is when the current CP entry for a particular
  /// CPUser is out of range, but there is another CP entry for the same
  /// constant value in range.  We want to use the existing in-range CP
  /// entry, but if it later moves out of range, the search for new water
  /// should resume where it left off.  The HighWaterMark is used to record
  /// that point.
  struct CPUser {
    MachineInstr *MI;
    MachineInstr *CPEMI;
    MachineBasicBlock *HighWaterMark;

  private:
    unsigned MaxDisp;

  public:
    bool NegOk;

    CPUser(MachineInstr *Mi, MachineInstr *Cpemi, unsigned Maxdisp, bool Neg)
        : MI(Mi), CPEMI(Cpemi), MaxDisp(Maxdisp), NegOk(Neg) {
      HighWaterMark = CPEMI->getParent();
    }

    /// getMaxDisp - Returns the maximum displacement supported by MI.
    unsigned getMaxDisp() const { return MaxDisp - 16; }

    void setMaxDisp(unsigned Val) { MaxDisp = Val; }
  };

  /// CPUsers - Keep track of all of the machine instructions that use various
  /// constant pools and their max displacement.
  std::vector<CPUser> CPUsers;

  /// CPEntry - One per constant pool entry, keeping the machine instruction
  /// pointer, the constpool index, and the number of CPUser's which
  /// reference this entry.
  struct CPEntry {
    MachineInstr *CPEMI;
    unsigned CPI;
    unsigned RefCount;

    CPEntry(MachineInstr *Cpemi, unsigned Cpi, unsigned Rc = 0)
        : CPEMI(Cpemi), CPI(Cpi), RefCount(Rc) {}
  };

  /// CPEntries - Keep track of all of the constant pool entry machine
  /// instructions. For each original constpool index (i.e. those that
  /// existed upon entry to this pass), it keeps a vector of entries.
  /// Original elements are cloned as we go along; the clones are
  /// put in the vector of the original element, but have distinct CPIs.
  std::vector<std::vector<CPEntry>> CPEntries;

  /// ImmBranch - One per immediate branch, keeping the machine instruction
  /// pointer, conditional or unconditional, the max displacement,
  /// and (if isCond is true) the corresponding unconditional branch
  /// opcode.
  struct ImmBranch {
    MachineInstr *MI;
    unsigned MaxDisp : 31;
    bool IsCond : 1;
    int UncondBr;

    ImmBranch(MachineInstr *Mi, unsigned Maxdisp, bool Cond, int Ubr)
        : MI(Mi), MaxDisp(Maxdisp), IsCond(Cond), UncondBr(Ubr) {}
  };

  /// ImmBranches - Keep track of all the immediate branch instructions.
  ///
  std::vector<ImmBranch> ImmBranches;

  const CSKYSubtarget *STI = nullptr;
  const CSKYInstrInfo *TII;
  CSKYMachineFunctionInfo *MFI;
  MachineFunction *MF = nullptr;
  MachineConstantPool *MCP = nullptr;

  unsigned PICLabelUId;

  void initPICLabelUId(unsigned UId) { PICLabelUId = UId; }

  unsigned createPICLabelUId() { return PICLabelUId++; }

public:
  static char ID;

  CSKYConstantIslands() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return "CSKY Constant Islands"; }

  bool runOnMachineFunction(MachineFunction &F) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineDominatorTreeWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }

  void doInitialPlacement(std::vector<MachineInstr *> &CPEMIs);
  CPEntry *findConstPoolEntry(unsigned CPI, const MachineInstr *CPEMI);
  Align getCPEAlign(const MachineInstr &CPEMI);
  void initializeFunctionInfo(const std::vector<MachineInstr *> &CPEMIs);
  unsigned getOffsetOf(MachineInstr *MI) const;
  unsigned getUserOffset(CPUser &) const;
  void dumpBBs();

  bool isOffsetInRange(unsigned UserOffset, unsigned TrialOffset, unsigned Disp,
                       bool NegativeOK);
  bool isOffsetInRange(unsigned UserOffset, unsigned TrialOffset,
                       const CPUser &U);

  void computeBlockSize(MachineBasicBlock *MBB);
  MachineBasicBlock *splitBlockBeforeInstr(MachineInstr &MI);
  void updateForInsertedWaterBlock(MachineBasicBlock *NewBB);
  void adjustBBOffsetsAfter(MachineBasicBlock *BB);
  bool decrementCPEReferenceCount(unsigned CPI, MachineInstr *CPEMI);
  int findInRangeCPEntry(CPUser &U, unsigned UserOffset);
  bool findAvailableWater(CPUser &U, unsigned UserOffset,
                          water_iterator &WaterIter);
  void createNewWater(unsigned CPUserIndex, unsigned UserOffset,
                      MachineBasicBlock *&NewMBB);
  bool handleConstantPoolUser(unsigned CPUserIndex);
  void removeDeadCPEMI(MachineInstr *CPEMI);
  bool removeUnusedCPEntries();
  bool isCPEntryInRange(MachineInstr *MI, unsigned UserOffset,
                        MachineInstr *CPEMI, unsigned Disp, bool NegOk,
                        bool DoDump = false);
  bool isWaterInRange(unsigned UserOffset, MachineBasicBlock *Water, CPUser &U,
                      unsigned &Growth);
  bool isBBInRange(MachineInstr *MI, MachineBasicBlock *BB, unsigned Disp);
  bool fixupImmediateBr(ImmBranch &Br);
  bool fixupConditionalBr(ImmBranch &Br);
  bool fixupUnconditionalBr(ImmBranch &Br);
};
} // end anonymous namespace

char CSKYConstantIslands::ID = 0;

bool CSKYConstantIslands::isOffsetInRange(unsigned UserOffset,
                                          unsigned TrialOffset,
                                          const CPUser &U) {
  return isOffsetInRange(UserOffset, TrialOffset, U.getMaxDisp(), U.NegOk);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
/// print block size and offset information - debugging
LLVM_DUMP_METHOD void CSKYConstantIslands::dumpBBs() {
  for (unsigned J = 0, E = BBInfo.size(); J != E; ++J) {
    const BasicBlockInfo &BBI = BBInfo[J];
    dbgs() << format("%08x %bb.%u\t", BBI.Offset, J)
           << format(" size=%#x\n", BBInfo[J].Size);
  }
}
#endif

bool CSKYConstantIslands::runOnMachineFunction(MachineFunction &Mf) {
  MF = &Mf;
  MCP = Mf.getConstantPool();
  STI = &Mf.getSubtarget<CSKYSubtarget>();

  LLVM_DEBUG(dbgs() << "***** CSKYConstantIslands: "
                    << MCP->getConstants().size() << " CP entries, aligned to "
                    << MCP->getConstantPoolAlign().value() << " bytes *****\n");

  TII = STI->getInstrInfo();
  MFI = MF->getInfo<CSKYMachineFunctionInfo>();

  // This pass invalidates liveness information when it splits basic blocks.
  MF->getRegInfo().invalidateLiveness();

  // Renumber all of the machine basic blocks in the function, guaranteeing that
  // the numbers agree with the position of the block in the function.
  MF->RenumberBlocks();

  bool MadeChange = false;

  // Perform the initial placement of the constant pool entries.  To start with,
  // we put them all at the end of the function.
  std::vector<MachineInstr *> CPEMIs;
  if (!MCP->isEmpty())
    doInitialPlacement(CPEMIs);

  /// The next UID to take is the first unused one.
  initPICLabelUId(CPEMIs.size());

  // Do the initial scan of the function, building up information about the
  // sizes of each block, the location of all the water, and finding all of the
  // constant pool users.
  initializeFunctionInfo(CPEMIs);
  CPEMIs.clear();
  LLVM_DEBUG(dumpBBs());

  /// Remove dead constant pool entries.
  MadeChange |= removeUnusedCPEntries();

  // Iteratively place constant pool entries and fix up branches until there
  // is no change.
  unsigned NoCPIters = 0, NoBRIters = 0;
  (void)NoBRIters;
  while (true) {
    LLVM_DEBUG(dbgs() << "Beginning CP iteration #" << NoCPIters << '\n');
    bool CPChange = false;
    for (unsigned I = 0, E = CPUsers.size(); I != E; ++I)
      CPChange |= handleConstantPoolUser(I);
    if (CPChange && ++NoCPIters > 30)
      report_fatal_error("Constant Island pass failed to converge!");
    LLVM_DEBUG(dumpBBs());

    // Clear NewWaterList now.  If we split a block for branches, it should
    // appear as "new water" for the next iteration of constant pool placement.
    NewWaterList.clear();

    LLVM_DEBUG(dbgs() << "Beginning BR iteration #" << NoBRIters << '\n');
    bool BRChange = false;
    for (unsigned I = 0, E = ImmBranches.size(); I != E; ++I)
      BRChange |= fixupImmediateBr(ImmBranches[I]);
    if (BRChange && ++NoBRIters > 30)
      report_fatal_error("Branch Fix Up pass failed to converge!");
    LLVM_DEBUG(dumpBBs());
    if (!CPChange && !BRChange)
      break;
    MadeChange = true;
  }

  LLVM_DEBUG(dbgs() << '\n'; dumpBBs());

  BBInfo.clear();
  WaterList.clear();
  CPUsers.clear();
  CPEntries.clear();
  ImmBranches.clear();
  return MadeChange;
}

/// doInitialPlacement - Perform the initial placement of the constant pool
/// entries.  To start with, we put them all at the end of the function.
void CSKYConstantIslands::doInitialPlacement(
    std::vector<MachineInstr *> &CPEMIs) {
  // Create the basic block to hold the CPE's.
  MachineBasicBlock *BB = MF->CreateMachineBasicBlock();
  MF->push_back(BB);

  // MachineConstantPool measures alignment in bytes. We measure in log2(bytes).
  const Align MaxAlign = MCP->getConstantPoolAlign();

  // Mark the basic block as required by the const-pool.
  BB->setAlignment(Align(2));

  // The function needs to be as aligned as the basic blocks. The linker may
  // move functions around based on their alignment.
  MF->ensureAlignment(BB->getAlignment());

  // Order the entries in BB by descending alignment.  That ensures correct
  // alignment of all entries as long as BB is sufficiently aligned.  Keep
  // track of the insertion point for each alignment.  We are going to bucket
  // sort the entries as they are created.
  SmallVector<MachineBasicBlock::iterator, 8> InsPoint(Log2(MaxAlign) + 1,
                                                       BB->end());

  // Add all of the constants from the constant pool to the end block, use an
  // identity mapping of CPI's to CPE's.
  const std::vector<MachineConstantPoolEntry> &CPs = MCP->getConstants();

  const DataLayout &TD = MF->getDataLayout();
  for (unsigned I = 0, E = CPs.size(); I != E; ++I) {
    unsigned Size = CPs[I].getSizeInBytes(TD);
    assert(Size >= 4 && "Too small constant pool entry");
    Align Alignment = CPs[I].getAlign();
    // Verify that all constant pool entries are a multiple of their alignment.
    // If not, we would have to pad them out so that instructions stay aligned.
    assert(isAligned(Alignment, Size) && "CP Entry not multiple of 4 bytes!");

    // Insert CONSTPOOL_ENTRY before entries with a smaller alignment.
    unsigned LogAlign = Log2(Alignment);
    MachineBasicBlock::iterator InsAt = InsPoint[LogAlign];

    MachineInstr *CPEMI =
        BuildMI(*BB, InsAt, DebugLoc(), TII->get(CSKY::CONSTPOOL_ENTRY))
            .addImm(I)
            .addConstantPoolIndex(I)
            .addImm(Size);

    CPEMIs.push_back(CPEMI);

    // Ensure that future entries with higher alignment get inserted before
    // CPEMI. This is bucket sort with iterators.
    for (unsigned A = LogAlign + 1; A <= Log2(MaxAlign); ++A)
      if (InsPoint[A] == InsAt)
        InsPoint[A] = CPEMI;
    // Add a new CPEntry, but no corresponding CPUser yet.
    CPEntries.emplace_back(1, CPEntry(CPEMI, I));
    ++NumCPEs;
    LLVM_DEBUG(dbgs() << "Moved CPI#" << I << " to end of function, size = "
                      << Size << ", align = " << Alignment.value() << '\n');
  }
  LLVM_DEBUG(BB->dump());
}

/// BBHasFallthrough - Return true if the specified basic block can fallthrough
/// into the block immediately after it.
static bool bbHasFallthrough(MachineBasicBlock *MBB) {
  // Get the next machine basic block in the function.
  MachineFunction::iterator MBBI = MBB->getIterator();
  // Can't fall off end of function.
  if (std::next(MBBI) == MBB->getParent()->end())
    return false;

  MachineBasicBlock *NextBB = &*std::next(MBBI);
  for (MachineBasicBlock::succ_iterator I = MBB->succ_begin(),
                                        E = MBB->succ_end();
       I != E; ++I)
    if (*I == NextBB)
      return true;

  return false;
}

/// findConstPoolEntry - Given the constpool index and CONSTPOOL_ENTRY MI,
/// look up the corresponding CPEntry.
CSKYConstantIslands::CPEntry *
CSKYConstantIslands::findConstPoolEntry(unsigned CPI,
                                        const MachineInstr *CPEMI) {
  std::vector<CPEntry> &CPEs = CPEntries[CPI];
  // Number of entries per constpool index should be small, just do a
  // linear search.
  for (unsigned I = 0, E = CPEs.size(); I != E; ++I) {
    if (CPEs[I].CPEMI == CPEMI)
      return &CPEs[I];
  }
  return nullptr;
}

/// getCPEAlign - Returns the required alignment of the constant pool entry
/// represented by CPEMI.  Alignment is measured in log2(bytes) units.
Align CSKYConstantIslands::getCPEAlign(const MachineInstr &CPEMI) {
  assert(CPEMI.getOpcode() == CSKY::CONSTPOOL_ENTRY);

  unsigned CPI = CPEMI.getOperand(1).getIndex();
  assert(CPI < MCP->getConstants().size() && "Invalid constant pool index.");
  return MCP->getConstants()[CPI].getAlign();
}

/// initializeFunctionInfo - Do the initial scan of the function, building up
/// information about the sizes of each block, the location of all the water,
/// and finding all of the constant pool users.
void CSKYConstantIslands::initializeFunctionInfo(
    const std::vector<MachineInstr *> &CPEMIs) {
  BBInfo.clear();
  BBInfo.resize(MF->getNumBlockIDs());

  // First thing, compute the size of all basic blocks, and see if the function
  // has any inline assembly in it. If so, we have to be conservative about
  // alignment assumptions, as we don't know for sure the size of any
  // instructions in the inline assembly.
  for (MachineFunction::iterator I = MF->begin(), E = MF->end(); I != E; ++I)
    computeBlockSize(&*I);

  // Compute block offsets.
  adjustBBOffsetsAfter(&MF->front());

  // Now go back through the instructions and build up our data structures.
  for (MachineBasicBlock &MBB : *MF) {
    // If this block doesn't fall through into the next MBB, then this is
    // 'water' that a constant pool island could be placed.
    if (!bbHasFallthrough(&MBB))
      WaterList.push_back(&MBB);
    for (MachineInstr &MI : MBB) {
      if (MI.isDebugInstr())
        continue;

      int Opc = MI.getOpcode();
      if (MI.isBranch() && !MI.isIndirectBranch()) {
        bool IsCond = MI.isConditionalBranch();
        unsigned Bits = 0;
        unsigned Scale = 1;
        int UOpc = CSKY::BR32;

        switch (MI.getOpcode()) {
        case CSKY::BR16:
        case CSKY::BF16:
        case CSKY::BT16:
          Bits = 10;
          Scale = 2;
          break;
        default:
          Bits = 16;
          Scale = 2;
          break;
        }

        // Record this immediate branch.
        unsigned MaxOffs = ((1 << (Bits - 1)) - 1) * Scale;
        ImmBranches.push_back(ImmBranch(&MI, MaxOffs, IsCond, UOpc));
      }

      if (Opc == CSKY::CONSTPOOL_ENTRY)
        continue;

      // Scan the instructions for constant pool operands.
      for (unsigned Op = 0, E = MI.getNumOperands(); Op != E; ++Op)
        if (MI.getOperand(Op).isCPI()) {
          // We found one.  The addressing mode tells us the max displacement
          // from the PC that this instruction permits.

          // Basic size info comes from the TSFlags field.
          unsigned Bits = 0;
          unsigned Scale = 1;
          bool NegOk = false;

          switch (Opc) {
          default:
            llvm_unreachable("Unknown addressing mode for CP reference!");
          case CSKY::MOVIH32:
          case CSKY::ORI32:
            continue;
          case CSKY::PseudoTLSLA32:
          case CSKY::JSRI32:
          case CSKY::JMPI32:
          case CSKY::LRW32:
          case CSKY::LRW32_Gen:
            Bits = 16;
            Scale = 4;
            break;
          case CSKY::f2FLRW_S:
          case CSKY::f2FLRW_D:
            Bits = 8;
            Scale = 4;
            break;
          case CSKY::GRS32:
            Bits = 17;
            Scale = 2;
            NegOk = true;
            break;
          }
          // Remember that this is a user of a CP entry.
          unsigned CPI = MI.getOperand(Op).getIndex();
          MachineInstr *CPEMI = CPEMIs[CPI];
          unsigned MaxOffs = ((1 << Bits) - 1) * Scale;
          CPUsers.push_back(CPUser(&MI, CPEMI, MaxOffs, NegOk));

          // Increment corresponding CPEntry reference count.
          CPEntry *CPE = findConstPoolEntry(CPI, CPEMI);
          assert(CPE && "Cannot find a corresponding CPEntry!");
          CPE->RefCount++;
        }
    }
  }
}

/// computeBlockSize - Compute the size and some alignment information for MBB.
/// This function updates BBInfo directly.
void CSKYConstantIslands::computeBlockSize(MachineBasicBlock *MBB) {
  BasicBlockInfo &BBI = BBInfo[MBB->getNumber()];
  BBI.Size = 0;

  for (const MachineInstr &MI : *MBB)
    BBI.Size += TII->getInstSizeInBytes(MI);
}

/// getOffsetOf - Return the current offset of the specified machine instruction
/// from the start of the function.  This offset changes as stuff is moved
/// around inside the function.
unsigned CSKYConstantIslands::getOffsetOf(MachineInstr *MI) const {
  MachineBasicBlock *MBB = MI->getParent();

  // The offset is composed of two things: the sum of the sizes of all MBB's
  // before this instruction's block, and the offset from the start of the block
  // it is in.
  unsigned Offset = BBInfo[MBB->getNumber()].Offset;

  // Sum instructions before MI in MBB.
  for (MachineBasicBlock::iterator I = MBB->begin(); &*I != MI; ++I) {
    assert(I != MBB->end() && "Didn't find MI in its own basic block?");
    Offset += TII->getInstSizeInBytes(*I);
  }
  return Offset;
}

/// CompareMBBNumbers - Little predicate function to sort the WaterList by MBB
/// ID.
static bool compareMbbNumbers(const MachineBasicBlock *LHS,
                              const MachineBasicBlock *RHS) {
  return LHS->getNumber() < RHS->getNumber();
}

/// updateForInsertedWaterBlock - When a block is newly inserted into the
/// machine function, it upsets all of the block numbers.  Renumber the blocks
/// and update the arrays that parallel this numbering.
void CSKYConstantIslands::updateForInsertedWaterBlock(
    MachineBasicBlock *NewBB) {
  // Renumber the MBB's to keep them consecutive.
  NewBB->getParent()->RenumberBlocks(NewBB);

  // Insert an entry into BBInfo to align it properly with the (newly
  // renumbered) block numbers.
  BBInfo.insert(BBInfo.begin() + NewBB->getNumber(), BasicBlockInfo());

  // Next, update WaterList.  Specifically, we need to add NewMBB as having
  // available water after it.
  water_iterator IP = llvm::lower_bound(WaterList, NewBB, compareMbbNumbers);
  WaterList.insert(IP, NewBB);
}

unsigned CSKYConstantIslands::getUserOffset(CPUser &U) const {
  unsigned UserOffset = getOffsetOf(U.MI);

  UserOffset &= ~3u;

  return UserOffset;
}

/// Split the basic block containing MI into two blocks, which are joined by
/// an unconditional branch.  Update data structures and renumber blocks to
/// account for this change and returns the newly created block.
MachineBasicBlock *
CSKYConstantIslands::splitBlockBeforeInstr(MachineInstr &MI) {
  MachineBasicBlock *OrigBB = MI.getParent();

  // Create a new MBB for the code after the OrigBB.
  MachineBasicBlock *NewBB =
      MF->CreateMachineBasicBlock(OrigBB->getBasicBlock());
  MachineFunction::iterator MBBI = ++OrigBB->getIterator();
  MF->insert(MBBI, NewBB);

  // Splice the instructions starting with MI over to NewBB.
  NewBB->splice(NewBB->end(), OrigBB, MI, OrigBB->end());

  // Add an unconditional branch from OrigBB to NewBB.
  // Note the new unconditional branch is not being recorded.
  // There doesn't seem to be meaningful DebugInfo available; this doesn't
  // correspond to anything in the source.

  // TODO: Add support for 16bit instr.
  BuildMI(OrigBB, DebugLoc(), TII->get(CSKY::BR32)).addMBB(NewBB);
  ++NumSplit;

  // Update the CFG.  All succs of OrigBB are now succs of NewBB.
  NewBB->transferSuccessors(OrigBB);

  // OrigBB branches to NewBB.
  OrigBB->addSuccessor(NewBB);

  // Update internal data structures to account for the newly inserted MBB.
  // This is almost the same as updateForInsertedWaterBlock, except that
  // the Water goes after OrigBB, not NewBB.
  MF->RenumberBlocks(NewBB);

  // Insert an entry into BBInfo to align it properly with the (newly
  // renumbered) block numbers.
  BBInfo.insert(BBInfo.begin() + NewBB->getNumber(), BasicBlockInfo());

  // Next, update WaterList.  Specifically, we need to add OrigMBB as having
  // available water after it (but not if it's already there, which happens
  // when splitting before a conditional branch that is followed by an
  // unconditional branch - in that case we want to insert NewBB).
  water_iterator IP = llvm::lower_bound(WaterList, OrigBB, compareMbbNumbers);
  MachineBasicBlock *WaterBB = *IP;
  if (WaterBB == OrigBB)
    WaterList.insert(std::next(IP), NewBB);
  else
    WaterList.insert(IP, OrigBB);
  NewWaterList.insert(OrigBB);

  // Figure out how large the OrigBB is.  As the first half of the original
  // block, it cannot contain a tablejump.  The size includes
  // the new jump we added.  (It should be possible to do this without
  // recounting everything, but it's very confusing, and this is rarely
  // executed.)
  computeBlockSize(OrigBB);

  // Figure out how large the NewMBB is.  As the second half of the original
  // block, it may contain a tablejump.
  computeBlockSize(NewBB);

  // All BBOffsets following these blocks must be modified.
  adjustBBOffsetsAfter(OrigBB);

  return NewBB;
}

/// isOffsetInRange - Checks whether UserOffset (the location of a constant pool
/// reference) is within MaxDisp of TrialOffset (a proposed location of a
/// constant pool entry).
bool CSKYConstantIslands::isOffsetInRange(unsigned UserOffset,
                                          unsigned TrialOffset,
                                          unsigned MaxDisp, bool NegativeOK) {
  if (UserOffset <= TrialOffset) {
    // User before the Trial.
    if (TrialOffset - UserOffset <= MaxDisp)
      return true;
  } else if (NegativeOK) {
    if (UserOffset - TrialOffset <= MaxDisp)
      return true;
  }
  return false;
}

/// isWaterInRange - Returns true if a CPE placed after the specified
/// Water (a basic block) will be in range for the specific MI.
///
/// Compute how much the function will grow by inserting a CPE after Water.
bool CSKYConstantIslands::isWaterInRange(unsigned UserOffset,
                                         MachineBasicBlock *Water, CPUser &U,
                                         unsigned &Growth) {
  unsigned CPEOffset = BBInfo[Water->getNumber()].postOffset();
  unsigned NextBlockOffset;
  Align NextBlockAlignment;
  MachineFunction::const_iterator NextBlock = ++Water->getIterator();
  if (NextBlock == MF->end()) {
    NextBlockOffset = BBInfo[Water->getNumber()].postOffset();
    NextBlockAlignment = Align(4);
  } else {
    NextBlockOffset = BBInfo[NextBlock->getNumber()].Offset;
    NextBlockAlignment = NextBlock->getAlignment();
  }
  unsigned Size = U.CPEMI->getOperand(2).getImm();
  unsigned CPEEnd = CPEOffset + Size;

  // The CPE may be able to hide in the alignment padding before the next
  // block. It may also cause more padding to be required if it is more aligned
  // that the next block.
  if (CPEEnd > NextBlockOffset) {
    Growth = CPEEnd - NextBlockOffset;
    // Compute the padding that would go at the end of the CPE to align the next
    // block.
    Growth += offsetToAlignment(CPEEnd, NextBlockAlignment);

    // If the CPE is to be inserted before the instruction, that will raise
    // the offset of the instruction. Also account for unknown alignment padding
    // in blocks between CPE and the user.
    if (CPEOffset < UserOffset)
      UserOffset += Growth;
  } else
    // CPE fits in existing padding.
    Growth = 0;

  return isOffsetInRange(UserOffset, CPEOffset, U);
}

/// isCPEntryInRange - Returns true if the distance between specific MI and
/// specific ConstPool entry instruction can fit in MI's displacement field.
bool CSKYConstantIslands::isCPEntryInRange(MachineInstr *MI,
                                           unsigned UserOffset,
                                           MachineInstr *CPEMI,
                                           unsigned MaxDisp, bool NegOk,
                                           bool DoDump) {
  unsigned CPEOffset = getOffsetOf(CPEMI);

  if (DoDump) {
    LLVM_DEBUG({
      unsigned Block = MI->getParent()->getNumber();
      const BasicBlockInfo &BBI = BBInfo[Block];
      dbgs() << "User of CPE#" << CPEMI->getOperand(0).getImm()
             << " max delta=" << MaxDisp
             << format(" insn address=%#x", UserOffset) << " in "
             << printMBBReference(*MI->getParent()) << ": "
             << format("%#x-%x\t", BBI.Offset, BBI.postOffset()) << *MI
             << format("CPE address=%#x offset=%+d: ", CPEOffset,
                       int(CPEOffset - UserOffset));
    });
  }

  return isOffsetInRange(UserOffset, CPEOffset, MaxDisp, NegOk);
}

#ifndef NDEBUG
/// BBIsJumpedOver - Return true of the specified basic block's only predecessor
/// unconditionally branches to its only successor.
static bool bbIsJumpedOver(MachineBasicBlock *MBB) {
  if (MBB->pred_size() != 1 || MBB->succ_size() != 1)
    return false;
  MachineBasicBlock *Succ = *MBB->succ_begin();
  MachineBasicBlock *Pred = *MBB->pred_begin();
  MachineInstr *PredMI = &Pred->back();
  if (PredMI->getOpcode() == CSKY::BR32 /*TODO: change to 16bit instr. */)
    return PredMI->getOperand(0).getMBB() == Succ;
  return false;
}
#endif

void CSKYConstantIslands::adjustBBOffsetsAfter(MachineBasicBlock *BB) {
  unsigned BBNum = BB->getNumber();
  for (unsigned I = BBNum + 1, E = MF->getNumBlockIDs(); I < E; ++I) {
    // Get the offset and known bits at the end of the layout predecessor.
    // Include the alignment of the current block.
    unsigned Offset = BBInfo[I - 1].Offset + BBInfo[I - 1].Size;
    BBInfo[I].Offset = Offset;
  }
}

/// decrementCPEReferenceCount - find the constant pool entry with index CPI
/// and instruction CPEMI, and decrement its refcount.  If the refcount
/// becomes 0 remove the entry and instruction.  Returns true if we removed
/// the entry, false if we didn't.
bool CSKYConstantIslands::decrementCPEReferenceCount(unsigned CPI,
                                                     MachineInstr *CPEMI) {
  // Find the old entry. Eliminate it if it is no longer used.
  CPEntry *CPE = findConstPoolEntry(CPI, CPEMI);
  assert(CPE && "Unexpected!");
  if (--CPE->RefCount == 0) {
    removeDeadCPEMI(CPEMI);
    CPE->CPEMI = nullptr;
    --NumCPEs;
    return true;
  }
  return false;
}

/// LookForCPEntryInRange - see if the currently referenced CPE is in range;
/// if not, see if an in-range clone of the CPE is in range, and if so,
/// change the data structures so the user references the clone.  Returns:
/// 0 = no existing entry found
/// 1 = entry found, and there were no code insertions or deletions
/// 2 = entry found, and there were code insertions or deletions
int CSKYConstantIslands::findInRangeCPEntry(CPUser &U, unsigned UserOffset) {
  MachineInstr *UserMI = U.MI;
  MachineInstr *CPEMI = U.CPEMI;

  // Check to see if the CPE is already in-range.
  if (isCPEntryInRange(UserMI, UserOffset, CPEMI, U.getMaxDisp(), U.NegOk,
                       true)) {
    LLVM_DEBUG(dbgs() << "In range\n");
    return 1;
  }

  // No.  Look for previously created clones of the CPE that are in range.
  unsigned CPI = CPEMI->getOperand(1).getIndex();
  std::vector<CPEntry> &CPEs = CPEntries[CPI];
  for (unsigned I = 0, E = CPEs.size(); I != E; ++I) {
    // We already tried this one
    if (CPEs[I].CPEMI == CPEMI)
      continue;
    // Removing CPEs can leave empty entries, skip
    if (CPEs[I].CPEMI == nullptr)
      continue;
    if (isCPEntryInRange(UserMI, UserOffset, CPEs[I].CPEMI, U.getMaxDisp(),
                         U.NegOk)) {
      LLVM_DEBUG(dbgs() << "Replacing CPE#" << CPI << " with CPE#"
                        << CPEs[I].CPI << "\n");
      // Point the CPUser node to the replacement
      U.CPEMI = CPEs[I].CPEMI;
      // Change the CPI in the instruction operand to refer to the clone.
      for (unsigned J = 0, E = UserMI->getNumOperands(); J != E; ++J)
        if (UserMI->getOperand(J).isCPI()) {
          UserMI->getOperand(J).setIndex(CPEs[I].CPI);
          break;
        }
      // Adjust the refcount of the clone...
      CPEs[I].RefCount++;
      // ...and the original.  If we didn't remove the old entry, none of the
      // addresses changed, so we don't need another pass.
      return decrementCPEReferenceCount(CPI, CPEMI) ? 2 : 1;
    }
  }
  return 0;
}

/// getUnconditionalBrDisp - Returns the maximum displacement that can fit in
/// the specific unconditional branch instruction.
static inline unsigned getUnconditionalBrDisp(int Opc) {
  unsigned Bits, Scale;

  switch (Opc) {
  case CSKY::BR16:
    Bits = 10;
    Scale = 2;
    break;
  case CSKY::BR32:
    Bits = 16;
    Scale = 2;
    break;
  default:
    llvm_unreachable("");
  }

  unsigned MaxOffs = ((1 << (Bits - 1)) - 1) * Scale;
  return MaxOffs;
}

/// findAvailableWater - Look for an existing entry in the WaterList in which
/// we can place the CPE referenced from U so it's within range of U's MI.
/// Returns true if found, false if not.  If it returns true, WaterIter
/// is set to the WaterList entry.
/// To ensure that this pass
/// terminates, the CPE location for a particular CPUser is only allowed to
/// move to a lower address, so search backward from the end of the list and
/// prefer the first water that is in range.
bool CSKYConstantIslands::findAvailableWater(CPUser &U, unsigned UserOffset,
                                             water_iterator &WaterIter) {
  if (WaterList.empty())
    return false;

  unsigned BestGrowth = ~0u;
  for (water_iterator IP = std::prev(WaterList.end()), B = WaterList.begin();;
       --IP) {
    MachineBasicBlock *WaterBB = *IP;
    // Check if water is in range and is either at a lower address than the
    // current "high water mark" or a new water block that was created since
    // the previous iteration by inserting an unconditional branch.  In the
    // latter case, we want to allow resetting the high water mark back to
    // this new water since we haven't seen it before.  Inserting branches
    // should be relatively uncommon and when it does happen, we want to be
    // sure to take advantage of it for all the CPEs near that block, so that
    // we don't insert more branches than necessary.
    unsigned Growth;
    if (isWaterInRange(UserOffset, WaterBB, U, Growth) &&
        (WaterBB->getNumber() < U.HighWaterMark->getNumber() ||
         NewWaterList.count(WaterBB)) &&
        Growth < BestGrowth) {
      // This is the least amount of required padding seen so far.
      BestGrowth = Growth;
      WaterIter = IP;
      LLVM_DEBUG(dbgs() << "Found water after " << printMBBReference(*WaterBB)
                        << " Growth=" << Growth << '\n');

      // Keep looking unless it is perfect.
      if (BestGrowth == 0)
        return true;
    }
    if (IP == B)
      break;
  }
  return BestGrowth != ~0u;
}

/// createNewWater - No existing WaterList entry will work for
/// CPUsers[CPUserIndex], so create a place to put the CPE.  The end of the
/// block is used if in range, and the conditional branch munged so control
/// flow is correct.  Otherwise the block is split to create a hole with an
/// unconditional branch around it.  In either case NewMBB is set to a
/// block following which the new island can be inserted (the WaterList
/// is not adjusted).
void CSKYConstantIslands::createNewWater(unsigned CPUserIndex,
                                         unsigned UserOffset,
                                         MachineBasicBlock *&NewMBB) {
  CPUser &U = CPUsers[CPUserIndex];
  MachineInstr *UserMI = U.MI;
  MachineInstr *CPEMI = U.CPEMI;
  MachineBasicBlock *UserMBB = UserMI->getParent();
  const BasicBlockInfo &UserBBI = BBInfo[UserMBB->getNumber()];

  // If the block does not end in an unconditional branch already, and if the
  // end of the block is within range, make new water there.
  if (bbHasFallthrough(UserMBB)) {
    // Size of branch to insert.
    unsigned Delta = 4;
    // Compute the offset where the CPE will begin.
    unsigned CPEOffset = UserBBI.postOffset() + Delta;

    if (isOffsetInRange(UserOffset, CPEOffset, U)) {
      LLVM_DEBUG(dbgs() << "Split at end of " << printMBBReference(*UserMBB)
                        << format(", expected CPE offset %#x\n", CPEOffset));
      NewMBB = &*++UserMBB->getIterator();
      // Add an unconditional branch from UserMBB to fallthrough block.  Record
      // it for branch lengthening; this new branch will not get out of range,
      // but if the preceding conditional branch is out of range, the targets
      // will be exchanged, and the altered branch may be out of range, so the
      // machinery has to know about it.

      // TODO: Add support for 16bit instr.
      int UncondBr = CSKY::BR32;
      auto *NewMI = BuildMI(UserMBB, DebugLoc(), TII->get(UncondBr))
                        .addMBB(NewMBB)
                        .getInstr();
      unsigned MaxDisp = getUnconditionalBrDisp(UncondBr);
      ImmBranches.push_back(
          ImmBranch(&UserMBB->back(), MaxDisp, false, UncondBr));
      BBInfo[UserMBB->getNumber()].Size += TII->getInstSizeInBytes(*NewMI);
      adjustBBOffsetsAfter(UserMBB);
      return;
    }
  }

  // What a big block.  Find a place within the block to split it.

  // Try to split the block so it's fully aligned.  Compute the latest split
  // point where we can add a 4-byte branch instruction, and then align to
  // Align which is the largest possible alignment in the function.
  const Align Align = MF->getAlignment();
  unsigned BaseInsertOffset = UserOffset + U.getMaxDisp();
  LLVM_DEBUG(dbgs() << format("Split in middle of big block before %#x",
                              BaseInsertOffset));

  // The 4 in the following is for the unconditional branch we'll be inserting
  // Alignment of the island is handled
  // inside isOffsetInRange.
  BaseInsertOffset -= 4;

  LLVM_DEBUG(dbgs() << format(", adjusted to %#x", BaseInsertOffset)
                    << " la=" << Log2(Align) << '\n');

  // This could point off the end of the block if we've already got constant
  // pool entries following this block; only the last one is in the water list.
  // Back past any possible branches (allow for a conditional and a maximally
  // long unconditional).
  if (BaseInsertOffset + 8 >= UserBBI.postOffset()) {
    BaseInsertOffset = UserBBI.postOffset() - 8;
    LLVM_DEBUG(dbgs() << format("Move inside block: %#x\n", BaseInsertOffset));
  }
  unsigned EndInsertOffset =
      BaseInsertOffset + 4 + CPEMI->getOperand(2).getImm();
  MachineBasicBlock::iterator MI = UserMI;
  ++MI;
  unsigned CPUIndex = CPUserIndex + 1;
  unsigned NumCPUsers = CPUsers.size();
  for (unsigned Offset = UserOffset + TII->getInstSizeInBytes(*UserMI);
       Offset < BaseInsertOffset;
       Offset += TII->getInstSizeInBytes(*MI), MI = std::next(MI)) {
    assert(MI != UserMBB->end() && "Fell off end of block");
    if (CPUIndex < NumCPUsers && CPUsers[CPUIndex].MI == MI) {
      CPUser &U = CPUsers[CPUIndex];
      if (!isOffsetInRange(Offset, EndInsertOffset, U)) {
        // Shift intertion point by one unit of alignment so it is within reach.
        BaseInsertOffset -= Align.value();
        EndInsertOffset -= Align.value();
      }
      // This is overly conservative, as we don't account for CPEMIs being
      // reused within the block, but it doesn't matter much.  Also assume CPEs
      // are added in order with alignment padding.  We may eventually be able
      // to pack the aligned CPEs better.
      EndInsertOffset += U.CPEMI->getOperand(2).getImm();
      CPUIndex++;
    }
  }

  NewMBB = splitBlockBeforeInstr(*--MI);
}

/// handleConstantPoolUser - Analyze the specified user, checking to see if it
/// is out-of-range.  If so, pick up the constant pool value and move it some
/// place in-range.  Return true if we changed any addresses (thus must run
/// another pass of branch lengthening), false otherwise.
bool CSKYConstantIslands::handleConstantPoolUser(unsigned CPUserIndex) {
  CPUser &U = CPUsers[CPUserIndex];
  MachineInstr *UserMI = U.MI;
  MachineInstr *CPEMI = U.CPEMI;
  unsigned CPI = CPEMI->getOperand(1).getIndex();
  unsigned Size = CPEMI->getOperand(2).getImm();
  // Compute this only once, it's expensive.
  unsigned UserOffset = getUserOffset(U);

  // See if the current entry is within range, or there is a clone of it
  // in range.
  int result = findInRangeCPEntry(U, UserOffset);
  if (result == 1)
    return false;
  if (result == 2)
    return true;

  // Look for water where we can place this CPE.
  MachineBasicBlock *NewIsland = MF->CreateMachineBasicBlock();
  MachineBasicBlock *NewMBB;
  water_iterator IP;
  if (findAvailableWater(U, UserOffset, IP)) {
    LLVM_DEBUG(dbgs() << "Found water in range\n");
    MachineBasicBlock *WaterBB = *IP;

    // If the original WaterList entry was "new water" on this iteration,
    // propagate that to the new island.  This is just keeping NewWaterList
    // updated to match the WaterList, which will be updated below.
    if (NewWaterList.erase(WaterBB))
      NewWaterList.insert(NewIsland);

    // The new CPE goes before the following block (NewMBB).
    NewMBB = &*++WaterBB->getIterator();
  } else {
    LLVM_DEBUG(dbgs() << "No water found\n");
    createNewWater(CPUserIndex, UserOffset, NewMBB);

    // splitBlockBeforeInstr adds to WaterList, which is important when it is
    // called while handling branches so that the water will be seen on the
    // next iteration for constant pools, but in this context, we don't want
    // it.  Check for this so it will be removed from the WaterList.
    // Also remove any entry from NewWaterList.
    MachineBasicBlock *WaterBB = &*--NewMBB->getIterator();
    IP = llvm::find(WaterList, WaterBB);
    if (IP != WaterList.end())
      NewWaterList.erase(WaterBB);

    // We are adding new water.  Update NewWaterList.
    NewWaterList.insert(NewIsland);
  }

  // Remove the original WaterList entry; we want subsequent insertions in
  // this vicinity to go after the one we're about to insert.  This
  // considerably reduces the number of times we have to move the same CPE
  // more than once and is also important to ensure the algorithm terminates.
  if (IP != WaterList.end())
    WaterList.erase(IP);

  // Okay, we know we can put an island before NewMBB now, do it!
  MF->insert(NewMBB->getIterator(), NewIsland);

  // Update internal data structures to account for the newly inserted MBB.
  updateForInsertedWaterBlock(NewIsland);

  // Decrement the old entry, and remove it if refcount becomes 0.
  decrementCPEReferenceCount(CPI, CPEMI);

  // No existing clone of this CPE is within range.
  // We will be generating a new clone.  Get a UID for it.
  unsigned ID = createPICLabelUId();

  // Now that we have an island to add the CPE to, clone the original CPE and
  // add it to the island.
  U.HighWaterMark = NewIsland;
  U.CPEMI = BuildMI(NewIsland, DebugLoc(), TII->get(CSKY::CONSTPOOL_ENTRY))
                .addImm(ID)
                .addConstantPoolIndex(CPI)
                .addImm(Size);
  CPEntries[CPI].push_back(CPEntry(U.CPEMI, ID, 1));
  ++NumCPEs;

  // Mark the basic block as aligned as required by the const-pool entry.
  NewIsland->setAlignment(getCPEAlign(*U.CPEMI));

  // Increase the size of the island block to account for the new entry.
  BBInfo[NewIsland->getNumber()].Size += Size;
  adjustBBOffsetsAfter(&*--NewIsland->getIterator());

  // Finally, change the CPI in the instruction operand to be ID.
  for (unsigned I = 0, E = UserMI->getNumOperands(); I != E; ++I)
    if (UserMI->getOperand(I).isCPI()) {
      UserMI->getOperand(I).setIndex(ID);
      break;
    }

  LLVM_DEBUG(
      dbgs() << "  Moved CPE to #" << ID << " CPI=" << CPI
             << format(" offset=%#x\n", BBInfo[NewIsland->getNumber()].Offset));

  return true;
}

/// removeDeadCPEMI - Remove a dead constant pool entry instruction. Update
/// sizes and offsets of impacted basic blocks.
void CSKYConstantIslands::removeDeadCPEMI(MachineInstr *CPEMI) {
  MachineBasicBlock *CPEBB = CPEMI->getParent();
  unsigned Size = CPEMI->getOperand(2).getImm();
  CPEMI->eraseFromParent();
  BBInfo[CPEBB->getNumber()].Size -= Size;
  // All succeeding offsets have the current size value added in, fix this.
  if (CPEBB->empty()) {
    BBInfo[CPEBB->getNumber()].Size = 0;

    // This block no longer needs to be aligned.
    CPEBB->setAlignment(Align(4));
  } else {
    // Entries are sorted by descending alignment, so realign from the front.
    CPEBB->setAlignment(getCPEAlign(*CPEBB->begin()));
  }

  adjustBBOffsetsAfter(CPEBB);
  // An island has only one predecessor BB and one successor BB. Check if
  // this BB's predecessor jumps directly to this BB's successor. This
  // shouldn't happen currently.
  assert(!bbIsJumpedOver(CPEBB) && "How did this happen?");
  // FIXME: remove the empty blocks after all the work is done?
}

/// removeUnusedCPEntries - Remove constant pool entries whose refcounts
/// are zero.
bool CSKYConstantIslands::removeUnusedCPEntries() {
  unsigned MadeChange = false;
  for (unsigned I = 0, E = CPEntries.size(); I != E; ++I) {
    std::vector<CPEntry> &CPEs = CPEntries[I];
    for (unsigned J = 0, Ee = CPEs.size(); J != Ee; ++J) {
      if (CPEs[J].RefCount == 0 && CPEs[J].CPEMI) {
        removeDeadCPEMI(CPEs[J].CPEMI);
        CPEs[J].CPEMI = nullptr;
        MadeChange = true;
      }
    }
  }
  return MadeChange;
}

/// isBBInRange - Returns true if the distance between specific MI and
/// specific BB can fit in MI's displacement field.
bool CSKYConstantIslands::isBBInRange(MachineInstr *MI,
                                      MachineBasicBlock *DestBB,
                                      unsigned MaxDisp) {
  unsigned BrOffset = getOffsetOf(MI);
  unsigned DestOffset = BBInfo[DestBB->getNumber()].Offset;

  LLVM_DEBUG(dbgs() << "Branch of destination " << printMBBReference(*DestBB)
                    << " from " << printMBBReference(*MI->getParent())
                    << " max delta=" << MaxDisp << " from " << getOffsetOf(MI)
                    << " to " << DestOffset << " offset "
                    << int(DestOffset - BrOffset) << "\t" << *MI);

  if (BrOffset <= DestOffset) {
    // Branch before the Dest.
    if (DestOffset - BrOffset <= MaxDisp)
      return true;
  } else {
    if (BrOffset - DestOffset <= MaxDisp)
      return true;
  }
  return false;
}

/// fixupImmediateBr - Fix up an immediate branch whose destination is too far
/// away to fit in its displacement field.
bool CSKYConstantIslands::fixupImmediateBr(ImmBranch &Br) {
  MachineInstr *MI = Br.MI;
  MachineBasicBlock *DestBB = TII->getBranchDestBlock(*MI);

  // Check to see if the DestBB is already in-range.
  if (isBBInRange(MI, DestBB, Br.MaxDisp))
    return false;

  if (!Br.IsCond)
    return fixupUnconditionalBr(Br);
  return fixupConditionalBr(Br);
}

/// fixupUnconditionalBr - Fix up an unconditional branch whose destination is
/// too far away to fit in its displacement field. If the LR register has been
/// spilled in the epilogue, then we can use BSR to implement a far jump.
/// Otherwise, add an intermediate branch instruction to a branch.
bool CSKYConstantIslands::fixupUnconditionalBr(ImmBranch &Br) {
  MachineInstr *MI = Br.MI;
  MachineBasicBlock *MBB = MI->getParent();

  if (!MFI->isLRSpilled())
    report_fatal_error("underestimated function size");

  // Use BSR to implement far jump.
  Br.MaxDisp = ((1 << (26 - 1)) - 1) * 2;
  MI->setDesc(TII->get(CSKY::BSR32_BR));
  BBInfo[MBB->getNumber()].Size += 4;
  adjustBBOffsetsAfter(MBB);
  ++NumUBrFixed;

  LLVM_DEBUG(dbgs() << "  Changed B to long jump " << *MI);

  return true;
}

/// fixupConditionalBr - Fix up a conditional branch whose destination is too
/// far away to fit in its displacement field. It is converted to an inverse
/// conditional branch + an unconditional branch to the destination.
bool CSKYConstantIslands::fixupConditionalBr(ImmBranch &Br) {
  MachineInstr *MI = Br.MI;
  MachineBasicBlock *DestBB = TII->getBranchDestBlock(*MI);

  SmallVector<MachineOperand, 4> Cond;
  Cond.push_back(MachineOperand::CreateImm(MI->getOpcode()));
  Cond.push_back(MI->getOperand(0));
  TII->reverseBranchCondition(Cond);

  // Add an unconditional branch to the destination and invert the branch
  // condition to jump over it:
  // bteqz L1
  // =>
  // bnez L2
  // b   L1
  // L2:

  // If the branch is at the end of its MBB and that has a fall-through block,
  // direct the updated conditional branch to the fall-through block. Otherwise,
  // split the MBB before the next instruction.
  MachineBasicBlock *MBB = MI->getParent();
  MachineInstr *BMI = &MBB->back();
  bool NeedSplit = (BMI != MI) || !bbHasFallthrough(MBB);

  ++NumCBrFixed;
  if (BMI != MI) {
    if (std::next(MachineBasicBlock::iterator(MI)) == std::prev(MBB->end()) &&
        BMI->isUnconditionalBranch()) {
      // Last MI in the BB is an unconditional branch. Can we simply invert the
      // condition and swap destinations:
      // beqz L1
      // b   L2
      // =>
      // bnez L2
      // b   L1
      MachineBasicBlock *NewDest = TII->getBranchDestBlock(*BMI);
      if (isBBInRange(MI, NewDest, Br.MaxDisp)) {
        LLVM_DEBUG(
            dbgs() << "  Invert Bcc condition and swap its destination with "
                   << *BMI);
        BMI->getOperand(BMI->getNumExplicitOperands() - 1).setMBB(DestBB);
        MI->getOperand(MI->getNumExplicitOperands() - 1).setMBB(NewDest);

        MI->setDesc(TII->get(Cond[0].getImm()));
        return true;
      }
    }
  }

  if (NeedSplit) {
    splitBlockBeforeInstr(*MI);
    // No need for the branch to the next block. We're adding an unconditional
    // branch to the destination.
    int Delta = TII->getInstSizeInBytes(MBB->back());
    BBInfo[MBB->getNumber()].Size -= Delta;
    MBB->back().eraseFromParent();
    // BBInfo[SplitBB].Offset is wrong temporarily, fixed below

    // The conditional successor will be swapped between the BBs after this, so
    // update CFG.
    MBB->addSuccessor(DestBB);
    std::next(MBB->getIterator())->removeSuccessor(DestBB);
  }
  MachineBasicBlock *NextBB = &*++MBB->getIterator();

  LLVM_DEBUG(dbgs() << "  Insert B to " << printMBBReference(*DestBB)
                    << " also invert condition and change dest. to "
                    << printMBBReference(*NextBB) << "\n");

  // Insert a new conditional branch and a new unconditional branch.
  // Also update the ImmBranch as well as adding a new entry for the new branch.

  BuildMI(MBB, DebugLoc(), TII->get(Cond[0].getImm()))
      .addReg(MI->getOperand(0).getReg())
      .addMBB(NextBB);

  Br.MI = &MBB->back();
  BBInfo[MBB->getNumber()].Size += TII->getInstSizeInBytes(MBB->back());
  BuildMI(MBB, DebugLoc(), TII->get(Br.UncondBr)).addMBB(DestBB);
  BBInfo[MBB->getNumber()].Size += TII->getInstSizeInBytes(MBB->back());
  unsigned MaxDisp = getUnconditionalBrDisp(Br.UncondBr);
  ImmBranches.push_back(ImmBranch(&MBB->back(), MaxDisp, false, Br.UncondBr));

  // Remove the old conditional branch.  It may or may not still be in MBB.
  BBInfo[MI->getParent()->getNumber()].Size -= TII->getInstSizeInBytes(*MI);
  MI->eraseFromParent();
  adjustBBOffsetsAfter(MBB);
  return true;
}

/// Returns a pass that converts branches to long branches.
FunctionPass *llvm::createCSKYConstantIslandPass() {
  return new CSKYConstantIslands();
}

INITIALIZE_PASS(CSKYConstantIslands, DEBUG_TYPE,
                "CSKY constant island placement and branch shortening pass",
                false, false)
