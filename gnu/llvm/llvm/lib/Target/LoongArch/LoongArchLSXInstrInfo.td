//===- LoongArchLSXInstrInfo.td - LoongArch LSX instructions -*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the SIMD extension instructions.
//
//===----------------------------------------------------------------------===//

def SDT_LoongArchVreplve : SDTypeProfile<1, 2, [SDTCisInt<0>, SDTCisVec<0>,
                                         SDTCisInt<1>, SDTCisVec<1>,
                                         SDTCisSameAs<0, 1>, SDTCisInt<2>]>;
def SDT_LoongArchVecCond : SDTypeProfile<1, 1, [SDTCisInt<0>, SDTCisVec<1>]>;

def SDT_LoongArchVShuf : SDTypeProfile<1, 3, [SDTCisVec<0>,
                                         SDTCisInt<1>, SDTCisVec<1>,
                                         SDTCisSameAs<0, 2>,
                                         SDTCisSameAs<2, 3>]>;
def SDT_LoongArchV2R : SDTypeProfile<1, 2, [SDTCisVec<0>,
                                         SDTCisSameAs<0, 1>, SDTCisSameAs<1, 2>]>;
def SDT_loongArchV1RUimm: SDTypeProfile<1, 2, [SDTCisVec<0>,
                                         SDTCisSameAs<0,1>, SDTCisVT<2, i64>]>;

// Target nodes.
def loongarch_vreplve : SDNode<"LoongArchISD::VREPLVE", SDT_LoongArchVreplve>;
def loongarch_vall_nonzero : SDNode<"LoongArchISD::VALL_NONZERO",
                                    SDT_LoongArchVecCond>;
def loongarch_vany_nonzero : SDNode<"LoongArchISD::VANY_NONZERO",
                                    SDT_LoongArchVecCond>;
def loongarch_vall_zero : SDNode<"LoongArchISD::VALL_ZERO",
                                SDT_LoongArchVecCond>;
def loongarch_vany_zero : SDNode<"LoongArchISD::VANY_ZERO",
                                SDT_LoongArchVecCond>;

def loongarch_vpick_sext_elt : SDNode<"LoongArchISD::VPICK_SEXT_ELT",
                                      SDTypeProfile<1, 3, [SDTCisPtrTy<2>]>>;
def loongarch_vpick_zext_elt : SDNode<"LoongArchISD::VPICK_ZEXT_ELT",
                                      SDTypeProfile<1, 3, [SDTCisPtrTy<2>]>>;

def loongarch_vshuf: SDNode<"LoongArchISD::VSHUF", SDT_LoongArchVShuf>;
def loongarch_vpickev: SDNode<"LoongArchISD::VPICKEV", SDT_LoongArchV2R>;
def loongarch_vpickod: SDNode<"LoongArchISD::VPICKOD", SDT_LoongArchV2R>;
def loongarch_vpackev: SDNode<"LoongArchISD::VPACKEV", SDT_LoongArchV2R>;
def loongarch_vpackod: SDNode<"LoongArchISD::VPACKOD", SDT_LoongArchV2R>;
def loongarch_vilvl: SDNode<"LoongArchISD::VILVL", SDT_LoongArchV2R>;
def loongarch_vilvh: SDNode<"LoongArchISD::VILVH", SDT_LoongArchV2R>;

def loongarch_vshuf4i: SDNode<"LoongArchISD::VSHUF4I", SDT_loongArchV1RUimm>;
def loongarch_vreplvei: SDNode<"LoongArchISD::VREPLVEI", SDT_loongArchV1RUimm>;

def immZExt1 : ImmLeaf<i64, [{return isUInt<1>(Imm);}]>;
def immZExt2 : ImmLeaf<i64, [{return isUInt<2>(Imm);}]>;
def immZExt3 : ImmLeaf<i64, [{return isUInt<3>(Imm);}]>;
def immZExt4 : ImmLeaf<i64, [{return isUInt<4>(Imm);}]>;
def immZExt8 : ImmLeaf<i64, [{return isUInt<8>(Imm);}]>;

class VecCond<SDPatternOperator OpNode, ValueType TyNode,
              RegisterClass RC = LSX128>
    : Pseudo<(outs GPR:$rd), (ins RC:$vj),
             [(set GPR:$rd, (OpNode (TyNode RC:$vj)))]> {
  let hasSideEffects = 0;
  let mayLoad = 0;
  let mayStore = 0;
  let usesCustomInserter = 1;
}

def vsplat_imm_eq_1 : PatFrags<(ops), [(build_vector)], [{
  APInt Imm;
  EVT EltTy = N->getValueType(0).getVectorElementType();

  if (N->getOpcode() == ISD::BITCAST)
    N = N->getOperand(0).getNode();

  return selectVSplat(N, Imm, EltTy.getSizeInBits()) &&
         Imm.getBitWidth() == EltTy.getSizeInBits() && Imm == 1;
}]>;

def vsplati8_imm_eq_7 : PatFrags<(ops), [(build_vector)], [{
  APInt Imm;
  EVT EltTy = N->getValueType(0).getVectorElementType();

  if (N->getOpcode() == ISD::BITCAST)
    N = N->getOperand(0).getNode();

  return selectVSplat(N, Imm, EltTy.getSizeInBits()) &&
         Imm.getBitWidth() == EltTy.getSizeInBits() && Imm == 7;
}]>;
def vsplati16_imm_eq_15 : PatFrags<(ops), [(build_vector)], [{
  APInt Imm;
  EVT EltTy = N->getValueType(0).getVectorElementType();

  if (N->getOpcode() == ISD::BITCAST)
    N = N->getOperand(0).getNode();

  return selectVSplat(N, Imm, EltTy.getSizeInBits()) &&
         Imm.getBitWidth() == EltTy.getSizeInBits() && Imm == 15;
}]>;
def vsplati32_imm_eq_31 : PatFrags<(ops), [(build_vector)], [{
  APInt Imm;
  EVT EltTy = N->getValueType(0).getVectorElementType();

  if (N->getOpcode() == ISD::BITCAST)
    N = N->getOperand(0).getNode();

  return selectVSplat(N, Imm, EltTy.getSizeInBits()) &&
         Imm.getBitWidth() == EltTy.getSizeInBits() && Imm == 31;
}]>;
def vsplati64_imm_eq_63 : PatFrags<(ops), [(build_vector)], [{
  APInt Imm;
  EVT EltTy = N->getValueType(0).getVectorElementType();

  if (N->getOpcode() == ISD::BITCAST)
    N = N->getOperand(0).getNode();

  return selectVSplat(N, Imm, EltTy.getSizeInBits()) &&
         Imm.getBitWidth() == EltTy.getSizeInBits() && Imm == 63;
}]>;

def vsplatf32_fpimm_eq_1
  : PatFrags<(ops), [(bitconvert (v4i32 (build_vector))),
                     (bitconvert (v8i32 (build_vector)))], [{
  APInt Imm;
  EVT EltTy = N->getValueType(0).getVectorElementType();
  N = N->getOperand(0).getNode();

  return selectVSplat(N, Imm, EltTy.getSizeInBits()) &&
         Imm.getBitWidth() == EltTy.getSizeInBits() &&
         Imm == APFloat(+1.0f).bitcastToAPInt();
}]>;
def vsplatf64_fpimm_eq_1
  : PatFrags<(ops), [(bitconvert (v2i64 (build_vector))),
                     (bitconvert (v4i64 (build_vector)))], [{
  APInt Imm;
  EVT EltTy = N->getValueType(0).getVectorElementType();
  N = N->getOperand(0).getNode();

  return selectVSplat(N, Imm, EltTy.getSizeInBits()) &&
         Imm.getBitWidth() == EltTy.getSizeInBits() &&
         Imm == APFloat(+1.0).bitcastToAPInt();
}]>;

def vsplati8imm7   : PatFrag<(ops node:$reg),
                             (and node:$reg, vsplati8_imm_eq_7)>;
def vsplati16imm15 : PatFrag<(ops node:$reg),
                             (and node:$reg, vsplati16_imm_eq_15)>;
def vsplati32imm31 : PatFrag<(ops node:$reg),
                             (and node:$reg, vsplati32_imm_eq_31)>;
def vsplati64imm63 : PatFrag<(ops node:$reg),
                             (and node:$reg, vsplati64_imm_eq_63)>;

foreach N = [3, 4, 5, 6, 8] in
  def SplatPat_uimm#N : ComplexPattern<vAny, 1, "selectVSplatImm<"#N#">",
                                       [build_vector, bitconvert], [], 2>;

foreach N = [5] in
  def SplatPat_simm#N : ComplexPattern<vAny, 1, "selectVSplatImm<"#N#", true>",
                                       [build_vector, bitconvert]>;

def vsplat_uimm_inv_pow2 : ComplexPattern<vAny, 1, "selectVSplatUimmInvPow2",
                                          [build_vector, bitconvert]>;

def vsplat_uimm_pow2 : ComplexPattern<vAny, 1, "selectVSplatUimmPow2",
                                      [build_vector, bitconvert]>;

def muladd : PatFrag<(ops node:$vd, node:$vj, node:$vk),
                     (add node:$vd, (mul node:$vj, node:$vk))>;

def mulsub : PatFrag<(ops node:$vd, node:$vj, node:$vk),
                     (sub node:$vd, (mul node:$vj, node:$vk))>;

def lsxsplati8  : PatFrag<(ops node:$e0),
                          (v16i8 (build_vector node:$e0, node:$e0,
                                               node:$e0, node:$e0,
                                               node:$e0, node:$e0,
                                               node:$e0, node:$e0,
                                               node:$e0, node:$e0,
                                               node:$e0, node:$e0,
                                               node:$e0, node:$e0,
                                               node:$e0, node:$e0))>;
def lsxsplati16 : PatFrag<(ops node:$e0),
                          (v8i16 (build_vector node:$e0, node:$e0,
                                               node:$e0, node:$e0,
                                               node:$e0, node:$e0,
                                               node:$e0, node:$e0))>;
def lsxsplati32 : PatFrag<(ops node:$e0),
                          (v4i32 (build_vector node:$e0, node:$e0,
                                               node:$e0, node:$e0))>;
def lsxsplati64 : PatFrag<(ops node:$e0),
                          (v2i64 (build_vector node:$e0, node:$e0))>;
def lsxsplatf32 : PatFrag<(ops node:$e0),
                          (v4f32 (build_vector node:$e0, node:$e0,
                                               node:$e0, node:$e0))>;
def lsxsplatf64 : PatFrag<(ops node:$e0),
                          (v2f64 (build_vector node:$e0, node:$e0))>;

def to_valid_timm : SDNodeXForm<timm, [{
  auto CN = cast<ConstantSDNode>(N);
  return CurDAG->getTargetConstant(CN->getSExtValue(), SDLoc(N), Subtarget->getGRLenVT());
}]>;

//===----------------------------------------------------------------------===//
// Instruction class templates
//===----------------------------------------------------------------------===//

class LSX1RI13_VI<bits<32> op, Operand ImmOpnd = simm13>
    : Fmt1RI13_VI<op, (outs LSX128:$vd), (ins ImmOpnd:$imm13), "$vd, $imm13">;

class LSX2R_VV<bits<32> op>
    : Fmt2R_VV<op, (outs LSX128:$vd), (ins LSX128:$vj), "$vd, $vj">;

class LSX2R_VR<bits<32> op>
    : Fmt2R_VR<op, (outs LSX128:$vd), (ins GPR:$rj), "$vd, $rj">;

class LSX2R_CV<bits<32> op>
    : Fmt2R_CV<op, (outs CFR:$cd), (ins LSX128:$vj), "$cd, $vj">;

class LSX2RI1_VVI<bits<32> op, Operand ImmOpnd = uimm1>
    : Fmt2RI1_VVI<op, (outs LSX128:$vd), (ins LSX128:$vj, ImmOpnd:$imm1),
                  "$vd, $vj, $imm1">;

class LSX2RI1_RVI<bits<32> op, Operand ImmOpnd = uimm1>
    : Fmt2RI1_RVI<op, (outs GPR:$rd), (ins LSX128:$vj, ImmOpnd:$imm1),
                  "$rd, $vj, $imm1">;

class LSX2RI2_VVI<bits<32> op, Operand ImmOpnd = uimm2>
    : Fmt2RI2_VVI<op, (outs LSX128:$vd), (ins LSX128:$vj, ImmOpnd:$imm2),
                  "$vd, $vj, $imm2">;

class LSX2RI2_RVI<bits<32> op, Operand ImmOpnd = uimm2>
    : Fmt2RI2_RVI<op, (outs GPR:$rd), (ins LSX128:$vj, ImmOpnd:$imm2),
                  "$rd, $vj, $imm2">;

class LSX2RI3_VVI<bits<32> op, Operand ImmOpnd = uimm3>
    : Fmt2RI3_VVI<op, (outs LSX128:$vd), (ins LSX128:$vj, ImmOpnd:$imm3),
                  "$vd, $vj, $imm3">;

class LSX2RI3_RVI<bits<32> op, Operand ImmOpnd = uimm3>
    : Fmt2RI3_RVI<op, (outs GPR:$rd), (ins LSX128:$vj, ImmOpnd:$imm3),
                  "$rd, $vj, $imm3">;

class LSX2RI4_VVI<bits<32> op, Operand ImmOpnd = uimm4>
    : Fmt2RI4_VVI<op, (outs LSX128:$vd), (ins LSX128:$vj, ImmOpnd:$imm4),
                  "$vd, $vj, $imm4">;

class LSX2RI4_RVI<bits<32> op, Operand ImmOpnd = uimm4>
    : Fmt2RI4_RVI<op, (outs GPR:$rd), (ins LSX128:$vj, ImmOpnd:$imm4),
                  "$rd, $vj, $imm4">;

class LSX2RI5_VVI<bits<32> op, Operand ImmOpnd = uimm5>
    : Fmt2RI5_VVI<op, (outs LSX128:$vd), (ins LSX128:$vj, ImmOpnd:$imm5),
                  "$vd, $vj, $imm5">;

class LSX2RI6_VVI<bits<32> op, Operand ImmOpnd = uimm6>
    : Fmt2RI6_VVI<op, (outs LSX128:$vd), (ins LSX128:$vj, ImmOpnd:$imm6),
                  "$vd, $vj, $imm6">;

class LSX2RI8_VVI<bits<32> op, Operand ImmOpnd = uimm8>
    : Fmt2RI8_VVI<op, (outs LSX128:$vd), (ins LSX128:$vj, ImmOpnd:$imm8),
                  "$vd, $vj, $imm8">;

class LSX2RI8I1_VRII<bits<32> op, Operand ImmOpnd = simm8,
                     Operand IdxOpnd = uimm1>
    : Fmt2RI8I1_VRII<op, (outs),
                     (ins LSX128:$vd, GPR:$rj, ImmOpnd:$imm8, IdxOpnd:$imm1),
                     "$vd, $rj, $imm8, $imm1">;
class LSX2RI8I2_VRII<bits<32> op, Operand ImmOpnd = simm8,
                     Operand IdxOpnd = uimm2>
    : Fmt2RI8I2_VRII<op, (outs),
                     (ins LSX128:$vd, GPR:$rj, ImmOpnd:$imm8, IdxOpnd:$imm2),
                     "$vd, $rj, $imm8, $imm2">;
class LSX2RI8I3_VRII<bits<32> op, Operand ImmOpnd = simm8,
                     Operand IdxOpnd = uimm3>
    : Fmt2RI8I3_VRII<op, (outs),
                     (ins LSX128:$vd, GPR:$rj, ImmOpnd:$imm8, IdxOpnd:$imm3),
                     "$vd, $rj, $imm8, $imm3">;
class LSX2RI8I4_VRII<bits<32> op, Operand ImmOpnd = simm8,
                     Operand IdxOpnd = uimm4>
    : Fmt2RI8I4_VRII<op, (outs),
                     (ins LSX128:$vd, GPR:$rj, ImmOpnd:$imm8, IdxOpnd:$imm4),
                     "$vd, $rj, $imm8, $imm4">;

class LSX3R_VVV<bits<32> op>
    : Fmt3R_VVV<op, (outs LSX128:$vd), (ins LSX128:$vj, LSX128:$vk),
                "$vd, $vj, $vk">;

class LSX3R_VVR<bits<32> op>
    : Fmt3R_VVR<op, (outs LSX128:$vd), (ins LSX128:$vj, GPR:$rk),
                "$vd, $vj, $rk">;

class LSX4R_VVVV<bits<32> op>
    : Fmt4R_VVVV<op, (outs LSX128:$vd),
                 (ins LSX128:$vj, LSX128:$vk, LSX128:$va),
                 "$vd, $vj, $vk, $va">;

let Constraints = "$vd = $dst" in {

class LSX2RI1_VVRI<bits<32> op, Operand ImmOpnd = uimm1>
    : Fmt2RI1_VRI<op, (outs LSX128:$dst), (ins LSX128:$vd, GPR:$rj, ImmOpnd:$imm1),
                  "$vd, $rj, $imm1">;
class LSX2RI2_VVRI<bits<32> op, Operand ImmOpnd = uimm2>
    : Fmt2RI2_VRI<op, (outs LSX128:$dst), (ins LSX128:$vd, GPR:$rj, ImmOpnd:$imm2),
                  "$vd, $rj, $imm2">;
class LSX2RI3_VVRI<bits<32> op, Operand ImmOpnd = uimm3>
    : Fmt2RI3_VRI<op, (outs LSX128:$dst), (ins LSX128:$vd, GPR:$rj, ImmOpnd:$imm3),
                  "$vd, $rj, $imm3">;
class LSX2RI4_VVRI<bits<32> op, Operand ImmOpnd = uimm4>
    : Fmt2RI4_VRI<op, (outs LSX128:$dst), (ins LSX128:$vd, GPR:$rj, ImmOpnd:$imm4),
                  "$vd, $rj, $imm4">;

class LSX2RI4_VVVI<bits<32> op, Operand ImmOpnd = uimm4>
    : Fmt2RI4_VVI<op, (outs LSX128:$dst), (ins LSX128:$vd, LSX128:$vj, ImmOpnd:$imm4),
                  "$vd, $vj, $imm4">;
class LSX2RI5_VVVI<bits<32> op, Operand ImmOpnd = uimm5>
    : Fmt2RI5_VVI<op, (outs LSX128:$dst), (ins LSX128:$vd, LSX128:$vj, ImmOpnd:$imm5),
                  "$vd, $vj, $imm5">;
class LSX2RI6_VVVI<bits<32> op, Operand ImmOpnd = uimm6>
    : Fmt2RI6_VVI<op, (outs LSX128:$dst), (ins LSX128:$vd, LSX128:$vj, ImmOpnd:$imm6),
                  "$vd, $vj, $imm6">;
class LSX2RI7_VVVI<bits<32> op, Operand ImmOpnd = uimm7>
    : Fmt2RI7_VVI<op, (outs LSX128:$dst), (ins LSX128:$vd, LSX128:$vj, ImmOpnd:$imm7),
                  "$vd, $vj, $imm7">;

class LSX2RI8_VVVI<bits<32> op, Operand ImmOpnd = uimm8>
    : Fmt2RI8_VVI<op, (outs LSX128:$dst), (ins LSX128:$vd, LSX128:$vj, ImmOpnd:$imm8),
                  "$vd, $vj, $imm8">;

class LSX3R_VVVV<bits<32> op>
    : Fmt3R_VVV<op, (outs LSX128:$dst), (ins LSX128:$vd, LSX128:$vj, LSX128:$vk),
                "$vd, $vj, $vk">;

} // Constraints = "$vd = $dst"

class LSX2RI9_Load<bits<32> op, Operand ImmOpnd = simm9_lsl3>
    : Fmt2RI9_VRI<op, (outs LSX128:$vd), (ins GPR:$rj, ImmOpnd:$imm9),
                  "$vd, $rj, $imm9">;
class LSX2RI10_Load<bits<32> op, Operand ImmOpnd = simm10_lsl2>
    : Fmt2RI10_VRI<op, (outs LSX128:$vd), (ins GPR:$rj, ImmOpnd:$imm10),
                  "$vd, $rj, $imm10">;
class LSX2RI11_Load<bits<32> op, Operand ImmOpnd = simm11_lsl1>
    : Fmt2RI11_VRI<op, (outs LSX128:$vd), (ins GPR:$rj, ImmOpnd:$imm11),
                  "$vd, $rj, $imm11">;
class LSX2RI12_Load<bits<32> op, Operand ImmOpnd = simm12>
    : Fmt2RI12_VRI<op, (outs LSX128:$vd), (ins GPR:$rj, ImmOpnd:$imm12),
                  "$vd, $rj, $imm12">;
class LSX2RI12_Store<bits<32> op, Operand ImmOpnd = simm12>
    : Fmt2RI12_VRI<op, (outs), (ins LSX128:$vd, GPR:$rj, ImmOpnd:$imm12),
                  "$vd, $rj, $imm12">;

class LSX3R_Load<bits<32> op>
    : Fmt3R_VRR<op, (outs LSX128:$vd), (ins GPR:$rj, GPR:$rk),
                "$vd, $rj, $rk">;
class LSX3R_Store<bits<32> op>
    : Fmt3R_VRR<op, (outs), (ins LSX128:$vd, GPR:$rj, GPR:$rk),
                "$vd, $rj, $rk">;

//===----------------------------------------------------------------------===//
// Instructions
//===----------------------------------------------------------------------===//

let hasSideEffects = 0, Predicates = [HasExtLSX] in {

let mayLoad = 0, mayStore = 0 in {

def VADD_B : LSX3R_VVV<0x700a0000>;
def VADD_H : LSX3R_VVV<0x700a8000>;
def VADD_W : LSX3R_VVV<0x700b0000>;
def VADD_D : LSX3R_VVV<0x700b8000>;
def VADD_Q : LSX3R_VVV<0x712d0000>;

def VSUB_B : LSX3R_VVV<0x700c0000>;
def VSUB_H : LSX3R_VVV<0x700c8000>;
def VSUB_W : LSX3R_VVV<0x700d0000>;
def VSUB_D : LSX3R_VVV<0x700d8000>;
def VSUB_Q : LSX3R_VVV<0x712d8000>;

def VADDI_BU : LSX2RI5_VVI<0x728a0000>;
def VADDI_HU : LSX2RI5_VVI<0x728a8000>;
def VADDI_WU : LSX2RI5_VVI<0x728b0000>;
def VADDI_DU : LSX2RI5_VVI<0x728b8000>;

def VSUBI_BU : LSX2RI5_VVI<0x728c0000>;
def VSUBI_HU : LSX2RI5_VVI<0x728c8000>;
def VSUBI_WU : LSX2RI5_VVI<0x728d0000>;
def VSUBI_DU : LSX2RI5_VVI<0x728d8000>;

def VNEG_B : LSX2R_VV<0x729c3000>;
def VNEG_H : LSX2R_VV<0x729c3400>;
def VNEG_W : LSX2R_VV<0x729c3800>;
def VNEG_D : LSX2R_VV<0x729c3c00>;

def VSADD_B : LSX3R_VVV<0x70460000>;
def VSADD_H : LSX3R_VVV<0x70468000>;
def VSADD_W : LSX3R_VVV<0x70470000>;
def VSADD_D : LSX3R_VVV<0x70478000>;
def VSADD_BU : LSX3R_VVV<0x704a0000>;
def VSADD_HU : LSX3R_VVV<0x704a8000>;
def VSADD_WU : LSX3R_VVV<0x704b0000>;
def VSADD_DU : LSX3R_VVV<0x704b8000>;

def VSSUB_B : LSX3R_VVV<0x70480000>;
def VSSUB_H : LSX3R_VVV<0x70488000>;
def VSSUB_W : LSX3R_VVV<0x70490000>;
def VSSUB_D : LSX3R_VVV<0x70498000>;
def VSSUB_BU : LSX3R_VVV<0x704c0000>;
def VSSUB_HU : LSX3R_VVV<0x704c8000>;
def VSSUB_WU : LSX3R_VVV<0x704d0000>;
def VSSUB_DU : LSX3R_VVV<0x704d8000>;

def VHADDW_H_B : LSX3R_VVV<0x70540000>;
def VHADDW_W_H : LSX3R_VVV<0x70548000>;
def VHADDW_D_W : LSX3R_VVV<0x70550000>;
def VHADDW_Q_D : LSX3R_VVV<0x70558000>;
def VHADDW_HU_BU : LSX3R_VVV<0x70580000>;
def VHADDW_WU_HU : LSX3R_VVV<0x70588000>;
def VHADDW_DU_WU : LSX3R_VVV<0x70590000>;
def VHADDW_QU_DU : LSX3R_VVV<0x70598000>;

def VHSUBW_H_B : LSX3R_VVV<0x70560000>;
def VHSUBW_W_H : LSX3R_VVV<0x70568000>;
def VHSUBW_D_W : LSX3R_VVV<0x70570000>;
def VHSUBW_Q_D : LSX3R_VVV<0x70578000>;
def VHSUBW_HU_BU : LSX3R_VVV<0x705a0000>;
def VHSUBW_WU_HU : LSX3R_VVV<0x705a8000>;
def VHSUBW_DU_WU : LSX3R_VVV<0x705b0000>;
def VHSUBW_QU_DU : LSX3R_VVV<0x705b8000>;

def VADDWEV_H_B : LSX3R_VVV<0x701e0000>;
def VADDWEV_W_H : LSX3R_VVV<0x701e8000>;
def VADDWEV_D_W : LSX3R_VVV<0x701f0000>;
def VADDWEV_Q_D : LSX3R_VVV<0x701f8000>;
def VADDWOD_H_B : LSX3R_VVV<0x70220000>;
def VADDWOD_W_H : LSX3R_VVV<0x70228000>;
def VADDWOD_D_W : LSX3R_VVV<0x70230000>;
def VADDWOD_Q_D : LSX3R_VVV<0x70238000>;

def VSUBWEV_H_B : LSX3R_VVV<0x70200000>;
def VSUBWEV_W_H : LSX3R_VVV<0x70208000>;
def VSUBWEV_D_W : LSX3R_VVV<0x70210000>;
def VSUBWEV_Q_D : LSX3R_VVV<0x70218000>;
def VSUBWOD_H_B : LSX3R_VVV<0x70240000>;
def VSUBWOD_W_H : LSX3R_VVV<0x70248000>;
def VSUBWOD_D_W : LSX3R_VVV<0x70250000>;
def VSUBWOD_Q_D : LSX3R_VVV<0x70258000>;

def VADDWEV_H_BU : LSX3R_VVV<0x702e0000>;
def VADDWEV_W_HU : LSX3R_VVV<0x702e8000>;
def VADDWEV_D_WU : LSX3R_VVV<0x702f0000>;
def VADDWEV_Q_DU : LSX3R_VVV<0x702f8000>;
def VADDWOD_H_BU : LSX3R_VVV<0x70320000>;
def VADDWOD_W_HU : LSX3R_VVV<0x70328000>;
def VADDWOD_D_WU : LSX3R_VVV<0x70330000>;
def VADDWOD_Q_DU : LSX3R_VVV<0x70338000>;

def VSUBWEV_H_BU : LSX3R_VVV<0x70300000>;
def VSUBWEV_W_HU : LSX3R_VVV<0x70308000>;
def VSUBWEV_D_WU : LSX3R_VVV<0x70310000>;
def VSUBWEV_Q_DU : LSX3R_VVV<0x70318000>;
def VSUBWOD_H_BU : LSX3R_VVV<0x70340000>;
def VSUBWOD_W_HU : LSX3R_VVV<0x70348000>;
def VSUBWOD_D_WU : LSX3R_VVV<0x70350000>;
def VSUBWOD_Q_DU : LSX3R_VVV<0x70358000>;

def VADDWEV_H_BU_B : LSX3R_VVV<0x703e0000>;
def VADDWEV_W_HU_H : LSX3R_VVV<0x703e8000>;
def VADDWEV_D_WU_W : LSX3R_VVV<0x703f0000>;
def VADDWEV_Q_DU_D : LSX3R_VVV<0x703f8000>;
def VADDWOD_H_BU_B : LSX3R_VVV<0x70400000>;
def VADDWOD_W_HU_H : LSX3R_VVV<0x70408000>;
def VADDWOD_D_WU_W : LSX3R_VVV<0x70410000>;
def VADDWOD_Q_DU_D : LSX3R_VVV<0x70418000>;

def VAVG_B : LSX3R_VVV<0x70640000>;
def VAVG_H : LSX3R_VVV<0x70648000>;
def VAVG_W : LSX3R_VVV<0x70650000>;
def VAVG_D : LSX3R_VVV<0x70658000>;
def VAVG_BU : LSX3R_VVV<0x70660000>;
def VAVG_HU : LSX3R_VVV<0x70668000>;
def VAVG_WU : LSX3R_VVV<0x70670000>;
def VAVG_DU : LSX3R_VVV<0x70678000>;
def VAVGR_B : LSX3R_VVV<0x70680000>;
def VAVGR_H : LSX3R_VVV<0x70688000>;
def VAVGR_W : LSX3R_VVV<0x70690000>;
def VAVGR_D : LSX3R_VVV<0x70698000>;
def VAVGR_BU : LSX3R_VVV<0x706a0000>;
def VAVGR_HU : LSX3R_VVV<0x706a8000>;
def VAVGR_WU : LSX3R_VVV<0x706b0000>;
def VAVGR_DU : LSX3R_VVV<0x706b8000>;

def VABSD_B : LSX3R_VVV<0x70600000>;
def VABSD_H : LSX3R_VVV<0x70608000>;
def VABSD_W : LSX3R_VVV<0x70610000>;
def VABSD_D : LSX3R_VVV<0x70618000>;
def VABSD_BU : LSX3R_VVV<0x70620000>;
def VABSD_HU : LSX3R_VVV<0x70628000>;
def VABSD_WU : LSX3R_VVV<0x70630000>;
def VABSD_DU : LSX3R_VVV<0x70638000>;

def VADDA_B : LSX3R_VVV<0x705c0000>;
def VADDA_H : LSX3R_VVV<0x705c8000>;
def VADDA_W : LSX3R_VVV<0x705d0000>;
def VADDA_D : LSX3R_VVV<0x705d8000>;

def VMAX_B : LSX3R_VVV<0x70700000>;
def VMAX_H : LSX3R_VVV<0x70708000>;
def VMAX_W : LSX3R_VVV<0x70710000>;
def VMAX_D : LSX3R_VVV<0x70718000>;
def VMAXI_B : LSX2RI5_VVI<0x72900000, simm5>;
def VMAXI_H : LSX2RI5_VVI<0x72908000, simm5>;
def VMAXI_W : LSX2RI5_VVI<0x72910000, simm5>;
def VMAXI_D : LSX2RI5_VVI<0x72918000, simm5>;
def VMAX_BU : LSX3R_VVV<0x70740000>;
def VMAX_HU : LSX3R_VVV<0x70748000>;
def VMAX_WU : LSX3R_VVV<0x70750000>;
def VMAX_DU : LSX3R_VVV<0x70758000>;
def VMAXI_BU : LSX2RI5_VVI<0x72940000>;
def VMAXI_HU : LSX2RI5_VVI<0x72948000>;
def VMAXI_WU : LSX2RI5_VVI<0x72950000>;
def VMAXI_DU : LSX2RI5_VVI<0x72958000>;

def VMIN_B : LSX3R_VVV<0x70720000>;
def VMIN_H : LSX3R_VVV<0x70728000>;
def VMIN_W : LSX3R_VVV<0x70730000>;
def VMIN_D : LSX3R_VVV<0x70738000>;
def VMINI_B : LSX2RI5_VVI<0x72920000, simm5>;
def VMINI_H : LSX2RI5_VVI<0x72928000, simm5>;
def VMINI_W : LSX2RI5_VVI<0x72930000, simm5>;
def VMINI_D : LSX2RI5_VVI<0x72938000, simm5>;
def VMIN_BU : LSX3R_VVV<0x70760000>;
def VMIN_HU : LSX3R_VVV<0x70768000>;
def VMIN_WU : LSX3R_VVV<0x70770000>;
def VMIN_DU : LSX3R_VVV<0x70778000>;
def VMINI_BU : LSX2RI5_VVI<0x72960000>;
def VMINI_HU : LSX2RI5_VVI<0x72968000>;
def VMINI_WU : LSX2RI5_VVI<0x72970000>;
def VMINI_DU : LSX2RI5_VVI<0x72978000>;

def VMUL_B : LSX3R_VVV<0x70840000>;
def VMUL_H : LSX3R_VVV<0x70848000>;
def VMUL_W : LSX3R_VVV<0x70850000>;
def VMUL_D : LSX3R_VVV<0x70858000>;

def VMUH_B : LSX3R_VVV<0x70860000>;
def VMUH_H : LSX3R_VVV<0x70868000>;
def VMUH_W : LSX3R_VVV<0x70870000>;
def VMUH_D : LSX3R_VVV<0x70878000>;
def VMUH_BU : LSX3R_VVV<0x70880000>;
def VMUH_HU : LSX3R_VVV<0x70888000>;
def VMUH_WU : LSX3R_VVV<0x70890000>;
def VMUH_DU : LSX3R_VVV<0x70898000>;

def VMULWEV_H_B : LSX3R_VVV<0x70900000>;
def VMULWEV_W_H : LSX3R_VVV<0x70908000>;
def VMULWEV_D_W : LSX3R_VVV<0x70910000>;
def VMULWEV_Q_D : LSX3R_VVV<0x70918000>;
def VMULWOD_H_B : LSX3R_VVV<0x70920000>;
def VMULWOD_W_H : LSX3R_VVV<0x70928000>;
def VMULWOD_D_W : LSX3R_VVV<0x70930000>;
def VMULWOD_Q_D : LSX3R_VVV<0x70938000>;
def VMULWEV_H_BU : LSX3R_VVV<0x70980000>;
def VMULWEV_W_HU : LSX3R_VVV<0x70988000>;
def VMULWEV_D_WU : LSX3R_VVV<0x70990000>;
def VMULWEV_Q_DU : LSX3R_VVV<0x70998000>;
def VMULWOD_H_BU : LSX3R_VVV<0x709a0000>;
def VMULWOD_W_HU : LSX3R_VVV<0x709a8000>;
def VMULWOD_D_WU : LSX3R_VVV<0x709b0000>;
def VMULWOD_Q_DU : LSX3R_VVV<0x709b8000>;
def VMULWEV_H_BU_B : LSX3R_VVV<0x70a00000>;
def VMULWEV_W_HU_H : LSX3R_VVV<0x70a08000>;
def VMULWEV_D_WU_W : LSX3R_VVV<0x70a10000>;
def VMULWEV_Q_DU_D : LSX3R_VVV<0x70a18000>;
def VMULWOD_H_BU_B : LSX3R_VVV<0x70a20000>;
def VMULWOD_W_HU_H : LSX3R_VVV<0x70a28000>;
def VMULWOD_D_WU_W : LSX3R_VVV<0x70a30000>;
def VMULWOD_Q_DU_D : LSX3R_VVV<0x70a38000>;

def VMADD_B : LSX3R_VVVV<0x70a80000>;
def VMADD_H : LSX3R_VVVV<0x70a88000>;
def VMADD_W : LSX3R_VVVV<0x70a90000>;
def VMADD_D : LSX3R_VVVV<0x70a98000>;

def VMSUB_B : LSX3R_VVVV<0x70aa0000>;
def VMSUB_H : LSX3R_VVVV<0x70aa8000>;
def VMSUB_W : LSX3R_VVVV<0x70ab0000>;
def VMSUB_D : LSX3R_VVVV<0x70ab8000>;

def VMADDWEV_H_B : LSX3R_VVVV<0x70ac0000>;
def VMADDWEV_W_H : LSX3R_VVVV<0x70ac8000>;
def VMADDWEV_D_W : LSX3R_VVVV<0x70ad0000>;
def VMADDWEV_Q_D : LSX3R_VVVV<0x70ad8000>;
def VMADDWOD_H_B : LSX3R_VVVV<0x70ae0000>;
def VMADDWOD_W_H : LSX3R_VVVV<0x70ae8000>;
def VMADDWOD_D_W : LSX3R_VVVV<0x70af0000>;
def VMADDWOD_Q_D : LSX3R_VVVV<0x70af8000>;
def VMADDWEV_H_BU : LSX3R_VVVV<0x70b40000>;
def VMADDWEV_W_HU : LSX3R_VVVV<0x70b48000>;
def VMADDWEV_D_WU : LSX3R_VVVV<0x70b50000>;
def VMADDWEV_Q_DU : LSX3R_VVVV<0x70b58000>;
def VMADDWOD_H_BU : LSX3R_VVVV<0x70b60000>;
def VMADDWOD_W_HU : LSX3R_VVVV<0x70b68000>;
def VMADDWOD_D_WU : LSX3R_VVVV<0x70b70000>;
def VMADDWOD_Q_DU : LSX3R_VVVV<0x70b78000>;
def VMADDWEV_H_BU_B : LSX3R_VVVV<0x70bc0000>;
def VMADDWEV_W_HU_H : LSX3R_VVVV<0x70bc8000>;
def VMADDWEV_D_WU_W : LSX3R_VVVV<0x70bd0000>;
def VMADDWEV_Q_DU_D : LSX3R_VVVV<0x70bd8000>;
def VMADDWOD_H_BU_B : LSX3R_VVVV<0x70be0000>;
def VMADDWOD_W_HU_H : LSX3R_VVVV<0x70be8000>;
def VMADDWOD_D_WU_W : LSX3R_VVVV<0x70bf0000>;
def VMADDWOD_Q_DU_D : LSX3R_VVVV<0x70bf8000>;

def VDIV_B : LSX3R_VVV<0x70e00000>;
def VDIV_H : LSX3R_VVV<0x70e08000>;
def VDIV_W : LSX3R_VVV<0x70e10000>;
def VDIV_D : LSX3R_VVV<0x70e18000>;
def VDIV_BU : LSX3R_VVV<0x70e40000>;
def VDIV_HU : LSX3R_VVV<0x70e48000>;
def VDIV_WU : LSX3R_VVV<0x70e50000>;
def VDIV_DU : LSX3R_VVV<0x70e58000>;

def VMOD_B : LSX3R_VVV<0x70e20000>;
def VMOD_H : LSX3R_VVV<0x70e28000>;
def VMOD_W : LSX3R_VVV<0x70e30000>;
def VMOD_D : LSX3R_VVV<0x70e38000>;
def VMOD_BU : LSX3R_VVV<0x70e60000>;
def VMOD_HU : LSX3R_VVV<0x70e68000>;
def VMOD_WU : LSX3R_VVV<0x70e70000>;
def VMOD_DU : LSX3R_VVV<0x70e78000>;

def VSAT_B : LSX2RI3_VVI<0x73242000>;
def VSAT_H : LSX2RI4_VVI<0x73244000>;
def VSAT_W : LSX2RI5_VVI<0x73248000>;
def VSAT_D : LSX2RI6_VVI<0x73250000>;
def VSAT_BU : LSX2RI3_VVI<0x73282000>;
def VSAT_HU : LSX2RI4_VVI<0x73284000>;
def VSAT_WU : LSX2RI5_VVI<0x73288000>;
def VSAT_DU : LSX2RI6_VVI<0x73290000>;

def VEXTH_H_B : LSX2R_VV<0x729ee000>;
def VEXTH_W_H : LSX2R_VV<0x729ee400>;
def VEXTH_D_W : LSX2R_VV<0x729ee800>;
def VEXTH_Q_D : LSX2R_VV<0x729eec00>;
def VEXTH_HU_BU : LSX2R_VV<0x729ef000>;
def VEXTH_WU_HU : LSX2R_VV<0x729ef400>;
def VEXTH_DU_WU : LSX2R_VV<0x729ef800>;
def VEXTH_QU_DU : LSX2R_VV<0x729efc00>;

def VSIGNCOV_B : LSX3R_VVV<0x712e0000>;
def VSIGNCOV_H : LSX3R_VVV<0x712e8000>;
def VSIGNCOV_W : LSX3R_VVV<0x712f0000>;
def VSIGNCOV_D : LSX3R_VVV<0x712f8000>;

def VMSKLTZ_B : LSX2R_VV<0x729c4000>;
def VMSKLTZ_H : LSX2R_VV<0x729c4400>;
def VMSKLTZ_W : LSX2R_VV<0x729c4800>;
def VMSKLTZ_D : LSX2R_VV<0x729c4c00>;

def VMSKGEZ_B : LSX2R_VV<0x729c5000>;

def VMSKNZ_B : LSX2R_VV<0x729c6000>;

def VLDI : LSX1RI13_VI<0x73e00000>;

def VAND_V : LSX3R_VVV<0x71260000>;
def VOR_V : LSX3R_VVV<0x71268000>;
def VXOR_V : LSX3R_VVV<0x71270000>;
def VNOR_V : LSX3R_VVV<0x71278000>;
def VANDN_V : LSX3R_VVV<0x71280000>;
def VORN_V : LSX3R_VVV<0x71288000>;

def VANDI_B : LSX2RI8_VVI<0x73d00000>;
def VORI_B : LSX2RI8_VVI<0x73d40000>;
def VXORI_B : LSX2RI8_VVI<0x73d80000>;
def VNORI_B : LSX2RI8_VVI<0x73dc0000>;

def VSLL_B : LSX3R_VVV<0x70e80000>;
def VSLL_H : LSX3R_VVV<0x70e88000>;
def VSLL_W : LSX3R_VVV<0x70e90000>;
def VSLL_D : LSX3R_VVV<0x70e98000>;
def VSLLI_B : LSX2RI3_VVI<0x732c2000>;
def VSLLI_H : LSX2RI4_VVI<0x732c4000>;
def VSLLI_W : LSX2RI5_VVI<0x732c8000>;
def VSLLI_D : LSX2RI6_VVI<0x732d0000>;

def VSRL_B : LSX3R_VVV<0x70ea0000>;
def VSRL_H : LSX3R_VVV<0x70ea8000>;
def VSRL_W : LSX3R_VVV<0x70eb0000>;
def VSRL_D : LSX3R_VVV<0x70eb8000>;
def VSRLI_B : LSX2RI3_VVI<0x73302000>;
def VSRLI_H : LSX2RI4_VVI<0x73304000>;
def VSRLI_W : LSX2RI5_VVI<0x73308000>;
def VSRLI_D : LSX2RI6_VVI<0x73310000>;

def VSRA_B : LSX3R_VVV<0x70ec0000>;
def VSRA_H : LSX3R_VVV<0x70ec8000>;
def VSRA_W : LSX3R_VVV<0x70ed0000>;
def VSRA_D : LSX3R_VVV<0x70ed8000>;
def VSRAI_B : LSX2RI3_VVI<0x73342000>;
def VSRAI_H : LSX2RI4_VVI<0x73344000>;
def VSRAI_W : LSX2RI5_VVI<0x73348000>;
def VSRAI_D : LSX2RI6_VVI<0x73350000>;

def VROTR_B : LSX3R_VVV<0x70ee0000>;
def VROTR_H : LSX3R_VVV<0x70ee8000>;
def VROTR_W : LSX3R_VVV<0x70ef0000>;
def VROTR_D : LSX3R_VVV<0x70ef8000>;
def VROTRI_B : LSX2RI3_VVI<0x72a02000>;
def VROTRI_H : LSX2RI4_VVI<0x72a04000>;
def VROTRI_W : LSX2RI5_VVI<0x72a08000>;
def VROTRI_D : LSX2RI6_VVI<0x72a10000>;

def VSLLWIL_H_B : LSX2RI3_VVI<0x73082000>;
def VSLLWIL_W_H : LSX2RI4_VVI<0x73084000>;
def VSLLWIL_D_W : LSX2RI5_VVI<0x73088000>;
def VEXTL_Q_D : LSX2R_VV<0x73090000>;
def VSLLWIL_HU_BU : LSX2RI3_VVI<0x730c2000>;
def VSLLWIL_WU_HU : LSX2RI4_VVI<0x730c4000>;
def VSLLWIL_DU_WU : LSX2RI5_VVI<0x730c8000>;
def VEXTL_QU_DU : LSX2R_VV<0x730d0000>;

def VSRLR_B : LSX3R_VVV<0x70f00000>;
def VSRLR_H : LSX3R_VVV<0x70f08000>;
def VSRLR_W : LSX3R_VVV<0x70f10000>;
def VSRLR_D : LSX3R_VVV<0x70f18000>;
def VSRLRI_B : LSX2RI3_VVI<0x72a42000>;
def VSRLRI_H : LSX2RI4_VVI<0x72a44000>;
def VSRLRI_W : LSX2RI5_VVI<0x72a48000>;
def VSRLRI_D : LSX2RI6_VVI<0x72a50000>;

def VSRAR_B : LSX3R_VVV<0x70f20000>;
def VSRAR_H : LSX3R_VVV<0x70f28000>;
def VSRAR_W : LSX3R_VVV<0x70f30000>;
def VSRAR_D : LSX3R_VVV<0x70f38000>;
def VSRARI_B : LSX2RI3_VVI<0x72a82000>;
def VSRARI_H : LSX2RI4_VVI<0x72a84000>;
def VSRARI_W : LSX2RI5_VVI<0x72a88000>;
def VSRARI_D : LSX2RI6_VVI<0x72a90000>;

def VSRLN_B_H : LSX3R_VVV<0x70f48000>;
def VSRLN_H_W : LSX3R_VVV<0x70f50000>;
def VSRLN_W_D : LSX3R_VVV<0x70f58000>;
def VSRAN_B_H : LSX3R_VVV<0x70f68000>;
def VSRAN_H_W : LSX3R_VVV<0x70f70000>;
def VSRAN_W_D : LSX3R_VVV<0x70f78000>;

def VSRLNI_B_H : LSX2RI4_VVVI<0x73404000>;
def VSRLNI_H_W : LSX2RI5_VVVI<0x73408000>;
def VSRLNI_W_D : LSX2RI6_VVVI<0x73410000>;
def VSRLNI_D_Q : LSX2RI7_VVVI<0x73420000>;
def VSRANI_B_H : LSX2RI4_VVVI<0x73584000>;
def VSRANI_H_W : LSX2RI5_VVVI<0x73588000>;
def VSRANI_W_D : LSX2RI6_VVVI<0x73590000>;
def VSRANI_D_Q : LSX2RI7_VVVI<0x735a0000>;

def VSRLRN_B_H : LSX3R_VVV<0x70f88000>;
def VSRLRN_H_W : LSX3R_VVV<0x70f90000>;
def VSRLRN_W_D : LSX3R_VVV<0x70f98000>;
def VSRARN_B_H : LSX3R_VVV<0x70fa8000>;
def VSRARN_H_W : LSX3R_VVV<0x70fb0000>;
def VSRARN_W_D : LSX3R_VVV<0x70fb8000>;

def VSRLRNI_B_H : LSX2RI4_VVVI<0x73444000>;
def VSRLRNI_H_W : LSX2RI5_VVVI<0x73448000>;
def VSRLRNI_W_D : LSX2RI6_VVVI<0x73450000>;
def VSRLRNI_D_Q : LSX2RI7_VVVI<0x73460000>;
def VSRARNI_B_H : LSX2RI4_VVVI<0x735c4000>;
def VSRARNI_H_W : LSX2RI5_VVVI<0x735c8000>;
def VSRARNI_W_D : LSX2RI6_VVVI<0x735d0000>;
def VSRARNI_D_Q : LSX2RI7_VVVI<0x735e0000>;

def VSSRLN_B_H : LSX3R_VVV<0x70fc8000>;
def VSSRLN_H_W : LSX3R_VVV<0x70fd0000>;
def VSSRLN_W_D : LSX3R_VVV<0x70fd8000>;
def VSSRAN_B_H : LSX3R_VVV<0x70fe8000>;
def VSSRAN_H_W : LSX3R_VVV<0x70ff0000>;
def VSSRAN_W_D : LSX3R_VVV<0x70ff8000>;
def VSSRLN_BU_H : LSX3R_VVV<0x71048000>;
def VSSRLN_HU_W : LSX3R_VVV<0x71050000>;
def VSSRLN_WU_D : LSX3R_VVV<0x71058000>;
def VSSRAN_BU_H : LSX3R_VVV<0x71068000>;
def VSSRAN_HU_W : LSX3R_VVV<0x71070000>;
def VSSRAN_WU_D : LSX3R_VVV<0x71078000>;

def VSSRLNI_B_H : LSX2RI4_VVVI<0x73484000>;
def VSSRLNI_H_W : LSX2RI5_VVVI<0x73488000>;
def VSSRLNI_W_D : LSX2RI6_VVVI<0x73490000>;
def VSSRLNI_D_Q : LSX2RI7_VVVI<0x734a0000>;
def VSSRANI_B_H : LSX2RI4_VVVI<0x73604000>;
def VSSRANI_H_W : LSX2RI5_VVVI<0x73608000>;
def VSSRANI_W_D : LSX2RI6_VVVI<0x73610000>;
def VSSRANI_D_Q : LSX2RI7_VVVI<0x73620000>;
def VSSRLNI_BU_H : LSX2RI4_VVVI<0x734c4000>;
def VSSRLNI_HU_W : LSX2RI5_VVVI<0x734c8000>;
def VSSRLNI_WU_D : LSX2RI6_VVVI<0x734d0000>;
def VSSRLNI_DU_Q : LSX2RI7_VVVI<0x734e0000>;
def VSSRANI_BU_H : LSX2RI4_VVVI<0x73644000>;
def VSSRANI_HU_W : LSX2RI5_VVVI<0x73648000>;
def VSSRANI_WU_D : LSX2RI6_VVVI<0x73650000>;
def VSSRANI_DU_Q : LSX2RI7_VVVI<0x73660000>;

def VSSRLRN_B_H : LSX3R_VVV<0x71008000>;
def VSSRLRN_H_W : LSX3R_VVV<0x71010000>;
def VSSRLRN_W_D : LSX3R_VVV<0x71018000>;
def VSSRARN_B_H : LSX3R_VVV<0x71028000>;
def VSSRARN_H_W : LSX3R_VVV<0x71030000>;
def VSSRARN_W_D : LSX3R_VVV<0x71038000>;
def VSSRLRN_BU_H : LSX3R_VVV<0x71088000>;
def VSSRLRN_HU_W : LSX3R_VVV<0x71090000>;
def VSSRLRN_WU_D : LSX3R_VVV<0x71098000>;
def VSSRARN_BU_H : LSX3R_VVV<0x710a8000>;
def VSSRARN_HU_W : LSX3R_VVV<0x710b0000>;
def VSSRARN_WU_D : LSX3R_VVV<0x710b8000>;

def VSSRLRNI_B_H : LSX2RI4_VVVI<0x73504000>;
def VSSRLRNI_H_W : LSX2RI5_VVVI<0x73508000>;
def VSSRLRNI_W_D : LSX2RI6_VVVI<0x73510000>;
def VSSRLRNI_D_Q : LSX2RI7_VVVI<0x73520000>;
def VSSRARNI_B_H : LSX2RI4_VVVI<0x73684000>;
def VSSRARNI_H_W : LSX2RI5_VVVI<0x73688000>;
def VSSRARNI_W_D : LSX2RI6_VVVI<0x73690000>;
def VSSRARNI_D_Q : LSX2RI7_VVVI<0x736a0000>;
def VSSRLRNI_BU_H : LSX2RI4_VVVI<0x73544000>;
def VSSRLRNI_HU_W : LSX2RI5_VVVI<0x73548000>;
def VSSRLRNI_WU_D : LSX2RI6_VVVI<0x73550000>;
def VSSRLRNI_DU_Q : LSX2RI7_VVVI<0x73560000>;
def VSSRARNI_BU_H : LSX2RI4_VVVI<0x736c4000>;
def VSSRARNI_HU_W : LSX2RI5_VVVI<0x736c8000>;
def VSSRARNI_WU_D : LSX2RI6_VVVI<0x736d0000>;
def VSSRARNI_DU_Q : LSX2RI7_VVVI<0x736e0000>;

def VCLO_B : LSX2R_VV<0x729c0000>;
def VCLO_H : LSX2R_VV<0x729c0400>;
def VCLO_W : LSX2R_VV<0x729c0800>;
def VCLO_D : LSX2R_VV<0x729c0c00>;
def VCLZ_B : LSX2R_VV<0x729c1000>;
def VCLZ_H : LSX2R_VV<0x729c1400>;
def VCLZ_W : LSX2R_VV<0x729c1800>;
def VCLZ_D : LSX2R_VV<0x729c1c00>;

def VPCNT_B : LSX2R_VV<0x729c2000>;
def VPCNT_H : LSX2R_VV<0x729c2400>;
def VPCNT_W : LSX2R_VV<0x729c2800>;
def VPCNT_D : LSX2R_VV<0x729c2c00>;

def VBITCLR_B : LSX3R_VVV<0x710c0000>;
def VBITCLR_H : LSX3R_VVV<0x710c8000>;
def VBITCLR_W : LSX3R_VVV<0x710d0000>;
def VBITCLR_D : LSX3R_VVV<0x710d8000>;
def VBITCLRI_B : LSX2RI3_VVI<0x73102000>;
def VBITCLRI_H : LSX2RI4_VVI<0x73104000>;
def VBITCLRI_W : LSX2RI5_VVI<0x73108000>;
def VBITCLRI_D : LSX2RI6_VVI<0x73110000>;

def VBITSET_B : LSX3R_VVV<0x710e0000>;
def VBITSET_H : LSX3R_VVV<0x710e8000>;
def VBITSET_W : LSX3R_VVV<0x710f0000>;
def VBITSET_D : LSX3R_VVV<0x710f8000>;
def VBITSETI_B : LSX2RI3_VVI<0x73142000>;
def VBITSETI_H : LSX2RI4_VVI<0x73144000>;
def VBITSETI_W : LSX2RI5_VVI<0x73148000>;
def VBITSETI_D : LSX2RI6_VVI<0x73150000>;

def VBITREV_B : LSX3R_VVV<0x71100000>;
def VBITREV_H : LSX3R_VVV<0x71108000>;
def VBITREV_W : LSX3R_VVV<0x71110000>;
def VBITREV_D : LSX3R_VVV<0x71118000>;
def VBITREVI_B : LSX2RI3_VVI<0x73182000>;
def VBITREVI_H : LSX2RI4_VVI<0x73184000>;
def VBITREVI_W : LSX2RI5_VVI<0x73188000>;
def VBITREVI_D : LSX2RI6_VVI<0x73190000>;

def VFRSTP_B : LSX3R_VVVV<0x712b0000>;
def VFRSTP_H : LSX3R_VVVV<0x712b8000>;
def VFRSTPI_B : LSX2RI5_VVVI<0x729a0000>;
def VFRSTPI_H : LSX2RI5_VVVI<0x729a8000>;

def VFADD_S : LSX3R_VVV<0x71308000>;
def VFADD_D : LSX3R_VVV<0x71310000>;
def VFSUB_S : LSX3R_VVV<0x71328000>;
def VFSUB_D : LSX3R_VVV<0x71330000>;
def VFMUL_S : LSX3R_VVV<0x71388000>;
def VFMUL_D : LSX3R_VVV<0x71390000>;
def VFDIV_S : LSX3R_VVV<0x713a8000>;
def VFDIV_D : LSX3R_VVV<0x713b0000>;

def VFMADD_S : LSX4R_VVVV<0x09100000>;
def VFMADD_D : LSX4R_VVVV<0x09200000>;
def VFMSUB_S : LSX4R_VVVV<0x09500000>;
def VFMSUB_D : LSX4R_VVVV<0x09600000>;
def VFNMADD_S : LSX4R_VVVV<0x09900000>;
def VFNMADD_D : LSX4R_VVVV<0x09a00000>;
def VFNMSUB_S : LSX4R_VVVV<0x09d00000>;
def VFNMSUB_D : LSX4R_VVVV<0x09e00000>;

def VFMAX_S : LSX3R_VVV<0x713c8000>;
def VFMAX_D : LSX3R_VVV<0x713d0000>;
def VFMIN_S : LSX3R_VVV<0x713e8000>;
def VFMIN_D : LSX3R_VVV<0x713f0000>;

def VFMAXA_S : LSX3R_VVV<0x71408000>;
def VFMAXA_D : LSX3R_VVV<0x71410000>;
def VFMINA_S : LSX3R_VVV<0x71428000>;
def VFMINA_D : LSX3R_VVV<0x71430000>;

def VFLOGB_S : LSX2R_VV<0x729cc400>;
def VFLOGB_D : LSX2R_VV<0x729cc800>;

def VFCLASS_S : LSX2R_VV<0x729cd400>;
def VFCLASS_D : LSX2R_VV<0x729cd800>;

def VFSQRT_S : LSX2R_VV<0x729ce400>;
def VFSQRT_D : LSX2R_VV<0x729ce800>;
def VFRECIP_S : LSX2R_VV<0x729cf400>;
def VFRECIP_D : LSX2R_VV<0x729cf800>;
def VFRSQRT_S : LSX2R_VV<0x729d0400>;
def VFRSQRT_D : LSX2R_VV<0x729d0800>;
def VFRECIPE_S : LSX2R_VV<0x729d1400>;
def VFRECIPE_D : LSX2R_VV<0x729d1800>;
def VFRSQRTE_S : LSX2R_VV<0x729d2400>;
def VFRSQRTE_D : LSX2R_VV<0x729d2800>;

def VFCVTL_S_H : LSX2R_VV<0x729de800>;
def VFCVTH_S_H : LSX2R_VV<0x729dec00>;
def VFCVTL_D_S : LSX2R_VV<0x729df000>;
def VFCVTH_D_S : LSX2R_VV<0x729df400>;
def VFCVT_H_S : LSX3R_VVV<0x71460000>;
def VFCVT_S_D : LSX3R_VVV<0x71468000>;

def VFRINTRNE_S : LSX2R_VV<0x729d7400>;
def VFRINTRNE_D : LSX2R_VV<0x729d7800>;
def VFRINTRZ_S : LSX2R_VV<0x729d6400>;
def VFRINTRZ_D : LSX2R_VV<0x729d6800>;
def VFRINTRP_S : LSX2R_VV<0x729d5400>;
def VFRINTRP_D : LSX2R_VV<0x729d5800>;
def VFRINTRM_S : LSX2R_VV<0x729d4400>;
def VFRINTRM_D : LSX2R_VV<0x729d4800>;
def VFRINT_S : LSX2R_VV<0x729d3400>;
def VFRINT_D : LSX2R_VV<0x729d3800>;

def VFTINTRNE_W_S : LSX2R_VV<0x729e5000>;
def VFTINTRNE_L_D : LSX2R_VV<0x729e5400>;
def VFTINTRZ_W_S : LSX2R_VV<0x729e4800>;
def VFTINTRZ_L_D : LSX2R_VV<0x729e4c00>;
def VFTINTRP_W_S : LSX2R_VV<0x729e4000>;
def VFTINTRP_L_D : LSX2R_VV<0x729e4400>;
def VFTINTRM_W_S : LSX2R_VV<0x729e3800>;
def VFTINTRM_L_D : LSX2R_VV<0x729e3c00>;
def VFTINT_W_S : LSX2R_VV<0x729e3000>;
def VFTINT_L_D : LSX2R_VV<0x729e3400>;
def VFTINTRZ_WU_S : LSX2R_VV<0x729e7000>;
def VFTINTRZ_LU_D : LSX2R_VV<0x729e7400>;
def VFTINT_WU_S : LSX2R_VV<0x729e5800>;
def VFTINT_LU_D : LSX2R_VV<0x729e5c00>;

def VFTINTRNE_W_D : LSX3R_VVV<0x714b8000>;
def VFTINTRZ_W_D : LSX3R_VVV<0x714b0000>;
def VFTINTRP_W_D : LSX3R_VVV<0x714a8000>;
def VFTINTRM_W_D : LSX3R_VVV<0x714a0000>;
def VFTINT_W_D : LSX3R_VVV<0x71498000>;

def VFTINTRNEL_L_S : LSX2R_VV<0x729ea000>;
def VFTINTRNEH_L_S : LSX2R_VV<0x729ea400>;
def VFTINTRZL_L_S : LSX2R_VV<0x729e9800>;
def VFTINTRZH_L_S : LSX2R_VV<0x729e9c00>;
def VFTINTRPL_L_S : LSX2R_VV<0x729e9000>;
def VFTINTRPH_L_S : LSX2R_VV<0x729e9400>;
def VFTINTRML_L_S : LSX2R_VV<0x729e8800>;
def VFTINTRMH_L_S : LSX2R_VV<0x729e8c00>;
def VFTINTL_L_S : LSX2R_VV<0x729e8000>;
def VFTINTH_L_S : LSX2R_VV<0x729e8400>;

def VFFINT_S_W : LSX2R_VV<0x729e0000>;
def VFFINT_D_L : LSX2R_VV<0x729e0800>;
def VFFINT_S_WU : LSX2R_VV<0x729e0400>;
def VFFINT_D_LU : LSX2R_VV<0x729e0c00>;
def VFFINTL_D_W : LSX2R_VV<0x729e1000>;
def VFFINTH_D_W : LSX2R_VV<0x729e1400>;
def VFFINT_S_L : LSX3R_VVV<0x71480000>;

def VSEQ_B : LSX3R_VVV<0x70000000>;
def VSEQ_H : LSX3R_VVV<0x70008000>;
def VSEQ_W : LSX3R_VVV<0x70010000>;
def VSEQ_D : LSX3R_VVV<0x70018000>;
def VSEQI_B : LSX2RI5_VVI<0x72800000, simm5>;
def VSEQI_H : LSX2RI5_VVI<0x72808000, simm5>;
def VSEQI_W : LSX2RI5_VVI<0x72810000, simm5>;
def VSEQI_D : LSX2RI5_VVI<0x72818000, simm5>;

def VSLE_B : LSX3R_VVV<0x70020000>;
def VSLE_H : LSX3R_VVV<0x70028000>;
def VSLE_W : LSX3R_VVV<0x70030000>;
def VSLE_D : LSX3R_VVV<0x70038000>;
def VSLEI_B : LSX2RI5_VVI<0x72820000, simm5>;
def VSLEI_H : LSX2RI5_VVI<0x72828000, simm5>;
def VSLEI_W : LSX2RI5_VVI<0x72830000, simm5>;
def VSLEI_D : LSX2RI5_VVI<0x72838000, simm5>;

def VSLE_BU : LSX3R_VVV<0x70040000>;
def VSLE_HU : LSX3R_VVV<0x70048000>;
def VSLE_WU : LSX3R_VVV<0x70050000>;
def VSLE_DU : LSX3R_VVV<0x70058000>;
def VSLEI_BU : LSX2RI5_VVI<0x72840000>;
def VSLEI_HU : LSX2RI5_VVI<0x72848000>;
def VSLEI_WU : LSX2RI5_VVI<0x72850000>;
def VSLEI_DU : LSX2RI5_VVI<0x72858000>;

def VSLT_B : LSX3R_VVV<0x70060000>;
def VSLT_H : LSX3R_VVV<0x70068000>;
def VSLT_W : LSX3R_VVV<0x70070000>;
def VSLT_D : LSX3R_VVV<0x70078000>;
def VSLTI_B : LSX2RI5_VVI<0x72860000, simm5>;
def VSLTI_H : LSX2RI5_VVI<0x72868000, simm5>;
def VSLTI_W : LSX2RI5_VVI<0x72870000, simm5>;
def VSLTI_D : LSX2RI5_VVI<0x72878000, simm5>;

def VSLT_BU : LSX3R_VVV<0x70080000>;
def VSLT_HU : LSX3R_VVV<0x70088000>;
def VSLT_WU : LSX3R_VVV<0x70090000>;
def VSLT_DU : LSX3R_VVV<0x70098000>;
def VSLTI_BU : LSX2RI5_VVI<0x72880000>;
def VSLTI_HU : LSX2RI5_VVI<0x72888000>;
def VSLTI_WU : LSX2RI5_VVI<0x72890000>;
def VSLTI_DU : LSX2RI5_VVI<0x72898000>;

def VFCMP_CAF_S : LSX3R_VVV<0x0c500000>;
def VFCMP_SAF_S : LSX3R_VVV<0x0c508000>;
def VFCMP_CLT_S : LSX3R_VVV<0x0c510000>;
def VFCMP_SLT_S : LSX3R_VVV<0x0c518000>;
def VFCMP_CEQ_S : LSX3R_VVV<0x0c520000>;
def VFCMP_SEQ_S : LSX3R_VVV<0x0c528000>;
def VFCMP_CLE_S : LSX3R_VVV<0x0c530000>;
def VFCMP_SLE_S : LSX3R_VVV<0x0c538000>;
def VFCMP_CUN_S : LSX3R_VVV<0x0c540000>;
def VFCMP_SUN_S : LSX3R_VVV<0x0c548000>;
def VFCMP_CULT_S : LSX3R_VVV<0x0c550000>;
def VFCMP_SULT_S : LSX3R_VVV<0x0c558000>;
def VFCMP_CUEQ_S : LSX3R_VVV<0x0c560000>;
def VFCMP_SUEQ_S : LSX3R_VVV<0x0c568000>;
def VFCMP_CULE_S : LSX3R_VVV<0x0c570000>;
def VFCMP_SULE_S : LSX3R_VVV<0x0c578000>;
def VFCMP_CNE_S : LSX3R_VVV<0x0c580000>;
def VFCMP_SNE_S : LSX3R_VVV<0x0c588000>;
def VFCMP_COR_S : LSX3R_VVV<0x0c5a0000>;
def VFCMP_SOR_S : LSX3R_VVV<0x0c5a8000>;
def VFCMP_CUNE_S : LSX3R_VVV<0x0c5c0000>;
def VFCMP_SUNE_S : LSX3R_VVV<0x0c5c8000>;

def VFCMP_CAF_D : LSX3R_VVV<0x0c600000>;
def VFCMP_SAF_D : LSX3R_VVV<0x0c608000>;
def VFCMP_CLT_D : LSX3R_VVV<0x0c610000>;
def VFCMP_SLT_D : LSX3R_VVV<0x0c618000>;
def VFCMP_CEQ_D : LSX3R_VVV<0x0c620000>;
def VFCMP_SEQ_D : LSX3R_VVV<0x0c628000>;
def VFCMP_CLE_D : LSX3R_VVV<0x0c630000>;
def VFCMP_SLE_D : LSX3R_VVV<0x0c638000>;
def VFCMP_CUN_D : LSX3R_VVV<0x0c640000>;
def VFCMP_SUN_D : LSX3R_VVV<0x0c648000>;
def VFCMP_CULT_D : LSX3R_VVV<0x0c650000>;
def VFCMP_SULT_D : LSX3R_VVV<0x0c658000>;
def VFCMP_CUEQ_D : LSX3R_VVV<0x0c660000>;
def VFCMP_SUEQ_D : LSX3R_VVV<0x0c668000>;
def VFCMP_CULE_D : LSX3R_VVV<0x0c670000>;
def VFCMP_SULE_D : LSX3R_VVV<0x0c678000>;
def VFCMP_CNE_D : LSX3R_VVV<0x0c680000>;
def VFCMP_SNE_D : LSX3R_VVV<0x0c688000>;
def VFCMP_COR_D : LSX3R_VVV<0x0c6a0000>;
def VFCMP_SOR_D : LSX3R_VVV<0x0c6a8000>;
def VFCMP_CUNE_D : LSX3R_VVV<0x0c6c0000>;
def VFCMP_SUNE_D : LSX3R_VVV<0x0c6c8000>;

def VBITSEL_V : LSX4R_VVVV<0x0d100000>;

def VBITSELI_B : LSX2RI8_VVVI<0x73c40000>;

def VSETEQZ_V : LSX2R_CV<0x729c9800>;
def VSETNEZ_V : LSX2R_CV<0x729c9c00>;
def VSETANYEQZ_B : LSX2R_CV<0x729ca000>;
def VSETANYEQZ_H : LSX2R_CV<0x729ca400>;
def VSETANYEQZ_W : LSX2R_CV<0x729ca800>;
def VSETANYEQZ_D : LSX2R_CV<0x729cac00>;
def VSETALLNEZ_B : LSX2R_CV<0x729cb000>;
def VSETALLNEZ_H : LSX2R_CV<0x729cb400>;
def VSETALLNEZ_W : LSX2R_CV<0x729cb800>;
def VSETALLNEZ_D : LSX2R_CV<0x729cbc00>;

def VINSGR2VR_B : LSX2RI4_VVRI<0x72eb8000>;
def VINSGR2VR_H : LSX2RI3_VVRI<0x72ebc000>;
def VINSGR2VR_W : LSX2RI2_VVRI<0x72ebe000>;
def VINSGR2VR_D : LSX2RI1_VVRI<0x72ebf000>;
def VPICKVE2GR_B : LSX2RI4_RVI<0x72ef8000>;
def VPICKVE2GR_H : LSX2RI3_RVI<0x72efc000>;
def VPICKVE2GR_W : LSX2RI2_RVI<0x72efe000>;
def VPICKVE2GR_D : LSX2RI1_RVI<0x72eff000>;
def VPICKVE2GR_BU : LSX2RI4_RVI<0x72f38000>;
def VPICKVE2GR_HU : LSX2RI3_RVI<0x72f3c000>;
def VPICKVE2GR_WU : LSX2RI2_RVI<0x72f3e000>;
def VPICKVE2GR_DU : LSX2RI1_RVI<0x72f3f000>;

def VREPLGR2VR_B : LSX2R_VR<0x729f0000>;
def VREPLGR2VR_H : LSX2R_VR<0x729f0400>;
def VREPLGR2VR_W : LSX2R_VR<0x729f0800>;
def VREPLGR2VR_D : LSX2R_VR<0x729f0c00>;

def VREPLVE_B : LSX3R_VVR<0x71220000>;
def VREPLVE_H : LSX3R_VVR<0x71228000>;
def VREPLVE_W : LSX3R_VVR<0x71230000>;
def VREPLVE_D : LSX3R_VVR<0x71238000>;
def VREPLVEI_B : LSX2RI4_VVI<0x72f78000>;
def VREPLVEI_H : LSX2RI3_VVI<0x72f7c000>;
def VREPLVEI_W : LSX2RI2_VVI<0x72f7e000>;
def VREPLVEI_D : LSX2RI1_VVI<0x72f7f000>;

def VBSLL_V : LSX2RI5_VVI<0x728e0000>;
def VBSRL_V : LSX2RI5_VVI<0x728e8000>;

def VPACKEV_B : LSX3R_VVV<0x71160000>;
def VPACKEV_H : LSX3R_VVV<0x71168000>;
def VPACKEV_W : LSX3R_VVV<0x71170000>;
def VPACKEV_D : LSX3R_VVV<0x71178000>;
def VPACKOD_B : LSX3R_VVV<0x71180000>;
def VPACKOD_H : LSX3R_VVV<0x71188000>;
def VPACKOD_W : LSX3R_VVV<0x71190000>;
def VPACKOD_D : LSX3R_VVV<0x71198000>;

def VPICKEV_B : LSX3R_VVV<0x711e0000>;
def VPICKEV_H : LSX3R_VVV<0x711e8000>;
def VPICKEV_W : LSX3R_VVV<0x711f0000>;
def VPICKEV_D : LSX3R_VVV<0x711f8000>;
def VPICKOD_B : LSX3R_VVV<0x71200000>;
def VPICKOD_H : LSX3R_VVV<0x71208000>;
def VPICKOD_W : LSX3R_VVV<0x71210000>;
def VPICKOD_D : LSX3R_VVV<0x71218000>;

def VILVL_B : LSX3R_VVV<0x711a0000>;
def VILVL_H : LSX3R_VVV<0x711a8000>;
def VILVL_W : LSX3R_VVV<0x711b0000>;
def VILVL_D : LSX3R_VVV<0x711b8000>;
def VILVH_B : LSX3R_VVV<0x711c0000>;
def VILVH_H : LSX3R_VVV<0x711c8000>;
def VILVH_W : LSX3R_VVV<0x711d0000>;
def VILVH_D : LSX3R_VVV<0x711d8000>;

def VSHUF_B : LSX4R_VVVV<0x0d500000>;

def VSHUF_H : LSX3R_VVVV<0x717a8000>;
def VSHUF_W : LSX3R_VVVV<0x717b0000>;
def VSHUF_D : LSX3R_VVVV<0x717b8000>;

def VSHUF4I_B : LSX2RI8_VVI<0x73900000>;
def VSHUF4I_H : LSX2RI8_VVI<0x73940000>;
def VSHUF4I_W : LSX2RI8_VVI<0x73980000>;
def VSHUF4I_D : LSX2RI8_VVVI<0x739c0000>;

def VPERMI_W : LSX2RI8_VVVI<0x73e40000>;

def VEXTRINS_D : LSX2RI8_VVVI<0x73800000>;
def VEXTRINS_W : LSX2RI8_VVVI<0x73840000>;
def VEXTRINS_H : LSX2RI8_VVVI<0x73880000>;
def VEXTRINS_B : LSX2RI8_VVVI<0x738c0000>;
} // mayLoad = 0, mayStore = 0

let mayLoad = 1, mayStore = 0 in {
def VLD : LSX2RI12_Load<0x2c000000>;
def VLDX : LSX3R_Load<0x38400000>;

def VLDREPL_B : LSX2RI12_Load<0x30800000>;
def VLDREPL_H : LSX2RI11_Load<0x30400000>;
def VLDREPL_W : LSX2RI10_Load<0x30200000>;
def VLDREPL_D : LSX2RI9_Load<0x30100000>;
} // mayLoad = 1, mayStore = 0

let mayLoad = 0, mayStore = 1 in {
def VST : LSX2RI12_Store<0x2c400000>;
def VSTX : LSX3R_Store<0x38440000>;

def VSTELM_B : LSX2RI8I4_VRII<0x31800000>;
def VSTELM_H : LSX2RI8I3_VRII<0x31400000, simm8_lsl1>;
def VSTELM_W : LSX2RI8I2_VRII<0x31200000, simm8_lsl2>;
def VSTELM_D : LSX2RI8I1_VRII<0x31100000, simm8_lsl3>;
} // mayLoad = 0, mayStore = 1

} // hasSideEffects = 0, Predicates = [HasExtLSX]

/// Pseudo-instructions

let Predicates = [HasExtLSX] in {

let hasSideEffects = 0, mayLoad = 0, mayStore = 0, isCodeGenOnly = 0,
    isAsmParserOnly = 1 in {
def PseudoVREPLI_B : Pseudo<(outs LSX128:$vd), (ins simm10:$imm), [],
                            "vrepli.b", "$vd, $imm">;
def PseudoVREPLI_H : Pseudo<(outs LSX128:$vd), (ins simm10:$imm), [],
                            "vrepli.h", "$vd, $imm">;
def PseudoVREPLI_W : Pseudo<(outs LSX128:$vd), (ins simm10:$imm), [],
                            "vrepli.w", "$vd, $imm">;
def PseudoVREPLI_D : Pseudo<(outs LSX128:$vd), (ins simm10:$imm), [],
                            "vrepli.d", "$vd, $imm">;
}

def PseudoVBNZ_B : VecCond<loongarch_vall_nonzero, v16i8>;
def PseudoVBNZ_H : VecCond<loongarch_vall_nonzero, v8i16>;
def PseudoVBNZ_W : VecCond<loongarch_vall_nonzero, v4i32>;
def PseudoVBNZ_D : VecCond<loongarch_vall_nonzero, v2i64>;
def PseudoVBNZ : VecCond<loongarch_vany_nonzero, v16i8>;

def PseudoVBZ_B : VecCond<loongarch_vall_zero, v16i8>;
def PseudoVBZ_H : VecCond<loongarch_vall_zero, v8i16>;
def PseudoVBZ_W : VecCond<loongarch_vall_zero, v4i32>;
def PseudoVBZ_D : VecCond<loongarch_vall_zero, v2i64>;
def PseudoVBZ : VecCond<loongarch_vany_zero, v16i8>;

} // Predicates = [HasExtLSX]

multiclass PatVr<SDPatternOperator OpNode, string Inst> {
  def : Pat<(v16i8 (OpNode (v16i8 LSX128:$vj))),
            (!cast<LAInst>(Inst#"_B") LSX128:$vj)>;
  def : Pat<(v8i16 (OpNode (v8i16 LSX128:$vj))),
            (!cast<LAInst>(Inst#"_H") LSX128:$vj)>;
  def : Pat<(v4i32 (OpNode (v4i32 LSX128:$vj))),
            (!cast<LAInst>(Inst#"_W") LSX128:$vj)>;
  def : Pat<(v2i64 (OpNode (v2i64 LSX128:$vj))),
            (!cast<LAInst>(Inst#"_D") LSX128:$vj)>;
}

multiclass PatVrF<SDPatternOperator OpNode, string Inst> {
  def : Pat<(v4f32 (OpNode (v4f32 LSX128:$vj))),
            (!cast<LAInst>(Inst#"_S") LSX128:$vj)>;
  def : Pat<(v2f64 (OpNode (v2f64 LSX128:$vj))),
            (!cast<LAInst>(Inst#"_D") LSX128:$vj)>;
}

multiclass PatVrVr<SDPatternOperator OpNode, string Inst> {
  def : Pat<(OpNode (v16i8 LSX128:$vj), (v16i8 LSX128:$vk)),
            (!cast<LAInst>(Inst#"_B") LSX128:$vj, LSX128:$vk)>;
  def : Pat<(OpNode (v8i16 LSX128:$vj), (v8i16 LSX128:$vk)),
            (!cast<LAInst>(Inst#"_H") LSX128:$vj, LSX128:$vk)>;
  def : Pat<(OpNode (v4i32 LSX128:$vj), (v4i32 LSX128:$vk)),
            (!cast<LAInst>(Inst#"_W") LSX128:$vj, LSX128:$vk)>;
  def : Pat<(OpNode (v2i64 LSX128:$vj), (v2i64 LSX128:$vk)),
            (!cast<LAInst>(Inst#"_D") LSX128:$vj, LSX128:$vk)>;
}

multiclass PatVrVrF<SDPatternOperator OpNode, string Inst> {
  def : Pat<(OpNode (v4f32 LSX128:$vj), (v4f32 LSX128:$vk)),
            (!cast<LAInst>(Inst#"_S") LSX128:$vj, LSX128:$vk)>;
  def : Pat<(OpNode (v2f64 LSX128:$vj), (v2f64 LSX128:$vk)),
            (!cast<LAInst>(Inst#"_D") LSX128:$vj, LSX128:$vk)>;
}

multiclass PatVrVrU<SDPatternOperator OpNode, string Inst> {
  def : Pat<(OpNode (v16i8 LSX128:$vj), (v16i8 LSX128:$vk)),
            (!cast<LAInst>(Inst#"_BU") LSX128:$vj, LSX128:$vk)>;
  def : Pat<(OpNode (v8i16 LSX128:$vj), (v8i16 LSX128:$vk)),
            (!cast<LAInst>(Inst#"_HU") LSX128:$vj, LSX128:$vk)>;
  def : Pat<(OpNode (v4i32 LSX128:$vj), (v4i32 LSX128:$vk)),
            (!cast<LAInst>(Inst#"_WU") LSX128:$vj, LSX128:$vk)>;
  def : Pat<(OpNode (v2i64 LSX128:$vj), (v2i64 LSX128:$vk)),
            (!cast<LAInst>(Inst#"_DU") LSX128:$vj, LSX128:$vk)>;
}

multiclass PatVrSimm5<SDPatternOperator OpNode, string Inst> {
  def : Pat<(OpNode (v16i8 LSX128:$vj), (v16i8 (SplatPat_simm5 simm5:$imm))),
            (!cast<LAInst>(Inst#"_B") LSX128:$vj, simm5:$imm)>;
  def : Pat<(OpNode (v8i16 LSX128:$vj), (v8i16 (SplatPat_simm5 simm5:$imm))),
            (!cast<LAInst>(Inst#"_H") LSX128:$vj, simm5:$imm)>;
  def : Pat<(OpNode (v4i32 LSX128:$vj), (v4i32 (SplatPat_simm5 simm5:$imm))),
            (!cast<LAInst>(Inst#"_W") LSX128:$vj, simm5:$imm)>;
  def : Pat<(OpNode (v2i64 LSX128:$vj), (v2i64 (SplatPat_simm5 simm5:$imm))),
            (!cast<LAInst>(Inst#"_D") LSX128:$vj, simm5:$imm)>;
}

multiclass PatVrUimm5<SDPatternOperator OpNode, string Inst> {
  def : Pat<(OpNode (v16i8 LSX128:$vj), (v16i8 (SplatPat_uimm5 uimm5:$imm))),
            (!cast<LAInst>(Inst#"_BU") LSX128:$vj, uimm5:$imm)>;
  def : Pat<(OpNode (v8i16 LSX128:$vj), (v8i16 (SplatPat_uimm5 uimm5:$imm))),
            (!cast<LAInst>(Inst#"_HU") LSX128:$vj, uimm5:$imm)>;
  def : Pat<(OpNode (v4i32 LSX128:$vj), (v4i32 (SplatPat_uimm5 uimm5:$imm))),
            (!cast<LAInst>(Inst#"_WU") LSX128:$vj, uimm5:$imm)>;
  def : Pat<(OpNode (v2i64 LSX128:$vj), (v2i64 (SplatPat_uimm5 uimm5:$imm))),
            (!cast<LAInst>(Inst#"_DU") LSX128:$vj, uimm5:$imm)>;
}

multiclass PatVrVrVr<SDPatternOperator OpNode, string Inst> {
  def : Pat<(OpNode (v16i8 LSX128:$vd), (v16i8 LSX128:$vj), (v16i8 LSX128:$vk)),
            (!cast<LAInst>(Inst#"_B") LSX128:$vd, LSX128:$vj, LSX128:$vk)>;
  def : Pat<(OpNode (v8i16 LSX128:$vd), (v8i16 LSX128:$vj), (v8i16 LSX128:$vk)),
            (!cast<LAInst>(Inst#"_H") LSX128:$vd, LSX128:$vj, LSX128:$vk)>;
  def : Pat<(OpNode (v4i32 LSX128:$vd), (v4i32 LSX128:$vj), (v4i32 LSX128:$vk)),
            (!cast<LAInst>(Inst#"_W") LSX128:$vd, LSX128:$vj, LSX128:$vk)>;
  def : Pat<(OpNode (v2i64 LSX128:$vd), (v2i64 LSX128:$vj), (v2i64 LSX128:$vk)),
            (!cast<LAInst>(Inst#"_D") LSX128:$vd, LSX128:$vj, LSX128:$vk)>;
}

multiclass PatShiftVrVr<SDPatternOperator OpNode, string Inst> {
  def : Pat<(OpNode (v16i8 LSX128:$vj), (and vsplati8_imm_eq_7,
                                             (v16i8 LSX128:$vk))),
            (!cast<LAInst>(Inst#"_B") LSX128:$vj, LSX128:$vk)>;
  def : Pat<(OpNode (v8i16 LSX128:$vj), (and vsplati16_imm_eq_15,
                                             (v8i16 LSX128:$vk))),
            (!cast<LAInst>(Inst#"_H") LSX128:$vj, LSX128:$vk)>;
  def : Pat<(OpNode (v4i32 LSX128:$vj), (and vsplati32_imm_eq_31,
                                             (v4i32 LSX128:$vk))),
            (!cast<LAInst>(Inst#"_W") LSX128:$vj, LSX128:$vk)>;
  def : Pat<(OpNode (v2i64 LSX128:$vj), (and vsplati64_imm_eq_63,
                                             (v2i64 LSX128:$vk))),
            (!cast<LAInst>(Inst#"_D") LSX128:$vj, LSX128:$vk)>;
}

multiclass PatShiftVrUimm<SDPatternOperator OpNode, string Inst> {
  def : Pat<(OpNode (v16i8 LSX128:$vj), (v16i8 (SplatPat_uimm3 uimm3:$imm))),
            (!cast<LAInst>(Inst#"_B") LSX128:$vj, uimm3:$imm)>;
  def : Pat<(OpNode (v8i16 LSX128:$vj), (v8i16 (SplatPat_uimm4 uimm4:$imm))),
            (!cast<LAInst>(Inst#"_H") LSX128:$vj, uimm4:$imm)>;
  def : Pat<(OpNode (v4i32 LSX128:$vj), (v4i32 (SplatPat_uimm5 uimm5:$imm))),
            (!cast<LAInst>(Inst#"_W") LSX128:$vj, uimm5:$imm)>;
  def : Pat<(OpNode (v2i64 LSX128:$vj), (v2i64 (SplatPat_uimm6 uimm6:$imm))),
            (!cast<LAInst>(Inst#"_D") LSX128:$vj, uimm6:$imm)>;
}

multiclass PatCCVrSimm5<CondCode CC, string Inst> {
  def : Pat<(v16i8 (setcc (v16i8 LSX128:$vj),
                          (v16i8 (SplatPat_simm5 simm5:$imm)), CC)),
            (!cast<LAInst>(Inst#"_B") LSX128:$vj, simm5:$imm)>;
  def : Pat<(v8i16 (setcc (v8i16 LSX128:$vj),
                          (v8i16 (SplatPat_simm5 simm5:$imm)), CC)),
            (!cast<LAInst>(Inst#"_H") LSX128:$vj, simm5:$imm)>;
  def : Pat<(v4i32 (setcc (v4i32 LSX128:$vj),
                          (v4i32 (SplatPat_simm5 simm5:$imm)), CC)),
            (!cast<LAInst>(Inst#"_W") LSX128:$vj, simm5:$imm)>;
  def : Pat<(v2i64 (setcc (v2i64 LSX128:$vj),
                          (v2i64 (SplatPat_simm5 simm5:$imm)), CC)),
            (!cast<LAInst>(Inst#"_D") LSX128:$vj, simm5:$imm)>;
}

multiclass PatCCVrUimm5<CondCode CC, string Inst> {
  def : Pat<(v16i8 (setcc (v16i8 LSX128:$vj),
                          (v16i8 (SplatPat_uimm5 uimm5:$imm)), CC)),
            (!cast<LAInst>(Inst#"_BU") LSX128:$vj, uimm5:$imm)>;
  def : Pat<(v8i16 (setcc (v8i16 LSX128:$vj),
                          (v8i16 (SplatPat_uimm5 uimm5:$imm)), CC)),
            (!cast<LAInst>(Inst#"_HU") LSX128:$vj, uimm5:$imm)>;
  def : Pat<(v4i32 (setcc (v4i32 LSX128:$vj),
                          (v4i32 (SplatPat_uimm5 uimm5:$imm)), CC)),
            (!cast<LAInst>(Inst#"_WU") LSX128:$vj, uimm5:$imm)>;
  def : Pat<(v2i64 (setcc (v2i64 LSX128:$vj),
                          (v2i64 (SplatPat_uimm5 uimm5:$imm)), CC)),
            (!cast<LAInst>(Inst#"_DU") LSX128:$vj, uimm5:$imm)>;
}

multiclass PatCCVrVr<CondCode CC, string Inst> {
  def : Pat<(v16i8 (setcc (v16i8 LSX128:$vj), (v16i8 LSX128:$vk), CC)),
            (!cast<LAInst>(Inst#"_B") LSX128:$vj, LSX128:$vk)>;
  def : Pat<(v8i16 (setcc (v8i16 LSX128:$vj), (v8i16 LSX128:$vk), CC)),
            (!cast<LAInst>(Inst#"_H") LSX128:$vj, LSX128:$vk)>;
  def : Pat<(v4i32 (setcc (v4i32 LSX128:$vj), (v4i32 LSX128:$vk), CC)),
            (!cast<LAInst>(Inst#"_W") LSX128:$vj, LSX128:$vk)>;
  def : Pat<(v2i64 (setcc (v2i64 LSX128:$vj), (v2i64 LSX128:$vk), CC)),
            (!cast<LAInst>(Inst#"_D") LSX128:$vj, LSX128:$vk)>;
}

multiclass PatCCVrVrU<CondCode CC, string Inst> {
  def : Pat<(v16i8 (setcc (v16i8 LSX128:$vj), (v16i8 LSX128:$vk), CC)),
            (!cast<LAInst>(Inst#"_BU") LSX128:$vj, LSX128:$vk)>;
  def : Pat<(v8i16 (setcc (v8i16 LSX128:$vj), (v8i16 LSX128:$vk), CC)),
            (!cast<LAInst>(Inst#"_HU") LSX128:$vj, LSX128:$vk)>;
  def : Pat<(v4i32 (setcc (v4i32 LSX128:$vj), (v4i32 LSX128:$vk), CC)),
            (!cast<LAInst>(Inst#"_WU") LSX128:$vj, LSX128:$vk)>;
  def : Pat<(v2i64 (setcc (v2i64 LSX128:$vj), (v2i64 LSX128:$vk), CC)),
            (!cast<LAInst>(Inst#"_DU") LSX128:$vj, LSX128:$vk)>;
}

multiclass PatCCVrVrF<CondCode CC, string Inst> {
  def : Pat<(v4i32 (setcc (v4f32 LSX128:$vj), (v4f32 LSX128:$vk), CC)),
            (!cast<LAInst>(Inst#"_S") LSX128:$vj, LSX128:$vk)>;
  def : Pat<(v2i64 (setcc (v2f64 LSX128:$vj), (v2f64 LSX128:$vk), CC)),
            (!cast<LAInst>(Inst#"_D") LSX128:$vj, LSX128:$vk)>;
}

let Predicates = [HasExtLSX] in {

// VADD_{B/H/W/D}
defm : PatVrVr<add, "VADD">;
// VSUB_{B/H/W/D}
defm : PatVrVr<sub, "VSUB">;

// VADDI_{B/H/W/D}U
defm : PatVrUimm5<add, "VADDI">;
// VSUBI_{B/H/W/D}U
defm : PatVrUimm5<sub, "VSUBI">;

// VNEG_{B/H/W/D}
def : Pat<(sub immAllZerosV, (v16i8 LSX128:$vj)), (VNEG_B LSX128:$vj)>;
def : Pat<(sub immAllZerosV, (v8i16 LSX128:$vj)), (VNEG_H LSX128:$vj)>;
def : Pat<(sub immAllZerosV, (v4i32 LSX128:$vj)), (VNEG_W LSX128:$vj)>;
def : Pat<(sub immAllZerosV, (v2i64 LSX128:$vj)), (VNEG_D LSX128:$vj)>;

// VMAX[I]_{B/H/W/D}[U]
defm : PatVrVr<smax, "VMAX">;
defm : PatVrVrU<umax, "VMAX">;
defm : PatVrSimm5<smax, "VMAXI">;
defm : PatVrUimm5<umax, "VMAXI">;

// VMIN[I]_{B/H/W/D}[U]
defm : PatVrVr<smin, "VMIN">;
defm : PatVrVrU<umin, "VMIN">;
defm : PatVrSimm5<smin, "VMINI">;
defm : PatVrUimm5<umin, "VMINI">;

// VMUL_{B/H/W/D}
defm : PatVrVr<mul, "VMUL">;

// VMUH_{B/H/W/D}[U]
defm : PatVrVr<mulhs, "VMUH">;
defm : PatVrVrU<mulhu, "VMUH">;

// VMADD_{B/H/W/D}
defm : PatVrVrVr<muladd, "VMADD">;
// VMSUB_{B/H/W/D}
defm : PatVrVrVr<mulsub, "VMSUB">;

// VDIV_{B/H/W/D}[U]
defm : PatVrVr<sdiv, "VDIV">;
defm : PatVrVrU<udiv, "VDIV">;

// VMOD_{B/H/W/D}[U]
defm : PatVrVr<srem, "VMOD">;
defm : PatVrVrU<urem, "VMOD">;

// VAND_V
foreach vt = [v16i8, v8i16, v4i32, v2i64] in
def : Pat<(and (vt LSX128:$vj), (vt LSX128:$vk)),
          (VAND_V LSX128:$vj, LSX128:$vk)>;
// VOR_V
foreach vt = [v16i8, v8i16, v4i32, v2i64] in
def : Pat<(or (vt LSX128:$vj), (vt LSX128:$vk)),
          (VOR_V LSX128:$vj, LSX128:$vk)>;
// VXOR_V
foreach vt = [v16i8, v8i16, v4i32, v2i64] in
def : Pat<(xor (vt LSX128:$vj), (vt LSX128:$vk)),
          (VXOR_V LSX128:$vj, LSX128:$vk)>;
// VNOR_V
foreach vt = [v16i8, v8i16, v4i32, v2i64] in
def : Pat<(vnot (or (vt LSX128:$vj), (vt LSX128:$vk))),
          (VNOR_V LSX128:$vj, LSX128:$vk)>;

// VANDI_B
def : Pat<(and (v16i8 LSX128:$vj), (v16i8 (SplatPat_uimm8 uimm8:$imm))),
          (VANDI_B LSX128:$vj, uimm8:$imm)>;
// VORI_B
def : Pat<(or (v16i8 LSX128:$vj), (v16i8 (SplatPat_uimm8 uimm8:$imm))),
          (VORI_B LSX128:$vj, uimm8:$imm)>;

// VXORI_B
def : Pat<(xor (v16i8 LSX128:$vj), (v16i8 (SplatPat_uimm8 uimm8:$imm))),
          (VXORI_B LSX128:$vj, uimm8:$imm)>;

// VSLL[I]_{B/H/W/D}
defm : PatVrVr<shl, "VSLL">;
defm : PatShiftVrVr<shl, "VSLL">;
defm : PatShiftVrUimm<shl, "VSLLI">;

// VSRL[I]_{B/H/W/D}
defm : PatVrVr<srl, "VSRL">;
defm : PatShiftVrVr<srl, "VSRL">;
defm : PatShiftVrUimm<srl, "VSRLI">;

// VSRA[I]_{B/H/W/D}
defm : PatVrVr<sra, "VSRA">;
defm : PatShiftVrVr<sra, "VSRA">;
defm : PatShiftVrUimm<sra, "VSRAI">;

// VCLZ_{B/H/W/D}
defm : PatVr<ctlz, "VCLZ">;

// VPCNT_{B/H/W/D}
defm : PatVr<ctpop, "VPCNT">;

// VBITCLR_{B/H/W/D}
def : Pat<(and v16i8:$vj, (vnot (shl vsplat_imm_eq_1, v16i8:$vk))),
          (v16i8 (VBITCLR_B v16i8:$vj, v16i8:$vk))>;
def : Pat<(and v8i16:$vj, (vnot (shl vsplat_imm_eq_1, v8i16:$vk))),
          (v8i16 (VBITCLR_H v8i16:$vj, v8i16:$vk))>;
def : Pat<(and v4i32:$vj, (vnot (shl vsplat_imm_eq_1, v4i32:$vk))),
          (v4i32 (VBITCLR_W v4i32:$vj, v4i32:$vk))>;
def : Pat<(and v2i64:$vj, (vnot (shl vsplat_imm_eq_1, v2i64:$vk))),
          (v2i64 (VBITCLR_D v2i64:$vj, v2i64:$vk))>;
def : Pat<(and v16i8:$vj, (vnot (shl vsplat_imm_eq_1,
                                     (vsplati8imm7 v16i8:$vk)))),
          (v16i8 (VBITCLR_B v16i8:$vj, v16i8:$vk))>;
def : Pat<(and v8i16:$vj, (vnot (shl vsplat_imm_eq_1,
                                     (vsplati16imm15 v8i16:$vk)))),
          (v8i16 (VBITCLR_H v8i16:$vj, v8i16:$vk))>;
def : Pat<(and v4i32:$vj, (vnot (shl vsplat_imm_eq_1,
                                     (vsplati32imm31 v4i32:$vk)))),
          (v4i32 (VBITCLR_W v4i32:$vj, v4i32:$vk))>;
def : Pat<(and v2i64:$vj, (vnot (shl vsplat_imm_eq_1,
                                     (vsplati64imm63 v2i64:$vk)))),
          (v2i64 (VBITCLR_D v2i64:$vj, v2i64:$vk))>;

// VBITCLRI_{B/H/W/D}
def : Pat<(and (v16i8 LSX128:$vj), (v16i8 (vsplat_uimm_inv_pow2 uimm3:$imm))),
          (VBITCLRI_B LSX128:$vj, uimm3:$imm)>;
def : Pat<(and (v8i16 LSX128:$vj), (v8i16 (vsplat_uimm_inv_pow2 uimm4:$imm))),
          (VBITCLRI_H LSX128:$vj, uimm4:$imm)>;
def : Pat<(and (v4i32 LSX128:$vj), (v4i32 (vsplat_uimm_inv_pow2 uimm5:$imm))),
          (VBITCLRI_W LSX128:$vj, uimm5:$imm)>;
def : Pat<(and (v2i64 LSX128:$vj), (v2i64 (vsplat_uimm_inv_pow2 uimm6:$imm))),
          (VBITCLRI_D LSX128:$vj, uimm6:$imm)>;

// VBITSET_{B/H/W/D}
def : Pat<(or v16i8:$vj, (shl vsplat_imm_eq_1, v16i8:$vk)),
          (v16i8 (VBITSET_B v16i8:$vj, v16i8:$vk))>;
def : Pat<(or v8i16:$vj, (shl vsplat_imm_eq_1, v8i16:$vk)),
          (v8i16 (VBITSET_H v8i16:$vj, v8i16:$vk))>;
def : Pat<(or v4i32:$vj, (shl vsplat_imm_eq_1, v4i32:$vk)),
          (v4i32 (VBITSET_W v4i32:$vj, v4i32:$vk))>;
def : Pat<(or v2i64:$vj, (shl vsplat_imm_eq_1, v2i64:$vk)),
          (v2i64 (VBITSET_D v2i64:$vj, v2i64:$vk))>;
def : Pat<(or v16i8:$vj, (shl vsplat_imm_eq_1, (vsplati8imm7 v16i8:$vk))),
          (v16i8 (VBITSET_B v16i8:$vj, v16i8:$vk))>;
def : Pat<(or v8i16:$vj, (shl vsplat_imm_eq_1, (vsplati16imm15 v8i16:$vk))),
          (v8i16 (VBITSET_H v8i16:$vj, v8i16:$vk))>;
def : Pat<(or v4i32:$vj, (shl vsplat_imm_eq_1, (vsplati32imm31 v4i32:$vk))),
          (v4i32 (VBITSET_W v4i32:$vj, v4i32:$vk))>;
def : Pat<(or v2i64:$vj, (shl vsplat_imm_eq_1, (vsplati64imm63 v2i64:$vk))),
          (v2i64 (VBITSET_D v2i64:$vj, v2i64:$vk))>;

// VBITSETI_{B/H/W/D}
def : Pat<(or (v16i8 LSX128:$vj), (v16i8 (vsplat_uimm_pow2 uimm3:$imm))),
          (VBITSETI_B LSX128:$vj, uimm3:$imm)>;
def : Pat<(or (v8i16 LSX128:$vj), (v8i16 (vsplat_uimm_pow2 uimm4:$imm))),
          (VBITSETI_H LSX128:$vj, uimm4:$imm)>;
def : Pat<(or (v4i32 LSX128:$vj), (v4i32 (vsplat_uimm_pow2 uimm5:$imm))),
          (VBITSETI_W LSX128:$vj, uimm5:$imm)>;
def : Pat<(or (v2i64 LSX128:$vj), (v2i64 (vsplat_uimm_pow2 uimm6:$imm))),
          (VBITSETI_D LSX128:$vj, uimm6:$imm)>;

// VBITREV_{B/H/W/D}
def : Pat<(xor v16i8:$vj, (shl vsplat_imm_eq_1, v16i8:$vk)),
          (v16i8 (VBITREV_B v16i8:$vj, v16i8:$vk))>;
def : Pat<(xor v8i16:$vj, (shl vsplat_imm_eq_1, v8i16:$vk)),
          (v8i16 (VBITREV_H v8i16:$vj, v8i16:$vk))>;
def : Pat<(xor v4i32:$vj, (shl vsplat_imm_eq_1, v4i32:$vk)),
          (v4i32 (VBITREV_W v4i32:$vj, v4i32:$vk))>;
def : Pat<(xor v2i64:$vj, (shl vsplat_imm_eq_1, v2i64:$vk)),
          (v2i64 (VBITREV_D v2i64:$vj, v2i64:$vk))>;
def : Pat<(xor v16i8:$vj, (shl vsplat_imm_eq_1, (vsplati8imm7 v16i8:$vk))),
          (v16i8 (VBITREV_B v16i8:$vj, v16i8:$vk))>;
def : Pat<(xor v8i16:$vj, (shl vsplat_imm_eq_1, (vsplati16imm15 v8i16:$vk))),
          (v8i16 (VBITREV_H v8i16:$vj, v8i16:$vk))>;
def : Pat<(xor v4i32:$vj, (shl vsplat_imm_eq_1, (vsplati32imm31 v4i32:$vk))),
          (v4i32 (VBITREV_W v4i32:$vj, v4i32:$vk))>;
def : Pat<(xor v2i64:$vj, (shl vsplat_imm_eq_1, (vsplati64imm63 v2i64:$vk))),
          (v2i64 (VBITREV_D v2i64:$vj, v2i64:$vk))>;

// VBITREVI_{B/H/W/D}
def : Pat<(xor (v16i8 LSX128:$vj), (v16i8 (vsplat_uimm_pow2 uimm3:$imm))),
          (VBITREVI_B LSX128:$vj, uimm3:$imm)>;
def : Pat<(xor (v8i16 LSX128:$vj), (v8i16 (vsplat_uimm_pow2 uimm4:$imm))),
          (VBITREVI_H LSX128:$vj, uimm4:$imm)>;
def : Pat<(xor (v4i32 LSX128:$vj), (v4i32 (vsplat_uimm_pow2 uimm5:$imm))),
          (VBITREVI_W LSX128:$vj, uimm5:$imm)>;
def : Pat<(xor (v2i64 LSX128:$vj), (v2i64 (vsplat_uimm_pow2 uimm6:$imm))),
          (VBITREVI_D LSX128:$vj, uimm6:$imm)>;

// VFADD_{S/D}
defm : PatVrVrF<fadd, "VFADD">;

// VFSUB_{S/D}
defm : PatVrVrF<fsub, "VFSUB">;

// VFMUL_{S/D}
defm : PatVrVrF<fmul, "VFMUL">;

// VFDIV_{S/D}
defm : PatVrVrF<fdiv, "VFDIV">;

// VFMADD_{S/D}
def : Pat<(fma v4f32:$vj, v4f32:$vk, v4f32:$va),
          (VFMADD_S v4f32:$vj, v4f32:$vk, v4f32:$va)>;
def : Pat<(fma v2f64:$vj, v2f64:$vk, v2f64:$va),
          (VFMADD_D v2f64:$vj, v2f64:$vk, v2f64:$va)>;

// VFMSUB_{S/D}
def : Pat<(fma v4f32:$vj, v4f32:$vk, (fneg v4f32:$va)),
          (VFMSUB_S v4f32:$vj, v4f32:$vk, v4f32:$va)>;
def : Pat<(fma v2f64:$vj, v2f64:$vk, (fneg v2f64:$va)),
          (VFMSUB_D v2f64:$vj, v2f64:$vk, v2f64:$va)>;

// VFNMADD_{S/D}
def : Pat<(fneg (fma v4f32:$vj, v4f32:$vk, v4f32:$va)),
          (VFNMADD_S v4f32:$vj, v4f32:$vk, v4f32:$va)>;
def : Pat<(fneg (fma v2f64:$vj, v2f64:$vk, v2f64:$va)),
          (VFNMADD_D v2f64:$vj, v2f64:$vk, v2f64:$va)>;
def : Pat<(fma_nsz (fneg v4f32:$vj), v4f32:$vk, (fneg v4f32:$va)),
          (VFNMADD_S v4f32:$vj, v4f32:$vk, v4f32:$va)>;
def : Pat<(fma_nsz (fneg v2f64:$vj), v2f64:$vk, (fneg v2f64:$va)),
          (VFNMADD_D v2f64:$vj, v2f64:$vk, v2f64:$va)>;

// VFNMSUB_{S/D}
def : Pat<(fneg (fma v4f32:$vj, v4f32:$vk, (fneg v4f32:$va))),
          (VFNMSUB_S v4f32:$vj, v4f32:$vk, v4f32:$va)>;
def : Pat<(fneg (fma v2f64:$vj, v2f64:$vk, (fneg v2f64:$va))),
          (VFNMSUB_D v2f64:$vj, v2f64:$vk, v2f64:$va)>;
def : Pat<(fma_nsz (fneg v4f32:$vj), v4f32:$vk, v4f32:$va),
          (VFNMSUB_S v4f32:$vj, v4f32:$vk, v4f32:$va)>;
def : Pat<(fma_nsz (fneg v2f64:$vj), v2f64:$vk, v2f64:$va),
          (VFNMSUB_D v2f64:$vj, v2f64:$vk, v2f64:$va)>;

// VFSQRT_{S/D}
defm : PatVrF<fsqrt, "VFSQRT">;

// VFRECIP_{S/D}
def : Pat<(fdiv vsplatf32_fpimm_eq_1, v4f32:$vj),
          (VFRECIP_S v4f32:$vj)>;
def : Pat<(fdiv vsplatf64_fpimm_eq_1, v2f64:$vj),
          (VFRECIP_D v2f64:$vj)>;

// VFRSQRT_{S/D}
def : Pat<(fdiv vsplatf32_fpimm_eq_1, (fsqrt v4f32:$vj)),
          (VFRSQRT_S v4f32:$vj)>;
def : Pat<(fdiv vsplatf64_fpimm_eq_1, (fsqrt v2f64:$vj)),
          (VFRSQRT_D v2f64:$vj)>;

// VSEQ[I]_{B/H/W/D}
defm : PatCCVrSimm5<SETEQ, "VSEQI">;
defm : PatCCVrVr<SETEQ, "VSEQ">;

// VSLE[I]_{B/H/W/D}[U]
defm : PatCCVrSimm5<SETLE, "VSLEI">;
defm : PatCCVrUimm5<SETULE, "VSLEI">;
defm : PatCCVrVr<SETLE, "VSLE">;
defm : PatCCVrVrU<SETULE, "VSLE">;

// VSLT[I]_{B/H/W/D}[U]
defm : PatCCVrSimm5<SETLT, "VSLTI">;
defm : PatCCVrUimm5<SETULT, "VSLTI">;
defm : PatCCVrVr<SETLT, "VSLT">;
defm : PatCCVrVrU<SETULT, "VSLT">;

// VFCMP.cond.{S/D}
defm : PatCCVrVrF<SETEQ, "VFCMP_CEQ">;
defm : PatCCVrVrF<SETOEQ, "VFCMP_CEQ">;
defm : PatCCVrVrF<SETUEQ, "VFCMP_CUEQ">;

defm : PatCCVrVrF<SETLE, "VFCMP_CLE">;
defm : PatCCVrVrF<SETOLE, "VFCMP_CLE">;
defm : PatCCVrVrF<SETULE, "VFCMP_CULE">;

defm : PatCCVrVrF<SETLT, "VFCMP_CLT">;
defm : PatCCVrVrF<SETOLT, "VFCMP_CLT">;
defm : PatCCVrVrF<SETULT, "VFCMP_CULT">;

defm : PatCCVrVrF<SETNE, "VFCMP_CNE">;
defm : PatCCVrVrF<SETONE, "VFCMP_CNE">;
defm : PatCCVrVrF<SETUNE, "VFCMP_CUNE">;

defm : PatCCVrVrF<SETO, "VFCMP_COR">;
defm : PatCCVrVrF<SETUO, "VFCMP_CUN">;

// VINSGR2VR_{B/H/W/D}
def : Pat<(vector_insert v16i8:$vd, GRLenVT:$rj, uimm4:$imm),
          (VINSGR2VR_B v16i8:$vd, GRLenVT:$rj, uimm4:$imm)>;
def : Pat<(vector_insert v8i16:$vd, GRLenVT:$rj, uimm3:$imm),
          (VINSGR2VR_H v8i16:$vd, GRLenVT:$rj, uimm3:$imm)>;
def : Pat<(vector_insert v4i32:$vd, GRLenVT:$rj, uimm2:$imm),
          (VINSGR2VR_W v4i32:$vd, GRLenVT:$rj, uimm2:$imm)>;
def : Pat<(vector_insert v2i64:$vd, GRLenVT:$rj, uimm1:$imm),
          (VINSGR2VR_D v2i64:$vd, GRLenVT:$rj, uimm1:$imm)>;

def : Pat<(vector_insert v4f32:$vd, FPR32:$fj, uimm2:$imm),
          (VINSGR2VR_W $vd, (COPY_TO_REGCLASS FPR32:$fj, GPR), uimm2:$imm)>;
def : Pat<(vector_insert v2f64:$vd, FPR64:$fj, uimm1:$imm),
          (VINSGR2VR_D $vd, (COPY_TO_REGCLASS FPR64:$fj, GPR), uimm1:$imm)>;

// VPICKVE2GR_{B/H/W}[U]
def : Pat<(loongarch_vpick_sext_elt v16i8:$vd, uimm4:$imm, i8),
          (VPICKVE2GR_B v16i8:$vd, uimm4:$imm)>;
def : Pat<(loongarch_vpick_sext_elt v8i16:$vd, uimm3:$imm, i16),
          (VPICKVE2GR_H v8i16:$vd, uimm3:$imm)>;
def : Pat<(loongarch_vpick_sext_elt v4i32:$vd, uimm2:$imm, i32),
          (VPICKVE2GR_W v4i32:$vd, uimm2:$imm)>;

def : Pat<(loongarch_vpick_zext_elt v16i8:$vd, uimm4:$imm, i8),
          (VPICKVE2GR_BU v16i8:$vd, uimm4:$imm)>;
def : Pat<(loongarch_vpick_zext_elt v8i16:$vd, uimm3:$imm, i16),
          (VPICKVE2GR_HU v8i16:$vd, uimm3:$imm)>;
def : Pat<(loongarch_vpick_zext_elt v4i32:$vd, uimm2:$imm, i32),
          (VPICKVE2GR_WU v4i32:$vd, uimm2:$imm)>;

// VREPLGR2VR_{B/H/W/D}
def : Pat<(lsxsplati8 GPR:$rj), (VREPLGR2VR_B GPR:$rj)>;
def : Pat<(lsxsplati16 GPR:$rj), (VREPLGR2VR_H GPR:$rj)>;
def : Pat<(lsxsplati32 GPR:$rj), (VREPLGR2VR_W GPR:$rj)>;
def : Pat<(lsxsplati64 GPR:$rj), (VREPLGR2VR_D GPR:$rj)>;

// VREPLVE_{B/H/W/D}
def : Pat<(loongarch_vreplve v16i8:$vj, GRLenVT:$rk),
          (VREPLVE_B v16i8:$vj, GRLenVT:$rk)>;
def : Pat<(loongarch_vreplve v8i16:$vj, GRLenVT:$rk),
          (VREPLVE_H v8i16:$vj, GRLenVT:$rk)>;
def : Pat<(loongarch_vreplve v4i32:$vj, GRLenVT:$rk),
          (VREPLVE_W v4i32:$vj, GRLenVT:$rk)>;
def : Pat<(loongarch_vreplve v2i64:$vj, GRLenVT:$rk),
          (VREPLVE_D v2i64:$vj, GRLenVT:$rk)>;

// VSHUF_{B/H/W/D}
def : Pat<(loongarch_vshuf v16i8:$va, v16i8:$vj, v16i8:$vk),
          (VSHUF_B v16i8:$vj, v16i8:$vk, v16i8:$va)>;
def : Pat<(loongarch_vshuf v8i16:$vd, v8i16:$vj, v8i16:$vk),
          (VSHUF_H v8i16:$vd, v8i16:$vj, v8i16:$vk)>;
def : Pat<(loongarch_vshuf v4i32:$vd, v4i32:$vj, v4i32:$vk),
          (VSHUF_W v4i32:$vd, v4i32:$vj, v4i32:$vk)>;
def : Pat<(loongarch_vshuf v2i64:$vd, v2i64:$vj, v2i64:$vk),
          (VSHUF_D v2i64:$vd, v2i64:$vj, v2i64:$vk)>;
def : Pat<(loongarch_vshuf v4i32:$vd, v4f32:$vj, v4f32:$vk),
          (VSHUF_W v4i32:$vd, v4f32:$vj, v4f32:$vk)>;
def : Pat<(loongarch_vshuf v2i64:$vd, v2f64:$vj, v2f64:$vk),
          (VSHUF_D v2i64:$vd, v2f64:$vj, v2f64:$vk)>;

// VPICKEV_{B/H/W/D}
def : Pat<(loongarch_vpickev v16i8:$vj, v16i8:$vk),
          (VPICKEV_B v16i8:$vj, v16i8:$vk)>;
def : Pat<(loongarch_vpickev v8i16:$vj, v8i16:$vk),
          (VPICKEV_H v8i16:$vj, v8i16:$vk)>;
def : Pat<(loongarch_vpickev v4i32:$vj, v4i32:$vk),
          (VPICKEV_W v4i32:$vj, v4i32:$vk)>;
def : Pat<(loongarch_vpickev v2i64:$vj, v2i64:$vk),
          (VPICKEV_D v2i64:$vj, v2i64:$vk)>;
def : Pat<(loongarch_vpickev v4f32:$vj, v4f32:$vk),
          (VPICKEV_W v4f32:$vj, v4f32:$vk)>;
def : Pat<(loongarch_vpickev v2f64:$vj, v2f64:$vk),
          (VPICKEV_D v2f64:$vj, v2f64:$vk)>;

// VPICKOD_{B/H/W/D}
def : Pat<(loongarch_vpickod v16i8:$vj, v16i8:$vk),
          (VPICKOD_B v16i8:$vj, v16i8:$vk)>;
def : Pat<(loongarch_vpickod v8i16:$vj, v8i16:$vk),
          (VPICKOD_H v8i16:$vj, v8i16:$vk)>;
def : Pat<(loongarch_vpickod v4i32:$vj, v4i32:$vk),
          (VPICKOD_W v4i32:$vj, v4i32:$vk)>;
def : Pat<(loongarch_vpickod v2i64:$vj, v2i64:$vk),
          (VPICKOD_D v2i64:$vj, v2i64:$vk)>;
def : Pat<(loongarch_vpickod v4f32:$vj, v4f32:$vk),
          (VPICKOD_W v4f32:$vj, v4f32:$vk)>;
def : Pat<(loongarch_vpickod v2f64:$vj, v2f64:$vk),
          (VPICKOD_D v2f64:$vj, v2f64:$vk)>;

// VPACKEV_{B/H/W/D}
def : Pat<(loongarch_vpackev v16i8:$vj, v16i8:$vk),
          (VPACKEV_B v16i8:$vj, v16i8:$vk)>;
def : Pat<(loongarch_vpackev v8i16:$vj, v8i16:$vk),
          (VPACKEV_H v8i16:$vj, v8i16:$vk)>;
def : Pat<(loongarch_vpackev v4i32:$vj, v4i32:$vk),
          (VPACKEV_W v4i32:$vj, v4i32:$vk)>;
def : Pat<(loongarch_vpackev v2i64:$vj, v2i64:$vk),
          (VPACKEV_D v2i64:$vj, v2i64:$vk)>;
def : Pat<(loongarch_vpackev v4f32:$vj, v4f32:$vk),
          (VPACKEV_W v4f32:$vj, v4f32:$vk)>;
def : Pat<(loongarch_vpackev v2f64:$vj, v2f64:$vk),
          (VPACKEV_D v2f64:$vj, v2f64:$vk)>;

// VPACKOD_{B/H/W/D}
def : Pat<(loongarch_vpackod v16i8:$vj, v16i8:$vk),
          (VPACKOD_B v16i8:$vj, v16i8:$vk)>;
def : Pat<(loongarch_vpackod v8i16:$vj, v8i16:$vk),
          (VPACKOD_H v8i16:$vj, v8i16:$vk)>;
def : Pat<(loongarch_vpackod v4i32:$vj, v4i32:$vk),
          (VPACKOD_W v4i32:$vj, v4i32:$vk)>;
def : Pat<(loongarch_vpackod v2i64:$vj, v2i64:$vk),
          (VPACKOD_D v2i64:$vj, v2i64:$vk)>;
def : Pat<(loongarch_vpackod v4f32:$vj, v4f32:$vk),
          (VPACKOD_W v4f32:$vj, v4f32:$vk)>;
def : Pat<(loongarch_vpackod v2f64:$vj, v2f64:$vk),
          (VPACKOD_D v2f64:$vj, v2f64:$vk)>;

// VILVL_{B/H/W/D}
def : Pat<(loongarch_vilvl v16i8:$vj, v16i8:$vk),
          (VILVL_B v16i8:$vj, v16i8:$vk)>;
def : Pat<(loongarch_vilvl v8i16:$vj, v8i16:$vk),
          (VILVL_H v8i16:$vj, v8i16:$vk)>;
def : Pat<(loongarch_vilvl v4i32:$vj, v4i32:$vk),
          (VILVL_W v4i32:$vj, v4i32:$vk)>;
def : Pat<(loongarch_vilvl v2i64:$vj, v2i64:$vk),
          (VILVL_D v2i64:$vj, v2i64:$vk)>;
def : Pat<(loongarch_vilvl v4f32:$vj, v4f32:$vk),
          (VILVL_W v4f32:$vj, v4f32:$vk)>;
def : Pat<(loongarch_vilvl v2f64:$vj, v2f64:$vk),
          (VILVL_D v2f64:$vj, v2f64:$vk)>;

// VILVH_{B/H/W/D}
def : Pat<(loongarch_vilvh v16i8:$vj, v16i8:$vk),
          (VILVH_B v16i8:$vj, v16i8:$vk)>;
def : Pat<(loongarch_vilvh v8i16:$vj, v8i16:$vk),
          (VILVH_H v8i16:$vj, v8i16:$vk)>;
def : Pat<(loongarch_vilvh v4i32:$vj, v4i32:$vk),
          (VILVH_W v4i32:$vj, v4i32:$vk)>;
def : Pat<(loongarch_vilvh v2i64:$vj, v2i64:$vk),
          (VILVH_D v2i64:$vj, v2i64:$vk)>;
def : Pat<(loongarch_vilvh v4f32:$vj, v4f32:$vk),
          (VILVH_W v4f32:$vj, v4f32:$vk)>;
def : Pat<(loongarch_vilvh v2f64:$vj, v2f64:$vk),
          (VILVH_D v2f64:$vj, v2f64:$vk)>;

// VSHUF4I_{B/H/W}
def : Pat<(loongarch_vshuf4i v16i8:$vj, immZExt8:$ui8),
          (VSHUF4I_B v16i8:$vj, immZExt8:$ui8)>;
def : Pat<(loongarch_vshuf4i v8i16:$vj, immZExt8:$ui8),
        (VSHUF4I_H v8i16:$vj, immZExt8:$ui8)>;
def : Pat<(loongarch_vshuf4i v4i32:$vj, immZExt8:$ui8),
        (VSHUF4I_W v4i32:$vj, immZExt8:$ui8)>;
def : Pat<(loongarch_vshuf4i v4f32:$vj, immZExt8:$ui8),
        (VSHUF4I_W v4f32:$vj, immZExt8:$ui8)>;

// VREPLVEI_{B/H/W/D}
def : Pat<(loongarch_vreplvei v16i8:$vj, immZExt4:$ui4),
          (VREPLVEI_B v16i8:$vj, immZExt4:$ui4)>;
def : Pat<(loongarch_vreplvei v8i16:$vj, immZExt3:$ui3),
        (VREPLVEI_H v8i16:$vj, immZExt3:$ui3)>;
def : Pat<(loongarch_vreplvei v4i32:$vj, immZExt2:$ui2),
        (VREPLVEI_W v4i32:$vj, immZExt2:$ui2)>;
def : Pat<(loongarch_vreplvei v2i64:$vj, immZExt1:$ui1),
        (VREPLVEI_D v2i64:$vj, immZExt1:$ui1)>;
def : Pat<(loongarch_vreplvei v4f32:$vj, immZExt2:$ui2),
        (VREPLVEI_W v4f32:$vj, immZExt2:$ui2)>;
def : Pat<(loongarch_vreplvei v2f64:$vj, immZExt1:$ui1),
        (VREPLVEI_D v2f64:$vj, immZExt1:$ui1)>;

// VREPLVEI_{W/D}
def : Pat<(lsxsplatf32 FPR32:$fj),
          (VREPLVEI_W (SUBREG_TO_REG (i64 0), FPR32:$fj, sub_32), 0)>;
def : Pat<(lsxsplatf64 FPR64:$fj),
          (VREPLVEI_D (SUBREG_TO_REG (i64 0), FPR64:$fj, sub_64), 0)>;

// Loads/Stores
foreach vt = [v16i8, v8i16, v4i32, v2i64, v4f32, v2f64] in {
  defm : LdPat<load, VLD, vt>;
  def  : RegRegLdPat<load, VLDX, vt>;
  defm : StPat<store, VST, LSX128, vt>;
  def  : RegRegStPat<store, VSTX, LSX128, vt>;
}

// Vector extraction with constant index.
def : Pat<(i64 (vector_extract v16i8:$vj, uimm4:$imm)),
          (VPICKVE2GR_B v16i8:$vj, uimm4:$imm)>;
def : Pat<(i64 (vector_extract v8i16:$vj, uimm3:$imm)),
          (VPICKVE2GR_H v8i16:$vj, uimm3:$imm)>;
def : Pat<(i64 (vector_extract v4i32:$vj, uimm2:$imm)),
          (VPICKVE2GR_W v4i32:$vj, uimm2:$imm)>;
def : Pat<(i64 (vector_extract v2i64:$vj, uimm1:$imm)),
          (VPICKVE2GR_D v2i64:$vj, uimm1:$imm)>;
def : Pat<(f32 (vector_extract v4f32:$vj, uimm2:$imm)),
          (f32 (EXTRACT_SUBREG (VREPLVEI_W v4f32:$vj, uimm2:$imm), sub_32))>;
def : Pat<(f64 (vector_extract v2f64:$vj, uimm1:$imm)),
          (f64 (EXTRACT_SUBREG (VREPLVEI_D v2f64:$vj, uimm1:$imm), sub_64))>;

// Vector extraction with variable index.
def : Pat<(i64 (vector_extract v16i8:$vj, i64:$rk)),
          (SRAI_W (COPY_TO_REGCLASS (f32 (EXTRACT_SUBREG (VREPLVE_B v16i8:$vj,
                                                                    i64:$rk),
                                                         sub_32)),
                                    GPR), (i64 24))>;
def : Pat<(i64 (vector_extract v8i16:$vj, i64:$rk)),
          (SRAI_W (COPY_TO_REGCLASS (f32 (EXTRACT_SUBREG (VREPLVE_H v8i16:$vj,
                                                                    i64:$rk),
                                                         sub_32)),
                                    GPR), (i64 16))>;
def : Pat<(i64 (vector_extract v4i32:$vj, i64:$rk)),
          (COPY_TO_REGCLASS (f32 (EXTRACT_SUBREG (VREPLVE_W v4i32:$vj, i64:$rk),
                                                 sub_32)),
                            GPR)>;
def : Pat<(i64 (vector_extract v2i64:$vj, i64:$rk)),
          (COPY_TO_REGCLASS (f64 (EXTRACT_SUBREG (VREPLVE_D v2i64:$vj, i64:$rk),
                                                 sub_64)),
                            GPR)>;
def : Pat<(f32 (vector_extract v4f32:$vj, i64:$rk)),
          (f32 (EXTRACT_SUBREG (VREPLVE_W v4f32:$vj, i64:$rk), sub_32))>;
def : Pat<(f64 (vector_extract v2f64:$vj, i64:$rk)),
          (f64 (EXTRACT_SUBREG (VREPLVE_D v2f64:$vj, i64:$rk), sub_64))>;

// vselect
def : Pat<(v16i8 (vselect LSX128:$vd, (v16i8 (SplatPat_uimm8 uimm8:$imm)),
                          LSX128:$vj)),
          (VBITSELI_B LSX128:$vd, LSX128:$vj, uimm8:$imm)>;
foreach vt = [v16i8, v8i16, v4i32, v2i64, v4f32, v2f64] in
  def  : Pat<(vt (vselect LSX128:$va, LSX128:$vk, LSX128:$vj)),
             (VBITSEL_V LSX128:$vj, LSX128:$vk, LSX128:$va)>;

// fneg
def : Pat<(fneg (v4f32 LSX128:$vj)), (VBITREVI_W LSX128:$vj, 31)>;
def : Pat<(fneg (v2f64 LSX128:$vj)), (VBITREVI_D LSX128:$vj, 63)>;

// VFFINT_{S_W/D_L}
def : Pat<(v4f32 (sint_to_fp v4i32:$vj)), (VFFINT_S_W v4i32:$vj)>;
def : Pat<(v2f64 (sint_to_fp v2i64:$vj)), (VFFINT_D_L v2i64:$vj)>;

// VFFINT_{S_WU/D_LU}
def : Pat<(v4f32 (uint_to_fp v4i32:$vj)), (VFFINT_S_WU v4i32:$vj)>;
def : Pat<(v2f64 (uint_to_fp v2i64:$vj)), (VFFINT_D_LU v2i64:$vj)>;

// VFTINTRZ_{W_S/L_D}
def : Pat<(v4i32 (fp_to_sint v4f32:$vj)), (VFTINTRZ_W_S v4f32:$vj)>;
def : Pat<(v2i64 (fp_to_sint v2f64:$vj)), (VFTINTRZ_L_D v2f64:$vj)>;

// VFTINTRZ_{W_SU/L_DU}
def : Pat<(v4i32 (fp_to_uint v4f32:$vj)), (VFTINTRZ_WU_S v4f32:$vj)>;
def : Pat<(v2i64 (fp_to_uint v2f64:$vj)), (VFTINTRZ_LU_D v2f64:$vj)>;

} // Predicates = [HasExtLSX]

/// Intrinsic pattern

class deriveLSXIntrinsic<string Inst> {
  Intrinsic ret = !cast<Intrinsic>(!tolower("int_loongarch_lsx_"#Inst));
}

let Predicates = [HasExtLSX] in {

// vty: v16i8/v8i16/v4i32/v2i64
// Pat<(Intrinsic vty:$vj, vty:$vk),
//     (LAInst vty:$vj, vty:$vk)>;
foreach Inst = ["VSADD_B", "VSADD_BU", "VSSUB_B", "VSSUB_BU",
                "VHADDW_H_B", "VHADDW_HU_BU", "VHSUBW_H_B", "VHSUBW_HU_BU",
                "VADDWEV_H_B", "VADDWOD_H_B", "VSUBWEV_H_B", "VSUBWOD_H_B",
                "VADDWEV_H_BU", "VADDWOD_H_BU", "VSUBWEV_H_BU", "VSUBWOD_H_BU",
                "VADDWEV_H_BU_B", "VADDWOD_H_BU_B",
                "VAVG_B", "VAVG_BU", "VAVGR_B", "VAVGR_BU",
                "VABSD_B", "VABSD_BU", "VADDA_B", "VMUH_B", "VMUH_BU",
                "VMULWEV_H_B", "VMULWOD_H_B", "VMULWEV_H_BU", "VMULWOD_H_BU",
                "VMULWEV_H_BU_B", "VMULWOD_H_BU_B", "VSIGNCOV_B",
                "VANDN_V", "VORN_V", "VROTR_B", "VSRLR_B", "VSRAR_B",
                "VSEQ_B", "VSLE_B", "VSLE_BU", "VSLT_B", "VSLT_BU",
                "VPACKEV_B", "VPACKOD_B", "VPICKEV_B", "VPICKOD_B",
                "VILVL_B", "VILVH_B"] in
  def : Pat<(deriveLSXIntrinsic<Inst>.ret
               (v16i8 LSX128:$vj), (v16i8 LSX128:$vk)),
            (!cast<LAInst>(Inst) LSX128:$vj, LSX128:$vk)>;
foreach Inst = ["VSADD_H", "VSADD_HU", "VSSUB_H", "VSSUB_HU",
                "VHADDW_W_H", "VHADDW_WU_HU", "VHSUBW_W_H", "VHSUBW_WU_HU",
                "VADDWEV_W_H", "VADDWOD_W_H", "VSUBWEV_W_H", "VSUBWOD_W_H",
                "VADDWEV_W_HU", "VADDWOD_W_HU", "VSUBWEV_W_HU", "VSUBWOD_W_HU",
                "VADDWEV_W_HU_H", "VADDWOD_W_HU_H",
                "VAVG_H", "VAVG_HU", "VAVGR_H", "VAVGR_HU",
                "VABSD_H", "VABSD_HU", "VADDA_H", "VMUH_H", "VMUH_HU",
                "VMULWEV_W_H", "VMULWOD_W_H", "VMULWEV_W_HU", "VMULWOD_W_HU",
                "VMULWEV_W_HU_H", "VMULWOD_W_HU_H", "VSIGNCOV_H", "VROTR_H",
                "VSRLR_H", "VSRAR_H", "VSRLN_B_H", "VSRAN_B_H", "VSRLRN_B_H",
                "VSRARN_B_H", "VSSRLN_B_H", "VSSRAN_B_H", "VSSRLN_BU_H",
                "VSSRAN_BU_H", "VSSRLRN_B_H", "VSSRARN_B_H", "VSSRLRN_BU_H",
                "VSSRARN_BU_H",
                "VSEQ_H", "VSLE_H", "VSLE_HU", "VSLT_H", "VSLT_HU",
                "VPACKEV_H", "VPACKOD_H", "VPICKEV_H", "VPICKOD_H",
                "VILVL_H", "VILVH_H"] in
  def : Pat<(deriveLSXIntrinsic<Inst>.ret
               (v8i16 LSX128:$vj), (v8i16 LSX128:$vk)),
            (!cast<LAInst>(Inst) LSX128:$vj, LSX128:$vk)>;
foreach Inst = ["VSADD_W", "VSADD_WU", "VSSUB_W", "VSSUB_WU",
                "VHADDW_D_W", "VHADDW_DU_WU", "VHSUBW_D_W", "VHSUBW_DU_WU",
                "VADDWEV_D_W", "VADDWOD_D_W", "VSUBWEV_D_W", "VSUBWOD_D_W",
                "VADDWEV_D_WU", "VADDWOD_D_WU", "VSUBWEV_D_WU", "VSUBWOD_D_WU",
                "VADDWEV_D_WU_W", "VADDWOD_D_WU_W",
                "VAVG_W", "VAVG_WU", "VAVGR_W", "VAVGR_WU",
                "VABSD_W", "VABSD_WU", "VADDA_W", "VMUH_W", "VMUH_WU",
                "VMULWEV_D_W", "VMULWOD_D_W", "VMULWEV_D_WU", "VMULWOD_D_WU",
                "VMULWEV_D_WU_W", "VMULWOD_D_WU_W", "VSIGNCOV_W", "VROTR_W",
                "VSRLR_W", "VSRAR_W", "VSRLN_H_W", "VSRAN_H_W", "VSRLRN_H_W",
                "VSRARN_H_W", "VSSRLN_H_W", "VSSRAN_H_W", "VSSRLN_HU_W",
                "VSSRAN_HU_W", "VSSRLRN_H_W", "VSSRARN_H_W", "VSSRLRN_HU_W",
                "VSSRARN_HU_W",
                "VSEQ_W", "VSLE_W", "VSLE_WU", "VSLT_W", "VSLT_WU",
                "VPACKEV_W", "VPACKOD_W", "VPICKEV_W", "VPICKOD_W",
                "VILVL_W", "VILVH_W"] in
  def : Pat<(deriveLSXIntrinsic<Inst>.ret
               (v4i32 LSX128:$vj), (v4i32 LSX128:$vk)),
            (!cast<LAInst>(Inst) LSX128:$vj, LSX128:$vk)>;
foreach Inst = ["VADD_Q", "VSUB_Q",
                "VSADD_D", "VSADD_DU", "VSSUB_D", "VSSUB_DU",
                "VHADDW_Q_D", "VHADDW_QU_DU", "VHSUBW_Q_D", "VHSUBW_QU_DU",
                "VADDWEV_Q_D", "VADDWOD_Q_D", "VSUBWEV_Q_D", "VSUBWOD_Q_D",
                "VADDWEV_Q_DU", "VADDWOD_Q_DU", "VSUBWEV_Q_DU", "VSUBWOD_Q_DU",
                "VADDWEV_Q_DU_D", "VADDWOD_Q_DU_D",
                "VAVG_D", "VAVG_DU", "VAVGR_D", "VAVGR_DU",
                "VABSD_D", "VABSD_DU", "VADDA_D", "VMUH_D", "VMUH_DU",
                "VMULWEV_Q_D", "VMULWOD_Q_D", "VMULWEV_Q_DU", "VMULWOD_Q_DU",
                "VMULWEV_Q_DU_D", "VMULWOD_Q_DU_D", "VSIGNCOV_D", "VROTR_D",
                "VSRLR_D", "VSRAR_D", "VSRLN_W_D", "VSRAN_W_D", "VSRLRN_W_D",
                "VSRARN_W_D", "VSSRLN_W_D", "VSSRAN_W_D", "VSSRLN_WU_D",
                "VSSRAN_WU_D", "VSSRLRN_W_D", "VSSRARN_W_D", "VSSRLRN_WU_D",
                "VSSRARN_WU_D", "VFFINT_S_L",
                "VSEQ_D", "VSLE_D", "VSLE_DU", "VSLT_D", "VSLT_DU",
                "VPACKEV_D", "VPACKOD_D", "VPICKEV_D", "VPICKOD_D",
                "VILVL_D", "VILVH_D"] in
  def : Pat<(deriveLSXIntrinsic<Inst>.ret
               (v2i64 LSX128:$vj), (v2i64 LSX128:$vk)),
            (!cast<LAInst>(Inst) LSX128:$vj, LSX128:$vk)>;

// vty: v16i8/v8i16/v4i32/v2i64
// Pat<(Intrinsic vty:$vd, vty:$vj, vty:$vk),
//     (LAInst vty:$vd, vty:$vj, vty:$vk)>;
foreach Inst = ["VMADDWEV_H_B", "VMADDWOD_H_B", "VMADDWEV_H_BU",
                "VMADDWOD_H_BU", "VMADDWEV_H_BU_B", "VMADDWOD_H_BU_B"] in
  def : Pat<(deriveLSXIntrinsic<Inst>.ret
               (v8i16 LSX128:$vd), (v16i8 LSX128:$vj), (v16i8 LSX128:$vk)),
            (!cast<LAInst>(Inst) LSX128:$vd, LSX128:$vj, LSX128:$vk)>;
foreach Inst = ["VMADDWEV_W_H", "VMADDWOD_W_H", "VMADDWEV_W_HU",
                "VMADDWOD_W_HU", "VMADDWEV_W_HU_H", "VMADDWOD_W_HU_H"] in
  def : Pat<(deriveLSXIntrinsic<Inst>.ret
               (v4i32 LSX128:$vd), (v8i16 LSX128:$vj), (v8i16 LSX128:$vk)),
            (!cast<LAInst>(Inst) LSX128:$vd, LSX128:$vj, LSX128:$vk)>;
foreach Inst = ["VMADDWEV_D_W", "VMADDWOD_D_W", "VMADDWEV_D_WU",
                "VMADDWOD_D_WU", "VMADDWEV_D_WU_W", "VMADDWOD_D_WU_W"] in
  def : Pat<(deriveLSXIntrinsic<Inst>.ret
               (v2i64 LSX128:$vd), (v4i32 LSX128:$vj), (v4i32 LSX128:$vk)),
            (!cast<LAInst>(Inst) LSX128:$vd, LSX128:$vj, LSX128:$vk)>;
foreach Inst = ["VMADDWEV_Q_D", "VMADDWOD_Q_D", "VMADDWEV_Q_DU",
                "VMADDWOD_Q_DU", "VMADDWEV_Q_DU_D", "VMADDWOD_Q_DU_D"] in
  def : Pat<(deriveLSXIntrinsic<Inst>.ret
               (v2i64 LSX128:$vd), (v2i64 LSX128:$vj), (v2i64 LSX128:$vk)),
            (!cast<LAInst>(Inst) LSX128:$vd, LSX128:$vj, LSX128:$vk)>;

// vty: v16i8/v8i16/v4i32/v2i64
// Pat<(Intrinsic vty:$vj),
//     (LAInst vty:$vj)>;
foreach Inst = ["VEXTH_H_B", "VEXTH_HU_BU",
                "VMSKLTZ_B", "VMSKGEZ_B", "VMSKNZ_B",
                "VCLO_B"] in
  def : Pat<(deriveLSXIntrinsic<Inst>.ret (v16i8 LSX128:$vj)),
            (!cast<LAInst>(Inst) LSX128:$vj)>;
foreach Inst = ["VEXTH_W_H", "VEXTH_WU_HU", "VMSKLTZ_H",
                "VCLO_H", "VFCVTL_S_H", "VFCVTH_S_H"] in
  def : Pat<(deriveLSXIntrinsic<Inst>.ret (v8i16 LSX128:$vj)),
            (!cast<LAInst>(Inst) LSX128:$vj)>;
foreach Inst = ["VEXTH_D_W", "VEXTH_DU_WU", "VMSKLTZ_W",
                "VCLO_W", "VFFINT_S_W", "VFFINT_S_WU",
                "VFFINTL_D_W", "VFFINTH_D_W"] in
  def : Pat<(deriveLSXIntrinsic<Inst>.ret (v4i32 LSX128:$vj)),
            (!cast<LAInst>(Inst) LSX128:$vj)>;
foreach Inst = ["VEXTH_Q_D", "VEXTH_QU_DU", "VMSKLTZ_D",
                "VEXTL_Q_D", "VEXTL_QU_DU",
                "VCLO_D", "VFFINT_D_L", "VFFINT_D_LU"] in
  def : Pat<(deriveLSXIntrinsic<Inst>.ret (v2i64 LSX128:$vj)),
            (!cast<LAInst>(Inst) LSX128:$vj)>;

// Pat<(Intrinsic timm:$imm)
//     (LAInst timm:$imm)>;
def : Pat<(int_loongarch_lsx_vldi timm:$imm),
          (VLDI (to_valid_timm timm:$imm))>;
foreach Inst = ["VREPLI_B", "VREPLI_H", "VREPLI_W", "VREPLI_D"] in
  def : Pat<(deriveLSXIntrinsic<Inst>.ret timm:$imm),
            (!cast<LAInst>("Pseudo"#Inst) (to_valid_timm timm:$imm))>;

// vty: v16i8/v8i16/v4i32/v2i64
// Pat<(Intrinsic vty:$vj, timm:$imm)
//     (LAInst vty:$vj, timm:$imm)>;
foreach Inst = ["VSAT_B", "VSAT_BU", "VNORI_B", "VROTRI_B", "VSLLWIL_H_B",
                "VSLLWIL_HU_BU", "VSRLRI_B", "VSRARI_B",
                "VSEQI_B", "VSLEI_B", "VSLEI_BU", "VSLTI_B", "VSLTI_BU",
                "VREPLVEI_B", "VBSLL_V", "VBSRL_V", "VSHUF4I_B"] in
  def : Pat<(deriveLSXIntrinsic<Inst>.ret (v16i8 LSX128:$vj), timm:$imm),
            (!cast<LAInst>(Inst) LSX128:$vj, (to_valid_timm timm:$imm))>;
foreach Inst = ["VSAT_H", "VSAT_HU", "VROTRI_H", "VSLLWIL_W_H",
                "VSLLWIL_WU_HU", "VSRLRI_H", "VSRARI_H",
                "VSEQI_H", "VSLEI_H", "VSLEI_HU", "VSLTI_H", "VSLTI_HU",
                "VREPLVEI_H", "VSHUF4I_H"] in
  def : Pat<(deriveLSXIntrinsic<Inst>.ret (v8i16 LSX128:$vj), timm:$imm),
            (!cast<LAInst>(Inst) LSX128:$vj, (to_valid_timm timm:$imm))>;
foreach Inst = ["VSAT_W", "VSAT_WU", "VROTRI_W", "VSLLWIL_D_W",
                "VSLLWIL_DU_WU", "VSRLRI_W", "VSRARI_W",
                "VSEQI_W", "VSLEI_W", "VSLEI_WU", "VSLTI_W", "VSLTI_WU",
                "VREPLVEI_W", "VSHUF4I_W"] in
  def : Pat<(deriveLSXIntrinsic<Inst>.ret (v4i32 LSX128:$vj), timm:$imm),
            (!cast<LAInst>(Inst) LSX128:$vj, (to_valid_timm timm:$imm))>;
foreach Inst = ["VSAT_D", "VSAT_DU", "VROTRI_D", "VSRLRI_D", "VSRARI_D",
                "VSEQI_D", "VSLEI_D", "VSLEI_DU", "VSLTI_D", "VSLTI_DU",
                "VPICKVE2GR_D", "VPICKVE2GR_DU",
                "VREPLVEI_D"] in
  def : Pat<(deriveLSXIntrinsic<Inst>.ret (v2i64 LSX128:$vj), timm:$imm),
            (!cast<LAInst>(Inst) LSX128:$vj, (to_valid_timm timm:$imm))>;

// vty: v16i8/v8i16/v4i32/v2i64
// Pat<(Intrinsic vty:$vd, vty:$vj, timm:$imm)
//     (LAInst vty:$vd, vty:$vj, timm:$imm)>;
foreach Inst = ["VSRLNI_B_H", "VSRANI_B_H", "VSRLRNI_B_H", "VSRARNI_B_H",
                "VSSRLNI_B_H", "VSSRANI_B_H", "VSSRLNI_BU_H", "VSSRANI_BU_H",
                "VSSRLRNI_B_H", "VSSRARNI_B_H", "VSSRLRNI_BU_H", "VSSRARNI_BU_H",
                "VFRSTPI_B", "VBITSELI_B", "VEXTRINS_B"] in
  def : Pat<(deriveLSXIntrinsic<Inst>.ret
               (v16i8 LSX128:$vd), (v16i8 LSX128:$vj), timm:$imm),
            (!cast<LAInst>(Inst) LSX128:$vd, LSX128:$vj,
               (to_valid_timm timm:$imm))>;
foreach Inst = ["VSRLNI_H_W", "VSRANI_H_W", "VSRLRNI_H_W", "VSRARNI_H_W",
                "VSSRLNI_H_W", "VSSRANI_H_W", "VSSRLNI_HU_W", "VSSRANI_HU_W",
                "VSSRLRNI_H_W", "VSSRARNI_H_W", "VSSRLRNI_HU_W", "VSSRARNI_HU_W",
                "VFRSTPI_H", "VEXTRINS_H"] in
  def : Pat<(deriveLSXIntrinsic<Inst>.ret
               (v8i16 LSX128:$vd), (v8i16 LSX128:$vj), timm:$imm),
            (!cast<LAInst>(Inst) LSX128:$vd, LSX128:$vj,
               (to_valid_timm timm:$imm))>;
foreach Inst = ["VSRLNI_W_D", "VSRANI_W_D", "VSRLRNI_W_D", "VSRARNI_W_D",
                "VSSRLNI_W_D", "VSSRANI_W_D", "VSSRLNI_WU_D", "VSSRANI_WU_D",
                "VSSRLRNI_W_D", "VSSRARNI_W_D", "VSSRLRNI_WU_D", "VSSRARNI_WU_D",
                "VPERMI_W", "VEXTRINS_W"] in
  def : Pat<(deriveLSXIntrinsic<Inst>.ret
               (v4i32 LSX128:$vd), (v4i32 LSX128:$vj), timm:$imm),
            (!cast<LAInst>(Inst) LSX128:$vd, LSX128:$vj,
               (to_valid_timm timm:$imm))>;
foreach Inst = ["VSRLNI_D_Q", "VSRANI_D_Q", "VSRLRNI_D_Q", "VSRARNI_D_Q",
                "VSSRLNI_D_Q", "VSSRANI_D_Q", "VSSRLNI_DU_Q", "VSSRANI_DU_Q",
                "VSSRLRNI_D_Q", "VSSRARNI_D_Q", "VSSRLRNI_DU_Q", "VSSRARNI_DU_Q",
                "VSHUF4I_D", "VEXTRINS_D"] in
  def : Pat<(deriveLSXIntrinsic<Inst>.ret
               (v2i64 LSX128:$vd), (v2i64 LSX128:$vj), timm:$imm),
            (!cast<LAInst>(Inst) LSX128:$vd, LSX128:$vj,
               (to_valid_timm timm:$imm))>;

// vty: v16i8/v8i16/v4i32/v2i64
// Pat<(Intrinsic vty:$vd, vty:$vj, vty:$vk),
//     (LAInst vty:$vd, vty:$vj, vty:$vk)>;
foreach Inst = ["VFRSTP_B", "VBITSEL_V", "VSHUF_B"] in
  def : Pat<(deriveLSXIntrinsic<Inst>.ret
               (v16i8 LSX128:$vd), (v16i8 LSX128:$vj), (v16i8 LSX128:$vk)),
            (!cast<LAInst>(Inst) LSX128:$vd, LSX128:$vj, LSX128:$vk)>;
foreach Inst = ["VFRSTP_H", "VSHUF_H"] in
  def : Pat<(deriveLSXIntrinsic<Inst>.ret
               (v8i16 LSX128:$vd), (v8i16 LSX128:$vj), (v8i16 LSX128:$vk)),
            (!cast<LAInst>(Inst) LSX128:$vd, LSX128:$vj, LSX128:$vk)>;
def : Pat<(int_loongarch_lsx_vshuf_w (v4i32 LSX128:$vd), (v4i32 LSX128:$vj),
                                     (v4i32 LSX128:$vk)),
          (VSHUF_W LSX128:$vd, LSX128:$vj, LSX128:$vk)>;
def : Pat<(int_loongarch_lsx_vshuf_d (v2i64 LSX128:$vd), (v2i64 LSX128:$vj),
                                     (v2i64 LSX128:$vk)),
          (VSHUF_D LSX128:$vd, LSX128:$vj, LSX128:$vk)>;

// vty: v4f32/v2f64
// Pat<(Intrinsic vty:$vj, vty:$vk, vty:$va),
//     (LAInst vty:$vj, vty:$vk, vty:$va)>;
foreach Inst = ["VFMSUB_S", "VFNMADD_S", "VFNMSUB_S"] in
  def : Pat<(deriveLSXIntrinsic<Inst>.ret
               (v4f32 LSX128:$vj), (v4f32 LSX128:$vk), (v4f32 LSX128:$va)),
            (!cast<LAInst>(Inst) LSX128:$vj, LSX128:$vk, LSX128:$va)>;
foreach Inst = ["VFMSUB_D", "VFNMADD_D", "VFNMSUB_D"] in
  def : Pat<(deriveLSXIntrinsic<Inst>.ret
               (v2f64 LSX128:$vj), (v2f64 LSX128:$vk), (v2f64 LSX128:$va)),
            (!cast<LAInst>(Inst) LSX128:$vj, LSX128:$vk, LSX128:$va)>;

// vty: v4f32/v2f64
// Pat<(Intrinsic vty:$vj, vty:$vk),
//     (LAInst vty:$vj, vty:$vk)>;
foreach Inst = ["VFMAX_S", "VFMIN_S", "VFMAXA_S", "VFMINA_S", "VFCVT_H_S",
                "VFCMP_CAF_S", "VFCMP_CUN_S", "VFCMP_CEQ_S", "VFCMP_CUEQ_S",
                "VFCMP_CLT_S", "VFCMP_CULT_S", "VFCMP_CLE_S", "VFCMP_CULE_S",
                "VFCMP_CNE_S", "VFCMP_COR_S", "VFCMP_CUNE_S",
                "VFCMP_SAF_S", "VFCMP_SUN_S", "VFCMP_SEQ_S", "VFCMP_SUEQ_S",
                "VFCMP_SLT_S", "VFCMP_SULT_S", "VFCMP_SLE_S", "VFCMP_SULE_S",
                "VFCMP_SNE_S", "VFCMP_SOR_S", "VFCMP_SUNE_S"] in
  def : Pat<(deriveLSXIntrinsic<Inst>.ret
               (v4f32 LSX128:$vj), (v4f32 LSX128:$vk)),
            (!cast<LAInst>(Inst) LSX128:$vj, LSX128:$vk)>;
foreach Inst = ["VFMAX_D", "VFMIN_D", "VFMAXA_D", "VFMINA_D", "VFCVT_S_D",
                "VFTINTRNE_W_D", "VFTINTRZ_W_D", "VFTINTRP_W_D", "VFTINTRM_W_D",
                "VFTINT_W_D",
                "VFCMP_CAF_D", "VFCMP_CUN_D", "VFCMP_CEQ_D", "VFCMP_CUEQ_D",
                "VFCMP_CLT_D", "VFCMP_CULT_D", "VFCMP_CLE_D", "VFCMP_CULE_D",
                "VFCMP_CNE_D", "VFCMP_COR_D", "VFCMP_CUNE_D",
                "VFCMP_SAF_D", "VFCMP_SUN_D", "VFCMP_SEQ_D", "VFCMP_SUEQ_D",
                "VFCMP_SLT_D", "VFCMP_SULT_D", "VFCMP_SLE_D", "VFCMP_SULE_D",
                "VFCMP_SNE_D", "VFCMP_SOR_D", "VFCMP_SUNE_D"] in
  def : Pat<(deriveLSXIntrinsic<Inst>.ret
               (v2f64 LSX128:$vj), (v2f64 LSX128:$vk)),
            (!cast<LAInst>(Inst) LSX128:$vj, LSX128:$vk)>;

// vty: v4f32/v2f64
// Pat<(Intrinsic vty:$vj),
//     (LAInst vty:$vj)>;
foreach Inst = ["VFLOGB_S", "VFCLASS_S", "VFSQRT_S", "VFRECIP_S", "VFRSQRT_S",
                "VFRINT_S", "VFCVTL_D_S", "VFCVTH_D_S",
                "VFRINTRNE_S", "VFRINTRZ_S", "VFRINTRP_S", "VFRINTRM_S",
                "VFTINTRNE_W_S", "VFTINTRZ_W_S", "VFTINTRP_W_S", "VFTINTRM_W_S",
                "VFTINT_W_S", "VFTINTRZ_WU_S", "VFTINT_WU_S",
                "VFTINTRNEL_L_S", "VFTINTRNEH_L_S", "VFTINTRZL_L_S",
                "VFTINTRZH_L_S", "VFTINTRPL_L_S", "VFTINTRPH_L_S",
                "VFTINTRML_L_S", "VFTINTRMH_L_S", "VFTINTL_L_S",
                "VFTINTH_L_S"] in
  def : Pat<(deriveLSXIntrinsic<Inst>.ret (v4f32 LSX128:$vj)),
            (!cast<LAInst>(Inst) LSX128:$vj)>;
foreach Inst = ["VFLOGB_D", "VFCLASS_D", "VFSQRT_D", "VFRECIP_D", "VFRSQRT_D",
                "VFRINT_D",
                "VFRINTRNE_D", "VFRINTRZ_D", "VFRINTRP_D", "VFRINTRM_D",
                "VFTINTRNE_L_D", "VFTINTRZ_L_D", "VFTINTRP_L_D", "VFTINTRM_L_D",
                "VFTINT_L_D", "VFTINTRZ_LU_D", "VFTINT_LU_D"] in
  def : Pat<(deriveLSXIntrinsic<Inst>.ret (v2f64 LSX128:$vj)),
            (!cast<LAInst>(Inst) LSX128:$vj)>;

// 128-Bit vector FP approximate reciprocal operation
let Predicates = [HasFrecipe] in {
foreach Inst = ["VFRECIPE_S", "VFRSQRTE_S"] in
  def : Pat<(deriveLSXIntrinsic<Inst>.ret (v4f32 LSX128:$vj)),
            (!cast<LAInst>(Inst) LSX128:$vj)>;
foreach Inst = ["VFRECIPE_D", "VFRSQRTE_D"] in
  def : Pat<(deriveLSXIntrinsic<Inst>.ret (v2f64 LSX128:$vj)),
            (!cast<LAInst>(Inst) LSX128:$vj)>;
}

// load
def : Pat<(int_loongarch_lsx_vld GPR:$rj, timm:$imm),
          (VLD GPR:$rj, (to_valid_timm timm:$imm))>;
def : Pat<(int_loongarch_lsx_vldx GPR:$rj, GPR:$rk),
          (VLDX GPR:$rj, GPR:$rk)>;

def : Pat<(int_loongarch_lsx_vldrepl_b GPR:$rj, timm:$imm),
          (VLDREPL_B GPR:$rj, (to_valid_timm timm:$imm))>;
def : Pat<(int_loongarch_lsx_vldrepl_h GPR:$rj, timm:$imm),
          (VLDREPL_H GPR:$rj, (to_valid_timm timm:$imm))>;
def : Pat<(int_loongarch_lsx_vldrepl_w GPR:$rj, timm:$imm),
          (VLDREPL_W GPR:$rj, (to_valid_timm timm:$imm))>;
def : Pat<(int_loongarch_lsx_vldrepl_d GPR:$rj, timm:$imm),
          (VLDREPL_D GPR:$rj, (to_valid_timm timm:$imm))>;

// store
def : Pat<(int_loongarch_lsx_vst LSX128:$vd, GPR:$rj, timm:$imm),
          (VST LSX128:$vd, GPR:$rj, (to_valid_timm timm:$imm))>;
def : Pat<(int_loongarch_lsx_vstx LSX128:$vd, GPR:$rj, GPR:$rk),
          (VSTX LSX128:$vd, GPR:$rj, GPR:$rk)>;

def : Pat<(int_loongarch_lsx_vstelm_b v16i8:$vd, GPR:$rj, timm:$imm, timm:$idx),
          (VSTELM_B v16i8:$vd, GPR:$rj, (to_valid_timm timm:$imm),
                    (to_valid_timm timm:$idx))>;
def : Pat<(int_loongarch_lsx_vstelm_h v8i16:$vd, GPR:$rj, timm:$imm, timm:$idx),
          (VSTELM_H v8i16:$vd, GPR:$rj, (to_valid_timm timm:$imm),
                    (to_valid_timm timm:$idx))>;
def : Pat<(int_loongarch_lsx_vstelm_w v4i32:$vd, GPR:$rj, timm:$imm, timm:$idx),
          (VSTELM_W v4i32:$vd, GPR:$rj, (to_valid_timm timm:$imm),
                    (to_valid_timm timm:$idx))>;
def : Pat<(int_loongarch_lsx_vstelm_d v2i64:$vd, GPR:$rj, timm:$imm, timm:$idx),
          (VSTELM_D v2i64:$vd, GPR:$rj, (to_valid_timm timm:$imm),
                    (to_valid_timm timm:$idx))>;

} // Predicates = [HasExtLSX]
