//===--- arm_sme.td - ARM SME compiler interface ------------------------===//
//
//  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
//  See https://llvm.org/LICENSE.txt for license information.
//  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the TableGen definitions from which the ARM SME header
//  file will be generated.  See:
//
//      https://developer.arm.com/architectures/system-architectures/software-standards/acle
//
//===----------------------------------------------------------------------===//

include "arm_sve_sme_incl.td"

let SVETargetGuard = InvalidMode in {

////////////////////////////////////////////////////////////////////////////////
// Loads

multiclass ZALoad<string n_suffix, string t, string i_prefix, list<ImmCheck> ch> {
  let SMETargetGuard = "sme" in {
    def NAME # _H : MInst<"svld1_hor_" # n_suffix, "vimPQ", t,
                          [IsLoad, IsOverloadNone, IsStreaming, IsInOutZA],
                          MemEltTyDefault, i_prefix # "_horiz", ch>;

    def NAME # _H_VNUM : MInst<"svld1_hor_vnum_" # n_suffix, "vimPQl", t,
                               [IsLoad, IsOverloadNone, IsStreaming, IsInOutZA],
                               MemEltTyDefault, i_prefix # "_horiz", ch>;

    def NAME # _V : MInst<"svld1_ver_" # n_suffix, "vimPQ", t,
                          [IsLoad, IsOverloadNone, IsStreaming, IsInOutZA],
                          MemEltTyDefault, i_prefix # "_vert", ch>;

    def NAME # _V_VNUM : MInst<"svld1_ver_vnum_" # n_suffix, "vimPQl", t,
                               [IsLoad, IsOverloadNone, IsStreaming, IsInOutZA],
                               MemEltTyDefault, i_prefix # "_vert", ch>;
  }
}

defm SVLD1_ZA8 : ZALoad<"za8", "c", "aarch64_sme_ld1b", [ImmCheck<0, ImmCheck0_0>]>;
defm SVLD1_ZA16 : ZALoad<"za16", "s", "aarch64_sme_ld1h", [ImmCheck<0, ImmCheck0_1>]>;
defm SVLD1_ZA32 : ZALoad<"za32", "i", "aarch64_sme_ld1w", [ImmCheck<0, ImmCheck0_3>]>;
defm SVLD1_ZA64 : ZALoad<"za64", "l", "aarch64_sme_ld1d", [ImmCheck<0, ImmCheck0_7>]>;
defm SVLD1_ZA128 : ZALoad<"za128", "q", "aarch64_sme_ld1q", [ImmCheck<0, ImmCheck0_15>]>;

let SMETargetGuard = "sme" in {
def SVLDR_VNUM_ZA : MInst<"svldr_vnum_za", "vmQl", "",
                          [IsOverloadNone, IsStreamingCompatible, IsInOutZA],
                          MemEltTyDefault, "aarch64_sme_ldr">;

def SVLDR_ZA : MInst<"svldr_za", "vmQ", "",
                          [IsOverloadNone, IsStreamingCompatible, IsInOutZA],
                          MemEltTyDefault, "aarch64_sme_ldr", []>;
}

////////////////////////////////////////////////////////////////////////////////
// Stores

multiclass ZAStore<string n_suffix, string t, string i_prefix, list<ImmCheck> ch> {
  let SMETargetGuard = "sme" in {
    def NAME # _H : MInst<"svst1_hor_" # n_suffix, "vimP%", t,
                          [IsStore, IsOverloadNone, IsStreaming, IsInZA],
                          MemEltTyDefault, i_prefix # "_horiz", ch>;

    def NAME # _H_VNUM : MInst<"svst1_hor_vnum_" # n_suffix, "vimP%l", t,
                               [IsStore, IsOverloadNone, IsStreaming, IsInZA],
                               MemEltTyDefault, i_prefix # "_horiz", ch>;

    def NAME # _V : MInst<"svst1_ver_" # n_suffix, "vimP%", t,
                          [IsStore, IsOverloadNone, IsStreaming, IsInZA],
                          MemEltTyDefault, i_prefix # "_vert", ch>;

    def NAME # _V_VNUM : MInst<"svst1_ver_vnum_" # n_suffix, "vimP%l", t,
                               [IsStore, IsOverloadNone, IsStreaming, IsInZA],
                               MemEltTyDefault, i_prefix # "_vert", ch>;
  }
}

defm SVST1_ZA8 : ZAStore<"za8", "c", "aarch64_sme_st1b", [ImmCheck<0, ImmCheck0_0>]>;
defm SVST1_ZA16 : ZAStore<"za16", "s", "aarch64_sme_st1h", [ImmCheck<0, ImmCheck0_1>]>;
defm SVST1_ZA32 : ZAStore<"za32", "i", "aarch64_sme_st1w", [ImmCheck<0, ImmCheck0_3>]>;
defm SVST1_ZA64 : ZAStore<"za64", "l", "aarch64_sme_st1d", [ImmCheck<0, ImmCheck0_7>]>;
defm SVST1_ZA128 : ZAStore<"za128", "q", "aarch64_sme_st1q", [ImmCheck<0, ImmCheck0_15>]>;

let SMETargetGuard = "sme" in {
def SVSTR_VNUM_ZA : MInst<"svstr_vnum_za", "vm%l", "",
                          [IsOverloadNone, IsStreamingCompatible, IsInZA],
                          MemEltTyDefault, "aarch64_sme_str">;

def SVSTR_ZA : MInst<"svstr_za", "vm%", "",
                      [IsOverloadNone, IsStreamingCompatible, IsInZA],
                      MemEltTyDefault, "aarch64_sme_str", []>;
}

////////////////////////////////////////////////////////////////////////////////
// Read horizontal/vertical ZA slices

multiclass ZARead<string n_suffix, string t, string i_prefix, list<ImmCheck> ch> {
  let SMETargetGuard = "sme" in {
    def NAME # _H : SInst<"svread_hor_" # n_suffix # "[_{d}]", "ddPim", t,
                          MergeOp1, i_prefix # "_horiz",
                          [IsReadZA, IsStreaming, IsInZA], ch>;

    def NAME # _V : SInst<"svread_ver_" # n_suffix # "[_{d}]", "ddPim", t,
                          MergeOp1, i_prefix # "_vert",
                          [IsReadZA, IsStreaming, IsInZA], ch>;
  }
}

defm SVREAD_ZA8 : ZARead<"za8", "cUc", "aarch64_sme_read", [ImmCheck<2, ImmCheck0_0>]>;
defm SVREAD_ZA16 : ZARead<"za16", "sUshb", "aarch64_sme_read", [ImmCheck<2, ImmCheck0_1>]>;
defm SVREAD_ZA32 : ZARead<"za32", "iUif", "aarch64_sme_read", [ImmCheck<2, ImmCheck0_3>]>;
defm SVREAD_ZA64 : ZARead<"za64", "lUld", "aarch64_sme_read", [ImmCheck<2, ImmCheck0_7>]>;
defm SVREAD_ZA128 : ZARead<"za128", "csilUcUsUiUlhbfd", "aarch64_sme_readq", [ImmCheck<2, ImmCheck0_15>]>;

////////////////////////////////////////////////////////////////////////////////
// Write horizontal/vertical ZA slices

multiclass ZAWrite<string n_suffix, string t, string i_prefix, list<ImmCheck> ch> {
  let SMETargetGuard = "sme" in {
    def NAME # _H : SInst<"svwrite_hor_" # n_suffix # "[_{d}]", "vimPd", t,
                          MergeOp1, i_prefix # "_horiz",
                          [IsWriteZA, IsStreaming, IsInOutZA], ch>;

    def NAME # _V : SInst<"svwrite_ver_" # n_suffix # "[_{d}]", "vimPd", t,
                          MergeOp1, i_prefix # "_vert",
                          [IsWriteZA, IsStreaming, IsInOutZA], ch>;
  }
}

defm SVWRITE_ZA8 : ZAWrite<"za8", "cUc", "aarch64_sme_write", [ImmCheck<0, ImmCheck0_0>]>;
defm SVWRITE_ZA16 : ZAWrite<"za16", "sUshb", "aarch64_sme_write", [ImmCheck<0, ImmCheck0_1>]>;
defm SVWRITE_ZA32 : ZAWrite<"za32", "iUif", "aarch64_sme_write", [ImmCheck<0, ImmCheck0_3>]>;
defm SVWRITE_ZA64 : ZAWrite<"za64", "lUld", "aarch64_sme_write", [ImmCheck<0, ImmCheck0_7>]>;
defm SVWRITE_ZA128 : ZAWrite<"za128", "csilUcUsUiUlhbfd", "aarch64_sme_writeq", [ImmCheck<0, ImmCheck0_15>]>;

////////////////////////////////////////////////////////////////////////////////
// SME - Zero

let SMETargetGuard = "sme" in {
  def SVZERO_MASK_ZA : SInst<"svzero_mask_za", "vi", "", MergeNone, "aarch64_sme_zero",
                             [IsOverloadNone, IsStreamingCompatible, IsInOutZA],
                             [ImmCheck<0, ImmCheck0_255>]>;
  def SVZERO_ZA      : SInst<"svzero_za", "vv", "", MergeNone, "aarch64_sme_zero",
                             [IsOverloadNone, IsStreamingCompatible, IsOutZA]>;
}

let SMETargetGuard = "sme2p1" in {
  def SVZERO_ZA64_VG1x2 : SInst<"svzero_za64_vg1x2", "vm", "", MergeNone, "aarch64_sme_zero_za64_vg1x2",
                            [IsOverloadNone, IsStreaming, IsInOutZA]>;
  def SVZERO_ZA64_VG1x4 : SInst<"svzero_za64_vg1x4", "vm", "", MergeNone, "aarch64_sme_zero_za64_vg1x4",
                            [IsOverloadNone, IsStreaming, IsInOutZA]>;
  def SVZERO_ZA64_VG2x1 : SInst<"svzero_za64_vg2x1", "vm", "", MergeNone, "aarch64_sme_zero_za64_vg2x1",
                            [IsOverloadNone, IsStreaming, IsInOutZA]>;
  def SVZERO_ZA64_VG2x2 : SInst<"svzero_za64_vg2x2", "vm", "", MergeNone, "aarch64_sme_zero_za64_vg2x2",
                            [IsOverloadNone, IsStreaming, IsInOutZA]>;
  def SVZERO_ZA64_VG2x4 : SInst<"svzero_za64_vg2x4", "vm", "", MergeNone, "aarch64_sme_zero_za64_vg2x4",
                            [IsOverloadNone, IsStreaming, IsInOutZA]>;
  def SVZERO_ZA64_VG4x1 : SInst<"svzero_za64_vg4x1", "vm", "", MergeNone, "aarch64_sme_zero_za64_vg4x1",
                            [IsOverloadNone, IsStreaming, IsInOutZA]>;
  def SVZERO_ZA64_VG4x2 : SInst<"svzero_za64_vg4x2", "vm", "", MergeNone, "aarch64_sme_zero_za64_vg4x2",
                            [IsOverloadNone, IsStreaming, IsInOutZA]>;
  def SVZERO_ZA64_VG4x4 : SInst<"svzero_za64_vg4x4", "vm", "", MergeNone, "aarch64_sme_zero_za64_vg4x4",
                            [IsOverloadNone, IsStreaming, IsInOutZA]>;
}

////////////////////////////////////////////////////////////////////////////////
// SME - Counting elements in a streaming vector

multiclass ZACount<string n_suffix> {
  let SMETargetGuard = "sme" in {
    def NAME : SInst<"sv" # n_suffix, "nv", "", MergeNone,
                      "aarch64_sme_" # n_suffix,
                      [IsOverloadNone, IsStreamingCompatible]>;
  }
}

defm SVCNTSB : ZACount<"cntsb">;
defm SVCNTSH : ZACount<"cntsh">;
defm SVCNTSW : ZACount<"cntsw">;
defm SVCNTSD : ZACount<"cntsd">;

////////////////////////////////////////////////////////////////////////////////
// SME - ADDHA/ADDVA

multiclass ZAAdd<string n_suffix> {
  let SMETargetGuard = "sme" in {
    def NAME # _ZA32: SInst<"sv" # n_suffix # "_za32[_{d}]", "viPPd", "iUi", MergeOp1,
                      "aarch64_sme_" # n_suffix, [IsStreaming, IsInOutZA],
                      [ImmCheck<0, ImmCheck0_3>]>;
  }

  let SMETargetGuard = "sme-i16i64" in {
    def NAME # _ZA64: SInst<"sv" # n_suffix # "_za64[_{d}]", "viPPd", "lUl", MergeOp1,
                     "aarch64_sme_" # n_suffix, [IsStreaming, IsInOutZA],
                     [ImmCheck<0, ImmCheck0_7>]>;
  }
}

defm SVADDHA : ZAAdd<"addha">;
defm SVADDVA : ZAAdd<"addva">;

////////////////////////////////////////////////////////////////////////////////
// SME - SMOPA, SMOPS, UMOPA, UMOPS

multiclass ZAIntOuterProd<string n_suffix1, string n_suffix2> {
  let SMETargetGuard = "sme" in {
    def NAME # _ZA32_B: SInst<"sv" # n_suffix2 # "_za32[_{d}]",
                              "viPPdd", !cond(!eq(n_suffix1, "s") : "", true: "U") # "c",
                              MergeOp1, "aarch64_sme_" # n_suffix1 # n_suffix2 # "_wide",
                              [IsStreaming, IsInOutZA],
                              [ImmCheck<0, ImmCheck0_3>]>;
  }

  let SMETargetGuard = "sme-i16i64" in {
    def NAME # _ZA64_H: SInst<"sv" # n_suffix2 # "_za64[_{d}]",
                              "viPPdd", !cond(!eq(n_suffix1, "s") : "", true: "U") # "s",
                              MergeOp1, "aarch64_sme_" # n_suffix1 # n_suffix2 # "_wide",
                              [IsStreaming, IsInOutZA],
                              [ImmCheck<0, ImmCheck0_7>]>;
  }
}

defm SVSMOPA : ZAIntOuterProd<"s", "mopa">;
defm SVSMOPS : ZAIntOuterProd<"s", "mops">;
defm SVUMOPA : ZAIntOuterProd<"u", "mopa">;
defm SVUMOPS : ZAIntOuterProd<"u", "mops">;

////////////////////////////////////////////////////////////////////////////////
// SME - SUMOPA, SUMOPS, USMOPA, USMOPS

multiclass ZAIntOuterProdMixedSigns<string n_suffix1, string n_suffix2> {
  let SMETargetGuard = "sme" in {
    def NAME # _ZA32_B: SInst<"sv" # n_suffix1 # n_suffix2 # "_za32[_{d}]",
                              "viPPd" # !cond(!eq(n_suffix1, "su") : "u", true: "x"),
                              !cond(!eq(n_suffix1, "su") : "", true: "U") # "c",
                              MergeOp1, "aarch64_sme_" # n_suffix1 # n_suffix2 # "_wide",
                              [IsStreaming, IsInOutZA],
                              [ImmCheck<0, ImmCheck0_3>]>;
  }

  let SMETargetGuard = "sme-i16i64" in {
    def NAME # _ZA64_H: SInst<"sv" # n_suffix1 # n_suffix2 # "_za64[_{d}]",
                              "viPPd" # !cond(!eq(n_suffix1, "su") : "u", true: "x"),
                              !cond(!eq(n_suffix1, "su") : "", true: "U") # "s",
                              MergeOp1, "aarch64_sme_" # n_suffix1 # n_suffix2 # "_wide",
                              [IsStreaming, IsInOutZA],
                              [ImmCheck<0, ImmCheck0_7>]>;
  }
}

defm SVSUMOPA : ZAIntOuterProdMixedSigns<"su", "mopa">;
defm SVSUMOPS : ZAIntOuterProdMixedSigns<"su", "mops">;
defm SVUSMOPA : ZAIntOuterProdMixedSigns<"us", "mopa">;
defm SVUSMOPS : ZAIntOuterProdMixedSigns<"us", "mops">;

////////////////////////////////////////////////////////////////////////////////
// SME - FMOPA, FMOPS

multiclass ZAFPOuterProd<string n_suffix> {
  let SMETargetGuard = "sme" in {
    def NAME # _ZA32_B: SInst<"sv" # n_suffix # "_za32[_{d}]", "viPPdd", "h",
                              MergeOp1, "aarch64_sme_" # n_suffix # "_wide",
                              [IsStreaming, IsInOutZA],
                              [ImmCheck<0, ImmCheck0_3>]>;

    def NAME # _ZA32_H: SInst<"sv" # n_suffix # "_za32[_{d}]", "viPPdd", "b",
                              MergeOp1, "aarch64_sme_" # n_suffix # "_wide",
                              [IsStreaming, IsInOutZA],
                              [ImmCheck<0, ImmCheck0_3>]>;

    def NAME # _ZA32_S: SInst<"sv" # n_suffix # "_za32[_{d}]", "viPPdd", "f",
                              MergeOp1, "aarch64_sme_" # n_suffix,
                              [IsStreaming, IsInOutZA],
                              [ImmCheck<0, ImmCheck0_3>]>;
  }

  let SMETargetGuard = "sme-f64f64" in {
    def NAME # _ZA64_D: SInst<"sv" # n_suffix # "_za64[_{d}]", "viPPdd", "d",
                              MergeOp1, "aarch64_sme_" # n_suffix,
                              [IsStreaming, IsInOutZA],
                              [ImmCheck<0, ImmCheck0_7>]>;
  }
}

defm SVMOPA : ZAFPOuterProd<"mopa">;
defm SVMOPS : ZAFPOuterProd<"mops">;

////////////////////////////////////////////////////////////////////////////////
// SME2 - ADD, SUB

multiclass ZAAddSub<string n_suffix> {
  let SMETargetGuard = "sme2" in {
    def NAME # _WRITE_SINGLE_ZA32_VG1X2_I32 : Inst<"sv" # n_suffix # "_write[_single]_za32[_{d}]_vg1x2", "vm2d", "iUi", MergeNone, "aarch64_sme_" # n_suffix # "_write_single_za_vg1x2", [IsStreaming, IsInOutZA], []>;
    def NAME # _WRITE_SINGLE_ZA32_VG1X4_I32 : Inst<"sv" # n_suffix # "_write[_single]_za32[_{d}]_vg1x4", "vm4d", "iUi", MergeNone, "aarch64_sme_" # n_suffix # "_write_single_za_vg1x4", [IsStreaming, IsInOutZA], []>;

    def NAME # _WRITE_ZA32_VG1X2_I32 : Inst<"sv" # n_suffix # "_write_za32[_{d}]_vg1x2", "vm22", "iUi", MergeNone, "aarch64_sme_" # n_suffix # "_write_za_vg1x2", [IsStreaming, IsInOutZA], []>;
    def NAME # _WRITE_ZA32_VG1X4_I32 : Inst<"sv" # n_suffix # "_write_za32[_{d}]_vg1x4", "vm44", "iUi", MergeNone, "aarch64_sme_" # n_suffix # "_write_za_vg1x4", [IsStreaming, IsInOutZA], []>;

    def NAME # _ZA32_VG1x2_I32 : Inst<"sv" # n_suffix # "_za32[_{d}]_vg1x2", "vm2", "iUif", MergeNone, "aarch64_sme_" # n_suffix # "_za32_vg1x2", [IsStreaming, IsInOutZA], []>;
    def NAME # _ZA32_VG1X4_I32 : Inst<"sv" # n_suffix # "_za32[_{d}]_vg1x4", "vm4", "iUif", MergeNone, "aarch64_sme_" # n_suffix # "_za32_vg1x4", [IsStreaming, IsInOutZA], []>;
  }

  let SMETargetGuard = "sme2,sme-i16i64" in {
    def NAME # _WRITE_SINGLE_ZA64_VG1X2_I64 : Inst<"sv" # n_suffix # "_write[_single]_za64[_{d}]_vg1x2", "vm2d", "lUl", MergeNone, "aarch64_sme_" # n_suffix # "_write_single_za_vg1x2", [IsStreaming, IsInOutZA], []>;
    def NAME # _WRITE_SINGLE_ZA64_VG1X4_I64 : Inst<"sv" # n_suffix # "_write[_single]_za64[_{d}]_vg1x4", "vm4d", "lUl", MergeNone, "aarch64_sme_" # n_suffix # "_write_single_za_vg1x4", [IsStreaming, IsInOutZA], []>;

    def NAME # _WRITE_ZA64_VG1x2_I64 : Inst<"sv" # n_suffix # "_write_za64[_{d}]_vg1x2", "vm22", "lUl", MergeNone, "aarch64_sme_" # n_suffix # "_write_za_vg1x2", [IsStreaming, IsInOutZA], []>;
    def NAME # _WRITE_ZA64_VG1x4_I64 : Inst<"sv" # n_suffix # "_write_za64[_{d}]_vg1x4", "vm44", "lUl", MergeNone, "aarch64_sme_" # n_suffix # "_write_za_vg1x4", [IsStreaming, IsInOutZA], []>;

    def NAME # _ZA64_VG1X2_I64 : Inst<"sv" # n_suffix # "_za64[_{d}]_vg1x2", "vm2", "lUl", MergeNone, "aarch64_sme_" # n_suffix # "_za64_vg1x2", [IsStreaming, IsInOutZA], []>;
    def NAME # _ZA64_VG1X4_I64 : Inst<"sv" # n_suffix # "_za64[_{d}]_vg1x4", "vm4", "lUl", MergeNone, "aarch64_sme_" # n_suffix # "_za64_vg1x4", [IsStreaming, IsInOutZA], []>;
  }

  let SMETargetGuard = "sme2,sme-f64f64" in {
    def NAME # _ZA64_VG1X2_F64 : Inst<"sv" # n_suffix # "_za64[_{d}]_vg1x2", "vm2", "d", MergeNone, "aarch64_sme_" # n_suffix # "_za64_vg1x2", [IsStreaming, IsInOutZA], []>;
    def NAME # _ZA64_VG1X4_F64 : Inst<"sv" # n_suffix # "_za64[_{d}]_vg1x4", "vm4", "d", MergeNone, "aarch64_sme_" # n_suffix # "_za64_vg1x4", [IsStreaming, IsInOutZA], []>;
  }

  let SMETargetGuard = "sme-f16f16|sme-f8f16" in {
    def NAME # _ZA16_VG1X2_F16 : Inst<"sv" # n_suffix # "_za16[_{d}]_vg1x2", "vm2", "h", MergeNone, "aarch64_sme_" # n_suffix # "_za16_vg1x2", [IsStreaming, IsInOutZA], []>;
    def NAME # _ZA16_VG1X4_F16 : Inst<"sv" # n_suffix # "_za16[_{d}]_vg1x4", "vm4", "h", MergeNone, "aarch64_sme_" # n_suffix # "_za16_vg1x4", [IsStreaming, IsInOutZA], []>;
  }

  let SMETargetGuard = "sme2,b16b16" in {
    def NAME # _ZA16_VG1X2_BF16 : Inst<"sv" # n_suffix # "_za16[_{d}]_vg1x2", "vm2", "b", MergeNone, "aarch64_sme_" # n_suffix # "_za16_vg1x2", [IsStreaming, IsInOutZA], []>;
    def NAME # _ZA16_VG1X4_BF16 : Inst<"sv" # n_suffix # "_za16[_{d}]_vg1x4", "vm4", "b", MergeNone, "aarch64_sme_" # n_suffix # "_za16_vg1x4", [IsStreaming, IsInOutZA], []>;
  }
}

defm SVADD : ZAAddSub<"add">;
defm SVSUB : ZAAddSub<"sub">;

// SME2 - MOVA

//
// Single, 2 and 4 vector-group read/write intrinsics.
//

multiclass ZAWrite_VG<string n, string t, string i, list<ImmCheck> checks> {
  def NAME # _VG2_H : Inst<"svwrite_hor_" # n # "[_{d}]_vg2",   "vim2", t, MergeNone, i # "_hor_vg2", [IsInOutZA, IsStreaming], checks>;
  def NAME # _VG2_V : Inst<"svwrite_ver_" # n # "[_{d}]_vg2",   "vim2", t, MergeNone, i # "_ver_vg2", [IsInOutZA, IsStreaming], checks>;
  def NAME # _VG4_H : Inst<"svwrite_hor_" # n # "[_{d}]_vg4",   "vim4", t, MergeNone, i # "_hor_vg4", [IsInOutZA, IsStreaming], checks>;
  def NAME # _VG4_V : Inst<"svwrite_ver_" # n # "[_{d}]_vg4",   "vim4", t, MergeNone, i # "_ver_vg4", [IsInOutZA, IsStreaming], checks>;
  def NAME # _VG1x2 : Inst<"svwrite_"     # n # "[_{d}]_vg1x2", "vm2",  t, MergeNone, i # "_vg1x2",   [IsInOutZA, IsStreaming], []>;
  def NAME # _VG1x4 : Inst<"svwrite_"     # n # "[_{d}]_vg1x4", "vm4",  t, MergeNone, i # "_vg1x4",   [IsInOutZA, IsStreaming], []>;
}

let SMETargetGuard = "sme2" in {
  defm SVWRITE_ZA8  : ZAWrite_VG<"za8",  "cUc",   "aarch64_sme_write", [ImmCheck<0, ImmCheck0_0>]>;
  defm SVWRITE_ZA16 : ZAWrite_VG<"za16", "sUshb", "aarch64_sme_write", [ImmCheck<0, ImmCheck0_1>]>;
  defm SVWRITE_ZA32 : ZAWrite_VG<"za32", "iUif",  "aarch64_sme_write", [ImmCheck<0, ImmCheck0_3>]>;
  defm SVWRITE_ZA64 : ZAWrite_VG<"za64", "lUld",  "aarch64_sme_write", [ImmCheck<0, ImmCheck0_7>]>;
}

multiclass ZARead_VG<string n, string t, string i, list<ImmCheck> checks> {
  def NAME # _VG2_H : Inst<"svread_hor_" # n # "_{d}_vg2",   "2im", t, MergeNone, i # "_hor_vg2", [IsInZA, IsStreaming], checks>;
  def NAME # _VG2_V : Inst<"svread_ver_" # n # "_{d}_vg2",   "2im", t, MergeNone, i # "_ver_vg2", [IsInZA, IsStreaming], checks>;
  def NAME # _VG4_H : Inst<"svread_hor_" # n # "_{d}_vg4",   "4im", t, MergeNone, i # "_hor_vg4", [IsInZA, IsStreaming], checks>;
  def NAME # _VG4_V : Inst<"svread_ver_" # n # "_{d}_vg4",   "4im", t, MergeNone, i # "_ver_vg4", [IsInZA, IsStreaming], checks>;
  def NAME # _VG1x2 : Inst<"svread_"     # n # "_{d}_vg1x2", "2m",  t, MergeNone, i # "_vg1x2",   [IsInZA, IsStreaming], []>;
  def NAME # _VG1x4 : Inst<"svread_"     # n # "_{d}_vg1x4", "4m",  t, MergeNone, i # "_vg1x4",   [IsInZA, IsStreaming], []>;
}

let SMETargetGuard = "sme2" in {
  defm SVREAD_ZA8  : ZARead_VG<"za8",  "cUc",   "aarch64_sme_read", [ImmCheck<0, ImmCheck0_0>]>;
  defm SVREAD_ZA16 : ZARead_VG<"za16", "sUshb", "aarch64_sme_read", [ImmCheck<0, ImmCheck0_1>]>;
  defm SVREAD_ZA32 : ZARead_VG<"za32", "iUif",  "aarch64_sme_read", [ImmCheck<0, ImmCheck0_3>]>;
  defm SVREAD_ZA64 : ZARead_VG<"za64", "lUld",  "aarch64_sme_read", [ImmCheck<0, ImmCheck0_7>]>;
}

//
// Outer product and accumulate/subtract
//

let SMETargetGuard = "sme2" in {
  def SVSMOPA  : Inst<"svmopa_za32[_{d}]_m", "viPPdd", "s", MergeNone, "aarch64_sme_smopa_za32", [IsInOutZA, IsStreaming], [ImmCheck<0, ImmCheck0_3>]>;
  def SVUSMOPA : Inst<"svmopa_za32[_{d}]_m", "viPPdd", "Us", MergeNone, "aarch64_sme_umopa_za32", [IsInOutZA, IsStreaming], [ImmCheck<0, ImmCheck0_3>]>;

  def SVSMOPS  : Inst<"svmops_za32[_{d}]_m", "viPPdd", "s", MergeNone, "aarch64_sme_smops_za32", [IsInOutZA, IsStreaming], [ImmCheck<0, ImmCheck0_3>]>;
  def SVUSMOPS : Inst<"svmops_za32[_{d}]_m", "viPPdd", "Us", MergeNone, "aarch64_sme_umops_za32", [IsInOutZA, IsStreaming], [ImmCheck<0, ImmCheck0_3>]>;

  def SVBMOPA : Inst<"svbmopa_za32[_{d}]_m", "viPPdd", "iUi", MergeNone, "aarch64_sme_bmopa_za32", [IsInOutZA, IsStreaming], [ImmCheck<0, ImmCheck0_3>]>;

  def SVBMOPS : Inst<"svbmops_za32[_{d}]_m", "viPPdd", "iUi", MergeNone, "aarch64_sme_bmops_za32", [IsInOutZA, IsStreaming], [ImmCheck<0, ImmCheck0_3>]>;

  // VERTICAL DOT-PRODUCT
  def SVVDOT_LANE_ZA32_VG1x2_S : Inst<"svvdot_lane_za32[_{d}]_vg1x2", "vm2di", "s", MergeNone, "aarch64_sme_svdot_lane_za32_vg1x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_3>]>;
  def SVVDOT_LANE_ZA32_VG1x4_S : Inst<"svvdot_lane_za32[_{d}]_vg1x4", "vm4di", "c", MergeNone, "aarch64_sme_svdot_lane_za32_vg1x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_3>]>;
  def SVVDOT_LANE_ZA32_VG1x2_U : Inst<"svvdot_lane_za32[_{d}]_vg1x2", "vm2di", "Us", MergeNone, "aarch64_sme_uvdot_lane_za32_vg1x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_3>]>;
  def SVVDOT_LANE_ZA32_VG1x4_U : Inst<"svvdot_lane_za32[_{d}]_vg1x4", "vm4di", "Uc", MergeNone, "aarch64_sme_uvdot_lane_za32_vg1x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_3>]>;
  def SVVDOT_LANE_ZA32_VG1x2_F : Inst<"svvdot_lane_za32[_{d}]_vg1x2", "vm2di", "hb", MergeNone, "aarch64_sme_fvdot_lane_za32_vg1x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_3>]>;
  def SVSUVDOT_LANE_ZA32_VG1x4 : Inst<"svsuvdot_lane_za32[_{d}]_vg1x4", "vm4di", "c", MergeNone, "aarch64_sme_suvdot_lane_za32_vg1x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_3>]>;
  def SVUSVDOT_LANE_ZA32_VG1x4 : Inst<"svusvdot_lane_za32[_{d}]_vg1x4", "vm4di", "Uc", MergeNone, "aarch64_sme_usvdot_lane_za32_vg1x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_3>]>;

  // Multi-vector signed & unsigned integer dot-product
  def SVDOT_MULTI_ZA32_VG1x2_S  : Inst<"svdot_za32[_{d}]_vg1x2", "vm22", "cs", MergeNone, "aarch64_sme_sdot_za32_vg1x2", [IsStreaming, IsInOutZA], []>;
  def SVDOT_MULTI_ZA32_VG1x4_S  : Inst<"svdot_za32[_{d}]_vg1x4", "vm44", "cs", MergeNone, "aarch64_sme_sdot_za32_vg1x4", [IsStreaming, IsInOutZA], []>;
  def SVDOT_MULTI_ZA32_VG1x2_U  : Inst<"svdot_za32[_{d}]_vg1x2", "vm22", "UcUs", MergeNone, "aarch64_sme_udot_za32_vg1x2", [IsStreaming, IsInOutZA], []>;
  def SVDOT_MULTI_ZA32_VG1x4_U  : Inst<"svdot_za32[_{d}]_vg1x4", "vm44", "UcUs", MergeNone, "aarch64_sme_udot_za32_vg1x4", [IsStreaming, IsInOutZA], []>;
  def SVDOT_SINGLE_ZA32_VG1x2_S : Inst<"svdot[_single]_za32[_{d}]_vg1x2", "vm2d", "cs", MergeNone, "aarch64_sme_sdot_single_za32_vg1x2", [IsStreaming, IsInOutZA], []>;
  def SVDOT_SINGLE_ZA32_VG1x4_S : Inst<"svdot[_single]_za32[_{d}]_vg1x4", "vm4d", "cs", MergeNone, "aarch64_sme_sdot_single_za32_vg1x4", [IsStreaming, IsInOutZA], []>;
  def SVDOT_SINGLE_ZA32_VG1x2_U : Inst<"svdot[_single]_za32[_{d}]_vg1x2", "vm2d", "UcUs", MergeNone, "aarch64_sme_udot_single_za32_vg1x2", [IsStreaming, IsInOutZA], []>;
  def SVDOT_SINGLE_ZA32_VG1x4_U : Inst<"svdot[_single]_za32[_{d}]_vg1x4", "vm4d", "UcUs", MergeNone, "aarch64_sme_udot_single_za32_vg1x4", [IsStreaming, IsInOutZA], []>;
  def SVDOT_LANE_ZA32_VG1x2_S   : Inst<"svdot_lane_za32[_{d}]_vg1x2", "vm2di", "cs", MergeNone, "aarch64_sme_sdot_lane_za32_vg1x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_3>]>;
  def SVDOT_LANE_ZA32_VG1x4_S   : Inst<"svdot_lane_za32[_{d}]_vg1x4", "vm4di", "cs", MergeNone, "aarch64_sme_sdot_lane_za32_vg1x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_3>]>;
  def SVDOT_LANE_ZA32_VG1x2_U   : Inst<"svdot_lane_za32[_{d}]_vg1x2", "vm2di", "UcUs", MergeNone, "aarch64_sme_udot_lane_za32_vg1x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_3>]>;
  def SVDOT_LANE_ZA32_VG1x4_U   : Inst<"svdot_lane_za32[_{d}]_vg1x4", "vm4di", "UcUs", MergeNone, "aarch64_sme_udot_lane_za32_vg1x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_3>]>;

  def SVUSDOT_SINGLE_ZA32_VG1x2 : Inst<"svusdot[_single]_za32[_{d}]_vg1x2", "vm2.dx", "Uc", MergeNone, "aarch64_sme_usdot_single_za32_vg1x2", [IsStreaming, IsInOutZA], []>;
  def SVUSDOT_SINGLE_ZA32_VG1x4 : Inst<"svusdot[_single]_za32[_{d}]_vg1x4", "vm4.dx", "Uc", MergeNone, "aarch64_sme_usdot_single_za32_vg1x4", [IsStreaming, IsInOutZA], []>;
  def SVUSDOT_MULTI_ZA32_VG1x2  : Inst<"svusdot_za32[_{d}]_vg1x2", "vm2.d2.x", "Uc", MergeNone, "aarch64_sme_usdot_za32_vg1x2", [IsStreaming, IsInOutZA], []>;
  def SVUSDOT_MULTI_ZA32_VG1x4  : Inst<"svusdot_za32[_{d}]_vg1x4", "vm4.d4.x", "Uc", MergeNone, "aarch64_sme_usdot_za32_vg1x4", [IsStreaming, IsInOutZA], []>;
  def SVUSDOT_LANE_ZA32_VG1x2   : Inst<"svusdot_lane_za32[_{d}]_vg1x2", "vm2.dxi", "Uc", MergeNone, "aarch64_sme_usdot_lane_za32_vg1x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_3>]>;
  def SVUSDOT_LANE_ZA32_VG1x4   : Inst<"svusdot_lane_za32[_{d}]_vg1x4", "vm4.dxi", "Uc", MergeNone, "aarch64_sme_usdot_lane_za32_vg1x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_3>]>;

  def SVSUDOT_SINGLE_ZA32_VG1x2 : Inst<"svsudot[_single]_za32[_{d}]_vg1x2", "vm2.du", "c", MergeNone, "aarch64_sme_sudot_single_za32_vg1x2", [IsStreaming, IsInOutZA], []>;
  def SVSUDOT_SINGLE_ZA32_VG1x4 : Inst<"svsudot[_single]_za32[_{d}]_vg1x4", "vm4.du", "c", MergeNone, "aarch64_sme_sudot_single_za32_vg1x4", [IsStreaming, IsInOutZA], []>;

  // Multi-multi sudot builtins are mapped to usdot, with zn & zm operands swapped
  def SVSUDOT_MULTI_ZA32_VG1x2  : Inst<"svsudot_za32[_{d}]_vg1x2", "vm2.d2.u", "c", MergeNone, "aarch64_sme_usdot_za32_vg1x2", [IsStreaming, IsInOutZA], []>;
  def SVSUDOT_MULTI_ZA32_VG1x4  : Inst<"svsudot_za32[_{d}]_vg1x4", "vm4.d4.u", "c", MergeNone, "aarch64_sme_usdot_za32_vg1x4", [IsStreaming, IsInOutZA], []>;

  def SVSUDOT_LANE_ZA32_VG1x2   : Inst<"svsudot_lane_za32[_{d}]_vg1x2", "vm2.dui", "c", MergeNone, "aarch64_sme_sudot_lane_za32_vg1x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_3>]>;
  def SVSUDOT_LANE_ZA32_VG1x4   : Inst<"svsudot_lane_za32[_{d}]_vg1x4", "vm4.dui", "c", MergeNone, "aarch64_sme_sudot_lane_za32_vg1x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_3>]>;

  // Multi-vector half-precision/BFloat16 floating-point dot-product
  def SVDOT_MULTI_ZA32_VG1x2_F16  : Inst<"svdot_za32[_{d}]_vg1x2", "vm22", "bh", MergeNone, "aarch64_sme_fdot_za32_vg1x2", [IsStreaming, IsInOutZA], []>;
  def SVDOT_MULTI_ZA32_VG1x4_F16  : Inst<"svdot_za32[_{d}]_vg1x4", "vm44", "bh", MergeNone, "aarch64_sme_fdot_za32_vg1x4", [IsStreaming, IsInOutZA], []>;
  def SVDOT_SINGLE_ZA32_VG1x2_F16 : Inst<"svdot[_single]_za32[_{d}]_vg1x2", "vm2d", "bh", MergeNone, "aarch64_sme_fdot_single_za32_vg1x2", [IsStreaming, IsInOutZA], []>;
  def SVDOT_SINGLE_ZA32_VG1x4_F16 : Inst<"svdot[_single]_za32[_{d}]_vg1x4", "vm4d", "bh", MergeNone, "aarch64_sme_fdot_single_za32_vg1x4", [IsStreaming, IsInOutZA], []>;
  def SVDOT_LANE_ZA32_VG1x2_F16   : Inst<"svdot_lane_za32[_{d}]_vg1x2", "vm2di", "bh", MergeNone, "aarch64_sme_fdot_lane_za32_vg1x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_3>]>;
  def SVDOT_LANE_ZA32_VG1x4_F16   : Inst<"svdot_lane_za32[_{d}]_vg1x4", "vm4di", "bh", MergeNone, "aarch64_sme_fdot_lane_za32_vg1x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_3>]>;
}

let SMETargetGuard = "sme2,sme-i16i64" in {
  def SVVDOT_LANE_ZA64_VG1x4_S : Inst<"svvdot_lane_za64[_{d}]_vg1x4", "vm4di", "s", MergeNone, "aarch64_sme_svdot_lane_za64_vg1x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_1>]>;
  def SVVDOT_LANE_ZA64_VG1x4_U : Inst<"svvdot_lane_za64[_{d}]_vg1x4", "vm4di", "Us", MergeNone, "aarch64_sme_uvdot_lane_za64_vg1x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_1>]>;

  def SVDOT_MULTI_ZA64_VG1x2_S16  : Inst<"svdot_za64[_{d}]_vg1x2", "vm22", "s", MergeNone, "aarch64_sme_sdot_za64_vg1x2", [IsStreaming, IsInOutZA], []>;
  def SVDOT_MULTI_ZA64_VG1x4_S16  : Inst<"svdot_za64[_{d}]_vg1x4", "vm44", "s", MergeNone, "aarch64_sme_sdot_za64_vg1x4", [IsStreaming, IsInOutZA], []>;
  def SVDOT_MULTI_ZA64_VG1x2_U16  : Inst<"svdot_za64[_{d}]_vg1x2", "vm22", "Us", MergeNone, "aarch64_sme_udot_za64_vg1x2", [IsStreaming, IsInOutZA], []>;
  def SVDOT_MULTI_ZA64_VG1x4_U16  : Inst<"svdot_za64[_{d}]_vg1x4", "vm44", "Us", MergeNone, "aarch64_sme_udot_za64_vg1x4", [IsStreaming, IsInOutZA], []>;
  def SVDOT_SINGLE_ZA64_VG1x2_S16 : Inst<"svdot[_single]_za64[_{d}]_vg1x2", "vm2d", "s", MergeNone, "aarch64_sme_sdot_single_za64_vg1x2", [IsStreaming, IsInOutZA], []>;
  def SVDOT_SINGLE_ZA64_VG1x4_S16 : Inst<"svdot[_single]_za64[_{d}]_vg1x4", "vm4d", "s", MergeNone, "aarch64_sme_sdot_single_za64_vg1x4", [IsStreaming, IsInOutZA], []>;
  def SVDOT_SINGLE_ZA64_VG1x2_U16 : Inst<"svdot[_single]_za64[_{d}]_vg1x2", "vm2d", "Us", MergeNone, "aarch64_sme_udot_single_za64_vg1x2", [IsStreaming, IsInOutZA], []>;
  def SVDOT_SINGLE_ZA64_VG1x4_U16 : Inst<"svdot[_single]_za64[_{d}]_vg1x4", "vm4d", "Us", MergeNone, "aarch64_sme_udot_single_za64_vg1x4", [IsStreaming, IsInOutZA], []>;
  def SVDOT_LANE_ZA64_VG1x2_S16   : Inst<"svdot_lane_za64[_{d}]_vg1x2", "vm2di", "s", MergeNone, "aarch64_sme_sdot_lane_za64_vg1x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_1>]>;
  def SVDOT_LANE_ZA64_VG1x4_S16   : Inst<"svdot_lane_za64[_{d}]_vg1x4", "vm4di", "s", MergeNone, "aarch64_sme_sdot_lane_za64_vg1x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_1>]>;
  def SVDOT_LANE_ZA64_VG1x2_U16   : Inst<"svdot_lane_za64[_{d}]_vg1x2", "vm2di", "Us", MergeNone, "aarch64_sme_udot_lane_za64_vg1x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_1>]>;
  def SVDOT_LANE_ZA64_VG1x4_U16   : Inst<"svdot_lane_za64[_{d}]_vg1x4", "vm4di", "Us", MergeNone, "aarch64_sme_udot_lane_za64_vg1x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_1>]>;
}

// FMLA/FMLS
let SMETargetGuard = "sme2" in {
  def SVMLA_MULTI_VG1x2_F32 : Inst<"svmla_za32[_{d}]_vg1x2", "vm22", "f", MergeNone, "aarch64_sme_fmla_vg1x2", [IsStreaming, IsInOutZA], []>;
  def SVMLA_MULTI_VG1x4_F32 : Inst<"svmla_za32[_{d}]_vg1x4", "vm44", "f", MergeNone, "aarch64_sme_fmla_vg1x4", [IsStreaming, IsInOutZA], []>;
  def SVMLS_MULTI_VG1x2_F32 : Inst<"svmls_za32[_{d}]_vg1x2", "vm22", "f", MergeNone, "aarch64_sme_fmls_vg1x2", [IsStreaming, IsInOutZA], []>;
  def SVMLS_MULTI_VG1x4_F32 : Inst<"svmls_za32[_{d}]_vg1x4", "vm44", "f", MergeNone, "aarch64_sme_fmls_vg1x4", [IsStreaming, IsInOutZA], []>;

  def SVMLA_SINGLE_VG1x2_F32 : Inst<"svmla[_single]_za32[_{d}]_vg1x2", "vm2d", "f", MergeNone, "aarch64_sme_fmla_single_vg1x2", [IsStreaming, IsInOutZA], []>;
  def SVMLA_SINGLE_VG1x4_F32 : Inst<"svmla[_single]_za32[_{d}]_vg1x4", "vm4d", "f", MergeNone, "aarch64_sme_fmla_single_vg1x4", [IsStreaming, IsInOutZA], []>;
  def SVMLS_SINGLE_VG1x2_F32 : Inst<"svmls[_single]_za32[_{d}]_vg1x2", "vm2d", "f", MergeNone, "aarch64_sme_fmls_single_vg1x2", [IsStreaming, IsInOutZA], []>;
  def SVMLS_SINGLE_VG1x4_F32 : Inst<"svmls[_single]_za32[_{d}]_vg1x4", "vm4d", "f", MergeNone, "aarch64_sme_fmls_single_vg1x4", [IsStreaming, IsInOutZA], []>;

  def SVMLA_LANE_VG1x2_F32 : Inst<"svmla_lane_za32[_{d}]_vg1x2", "vm2di", "f", MergeNone, "aarch64_sme_fmla_lane_vg1x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_3>]>;
  def SVMLA_LANE_VG1x4_F32 : Inst<"svmla_lane_za32[_{d}]_vg1x4", "vm4di", "f", MergeNone, "aarch64_sme_fmla_lane_vg1x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_3>]>;
  def SVMLS_LANE_VG1x2_F32 : Inst<"svmls_lane_za32[_{d}]_vg1x2", "vm2di", "f", MergeNone, "aarch64_sme_fmls_lane_vg1x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_3>]>;
  def SVMLS_LANE_VG1x4_F32 : Inst<"svmls_lane_za32[_{d}]_vg1x4", "vm4di", "f", MergeNone, "aarch64_sme_fmls_lane_vg1x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_3>]>;
}

let SMETargetGuard = "sme2,sme-f64f64" in {
  def SVMLA_MULTI_VG1x2_F64 : Inst<"svmla_za64[_{d}]_vg1x2", "vm22", "d", MergeNone, "aarch64_sme_fmla_vg1x2", [IsStreaming, IsInOutZA], []>;
  def SVMLA_MULTI_VG1x4_F64 : Inst<"svmla_za64[_{d}]_vg1x4", "vm44", "d", MergeNone, "aarch64_sme_fmla_vg1x4", [IsStreaming, IsInOutZA], []>;
  def SVMLS_MULTI_VG1x2_F64 : Inst<"svmls_za64[_{d}]_vg1x2", "vm22", "d", MergeNone, "aarch64_sme_fmls_vg1x2", [IsStreaming, IsInOutZA], []>;
  def SVMLS_MULTI_VG1x4_F64 : Inst<"svmls_za64[_{d}]_vg1x4", "vm44", "d", MergeNone, "aarch64_sme_fmls_vg1x4", [IsStreaming, IsInOutZA], []>;

  def SVMLA_SINGLE_VG1x2_F64 : Inst<"svmla[_single]_za64[_{d}]_vg1x2", "vm2d", "d", MergeNone, "aarch64_sme_fmla_single_vg1x2", [IsStreaming, IsInOutZA], []>;
  def SVMLA_SINGLE_VG1x4_F64 : Inst<"svmla[_single]_za64[_{d}]_vg1x4", "vm4d", "d", MergeNone, "aarch64_sme_fmla_single_vg1x4", [IsStreaming, IsInOutZA], []>;
  def SVMLS_SINGLE_VG1x2_F64 : Inst<"svmls[_single]_za64[_{d}]_vg1x2", "vm2d", "d", MergeNone, "aarch64_sme_fmls_single_vg1x2", [IsStreaming, IsInOutZA], []>;
  def SVMLS_SINGLE_VG1x4_F64 : Inst<"svmls[_single]_za64[_{d}]_vg1x4", "vm4d", "d", MergeNone, "aarch64_sme_fmls_single_vg1x4", [IsStreaming, IsInOutZA], []>;

  def SVMLA_LANE_VG1x2_F64 : Inst<"svmla_lane_za64[_{d}]_vg1x2", "vm2di", "d", MergeNone, "aarch64_sme_fmla_lane_vg1x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_1>]>;
  def SVMLA_LANE_VG1x4_F64 : Inst<"svmla_lane_za64[_{d}]_vg1x4", "vm4di", "d", MergeNone, "aarch64_sme_fmla_lane_vg1x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_1>]>;
  def SVMLS_LANE_VG1x2_F64 : Inst<"svmls_lane_za64[_{d}]_vg1x2", "vm2di", "d", MergeNone, "aarch64_sme_fmls_lane_vg1x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_1>]>;
  def SVMLS_LANE_VG1x4_F64 : Inst<"svmls_lane_za64[_{d}]_vg1x4", "vm4di", "d", MergeNone, "aarch64_sme_fmls_lane_vg1x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_1>]>;
}

let SMETargetGuard = "sme-f16f16" in {
  def SVMLA_MULTI_VG1x2_F16 : Inst<"svmla_za16[_f16]_vg1x2", "vm22", "h", MergeNone, "aarch64_sme_fmla_vg1x2", [IsStreaming, IsInOutZA], []>;
  def SVMLA_MULTI_VG1x4_F16 : Inst<"svmla_za16[_f16]_vg1x4", "vm44", "h", MergeNone, "aarch64_sme_fmla_vg1x4", [IsStreaming, IsInOutZA], []>;
  def SVMLS_MULTI_VG1x2_F16 : Inst<"svmls_za16[_f16]_vg1x2", "vm22", "h", MergeNone, "aarch64_sme_fmls_vg1x2", [IsStreaming, IsInOutZA], []>;
  def SVMLS_MULTI_VG1x4_F16 : Inst<"svmls_za16[_f16]_vg1x4", "vm44", "h", MergeNone, "aarch64_sme_fmls_vg1x4", [IsStreaming, IsInOutZA], []>;

  def SVMLA_SINGLE_VG1x2_F16 : Inst<"svmla[_single]_za16[_f16]_vg1x2", "vm2d", "h", MergeNone, "aarch64_sme_fmla_single_vg1x2", [IsStreaming, IsInOutZA], []>;
  def SVMLA_SINGLE_VG1x4_F16 : Inst<"svmla[_single]_za16[_f16]_vg1x4", "vm4d", "h", MergeNone, "aarch64_sme_fmla_single_vg1x4", [IsStreaming, IsInOutZA], []>;
  def SVMLS_SINGLE_VG1x2_F16 : Inst<"svmls[_single]_za16[_f16]_vg1x2", "vm2d", "h", MergeNone, "aarch64_sme_fmls_single_vg1x2", [IsStreaming, IsInOutZA], []>;
  def SVMLS_SINGLE_VG1x4_F16 : Inst<"svmls[_single]_za16[_f16]_vg1x4", "vm4d", "h", MergeNone, "aarch64_sme_fmls_single_vg1x4", [IsStreaming, IsInOutZA], []>;

  def SVMLA_LANE_VG1x2_F16 : Inst<"svmla_lane_za16[_f16]_vg1x2", "vm2di", "h", MergeNone, "aarch64_sme_fmla_lane_vg1x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
  def SVMLA_LANE_VG1x4_F16 : Inst<"svmla_lane_za16[_f16]_vg1x4", "vm4di", "h", MergeNone, "aarch64_sme_fmla_lane_vg1x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
  def SVMLS_LANE_VG1x2_F16 : Inst<"svmls_lane_za16[_f16]_vg1x2", "vm2di", "h", MergeNone, "aarch64_sme_fmls_lane_vg1x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
  def SVMLS_LANE_VG1x4_F16 : Inst<"svmls_lane_za16[_f16]_vg1x4", "vm4di", "h", MergeNone, "aarch64_sme_fmls_lane_vg1x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
}

let SMETargetGuard = "sme2,b16b16" in {
  def SVMLA_MULTI_VG1x2_BF16 : Inst<"svmla_za16[_bf16]_vg1x2", "vm22", "b", MergeNone, "aarch64_sme_fmla_vg1x2", [IsStreaming, IsInOutZA], []>;
  def SVMLA_MULTI_VG1x4_BF16 : Inst<"svmla_za16[_bf16]_vg1x4", "vm44", "b", MergeNone, "aarch64_sme_fmla_vg1x4", [IsStreaming, IsInOutZA], []>;
  def SVMLS_MULTI_VG1x2_BF16 : Inst<"svmls_za16[_bf16]_vg1x2", "vm22", "b", MergeNone, "aarch64_sme_fmls_vg1x2", [IsStreaming, IsInOutZA], []>;
  def SVMLS_MULTI_VG1x4_BF16 : Inst<"svmls_za16[_bf16]_vg1x4", "vm44", "b", MergeNone, "aarch64_sme_fmls_vg1x4", [IsStreaming, IsInOutZA], []>;

  def SVMLA_SINGLE_VG1x2_BF16 : Inst<"svmla[_single]_za16[_bf16]_vg1x2", "vm2d", "b", MergeNone, "aarch64_sme_fmla_single_vg1x2", [IsStreaming, IsInOutZA], []>;
  def SVMLA_SINGLE_VG1x4_BF16 : Inst<"svmla[_single]_za16[_bf16]_vg1x4", "vm4d", "b", MergeNone, "aarch64_sme_fmla_single_vg1x4", [IsStreaming, IsInOutZA], []>;
  def SVMLS_SINGLE_VG1x2_BF16 : Inst<"svmls[_single]_za16[_bf16]_vg1x2", "vm2d", "b", MergeNone, "aarch64_sme_fmls_single_vg1x2", [IsStreaming, IsInOutZA], []>;
  def SVMLS_SINGLE_VG1x4_BF16 : Inst<"svmls[_single]_za16[_bf16]_vg1x4", "vm4d", "b", MergeNone, "aarch64_sme_fmls_single_vg1x4", [IsStreaming, IsInOutZA], []>;

  def SVMLA_LANE_VG1x2_BF16 : Inst<"svmla_lane_za16[_bf16]_vg1x2", "vm2di", "b", MergeNone, "aarch64_sme_fmla_lane_vg1x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
  def SVMLA_LANE_VG1x4_BF16 : Inst<"svmla_lane_za16[_bf16]_vg1x4", "vm4di", "b", MergeNone, "aarch64_sme_fmla_lane_vg1x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
  def SVMLS_LANE_VG1x2_BF16 : Inst<"svmls_lane_za16[_bf16]_vg1x2", "vm2di", "b", MergeNone, "aarch64_sme_fmls_lane_vg1x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
  def SVMLS_LANE_VG1x4_BF16 : Inst<"svmls_lane_za16[_bf16]_vg1x4", "vm4di", "b", MergeNone, "aarch64_sme_fmls_lane_vg1x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
}

// FMLAL/FMLSL/UMLAL/SMLAL
// SMLALL/UMLALL/USMLALL/SUMLALL
let SMETargetGuard = "sme2" in {
  // MULTI MLAL
  def SVMLAL_MULTI_VG2x2_F16 : Inst<"svmla_za32[_{d}]_vg2x2", "vm22", "bh", MergeNone, "aarch64_sme_fmlal_vg2x2", [IsStreaming, IsInOutZA], []>;
  def SVMLAL_MULTI_VG2x4_F16 : Inst<"svmla_za32[_{d}]_vg2x4", "vm44", "bh", MergeNone, "aarch64_sme_fmlal_vg2x4", [IsStreaming, IsInOutZA], []>;
  def SVMLAL_MULTI_VG2x2_S16 : Inst<"svmla_za32[_{d}]_vg2x2", "vm22", "s", MergeNone, "aarch64_sme_smlal_vg2x2", [IsStreaming, IsInOutZA], []>;
  def SVMLAL_MULTI_VG2x4_S16 : Inst<"svmla_za32[_{d}]_vg2x4", "vm44", "s", MergeNone, "aarch64_sme_smlal_vg2x4", [IsStreaming, IsInOutZA], []>;
  def SVMLAL_MULTI_VG2x2_U16 : Inst<"svmla_za32[_{d}]_vg2x2", "vm22", "Us", MergeNone, "aarch64_sme_umlal_vg2x2", [IsStreaming, IsInOutZA], []>;
  def SVMLAL_MULTI_VG2x4_U16 : Inst<"svmla_za32[_{d}]_vg2x4", "vm44", "Us", MergeNone, "aarch64_sme_umlal_vg2x4", [IsStreaming, IsInOutZA], []>;

  def SVMLAL_MULTI_VG4x2_S8 : Inst<"svmla_za32[_{d}]_vg4x2", "vm22", "c", MergeNone, "aarch64_sme_smla_za32_vg4x2", [IsStreaming, IsInOutZA], []>;
  def SVMLAL_MULTI_VG4x2_U8 : Inst<"svmla_za32[_{d}]_vg4x2", "vm22", "Uc", MergeNone, "aarch64_sme_umla_za32_vg4x2", [IsStreaming, IsInOutZA], []>;
  def SVMLAL_MULTI_VG4x4_S8 : Inst<"svmla_za32[_{d}]_vg4x4", "vm44", "c", MergeNone, "aarch64_sme_smla_za32_vg4x4", [IsStreaming, IsInOutZA], []>;
  def SVMLAL_MULTI_VG4x4_U8 : Inst<"svmla_za32[_{d}]_vg4x4", "vm44", "Uc", MergeNone, "aarch64_sme_umla_za32_vg4x4", [IsStreaming, IsInOutZA], []>;

  // MULTI MLSL
  def SVMLSL_MULTI_VG2x2_F16 : Inst<"svmls_za32[_{d}]_vg2x2", "vm22", "bh", MergeNone, "aarch64_sme_fmlsl_vg2x2", [IsStreaming, IsInOutZA], []>;
  def SVMLSL_MULTI_VG2x4_F16 : Inst<"svmls_za32[_{d}]_vg2x4", "vm44", "bh", MergeNone, "aarch64_sme_fmlsl_vg2x4", [IsStreaming, IsInOutZA], []>;
  def SVMLSL_MULTI_VG2x2_S16 : Inst<"svmls_za32[_{d}]_vg2x2", "vm22", "s", MergeNone, "aarch64_sme_smlsl_vg2x2", [IsStreaming, IsInOutZA], []>;
  def SVMLSL_MULTI_VG2x4_S16 : Inst<"svmls_za32[_{d}]_vg2x4", "vm44", "s", MergeNone, "aarch64_sme_smlsl_vg2x4", [IsStreaming, IsInOutZA], []>;
  def SVMLSL_MULTI_VG2x2_U16 : Inst<"svmls_za32[_{d}]_vg2x2", "vm22", "Us", MergeNone, "aarch64_sme_umlsl_vg2x2", [IsStreaming, IsInOutZA], []>;
  def SVMLSL_MULTI_VG2x4_U16 : Inst<"svmls_za32[_{d}]_vg2x4", "vm44", "Us", MergeNone, "aarch64_sme_umlsl_vg2x4", [IsStreaming, IsInOutZA], []>;

  def SVMLSL_MULTI_VG4x2_S8 : Inst<"svmls_za32[_{d}]_vg4x2", "vm22", "c", MergeNone, "aarch64_sme_smls_za32_vg4x2", [IsStreaming, IsInOutZA], []>;
  def SVMLSL_MULTI_VG4x2_U8 : Inst<"svmls_za32[_{d}]_vg4x2", "vm22", "Uc", MergeNone, "aarch64_sme_umls_za32_vg4x2", [IsStreaming, IsInOutZA], []>;
  def SVMLSL_MULTI_VG4x4_S8 : Inst<"svmls_za32[_{d}]_vg4x4", "vm44", "c", MergeNone, "aarch64_sme_smls_za32_vg4x4", [IsStreaming, IsInOutZA], []>;
  def SVMLSL_MULTI_VG4x4_U8 : Inst<"svmls_za32[_{d}]_vg4x4", "vm44", "Uc", MergeNone, "aarch64_sme_umls_za32_vg4x4", [IsStreaming, IsInOutZA], []>;

  // SINGLE MLAL
  def SVMLAL_SINGLE_VG2x1_F16 : Inst<"svmla_za32[_{d}]_vg2x1",          "vmdd", "bh", MergeNone, "aarch64_sme_fmlal_single_vg2x1", [IsStreaming, IsInOutZA], []>;
  def SVMLAL_SINGLE_VG2x2_F16 : Inst<"svmla[_single]_za32[_{d}]_vg2x2", "vm2d", "bh", MergeNone, "aarch64_sme_fmlal_single_vg2x2", [IsStreaming, IsInOutZA], []>;
  def SVMLAL_SINGLE_VG2x4_F16 : Inst<"svmla[_single]_za32[_{d}]_vg2x4", "vm4d", "bh", MergeNone, "aarch64_sme_fmlal_single_vg2x4", [IsStreaming, IsInOutZA], []>;
  def SVMLAL_SINGLE_VG2x1_S16 : Inst<"svmla_za32[_{d}]_vg2x1",          "vmdd", "s", MergeNone, "aarch64_sme_smlal_single_vg2x1", [IsStreaming, IsInOutZA], []>;
  def SVMLAL_SINGLE_VG2x2_S16 : Inst<"svmla[_single]_za32[_{d}]_vg2x2", "vm2d", "s", MergeNone, "aarch64_sme_smlal_single_vg2x2", [IsStreaming, IsInOutZA], []>;
  def SVMLAL_SINGLE_VG2x4_S16 : Inst<"svmla[_single]_za32[_{d}]_vg2x4", "vm4d", "s", MergeNone, "aarch64_sme_smlal_single_vg2x4", [IsStreaming, IsInOutZA], []>;
  def SVMLAL_SINGLE_VG2x1_U16 : Inst<"svmla_za32[_{d}]_vg2x1",          "vmdd", "Us", MergeNone, "aarch64_sme_umlal_single_vg2x1", [IsStreaming, IsInOutZA], []>;
  def SVMLAL_SINGLE_VG2x2_U16 : Inst<"svmla[_single]_za32[_{d}]_vg2x2", "vm2d", "Us", MergeNone, "aarch64_sme_umlal_single_vg2x2", [IsStreaming, IsInOutZA], []>;
  def SVMLAL_SINGLE_VG2x4_U16 : Inst<"svmla[_single]_za32[_{d}]_vg2x4", "vm4d", "Us", MergeNone, "aarch64_sme_umlal_single_vg2x4", [IsStreaming, IsInOutZA], []>;

  def SVMLAL_SINGLE_VG4x1_S8  : Inst<"svmla_za32[_{d}]_vg4x1",          "vmdd", "c", MergeNone, "aarch64_sme_smla_za32_single_vg4x1", [IsStreaming, IsInOutZA], []>;
  def SVMLAL_SINGLE_VG4x1_U8  : Inst<"svmla_za32[_{d}]_vg4x1",          "vmdd", "Uc", MergeNone, "aarch64_sme_umla_za32_single_vg4x1", [IsStreaming, IsInOutZA], []>;
  def SVMLAL_SINGLE_VG4x2_S8  : Inst<"svmla[_single]_za32[_{d}]_vg4x2", "vm2d", "c", MergeNone, "aarch64_sme_smla_za32_single_vg4x2", [IsStreaming, IsInOutZA], []>;
  def SVMLAL_SINGLE_VG4x2_U8  : Inst<"svmla[_single]_za32[_{d}]_vg4x2", "vm2d", "Uc", MergeNone, "aarch64_sme_umla_za32_single_vg4x2", [IsStreaming, IsInOutZA], []>;
  def SVMLAL_SINGLE_VG4x4_S8  : Inst<"svmla[_single]_za32[_{d}]_vg4x4", "vm4d", "c", MergeNone, "aarch64_sme_smla_za32_single_vg4x4", [IsStreaming, IsInOutZA], []>;
  def SVMLAL_SINGLE_VG4x4_U8  : Inst<"svmla[_single]_za32[_{d}]_vg4x4", "vm4d", "Uc", MergeNone, "aarch64_sme_umla_za32_single_vg4x4", [IsStreaming, IsInOutZA], []>;

  // SINGLE MLSL
  def SVMLSL_SINGLE_VG2x1_F16 : Inst<"svmls_za32[_{d}]_vg2x1",          "vmdd", "bh", MergeNone, "aarch64_sme_fmlsl_single_vg2x1", [IsStreaming, IsInOutZA], []>;
  def SVMLSL_SINGLE_VG2x2_F16 : Inst<"svmls[_single]_za32[_{d}]_vg2x2", "vm2d", "bh", MergeNone, "aarch64_sme_fmlsl_single_vg2x2", [IsStreaming, IsInOutZA], []>;
  def SVMLSL_SINGLE_VG2x4_F16 : Inst<"svmls[_single]_za32[_{d}]_vg2x4", "vm4d", "bh", MergeNone, "aarch64_sme_fmlsl_single_vg2x4", [IsStreaming, IsInOutZA], []>;
  def SVMLSL_SINGLE_VG2x1_S16 : Inst<"svmls_za32[_{d}]_vg2x1",          "vmdd", "s", MergeNone, "aarch64_sme_smlsl_single_vg2x1", [IsStreaming, IsInOutZA], []>;
  def SVMLSL_SINGLE_VG2x2_S16 : Inst<"svmls[_single]_za32[_{d}]_vg2x2", "vm2d", "s", MergeNone, "aarch64_sme_smlsl_single_vg2x2", [IsStreaming, IsInOutZA], []>;
  def SVMLSL_SINGLE_VG2x4_S16 : Inst<"svmls[_single]_za32[_{d}]_vg2x4", "vm4d", "s", MergeNone, "aarch64_sme_smlsl_single_vg2x4", [IsStreaming, IsInOutZA], []>;
  def SVMLSL_SINGLE_VG2x1_U16 : Inst<"svmls_za32[_{d}]_vg2x1",          "vmdd", "Us", MergeNone, "aarch64_sme_umlsl_single_vg2x1", [IsStreaming, IsInOutZA], []>;
  def SVMLSL_SINGLE_VG2x2_U16 : Inst<"svmls[_single]_za32[_{d}]_vg2x2", "vm2d", "Us", MergeNone, "aarch64_sme_umlsl_single_vg2x2", [IsStreaming, IsInOutZA], []>;
  def SVMLSL_SINGLE_VG2x4_U16 : Inst<"svmls[_single]_za32[_{d}]_vg2x4", "vm4d", "Us", MergeNone, "aarch64_sme_umlsl_single_vg2x4", [IsStreaming, IsInOutZA], []>;

  def SVMLSL_SINGLE_VG4x1_S8  : Inst<"svmls_za32[_{d}]_vg4x1",          "vmdd", "c", MergeNone, "aarch64_sme_smls_za32_single_vg4x1", [IsStreaming, IsInOutZA], []>;
  def SVMLSL_SINGLE_VG4x1_U8  : Inst<"svmls_za32[_{d}]_vg4x1",          "vmdd", "Uc", MergeNone, "aarch64_sme_umls_za32_single_vg4x1", [IsStreaming, IsInOutZA], []>;
  def SVMLSL_SINGLE_VG4x2_S8  : Inst<"svmls[_single]_za32[_{d}]_vg4x2", "vm2d", "c", MergeNone, "aarch64_sme_smls_za32_single_vg4x2", [IsStreaming, IsInOutZA], []>;
  def SVMLSL_SINGLE_VG4x2_U8  : Inst<"svmls[_single]_za32[_{d}]_vg4x2", "vm2d", "Uc", MergeNone, "aarch64_sme_umls_za32_single_vg4x2", [IsStreaming, IsInOutZA], []>;
  def SVMLSL_SINGLE_VG4x4_S8  : Inst<"svmls[_single]_za32[_{d}]_vg4x4", "vm4d", "c", MergeNone, "aarch64_sme_smls_za32_single_vg4x4", [IsStreaming, IsInOutZA], []>;
  def SVMLSL_SINGLE_VG4x4_U8  : Inst<"svmls[_single]_za32[_{d}]_vg4x4", "vm4d", "Uc", MergeNone, "aarch64_sme_umls_za32_single_vg4x4", [IsStreaming, IsInOutZA], []>;

  // INDEXED MLAL
  def SVMLAL_LANE_VG2x1_F16 : Inst<"svmla_lane_za32[_{d}]_vg2x1", "vmddi", "bh", MergeNone, "aarch64_sme_fmlal_lane_vg2x1", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
  def SVMLAL_LANE_VG2x2_F16 : Inst<"svmla_lane_za32[_{d}]_vg2x2", "vm2di", "bh", MergeNone, "aarch64_sme_fmlal_lane_vg2x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
  def SVMLAL_LANE_VG2x4_F16 : Inst<"svmla_lane_za32[_{d}]_vg2x4", "vm4di", "bh", MergeNone, "aarch64_sme_fmlal_lane_vg2x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
  def SVMLAL_LANE_VG2x1_S16 : Inst<"svmla_lane_za32[_{d}]_vg2x1", "vmddi", "s", MergeNone, "aarch64_sme_smlal_lane_vg2x1", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
  def SVMLAL_LANE_VG2x2_S16 : Inst<"svmla_lane_za32[_{d}]_vg2x2", "vm2di", "s", MergeNone, "aarch64_sme_smlal_lane_vg2x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
  def SVMLAL_LANE_VG2x4_S16 : Inst<"svmla_lane_za32[_{d}]_vg2x4", "vm4di", "s", MergeNone, "aarch64_sme_smlal_lane_vg2x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
  def SVMLAL_LANE_VG2x1_U16 : Inst<"svmla_lane_za32[_{d}]_vg2x1", "vmddi", "Us", MergeNone, "aarch64_sme_umlal_lane_vg2x1", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
  def SVMLAL_LANE_VG2x2_U16 : Inst<"svmla_lane_za32[_{d}]_vg2x2", "vm2di", "Us", MergeNone, "aarch64_sme_umlal_lane_vg2x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
  def SVMLAL_LANE_VG2x4_U16 : Inst<"svmla_lane_za32[_{d}]_vg2x4", "vm4di", "Us", MergeNone, "aarch64_sme_umlal_lane_vg2x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;

  def SVMLAL_LANE_VG4x1_S8 : Inst<"svmla_lane_za32[_{d}]_vg4x1", "vmddi", "c", MergeNone, "aarch64_sme_smla_za32_lane_vg4x1", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_15>]>;
  def SVMLAL_LANE_VG4x1_U8 : Inst<"svmla_lane_za32[_{d}]_vg4x1", "vmddi", "Uc", MergeNone, "aarch64_sme_umla_za32_lane_vg4x1", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_15>]>;
  def SVMLAL_LANE_VG4x2_S8 : Inst<"svmla_lane_za32[_{d}]_vg4x2", "vm2di", "c", MergeNone, "aarch64_sme_smla_za32_lane_vg4x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_15>]>;
  def SVMLAL_LANE_VG4x2_U8 : Inst<"svmla_lane_za32[_{d}]_vg4x2", "vm2di", "Uc", MergeNone, "aarch64_sme_umla_za32_lane_vg4x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_15>]>;
  def SVMLAL_LANE_VG4x4_S8 : Inst<"svmla_lane_za32[_{d}]_vg4x4", "vm4di", "c", MergeNone, "aarch64_sme_smla_za32_lane_vg4x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_15>]>;
  def SVMLAL_LANE_VG4x4_U8 : Inst<"svmla_lane_za32[_{d}]_vg4x4", "vm4di", "Uc", MergeNone, "aarch64_sme_umla_za32_lane_vg4x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_15>]>;

  // INDEXED MLSL
  def SVMLSL_LANE_VG2x1_F16 : Inst<"svmls_lane_za32[_{d}]_vg2x1", "vmddi", "bh", MergeNone, "aarch64_sme_fmlsl_lane_vg2x1", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
  def SVMLSL_LANE_VG2x2_F16 : Inst<"svmls_lane_za32[_{d}]_vg2x2", "vm2di", "bh", MergeNone, "aarch64_sme_fmlsl_lane_vg2x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
  def SVMLSL_LANE_VG2x4_F16 : Inst<"svmls_lane_za32[_{d}]_vg2x4", "vm4di", "bh", MergeNone, "aarch64_sme_fmlsl_lane_vg2x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
  def SVMLSL_LANE_VG2x1_S16 : Inst<"svmls_lane_za32[_{d}]_vg2x1", "vmddi", "s", MergeNone, "aarch64_sme_smlsl_lane_vg2x1", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
  def SVMLSL_LANE_VG2x2_S16 : Inst<"svmls_lane_za32[_{d}]_vg2x2", "vm2di", "s", MergeNone, "aarch64_sme_smlsl_lane_vg2x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
  def SVMLSL_LANE_VG2x4_S16 : Inst<"svmls_lane_za32[_{d}]_vg2x4", "vm4di", "s", MergeNone, "aarch64_sme_smlsl_lane_vg2x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
  def SVMLSL_LANE_VG2x1_U16 : Inst<"svmls_lane_za32[_{d}]_vg2x1", "vmddi", "Us", MergeNone, "aarch64_sme_umlsl_lane_vg2x1", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
  def SVMLSL_LANE_VG2x2_U16 : Inst<"svmls_lane_za32[_{d}]_vg2x2", "vm2di", "Us", MergeNone, "aarch64_sme_umlsl_lane_vg2x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
  def SVMLSL_LANE_VG2x4_U16 : Inst<"svmls_lane_za32[_{d}]_vg2x4", "vm4di", "Us", MergeNone, "aarch64_sme_umlsl_lane_vg2x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;

  def SVMLSL_LANE_VG4x1_S8 : Inst<"svmls_lane_za32[_{d}]_vg4x1", "vmddi", "c", MergeNone, "aarch64_sme_smls_za32_lane_vg4x1", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_15>]>;
  def SVMLSL_LANE_VG4x1_U8 : Inst<"svmls_lane_za32[_{d}]_vg4x1", "vmddi", "Uc", MergeNone, "aarch64_sme_umls_za32_lane_vg4x1", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_15>]>;
  def SVMLSL_LANE_VG4x2_S8 : Inst<"svmls_lane_za32[_{d}]_vg4x2", "vm2di", "c", MergeNone, "aarch64_sme_smls_za32_lane_vg4x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_15>]>;
  def SVMLSL_LANE_VG4x2_U8 : Inst<"svmls_lane_za32[_{d}]_vg4x2", "vm2di", "Uc", MergeNone, "aarch64_sme_umls_za32_lane_vg4x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_15>]>;
  def SVMLSL_LANE_VG4x4_S8 : Inst<"svmls_lane_za32[_{d}]_vg4x4", "vm4di", "c", MergeNone, "aarch64_sme_smls_za32_lane_vg4x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_15>]>;
  def SVMLSL_LANE_VG4x4_U8 : Inst<"svmls_lane_za32[_{d}]_vg4x4", "vm4di", "Uc", MergeNone, "aarch64_sme_umls_za32_lane_vg4x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_15>]>;

  // SINGLE SUMLALL
  // Single sumla maps to usmla, with zn & zm operands swapped
  def SVSUMLALL_SINGLE_VG4x1 : Inst<"svsumla_za32[_{d}]_vg4x1",          "vmdu",   "c", MergeNone, "aarch64_sme_usmla_za32_single_vg4x1", [IsStreaming, IsInOutZA], []>;

  def SVSUMLALL_SINGLE_VG4x2 : Inst<"svsumla[_single]_za32[_{d}]_vg4x2", "vm2.du", "c", MergeNone, "aarch64_sme_sumla_za32_single_vg4x2", [IsStreaming, IsInOutZA], []>;
  def SVSUMLALL_SINGLE_VG4x4 : Inst<"svsumla[_single]_za32[_{d}]_vg4x4", "vm4.du", "c", MergeNone, "aarch64_sme_sumla_za32_single_vg4x4", [IsStreaming, IsInOutZA], []>;

  // Multi-multi sumla builtins are mapped to usmla, with zn & zm operands swapped
  def SVSUMLALL_MULTI_VG4x2 : Inst<"svsumla_za32[_{d}]_vg4x2", "vm2.d2.u", "c", MergeNone, "aarch64_sme_usmla_za32_vg4x2", [IsStreaming, IsInOutZA], []>;
  def SVSUMLALL_MULTI_VG4x4 : Inst<"svsumla_za32[_{d}]_vg4x4", "vm4.d4.u", "c", MergeNone, "aarch64_sme_usmla_za32_vg4x4", [IsStreaming, IsInOutZA], []>;

  // INDEXED SUMLALL
  def SVSUMLALL_LANE_VG4x1 : Inst<"svsumla_lane_za32[_{d}]_vg4x1", "vmdui", "c", MergeNone, "aarch64_sme_sumla_za32_lane_vg4x1", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_15>]>;
  def SVSUMLALL_LANE_VG4x2 : Inst<"svsumla_lane_za32[_{d}]_vg4x2", "vm2ui", "c", MergeNone, "aarch64_sme_sumla_za32_lane_vg4x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_15>]>;
  def SVSUMLALL_LANE_VG4x4 : Inst<"svsumla_lane_za32[_{d}]_vg4x4", "vm4ui", "c", MergeNone, "aarch64_sme_sumla_za32_lane_vg4x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_15>]>;

  // SINGLE USMLALL
  def SVUSMLALL_SINGLE_VG4x1 : Inst<"svusmla_za32[_{d}]_vg4x1",          "vmdx",   "Uc", MergeNone, "aarch64_sme_usmla_za32_single_vg4x1", [IsStreaming, IsInOutZA], []>;
  def SVUSMLALL_SINGLE_VG4x2 : Inst<"svusmla[_single]_za32[_{d}]_vg4x2", "vm2.dx", "Uc", MergeNone, "aarch64_sme_usmla_za32_single_vg4x2", [IsStreaming, IsInOutZA], []>;
  def SVUSMLALL_SINGLE_VG4x4 : Inst<"svusmla[_single]_za32[_{d}]_vg4x4", "vm4.dx", "Uc", MergeNone, "aarch64_sme_usmla_za32_single_vg4x4", [IsStreaming, IsInOutZA], []>;

  // MULTI USMLALL
  def SVUSMLALL_MULTI_VG4x2 : Inst<"svusmla_za32[_{d}]_vg4x2", "vm2.d2.x", "Uc", MergeNone, "aarch64_sme_usmla_za32_vg4x2", [IsStreaming, IsInOutZA], []>;
  def SVUSMLALL_MULTI_VG4x4 : Inst<"svusmla_za32[_{d}]_vg4x4", "vm4.d4.x", "Uc", MergeNone, "aarch64_sme_usmla_za32_vg4x4", [IsStreaming, IsInOutZA], []>;

  // INDEXED USMLALL
  def SVUSMLALL_LANE_VG4x1 : Inst<"svusmla_lane_za32[_{d}]_vg4x1", "vmdxi", "Uc", MergeNone, "aarch64_sme_usmla_za32_lane_vg4x1", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_15>]>;
  def SVUSMLALL_LANE_VG4x2 : Inst<"svusmla_lane_za32[_{d}]_vg4x2", "vm2xi", "Uc", MergeNone, "aarch64_sme_usmla_za32_lane_vg4x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_15>]>;
  def SVUSMLALL_LANE_VG4x4 : Inst<"svusmla_lane_za32[_{d}]_vg4x4", "vm4xi", "Uc", MergeNone, "aarch64_sme_usmla_za32_lane_vg4x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_15>]>;
}

let SMETargetGuard = "sme2,sme-i16i64" in {
  // MULTI MLAL
  def SVMLAL_MULTI_VG4x2_S16 : Inst<"svmla_za64[_{d}]_vg4x2", "vm22", "s", MergeNone, "aarch64_sme_smla_za64_vg4x2", [IsStreaming, IsInOutZA], []>;
  def SVMLAL_MULTI_VG4x2_U16 : Inst<"svmla_za64[_{d}]_vg4x2", "vm22", "Us", MergeNone, "aarch64_sme_umla_za64_vg4x2", [IsStreaming, IsInOutZA], []>;
  def SVMLAL_MULTI_VG4x4_S16 : Inst<"svmla_za64[_{d}]_vg4x4", "vm44", "s", MergeNone, "aarch64_sme_smla_za64_vg4x4", [IsStreaming, IsInOutZA], []>;
  def SVMLAL_MULTI_VG4x4_U16 : Inst<"svmla_za64[_{d}]_vg4x4", "vm44", "Us", MergeNone, "aarch64_sme_umla_za64_vg4x4", [IsStreaming, IsInOutZA], []>;

  // MULTI MLSL
  def SVMLSL_MULTI_VG4x2_S16 : Inst<"svmls_za64[_{d}]_vg4x2", "vm22", "s", MergeNone, "aarch64_sme_smls_za64_vg4x2", [IsStreaming, IsInOutZA], []>;
  def SVMLSL_MULTI_VG4x2_U16 : Inst<"svmls_za64[_{d}]_vg4x2", "vm22", "Us", MergeNone, "aarch64_sme_umls_za64_vg4x2", [IsStreaming, IsInOutZA], []>;
  def SVMLSL_MULTI_VG4x4_S16 : Inst<"svmls_za64[_{d}]_vg4x4", "vm44", "s", MergeNone, "aarch64_sme_smls_za64_vg4x4", [IsStreaming, IsInOutZA], []>;
  def SVMLSL_MULTI_VG4x4_U16 : Inst<"svmls_za64[_{d}]_vg4x4", "vm44", "Us", MergeNone, "aarch64_sme_umls_za64_vg4x4", [IsStreaming, IsInOutZA], []>;

  // SINGLE MLAL
  def SVMLAL_SINGLE_VG4x1_S16 : Inst<"svmla_za64[_{d}]_vg4x1",          "vmdd", "s", MergeNone, "aarch64_sme_smla_za64_single_vg4x1", [IsStreaming, IsInOutZA], []>;
  def SVMLAL_SINGLE_VG4x1_U16 : Inst<"svmla_za64[_{d}]_vg4x1",          "vmdd", "Us", MergeNone, "aarch64_sme_umla_za64_single_vg4x1", [IsStreaming, IsInOutZA], []>;
  def SVMLAL_SINGLE_VG4x2_S16 : Inst<"svmla[_single]_za64[_{d}]_vg4x2", "vm2d", "s", MergeNone, "aarch64_sme_smla_za64_single_vg4x2", [IsStreaming, IsInOutZA], []>;
  def SVMLAL_SINGLE_VG4x2_U16 : Inst<"svmla[_single]_za64[_{d}]_vg4x2", "vm2d", "Us", MergeNone, "aarch64_sme_umla_za64_single_vg4x2", [IsStreaming, IsInOutZA], []>;
  def SVMLAL_SINGLE_VG4x4_S16 : Inst<"svmla[_single]_za64[_{d}]_vg4x4", "vm4d", "s", MergeNone, "aarch64_sme_smla_za64_single_vg4x4", [IsStreaming, IsInOutZA], []>;
  def SVMLAL_SINGLE_VG4x4_U16 : Inst<"svmla[_single]_za64[_{d}]_vg4x4", "vm4d", "Us", MergeNone, "aarch64_sme_umla_za64_single_vg4x4", [IsStreaming, IsInOutZA], []>;

  // SINGLE MLSL
  def SVMLSL_SINGLE_VG4x1_S16 : Inst<"svmls_za64[_{d}]_vg4x1",          "vmdd", "s", MergeNone, "aarch64_sme_smls_za64_single_vg4x1", [IsStreaming, IsInOutZA], []>;
  def SVMLSL_SINGLE_VG4x1_U16 : Inst<"svmls_za64[_{d}]_vg4x1",          "vmdd", "Us", MergeNone, "aarch64_sme_umls_za64_single_vg4x1", [IsStreaming, IsInOutZA], []>;
  def SVMLSL_SINGLE_VG4x2_S16 : Inst<"svmls[_single]_za64[_{d}]_vg4x2", "vm2d", "s", MergeNone, "aarch64_sme_smls_za64_single_vg4x2", [IsStreaming, IsInOutZA], []>;
  def SVMLSL_SINGLE_VG4x2_U16 : Inst<"svmls[_single]_za64[_{d}]_vg4x2", "vm2d", "Us", MergeNone, "aarch64_sme_umls_za64_single_vg4x2", [IsStreaming, IsInOutZA], []>;
  def SVMLSL_SINGLE_VG4x4_S16 : Inst<"svmls[_single]_za64[_{d}]_vg4x4", "vm4d", "s", MergeNone, "aarch64_sme_smls_za64_single_vg4x4", [IsStreaming, IsInOutZA], []>;
  def SVMLSL_SINGLE_VG4x4_U16 : Inst<"svmls[_single]_za64[_{d}]_vg4x4", "vm4d", "Us", MergeNone, "aarch64_sme_umls_za64_single_vg4x4", [IsStreaming, IsInOutZA], []>;

  // INDEXED MLAL
  def SVMLAL_LANE_VG4x1_S16 : Inst<"svmla_lane_za64[_{d}]_vg4x1", "vmddi", "s", MergeNone, "aarch64_sme_smla_za64_lane_vg4x1", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
  def SVMLAL_LANE_VG4x1_U16 : Inst<"svmla_lane_za64[_{d}]_vg4x1", "vmddi", "Us", MergeNone, "aarch64_sme_umla_za64_lane_vg4x1", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
  def SVMLAL_LANE_VG4x2_S16 : Inst<"svmla_lane_za64[_{d}]_vg4x2", "vm2di", "s", MergeNone, "aarch64_sme_smla_za64_lane_vg4x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
  def SVMLAL_LANE_VG4x2_U16 : Inst<"svmla_lane_za64[_{d}]_vg4x2", "vm2di", "Us", MergeNone, "aarch64_sme_umla_za64_lane_vg4x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
  def SVMLAL_LANE_VG4x4_S16 : Inst<"svmla_lane_za64[_{d}]_vg4x4", "vm4di", "s", MergeNone, "aarch64_sme_smla_za64_lane_vg4x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
  def SVMLAL_LANE_VG4x4_U16 : Inst<"svmla_lane_za64[_{d}]_vg4x4", "vm4di", "Us", MergeNone, "aarch64_sme_umla_za64_lane_vg4x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;

  // INDEXED MLSL
  def SVMLSL_LANE_VG4x1_S16 : Inst<"svmls_lane_za64[_{d}]_vg4x1", "vmddi", "s", MergeNone, "aarch64_sme_smls_za64_lane_vg4x1", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
  def SVMLSL_LANE_VG4x1_U16 : Inst<"svmls_lane_za64[_{d}]_vg4x1", "vmddi", "Us", MergeNone, "aarch64_sme_umls_za64_lane_vg4x1", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
  def SVMLSL_LANE_VG4x2_S16 : Inst<"svmls_lane_za64[_{d}]_vg4x2", "vm2di", "s", MergeNone, "aarch64_sme_smls_za64_lane_vg4x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
  def SVMLSL_LANE_VG4x2_U16 : Inst<"svmls_lane_za64[_{d}]_vg4x2", "vm2di", "Us", MergeNone, "aarch64_sme_umls_za64_lane_vg4x2", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
  def SVMLSL_LANE_VG4x4_S16 : Inst<"svmls_lane_za64[_{d}]_vg4x4", "vm4di", "s", MergeNone, "aarch64_sme_smls_za64_lane_vg4x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
  def SVMLSL_LANE_VG4x4_U16 : Inst<"svmls_lane_za64[_{d}]_vg4x4", "vm4di", "Us", MergeNone, "aarch64_sme_umls_za64_lane_vg4x4", [IsStreaming, IsInOutZA], [ImmCheck<3, ImmCheck0_7>]>;
}

//
// Spill and fill of ZT0
//
let SMETargetGuard = "sme2" in {
  def SVLDR_ZT : Inst<"svldr_zt", "viQ", "", MergeNone, "aarch64_sme_ldr_zt", [IsOverloadNone, IsStreamingCompatible, IsInOutZT0], [ImmCheck<0, ImmCheck0_0>]>;
  def SVSTR_ZT : Inst<"svstr_zt", "vi%", "", MergeNone, "aarch64_sme_str_zt", [IsOverloadNone, IsStreamingCompatible, IsInZT0], [ImmCheck<0, ImmCheck0_0>]>;
}

//
// Zero ZT0
//
let SMETargetGuard = "sme2" in {
  def SVZERO_ZT : Inst<"svzero_zt", "vi", "", MergeNone, "aarch64_sme_zero_zt", [IsOverloadNone, IsStreamingCompatible, IsOutZT0], [ImmCheck<0, ImmCheck0_0>]>;
}

//
// lookup table expand four contiguous registers
//
let SMETargetGuard = "sme2" in {
  def SVLUTI2_LANE_ZT_X4 : Inst<"svluti2_lane_zt_{d}_x4", "4.di[i", "cUcsUsiUibhf", MergeNone, "aarch64_sme_luti2_lane_zt_x4", [IsStreaming, IsInZT0], [ImmCheck<0, ImmCheck0_0>, ImmCheck<2, ImmCheck0_3>]>;
  def SVLUTI4_LANE_ZT_X4 : Inst<"svluti4_lane_zt_{d}_x4", "4.di[i", "sUsiUibhf", MergeNone, "aarch64_sme_luti4_lane_zt_x4", [IsStreaming, IsInZT0], [ImmCheck<0, ImmCheck0_0>, ImmCheck<2, ImmCheck0_1>]>;
}

//
// lookup table expand one register
//
let SMETargetGuard = "sme2" in {
  def SVLUTI2_LANE_ZT : Inst<"svluti2_lane_zt_{d}", "di[i", "cUcsUsiUibhf", MergeNone, "aarch64_sme_luti2_lane_zt", [IsStreaming, IsInZT0], [ImmCheck<0, ImmCheck0_0>, ImmCheck<2, ImmCheck0_15>]>;
  def SVLUTI4_LANE_ZT : Inst<"svluti4_lane_zt_{d}", "di[i", "cUcsUsiUibhf", MergeNone, "aarch64_sme_luti4_lane_zt", [IsStreaming, IsInZT0], [ImmCheck<0, ImmCheck0_0>, ImmCheck<2, ImmCheck0_7>]>;
}

//
// lookup table expand two contiguous registers
//
let SMETargetGuard = "sme2" in {
  def SVLUTI2_LANE_ZT_X2 : Inst<"svluti2_lane_zt_{d}_x2", "2.di[i", "cUcsUsiUibhf", MergeNone, "aarch64_sme_luti2_lane_zt_x2", [IsStreaming, IsInZT0], [ImmCheck<0, ImmCheck0_0>, ImmCheck<2, ImmCheck0_7>]>;
  def SVLUTI4_LANE_ZT_X2 : Inst<"svluti4_lane_zt_{d}_x2", "2.di[i", "cUcsUsiUibhf", MergeNone, "aarch64_sme_luti4_lane_zt_x2", [IsStreaming, IsInZT0], [ImmCheck<0, ImmCheck0_0>, ImmCheck<2, ImmCheck0_3>]>;
}

////////////////////////////////////////////////////////////////////////////////
// SME2p1 - FMOPA, FMOPS (non-widening)
let SMETargetGuard = "sme2,b16b16" in {
  def SVMOPA_BF16_NW : SInst<"svmopa_za16[_bf16]_m", "viPPdd", "b",
                             MergeNone, "aarch64_sme_mopa",
                             [IsStreaming, IsInOutZA],
                             [ImmCheck<0, ImmCheck0_1>]>;
  def SVMOPS_BF16_NW : SInst<"svmops_za16[_bf16]_m", "viPPdd", "b",
                             MergeNone, "aarch64_sme_mops",
                             [IsStreaming, IsInOutZA],
                             [ImmCheck<0, ImmCheck0_1>]>;
}

let SMETargetGuard = "sme-f16f16" in {
  def SVMOPA_F16_NW : SInst<"svmopa_za16[_f16]_m", "viPPdd", "h",
                            MergeNone, "aarch64_sme_mopa",
                            [IsStreaming, IsInOutZA],
                            [ImmCheck<0, ImmCheck0_1>]>;
  def SVMOPS_F16_NW : SInst<"svmops_za16[_f16]_m", "viPPdd", "h",
                            MergeNone, "aarch64_sme_mops",
                            [IsStreaming, IsInOutZA],
                            [ImmCheck<0, ImmCheck0_1>]>;
}


multiclass ZAReadz<string n_suffix, string vg_num, string t, string i_prefix, list<ImmCheck> ch> {
  let SMETargetGuard = "sme2p1" in {
    def NAME # _H : SInst<"svreadz_hor_" # n_suffix # "_{d}_vg" # vg_num, vg_num # "im", t,
                          MergeNone, i_prefix # "_horiz_x" # vg_num,
                          [IsStreaming, IsInOutZA], ch>;

    def NAME # _V : SInst<"svreadz_ver_" # n_suffix # "_{d}_vg" # vg_num, vg_num # "im", t,
                          MergeNone, i_prefix # "_vert_x" #vg_num,
                          [IsStreaming, IsInOutZA], ch>;
  }
}

defm SVREADZ_ZA8_X2  : ZAReadz<"za8",  "2", "cUc",   "aarch64_sme_readz", [ImmCheck<0, ImmCheck0_0>]>;
defm SVREADZ_ZA16_X2 : ZAReadz<"za16", "2", "sUshb", "aarch64_sme_readz", [ImmCheck<0, ImmCheck0_1>]>;
defm SVREADZ_ZA32_X2 : ZAReadz<"za32", "2", "iUif",  "aarch64_sme_readz", [ImmCheck<0, ImmCheck0_3>]>;
defm SVREADZ_ZA64_X2 : ZAReadz<"za64", "2", "lUld",  "aarch64_sme_readz", [ImmCheck<0, ImmCheck0_7>]>;

defm SVREADZ_ZA8_X4  : ZAReadz<"za8",  "4", "cUc",   "aarch64_sme_readz", [ImmCheck<0, ImmCheck0_0>]>;
defm SVREADZ_ZA16_X4 : ZAReadz<"za16", "4", "sUshb", "aarch64_sme_readz", [ImmCheck<0, ImmCheck0_1>]>;
defm SVREADZ_ZA32_X4 : ZAReadz<"za32", "4", "iUif",  "aarch64_sme_readz", [ImmCheck<0, ImmCheck0_3>]>;
defm SVREADZ_ZA64_X4 : ZAReadz<"za64", "4", "lUld",  "aarch64_sme_readz", [ImmCheck<0, ImmCheck0_7>]>;


multiclass ZAReadzSingle<string n_suffix, string t, string i_prefix, list<ImmCheck> ch> {
  let SMETargetGuard = "sme2p1" in {
    def NAME # _H : SInst<"svreadz_hor_" # n_suffix # "_{d}", "dim", t,
                          MergeNone, i_prefix # "_horiz",
                          [IsStreaming, IsInOutZA], ch>;

    def NAME # _V : SInst<"svreadz_ver_" # n_suffix # "_{d}", "dim", t,
                          MergeNone, i_prefix # "_vert",
                          [IsStreaming, IsInOutZA], ch>;
  }
}

defm SVREADZ_ZA8   : ZAReadzSingle<"za8", "cUc", "aarch64_sme_readz", [ImmCheck<0, ImmCheck0_0>]>;
defm SVREADZ_ZA16  : ZAReadzSingle<"za16", "sUshb", "aarch64_sme_readz", [ImmCheck<0, ImmCheck0_1>]>;
defm SVREADZ_ZA32  : ZAReadzSingle<"za32", "iUif", "aarch64_sme_readz", [ImmCheck<0, ImmCheck0_3>]>;
defm SVREADZ_ZA64  : ZAReadzSingle<"za64", "lUld", "aarch64_sme_readz", [ImmCheck<0, ImmCheck0_7>]>;
defm SVREADZ_ZA128 : ZAReadzSingle<"za128", "csilUcUiUsUlbhfd", "aarch64_sme_readz_q", [ImmCheck<0, ImmCheck0_15>]>;

multiclass ZAReadzArray<string vg_num>{
  let SMETargetGuard = "sme2p1" in {
    def NAME # _B : SInst<"svreadz_za8_{d}_vg1x" # vg_num, vg_num # "m", "cUc", MergeNone, "aarch64_sme_readz_x" # vg_num, [IsStreaming, IsInOutZA]>;
    def NAME # _H : SInst<"svreadz_za16_{d}_vg1x" # vg_num, vg_num # "m", "sUsbh", MergeNone, "aarch64_sme_readz_x" # vg_num, [IsStreaming, IsInOutZA]>;
    def NAME # _S : SInst<"svreadz_za32_{d}_vg1x" # vg_num, vg_num # "m", "iUif", MergeNone, "aarch64_sme_readz_x" # vg_num, [IsStreaming, IsInOutZA]>;
    def NAME # _D : SInst<"svreadz_za64_{d}_vg1x" # vg_num, vg_num # "m", "lUld", MergeNone, "aarch64_sme_readz_x" # vg_num, [IsStreaming, IsInOutZA]>;
  }
}

defm SVREADZ_VG2 :  ZAReadzArray<"2">;
defm SVREADZ_VG4 :  ZAReadzArray<"4">;
} // let SVETargetGuard = InvalidMode
