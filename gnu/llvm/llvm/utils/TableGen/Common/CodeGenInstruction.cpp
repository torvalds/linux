//===- CodeGenInstruction.cpp - CodeGen Instruction Class Wrapper ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the CodeGenInstruction class.
//
//===----------------------------------------------------------------------===//

#include "CodeGenInstruction.h"
#include "CodeGenTarget.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include <set>
using namespace llvm;

//===----------------------------------------------------------------------===//
// CGIOperandList Implementation
//===----------------------------------------------------------------------===//

CGIOperandList::CGIOperandList(Record *R) : TheDef(R) {
  isPredicable = false;
  hasOptionalDef = false;
  isVariadic = false;

  DagInit *OutDI = R->getValueAsDag("OutOperandList");

  if (DefInit *Init = dyn_cast<DefInit>(OutDI->getOperator())) {
    if (Init->getDef()->getName() != "outs")
      PrintFatalError(R->getLoc(),
                      R->getName() +
                          ": invalid def name for output list: use 'outs'");
  } else
    PrintFatalError(R->getLoc(),
                    R->getName() + ": invalid output list: use 'outs'");

  NumDefs = OutDI->getNumArgs();

  DagInit *InDI = R->getValueAsDag("InOperandList");
  if (DefInit *Init = dyn_cast<DefInit>(InDI->getOperator())) {
    if (Init->getDef()->getName() != "ins")
      PrintFatalError(R->getLoc(),
                      R->getName() +
                          ": invalid def name for input list: use 'ins'");
  } else
    PrintFatalError(R->getLoc(),
                    R->getName() + ": invalid input list: use 'ins'");

  unsigned MIOperandNo = 0;
  std::set<std::string> OperandNames;
  unsigned e = InDI->getNumArgs() + OutDI->getNumArgs();
  OperandList.reserve(e);
  bool VariadicOuts = false;
  for (unsigned i = 0; i != e; ++i) {
    Init *ArgInit;
    StringRef ArgName;
    if (i < NumDefs) {
      ArgInit = OutDI->getArg(i);
      ArgName = OutDI->getArgNameStr(i);
    } else {
      ArgInit = InDI->getArg(i - NumDefs);
      ArgName = InDI->getArgNameStr(i - NumDefs);
    }

    DagInit *SubArgDag = dyn_cast<DagInit>(ArgInit);
    if (SubArgDag)
      ArgInit = SubArgDag->getOperator();

    DefInit *Arg = dyn_cast<DefInit>(ArgInit);
    if (!Arg)
      PrintFatalError(R->getLoc(), "Illegal operand for the '" + R->getName() +
                                       "' instruction!");

    Record *Rec = Arg->getDef();
    std::string PrintMethod = "printOperand";
    std::string EncoderMethod;
    std::string OperandType = "OPERAND_UNKNOWN";
    std::string OperandNamespace = "MCOI";
    unsigned NumOps = 1;
    DagInit *MIOpInfo = nullptr;
    if (Rec->isSubClassOf("RegisterOperand")) {
      PrintMethod = std::string(Rec->getValueAsString("PrintMethod"));
      OperandType = std::string(Rec->getValueAsString("OperandType"));
      OperandNamespace = std::string(Rec->getValueAsString("OperandNamespace"));
      EncoderMethod = std::string(Rec->getValueAsString("EncoderMethod"));
    } else if (Rec->isSubClassOf("Operand")) {
      PrintMethod = std::string(Rec->getValueAsString("PrintMethod"));
      OperandType = std::string(Rec->getValueAsString("OperandType"));
      OperandNamespace = std::string(Rec->getValueAsString("OperandNamespace"));
      // If there is an explicit encoder method, use it.
      EncoderMethod = std::string(Rec->getValueAsString("EncoderMethod"));
      MIOpInfo = Rec->getValueAsDag("MIOperandInfo");

      // Verify that MIOpInfo has an 'ops' root value.
      if (!isa<DefInit>(MIOpInfo->getOperator()) ||
          cast<DefInit>(MIOpInfo->getOperator())->getDef()->getName() != "ops")
        PrintFatalError(R->getLoc(),
                        "Bad value for MIOperandInfo in operand '" +
                            Rec->getName() + "'\n");

      // If we have MIOpInfo, then we have #operands equal to number of entries
      // in MIOperandInfo.
      if (unsigned NumArgs = MIOpInfo->getNumArgs())
        NumOps = NumArgs;

      if (Rec->isSubClassOf("PredicateOp"))
        isPredicable = true;
      else if (Rec->isSubClassOf("OptionalDefOperand"))
        hasOptionalDef = true;
    } else if (Rec->getName() == "variable_ops") {
      if (i < NumDefs)
        VariadicOuts = true;
      isVariadic = true;
      continue;
    } else if (Rec->isSubClassOf("RegisterClass")) {
      OperandType = "OPERAND_REGISTER";
    } else if (!Rec->isSubClassOf("PointerLikeRegClass") &&
               !Rec->isSubClassOf("unknown_class")) {
      PrintFatalError(R->getLoc(), "Unknown operand class '" + Rec->getName() +
                                       "' in '" + R->getName() +
                                       "' instruction!");
    }

    // Check that the operand has a name and that it's unique.
    if (ArgName.empty())
      PrintFatalError(R->getLoc(), "In instruction '" + R->getName() +
                                       "', operand #" + Twine(i) +
                                       " has no name!");
    if (!OperandNames.insert(std::string(ArgName)).second)
      PrintFatalError(R->getLoc(),
                      "In instruction '" + R->getName() + "', operand #" +
                          Twine(i) +
                          " has the same name as a previous operand!");

    OperandInfo &OpInfo = OperandList.emplace_back(
        Rec, std::string(ArgName), std::string(PrintMethod),
        OperandNamespace + "::" + OperandType, MIOperandNo, NumOps, MIOpInfo);

    if (SubArgDag) {
      if (SubArgDag->getNumArgs() != NumOps) {
        PrintFatalError(R->getLoc(), "In instruction '" + R->getName() +
                                         "', operand #" + Twine(i) + " has " +
                                         Twine(SubArgDag->getNumArgs()) +
                                         " sub-arg names, expected " +
                                         Twine(NumOps) + ".");
      }

      for (unsigned j = 0; j < NumOps; ++j) {
        if (!isa<UnsetInit>(SubArgDag->getArg(j)))
          PrintFatalError(R->getLoc(),
                          "In instruction '" + R->getName() + "', operand #" +
                              Twine(i) + " sub-arg #" + Twine(j) +
                              " has unexpected operand (expected only $name).");

        StringRef SubArgName = SubArgDag->getArgNameStr(j);
        if (SubArgName.empty())
          PrintFatalError(R->getLoc(), "In instruction '" + R->getName() +
                                           "', operand #" + Twine(i) +
                                           " has no name!");
        if (!OperandNames.insert(std::string(SubArgName)).second)
          PrintFatalError(R->getLoc(),
                          "In instruction '" + R->getName() + "', operand #" +
                              Twine(i) + " sub-arg #" + Twine(j) +
                              " has the same name as a previous operand!");

        if (auto MaybeEncoderMethod =
                cast<DefInit>(MIOpInfo->getArg(j))
                    ->getDef()
                    ->getValueAsOptionalString("EncoderMethod")) {
          OpInfo.EncoderMethodNames[j] = *MaybeEncoderMethod;
        }

        OpInfo.SubOpNames[j] = SubArgName;
        SubOpAliases[SubArgName] = std::pair(i, j);
      }
    } else if (!EncoderMethod.empty()) {
      // If we have no explicit sub-op dag, but have an top-level encoder
      // method, the single encoder will multiple sub-ops, itself.
      OpInfo.EncoderMethodNames[0] = EncoderMethod;
      for (unsigned j = 1; j < NumOps; ++j)
        OpInfo.DoNotEncode[j] = true;
    }

    MIOperandNo += NumOps;
  }

  if (VariadicOuts)
    --NumDefs;
}

/// getOperandNamed - Return the index of the operand with the specified
/// non-empty name.  If the instruction does not have an operand with the
/// specified name, abort.
///
unsigned CGIOperandList::getOperandNamed(StringRef Name) const {
  unsigned OpIdx;
  if (hasOperandNamed(Name, OpIdx))
    return OpIdx;
  PrintFatalError(TheDef->getLoc(), "'" + TheDef->getName() +
                                        "' does not have an operand named '$" +
                                        Name + "'!");
}

/// hasOperandNamed - Query whether the instruction has an operand of the
/// given name. If so, return true and set OpIdx to the index of the
/// operand. Otherwise, return false.
bool CGIOperandList::hasOperandNamed(StringRef Name, unsigned &OpIdx) const {
  assert(!Name.empty() && "Cannot search for operand with no name!");
  for (unsigned i = 0, e = OperandList.size(); i != e; ++i)
    if (OperandList[i].Name == Name) {
      OpIdx = i;
      return true;
    }
  return false;
}

bool CGIOperandList::hasSubOperandAlias(
    StringRef Name, std::pair<unsigned, unsigned> &SubOp) const {
  assert(!Name.empty() && "Cannot search for operand with no name!");
  auto SubOpIter = SubOpAliases.find(Name);
  if (SubOpIter != SubOpAliases.end()) {
    SubOp = SubOpIter->second;
    return true;
  }
  return false;
}

std::pair<unsigned, unsigned>
CGIOperandList::ParseOperandName(StringRef Op, bool AllowWholeOp) {
  if (!Op.starts_with("$"))
    PrintFatalError(TheDef->getLoc(),
                    TheDef->getName() + ": Illegal operand name: '" + Op + "'");

  StringRef OpName = Op.substr(1);
  StringRef SubOpName;

  // Check to see if this is $foo.bar.
  StringRef::size_type DotIdx = OpName.find_first_of('.');
  if (DotIdx != StringRef::npos) {
    SubOpName = OpName.substr(DotIdx + 1);
    if (SubOpName.empty())
      PrintFatalError(TheDef->getLoc(),
                      TheDef->getName() +
                          ": illegal empty suboperand name in '" + Op + "'");
    OpName = OpName.substr(0, DotIdx);
  }

  unsigned OpIdx;

  if (std::pair<unsigned, unsigned> SubOp; hasSubOperandAlias(OpName, SubOp)) {
    // Found a name for a piece of an operand, just return it directly.
    if (!SubOpName.empty()) {
      PrintFatalError(
          TheDef->getLoc(),
          TheDef->getName() +
              ": Cannot use dotted suboperand name within suboperand '" +
              OpName + "'");
    }
    return SubOp;
  }

  OpIdx = getOperandNamed(OpName);

  if (SubOpName.empty()) { // If no suboperand name was specified:
    // If one was needed, throw.
    if (OperandList[OpIdx].MINumOperands > 1 && !AllowWholeOp &&
        SubOpName.empty())
      PrintFatalError(TheDef->getLoc(),
                      TheDef->getName() +
                          ": Illegal to refer to"
                          " whole operand part of complex operand '" +
                          Op + "'");

    // Otherwise, return the operand.
    return std::pair(OpIdx, 0U);
  }

  // Find the suboperand number involved.
  DagInit *MIOpInfo = OperandList[OpIdx].MIOperandInfo;
  if (!MIOpInfo)
    PrintFatalError(TheDef->getLoc(), TheDef->getName() +
                                          ": unknown suboperand name in '" +
                                          Op + "'");

  // Find the operand with the right name.
  for (unsigned i = 0, e = MIOpInfo->getNumArgs(); i != e; ++i)
    if (MIOpInfo->getArgNameStr(i) == SubOpName)
      return std::pair(OpIdx, i);

  // Otherwise, didn't find it!
  PrintFatalError(TheDef->getLoc(), TheDef->getName() +
                                        ": unknown suboperand name in '" + Op +
                                        "'");
  return std::pair(0U, 0U);
}

static void ParseConstraint(StringRef CStr, CGIOperandList &Ops, Record *Rec) {
  // EARLY_CLOBBER: @early $reg
  StringRef::size_type wpos = CStr.find_first_of(" \t");
  StringRef::size_type start = CStr.find_first_not_of(" \t");
  StringRef Tok = CStr.substr(start, wpos - start);
  if (Tok == "@earlyclobber") {
    StringRef Name = CStr.substr(wpos + 1);
    wpos = Name.find_first_not_of(" \t");
    if (wpos == StringRef::npos)
      PrintFatalError(Rec->getLoc(),
                      "Illegal format for @earlyclobber constraint in '" +
                          Rec->getName() + "': '" + CStr + "'");
    Name = Name.substr(wpos);
    std::pair<unsigned, unsigned> Op = Ops.ParseOperandName(Name, false);

    // Build the string for the operand
    if (!Ops[Op.first].Constraints[Op.second].isNone())
      PrintFatalError(Rec->getLoc(), "Operand '" + Name + "' of '" +
                                         Rec->getName() +
                                         "' cannot have multiple constraints!");
    Ops[Op.first].Constraints[Op.second] =
        CGIOperandList::ConstraintInfo::getEarlyClobber();
    return;
  }

  // Only other constraint is "TIED_TO" for now.
  StringRef::size_type pos = CStr.find_first_of('=');
  if (pos == StringRef::npos || pos == 0 ||
      CStr.find_first_of(" \t", pos) != (pos + 1) ||
      CStr.find_last_of(" \t", pos) != (pos - 1))
    PrintFatalError(Rec->getLoc(), "Unrecognized constraint '" + CStr +
                                       "' in '" + Rec->getName() + "'");
  start = CStr.find_first_not_of(" \t");

  // TIED_TO: $src1 = $dst
  wpos = CStr.find_first_of(" \t", start);
  if (wpos == StringRef::npos || wpos > pos)
    PrintFatalError(Rec->getLoc(),
                    "Illegal format for tied-to constraint in '" +
                        Rec->getName() + "': '" + CStr + "'");
  StringRef LHSOpName = CStr.substr(start, wpos - start);
  std::pair<unsigned, unsigned> LHSOp = Ops.ParseOperandName(LHSOpName, false);

  wpos = CStr.find_first_not_of(" \t", pos + 1);
  if (wpos == StringRef::npos)
    PrintFatalError(Rec->getLoc(),
                    "Illegal format for tied-to constraint: '" + CStr + "'");

  StringRef RHSOpName = CStr.substr(wpos);
  std::pair<unsigned, unsigned> RHSOp = Ops.ParseOperandName(RHSOpName, false);

  // Sort the operands into order, which should put the output one
  // first. But keep the original order, for use in diagnostics.
  bool FirstIsDest = (LHSOp < RHSOp);
  std::pair<unsigned, unsigned> DestOp = (FirstIsDest ? LHSOp : RHSOp);
  StringRef DestOpName = (FirstIsDest ? LHSOpName : RHSOpName);
  std::pair<unsigned, unsigned> SrcOp = (FirstIsDest ? RHSOp : LHSOp);
  StringRef SrcOpName = (FirstIsDest ? RHSOpName : LHSOpName);

  // Ensure one operand is a def and the other is a use.
  if (DestOp.first >= Ops.NumDefs)
    PrintFatalError(Rec->getLoc(), "Input operands '" + LHSOpName + "' and '" +
                                       RHSOpName + "' of '" + Rec->getName() +
                                       "' cannot be tied!");
  if (SrcOp.first < Ops.NumDefs)
    PrintFatalError(Rec->getLoc(), "Output operands '" + LHSOpName + "' and '" +
                                       RHSOpName + "' of '" + Rec->getName() +
                                       "' cannot be tied!");

  // The constraint has to go on the operand with higher index, i.e.
  // the source one. Check there isn't another constraint there
  // already.
  if (!Ops[SrcOp.first].Constraints[SrcOp.second].isNone())
    PrintFatalError(Rec->getLoc(), "Operand '" + SrcOpName + "' of '" +
                                       Rec->getName() +
                                       "' cannot have multiple constraints!");

  unsigned DestFlatOpNo = Ops.getFlattenedOperandNumber(DestOp);
  auto NewConstraint = CGIOperandList::ConstraintInfo::getTied(DestFlatOpNo);

  // Check that the earlier operand is not the target of another tie
  // before making it the target of this one.
  for (const CGIOperandList::OperandInfo &Op : Ops) {
    for (unsigned i = 0; i < Op.MINumOperands; i++)
      if (Op.Constraints[i] == NewConstraint)
        PrintFatalError(Rec->getLoc(),
                        "Operand '" + DestOpName + "' of '" + Rec->getName() +
                            "' cannot have multiple operands tied to it!");
  }

  Ops[SrcOp.first].Constraints[SrcOp.second] = NewConstraint;
}

static void ParseConstraints(StringRef CStr, CGIOperandList &Ops, Record *Rec) {
  if (CStr.empty())
    return;

  StringRef delims(",");
  StringRef::size_type bidx, eidx;

  bidx = CStr.find_first_not_of(delims);
  while (bidx != StringRef::npos) {
    eidx = CStr.find_first_of(delims, bidx);
    if (eidx == StringRef::npos)
      eidx = CStr.size();

    ParseConstraint(CStr.substr(bidx, eidx - bidx), Ops, Rec);
    bidx = CStr.find_first_not_of(delims, eidx);
  }
}

void CGIOperandList::ProcessDisableEncoding(StringRef DisableEncoding) {
  while (true) {
    StringRef OpName;
    std::tie(OpName, DisableEncoding) = getToken(DisableEncoding, " ,\t");
    if (OpName.empty())
      break;

    // Figure out which operand this is.
    std::pair<unsigned, unsigned> Op = ParseOperandName(OpName, false);

    // Mark the operand as not-to-be encoded.
    OperandList[Op.first].DoNotEncode[Op.second] = true;
  }
}

//===----------------------------------------------------------------------===//
// CodeGenInstruction Implementation
//===----------------------------------------------------------------------===//

CodeGenInstruction::CodeGenInstruction(Record *R)
    : TheDef(R), Operands(R), InferredFrom(nullptr) {
  Namespace = R->getValueAsString("Namespace");
  AsmString = std::string(R->getValueAsString("AsmString"));

  isPreISelOpcode = R->getValueAsBit("isPreISelOpcode");
  isReturn = R->getValueAsBit("isReturn");
  isEHScopeReturn = R->getValueAsBit("isEHScopeReturn");
  isBranch = R->getValueAsBit("isBranch");
  isIndirectBranch = R->getValueAsBit("isIndirectBranch");
  isCompare = R->getValueAsBit("isCompare");
  isMoveImm = R->getValueAsBit("isMoveImm");
  isMoveReg = R->getValueAsBit("isMoveReg");
  isBitcast = R->getValueAsBit("isBitcast");
  isSelect = R->getValueAsBit("isSelect");
  isBarrier = R->getValueAsBit("isBarrier");
  isCall = R->getValueAsBit("isCall");
  isAdd = R->getValueAsBit("isAdd");
  isTrap = R->getValueAsBit("isTrap");
  canFoldAsLoad = R->getValueAsBit("canFoldAsLoad");
  isPredicable = !R->getValueAsBit("isUnpredicable") &&
                 (Operands.isPredicable || R->getValueAsBit("isPredicable"));
  isConvertibleToThreeAddress = R->getValueAsBit("isConvertibleToThreeAddress");
  isCommutable = R->getValueAsBit("isCommutable");
  isTerminator = R->getValueAsBit("isTerminator");
  isReMaterializable = R->getValueAsBit("isReMaterializable");
  hasDelaySlot = R->getValueAsBit("hasDelaySlot");
  usesCustomInserter = R->getValueAsBit("usesCustomInserter");
  hasPostISelHook = R->getValueAsBit("hasPostISelHook");
  hasCtrlDep = R->getValueAsBit("hasCtrlDep");
  isNotDuplicable = R->getValueAsBit("isNotDuplicable");
  isRegSequence = R->getValueAsBit("isRegSequence");
  isExtractSubreg = R->getValueAsBit("isExtractSubreg");
  isInsertSubreg = R->getValueAsBit("isInsertSubreg");
  isConvergent = R->getValueAsBit("isConvergent");
  hasNoSchedulingInfo = R->getValueAsBit("hasNoSchedulingInfo");
  FastISelShouldIgnore = R->getValueAsBit("FastISelShouldIgnore");
  variadicOpsAreDefs = R->getValueAsBit("variadicOpsAreDefs");
  isAuthenticated = R->getValueAsBit("isAuthenticated");

  bool Unset;
  mayLoad = R->getValueAsBitOrUnset("mayLoad", Unset);
  mayLoad_Unset = Unset;
  mayStore = R->getValueAsBitOrUnset("mayStore", Unset);
  mayStore_Unset = Unset;
  mayRaiseFPException = R->getValueAsBit("mayRaiseFPException");
  hasSideEffects = R->getValueAsBitOrUnset("hasSideEffects", Unset);
  hasSideEffects_Unset = Unset;

  isAsCheapAsAMove = R->getValueAsBit("isAsCheapAsAMove");
  hasExtraSrcRegAllocReq = R->getValueAsBit("hasExtraSrcRegAllocReq");
  hasExtraDefRegAllocReq = R->getValueAsBit("hasExtraDefRegAllocReq");
  isCodeGenOnly = R->getValueAsBit("isCodeGenOnly");
  isPseudo = R->getValueAsBit("isPseudo");
  isMeta = R->getValueAsBit("isMeta");
  ImplicitDefs = R->getValueAsListOfDefs("Defs");
  ImplicitUses = R->getValueAsListOfDefs("Uses");

  // This flag is only inferred from the pattern.
  hasChain = false;
  hasChain_Inferred = false;

  // Parse Constraints.
  ParseConstraints(R->getValueAsString("Constraints"), Operands, R);

  // Parse the DisableEncoding field.
  Operands.ProcessDisableEncoding(R->getValueAsString("DisableEncoding"));

  // First check for a ComplexDeprecationPredicate.
  if (R->getValue("ComplexDeprecationPredicate")) {
    HasComplexDeprecationPredicate = true;
    DeprecatedReason =
        std::string(R->getValueAsString("ComplexDeprecationPredicate"));
  } else if (RecordVal *Dep = R->getValue("DeprecatedFeatureMask")) {
    // Check if we have a Subtarget feature mask.
    HasComplexDeprecationPredicate = false;
    DeprecatedReason = Dep->getValue()->getAsString();
  } else {
    // This instruction isn't deprecated.
    HasComplexDeprecationPredicate = false;
    DeprecatedReason = "";
  }
}

/// HasOneImplicitDefWithKnownVT - If the instruction has at least one
/// implicit def and it has a known VT, return the VT, otherwise return
/// MVT::Other.
MVT::SimpleValueType CodeGenInstruction::HasOneImplicitDefWithKnownVT(
    const CodeGenTarget &TargetInfo) const {
  if (ImplicitDefs.empty())
    return MVT::Other;

  // Check to see if the first implicit def has a resolvable type.
  Record *FirstImplicitDef = ImplicitDefs[0];
  assert(FirstImplicitDef->isSubClassOf("Register"));
  const std::vector<ValueTypeByHwMode> &RegVTs =
      TargetInfo.getRegisterVTs(FirstImplicitDef);
  if (RegVTs.size() == 1 && RegVTs[0].isSimple())
    return RegVTs[0].getSimple().SimpleTy;
  return MVT::Other;
}

/// FlattenAsmStringVariants - Flatten the specified AsmString to only
/// include text from the specified variant, returning the new string.
std::string CodeGenInstruction::FlattenAsmStringVariants(StringRef Cur,
                                                         unsigned Variant) {
  std::string Res;

  for (;;) {
    // Find the start of the next variant string.
    size_t VariantsStart = 0;
    for (size_t e = Cur.size(); VariantsStart != e; ++VariantsStart)
      if (Cur[VariantsStart] == '{' &&
          (VariantsStart == 0 ||
           (Cur[VariantsStart - 1] != '$' && Cur[VariantsStart - 1] != '\\')))
        break;

    // Add the prefix to the result.
    Res += Cur.slice(0, VariantsStart);
    if (VariantsStart == Cur.size())
      break;

    ++VariantsStart; // Skip the '{'.

    // Scan to the end of the variants string.
    size_t VariantsEnd = VariantsStart;
    unsigned NestedBraces = 1;
    for (size_t e = Cur.size(); VariantsEnd != e; ++VariantsEnd) {
      if (Cur[VariantsEnd] == '}' && Cur[VariantsEnd - 1] != '\\') {
        if (--NestedBraces == 0)
          break;
      } else if (Cur[VariantsEnd] == '{')
        ++NestedBraces;
    }

    // Select the Nth variant (or empty).
    StringRef Selection = Cur.slice(VariantsStart, VariantsEnd);
    for (unsigned i = 0; i != Variant; ++i)
      Selection = Selection.split('|').second;
    Res += Selection.split('|').first;

    assert(VariantsEnd != Cur.size() &&
           "Unterminated variants in assembly string!");
    Cur = Cur.substr(VariantsEnd + 1);
  }

  return Res;
}

bool CodeGenInstruction::isOperandImpl(StringRef OpListName, unsigned i,
                                       StringRef PropertyName) const {
  DagInit *ConstraintList = TheDef->getValueAsDag(OpListName);
  if (!ConstraintList || i >= ConstraintList->getNumArgs())
    return false;

  DefInit *Constraint = dyn_cast<DefInit>(ConstraintList->getArg(i));
  if (!Constraint)
    return false;

  return Constraint->getDef()->isSubClassOf("TypedOperand") &&
         Constraint->getDef()->getValueAsBit(PropertyName);
}
