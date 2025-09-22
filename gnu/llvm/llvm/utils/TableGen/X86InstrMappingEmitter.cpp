//========- utils/TableGen/X86InstrMappingEmitter.cpp - X86 backend-*- C++ -*-//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// This tablegen backend is responsible for emitting the X86 backend
/// instruction mapping.
///
//===----------------------------------------------------------------------===//

#include "Common/CodeGenInstruction.h"
#include "Common/CodeGenTarget.h"
#include "X86RecognizableInstr.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <map>
#include <set>

using namespace llvm;
using namespace X86Disassembler;

namespace {

class X86InstrMappingEmitter {
  RecordKeeper &Records;
  CodeGenTarget Target;

  // Hold all pontentially compressible EVEX instructions
  std::vector<const CodeGenInstruction *> PreCompressionInsts;
  // Hold all compressed instructions. Divided into groups with same opcodes
  // to make the search more efficient
  std::map<uint64_t, std::vector<const CodeGenInstruction *>> CompressedInsts;

  typedef std::pair<const CodeGenInstruction *, const CodeGenInstruction *>
      Entry;
  typedef std::map<StringRef, std::vector<const CodeGenInstruction *>>
      PredicateInstMap;

  // Hold all compressed instructions that need to check predicate
  PredicateInstMap PredicateInsts;

public:
  X86InstrMappingEmitter(RecordKeeper &R) : Records(R), Target(R) {}

  // run - Output X86 EVEX compression tables.
  void run(raw_ostream &OS);

private:
  void emitCompressEVEXTable(ArrayRef<const CodeGenInstruction *> Insts,
                             raw_ostream &OS);
  void emitNFTransformTable(ArrayRef<const CodeGenInstruction *> Insts,
                            raw_ostream &OS);
  void emitND2NonNDTable(ArrayRef<const CodeGenInstruction *> Insts,
                         raw_ostream &OS);
  void emitSSE2AVXTable(ArrayRef<const CodeGenInstruction *> Insts,
                        raw_ostream &OS);

  // Prints the definition of class X86TableEntry.
  void printClassDef(raw_ostream &OS);
  // Prints the given table as a C++ array of type X86TableEntry under the guard
  // \p Macro.
  void printTable(const std::vector<Entry> &Table, StringRef Name,
                  StringRef Macro, raw_ostream &OS);
};

void X86InstrMappingEmitter::printClassDef(raw_ostream &OS) {
  OS << "struct X86TableEntry {\n"
        "  uint16_t OldOpc;\n"
        "  uint16_t NewOpc;\n"
        "  bool operator<(const X86TableEntry &RHS) const {\n"
        "    return OldOpc < RHS.OldOpc;\n"
        "  }"
        "  friend bool operator<(const X86TableEntry &TE, unsigned Opc) {\n"
        "    return TE.OldOpc < Opc;\n"
        "  }\n"
        "};";

  OS << "\n\n";
}

static void printMacroBegin(StringRef Macro, raw_ostream &OS) {
  OS << "\n#ifdef " << Macro << "\n";
}

static void printMacroEnd(StringRef Macro, raw_ostream &OS) {
  OS << "#endif // " << Macro << "\n\n";
}

void X86InstrMappingEmitter::printTable(const std::vector<Entry> &Table,
                                        StringRef Name, StringRef Macro,
                                        raw_ostream &OS) {
  printMacroBegin(Macro, OS);

  OS << "static const X86TableEntry " << Name << "[] = {\n";

  // Print all entries added to the table
  for (const auto &Pair : Table)
    OS << "  { X86::" << Pair.first->TheDef->getName()
       << ", X86::" << Pair.second->TheDef->getName() << " },\n";

  OS << "};\n\n";

  printMacroEnd(Macro, OS);
}

static uint8_t byteFromBitsInit(const BitsInit *B) {
  unsigned N = B->getNumBits();
  assert(N <= 8 && "Field is too large for uint8_t!");

  uint8_t Value = 0;
  for (unsigned I = 0; I != N; ++I) {
    BitInit *Bit = cast<BitInit>(B->getBit(I));
    Value |= Bit->getValue() << I;
  }
  return Value;
}

class IsMatch {
  const CodeGenInstruction *OldInst;

public:
  IsMatch(const CodeGenInstruction *OldInst) : OldInst(OldInst) {}

  bool operator()(const CodeGenInstruction *NewInst) {
    RecognizableInstrBase NewRI(*NewInst);
    RecognizableInstrBase OldRI(*OldInst);

    // Return false if any of the following fields of does not match.
    if (std::tuple(OldRI.IsCodeGenOnly, OldRI.OpMap, NewRI.OpPrefix,
                   OldRI.HasVEX_4V, OldRI.HasVEX_L, OldRI.HasREX_W,
                   OldRI.Form) !=
        std::tuple(NewRI.IsCodeGenOnly, NewRI.OpMap, OldRI.OpPrefix,
                   NewRI.HasVEX_4V, NewRI.HasVEX_L, NewRI.HasREX_W, NewRI.Form))
      return false;

    for (unsigned I = 0, E = OldInst->Operands.size(); I < E; ++I) {
      Record *OldOpRec = OldInst->Operands[I].Rec;
      Record *NewOpRec = NewInst->Operands[I].Rec;

      if (OldOpRec == NewOpRec)
        continue;

      if (isRegisterOperand(OldOpRec) && isRegisterOperand(NewOpRec)) {
        if (getRegOperandSize(OldOpRec) != getRegOperandSize(NewOpRec))
          return false;
      } else if (isMemoryOperand(OldOpRec) && isMemoryOperand(NewOpRec)) {
        if (getMemOperandSize(OldOpRec) != getMemOperandSize(NewOpRec))
          return false;
      } else if (isImmediateOperand(OldOpRec) && isImmediateOperand(NewOpRec)) {
        if (OldOpRec->getValueAsDef("Type") != NewOpRec->getValueAsDef("Type"))
          return false;
      }
    }

    return true;
  }
};

static bool isInteresting(const Record *Rec) {
  // _REV instruction should not appear before encoding optimization
  return Rec->isSubClassOf("X86Inst") &&
         !Rec->getValueAsBit("isAsmParserOnly") &&
         !Rec->getName().ends_with("_REV");
}

void X86InstrMappingEmitter::emitCompressEVEXTable(
    ArrayRef<const CodeGenInstruction *> Insts, raw_ostream &OS) {

  const std::map<StringRef, StringRef> ManualMap = {
#define ENTRY(OLD, NEW) {#OLD, #NEW},
#include "X86ManualInstrMapping.def"
  };
  const std::set<StringRef> NoCompressSet = {
#define NOCOMP(INSN) #INSN,
#include "X86ManualInstrMapping.def"
  };

  for (const CodeGenInstruction *Inst : Insts) {
    const Record *Rec = Inst->TheDef;
    StringRef Name = Rec->getName();
    if (!isInteresting(Rec))
      continue;

    // Promoted legacy instruction is in EVEX space, and has REX2-encoding
    // alternative. It's added due to HW design and never emitted by compiler.
    if (byteFromBitsInit(Rec->getValueAsBitsInit("OpMapBits")) ==
            X86Local::T_MAP4 &&
        byteFromBitsInit(Rec->getValueAsBitsInit("explicitOpPrefixBits")) ==
            X86Local::ExplicitEVEX)
      continue;

    if (NoCompressSet.find(Name) != NoCompressSet.end())
      continue;

    RecognizableInstrBase RI(*Inst);

    bool IsND = RI.OpMap == X86Local::T_MAP4 && RI.HasEVEX_B && RI.HasVEX_4V;
    // Add VEX encoded instructions to one of CompressedInsts vectors according
    // to it's opcode.
    if (RI.Encoding == X86Local::VEX)
      CompressedInsts[RI.Opcode].push_back(Inst);
    // Add relevant EVEX encoded instructions to PreCompressionInsts
    else if (RI.Encoding == X86Local::EVEX && !RI.HasEVEX_K && !RI.HasEVEX_L2 &&
             (!RI.HasEVEX_B || IsND))
      PreCompressionInsts.push_back(Inst);
  }

  std::vector<Entry> Table;
  for (const CodeGenInstruction *Inst : PreCompressionInsts) {
    const Record *Rec = Inst->TheDef;
    uint8_t Opcode = byteFromBitsInit(Rec->getValueAsBitsInit("Opcode"));
    StringRef Name = Rec->getName();
    const CodeGenInstruction *NewInst = nullptr;
    if (ManualMap.find(Name) != ManualMap.end()) {
      Record *NewRec = Records.getDef(ManualMap.at(Rec->getName()));
      assert(NewRec && "Instruction not found!");
      NewInst = &Target.getInstruction(NewRec);
    } else if (Name.ends_with("_EVEX")) {
      if (auto *NewRec = Records.getDef(Name.drop_back(5)))
        NewInst = &Target.getInstruction(NewRec);
    } else if (Name.ends_with("_ND"))
      // Leave it to ND2NONND table.
      continue;
    else {
      // For each pre-compression instruction look for a match in the
      // appropriate vector (instructions with the same opcode) using function
      // object IsMatch.
      auto Match = llvm::find_if(CompressedInsts[Opcode], IsMatch(Inst));
      if (Match != CompressedInsts[Opcode].end())
        NewInst = *Match;
    }

    if (!NewInst)
      continue;

    Table.push_back(std::pair(Inst, NewInst));
    auto Predicates = NewInst->TheDef->getValueAsListOfDefs("Predicates");
    auto It = llvm::find_if(Predicates, [](const Record *R) {
      StringRef Name = R->getName();
      return Name == "HasAVXNECONVERT" || Name == "HasAVXVNNI" ||
             Name == "HasAVXIFMA";
    });
    if (It != Predicates.end())
      PredicateInsts[(*It)->getValueAsString("CondString")].push_back(NewInst);
  }

  StringRef Macro = "GET_X86_COMPRESS_EVEX_TABLE";
  printTable(Table, "X86CompressEVEXTable", Macro, OS);

  // Prints function which checks target feature for compressed instructions.
  printMacroBegin(Macro, OS);
  OS << "static bool checkPredicate(unsigned Opc, const X86Subtarget "
        "*Subtarget) {\n"
     << "  switch (Opc) {\n"
     << "  default: return true;\n";
  for (const auto &[Key, Val] : PredicateInsts) {
    for (const auto &Inst : Val)
      OS << "  case X86::" << Inst->TheDef->getName() << ":\n";
    OS << "    return " << Key << ";\n";
  }
  OS << "  }\n";
  OS << "}\n\n";
  printMacroEnd(Macro, OS);
}

void X86InstrMappingEmitter::emitNFTransformTable(
    ArrayRef<const CodeGenInstruction *> Insts, raw_ostream &OS) {
  std::vector<Entry> Table;
  for (const CodeGenInstruction *Inst : Insts) {
    const Record *Rec = Inst->TheDef;
    if (!isInteresting(Rec))
      continue;
    std::string Name = Rec->getName().str();
    auto Pos = Name.find("_NF");
    if (Pos == std::string::npos)
      continue;

    if (auto *NewRec = Records.getDef(Name.erase(Pos, 3))) {
#ifndef NDEBUG
      auto ClobberEFLAGS = [](const Record *R) {
        return llvm::any_of(
            R->getValueAsListOfDefs("Defs"),
            [](const Record *Def) { return Def->getName() == "EFLAGS"; });
      };
      if (ClobberEFLAGS(Rec))
        report_fatal_error("EFLAGS should not be clobbered by " +
                           Rec->getName());
      if (!ClobberEFLAGS(NewRec))
        report_fatal_error("EFLAGS should be clobbered by " +
                           NewRec->getName());
#endif
      Table.push_back(std::pair(&Target.getInstruction(NewRec), Inst));
    }
  }
  printTable(Table, "X86NFTransformTable", "GET_X86_NF_TRANSFORM_TABLE", OS);
}

void X86InstrMappingEmitter::emitND2NonNDTable(
    ArrayRef<const CodeGenInstruction *> Insts, raw_ostream &OS) {

  const std::map<StringRef, StringRef> ManualMap = {
#define ENTRY_ND(OLD, NEW) {#OLD, #NEW},
#include "X86ManualInstrMapping.def"
  };
  const std::set<StringRef> NoCompressSet = {
#define NOCOMP_ND(INSN) #INSN,
#include "X86ManualInstrMapping.def"
  };

  std::vector<Entry> Table;
  for (const CodeGenInstruction *Inst : Insts) {
    const Record *Rec = Inst->TheDef;
    StringRef Name = Rec->getName();
    if (!isInteresting(Rec) || NoCompressSet.find(Name) != NoCompressSet.end())
      continue;
    if (ManualMap.find(Name) != ManualMap.end()) {
      auto *NewRec = Records.getDef(ManualMap.at(Rec->getName()));
      assert(NewRec && "Instruction not found!");
      auto &NewInst = Target.getInstruction(NewRec);
      Table.push_back(std::pair(Inst, &NewInst));
      continue;
    }

    if (!Name.ends_with("_ND"))
      continue;
    auto *NewRec = Records.getDef(Name.drop_back(3));
    if (!NewRec)
      continue;
    auto &NewInst = Target.getInstruction(NewRec);
    if (isRegisterOperand(NewInst.Operands[0].Rec))
      Table.push_back(std::pair(Inst, &NewInst));
  }
  printTable(Table, "X86ND2NonNDTable", "GET_X86_ND2NONND_TABLE", OS);
}

void X86InstrMappingEmitter::emitSSE2AVXTable(
    ArrayRef<const CodeGenInstruction *> Insts, raw_ostream &OS) {

  const std::map<StringRef, StringRef> ManualMap = {
#define ENTRY_SSE2AVX(OLD, NEW) {#OLD, #NEW},
#include "X86ManualInstrMapping.def"
  };

  std::vector<Entry> Table;
  for (const CodeGenInstruction *Inst : Insts) {
    const Record *Rec = Inst->TheDef;
    StringRef Name = Rec->getName();
    if (!isInteresting(Rec))
      continue;
    if (ManualMap.find(Name) != ManualMap.end()) {
      auto *NewRec = Records.getDef(ManualMap.at(Rec->getName()));
      assert(NewRec && "Instruction not found!");
      auto &NewInst = Target.getInstruction(NewRec);
      Table.push_back(std::pair(Inst, &NewInst));
      continue;
    }

    std::string NewName = ("V" + Name).str();
    auto *AVXRec = Records.getDef(NewName);
    if (!AVXRec)
      continue;
    auto &AVXInst = Target.getInstruction(AVXRec);
    Table.push_back(std::pair(Inst, &AVXInst));
  }
  printTable(Table, "X86SSE2AVXTable", "GET_X86_SSE2AVX_TABLE", OS);
}

void X86InstrMappingEmitter::run(raw_ostream &OS) {
  emitSourceFileHeader("X86 instruction mapping", OS);

  ArrayRef<const CodeGenInstruction *> Insts =
      Target.getInstructionsByEnumValue();
  printClassDef(OS);
  emitCompressEVEXTable(Insts, OS);
  emitNFTransformTable(Insts, OS);
  emitND2NonNDTable(Insts, OS);
  emitSSE2AVXTable(Insts, OS);
}
} // namespace

static TableGen::Emitter::OptClass<X86InstrMappingEmitter>
    X("gen-x86-instr-mapping", "Generate X86 instruction mapping");
