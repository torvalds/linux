//===-- AVRInstrInfo.td - AVR Instruction Formats ----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// AVR Instruction Format Definitions.
//
//===----------------------------------------------------------------------===//

// A generic AVR instruction.
class AVRInst<dag outs, dag ins, string asmstr, list<dag> pattern>
    : Instruction {
  let Namespace = "AVR";

  dag OutOperandList = outs;
  dag InOperandList = ins;
  let AsmString = asmstr;
  let Pattern = pattern;

  field bits<32> SoftFail = 0;
}

/// A 16-bit AVR instruction.
class AVRInst16<dag outs, dag ins, string asmstr, list<dag> pattern>
    : AVRInst<outs, ins, asmstr, pattern> {
  field bits<16> Inst;

  let Size = 2;
}

/// a 32-bit AVR instruction.
class AVRInst32<dag outs, dag ins, string asmstr, list<dag> pattern>
    : AVRInst<outs, ins, asmstr, pattern> {
  field bits<32> Inst;

  let Size = 4;
}

// A class for pseudo instructions.
// Pseudo instructions are not real AVR instructions. The DAG stores
// pseudo instructions which are replaced by real AVR instructions by
// AVRExpandPseudoInsts.cpp.
//
// For example, the ADDW (add wide, as in add 16 bit values) instruction
// is defined as a pseudo instruction. In AVRExpandPseudoInsts.cpp,
// the instruction is then replaced by two add instructions - one for each byte.
class Pseudo<dag outs, dag ins, string asmstr, list<dag> pattern>
    : AVRInst16<outs, ins, asmstr, pattern> {
  let Pattern = pattern;

  let isPseudo = 1;
  let isCodeGenOnly = 1;
}

//===----------------------------------------------------------------------===//
// Register / register instruction: <|opcode|ffrd|dddd|rrrr|>
// opcode = 4 bits.
// f = secondary opcode = 2 bits
// d = destination = 5 bits
// r = source = 5 bits
// (Accepts all registers)
//===----------------------------------------------------------------------===//
class FRdRr<bits<4> opcode, bits<2> f, dag outs, dag ins, string asmstr,
            list<dag> pattern> : AVRInst16<outs, ins, asmstr, pattern> {
  bits<5> rd;
  bits<5> rr;

  let Inst{15 - 12} = opcode;
  let Inst{11 - 10} = f;
  let Inst{9} = rr{4};
  let Inst{8 - 4} = rd;
  let Inst{3 - 0} = rr{3 - 0};
}

//===----------------------------------------------------------------------===//
// Instruction of the format `<mnemonic> Z, Rd`
// <|1001|001r|rrrr|0ttt>
//===----------------------------------------------------------------------===//
class FZRd<bits<3> t, dag outs, dag ins, string asmstr, list<dag> pattern>
    : AVRInst16<outs, ins, asmstr, pattern> {
  bits<5> rd;

  let Inst{15 - 12} = 0b1001;

  let Inst{11 - 9} = 0b001;
  let Inst{8} = rd{4};

  let Inst{7 - 4} = rd{3 - 0};

  let Inst{3} = 0;
  let Inst{2 - 0} = t;
}

//===----------------------------------------------------------------------===//
// Register / immediate8 instruction: <|opcode|KKKK|dddd|KKKK|>
// opcode = 4 bits.
// K = constant data = 8 bits
// d = destination = 4 bits
// (Only accepts r16-r31)
//===----------------------------------------------------------------------===//
class FRdK<bits<4> opcode, dag outs, dag ins, string asmstr, list<dag> pattern>
    : AVRInst16<outs, ins, asmstr, pattern> {
  bits<4> rd;
  bits<8> k;

  let Inst{15 - 12} = opcode;
  let Inst{11 - 8} = k{7 - 4};
  let Inst{7 - 4} = rd{3 - 0};
  let Inst{3 - 0} = k{3 - 0};

  let isAsCheapAsAMove = 1;
}

//===----------------------------------------------------------------------===//
// Register instruction: <|opcode|fffd|dddd|ffff|>
// opcode = 4 bits.
// f = secondary opcode = 7 bits
// d = destination = 5 bits
// (Accepts all registers)
//===----------------------------------------------------------------------===//
class FRd<bits<4> opcode, bits<7> f, dag outs, dag ins, string asmstr,
          list<dag> pattern> : AVRInst16<outs, ins, asmstr, pattern> {
  bits<5> rd;

  let Inst{15 - 12} = opcode;
  let Inst{11 - 9} = f{6 - 4};
  let Inst{8 - 4} = rd;
  let Inst{3 - 0} = f{3 - 0};

  let DecoderMethod = "decodeFRd";
}

//===----------------------------------------------------------------------===//
// [STD/LDD] P+q, Rr special encoding: <|10q0|qqtr|rrrr|pqqq>
// t = type (1 for STD, 0 for LDD)
// q = displacement (6 bits)
// r = register (5 bits)
// p = pointer register (1 bit) [1 for Y, 0 for Z]
//===----------------------------------------------------------------------===//
class FSTDLDD<bit type, dag outs, dag ins, string asmstr, list<dag> pattern>
    : AVRInst16<outs, ins, asmstr, pattern> {
  bits<7> memri;
  bits<5> reg; // the GP register

  let Inst{15 - 14} = 0b10;
  let Inst{13} = memri{5};
  let Inst{12} = 0;

  let Inst{11 - 10} = memri{4 - 3};
  let Inst{9} = type;
  let Inst{8} = reg{4};

  let Inst{7 - 4} = reg{3 - 0};

  let Inst{3} = memri{6};
  let Inst{2 - 0} = memri{2 - 0};
}

//===---------------------------------------------------------------------===//
// An ST/LD instruction.
// <|100i|00tr|rrrr|ppaa|>
// t = type (1 for store, 0 for load)
// a = regular/postinc/predec (reg = 0b00, postinc = 0b01, predec = 0b10)
// p = pointer register
// r = src/dst register
//
// Note that the bit labelled 'i' above does not follow a simple pattern,
// so there exists a post encoder method to set it manually. Also a specified
// decoder method is needed.
//===---------------------------------------------------------------------===//
class FSTLD<bit type, bits<2> mode, dag outs, dag ins, string asmstr,
            list<dag> pattern> : AVRInst16<outs, ins, asmstr, pattern> {
  bits<2> ptrreg;
  bits<5> reg;

  let Inst{15 - 13} = 0b100;
  // This bit varies depending on the arguments and the mode.
  // We have a post encoder method to set this bit manually.
  let Inst{12} = 0;

  let Inst{11 - 10} = 0b00;
  let Inst{9} = type;
  let Inst{8} = reg{4};

  let Inst{7 - 4} = reg{3 - 0};

  let Inst{3 - 2} = ptrreg{1 - 0};
  let Inst{1 - 0} = mode{1 - 0};

  let DecoderMethod = "decodeLoadStore";
  let PostEncoderMethod = "loadStorePostEncoder";
}

//===---------------------------------------------------------------------===//
// Special format for the LPM/ELPM instructions
// [E]LPM Rd, Z[+]
// <|1001|000d|dddd|01ep>
// d = destination register
// e = is elpm
// p = is postincrement
//===---------------------------------------------------------------------===//
class FLPMX<bit e, bit p, dag outs, dag ins, string asmstr, list<dag> pattern>
    : AVRInst16<outs, ins, asmstr, pattern> {
  bits<5> rd;

  let Inst{15 - 12} = 0b1001;

  let Inst{11 - 9} = 0b000;
  let Inst{8} = rd{4};

  let Inst{7 - 4} = rd{3 - 0};

  let Inst{3 - 2} = 0b01;
  let Inst{1} = e;
  let Inst{0} = p;

  let DecoderMethod = "decodeFLPMX";
}

//===----------------------------------------------------------------------===//
// MOVWRdRr special encoding: <|0000|0001|dddd|rrrr|>
// d = destination = 4 bits
// r = source = 4 bits
// (Only accepts even registers)
//===----------------------------------------------------------------------===//
class FMOVWRdRr<dag outs, dag ins, string asmstr, list<dag> pattern>
    : AVRInst16<outs, ins, asmstr, pattern> {
  bits<5> rd;
  bits<5> rr;

  let Inst{15 - 8} = 0b00000001;
  let Inst{7 - 4} = rd{4 - 1};
  let Inst{3 - 0} = rr{4 - 1};

  let DecoderMethod = "decodeFMOVWRdRr";
}

//===----------------------------------------------------------------------===//
// MULSrr special encoding: <|0000|0010|dddd|rrrr|>
// d = multiplicand = 4 bits
// r = multiplier = 4 bits
// (Only accepts r16-r31)
//===----------------------------------------------------------------------===//
class FMUL2RdRr<bit f, dag outs, dag ins, string asmstr, list<dag> pattern>
    : AVRInst16<outs, ins, asmstr, pattern> {
  bits<5> rd; // accept 5 bits but only encode the lower 4
  bits<5> rr; // accept 5 bits but only encode the lower 4

  let Inst{15 - 9} = 0b0000001;
  let Inst{8} = f;
  let Inst{7 - 4} = rd{3 - 0};
  let Inst{3 - 0} = rr{3 - 0};

  let DecoderMethod = "decodeFMUL2RdRr";
}

// Special encoding for the FMUL family of instructions.
//
// <0000|0011|fddd|frrr|>
//
// ff = 0b01 for FMUL
//      0b10 for FMULS
//      0b11 for FMULSU
//
// ddd = destination register
// rrr = source register
class FFMULRdRr<bits<2> f, dag outs, dag ins, string asmstr, list<dag> pattern>
    : AVRInst16<outs, ins, asmstr, pattern> {
  bits<3> rd;
  bits<3> rr;

  let Inst{15 - 8} = 0b00000011;
  let Inst{7} = f{1};
  let Inst{6 - 4} = rd;
  let Inst{3} = f{0};
  let Inst{2 - 0} = rr;

  let DecoderMethod = "decodeFFMULRdRr";
}

//===----------------------------------------------------------------------===//
// Arithmetic word instructions (ADIW / SBIW): <|1001|011f|kkdd|kkkk|>
// f = secondary opcode = 1 bit
// k = constant data = 6 bits
// d = destination = 2 bits
// (Only accepts r25:24 r27:26 r29:28 r31:30)
//===----------------------------------------------------------------------===//
class FWRdK<bit f, dag outs, dag ins, string asmstr, list<dag> pattern>
    : AVRInst16<outs, ins, asmstr, pattern> {
  bits<5> rd; // accept 5 bits but only encode bits 1 and 2
  bits<6> k;

  let Inst{15 - 9} = 0b1001011;
  let Inst{8} = f;
  let Inst{7 - 6} = k{5 - 4};
  let Inst{5 - 4} = rd{2 - 1};
  let Inst{3 - 0} = k{3 - 0};

  let DecoderMethod = "decodeFWRdK";
}

//===----------------------------------------------------------------------===//
// In I/O instruction: <|1011|0AAd|dddd|AAAA|>
// A = I/O location address = 6 bits
// d = destination = 5 bits
// (Accepts all registers)
//===----------------------------------------------------------------------===//
class FIORdA<dag outs, dag ins, string asmstr, list<dag> pattern>
    : AVRInst16<outs, ins, asmstr, pattern> {
  bits<5> rd;
  bits<6> A;

  let Inst{15 - 11} = 0b10110;
  let Inst{10 - 9} = A{5 - 4};
  let Inst{8 - 4} = rd;
  let Inst{3 - 0} = A{3 - 0};

  let DecoderMethod = "decodeFIORdA";
}

//===----------------------------------------------------------------------===//
// Out I/O instruction: <|1011|1AAr|rrrr|AAAA|>
// A = I/O location address = 6 bits
// d = destination = 5 bits
// (Accepts all registers)
//===----------------------------------------------------------------------===//
class FIOARr<dag outs, dag ins, string asmstr, list<dag> pattern>
    : AVRInst16<outs, ins, asmstr, pattern> {
  bits<6> A;
  bits<5> rr;

  let Inst{15 - 11} = 0b10111;
  let Inst{10 - 9} = A{5 - 4};
  let Inst{8 - 4} = rr;
  let Inst{3 - 0} = A{3 - 0};

  let DecoderMethod = "decodeFIOARr";
}

//===----------------------------------------------------------------------===//
// I/O bit instruction.
// <|1001|10tt|AAAA|Abbb>
// t = type (1 for SBI, 0 for CBI)
// A = I/O location address (5 bits)
// b = bit number
//===----------------------------------------------------------------------===//
class FIOBIT<bits<2> t, dag outs, dag ins, string asmstr, list<dag> pattern>
    : AVRInst16<outs, ins, asmstr, pattern> {
  bits<5> addr;
  bits<3> b;

  let Inst{15 - 12} = 0b1001;

  let Inst{11 - 10} = 0b10;
  let Inst{9 - 8} = t;

  let Inst{7 - 4} = addr{4 - 1};

  let Inst{3} = addr{0};
  let Inst{2 - 0} = b{2 - 0};

  let DecoderMethod = "decodeFIOBIT";
}

//===----------------------------------------------------------------------===//
// BST/BLD instruction.
// <|1111|1ttd|dddd|0bbb>
// t = type (1 for BST, 0 for BLD)
// d = destination register
// b = bit
//===----------------------------------------------------------------------===//
class FRdB<bits<2> t, dag outs, dag ins, string asmstr, list<dag> pattern>
    : AVRInst16<outs, ins, asmstr, pattern> {
  bits<5> rd;
  bits<3> b;

  let Inst{15 - 12} = 0b1111;

  let Inst{11} = 0b1;
  let Inst{10 - 9} = t;
  let Inst{8} = rd{4};

  let Inst{7 - 4} = rd{3 - 0};

  let Inst{3} = 0;
  let Inst{2 - 0} = b;
}

// Special encoding for the `DES K` instruction.
//
// <|1001|0100|KKKK|1011>
//
// KKKK = 4 bit immediate
class FDES<dag outs, dag ins, string asmstr, list<dag> pattern>
    : AVRInst16<outs, ins, asmstr, pattern> {
  bits<4> k;

  let Inst{15 - 12} = 0b1001;

  let Inst{11 - 8} = 0b0100;

  let Inst{7 - 4} = k;

  let Inst{3 - 0} = 0b1011;
}

//===----------------------------------------------------------------------===//
// Conditional Branching instructions: <|1111|0fkk|kkkk|ksss|>
// f = secondary opcode = 1 bit
// k = constant address = 7 bits
// s = bit in status register = 3 bits
//===----------------------------------------------------------------------===//
class FBRsk<bit f, bits<3> s, dag outs, dag ins, string asmstr,
            list<dag> pattern> : AVRInst16<outs, ins, asmstr, pattern> {
  bits<7> k;

  let Inst{15 - 11} = 0b11110;
  let Inst{10} = f;
  let Inst{9 - 3} = k;
  let Inst{2 - 0} = s;

  let DecoderMethod = "decodeCondBranch";
}

//===----------------------------------------------------------------------===//
// Special, opcode only instructions: <|opcode|>
//===----------------------------------------------------------------------===//

class F16<bits<16> opcode, dag outs, dag ins, string asmstr, list<dag> pattern>
    : AVRInst16<outs, ins, asmstr, pattern> {
  let Inst = opcode;
}

//===----------------------------------------------------------------------===//
// Branching instructions with immediate12: <|110f|kkkk|kkkk|kkkk|>
// f = secondary opcode = 1 bit
// k = constant address = 12 bits
//===----------------------------------------------------------------------===//
class FBRk<bit f, dag outs, dag ins, string asmstr, list<dag> pattern>
    : AVRInst16<outs, ins, asmstr, pattern> {
  bits<12> k;

  let Inst{15 - 13} = 0b110;
  let Inst{12} = f;
  let Inst{11 - 0} = k;

  let DecoderMethod = "decodeFBRk";
}

//===----------------------------------------------------------------------===//
// 32 bits branching instructions: <|1001|010k|kkkk|fffk|kkkk|kkkk|kkkk|kkkk|>
// f = secondary opcode = 3 bits
// k = constant address = 22 bits
//===----------------------------------------------------------------------===//
class F32BRk<bits<3> f, dag outs, dag ins, string asmstr, list<dag> pattern>
    : AVRInst32<outs, ins, asmstr, pattern> {
  bits<22> k;

  let Inst{31 - 25} = 0b1001010;
  let Inst{24 - 20} = k{21 - 17};
  let Inst{19 - 17} = f;
  let Inst{16 - 0} = k{16 - 0};
}

//===----------------------------------------------------------------------===//
// 32 bits direct mem instructions: <|1001|00fd|dddd|0000|kkkk|kkkk|kkkk|kkkk|>
// f = secondary opcode = 1 bit
// d = destination = 5 bits
// k = constant address = 16 bits
// (Accepts all registers)
//===----------------------------------------------------------------------===//
class F32DM<bit f, dag outs, dag ins, string asmstr, list<dag> pattern>
    : AVRInst32<outs, ins, asmstr, pattern> {
  bits<5> rd;
  bits<16> k;

  let Inst{31 - 28} = 0b1001;

  let Inst{27 - 26} = 0b00;
  let Inst{25} = f;
  let Inst{24} = rd{4};

  let Inst{23 - 20} = rd{3 - 0};

  let Inst{19 - 16} = 0b0000;

  let Inst{15 - 0} = k;
}

//===---------------------------------------------------------------------===//
// Special format for the LDS/STS instructions on AVRTiny.
// <|1010|ikkk|dddd|kkkk>
// d = R16 ~ R31
// i = 0 - lds, 1 - sts
// k = 7-bit data space address
//===---------------------------------------------------------------------===//
class FLDSSTSTINY<bit i, dag outs, dag ins, string asmstr, list<dag> pattern>
    : AVRInst16<outs, ins, asmstr, pattern> {
  bits<5> rd;
  bits<7> k;

  let Inst{15 - 12} = 0b1010;

  let Inst{11} = i;

  let Inst{10 - 8} = k{6 - 4};
  let Inst{7 - 4} = rd{3 - 0};
  let Inst{3 - 0} = k{3 - 0};

  let DecoderNamespace = "AVRTiny";
}

// <|1001|0100|bfff|1000>
class FS<bit b, dag outs, dag ins, string asmstr, list<dag> pattern>
    : AVRInst16<outs, ins, asmstr, pattern> {
  bits<3> s;

  let Inst{15 - 12} = 0b1001;

  let Inst{11 - 8} = 0b0100;

  let Inst{7} = b;
  let Inst{6 - 4} = s;

  let Inst{3 - 0} = 0b1000;
}

// Set/clr bit in status flag instructions/
// <BRBS|BRBC> s, k
// ---------------------
// <|1111|0fkk|kkkk|ksss>
class FSK<bit f, dag outs, dag ins, string asmstr, list<dag> pattern>
    : AVRInst16<outs, ins, asmstr, pattern> {
  bits<7> k;
  bits<3> s;

  let Inst{15 - 12} = 0b1111;

  let Inst{11} = 0;
  let Inst{10} = f;
  let Inst{9 - 8} = k{6 - 5};

  let Inst{7 - 4} = k{4 - 1};

  let Inst{3} = k{0};
  let Inst{2 - 0} = s;

  let DecoderMethod = "decodeCondBranch";
}

class ExtensionPseudo<dag outs, dag ins, string asmstr, list<dag> pattern>
    : Pseudo<outs, ins, asmstr, pattern> {
  let Defs = [SREG];
}

class StorePseudo<dag outs, dag ins, string asmstr, list<dag> pattern>
    : Pseudo<outs, ins, asmstr, pattern> {
  let Defs = [SP];
}

class SelectPseudo<dag outs, dag ins, string asmstr, list<dag> pattern>
    : Pseudo<outs, ins, asmstr, pattern> {
  let usesCustomInserter = 1;

  let Uses = [SREG];
}

class ShiftPseudo<dag outs, dag ins, string asmstr, list<dag> pattern>
    : Pseudo<outs, ins, asmstr, pattern> {
  let usesCustomInserter = 1;

  let Defs = [SREG];
}
