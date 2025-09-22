//===-- CSKYInstrFormats.td - CSKY Instruction Formats -----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

class AddrMode<bits<5> val> {
  bits<5> Value = val;
}

def AddrModeNone : AddrMode<0>;
def AddrMode32B : AddrMode<1>;   // ld32.b, ld32.bs, st32.b, st32.bs, +4kb
def AddrMode32H : AddrMode<2>;   // ld32.h, ld32.hs, st32.h, st32.hs, +8kb
def AddrMode32WD : AddrMode<3>;  // ld32.w, st32.w, ld32.d, st32.d, +16kb
def AddrMode16B : AddrMode<4>;   // ld16.b, +32b
def AddrMode16H : AddrMode<5>;   // ld16.h, +64b
def AddrMode16W : AddrMode<6>;   // ld16.w, +128b or +1kb
def AddrMode32SDF : AddrMode<7>; // flds, fldd, +1kb

class CSKYInst<AddrMode am, int sz, dag outs, dag ins, string asmstr,
               list<dag> pattern> : Instruction {
  let Namespace = "CSKY";
  int Size = sz;
  AddrMode AM = am;
  field bits<32> SoftFail = 0;
  let OutOperandList = outs;
  let InOperandList = ins;
  let AsmString = asmstr;
  let Pattern = pattern;
  let Itinerary = NoItinerary;
  let TSFlags{4 - 0} = AM.Value;
}

class CSKYPseudo<dag outs, dag ins, string asmstr, list<dag> pattern>
    : CSKYInst<AddrModeNone, 0, outs, ins, asmstr, pattern> {
  let isCodeGenOnly = 1;
  let isPseudo = 1;
}

class CSKY32Inst<AddrMode am, bits<6> opcode, dag outs, dag ins, string asmstr,
                 list<dag> pattern>
    : CSKYInst<am, 4, outs, ins, asmstr, pattern> {
  field bits<32> Inst;
  let Inst{31 - 26} = opcode;
}

class CSKY16Inst<AddrMode am, dag outs, dag ins, string asmstr, list<dag> pattern>
  : CSKYInst<am, 2, outs, ins, asmstr, pattern> {
  field bits<16> Inst;
}

// CSKY 32-bit instruction
// Format< OP[6] | Offset[26] >
// Instruction(1): bsr32
class J<bits<6> opcode, dag outs, dag ins, string op, list<dag> pattern>
    : CSKY32Inst<AddrModeNone, opcode, outs, ins, !strconcat(op, "\t$offset"),
                 pattern> {
  bits<26> offset;
  let Inst{25 - 0} = offset;
  let isCall = 1;
  let Defs = [ R15 ];
}

// Format< OP[6] | RZ[5] | SOP[3] | OFFSET[18] >
// Instructions(7): grs, lrs32.b, lrs32.h, lrs32.w, srs32.b, srs32.h, srs32.w
class I_18_Z_L<bits<3> sop, string asm, dag outs, dag ins, list<dag> pattern>
    : CSKY32Inst<AddrModeNone, 0x33, outs, ins, asm, pattern> {
  bits<5> rz;
  bits<18> offset;
  let Inst{25 - 21} = rz;
  let Inst{20 - 18} = sop;
  let Inst{17 - 0} = offset;
}

// Format< OP[6] | RZ[5] | RX[5] | IMM[16] >
// Instructions(1): ori32
class I_16_ZX<string op, ImmLeaf ImmType, list<dag> pattern>
    : CSKY32Inst<AddrModeNone, 0x3b,
                 (outs GPR:$rz), (ins GPR:$rx,ImmType:$imm16),
                 !strconcat(op, "\t$rz, $rx, $imm16"), pattern> {
  bits<5> rz;
  bits<5> rx;
  bits<16> imm16;
  let Inst{25 - 21} = rz;
  let Inst{20 - 16} = rx;
  let Inst{15 - 0} = imm16;
}

// Format< OP[6] | SOP[5] | RZ[5] | IMM[16] >
// Instructions(3): movi32, movih32, (bgeni32)
class I_16_MOV<bits<5> sop, string op, ImmLeaf ImmType>
    : CSKY32Inst<AddrModeNone, 0x3a, (outs GPR:$rz), (ins ImmType:$imm16),
                 !strconcat(op, "\t$rz, $imm16"),
                 [(set GPR:$rz, ImmType:$imm16)]> {
  bits<5> rz;
  bits<16> imm16;
  let Inst{25 - 21} = sop;
  let Inst{20 - 16} = rz;
  let Inst{15 - 0} = imm16;
  let isReMaterializable = 1;
  let isAsCheapAsAMove = 1;
  let isMoveImm = 1;
}

// Format< OP[6] | SOP[5] | RZ[5] | OFFSET[16] >
// Instructions(1): lrw32
class I_16_Z_L<bits<5> sop, string op, dag ins, list<dag> pattern>
  : CSKY32Inst<AddrModeNone, 0x3a, (outs GPR:$rz), ins,
  !strconcat(op, "\t$rz, $imm16"), pattern> {
  bits<5> rz;
  bits<16> imm16;
  let Inst{25 - 21} = sop;
  let Inst{20 - 16} = rz;
  let Inst{15 - 0} = imm16;
}

// Format< OP[6] | SOP[5] | 00000[5] | OFFSET[16] >
// Instructions(5): bt32, bf32, br32, jmpi32, jsri32
class I_16_L<bits<5> sop, dag outs, dag ins, string asm, list<dag> pattern>
    : CSKY32Inst<AddrModeNone, 0x3a, outs, ins, asm, pattern> {
  bits<16> imm16;
  let Inst{25 - 21} = sop;
  let Inst{20 - 16} = 0;
  let Inst{15 - 0} = imm16;
}

// Format< OP[6] | SOP[5] | RX[5] | 0000000000000000[16] >
// Instructions(2): jmp32, jsr32
class I_16_JX<bits<5> sop, string op, list<dag> pattern>
    : CSKY32Inst<AddrModeNone, 0x3a, (outs), (ins GPR:$rx),
                 !strconcat(op, "\t$rx"), pattern> {
  bits<5> rx;
  bits<16> imm16;
  let Inst{25 - 21} = sop;
  let Inst{20 - 16} = rx;
  let Inst{15 - 0} = 0;
}

// Format< OP[6] | SOP[5] | RX[5] | 00000000000000[14] | IMM[2] >
// Instructions(1): jmpix32
class I_16_J_XI<bits<5> sop, string op, Operand operand, list<dag> pattern>
    : CSKY32Inst<AddrModeNone, 0x3a, (outs),
                 (ins GPR:$rx, operand:$imm2),
                 !strconcat(op, "\t$rx, $imm2"), pattern> {
  bits<5> rx;
  bits<2> imm2;
  let Inst{25 - 21} = sop;
  let Inst{20 - 16} = rx;
  let Inst{15 - 2} = 0;
  let Inst{1 - 0} = imm2;
}

// Format< OP[6] | SOP[5] | PCODE[5] | 0000000000000000[16] >
// Instructions(1): rts32
class I_16_RET<bits<5> sop, bits<5> pcode, string op, list<dag> pattern>
    : CSKY32Inst<AddrModeNone, 0x3a, (outs), (ins), op, pattern> {
  let Inst{25 - 21} = sop;
  let Inst{20 - 16} = pcode;
  let Inst{15 - 0} = 0;
  let isTerminator = 1;
  let isReturn = 1;
  let isBarrier = 1;
  let Uses = [ R15 ];
}

// Format< OP[6] | SOP[5] | RX[5] | IMM16[16] >
// Instructions(3): cmpnei32, cmphsi32, cmplti32
class I_16_X<bits<5> sop, string op, Operand operand>
    : CSKY32Inst<AddrModeNone, 0x3a, (outs CARRY:$ca),
    (ins GPR:$rx, operand:$imm16), !strconcat(op, "\t$rx, $imm16"), []> {
  bits<16> imm16;
  bits<5> rx;
  let Inst{25 - 21} = sop;
  let Inst{20 - 16} = rx;
  let Inst{15 - 0} = imm16;
  let isCompare = 1;
}

// Format< OP[6] | SOP[5] | RX[5] | OFFSET[16] >
// Instructions(7): bez32, bnez32, bnezad32, bhz32, blsz32, blz32, bhsz32
class I_16_X_L<bits<5> sop, string op, Operand operand>
    : CSKY32Inst<AddrModeNone, 0x3a, (outs), (ins GPR:$rx, operand:$imm16),
                 !strconcat(op, "\t$rx, $imm16"), []> {
  bits<5> rx;
  bits<16> imm16;
  let Inst{25 - 21} = sop;
  let Inst{20 - 16} = rx;
  let Inst{15 - 0} = imm16;
  let isBranch = 1;
  let isTerminator = 1;
}

// Format< OP[6] | RZ[5] | RX[5] | SOP[4] | IMM[12] >
// Instructions(5): addi32, subi32, andi32, andni32, xori32
class I_12<bits<4> sop, string op, SDNode node, ImmLeaf ImmType>
    : CSKY32Inst<AddrModeNone, 0x39, (outs GPR:$rz),
    (ins GPR:$rx, ImmType:$imm12), !strconcat(op, "\t$rz, $rx, $imm12"),
    [(set GPR:$rz, (node GPR:$rx, ImmType:$imm12))]> {
  bits<5> rz;
  bits<5> rx;
  bits<12> imm12;
  let Inst{25 - 21} = rz;
  let Inst{20 - 16} = rx;
  let Inst{15 - 12} = sop;
  let Inst{11 - 0} = imm12;
}

class I_LDST<AddrMode am, bits<6> opcode, bits<4> sop, dag outs, dag ins,
             string op, list<dag> pattern>
    : CSKY32Inst<am, opcode, outs, ins, !strconcat(op, "\t$rz, ($rx, ${imm12})"),
                 pattern> {
  bits<5> rx;
  bits<5> rz;
  bits<12> imm12;
  let Inst{25 - 21} = rz;
  let Inst{20 - 16} = rx;
  let Inst{15 - 12} = sop;
  let Inst{11 - 0} = imm12;
}

class I_PLDR<AddrMode am, bits<6> opcode, bits<4> sop, dag outs, dag ins,
             string op, list<dag> pattern>
    : CSKY32Inst<am, opcode, outs, ins, !strconcat(op, "\t($rx, ${imm12})"),
                 pattern> {
  bits<5> rx;
  bits<12> imm12;
  let Inst{25 - 21} = 0;
  let Inst{20 - 16} = rx;
  let Inst{15 - 12} = sop;
  let Inst{11 - 0} = imm12;
}


// Format< OP[6] | RZ[5] | RX[5] | SOP[4] | OFFSET[12] >
// Instructions(6): ld32.b, ld32.bs, ld32.h, ld32.hs, ld32.w
class I_LD<AddrMode am, bits<4> sop, string op, Operand operand>
    : I_LDST<am, 0x36, sop,
    (outs GPR:$rz), (ins GPR:$rx, operand:$imm12), op, []>;

// Format< OP[6] | RZ[5] | RX[5] | SOP[4] | OFFSET[12] >
// Instructions(4): st32.b, st32.h, st32.w
class I_ST<AddrMode am, bits<4> sop, string op, Operand operand>
    : I_LDST<am, 0x37, sop, (outs),
    (ins GPR:$rz, GPR:$rx, operand:$imm12), op, []>;

// Format< OP[6] | SOP[5] | PCODE[5] | 0000[4] | 000 | R28 | LIST2[3] | R15 |
// LIST1[4] >
// Instructions(2): push32, pop32
class I_12_PP<bits<5> sop, bits<5> pcode, dag outs, dag ins, string op>
    : CSKY32Inst<AddrModeNone, 0x3a, outs, ins, !strconcat(op, "\t$regs"), []> {
  bits<12> regs;
  let Inst{25 - 21} = sop;
  let Inst{20 - 16} = pcode;
  let Inst{15 - 12} = 0;
  let Inst{11 - 0} = regs;
  let Uses = [R14];
  let Defs = [R14];
}

// Format< OP[6] | RZ[5] | RX[5] | SOP[6] | PCODE[5] | IMM[5]>
// Instructions(4): incf32, inct32, decf32, dect32
class I_5_ZX<bits<6> sop, bits<5> pcode, string op, ImmLeaf ImmType,
             list<dag> pattern>
    : CSKY32Inst<AddrModeNone, 0x31, (outs GPR:$rz),
    (ins CARRY:$cond, GPR:$false, GPR:$rx, ImmType:$imm5),
    !strconcat(op, "\t$rz, $rx, $imm5"), pattern> {
  bits<5> rz;
  bits<5> rx;
  bits<5> imm5;
  let Inst{25 - 21} = rz;
  let Inst{20 - 16} = rx;
  let Inst{15 - 10} = sop;
  let Inst{9 - 5} = pcode;
  let Inst{4 - 0} = imm5;
  let Constraints = "$rz = $false";
}

// Format< OP[6] | IMM[5] | RX[5] | SOP[6] | PCODE[5] | RZ[5]>
// Instructions(13): decgt32, declt32, decne32, lsli32, lslc32, lsri32
//                   lsrc32, asri32, asrc32, rotli32, xsr32, bclri32, bseti32
class I_5_XZ<bits<6> sop, bits<5> pcode, string op, dag outs, dag ins,
             list<dag> pattern>
    : CSKY32Inst<AddrModeNone, 0x31, outs, ins,
                 !strconcat(op, "\t$rz, $rx, $imm5"), pattern> {
  bits<5> imm5;
  bits<5> rx;
  bits<5> rz;
  let Inst{25 - 21} = imm5;
  let Inst{20 - 16} = rx;
  let Inst{15 - 10} = sop;
  let Inst{9 - 5} = pcode;
  let Inst{4 - 0} = rz;
}

// mtcr32, mfcr32
class I_5_XZ_CR<bits<6> sop, bits<5> pcode, string opStr, dag outs, dag ins,
             list<dag> pattern>
    : CSKY32Inst<AddrModeNone, 0x30, outs, ins, opStr, pattern> {
  bits<5> sel;
  bits<5> rx;
  bits<5> cr;
  let Inst{25 - 21} = sel;
  let Inst{20 - 16} = rx;
  let Inst{15 - 10} = sop;
  let Inst{9 - 5} = pcode;
  let Inst{4 - 0} = cr;
}

// sync
class I_5_XZ_SYNC<bits<6> sop, bits<5> pcode, string opStr, bits<1> S, bits<1> I>
    : CSKY32Inst<AddrModeNone, 0x30, (outs), (ins), opStr, []> {
  let Inst{25 - 21} = 0;
  let Inst{20 - 16} = 0;
  let Inst{15 - 10} = sop;
  let Inst{9 - 5} = pcode;
  let Inst{4 - 0} = 0;
  let Inst{25} = S;
  let Inst{21} = I;

}

// Priviledged Instructions
class I_5_XZ_PRIVI<bits<6> sop, bits<5> pcode, string opStr>
    : CSKY32Inst<AddrModeNone, 0x30, (outs), (ins), opStr, []> {
  let Inst{25 - 21} = 0;
  let Inst{20 - 16} = 0;
  let Inst{15 - 10} = sop;
  let Inst{9 - 5} = pcode;
  let Inst{4 - 0} = 0;
}

class I_CP<bits<4> sop, dag outs, dag ins, string opStr>
    : CSKY32Inst<AddrModeNone, 0x3f, outs, ins, opStr, []> {
  bits<5> cpid;
  bits<12> usdef;
  let Inst{25 - 21} = cpid;
  let Inst{20 - 16} = 0;
  let Inst{15 - 12} = sop;
  let Inst{11 - 0} = usdef;
}

class I_CPOP<dag outs, dag ins, string opStr>
    : CSKY32Inst<AddrModeNone, 0x3f, outs, ins, opStr, []> {
  bits<5> cpid;
  bits<20> usdef;
  let Inst{25 - 21} = cpid;
  let Inst{20 - 16} = usdef{19-15};
  let Inst{15} = 1;
  let Inst{14 - 0} = usdef{14-0};
}

class I_CP_Z<bits<4> sop, dag outs, dag ins, string opStr>
    : CSKY32Inst<AddrModeNone, 0x3f, outs, ins, opStr, []> {
  bits<5> cpid;
  bits<12> usdef;
  bits<5> rz;

  let Inst{25 - 21} = cpid;
  let Inst{20 - 16} = rz;
  let Inst{15 - 12} = sop;
  let Inst{11 - 0} = usdef;
}

class I_5_CACHE<bits<6> sop, bits<5> pcode, string opStr>
    : CSKY32Inst<AddrModeNone, 0x30, (outs), (ins), opStr, []> {
  let Inst{25 - 21} = pcode;
  let Inst{20 - 16} = 0;
  let Inst{15 - 10} = sop;
  let Inst{9 - 5} = 0b00001;
  let Inst{4 - 0} = 0;
}

class I_5_X_CACHE<bits<6> sop, bits<5> pcode, string opStr>
    : CSKY32Inst<AddrModeNone, 0x30, (outs), (ins GPR:$rx), opStr #"\t$rx", []> {
  bits<5> rx;

  let Inst{25 - 21} = pcode;
  let Inst{20 - 16} = rx;
  let Inst{15 - 10} = sop;
  let Inst{9 - 5} = 0b00001;
  let Inst{4 - 0} = 0;
}

// Format< OP[6] | RY[5] | RX[5] | SOP[6] | PCODE[5] | IMM[5]>
// Instructions(2): ldm32, (ldq32), stm32, (stq32)
class I_5_YX<bits<6> opcode, bits<6> sop, dag outs, dag ins, string opStr, list<dag> pattern>
    : CSKY32Inst<AddrModeNone, opcode, outs, ins, opStr, pattern> {
  bits<10> regs;
  bits<5> rx;

  let Inst{25 - 21} = regs{9 - 5}; // ry
  let Inst{20 - 16} = rx;
  let Inst{15 - 10} = sop;
  let Inst{9 - 5} = 0b00001;
  let Inst{4 - 0} = regs{4 - 0}; // imm5
}

// Format< OP[6] | LSB[5] | RX[5] | SOP[6] | MSB[5] | RZ[5]>
// Instructions(6): zext32, zextb32, zexth32, sext32, sextb32, sexth32
class I_5_XZ_U<bits<6> sop, dag outs, dag ins, string op, list<dag> pattern>
    : CSKY32Inst<AddrModeNone, 0x31, outs, ins, op #"\t$rz, $rx, $msb, $lsb",
                 pattern> {
  bits<5> rx;
  bits<5> rz;
  bits<5> msb;
  bits<5> lsb;
  let Inst{25 - 21} = lsb; // lsb
  let Inst{20 - 16} = rx;
  let Inst{15 - 10} = sop;
  let Inst{9 - 5} = msb; // msb
  let Inst{4 - 0} = rz;
}

class I_5_XZ_INS<bits<6> sop, dag outs, dag ins, string op, list<dag> pattern>
    : CSKY32Inst<AddrModeNone, 0x31, outs, ins, op #"\t$rz, $rx, $msb, $lsb",
                 pattern> {
  bits<5> rx;
  bits<5> rz;
  bits<5> msb;
  bits<5> lsb;
  let Inst{25 - 21} = rz;
  let Inst{20 - 16} = rx;
  let Inst{15 - 10} = sop;
  let Inst{9 - 5} = msb;
  let Inst{4 - 0} = lsb;
}

// Format< OP[6] | LSB[5] | RX[5] | SOP[6] | MSB[5] | RZ[5]>
// Instructions(6): zext32, zextb32, zexth32, sext32, sextb32, sexth32
class I_5_XZ_U2<bits<6> sop, bits<5> lsb, bits<5> msb, dag outs, dag ins,
  string op, list<dag> pattern>
  : CSKY32Inst<AddrModeNone, 0x31, outs, ins, !strconcat(op, "\t$rz, $rx"), pattern> {
  bits<5> rx;
  bits<5> rz;
  let Inst{25 - 21} = lsb;     // lsb
  let Inst{20 - 16} = rx;
  let Inst{15 - 10} = sop;
  let Inst{9 - 5} = msb;       // msb
  let Inst{4 - 0} = rz;
}

// Format< OP[6] | RZ[5] | RX[5] | SOP[6] | SIZE[5] | LSB[5]>
// Instructions(1): ins32
class I_5_ZX_U<bits<6> sop, string op, Operand operand, list<dag> pattern>
    : CSKY32Inst<AddrModeNone, 0x31, (outs GPR:$rz), (ins operand:$size_lsb),
                 !strconcat(op, "\t$rz, operand:$size_lsb"), pattern> {
  bits<10> size_lsb;
  bits<5> rz;
  bits<5> rx;
  let Inst{25 - 21} = rz;
  let Inst{20 - 16} = rx;
  let Inst{15 - 10} = sop;
  let Inst{9 - 5} = size_lsb{9 - 5}; // size
  let Inst{4 - 0} = size_lsb{4 - 0}; // lsb
}

// sextb, sexth
class I_5_XZ_US<bits<6> sop, bits<5> lsb, bits<5> msb, string op,
  SDNode opnode, ValueType type>
  : I_5_XZ_U2<sop, lsb, msb, (outs GPR:$rz), (ins GPR:$rx), op,
  [(set GPR:$rz, (opnode GPR:$rx, type))]>;

class I_5_XZ_UZ<bits<6> sop, bits<5> lsb, bits<5> msb, string op, int v>
  : I_5_XZ_U2<sop, lsb, msb, (outs GPR:$rz), (ins GPR:$rx), op,
  [(set GPR:$rz, (and GPR:$rx, (i32 v)))]>;

// Format< OP[6] | IMM[5] | RX[5] | SOP[6] | PCODE[5] | 00000 >
// Instructions(1): btsti32
class I_5_X<bits<6> sop, bits<5> pcode, string op, ImmLeaf ImmType,
            list<dag> pattern>
    : CSKY32Inst<AddrModeNone, 0x31,
    (outs CARRY:$ca), (ins GPR:$rx, ImmType:$imm5),
    !strconcat(op, "\t$rx, $imm5"), pattern> {
  bits<5> imm5;
  bits<5> rx;
  let Inst{25 - 21} = imm5;
  let Inst{20 - 16} = rx;
  let Inst{15 - 10} = sop;
  let Inst{9 - 5} = pcode;
  let Inst{4 - 0} = 0;
  let isCompare = 1;
}

// Format< OP[6] | IMM[5] | 00000[5] | SOP[6] | PCODE[5] | RZ[5]>
// Instructions(1): bmaski32
class I_5_Z<bits<6> sop, bits<5> pcode, string op, ImmLeaf ImmType,
            list<dag> pattern>
    : CSKY32Inst<AddrModeNone, 0x31, (outs GPR:$rz), (ins ImmType:$imm5),
                 !strconcat(op, "\t$rz, $imm5"), pattern> {
  bits<5> imm5;
  bits<5> rz;
  let Inst{25 - 21} = imm5;
  let Inst{20 - 16} = 0;
  let Inst{15 - 10} = sop;
  let Inst{9 - 5} = pcode;
  let Inst{4 - 0} = rz;
}

class I_5_IMM5<bits<6> opcode, bits<6> sop, bits<5> pcode, string op, ImmLeaf ImmType,
            list<dag> pattern>
    : CSKY32Inst<AddrModeNone, opcode, (outs), (ins ImmType:$imm5),
                 !strconcat(op, "\t$imm5"), pattern> {
  bits<5> imm5;
  let Inst{25 - 21} = imm5;
  let Inst{20 - 16} = 0;
  let Inst{15 - 10} = sop;
  let Inst{9 - 5} = pcode;
  let Inst{4 - 0} = 0;
}

// Format< OP[6] | RY[5] | RX[5] | SOP[6] | PCODE[5] | RZ[5] >
// Instructions(24): addu32, addc32, subu32, subc32, (rsub32), ixh32, ixw32,
// ixd32, and32, andn32, or32, xor32, nor32, lsl32, lsr32, asr32, rotl32
// mult32, divu32, divs32, mul.(u/s)32, mula.32.l, mula.u32, mulall.s16.s
class R_YXZ<bits<6> opcode, bits<6> sop, bits<5> pcode, dag outs, dag ins,
            string op, list<dag> pattern>
    : CSKY32Inst<AddrModeNone, opcode, outs, ins,
                 !strconcat(op, "\t$rz, $rx, $ry"), pattern> {
  bits<5> ry;
  bits<5> rx;
  bits<5> rz;
  let Inst{25 - 21} = ry;
  let Inst{20 - 16} = rx;
  let Inst{15 - 10} = sop;
  let Inst{9 - 5} = pcode;
  let Inst{4 - 0} = rz;
}

// R_YXZ instructions with simple pattern
// Output: GPR:rz
// Input: GPR:rx, GPR:ry
// Asm string: op rz, rx, ry
// Instructions: addu32, subu32, ixh32, ixw32, ixd32, and32, andn32, or32,
// xor32, nor32, lsl32, lsr32, asr32, mult32, divu32, divs32
class R_YXZ_SP_F1<bits<6> sop, bits<5> pcode, PatFrag opnode, string op,
  bit Commutable = 0> : R_YXZ<0x31, sop, pcode, (outs GPR:$rz),
  (ins GPR:$rx, GPR:$ry), op, [(set GPR:$rz, (opnode GPR:$rx, GPR:$ry))]> {
  let isCommutable = Commutable;
}

// Format< OP[6] | RY[5] | RX[5] | SOP[6] | PCODE[5] | RZ[5] >
// Instructions:(8) ldr32.b, ldr32.h, ldr32.bs, ldr32.hs, ldr32.w,
//                  str32.b, str32.h, str32.w
class R_YXZ_LDST<bits<6> opcode, bits<6> sop, dag outs,
                 dag ins, string op, list<dag> pattern>
    : CSKY32Inst<AddrModeNone, opcode, outs, ins,
                 op # "\t$rz, ($rx, $ry << ${imm})", pattern> {
  bits<5> rx;
  bits<5> ry;
  bits<5> rz;
  bits<5> imm;
  let Inst{25 - 21} = ry; // ry;
  let Inst{20 - 16} = rx; // rx;
  let Inst{15 - 10} = sop;
  let Inst{9 - 5} = imm;  // pcode;
  let Inst{4 - 0} = rz;
}

class I_LDR<bits<6> sop, string op> : R_YXZ_LDST<0x34, sop,
  (outs GPR:$rz), (ins GPR:$rx, GPR:$ry, uimm_shift:$imm), op, []>;

class I_STR<bits<6> sop, string op> : R_YXZ_LDST<0x35, sop,
  (outs), (ins GPR:$rz, GPR:$rx, GPR:$ry, uimm_shift:$imm), op, []>;

// Format< OP[6] | RX[5] | RX[5] | SOP[6] | PCODE[5] | RZ[5] >
// Instructions:(1) not32
class R_XXZ<bits<6> sop, bits<5> pcode, dag outs, dag ins, string op,
            list<dag> pattern>
    : CSKY32Inst<AddrModeNone, 0x31, outs, ins, !strconcat(op, "\t$rz, $rx"),
                 pattern> {
  bits<5> rx;
  bits<5> rz;
  let Inst{25 - 21} = rx;
  let Inst{20 - 16} = rx;
  let Inst{15 - 10} = sop;
  let Inst{9 - 5} = pcode;
  let Inst{4 - 0} = rz;
}

// Format< OP[6] | RY[5] | RX[5] | SOP[6] | PCODE[5] | 00000[5] >
// Instructions:(4) cmpne32, cmphs32, cmplt32, tst32
class R_YX<bits<6> sop, bits<5> pcode, string op>
    : CSKY32Inst<AddrModeNone, 0x31, (outs CARRY:$ca),
                 (ins GPR:$rx, GPR:$ry),
                 !strconcat(op, "\t$rx, $ry"), []> {
  bits<5> ry;
  bits<5> rx;
  let Inst{25 - 21} = ry;
  let Inst{20 - 16} = rx;
  let Inst{15 - 10} = sop;
  let Inst{9 - 5} = pcode;
  let Inst{4 - 0} = 0;
  let isCompare = 1;
}

// Format< OP[6] | 00000[5] | RX[5] | SOP[6] | PCODE[5] | RZ[5] >
// Instructions:(12)
//   mov32, xtrb0.32, xtrb1.32, xtrb2.32, xtrb3.32, brev32, revb32
//   revh32, abs32, ff0.32, ff1.32, bgenr32
class R_XZ<bits<6> sop, bits<5> pcode, string op>
    : CSKY32Inst<AddrModeNone, 0x31, (outs GPR:$rz), (ins GPR:$rx),
                 !strconcat(op, "\t$rz, $rx"), []> {
  bits<5> rx;
  bits<5> rz;
  let Inst{25 - 21} = 0;
  let Inst{20 - 16} = rx;
  let Inst{15 - 10} = sop;
  let Inst{9 - 5} = pcode;
  let Inst{4 - 0} = rz;
}

// Format< OP[6] | RZ[5] | RX[5] | SOP[6] | PCODE[5] | 00000[5] >
// Instructions:(2) movf32, movt32
class R_ZX<bits<6> sop, bits<5> pcode, string op, list<dag> pattern>
    : CSKY32Inst<AddrModeNone, 0x31, (outs GPR:$rz),
                 (ins CARRY:$ca, GPR:$rx, GPR:$false),
                 !strconcat(op, "\t$rz, $rx"), pattern> {
  bits<5> rz;
  bits<5> rx;
  let Inst{25 - 21} = rz;
  let Inst{20 - 16} = rx;
  let Inst{15 - 10} = sop;
  let Inst{9 - 5} = pcode;
  let Inst{4 - 0} = 0;
  let Constraints = "$rz = $false";
  let isSelect = 1;
}

// Format< OP[6] | 00000[5] | RX[5] | SOP[6] | PCODE[5] | 00000[5] >
// Instructions:(1) tstnbz32
class R_X<bits<6> sop, bits<5> pcode, dag outs, dag ins, string op, list<dag> pattern>
    : CSKY32Inst<AddrModeNone, 0x31, outs, ins, !strconcat(op, "\t$rx"), pattern> {
  bits<5> rx;
  let Inst{25 - 21} = 0;
  let Inst{20 - 16} = rx;
  let Inst{15 - 10} = sop;
  let Inst{9 - 5} = pcode;
  let Inst{4 - 0} = 0;
}

// Format< OP[6] | 00000[5] | 00000[5] | SOP[6] | PCODE[5] | RZ[5] >
// Instructions:(2) mvc32, mvcv32
class R_Z_1<bits<6> sop, bits<5> pcode, string op>
    : CSKY32Inst<AddrModeNone, 0x31, (outs GPR:$rz),
                 (ins CARRY:$ca), !strconcat(op, "\t$rz"), []> {
  bits<5> rz;
  let Inst{25 - 21} = 0;
  let Inst{20 - 16} = 0;
  let Inst{15 - 10} = sop;
  let Inst{9 - 5} = pcode;
  let Inst{4 - 0} = rz;
}

// Format< OP[6] | RZ[5] | 00000[5] | SOP[6] | PCODE[5] | 00000[5] >
// Instructions:(2) clrf32, clrt32
class R_Z_2<bits<6> sop, bits<5> pcode, string op>
    : CSKY32Inst<AddrModeNone, 0x31, (outs GPR:$rz),
    (ins CARRY:$ca, GPR:$false), !strconcat(op, "\t$rz"), []> {
  bits<5> rz;
  let Inst{25 - 21} = rz;
  let Inst{20 - 16} = 0;
  let Inst{15 - 10} = sop;
  let Inst{9 - 5} = pcode;
  let Inst{4 - 0} = 0;
  let Constraints = "$rz = $false";
}

class BAR<bits<5> sop, string op, bits<1> signed>
  : CSKY32Inst<AddrModeNone, 0x30, (outs), (ins), op, []> {
  let Inst{25} = signed;
  let Inst{24 - 16} = 0;
  let Inst{15 - 5} = 0x421;
  let Inst{4 - 0} = sop;
  let hasSideEffects = 1;
  let mayLoad = 0;
  let mayStore = 0;
}
