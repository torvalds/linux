//===- CSKYInstrFormats16Instr.td - 16-bit Instr. Formats -*- tablegen --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

class J16<bits<5> sop, string opstr, dag ins>
  : CSKY16Inst<AddrModeNone, (outs), ins,
    !strconcat(opstr, "\t$offset"), []> {
  bits<10> offset;
  let Inst{15} = 0;
  let Inst{14 - 10} = sop;
  let Inst{9 - 0} = offset;
}

class J16_B<bits<5> sop, string opstr>
  : CSKY16Inst<AddrModeNone, (outs), (ins CARRY:$ca, br_symbol_16bit:$offset),
    !strconcat(opstr, "\t$offset"), []> {
  bits<10> offset;
  let Inst{15} = 0;
  let Inst{14 - 10} = sop;
  let Inst{9 - 0} = offset;
}

class R16_XYZ<bits<2> sop, string opstr, SDNode opnode> : CSKY16Inst<AddrModeNone,
  (outs mGPR:$rz), (ins mGPR:$rx, mGPR:$ry), !strconcat(opstr, "\t$rz, $rx, $ry"),
  [(set mGPR:$rz, (opnode mGPR:$rx, mGPR:$ry)) ]> {
  bits<3> rz;
  bits<3> rx;
  bits<3> ry;
  let Inst{15 - 11} = 0b01011;
  let Inst{10 - 8} = rx;
  let Inst{7 - 5} = rz;
  let Inst{4 - 2} = ry;
  let Inst{1, 0} = sop;
}

class R16_XZ_BINOP<bits<4> op, bits<2> sop, string opstr, PatFrag opnode> : CSKY16Inst<
  AddrModeNone, (outs sGPR:$rz), (ins sGPR:$rZ, sGPR:$rx), !strconcat(opstr, "\t$rz, $rx"),
  [(set sGPR:$rz, (opnode sGPR:$rZ, sGPR:$rx))]> {
  bits<4> rz;
  bits<4> rx;
  let Inst{15, 14} = 0b01;
  let Inst{13 - 10} = op;
  let Inst{9 - 6} = rz;
  let Inst{5 - 2} = rx;
  let Inst{1, 0} = sop;
  let Constraints = "$rz = $rZ";
}

class R16_XZ_BINOP_NOPat<bits<4> op, bits<2> sop, string opstr> : CSKY16Inst<
  AddrModeNone, (outs sGPR:$rz), (ins sGPR:$rZ, sGPR:$rx), !strconcat(opstr, "\t$rz, $rx"),
  []> {
  bits<4> rz;
  bits<4> rx;
  let Inst{15, 14} = 0b01;
  let Inst{13 - 10} = op;
  let Inst{9 - 6} = rz;
  let Inst{5 - 2} = rx;
  let Inst{1, 0} = sop;
  let Constraints = "$rz = $rZ";
}

class R16_XZ_BINOP_C<bits<4> op, bits<2> sop, string opstr> : CSKY16Inst<
  AddrModeNone, (outs sGPR:$rz, CARRY:$cout),
  (ins sGPR:$rZ, sGPR:$rx, CARRY:$cin), !strconcat(opstr, "\t$rz, $rx"), []> {
  bits<4> rz;
  bits<4> rx;
  let Inst{15, 14} = 0b01;
  let Inst{13 - 10} = op;
  let Inst{9 - 6} = rz;
  let Inst{5 - 2} = rx;
  let Inst{1, 0} = sop;
  let Constraints = "$rz = $rZ";
}

class R16_XZ_UNOP<bits<4> op, bits<2> sop, string opstr> : CSKY16Inst<
  AddrModeNone, (outs sGPR:$rz), (ins sGPR:$rx), !strconcat(opstr, "\t$rz, $rx"),
  []> {
  bits<4> rz;
  bits<4> rx;
  let Inst{15, 14} = 0b01;
  let Inst{13 - 10} = op;
  let Inst{9 - 6} = rz;
  let Inst{5 - 2} = rx;
  let Inst{1, 0} = sop;
}

class R16_Z_UNOP<bits<4> op, bits<2> sop, string opstr> : CSKY16Inst<
  AddrModeNone, (outs sGPR:$rz), (ins sGPR:$rx), !strconcat(opstr, "\t$rz"),
  []> {
  bits<4> rz;
  bits<4> rx;
  let Inst{15, 14} = 0b01;
  let Inst{13 - 10} = op;
  let Inst{9 - 6} = rz;
  let Inst{5 - 2} = rx;
  let Inst{1, 0} = sop;
  let Constraints = "$rz = $rx";
}

class R16_XY_CMP<bits<2> sop, string opstr> : CSKY16Inst<
  AddrModeNone, (outs CARRY:$ca), (ins sGPR:$rx, sGPR:$ry), !strconcat(opstr, "\t$rx, $ry"),
  []> {
  bits<4> ry;
  bits<4> rx;
  let Inst{15, 14} = 0b01;
  let Inst{13 - 10} = 0b1001;
  let Inst{9 - 6} = ry;
  let Inst{5 - 2} = rx;
  let Inst{1, 0} = sop;
  let isCompare = 1;
}

class R16_X_J<bits<8> op_rz, bits<2> sop, string opstr> : CSKY16Inst<
  AddrModeNone, (outs), (ins sGPR:$rx), !strconcat(opstr, "\t$rx"), []> {
  bits<4> rx;
  let Inst{15, 14} = 0b01;
  let Inst{13 - 6} = op_rz;
  let Inst{5 - 2} = rx;
  let Inst{1, 0} = sop;
}

class I16_Z_8<bits<3> op, dag ins, string asmstr>
  : CSKY16Inst<AddrModeNone, (outs mGPR:$rz), ins, asmstr, []> {
  bits<3> rz;
  bits<8> imm8;
  let Inst{15, 14} = 0b00;
  let Inst{13 - 11} = op;
  let Inst{10 - 8} = rz;
  let Inst{7 - 0} = imm8;
}

class I16_Z_5<bits<3> sop, dag outs, dag ins,string opstr>
  : CSKY16Inst<AddrModeNone, outs, ins,
  !strconcat(opstr, "\t$rz, $imm5"), []> {
  bits<3> rz;
  bits<5> imm5;
  let Inst{15, 14} = 0b00;
  let Inst{13 - 11} = 0b111;
  let Inst{10 - 8} = rz;
  let Inst{7 - 5} = sop;
  let Inst{4 - 0} = imm5;
}

class I16_X_CMP<bits<3> sop, string opstr, Operand Immoperand> : CSKY16Inst<
  AddrModeNone, (outs CARRY:$ca), (ins mGPR:$rx, Immoperand:$imm5),
  !strconcat(opstr, "\t$rx, $imm5"), []> {
  bits<3> rx;
  bits<5> imm5;
  let Inst{15, 14} = 0b00;
  let Inst{13 - 11} = 0b111;
  let Inst{10 - 8} = rx;
  let Inst{7 - 5} = sop;
  let Inst{4 - 0} = imm5;
  let isCompare = 1;
}

class I16_SP_IMM7<bits<3> sop, string opstr> : CSKY16Inst<
  AddrModeNone, (outs GPRSP:$sp2), (ins GPRSP:$sp1, uimm7_2:$imm7),
  !strconcat(opstr, "\t$sp2, $sp1, $imm7"), []> {
  bits<7> imm7;
  let Inst{15, 14} = 0b00;
  let Inst{13 - 10} = 0b0101;
  let Inst{9, 8} = imm7{6,5};
  let Inst{7 - 5} = sop;
  let Inst{4 - 0} = imm7{4 - 0};
}

class I16_XZ_IMM5<bits<3> sop, string opstr, SDNode opnode> : CSKY16Inst<
  AddrModeNone, (outs mGPR:$rz), (ins mGPR:$rx, uimm5:$imm5),
  !strconcat(opstr, "\t$rz, $rx, $imm5"), [(set mGPR:$rz, (opnode mGPR:$rx, uimm5:$imm5))]> {
  bits<3> rx;
  bits<3> rz;
  bits<5> imm5;
  let Inst{15, 14} = 0b01;
  let Inst{13 - 11} = sop;
  let Inst{10 - 8} = rx;
  let Inst{7 - 5} = rz;
  let Inst{4 - 0} = imm5;
}

class I16_XZ_LDST<AddrMode am, bits<3> sop, string opstr, dag outs, dag ins>
  : CSKY16Inst<am, outs, ins, !strconcat(opstr, "\t$rz, ($rx, ${imm})"),
  []> {
  bits<3> rx;
  bits<3> rz;
  bits<5> imm;
  let Inst{15, 14} = 0b10;
  let Inst{13 - 11} = sop;
  let Inst{10 - 8} = rx;
  let Inst{7 - 5} = rz;
  let Inst{4 - 0} = imm;
}

class I16_ZSP_LDST<AddrMode am, bits<3> sop, string opstr, dag outs, dag ins> : CSKY16Inst<
  am,  outs, ins, !strconcat(opstr, "\t$rz, ($sp, ${addr})"),
  []> {
  bits<3> rz;
  bits<8> addr;
  let Inst{15, 14} = 0b10;
  let Inst{13 - 11} = sop;
  let Inst{10 - 8} = addr{7 - 5};
  let Inst{7 - 5} = rz;
  let Inst{4 - 0} = addr{4 - 0};
}

class I16_XZ_IMM3<bits<2> sop, string opstr, SDNode opnode> : CSKY16Inst<
  AddrModeNone, (outs mGPR:$rz), (ins mGPR:$rx, oimm3:$oimm3),
  !strconcat(opstr, "\t$rz, $rx, $oimm3"), [(set mGPR:$rz, (opnode mGPR:$rx, oimm3:$oimm3))]> {
  bits<3> rx;
  bits<3> rz;
  bits<3> oimm3;
  let Inst{15, 14} = 0b01;
  let Inst{13 - 11} = 0b011;
  let Inst{10 - 8} = rx;
  let Inst{7 - 5} = rz;
  let Inst{4 - 2} = oimm3;
  let Inst{1, 0} = sop;
}

class I16_BPushPop<bits<11> op, bits<2> uop, dag out, dag ins, string opstr> :
  CSKY16Inst<AddrModeNone, out, ins, opstr, []>{
  bits<3> rz;
  let Inst{15- 5} = op;
  let Inst{4 -2} = rz;
  let Inst{1,0} = uop;
  let Predicates = [HasJAVA];
  let hasSideEffects = 1;
}
