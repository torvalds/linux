//===- X86DisassemblerShared.h - Emitter shared header ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_TABLEGEN_X86DISASSEMBLERSHARED_H
#define LLVM_UTILS_TABLEGEN_X86DISASSEMBLERSHARED_H

#include <cstring>
#include <string>

#include "llvm/Support/X86DisassemblerDecoderCommon.h"

struct InstructionSpecifier {
  llvm::X86Disassembler::OperandSpecifier
      operands[llvm::X86Disassembler::X86_MAX_OPERANDS];
  llvm::X86Disassembler::InstructionContext insnContext;
  std::string name;

  InstructionSpecifier() {
    insnContext = llvm::X86Disassembler::IC;
    name = "";
    memset(operands, 0, sizeof(operands));
  }
};

/// Specifies whether a ModR/M byte is needed and (if so) which
/// instruction each possible value of the ModR/M byte corresponds to. Once
/// this information is known, we have narrowed down to a single instruction.
struct ModRMDecision {
  uint8_t modrm_type;
  llvm::X86Disassembler::InstrUID instructionIDs[256];
};

/// Specifies which set of ModR/M->instruction tables to look at
/// given a particular opcode.
struct OpcodeDecision {
  ModRMDecision modRMDecisions[256];
};

/// Specifies which opcode->instruction tables to look at given
/// a particular context (set of attributes).  Since there are many possible
/// contexts, the decoder first uses CONTEXTS_SYM to determine which context
/// applies given a specific set of attributes.  Hence there are only IC_max
/// entries in this table, rather than 2^(ATTR_max).
struct ContextDecision {
  OpcodeDecision opcodeDecisions[llvm::X86Disassembler::IC_max];

  ContextDecision() { memset(opcodeDecisions, 0, sizeof(opcodeDecisions)); }
};

#endif
