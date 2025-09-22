//===- HexagonIntrinsicsV60.td - V60 instruction intrinsics -*- tablegen *-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the Hexagon V60 Compiler Intrinsics in TableGen format.
//
//===----------------------------------------------------------------------===//


let AddedComplexity = 100 in {
def : Pat < (v16i32 (int_hexagon_V6_lo (v32i32 HvxWR:$src1))),
            (v16i32 (EXTRACT_SUBREG (v32i32 HvxWR:$src1), vsub_lo)) >;

def : Pat < (v16i32 (int_hexagon_V6_hi (v32i32 HvxWR:$src1))),
            (v16i32 (EXTRACT_SUBREG (v32i32 HvxWR:$src1), vsub_hi)) >;

def : Pat < (v32i32 (int_hexagon_V6_lo_128B (v64i32 HvxWR:$src1))),
            (v32i32 (EXTRACT_SUBREG (v64i32 HvxWR:$src1), vsub_lo)) >;

def : Pat < (v32i32 (int_hexagon_V6_hi_128B (v64i32 HvxWR:$src1))),
            (v32i32 (EXTRACT_SUBREG (v64i32 HvxWR:$src1), vsub_hi)) >;
}

def : Pat <(v64i1 (bitconvert (v16i32 HvxVR:$src1))),
           (v64i1 (V6_vandvrt(v16i32 HvxVR:$src1), (A2_tfrsi 0x01010101)))>;

def : Pat <(v64i1 (bitconvert (v32i16 HvxVR:$src1))),
           (v64i1 (V6_vandvrt(v32i16 HvxVR:$src1), (A2_tfrsi 0x01010101)))>;

def : Pat <(v64i1 (bitconvert (v64i8  HvxVR:$src1))),
           (v64i1 (V6_vandvrt(v64i8  HvxVR:$src1), (A2_tfrsi 0x01010101)))>;

def : Pat <(v16i32 (bitconvert (v64i1 HvxQR:$src1))),
           (v16i32 (V6_vandqrt(v64i1 HvxQR:$src1), (A2_tfrsi 0x01010101)))>;

def : Pat <(v32i16 (bitconvert (v64i1 HvxQR:$src1))),
           (v32i16 (V6_vandqrt(v64i1 HvxQR:$src1), (A2_tfrsi 0x01010101)))>;

def : Pat <(v64i8  (bitconvert (v64i1 HvxQR:$src1))),
           (v64i8  (V6_vandqrt(v64i1 HvxQR:$src1), (A2_tfrsi 0x01010101)))>;

def : Pat <(v128i1 (bitconvert (v32i32 HvxVR:$src1))),
           (v128i1 (V6_vandvrt (v32i32 HvxVR:$src1), (A2_tfrsi 0x01010101)))>;

def : Pat <(v128i1 (bitconvert (v64i16 HvxVR:$src1))),
           (v128i1 (V6_vandvrt (v64i16 HvxVR:$src1), (A2_tfrsi 0x01010101)))>;

def : Pat <(v128i1 (bitconvert (v128i8  HvxVR:$src1))),
           (v128i1 (V6_vandvrt (v128i8  HvxVR:$src1), (A2_tfrsi 0x01010101)))>;

def : Pat <(v32i32 (bitconvert (v128i1 HvxQR:$src1))),
           (v32i32 (V6_vandqrt (v128i1 HvxQR:$src1), (A2_tfrsi 0x01010101)))>;

def : Pat <(v64i16 (bitconvert (v128i1 HvxQR:$src1))),
           (v64i16 (V6_vandqrt (v128i1 HvxQR:$src1), (A2_tfrsi 0x01010101)))>;

def : Pat <(v128i8  (bitconvert (v128i1 HvxQR:$src1))),
           (v128i8  (V6_vandqrt (v128i1 HvxQR:$src1), (A2_tfrsi 0x01010101)))>;

let AddedComplexity = 140 in {
def : Pat <(store (v64i1 HvxQR:$src1), (i32 IntRegs:$addr)),
           (V6_vS32b_ai IntRegs:$addr, 0,
           (v16i32 (V6_vandqrt (v64i1 HvxQR:$src1),
                                       (A2_tfrsi 0x01010101))))>;

def : Pat <(v64i1 (load (i32 IntRegs:$addr))),
           (v64i1 (V6_vandvrt
           (v16i32 (V6_vL32b_ai IntRegs:$addr, 0)), (A2_tfrsi 0x01010101)))>;

def : Pat <(store (v128i1 HvxQR:$src1), (i32 IntRegs:$addr)),
           (V6_vS32b_ai IntRegs:$addr, 0,
           (v32i32 (V6_vandqrt (v128i1 HvxQR:$src1),
                                       (A2_tfrsi 0x01010101))))>;

def : Pat <(v128i1 (load (i32 IntRegs:$addr))),
           (v128i1 (V6_vandvrt
           (v32i32 (V6_vL32b_ai IntRegs:$addr, 0)), (A2_tfrsi 0x01010101)))>;
}

multiclass T_R_pat <InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID IntRegs:$src1), (MI IntRegs:$src1)>;
  def: Pat<(!cast<Intrinsic>(IntID#"_128B") IntRegs:$src1),
           (MI IntRegs:$src1)>;
}

multiclass T_V_pat <InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID HvxVR:$src1),
           (MI    HvxVR:$src1)>;

  def: Pat<(!cast<Intrinsic>(IntID#"_128B") HvxVR:$src1),
           (MI HvxVR:$src1)>;
}

multiclass T_W_pat <InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID HvxWR:$src1),
           (MI    HvxWR:$src1)>;

  def: Pat<(!cast<Intrinsic>(IntID#"_128B") HvxWR:$src1),
           (MI HvxWR:$src1)>;
}

multiclass T_Q_pat <InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID HvxQR:$src1),
           (MI    HvxQR:$src1)>;

  def: Pat<(!cast<Intrinsic>(IntID#"_128B") HvxQR:$src1),
           (MI HvxQR:$src1)>;
}

multiclass T_WR_pat <InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID HvxWR:$src1, IntRegs:$src2),
           (MI    HvxWR:$src1, IntRegs:$src2)>;

  def: Pat<(!cast<Intrinsic>(IntID#"_128B")HvxWR:$src1, IntRegs:$src2),
           (MI HvxWR:$src1, IntRegs:$src2)>;
}

multiclass T_VR_pat <InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID HvxVR:$src1, IntRegs:$src2),
           (MI    HvxVR:$src1, IntRegs:$src2)>;

  def: Pat<(!cast<Intrinsic>(IntID#"_128B")HvxVR:$src1, IntRegs:$src2),
           (MI HvxVR:$src1, IntRegs:$src2)>;
}

multiclass T_WV_pat <InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID HvxWR:$src1, HvxVR:$src2),
           (MI    HvxWR:$src1, HvxVR:$src2)>;

  def: Pat<(!cast<Intrinsic>(IntID#"_128B") HvxWR:$src1, HvxVR:$src2),
           (MI  HvxWR:$src1, HvxVR:$src2)>;
}

multiclass T_WW_pat <InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID HvxWR:$src1, HvxWR:$src2),
           (MI    HvxWR:$src1, HvxWR:$src2)>;

  def: Pat<(!cast<Intrinsic>(IntID#"_128B") HvxWR:$src1, HvxWR:$src2),
           (MI  HvxWR:$src1, HvxWR:$src2)>;
}

multiclass T_VV_pat <InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID HvxVR:$src1, HvxVR:$src2),
           (MI    HvxVR:$src1, HvxVR:$src2)>;

  def: Pat<(!cast<Intrinsic>(IntID#"_128B") HvxVR:$src1, HvxVR:$src2),
           (MI  HvxVR:$src1, HvxVR:$src2)>;
}

multiclass T_QR_pat <InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID HvxQR:$src1, IntRegs:$src2),
           (MI    HvxQR:$src1, IntRegs:$src2)>;

  def: Pat<(!cast<Intrinsic>(IntID#"_128B") HvxQR:$src1, IntRegs:$src2),
           (MI  HvxQR:$src1, IntRegs:$src2)>;
}

multiclass T_QQ_pat <InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID HvxQR:$src1, HvxQR:$src2),
           (MI    HvxQR:$src1, HvxQR:$src2)>;

  def: Pat<(!cast<Intrinsic>(IntID#"_128B") HvxQR:$src1, HvxQR:$src2),
           (MI  HvxQR:$src1, HvxQR:$src2)>;
}

multiclass T_WWR_pat <InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID HvxWR:$src1, HvxWR:$src2, IntRegs:$src3),
           (MI    HvxWR:$src1, HvxWR:$src2, IntRegs:$src3)>;

  def: Pat<(!cast<Intrinsic>(IntID#"_128B") HvxWR:$src1, HvxWR:$src2,
                                            IntRegs:$src3),
           (MI  HvxWR:$src1, HvxWR:$src2, IntRegs:$src3)>;
}

multiclass T_VVR_pat <InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID HvxVR:$src1, HvxVR:$src2, IntRegs:$src3),
           (MI    HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>;

  def: Pat<(!cast<Intrinsic>(IntID#"_128B") HvxVR:$src1, HvxVR:$src2,
                                            IntRegs:$src3),
           (MI  HvxVR:$src1, HvxVR:$src2, IntRegs:$src3)>;
}

multiclass T_WVR_pat <InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID HvxWR:$src1, HvxVR:$src2, IntRegs:$src3),
           (MI    HvxWR:$src1, HvxVR:$src2, IntRegs:$src3)>;

  def: Pat<(!cast<Intrinsic>(IntID#"_128B") HvxWR:$src1, HvxVR:$src2,
                                            IntRegs:$src3),
           (MI  HvxWR:$src1, HvxVR:$src2, IntRegs:$src3)>;
}

multiclass T_VWR_pat <InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID HvxVR:$src1, HvxWR:$src2, IntRegs:$src3),
           (MI    HvxVR:$src1, HvxWR:$src2, IntRegs:$src3)>;

  def: Pat<(!cast<Intrinsic>(IntID#"_128B") HvxVR:$src1, HvxWR:$src2,
                                            IntRegs:$src3),
           (MI  HvxVR:$src1, HvxWR:$src2, IntRegs:$src3)>;
}

multiclass T_VVV_pat <InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID HvxVR:$src1, HvxVR:$src2, HvxVR:$src3),
           (MI    HvxVR:$src1, HvxVR:$src2, HvxVR:$src3)>;

  def: Pat<(!cast<Intrinsic>(IntID#"_128B") HvxVR:$src1, HvxVR:$src2,
                                            HvxVR:$src3),
           (MI  HvxVR:$src1, HvxVR:$src2, HvxVR:$src3)>;
}

multiclass T_WVV_pat <InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID HvxWR:$src1, HvxVR:$src2, HvxVR:$src3),
           (MI    HvxWR:$src1, HvxVR:$src2, HvxVR:$src3)>;

  def: Pat<(!cast<Intrinsic>(IntID#"_128B") HvxWR:$src1, HvxVR:$src2,
                                            HvxVR:$src3),
           (MI  HvxWR:$src1, HvxVR:$src2, HvxVR:$src3)>;
}

multiclass T_QVV_pat <InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID HvxQR:$src1, HvxVR:$src2, HvxVR:$src3),
           (MI    HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>;

  def: Pat<(!cast<Intrinsic>(IntID#"_128B") HvxQR:$src1, HvxVR:$src2,
                                            HvxVR:$src3),
           (MI  HvxQR:$src1, HvxVR:$src2, HvxVR:$src3)>;
}

multiclass T_VQR_pat <InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID HvxVR:$src1, HvxQR:$src2, IntRegs:$src3),
           (MI    HvxVR:$src1, HvxQR:$src2, IntRegs:$src3)>;

  def: Pat<(!cast<Intrinsic>(IntID#"_128B") HvxVR:$src1, HvxQR:$src2,
                                            IntRegs:$src3),
           (MI  HvxVR:$src1, HvxQR:$src2, IntRegs:$src3)>;
}


multiclass T_QVR_pat <InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID HvxQR:$src1, HvxVR:$src2, IntRegs:$src3),
           (MI    HvxQR:$src1, HvxVR:$src2, IntRegs:$src3)>;

  def: Pat<(!cast<Intrinsic>(IntID#"_128B") HvxQR:$src1, HvxVR:$src2,
                                            IntRegs:$src3),
           (MI  HvxQR:$src1, HvxVR:$src2, IntRegs:$src3)>;
}

multiclass T_VVI_pat <InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID HvxVR:$src1, HvxVR:$src2, imm:$src3),
           (MI    HvxVR:$src1, HvxVR:$src2, imm:$src3)>;

  def: Pat<(!cast<Intrinsic>(IntID#"_128B") HvxVR:$src1,
                                            HvxVR:$src2, imm:$src3),
           (MI  HvxVR:$src1, HvxVR:$src2, imm:$src3)>;
}

multiclass T_WRI_pat <InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID HvxWR:$src1, IntRegs:$src2, imm:$src3),
           (MI    HvxWR:$src1, IntRegs:$src2, imm:$src3)>;

  def: Pat<(!cast<Intrinsic>(IntID#"_128B") HvxWR:$src1,
                                            IntRegs:$src2, imm:$src3),
           (MI  HvxWR:$src1, IntRegs:$src2, imm:$src3)>;
}

multiclass T_WWRI_pat <InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID HvxWR:$src1, HvxWR:$src2, IntRegs:$src3, imm:$src4),
           (MI   HvxWR:$src1, HvxWR:$src2, IntRegs:$src3, imm:$src4)>;

  def: Pat<(!cast<Intrinsic>(IntID#"_128B") HvxWR:$src1, HvxWR:$src2,
                                            IntRegs:$src3, imm:$src4),
           (MI  HvxWR:$src1, HvxWR:$src2, IntRegs:$src3, imm:$src4)>;
}

multiclass T_VVVR_pat <InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID HvxVR:$src1, HvxVR:$src2, HvxVR:$src3, IntRegs:$src4),
           (MI    HvxVR:$src1, HvxVR:$src2, HvxVR:$src3, IntRegs:$src4)>;

  def: Pat<(!cast<Intrinsic>(IntID#"_128B") HvxVR:$src1, HvxVR:$src2,
                                            HvxVR:$src3, IntRegs:$src4),
           (MI  HvxVR:$src1, HvxVR:$src2, HvxVR:$src3, IntRegs:$src4)>;
}

multiclass T_WVVR_pat <InstHexagon MI, Intrinsic IntID> {
  def: Pat<(IntID HvxWR:$src1, HvxVR:$src2, HvxVR:$src3, IntRegs:$src4),
           (MI    HvxWR:$src1, HvxVR:$src2, HvxVR:$src3, IntRegs:$src4)>;

  def: Pat<(!cast<Intrinsic>(IntID#"_128B") HvxWR:$src1, HvxVR:$src2,
                                            HvxVR:$src3, IntRegs:$src4),
           (MI  HvxWR:$src1, HvxVR:$src2, HvxVR:$src3, IntRegs:$src4)>;
}

defm : T_WR_pat <V6_vtmpyb, int_hexagon_V6_vtmpyb>;
defm : T_WR_pat <V6_vtmpybus, int_hexagon_V6_vtmpybus>;
defm : T_VR_pat <V6_vdmpyhb, int_hexagon_V6_vdmpyhb>;
defm : T_VR_pat <V6_vrmpyub, int_hexagon_V6_vrmpyub>;
defm : T_VR_pat <V6_vrmpybus, int_hexagon_V6_vrmpybus>;
defm : T_WR_pat <V6_vdsaduh, int_hexagon_V6_vdsaduh>;
defm : T_VR_pat <V6_vdmpybus, int_hexagon_V6_vdmpybus>;
defm : T_WR_pat <V6_vdmpybus_dv, int_hexagon_V6_vdmpybus_dv>;
defm : T_VR_pat <V6_vdmpyhsusat, int_hexagon_V6_vdmpyhsusat>;
defm : T_WR_pat <V6_vdmpyhsuisat, int_hexagon_V6_vdmpyhsuisat>;
defm : T_VR_pat <V6_vdmpyhsat, int_hexagon_V6_vdmpyhsat>;
defm : T_WR_pat <V6_vdmpyhisat, int_hexagon_V6_vdmpyhisat>;
defm : T_WR_pat <V6_vdmpyhb_dv, int_hexagon_V6_vdmpyhb_dv>;
defm : T_VR_pat <V6_vmpybus, int_hexagon_V6_vmpybus>;
defm : T_WR_pat <V6_vmpabus, int_hexagon_V6_vmpabus>;
defm : T_WR_pat <V6_vmpahb, int_hexagon_V6_vmpahb>;
defm : T_VR_pat <V6_vmpyh, int_hexagon_V6_vmpyh>;
defm : T_VR_pat <V6_vmpyhss, int_hexagon_V6_vmpyhss>;
defm : T_VR_pat <V6_vmpyhsrs, int_hexagon_V6_vmpyhsrs>;
defm : T_VR_pat <V6_vmpyuh, int_hexagon_V6_vmpyuh>;
defm : T_VR_pat <V6_vmpyihb, int_hexagon_V6_vmpyihb>;
defm : T_VR_pat <V6_vror, int_hexagon_V6_vror>;
defm : T_VR_pat <V6_vasrw, int_hexagon_V6_vasrw>;
defm : T_VR_pat <V6_vasrh, int_hexagon_V6_vasrh>;
defm : T_VR_pat <V6_vaslw, int_hexagon_V6_vaslw>;
defm : T_VR_pat <V6_vaslh, int_hexagon_V6_vaslh>;
defm : T_VR_pat <V6_vlsrw, int_hexagon_V6_vlsrw>;
defm : T_VR_pat <V6_vlsrh, int_hexagon_V6_vlsrh>;
defm : T_VR_pat <V6_vmpyiwh, int_hexagon_V6_vmpyiwh>;
defm : T_VR_pat <V6_vmpyiwb, int_hexagon_V6_vmpyiwb>;
defm : T_WR_pat <V6_vtmpyhb, int_hexagon_V6_vtmpyhb>;
defm : T_VR_pat <V6_vmpyub, int_hexagon_V6_vmpyub>;

defm : T_VV_pat <V6_vrmpyubv, int_hexagon_V6_vrmpyubv>;
defm : T_VV_pat <V6_vrmpybv, int_hexagon_V6_vrmpybv>;
defm : T_VV_pat <V6_vrmpybusv, int_hexagon_V6_vrmpybusv>;
defm : T_VV_pat <V6_vdmpyhvsat, int_hexagon_V6_vdmpyhvsat>;
defm : T_VV_pat <V6_vmpybv, int_hexagon_V6_vmpybv>;
defm : T_VV_pat <V6_vmpyubv, int_hexagon_V6_vmpyubv>;
defm : T_VV_pat <V6_vmpybusv, int_hexagon_V6_vmpybusv>;
defm : T_VV_pat <V6_vmpyhv, int_hexagon_V6_vmpyhv>;
defm : T_VV_pat <V6_vmpyuhv, int_hexagon_V6_vmpyuhv>;
defm : T_VV_pat <V6_vmpyhvsrs, int_hexagon_V6_vmpyhvsrs>;
defm : T_VV_pat <V6_vmpyhus, int_hexagon_V6_vmpyhus>;
defm : T_WW_pat <V6_vmpabusv, int_hexagon_V6_vmpabusv>;
defm : T_VV_pat <V6_vmpyih, int_hexagon_V6_vmpyih>;
defm : T_VV_pat <V6_vand, int_hexagon_V6_vand>;
defm : T_VV_pat <V6_vor, int_hexagon_V6_vor>;
defm : T_VV_pat <V6_vxor, int_hexagon_V6_vxor>;
defm : T_VV_pat <V6_vaddw, int_hexagon_V6_vaddw>;
defm : T_VV_pat <V6_vaddubsat, int_hexagon_V6_vaddubsat>;
defm : T_VV_pat <V6_vadduhsat, int_hexagon_V6_vadduhsat>;
defm : T_VV_pat <V6_vaddhsat, int_hexagon_V6_vaddhsat>;
defm : T_VV_pat <V6_vaddwsat, int_hexagon_V6_vaddwsat>;
defm : T_VV_pat <V6_vsubb, int_hexagon_V6_vsubb>;
defm : T_VV_pat <V6_vsubh, int_hexagon_V6_vsubh>;
defm : T_VV_pat <V6_vsubw, int_hexagon_V6_vsubw>;
defm : T_VV_pat <V6_vsububsat, int_hexagon_V6_vsububsat>;
defm : T_VV_pat <V6_vsubuhsat, int_hexagon_V6_vsubuhsat>;
defm : T_VV_pat <V6_vsubhsat, int_hexagon_V6_vsubhsat>;
defm : T_VV_pat <V6_vsubwsat, int_hexagon_V6_vsubwsat>;
defm : T_WW_pat <V6_vaddb_dv, int_hexagon_V6_vaddb_dv>;
defm : T_WW_pat <V6_vaddh_dv, int_hexagon_V6_vaddh_dv>;
defm : T_WW_pat <V6_vaddw_dv, int_hexagon_V6_vaddw_dv>;
defm : T_WW_pat <V6_vaddubsat_dv, int_hexagon_V6_vaddubsat_dv>;
defm : T_WW_pat <V6_vadduhsat_dv, int_hexagon_V6_vadduhsat_dv>;
defm : T_WW_pat <V6_vaddhsat_dv, int_hexagon_V6_vaddhsat_dv>;
defm : T_WW_pat <V6_vaddwsat_dv, int_hexagon_V6_vaddwsat_dv>;
defm : T_WW_pat <V6_vsubb_dv, int_hexagon_V6_vsubb_dv>;
defm : T_WW_pat <V6_vsubh_dv, int_hexagon_V6_vsubh_dv>;
defm : T_WW_pat <V6_vsubw_dv, int_hexagon_V6_vsubw_dv>;
defm : T_WW_pat <V6_vsububsat_dv, int_hexagon_V6_vsububsat_dv>;
defm : T_WW_pat <V6_vsubuhsat_dv, int_hexagon_V6_vsubuhsat_dv>;
defm : T_WW_pat <V6_vsubhsat_dv, int_hexagon_V6_vsubhsat_dv>;
defm : T_WW_pat <V6_vsubwsat_dv, int_hexagon_V6_vsubwsat_dv>;
defm : T_VV_pat <V6_vaddubh, int_hexagon_V6_vaddubh>;
defm : T_VV_pat <V6_vadduhw, int_hexagon_V6_vadduhw>;
defm : T_VV_pat <V6_vaddhw, int_hexagon_V6_vaddhw>;
defm : T_VV_pat <V6_vsububh, int_hexagon_V6_vsububh>;
defm : T_VV_pat <V6_vsubuhw, int_hexagon_V6_vsubuhw>;
defm : T_VV_pat <V6_vsubhw, int_hexagon_V6_vsubhw>;
defm : T_VV_pat <V6_vabsdiffub, int_hexagon_V6_vabsdiffub>;
defm : T_VV_pat <V6_vabsdiffh, int_hexagon_V6_vabsdiffh>;
defm : T_VV_pat <V6_vabsdiffuh, int_hexagon_V6_vabsdiffuh>;
defm : T_VV_pat <V6_vabsdiffw, int_hexagon_V6_vabsdiffw>;
defm : T_VV_pat <V6_vavgub, int_hexagon_V6_vavgub>;
defm : T_VV_pat <V6_vavguh, int_hexagon_V6_vavguh>;
defm : T_VV_pat <V6_vavgh, int_hexagon_V6_vavgh>;
defm : T_VV_pat <V6_vavgw, int_hexagon_V6_vavgw>;
defm : T_VV_pat <V6_vnavgub, int_hexagon_V6_vnavgub>;
defm : T_VV_pat <V6_vnavgh, int_hexagon_V6_vnavgh>;
defm : T_VV_pat <V6_vnavgw, int_hexagon_V6_vnavgw>;
defm : T_VV_pat <V6_vavgubrnd, int_hexagon_V6_vavgubrnd>;
defm : T_VV_pat <V6_vavguhrnd, int_hexagon_V6_vavguhrnd>;
defm : T_VV_pat <V6_vavghrnd, int_hexagon_V6_vavghrnd>;
defm : T_VV_pat <V6_vavgwrnd, int_hexagon_V6_vavgwrnd>;
defm : T_WW_pat <V6_vmpabuuv, int_hexagon_V6_vmpabuuv>;

defm : T_VVR_pat <V6_vdmpyhb_acc, int_hexagon_V6_vdmpyhb_acc>;
defm : T_VVR_pat <V6_vrmpyub_acc, int_hexagon_V6_vrmpyub_acc>;
defm : T_VVR_pat <V6_vrmpybus_acc, int_hexagon_V6_vrmpybus_acc>;
defm : T_VVR_pat <V6_vdmpybus_acc, int_hexagon_V6_vdmpybus_acc>;
defm : T_VVR_pat <V6_vdmpyhsusat_acc, int_hexagon_V6_vdmpyhsusat_acc>;
defm : T_VVR_pat <V6_vdmpyhsat_acc, int_hexagon_V6_vdmpyhsat_acc>;
defm : T_VVR_pat <V6_vmpyiwb_acc, int_hexagon_V6_vmpyiwb_acc>;
defm : T_VVR_pat <V6_vmpyiwh_acc, int_hexagon_V6_vmpyiwh_acc>;
defm : T_VVR_pat <V6_vmpyihb_acc, int_hexagon_V6_vmpyihb_acc>;
defm : T_VVR_pat <V6_vaslw_acc, int_hexagon_V6_vaslw_acc>;
defm : T_VVR_pat <V6_vasrw_acc, int_hexagon_V6_vasrw_acc>;

defm : T_VWR_pat <V6_vdmpyhsuisat_acc, int_hexagon_V6_vdmpyhsuisat_acc>;
defm : T_VWR_pat <V6_vdmpyhisat_acc, int_hexagon_V6_vdmpyhisat_acc>;

defm : T_WVR_pat <V6_vmpybus_acc, int_hexagon_V6_vmpybus_acc>;
defm : T_WVR_pat <V6_vmpyhsat_acc, int_hexagon_V6_vmpyhsat_acc>;
defm : T_WVR_pat <V6_vmpyuh_acc, int_hexagon_V6_vmpyuh_acc>;
defm : T_WVR_pat <V6_vmpyub_acc, int_hexagon_V6_vmpyub_acc>;

defm : T_WWR_pat <V6_vtmpyb_acc, int_hexagon_V6_vtmpyb_acc>;
defm : T_WWR_pat <V6_vtmpybus_acc, int_hexagon_V6_vtmpybus_acc>;
defm : T_WWR_pat <V6_vtmpyhb_acc, int_hexagon_V6_vtmpyhb_acc>;
defm : T_WWR_pat <V6_vdmpybus_dv_acc, int_hexagon_V6_vdmpybus_dv_acc>;
defm : T_WWR_pat <V6_vdmpyhb_dv_acc, int_hexagon_V6_vdmpyhb_dv_acc>;
defm : T_WWR_pat <V6_vmpabus_acc, int_hexagon_V6_vmpabus_acc>;
defm : T_WWR_pat <V6_vmpahb_acc, int_hexagon_V6_vmpahb_acc>;
defm : T_WWR_pat <V6_vdsaduh_acc, int_hexagon_V6_vdsaduh_acc>;

defm : T_VVV_pat <V6_vdmpyhvsat_acc, int_hexagon_V6_vdmpyhvsat_acc>;
defm : T_WVV_pat <V6_vmpybusv_acc, int_hexagon_V6_vmpybusv_acc>;
defm : T_WVV_pat <V6_vmpybv_acc, int_hexagon_V6_vmpybv_acc>;
defm : T_WVV_pat <V6_vmpyhus_acc, int_hexagon_V6_vmpyhus_acc>;
defm : T_WVV_pat <V6_vmpyhv_acc, int_hexagon_V6_vmpyhv_acc>;
defm : T_VVV_pat <V6_vmpyiewh_acc, int_hexagon_V6_vmpyiewh_acc>;
defm : T_VVV_pat <V6_vmpyiewuh_acc, int_hexagon_V6_vmpyiewuh_acc>;
defm : T_VVV_pat <V6_vmpyih_acc, int_hexagon_V6_vmpyih_acc>;
defm : T_VVV_pat <V6_vmpyowh_rnd_sacc, int_hexagon_V6_vmpyowh_rnd_sacc>;
defm : T_VVV_pat <V6_vmpyowh_sacc, int_hexagon_V6_vmpyowh_sacc>;
defm : T_WVV_pat <V6_vmpyubv_acc, int_hexagon_V6_vmpyubv_acc>;
defm : T_WVV_pat <V6_vmpyuhv_acc, int_hexagon_V6_vmpyuhv_acc>;
defm : T_VVV_pat <V6_vrmpybusv_acc, int_hexagon_V6_vrmpybusv_acc>;
defm : T_VVV_pat <V6_vrmpybv_acc, int_hexagon_V6_vrmpybv_acc>;
defm : T_VVV_pat <V6_vrmpyubv_acc, int_hexagon_V6_vrmpyubv_acc>;

// Compare instructions
defm : T_QVV_pat <V6_veqb_and, int_hexagon_V6_veqb_and>;
defm : T_QVV_pat <V6_veqh_and, int_hexagon_V6_veqh_and>;
defm : T_QVV_pat <V6_veqw_and, int_hexagon_V6_veqw_and>;
defm : T_QVV_pat <V6_vgtb_and, int_hexagon_V6_vgtb_and>;
defm : T_QVV_pat <V6_vgth_and, int_hexagon_V6_vgth_and>;
defm : T_QVV_pat <V6_vgtw_and, int_hexagon_V6_vgtw_and>;
defm : T_QVV_pat <V6_vgtub_and, int_hexagon_V6_vgtub_and>;
defm : T_QVV_pat <V6_vgtuh_and, int_hexagon_V6_vgtuh_and>;
defm : T_QVV_pat <V6_vgtuw_and, int_hexagon_V6_vgtuw_and>;
defm : T_QVV_pat <V6_veqb_or, int_hexagon_V6_veqb_or>;
defm : T_QVV_pat <V6_veqh_or, int_hexagon_V6_veqh_or>;
defm : T_QVV_pat <V6_veqw_or, int_hexagon_V6_veqw_or>;
defm : T_QVV_pat <V6_vgtb_or, int_hexagon_V6_vgtb_or>;
defm : T_QVV_pat <V6_vgth_or, int_hexagon_V6_vgth_or>;
defm : T_QVV_pat <V6_vgtw_or, int_hexagon_V6_vgtw_or>;
defm : T_QVV_pat <V6_vgtub_or, int_hexagon_V6_vgtub_or>;
defm : T_QVV_pat <V6_vgtuh_or, int_hexagon_V6_vgtuh_or>;
defm : T_QVV_pat <V6_vgtuw_or, int_hexagon_V6_vgtuw_or>;
defm : T_QVV_pat <V6_veqb_xor, int_hexagon_V6_veqb_xor>;
defm : T_QVV_pat <V6_veqh_xor, int_hexagon_V6_veqh_xor>;
defm : T_QVV_pat <V6_veqw_xor, int_hexagon_V6_veqw_xor>;
defm : T_QVV_pat <V6_vgtb_xor, int_hexagon_V6_vgtb_xor>;
defm : T_QVV_pat <V6_vgth_xor, int_hexagon_V6_vgth_xor>;
defm : T_QVV_pat <V6_vgtw_xor, int_hexagon_V6_vgtw_xor>;
defm : T_QVV_pat <V6_vgtub_xor, int_hexagon_V6_vgtub_xor>;
defm : T_QVV_pat <V6_vgtuh_xor, int_hexagon_V6_vgtuh_xor>;
defm : T_QVV_pat <V6_vgtuw_xor, int_hexagon_V6_vgtuw_xor>;

defm : T_VV_pat <V6_vminub, int_hexagon_V6_vminub>;
defm : T_VV_pat <V6_vminuh, int_hexagon_V6_vminuh>;
defm : T_VV_pat <V6_vminh, int_hexagon_V6_vminh>;
defm : T_VV_pat <V6_vminw, int_hexagon_V6_vminw>;
defm : T_VV_pat <V6_vmaxub, int_hexagon_V6_vmaxub>;
defm : T_VV_pat <V6_vmaxuh, int_hexagon_V6_vmaxuh>;
defm : T_VV_pat <V6_vmaxh, int_hexagon_V6_vmaxh>;
defm : T_VV_pat <V6_vmaxw, int_hexagon_V6_vmaxw>;
defm : T_VV_pat <V6_vdelta, int_hexagon_V6_vdelta>;
defm : T_VV_pat <V6_vrdelta, int_hexagon_V6_vrdelta>;
defm : T_VV_pat <V6_vdealb4w, int_hexagon_V6_vdealb4w>;
defm : T_VV_pat <V6_vmpyowh_rnd, int_hexagon_V6_vmpyowh_rnd>;
defm : T_VV_pat <V6_vshuffeb, int_hexagon_V6_vshuffeb>;
defm : T_VV_pat <V6_vshuffob, int_hexagon_V6_vshuffob>;
defm : T_VV_pat <V6_vshufeh, int_hexagon_V6_vshufeh>;
defm : T_VV_pat <V6_vshufoh, int_hexagon_V6_vshufoh>;
defm : T_VV_pat <V6_vshufoeh, int_hexagon_V6_vshufoeh>;
defm : T_VV_pat <V6_vshufoeb, int_hexagon_V6_vshufoeb>;
defm : T_VV_pat <V6_vcombine, int_hexagon_V6_vcombine>;
defm : T_VV_pat <V6_vmpyieoh, int_hexagon_V6_vmpyieoh>;
defm : T_VV_pat <V6_vsathub, int_hexagon_V6_vsathub>;
defm : T_VV_pat <V6_vsatwh, int_hexagon_V6_vsatwh>;
defm : T_VV_pat <V6_vroundwh, int_hexagon_V6_vroundwh>;
defm : T_VV_pat <V6_vroundwuh, int_hexagon_V6_vroundwuh>;
defm : T_VV_pat <V6_vroundhb, int_hexagon_V6_vroundhb>;
defm : T_VV_pat <V6_vroundhub, int_hexagon_V6_vroundhub>;
defm : T_VV_pat <V6_vasrwv, int_hexagon_V6_vasrwv>;
defm : T_VV_pat <V6_vlsrwv, int_hexagon_V6_vlsrwv>;
defm : T_VV_pat <V6_vlsrhv, int_hexagon_V6_vlsrhv>;
defm : T_VV_pat <V6_vasrhv, int_hexagon_V6_vasrhv>;
defm : T_VV_pat <V6_vaslwv, int_hexagon_V6_vaslwv>;
defm : T_VV_pat <V6_vaslhv, int_hexagon_V6_vaslhv>;
defm : T_VV_pat <V6_vaddb, int_hexagon_V6_vaddb>;
defm : T_VV_pat <V6_vaddh, int_hexagon_V6_vaddh>;
defm : T_VV_pat <V6_vmpyiewuh, int_hexagon_V6_vmpyiewuh>;
defm : T_VV_pat <V6_vmpyiowh, int_hexagon_V6_vmpyiowh>;
defm : T_VV_pat <V6_vpackeb, int_hexagon_V6_vpackeb>;
defm : T_VV_pat <V6_vpackeh, int_hexagon_V6_vpackeh>;
defm : T_VV_pat <V6_vpackhub_sat, int_hexagon_V6_vpackhub_sat>;
defm : T_VV_pat <V6_vpackhb_sat, int_hexagon_V6_vpackhb_sat>;
defm : T_VV_pat <V6_vpackwuh_sat, int_hexagon_V6_vpackwuh_sat>;
defm : T_VV_pat <V6_vpackwh_sat, int_hexagon_V6_vpackwh_sat>;
defm : T_VV_pat <V6_vpackob, int_hexagon_V6_vpackob>;
defm : T_VV_pat <V6_vpackoh, int_hexagon_V6_vpackoh>;
defm : T_VV_pat <V6_vmpyewuh, int_hexagon_V6_vmpyewuh>;
defm : T_VV_pat <V6_vmpyowh, int_hexagon_V6_vmpyowh>;

defm : T_QVV_pat <V6_vaddbq, int_hexagon_V6_vaddbq>;
defm : T_QVV_pat <V6_vaddhq, int_hexagon_V6_vaddhq>;
defm : T_QVV_pat <V6_vaddwq, int_hexagon_V6_vaddwq>;
defm : T_QVV_pat <V6_vaddbnq, int_hexagon_V6_vaddbnq>;
defm : T_QVV_pat <V6_vaddhnq, int_hexagon_V6_vaddhnq>;
defm : T_QVV_pat <V6_vaddwnq, int_hexagon_V6_vaddwnq>;
defm : T_QVV_pat <V6_vsubbq, int_hexagon_V6_vsubbq>;
defm : T_QVV_pat <V6_vsubhq, int_hexagon_V6_vsubhq>;
defm : T_QVV_pat <V6_vsubwq, int_hexagon_V6_vsubwq>;
defm : T_QVV_pat <V6_vsubbnq, int_hexagon_V6_vsubbnq>;
defm : T_QVV_pat <V6_vsubhnq, int_hexagon_V6_vsubhnq>;
defm : T_QVV_pat <V6_vsubwnq, int_hexagon_V6_vsubwnq>;

defm : T_V_pat <V6_vabsh, int_hexagon_V6_vabsh>;
defm : T_V_pat <V6_vabsw, int_hexagon_V6_vabsw>;
defm : T_V_pat <V6_vabsw_sat, int_hexagon_V6_vabsw_sat>;
defm : T_V_pat <V6_vabsh_sat, int_hexagon_V6_vabsh_sat>;
defm : T_V_pat <V6_vnot, int_hexagon_V6_vnot>;
defm : T_V_pat <V6_vassign, int_hexagon_V6_vassign>;
defm : T_V_pat <V6_vzb, int_hexagon_V6_vzb>;
defm : T_V_pat <V6_vzh, int_hexagon_V6_vzh>;
defm : T_V_pat <V6_vsb, int_hexagon_V6_vsb>;
defm : T_V_pat <V6_vsh, int_hexagon_V6_vsh>;
defm : T_V_pat <V6_vdealh, int_hexagon_V6_vdealh>;
defm : T_V_pat <V6_vdealb, int_hexagon_V6_vdealb>;
defm : T_V_pat <V6_vunpackub, int_hexagon_V6_vunpackub>;
defm : T_V_pat <V6_vunpackuh, int_hexagon_V6_vunpackuh>;
defm : T_V_pat <V6_vunpackb, int_hexagon_V6_vunpackb>;
defm : T_V_pat <V6_vunpackh, int_hexagon_V6_vunpackh>;
defm : T_V_pat <V6_vshuffh, int_hexagon_V6_vshuffh>;
defm : T_V_pat <V6_vshuffb, int_hexagon_V6_vshuffb>;
defm : T_V_pat <V6_vcl0w, int_hexagon_V6_vcl0w>;
defm : T_V_pat <V6_vpopcounth, int_hexagon_V6_vpopcounth>;
defm : T_V_pat <V6_vcl0h, int_hexagon_V6_vcl0h>;
defm : T_V_pat <V6_vnormamtw, int_hexagon_V6_vnormamtw>;
defm : T_V_pat <V6_vnormamth, int_hexagon_V6_vnormamth>;

defm : T_W_pat <V6_lo, int_hexagon_V6_lo>;
defm : T_W_pat <V6_hi, int_hexagon_V6_hi>;
defm : T_W_pat <V6_vassignp, int_hexagon_V6_vassignp>;

defm : T_WRI_pat <V6_vrmpybusi, int_hexagon_V6_vrmpybusi>;
defm : T_WRI_pat <V6_vrsadubi, int_hexagon_V6_vrsadubi>;
defm : T_WRI_pat <V6_vrmpyubi, int_hexagon_V6_vrmpyubi>;

defm : T_WWRI_pat <V6_vrmpybusi_acc, int_hexagon_V6_vrmpybusi_acc>;
defm : T_WWRI_pat <V6_vrsadubi_acc, int_hexagon_V6_vrsadubi_acc>;
defm : T_WWRI_pat <V6_vrmpyubi_acc, int_hexagon_V6_vrmpyubi_acc>;

// assembler mapped.
//defm : T_V_pat <V6_vtran2x2, int_hexagon_V6_vtran2x2>;
// not present earlier.. need to add intrinsic
defm : T_VVR_pat <V6_valignb, int_hexagon_V6_valignb>;
defm : T_VVR_pat <V6_vlalignb, int_hexagon_V6_vlalignb>;
defm : T_VVR_pat <V6_vasrwh, int_hexagon_V6_vasrwh>;
defm : T_VVR_pat <V6_vasrwhsat, int_hexagon_V6_vasrwhsat>;
defm : T_VVR_pat <V6_vasrwhrndsat, int_hexagon_V6_vasrwhrndsat>;
defm : T_VVR_pat <V6_vasrwuhsat, int_hexagon_V6_vasrwuhsat>;
defm : T_VVR_pat <V6_vasrhubsat, int_hexagon_V6_vasrhubsat>;
defm : T_VVR_pat <V6_vasrhubrndsat, int_hexagon_V6_vasrhubrndsat>;
defm : T_VVR_pat <V6_vasrhbrndsat, int_hexagon_V6_vasrhbrndsat>;

defm : T_VVR_pat <V6_vshuffvdd, int_hexagon_V6_vshuffvdd>;
defm : T_VVR_pat <V6_vdealvdd, int_hexagon_V6_vdealvdd>;

defm : T_WV_pat <V6_vunpackob, int_hexagon_V6_vunpackob>;
defm : T_WV_pat <V6_vunpackoh, int_hexagon_V6_vunpackoh>;
defm : T_VVI_pat <V6_valignbi, int_hexagon_V6_valignbi>;
defm : T_VVI_pat <V6_vlalignbi, int_hexagon_V6_vlalignbi>;

defm : T_QVV_pat <V6_vswap, int_hexagon_V6_vswap>;
defm : T_QVV_pat <V6_vmux, int_hexagon_V6_vmux>;
defm : T_QQ_pat <V6_pred_and, int_hexagon_V6_pred_and>;
defm : T_QQ_pat <V6_pred_or, int_hexagon_V6_pred_or>;
defm : T_Q_pat <V6_pred_not, int_hexagon_V6_pred_not>;
defm : T_QQ_pat <V6_pred_xor, int_hexagon_V6_pred_xor>;
defm : T_QQ_pat <V6_pred_or_n, int_hexagon_V6_pred_or_n>;
defm : T_QQ_pat <V6_pred_and_n, int_hexagon_V6_pred_and_n>;
defm : T_VV_pat <V6_veqb, int_hexagon_V6_veqb>;
defm : T_VV_pat <V6_veqh, int_hexagon_V6_veqh>;
defm : T_VV_pat <V6_veqw, int_hexagon_V6_veqw>;
defm : T_VV_pat <V6_vgtb, int_hexagon_V6_vgtb>;
defm : T_VV_pat <V6_vgth, int_hexagon_V6_vgth>;
defm : T_VV_pat <V6_vgtw, int_hexagon_V6_vgtw>;
defm : T_VV_pat <V6_vgtub, int_hexagon_V6_vgtub>;
defm : T_VV_pat <V6_vgtuh, int_hexagon_V6_vgtuh>;
defm : T_VV_pat <V6_vgtuw, int_hexagon_V6_vgtuw>;

defm : T_VQR_pat <V6_vandqrt_acc, int_hexagon_V6_vandqrt_acc>;
defm : T_QVR_pat <V6_vandvrt_acc, int_hexagon_V6_vandvrt_acc>;
defm : T_QR_pat <V6_vandqrt, int_hexagon_V6_vandqrt>;
defm : T_R_pat <V6_lvsplatw, int_hexagon_V6_lvsplatw>;
defm : T_R_pat <V6_pred_scalar2, int_hexagon_V6_pred_scalar2>;
defm : T_VR_pat <V6_vandvrt, int_hexagon_V6_vandvrt>;

defm : T_VVR_pat <V6_vlutvvb, int_hexagon_V6_vlutvvb>;
defm : T_VVR_pat <V6_vlutvwh, int_hexagon_V6_vlutvwh>;
defm : T_VVVR_pat <V6_vlutvvb_oracc, int_hexagon_V6_vlutvvb_oracc>;
defm : T_WVVR_pat <V6_vlutvwh_oracc, int_hexagon_V6_vlutvwh_oracc>;

defm : T_QVR_pat <V6_vandvrt_acc, int_hexagon_V6_vandvrt_acc>;
def : T_PI_pat <S6_rol_i_p, int_hexagon_S6_rol_i_p>;
def : T_RI_pat <S6_rol_i_r, int_hexagon_S6_rol_i_r>;
def : T_PPI_pat <S6_rol_i_p_nac, int_hexagon_S6_rol_i_p_nac>;
def : T_PPI_pat <S6_rol_i_p_acc, int_hexagon_S6_rol_i_p_acc>;
def : T_PPI_pat <S6_rol_i_p_and, int_hexagon_S6_rol_i_p_and>;
def : T_PPI_pat <S6_rol_i_p_or, int_hexagon_S6_rol_i_p_or>;
def : T_PPI_pat <S6_rol_i_p_xacc, int_hexagon_S6_rol_i_p_xacc>;
def : T_RRI_pat <S6_rol_i_r_nac, int_hexagon_S6_rol_i_r_nac>;
def : T_RRI_pat <S6_rol_i_r_acc, int_hexagon_S6_rol_i_r_acc>;
def : T_RRI_pat <S6_rol_i_r_and, int_hexagon_S6_rol_i_r_and>;
def : T_RRI_pat <S6_rol_i_r_or, int_hexagon_S6_rol_i_r_or>;
def : T_RRI_pat <S6_rol_i_r_xacc, int_hexagon_S6_rol_i_r_xacc>;

defm : T_VR_pat <V6_extractw, int_hexagon_V6_extractw>;
defm : T_VR_pat <V6_vinsertwr, int_hexagon_V6_vinsertwr>;

//def : T_PPQ_pat <S2_cabacencbin, int_hexagon_S2_cabacencbin>;

def: Pat<(v64i16 (trunc v64i32:$Vdd)),
         (v64i16 (V6_vpackwh_sat
                 (v32i32 (V6_hi HvxWR:$Vdd)),
                 (v32i32 (V6_lo HvxWR:$Vdd))))>;

def: Pat<(int_hexagon_V6_vd0),      (V6_vd0)>;
def: Pat<(int_hexagon_V6_vd0_128B), (V6_vd0)>;

