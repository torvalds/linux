//===- AArch64LowerHomogeneousPrologEpilog.cpp ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a pass that lowers homogeneous prolog/epilog instructions.
//
//===----------------------------------------------------------------------===//

#include "AArch64InstrInfo.h"
#include "AArch64Subtarget.h"
#include "MCTargetDesc/AArch64InstPrinter.h"
#include "Utils/AArch64BaseInfo.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>
#include <sstream>

using namespace llvm;

#define AARCH64_LOWER_HOMOGENEOUS_PROLOG_EPILOG_NAME                           \
  "AArch64 homogeneous prolog/epilog lowering pass"

cl::opt<int> FrameHelperSizeThreshold(
    "frame-helper-size-threshold", cl::init(2), cl::Hidden,
    cl::desc("The minimum number of instructions that are outlined in a frame "
             "helper (default = 2)"));

namespace {

class AArch64LowerHomogeneousPE {
public:
  const AArch64InstrInfo *TII;

  AArch64LowerHomogeneousPE(Module *M, MachineModuleInfo *MMI)
      : M(M), MMI(MMI) {}

  bool run();
  bool runOnMachineFunction(MachineFunction &Fn);

private:
  Module *M;
  MachineModuleInfo *MMI;

  bool runOnMBB(MachineBasicBlock &MBB);
  bool runOnMI(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
               MachineBasicBlock::iterator &NextMBBI);

  /// Lower a HOM_Prolog pseudo instruction into a helper call
  /// or a sequence of homogeneous stores.
  /// When a fp setup follows, it can be optimized.
  bool lowerProlog(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                   MachineBasicBlock::iterator &NextMBBI);
  /// Lower a HOM_Epilog pseudo instruction into a helper call
  /// or a sequence of homogeneous loads.
  /// When a return follow, it can be optimized.
  bool lowerEpilog(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                   MachineBasicBlock::iterator &NextMBBI);
};

class AArch64LowerHomogeneousPrologEpilog : public ModulePass {
public:
  static char ID;

  AArch64LowerHomogeneousPrologEpilog() : ModulePass(ID) {
    initializeAArch64LowerHomogeneousPrologEpilogPass(
        *PassRegistry::getPassRegistry());
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineModuleInfoWrapperPass>();
    AU.addPreserved<MachineModuleInfoWrapperPass>();
    AU.setPreservesAll();
    ModulePass::getAnalysisUsage(AU);
  }
  bool runOnModule(Module &M) override;

  StringRef getPassName() const override {
    return AARCH64_LOWER_HOMOGENEOUS_PROLOG_EPILOG_NAME;
  }
};

} // end anonymous namespace

char AArch64LowerHomogeneousPrologEpilog::ID = 0;

INITIALIZE_PASS(AArch64LowerHomogeneousPrologEpilog,
                "aarch64-lower-homogeneous-prolog-epilog",
                AARCH64_LOWER_HOMOGENEOUS_PROLOG_EPILOG_NAME, false, false)

bool AArch64LowerHomogeneousPrologEpilog::runOnModule(Module &M) {
  if (skipModule(M))
    return false;

  MachineModuleInfo *MMI =
      &getAnalysis<MachineModuleInfoWrapperPass>().getMMI();
  return AArch64LowerHomogeneousPE(&M, MMI).run();
}

bool AArch64LowerHomogeneousPE::run() {
  bool Changed = false;
  for (auto &F : *M) {
    if (F.empty())
      continue;

    MachineFunction *MF = MMI->getMachineFunction(F);
    if (!MF)
      continue;
    Changed |= runOnMachineFunction(*MF);
  }

  return Changed;
}
enum FrameHelperType { Prolog, PrologFrame, Epilog, EpilogTail };

/// Return a frame helper name with the given CSRs and the helper type.
/// For instance, a prolog helper that saves x19 and x20 is named as
/// OUTLINED_FUNCTION_PROLOG_x19x20.
static std::string getFrameHelperName(SmallVectorImpl<unsigned> &Regs,
                                      FrameHelperType Type, unsigned FpOffset) {
  std::ostringstream RegStream;
  switch (Type) {
  case FrameHelperType::Prolog:
    RegStream << "OUTLINED_FUNCTION_PROLOG_";
    break;
  case FrameHelperType::PrologFrame:
    RegStream << "OUTLINED_FUNCTION_PROLOG_FRAME" << FpOffset << "_";
    break;
  case FrameHelperType::Epilog:
    RegStream << "OUTLINED_FUNCTION_EPILOG_";
    break;
  case FrameHelperType::EpilogTail:
    RegStream << "OUTLINED_FUNCTION_EPILOG_TAIL_";
    break;
  }

  for (auto Reg : Regs) {
    if (Reg == AArch64::NoRegister)
      continue;
    RegStream << AArch64InstPrinter::getRegisterName(Reg);
  }

  return RegStream.str();
}

/// Create a Function for the unique frame helper with the given name.
/// Return a newly created MachineFunction with an empty MachineBasicBlock.
static MachineFunction &createFrameHelperMachineFunction(Module *M,
                                                         MachineModuleInfo *MMI,
                                                         StringRef Name) {
  LLVMContext &C = M->getContext();
  Function *F = M->getFunction(Name);
  assert(F == nullptr && "Function has been created before");
  F = Function::Create(FunctionType::get(Type::getVoidTy(C), false),
                       Function::ExternalLinkage, Name, M);
  assert(F && "Function was null!");

  // Use ODR linkage to avoid duplication.
  F->setLinkage(GlobalValue::LinkOnceODRLinkage);
  F->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);

  // Set no-opt/minsize, so we don't insert padding between outlined
  // functions.
  F->addFnAttr(Attribute::OptimizeNone);
  F->addFnAttr(Attribute::NoInline);
  F->addFnAttr(Attribute::MinSize);
  F->addFnAttr(Attribute::Naked);

  MachineFunction &MF = MMI->getOrCreateMachineFunction(*F);
  // Remove unnecessary register liveness and set NoVRegs.
  MF.getProperties().reset(MachineFunctionProperties::Property::TracksLiveness);
  MF.getProperties().reset(MachineFunctionProperties::Property::IsSSA);
  MF.getProperties().set(MachineFunctionProperties::Property::NoVRegs);
  MF.getRegInfo().freezeReservedRegs();

  // Create entry block.
  BasicBlock *EntryBB = BasicBlock::Create(C, "entry", F);
  IRBuilder<> Builder(EntryBB);
  Builder.CreateRetVoid();

  // Insert the new block into the function.
  MachineBasicBlock *MBB = MF.CreateMachineBasicBlock();
  MF.insert(MF.begin(), MBB);

  return MF;
}

/// Emit a store-pair instruction for frame-setup.
/// If Reg2 is AArch64::NoRegister, emit STR instead.
static void emitStore(MachineFunction &MF, MachineBasicBlock &MBB,
                      MachineBasicBlock::iterator Pos,
                      const TargetInstrInfo &TII, unsigned Reg1, unsigned Reg2,
                      int Offset, bool IsPreDec) {
  assert(Reg1 != AArch64::NoRegister);
  const bool IsPaired = Reg2 != AArch64::NoRegister;
  bool IsFloat = AArch64::FPR64RegClass.contains(Reg1);
  assert(!(IsFloat ^ AArch64::FPR64RegClass.contains(Reg2)));
  unsigned Opc;
  if (IsPreDec) {
    if (IsFloat)
      Opc = IsPaired ? AArch64::STPDpre : AArch64::STRDpre;
    else
      Opc = IsPaired ? AArch64::STPXpre : AArch64::STRXpre;
  } else {
    if (IsFloat)
      Opc = IsPaired ? AArch64::STPDi : AArch64::STRDui;
    else
      Opc = IsPaired ? AArch64::STPXi : AArch64::STRXui;
  }
  // The implicit scale for Offset is 8.
  TypeSize Scale(0U, false), Width(0U, false);
  int64_t MinOffset, MaxOffset;
  [[maybe_unused]] bool Success =
      AArch64InstrInfo::getMemOpInfo(Opc, Scale, Width, MinOffset, MaxOffset);
  assert(Success && "Invalid Opcode");
  Offset *= (8 / (int)Scale);

  MachineInstrBuilder MIB = BuildMI(MBB, Pos, DebugLoc(), TII.get(Opc));
  if (IsPreDec)
    MIB.addDef(AArch64::SP);
  if (IsPaired)
    MIB.addReg(Reg2);
  MIB.addReg(Reg1)
      .addReg(AArch64::SP)
      .addImm(Offset)
      .setMIFlag(MachineInstr::FrameSetup);
}

/// Emit a load-pair instruction for frame-destroy.
/// If Reg2 is AArch64::NoRegister, emit LDR instead.
static void emitLoad(MachineFunction &MF, MachineBasicBlock &MBB,
                     MachineBasicBlock::iterator Pos,
                     const TargetInstrInfo &TII, unsigned Reg1, unsigned Reg2,
                     int Offset, bool IsPostDec) {
  assert(Reg1 != AArch64::NoRegister);
  const bool IsPaired = Reg2 != AArch64::NoRegister;
  bool IsFloat = AArch64::FPR64RegClass.contains(Reg1);
  assert(!(IsFloat ^ AArch64::FPR64RegClass.contains(Reg2)));
  unsigned Opc;
  if (IsPostDec) {
    if (IsFloat)
      Opc = IsPaired ? AArch64::LDPDpost : AArch64::LDRDpost;
    else
      Opc = IsPaired ? AArch64::LDPXpost : AArch64::LDRXpost;
  } else {
    if (IsFloat)
      Opc = IsPaired ? AArch64::LDPDi : AArch64::LDRDui;
    else
      Opc = IsPaired ? AArch64::LDPXi : AArch64::LDRXui;
  }
  // The implicit scale for Offset is 8.
  TypeSize Scale(0U, false), Width(0U, false);
  int64_t MinOffset, MaxOffset;
  [[maybe_unused]] bool Success =
      AArch64InstrInfo::getMemOpInfo(Opc, Scale, Width, MinOffset, MaxOffset);
  assert(Success && "Invalid Opcode");
  Offset *= (8 / (int)Scale);

  MachineInstrBuilder MIB = BuildMI(MBB, Pos, DebugLoc(), TII.get(Opc));
  if (IsPostDec)
    MIB.addDef(AArch64::SP);
  if (IsPaired)
    MIB.addReg(Reg2, getDefRegState(true));
  MIB.addReg(Reg1, getDefRegState(true))
      .addReg(AArch64::SP)
      .addImm(Offset)
      .setMIFlag(MachineInstr::FrameDestroy);
}

/// Return a unique function if a helper can be formed with the given Regs
/// and frame type.
/// 1) _OUTLINED_FUNCTION_PROLOG_x30x29x19x20x21x22:
///    stp x22, x21, [sp, #-32]!    ; x29/x30 has been stored at the caller
///    stp x20, x19, [sp, #16]
///    ret
///
/// 2) _OUTLINED_FUNCTION_PROLOG_FRAME32_x30x29x19x20x21x22:
///    stp x22, x21, [sp, #-32]!    ; x29/x30 has been stored at the caller
///    stp x20, x19, [sp, #16]
///    add fp, sp, #32
///    ret
///
/// 3) _OUTLINED_FUNCTION_EPILOG_x30x29x19x20x21x22:
///    mov x16, x30
///    ldp x29, x30, [sp, #32]
///    ldp x20, x19, [sp, #16]
///    ldp x22, x21, [sp], #48
///    ret x16
///
/// 4) _OUTLINED_FUNCTION_EPILOG_TAIL_x30x29x19x20x21x22:
///    ldp x29, x30, [sp, #32]
///    ldp x20, x19, [sp, #16]
///    ldp x22, x21, [sp], #48
///    ret
/// @param M module
/// @param MMI machine module info
/// @param Regs callee save regs that the helper will handle
/// @param Type frame helper type
/// @return a helper function
static Function *getOrCreateFrameHelper(Module *M, MachineModuleInfo *MMI,
                                        SmallVectorImpl<unsigned> &Regs,
                                        FrameHelperType Type,
                                        unsigned FpOffset = 0) {
  assert(Regs.size() >= 2);
  auto Name = getFrameHelperName(Regs, Type, FpOffset);
  auto *F = M->getFunction(Name);
  if (F)
    return F;

  auto &MF = createFrameHelperMachineFunction(M, MMI, Name);
  MachineBasicBlock &MBB = *MF.begin();
  const TargetSubtargetInfo &STI = MF.getSubtarget();
  const TargetInstrInfo &TII = *STI.getInstrInfo();

  int Size = (int)Regs.size();
  switch (Type) {
  case FrameHelperType::Prolog:
  case FrameHelperType::PrologFrame: {
    // Compute the remaining SP adjust beyond FP/LR.
    auto LRIdx = std::distance(Regs.begin(), llvm::find(Regs, AArch64::LR));

    // If the register stored to the lowest address is not LR, we must subtract
    // more from SP here.
    if (LRIdx != Size - 2) {
      assert(Regs[Size - 2] != AArch64::LR);
      emitStore(MF, MBB, MBB.end(), TII, Regs[Size - 2], Regs[Size - 1],
                LRIdx - Size + 2, true);
    }

    // Store CSRs in the reverse order.
    for (int I = Size - 3; I >= 0; I -= 2) {
      // FP/LR has been stored at call-site.
      if (Regs[I - 1] == AArch64::LR)
        continue;
      emitStore(MF, MBB, MBB.end(), TII, Regs[I - 1], Regs[I], Size - I - 1,
                false);
    }
    if (Type == FrameHelperType::PrologFrame)
      BuildMI(MBB, MBB.end(), DebugLoc(), TII.get(AArch64::ADDXri))
          .addDef(AArch64::FP)
          .addUse(AArch64::SP)
          .addImm(FpOffset)
          .addImm(0)
          .setMIFlag(MachineInstr::FrameSetup);

    BuildMI(MBB, MBB.end(), DebugLoc(), TII.get(AArch64::RET))
        .addReg(AArch64::LR);
    break;
  }
  case FrameHelperType::Epilog:
  case FrameHelperType::EpilogTail:
    if (Type == FrameHelperType::Epilog)
      // Stash LR to X16
      BuildMI(MBB, MBB.end(), DebugLoc(), TII.get(AArch64::ORRXrs))
          .addDef(AArch64::X16)
          .addReg(AArch64::XZR)
          .addUse(AArch64::LR)
          .addImm(0);

    for (int I = 0; I < Size - 2; I += 2)
      emitLoad(MF, MBB, MBB.end(), TII, Regs[I], Regs[I + 1], Size - I - 2,
               false);
    // Restore the last CSR with post-increment of SP.
    emitLoad(MF, MBB, MBB.end(), TII, Regs[Size - 2], Regs[Size - 1], Size,
             true);

    BuildMI(MBB, MBB.end(), DebugLoc(), TII.get(AArch64::RET))
        .addReg(Type == FrameHelperType::Epilog ? AArch64::X16 : AArch64::LR);
    break;
  }

  return M->getFunction(Name);
}

/// This function checks if a frame helper should be used for
/// HOM_Prolog/HOM_Epilog pseudo instruction expansion.
/// @param MBB machine basic block
/// @param NextMBBI  next instruction following HOM_Prolog/HOM_Epilog
/// @param Regs callee save registers that are saved or restored.
/// @param Type frame helper type
/// @return True if a use of helper is qualified.
static bool shouldUseFrameHelper(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator &NextMBBI,
                                 SmallVectorImpl<unsigned> &Regs,
                                 FrameHelperType Type) {
  const auto *TRI = MBB.getParent()->getSubtarget().getRegisterInfo();
  auto RegCount = Regs.size();
  assert(RegCount > 0 && (RegCount % 2 == 0));
  // # of instructions that will be outlined.
  int InstCount = RegCount / 2;

  // Do not use a helper call when not saving LR.
  if (!llvm::is_contained(Regs, AArch64::LR))
    return false;

  switch (Type) {
  case FrameHelperType::Prolog:
    // Prolog helper cannot save FP/LR.
    InstCount--;
    break;
  case FrameHelperType::PrologFrame: {
    // Effecitvely no change in InstCount since FpAdjusment is included.
    break;
  }
  case FrameHelperType::Epilog:
    // Bail-out if X16 is live across the epilog helper because it is used in
    // the helper to handle X30.
    for (auto NextMI = NextMBBI; NextMI != MBB.end(); NextMI++) {
      if (NextMI->readsRegister(AArch64::W16, TRI))
        return false;
    }
    // Epilog may not be in the last block. Check the liveness in successors.
    for (const MachineBasicBlock *SuccMBB : MBB.successors()) {
      if (SuccMBB->isLiveIn(AArch64::W16) || SuccMBB->isLiveIn(AArch64::X16))
        return false;
    }
    // No change in InstCount for the regular epilog case.
    break;
  case FrameHelperType::EpilogTail: {
    // EpilogTail helper includes the caller's return.
    if (NextMBBI == MBB.end())
      return false;
    if (NextMBBI->getOpcode() != AArch64::RET_ReallyLR)
      return false;
    InstCount++;
    break;
  }
  }

  return InstCount >= FrameHelperSizeThreshold;
}

/// Lower a HOM_Epilog pseudo instruction into a helper call while
/// creating the helper on demand. Or emit a sequence of loads in place when not
/// using a helper call.
///
/// 1. With a helper including ret
///    HOM_Epilog x30, x29, x19, x20, x21, x22              ; MBBI
///    ret                                                  ; NextMBBI
///    =>
///    b _OUTLINED_FUNCTION_EPILOG_TAIL_x30x29x19x20x21x22
///    ...                                                  ; NextMBBI
///
/// 2. With a helper
///    HOM_Epilog x30, x29, x19, x20, x21, x22
///    =>
///    bl _OUTLINED_FUNCTION_EPILOG_x30x29x19x20x21x22
///
/// 3. Without a helper
///    HOM_Epilog x30, x29, x19, x20, x21, x22
///    =>
///    ldp x29, x30, [sp, #32]
///    ldp x20, x19, [sp, #16]
///    ldp x22, x21, [sp], #48
bool AArch64LowerHomogeneousPE::lowerEpilog(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    MachineBasicBlock::iterator &NextMBBI) {
  auto &MF = *MBB.getParent();
  MachineInstr &MI = *MBBI;

  DebugLoc DL = MI.getDebugLoc();
  SmallVector<unsigned, 8> Regs;
  bool HasUnpairedReg = false;
  for (auto &MO : MI.operands())
    if (MO.isReg()) {
      if (!MO.getReg().isValid()) {
        // For now we are only expecting unpaired GP registers which should
        // occur exactly once.
        assert(!HasUnpairedReg);
        HasUnpairedReg = true;
      }
      Regs.push_back(MO.getReg());
    }
  (void)HasUnpairedReg;
  int Size = (int)Regs.size();
  if (Size == 0)
    return false;
  // Registers are in pair.
  assert(Size % 2 == 0);
  assert(MI.getOpcode() == AArch64::HOM_Epilog);

  auto Return = NextMBBI;
  if (shouldUseFrameHelper(MBB, NextMBBI, Regs, FrameHelperType::EpilogTail)) {
    // When MBB ends with a return, emit a tail-call to the epilog helper
    auto *EpilogTailHelper =
        getOrCreateFrameHelper(M, MMI, Regs, FrameHelperType::EpilogTail);
    BuildMI(MBB, MBBI, DL, TII->get(AArch64::TCRETURNdi))
        .addGlobalAddress(EpilogTailHelper)
        .addImm(0)
        .setMIFlag(MachineInstr::FrameDestroy)
        .copyImplicitOps(MI)
        .copyImplicitOps(*Return);
    NextMBBI = std::next(Return);
    Return->removeFromParent();
  } else if (shouldUseFrameHelper(MBB, NextMBBI, Regs,
                                  FrameHelperType::Epilog)) {
    // The default epilog helper case.
    auto *EpilogHelper =
        getOrCreateFrameHelper(M, MMI, Regs, FrameHelperType::Epilog);
    BuildMI(MBB, MBBI, DL, TII->get(AArch64::BL))
        .addGlobalAddress(EpilogHelper)
        .setMIFlag(MachineInstr::FrameDestroy)
        .copyImplicitOps(MI);
  } else {
    // Fall back to no-helper.
    for (int I = 0; I < Size - 2; I += 2)
      emitLoad(MF, MBB, MBBI, *TII, Regs[I], Regs[I + 1], Size - I - 2, false);
    // Restore the last CSR with post-increment of SP.
    emitLoad(MF, MBB, MBBI, *TII, Regs[Size - 2], Regs[Size - 1], Size, true);
  }

  MBBI->removeFromParent();
  return true;
}

/// Lower a HOM_Prolog pseudo instruction into a helper call while
/// creating the helper on demand. Or emit a sequence of stores in place when
/// not using a helper call.
///
/// 1. With a helper including frame-setup
///    HOM_Prolog x30, x29, x19, x20, x21, x22, 32
///    =>
///    stp x29, x30, [sp, #-16]!
///    bl _OUTLINED_FUNCTION_PROLOG_FRAME32_x30x29x19x20x21x22
///
/// 2. With a helper
///    HOM_Prolog x30, x29, x19, x20, x21, x22
///    =>
///    stp x29, x30, [sp, #-16]!
///    bl _OUTLINED_FUNCTION_PROLOG_x30x29x19x20x21x22
///
/// 3. Without a helper
///    HOM_Prolog x30, x29, x19, x20, x21, x22
///    =>
///    stp	x22, x21, [sp, #-48]!
///    stp	x20, x19, [sp, #16]
///    stp	x29, x30, [sp, #32]
bool AArch64LowerHomogeneousPE::lowerProlog(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    MachineBasicBlock::iterator &NextMBBI) {
  auto &MF = *MBB.getParent();
  MachineInstr &MI = *MBBI;

  DebugLoc DL = MI.getDebugLoc();
  SmallVector<unsigned, 8> Regs;
  bool HasUnpairedReg = false;
  int LRIdx = 0;
  std::optional<int> FpOffset;
  for (auto &MO : MI.operands()) {
    if (MO.isReg()) {
      if (MO.getReg().isValid()) {
        if (MO.getReg() == AArch64::LR)
          LRIdx = Regs.size();
      } else {
        // For now we are only expecting unpaired GP registers which should
        // occur exactly once.
        assert(!HasUnpairedReg);
        HasUnpairedReg = true;
      }
      Regs.push_back(MO.getReg());
    } else if (MO.isImm()) {
      FpOffset = MO.getImm();
    }
  }
  (void)HasUnpairedReg;
  int Size = (int)Regs.size();
  if (Size == 0)
    return false;
  // Allow compact unwind case only for oww.
  assert(Size % 2 == 0);
  assert(MI.getOpcode() == AArch64::HOM_Prolog);

  if (FpOffset &&
      shouldUseFrameHelper(MBB, NextMBBI, Regs, FrameHelperType::PrologFrame)) {
    // FP/LR is stored at the top of stack before the prolog helper call.
    emitStore(MF, MBB, MBBI, *TII, AArch64::LR, AArch64::FP, -LRIdx - 2, true);
    auto *PrologFrameHelper = getOrCreateFrameHelper(
        M, MMI, Regs, FrameHelperType::PrologFrame, *FpOffset);
    BuildMI(MBB, MBBI, DL, TII->get(AArch64::BL))
        .addGlobalAddress(PrologFrameHelper)
        .setMIFlag(MachineInstr::FrameSetup)
        .copyImplicitOps(MI)
        .addReg(AArch64::FP, RegState::Implicit | RegState::Define)
        .addReg(AArch64::SP, RegState::Implicit);
  } else if (!FpOffset && shouldUseFrameHelper(MBB, NextMBBI, Regs,
                                               FrameHelperType::Prolog)) {
    // FP/LR is stored at the top of stack before the prolog helper call.
    emitStore(MF, MBB, MBBI, *TII, AArch64::LR, AArch64::FP, -LRIdx - 2, true);
    auto *PrologHelper =
        getOrCreateFrameHelper(M, MMI, Regs, FrameHelperType::Prolog);
    BuildMI(MBB, MBBI, DL, TII->get(AArch64::BL))
        .addGlobalAddress(PrologHelper)
        .setMIFlag(MachineInstr::FrameSetup)
        .copyImplicitOps(MI);
  } else {
    // Fall back to no-helper.
    emitStore(MF, MBB, MBBI, *TII, Regs[Size - 2], Regs[Size - 1], -Size, true);
    for (int I = Size - 3; I >= 0; I -= 2)
      emitStore(MF, MBB, MBBI, *TII, Regs[I - 1], Regs[I], Size - I - 1, false);
    if (FpOffset) {
      BuildMI(MBB, MBBI, DL, TII->get(AArch64::ADDXri))
          .addDef(AArch64::FP)
          .addUse(AArch64::SP)
          .addImm(*FpOffset)
          .addImm(0)
          .setMIFlag(MachineInstr::FrameSetup);
    }
  }

  MBBI->removeFromParent();
  return true;
}

/// Process each machine instruction
/// @param MBB machine basic block
/// @param MBBI current instruction iterator
/// @param NextMBBI next instruction iterator which can be updated
/// @return True when IR is changed.
bool AArch64LowerHomogeneousPE::runOnMI(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator MBBI,
                                        MachineBasicBlock::iterator &NextMBBI) {
  MachineInstr &MI = *MBBI;
  unsigned Opcode = MI.getOpcode();
  switch (Opcode) {
  default:
    break;
  case AArch64::HOM_Prolog:
    return lowerProlog(MBB, MBBI, NextMBBI);
  case AArch64::HOM_Epilog:
    return lowerEpilog(MBB, MBBI, NextMBBI);
  }
  return false;
}

bool AArch64LowerHomogeneousPE::runOnMBB(MachineBasicBlock &MBB) {
  bool Modified = false;

  MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
  while (MBBI != E) {
    MachineBasicBlock::iterator NMBBI = std::next(MBBI);
    Modified |= runOnMI(MBB, MBBI, NMBBI);
    MBBI = NMBBI;
  }

  return Modified;
}

bool AArch64LowerHomogeneousPE::runOnMachineFunction(MachineFunction &MF) {
  TII = static_cast<const AArch64InstrInfo *>(MF.getSubtarget().getInstrInfo());

  bool Modified = false;
  for (auto &MBB : MF)
    Modified |= runOnMBB(MBB);
  return Modified;
}

ModulePass *llvm::createAArch64LowerHomogeneousPrologEpilogPass() {
  return new AArch64LowerHomogeneousPrologEpilog();
}
