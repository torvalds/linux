//===- CSKYInstrFormatsF2.td - CSKY Float2.0 Instr Format --*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// CSKY Instruction Format Float2.0 Definitions.
//
//===----------------------------------------------------------------------===//

class CSKYInstF2<AddrMode am, dag outs, dag ins, string opcodestr,
                 list<dag> pattern>
    : CSKY32Inst<am, 0x3d, outs, ins, opcodestr, pattern> {
  let Predicates = [HasFPUv3_SF];
  let DecoderNamespace = "FPUV3";
}

class F2_XYZ<bits<5> datatype, bits<6> sop, string opcodestr, dag outs, dag ins,
             list<dag> pattern>
    : CSKYInstF2<AddrModeNone, outs, ins, opcodestr, pattern> {
  bits<5> vry;
  bits<5> vrx;
  bits<5> vrz;

  let Inst{25-21} = vry;
  let Inst{20-16} = vrx;
  let Inst{15-11} = datatype;
  let Inst{10-5} = sop;
  let Inst{4-0} = vrz;
}

multiclass F2_XYZ_T<bits<6> sop, string op, PatFrag opnode> {
  def _S : F2_XYZ<0b00000, sop, op#".32"#"\t$vrz, $vrx, $vry",
             (outs FPR32Op:$vrz), (ins FPR32Op:$vrx, FPR32Op:$vry),
             [(set FPR32Op:$vrz, (opnode FPR32Op:$vrx, FPR32Op:$vry))]>;
  let Predicates = [HasFPUv3_DF] in
  def _D : F2_XYZ<0b00001, sop, op#".64"#"\t$vrz, $vrx, $vry",
             (outs FPR64Op:$vrz), (ins FPR64Op:$vrx, FPR64Op:$vry),
             [(set FPR64Op:$vrz, (opnode FPR64Op:$vrx, FPR64Op:$vry))]>;
}

let Constraints = "$vrZ = $vrz" in
multiclass F2_XYZZ_T<bits<6> sop, string op, PatFrag opnode> {
  def _S : F2_XYZ<0b00000, sop, op#".32"#"\t$vrz, $vrx, $vry",
                  (outs FPR32Op:$vrz), (ins FPR32Op:$vrZ, FPR32Op:$vrx, FPR32Op:$vry),
                  [(set FPR32Op:$vrz, (opnode FPR32Op:$vrx, FPR32Op:$vry, FPR32Op:$vrZ))]>;
  let Predicates = [HasFPUv3_DF] in
  def _D : F2_XYZ<0b00001, sop, op#".64"#"\t$vrz, $vrx, $vry",
                  (outs FPR64Op:$vrz), (ins FPR64Op:$vrZ, FPR64Op:$vrx, FPR64Op:$vry),
                  [(set FPR64Op:$vrz, (opnode FPR64Op:$vrx, FPR64Op:$vry, FPR64Op:$vrZ))]>;
}

let vry = 0 in {
class F2_XZ<bits<5> datatype, RegisterOperand regtype, bits<6> sop, string op, SDNode opnode>
    : F2_XYZ<datatype, sop, !strconcat(op, "\t$vrz, $vrx"),
             (outs regtype:$vrz), (ins regtype:$vrx),
             [(set regtype:$vrz, (opnode regtype:$vrx))]>;

class F2_XZ_SET<bits<5> datatype, RegisterOperand regtype, bits<6> sop, string op>
    : F2_XYZ<datatype, sop, !strconcat(op, "\t$vrz, $vrx"),
             (outs regtype:$vrz), (ins regtype:$vrx),
             []>;

class F2_XZ_P<bits<5> datatype, bits<6> sop, string op, list<dag> pattern = [],
              dag outs, dag ins>
    : F2_XYZ<datatype, sop, op#"\t$vrz, $vrx", outs, ins, pattern>;
}

multiclass F2_XZ_RM<bits<5> datatype, bits<4> sop, string op, dag outs, dag ins> {
  def _RN  : F2_XZ_P<datatype, {sop, 0b00}, op#".rn", [], outs, ins>;
  def _RZ  : F2_XZ_P<datatype, {sop, 0b01}, op#".rz", [], outs, ins>;
  def _RPI : F2_XZ_P<datatype, {sop, 0b10}, op#".rpi", [], outs, ins>;
  def _RNI : F2_XZ_P<datatype, {sop, 0b11}, op#".rni", [], outs, ins>;
}

multiclass F2_XZ_T<bits<6> sop, string op, SDNode opnode> {
  def _S : F2_XZ<0b00000, FPR32Op, sop, op#".32", opnode>;
  let Predicates = [HasFPUv3_DF] in
  def _D : F2_XZ<0b00001, FPR64Op, sop, op#".64", opnode>;
}

multiclass F2_XZ_SET_T<bits<6> sop, string op, string suffix = ""> {
  def _S : F2_XZ_SET<0b00000, FPR32Op, sop, op#".32"#suffix>;
  let Predicates = [HasFPUv3_DF] in
  def _D : F2_XZ_SET<0b00001, FPR64Op, sop, op#".64"#suffix>;
}


let vrz = 0, isCompare = 1 in
class F2_CXY<bits<5> datatype, RegisterOperand regtype, bits<6> sop, string op>
    : F2_XYZ<datatype, sop, !strconcat(op, "\t$vrx, $vry"),
             (outs CARRY:$ca), (ins regtype:$vrx, regtype:$vry),
             []>;

multiclass F2_CXY_T<bits<6> sop, string op> {
  def _S : F2_CXY<0b00000, FPR32Op, sop, op#".32">;
  let Predicates = [HasFPUv3_DF] in
  def _D : F2_CXY<0b00001, FPR64Op, sop, op#".64">;
}


let vrz = 0, vry = 0, isCompare = 1 in
class F2_CX<bits<5> datatype, RegisterOperand regtype, bits<6> sop, string op>
    : F2_XYZ<datatype, sop, !strconcat(op, "\t$vrx"),
             (outs CARRY:$ca), (ins regtype:$vrx),
             []>;

multiclass F2_CX_T<bits<6> sop, string op> {
  def _S : F2_CX<0b00000, FPR32Op, sop, op#".32">;
  let Predicates = [HasFPUv3_DF] in
  def _D : F2_CX<0b00001, FPR64Op, sop, op#".64">;
}


class F2_LDST<bits<2> datatype, bits<1> sop, string op, dag outs, dag ins>
    : CSKYInstF2<AddrMode32SDF, outs, ins,
                 !strconcat(op, "\t$vrz, ($rx, ${imm8})"), []> {
  bits<10> imm8;
  bits<5> rx;
  bits<5> vrz;

  let Inst{25} = vrz{4};
  let Inst{24-21} = imm8{7-4};
  let Inst{20-16} = rx;
  let Inst{15-11} = 0b00100;
  let Inst{10} = sop;
  let Inst{9-8} = datatype;
  let Inst{7-4} = imm8{3-0};
  let Inst{3-0} = vrz{3-0};
}

class F2_LDST_S<bits<1> sop, string op, dag outs, dag ins>
    : F2_LDST<0b00, sop, op#".32", outs, ins>;
class F2_LDST_D<bits<1> sop, string op, dag outs, dag ins>
    : F2_LDST<0b01, sop, op#".64", outs, ins>;

class F2_LDSTM<bits<2> datatype, bits<1> sop, bits<3> sop2, string op, dag outs, dag ins>
    : CSKYInstF2<AddrMode32SDF, outs, ins,
                 !strconcat(op, "\t$regs, (${rx})"), []> {
  bits<10> regs;
  bits<5> rx;

  let Inst{25-21} = regs{4-0};
  let Inst{20-16} = rx;
  let Inst{15-11} = 0b00110;
  let Inst{10} = sop;
  let Inst{9-8} = datatype;
  let Inst{7-5} = sop2;
  let Inst{4-0} = regs{9-5};
}

class F2_LDSTM_S<bits<1> sop, bits<3> sop2, string op, dag outs, dag ins>
    : F2_LDSTM<0b00, sop, sop2, op#".32", outs, ins>;
class F2_LDSTM_D<bits<1> sop, bits<3> sop2, string op, dag outs, dag ins>
    : F2_LDSTM<0b01, sop, sop2, op#".64", outs, ins>;


class F2_LDSTR<bits<2> datatype, bits<1> sop, string op, dag outs, dag ins>
    : CSKYInstF2<AddrModeNone, outs, ins,
                 op#"\t$rz, ($rx, $ry << ${imm})", []> {
  bits<5> rx;
  bits<5> ry;
  bits<5> rz;
  bits<2> imm;

  let Inst{25-21} = ry;
  let Inst{20-16} = rx;
  let Inst{15-11} = 0b00101;
  let Inst{10} = sop;
  let Inst{9-8} = datatype;
  let Inst{7} = 0;
  let Inst{6-5} = imm;
  let Inst{4-0} = rz;
}

class F2_LDSTR_S<bits<1> sop, string op, dag outs, dag ins>
    : F2_LDSTR<0b00, sop, op#".32", outs, ins>;
class F2_LDSTR_D<bits<1> sop, string op, dag outs, dag ins>
    : F2_LDSTR<0b01, sop, op#".64", outs, ins>;

class F2_CXYZ<bits<5> datatype, RegisterOperand regtype, bits<6> sop, string op>
    : F2_XYZ<datatype, sop, !strconcat(op, "\t$vrz, $vrx, $vry"),
             (outs regtype:$vrz), (ins CARRY:$ca, regtype:$vrx, regtype:$vry),
             []>;
multiclass F2_CXYZ_T<bits<6> sop, string op> {
  def _S : F2_CXYZ<0b00000, FPR32Op, sop, op#".32">;
  let Predicates = [HasFPUv3_DF] in
  def _D : F2_CXYZ<0b00001, FPR64Op, sop, op#".64">;
}

class F2_LRW<bits<2> datatype, bits<1> sop, string op, dag outs, dag ins>
    : CSKYInstF2<AddrModeNone, outs, ins,
                 !strconcat(op, "\t$vrz, ${imm8}"), []> {
  bits<10> imm8;
  bits<5> rx;
  bits<5> vrz;

  let Inst{25} = vrz{4};
  let Inst{24-21} = imm8{7-4};
  let Inst{20-16} = 0;
  let Inst{15-11} = 0b00111;
  let Inst{10} = sop;
  let Inst{9-8} = datatype;
  let Inst{7-4} = imm8{3-0};
  let Inst{3-0} = vrz{3-0};
}
