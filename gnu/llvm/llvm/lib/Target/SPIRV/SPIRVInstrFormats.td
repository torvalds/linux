//===-- SPIRVInstrFormats.td - SPIR-V Instruction Formats --*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

def StringImm: Operand<i32>{
  let PrintMethod="printStringImm";
}

class Op<bits<16> Opcode, dag outs, dag ins, string asmstr, list<dag> pattern = []>
  : Instruction {
  field bits<16> Inst;

  let Inst = Opcode;

  let Namespace = "SPIRV";
  let DecoderNamespace = "SPIRV";

  dag OutOperandList = outs;
  dag InOperandList = ins;
  let AsmString = asmstr;
  let Pattern = pattern;
}

// Pseudo instructions
class Pseudo<dag outs, dag ins> : Op<0, outs, ins, ""> {
  let isPseudo = 1;
  let hasSideEffects = 0;
}
