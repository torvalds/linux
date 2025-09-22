//===- X86RecognizableInstr.h - Disassembler instruction spec ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is part of the X86 Disassembler Emitter.
// It contains the interface of a single recognizable instruction.
// Documentation for the disassembler emitter in general can be found in
//  X86DisassemblerEmitter.h.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_TABLEGEN_X86RECOGNIZABLEINSTR_H
#define LLVM_UTILS_TABLEGEN_X86RECOGNIZABLEINSTR_H

#include "Common/CodeGenInstruction.h"
#include "llvm/Support/X86DisassemblerDecoderCommon.h"
#include <cstdint>
#include <string>
#include <vector>

struct InstructionSpecifier;

namespace llvm {
class Record;
#define X86_INSTR_MRM_MAPPING                                                  \
  MAP(C0, 64)                                                                  \
  MAP(C1, 65)                                                                  \
  MAP(C2, 66)                                                                  \
  MAP(C3, 67)                                                                  \
  MAP(C4, 68)                                                                  \
  MAP(C5, 69)                                                                  \
  MAP(C6, 70)                                                                  \
  MAP(C7, 71)                                                                  \
  MAP(C8, 72)                                                                  \
  MAP(C9, 73)                                                                  \
  MAP(CA, 74)                                                                  \
  MAP(CB, 75)                                                                  \
  MAP(CC, 76)                                                                  \
  MAP(CD, 77)                                                                  \
  MAP(CE, 78)                                                                  \
  MAP(CF, 79)                                                                  \
  MAP(D0, 80)                                                                  \
  MAP(D1, 81)                                                                  \
  MAP(D2, 82)                                                                  \
  MAP(D3, 83)                                                                  \
  MAP(D4, 84)                                                                  \
  MAP(D5, 85)                                                                  \
  MAP(D6, 86)                                                                  \
  MAP(D7, 87)                                                                  \
  MAP(D8, 88)                                                                  \
  MAP(D9, 89)                                                                  \
  MAP(DA, 90)                                                                  \
  MAP(DB, 91)                                                                  \
  MAP(DC, 92)                                                                  \
  MAP(DD, 93)                                                                  \
  MAP(DE, 94)                                                                  \
  MAP(DF, 95)                                                                  \
  MAP(E0, 96)                                                                  \
  MAP(E1, 97)                                                                  \
  MAP(E2, 98)                                                                  \
  MAP(E3, 99)                                                                  \
  MAP(E4, 100)                                                                 \
  MAP(E5, 101)                                                                 \
  MAP(E6, 102)                                                                 \
  MAP(E7, 103)                                                                 \
  MAP(E8, 104)                                                                 \
  MAP(E9, 105)                                                                 \
  MAP(EA, 106)                                                                 \
  MAP(EB, 107)                                                                 \
  MAP(EC, 108)                                                                 \
  MAP(ED, 109)                                                                 \
  MAP(EE, 110)                                                                 \
  MAP(EF, 111)                                                                 \
  MAP(F0, 112)                                                                 \
  MAP(F1, 113)                                                                 \
  MAP(F2, 114)                                                                 \
  MAP(F3, 115)                                                                 \
  MAP(F4, 116)                                                                 \
  MAP(F5, 117)                                                                 \
  MAP(F6, 118)                                                                 \
  MAP(F7, 119)                                                                 \
  MAP(F8, 120)                                                                 \
  MAP(F9, 121)                                                                 \
  MAP(FA, 122)                                                                 \
  MAP(FB, 123)                                                                 \
  MAP(FC, 124)                                                                 \
  MAP(FD, 125)                                                                 \
  MAP(FE, 126)                                                                 \
  MAP(FF, 127)

// A clone of X86 since we can't depend on something that is generated.
namespace X86Local {
enum {
  Pseudo = 0,
  RawFrm = 1,
  AddRegFrm = 2,
  RawFrmMemOffs = 3,
  RawFrmSrc = 4,
  RawFrmDst = 5,
  RawFrmDstSrc = 6,
  RawFrmImm8 = 7,
  RawFrmImm16 = 8,
  AddCCFrm = 9,
  PrefixByte = 10,
  MRMDestRegCC = 18,
  MRMDestMemCC = 19,
  MRMDestMem4VOp3CC = 20,
  MRMr0 = 21,
  MRMSrcMemFSIB = 22,
  MRMDestMemFSIB = 23,
  MRMDestMem = 24,
  MRMSrcMem = 25,
  MRMSrcMem4VOp3 = 26,
  MRMSrcMemOp4 = 27,
  MRMSrcMemCC = 28,
  MRMXmCC = 30,
  MRMXm = 31,
  MRM0m = 32,
  MRM1m = 33,
  MRM2m = 34,
  MRM3m = 35,
  MRM4m = 36,
  MRM5m = 37,
  MRM6m = 38,
  MRM7m = 39,
  MRMDestReg = 40,
  MRMSrcReg = 41,
  MRMSrcReg4VOp3 = 42,
  MRMSrcRegOp4 = 43,
  MRMSrcRegCC = 44,
  MRMXrCC = 46,
  MRMXr = 47,
  MRM0r = 48,
  MRM1r = 49,
  MRM2r = 50,
  MRM3r = 51,
  MRM4r = 52,
  MRM5r = 53,
  MRM6r = 54,
  MRM7r = 55,
  MRM0X = 56,
  MRM1X = 57,
  MRM2X = 58,
  MRM3X = 59,
  MRM4X = 60,
  MRM5X = 61,
  MRM6X = 62,
  MRM7X = 63,
#define MAP(from, to) MRM_##from = to,
  X86_INSTR_MRM_MAPPING
#undef MAP
};

enum {
  OB = 0,
  TB = 1,
  T8 = 2,
  TA = 3,
  XOP8 = 4,
  XOP9 = 5,
  XOPA = 6,
  ThreeDNow = 7,
  T_MAP4 = 8,
  T_MAP5 = 9,
  T_MAP6 = 10,
  T_MAP7 = 11
};

enum { PD = 1, XS = 2, XD = 3, PS = 4 };
enum { VEX = 1, XOP = 2, EVEX = 3 };
enum { OpSize16 = 1, OpSize32 = 2 };
enum { AdSize16 = 1, AdSize32 = 2, AdSize64 = 3 };
enum { ExplicitREX2 = 1, ExplicitEVEX = 3 };
} // namespace X86Local

namespace X86Disassembler {
class DisassemblerTables;
/// Extract common fields of a single X86 instruction from a CodeGenInstruction
struct RecognizableInstrBase {
  /// The OpPrefix field from the record
  uint8_t OpPrefix;
  /// The OpMap field from the record
  uint8_t OpMap;
  /// The opcode field from the record; this is the opcode used in the Intel
  /// encoding and therefore distinct from the UID
  uint8_t Opcode;
  /// The form field from the record
  uint8_t Form;
  // The encoding field from the record
  uint8_t Encoding;
  /// The OpSize field from the record
  uint8_t OpSize;
  /// The AdSize field from the record
  uint8_t AdSize;
  /// The hasREX_W field from the record
  bool HasREX_W;
  /// The hasVEX_4V field from the record
  bool HasVEX_4V;
  /// The IgnoresW field from the record
  bool IgnoresW;
  /// The hasVEX_L field from the record
  bool HasVEX_L;
  /// The ignoreVEX_L field from the record
  bool IgnoresVEX_L;
  /// The hasEVEX_L2Prefix field from the record
  bool HasEVEX_L2;
  /// The hasEVEX_K field from the record
  bool HasEVEX_K;
  /// The hasEVEX_KZ field from the record
  bool HasEVEX_KZ;
  /// The hasEVEX_B field from the record
  bool HasEVEX_B;
  /// The hasEVEX_NF field from the record
  bool HasEVEX_NF;
  /// The hasTwoConditionalOps field from the record
  bool HasTwoConditionalOps;
  /// Indicates that the instruction uses the L and L' fields for RC.
  bool EncodeRC;
  /// The isCodeGenOnly field from the record
  bool IsCodeGenOnly;
  /// The isAsmParserOnly field from the record
  bool IsAsmParserOnly;
  /// The ForceDisassemble field from the record
  bool ForceDisassemble;
  // The CD8_Scale field from the record
  uint8_t CD8_Scale;
  /// If explicitOpPrefix field from the record equals ExplicitREX2
  bool ExplicitREX2Prefix;
  /// \param insn The CodeGenInstruction to extract information from.
  RecognizableInstrBase(const CodeGenInstruction &insn);
  /// \returns true if this instruction should be emitted
  bool shouldBeEmitted() const;
};

/// RecognizableInstr - Encapsulates all information required to decode a single
///   instruction, as extracted from the LLVM instruction tables.  Has methods
///   to interpret the information available in the LLVM tables, and to emit the
///   instruction into DisassemblerTables.
class RecognizableInstr : public RecognizableInstrBase {
private:
  /// The record from the .td files corresponding to this instruction
  const Record *Rec;
  /// The instruction name as listed in the tables
  std::string Name;
  // Whether the instruction has the predicate "In32BitMode"
  bool Is32Bit;
  // Whether the instruction has the predicate "In64BitMode"
  bool Is64Bit;
  /// The operands of the instruction, as listed in the CodeGenInstruction.
  /// They are not one-to-one with operands listed in the MCInst; for example,
  /// memory operands expand to 5 operands in the MCInst
  const std::vector<CGIOperandList::OperandInfo> *Operands;

  /// The opcode of the instruction, as used in an MCInst
  InstrUID UID;
  /// The description of the instruction that is emitted into the instruction
  /// info table
  InstructionSpecifier *Spec;

  /// insnContext - Returns the primary context in which the instruction is
  ///   valid.
  ///
  /// @return - The context in which the instruction is valid.
  InstructionContext insnContext() const;

  /// typeFromString - Translates an operand type from the string provided in
  ///   the LLVM tables to an OperandType for use in the operand specifier.
  ///
  /// @param s              - The string, as extracted by calling Rec->getName()
  ///                         on a CodeGenInstruction::OperandInfo.
  /// @param hasREX_W - Indicates whether the instruction has a REX.W
  ///                         prefix.  If it does, 32-bit register operands stay
  ///                         32-bit regardless of the operand size.
  /// @param OpSize           Indicates the operand size of the instruction.
  ///                         If register size does not match OpSize, then
  ///                         register sizes keep their size.
  /// @return               - The operand's type.
  static OperandType typeFromString(const std::string &s, bool hasREX_W,
                                    uint8_t OpSize);

  /// immediateEncodingFromString - Translates an immediate encoding from the
  ///   string provided in the LLVM tables to an OperandEncoding for use in
  ///   the operand specifier.
  ///
  /// @param s       - See typeFromString().
  /// @param OpSize  - Indicates whether this is an OpSize16 instruction.
  ///                  If it is not, then 16-bit immediate operands stay 16-bit.
  /// @return        - The operand's encoding.
  static OperandEncoding immediateEncodingFromString(const std::string &s,
                                                     uint8_t OpSize);

  /// rmRegisterEncodingFromString - Like immediateEncodingFromString, but
  ///   handles operands that are in the REG field of the ModR/M byte.
  static OperandEncoding rmRegisterEncodingFromString(const std::string &s,
                                                      uint8_t OpSize);

  /// rmRegisterEncodingFromString - Like immediateEncodingFromString, but
  ///   handles operands that are in the REG field of the ModR/M byte.
  static OperandEncoding roRegisterEncodingFromString(const std::string &s,
                                                      uint8_t OpSize);
  static OperandEncoding memoryEncodingFromString(const std::string &s,
                                                  uint8_t OpSize);
  static OperandEncoding relocationEncodingFromString(const std::string &s,
                                                      uint8_t OpSize);
  static OperandEncoding opcodeModifierEncodingFromString(const std::string &s,
                                                          uint8_t OpSize);
  static OperandEncoding vvvvRegisterEncodingFromString(const std::string &s,
                                                        uint8_t OpSize);
  static OperandEncoding
  writemaskRegisterEncodingFromString(const std::string &s, uint8_t OpSize);

  /// Adjust the encoding type for an operand based on the instruction.
  void adjustOperandEncoding(OperandEncoding &encoding);

  /// handleOperand - Converts a single operand from the LLVM table format to
  ///   the emitted table format, handling any duplicate operands it encounters
  ///   and then one non-duplicate.
  ///
  /// @param optional             - Determines whether to assert that the
  ///                               operand exists.
  /// @param operandIndex         - The index into the generated operand table.
  ///                               Incremented by this function one or more
  ///                               times to reflect possible duplicate
  ///                               operands).
  /// @param physicalOperandIndex - The index of the current operand into the
  ///                               set of non-duplicate ('physical') operands.
  ///                               Incremented by this function once.
  /// @param numPhysicalOperands  - The number of non-duplicate operands in the
  ///                               instructions.
  /// @param operandMapping       - The operand mapping, which has an entry for
  ///                               each operand that indicates whether it is a
  ///                               duplicate, and of what.
  void handleOperand(bool optional, unsigned &operandIndex,
                     unsigned &physicalOperandIndex,
                     unsigned numPhysicalOperands,
                     const unsigned *operandMapping,
                     OperandEncoding (*encodingFromString)(const std::string &,
                                                           uint8_t OpSize));

  /// emitInstructionSpecifier - Loads the instruction specifier for the current
  ///   instruction into a DisassemblerTables.
  ///
  void emitInstructionSpecifier();

  /// emitDecodePath - Populates the proper fields in the decode tables
  ///   corresponding to the decode paths for this instruction.
  ///
  /// \param tables The DisassemblerTables to populate with the decode
  ///               decode information for the current instruction.
  void emitDecodePath(DisassemblerTables &tables) const;

public:
  /// Constructor - Initializes a RecognizableInstr with the appropriate fields
  ///   from a CodeGenInstruction.
  ///
  /// \param tables The DisassemblerTables that the specifier will be added to.
  /// \param insn   The CodeGenInstruction to extract information from.
  /// \param uid    The unique ID of the current instruction.
  RecognizableInstr(DisassemblerTables &tables, const CodeGenInstruction &insn,
                    InstrUID uid);
  /// processInstr - Accepts a CodeGenInstruction and loads decode information
  ///   for it into a DisassemblerTables if appropriate.
  ///
  /// \param tables The DiassemblerTables to be populated with decode
  ///               information.
  /// \param insn   The CodeGenInstruction to be used as a source for this
  ///               information.
  /// \param uid    The unique ID of the instruction.
  static void processInstr(DisassemblerTables &tables,
                           const CodeGenInstruction &insn, InstrUID uid);
};

std::string getMnemonic(const CodeGenInstruction *I, unsigned Variant);
bool isRegisterOperand(const Record *Rec);
bool isMemoryOperand(const Record *Rec);
bool isImmediateOperand(const Record *Rec);
unsigned getRegOperandSize(const Record *RegRec);
unsigned getMemOperandSize(const Record *MemRec);
} // namespace X86Disassembler
} // namespace llvm
#endif
