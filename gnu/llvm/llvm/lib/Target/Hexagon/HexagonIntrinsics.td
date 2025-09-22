//===-- HexagonIntrinsics.td - Instruction intrinsics ------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// These intrinsic patterns are not auto-generated.

class T_R_pat <InstHexagon MI, Intrinsic IntID>
  : Pat <(IntID I32:$Rs),
         (MI I32:$Rs)>;

class T_RR_pat <InstHexagon MI, Intrinsic IntID>
  : Pat <(IntID I32:$Rs, I32:$Rt),
         (MI I32:$Rs, I32:$Rt)>;

class T_RP_pat <InstHexagon MI, Intrinsic IntID>
  : Pat <(IntID I32:$Rs, I64:$Rt),
         (MI I32:$Rs, I64:$Rt)>;

def: Pat<(int_hexagon_A2_add IntRegs:$Rs, IntRegs:$Rt),
         (A2_add IntRegs:$Rs, IntRegs:$Rt)>;
def: Pat<(int_hexagon_A2_addi IntRegs:$Rs, timm:$s16),
         (A2_addi IntRegs:$Rs, imm:$s16)>;
def: Pat<(int_hexagon_A2_addp DoubleRegs:$Rs, DoubleRegs:$Rt),
         (A2_addp DoubleRegs:$Rs, DoubleRegs:$Rt)>;

def: Pat<(int_hexagon_A2_sub IntRegs:$Rs, IntRegs:$Rt),
         (A2_sub IntRegs:$Rs, IntRegs:$Rt)>;
def: Pat<(int_hexagon_A2_subri timm:$s10, IntRegs:$Rs),
         (A2_subri imm:$s10, IntRegs:$Rs)>;
def: Pat<(int_hexagon_A2_subp DoubleRegs:$Rs, DoubleRegs:$Rt),
         (A2_subp DoubleRegs:$Rs, DoubleRegs:$Rt)>;

def: Pat<(int_hexagon_M2_mpyi IntRegs:$Rs, IntRegs:$Rt),
         (M2_mpyi IntRegs:$Rs, IntRegs:$Rt)>;
def: Pat<(int_hexagon_M2_mpyui IntRegs:$Rs, IntRegs:$Rt), // Same as M2_mpyi
         (M2_mpyi IntRegs:$Rs, IntRegs:$Rt)>;
def: Pat<(int_hexagon_M2_mpysmi IntRegs:$Rs, imm:$s9),
         (M2_mpysmi IntRegs:$Rs, imm:$s9)>;
def: Pat<(int_hexagon_M2_dpmpyss_s0 IntRegs:$Rs, IntRegs:$Rt),
         (M2_dpmpyss_s0 IntRegs:$Rs, IntRegs:$Rt)>;
def: Pat<(int_hexagon_M2_dpmpyuu_s0 IntRegs:$Rs, IntRegs:$Rt),
         (M2_dpmpyuu_s0 IntRegs:$Rs, IntRegs:$Rt)>;

def: Pat<(int_hexagon_S2_asl_i_r IntRegs:$Rs, timm:$u5),
         (S2_asl_i_r IntRegs:$Rs, imm:$u5)>;
def: Pat<(int_hexagon_S2_lsr_i_r IntRegs:$Rs, timm:$u5),
         (S2_lsr_i_r IntRegs:$Rs, imm:$u5)>;
def: Pat<(int_hexagon_S2_asr_i_r IntRegs:$Rs, timm:$u5),
         (S2_asr_i_r IntRegs:$Rs, imm:$u5)>;
def: Pat<(int_hexagon_S2_asl_i_p DoubleRegs:$Rs, timm:$u6),
         (S2_asl_i_p DoubleRegs:$Rs, imm:$u6)>;
def: Pat<(int_hexagon_S2_lsr_i_p DoubleRegs:$Rs, timm:$u6),
         (S2_lsr_i_p DoubleRegs:$Rs, imm:$u6)>;
def: Pat<(int_hexagon_S2_asr_i_p DoubleRegs:$Rs, timm:$u6),
         (S2_asr_i_p DoubleRegs:$Rs, imm:$u6)>;

def: Pat<(int_hexagon_A2_and IntRegs:$Rs, IntRegs:$Rt),
         (A2_and IntRegs:$Rs, IntRegs:$Rt)>;
def: Pat<(int_hexagon_A2_andir IntRegs:$Rs, timm:$s10),
         (A2_andir IntRegs:$Rs, imm:$s10)>;
def: Pat<(int_hexagon_A2_or IntRegs:$Rs, IntRegs:$Rt),
         (A2_or IntRegs:$Rs, IntRegs:$Rt)>;
def: Pat<(int_hexagon_A2_orir IntRegs:$Rs, timm:$s10),
         (A2_orir IntRegs:$Rs, imm:$s10)>;
def: Pat<(int_hexagon_A2_xor IntRegs:$Rs, IntRegs:$Rt),
         (A2_xor IntRegs:$Rs, IntRegs:$Rt)>;

def: Pat<(int_hexagon_A2_sxtb IntRegs:$Rs),
         (A2_sxtb IntRegs:$Rs)>;
def: Pat<(int_hexagon_A2_sxth IntRegs:$Rs),
         (A2_sxth IntRegs:$Rs)>;
def: Pat<(int_hexagon_A2_zxtb IntRegs:$Rs),
         (A2_zxtb IntRegs:$Rs)>;
def: Pat<(int_hexagon_A2_zxth IntRegs:$Rs),
         (A2_zxth IntRegs:$Rs)>;

// Assembler mapped from Rd32=not(Rs32) to Rd32=sub(#-1,Rs32)
def : Pat <(int_hexagon_A2_not I32:$Rs),
           (A2_subri -1, I32:$Rs)>;

// Assembler mapped from Rd32=neg(Rs32) to Rd32=sub(#0,Rs32)
def : Pat <(int_hexagon_A2_neg I32:$Rs),
           (A2_subri 0, I32:$Rs)>;

// Make sure the patterns with zero immediate value has higher complexity
// otherwise, we need to updated the predicates for immediates to exclude zero
let AddedComplexity = 200 in {
def : Pat <(int_hexagon_S2_asr_i_r_rnd_goodsyntax I32:$Rs, (i32 0)),
           (A2_tfr I32:$Rs)>;
def : Pat <(int_hexagon_S2_asr_i_p_rnd_goodsyntax I64:$Rs, (i32 0)),
           (A2_combinew (HiReg I64:$Rs), (LoReg I64:$Rs))>;
def : Pat <(int_hexagon_S5_vasrhrnd_goodsyntax I64:$Rs, (i32 0)),
           (A2_combinew (HiReg I64:$Rs), (LoReg I64:$Rs))>;
def : Pat <(int_hexagon_S5_asrhub_rnd_sat_goodsyntax I64:$Rs, (i32 0)),
           (S2_vsathub I64:$Rs)>;
}

def : Pat <(int_hexagon_S2_asr_i_r_rnd_goodsyntax I32:$Rs, u5_0ImmPred_timm:$imm),
           (S2_asr_i_r_rnd I32:$Rs, (UDEC1 u5_0ImmPred:$imm))>;
def : Pat <(int_hexagon_S2_asr_i_p_rnd_goodsyntax I64:$Rs, u6_0ImmPred_timm:$imm),
           (S2_asr_i_p_rnd I64:$Rs, (UDEC1 u6_0ImmPred:$imm))>;
def : Pat <(int_hexagon_S5_vasrhrnd_goodsyntax I64:$Rs, u4_0ImmPred_timm:$imm),
           (S5_vasrhrnd I64:$Rs, (UDEC1 u4_0ImmPred:$imm))>;
def : Pat <(int_hexagon_S5_asrhub_rnd_sat_goodsyntax I64:$Rs, u4_0ImmPred_timm:$imm),
           (S5_asrhub_rnd_sat I64:$Rs, (UDEC1 u4_0ImmPred:$imm))>;

def ImmExt64: SDNodeXForm<imm, [{
  int64_t V = N->getSExtValue();
  return CurDAG->getTargetConstant(V, SDLoc(N), MVT::i64);
}]>;

// A2_tfrpi has an operand of type i64. This is necessary, since it is
// generated from "(set I64:$Rd, imm)". That pattern would not appear
// in the DAG, if the immediate was not a 64-bit value.
// The builtin for A2_tfrpi, on the other hand, takes a 32-bit value,
// which makes it impossible to simply replace it with the instruction.
// To connect the builtin with the instruction, the builtin's operand
// needs to be extended to the right type.

def : Pat<(int_hexagon_A2_tfrpi timm:$Is),
          (A2_tfrpi (ImmExt64 $Is))>;

def : Pat <(int_hexagon_C2_cmpgei I32:$src1, s32_0ImmPred_timm:$src2),
           (C2_tfrpr (C2_cmpgti I32:$src1, (SDEC1 s32_0ImmPred:$src2)))>;

def : Pat <(int_hexagon_C2_cmpgeui I32:$src1, u32_0ImmPred_timm:$src2),
           (C2_tfrpr (C2_cmpgtui I32:$src1, (UDEC1 u32_0ImmPred:$src2)))>;

def : Pat <(int_hexagon_C2_cmpgeui I32:$src, 0),
           (C2_tfrpr (C2_cmpeq I32:$src, I32:$src))>;
def : Pat <(int_hexagon_C2_cmplt I32:$src1, I32:$src2),
           (C2_tfrpr (C2_cmpgt I32:$src2, I32:$src1))>;
def : Pat <(int_hexagon_C2_cmpltu I32:$src1, I32:$src2),
           (C2_tfrpr (C2_cmpgtu I32:$src2, I32:$src1))>;

//===----------------------------------------------------------------------===//
// Template 'def pat' to map tableidx[bhwd] intrinsics to :raw instructions.
//===----------------------------------------------------------------------===//
class S2op_tableidx_pat <Intrinsic IntID, InstHexagon OutputInst,
                         SDNodeXForm XformImm>
  : Pat <(IntID I32:$src1, I32:$src2, u4_0ImmPred_timm:$src3, u5_0ImmPred_timm:$src4),
         (OutputInst I32:$src1, I32:$src2, u4_0ImmPred:$src3,
                     (XformImm u5_0ImmPred:$src4))>;

def SDEC2 : SDNodeXForm<imm, [{
  int32_t V = N->getSExtValue();
  return CurDAG->getTargetConstant(V-2, SDLoc(N), MVT::i32);
}]>;

def SDEC3 : SDNodeXForm<imm, [{
  int32_t V = N->getSExtValue();
  return CurDAG->getTargetConstant(V-3, SDLoc(N), MVT::i32);
}]>;

// Table Index : Extract and insert bits.
// Map to the real hardware instructions after subtracting appropriate
// values from the 4th input operand. Please note that subtraction is not
// needed for int_hexagon_S2_tableidxb_goodsyntax.

def : S2op_tableidx_pat <int_hexagon_S2_tableidxb_goodsyntax, S2_tableidxb,
                         IdImm>;
def : S2op_tableidx_pat <int_hexagon_S2_tableidxh_goodsyntax, S2_tableidxh,
                         SDEC1>;
def : S2op_tableidx_pat <int_hexagon_S2_tableidxw_goodsyntax, S2_tableidxw,
                         SDEC2>;
def : S2op_tableidx_pat <int_hexagon_S2_tableidxd_goodsyntax, S2_tableidxd,
                         SDEC3>;

// Load/store locked.
def : T_R_pat<L2_loadw_locked, int_hexagon_L2_loadw_locked>;
def : T_R_pat<L4_loadd_locked, int_hexagon_L4_loadd_locked>;

def : Pat<(int_hexagon_S2_storew_locked I32:$Rs, I32:$Rt),
          (C2_tfrpr (S2_storew_locked I32:$Rs, I32:$Rt))>;
def : Pat<(int_hexagon_S4_stored_locked I32:$Rs, I64:$Rt),
          (C2_tfrpr (S4_stored_locked I32:$Rs, I64:$Rt))>;

//*******************************************************************
//           ST
//*******************************************************************

class T_stb_pat <InstHexagon MI, Intrinsic IntID, PatLeaf Val>
  : Pat<(IntID I32:$Rs, Val:$Rt, I32:$Ru),
        (MI I32:$Rs, I32:$Ru, Val:$Rt)>;

def : T_stb_pat <S2_storerh_pbr, int_hexagon_S2_storerh_pbr, I32>;
def : T_stb_pat <S2_storerb_pbr, int_hexagon_S2_storerb_pbr, I32>;
def : T_stb_pat <S2_storeri_pbr, int_hexagon_S2_storeri_pbr, I32>;
def : T_stb_pat <S2_storerf_pbr, int_hexagon_S2_storerf_pbr, I32>;
def : T_stb_pat <S2_storerd_pbr, int_hexagon_S2_storerd_pbr, I64>;

class T_stc_pat <InstHexagon MI, Intrinsic IntID, PatLeaf Imm, PatLeaf Val>
  : Pat<(IntID I32:$Rs, Val:$Rt, I32:$Ru, Imm:$s),
        (MI I32:$Rs, Imm:$s, I32:$Ru, Val:$Rt)>;

def: T_stc_pat<S2_storerb_pci, int_hexagon_circ_stb,   s4_0ImmPred_timm, I32>;
def: T_stc_pat<S2_storerh_pci, int_hexagon_circ_sth,   s4_1ImmPred_timm, I32>;
def: T_stc_pat<S2_storeri_pci, int_hexagon_circ_stw,   s4_2ImmPred_timm, I32>;
def: T_stc_pat<S2_storerd_pci, int_hexagon_circ_std,   s4_3ImmPred_timm, I64>;
def: T_stc_pat<S2_storerf_pci, int_hexagon_circ_sthhi, s4_1ImmPred_timm, I32>;

multiclass MaskedStore <InstHexagon MI, Intrinsic IntID> {
  def : Pat<(IntID HvxQR:$src1, IntRegs:$src2, HvxVR:$src3),
            (MI HvxQR:$src1, IntRegs:$src2, 0, HvxVR:$src3)>,
        Requires<[UseHVX]>;

  def : Pat<(!cast<Intrinsic>(IntID#"_128B") HvxQR:$src1, IntRegs:$src2,
                                             HvxVR:$src3),
            (MI HvxQR:$src1, IntRegs:$src2, 0, HvxVR:$src3)>,
        Requires<[UseHVX]>;
}

defm : MaskedStore <V6_vS32b_qpred_ai, int_hexagon_V6_vmaskedstoreq>;
defm : MaskedStore <V6_vS32b_nqpred_ai, int_hexagon_V6_vmaskedstorenq>;
defm : MaskedStore <V6_vS32b_nt_qpred_ai, int_hexagon_V6_vmaskedstorentq>;
defm : MaskedStore <V6_vS32b_nt_nqpred_ai, int_hexagon_V6_vmaskedstorentnq>;

defm : MaskedStore <V6_vS32b_qpred_ai, int_hexagon_V6_vS32b_qpred_ai>;
defm : MaskedStore <V6_vS32b_nqpred_ai, int_hexagon_V6_vS32b_nqpred_ai>;
defm : MaskedStore <V6_vS32b_nt_qpred_ai, int_hexagon_V6_vS32b_nt_qpred_ai>;
defm : MaskedStore <V6_vS32b_nt_nqpred_ai, int_hexagon_V6_vS32b_nt_nqpred_ai>;

//*******************************************************************
//           SYSTEM
//*******************************************************************

def: T_R_pat<Y2_dccleana,    int_hexagon_Y2_dccleana>;
def: T_R_pat<Y2_dccleaninva, int_hexagon_Y2_dccleaninva>;
def: T_R_pat<Y2_dcinva,      int_hexagon_Y2_dcinva>;
def: T_R_pat<Y2_dczeroa,     int_hexagon_Y2_dczeroa>;

def: T_RR_pat<Y4_l2fetch,    int_hexagon_Y4_l2fetch>;
def: T_RP_pat<Y5_l2fetch,    int_hexagon_Y5_l2fetch>;

def: Pat<(int_hexagon_Y2_dcfetch I32:$Rt), (Y2_dcfetchbo I32:$Rt, 0)>;

//
// Patterns for optimizing code generations for HVX.

def u3_64_ImmPred  : PatLeaf<(i32 imm), [{
  int64_t v = (int64_t)(64 - N->getSExtValue());
  return isUInt<3>(v);
}]>;

def u3_128_ImmPred  : PatLeaf<(i32 imm), [{
  int64_t v = (int64_t)(128 - N->getSExtValue());
  return isUInt<3>(v);
}]>;

def SUB_64_VAL : SDNodeXForm<imm, [{
   int32_t Imm = N->getSExtValue();
   return CurDAG->getTargetConstant(64 - Imm, SDLoc(N), MVT::i32);
}]>;

def SUB_128_VAL : SDNodeXForm<imm, [{
   int32_t Imm = N->getSExtValue();
   return CurDAG->getTargetConstant(128 - Imm, SDLoc(N), MVT::i32);
}]>;

let AddedComplexity = 100 in {
def : Pat <(v16i32 (int_hexagon_V6_lo (v32i32 HvxWR:$src1))),
           (v16i32 (EXTRACT_SUBREG (v32i32 HvxWR:$src1), vsub_lo))>,
           Requires<[UseHVX]>;

def : Pat <(v16i32 (int_hexagon_V6_hi (v32i32 HvxWR:$src1))),
           (v16i32 (EXTRACT_SUBREG (v32i32 HvxWR:$src1), vsub_hi))>,
           Requires<[UseHVX]>;

def : Pat <(v32i32 (int_hexagon_V6_lo_128B (v64i32 HvxWR:$src1))),
           (v32i32 (EXTRACT_SUBREG (v64i32 HvxWR:$src1), vsub_lo))>,
           Requires<[UseHVX]>;

def : Pat <(v32i32 (int_hexagon_V6_hi_128B (v64i32 HvxWR:$src1))),
           (v32i32 (EXTRACT_SUBREG (v64i32 HvxWR:$src1), vsub_hi))>,
           Requires<[UseHVX]>;
}

def: Pat<(v64i16 (trunc v64i32:$Vdd)),
         (v64i16 (V6_vpackwh_sat
                 (v32i32 (V6_hi HvxWR:$Vdd)),
                 (v32i32 (V6_lo HvxWR:$Vdd))))>,
     Requires<[UseHVX]>;

multiclass T_VI_pat <InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID HvxVR:$src1, u3_0ImmPred:$src2),
           (MI    HvxVR:$src1, HvxVR:$src1, u3_0ImmPred:$src2)>,
       Requires<[UseHVX]>;

  def: Pat<(!cast<Intrinsic>(IntID#"_128B") HvxVR:$src1, u3_0ImmPred:$src2),
           (MI                 HvxVR:$src1, HvxVR:$src1, u3_0ImmPred:$src2)>,
       Requires<[UseHVX]>;
}

multiclass T_VI_inv_pat <InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID HvxVR:$src1, u3_64_ImmPred:$src2),
           (MI    HvxVR:$src1, HvxVR:$src1,
                  (SUB_64_VAL u3_64_ImmPred:$src2))>,
       Requires<[UseHVX]>;

  def: Pat<(!cast<Intrinsic>(IntID#"_128B") HvxVR:$src1, u3_128_ImmPred:$src2),
           (MI HvxVR:$src1, HvxVR:$src1, (SUB_128_VAL u3_128_ImmPred:$src2))>,
       Requires<[UseHVX]>;
}

multiclass T_VVI_pat <InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID HvxVR:$src1, HvxVR:$src2, u3_0ImmPred:$src3),
           (MI    HvxVR:$src1, HvxVR:$src2, u3_0ImmPred:$src3)>,
       Requires<[UseHVX]>;

  def: Pat<(!cast<Intrinsic>(IntID#"_128B") HvxVR:$src1, HvxVR:$src2,
                                            u3_0ImmPred:$src3),
           (MI                              HvxVR:$src1, HvxVR:$src2,
                                            u3_0ImmPred:$src3)>,
       Requires<[UseHVX]>;
}

multiclass T_VVI_inv_pat <InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID HvxVR:$src1, HvxVR:$src2, u3_64_ImmPred:$src3),
           (MI    HvxVR:$src1, HvxVR:$src2,
                                    (SUB_64_VAL u3_64_ImmPred:$src3))>,
       Requires<[UseHVX]>;

  def: Pat<(!cast<Intrinsic>(IntID#"_128B") HvxVR:$src1, HvxVR:$src2,
                                            u3_128_ImmPred:$src3),
           (MI                              HvxVR:$src1, HvxVR:$src2,
                                          (SUB_128_VAL u3_128_ImmPred:$src3))>,
       Requires<[UseHVX]>;
}

multiclass T_VVR_pat <InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
           (MI    HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>,
       Requires<[UseHVX]>;

  def: Pat<(!cast<Intrinsic>(IntID#"_128B") HvxVR:$src1, HvxVR:$src2,
                                            IntRegs:$src3),
           (MI                              HvxVR:$src1, HvxVR:$src2,
                                            IntRegs:$src3)>,
       Requires<[UseHVX]>;
}

defm : T_VI_pat <V6_valignbi, int_hexagon_V6_vror>;
defm : T_VI_inv_pat <V6_vlalignbi, int_hexagon_V6_vror>;

defm : T_VVI_pat <V6_valignbi, int_hexagon_V6_valignb>;
defm : T_VVI_inv_pat <V6_vlalignbi, int_hexagon_V6_valignbi>;
defm : T_VVI_inv_pat <V6_vlalignbi, int_hexagon_V6_valignb>;
defm : T_VVR_pat <V6_valignb, int_hexagon_V6_valignbi>;
defm : T_VVI_pat <V6_vlalignbi, int_hexagon_V6_vlalignb>;
defm : T_VVI_inv_pat <V6_valignbi, int_hexagon_V6_vlalignbi>;
defm : T_VVI_inv_pat <V6_valignbi, int_hexagon_V6_vlalignb>;
defm : T_VVR_pat <V6_vlalignb, int_hexagon_V6_vlalignbi>;

def: Pat<(int_hexagon_V6_vd0),
         (V6_vd0)>, Requires<[UseHVXV60, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vd0_128B ),
         (V6_vd0)>, Requires<[UseHVXV60, UseHVX128B]>;

def: Pat<(int_hexagon_V6_vdd0),
         (V6_vdd0)>, Requires<[UseHVXV65, UseHVX64B]>;
def: Pat<(int_hexagon_V6_vdd0_128B),
         (V6_vdd0)>, Requires<[UseHVXV65, UseHVX128B]>;


multiclass T_VP_pat<InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID HvxVR:$Vu, DoubleRegs:$Rt),
           (MI    HvxVR:$Vu, DoubleRegs:$Rt)>;
  def: Pat<(!cast<Intrinsic>(IntID#"_128B")
                  HvxVR:$Vu, DoubleRegs:$Rt),
           (MI    HvxVR:$Vu, DoubleRegs:$Rt)>;
}

multiclass T_WVP_pat<InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID HvxWR:$Vx, HvxVR:$Vu, DoubleRegs:$Rt),
           (MI    HvxWR:$Vx, HvxVR:$Vu, DoubleRegs:$Rt)>;
  def: Pat<(!cast<Intrinsic>(IntID#"_128B")
                  HvxWR:$Vx, HvxVR:$Vu, DoubleRegs:$Rt),
           (MI    HvxWR:$Vx, HvxVR:$Vu, DoubleRegs:$Rt)>;
}

// These are actually only in V65.
let Predicates = [UseHVXV65, UseHVX] in {
  defm: T_VP_pat<V6_vrmpyub_rtt,        int_hexagon_V6_vrmpyub_rtt>;
  defm: T_VP_pat<V6_vrmpybub_rtt,       int_hexagon_V6_vrmpybub_rtt>;

  defm: T_WVP_pat<V6_vrmpyub_rtt_acc,   int_hexagon_V6_vrmpyub_rtt_acc>;
  defm: T_WVP_pat<V6_vrmpybub_rtt_acc,  int_hexagon_V6_vrmpybub_rtt_acc>;
}


multiclass T_pRI_pat<InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID PredRegs:$P, IntRegs:$R, timm:$s),
           (MI    PredRegs:$P, IntRegs:$R, imm:$s)>;
  def: Pat<(!cast<Intrinsic>(IntID#"_128B")
                  PredRegs:$P, IntRegs:$R, timm:$s),
           (MI    PredRegs:$P, IntRegs:$R, imm:$s)>;
}

multiclass T_pRM_pat<InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID PredRegs:$P, IntRegs:$R, ModRegs:$M),
           (MI    PredRegs:$P, IntRegs:$R, ModRegs:$M)>;
  def: Pat<(!cast<Intrinsic>(IntID#"_128B")
                  PredRegs:$P, IntRegs:$R, ModRegs:$M),
           (MI    PredRegs:$P, IntRegs:$R, ModRegs:$M)>;
}

let Predicates = [UseHVXV62, UseHVX] in {
  defm: T_pRI_pat<V6_vL32b_pred_ai,       int_hexagon_V6_vL32b_pred_ai>;
  defm: T_pRI_pat<V6_vL32b_npred_ai,      int_hexagon_V6_vL32b_npred_ai>;
  defm: T_pRI_pat<V6_vL32b_pred_pi,       int_hexagon_V6_vL32b_pred_pi>;
  defm: T_pRI_pat<V6_vL32b_npred_pi,      int_hexagon_V6_vL32b_npred_pi>;
  defm: T_pRI_pat<V6_vL32b_nt_pred_ai,    int_hexagon_V6_vL32b_nt_pred_ai>;
  defm: T_pRI_pat<V6_vL32b_nt_npred_ai,   int_hexagon_V6_vL32b_nt_npred_ai>;
  defm: T_pRI_pat<V6_vL32b_nt_pred_pi,    int_hexagon_V6_vL32b_nt_pred_pi>;
  defm: T_pRI_pat<V6_vL32b_nt_npred_pi,   int_hexagon_V6_vL32b_nt_npred_pi>;

  defm: T_pRM_pat<V6_vL32b_pred_ppu,      int_hexagon_V6_vL32b_pred_ppu>;
  defm: T_pRM_pat<V6_vL32b_npred_ppu,     int_hexagon_V6_vL32b_npred_ppu>;
  defm: T_pRM_pat<V6_vL32b_nt_pred_ppu,   int_hexagon_V6_vL32b_nt_pred_ppu>;
  defm: T_pRM_pat<V6_vL32b_nt_npred_ppu,  int_hexagon_V6_vL32b_nt_npred_ppu>;
}

multiclass T_pRIV_pat<InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID PredRegs:$P, IntRegs:$R, timm:$s, HvxVR:$V),
           (MI    PredRegs:$P, IntRegs:$R, imm:$s, HvxVR:$V)>;
  def: Pat<(!cast<Intrinsic>(IntID#"_128B")
                  PredRegs:$P, IntRegs:$R, timm:$s, HvxVR:$V),
           (MI    PredRegs:$P, IntRegs:$R, imm:$s, HvxVR:$V)>;
}

multiclass T_pRMV_pat<InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID PredRegs:$P, IntRegs:$R, ModRegs:$M, HvxVR:$V),
           (MI    PredRegs:$P, IntRegs:$R, ModRegs:$M, HvxVR:$V)>;
  def: Pat<(!cast<Intrinsic>(IntID#"_128B")
                  PredRegs:$P, IntRegs:$R, ModRegs:$M, HvxVR:$V),
           (MI    PredRegs:$P, IntRegs:$R, ModRegs:$M, HvxVR:$V)>;
}

let Predicates = [UseHVXV60, UseHVX] in {
  defm: T_pRIV_pat<V6_vS32b_pred_ai,      int_hexagon_V6_vS32b_pred_ai>;
  defm: T_pRIV_pat<V6_vS32b_npred_ai,     int_hexagon_V6_vS32b_npred_ai>;
  defm: T_pRIV_pat<V6_vS32b_pred_pi,      int_hexagon_V6_vS32b_pred_pi>;
  defm: T_pRIV_pat<V6_vS32b_npred_pi,     int_hexagon_V6_vS32b_npred_pi>;
  defm: T_pRIV_pat<V6_vS32Ub_pred_ai,     int_hexagon_V6_vS32Ub_pred_ai>;
  defm: T_pRIV_pat<V6_vS32Ub_npred_ai,    int_hexagon_V6_vS32Ub_npred_ai>;
  defm: T_pRIV_pat<V6_vS32Ub_pred_pi,     int_hexagon_V6_vS32Ub_pred_pi>;
  defm: T_pRIV_pat<V6_vS32Ub_npred_pi,    int_hexagon_V6_vS32Ub_npred_pi>;
  defm: T_pRIV_pat<V6_vS32b_nt_pred_ai,   int_hexagon_V6_vS32b_nt_pred_ai>;
  defm: T_pRIV_pat<V6_vS32b_nt_npred_ai,  int_hexagon_V6_vS32b_nt_npred_ai>;
  defm: T_pRIV_pat<V6_vS32b_nt_pred_pi,   int_hexagon_V6_vS32b_nt_pred_pi>;
  defm: T_pRIV_pat<V6_vS32b_nt_npred_pi,  int_hexagon_V6_vS32b_nt_npred_pi>;

  defm: T_pRMV_pat<V6_vS32b_pred_ppu,     int_hexagon_V6_vS32b_pred_ppu>;
  defm: T_pRMV_pat<V6_vS32b_npred_ppu,    int_hexagon_V6_vS32b_npred_ppu>;
  defm: T_pRMV_pat<V6_vS32Ub_pred_ppu,    int_hexagon_V6_vS32Ub_pred_ppu>;
  defm: T_pRMV_pat<V6_vS32Ub_npred_ppu,   int_hexagon_V6_vS32Ub_npred_ppu>;
  defm: T_pRMV_pat<V6_vS32b_nt_pred_ppu,  int_hexagon_V6_vS32b_nt_pred_ppu>;
  defm: T_pRMV_pat<V6_vS32b_nt_npred_ppu, int_hexagon_V6_vS32b_nt_npred_ppu>;
}

include "HexagonDepMapAsm2Intrin.td"
