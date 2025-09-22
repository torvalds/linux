//===-- BPFInstrFormats.td - BPF Instruction Formats -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

class BPFOpClass<bits<3> val> {
  bits<3> Value = val;
}

def BPF_LD    : BPFOpClass<0x0>;
def BPF_LDX   : BPFOpClass<0x1>;
def BPF_ST    : BPFOpClass<0x2>;
def BPF_STX   : BPFOpClass<0x3>;
def BPF_ALU   : BPFOpClass<0x4>;
def BPF_JMP   : BPFOpClass<0x5>;
def BPF_JMP32 : BPFOpClass<0x6>;
def BPF_ALU64 : BPFOpClass<0x7>;

class BPFSrcType<bits<1> val> {
  bits<1> Value = val;
}

def BPF_K : BPFSrcType<0x0>;
def BPF_X : BPFSrcType<0x1>;

class BPFArithOp<bits<4> val> {
  bits<4> Value = val;
}

def BPF_ADD  : BPFArithOp<0x0>;
def BPF_SUB  : BPFArithOp<0x1>;
def BPF_MUL  : BPFArithOp<0x2>;
def BPF_DIV  : BPFArithOp<0x3>;
def BPF_OR   : BPFArithOp<0x4>;
def BPF_AND  : BPFArithOp<0x5>;
def BPF_LSH  : BPFArithOp<0x6>;
def BPF_RSH  : BPFArithOp<0x7>;
def BPF_NEG  : BPFArithOp<0x8>;
def BPF_MOD  : BPFArithOp<0x9>;
def BPF_XOR  : BPFArithOp<0xa>;
def BPF_MOV  : BPFArithOp<0xb>;
def BPF_ARSH : BPFArithOp<0xc>;
def BPF_END  : BPFArithOp<0xd>;

def BPF_XCHG    : BPFArithOp<0xe>;
def BPF_CMPXCHG : BPFArithOp<0xf>;

class BPFEndDir<bits<1> val> {
  bits<1> Value = val;
}

def BPF_TO_LE : BPFSrcType<0x0>;
def BPF_TO_BE : BPFSrcType<0x1>;

class BPFJumpOp<bits<4> val> {
  bits<4> Value = val;
}

def BPF_JA   : BPFJumpOp<0x0>;
def BPF_JEQ  : BPFJumpOp<0x1>;
def BPF_JGT  : BPFJumpOp<0x2>;
def BPF_JGE  : BPFJumpOp<0x3>;
def BPF_JSET : BPFJumpOp<0x4>;
def BPF_JNE  : BPFJumpOp<0x5>;
def BPF_JSGT : BPFJumpOp<0x6>;
def BPF_JSGE : BPFJumpOp<0x7>;
def BPF_CALL : BPFJumpOp<0x8>;
def BPF_EXIT : BPFJumpOp<0x9>;
def BPF_JLT  : BPFJumpOp<0xa>;
def BPF_JLE  : BPFJumpOp<0xb>;
def BPF_JSLT : BPFJumpOp<0xc>;
def BPF_JSLE : BPFJumpOp<0xd>;
def BPF_JCOND : BPFJumpOp<0xe>;

class BPFWidthModifer<bits<2> val> {
  bits<2> Value = val;
}

def BPF_W  : BPFWidthModifer<0x0>;
def BPF_H  : BPFWidthModifer<0x1>;
def BPF_B  : BPFWidthModifer<0x2>;
def BPF_DW : BPFWidthModifer<0x3>;

class BPFModeModifer<bits<3> val> {
  bits<3> Value = val;
}

def BPF_IMM  : BPFModeModifer<0x0>;
def BPF_ABS  : BPFModeModifer<0x1>;
def BPF_IND  : BPFModeModifer<0x2>;
def BPF_MEM  : BPFModeModifer<0x3>;
def BPF_MEMSX  : BPFModeModifer<0x4>;
def BPF_ATOMIC : BPFModeModifer<0x6>;

class BPFAtomicFlag<bits<4> val> {
  bits<4> Value = val;
}

def BPF_FETCH : BPFAtomicFlag<0x1>;

class InstBPF<dag outs, dag ins, string asmstr, list<dag> pattern>
  : Instruction {
  field bits<64> Inst;
  field bits<64> SoftFail = 0;
  let Size = 8;

  let Namespace = "BPF";
  let DecoderNamespace = "BPF";

  BPFOpClass BPFClass;
  let Inst{58-56} = BPFClass.Value;

  dag OutOperandList = outs;
  dag InOperandList = ins;
  let AsmString = asmstr;
  let Pattern = pattern;
}

// Pseudo instructions
class Pseudo<dag outs, dag ins, string asmstr, list<dag> pattern>
  : InstBPF<outs, ins, asmstr, pattern> {
  let Inst{63-0} = 0;
  let isPseudo = 1;
}
