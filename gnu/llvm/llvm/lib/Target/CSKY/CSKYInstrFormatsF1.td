//===- CSKYInstrFormatsF1.td - CSKY Float1.0 Instr Format --*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// CSKY Instruction Format Float1.0 Definitions.
//
//===----------------------------------------------------------------------===//

class CSKYFP1Inst<dag outs, dag ins, string asmstr, list<dag> pattern>
  : CSKY32Inst<AddrModeNone, 0x3d, outs, ins, asmstr, pattern>, Requires<[HasFPUv2_SF]> {
}

class F_XYZ_BASE<bits<5> datatype, bits<6> sop, dag outs, dag ins, string opcodestr, list<dag> pattern>
  : CSKYFP1Inst<outs, ins, opcodestr, pattern> {
  bits<4> vrx;
  bits<4> vry;
  bits<4> vrz;
  let Inst{25 - 21} = {0, vry};
  let Inst{20 - 16} = {0, vrx};
  let Inst{15 - 11} = datatype;
  let Inst{10 - 5} = sop;
  let Inst{4 - 0} = {0, vrz};
}

class F_XZ_GF<bits<5> datatype, bits<6> sop, dag outs, dag ins, string opcodestr, list<dag> pattern>
  : CSKYFP1Inst<outs, ins, opcodestr, pattern> {
  bits<4> vrx;
  bits<5> rz;
  let Inst{25 - 21} = 0;
  let Inst{20 - 16} = {0, vrx};
  let Inst{15 - 11} = datatype;
  let Inst{10 - 5} = sop;
  let Inst{4 - 0} = {rz};
}

class F_XZ_FG<bits<5> datatype, bits<6> sop, dag outs, dag ins, string opcodestr, list<dag> pattern>
  : CSKYFP1Inst<outs, ins, opcodestr, pattern> {
  bits<5> rx;
  bits<4> vrz;
  let Inst{25 - 21} = 0;
  let Inst{20 - 16} = {rx};
  let Inst{15 - 11} = datatype;
  let Inst{10 - 5} = sop;
  let Inst{4 - 0} = {0, vrz};
}

class F_XZ_TRANS_FROM<bits<6> sop, string op, RegisterOperand regtype1, RegisterOperand regtype2>
  : F_XZ_GF<3, sop, (outs regtype1:$rz), (ins regtype2:$vrx), !strconcat(op, "\t$rz, $vrx"),
  []>;

class F_XZ_TRANS_TO<bits<6> sop, string op, RegisterOperand regtype1, RegisterOperand regtype2>
  : F_XZ_FG<3, sop, (outs regtype1:$vrz), (ins regtype2:$rx), !strconcat(op, "\t$vrz, $rx"),
  []>;

let vry = 0 in {
class F_XZ<bits<5> datatype, bits<6> sop, string op, string op_su, PatFrag opnode, RegisterOperand regtype>
  : F_XYZ_BASE<datatype, sop, (outs regtype:$vrz), (ins regtype:$vrx), !strconcat(op#op_su, "\t$vrz, $vrx"),
  [(set regtype:$vrz, (opnode regtype:$vrx))]>;

class F_MOV<bits<5> datatype, bits<6> sop, string op, string op_su, RegisterOperand regtype>
  : F_XYZ_BASE<datatype, sop, (outs regtype:$vrz), (ins regtype:$vrx), !strconcat(op#op_su, "\t$vrz, $vrx"),
  []>;

class F_XZ_TRANS<bits<6> sop, string op, RegisterOperand regtype1, RegisterOperand regtype2>
  : F_XYZ_BASE<3, sop, (outs regtype1:$vrz), (ins regtype2:$vrx), !strconcat(op, "\t$vrz, $vrx"),
  []>;

class F_XZ_TRANS_DS<bits<6> sop, string op, PatFrag opnode>
  : F_XYZ_BASE<3, sop, (outs sFPR32Op:$vrz), (ins sFPR64Op:$vrx), !strconcat(op, "\t$vrz, $vrx"),
  [(set sFPR32Op:$vrz, (opnode sFPR64Op:$vrx))]>;

class F_XZ_TRANS_SD<bits<6> sop, string op, PatFrag opnode>
  : F_XYZ_BASE<3, sop, (outs sFPR64Op:$vrz), (ins sFPR32Op:$vrx), !strconcat(op, "\t$vrz, $vrx"),
  [(set sFPR64Op:$vrz, (opnode sFPR32Op:$vrx))]>;
}

multiclass FT_MOV<bits<6> sop, string op> {
  def _S :  F_MOV<0, sop, op, "s", sFPR32Op>;
  let Predicates = [HasFPUv2_DF] in
  def _D :  F_MOV<1, sop, op, "d", sFPR64Op>;
}

multiclass FT_XZ<bits<6> sop, string op, PatFrag opnode> {
  def _S :  F_XZ<0, sop, op, "s", opnode, sFPR32Op>;
  let Predicates = [HasFPUv2_DF] in
  def _D :  F_XZ<1, sop, op, "d", opnode, sFPR64Op>;
}

let vrz = 0, isCompare = 1 in {
class F_CMPXY<bits<5> datatype, bits<6> sop, string op, string op_su, RegisterOperand regtype>
  : F_XYZ_BASE<datatype, sop, (outs CARRY:$ca), (ins regtype:$vrx, regtype:$vry), !strconcat(op#op_su, "\t$vrx, $vry"),
  []>;

let vry = 0 in{
class F_CMPZX<bits<5> datatype, bits<6> sop, string op, string op_su, RegisterOperand regtype>
  : F_XYZ_BASE<datatype, sop, (outs CARRY:$ca), (ins regtype:$vrx), !strconcat(op#op_su, "\t$vrx"),
  []>;
}
}

class F_XYZ<bits<5> datatype, bits<6> sop, string op, string op_su, PatFrag opnode, RegisterOperand regtype>
  : F_XYZ_BASE<datatype, sop, (outs regtype:$vrz), (ins regtype:$vrx, regtype:$vry),
    !strconcat(op#op_su, "\t$vrz, $vrx, $vry"),
  [(set regtype:$vrz, (opnode regtype:$vrx, regtype:$vry))]>;

multiclass FT_XYZ<bits<6> sop, string op, PatFrag opnode> {
  def _S :  F_XYZ<0, sop, op, "s", opnode, sFPR32Op>;
  let Predicates = [HasFPUv2_DF] in
  def _D :  F_XYZ<1, sop, op, "d", opnode, sFPR64Op>;
}

let Constraints = "$vrt = $vrz" in {
class F_ACCUM_XYZ<bits<5> datatype, bits<6> sop, string op, string op_su, PatFrag opnode, RegisterOperand regtype>
  : F_XYZ_BASE<datatype, sop, (outs regtype:$vrz), (ins regtype:$vrt, regtype:$vrx, regtype:$vry),
    !strconcat(op#op_su, "\t$vrz, $vrx, $vry"),
  [(set regtype:$vrz, (opnode regtype:$vrt, regtype:$vrx, regtype:$vry))]>;
}

multiclass FT_ACCUM_XYZ<bits<6> sop, string op, PatFrag opnode> {
  def _S :  F_ACCUM_XYZ<0, sop, op, "s", opnode, sFPR32Op>;
  let Predicates = [HasFPUv2_DF] in
  def _D :  F_ACCUM_XYZ<1, sop, op, "d", opnode, sFPR64Op>;
}

multiclass FT_CMPXY<bits<6> sop, string op> {
  def _S :  F_CMPXY<0, sop, op, "s", sFPR32Op>;
  let Predicates = [HasFPUv2_DF] in
  def _D :  F_CMPXY<1, sop, op, "d", sFPR64Op>;
}


multiclass FT_CMPZX<bits<6> sop, string op> {
  def _S :  F_CMPZX<0, sop, op, "s", sFPR32Op>;
  let Predicates = [HasFPUv2_DF] in
  def _D :  F_CMPZX<1, sop, op, "d", sFPR64Op>;
}

class F_I8_XY_MEM<bits<7> sop, bits<1> sop_su, dag outs, dag ins, string opcodestr, list<dag> pattern>
  : CSKY32Inst<AddrMode32SDF, 0x3d, outs, ins, opcodestr, pattern> {
  bits<5> rx;
  bits<4> vrz;
  bits<8> imm8;
  let Inst{25} = 0;
  let Inst{24 - 21} = imm8{7 - 4};  //imm4h
  let Inst{20 - 16} = rx;  //rx
  let Inst{15 - 9} = sop;
  let Inst{8} = sop_su;
  let Inst{7 - 4} = imm8{3 - 0}; // imm4l
  let Inst{3 - 0} = vrz;
}

class F_I4_XY_MEM<bits<7> sop, bits<1> sop_su, dag outs, dag ins, string opcodestr, list<dag> pattern>
  : CSKY32Inst<AddrMode32SDF, 0x3d, outs, ins, opcodestr, pattern> {
  bits<10> regs;
  bits<5> rx;

  let Inst{25} = 0;
  let Inst{24 - 21} = regs{3-0};  //imm4
  let Inst{20 - 16} = rx;  //rx
  let Inst{15 - 9} = sop;
  let Inst{8} = sop_su;
  let Inst{7 - 4} = 0;
  let Inst{3 - 0} = regs{8-5};
}

class F_I8_Z_MEM<bits<7> sop, bits<1> sop_su, dag outs, dag ins, string opcodestr, list<dag> pattern>
  : CSKY32Inst<AddrModeNone, 0x3d, outs, ins, opcodestr, pattern> {
  bits<4> vrz;
  bits<8> imm8;
  let Inst{25} = 0;
  let Inst{24 - 21} = imm8{7 - 4};  //imm4h
  let Inst{20 - 16} = 0;  //rx
  let Inst{15 - 9} = sop;
  let Inst{8} = sop_su;
  let Inst{7 - 4} = imm8{3 - 0}; // imm4l
  let Inst{3 - 0} = vrz;
}

class F_XYZ_MEM<bits<7> sop, bits<1> sop_su, dag outs, dag ins, string opcodestr, list<dag> pattern>
  : CSKY32Inst<AddrModeNone, 0x3d, outs, ins, opcodestr, pattern> {
  bits<5> rx;
  bits<5> ry;
  bits<4> vrz;
  bits<2> imm;

  let Inst{25 - 21} = ry;  // ry;
  let Inst{20 - 16} = rx;  // rx;
  let Inst{15 - 9} = sop;
  let Inst{8} = sop_su;
  let Inst{7} = 0;
  let Inst{6,5} = imm;  // shift;
  let Inst{4} = 0;
  let Inst{3 - 0} = vrz;
}

class F_XYAI_LD<bits<7> sop, bits<1> sop_su, string op, string op_su,
                 RegisterOperand regtype, Operand operand>
  : F_I8_XY_MEM<sop, sop_su, (outs regtype:$vrz), (ins GPR:$rx, operand:$imm8),
    !strconcat(op#op_su, "\t$vrz, ($rx, ${imm8})"), []>;

class F_XYAR_LD<bits<7> sop, bits<1> sop_su, string op, string op_su,
                 RegisterOperand regtype>
  : F_XYZ_MEM<sop, sop_su, (outs regtype:$vrz), (ins GPR:$rx, GPR:$ry, uimm2:$imm),
    op#op_su#"\t$vrz, ($rx, $ry << ${imm})", []>;

class F_XYAI_ST<bits<7> sop, bits<1> sop_su, string op, string op_su,
                 RegisterOperand regtype, Operand operand>
  : F_I8_XY_MEM<sop, sop_su, (outs), (ins regtype:$vrz, GPR:$rx, operand:$imm8),
    !strconcat(op#op_su, "\t$vrz, ($rx, ${imm8})"), []>;

class F_XYAR_ST<bits<7> sop, bits<1> sop_su, string op, string op_su,
                 RegisterOperand regtype>
  : F_XYZ_MEM<sop, sop_su, (outs), (ins regtype:$vrz, GPR:$rx, GPR:$ry, uimm2:$imm),
    op#op_su#"\t$vrz, ($rx, $ry << ${imm})", []>;

def Mem8SL2 : Operand<iPTR>, ComplexPattern<iPTR, 2, "SelectAddrRegImm8", []> {
  let MIOperandInfo = (ops GPR, i32imm);
  let PrintMethod = "printAddrModeRegImmOperand";
  let EncoderMethod = "getAddrModeFloatImm8_sl2OpValue";
}

def FRRS : Operand<iPTR>, ComplexPattern<iPTR, 3, "SelectAddrRegReg", []> {
  let MIOperandInfo = (ops GPR, GPR, i32imm);
  let PrintMethod = "printAddrModeRegRegSLOperand";
  let EncoderMethod = "getAddrModeFloatRegRegSLOpValue";
}

multiclass FT_XYAI_LD<bits<7> sop, string op> {
  def _S :  F_XYAI_LD<sop, 0, op, "s", sFPR32Op, uimm8_2>;
  let Predicates = [HasFPUv2_DF] in
  def _D :  F_XYAI_LD<sop, 1, op, "d", sFPR64Op, uimm8_2>;
}

multiclass FT_XYAR_LD<bits<7> sop, string op> {
  def _S :  F_XYAR_LD<sop, 0, op, "s", sFPR32Op>;
  let Predicates = [HasFPUv2_DF] in
  def _D :  F_XYAR_LD<sop, 1, op, "d", sFPR64Op>;
}

multiclass FT_XYAI_ST<bits<7> sop, string op> {
  def _S :  F_XYAI_ST<sop, 0, op, "s", sFPR32Op, uimm8_2>;
  let Predicates = [HasFPUv2_DF] in
  def _D :  F_XYAI_ST<sop, 1, op, "d", sFPR64Op, uimm8_2>;
}

multiclass FT_XYAR_ST<bits<7> sop, string op> {
  def _S :  F_XYAR_ST<sop, 0, op, "s", sFPR32Op>;
  let Predicates = [HasFPUv2_DF] in
  def _D :  F_XYAR_ST<sop, 1, op, "d", sFPR64Op>;
}

multiclass FT_XYAR_STM<bits<7> sop, string op> {
  def _S :  F_I4_XY_MEM<sop, 0, (outs),
    (ins GPR:$rx, regseq_f1:$regs, variable_ops),
      !strconcat(op#"s", "\t$regs, (${rx})"), []>;
  let Predicates = [HasFPUv2_DF] in
  def _D :  F_I4_XY_MEM<sop, 1, (outs),
    (ins GPR:$rx, regseq_d1:$regs, variable_ops),
      !strconcat(op#"d", "\t$regs, (${rx})"), []>;
}

multiclass FT_XYAR_LDM<bits<7> sop, string op> {
  def _S :  F_I4_XY_MEM<sop, 0, (outs),
    (ins GPR:$rx, regseq_f1:$regs, variable_ops),
      !strconcat(op#"s", "\t$regs, (${rx})"), []>;
  let Predicates = [HasFPUv2_DF] in
  def _D :  F_I4_XY_MEM<sop, 1, (outs),
    (ins GPR:$rx, regseq_d1:$regs, variable_ops),
      !strconcat(op#"d", "\t$regs, (${rx})"), []>;
}
