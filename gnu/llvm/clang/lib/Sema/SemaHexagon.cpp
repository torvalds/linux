//===------ SemaHexagon.cpp ------ Hexagon target-specific routines -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis functions specific to Hexagon.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaHexagon.h"
#include "clang/Basic/TargetBuiltins.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/STLExtras.h"
#include <cstdint>
#include <iterator>

namespace clang {

SemaHexagon::SemaHexagon(Sema &S) : SemaBase(S) {}

bool SemaHexagon::CheckHexagonBuiltinArgument(unsigned BuiltinID,
                                              CallExpr *TheCall) {
  struct ArgInfo {
    uint8_t OpNum;
    bool IsSigned;
    uint8_t BitWidth;
    uint8_t Align;
  };
  struct BuiltinInfo {
    unsigned BuiltinID;
    ArgInfo Infos[2];
  };

  static BuiltinInfo Infos[] = {
    { Hexagon::BI__builtin_circ_ldd,                  {{ 3, true,  4,  3 }} },
    { Hexagon::BI__builtin_circ_ldw,                  {{ 3, true,  4,  2 }} },
    { Hexagon::BI__builtin_circ_ldh,                  {{ 3, true,  4,  1 }} },
    { Hexagon::BI__builtin_circ_lduh,                 {{ 3, true,  4,  1 }} },
    { Hexagon::BI__builtin_circ_ldb,                  {{ 3, true,  4,  0 }} },
    { Hexagon::BI__builtin_circ_ldub,                 {{ 3, true,  4,  0 }} },
    { Hexagon::BI__builtin_circ_std,                  {{ 3, true,  4,  3 }} },
    { Hexagon::BI__builtin_circ_stw,                  {{ 3, true,  4,  2 }} },
    { Hexagon::BI__builtin_circ_sth,                  {{ 3, true,  4,  1 }} },
    { Hexagon::BI__builtin_circ_sthhi,                {{ 3, true,  4,  1 }} },
    { Hexagon::BI__builtin_circ_stb,                  {{ 3, true,  4,  0 }} },

    { Hexagon::BI__builtin_HEXAGON_L2_loadrub_pci,    {{ 1, true,  4,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_L2_loadrb_pci,     {{ 1, true,  4,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_L2_loadruh_pci,    {{ 1, true,  4,  1 }} },
    { Hexagon::BI__builtin_HEXAGON_L2_loadrh_pci,     {{ 1, true,  4,  1 }} },
    { Hexagon::BI__builtin_HEXAGON_L2_loadri_pci,     {{ 1, true,  4,  2 }} },
    { Hexagon::BI__builtin_HEXAGON_L2_loadrd_pci,     {{ 1, true,  4,  3 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_storerb_pci,    {{ 1, true,  4,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_storerh_pci,    {{ 1, true,  4,  1 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_storerf_pci,    {{ 1, true,  4,  1 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_storeri_pci,    {{ 1, true,  4,  2 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_storerd_pci,    {{ 1, true,  4,  3 }} },

    { Hexagon::BI__builtin_HEXAGON_A2_combineii,      {{ 1, true,  8,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_A2_tfrih,          {{ 1, false, 16, 0 }} },
    { Hexagon::BI__builtin_HEXAGON_A2_tfril,          {{ 1, false, 16, 0 }} },
    { Hexagon::BI__builtin_HEXAGON_A2_tfrpi,          {{ 0, true,  8,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_A4_bitspliti,      {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_A4_cmpbeqi,        {{ 1, false, 8,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_A4_cmpbgti,        {{ 1, true,  8,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_A4_cround_ri,      {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_A4_round_ri,       {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_A4_round_ri_sat,   {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_A4_vcmpbeqi,       {{ 1, false, 8,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_A4_vcmpbgti,       {{ 1, true,  8,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_A4_vcmpbgtui,      {{ 1, false, 7,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_A4_vcmpheqi,       {{ 1, true,  8,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_A4_vcmphgti,       {{ 1, true,  8,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_A4_vcmphgtui,      {{ 1, false, 7,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_A4_vcmpweqi,       {{ 1, true,  8,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_A4_vcmpwgti,       {{ 1, true,  8,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_A4_vcmpwgtui,      {{ 1, false, 7,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_C2_bitsclri,       {{ 1, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_C2_muxii,          {{ 2, true,  8,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_C4_nbitsclri,      {{ 1, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_F2_dfclass,        {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_F2_dfimm_n,        {{ 0, false, 10, 0 }} },
    { Hexagon::BI__builtin_HEXAGON_F2_dfimm_p,        {{ 0, false, 10, 0 }} },
    { Hexagon::BI__builtin_HEXAGON_F2_sfclass,        {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_F2_sfimm_n,        {{ 0, false, 10, 0 }} },
    { Hexagon::BI__builtin_HEXAGON_F2_sfimm_p,        {{ 0, false, 10, 0 }} },
    { Hexagon::BI__builtin_HEXAGON_M4_mpyri_addi,     {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_M4_mpyri_addr_u2,  {{ 1, false, 6,  2 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_addasl_rrri,    {{ 2, false, 3,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asl_i_p_acc,    {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asl_i_p_and,    {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asl_i_p,        {{ 1, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asl_i_p_nac,    {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asl_i_p_or,     {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asl_i_p_xacc,   {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asl_i_r_acc,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asl_i_r_and,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asl_i_r,        {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asl_i_r_nac,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asl_i_r_or,     {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asl_i_r_sat,    {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asl_i_r_xacc,   {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asl_i_vh,       {{ 1, false, 4,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asl_i_vw,       {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_p_acc,    {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_p_and,    {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_p,        {{ 1, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_p_nac,    {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_p_or,     {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_p_rnd_goodsyntax,
                                                      {{ 1, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_p_rnd,    {{ 1, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_r_acc,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_r_and,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_r,        {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_r_nac,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_r_or,     {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_r_rnd_goodsyntax,
                                                      {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_r_rnd,    {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_svw_trun, {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_vh,       {{ 1, false, 4,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_asr_i_vw,       {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_clrbit_i,       {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_extractu,       {{ 1, false, 5,  0 },
                                                       { 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_extractup,      {{ 1, false, 6,  0 },
                                                       { 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_insert,         {{ 2, false, 5,  0 },
                                                       { 3, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_insertp,        {{ 2, false, 6,  0 },
                                                       { 3, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_lsr_i_p_acc,    {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_lsr_i_p_and,    {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_lsr_i_p,        {{ 1, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_lsr_i_p_nac,    {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_lsr_i_p_or,     {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_lsr_i_p_xacc,   {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_lsr_i_r_acc,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_lsr_i_r_and,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_lsr_i_r,        {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_lsr_i_r_nac,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_lsr_i_r_or,     {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_lsr_i_r_xacc,   {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_lsr_i_vh,       {{ 1, false, 4,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_lsr_i_vw,       {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_setbit_i,       {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_tableidxb_goodsyntax,
                                                      {{ 2, false, 4,  0 },
                                                       { 3, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_tableidxd_goodsyntax,
                                                      {{ 2, false, 4,  0 },
                                                       { 3, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_tableidxh_goodsyntax,
                                                      {{ 2, false, 4,  0 },
                                                       { 3, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_tableidxw_goodsyntax,
                                                      {{ 2, false, 4,  0 },
                                                       { 3, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_togglebit_i,    {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_tstbit_i,       {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_valignib,       {{ 2, false, 3,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S2_vspliceib,      {{ 2, false, 3,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S4_addi_asl_ri,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S4_addi_lsr_ri,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S4_andi_asl_ri,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S4_andi_lsr_ri,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S4_clbaddi,        {{ 1, true , 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S4_clbpaddi,       {{ 1, true,  6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S4_extract,        {{ 1, false, 5,  0 },
                                                       { 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S4_extractp,       {{ 1, false, 6,  0 },
                                                       { 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S4_lsli,           {{ 0, true,  6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S4_ntstbit_i,      {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S4_ori_asl_ri,     {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S4_ori_lsr_ri,     {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S4_subi_asl_ri,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S4_subi_lsr_ri,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S4_vrcrotate_acc,  {{ 3, false, 2,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S4_vrcrotate,      {{ 2, false, 2,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S5_asrhub_rnd_sat_goodsyntax,
                                                      {{ 1, false, 4,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S5_asrhub_sat,     {{ 1, false, 4,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S5_vasrhrnd_goodsyntax,
                                                      {{ 1, false, 4,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_p,        {{ 1, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_p_acc,    {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_p_and,    {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_p_nac,    {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_p_or,     {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_p_xacc,   {{ 2, false, 6,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_r,        {{ 1, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_r_acc,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_r_and,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_r_nac,    {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_r_or,     {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_S6_rol_i_r_xacc,   {{ 2, false, 5,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_valignbi,       {{ 2, false, 3,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_valignbi_128B,  {{ 2, false, 3,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vlalignbi,      {{ 2, false, 3,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vlalignbi_128B, {{ 2, false, 3,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpybusi,      {{ 2, false, 1,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpybusi_128B, {{ 2, false, 1,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpybusi_acc,  {{ 3, false, 1,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpybusi_acc_128B,
                                                      {{ 3, false, 1,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpyubi,       {{ 2, false, 1,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpyubi_128B,  {{ 2, false, 1,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpyubi_acc,   {{ 3, false, 1,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vrmpyubi_acc_128B,
                                                      {{ 3, false, 1,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vrsadubi,       {{ 2, false, 1,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vrsadubi_128B,  {{ 2, false, 1,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vrsadubi_acc,   {{ 3, false, 1,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vrsadubi_acc_128B,
                                                      {{ 3, false, 1,  0 }} },

    { Hexagon::BI__builtin_HEXAGON_V6_v6mpyhubs10,    {{ 2, false, 2,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_v6mpyhubs10_128B,
                                                      {{ 2, false, 2,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_v6mpyhubs10_vxx,
                                                      {{ 3, false, 2,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_v6mpyhubs10_vxx_128B,
                                                      {{ 3, false, 2,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_v6mpyvubs10,    {{ 2, false, 2,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_v6mpyvubs10_128B,
                                                      {{ 2, false, 2,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_v6mpyvubs10_vxx,
                                                      {{ 3, false, 2,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_v6mpyvubs10_vxx_128B,
                                                      {{ 3, false, 2,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vlutvvbi,       {{ 2, false, 3,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vlutvvbi_128B,  {{ 2, false, 3,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vlutvvb_oracci, {{ 3, false, 3,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vlutvvb_oracci_128B,
                                                      {{ 3, false, 3,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vlutvwhi,       {{ 2, false, 3,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vlutvwhi_128B,  {{ 2, false, 3,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vlutvwh_oracci, {{ 3, false, 3,  0 }} },
    { Hexagon::BI__builtin_HEXAGON_V6_vlutvwh_oracci_128B,
                                                      {{ 3, false, 3,  0 }} },
  };

  // Use a dynamically initialized static to sort the table exactly once on
  // first run.
  static const bool SortOnce =
      (llvm::sort(Infos,
                  [](const BuiltinInfo &LHS, const BuiltinInfo &RHS) {
                    return LHS.BuiltinID < RHS.BuiltinID;
                  }),
       true);
  (void)SortOnce;

  const BuiltinInfo *F = llvm::partition_point(
      Infos, [=](const BuiltinInfo &BI) { return BI.BuiltinID < BuiltinID; });
  if (F == std::end(Infos) || F->BuiltinID != BuiltinID)
    return false;

  bool Error = false;

  for (const ArgInfo &A : F->Infos) {
    // Ignore empty ArgInfo elements.
    if (A.BitWidth == 0)
      continue;

    int32_t Min = A.IsSigned ? -(1 << (A.BitWidth - 1)) : 0;
    int32_t Max = (1 << (A.IsSigned ? A.BitWidth - 1 : A.BitWidth)) - 1;
    if (!A.Align) {
      Error |= SemaRef.BuiltinConstantArgRange(TheCall, A.OpNum, Min, Max);
    } else {
      unsigned M = 1 << A.Align;
      Min *= M;
      Max *= M;
      Error |= SemaRef.BuiltinConstantArgRange(TheCall, A.OpNum, Min, Max);
      Error |= SemaRef.BuiltinConstantArgMultiple(TheCall, A.OpNum, M);
    }
  }
  return Error;
}

bool SemaHexagon::CheckHexagonBuiltinFunctionCall(unsigned BuiltinID,
                                                  CallExpr *TheCall) {
  return CheckHexagonBuiltinArgument(BuiltinID, TheCall);
}

} // namespace clang
