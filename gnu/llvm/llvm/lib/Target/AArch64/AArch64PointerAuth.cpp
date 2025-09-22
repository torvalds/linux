//===-- AArch64PointerAuth.cpp -- Harden code using PAuth ------------------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AArch64PointerAuth.h"

#include "AArch64.h"
#include "AArch64InstrInfo.h"
#include "AArch64MachineFunctionInfo.h"
#include "AArch64Subtarget.h"
#include "Utils/AArch64BaseInfo.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"

using namespace llvm;
using namespace llvm::AArch64PAuth;

#define AARCH64_POINTER_AUTH_NAME "AArch64 Pointer Authentication"

namespace {

class AArch64PointerAuth : public MachineFunctionPass {
public:
  static char ID;

  AArch64PointerAuth() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override { return AARCH64_POINTER_AUTH_NAME; }

private:
  /// An immediate operand passed to BRK instruction, if it is ever emitted.
  static unsigned BrkOperandForKey(AArch64PACKey::ID KeyId) {
    const unsigned BrkOperandBase = 0xc470;
    return BrkOperandBase + KeyId;
  }

  const AArch64Subtarget *Subtarget = nullptr;
  const AArch64InstrInfo *TII = nullptr;
  const AArch64RegisterInfo *TRI = nullptr;

  void signLR(MachineFunction &MF, MachineBasicBlock::iterator MBBI) const;

  void authenticateLR(MachineFunction &MF,
                      MachineBasicBlock::iterator MBBI) const;

  /// Stores blend(AddrDisc, IntDisc) to the Result register.
  void emitBlend(MachineBasicBlock::iterator MBBI, Register Result,
                 Register AddrDisc, unsigned IntDisc) const;

  /// Expands PAUTH_BLEND pseudo instruction.
  void expandPAuthBlend(MachineBasicBlock::iterator MBBI) const;

  bool checkAuthenticatedLR(MachineBasicBlock::iterator TI) const;
};

} // end anonymous namespace

INITIALIZE_PASS(AArch64PointerAuth, "aarch64-ptrauth",
                AARCH64_POINTER_AUTH_NAME, false, false)

FunctionPass *llvm::createAArch64PointerAuthPass() {
  return new AArch64PointerAuth();
}

char AArch64PointerAuth::ID = 0;

// Where PAuthLR support is not known at compile time, it is supported using
// PACM. PACM is in the hint space so has no effect when PAuthLR is not
// supported by the hardware, but will alter the behaviour of PACI*SP, AUTI*SP
// and RETAA/RETAB if the hardware supports PAuthLR.
static void BuildPACM(const AArch64Subtarget &Subtarget, MachineBasicBlock &MBB,
                      MachineBasicBlock::iterator MBBI, DebugLoc DL,
                      MachineInstr::MIFlag Flags, MCSymbol *PACSym = nullptr) {
  const TargetInstrInfo *TII = Subtarget.getInstrInfo();
  auto &MFnI = *MBB.getParent()->getInfo<AArch64FunctionInfo>();

  // ADR X16,<address_of_PACIASP>
  if (PACSym) {
    assert(Flags == MachineInstr::FrameDestroy);
    BuildMI(MBB, MBBI, DL, TII->get(AArch64::ADR))
        .addReg(AArch64::X16, RegState::Define)
        .addSym(PACSym);
  }

  // Only emit PACM if -mbranch-protection has +pc and the target does not
  // have feature +pauth-lr.
  if (MFnI.branchProtectionPAuthLR() && !Subtarget.hasPAuthLR())
    BuildMI(MBB, MBBI, DL, TII->get(AArch64::PACM)).setMIFlag(Flags);
}

void AArch64PointerAuth::signLR(MachineFunction &MF,
                                MachineBasicBlock::iterator MBBI) const {
  auto &MFnI = *MF.getInfo<AArch64FunctionInfo>();
  bool UseBKey = MFnI.shouldSignWithBKey();
  bool EmitCFI = MFnI.needsDwarfUnwindInfo(MF);
  bool EmitAsyncCFI = MFnI.needsAsyncDwarfUnwindInfo(MF);
  bool NeedsWinCFI = MF.hasWinCFI();

  MachineBasicBlock &MBB = *MBBI->getParent();

  // Debug location must be unknown, see AArch64FrameLowering::emitPrologue.
  DebugLoc DL;

  if (UseBKey) {
    BuildMI(MBB, MBBI, DL, TII->get(AArch64::EMITBKEY))
        .setMIFlag(MachineInstr::FrameSetup);
  }

  // PAuthLR authentication instructions need to know the value of PC at the
  // point of signing (PACI*).
  if (MFnI.branchProtectionPAuthLR()) {
    MCSymbol *PACSym = MF.getContext().createTempSymbol();
    MFnI.setSigningInstrLabel(PACSym);
  }

  // No SEH opcode for this one; it doesn't materialize into an
  // instruction on Windows.
  if (MFnI.branchProtectionPAuthLR() && Subtarget->hasPAuthLR()) {
    BuildMI(MBB, MBBI, DL,
            TII->get(MFnI.shouldSignWithBKey() ? AArch64::PACIBSPPC
                                               : AArch64::PACIASPPC))
        .setMIFlag(MachineInstr::FrameSetup)
        ->setPreInstrSymbol(MF, MFnI.getSigningInstrLabel());
  } else {
    BuildPACM(*Subtarget, MBB, MBBI, DL, MachineInstr::FrameSetup);
    BuildMI(MBB, MBBI, DL,
            TII->get(MFnI.shouldSignWithBKey() ? AArch64::PACIBSP
                                               : AArch64::PACIASP))
        .setMIFlag(MachineInstr::FrameSetup)
        ->setPreInstrSymbol(MF, MFnI.getSigningInstrLabel());
  }

  if (EmitCFI) {
    if (!EmitAsyncCFI) {
      // Reduce the size of the generated call frame information for synchronous
      // CFI by bundling the new CFI instruction with others in the prolog, so
      // that no additional DW_CFA_advance_loc is needed.
      for (auto I = MBBI; I != MBB.end(); ++I) {
        if (I->getOpcode() == TargetOpcode::CFI_INSTRUCTION &&
            I->getFlag(MachineInstr::FrameSetup)) {
          MBBI = I;
          break;
        }
      }
    }
    unsigned CFIIndex =
        MF.addFrameInst(MCCFIInstruction::createNegateRAState(nullptr));
    BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex)
        .setMIFlags(MachineInstr::FrameSetup);
  } else if (NeedsWinCFI) {
    BuildMI(MBB, MBBI, DL, TII->get(AArch64::SEH_PACSignLR))
        .setMIFlag(MachineInstr::FrameSetup);
  }
}

void AArch64PointerAuth::authenticateLR(
    MachineFunction &MF, MachineBasicBlock::iterator MBBI) const {
  const AArch64FunctionInfo *MFnI = MF.getInfo<AArch64FunctionInfo>();
  bool UseBKey = MFnI->shouldSignWithBKey();
  bool EmitAsyncCFI = MFnI->needsAsyncDwarfUnwindInfo(MF);
  bool NeedsWinCFI = MF.hasWinCFI();

  MachineBasicBlock &MBB = *MBBI->getParent();
  DebugLoc DL = MBBI->getDebugLoc();
  // MBBI points to a PAUTH_EPILOGUE instruction to be replaced and
  // TI points to a terminator instruction that may or may not be combined.
  // Note that inserting new instructions "before MBBI" and "before TI" is
  // not the same because if ShadowCallStack is enabled, its instructions
  // are placed between MBBI and TI.
  MachineBasicBlock::iterator TI = MBB.getFirstInstrTerminator();

  // The AUTIASP instruction assembles to a hint instruction before v8.3a so
  // this instruction can safely used for any v8a architecture.
  // From v8.3a onwards there are optimised authenticate LR and return
  // instructions, namely RETA{A,B}, that can be used instead. In this case the
  // DW_CFA_AARCH64_negate_ra_state can't be emitted.
  bool TerminatorIsCombinable =
      TI != MBB.end() && TI->getOpcode() == AArch64::RET;
  MCSymbol *PACSym = MFnI->getSigningInstrLabel();

  if (Subtarget->hasPAuth() && TerminatorIsCombinable && !NeedsWinCFI &&
      !MF.getFunction().hasFnAttribute(Attribute::ShadowCallStack)) {
    if (MFnI->branchProtectionPAuthLR() && Subtarget->hasPAuthLR()) {
      assert(PACSym && "No PAC instruction to refer to");
      BuildMI(MBB, TI, DL,
              TII->get(UseBKey ? AArch64::RETABSPPCi : AArch64::RETAASPPCi))
          .addSym(PACSym)
          .copyImplicitOps(*MBBI)
          .setMIFlag(MachineInstr::FrameDestroy);
    } else {
      BuildPACM(*Subtarget, MBB, TI, DL, MachineInstr::FrameDestroy, PACSym);
      BuildMI(MBB, TI, DL, TII->get(UseBKey ? AArch64::RETAB : AArch64::RETAA))
          .copyImplicitOps(*MBBI)
          .setMIFlag(MachineInstr::FrameDestroy);
    }
    MBB.erase(TI);
  } else {
    if (MFnI->branchProtectionPAuthLR() && Subtarget->hasPAuthLR()) {
      assert(PACSym && "No PAC instruction to refer to");
      BuildMI(MBB, MBBI, DL,
              TII->get(UseBKey ? AArch64::AUTIBSPPCi : AArch64::AUTIASPPCi))
          .addSym(PACSym)
          .setMIFlag(MachineInstr::FrameDestroy);
    } else {
      BuildPACM(*Subtarget, MBB, MBBI, DL, MachineInstr::FrameDestroy, PACSym);
      BuildMI(MBB, MBBI, DL,
              TII->get(UseBKey ? AArch64::AUTIBSP : AArch64::AUTIASP))
          .setMIFlag(MachineInstr::FrameDestroy);
    }

    if (EmitAsyncCFI) {
      unsigned CFIIndex =
          MF.addFrameInst(MCCFIInstruction::createNegateRAState(nullptr));
      BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex)
          .setMIFlags(MachineInstr::FrameDestroy);
    }
    if (NeedsWinCFI) {
      BuildMI(MBB, MBBI, DL, TII->get(AArch64::SEH_PACSignLR))
          .setMIFlag(MachineInstr::FrameDestroy);
    }
  }
}

namespace {

// Mark dummy LDR instruction as volatile to prevent removing it as dead code.
MachineMemOperand *createCheckMemOperand(MachineFunction &MF,
                                         const AArch64Subtarget &Subtarget) {
  MachinePointerInfo PointerInfo(Subtarget.getAddressCheckPSV());
  auto MOVolatileLoad =
      MachineMemOperand::MOLoad | MachineMemOperand::MOVolatile;

  return MF.getMachineMemOperand(PointerInfo, MOVolatileLoad, 4, Align(4));
}

} // namespace

void llvm::AArch64PAuth::checkAuthenticatedRegister(
    MachineBasicBlock::iterator MBBI, AuthCheckMethod Method,
    Register AuthenticatedReg, Register TmpReg, bool UseIKey, unsigned BrkImm) {

  MachineBasicBlock &MBB = *MBBI->getParent();
  MachineFunction &MF = *MBB.getParent();
  const AArch64Subtarget &Subtarget = MF.getSubtarget<AArch64Subtarget>();
  const AArch64InstrInfo *TII = Subtarget.getInstrInfo();
  DebugLoc DL = MBBI->getDebugLoc();

  // All terminator instructions should be grouped at the end of the machine
  // basic block, with no non-terminator instructions between them. Depending on
  // the method requested, we will insert some regular instructions, maybe
  // followed by a conditional branch instruction, which is a terminator, before
  // MBBI. Thus, MBBI is expected to be the first terminator of its MBB.
  assert(MBBI->isTerminator() && MBBI == MBB.getFirstTerminator() &&
         "MBBI should be the first terminator in MBB");

  // First, handle the methods not requiring creating extra MBBs.
  switch (Method) {
  default:
    break;
  case AuthCheckMethod::None:
    return;
  case AuthCheckMethod::DummyLoad:
    BuildMI(MBB, MBBI, DL, TII->get(AArch64::LDRWui), getWRegFromXReg(TmpReg))
        .addReg(AuthenticatedReg)
        .addImm(0)
        .addMemOperand(createCheckMemOperand(MF, Subtarget));
    return;
  }

  // Control flow has to be changed, so arrange new MBBs.

  // The block that explicitly generates a break-point exception on failure.
  MachineBasicBlock *BreakBlock =
      MF.CreateMachineBasicBlock(MBB.getBasicBlock());
  MF.push_back(BreakBlock);
  MBB.addSuccessor(BreakBlock);

  BuildMI(BreakBlock, DL, TII->get(AArch64::BRK)).addImm(BrkImm);

  switch (Method) {
  case AuthCheckMethod::None:
  case AuthCheckMethod::DummyLoad:
    llvm_unreachable("Should be handled above");
  case AuthCheckMethod::HighBitsNoTBI:
    BuildMI(MBB, MBBI, DL, TII->get(AArch64::EORXrs), TmpReg)
        .addReg(AuthenticatedReg)
        .addReg(AuthenticatedReg)
        .addImm(1);
    BuildMI(MBB, MBBI, DL, TII->get(AArch64::TBNZX))
        .addReg(TmpReg)
        .addImm(62)
        .addMBB(BreakBlock);
    return;
  case AuthCheckMethod::XPACHint:
    assert(AuthenticatedReg == AArch64::LR &&
           "XPACHint mode is only compatible with checking the LR register");
    assert(UseIKey && "XPACHint mode is only compatible with I-keys");
    BuildMI(MBB, MBBI, DL, TII->get(AArch64::ORRXrs), TmpReg)
        .addReg(AArch64::XZR)
        .addReg(AArch64::LR)
        .addImm(0);
    BuildMI(MBB, MBBI, DL, TII->get(AArch64::XPACLRI));
    BuildMI(MBB, MBBI, DL, TII->get(AArch64::SUBSXrs), AArch64::XZR)
        .addReg(TmpReg)
        .addReg(AArch64::LR)
        .addImm(0);
    BuildMI(MBB, MBBI, DL, TII->get(AArch64::Bcc))
        .addImm(AArch64CC::NE)
        .addMBB(BreakBlock);
    return;
  }
  llvm_unreachable("Unknown AuthCheckMethod enum");
}

unsigned llvm::AArch64PAuth::getCheckerSizeInBytes(AuthCheckMethod Method) {
  switch (Method) {
  case AuthCheckMethod::None:
    return 0;
  case AuthCheckMethod::DummyLoad:
    return 4;
  case AuthCheckMethod::HighBitsNoTBI:
    return 12;
  case AuthCheckMethod::XPACHint:
    return 20;
  }
  llvm_unreachable("Unknown AuthCheckMethod enum");
}

bool AArch64PointerAuth::checkAuthenticatedLR(
    MachineBasicBlock::iterator TI) const {
  const AArch64FunctionInfo *MFnI = TI->getMF()->getInfo<AArch64FunctionInfo>();
  AArch64PACKey::ID KeyId =
      MFnI->shouldSignWithBKey() ? AArch64PACKey::IB : AArch64PACKey::IA;

  AuthCheckMethod Method =
      Subtarget->getAuthenticatedLRCheckMethod(*TI->getMF());

  if (Method == AuthCheckMethod::None)
    return false;

  // FIXME If FEAT_FPAC is implemented by the CPU, this check can be skipped.

  assert(!TI->getMF()->hasWinCFI() && "WinCFI is not yet supported");

  // The following code may create a signing oracle:
  //
  //   <authenticate LR>
  //   TCRETURN          ; the callee may sign and spill the LR in its prologue
  //
  // To avoid generating a signing oracle, check the authenticated value
  // before possibly re-signing it in the callee, as follows:
  //
  //   <authenticate LR>
  //   <check if LR contains a valid address>
  //   b.<cond> break_block
  // ret_block:
  //   TCRETURN
  // break_block:
  //   brk <BrkOperand>
  //
  // or just
  //
  //   <authenticate LR>
  //   ldr tmp, [lr]
  //   TCRETURN

  // TmpReg is chosen assuming X16 and X17 are dead after TI.
  assert(AArch64InstrInfo::isTailCallReturnInst(*TI) &&
         "Tail call is expected");
  Register TmpReg =
      TI->readsRegister(AArch64::X16, TRI) ? AArch64::X17 : AArch64::X16;
  assert(!TI->readsRegister(TmpReg, TRI) &&
         "More than a single register is used by TCRETURN");

  checkAuthenticatedRegister(TI, Method, AArch64::LR, TmpReg, /*UseIKey=*/true,
                             BrkOperandForKey(KeyId));

  return true;
}

void AArch64PointerAuth::emitBlend(MachineBasicBlock::iterator MBBI,
                                   Register Result, Register AddrDisc,
                                   unsigned IntDisc) const {
  MachineBasicBlock &MBB = *MBBI->getParent();
  DebugLoc DL = MBBI->getDebugLoc();

  if (Result != AddrDisc)
    BuildMI(MBB, MBBI, DL, TII->get(AArch64::ORRXrs), Result)
        .addReg(AArch64::XZR)
        .addReg(AddrDisc)
        .addImm(0);

  BuildMI(MBB, MBBI, DL, TII->get(AArch64::MOVKXi), Result)
      .addReg(Result)
      .addImm(IntDisc)
      .addImm(48);
}

void AArch64PointerAuth::expandPAuthBlend(
    MachineBasicBlock::iterator MBBI) const {
  Register ResultReg = MBBI->getOperand(0).getReg();
  Register AddrDisc = MBBI->getOperand(1).getReg();
  unsigned IntDisc = MBBI->getOperand(2).getImm();
  emitBlend(MBBI, ResultReg, AddrDisc, IntDisc);
}

bool AArch64PointerAuth::runOnMachineFunction(MachineFunction &MF) {
  const auto *MFnI = MF.getInfo<AArch64FunctionInfo>();

  Subtarget = &MF.getSubtarget<AArch64Subtarget>();
  TII = Subtarget->getInstrInfo();
  TRI = Subtarget->getRegisterInfo();

  SmallVector<MachineBasicBlock::instr_iterator> PAuthPseudoInstrs;
  SmallVector<MachineBasicBlock::instr_iterator> TailCallInstrs;

  bool Modified = false;
  bool HasAuthenticationInstrs = false;

  for (auto &MBB : MF) {
    // Using instr_iterator to catch unsupported bundled TCRETURN* instructions
    // instead of just skipping them.
    for (auto &MI : MBB.instrs()) {
      switch (MI.getOpcode()) {
      default:
        // Bundled TCRETURN* instructions (such as created by KCFI)
        // are not supported yet, but no support is required if no
        // PAUTH_EPILOGUE instructions exist in the same function.
        // Skip the BUNDLE instruction itself (actual bundled instructions
        // follow it in the instruction list).
        if (MI.isBundle())
          continue;
        if (AArch64InstrInfo::isTailCallReturnInst(MI))
          TailCallInstrs.push_back(MI.getIterator());
        break;
      case AArch64::PAUTH_PROLOGUE:
      case AArch64::PAUTH_EPILOGUE:
      case AArch64::PAUTH_BLEND:
        assert(!MI.isBundled());
        PAuthPseudoInstrs.push_back(MI.getIterator());
        break;
      }
    }
  }

  for (auto It : PAuthPseudoInstrs) {
    switch (It->getOpcode()) {
    case AArch64::PAUTH_PROLOGUE:
      signLR(MF, It);
      break;
    case AArch64::PAUTH_EPILOGUE:
      authenticateLR(MF, It);
      HasAuthenticationInstrs = true;
      break;
    case AArch64::PAUTH_BLEND:
      expandPAuthBlend(It);
      break;
    default:
      llvm_unreachable("Unhandled opcode");
    }
    It->eraseFromParent();
    Modified = true;
  }

  // FIXME Do we need to emit any PAuth-related epilogue code at all
  //       when SCS is enabled?
  if (HasAuthenticationInstrs &&
      !MFnI->needsShadowCallStackPrologueEpilogue(MF)) {
    for (auto TailCall : TailCallInstrs) {
      assert(!TailCall->isBundled() && "Not yet supported");
      Modified |= checkAuthenticatedLR(TailCall);
    }
  }

  return Modified;
}
