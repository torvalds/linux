//===- lib/CodeGen/GlobalISel/LegalizerInfo.cpp - Legalizer ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implement an interface to specify and query how an illegal operation on a
// given type should be expanded.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGenTypes/LowLevelType.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>

using namespace llvm;
using namespace LegalizeActions;

#define DEBUG_TYPE "legalizer-info"

cl::opt<bool> llvm::DisableGISelLegalityCheck(
    "disable-gisel-legality-check",
    cl::desc("Don't verify that MIR is fully legal between GlobalISel passes"),
    cl::Hidden);

raw_ostream &llvm::operator<<(raw_ostream &OS, LegalizeAction Action) {
  switch (Action) {
  case Legal:
    OS << "Legal";
    break;
  case NarrowScalar:
    OS << "NarrowScalar";
    break;
  case WidenScalar:
    OS << "WidenScalar";
    break;
  case FewerElements:
    OS << "FewerElements";
    break;
  case MoreElements:
    OS << "MoreElements";
    break;
  case Bitcast:
    OS << "Bitcast";
    break;
  case Lower:
    OS << "Lower";
    break;
  case Libcall:
    OS << "Libcall";
    break;
  case Custom:
    OS << "Custom";
    break;
  case Unsupported:
    OS << "Unsupported";
    break;
  case NotFound:
    OS << "NotFound";
    break;
  case UseLegacyRules:
    OS << "UseLegacyRules";
    break;
  }
  return OS;
}

raw_ostream &LegalityQuery::print(raw_ostream &OS) const {
  OS << "Opcode=" << Opcode << ", Tys={";
  for (const auto &Type : Types) {
    OS << Type << ", ";
  }
  OS << "}, MMOs={";
  for (const auto &MMODescr : MMODescrs) {
    OS << MMODescr.MemoryTy << ", ";
  }
  OS << "}";

  return OS;
}

#ifndef NDEBUG
// Make sure the rule won't (trivially) loop forever.
static bool hasNoSimpleLoops(const LegalizeRule &Rule, const LegalityQuery &Q,
                             const std::pair<unsigned, LLT> &Mutation) {
  switch (Rule.getAction()) {
  case Legal:
  case Custom:
  case Lower:
  case MoreElements:
  case FewerElements:
  case Libcall:
    break;
  default:
    return Q.Types[Mutation.first] != Mutation.second;
  }
  return true;
}

// Make sure the returned mutation makes sense for the match type.
static bool mutationIsSane(const LegalizeRule &Rule,
                           const LegalityQuery &Q,
                           std::pair<unsigned, LLT> Mutation) {
  // If the user wants a custom mutation, then we can't really say much about
  // it. Return true, and trust that they're doing the right thing.
  if (Rule.getAction() == Custom || Rule.getAction() == Legal)
    return true;

  // Skip null mutation.
  if (!Mutation.second.isValid())
    return true;

  const unsigned TypeIdx = Mutation.first;
  const LLT OldTy = Q.Types[TypeIdx];
  const LLT NewTy = Mutation.second;

  switch (Rule.getAction()) {
  case FewerElements:
    if (!OldTy.isVector())
      return false;
    [[fallthrough]];
  case MoreElements: {
    // MoreElements can go from scalar to vector.
    const ElementCount OldElts = OldTy.isVector() ?
      OldTy.getElementCount() : ElementCount::getFixed(1);
    if (NewTy.isVector()) {
      if (Rule.getAction() == FewerElements) {
        // Make sure the element count really decreased.
        if (ElementCount::isKnownGE(NewTy.getElementCount(), OldElts))
          return false;
      } else {
        // Make sure the element count really increased.
        if (ElementCount::isKnownLE(NewTy.getElementCount(), OldElts))
          return false;
      }
    } else if (Rule.getAction() == MoreElements)
      return false;

    // Make sure the element type didn't change.
    return NewTy.getScalarType() == OldTy.getScalarType();
  }
  case NarrowScalar:
  case WidenScalar: {
    if (OldTy.isVector()) {
      // Number of elements should not change.
      if (!NewTy.isVector() ||
          OldTy.getElementCount() != NewTy.getElementCount())
        return false;
    } else {
      // Both types must be vectors
      if (NewTy.isVector())
        return false;
    }

    if (Rule.getAction() == NarrowScalar)  {
      // Make sure the size really decreased.
      if (NewTy.getScalarSizeInBits() >= OldTy.getScalarSizeInBits())
        return false;
    } else {
      // Make sure the size really increased.
      if (NewTy.getScalarSizeInBits() <= OldTy.getScalarSizeInBits())
        return false;
    }

    return true;
  }
  case Bitcast: {
    return OldTy != NewTy && OldTy.getSizeInBits() == NewTy.getSizeInBits();
  }
  default:
    return true;
  }
}
#endif

LegalizeActionStep LegalizeRuleSet::apply(const LegalityQuery &Query) const {
  LLVM_DEBUG(dbgs() << "Applying legalizer ruleset to: "; Query.print(dbgs());
             dbgs() << "\n");
  if (Rules.empty()) {
    LLVM_DEBUG(dbgs() << ".. fallback to legacy rules (no rules defined)\n");
    return {LegalizeAction::UseLegacyRules, 0, LLT{}};
  }
  for (const LegalizeRule &Rule : Rules) {
    if (Rule.match(Query)) {
      LLVM_DEBUG(dbgs() << ".. match\n");
      std::pair<unsigned, LLT> Mutation = Rule.determineMutation(Query);
      LLVM_DEBUG(dbgs() << ".. .. " << Rule.getAction() << ", "
                        << Mutation.first << ", " << Mutation.second << "\n");
      assert(mutationIsSane(Rule, Query, Mutation) &&
             "legality mutation invalid for match");
      assert(hasNoSimpleLoops(Rule, Query, Mutation) && "Simple loop detected");
      return {Rule.getAction(), Mutation.first, Mutation.second};
    } else
      LLVM_DEBUG(dbgs() << ".. no match\n");
  }
  LLVM_DEBUG(dbgs() << ".. unsupported\n");
  return {LegalizeAction::Unsupported, 0, LLT{}};
}

bool LegalizeRuleSet::verifyTypeIdxsCoverage(unsigned NumTypeIdxs) const {
#ifndef NDEBUG
  if (Rules.empty()) {
    LLVM_DEBUG(
        dbgs() << ".. type index coverage check SKIPPED: no rules defined\n");
    return true;
  }
  const int64_t FirstUncovered = TypeIdxsCovered.find_first_unset();
  if (FirstUncovered < 0) {
    LLVM_DEBUG(dbgs() << ".. type index coverage check SKIPPED:"
                         " user-defined predicate detected\n");
    return true;
  }
  const bool AllCovered = (FirstUncovered >= NumTypeIdxs);
  if (NumTypeIdxs > 0)
    LLVM_DEBUG(dbgs() << ".. the first uncovered type index: " << FirstUncovered
                      << ", " << (AllCovered ? "OK" : "FAIL") << "\n");
  return AllCovered;
#else
  return true;
#endif
}

bool LegalizeRuleSet::verifyImmIdxsCoverage(unsigned NumImmIdxs) const {
#ifndef NDEBUG
  if (Rules.empty()) {
    LLVM_DEBUG(
        dbgs() << ".. imm index coverage check SKIPPED: no rules defined\n");
    return true;
  }
  const int64_t FirstUncovered = ImmIdxsCovered.find_first_unset();
  if (FirstUncovered < 0) {
    LLVM_DEBUG(dbgs() << ".. imm index coverage check SKIPPED:"
                         " user-defined predicate detected\n");
    return true;
  }
  const bool AllCovered = (FirstUncovered >= NumImmIdxs);
  LLVM_DEBUG(dbgs() << ".. the first uncovered imm index: " << FirstUncovered
                    << ", " << (AllCovered ? "OK" : "FAIL") << "\n");
  return AllCovered;
#else
  return true;
#endif
}

/// Helper function to get LLT for the given type index.
static LLT getTypeFromTypeIdx(const MachineInstr &MI,
                              const MachineRegisterInfo &MRI, unsigned OpIdx,
                              unsigned TypeIdx) {
  assert(TypeIdx < MI.getNumOperands() && "Unexpected TypeIdx");
  // G_UNMERGE_VALUES has variable number of operands, but there is only
  // one source type and one destination type as all destinations must be the
  // same type. So, get the last operand if TypeIdx == 1.
  if (MI.getOpcode() == TargetOpcode::G_UNMERGE_VALUES && TypeIdx == 1)
    return MRI.getType(MI.getOperand(MI.getNumOperands() - 1).getReg());
  return MRI.getType(MI.getOperand(OpIdx).getReg());
}

unsigned LegalizerInfo::getOpcodeIdxForOpcode(unsigned Opcode) const {
  assert(Opcode >= FirstOp && Opcode <= LastOp && "Unsupported opcode");
  return Opcode - FirstOp;
}

unsigned LegalizerInfo::getActionDefinitionsIdx(unsigned Opcode) const {
  unsigned OpcodeIdx = getOpcodeIdxForOpcode(Opcode);
  if (unsigned Alias = RulesForOpcode[OpcodeIdx].getAlias()) {
    LLVM_DEBUG(dbgs() << ".. opcode " << Opcode << " is aliased to " << Alias
                      << "\n");
    OpcodeIdx = getOpcodeIdxForOpcode(Alias);
    assert(RulesForOpcode[OpcodeIdx].getAlias() == 0 && "Cannot chain aliases");
  }

  return OpcodeIdx;
}

const LegalizeRuleSet &
LegalizerInfo::getActionDefinitions(unsigned Opcode) const {
  unsigned OpcodeIdx = getActionDefinitionsIdx(Opcode);
  return RulesForOpcode[OpcodeIdx];
}

LegalizeRuleSet &LegalizerInfo::getActionDefinitionsBuilder(unsigned Opcode) {
  unsigned OpcodeIdx = getActionDefinitionsIdx(Opcode);
  auto &Result = RulesForOpcode[OpcodeIdx];
  assert(!Result.isAliasedByAnother() && "Modifying this opcode will modify aliases");
  return Result;
}

LegalizeRuleSet &LegalizerInfo::getActionDefinitionsBuilder(
    std::initializer_list<unsigned> Opcodes) {
  unsigned Representative = *Opcodes.begin();

  assert(Opcodes.size() >= 2 &&
         "Initializer list must have at least two opcodes");

  for (unsigned Op : llvm::drop_begin(Opcodes))
    aliasActionDefinitions(Representative, Op);

  auto &Return = getActionDefinitionsBuilder(Representative);
  Return.setIsAliasedByAnother();
  return Return;
}

void LegalizerInfo::aliasActionDefinitions(unsigned OpcodeTo,
                                           unsigned OpcodeFrom) {
  assert(OpcodeTo != OpcodeFrom && "Cannot alias to self");
  assert(OpcodeTo >= FirstOp && OpcodeTo <= LastOp && "Unsupported opcode");
  const unsigned OpcodeFromIdx = getOpcodeIdxForOpcode(OpcodeFrom);
  RulesForOpcode[OpcodeFromIdx].aliasTo(OpcodeTo);
}

LegalizeActionStep
LegalizerInfo::getAction(const LegalityQuery &Query) const {
  LegalizeActionStep Step = getActionDefinitions(Query.Opcode).apply(Query);
  if (Step.Action != LegalizeAction::UseLegacyRules) {
    return Step;
  }

  return getLegacyLegalizerInfo().getAction(Query);
}

LegalizeActionStep
LegalizerInfo::getAction(const MachineInstr &MI,
                         const MachineRegisterInfo &MRI) const {
  SmallVector<LLT, 8> Types;
  SmallBitVector SeenTypes(8);
  ArrayRef<MCOperandInfo> OpInfo = MI.getDesc().operands();
  // FIXME: probably we'll need to cache the results here somehow?
  for (unsigned i = 0; i < MI.getDesc().getNumOperands(); ++i) {
    if (!OpInfo[i].isGenericType())
      continue;

    // We must only record actions once for each TypeIdx; otherwise we'd
    // try to legalize operands multiple times down the line.
    unsigned TypeIdx = OpInfo[i].getGenericTypeIndex();
    if (SeenTypes[TypeIdx])
      continue;

    SeenTypes.set(TypeIdx);

    LLT Ty = getTypeFromTypeIdx(MI, MRI, i, TypeIdx);
    Types.push_back(Ty);
  }

  SmallVector<LegalityQuery::MemDesc, 2> MemDescrs;
  for (const auto &MMO : MI.memoperands())
    MemDescrs.push_back({*MMO});

  return getAction({MI.getOpcode(), Types, MemDescrs});
}

bool LegalizerInfo::isLegal(const MachineInstr &MI,
                            const MachineRegisterInfo &MRI) const {
  return getAction(MI, MRI).Action == Legal;
}

bool LegalizerInfo::isLegalOrCustom(const MachineInstr &MI,
                                    const MachineRegisterInfo &MRI) const {
  auto Action = getAction(MI, MRI).Action;
  // If the action is custom, it may not necessarily modify the instruction,
  // so we have to assume it's legal.
  return Action == Legal || Action == Custom;
}

unsigned LegalizerInfo::getExtOpcodeForWideningConstant(LLT SmallTy) const {
  return SmallTy.isByteSized() ? TargetOpcode::G_SEXT : TargetOpcode::G_ZEXT;
}

/// \pre Type indices of every opcode form a dense set starting from 0.
void LegalizerInfo::verify(const MCInstrInfo &MII) const {
#ifndef NDEBUG
  std::vector<unsigned> FailedOpcodes;
  for (unsigned Opcode = FirstOp; Opcode <= LastOp; ++Opcode) {
    const MCInstrDesc &MCID = MII.get(Opcode);
    const unsigned NumTypeIdxs = std::accumulate(
        MCID.operands().begin(), MCID.operands().end(), 0U,
        [](unsigned Acc, const MCOperandInfo &OpInfo) {
          return OpInfo.isGenericType()
                     ? std::max(OpInfo.getGenericTypeIndex() + 1U, Acc)
                     : Acc;
        });
    const unsigned NumImmIdxs = std::accumulate(
        MCID.operands().begin(), MCID.operands().end(), 0U,
        [](unsigned Acc, const MCOperandInfo &OpInfo) {
          return OpInfo.isGenericImm()
                     ? std::max(OpInfo.getGenericImmIndex() + 1U, Acc)
                     : Acc;
        });
    LLVM_DEBUG(dbgs() << MII.getName(Opcode) << " (opcode " << Opcode
                      << "): " << NumTypeIdxs << " type ind"
                      << (NumTypeIdxs == 1 ? "ex" : "ices") << ", "
                      << NumImmIdxs << " imm ind"
                      << (NumImmIdxs == 1 ? "ex" : "ices") << "\n");
    const LegalizeRuleSet &RuleSet = getActionDefinitions(Opcode);
    if (!RuleSet.verifyTypeIdxsCoverage(NumTypeIdxs))
      FailedOpcodes.push_back(Opcode);
    else if (!RuleSet.verifyImmIdxsCoverage(NumImmIdxs))
      FailedOpcodes.push_back(Opcode);
  }
  if (!FailedOpcodes.empty()) {
    errs() << "The following opcodes have ill-defined legalization rules:";
    for (unsigned Opcode : FailedOpcodes)
      errs() << " " << MII.getName(Opcode);
    errs() << "\n";

    report_fatal_error("ill-defined LegalizerInfo"
                       ", try -debug-only=legalizer-info for details");
  }
#endif
}

#ifndef NDEBUG
// FIXME: This should be in the MachineVerifier, but it can't use the
// LegalizerInfo as it's currently in the separate GlobalISel library.
// Note that RegBankSelected property already checked in the verifier
// has the same layering problem, but we only use inline methods so
// end up not needing to link against the GlobalISel library.
const MachineInstr *llvm::machineFunctionIsIllegal(const MachineFunction &MF) {
  if (const LegalizerInfo *MLI = MF.getSubtarget().getLegalizerInfo()) {
    const MachineRegisterInfo &MRI = MF.getRegInfo();
    for (const MachineBasicBlock &MBB : MF)
      for (const MachineInstr &MI : MBB)
        if (isPreISelGenericOpcode(MI.getOpcode()) &&
            !MLI->isLegalOrCustom(MI, MRI))
          return &MI;
  }
  return nullptr;
}
#endif
