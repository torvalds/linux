//=-- SMEInstrFormats.td -  AArch64 SME Instruction classes -*- tablegen -*--=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// AArch64 Scalable Matrix Extension (SME) Instruction Class Definitions.
//
//===----------------------------------------------------------------------===//

def imm_to_tile8   : ComplexPattern<i32, 1, "ImmToReg<AArch64::ZAB0, 0>",  []>;
def imm_to_tile16  : ComplexPattern<i32, 1, "ImmToReg<AArch64::ZAH0, 1>",  []>;
def imm_to_tile32  : ComplexPattern<i32, 1, "ImmToReg<AArch64::ZAS0, 3>",  []>;
def imm_to_tile64  : ComplexPattern<i32, 1, "ImmToReg<AArch64::ZAD0, 7>",  []>;
def imm_to_tile128 : ComplexPattern<i32, 1, "ImmToReg<AArch64::ZAQ0, 15>", []>;
def imm_to_zt      : ComplexPattern<i32, 1, "ImmToReg<AArch64::ZT0,  0>",  []>;

def tileslice8   : ComplexPattern<i32 , 2, "SelectSMETileSlice<15, 1>", []>;
def tileslice16  : ComplexPattern<i32 , 2, "SelectSMETileSlice<7,  1>", []>;
def tileslice32  : ComplexPattern<i32 , 2, "SelectSMETileSlice<3,  1>", []>;
def tileslice64  : ComplexPattern<i32 , 2, "SelectSMETileSlice<1,  1>", []>;
def tileslice128 : ComplexPattern<i32 , 2, "SelectSMETileSlice<0,  1>", []>; // nop

def tileslicerange3s2 : ComplexPattern<i32, 2, "SelectSMETileSlice<14, 2>", []>;
def tileslicerange2s2 : ComplexPattern<i32, 2, "SelectSMETileSlice<6,  2>", []>;
def tileslicerange1s2 : ComplexPattern<i32, 2, "SelectSMETileSlice<2,  2>", []>;
def tileslicerange0s2 : ComplexPattern<i32, 2, "SelectSMETileSlice<0,  2>", []>;

def tileslicerange2s4 : ComplexPattern<i32, 2, "SelectSMETileSlice<12, 4>", []>;
def tileslicerange1s4 : ComplexPattern<i32, 2, "SelectSMETileSlice<4,  4>", []>;
def tileslicerange0s4 : ComplexPattern<i32, 2, "SelectSMETileSlice<0,  4>", []>;

def am_sme_indexed_b4 :ComplexPattern<iPTR, 2, "SelectAddrModeIndexedSVE<0,15>", [], [SDNPWantRoot]>;

def SDTZALoadStore : SDTypeProfile<0, 3, [SDTCisInt<0>, SDTCisPtrTy<1>, SDTCisInt<2>]>;
def AArch64SMELdr : SDNode<"AArch64ISD::SME_ZA_LDR", SDTZALoadStore,
                             [SDNPHasChain, SDNPSideEffect, SDNPMayLoad]>;
def AArch64SMEStr : SDNode<"AArch64ISD::SME_ZA_STR", SDTZALoadStore,
                             [SDNPHasChain, SDNPSideEffect, SDNPMayStore]>;

//===----------------------------------------------------------------------===//
// SME Pseudo Classes
//===----------------------------------------------------------------------===//

def getSMEPseudoMap : InstrMapping {
  let FilterClass = "SMEPseudo2Instr";
  let RowFields = ["PseudoName"];
  let ColFields = ["IsInstr"];
  let KeyCol = ["0"];
  let ValueCols = [["1"]];
}

class SMEPseudo2Instr<string name, bit instr> {
  string PseudoName = name;
  bit IsInstr = instr;
}

class sme_outer_product_pseudo<ZPRRegOp zpr_ty, SMEMatrixTypeEnum za_flag>
    : Pseudo<(outs), (ins i32imm:$tile, PPR3bAny:$pn, PPR3bAny:$pm,
                          zpr_ty:$zn, zpr_ty:$zm), []>,
      Sched<[]> {
  // Translated to the actual instructions in AArch64ISelLowering.cpp
  let SMEMatrixType = za_flag;
  let usesCustomInserter = 1;
}

class sme2_za_array_2op_multi_single_pseudo<string name, Operand index_ty, RegisterOperand multi_vector_ty,
                                            ZPRRegOp zpr_ty, SMEMatrixTypeEnum za_flag>
    : SMEPseudo2Instr<name, 0>,
      Pseudo<(outs), (ins MatrixIndexGPR32Op8_11:$Rv, index_ty:$imm3, multi_vector_ty:$Zn, zpr_ty:$Zm), []> {
  let SMEMatrixType = za_flag;
  let usesCustomInserter = 1;
}

class sme2_za_array_2op_multi_multi_pseudo<string name, Operand index_ty, RegisterOperand multi_vector_ty,
                                           SMEMatrixTypeEnum za_flag>
    : SMEPseudo2Instr<name, 0>,
      Pseudo<(outs), (ins MatrixIndexGPR32Op8_11:$Rv, index_ty:$imm3, multi_vector_ty:$Zn, multi_vector_ty:$Zm), []> {
  let SMEMatrixType = za_flag;
  let usesCustomInserter = 1;
}

class sme2_za_array_2op_multi_index_pseudo<string name, Operand index_ty, RegisterOperand multi_vector_ty,
                                           ZPRRegOp zpr_ty, Operand imm_ty, SMEMatrixTypeEnum za_flag>
    : SMEPseudo2Instr<name, 0>,
      Pseudo<(outs), (ins MatrixIndexGPR32Op8_11:$Rv, index_ty:$imm3, multi_vector_ty:$Zn, zpr_ty:$Zm, imm_ty:$i), []> {
  let SMEMatrixType = za_flag;
  let usesCustomInserter = 1;
}

class sme2_move_to_za_pseudo<string name, Operand imm_ty, RegisterOperand multi_vector_ty, SMEMatrixTypeEnum za_flag>
    : SMEPseudo2Instr<name, 0>,
      Pseudo<(outs), (ins MatrixIndexGPR32Op8_11:$Rs, imm_ty:$imm, multi_vector_ty:$Zn), []> {
  let SMEMatrixType = za_flag;
  let usesCustomInserter = 1;
}

class sme2_move_to_tile_pseudo<string name, Operand tile_imm, Operand imm_ty, RegisterOperand multi_vector_ty, SMEMatrixTypeEnum za_flag>
    : SMEPseudo2Instr<name, 0>,
      Pseudo<(outs), (ins tile_imm:$tile, MatrixIndexGPR32Op12_15:$Rs, imm_ty:$imm, multi_vector_ty:$Zn), []> {
  let SMEMatrixType = za_flag;
  let usesCustomInserter = 1;
}

class sem2p1_zero_matrix_pseudo<string name, Operand index_ty, SMEMatrixTypeEnum za_flag>
    : SMEPseudo2Instr<name, 0>,
      Pseudo<(outs), (ins MatrixIndexGPR32Op8_11:$Rs, index_ty:$imm), []> {
  let SMEMatrixType = za_flag;
  let usesCustomInserter = 1;
}

class sme2_movez_to_tile_pseudo<string name, Operand tile_imm, Operand imm_ty, RegisterOperand vector_ty, SMEMatrixTypeEnum za_flag>
    : SMEPseudo2Instr<name, 0>,
      Pseudo<(outs vector_ty:$Zn), (ins tile_imm:$tile, MatrixIndexGPR32Op12_15:$Rs, imm_ty:$imm), []> {
  let SMEMatrixType = za_flag;
  let usesCustomInserter = 1;
}

class sme2_movaz_array_to_tile_pseudo<string name, Operand index_ty, RegisterOperand multi_vector_ty,
                                      SMEMatrixTypeEnum za_flag>
    : SMEPseudo2Instr<name, 0>,
      Pseudo<(outs multi_vector_ty:$Zd), (ins MatrixIndexGPR32Op8_11:$Rv, index_ty:$imm3), []> {
  let SMEMatrixType = za_flag;
  let usesCustomInserter = 1;
}

//===----------------------------------------------------------------------===//
// SME pattern match helpers.
//===----------------------------------------------------------------------===//

class SME2_ZA_TwoOp_Multi_Single_Pat<string name, SDPatternOperator intrinsic, Operand index_ty, ZPRRegOp zpr_ty,
                                     ValueType vt, ComplexPattern tileslice>
    : Pat<(intrinsic (i32 (tileslice MatrixIndexGPR32Op8_11:$base, index_ty:$offset)), vt:$Zn, vt:$Zm),
          (!cast<Instruction>(name # _PSEUDO) $base, $offset, vt:$Zn, zpr_ty:$Zm)>;


class SME2_ZA_TwoOp_VG2_Multi_Single_Pat<string name, SDPatternOperator intrinsic, Operand index_ty, ZPRRegOp zpr_ty,
                                         ValueType vt, ComplexPattern tileslice>
    : Pat<(intrinsic (i32 (tileslice MatrixIndexGPR32Op8_11:$base, index_ty:$offset)), vt:$Zn1, vt:$Zn2, vt:$Zm),
          (!cast<Instruction>(name # _PSEUDO) $base, $offset, (REG_SEQUENCE ZPR2, vt:$Zn1, zsub0, vt:$Zn2, zsub1),
                                              zpr_ty:$Zm)>;
class SME2_ZA_TwoOp_VG4_Multi_Single_Pat<string name, SDPatternOperator intrinsic, Operand index_ty, ZPRRegOp zpr_ty,
                                         ValueType vt, ComplexPattern tileslice>
    : Pat<(intrinsic (i32 (tileslice MatrixIndexGPR32Op8_11:$base, index_ty:$offset)),
                     vt:$Zn1, vt:$Zn2, vt:$Zn3, vt:$Zn4, vt:$Zm),
          (!cast<Instruction>(name # _PSEUDO) $base, $offset,
                                              (REG_SEQUENCE ZPR4, vt:$Zn1, zsub0, vt:$Zn2, zsub1, vt:$Zn3, zsub2, vt:$Zn4, zsub3),
                                              zpr_ty:$Zm)>;

class SME2_ZA_TwoOp_VG2_Multi_Multi_Pat<string name, SDPatternOperator intrinsic, Operand index_ty, ValueType vt, ComplexPattern tileslice>
    : Pat<(intrinsic (i32 (tileslice MatrixIndexGPR32Op8_11:$base, index_ty:$offset)), vt:$Zn1, vt:$Zn2, vt:$Zm1, vt:$Zm2),
          (!cast<Instruction>(name # _PSEUDO) $base, $offset,
                                              (REG_SEQUENCE ZPR2Mul2, vt:$Zn1, zsub0, vt:$Zn2, zsub1),
                                              (REG_SEQUENCE ZPR2Mul2, vt:$Zm1, zsub0, vt:$Zm2, zsub1))>;

class SME2_ZA_TwoOp_VG4_Multi_Multi_Pat<string name, SDPatternOperator intrinsic, Operand index_ty, ValueType vt, ComplexPattern tileslice>
    : Pat<(intrinsic (i32 (tileslice MatrixIndexGPR32Op8_11:$base, index_ty:$offset)),
                     vt:$Zn1, vt:$Zn2, vt:$Zn3, vt:$Zn4, vt:$Zm1, vt:$Zm2, vt:$Zm3, vt:$Zm4),
          (!cast<Instruction>(name # _PSEUDO) $base, $offset,
                                              (REG_SEQUENCE ZPR4Mul4, vt:$Zn1, zsub0, vt:$Zn2, zsub1, vt:$Zn3, zsub2, vt:$Zn4, zsub3),
                                              (REG_SEQUENCE ZPR4Mul4, vt:$Zm1, zsub0, vt:$Zm2, zsub1, vt:$Zm3, zsub2, vt:$Zm4, zsub3))>;

class SME2_ZA_TwoOp_Multi_Index_Pat<string name, SDPatternOperator intrinsic, Operand index_ty, ZPRRegOp zpr_ty, ValueType vt,
                                    Operand imm_ty, ComplexPattern tileslice>
   : Pat<(intrinsic (i32 (tileslice MatrixIndexGPR32Op8_11:$base, index_ty:$offset)), vt:$Zn, vt:$Zm, (i32 imm_ty:$i)),
         (!cast<Instruction>(name # _PSEUDO) $base, $offset, vt:$Zn, zpr_ty:$Zm, (i32 imm_ty:$i))>;


class SME2_ZA_TwoOp_VG2_Multi_Index_Pat<string name, SDPatternOperator intrinsic, Operand index_ty, ZPRRegOp zpr_ty, ValueType vt,
                                        Operand imm_ty, ComplexPattern tileslice>
    : Pat<(intrinsic (i32 (tileslice MatrixIndexGPR32Op8_11:$base, index_ty:$offset)), vt:$Zn1, vt:$Zn2, vt:$Zm, (i32 imm_ty:$i)),
          (!cast<Instruction>(name # _PSEUDO) $base, $offset,
                                              (REG_SEQUENCE ZPR2Mul2, vt:$Zn1, zsub0, vt:$Zn2, zsub1), zpr_ty:$Zm, imm_ty:$i)>;

class SME2_ZA_TwoOp_VG4_Multi_Index_Pat<string name, SDPatternOperator intrinsic, Operand index_ty, ZPRRegOp zpr_ty, ValueType vt,
                                        Operand imm_ty, ComplexPattern tileslice>
    : Pat<(intrinsic (i32 (tileslice MatrixIndexGPR32Op8_11:$base, index_ty:$offset)),
                     vt:$Zn1, vt:$Zn2, vt:$Zn3, vt:$Zn4, vt:$Zm, (i32 imm_ty:$i)),
          (!cast<Instruction>(name # _PSEUDO) $base, $offset,
                                              (REG_SEQUENCE ZPR4Mul4, vt:$Zn1, zsub0, vt:$Zn2, zsub1, vt:$Zn3, zsub2, vt:$Zn4, zsub3),
                                              zpr_ty:$Zm, imm_ty:$i)>;

class SME2_Sat_Shift_VG2_Pat<string name, SDPatternOperator intrinsic, ValueType out_vt, ValueType in_vt, Operand imm_ty>
    : Pat<(out_vt (intrinsic in_vt:$Zn1, in_vt:$Zn2, (i32 imm_ty:$i))),
                  (!cast<Instruction>(name) (REG_SEQUENCE ZPR2Mul2, in_vt:$Zn1, zsub0, in_vt:$Zn2, zsub1), imm_ty:$i)>;

class SME2_Sat_Shift_VG4_Pat<string name, SDPatternOperator intrinsic, ValueType out_vt, ValueType in_vt, Operand imm_ty>
    : Pat<(out_vt (intrinsic in_vt:$Zn1, in_vt:$Zn2, in_vt:$Zn3, in_vt:$Zn4, (i32 imm_ty:$i))),
                  (!cast<Instruction>(name) (REG_SEQUENCE ZPR4Mul4, in_vt:$Zn1, zsub0, in_vt:$Zn2, zsub1, in_vt:$Zn3, zsub2, in_vt:$Zn4, zsub3),
                                            imm_ty:$i)>;

class SME2_Cvt_VG4_Pat<string name, SDPatternOperator intrinsic, ValueType out_vt, ValueType in_vt>
    : Pat<(out_vt (intrinsic in_vt:$Zn1, in_vt:$Zn2, in_vt:$Zn3, in_vt:$Zn4)),
                  (!cast<Instruction>(name) (REG_SEQUENCE ZPR4Mul4, in_vt:$Zn1, zsub0, in_vt:$Zn2, zsub1, in_vt:$Zn3, zsub2, in_vt:$Zn4, zsub3))>;

class SME2_ZA_VG1x2_Multi_Pat<string name, SDPatternOperator intrinsic, ValueType vt, Operand index_ty, ComplexPattern tileslice>
    : Pat<(intrinsic (i32 (tileslice MatrixIndexGPR32Op8_11:$base, index_ty:$offset)), vt:$Zn1, vt:$Zn2),
          (!cast<Instruction>(name # _PSEUDO) $base, $offset, (REG_SEQUENCE ZPR2Mul2, vt:$Zn1, zsub0, vt:$Zn2, zsub1))>;

class SME2_ZA_VG1x4_Multi_Pat<string name, SDPatternOperator intrinsic, ValueType vt, Operand index_ty, ComplexPattern tileslice>
    : Pat<(intrinsic (i32 (tileslice MatrixIndexGPR32Op8_11:$base, index_ty:$offset)), vt:$Zn1, vt:$Zn2, vt:$Zn3, vt:$Zn4),
          (!cast<Instruction>(name # _PSEUDO) $base, $offset, (REG_SEQUENCE ZPR4Mul4, vt:$Zn1, zsub0, vt:$Zn2, zsub1, vt:$Zn3, zsub2, vt:$Zn4, zsub3))>;

class SME2_Tile_VG2_Multi_Pat<string name, SDPatternOperator intrinsic, Operand tile_imm, ValueType vt, Operand index_ty, ComplexPattern tileslice>
    : Pat<(intrinsic tile_imm:$tile, (i32 (tileslice MatrixIndexGPR32Op12_15:$base, index_ty:$offset)), vt:$Zn1, vt:$Zn2),
          (!cast<Instruction>(name # _PSEUDO) $tile, $base, $offset, (REG_SEQUENCE ZPR2Mul2, vt:$Zn1, zsub0, vt:$Zn2, zsub1))>;

class SME2_Tile_VG4_Multi_Pat<string name, SDPatternOperator intrinsic, Operand tile_imm, ValueType vt, Operand index_ty, ComplexPattern tileslice>
    : Pat<(intrinsic tile_imm:$tile, (i32 (tileslice MatrixIndexGPR32Op12_15:$base, index_ty:$offset)), vt:$Zn1, vt:$Zn2, vt:$Zn3, vt:$Zn4),
          (!cast<Instruction>(name # _PSEUDO) $tile, $base, $offset, (REG_SEQUENCE ZPR4Mul4, vt:$Zn1, zsub0, vt:$Zn2, zsub1, vt:$Zn3, zsub2, vt:$Zn4, zsub3))>;

class SME2_Zero_Matrix_Pat<string name, SDPatternOperator intrinsic, Operand offset_ty, ComplexPattern tileslice>
    : Pat<(intrinsic (i32 (tileslice MatrixIndexGPR32Op8_11:$base, offset_ty:$offset))),
    (!cast<Instruction>(name) $base, $offset)>; 

class SME2_Tile_Movaz_Pat<string name, SDPatternOperator intrinsic, ValueType out_vt, Operand tile_imm, Operand index_ty, ComplexPattern tileslice>
    : Pat<(out_vt (intrinsic tile_imm:$tile, (i32 (tileslice MatrixIndexGPR32Op12_15:$base, index_ty:$offset)))),
          (!cast<Instruction>(name # _PSEUDO) $tile, $base, $offset)>;

//===----------------------------------------------------------------------===//
// SME pattern match helpers.
//===----------------------------------------------------------------------===//

class SME_ZA_Tile_TwoPred_TwoVec_Pat<string name, SDPatternOperator intrinsic, Operand imm_ty, ValueType pg_ty, ValueType vt>
    : Pat<(intrinsic imm_ty:$tile, (pg_ty PPR3bAny:$Pn), (pg_ty PPR3bAny:$Pm), vt:$Zn, vt:$Zm),
          (!cast<Instruction>(name # _PSEUDO) $tile, $Pn, $Pm, $Zn, $Zm)>;


//===----------------------------------------------------------------------===//
// SME smstart/smstop
//===----------------------------------------------------------------------===//

// SME defines three pstate fields to set or clear PSTATE.SM, PSTATE.ZA, or
// both fields:
//
//   MSR SVCRSM, #<imm1>
//   MSR SVCRZA, #<imm1>
//   MSR SVCRSMZA, #<imm1>
//
// It's tricky to using the existing pstate operand defined in
// AArch64SystemOperands.td since it only encodes 5 bits including op1;op2,
// when these fields are also encoded in CRm[3:1].
def MSRpstatesvcrImm1
  : PstateWriteSimple<(ins svcr_op:$pstatefield, timm0_1:$imm), "msr",
                      "\t$pstatefield, $imm">,
    Sched<[WriteSys]> {
  bits<3> pstatefield;
  bit imm;
  let Inst{18-16} = 0b011; // op1
  let Inst{11-9} = pstatefield;
  let Inst{8} = imm;
  let Inst{7-5} = 0b011; // op2
  let hasPostISelHook = 1;
}

def : InstAlias<"smstart",    (MSRpstatesvcrImm1 0b011, 0b1)>;
def : InstAlias<"smstart sm", (MSRpstatesvcrImm1 0b001, 0b1)>;
def : InstAlias<"smstart za", (MSRpstatesvcrImm1 0b010, 0b1)>;

def : InstAlias<"smstop",     (MSRpstatesvcrImm1 0b011, 0b0)>;
def : InstAlias<"smstop sm",  (MSRpstatesvcrImm1 0b001, 0b0)>;
def : InstAlias<"smstop za",  (MSRpstatesvcrImm1 0b010, 0b0)>;


//===----------------------------------------------------------------------===//
// SME Outer Products
//===----------------------------------------------------------------------===//

class sme_fp_outer_product_inst<bit S, bits<2> sz, bits<2> op, MatrixTileOperand za_ty,
                                ZPRRegOp zpr_ty, string mnemonic>
    : I<(outs za_ty:$ZAda),
      (ins za_ty:$_ZAda, PPR3bAny:$Pn, PPR3bAny:$Pm, zpr_ty:$Zn, zpr_ty:$Zm),
        mnemonic, "\t$ZAda, $Pn/m, $Pm/m, $Zn, $Zm",
        "", []>,
      Sched<[]> {
  bits<5> Zm;
  bits<3> Pm;
  bits<3> Pn;
  bits<5> Zn;
  let Inst{31-25} = 0b1000000;
  let Inst{24}    = op{1};
  let Inst{23}    = 0b1;
  let Inst{22-21} = sz;
  let Inst{20-16} = Zm;
  let Inst{15-13} = Pm;
  let Inst{12-10} = Pn;
  let Inst{9-5}   = Zn;
  let Inst{4}     = S;
  let Inst{3}     = op{0};

  let Constraints = "$ZAda = $_ZAda";
}

multiclass sme_outer_product_fp32<bit S, bits<2> sz, ZPRRegOp zpr_ty, string mnemonic, SDPatternOperator op> {
  def NAME : sme_fp_outer_product_inst<S, sz, 0b00, TileOp32, zpr_ty, mnemonic>, SMEPseudo2Instr<NAME, 1> {
    bits<2> ZAda;
    let Inst{1-0} = ZAda;
    let Inst{2}   = 0b0;
  }

  def NAME # _PSEUDO : sme_outer_product_pseudo<zpr_ty, SMEMatrixTileS>, SMEPseudo2Instr<NAME, 0>;

  def : SME_ZA_Tile_TwoPred_TwoVec_Pat<NAME, op, timm32_0_3, nxv4i1, nxv4f32>;
}

multiclass sme_outer_product_fp64<bit S, string mnemonic, SDPatternOperator op> {
  def NAME : sme_fp_outer_product_inst<S, 0b10, 0b00, TileOp64, ZPR64, mnemonic>, SMEPseudo2Instr<NAME, 1> {
    bits<3> ZAda;
    let Inst{2-0} = ZAda;
  }

  def NAME # _PSEUDO : sme_outer_product_pseudo<ZPR64, SMEMatrixTileD>, SMEPseudo2Instr<NAME, 0>;

  def : SME_ZA_Tile_TwoPred_TwoVec_Pat<NAME, op, timm32_0_7, nxv2i1, nxv2f64>;
}

multiclass sme2p1_fmop_tile_f8f16<string mnemonic, bit bf, bit s, bits<2> op> {
  def NAME : sme_fp_outer_product_inst<s, {0,bf}, op, TileOp16, ZPR8, mnemonic> {
    bits<1> ZAda;
    let Inst{2-1} = 0b00;
    let Inst{0}   = ZAda;
  }
}

multiclass sme2p1_fmop_tile_fp16<string mnemonic, bit bf, bit s, ValueType vt, SDPatternOperator intrinsic = null_frag> {
  def NAME : sme_fp_outer_product_inst<s, {0,bf}, 0b11, TileOp16, ZPR16, mnemonic>, SMEPseudo2Instr<NAME, 1> {
    bits<1> ZAda;
    let Inst{2-1} = 0b00;
    let Inst{0}   = ZAda;
  }

  def NAME # _PSEUDO : sme_outer_product_pseudo<ZPR16, SMEMatrixTileH>, SMEPseudo2Instr<NAME, 0>;

  def : SME_ZA_Tile_TwoPred_TwoVec_Pat<NAME, intrinsic, timm32_0_1, nxv8i1, vt>;
}

class sme_int_outer_product_inst<bits<3> opc, bit sz, bit sme2,
                                 MatrixTileOperand za_ty, ZPRRegOp zpr_ty,
                                 string mnemonic>
    : I<(outs za_ty:$ZAda),
        (ins za_ty:$_ZAda, PPR3bAny:$Pn, PPR3bAny:$Pm, zpr_ty:$Zn, zpr_ty:$Zm),
        mnemonic, "\t$ZAda, $Pn/m, $Pm/m, $Zn, $Zm",
        "", []>,
      Sched<[]> {
  bits<5> Zm;
  bits<3> Pm;
  bits<3> Pn;
  bits<5> Zn;
  let Inst{31-25} = 0b1010000;
  let Inst{24}    = opc{2}; // u0
  let Inst{23}    = 0b1;
  let Inst{22}    = sz;
  let Inst{21}    = opc{1}; // u1
  let Inst{20-16} = Zm;
  let Inst{15-13} = Pm;
  let Inst{12-10} = Pn;
  let Inst{9-5}   = Zn;
  let Inst{4}     = opc{0};  //S;
  let Inst{3}     = sme2;

  let Constraints = "$ZAda = $_ZAda";
}

multiclass sme_int_outer_product_i32<bits<3> opc, string mnemonic,
                                     SDPatternOperator op> {
  def NAME : sme_int_outer_product_inst<opc, 0b0, 0b0,  TileOp32,
                                        ZPR8, mnemonic>, SMEPseudo2Instr<NAME, 1> {
    bits<2> ZAda;
    let Inst{1-0} = ZAda;
    let Inst{2}   = 0b0;
  }

  def NAME # _PSEUDO : sme_outer_product_pseudo<ZPR8, SMEMatrixTileS>, SMEPseudo2Instr<NAME, 0>;

  def : SME_ZA_Tile_TwoPred_TwoVec_Pat<NAME, op, timm32_0_3, nxv16i1, nxv16i8>;
}

multiclass sme_int_outer_product_i64<bits<3> opc, string mnemonic,
                                     SDPatternOperator op> {
  def NAME : sme_int_outer_product_inst<opc, 0b1, 0b0, TileOp64,
                                        ZPR16, mnemonic>, SMEPseudo2Instr<NAME, 1> {
    bits<3> ZAda;
    let Inst{2-0} = ZAda;
  }

  def NAME # _PSEUDO : sme_outer_product_pseudo<ZPR16, SMEMatrixTileD>, SMEPseudo2Instr<NAME, 0>;

  def : SME_ZA_Tile_TwoPred_TwoVec_Pat<NAME, op, timm32_0_7, nxv8i1, nxv8i16>;
}

class sme_outer_product_widening_inst<bits<3> opc, ZPRRegOp zpr_ty, string mnemonic>
    : I<(outs TileOp32:$ZAda),
        (ins  TileOp32:$_ZAda, PPR3bAny:$Pn, PPR3bAny:$Pm, zpr_ty:$Zn, zpr_ty:$Zm),
        mnemonic, "\t$ZAda, $Pn/m, $Pm/m, $Zn, $Zm",
        "", []>,
      Sched<[]> {
  bits<5> Zm;
  bits<3> Pm;
  bits<3> Pn;
  bits<5> Zn;
  bits<2> ZAda;
  let Inst{31-25} = 0b1000000;
  let Inst{24}    = !if(opc{2}, 0, 1);
  let Inst{23-22} = 0b10;
  let Inst{21}    = opc{1};
  let Inst{20-16} = Zm;
  let Inst{15-13} = Pm;
  let Inst{12-10} = Pn;
  let Inst{9-5}   = Zn;
  let Inst{4}     = opc{0};
  let Inst{3}     = opc{2};
  let Inst{2}     = 0b0;
  let Inst{1-0}   = ZAda;

  let Constraints = "$ZAda = $_ZAda";
}

multiclass sme_bf16_outer_product<bits<3> opc, string mnemonic, SDPatternOperator op> {
  def NAME : sme_outer_product_widening_inst<opc, ZPR16, mnemonic>, SMEPseudo2Instr<NAME, 1>;

  def NAME # _PSEUDO : sme_outer_product_pseudo<ZPR16, SMEMatrixTileS>, SMEPseudo2Instr<NAME, 0>;

  def : SME_ZA_Tile_TwoPred_TwoVec_Pat<NAME, op, timm32_0_3, nxv8i1, nxv8bf16>;
}

multiclass sme_f16_outer_product<bits<3> opc, string mnemonic, SDPatternOperator op> {
  def NAME : sme_outer_product_widening_inst<opc, ZPR16, mnemonic>, SMEPseudo2Instr<NAME, 1>;

  def NAME # _PSEUDO : sme_outer_product_pseudo<ZPR16, SMEMatrixTileS>, SMEPseudo2Instr<NAME, 0>;

  def : SME_ZA_Tile_TwoPred_TwoVec_Pat<NAME, op, timm32_0_3, nxv8i1, nxv8f16>;
}

//===----------------------------------------------------------------------===//
// SME Add Vector to Tile
//===----------------------------------------------------------------------===//

class sme_add_vector_to_tile_inst<bit op, bit V, MatrixTileOperand tile_ty,
                                  ZPRRegOp zpr_ty, string mnemonic>
    : I<(outs tile_ty:$ZAda),
        (ins tile_ty:$_ZAda, PPR3bAny:$Pn, PPR3bAny:$Pm, zpr_ty:$Zn),
        mnemonic, "\t$ZAda, $Pn/m, $Pm/m, $Zn",
        "", []>, Sched<[]> {
  bits<3> Pm;
  bits<3> Pn;
  bits<5> Zn;
  let Inst{31-23} = 0b110000001;
  let Inst{22}    = op;
  let Inst{21-17} = 0b01000;
  let Inst{16}    = V;
  let Inst{15-13} = Pm;
  let Inst{12-10} = Pn;
  let Inst{9-5}   = Zn;
  let Inst{4-3}   = 0b00;

  let Constraints = "$ZAda = $_ZAda";
}

class sme_add_vector_to_tile_pseudo<ZPRRegOp zpr_ty, SMEMatrixTypeEnum za_flag>
    : Pseudo<(outs),
             (ins i32imm:$tile, PPR3bAny:$Pn, PPR3bAny:$Pm, zpr_ty:$Zn), []>,
      Sched<[]> {
  // Translated to the actual instructions in AArch64ISelLowering.cpp
  let SMEMatrixType = za_flag;
  let usesCustomInserter = 1;
}

multiclass sme_add_vector_to_tile_u32<bit V, string mnemonic, SDPatternOperator op> {
    def NAME : sme_add_vector_to_tile_inst<0b0, V, TileOp32, ZPR32, mnemonic>, SMEPseudo2Instr<NAME, 1> {
  bits<2> ZAda;
  let Inst{2}   = 0b0;
  let Inst{1-0} = ZAda;
  }

  def _PSEUDO_S : sme_add_vector_to_tile_pseudo<ZPR32, SMEMatrixTileS>, SMEPseudo2Instr<NAME, 0>;

  def : Pat<(op timm32_0_3:$tile, (nxv4i1 PPR3bAny:$pn), (nxv4i1 PPR3bAny:$pm),
            (nxv4i32 ZPR32:$zn)),
          (!cast<Instruction>(NAME # _PSEUDO_S) timm32_0_3:$tile, $pn, $pm, $zn)>;
}

multiclass sme_add_vector_to_tile_u64<bit V, string mnemonic, SDPatternOperator op> {
    def NAME : sme_add_vector_to_tile_inst<0b1, V, TileOp64, ZPR64, mnemonic>, SMEPseudo2Instr<NAME, 1> {
  bits<3> ZAda;
  let Inst{2-0} = ZAda;
  }

  def _PSEUDO_D : sme_add_vector_to_tile_pseudo<ZPR64, SMEMatrixTileD>, SMEPseudo2Instr<NAME, 0>;

  let Predicates = [HasSMEI16I64] in {
  def : Pat<(op timm32_0_7:$tile, (nxv2i1 PPR3bAny:$pn), (nxv2i1 PPR3bAny:$pm),
                (nxv2i64 ZPR64:$zn)),
            (!cast<Instruction>(NAME # _PSEUDO_D) timm32_0_7:$tile, $pn, $pm, $zn)>;
  }
}

//===----------------------------------------------------------------------===//
// SME Contiguous Loads
//===----------------------------------------------------------------------===//

class sme_mem_ld_ss_base<bit Q, bit V, bits<2> msz, dag outs, dag ins,
                         string mnemonic, string argstr>
    : I<outs, ins, mnemonic, argstr, "", []>, Sched<[]> {
  bits<5> Rm;
  bits<2> Rv;
  bits<3> Pg;
  bits<5> Rn;
  let Inst{31-25} = 0b1110000;
  let Inst{24}    = Q;
  let Inst{23-22} = msz;
  let Inst{21}    = 0b0;
  let Inst{20-16} = Rm;
  let Inst{15}    = V;
  let Inst{14-13} = Rv;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Rn;
  let Inst{4}     = 0b0;

  let mayLoad = 1;
}

class sme_mem_ld_ss_inst<bit Q, bits<2> msz, string mnemonic,
                         MatrixTileVectorOperand tile_ty, bit is_col,
                         Operand imm_ty, RegisterOperand gpr_ty>
    : sme_mem_ld_ss_base<
        Q, is_col, msz, (outs tile_ty:$ZAt),
        (ins MatrixIndexGPR32Op12_15:$Rv, imm_ty:$imm, PPR3bAny:$Pg, GPR64sp:$Rn,
             gpr_ty:$Rm),
        mnemonic, "\t\\{$ZAt[$Rv, $imm]\\}, $Pg/z, [$Rn, $Rm]">;

multiclass sme_mem_ss_aliases_base<string mnemonic, Instruction inst,
                                   MatrixTileVectorOperand tile_ty,
                                   Operand imm_ty, RegisterOperand gpr_ty,
                                   string pg_suffix=""> {
  def : InstAlias<mnemonic # "\t$ZAt[$Rv, $imm], $Pg" # pg_suffix # ", [$Rn, $Rm]",
                  (inst tile_ty:$ZAt, MatrixIndexGPR32Op12_15:$Rv, imm_ty:$imm, PPR3bAny:$Pg, GPR64sp:$Rn, gpr_ty:$Rm), 0>;
  // Default XZR offset aliases
  def : InstAlias<mnemonic # "\t\\{$ZAt[$Rv, $imm]\\}, $Pg" # pg_suffix # ", [$Rn]",
                  (inst tile_ty:$ZAt, MatrixIndexGPR32Op12_15:$Rv, imm_ty:$imm, PPR3bAny:$Pg, GPR64sp:$Rn, XZR), 1>;
  def : InstAlias<mnemonic # "\t$ZAt[$Rv, $imm], $Pg" # pg_suffix # ", [$Rn]",
                  (inst tile_ty:$ZAt, MatrixIndexGPR32Op12_15:$Rv, imm_ty:$imm, PPR3bAny:$Pg, GPR64sp:$Rn, XZR), 0>;
}

multiclass sme_mem_ss_aliases<string mnemonic, string inst, bit is_col,
                              string pg_suffix=""> {
  defm : sme_mem_ss_aliases_base<mnemonic # "b", !cast<Instruction>(inst # _B),
                                 !if(is_col, TileVectorOpV8, TileVectorOpH8),
                                 sme_elm_idx0_15, GPR64shifted8, pg_suffix>;
  defm : sme_mem_ss_aliases_base<mnemonic # "h", !cast<Instruction>(inst # _H),
                                 !if(is_col, TileVectorOpV16, TileVectorOpH16),
                                 sme_elm_idx0_7, GPR64shifted16, pg_suffix>;
  defm : sme_mem_ss_aliases_base<mnemonic # "w", !cast<Instruction>(inst # _S),
                                 !if(is_col, TileVectorOpV32, TileVectorOpH32),
                                 sme_elm_idx0_3, GPR64shifted32, pg_suffix>;
  defm : sme_mem_ss_aliases_base<mnemonic # "d", !cast<Instruction>(inst # _D),
                                 !if(is_col, TileVectorOpV64, TileVectorOpH64),
                                 sme_elm_idx0_1, GPR64shifted64, pg_suffix>;
  defm : sme_mem_ss_aliases_base<mnemonic # "q", !cast<Instruction>(inst # _Q),
                                 !if(is_col, TileVectorOpV128, TileVectorOpH128),
                                 sme_elm_idx0_0, GPR64shifted128, pg_suffix>;
}

multiclass sme_mem_ld_ss_aliases<string inst, bit is_col> {
  defm NAME : sme_mem_ss_aliases<"ld1", inst, is_col, "/z">;
}

multiclass sme_mem_ld_ss_patterns<Instruction Inst, SDPatternOperator Load,
                                  Operand tile_ty, Operand offset_ty,
                                  ComplexPattern addr,
                                  ComplexPattern tileslice> {
  // base, tileslice
  def : Pat<(Load PPR3bAny:$pg, GPR64sp:$base, tile_ty:$tile,
                  (i32 (tileslice MatrixIndexGPR32Op12_15:$idx, offset_ty:$imm))),
            (Inst tile_ty:$tile, $idx, $imm, $pg, $base, XZR)>;

  // reg + reg, tileslice
  let AddedComplexity = 1 in {
    def : Pat<(Load PPR3bAny:$pg, (addr GPR64sp:$base, GPR64:$offset),
                    tile_ty:$tile, (i32 (tileslice MatrixIndexGPR32Op12_15:$idx,
                                              offset_ty:$imm))),
              (Inst tile_ty:$tile, $idx, $imm, $pg, $base, $offset)>;
  }
}

class sme_load_pseudo
    : Pseudo<(outs), (ins i32imm:$tile, MatrixIndexGPR32Op12_15:$idx,
                          i32imm:$imm, PPR3bAny:$pg, GPR64sp:$base, GPR64:$offset), []>,
      Sched<[]> {
  // Translated to the actual instructions in AArch64ISelLowering.cpp
  let usesCustomInserter = 1;
  let mayLoad = 1;
}

multiclass sme_mem_ld_v_ss<string mnemonic, bit is_col> {
  def _B : sme_mem_ld_ss_inst<0b0, 0b00, mnemonic # "b",
                              !if(is_col, TileVectorOpV8, TileVectorOpH8),
                              is_col, sme_elm_idx0_15, GPR64shifted8> {
    bits<4> imm;
    let Inst{3-0} = imm;
  }
  def _H : sme_mem_ld_ss_inst<0b0, 0b01, mnemonic # "h",
                              !if(is_col, TileVectorOpV16, TileVectorOpH16),
                              is_col, sme_elm_idx0_7, GPR64shifted16> {
    bits<1> ZAt;
    bits<3> imm;
    let Inst{3}   = ZAt;
    let Inst{2-0} = imm;
  }
  def _S : sme_mem_ld_ss_inst<0b0, 0b10, mnemonic # "w",
                              !if(is_col, TileVectorOpV32, TileVectorOpH32),
                              is_col, sme_elm_idx0_3, GPR64shifted32> {
    bits<2> ZAt;
    bits<2> imm;
    let Inst{3-2} = ZAt;
    let Inst{1-0} = imm;
  }
  def _D : sme_mem_ld_ss_inst<0b0, 0b11, mnemonic # "d",
                              !if(is_col, TileVectorOpV64, TileVectorOpH64),
                              is_col, sme_elm_idx0_1, GPR64shifted64> {
    bits<3> ZAt;
    bits<1> imm;
    let Inst{3-1} = ZAt;
    let Inst{0}   = imm;
  }
  def _Q : sme_mem_ld_ss_inst<0b1, 0b11, mnemonic # "q",
                              !if(is_col, TileVectorOpV128, TileVectorOpH128),
                              is_col, sme_elm_idx0_0, GPR64shifted128> {
    bits<4> ZAt;
    let Inst{3-0} = ZAt;
  }

  defm : sme_mem_ld_ss_aliases<NAME, is_col>;

  // Pseudo instructions for lowering intrinsics, using immediates instead of
  // tile registers.
  def _PSEUDO_B : sme_load_pseudo;
  def _PSEUDO_H : sme_load_pseudo;
  def _PSEUDO_S : sme_load_pseudo;
  def _PSEUDO_D : sme_load_pseudo;
  def _PSEUDO_Q : sme_load_pseudo;

  defm : sme_mem_ld_ss_patterns<!cast<Instruction>(NAME # _PSEUDO_B),
                                !if(is_col, int_aarch64_sme_ld1b_vert,
                                            int_aarch64_sme_ld1b_horiz),
                                sme_elm_idx0_0, timm32_0_15, am_sve_regreg_lsl0,
                                tileslice8>;
  defm : sme_mem_ld_ss_patterns<!cast<Instruction>(NAME # _PSEUDO_H),
                                !if(is_col, int_aarch64_sme_ld1h_vert,
                                            int_aarch64_sme_ld1h_horiz),
                                timm32_0_1, timm32_0_7, am_sve_regreg_lsl1,
                                tileslice16>;
  defm : sme_mem_ld_ss_patterns<!cast<Instruction>(NAME # _PSEUDO_S),
                                !if(is_col, int_aarch64_sme_ld1w_vert,
                                            int_aarch64_sme_ld1w_horiz),
                                timm32_0_3, timm32_0_3, am_sve_regreg_lsl2,
                                tileslice32>;
  defm : sme_mem_ld_ss_patterns<!cast<Instruction>(NAME # _PSEUDO_D),
                                !if(is_col, int_aarch64_sme_ld1d_vert,
                                            int_aarch64_sme_ld1d_horiz),
                                timm32_0_7, timm32_0_1, am_sve_regreg_lsl3,
                                tileslice64>;
  defm : sme_mem_ld_ss_patterns<!cast<Instruction>(NAME # _PSEUDO_Q),
                                !if(is_col, int_aarch64_sme_ld1q_vert,
                                            int_aarch64_sme_ld1q_horiz),
                                timm32_0_15, sme_elm_idx0_0, am_sve_regreg_lsl4,
                                tileslice128>;
}

multiclass sme_mem_ld_ss<string mnemonic> {
  defm _H : sme_mem_ld_v_ss<mnemonic, /*is_col=*/0b0>;
  defm _V : sme_mem_ld_v_ss<mnemonic, /*is_col=*/0b1>;
}

//===----------------------------------------------------------------------===//
// SME Contiguous Stores
//===----------------------------------------------------------------------===//

class sme_mem_st_ss_base<bit Q, bit V, bits<2> msz, dag ins,
                         string mnemonic, string argstr>
    : I<(outs), ins, mnemonic, argstr, "", []>, Sched<[]> {
  bits<5> Rm;
  bits<2> Rv;
  bits<3> Pg;
  bits<5> Rn;
  let Inst{31-25} = 0b1110000;
  let Inst{24}    = Q;
  let Inst{23-22} = msz;
  let Inst{21}    = 0b1;
  let Inst{20-16} = Rm;
  let Inst{15}    = V;
  let Inst{14-13} = Rv;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Rn;
  let Inst{4}     = 0b0;

  let mayStore = 1;
  let hasSideEffects = 1;
}

class sme_mem_st_ss_inst<bit Q, bits<2> msz, string mnemonic,
                         MatrixTileVectorOperand tile_ty, bit is_col,
                         Operand imm_ty, RegisterOperand gpr_ty>
    : sme_mem_st_ss_base<
        Q, is_col, msz,
        (ins tile_ty:$ZAt, MatrixIndexGPR32Op12_15:$Rv, imm_ty:$imm, PPR3bAny:$Pg,
             GPR64sp:$Rn, gpr_ty:$Rm),
        mnemonic, "\t\\{$ZAt[$Rv, $imm]\\}, $Pg, [$Rn, $Rm]">;

multiclass sme_mem_st_ss_aliases<string inst, bit is_col> {
  defm NAME : sme_mem_ss_aliases<"st1", inst, is_col>;
}

multiclass sme_mem_st_ss_patterns<Instruction Inst, SDPatternOperator Store,
                                  Operand offset_ty,
                                  ComplexPattern imm2tile,
                                  ComplexPattern addr,
                                  ComplexPattern tileslice> {
  // base, tileslice
  def : Pat<(Store PPR3bAny:$pg, GPR64sp:$base, (imm2tile untyped:$tile),
                   (i32 (tileslice MatrixIndexGPR32Op12_15:$idx, offset_ty:$imm))),
            (Inst $tile, $idx, $imm, $pg, $base, XZR)>;

  // reg + reg, tileslice
  let AddedComplexity = 1 in {
    def : Pat<(Store PPR3bAny:$pg, (addr GPR64sp:$base, GPR64:$offset),
                     (imm2tile untyped:$tile),
                     (i32 (tileslice MatrixIndexGPR32Op12_15:$idx, offset_ty:$imm))),
              (Inst $tile, $idx, $imm, $pg, $base, $offset)>;
  }
}

multiclass sme_mem_st_v_ss<string mnemonic, bit is_col> {
  def _B : sme_mem_st_ss_inst<0b0, 0b00, mnemonic # "b",
                              !if(is_col, TileVectorOpV8, TileVectorOpH8),
                              is_col, sme_elm_idx0_15, GPR64shifted8> {
    bits<4> imm;
    let Inst{3-0} = imm;
  }
  def _H : sme_mem_st_ss_inst<0b0, 0b01, mnemonic # "h",
                              !if(is_col, TileVectorOpV16, TileVectorOpH16),
                              is_col, sme_elm_idx0_7, GPR64shifted16> {
    bits<1> ZAt;
    bits<3> imm;
    let Inst{3}   = ZAt;
    let Inst{2-0} = imm;
  }
  def _S : sme_mem_st_ss_inst<0b0, 0b10, mnemonic # "w",
                              !if(is_col, TileVectorOpV32, TileVectorOpH32),
                              is_col, sme_elm_idx0_3, GPR64shifted32> {
    bits<2> ZAt;
    bits<2> imm;
    let Inst{3-2} = ZAt;
    let Inst{1-0} = imm;
  }
  def _D : sme_mem_st_ss_inst<0b0, 0b11, mnemonic # "d",
                              !if(is_col, TileVectorOpV64, TileVectorOpH64),
                              is_col, sme_elm_idx0_1, GPR64shifted64> {
    bits<3> ZAt;
    bits<1> imm;
    let Inst{3-1} = ZAt;
    let Inst{0}   = imm;
  }
  def _Q : sme_mem_st_ss_inst<0b1, 0b11, mnemonic # "q",
                              !if(is_col, TileVectorOpV128, TileVectorOpH128),
                              is_col, sme_elm_idx0_0, GPR64shifted128> {
    bits<4> ZAt;
    let Inst{3-0} = ZAt;
  }

  defm : sme_mem_st_ss_aliases<NAME, is_col>;

  defm : sme_mem_st_ss_patterns<!cast<Instruction>(NAME # _B),
                                !if(is_col, int_aarch64_sme_st1b_vert,
                                            int_aarch64_sme_st1b_horiz),
                                timm32_0_15, imm_to_tile8, am_sve_regreg_lsl0,
                                tileslice8>;
  defm : sme_mem_st_ss_patterns<!cast<Instruction>(NAME # _H),
                                !if(is_col, int_aarch64_sme_st1h_vert,
                                            int_aarch64_sme_st1h_horiz),
                                timm32_0_7, imm_to_tile16, am_sve_regreg_lsl1,
                                tileslice16>;
  defm : sme_mem_st_ss_patterns<!cast<Instruction>(NAME # _S),
                                !if(is_col, int_aarch64_sme_st1w_vert,
                                            int_aarch64_sme_st1w_horiz),
                                timm32_0_3, imm_to_tile32, am_sve_regreg_lsl2,
                                tileslice32>;
  defm : sme_mem_st_ss_patterns<!cast<Instruction>(NAME # _D),
                                !if(is_col, int_aarch64_sme_st1d_vert,
                                            int_aarch64_sme_st1d_horiz),
                                timm32_0_1, imm_to_tile64, am_sve_regreg_lsl3,
                                tileslice64>;
  defm : sme_mem_st_ss_patterns<!cast<Instruction>(NAME # _Q),
                                !if(is_col, int_aarch64_sme_st1q_vert,
                                            int_aarch64_sme_st1q_horiz),
                                sme_elm_idx0_0, imm_to_tile128,
                                am_sve_regreg_lsl4, tileslice128>;
}

multiclass sme_mem_st_ss<string mnemonic> {
  defm _H : sme_mem_st_v_ss<mnemonic, /*is_col=*/0b0>;
  defm _V : sme_mem_st_v_ss<mnemonic, /*is_col=*/0b1>;
}

//===----------------------------------------------------------------------===//
// SME Save and Restore Array
//===----------------------------------------------------------------------===//

class sme_spill_fill_base<bit isStore, dag outs, dag ins, string opcodestr>
    : I<outs, ins, opcodestr, "\t$ZAt[$Rv, $imm4], [$Rn, $offset, mul vl]", "",
        []>,
      Sched<[]> {
  bits<2> Rv;
  bits<5> Rn;
  bits<4> imm4;
  let Inst{31-22} = 0b1110000100;
  let Inst{21}    = isStore;
  let Inst{20-15} = 0b000000;
  let Inst{14-13} = Rv;
  let Inst{12-10} = 0b000;
  let Inst{9-5}   = Rn;
  let Inst{4}     = 0b0;
  let Inst{3-0}   = imm4;
}

let mayStore = 1 in
class sme_spill_inst<string opcodestr>
    : sme_spill_fill_base<0b1, (outs),
                          (ins MatrixOp:$ZAt, MatrixIndexGPR32Op12_15:$Rv,
                               sme_elm_idx0_15:$imm4, GPR64sp:$Rn,
                               imm32_0_15:$offset),
                          opcodestr>;
let mayLoad = 1 in
class sme_fill_inst<string opcodestr>
    : sme_spill_fill_base<0b0, (outs MatrixOp:$ZAt),
                          (ins MatrixIndexGPR32Op12_15:$Rv,
                               sme_elm_idx0_15:$imm4, GPR64sp:$Rn,
                               imm32_0_15:$offset),
                          opcodestr>;
multiclass sme_spill<string opcodestr> {
  def NAME : sme_spill_inst<opcodestr>;
  def : InstAlias<opcodestr # "\t$ZAt[$Rv, $imm4], [$Rn]",
                  (!cast<Instruction>(NAME) MatrixOp:$ZAt,
                   MatrixIndexGPR32Op12_15:$Rv, sme_elm_idx0_15:$imm4, GPR64sp:$Rn, 0), 1>;

  def : Pat<(AArch64SMEStr (i32 MatrixIndexGPR32Op12_15:$slice), (i64 GPR64sp:$base), (i32 sme_elm_idx0_15:$imm)),
          (!cast<Instruction>(NAME) ZA, MatrixIndexGPR32Op12_15:$slice, sme_elm_idx0_15:$imm, GPR64sp:$base, imm32_0_15:$imm)>;
}

multiclass sme_fill<string opcodestr> {
  def NAME : sme_fill_inst<opcodestr>;
  def : InstAlias<opcodestr # "\t$ZAt[$Rv, $imm4], [$Rn]",
                  (!cast<Instruction>(NAME) MatrixOp:$ZAt,
                   MatrixIndexGPR32Op12_15:$Rv, sme_elm_idx0_15:$imm4, GPR64sp:$Rn, 0), 1>;
  def NAME # _PSEUDO
      : Pseudo<(outs),
               (ins MatrixIndexGPR32Op12_15:$idx, sme_elm_idx0_15:$imm4,
                    GPR64sp:$base), []>,
        Sched<[]> {
    // Translated to actual instruction in AArch64ISelLowering.cpp
    let usesCustomInserter = 1;
    let mayLoad = 1;
  }
  def : Pat<(AArch64SMELdr MatrixIndexGPR32Op12_15:$slice, GPR64sp:$base, sme_elm_idx0_15:$imm),
          (!cast<Instruction>(NAME # _PSEUDO) MatrixIndexGPR32Op12_15:$slice, sme_elm_idx0_15:$imm, GPR64sp:$base)>;
}

//===----------------------------------------------------------------------===//
// Move instructions
//===----------------------------------------------------------------------===//

class sme_vector_to_tile_base<bit Q, bit V, bits<2> sz, dag outs, dag ins,
                              string mnemonic, string argstr>
    : I<outs, ins, mnemonic, argstr, "", []>, Sched<[]> {
  bits<2> Rv;
  bits<3> Pg;
  bits<5> Zn;
  let Inst{31-24} = 0b11000000;
  let Inst{23-22} = sz;
  let Inst{21-17} = 0b00000;
  let Inst{16}    = Q;
  let Inst{15}    = V;
  let Inst{14-13} = Rv;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zn;
  let Inst{4}     = 0b0;
}

class sme_vector_to_tile_inst<bit Q, bits<2> sz, MatrixTileVectorOperand tile_ty,
                              bit is_col, Operand imm_ty, ZPRRegOp zpr_ty,
                              string mnemonic>
    : sme_vector_to_tile_base<Q, is_col, sz, (outs tile_ty:$ZAd),
        (ins tile_ty:$_ZAd, MatrixIndexGPR32Op12_15:$Rv, imm_ty:$imm, PPR3bAny:$Pg, zpr_ty:$Zn),
        mnemonic, "\t$ZAd[$Rv, $imm], $Pg/m, $Zn">{

  let Constraints = "$ZAd = $_ZAd";
}


multiclass sme_vector_to_tile_aliases<Instruction inst,
                                      MatrixTileVectorOperand tile_ty,
                                      ZPRRegOp zpr_ty, Operand imm_ty> {
  def : InstAlias<"mov\t$ZAd[$Rv, $imm], $Pg/m, $Zn",
                  (inst tile_ty:$ZAd, MatrixIndexGPR32Op12_15:$Rv, imm_ty:$imm, PPR3bAny:$Pg, zpr_ty:$Zn), 1>;
}

multiclass sme_vector_to_tile_patterns<Instruction inst, ValueType zpr_vt,
                                       ValueType ppr_vt, Operand imm_ty,
                                       Operand offset_ty,
                                       SDPatternOperator op,
                                       ComplexPattern tileslice> {
  def : Pat<(op imm_ty:$tile, (i32 (tileslice MatrixIndexGPR32Op12_15:$idx,
                                              offset_ty:$imm)),
                (ppr_vt PPR3bAny:$pg), (zpr_vt ZPRAny:$zn)),
            (inst imm_ty:$tile, $idx, $imm, $pg, $zn)>;
}

class sme_mova_insert_pseudo<SMEMatrixTypeEnum za_flag>
    : Pseudo<(outs), (ins i32imm:$tile, MatrixIndexGPR32Op12_15:$idx,
                          i32imm:$imm, PPR3bAny:$pg, ZPRAny:$zn), []>,
      Sched<[]> {
  // Translated to the actual instructions in AArch64ISelLowering.cpp
  let SMEMatrixType = za_flag;
  let usesCustomInserter = 1;
}

multiclass sme_vector_v_to_tile<string mnemonic, bit is_col> {
  def _B : sme_vector_to_tile_inst<0b0, 0b00, !if(is_col, TileVectorOpV8,
                                                          TileVectorOpH8),
                                   is_col, sme_elm_idx0_15, ZPR8, mnemonic>,
                                   SMEPseudo2Instr<NAME # _B, 1> {
    bits<4> imm;
    let Inst{3-0} = imm;
  }
  def _H : sme_vector_to_tile_inst<0b0, 0b01, !if(is_col, TileVectorOpV16,
                                                          TileVectorOpH16),
                                   is_col, sme_elm_idx0_7, ZPR16, mnemonic>,
                                   SMEPseudo2Instr<NAME # _H, 1> {
    bits<1> ZAd;
    bits<3> imm;
    let Inst{3}   = ZAd;
    let Inst{2-0} = imm;
  }
  def _S : sme_vector_to_tile_inst<0b0, 0b10, !if(is_col, TileVectorOpV32,
                                                          TileVectorOpH32),
                                   is_col, sme_elm_idx0_3, ZPR32, mnemonic>,
                                   SMEPseudo2Instr<NAME # _S, 1> {
    bits<2> ZAd;
    bits<2> imm;
    let Inst{3-2} = ZAd;
    let Inst{1-0} = imm;
  }
  def _D : sme_vector_to_tile_inst<0b0, 0b11, !if(is_col, TileVectorOpV64,
                                                          TileVectorOpH64),
                                   is_col, sme_elm_idx0_1, ZPR64, mnemonic>,
                                   SMEPseudo2Instr<NAME # _D, 1> {
    bits<3> ZAd;
    bits<1> imm;
    let Inst{3-1} = ZAd;
    let Inst{0}   = imm;
  }
  def _Q : sme_vector_to_tile_inst<0b1, 0b11, !if(is_col, TileVectorOpV128,
                                                          TileVectorOpH128),
                                   is_col, sme_elm_idx0_0, ZPR128, mnemonic>,
                                   SMEPseudo2Instr<NAME # _Q, 1> {
    bits<4> ZAd;
    bits<1> imm;
    let Inst{3-0} = ZAd;
  }

  // Pseudo instructions for lowering intrinsics, using immediates instead of
  // tile registers.
  def _PSEUDO_B : sme_mova_insert_pseudo<SMEMatrixTileB>, SMEPseudo2Instr<NAME # _B, 0>;
  def _PSEUDO_H : sme_mova_insert_pseudo<SMEMatrixTileH>, SMEPseudo2Instr<NAME # _H, 0>;
  def _PSEUDO_S : sme_mova_insert_pseudo<SMEMatrixTileS>, SMEPseudo2Instr<NAME # _S, 0>;
  def _PSEUDO_D : sme_mova_insert_pseudo<SMEMatrixTileD>, SMEPseudo2Instr<NAME # _D, 0>;
  def _PSEUDO_Q : sme_mova_insert_pseudo<SMEMatrixTileQ>, SMEPseudo2Instr<NAME # _Q, 0>;

  defm : sme_vector_to_tile_aliases<!cast<Instruction>(NAME # _B),
                                    !if(is_col, TileVectorOpV8,
                                                TileVectorOpH8),
                                    ZPR8, sme_elm_idx0_15>;
  defm : sme_vector_to_tile_aliases<!cast<Instruction>(NAME # _H),
                                    !if(is_col, TileVectorOpV16,
                                                TileVectorOpH16),
                                    ZPR16, sme_elm_idx0_7>;
  defm : sme_vector_to_tile_aliases<!cast<Instruction>(NAME # _S),
                                    !if(is_col, TileVectorOpV32,
                                                TileVectorOpH32),
                                    ZPR32, sme_elm_idx0_3>;
  defm : sme_vector_to_tile_aliases<!cast<Instruction>(NAME # _D),
                                    !if(is_col, TileVectorOpV64,
                                                TileVectorOpH64),
                                    ZPR64, sme_elm_idx0_1>;
  defm : sme_vector_to_tile_aliases<!cast<Instruction>(NAME # _Q),
                                    !if(is_col, TileVectorOpV128,
                                                TileVectorOpH128),
                                    ZPR128, sme_elm_idx0_0>;

  defvar op = !if(is_col, int_aarch64_sme_write_vert,
                          int_aarch64_sme_write_horiz);

  defm : sme_vector_to_tile_patterns<!cast<Instruction>(NAME # _PSEUDO_B),
                                     nxv16i8, nxv16i1, sme_elm_idx0_0, sme_elm_idx0_15,
                                     op, tileslice8>;
  defm : sme_vector_to_tile_patterns<!cast<Instruction>(NAME # _PSEUDO_H),
                                     nxv8i16, nxv8i1, sme_elm_idx0_1, sme_elm_idx0_7,
                                     op, tileslice16>;
  defm : sme_vector_to_tile_patterns<!cast<Instruction>(NAME # _PSEUDO_H),
                                     nxv8f16, nxv8i1, sme_elm_idx0_1, sme_elm_idx0_7,
                                     op, tileslice16>;
  defm : sme_vector_to_tile_patterns<!cast<Instruction>(NAME # _PSEUDO_H),
                                     nxv8bf16, nxv8i1, sme_elm_idx0_1, sme_elm_idx0_7,
                                     op, tileslice16>;
  defm : sme_vector_to_tile_patterns<!cast<Instruction>(NAME # _PSEUDO_S),
                                     nxv4i32, nxv4i1, sme_elm_idx0_3, sme_elm_idx0_3,
                                     op, tileslice32>;
  defm : sme_vector_to_tile_patterns<!cast<Instruction>(NAME # _PSEUDO_S),
                                     nxv4f32, nxv4i1, sme_elm_idx0_3, sme_elm_idx0_3,
                                     op, tileslice32>;
  defm : sme_vector_to_tile_patterns<!cast<Instruction>(NAME # _PSEUDO_D),
                                     nxv2i64, nxv2i1, sme_elm_idx0_7, sme_elm_idx0_1,
                                     op, tileslice64>;
  defm : sme_vector_to_tile_patterns<!cast<Instruction>(NAME # _PSEUDO_D),
                                     nxv2f64, nxv2i1, sme_elm_idx0_7, sme_elm_idx0_1,
                                     op, tileslice64>;

  defvar opq = !if(is_col, int_aarch64_sme_writeq_vert,
                           int_aarch64_sme_writeq_horiz);

  defm : sme_vector_to_tile_patterns<!cast<Instruction>(NAME # _PSEUDO_Q),
                                     nxv16i8, nxv16i1, sme_elm_idx0_15,
                                     sme_elm_idx0_0, opq, tileslice128>;
  defm : sme_vector_to_tile_patterns<!cast<Instruction>(NAME # _PSEUDO_Q),
                                     nxv8i16, nxv8i1, sme_elm_idx0_15,
                                     sme_elm_idx0_0, opq, tileslice128>;
  defm : sme_vector_to_tile_patterns<!cast<Instruction>(NAME # _PSEUDO_Q),
                                     nxv8f16, nxv8i1, sme_elm_idx0_15,
                                     sme_elm_idx0_0, opq, tileslice128>;
  defm : sme_vector_to_tile_patterns<!cast<Instruction>(NAME # _PSEUDO_Q),
                                     nxv8bf16, nxv8i1, sme_elm_idx0_15,
                                     sme_elm_idx0_0, opq, tileslice128>;
  defm : sme_vector_to_tile_patterns<!cast<Instruction>(NAME # _PSEUDO_Q),
                                     nxv4i32, nxv4i1, sme_elm_idx0_15,
                                     sme_elm_idx0_0, opq, tileslice128>;
  defm : sme_vector_to_tile_patterns<!cast<Instruction>(NAME # _PSEUDO_Q),
                                     nxv4f32, nxv4i1, sme_elm_idx0_15,
                                     sme_elm_idx0_0, opq, tileslice128>;
  defm : sme_vector_to_tile_patterns<!cast<Instruction>(NAME # _PSEUDO_Q),
                                     nxv2i64, nxv2i1, sme_elm_idx0_15,
                                     sme_elm_idx0_0, opq, tileslice128>;
  defm : sme_vector_to_tile_patterns<!cast<Instruction>(NAME # _PSEUDO_Q),
                                     nxv2f64, nxv2i1, sme_elm_idx0_15,
                                     sme_elm_idx0_0, opq, tileslice128>;
}

multiclass sme_vector_to_tile<string mnemonic> {
  defm _H : sme_vector_v_to_tile<mnemonic, /*is_col=*/0b0>;
  defm _V : sme_vector_v_to_tile<mnemonic, /*is_col=*/0b1>;
}

class sme_tile_to_vector_base<bit Q, bit V, bits<2> sz, dag outs, dag ins,
                              string mnemonic, string argstr>
    : I<outs, ins, mnemonic, argstr, "", []>, Sched<[]> {
  bits<2> Rv;
  bits<3> Pg;
  bits<5> Zd;
  let Inst{31-24} = 0b11000000;
  let Inst{23-22} = sz;
  let Inst{21-17} = 0b00001;
  let Inst{16}    = Q;
  let Inst{15}    = V;
  let Inst{14-13} = Rv;
  let Inst{12-10} = Pg;
  let Inst{9}     = 0b0;
  let Inst{4-0}   = Zd;
}

class sme_tile_to_vector_inst<bit Q, bits<2> sz, ZPRRegOp zpr_ty,
                              MatrixTileVectorOperand tile_ty,
                              bit is_col, Operand imm_ty, string mnemonic>
    : sme_tile_to_vector_base<Q, is_col, sz, (outs zpr_ty:$Zd),
        (ins zpr_ty:$_Zd, PPR3bAny:$Pg, tile_ty:$ZAn, MatrixIndexGPR32Op12_15:$Rv, imm_ty:$imm),
        mnemonic, "\t$Zd, $Pg/m, $ZAn[$Rv, $imm]"> {

  let Constraints = "$Zd = $_Zd";
}

multiclass sme_tile_to_vector_aliases<Instruction inst, ZPRRegOp zpr_ty,
                                      MatrixTileVectorOperand tile_ty,
                                      Operand imm_ty > {
  def : InstAlias<"mov\t$Zd, $Pg/m, $ZAn[$Rv, $imm]",
                  (inst zpr_ty:$Zd, PPR3bAny:$Pg, tile_ty:$ZAn, MatrixIndexGPR32Op12_15:$Rv, imm_ty:$imm), 1>;
}

multiclass sme_tile_to_vector_patterns<Instruction inst, ValueType zpr_vt,
                                       ValueType ppr_vt, Operand offset_ty,
                                       ComplexPattern imm2tile,
                                       ComplexPattern tileslice,
                                       SDPatternOperator op> {
  def : Pat<(zpr_vt (op (zpr_vt ZPRAny:$passthru), (ppr_vt PPR3bAny:$pg),
                        (imm2tile untyped:$tile), MatrixIndexGPR32Op12_15:$idx)),
            (inst $passthru, $pg, $tile, $idx, 0)>;
  let AddedComplexity = 1 in {
    def : Pat<(zpr_vt (op (zpr_vt ZPRAny:$passthru), (ppr_vt PPR3bAny:$pg),
                          (imm2tile untyped:$tile),
                          (i32 (tileslice MatrixIndexGPR32Op12_15:$idx,
                                          offset_ty:$imm)))),
              (inst $passthru, $pg, $tile, $idx, $imm)>;
  }
}

multiclass sme_tile_to_vector_v<string mnemonic, bit is_col> {
  def _B : sme_tile_to_vector_inst<0b0, 0b00, ZPR8, !if(is_col, TileVectorOpV8,
                                                                TileVectorOpH8),
                                   is_col, sme_elm_idx0_15, mnemonic> {
    bits<4> imm;
    let Inst{8-5} = imm;
  }
  def _H : sme_tile_to_vector_inst<0b0, 0b01, ZPR16, !if(is_col, TileVectorOpV16,
                                                                 TileVectorOpH16),
                                   is_col, sme_elm_idx0_7, mnemonic> {
    bits<1> ZAn;
    bits<3> imm;
    let Inst{8}   = ZAn;
    let Inst{7-5} = imm;
  }
  def _S : sme_tile_to_vector_inst<0b0, 0b10, ZPR32, !if(is_col, TileVectorOpV32,
                                                                 TileVectorOpH32),
                                   is_col, sme_elm_idx0_3, mnemonic> {
    bits<2> ZAn;
    bits<2> imm;
    let Inst{8-7} = ZAn;
    let Inst{6-5} = imm;
  }
  def _D : sme_tile_to_vector_inst<0b0, 0b11, ZPR64, !if(is_col, TileVectorOpV64,
                                                                 TileVectorOpH64),
                                   is_col, sme_elm_idx0_1, mnemonic> {
    bits<3> ZAn;
    bits<1> imm;
    let Inst{8-6} = ZAn;
    let Inst{5}   = imm;
  }
  def _Q : sme_tile_to_vector_inst<0b1, 0b11, ZPR128, !if(is_col, TileVectorOpV128,
                                                                  TileVectorOpH128),
                                   is_col, sme_elm_idx0_0, mnemonic> {
    bits<4> ZAn;
    let Inst{8-5} = ZAn;
  }

  defm : sme_tile_to_vector_aliases<!cast<Instruction>(NAME # _B), ZPR8,
                                    !if(is_col, TileVectorOpV8,
                                                TileVectorOpH8), sme_elm_idx0_15>;
  defm : sme_tile_to_vector_aliases<!cast<Instruction>(NAME # _H), ZPR16,
                                    !if(is_col, TileVectorOpV16,
                                                TileVectorOpH16), sme_elm_idx0_7>;
  defm : sme_tile_to_vector_aliases<!cast<Instruction>(NAME # _S), ZPR32,
                                    !if(is_col, TileVectorOpV32,
                                                TileVectorOpH32), sme_elm_idx0_3>;
  defm : sme_tile_to_vector_aliases<!cast<Instruction>(NAME # _D), ZPR64,
                                    !if(is_col, TileVectorOpV64,
                                                TileVectorOpH64), sme_elm_idx0_1>;
  defm : sme_tile_to_vector_aliases<!cast<Instruction>(NAME # _Q), ZPR128,
                                    !if(is_col, TileVectorOpV128,
                                                TileVectorOpH128), sme_elm_idx0_0>;

  defvar op = !if(is_col, int_aarch64_sme_read_vert,
                          int_aarch64_sme_read_horiz);

  defm : sme_tile_to_vector_patterns<!cast<Instruction>(NAME # _B),
                                     nxv16i8, nxv16i1, sme_elm_idx0_15,
                                     imm_to_tile8, tileslice8, op>;
  defm : sme_tile_to_vector_patterns<!cast<Instruction>(NAME # _H),
                                     nxv8i16, nxv8i1, sme_elm_idx0_7,
                                     imm_to_tile16, tileslice16, op>;
  defm : sme_tile_to_vector_patterns<!cast<Instruction>(NAME # _H),
                                     nxv8f16, nxv8i1, sme_elm_idx0_7,
                                     imm_to_tile16, tileslice16, op>;
  defm : sme_tile_to_vector_patterns<!cast<Instruction>(NAME # _H),
                                     nxv8bf16, nxv8i1, sme_elm_idx0_7,
                                     imm_to_tile16, tileslice16, op>;
  defm : sme_tile_to_vector_patterns<!cast<Instruction>(NAME # _S),
                                     nxv4i32, nxv4i1, sme_elm_idx0_3,
                                     imm_to_tile32, tileslice32, op>;
  defm : sme_tile_to_vector_patterns<!cast<Instruction>(NAME # _S),
                                     nxv4f32, nxv4i1, sme_elm_idx0_3,
                                     imm_to_tile32, tileslice32, op>;
  defm : sme_tile_to_vector_patterns<!cast<Instruction>(NAME # _D),
                                     nxv2i64, nxv2i1, sme_elm_idx0_1,
                                     imm_to_tile64, tileslice64, op>;
  defm : sme_tile_to_vector_patterns<!cast<Instruction>(NAME # _D),
                                     nxv2f64, nxv2i1, sme_elm_idx0_1,
                                     imm_to_tile64, tileslice64, op>;

  defvar opq = !if(is_col, int_aarch64_sme_readq_vert,
                           int_aarch64_sme_readq_horiz);

  defm : sme_tile_to_vector_patterns<!cast<Instruction>(NAME # _Q),
                                     nxv16i8, nxv16i1, sme_elm_idx0_0,
                                     imm_to_tile128, tileslice128, opq>;
  defm : sme_tile_to_vector_patterns<!cast<Instruction>(NAME # _Q),
                                     nxv8i16, nxv8i1, sme_elm_idx0_0,
                                     imm_to_tile128, tileslice128, opq>;
  defm : sme_tile_to_vector_patterns<!cast<Instruction>(NAME # _Q),
                                     nxv8f16, nxv8i1, sme_elm_idx0_0,
                                     imm_to_tile128, tileslice128, opq>;
  defm : sme_tile_to_vector_patterns<!cast<Instruction>(NAME # _Q),
                                     nxv8bf16, nxv8i1, sme_elm_idx0_0,
                                     imm_to_tile128, tileslice128, opq>;
  defm : sme_tile_to_vector_patterns<!cast<Instruction>(NAME # _Q),
                                     nxv4i32, nxv4i1, sme_elm_idx0_0,
                                     imm_to_tile128, tileslice128, opq>;
  defm : sme_tile_to_vector_patterns<!cast<Instruction>(NAME # _Q),
                                     nxv4f32, nxv4i1, sme_elm_idx0_0,
                                     imm_to_tile128, tileslice128, opq>;
  defm : sme_tile_to_vector_patterns<!cast<Instruction>(NAME # _Q),
                                     nxv2i64, nxv2i1, sme_elm_idx0_0,
                                     imm_to_tile128, tileslice128, opq>;
  defm : sme_tile_to_vector_patterns<!cast<Instruction>(NAME # _Q),
                                     nxv2f64, nxv2i1, sme_elm_idx0_0,
                                     imm_to_tile128, tileslice128, opq>;
}

multiclass sme_tile_to_vector<string mnemonic> {
  defm _H : sme_tile_to_vector_v<mnemonic, /*is_col=*/0b0>;
  defm _V : sme_tile_to_vector_v<mnemonic, /*is_col=*/0b1>;
}

//===----------------------------------------------------------------------===//
// SME Zero
//===----------------------------------------------------------------------===//

// NOTE: This definition isn't really correct because there are outputs, i.e.
// the tile registers being zeroed. We fix this up in a custom inserter that
// marks the appropriate registers as being implicitly defined.
class sme_zero_inst<string mnemonic>
    : I<(outs), (ins MatrixTileList:$imm),
        mnemonic, "\t$imm", "", []>, Sched<[]> {
  bits<8> imm;
  let Inst{31-8} = 0b110000000000100000000000;
  let Inst{7-0}  = imm;
}

multiclass sme_zero<string mnemonic> {
  def NAME : sme_zero_inst<mnemonic>;

  def : InstAlias<"zero\t\\{za\\}", (!cast<Instruction>(NAME) 0b11111111), 1>;
  def : InstAlias<"zero\t\\{za0.h\\}", (!cast<Instruction>(NAME) 0b01010101), 1>;
  def : InstAlias<"zero\t\\{za1.h\\}", (!cast<Instruction>(NAME) 0b10101010), 1>;
  def : InstAlias<"zero\t\\{za0.s\\}", (!cast<Instruction>(NAME) 0b00010001), 1>;
  def : InstAlias<"zero\t\\{za1.s\\}", (!cast<Instruction>(NAME) 0b00100010), 1>;
  def : InstAlias<"zero\t\\{za2.s\\}", (!cast<Instruction>(NAME) 0b01000100), 1>;
  def : InstAlias<"zero\t\\{za3.s\\}", (!cast<Instruction>(NAME) 0b10001000), 1>;
  def : InstAlias<"zero\t\\{za0.s,za1.s\\}", (!cast<Instruction>(NAME) 0b00110011), 1>;
  def : InstAlias<"zero\t\\{za0.s,za3.s\\}", (!cast<Instruction>(NAME) 0b10011001), 1>;
  def : InstAlias<"zero\t\\{za1.s,za2.s\\}", (!cast<Instruction>(NAME) 0b01100110), 1>;
  def : InstAlias<"zero\t\\{za2.s,za3.s\\}", (!cast<Instruction>(NAME) 0b11001100), 1>;
  def : InstAlias<"zero\t\\{za0.s,za1.s,za2.s\\}", (!cast<Instruction>(NAME) 0b01110111), 1>;
  def : InstAlias<"zero\t\\{za0.s,za1.s,za3.s\\}", (!cast<Instruction>(NAME) 0b10111011), 1>;
  def : InstAlias<"zero\t\\{za0.s,za2.s,za3.s\\}", (!cast<Instruction>(NAME) 0b11011101), 1>;
  def : InstAlias<"zero\t\\{za1.s,za2.s,za3.s\\}", (!cast<Instruction>(NAME) 0b11101110), 1>;

  def NAME # _PSEUDO : Pseudo<(outs), (ins i32imm:$tilelist), []>,
      Sched<[]> {
    // Translated to the actual instructions in AArch64ISelLowering.cpp
    let usesCustomInserter = 1;
  }

  def : Pat<(int_aarch64_sme_zero timm32_0_255:$imm),
            (!cast<Instruction>(NAME # _PSEUDO) timm32_0_255:$imm)>;
}

//===----------------------------------------------------------------------===//
// SVE2 Instructions
//===----------------------------------------------------------------------===//

class sve2_int_perm_revd<string asm>
    : I<(outs ZPR128:$Zd), (ins ZPR128:$_Zd, PPR3bAny:$Pg, ZPR128:$Zn),
        asm, "\t$Zd, $Pg/m, $Zn", "", []>,
      Sched<[]> {
  bits<5> Zd;
  bits<3> Pg;
  bits<5> Zn;
  let Inst{31-24} = 0b00000101;
  let Inst{23-22} = 0b00; // size
  let Inst{21-13} = 0b101110100;
  let Inst{12-10} = Pg;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zd;

  let Constraints = "$Zd = $_Zd";
  let DestructiveInstType = DestructiveUnary;
  let ElementSize = ZPR128.ElementSize;
}

multiclass sve2_int_perm_revd<string asm, SDPatternOperator op> {
  def NAME : sve2_int_perm_revd<asm>;

  def : SVE_1_Op_Passthru_Pat<nxv16i8, op, nxv16i1, nxv16i8, !cast<Instruction>(NAME)>;
  def : SVE_1_Op_Passthru_Pat<nxv8i16, op, nxv8i1,  nxv8i16, !cast<Instruction>(NAME)>;
  def : SVE_1_Op_Passthru_Pat<nxv4i32, op, nxv4i1,  nxv4i32, !cast<Instruction>(NAME)>;
  def : SVE_1_Op_Passthru_Pat<nxv2i64, op, nxv2i1,  nxv2i64, !cast<Instruction>(NAME)>;

  def : SVE_1_Op_Passthru_Pat<nxv8bf16, op, nxv8i1, nxv8bf16, !cast<Instruction>(NAME)>;
  def : SVE_1_Op_Passthru_Pat<nxv8f16,  op, nxv8i1, nxv8f16,  !cast<Instruction>(NAME)>;
  def : SVE_1_Op_Passthru_Pat<nxv4f32,  op, nxv4i1, nxv4f32,  !cast<Instruction>(NAME)>;
  def : SVE_1_Op_Passthru_Pat<nxv2f64,  op, nxv2i1, nxv2f64,  !cast<Instruction>(NAME)>;

}

class sve2_clamp<string asm, bits<2> sz, bit U, ZPRRegOp zpr_ty>
    : I<(outs zpr_ty:$Zd), (ins zpr_ty:$_Zd, zpr_ty:$Zn, zpr_ty:$Zm),
        asm, "\t$Zd, $Zn, $Zm", "", []>,
      Sched<[]> {
  bits<5> Zm;
  bits<5> Zn;
  bits<5> Zd;
  let Inst{31-24} = 0b01000100;
  let Inst{23-22} = sz;
  let Inst{21}    = 0b0;
  let Inst{20-16} = Zm;
  let Inst{15-11} = 0b11000;
  let Inst{10}    = U;
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zd;

  let Constraints = "$Zd = $_Zd";
  let DestructiveInstType = DestructiveOther;
  let ElementSize = zpr_ty.ElementSize;
}

multiclass sve2_clamp<string asm, bit U, SDPatternOperator op> {
  def _B : sve2_clamp<asm, 0b00, U, ZPR8>;
  def _H : sve2_clamp<asm, 0b01, U, ZPR16>;
  def _S : sve2_clamp<asm, 0b10, U, ZPR32>;
  def _D : sve2_clamp<asm, 0b11, U, ZPR64>;

  def : SVE_3_Op_Pat<nxv16i8, op, nxv16i8, nxv16i8, nxv16i8, !cast<Instruction>(NAME # _B)>;
  def : SVE_3_Op_Pat<nxv8i16, op, nxv8i16, nxv8i16, nxv8i16, !cast<Instruction>(NAME # _H)>;
  def : SVE_3_Op_Pat<nxv4i32, op, nxv4i32, nxv4i32, nxv4i32, !cast<Instruction>(NAME # _S)>;
  def : SVE_3_Op_Pat<nxv2i64, op, nxv2i64, nxv2i64, nxv2i64, !cast<Instruction>(NAME # _D)>;
}

class sve2_int_perm_sel_p<string asm, PPRRegOp ppr_ty, Operand imm_ty>
    : I<(outs PPRorPNRAny:$Pd), (ins PPRorPNRAny:$Pn, ppr_ty:$Pm,
                            MatrixIndexGPR32Op12_15:$Rv, imm_ty:$imm),
        asm, "\t$Pd, $Pn, $Pm[$Rv, $imm]", "", []>,
      Sched<[]> {
  bits<2> Rv;
  bits<4> Pn;
  bits<4> Pm;
  bits<4> Pd;
  let Inst{31-24} = 0b00100101;
  let Inst{21}    = 0b1;
  let Inst{17-16} = Rv;
  let Inst{15-14} = 0b01;
  let Inst{13-10} = Pn;
  let Inst{9}     = 0b0;
  let Inst{8-5}   = Pm;
  let Inst{4}     = 0b0;
  let Inst{3-0}   = Pd;
}

multiclass sve2_int_perm_sel_p<string asm, SDPatternOperator op> {
  def _B : sve2_int_perm_sel_p<asm, PPR8, sme_elm_idx0_15> {
    bits<4> imm;
    let Inst{23-22} = imm{3-2};
    let Inst{20-19} = imm{1-0};
    let Inst{18}    = 0b1;
  }
  def _H : sve2_int_perm_sel_p<asm, PPR16, sme_elm_idx0_7> {
    bits<3> imm;
    let Inst{23-22} = imm{2-1};
    let Inst{20}    = imm{0};
    let Inst{19-18} = 0b10;
  }
  def _S : sve2_int_perm_sel_p<asm, PPR32, sme_elm_idx0_3> {
    bits<2> imm;
    let Inst{23-22} = imm{1-0};
    let Inst{20-18} = 0b100;
  }
  def _D : sve2_int_perm_sel_p<asm, PPR64, sme_elm_idx0_1> {
    bits<1> imm;
    let Inst{23}    = imm;
    let Inst{22}    = 0b1;
    let Inst{20-18} = 0b000;
  }

  def : Pat<(nxv16i1 (op (nxv16i1 PPRAny:$Pn), (nxv16i1 PPR8:$Pm),
             MatrixIndexGPR32Op12_15:$idx)),
            (!cast<Instruction>(NAME # _B) $Pn, $Pm, $idx, 0)>;
  def : Pat<(nxv16i1 (op (nxv16i1 PPRAny:$Pn), (nxv8i1 PPR16:$Pm),
             MatrixIndexGPR32Op12_15:$idx)),
            (!cast<Instruction>(NAME # _H) $Pn, $Pm, $idx, 0)>;
  def : Pat<(nxv16i1 (op (nxv16i1 PPRAny:$Pn), (nxv4i1 PPR32:$Pm),
             MatrixIndexGPR32Op12_15:$idx)),
            (!cast<Instruction>(NAME # _S) $Pn, $Pm, $idx, 0)>;
  def : Pat<(nxv16i1 (op (nxv16i1 PPRAny:$Pn), (nxv2i1 PPR64:$Pm),
             MatrixIndexGPR32Op12_15:$idx)),
            (!cast<Instruction>(NAME # _D) $Pn, $Pm, $idx, 0)>;

  let AddedComplexity = 1 in {
    def : Pat<(nxv16i1 (op (nxv16i1 PPRAny:$Pn), (nxv16i1 PPR8:$Pm),
               (i32 (tileslice8 MatrixIndexGPR32Op12_15:$idx, sme_elm_idx0_15:$imm)))),
              (!cast<Instruction>(NAME # _B) $Pn, $Pm, $idx, $imm)>;
    def : Pat<(nxv16i1 (op (nxv16i1 PPRAny:$Pn), (nxv8i1 PPR16:$Pm),
               (i32 (tileslice16 MatrixIndexGPR32Op12_15:$idx, sme_elm_idx0_7:$imm)))),
              (!cast<Instruction>(NAME # _H) $Pn, $Pm, $idx, $imm)>;
    def : Pat<(nxv16i1 (op (nxv16i1 PPRAny:$Pn), (nxv4i1 PPR32:$Pm),
               (i32 (tileslice32 MatrixIndexGPR32Op12_15:$idx, sme_elm_idx0_3:$imm)))),
              (!cast<Instruction>(NAME # _S) $Pn, $Pm, $idx, $imm)>;
    def : Pat<(nxv16i1 (op (nxv16i1 PPRAny:$Pn), (nxv2i1 PPR64:$Pm),
               (i32 (tileslice64 MatrixIndexGPR32Op12_15:$idx, sme_elm_idx0_1:$imm)))),
              (!cast<Instruction>(NAME # _D) $Pn, $Pm, $idx, $imm)>;
  }
}

//===----------------------------------------------------------------------===//
// SME2 Instructions
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// SME2 single-multi ternary int/fp, two/four registers

class sme2_dot_mla_add_sub_array_vg24_single<bits<7> op,
                                         MatrixOperand matrix_ty,
                                         RegisterOperand multi_vector_ty,
                                         ZPRRegOp zpr_ty,
                                         string mnemonic>
   : I<(outs matrix_ty:$ZAd),
       (ins  matrix_ty:$_ZAd, MatrixIndexGPR32Op8_11:$Rv,
       sme_elm_idx0_7:$imm3, multi_vector_ty:$Zn, zpr_ty:$Zm),
       mnemonic,"\t$ZAd[$Rv, $imm3, " # !if(op{5}, "vgx4", "vgx2") # "], $Zn, $Zm",
       "", []> , Sched<[]> {
  bits<4> Zm;
  bits<5> Zn;
  bits<2> Rv;
  bits<3> imm3;
  let Inst{31-23} = 0b110000010;
  let Inst{22}    = op{6}; //sz
  let Inst{21}    = 0b1;
  let Inst{20}    = op{5}; //vgx4
  let Inst{19-16} = Zm;
  let Inst{15}    = 0b0;
  let Inst{14-13} = Rv;
  let Inst{12-10} = op{4-2};
  let Inst{9-5}   = Zn;
  let Inst{4-3}   = op{1-0};
  let Inst{2-0}   = imm3;
  let Constraints = "$ZAd = $_ZAd";
}

multiclass sme2_dot_mla_add_sub_array_vg24_single<string mnemonic, bits<7> op,
                                              MatrixOperand matrix_ty,
                                              RegisterOperand multi_vector_ty,
                                              ZPRRegOp zpr_ty>{
  def NAME: sme2_dot_mla_add_sub_array_vg24_single<op, matrix_ty, multi_vector_ty, zpr_ty, mnemonic>, SMEPseudo2Instr<NAME, 1>;

  def : InstAlias<mnemonic # "\t$ZAd[$Rv, $imm3], $Zn, $Zm",
                 (!cast<Instruction>(NAME) matrix_ty:$ZAd,  MatrixIndexGPR32Op8_11:$Rv, sme_elm_idx0_7:$imm3, multi_vector_ty:$Zn, zpr_ty:$Zm), 0>;
}

multiclass sme2_dot_mla_add_sub_array_vg2_single<string mnemonic, bits<7> op,
                                              MatrixOperand matrix_ty,
                                              RegisterOperand multi_vector_ty,
                                              ZPRRegOp zpr_ty, ValueType vty, SDPatternOperator intrinsic>{
  def NAME: sme2_dot_mla_add_sub_array_vg24_single<op, matrix_ty, multi_vector_ty, zpr_ty, mnemonic>, SMEPseudo2Instr<NAME, 1>;

  def : InstAlias<mnemonic # "\t$ZAd[$Rv, $imm3], $Zn, $Zm",
                 (!cast<Instruction>(NAME) matrix_ty:$ZAd,  MatrixIndexGPR32Op8_11:$Rv, sme_elm_idx0_7:$imm3, multi_vector_ty:$Zn, zpr_ty:$Zm), 0>;

  def _PSEUDO : sme2_za_array_2op_multi_single_pseudo<NAME, sme_elm_idx0_7, multi_vector_ty, zpr_ty, SMEMatrixArray>;

  def : SME2_ZA_TwoOp_VG2_Multi_Single_Pat<NAME, intrinsic, sme_elm_idx0_7, zpr_ty, vty, tileslice16>;
}

multiclass sme2_dot_mla_add_sub_array_vg4_single<string mnemonic, bits<7> op,
                                              MatrixOperand matrix_ty,
                                              RegisterOperand multi_vector_ty,
                                              ZPRRegOp zpr_ty, ValueType vty, SDPatternOperator intrinsic>{
  def NAME: sme2_dot_mla_add_sub_array_vg24_single<op, matrix_ty, multi_vector_ty, zpr_ty, mnemonic>, SMEPseudo2Instr<NAME, 1>;

  def : InstAlias<mnemonic # "\t$ZAd[$Rv, $imm3], $Zn, $Zm",
                 (!cast<Instruction>(NAME) matrix_ty:$ZAd,  MatrixIndexGPR32Op8_11:$Rv, sme_elm_idx0_7:$imm3, multi_vector_ty:$Zn, zpr_ty:$Zm), 0>;

  def _PSEUDO : sme2_za_array_2op_multi_single_pseudo<NAME, sme_elm_idx0_7, multi_vector_ty, zpr_ty, SMEMatrixArray>;

  def : SME2_ZA_TwoOp_VG4_Multi_Single_Pat<NAME, intrinsic, sme_elm_idx0_7, zpr_ty, vty, tileslice16>;
}

//===----------------------------------------------------------------------===//
// SME2 multiple vectors ternary INT/FP  two and four registers
class sme2_dot_mla_add_sub_array_vg2_multi<bits<7> op,
                                       MatrixOperand matrix_ty,
                                       RegisterOperand multi_vector_ty,
                                       string mnemonic>
   : I<(outs matrix_ty:$ZAd),
       (ins  matrix_ty:$_ZAd, MatrixIndexGPR32Op8_11:$Rv,
       sme_elm_idx0_7:$imm3, multi_vector_ty:$Zn, multi_vector_ty:$Zm),
       mnemonic, "\t$ZAd[$Rv, $imm3, vgx2], $Zn, $Zm",
       "", []>, Sched<[]>{
  bits<4> Zm;
  bits<4> Zn;
  bits<2> Rv;
  bits<3> imm3;
  let Inst{31-23} = 0b110000011;
  let Inst{22}    = op{6}; //sz
  let Inst{21}    = 0b1;
  let Inst{20-17} = Zm;
  let Inst{16-15} = 0b00;
  let Inst{14-13} = Rv;
  let Inst{12-10} = op{5-3};
  let Inst{9-6}   = Zn;
  let Inst{5-3}   = op{2-0};
  let Inst{2-0}   = imm3;
  let Constraints = "$ZAd = $_ZAd";
}

multiclass sme2_dot_mla_add_sub_array_vg2_multi<string mnemonic, bits<7> op,
                                            MatrixOperand  matrix_ty,
                                            RegisterOperand multi_vector_ty, ValueType zpr_ty,
                                            SDPatternOperator intrinsic> {
  def NAME : sme2_dot_mla_add_sub_array_vg2_multi<op, matrix_ty, multi_vector_ty, mnemonic>, SMEPseudo2Instr<NAME, 1>;

  def _PSEUDO : sme2_za_array_2op_multi_multi_pseudo<NAME, sme_elm_idx0_7, multi_vector_ty, SMEMatrixArray>;

  def : SME2_ZA_TwoOp_VG2_Multi_Multi_Pat<NAME, intrinsic, sme_elm_idx0_7, zpr_ty, tileslice16>;

  def : InstAlias<mnemonic # "\t$ZAd[$Rv, $imm3], $Zn, $Zm",
                  (!cast<Instruction>(NAME) matrix_ty:$ZAd,  MatrixIndexGPR32Op8_11:$Rv, sme_elm_idx0_7:$imm3, multi_vector_ty:$Zn, multi_vector_ty:$Zm), 0>;
}

class sme2_dot_mla_add_sub_array_vg4_multi<bits<7> op,
                                            MatrixOperand matrix_ty,
                                            RegisterOperand multi_vector_ty,
                                            string mnemonic>
   : I<(outs matrix_ty:$ZAd),
       (ins  matrix_ty:$_ZAd, MatrixIndexGPR32Op8_11:$Rv,
        sme_elm_idx0_7:$imm3, multi_vector_ty:$Zn, multi_vector_ty:$Zm),
        mnemonic, "\t$ZAd[$Rv, $imm3, vgx4], $Zn, $Zm",
        "", []>, Sched<[]>{
  bits<3> Zm;
  bits<3> Zn;
  bits<2> Rv;
  bits<3> imm3;
  let Inst{31-23} = 0b110000011;
  let Inst{22}    = op{6}; //sz
  let Inst{21}    = 0b1;
  let Inst{20-18} = Zm;
  let Inst{17-15} = 0b010;
  let Inst{14-13} = Rv;
  let Inst{12-10} = op{5-3};
  let Inst{9-7}   = Zn;
  let Inst{6}     = 0b0;
  let Inst{5-3}   = op{2-0};
  let Inst{2-0}   = imm3;
  let Constraints = "$ZAd = $_ZAd";
}

multiclass sme2_dot_mla_add_sub_array_vg4_multi<string mnemonic, bits<7> op,
                                            MatrixOperand  matrix_ty,
                                            RegisterOperand multi_vector_ty,
                                            ValueType zpr_ty, SDPatternOperator intrinsic>{
  def NAME : sme2_dot_mla_add_sub_array_vg4_multi<op, matrix_ty, multi_vector_ty, mnemonic>, SMEPseudo2Instr<NAME, 1>;

  def _PSEUDO : sme2_za_array_2op_multi_multi_pseudo<NAME, sme_elm_idx0_7, multi_vector_ty, SMEMatrixArray>;

  def : SME2_ZA_TwoOp_VG4_Multi_Multi_Pat<NAME, intrinsic, sme_elm_idx0_7, zpr_ty, tileslice16>;

  def : InstAlias<mnemonic # "\t$ZAd[$Rv, $imm3], $Zn, $Zm",
                 (!cast<Instruction>(NAME) matrix_ty:$ZAd,  MatrixIndexGPR32Op8_11:$Rv, sme_elm_idx0_7:$imm3, multi_vector_ty:$Zn, multi_vector_ty:$Zm), 0>;
}

//===----------------------------------------------------------------------===//
// SME2 multiple vectors binary two or four  registers

class sme2_multivec_accum_add_sub<string mnemonic, bit sz, bit vg4, bits<3> op,
                                  MatrixOperand matrix_ty,
                                  RegisterOperand vector_ty>
    : I<(outs matrix_ty:$ZAdn),
        (ins matrix_ty:$_ZAdn, MatrixIndexGPR32Op8_11:$Rv, sme_elm_idx0_7:$imm3, vector_ty:$Zm),
        mnemonic, "\t$ZAdn[$Rv, $imm3, " # !if(vg4, "vgx4", "vgx2") # "], $Zm",
        "", []>, Sched<[]> {
  bits<2> Rv;
  bits<3> imm3;
  let Inst{31-23} = 0b110000011;
  let Inst{22}    = sz;
  let Inst{21-19} = 0b100;
  let Inst{18}    = op{2};
  let Inst{17}    = 0b0;
  let Inst{16}    = vg4;
  let Inst{15}    = 0b0;
  let Inst{14-13} = Rv;
  let Inst{12-10} = 0b111;
  let Inst{5}     = 0b0;
  let Inst{4-3}   = op{1-0};
  let Inst{2-0}   = imm3;

  let Constraints = "$ZAdn = $_ZAdn";
}

class sme2_multivec_accum_add_sub_vg2<string mnemonic, bit sz, bits<3> op,
                                      MatrixOperand matrix_ty,
                                      RegisterOperand vector_ty>
    : sme2_multivec_accum_add_sub<mnemonic, sz, 0b0, op, matrix_ty, vector_ty> {
  bits<4> Zm;
  let Inst{9-6} = Zm;
}


multiclass sme2_multivec_accum_add_sub_vg2<string mnemonic, bits<4> op,
                                           MatrixOperand matrix_ty,
                                           RegisterOperand vector_ty,
                                           ValueType vty,
                                           SDPatternOperator intrinsic> {
  def NAME : sme2_multivec_accum_add_sub_vg2<mnemonic, op{3}, op{2-0}, matrix_ty, vector_ty>,
                                             SMEPseudo2Instr<NAME, 1>;
  def : InstAlias<mnemonic # "\t$ZAdn[$Rv, $imm3], $Zm",
  (!cast<Instruction>(NAME) matrix_ty:$ZAdn,  MatrixIndexGPR32Op8_11:$Rv, sme_elm_idx0_7:$imm3, vector_ty:$Zm), 0>;

  def _PSEUDO : sme2_move_to_za_pseudo<NAME, sme_elm_idx0_7, vector_ty, SMEMatrixArray>;
  def : SME2_ZA_VG1x2_Multi_Pat<NAME, intrinsic, vty, sme_elm_idx0_7, tileslice16>;
}

class sme2_multivec_accum_add_sub_vg4<string mnemonic, bit sz, bits<3> op,
                                      MatrixOperand matrix_ty,
                                      RegisterOperand vector_ty>
    : sme2_multivec_accum_add_sub<mnemonic, sz, 0b1, op, matrix_ty, vector_ty> {
  bits<3> Zm;
  let Inst{9-7} = Zm;
  let Inst{6}   = 0b0;
}

multiclass sme2_multivec_accum_add_sub_vg4<string mnemonic, bits<4> op,
                                           MatrixOperand matrix_ty,
                                           RegisterOperand vector_ty,
                                           ValueType vty,
                                           SDPatternOperator intrinsic> {
  def NAME : sme2_multivec_accum_add_sub_vg4<mnemonic, op{3}, op{2-0}, matrix_ty, vector_ty>,
                                             SMEPseudo2Instr<NAME, 1>;
  def : InstAlias<mnemonic # "\t$ZAdn[$Rv, $imm3], $Zm",
  (!cast<Instruction>(NAME) matrix_ty:$ZAdn,  MatrixIndexGPR32Op8_11:$Rv, sme_elm_idx0_7:$imm3, vector_ty:$Zm), 0>;

  def _PSEUDO : sme2_move_to_za_pseudo<NAME, sme_elm_idx0_7, vector_ty, SMEMatrixArray>;
  def : SME2_ZA_VG1x4_Multi_Pat<NAME, intrinsic, vty, sme_elm_idx0_7, tileslice16>;
}

//===----------------------------------------------------------------------===//
// SME2 Multi-vector - Multiple and Single SVE Destructive
// Two and Four registers

class sme2_sve_destructive_vector_vg2_single<bits<2> sz, bits<7> op,
                                             RegisterOperand vector_ty,
                                             ZPRRegOp zpr_ty,
                                             string mnemonic>
    : I<(outs vector_ty:$Zdn), (ins vector_ty:$_Zdn, zpr_ty:$Zm),
        mnemonic, "\t$Zdn, $_Zdn, $Zm",
        "", []>, Sched<[]> {
  bits<4> Zm;
  bits<4> Zdn;
  let Inst{31-24} = 0b11000001;
  let Inst{23-22} = sz;
  let Inst{21-20} = 0b10;
  let Inst{19-16} = Zm;
  let Inst{15-11} = 0b10100;
  let Inst{10-5}  = op{6-1};
  let Inst{4-1}   = Zdn;
  let Inst{0}     = op{0};

  let Constraints = "$Zdn = $_Zdn";
}

multiclass sme2_fp_sve_destructive_vector_vg2_single<string mnemonic, bits<7> op> {
  def _H : sme2_sve_destructive_vector_vg2_single<0b01, op, ZZ_h_mul_r, ZPR4b16, mnemonic>;
  def _S : sme2_sve_destructive_vector_vg2_single<0b10, op, ZZ_s_mul_r, ZPR4b32, mnemonic>;
  def _D : sme2_sve_destructive_vector_vg2_single<0b11, op, ZZ_d_mul_r, ZPR4b64, mnemonic>;
}

multiclass sme2_int_sve_destructive_vector_vg2_single<string mnemonic, bits<7> op> {
  def _B : sme2_sve_destructive_vector_vg2_single<0b00, op, ZZ_b_mul_r, ZPR4b8, mnemonic>;
  def _H : sme2_sve_destructive_vector_vg2_single<0b01, op, ZZ_h_mul_r, ZPR4b16, mnemonic>;
  def _S : sme2_sve_destructive_vector_vg2_single<0b10, op, ZZ_s_mul_r, ZPR4b32, mnemonic>;
  def _D : sme2_sve_destructive_vector_vg2_single<0b11, op, ZZ_d_mul_r, ZPR4b64, mnemonic>;
}

// SME2.1 fmax/fmin instructions.
multiclass sme2p1_bf_max_min_vector_vg2_single<string mnemonic, bits<7>op> {
  def _H : sme2_sve_destructive_vector_vg2_single<0b00, op, ZZ_h_mul_r,
                                                  ZPR4b16, mnemonic>;
}

class sme2_sve_destructive_vector_vg4_single<bits<2> sz, bits<7> op,
                                             RegisterOperand vector_ty,
                                             ZPRRegOp zpr_ty,
                                             string mnemonic>
    : I<(outs vector_ty:$Zdn), (ins vector_ty:$_Zdn, zpr_ty:$Zm),
        mnemonic, "\t$Zdn, $_Zdn, $Zm",
        "", []>, Sched<[]> {
  bits<4> Zm;
  bits<3> Zdn;
  let Inst{31-24} = 0b11000001;
  let Inst{23-22} = sz;
  let Inst{21-20} = 0b10;
  let Inst{19-16} = Zm;
  let Inst{15-11} = 0b10101;
  let Inst{10-5}  = op{6-1};
  let Inst{4-2}   = Zdn;
  let Inst{1}     = 0b0;
  let Inst{0}     = op{0};

  let Constraints = "$Zdn = $_Zdn";
}

multiclass sme2_fp_sve_destructive_vector_vg4_single<string mnemonic, bits<7> op> {
  def _H : sme2_sve_destructive_vector_vg4_single<0b01, op, ZZZZ_h_mul_r, ZPR4b16, mnemonic>;
  def _S : sme2_sve_destructive_vector_vg4_single<0b10, op, ZZZZ_s_mul_r, ZPR4b32, mnemonic>;
  def _D : sme2_sve_destructive_vector_vg4_single<0b11, op, ZZZZ_d_mul_r, ZPR4b64, mnemonic>;
}

multiclass sme2_int_sve_destructive_vector_vg4_single<string mnemonic, bits<7> op> {
  def _B : sme2_sve_destructive_vector_vg4_single<0b00, op, ZZZZ_b_mul_r, ZPR4b8, mnemonic>;
  def _H : sme2_sve_destructive_vector_vg4_single<0b01, op, ZZZZ_h_mul_r, ZPR4b16, mnemonic>;
  def _S : sme2_sve_destructive_vector_vg4_single<0b10, op, ZZZZ_s_mul_r, ZPR4b32, mnemonic>;
  def _D : sme2_sve_destructive_vector_vg4_single<0b11, op, ZZZZ_d_mul_r, ZPR4b64, mnemonic>;
}

// SME2.1 fmax/fmin instructions.
multiclass sme2p1_bf_max_min_vector_vg4_single<string mnemonic, bits<7>op> {
  def _H : sme2_sve_destructive_vector_vg4_single<0b00, op, ZZZZ_h_mul_r,
                                                  ZPR4b16, mnemonic>;
}

class sme2_sve_destructive_vector_vg2_multi<bits<2> sz, bits<7> op,
                                            RegisterOperand vector_ty,
                                            string mnemonic>
    : I<(outs vector_ty:$Zdn), (ins vector_ty:$_Zdn, vector_ty:$Zm),
        mnemonic, "\t$Zdn, $_Zdn, $Zm",
        "", []>, Sched<[]> {
  bits<4> Zm;
  bits<4> Zdn;
  let Inst{31-24} = 0b11000001;
  let Inst{23-22} = sz;
  let Inst{21}    = 0b1;
  let Inst{20-17} = Zm;
  let Inst{16-11} = 0b010110;
  let Inst{10-5}  = op{6-1};
  let Inst{4-1}   = Zdn;
  let Inst{0}     = op{0};

  let Constraints = "$Zdn = $_Zdn";
}

multiclass sme2_fp_sve_destructive_vector_vg2_multi<string mnemonic, bits<7> op> {
  def _H : sme2_sve_destructive_vector_vg2_multi<0b01, op, ZZ_h_mul_r, mnemonic>;
  def _S : sme2_sve_destructive_vector_vg2_multi<0b10, op, ZZ_s_mul_r, mnemonic>;
  def _D : sme2_sve_destructive_vector_vg2_multi<0b11, op, ZZ_d_mul_r, mnemonic>;
}

multiclass sme2_int_sve_destructive_vector_vg2_multi<string mnemonic, bits<7> op> {
  def _B : sme2_sve_destructive_vector_vg2_multi<0b00, op, ZZ_b_mul_r, mnemonic>;
  def _H : sme2_sve_destructive_vector_vg2_multi<0b01, op, ZZ_h_mul_r, mnemonic>;
  def _S : sme2_sve_destructive_vector_vg2_multi<0b10, op, ZZ_s_mul_r, mnemonic>;
  def _D : sme2_sve_destructive_vector_vg2_multi<0b11, op, ZZ_d_mul_r, mnemonic>;
}

// SME2.1 fmax/fmin instructions.
multiclass sme2p1_bf_max_min_vector_vg2_multi<string mnemonic, bits<7>op> {
  def _H : sme2_sve_destructive_vector_vg2_multi<0b00, op, ZZ_h_mul_r,
                                                 mnemonic>;
}

class sme2_sve_destructive_vector_vg4_multi<bits<2> sz, bits<7> op,
                                            RegisterOperand vector_ty,
                                            string mnemonic>
    : I<(outs vector_ty:$Zdn), (ins vector_ty:$_Zdn, vector_ty:$Zm),
        mnemonic, "\t$Zdn, $_Zdn, $Zm",
        "", []>, Sched<[]> {
  bits<3> Zm;
  bits<3> Zdn;
  let Inst{31-24} = 0b11000001;
  let Inst{23-22} = sz;
  let Inst{21}    = 0b1;
  let Inst{20-18} = Zm;
  let Inst{17-11} = 0b0010111;
  let Inst{10-5}  = op{6-1};
  let Inst{4-2}   = Zdn;
  let Inst{1}     = 0b0;
  let Inst{0}     = op{0};

  let Constraints = "$Zdn = $_Zdn";
}

multiclass sme2_fp_sve_destructive_vector_vg4_multi<string mnemonic, bits<7> op> {
  def _H : sme2_sve_destructive_vector_vg4_multi<0b01, op, ZZZZ_h_mul_r, mnemonic>;
  def _S : sme2_sve_destructive_vector_vg4_multi<0b10, op, ZZZZ_s_mul_r, mnemonic>;
  def _D : sme2_sve_destructive_vector_vg4_multi<0b11, op, ZZZZ_d_mul_r, mnemonic>;
}

multiclass sme2_int_sve_destructive_vector_vg4_multi<string mnemonic, bits<7> op> {
  def _B : sme2_sve_destructive_vector_vg4_multi<0b00, op, ZZZZ_b_mul_r, mnemonic>;
  def _H : sme2_sve_destructive_vector_vg4_multi<0b01, op, ZZZZ_h_mul_r, mnemonic>;
  def _S : sme2_sve_destructive_vector_vg4_multi<0b10, op, ZZZZ_s_mul_r, mnemonic>;
  def _D : sme2_sve_destructive_vector_vg4_multi<0b11, op, ZZZZ_d_mul_r, mnemonic>;
}

// SME2.1 fmax/fmin instructions.
multiclass sme2p1_bf_max_min_vector_vg4_multi<string mnemonic, bits<7>op> {
  def _H : sme2_sve_destructive_vector_vg4_multi<0b00, op, ZZZZ_h_mul_r,
                                                 mnemonic>;
}

//===----------------------------------------------------------------------===//
// SME2 Multi-vector - Index/Single/Multi Array Vectors FMA sources

class sme2_mla_long_array_index_base<bits<2> op0, bits<2> op, Operand index_ty,
                                     RegisterOperand multi_vector_ty,
                                     string mnemonic, string vg_acronym="">
    : I<(outs MatrixOp32:$ZAda),
        (ins MatrixOp32:$_ZAda, MatrixIndexGPR32Op8_11:$Rv, index_ty:$imm, multi_vector_ty:$Zn, ZPR4b16:$Zm, VectorIndexH32b_timm:$i3),
        mnemonic, "\t$ZAda[$Rv, $imm" # !if(!eq(vg_acronym, ""), "", ", " # vg_acronym) # "], $Zn, $Zm$i3",
        "", []>, Sched<[]> {
  bits<4> Zm;
  bits<2> Rv;
  let Inst{31-24} = 0b11000001;
  let Inst{23-22} = op0;
  let Inst{21}    = 0b0;
  let Inst{20}    = !if(!eq(vg_acronym, ""), 0, 1);
  let Inst{19-16} = Zm;
  let Inst{14-13} = Rv;
  let Inst{12}    = 0b1;
  let Inst{4-3}   = op;

  let Constraints = "$ZAda = $_ZAda";
}

multiclass sme2_mla_long_array_index<string mnemonic, bits<2> op0, bits<2> op, ValueType zpr_ty, SDPatternOperator intrinsic> {
  def _HtoS : sme2_mla_long_array_index_base<op0, op, uimm3s2range, ZPR16,
                                          mnemonic>, SMEPseudo2Instr<NAME # _HtoS, 1> {
    bits<3> i3;
    bits<5> Zn;
    bits<3> imm;
    let Inst{15}    = i3{2};
    let Inst{11-10} = i3{1-0};
    let Inst{9-5}   = Zn;
    let Inst{2-0}   = imm;
  }

  def _HtoS_PSEUDO : sme2_za_array_2op_multi_index_pseudo<NAME # _HtoS, uimm3s2range, ZPR16, ZPR4b16, VectorIndexH32b_timm, SMEMatrixArray>;

  def : SME2_ZA_TwoOp_Multi_Index_Pat<NAME # _HtoS, intrinsic, uimm3s2range, ZPR4b16, zpr_ty, VectorIndexH32b_timm, tileslicerange3s2>;
}

class sme2_mla_long_array_vg2_index<string mnemonic, bits<2> op0, bits<2> op>
    : sme2_mla_long_array_index_base<op0, op, uimm2s2range, ZZ_h_mul_r,
                                     mnemonic, "vgx2"> {
  bits<3> i3;
  bits<4> Zn;
  bits<2> imm;
  let Inst{15}    = 0b0;
  let Inst{11-10} = i3{2-1};
  let Inst{9-6}   = Zn;
  let Inst{5}     = 0b0;
  let Inst{2}     = i3{0};
  let Inst{1-0}   = imm;
}

multiclass sme2_fp_mla_long_array_vg2_index<string mnemonic, bits<2> op, ValueType zpr_ty, SDPatternOperator intrinsic> {
  def _HtoS : sme2_mla_long_array_vg2_index<mnemonic, 0b10, op>, SMEPseudo2Instr<NAME # _HtoS, 1>;

  def _HtoS_PSEUDO : sme2_za_array_2op_multi_index_pseudo<NAME # _HtoS, uimm2s2range, ZZ_h_mul_r, ZPR4b16, VectorIndexH32b_timm, SMEMatrixArray>;

  def : SME2_ZA_TwoOp_VG2_Multi_Index_Pat<NAME # _HtoS, intrinsic, uimm2s2range, ZPR4b16, zpr_ty, VectorIndexH32b_timm, tileslicerange2s2>;

  def : InstAlias<mnemonic # "\t$ZAda[$Rv, $imm], $Zn, $Zm$i3",
                 (!cast<Instruction>(NAME #_HtoS) MatrixOp32:$ZAda,  MatrixIndexGPR32Op8_11:$Rv, uimm2s2range:$imm, ZZ_h_mul_r:$Zn, ZPR4b16:$Zm, VectorIndexH32b_timm:$i3), 0>;
}

multiclass sme2_int_mla_long_array_vg2_index<string mnemonic, bits<2> op, SDPatternOperator intrinsic> {
  def _S : sme2_mla_long_array_vg2_index<mnemonic, 0b11, op>, SMEPseudo2Instr<NAME # _S, 1>;

  def _S_PSEUDO : sme2_za_array_2op_multi_index_pseudo<NAME # _S, uimm2s2range, ZZ_h_mul_r, ZPR4b16, VectorIndexH32b_timm, SMEMatrixArray>;

  def : SME2_ZA_TwoOp_VG2_Multi_Index_Pat<NAME # _S, intrinsic, uimm2s2range, ZPR4b16, nxv8i16, VectorIndexH32b_timm, tileslicerange2s2>;

  def : InstAlias<mnemonic # "\t$ZAda[$Rv, $imm], $Zn, $Zm$i3",
                 (!cast<Instruction>(NAME #_S) MatrixOp32:$ZAda,  MatrixIndexGPR32Op8_11:$Rv, uimm2s2range:$imm, ZZ_h_mul_r:$Zn, ZPR4b16:$Zm, VectorIndexH32b_timm:$i3), 0>;
}

class sme2_mla_long_array_vg4_index<string mnemonic, bits<2> op0, bits<2> op>
    : sme2_mla_long_array_index_base<op0, op, uimm2s2range, ZZZZ_h_mul_r,
                                      mnemonic, "vgx4"> {
  bits<3> i3;
  bits<3> Zn;
  bits<2> imm;
  let Inst{15}    = 0b1;
  let Inst{11-10} = i3{2-1};
  let Inst{9-7}   = Zn;
  let Inst{6-5}   = 0b00;
  let Inst{2}     = i3{0};
  let Inst{1-0}   = imm;
}

multiclass sme2_fp_mla_long_array_vg4_index<string mnemonic, bits<2> op, ValueType zpr_ty, SDPatternOperator intrinsic> {
  def _HtoS : sme2_mla_long_array_vg4_index<mnemonic, 0b10, op>, SMEPseudo2Instr<NAME # _HtoS, 1>;

  def _HtoS_PSEUDO : sme2_za_array_2op_multi_index_pseudo<NAME # _HtoS, uimm2s2range, ZZZZ_h_mul_r, ZPR4b16, VectorIndexH32b_timm, SMEMatrixArray>;

  def : SME2_ZA_TwoOp_VG4_Multi_Index_Pat<NAME # _HtoS, intrinsic, uimm2s2range, ZPR4b16, zpr_ty, VectorIndexH32b_timm, tileslicerange2s2>;

  def : InstAlias<mnemonic # "\t$ZAda[$Rv, $imm], $Zn, $Zm$i3",
                 (!cast<Instruction>(NAME #_HtoS) MatrixOp32:$ZAda,  MatrixIndexGPR32Op8_11:$Rv, uimm2s2range:$imm, ZZZZ_h_mul_r:$Zn, ZPR4b16:$Zm, VectorIndexH32b_timm:$i3), 0>;
}

multiclass sme2_int_mla_long_array_vg4_index<string mnemonic, bits<2> op, SDPatternOperator intrinsic> {
  def _HtoS : sme2_mla_long_array_vg4_index<mnemonic, 0b11, op>, SMEPseudo2Instr<NAME # _HtoS, 1>;

  def _HtoS_PSEUDO : sme2_za_array_2op_multi_index_pseudo<NAME # _HtoS, uimm2s2range, ZZZZ_h_mul_r, ZPR4b16, VectorIndexH32b_timm, SMEMatrixArray>;

  def : SME2_ZA_TwoOp_VG4_Multi_Index_Pat<NAME # _HtoS, intrinsic, uimm2s2range, ZPR4b16, nxv8i16, VectorIndexH32b_timm, tileslicerange2s2>;

  def : InstAlias<mnemonic # "\t$ZAda[$Rv, $imm], $Zn, $Zm$i3",
                 (!cast<Instruction>(NAME #_HtoS) MatrixOp32:$ZAda,  MatrixIndexGPR32Op8_11:$Rv, uimm2s2range:$imm, ZZZZ_h_mul_r:$Zn, ZPR4b16:$Zm, VectorIndexH32b_timm:$i3), 0>;
}

class sme2_mla_long_array<bits<2>op0, bits<2> op,
                          MatrixOperand matrix_ty,
                          Operand index_ty,
                          RegisterOperand first_vector_ty,
                          RegisterOperand second_vector_ty,
                          string mnemonic, string vg_acronym="">
   : I<(outs matrix_ty:$ZAda),
       (ins  matrix_ty:$_ZAda, MatrixIndexGPR32Op8_11:$Rv,
       index_ty:$imm, first_vector_ty:$Zn, second_vector_ty:$Zm),
       mnemonic,"\t$ZAda[$Rv, $imm" # !if(!eq(vg_acronym, ""), "", ", " # vg_acronym) # "], $Zn, $Zm",
       "", []> , Sched<[]> {
  bits<2> Rv;
  let Inst{31-24} = 0b11000001;
  let Inst{23-22} = op0;
  let Inst{21}    = 0b1;
  let Inst{15}    = 0b0;
  let Inst{14-13} = Rv;
  let Inst{12-11} = 0b01;
  let Inst{10}    = !if(!eq(vg_acronym, ""), 1, 0);
  let Inst{4-3}   = op;

  let Constraints = "$ZAda = $_ZAda";
}

multiclass sme2_mla_long_array_single<string mnemonic, bits<2> op0, bits<2> op, ValueType zpr_ty, SDPatternOperator intrinsic> {
  def _HtoS : sme2_mla_long_array<op0, op, MatrixOp32, uimm3s2range, ZPR16, ZPR4b16,
                               mnemonic> , SMEPseudo2Instr<NAME # _HtoS, 1>{
    bits<4> Zm;
    bits<5> Zn;
    bits<3> imm;
    let Inst{20}    = 0b0;
    let Inst{19-16} = Zm;
    let Inst{9-5}   = Zn;
    let Inst{2-0}   = imm;
  }

  def _HtoS_PSEUDO : sme2_za_array_2op_multi_single_pseudo<NAME # _HtoS, uimm3s2range, ZPR16, ZPR4b16, SMEMatrixArray>;

  def : SME2_ZA_TwoOp_Multi_Single_Pat<NAME # _HtoS, intrinsic, uimm3s2range, ZPR4b16, zpr_ty, tileslicerange3s2>;
}

class sme2_mla_long_array_single_16b<string mnemonic>
    : sme2_mla_long_array<0b00, 0b00, MatrixOp16, uimm3s2range, ZPR8, ZPR4b8,  mnemonic> {
    bits<4> Zm;
    bits<5> Zn;
    bits<3> imm;
    let Inst{20}    = 0b1;
    let Inst{19-16} = Zm;
    let Inst{9-5}   = Zn;
    let Inst{2-0}   = imm;
}

class sme2_mla_long_array_vg24_single<bits<2> op0, bit vg4, bits<2> op, bit o2,
                                      MatrixOperand matrix_ty, RegisterOperand multi_vector_ty,
                                      ZPRRegOp zpr_ty, string mnemonic, string vg_acronym>
    : sme2_mla_long_array<op0, op, matrix_ty, uimm2s2range, multi_vector_ty, zpr_ty,
                          mnemonic, vg_acronym> {
  bits<4> Zm;
  bits<5> Zn;
  bits<2> imm;
  let Inst{20}    = vg4;
  let Inst{19-16} = Zm;
  let Inst{9-5}   = Zn;
  let Inst{2}     = o2;
  let Inst{1-0}   = imm;
}

multiclass sme2_fp_mla_long_array_vg2_single<string mnemonic, bits<3> op, MatrixOperand matrix_ty,
                                             RegisterOperand multi_vector_ty, ZPRRegOp vector_ty,
                                             ValueType zpr_ty, SDPatternOperator intrinsic> {
  def NAME : sme2_mla_long_array_vg24_single<0b00, 0b0, op{2-1}, op{0}, matrix_ty,  multi_vector_ty,
                                           vector_ty, mnemonic, "vgx2">, SMEPseudo2Instr<NAME, 1>;

  def _PSEUDO : sme2_za_array_2op_multi_single_pseudo<NAME, uimm2s2range, multi_vector_ty,
                                                        vector_ty, SMEMatrixArray>;

  def : SME2_ZA_TwoOp_VG2_Multi_Single_Pat<NAME, intrinsic, uimm2s2range, vector_ty, zpr_ty,
                                           tileslicerange2s2>;

  def : InstAlias<mnemonic # "\t$ZAda[$Rv, $imm], $Zn, $Zm",
                 (!cast<Instruction>(NAME) matrix_ty:$ZAda,  MatrixIndexGPR32Op8_11:$Rv,
                  uimm2s2range:$imm, multi_vector_ty:$Zn, vector_ty:$Zm), 0>;
}

multiclass sme2_int_mla_long_array_vg2_single<string mnemonic, bits<2> op, SDPatternOperator intrinsic> {
  def _HtoS : sme2_mla_long_array_vg24_single<0b01, 0b0, op, 0b0, MatrixOp32, ZZ_h, ZPR4b16, mnemonic,
                                             "vgx2">, SMEPseudo2Instr<NAME # _HtoS, 1>;

  def _HtoS_PSEUDO : sme2_za_array_2op_multi_single_pseudo<NAME # _HtoS, uimm2s2range, ZZ_h, ZPR4b16, SMEMatrixArray>;

  def : SME2_ZA_TwoOp_VG2_Multi_Single_Pat<NAME # _HtoS, intrinsic, uimm2s2range, ZPR4b16, nxv8i16, tileslicerange2s2>;

  def : InstAlias<mnemonic # "\t$ZAda[$Rv, $imm], $Zn, $Zm",
                 (!cast<Instruction>(NAME #_HtoS) MatrixOp32:$ZAda,  MatrixIndexGPR32Op8_11:$Rv, uimm2s2range:$imm, ZZ_h:$Zn, ZPR4b16:$Zm), 0>;
}

multiclass sme2_fp_mla_long_array_vg4_single<string mnemonic, bits<3> op, MatrixOperand matrix_ty,
                                             RegisterOperand multi_vector_ty, ZPRRegOp vector_ty,
                                             ValueType zpr_ty, SDPatternOperator intrinsic> {
  def NAME : sme2_mla_long_array_vg24_single<0b00, 0b1, op{2-1}, op{0}, matrix_ty, multi_vector_ty, 
                                             vector_ty, mnemonic, "vgx4">,
                                             SMEPseudo2Instr<NAME, 1>;

  def _PSEUDO : sme2_za_array_2op_multi_single_pseudo<NAME, uimm2s2range, multi_vector_ty, vector_ty,
                                                      SMEMatrixArray>;

  def : SME2_ZA_TwoOp_VG4_Multi_Single_Pat<NAME, intrinsic, uimm2s2range, vector_ty, zpr_ty,
                                           tileslicerange2s2>;

  def : InstAlias<mnemonic # "\t$ZAda[$Rv, $imm], $Zn, $Zm",
                 (!cast<Instruction>(NAME) matrix_ty:$ZAda, MatrixIndexGPR32Op8_11:$Rv,
                  uimm2s2range:$imm, multi_vector_ty:$Zn, vector_ty:$Zm), 0>;
}

multiclass sme2_int_mla_long_array_vg4_single<string mnemonic, bits<2> op, SDPatternOperator intrinsic> {
  def _HtoS : sme2_mla_long_array_vg24_single<0b01, 0b1, op, 0b0, MatrixOp32, ZZZZ_h, ZPR4b16,  mnemonic,
                                           "vgx4">, SMEPseudo2Instr<NAME # _HtoS, 1>;

  def _HtoS_PSEUDO : sme2_za_array_2op_multi_single_pseudo<NAME # _HtoS, uimm2s2range, ZZZZ_h, ZPR4b16, SMEMatrixArray>;

  def : SME2_ZA_TwoOp_VG4_Multi_Single_Pat<NAME # _HtoS, intrinsic, uimm2s2range, ZPR4b16, nxv8i16, tileslicerange2s2>;

  def : InstAlias<mnemonic # "\t$ZAda[$Rv, $imm], $Zn, $Zm",
                 (!cast<Instruction>(NAME #_HtoS) MatrixOp32:$ZAda,  MatrixIndexGPR32Op8_11:$Rv, uimm2s2range:$imm, ZZZZ_h:$Zn, ZPR4b16:$Zm), 0>;
}

class sme2_mla_long_array_vg2_multi<string mnemonic, bits<2> op0, bits<3> op,
                                    MatrixOperand matrix_ty, RegisterOperand multi_vector_ty>
   : sme2_mla_long_array<op0, op{1-0},  matrix_ty, uimm2s2range, multi_vector_ty, multi_vector_ty,
                        mnemonic, "vgx2"> {
  bits<4> Zm;
  bits<4> Zn;
  bits<2> imm;
  let Inst{20-17} = Zm;
  let Inst{16}    = 0b0;
  let Inst{9-6}   = Zn;
  let Inst{5}     = op{2};  // fp8
  let Inst{2}     = 0b0;
  let Inst{1-0}   = imm;
}

multiclass sme2_fp_mla_long_array_vg2_multi<string mnemonic, bits<3> op, MatrixOperand matrix_ty,
                                            RegisterOperand multi_vector_ty,
                                            ValueType zpr_ty, SDPatternOperator intrinsic> {

  def NAME : sme2_mla_long_array_vg2_multi<mnemonic, 0b10, op, matrix_ty, multi_vector_ty>,
                                           SMEPseudo2Instr<NAME, 1>;

  def _PSEUDO : sme2_za_array_2op_multi_multi_pseudo<NAME, uimm2s2range, multi_vector_ty, SMEMatrixArray>;

  def : SME2_ZA_TwoOp_VG2_Multi_Multi_Pat<NAME, intrinsic, uimm2s2range, zpr_ty, tileslicerange2s2>;

  def : InstAlias<mnemonic # "\t$ZAda[$Rv, $imm], $Zn, $Zm",
                  (!cast<Instruction>(NAME) matrix_ty:$ZAda,  MatrixIndexGPR32Op8_11:$Rv,
                  uimm2s2range:$imm, multi_vector_ty:$Zn, multi_vector_ty:$Zm), 0>;
}

multiclass sme2_int_mla_long_array_vg2_multi<string mnemonic, bits<2> op, SDPatternOperator intrinsic> {
  def _HtoS : sme2_mla_long_array_vg2_multi<mnemonic, 0b11, {0b0, op}, MatrixOp32, ZZ_h_mul_r>,
                                         SMEPseudo2Instr<NAME # _HtoS, 1>;

  def _HtoS_PSEUDO : sme2_za_array_2op_multi_multi_pseudo<NAME # _HtoS, uimm2s2range, ZZ_h_mul_r, SMEMatrixArray>;

  def : SME2_ZA_TwoOp_VG2_Multi_Multi_Pat<NAME # _HtoS, intrinsic, uimm2s2range, nxv8i16, tileslicerange2s2>;

  def : InstAlias<mnemonic # "\t$ZAda[$Rv, $imm2], $Zn, $Zm",
                 (!cast<Instruction>(NAME #_HtoS) MatrixOp32:$ZAda,  MatrixIndexGPR32Op8_11:$Rv, uimm2s2range:$imm2, ZZ_h_mul_r:$Zn, ZZ_h_mul_r:$Zm), 0>;
}

class sme2_mla_long_array_vg4_multi<string mnemonic, bits<2> op0, bits<3> op,
                                    MatrixOperand matrix_ty,
                                    RegisterOperand multi_vector_ty>
   : sme2_mla_long_array<op0, op{1-0}, matrix_ty, uimm2s2range, multi_vector_ty, multi_vector_ty,
                         mnemonic, "vgx4"> {
  bits<3> Zm;
  bits<3> Zn;
  bits<2> imm;
  let Inst{20-18} = Zm;
  let Inst{17}    = 0b0;
  let Inst{16}    = 0b1;
  let Inst{9-7}   = Zn;
  let Inst{6}     = 0b0;
  let Inst{5}     = op{2};  //fp8
  let Inst{2}     = 0b0;
  let Inst{1-0}   = imm;
}

multiclass sme2_fp_mla_long_array_vg4_multi<string mnemonic, bits<3> op, MatrixOperand matrix_ty,
                                            RegisterOperand multi_vector_ty, ValueType zpr_ty,
                                            SDPatternOperator intrinsic> {
  def NAME : sme2_mla_long_array_vg4_multi<mnemonic, 0b10, op, matrix_ty, multi_vector_ty>,
                                           SMEPseudo2Instr<NAME, 1>;

  def _PSEUDO : sme2_za_array_2op_multi_multi_pseudo<NAME, uimm2s2range, multi_vector_ty, SMEMatrixArray>;

  def : SME2_ZA_TwoOp_VG4_Multi_Multi_Pat<NAME, intrinsic, uimm2s2range, zpr_ty, tileslicerange2s2>;

  def : InstAlias<mnemonic # "\t$ZAda[$Rv, $imm], $Zn, $Zm",
                 (!cast<Instruction>(NAME) matrix_ty:$ZAda,  MatrixIndexGPR32Op8_11:$Rv,
                  uimm2s2range:$imm, multi_vector_ty:$Zn, multi_vector_ty:$Zm), 0>;
}

multiclass sme2_int_mla_long_array_vg4_multi<string mnemonic, bits<2> op, SDPatternOperator intrinsic> {
  def _HtoS : sme2_mla_long_array_vg4_multi<mnemonic, 0b11, {0b0, op}, MatrixOp32, ZZZZ_h_mul_r>,
                                            SMEPseudo2Instr<NAME # _HtoS, 1>;

  def _HtoS_PSEUDO : sme2_za_array_2op_multi_multi_pseudo<NAME # _HtoS, uimm2s2range, ZZZZ_h_mul_r, SMEMatrixArray>;

  def : SME2_ZA_TwoOp_VG4_Multi_Multi_Pat<NAME # _HtoS, intrinsic, uimm2s2range, nxv8i16, tileslicerange2s2>;

  def : InstAlias<mnemonic # "\t$ZAda[$Rv, $imm2], $Zn, $Zm",
                 (!cast<Instruction>(NAME #_HtoS) MatrixOp32:$ZAda,  MatrixIndexGPR32Op8_11:$Rv, uimm2s2range:$imm2, ZZZZ_h_mul_r:$Zn, ZZZZ_h_mul_r:$Zm), 0>;
}

//===----------------------------------------------------------------------===//
class sme2_frint_cvt_vg2_multi<bits<2>sz, bits<5>op, RegisterOperand first_ty,
                               RegisterOperand second_ty, string mnemonic>
    : I<(outs first_ty:$Zd), (ins second_ty:$Zn),
        mnemonic, "\t$Zd, $Zn", "", []>, Sched<[]> {
  bits<4> Zn;
  bits<4> Zd;
  let Inst{31-24} = 0b11000001;
  let Inst{23-22} = sz;
  let Inst{21-20} = 0b10;
  let Inst{19-16} = op{4-1};
  let Inst{15-10} = 0b111000;
  let Inst{9-6}   = Zn;
  let Inst{5}     = op{0};
  let Inst{4-1}   = Zd;
  let Inst{0}     = 0b0;
}

// SME2 multi-vec FP to int convert two registers
// SME2 multi-vec int to FP two registers
multiclass sme2_fp_cvt_vg2_multi<string mnemonic, bits<5> op> {
  def NAME : sme2_frint_cvt_vg2_multi<0b00, op, ZZ_s_mul_r, ZZ_s_mul_r, mnemonic>;
}

// SME2 multi-vec FRINT two registers
multiclass sme2_frint_vector_vg2_multi<string mnemonic, bits<5> op> {
  def _S : sme2_frint_cvt_vg2_multi<0b10, op, ZZ_s_mul_r, ZZ_s_mul_r, mnemonic>;
}

class sme2_frint_zip_cvt_vg4_multi<bits<2>sz, bits<7>op, RegisterOperand first_ty,
                                   RegisterOperand second_ty, string mnemonic>
    : I<(outs first_ty:$Zd), (ins second_ty:$Zn),
        mnemonic, "\t$Zd, $Zn", "", []>, Sched<[]> {
  bits<3> Zn;
  bits<3> Zd;
  let Inst{31-24} = 0b11000001;
  let Inst{23-22} = sz;
  let Inst{21-20} = 0b11;
  let Inst{19-16} = op{6-3};
  let Inst{15-10} = 0b111000;
  let Inst{9-7}   = Zn;
  let Inst{6-5}   = op{2-1};
  let Inst{4-2}   = Zd;
  let Inst{1}     = op{0};
  let Inst{0}     = 0b0;
}

// SME2 multi-vec FP to int convert four registers
// SME2 multi-vec int to FP four registers
multiclass sme2_fp_cvt_vg4_multi<string mnemonic, bits<7> op> {
  def NAME : sme2_frint_zip_cvt_vg4_multi<0b00, op, ZZZZ_s_mul_r, ZZZZ_s_mul_r, mnemonic>;
}

// SME2 multi-vec quadwords ZIP four registers
multiclass sme2_zip_vector_vg4<string mnemonic, bits<7> op> {
  def _B : sme2_frint_zip_cvt_vg4_multi<0b00, op, ZZZZ_b_mul_r, ZZZZ_b_mul_r,
                                        mnemonic>;
  def _H : sme2_frint_zip_cvt_vg4_multi<0b01, op, ZZZZ_h_mul_r, ZZZZ_h_mul_r,
                                        mnemonic>;
  def _S : sme2_frint_zip_cvt_vg4_multi<0b10, op, ZZZZ_s_mul_r, ZZZZ_s_mul_r,
                                        mnemonic>;
  def _D : sme2_frint_zip_cvt_vg4_multi<0b11, op, ZZZZ_d_mul_r, ZZZZ_d_mul_r,
                                         mnemonic>;
}

// SME2 multi-vec quadwords ZIP four registers
multiclass sme2_zip_vector_vg4_Q<string mnemonic, bits<7> op> {
  def NAME: sme2_frint_zip_cvt_vg4_multi<0b00, op, ZZZZ_q_mul_r, ZZZZ_q_mul_r,
                                         mnemonic>;
}

// SME2 multi-vec FRINT four registers
multiclass sme2_frint_vector_vg4_multi<string mnemonic, bits<7> op> {
  def _S :  sme2_frint_zip_cvt_vg4_multi<0b10, op, ZZZZ_s_mul_r, ZZZZ_s_mul_r,
                                         mnemonic>;
}

class sme2_cvt_vg2_single<string mnemonic, bits<5> op,
                           RegisterOperand first_ty, RegisterOperand second_ty>
    : I<(outs first_ty:$Zd), (ins second_ty:$Zn),
        mnemonic, "\t$Zd, $Zn", "", []>, Sched<[]> {
  bits<4> Zn;
  bits<5> Zd;
  let Inst{31-23} = 0b110000010;
  let Inst{22}    = op{4};
  let Inst{21-19} = 0b100;
  let Inst{18-16} = op{3-1};
  let Inst{15-10} = 0b111000;
  let Inst{9-6}   = Zn;
  let Inst{5}     = op{0};
  let Inst{4-0}   = Zd;
}

// SME2 multi-vec FP down convert two registers
// SME2 multi-vec int down convert two registers
multiclass sme2_cvt_vg2_single<string mnemonic, bits<5> op, ValueType out_vt,
                               ValueType in_vt, SDPatternOperator intrinsic> {
  def NAME :  sme2_cvt_vg2_single<mnemonic, op, ZPR16, ZZ_s_mul_r>;
  def : SVE2p1_Cvt_VG2_Pat<NAME, intrinsic, out_vt, in_vt>;
}

// SME2 multi-vec FP8 down convert two registers
multiclass sme2_fp8_cvt_vg2_single<string mnemonic, bit op> {
  def NAME :  sme2_cvt_vg2_single<mnemonic, {op, 0b1000}, ZPR8, ZZ_h_mul_r>;
}

class sme2_cvt_unpk_vector_vg2<bits<2>sz, bits<3> op, bit u, RegisterOperand first_ty,
                           RegisterOperand second_ty, string mnemonic>
    : I<(outs first_ty:$Zd), (ins second_ty:$Zn),
        mnemonic, "\t$Zd, $Zn", "", []>, Sched<[]> {
  bits<5> Zn;
  bits<4> Zd;
  let Inst{31-24} = 0b11000001;
  let Inst{23-22} = sz;
  let Inst{21-19} = 0b100;
  let Inst{18-16} = op;
  let Inst{15-10} = 0b111000;
  let Inst{9-5}   = Zn;
  let Inst{4-1}   = Zd;
  let Inst{0}     = u;
}

// SME2 multi-vec unpack two registers
multiclass sme2_unpk_vector_vg2<string mnemonic, bit u> {
  def _H : sme2_cvt_unpk_vector_vg2<0b01, 0b101, u, ZZ_h_mul_r, ZPR8, mnemonic>;
  def _S : sme2_cvt_unpk_vector_vg2<0b10, 0b101, u, ZZ_s_mul_r, ZPR16, mnemonic>;
  def _D : sme2_cvt_unpk_vector_vg2<0b11, 0b101, u, ZZ_d_mul_r, ZPR32, mnemonic>;
}

// SME2.1 multi-vec convert two registers
multiclass sme2p1_fp_cvt_vector_vg2_single<string mnemonic, bit l> {
  def _S : sme2_cvt_unpk_vector_vg2<0b10, 0b000, l, ZZ_s_mul_r, ZPR16, mnemonic>;
}

// SME2 multi-vec FP8 up convert two registers
multiclass sme2p1_fp8_cvt_vector_vg2_single<string mnemonic, bits<2> opc, bit L> {
  def _NAME : sme2_cvt_unpk_vector_vg2<opc, 0b110, L, ZZ_h_mul_r, ZPR8, mnemonic>;
}


class sme2_cvt_vg4_single<bit sz, bits<3> op, bits<4>op2,  RegisterOperand first_ty,
                          RegisterOperand second_ty, string mnemonic>
    : I<(outs first_ty:$Zd), (ins second_ty:$Zn),
        mnemonic, "\t$Zd, $Zn", "", []>, Sched<[]> {
  bits<3> Zn;
  bits<5> Zd;
  let Inst{31-24} = 0b11000001;
  let Inst{23}    = sz;
  let Inst{22}    = op{2};
  let Inst{21-20} = 0b11;
  let Inst{19-16} = op2;
  let Inst{15-10} = 0b111000;
  let Inst{9-7}   = Zn;
  let Inst{6-5}   = op{1-0};
  let Inst{4-0}   = Zd;
}

// SME2 multi-vec int down convert four registers
multiclass sme2_int_cvt_vg4_single<string mnemonic, bits<3> op, SDPatternOperator intrinsic> {
  def _StoB : sme2_cvt_vg4_single<0, op, 0b0011, ZPR8, ZZZZ_s_mul_r, mnemonic>;
  def _DtoH : sme2_cvt_vg4_single<1, op, 0b0011, ZPR16, ZZZZ_d_mul_r, mnemonic>;

  def : SME2_Cvt_VG4_Pat<NAME # _StoB, intrinsic, nxv16i8, nxv4i32>;
  def : SME2_Cvt_VG4_Pat<NAME # _DtoH, intrinsic, nxv8i16, nxv2i64>;
}

//SME2 multi-vec FP8 down convert four registers
multiclass sme2_fp8_cvt_vg4_single<string mnemonic, bit N> {
 def _NAME : sme2_cvt_vg4_single<0b0, {0b00, N}, 0b0100, ZPR8, ZZZZ_s_mul_r, mnemonic>;
}

class sme2_unpk_vector_vg4<bits<2>sz, bit u, RegisterOperand first_ty,
                           RegisterOperand second_ty, string mnemonic>
    : I<(outs first_ty:$Zd), (ins second_ty:$Zn),
        mnemonic, "\t$Zd, $Zn", "", []>, Sched<[]> {
  bits<4> Zn;
  bits<3> Zd;
  let Inst{31-24} = 0b11000001;
  let Inst{23-22} = sz;
  let Inst{21-10} = 0b110101111000;
  let Inst{9-6}   = Zn;
  let Inst{5}     = 0b0;
  let Inst{4-2}   = Zd;
  let Inst{1}     = 0b0;
  let Inst{0}     = u;
}

// SME2 multi-vec UNPK four registers
multiclass sme2_unpk_vector_vg4<string mnemonic, bit u> {
  def _H : sme2_unpk_vector_vg4<0b01, u, ZZZZ_h_mul_r, ZZ_b_mul_r, mnemonic>;
  def _S : sme2_unpk_vector_vg4<0b10, u, ZZZZ_s_mul_r, ZZ_h_mul_r, mnemonic>;
  def _D : sme2_unpk_vector_vg4<0b11, u, ZZZZ_d_mul_r, ZZ_s_mul_r, mnemonic>;
}

//===----------------------------------------------------------------------===//
// SME2 multi-vec CLAMP registers

class sme2_clamp_vector_vg24_multi<bits<2> sz, bits<3> op1, bit u,
                                   RegisterOperand multi_vector_ty,
                                   ZPRRegOp vector_ty, string mnemonic>
    : I<(outs multi_vector_ty:$Zd),
        (ins  multi_vector_ty:$_Zd, vector_ty:$Zn, vector_ty:$Zm),
        mnemonic, "\t$Zd, $Zn, $Zm",
        "", []>, Sched<[]>{
  bits<5> Zm;
  bits<5> Zn;
  let Inst{31-24} = 0b11000001;
  let Inst{23-22} = sz;
  let Inst{21}    = 0b1;
  let Inst{20-16} = Zm;
  let Inst{15-13} = 0b110;
  let Inst{12-10} = op1;
  let Inst{9-5}   = Zn;
  let Inst{0}     = u;

  let Constraints = "$Zd = $_Zd";
}

class sme2_clamp_vector_vg2_multi<bits<2> sz, bits<3> op1, bit u,
                                  RegisterOperand multi_vector_ty,
                                  ZPRRegOp vector_ty, string mnemonic>
    : sme2_clamp_vector_vg24_multi<sz, op1, u, multi_vector_ty, vector_ty,
                                   mnemonic>{
  bits<4> Zd;
  let Inst{4-1} = Zd;
}

multiclass sme2_fp_clamp_vector_vg2_multi<string mnemonic>{
  def _H : sme2_clamp_vector_vg2_multi<0b01, 0b000, 0b0, ZZ_h_mul_r, ZPR16, mnemonic>;
  def _S : sme2_clamp_vector_vg2_multi<0b10, 0b000, 0b0, ZZ_s_mul_r, ZPR32, mnemonic>;
  def _D : sme2_clamp_vector_vg2_multi<0b11, 0b000, 0b0, ZZ_d_mul_r, ZPR64, mnemonic>;
}

multiclass sme2_int_clamp_vector_vg2_multi<string mnemonic, bit u>{
  def _B : sme2_clamp_vector_vg2_multi<0b00, 0b001, u, ZZ_b_mul_r, ZPR8, mnemonic>;
  def _H : sme2_clamp_vector_vg2_multi<0b01, 0b001, u, ZZ_h_mul_r, ZPR16, mnemonic>;
  def _S : sme2_clamp_vector_vg2_multi<0b10, 0b001, u, ZZ_s_mul_r, ZPR32, mnemonic>;
  def _D : sme2_clamp_vector_vg2_multi<0b11, 0b001, u, ZZ_d_mul_r, ZPR64, mnemonic>;
}

// SME2.1 multi-vec FCLAMP two registers
multiclass sme2p1_bfclamp_vector_vg2_multi<string mnemonic> {
  def _H : sme2_clamp_vector_vg2_multi<0b00, 0b000, 0b0, ZZ_h_mul_r, ZPR16,
                                           mnemonic>;
}

class sme2_clamp_vector_vg4_multi<bits<2> sz, bits<3> op1, bit u,
                                  RegisterOperand multi_vector_ty,
                                  ZPRRegOp vector_ty, string mnemonic>
    : sme2_clamp_vector_vg24_multi<sz, op1, u,  multi_vector_ty, vector_ty,
                                   mnemonic>{
  bits<3> Zd;
  let Inst{4-2} = Zd;
  let Inst{1}   = 0b0;
}

multiclass sme2_fp_clamp_vector_vg4_multi<string mnemonic>{
  def _H : sme2_clamp_vector_vg4_multi<0b01, 0b010, 0b0, ZZZZ_h_mul_r, ZPR16, mnemonic>;
  def _S : sme2_clamp_vector_vg4_multi<0b10, 0b010, 0b0, ZZZZ_s_mul_r, ZPR32, mnemonic>;
  def _D : sme2_clamp_vector_vg4_multi<0b11, 0b010, 0b0, ZZZZ_d_mul_r, ZPR64, mnemonic>;
}

multiclass sme2_int_clamp_vector_vg4_multi<string mnemonic, bit u>{
  def _B : sme2_clamp_vector_vg4_multi<0b00, 0b011, u, ZZZZ_b_mul_r, ZPR8, mnemonic>;
  def _H : sme2_clamp_vector_vg4_multi<0b01, 0b011, u, ZZZZ_h_mul_r, ZPR16, mnemonic>;
  def _S : sme2_clamp_vector_vg4_multi<0b10, 0b011, u, ZZZZ_s_mul_r, ZPR32, mnemonic>;
  def _D : sme2_clamp_vector_vg4_multi<0b11, 0b011, u, ZZZZ_d_mul_r, ZPR64, mnemonic>;
}

// SME2.1 multi-vec FCLAMP four registers
multiclass sme2p1_bfclamp_vector_vg4_multi<string mnemonic> {
  def _H : sme2_clamp_vector_vg4_multi<0b00, 0b010, 0b0, ZZZZ_h_mul_r, ZPR16,
                                       mnemonic>;
}

// SME2 multi-vec ZIP two registers
class sme2_zip_vector_vg2<bits<2> sz, bit q, bit u,
                         RegisterOperand multi_vector_ty,
                         ZPRRegOp vector_ty, string mnemonic>
    : I<(outs multi_vector_ty:$Zd), (ins vector_ty:$Zn, vector_ty:$Zm),
        mnemonic, "\t$Zd, $Zn, $Zm",
        "", []>, Sched<[]>{
  bits<4> Zd;
  bits<5> Zm;
  bits<5> Zn;
  let Inst{31-24} = 0b11000001;
  let Inst{23-22} = sz;
  let Inst{21}    = 0b1;
  let Inst{20-16} = Zm;
  let Inst{15-11} = 0b11010;
  let Inst{10}    = q;
  let Inst{9-5}   = Zn;
  let Inst{4-1}   = Zd;
  let Inst{0}     = u;
}

multiclass sme2_zip_vector_vg2<string mnemonic, bit op> {
  def _B : sme2_zip_vector_vg2<0b00, 0b0, op, ZZ_b_mul_r, ZPR8, mnemonic>;
  def _H : sme2_zip_vector_vg2<0b01, 0b0, op, ZZ_h_mul_r, ZPR16, mnemonic>;
  def _S : sme2_zip_vector_vg2<0b10, 0b0, op, ZZ_s_mul_r, ZPR32, mnemonic>;
  def _D : sme2_zip_vector_vg2<0b11, 0b0, op, ZZ_d_mul_r, ZPR64, mnemonic>;
  def _Q : sme2_zip_vector_vg2<0b00, 0b1, op, ZZ_q_mul_r, ZPR128, mnemonic>;
}

//===----------------------------------------------------------------------===//
// SME2 Dot Products and MLA
class sme2_multi_vec_array_vg2_index<bits<2> sz, bits<6> op, MatrixOperand matrix_ty,
                                     RegisterOperand multi_vector_ty,
                                     ZPRRegOp vector_ty, Operand index_ty,
                                     string mnemonic>
    : I<(outs matrix_ty:$ZAda),
        (ins matrix_ty:$_ZAda, MatrixIndexGPR32Op8_11:$Rv, sme_elm_idx0_7:$imm3,
         multi_vector_ty:$Zn, vector_ty:$Zm, index_ty:$i),
         mnemonic, "\t$ZAda[$Rv, $imm3, vgx2], $Zn, $Zm$i",
        "", []>, Sched<[]> {
  bits<4> Zm;
  bits<2> Rv;
  bits<4> Zn;
  bits<3> imm3;
  let Inst{31-24} = 0b11000001;
  let Inst{23-22} = sz;
  let Inst{21-20} = 0b01;
  let Inst{19-16} = Zm;
  let Inst{15}    = 0b0;
  let Inst{14-13} = Rv;
  let Inst{12-10} = op{5-3};
  let Inst{9-6}   = Zn;
  let Inst{5-3}   = op{2-0};
  let Inst{2-0}   = imm3;

  let Constraints = "$ZAda = $_ZAda";
}

// SME2 multi-vec ternary indexed two registers 32-bit
multiclass sme2_multi_vec_array_vg2_index_32b<string mnemonic, bits<2> sz, bits<4> op,
                                              RegisterOperand multi_vector_ty,
                                              ZPRRegOp vector_ty, ValueType vt,
                                              SDPatternOperator intrinsic> {
  def NAME : sme2_multi_vec_array_vg2_index<sz, {op{3},?,?,op{2-0}}, MatrixOp32, multi_vector_ty, vector_ty,
                                             VectorIndexS32b_timm,  mnemonic>, SMEPseudo2Instr<NAME, 1> {
    bits<2> i;
    let Inst{11-10} = i;
  }
  def _PSEUDO : sme2_za_array_2op_multi_index_pseudo<NAME, sme_elm_idx0_7, multi_vector_ty, vector_ty, VectorIndexS32b_timm, SMEMatrixArray>;

  def : SME2_ZA_TwoOp_VG2_Multi_Index_Pat<NAME, intrinsic, sme_elm_idx0_7, vector_ty, vt, VectorIndexS32b_timm, tileslice16>;

  def : InstAlias<mnemonic # "\t$ZAda[$Rv, $imm3], $Zn, $Zm$i",
        (!cast<Instruction>(NAME) MatrixOp32:$ZAda,  MatrixIndexGPR32Op8_11:$Rv, sme_elm_idx0_7:$imm3,
        multi_vector_ty:$Zn, vector_ty:$Zm, VectorIndexS32b_timm:$i), 0>;
}

// SME2.1 multi-vec ternary indexed two registers 16-bit
multiclass sme2p1_multi_vec_array_vg2_index_16b<string mnemonic, bits<2> sz, bits<3> op,
                                                RegisterOperand multi_vector_ty, ZPRRegOp vector_ty,
                                                ValueType vt, SDPatternOperator intrinsic> {
  def NAME : sme2_multi_vec_array_vg2_index<sz, {op{2},?,?,op{1-0},?}, MatrixOp16,
                                            multi_vector_ty, vector_ty,
                                            VectorIndexH, mnemonic>, SMEPseudo2Instr<NAME, 1> {
    bits<3> i;
    let Inst{11-10} = i{2-1};
    let Inst{3}     = i{0};
  }

  def _PSEUDO : sme2_za_array_2op_multi_index_pseudo<NAME, sme_elm_idx0_7, multi_vector_ty, vector_ty, VectorIndexH32b, SMEMatrixArray>;

  def : SME2_ZA_TwoOp_VG2_Multi_Index_Pat<NAME, intrinsic, sme_elm_idx0_7, vector_ty, vt, VectorIndexH32b_timm, tileslice16>;

  def : InstAlias<mnemonic # "\t$ZAda[$Rv, $imm3], $Zn, $Zm$i",
        (!cast<Instruction>(NAME) MatrixOp16:$ZAda,  MatrixIndexGPR32Op8_11:$Rv, sme_elm_idx0_7:$imm3,
        multi_vector_ty:$Zn, vector_ty:$Zm, VectorIndexH:$i), 0>;
}

// SME2 multi-vec indexed FP8 two-way dot product to FP16 two registers
multiclass sme2p1_multi_vec_array_vg2_index_f8f16<string mnemonic, bits<2> sz, bits<3> op,
                                                  RegisterOperand multi_vector_ty, ZPRRegOp zpr_ty> {
  def NAME : sme2_multi_vec_array_vg2_index<sz, {op{2},?,?,op{1-0},?}, MatrixOp16,
                                            multi_vector_ty, zpr_ty,
                                            VectorIndexH, mnemonic> {
    bits<3> i;
    let Inst{11-10} = i{2-1};
    let Inst{3}     = i{0};
  }

  def : InstAlias<mnemonic # "\t$ZAda[$Rv, $imm3], $Zn, $Zm$i",
        (!cast<Instruction>(NAME) MatrixOp16:$ZAda,  MatrixIndexGPR32Op8_11:$Rv, sme_elm_idx0_7:$imm3,
        multi_vector_ty:$Zn, zpr_ty:$Zm, VectorIndexH:$i), 0>;
}

// SME2 multi-vec indexed FP8 two-way vertical dot product to single precision
// two registers
class sme2_fp8_multi_vec_array_vg4_index<string mnemonic, bit T>
   : sme2_multi_vec_array_vg2_index<0b11, {0b01,?,0b0, T,?}, MatrixOp32,
                                    ZZ_b_mul_r, ZPR4b8, VectorIndexS, mnemonic> {

  bits<2> i;
  let Inst{10} = i{1};
  let Inst{3}  = i{0};
  let AsmString = !strconcat(mnemonic, "{\t$ZAda[$Rv, $imm3, vgx4], $Zn, $Zm$i}");
}

// SME2 multi-vec ternary indexed two registers 64-bit

class sme2_multi_vec_array_vg2_index_64b<bits<2> op,
                                         RegisterOperand multi_vector_ty,
                                         ZPRRegOp vector_ty,
                                         string mnemonic>
    : I<(outs MatrixOp64:$ZAda),
        (ins MatrixOp64:$_ZAda, MatrixIndexGPR32Op8_11:$Rv, sme_elm_idx0_7:$imm3,
         multi_vector_ty:$Zn, vector_ty:$Zm, VectorIndexD32b_timm:$i1),
        mnemonic, "\t$ZAda[$Rv, $imm3, vgx2], $Zn, $Zm$i1",
        "", []>, Sched<[]> {
  bits<4> Zm;
  bits<2> Rv;
  bits<1> i1;
  bits<4> Zn;
  bits<3> imm3;
  let Inst{31-20} = 0b110000011101;
  let Inst{19-16} = Zm;
  let Inst{15}    = 0b0;
  let Inst{14-13} = Rv;
  let Inst{12-11} = 0b00;
  let Inst{10}    = i1;
  let Inst{9-6}   = Zn;
  let Inst{5}     = 0b0;
  let Inst{4-3}   = op;
  let Inst{2-0}   = imm3;

  let Constraints = "$ZAda = $_ZAda";
}

multiclass sme2_multi_vec_array_vg2_index_64b<string mnemonic, bits<2> op,
                                              RegisterOperand multi_vector_ty,
                                              ZPRRegOp vector_ty, ValueType vt,
                                              SDPatternOperator intrinsic> {
  def NAME : sme2_multi_vec_array_vg2_index_64b<op, multi_vector_ty, vector_ty,
                                                mnemonic>, SMEPseudo2Instr<NAME, 1>;

  def _PSEUDO : sme2_za_array_2op_multi_index_pseudo<NAME, sme_elm_idx0_7, multi_vector_ty, vector_ty, VectorIndexD32b_timm, SMEMatrixArray>;

  def : SME2_ZA_TwoOp_VG2_Multi_Index_Pat<NAME, intrinsic, sme_elm_idx0_7, vector_ty, vt, VectorIndexD32b_timm, tileslice16>;

  def : InstAlias<mnemonic # "\t$ZAda[$Rv, $imm3], $Zn, $Zm$i1",
        (!cast<Instruction>(NAME) MatrixOp64:$ZAda,  MatrixIndexGPR32Op8_11:$Rv, sme_elm_idx0_7:$imm3,
        multi_vector_ty:$Zn, vector_ty:$Zm, VectorIndexD32b_timm:$i1), 0>;
}

class sme2_multi_vec_array_vg4_index<bit sz, bits<7> op, MatrixOperand matrix_ty,
                                     RegisterOperand multi_vector_ty,
                                     ZPRRegOp vector_ty, Operand index_ty,
                                     string mnemonic>
    : I<(outs matrix_ty:$ZAda),
        (ins matrix_ty:$_ZAda, MatrixIndexGPR32Op8_11:$Rv, sme_elm_idx0_7:$imm3,
         multi_vector_ty:$Zn, vector_ty:$Zm, index_ty:$i),
         mnemonic, "\t$ZAda[$Rv, $imm3, vgx4], $Zn, $Zm$i",
        "", []>, Sched<[]> {
  bits<4> Zm;
  bits<2> Rv;
  bits<3> Zn;
  bits<3> imm3;
  let Inst{31-23} = 0b110000010;
  let Inst{22}    = sz;
  let Inst{21-20} = 0b01;
  let Inst{19-16} = Zm;
  let Inst{15}    = 0b1;
  let Inst{14-13} = Rv;
  let Inst{12-10} = op{6-4};
  let Inst{9-7}   = Zn;
  let Inst{6-3}   = op{3-0};
  let Inst{2-0}   = imm3;

  let Constraints = "$ZAda = $_ZAda";
}

// SME2 multi-vec ternary indexed four registers 32-bit
multiclass sme2_multi_vec_array_vg4_index_32b<string mnemonic, bits<4> op,
                                              RegisterOperand multi_vector_ty,
                                              ZPRRegOp vector_ty, ValueType vt,
                                              SDPatternOperator intrinsic> {
  def NAME : sme2_multi_vec_array_vg4_index<0b1, {op{3},?,?,0b0, op{2-0}}, MatrixOp32,  multi_vector_ty,
                                            vector_ty, VectorIndexS32b_timm, mnemonic>, SMEPseudo2Instr<NAME, 1> {
   bits<2> i;
   let Inst{11-10} = i;
  }

  def _PSEUDO : sme2_za_array_2op_multi_index_pseudo<NAME, sme_elm_idx0_7, multi_vector_ty, vector_ty, VectorIndexS32b_timm, SMEMatrixArray>;

  def : SME2_ZA_TwoOp_VG4_Multi_Index_Pat<NAME, intrinsic, sme_elm_idx0_7, vector_ty, vt, VectorIndexS32b_timm, tileslice16>;

  def : InstAlias<mnemonic # "\t$ZAda[$Rv, $imm3], $Zn, $Zm$i",
        (!cast<Instruction>(NAME) MatrixOp32:$ZAda,  MatrixIndexGPR32Op8_11:$Rv, sme_elm_idx0_7:$imm3,
        multi_vector_ty:$Zn, vector_ty:$Zm, VectorIndexS32b_timm:$i), 0>;
}

// SME2.1 multi-vec ternary indexed four registers 16-bit (FP8)
multiclass sme2p1_multi_vec_array_vg4_index_f8f16<string mnemonic, bits<3> op,
                                                  RegisterOperand multi_vector_ty,
                                                  ZPRRegOp zpr_ty> {
  def NAME : sme2_multi_vec_array_vg4_index<0b0,{0b1,?,?,op,?}, MatrixOp16,
                                            multi_vector_ty, zpr_ty,
                                            VectorIndexH, mnemonic>{
    bits<3> i;
    let Inst{11-10} = i{2-1};
    let Inst{3}     = i{0};
  }

  def : InstAlias<mnemonic # "\t$ZAda[$Rv, $imm3], $Zn, $Zm$i",
        (!cast<Instruction>(NAME) MatrixOp16:$ZAda,  MatrixIndexGPR32Op8_11:$Rv,
        sme_elm_idx0_7:$imm3, multi_vector_ty:$Zn, zpr_ty:$Zm, VectorIndexH:$i), 0>;
}

// SME2.1 multi-vec ternary indexed four registers 16-bit
multiclass sme2p1_multi_vec_array_vg4_index_16b<string mnemonic, bits<3> op,
                                                RegisterOperand multi_vector_ty,
                                                ZPRRegOp vector_ty, ValueType vt,
                                                SDPatternOperator intrinsic> {
  def NAME : sme2_multi_vec_array_vg4_index<0b0,{0b1,?,?,op,?}, MatrixOp16,
                                            multi_vector_ty, vector_ty,
                                            VectorIndexH, mnemonic>, SMEPseudo2Instr<NAME, 1> {
    bits<3> i;
    let Inst{11-10} = i{2-1};
    let Inst{3}     = i{0};
  }

  def _PSEUDO : sme2_za_array_2op_multi_index_pseudo<NAME, sme_elm_idx0_7, multi_vector_ty, vector_ty, VectorIndexH32b_timm, SMEMatrixArray>;

  def : SME2_ZA_TwoOp_VG4_Multi_Index_Pat<NAME, intrinsic, sme_elm_idx0_7, vector_ty, vt, VectorIndexH32b_timm, tileslice16>;

  def : InstAlias<mnemonic # "\t$ZAda[$Rv, $imm3], $Zn, $Zm$i",
        (!cast<Instruction>(NAME) MatrixOp16:$ZAda,  MatrixIndexGPR32Op8_11:$Rv,
        sme_elm_idx0_7:$imm3, multi_vector_ty:$Zn, vector_ty:$Zm, VectorIndexH:$i), 0>;
}

// SME2 multi-vec ternary indexed four registers 64-bit
class sme2_multi_vec_array_vg4_index_64b<bits<3> op,
                                         RegisterOperand multi_vector_ty,
                                         ZPRRegOp vector_ty,
                                         string mnemonic>
    : I<(outs MatrixOp64:$ZAda),
        (ins MatrixOp64:$_ZAda, MatrixIndexGPR32Op8_11:$Rv, sme_elm_idx0_7:$imm3,
         multi_vector_ty:$Zn, vector_ty:$Zm, VectorIndexD32b_timm:$i1),
        mnemonic, "\t$ZAda[$Rv, $imm3, vgx4], $Zn, $Zm$i1",
        "", []>, Sched<[]> {
  bits<4> Zm;
  bits<2> Rv;
  bits<1> i1;
  bits<3> Zn;
  bits<3> imm3;
  let Inst{31-20} = 0b110000011101;
  let Inst{19-16} = Zm;
  let Inst{15}    = 0b1;
  let Inst{14-13} = Rv;
  let Inst{12}    = 0b0;
  let Inst{11}    = op{2};
  let Inst{10}    = i1;
  let Inst{9-7}   = Zn;
  let Inst{6-5}   = 0b00;
  let Inst{4-3}   = op{1-0};
  let Inst{2-0}   = imm3;

  let Constraints = "$ZAda = $_ZAda";
}

multiclass sme2_multi_vec_array_vg4_index_64b<string mnemonic, bits<3> op,
                                              RegisterOperand multi_vector_ty,
                                              ZPRRegOp vector_ty, ValueType vty,
                                              SDPatternOperator intrinsic> {
  def NAME : sme2_multi_vec_array_vg4_index_64b<op, multi_vector_ty, vector_ty,
                                                mnemonic>, SMEPseudo2Instr<NAME, 1>;

  def _PSEUDO : sme2_za_array_2op_multi_index_pseudo<NAME, sme_elm_idx0_7, multi_vector_ty, vector_ty, VectorIndexD32b_timm, SMEMatrixArray>;

  def : SME2_ZA_TwoOp_VG4_Multi_Index_Pat<NAME, intrinsic, sme_elm_idx0_7, vector_ty, vty, VectorIndexD32b_timm, tileslice16>;

  def : InstAlias<mnemonic # "\t$ZAda[$Rv, $imm3], $Zn, $Zm$i1",
        (!cast<Instruction>(NAME) MatrixOp64:$ZAda,  MatrixIndexGPR32Op8_11:$Rv, sme_elm_idx0_7:$imm3,
        multi_vector_ty:$Zn, vector_ty:$Zm, VectorIndexD32b_timm:$i1), 0>;
}

// FMLAL (multiple and indexed vector, FP8 to FP16)
class sme2_multi_vec_array_vg24_index_16b<bits<2> sz, bit vg4, bits<3> op,
                                          RegisterOperand multi_vector_ty, string mnemonic>
    : I<(outs MatrixOp16:$ZAda),
        (ins MatrixOp16:$_ZAda, MatrixIndexGPR32Op8_11:$Rv, uimm2s2range:$imm2,
         multi_vector_ty:$Zn, ZPR4b8:$Zm, VectorIndexB:$i),
         mnemonic, "\t$ZAda[$Rv, $imm2, " # !if(vg4, "vgx4", "vgx2") # "], $Zn, $Zm$i",
         "", []>, Sched<[]> {
  bits<4> Zm;
  bits<2> Rv;
  bits<4> i;
  bits<2> imm2;
  let Inst{31-24} = 0b11000001;
  let Inst{23-22} = sz;
  let Inst{21-20} = 0b01;
  let Inst{19-16} = Zm;
  let Inst{15}    = vg4;
  let Inst{14-13} = Rv;
  let Inst{12}    = op{2};
  let Inst{11-10} = i{3-2};
  let Inst{5-4}   = op{1-0};
  let Inst{3-2}   = i{1-0};
  let Inst{1-0}   = imm2;

  let Constraints = "$ZAda = $_ZAda";
}

multiclass sme2_multi_vec_array_vg2_index_16b<string mnemonic, bits<2> sz, bits<3>op> {
  def NAME : sme2_multi_vec_array_vg24_index_16b<sz, 0b0, op, ZZ_b_mul_r, mnemonic> {
    bits<4> Zn;
    let Inst{9-6} = Zn;
 }
 def : InstAlias<mnemonic # "\t$ZAda[$Rv, $imm2], $Zn, $Zm$i",
                 (!cast<Instruction>(NAME) MatrixOp16:$ZAda,  MatrixIndexGPR32Op8_11:$Rv,
                  uimm2s2range:$imm2, ZZ_b_mul_r:$Zn, ZPR4b8:$Zm, VectorIndexB:$i), 0>;
}

multiclass sme2_multi_vec_array_vg4_index_16b<string mnemonic, bits<2>sz, bits<3>op> {
  def NAME: sme2_multi_vec_array_vg24_index_16b<sz, 0b1, op, ZZZZ_b_mul_r, mnemonic> {
    bits<3> Zn;
    let Inst{9-7} = Zn;
    let Inst{6}   = 0b0;
  }
 def : InstAlias<mnemonic # "\t$ZAda[$Rv, $imm2], $Zn, $Zm$i",
                 (!cast<Instruction>(NAME) MatrixOp16:$ZAda,  MatrixIndexGPR32Op8_11:$Rv,
                  uimm2s2range:$imm2, ZZZZ_b_mul_r:$Zn, ZPR4b8:$Zm, VectorIndexB:$i), 0>;
}

//===----------------------------------------------------------------------===//
// SME2 multi-vec indexed long long MLA one source 16-bit
class sme2_mla_ll_array_index_16b<string mnemonic, bits<2> sz,bits<2> op>
    : I<(outs MatrixOp16:$ZAda),
        (ins MatrixOp16:$_ZAda, MatrixIndexGPR32Op8_11:$Rv, uimm3s2range:$imm3, ZPR8:$Zn, ZPR4b8:$Zm, VectorIndexB32b_timm:$i),
        mnemonic, "\t$ZAda[$Rv, $imm3], $Zn, $Zm$i",
        "", []>, Sched<[]> {
  bits<4> Zm;
  bits<2> Rv;
  bits<4> i;
  bits<5> Zn;
  bits<3> imm3;
  let Inst{31-24} = 0b11000001;
  let Inst{23-22} = sz;
  let Inst{21-20} = 0b00;
  let Inst{19-16} = Zm;
  let Inst{15}    = i{3};
  let Inst{14-13} = Rv;
  let Inst{12}    = op{1};
  let Inst{11-10} = i{2-1};
  let Inst{9-5}   = Zn;
  let Inst{4}     = op{0};
  let Inst{3}     = i{0};
  let Inst{2-0}   = imm3;

  let Constraints = "$ZAda = $_ZAda";
}

// SME2 multi-vec indexed long long MLA one source 32-bit
class sme2_mla_ll_array_index_32b<string mnemonic, bits<2> sz, bits<3> op>
    : I<(outs MatrixOp32:$ZAda),
        (ins MatrixOp32:$_ZAda, MatrixIndexGPR32Op8_11:$Rv, uimm2s4range:$imm2, ZPR8:$Zn, ZPR4b8:$Zm, VectorIndexB32b_timm:$i),
        mnemonic, "\t$ZAda[$Rv, $imm2], $Zn, $Zm$i",
        "", []>, Sched<[]> {
  bits<4> Zm;
  bits<2> Rv;
  bits<4> i;
  bits<5> Zn;
  bits<2> imm2;
  let Inst{31-24} = 0b11000001;
  let Inst{23-22} = sz;
  let Inst{21-20} = 0b00;
  let Inst{19-16} = Zm;
  let Inst{15}    = i{3};
  let Inst{14-13} = Rv;
  let Inst{12-10} = i{2-0};
  let Inst{9-5}   = Zn;
  let Inst{4-2}   = op;
  let Inst{1-0}   = imm2;

  let Constraints = "$ZAda = $_ZAda";
}

multiclass sme2_mla_ll_array_index_32b<string mnemonic, bits<2> sz, bits<3> op, SDPatternOperator intrinsic> {
  def NAME : sme2_mla_ll_array_index_32b<mnemonic, sz, op>, SMEPseudo2Instr<NAME, 1>;

  def _PSEUDO : sme2_za_array_2op_multi_index_pseudo<NAME, uimm2s4range, ZPR8, ZPR4b8, VectorIndexB32b_timm, SMEMatrixArray>;

  def : SME2_ZA_TwoOp_Multi_Index_Pat<NAME, intrinsic, uimm2s4range, ZPR4b8, nxv16i8, VectorIndexB32b_timm, tileslicerange2s4>;
}

// SME2 multi-vec indexed long long MLA one source 64-bit

class sme2_mla_ll_array_index_64b<string mnemonic, bits<2> op>
    : I<(outs MatrixOp64:$ZAda),
        (ins MatrixOp64:$_ZAda, MatrixIndexGPR32Op8_11:$Rv, uimm2s4range:$imm2, ZPR16:$Zn, ZPR4b16:$Zm, VectorIndexH32b_timm:$i),
        mnemonic, "\t$ZAda[$Rv, $imm2], $Zn, $Zm$i",
        "", []>, Sched<[]> {
  bits<4> Zm;
  bits<2> Rv;
  bits<3> i;
  bits<5> Zn;
  bits<2> imm2;
  let Inst{31-20} = 0b110000011000;
  let Inst{19-16} = Zm;
  let Inst{15}    = i{2};
  let Inst{14-13} = Rv;
  let Inst{12}    = 0b0;
  let Inst{11-10} = i{1-0};
  let Inst{9-5}   = Zn;
  let Inst{4-3}   = op;
  let Inst{2}     = 0b0;
  let Inst{1-0}   = imm2;

  let Constraints = "$ZAda = $_ZAda";
}

multiclass sme2_mla_ll_array_index_64b<string mnemonic, bits<2> op, SDPatternOperator intrinsic> {
  def NAME : sme2_mla_ll_array_index_64b<mnemonic, op>, SMEPseudo2Instr<NAME, 1>;

  def _PSEUDO : sme2_za_array_2op_multi_index_pseudo<NAME, uimm2s4range, ZPR16, ZPR4b16, VectorIndexH32b_timm, SMEMatrixArray>;

  def : SME2_ZA_TwoOp_Multi_Index_Pat<NAME, intrinsic, uimm2s4range, ZPR4b16, nxv8i16, VectorIndexH32b_timm, tileslicerange2s4>;
}

class sme2_mla_ll_array_vg24_index_32b<bits<2> sz, bit vg4, bits<3> op,
                                       RegisterOperand vector_ty,
                                       string mnemonic>
    : I<(outs MatrixOp32:$ZAda),
        (ins MatrixOp32:$_ZAda, MatrixIndexGPR32Op8_11:$Rv, uimm1s4range:$imm,
             vector_ty:$Zn, ZPR4b8:$Zm, VectorIndexB32b_timm:$i),
        mnemonic, "\t$ZAda[$Rv, $imm, " # !if(vg4, "vgx4", "vgx2") # "], $Zn, $Zm$i",
        "", []>, Sched<[]> {
  bits<4> Zm;
  bits<2> Rv;
  bits<4> i;
  bit     imm;
  let Inst{31-24} = 0b11000001;
  let Inst{23-22} = sz;
  let Inst{21-20} = 0b01;
  let Inst{19-16} = Zm;
  let Inst{15}    = vg4;
  let Inst{14-13} = Rv;
  let Inst{12}    = 0b0;
  let Inst{11-10} = i{3-2};
  let Inst{5-3}   = op;
  let Inst{2-1}   = i{1-0};
  let Inst{0}     = imm;

  let Constraints = "$ZAda = $_ZAda";
}

//SME2 multi-vec indexed long long MLA two sources 32-bit

multiclass sme2_mla_ll_array_vg2_index_32b<string mnemonic, bits<2> sz, bits<3> op, SDPatternOperator intrinsic> {
  def NAME: sme2_mla_ll_array_vg24_index_32b<sz, 0b0, op, ZZ_b_mul_r, mnemonic>, SMEPseudo2Instr<NAME, 1> {
   bits<4> Zn;
   let Inst{9-6} = Zn;
  }

  def _PSEUDO : sme2_za_array_2op_multi_index_pseudo<NAME, uimm1s4range, ZZ_b_mul_r, ZPR4b8, VectorIndexB32b_timm, SMEMatrixArray>;

  def : SME2_ZA_TwoOp_VG2_Multi_Index_Pat<NAME, intrinsic, uimm1s4range, ZPR4b8, nxv16i8, VectorIndexB32b_timm, tileslicerange1s4>;

  def : InstAlias<mnemonic # "\t$ZAda[$Rv, $imm], $Zn, $Zm$i",
                 (!cast<Instruction>(NAME) MatrixOp32:$ZAda,  MatrixIndexGPR32Op8_11:$Rv, uimm1s4range:$imm, ZZ_b_mul_r:$Zn, ZPR4b8:$Zm, VectorIndexB32b_timm:$i), 0>;
}

// SME2 multi-vec indexed long long MLA four sources 32-bit

multiclass sme2_mla_ll_array_vg4_index_32b<string mnemonic, bits<2> sz, bits<4> op, SDPatternOperator intrinsic> {
  def NAME: sme2_mla_ll_array_vg24_index_32b<sz, 0b1, op{2-0}, ZZZZ_b_mul_r, mnemonic>, SMEPseudo2Instr<NAME, 1> {
   bits<3> Zn;
   let Inst{9-7} = Zn;
   let Inst{6}   = op{3};
  }

  def _PSEUDO : sme2_za_array_2op_multi_index_pseudo<NAME, uimm1s4range, ZZZZ_b_mul_r, ZPR4b8, VectorIndexB32b_timm, SMEMatrixArray>;

  def : SME2_ZA_TwoOp_VG4_Multi_Index_Pat<NAME, intrinsic, uimm1s4range, ZPR4b8, nxv16i8, VectorIndexB32b_timm, tileslicerange1s4>;

  def : InstAlias<mnemonic # "\t$ZAda[$Rv, $imm], $Zn, $Zm$i",
                 (!cast<Instruction>(NAME) MatrixOp32:$ZAda,  MatrixIndexGPR32Op8_11:$Rv, uimm1s4range:$imm, ZZZZ_b_mul_r:$Zn, ZPR4b8:$Zm, VectorIndexB32b_timm:$i), 0>;
}
class sme2_mla_ll_array_vg24_index_64b<bit vg4,  bits<2> op,
                                       RegisterOperand vector_ty,
                                       string mnemonic>
    : I<(outs MatrixOp64:$ZAda),
        (ins MatrixOp64:$_ZAda, MatrixIndexGPR32Op8_11:$Rv, uimm1s4range:$imm,
             vector_ty:$Zn, ZPR4b16:$Zm, VectorIndexH32b_timm:$i),
        mnemonic, "\t$ZAda[$Rv, $imm, " # !if(vg4, "vgx4", "vgx2") # "], $Zn, $Zm$i",
        "", []>, Sched<[]> {
  bits<4> Zm;
  bits<2> Rv;
  bits<3> i;
  bit     imm;
  let Inst{31-20} = 0b110000011001;
  let Inst{19-16} = Zm;
  let Inst{15}    = vg4;
  let Inst{14-13} = Rv;
  let Inst{12-11} = 0b00;
  let Inst{10}    = i{2};
  let Inst{5}     = 0b0;
  let Inst{4-3}   = op;
  let Inst{2-1}   = i{1-0};
  let Inst{0}     = imm;

  let Constraints = "$ZAda = $_ZAda";
}

// SME2 multi-vec indexed long long MLA two sources 64-bit

multiclass sme2_mla_ll_array_vg2_index_64b<string mnemonic, bits<2> op, SDPatternOperator intrinsic> {
  def NAME: sme2_mla_ll_array_vg24_index_64b<0b0, op, ZZ_h_mul_r, mnemonic>, SMEPseudo2Instr<NAME, 1> {
    bits<4> Zn;
    let Inst{9-6} = Zn;
  }

  def _PSEUDO : sme2_za_array_2op_multi_index_pseudo<NAME, uimm1s4range, ZZ_h_mul_r, ZPR4b16, VectorIndexH32b_timm, SMEMatrixArray>;

  def : SME2_ZA_TwoOp_VG2_Multi_Index_Pat<NAME, intrinsic, uimm1s4range, ZPR4b16, nxv8i16, VectorIndexH32b_timm, tileslicerange1s4>;

  def : InstAlias<mnemonic # "\t$ZAda[$Rv, $imm], $Zn, $Zm$i",
                 (!cast<Instruction>(NAME) MatrixOp64:$ZAda,  MatrixIndexGPR32Op8_11:$Rv, uimm1s4range:$imm, ZZ_h_mul_r:$Zn, ZPR4b16:$Zm, VectorIndexH32b_timm:$i), 0>;
}

// SME2 multi-vec indexed long long MLA four sources 64-bit

multiclass sme2_mla_ll_array_vg4_index_64b<string mnemonic, bits<2> op, SDPatternOperator intrinsic> {
  def NAME: sme2_mla_ll_array_vg24_index_64b<0b1, op, ZZZZ_h_mul_r,  mnemonic>, SMEPseudo2Instr<NAME, 1> {
    bits<3> Zn;
    let Inst{9-7} = Zn;
    let Inst{6}   = 0b0;
  }

  def _PSEUDO : sme2_za_array_2op_multi_index_pseudo<NAME, uimm1s4range, ZZZZ_h_mul_r, ZPR4b16, VectorIndexH32b_timm, SMEMatrixArray>;

  def : SME2_ZA_TwoOp_VG4_Multi_Index_Pat<NAME, intrinsic, uimm1s4range, ZPR4b16, nxv8i16, VectorIndexH32b_timm, tileslicerange1s4>;

  def : InstAlias<mnemonic # "\t$ZAda[$Rv, $imm], $Zn, $Zm$i",
                 (!cast<Instruction>(NAME) MatrixOp64:$ZAda,  MatrixIndexGPR32Op8_11:$Rv, uimm1s4range:$imm, ZZZZ_h_mul_r:$Zn, ZPR4b16:$Zm, VectorIndexH32b_timm:$i), 0>;
}


//SME2 multiple and single vector long long FMA one source

class sme2_mla_ll_array_single<string mnemonic, bits<5> op,
                               MatrixOperand matrix_ty, ZPRRegOp vector_ty,
                               ZPRRegOp zpr_ty>
    : I<(outs matrix_ty:$ZAda),
        (ins matrix_ty:$_ZAda, MatrixIndexGPR32Op8_11:$Rv, uimm2s4range:$imm,
             vector_ty:$Zn, zpr_ty:$Zm),
        mnemonic, "\t$ZAda[$Rv, $imm], $Zn, $Zm",
        "", []>, Sched<[]> {
  bits<4> Zm;
  bits<2> Rv;
  bits<5> Zn;
  bits<2> imm;
  let Inst{31-23} = 0b110000010;
  let Inst{22}    = op{4}; //sz
  let Inst{21}    = 0b1;
  let Inst{20}    = op{3}; //fp8
  let Inst{19-16} = Zm;
  let Inst{15}    = 0b0;
  let Inst{14-13} = Rv;
  let Inst{12-10} = 0b001;
  let Inst{9-5}   = Zn;
  let Inst{4-2}   = op{2-0};
  let Inst{1-0}   = imm;

  let Constraints = "$ZAda = $_ZAda";
}

multiclass sme2_mla_ll_array_single<string mnemonic, bits<5> op,
                                    MatrixOperand matrix_ty, ZPRRegOp vector_ty,
                                    ZPRRegOp zpr_ty, ValueType vt, SDPatternOperator intrinsic> {
  def NAME : sme2_mla_ll_array_single<mnemonic, op, matrix_ty, vector_ty, zpr_ty>, SMEPseudo2Instr<NAME, 1>;

  def NAME # _PSEUDO : sme2_za_array_2op_multi_single_pseudo<NAME, uimm2s4range, vector_ty, zpr_ty, SMEMatrixArray>;

  def : SME2_ZA_TwoOp_Multi_Single_Pat<NAME, intrinsic, uimm2s4range, zpr_ty, vt, tileslicerange2s4>;
}

class sme2_mla_ll_array_vg24_single<bits<6> op, MatrixOperand matrix_ty,
                                    RegisterOperand vector_ty, ZPRRegOp zpr_ty,
                                    string mnemonic>
    : I<(outs matrix_ty:$ZAda),
        (ins matrix_ty:$_ZAda, MatrixIndexGPR32Op8_11:$Rv, uimm1s4range:$imm,
             vector_ty:$Zn, zpr_ty:$Zm),
        mnemonic, "\t$ZAda[$Rv, $imm,  " # !if(op{4}, "vgx4", "vgx2") # "], $Zn, $Zm",
        "", []>, Sched<[]> {
  bits<4> Zm;
  bits<2> Rv;
  bits<5> Zn;
  bit     imm;
  let Inst{31-23} = 0b110000010;
  let Inst{22}    = op{5}; //sz
  let Inst{21}    = 0b1;
  let Inst{20}    = op{4}; //vg4
  let Inst{19-16} = Zm;
  let Inst{15}    = 0b0;
  let Inst{14-13} = Rv;
  let Inst{12-10} = 0b000;
  let Inst{9-5}   = Zn;
  let Inst{4-1}   = op{3-0};
  let Inst{0}     = imm;

  let Constraints = "$ZAda = $_ZAda";
}

//SME2 single-multi long long MLA two and four sources

multiclass sme2_mla_ll_array_vg24_single<string mnemonic, bits<6> op,
                                          MatrixOperand matrix_ty,
                                          RegisterOperand multi_vector_ty,
                                          ZPRRegOp zpr_ty> {
  def NAME: sme2_mla_ll_array_vg24_single<op, matrix_ty, multi_vector_ty,
                                          zpr_ty, mnemonic>, SMEPseudo2Instr<NAME, 1>;

  def NAME # _PSEUDO : sme2_za_array_2op_multi_single_pseudo<NAME, uimm1s4range, multi_vector_ty, zpr_ty, SMEMatrixArray>;

  def : InstAlias<mnemonic # "\t$ZAd[$Rv, $imm], $Zn, $Zm",
                 (!cast<Instruction>(NAME) matrix_ty:$ZAd,  MatrixIndexGPR32Op8_11:$Rv, uimm1s4range:$imm, multi_vector_ty:$Zn, zpr_ty:$Zm), 0>;
}

multiclass sme2_mla_ll_array_vg2_single<string mnemonic, bits<5> op,
                                        MatrixOperand matrix_ty,
                                        RegisterOperand multi_vector_ty,
                                        ZPRRegOp zpr_ty, ValueType vt, SDPatternOperator intrinsic> {

  defm NAME: sme2_mla_ll_array_vg24_single<mnemonic, {op, 0b0}, matrix_ty, multi_vector_ty, zpr_ty>;

  def : SME2_ZA_TwoOp_VG2_Multi_Single_Pat<NAME, intrinsic, uimm1s4range, zpr_ty, vt, tileslicerange1s4>;
}

multiclass sme2_mla_ll_array_vg4_single<string mnemonic, bits<5> op,
                                        MatrixOperand matrix_ty,
                                        RegisterOperand multi_vector_ty,
                                        ZPRRegOp zpr_ty, ValueType vt, SDPatternOperator intrinsic> {
  defm NAME: sme2_mla_ll_array_vg24_single<mnemonic, {op, 0b0}, matrix_ty, multi_vector_ty, zpr_ty>;

  def : SME2_ZA_TwoOp_VG4_Multi_Single_Pat<NAME, intrinsic, uimm1s4range, zpr_ty, vt, tileslicerange1s4>;
}

// SME2 multiple vectors long long MLA two sources

class sme2_mla_ll_array_vg2_multi<bits<5> op, MatrixOperand matrix_ty,
                                  RegisterOperand vector_ty,string mnemonic>
    : I<(outs matrix_ty:$ZAda),
        (ins matrix_ty:$_ZAda, MatrixIndexGPR32Op8_11:$Rv, uimm1s4range:$imm,
             vector_ty:$Zn, vector_ty:$Zm),
        mnemonic, "\t$ZAda[$Rv, $imm, vgx2], $Zn, $Zm",
        "", []>, Sched<[]> {
  bits<4> Zm;
  bits<2> Rv;
  bits<4> Zn;
  bit     imm;
  let Inst{31-23} = 0b110000011;
  let Inst{22}    = op{4};  // sz
  let Inst{21}    = 0b1;
  let Inst{20-17} = Zm;
  let Inst{16-15} = 0b00;
  let Inst{14-13} = Rv;
  let Inst{12-10} = 0b000;
  let Inst{9-6}   = Zn;
  let Inst{5-2}   = op{3-0};
  let Inst{1}     = 0b0;
  let Inst{0}     = imm;

  let Constraints = "$ZAda = $_ZAda";
}

multiclass sme2_mla_ll_array_vg2_multi<string mnemonic, bits<5> op,
                                       MatrixOperand matrix_ty,
                                       RegisterOperand vector_ty,
                                       ValueType vt, SDPatternOperator intrinsic> {
  def NAME : sme2_mla_ll_array_vg2_multi<op, matrix_ty, vector_ty, mnemonic>, SMEPseudo2Instr<NAME, 1>;

  def _PSEUDO : sme2_za_array_2op_multi_multi_pseudo<NAME, uimm1s4range, vector_ty, SMEMatrixArray>;

  def : SME2_ZA_TwoOp_VG2_Multi_Multi_Pat<NAME, intrinsic, uimm1s4range, vt, tileslicerange1s4>;

  def : InstAlias<mnemonic # "\t$ZAda[$Rv, $imm], $Zn, $Zm",
                 (!cast<Instruction>(NAME) matrix_ty:$ZAda, MatrixIndexGPR32Op8_11:$Rv, uimm1s4range:$imm, vector_ty:$Zn, vector_ty:$Zm), 0>;
}

// SME2 multiple vectors long long MLA four sources

class sme2_mla_ll_array_vg4_multi<bits<5> op,MatrixOperand matrix_ty,
                                  RegisterOperand vector_ty,
                                  string mnemonic>
    : I<(outs matrix_ty:$ZAda),
        (ins matrix_ty:$_ZAda, MatrixIndexGPR32Op8_11:$Rv, uimm1s4range:$imm,
             vector_ty:$Zn, vector_ty:$Zm),
        mnemonic, "\t$ZAda[$Rv, $imm, vgx4], $Zn, $Zm",
        "", []>, Sched<[]> {
  bits<3> Zm;
  bits<2> Rv;
  bits<3> Zn;
  bit     imm;
  let Inst{31-23} = 0b110000011;
  let Inst{22}    = op{4}; // sz
  let Inst{21}    = 0b1;
  let Inst{20-18} = Zm;
  let Inst{17-15} = 0b010;
  let Inst{14-13} = Rv;
  let Inst{12-10} = 0b000;
  let Inst{9-7}   = Zn;
  let Inst{6}     = 0b0;
  let Inst{5-2}   = op{3-0};
  let Inst{1}     = 0b0;
  let Inst{0}     = imm;

  let Constraints = "$ZAda = $_ZAda";
}

multiclass sme2_mla_ll_array_vg4_multi<string mnemonic, bits<5> op,
                                       MatrixOperand matrix_ty,
                                       RegisterOperand vector_ty,
                                       ValueType vt, SDPatternOperator intrinsic> {
  def NAME : sme2_mla_ll_array_vg4_multi<op, matrix_ty, vector_ty, mnemonic>, SMEPseudo2Instr<NAME, 1>;

  def _PSEUDO : sme2_za_array_2op_multi_multi_pseudo<NAME, uimm1s4range, vector_ty, SMEMatrixArray>;

  def : SME2_ZA_TwoOp_VG4_Multi_Multi_Pat<NAME, intrinsic, uimm1s4range, vt, tileslicerange1s4>;

  def : InstAlias<mnemonic # "\t$ZAda[$Rv, $imm], $Zn, $Zm",
                 (!cast<Instruction>(NAME) matrix_ty:$ZAda, MatrixIndexGPR32Op8_11:$Rv, uimm1s4range:$imm, vector_ty:$Zn, vector_ty:$Zm), 0>;
}

//===----------------------------------------------------------------------===//
// SME2 Outer Product and Accumulate

multiclass sme2_int_mopx_tile<string mnemonic, bits<3> op, SDPatternOperator intrinsic> {
  def NAME : sme_int_outer_product_inst<op, 0b0, 0b1, TileOp32, ZPR16, mnemonic>, SMEPseudo2Instr<NAME, 1> {
    bits<2> ZAda;
    let Inst{1-0} = ZAda;
    let Inst{2}   = 0b0;
  }

  def _PSEUDO : sme_outer_product_pseudo<ZPR16, SMEMatrixTileS>, SMEPseudo2Instr<NAME, 0>;

  def : SME_ZA_Tile_TwoPred_TwoVec_Pat<NAME, intrinsic, timm32_0_3, nxv8i1, nxv8i16>;
}

multiclass  sme2_int_bmopx_tile<string mnemonic, bits<3> op, SDPatternOperator intrinsic> {
  def NAME : sme_outer_product_widening_inst<op, ZPR32, mnemonic>, SMEPseudo2Instr<NAME, 1>;

  def _PSEUDO : sme_outer_product_pseudo<ZPR32, SMEMatrixTileS>, SMEPseudo2Instr<NAME, 0>;

  def : SME_ZA_Tile_TwoPred_TwoVec_Pat<NAME, intrinsic, timm32_0_3, nxv4i1, nxv4i32>;
}

//===----------------------------------------------------------------------===///
// SME2 Zero Lookup Table.
class sme2_zero_zt<string mnemonic, bits<4> opc>
    : I<(outs ZTR:$ZT), (ins ),
         mnemonic, "\t\\{ $ZT \\}",
         "", []>, Sched<[]> {
  let Inst{31-4} = 0b1100000001001000000000000000;
  let Inst{3-0}  = opc;
}

multiclass sme2_zero_zt<string mnemonic, bits<4> opc> {
  def NAME : sme2_zero_zt<mnemonic, opc>;
  def NAME # _PSEUDO
        : Pseudo<(outs), (ins ZTR:$ZT), []>, Sched<[]> {
    // Translated to actual instruction in AArch64ISelLowering.cpp
    let usesCustomInserter = 1;
  }
  def : Pat<(int_aarch64_sme_zero_zt (imm_to_zt untyped:$zt)),
          (!cast<Instruction>(NAME # _PSEUDO) $zt)>;
}

//===----------------------------------------------------------------------===//
// SME2 lookup table load/store
class sme2_spill_fill_vector<string mnemonic, bits<8> opc>
    : I<!if(opc{7}, (outs ), (outs ZTR:$ZTt)),
        !if(opc{7}, (ins ZTR:$ZTt, GPR64sp:$Rn), (ins GPR64sp:$Rn)),
        mnemonic, "\t$ZTt, [$Rn]",
        "", []>, Sched<[]> {
  bits<5> Rn;
  let Inst{31-22} = 0b1110000100;
  let Inst{21-16} = opc{7-2};
  let Inst{15-10} = 0b100000;
  let Inst{9-5}   = Rn;
  let Inst{4-2}   = 0b000;
  let Inst{1-0}   = opc{1-0};

  let mayLoad     = !not(opc{7});
  let mayStore    = opc{7};
}


multiclass sme2_spill_fill_vector<string mnemonic, bits<8> opc, SDPatternOperator op> {
  def NAME : sme2_spill_fill_vector<mnemonic, opc>;
  def NAME # _PSEUDO
      : Pseudo<(outs), (ins ZTR:$ZTt, GPR64sp:$base), []>, Sched<[]> {
    // Translated to actual instruction in AArch64ISelLowering.cpp
    let usesCustomInserter = 1;
  }
  def : Pat<(op (imm_to_zt untyped:$tile), GPR64sp:$base),
            (!cast<Instruction>(NAME # _PSEUDO) $tile, $base)>;
}

//===----------------------------------------------------------------------===///
// SME2 move to/from lookup table
class sme2_movt_zt_to_scalar<string mnemonic, bits<7> opc>
    : I<(outs GPR64:$Rt), (ins ZTR:$ZTt, uimm3s8:$imm3),
         mnemonic, "\t$Rt, $ZTt[$imm3]",
         "", []>, Sched<[]> {
  bits<3> imm3;
  bits<5> Rt;
  let Inst{31-15} = 0b11000000010011000;
  let Inst{14-12} = imm3;
  let Inst{11-5}  = opc;
  let Inst{4-0}   = Rt;
}

class sme2_movt_scalar_to_zt<string mnemonic, bits<7> opc>
    : I<(outs ZTR:$ZTt), (ins uimm3s8:$imm3, GPR64:$Rt),
         mnemonic, "\t$ZTt[$imm3], $Rt",
         "", []>, Sched<[]> {
  bits<3> imm3;
  bits<5> Rt;
  let Inst{31-15} = 0b11000000010011100;
  let Inst{14-12} = imm3;
  let Inst{11-5}  = opc;
  let Inst{4-0}   = Rt;
}

// SME2 move vector to lookup table
class sme2_movt_zt_to_zt<string mnemonic, bits<7> opc>
   : I<(outs ZTR:$ZTt), (ins sme_elm_idx0_3:$off2, ZPRAny:$Zt),
        mnemonic, "\t$ZTt[$off2, mul vl], $Zt",
        "", []>, Sched<[]> {
  bits<5> Zt;
  bits<2> off2;
  let Inst{31-14} = 0b110000000100111100;
  let Inst{13-12} = off2;
  let Inst{11-5}  = opc;
  let Inst{4-0}   = Zt;
}

multiclass sme2_movt_zt_to_zt<string mnemonic, bits<7> opc> {
  def NAME : sme2_movt_zt_to_zt<mnemonic, opc>;
  def : InstAlias<mnemonic # "\t$ZTt, $Zt",
                 (!cast<Instruction>(NAME) ZTR:$ZTt, 0, ZPRAny:$Zt), 1>;
}

//===----------------------------------------------------------------------===//
// SME2 lookup table expand one register
class sme2_luti_vector_index<bits<2> sz, bits<7> opc, RegisterOperand vector_ty,
                             AsmVectorIndexOpnd index_ty, string mnemonic>
    : I<(outs vector_ty:$Zd),
        (ins ZTR:$ZTt, ZPRAny:$Zn, index_ty:$i),
        mnemonic, "\t$Zd, $ZTt, $Zn$i",
        "", []>, Sched<[]> {
  bits<5> Zn;
  bits<5> Zd;
  let Inst{31-19} = 0b1100000011001;
  let Inst{18-14} = opc{6-2};
  let Inst{13-12} = sz;
  let Inst{11-10} = opc{1-0};
  let Inst{9-5}   = Zn;
  let Inst{4-0}   = Zd;
}

class sme2_luti2_vector_index<bits<2> sz, RegisterOperand vector_ty,
                              string mnemonic>
    : sme2_luti_vector_index<sz, {1,?,?,?,?,0,0}, vector_ty, VectorIndexB32b_timm, mnemonic> {
  bits<4> i;
  let Inst{17-14} = i;
}

multiclass sme2_luti2_vector_index<string mnemonic, SDPatternOperator intrinsic> {
  def _B : sme2_luti2_vector_index<0b00, ZPR8, mnemonic>;
  def _H : sme2_luti2_vector_index<0b01, ZPR16, mnemonic>;
  def _S : sme2_luti2_vector_index<0b10, ZPR32, mnemonic>;

  def : Pat<(nxv16i8 (intrinsic (imm_to_zt untyped:$zt), nxv16i8:$zn, (i32 VectorIndexB32b_timm:$imm))),
             (!cast<Instruction>(NAME # _B) $zt, nxv16i8:$zn, (i32 VectorIndexB32b_timm:$imm))>;
  def : Pat<(nxv8i16 (intrinsic (imm_to_zt untyped:$zt), nxv16i8:$zn, (i32 VectorIndexB32b_timm:$imm))),
             (!cast<Instruction>(NAME # _H) $zt, nxv16i8:$zn, (i32 VectorIndexB32b_timm:$imm))>;
  def : Pat<(nxv4i32 (intrinsic (imm_to_zt untyped:$zt), nxv16i8:$zn, (i32 VectorIndexB32b_timm:$imm))),
             (!cast<Instruction>(NAME # _S) $zt, nxv16i8:$zn, (i32 VectorIndexB32b_timm:$imm))>;
  def : Pat<(nxv8f16 (intrinsic (imm_to_zt untyped:$zt), nxv16i8:$zn, (i32 VectorIndexB32b_timm:$imm))),
             (!cast<Instruction>(NAME # _H) $zt, nxv16i8:$zn, (i32 VectorIndexB32b_timm:$imm))>;
  def : Pat<(nxv8bf16 (intrinsic (imm_to_zt untyped:$zt), nxv16i8:$zn, (i32 VectorIndexB32b_timm:$imm))),
             (!cast<Instruction>(NAME # _H) $zt, nxv16i8:$zn, (i32 VectorIndexB32b_timm:$imm))>;
  def : Pat<(nxv4f32 (intrinsic (imm_to_zt untyped:$zt), nxv16i8:$zn, (i32 VectorIndexB32b_timm:$imm))),
             (!cast<Instruction>(NAME # _S) $zt, nxv16i8:$zn, (i32 VectorIndexB32b_timm:$imm))>;
}

class sme2_luti4_vector_index<bits<2> sz, RegisterOperand vector_ty,
                              string mnemonic>
    : sme2_luti_vector_index<sz, {0,1,?,?,?,0,0}, vector_ty, VectorIndexH32b_timm, mnemonic> {
  bits<3> i;
  let Inst{16-14} = i;
}

multiclass sme2_luti4_vector_index<string mnemonic, SDPatternOperator intrinsic> {
  def _B : sme2_luti4_vector_index<0b00, ZPR8, mnemonic>;
  def _H : sme2_luti4_vector_index<0b01, ZPR16, mnemonic>;
  def _S : sme2_luti4_vector_index<0b10, ZPR32, mnemonic>;

  def : Pat<(nxv16i8 (intrinsic (imm_to_zt untyped:$zt), nxv16i8:$zn, (i32 VectorIndexH32b_timm:$imm))),
             (!cast<Instruction>(NAME # _B) $zt, nxv16i8:$zn, (i32 VectorIndexH32b_timm:$imm))>;
  def : Pat<(nxv8i16 (intrinsic (imm_to_zt untyped:$zt), nxv16i8:$zn, (i32 VectorIndexH32b_timm:$imm))),
             (!cast<Instruction>(NAME # _H) $zt, nxv16i8:$zn, (i32 VectorIndexH32b_timm:$imm))>;
  def : Pat<(nxv4i32 (intrinsic (imm_to_zt untyped:$zt), nxv16i8:$zn, (i32 VectorIndexH32b_timm:$imm))),
             (!cast<Instruction>(NAME # _S) $zt, nxv16i8:$zn, (i32 VectorIndexH32b_timm:$imm))>;
  def : Pat<(nxv8f16 (intrinsic (imm_to_zt untyped:$zt), nxv16i8:$zn, (i32 VectorIndexH32b_timm:$imm))),
             (!cast<Instruction>(NAME # _H) $zt, nxv16i8:$zn, (i32 VectorIndexH32b_timm:$imm))>;
  def : Pat<(nxv8bf16 (intrinsic (imm_to_zt untyped:$zt), nxv16i8:$zn, (i32 VectorIndexH32b_timm:$imm))),
             (!cast<Instruction>(NAME # _H) $zt, nxv16i8:$zn, (i32 VectorIndexH32b_timm:$imm))>;
  def : Pat<(nxv4f32 (intrinsic (imm_to_zt untyped:$zt), nxv16i8:$zn, (i32 VectorIndexH32b_timm:$imm))),
             (!cast<Instruction>(NAME # _S) $zt, nxv16i8:$zn, (i32 VectorIndexH32b_timm:$imm))>;
}

// SME2 lookup table expand two contiguous registers
class sme2_luti_vector_vg2_index<bits<2> sz, bits<6> opc, RegisterOperand vector_ty,
                                 AsmVectorIndexOpnd index_ty, string mnemonic>
    : I<(outs vector_ty:$Zd),
        (ins ZTR:$ZTt, ZPRAny:$Zn, index_ty:$i),
        mnemonic, "\t$Zd, $ZTt, $Zn$i",
        "", []>, Sched<[]> {
  bits<5> Zn;
  bits<4> Zd;
  let Inst{31-19} = 0b1100000010001;
  let Inst{18-15} = opc{5-2};
  let Inst{14}    = 0b1;
  let Inst{13-12} = sz;
  let Inst{11-10} = opc{1-0};
  let Inst{9-5}   = Zn;
  let Inst{4-1}   = Zd;
  let Inst{0}     = 0b0;
}

class sme2_luti2_vector_vg2_index<bits<2> sz, RegisterOperand vector_ty,
                                  string mnemonic>
    : sme2_luti_vector_vg2_index<sz, {1,?,?,?,0,0}, vector_ty, VectorIndexH, mnemonic> {
  bits<3> i;
  let Inst{17-15} = i;
}

multiclass sme2_luti2_vector_vg2_index<string mnemonic> {
  def _B : sme2_luti2_vector_vg2_index<0b00, ZZ_b_mul_r, mnemonic>;
  def _H : sme2_luti2_vector_vg2_index<0b01, ZZ_h_mul_r, mnemonic>;
  def _S : sme2_luti2_vector_vg2_index<0b10, ZZ_s_mul_r, mnemonic>;
}

class sme2_luti4_vector_vg2_index<bits<2> sz, RegisterOperand vector_ty,
                                 string mnemonic>
    : sme2_luti_vector_vg2_index<sz, {0,1,?,?,0,0}, vector_ty, VectorIndexS, mnemonic> {
  bits<2> i;
  let Inst{16-15} = i;
}

multiclass sme2_luti4_vector_vg2_index<string mnemonic> {
  def _B : sme2_luti4_vector_vg2_index<0b00, ZZ_b_mul_r, mnemonic>;
  def _H : sme2_luti4_vector_vg2_index<0b01, ZZ_h_mul_r, mnemonic>;
  def _S : sme2_luti4_vector_vg2_index<0b10, ZZ_s_mul_r, mnemonic>;
}

// SME2 lookup table expand four contiguous registers
class sme2_luti_vector_vg4_index<bits<2> sz, bits<5>opc, RegisterOperand vector_ty,
                                 AsmVectorIndexOpnd index_ty, string mnemonic>
    : I<(outs vector_ty:$Zd),
        (ins ZTR:$ZTt, ZPRAny:$Zn, index_ty:$i),
        mnemonic, "\t$Zd, $ZTt, $Zn$i",
        "", []>, Sched<[]> {
  bits<5> Zn;
  bits<3> Zd;
  let Inst{31-19} = 0b1100000010001;
  let Inst{18-16} = opc{4-2};
  let Inst{15-14} = 0b10;
  let Inst{13-12} = sz;
  let Inst{11-10} = opc{1-0};
  let Inst{9-5}   = Zn;
  let Inst{4-2}   = Zd;
  let Inst{1-0}   = 0b00;
}

class sme2_luti2_vector_vg4_index<bits<2> sz, RegisterOperand vector_ty,
                                  string mnemonic>
    : sme2_luti_vector_vg4_index<sz, {1,?,?,0,0}, vector_ty, VectorIndexS, mnemonic> {
  bits<2> i;
  let Inst{17-16} = i;
}

multiclass sme2_luti2_vector_vg4_index<string mnemonic> {
  def _B : sme2_luti2_vector_vg4_index<0b00, ZZZZ_b_mul_r, mnemonic>;
  def _H : sme2_luti2_vector_vg4_index<0b01, ZZZZ_h_mul_r, mnemonic>;
  def _S : sme2_luti2_vector_vg4_index<0b10, ZZZZ_s_mul_r, mnemonic>;
}

class sme2_luti4_vector_vg4_index<bits<2> sz, RegisterOperand vector_ty,
                                  string mnemonic>
    : sme2_luti_vector_vg4_index<sz, {0,1,?,0,0}, vector_ty, VectorIndexD, mnemonic> {
  bits<1> i;
  let Inst{16}    = i;
}

multiclass sme2_luti4_vector_vg4_index<string mnemonic> {
  def _H : sme2_luti4_vector_vg4_index<0b01, ZZZZ_h_mul_r, mnemonic>;
  def _S : sme2_luti4_vector_vg4_index<0b10, ZZZZ_s_mul_r, mnemonic>;
}

//===----------------------------------------------------------------------===//
// SME2 MOV
class sme2_mova_vec_to_tile_vg2_multi_base<bits<2> sz, bit v,
                                           RegisterOperand tile_ty,
                                           Operand index_ty,
                                           RegisterOperand vector_ty,
                                           string mnemonic>
   : I<(outs tile_ty:$ZAd),
       (ins tile_ty:$_ZAd, MatrixIndexGPR32Op12_15:$Rs, index_ty:$imm, vector_ty:$Zn),
       mnemonic, "\t$ZAd[$Rs, $imm], $Zn",
       "", []>, Sched<[]> {
  bits<2> Rs;
  bits<4> Zn;
  let Inst{31-24} = 0b11000000;
  let Inst{23-22} = sz;
  let Inst{21-16} = 0b000100;
  let Inst{15}    = v;
  let Inst{14-13} = Rs;
  let Inst{12-10} = 0b000;
  let Inst{9-6}   = Zn;
  let Inst{5-3}   = 0b000;

  let Constraints = "$ZAd = $_ZAd";
}

multiclass sme2_mova_vec_to_tile_or_array_aliases<int prefer, Instruction inst,
                                                  RegisterOperand tile_or_array_ty,
                                                  RegisterOperand  rv_ty,
                                                  Operand index_ty,
                                                  RegisterOperand vector_ty,
                                                  string mnemonic,
                                                  string vg_acronym=""> {
  def : InstAlias<mnemonic # "\t$ZAd[$Rs, $imm" # !if(!eq(vg_acronym, ""), "", ", " # vg_acronym) # "], $Zn",
                  (inst tile_or_array_ty:$ZAd, rv_ty:$Rs, index_ty:$imm, vector_ty:$Zn), prefer>;

}

// SME2 move vector to tile, two registers
multiclass sme2_mova_vec_to_tile_vg2_multi_base<bit v, string mnemonic, SDPatternOperator intrinsic> {

  def _B : sme2_mova_vec_to_tile_vg2_multi_base<0b00, v,
                                                !if(v, TileVectorOpV8,
                                                       TileVectorOpH8),
                                                uimm3s2range,  ZZ_b_mul_r,
                                                mnemonic>, SMEPseudo2Instr<NAME # _B, 1> {
    bits<3> imm;
    let Inst{2-0} = imm;
  }

  def _H : sme2_mova_vec_to_tile_vg2_multi_base<0b01, v,
                                                !if(v, TileVectorOpV16,
                                                       TileVectorOpH16),
                                                uimm2s2range, ZZ_h_mul_r,
                                                mnemonic>, SMEPseudo2Instr<NAME # _H, 1> {
    bits<1> ZAd;
    bits<2> imm;
    let Inst{2}   = ZAd;
    let Inst{1-0} = imm;
  }

  def _S : sme2_mova_vec_to_tile_vg2_multi_base<0b10, v,
                                                !if(v, TileVectorOpV32,
                                                       TileVectorOpH32),
                                                 uimm1s2range, ZZ_s_mul_r,
                                                 mnemonic>, SMEPseudo2Instr<NAME # _S, 1> {
    bits<2> ZAd;
    bits<1> imm;
    let Inst{2-1} = ZAd;
    let Inst{0}   = imm;
  }

  def _D : sme2_mova_vec_to_tile_vg2_multi_base<0b11, v,
                                                !if(v, TileVectorOpV64,
                                                       TileVectorOpH64),
                                                uimm0s2range, ZZ_d_mul_r,
                                                mnemonic>, SMEPseudo2Instr<NAME # _D, 1> {
    bits<3> ZAd;
    let Inst{2-0} = ZAd;
   }

  def NAME # _B_PSEUDO : sme2_move_to_tile_pseudo<NAME # _B, sme_elm_idx0_0, uimm3s2range, ZZ_b_mul_r, SMEMatrixTileB>;
  def NAME # _H_PSEUDO : sme2_move_to_tile_pseudo<NAME # _H, sme_elm_idx0_1, uimm2s2range, ZZ_h_mul_r, SMEMatrixTileH>;
  def NAME # _S_PSEUDO : sme2_move_to_tile_pseudo<NAME # _S, sme_elm_idx0_3, uimm1s2range, ZZ_s_mul_r, SMEMatrixTileS>;
  def NAME # _D_PSEUDO : sme2_move_to_tile_pseudo<NAME # _D, sme_elm_idx0_7, uimm0s2range, ZZ_d_mul_r, SMEMatrixTileD>;

  def : SME2_Tile_VG2_Multi_Pat<NAME # _B, intrinsic, sme_elm_idx0_0, nxv16i8, uimm3s2range, tileslicerange3s2>;
  def : SME2_Tile_VG2_Multi_Pat<NAME # _H, intrinsic, sme_elm_idx0_1, nxv8i16, uimm2s2range, tileslicerange2s2>;
  def : SME2_Tile_VG2_Multi_Pat<NAME # _H, intrinsic, sme_elm_idx0_1, nxv8f16, uimm2s2range, tileslicerange2s2>;
  def : SME2_Tile_VG2_Multi_Pat<NAME # _H, intrinsic, sme_elm_idx0_1, nxv8bf16, uimm2s2range, tileslicerange2s2>;
  def : SME2_Tile_VG2_Multi_Pat<NAME # _S, intrinsic, sme_elm_idx0_3, nxv4i32, uimm1s2range, tileslicerange1s2>;
  def : SME2_Tile_VG2_Multi_Pat<NAME # _S, intrinsic, sme_elm_idx0_3, nxv4f32, uimm1s2range, tileslicerange1s2>;
  def : SME2_Tile_VG2_Multi_Pat<NAME # _D, intrinsic, sme_elm_idx0_7, nxv2i64, uimm0s2range, tileslicerange0s2>;
  def : SME2_Tile_VG2_Multi_Pat<NAME # _D, intrinsic, sme_elm_idx0_7, nxv2f64, uimm0s2range, tileslicerange0s2>;

  defm : sme2_mova_vec_to_tile_or_array_aliases<1, !cast<Instruction>(NAME # _B),
                                                !if(v, TileVectorOpV8,
                                                       TileVectorOpH8),
                                                MatrixIndexGPR32Op12_15,
                                                uimm3s2range,  ZZ_b_mul_r,
                                                "mov">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<1, !cast<Instruction>(NAME # _H),
                                                !if(v, TileVectorOpV16,
                                                       TileVectorOpH16),
                                                MatrixIndexGPR32Op12_15,
                                                uimm2s2range,  ZZ_h_mul_r,
                                                "mov">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<1, !cast<Instruction>(NAME # _S),
                                                !if(v, TileVectorOpV32,
                                                       TileVectorOpH32),
                                                MatrixIndexGPR32Op12_15,
                                                uimm1s2range,  ZZ_s_mul_r,
                                                "mov">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<1, !cast<Instruction>(NAME # _D),
                                                !if(v, TileVectorOpV64,
                                                       TileVectorOpH64),
                                                MatrixIndexGPR32Op12_15,
                                                uimm0s2range,  ZZ_d_mul_r,
                                                "mov">;

  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME # _B),
                                                !if(v, TileVectorOpV8,
                                                       TileVectorOpH8),
                                                MatrixIndexGPR32Op12_15,
                                                uimm3s2range,  ZZ_b_mul_r,
                                                "mova">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME # _H),
                                                !if(v, TileVectorOpV16,
                                                       TileVectorOpH16),
                                                MatrixIndexGPR32Op12_15,
                                                uimm2s2range,  ZZ_h_mul_r,
                                                "mova">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME # _S),
                                                !if(v, TileVectorOpV32,
                                                       TileVectorOpH32),
                                                MatrixIndexGPR32Op12_15,
                                                uimm1s2range,  ZZ_s_mul_r,
                                                "mova">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME # _D),
                                                !if(v, TileVectorOpV64,
                                                       TileVectorOpH64),
                                                MatrixIndexGPR32Op12_15,
                                                uimm0s2range,  ZZ_d_mul_r,
                                                "mova">;

  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME # _B),
                                                !if(v, TileVectorOpV8,
                                                       TileVectorOpH8),
                                                MatrixIndexGPR32Op12_15,
                                                uimm3s2range,  ZZ_b_mul_r,
                                                "mova">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME # _H),
                                                !if(v, TileVectorOpV16,
                                                       TileVectorOpH16),
                                                MatrixIndexGPR32Op12_15,
                                                uimm2s2range,  ZZ_h_mul_r,
                                                "mova">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME # _S),
                                                !if(v, TileVectorOpV32,
                                                       TileVectorOpH32),
                                                MatrixIndexGPR32Op12_15,
                                                uimm1s2range,  ZZ_s_mul_r,
                                                "mova">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME # _D),
                                                !if(v, TileVectorOpV64,
                                                       TileVectorOpH64),
                                                MatrixIndexGPR32Op12_15,
                                                uimm0s2range,  ZZ_d_mul_r,
                                                "mova">;
}

multiclass sme2_mova_vec_to_tile_vg2_multi<string mnemonic,
                                           SDPatternOperator int_h, SDPatternOperator int_v>{
 defm _H : sme2_mova_vec_to_tile_vg2_multi_base<0b0, mnemonic, int_h>;
 defm _V : sme2_mova_vec_to_tile_vg2_multi_base<0b1, mnemonic, int_v>;
}

class sme2_mova_vec_to_tile_vg4_multi_base<bits<2> sz, bit v, bits<3> op,
                                           RegisterOperand tile_ty,
                                           Operand index_ty,
                                           RegisterOperand vector_ty,
                                           string mnemonic>
   : I<(outs tile_ty:$ZAd),
       (ins tile_ty:$_ZAd, MatrixIndexGPR32Op12_15:$Rs, index_ty:$imm,
            vector_ty:$Zn),
       mnemonic,
       "\t$ZAd[$Rs, $imm], $Zn",
       "", []>, Sched<[]> {
  bits<2> Rs;
  bits<3> Zn;
  let Inst{31-24} = 0b11000000;
  let Inst{23-22} = sz;
  let Inst{21-16} = 0b000100;
  let Inst{15}    = v;
  let Inst{14-13} = Rs;
  let Inst{12-10} = 0b001;
  let Inst{9-7}   = Zn;
  let Inst{6-3}   = 0b0000;
  let Inst{2-0}   = op;
  let Constraints = "$ZAd = $_ZAd";
}

// SME2 move vector to tile, four registers
multiclass sme2_mova_vec_to_tile_vg4_multi_base<bit v, string mnemonic, SDPatternOperator intrinsic> {

  def _B : sme2_mova_vec_to_tile_vg4_multi_base<0b00, v, {0,?,?},
                                                !if(v, TileVectorOpV8,
                                                       TileVectorOpH8),
                                                uimm2s4range, ZZZZ_b_mul_r,
                                                mnemonic>, SMEPseudo2Instr<NAME # _B, 1> {
    bits<2> imm;
    let Inst{1-0} = imm;
  }

  def _H : sme2_mova_vec_to_tile_vg4_multi_base<0b01, v, {0,?,?},
                                                !if(v, TileVectorOpV16,
                                                       TileVectorOpH16),
                                                uimm1s4range, ZZZZ_h_mul_r,
                                                mnemonic>, SMEPseudo2Instr<NAME # _H, 1> {
    bits<1> ZAd;
    bits<1> imm;
    let Inst{1}   = ZAd;
    let Inst{0}   = imm;
  }

  def _S : sme2_mova_vec_to_tile_vg4_multi_base<0b10, v, {0,?,?},
                                                !if(v, TileVectorOpV32,
                                                       TileVectorOpH32),
                                                 uimm0s4range, ZZZZ_s_mul_r,
                                                 mnemonic>, SMEPseudo2Instr<NAME # _S, 1> {
    bits<2> ZAd;
    let Inst{1-0} = ZAd;
  }

  def _D : sme2_mova_vec_to_tile_vg4_multi_base<0b11, v, {?,?,?},
                                                !if(v, TileVectorOpV64,
                                                       TileVectorOpH64),
                                                uimm0s4range, ZZZZ_d_mul_r,
                                                mnemonic>, SMEPseudo2Instr<NAME # _D, 1> {
    bits<3> ZAd;
    let Inst{2-0} = ZAd;
  }

  def NAME # _B_PSEUDO : sme2_move_to_tile_pseudo<NAME # _B, sme_elm_idx0_0, uimm2s4range, ZZZZ_b_mul_r, SMEMatrixTileB>;
  def NAME # _H_PSEUDO : sme2_move_to_tile_pseudo<NAME # _H, sme_elm_idx0_1, uimm1s4range, ZZZZ_h_mul_r, SMEMatrixTileH>;
  def NAME # _S_PSEUDO : sme2_move_to_tile_pseudo<NAME # _S, sme_elm_idx0_3, uimm0s4range, ZZZZ_s_mul_r, SMEMatrixTileS>;
  def NAME # _D_PSEUDO : sme2_move_to_tile_pseudo<NAME # _D, sme_elm_idx0_7, uimm0s4range, ZZZZ_d_mul_r, SMEMatrixTileD>;

  def : SME2_Tile_VG4_Multi_Pat<NAME # _B, intrinsic, sme_elm_idx0_0, nxv16i8, uimm2s4range, tileslicerange2s4>;
  def : SME2_Tile_VG4_Multi_Pat<NAME # _H, intrinsic, sme_elm_idx0_1, nxv8i16, uimm1s4range, tileslicerange1s4>;
  def : SME2_Tile_VG4_Multi_Pat<NAME # _H, intrinsic, sme_elm_idx0_1, nxv8f16, uimm1s4range, tileslicerange1s4>;
  def : SME2_Tile_VG4_Multi_Pat<NAME # _H, intrinsic, sme_elm_idx0_1, nxv8bf16, uimm1s4range, tileslicerange1s4>;
  def : SME2_Tile_VG4_Multi_Pat<NAME # _S, intrinsic, sme_elm_idx0_3, nxv4i32, uimm0s4range, tileslicerange0s4>;
  def : SME2_Tile_VG4_Multi_Pat<NAME # _S, intrinsic, sme_elm_idx0_3, nxv4f32, uimm0s4range, tileslicerange0s4>;
  def : SME2_Tile_VG4_Multi_Pat<NAME # _D, intrinsic, sme_elm_idx0_7, nxv2i64, uimm0s4range, tileslicerange0s4>;
  def : SME2_Tile_VG4_Multi_Pat<NAME # _D, intrinsic, sme_elm_idx0_7, nxv2f64, uimm0s4range, tileslicerange0s4>;

  defm : sme2_mova_vec_to_tile_or_array_aliases<1, !cast<Instruction>(NAME # _B),
                                                !if(v, TileVectorOpV8,
                                                       TileVectorOpH8),
                                                MatrixIndexGPR32Op12_15,
                                                uimm2s4range, ZZZZ_b_mul_r,
                                                "mov">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<1, !cast<Instruction>(NAME # _H),
                                                !if(v, TileVectorOpV16,
                                                       TileVectorOpH16),
                                                MatrixIndexGPR32Op12_15,
                                                uimm1s4range, ZZZZ_h_mul_r,
                                                "mov">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<1, !cast<Instruction>(NAME # _S),
                                                !if(v, TileVectorOpV32,
                                                       TileVectorOpH32),
                                                MatrixIndexGPR32Op12_15,
                                                uimm0s4range, ZZZZ_s_mul_r,
                                                "mov">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<1, !cast<Instruction>(NAME # _D),
                                                !if(v, TileVectorOpV64,
                                                       TileVectorOpH64),
                                                MatrixIndexGPR32Op12_15,
                                                uimm0s4range, ZZZZ_d_mul_r,
                                                "mov">;

  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME # _B),
                                                !if(v, TileVectorOpV8,
                                                       TileVectorOpH8),
                                                MatrixIndexGPR32Op12_15,
                                                uimm2s4range, ZZZZ_b_mul_r,
                                                "mova">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME # _H),
                                                !if(v, TileVectorOpV16,
                                                       TileVectorOpH16),
                                                MatrixIndexGPR32Op12_15,
                                                uimm1s4range, ZZZZ_h_mul_r,
                                                "mova">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME # _S),
                                                !if(v, TileVectorOpV32,
                                                       TileVectorOpH32),
                                                MatrixIndexGPR32Op12_15,
                                                uimm0s4range, ZZZZ_s_mul_r,
                                                "mova">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME # _D),
                                                !if(v, TileVectorOpV64,
                                                       TileVectorOpH64),
                                                MatrixIndexGPR32Op12_15,
                                                uimm0s4range, ZZZZ_d_mul_r,
                                                "mova">;

}

multiclass sme2_mova_vec_to_tile_vg4_multi<string mnemonic,
                                           SDPatternOperator int_h, SDPatternOperator int_v>{
 defm _H : sme2_mova_vec_to_tile_vg4_multi_base<0b0, mnemonic, int_h>;
 defm _V : sme2_mova_vec_to_tile_vg4_multi_base<0b1, mnemonic, int_v>;
}

// SME Move into Array
class sme2_mova_vec_to_array_vg24_multi< bits<5> op, RegisterOperand array_ty,
                                        RegisterOperand vector_ty,
                                        string mnemonic,
                                        string vg_acronym="">
   : I<(outs array_ty:$ZAd),
       (ins array_ty:$_ZAd, MatrixIndexGPR32Op8_11:$Rs, sme_elm_idx0_7:$imm,
            vector_ty:$Zn),
       mnemonic, "\t$ZAd[$Rs, $imm, " # vg_acronym # "], $Zn",
       "", []>, Sched<[]> {
  bits<2> Rs;
  bits<3> imm;
  let Inst{31-15} = 0b11000000000001000;
  let Inst{14-13} = Rs;
  let Inst{12-11} = 0b01;
  let Inst{10-6}  = op;
  let Inst{5-3}   = 0b000;
  let Inst{2-0}   = imm;

  let Constraints = "$ZAd = $_ZAd";
}

// MOVA (vector to array, two registers)
multiclass sme2_mova_vec_to_array_vg2_multi<string mnemonic, SDPatternOperator intrinsic> {
  def NAME : sme2_mova_vec_to_array_vg24_multi<{0,?,?,?,?}, MatrixOp64,
                                               ZZ_d_mul_r, mnemonic, "vgx2">, SMEPseudo2Instr<NAME, 1> {
   bits<4> Zn;
   let Inst{9-6} = Zn;
  }

  def NAME # _PSEUDO : sme2_move_to_za_pseudo<NAME, sme_elm_idx0_7, ZZ_d_mul_r, SMEMatrixArray>;

  def : SME2_ZA_VG1x2_Multi_Pat<NAME, intrinsic, nxv16i8,  sme_elm_idx0_7, tileslice16>;
  def : SME2_ZA_VG1x2_Multi_Pat<NAME, intrinsic, nxv8i16,  sme_elm_idx0_7, tileslice16>;
  def : SME2_ZA_VG1x2_Multi_Pat<NAME, intrinsic, nxv8f16,  sme_elm_idx0_7, tileslice16>;
  def : SME2_ZA_VG1x2_Multi_Pat<NAME, intrinsic, nxv8bf16, sme_elm_idx0_7, tileslice16>;
  def : SME2_ZA_VG1x2_Multi_Pat<NAME, intrinsic, nxv4i32,  sme_elm_idx0_7, tileslice16>;
  def : SME2_ZA_VG1x2_Multi_Pat<NAME, intrinsic, nxv4f32,  sme_elm_idx0_7, tileslice16>;
  def : SME2_ZA_VG1x2_Multi_Pat<NAME, intrinsic, nxv2i64,  sme_elm_idx0_7, tileslice16>;
  def : SME2_ZA_VG1x2_Multi_Pat<NAME, intrinsic, nxv2f64,  sme_elm_idx0_7, tileslice16>;

  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME),
                                                MatrixOp8,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, ZZ_b_mul_r,
                                                "mova">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME),
                                                MatrixOp16,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, ZZ_h_mul_r,
                                                "mova">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME),
                                                MatrixOp32,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, ZZ_s_mul_r,
                                                "mova">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME),
                                                MatrixOp64,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, ZZ_d_mul_r,
                                                "mova">;

  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME),
                                                MatrixOp8,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, ZZ_b_mul_r,
                                                "mova", "vgx2">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME),
                                                MatrixOp16,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, ZZ_h_mul_r,
                                                "mova", "vgx2">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME),
                                                MatrixOp32,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, ZZ_s_mul_r,
                                                "mova", "vgx2">;

  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME),
                                                MatrixOp8,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, ZZ_b_mul_r,
                                                "mov">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME),
                                                MatrixOp16,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, ZZ_h_mul_r,
                                                "mov">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME),
                                                MatrixOp32,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, ZZ_s_mul_r,
                                                "mov">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME),
                                                MatrixOp64,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, ZZ_d_mul_r,
                                                "mov">;

  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME),
                                                MatrixOp8,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, ZZ_b_mul_r,
                                                "mov", "vgx2">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME),
                                                MatrixOp16,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, ZZ_h_mul_r,
                                                "mov", "vgx2">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME),
                                                MatrixOp32,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, ZZ_s_mul_r,
                                                "mov", "vgx2">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<1, !cast<Instruction>(NAME),
                                                MatrixOp64,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, ZZ_d_mul_r,
                                                "mov", "vgx2">;
}

// MOVA (vector to array, four registers)
multiclass sme2_mova_vec_to_array_vg4_multi<string mnemonic, SDPatternOperator intrinsic> {
  def NAME : sme2_mova_vec_to_array_vg24_multi<{1,?,?,?,0}, MatrixOp64,
                                               ZZZZ_d_mul_r, mnemonic, "vgx4">, SMEPseudo2Instr<NAME, 1> {
    bits<3> Zn;
    let Inst{9-7} = Zn;
  }

  def NAME # _PSEUDO : sme2_move_to_za_pseudo<NAME, sme_elm_idx0_7, ZZZZ_d_mul_r, SMEMatrixArray>;

  def : SME2_ZA_VG1x4_Multi_Pat<NAME, intrinsic, nxv16i8,  sme_elm_idx0_7, tileslice16>;
  def : SME2_ZA_VG1x4_Multi_Pat<NAME, intrinsic, nxv8i16,  sme_elm_idx0_7, tileslice16>;
  def : SME2_ZA_VG1x4_Multi_Pat<NAME, intrinsic, nxv8f16,  sme_elm_idx0_7, tileslice16>;
  def : SME2_ZA_VG1x4_Multi_Pat<NAME, intrinsic, nxv8bf16, sme_elm_idx0_7, tileslice16>;
  def : SME2_ZA_VG1x4_Multi_Pat<NAME, intrinsic, nxv4i32,  sme_elm_idx0_7, tileslice16>;
  def : SME2_ZA_VG1x4_Multi_Pat<NAME, intrinsic, nxv4f32,  sme_elm_idx0_7, tileslice16>;
  def : SME2_ZA_VG1x4_Multi_Pat<NAME, intrinsic, nxv2i64,  sme_elm_idx0_7, tileslice16>;
  def : SME2_ZA_VG1x4_Multi_Pat<NAME, intrinsic, nxv2f64,  sme_elm_idx0_7, tileslice16>;

  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME),
                                                MatrixOp8,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, ZZZZ_b_mul_r,
                                                "mova">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME),
                                                MatrixOp16,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, ZZZZ_h_mul_r,
                                                "mova">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME),
                                                MatrixOp32,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, ZZZZ_s_mul_r,
                                                "mova">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME),
                                                MatrixOp64,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, ZZZZ_d_mul_r,
                                                "mova">;

  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME),
                                                MatrixOp8,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, ZZZZ_b_mul_r,
                                                "mova", "vgx4">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME),
                                                MatrixOp16,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, ZZZZ_h_mul_r,
                                                "mova", "vgx4">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME),
                                                MatrixOp32,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, ZZZZ_s_mul_r,
                                                "mova", "vgx4">;

  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME),
                                                MatrixOp8,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, ZZZZ_b_mul_r,
                                                "mov">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME),
                                                MatrixOp16,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, ZZZZ_h_mul_r,
                                                "mov">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME),
                                                MatrixOp32,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, ZZZZ_s_mul_r,
                                                "mov">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME),
                                                MatrixOp64,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, ZZZZ_d_mul_r,
                                                "mov">;

  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME),
                                                MatrixOp8,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, ZZZZ_b_mul_r,
                                                "mov", "vgx4">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME),
                                                MatrixOp16,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, ZZZZ_h_mul_r,
                                                "mov", "vgx4">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<0, !cast<Instruction>(NAME),
                                                MatrixOp32,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, ZZZZ_s_mul_r,
                                                "mov", "vgx4">;
  defm : sme2_mova_vec_to_tile_or_array_aliases<1, !cast<Instruction>(NAME),
                                                MatrixOp64,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, ZZZZ_d_mul_r,
                                                "mov", "vgx4">;

}

class sme2_mova_tile_to_vec_vg2_multi_base<bits<2> sz, bit v, bits<3> op,
                                           RegisterOperand vector_ty,
                                           RegisterOperand tile_ty,
                                           Operand index_ty,
                                           string mnemonic>
   : I<!if(op{1}, (outs vector_ty:$Zd, tile_ty:$_ZAn), (outs vector_ty:$Zd)),
       (ins tile_ty:$ZAn, MatrixIndexGPR32Op12_15:$Rs, index_ty:$imm),
       mnemonic,
       "\t$Zd, $ZAn[$Rs, $imm]",
       "", []>, Sched<[]> {
  bits<4> Zd;
  bits<2> Rs;
  let Inst{31-24} = 0b11000000;
  let Inst{23-22} = sz;
  let Inst{21-16} = 0b000110;
  let Inst{15}    = v;
  let Inst{14-13} = Rs;
  let Inst{12-11} = 0b00;
  let Inst{10-8}  = op;
  let Inst{4-1}   = Zd;
  let Inst{0}     = 0b0;

  let Constraints = !if(op{1}, "$ZAn = $_ZAn", "");
}

multiclass sme2_mova_tile_or_array_to_vec_aliases<int op, Instruction inst,
                                                  RegisterOperand vector_ty,
                                                  RegisterOperand tile_or_array_ty,
                                                  RegisterOperand rv_ty,
                                                  Operand index_ty,
                                                  string mnemonic,
                                                  string vg_acronym=""> {
def : InstAlias<mnemonic # "\t$Zd, $ZAn[$Rs, $imm" # !if(!eq(vg_acronym, ""), "", ", " # vg_acronym) # "]",
                  (inst vector_ty:$Zd, tile_or_array_ty:$ZAn, rv_ty:$Rs, index_ty:$imm), op>;

}

multiclass sme2_mova_tile_to_vec_vg2_multi_inst<bit v, bits<3> opc, string mnemonic> {

  def _B : sme2_mova_tile_to_vec_vg2_multi_base<0b00, v, opc, ZZ_b_mul_r,
                                                !if(v, TileVectorOpV8,
                                                       TileVectorOpH8),
                                                 uimm3s2range, mnemonic>, SMEPseudo2Instr<NAME # _B, 1> {
    bits<3> imm;
    let Inst{7-5} = imm;
  }

  def _H : sme2_mova_tile_to_vec_vg2_multi_base<0b01, v, opc, ZZ_h_mul_r,
                                                !if(v, TileVectorOpV16,
                                                       TileVectorOpH16),
                                                 uimm2s2range, mnemonic>, SMEPseudo2Instr<NAME # _H, 1> {
    bits<1> ZAn;
    bits<2> imm;
    let Inst{7}   = ZAn;
    let Inst{6-5} = imm;
  }

  def _S : sme2_mova_tile_to_vec_vg2_multi_base<0b10, v, opc, ZZ_s_mul_r,
                                                !if(v, TileVectorOpV32,
                                                       TileVectorOpH32),
                                                 uimm1s2range, mnemonic>, SMEPseudo2Instr<NAME # _S, 1> {
    bits<2> ZAn;
    bits<1> imm;
    let Inst{7-6} = ZAn;
    let Inst{5}   = imm;
  }

  def _D : sme2_mova_tile_to_vec_vg2_multi_base<0b11, v, opc, ZZ_d_mul_r,
                                                !if(v, TileVectorOpV64,
                                                       TileVectorOpH64),
                                                uimm0s2range, mnemonic>, SMEPseudo2Instr<NAME # _D, 1> {
    bits<3> ZAn;
    let Inst{7-5} = ZAn;
  }

  if !eq(mnemonic, "mova") then {
  defm : sme2_mova_tile_or_array_to_vec_aliases<1,!cast<Instruction>(NAME # _B),
                                                ZZ_b_mul_r,
                                               !if(v, TileVectorOpV8,
                                                      TileVectorOpH8),
                                                MatrixIndexGPR32Op12_15,
                                                uimm3s2range, "mov">;
  defm : sme2_mova_tile_or_array_to_vec_aliases<1,!cast<Instruction>(NAME # _H),
                                                ZZ_h_mul_r,
                                                !if(v, TileVectorOpV16,
                                                       TileVectorOpH16),
                                                MatrixIndexGPR32Op12_15,
                                                uimm2s2range, "mov">;
  defm : sme2_mova_tile_or_array_to_vec_aliases<1, !cast<Instruction>(NAME # _S),
                                                ZZ_s_mul_r,
                                                !if(v, TileVectorOpV32,
                                                       TileVectorOpH32),
                                                MatrixIndexGPR32Op12_15,
                                                uimm1s2range, "mov">;
  defm : sme2_mova_tile_or_array_to_vec_aliases<1, !cast<Instruction>(NAME # _D),
                                                ZZ_d_mul_r,
                                                !if(v, TileVectorOpV64,
                                                       TileVectorOpH64),
                                                MatrixIndexGPR32Op12_15,
                                                uimm0s2range, "mov">;
  }

  defm : sme2_mova_tile_or_array_to_vec_aliases<0,!cast<Instruction>(NAME # _B),
                                                ZZ_b_mul_r,
                                               !if(v, TileVectorOpV8,
                                                      TileVectorOpH8),
                                                MatrixIndexGPR32Op12_15,
                                                uimm3s2range, mnemonic>;
  defm : sme2_mova_tile_or_array_to_vec_aliases<0,!cast<Instruction>(NAME # _H),
                                                ZZ_h_mul_r,
                                                !if(v, TileVectorOpV16,
                                                       TileVectorOpH16),
                                                MatrixIndexGPR32Op12_15,
                                                uimm2s2range, mnemonic>;
  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME # _S),
                                                ZZ_s_mul_r,
                                                !if(v, TileVectorOpV32,
                                                       TileVectorOpH32),
                                                MatrixIndexGPR32Op12_15,
                                                uimm1s2range, mnemonic>;
  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME # _D),
                                                ZZ_d_mul_r,
                                                !if(v, TileVectorOpV64,
                                                       TileVectorOpH64),
                                                MatrixIndexGPR32Op12_15,
                                                uimm0s2range, mnemonic>;

}

// SME2 move tile to vector, two registers
multiclass sme2_mova_tile_to_vec_vg2_multi<string mnemonic>{
 defm _H : sme2_mova_tile_to_vec_vg2_multi_inst<0b0, 0b000, mnemonic>;
 defm _V : sme2_mova_tile_to_vec_vg2_multi_inst<0b1, 0b000, mnemonic>;
}


// SME2p1 move tile to vector and zero tile, two registers
multiclass sme2p1_movaz_tile_to_vec_vg2<string mnemonic>{
 defm _H : sme2_mova_tile_to_vec_vg2_multi_inst<0b0, 0b010, mnemonic>;
 defm _V : sme2_mova_tile_to_vec_vg2_multi_inst<0b1, 0b010, mnemonic>;


 def NAME # _H_B_PSEUDO : sme2_movez_to_tile_pseudo<NAME # _H_B, sme_elm_idx0_0, uimm3s2range, ZZ_b_mul_r, SMEMatrixTileB>;
 def NAME # _H_H_PSEUDO : sme2_movez_to_tile_pseudo<NAME # _H_H, sme_elm_idx0_1, uimm2s2range, ZZ_h_mul_r, SMEMatrixTileH>;
 def NAME # _H_S_PSEUDO : sme2_movez_to_tile_pseudo<NAME # _H_S, sme_elm_idx0_3, uimm1s2range, ZZ_s_mul_r, SMEMatrixTileS>;
 def NAME # _H_D_PSEUDO : sme2_movez_to_tile_pseudo<NAME # _H_D, sme_elm_idx0_7, uimm0s2range, ZZ_d_mul_r, SMEMatrixTileD>;

 def NAME # _V_B_PSEUDO : sme2_movez_to_tile_pseudo<NAME # _V_B, sme_elm_idx0_0, uimm3s2range, ZZ_b_mul_r, SMEMatrixTileB>;
 def NAME # _V_H_PSEUDO : sme2_movez_to_tile_pseudo<NAME # _V_H, sme_elm_idx0_1, uimm2s2range, ZZ_h_mul_r, SMEMatrixTileH>;
 def NAME # _V_S_PSEUDO : sme2_movez_to_tile_pseudo<NAME # _V_S, sme_elm_idx0_3, uimm1s2range, ZZ_s_mul_r, SMEMatrixTileS>;
 def NAME # _V_D_PSEUDO : sme2_movez_to_tile_pseudo<NAME # _V_D, sme_elm_idx0_7, uimm0s2range, ZZ_d_mul_r, SMEMatrixTileD>;
}

class sme2_mova_tile_to_vec_vg4_multi_base<bits<2> sz, bit v, bits<6> op,
                                           RegisterOperand vector_ty,
                                           RegisterOperand tile_ty,
                                           Operand index_ty,
                                           string mnemonic>
   : I<!if(op{4}, (outs vector_ty:$Zd, tile_ty:$_ZAn), (outs vector_ty:$Zd)),
       (ins tile_ty:$ZAn, MatrixIndexGPR32Op12_15:$Rs, index_ty:$imm),
       mnemonic,
       "\t$Zd, $ZAn[$Rs, $imm]",
       "", []>, Sched<[]> {
  bits<3> Zd;
  bits<2> Rs;
  let Inst{31-24} = 0b11000000;
  let Inst{23-22} = sz;
  let Inst{21-16} = 0b000110;
  let Inst{15}    = v;
  let Inst{14-13} = Rs;
  let Inst{12-11} = 0b00;
  let Inst{10-5}  = op{5-0};
  let Inst{4-2}   = Zd;
  let Inst{1-0}   = 0b00;

  let Constraints = !if(op{4}, "$ZAn = $_ZAn", "");
}

multiclass sme2_mova_tile_to_vec_vg4_multi_base<bit v, bits<3> opc, string mnemonic> {

  def _B : sme2_mova_tile_to_vec_vg4_multi_base<0b00, v, {opc,0,?,?},
                                                ZZZZ_b_mul_r,
                                                !if(v, TileVectorOpV8,
                                                       TileVectorOpH8),
                                                uimm2s4range, mnemonic>, SMEPseudo2Instr<NAME # _B, 1> {
    bits<2> imm;
    let Inst{6-5} = imm;
  }

  def _H : sme2_mova_tile_to_vec_vg4_multi_base<0b01, v, {opc,0,?,?},
                                                ZZZZ_h_mul_r,
                                                !if(v, TileVectorOpV16,
                                                       TileVectorOpH16),
                                                uimm1s4range, mnemonic>, SMEPseudo2Instr<NAME # _H, 1> {
    bits<1> ZAn;
    bits<1> imm;
    let Inst{6}   = ZAn;
    let Inst{5}   = imm;
  }

  def _S : sme2_mova_tile_to_vec_vg4_multi_base<0b10, v, {opc,0,?,?},
                                                ZZZZ_s_mul_r,
                                                !if(v, TileVectorOpV32,
                                                       TileVectorOpH32),
                                                 uimm0s4range, mnemonic>, SMEPseudo2Instr<NAME # _S, 1> {
    bits<2> ZAn;
    let Inst{6-5} = ZAn;
  }

  def _D : sme2_mova_tile_to_vec_vg4_multi_base<0b11, v, {opc,?,?,?},
                                                ZZZZ_d_mul_r,
                                                !if(v, TileVectorOpV64,
                                                       TileVectorOpH64),
                                                uimm0s4range, mnemonic>, SMEPseudo2Instr<NAME # _D, 1> {
    bits<3> ZAn;
    let Inst{7-5} = ZAn;
  }

  if !eq(mnemonic, "mova") then {
  defm : sme2_mova_tile_or_array_to_vec_aliases<1, !cast<Instruction>(NAME # _B),
                                                ZZZZ_b_mul_r,
                                                !if(v, TileVectorOpV8,
                                                      TileVectorOpH8),
                                                MatrixIndexGPR32Op12_15,
                                                uimm2s4range, "mov">;
  defm : sme2_mova_tile_or_array_to_vec_aliases<1, !cast<Instruction>(NAME # _H),
                                                ZZZZ_h_mul_r,
                                                !if(v, TileVectorOpV16,
                                                       TileVectorOpH16),
                                                MatrixIndexGPR32Op12_15,
                                                uimm1s4range, "mov">;
  defm : sme2_mova_tile_or_array_to_vec_aliases<1, !cast<Instruction>(NAME # _S),
                                                ZZZZ_s_mul_r,
                                                !if(v, TileVectorOpV32,
                                                      TileVectorOpH32),
                                                MatrixIndexGPR32Op12_15,
                                                uimm0s4range, "mov">;
  defm : sme2_mova_tile_or_array_to_vec_aliases<1, !cast<Instruction>(NAME # _D),
                                                ZZZZ_d_mul_r,
                                                !if(v, TileVectorOpV64,
                                                       TileVectorOpH64),
                                                MatrixIndexGPR32Op12_15,
                                                uimm0s4range, "mov">;
  }

  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME # _B),
                                                ZZZZ_b_mul_r,
                                                !if(v, TileVectorOpV8,
                                                       TileVectorOpH8),
                                                MatrixIndexGPR32Op12_15,
                                                uimm2s4range, mnemonic>;
  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME # _H),
                                                ZZZZ_h_mul_r,
                                                !if(v, TileVectorOpV16,
                                                       TileVectorOpH16),
                                                MatrixIndexGPR32Op12_15,
                                                uimm1s4range, mnemonic>;
  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME # _S),
                                                ZZZZ_s_mul_r,
                                                !if(v, TileVectorOpV32,
                                                      TileVectorOpH32),
                                                MatrixIndexGPR32Op12_15,
                                                uimm0s4range, mnemonic>;
  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME # _D),
                                                ZZZZ_d_mul_r,
                                                !if(v, TileVectorOpV64,
                                                       TileVectorOpH64),
                                                MatrixIndexGPR32Op12_15,
                                                uimm0s4range, mnemonic>;

}

// SME2 move tile to vector, four registers
multiclass sme2_mova_tile_to_vec_vg4_multi<string mnemonic>{
 defm _H : sme2_mova_tile_to_vec_vg4_multi_base<0b0, 0b100, mnemonic>;
 defm _V : sme2_mova_tile_to_vec_vg4_multi_base<0b1, 0b100, mnemonic>;
}

// SME2p1 move tile to vector and zero tile, four registers
multiclass sme2p1_movaz_tile_to_vec_vg4<string mnemonic>{
 defm _H : sme2_mova_tile_to_vec_vg4_multi_base<0b0, 0b110, mnemonic>;
 defm _V : sme2_mova_tile_to_vec_vg4_multi_base<0b1, 0b110, mnemonic>;

 def NAME # _H_B_PSEUDO : sme2_movez_to_tile_pseudo<NAME # _H_B, sme_elm_idx0_0, uimm2s4range, ZZZZ_b_mul_r, SMEMatrixTileB>;
 def NAME # _H_H_PSEUDO : sme2_movez_to_tile_pseudo<NAME # _H_H, sme_elm_idx0_1, uimm1s4range, ZZZZ_h_mul_r, SMEMatrixTileH>;
 def NAME # _H_S_PSEUDO : sme2_movez_to_tile_pseudo<NAME # _H_S, sme_elm_idx0_3, uimm0s4range, ZZZZ_s_mul_r, SMEMatrixTileS>;
 def NAME # _H_D_PSEUDO : sme2_movez_to_tile_pseudo<NAME # _H_D, sme_elm_idx0_7, uimm0s4range, ZZZZ_d_mul_r, SMEMatrixTileD>;

 def NAME # _V_B_PSEUDO : sme2_movez_to_tile_pseudo<NAME # _V_B, sme_elm_idx0_0, uimm2s4range, ZZZZ_b_mul_r, SMEMatrixTileB>;
 def NAME # _V_H_PSEUDO : sme2_movez_to_tile_pseudo<NAME # _V_H, sme_elm_idx0_1, uimm1s4range, ZZZZ_h_mul_r, SMEMatrixTileH>;
 def NAME # _V_S_PSEUDO : sme2_movez_to_tile_pseudo<NAME # _V_S, sme_elm_idx0_3, uimm0s4range, ZZZZ_s_mul_r, SMEMatrixTileS>;
 def NAME # _V_D_PSEUDO : sme2_movez_to_tile_pseudo<NAME # _V_D, sme_elm_idx0_7, uimm0s4range, ZZZZ_d_mul_r, SMEMatrixTileD>;
}


class sme2_mova_array_to_vec_vg24_multi<bits<4>op, RegisterOperand vector_ty,
                                        RegisterOperand array_ty,
                                        string mnemonic, string vg_acronym>
   : I<!if(op{2}, (outs vector_ty:$Zd, array_ty:$_ZAn), (outs vector_ty:$Zd)),
       (ins array_ty:$ZAn, MatrixIndexGPR32Op8_11:$Rs, sme_elm_idx0_7:$imm),
       mnemonic,
       "\t$Zd, $ZAn[$Rs, $imm, " # vg_acronym # "]",
       "", []>, Sched<[]> {
  bits<2> Rs;
  bits<3> imm;
  let Inst{31-15} = 0b11000000000001100;
  let Inst{14-13} = Rs;
  let Inst{12-11} = 0b01;
  let Inst{10-8}  = op{3-1};
  let Inst{7-5}   = imm;
  let Inst{1}     = op{0};
  let Inst{0}     = 0b0;
  let Constraints = !if(op{2}, "$ZAn = $_ZAn", "");
}

// move array to vector, two registers.
multiclass sme2_mova_array_to_vec_vg2_multi<bits<3> opc, string mnemonic> {
  def NAME : sme2_mova_array_to_vec_vg24_multi<{opc,?}, ZZ_d_mul_r, MatrixOp64,
                                               mnemonic, "vgx2">, SMEPseudo2Instr<NAME, 1>{
    bits<4> Zd;
    let Inst{4-1} = Zd;
  }

  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME),
                                                ZZ_b_mul_r, MatrixOp8,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, mnemonic>;
  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME),
                                                ZZ_h_mul_r, MatrixOp16,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, mnemonic>;
  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME),
                                                ZZ_s_mul_r, MatrixOp32,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, mnemonic>;
  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME),
                                                ZZ_d_mul_r,  MatrixOp64,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, mnemonic>;

  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME),
                                                ZZ_b_mul_r, MatrixOp8,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, mnemonic, "vgx2">;
  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME),
                                                ZZ_h_mul_r, MatrixOp16,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, mnemonic, "vgx2">;
  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME),
                                                ZZ_s_mul_r, MatrixOp32,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, mnemonic, "vgx2">;

  if !eq(mnemonic, "mova") then {
  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME),
                                                ZZ_b_mul_r, MatrixOp8,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, "mov">;
  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME),
                                                ZZ_h_mul_r, MatrixOp16,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, "mov">;
  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME),
                                                ZZ_s_mul_r, MatrixOp32,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, "mov">;
  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME),
                                                ZZ_d_mul_r,  MatrixOp64,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, "mov">;

  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME),
                                                ZZ_b_mul_r, MatrixOp8,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, "mov", "vgx2">;
  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME),
                                                ZZ_h_mul_r, MatrixOp16,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, "mov", "vgx2">;
  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME),
                                                ZZ_s_mul_r, MatrixOp32,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, "mov", "vgx2">;
  defm : sme2_mova_tile_or_array_to_vec_aliases<1, !cast<Instruction>(NAME),
                                                ZZ_d_mul_r,  MatrixOp64,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, "mov", "vgx2">;
  }
}

multiclass sme2_movaz_array_to_vec_vg2_multi<string mnemonic> {
  defm NAME : sme2_mova_array_to_vec_vg2_multi<0b010, mnemonic>;
  def NAME # _PSEUDO : sme2_movaz_array_to_tile_pseudo<NAME, sme_elm_idx0_7, ZZ_d_mul_r, SMEMatrixArray>;
}

// move array to vector, four registers
multiclass sme2_mova_array_to_vec_vg4_multi<bits<4> opc, string mnemonic> {
  def NAME : sme2_mova_array_to_vec_vg24_multi<opc, ZZZZ_d_mul_r, MatrixOp64,
                                               mnemonic, "vgx4">, SMEPseudo2Instr<NAME, 1> {
    bits<3> Zd;
    let Inst{4-2} = Zd;
  }

  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME),
                                                ZZZZ_b_mul_r, MatrixOp8,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, mnemonic>;
  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME),
                                                ZZZZ_h_mul_r, MatrixOp16,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, mnemonic>;
  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME),
                                                ZZZZ_s_mul_r, MatrixOp32,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, mnemonic>;
  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME),
                                                ZZZZ_d_mul_r, MatrixOp64,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, mnemonic>;

  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME),
                                                ZZZZ_b_mul_r, MatrixOp8,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, mnemonic, "vgx4">;
  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME),
                                                ZZZZ_h_mul_r, MatrixOp16,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, mnemonic, "vgx4">;
  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME),
                                                ZZZZ_s_mul_r, MatrixOp32,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, mnemonic, "vgx4">;

  if !eq(mnemonic, "mova") then {
  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME),
                                                ZZZZ_b_mul_r, MatrixOp8,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, "mov">;
  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME),
                                                ZZZZ_h_mul_r, MatrixOp16,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, "mov">;
  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME),
                                                ZZZZ_s_mul_r, MatrixOp32,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, "mov">;
  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME),
                                                ZZZZ_d_mul_r, MatrixOp64,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, "mov">;

  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME),
                                                ZZZZ_b_mul_r, MatrixOp8,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, "mov", "vgx4">;
  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME),
                                                ZZZZ_h_mul_r, MatrixOp16,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, "mov", "vgx4">;
  defm : sme2_mova_tile_or_array_to_vec_aliases<0, !cast<Instruction>(NAME),
                                                ZZZZ_s_mul_r, MatrixOp32,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, "mov", "vgx4">;
  defm : sme2_mova_tile_or_array_to_vec_aliases<1, !cast<Instruction>(NAME),
                                                ZZZZ_d_mul_r, MatrixOp64,
                                                MatrixIndexGPR32Op8_11,
                                                sme_elm_idx0_7, "mov", "vgx4">;
  }
}

multiclass sme2_movaz_array_to_vec_vg4_multi<string mnemonic> {
  defm NAME : sme2_mova_array_to_vec_vg4_multi<0b1100, mnemonic>;
  def NAME # _PSEUDO : sme2_movaz_array_to_tile_pseudo<NAME, sme_elm_idx0_7, ZZZZ_d_mul_r, SMEMatrixArray>;
}

//===----------------------------------------------------------------------===//
// SME2 multi-vec saturating shift right narrow
class sme2_sat_shift_vector_vg2<string mnemonic, bit op, bit u>
    : I<(outs ZPR16:$Zd), (ins ZZ_s_mul_r:$Zn, tvecshiftR16:$imm4),
        mnemonic, "\t$Zd, $Zn, $imm4",
        "", []>, Sched<[]> {
  bits<4> imm4;
  bits<4> Zn;
  bits<5> Zd;
  let Inst{31-21} = 0b11000001111;
  let Inst{20}    = op;
  let Inst{19-16} = imm4;
  let Inst{15-10} = 0b110101;
  let Inst{9-6}   = Zn;
  let Inst{5}     = u;
  let Inst{4-0}   = Zd;
}

multiclass sme2_sat_shift_vector_vg2<string mnemonic, bit op, bit u, SDPatternOperator intrinsic> {
  def _H : sme2_sat_shift_vector_vg2<mnemonic, op, u>;

  def : SME2_Sat_Shift_VG2_Pat<NAME # _H, intrinsic, nxv8i16, nxv4i32, tvecshiftR16>;
}

class sme2_sat_shift_vector_vg4<bits<2> sz, bits<3> op, ZPRRegOp zpr_ty,
                                RegisterOperand vector_ty, Operand imm_ty,
                                string mnemonic>
    : I<(outs zpr_ty:$Zd), (ins vector_ty:$Zn, imm_ty:$imm),
        mnemonic, "\t$Zd, $Zn, $imm",
        "", []>, Sched<[]> {
  bits<3> Zn;
  bits<5> Zd;
  let Inst{31-24} = 0b11000001;
  let Inst{23-22} = sz;
  let Inst{21}    = 0b1;
  //  Inst{20-16} = imm5;
  let Inst{15-11} = 0b11011;
  let Inst{10}    = op{2};
  let Inst{9-7}   = Zn;
  let Inst{6-5}   = op{1-0};
  let Inst{4-0}   = Zd;
}

multiclass sme2_sat_shift_vector_vg4<string mnemonic, bits<3> op, SDPatternOperator intrinsic> {
  def _B : sme2_sat_shift_vector_vg4<{0,1}, op, ZPR8, ZZZZ_s_mul_r, tvecshiftR32,
                                     mnemonic>{
    bits<5> imm;
    let Inst{20-16} = imm;
  }
  def _H : sme2_sat_shift_vector_vg4<{1,?}, op, ZPR16, ZZZZ_d_mul_r, tvecshiftR64,
                                      mnemonic> {
    bits<6> imm;
    let Inst{22}    = imm{5};
    let Inst{20-16} = imm{4-0};
  }

  def : SME2_Sat_Shift_VG4_Pat<NAME # _B, intrinsic, nxv16i8, nxv4i32, tvecshiftR32>;
  def : SME2_Sat_Shift_VG4_Pat<NAME # _H, intrinsic, nxv8i16, nxv2i64, tvecshiftR64>;
}

//===----------------------------------------------------------------------===//
// SME2 Multi-vector - SVE Select
class sme2_sel_vector_vg24<bits<2> sz, bits<4> op, RegisterOperand vector_ty,
                           string mnemonic>
    : I<(outs vector_ty:$Zd),
        (ins PNRAny_p8to15:$PNg, vector_ty:$Zn, vector_ty:$Zm),
        mnemonic, "\t$Zd, $PNg, $Zn, $Zm",
        "", []>, Sched<[]> {
  bits<3> PNg;
  let Inst{31-24} = 0b11000001;
  let Inst{23-22} = sz;
  let Inst{21}    = 0b1;
  let Inst{17-16} = op{3-2};
  let Inst{15-13} = 0b100;
  let Inst{12-10} = PNg;
  let Inst{6}     = op{1};
  let Inst{5}     = 0b0;
  let Inst{1}     = op{0};
  let Inst{0}     = 0b0;
}

class sme2_sel_vector_vg2<bits<2> sz, RegisterOperand vector_ty,
                          string mnemonic>
     : sme2_sel_vector_vg24<sz, {?,0,?,?}, vector_ty, mnemonic> {
  bits<4> Zm;
  bits<4> Zn;
  bits<4> Zd;
  let Inst{20-17} = Zm;
  let Inst{9-6}   = Zn;
  let Inst{4-1}   = Zd;
}

multiclass sme2_sel_vector_vg2<string mnemonic>{
  def _B : sme2_sel_vector_vg2<0b00, ZZ_b_mul_r, mnemonic>;
  def _H : sme2_sel_vector_vg2<0b01, ZZ_h_mul_r, mnemonic>;
  def _S : sme2_sel_vector_vg2<0b10, ZZ_s_mul_r, mnemonic>;
  def _D : sme2_sel_vector_vg2<0b11, ZZ_d_mul_r, mnemonic>;
}
class sme2_sel_vector_vg4<bits<2> sz, RegisterOperand vector_ty,
                          string mnemonic>
     : sme2_sel_vector_vg24<sz, 0b0100, vector_ty, mnemonic> {
  bits<3> Zm;
  bits<3> Zn;
  bits<3> Zd;
  let Inst{20-18} = Zm;
  let Inst{9-7}   = Zn;
  let Inst{4-2}   = Zd;
}
multiclass sme2_sel_vector_vg4<string mnemonic> {
  def _B : sme2_sel_vector_vg4<0b00, ZZZZ_b_mul_r, mnemonic>;
  def _H : sme2_sel_vector_vg4<0b01, ZZZZ_h_mul_r, mnemonic>;
  def _S : sme2_sel_vector_vg4<0b10, ZZZZ_s_mul_r, mnemonic>;
  def _D : sme2_sel_vector_vg4<0b11, ZZZZ_d_mul_r, mnemonic>;
}

//===----------------------------------------------------------------------===//
// Non contiguous Load and Store

class sme2_ld_vector_vg2_multi_scalar_scalar<bits<2> msz, bit n,
                                             RegisterOperand multi_vector_ty,
                                             RegisterOperand gpr_ty,
                                             string mnemonic>
   : I<(outs multi_vector_ty:$Zt),
       (ins PNRAny_p8to15:$PNg, GPR64sp:$Rn, gpr_ty:$Rm),
       mnemonic, "\t$Zt, $PNg/z, [$Rn, $Rm]",
       "", []>, Sched<[]> {
   bits<5> Rm;
   bits<3> PNg;
   bits<5> Rn;
   bits<4> Zt;
   let Inst{31-21} = 0b10100001000;
   let Inst{20-16} = Rm;
   let Inst{15}    = 0b0;
   let Inst{14-13} = msz;
   let Inst{12-10} = PNg;
   let Inst{9-5}   = Rn;
   let Inst{4}     = Zt{3};
   let Inst{3}     = n;
   let Inst{2-0}   = Zt{2-0};

   let mayLoad = 1;
}

class sme2_ld_vector_vg4_multi_scalar_scalar<bits<2> msz, bit n,
                                             RegisterOperand multi_vector_ty,
                                             RegisterOperand gpr_ty,
                                             string mnemonic>
   : I<(outs multi_vector_ty:$Zt),
       (ins PNRAny_p8to15:$PNg, GPR64sp:$Rn, gpr_ty:$Rm),
       mnemonic, "\t$Zt, $PNg/z, [$Rn, $Rm]",
       "", []>, Sched<[]> {
   bits<5> Rm;
   bits<3> PNg;
   bits<5> Rn;
   bits<3> Zt;
   let Inst{31-21} = 0b10100001000;
   let Inst{20-16} = Rm;
   let Inst{15}    = 0b1;
   let Inst{14-13} = msz;
   let Inst{12-10} = PNg;
   let Inst{9-5}   = Rn;
   let Inst{4}     = Zt{2};
   let Inst{3}     = n;
   let Inst{2}     = 0b0;
   let Inst{1-0}   = Zt{1-0};

   let mayLoad = 1;
}

class sme2_ld_vector_vg24_multi_scalar_immediate<bits<2> msz, bit n, bits<2> op,
                                                 RegisterOperand multi_vector_ty,
                                                 Operand index_ty,
                                                 string mnemonic>
    : I<(outs multi_vector_ty:$Zt),
        (ins PNRAny_p8to15:$PNg, GPR64sp:$Rn, index_ty:$imm4),
        mnemonic,  "\t$Zt, $PNg/z, [$Rn, $imm4, mul vl]",
        "", []>, Sched<[]> {
   bits<4> imm4;
   bits<3> PNg;
   bits<5> Rn;
   let Inst{31-20} = 0b101000010100;
   let Inst{19-16} = imm4;
   let Inst{15}    = op{1};
   let Inst{14-13} = msz;
   let Inst{12-10} = PNg;
   let Inst{9-5}   = Rn;
   let Inst{3}     = n;
   let Inst{2}     = op{0};

   let mayLoad = 1;
}

multiclass sme2_ld_vector_vg2_multi_scalar_immediate<bits<2> msz, bit n,
                                                     RegisterOperand multi_vector_ty,
                                                     Operand index_ty,
                                                     string mnemonic>{
  def NAME : sme2_ld_vector_vg24_multi_scalar_immediate<msz, n, {0,?},
                                                        multi_vector_ty,
                                                        index_ty, mnemonic> {
    bits<4> Zt;
    let Inst{4} = Zt{3};
    let Inst{2-0} = Zt{2-0};
  }

   def : InstAlias<mnemonic # "\t$Zt, $PNg/z, [$Rn]",
                  (!cast<Instruction>(NAME) multi_vector_ty:$Zt, PNRAny_p8to15:$PNg, GPR64sp:$Rn, 0), 1>;
}

multiclass sme2_ld_vector_vg4_multi_scalar_immediate<bits<2> msz, bit n,
                                                     RegisterOperand multi_vector_ty,
                                                     Operand index_ty,
                                                     string mnemonic> {
  def NAME : sme2_ld_vector_vg24_multi_scalar_immediate<msz, n, 0b10,
                                                        multi_vector_ty,
                                                        index_ty, mnemonic> {
    bits<3> Zt;
    let Inst{4} = Zt{2};
    let Inst{1-0} = Zt{1-0};
  }

   def : InstAlias<mnemonic # "\t$Zt, $PNg/z, [$Rn]",
                   (!cast<Instruction>(NAME) multi_vector_ty:$Zt, PNRAny_p8to15:$PNg, GPR64sp:$Rn, 0), 1>;
}

//===----------------------------------------------------------------------===//
// SME2 Non-Contiguous Store
class sme2_st_vector_vg2_multi_scalar_scalar<bits<2> msz, bit n,
                                             RegisterOperand multi_vector_ty,
                                             RegisterOperand gpr_ty,
                                             string mnemonic>
   : I<(outs ),
       (ins multi_vector_ty:$Zt, PNRAny_p8to15:$PNg, GPR64sp:$Rn, gpr_ty:$Rm),
       mnemonic, "\t$Zt, $PNg, [$Rn, $Rm]",
       "", []>, Sched<[]> {
   bits<5> Rm;
   bits<3> PNg;
   bits<5> Rn;
   bits<4> Zt;
   let Inst{31-21} = 0b10100001001;
   let Inst{20-16} = Rm;
   let Inst{15}    = 0b0;
   let Inst{14-13} = msz;
   let Inst{12-10} = PNg;
   let Inst{9-5}   = Rn;
   let Inst{4}     = Zt{3};
   let Inst{3}     = n;
   let Inst{2-0}   = Zt{2-0};

   let mayStore    = 1;
}

class sme2_st_vector_vg4_multi_scalar_scalar<bits<2> msz, bit n,
                                             RegisterOperand multi_vector_ty,
                                             RegisterOperand gpr_ty,
                                             string mnemonic>
   : I<(outs ),
       (ins multi_vector_ty:$Zt, PNRAny_p8to15:$PNg, GPR64sp:$Rn, gpr_ty:$Rm),
       mnemonic, "\t$Zt, $PNg, [$Rn, $Rm]",
       "", []>, Sched<[]> {
   bits<5> Rm;
   bits<3> PNg;
   bits<5> Rn;
   bits<3> Zt;
   let Inst{31-21} = 0b10100001001;
   let Inst{20-16} = Rm;
   let Inst{15}     = 0b1;
   let Inst{14-13} = msz;
   let Inst{12-10} = PNg;
   let Inst{9-5}   = Rn;
   let Inst{4}     = Zt{2};
   let Inst{3}     = n;
   let Inst{2}     = 0b0;
   let Inst{1-0}   = Zt{1-0};

   let mayStore    = 1;
}

class sme2_st_vector_vg24_multi_scalar_immediate<bits<2> msz, bit n, bits<2> op,
                                                 RegisterOperand multi_vector_ty,
                                                 Operand index_ty,
                                                 string mnemonic>
    : I<(outs ),
        (ins multi_vector_ty:$Zt, PNRAny_p8to15:$PNg, GPR64sp:$Rn, index_ty:$imm4),
        mnemonic,  "\t$Zt, $PNg, [$Rn, $imm4, mul vl]",
        "", []>, Sched<[]> {
   bits<4> imm4;
   bits<3> PNg;
   bits<5> Rn;
   let Inst{31-20} = 0b101000010110;
   let Inst{19-16} = imm4;
   let Inst{15}    = op{1};
   let Inst{14-13} = msz;
   let Inst{12-10} = PNg;
   let Inst{9-5}   = Rn;
   let Inst{3}     = n;
   let Inst{2}     = op{0};

   let mayStore    = 1;
}


multiclass sme2_st_vector_vg2_multi_scalar_immediate<bits<2> msz, bit n,
                                                     RegisterOperand multi_vector_ty,
                                                     Operand index_ty,
                                                     string mnemonic> {
  def NAME: sme2_st_vector_vg24_multi_scalar_immediate<msz, n, {0,?},
                                                       multi_vector_ty,
                                                       index_ty, mnemonic> {
    bits<4> Zt;
    let Inst{4}   = Zt{3};
    let Inst{2-0} = Zt{2-0};
  }

    def : InstAlias<mnemonic # "\t$Zt, $PNg, [$Rn]",
                   (!cast<Instruction>(NAME) multi_vector_ty:$Zt, PNRAny_p8to15:$PNg, GPR64sp:$Rn,0), 1>;
}

multiclass sme2_st_vector_vg4_multi_scalar_immediate<bits<2> msz, bit n,
                                                     RegisterOperand multi_vector_ty,
                                                     Operand index_ty,
                                                     string mnemonic> {
  def NAME : sme2_st_vector_vg24_multi_scalar_immediate<msz, n, 0b10,
                                                        multi_vector_ty,
                                                        index_ty, mnemonic> {
    bits<3> Zt;
    let Inst{4}   = Zt{2};
    let Inst{1-0} = Zt{1-0};
  }

    def : InstAlias<mnemonic # "\t$Zt, $PNg, [$Rn]",
                   (!cast<Instruction>(NAME) multi_vector_ty:$Zt, PNRAny_p8to15:$PNg, GPR64sp:$Rn,0), 1>;
}

//===----------------------------------------------------------------------===//
// SME2.1
//===----------------------------------------------------------------------===//
// SME zeroing move array to vector
class sme2p1_movaz_tile_to_vec_base<bits<2> sz, bit q, bit v, ZPRRegOp vector_ty,
                                    RegisterOperand tile_ty, Operand index_ty,
                                    string mnemonic>
    : I<(outs vector_ty:$Zd, tile_ty:$ZAn),
        (ins tile_ty:$_ZAn, MatrixIndexGPR32Op12_15:$Rs, index_ty:$imm),
        mnemonic, "\t$Zd, $ZAn[$Rs, $imm]",
        "", []>, Sched<[]> {
  bits<2> Rs;
  bits<5> Zd;
  let Inst{31-24} = 0b11000000;
  let Inst{23-22} = sz;
  let Inst{21-17} = 0b00001;
  let Inst{16}    = q;
  let Inst{15}    = v;
  let Inst{14-13} = Rs;
  let Inst{12-9}  = 0b0001;
  let Inst{4-0}   = Zd;
  let Constraints = "$ZAn = $_ZAn";
}

multiclass sme2p1_movaz_tile_to_vec_base<bit v, string mnemonic> {
  def _B : sme2p1_movaz_tile_to_vec_base<0b00, 0b0, v, ZPR8,
                                    !if(v, TileVectorOpV8, TileVectorOpH8),
                                    sme_elm_idx0_15, mnemonic>, SMEPseudo2Instr<NAME # _B, 1> {
    bits<4> imm;
    let Inst{8-5} = imm;
  }

  def _H : sme2p1_movaz_tile_to_vec_base<0b01, 0b0, v, ZPR16,
                                    !if(v, TileVectorOpV16, TileVectorOpH16),
                                    sme_elm_idx0_7, mnemonic>, SMEPseudo2Instr<NAME # _H, 1> {
    bits<1> ZAn;
    bits<3> imm;
    let Inst{8}   = ZAn;
    let Inst{7-5} = imm;
  }

  def _S : sme2p1_movaz_tile_to_vec_base<0b10, 0b0, v, ZPR32,
                                    !if(v, TileVectorOpV32, TileVectorOpH32),
                                    sme_elm_idx0_3, mnemonic>, SMEPseudo2Instr<NAME # _S, 1> {
    bits<2> ZAn;
    bits<2> imm;
    let Inst{8-7} = ZAn;
    let Inst{6-5} = imm;
  }

  def _D : sme2p1_movaz_tile_to_vec_base<0b11, 0b0, v, ZPR64,
                                    !if(v, TileVectorOpV64, TileVectorOpH64),
                                    sme_elm_idx0_1, mnemonic>, SMEPseudo2Instr<NAME # _D, 1> {
    bits<3> ZAn;
    bits<1> imm;
    let Inst{8-6} = ZAn;
    let Inst{5}   = imm;
  }

  def _Q : sme2p1_movaz_tile_to_vec_base<0b11, 0b1, v, ZPR128,
                                    !if(v, TileVectorOpV128, TileVectorOpH128),
                                    sme_elm_idx0_0, mnemonic>, SMEPseudo2Instr<NAME # _Q, 1> {
    bits<4> ZAn;
    let Inst{8-5} = ZAn;
  }
}

multiclass sme2p1_movaz_tile_to_vec<string mnemonic, SDPatternOperator intrinsic_horiz, SDPatternOperator intrinsic_vert,
                                    SDPatternOperator intrinsic_horiz_q, SDPatternOperator intrinsic_vert_q>{
 defm _H : sme2p1_movaz_tile_to_vec_base<0b0, mnemonic>;
 defm _V : sme2p1_movaz_tile_to_vec_base<0b1, mnemonic>;

 def NAME # _H_B_PSEUDO : sme2_movez_to_tile_pseudo<NAME # _H_B, sme_elm_idx0_0,  sme_elm_idx0_15, ZPR8,   SMEMatrixTileB>;
 def NAME # _H_H_PSEUDO : sme2_movez_to_tile_pseudo<NAME # _H_H, sme_elm_idx0_1,  sme_elm_idx0_7,  ZPR16,  SMEMatrixTileH>;
 def NAME # _H_S_PSEUDO : sme2_movez_to_tile_pseudo<NAME # _H_S, sme_elm_idx0_3,  sme_elm_idx0_3,  ZPR32,  SMEMatrixTileS>;
 def NAME # _H_D_PSEUDO : sme2_movez_to_tile_pseudo<NAME # _H_D, sme_elm_idx0_7,  sme_elm_idx0_1,  ZPR64,  SMEMatrixTileD>;
 def NAME # _H_Q_PSEUDO : sme2_movez_to_tile_pseudo<NAME # _H_Q, sme_elm_idx0_15, sme_elm_idx0_0,  ZPR128, SMEMatrixTileQ>;

 def NAME # _V_B_PSEUDO : sme2_movez_to_tile_pseudo<NAME # _V_B, sme_elm_idx0_0, sme_elm_idx0_15, ZPR8, SMEMatrixTileB>;
 def NAME # _V_H_PSEUDO : sme2_movez_to_tile_pseudo<NAME # _V_H, sme_elm_idx0_1, sme_elm_idx0_7, ZPR16, SMEMatrixTileH>;
 def NAME # _V_S_PSEUDO : sme2_movez_to_tile_pseudo<NAME # _V_S, sme_elm_idx0_3, sme_elm_idx0_3, ZPR32, SMEMatrixTileS>;
 def NAME # _V_D_PSEUDO : sme2_movez_to_tile_pseudo<NAME # _V_D, sme_elm_idx0_7, sme_elm_idx0_1, ZPR64, SMEMatrixTileD>;
 def NAME # _V_Q_PSEUDO : sme2_movez_to_tile_pseudo<NAME # _V_Q, sme_elm_idx0_15, sme_elm_idx0_0, ZPR128, SMEMatrixTileQ>;

 def : SME2_Tile_Movaz_Pat<NAME # _H_B, intrinsic_horiz, nxv16i8,sme_elm_idx0_0,  sme_elm_idx0_15, tileslice8>;
 def : SME2_Tile_Movaz_Pat<NAME # _H_H, intrinsic_horiz, nxv8i16, sme_elm_idx0_1, sme_elm_idx0_7, tileslice16>;
 def : SME2_Tile_Movaz_Pat<NAME # _H_S, intrinsic_horiz, nxv4i32, sme_elm_idx0_3, sme_elm_idx0_3, tileslice32>;
 def : SME2_Tile_Movaz_Pat<NAME # _H_D, intrinsic_horiz, nxv2i64, sme_elm_idx0_7, sme_elm_idx0_1, tileslice64>;
 def : SME2_Tile_Movaz_Pat<NAME # _H_H, intrinsic_horiz, nxv8bf16, sme_elm_idx0_1, sme_elm_idx0_7, tileslice16>;
 def : SME2_Tile_Movaz_Pat<NAME # _H_H, intrinsic_horiz, nxv8f16, sme_elm_idx0_1, sme_elm_idx0_7, tileslice16>;
 def : SME2_Tile_Movaz_Pat<NAME # _H_S, intrinsic_horiz, nxv4f32, sme_elm_idx0_3, sme_elm_idx0_3, tileslice32>;
 def : SME2_Tile_Movaz_Pat<NAME # _H_D, intrinsic_horiz, nxv2f64, sme_elm_idx0_7, sme_elm_idx0_1, tileslice64>;

 def : SME2_Tile_Movaz_Pat<NAME # _V_B, intrinsic_vert, nxv16i8, sme_elm_idx0_0, sme_elm_idx0_15, tileslice8>;
 def : SME2_Tile_Movaz_Pat<NAME # _V_H, intrinsic_vert, nxv8i16, sme_elm_idx0_1, sme_elm_idx0_7, tileslice16>;
 def : SME2_Tile_Movaz_Pat<NAME # _V_S, intrinsic_vert, nxv4i32, sme_elm_idx0_3, sme_elm_idx0_3, tileslice32>;
 def : SME2_Tile_Movaz_Pat<NAME # _V_D, intrinsic_vert, nxv2i64, sme_elm_idx0_7, sme_elm_idx0_1, tileslice64>;
 def : SME2_Tile_Movaz_Pat<NAME # _V_H, intrinsic_vert, nxv8bf16, sme_elm_idx0_1, sme_elm_idx0_7, tileslice16>;
 def : SME2_Tile_Movaz_Pat<NAME # _V_H, intrinsic_vert, nxv8f16, sme_elm_idx0_1, sme_elm_idx0_7, tileslice16>;
 def : SME2_Tile_Movaz_Pat<NAME # _V_S, intrinsic_vert, nxv4f32, sme_elm_idx0_3, sme_elm_idx0_3, tileslice32>;
 def : SME2_Tile_Movaz_Pat<NAME # _V_D, intrinsic_vert, nxv2f64, sme_elm_idx0_7, sme_elm_idx0_1, tileslice64>;

 // H_Q
 def : SME2_Tile_Movaz_Pat<NAME # _H_Q, intrinsic_horiz_q, nxv16i8, sme_elm_idx0_15, sme_elm_idx0_0, tileslice128>;
 def : SME2_Tile_Movaz_Pat<NAME # _H_Q, intrinsic_horiz_q, nxv8i16, sme_elm_idx0_15, sme_elm_idx0_0, tileslice128>;
 def : SME2_Tile_Movaz_Pat<NAME # _H_Q, intrinsic_horiz_q, nxv4i32, sme_elm_idx0_15, sme_elm_idx0_0, tileslice128>;
 def : SME2_Tile_Movaz_Pat<NAME # _H_Q, intrinsic_horiz_q, nxv2i64, sme_elm_idx0_15, sme_elm_idx0_0, tileslice128>;
 def : SME2_Tile_Movaz_Pat<NAME # _H_Q, intrinsic_horiz_q, nxv8bf16, sme_elm_idx0_15, sme_elm_idx0_0, tileslice128>;
 def : SME2_Tile_Movaz_Pat<NAME # _H_Q, intrinsic_horiz_q, nxv8f16, sme_elm_idx0_15, sme_elm_idx0_0, tileslice128>;
 def : SME2_Tile_Movaz_Pat<NAME # _H_Q, intrinsic_horiz_q, nxv4f32, sme_elm_idx0_15, sme_elm_idx0_0, tileslice128>;
 def : SME2_Tile_Movaz_Pat<NAME # _H_Q, intrinsic_horiz_q, nxv2f64, sme_elm_idx0_15, sme_elm_idx0_0, tileslice128>;

 // _V_Q
 def : SME2_Tile_Movaz_Pat<NAME # _V_Q, intrinsic_vert_q, nxv16i8, sme_elm_idx0_15, sme_elm_idx0_0, tileslice128>;
 def : SME2_Tile_Movaz_Pat<NAME # _V_Q, intrinsic_vert_q, nxv8i16, sme_elm_idx0_15, sme_elm_idx0_0, tileslice128>;
 def : SME2_Tile_Movaz_Pat<NAME # _V_Q, intrinsic_vert_q, nxv4i32, sme_elm_idx0_15, sme_elm_idx0_0, tileslice128>;
 def : SME2_Tile_Movaz_Pat<NAME # _V_Q, intrinsic_vert_q, nxv2i64, sme_elm_idx0_15, sme_elm_idx0_0, tileslice128>;
 def : SME2_Tile_Movaz_Pat<NAME # _V_Q, intrinsic_vert_q, nxv8bf16, sme_elm_idx0_15, sme_elm_idx0_0, tileslice128>;
 def : SME2_Tile_Movaz_Pat<NAME # _V_Q, intrinsic_vert_q, nxv8f16, sme_elm_idx0_15, sme_elm_idx0_0, tileslice128>;
 def : SME2_Tile_Movaz_Pat<NAME # _V_Q, intrinsic_vert_q, nxv4f32, sme_elm_idx0_15, sme_elm_idx0_0, tileslice128>;
 def : SME2_Tile_Movaz_Pat<NAME # _V_Q, intrinsic_vert_q, nxv2f64, sme_elm_idx0_15, sme_elm_idx0_0, tileslice128>;
}

//===----------------------------------------------------------------------===//
// SME2.1 multiple vectors zero array

class sme2p1_zero_matrix<bits<6> opc, Operand index_ty, string mnemonic,
                         string vg_acronym="">
    : I<(outs MatrixOp64:$ZAd),
        (ins MatrixOp64:$_ZAd, MatrixIndexGPR32Op8_11:$Rv, index_ty:$imm),
        mnemonic, "\t$ZAd[$Rv, $imm" # !if(!eq(vg_acronym, ""), "", ", " # vg_acronym) # "]",
        "", []>, Sched<[]> {
  bits <2> Rv;
  let Inst{31-18} = 0b11000000000011;
  let Inst{17-15} = opc{5-3};
  let Inst{14-13} = Rv;
  let Inst{12-3} = 0b0000000000;
  let Inst{2-0}  = opc{2-0};
  let Constraints = "$ZAd = $_ZAd";
}

multiclass sme2p1_zero_matrix<string mnemonic> {
  def _VG2_Z : sme2p1_zero_matrix<{0b000,?,?,?}, sme_elm_idx0_7, mnemonic, "vgx2">, SMEPseudo2Instr<NAME # _VG2_Z , 1> {
    bits<3> imm;
    let Inst{2-0} = imm;
  }
  def _2Z : sme2p1_zero_matrix<{0b001,?,?,?}, uimm3s2range, mnemonic>, SMEPseudo2Instr<NAME # _2Z, 1> {
    bits<3> imm;
    let Inst{2-0} = imm;
  }
  def _VG2_2Z : sme2p1_zero_matrix<{0b0100,?,?}, uimm2s2range, mnemonic, "vgx2">, SMEPseudo2Instr<NAME # _VG2_2Z, 1> {
    bits<2> imm;
    let Inst{1-0} = imm;
  }
  def _VG4_2Z : sme2p1_zero_matrix<{0b0110,?,?}, uimm2s2range, mnemonic, "vgx4">, SMEPseudo2Instr<NAME # _VG4_2Z, 1> {
    bits<2> imm;
    let Inst{1-0} = imm;
  }
  def _VG4_Z : sme2p1_zero_matrix<{0b100,?,?,?}, sme_elm_idx0_7, mnemonic, "vgx4">, SMEPseudo2Instr<NAME # _VG4_Z, 1> {
    bits<3> imm;
    let Inst{2-0} = imm;
  }
  def _4Z : sme2p1_zero_matrix<{0b1010,?,?}, uimm2s4range, mnemonic>, SMEPseudo2Instr<NAME # _4Z, 1> {
    bits<2> imm;
    let Inst{1-0} = imm;
  }
  def _VG2_4Z : sme2p1_zero_matrix<{0b11000,?}, uimm1s4range, mnemonic, "vgx2">, SMEPseudo2Instr<NAME # _VG2_4Z, 1> {
    bits<1> imm;
    let Inst{0}   = imm;
  }
  def _VG4_4Z : sme2p1_zero_matrix<{0b11100,?}, uimm1s4range, mnemonic, "vgx4">, SMEPseudo2Instr<NAME # _VG4_4Z, 1> {
    bits<1> imm;
    let Inst{0}   = imm;
  }

  def NAME # _VG2_Z_PSEUDO : sem2p1_zero_matrix_pseudo<NAME # _VG2_Z, sme_elm_idx0_7, SMEMatrixArray>;
  def NAME # _VG4_Z_PSEUDO : sem2p1_zero_matrix_pseudo<NAME # _VG4_Z, sme_elm_idx0_7, SMEMatrixArray>;
  def NAME # _2Z_PSEUDO : sem2p1_zero_matrix_pseudo<NAME # _2Z, uimm2s2range, SMEMatrixArray>;
  def NAME # _VG2_2Z_PSEUDO : sem2p1_zero_matrix_pseudo<NAME # _VG2_2Z, uimm1s2range, SMEMatrixArray>;
  def NAME # _VG4_2Z_PSEUDO : sem2p1_zero_matrix_pseudo<NAME # _VG4_2Z, uimm1s2range, SMEMatrixArray>;
  def NAME # _4Z_PSEUDO : sem2p1_zero_matrix_pseudo<NAME # _4Z, uimm1s4range, SMEMatrixArray>;
  def NAME # _VG2_4Z_PSEUDO : sem2p1_zero_matrix_pseudo<NAME # _VG2_4Z, uimm0s4range, SMEMatrixArray>;
  def NAME # _VG4_4Z_PSEUDO : sem2p1_zero_matrix_pseudo<NAME # _VG4_4Z, uimm0s4range, SMEMatrixArray>;

  def : SME2_Zero_Matrix_Pat<NAME # _VG2_Z_PSEUDO, int_aarch64_sme_zero_za64_vg1x2, sme_elm_idx0_7, tileslice16>;
  def : SME2_Zero_Matrix_Pat<NAME # _VG4_Z_PSEUDO, int_aarch64_sme_zero_za64_vg1x4, sme_elm_idx0_7, tileslice16>;
  def : SME2_Zero_Matrix_Pat<NAME # _2Z_PSEUDO, int_aarch64_sme_zero_za64_vg2x1, uimm2s2range, tileslicerange2s2>;
  def : SME2_Zero_Matrix_Pat<NAME # _VG2_2Z_PSEUDO, int_aarch64_sme_zero_za64_vg2x2, uimm1s2range, tileslicerange1s2>;
  def : SME2_Zero_Matrix_Pat<NAME # _VG4_2Z_PSEUDO, int_aarch64_sme_zero_za64_vg2x4, uimm1s2range, tileslicerange1s2>;
  def : SME2_Zero_Matrix_Pat<NAME # _4Z_PSEUDO, int_aarch64_sme_zero_za64_vg4x1, uimm1s4range, tileslicerange1s4>;
  def : SME2_Zero_Matrix_Pat<NAME # _VG2_4Z_PSEUDO, int_aarch64_sme_zero_za64_vg4x2, uimm0s4range, tileslicerange0s4>;
  def : SME2_Zero_Matrix_Pat<NAME # _VG4_4Z_PSEUDO, int_aarch64_sme_zero_za64_vg4x4, uimm0s4range, tileslicerange0s4>;
} 

//===----------------------------------------------------------------------===//
// SME2.1 lookup table expand two non-contiguous registers

class sme2p1_luti_vector_vg2_index<bits<4> op, bits<2> sz, RegisterOperand vector_ty,
                                   AsmVectorIndexOpnd index_ty,
                                   string mnemonic>
    :  I<(outs vector_ty:$Zd), (ins ZTR:$ZTt, ZPRAny:$Zn, index_ty:$i),
          mnemonic, "\t$Zd, $ZTt, $Zn$i",
          "", []>, Sched<[]> {
  bits<5> Zn;
  bits<4> Zd;
  let Inst{31-19} = 0b1100000010011;
  let Inst{18-15} = op;
  let Inst{14}    = 0b1;
  let Inst{13-12} = sz;
  let Inst{11-10} = 0b00;
  let Inst{9-5}   = Zn;
  let Inst{4}     = Zd{3};
  let Inst{3}     = 0b0;
  let Inst{2-0}   = Zd{2-0};
}

class sme2p1_luti2_vector_vg2_index<bits<2> sz, RegisterOperand vector_ty,
                                    AsmVectorIndexOpnd index_ty,
                                    string mnemonic>
  : sme2p1_luti_vector_vg2_index<{1,?,?,?}, sz, vector_ty, index_ty, mnemonic> {
  bits<3> i;
  let Inst{17-15} = i;
}

multiclass sme2p1_luti2_vector_vg2_index<string mnemonic> {
  def _B : sme2p1_luti2_vector_vg2_index<0b00, ZZ_b_strided, VectorIndexH,
                                         mnemonic>;
  def _H : sme2p1_luti2_vector_vg2_index<0b01, ZZ_h_strided, VectorIndexH,
                                         mnemonic>;
}

class sme2p1_luti4_vector_vg2_index<bits<2> sz, RegisterOperand vector_ty,
                                    AsmVectorIndexOpnd index_ty,
                                    string mnemonic>
  : sme2p1_luti_vector_vg2_index<{0b01,?,?}, sz, vector_ty, index_ty, mnemonic> {
  bits<2> i;
  let Inst{16-15} = i;
}
multiclass sme2p1_luti4_vector_vg2_index<string mnemonic> {
  def _B : sme2p1_luti4_vector_vg2_index<0b00, ZZ_b_strided, VectorIndexS,
                                         mnemonic>;
  def _H : sme2p1_luti4_vector_vg2_index<0b01, ZZ_h_strided, VectorIndexS,
                                         mnemonic>;
}

// SME2.1 lookup table expand four non-contiguous registers
class sme2p1_luti_vector_vg4_index<bits<3> op, bits<2> sz, RegisterOperand vector_ty,
                                   AsmVectorIndexOpnd index_ty,
                                   string mnemonic>
    :  I<(outs vector_ty:$Zd), (ins ZTR:$ZTt, ZPRAny:$Zn, index_ty:$i),
          mnemonic, "\t$Zd, $ZTt, $Zn$i",
          "", []>, Sched<[]> {
  bits<5> Zn;
  bits<3> Zd;
  let Inst{31-19} = 0b1100000010011;
  let Inst{18-16} = op;
  let Inst{15-14} = 0b10;
  let Inst{13-12} = sz;
  let Inst{11-10} = 0b00;
  let Inst{9-5}   = Zn;
  let Inst{4}     = Zd{2};
  let Inst{3-2}   = 0b00;
  let Inst{1-0}   = Zd{1-0};
}

class sme2p1_luti2_vector_vg4_index<bits<2> sz, RegisterOperand vector_ty,
                                    AsmVectorIndexOpnd index_ty,
                                    string mnemonic>
  : sme2p1_luti_vector_vg4_index<{1,?,?}, sz, vector_ty, index_ty, mnemonic> {
  bits<2> i;
  let Inst{17-16} = i;
}

multiclass sme2p1_luti2_vector_vg4_index<string mnemonic> {
  def _B : sme2p1_luti2_vector_vg4_index<0b00, ZZZZ_b_strided, VectorIndexS,
                                         mnemonic>;
  def _H : sme2p1_luti2_vector_vg4_index<0b01, ZZZZ_h_strided, VectorIndexS,
                                         mnemonic>;
}

class sme2p1_luti4_vector_vg4_index<bits<2> sz, RegisterOperand vector_ty,
                                    AsmVectorIndexOpnd index_ty,
                                    string mnemonic>
  : sme2p1_luti_vector_vg4_index<{0b01,?}, sz, vector_ty, index_ty, mnemonic> {
  bit i;
  let Inst{16}    = i;
}

multiclass sme2p1_luti4_vector_vg4_index<string mnemonic> {
  def _H: sme2p1_luti4_vector_vg4_index<0b01, ZZZZ_h_strided, VectorIndexD, mnemonic>;
}

// SME2 lookup table two source registers expand to four contiguous destination registers
class sme2_luti4_vector_vg4<bits<2> sz, bits<2> op, string mnemonic>
  : I<(outs ZZZZ_b_mul_r:$Zd), (ins ZTR:$ZTt, ZZ_mul_r:$Zn),
       mnemonic, "\t$Zd, $ZTt, $Zn",
       "", []>, Sched<[]> {
  bits<4> Zn;
  bits<3> Zd;
  let Inst{31-14} = 0b110000001000101100;
  let Inst{13-12} = sz;
  let Inst{11-10} = op;
  let Inst{9-6}   = Zn;
  let Inst{5}     = 0b0;
  let Inst{4-2}   = Zd;
  let Inst{1-0}   = 0b00;
}

// SME2 lookup table two source registers expand to four non-contiguous destination registers
class sme2_luti4_vector_vg4_strided<bits<2> sz, bits<2> op, string mnemonic>
   : I<(outs ZZZZ_b_strided:$Zd), (ins ZTR:$ZTt, ZZ_mul_r:$Zn),
        mnemonic, "\t$Zd, $ZTt, $Zn",
        "", []>, Sched<[]> {
  bits<4> Zn;
  bits<3> Zd;
  let Inst{31-14} = 0b110000001001101100;
  let Inst{13-12} = sz;
  let Inst{11-10} = op;
  let Inst{9-6}   = Zn;
  let Inst{5}     = 0b0;
  let Inst{4}     = Zd{2};
  let Inst{3-2}   = 0b00;
  let Inst{1-0}   = Zd{1-0};
}
