//===-- M68kInstrFormats.td - M68k Instruction Formats -----*- tablegen -*-===//
//                     The LLVM Compiler Infrastructure
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains M68k instruction formats.
///
/// Since M68k has quite a lot memory addressing modes there are more
/// instruction prefixes than just i, r and m:
/// TSF  Since     Form                     Letter  Description
///  00   M68000    Dn or An                 r       any register
///  01   M68000    Dn                       d       data register direct
///  02   M68000    An                       a       address register direct
///  03   M68000    (An)                     j       address register indirect
///  04   M68000    (An)+                    o       address register indirect with postincrement
///  05   M68000    -(An)                    e       address register indirect with predecrement
///  06   M68000    (d16,An)                 p       address register indirect with displacement
///  10   M68000    (d8,An,Xn.L)             f       address register indirect with index and scale = 1
///  07   M68000    (d8,An,Xn.W)             F       address register indirect with index and scale = 1
///  12   M68020    (d8,An,Xn.L,SCALE)       g       address register indirect with index
///  11   M68020    (d8,An,Xn.W,SCALE)       G       address register indirect with index
///  14   M68020    ([bd,An],Xn.L,SCALE,od)  u       memory indirect postindexed mode
///  13   M68020    ([bd,An],Xn.W,SCALE,od)  U       memory indirect postindexed mode
///  16   M68020    ([bd,An,Xn.L,SCALE],od)  v       memory indirect preindexed mode
///  15   M68020    ([bd,An,Xn.W,SCALE],od)  V       memory indirect preindexed mode
///  20   M68000    abs.L                    b       absolute long address
///  17   M68000    abs.W                    B       absolute short address
///  21   M68000    (d16,PC)                 q       program counter with displacement
///  23   M68000    (d8,PC,Xn.L)             k       program counter with index and scale = 1
///  22   M68000    (d8,PC,Xn.W)             K       program counter with index and scale = 1
///  25   M68020    (d8,PC,Xn.L,SCALE)       l       program counter with index
///  24   M68020    (d8,PC,Xn.W,SCALE)       L       program counter with index
///  27   M68020    ([bd,PC],Xn.L,SCALE,od)  x       program counter memory indirect postindexed mode
///  26   M68020    ([bd,PC],Xn.W,SCALE,od)  X       program counter memory indirect postindexed mode
///  31   M68020    ([bd,PC,Xn.L,SCALE],od)  y       program counter memory indirect preindexed mode
///  30   M68020    ([bd,PC,Xn.W,SCALE],od)  Y       program counter memory indirect preindexed mode
///  32   M68000    #immediate               i       immediate data
///
/// NOTE that long form is always lowercase, word variants are capitalized
///
/// Operand can be qualified with size where appropriate to force a particular
/// instruction encoding, e.g.:
///    (i8,An,Xn.W)             f8      1 extension word
///    (i16,An,Xn.W)            f16     2 extension words
///    (i32,An,Xn.W)            f32     3 extension words
///
/// Form without size qualifier will adapt to operand size automatically, e.g.:
///    (i,An,Xn.W)              f       1, 2 or 3 extension words
///
/// Some forms already imply a particular size of their operands, e.g.:
///    (i,An)                   p       1 extension word and i is 16bit
///
/// Operand order follows x86 Intel order(destination before source), e.g.:
///    MOV8df                   MOVE (4,A0,D0), D1
///
/// Number after instruction mnemonics determines the size of the data
///
//===----------------------------------------------------------------------===//

/// ??? Is it possible to use this stuff for disassembling?
/// NOTE 1: In case of conditional beads(DA, DAReg), cond part is able to
/// consume any bit, though a more general instructions must be chosen, e.g.
/// d -> r, a -> r

//===----------------------------------------------------------------------===//
// Encoding primitives
//===----------------------------------------------------------------------===//

class MxEncMemOp {
  dag EA = (ascend);
  dag Supplement = (ascend);
}

class MxEncBriefExt<string reg_opnd, string disp_opnd,
                    bit size_w_l = false, int scale = 1,
                    string disp_encoder = ""> {
  dag Value = (descend
    // D/A + REGISTER
    (operand "$"#reg_opnd, 4),
    // W/L
    size_w_l,
    // SCALE
    !cond(
      !eq(scale, 1) : 0b00,
      !eq(scale, 2) : 0b01,
      !eq(scale, 4) : 0b10,
      !eq(scale, 8) : 0b11
    ),
    0b0,
    // Displacement
    (operand "$"#disp_opnd, 8, (encoder disp_encoder))
  );
}

class MxEncAddrMode_d<string reg_opnd> : MxEncMemOp {
  let EA = (descend /*MODE*/0b000,
                    /*REGISTER*/(operand "$"#reg_opnd, 3));
}

class MxEncAddrMode_a<string reg_opnd> : MxEncMemOp {
  let EA = (descend /*MODE*/0b001,
                    /*REGISTER*/(operand "$"#reg_opnd, 3));
}

class MxEncAddrMode_r<string reg_opnd> : MxEncMemOp {
  let EA = (descend /*MODE without the last bit*/0b00,
                    /*REGISTER with D/A bit*/(operand "$"#reg_opnd, 4));
}

class MxEncAddrMode_k<string opnd_name> : MxEncMemOp {
  let EA = (descend /*MODE*/0b111,
                    /*REGISTER*/0b011);

  let Supplement = MxEncBriefExt<opnd_name#".index", opnd_name#".disp",
                                 /*W/L*/true, /*SCALE*/1,
                                 "encodePCRelImm<8>">.Value;
}

class MxEncAddrMode_q<string opnd_name> : MxEncMemOp {
  let EA = (descend /*MODE*/0b111,
                     /*REGISTER*/0b010);

  // 16-bit Displacement
  let Supplement = (operand "$"#opnd_name, 16,
                            (encoder "encodePCRelImm<16>"));
}

class MxEncAddrMode_p<string opnd_name> : MxEncMemOp {
  let EA = (descend /*MODE*/0b101,
                     /*REGISTER*/(operand "$"#opnd_name#".reg", 3));

  // 16-bit Displacement
  let Supplement = (operand "$"#opnd_name#".disp", 16,
                            (encoder "encodeRelocImm<16>"));
}

class MxEncAddrMode_f<string opnd_name> : MxEncMemOp {
  let EA = (descend /*MODE*/0b110,
                     /*REGISTER*/(operand "$"#opnd_name#".reg", 3));

  let Supplement = MxEncBriefExt<opnd_name#".index", opnd_name#".disp",
                                 /*W/L*/true, /*SCALE*/1,
                                 "encodeRelocImm<8>">.Value;
}

class MxEncAddrMode_j<string reg_opnd> : MxEncMemOp {
  let EA = (descend /*MODE*/0b010,
                     /*REGISTER*/(operand "$"#reg_opnd, 3));
}

class MxEncAddrMode_i<string opnd_name, int size> : MxEncMemOp {
  let EA = (descend /*MODE*/0b111,
                     /*REGISTER*/0b100);

  // Immediate
  let Supplement =
    !cond(
      !eq(size, 8)  : (descend 0b00000000, (operand "$"#opnd_name, 8,
	                   (encoder "encodeRelocImm<8>"))),
      !eq(size, 16) : (operand "$"#opnd_name, 16,
                           (encoder "encodeRelocImm<16>")),
      !eq(size, 32) : (operand "$"#opnd_name, 32,
                           (encoder "encodeRelocImm<32>"),
                           (decoder "DecodeImm32"))
    );
}

// abs.W -> size_w_l = false
// abs.L -> size_w_l = true
class MxEncAddrMode_abs<string opnd_name, bit size_w_l = false> : MxEncMemOp {
  let EA = (descend /*MODE*/0b111,
                    // Wrap the REGISTER part in another dag to make sure
                    // the dag assigned to EA only has two arguments. Such
                    // that it's easier for MOV instructions to reverse
                    // on its destination part.
                    /*REGISTER*/(descend 0b00, size_w_l));

  // Absolute address
  let Supplement = !if(size_w_l,
    // abs.L
    (operand "$"#opnd_name, 32, (encoder "encodeRelocImm<32>"),
                                (decoder "DecodeImm32")),
    // abs.W
    (operand "$"#opnd_name, 16, (encoder "encodeRelocImm<16>"))
  );
}

class MxEncAddrMode_o<string reg_opnd> : MxEncMemOp {
  let EA = (descend /*MODE*/0b011,
                    /*REGISTER*/(operand "$"#reg_opnd, 3));
}

class MxEncAddrMode_e<string reg_opnd> : MxEncMemOp {
  let EA = (descend /*MODE*/0b100,
                    /*REGISTER*/(operand "$"#reg_opnd, 3));
}

class MxEncSize<bits<2> value> {
  bits<2> Value = value;
}
def MxEncSize8  : MxEncSize<0b00>;
def MxEncSize16 : MxEncSize<0b01>;
def MxEncSize32 : MxEncSize<0b10>;
def MxEncSize64 : MxEncSize<0b11>;

// M68k INSTRUCTION. Most instructions specify the location of an operand by
// using the effective address field in the operation word. The effective address
// is composed of two 3-bit fields: the mode field and the register field. The
// value in the mode field selects the different address modes. The register
// field contains the number of a register.  The effective address field may
// require additional information to fully specify the operand. This additional
// information, called the effective address extension, is contained in the
// following word or words and is considered part of the instruction. The
// effective address modes are grouped into three categories: register direct,
// memory addressing, and special.
class MxInst<dag outs, dag ins,
             string asmStr = "",
             list<dag> pattern = [],
             InstrItinClass itin = NoItinerary>
    : Instruction {
  let Namespace      = "M68k";
  let OutOperandList = outs;
  let InOperandList  = ins;
  let AsmString      = asmStr;
  let Pattern        = pattern;
  let Itinerary      = itin;

  dag Inst = (ascend);

  // Number of bytes
  let Size = 0;

  let UseLogicalOperandMappings = 1;
}

// M68k PSEUDO INSTRUCTION
class MxPseudo<dag outs, dag ins, list<dag> pattern = []>
    : MxInst<outs, ins, "; error: this should not be emitted", pattern> {
  let isPseudo = 1;
}
