//===-- X86FixupBWInsts.cpp - Fixup Byte or Word instructions -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file defines the pass that looks through the machine instructions
/// late in the compilation, and finds byte or word instructions that
/// can be profitably replaced with 32 bit instructions that give equivalent
/// results for the bits of the results that are used. There are two possible
/// reasons to do this.
///
/// One reason is to avoid false-dependences on the upper portions
/// of the registers.  Only instructions that have a destination register
/// which is not in any of the source registers can be affected by this.
/// Any instruction where one of the source registers is also the destination
/// register is unaffected, because it has a true dependence on the source
/// register already.  So, this consideration primarily affects load
/// instructions and register-to-register moves.  It would
/// seem like cmov(s) would also be affected, but because of the way cmov is
/// really implemented by most machines as reading both the destination and
/// and source registers, and then "merging" the two based on a condition,
/// it really already should be considered as having a true dependence on the
/// destination register as well.
///
/// The other reason to do this is for potential code size savings.  Word
/// operations need an extra override byte compared to their 32 bit
/// versions. So this can convert many word operations to their larger
/// size, saving a byte in encoding. This could introduce partial register
/// dependences where none existed however.  As an example take:
///   orw  ax, $0x1000
///   addw ax, $3
/// now if this were to get transformed into
///   orw  ax, $1000
///   addl eax, $3
/// because the addl encodes shorter than the addw, this would introduce
/// a use of a register that was only partially written earlier.  On older
/// Intel processors this can be quite a performance penalty, so this should
/// probably only be done when it can be proven that a new partial dependence
/// wouldn't be created, or when your know a newer processor is being
/// targeted, or when optimizing for minimum code size.
///
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrInfo.h"
#include "X86Subtarget.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/CodeGen/LazyMachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineSizeOpts.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define FIXUPBW_DESC "X86 Byte/Word Instruction Fixup"
#define FIXUPBW_NAME "x86-fixup-bw-insts"

#define DEBUG_TYPE FIXUPBW_NAME

// Option to allow this optimization pass to have fine-grained control.
static cl::opt<bool>
    FixupBWInsts("fixup-byte-word-insts",
                 cl::desc("Change byte and word instructions to larger sizes"),
                 cl::init(true), cl::Hidden);

namespace {
class FixupBWInstPass : public MachineFunctionPass {
  /// Loop over all of the instructions in the basic block replacing applicable
  /// byte or word instructions with better alternatives.
  void processBasicBlock(MachineFunction &MF, MachineBasicBlock &MBB);

  /// This returns the 32 bit super reg of the original destination register of
  /// the MachineInstr passed in, if that super register is dead just prior to
  /// \p OrigMI. Otherwise it returns Register().
  Register getSuperRegDestIfDead(MachineInstr *OrigMI) const;

  /// Change the MachineInstr \p MI into the equivalent extending load to 32 bit
  /// register if it is safe to do so.  Return the replacement instruction if
  /// OK, otherwise return nullptr.
  MachineInstr *tryReplaceLoad(unsigned New32BitOpcode, MachineInstr *MI) const;

  /// Change the MachineInstr \p MI into the equivalent 32-bit copy if it is
  /// safe to do so.  Return the replacement instruction if OK, otherwise return
  /// nullptr.
  MachineInstr *tryReplaceCopy(MachineInstr *MI) const;

  /// Change the MachineInstr \p MI into the equivalent extend to 32 bit
  /// register if it is safe to do so.  Return the replacement instruction if
  /// OK, otherwise return nullptr.
  MachineInstr *tryReplaceExtend(unsigned New32BitOpcode,
                                 MachineInstr *MI) const;

  // Change the MachineInstr \p MI into an eqivalent 32 bit instruction if
  // possible.  Return the replacement instruction if OK, return nullptr
  // otherwise.
  MachineInstr *tryReplaceInstr(MachineInstr *MI, MachineBasicBlock &MBB) const;

public:
  static char ID;

  StringRef getPassName() const override { return FIXUPBW_DESC; }

  FixupBWInstPass() : MachineFunctionPass(ID) { }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<ProfileSummaryInfoWrapperPass>();
    AU.addRequired<LazyMachineBlockFrequencyInfoPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  /// Loop over all of the basic blocks, replacing byte and word instructions by
  /// equivalent 32 bit instructions where performance or code size can be
  /// improved.
  bool runOnMachineFunction(MachineFunction &MF) override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }

private:
  MachineFunction *MF = nullptr;

  /// Machine instruction info used throughout the class.
  const X86InstrInfo *TII = nullptr;

  const TargetRegisterInfo *TRI = nullptr;

  /// Local member for function's OptForSize attribute.
  bool OptForSize = false;

  /// Register Liveness information after the current instruction.
  LiveRegUnits LiveUnits;

  ProfileSummaryInfo *PSI = nullptr;
  MachineBlockFrequencyInfo *MBFI = nullptr;
};
char FixupBWInstPass::ID = 0;
}

INITIALIZE_PASS(FixupBWInstPass, FIXUPBW_NAME, FIXUPBW_DESC, false, false)

FunctionPass *llvm::createX86FixupBWInsts() { return new FixupBWInstPass(); }

bool FixupBWInstPass::runOnMachineFunction(MachineFunction &MF) {
  if (!FixupBWInsts || skipFunction(MF.getFunction()))
    return false;

  this->MF = &MF;
  TII = MF.getSubtarget<X86Subtarget>().getInstrInfo();
  TRI = MF.getRegInfo().getTargetRegisterInfo();
  PSI = &getAnalysis<ProfileSummaryInfoWrapperPass>().getPSI();
  MBFI = (PSI && PSI->hasProfileSummary()) ?
         &getAnalysis<LazyMachineBlockFrequencyInfoPass>().getBFI() :
         nullptr;
  LiveUnits.init(TII->getRegisterInfo());

  LLVM_DEBUG(dbgs() << "Start X86FixupBWInsts\n";);

  // Process all basic blocks.
  for (auto &MBB : MF)
    processBasicBlock(MF, MBB);

  LLVM_DEBUG(dbgs() << "End X86FixupBWInsts\n";);

  return true;
}

/// Check if after \p OrigMI the only portion of super register
/// of the destination register of \p OrigMI that is alive is that
/// destination register.
///
/// If so, return that super register in \p SuperDestReg.
Register FixupBWInstPass::getSuperRegDestIfDead(MachineInstr *OrigMI) const {
  const X86RegisterInfo *TRI = &TII->getRegisterInfo();
  Register OrigDestReg = OrigMI->getOperand(0).getReg();
  Register SuperDestReg = getX86SubSuperRegister(OrigDestReg, 32);
  assert(SuperDestReg.isValid() && "Invalid Operand");

  const auto SubRegIdx = TRI->getSubRegIndex(SuperDestReg, OrigDestReg);

  // Make sure that the sub-register that this instruction has as its
  // destination is the lowest order sub-register of the super-register.
  // If it isn't, then the register isn't really dead even if the
  // super-register is considered dead.
  if (SubRegIdx == X86::sub_8bit_hi)
    return Register();

  // Test all regunits of the super register that are not part of the
  // sub register. If none of them are live then the super register is safe to
  // use.
  bool SuperIsLive = false;
  auto Range = TRI->regunits(OrigDestReg);
  MCRegUnitIterator I = Range.begin(), E = Range.end();
  for (MCRegUnit S : TRI->regunits(SuperDestReg)) {
    I = std::lower_bound(I, E, S);
    if ((I == E || *I > S) && LiveUnits.getBitVector().test(S)) {
      SuperIsLive = true;
      break;
    }
  }
  if (!SuperIsLive)
    return SuperDestReg;

  // If we get here, the super-register destination (or some part of it) is
  // marked as live after the original instruction.
  //
  // The X86 backend does not have subregister liveness tracking enabled,
  // so liveness information might be overly conservative. Specifically, the
  // super register might be marked as live because it is implicitly defined
  // by the instruction we are examining.
  //
  // However, for some specific instructions (this pass only cares about MOVs)
  // we can produce more precise results by analysing that MOV's operands.
  //
  // Indeed, if super-register is not live before the mov it means that it
  // was originally <read-undef> and so we are free to modify these
  // undef upper bits. That may happen in case where the use is in another MBB
  // and the vreg/physreg corresponding to the move has higher width than
  // necessary (e.g. due to register coalescing with a "truncate" copy).
  // So, we would like to handle patterns like this:
  //
  //   %bb.2: derived from LLVM BB %if.then
  //   Live Ins: %rdi
  //   Predecessors according to CFG: %bb.0
  //   %ax<def> = MOV16rm killed %rdi, 1, %noreg, 0, %noreg, implicit-def %eax
  //                                 ; No implicit %eax
  //   Successors according to CFG: %bb.3(?%)
  //
  //   %bb.3: derived from LLVM BB %if.end
  //   Live Ins: %eax                            Only %ax is actually live
  //   Predecessors according to CFG: %bb.2 %bb.1
  //   %ax = KILL %ax, implicit killed %eax
  //   RET 0, %ax
  unsigned Opc = OrigMI->getOpcode();
  // These are the opcodes currently known to work with the code below, if
  // something // else will be added we need to ensure that new opcode has the
  // same properties.
  if (Opc != X86::MOV8rm && Opc != X86::MOV16rm && Opc != X86::MOV8rr &&
      Opc != X86::MOV16rr)
    return Register();

  bool IsDefined = false;
  for (auto &MO: OrigMI->implicit_operands()) {
    if (!MO.isReg())
      continue;

    if (MO.isDef() && TRI->isSuperRegisterEq(OrigDestReg, MO.getReg()))
      IsDefined = true;

    // If MO is a use of any part of the destination register but is not equal
    // to OrigDestReg or one of its subregisters, we cannot use SuperDestReg.
    // For example, if OrigDestReg is %al then an implicit use of %ah, %ax,
    // %eax, or %rax will prevent us from using the %eax register.
    if (MO.isUse() && !TRI->isSubRegisterEq(OrigDestReg, MO.getReg()) &&
        TRI->regsOverlap(SuperDestReg, MO.getReg()))
      return Register();
  }
  // Reg is not Imp-def'ed -> it's live both before/after the instruction.
  if (!IsDefined)
    return Register();

  // Otherwise, the Reg is not live before the MI and the MOV can't
  // make it really live, so it's in fact dead even after the MI.
  return SuperDestReg;
}

MachineInstr *FixupBWInstPass::tryReplaceLoad(unsigned New32BitOpcode,
                                              MachineInstr *MI) const {
  // We are going to try to rewrite this load to a larger zero-extending
  // load.  This is safe if all portions of the 32 bit super-register
  // of the original destination register, except for the original destination
  // register are dead. getSuperRegDestIfDead checks that.
  Register NewDestReg = getSuperRegDestIfDead(MI);
  if (!NewDestReg)
    return nullptr;

  // Safe to change the instruction.
  MachineInstrBuilder MIB =
      BuildMI(*MF, MIMetadata(*MI), TII->get(New32BitOpcode), NewDestReg);

  unsigned NumArgs = MI->getNumOperands();
  for (unsigned i = 1; i < NumArgs; ++i)
    MIB.add(MI->getOperand(i));

  MIB.setMemRefs(MI->memoperands());

  // If it was debug tracked, record a substitution.
  if (unsigned OldInstrNum = MI->peekDebugInstrNum()) {
    unsigned Subreg = TRI->getSubRegIndex(MIB->getOperand(0).getReg(),
                                          MI->getOperand(0).getReg());
    unsigned NewInstrNum = MIB->getDebugInstrNum(*MF);
    MF->makeDebugValueSubstitution({OldInstrNum, 0}, {NewInstrNum, 0}, Subreg);
  }

  return MIB;
}

MachineInstr *FixupBWInstPass::tryReplaceCopy(MachineInstr *MI) const {
  assert(MI->getNumExplicitOperands() == 2);
  auto &OldDest = MI->getOperand(0);
  auto &OldSrc = MI->getOperand(1);

  Register NewDestReg = getSuperRegDestIfDead(MI);
  if (!NewDestReg)
    return nullptr;

  Register NewSrcReg = getX86SubSuperRegister(OldSrc.getReg(), 32);
  assert(NewSrcReg.isValid() && "Invalid Operand");

  // This is only correct if we access the same subregister index: otherwise,
  // we could try to replace "movb %ah, %al" with "movl %eax, %eax".
  const X86RegisterInfo *TRI = &TII->getRegisterInfo();
  if (TRI->getSubRegIndex(NewSrcReg, OldSrc.getReg()) !=
      TRI->getSubRegIndex(NewDestReg, OldDest.getReg()))
    return nullptr;

  // Safe to change the instruction.
  // Don't set src flags, as we don't know if we're also killing the superreg.
  // However, the superregister might not be defined; make it explicit that
  // we don't care about the higher bits by reading it as Undef, and adding
  // an imp-use on the original subregister.
  MachineInstrBuilder MIB =
      BuildMI(*MF, MIMetadata(*MI), TII->get(X86::MOV32rr), NewDestReg)
          .addReg(NewSrcReg, RegState::Undef)
          .addReg(OldSrc.getReg(), RegState::Implicit);

  // Drop imp-defs/uses that would be redundant with the new def/use.
  for (auto &Op : MI->implicit_operands())
    if (Op.getReg() != (Op.isDef() ? NewDestReg : NewSrcReg))
      MIB.add(Op);

  return MIB;
}

MachineInstr *FixupBWInstPass::tryReplaceExtend(unsigned New32BitOpcode,
                                                MachineInstr *MI) const {
  Register NewDestReg = getSuperRegDestIfDead(MI);
  if (!NewDestReg)
    return nullptr;

  // Don't interfere with formation of CBW instructions which should be a
  // shorter encoding than even the MOVSX32rr8. It's also immune to partial
  // merge issues on Intel CPUs.
  if (MI->getOpcode() == X86::MOVSX16rr8 &&
      MI->getOperand(0).getReg() == X86::AX &&
      MI->getOperand(1).getReg() == X86::AL)
    return nullptr;

  // Safe to change the instruction.
  MachineInstrBuilder MIB =
      BuildMI(*MF, MIMetadata(*MI), TII->get(New32BitOpcode), NewDestReg);

  unsigned NumArgs = MI->getNumOperands();
  for (unsigned i = 1; i < NumArgs; ++i)
    MIB.add(MI->getOperand(i));

  MIB.setMemRefs(MI->memoperands());

  if (unsigned OldInstrNum = MI->peekDebugInstrNum()) {
    unsigned Subreg = TRI->getSubRegIndex(MIB->getOperand(0).getReg(),
                                          MI->getOperand(0).getReg());
    unsigned NewInstrNum = MIB->getDebugInstrNum(*MF);
    MF->makeDebugValueSubstitution({OldInstrNum, 0}, {NewInstrNum, 0}, Subreg);
  }

  return MIB;
}

MachineInstr *FixupBWInstPass::tryReplaceInstr(MachineInstr *MI,
                                               MachineBasicBlock &MBB) const {
  // See if this is an instruction of the type we are currently looking for.
  switch (MI->getOpcode()) {

  case X86::MOV8rm:
    // Replace 8-bit loads with the zero-extending version if not optimizing
    // for size. The extending op is cheaper across a wide range of uarch and
    // it avoids a potentially expensive partial register stall. It takes an
    // extra byte to encode, however, so don't do this when optimizing for size.
    if (!OptForSize)
      return tryReplaceLoad(X86::MOVZX32rm8, MI);
    break;

  case X86::MOV16rm:
    // Always try to replace 16 bit load with 32 bit zero extending.
    // Code size is the same, and there is sometimes a perf advantage
    // from eliminating a false dependence on the upper portion of
    // the register.
    return tryReplaceLoad(X86::MOVZX32rm16, MI);

  case X86::MOV8rr:
  case X86::MOV16rr:
    // Always try to replace 8/16 bit copies with a 32 bit copy.
    // Code size is either less (16) or equal (8), and there is sometimes a
    // perf advantage from eliminating a false dependence on the upper portion
    // of the register.
    return tryReplaceCopy(MI);

  case X86::MOVSX16rr8:
    return tryReplaceExtend(X86::MOVSX32rr8, MI);
  case X86::MOVSX16rm8:
    return tryReplaceExtend(X86::MOVSX32rm8, MI);
  case X86::MOVZX16rr8:
    return tryReplaceExtend(X86::MOVZX32rr8, MI);
  case X86::MOVZX16rm8:
    return tryReplaceExtend(X86::MOVZX32rm8, MI);

  default:
    // nothing to do here.
    break;
  }

  return nullptr;
}

void FixupBWInstPass::processBasicBlock(MachineFunction &MF,
                                        MachineBasicBlock &MBB) {

  // This algorithm doesn't delete the instructions it is replacing
  // right away.  By leaving the existing instructions in place, the
  // register liveness information doesn't change, and this makes the
  // analysis that goes on be better than if the replaced instructions
  // were immediately removed.
  //
  // This algorithm always creates a replacement instruction
  // and notes that and the original in a data structure, until the
  // whole BB has been analyzed.  This keeps the replacement instructions
  // from making it seem as if the larger register might be live.
  SmallVector<std::pair<MachineInstr *, MachineInstr *>, 8> MIReplacements;

  // Start computing liveness for this block. We iterate from the end to be able
  // to update this for each instruction.
  LiveUnits.clear();
  // We run after PEI, so we need to AddPristinesAndCSRs.
  LiveUnits.addLiveOuts(MBB);

  OptForSize = MF.getFunction().hasOptSize() ||
               llvm::shouldOptimizeForSize(&MBB, PSI, MBFI);

  for (MachineInstr &MI : llvm::reverse(MBB)) {
    if (MachineInstr *NewMI = tryReplaceInstr(&MI, MBB))
      MIReplacements.push_back(std::make_pair(&MI, NewMI));

    // We're done with this instruction, update liveness for the next one.
    LiveUnits.stepBackward(MI);
  }

  while (!MIReplacements.empty()) {
    MachineInstr *MI = MIReplacements.back().first;
    MachineInstr *NewMI = MIReplacements.back().second;
    MIReplacements.pop_back();
    MBB.insert(MI, NewMI);
    MBB.erase(MI);
  }
}
