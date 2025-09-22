//===----- HexagonMCChecker.cpp - Instruction bundle checking -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements the checking of insns inside a bundle according to the
// packet constraint rules of the Hexagon ISA.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/HexagonMCChecker.h"
#include "MCTargetDesc/HexagonBaseInfo.h"
#include "MCTargetDesc/HexagonMCInstrInfo.h"
#include "MCTargetDesc/HexagonMCShuffler.h"
#include "MCTargetDesc/HexagonMCTargetDesc.h"

#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/SourceMgr.h"
#include <cassert>

using namespace llvm;

static cl::opt<bool>
    RelaxNVChecks("relax-nv-checks", cl::Hidden,
                  cl::desc("Relax checks of new-value validity"));

const HexagonMCChecker::PredSense
    HexagonMCChecker::Unconditional(Hexagon::NoRegister, false);

void HexagonMCChecker::init() {
  // Initialize read-only registers set.
  ReadOnly.insert(Hexagon::PC);
  ReadOnly.insert(Hexagon::C9_8);

  // Figure out the loop-registers definitions.
  if (HexagonMCInstrInfo::isInnerLoop(MCB)) {
    Defs[Hexagon::SA0].insert(Unconditional); // FIXME: define or change SA0?
    Defs[Hexagon::LC0].insert(Unconditional);
  }
  if (HexagonMCInstrInfo::isOuterLoop(MCB)) {
    Defs[Hexagon::SA1].insert(Unconditional); // FIXME: define or change SA0?
    Defs[Hexagon::LC1].insert(Unconditional);
  }

  if (HexagonMCInstrInfo::isBundle(MCB))
    // Unfurl a bundle.
    for (auto const &I : HexagonMCInstrInfo::bundleInstructions(MCB)) {
      MCInst const &Inst = *I.getInst();
      if (HexagonMCInstrInfo::isDuplex(MCII, Inst)) {
        init(*Inst.getOperand(0).getInst());
        init(*Inst.getOperand(1).getInst());
      } else
        init(Inst);
    }
  else
    init(MCB);
}

void HexagonMCChecker::initReg(MCInst const &MCI, unsigned R, unsigned &PredReg,
                               bool &isTrue) {
  if (HexagonMCInstrInfo::isPredicated(MCII, MCI) &&
      HexagonMCInstrInfo::isPredReg(RI, R)) {
    // Note an used predicate register.
    PredReg = R;
    isTrue = HexagonMCInstrInfo::isPredicatedTrue(MCII, MCI);

    // Note use of new predicate register.
    if (HexagonMCInstrInfo::isPredicatedNew(MCII, MCI))
      NewPreds.insert(PredReg);
  } else
    // Note register use.  Super-registers are not tracked directly,
    // but their components.
    for (MCRegAliasIterator SRI(R, &RI, RI.subregs(R).empty()); SRI.isValid();
         ++SRI)
      if (RI.subregs(*SRI).empty())
        // Skip super-registers used indirectly.
        Uses.insert(*SRI);

  if (HexagonMCInstrInfo::IsReverseVecRegPair(R))
    ReversePairs.insert(R);
}

void HexagonMCChecker::init(MCInst const &MCI) {
  const MCInstrDesc &MCID = HexagonMCInstrInfo::getDesc(MCII, MCI);
  unsigned PredReg = Hexagon::NoRegister;
  bool isTrue = false;

  // Get used registers.
  for (unsigned i = MCID.getNumDefs(); i < MCID.getNumOperands(); ++i)
    if (MCI.getOperand(i).isReg())
      initReg(MCI, MCI.getOperand(i).getReg(), PredReg, isTrue);
  for (MCPhysReg ImpUse : MCID.implicit_uses())
    initReg(MCI, ImpUse, PredReg, isTrue);

  const bool IgnoreTmpDst = (HexagonMCInstrInfo::hasTmpDst(MCII, MCI) ||
                             HexagonMCInstrInfo::hasHvxTmp(MCII, MCI)) &&
                            STI.hasFeature(Hexagon::ArchV69);

  // Get implicit register definitions.
  for (MCPhysReg R : MCID.implicit_defs()) {
    if (Hexagon::R31 != R && MCID.isCall())
      // Any register other than the LR and the PC are actually volatile ones
      // as defined by the ABI, not modified implicitly by the call insn.
      continue;
    if (Hexagon::PC == R)
      // Branches are the only insns that can change the PC,
      // otherwise a read-only register.
      continue;

    if (Hexagon::USR_OVF == R)
      // Many insns change the USR implicitly, but only one or another flag.
      // The instruction table models the USR.OVF flag, which can be
      // implicitly modified more than once, but cannot be modified in the
      // same packet with an instruction that modifies is explicitly. Deal
      // with such situations individually.
      SoftDefs.insert(R);
    else if (HexagonMCInstrInfo::isPredReg(RI, R) &&
             HexagonMCInstrInfo::isPredicateLate(MCII, MCI))
      // Include implicit late predicates.
      LatePreds.insert(R);
    else if (!IgnoreTmpDst)
      Defs[R].insert(PredSense(PredReg, isTrue));
  }

  // Figure out explicit register definitions.
  for (unsigned i = 0; i < MCID.getNumDefs(); ++i) {
    unsigned R = MCI.getOperand(i).getReg(), S = Hexagon::NoRegister;
    // USR has subregisters (while C8 does not for technical reasons), so
    // reset R to USR, since we know how to handle multiple defs of USR,
    // taking into account its subregisters.
    if (R == Hexagon::C8)
      R = Hexagon::USR;

    if (HexagonMCInstrInfo::IsReverseVecRegPair(R))
      ReversePairs.insert(R);

    // Note register definitions, direct ones as well as indirect side-effects.
    // Super-registers are not tracked directly, but their components.
    for (MCRegAliasIterator SRI(R, &RI, RI.subregs(R).empty()); SRI.isValid();
         ++SRI) {
      if (!RI.subregs(*SRI).empty())
        // Skip super-registers defined indirectly.
        continue;

      if (R == *SRI) {
        if (S == R)
          // Avoid scoring the defined register multiple times.
          continue;
        else
          // Note that the defined register has already been scored.
          S = R;
      }

      if (Hexagon::P3_0 != R && Hexagon::P3_0 == *SRI)
        // P3:0 is a special case, since multiple predicate register definitions
        // in a packet is allowed as the equivalent of their logical "and".
        // Only an explicit definition of P3:0 is noted as such; if a
        // side-effect, then note as a soft definition.
        SoftDefs.insert(*SRI);
      else if (HexagonMCInstrInfo::isPredicateLate(MCII, MCI) &&
               HexagonMCInstrInfo::isPredReg(RI, *SRI))
        // Some insns produce predicates too late to be used in the same packet.
        LatePreds.insert(*SRI);
      else if (i == 0 && HexagonMCInstrInfo::getType(MCII, MCI) ==
                             HexagonII::TypeCVI_VM_TMP_LD)
        // Temporary loads should be used in the same packet, but don't commit
        // results, so it should be disregarded if another insn changes the same
        // register.
        // TODO: relies on the impossibility of a current and a temporary loads
        // in the same packet.
        TmpDefs.insert(*SRI);
      else if (!IgnoreTmpDst)
        Defs[*SRI].insert(PredSense(PredReg, isTrue));
    }
  }

  // Figure out definitions of new predicate registers.
  if (HexagonMCInstrInfo::isPredicatedNew(MCII, MCI))
    for (unsigned i = MCID.getNumDefs(); i < MCID.getNumOperands(); ++i)
      if (MCI.getOperand(i).isReg()) {
        unsigned P = MCI.getOperand(i).getReg();

        if (HexagonMCInstrInfo::isPredReg(RI, P))
          NewPreds.insert(P);
      }
}

HexagonMCChecker::HexagonMCChecker(MCContext &Context, MCInstrInfo const &MCII,
                                   MCSubtargetInfo const &STI, MCInst &mcb,
                                   MCRegisterInfo const &ri, bool ReportErrors)
    : Context(Context), MCB(mcb), RI(ri), MCII(MCII), STI(STI),
      ReportErrors(ReportErrors) {
  init();
}

HexagonMCChecker::HexagonMCChecker(HexagonMCChecker const &Other,
                                   MCSubtargetInfo const &STI,
                                   bool CopyReportErrors)
    : Context(Other.Context), MCB(Other.MCB), RI(Other.RI), MCII(Other.MCII),
      STI(STI), ReportErrors(CopyReportErrors ? Other.ReportErrors : false) {
  init();
}

bool HexagonMCChecker::check(bool FullCheck) {
  bool chkP = checkPredicates();
  bool chkNV = checkNewValues();
  bool chkR = checkRegisters();
  bool chkRRO = checkRegistersReadOnly();
  checkRegisterCurDefs();
  bool chkS = checkSolo();
  bool chkSh = true;
  if (FullCheck)
    chkSh = checkShuffle();
  bool chkSl = true;
  if (FullCheck)
    chkSl = checkSlots();
  bool chkAXOK = checkAXOK();
  bool chkCofMax1 = checkCOFMax1();
  bool chkHWLoop = checkHWLoop();
  bool chkValidTmpDst = FullCheck ? checkValidTmpDst() : true;
  bool chkLegalVecRegPair = checkLegalVecRegPair();
  bool ChkHVXAccum = checkHVXAccum();
  bool chk = chkP && chkNV && chkR && chkRRO && chkS && chkSh && chkSl &&
             chkAXOK && chkCofMax1 && chkHWLoop && chkValidTmpDst &&
             chkLegalVecRegPair && ChkHVXAccum;

  return chk;
}

static bool isDuplexAGroup(unsigned Opcode) {
  switch (Opcode) {
  case Hexagon::SA1_addi:
  case Hexagon::SA1_addrx:
  case Hexagon::SA1_addsp:
  case Hexagon::SA1_and1:
  case Hexagon::SA1_clrf:
  case Hexagon::SA1_clrfnew:
  case Hexagon::SA1_clrt:
  case Hexagon::SA1_clrtnew:
  case Hexagon::SA1_cmpeqi:
  case Hexagon::SA1_combine0i:
  case Hexagon::SA1_combine1i:
  case Hexagon::SA1_combine2i:
  case Hexagon::SA1_combine3i:
  case Hexagon::SA1_combinerz:
  case Hexagon::SA1_combinezr:
  case Hexagon::SA1_dec:
  case Hexagon::SA1_inc:
  case Hexagon::SA1_seti:
  case Hexagon::SA1_setin1:
  case Hexagon::SA1_sxtb:
  case Hexagon::SA1_sxth:
  case Hexagon::SA1_tfr:
  case Hexagon::SA1_zxtb:
  case Hexagon::SA1_zxth:
    return true;
    break;
  default:
    return false;
  }
}

static bool isNeitherAnorX(MCInstrInfo const &MCII, MCInst const &ID) {
  if (HexagonMCInstrInfo::isFloat(MCII, ID))
    return true;
  unsigned Type = HexagonMCInstrInfo::getType(MCII, ID);
  switch (Type) {
  case HexagonII::TypeALU32_2op:
  case HexagonII::TypeALU32_3op:
  case HexagonII::TypeALU32_ADDI:
  case HexagonII::TypeS_2op:
  case HexagonII::TypeS_3op:
  case HexagonII::TypeEXTENDER:
  case HexagonII::TypeM:
  case HexagonII::TypeALU64:
    return false;
  case HexagonII::TypeSUBINSN: {
    return !isDuplexAGroup(ID.getOpcode());
  }
  case HexagonII::TypeDUPLEX:
    llvm_unreachable("unexpected duplex instruction");
  default:
    return true;
  }
}

bool HexagonMCChecker::checkAXOK() {
  MCInst const *HasSoloAXInst = nullptr;
  for (auto const &I : HexagonMCInstrInfo::bundleInstructions(MCII, MCB)) {
    if (HexagonMCInstrInfo::isSoloAX(MCII, I)) {
      HasSoloAXInst = &I;
    }
  }
  if (!HasSoloAXInst)
    return true;
  for (auto const &I : HexagonMCInstrInfo::bundleInstructions(MCII, MCB)) {
    if (&I != HasSoloAXInst && isNeitherAnorX(MCII, I)) {
      reportError(
          HasSoloAXInst->getLoc(),
          Twine("Instruction can only be in a packet with ALU or non-FPU XTYPE "
                "instructions"));
      reportError(I.getLoc(),
                  Twine("Not an ALU or non-FPU XTYPE instruction"));
      return false;
    }
  }
  return true;
}

void HexagonMCChecker::reportBranchErrors() {
  for (auto const &I : HexagonMCInstrInfo::bundleInstructions(MCII, MCB)) {
    if (HexagonMCInstrInfo::IsABranchingInst(MCII, STI, I))
      reportNote(I.getLoc(), "Branching instruction");
  }
}

bool HexagonMCChecker::checkHWLoop() {
  if (!HexagonMCInstrInfo::isInnerLoop(MCB) &&
      !HexagonMCInstrInfo::isOuterLoop(MCB))
    return true;
  for (auto const &I : HexagonMCInstrInfo::bundleInstructions(MCII, MCB)) {
    if (HexagonMCInstrInfo::IsABranchingInst(MCII, STI, I)) {
      reportError(MCB.getLoc(),
                  "Branches cannot be in a packet with hardware loops");
      reportBranchErrors();
      return false;
    }
  }
  return true;
}

bool HexagonMCChecker::checkCOFMax1() {
  SmallVector<MCInst const *, 2> BranchLocations;
  for (auto const &I : HexagonMCInstrInfo::bundleInstructions(MCII, MCB)) {
    if (HexagonMCInstrInfo::IsABranchingInst(MCII, STI, I))
      BranchLocations.push_back(&I);
  }
  for (unsigned J = 0, N = BranchLocations.size(); J < N; ++J) {
    MCInst const &I = *BranchLocations[J];
    if (HexagonMCInstrInfo::isCofMax1(MCII, I)) {
      bool Relax1 = HexagonMCInstrInfo::isCofRelax1(MCII, I);
      bool Relax2 = HexagonMCInstrInfo::isCofRelax2(MCII, I);
      if (N > 1 && !Relax1 && !Relax2) {
        reportError(I.getLoc(),
                    "Instruction may not be in a packet with other branches");
        reportBranchErrors();
        return false;
      }
      if (N > 1 && J == 0 && !Relax1) {
        reportError(I.getLoc(),
                    "Instruction may not be the first branch in packet");
        reportBranchErrors();
        return false;
      }
      if (N > 1 && J == 1 && !Relax2) {
        reportError(I.getLoc(),
                    "Instruction may not be the second branch in packet");
        reportBranchErrors();
        return false;
      }
    }
  }
  return true;
}

bool HexagonMCChecker::checkSlots() {
  if (HexagonMCInstrInfo::slotsConsumed(MCII, STI, MCB) >
      HexagonMCInstrInfo::packetSizeSlots(STI)) {
    reportError("invalid instruction packet: out of slots");
    return false;
  }
  return true;
}

// Check legal use of predicate registers.
bool HexagonMCChecker::checkPredicates() {
  // Check for proper use of new predicate registers.
  for (const auto &I : NewPreds) {
    unsigned P = I;

    if (!Defs.count(P) || LatePreds.count(P) || Defs.count(Hexagon::P3_0)) {
      // Error out if the new predicate register is not defined,
      // or defined "late"
      // (e.g., "{ if (p3.new)... ; p3 = sp1loop0(#r7:2, Rs) }").
      reportErrorNewValue(P);
      return false;
    }
  }

  // Check for proper use of auto-anded of predicate registers.
  for (const auto &I : LatePreds) {
    unsigned P = I;

    if (LatePreds.count(P) > 1 || Defs.count(P)) {
      // Error out if predicate register defined "late" multiple times or
      // defined late and regularly defined
      // (e.g., "{ p3 = sp1loop0(...); p3 = cmp.eq(...) }".
      reportErrorRegisters(P);
      return false;
    }
  }

  return true;
}

// Check legal use of new values.
bool HexagonMCChecker::checkNewValues() {
  for (auto const &ConsumerInst :
       HexagonMCInstrInfo::bundleInstructions(MCII, MCB)) {
    if (!HexagonMCInstrInfo::isNewValue(MCII, ConsumerInst))
      continue;

    const HexagonMCInstrInfo::PredicateInfo ConsumerPredInfo =
        HexagonMCInstrInfo::predicateInfo(MCII, ConsumerInst);

    bool Branch = HexagonMCInstrInfo::getDesc(MCII, ConsumerInst).isBranch();
    MCOperand const &Op =
        HexagonMCInstrInfo::getNewValueOperand(MCII, ConsumerInst);
    assert(Op.isReg());

    auto Producer = registerProducer(Op.getReg(), ConsumerPredInfo);
    const MCInst *const ProducerInst = std::get<0>(Producer);
    const HexagonMCInstrInfo::PredicateInfo ProducerPredInfo =
        std::get<2>(Producer);

    if (ProducerInst == nullptr) {
      reportError(ConsumerInst.getLoc(),
                  "New value register consumer has no producer");
      return false;
    }
    if (!RelaxNVChecks) {
      // Checks that statically prove correct new value consumption
      if (ProducerPredInfo.isPredicated() &&
          (!ConsumerPredInfo.isPredicated() ||
           llvm::HexagonMCInstrInfo::getType(MCII, ConsumerInst) ==
               HexagonII::TypeNCJ)) {
        reportNote(
            ProducerInst->getLoc(),
            "Register producer is predicated and consumer is unconditional");
        reportError(ConsumerInst.getLoc(),
                    "Instruction does not have a valid new register producer");
        return false;
      }
      if (ProducerPredInfo.Register != Hexagon::NoRegister &&
          ProducerPredInfo.Register != ConsumerPredInfo.Register) {
        reportNote(ProducerInst->getLoc(),
                   "Register producer does not use the same predicate "
                   "register as the consumer");
        reportError(ConsumerInst.getLoc(),
                    "Instruction does not have a valid new register producer");
        return false;
      }
    }
    if (ProducerPredInfo.Register == ConsumerPredInfo.Register &&
        ConsumerPredInfo.PredicatedTrue != ProducerPredInfo.PredicatedTrue) {
      reportNote(
          ProducerInst->getLoc(),
          "Register producer has the opposite predicate sense as consumer");
      reportError(ConsumerInst.getLoc(),
                  "Instruction does not have a valid new register producer");
      return false;
    }

    MCInstrDesc const &Desc = HexagonMCInstrInfo::getDesc(MCII, *ProducerInst);
    const unsigned ProducerOpIndex = std::get<1>(Producer);

    if (Desc.operands()[ProducerOpIndex].RegClass ==
        Hexagon::DoubleRegsRegClassID) {
      reportNote(ProducerInst->getLoc(),
                 "Double registers cannot be new-value producers");
      reportError(ConsumerInst.getLoc(),
                  "Instruction does not have a valid new register producer");
      return false;
    }

    // The ProducerOpIsMemIndex logic checks for the index of the producer
    // register operand.  Z-reg load instructions have an implicit operand
    // that's not encoded, so the producer won't appear as the 1-th def, it
    // will be at the 0-th.
    const unsigned ProducerOpSearchIndex =
        (HexagonMCInstrInfo::getType(MCII, *ProducerInst) ==
         HexagonII::TypeCVI_ZW)
            ? 0
            : 1;

    const bool ProducerOpIsMemIndex =
        ((Desc.mayLoad() && ProducerOpIndex == ProducerOpSearchIndex) ||
         (Desc.mayStore() && ProducerOpIndex == 0));

    if (ProducerOpIsMemIndex) {
      unsigned Mode = HexagonMCInstrInfo::getAddrMode(MCII, *ProducerInst);

      StringRef ModeError;
      if (Mode == HexagonII::AbsoluteSet)
        ModeError = "Absolute-set";
      if (Mode == HexagonII::PostInc)
        ModeError = "Auto-increment";
      if (!ModeError.empty()) {
        reportNote(ProducerInst->getLoc(),
                   ModeError + " registers cannot be a new-value "
                               "producer");
        reportError(ConsumerInst.getLoc(),
                    "Instruction does not have a valid new register producer");
        return false;
      }
    }
    if (Branch && HexagonMCInstrInfo::isFloat(MCII, *ProducerInst)) {
      reportNote(ProducerInst->getLoc(),
                 "FPU instructions cannot be new-value producers for jumps");
      reportError(ConsumerInst.getLoc(),
                  "Instruction does not have a valid new register producer");
      return false;
    }
  }
  return true;
}

bool HexagonMCChecker::checkRegistersReadOnly() {
  for (auto I : HexagonMCInstrInfo::bundleInstructions(MCB)) {
    MCInst const &Inst = *I.getInst();
    unsigned Defs = HexagonMCInstrInfo::getDesc(MCII, Inst).getNumDefs();
    for (unsigned j = 0; j < Defs; ++j) {
      MCOperand const &Operand = Inst.getOperand(j);
      assert(Operand.isReg() && "Def is not a register");
      unsigned Register = Operand.getReg();
      if (ReadOnly.find(Register) != ReadOnly.end()) {
        reportError(Inst.getLoc(), "Cannot write to read-only register `" +
                                       Twine(RI.getName(Register)) + "'");
        return false;
      }
    }
  }
  return true;
}

bool HexagonMCChecker::registerUsed(unsigned Register) {
  for (auto const &I : HexagonMCInstrInfo::bundleInstructions(MCII, MCB))
    for (unsigned j = HexagonMCInstrInfo::getDesc(MCII, I).getNumDefs(),
                  n = I.getNumOperands();
         j < n; ++j) {
      MCOperand const &Operand = I.getOperand(j);
      if (Operand.isReg() && Operand.getReg() == Register)
        return true;
    }
  return false;
}

std::tuple<MCInst const *, unsigned, HexagonMCInstrInfo::PredicateInfo>
HexagonMCChecker::registerProducer(
    unsigned Register, HexagonMCInstrInfo::PredicateInfo ConsumerPredicate) {
  std::tuple<MCInst const *, unsigned, HexagonMCInstrInfo::PredicateInfo>
      WrongSense;

  for (auto const &I : HexagonMCInstrInfo::bundleInstructions(MCII, MCB)) {
    MCInstrDesc const &Desc = HexagonMCInstrInfo::getDesc(MCII, I);
    auto ProducerPredicate = HexagonMCInstrInfo::predicateInfo(MCII, I);

    for (unsigned J = 0, N = Desc.getNumDefs(); J < N; ++J)
      for (auto K = MCRegAliasIterator(I.getOperand(J).getReg(), &RI, true);
           K.isValid(); ++K)
        if (*K == Register) {
          if (RelaxNVChecks ||
              (ProducerPredicate.Register == ConsumerPredicate.Register &&
               (ProducerPredicate.Register == Hexagon::NoRegister ||
                ProducerPredicate.PredicatedTrue ==
                    ConsumerPredicate.PredicatedTrue)))
            return std::make_tuple(&I, J, ProducerPredicate);
          std::get<0>(WrongSense) = &I;
          std::get<1>(WrongSense) = J;
          std::get<2>(WrongSense) = ProducerPredicate;
        }
    if (Register == Hexagon::VTMP && HexagonMCInstrInfo::hasTmpDst(MCII, I))
      return std::make_tuple(&I, 0, HexagonMCInstrInfo::PredicateInfo());
  }
  return WrongSense;
}

void HexagonMCChecker::checkRegisterCurDefs() {
  for (auto const &I : HexagonMCInstrInfo::bundleInstructions(MCII, MCB)) {
    if (HexagonMCInstrInfo::isCVINew(MCII, I) &&
        HexagonMCInstrInfo::getDesc(MCII, I).mayLoad()) {
      const unsigned RegDef = I.getOperand(0).getReg();

      bool HasRegDefUse = false;
      for (MCRegAliasIterator Alias(RegDef, &RI, true); Alias.isValid();
           ++Alias)
        HasRegDefUse = HasRegDefUse || registerUsed(*Alias);

      if (!HasRegDefUse)
        reportWarning("Register `" + Twine(RI.getName(RegDef)) +
                      "' used with `.cur' "
                      "but not used in the same packet");
    }
  }
}

// Check for legal register uses and definitions.
bool HexagonMCChecker::checkRegisters() {
  // Check for proper register definitions.
  for (const auto &I : Defs) {
    unsigned R = I.first;

    if (isLoopRegister(R) && Defs.count(R) > 1 &&
        (HexagonMCInstrInfo::isInnerLoop(MCB) ||
         HexagonMCInstrInfo::isOuterLoop(MCB))) {
      // Error out for definitions of loop registers at the end of a loop.
      reportError("loop-setup and some branch instructions "
                  "cannot be in the same packet");
      return false;
    }
    if (SoftDefs.count(R)) {
      // Error out for explicit changes to registers also weakly defined
      // (e.g., "{ usr = r0; r0 = sfadd(...) }").
      unsigned UsrR = Hexagon::USR; // Silence warning about mixed types in ?:.
      unsigned BadR = RI.isSubRegister(Hexagon::USR, R) ? UsrR : R;
      reportErrorRegisters(BadR);
      return false;
    }
    if (!HexagonMCInstrInfo::isPredReg(RI, R) && Defs[R].size() > 1) {
      // Check for multiple register definitions.
      PredSet &PM = Defs[R];

      // Check for multiple unconditional register definitions.
      if (PM.count(Unconditional)) {
        // Error out on an unconditional change when there are any other
        // changes, conditional or not.
        unsigned UsrR = Hexagon::USR;
        unsigned BadR = RI.isSubRegister(Hexagon::USR, R) ? UsrR : R;
        reportErrorRegisters(BadR);
        return false;
      }
      // Check for multiple conditional register definitions.
      for (const auto &J : PM) {
        PredSense P = J;

        // Check for multiple uses of the same condition.
        if (PM.count(P) > 1) {
          // Error out on conditional changes based on the same predicate
          // (e.g., "{ if (!p0) r0 =...; if (!p0) r0 =... }").
          reportErrorRegisters(R);
          return false;
        }
        // Check for the use of the complementary condition.
        P.second = !P.second;
        if (PM.count(P) && PM.size() > 2) {
          // Error out on conditional changes based on the same predicate
          // multiple times
          // (e.g., "if (p0) r0 =...; if (!p0) r0 =... }; if (!p0) r0 =...").
          reportErrorRegisters(R);
          return false;
        }
      }
    }
  }

  // Check for use of temporary definitions.
  for (const auto &I : TmpDefs) {
    unsigned R = I;

    if (!Uses.count(R)) {
      // special case for vhist
      bool vHistFound = false;
      for (auto const &HMI : HexagonMCInstrInfo::bundleInstructions(MCB)) {
        if (HexagonMCInstrInfo::getType(MCII, *HMI.getInst()) ==
            HexagonII::TypeCVI_HIST) {
          vHistFound = true; // vhist() implicitly uses ALL REGxx.tmp
          break;
        }
      }
      // Warn on an unused temporary definition.
      if (!vHistFound) {
        reportWarning("register `" + Twine(RI.getName(R)) +
                      "' used with `.tmp' but not used in the same packet");
        return true;
      }
    }
  }

  return true;
}

// Check for legal use of solo insns.
bool HexagonMCChecker::checkSolo() {
  if (HexagonMCInstrInfo::bundleSize(MCB) > 1)
    for (auto const &I : HexagonMCInstrInfo::bundleInstructions(MCII, MCB)) {
      if (HexagonMCInstrInfo::isSolo(MCII, I)) {
        reportError(I.getLoc(), "Instruction is marked `isSolo' and "
                                "cannot have other instructions in "
                                "the same packet");
        return false;
      }
    }

  return true;
}

bool HexagonMCChecker::checkShuffle() {
  HexagonMCShuffler MCSDX(Context, ReportErrors, MCII, STI, MCB);
  return MCSDX.check();
}

bool HexagonMCChecker::checkValidTmpDst() {
  if (!STI.hasFeature(Hexagon::ArchV69)) {
    return true;
  }
  auto HasTmp = [&](MCInst const &I) {
    return HexagonMCInstrInfo::hasTmpDst(MCII, I) ||
           HexagonMCInstrInfo::hasHvxTmp(MCII, I);
  };
  unsigned HasTmpCount =
      llvm::count_if(HexagonMCInstrInfo::bundleInstructions(MCII, MCB), HasTmp);

  if (HasTmpCount > 1) {
    reportError(
        MCB.getLoc(),
        "this packet has more than one HVX vtmp/.tmp destination instruction");

    for (auto const &I : HexagonMCInstrInfo::bundleInstructions(MCII, MCB))
      if (HasTmp(I))
        reportNote(I.getLoc(),
                   "this is an HVX vtmp/.tmp destination instruction");

    return false;
  }
  return true;
}

void HexagonMCChecker::compoundRegisterMap(unsigned &Register) {
  switch (Register) {
  default:
    break;
  case Hexagon::R15:
    Register = Hexagon::R23;
    break;
  case Hexagon::R14:
    Register = Hexagon::R22;
    break;
  case Hexagon::R13:
    Register = Hexagon::R21;
    break;
  case Hexagon::R12:
    Register = Hexagon::R20;
    break;
  case Hexagon::R11:
    Register = Hexagon::R19;
    break;
  case Hexagon::R10:
    Register = Hexagon::R18;
    break;
  case Hexagon::R9:
    Register = Hexagon::R17;
    break;
  case Hexagon::R8:
    Register = Hexagon::R16;
    break;
  }
}

void HexagonMCChecker::reportErrorRegisters(unsigned Register) {
  reportError("register `" + Twine(RI.getName(Register)) +
              "' modified more than once");
}

void HexagonMCChecker::reportErrorNewValue(unsigned Register) {
  reportError("register `" + Twine(RI.getName(Register)) +
              "' used with `.new' "
              "but not validly modified in the same packet");
}

void HexagonMCChecker::reportError(Twine const &Msg) {
  reportError(MCB.getLoc(), Msg);
}

void HexagonMCChecker::reportError(SMLoc Loc, Twine const &Msg) {
  if (ReportErrors)
    Context.reportError(Loc, Msg);
}

void HexagonMCChecker::reportNote(SMLoc Loc, llvm::Twine const &Msg) {
  if (ReportErrors) {
    auto SM = Context.getSourceManager();
    if (SM)
      SM->PrintMessage(Loc, SourceMgr::DK_Note, Msg);
  }
}

void HexagonMCChecker::reportWarning(Twine const &Msg) {
  if (ReportErrors)
    Context.reportWarning(MCB.getLoc(), Msg);
}

bool HexagonMCChecker::checkLegalVecRegPair() {
  const bool IsPermitted = STI.hasFeature(Hexagon::ArchV67);
  const bool HasReversePairs = ReversePairs.size() != 0;

  if (!IsPermitted && HasReversePairs) {
    for (auto R : ReversePairs)
      reportError("register pair `" + Twine(RI.getName(R)) +
                  "' is not permitted for this architecture");
    return false;
  }
  return true;
}

// Vd.tmp can't be accumulated
bool HexagonMCChecker::checkHVXAccum()
{
  for (const auto &I : HexagonMCInstrInfo::bundleInstructions(MCII, MCB)) {
    bool IsTarget =
        HexagonMCInstrInfo::isAccumulator(MCII, I) && I.getOperand(0).isReg();
    if (!IsTarget)
      continue;
    unsigned int R = I.getOperand(0).getReg();
    TmpDefsIterator It = TmpDefs.find(R);
    if (It != TmpDefs.end()) {
      reportError("register `" + Twine(RI.getName(R)) + ".tmp" +
                  "' is accumulated in this packet");
      return false;
    }
  }
  return true;
}
