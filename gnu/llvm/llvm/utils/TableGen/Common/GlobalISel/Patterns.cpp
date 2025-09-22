//===- Patterns.cpp --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Patterns.h"
#include "Basic/CodeGenIntrinsics.h"
#include "CXXPredicates.h"
#include "CodeExpander.h"
#include "CodeExpansions.h"
#include "Common/CodeGenInstruction.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"

namespace llvm {
namespace gi {

//===- PatternType --------------------------------------------------------===//

std::optional<PatternType> PatternType::get(ArrayRef<SMLoc> DiagLoc,
                                            const Record *R, Twine DiagCtx) {
  assert(R);
  if (R->isSubClassOf("ValueType")) {
    PatternType PT(PT_ValueType);
    PT.Data.Def = R;
    return PT;
  }

  if (R->isSubClassOf(TypeOfClassName)) {
    auto RawOpName = R->getValueAsString("OpName");
    if (!RawOpName.starts_with("$")) {
      PrintError(DiagLoc, DiagCtx + ": invalid operand name format '" +
                              RawOpName + "' in " + TypeOfClassName +
                              ": expected '$' followed by an operand name");
      return std::nullopt;
    }

    PatternType PT(PT_TypeOf);
    PT.Data.Str = RawOpName.drop_front(1);
    return PT;
  }

  PrintError(DiagLoc, DiagCtx + ": unknown type '" + R->getName() + "'");
  return std::nullopt;
}

PatternType PatternType::getTypeOf(StringRef OpName) {
  PatternType PT(PT_TypeOf);
  PT.Data.Str = OpName;
  return PT;
}

StringRef PatternType::getTypeOfOpName() const {
  assert(isTypeOf());
  return Data.Str;
}

const Record *PatternType::getLLTRecord() const {
  assert(isLLT());
  return Data.Def;
}

bool PatternType::operator==(const PatternType &Other) const {
  if (Kind != Other.Kind)
    return false;

  switch (Kind) {
  case PT_None:
    return true;
  case PT_ValueType:
    return Data.Def == Other.Data.Def;
  case PT_TypeOf:
    return Data.Str == Other.Data.Str;
  }

  llvm_unreachable("Unknown Type Kind");
}

std::string PatternType::str() const {
  switch (Kind) {
  case PT_None:
    return "";
  case PT_ValueType:
    return Data.Def->getName().str();
  case PT_TypeOf:
    return (TypeOfClassName + "<$" + getTypeOfOpName() + ">").str();
  }

  llvm_unreachable("Unknown type!");
}

//===- Pattern ------------------------------------------------------------===//

void Pattern::dump() const { return print(dbgs()); }

const char *Pattern::getKindName() const {
  switch (Kind) {
  case K_AnyOpcode:
    return "AnyOpcodePattern";
  case K_CXX:
    return "CXXPattern";
  case K_CodeGenInstruction:
    return "CodeGenInstructionPattern";
  case K_PatFrag:
    return "PatFragPattern";
  case K_Builtin:
    return "BuiltinPattern";
  }

  llvm_unreachable("unknown pattern kind!");
}

void Pattern::printImpl(raw_ostream &OS, bool PrintName,
                        function_ref<void()> ContentPrinter) const {
  OS << "(" << getKindName() << " ";
  if (PrintName)
    OS << "name:" << getName() << " ";
  ContentPrinter();
  OS << ")";
}

//===- AnyOpcodePattern ---------------------------------------------------===//

void AnyOpcodePattern::print(raw_ostream &OS, bool PrintName) const {
  printImpl(OS, PrintName, [&OS, this]() {
    OS << "["
       << join(map_range(Insts,
                         [](const auto *I) { return I->TheDef->getName(); }),
               ", ")
       << "]";
  });
}

//===- CXXPattern ---------------------------------------------------------===//

CXXPattern::CXXPattern(const StringInit &Code, StringRef Name)
    : CXXPattern(Code.getAsUnquotedString(), Name) {}

const CXXPredicateCode &
CXXPattern::expandCode(const CodeExpansions &CE, ArrayRef<SMLoc> Locs,
                       function_ref<void(raw_ostream &)> AddComment) const {
  assert(!IsApply && "'apply' CXX patterns should be handled differently!");

  std::string Result;
  raw_string_ostream OS(Result);

  if (AddComment)
    AddComment(OS);

  CodeExpander Expander(RawCode, CE, Locs, /*ShowExpansions*/ false);
  Expander.emit(OS);
  return CXXPredicateCode::getMatchCode(std::move(Result));
}

void CXXPattern::print(raw_ostream &OS, bool PrintName) const {
  printImpl(OS, PrintName, [&OS, this] {
    OS << (IsApply ? "apply" : "match") << " code:\"";
    printEscapedString(getRawCode(), OS);
    OS << "\"";
  });
}

//===- InstructionOperand -------------------------------------------------===//

std::string InstructionOperand::describe() const {
  if (!hasImmValue())
    return "MachineOperand $" + getOperandName().str() + "";
  std::string Str = "imm " + std::to_string(getImmValue());
  if (isNamedImmediate())
    Str += ":$" + getOperandName().str() + "";
  return Str;
}

void InstructionOperand::print(raw_ostream &OS) const {
  if (isDef())
    OS << "<def>";

  bool NeedsColon = true;
  if (Type) {
    if (hasImmValue())
      OS << "(" << Type.str() << " " << getImmValue() << ")";
    else
      OS << Type.str();
  } else if (hasImmValue())
    OS << getImmValue();
  else
    NeedsColon = false;

  if (isNamedOperand())
    OS << (NeedsColon ? ":" : "") << "$" << getOperandName();
}

void InstructionOperand::dump() const { return print(dbgs()); }

//===- InstructionPattern -------------------------------------------------===//

bool InstructionPattern::diagnoseAllSpecialTypes(ArrayRef<SMLoc> Loc,
                                                 Twine Msg) const {
  bool HasDiag = false;
  for (const auto &[Idx, Op] : enumerate(operands())) {
    if (Op.getType().isSpecial()) {
      PrintError(Loc, Msg);
      PrintNote(Loc, "operand " + Twine(Idx) + " of '" + getName() +
                         "' has type '" + Op.getType().str() + "'");
      HasDiag = true;
    }
  }
  return HasDiag;
}

void InstructionPattern::reportUnreachable(ArrayRef<SMLoc> Locs) const {
  PrintError(Locs, "pattern '" + getName() + "' ('" + getInstName() +
                       "') is unreachable from the pattern root!");
}

bool InstructionPattern::checkSemantics(ArrayRef<SMLoc> Loc) {
  unsigned NumExpectedOperands = getNumInstOperands();

  if (isVariadic()) {
    if (Operands.size() < NumExpectedOperands) {
      PrintError(Loc, +"'" + getInstName() + "' expected at least " +
                          Twine(NumExpectedOperands) + " operands, got " +
                          Twine(Operands.size()));
      return false;
    }
  } else if (NumExpectedOperands != Operands.size()) {
    PrintError(Loc, +"'" + getInstName() + "' expected " +
                        Twine(NumExpectedOperands) + " operands, got " +
                        Twine(Operands.size()));
    return false;
  }

  unsigned OpIdx = 0;
  unsigned NumDefs = getNumInstDefs();
  for (auto &Op : Operands)
    Op.setIsDef(OpIdx++ < NumDefs);

  return true;
}

void InstructionPattern::print(raw_ostream &OS, bool PrintName) const {
  printImpl(OS, PrintName, [&OS, this] {
    OS << getInstName() << " operands:[";
    StringRef Sep;
    for (const auto &Op : Operands) {
      OS << Sep;
      Op.print(OS);
      Sep = ", ";
    }
    OS << "]";

    printExtras(OS);
  });
}

//===- OperandTable -------------------------------------------------------===//

bool OperandTable::addPattern(InstructionPattern *P,
                              function_ref<void(StringRef)> DiagnoseRedef) {
  for (const auto &Op : P->named_operands()) {
    StringRef OpName = Op.getOperandName();

    // We always create an entry in the OperandTable, even for uses.
    // Uses of operands that don't have a def (= live-ins) will remain with a
    // nullptr as the Def.
    //
    // This allows us tell whether an operand exists in a pattern or not. If
    // there is no entry for it, it doesn't exist, if there is an entry, it's
    // used/def'd at least once.
    auto &Def = Table[OpName];

    if (!Op.isDef())
      continue;

    if (Def) {
      DiagnoseRedef(OpName);
      return false;
    }

    Def = P;
  }

  return true;
}

void OperandTable::print(raw_ostream &OS, StringRef Name,
                         StringRef Indent) const {
  OS << Indent << "(OperandTable ";
  if (!Name.empty())
    OS << Name << " ";
  if (Table.empty()) {
    OS << "<empty>)\n";
    return;
  }

  SmallVector<StringRef, 0> Keys(Table.keys());
  sort(Keys);

  OS << '\n';
  for (const auto &Key : Keys) {
    const auto *Def = Table.at(Key);
    OS << Indent << "  " << Key << " -> "
       << (Def ? Def->getName() : "<live-in>") << '\n';
  }
  OS << Indent << ")\n";
}

void OperandTable::dump() const { print(dbgs()); }

//===- MIFlagsInfo --------------------------------------------------------===//

void MIFlagsInfo::addSetFlag(const Record *R) {
  SetF.insert(R->getValueAsString("EnumName"));
}

void MIFlagsInfo::addUnsetFlag(const Record *R) {
  UnsetF.insert(R->getValueAsString("EnumName"));
}

void MIFlagsInfo::addCopyFlag(StringRef InstName) { CopyF.insert(InstName); }

//===- CodeGenInstructionPattern ------------------------------------------===//

bool CodeGenInstructionPattern::is(StringRef OpcodeName) const {
  return I.TheDef->getName() == OpcodeName;
}

bool CodeGenInstructionPattern::isVariadic() const {
  return !isIntrinsic() && I.Operands.isVariadic;
}

bool CodeGenInstructionPattern::hasVariadicDefs() const {
  // Note: we cannot use variadicOpsAreDefs, it's not set for
  // GenericInstructions.
  if (!isVariadic())
    return false;

  if (I.variadicOpsAreDefs)
    return true;

  DagInit *OutOps = I.TheDef->getValueAsDag("OutOperandList");
  if (OutOps->arg_empty())
    return false;

  auto *LastArgTy = dyn_cast<DefInit>(OutOps->getArg(OutOps->arg_size() - 1));
  return LastArgTy && LastArgTy->getDef()->getName() == "variable_ops";
}

unsigned CodeGenInstructionPattern::getNumInstDefs() const {
  if (isIntrinsic())
    return IntrinInfo->IS.RetTys.size();

  if (!isVariadic() || !hasVariadicDefs())
    return I.Operands.NumDefs;
  unsigned NumOuts = I.Operands.size() - I.Operands.NumDefs;
  assert(Operands.size() > NumOuts);
  return std::max<unsigned>(I.Operands.NumDefs, Operands.size() - NumOuts);
}

unsigned CodeGenInstructionPattern::getNumInstOperands() const {
  if (isIntrinsic())
    return IntrinInfo->IS.RetTys.size() + IntrinInfo->IS.ParamTys.size();

  unsigned NumCGIOps = I.Operands.size();
  return isVariadic() ? std::max<unsigned>(NumCGIOps, Operands.size())
                      : NumCGIOps;
}

MIFlagsInfo &CodeGenInstructionPattern::getOrCreateMIFlagsInfo() {
  if (!FI)
    FI = std::make_unique<MIFlagsInfo>();
  return *FI;
}

StringRef CodeGenInstructionPattern::getInstName() const {
  return I.TheDef->getName();
}

void CodeGenInstructionPattern::printExtras(raw_ostream &OS) const {
  if (isIntrinsic())
    OS << " intrinsic(@" << IntrinInfo->Name << ")";

  if (!FI)
    return;

  OS << " (MIFlags";
  if (!FI->set_flags().empty())
    OS << " (set " << join(FI->set_flags(), ", ") << ")";
  if (!FI->unset_flags().empty())
    OS << " (unset " << join(FI->unset_flags(), ", ") << ")";
  if (!FI->copy_flags().empty())
    OS << " (copy " << join(FI->copy_flags(), ", ") << ")";
  OS << ')';
}

//===- OperandTypeChecker -------------------------------------------------===//

bool OperandTypeChecker::check(
    InstructionPattern &P,
    std::function<bool(const PatternType &)> VerifyTypeOfOperand) {
  Pats.push_back(&P);

  for (auto &Op : P.operands()) {
    const auto Ty = Op.getType();
    if (!Ty)
      continue;

    if (Ty.isTypeOf() && !VerifyTypeOfOperand(Ty))
      return false;

    if (!Op.isNamedOperand())
      continue;

    StringRef OpName = Op.getOperandName();
    auto &Info = Types[OpName];
    if (!Info.Type) {
      Info.Type = Ty;
      Info.PrintTypeSrcNote = [this, OpName, Ty, &P]() {
        PrintSeenWithTypeIn(P, OpName, Ty);
      };
      continue;
    }

    if (Info.Type != Ty) {
      PrintError(DiagLoc, "conflicting types for operand '" +
                              Op.getOperandName() + "': '" + Info.Type.str() +
                              "' vs '" + Ty.str() + "'");
      PrintSeenWithTypeIn(P, OpName, Ty);
      Info.PrintTypeSrcNote();
      return false;
    }
  }

  return true;
}

void OperandTypeChecker::propagateTypes() {
  for (auto *Pat : Pats) {
    for (auto &Op : Pat->named_operands()) {
      if (auto &Info = Types[Op.getOperandName()]; Info.Type)
        Op.setType(Info.Type);
    }
  }
}

void OperandTypeChecker::PrintSeenWithTypeIn(InstructionPattern &P,
                                             StringRef OpName,
                                             PatternType Ty) const {
  PrintNote(DiagLoc, "'" + OpName + "' seen with type '" + Ty.str() + "' in '" +
                         P.getName() + "'");
}

StringRef PatFrag::getParamKindStr(ParamKind OK) {
  switch (OK) {
  case PK_Root:
    return "root";
  case PK_MachineOperand:
    return "machine_operand";
  case PK_Imm:
    return "imm";
  }

  llvm_unreachable("Unknown operand kind!");
}

//===- PatFrag -----------------------------------------------------------===//

PatFrag::PatFrag(const Record &Def) : Def(Def) {
  assert(Def.isSubClassOf(ClassName));
}

StringRef PatFrag::getName() const { return Def.getName(); }

ArrayRef<SMLoc> PatFrag::getLoc() const { return Def.getLoc(); }

void PatFrag::addInParam(StringRef Name, ParamKind Kind) {
  Params.emplace_back(Param{Name, Kind});
}

iterator_range<PatFrag::ParamIt> PatFrag::in_params() const {
  return {Params.begin() + NumOutParams, Params.end()};
}

void PatFrag::addOutParam(StringRef Name, ParamKind Kind) {
  assert(NumOutParams == Params.size() &&
         "Adding out-param after an in-param!");
  Params.emplace_back(Param{Name, Kind});
  ++NumOutParams;
}

iterator_range<PatFrag::ParamIt> PatFrag::out_params() const {
  return {Params.begin(), Params.begin() + NumOutParams};
}

unsigned PatFrag::num_roots() const {
  return count_if(out_params(),
                  [&](const auto &P) { return P.Kind == PK_Root; });
}

unsigned PatFrag::getParamIdx(StringRef Name) const {
  for (const auto &[Idx, Op] : enumerate(Params)) {
    if (Op.Name == Name)
      return Idx;
  }

  return -1;
}

bool PatFrag::checkSemantics() {
  for (const auto &Alt : Alts) {
    for (const auto &Pat : Alt.Pats) {
      switch (Pat->getKind()) {
      case Pattern::K_AnyOpcode:
        PrintError("wip_match_opcode cannot be used in " + ClassName);
        return false;
      case Pattern::K_Builtin:
        PrintError("Builtin instructions cannot be used in " + ClassName);
        return false;
      case Pattern::K_CXX:
        continue;
      case Pattern::K_CodeGenInstruction:
        if (cast<CodeGenInstructionPattern>(Pat.get())->diagnoseAllSpecialTypes(
                Def.getLoc(), PatternType::SpecialTyClassName +
                                  " is not supported in " + ClassName))
          return false;
        continue;
      case Pattern::K_PatFrag:
        // TODO: It's just that the emitter doesn't handle it but technically
        // there is no reason why we can't. We just have to be careful with
        // operand mappings, it could get complex.
        PrintError("nested " + ClassName + " are not supported");
        return false;
      }
    }
  }

  StringSet<> SeenOps;
  for (const auto &Op : in_params()) {
    if (SeenOps.count(Op.Name)) {
      PrintError("duplicate parameter '" + Op.Name + "'");
      return false;
    }

    // Check this operand is NOT defined in any alternative's patterns.
    for (const auto &Alt : Alts) {
      if (Alt.OpTable.lookup(Op.Name).Def) {
        PrintError("input parameter '" + Op.Name + "' cannot be redefined!");
        return false;
      }
    }

    if (Op.Kind == PK_Root) {
      PrintError("input parameterr '" + Op.Name + "' cannot be a root!");
      return false;
    }

    SeenOps.insert(Op.Name);
  }

  for (const auto &Op : out_params()) {
    if (Op.Kind != PK_Root && Op.Kind != PK_MachineOperand) {
      PrintError("output parameter '" + Op.Name +
                 "' must be 'root' or 'gi_mo'");
      return false;
    }

    if (SeenOps.count(Op.Name)) {
      PrintError("duplicate parameter '" + Op.Name + "'");
      return false;
    }

    // Check this operand is defined in all alternative's patterns.
    for (const auto &Alt : Alts) {
      const auto *OpDef = Alt.OpTable.getDef(Op.Name);
      if (!OpDef) {
        PrintError("output parameter '" + Op.Name +
                   "' must be defined by all alternative patterns in '" +
                   Def.getName() + "'");
        return false;
      }

      if (Op.Kind == PK_Root && OpDef->getNumInstDefs() != 1) {
        // The instruction that defines the root must have a single def.
        // Otherwise we'd need to support multiple roots and it gets messy.
        //
        // e.g. this is not supported:
        //   (pattern (G_UNMERGE_VALUES $x, $root, $vec))
        PrintError("all instructions that define root '" + Op.Name + "' in '" +
                   Def.getName() + "' can only have a single output operand");
        return false;
      }
    }

    SeenOps.insert(Op.Name);
  }

  if (num_out_params() != 0 && num_roots() == 0) {
    PrintError(ClassName + " must have one root in its 'out' operands");
    return false;
  }

  if (num_roots() > 1) {
    PrintError(ClassName + " can only have one root");
    return false;
  }

  // TODO: find unused params

  const auto CheckTypeOf = [&](const PatternType &) -> bool {
    llvm_unreachable("GITypeOf should have been rejected earlier!");
  };

  // Now, typecheck all alternatives.
  for (auto &Alt : Alts) {
    OperandTypeChecker OTC(Def.getLoc());
    for (auto &Pat : Alt.Pats) {
      if (auto *IP = dyn_cast<InstructionPattern>(Pat.get())) {
        if (!OTC.check(*IP, CheckTypeOf))
          return false;
      }
    }
    OTC.propagateTypes();
  }

  return true;
}

bool PatFrag::handleUnboundInParam(StringRef ParamName, StringRef ArgName,
                                   ArrayRef<SMLoc> DiagLoc) const {
  // The parameter must be a live-in of all alternatives for this to work.
  // Otherwise, we risk having unbound parameters being used (= crashes).
  //
  // Examples:
  //
  // in (ins $y), (patterns (G_FNEG $dst, $y), "return matchFnegOp(${y})")
  //    even if $y is unbound, we'll lazily bind it when emitting the G_FNEG.
  //
  // in (ins $y), (patterns "return matchFnegOp(${y})")
  //    if $y is unbound when this fragment is emitted, C++ code expansion will
  //    fail.
  for (const auto &Alt : Alts) {
    auto &OT = Alt.OpTable;
    if (!OT.lookup(ParamName).Found) {
      llvm::PrintError(DiagLoc, "operand '" + ArgName + "' (for parameter '" +
                                    ParamName + "' of '" + getName() +
                                    "') cannot be unbound");
      PrintNote(
          DiagLoc,
          "one or more alternatives of '" + getName() + "' do not bind '" +
              ParamName +
              "' to an instruction operand; either use a bound operand or "
              "ensure '" +
              Def.getName() + "' binds '" + ParamName +
              "' in all alternatives");
      return false;
    }
  }

  return true;
}

bool PatFrag::buildOperandsTables() {
  // enumerate(...) doesn't seem to allow lvalues so we need to count the old
  // way.
  unsigned Idx = 0;

  const auto DiagnoseRedef = [this, &Idx](StringRef OpName) {
    PrintError("Operand '" + OpName +
               "' is defined multiple times in patterns of alternative #" +
               std::to_string(Idx));
  };

  for (auto &Alt : Alts) {
    for (auto &Pat : Alt.Pats) {
      auto *IP = dyn_cast<InstructionPattern>(Pat.get());
      if (!IP)
        continue;

      if (!Alt.OpTable.addPattern(IP, DiagnoseRedef))
        return false;
    }

    ++Idx;
  }

  return true;
}

void PatFrag::print(raw_ostream &OS, StringRef Indent) const {
  OS << Indent << "(PatFrag name:" << getName() << '\n';
  if (!in_params().empty()) {
    OS << Indent << "  (ins ";
    printParamsList(OS, in_params());
    OS << ")\n";
  }

  if (!out_params().empty()) {
    OS << Indent << "  (outs ";
    printParamsList(OS, out_params());
    OS << ")\n";
  }

  // TODO: Dump OperandTable as well.
  OS << Indent << "  (alternatives [\n";
  for (const auto &Alt : Alts) {
    OS << Indent << "    [\n";
    for (const auto &Pat : Alt.Pats) {
      OS << Indent << "      ";
      Pat->print(OS, /*PrintName=*/true);
      OS << ",\n";
    }
    OS << Indent << "    ],\n";
  }
  OS << Indent << "  ])\n";

  OS << Indent << ')';
}

void PatFrag::dump() const { print(dbgs()); }

void PatFrag::printParamsList(raw_ostream &OS, iterator_range<ParamIt> Params) {
  OS << '['
     << join(map_range(Params,
                       [](auto &O) {
                         return (O.Name + ":" + getParamKindStr(O.Kind)).str();
                       }),
             ", ")
     << ']';
}

void PatFrag::PrintError(Twine Msg) const { llvm::PrintError(&Def, Msg); }

ArrayRef<InstructionOperand> PatFragPattern::getApplyDefsNeeded() const {
  assert(PF.num_roots() == 1);
  // Only roots need to be redef.
  for (auto [Idx, Param] : enumerate(PF.out_params())) {
    if (Param.Kind == PatFrag::PK_Root)
      return getOperand(Idx);
  }
  llvm_unreachable("root not found!");
}

//===- PatFragPattern -----------------------------------------------------===//

bool PatFragPattern::checkSemantics(ArrayRef<SMLoc> DiagLoc) {
  if (!InstructionPattern::checkSemantics(DiagLoc))
    return false;

  for (const auto &[Idx, Op] : enumerate(Operands)) {
    switch (PF.getParam(Idx).Kind) {
    case PatFrag::PK_Imm:
      if (!Op.hasImmValue()) {
        PrintError(DiagLoc, "expected operand " + std::to_string(Idx) +
                                " of '" + getInstName() +
                                "' to be an immediate; got " + Op.describe());
        return false;
      }
      if (Op.isNamedImmediate()) {
        PrintError(DiagLoc, "operand " + std::to_string(Idx) + " of '" +
                                getInstName() +
                                "' cannot be a named immediate");
        return false;
      }
      break;
    case PatFrag::PK_Root:
    case PatFrag::PK_MachineOperand:
      if (!Op.isNamedOperand() || Op.isNamedImmediate()) {
        PrintError(DiagLoc, "expected operand " + std::to_string(Idx) +
                                " of '" + getInstName() +
                                "' to be a MachineOperand; got " +
                                Op.describe());
        return false;
      }
      break;
    }
  }

  return true;
}

bool PatFragPattern::mapInputCodeExpansions(const CodeExpansions &ParentCEs,
                                            CodeExpansions &PatFragCEs,
                                            ArrayRef<SMLoc> DiagLoc) const {
  for (const auto &[Idx, Op] : enumerate(operands())) {
    StringRef ParamName = PF.getParam(Idx).Name;

    // Operands to a PFP can only be named, or be an immediate, but not a named
    // immediate.
    assert(!Op.isNamedImmediate());

    if (Op.isNamedOperand()) {
      StringRef ArgName = Op.getOperandName();
      // Map it only if it's been defined.
      auto It = ParentCEs.find(ArgName);
      if (It == ParentCEs.end()) {
        if (!PF.handleUnboundInParam(ParamName, ArgName, DiagLoc))
          return false;
      } else
        PatFragCEs.declare(ParamName, It->second);
      continue;
    }

    if (Op.hasImmValue()) {
      PatFragCEs.declare(ParamName, std::to_string(Op.getImmValue()));
      continue;
    }

    llvm_unreachable("Unknown Operand Type!");
  }

  return true;
}

//===- BuiltinPattern -----------------------------------------------------===//

BuiltinPattern::BuiltinInfo BuiltinPattern::getBuiltinInfo(const Record &Def) {
  assert(Def.isSubClassOf(ClassName));

  StringRef Name = Def.getName();
  for (const auto &KBI : KnownBuiltins) {
    if (KBI.DefName == Name)
      return KBI;
  }

  PrintFatalError(Def.getLoc(),
                  "Unimplemented " + ClassName + " def '" + Name + "'");
}

bool BuiltinPattern::checkSemantics(ArrayRef<SMLoc> Loc) {
  if (!InstructionPattern::checkSemantics(Loc))
    return false;

  // For now all builtins just take names, no immediates.
  for (const auto &[Idx, Op] : enumerate(operands())) {
    if (!Op.isNamedOperand() || Op.isNamedImmediate()) {
      PrintError(Loc, "expected operand " + std::to_string(Idx) + " of '" +
                          getInstName() + "' to be a name");
      return false;
    }
  }

  return true;
}

} // namespace gi
} // namespace llvm
