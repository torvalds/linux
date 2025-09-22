//===- MipsMSAInstrInfo.td - MSA ASE instructions -*- tablegen ------------*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes Mips MSA ASE instructions.
//
//===----------------------------------------------------------------------===//

def SDT_MipsVecCond : SDTypeProfile<1, 1, [SDTCisInt<0>, SDTCisVec<1>]>;
def SDT_VSetCC : SDTypeProfile<1, 3, [SDTCisInt<0>,
                                      SDTCisInt<1>,
                                      SDTCisSameAs<1, 2>,
                                      SDTCisVT<3, OtherVT>]>;
def SDT_VFSetCC : SDTypeProfile<1, 3, [SDTCisInt<0>,
                                       SDTCisFP<1>,
                                       SDTCisSameAs<1, 2>,
                                       SDTCisVT<3, OtherVT>]>;
def SDT_VSHF : SDTypeProfile<1, 3, [SDTCisInt<0>, SDTCisVec<0>,
                                    SDTCisInt<1>, SDTCisVec<1>,
                                    SDTCisSameAs<0, 2>, SDTCisSameAs<2, 3>]>;
def SDT_SHF : SDTypeProfile<1, 2, [SDTCisInt<0>, SDTCisVec<0>,
                                   SDTCisVT<1, i32>, SDTCisSameAs<0, 2>]>;
def SDT_ILV : SDTypeProfile<1, 2, [SDTCisInt<0>, SDTCisVec<0>,
                                   SDTCisSameAs<0, 1>, SDTCisSameAs<1, 2>]>;
def SDT_INSVE : SDTypeProfile<1, 4, [SDTCisVec<0>, SDTCisSameAs<0, 1>,
                                     SDTCisVT<2, i32>, SDTCisSameAs<0, 3>,
                                     SDTCisVT<4, i32>]>;

def MipsVAllNonZero : SDNode<"MipsISD::VALL_NONZERO", SDT_MipsVecCond>;
def MipsVAnyNonZero : SDNode<"MipsISD::VANY_NONZERO", SDT_MipsVecCond>;
def MipsVAllZero : SDNode<"MipsISD::VALL_ZERO", SDT_MipsVecCond>;
def MipsVAnyZero : SDNode<"MipsISD::VANY_ZERO", SDT_MipsVecCond>;
def MipsVNOR : SDNode<"MipsISD::VNOR", SDTIntBinOp,
                      [SDNPCommutative, SDNPAssociative]>;
def MipsVSHF : SDNode<"MipsISD::VSHF", SDT_VSHF>;
def MipsSHF : SDNode<"MipsISD::SHF", SDT_SHF>;
def MipsILVEV : SDNode<"MipsISD::ILVEV", SDT_ILV>;
def MipsILVOD : SDNode<"MipsISD::ILVOD", SDT_ILV>;
def MipsILVL  : SDNode<"MipsISD::ILVL",  SDT_ILV>;
def MipsILVR  : SDNode<"MipsISD::ILVR",  SDT_ILV>;
def MipsPCKEV : SDNode<"MipsISD::PCKEV", SDT_ILV>;
def MipsPCKOD : SDNode<"MipsISD::PCKOD", SDT_ILV>;
def MipsINSVE : SDNode<"MipsISD::INSVE", SDT_INSVE>;
def MipsFMS   : SDNode<"MipsISD::FMS", SDTFPTernaryOp>;

def vsetcc : SDNode<"ISD::SETCC", SDT_VSetCC>;
def vfsetcc : SDNode<"ISD::SETCC", SDT_VFSetCC>;

def MipsVExtractSExt : SDNode<"MipsISD::VEXTRACT_SEXT_ELT",
    SDTypeProfile<1, 3, [SDTCisPtrTy<2>]>, []>;
def MipsVExtractZExt : SDNode<"MipsISD::VEXTRACT_ZEXT_ELT",
    SDTypeProfile<1, 3, [SDTCisPtrTy<2>]>, []>;

def immZExt1Ptr : ImmLeaf<iPTR, [{return isUInt<1>(Imm);}]>;
def immZExt2Ptr : ImmLeaf<iPTR, [{return isUInt<2>(Imm);}]>;
def immZExt3Ptr : ImmLeaf<iPTR, [{return isUInt<3>(Imm);}]>;
def immZExt4Ptr : ImmLeaf<iPTR, [{return isUInt<4>(Imm);}]>;

def timmZExt1Ptr : TImmLeaf<iPTR, [{return isUInt<1>(Imm);}]>;
def timmZExt2Ptr : TImmLeaf<iPTR, [{return isUInt<2>(Imm);}]>;
def timmZExt3Ptr : TImmLeaf<iPTR, [{return isUInt<3>(Imm);}]>;
def timmZExt4Ptr : TImmLeaf<iPTR, [{return isUInt<4>(Imm);}]>;

// Operands

def immZExt2Lsa : ImmLeaf<i32, [{return isUInt<2>(Imm - 1);}]>;

// Pattern fragments
def vextract_sext_i8  : PatFrag<(ops node:$vec, node:$idx),
                                (MipsVExtractSExt node:$vec, node:$idx, i8)>;
def vextract_sext_i16 : PatFrag<(ops node:$vec, node:$idx),
                                (MipsVExtractSExt node:$vec, node:$idx, i16)>;
def vextract_sext_i32 : PatFrag<(ops node:$vec, node:$idx),
                                (MipsVExtractSExt node:$vec, node:$idx, i32)>;
def vextract_sext_i64 : PatFrag<(ops node:$vec, node:$idx),
                                (MipsVExtractSExt node:$vec, node:$idx, i64)>;

def vextract_zext_i8  : PatFrag<(ops node:$vec, node:$idx),
                                (MipsVExtractZExt node:$vec, node:$idx, i8)>;
def vextract_zext_i16 : PatFrag<(ops node:$vec, node:$idx),
                                (MipsVExtractZExt node:$vec, node:$idx, i16)>;
def vextract_zext_i32 : PatFrag<(ops node:$vec, node:$idx),
                                (MipsVExtractZExt node:$vec, node:$idx, i32)>;
def vextract_zext_i64 : PatFrag<(ops node:$vec, node:$idx),
                                (MipsVExtractZExt node:$vec, node:$idx, i64)>;

def vinsert_v16i8 : PatFrag<(ops node:$vec, node:$val, node:$idx),
    (v16i8 (vector_insert node:$vec, node:$val, node:$idx))>;
def vinsert_v8i16 : PatFrag<(ops node:$vec, node:$val, node:$idx),
    (v8i16 (vector_insert node:$vec, node:$val, node:$idx))>;
def vinsert_v4i32 : PatFrag<(ops node:$vec, node:$val, node:$idx),
    (v4i32 (vector_insert node:$vec, node:$val, node:$idx))>;
def vinsert_v2i64 : PatFrag<(ops node:$vec, node:$val, node:$idx),
    (v2i64 (vector_insert node:$vec, node:$val, node:$idx))>;

def insve_v16i8 : PatFrag<(ops node:$v1, node:$i1, node:$v2, node:$i2),
    (v16i8 (MipsINSVE node:$v1, node:$i1, node:$v2, node:$i2))>;
def insve_v8i16 : PatFrag<(ops node:$v1, node:$i1, node:$v2, node:$i2),
    (v8i16 (MipsINSVE node:$v1, node:$i1, node:$v2, node:$i2))>;
def insve_v4i32 : PatFrag<(ops node:$v1, node:$i1, node:$v2, node:$i2),
    (v4i32 (MipsINSVE node:$v1, node:$i1, node:$v2, node:$i2))>;
def insve_v2i64 : PatFrag<(ops node:$v1, node:$i1, node:$v2, node:$i2),
    (v2i64 (MipsINSVE node:$v1, node:$i1, node:$v2, node:$i2))>;

class vfsetcc_type<ValueType ResTy, ValueType OpTy, CondCode CC> :
  PatFrag<(ops node:$lhs, node:$rhs),
          (ResTy (vfsetcc (OpTy node:$lhs), (OpTy node:$rhs), CC))>;

// ISD::SETFALSE cannot occur
def vfseteq_v4f32 : vfsetcc_type<v4i32, v4f32, SETEQ>;
def vfseteq_v2f64 : vfsetcc_type<v2i64, v2f64, SETEQ>;
def vfsetge_v4f32 : vfsetcc_type<v4i32, v4f32, SETGE>;
def vfsetge_v2f64 : vfsetcc_type<v2i64, v2f64, SETGE>;
def vfsetgt_v4f32 : vfsetcc_type<v4i32, v4f32, SETGT>;
def vfsetgt_v2f64 : vfsetcc_type<v2i64, v2f64, SETGT>;
def vfsetle_v4f32 : vfsetcc_type<v4i32, v4f32, SETLE>;
def vfsetle_v2f64 : vfsetcc_type<v2i64, v2f64, SETLE>;
def vfsetlt_v4f32 : vfsetcc_type<v4i32, v4f32, SETLT>;
def vfsetlt_v2f64 : vfsetcc_type<v2i64, v2f64, SETLT>;
def vfsetne_v4f32 : vfsetcc_type<v4i32, v4f32, SETNE>;
def vfsetne_v2f64 : vfsetcc_type<v2i64, v2f64, SETNE>;
def vfsetoeq_v4f32 : vfsetcc_type<v4i32, v4f32, SETOEQ>;
def vfsetoeq_v2f64 : vfsetcc_type<v2i64, v2f64, SETOEQ>;
def vfsetoge_v4f32 : vfsetcc_type<v4i32, v4f32, SETOGE>;
def vfsetoge_v2f64 : vfsetcc_type<v2i64, v2f64, SETOGE>;
def vfsetogt_v4f32 : vfsetcc_type<v4i32, v4f32, SETOGT>;
def vfsetogt_v2f64 : vfsetcc_type<v2i64, v2f64, SETOGT>;
def vfsetole_v4f32 : vfsetcc_type<v4i32, v4f32, SETOLE>;
def vfsetole_v2f64 : vfsetcc_type<v2i64, v2f64, SETOLE>;
def vfsetolt_v4f32 : vfsetcc_type<v4i32, v4f32, SETOLT>;
def vfsetolt_v2f64 : vfsetcc_type<v2i64, v2f64, SETOLT>;
def vfsetone_v4f32 : vfsetcc_type<v4i32, v4f32, SETONE>;
def vfsetone_v2f64 : vfsetcc_type<v2i64, v2f64, SETONE>;
def vfsetord_v4f32 : vfsetcc_type<v4i32, v4f32, SETO>;
def vfsetord_v2f64 : vfsetcc_type<v2i64, v2f64, SETO>;
def vfsetun_v4f32  : vfsetcc_type<v4i32, v4f32, SETUO>;
def vfsetun_v2f64  : vfsetcc_type<v2i64, v2f64, SETUO>;
def vfsetueq_v4f32 : vfsetcc_type<v4i32, v4f32, SETUEQ>;
def vfsetueq_v2f64 : vfsetcc_type<v2i64, v2f64, SETUEQ>;
def vfsetuge_v4f32 : vfsetcc_type<v4i32, v4f32, SETUGE>;
def vfsetuge_v2f64 : vfsetcc_type<v2i64, v2f64, SETUGE>;
def vfsetugt_v4f32 : vfsetcc_type<v4i32, v4f32, SETUGT>;
def vfsetugt_v2f64 : vfsetcc_type<v2i64, v2f64, SETUGT>;
def vfsetule_v4f32 : vfsetcc_type<v4i32, v4f32, SETULE>;
def vfsetule_v2f64 : vfsetcc_type<v2i64, v2f64, SETULE>;
def vfsetult_v4f32 : vfsetcc_type<v4i32, v4f32, SETULT>;
def vfsetult_v2f64 : vfsetcc_type<v2i64, v2f64, SETULT>;
def vfsetune_v4f32 : vfsetcc_type<v4i32, v4f32, SETUNE>;
def vfsetune_v2f64 : vfsetcc_type<v2i64, v2f64, SETUNE>;
// ISD::SETTRUE cannot occur
// ISD::SETFALSE2 cannot occur
// ISD::SETTRUE2 cannot occur

class vsetcc_type<ValueType ResTy, CondCode CC> :
  PatFrag<(ops node:$lhs, node:$rhs),
          (ResTy (vsetcc node:$lhs, node:$rhs, CC))>;

def vseteq_v16i8  : vsetcc_type<v16i8, SETEQ>;
def vseteq_v8i16  : vsetcc_type<v8i16, SETEQ>;
def vseteq_v4i32  : vsetcc_type<v4i32, SETEQ>;
def vseteq_v2i64  : vsetcc_type<v2i64, SETEQ>;
def vsetle_v16i8  : vsetcc_type<v16i8, SETLE>;
def vsetle_v8i16  : vsetcc_type<v8i16, SETLE>;
def vsetle_v4i32  : vsetcc_type<v4i32, SETLE>;
def vsetle_v2i64  : vsetcc_type<v2i64, SETLE>;
def vsetlt_v16i8  : vsetcc_type<v16i8, SETLT>;
def vsetlt_v8i16  : vsetcc_type<v8i16, SETLT>;
def vsetlt_v4i32  : vsetcc_type<v4i32, SETLT>;
def vsetlt_v2i64  : vsetcc_type<v2i64, SETLT>;
def vsetule_v16i8 : vsetcc_type<v16i8, SETULE>;
def vsetule_v8i16 : vsetcc_type<v8i16, SETULE>;
def vsetule_v4i32 : vsetcc_type<v4i32, SETULE>;
def vsetule_v2i64 : vsetcc_type<v2i64, SETULE>;
def vsetult_v16i8 : vsetcc_type<v16i8, SETULT>;
def vsetult_v8i16 : vsetcc_type<v8i16, SETULT>;
def vsetult_v4i32 : vsetcc_type<v4i32, SETULT>;
def vsetult_v2i64 : vsetcc_type<v2i64, SETULT>;

def vsplati8  : PatFrag<(ops node:$e0),
                        (v16i8 (build_vector node:$e0, node:$e0,
                                             node:$e0, node:$e0,
                                             node:$e0, node:$e0,
                                             node:$e0, node:$e0,
                                             node:$e0, node:$e0,
                                             node:$e0, node:$e0,
                                             node:$e0, node:$e0,
                                             node:$e0, node:$e0))>;
def vsplati16 : PatFrag<(ops node:$e0),
                        (v8i16 (build_vector node:$e0, node:$e0,
                                             node:$e0, node:$e0,
                                             node:$e0, node:$e0,
                                             node:$e0, node:$e0))>;
def vsplati32 : PatFrag<(ops node:$e0),
                        (v4i32 (build_vector node:$e0, node:$e0,
                                             node:$e0, node:$e0))>;

// Any build_vector that is a constant splat with a value that equals 1
def vsplat_imm_eq_1 : ComplexPattern<vAny, 0, "selectVSplatImmEq1">;

def vsplati64 : PatFrag<(ops node:$e0),
                        (v2i64 (build_vector node:$e0, node:$e0))>;

def vsplati64_splat_d : PatFrag<(ops node:$e0),
                                (v2i64 (bitconvert
                                         (v4i32 (and
                                           (v4i32 (build_vector node:$e0,
                                                                node:$e0,
                                                                node:$e0,
                                                                node:$e0)),
                                           (vsplat_imm_eq_1)))))>;

def vsplatf32 : PatFrag<(ops node:$e0),
                        (v4f32 (build_vector node:$e0, node:$e0,
                                             node:$e0, node:$e0))>;
def vsplatf64 : PatFrag<(ops node:$e0),
                        (v2f64 (build_vector node:$e0, node:$e0))>;

def vsplati8_elt  : PatFrag<(ops node:$v, node:$i),
                            (MipsVSHF (vsplati8 node:$i), node:$v, node:$v)>;
def vsplati16_elt : PatFrag<(ops node:$v, node:$i),
                            (MipsVSHF (vsplati16 node:$i), node:$v, node:$v)>;
def vsplati32_elt : PatFrag<(ops node:$v, node:$i),
                            (MipsVSHF (vsplati32 node:$i), node:$v, node:$v)>;
def vsplati64_elt : PatFrag<(ops node:$v, node:$i),
                            (MipsVSHF (vsplati64_splat_d node:$i),
                                      node:$v, node:$v)>;

class SplatPatLeaf<Operand opclass, dag frag, code pred = [{}],
                   SDNodeXForm xform = NOOP_SDNodeXForm>
  : PatLeaf<frag, pred, xform> {
  Operand OpClass = opclass;
}

class SplatComplexPattern<Operand opclass, ValueType ty, int numops, string fn,
                          list<SDNode> roots = [],
                          list<SDNodeProperty> props = []> :
  ComplexPattern<ty, numops, fn, roots, props> {
  Operand OpClass = opclass;
}

def vsplati8_uimm3 : SplatComplexPattern<vsplat_uimm3, v16i8, 1,
                                         "selectVSplatUimm3",
                                         [build_vector, bitconvert]>;

def vsplati8_uimm4 : SplatComplexPattern<vsplat_uimm4, v16i8, 1,
                                         "selectVSplatUimm4",
                                         [build_vector, bitconvert]>;

def vsplati8_uimm5 : SplatComplexPattern<vsplat_uimm5, v16i8, 1,
                                         "selectVSplatUimm5",
                                         [build_vector, bitconvert]>;

def vsplati8_uimm8 : SplatComplexPattern<vsplat_uimm8, v16i8, 1,
                                         "selectVSplatUimm8",
                                         [build_vector, bitconvert]>;

def vsplati8_simm5 : SplatComplexPattern<vsplat_simm5, v16i8, 1,
                                         "selectVSplatSimm5",
                                         [build_vector, bitconvert]>;

def vsplati16_uimm3 : SplatComplexPattern<vsplat_uimm3, v8i16, 1,
                                          "selectVSplatUimm3",
                                          [build_vector, bitconvert]>;

def vsplati16_uimm4 : SplatComplexPattern<vsplat_uimm4, v8i16, 1,
                                          "selectVSplatUimm4",
                                          [build_vector, bitconvert]>;

def vsplati16_uimm5 : SplatComplexPattern<vsplat_uimm5, v8i16, 1,
                                          "selectVSplatUimm5",
                                          [build_vector, bitconvert]>;

def vsplati16_simm5 : SplatComplexPattern<vsplat_simm5, v8i16, 1,
                                          "selectVSplatSimm5",
                                          [build_vector, bitconvert]>;

def vsplati32_uimm2 : SplatComplexPattern<vsplat_uimm2, v4i32, 1,
                                          "selectVSplatUimm2",
                                          [build_vector, bitconvert]>;

def vsplati32_uimm5 : SplatComplexPattern<vsplat_uimm5, v4i32, 1,
                                          "selectVSplatUimm5",
                                          [build_vector, bitconvert]>;

def vsplati32_simm5 : SplatComplexPattern<vsplat_simm5, v4i32, 1,
                                          "selectVSplatSimm5",
                                          [build_vector, bitconvert]>;

def vsplati64_uimm1 : SplatComplexPattern<vsplat_uimm1, v2i64, 1,
                                          "selectVSplatUimm1",
                                          [build_vector, bitconvert]>;

def vsplati64_uimm5 : SplatComplexPattern<vsplat_uimm5, v2i64, 1,
                                          "selectVSplatUimm5",
                                          [build_vector, bitconvert]>;

def vsplati64_uimm6 : SplatComplexPattern<vsplat_uimm6, v2i64, 1,
                                          "selectVSplatUimm6",
                                          [build_vector, bitconvert]>;

def vsplati64_simm5 : SplatComplexPattern<vsplat_simm5, v2i64, 1,
                                          "selectVSplatSimm5",
                                          [build_vector, bitconvert]>;

// Any build_vector that is a constant splat with a value that is an exact
// power of 2
def vsplat_uimm_pow2 : ComplexPattern<vAny, 1, "selectVSplatUimmPow2",
                                      [build_vector, bitconvert]>;

// Any build_vector that is a constant splat with a value that is the bitwise
// inverse of an exact power of 2
def vsplat_uimm_inv_pow2 : ComplexPattern<vAny, 1, "selectVSplatUimmInvPow2",
                                          [build_vector, bitconvert]>;

// Any build_vector that is a constant splat with only a consecutive sequence
// of left-most bits set.
def vsplat_maskl_bits_uimm3
    : SplatComplexPattern<vsplat_uimm3, vAny, 1, "selectVSplatMaskL",
                          [build_vector, bitconvert]>;
def vsplat_maskl_bits_uimm4
    : SplatComplexPattern<vsplat_uimm4, vAny, 1, "selectVSplatMaskL",
                          [build_vector, bitconvert]>;
def vsplat_maskl_bits_uimm5
    : SplatComplexPattern<vsplat_uimm5, vAny, 1, "selectVSplatMaskL",
                          [build_vector, bitconvert]>;
def vsplat_maskl_bits_uimm6
    : SplatComplexPattern<vsplat_uimm6, vAny, 1, "selectVSplatMaskL",
                          [build_vector, bitconvert]>;

// Any build_vector that is a constant splat with only a consecutive sequence
// of right-most bits set.
def vsplat_maskr_bits_uimm3
    : SplatComplexPattern<vsplat_uimm3, vAny, 1, "selectVSplatMaskR",
                          [build_vector, bitconvert]>;
def vsplat_maskr_bits_uimm4
    : SplatComplexPattern<vsplat_uimm4, vAny, 1, "selectVSplatMaskR",
                          [build_vector, bitconvert]>;
def vsplat_maskr_bits_uimm5
    : SplatComplexPattern<vsplat_uimm5, vAny, 1, "selectVSplatMaskR",
                          [build_vector, bitconvert]>;
def vsplat_maskr_bits_uimm6
    : SplatComplexPattern<vsplat_uimm6, vAny, 1, "selectVSplatMaskR",
                          [build_vector, bitconvert]>;


def vbclr_b : PatFrag<(ops node:$ws, node:$wt),
                      (and node:$ws, (vnot (shl (vsplat_imm_eq_1), node:$wt)))>;
def vbclr_h : PatFrag<(ops node:$ws, node:$wt),
                      (and node:$ws, (vnot (shl (vsplat_imm_eq_1), node:$wt)))>;
def vbclr_w : PatFrag<(ops node:$ws, node:$wt),
                      (and node:$ws, (vnot (shl (vsplat_imm_eq_1), node:$wt)))>;
def vbclr_d : PatFrag<(ops node:$ws, node:$wt),
                      (and node:$ws, (vnot (shl (v2i64 (vsplat_imm_eq_1)),
                                               node:$wt)))>;

def vbneg_b : PatFrag<(ops node:$ws, node:$wt),
                      (xor node:$ws, (shl (vsplat_imm_eq_1), node:$wt))>;
def vbneg_h : PatFrag<(ops node:$ws, node:$wt),
                      (xor node:$ws, (shl (vsplat_imm_eq_1), node:$wt))>;
def vbneg_w : PatFrag<(ops node:$ws, node:$wt),
                      (xor node:$ws, (shl (vsplat_imm_eq_1), node:$wt))>;
def vbneg_d : PatFrag<(ops node:$ws, node:$wt),
                      (xor node:$ws, (shl (v2i64 (vsplat_imm_eq_1)),
                                          node:$wt))>;

def vbset_b : PatFrag<(ops node:$ws, node:$wt),
                      (or node:$ws, (shl (vsplat_imm_eq_1), node:$wt))>;
def vbset_h : PatFrag<(ops node:$ws, node:$wt),
                      (or node:$ws, (shl (vsplat_imm_eq_1), node:$wt))>;
def vbset_w : PatFrag<(ops node:$ws, node:$wt),
                      (or node:$ws, (shl (vsplat_imm_eq_1), node:$wt))>;
def vbset_d : PatFrag<(ops node:$ws, node:$wt),
                      (or node:$ws, (shl (v2i64 (vsplat_imm_eq_1)),
                                         node:$wt))>;

def muladd : PatFrag<(ops node:$wd, node:$ws, node:$wt),
                     (add node:$wd, (mul node:$ws, node:$wt))>;

def mulsub : PatFrag<(ops node:$wd, node:$ws, node:$wt),
                     (sub node:$wd, (mul node:$ws, node:$wt))>;

def mul_fexp2 : PatFrag<(ops node:$ws, node:$wt),
                        (fmul node:$ws, (fexp2 node:$wt))>;

// Instruction encoding.
class ADD_A_B_ENC : MSA_3R_FMT<0b000, 0b00, 0b010000>;
class ADD_A_H_ENC : MSA_3R_FMT<0b000, 0b01, 0b010000>;
class ADD_A_W_ENC : MSA_3R_FMT<0b000, 0b10, 0b010000>;
class ADD_A_D_ENC : MSA_3R_FMT<0b000, 0b11, 0b010000>;

class ADDS_A_B_ENC : MSA_3R_FMT<0b001, 0b00, 0b010000>;
class ADDS_A_H_ENC : MSA_3R_FMT<0b001, 0b01, 0b010000>;
class ADDS_A_W_ENC : MSA_3R_FMT<0b001, 0b10, 0b010000>;
class ADDS_A_D_ENC : MSA_3R_FMT<0b001, 0b11, 0b010000>;

class ADDS_S_B_ENC : MSA_3R_FMT<0b010, 0b00, 0b010000>;
class ADDS_S_H_ENC : MSA_3R_FMT<0b010, 0b01, 0b010000>;
class ADDS_S_W_ENC : MSA_3R_FMT<0b010, 0b10, 0b010000>;
class ADDS_S_D_ENC : MSA_3R_FMT<0b010, 0b11, 0b010000>;

class ADDS_U_B_ENC : MSA_3R_FMT<0b011, 0b00, 0b010000>;
class ADDS_U_H_ENC : MSA_3R_FMT<0b011, 0b01, 0b010000>;
class ADDS_U_W_ENC : MSA_3R_FMT<0b011, 0b10, 0b010000>;
class ADDS_U_D_ENC : MSA_3R_FMT<0b011, 0b11, 0b010000>;

class ADDV_B_ENC : MSA_3R_FMT<0b000, 0b00, 0b001110>;
class ADDV_H_ENC : MSA_3R_FMT<0b000, 0b01, 0b001110>;
class ADDV_W_ENC : MSA_3R_FMT<0b000, 0b10, 0b001110>;
class ADDV_D_ENC : MSA_3R_FMT<0b000, 0b11, 0b001110>;

class ADDVI_B_ENC : MSA_I5_FMT<0b000, 0b00, 0b000110>;
class ADDVI_H_ENC : MSA_I5_FMT<0b000, 0b01, 0b000110>;
class ADDVI_W_ENC : MSA_I5_FMT<0b000, 0b10, 0b000110>;
class ADDVI_D_ENC : MSA_I5_FMT<0b000, 0b11, 0b000110>;

class AND_V_ENC : MSA_VEC_FMT<0b00000, 0b011110>;

class ANDI_B_ENC : MSA_I8_FMT<0b00, 0b000000>;

class ASUB_S_B_ENC : MSA_3R_FMT<0b100, 0b00, 0b010001>;
class ASUB_S_H_ENC : MSA_3R_FMT<0b100, 0b01, 0b010001>;
class ASUB_S_W_ENC : MSA_3R_FMT<0b100, 0b10, 0b010001>;
class ASUB_S_D_ENC : MSA_3R_FMT<0b100, 0b11, 0b010001>;

class ASUB_U_B_ENC : MSA_3R_FMT<0b101, 0b00, 0b010001>;
class ASUB_U_H_ENC : MSA_3R_FMT<0b101, 0b01, 0b010001>;
class ASUB_U_W_ENC : MSA_3R_FMT<0b101, 0b10, 0b010001>;
class ASUB_U_D_ENC : MSA_3R_FMT<0b101, 0b11, 0b010001>;

class AVE_S_B_ENC : MSA_3R_FMT<0b100, 0b00, 0b010000>;
class AVE_S_H_ENC : MSA_3R_FMT<0b100, 0b01, 0b010000>;
class AVE_S_W_ENC : MSA_3R_FMT<0b100, 0b10, 0b010000>;
class AVE_S_D_ENC : MSA_3R_FMT<0b100, 0b11, 0b010000>;

class AVE_U_B_ENC : MSA_3R_FMT<0b101, 0b00, 0b010000>;
class AVE_U_H_ENC : MSA_3R_FMT<0b101, 0b01, 0b010000>;
class AVE_U_W_ENC : MSA_3R_FMT<0b101, 0b10, 0b010000>;
class AVE_U_D_ENC : MSA_3R_FMT<0b101, 0b11, 0b010000>;

class AVER_S_B_ENC : MSA_3R_FMT<0b110, 0b00, 0b010000>;
class AVER_S_H_ENC : MSA_3R_FMT<0b110, 0b01, 0b010000>;
class AVER_S_W_ENC : MSA_3R_FMT<0b110, 0b10, 0b010000>;
class AVER_S_D_ENC : MSA_3R_FMT<0b110, 0b11, 0b010000>;

class AVER_U_B_ENC : MSA_3R_FMT<0b111, 0b00, 0b010000>;
class AVER_U_H_ENC : MSA_3R_FMT<0b111, 0b01, 0b010000>;
class AVER_U_W_ENC : MSA_3R_FMT<0b111, 0b10, 0b010000>;
class AVER_U_D_ENC : MSA_3R_FMT<0b111, 0b11, 0b010000>;

class BCLR_B_ENC : MSA_3R_FMT<0b011, 0b00, 0b001101>;
class BCLR_H_ENC : MSA_3R_FMT<0b011, 0b01, 0b001101>;
class BCLR_W_ENC : MSA_3R_FMT<0b011, 0b10, 0b001101>;
class BCLR_D_ENC : MSA_3R_FMT<0b011, 0b11, 0b001101>;

class BCLRI_B_ENC : MSA_BIT_B_FMT<0b011, 0b001001>;
class BCLRI_H_ENC : MSA_BIT_H_FMT<0b011, 0b001001>;
class BCLRI_W_ENC : MSA_BIT_W_FMT<0b011, 0b001001>;
class BCLRI_D_ENC : MSA_BIT_D_FMT<0b011, 0b001001>;

class BINSL_B_ENC : MSA_3R_FMT<0b110, 0b00, 0b001101>;
class BINSL_H_ENC : MSA_3R_FMT<0b110, 0b01, 0b001101>;
class BINSL_W_ENC : MSA_3R_FMT<0b110, 0b10, 0b001101>;
class BINSL_D_ENC : MSA_3R_FMT<0b110, 0b11, 0b001101>;

class BINSLI_B_ENC : MSA_BIT_B_FMT<0b110, 0b001001>;
class BINSLI_H_ENC : MSA_BIT_H_FMT<0b110, 0b001001>;
class BINSLI_W_ENC : MSA_BIT_W_FMT<0b110, 0b001001>;
class BINSLI_D_ENC : MSA_BIT_D_FMT<0b110, 0b001001>;

class BINSR_B_ENC : MSA_3R_FMT<0b111, 0b00, 0b001101>;
class BINSR_H_ENC : MSA_3R_FMT<0b111, 0b01, 0b001101>;
class BINSR_W_ENC : MSA_3R_FMT<0b111, 0b10, 0b001101>;
class BINSR_D_ENC : MSA_3R_FMT<0b111, 0b11, 0b001101>;

class BINSRI_B_ENC : MSA_BIT_B_FMT<0b111, 0b001001>;
class BINSRI_H_ENC : MSA_BIT_H_FMT<0b111, 0b001001>;
class BINSRI_W_ENC : MSA_BIT_W_FMT<0b111, 0b001001>;
class BINSRI_D_ENC : MSA_BIT_D_FMT<0b111, 0b001001>;

class BMNZ_V_ENC : MSA_VEC_FMT<0b00100, 0b011110>;

class BMNZI_B_ENC : MSA_I8_FMT<0b00, 0b000001>;

class BMZ_V_ENC : MSA_VEC_FMT<0b00101, 0b011110>;

class BMZI_B_ENC : MSA_I8_FMT<0b01, 0b000001>;

class BNEG_B_ENC : MSA_3R_FMT<0b101, 0b00, 0b001101>;
class BNEG_H_ENC : MSA_3R_FMT<0b101, 0b01, 0b001101>;
class BNEG_W_ENC : MSA_3R_FMT<0b101, 0b10, 0b001101>;
class BNEG_D_ENC : MSA_3R_FMT<0b101, 0b11, 0b001101>;

class BNEGI_B_ENC : MSA_BIT_B_FMT<0b101, 0b001001>;
class BNEGI_H_ENC : MSA_BIT_H_FMT<0b101, 0b001001>;
class BNEGI_W_ENC : MSA_BIT_W_FMT<0b101, 0b001001>;
class BNEGI_D_ENC : MSA_BIT_D_FMT<0b101, 0b001001>;

class BNZ_B_ENC : MSA_CBRANCH_FMT<0b111, 0b00>;
class BNZ_H_ENC : MSA_CBRANCH_FMT<0b111, 0b01>;
class BNZ_W_ENC : MSA_CBRANCH_FMT<0b111, 0b10>;
class BNZ_D_ENC : MSA_CBRANCH_FMT<0b111, 0b11>;

class BNZ_V_ENC : MSA_CBRANCH_V_FMT<0b01111>;

class BSEL_V_ENC : MSA_VEC_FMT<0b00110, 0b011110>;

class BSELI_B_ENC : MSA_I8_FMT<0b10, 0b000001>;

class BSET_B_ENC : MSA_3R_FMT<0b100, 0b00, 0b001101>;
class BSET_H_ENC : MSA_3R_FMT<0b100, 0b01, 0b001101>;
class BSET_W_ENC : MSA_3R_FMT<0b100, 0b10, 0b001101>;
class BSET_D_ENC : MSA_3R_FMT<0b100, 0b11, 0b001101>;

class BSETI_B_ENC : MSA_BIT_B_FMT<0b100, 0b001001>;
class BSETI_H_ENC : MSA_BIT_H_FMT<0b100, 0b001001>;
class BSETI_W_ENC : MSA_BIT_W_FMT<0b100, 0b001001>;
class BSETI_D_ENC : MSA_BIT_D_FMT<0b100, 0b001001>;

class BZ_B_ENC : MSA_CBRANCH_FMT<0b110, 0b00>;
class BZ_H_ENC : MSA_CBRANCH_FMT<0b110, 0b01>;
class BZ_W_ENC : MSA_CBRANCH_FMT<0b110, 0b10>;
class BZ_D_ENC : MSA_CBRANCH_FMT<0b110, 0b11>;

class BZ_V_ENC : MSA_CBRANCH_V_FMT<0b01011>;

class CEQ_B_ENC : MSA_3R_FMT<0b000, 0b00, 0b001111>;
class CEQ_H_ENC : MSA_3R_FMT<0b000, 0b01, 0b001111>;
class CEQ_W_ENC : MSA_3R_FMT<0b000, 0b10, 0b001111>;
class CEQ_D_ENC : MSA_3R_FMT<0b000, 0b11, 0b001111>;

class CEQI_B_ENC : MSA_I5_FMT<0b000, 0b00, 0b000111>;
class CEQI_H_ENC : MSA_I5_FMT<0b000, 0b01, 0b000111>;
class CEQI_W_ENC : MSA_I5_FMT<0b000, 0b10, 0b000111>;
class CEQI_D_ENC : MSA_I5_FMT<0b000, 0b11, 0b000111>;

class CFCMSA_ENC : MSA_ELM_CFCMSA_FMT<0b0001111110, 0b011001>;

class CLE_S_B_ENC : MSA_3R_FMT<0b100, 0b00, 0b001111>;
class CLE_S_H_ENC : MSA_3R_FMT<0b100, 0b01, 0b001111>;
class CLE_S_W_ENC : MSA_3R_FMT<0b100, 0b10, 0b001111>;
class CLE_S_D_ENC : MSA_3R_FMT<0b100, 0b11, 0b001111>;

class CLE_U_B_ENC : MSA_3R_FMT<0b101, 0b00, 0b001111>;
class CLE_U_H_ENC : MSA_3R_FMT<0b101, 0b01, 0b001111>;
class CLE_U_W_ENC : MSA_3R_FMT<0b101, 0b10, 0b001111>;
class CLE_U_D_ENC : MSA_3R_FMT<0b101, 0b11, 0b001111>;

class CLEI_S_B_ENC : MSA_I5_FMT<0b100, 0b00, 0b000111>;
class CLEI_S_H_ENC : MSA_I5_FMT<0b100, 0b01, 0b000111>;
class CLEI_S_W_ENC : MSA_I5_FMT<0b100, 0b10, 0b000111>;
class CLEI_S_D_ENC : MSA_I5_FMT<0b100, 0b11, 0b000111>;

class CLEI_U_B_ENC : MSA_I5_FMT<0b101, 0b00, 0b000111>;
class CLEI_U_H_ENC : MSA_I5_FMT<0b101, 0b01, 0b000111>;
class CLEI_U_W_ENC : MSA_I5_FMT<0b101, 0b10, 0b000111>;
class CLEI_U_D_ENC : MSA_I5_FMT<0b101, 0b11, 0b000111>;

class CLT_S_B_ENC : MSA_3R_FMT<0b010, 0b00, 0b001111>;
class CLT_S_H_ENC : MSA_3R_FMT<0b010, 0b01, 0b001111>;
class CLT_S_W_ENC : MSA_3R_FMT<0b010, 0b10, 0b001111>;
class CLT_S_D_ENC : MSA_3R_FMT<0b010, 0b11, 0b001111>;

class CLT_U_B_ENC : MSA_3R_FMT<0b011, 0b00, 0b001111>;
class CLT_U_H_ENC : MSA_3R_FMT<0b011, 0b01, 0b001111>;
class CLT_U_W_ENC : MSA_3R_FMT<0b011, 0b10, 0b001111>;
class CLT_U_D_ENC : MSA_3R_FMT<0b011, 0b11, 0b001111>;

class CLTI_S_B_ENC : MSA_I5_FMT<0b010, 0b00, 0b000111>;
class CLTI_S_H_ENC : MSA_I5_FMT<0b010, 0b01, 0b000111>;
class CLTI_S_W_ENC : MSA_I5_FMT<0b010, 0b10, 0b000111>;
class CLTI_S_D_ENC : MSA_I5_FMT<0b010, 0b11, 0b000111>;

class CLTI_U_B_ENC : MSA_I5_FMT<0b011, 0b00, 0b000111>;
class CLTI_U_H_ENC : MSA_I5_FMT<0b011, 0b01, 0b000111>;
class CLTI_U_W_ENC : MSA_I5_FMT<0b011, 0b10, 0b000111>;
class CLTI_U_D_ENC : MSA_I5_FMT<0b011, 0b11, 0b000111>;

class COPY_S_B_ENC : MSA_ELM_COPY_B_FMT<0b0010, 0b011001>;
class COPY_S_H_ENC : MSA_ELM_COPY_H_FMT<0b0010, 0b011001>;
class COPY_S_W_ENC : MSA_ELM_COPY_W_FMT<0b0010, 0b011001>;
class COPY_S_D_ENC : MSA_ELM_COPY_D_FMT<0b0010, 0b011001>;

class COPY_U_B_ENC : MSA_ELM_COPY_B_FMT<0b0011, 0b011001>;
class COPY_U_H_ENC : MSA_ELM_COPY_H_FMT<0b0011, 0b011001>;
class COPY_U_W_ENC : MSA_ELM_COPY_W_FMT<0b0011, 0b011001>;

class CTCMSA_ENC : MSA_ELM_CTCMSA_FMT<0b0000111110, 0b011001>;

class DIV_S_B_ENC : MSA_3R_FMT<0b100, 0b00, 0b010010>;
class DIV_S_H_ENC : MSA_3R_FMT<0b100, 0b01, 0b010010>;
class DIV_S_W_ENC : MSA_3R_FMT<0b100, 0b10, 0b010010>;
class DIV_S_D_ENC : MSA_3R_FMT<0b100, 0b11, 0b010010>;

class DIV_U_B_ENC : MSA_3R_FMT<0b101, 0b00, 0b010010>;
class DIV_U_H_ENC : MSA_3R_FMT<0b101, 0b01, 0b010010>;
class DIV_U_W_ENC : MSA_3R_FMT<0b101, 0b10, 0b010010>;
class DIV_U_D_ENC : MSA_3R_FMT<0b101, 0b11, 0b010010>;

class DOTP_S_H_ENC : MSA_3R_FMT<0b000, 0b01, 0b010011>;
class DOTP_S_W_ENC : MSA_3R_FMT<0b000, 0b10, 0b010011>;
class DOTP_S_D_ENC : MSA_3R_FMT<0b000, 0b11, 0b010011>;

class DOTP_U_H_ENC : MSA_3R_FMT<0b001, 0b01, 0b010011>;
class DOTP_U_W_ENC : MSA_3R_FMT<0b001, 0b10, 0b010011>;
class DOTP_U_D_ENC : MSA_3R_FMT<0b001, 0b11, 0b010011>;

class DPADD_S_H_ENC : MSA_3R_FMT<0b010, 0b01, 0b010011>;
class DPADD_S_W_ENC : MSA_3R_FMT<0b010, 0b10, 0b010011>;
class DPADD_S_D_ENC : MSA_3R_FMT<0b010, 0b11, 0b010011>;

class DPADD_U_H_ENC : MSA_3R_FMT<0b011, 0b01, 0b010011>;
class DPADD_U_W_ENC : MSA_3R_FMT<0b011, 0b10, 0b010011>;
class DPADD_U_D_ENC : MSA_3R_FMT<0b011, 0b11, 0b010011>;

class DPSUB_S_H_ENC : MSA_3R_FMT<0b100, 0b01, 0b010011>;
class DPSUB_S_W_ENC : MSA_3R_FMT<0b100, 0b10, 0b010011>;
class DPSUB_S_D_ENC : MSA_3R_FMT<0b100, 0b11, 0b010011>;

class DPSUB_U_H_ENC : MSA_3R_FMT<0b101, 0b01, 0b010011>;
class DPSUB_U_W_ENC : MSA_3R_FMT<0b101, 0b10, 0b010011>;
class DPSUB_U_D_ENC : MSA_3R_FMT<0b101, 0b11, 0b010011>;

class FADD_W_ENC : MSA_3RF_FMT<0b0000, 0b0, 0b011011>;
class FADD_D_ENC : MSA_3RF_FMT<0b0000, 0b1, 0b011011>;

class FCAF_W_ENC : MSA_3RF_FMT<0b0000, 0b0, 0b011010>;
class FCAF_D_ENC : MSA_3RF_FMT<0b0000, 0b1, 0b011010>;

class FCEQ_W_ENC : MSA_3RF_FMT<0b0010, 0b0, 0b011010>;
class FCEQ_D_ENC : MSA_3RF_FMT<0b0010, 0b1, 0b011010>;

class FCLASS_W_ENC : MSA_2RF_FMT<0b110010000, 0b0, 0b011110>;
class FCLASS_D_ENC : MSA_2RF_FMT<0b110010000, 0b1, 0b011110>;

class FCLE_W_ENC : MSA_3RF_FMT<0b0110, 0b0, 0b011010>;
class FCLE_D_ENC : MSA_3RF_FMT<0b0110, 0b1, 0b011010>;

class FCLT_W_ENC : MSA_3RF_FMT<0b0100, 0b0, 0b011010>;
class FCLT_D_ENC : MSA_3RF_FMT<0b0100, 0b1, 0b011010>;

class FCNE_W_ENC : MSA_3RF_FMT<0b0011, 0b0, 0b011100>;
class FCNE_D_ENC : MSA_3RF_FMT<0b0011, 0b1, 0b011100>;

class FCOR_W_ENC : MSA_3RF_FMT<0b0001, 0b0, 0b011100>;
class FCOR_D_ENC : MSA_3RF_FMT<0b0001, 0b1, 0b011100>;

class FCUEQ_W_ENC : MSA_3RF_FMT<0b0011, 0b0, 0b011010>;
class FCUEQ_D_ENC : MSA_3RF_FMT<0b0011, 0b1, 0b011010>;

class FCULE_W_ENC : MSA_3RF_FMT<0b0111, 0b0, 0b011010>;
class FCULE_D_ENC : MSA_3RF_FMT<0b0111, 0b1, 0b011010>;

class FCULT_W_ENC : MSA_3RF_FMT<0b0101, 0b0, 0b011010>;
class FCULT_D_ENC : MSA_3RF_FMT<0b0101, 0b1, 0b011010>;

class FCUN_W_ENC : MSA_3RF_FMT<0b0001, 0b0, 0b011010>;
class FCUN_D_ENC : MSA_3RF_FMT<0b0001, 0b1, 0b011010>;

class FCUNE_W_ENC : MSA_3RF_FMT<0b0010, 0b0, 0b011100>;
class FCUNE_D_ENC : MSA_3RF_FMT<0b0010, 0b1, 0b011100>;

class FDIV_W_ENC : MSA_3RF_FMT<0b0011, 0b0, 0b011011>;
class FDIV_D_ENC : MSA_3RF_FMT<0b0011, 0b1, 0b011011>;

class FEXDO_H_ENC : MSA_3RF_FMT<0b1000, 0b0, 0b011011>;
class FEXDO_W_ENC : MSA_3RF_FMT<0b1000, 0b1, 0b011011>;

class FEXP2_W_ENC : MSA_3RF_FMT<0b0111, 0b0, 0b011011>;
class FEXP2_D_ENC : MSA_3RF_FMT<0b0111, 0b1, 0b011011>;

class FEXUPL_W_ENC : MSA_2RF_FMT<0b110011000, 0b0, 0b011110>;
class FEXUPL_D_ENC : MSA_2RF_FMT<0b110011000, 0b1, 0b011110>;

class FEXUPR_W_ENC : MSA_2RF_FMT<0b110011001, 0b0, 0b011110>;
class FEXUPR_D_ENC : MSA_2RF_FMT<0b110011001, 0b1, 0b011110>;

class FFINT_S_W_ENC : MSA_2RF_FMT<0b110011110, 0b0, 0b011110>;
class FFINT_S_D_ENC : MSA_2RF_FMT<0b110011110, 0b1, 0b011110>;

class FFINT_U_W_ENC : MSA_2RF_FMT<0b110011111, 0b0, 0b011110>;
class FFINT_U_D_ENC : MSA_2RF_FMT<0b110011111, 0b1, 0b011110>;

class FFQL_W_ENC : MSA_2RF_FMT<0b110011010, 0b0, 0b011110>;
class FFQL_D_ENC : MSA_2RF_FMT<0b110011010, 0b1, 0b011110>;

class FFQR_W_ENC : MSA_2RF_FMT<0b110011011, 0b0, 0b011110>;
class FFQR_D_ENC : MSA_2RF_FMT<0b110011011, 0b1, 0b011110>;

class FILL_B_ENC : MSA_2R_FILL_FMT<0b11000000, 0b00, 0b011110>;
class FILL_H_ENC : MSA_2R_FILL_FMT<0b11000000, 0b01, 0b011110>;
class FILL_W_ENC : MSA_2R_FILL_FMT<0b11000000, 0b10, 0b011110>;
class FILL_D_ENC : MSA_2R_FILL_D_FMT<0b11000000, 0b11, 0b011110>;

class FLOG2_W_ENC : MSA_2RF_FMT<0b110010111, 0b0, 0b011110>;
class FLOG2_D_ENC : MSA_2RF_FMT<0b110010111, 0b1, 0b011110>;

class FMADD_W_ENC : MSA_3RF_FMT<0b0100, 0b0, 0b011011>;
class FMADD_D_ENC : MSA_3RF_FMT<0b0100, 0b1, 0b011011>;

class FMAX_W_ENC : MSA_3RF_FMT<0b1110, 0b0, 0b011011>;
class FMAX_D_ENC : MSA_3RF_FMT<0b1110, 0b1, 0b011011>;

class FMAX_A_W_ENC : MSA_3RF_FMT<0b1111, 0b0, 0b011011>;
class FMAX_A_D_ENC : MSA_3RF_FMT<0b1111, 0b1, 0b011011>;

class FMIN_W_ENC : MSA_3RF_FMT<0b1100, 0b0, 0b011011>;
class FMIN_D_ENC : MSA_3RF_FMT<0b1100, 0b1, 0b011011>;

class FMIN_A_W_ENC : MSA_3RF_FMT<0b1101, 0b0, 0b011011>;
class FMIN_A_D_ENC : MSA_3RF_FMT<0b1101, 0b1, 0b011011>;

class FMSUB_W_ENC : MSA_3RF_FMT<0b0101, 0b0, 0b011011>;
class FMSUB_D_ENC : MSA_3RF_FMT<0b0101, 0b1, 0b011011>;

class FMUL_W_ENC : MSA_3RF_FMT<0b0010, 0b0, 0b011011>;
class FMUL_D_ENC : MSA_3RF_FMT<0b0010, 0b1, 0b011011>;

class FRINT_W_ENC : MSA_2RF_FMT<0b110010110, 0b0, 0b011110>;
class FRINT_D_ENC : MSA_2RF_FMT<0b110010110, 0b1, 0b011110>;

class FRCP_W_ENC : MSA_2RF_FMT<0b110010101, 0b0, 0b011110>;
class FRCP_D_ENC : MSA_2RF_FMT<0b110010101, 0b1, 0b011110>;

class FRSQRT_W_ENC : MSA_2RF_FMT<0b110010100, 0b0, 0b011110>;
class FRSQRT_D_ENC : MSA_2RF_FMT<0b110010100, 0b1, 0b011110>;

class FSAF_W_ENC : MSA_3RF_FMT<0b1000, 0b0, 0b011010>;
class FSAF_D_ENC : MSA_3RF_FMT<0b1000, 0b1, 0b011010>;

class FSEQ_W_ENC : MSA_3RF_FMT<0b1010, 0b0, 0b011010>;
class FSEQ_D_ENC : MSA_3RF_FMT<0b1010, 0b1, 0b011010>;

class FSLE_W_ENC : MSA_3RF_FMT<0b1110, 0b0, 0b011010>;
class FSLE_D_ENC : MSA_3RF_FMT<0b1110, 0b1, 0b011010>;

class FSLT_W_ENC : MSA_3RF_FMT<0b1100, 0b0, 0b011010>;
class FSLT_D_ENC : MSA_3RF_FMT<0b1100, 0b1, 0b011010>;

class FSNE_W_ENC : MSA_3RF_FMT<0b1011, 0b0, 0b011100>;
class FSNE_D_ENC : MSA_3RF_FMT<0b1011, 0b1, 0b011100>;

class FSOR_W_ENC : MSA_3RF_FMT<0b1001, 0b0, 0b011100>;
class FSOR_D_ENC : MSA_3RF_FMT<0b1001, 0b1, 0b011100>;

class FSQRT_W_ENC : MSA_2RF_FMT<0b110010011, 0b0, 0b011110>;
class FSQRT_D_ENC : MSA_2RF_FMT<0b110010011, 0b1, 0b011110>;

class FSUB_W_ENC : MSA_3RF_FMT<0b0001, 0b0, 0b011011>;
class FSUB_D_ENC : MSA_3RF_FMT<0b0001, 0b1, 0b011011>;

class FSUEQ_W_ENC : MSA_3RF_FMT<0b1011, 0b0, 0b011010>;
class FSUEQ_D_ENC : MSA_3RF_FMT<0b1011, 0b1, 0b011010>;

class FSULE_W_ENC : MSA_3RF_FMT<0b1111, 0b0, 0b011010>;
class FSULE_D_ENC : MSA_3RF_FMT<0b1111, 0b1, 0b011010>;

class FSULT_W_ENC : MSA_3RF_FMT<0b1101, 0b0, 0b011010>;
class FSULT_D_ENC : MSA_3RF_FMT<0b1101, 0b1, 0b011010>;

class FSUN_W_ENC : MSA_3RF_FMT<0b1001, 0b0, 0b011010>;
class FSUN_D_ENC : MSA_3RF_FMT<0b1001, 0b1, 0b011010>;

class FSUNE_W_ENC : MSA_3RF_FMT<0b1010, 0b0, 0b011100>;
class FSUNE_D_ENC : MSA_3RF_FMT<0b1010, 0b1, 0b011100>;

class FTINT_S_W_ENC : MSA_2RF_FMT<0b110011100, 0b0, 0b011110>;
class FTINT_S_D_ENC : MSA_2RF_FMT<0b110011100, 0b1, 0b011110>;

class FTINT_U_W_ENC : MSA_2RF_FMT<0b110011101, 0b0, 0b011110>;
class FTINT_U_D_ENC : MSA_2RF_FMT<0b110011101, 0b1, 0b011110>;

class FTQ_H_ENC : MSA_3RF_FMT<0b1010, 0b0, 0b011011>;
class FTQ_W_ENC : MSA_3RF_FMT<0b1010, 0b1, 0b011011>;

class FTRUNC_S_W_ENC : MSA_2RF_FMT<0b110010001, 0b0, 0b011110>;
class FTRUNC_S_D_ENC : MSA_2RF_FMT<0b110010001, 0b1, 0b011110>;

class FTRUNC_U_W_ENC : MSA_2RF_FMT<0b110010010, 0b0, 0b011110>;
class FTRUNC_U_D_ENC : MSA_2RF_FMT<0b110010010, 0b1, 0b011110>;

class HADD_S_H_ENC : MSA_3R_FMT<0b100, 0b01, 0b010101>;
class HADD_S_W_ENC : MSA_3R_FMT<0b100, 0b10, 0b010101>;
class HADD_S_D_ENC : MSA_3R_FMT<0b100, 0b11, 0b010101>;

class HADD_U_H_ENC : MSA_3R_FMT<0b101, 0b01, 0b010101>;
class HADD_U_W_ENC : MSA_3R_FMT<0b101, 0b10, 0b010101>;
class HADD_U_D_ENC : MSA_3R_FMT<0b101, 0b11, 0b010101>;

class HSUB_S_H_ENC : MSA_3R_FMT<0b110, 0b01, 0b010101>;
class HSUB_S_W_ENC : MSA_3R_FMT<0b110, 0b10, 0b010101>;
class HSUB_S_D_ENC : MSA_3R_FMT<0b110, 0b11, 0b010101>;

class HSUB_U_H_ENC : MSA_3R_FMT<0b111, 0b01, 0b010101>;
class HSUB_U_W_ENC : MSA_3R_FMT<0b111, 0b10, 0b010101>;
class HSUB_U_D_ENC : MSA_3R_FMT<0b111, 0b11, 0b010101>;

class ILVEV_B_ENC : MSA_3R_FMT<0b110, 0b00, 0b010100>;
class ILVEV_H_ENC : MSA_3R_FMT<0b110, 0b01, 0b010100>;
class ILVEV_W_ENC : MSA_3R_FMT<0b110, 0b10, 0b010100>;
class ILVEV_D_ENC : MSA_3R_FMT<0b110, 0b11, 0b010100>;

class ILVL_B_ENC : MSA_3R_FMT<0b100, 0b00, 0b010100>;
class ILVL_H_ENC : MSA_3R_FMT<0b100, 0b01, 0b010100>;
class ILVL_W_ENC : MSA_3R_FMT<0b100, 0b10, 0b010100>;
class ILVL_D_ENC : MSA_3R_FMT<0b100, 0b11, 0b010100>;

class ILVOD_B_ENC : MSA_3R_FMT<0b111, 0b00, 0b010100>;
class ILVOD_H_ENC : MSA_3R_FMT<0b111, 0b01, 0b010100>;
class ILVOD_W_ENC : MSA_3R_FMT<0b111, 0b10, 0b010100>;
class ILVOD_D_ENC : MSA_3R_FMT<0b111, 0b11, 0b010100>;

class ILVR_B_ENC : MSA_3R_FMT<0b101, 0b00, 0b010100>;
class ILVR_H_ENC : MSA_3R_FMT<0b101, 0b01, 0b010100>;
class ILVR_W_ENC : MSA_3R_FMT<0b101, 0b10, 0b010100>;
class ILVR_D_ENC : MSA_3R_FMT<0b101, 0b11, 0b010100>;

class INSERT_B_ENC : MSA_ELM_INSERT_B_FMT<0b0100, 0b011001>;
class INSERT_H_ENC : MSA_ELM_INSERT_H_FMT<0b0100, 0b011001>;
class INSERT_W_ENC : MSA_ELM_INSERT_W_FMT<0b0100, 0b011001>;
class INSERT_D_ENC : MSA_ELM_INSERT_D_FMT<0b0100, 0b011001>;

class INSVE_B_ENC : MSA_ELM_B_FMT<0b0101, 0b011001>;
class INSVE_H_ENC : MSA_ELM_H_FMT<0b0101, 0b011001>;
class INSVE_W_ENC : MSA_ELM_W_FMT<0b0101, 0b011001>;
class INSVE_D_ENC : MSA_ELM_D_FMT<0b0101, 0b011001>;

class LD_B_ENC   : MSA_MI10_FMT<0b00, 0b1000>;
class LD_H_ENC   : MSA_MI10_FMT<0b01, 0b1000>;
class LD_W_ENC   : MSA_MI10_FMT<0b10, 0b1000>;
class LD_D_ENC   : MSA_MI10_FMT<0b11, 0b1000>;

class LDI_B_ENC  : MSA_I10_FMT<0b110, 0b00, 0b000111>;
class LDI_H_ENC  : MSA_I10_FMT<0b110, 0b01, 0b000111>;
class LDI_W_ENC  : MSA_I10_FMT<0b110, 0b10, 0b000111>;
class LDI_D_ENC  : MSA_I10_FMT<0b110, 0b11, 0b000111>;

class LSA_ENC : SPECIAL_LSA_FMT<0b000101>;
class DLSA_ENC : SPECIAL_DLSA_FMT<0b010101>;

class MADD_Q_H_ENC : MSA_3RF_FMT<0b0101, 0b0, 0b011100>;
class MADD_Q_W_ENC : MSA_3RF_FMT<0b0101, 0b1, 0b011100>;

class MADDR_Q_H_ENC : MSA_3RF_FMT<0b1101, 0b0, 0b011100>;
class MADDR_Q_W_ENC : MSA_3RF_FMT<0b1101, 0b1, 0b011100>;

class MADDV_B_ENC : MSA_3R_FMT<0b001, 0b00, 0b010010>;
class MADDV_H_ENC : MSA_3R_FMT<0b001, 0b01, 0b010010>;
class MADDV_W_ENC : MSA_3R_FMT<0b001, 0b10, 0b010010>;
class MADDV_D_ENC : MSA_3R_FMT<0b001, 0b11, 0b010010>;

class MAX_A_B_ENC : MSA_3R_FMT<0b110, 0b00, 0b001110>;
class MAX_A_H_ENC : MSA_3R_FMT<0b110, 0b01, 0b001110>;
class MAX_A_W_ENC : MSA_3R_FMT<0b110, 0b10, 0b001110>;
class MAX_A_D_ENC : MSA_3R_FMT<0b110, 0b11, 0b001110>;

class MAX_S_B_ENC : MSA_3R_FMT<0b010, 0b00, 0b001110>;
class MAX_S_H_ENC : MSA_3R_FMT<0b010, 0b01, 0b001110>;
class MAX_S_W_ENC : MSA_3R_FMT<0b010, 0b10, 0b001110>;
class MAX_S_D_ENC : MSA_3R_FMT<0b010, 0b11, 0b001110>;

class MAX_U_B_ENC : MSA_3R_FMT<0b011, 0b00, 0b001110>;
class MAX_U_H_ENC : MSA_3R_FMT<0b011, 0b01, 0b001110>;
class MAX_U_W_ENC : MSA_3R_FMT<0b011, 0b10, 0b001110>;
class MAX_U_D_ENC : MSA_3R_FMT<0b011, 0b11, 0b001110>;

class MAXI_S_B_ENC : MSA_I5_FMT<0b010, 0b00, 0b000110>;
class MAXI_S_H_ENC : MSA_I5_FMT<0b010, 0b01, 0b000110>;
class MAXI_S_W_ENC : MSA_I5_FMT<0b010, 0b10, 0b000110>;
class MAXI_S_D_ENC : MSA_I5_FMT<0b010, 0b11, 0b000110>;

class MAXI_U_B_ENC : MSA_I5_FMT<0b011, 0b00, 0b000110>;
class MAXI_U_H_ENC : MSA_I5_FMT<0b011, 0b01, 0b000110>;
class MAXI_U_W_ENC : MSA_I5_FMT<0b011, 0b10, 0b000110>;
class MAXI_U_D_ENC : MSA_I5_FMT<0b011, 0b11, 0b000110>;

class MIN_A_B_ENC : MSA_3R_FMT<0b111, 0b00, 0b001110>;
class MIN_A_H_ENC : MSA_3R_FMT<0b111, 0b01, 0b001110>;
class MIN_A_W_ENC : MSA_3R_FMT<0b111, 0b10, 0b001110>;
class MIN_A_D_ENC : MSA_3R_FMT<0b111, 0b11, 0b001110>;

class MIN_S_B_ENC : MSA_3R_FMT<0b100, 0b00, 0b001110>;
class MIN_S_H_ENC : MSA_3R_FMT<0b100, 0b01, 0b001110>;
class MIN_S_W_ENC : MSA_3R_FMT<0b100, 0b10, 0b001110>;
class MIN_S_D_ENC : MSA_3R_FMT<0b100, 0b11, 0b001110>;

class MIN_U_B_ENC : MSA_3R_FMT<0b101, 0b00, 0b001110>;
class MIN_U_H_ENC : MSA_3R_FMT<0b101, 0b01, 0b001110>;
class MIN_U_W_ENC : MSA_3R_FMT<0b101, 0b10, 0b001110>;
class MIN_U_D_ENC : MSA_3R_FMT<0b101, 0b11, 0b001110>;

class MINI_S_B_ENC : MSA_I5_FMT<0b100, 0b00, 0b000110>;
class MINI_S_H_ENC : MSA_I5_FMT<0b100, 0b01, 0b000110>;
class MINI_S_W_ENC : MSA_I5_FMT<0b100, 0b10, 0b000110>;
class MINI_S_D_ENC : MSA_I5_FMT<0b100, 0b11, 0b000110>;

class MINI_U_B_ENC : MSA_I5_FMT<0b101, 0b00, 0b000110>;
class MINI_U_H_ENC : MSA_I5_FMT<0b101, 0b01, 0b000110>;
class MINI_U_W_ENC : MSA_I5_FMT<0b101, 0b10, 0b000110>;
class MINI_U_D_ENC : MSA_I5_FMT<0b101, 0b11, 0b000110>;

class MOD_S_B_ENC : MSA_3R_FMT<0b110, 0b00, 0b010010>;
class MOD_S_H_ENC : MSA_3R_FMT<0b110, 0b01, 0b010010>;
class MOD_S_W_ENC : MSA_3R_FMT<0b110, 0b10, 0b010010>;
class MOD_S_D_ENC : MSA_3R_FMT<0b110, 0b11, 0b010010>;

class MOD_U_B_ENC : MSA_3R_FMT<0b111, 0b00, 0b010010>;
class MOD_U_H_ENC : MSA_3R_FMT<0b111, 0b01, 0b010010>;
class MOD_U_W_ENC : MSA_3R_FMT<0b111, 0b10, 0b010010>;
class MOD_U_D_ENC : MSA_3R_FMT<0b111, 0b11, 0b010010>;

class MOVE_V_ENC : MSA_ELM_FMT<0b0010111110, 0b011001>;

class MSUB_Q_H_ENC : MSA_3RF_FMT<0b0110, 0b0, 0b011100>;
class MSUB_Q_W_ENC : MSA_3RF_FMT<0b0110, 0b1, 0b011100>;

class MSUBR_Q_H_ENC : MSA_3RF_FMT<0b1110, 0b0, 0b011100>;
class MSUBR_Q_W_ENC : MSA_3RF_FMT<0b1110, 0b1, 0b011100>;

class MSUBV_B_ENC : MSA_3R_FMT<0b010, 0b00, 0b010010>;
class MSUBV_H_ENC : MSA_3R_FMT<0b010, 0b01, 0b010010>;
class MSUBV_W_ENC : MSA_3R_FMT<0b010, 0b10, 0b010010>;
class MSUBV_D_ENC : MSA_3R_FMT<0b010, 0b11, 0b010010>;

class MUL_Q_H_ENC : MSA_3RF_FMT<0b0100, 0b0, 0b011100>;
class MUL_Q_W_ENC : MSA_3RF_FMT<0b0100, 0b1, 0b011100>;

class MULR_Q_H_ENC : MSA_3RF_FMT<0b1100, 0b0, 0b011100>;
class MULR_Q_W_ENC : MSA_3RF_FMT<0b1100, 0b1, 0b011100>;

class MULV_B_ENC : MSA_3R_FMT<0b000, 0b00, 0b010010>;
class MULV_H_ENC : MSA_3R_FMT<0b000, 0b01, 0b010010>;
class MULV_W_ENC : MSA_3R_FMT<0b000, 0b10, 0b010010>;
class MULV_D_ENC : MSA_3R_FMT<0b000, 0b11, 0b010010>;

class NLOC_B_ENC : MSA_2R_FMT<0b11000010, 0b00, 0b011110>;
class NLOC_H_ENC : MSA_2R_FMT<0b11000010, 0b01, 0b011110>;
class NLOC_W_ENC : MSA_2R_FMT<0b11000010, 0b10, 0b011110>;
class NLOC_D_ENC : MSA_2R_FMT<0b11000010, 0b11, 0b011110>;

class NLZC_B_ENC : MSA_2R_FMT<0b11000011, 0b00, 0b011110>;
class NLZC_H_ENC : MSA_2R_FMT<0b11000011, 0b01, 0b011110>;
class NLZC_W_ENC : MSA_2R_FMT<0b11000011, 0b10, 0b011110>;
class NLZC_D_ENC : MSA_2R_FMT<0b11000011, 0b11, 0b011110>;

class NOR_V_ENC : MSA_VEC_FMT<0b00010, 0b011110>;

class NORI_B_ENC : MSA_I8_FMT<0b10, 0b000000>;

class OR_V_ENC : MSA_VEC_FMT<0b00001, 0b011110>;

class ORI_B_ENC  : MSA_I8_FMT<0b01, 0b000000>;

class PCKEV_B_ENC : MSA_3R_FMT<0b010, 0b00, 0b010100>;
class PCKEV_H_ENC : MSA_3R_FMT<0b010, 0b01, 0b010100>;
class PCKEV_W_ENC : MSA_3R_FMT<0b010, 0b10, 0b010100>;
class PCKEV_D_ENC : MSA_3R_FMT<0b010, 0b11, 0b010100>;

class PCKOD_B_ENC : MSA_3R_FMT<0b011, 0b00, 0b010100>;
class PCKOD_H_ENC : MSA_3R_FMT<0b011, 0b01, 0b010100>;
class PCKOD_W_ENC : MSA_3R_FMT<0b011, 0b10, 0b010100>;
class PCKOD_D_ENC : MSA_3R_FMT<0b011, 0b11, 0b010100>;

class PCNT_B_ENC : MSA_2R_FMT<0b11000001, 0b00, 0b011110>;
class PCNT_H_ENC : MSA_2R_FMT<0b11000001, 0b01, 0b011110>;
class PCNT_W_ENC : MSA_2R_FMT<0b11000001, 0b10, 0b011110>;
class PCNT_D_ENC : MSA_2R_FMT<0b11000001, 0b11, 0b011110>;

class SAT_S_B_ENC : MSA_BIT_B_FMT<0b000, 0b001010>;
class SAT_S_H_ENC : MSA_BIT_H_FMT<0b000, 0b001010>;
class SAT_S_W_ENC : MSA_BIT_W_FMT<0b000, 0b001010>;
class SAT_S_D_ENC : MSA_BIT_D_FMT<0b000, 0b001010>;

class SAT_U_B_ENC : MSA_BIT_B_FMT<0b001, 0b001010>;
class SAT_U_H_ENC : MSA_BIT_H_FMT<0b001, 0b001010>;
class SAT_U_W_ENC : MSA_BIT_W_FMT<0b001, 0b001010>;
class SAT_U_D_ENC : MSA_BIT_D_FMT<0b001, 0b001010>;

class SHF_B_ENC  : MSA_I8_FMT<0b00, 0b000010>;
class SHF_H_ENC  : MSA_I8_FMT<0b01, 0b000010>;
class SHF_W_ENC  : MSA_I8_FMT<0b10, 0b000010>;

class SLD_B_ENC : MSA_3R_INDEX_FMT<0b000, 0b00, 0b010100>;
class SLD_H_ENC : MSA_3R_INDEX_FMT<0b000, 0b01, 0b010100>;
class SLD_W_ENC : MSA_3R_INDEX_FMT<0b000, 0b10, 0b010100>;
class SLD_D_ENC : MSA_3R_INDEX_FMT<0b000, 0b11, 0b010100>;

class SLDI_B_ENC : MSA_ELM_B_FMT<0b0000, 0b011001>;
class SLDI_H_ENC : MSA_ELM_H_FMT<0b0000, 0b011001>;
class SLDI_W_ENC : MSA_ELM_W_FMT<0b0000, 0b011001>;
class SLDI_D_ENC : MSA_ELM_D_FMT<0b0000, 0b011001>;

class SLL_B_ENC : MSA_3R_FMT<0b000, 0b00, 0b001101>;
class SLL_H_ENC : MSA_3R_FMT<0b000, 0b01, 0b001101>;
class SLL_W_ENC : MSA_3R_FMT<0b000, 0b10, 0b001101>;
class SLL_D_ENC : MSA_3R_FMT<0b000, 0b11, 0b001101>;

class SLLI_B_ENC : MSA_BIT_B_FMT<0b000, 0b001001>;
class SLLI_H_ENC : MSA_BIT_H_FMT<0b000, 0b001001>;
class SLLI_W_ENC : MSA_BIT_W_FMT<0b000, 0b001001>;
class SLLI_D_ENC : MSA_BIT_D_FMT<0b000, 0b001001>;

class SPLAT_B_ENC : MSA_3R_INDEX_FMT<0b001, 0b00, 0b010100>;
class SPLAT_H_ENC : MSA_3R_INDEX_FMT<0b001, 0b01, 0b010100>;
class SPLAT_W_ENC : MSA_3R_INDEX_FMT<0b001, 0b10, 0b010100>;
class SPLAT_D_ENC : MSA_3R_INDEX_FMT<0b001, 0b11, 0b010100>;

class SPLATI_B_ENC : MSA_ELM_B_FMT<0b0001, 0b011001>;
class SPLATI_H_ENC : MSA_ELM_H_FMT<0b0001, 0b011001>;
class SPLATI_W_ENC : MSA_ELM_W_FMT<0b0001, 0b011001>;
class SPLATI_D_ENC : MSA_ELM_D_FMT<0b0001, 0b011001>;

class SRA_B_ENC : MSA_3R_FMT<0b001, 0b00, 0b001101>;
class SRA_H_ENC : MSA_3R_FMT<0b001, 0b01, 0b001101>;
class SRA_W_ENC : MSA_3R_FMT<0b001, 0b10, 0b001101>;
class SRA_D_ENC : MSA_3R_FMT<0b001, 0b11, 0b001101>;

class SRAI_B_ENC : MSA_BIT_B_FMT<0b001, 0b001001>;
class SRAI_H_ENC : MSA_BIT_H_FMT<0b001, 0b001001>;
class SRAI_W_ENC : MSA_BIT_W_FMT<0b001, 0b001001>;
class SRAI_D_ENC : MSA_BIT_D_FMT<0b001, 0b001001>;

class SRAR_B_ENC : MSA_3R_FMT<0b001, 0b00, 0b010101>;
class SRAR_H_ENC : MSA_3R_FMT<0b001, 0b01, 0b010101>;
class SRAR_W_ENC : MSA_3R_FMT<0b001, 0b10, 0b010101>;
class SRAR_D_ENC : MSA_3R_FMT<0b001, 0b11, 0b010101>;

class SRARI_B_ENC : MSA_BIT_B_FMT<0b010, 0b001010>;
class SRARI_H_ENC : MSA_BIT_H_FMT<0b010, 0b001010>;
class SRARI_W_ENC : MSA_BIT_W_FMT<0b010, 0b001010>;
class SRARI_D_ENC : MSA_BIT_D_FMT<0b010, 0b001010>;

class SRL_B_ENC : MSA_3R_FMT<0b010, 0b00, 0b001101>;
class SRL_H_ENC : MSA_3R_FMT<0b010, 0b01, 0b001101>;
class SRL_W_ENC : MSA_3R_FMT<0b010, 0b10, 0b001101>;
class SRL_D_ENC : MSA_3R_FMT<0b010, 0b11, 0b001101>;

class SRLI_B_ENC : MSA_BIT_B_FMT<0b010, 0b001001>;
class SRLI_H_ENC : MSA_BIT_H_FMT<0b010, 0b001001>;
class SRLI_W_ENC : MSA_BIT_W_FMT<0b010, 0b001001>;
class SRLI_D_ENC : MSA_BIT_D_FMT<0b010, 0b001001>;

class SRLR_B_ENC : MSA_3R_FMT<0b010, 0b00, 0b010101>;
class SRLR_H_ENC : MSA_3R_FMT<0b010, 0b01, 0b010101>;
class SRLR_W_ENC : MSA_3R_FMT<0b010, 0b10, 0b010101>;
class SRLR_D_ENC : MSA_3R_FMT<0b010, 0b11, 0b010101>;

class SRLRI_B_ENC : MSA_BIT_B_FMT<0b011, 0b001010>;
class SRLRI_H_ENC : MSA_BIT_H_FMT<0b011, 0b001010>;
class SRLRI_W_ENC : MSA_BIT_W_FMT<0b011, 0b001010>;
class SRLRI_D_ENC : MSA_BIT_D_FMT<0b011, 0b001010>;

class ST_B_ENC   : MSA_MI10_FMT<0b00, 0b1001>;
class ST_H_ENC   : MSA_MI10_FMT<0b01, 0b1001>;
class ST_W_ENC   : MSA_MI10_FMT<0b10, 0b1001>;
class ST_D_ENC   : MSA_MI10_FMT<0b11, 0b1001>;

class SUBS_S_B_ENC : MSA_3R_FMT<0b000, 0b00, 0b010001>;
class SUBS_S_H_ENC : MSA_3R_FMT<0b000, 0b01, 0b010001>;
class SUBS_S_W_ENC : MSA_3R_FMT<0b000, 0b10, 0b010001>;
class SUBS_S_D_ENC : MSA_3R_FMT<0b000, 0b11, 0b010001>;

class SUBS_U_B_ENC : MSA_3R_FMT<0b001, 0b00, 0b010001>;
class SUBS_U_H_ENC : MSA_3R_FMT<0b001, 0b01, 0b010001>;
class SUBS_U_W_ENC : MSA_3R_FMT<0b001, 0b10, 0b010001>;
class SUBS_U_D_ENC : MSA_3R_FMT<0b001, 0b11, 0b010001>;

class SUBSUS_U_B_ENC : MSA_3R_FMT<0b010, 0b00, 0b010001>;
class SUBSUS_U_H_ENC : MSA_3R_FMT<0b010, 0b01, 0b010001>;
class SUBSUS_U_W_ENC : MSA_3R_FMT<0b010, 0b10, 0b010001>;
class SUBSUS_U_D_ENC : MSA_3R_FMT<0b010, 0b11, 0b010001>;

class SUBSUU_S_B_ENC : MSA_3R_FMT<0b011, 0b00, 0b010001>;
class SUBSUU_S_H_ENC : MSA_3R_FMT<0b011, 0b01, 0b010001>;
class SUBSUU_S_W_ENC : MSA_3R_FMT<0b011, 0b10, 0b010001>;
class SUBSUU_S_D_ENC : MSA_3R_FMT<0b011, 0b11, 0b010001>;

class SUBV_B_ENC : MSA_3R_FMT<0b001, 0b00, 0b001110>;
class SUBV_H_ENC : MSA_3R_FMT<0b001, 0b01, 0b001110>;
class SUBV_W_ENC : MSA_3R_FMT<0b001, 0b10, 0b001110>;
class SUBV_D_ENC : MSA_3R_FMT<0b001, 0b11, 0b001110>;

class SUBVI_B_ENC : MSA_I5_FMT<0b001, 0b00, 0b000110>;
class SUBVI_H_ENC : MSA_I5_FMT<0b001, 0b01, 0b000110>;
class SUBVI_W_ENC : MSA_I5_FMT<0b001, 0b10, 0b000110>;
class SUBVI_D_ENC : MSA_I5_FMT<0b001, 0b11, 0b000110>;

class VSHF_B_ENC : MSA_3R_FMT<0b000, 0b00, 0b010101>;
class VSHF_H_ENC : MSA_3R_FMT<0b000, 0b01, 0b010101>;
class VSHF_W_ENC : MSA_3R_FMT<0b000, 0b10, 0b010101>;
class VSHF_D_ENC : MSA_3R_FMT<0b000, 0b11, 0b010101>;

class XOR_V_ENC : MSA_VEC_FMT<0b00011, 0b011110>;

class XORI_B_ENC : MSA_I8_FMT<0b11, 0b000000>;

// Instruction desc.
class MSA_BIT_B_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                          ComplexPattern Imm, RegisterOperand ROWD,
                          RegisterOperand ROWS = ROWD,
                          InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs ROWD:$wd);
  dag InOperandList = (ins ROWS:$ws, vsplat_uimm3:$m);
  string AsmString = !strconcat(instr_asm, "\t$wd, $ws, $m");
  list<dag> Pattern = [(set ROWD:$wd, (OpNode ROWS:$ws, Imm:$m))];
  InstrItinClass Itinerary = itin;
}

class MSA_BIT_H_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                          ComplexPattern Imm, RegisterOperand ROWD,
                          RegisterOperand ROWS = ROWD,
                          InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs ROWD:$wd);
  dag InOperandList = (ins ROWS:$ws, vsplat_uimm4:$m);
  string AsmString = !strconcat(instr_asm, "\t$wd, $ws, $m");
  list<dag> Pattern = [(set ROWD:$wd, (OpNode ROWS:$ws, Imm:$m))];
  InstrItinClass Itinerary = itin;
}

class MSA_BIT_W_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                          ComplexPattern Imm, RegisterOperand ROWD,
                          RegisterOperand ROWS = ROWD,
                          InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs ROWD:$wd);
  dag InOperandList = (ins ROWS:$ws, vsplat_uimm5:$m);
  string AsmString = !strconcat(instr_asm, "\t$wd, $ws, $m");
  list<dag> Pattern = [(set ROWD:$wd, (OpNode ROWS:$ws, Imm:$m))];
  InstrItinClass Itinerary = itin;
}

class MSA_BIT_D_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                          ComplexPattern Imm, RegisterOperand ROWD,
                          RegisterOperand ROWS = ROWD,
                          InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs ROWD:$wd);
  dag InOperandList = (ins ROWS:$ws, vsplat_uimm6:$m);
  string AsmString = !strconcat(instr_asm, "\t$wd, $ws, $m");
  list<dag> Pattern = [(set ROWD:$wd, (OpNode ROWS:$ws, Imm:$m))];
  InstrItinClass Itinerary = itin;
}

class MSA_BIT_X_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                          Operand ImmOp, ImmLeaf Imm, RegisterOperand ROWD,
                          RegisterOperand ROWS = ROWD,
                          InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs ROWD:$wd);
  dag InOperandList = (ins ROWS:$ws, ImmOp:$m);
  string AsmString = !strconcat(instr_asm, "\t$wd, $ws, $m");
  list<dag> Pattern = [(set ROWD:$wd, (OpNode ROWS:$ws, Imm:$m))];
  InstrItinClass Itinerary = itin;
}

class MSA_BIT_BINSXI_DESC_BASE<string instr_asm, ValueType Ty,
                               SplatComplexPattern Mask, RegisterOperand ROWD,
                               RegisterOperand ROWS = ROWD,
                               InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs ROWD:$wd);
  dag InOperandList = (ins ROWD:$wd_in, ROWS:$ws, Mask.OpClass:$m);
  string AsmString = !strconcat(instr_asm, "\t$wd, $ws, $m");
  // Note that binsxi and vselect treat the condition operand the opposite
  // way to each other.
  //   (vselect cond, if_set, if_clear)
  //   (BSEL_V cond, if_clear, if_set)
  list<dag> Pattern = [(set ROWD:$wd, (vselect (Ty Mask:$m), (Ty ROWD:$ws),
                                               ROWS:$wd_in))];
  InstrItinClass Itinerary = itin;
  string Constraints = "$wd = $wd_in";
}

class MSA_BIT_BINSLI_DESC_BASE<string instr_asm, ValueType Ty,
                               SplatComplexPattern ImmOp, RegisterOperand ROWD,
                               RegisterOperand ROWS = ROWD,
                               InstrItinClass itin = NoItinerary> :
  MSA_BIT_BINSXI_DESC_BASE<instr_asm, Ty, ImmOp, ROWD, ROWS, itin>;

class MSA_BIT_BINSRI_DESC_BASE<string instr_asm, ValueType Ty,
                               SplatComplexPattern ImmOp, RegisterOperand ROWD,
                               RegisterOperand ROWS = ROWD,
                               InstrItinClass itin = NoItinerary> :
  MSA_BIT_BINSXI_DESC_BASE<instr_asm, Ty, ImmOp, ROWD, ROWS, itin>;

class MSA_BIT_SPLAT_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                              SplatComplexPattern SplatImm,
                              RegisterOperand ROWD, RegisterOperand ROWS = ROWD,
                              InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs ROWD:$wd);
  dag InOperandList = (ins ROWS:$ws, SplatImm.OpClass:$m);
  string AsmString = !strconcat(instr_asm, "\t$wd, $ws, $m");
  list<dag> Pattern = [(set ROWD:$wd, (OpNode ROWS:$ws, SplatImm:$m))];
  InstrItinClass Itinerary = itin;
}

class MSA_COPY_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                         ValueType VecTy, Operand ImmOp, ImmLeaf Imm,
                         RegisterOperand ROD, RegisterOperand ROWS,
                         InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs ROD:$rd);
  dag InOperandList = (ins ROWS:$ws, ImmOp:$n);
  string AsmString = !strconcat(instr_asm, "\t$rd, $ws[$n]");
  list<dag> Pattern = [(set ROD:$rd, (OpNode (VecTy ROWS:$ws), Imm:$n))];
  InstrItinClass Itinerary = itin;
}

class MSA_ELM_SLD_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                            RegisterOperand ROWD, RegisterOperand ROWS,
                            Operand ImmOp, ImmLeaf Imm,
                            InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs ROWD:$wd);
  dag InOperandList = (ins ROWD:$wd_in, ROWS:$ws, ImmOp:$n);
  string AsmString = !strconcat(instr_asm, "\t$wd, $ws[$n]");
  list<dag> Pattern = [(set ROWD:$wd, (OpNode ROWD:$wd_in, ROWS:$ws,
                                              Imm:$n))];
  string Constraints = "$wd = $wd_in";
  InstrItinClass Itinerary = itin;
}

class MSA_COPY_PSEUDO_BASE<SDPatternOperator OpNode, ValueType VecTy,
                           Operand ImmOp, ImmLeaf Imm, RegisterClass RCD,
                           RegisterClass RCWS> :
      MSAPseudo<(outs RCD:$wd), (ins RCWS:$ws, ImmOp:$n),
                [(set RCD:$wd, (OpNode (VecTy RCWS:$ws), Imm:$n))]> {
  bit usesCustomInserter = 1;
  bit hasNoSchedulingInfo = 1;
}

class MSA_I5_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                       SplatComplexPattern SplatImm, RegisterOperand ROWD,
                       RegisterOperand ROWS = ROWD,
                       InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs ROWD:$wd);
  dag InOperandList = (ins ROWS:$ws, SplatImm.OpClass:$imm);
  string AsmString = !strconcat(instr_asm, "\t$wd, $ws, $imm");
  list<dag> Pattern = [(set ROWD:$wd, (OpNode ROWS:$ws, SplatImm:$imm))];
  InstrItinClass Itinerary = itin;
}

class MSA_I8_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                       SplatComplexPattern SplatImm, RegisterOperand ROWD,
                       RegisterOperand ROWS = ROWD,
                       InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs ROWD:$wd);
  dag InOperandList = (ins ROWS:$ws, SplatImm.OpClass:$u8);
  string AsmString = !strconcat(instr_asm, "\t$wd, $ws, $u8");
  list<dag> Pattern = [(set ROWD:$wd, (OpNode ROWS:$ws, SplatImm:$u8))];
  InstrItinClass Itinerary = itin;
}

class MSA_I8_SHF_DESC_BASE<string instr_asm, RegisterOperand ROWD,
                           RegisterOperand ROWS = ROWD,
                           InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs ROWD:$wd);
  dag InOperandList = (ins ROWS:$ws, uimm8:$u8);
  string AsmString = !strconcat(instr_asm, "\t$wd, $ws, $u8");
  list<dag> Pattern = [(set ROWD:$wd, (MipsSHF timmZExt8:$u8, ROWS:$ws))];
  InstrItinClass Itinerary = itin;
}

class MSA_I10_LDI_DESC_BASE<string instr_asm, RegisterOperand ROWD,
                            InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs ROWD:$wd);
  dag InOperandList = (ins vsplat_simm10:$s10);
  string AsmString = !strconcat(instr_asm, "\t$wd, $s10");
  // LDI is matched using custom matching code in MipsSEISelDAGToDAG.cpp
  list<dag> Pattern = [];
  bit hasSideEffects = 0;
  bit isReMaterializable = 1;
  InstrItinClass Itinerary = itin;
}

class MSA_2R_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                       RegisterOperand ROWD, RegisterOperand ROWS = ROWD,
                       InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs ROWD:$wd);
  dag InOperandList = (ins ROWS:$ws);
  string AsmString = !strconcat(instr_asm, "\t$wd, $ws");
  list<dag> Pattern = [(set ROWD:$wd, (OpNode ROWS:$ws))];
  InstrItinClass Itinerary = itin;
}

class MSA_2R_FILL_DESC_BASE<string instr_asm, ValueType VT,
                            SDPatternOperator OpNode, RegisterOperand ROWD,
                            RegisterOperand ROS = ROWD,
                            InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs ROWD:$wd);
  dag InOperandList = (ins ROS:$rs);
  string AsmString = !strconcat(instr_asm, "\t$wd, $rs");
  list<dag> Pattern = [(set ROWD:$wd, (VT (OpNode ROS:$rs)))];
  InstrItinClass Itinerary = itin;
}

class MSA_2R_FILL_PSEUDO_BASE<SDPatternOperator OpNode,
                              RegisterClass RCWD, RegisterClass RCWS> :
      MSAPseudo<(outs RCWD:$wd), (ins RCWS:$fs),
                [(set RCWD:$wd, (OpNode RCWS:$fs))]> {
  let usesCustomInserter = 1;
}

class MSA_2RF_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                        RegisterOperand ROWD, RegisterOperand ROWS = ROWD,
                        InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs ROWD:$wd);
  dag InOperandList = (ins ROWS:$ws);
  string AsmString = !strconcat(instr_asm, "\t$wd, $ws");
  list<dag> Pattern = [(set ROWD:$wd, (OpNode ROWS:$ws))];
  InstrItinClass Itinerary = itin;
}

class MSA_3R_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                       RegisterOperand ROWD, RegisterOperand ROWS = ROWD,
                       RegisterOperand ROWT = ROWD,
                       InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs ROWD:$wd);
  dag InOperandList = (ins ROWS:$ws, ROWT:$wt);
  string AsmString = !strconcat(instr_asm, "\t$wd, $ws, $wt");
  list<dag> Pattern = [(set ROWD:$wd, (OpNode ROWS:$ws, ROWT:$wt))];
  InstrItinClass Itinerary = itin;
}

class MSA_3R_BINSX_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                             RegisterOperand ROWD, RegisterOperand ROWS = ROWD,
                             RegisterOperand ROWT = ROWD,
                             InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs ROWD:$wd);
  dag InOperandList = (ins ROWD:$wd_in, ROWS:$ws, ROWT:$wt);
  string AsmString = !strconcat(instr_asm, "\t$wd, $ws, $wt");
  list<dag> Pattern = [(set ROWD:$wd, (OpNode ROWD:$wd_in, ROWS:$ws,
                                              ROWT:$wt))];
  string Constraints = "$wd = $wd_in";
  InstrItinClass Itinerary = itin;
}

class MSA_3R_SPLAT_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                             RegisterOperand ROWD, RegisterOperand ROWS = ROWD,
                             InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs ROWD:$wd);
  dag InOperandList = (ins ROWS:$ws, GPR32Opnd:$rt);
  string AsmString = !strconcat(instr_asm, "\t$wd, $ws[$rt]");
  list<dag> Pattern = [(set ROWD:$wd, (OpNode ROWS:$ws, GPR32Opnd:$rt))];
  InstrItinClass Itinerary = itin;
}

class MSA_3R_VSHF_DESC_BASE<string instr_asm, RegisterOperand ROWD,
                            RegisterOperand ROWS = ROWD,
                            RegisterOperand ROWT = ROWD,
                            InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs ROWD:$wd);
  dag InOperandList = (ins ROWD:$wd_in, ROWS:$ws, ROWT:$wt);
  string AsmString = !strconcat(instr_asm, "\t$wd, $ws, $wt");
  list<dag> Pattern = [(set ROWD:$wd, (MipsVSHF ROWD:$wd_in, ROWS:$ws,
                                                ROWT:$wt))];
  string Constraints = "$wd = $wd_in";
  InstrItinClass Itinerary = itin;
}

class MSA_3R_SLD_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                           RegisterOperand ROWD, RegisterOperand ROWS = ROWD,
                           InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs ROWD:$wd);
  dag InOperandList = (ins ROWD:$wd_in, ROWS:$ws, GPR32Opnd:$rt);
  string AsmString = !strconcat(instr_asm, "\t$wd, $ws[$rt]");
  list<dag> Pattern = [(set ROWD:$wd, (OpNode ROWD:$wd_in, ROWS:$ws,
                                              GPR32Opnd:$rt))];
  InstrItinClass Itinerary = itin;
  string Constraints = "$wd = $wd_in";
}

class MSA_3R_4R_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                          RegisterOperand ROWD, RegisterOperand ROWS = ROWD,
                          RegisterOperand ROWT = ROWD,
                          InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs ROWD:$wd);
  dag InOperandList = (ins ROWD:$wd_in, ROWS:$ws, ROWT:$wt);
  string AsmString = !strconcat(instr_asm, "\t$wd, $ws, $wt");
  list<dag> Pattern = [(set ROWD:$wd, (OpNode ROWD:$wd_in, ROWS:$ws,
                                              ROWT:$wt))];
  InstrItinClass Itinerary = itin;
  string Constraints = "$wd = $wd_in";
}

class MSA_3RF_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                        RegisterOperand ROWD, RegisterOperand ROWS = ROWD,
                        RegisterOperand ROWT = ROWD,
                        InstrItinClass itin = NoItinerary> :
  MSA_3R_DESC_BASE<instr_asm, OpNode, ROWD, ROWS, ROWT, itin>;

class MSA_3RF_4RF_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                            RegisterOperand ROWD, RegisterOperand ROWS = ROWD,
                            RegisterOperand ROWT = ROWD,
                            InstrItinClass itin = NoItinerary> :
  MSA_3R_4R_DESC_BASE<instr_asm, OpNode, ROWD, ROWS, ROWT, itin>;

class MSA_CBRANCH_DESC_BASE<string instr_asm, RegisterOperand ROWD> {
  dag OutOperandList = (outs);
  dag InOperandList = (ins ROWD:$wt, brtarget:$offset);
  string AsmString = !strconcat(instr_asm, "\t$wt, $offset");
  list<dag> Pattern = [];
  InstrItinClass Itinerary = NoItinerary;
  bit isBranch = 1;
  bit isTerminator = 1;
  bit hasDelaySlot = 1;
  list<Register> Defs = [AT];
}

class MSA_INSERT_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                           Operand ImmOp, ImmLeaf Imm, RegisterOperand ROWD,
                           RegisterOperand ROS,
                           InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs ROWD:$wd);
  dag InOperandList = (ins ROWD:$wd_in, ROS:$rs, ImmOp:$n);
  string AsmString = !strconcat(instr_asm, "\t$wd[$n], $rs");
  list<dag> Pattern = [(set ROWD:$wd, (OpNode ROWD:$wd_in, ROS:$rs, Imm:$n))];
  InstrItinClass Itinerary = itin;
  string Constraints = "$wd = $wd_in";
}

class MSA_INSERT_PSEUDO_BASE<SDPatternOperator OpNode, ValueType Ty,
                             Operand ImmOp, ImmLeaf Imm, RegisterOperand ROWD,
                             RegisterOperand ROFS> :
      MSAPseudo<(outs ROWD:$wd), (ins ROWD:$wd_in, ImmOp:$n, ROFS:$fs),
                [(set ROWD:$wd, (OpNode (Ty ROWD:$wd_in), ROFS:$fs, Imm:$n))]> {
  bit usesCustomInserter = 1;
  string Constraints = "$wd = $wd_in";
}

class MSA_INSERT_VIDX_PSEUDO_BASE<SDPatternOperator OpNode, ValueType Ty,
                                  RegisterOperand ROWD, RegisterOperand ROFS,
                                  RegisterOperand ROIdx> :
      MSAPseudo<(outs ROWD:$wd), (ins ROWD:$wd_in, ROIdx:$n, ROFS:$fs),
                [(set ROWD:$wd, (OpNode (Ty ROWD:$wd_in), ROFS:$fs,
                                        ROIdx:$n))]> {
  bit usesCustomInserter = 1;
  bit hasNoSchedulingInfo = 1;
  string Constraints = "$wd = $wd_in";
}

class MSA_INSVE_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                          Operand ImmOp, ImmLeaf Imm, RegisterOperand ROWD,
                          RegisterOperand ROWS = ROWD,
                          InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs ROWD:$wd);
  dag InOperandList = (ins ROWD:$wd_in, ImmOp:$n, ROWS:$ws, uimmz:$n2);
  string AsmString = !strconcat(instr_asm, "\t$wd[$n], $ws[$n2]");
  list<dag> Pattern = [(set ROWD:$wd, (OpNode ROWD:$wd_in,
                                              Imm:$n,
                                              ROWS:$ws,
                                              immz:$n2))];
  InstrItinClass Itinerary = itin;
  string Constraints = "$wd = $wd_in";
}

class MSA_VEC_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                        RegisterOperand ROWD, RegisterOperand ROWS = ROWD,
                        RegisterOperand ROWT = ROWD,
                        InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs ROWD:$wd);
  dag InOperandList = (ins ROWS:$ws, ROWT:$wt);
  string AsmString = !strconcat(instr_asm, "\t$wd, $ws, $wt");
  list<dag> Pattern = [(set ROWD:$wd, (OpNode ROWS:$ws, ROWT:$wt))];
  InstrItinClass Itinerary = itin;
}

class MSA_ELM_SPLAT_DESC_BASE<string instr_asm, SplatComplexPattern SplatImm,
                              RegisterOperand ROWD,
                              RegisterOperand ROWS = ROWD,
                              InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs ROWD:$wd);
  dag InOperandList = (ins ROWS:$ws, SplatImm.OpClass:$n);
  string AsmString = !strconcat(instr_asm, "\t$wd, $ws[$n]");
  list<dag> Pattern = [(set ROWD:$wd, (MipsVSHF SplatImm:$n, ROWS:$ws,
                                                ROWS:$ws))];
  InstrItinClass Itinerary = itin;
}

class MSA_VEC_PSEUDO_BASE<SDPatternOperator OpNode, RegisterOperand ROWD,
                          RegisterOperand ROWS = ROWD,
                          RegisterOperand ROWT = ROWD> :
      MSAPseudo<(outs ROWD:$wd), (ins ROWS:$ws, ROWT:$wt),
                [(set ROWD:$wd, (OpNode ROWS:$ws, ROWT:$wt))]>;

class ADD_A_B_DESC : MSA_3R_DESC_BASE<"add_a.b", int_mips_add_a_b, MSA128BOpnd>,
                     IsCommutable;
class ADD_A_H_DESC : MSA_3R_DESC_BASE<"add_a.h", int_mips_add_a_h, MSA128HOpnd>,
                     IsCommutable;
class ADD_A_W_DESC : MSA_3R_DESC_BASE<"add_a.w", int_mips_add_a_w, MSA128WOpnd>,
                     IsCommutable;
class ADD_A_D_DESC : MSA_3R_DESC_BASE<"add_a.d", int_mips_add_a_d, MSA128DOpnd>,
                     IsCommutable;

class ADDS_A_B_DESC : MSA_3R_DESC_BASE<"adds_a.b", int_mips_adds_a_b,
                                       MSA128BOpnd>, IsCommutable;
class ADDS_A_H_DESC : MSA_3R_DESC_BASE<"adds_a.h", int_mips_adds_a_h,
                                       MSA128HOpnd>, IsCommutable;
class ADDS_A_W_DESC : MSA_3R_DESC_BASE<"adds_a.w", int_mips_adds_a_w,
                                       MSA128WOpnd>, IsCommutable;
class ADDS_A_D_DESC : MSA_3R_DESC_BASE<"adds_a.d", int_mips_adds_a_d,
                                       MSA128DOpnd>, IsCommutable;

class ADDS_S_B_DESC : MSA_3R_DESC_BASE<"adds_s.b", int_mips_adds_s_b,
                                       MSA128BOpnd>, IsCommutable;
class ADDS_S_H_DESC : MSA_3R_DESC_BASE<"adds_s.h", int_mips_adds_s_h,
                                       MSA128HOpnd>, IsCommutable;
class ADDS_S_W_DESC : MSA_3R_DESC_BASE<"adds_s.w", int_mips_adds_s_w,
                                       MSA128WOpnd>, IsCommutable;
class ADDS_S_D_DESC : MSA_3R_DESC_BASE<"adds_s.d", int_mips_adds_s_d,
                                       MSA128DOpnd>, IsCommutable;

class ADDS_U_B_DESC : MSA_3R_DESC_BASE<"adds_u.b", int_mips_adds_u_b,
                                       MSA128BOpnd>, IsCommutable;
class ADDS_U_H_DESC : MSA_3R_DESC_BASE<"adds_u.h", int_mips_adds_u_h,
                                       MSA128HOpnd>, IsCommutable;
class ADDS_U_W_DESC : MSA_3R_DESC_BASE<"adds_u.w", int_mips_adds_u_w,
                                       MSA128WOpnd>, IsCommutable;
class ADDS_U_D_DESC : MSA_3R_DESC_BASE<"adds_u.d", int_mips_adds_u_d,
                                       MSA128DOpnd>, IsCommutable;

class ADDV_B_DESC : MSA_3R_DESC_BASE<"addv.b", add, MSA128BOpnd>, IsCommutable;
class ADDV_H_DESC : MSA_3R_DESC_BASE<"addv.h", add, MSA128HOpnd>, IsCommutable;
class ADDV_W_DESC : MSA_3R_DESC_BASE<"addv.w", add, MSA128WOpnd>, IsCommutable;
class ADDV_D_DESC : MSA_3R_DESC_BASE<"addv.d", add, MSA128DOpnd>, IsCommutable;

class ADDVI_B_DESC : MSA_I5_DESC_BASE<"addvi.b", add, vsplati8_uimm5,
                                      MSA128BOpnd>;
class ADDVI_H_DESC : MSA_I5_DESC_BASE<"addvi.h", add, vsplati16_uimm5,
                                      MSA128HOpnd>;
class ADDVI_W_DESC : MSA_I5_DESC_BASE<"addvi.w", add, vsplati32_uimm5,
                                      MSA128WOpnd>;
class ADDVI_D_DESC : MSA_I5_DESC_BASE<"addvi.d", add, vsplati64_uimm5,
                                      MSA128DOpnd>;

class AND_V_DESC : MSA_VEC_DESC_BASE<"and.v", and, MSA128BOpnd>;
class AND_V_H_PSEUDO_DESC : MSA_VEC_PSEUDO_BASE<and, MSA128HOpnd>;
class AND_V_W_PSEUDO_DESC : MSA_VEC_PSEUDO_BASE<and, MSA128WOpnd>;
class AND_V_D_PSEUDO_DESC : MSA_VEC_PSEUDO_BASE<and, MSA128DOpnd>;

class ANDI_B_DESC : MSA_I8_DESC_BASE<"andi.b", and, vsplati8_uimm8,
                                     MSA128BOpnd>;

class ASUB_S_B_DESC : MSA_3R_DESC_BASE<"asub_s.b", int_mips_asub_s_b,
                                       MSA128BOpnd>;
class ASUB_S_H_DESC : MSA_3R_DESC_BASE<"asub_s.h", int_mips_asub_s_h,
                                       MSA128HOpnd>;
class ASUB_S_W_DESC : MSA_3R_DESC_BASE<"asub_s.w", int_mips_asub_s_w,
                                       MSA128WOpnd>;
class ASUB_S_D_DESC : MSA_3R_DESC_BASE<"asub_s.d", int_mips_asub_s_d,
                                       MSA128DOpnd>;

class ASUB_U_B_DESC : MSA_3R_DESC_BASE<"asub_u.b", int_mips_asub_u_b,
                                       MSA128BOpnd>;
class ASUB_U_H_DESC : MSA_3R_DESC_BASE<"asub_u.h", int_mips_asub_u_h,
                                       MSA128HOpnd>;
class ASUB_U_W_DESC : MSA_3R_DESC_BASE<"asub_u.w", int_mips_asub_u_w,
                                       MSA128WOpnd>;
class ASUB_U_D_DESC : MSA_3R_DESC_BASE<"asub_u.d", int_mips_asub_u_d,
                                       MSA128DOpnd>;

class AVE_S_B_DESC : MSA_3R_DESC_BASE<"ave_s.b", int_mips_ave_s_b, MSA128BOpnd>,
                     IsCommutable;
class AVE_S_H_DESC : MSA_3R_DESC_BASE<"ave_s.h", int_mips_ave_s_h, MSA128HOpnd>,
                     IsCommutable;
class AVE_S_W_DESC : MSA_3R_DESC_BASE<"ave_s.w", int_mips_ave_s_w, MSA128WOpnd>,
                     IsCommutable;
class AVE_S_D_DESC : MSA_3R_DESC_BASE<"ave_s.d", int_mips_ave_s_d, MSA128DOpnd>,
                     IsCommutable;

class AVE_U_B_DESC : MSA_3R_DESC_BASE<"ave_u.b", int_mips_ave_u_b, MSA128BOpnd>,
                     IsCommutable;
class AVE_U_H_DESC : MSA_3R_DESC_BASE<"ave_u.h", int_mips_ave_u_h, MSA128HOpnd>,
                     IsCommutable;
class AVE_U_W_DESC : MSA_3R_DESC_BASE<"ave_u.w", int_mips_ave_u_w, MSA128WOpnd>,
                     IsCommutable;
class AVE_U_D_DESC : MSA_3R_DESC_BASE<"ave_u.d", int_mips_ave_u_d, MSA128DOpnd>,
                     IsCommutable;

class AVER_S_B_DESC : MSA_3R_DESC_BASE<"aver_s.b", int_mips_aver_s_b,
                                       MSA128BOpnd>, IsCommutable;
class AVER_S_H_DESC : MSA_3R_DESC_BASE<"aver_s.h", int_mips_aver_s_h,
                                       MSA128HOpnd>, IsCommutable;
class AVER_S_W_DESC : MSA_3R_DESC_BASE<"aver_s.w", int_mips_aver_s_w,
                                       MSA128WOpnd>, IsCommutable;
class AVER_S_D_DESC : MSA_3R_DESC_BASE<"aver_s.d", int_mips_aver_s_d,
                                       MSA128DOpnd>, IsCommutable;

class AVER_U_B_DESC : MSA_3R_DESC_BASE<"aver_u.b", int_mips_aver_u_b,
                                       MSA128BOpnd>, IsCommutable;
class AVER_U_H_DESC : MSA_3R_DESC_BASE<"aver_u.h", int_mips_aver_u_h,
                                       MSA128HOpnd>, IsCommutable;
class AVER_U_W_DESC : MSA_3R_DESC_BASE<"aver_u.w", int_mips_aver_u_w,
                                       MSA128WOpnd>, IsCommutable;
class AVER_U_D_DESC : MSA_3R_DESC_BASE<"aver_u.d", int_mips_aver_u_d,
                                       MSA128DOpnd>, IsCommutable;

class BCLR_B_DESC : MSA_3R_DESC_BASE<"bclr.b", vbclr_b, MSA128BOpnd>;
class BCLR_H_DESC : MSA_3R_DESC_BASE<"bclr.h", vbclr_h, MSA128HOpnd>;
class BCLR_W_DESC : MSA_3R_DESC_BASE<"bclr.w", vbclr_w, MSA128WOpnd>;
class BCLR_D_DESC : MSA_3R_DESC_BASE<"bclr.d", vbclr_d, MSA128DOpnd>;

class BCLRI_B_DESC : MSA_BIT_B_DESC_BASE<"bclri.b", and, vsplat_uimm_inv_pow2,
                                         MSA128BOpnd>;
class BCLRI_H_DESC : MSA_BIT_H_DESC_BASE<"bclri.h", and, vsplat_uimm_inv_pow2,
                                         MSA128HOpnd>;
class BCLRI_W_DESC : MSA_BIT_W_DESC_BASE<"bclri.w", and, vsplat_uimm_inv_pow2,
                                         MSA128WOpnd>;
class BCLRI_D_DESC : MSA_BIT_D_DESC_BASE<"bclri.d", and, vsplat_uimm_inv_pow2,
                                         MSA128DOpnd>;

class BINSL_B_DESC : MSA_3R_BINSX_DESC_BASE<"binsl.b", int_mips_binsl_b,
                                            MSA128BOpnd>;
class BINSL_H_DESC : MSA_3R_BINSX_DESC_BASE<"binsl.h", int_mips_binsl_h,
                                            MSA128HOpnd>;
class BINSL_W_DESC : MSA_3R_BINSX_DESC_BASE<"binsl.w", int_mips_binsl_w,
                                            MSA128WOpnd>;
class BINSL_D_DESC : MSA_3R_BINSX_DESC_BASE<"binsl.d", int_mips_binsl_d,
                                            MSA128DOpnd>;

class BINSLI_B_DESC : MSA_BIT_BINSLI_DESC_BASE<"binsli.b", v16i8, vsplat_maskl_bits_uimm3, MSA128BOpnd>;
class BINSLI_H_DESC : MSA_BIT_BINSLI_DESC_BASE<"binsli.h", v8i16, vsplat_maskl_bits_uimm4, MSA128HOpnd>;
class BINSLI_W_DESC : MSA_BIT_BINSLI_DESC_BASE<"binsli.w", v4i32, vsplat_maskl_bits_uimm5, MSA128WOpnd>;
class BINSLI_D_DESC : MSA_BIT_BINSLI_DESC_BASE<"binsli.d", v2i64, vsplat_maskl_bits_uimm6, MSA128DOpnd>;

class BINSR_B_DESC : MSA_3R_BINSX_DESC_BASE<"binsr.b", int_mips_binsr_b,
                                            MSA128BOpnd>;
class BINSR_H_DESC : MSA_3R_BINSX_DESC_BASE<"binsr.h", int_mips_binsr_h,
                                            MSA128HOpnd>;
class BINSR_W_DESC : MSA_3R_BINSX_DESC_BASE<"binsr.w", int_mips_binsr_w,
                                            MSA128WOpnd>;
class BINSR_D_DESC : MSA_3R_BINSX_DESC_BASE<"binsr.d", int_mips_binsr_d,
                                            MSA128DOpnd>;

class BINSRI_B_DESC
    : MSA_BIT_BINSRI_DESC_BASE<"binsri.b", v16i8, vsplat_maskr_bits_uimm3,
                               MSA128BOpnd>;
class BINSRI_H_DESC
    : MSA_BIT_BINSRI_DESC_BASE<"binsri.h", v8i16, vsplat_maskr_bits_uimm4,
                               MSA128HOpnd>;
class BINSRI_W_DESC
    : MSA_BIT_BINSRI_DESC_BASE<"binsri.w", v4i32, vsplat_maskr_bits_uimm5,
                               MSA128WOpnd>;
class BINSRI_D_DESC
    : MSA_BIT_BINSRI_DESC_BASE<"binsri.d", v2i64, vsplat_maskr_bits_uimm6,
                               MSA128DOpnd>;

class BMNZ_V_DESC {
  dag OutOperandList = (outs MSA128BOpnd:$wd);
  dag InOperandList = (ins MSA128BOpnd:$wd_in, MSA128BOpnd:$ws,
                       MSA128BOpnd:$wt);
  string AsmString = "bmnz.v\t$wd, $ws, $wt";
  list<dag> Pattern = [(set MSA128BOpnd:$wd, (vselect MSA128BOpnd:$wt,
                                                      MSA128BOpnd:$ws,
                                                      MSA128BOpnd:$wd_in))];
  InstrItinClass Itinerary = NoItinerary;
  string Constraints = "$wd = $wd_in";
}

class BMNZI_B_DESC {
  dag OutOperandList = (outs MSA128BOpnd:$wd);
  dag InOperandList = (ins MSA128BOpnd:$wd_in, MSA128BOpnd:$ws,
                           vsplat_uimm8:$u8);
  string AsmString = "bmnzi.b\t$wd, $ws, $u8";
  list<dag> Pattern = [(set MSA128BOpnd:$wd, (vselect vsplati8_uimm8:$u8,
                                                      MSA128BOpnd:$ws,
                                                      MSA128BOpnd:$wd_in))];
  InstrItinClass Itinerary = NoItinerary;
  string Constraints = "$wd = $wd_in";
}

class BMZ_V_DESC {
  dag OutOperandList = (outs MSA128BOpnd:$wd);
  dag InOperandList = (ins MSA128BOpnd:$wd_in, MSA128BOpnd:$ws,
                       MSA128BOpnd:$wt);
  string AsmString = "bmz.v\t$wd, $ws, $wt";
  list<dag> Pattern = [(set MSA128BOpnd:$wd, (vselect MSA128BOpnd:$wt,
                                                      MSA128BOpnd:$wd_in,
                                                      MSA128BOpnd:$ws))];
  InstrItinClass Itinerary = NoItinerary;
  string Constraints = "$wd = $wd_in";
}

class BMZI_B_DESC {
  dag OutOperandList = (outs MSA128BOpnd:$wd);
  dag InOperandList = (ins MSA128BOpnd:$wd_in, MSA128BOpnd:$ws,
                           vsplat_uimm8:$u8);
  string AsmString = "bmzi.b\t$wd, $ws, $u8";
  list<dag> Pattern = [(set MSA128BOpnd:$wd, (vselect vsplati8_uimm8:$u8,
                                                      MSA128BOpnd:$wd_in,
                                                      MSA128BOpnd:$ws))];
  InstrItinClass Itinerary = NoItinerary;
  string Constraints = "$wd = $wd_in";
}

class BNEG_B_DESC : MSA_3R_DESC_BASE<"bneg.b", vbneg_b, MSA128BOpnd>;
class BNEG_H_DESC : MSA_3R_DESC_BASE<"bneg.h", vbneg_h, MSA128HOpnd>;
class BNEG_W_DESC : MSA_3R_DESC_BASE<"bneg.w", vbneg_w, MSA128WOpnd>;
class BNEG_D_DESC : MSA_3R_DESC_BASE<"bneg.d", vbneg_d, MSA128DOpnd>;

class BNEGI_B_DESC : MSA_BIT_B_DESC_BASE<"bnegi.b", xor, vsplat_uimm_pow2,
                                         MSA128BOpnd>;
class BNEGI_H_DESC : MSA_BIT_H_DESC_BASE<"bnegi.h", xor, vsplat_uimm_pow2,
                                         MSA128HOpnd>;
class BNEGI_W_DESC : MSA_BIT_W_DESC_BASE<"bnegi.w", xor, vsplat_uimm_pow2,
                                         MSA128WOpnd>;
class BNEGI_D_DESC : MSA_BIT_D_DESC_BASE<"bnegi.d", xor, vsplat_uimm_pow2,
                                         MSA128DOpnd>;

class BNZ_B_DESC : MSA_CBRANCH_DESC_BASE<"bnz.b", MSA128BOpnd>;
class BNZ_H_DESC : MSA_CBRANCH_DESC_BASE<"bnz.h", MSA128HOpnd>;
class BNZ_W_DESC : MSA_CBRANCH_DESC_BASE<"bnz.w", MSA128WOpnd>;
class BNZ_D_DESC : MSA_CBRANCH_DESC_BASE<"bnz.d", MSA128DOpnd>;

class BNZ_V_DESC : MSA_CBRANCH_DESC_BASE<"bnz.v", MSA128BOpnd>;

class BSEL_V_DESC {
  dag OutOperandList = (outs MSA128BOpnd:$wd);
  dag InOperandList = (ins MSA128BOpnd:$wd_in, MSA128BOpnd:$ws,
                       MSA128BOpnd:$wt);
  string AsmString = "bsel.v\t$wd, $ws, $wt";
  // Note that vselect and BSEL_V treat the condition operand the opposite way
  // from each other.
  //   (vselect cond, if_set, if_clear)
  //   (BSEL_V cond, if_clear, if_set)
  list<dag> Pattern = [(set MSA128BOpnd:$wd,
                        (vselect MSA128BOpnd:$wd_in, MSA128BOpnd:$wt,
                                                     MSA128BOpnd:$ws))];
  InstrItinClass Itinerary = NoItinerary;
  string Constraints = "$wd = $wd_in";
}

class BSELI_B_DESC {
  dag OutOperandList = (outs MSA128BOpnd:$wd);
  dag InOperandList = (ins MSA128BOpnd:$wd_in, MSA128BOpnd:$ws,
                           vsplat_uimm8:$u8);
  string AsmString = "bseli.b\t$wd, $ws, $u8";
  // Note that vselect and BSEL_V treat the condition operand the opposite way
  // from each other.
  //   (vselect cond, if_set, if_clear)
  //   (BSEL_V cond, if_clear, if_set)
  list<dag> Pattern = [(set MSA128BOpnd:$wd, (vselect MSA128BOpnd:$wd_in,
                                                      vsplati8_uimm8:$u8,
                                                      MSA128BOpnd:$ws))];
  InstrItinClass Itinerary = NoItinerary;
  string Constraints = "$wd = $wd_in";
}

class BSET_B_DESC : MSA_3R_DESC_BASE<"bset.b", vbset_b, MSA128BOpnd>;
class BSET_H_DESC : MSA_3R_DESC_BASE<"bset.h", vbset_h, MSA128HOpnd>;
class BSET_W_DESC : MSA_3R_DESC_BASE<"bset.w", vbset_w, MSA128WOpnd>;
class BSET_D_DESC : MSA_3R_DESC_BASE<"bset.d", vbset_d, MSA128DOpnd>;

class BSETI_B_DESC : MSA_BIT_B_DESC_BASE<"bseti.b", or, vsplat_uimm_pow2,
                                         MSA128BOpnd>;
class BSETI_H_DESC : MSA_BIT_H_DESC_BASE<"bseti.h", or, vsplat_uimm_pow2,
                                         MSA128HOpnd>;
class BSETI_W_DESC : MSA_BIT_W_DESC_BASE<"bseti.w", or, vsplat_uimm_pow2,
                                         MSA128WOpnd>;
class BSETI_D_DESC : MSA_BIT_D_DESC_BASE<"bseti.d", or, vsplat_uimm_pow2,
                                         MSA128DOpnd>;

class BZ_B_DESC : MSA_CBRANCH_DESC_BASE<"bz.b", MSA128BOpnd>;
class BZ_H_DESC : MSA_CBRANCH_DESC_BASE<"bz.h", MSA128HOpnd>;
class BZ_W_DESC : MSA_CBRANCH_DESC_BASE<"bz.w", MSA128WOpnd>;
class BZ_D_DESC : MSA_CBRANCH_DESC_BASE<"bz.d", MSA128DOpnd>;

class BZ_V_DESC : MSA_CBRANCH_DESC_BASE<"bz.v", MSA128BOpnd>;

class CEQ_B_DESC : MSA_3R_DESC_BASE<"ceq.b", vseteq_v16i8, MSA128BOpnd>,
                   IsCommutable;
class CEQ_H_DESC : MSA_3R_DESC_BASE<"ceq.h", vseteq_v8i16, MSA128HOpnd>,
                   IsCommutable;
class CEQ_W_DESC : MSA_3R_DESC_BASE<"ceq.w", vseteq_v4i32, MSA128WOpnd>,
                   IsCommutable;
class CEQ_D_DESC : MSA_3R_DESC_BASE<"ceq.d", vseteq_v2i64, MSA128DOpnd>,
                   IsCommutable;

class CEQI_B_DESC : MSA_I5_DESC_BASE<"ceqi.b", vseteq_v16i8, vsplati8_simm5,
                                     MSA128BOpnd>;
class CEQI_H_DESC : MSA_I5_DESC_BASE<"ceqi.h", vseteq_v8i16, vsplati16_simm5,
                                     MSA128HOpnd>;
class CEQI_W_DESC : MSA_I5_DESC_BASE<"ceqi.w", vseteq_v4i32, vsplati32_simm5,
                                     MSA128WOpnd>;
class CEQI_D_DESC : MSA_I5_DESC_BASE<"ceqi.d", vseteq_v2i64, vsplati64_simm5,
                                     MSA128DOpnd>;

class CFCMSA_DESC {
  dag OutOperandList = (outs GPR32Opnd:$rd);
  dag InOperandList = (ins MSA128CROpnd:$cs);
  string AsmString = "cfcmsa\t$rd, $cs";
  InstrItinClass Itinerary = NoItinerary;
  bit hasSideEffects = 1;
  bit isMoveReg = 1;
}

class CLE_S_B_DESC : MSA_3R_DESC_BASE<"cle_s.b", vsetle_v16i8, MSA128BOpnd>;
class CLE_S_H_DESC : MSA_3R_DESC_BASE<"cle_s.h", vsetle_v8i16, MSA128HOpnd>;
class CLE_S_W_DESC : MSA_3R_DESC_BASE<"cle_s.w", vsetle_v4i32, MSA128WOpnd>;
class CLE_S_D_DESC : MSA_3R_DESC_BASE<"cle_s.d", vsetle_v2i64, MSA128DOpnd>;

class CLE_U_B_DESC : MSA_3R_DESC_BASE<"cle_u.b", vsetule_v16i8, MSA128BOpnd>;
class CLE_U_H_DESC : MSA_3R_DESC_BASE<"cle_u.h", vsetule_v8i16, MSA128HOpnd>;
class CLE_U_W_DESC : MSA_3R_DESC_BASE<"cle_u.w", vsetule_v4i32, MSA128WOpnd>;
class CLE_U_D_DESC : MSA_3R_DESC_BASE<"cle_u.d", vsetule_v2i64, MSA128DOpnd>;

class CLEI_S_B_DESC : MSA_I5_DESC_BASE<"clei_s.b", vsetle_v16i8,
                                       vsplati8_simm5,  MSA128BOpnd>;
class CLEI_S_H_DESC : MSA_I5_DESC_BASE<"clei_s.h", vsetle_v8i16,
                                       vsplati16_simm5, MSA128HOpnd>;
class CLEI_S_W_DESC : MSA_I5_DESC_BASE<"clei_s.w", vsetle_v4i32,
                                       vsplati32_simm5, MSA128WOpnd>;
class CLEI_S_D_DESC : MSA_I5_DESC_BASE<"clei_s.d", vsetle_v2i64,
                                       vsplati64_simm5, MSA128DOpnd>;

class CLEI_U_B_DESC : MSA_I5_DESC_BASE<"clei_u.b", vsetule_v16i8,
                                       vsplati8_uimm5,  MSA128BOpnd>;
class CLEI_U_H_DESC : MSA_I5_DESC_BASE<"clei_u.h", vsetule_v8i16,
                                       vsplati16_uimm5, MSA128HOpnd>;
class CLEI_U_W_DESC : MSA_I5_DESC_BASE<"clei_u.w", vsetule_v4i32,
                                       vsplati32_uimm5, MSA128WOpnd>;
class CLEI_U_D_DESC : MSA_I5_DESC_BASE<"clei_u.d", vsetule_v2i64,
                                       vsplati64_uimm5, MSA128DOpnd>;

class CLT_S_B_DESC : MSA_3R_DESC_BASE<"clt_s.b", vsetlt_v16i8, MSA128BOpnd>;
class CLT_S_H_DESC : MSA_3R_DESC_BASE<"clt_s.h", vsetlt_v8i16, MSA128HOpnd>;
class CLT_S_W_DESC : MSA_3R_DESC_BASE<"clt_s.w", vsetlt_v4i32, MSA128WOpnd>;
class CLT_S_D_DESC : MSA_3R_DESC_BASE<"clt_s.d", vsetlt_v2i64, MSA128DOpnd>;

class CLT_U_B_DESC : MSA_3R_DESC_BASE<"clt_u.b", vsetult_v16i8, MSA128BOpnd>;
class CLT_U_H_DESC : MSA_3R_DESC_BASE<"clt_u.h", vsetult_v8i16, MSA128HOpnd>;
class CLT_U_W_DESC : MSA_3R_DESC_BASE<"clt_u.w", vsetult_v4i32, MSA128WOpnd>;
class CLT_U_D_DESC : MSA_3R_DESC_BASE<"clt_u.d", vsetult_v2i64, MSA128DOpnd>;

class CLTI_S_B_DESC : MSA_I5_DESC_BASE<"clti_s.b", vsetlt_v16i8,
                                       vsplati8_simm5, MSA128BOpnd>;
class CLTI_S_H_DESC : MSA_I5_DESC_BASE<"clti_s.h", vsetlt_v8i16,
                                       vsplati16_simm5, MSA128HOpnd>;
class CLTI_S_W_DESC : MSA_I5_DESC_BASE<"clti_s.w", vsetlt_v4i32,
                                       vsplati32_simm5, MSA128WOpnd>;
class CLTI_S_D_DESC : MSA_I5_DESC_BASE<"clti_s.d", vsetlt_v2i64,
                                       vsplati64_simm5, MSA128DOpnd>;

class CLTI_U_B_DESC : MSA_I5_DESC_BASE<"clti_u.b", vsetult_v16i8,
                                       vsplati8_uimm5, MSA128BOpnd>;
class CLTI_U_H_DESC : MSA_I5_DESC_BASE<"clti_u.h", vsetult_v8i16,
                                       vsplati16_uimm5, MSA128HOpnd>;
class CLTI_U_W_DESC : MSA_I5_DESC_BASE<"clti_u.w", vsetult_v4i32,
                                       vsplati32_uimm5, MSA128WOpnd>;
class CLTI_U_D_DESC : MSA_I5_DESC_BASE<"clti_u.d", vsetult_v2i64,
                                       vsplati64_uimm5, MSA128DOpnd>;

class COPY_S_B_DESC : MSA_COPY_DESC_BASE<"copy_s.b", vextract_sext_i8,  v16i8,
                                         uimm4_ptr, immZExt4Ptr, GPR32Opnd,
                                         MSA128BOpnd>;
class COPY_S_H_DESC : MSA_COPY_DESC_BASE<"copy_s.h", vextract_sext_i16, v8i16,
                                         uimm3_ptr, immZExt3Ptr, GPR32Opnd,
                                         MSA128HOpnd>;
class COPY_S_W_DESC : MSA_COPY_DESC_BASE<"copy_s.w", vextract_sext_i32, v4i32,
                                         uimm2_ptr, immZExt2Ptr, GPR32Opnd,
                                         MSA128WOpnd>;
class COPY_S_D_DESC : MSA_COPY_DESC_BASE<"copy_s.d", vextract_sext_i64, v2i64,
                                         uimm1_ptr, immZExt1Ptr, GPR64Opnd,
                                         MSA128DOpnd>;

class COPY_U_B_DESC : MSA_COPY_DESC_BASE<"copy_u.b", vextract_zext_i8,  v16i8,
                                         uimm4_ptr, immZExt4Ptr, GPR32Opnd,
                                         MSA128BOpnd>;
class COPY_U_H_DESC : MSA_COPY_DESC_BASE<"copy_u.h", vextract_zext_i16, v8i16,
                                         uimm3_ptr, immZExt3Ptr, GPR32Opnd,
                                         MSA128HOpnd>;
class COPY_U_W_DESC : MSA_COPY_DESC_BASE<"copy_u.w", vextract_zext_i32, v4i32,
                                         uimm2_ptr, immZExt2Ptr, GPR32Opnd,
                                         MSA128WOpnd>;

class COPY_FW_PSEUDO_DESC : MSA_COPY_PSEUDO_BASE<vector_extract, v4f32,
                                                 uimm2_ptr, immZExt2Ptr, FGR32,
                                                 MSA128W>;
class COPY_FD_PSEUDO_DESC : MSA_COPY_PSEUDO_BASE<vector_extract, v2f64,
                                                 uimm1_ptr, immZExt1Ptr, FGR64,
                                                 MSA128D>;

class CTCMSA_DESC {
  dag OutOperandList = (outs);
  dag InOperandList = (ins MSA128CROpnd:$cd, GPR32Opnd:$rs);
  string AsmString = "ctcmsa\t$cd, $rs";
  InstrItinClass Itinerary = NoItinerary;
  bit hasSideEffects = 1;
  bit isMoveReg = 1;
}

class DIV_S_B_DESC : MSA_3R_DESC_BASE<"div_s.b", sdiv, MSA128BOpnd>;
class DIV_S_H_DESC : MSA_3R_DESC_BASE<"div_s.h", sdiv, MSA128HOpnd>;
class DIV_S_W_DESC : MSA_3R_DESC_BASE<"div_s.w", sdiv, MSA128WOpnd>;
class DIV_S_D_DESC : MSA_3R_DESC_BASE<"div_s.d", sdiv, MSA128DOpnd>;

class DIV_U_B_DESC : MSA_3R_DESC_BASE<"div_u.b", udiv, MSA128BOpnd>;
class DIV_U_H_DESC : MSA_3R_DESC_BASE<"div_u.h", udiv, MSA128HOpnd>;
class DIV_U_W_DESC : MSA_3R_DESC_BASE<"div_u.w", udiv, MSA128WOpnd>;
class DIV_U_D_DESC : MSA_3R_DESC_BASE<"div_u.d", udiv, MSA128DOpnd>;

class DOTP_S_H_DESC : MSA_3R_DESC_BASE<"dotp_s.h", int_mips_dotp_s_h,
                                       MSA128HOpnd, MSA128BOpnd, MSA128BOpnd>,
                      IsCommutable;
class DOTP_S_W_DESC : MSA_3R_DESC_BASE<"dotp_s.w", int_mips_dotp_s_w,
                                       MSA128WOpnd, MSA128HOpnd, MSA128HOpnd>,
                      IsCommutable;
class DOTP_S_D_DESC : MSA_3R_DESC_BASE<"dotp_s.d", int_mips_dotp_s_d,
                                       MSA128DOpnd, MSA128WOpnd, MSA128WOpnd>,
                      IsCommutable;

class DOTP_U_H_DESC : MSA_3R_DESC_BASE<"dotp_u.h", int_mips_dotp_u_h,
                                       MSA128HOpnd, MSA128BOpnd, MSA128BOpnd>,
                      IsCommutable;
class DOTP_U_W_DESC : MSA_3R_DESC_BASE<"dotp_u.w", int_mips_dotp_u_w,
                                       MSA128WOpnd, MSA128HOpnd, MSA128HOpnd>,
                      IsCommutable;
class DOTP_U_D_DESC : MSA_3R_DESC_BASE<"dotp_u.d", int_mips_dotp_u_d,
                                       MSA128DOpnd, MSA128WOpnd, MSA128WOpnd>,
                      IsCommutable;

class DPADD_S_H_DESC : MSA_3R_4R_DESC_BASE<"dpadd_s.h", int_mips_dpadd_s_h,
                                           MSA128HOpnd, MSA128BOpnd,
                                           MSA128BOpnd>, IsCommutable;
class DPADD_S_W_DESC : MSA_3R_4R_DESC_BASE<"dpadd_s.w", int_mips_dpadd_s_w,
                                           MSA128WOpnd, MSA128HOpnd,
                                           MSA128HOpnd>, IsCommutable;
class DPADD_S_D_DESC : MSA_3R_4R_DESC_BASE<"dpadd_s.d", int_mips_dpadd_s_d,
                                           MSA128DOpnd, MSA128WOpnd,
                                           MSA128WOpnd>, IsCommutable;

class DPADD_U_H_DESC : MSA_3R_4R_DESC_BASE<"dpadd_u.h", int_mips_dpadd_u_h,
                                           MSA128HOpnd, MSA128BOpnd,
                                           MSA128BOpnd>, IsCommutable;
class DPADD_U_W_DESC : MSA_3R_4R_DESC_BASE<"dpadd_u.w", int_mips_dpadd_u_w,
                                           MSA128WOpnd, MSA128HOpnd,
                                           MSA128HOpnd>, IsCommutable;
class DPADD_U_D_DESC : MSA_3R_4R_DESC_BASE<"dpadd_u.d", int_mips_dpadd_u_d,
                                           MSA128DOpnd, MSA128WOpnd,
                                           MSA128WOpnd>, IsCommutable;

class DPSUB_S_H_DESC : MSA_3R_4R_DESC_BASE<"dpsub_s.h", int_mips_dpsub_s_h,
                                           MSA128HOpnd, MSA128BOpnd,
                                           MSA128BOpnd>;
class DPSUB_S_W_DESC : MSA_3R_4R_DESC_BASE<"dpsub_s.w", int_mips_dpsub_s_w,
                                           MSA128WOpnd, MSA128HOpnd,
                                           MSA128HOpnd>;
class DPSUB_S_D_DESC : MSA_3R_4R_DESC_BASE<"dpsub_s.d", int_mips_dpsub_s_d,
                                           MSA128DOpnd, MSA128WOpnd,
                                           MSA128WOpnd>;

class DPSUB_U_H_DESC : MSA_3R_4R_DESC_BASE<"dpsub_u.h", int_mips_dpsub_u_h,
                                           MSA128HOpnd, MSA128BOpnd,
                                           MSA128BOpnd>;
class DPSUB_U_W_DESC : MSA_3R_4R_DESC_BASE<"dpsub_u.w", int_mips_dpsub_u_w,
                                           MSA128WOpnd, MSA128HOpnd,
                                           MSA128HOpnd>;
class DPSUB_U_D_DESC : MSA_3R_4R_DESC_BASE<"dpsub_u.d", int_mips_dpsub_u_d,
                                           MSA128DOpnd, MSA128WOpnd,
                                           MSA128WOpnd>;

class FADD_W_DESC : MSA_3RF_DESC_BASE<"fadd.w", fadd, MSA128WOpnd>,
                    IsCommutable;
class FADD_D_DESC : MSA_3RF_DESC_BASE<"fadd.d", fadd, MSA128DOpnd>,
                    IsCommutable;

class FCAF_W_DESC : MSA_3RF_DESC_BASE<"fcaf.w", int_mips_fcaf_w, MSA128WOpnd>,
                    IsCommutable;
class FCAF_D_DESC : MSA_3RF_DESC_BASE<"fcaf.d", int_mips_fcaf_d, MSA128DOpnd>,
                    IsCommutable;

class FCEQ_W_DESC : MSA_3RF_DESC_BASE<"fceq.w", vfsetoeq_v4f32, MSA128WOpnd>,
                    IsCommutable;
class FCEQ_D_DESC : MSA_3RF_DESC_BASE<"fceq.d", vfsetoeq_v2f64, MSA128DOpnd>,
                    IsCommutable;

class FCLASS_W_DESC : MSA_2RF_DESC_BASE<"fclass.w", int_mips_fclass_w,
                                        MSA128WOpnd>;
class FCLASS_D_DESC : MSA_2RF_DESC_BASE<"fclass.d", int_mips_fclass_d,
                                        MSA128DOpnd>;

class FCLE_W_DESC : MSA_3RF_DESC_BASE<"fcle.w", vfsetole_v4f32, MSA128WOpnd>;
class FCLE_D_DESC : MSA_3RF_DESC_BASE<"fcle.d", vfsetole_v2f64, MSA128DOpnd>;

class FCLT_W_DESC : MSA_3RF_DESC_BASE<"fclt.w", vfsetolt_v4f32, MSA128WOpnd>;
class FCLT_D_DESC : MSA_3RF_DESC_BASE<"fclt.d", vfsetolt_v2f64, MSA128DOpnd>;

class FCNE_W_DESC : MSA_3RF_DESC_BASE<"fcne.w", vfsetone_v4f32, MSA128WOpnd>,
                    IsCommutable;
class FCNE_D_DESC : MSA_3RF_DESC_BASE<"fcne.d", vfsetone_v2f64, MSA128DOpnd>,
                    IsCommutable;

class FCOR_W_DESC : MSA_3RF_DESC_BASE<"fcor.w", vfsetord_v4f32, MSA128WOpnd>,
                    IsCommutable;
class FCOR_D_DESC : MSA_3RF_DESC_BASE<"fcor.d", vfsetord_v2f64, MSA128DOpnd>,
                    IsCommutable;

class FCUEQ_W_DESC : MSA_3RF_DESC_BASE<"fcueq.w", vfsetueq_v4f32, MSA128WOpnd>,
                     IsCommutable;
class FCUEQ_D_DESC : MSA_3RF_DESC_BASE<"fcueq.d", vfsetueq_v2f64, MSA128DOpnd>,
                     IsCommutable;

class FCULE_W_DESC : MSA_3RF_DESC_BASE<"fcule.w", vfsetule_v4f32, MSA128WOpnd>,
                     IsCommutable;
class FCULE_D_DESC : MSA_3RF_DESC_BASE<"fcule.d", vfsetule_v2f64, MSA128DOpnd>,
                     IsCommutable;

class FCULT_W_DESC : MSA_3RF_DESC_BASE<"fcult.w", vfsetult_v4f32, MSA128WOpnd>,
                     IsCommutable;
class FCULT_D_DESC : MSA_3RF_DESC_BASE<"fcult.d", vfsetult_v2f64, MSA128DOpnd>,
                     IsCommutable;

class FCUN_W_DESC : MSA_3RF_DESC_BASE<"fcun.w", vfsetun_v4f32, MSA128WOpnd>,
                    IsCommutable;
class FCUN_D_DESC : MSA_3RF_DESC_BASE<"fcun.d", vfsetun_v2f64, MSA128DOpnd>,
                    IsCommutable;

class FCUNE_W_DESC : MSA_3RF_DESC_BASE<"fcune.w", vfsetune_v4f32, MSA128WOpnd>,
                     IsCommutable;
class FCUNE_D_DESC : MSA_3RF_DESC_BASE<"fcune.d", vfsetune_v2f64, MSA128DOpnd>,
                     IsCommutable;

class FDIV_W_DESC : MSA_3RF_DESC_BASE<"fdiv.w", fdiv, MSA128WOpnd>;
class FDIV_D_DESC : MSA_3RF_DESC_BASE<"fdiv.d", fdiv, MSA128DOpnd>;

class FEXDO_H_DESC : MSA_3RF_DESC_BASE<"fexdo.h", int_mips_fexdo_h,
                                       MSA128HOpnd, MSA128WOpnd, MSA128WOpnd>;
class FEXDO_W_DESC : MSA_3RF_DESC_BASE<"fexdo.w", int_mips_fexdo_w,
                                       MSA128WOpnd, MSA128DOpnd, MSA128DOpnd>;

// The fexp2.df instruction multiplies the first operand by 2 to the power of
// the second operand. We therefore need a pseudo-insn in order to invent the
// 1.0 when we only need to match ISD::FEXP2.
class FEXP2_W_DESC : MSA_3RF_DESC_BASE<"fexp2.w", mul_fexp2, MSA128WOpnd>;
class FEXP2_D_DESC : MSA_3RF_DESC_BASE<"fexp2.d", mul_fexp2, MSA128DOpnd>;
let usesCustomInserter = 1, hasNoSchedulingInfo = 1 in {
  class FEXP2_W_1_PSEUDO_DESC :
      MSAPseudo<(outs MSA128W:$wd), (ins MSA128W:$ws),
                [(set MSA128W:$wd, (fexp2 MSA128W:$ws))]>;
  class FEXP2_D_1_PSEUDO_DESC :
      MSAPseudo<(outs MSA128D:$wd), (ins MSA128D:$ws),
                [(set MSA128D:$wd, (fexp2 MSA128D:$ws))]>;
}

class FEXUPL_W_DESC : MSA_2RF_DESC_BASE<"fexupl.w", int_mips_fexupl_w,
                                        MSA128WOpnd, MSA128HOpnd>;
class FEXUPL_D_DESC : MSA_2RF_DESC_BASE<"fexupl.d", int_mips_fexupl_d,
                                        MSA128DOpnd, MSA128WOpnd>;

class FEXUPR_W_DESC : MSA_2RF_DESC_BASE<"fexupr.w", int_mips_fexupr_w,
                                        MSA128WOpnd, MSA128HOpnd>;
class FEXUPR_D_DESC : MSA_2RF_DESC_BASE<"fexupr.d", int_mips_fexupr_d,
                                        MSA128DOpnd, MSA128WOpnd>;

class FFINT_S_W_DESC : MSA_2RF_DESC_BASE<"ffint_s.w", sint_to_fp, MSA128WOpnd>;
class FFINT_S_D_DESC : MSA_2RF_DESC_BASE<"ffint_s.d", sint_to_fp, MSA128DOpnd>;

class FFINT_U_W_DESC : MSA_2RF_DESC_BASE<"ffint_u.w", uint_to_fp, MSA128WOpnd>;
class FFINT_U_D_DESC : MSA_2RF_DESC_BASE<"ffint_u.d", uint_to_fp, MSA128DOpnd>;

class FFQL_W_DESC : MSA_2RF_DESC_BASE<"ffql.w", int_mips_ffql_w,
                                      MSA128WOpnd, MSA128HOpnd>;
class FFQL_D_DESC : MSA_2RF_DESC_BASE<"ffql.d", int_mips_ffql_d,
                                      MSA128DOpnd, MSA128WOpnd>;

class FFQR_W_DESC : MSA_2RF_DESC_BASE<"ffqr.w", int_mips_ffqr_w,
                                      MSA128WOpnd, MSA128HOpnd>;
class FFQR_D_DESC : MSA_2RF_DESC_BASE<"ffqr.d", int_mips_ffqr_d,
                                      MSA128DOpnd, MSA128WOpnd>;

class FILL_B_DESC : MSA_2R_FILL_DESC_BASE<"fill.b", v16i8, vsplati8,
                                          MSA128BOpnd, GPR32Opnd>;
class FILL_H_DESC : MSA_2R_FILL_DESC_BASE<"fill.h", v8i16, vsplati16,
                                          MSA128HOpnd, GPR32Opnd>;
class FILL_W_DESC : MSA_2R_FILL_DESC_BASE<"fill.w", v4i32, vsplati32,
                                          MSA128WOpnd, GPR32Opnd>;
class FILL_D_DESC : MSA_2R_FILL_DESC_BASE<"fill.d", v2i64, vsplati64,
                                          MSA128DOpnd, GPR64Opnd>;

class FILL_FW_PSEUDO_DESC : MSA_2R_FILL_PSEUDO_BASE<vsplatf32, MSA128W, FGR32>;
class FILL_FD_PSEUDO_DESC : MSA_2R_FILL_PSEUDO_BASE<vsplatf64, MSA128D, FGR64>;

class FLOG2_W_DESC : MSA_2RF_DESC_BASE<"flog2.w", flog2, MSA128WOpnd>;
class FLOG2_D_DESC : MSA_2RF_DESC_BASE<"flog2.d", flog2, MSA128DOpnd>;

class FMADD_W_DESC : MSA_3RF_4RF_DESC_BASE<"fmadd.w", fma, MSA128WOpnd>;
class FMADD_D_DESC : MSA_3RF_4RF_DESC_BASE<"fmadd.d", fma, MSA128DOpnd>;

class FMAX_W_DESC : MSA_3RF_DESC_BASE<"fmax.w", int_mips_fmax_w, MSA128WOpnd>;
class FMAX_D_DESC : MSA_3RF_DESC_BASE<"fmax.d", int_mips_fmax_d, MSA128DOpnd>;

class FMAX_A_W_DESC : MSA_3RF_DESC_BASE<"fmax_a.w", int_mips_fmax_a_w,
                                        MSA128WOpnd>;
class FMAX_A_D_DESC : MSA_3RF_DESC_BASE<"fmax_a.d", int_mips_fmax_a_d,
                                        MSA128DOpnd>;

class FMIN_W_DESC : MSA_3RF_DESC_BASE<"fmin.w", int_mips_fmin_w, MSA128WOpnd>;
class FMIN_D_DESC : MSA_3RF_DESC_BASE<"fmin.d", int_mips_fmin_d, MSA128DOpnd>;

class FMIN_A_W_DESC : MSA_3RF_DESC_BASE<"fmin_a.w", int_mips_fmin_a_w,
                                        MSA128WOpnd>;
class FMIN_A_D_DESC : MSA_3RF_DESC_BASE<"fmin_a.d", int_mips_fmin_a_d,
                                        MSA128DOpnd>;

class FMSUB_W_DESC : MSA_3RF_4RF_DESC_BASE<"fmsub.w", MipsFMS, MSA128WOpnd>;
class FMSUB_D_DESC : MSA_3RF_4RF_DESC_BASE<"fmsub.d", MipsFMS, MSA128DOpnd>;

class FMUL_W_DESC : MSA_3RF_DESC_BASE<"fmul.w", fmul, MSA128WOpnd>;
class FMUL_D_DESC : MSA_3RF_DESC_BASE<"fmul.d", fmul, MSA128DOpnd>;

class FRINT_W_DESC : MSA_2RF_DESC_BASE<"frint.w", frint, MSA128WOpnd>;
class FRINT_D_DESC : MSA_2RF_DESC_BASE<"frint.d", frint, MSA128DOpnd>;

class FRCP_W_DESC : MSA_2RF_DESC_BASE<"frcp.w", int_mips_frcp_w, MSA128WOpnd>;
class FRCP_D_DESC : MSA_2RF_DESC_BASE<"frcp.d", int_mips_frcp_d, MSA128DOpnd>;

class FRSQRT_W_DESC : MSA_2RF_DESC_BASE<"frsqrt.w", int_mips_frsqrt_w,
                                        MSA128WOpnd>;
class FRSQRT_D_DESC : MSA_2RF_DESC_BASE<"frsqrt.d", int_mips_frsqrt_d,
                                        MSA128DOpnd>;

class FSAF_W_DESC : MSA_3RF_DESC_BASE<"fsaf.w", int_mips_fsaf_w, MSA128WOpnd>;
class FSAF_D_DESC : MSA_3RF_DESC_BASE<"fsaf.d", int_mips_fsaf_d, MSA128DOpnd>;

class FSEQ_W_DESC : MSA_3RF_DESC_BASE<"fseq.w", int_mips_fseq_w, MSA128WOpnd>;
class FSEQ_D_DESC : MSA_3RF_DESC_BASE<"fseq.d", int_mips_fseq_d, MSA128DOpnd>;

class FSLE_W_DESC : MSA_3RF_DESC_BASE<"fsle.w", int_mips_fsle_w, MSA128WOpnd>;
class FSLE_D_DESC : MSA_3RF_DESC_BASE<"fsle.d", int_mips_fsle_d, MSA128DOpnd>;

class FSLT_W_DESC : MSA_3RF_DESC_BASE<"fslt.w", int_mips_fslt_w, MSA128WOpnd>;
class FSLT_D_DESC : MSA_3RF_DESC_BASE<"fslt.d", int_mips_fslt_d, MSA128DOpnd>;

class FSNE_W_DESC : MSA_3RF_DESC_BASE<"fsne.w", int_mips_fsne_w, MSA128WOpnd>;
class FSNE_D_DESC : MSA_3RF_DESC_BASE<"fsne.d", int_mips_fsne_d, MSA128DOpnd>;

class FSOR_W_DESC : MSA_3RF_DESC_BASE<"fsor.w", int_mips_fsor_w, MSA128WOpnd>;
class FSOR_D_DESC : MSA_3RF_DESC_BASE<"fsor.d", int_mips_fsor_d, MSA128DOpnd>;

class FSQRT_W_DESC : MSA_2RF_DESC_BASE<"fsqrt.w", fsqrt, MSA128WOpnd>;
class FSQRT_D_DESC : MSA_2RF_DESC_BASE<"fsqrt.d", fsqrt, MSA128DOpnd>;

class FSUB_W_DESC : MSA_3RF_DESC_BASE<"fsub.w", fsub, MSA128WOpnd>;
class FSUB_D_DESC : MSA_3RF_DESC_BASE<"fsub.d", fsub, MSA128DOpnd>;

class FSUEQ_W_DESC : MSA_3RF_DESC_BASE<"fsueq.w", int_mips_fsueq_w,
                                       MSA128WOpnd>;
class FSUEQ_D_DESC : MSA_3RF_DESC_BASE<"fsueq.d", int_mips_fsueq_d,
                                       MSA128DOpnd>;

class FSULE_W_DESC : MSA_3RF_DESC_BASE<"fsule.w", int_mips_fsule_w,
                                       MSA128WOpnd>;
class FSULE_D_DESC : MSA_3RF_DESC_BASE<"fsule.d", int_mips_fsule_d,
                                       MSA128DOpnd>;

class FSULT_W_DESC : MSA_3RF_DESC_BASE<"fsult.w", int_mips_fsult_w,
                                       MSA128WOpnd>;
class FSULT_D_DESC : MSA_3RF_DESC_BASE<"fsult.d", int_mips_fsult_d,
                                       MSA128DOpnd>;

class FSUN_W_DESC : MSA_3RF_DESC_BASE<"fsun.w", int_mips_fsun_w,
                                      MSA128WOpnd>;
class FSUN_D_DESC : MSA_3RF_DESC_BASE<"fsun.d", int_mips_fsun_d,
                                      MSA128DOpnd>;

class FSUNE_W_DESC : MSA_3RF_DESC_BASE<"fsune.w", int_mips_fsune_w,
                                       MSA128WOpnd>;
class FSUNE_D_DESC : MSA_3RF_DESC_BASE<"fsune.d", int_mips_fsune_d,
                                       MSA128DOpnd>;

class FTINT_S_W_DESC : MSA_2RF_DESC_BASE<"ftint_s.w", int_mips_ftint_s_w,
                                         MSA128WOpnd>;
class FTINT_S_D_DESC : MSA_2RF_DESC_BASE<"ftint_s.d", int_mips_ftint_s_d,
                                         MSA128DOpnd>;

class FTINT_U_W_DESC : MSA_2RF_DESC_BASE<"ftint_u.w", int_mips_ftint_u_w,
                                         MSA128WOpnd>;
class FTINT_U_D_DESC : MSA_2RF_DESC_BASE<"ftint_u.d", int_mips_ftint_u_d,
                                         MSA128DOpnd>;

class FTQ_H_DESC : MSA_3RF_DESC_BASE<"ftq.h", int_mips_ftq_h,
                                     MSA128HOpnd, MSA128WOpnd, MSA128WOpnd>;
class FTQ_W_DESC : MSA_3RF_DESC_BASE<"ftq.w", int_mips_ftq_w,
                                     MSA128WOpnd, MSA128DOpnd, MSA128DOpnd>;

class FTRUNC_S_W_DESC : MSA_2RF_DESC_BASE<"ftrunc_s.w", fp_to_sint,
                                          MSA128WOpnd>;
class FTRUNC_S_D_DESC : MSA_2RF_DESC_BASE<"ftrunc_s.d", fp_to_sint,
                                          MSA128DOpnd>;

class FTRUNC_U_W_DESC : MSA_2RF_DESC_BASE<"ftrunc_u.w", fp_to_uint,
                                          MSA128WOpnd>;
class FTRUNC_U_D_DESC : MSA_2RF_DESC_BASE<"ftrunc_u.d", fp_to_uint,
                                          MSA128DOpnd>;

class HADD_S_H_DESC : MSA_3R_DESC_BASE<"hadd_s.h", int_mips_hadd_s_h,
                                       MSA128HOpnd, MSA128BOpnd, MSA128BOpnd>;
class HADD_S_W_DESC : MSA_3R_DESC_BASE<"hadd_s.w", int_mips_hadd_s_w,
                                       MSA128WOpnd, MSA128HOpnd, MSA128HOpnd>;
class HADD_S_D_DESC : MSA_3R_DESC_BASE<"hadd_s.d", int_mips_hadd_s_d,
                                       MSA128DOpnd, MSA128WOpnd, MSA128WOpnd>;

class HADD_U_H_DESC : MSA_3R_DESC_BASE<"hadd_u.h", int_mips_hadd_u_h,
                                       MSA128HOpnd, MSA128BOpnd, MSA128BOpnd>;
class HADD_U_W_DESC : MSA_3R_DESC_BASE<"hadd_u.w", int_mips_hadd_u_w,
                                       MSA128WOpnd, MSA128HOpnd, MSA128HOpnd>;
class HADD_U_D_DESC : MSA_3R_DESC_BASE<"hadd_u.d", int_mips_hadd_u_d,
                                       MSA128DOpnd, MSA128WOpnd, MSA128WOpnd>;

class HSUB_S_H_DESC : MSA_3R_DESC_BASE<"hsub_s.h", int_mips_hsub_s_h,
                                       MSA128HOpnd, MSA128BOpnd, MSA128BOpnd>;
class HSUB_S_W_DESC : MSA_3R_DESC_BASE<"hsub_s.w", int_mips_hsub_s_w,
                                       MSA128WOpnd, MSA128HOpnd, MSA128HOpnd>;
class HSUB_S_D_DESC : MSA_3R_DESC_BASE<"hsub_s.d", int_mips_hsub_s_d,
                                       MSA128DOpnd, MSA128WOpnd, MSA128WOpnd>;

class HSUB_U_H_DESC : MSA_3R_DESC_BASE<"hsub_u.h", int_mips_hsub_u_h,
                                       MSA128HOpnd, MSA128BOpnd, MSA128BOpnd>;
class HSUB_U_W_DESC : MSA_3R_DESC_BASE<"hsub_u.w", int_mips_hsub_u_w,
                                       MSA128WOpnd, MSA128HOpnd, MSA128HOpnd>;
class HSUB_U_D_DESC : MSA_3R_DESC_BASE<"hsub_u.d", int_mips_hsub_u_d,
                                       MSA128DOpnd, MSA128WOpnd, MSA128WOpnd>;

class ILVEV_B_DESC : MSA_3R_DESC_BASE<"ilvev.b", MipsILVEV, MSA128BOpnd>;
class ILVEV_H_DESC : MSA_3R_DESC_BASE<"ilvev.h", MipsILVEV, MSA128HOpnd>;
class ILVEV_W_DESC : MSA_3R_DESC_BASE<"ilvev.w", MipsILVEV, MSA128WOpnd>;
class ILVEV_D_DESC : MSA_3R_DESC_BASE<"ilvev.d", MipsILVEV, MSA128DOpnd>;

class ILVL_B_DESC : MSA_3R_DESC_BASE<"ilvl.b", MipsILVL, MSA128BOpnd>;
class ILVL_H_DESC : MSA_3R_DESC_BASE<"ilvl.h", MipsILVL, MSA128HOpnd>;
class ILVL_W_DESC : MSA_3R_DESC_BASE<"ilvl.w", MipsILVL, MSA128WOpnd>;
class ILVL_D_DESC : MSA_3R_DESC_BASE<"ilvl.d", MipsILVL, MSA128DOpnd>;

class ILVOD_B_DESC : MSA_3R_DESC_BASE<"ilvod.b", MipsILVOD, MSA128BOpnd>;
class ILVOD_H_DESC : MSA_3R_DESC_BASE<"ilvod.h", MipsILVOD, MSA128HOpnd>;
class ILVOD_W_DESC : MSA_3R_DESC_BASE<"ilvod.w", MipsILVOD, MSA128WOpnd>;
class ILVOD_D_DESC : MSA_3R_DESC_BASE<"ilvod.d", MipsILVOD, MSA128DOpnd>;

class ILVR_B_DESC : MSA_3R_DESC_BASE<"ilvr.b", MipsILVR, MSA128BOpnd>;
class ILVR_H_DESC : MSA_3R_DESC_BASE<"ilvr.h", MipsILVR, MSA128HOpnd>;
class ILVR_W_DESC : MSA_3R_DESC_BASE<"ilvr.w", MipsILVR, MSA128WOpnd>;
class ILVR_D_DESC : MSA_3R_DESC_BASE<"ilvr.d", MipsILVR, MSA128DOpnd>;

class INSERT_B_DESC : MSA_INSERT_DESC_BASE<"insert.b", vinsert_v16i8, uimm4,
                                           immZExt4Ptr, MSA128BOpnd, GPR32Opnd>;
class INSERT_H_DESC : MSA_INSERT_DESC_BASE<"insert.h", vinsert_v8i16, uimm3,
                                           immZExt3Ptr, MSA128HOpnd, GPR32Opnd>;
class INSERT_W_DESC : MSA_INSERT_DESC_BASE<"insert.w", vinsert_v4i32, uimm2,
                                           immZExt2Ptr, MSA128WOpnd, GPR32Opnd>;
class INSERT_D_DESC : MSA_INSERT_DESC_BASE<"insert.d", vinsert_v2i64, uimm1,
                                           immZExt1Ptr, MSA128DOpnd, GPR64Opnd>;

class INSERT_B_VIDX_PSEUDO_DESC :
    MSA_INSERT_VIDX_PSEUDO_BASE<vector_insert, v16i8, MSA128BOpnd, GPR32Opnd, GPR32Opnd>;
class INSERT_H_VIDX_PSEUDO_DESC :
    MSA_INSERT_VIDX_PSEUDO_BASE<vector_insert, v8i16, MSA128HOpnd, GPR32Opnd, GPR32Opnd>;
class INSERT_W_VIDX_PSEUDO_DESC :
    MSA_INSERT_VIDX_PSEUDO_BASE<vector_insert, v4i32, MSA128WOpnd, GPR32Opnd, GPR32Opnd>;
class INSERT_D_VIDX_PSEUDO_DESC :
    MSA_INSERT_VIDX_PSEUDO_BASE<vector_insert, v2i64, MSA128DOpnd, GPR64Opnd, GPR32Opnd>;

class INSERT_FW_PSEUDO_DESC : MSA_INSERT_PSEUDO_BASE<vector_insert, v4f32,
                                                     uimm2, immZExt2Ptr,
                                                     MSA128WOpnd, FGR32Opnd>;
class INSERT_FD_PSEUDO_DESC : MSA_INSERT_PSEUDO_BASE<vector_insert, v2f64,
                                                     uimm1, immZExt1Ptr,
                                                     MSA128DOpnd, FGR64Opnd>;

class INSERT_FW_VIDX_PSEUDO_DESC :
    MSA_INSERT_VIDX_PSEUDO_BASE<vector_insert, v4f32, MSA128WOpnd, FGR32Opnd, GPR32Opnd>;
class INSERT_FD_VIDX_PSEUDO_DESC :
    MSA_INSERT_VIDX_PSEUDO_BASE<vector_insert, v2f64, MSA128DOpnd, FGR64Opnd, GPR32Opnd>;

class INSERT_B_VIDX64_PSEUDO_DESC :
    MSA_INSERT_VIDX_PSEUDO_BASE<vector_insert, v16i8, MSA128BOpnd, GPR32Opnd, GPR64Opnd>;
class INSERT_H_VIDX64_PSEUDO_DESC :
    MSA_INSERT_VIDX_PSEUDO_BASE<vector_insert, v8i16, MSA128HOpnd, GPR32Opnd, GPR64Opnd>;
class INSERT_W_VIDX64_PSEUDO_DESC :
    MSA_INSERT_VIDX_PSEUDO_BASE<vector_insert, v4i32, MSA128WOpnd, GPR32Opnd, GPR64Opnd>;
class INSERT_D_VIDX64_PSEUDO_DESC :
    MSA_INSERT_VIDX_PSEUDO_BASE<vector_insert, v2i64, MSA128DOpnd, GPR64Opnd, GPR64Opnd>;

class INSERT_FW_VIDX64_PSEUDO_DESC :
    MSA_INSERT_VIDX_PSEUDO_BASE<vector_insert, v4f32, MSA128WOpnd, FGR32Opnd, GPR64Opnd>;
class INSERT_FD_VIDX64_PSEUDO_DESC :
    MSA_INSERT_VIDX_PSEUDO_BASE<vector_insert, v2f64, MSA128DOpnd, FGR64Opnd, GPR64Opnd>;

class INSVE_B_DESC : MSA_INSVE_DESC_BASE<"insve.b", insve_v16i8, uimm4, timmZExt4,
                                         MSA128BOpnd>;
class INSVE_H_DESC : MSA_INSVE_DESC_BASE<"insve.h", insve_v8i16, uimm3, timmZExt3,
                                         MSA128HOpnd>;
class INSVE_W_DESC : MSA_INSVE_DESC_BASE<"insve.w", insve_v4i32, uimm2, timmZExt2,
                                         MSA128WOpnd>;
class INSVE_D_DESC : MSA_INSVE_DESC_BASE<"insve.d", insve_v2i64, uimm1, timmZExt1,
                                         MSA128DOpnd>;

class LD_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                   ValueType TyNode, RegisterOperand ROWD,
                   Operand MemOpnd, ComplexPattern Addr = addrimm10,
                   InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs ROWD:$wd);
  dag InOperandList = (ins MemOpnd:$addr);
  string AsmString = !strconcat(instr_asm, "\t$wd, $addr");
  list<dag> Pattern = [(set ROWD:$wd, (TyNode (OpNode Addr:$addr)))];
  InstrItinClass Itinerary = itin;
  string DecoderMethod = "DecodeMSA128Mem";
}

class LD_B_DESC : LD_DESC_BASE<"ld.b", load, v16i8, MSA128BOpnd, mem_simm10>;
class LD_H_DESC : LD_DESC_BASE<"ld.h", load, v8i16, MSA128HOpnd,
                               mem_simm10_lsl1, addrimm10lsl1>;
class LD_W_DESC : LD_DESC_BASE<"ld.w", load, v4i32, MSA128WOpnd,
                               mem_simm10_lsl2, addrimm10lsl2>;
class LD_D_DESC : LD_DESC_BASE<"ld.d", load, v2i64, MSA128DOpnd,
                               mem_simm10_lsl3, addrimm10lsl3>;

class LDI_B_DESC : MSA_I10_LDI_DESC_BASE<"ldi.b", MSA128BOpnd>;
class LDI_H_DESC : MSA_I10_LDI_DESC_BASE<"ldi.h", MSA128HOpnd>;
class LDI_W_DESC : MSA_I10_LDI_DESC_BASE<"ldi.w", MSA128WOpnd>;
class LDI_D_DESC : MSA_I10_LDI_DESC_BASE<"ldi.d", MSA128DOpnd>;

class MSA_LOAD_PSEUDO_BASE<SDPatternOperator intrinsic, RegisterOperand RO> :
  PseudoSE<(outs RO:$dst), (ins PtrRC:$ptr, GPR32:$imm),
           [(set RO:$dst, (intrinsic iPTR:$ptr, GPR32:$imm))]> {
  let hasNoSchedulingInfo = 1;
  let usesCustomInserter = 1;
}

def LDR_D : MSA_LOAD_PSEUDO_BASE<int_mips_ldr_d, MSA128DOpnd>;
def LDR_W : MSA_LOAD_PSEUDO_BASE<int_mips_ldr_w, MSA128WOpnd>;

class LSA_DESC_BASE<string instr_asm, RegisterOperand RORD,
                    InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs RORD:$rd);
  dag InOperandList = (ins RORD:$rs, RORD:$rt, uimm2_plus1:$sa);
  string AsmString = !strconcat(instr_asm, "\t$rd, $rs, $rt, $sa");
  list<dag> Pattern = [(set RORD:$rd, (add RORD:$rt,
                                                (shl RORD:$rs,
                                                     immZExt2Lsa:$sa)))];
  InstrItinClass Itinerary = itin;
}

class LSA_DESC : LSA_DESC_BASE<"lsa", GPR32Opnd, II_LSA>;
class DLSA_DESC : LSA_DESC_BASE<"dlsa", GPR64Opnd, II_DLSA>;

class MADD_Q_H_DESC : MSA_3RF_4RF_DESC_BASE<"madd_q.h", int_mips_madd_q_h,
                                            MSA128HOpnd>;
class MADD_Q_W_DESC : MSA_3RF_4RF_DESC_BASE<"madd_q.w", int_mips_madd_q_w,
                                            MSA128WOpnd>;

class MADDR_Q_H_DESC : MSA_3RF_4RF_DESC_BASE<"maddr_q.h", int_mips_maddr_q_h,
                                             MSA128HOpnd>;
class MADDR_Q_W_DESC : MSA_3RF_4RF_DESC_BASE<"maddr_q.w", int_mips_maddr_q_w,
                                             MSA128WOpnd>;

class MADDV_B_DESC : MSA_3R_4R_DESC_BASE<"maddv.b", muladd, MSA128BOpnd>;
class MADDV_H_DESC : MSA_3R_4R_DESC_BASE<"maddv.h", muladd, MSA128HOpnd>;
class MADDV_W_DESC : MSA_3R_4R_DESC_BASE<"maddv.w", muladd, MSA128WOpnd>;
class MADDV_D_DESC : MSA_3R_4R_DESC_BASE<"maddv.d", muladd, MSA128DOpnd>;

class MAX_A_B_DESC : MSA_3R_DESC_BASE<"max_a.b", int_mips_max_a_b, MSA128BOpnd>;
class MAX_A_H_DESC : MSA_3R_DESC_BASE<"max_a.h", int_mips_max_a_h, MSA128HOpnd>;
class MAX_A_W_DESC : MSA_3R_DESC_BASE<"max_a.w", int_mips_max_a_w, MSA128WOpnd>;
class MAX_A_D_DESC : MSA_3R_DESC_BASE<"max_a.d", int_mips_max_a_d, MSA128DOpnd>;

class MAX_S_B_DESC : MSA_3R_DESC_BASE<"max_s.b", smax, MSA128BOpnd>;
class MAX_S_H_DESC : MSA_3R_DESC_BASE<"max_s.h", smax, MSA128HOpnd>;
class MAX_S_W_DESC : MSA_3R_DESC_BASE<"max_s.w", smax, MSA128WOpnd>;
class MAX_S_D_DESC : MSA_3R_DESC_BASE<"max_s.d", smax, MSA128DOpnd>;

class MAX_U_B_DESC : MSA_3R_DESC_BASE<"max_u.b", umax, MSA128BOpnd>;
class MAX_U_H_DESC : MSA_3R_DESC_BASE<"max_u.h", umax, MSA128HOpnd>;
class MAX_U_W_DESC : MSA_3R_DESC_BASE<"max_u.w", umax, MSA128WOpnd>;
class MAX_U_D_DESC : MSA_3R_DESC_BASE<"max_u.d", umax, MSA128DOpnd>;

class MAXI_S_B_DESC : MSA_I5_DESC_BASE<"maxi_s.b", smax, vsplati8_simm5,
                                       MSA128BOpnd>;
class MAXI_S_H_DESC : MSA_I5_DESC_BASE<"maxi_s.h", smax, vsplati16_simm5,
                                       MSA128HOpnd>;
class MAXI_S_W_DESC : MSA_I5_DESC_BASE<"maxi_s.w", smax, vsplati32_simm5,
                                       MSA128WOpnd>;
class MAXI_S_D_DESC : MSA_I5_DESC_BASE<"maxi_s.d", smax, vsplati64_simm5,
                                       MSA128DOpnd>;

class MAXI_U_B_DESC : MSA_I5_DESC_BASE<"maxi_u.b", umax, vsplati8_uimm5,
                                       MSA128BOpnd>;
class MAXI_U_H_DESC : MSA_I5_DESC_BASE<"maxi_u.h", umax, vsplati16_uimm5,
                                       MSA128HOpnd>;
class MAXI_U_W_DESC : MSA_I5_DESC_BASE<"maxi_u.w", umax, vsplati32_uimm5,
                                       MSA128WOpnd>;
class MAXI_U_D_DESC : MSA_I5_DESC_BASE<"maxi_u.d", umax, vsplati64_uimm5,
                                       MSA128DOpnd>;

class MIN_A_B_DESC : MSA_3R_DESC_BASE<"min_a.b", int_mips_min_a_b, MSA128BOpnd>;
class MIN_A_H_DESC : MSA_3R_DESC_BASE<"min_a.h", int_mips_min_a_h, MSA128HOpnd>;
class MIN_A_W_DESC : MSA_3R_DESC_BASE<"min_a.w", int_mips_min_a_w, MSA128WOpnd>;
class MIN_A_D_DESC : MSA_3R_DESC_BASE<"min_a.d", int_mips_min_a_d, MSA128DOpnd>;

class MIN_S_B_DESC : MSA_3R_DESC_BASE<"min_s.b", smin, MSA128BOpnd>;
class MIN_S_H_DESC : MSA_3R_DESC_BASE<"min_s.h", smin, MSA128HOpnd>;
class MIN_S_W_DESC : MSA_3R_DESC_BASE<"min_s.w", smin, MSA128WOpnd>;
class MIN_S_D_DESC : MSA_3R_DESC_BASE<"min_s.d", smin, MSA128DOpnd>;

class MIN_U_B_DESC : MSA_3R_DESC_BASE<"min_u.b", umin, MSA128BOpnd>;
class MIN_U_H_DESC : MSA_3R_DESC_BASE<"min_u.h", umin, MSA128HOpnd>;
class MIN_U_W_DESC : MSA_3R_DESC_BASE<"min_u.w", umin, MSA128WOpnd>;
class MIN_U_D_DESC : MSA_3R_DESC_BASE<"min_u.d", umin, MSA128DOpnd>;

class MINI_S_B_DESC : MSA_I5_DESC_BASE<"mini_s.b", smin, vsplati8_simm5,
                                       MSA128BOpnd>;
class MINI_S_H_DESC : MSA_I5_DESC_BASE<"mini_s.h", smin, vsplati16_simm5,
                                       MSA128HOpnd>;
class MINI_S_W_DESC : MSA_I5_DESC_BASE<"mini_s.w", smin, vsplati32_simm5,
                                       MSA128WOpnd>;
class MINI_S_D_DESC : MSA_I5_DESC_BASE<"mini_s.d", smin, vsplati64_simm5,
                                       MSA128DOpnd>;

class MINI_U_B_DESC : MSA_I5_DESC_BASE<"mini_u.b", umin, vsplati8_uimm5,
                                       MSA128BOpnd>;
class MINI_U_H_DESC : MSA_I5_DESC_BASE<"mini_u.h", umin, vsplati16_uimm5,
                                       MSA128HOpnd>;
class MINI_U_W_DESC : MSA_I5_DESC_BASE<"mini_u.w", umin, vsplati32_uimm5,
                                       MSA128WOpnd>;
class MINI_U_D_DESC : MSA_I5_DESC_BASE<"mini_u.d", umin, vsplati64_uimm5,
                                       MSA128DOpnd>;

class MOD_S_B_DESC : MSA_3R_DESC_BASE<"mod_s.b", srem, MSA128BOpnd>;
class MOD_S_H_DESC : MSA_3R_DESC_BASE<"mod_s.h", srem, MSA128HOpnd>;
class MOD_S_W_DESC : MSA_3R_DESC_BASE<"mod_s.w", srem, MSA128WOpnd>;
class MOD_S_D_DESC : MSA_3R_DESC_BASE<"mod_s.d", srem, MSA128DOpnd>;

class MOD_U_B_DESC : MSA_3R_DESC_BASE<"mod_u.b", urem, MSA128BOpnd>;
class MOD_U_H_DESC : MSA_3R_DESC_BASE<"mod_u.h", urem, MSA128HOpnd>;
class MOD_U_W_DESC : MSA_3R_DESC_BASE<"mod_u.w", urem, MSA128WOpnd>;
class MOD_U_D_DESC : MSA_3R_DESC_BASE<"mod_u.d", urem, MSA128DOpnd>;

class MOVE_V_DESC {
  dag OutOperandList = (outs MSA128BOpnd:$wd);
  dag InOperandList = (ins MSA128BOpnd:$ws);
  string AsmString = "move.v\t$wd, $ws";
  list<dag> Pattern = [];
  InstrItinClass Itinerary = NoItinerary;
  bit isMoveReg = 1;
}

class MSUB_Q_H_DESC : MSA_3RF_4RF_DESC_BASE<"msub_q.h", int_mips_msub_q_h,
                                            MSA128HOpnd>;
class MSUB_Q_W_DESC : MSA_3RF_4RF_DESC_BASE<"msub_q.w", int_mips_msub_q_w,
                                            MSA128WOpnd>;

class MSUBR_Q_H_DESC : MSA_3RF_4RF_DESC_BASE<"msubr_q.h", int_mips_msubr_q_h,
                                             MSA128HOpnd>;
class MSUBR_Q_W_DESC : MSA_3RF_4RF_DESC_BASE<"msubr_q.w", int_mips_msubr_q_w,
                                             MSA128WOpnd>;

class MSUBV_B_DESC : MSA_3R_4R_DESC_BASE<"msubv.b", mulsub, MSA128BOpnd>;
class MSUBV_H_DESC : MSA_3R_4R_DESC_BASE<"msubv.h", mulsub, MSA128HOpnd>;
class MSUBV_W_DESC : MSA_3R_4R_DESC_BASE<"msubv.w", mulsub, MSA128WOpnd>;
class MSUBV_D_DESC : MSA_3R_4R_DESC_BASE<"msubv.d", mulsub, MSA128DOpnd>;

class MUL_Q_H_DESC : MSA_3RF_DESC_BASE<"mul_q.h", int_mips_mul_q_h,
                                       MSA128HOpnd>;
class MUL_Q_W_DESC : MSA_3RF_DESC_BASE<"mul_q.w", int_mips_mul_q_w,
                                       MSA128WOpnd>;

class MULR_Q_H_DESC : MSA_3RF_DESC_BASE<"mulr_q.h", int_mips_mulr_q_h,
                                        MSA128HOpnd>;
class MULR_Q_W_DESC : MSA_3RF_DESC_BASE<"mulr_q.w", int_mips_mulr_q_w,
                                        MSA128WOpnd>;

class MULV_B_DESC : MSA_3R_DESC_BASE<"mulv.b", mul, MSA128BOpnd>;
class MULV_H_DESC : MSA_3R_DESC_BASE<"mulv.h", mul, MSA128HOpnd>;
class MULV_W_DESC : MSA_3R_DESC_BASE<"mulv.w", mul, MSA128WOpnd>;
class MULV_D_DESC : MSA_3R_DESC_BASE<"mulv.d", mul, MSA128DOpnd>;

class NLOC_B_DESC : MSA_2R_DESC_BASE<"nloc.b", int_mips_nloc_b, MSA128BOpnd>;
class NLOC_H_DESC : MSA_2R_DESC_BASE<"nloc.h", int_mips_nloc_h, MSA128HOpnd>;
class NLOC_W_DESC : MSA_2R_DESC_BASE<"nloc.w", int_mips_nloc_w, MSA128WOpnd>;
class NLOC_D_DESC : MSA_2R_DESC_BASE<"nloc.d", int_mips_nloc_d, MSA128DOpnd>;

class NLZC_B_DESC : MSA_2R_DESC_BASE<"nlzc.b", ctlz, MSA128BOpnd>;
class NLZC_H_DESC : MSA_2R_DESC_BASE<"nlzc.h", ctlz, MSA128HOpnd>;
class NLZC_W_DESC : MSA_2R_DESC_BASE<"nlzc.w", ctlz, MSA128WOpnd>;
class NLZC_D_DESC : MSA_2R_DESC_BASE<"nlzc.d", ctlz, MSA128DOpnd>;

class NOR_V_DESC : MSA_VEC_DESC_BASE<"nor.v", MipsVNOR, MSA128BOpnd>;
class NOR_V_H_PSEUDO_DESC : MSA_VEC_PSEUDO_BASE<MipsVNOR, MSA128HOpnd>;
class NOR_V_W_PSEUDO_DESC : MSA_VEC_PSEUDO_BASE<MipsVNOR, MSA128WOpnd>;
class NOR_V_D_PSEUDO_DESC : MSA_VEC_PSEUDO_BASE<MipsVNOR, MSA128DOpnd>;

class NORI_B_DESC : MSA_I8_DESC_BASE<"nori.b", MipsVNOR, vsplati8_uimm8,
                                     MSA128BOpnd>;

class OR_V_DESC : MSA_VEC_DESC_BASE<"or.v", or, MSA128BOpnd>;
class OR_V_H_PSEUDO_DESC : MSA_VEC_PSEUDO_BASE<or, MSA128HOpnd>;
class OR_V_W_PSEUDO_DESC : MSA_VEC_PSEUDO_BASE<or, MSA128WOpnd>;
class OR_V_D_PSEUDO_DESC : MSA_VEC_PSEUDO_BASE<or, MSA128DOpnd>;

class ORI_B_DESC : MSA_I8_DESC_BASE<"ori.b", or, vsplati8_uimm8, MSA128BOpnd>;

class PCKEV_B_DESC : MSA_3R_DESC_BASE<"pckev.b", MipsPCKEV, MSA128BOpnd>;
class PCKEV_H_DESC : MSA_3R_DESC_BASE<"pckev.h", MipsPCKEV, MSA128HOpnd>;
class PCKEV_W_DESC : MSA_3R_DESC_BASE<"pckev.w", MipsPCKEV, MSA128WOpnd>;
class PCKEV_D_DESC : MSA_3R_DESC_BASE<"pckev.d", MipsPCKEV, MSA128DOpnd>;

class PCKOD_B_DESC : MSA_3R_DESC_BASE<"pckod.b", MipsPCKOD, MSA128BOpnd>;
class PCKOD_H_DESC : MSA_3R_DESC_BASE<"pckod.h", MipsPCKOD, MSA128HOpnd>;
class PCKOD_W_DESC : MSA_3R_DESC_BASE<"pckod.w", MipsPCKOD, MSA128WOpnd>;
class PCKOD_D_DESC : MSA_3R_DESC_BASE<"pckod.d", MipsPCKOD, MSA128DOpnd>;

class PCNT_B_DESC : MSA_2R_DESC_BASE<"pcnt.b", ctpop, MSA128BOpnd>;
class PCNT_H_DESC : MSA_2R_DESC_BASE<"pcnt.h", ctpop, MSA128HOpnd>;
class PCNT_W_DESC : MSA_2R_DESC_BASE<"pcnt.w", ctpop, MSA128WOpnd>;
class PCNT_D_DESC : MSA_2R_DESC_BASE<"pcnt.d", ctpop, MSA128DOpnd>;

class SAT_S_B_DESC : MSA_BIT_X_DESC_BASE<"sat_s.b", int_mips_sat_s_b, uimm3,
                                         timmZExt3, MSA128BOpnd>;
class SAT_S_H_DESC : MSA_BIT_X_DESC_BASE<"sat_s.h", int_mips_sat_s_h, uimm4,
                                         timmZExt4, MSA128HOpnd>;
class SAT_S_W_DESC : MSA_BIT_X_DESC_BASE<"sat_s.w", int_mips_sat_s_w, uimm5,
                                         timmZExt5, MSA128WOpnd>;
class SAT_S_D_DESC : MSA_BIT_X_DESC_BASE<"sat_s.d", int_mips_sat_s_d, uimm6,
                                         timmZExt6, MSA128DOpnd>;

class SAT_U_B_DESC : MSA_BIT_X_DESC_BASE<"sat_u.b", int_mips_sat_u_b, uimm3,
                                         timmZExt3, MSA128BOpnd>;
class SAT_U_H_DESC : MSA_BIT_X_DESC_BASE<"sat_u.h", int_mips_sat_u_h, uimm4,
                                         timmZExt4, MSA128HOpnd>;
class SAT_U_W_DESC : MSA_BIT_X_DESC_BASE<"sat_u.w", int_mips_sat_u_w, uimm5,
                                         timmZExt5, MSA128WOpnd>;
class SAT_U_D_DESC : MSA_BIT_X_DESC_BASE<"sat_u.d", int_mips_sat_u_d, uimm6,
                                         timmZExt6, MSA128DOpnd>;

class SHF_B_DESC : MSA_I8_SHF_DESC_BASE<"shf.b", MSA128BOpnd>;
class SHF_H_DESC : MSA_I8_SHF_DESC_BASE<"shf.h", MSA128HOpnd>;
class SHF_W_DESC : MSA_I8_SHF_DESC_BASE<"shf.w", MSA128WOpnd>;

class SLD_B_DESC : MSA_3R_SLD_DESC_BASE<"sld.b", int_mips_sld_b, MSA128BOpnd>;
class SLD_H_DESC : MSA_3R_SLD_DESC_BASE<"sld.h", int_mips_sld_h, MSA128HOpnd>;
class SLD_W_DESC : MSA_3R_SLD_DESC_BASE<"sld.w", int_mips_sld_w, MSA128WOpnd>;
class SLD_D_DESC : MSA_3R_SLD_DESC_BASE<"sld.d", int_mips_sld_d, MSA128DOpnd>;

class SLDI_B_DESC : MSA_ELM_SLD_DESC_BASE<"sldi.b", int_mips_sldi_b,
                                          MSA128BOpnd, MSA128BOpnd, uimm4,
                                          timmZExt4>;
class SLDI_H_DESC : MSA_ELM_SLD_DESC_BASE<"sldi.h", int_mips_sldi_h,
                                          MSA128HOpnd, MSA128HOpnd, uimm3,
                                          timmZExt3>;
class SLDI_W_DESC : MSA_ELM_SLD_DESC_BASE<"sldi.w", int_mips_sldi_w,
                                          MSA128WOpnd, MSA128WOpnd, uimm2,
                                          timmZExt2>;
class SLDI_D_DESC : MSA_ELM_SLD_DESC_BASE<"sldi.d", int_mips_sldi_d,
                                          MSA128DOpnd, MSA128DOpnd, uimm1,
                                          timmZExt1>;

class SLL_B_DESC : MSA_3R_DESC_BASE<"sll.b", shl, MSA128BOpnd>;
class SLL_H_DESC : MSA_3R_DESC_BASE<"sll.h", shl, MSA128HOpnd>;
class SLL_W_DESC : MSA_3R_DESC_BASE<"sll.w", shl, MSA128WOpnd>;
class SLL_D_DESC : MSA_3R_DESC_BASE<"sll.d", shl, MSA128DOpnd>;

class SLLI_B_DESC : MSA_BIT_SPLAT_DESC_BASE<"slli.b", shl, vsplati8_uimm3,
                                            MSA128BOpnd>;
class SLLI_H_DESC : MSA_BIT_SPLAT_DESC_BASE<"slli.h", shl, vsplati16_uimm4,
                                            MSA128HOpnd>;
class SLLI_W_DESC : MSA_BIT_SPLAT_DESC_BASE<"slli.w", shl, vsplati32_uimm5,
                                            MSA128WOpnd>;
class SLLI_D_DESC : MSA_BIT_SPLAT_DESC_BASE<"slli.d", shl, vsplati64_uimm6,
                                            MSA128DOpnd>;

class SPLAT_B_DESC : MSA_3R_SPLAT_DESC_BASE<"splat.b", vsplati8_elt,
                                            MSA128BOpnd>;
class SPLAT_H_DESC : MSA_3R_SPLAT_DESC_BASE<"splat.h", vsplati16_elt,
                                            MSA128HOpnd>;
class SPLAT_W_DESC : MSA_3R_SPLAT_DESC_BASE<"splat.w", vsplati32_elt,
                                            MSA128WOpnd>;
class SPLAT_D_DESC : MSA_3R_SPLAT_DESC_BASE<"splat.d", vsplati64_elt,
                                            MSA128DOpnd>;

class SPLATI_B_DESC : MSA_ELM_SPLAT_DESC_BASE<"splati.b", vsplati8_uimm4,
                                              MSA128BOpnd>;
class SPLATI_H_DESC : MSA_ELM_SPLAT_DESC_BASE<"splati.h", vsplati16_uimm3,
                                              MSA128HOpnd>;
class SPLATI_W_DESC : MSA_ELM_SPLAT_DESC_BASE<"splati.w", vsplati32_uimm2,
                                              MSA128WOpnd>;
class SPLATI_D_DESC : MSA_ELM_SPLAT_DESC_BASE<"splati.d", vsplati64_uimm1,
                                              MSA128DOpnd>;

class SRA_B_DESC : MSA_3R_DESC_BASE<"sra.b", sra, MSA128BOpnd>;
class SRA_H_DESC : MSA_3R_DESC_BASE<"sra.h", sra, MSA128HOpnd>;
class SRA_W_DESC : MSA_3R_DESC_BASE<"sra.w", sra, MSA128WOpnd>;
class SRA_D_DESC : MSA_3R_DESC_BASE<"sra.d", sra, MSA128DOpnd>;

class SRAI_B_DESC : MSA_BIT_SPLAT_DESC_BASE<"srai.b", sra, vsplati8_uimm3,
                                            MSA128BOpnd>;
class SRAI_H_DESC : MSA_BIT_SPLAT_DESC_BASE<"srai.h", sra, vsplati16_uimm4,
                                            MSA128HOpnd>;
class SRAI_W_DESC : MSA_BIT_SPLAT_DESC_BASE<"srai.w", sra, vsplati32_uimm5,
                                            MSA128WOpnd>;
class SRAI_D_DESC : MSA_BIT_SPLAT_DESC_BASE<"srai.d", sra, vsplati64_uimm6,
                                            MSA128DOpnd>;

class SRAR_B_DESC : MSA_3R_DESC_BASE<"srar.b", int_mips_srar_b, MSA128BOpnd>;
class SRAR_H_DESC : MSA_3R_DESC_BASE<"srar.h", int_mips_srar_h, MSA128HOpnd>;
class SRAR_W_DESC : MSA_3R_DESC_BASE<"srar.w", int_mips_srar_w, MSA128WOpnd>;
class SRAR_D_DESC : MSA_3R_DESC_BASE<"srar.d", int_mips_srar_d, MSA128DOpnd>;

class SRARI_B_DESC : MSA_BIT_X_DESC_BASE<"srari.b", int_mips_srari_b, uimm3,
                                         timmZExt3, MSA128BOpnd>;
class SRARI_H_DESC : MSA_BIT_X_DESC_BASE<"srari.h", int_mips_srari_h, uimm4,
                                         timmZExt4, MSA128HOpnd>;
class SRARI_W_DESC : MSA_BIT_X_DESC_BASE<"srari.w", int_mips_srari_w, uimm5,
                                         timmZExt5, MSA128WOpnd>;
class SRARI_D_DESC : MSA_BIT_X_DESC_BASE<"srari.d", int_mips_srari_d, uimm6,
                                         timmZExt6, MSA128DOpnd>;

class SRL_B_DESC : MSA_3R_DESC_BASE<"srl.b", srl, MSA128BOpnd>;
class SRL_H_DESC : MSA_3R_DESC_BASE<"srl.h", srl, MSA128HOpnd>;
class SRL_W_DESC : MSA_3R_DESC_BASE<"srl.w", srl, MSA128WOpnd>;
class SRL_D_DESC : MSA_3R_DESC_BASE<"srl.d", srl, MSA128DOpnd>;

class SRLI_B_DESC : MSA_BIT_SPLAT_DESC_BASE<"srli.b", srl, vsplati8_uimm3,
                                            MSA128BOpnd>;
class SRLI_H_DESC : MSA_BIT_SPLAT_DESC_BASE<"srli.h", srl, vsplati16_uimm4,
                                            MSA128HOpnd>;
class SRLI_W_DESC : MSA_BIT_SPLAT_DESC_BASE<"srli.w", srl, vsplati32_uimm5,
                                            MSA128WOpnd>;
class SRLI_D_DESC : MSA_BIT_SPLAT_DESC_BASE<"srli.d", srl, vsplati64_uimm6,
                                            MSA128DOpnd>;

class SRLR_B_DESC : MSA_3R_DESC_BASE<"srlr.b", int_mips_srlr_b, MSA128BOpnd>;
class SRLR_H_DESC : MSA_3R_DESC_BASE<"srlr.h", int_mips_srlr_h, MSA128HOpnd>;
class SRLR_W_DESC : MSA_3R_DESC_BASE<"srlr.w", int_mips_srlr_w, MSA128WOpnd>;
class SRLR_D_DESC : MSA_3R_DESC_BASE<"srlr.d", int_mips_srlr_d, MSA128DOpnd>;

class SRLRI_B_DESC : MSA_BIT_X_DESC_BASE<"srlri.b", int_mips_srlri_b, uimm3,
                                         timmZExt3, MSA128BOpnd>;
class SRLRI_H_DESC : MSA_BIT_X_DESC_BASE<"srlri.h", int_mips_srlri_h, uimm4,
                                         timmZExt4, MSA128HOpnd>;
class SRLRI_W_DESC : MSA_BIT_X_DESC_BASE<"srlri.w", int_mips_srlri_w, uimm5,
                                         timmZExt5, MSA128WOpnd>;
class SRLRI_D_DESC : MSA_BIT_X_DESC_BASE<"srlri.d", int_mips_srlri_d, uimm6,
                                         timmZExt6, MSA128DOpnd>;

class ST_DESC_BASE<string instr_asm, SDPatternOperator OpNode,
                   ValueType TyNode, RegisterOperand ROWD,
                   Operand MemOpnd, ComplexPattern Addr = addrimm10,
                   InstrItinClass itin = NoItinerary> {
  dag OutOperandList = (outs);
  dag InOperandList = (ins ROWD:$wd, MemOpnd:$addr);
  string AsmString = !strconcat(instr_asm, "\t$wd, $addr");
  list<dag> Pattern = [(OpNode (TyNode ROWD:$wd), Addr:$addr)];
  InstrItinClass Itinerary = itin;
  string DecoderMethod = "DecodeMSA128Mem";
}

class ST_B_DESC : ST_DESC_BASE<"st.b", store, v16i8, MSA128BOpnd, mem_simm10>;
class ST_H_DESC : ST_DESC_BASE<"st.h", store, v8i16, MSA128HOpnd,
                               mem_simm10_lsl1, addrimm10lsl1>;
class ST_W_DESC : ST_DESC_BASE<"st.w", store, v4i32, MSA128WOpnd,
                               mem_simm10_lsl2, addrimm10lsl2>;
class ST_D_DESC : ST_DESC_BASE<"st.d", store, v2i64, MSA128DOpnd,
                               mem_simm10_lsl3, addrimm10lsl3>;

class MSA_STORE_PSEUDO_BASE<SDPatternOperator intrinsic, RegisterOperand RO> :
  PseudoSE<(outs), (ins RO:$dst, PtrRC:$ptr, GPR32:$imm),
           [(intrinsic RO:$dst, iPTR:$ptr, GPR32:$imm)]> {
  let hasNoSchedulingInfo = 1;
  let usesCustomInserter = 1;
}

def STR_D : MSA_STORE_PSEUDO_BASE<int_mips_str_d, MSA128DOpnd>;
def STR_W : MSA_STORE_PSEUDO_BASE<int_mips_str_w, MSA128WOpnd>;

class SUBS_S_B_DESC : MSA_3R_DESC_BASE<"subs_s.b", int_mips_subs_s_b,
                                       MSA128BOpnd>;
class SUBS_S_H_DESC : MSA_3R_DESC_BASE<"subs_s.h", int_mips_subs_s_h,
                                       MSA128HOpnd>;
class SUBS_S_W_DESC : MSA_3R_DESC_BASE<"subs_s.w", int_mips_subs_s_w,
                                       MSA128WOpnd>;
class SUBS_S_D_DESC : MSA_3R_DESC_BASE<"subs_s.d", int_mips_subs_s_d,
                                       MSA128DOpnd>;

class SUBS_U_B_DESC : MSA_3R_DESC_BASE<"subs_u.b", int_mips_subs_u_b,
                                       MSA128BOpnd>;
class SUBS_U_H_DESC : MSA_3R_DESC_BASE<"subs_u.h", int_mips_subs_u_h,
                                       MSA128HOpnd>;
class SUBS_U_W_DESC : MSA_3R_DESC_BASE<"subs_u.w", int_mips_subs_u_w,
                                       MSA128WOpnd>;
class SUBS_U_D_DESC : MSA_3R_DESC_BASE<"subs_u.d", int_mips_subs_u_d,
                                       MSA128DOpnd>;

class SUBSUS_U_B_DESC : MSA_3R_DESC_BASE<"subsus_u.b", int_mips_subsus_u_b,
                                         MSA128BOpnd>;
class SUBSUS_U_H_DESC : MSA_3R_DESC_BASE<"subsus_u.h", int_mips_subsus_u_h,
                                         MSA128HOpnd>;
class SUBSUS_U_W_DESC : MSA_3R_DESC_BASE<"subsus_u.w", int_mips_subsus_u_w,
                                         MSA128WOpnd>;
class SUBSUS_U_D_DESC : MSA_3R_DESC_BASE<"subsus_u.d", int_mips_subsus_u_d,
                                         MSA128DOpnd>;

class SUBSUU_S_B_DESC : MSA_3R_DESC_BASE<"subsuu_s.b", int_mips_subsuu_s_b,
                                         MSA128BOpnd>;
class SUBSUU_S_H_DESC : MSA_3R_DESC_BASE<"subsuu_s.h", int_mips_subsuu_s_h,
                                         MSA128HOpnd>;
class SUBSUU_S_W_DESC : MSA_3R_DESC_BASE<"subsuu_s.w", int_mips_subsuu_s_w,
                                         MSA128WOpnd>;
class SUBSUU_S_D_DESC : MSA_3R_DESC_BASE<"subsuu_s.d", int_mips_subsuu_s_d,
                                         MSA128DOpnd>;

class SUBV_B_DESC : MSA_3R_DESC_BASE<"subv.b", sub, MSA128BOpnd>;
class SUBV_H_DESC : MSA_3R_DESC_BASE<"subv.h", sub, MSA128HOpnd>;
class SUBV_W_DESC : MSA_3R_DESC_BASE<"subv.w", sub, MSA128WOpnd>;
class SUBV_D_DESC : MSA_3R_DESC_BASE<"subv.d", sub, MSA128DOpnd>;

class SUBVI_B_DESC : MSA_I5_DESC_BASE<"subvi.b", sub, vsplati8_uimm5,
                                      MSA128BOpnd>;
class SUBVI_H_DESC : MSA_I5_DESC_BASE<"subvi.h", sub, vsplati16_uimm5,
                                      MSA128HOpnd>;
class SUBVI_W_DESC : MSA_I5_DESC_BASE<"subvi.w", sub, vsplati32_uimm5,
                                      MSA128WOpnd>;
class SUBVI_D_DESC : MSA_I5_DESC_BASE<"subvi.d", sub, vsplati64_uimm5,
                                      MSA128DOpnd>;

class VSHF_B_DESC : MSA_3R_VSHF_DESC_BASE<"vshf.b", MSA128BOpnd>;
class VSHF_H_DESC : MSA_3R_VSHF_DESC_BASE<"vshf.h", MSA128HOpnd>;
class VSHF_W_DESC : MSA_3R_VSHF_DESC_BASE<"vshf.w", MSA128WOpnd>;
class VSHF_D_DESC : MSA_3R_VSHF_DESC_BASE<"vshf.d", MSA128DOpnd>;

class XOR_V_DESC : MSA_VEC_DESC_BASE<"xor.v", xor, MSA128BOpnd>;
class XOR_V_H_PSEUDO_DESC : MSA_VEC_PSEUDO_BASE<xor, MSA128HOpnd>;
class XOR_V_W_PSEUDO_DESC : MSA_VEC_PSEUDO_BASE<xor, MSA128WOpnd>;
class XOR_V_D_PSEUDO_DESC : MSA_VEC_PSEUDO_BASE<xor, MSA128DOpnd>;

class XORI_B_DESC : MSA_I8_DESC_BASE<"xori.b", xor, vsplati8_uimm8,
                                     MSA128BOpnd>;

// Instruction defs.
def ADD_A_B : ADD_A_B_ENC, ADD_A_B_DESC;
def ADD_A_H : ADD_A_H_ENC, ADD_A_H_DESC;
def ADD_A_W : ADD_A_W_ENC, ADD_A_W_DESC;
def ADD_A_D : ADD_A_D_ENC, ADD_A_D_DESC;

def ADDS_A_B : ADDS_A_B_ENC, ADDS_A_B_DESC;
def ADDS_A_H : ADDS_A_H_ENC, ADDS_A_H_DESC;
def ADDS_A_W : ADDS_A_W_ENC, ADDS_A_W_DESC;
def ADDS_A_D : ADDS_A_D_ENC, ADDS_A_D_DESC;

def ADDS_S_B : ADDS_S_B_ENC, ADDS_S_B_DESC;
def ADDS_S_H : ADDS_S_H_ENC, ADDS_S_H_DESC;
def ADDS_S_W : ADDS_S_W_ENC, ADDS_S_W_DESC;
def ADDS_S_D : ADDS_S_D_ENC, ADDS_S_D_DESC;

def ADDS_U_B : ADDS_U_B_ENC, ADDS_U_B_DESC;
def ADDS_U_H : ADDS_U_H_ENC, ADDS_U_H_DESC;
def ADDS_U_W : ADDS_U_W_ENC, ADDS_U_W_DESC;
def ADDS_U_D : ADDS_U_D_ENC, ADDS_U_D_DESC;

def ADDV_B : ADDV_B_ENC, ADDV_B_DESC;
def ADDV_H : ADDV_H_ENC, ADDV_H_DESC;
def ADDV_W : ADDV_W_ENC, ADDV_W_DESC;
def ADDV_D : ADDV_D_ENC, ADDV_D_DESC;

def ADDVI_B : ADDVI_B_ENC, ADDVI_B_DESC;
def ADDVI_H : ADDVI_H_ENC, ADDVI_H_DESC;
def ADDVI_W : ADDVI_W_ENC, ADDVI_W_DESC;
def ADDVI_D : ADDVI_D_ENC, ADDVI_D_DESC;

def AND_V : AND_V_ENC, AND_V_DESC;
def AND_V_H_PSEUDO : AND_V_H_PSEUDO_DESC,
                     PseudoInstExpansion<(AND_V MSA128BOpnd:$wd,
                                                MSA128BOpnd:$ws,
                                                MSA128BOpnd:$wt)>;
def AND_V_W_PSEUDO : AND_V_W_PSEUDO_DESC,
                     PseudoInstExpansion<(AND_V MSA128BOpnd:$wd,
                                                MSA128BOpnd:$ws,
                                                MSA128BOpnd:$wt)>;
def AND_V_D_PSEUDO : AND_V_D_PSEUDO_DESC,
                     PseudoInstExpansion<(AND_V MSA128BOpnd:$wd,
                                                MSA128BOpnd:$ws,
                                                MSA128BOpnd:$wt)>;

def ANDI_B : ANDI_B_ENC, ANDI_B_DESC;

def ASUB_S_B : ASUB_S_B_ENC, ASUB_S_B_DESC;
def ASUB_S_H : ASUB_S_H_ENC, ASUB_S_H_DESC;
def ASUB_S_W : ASUB_S_W_ENC, ASUB_S_W_DESC;
def ASUB_S_D : ASUB_S_D_ENC, ASUB_S_D_DESC;

def ASUB_U_B : ASUB_U_B_ENC, ASUB_U_B_DESC;
def ASUB_U_H : ASUB_U_H_ENC, ASUB_U_H_DESC;
def ASUB_U_W : ASUB_U_W_ENC, ASUB_U_W_DESC;
def ASUB_U_D : ASUB_U_D_ENC, ASUB_U_D_DESC;

def AVE_S_B : AVE_S_B_ENC, AVE_S_B_DESC;
def AVE_S_H : AVE_S_H_ENC, AVE_S_H_DESC;
def AVE_S_W : AVE_S_W_ENC, AVE_S_W_DESC;
def AVE_S_D : AVE_S_D_ENC, AVE_S_D_DESC;

def AVE_U_B : AVE_U_B_ENC, AVE_U_B_DESC;
def AVE_U_H : AVE_U_H_ENC, AVE_U_H_DESC;
def AVE_U_W : AVE_U_W_ENC, AVE_U_W_DESC;
def AVE_U_D : AVE_U_D_ENC, AVE_U_D_DESC;

def AVER_S_B : AVER_S_B_ENC, AVER_S_B_DESC;
def AVER_S_H : AVER_S_H_ENC, AVER_S_H_DESC;
def AVER_S_W : AVER_S_W_ENC, AVER_S_W_DESC;
def AVER_S_D : AVER_S_D_ENC, AVER_S_D_DESC;

def AVER_U_B : AVER_U_B_ENC, AVER_U_B_DESC;
def AVER_U_H : AVER_U_H_ENC, AVER_U_H_DESC;
def AVER_U_W : AVER_U_W_ENC, AVER_U_W_DESC;
def AVER_U_D : AVER_U_D_ENC, AVER_U_D_DESC;

def BCLR_B : BCLR_B_ENC, BCLR_B_DESC;
def BCLR_H : BCLR_H_ENC, BCLR_H_DESC;
def BCLR_W : BCLR_W_ENC, BCLR_W_DESC;
def BCLR_D : BCLR_D_ENC, BCLR_D_DESC;

def BCLRI_B : BCLRI_B_ENC, BCLRI_B_DESC;
def BCLRI_H : BCLRI_H_ENC, BCLRI_H_DESC;
def BCLRI_W : BCLRI_W_ENC, BCLRI_W_DESC;
def BCLRI_D : BCLRI_D_ENC, BCLRI_D_DESC;

def BINSL_B : BINSL_B_ENC, BINSL_B_DESC;
def BINSL_H : BINSL_H_ENC, BINSL_H_DESC;
def BINSL_W : BINSL_W_ENC, BINSL_W_DESC;
def BINSL_D : BINSL_D_ENC, BINSL_D_DESC;

def BINSLI_B : BINSLI_B_ENC, BINSLI_B_DESC;
def BINSLI_H : BINSLI_H_ENC, BINSLI_H_DESC;
def BINSLI_W : BINSLI_W_ENC, BINSLI_W_DESC;
def BINSLI_D : BINSLI_D_ENC, BINSLI_D_DESC;

def BINSR_B : BINSR_B_ENC, BINSR_B_DESC;
def BINSR_H : BINSR_H_ENC, BINSR_H_DESC;
def BINSR_W : BINSR_W_ENC, BINSR_W_DESC;
def BINSR_D : BINSR_D_ENC, BINSR_D_DESC;

def BINSRI_B : BINSRI_B_ENC, BINSRI_B_DESC;
def BINSRI_H : BINSRI_H_ENC, BINSRI_H_DESC;
def BINSRI_W : BINSRI_W_ENC, BINSRI_W_DESC;
def BINSRI_D : BINSRI_D_ENC, BINSRI_D_DESC;

def BMNZ_V : BMNZ_V_ENC, BMNZ_V_DESC;

def BMNZI_B : BMNZI_B_ENC, BMNZI_B_DESC;

def BMZ_V : BMZ_V_ENC, BMZ_V_DESC;

def BMZI_B : BMZI_B_ENC, BMZI_B_DESC;

def BNEG_B : BNEG_B_ENC, BNEG_B_DESC;
def BNEG_H : BNEG_H_ENC, BNEG_H_DESC;
def BNEG_W : BNEG_W_ENC, BNEG_W_DESC;
def BNEG_D : BNEG_D_ENC, BNEG_D_DESC;

def BNEGI_B : BNEGI_B_ENC, BNEGI_B_DESC;
def BNEGI_H : BNEGI_H_ENC, BNEGI_H_DESC;
def BNEGI_W : BNEGI_W_ENC, BNEGI_W_DESC;
def BNEGI_D : BNEGI_D_ENC, BNEGI_D_DESC;

def BNZ_B : BNZ_B_ENC, BNZ_B_DESC;
def BNZ_H : BNZ_H_ENC, BNZ_H_DESC;
def BNZ_W : BNZ_W_ENC, BNZ_W_DESC;
def BNZ_D : BNZ_D_ENC, BNZ_D_DESC;

def BNZ_V : BNZ_V_ENC, BNZ_V_DESC;

def BSEL_V : BSEL_V_ENC, BSEL_V_DESC;

class MSA_BSEL_PSEUDO_BASE<RegisterOperand RO, ValueType Ty> :
  MSAPseudo<(outs RO:$wd), (ins RO:$wd_in, RO:$ws, RO:$wt),
            [(set RO:$wd, (Ty (vselect RO:$wd_in, RO:$wt, RO:$ws)))]>,
  // Note that vselect and BSEL_V treat the condition operand the opposite way
  // from each other.
  //   (vselect cond, if_set, if_clear)
  //   (BSEL_V cond, if_clear, if_set)
  PseudoInstExpansion<(BSEL_V MSA128BOpnd:$wd, MSA128BOpnd:$wd_in,
                              MSA128BOpnd:$ws, MSA128BOpnd:$wt)> {
  let Constraints = "$wd_in = $wd";
}

def BSEL_H_PSEUDO : MSA_BSEL_PSEUDO_BASE<MSA128HOpnd, v8i16>;
def BSEL_W_PSEUDO : MSA_BSEL_PSEUDO_BASE<MSA128WOpnd, v4i32>;
def BSEL_D_PSEUDO : MSA_BSEL_PSEUDO_BASE<MSA128DOpnd, v2i64>;
def BSEL_FW_PSEUDO : MSA_BSEL_PSEUDO_BASE<MSA128WOpnd, v4f32>;
def BSEL_FD_PSEUDO : MSA_BSEL_PSEUDO_BASE<MSA128DOpnd, v2f64>;

def BSELI_B : BSELI_B_ENC, BSELI_B_DESC;

def BSET_B : BSET_B_ENC, BSET_B_DESC;
def BSET_H : BSET_H_ENC, BSET_H_DESC;
def BSET_W : BSET_W_ENC, BSET_W_DESC;
def BSET_D : BSET_D_ENC, BSET_D_DESC;

def BSETI_B : BSETI_B_ENC, BSETI_B_DESC;
def BSETI_H : BSETI_H_ENC, BSETI_H_DESC;
def BSETI_W : BSETI_W_ENC, BSETI_W_DESC;
def BSETI_D : BSETI_D_ENC, BSETI_D_DESC;

def BZ_B : BZ_B_ENC, BZ_B_DESC;
def BZ_H : BZ_H_ENC, BZ_H_DESC;
def BZ_W : BZ_W_ENC, BZ_W_DESC;
def BZ_D : BZ_D_ENC, BZ_D_DESC;

def BZ_V : BZ_V_ENC, BZ_V_DESC;

def CEQ_B : CEQ_B_ENC, CEQ_B_DESC;
def CEQ_H : CEQ_H_ENC, CEQ_H_DESC;
def CEQ_W : CEQ_W_ENC, CEQ_W_DESC;
def CEQ_D : CEQ_D_ENC, CEQ_D_DESC;

def CEQI_B : CEQI_B_ENC, CEQI_B_DESC;
def CEQI_H : CEQI_H_ENC, CEQI_H_DESC;
def CEQI_W : CEQI_W_ENC, CEQI_W_DESC;
def CEQI_D : CEQI_D_ENC, CEQI_D_DESC;

def CFCMSA : CFCMSA_ENC, CFCMSA_DESC;

def CLE_S_B : CLE_S_B_ENC, CLE_S_B_DESC;
def CLE_S_H : CLE_S_H_ENC, CLE_S_H_DESC;
def CLE_S_W : CLE_S_W_ENC, CLE_S_W_DESC;
def CLE_S_D : CLE_S_D_ENC, CLE_S_D_DESC;

def CLE_U_B : CLE_U_B_ENC, CLE_U_B_DESC;
def CLE_U_H : CLE_U_H_ENC, CLE_U_H_DESC;
def CLE_U_W : CLE_U_W_ENC, CLE_U_W_DESC;
def CLE_U_D : CLE_U_D_ENC, CLE_U_D_DESC;

def CLEI_S_B : CLEI_S_B_ENC, CLEI_S_B_DESC;
def CLEI_S_H : CLEI_S_H_ENC, CLEI_S_H_DESC;
def CLEI_S_W : CLEI_S_W_ENC, CLEI_S_W_DESC;
def CLEI_S_D : CLEI_S_D_ENC, CLEI_S_D_DESC;

def CLEI_U_B : CLEI_U_B_ENC, CLEI_U_B_DESC;
def CLEI_U_H : CLEI_U_H_ENC, CLEI_U_H_DESC;
def CLEI_U_W : CLEI_U_W_ENC, CLEI_U_W_DESC;
def CLEI_U_D : CLEI_U_D_ENC, CLEI_U_D_DESC;

def CLT_S_B : CLT_S_B_ENC, CLT_S_B_DESC;
def CLT_S_H : CLT_S_H_ENC, CLT_S_H_DESC;
def CLT_S_W : CLT_S_W_ENC, CLT_S_W_DESC;
def CLT_S_D : CLT_S_D_ENC, CLT_S_D_DESC;

def CLT_U_B : CLT_U_B_ENC, CLT_U_B_DESC;
def CLT_U_H : CLT_U_H_ENC, CLT_U_H_DESC;
def CLT_U_W : CLT_U_W_ENC, CLT_U_W_DESC;
def CLT_U_D : CLT_U_D_ENC, CLT_U_D_DESC;

def CLTI_S_B : CLTI_S_B_ENC, CLTI_S_B_DESC;
def CLTI_S_H : CLTI_S_H_ENC, CLTI_S_H_DESC;
def CLTI_S_W : CLTI_S_W_ENC, CLTI_S_W_DESC;
def CLTI_S_D : CLTI_S_D_ENC, CLTI_S_D_DESC;

def CLTI_U_B : CLTI_U_B_ENC, CLTI_U_B_DESC;
def CLTI_U_H : CLTI_U_H_ENC, CLTI_U_H_DESC;
def CLTI_U_W : CLTI_U_W_ENC, CLTI_U_W_DESC;
def CLTI_U_D : CLTI_U_D_ENC, CLTI_U_D_DESC;

def COPY_S_B : COPY_S_B_ENC, COPY_S_B_DESC;
def COPY_S_H : COPY_S_H_ENC, COPY_S_H_DESC;
def COPY_S_W : COPY_S_W_ENC, COPY_S_W_DESC;
def COPY_S_D : COPY_S_D_ENC, COPY_S_D_DESC, ASE_MSA64;

def COPY_U_B : COPY_U_B_ENC, COPY_U_B_DESC;
def COPY_U_H : COPY_U_H_ENC, COPY_U_H_DESC;
def COPY_U_W : COPY_U_W_ENC, COPY_U_W_DESC, ASE_MSA64;

def COPY_FW_PSEUDO : COPY_FW_PSEUDO_DESC;
def COPY_FD_PSEUDO : COPY_FD_PSEUDO_DESC;

def CTCMSA : CTCMSA_ENC, CTCMSA_DESC;

def DIV_S_B : DIV_S_B_ENC, DIV_S_B_DESC;
def DIV_S_H : DIV_S_H_ENC, DIV_S_H_DESC;
def DIV_S_W : DIV_S_W_ENC, DIV_S_W_DESC;
def DIV_S_D : DIV_S_D_ENC, DIV_S_D_DESC;

def DIV_U_B : DIV_U_B_ENC, DIV_U_B_DESC;
def DIV_U_H : DIV_U_H_ENC, DIV_U_H_DESC;
def DIV_U_W : DIV_U_W_ENC, DIV_U_W_DESC;
def DIV_U_D : DIV_U_D_ENC, DIV_U_D_DESC;

def DOTP_S_H : DOTP_S_H_ENC, DOTP_S_H_DESC;
def DOTP_S_W : DOTP_S_W_ENC, DOTP_S_W_DESC;
def DOTP_S_D : DOTP_S_D_ENC, DOTP_S_D_DESC;

def DOTP_U_H : DOTP_U_H_ENC, DOTP_U_H_DESC;
def DOTP_U_W : DOTP_U_W_ENC, DOTP_U_W_DESC;
def DOTP_U_D : DOTP_U_D_ENC, DOTP_U_D_DESC;

def DPADD_S_H : DPADD_S_H_ENC, DPADD_S_H_DESC;
def DPADD_S_W : DPADD_S_W_ENC, DPADD_S_W_DESC;
def DPADD_S_D : DPADD_S_D_ENC, DPADD_S_D_DESC;

def DPADD_U_H : DPADD_U_H_ENC, DPADD_U_H_DESC;
def DPADD_U_W : DPADD_U_W_ENC, DPADD_U_W_DESC;
def DPADD_U_D : DPADD_U_D_ENC, DPADD_U_D_DESC;

def DPSUB_S_H : DPSUB_S_H_ENC, DPSUB_S_H_DESC;
def DPSUB_S_W : DPSUB_S_W_ENC, DPSUB_S_W_DESC;
def DPSUB_S_D : DPSUB_S_D_ENC, DPSUB_S_D_DESC;

def DPSUB_U_H : DPSUB_U_H_ENC, DPSUB_U_H_DESC;
def DPSUB_U_W : DPSUB_U_W_ENC, DPSUB_U_W_DESC;
def DPSUB_U_D : DPSUB_U_D_ENC, DPSUB_U_D_DESC;

def FADD_W : FADD_W_ENC, FADD_W_DESC;
def FADD_D : FADD_D_ENC, FADD_D_DESC;

def FCAF_W : FCAF_W_ENC, FCAF_W_DESC;
def FCAF_D : FCAF_D_ENC, FCAF_D_DESC;

def FCEQ_W : FCEQ_W_ENC, FCEQ_W_DESC;
def FCEQ_D : FCEQ_D_ENC, FCEQ_D_DESC;

def FCLE_W : FCLE_W_ENC, FCLE_W_DESC;
def FCLE_D : FCLE_D_ENC, FCLE_D_DESC;

def FCLT_W : FCLT_W_ENC, FCLT_W_DESC;
def FCLT_D : FCLT_D_ENC, FCLT_D_DESC;

def FCLASS_W : FCLASS_W_ENC, FCLASS_W_DESC;
def FCLASS_D : FCLASS_D_ENC, FCLASS_D_DESC;

def FCNE_W : FCNE_W_ENC, FCNE_W_DESC;
def FCNE_D : FCNE_D_ENC, FCNE_D_DESC;

def FCOR_W : FCOR_W_ENC, FCOR_W_DESC;
def FCOR_D : FCOR_D_ENC, FCOR_D_DESC;

def FCUEQ_W : FCUEQ_W_ENC, FCUEQ_W_DESC;
def FCUEQ_D : FCUEQ_D_ENC, FCUEQ_D_DESC;

def FCULE_W : FCULE_W_ENC, FCULE_W_DESC;
def FCULE_D : FCULE_D_ENC, FCULE_D_DESC;

def FCULT_W : FCULT_W_ENC, FCULT_W_DESC;
def FCULT_D : FCULT_D_ENC, FCULT_D_DESC;

def FCUN_W : FCUN_W_ENC, FCUN_W_DESC;
def FCUN_D : FCUN_D_ENC, FCUN_D_DESC;

def FCUNE_W : FCUNE_W_ENC, FCUNE_W_DESC;
def FCUNE_D : FCUNE_D_ENC, FCUNE_D_DESC;

def FDIV_W : FDIV_W_ENC, FDIV_W_DESC;
def FDIV_D : FDIV_D_ENC, FDIV_D_DESC;

def FEXDO_H : FEXDO_H_ENC, FEXDO_H_DESC;
def FEXDO_W : FEXDO_W_ENC, FEXDO_W_DESC;

def FEXP2_W : FEXP2_W_ENC, FEXP2_W_DESC;
def FEXP2_D : FEXP2_D_ENC, FEXP2_D_DESC;
def FEXP2_W_1_PSEUDO : FEXP2_W_1_PSEUDO_DESC;
def FEXP2_D_1_PSEUDO : FEXP2_D_1_PSEUDO_DESC;

def FEXUPL_W : FEXUPL_W_ENC, FEXUPL_W_DESC;
def FEXUPL_D : FEXUPL_D_ENC, FEXUPL_D_DESC;

def FEXUPR_W : FEXUPR_W_ENC, FEXUPR_W_DESC;
def FEXUPR_D : FEXUPR_D_ENC, FEXUPR_D_DESC;

def FFINT_S_W : FFINT_S_W_ENC, FFINT_S_W_DESC;
def FFINT_S_D : FFINT_S_D_ENC, FFINT_S_D_DESC;

def FFINT_U_W : FFINT_U_W_ENC, FFINT_U_W_DESC;
def FFINT_U_D : FFINT_U_D_ENC, FFINT_U_D_DESC;

def FFQL_W : FFQL_W_ENC, FFQL_W_DESC;
def FFQL_D : FFQL_D_ENC, FFQL_D_DESC;

def FFQR_W : FFQR_W_ENC, FFQR_W_DESC;
def FFQR_D : FFQR_D_ENC, FFQR_D_DESC;

def FILL_B : FILL_B_ENC, FILL_B_DESC;
def FILL_H : FILL_H_ENC, FILL_H_DESC;
def FILL_W : FILL_W_ENC, FILL_W_DESC;
def FILL_D : FILL_D_ENC, FILL_D_DESC, ASE_MSA64;
def FILL_FW_PSEUDO : FILL_FW_PSEUDO_DESC;
def FILL_FD_PSEUDO : FILL_FD_PSEUDO_DESC;

def FLOG2_W : FLOG2_W_ENC, FLOG2_W_DESC;
def FLOG2_D : FLOG2_D_ENC, FLOG2_D_DESC;

def FMADD_W : FMADD_W_ENC, FMADD_W_DESC;
def FMADD_D : FMADD_D_ENC, FMADD_D_DESC;

def FMAX_W : FMAX_W_ENC, FMAX_W_DESC;
def FMAX_D : FMAX_D_ENC, FMAX_D_DESC;

def FMAX_A_W : FMAX_A_W_ENC, FMAX_A_W_DESC;
def FMAX_A_D : FMAX_A_D_ENC, FMAX_A_D_DESC;

def FMIN_W : FMIN_W_ENC, FMIN_W_DESC;
def FMIN_D : FMIN_D_ENC, FMIN_D_DESC;

def FMIN_A_W : FMIN_A_W_ENC, FMIN_A_W_DESC;
def FMIN_A_D : FMIN_A_D_ENC, FMIN_A_D_DESC;

def FMSUB_W : FMSUB_W_ENC, FMSUB_W_DESC;
def FMSUB_D : FMSUB_D_ENC, FMSUB_D_DESC;

def FMUL_W : FMUL_W_ENC, FMUL_W_DESC;
def FMUL_D : FMUL_D_ENC, FMUL_D_DESC;

def FRINT_W : FRINT_W_ENC, FRINT_W_DESC;
def FRINT_D : FRINT_D_ENC, FRINT_D_DESC;

def FRCP_W : FRCP_W_ENC, FRCP_W_DESC;
def FRCP_D : FRCP_D_ENC, FRCP_D_DESC;

def FRSQRT_W : FRSQRT_W_ENC, FRSQRT_W_DESC;
def FRSQRT_D : FRSQRT_D_ENC, FRSQRT_D_DESC;

def FSAF_W : FSAF_W_ENC, FSAF_W_DESC;
def FSAF_D : FSAF_D_ENC, FSAF_D_DESC;

def FSEQ_W : FSEQ_W_ENC, FSEQ_W_DESC;
def FSEQ_D : FSEQ_D_ENC, FSEQ_D_DESC;

def FSLE_W : FSLE_W_ENC, FSLE_W_DESC;
def FSLE_D : FSLE_D_ENC, FSLE_D_DESC;

def FSLT_W : FSLT_W_ENC, FSLT_W_DESC;
def FSLT_D : FSLT_D_ENC, FSLT_D_DESC;

def FSNE_W : FSNE_W_ENC, FSNE_W_DESC;
def FSNE_D : FSNE_D_ENC, FSNE_D_DESC;

def FSOR_W : FSOR_W_ENC, FSOR_W_DESC;
def FSOR_D : FSOR_D_ENC, FSOR_D_DESC;

def FSQRT_W : FSQRT_W_ENC, FSQRT_W_DESC;
def FSQRT_D : FSQRT_D_ENC, FSQRT_D_DESC;

def FSUB_W : FSUB_W_ENC, FSUB_W_DESC;
def FSUB_D : FSUB_D_ENC, FSUB_D_DESC;

def FSUEQ_W : FSUEQ_W_ENC, FSUEQ_W_DESC;
def FSUEQ_D : FSUEQ_D_ENC, FSUEQ_D_DESC;

def FSULE_W : FSULE_W_ENC, FSULE_W_DESC;
def FSULE_D : FSULE_D_ENC, FSULE_D_DESC;

def FSULT_W : FSULT_W_ENC, FSULT_W_DESC;
def FSULT_D : FSULT_D_ENC, FSULT_D_DESC;

def FSUN_W : FSUN_W_ENC, FSUN_W_DESC;
def FSUN_D : FSUN_D_ENC, FSUN_D_DESC;

def FSUNE_W : FSUNE_W_ENC, FSUNE_W_DESC;
def FSUNE_D : FSUNE_D_ENC, FSUNE_D_DESC;

def FTINT_S_W : FTINT_S_W_ENC, FTINT_S_W_DESC;
def FTINT_S_D : FTINT_S_D_ENC, FTINT_S_D_DESC;

def FTINT_U_W : FTINT_U_W_ENC, FTINT_U_W_DESC;
def FTINT_U_D : FTINT_U_D_ENC, FTINT_U_D_DESC;

def FTQ_H : FTQ_H_ENC, FTQ_H_DESC;
def FTQ_W : FTQ_W_ENC, FTQ_W_DESC;

def FTRUNC_S_W : FTRUNC_S_W_ENC, FTRUNC_S_W_DESC;
def FTRUNC_S_D : FTRUNC_S_D_ENC, FTRUNC_S_D_DESC;

def FTRUNC_U_W : FTRUNC_U_W_ENC, FTRUNC_U_W_DESC;
def FTRUNC_U_D : FTRUNC_U_D_ENC, FTRUNC_U_D_DESC;

def : MipsPat<(fsub MSA128WOpnd:$wd, (fmul MSA128WOpnd:$ws, MSA128WOpnd:$wt)),
              (FMSUB_W MSA128WOpnd:$wd, MSA128WOpnd:$ws, MSA128WOpnd:$wt)>,
              ISA_MIPS1, ASE_MSA, FPOP_FUSION_FAST;
def : MipsPat<(fsub MSA128DOpnd:$wd, (fmul MSA128DOpnd:$ws, MSA128DOpnd:$wt)),
              (FMSUB_D MSA128DOpnd:$wd, MSA128DOpnd:$ws, MSA128DOpnd:$wt)>,
              ISA_MIPS1, ASE_MSA, FPOP_FUSION_FAST;

def : MipsPat<(fadd MSA128WOpnd:$wd, (fmul MSA128WOpnd:$ws, MSA128WOpnd:$wt)),
              (FMADD_W MSA128WOpnd:$wd, MSA128WOpnd:$ws, MSA128WOpnd:$wt)>,
              ISA_MIPS1, ASE_MSA, FPOP_FUSION_FAST;
def : MipsPat<(fadd MSA128DOpnd:$wd, (fmul MSA128DOpnd:$ws, MSA128DOpnd:$wt)),
              (FMADD_D MSA128DOpnd:$wd, MSA128DOpnd:$ws, MSA128DOpnd:$wt)>,
              ISA_MIPS1, ASE_MSA, FPOP_FUSION_FAST;

def HADD_S_H : HADD_S_H_ENC, HADD_S_H_DESC;
def HADD_S_W : HADD_S_W_ENC, HADD_S_W_DESC;
def HADD_S_D : HADD_S_D_ENC, HADD_S_D_DESC;

def HADD_U_H : HADD_U_H_ENC, HADD_U_H_DESC;
def HADD_U_W : HADD_U_W_ENC, HADD_U_W_DESC;
def HADD_U_D : HADD_U_D_ENC, HADD_U_D_DESC;

def HSUB_S_H : HSUB_S_H_ENC, HSUB_S_H_DESC;
def HSUB_S_W : HSUB_S_W_ENC, HSUB_S_W_DESC;
def HSUB_S_D : HSUB_S_D_ENC, HSUB_S_D_DESC;

def HSUB_U_H : HSUB_U_H_ENC, HSUB_U_H_DESC;
def HSUB_U_W : HSUB_U_W_ENC, HSUB_U_W_DESC;
def HSUB_U_D : HSUB_U_D_ENC, HSUB_U_D_DESC;

def ILVEV_B : ILVEV_B_ENC, ILVEV_B_DESC;
def ILVEV_H : ILVEV_H_ENC, ILVEV_H_DESC;
def ILVEV_W : ILVEV_W_ENC, ILVEV_W_DESC;
def ILVEV_D : ILVEV_D_ENC, ILVEV_D_DESC;

def ILVL_B : ILVL_B_ENC, ILVL_B_DESC;
def ILVL_H : ILVL_H_ENC, ILVL_H_DESC;
def ILVL_W : ILVL_W_ENC, ILVL_W_DESC;
def ILVL_D : ILVL_D_ENC, ILVL_D_DESC;

def ILVOD_B : ILVOD_B_ENC, ILVOD_B_DESC;
def ILVOD_H : ILVOD_H_ENC, ILVOD_H_DESC;
def ILVOD_W : ILVOD_W_ENC, ILVOD_W_DESC;
def ILVOD_D : ILVOD_D_ENC, ILVOD_D_DESC;

def ILVR_B : ILVR_B_ENC, ILVR_B_DESC;
def ILVR_H : ILVR_H_ENC, ILVR_H_DESC;
def ILVR_W : ILVR_W_ENC, ILVR_W_DESC;
def ILVR_D : ILVR_D_ENC, ILVR_D_DESC;

def INSERT_B : INSERT_B_ENC, INSERT_B_DESC;
def INSERT_H : INSERT_H_ENC, INSERT_H_DESC;
def INSERT_W : INSERT_W_ENC, INSERT_W_DESC;
def INSERT_D : INSERT_D_ENC, INSERT_D_DESC, ASE_MSA64;

// INSERT_FW_PSEUDO defined after INSVE_W
// INSERT_FD_PSEUDO defined after INSVE_D

// There is a fourth operand that is not present in the encoding. Use a
// custom decoder to get a chance to add it.
let DecoderMethod = "DecodeINSVE_DF" in {
  def INSVE_B : INSVE_B_ENC, INSVE_B_DESC;
  def INSVE_H : INSVE_H_ENC, INSVE_H_DESC;
  def INSVE_W : INSVE_W_ENC, INSVE_W_DESC;
  def INSVE_D : INSVE_D_ENC, INSVE_D_DESC;
}

def INSERT_FW_PSEUDO : INSERT_FW_PSEUDO_DESC;
def INSERT_FD_PSEUDO : INSERT_FD_PSEUDO_DESC;

def INSERT_B_VIDX_PSEUDO : INSERT_B_VIDX_PSEUDO_DESC;
def INSERT_H_VIDX_PSEUDO : INSERT_H_VIDX_PSEUDO_DESC;
def INSERT_W_VIDX_PSEUDO : INSERT_W_VIDX_PSEUDO_DESC;
def INSERT_D_VIDX_PSEUDO : INSERT_D_VIDX_PSEUDO_DESC;
def INSERT_FW_VIDX_PSEUDO : INSERT_FW_VIDX_PSEUDO_DESC;
def INSERT_FD_VIDX_PSEUDO : INSERT_FD_VIDX_PSEUDO_DESC;

def INSERT_B_VIDX64_PSEUDO : INSERT_B_VIDX64_PSEUDO_DESC;
def INSERT_H_VIDX64_PSEUDO : INSERT_H_VIDX64_PSEUDO_DESC;
def INSERT_W_VIDX64_PSEUDO : INSERT_W_VIDX64_PSEUDO_DESC;
def INSERT_D_VIDX64_PSEUDO : INSERT_D_VIDX64_PSEUDO_DESC;
def INSERT_FW_VIDX64_PSEUDO : INSERT_FW_VIDX64_PSEUDO_DESC;
def INSERT_FD_VIDX64_PSEUDO : INSERT_FD_VIDX64_PSEUDO_DESC;

def LD_B: LD_B_ENC, LD_B_DESC;
def LD_H: LD_H_ENC, LD_H_DESC;
def LD_W: LD_W_ENC, LD_W_DESC;
def LD_D: LD_D_ENC, LD_D_DESC;

def LDI_B : LDI_B_ENC, LDI_B_DESC;
def LDI_H : LDI_H_ENC, LDI_H_DESC;
def LDI_W : LDI_W_ENC, LDI_W_DESC;
def LDI_D : LDI_D_ENC, LDI_D_DESC;

def LSA : LSA_ENC, LSA_DESC;
def DLSA : DLSA_ENC, DLSA_DESC, ASE_MSA64;

def MADD_Q_H : MADD_Q_H_ENC, MADD_Q_H_DESC;
def MADD_Q_W : MADD_Q_W_ENC, MADD_Q_W_DESC;

def MADDR_Q_H : MADDR_Q_H_ENC, MADDR_Q_H_DESC;
def MADDR_Q_W : MADDR_Q_W_ENC, MADDR_Q_W_DESC;

def MADDV_B : MADDV_B_ENC, MADDV_B_DESC;
def MADDV_H : MADDV_H_ENC, MADDV_H_DESC;
def MADDV_W : MADDV_W_ENC, MADDV_W_DESC;
def MADDV_D : MADDV_D_ENC, MADDV_D_DESC;

def MAX_A_B : MAX_A_B_ENC, MAX_A_B_DESC;
def MAX_A_H : MAX_A_H_ENC, MAX_A_H_DESC;
def MAX_A_W : MAX_A_W_ENC, MAX_A_W_DESC;
def MAX_A_D : MAX_A_D_ENC, MAX_A_D_DESC;

def MAX_S_B : MAX_S_B_ENC, MAX_S_B_DESC;
def MAX_S_H : MAX_S_H_ENC, MAX_S_H_DESC;
def MAX_S_W : MAX_S_W_ENC, MAX_S_W_DESC;
def MAX_S_D : MAX_S_D_ENC, MAX_S_D_DESC;

def MAX_U_B : MAX_U_B_ENC, MAX_U_B_DESC;
def MAX_U_H : MAX_U_H_ENC, MAX_U_H_DESC;
def MAX_U_W : MAX_U_W_ENC, MAX_U_W_DESC;
def MAX_U_D : MAX_U_D_ENC, MAX_U_D_DESC;

def MAXI_S_B : MAXI_S_B_ENC, MAXI_S_B_DESC;
def MAXI_S_H : MAXI_S_H_ENC, MAXI_S_H_DESC;
def MAXI_S_W : MAXI_S_W_ENC, MAXI_S_W_DESC;
def MAXI_S_D : MAXI_S_D_ENC, MAXI_S_D_DESC;

def MAXI_U_B : MAXI_U_B_ENC, MAXI_U_B_DESC;
def MAXI_U_H : MAXI_U_H_ENC, MAXI_U_H_DESC;
def MAXI_U_W : MAXI_U_W_ENC, MAXI_U_W_DESC;
def MAXI_U_D : MAXI_U_D_ENC, MAXI_U_D_DESC;

def MIN_A_B : MIN_A_B_ENC, MIN_A_B_DESC;
def MIN_A_H : MIN_A_H_ENC, MIN_A_H_DESC;
def MIN_A_W : MIN_A_W_ENC, MIN_A_W_DESC;
def MIN_A_D : MIN_A_D_ENC, MIN_A_D_DESC;

def MIN_S_B : MIN_S_B_ENC, MIN_S_B_DESC;
def MIN_S_H : MIN_S_H_ENC, MIN_S_H_DESC;
def MIN_S_W : MIN_S_W_ENC, MIN_S_W_DESC;
def MIN_S_D : MIN_S_D_ENC, MIN_S_D_DESC;

def MIN_U_B : MIN_U_B_ENC, MIN_U_B_DESC;
def MIN_U_H : MIN_U_H_ENC, MIN_U_H_DESC;
def MIN_U_W : MIN_U_W_ENC, MIN_U_W_DESC;
def MIN_U_D : MIN_U_D_ENC, MIN_U_D_DESC;

def MINI_S_B : MINI_S_B_ENC, MINI_S_B_DESC;
def MINI_S_H : MINI_S_H_ENC, MINI_S_H_DESC;
def MINI_S_W : MINI_S_W_ENC, MINI_S_W_DESC;
def MINI_S_D : MINI_S_D_ENC, MINI_S_D_DESC;

def MINI_U_B : MINI_U_B_ENC, MINI_U_B_DESC;
def MINI_U_H : MINI_U_H_ENC, MINI_U_H_DESC;
def MINI_U_W : MINI_U_W_ENC, MINI_U_W_DESC;
def MINI_U_D : MINI_U_D_ENC, MINI_U_D_DESC;

def MOD_S_B : MOD_S_B_ENC, MOD_S_B_DESC;
def MOD_S_H : MOD_S_H_ENC, MOD_S_H_DESC;
def MOD_S_W : MOD_S_W_ENC, MOD_S_W_DESC;
def MOD_S_D : MOD_S_D_ENC, MOD_S_D_DESC;

def MOD_U_B : MOD_U_B_ENC, MOD_U_B_DESC;
def MOD_U_H : MOD_U_H_ENC, MOD_U_H_DESC;
def MOD_U_W : MOD_U_W_ENC, MOD_U_W_DESC;
def MOD_U_D : MOD_U_D_ENC, MOD_U_D_DESC;

def MOVE_V : MOVE_V_ENC, MOVE_V_DESC;

def MSUB_Q_H : MSUB_Q_H_ENC, MSUB_Q_H_DESC;
def MSUB_Q_W : MSUB_Q_W_ENC, MSUB_Q_W_DESC;

def MSUBR_Q_H : MSUBR_Q_H_ENC, MSUBR_Q_H_DESC;
def MSUBR_Q_W : MSUBR_Q_W_ENC, MSUBR_Q_W_DESC;

def MSUBV_B : MSUBV_B_ENC, MSUBV_B_DESC;
def MSUBV_H : MSUBV_H_ENC, MSUBV_H_DESC;
def MSUBV_W : MSUBV_W_ENC, MSUBV_W_DESC;
def MSUBV_D : MSUBV_D_ENC, MSUBV_D_DESC;

def MUL_Q_H : MUL_Q_H_ENC, MUL_Q_H_DESC;
def MUL_Q_W : MUL_Q_W_ENC, MUL_Q_W_DESC;

def MULR_Q_H : MULR_Q_H_ENC, MULR_Q_H_DESC;
def MULR_Q_W : MULR_Q_W_ENC, MULR_Q_W_DESC;

def MULV_B : MULV_B_ENC, MULV_B_DESC;
def MULV_H : MULV_H_ENC, MULV_H_DESC;
def MULV_W : MULV_W_ENC, MULV_W_DESC;
def MULV_D : MULV_D_ENC, MULV_D_DESC;

def NLOC_B : NLOC_B_ENC, NLOC_B_DESC;
def NLOC_H : NLOC_H_ENC, NLOC_H_DESC;
def NLOC_W : NLOC_W_ENC, NLOC_W_DESC;
def NLOC_D : NLOC_D_ENC, NLOC_D_DESC;

def NLZC_B : NLZC_B_ENC, NLZC_B_DESC;
def NLZC_H : NLZC_H_ENC, NLZC_H_DESC;
def NLZC_W : NLZC_W_ENC, NLZC_W_DESC;
def NLZC_D : NLZC_D_ENC, NLZC_D_DESC;

def NOR_V : NOR_V_ENC, NOR_V_DESC;
def NOR_V_H_PSEUDO : NOR_V_H_PSEUDO_DESC,
                     PseudoInstExpansion<(NOR_V MSA128BOpnd:$wd,
                                                MSA128BOpnd:$ws,
                                                MSA128BOpnd:$wt)>;
def NOR_V_W_PSEUDO : NOR_V_W_PSEUDO_DESC,
                     PseudoInstExpansion<(NOR_V MSA128BOpnd:$wd,
                                                MSA128BOpnd:$ws,
                                                MSA128BOpnd:$wt)>;
def NOR_V_D_PSEUDO : NOR_V_D_PSEUDO_DESC,
                     PseudoInstExpansion<(NOR_V MSA128BOpnd:$wd,
                                                MSA128BOpnd:$ws,
                                                MSA128BOpnd:$wt)>;

def NORI_B : NORI_B_ENC, NORI_B_DESC;

def OR_V : OR_V_ENC, OR_V_DESC;
def OR_V_H_PSEUDO : OR_V_H_PSEUDO_DESC,
                    PseudoInstExpansion<(OR_V MSA128BOpnd:$wd,
                                              MSA128BOpnd:$ws,
                                              MSA128BOpnd:$wt)>;
def OR_V_W_PSEUDO : OR_V_W_PSEUDO_DESC,
                    PseudoInstExpansion<(OR_V MSA128BOpnd:$wd,
                                              MSA128BOpnd:$ws,
                                              MSA128BOpnd:$wt)>;
def OR_V_D_PSEUDO : OR_V_D_PSEUDO_DESC,
                    PseudoInstExpansion<(OR_V MSA128BOpnd:$wd,
                                              MSA128BOpnd:$ws,
                                              MSA128BOpnd:$wt)>;

def ORI_B : ORI_B_ENC, ORI_B_DESC;

def PCKEV_B : PCKEV_B_ENC, PCKEV_B_DESC;
def PCKEV_H : PCKEV_H_ENC, PCKEV_H_DESC;
def PCKEV_W : PCKEV_W_ENC, PCKEV_W_DESC;
def PCKEV_D : PCKEV_D_ENC, PCKEV_D_DESC;

def PCKOD_B : PCKOD_B_ENC, PCKOD_B_DESC;
def PCKOD_H : PCKOD_H_ENC, PCKOD_H_DESC;
def PCKOD_W : PCKOD_W_ENC, PCKOD_W_DESC;
def PCKOD_D : PCKOD_D_ENC, PCKOD_D_DESC;

def PCNT_B : PCNT_B_ENC, PCNT_B_DESC;
def PCNT_H : PCNT_H_ENC, PCNT_H_DESC;
def PCNT_W : PCNT_W_ENC, PCNT_W_DESC;
def PCNT_D : PCNT_D_ENC, PCNT_D_DESC;

def SAT_S_B : SAT_S_B_ENC, SAT_S_B_DESC;
def SAT_S_H : SAT_S_H_ENC, SAT_S_H_DESC;
def SAT_S_W : SAT_S_W_ENC, SAT_S_W_DESC;
def SAT_S_D : SAT_S_D_ENC, SAT_S_D_DESC;

def SAT_U_B : SAT_U_B_ENC, SAT_U_B_DESC;
def SAT_U_H : SAT_U_H_ENC, SAT_U_H_DESC;
def SAT_U_W : SAT_U_W_ENC, SAT_U_W_DESC;
def SAT_U_D : SAT_U_D_ENC, SAT_U_D_DESC;

def SHF_B : SHF_B_ENC, SHF_B_DESC;
def SHF_H : SHF_H_ENC, SHF_H_DESC;
def SHF_W : SHF_W_ENC, SHF_W_DESC;

def SLD_B : SLD_B_ENC, SLD_B_DESC;
def SLD_H : SLD_H_ENC, SLD_H_DESC;
def SLD_W : SLD_W_ENC, SLD_W_DESC;
def SLD_D : SLD_D_ENC, SLD_D_DESC;

def SLDI_B : SLDI_B_ENC, SLDI_B_DESC;
def SLDI_H : SLDI_H_ENC, SLDI_H_DESC;
def SLDI_W : SLDI_W_ENC, SLDI_W_DESC;
def SLDI_D : SLDI_D_ENC, SLDI_D_DESC;

def SLL_B : SLL_B_ENC, SLL_B_DESC;
def SLL_H : SLL_H_ENC, SLL_H_DESC;
def SLL_W : SLL_W_ENC, SLL_W_DESC;
def SLL_D : SLL_D_ENC, SLL_D_DESC;

def SLLI_B : SLLI_B_ENC, SLLI_B_DESC;
def SLLI_H : SLLI_H_ENC, SLLI_H_DESC;
def SLLI_W : SLLI_W_ENC, SLLI_W_DESC;
def SLLI_D : SLLI_D_ENC, SLLI_D_DESC;

def SPLAT_B : SPLAT_B_ENC, SPLAT_B_DESC;
def SPLAT_H : SPLAT_H_ENC, SPLAT_H_DESC;
def SPLAT_W : SPLAT_W_ENC, SPLAT_W_DESC;
def SPLAT_D : SPLAT_D_ENC, SPLAT_D_DESC;

def SPLATI_B : SPLATI_B_ENC, SPLATI_B_DESC;
def SPLATI_H : SPLATI_H_ENC, SPLATI_H_DESC;
def SPLATI_W : SPLATI_W_ENC, SPLATI_W_DESC;
def SPLATI_D : SPLATI_D_ENC, SPLATI_D_DESC;

def SRA_B : SRA_B_ENC, SRA_B_DESC;
def SRA_H : SRA_H_ENC, SRA_H_DESC;
def SRA_W : SRA_W_ENC, SRA_W_DESC;
def SRA_D : SRA_D_ENC, SRA_D_DESC;

def SRAI_B : SRAI_B_ENC, SRAI_B_DESC;
def SRAI_H : SRAI_H_ENC, SRAI_H_DESC;
def SRAI_W : SRAI_W_ENC, SRAI_W_DESC;
def SRAI_D : SRAI_D_ENC, SRAI_D_DESC;

def SRAR_B : SRAR_B_ENC, SRAR_B_DESC;
def SRAR_H : SRAR_H_ENC, SRAR_H_DESC;
def SRAR_W : SRAR_W_ENC, SRAR_W_DESC;
def SRAR_D : SRAR_D_ENC, SRAR_D_DESC;

def SRARI_B : SRARI_B_ENC, SRARI_B_DESC;
def SRARI_H : SRARI_H_ENC, SRARI_H_DESC;
def SRARI_W : SRARI_W_ENC, SRARI_W_DESC;
def SRARI_D : SRARI_D_ENC, SRARI_D_DESC;

def SRL_B : SRL_B_ENC, SRL_B_DESC;
def SRL_H : SRL_H_ENC, SRL_H_DESC;
def SRL_W : SRL_W_ENC, SRL_W_DESC;
def SRL_D : SRL_D_ENC, SRL_D_DESC;

def SRLI_B : SRLI_B_ENC, SRLI_B_DESC;
def SRLI_H : SRLI_H_ENC, SRLI_H_DESC;
def SRLI_W : SRLI_W_ENC, SRLI_W_DESC;
def SRLI_D : SRLI_D_ENC, SRLI_D_DESC;

def SRLR_B : SRLR_B_ENC, SRLR_B_DESC;
def SRLR_H : SRLR_H_ENC, SRLR_H_DESC;
def SRLR_W : SRLR_W_ENC, SRLR_W_DESC;
def SRLR_D : SRLR_D_ENC, SRLR_D_DESC;

def SRLRI_B : SRLRI_B_ENC, SRLRI_B_DESC;
def SRLRI_H : SRLRI_H_ENC, SRLRI_H_DESC;
def SRLRI_W : SRLRI_W_ENC, SRLRI_W_DESC;
def SRLRI_D : SRLRI_D_ENC, SRLRI_D_DESC;

def ST_B: ST_B_ENC, ST_B_DESC;
def ST_H: ST_H_ENC, ST_H_DESC;
def ST_W: ST_W_ENC, ST_W_DESC;
def ST_D: ST_D_ENC, ST_D_DESC;

def SUBS_S_B : SUBS_S_B_ENC, SUBS_S_B_DESC;
def SUBS_S_H : SUBS_S_H_ENC, SUBS_S_H_DESC;
def SUBS_S_W : SUBS_S_W_ENC, SUBS_S_W_DESC;
def SUBS_S_D : SUBS_S_D_ENC, SUBS_S_D_DESC;

def SUBS_U_B : SUBS_U_B_ENC, SUBS_U_B_DESC;
def SUBS_U_H : SUBS_U_H_ENC, SUBS_U_H_DESC;
def SUBS_U_W : SUBS_U_W_ENC, SUBS_U_W_DESC;
def SUBS_U_D : SUBS_U_D_ENC, SUBS_U_D_DESC;

def SUBSUS_U_B : SUBSUS_U_B_ENC, SUBSUS_U_B_DESC;
def SUBSUS_U_H : SUBSUS_U_H_ENC, SUBSUS_U_H_DESC;
def SUBSUS_U_W : SUBSUS_U_W_ENC, SUBSUS_U_W_DESC;
def SUBSUS_U_D : SUBSUS_U_D_ENC, SUBSUS_U_D_DESC;

def SUBSUU_S_B : SUBSUU_S_B_ENC, SUBSUU_S_B_DESC;
def SUBSUU_S_H : SUBSUU_S_H_ENC, SUBSUU_S_H_DESC;
def SUBSUU_S_W : SUBSUU_S_W_ENC, SUBSUU_S_W_DESC;
def SUBSUU_S_D : SUBSUU_S_D_ENC, SUBSUU_S_D_DESC;

def SUBV_B : SUBV_B_ENC, SUBV_B_DESC;
def SUBV_H : SUBV_H_ENC, SUBV_H_DESC;
def SUBV_W : SUBV_W_ENC, SUBV_W_DESC;
def SUBV_D : SUBV_D_ENC, SUBV_D_DESC;

def SUBVI_B : SUBVI_B_ENC, SUBVI_B_DESC;
def SUBVI_H : SUBVI_H_ENC, SUBVI_H_DESC;
def SUBVI_W : SUBVI_W_ENC, SUBVI_W_DESC;
def SUBVI_D : SUBVI_D_ENC, SUBVI_D_DESC;

def VSHF_B : VSHF_B_ENC, VSHF_B_DESC;
def VSHF_H : VSHF_H_ENC, VSHF_H_DESC;
def VSHF_W : VSHF_W_ENC, VSHF_W_DESC;
def VSHF_D : VSHF_D_ENC, VSHF_D_DESC;

def XOR_V : XOR_V_ENC, XOR_V_DESC;
def XOR_V_H_PSEUDO : XOR_V_H_PSEUDO_DESC,
                     PseudoInstExpansion<(XOR_V MSA128BOpnd:$wd,
                                                MSA128BOpnd:$ws,
                                                MSA128BOpnd:$wt)>;
def XOR_V_W_PSEUDO : XOR_V_W_PSEUDO_DESC,
                     PseudoInstExpansion<(XOR_V MSA128BOpnd:$wd,
                                                MSA128BOpnd:$ws,
                                                MSA128BOpnd:$wt)>;
def XOR_V_D_PSEUDO : XOR_V_D_PSEUDO_DESC,
                     PseudoInstExpansion<(XOR_V MSA128BOpnd:$wd,
                                                MSA128BOpnd:$ws,
                                                MSA128BOpnd:$wt)>;

def XORI_B : XORI_B_ENC, XORI_B_DESC;

// Patterns.
class MSAPat<dag pattern, dag result, list<Predicate> pred = [HasMSA]> :
  Pat<pattern, result>, Requires<pred>;

def : MSAPat<(extractelt (v4i32 MSA128W:$ws), immZExt4:$idx),
             (COPY_S_W MSA128W:$ws, immZExt4:$idx)>;

def : MSAPat<(v8f16 (load addrimm10lsl1:$addr)), (LD_H addrimm10lsl1:$addr)>;
def : MSAPat<(v4f32 (load addrimm10lsl2:$addr)), (LD_W addrimm10lsl2:$addr)>;
def : MSAPat<(v2f64 (load addrimm10lsl3:$addr)), (LD_D addrimm10lsl3:$addr)>;

def ST_FH : MSAPat<(store (v8f16 MSA128H:$ws), addrimm10lsl1:$addr),
                   (ST_H MSA128H:$ws, addrimm10lsl1:$addr)>;
def ST_FW : MSAPat<(store (v4f32 MSA128W:$ws), addrimm10lsl2:$addr),
                   (ST_W MSA128W:$ws, addrimm10lsl2:$addr)>;
def ST_FD : MSAPat<(store (v2f64 MSA128D:$ws), addrimm10lsl3:$addr),
                   (ST_D MSA128D:$ws, addrimm10lsl3:$addr)>;

class MSA_FABS_PSEUDO_DESC_BASE<RegisterOperand ROWD,
                                RegisterOperand ROWS = ROWD,
                                InstrItinClass itin = NoItinerary> :
  MSAPseudo<(outs ROWD:$wd),
            (ins ROWS:$ws),
            [(set ROWD:$wd, (fabs ROWS:$ws))]> {
  InstrItinClass Itinerary = itin;
}
def FABS_W : MSA_FABS_PSEUDO_DESC_BASE<MSA128WOpnd>,
             PseudoInstExpansion<(FMAX_A_W MSA128WOpnd:$wd, MSA128WOpnd:$ws,
                                           MSA128WOpnd:$ws)>;
def FABS_D : MSA_FABS_PSEUDO_DESC_BASE<MSA128DOpnd>,
             PseudoInstExpansion<(FMAX_A_D MSA128DOpnd:$wd, MSA128DOpnd:$ws,
                                           MSA128DOpnd:$ws)>;

class MSABitconvertPat<ValueType DstVT, ValueType SrcVT,
                       RegisterClass DstRC, list<Predicate> preds = [HasMSA]> :
   MSAPat<(DstVT (bitconvert SrcVT:$src)),
          (COPY_TO_REGCLASS SrcVT:$src, DstRC), preds>;

// These are endian-independent because the element size doesnt change
def : MSABitconvertPat<v8i16, v8f16, MSA128H>;
def : MSABitconvertPat<v4i32, v4f32, MSA128W>;
def : MSABitconvertPat<v2i64, v2f64, MSA128D>;
def : MSABitconvertPat<v8f16, v8i16, MSA128H>;
def : MSABitconvertPat<v4f32, v4i32, MSA128W>;
def : MSABitconvertPat<v2f64, v2i64, MSA128D>;

// Little endian bitcasts are always no-ops
def : MSABitconvertPat<v16i8, v8i16, MSA128B, [HasMSA, IsLE]>;
def : MSABitconvertPat<v16i8, v4i32, MSA128B, [HasMSA, IsLE]>;
def : MSABitconvertPat<v16i8, v2i64, MSA128B, [HasMSA, IsLE]>;
def : MSABitconvertPat<v16i8, v8f16, MSA128B, [HasMSA, IsLE]>;
def : MSABitconvertPat<v16i8, v4f32, MSA128B, [HasMSA, IsLE]>;
def : MSABitconvertPat<v16i8, v2f64, MSA128B, [HasMSA, IsLE]>;

def : MSABitconvertPat<v8i16, v16i8, MSA128H, [HasMSA, IsLE]>;
def : MSABitconvertPat<v8i16, v4i32, MSA128H, [HasMSA, IsLE]>;
def : MSABitconvertPat<v8i16, v2i64, MSA128H, [HasMSA, IsLE]>;
def : MSABitconvertPat<v8i16, v4f32, MSA128H, [HasMSA, IsLE]>;
def : MSABitconvertPat<v8i16, v2f64, MSA128H, [HasMSA, IsLE]>;

def : MSABitconvertPat<v4i32, v16i8, MSA128W, [HasMSA, IsLE]>;
def : MSABitconvertPat<v4i32, v8i16, MSA128W, [HasMSA, IsLE]>;
def : MSABitconvertPat<v4i32, v2i64, MSA128W, [HasMSA, IsLE]>;
def : MSABitconvertPat<v4i32, v8f16, MSA128W, [HasMSA, IsLE]>;
def : MSABitconvertPat<v4i32, v2f64, MSA128W, [HasMSA, IsLE]>;

def : MSABitconvertPat<v2i64, v16i8, MSA128D, [HasMSA, IsLE]>;
def : MSABitconvertPat<v2i64, v8i16, MSA128D, [HasMSA, IsLE]>;
def : MSABitconvertPat<v2i64, v4i32, MSA128D, [HasMSA, IsLE]>;
def : MSABitconvertPat<v2i64, v8f16, MSA128D, [HasMSA, IsLE]>;
def : MSABitconvertPat<v2i64, v4f32, MSA128D, [HasMSA, IsLE]>;

def : MSABitconvertPat<v4f32, v16i8, MSA128W, [HasMSA, IsLE]>;
def : MSABitconvertPat<v4f32, v8i16, MSA128W, [HasMSA, IsLE]>;
def : MSABitconvertPat<v4f32, v2i64, MSA128W, [HasMSA, IsLE]>;
def : MSABitconvertPat<v4f32, v8f16, MSA128W, [HasMSA, IsLE]>;
def : MSABitconvertPat<v4f32, v2f64, MSA128W, [HasMSA, IsLE]>;

def : MSABitconvertPat<v2f64, v16i8, MSA128D, [HasMSA, IsLE]>;
def : MSABitconvertPat<v2f64, v8i16, MSA128D, [HasMSA, IsLE]>;
def : MSABitconvertPat<v2f64, v4i32, MSA128D, [HasMSA, IsLE]>;
def : MSABitconvertPat<v2f64, v8f16, MSA128D, [HasMSA, IsLE]>;
def : MSABitconvertPat<v2f64, v4f32, MSA128D, [HasMSA, IsLE]>;

// Big endian bitcasts expand to shuffle instructions.
// This is because bitcast is defined to be a store/load sequence and the
// vector store/load instructions are mixed-endian with respect to the vector
// as a whole (little endian with respect to element order, but big endian
// elements).

class MSABitconvertReverseQuartersPat<ValueType DstVT, ValueType SrcVT,
                                      RegisterClass DstRC, MSAInst Insn,
                                      RegisterClass ViaRC> :
  MSAPat<(DstVT (bitconvert SrcVT:$src)),
         (COPY_TO_REGCLASS (Insn (COPY_TO_REGCLASS SrcVT:$src, ViaRC), 27),
                           DstRC),
         [HasMSA, IsBE]>;

class MSABitconvertReverseHalvesPat<ValueType DstVT, ValueType SrcVT,
                                    RegisterClass DstRC, MSAInst Insn,
                                    RegisterClass ViaRC> :
  MSAPat<(DstVT (bitconvert SrcVT:$src)),
         (COPY_TO_REGCLASS (Insn (COPY_TO_REGCLASS SrcVT:$src, ViaRC), 177),
                           DstRC),
         [HasMSA, IsBE]>;

class MSABitconvertReverseBInHPat<ValueType DstVT, ValueType SrcVT,
                                  RegisterClass DstRC> :
  MSABitconvertReverseHalvesPat<DstVT, SrcVT, DstRC, SHF_B, MSA128B>;

class MSABitconvertReverseBInWPat<ValueType DstVT, ValueType SrcVT,
                                  RegisterClass DstRC> :
  MSABitconvertReverseQuartersPat<DstVT, SrcVT, DstRC, SHF_B, MSA128B>;

class MSABitconvertReverseBInDPat<ValueType DstVT, ValueType SrcVT,
                                  RegisterClass DstRC> :
  MSAPat<(DstVT (bitconvert SrcVT:$src)),
         (COPY_TO_REGCLASS
           (SHF_W
             (COPY_TO_REGCLASS
               (SHF_B (COPY_TO_REGCLASS SrcVT:$src, MSA128B), 27),
               MSA128W), 177),
           DstRC),
         [HasMSA, IsBE]>;

class MSABitconvertReverseHInWPat<ValueType DstVT, ValueType SrcVT,
                                  RegisterClass DstRC> :
  MSABitconvertReverseHalvesPat<DstVT, SrcVT, DstRC, SHF_H, MSA128H>;

class MSABitconvertReverseHInDPat<ValueType DstVT, ValueType SrcVT,
                                  RegisterClass DstRC> :
  MSABitconvertReverseQuartersPat<DstVT, SrcVT, DstRC, SHF_H, MSA128H>;

class MSABitconvertReverseWInDPat<ValueType DstVT, ValueType SrcVT,
                                  RegisterClass DstRC> :
  MSABitconvertReverseHalvesPat<DstVT, SrcVT, DstRC, SHF_W, MSA128W>;

def : MSABitconvertReverseBInHPat<v8i16, v16i8, MSA128H>;
def : MSABitconvertReverseBInHPat<v8f16, v16i8, MSA128H>;
def : MSABitconvertReverseBInWPat<v4i32, v16i8, MSA128W>;
def : MSABitconvertReverseBInWPat<v4f32, v16i8, MSA128W>;
def : MSABitconvertReverseBInDPat<v2i64, v16i8, MSA128D>;
def : MSABitconvertReverseBInDPat<v2f64, v16i8, MSA128D>;

def : MSABitconvertReverseBInHPat<v16i8, v8i16, MSA128B>;
def : MSABitconvertReverseHInWPat<v4i32, v8i16, MSA128W>;
def : MSABitconvertReverseHInWPat<v4f32, v8i16, MSA128W>;
def : MSABitconvertReverseHInDPat<v2i64, v8i16, MSA128D>;
def : MSABitconvertReverseHInDPat<v2f64, v8i16, MSA128D>;

def : MSABitconvertReverseBInHPat<v16i8, v8f16, MSA128B>;
def : MSABitconvertReverseHInWPat<v4i32, v8f16, MSA128W>;
def : MSABitconvertReverseHInWPat<v4f32, v8f16, MSA128W>;
def : MSABitconvertReverseHInDPat<v2i64, v8f16, MSA128D>;
def : MSABitconvertReverseHInDPat<v2f64, v8f16, MSA128D>;

def : MSABitconvertReverseBInWPat<v16i8, v4i32, MSA128B>;
def : MSABitconvertReverseHInWPat<v8i16, v4i32, MSA128H>;
def : MSABitconvertReverseHInWPat<v8f16, v4i32, MSA128H>;
def : MSABitconvertReverseWInDPat<v2i64, v4i32, MSA128D>;
def : MSABitconvertReverseWInDPat<v2f64, v4i32, MSA128D>;

def : MSABitconvertReverseBInWPat<v16i8, v4f32, MSA128B>;
def : MSABitconvertReverseHInWPat<v8i16, v4f32, MSA128H>;
def : MSABitconvertReverseHInWPat<v8f16, v4f32, MSA128H>;
def : MSABitconvertReverseWInDPat<v2i64, v4f32, MSA128D>;
def : MSABitconvertReverseWInDPat<v2f64, v4f32, MSA128D>;

def : MSABitconvertReverseBInDPat<v16i8, v2i64, MSA128B>;
def : MSABitconvertReverseHInDPat<v8i16, v2i64, MSA128H>;
def : MSABitconvertReverseHInDPat<v8f16, v2i64, MSA128H>;
def : MSABitconvertReverseWInDPat<v4i32, v2i64, MSA128W>;
def : MSABitconvertReverseWInDPat<v4f32, v2i64, MSA128W>;

def : MSABitconvertReverseBInDPat<v16i8, v2f64, MSA128B>;
def : MSABitconvertReverseHInDPat<v8i16, v2f64, MSA128H>;
def : MSABitconvertReverseHInDPat<v8f16, v2f64, MSA128H>;
def : MSABitconvertReverseWInDPat<v4i32, v2f64, MSA128W>;
def : MSABitconvertReverseWInDPat<v4f32, v2f64, MSA128W>;

// Pseudos used to implement BNZ.df, and BZ.df

class MSA_CBRANCH_PSEUDO_DESC_BASE<SDPatternOperator OpNode, ValueType TyNode,
                                   RegisterClass RCWS> :
  MipsPseudo<(outs GPR32:$dst),
             (ins RCWS:$ws),
             [(set GPR32:$dst, (OpNode (TyNode RCWS:$ws)))]> {
  bit usesCustomInserter = 1;
  bit hasNoSchedulingInfo = 1;
}

def SNZ_B_PSEUDO
    : MSA_CBRANCH_PSEUDO_DESC_BASE<MipsVAllNonZero, v16i8, MSA128B>;
def SNZ_H_PSEUDO
    : MSA_CBRANCH_PSEUDO_DESC_BASE<MipsVAllNonZero, v8i16, MSA128H>;
def SNZ_W_PSEUDO
    : MSA_CBRANCH_PSEUDO_DESC_BASE<MipsVAllNonZero, v4i32, MSA128W>;
def SNZ_D_PSEUDO
    : MSA_CBRANCH_PSEUDO_DESC_BASE<MipsVAllNonZero, v2i64, MSA128D>;
def SNZ_V_PSEUDO
    : MSA_CBRANCH_PSEUDO_DESC_BASE<MipsVAnyNonZero, v16i8, MSA128B>;

def SZ_B_PSEUDO : MSA_CBRANCH_PSEUDO_DESC_BASE<MipsVAllZero, v16i8, MSA128B>;
def SZ_H_PSEUDO : MSA_CBRANCH_PSEUDO_DESC_BASE<MipsVAllZero, v8i16, MSA128H>;
def SZ_W_PSEUDO : MSA_CBRANCH_PSEUDO_DESC_BASE<MipsVAllZero, v4i32, MSA128W>;
def SZ_D_PSEUDO : MSA_CBRANCH_PSEUDO_DESC_BASE<MipsVAllZero, v2i64, MSA128D>;
def SZ_V_PSEUDO : MSA_CBRANCH_PSEUDO_DESC_BASE<MipsVAnyZero, v16i8, MSA128B>;

// Pseudoes used to implement transparent fp16 support.

let ASEPredicate = [HasMSA] in {
  let usesCustomInserter = 1 in {
    def ST_F16 :
        MipsPseudo<(outs), (ins MSA128F16:$ws, mem_simm10:$addr),
                   [(store (f16 MSA128F16:$ws), (addrimm10:$addr))]>;
    def LD_F16 :
        MipsPseudo<(outs MSA128F16:$ws), (ins mem_simm10:$addr),
                   [(set MSA128F16:$ws, (f16 (load addrimm10:$addr)))]>;
  }

  let usesCustomInserter = 1, hasNoSchedulingInfo = 1 in {
    def MSA_FP_EXTEND_W_PSEUDO :
        MipsPseudo<(outs FGR32Opnd:$fd), (ins MSA128F16:$ws),
                   [(set FGR32Opnd:$fd, (f32 (fpextend MSA128F16:$ws)))]>;
    def MSA_FP_ROUND_W_PSEUDO :
        MipsPseudo<(outs MSA128F16:$wd), (ins FGR32Opnd:$fs),
                   [(set MSA128F16:$wd, (f16 (fpround FGR32Opnd:$fs)))]>;
    def MSA_FP_EXTEND_D_PSEUDO :
        MipsPseudo<(outs FGR64Opnd:$fd), (ins MSA128F16:$ws),
                   [(set FGR64Opnd:$fd, (f64 (fpextend MSA128F16:$ws)))]>;
    def MSA_FP_ROUND_D_PSEUDO :
        MipsPseudo<(outs MSA128F16:$wd), (ins FGR64Opnd:$fs),
                   [(set MSA128F16:$wd, (f16 (fpround FGR64Opnd:$fs)))]>;
  }

  def : MipsPat<(MipsTruncIntFP MSA128F16:$ws),
                (TRUNC_W_D64 (MSA_FP_EXTEND_D_PSEUDO MSA128F16:$ws))>,
        ISA_MIPS1, ASE_MSA;

  def : MipsPat<(MipsFPCmp MSA128F16:$ws, MSA128F16:$wt, imm:$cond),
                (FCMP_S32 (MSA_FP_EXTEND_W_PSEUDO MSA128F16:$ws),
                          (MSA_FP_EXTEND_W_PSEUDO MSA128F16:$wt), imm:$cond)>,
        ISA_MIPS1_NOT_32R6_64R6, ASE_MSA;
}

def vsplati64_imm_eq_63 : PatLeaf<(bitconvert (v4i32 (build_vector))), [{
  APInt Imm;
  SDNode *BV = N->getOperand(0).getNode();
  EVT EltTy = N->getValueType(0).getVectorElementType();

  return selectVSplat(BV, Imm, EltTy.getSizeInBits()) &&
         Imm.getBitWidth() == EltTy.getSizeInBits() && Imm == 63;
}]>;

def immi32Cst7  : ImmLeaf<i32, [{return isUInt<32>(Imm) && Imm == 7;}]>;
def immi32Cst15 : ImmLeaf<i32, [{return isUInt<32>(Imm) && Imm == 15;}]>;
def immi32Cst31 : ImmLeaf<i32, [{return isUInt<32>(Imm) && Imm == 31;}]>;

def vsplati8imm7 :   PatFrag<(ops node:$wt),
                             (and node:$wt, (vsplati8 immi32Cst7))>;
def vsplati16imm15 : PatFrag<(ops node:$wt),
                             (and node:$wt, (vsplati16 immi32Cst15))>;
def vsplati32imm31 : PatFrag<(ops node:$wt),
                             (and node:$wt, (vsplati32 immi32Cst31))>;
def vsplati64imm63 : PatFrag<(ops node:$wt),
                             (and node:$wt, vsplati64_imm_eq_63)>;

class MSAShiftPat<SDNode Node, ValueType VT, MSAInst Insn, dag Vec> :
  MSAPat<(VT (Node VT:$ws, (VT (and VT:$wt, Vec)))),
         (VT (Insn VT:$ws, VT:$wt))>;

class MSABitPat<SDNode Node, ValueType VT, MSAInst Insn, PatFrag Frag> :
  MSAPat<(VT (Node VT:$ws, (shl (vsplat_imm_eq_1), (Frag VT:$wt)))),
         (VT (Insn VT:$ws, VT:$wt))>;

multiclass MSAShiftPats<SDNode Node, string Insn> {
  def : MSAShiftPat<Node, v16i8, !cast<MSAInst>(Insn#_B),
                    (vsplati8 immi32Cst7)>;
  def : MSAShiftPat<Node, v8i16, !cast<MSAInst>(Insn#_H),
                    (vsplati16 immi32Cst15)>;
  def : MSAShiftPat<Node, v4i32, !cast<MSAInst>(Insn#_W),
                    (vsplati32 immi32Cst31)>;
  def : MSAPat<(v2i64 (Node v2i64:$ws, (v2i64 (and v2i64:$wt,
                                                   vsplati64_imm_eq_63)))),
               (v2i64 (!cast<MSAInst>(Insn#_D) v2i64:$ws, v2i64:$wt))>;
}

multiclass MSABitPats<SDNode Node, string Insn> {
  def : MSABitPat<Node, v16i8, !cast<MSAInst>(Insn#_B), vsplati8imm7>;
  def : MSABitPat<Node, v8i16, !cast<MSAInst>(Insn#_H), vsplati16imm15>;
  def : MSABitPat<Node, v4i32, !cast<MSAInst>(Insn#_W), vsplati32imm31>;
  def : MSAPat<(Node v2i64:$ws, (shl (v2i64 (vsplat_imm_eq_1)),
                                     (vsplati64imm63 v2i64:$wt))),
               (v2i64 (!cast<MSAInst>(Insn#_D) v2i64:$ws, v2i64:$wt))>;
}

defm : MSAShiftPats<shl, "SLL">;
defm : MSAShiftPats<srl, "SRL">;
defm : MSAShiftPats<sra, "SRA">;
defm : MSABitPats<xor, "BNEG">;
defm : MSABitPats<or, "BSET">;

def : MSAPat<(and v16i8:$ws, (vnot (shl (vsplat_imm_eq_1),
                                        (vsplati8imm7 v16i8:$wt)))),
             (v16i8 (BCLR_B v16i8:$ws, v16i8:$wt))>;
def : MSAPat<(and v8i16:$ws, (vnot (shl (vsplat_imm_eq_1),
                                        (vsplati16imm15 v8i16:$wt)))),
             (v8i16 (BCLR_H v8i16:$ws, v8i16:$wt))>;
def : MSAPat<(and v4i32:$ws, (vnot (shl (vsplat_imm_eq_1),
                                        (vsplati32imm31 v4i32:$wt)))),
             (v4i32 (BCLR_W v4i32:$ws, v4i32:$wt))>;
def : MSAPat<(and v2i64:$ws, (vnot (shl (v2i64 (vsplat_imm_eq_1)),
                                        (vsplati64imm63 v2i64:$wt)))),
             (v2i64 (BCLR_D v2i64:$ws, v2i64:$wt))>;

// Vector extraction with fixed index.
//
// Extracting 32-bit values on MSA32 should always use COPY_S_W rather than
// COPY_U_W, even for the zero-extended case. This is because our forward
// compatibility strategy is to consider registers to be infinitely
// sign-extended so that a MIPS64 can execute MIPS32 code without getting
// different register values.
def : MSAPat<(vextract_zext_i32 (v4i32 MSA128W:$ws), immZExt2Ptr:$idx),
             (COPY_S_W MSA128W:$ws, immZExt2:$idx)>, ASE_MSA_NOT_MSA64;
def : MSAPat<(vextract_zext_i32 (v4f32 MSA128W:$ws), immZExt2Ptr:$idx),
             (COPY_S_W MSA128W:$ws, immZExt2:$idx)>, ASE_MSA_NOT_MSA64;

// Extracting 64-bit values on MSA64 should always use COPY_S_D rather than
// COPY_U_D, even for the zero-extended case. This is because our forward
// compatibility strategy is to consider registers to be infinitely
// sign-extended so that a hypothetical MIPS128 would be able to execute MIPS64
// code without getting different register values.
def : MSAPat<(vextract_zext_i64 (v2i64 MSA128D:$ws), immZExt1Ptr:$idx),
             (COPY_S_D MSA128D:$ws, immZExt1:$idx)>, ASE_MSA64;
def : MSAPat<(vextract_zext_i64 (v2f64 MSA128D:$ws), immZExt1Ptr:$idx),
             (COPY_S_D MSA128D:$ws, immZExt1:$idx)>, ASE_MSA64;

// Vector extraction with variable index
def : MSAPat<(i32 (vextract_sext_i8 v16i8:$ws, i32:$idx)),
             (SRA (COPY_TO_REGCLASS (i32 (EXTRACT_SUBREG (SPLAT_B v16i8:$ws,
                                                                  i32:$idx),
                                                         sub_lo)),
                                    GPR32), (i32 24))>;
def : MSAPat<(i32 (vextract_sext_i16 v8i16:$ws, i32:$idx)),
             (SRA (COPY_TO_REGCLASS (i32 (EXTRACT_SUBREG (SPLAT_H v8i16:$ws,
                                                                  i32:$idx),
                                                         sub_lo)),
                                    GPR32), (i32 16))>;
def : MSAPat<(i32 (vextract_sext_i32 v4i32:$ws, i32:$idx)),
             (COPY_TO_REGCLASS (i32 (EXTRACT_SUBREG (SPLAT_W v4i32:$ws,
                                                             i32:$idx),
                                                    sub_lo)),
                               GPR32)>;
def : MSAPat<(i64 (vextract_sext_i64 v2i64:$ws, i32:$idx)),
             (COPY_TO_REGCLASS (i64 (EXTRACT_SUBREG (SPLAT_D v2i64:$ws,
                                                             i32:$idx),
                                                    sub_64)),
                               GPR64), [HasMSA, IsGP64bit]>;

def : MSAPat<(i32 (vextract_zext_i8 v16i8:$ws, i32:$idx)),
             (SRL (COPY_TO_REGCLASS (i32 (EXTRACT_SUBREG (SPLAT_B v16i8:$ws,
                                                                  i32:$idx),
                                                         sub_lo)),
                                    GPR32), (i32 24))>;
def : MSAPat<(i32 (vextract_zext_i16 v8i16:$ws, i32:$idx)),
             (SRL (COPY_TO_REGCLASS (i32 (EXTRACT_SUBREG (SPLAT_H v8i16:$ws,
                                                                  i32:$idx),
                                                         sub_lo)),
                                    GPR32), (i32 16))>;
def : MSAPat<(i32 (vextract_zext_i32 v4i32:$ws, i32:$idx)),
             (COPY_TO_REGCLASS (i32 (EXTRACT_SUBREG (SPLAT_W v4i32:$ws,
                                                             i32:$idx),
                                                    sub_lo)),
                               GPR32)>;
def : MSAPat<(i64 (vextract_zext_i64 v2i64:$ws, i32:$idx)),
             (COPY_TO_REGCLASS (i64 (EXTRACT_SUBREG (SPLAT_D v2i64:$ws,
                                                             i32:$idx),
                                                    sub_64)),
                               GPR64), [HasMSA, IsGP64bit]>;

def : MSAPat<(f32 (vector_extract v4f32:$ws, i32:$idx)),
             (f32 (EXTRACT_SUBREG (SPLAT_W v4f32:$ws,
                                           i32:$idx),
                                  sub_lo))>;
def : MSAPat<(f64 (vector_extract v2f64:$ws, i32:$idx)),
             (f64 (EXTRACT_SUBREG (SPLAT_D v2f64:$ws,
                                           i32:$idx),
                                  sub_64))>;

// Vector extraction with variable index (N64 ABI)
def : MSAPat<
  (i32 (vextract_sext_i8 v16i8:$ws, i64:$idx)),
  (SRA (COPY_TO_REGCLASS
         (i32 (EXTRACT_SUBREG
                (SPLAT_B v16i8:$ws,
                  (COPY_TO_REGCLASS
                    (i32 (EXTRACT_SUBREG i64:$idx, sub_32)), GPR32)),
                sub_lo)),
         GPR32),
       (i32 24))>;
def : MSAPat<
  (i32 (vextract_sext_i16 v8i16:$ws, i64:$idx)),
  (SRA (COPY_TO_REGCLASS
         (i32 (EXTRACT_SUBREG
                (SPLAT_H v8i16:$ws,
                  (COPY_TO_REGCLASS
                    (i32 (EXTRACT_SUBREG i64:$idx, sub_32)), GPR32)),
                sub_lo)),
         GPR32),
       (i32 16))>;
def : MSAPat<
  (i32 (vextract_sext_i32 v4i32:$ws, i64:$idx)),
  (COPY_TO_REGCLASS
    (i32 (EXTRACT_SUBREG
           (SPLAT_W v4i32:$ws,
             (COPY_TO_REGCLASS
               (i32 (EXTRACT_SUBREG i64:$idx, sub_32)), GPR32)),
           sub_lo)),
    GPR32)>;
def : MSAPat<
  (i64 (vextract_sext_i64 v2i64:$ws, i64:$idx)),
  (COPY_TO_REGCLASS
    (i64 (EXTRACT_SUBREG
           (SPLAT_D v2i64:$ws,
             (COPY_TO_REGCLASS (i32 (EXTRACT_SUBREG i64:$idx, sub_32)), GPR32)),
           sub_64)),
    GPR64), [HasMSA, IsGP64bit]>;

def : MSAPat<
  (i32 (vextract_zext_i8 v16i8:$ws, i64:$idx)),
  (SRL (COPY_TO_REGCLASS
         (i32 (EXTRACT_SUBREG
                 (SPLAT_B v16i8:$ws,
                   (COPY_TO_REGCLASS
                     (i32 (EXTRACT_SUBREG i64:$idx, sub_32)), GPR32)),
                 sub_lo)),
         GPR32),
       (i32 24))>;
def : MSAPat<
  (i32 (vextract_zext_i16 v8i16:$ws, i64:$idx)),
  (SRL (COPY_TO_REGCLASS
         (i32 (EXTRACT_SUBREG
                (SPLAT_H v8i16:$ws,
                  (COPY_TO_REGCLASS
                    (i32 (EXTRACT_SUBREG i64:$idx, sub_32)), GPR32)),
                sub_lo)),
         GPR32),
       (i32 16))>;
def : MSAPat<
  (i32 (vextract_zext_i32 v4i32:$ws, i64:$idx)),
  (COPY_TO_REGCLASS
    (i32 (EXTRACT_SUBREG
           (SPLAT_W v4i32:$ws,
             (COPY_TO_REGCLASS (i32 (EXTRACT_SUBREG i64:$idx, sub_32)), GPR32)),
           sub_lo)),
    GPR32)>;
def : MSAPat<
  (i64 (vextract_zext_i64 v2i64:$ws, i64:$idx)),
  (COPY_TO_REGCLASS
    (i64 (EXTRACT_SUBREG
           (SPLAT_D v2i64:$ws,
             (COPY_TO_REGCLASS (i32 (EXTRACT_SUBREG i64:$idx, sub_32)), GPR32)),
           sub_64)),
    GPR64),
  [HasMSA, IsGP64bit]>;

def : MSAPat<
  (f32 (vector_extract v4f32:$ws, i64:$idx)),
  (f32 (EXTRACT_SUBREG
         (SPLAT_W v4f32:$ws,
           (COPY_TO_REGCLASS (i32 (EXTRACT_SUBREG i64:$idx, sub_32)), GPR32)),
         sub_lo))>;
def : MSAPat<
  (f64 (vector_extract v2f64:$ws, i64:$idx)),
  (f64 (EXTRACT_SUBREG
         (SPLAT_D v2f64:$ws,
           (COPY_TO_REGCLASS (i32 (EXTRACT_SUBREG i64:$idx, sub_32)), GPR32)),
         sub_64))>;

def : MSAPat<(vfseteq_v4f32 MSA128WOpnd:$a, MSA128WOpnd:$b),
             (FCEQ_W MSA128WOpnd:$a, MSA128WOpnd:$b)>;
def : MSAPat<(vfseteq_v2f64 MSA128DOpnd:$a, MSA128DOpnd:$b),
             (FCEQ_D MSA128DOpnd:$a, MSA128DOpnd:$b)>;
def : MSAPat<(vfsetle_v4f32 MSA128WOpnd:$a, MSA128WOpnd:$b),
             (FCLE_W MSA128WOpnd:$a, MSA128WOpnd:$b)>;
def : MSAPat<(vfsetle_v2f64 MSA128DOpnd:$a, MSA128DOpnd:$b),
             (FCLE_D MSA128DOpnd:$a, MSA128DOpnd:$b)>;
def : MSAPat<(vfsetlt_v4f32 MSA128WOpnd:$a, MSA128WOpnd:$b),
             (FCLT_W MSA128WOpnd:$a, MSA128WOpnd:$b)>;
def : MSAPat<(vfsetlt_v2f64 MSA128DOpnd:$a, MSA128DOpnd:$b),
             (FCLT_D MSA128DOpnd:$a, MSA128DOpnd:$b)>;
def : MSAPat<(vfsetne_v4f32 MSA128WOpnd:$a, MSA128WOpnd:$b),
             (FCNE_W MSA128WOpnd:$a, MSA128WOpnd:$b)>;
def : MSAPat<(vfsetne_v2f64 MSA128DOpnd:$a, MSA128DOpnd:$b),
             (FCNE_D MSA128DOpnd:$a, MSA128DOpnd:$b)>;
