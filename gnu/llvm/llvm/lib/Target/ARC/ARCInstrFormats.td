//===- ARCInstrFormats.td - ARC Instruction Formats --------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Instruction format superclass
//===----------------------------------------------------------------------===//

class Encoding64 {
  field bits<64> Inst;
  field bits<64> SoftFail = 0;
}

// Address operands

class immU<int BSz> : Operand<i32>, PatLeaf<(imm),
    "\n    return isUInt<"#BSz#">(N->getSExtValue());"> {
}

def immU6 : immU<6>;

class immS<int BSz> : Operand<i32>, PatLeaf<(imm),
    "\n    return isInt<"#BSz#">(N->getSExtValue());"> {
  let DecoderMethod = "DecodeSignedOperand<"#BSz#">";
}

// e.g. s3 field may encode the signed integers values -1 .. 6
// using binary codes 111, 000, 001, 010, 011, 100, 101, and 110, respectively
class immC<int BSz> : Operand<i32>, PatLeaf<(imm),
    "\n    return isInt<"#BSz#">(N->getSExtValue());"> {
  let DecoderMethod = "DecodeFromCyclicRange<"#BSz#">";
}

def MEMii : Operand<i32> {
  let MIOperandInfo = (ops i32imm, i32imm);
}

def MEMrs9 : Operand<iAny> {
  let MIOperandInfo = (ops GPR32:$B, immS<9>:$S9);
  let PrintMethod = "printMemOperandRI";
  let DecoderMethod = "DecodeMEMrs9";
}

def MEMrlimm : Operand<iAny> {
  let MIOperandInfo = (ops GPR32:$B, i32imm:$LImm);
  let PrintMethod = "printMemOperandRI";
  let DecoderMethod = "DecodeMEMrlimm";
}

def GPR32Reduced : Operand<iAny> {
  let DecoderMethod = "DecodeGBR32ShortRegister";
}

// Helper classes for load/store instructions
class DataSizeMode<bits<2> mode, string instSfx, string asmSfx> {
  bits<2> Value = mode;
  string  InstSuffix = instSfx;
  string  AsmSuffix  = asmSfx;
}
class ExtMode<bit mode, string instSfx, string asmSfx> {
  bit     Value = mode;
  string  InstSuffix = instSfx;
  string  AsmSuffix  = asmSfx;
}

class AddrMode<bits<2> mode, string instSfx, string asmSfx> {
  bits<2> Value = mode;
  string  InstSuffix = instSfx;
  string  AsmSuffix  = asmSfx;
}

class CacheMode<bit mode, string instSfx, string asmSfx> {
  bit     Value = mode;
  string  InstSuffix = instSfx;
  string  AsmSuffix  = asmSfx;
}

def ByteSM : DataSizeMode<0b01, "B", "b">;
def HalfSM : DataSizeMode<0b10, "H", "h">;
def WordSM : DataSizeMode<0b00,  "",  "">;

def NoEM      : ExtMode<0,   "",   "">;
def SignedEM  : ExtMode<1, "_X", ".x">;

def NoAM      : AddrMode<0b00, "", "">;
def PreIncAM  : AddrMode<0b01, "_AW", ".aw">;
def PostIncAM : AddrMode<0b10, "_AB", ".ab">;

def NoCC       : CacheMode<0b0,    "",    "">;
def UncachedCC : CacheMode<0b1, "_DI", ".di">;

class InstARC<int sz, dag outs, dag ins, string asmstr, list<dag> pattern>
    : Instruction, Encoding64 {

  let Namespace = "ARC";
  dag OutOperandList = outs;
  dag InOperandList = ins;
  let AsmString = asmstr;
  let Pattern = pattern;
  let Size = sz;

  // Load/Store instruction properties
  DataSizeMode ZZ = WordSM;
  ExtMode X = NoEM;
  AddrMode AA = NoAM;
  CacheMode DI = NoCC;

  // Field used for relation models
  string BaseOpcode = "";

  //TSFlags
  let TSFlags{1-0} = AA.Value;
}

// ARC pseudo instructions format
class PseudoInstARC<dag outs, dag ins, string asmstr, list<dag> pattern>
   : InstARC<0, outs, ins, asmstr, pattern> {
  let isPseudo = 1;
}

//===----------------------------------------------------------------------===//
// Instruction formats
//===----------------------------------------------------------------------===//

// All 32-bit ARC instructions have a 5-bit "major" opcode class designator
// in bits 27-31.
//
// Some general naming conventions:
// N  - Delay Slot bit.  ARC v2 branch instructions have an optional delay slot
//      which is encoded with this bit.  When set, a delay slot exists.
// cc - Condition code.
// SX - Signed X-bit immediate.
// UX - Unsigned X-bit immediate.
//
// [ABC] - 32-bit register operand.  These are 6-bit fields.  This encodes the
//         standard 32 general purpose registers, and allows use of additional
//         (extension) registers.  This also encodes an instruction that uses
//         a 32-bit Long Immediate (LImm), using 0x3e==62 as the field value.
//         This makes 32-bit format instructions with Long Immediates
//         64-bit instructions, with the Long Immediate in bits 32-63.
// A - Inst[5-0] = A[5-0], when the format has A.  A is always a register.
// B - Inst[14-12] = B[5-3], Inst[26-24] = B[2-0], when the format has B.
//     B is always a register.
// C - Inst[11-6] = C[5-0], when the format has C.  C can either be a register,
//     or a 6-bit unsigned immediate (immU6), depending on the format.
// F - Many instructions specify a flag bit. When set, the result of these
//     instructions will set the ZNCV flags of the STATUS32 register
//     (Zero/Negative/Carry/oVerflow).

// Branch Instructions.
class F32_BR<bits<5> major, dag outs, dag ins, bit b16, string asmstr,
             list<dag> pattern> :
  InstARC<4, outs, ins, asmstr, pattern> {
  bit N;

  let Inst{31-27} = major;
  let Inst{16} = b16;
  let Inst{5} = N;
}

class F32_BR_COND<bits<5> major, dag outs, dag ins, bit b16, string asmstr,
                  list<dag> pattern> :
  F32_BR<major, outs, ins, b16, asmstr, pattern> {
  bits<21> S21; // 2-byte aligned 21-bit byte-offset.
  bits<5> cc;
  let Inst{26-18} = S21{10-2};
  let Inst{15-6} = S21{20-11};
  let Inst{4-0} = cc;
}

class F32_BR_UCOND_FAR<bits<5> major, dag outs, dag ins, bit b16, string asmstr,
                       list<dag> pattern> :
  F32_BR<major, outs, ins, b16, asmstr, pattern> {
  bits<25> S25; // 2-byte aligned 25-bit byte-offset.
  let Inst{26-18} = S25{10-2};
  let Inst{15-6} = S25{20-11};
  let Inst{4} = 0;
  let Inst{3-0} = S25{24-21};
}

class F32_BR0_COND<dag outs, dag ins, string asmstr, list<dag> pat> :
  F32_BR_COND<0b00000, outs, ins, 0, asmstr, pat> {
  let Inst{17} = S21{1};
}

// Branch targets are 2-byte aligned, so S25[0] is implied 0.
// |26|25|24|23|22|21|20|19|18|17|16|15|14|13|12|11|10|9|8|7|6|5|4|3|2|1|0   |
// |S25[10-1]                    | 1|S25[20-11]               |N|0|S25[24-21]|
class F32_BR0_UCOND_FAR<dag outs, dag ins, string asmstr, list<dag> pat> :
  F32_BR_UCOND_FAR<0b00000, outs, ins, 1, asmstr, pat> {
  let Inst{17} = S25{1};
}

// BL targets (functions) are 4-byte aligned, so S25[1-0] = 0b00
// |26|25|24|23|22|21|20|19|18|17|16|15|14|13|12|11|10|9|8|7|6|5|4|3|2|1|0   |
// |S25[10-2]                 | 1| 0|S25[20-11]               |N|0|S25[24-21]|
class F32_BR1_BL_UCOND_FAR<dag outs, dag ins, string asmstr, list<dag> pat> :
  F32_BR_UCOND_FAR<0b00001, outs, ins, 0, asmstr, pat> {
  let Inst{17} = 1;
}

// BLcc targets have 21 bit range, and are 4-byte aligned.
// |26|25|24|23|22|21|20|19|18|17|16|15|14|13|12|11|10|9|8|7|6|5|4|3|2|1|0|
// |S25[10-2]                 | 0| 0|S25[20-11]               |N|0|cc     |
class F32_BR1_BL_COND<dag outs, dag ins, string asmstr, list<dag> pat> :
  F32_BR_COND<0b00001, outs, ins, 0, asmstr, pat> {
  let Inst{17} = 0;
}

// BRcc targets have limited 9-bit range.  These are for compare and branch
// in single instruction.  Their targets are 2-byte aligned.  They also use
// a different (3-bit) set of condition codes.
// |26|25|24|23|22|21|20|19|18|17|16|15   |14|13|12|11|10|9|8|7|6|5|4|3|2|1|0|
// |B[2-0]  |S9[7-1]             | 1|S9[8]|B[5-3]  |C            |N|u|0|cc   |
class F32_BR1_BCC<dag outs, dag ins, string asmstr, bit IsU6,
                  list<dag> pattern> :
  InstARC<4, outs, ins, asmstr, pattern> {

  bits<3> cc;
  bits<6> B;
  bits<6> C;
  bit N;
  bits<9> S9; // 2-byte aligned 9-bit byte-offset.

  let Inst{31-27} = 0b00001;
  let Inst{26-24} = B{2-0};
  let Inst{23-17} = S9{7-1};
  let Inst{16} = 1;
  let Inst{15} = S9{8};
  let Inst{14-12} = B{5-3};
  let Inst{11-6} = C;
  let Inst{5} = N;
  let Inst{4} = IsU6;
  let Inst{3} = 0;
  let Inst{2-0} = cc;
}

// General operations instructions.
// Single Operand Instructions.  Inst[5-0] specifies the specific operation
// for this format.
// |26|25|24|23|22|21|20|19|18|17|16|15|14|13|12|11|10|9|8|7|6|5|4|3|2|1|0|
// |B[2-0]  | 0| 0| 1| 0| 1| 1| 1| 1| F|B[5-3]  |C            |subop      |
class F32_SOP_RR<bits<5> major, bits<6> subop, bit F, dag outs, dag ins,
                 string asmstr, list<dag> pattern> :
  InstARC<4, outs, ins, asmstr, pattern> {

  bits<6> C;
  bits<6> B;

  let Inst{31-27} = major;
  let Inst{26-24} = B{2-0};
  let Inst{23-22} = 0b00;
  let Inst{21-16} = 0b101111;
  let Inst{15} = F;
  let Inst{14-12} = B{5-3};
  let Inst{11-6} = C;
  let Inst{5-0} = subop;
}

// Dual Operand Instructions.  Inst[21-16] specifies the specific operation
// for this format.

// 3-register Dual Operand instruction.
// |26|25|24|23|22|21|20|19|18|17|16|15|14|13|12|11|10|9|8|7|6|5|4|3|2|1|0|
// |B[2-0]  | 0| 0|            subop| F|B[5-3]  |C            |A          |
class F32_DOP_RR<bits<5> major, bits<6> subop, bit F, dag outs, dag ins,
                 string asmstr, list<dag> pattern> :
  InstARC<4, outs, ins, asmstr, pattern> {
  bits<6> C;
  bits<6> B;
  bits<6> A;

  let Inst{31-27} = major;
  let Inst{26-24} = B{2-0};
  let Inst{23-22} = 0b00;
  let Inst{21-16} = subop;
  let Inst{15} = F;
  let Inst{14-12} = B{5-3};
  let Inst{11-6} = C;
  let Inst{5-0} = A;
}

// Conditional Dual Operand instruction.  This instruction uses B as the
// first 2 operands (i.e, add.cc B, B, C).
// |26|25|24|23|22|21|20|19|18|17|16|15|14|13|12|11|10|9|8|7|6|5|4|3|2|1|0|
// |B[2-0]  | 1| 1|            subop| F|B[5-3]  |C            |A          |
class F32_DOP_CC_RR<bits<5> major, bits<6> subop, bit F, dag outs, dag ins,
                    string asmstr, list<dag> pattern> :
  InstARC<4, outs, ins, asmstr, pattern> {
  bits<5> cc;
  bits<6> C;
  bits<6> B;

  let Inst{31-27} = major;
  let Inst{26-24} = B{2-0};
  let Inst{23-22} = 0b11;
  let Inst{21-16} = subop;
  let Inst{15} = F;
  let Inst{14-12} = B{5-3};
  let Inst{11-6} = C;
  let Inst{5} = 0;
  let Inst{4-0} = cc;
}


// 2-register, unsigned 6-bit immediate Dual Operand instruction.
// |26|25|24|23|22|21|20|19|18|17|16|15|14|13|12|11|10|9|8|7|6|5|4|3|2|1|0|
// |B[2-0]  | 0| 1|            subop| F|B[5-3]  |U6           |A          |
class F32_DOP_RU6<bits<5> major, bits<6> subop, bit F, dag outs, dag ins,
                  string asmstr, list<dag> pattern> :
  InstARC<4, outs, ins, asmstr, pattern> {
  bits<6> U6;
  bits<6> B;
  bits<6> A;

  let Inst{31-27} = major;
  let Inst{26-24} = B{2-0};
  let Inst{23-22} = 0b01;
  let Inst{21-16} = subop;
  let Inst{15} = F;
  let Inst{14-12} = B{5-3};
  let Inst{11-6} = U6;
  let Inst{5-0} = A;
}

// 1-register, unsigned 6-bit, immediate Dual Operand instruction with
// condition code.
// |26|25|24|23|22|21|20|19|18|17|16|15|14|13|12|11|10|9|8|7|6|5|4|3|2|1|0|
// |B[2-0]  | 1| 1|            subop| F|B[5-3]  |U6           |1|cc       |
class F32_DOP_CC_RU6<bits<5> major, bits<6> subop, bit F, dag outs, dag ins,
                     string asmstr, list<dag> pattern> :
  InstARC<4, outs, ins, asmstr, pattern> {

  bits<5> cc;
  bits<6> U6;
  bits<6> B;

  let Inst{31-27} = major;
  let Inst{26-24} = B{2-0};
  let Inst{23-22} = 0b11;
  let Inst{21-16} = subop;
  let Inst{15} = F;
  let Inst{14-12} = B{5-3};
  let Inst{11-6} = U6;
  let Inst{5} = 1;
  let Inst{4-0} = cc;

  let DecoderMethod = "DecodeCCRU6Instruction";
}

// 2-register, unsigned 6-bit immediate Dual Operand instruction with
// condition code. This instruction uses B as the first 2 operands
// (i.e, add.cc B, B, u6).
// |26|25|24|23|22|21|20|19|18|17|16|15|14|13|12|11|10|9|8|7|6|5|4|3|2|1|0|
// |B[2-0]  | 1| 1|            subop| F|B[5-3]  |U6           |1|cc       |
class F32_DOP_CC_RRU6<bits<5> major, bits<6> subop, bit F, dag outs, dag ins,
                      string asmstr, list<dag> pattern> :
  InstARC<4, outs, ins, asmstr, pattern> {
  bits<5> cc;
  bits<6> U6;
  bits<6> B;

  let Inst{31-27} = major;
  let Inst{26-24} = B{2-0};
  let Inst{23-22} = 0b11;
  let Inst{21-16} = subop;
  let Inst{15} = F;
  let Inst{14-12} = B{5-3};
  let Inst{11-6} = U6;
  let Inst{5} = 1;
  let Inst{4-0} = cc;
}

// 2-register, signed 12-bit immediate Dual Operand instruction.
// This instruction uses B as the first 2 operands (i.e., add B, B, -128).
// |26|25|24|23|22|21|20|19|18|17|16|15|14|13|12|11|10|9|8|7|6|5|4|3|2|1|0|
// |B[2-0]  | 1| 0|            subop| F|B[5-3]  |S12[5-0]     |S12[11-6]  |
class F32_DOP_RS12<bits<5> major, bits<6> subop, bit F, dag outs, dag ins,
                   string asmstr, list<dag> pattern> :
  InstARC<4, outs, ins, asmstr, pattern> {
  bits<6> B;
  bits<12> S12;

  let Inst{31-27} = major;
  let Inst{26-24} = B{2-0};
  let Inst{23-22} = 0b10;
  let Inst{21-16} = subop;
  let Inst{15} = F;
  let Inst{14-12} = B{5-3};
  let Inst{11-6} = S12{5-0};
  let Inst{5-0} = S12{11-6};
}

// 1-register, signed 12-bit immediate Dual Operand instruction.
// This instruction uses B as the first operand (i.e., lr B, [%count0]).
// |26|25|24|23|22|21|20|19|18|17|16|15|14|13|12|11|10|9|8|7|6|5|4|3|2|1|0|
// |B[2-0]  | 1| 0|            subop| F|B[5-3]  |S12[5-0]     |S12[11-6]  |
class F32_SOP_RS12<bits<5> major, bits<6> subop, bit F, dag outs, dag ins,
                   string asmstr, list<dag> pattern> :
  InstARC<4, outs, ins, asmstr, pattern> {
  bits<6> B;
  bits<12> S12;

  let Inst{31-27} = major;
  let Inst{26-24} = B{2-0};
  let Inst{23-22} = 0b10;
  let Inst{21-16} = subop;
  let Inst{15} = F;
  let Inst{14-12} = B{5-3};
  let Inst{11-6} = S12{5-0};
  let Inst{5-0} = S12{11-6};

  let DecoderMethod = "DecodeSOPwithRS12";
}

// 1-register, unsigned 6-bit immediate Dual Operand instruction.
// This instruction uses B as the first operand.
// |26|25|24|23|22|21|20|19|18|17|16|15|14|13|12|11|10|9|8|7|6|5|4|3|2|1|0|
// |B[2-0]  | 0| 1|            subop| F|B[5-3]  |U6           |0|0|0|0|0|0|
class F32_SOP_RU6<bits<5> major, bits<6> subop, bit F, dag outs, dag ins,
                  string asmstr, list<dag> pattern> :
  InstARC<4, outs, ins, asmstr, pattern> {
  bits<6> B;
  bits<6> U6;

  let Inst{31-27} = major;
  let Inst{26-24} = B{2-0};
  let Inst{23-22} = 0b01;
  let Inst{21-16} = subop;
  let Inst{15} = F;
  let Inst{14-12} = B{5-3};
  let Inst{11-6} = U6;
  let Inst{5-0} = 0;

  let DecoderMethod = "DecodeSOPwithRU6";
}

// 2-register, 32-bit immediate (LImm) Dual Operand instruction.
// This instruction has the 32-bit immediate in bits 32-63, and
// 62 in the C register operand slot, but is otherwise F32_DOP_RR.
class F32_DOP_RLIMM<bits<5> major, bits<6> subop, bit F, dag outs, dag ins,
                    string asmstr, list<dag> pattern> :
  InstARC<8, outs, ins, asmstr, pattern> {
  bits<6> B;
  bits<6> A;
  bits<32> LImm;

  let Inst{63-32} = LImm;
  let Inst{31-27} = major;
  let Inst{26-24} = B{2-0};
  let Inst{23-22} = 0b00;
  let Inst{21-16} = subop;
  let Inst{15} = F;
  let Inst{14-12} = B{5-3};
  let Inst{11-6} = 0b111110;
  let Inst{5-0} = A;
}


// Load and store instructions.
// In addition to the previous naming conventions, load and store instructions
// have:
// di - Uncached bit.  When set, loads/stores bypass the cache and access
//      memory directly.
// aa - Incrementing mode.  Loads and stores can write-back address pre- or
//      post- memory operation.
// zz - Memory size (can be 8/16/32 bit load/store).
//  x - Sign-extending.  When set, short loads can be sign-extended to 32-bits.
// Loads and Stores support different memory addressing modes:
// Base Register + Signed 9-bit Immediate: Both Load/Store.
// LImm: Both Load/Store (Load/Store from a fixed 32-bit address).
// Register + Register: Load Only.
// Register + LImm: Load Only.

// Register + S9 Load. (B + S9)
// |26|25|24|23|22|21|20|19|18|17|16|15   |14|13|12|11|10|9|8|7|6|5|4|3|2|1|0|
// |B[2-0]  |S9[7-0]                |S9[8]|B[5-3]  |di|aa  |zz |x|A          |
class F32_LD_RS9<bit x, bits<2> aa, bit di, bits<2> zz, dag outs, dag ins,
                 string asmstr, list<dag> pattern> :
  InstARC<4, outs, ins, asmstr, pattern> {
  bits<6> B;
  bits<6> A;
  bits<9> S9;

  let Inst{31-27} = 0b00010;
  let Inst{26-24} = B{2-0};
  let Inst{23-16} = S9{7-0};
  let Inst{15} = S9{8};
  let Inst{14-12} = B{5-3};
  let Inst{11} = di;
  let Inst{10-9} = aa;
  let Inst{8-7} = zz;
  let Inst{6} = x;
  let Inst{5-0} = A;

  let BaseOpcode = "ld_rs9";
}

class F32_LD_ADDR<bit x, bits<2> aa, bit di, bits<2> zz, dag outs, dag ins,
                  string asmstr, list<dag> pattern> :
  F32_LD_RS9<x, aa, di, zz, outs, ins, asmstr, pattern> {
  bits<15> addr;

  let B = addr{14-9};
  let S9 = addr{8-0};

  let BaseOpcode = "ld_rs9";
}


// LImm Load.  The 32-bit immediate address is in Inst[63-32].
// |26|25|24|23|22|21|20|19|18|17|16|15|14|13|12|11|10|9|8|7|6|5|4|3|2|1|0|
// | 1| 1| 0| 0                        | 1| 1| 1|di| 0|0|zz |x|A          |
class F32_LD_LIMM<bit x, bit di, bits<2> zz, dag outs, dag ins,
                  string asmstr, list<dag> pattern> :
  InstARC<8, outs, ins, asmstr, pattern> {
  bits<6> LImmReg = 0b111110;
  bits<6> A;
  bits<32> LImm;

  let Inst{63-32} = LImm;
  let Inst{31-27} = 0b00010;
  let Inst{26-24} = LImmReg{2-0};
  let Inst{23-15} = 0;
  let Inst{14-12} = LImmReg{5-3};
  let Inst{11} = di;
  let Inst{10-9} = 0;
  let Inst{8-7} = zz;
  let Inst{6} = x;
  let Inst{5-0} = A;
  let DecoderMethod = "DecodeLdLImmInstruction";

  let BaseOpcode = "ld_limm";
}

// Register + LImm load.  The 32-bit immediate address is in Inst[63-32].
// |26|25|24|23|22|21|20|19|18|17|16|15|14|13|12|11|10|9|8|7|6|5|4|3|2|1|0|
// |B[2-0]  |aa   | 1| 1| 0|zz   | x|di|B[5-3]  | 1| 1|1|1|1|0|A          |
class F32_LD_RLIMM<bit x, bits<2> aa, bit di, bits<2> zz, dag outs, dag ins,
                   string asmstr, list<dag> pattern> :
  InstARC<8, outs, ins, asmstr, pattern> {
  bits<6> LImmReg = 0b111110;
  bits<32> LImm;
  bits<6> B;
  bits<6> A;
  bits<38> addr;
  let B = addr{37-32};
  let LImm = addr{31-0};

  let Inst{63-32} = LImm;
  let Inst{31-27} = 0b00100;
  let Inst{26-24} = B{2-0};
  let Inst{23-22} = aa;
  let Inst{21-19} = 0b110;
  let Inst{18-17} = zz;
  let Inst{16} = x;
  let Inst{15} = di;
  let Inst{14-12} = B{5-3};
  let Inst{11-6} = LImmReg;
  let Inst{5-0} = A;
  let DecoderMethod = "DecodeLdRLImmInstruction";

  let BaseOpcode = "ld_rlimm";
}

// Register + S9 Store. (B + S9)
// |26|25|24|23|22|21|20|19|18|17|16|15   |14|13|12|11|10|9|8|7|6|5 |4|3|2|1|0|
// |B[2-0]  |S9[7-0]                |S9[8]|B[5-3]  |C            |di|aa |zz |0|
class F32_ST_RS9<bits<2> aa, bit di, bits<2> zz, dag outs, dag ins,
                 string asmstr, list<dag> pattern> :
  InstARC<4, outs, ins, asmstr, pattern> {
  bits<6> B;
  bits<6> C;
  bits<9> S9;

  let Inst{31-27} = 0b00011;
  let Inst{26-24} = B{2-0};
  let Inst{23-16} = S9{7-0};
  let Inst{15} = S9{8};
  let Inst{14-12} = B{5-3};
  let Inst{11-6} = C;
  let Inst{5} = di;
  let Inst{4-3} = aa;
  let Inst{2-1} = zz;
  let Inst{0} = 0;

  let BaseOpcode = "st_rs9";
}

class F32_ST_ADDR<bits<2> aa, bit di, bits<2> zz, dag outs, dag ins,
                  string asmstr, list<dag> pattern> :
  F32_ST_RS9<aa, di, zz, outs, ins, asmstr, pattern> {
  bits<15> addr;

  let B = addr{14-9};
  let S9 = addr{8-0};

  let BaseOpcode = "st_rs9";
}

// LImm Store.
// |26|25|24|23|22|21|20|19|18|17|16|15|14|13|12|11|10|9|8|7|6|5 |4|3|2|1|0|
// | 1| 1| 0| 0                        | 1| 1| 1|C            |di|0|0|zz |0|
class F32_ST_LIMM<bit di, bits<2> zz, dag outs, dag ins,
                  string asmstr, list<dag> pattern> :
  InstARC<8, outs, ins, asmstr, pattern> {
  bits<6> LImmReg = 0b111110;
  bits<6> C;
  bits<32> LImm;

  let Inst{63-32} = LImm;
  let Inst{31-27} = 0b00011;
  let Inst{26-24} = LImmReg{2-0};
  let Inst{23-15} = 0;
  let Inst{14-12} = LImmReg{5-3};
  let Inst{11-6} = C;
  let Inst{5} = di;
  let Inst{4-3} = 0;
  let Inst{2-1} = zz;
  let Inst{0} = 0;
  let DecoderMethod = "DecodeStLImmInstruction";

  let BaseOpcode = "st_limm";
}

// Compact Move/Load.
// |10|9|8|7|6|5|4|3|2|1|0|
// |      |h    |   |i|H  |
class F16_COMPACT<bits<1> i, dag outs, dag ins,
                 string asmstr> :
  InstARC<2, outs, ins, asmstr, []> {

  bits<5> h;

  let Inst{15-11} = 0b01000;
  let Inst{7-5} = h{2-0};
  let Inst{2} = i;
  let Inst{1-0} = h{4-3};
}

// Compact Load/Add/Sub.
class F16_LD_ADD_SUB<dag outs, dag ins, string asmstr> :
  InstARC<2, outs, ins, asmstr, []> {

  bits<3> b;
  let Inst{15-11} = 0b01001;
  let Inst{10-8} = b;
}

class F16_LD_SUB<bit i, string asmstr> :
  F16_LD_ADD_SUB<(outs GPR32:$a), (ins GPR32:$b, GPR32:$c),
  asmstr> {

  bits<3> a;
  bits<3> c;

  let Inst{7-5} = c;
  let Inst{4} = i;
  let Inst{3} = 0;
  let Inst{2-0} = a;
}

class F16_ADD :
  F16_LD_ADD_SUB<(outs GPR32:$r), (ins GPR32:$b, immU<6>:$u6),
  "add_s\t$r, $b, $u6"> {

  bit r;
  bits<6> u6;

  let Inst{7} = r;
  let Inst{6-4} = u6{5-3};
  let Inst{3} = 1;
  let Inst{2-0} = u6{2-0};
}

// Compact Load/Store.
class F16_LD_ST_1<dag outs, dag ins, string asmstr> :
  InstARC<2, outs, ins, asmstr, []> {

  let Inst{15-11} = 0b01010;
}

class F16_LD_ST_s11<bit i, string asmstr> :
  F16_LD_ST_1<(outs), (ins immS<11>:$s11), asmstr> {

  bits<11> s11;

  let Inst{10-5} = s11{10-5};
  let Inst{4} = i;
  let Inst{3} = 0;
  let Inst{2-0} = s11{4-2};
  let s11{1-0} = 0b00;
}

class F16_LDI_u7 :
  F16_LD_ST_1<(outs GPR32:$b), (ins immU<7>:$u7),
  "ldi_s\t$b, [$u7]"> {

  bits<3> b;
  bits<7> u7;

  let Inst{10-8} = b;
  let Inst{7-4} = u7{6-3};
  let Inst{3} = 1;
  let Inst{2-0} = u7{2-0};
}

// Indexed Jump or Execute.
class F16_JLI_EI<bit i, string asmstr> :
  InstARC<2, (outs), (ins immU<10>:$u10),
  !strconcat(asmstr, "\t$u10"), []> {

  bits<10> u10;

  let Inst{15-11} = 0b01011;
  let Inst{10} = i;
  let Inst{9-0} = u10;
}

// Load/Add Register-Register.
class F16_LD_ADD_RR<bits<2> i, string asmstr> :
  InstARC<2, (outs GPR32:$a), (ins GPR32:$b, GPR32:$c),
  asmstr, []> {

  bits<3> a;
  bits<3> b;
  bits<3> c;

  let Inst{15-11} = 0b01100;
  let Inst{10-8} = b;
  let Inst{7-5} = c;
  let Inst{4-3} = i;
  let Inst{2-0} = a;
}

// Load/Add GP-Relative.
class F16_GP_LD_ADD<bits<2> i, dag ins, string asmstr> :
  InstARC<2, (outs), ins, asmstr, []> {

  let Inst{15-11} = 0b11001;
  let Inst{10-9} = i;
}

// Add/Sub/Shift Register-Immediate.
// |10|9|8|7|6|5|4|3|2|1|0|
// |b     |c    |i  |u    |
class F16_ADD_IMM<bits<2> i, string asmstr> :
  InstARC<2, (outs GPR32:$c), (ins GPR32:$b, immU<3>:$u3),
  !strconcat(asmstr, "\t$c, $b, $u3"), []> {

  bits<3> b;
  bits<3> c;
  bits<3> u3;

  let Inst{15-11} = 0b01101;
  let Inst{10-8} = b;
  let Inst{7-5} = c;
  let Inst{4-3} = i;
  let Inst{2-0} = u3;
}

// Dual Register Operations.
// |10|9|8|7|6|5|4|3|2|1|0|
// |b/s   |h    |i    |H  |
class F16_OP_HREG<bits<3> i, dag outs, dag ins, string asmstr> :
  InstARC<2, outs, ins, asmstr, []> {

  bits<3> b_s3;
  bits<5> h;

  let Inst{15-11} = 0b01110;
  let Inst{10-8} = b_s3;
  let Inst{7-5} = h{2-0};
  let Inst{4-2} = i;
  let Inst{1-0} = h{4-3};
}

class F16_OP_HREG30<bits<3> i, dag outs, dag ins, string asmstr> :
  F16_OP_HREG<i, outs, ins, asmstr> {

  bits<5> LImmReg = 0b11110;
  let Inst{7-5} = LImmReg{2-0};
  let Inst{1-0} = LImmReg{4-3};
}

class F16_OP_HREG_LIMM<bits<3> i, dag outs, dag ins, string asmstr> :
  F16_OP_HREG30<i, outs, ins, asmstr> {

  bits<32> LImm;
  let Inst{47-16} = LImm;
  let Size = 6;
}

// General compact DOP format.
class F16_GEN_DOP_BASE<bits<5> i, dag outs, dag ins, string asmstr> :
  InstARC<2, outs, ins, asmstr, []> {

  bits<3> b;
  bits<3> c;
  let Inst{15-11} = 0b01111;
  let Inst{10-8} = b;
  let Inst{7-5} = c;
  let Inst{4-0} = i;
}

class F16_GEN_DOP<bits<5> i, string asmstr> :
  F16_GEN_DOP_BASE<i, (outs GPR32:$b), (ins GPR32:$c),
  !strconcat(asmstr, "\t$b, $b, $c")>;

class F16_GEN_DOP_NODST<bits<5> i, string asmstr> :
  F16_GEN_DOP_BASE<i, (outs), (ins GPR32:$b, GPR32:$c),
  !strconcat(asmstr, "\t$b, $c")>;

class F16_GEN_DOP_SINGLESRC<bits<5> i, string asmstr> :
  F16_GEN_DOP_BASE<i, (outs GPR32:$b), (ins GPR32:$c),
  !strconcat(asmstr, "\t$b, $c")>;

class F16_GEN_SOP_BASE<bits<3> i, dag outs, dag ins, string asmstr> :
  F16_GEN_DOP_BASE<0b00000, outs, ins, asmstr> {

  let c = i;
}

class F16_GEN_SOP<bits<3> i, string asmstr> :
  F16_GEN_SOP_BASE<i, (outs), (ins GPR32:$b), asmstr>;

class F16_GEN_ZOP<bits<3> i, string asmstr> :
  F16_GEN_SOP_BASE<0b111, (outs), (ins), asmstr> {

  let b = i;
}

// Compact Load/Store with Offset Format.
class F16_LD_ST_OFF<bits<5> opc, dag outs, dag ins, string asmstr> :
  InstARC<2, outs, ins, !strconcat(asmstr, "\t$c, [$b, $off]"), []> {

  bits<3> b;
  bits<3> c;
  let Inst{15-11} = opc;
  let Inst{10-8} = b;
  let Inst{7-5} = c;
}

class F16_LD_ST_WORD_OFF<bits<5> opc, dag outs, dag ins, string asmstr> :
  F16_LD_ST_OFF<opc, outs, ins, asmstr> {

  bits<7> off;
  let Inst{4-0} = off{6-2};
  let off{1-0} = 0b00;
}

class F16_LD_ST_HALF_OFF<bits<5> opc, dag outs, dag ins, string asmstr> :
  F16_LD_ST_OFF<opc, outs, ins, asmstr> {

  bits<6> off;
  let Inst{4-0} = off{5-1};
  let off{0} = 0b0;
}

class F16_LD_ST_BYTE_OFF<bits<5> opc, dag outs, dag ins, string asmstr> :
  F16_LD_ST_OFF<opc, outs, ins, asmstr> {

  bits<5> off;
  let Inst{4-0} = off;
}

// Shift/Subtract/Bit Immediate.
// |10|9|8|7|6|5|4|3|2|1|0|
// |b     |i    |u        |
class F16_SH_SUB_BIT<bits<3> i, string asmstr> :
  InstARC<2, (outs), (ins GPR32:$b, immU<5>:$u5), asmstr, []> {

  bits<3> b;
  bits<5> u5;

  let Inst{15-11} = 0b10111;
  let Inst{10-8} = b;
  let Inst{7-5} = i;
  let Inst{4-0} = u5;
}

class F16_SH_SUB_BIT_DST<bits<3> i, string asmstr> :
  F16_SH_SUB_BIT<i, !strconcat(asmstr, "\t$b, $b, $u5")>;

// 16-bit stack-based operations.
// |10|9|8|7|6|5|4|3|2|1|0|
// |b     |i    |u        |
class F16_SP_OPS<bits<3> i,
  dag outs, dag ins, string asmstr> :
  InstARC<2, outs, ins, asmstr, []> {

  bits<3> fieldB;
  bits<5> fieldU;

  let Inst{15-11} = 0b11000;
  let Inst{10-8} = fieldB;
  let Inst{7-5} = i;
  let Inst{4-0} = fieldU;
}

class F16_SP_OPS_u7_aligned<bits<3> i,
  dag outs, dag ins, string asmstr> :
  F16_SP_OPS<i, outs, ins, asmstr> {

  bits<3> b3;
  bits<7> u7;

  let fieldB = b3;
  let fieldU = u7{6-2};
  let u7{1-0} = 0b00;
}

class F16_SP_OPS_bconst<bits<3> b, string asmop> :
  F16_SP_OPS_u7_aligned<0b101,
  (outs), (ins immU<7>:$u7),
  !strconcat(asmop, "\t%sp, %sp, $u7")> {

  let fieldB = b;
}

class F16_SP_OPS_uconst<bits<3> i,
  dag outs, dag ins, string asmop> :
  F16_SP_OPS_u7_aligned<i, outs, ins,
  !strconcat(asmop, "\t$b3")> {

  let fieldU = 0b00001;
}

class F16_SP_OPS_buconst<bits<3> i, string asmop> :
  F16_SP_OPS_u7_aligned<i, (outs), (ins),
    !strconcat(asmop, "\t%blink")> {

  let fieldB = 0x000;
  let fieldU = 0b10001;
}

class F16_SP_LD<bits<3> i, string asmop> : F16_SP_OPS_u7_aligned<i,
                         (outs GPR32Reduced:$b3), (ins immU<7>:$u7),
                         !strconcat(asmop, "\t$b3, [%sp, $u7]")>;

class F16_SP_ST<bits<3> i, string asmop> : F16_SP_OPS_u7_aligned<i,
                         (outs), (ins GPR32Reduced:$b3, immU<7>:$u7),
                         !strconcat(asmop, "\t$b3, [%sp, $u7]")>;

// Compact MOV/ADD/CMP Immediate Format.
class F16_OP_IMM<bits<5> opc, dag outs, dag ins, string asmstr> :
  InstARC<2, outs, ins, asmstr, []> {

  bits<3> b;
  let Inst{15-11} = opc;
  let Inst{10-8} = b;
}

class F16_OP_U7<bit i, string asmstr> :
  F16_OP_IMM<0b11100, (outs GPR32:$b), (ins immU<7>:$u7), asmstr> {

  bits<7> u7;
  let Inst{7} = i;
  let Inst{6-0} = u7;
}

// Special types for different instruction operands.
def ccond : Operand<i32> {
  let MIOperandInfo = (ops i32imm);
  let PrintMethod = "printPredicateOperand";
}

def brccond : Operand<i32> {
  let MIOperandInfo = (ops i32imm);
  let PrintMethod = "printBRCCPredicateOperand";
}

// Branch/call targets of different offset sizes.
class BCTarget<ValueType vt> : Operand<vt> {
  let OperandType = "OPERAND_PCREL";
}

def btarget : BCTarget<OtherVT>;

class BCTargetSigned<ValueType vt, int BSz> : BCTarget<vt> {
  let DecoderMethod = "DecodeBranchTargetS<"#BSz#">";
}

class BranchTargetS<int BSz> : BCTargetSigned<OtherVT, BSz>;
def btargetS7 : BranchTargetS<7>;
def btargetS8 : BranchTargetS<8>;
def btargetS9 : BranchTargetS<9>;
def btargetS10 : BranchTargetS<10>;
def btargetS13 : BranchTargetS<13>;
def btargetS21 : BranchTargetS<21>;
def btargetS25 : BranchTargetS<25>;

class CallTargetS<int BSz> : BCTargetSigned<i32, BSz>;
def calltargetS25: CallTargetS<25>;

// Compact Branch on Compare Register with Zero.
class F16_BCC_REG<bit i, string asmstr> :
  InstARC<2, (outs), (ins GPR32:$b, btargetS8:$s8),
  !strconcat(asmstr, "\t$b, 0, $s8"), []> {

  bits<3> b;
  bits<8> s8;

  let Inst{15-11} = 0b11101;
  let Inst{10-8} = b;
  let Inst{7} = i;
  let Inst{6-0} = s8{7-1};
  let s8{0} = 0b0;
}

// Compact Branch Conditionally Format.
class F16_BCC<bits<2> i, dag ins, string asmstr> :
  InstARC<2, (outs), ins, asmstr, []> {

  let Inst{15-11} = 0b11110;
  let Inst{10-9} = i;
}

class F16_BCC_s10<bits<2> i, string asmstr> :
  F16_BCC<i, (ins btargetS10:$s),
  !strconcat(asmstr, "\t$s")> {

  bits<10> s;
  let Inst{8-0} = s{9-1};
  let s{0} = 0b0;
}

class F16_BCC_s7<bits<3> i, string asmstr> :
  F16_BCC<0b11, (ins btargetS7:$s),
  !strconcat(asmstr, "\t$s")> {

  bits<7> s;
  let Inst{8-6} = i;
  let Inst{5-0} = s{6-1};
  let s{0} = 0b0;
}
