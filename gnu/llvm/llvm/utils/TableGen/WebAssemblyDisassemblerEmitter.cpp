//===- WebAssemblyDisassemblerEmitter.cpp - Disassembler tables -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is part of the WebAssembly Disassembler Emitter.
// It contains the implementation of the disassembler tables.
// Documentation for the disassembler emitter in general can be found in
// WebAssemblyDisassemblerEmitter.h.
//
//===----------------------------------------------------------------------===//

#include "WebAssemblyDisassemblerEmitter.h"
#include "Common/CodeGenInstruction.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Record.h"

namespace llvm {

static constexpr int WebAssemblyInstructionTableSize = 256;

void emitWebAssemblyDisassemblerTables(
    raw_ostream &OS,
    const ArrayRef<const CodeGenInstruction *> &NumberedInstructions) {
  // First lets organize all opcodes by (prefix) byte. Prefix 0 is the
  // starting table.
  std::map<unsigned,
           std::map<unsigned, std::pair<unsigned, const CodeGenInstruction *>>>
      OpcodeTable;
  for (unsigned I = 0; I != NumberedInstructions.size(); ++I) {
    auto &CGI = *NumberedInstructions[I];
    auto &Def = *CGI.TheDef;
    if (!Def.getValue("Inst"))
      continue;
    auto &Inst = *Def.getValueAsBitsInit("Inst");
    RecordKeeper &RK = Inst.getRecordKeeper();
    unsigned Opc = static_cast<unsigned>(
        cast<IntInit>(Inst.convertInitializerTo(IntRecTy::get(RK)))
            ->getValue());
    if (Opc == 0xFFFFFFFF)
      continue; // No opcode defined.
    assert(Opc <= 0xFFFFFF);
    unsigned Prefix;
    if (Opc <= 0xFFFF) {
      Prefix = Opc >> 8;
      Opc = Opc & 0xFF;
    } else {
      Prefix = Opc >> 16;
      Opc = Opc & 0xFFFF;
    }
    auto &CGIP = OpcodeTable[Prefix][Opc];
    // All wasm instructions have a StackBased field of type string, we only
    // want the instructions for which this is "true".
    bool IsStackBased = Def.getValueAsBit("StackBased");
    if (!IsStackBased)
      continue;
    if (CGIP.second) {
      // We already have an instruction for this slot, so decide which one
      // should be the canonical one. This determines which variant gets
      // printed in a disassembly. We want e.g. "call" not "i32.call", and
      // "end" when we don't know if its "end_loop" or "end_block" etc.
      bool IsCanonicalExisting =
          CGIP.second->TheDef->getValueAsBit("IsCanonical");
      // We already have one marked explicitly as canonical, so keep it.
      if (IsCanonicalExisting)
        continue;
      bool IsCanonicalNew = Def.getValueAsBit("IsCanonical");
      // If the new one is explicitly marked as canonical, take it.
      if (!IsCanonicalNew) {
        // Neither the existing or new instruction is canonical.
        // Pick the one with the shortest name as heuristic.
        // Though ideally IsCanonical is always defined for at least one
        // variant so this never has to apply.
        if (CGIP.second->AsmString.size() <= CGI.AsmString.size())
          continue;
      }
    }
    // Set this instruction as the one to use.
    CGIP = std::pair(I, &CGI);
  }
  OS << "#include \"MCTargetDesc/WebAssemblyMCTargetDesc.h\"\n";
  OS << "\n";
  OS << "namespace llvm {\n\n";
  OS << "static constexpr int WebAssemblyInstructionTableSize = ";
  OS << WebAssemblyInstructionTableSize << ";\n\n";
  OS << "enum EntryType : uint8_t { ";
  OS << "ET_Unused, ET_Prefix, ET_Instruction };\n\n";
  OS << "struct WebAssemblyInstruction {\n";
  OS << "  uint16_t Opcode;\n";
  OS << "  EntryType ET;\n";
  OS << "  uint8_t NumOperands;\n";
  OS << "  uint16_t OperandStart;\n";
  OS << "};\n\n";
  std::vector<std::string> OperandTable, CurOperandList;
  // Output one table per prefix.
  for (auto &PrefixPair : OpcodeTable) {
    if (PrefixPair.second.empty())
      continue;
    OS << "WebAssemblyInstruction InstructionTable" << PrefixPair.first;
    OS << "[] = {\n";
    for (unsigned I = 0; I < WebAssemblyInstructionTableSize; I++) {
      auto InstIt = PrefixPair.second.find(I);
      if (InstIt != PrefixPair.second.end()) {
        // Regular instruction.
        assert(InstIt->second.second);
        auto &CGI = *InstIt->second.second;
        OS << "  // 0x";
        OS.write_hex(static_cast<unsigned long long>(I));
        OS << ": " << CGI.AsmString << "\n";
        OS << "  { " << InstIt->second.first << ", ET_Instruction, ";
        OS << CGI.Operands.OperandList.size() << ", ";
        // Collect operand types for storage in a shared list.
        CurOperandList.clear();
        for (auto &Op : CGI.Operands.OperandList) {
          assert(Op.OperandType != "MCOI::OPERAND_UNKNOWN");
          CurOperandList.push_back(Op.OperandType);
        }
        // See if we already have stored this sequence before. This is not
        // strictly necessary but makes the table really small.
        size_t OperandStart = OperandTable.size();
        if (CurOperandList.size() <= OperandTable.size()) {
          for (size_t J = 0; J <= OperandTable.size() - CurOperandList.size();
               ++J) {
            size_t K = 0;
            for (; K < CurOperandList.size(); ++K) {
              if (OperandTable[J + K] != CurOperandList[K])
                break;
            }
            if (K == CurOperandList.size()) {
              OperandStart = J;
              break;
            }
          }
        }
        // Store operands if no prior occurrence.
        if (OperandStart == OperandTable.size()) {
          llvm::append_range(OperandTable, CurOperandList);
        }
        OS << OperandStart;
      } else {
        auto PrefixIt = OpcodeTable.find(I);
        // If we have a non-empty table for it that's not 0, this is a prefix.
        if (PrefixIt != OpcodeTable.end() && I && !PrefixPair.first) {
          OS << "  { 0, ET_Prefix, 0, 0";
        } else {
          OS << "  { 0, ET_Unused, 0, 0";
        }
      }
      OS << "  },\n";
    }
    OS << "};\n\n";
  }
  // Create a table of all operands:
  OS << "const uint8_t OperandTable[] = {\n";
  for (auto &Op : OperandTable) {
    OS << "  " << Op << ",\n";
  }
  OS << "};\n\n";
  // Create a table of all extension tables:
  OS << "struct { uint8_t Prefix; const WebAssemblyInstruction *Table; }\n";
  OS << "PrefixTable[] = {\n";
  for (auto &PrefixPair : OpcodeTable) {
    if (PrefixPair.second.empty() || !PrefixPair.first)
      continue;
    OS << "  { " << PrefixPair.first << ", InstructionTable"
       << PrefixPair.first;
    OS << " },\n";
  }
  OS << "  { 0, nullptr }\n};\n\n";
  OS << "} // end namespace llvm\n";
}

} // namespace llvm
