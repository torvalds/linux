//===- PatternParser.cpp ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Common/GlobalISel/PatternParser.h"
#include "Basic/CodeGenIntrinsics.h"
#include "Common/CodeGenTarget.h"
#include "Common/GlobalISel/CombinerUtils.h"
#include "Common/GlobalISel/Patterns.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"

namespace llvm {
namespace gi {
static constexpr StringLiteral MIFlagsEnumClassName = "MIFlagEnum";

namespace {
class PrettyStackTraceParse : public PrettyStackTraceEntry {
  const Record &Def;

public:
  PrettyStackTraceParse(const Record &Def) : Def(Def) {}

  void print(raw_ostream &OS) const override {
    if (Def.isSubClassOf("GICombineRule"))
      OS << "Parsing GICombineRule '" << Def.getName() << '\'';
    else if (Def.isSubClassOf(PatFrag::ClassName))
      OS << "Parsing " << PatFrag::ClassName << " '" << Def.getName() << '\'';
    else
      OS << "Parsing '" << Def.getName() << '\'';
    OS << '\n';
  }
};
} // namespace

bool PatternParser::parsePatternList(
    const DagInit &List,
    function_ref<bool(std::unique_ptr<Pattern>)> ParseAction,
    StringRef Operator, StringRef AnonPatNamePrefix) {
  if (List.getOperatorAsDef(DiagLoc)->getName() != Operator) {
    PrintError(DiagLoc, "Expected " + Operator + " operator");
    return false;
  }

  if (List.getNumArgs() == 0) {
    PrintError(DiagLoc, Operator + " pattern list is empty");
    return false;
  }

  // The match section consists of a list of matchers and predicates. Parse each
  // one and add the equivalent GIMatchDag nodes, predicates, and edges.
  for (unsigned I = 0; I < List.getNumArgs(); ++I) {
    Init *Arg = List.getArg(I);
    std::string Name = List.getArgName(I)
                           ? List.getArgName(I)->getValue().str()
                           : ("__" + AnonPatNamePrefix + "_" + Twine(I)).str();

    if (auto Pat = parseInstructionPattern(*Arg, Name)) {
      if (!ParseAction(std::move(Pat)))
        return false;
      continue;
    }

    if (auto Pat = parseWipMatchOpcodeMatcher(*Arg, Name)) {
      if (!ParseAction(std::move(Pat)))
        return false;
      continue;
    }

    // Parse arbitrary C++ code
    if (const auto *StringI = dyn_cast<StringInit>(Arg)) {
      auto CXXPat = std::make_unique<CXXPattern>(*StringI, insertStrRef(Name));
      if (!ParseAction(std::move(CXXPat)))
        return false;
      continue;
    }

    PrintError(DiagLoc,
               "Failed to parse pattern: '" + Arg->getAsString() + '\'');
    return false;
  }

  return true;
}

static const CodeGenInstruction &
getInstrForIntrinsic(const CodeGenTarget &CGT, const CodeGenIntrinsic *I) {
  StringRef Opc;
  if (I->isConvergent) {
    Opc = I->hasSideEffects ? "G_INTRINSIC_CONVERGENT_W_SIDE_EFFECTS"
                            : "G_INTRINSIC_CONVERGENT";
  } else {
    Opc = I->hasSideEffects ? "G_INTRINSIC_W_SIDE_EFFECTS" : "G_INTRINSIC";
  }

  RecordKeeper &RK = I->TheDef->getRecords();
  return CGT.getInstruction(RK.getDef(Opc));
}

static const CodeGenIntrinsic *getCodeGenIntrinsic(Record *R) {
  // Intrinsics need to have a static lifetime because the match table keeps
  // references to CodeGenIntrinsic objects.
  static DenseMap<const Record *, std::unique_ptr<CodeGenIntrinsic>>
      AllIntrinsics;

  auto &Ptr = AllIntrinsics[R];
  if (!Ptr)
    Ptr = std::make_unique<CodeGenIntrinsic>(R, std::vector<Record *>());
  return Ptr.get();
}

std::unique_ptr<Pattern>
PatternParser::parseInstructionPattern(const Init &Arg, StringRef Name) {
  const DagInit *DagPat = dyn_cast<DagInit>(&Arg);
  if (!DagPat)
    return nullptr;

  std::unique_ptr<InstructionPattern> Pat;
  if (const DagInit *IP = getDagWithOperatorOfSubClass(Arg, "Instruction")) {
    auto &Instr = CGT.getInstruction(IP->getOperatorAsDef(DiagLoc));
    Pat =
        std::make_unique<CodeGenInstructionPattern>(Instr, insertStrRef(Name));
  } else if (const DagInit *IP =
                 getDagWithOperatorOfSubClass(Arg, "Intrinsic")) {
    Record *TheDef = IP->getOperatorAsDef(DiagLoc);
    const CodeGenIntrinsic *Intrin = getCodeGenIntrinsic(TheDef);
    const CodeGenInstruction &Instr = getInstrForIntrinsic(CGT, Intrin);
    Pat =
        std::make_unique<CodeGenInstructionPattern>(Instr, insertStrRef(Name));
    cast<CodeGenInstructionPattern>(*Pat).setIntrinsic(Intrin);
  } else if (const DagInit *PFP =
                 getDagWithOperatorOfSubClass(Arg, PatFrag::ClassName)) {
    const Record *Def = PFP->getOperatorAsDef(DiagLoc);
    const PatFrag *PF = parsePatFrag(Def);
    if (!PF)
      return nullptr; // Already diagnosed by parsePatFrag
    Pat = std::make_unique<PatFragPattern>(*PF, insertStrRef(Name));
  } else if (const DagInit *BP =
                 getDagWithOperatorOfSubClass(Arg, BuiltinPattern::ClassName)) {
    Pat = std::make_unique<BuiltinPattern>(*BP->getOperatorAsDef(DiagLoc),
                                           insertStrRef(Name));
  } else
    return nullptr;

  for (unsigned K = 0; K < DagPat->getNumArgs(); ++K) {
    Init *Arg = DagPat->getArg(K);
    if (auto *DagArg = getDagWithSpecificOperator(*Arg, "MIFlags")) {
      if (!parseInstructionPatternMIFlags(*Pat, DagArg))
        return nullptr;
      continue;
    }

    if (!parseInstructionPatternOperand(*Pat, Arg, DagPat->getArgName(K)))
      return nullptr;
  }

  if (!Pat->checkSemantics(DiagLoc))
    return nullptr;

  return std::move(Pat);
}

std::unique_ptr<Pattern>
PatternParser::parseWipMatchOpcodeMatcher(const Init &Arg, StringRef Name) {
  const DagInit *Matcher = getDagWithSpecificOperator(Arg, "wip_match_opcode");
  if (!Matcher)
    return nullptr;

  if (Matcher->getNumArgs() == 0) {
    PrintError(DiagLoc, "Empty wip_match_opcode");
    return nullptr;
  }

  // Each argument is an opcode that can match.
  auto Result = std::make_unique<AnyOpcodePattern>(insertStrRef(Name));
  for (const auto &Arg : Matcher->getArgs()) {
    Record *OpcodeDef = getDefOfSubClass(*Arg, "Instruction");
    if (OpcodeDef) {
      Result->addOpcode(&CGT.getInstruction(OpcodeDef));
      continue;
    }

    PrintError(DiagLoc, "Arguments to wip_match_opcode must be instructions");
    return nullptr;
  }

  return std::move(Result);
}

bool PatternParser::parseInstructionPatternOperand(InstructionPattern &IP,
                                                   const Init *OpInit,
                                                   const StringInit *OpName) {
  const auto ParseErr = [&]() {
    PrintError(DiagLoc,
               "cannot parse operand '" + OpInit->getAsUnquotedString() + "' ");
    if (OpName)
      PrintNote(DiagLoc,
                "operand name is '" + OpName->getAsUnquotedString() + '\'');
    return false;
  };

  // untyped immediate, e.g. 0
  if (const auto *IntImm = dyn_cast<IntInit>(OpInit)) {
    std::string Name = OpName ? OpName->getAsUnquotedString() : "";
    IP.addOperand(IntImm->getValue(), insertStrRef(Name), PatternType());
    return true;
  }

  // typed immediate, e.g. (i32 0)
  if (const auto *DagOp = dyn_cast<DagInit>(OpInit)) {
    if (DagOp->getNumArgs() != 1)
      return ParseErr();

    const Record *TyDef = DagOp->getOperatorAsDef(DiagLoc);
    auto ImmTy = PatternType::get(DiagLoc, TyDef,
                                  "cannot parse immediate '" +
                                      DagOp->getAsUnquotedString() + '\'');
    if (!ImmTy)
      return false;

    if (!IP.hasAllDefs()) {
      PrintError(DiagLoc, "out operand of '" + IP.getInstName() +
                              "' cannot be an immediate");
      return false;
    }

    const auto *Val = dyn_cast<IntInit>(DagOp->getArg(0));
    if (!Val)
      return ParseErr();

    std::string Name = OpName ? OpName->getAsUnquotedString() : "";
    IP.addOperand(Val->getValue(), insertStrRef(Name), *ImmTy);
    return true;
  }

  // Typed operand e.g. $x/$z in (G_FNEG $x, $z)
  if (auto *DefI = dyn_cast<DefInit>(OpInit)) {
    if (!OpName) {
      PrintError(DiagLoc, "expected an operand name after '" +
                              OpInit->getAsString() + '\'');
      return false;
    }
    const Record *Def = DefI->getDef();
    auto Ty = PatternType::get(DiagLoc, Def, "cannot parse operand type");
    if (!Ty)
      return false;
    IP.addOperand(insertStrRef(OpName->getAsUnquotedString()), *Ty);
    return true;
  }

  // Untyped operand e.g. $x/$z in (G_FNEG $x, $z)
  if (isa<UnsetInit>(OpInit)) {
    assert(OpName && "Unset w/ no OpName?");
    IP.addOperand(insertStrRef(OpName->getAsUnquotedString()), PatternType());
    return true;
  }

  return ParseErr();
}

bool PatternParser::parseInstructionPatternMIFlags(InstructionPattern &IP,
                                                   const DagInit *Op) {
  auto *CGIP = dyn_cast<CodeGenInstructionPattern>(&IP);
  if (!CGIP) {
    PrintError(DiagLoc,
               "matching/writing MIFlags is only allowed on CodeGenInstruction "
               "patterns");
    return false;
  }

  const auto CheckFlagEnum = [&](const Record *R) {
    if (!R->isSubClassOf(MIFlagsEnumClassName)) {
      PrintError(DiagLoc, "'" + R->getName() + "' is not a subclass of '" +
                              MIFlagsEnumClassName + "'");
      return false;
    }

    return true;
  };

  if (CGIP->getMIFlagsInfo()) {
    PrintError(DiagLoc, "MIFlags can only be present once on an instruction");
    return false;
  }

  auto &FI = CGIP->getOrCreateMIFlagsInfo();
  for (unsigned K = 0; K < Op->getNumArgs(); ++K) {
    const Init *Arg = Op->getArg(K);

    // Match/set a flag: (MIFlags FmNoNans)
    if (const auto *Def = dyn_cast<DefInit>(Arg)) {
      const Record *R = Def->getDef();
      if (!CheckFlagEnum(R))
        return false;

      FI.addSetFlag(R);
      continue;
    }

    // Do not match a flag/unset a flag: (MIFlags (not FmNoNans))
    if (const DagInit *NotDag = getDagWithSpecificOperator(*Arg, "not")) {
      for (const Init *NotArg : NotDag->getArgs()) {
        const DefInit *DefArg = dyn_cast<DefInit>(NotArg);
        if (!DefArg) {
          PrintError(DiagLoc, "cannot parse '" + NotArg->getAsUnquotedString() +
                                  "': expected a '" + MIFlagsEnumClassName +
                                  "'");
          return false;
        }

        const Record *R = DefArg->getDef();
        if (!CheckFlagEnum(R))
          return false;

        FI.addUnsetFlag(R);
        continue;
      }

      continue;
    }

    // Copy flags from a matched instruction: (MIFlags $mi)
    if (isa<UnsetInit>(Arg)) {
      FI.addCopyFlag(insertStrRef(Op->getArgName(K)->getAsUnquotedString()));
      continue;
    }
  }

  return true;
}

std::unique_ptr<PatFrag> PatternParser::parsePatFragImpl(const Record *Def) {
  auto StackTrace = PrettyStackTraceParse(*Def);
  if (!Def->isSubClassOf(PatFrag::ClassName))
    return nullptr;

  const DagInit *Ins = Def->getValueAsDag("InOperands");
  if (Ins->getOperatorAsDef(Def->getLoc())->getName() != "ins") {
    PrintError(Def, "expected 'ins' operator for " + PatFrag::ClassName +
                        " in operands list");
    return nullptr;
  }

  const DagInit *Outs = Def->getValueAsDag("OutOperands");
  if (Outs->getOperatorAsDef(Def->getLoc())->getName() != "outs") {
    PrintError(Def, "expected 'outs' operator for " + PatFrag::ClassName +
                        " out operands list");
    return nullptr;
  }

  auto Result = std::make_unique<PatFrag>(*Def);
  if (!parsePatFragParamList(*Outs, [&](StringRef Name, unsigned Kind) {
        Result->addOutParam(insertStrRef(Name), (PatFrag::ParamKind)Kind);
        return true;
      }))
    return nullptr;

  if (!parsePatFragParamList(*Ins, [&](StringRef Name, unsigned Kind) {
        Result->addInParam(insertStrRef(Name), (PatFrag::ParamKind)Kind);
        return true;
      }))
    return nullptr;

  const ListInit *Alts = Def->getValueAsListInit("Alternatives");
  unsigned AltIdx = 0;
  for (const Init *Alt : *Alts) {
    const auto *PatDag = dyn_cast<DagInit>(Alt);
    if (!PatDag) {
      PrintError(Def, "expected dag init for PatFrag pattern alternative");
      return nullptr;
    }

    PatFrag::Alternative &A = Result->addAlternative();
    const auto AddPat = [&](std::unique_ptr<Pattern> Pat) {
      A.Pats.push_back(std::move(Pat));
      return true;
    };

    SaveAndRestore<ArrayRef<SMLoc>> DiagLocSAR(DiagLoc, Def->getLoc());
    if (!parsePatternList(
            *PatDag, AddPat, "pattern",
            /*AnonPatPrefix*/
            (Def->getName() + "_alt" + Twine(AltIdx++) + "_pattern").str()))
      return nullptr;
  }

  if (!Result->buildOperandsTables() || !Result->checkSemantics())
    return nullptr;

  return Result;
}

bool PatternParser::parsePatFragParamList(
    const DagInit &OpsList,
    function_ref<bool(StringRef, unsigned)> ParseAction) {
  for (unsigned K = 0; K < OpsList.getNumArgs(); ++K) {
    const StringInit *Name = OpsList.getArgName(K);
    const Init *Ty = OpsList.getArg(K);

    if (!Name) {
      PrintError(DiagLoc, "all operands must be named'");
      return false;
    }
    const std::string NameStr = Name->getAsUnquotedString();

    PatFrag::ParamKind OpKind;
    if (isSpecificDef(*Ty, "gi_imm"))
      OpKind = PatFrag::PK_Imm;
    else if (isSpecificDef(*Ty, "root"))
      OpKind = PatFrag::PK_Root;
    else if (isa<UnsetInit>(Ty) ||
             isSpecificDef(*Ty, "gi_mo")) // no type = gi_mo.
      OpKind = PatFrag::PK_MachineOperand;
    else {
      PrintError(
          DiagLoc,
          '\'' + NameStr +
              "' operand type was expected to be 'root', 'gi_imm' or 'gi_mo'");
      return false;
    }

    if (!ParseAction(NameStr, (unsigned)OpKind))
      return false;
  }

  return true;
}

const PatFrag *PatternParser::parsePatFrag(const Record *Def) {
  // Cache already parsed PatFrags to avoid doing extra work.
  static DenseMap<const Record *, std::unique_ptr<PatFrag>> ParsedPatFrags;

  auto It = ParsedPatFrags.find(Def);
  if (It != ParsedPatFrags.end()) {
    SeenPatFrags.insert(It->second.get());
    return It->second.get();
  }

  std::unique_ptr<PatFrag> NewPatFrag = parsePatFragImpl(Def);
  if (!NewPatFrag) {
    PrintError(Def, "Could not parse " + PatFrag::ClassName + " '" +
                        Def->getName() + "'");
    // Put a nullptr in the map so we don't attempt parsing this again.
    ParsedPatFrags[Def] = nullptr;
    return nullptr;
  }

  const auto *Res = NewPatFrag.get();
  ParsedPatFrags[Def] = std::move(NewPatFrag);
  SeenPatFrags.insert(Res);
  return Res;
}

} // namespace gi
} // namespace llvm
