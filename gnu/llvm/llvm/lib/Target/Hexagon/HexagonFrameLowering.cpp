//===- HexagonFrameLowering.cpp - Define frame lowering -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//
//===----------------------------------------------------------------------===//

#include "HexagonFrameLowering.h"
#include "HexagonBlockRanges.h"
#include "HexagonInstrInfo.h"
#include "HexagonMachineFunctionInfo.h"
#include "HexagonRegisterInfo.h"
#include "HexagonSubtarget.h"
#include "HexagonTargetMachine.h"
#include "MCTargetDesc/HexagonBaseInfo.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachinePostDominators.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Pass.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#define DEBUG_TYPE "hexagon-pei"

// Hexagon stack frame layout as defined by the ABI:
//
//                                                       Incoming arguments
//                                                       passed via stack
//                                                                      |
//                                                                      |
//        SP during function's                 FP during function's     |
//    +-- runtime (top of stack)               runtime (bottom) --+     |
//    |                                                           |     |
// --++---------------------+------------------+-----------------++-+-------
//   |  parameter area for  |  variable-size   |   fixed-size    |LR|  arg
//   |   called functions   |  local objects   |  local objects  |FP|
// --+----------------------+------------------+-----------------+--+-------
//    <-    size known    -> <- size unknown -> <- size known  ->
//
// Low address                                                 High address
//
// <--- stack growth
//
//
// - In any circumstances, the outgoing function arguments are always accessi-
//   ble using the SP, and the incoming arguments are accessible using the FP.
// - If the local objects are not aligned, they can always be accessed using
//   the FP.
// - If there are no variable-sized objects, the local objects can always be
//   accessed using the SP, regardless whether they are aligned or not. (The
//   alignment padding will be at the bottom of the stack (highest address),
//   and so the offset with respect to the SP will be known at the compile-
//   -time.)
//
// The only complication occurs if there are both, local aligned objects, and
// dynamically allocated (variable-sized) objects. The alignment pad will be
// placed between the FP and the local objects, thus preventing the use of the
// FP to access the local objects. At the same time, the variable-sized objects
// will be between the SP and the local objects, thus introducing an unknown
// distance from the SP to the locals.
//
// To avoid this problem, a new register is created that holds the aligned
// address of the bottom of the stack, referred in the sources as AP (aligned
// pointer). The AP will be equal to "FP-p", where "p" is the smallest pad
// that aligns AP to the required boundary (a maximum of the alignments of
// all stack objects, fixed- and variable-sized). All local objects[1] will
// then use AP as the base pointer.
// [1] The exception is with "fixed" stack objects. "Fixed" stack objects get
// their name from being allocated at fixed locations on the stack, relative
// to the FP. In the presence of dynamic allocation and local alignment, such
// objects can only be accessed through the FP.
//
// Illustration of the AP:
//                                                                FP --+
//                                                                     |
// ---------------+---------------------+-----+-----------------------++-+--
//   Rest of the  | Local stack objects | Pad |  Fixed stack objects  |LR|
//   stack frame  | (aligned)           |     |  (CSR, spills, etc.)  |FP|
// ---------------+---------------------+-----+-----------------+-----+--+--
//                                      |<-- Multiple of the -->|
//                                           stack alignment    +-- AP
//
// The AP is set up at the beginning of the function. Since it is not a dedi-
// cated (reserved) register, it needs to be kept live throughout the function
// to be available as the base register for local object accesses.
// Normally, an address of a stack objects is obtained by a pseudo-instruction
// PS_fi. To access local objects with the AP register present, a different
// pseudo-instruction needs to be used: PS_fia. The PS_fia takes one extra
// argument compared to PS_fi: the first input register is the AP register.
// This keeps the register live between its definition and its uses.

// The AP register is originally set up using pseudo-instruction PS_aligna:
//   AP = PS_aligna A
// where
//   A  - required stack alignment
// The alignment value must be the maximum of all alignments required by
// any stack object.

// The dynamic allocation uses a pseudo-instruction PS_alloca:
//   Rd = PS_alloca Rs, A
// where
//   Rd - address of the allocated space
//   Rs - minimum size (the actual allocated can be larger to accommodate
//        alignment)
//   A  - required alignment

using namespace llvm;

static cl::opt<bool> DisableDeallocRet("disable-hexagon-dealloc-ret",
    cl::Hidden, cl::desc("Disable Dealloc Return for Hexagon target"));

static cl::opt<unsigned>
    NumberScavengerSlots("number-scavenger-slots", cl::Hidden,
                         cl::desc("Set the number of scavenger slots"),
                         cl::init(2));

static cl::opt<int>
    SpillFuncThreshold("spill-func-threshold", cl::Hidden,
                       cl::desc("Specify O2(not Os) spill func threshold"),
                       cl::init(6));

static cl::opt<int>
    SpillFuncThresholdOs("spill-func-threshold-Os", cl::Hidden,
                         cl::desc("Specify Os spill func threshold"),
                         cl::init(1));

static cl::opt<bool> EnableStackOVFSanitizer(
    "enable-stackovf-sanitizer", cl::Hidden,
    cl::desc("Enable runtime checks for stack overflow."), cl::init(false));

static cl::opt<bool>
    EnableShrinkWrapping("hexagon-shrink-frame", cl::init(true), cl::Hidden,
                         cl::desc("Enable stack frame shrink wrapping"));

static cl::opt<unsigned>
    ShrinkLimit("shrink-frame-limit",
                cl::init(std::numeric_limits<unsigned>::max()), cl::Hidden,
                cl::desc("Max count of stack frame shrink-wraps"));

static cl::opt<bool>
    EnableSaveRestoreLong("enable-save-restore-long", cl::Hidden,
                          cl::desc("Enable long calls for save-restore stubs."),
                          cl::init(false));

static cl::opt<bool> EliminateFramePointer("hexagon-fp-elim", cl::init(true),
    cl::Hidden, cl::desc("Refrain from using FP whenever possible"));

static cl::opt<bool> OptimizeSpillSlots("hexagon-opt-spill", cl::Hidden,
    cl::init(true), cl::desc("Optimize spill slots"));

#ifndef NDEBUG
static cl::opt<unsigned> SpillOptMax("spill-opt-max", cl::Hidden,
    cl::init(std::numeric_limits<unsigned>::max()));
static unsigned SpillOptCount = 0;
#endif

namespace llvm {

  void initializeHexagonCallFrameInformationPass(PassRegistry&);
  FunctionPass *createHexagonCallFrameInformation();

} // end namespace llvm

namespace {

  class HexagonCallFrameInformation : public MachineFunctionPass {
  public:
    static char ID;

    HexagonCallFrameInformation() : MachineFunctionPass(ID) {
      PassRegistry &PR = *PassRegistry::getPassRegistry();
      initializeHexagonCallFrameInformationPass(PR);
    }

    bool runOnMachineFunction(MachineFunction &MF) override;

    MachineFunctionProperties getRequiredProperties() const override {
      return MachineFunctionProperties().set(
          MachineFunctionProperties::Property::NoVRegs);
    }
  };

  char HexagonCallFrameInformation::ID = 0;

} // end anonymous namespace

bool HexagonCallFrameInformation::runOnMachineFunction(MachineFunction &MF) {
  auto &HFI = *MF.getSubtarget<HexagonSubtarget>().getFrameLowering();
  bool NeedCFI = MF.needsFrameMoves();

  if (!NeedCFI)
    return false;
  HFI.insertCFIInstructions(MF);
  return true;
}

INITIALIZE_PASS(HexagonCallFrameInformation, "hexagon-cfi",
                "Hexagon call frame information", false, false)

FunctionPass *llvm::createHexagonCallFrameInformation() {
  return new HexagonCallFrameInformation();
}

/// Map a register pair Reg to the subregister that has the greater "number",
/// i.e. D3 (aka R7:6) will be mapped to R7, etc.
static Register getMax32BitSubRegister(Register Reg,
                                       const TargetRegisterInfo &TRI,
                                       bool hireg = true) {
    if (Reg < Hexagon::D0 || Reg > Hexagon::D15)
      return Reg;

    Register RegNo = 0;
    for (MCPhysReg SubReg : TRI.subregs(Reg)) {
      if (hireg) {
        if (SubReg > RegNo)
          RegNo = SubReg;
      } else {
        if (!RegNo || SubReg < RegNo)
          RegNo = SubReg;
      }
    }
    return RegNo;
}

/// Returns the callee saved register with the largest id in the vector.
static Register getMaxCalleeSavedReg(ArrayRef<CalleeSavedInfo> CSI,
                                     const TargetRegisterInfo &TRI) {
  static_assert(Hexagon::R1 > 0,
                "Assume physical registers are encoded as positive integers");
  if (CSI.empty())
    return 0;

  Register Max = getMax32BitSubRegister(CSI[0].getReg(), TRI);
  for (unsigned I = 1, E = CSI.size(); I < E; ++I) {
    Register Reg = getMax32BitSubRegister(CSI[I].getReg(), TRI);
    if (Reg > Max)
      Max = Reg;
  }
  return Max;
}

/// Checks if the basic block contains any instruction that needs a stack
/// frame to be already in place.
static bool needsStackFrame(const MachineBasicBlock &MBB, const BitVector &CSR,
                            const HexagonRegisterInfo &HRI) {
    for (const MachineInstr &MI : MBB) {
      if (MI.isCall())
        return true;
      unsigned Opc = MI.getOpcode();
      switch (Opc) {
        case Hexagon::PS_alloca:
        case Hexagon::PS_aligna:
          return true;
        default:
          break;
      }
      // Check individual operands.
      for (const MachineOperand &MO : MI.operands()) {
        // While the presence of a frame index does not prove that a stack
        // frame will be required, all frame indexes should be within alloc-
        // frame/deallocframe. Otherwise, the code that translates a frame
        // index into an offset would have to be aware of the placement of
        // the frame creation/destruction instructions.
        if (MO.isFI())
          return true;
        if (MO.isReg()) {
          Register R = MO.getReg();
          // Debug instructions may refer to $noreg.
          if (!R)
            continue;
          // Virtual registers will need scavenging, which then may require
          // a stack slot.
          if (R.isVirtual())
            return true;
          for (MCPhysReg S : HRI.subregs_inclusive(R))
            if (CSR[S])
              return true;
          continue;
        }
        if (MO.isRegMask()) {
          // A regmask would normally have all callee-saved registers marked
          // as preserved, so this check would not be needed, but in case of
          // ever having other regmasks (for other calling conventions),
          // make sure they would be processed correctly.
          const uint32_t *BM = MO.getRegMask();
          for (int x = CSR.find_first(); x >= 0; x = CSR.find_next(x)) {
            unsigned R = x;
            // If this regmask does not preserve a CSR, a frame will be needed.
            if (!(BM[R/32] & (1u << (R%32))))
              return true;
          }
        }
      }
    }
    return false;
}

  /// Returns true if MBB has a machine instructions that indicates a tail call
  /// in the block.
static bool hasTailCall(const MachineBasicBlock &MBB) {
    MachineBasicBlock::const_iterator I = MBB.getLastNonDebugInstr();
    if (I == MBB.end())
      return false;
    unsigned RetOpc = I->getOpcode();
    return RetOpc == Hexagon::PS_tailcall_i || RetOpc == Hexagon::PS_tailcall_r;
}

/// Returns true if MBB contains an instruction that returns.
static bool hasReturn(const MachineBasicBlock &MBB) {
    for (const MachineInstr &MI : MBB.terminators())
      if (MI.isReturn())
        return true;
    return false;
}

/// Returns the "return" instruction from this block, or nullptr if there
/// isn't any.
static MachineInstr *getReturn(MachineBasicBlock &MBB) {
    for (auto &I : MBB)
      if (I.isReturn())
        return &I;
    return nullptr;
}

static bool isRestoreCall(unsigned Opc) {
    switch (Opc) {
      case Hexagon::RESTORE_DEALLOC_RET_JMP_V4:
      case Hexagon::RESTORE_DEALLOC_RET_JMP_V4_PIC:
      case Hexagon::RESTORE_DEALLOC_RET_JMP_V4_EXT:
      case Hexagon::RESTORE_DEALLOC_RET_JMP_V4_EXT_PIC:
      case Hexagon::RESTORE_DEALLOC_BEFORE_TAILCALL_V4_EXT:
      case Hexagon::RESTORE_DEALLOC_BEFORE_TAILCALL_V4_EXT_PIC:
      case Hexagon::RESTORE_DEALLOC_BEFORE_TAILCALL_V4:
      case Hexagon::RESTORE_DEALLOC_BEFORE_TAILCALL_V4_PIC:
        return true;
    }
    return false;
}

static inline bool isOptNone(const MachineFunction &MF) {
    return MF.getFunction().hasOptNone() ||
           MF.getTarget().getOptLevel() == CodeGenOptLevel::None;
}

static inline bool isOptSize(const MachineFunction &MF) {
    const Function &F = MF.getFunction();
    return F.hasOptSize() && !F.hasMinSize();
}

static inline bool isMinSize(const MachineFunction &MF) {
    return MF.getFunction().hasMinSize();
}

/// Implements shrink-wrapping of the stack frame. By default, stack frame
/// is created in the function entry block, and is cleaned up in every block
/// that returns. This function finds alternate blocks: one for the frame
/// setup (prolog) and one for the cleanup (epilog).
void HexagonFrameLowering::findShrunkPrologEpilog(MachineFunction &MF,
      MachineBasicBlock *&PrologB, MachineBasicBlock *&EpilogB) const {
  static unsigned ShrinkCounter = 0;

  if (MF.getSubtarget<HexagonSubtarget>().isEnvironmentMusl() &&
      MF.getFunction().isVarArg())
    return;
  if (ShrinkLimit.getPosition()) {
    if (ShrinkCounter >= ShrinkLimit)
      return;
    ShrinkCounter++;
  }

  auto &HRI = *MF.getSubtarget<HexagonSubtarget>().getRegisterInfo();

  MachineDominatorTree MDT;
  MDT.calculate(MF);
  MachinePostDominatorTree MPT;
  MPT.recalculate(MF);

  using UnsignedMap = DenseMap<unsigned, unsigned>;
  using RPOTType = ReversePostOrderTraversal<const MachineFunction *>;

  UnsignedMap RPO;
  RPOTType RPOT(&MF);
  unsigned RPON = 0;
  for (auto &I : RPOT)
    RPO[I->getNumber()] = RPON++;

  // Don't process functions that have loops, at least for now. Placement
  // of prolog and epilog must take loop structure into account. For simpli-
  // city don't do it right now.
  for (auto &I : MF) {
    unsigned BN = RPO[I.getNumber()];
    for (MachineBasicBlock *Succ : I.successors())
      // If found a back-edge, return.
      if (RPO[Succ->getNumber()] <= BN)
        return;
  }

  // Collect the set of blocks that need a stack frame to execute. Scan
  // each block for uses/defs of callee-saved registers, calls, etc.
  SmallVector<MachineBasicBlock*,16> SFBlocks;
  BitVector CSR(Hexagon::NUM_TARGET_REGS);
  for (const MCPhysReg *P = HRI.getCalleeSavedRegs(&MF); *P; ++P)
    for (MCPhysReg S : HRI.subregs_inclusive(*P))
      CSR[S] = true;

  for (auto &I : MF)
    if (needsStackFrame(I, CSR, HRI))
      SFBlocks.push_back(&I);

  LLVM_DEBUG({
    dbgs() << "Blocks needing SF: {";
    for (auto &B : SFBlocks)
      dbgs() << " " << printMBBReference(*B);
    dbgs() << " }\n";
  });
  // No frame needed?
  if (SFBlocks.empty())
    return;

  // Pick a common dominator and a common post-dominator.
  MachineBasicBlock *DomB = SFBlocks[0];
  for (unsigned i = 1, n = SFBlocks.size(); i < n; ++i) {
    DomB = MDT.findNearestCommonDominator(DomB, SFBlocks[i]);
    if (!DomB)
      break;
  }
  MachineBasicBlock *PDomB = SFBlocks[0];
  for (unsigned i = 1, n = SFBlocks.size(); i < n; ++i) {
    PDomB = MPT.findNearestCommonDominator(PDomB, SFBlocks[i]);
    if (!PDomB)
      break;
  }
  LLVM_DEBUG({
    dbgs() << "Computed dom block: ";
    if (DomB)
      dbgs() << printMBBReference(*DomB);
    else
      dbgs() << "<null>";
    dbgs() << ", computed pdom block: ";
    if (PDomB)
      dbgs() << printMBBReference(*PDomB);
    else
      dbgs() << "<null>";
    dbgs() << "\n";
  });
  if (!DomB || !PDomB)
    return;

  // Make sure that DomB dominates PDomB and PDomB post-dominates DomB.
  if (!MDT.dominates(DomB, PDomB)) {
    LLVM_DEBUG(dbgs() << "Dom block does not dominate pdom block\n");
    return;
  }
  if (!MPT.dominates(PDomB, DomB)) {
    LLVM_DEBUG(dbgs() << "PDom block does not post-dominate dom block\n");
    return;
  }

  // Finally, everything seems right.
  PrologB = DomB;
  EpilogB = PDomB;
}

/// Perform most of the PEI work here:
/// - saving/restoring of the callee-saved registers,
/// - stack frame creation and destruction.
/// Normally, this work is distributed among various functions, but doing it
/// in one place allows shrink-wrapping of the stack frame.
void HexagonFrameLowering::emitPrologue(MachineFunction &MF,
                                        MachineBasicBlock &MBB) const {
  auto &HRI = *MF.getSubtarget<HexagonSubtarget>().getRegisterInfo();

  MachineFrameInfo &MFI = MF.getFrameInfo();
  const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();

  MachineBasicBlock *PrologB = &MF.front(), *EpilogB = nullptr;
  if (EnableShrinkWrapping)
    findShrunkPrologEpilog(MF, PrologB, EpilogB);

  bool PrologueStubs = false;
  insertCSRSpillsInBlock(*PrologB, CSI, HRI, PrologueStubs);
  insertPrologueInBlock(*PrologB, PrologueStubs);
  updateEntryPaths(MF, *PrologB);

  if (EpilogB) {
    insertCSRRestoresInBlock(*EpilogB, CSI, HRI);
    insertEpilogueInBlock(*EpilogB);
  } else {
    for (auto &B : MF)
      if (B.isReturnBlock())
        insertCSRRestoresInBlock(B, CSI, HRI);

    for (auto &B : MF)
      if (B.isReturnBlock())
        insertEpilogueInBlock(B);

    for (auto &B : MF) {
      if (B.empty())
        continue;
      MachineInstr *RetI = getReturn(B);
      if (!RetI || isRestoreCall(RetI->getOpcode()))
        continue;
      for (auto &R : CSI)
        RetI->addOperand(MachineOperand::CreateReg(R.getReg(), false, true));
    }
  }

  if (EpilogB) {
    // If there is an epilog block, it may not have a return instruction.
    // In such case, we need to add the callee-saved registers as live-ins
    // in all blocks on all paths from the epilog to any return block.
    unsigned MaxBN = MF.getNumBlockIDs();
    BitVector DoneT(MaxBN+1), DoneF(MaxBN+1), Path(MaxBN+1);
    updateExitPaths(*EpilogB, *EpilogB, DoneT, DoneF, Path);
  }
}

/// Returns true if the target can safely skip saving callee-saved registers
/// for noreturn nounwind functions.
bool HexagonFrameLowering::enableCalleeSaveSkip(
    const MachineFunction &MF) const {
  const auto &F = MF.getFunction();
  assert(F.hasFnAttribute(Attribute::NoReturn) &&
         F.getFunction().hasFnAttribute(Attribute::NoUnwind) &&
         !F.getFunction().hasFnAttribute(Attribute::UWTable));
  (void)F;

  // No need to save callee saved registers if the function does not return.
  return MF.getSubtarget<HexagonSubtarget>().noreturnStackElim();
}

// Helper function used to determine when to eliminate the stack frame for
// functions marked as noreturn and when the noreturn-stack-elim options are
// specified. When both these conditions are true, then a FP may not be needed
// if the function makes a call. It is very similar to enableCalleeSaveSkip,
// but it used to check if the allocframe can be eliminated as well.
static bool enableAllocFrameElim(const MachineFunction &MF) {
  const auto &F = MF.getFunction();
  const auto &MFI = MF.getFrameInfo();
  const auto &HST = MF.getSubtarget<HexagonSubtarget>();
  assert(!MFI.hasVarSizedObjects() &&
         !HST.getRegisterInfo()->hasStackRealignment(MF));
  return F.hasFnAttribute(Attribute::NoReturn) &&
    F.hasFnAttribute(Attribute::NoUnwind) &&
    !F.hasFnAttribute(Attribute::UWTable) && HST.noreturnStackElim() &&
    MFI.getStackSize() == 0;
}

void HexagonFrameLowering::insertPrologueInBlock(MachineBasicBlock &MBB,
      bool PrologueStubs) const {
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  auto &HST = MF.getSubtarget<HexagonSubtarget>();
  auto &HII = *HST.getInstrInfo();
  auto &HRI = *HST.getRegisterInfo();

  Align MaxAlign = std::max(MFI.getMaxAlign(), getStackAlign());

  // Calculate the total stack frame size.
  // Get the number of bytes to allocate from the FrameInfo.
  unsigned FrameSize = MFI.getStackSize();
  // Round up the max call frame size to the max alignment on the stack.
  unsigned MaxCFA = alignTo(MFI.getMaxCallFrameSize(), MaxAlign);
  MFI.setMaxCallFrameSize(MaxCFA);

  FrameSize = MaxCFA + alignTo(FrameSize, MaxAlign);
  MFI.setStackSize(FrameSize);

  bool AlignStack = (MaxAlign > getStackAlign());

  // Get the number of bytes to allocate from the FrameInfo.
  unsigned NumBytes = MFI.getStackSize();
  Register SP = HRI.getStackRegister();
  unsigned MaxCF = MFI.getMaxCallFrameSize();
  MachineBasicBlock::iterator InsertPt = MBB.begin();

  SmallVector<MachineInstr *, 4> AdjustRegs;
  for (auto &MBB : MF)
    for (auto &MI : MBB)
      if (MI.getOpcode() == Hexagon::PS_alloca)
        AdjustRegs.push_back(&MI);

  for (auto *MI : AdjustRegs) {
    assert((MI->getOpcode() == Hexagon::PS_alloca) && "Expected alloca");
    expandAlloca(MI, HII, SP, MaxCF);
    MI->eraseFromParent();
  }

  DebugLoc dl = MBB.findDebugLoc(InsertPt);

  if (MF.getFunction().isVarArg() &&
      MF.getSubtarget<HexagonSubtarget>().isEnvironmentMusl()) {
    // Calculate the size of register saved area.
    int NumVarArgRegs = 6 - FirstVarArgSavedReg;
    int RegisterSavedAreaSizePlusPadding = (NumVarArgRegs % 2 == 0)
                                              ? NumVarArgRegs * 4
                                              : NumVarArgRegs * 4 + 4;
    if (RegisterSavedAreaSizePlusPadding > 0) {
      // Decrement the stack pointer by size of register saved area plus
      // padding if any.
      BuildMI(MBB, InsertPt, dl, HII.get(Hexagon::A2_addi), SP)
        .addReg(SP)
        .addImm(-RegisterSavedAreaSizePlusPadding)
        .setMIFlag(MachineInstr::FrameSetup);

      int NumBytes = 0;
      // Copy all the named arguments below register saved area.
      auto &HMFI = *MF.getInfo<HexagonMachineFunctionInfo>();
      for (int i = HMFI.getFirstNamedArgFrameIndex(),
               e = HMFI.getLastNamedArgFrameIndex(); i >= e; --i) {
        uint64_t ObjSize = MFI.getObjectSize(i);
        Align ObjAlign = MFI.getObjectAlign(i);

        // Determine the kind of load/store that should be used.
        unsigned LDOpc, STOpc;
        uint64_t OpcodeChecker = ObjAlign.value();

        // Handle cases where alignment of an object is > its size.
        if (ObjAlign > ObjSize) {
          if (ObjSize <= 1)
            OpcodeChecker = 1;
          else if (ObjSize <= 2)
            OpcodeChecker = 2;
          else if (ObjSize <= 4)
            OpcodeChecker = 4;
          else if (ObjSize > 4)
            OpcodeChecker = 8;
        }

        switch (OpcodeChecker) {
          case 1:
            LDOpc = Hexagon::L2_loadrb_io;
            STOpc = Hexagon::S2_storerb_io;
            break;
          case 2:
            LDOpc = Hexagon::L2_loadrh_io;
            STOpc = Hexagon::S2_storerh_io;
            break;
          case 4:
            LDOpc = Hexagon::L2_loadri_io;
            STOpc = Hexagon::S2_storeri_io;
            break;
          case 8:
          default:
            LDOpc = Hexagon::L2_loadrd_io;
            STOpc = Hexagon::S2_storerd_io;
            break;
        }

        Register RegUsed = LDOpc == Hexagon::L2_loadrd_io ? Hexagon::D3
                                                          : Hexagon::R6;
        int LoadStoreCount = ObjSize / OpcodeChecker;

        if (ObjSize % OpcodeChecker)
          ++LoadStoreCount;

        // Get the start location of the load. NumBytes is basically the
        // offset from the stack pointer of previous function, which would be
        // the caller in this case, as this function has variable argument
        // list.
        if (NumBytes != 0)
          NumBytes = alignTo(NumBytes, ObjAlign);

        int Count = 0;
        while (Count < LoadStoreCount) {
          // Load the value of the named argument on stack.
          BuildMI(MBB, InsertPt, dl, HII.get(LDOpc), RegUsed)
              .addReg(SP)
              .addImm(RegisterSavedAreaSizePlusPadding +
                      ObjAlign.value() * Count + NumBytes)
              .setMIFlag(MachineInstr::FrameSetup);

          // Store it below the register saved area plus padding.
          BuildMI(MBB, InsertPt, dl, HII.get(STOpc))
              .addReg(SP)
              .addImm(ObjAlign.value() * Count + NumBytes)
              .addReg(RegUsed)
              .setMIFlag(MachineInstr::FrameSetup);

          Count++;
        }
        NumBytes += MFI.getObjectSize(i);
      }

      // Make NumBytes 8 byte aligned
      NumBytes = alignTo(NumBytes, 8);

      // If the number of registers having variable arguments is odd,
      // leave 4 bytes of padding to get to the location where first
      // variable argument which was passed through register was copied.
      NumBytes = (NumVarArgRegs % 2 == 0) ? NumBytes : NumBytes + 4;

      for (int j = FirstVarArgSavedReg, i = 0; j < 6; ++j, ++i) {
        BuildMI(MBB, InsertPt, dl, HII.get(Hexagon::S2_storeri_io))
          .addReg(SP)
          .addImm(NumBytes + 4 * i)
          .addReg(Hexagon::R0 + j)
          .setMIFlag(MachineInstr::FrameSetup);
      }
    }
  }

  if (hasFP(MF)) {
    insertAllocframe(MBB, InsertPt, NumBytes);
    if (AlignStack) {
      BuildMI(MBB, InsertPt, dl, HII.get(Hexagon::A2_andir), SP)
          .addReg(SP)
          .addImm(-int64_t(MaxAlign.value()));
    }
    // If the stack-checking is enabled, and we spilled the callee-saved
    // registers inline (i.e. did not use a spill function), then call
    // the stack checker directly.
    if (EnableStackOVFSanitizer && !PrologueStubs)
      BuildMI(MBB, InsertPt, dl, HII.get(Hexagon::PS_call_stk))
             .addExternalSymbol("__runtime_stack_check");
  } else if (NumBytes > 0) {
    assert(alignTo(NumBytes, 8) == NumBytes);
    BuildMI(MBB, InsertPt, dl, HII.get(Hexagon::A2_addi), SP)
      .addReg(SP)
      .addImm(-int(NumBytes));
  }
}

void HexagonFrameLowering::insertEpilogueInBlock(MachineBasicBlock &MBB) const {
  MachineFunction &MF = *MBB.getParent();
  auto &HST = MF.getSubtarget<HexagonSubtarget>();
  auto &HII = *HST.getInstrInfo();
  auto &HRI = *HST.getRegisterInfo();
  Register SP = HRI.getStackRegister();

  MachineBasicBlock::iterator InsertPt = MBB.getFirstTerminator();
  DebugLoc dl = MBB.findDebugLoc(InsertPt);

  if (!hasFP(MF)) {
    MachineFrameInfo &MFI = MF.getFrameInfo();
    unsigned NumBytes = MFI.getStackSize();
    if (MF.getFunction().isVarArg() &&
        MF.getSubtarget<HexagonSubtarget>().isEnvironmentMusl()) {
      // On Hexagon Linux, deallocate the stack for the register saved area.
      int NumVarArgRegs = 6 - FirstVarArgSavedReg;
      int RegisterSavedAreaSizePlusPadding = (NumVarArgRegs % 2 == 0) ?
        (NumVarArgRegs * 4) : (NumVarArgRegs * 4 + 4);
      NumBytes += RegisterSavedAreaSizePlusPadding;
    }
    if (NumBytes) {
      BuildMI(MBB, InsertPt, dl, HII.get(Hexagon::A2_addi), SP)
        .addReg(SP)
        .addImm(NumBytes);
    }
    return;
  }

  MachineInstr *RetI = getReturn(MBB);
  unsigned RetOpc = RetI ? RetI->getOpcode() : 0;

  // Handle EH_RETURN.
  if (RetOpc == Hexagon::EH_RETURN_JMPR) {
    BuildMI(MBB, InsertPt, dl, HII.get(Hexagon::L2_deallocframe))
        .addDef(Hexagon::D15)
        .addReg(Hexagon::R30);
    BuildMI(MBB, InsertPt, dl, HII.get(Hexagon::A2_add), SP)
        .addReg(SP)
        .addReg(Hexagon::R28);
    return;
  }

  // Check for RESTORE_DEALLOC_RET* tail call. Don't emit an extra dealloc-
  // frame instruction if we encounter it.
  if (RetOpc == Hexagon::RESTORE_DEALLOC_RET_JMP_V4 ||
      RetOpc == Hexagon::RESTORE_DEALLOC_RET_JMP_V4_PIC ||
      RetOpc == Hexagon::RESTORE_DEALLOC_RET_JMP_V4_EXT ||
      RetOpc == Hexagon::RESTORE_DEALLOC_RET_JMP_V4_EXT_PIC) {
    MachineBasicBlock::iterator It = RetI;
    ++It;
    // Delete all instructions after the RESTORE (except labels).
    while (It != MBB.end()) {
      if (!It->isLabel())
        It = MBB.erase(It);
      else
        ++It;
    }
    return;
  }

  // It is possible that the restoring code is a call to a library function.
  // All of the restore* functions include "deallocframe", so we need to make
  // sure that we don't add an extra one.
  bool NeedsDeallocframe = true;
  if (!MBB.empty() && InsertPt != MBB.begin()) {
    MachineBasicBlock::iterator PrevIt = std::prev(InsertPt);
    unsigned COpc = PrevIt->getOpcode();
    if (COpc == Hexagon::RESTORE_DEALLOC_BEFORE_TAILCALL_V4 ||
        COpc == Hexagon::RESTORE_DEALLOC_BEFORE_TAILCALL_V4_PIC ||
        COpc == Hexagon::RESTORE_DEALLOC_BEFORE_TAILCALL_V4_EXT ||
        COpc == Hexagon::RESTORE_DEALLOC_BEFORE_TAILCALL_V4_EXT_PIC ||
        COpc == Hexagon::PS_call_nr || COpc == Hexagon::PS_callr_nr)
      NeedsDeallocframe = false;
  }

  if (!MF.getSubtarget<HexagonSubtarget>().isEnvironmentMusl() ||
      !MF.getFunction().isVarArg()) {
    if (!NeedsDeallocframe)
      return;
    // If the returning instruction is PS_jmpret, replace it with
    // dealloc_return, otherwise just add deallocframe. The function
    // could be returning via a tail call.
    if (RetOpc != Hexagon::PS_jmpret || DisableDeallocRet) {
      BuildMI(MBB, InsertPt, dl, HII.get(Hexagon::L2_deallocframe))
      .addDef(Hexagon::D15)
      .addReg(Hexagon::R30);
      return;
    }
    unsigned NewOpc = Hexagon::L4_return;
    MachineInstr *NewI = BuildMI(MBB, RetI, dl, HII.get(NewOpc))
      .addDef(Hexagon::D15)
      .addReg(Hexagon::R30);
    // Transfer the function live-out registers.
    NewI->copyImplicitOps(MF, *RetI);
    MBB.erase(RetI);
  } else {
    // L2_deallocframe instruction after it.
    // Calculate the size of register saved area.
    int NumVarArgRegs = 6 - FirstVarArgSavedReg;
    int RegisterSavedAreaSizePlusPadding = (NumVarArgRegs % 2 == 0) ?
      (NumVarArgRegs * 4) : (NumVarArgRegs * 4 + 4);

    MachineBasicBlock::iterator Term = MBB.getFirstTerminator();
    MachineBasicBlock::iterator I = (Term == MBB.begin()) ? MBB.end()
                                                          : std::prev(Term);
    if (I == MBB.end() ||
       (I->getOpcode() != Hexagon::RESTORE_DEALLOC_BEFORE_TAILCALL_V4_EXT &&
        I->getOpcode() != Hexagon::RESTORE_DEALLOC_BEFORE_TAILCALL_V4_EXT_PIC &&
        I->getOpcode() != Hexagon::RESTORE_DEALLOC_BEFORE_TAILCALL_V4 &&
        I->getOpcode() != Hexagon::RESTORE_DEALLOC_BEFORE_TAILCALL_V4_PIC))
      BuildMI(MBB, InsertPt, dl, HII.get(Hexagon::L2_deallocframe))
        .addDef(Hexagon::D15)
        .addReg(Hexagon::R30);
    if (RegisterSavedAreaSizePlusPadding != 0)
      BuildMI(MBB, InsertPt, dl, HII.get(Hexagon::A2_addi), SP)
        .addReg(SP)
        .addImm(RegisterSavedAreaSizePlusPadding);
  }
}

void HexagonFrameLowering::insertAllocframe(MachineBasicBlock &MBB,
      MachineBasicBlock::iterator InsertPt, unsigned NumBytes) const {
  MachineFunction &MF = *MBB.getParent();
  auto &HST = MF.getSubtarget<HexagonSubtarget>();
  auto &HII = *HST.getInstrInfo();
  auto &HRI = *HST.getRegisterInfo();

  // Check for overflow.
  // Hexagon_TODO: Ugh! hardcoding. Is there an API that can be used?
  const unsigned int ALLOCFRAME_MAX = 16384;

  // Create a dummy memory operand to avoid allocframe from being treated as
  // a volatile memory reference.
  auto *MMO = MF.getMachineMemOperand(MachinePointerInfo::getStack(MF, 0),
                                      MachineMemOperand::MOStore, 4, Align(4));

  DebugLoc dl = MBB.findDebugLoc(InsertPt);
  Register SP = HRI.getStackRegister();

  if (NumBytes >= ALLOCFRAME_MAX) {
    // Emit allocframe(#0).
    BuildMI(MBB, InsertPt, dl, HII.get(Hexagon::S2_allocframe))
      .addDef(SP)
      .addReg(SP)
      .addImm(0)
      .addMemOperand(MMO);

    // Subtract the size from the stack pointer.
    Register SP = HRI.getStackRegister();
    BuildMI(MBB, InsertPt, dl, HII.get(Hexagon::A2_addi), SP)
      .addReg(SP)
      .addImm(-int(NumBytes));
  } else {
    BuildMI(MBB, InsertPt, dl, HII.get(Hexagon::S2_allocframe))
      .addDef(SP)
      .addReg(SP)
      .addImm(NumBytes)
      .addMemOperand(MMO);
  }
}

void HexagonFrameLowering::updateEntryPaths(MachineFunction &MF,
      MachineBasicBlock &SaveB) const {
  SetVector<unsigned> Worklist;

  MachineBasicBlock &EntryB = MF.front();
  Worklist.insert(EntryB.getNumber());

  unsigned SaveN = SaveB.getNumber();
  auto &CSI = MF.getFrameInfo().getCalleeSavedInfo();

  for (unsigned i = 0; i < Worklist.size(); ++i) {
    unsigned BN = Worklist[i];
    MachineBasicBlock &MBB = *MF.getBlockNumbered(BN);
    for (auto &R : CSI)
      if (!MBB.isLiveIn(R.getReg()))
        MBB.addLiveIn(R.getReg());
    if (BN != SaveN)
      for (auto &SB : MBB.successors())
        Worklist.insert(SB->getNumber());
  }
}

bool HexagonFrameLowering::updateExitPaths(MachineBasicBlock &MBB,
      MachineBasicBlock &RestoreB, BitVector &DoneT, BitVector &DoneF,
      BitVector &Path) const {
  assert(MBB.getNumber() >= 0);
  unsigned BN = MBB.getNumber();
  if (Path[BN] || DoneF[BN])
    return false;
  if (DoneT[BN])
    return true;

  auto &CSI = MBB.getParent()->getFrameInfo().getCalleeSavedInfo();

  Path[BN] = true;
  bool ReachedExit = false;
  for (auto &SB : MBB.successors())
    ReachedExit |= updateExitPaths(*SB, RestoreB, DoneT, DoneF, Path);

  if (!MBB.empty() && MBB.back().isReturn()) {
    // Add implicit uses of all callee-saved registers to the reached
    // return instructions. This is to prevent the anti-dependency breaker
    // from renaming these registers.
    MachineInstr &RetI = MBB.back();
    if (!isRestoreCall(RetI.getOpcode()))
      for (auto &R : CSI)
        RetI.addOperand(MachineOperand::CreateReg(R.getReg(), false, true));
    ReachedExit = true;
  }

  // We don't want to add unnecessary live-ins to the restore block: since
  // the callee-saved registers are being defined in it, the entry of the
  // restore block cannot be on the path from the definitions to any exit.
  if (ReachedExit && &MBB != &RestoreB) {
    for (auto &R : CSI)
      if (!MBB.isLiveIn(R.getReg()))
        MBB.addLiveIn(R.getReg());
    DoneT[BN] = true;
  }
  if (!ReachedExit)
    DoneF[BN] = true;

  Path[BN] = false;
  return ReachedExit;
}

static std::optional<MachineBasicBlock::iterator>
findCFILocation(MachineBasicBlock &B) {
    // The CFI instructions need to be inserted right after allocframe.
    // An exception to this is a situation where allocframe is bundled
    // with a call: then the CFI instructions need to be inserted before
    // the packet with the allocframe+call (in case the call throws an
    // exception).
    auto End = B.instr_end();

    for (MachineInstr &I : B) {
      MachineBasicBlock::iterator It = I.getIterator();
      if (!I.isBundle()) {
        if (I.getOpcode() == Hexagon::S2_allocframe)
          return std::next(It);
        continue;
      }
      // I is a bundle.
      bool HasCall = false, HasAllocFrame = false;
      auto T = It.getInstrIterator();
      while (++T != End && T->isBundled()) {
        if (T->getOpcode() == Hexagon::S2_allocframe)
          HasAllocFrame = true;
        else if (T->isCall())
          HasCall = true;
      }
      if (HasAllocFrame)
        return HasCall ? It : std::next(It);
    }
    return std::nullopt;
}

void HexagonFrameLowering::insertCFIInstructions(MachineFunction &MF) const {
    for (auto &B : MF)
      if (auto At = findCFILocation(B))
        insertCFIInstructionsAt(B, *At);
}

void HexagonFrameLowering::insertCFIInstructionsAt(MachineBasicBlock &MBB,
      MachineBasicBlock::iterator At) const {
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  auto &HST = MF.getSubtarget<HexagonSubtarget>();
  auto &HII = *HST.getInstrInfo();
  auto &HRI = *HST.getRegisterInfo();

  // If CFI instructions have debug information attached, something goes
  // wrong with the final assembly generation: the prolog_end is placed
  // in a wrong location.
  DebugLoc DL;
  const MCInstrDesc &CFID = HII.get(TargetOpcode::CFI_INSTRUCTION);

  MCSymbol *FrameLabel = MF.getContext().createTempSymbol();
  bool HasFP = hasFP(MF);

  if (HasFP) {
    unsigned DwFPReg = HRI.getDwarfRegNum(HRI.getFrameRegister(), true);
    unsigned DwRAReg = HRI.getDwarfRegNum(HRI.getRARegister(), true);

    // Define CFA via an offset from the value of FP.
    //
    //  -8   -4    0 (SP)
    // --+----+----+---------------------
    //   | FP | LR |          increasing addresses -->
    // --+----+----+---------------------
    //   |         +-- Old SP (before allocframe)
    //   +-- New FP (after allocframe)
    //
    // MCCFIInstruction::cfiDefCfa adds the offset from the register.
    // MCCFIInstruction::createOffset takes the offset without sign change.
    auto DefCfa = MCCFIInstruction::cfiDefCfa(FrameLabel, DwFPReg, 8);
    BuildMI(MBB, At, DL, CFID)
        .addCFIIndex(MF.addFrameInst(DefCfa));
    // R31 (return addr) = CFA - 4
    auto OffR31 = MCCFIInstruction::createOffset(FrameLabel, DwRAReg, -4);
    BuildMI(MBB, At, DL, CFID)
        .addCFIIndex(MF.addFrameInst(OffR31));
    // R30 (frame ptr) = CFA - 8
    auto OffR30 = MCCFIInstruction::createOffset(FrameLabel, DwFPReg, -8);
    BuildMI(MBB, At, DL, CFID)
        .addCFIIndex(MF.addFrameInst(OffR30));
  }

  static Register RegsToMove[] = {
    Hexagon::R1,  Hexagon::R0,  Hexagon::R3,  Hexagon::R2,
    Hexagon::R17, Hexagon::R16, Hexagon::R19, Hexagon::R18,
    Hexagon::R21, Hexagon::R20, Hexagon::R23, Hexagon::R22,
    Hexagon::R25, Hexagon::R24, Hexagon::R27, Hexagon::R26,
    Hexagon::D0,  Hexagon::D1,  Hexagon::D8,  Hexagon::D9,
    Hexagon::D10, Hexagon::D11, Hexagon::D12, Hexagon::D13,
    Hexagon::NoRegister
  };

  const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();

  for (unsigned i = 0; RegsToMove[i] != Hexagon::NoRegister; ++i) {
    Register Reg = RegsToMove[i];
    auto IfR = [Reg] (const CalleeSavedInfo &C) -> bool {
      return C.getReg() == Reg;
    };
    auto F = find_if(CSI, IfR);
    if (F == CSI.end())
      continue;

    int64_t Offset;
    if (HasFP) {
      // If the function has a frame pointer (i.e. has an allocframe),
      // then the CFA has been defined in terms of FP. Any offsets in
      // the following CFI instructions have to be defined relative
      // to FP, which points to the bottom of the stack frame.
      // The function getFrameIndexReference can still choose to use SP
      // for the offset calculation, so we cannot simply call it here.
      // Instead, get the offset (relative to the FP) directly.
      Offset = MFI.getObjectOffset(F->getFrameIdx());
    } else {
      Register FrameReg;
      Offset =
          getFrameIndexReference(MF, F->getFrameIdx(), FrameReg).getFixed();
    }
    // Subtract 8 to make room for R30 and R31, which are added above.
    Offset -= 8;

    if (Reg < Hexagon::D0 || Reg > Hexagon::D15) {
      unsigned DwarfReg = HRI.getDwarfRegNum(Reg, true);
      auto OffReg = MCCFIInstruction::createOffset(FrameLabel, DwarfReg,
                                                   Offset);
      BuildMI(MBB, At, DL, CFID)
          .addCFIIndex(MF.addFrameInst(OffReg));
    } else {
      // Split the double regs into subregs, and generate appropriate
      // cfi_offsets.
      // The only reason, we are split double regs is, llvm-mc does not
      // understand paired registers for cfi_offset.
      // Eg .cfi_offset r1:0, -64

      Register HiReg = HRI.getSubReg(Reg, Hexagon::isub_hi);
      Register LoReg = HRI.getSubReg(Reg, Hexagon::isub_lo);
      unsigned HiDwarfReg = HRI.getDwarfRegNum(HiReg, true);
      unsigned LoDwarfReg = HRI.getDwarfRegNum(LoReg, true);
      auto OffHi = MCCFIInstruction::createOffset(FrameLabel, HiDwarfReg,
                                                  Offset+4);
      BuildMI(MBB, At, DL, CFID)
          .addCFIIndex(MF.addFrameInst(OffHi));
      auto OffLo = MCCFIInstruction::createOffset(FrameLabel, LoDwarfReg,
                                                  Offset);
      BuildMI(MBB, At, DL, CFID)
          .addCFIIndex(MF.addFrameInst(OffLo));
    }
  }
}

bool HexagonFrameLowering::hasFP(const MachineFunction &MF) const {
  if (MF.getFunction().hasFnAttribute(Attribute::Naked))
    return false;

  auto &MFI = MF.getFrameInfo();
  auto &HRI = *MF.getSubtarget<HexagonSubtarget>().getRegisterInfo();
  bool HasExtraAlign = HRI.hasStackRealignment(MF);
  bool HasAlloca = MFI.hasVarSizedObjects();

  // Insert ALLOCFRAME if we need to or at -O0 for the debugger.  Think
  // that this shouldn't be required, but doing so now because gcc does and
  // gdb can't break at the start of the function without it.  Will remove if
  // this turns out to be a gdb bug.
  //
  if (MF.getTarget().getOptLevel() == CodeGenOptLevel::None)
    return true;

  // By default we want to use SP (since it's always there). FP requires
  // some setup (i.e. ALLOCFRAME).
  // Both, alloca and stack alignment modify the stack pointer by an
  // undetermined value, so we need to save it at the entry to the function
  // (i.e. use allocframe).
  if (HasAlloca || HasExtraAlign)
    return true;

  if (MFI.getStackSize() > 0) {
    // If FP-elimination is disabled, we have to use FP at this point.
    const TargetMachine &TM = MF.getTarget();
    if (TM.Options.DisableFramePointerElim(MF) || !EliminateFramePointer)
      return true;
    if (EnableStackOVFSanitizer)
      return true;
  }

  const auto &HMFI = *MF.getInfo<HexagonMachineFunctionInfo>();
  if ((MFI.hasCalls() && !enableAllocFrameElim(MF)) || HMFI.hasClobberLR())
    return true;

  return false;
}

enum SpillKind {
  SK_ToMem,
  SK_FromMem,
  SK_FromMemTailcall
};

static const char *getSpillFunctionFor(Register MaxReg, SpillKind SpillType,
      bool Stkchk = false) {
  const char * V4SpillToMemoryFunctions[] = {
    "__save_r16_through_r17",
    "__save_r16_through_r19",
    "__save_r16_through_r21",
    "__save_r16_through_r23",
    "__save_r16_through_r25",
    "__save_r16_through_r27" };

  const char * V4SpillToMemoryStkchkFunctions[] = {
    "__save_r16_through_r17_stkchk",
    "__save_r16_through_r19_stkchk",
    "__save_r16_through_r21_stkchk",
    "__save_r16_through_r23_stkchk",
    "__save_r16_through_r25_stkchk",
    "__save_r16_through_r27_stkchk" };

  const char * V4SpillFromMemoryFunctions[] = {
    "__restore_r16_through_r17_and_deallocframe",
    "__restore_r16_through_r19_and_deallocframe",
    "__restore_r16_through_r21_and_deallocframe",
    "__restore_r16_through_r23_and_deallocframe",
    "__restore_r16_through_r25_and_deallocframe",
    "__restore_r16_through_r27_and_deallocframe" };

  const char * V4SpillFromMemoryTailcallFunctions[] = {
    "__restore_r16_through_r17_and_deallocframe_before_tailcall",
    "__restore_r16_through_r19_and_deallocframe_before_tailcall",
    "__restore_r16_through_r21_and_deallocframe_before_tailcall",
    "__restore_r16_through_r23_and_deallocframe_before_tailcall",
    "__restore_r16_through_r25_and_deallocframe_before_tailcall",
    "__restore_r16_through_r27_and_deallocframe_before_tailcall"
  };

  const char **SpillFunc = nullptr;

  switch(SpillType) {
  case SK_ToMem:
    SpillFunc = Stkchk ? V4SpillToMemoryStkchkFunctions
                       : V4SpillToMemoryFunctions;
    break;
  case SK_FromMem:
    SpillFunc = V4SpillFromMemoryFunctions;
    break;
  case SK_FromMemTailcall:
    SpillFunc = V4SpillFromMemoryTailcallFunctions;
    break;
  }
  assert(SpillFunc && "Unknown spill kind");

  // Spill all callee-saved registers up to the highest register used.
  switch (MaxReg) {
  case Hexagon::R17:
    return SpillFunc[0];
  case Hexagon::R19:
    return SpillFunc[1];
  case Hexagon::R21:
    return SpillFunc[2];
  case Hexagon::R23:
    return SpillFunc[3];
  case Hexagon::R25:
    return SpillFunc[4];
  case Hexagon::R27:
    return SpillFunc[5];
  default:
    llvm_unreachable("Unhandled maximum callee save register");
  }
  return nullptr;
}

StackOffset
HexagonFrameLowering::getFrameIndexReference(const MachineFunction &MF, int FI,
                                             Register &FrameReg) const {
  auto &MFI = MF.getFrameInfo();
  auto &HRI = *MF.getSubtarget<HexagonSubtarget>().getRegisterInfo();

  int Offset = MFI.getObjectOffset(FI);
  bool HasAlloca = MFI.hasVarSizedObjects();
  bool HasExtraAlign = HRI.hasStackRealignment(MF);
  bool NoOpt = MF.getTarget().getOptLevel() == CodeGenOptLevel::None;

  auto &HMFI = *MF.getInfo<HexagonMachineFunctionInfo>();
  unsigned FrameSize = MFI.getStackSize();
  Register SP = HRI.getStackRegister();
  Register FP = HRI.getFrameRegister();
  Register AP = HMFI.getStackAlignBaseReg();
  // It may happen that AP will be absent even HasAlloca && HasExtraAlign
  // is true. HasExtraAlign may be set because of vector spills, without
  // aligned locals or aligned outgoing function arguments. Since vector
  // spills will ultimately be "unaligned", it is safe to use FP as the
  // base register.
  // In fact, in such a scenario the stack is actually not required to be
  // aligned, although it may end up being aligned anyway, since this
  // particular case is not easily detectable. The alignment will be
  // unnecessary, but not incorrect.
  // Unfortunately there is no quick way to verify that the above is
  // indeed the case (and that it's not a result of an error), so just
  // assume that missing AP will be replaced by FP.
  // (A better fix would be to rematerialize AP from FP and always align
  // vector spills.)
  bool UseFP = false, UseAP = false;  // Default: use SP (except at -O0).
  // Use FP at -O0, except when there are objects with extra alignment.
  // That additional alignment requirement may cause a pad to be inserted,
  // which will make it impossible to use FP to access objects located
  // past the pad.
  if (NoOpt && !HasExtraAlign)
    UseFP = true;
  if (MFI.isFixedObjectIndex(FI) || MFI.isObjectPreAllocated(FI)) {
    // Fixed and preallocated objects will be located before any padding
    // so FP must be used to access them.
    UseFP |= (HasAlloca || HasExtraAlign);
  } else {
    if (HasAlloca) {
      if (HasExtraAlign)
        UseAP = true;
      else
        UseFP = true;
    }
  }

  // If FP was picked, then there had better be FP.
  bool HasFP = hasFP(MF);
  assert((HasFP || !UseFP) && "This function must have frame pointer");

  // Having FP implies allocframe. Allocframe will store extra 8 bytes:
  // FP/LR. If the base register is used to access an object across these
  // 8 bytes, then the offset will need to be adjusted by 8.
  //
  // After allocframe:
  //                    HexagonISelLowering adds 8 to ---+
  //                    the offsets of all stack-based   |
  //                    arguments (*)                    |
  //                                                     |
  //   getObjectOffset < 0   0     8  getObjectOffset >= 8
  // ------------------------+-----+------------------------> increasing
  //     <local objects>     |FP/LR|    <input arguments>     addresses
  // -----------------+------+-----+------------------------>
  //                  |      |
  //    SP/AP point --+      +-- FP points here (**)
  //    somewhere on
  //    this side of FP/LR
  //
  // (*) See LowerFormalArguments. The FP/LR is assumed to be present.
  // (**) *FP == old-FP. FP+0..7 are the bytes of FP/LR.

  // The lowering assumes that FP/LR is present, and so the offsets of
  // the formal arguments start at 8. If FP/LR is not there we need to
  // reduce the offset by 8.
  if (Offset > 0 && !HasFP)
    Offset -= 8;

  if (UseFP)
    FrameReg = FP;
  else if (UseAP)
    FrameReg = AP;
  else
    FrameReg = SP;

  // Calculate the actual offset in the instruction. If there is no FP
  // (in other words, no allocframe), then SP will not be adjusted (i.e.
  // there will be no SP -= FrameSize), so the frame size should not be
  // added to the calculated offset.
  int RealOffset = Offset;
  if (!UseFP && !UseAP)
    RealOffset = FrameSize+Offset;
  return StackOffset::getFixed(RealOffset);
}

bool HexagonFrameLowering::insertCSRSpillsInBlock(MachineBasicBlock &MBB,
      const CSIVect &CSI, const HexagonRegisterInfo &HRI,
      bool &PrologueStubs) const {
  if (CSI.empty())
    return true;

  MachineBasicBlock::iterator MI = MBB.begin();
  PrologueStubs = false;
  MachineFunction &MF = *MBB.getParent();
  auto &HST = MF.getSubtarget<HexagonSubtarget>();
  auto &HII = *HST.getInstrInfo();

  if (useSpillFunction(MF, CSI)) {
    PrologueStubs = true;
    Register MaxReg = getMaxCalleeSavedReg(CSI, HRI);
    bool StkOvrFlowEnabled = EnableStackOVFSanitizer;
    const char *SpillFun = getSpillFunctionFor(MaxReg, SK_ToMem,
                                               StkOvrFlowEnabled);
    auto &HTM = static_cast<const HexagonTargetMachine&>(MF.getTarget());
    bool IsPIC = HTM.isPositionIndependent();
    bool LongCalls = HST.useLongCalls() || EnableSaveRestoreLong;

    // Call spill function.
    DebugLoc DL = MI != MBB.end() ? MI->getDebugLoc() : DebugLoc();
    unsigned SpillOpc;
    if (StkOvrFlowEnabled) {
      if (LongCalls)
        SpillOpc = IsPIC ? Hexagon::SAVE_REGISTERS_CALL_V4STK_EXT_PIC
                         : Hexagon::SAVE_REGISTERS_CALL_V4STK_EXT;
      else
        SpillOpc = IsPIC ? Hexagon::SAVE_REGISTERS_CALL_V4STK_PIC
                         : Hexagon::SAVE_REGISTERS_CALL_V4STK;
    } else {
      if (LongCalls)
        SpillOpc = IsPIC ? Hexagon::SAVE_REGISTERS_CALL_V4_EXT_PIC
                         : Hexagon::SAVE_REGISTERS_CALL_V4_EXT;
      else
        SpillOpc = IsPIC ? Hexagon::SAVE_REGISTERS_CALL_V4_PIC
                         : Hexagon::SAVE_REGISTERS_CALL_V4;
    }

    MachineInstr *SaveRegsCall =
        BuildMI(MBB, MI, DL, HII.get(SpillOpc))
          .addExternalSymbol(SpillFun);

    // Add callee-saved registers as use.
    addCalleeSaveRegistersAsImpOperand(SaveRegsCall, CSI, false, true);
    // Add live in registers.
    for (const CalleeSavedInfo &I : CSI)
      MBB.addLiveIn(I.getReg());
    return true;
  }

  for (const CalleeSavedInfo &I : CSI) {
    Register Reg = I.getReg();
    // Add live in registers. We treat eh_return callee saved register r0 - r3
    // specially. They are not really callee saved registers as they are not
    // supposed to be killed.
    bool IsKill = !HRI.isEHReturnCalleeSaveReg(Reg);
    int FI = I.getFrameIdx();
    const TargetRegisterClass *RC = HRI.getMinimalPhysRegClass(Reg);
    HII.storeRegToStackSlot(MBB, MI, Reg, IsKill, FI, RC, &HRI, Register());
    if (IsKill)
      MBB.addLiveIn(Reg);
  }
  return true;
}

bool HexagonFrameLowering::insertCSRRestoresInBlock(MachineBasicBlock &MBB,
      const CSIVect &CSI, const HexagonRegisterInfo &HRI) const {
  if (CSI.empty())
    return false;

  MachineBasicBlock::iterator MI = MBB.getFirstTerminator();
  MachineFunction &MF = *MBB.getParent();
  auto &HST = MF.getSubtarget<HexagonSubtarget>();
  auto &HII = *HST.getInstrInfo();

  if (useRestoreFunction(MF, CSI)) {
    bool HasTC = hasTailCall(MBB) || !hasReturn(MBB);
    Register MaxR = getMaxCalleeSavedReg(CSI, HRI);
    SpillKind Kind = HasTC ? SK_FromMemTailcall : SK_FromMem;
    const char *RestoreFn = getSpillFunctionFor(MaxR, Kind);
    auto &HTM = static_cast<const HexagonTargetMachine&>(MF.getTarget());
    bool IsPIC = HTM.isPositionIndependent();
    bool LongCalls = HST.useLongCalls() || EnableSaveRestoreLong;

    // Call spill function.
    DebugLoc DL = MI != MBB.end() ? MI->getDebugLoc()
                                  : MBB.findDebugLoc(MBB.end());
    MachineInstr *DeallocCall = nullptr;

    if (HasTC) {
      unsigned RetOpc;
      if (LongCalls)
        RetOpc = IsPIC ? Hexagon::RESTORE_DEALLOC_BEFORE_TAILCALL_V4_EXT_PIC
                       : Hexagon::RESTORE_DEALLOC_BEFORE_TAILCALL_V4_EXT;
      else
        RetOpc = IsPIC ? Hexagon::RESTORE_DEALLOC_BEFORE_TAILCALL_V4_PIC
                       : Hexagon::RESTORE_DEALLOC_BEFORE_TAILCALL_V4;
      DeallocCall = BuildMI(MBB, MI, DL, HII.get(RetOpc))
          .addExternalSymbol(RestoreFn);
    } else {
      // The block has a return.
      MachineBasicBlock::iterator It = MBB.getFirstTerminator();
      assert(It->isReturn() && std::next(It) == MBB.end());
      unsigned RetOpc;
      if (LongCalls)
        RetOpc = IsPIC ? Hexagon::RESTORE_DEALLOC_RET_JMP_V4_EXT_PIC
                       : Hexagon::RESTORE_DEALLOC_RET_JMP_V4_EXT;
      else
        RetOpc = IsPIC ? Hexagon::RESTORE_DEALLOC_RET_JMP_V4_PIC
                       : Hexagon::RESTORE_DEALLOC_RET_JMP_V4;
      DeallocCall = BuildMI(MBB, It, DL, HII.get(RetOpc))
          .addExternalSymbol(RestoreFn);
      // Transfer the function live-out registers.
      DeallocCall->copyImplicitOps(MF, *It);
    }
    addCalleeSaveRegistersAsImpOperand(DeallocCall, CSI, true, false);
    return true;
  }

  for (const CalleeSavedInfo &I : CSI) {
    Register Reg = I.getReg();
    const TargetRegisterClass *RC = HRI.getMinimalPhysRegClass(Reg);
    int FI = I.getFrameIdx();
    HII.loadRegFromStackSlot(MBB, MI, Reg, FI, RC, &HRI, Register());
  }

  return true;
}

MachineBasicBlock::iterator HexagonFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  MachineInstr &MI = *I;
  unsigned Opc = MI.getOpcode();
  (void)Opc; // Silence compiler warning.
  assert((Opc == Hexagon::ADJCALLSTACKDOWN || Opc == Hexagon::ADJCALLSTACKUP) &&
         "Cannot handle this call frame pseudo instruction");
  return MBB.erase(I);
}

void HexagonFrameLowering::processFunctionBeforeFrameFinalized(
    MachineFunction &MF, RegScavenger *RS) const {
  // If this function has uses aligned stack and also has variable sized stack
  // objects, then we need to map all spill slots to fixed positions, so that
  // they can be accessed through FP. Otherwise they would have to be accessed
  // via AP, which may not be available at the particular place in the program.
  MachineFrameInfo &MFI = MF.getFrameInfo();
  bool HasAlloca = MFI.hasVarSizedObjects();
  bool NeedsAlign = (MFI.getMaxAlign() > getStackAlign());

  if (!HasAlloca || !NeedsAlign)
    return;

  // Set the physical aligned-stack base address register.
  Register AP = 0;
  if (const MachineInstr *AI = getAlignaInstr(MF))
    AP = AI->getOperand(0).getReg();
  auto &HMFI = *MF.getInfo<HexagonMachineFunctionInfo>();
  assert(!AP.isValid() || AP.isPhysical());
  HMFI.setStackAlignBaseReg(AP);
}

/// Returns true if there are no caller-saved registers available in class RC.
static bool needToReserveScavengingSpillSlots(MachineFunction &MF,
      const HexagonRegisterInfo &HRI, const TargetRegisterClass *RC) {
  MachineRegisterInfo &MRI = MF.getRegInfo();

  auto IsUsed = [&HRI,&MRI] (Register Reg) -> bool {
    for (MCRegAliasIterator AI(Reg, &HRI, true); AI.isValid(); ++AI)
      if (MRI.isPhysRegUsed(*AI))
        return true;
    return false;
  };

  // Check for an unused caller-saved register. Callee-saved registers
  // have become pristine by now.
  for (const MCPhysReg *P = HRI.getCallerSavedRegs(&MF, RC); *P; ++P)
    if (!IsUsed(*P))
      return false;

  // All caller-saved registers are used.
  return true;
}

#ifndef NDEBUG
static void dump_registers(BitVector &Regs, const TargetRegisterInfo &TRI) {
  dbgs() << '{';
  for (int x = Regs.find_first(); x >= 0; x = Regs.find_next(x)) {
    Register R = x;
    dbgs() << ' ' << printReg(R, &TRI);
  }
  dbgs() << " }";
}
#endif

bool HexagonFrameLowering::assignCalleeSavedSpillSlots(MachineFunction &MF,
      const TargetRegisterInfo *TRI, std::vector<CalleeSavedInfo> &CSI) const {
  LLVM_DEBUG(dbgs() << __func__ << " on " << MF.getName() << '\n');
  MachineFrameInfo &MFI = MF.getFrameInfo();
  BitVector SRegs(Hexagon::NUM_TARGET_REGS);

  // Generate a set of unique, callee-saved registers (SRegs), where each
  // register in the set is maximal in terms of sub-/super-register relation,
  // i.e. for each R in SRegs, no proper super-register of R is also in SRegs.

  // (1) For each callee-saved register, add that register and all of its
  // sub-registers to SRegs.
  LLVM_DEBUG(dbgs() << "Initial CS registers: {");
  for (const CalleeSavedInfo &I : CSI) {
    Register R = I.getReg();
    LLVM_DEBUG(dbgs() << ' ' << printReg(R, TRI));
    for (MCPhysReg SR : TRI->subregs_inclusive(R))
      SRegs[SR] = true;
  }
  LLVM_DEBUG(dbgs() << " }\n");
  LLVM_DEBUG(dbgs() << "SRegs.1: "; dump_registers(SRegs, *TRI);
             dbgs() << "\n");

  // (2) For each reserved register, remove that register and all of its
  // sub- and super-registers from SRegs.
  BitVector Reserved = TRI->getReservedRegs(MF);
  // Unreserve the stack align register: it is reserved for this function
  // only, it still needs to be saved/restored.
  Register AP =
      MF.getInfo<HexagonMachineFunctionInfo>()->getStackAlignBaseReg();
  if (AP.isValid()) {
    Reserved[AP] = false;
    // Unreserve super-regs if no other subregisters are reserved.
    for (MCPhysReg SP : TRI->superregs(AP)) {
      bool HasResSub = false;
      for (MCPhysReg SB : TRI->subregs(SP)) {
        if (!Reserved[SB])
          continue;
        HasResSub = true;
        break;
      }
      if (!HasResSub)
        Reserved[SP] = false;
    }
  }

  for (int x = Reserved.find_first(); x >= 0; x = Reserved.find_next(x)) {
    Register R = x;
    for (MCPhysReg SR : TRI->superregs_inclusive(R))
      SRegs[SR] = false;
  }
  LLVM_DEBUG(dbgs() << "Res:     "; dump_registers(Reserved, *TRI);
             dbgs() << "\n");
  LLVM_DEBUG(dbgs() << "SRegs.2: "; dump_registers(SRegs, *TRI);
             dbgs() << "\n");

  // (3) Collect all registers that have at least one sub-register in SRegs,
  // and also have no sub-registers that are reserved. These will be the can-
  // didates for saving as a whole instead of their individual sub-registers.
  // (Saving R17:16 instead of R16 is fine, but only if R17 was not reserved.)
  BitVector TmpSup(Hexagon::NUM_TARGET_REGS);
  for (int x = SRegs.find_first(); x >= 0; x = SRegs.find_next(x)) {
    Register R = x;
    for (MCPhysReg SR : TRI->superregs(R))
      TmpSup[SR] = true;
  }
  for (int x = TmpSup.find_first(); x >= 0; x = TmpSup.find_next(x)) {
    Register R = x;
    for (MCPhysReg SR : TRI->subregs_inclusive(R)) {
      if (!Reserved[SR])
        continue;
      TmpSup[R] = false;
      break;
    }
  }
  LLVM_DEBUG(dbgs() << "TmpSup:  "; dump_registers(TmpSup, *TRI);
             dbgs() << "\n");

  // (4) Include all super-registers found in (3) into SRegs.
  SRegs |= TmpSup;
  LLVM_DEBUG(dbgs() << "SRegs.4: "; dump_registers(SRegs, *TRI);
             dbgs() << "\n");

  // (5) For each register R in SRegs, if any super-register of R is in SRegs,
  // remove R from SRegs.
  for (int x = SRegs.find_first(); x >= 0; x = SRegs.find_next(x)) {
    Register R = x;
    for (MCPhysReg SR : TRI->superregs(R)) {
      if (!SRegs[SR])
        continue;
      SRegs[R] = false;
      break;
    }
  }
  LLVM_DEBUG(dbgs() << "SRegs.5: "; dump_registers(SRegs, *TRI);
             dbgs() << "\n");

  // Now, for each register that has a fixed stack slot, create the stack
  // object for it.
  CSI.clear();

  using SpillSlot = TargetFrameLowering::SpillSlot;

  unsigned NumFixed;
  int64_t MinOffset = 0; // CS offsets are negative.
  const SpillSlot *FixedSlots = getCalleeSavedSpillSlots(NumFixed);
  for (const SpillSlot *S = FixedSlots; S != FixedSlots+NumFixed; ++S) {
    if (!SRegs[S->Reg])
      continue;
    const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(S->Reg);
    int FI = MFI.CreateFixedSpillStackObject(TRI->getSpillSize(*RC), S->Offset);
    MinOffset = std::min(MinOffset, S->Offset);
    CSI.push_back(CalleeSavedInfo(S->Reg, FI));
    SRegs[S->Reg] = false;
  }

  // There can be some registers that don't have fixed slots. For example,
  // we need to store R0-R3 in functions with exception handling. For each
  // such register, create a non-fixed stack object.
  for (int x = SRegs.find_first(); x >= 0; x = SRegs.find_next(x)) {
    Register R = x;
    const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(R);
    unsigned Size = TRI->getSpillSize(*RC);
    int64_t Off = MinOffset - Size;
    Align Alignment = std::min(TRI->getSpillAlign(*RC), getStackAlign());
    Off &= -Alignment.value();
    int FI = MFI.CreateFixedSpillStackObject(Size, Off);
    MinOffset = std::min(MinOffset, Off);
    CSI.push_back(CalleeSavedInfo(R, FI));
    SRegs[R] = false;
  }

  LLVM_DEBUG({
    dbgs() << "CS information: {";
    for (const CalleeSavedInfo &I : CSI) {
      int FI = I.getFrameIdx();
      int Off = MFI.getObjectOffset(FI);
      dbgs() << ' ' << printReg(I.getReg(), TRI) << ":fi#" << FI << ":sp";
      if (Off >= 0)
        dbgs() << '+';
      dbgs() << Off;
    }
    dbgs() << " }\n";
  });

#ifndef NDEBUG
  // Verify that all registers were handled.
  bool MissedReg = false;
  for (int x = SRegs.find_first(); x >= 0; x = SRegs.find_next(x)) {
    Register R = x;
    dbgs() << printReg(R, TRI) << ' ';
    MissedReg = true;
  }
  if (MissedReg)
    llvm_unreachable("...there are unhandled callee-saved registers!");
#endif

  return true;
}

bool HexagonFrameLowering::expandCopy(MachineBasicBlock &B,
      MachineBasicBlock::iterator It, MachineRegisterInfo &MRI,
      const HexagonInstrInfo &HII, SmallVectorImpl<Register> &NewRegs) const {
  MachineInstr *MI = &*It;
  DebugLoc DL = MI->getDebugLoc();
  Register DstR = MI->getOperand(0).getReg();
  Register SrcR = MI->getOperand(1).getReg();
  if (!Hexagon::ModRegsRegClass.contains(DstR) ||
      !Hexagon::ModRegsRegClass.contains(SrcR))
    return false;

  Register TmpR = MRI.createVirtualRegister(&Hexagon::IntRegsRegClass);
  BuildMI(B, It, DL, HII.get(TargetOpcode::COPY), TmpR).add(MI->getOperand(1));
  BuildMI(B, It, DL, HII.get(TargetOpcode::COPY), DstR)
    .addReg(TmpR, RegState::Kill);

  NewRegs.push_back(TmpR);
  B.erase(It);
  return true;
}

bool HexagonFrameLowering::expandStoreInt(MachineBasicBlock &B,
      MachineBasicBlock::iterator It, MachineRegisterInfo &MRI,
      const HexagonInstrInfo &HII, SmallVectorImpl<Register> &NewRegs) const {
  MachineInstr *MI = &*It;
  if (!MI->getOperand(0).isFI())
    return false;

  DebugLoc DL = MI->getDebugLoc();
  unsigned Opc = MI->getOpcode();
  Register SrcR = MI->getOperand(2).getReg();
  bool IsKill = MI->getOperand(2).isKill();
  int FI = MI->getOperand(0).getIndex();

  // TmpR = C2_tfrpr SrcR   if SrcR is a predicate register
  // TmpR = A2_tfrcrr SrcR  if SrcR is a modifier register
  Register TmpR = MRI.createVirtualRegister(&Hexagon::IntRegsRegClass);
  unsigned TfrOpc = (Opc == Hexagon::STriw_pred) ? Hexagon::C2_tfrpr
                                                 : Hexagon::A2_tfrcrr;
  BuildMI(B, It, DL, HII.get(TfrOpc), TmpR)
    .addReg(SrcR, getKillRegState(IsKill));

  // S2_storeri_io FI, 0, TmpR
  BuildMI(B, It, DL, HII.get(Hexagon::S2_storeri_io))
      .addFrameIndex(FI)
      .addImm(0)
      .addReg(TmpR, RegState::Kill)
      .cloneMemRefs(*MI);

  NewRegs.push_back(TmpR);
  B.erase(It);
  return true;
}

bool HexagonFrameLowering::expandLoadInt(MachineBasicBlock &B,
      MachineBasicBlock::iterator It, MachineRegisterInfo &MRI,
      const HexagonInstrInfo &HII, SmallVectorImpl<Register> &NewRegs) const {
  MachineInstr *MI = &*It;
  if (!MI->getOperand(1).isFI())
    return false;

  DebugLoc DL = MI->getDebugLoc();
  unsigned Opc = MI->getOpcode();
  Register DstR = MI->getOperand(0).getReg();
  int FI = MI->getOperand(1).getIndex();

  // TmpR = L2_loadri_io FI, 0
  Register TmpR = MRI.createVirtualRegister(&Hexagon::IntRegsRegClass);
  BuildMI(B, It, DL, HII.get(Hexagon::L2_loadri_io), TmpR)
      .addFrameIndex(FI)
      .addImm(0)
      .cloneMemRefs(*MI);

  // DstR = C2_tfrrp TmpR   if DstR is a predicate register
  // DstR = A2_tfrrcr TmpR  if DstR is a modifier register
  unsigned TfrOpc = (Opc == Hexagon::LDriw_pred) ? Hexagon::C2_tfrrp
                                                 : Hexagon::A2_tfrrcr;
  BuildMI(B, It, DL, HII.get(TfrOpc), DstR)
    .addReg(TmpR, RegState::Kill);

  NewRegs.push_back(TmpR);
  B.erase(It);
  return true;
}

bool HexagonFrameLowering::expandStoreVecPred(MachineBasicBlock &B,
      MachineBasicBlock::iterator It, MachineRegisterInfo &MRI,
      const HexagonInstrInfo &HII, SmallVectorImpl<Register> &NewRegs) const {
  MachineInstr *MI = &*It;
  if (!MI->getOperand(0).isFI())
    return false;

  DebugLoc DL = MI->getDebugLoc();
  Register SrcR = MI->getOperand(2).getReg();
  bool IsKill = MI->getOperand(2).isKill();
  int FI = MI->getOperand(0).getIndex();
  auto *RC = &Hexagon::HvxVRRegClass;

  // Insert transfer to general vector register.
  //   TmpR0 = A2_tfrsi 0x01010101
  //   TmpR1 = V6_vandqrt Qx, TmpR0
  //   store FI, 0, TmpR1
  Register TmpR0 = MRI.createVirtualRegister(&Hexagon::IntRegsRegClass);
  Register TmpR1 = MRI.createVirtualRegister(RC);

  BuildMI(B, It, DL, HII.get(Hexagon::A2_tfrsi), TmpR0)
    .addImm(0x01010101);

  BuildMI(B, It, DL, HII.get(Hexagon::V6_vandqrt), TmpR1)
    .addReg(SrcR, getKillRegState(IsKill))
    .addReg(TmpR0, RegState::Kill);

  auto *HRI = B.getParent()->getSubtarget<HexagonSubtarget>().getRegisterInfo();
  HII.storeRegToStackSlot(B, It, TmpR1, true, FI, RC, HRI, Register());
  expandStoreVec(B, std::prev(It), MRI, HII, NewRegs);

  NewRegs.push_back(TmpR0);
  NewRegs.push_back(TmpR1);
  B.erase(It);
  return true;
}

bool HexagonFrameLowering::expandLoadVecPred(MachineBasicBlock &B,
      MachineBasicBlock::iterator It, MachineRegisterInfo &MRI,
      const HexagonInstrInfo &HII, SmallVectorImpl<Register> &NewRegs) const {
  MachineInstr *MI = &*It;
  if (!MI->getOperand(1).isFI())
    return false;

  DebugLoc DL = MI->getDebugLoc();
  Register DstR = MI->getOperand(0).getReg();
  int FI = MI->getOperand(1).getIndex();
  auto *RC = &Hexagon::HvxVRRegClass;

  // TmpR0 = A2_tfrsi 0x01010101
  // TmpR1 = load FI, 0
  // DstR = V6_vandvrt TmpR1, TmpR0
  Register TmpR0 = MRI.createVirtualRegister(&Hexagon::IntRegsRegClass);
  Register TmpR1 = MRI.createVirtualRegister(RC);

  BuildMI(B, It, DL, HII.get(Hexagon::A2_tfrsi), TmpR0)
    .addImm(0x01010101);
  MachineFunction &MF = *B.getParent();
  auto *HRI = MF.getSubtarget<HexagonSubtarget>().getRegisterInfo();
  HII.loadRegFromStackSlot(B, It, TmpR1, FI, RC, HRI, Register());
  expandLoadVec(B, std::prev(It), MRI, HII, NewRegs);

  BuildMI(B, It, DL, HII.get(Hexagon::V6_vandvrt), DstR)
    .addReg(TmpR1, RegState::Kill)
    .addReg(TmpR0, RegState::Kill);

  NewRegs.push_back(TmpR0);
  NewRegs.push_back(TmpR1);
  B.erase(It);
  return true;
}

bool HexagonFrameLowering::expandStoreVec2(MachineBasicBlock &B,
      MachineBasicBlock::iterator It, MachineRegisterInfo &MRI,
      const HexagonInstrInfo &HII, SmallVectorImpl<Register> &NewRegs) const {
  MachineFunction &MF = *B.getParent();
  auto &MFI = MF.getFrameInfo();
  auto &HRI = *MF.getSubtarget<HexagonSubtarget>().getRegisterInfo();
  MachineInstr *MI = &*It;
  if (!MI->getOperand(0).isFI())
    return false;

  // It is possible that the double vector being stored is only partially
  // defined. From the point of view of the liveness tracking, it is ok to
  // store it as a whole, but if we break it up we may end up storing a
  // register that is entirely undefined.
  LivePhysRegs LPR(HRI);
  LPR.addLiveIns(B);
  SmallVector<std::pair<MCPhysReg, const MachineOperand*>,2> Clobbers;
  for (auto R = B.begin(); R != It; ++R) {
    Clobbers.clear();
    LPR.stepForward(*R, Clobbers);
  }

  DebugLoc DL = MI->getDebugLoc();
  Register SrcR = MI->getOperand(2).getReg();
  Register SrcLo = HRI.getSubReg(SrcR, Hexagon::vsub_lo);
  Register SrcHi = HRI.getSubReg(SrcR, Hexagon::vsub_hi);
  bool IsKill = MI->getOperand(2).isKill();
  int FI = MI->getOperand(0).getIndex();

  unsigned Size = HRI.getSpillSize(Hexagon::HvxVRRegClass);
  Align NeedAlign = HRI.getSpillAlign(Hexagon::HvxVRRegClass);
  Align HasAlign = MFI.getObjectAlign(FI);
  unsigned StoreOpc;

  // Store low part.
  if (LPR.contains(SrcLo)) {
    StoreOpc = NeedAlign <= HasAlign ? Hexagon::V6_vS32b_ai
                                     : Hexagon::V6_vS32Ub_ai;
    BuildMI(B, It, DL, HII.get(StoreOpc))
        .addFrameIndex(FI)
        .addImm(0)
        .addReg(SrcLo, getKillRegState(IsKill))
        .cloneMemRefs(*MI);
  }

  // Store high part.
  if (LPR.contains(SrcHi)) {
    StoreOpc = NeedAlign <= HasAlign ? Hexagon::V6_vS32b_ai
                                     : Hexagon::V6_vS32Ub_ai;
    BuildMI(B, It, DL, HII.get(StoreOpc))
        .addFrameIndex(FI)
        .addImm(Size)
        .addReg(SrcHi, getKillRegState(IsKill))
        .cloneMemRefs(*MI);
  }

  B.erase(It);
  return true;
}

bool HexagonFrameLowering::expandLoadVec2(MachineBasicBlock &B,
      MachineBasicBlock::iterator It, MachineRegisterInfo &MRI,
      const HexagonInstrInfo &HII, SmallVectorImpl<Register> &NewRegs) const {
  MachineFunction &MF = *B.getParent();
  auto &MFI = MF.getFrameInfo();
  auto &HRI = *MF.getSubtarget<HexagonSubtarget>().getRegisterInfo();
  MachineInstr *MI = &*It;
  if (!MI->getOperand(1).isFI())
    return false;

  DebugLoc DL = MI->getDebugLoc();
  Register DstR = MI->getOperand(0).getReg();
  Register DstHi = HRI.getSubReg(DstR, Hexagon::vsub_hi);
  Register DstLo = HRI.getSubReg(DstR, Hexagon::vsub_lo);
  int FI = MI->getOperand(1).getIndex();

  unsigned Size = HRI.getSpillSize(Hexagon::HvxVRRegClass);
  Align NeedAlign = HRI.getSpillAlign(Hexagon::HvxVRRegClass);
  Align HasAlign = MFI.getObjectAlign(FI);
  unsigned LoadOpc;

  // Load low part.
  LoadOpc = NeedAlign <= HasAlign ? Hexagon::V6_vL32b_ai
                                  : Hexagon::V6_vL32Ub_ai;
  BuildMI(B, It, DL, HII.get(LoadOpc), DstLo)
      .addFrameIndex(FI)
      .addImm(0)
      .cloneMemRefs(*MI);

  // Load high part.
  LoadOpc = NeedAlign <= HasAlign ? Hexagon::V6_vL32b_ai
                                  : Hexagon::V6_vL32Ub_ai;
  BuildMI(B, It, DL, HII.get(LoadOpc), DstHi)
      .addFrameIndex(FI)
      .addImm(Size)
      .cloneMemRefs(*MI);

  B.erase(It);
  return true;
}

bool HexagonFrameLowering::expandStoreVec(MachineBasicBlock &B,
      MachineBasicBlock::iterator It, MachineRegisterInfo &MRI,
      const HexagonInstrInfo &HII, SmallVectorImpl<Register> &NewRegs) const {
  MachineFunction &MF = *B.getParent();
  auto &MFI = MF.getFrameInfo();
  MachineInstr *MI = &*It;
  if (!MI->getOperand(0).isFI())
    return false;

  auto &HRI = *MF.getSubtarget<HexagonSubtarget>().getRegisterInfo();
  DebugLoc DL = MI->getDebugLoc();
  Register SrcR = MI->getOperand(2).getReg();
  bool IsKill = MI->getOperand(2).isKill();
  int FI = MI->getOperand(0).getIndex();

  Align NeedAlign = HRI.getSpillAlign(Hexagon::HvxVRRegClass);
  Align HasAlign = MFI.getObjectAlign(FI);
  unsigned StoreOpc = NeedAlign <= HasAlign ? Hexagon::V6_vS32b_ai
                                            : Hexagon::V6_vS32Ub_ai;
  BuildMI(B, It, DL, HII.get(StoreOpc))
      .addFrameIndex(FI)
      .addImm(0)
      .addReg(SrcR, getKillRegState(IsKill))
      .cloneMemRefs(*MI);

  B.erase(It);
  return true;
}

bool HexagonFrameLowering::expandLoadVec(MachineBasicBlock &B,
      MachineBasicBlock::iterator It, MachineRegisterInfo &MRI,
      const HexagonInstrInfo &HII, SmallVectorImpl<Register> &NewRegs) const {
  MachineFunction &MF = *B.getParent();
  auto &MFI = MF.getFrameInfo();
  MachineInstr *MI = &*It;
  if (!MI->getOperand(1).isFI())
    return false;

  auto &HRI = *MF.getSubtarget<HexagonSubtarget>().getRegisterInfo();
  DebugLoc DL = MI->getDebugLoc();
  Register DstR = MI->getOperand(0).getReg();
  int FI = MI->getOperand(1).getIndex();

  Align NeedAlign = HRI.getSpillAlign(Hexagon::HvxVRRegClass);
  Align HasAlign = MFI.getObjectAlign(FI);
  unsigned LoadOpc = NeedAlign <= HasAlign ? Hexagon::V6_vL32b_ai
                                           : Hexagon::V6_vL32Ub_ai;
  BuildMI(B, It, DL, HII.get(LoadOpc), DstR)
      .addFrameIndex(FI)
      .addImm(0)
      .cloneMemRefs(*MI);

  B.erase(It);
  return true;
}

bool HexagonFrameLowering::expandSpillMacros(MachineFunction &MF,
      SmallVectorImpl<Register> &NewRegs) const {
  auto &HII = *MF.getSubtarget<HexagonSubtarget>().getInstrInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  bool Changed = false;

  for (auto &B : MF) {
    // Traverse the basic block.
    MachineBasicBlock::iterator NextI;
    for (auto I = B.begin(), E = B.end(); I != E; I = NextI) {
      MachineInstr *MI = &*I;
      NextI = std::next(I);
      unsigned Opc = MI->getOpcode();

      switch (Opc) {
        case TargetOpcode::COPY:
          Changed |= expandCopy(B, I, MRI, HII, NewRegs);
          break;
        case Hexagon::STriw_pred:
        case Hexagon::STriw_ctr:
          Changed |= expandStoreInt(B, I, MRI, HII, NewRegs);
          break;
        case Hexagon::LDriw_pred:
        case Hexagon::LDriw_ctr:
          Changed |= expandLoadInt(B, I, MRI, HII, NewRegs);
          break;
        case Hexagon::PS_vstorerq_ai:
          Changed |= expandStoreVecPred(B, I, MRI, HII, NewRegs);
          break;
        case Hexagon::PS_vloadrq_ai:
          Changed |= expandLoadVecPred(B, I, MRI, HII, NewRegs);
          break;
        case Hexagon::PS_vloadrw_ai:
          Changed |= expandLoadVec2(B, I, MRI, HII, NewRegs);
          break;
        case Hexagon::PS_vstorerw_ai:
          Changed |= expandStoreVec2(B, I, MRI, HII, NewRegs);
          break;
      }
    }
  }

  return Changed;
}

void HexagonFrameLowering::determineCalleeSaves(MachineFunction &MF,
                                                BitVector &SavedRegs,
                                                RegScavenger *RS) const {
  auto &HRI = *MF.getSubtarget<HexagonSubtarget>().getRegisterInfo();

  SavedRegs.resize(HRI.getNumRegs());

  // If we have a function containing __builtin_eh_return we want to spill and
  // restore all callee saved registers. Pretend that they are used.
  if (MF.getInfo<HexagonMachineFunctionInfo>()->hasEHReturn())
    for (const MCPhysReg *R = HRI.getCalleeSavedRegs(&MF); *R; ++R)
      SavedRegs.set(*R);

  // Replace predicate register pseudo spill code.
  SmallVector<Register,8> NewRegs;
  expandSpillMacros(MF, NewRegs);
  if (OptimizeSpillSlots && !isOptNone(MF))
    optimizeSpillSlots(MF, NewRegs);

  // We need to reserve a spill slot if scavenging could potentially require
  // spilling a scavenged register.
  if (!NewRegs.empty() || mayOverflowFrameOffset(MF)) {
    MachineFrameInfo &MFI = MF.getFrameInfo();
    MachineRegisterInfo &MRI = MF.getRegInfo();
    SetVector<const TargetRegisterClass*> SpillRCs;
    // Reserve an int register in any case, because it could be used to hold
    // the stack offset in case it does not fit into a spill instruction.
    SpillRCs.insert(&Hexagon::IntRegsRegClass);

    for (Register VR : NewRegs)
      SpillRCs.insert(MRI.getRegClass(VR));

    for (const auto *RC : SpillRCs) {
      if (!needToReserveScavengingSpillSlots(MF, HRI, RC))
        continue;
      unsigned Num = 1;
      switch (RC->getID()) {
        case Hexagon::IntRegsRegClassID:
          Num = NumberScavengerSlots;
          break;
        case Hexagon::HvxQRRegClassID:
          Num = 2; // Vector predicate spills also need a vector register.
          break;
      }
      unsigned S = HRI.getSpillSize(*RC);
      Align A = HRI.getSpillAlign(*RC);
      for (unsigned i = 0; i < Num; i++) {
        int NewFI = MFI.CreateSpillStackObject(S, A);
        RS->addScavengingFrameIndex(NewFI);
      }
    }
  }

  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);
}

Register HexagonFrameLowering::findPhysReg(MachineFunction &MF,
      HexagonBlockRanges::IndexRange &FIR,
      HexagonBlockRanges::InstrIndexMap &IndexMap,
      HexagonBlockRanges::RegToRangeMap &DeadMap,
      const TargetRegisterClass *RC) const {
  auto &HRI = *MF.getSubtarget<HexagonSubtarget>().getRegisterInfo();
  auto &MRI = MF.getRegInfo();

  auto isDead = [&FIR,&DeadMap] (Register Reg) -> bool {
    auto F = DeadMap.find({Reg,0});
    if (F == DeadMap.end())
      return false;
    for (auto &DR : F->second)
      if (DR.contains(FIR))
        return true;
    return false;
  };

  for (Register Reg : RC->getRawAllocationOrder(MF)) {
    bool Dead = true;
    for (auto R : HexagonBlockRanges::expandToSubRegs({Reg,0}, MRI, HRI)) {
      if (isDead(R.Reg))
        continue;
      Dead = false;
      break;
    }
    if (Dead)
      return Reg;
  }
  return 0;
}

void HexagonFrameLowering::optimizeSpillSlots(MachineFunction &MF,
      SmallVectorImpl<Register> &VRegs) const {
  auto &HST = MF.getSubtarget<HexagonSubtarget>();
  auto &HII = *HST.getInstrInfo();
  auto &HRI = *HST.getRegisterInfo();
  auto &MRI = MF.getRegInfo();
  HexagonBlockRanges HBR(MF);

  using BlockIndexMap =
      std::map<MachineBasicBlock *, HexagonBlockRanges::InstrIndexMap>;
  using BlockRangeMap =
      std::map<MachineBasicBlock *, HexagonBlockRanges::RangeList>;
  using IndexType = HexagonBlockRanges::IndexType;

  struct SlotInfo {
    BlockRangeMap Map;
    unsigned Size = 0;
    const TargetRegisterClass *RC = nullptr;

    SlotInfo() = default;
  };

  BlockIndexMap BlockIndexes;
  SmallSet<int,4> BadFIs;
  std::map<int,SlotInfo> FIRangeMap;

  // Accumulate register classes: get a common class for a pre-existing
  // class HaveRC and a new class NewRC. Return nullptr if a common class
  // cannot be found, otherwise return the resulting class. If HaveRC is
  // nullptr, assume that it is still unset.
  auto getCommonRC =
      [](const TargetRegisterClass *HaveRC,
         const TargetRegisterClass *NewRC) -> const TargetRegisterClass * {
    if (HaveRC == nullptr || HaveRC == NewRC)
      return NewRC;
    // Different classes, both non-null. Pick the more general one.
    if (HaveRC->hasSubClassEq(NewRC))
      return HaveRC;
    if (NewRC->hasSubClassEq(HaveRC))
      return NewRC;
    return nullptr;
  };

  // Scan all blocks in the function. Check all occurrences of frame indexes,
  // and collect relevant information.
  for (auto &B : MF) {
    std::map<int,IndexType> LastStore, LastLoad;
    // Emplace appears not to be supported in gcc 4.7.2-4.
    //auto P = BlockIndexes.emplace(&B, HexagonBlockRanges::InstrIndexMap(B));
    auto P = BlockIndexes.insert(
                std::make_pair(&B, HexagonBlockRanges::InstrIndexMap(B)));
    auto &IndexMap = P.first->second;
    LLVM_DEBUG(dbgs() << "Index map for " << printMBBReference(B) << "\n"
                      << IndexMap << '\n');

    for (auto &In : B) {
      int LFI, SFI;
      bool Load = HII.isLoadFromStackSlot(In, LFI) && !HII.isPredicated(In);
      bool Store = HII.isStoreToStackSlot(In, SFI) && !HII.isPredicated(In);
      if (Load && Store) {
        // If it's both a load and a store, then we won't handle it.
        BadFIs.insert(LFI);
        BadFIs.insert(SFI);
        continue;
      }
      // Check for register classes of the register used as the source for
      // the store, and the register used as the destination for the load.
      // Also, only accept base+imm_offset addressing modes. Other addressing
      // modes can have side-effects (post-increments, etc.). For stack
      // slots they are very unlikely, so there is not much loss due to
      // this restriction.
      if (Load || Store) {
        int TFI = Load ? LFI : SFI;
        unsigned AM = HII.getAddrMode(In);
        SlotInfo &SI = FIRangeMap[TFI];
        bool Bad = (AM != HexagonII::BaseImmOffset);
        if (!Bad) {
          // If the addressing mode is ok, check the register class.
          unsigned OpNum = Load ? 0 : 2;
          auto *RC = HII.getRegClass(In.getDesc(), OpNum, &HRI, MF);
          RC = getCommonRC(SI.RC, RC);
          if (RC == nullptr)
            Bad = true;
          else
            SI.RC = RC;
        }
        if (!Bad) {
          // Check sizes.
          unsigned S = HII.getMemAccessSize(In);
          if (SI.Size != 0 && SI.Size != S)
            Bad = true;
          else
            SI.Size = S;
        }
        if (!Bad) {
          for (auto *Mo : In.memoperands()) {
            if (!Mo->isVolatile() && !Mo->isAtomic())
              continue;
            Bad = true;
            break;
          }
        }
        if (Bad)
          BadFIs.insert(TFI);
      }

      // Locate uses of frame indices.
      for (unsigned i = 0, n = In.getNumOperands(); i < n; ++i) {
        const MachineOperand &Op = In.getOperand(i);
        if (!Op.isFI())
          continue;
        int FI = Op.getIndex();
        // Make sure that the following operand is an immediate and that
        // it is 0. This is the offset in the stack object.
        if (i+1 >= n || !In.getOperand(i+1).isImm() ||
            In.getOperand(i+1).getImm() != 0)
          BadFIs.insert(FI);
        if (BadFIs.count(FI))
          continue;

        IndexType Index = IndexMap.getIndex(&In);
        if (Load) {
          if (LastStore[FI] == IndexType::None)
            LastStore[FI] = IndexType::Entry;
          LastLoad[FI] = Index;
        } else if (Store) {
          HexagonBlockRanges::RangeList &RL = FIRangeMap[FI].Map[&B];
          if (LastStore[FI] != IndexType::None)
            RL.add(LastStore[FI], LastLoad[FI], false, false);
          else if (LastLoad[FI] != IndexType::None)
            RL.add(IndexType::Entry, LastLoad[FI], false, false);
          LastLoad[FI] = IndexType::None;
          LastStore[FI] = Index;
        } else {
          BadFIs.insert(FI);
        }
      }
    }

    for (auto &I : LastLoad) {
      IndexType LL = I.second;
      if (LL == IndexType::None)
        continue;
      auto &RL = FIRangeMap[I.first].Map[&B];
      IndexType &LS = LastStore[I.first];
      if (LS != IndexType::None)
        RL.add(LS, LL, false, false);
      else
        RL.add(IndexType::Entry, LL, false, false);
      LS = IndexType::None;
    }
    for (auto &I : LastStore) {
      IndexType LS = I.second;
      if (LS == IndexType::None)
        continue;
      auto &RL = FIRangeMap[I.first].Map[&B];
      RL.add(LS, IndexType::None, false, false);
    }
  }

  LLVM_DEBUG({
    for (auto &P : FIRangeMap) {
      dbgs() << "fi#" << P.first;
      if (BadFIs.count(P.first))
        dbgs() << " (bad)";
      dbgs() << "  RC: ";
      if (P.second.RC != nullptr)
        dbgs() << HRI.getRegClassName(P.second.RC) << '\n';
      else
        dbgs() << "<null>\n";
      for (auto &R : P.second.Map)
        dbgs() << "  " << printMBBReference(*R.first) << " { " << R.second
               << "}\n";
    }
  });

  // When a slot is loaded from in a block without being stored to in the
  // same block, it is live-on-entry to this block. To avoid CFG analysis,
  // consider this slot to be live-on-exit from all blocks.
  SmallSet<int,4> LoxFIs;

  std::map<MachineBasicBlock*,std::vector<int>> BlockFIMap;

  for (auto &P : FIRangeMap) {
    // P = pair(FI, map: BB->RangeList)
    if (BadFIs.count(P.first))
      continue;
    for (auto &B : MF) {
      auto F = P.second.Map.find(&B);
      // F = pair(BB, RangeList)
      if (F == P.second.Map.end() || F->second.empty())
        continue;
      HexagonBlockRanges::IndexRange &IR = F->second.front();
      if (IR.start() == IndexType::Entry)
        LoxFIs.insert(P.first);
      BlockFIMap[&B].push_back(P.first);
    }
  }

  LLVM_DEBUG({
    dbgs() << "Block-to-FI map (* -- live-on-exit):\n";
    for (auto &P : BlockFIMap) {
      auto &FIs = P.second;
      if (FIs.empty())
        continue;
      dbgs() << "  " << printMBBReference(*P.first) << ": {";
      for (auto I : FIs) {
        dbgs() << " fi#" << I;
        if (LoxFIs.count(I))
          dbgs() << '*';
      }
      dbgs() << " }\n";
    }
  });

#ifndef NDEBUG
  bool HasOptLimit = SpillOptMax.getPosition();
#endif

  // eliminate loads, when all loads eliminated, eliminate all stores.
  for (auto &B : MF) {
    auto F = BlockIndexes.find(&B);
    assert(F != BlockIndexes.end());
    HexagonBlockRanges::InstrIndexMap &IM = F->second;
    HexagonBlockRanges::RegToRangeMap LM = HBR.computeLiveMap(IM);
    HexagonBlockRanges::RegToRangeMap DM = HBR.computeDeadMap(IM, LM);
    LLVM_DEBUG(dbgs() << printMBBReference(B) << " dead map\n"
                      << HexagonBlockRanges::PrintRangeMap(DM, HRI));

    for (auto FI : BlockFIMap[&B]) {
      if (BadFIs.count(FI))
        continue;
      LLVM_DEBUG(dbgs() << "Working on fi#" << FI << '\n');
      HexagonBlockRanges::RangeList &RL = FIRangeMap[FI].Map[&B];
      for (auto &Range : RL) {
        LLVM_DEBUG(dbgs() << "--Examining range:" << RL << '\n');
        if (!IndexType::isInstr(Range.start()) ||
            !IndexType::isInstr(Range.end()))
          continue;
        MachineInstr &SI = *IM.getInstr(Range.start());
        MachineInstr &EI = *IM.getInstr(Range.end());
        assert(SI.mayStore() && "Unexpected start instruction");
        assert(EI.mayLoad() && "Unexpected end instruction");
        MachineOperand &SrcOp = SI.getOperand(2);

        HexagonBlockRanges::RegisterRef SrcRR = { SrcOp.getReg(),
                                                  SrcOp.getSubReg() };
        auto *RC = HII.getRegClass(SI.getDesc(), 2, &HRI, MF);
        // The this-> is needed to unconfuse MSVC.
        Register FoundR = this->findPhysReg(MF, Range, IM, DM, RC);
        LLVM_DEBUG(dbgs() << "Replacement reg:" << printReg(FoundR, &HRI)
                          << '\n');
        if (FoundR == 0)
          continue;
#ifndef NDEBUG
        if (HasOptLimit) {
          if (SpillOptCount >= SpillOptMax)
            return;
          SpillOptCount++;
        }
#endif

        // Generate the copy-in: "FoundR = COPY SrcR" at the store location.
        MachineBasicBlock::iterator StartIt = SI.getIterator(), NextIt;
        MachineInstr *CopyIn = nullptr;
        if (SrcRR.Reg != FoundR || SrcRR.Sub != 0) {
          const DebugLoc &DL = SI.getDebugLoc();
          CopyIn = BuildMI(B, StartIt, DL, HII.get(TargetOpcode::COPY), FoundR)
                       .add(SrcOp);
        }

        ++StartIt;
        // Check if this is a last store and the FI is live-on-exit.
        if (LoxFIs.count(FI) && (&Range == &RL.back())) {
          // Update store's source register.
          if (unsigned SR = SrcOp.getSubReg())
            SrcOp.setReg(HRI.getSubReg(FoundR, SR));
          else
            SrcOp.setReg(FoundR);
          SrcOp.setSubReg(0);
          // We are keeping this register live.
          SrcOp.setIsKill(false);
        } else {
          B.erase(&SI);
          IM.replaceInstr(&SI, CopyIn);
        }

        auto EndIt = std::next(EI.getIterator());
        for (auto It = StartIt; It != EndIt; It = NextIt) {
          MachineInstr &MI = *It;
          NextIt = std::next(It);
          int TFI;
          if (!HII.isLoadFromStackSlot(MI, TFI) || TFI != FI)
            continue;
          Register DstR = MI.getOperand(0).getReg();
          assert(MI.getOperand(0).getSubReg() == 0);
          MachineInstr *CopyOut = nullptr;
          if (DstR != FoundR) {
            DebugLoc DL = MI.getDebugLoc();
            unsigned MemSize = HII.getMemAccessSize(MI);
            assert(HII.getAddrMode(MI) == HexagonII::BaseImmOffset);
            unsigned CopyOpc = TargetOpcode::COPY;
            if (HII.isSignExtendingLoad(MI))
              CopyOpc = (MemSize == 1) ? Hexagon::A2_sxtb : Hexagon::A2_sxth;
            else if (HII.isZeroExtendingLoad(MI))
              CopyOpc = (MemSize == 1) ? Hexagon::A2_zxtb : Hexagon::A2_zxth;
            CopyOut = BuildMI(B, It, DL, HII.get(CopyOpc), DstR)
                        .addReg(FoundR, getKillRegState(&MI == &EI));
          }
          IM.replaceInstr(&MI, CopyOut);
          B.erase(It);
        }

        // Update the dead map.
        HexagonBlockRanges::RegisterRef FoundRR = { FoundR, 0 };
        for (auto RR : HexagonBlockRanges::expandToSubRegs(FoundRR, MRI, HRI))
          DM[RR].subtract(Range);
      } // for Range in range list
    }
  }
}

void HexagonFrameLowering::expandAlloca(MachineInstr *AI,
      const HexagonInstrInfo &HII, Register SP, unsigned CF) const {
  MachineBasicBlock &MB = *AI->getParent();
  DebugLoc DL = AI->getDebugLoc();
  unsigned A = AI->getOperand(2).getImm();

  // Have
  //    Rd  = alloca Rs, #A
  //
  // If Rs and Rd are different registers, use this sequence:
  //    Rd  = sub(r29, Rs)
  //    r29 = sub(r29, Rs)
  //    Rd  = and(Rd, #-A)    ; if necessary
  //    r29 = and(r29, #-A)   ; if necessary
  //    Rd  = add(Rd, #CF)    ; CF size aligned to at most A
  // otherwise, do
  //    Rd  = sub(r29, Rs)
  //    Rd  = and(Rd, #-A)    ; if necessary
  //    r29 = Rd
  //    Rd  = add(Rd, #CF)    ; CF size aligned to at most A

  MachineOperand &RdOp = AI->getOperand(0);
  MachineOperand &RsOp = AI->getOperand(1);
  Register Rd = RdOp.getReg(), Rs = RsOp.getReg();

  // Rd = sub(r29, Rs)
  BuildMI(MB, AI, DL, HII.get(Hexagon::A2_sub), Rd)
      .addReg(SP)
      .addReg(Rs);
  if (Rs != Rd) {
    // r29 = sub(r29, Rs)
    BuildMI(MB, AI, DL, HII.get(Hexagon::A2_sub), SP)
        .addReg(SP)
        .addReg(Rs);
  }
  if (A > 8) {
    // Rd  = and(Rd, #-A)
    BuildMI(MB, AI, DL, HII.get(Hexagon::A2_andir), Rd)
        .addReg(Rd)
        .addImm(-int64_t(A));
    if (Rs != Rd)
      BuildMI(MB, AI, DL, HII.get(Hexagon::A2_andir), SP)
          .addReg(SP)
          .addImm(-int64_t(A));
  }
  if (Rs == Rd) {
    // r29 = Rd
    BuildMI(MB, AI, DL, HII.get(TargetOpcode::COPY), SP)
        .addReg(Rd);
  }
  if (CF > 0) {
    // Rd = add(Rd, #CF)
    BuildMI(MB, AI, DL, HII.get(Hexagon::A2_addi), Rd)
        .addReg(Rd)
        .addImm(CF);
  }
}

bool HexagonFrameLowering::needsAligna(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  if (!MFI.hasVarSizedObjects())
    return false;
  // Do not check for max stack object alignment here, because the stack
  // may not be complete yet. Assume that we will need PS_aligna if there
  // are variable-sized objects.
  return true;
}

const MachineInstr *HexagonFrameLowering::getAlignaInstr(
      const MachineFunction &MF) const {
  for (auto &B : MF)
    for (auto &I : B)
      if (I.getOpcode() == Hexagon::PS_aligna)
        return &I;
  return nullptr;
}

/// Adds all callee-saved registers as implicit uses or defs to the
/// instruction.
void HexagonFrameLowering::addCalleeSaveRegistersAsImpOperand(MachineInstr *MI,
      const CSIVect &CSI, bool IsDef, bool IsKill) const {
  // Add the callee-saved registers as implicit uses.
  for (auto &R : CSI)
    MI->addOperand(MachineOperand::CreateReg(R.getReg(), IsDef, true, IsKill));
}

/// Determine whether the callee-saved register saves and restores should
/// be generated via inline code. If this function returns "true", inline
/// code will be generated. If this function returns "false", additional
/// checks are performed, which may still lead to the inline code.
bool HexagonFrameLowering::shouldInlineCSR(const MachineFunction &MF,
      const CSIVect &CSI) const {
  if (MF.getSubtarget<HexagonSubtarget>().isEnvironmentMusl())
    return true;
  if (MF.getInfo<HexagonMachineFunctionInfo>()->hasEHReturn())
    return true;
  if (!hasFP(MF))
    return true;
  if (!isOptSize(MF) && !isMinSize(MF))
    if (MF.getTarget().getOptLevel() > CodeGenOptLevel::Default)
      return true;

  // Check if CSI only has double registers, and if the registers form
  // a contiguous block starting from D8.
  BitVector Regs(Hexagon::NUM_TARGET_REGS);
  for (const CalleeSavedInfo &I : CSI) {
    Register R = I.getReg();
    if (!Hexagon::DoubleRegsRegClass.contains(R))
      return true;
    Regs[R] = true;
  }
  int F = Regs.find_first();
  if (F != Hexagon::D8)
    return true;
  while (F >= 0) {
    int N = Regs.find_next(F);
    if (N >= 0 && N != F+1)
      return true;
    F = N;
  }

  return false;
}

bool HexagonFrameLowering::useSpillFunction(const MachineFunction &MF,
      const CSIVect &CSI) const {
  if (shouldInlineCSR(MF, CSI))
    return false;
  unsigned NumCSI = CSI.size();
  if (NumCSI <= 1)
    return false;

  unsigned Threshold = isOptSize(MF) ? SpillFuncThresholdOs
                                     : SpillFuncThreshold;
  return Threshold < NumCSI;
}

bool HexagonFrameLowering::useRestoreFunction(const MachineFunction &MF,
      const CSIVect &CSI) const {
  if (shouldInlineCSR(MF, CSI))
    return false;
  // The restore functions do a bit more than just restoring registers.
  // The non-returning versions will go back directly to the caller's
  // caller, others will clean up the stack frame in preparation for
  // a tail call. Using them can still save code size even if only one
  // register is getting restores. Make the decision based on -Oz:
  // using -Os will use inline restore for a single register.
  if (isMinSize(MF))
    return true;
  unsigned NumCSI = CSI.size();
  if (NumCSI <= 1)
    return false;

  unsigned Threshold = isOptSize(MF) ? SpillFuncThresholdOs-1
                                     : SpillFuncThreshold;
  return Threshold < NumCSI;
}

bool HexagonFrameLowering::mayOverflowFrameOffset(MachineFunction &MF) const {
  unsigned StackSize = MF.getFrameInfo().estimateStackSize(MF);
  auto &HST = MF.getSubtarget<HexagonSubtarget>();
  // A fairly simplistic guess as to whether a potential load/store to a
  // stack location could require an extra register.
  if (HST.useHVXOps() && StackSize > 256)
    return true;

  // Check if the function has store-immediate instructions that access
  // the stack. Since the offset field is not extendable, if the stack
  // size exceeds the offset limit (6 bits, shifted), the stores will
  // require a new base register.
  bool HasImmStack = false;
  unsigned MinLS = ~0u;   // Log_2 of the memory access size.

  for (const MachineBasicBlock &B : MF) {
    for (const MachineInstr &MI : B) {
      unsigned LS = 0;
      switch (MI.getOpcode()) {
        case Hexagon::S4_storeirit_io:
        case Hexagon::S4_storeirif_io:
        case Hexagon::S4_storeiri_io:
          ++LS;
          [[fallthrough]];
        case Hexagon::S4_storeirht_io:
        case Hexagon::S4_storeirhf_io:
        case Hexagon::S4_storeirh_io:
          ++LS;
          [[fallthrough]];
        case Hexagon::S4_storeirbt_io:
        case Hexagon::S4_storeirbf_io:
        case Hexagon::S4_storeirb_io:
          if (MI.getOperand(0).isFI())
            HasImmStack = true;
          MinLS = std::min(MinLS, LS);
          break;
      }
    }
  }

  if (HasImmStack)
    return !isUInt<6>(StackSize >> MinLS);

  return false;
}

namespace {
// Struct used by orderFrameObjects to help sort the stack objects.
struct HexagonFrameSortingObject {
  bool IsValid = false;
  unsigned Index = 0; // Index of Object into MFI list.
  unsigned Size = 0;
  Align ObjectAlignment = Align(1); // Alignment of Object in bytes.
};

struct HexagonFrameSortingComparator {
  inline bool operator()(const HexagonFrameSortingObject &A,
                         const HexagonFrameSortingObject &B) const {
    return std::make_tuple(!A.IsValid, A.ObjectAlignment, A.Size) <
           std::make_tuple(!B.IsValid, B.ObjectAlignment, B.Size);
  }
};
} // namespace

// Sort objects on the stack by alignment value and then by size to minimize
// padding.
void HexagonFrameLowering::orderFrameObjects(
    const MachineFunction &MF, SmallVectorImpl<int> &ObjectsToAllocate) const {

  if (ObjectsToAllocate.empty())
    return;

  const MachineFrameInfo &MFI = MF.getFrameInfo();
  int NObjects = ObjectsToAllocate.size();

  // Create an array of all MFI objects.
  SmallVector<HexagonFrameSortingObject> SortingObjects(
      MFI.getObjectIndexEnd());

  for (int i = 0, j = 0, e = MFI.getObjectIndexEnd(); i < e && j != NObjects;
       ++i) {
    if (i != ObjectsToAllocate[j])
      continue;
    j++;

    // A variable size object has size equal to 0. Since Hexagon sets
    // getUseLocalStackAllocationBlock() to true, a local block is allocated
    // earlier. This case is not handled here for now.
    int Size = MFI.getObjectSize(i);
    if (Size == 0)
      return;

    SortingObjects[i].IsValid = true;
    SortingObjects[i].Index = i;
    SortingObjects[i].Size = Size;
    SortingObjects[i].ObjectAlignment = MFI.getObjectAlign(i);
  }

  // Sort objects by alignment and then by size.
  llvm::stable_sort(SortingObjects, HexagonFrameSortingComparator());

  // Modify the original list to represent the final order.
  int i = NObjects;
  for (auto &Obj : SortingObjects) {
    if (i == 0)
      break;
    ObjectsToAllocate[--i] = Obj.Index;
  }
}
