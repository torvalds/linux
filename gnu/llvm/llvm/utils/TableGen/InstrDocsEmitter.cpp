//===- InstrDocsEmitter.cpp - Opcode Documentation Generator --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// InstrDocsEmitter generates restructured text documentation for the opcodes
// that can be used by MachineInstr. For each opcode, the documentation lists:
// * Opcode name
// * Assembly string
// * Flags (e.g. mayLoad, isBranch, ...)
// * Operands, including type and name
// * Operand constraints
// * Implicit register uses & defs
// * Predicates
//
//===----------------------------------------------------------------------===//

#include "Common/CodeGenDAGPatterns.h"
#include "Common/CodeGenInstruction.h"
#include "Common/CodeGenTarget.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <string>
#include <vector>

using namespace llvm;

static void writeTitle(StringRef Str, raw_ostream &OS, char Kind = '-') {
  OS << std::string(Str.size(), Kind) << "\n"
     << Str << "\n"
     << std::string(Str.size(), Kind) << "\n";
}

static void writeHeader(StringRef Str, raw_ostream &OS, char Kind = '-') {
  OS << Str << "\n" << std::string(Str.size(), Kind) << "\n";
}

static std::string escapeForRST(StringRef Str) {
  std::string Result;
  Result.reserve(Str.size() + 4);
  for (char C : Str) {
    switch (C) {
    // We want special characters to be shown as their C escape codes.
    case '\n':
      Result += "\\n";
      break;
    case '\t':
      Result += "\\t";
      break;
    // Underscore at the end of a line has a special meaning in rst.
    case '_':
      Result += "\\_";
      break;
    default:
      Result += C;
    }
  }
  return Result;
}

static void EmitInstrDocs(RecordKeeper &RK, raw_ostream &OS) {
  CodeGenDAGPatterns CDP(RK);
  CodeGenTarget &Target = CDP.getTargetInfo();
  unsigned VariantCount = Target.getAsmParserVariantCount();

  // Page title.
  std::string Title = std::string(Target.getName());
  Title += " Instructions";
  writeTitle(Title, OS);
  OS << "\n";

  for (const CodeGenInstruction *II : Target.getInstructionsByEnumValue()) {
    Record *Inst = II->TheDef;

    // Don't print the target-independent instructions.
    if (II->Namespace == "TargetOpcode")
      continue;

    // Heading (instruction name).
    writeHeader(escapeForRST(Inst->getName()), OS, '=');
    OS << "\n";

    // Assembly string(s).
    if (!II->AsmString.empty()) {
      for (unsigned VarNum = 0; VarNum < VariantCount; ++VarNum) {
        Record *AsmVariant = Target.getAsmParserVariant(VarNum);
        OS << "Assembly string";
        if (VariantCount != 1)
          OS << " (" << AsmVariant->getValueAsString("Name") << ")";
        std::string AsmString =
            CodeGenInstruction::FlattenAsmStringVariants(II->AsmString, VarNum);
        // We trim spaces at each end of the asm string because rst needs the
        // formatting backticks to be next to a non-whitespace character.
        OS << ": ``" << escapeForRST(StringRef(AsmString).trim(" "))
           << "``\n\n";
      }
    }

    // Boolean flags.
    std::vector<const char *> FlagStrings;
#define xstr(s) str(s)
#define str(s) #s
#define FLAG(f)                                                                \
  if (II->f) {                                                                 \
    FlagStrings.push_back(str(f));                                             \
  }
    FLAG(isReturn)
    FLAG(isEHScopeReturn)
    FLAG(isBranch)
    FLAG(isIndirectBranch)
    FLAG(isCompare)
    FLAG(isMoveImm)
    FLAG(isBitcast)
    FLAG(isSelect)
    FLAG(isBarrier)
    FLAG(isCall)
    FLAG(isAdd)
    FLAG(isTrap)
    FLAG(canFoldAsLoad)
    FLAG(mayLoad)
    // FLAG(mayLoad_Unset) // Deliberately omitted.
    FLAG(mayStore)
    // FLAG(mayStore_Unset) // Deliberately omitted.
    FLAG(isPredicable)
    FLAG(isConvertibleToThreeAddress)
    FLAG(isCommutable)
    FLAG(isTerminator)
    FLAG(isReMaterializable)
    FLAG(hasDelaySlot)
    FLAG(usesCustomInserter)
    FLAG(hasPostISelHook)
    FLAG(hasCtrlDep)
    FLAG(isNotDuplicable)
    FLAG(hasSideEffects)
    // FLAG(hasSideEffects_Unset) // Deliberately omitted.
    FLAG(isAsCheapAsAMove)
    FLAG(hasExtraSrcRegAllocReq)
    FLAG(hasExtraDefRegAllocReq)
    FLAG(isCodeGenOnly)
    FLAG(isPseudo)
    FLAG(isRegSequence)
    FLAG(isExtractSubreg)
    FLAG(isInsertSubreg)
    FLAG(isConvergent)
    FLAG(hasNoSchedulingInfo)
    FLAG(variadicOpsAreDefs)
    FLAG(isAuthenticated)
    if (!FlagStrings.empty()) {
      OS << "Flags: ";
      ListSeparator LS;
      for (auto FlagString : FlagStrings)
        OS << LS << "``" << FlagString << "``";
      OS << "\n\n";
    }

    // Operands.
    for (unsigned i = 0; i < II->Operands.size(); ++i) {
      bool IsDef = i < II->Operands.NumDefs;
      auto Op = II->Operands[i];

      if (Op.MINumOperands > 1) {
        // This operand corresponds to multiple operands on the
        // MachineInstruction, so print all of them, showing the types and
        // names of both the compound operand and the basic operands it
        // contains.
        for (unsigned SubOpIdx = 0; SubOpIdx < Op.MINumOperands; ++SubOpIdx) {
          Record *SubRec =
              cast<DefInit>(Op.MIOperandInfo->getArg(SubOpIdx))->getDef();
          StringRef SubOpName = Op.MIOperandInfo->getArgNameStr(SubOpIdx);
          StringRef SubOpTypeName = SubRec->getName();

          OS << "* " << (IsDef ? "DEF" : "USE") << " ``" << Op.Rec->getName()
             << "/" << SubOpTypeName << ":$" << Op.Name << ".";
          // Not all sub-operands are named, make up a name for these.
          if (SubOpName.empty())
            OS << "anon" << SubOpIdx;
          else
            OS << SubOpName;
          OS << "``\n\n";
        }
      } else {
        // The operand corresponds to only one MachineInstruction operand.
        OS << "* " << (IsDef ? "DEF" : "USE") << " ``" << Op.Rec->getName()
           << ":$" << Op.Name << "``\n\n";
      }
    }

    // Constraints.
    StringRef Constraints = Inst->getValueAsString("Constraints");
    if (!Constraints.empty()) {
      OS << "Constraints: ``" << Constraints << "``\n\n";
    }

    // Implicit definitions.
    if (!II->ImplicitDefs.empty()) {
      OS << "Implicit defs: ";
      ListSeparator LS;
      for (Record *Def : II->ImplicitDefs)
        OS << LS << "``" << Def->getName() << "``";
      OS << "\n\n";
    }

    // Implicit uses.
    if (!II->ImplicitUses.empty()) {
      OS << "Implicit uses: ";
      ListSeparator LS;
      for (Record *Use : II->ImplicitUses)
        OS << LS << "``" << Use->getName() << "``";
      OS << "\n\n";
    }

    // Predicates.
    std::vector<Record *> Predicates =
        II->TheDef->getValueAsListOfDefs("Predicates");
    if (!Predicates.empty()) {
      OS << "Predicates: ";
      ListSeparator LS;
      for (Record *P : Predicates)
        OS << LS << "``" << P->getName() << "``";
      OS << "\n\n";
    }
  }
}

static TableGen::Emitter::Opt X("gen-instr-docs", EmitInstrDocs,
                                "Generate instruction documentation");
