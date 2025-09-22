//===------ SemaLoongArch.cpp ---- LoongArch target-specific routines -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis functions specific to LoongArch.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaLoongArch.h"
#include "clang/Basic/TargetBuiltins.h"
#include "clang/Sema/Sema.h"
#include "llvm/Support/MathExtras.h"

namespace clang {

SemaLoongArch::SemaLoongArch(Sema &S) : SemaBase(S) {}

bool SemaLoongArch::CheckLoongArchBuiltinFunctionCall(const TargetInfo &TI,
                                                      unsigned BuiltinID,
                                                      CallExpr *TheCall) {
  switch (BuiltinID) {
  default:
    break;
  // Basic intrinsics.
  case LoongArch::BI__builtin_loongarch_cacop_d:
  case LoongArch::BI__builtin_loongarch_cacop_w: {
    SemaRef.BuiltinConstantArgRange(TheCall, 0, 0, llvm::maxUIntN(5));
    SemaRef.BuiltinConstantArgRange(TheCall, 2, llvm::minIntN(12),
                                    llvm::maxIntN(12));
    break;
  }
  case LoongArch::BI__builtin_loongarch_break:
  case LoongArch::BI__builtin_loongarch_dbar:
  case LoongArch::BI__builtin_loongarch_ibar:
  case LoongArch::BI__builtin_loongarch_syscall:
    // Check if immediate is in [0, 32767].
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, 0, 32767);
  case LoongArch::BI__builtin_loongarch_csrrd_w:
  case LoongArch::BI__builtin_loongarch_csrrd_d:
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, 0, 16383);
  case LoongArch::BI__builtin_loongarch_csrwr_w:
  case LoongArch::BI__builtin_loongarch_csrwr_d:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 16383);
  case LoongArch::BI__builtin_loongarch_csrxchg_w:
  case LoongArch::BI__builtin_loongarch_csrxchg_d:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 16383);
  case LoongArch::BI__builtin_loongarch_lddir_d:
  case LoongArch::BI__builtin_loongarch_ldpte_d:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 31);
  case LoongArch::BI__builtin_loongarch_movfcsr2gr:
  case LoongArch::BI__builtin_loongarch_movgr2fcsr:
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, 0, llvm::maxUIntN(2));

  // LSX intrinsics.
  case LoongArch::BI__builtin_lsx_vbitclri_b:
  case LoongArch::BI__builtin_lsx_vbitrevi_b:
  case LoongArch::BI__builtin_lsx_vbitseti_b:
  case LoongArch::BI__builtin_lsx_vsat_b:
  case LoongArch::BI__builtin_lsx_vsat_bu:
  case LoongArch::BI__builtin_lsx_vslli_b:
  case LoongArch::BI__builtin_lsx_vsrai_b:
  case LoongArch::BI__builtin_lsx_vsrari_b:
  case LoongArch::BI__builtin_lsx_vsrli_b:
  case LoongArch::BI__builtin_lsx_vsllwil_h_b:
  case LoongArch::BI__builtin_lsx_vsllwil_hu_bu:
  case LoongArch::BI__builtin_lsx_vrotri_b:
  case LoongArch::BI__builtin_lsx_vsrlri_b:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 7);
  case LoongArch::BI__builtin_lsx_vbitclri_h:
  case LoongArch::BI__builtin_lsx_vbitrevi_h:
  case LoongArch::BI__builtin_lsx_vbitseti_h:
  case LoongArch::BI__builtin_lsx_vsat_h:
  case LoongArch::BI__builtin_lsx_vsat_hu:
  case LoongArch::BI__builtin_lsx_vslli_h:
  case LoongArch::BI__builtin_lsx_vsrai_h:
  case LoongArch::BI__builtin_lsx_vsrari_h:
  case LoongArch::BI__builtin_lsx_vsrli_h:
  case LoongArch::BI__builtin_lsx_vsllwil_w_h:
  case LoongArch::BI__builtin_lsx_vsllwil_wu_hu:
  case LoongArch::BI__builtin_lsx_vrotri_h:
  case LoongArch::BI__builtin_lsx_vsrlri_h:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 15);
  case LoongArch::BI__builtin_lsx_vssrarni_b_h:
  case LoongArch::BI__builtin_lsx_vssrarni_bu_h:
  case LoongArch::BI__builtin_lsx_vssrani_b_h:
  case LoongArch::BI__builtin_lsx_vssrani_bu_h:
  case LoongArch::BI__builtin_lsx_vsrarni_b_h:
  case LoongArch::BI__builtin_lsx_vsrlni_b_h:
  case LoongArch::BI__builtin_lsx_vsrlrni_b_h:
  case LoongArch::BI__builtin_lsx_vssrlni_b_h:
  case LoongArch::BI__builtin_lsx_vssrlni_bu_h:
  case LoongArch::BI__builtin_lsx_vssrlrni_b_h:
  case LoongArch::BI__builtin_lsx_vssrlrni_bu_h:
  case LoongArch::BI__builtin_lsx_vsrani_b_h:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 15);
  case LoongArch::BI__builtin_lsx_vslei_bu:
  case LoongArch::BI__builtin_lsx_vslei_hu:
  case LoongArch::BI__builtin_lsx_vslei_wu:
  case LoongArch::BI__builtin_lsx_vslei_du:
  case LoongArch::BI__builtin_lsx_vslti_bu:
  case LoongArch::BI__builtin_lsx_vslti_hu:
  case LoongArch::BI__builtin_lsx_vslti_wu:
  case LoongArch::BI__builtin_lsx_vslti_du:
  case LoongArch::BI__builtin_lsx_vmaxi_bu:
  case LoongArch::BI__builtin_lsx_vmaxi_hu:
  case LoongArch::BI__builtin_lsx_vmaxi_wu:
  case LoongArch::BI__builtin_lsx_vmaxi_du:
  case LoongArch::BI__builtin_lsx_vmini_bu:
  case LoongArch::BI__builtin_lsx_vmini_hu:
  case LoongArch::BI__builtin_lsx_vmini_wu:
  case LoongArch::BI__builtin_lsx_vmini_du:
  case LoongArch::BI__builtin_lsx_vaddi_bu:
  case LoongArch::BI__builtin_lsx_vaddi_hu:
  case LoongArch::BI__builtin_lsx_vaddi_wu:
  case LoongArch::BI__builtin_lsx_vaddi_du:
  case LoongArch::BI__builtin_lsx_vbitclri_w:
  case LoongArch::BI__builtin_lsx_vbitrevi_w:
  case LoongArch::BI__builtin_lsx_vbitseti_w:
  case LoongArch::BI__builtin_lsx_vsat_w:
  case LoongArch::BI__builtin_lsx_vsat_wu:
  case LoongArch::BI__builtin_lsx_vslli_w:
  case LoongArch::BI__builtin_lsx_vsrai_w:
  case LoongArch::BI__builtin_lsx_vsrari_w:
  case LoongArch::BI__builtin_lsx_vsrli_w:
  case LoongArch::BI__builtin_lsx_vsllwil_d_w:
  case LoongArch::BI__builtin_lsx_vsllwil_du_wu:
  case LoongArch::BI__builtin_lsx_vsrlri_w:
  case LoongArch::BI__builtin_lsx_vrotri_w:
  case LoongArch::BI__builtin_lsx_vsubi_bu:
  case LoongArch::BI__builtin_lsx_vsubi_hu:
  case LoongArch::BI__builtin_lsx_vbsrl_v:
  case LoongArch::BI__builtin_lsx_vbsll_v:
  case LoongArch::BI__builtin_lsx_vsubi_wu:
  case LoongArch::BI__builtin_lsx_vsubi_du:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 31);
  case LoongArch::BI__builtin_lsx_vssrarni_h_w:
  case LoongArch::BI__builtin_lsx_vssrarni_hu_w:
  case LoongArch::BI__builtin_lsx_vssrani_h_w:
  case LoongArch::BI__builtin_lsx_vssrani_hu_w:
  case LoongArch::BI__builtin_lsx_vsrarni_h_w:
  case LoongArch::BI__builtin_lsx_vsrani_h_w:
  case LoongArch::BI__builtin_lsx_vfrstpi_b:
  case LoongArch::BI__builtin_lsx_vfrstpi_h:
  case LoongArch::BI__builtin_lsx_vsrlni_h_w:
  case LoongArch::BI__builtin_lsx_vsrlrni_h_w:
  case LoongArch::BI__builtin_lsx_vssrlni_h_w:
  case LoongArch::BI__builtin_lsx_vssrlni_hu_w:
  case LoongArch::BI__builtin_lsx_vssrlrni_h_w:
  case LoongArch::BI__builtin_lsx_vssrlrni_hu_w:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 31);
  case LoongArch::BI__builtin_lsx_vbitclri_d:
  case LoongArch::BI__builtin_lsx_vbitrevi_d:
  case LoongArch::BI__builtin_lsx_vbitseti_d:
  case LoongArch::BI__builtin_lsx_vsat_d:
  case LoongArch::BI__builtin_lsx_vsat_du:
  case LoongArch::BI__builtin_lsx_vslli_d:
  case LoongArch::BI__builtin_lsx_vsrai_d:
  case LoongArch::BI__builtin_lsx_vsrli_d:
  case LoongArch::BI__builtin_lsx_vsrari_d:
  case LoongArch::BI__builtin_lsx_vrotri_d:
  case LoongArch::BI__builtin_lsx_vsrlri_d:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 63);
  case LoongArch::BI__builtin_lsx_vssrarni_w_d:
  case LoongArch::BI__builtin_lsx_vssrarni_wu_d:
  case LoongArch::BI__builtin_lsx_vssrani_w_d:
  case LoongArch::BI__builtin_lsx_vssrani_wu_d:
  case LoongArch::BI__builtin_lsx_vsrarni_w_d:
  case LoongArch::BI__builtin_lsx_vsrlni_w_d:
  case LoongArch::BI__builtin_lsx_vsrlrni_w_d:
  case LoongArch::BI__builtin_lsx_vssrlni_w_d:
  case LoongArch::BI__builtin_lsx_vssrlni_wu_d:
  case LoongArch::BI__builtin_lsx_vssrlrni_w_d:
  case LoongArch::BI__builtin_lsx_vssrlrni_wu_d:
  case LoongArch::BI__builtin_lsx_vsrani_w_d:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 63);
  case LoongArch::BI__builtin_lsx_vssrarni_d_q:
  case LoongArch::BI__builtin_lsx_vssrarni_du_q:
  case LoongArch::BI__builtin_lsx_vssrani_d_q:
  case LoongArch::BI__builtin_lsx_vssrani_du_q:
  case LoongArch::BI__builtin_lsx_vsrarni_d_q:
  case LoongArch::BI__builtin_lsx_vssrlni_d_q:
  case LoongArch::BI__builtin_lsx_vssrlni_du_q:
  case LoongArch::BI__builtin_lsx_vssrlrni_d_q:
  case LoongArch::BI__builtin_lsx_vssrlrni_du_q:
  case LoongArch::BI__builtin_lsx_vsrani_d_q:
  case LoongArch::BI__builtin_lsx_vsrlrni_d_q:
  case LoongArch::BI__builtin_lsx_vsrlni_d_q:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 127);
  case LoongArch::BI__builtin_lsx_vseqi_b:
  case LoongArch::BI__builtin_lsx_vseqi_h:
  case LoongArch::BI__builtin_lsx_vseqi_w:
  case LoongArch::BI__builtin_lsx_vseqi_d:
  case LoongArch::BI__builtin_lsx_vslti_b:
  case LoongArch::BI__builtin_lsx_vslti_h:
  case LoongArch::BI__builtin_lsx_vslti_w:
  case LoongArch::BI__builtin_lsx_vslti_d:
  case LoongArch::BI__builtin_lsx_vslei_b:
  case LoongArch::BI__builtin_lsx_vslei_h:
  case LoongArch::BI__builtin_lsx_vslei_w:
  case LoongArch::BI__builtin_lsx_vslei_d:
  case LoongArch::BI__builtin_lsx_vmaxi_b:
  case LoongArch::BI__builtin_lsx_vmaxi_h:
  case LoongArch::BI__builtin_lsx_vmaxi_w:
  case LoongArch::BI__builtin_lsx_vmaxi_d:
  case LoongArch::BI__builtin_lsx_vmini_b:
  case LoongArch::BI__builtin_lsx_vmini_h:
  case LoongArch::BI__builtin_lsx_vmini_w:
  case LoongArch::BI__builtin_lsx_vmini_d:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, -16, 15);
  case LoongArch::BI__builtin_lsx_vandi_b:
  case LoongArch::BI__builtin_lsx_vnori_b:
  case LoongArch::BI__builtin_lsx_vori_b:
  case LoongArch::BI__builtin_lsx_vshuf4i_b:
  case LoongArch::BI__builtin_lsx_vshuf4i_h:
  case LoongArch::BI__builtin_lsx_vshuf4i_w:
  case LoongArch::BI__builtin_lsx_vxori_b:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 255);
  case LoongArch::BI__builtin_lsx_vbitseli_b:
  case LoongArch::BI__builtin_lsx_vshuf4i_d:
  case LoongArch::BI__builtin_lsx_vextrins_b:
  case LoongArch::BI__builtin_lsx_vextrins_h:
  case LoongArch::BI__builtin_lsx_vextrins_w:
  case LoongArch::BI__builtin_lsx_vextrins_d:
  case LoongArch::BI__builtin_lsx_vpermi_w:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 255);
  case LoongArch::BI__builtin_lsx_vpickve2gr_b:
  case LoongArch::BI__builtin_lsx_vpickve2gr_bu:
  case LoongArch::BI__builtin_lsx_vreplvei_b:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 15);
  case LoongArch::BI__builtin_lsx_vinsgr2vr_b:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 15);
  case LoongArch::BI__builtin_lsx_vpickve2gr_h:
  case LoongArch::BI__builtin_lsx_vpickve2gr_hu:
  case LoongArch::BI__builtin_lsx_vreplvei_h:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 7);
  case LoongArch::BI__builtin_lsx_vinsgr2vr_h:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 7);
  case LoongArch::BI__builtin_lsx_vpickve2gr_w:
  case LoongArch::BI__builtin_lsx_vpickve2gr_wu:
  case LoongArch::BI__builtin_lsx_vreplvei_w:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 3);
  case LoongArch::BI__builtin_lsx_vinsgr2vr_w:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 3);
  case LoongArch::BI__builtin_lsx_vpickve2gr_d:
  case LoongArch::BI__builtin_lsx_vpickve2gr_du:
  case LoongArch::BI__builtin_lsx_vreplvei_d:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 1);
  case LoongArch::BI__builtin_lsx_vinsgr2vr_d:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 1);
  case LoongArch::BI__builtin_lsx_vstelm_b:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, -128, 127) ||
           SemaRef.BuiltinConstantArgRange(TheCall, 3, 0, 15);
  case LoongArch::BI__builtin_lsx_vstelm_h:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, -256, 254) ||
           SemaRef.BuiltinConstantArgRange(TheCall, 3, 0, 7);
  case LoongArch::BI__builtin_lsx_vstelm_w:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, -512, 508) ||
           SemaRef.BuiltinConstantArgRange(TheCall, 3, 0, 3);
  case LoongArch::BI__builtin_lsx_vstelm_d:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, -1024, 1016) ||
           SemaRef.BuiltinConstantArgRange(TheCall, 3, 0, 1);
  case LoongArch::BI__builtin_lsx_vldrepl_b:
  case LoongArch::BI__builtin_lsx_vld:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, -2048, 2047);
  case LoongArch::BI__builtin_lsx_vldrepl_h:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, -2048, 2046);
  case LoongArch::BI__builtin_lsx_vldrepl_w:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, -2048, 2044);
  case LoongArch::BI__builtin_lsx_vldrepl_d:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, -2048, 2040);
  case LoongArch::BI__builtin_lsx_vst:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, -2048, 2047);
  case LoongArch::BI__builtin_lsx_vldi:
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, -4096, 4095);
  case LoongArch::BI__builtin_lsx_vrepli_b:
  case LoongArch::BI__builtin_lsx_vrepli_h:
  case LoongArch::BI__builtin_lsx_vrepli_w:
  case LoongArch::BI__builtin_lsx_vrepli_d:
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, -512, 511);

  // LASX intrinsics.
  case LoongArch::BI__builtin_lasx_xvbitclri_b:
  case LoongArch::BI__builtin_lasx_xvbitrevi_b:
  case LoongArch::BI__builtin_lasx_xvbitseti_b:
  case LoongArch::BI__builtin_lasx_xvsat_b:
  case LoongArch::BI__builtin_lasx_xvsat_bu:
  case LoongArch::BI__builtin_lasx_xvslli_b:
  case LoongArch::BI__builtin_lasx_xvsrai_b:
  case LoongArch::BI__builtin_lasx_xvsrari_b:
  case LoongArch::BI__builtin_lasx_xvsrli_b:
  case LoongArch::BI__builtin_lasx_xvsllwil_h_b:
  case LoongArch::BI__builtin_lasx_xvsllwil_hu_bu:
  case LoongArch::BI__builtin_lasx_xvrotri_b:
  case LoongArch::BI__builtin_lasx_xvsrlri_b:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 7);
  case LoongArch::BI__builtin_lasx_xvbitclri_h:
  case LoongArch::BI__builtin_lasx_xvbitrevi_h:
  case LoongArch::BI__builtin_lasx_xvbitseti_h:
  case LoongArch::BI__builtin_lasx_xvsat_h:
  case LoongArch::BI__builtin_lasx_xvsat_hu:
  case LoongArch::BI__builtin_lasx_xvslli_h:
  case LoongArch::BI__builtin_lasx_xvsrai_h:
  case LoongArch::BI__builtin_lasx_xvsrari_h:
  case LoongArch::BI__builtin_lasx_xvsrli_h:
  case LoongArch::BI__builtin_lasx_xvsllwil_w_h:
  case LoongArch::BI__builtin_lasx_xvsllwil_wu_hu:
  case LoongArch::BI__builtin_lasx_xvrotri_h:
  case LoongArch::BI__builtin_lasx_xvsrlri_h:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 15);
  case LoongArch::BI__builtin_lasx_xvssrarni_b_h:
  case LoongArch::BI__builtin_lasx_xvssrarni_bu_h:
  case LoongArch::BI__builtin_lasx_xvssrani_b_h:
  case LoongArch::BI__builtin_lasx_xvssrani_bu_h:
  case LoongArch::BI__builtin_lasx_xvsrarni_b_h:
  case LoongArch::BI__builtin_lasx_xvsrlni_b_h:
  case LoongArch::BI__builtin_lasx_xvsrlrni_b_h:
  case LoongArch::BI__builtin_lasx_xvssrlni_b_h:
  case LoongArch::BI__builtin_lasx_xvssrlni_bu_h:
  case LoongArch::BI__builtin_lasx_xvssrlrni_b_h:
  case LoongArch::BI__builtin_lasx_xvssrlrni_bu_h:
  case LoongArch::BI__builtin_lasx_xvsrani_b_h:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 15);
  case LoongArch::BI__builtin_lasx_xvslei_bu:
  case LoongArch::BI__builtin_lasx_xvslei_hu:
  case LoongArch::BI__builtin_lasx_xvslei_wu:
  case LoongArch::BI__builtin_lasx_xvslei_du:
  case LoongArch::BI__builtin_lasx_xvslti_bu:
  case LoongArch::BI__builtin_lasx_xvslti_hu:
  case LoongArch::BI__builtin_lasx_xvslti_wu:
  case LoongArch::BI__builtin_lasx_xvslti_du:
  case LoongArch::BI__builtin_lasx_xvmaxi_bu:
  case LoongArch::BI__builtin_lasx_xvmaxi_hu:
  case LoongArch::BI__builtin_lasx_xvmaxi_wu:
  case LoongArch::BI__builtin_lasx_xvmaxi_du:
  case LoongArch::BI__builtin_lasx_xvmini_bu:
  case LoongArch::BI__builtin_lasx_xvmini_hu:
  case LoongArch::BI__builtin_lasx_xvmini_wu:
  case LoongArch::BI__builtin_lasx_xvmini_du:
  case LoongArch::BI__builtin_lasx_xvaddi_bu:
  case LoongArch::BI__builtin_lasx_xvaddi_hu:
  case LoongArch::BI__builtin_lasx_xvaddi_wu:
  case LoongArch::BI__builtin_lasx_xvaddi_du:
  case LoongArch::BI__builtin_lasx_xvbitclri_w:
  case LoongArch::BI__builtin_lasx_xvbitrevi_w:
  case LoongArch::BI__builtin_lasx_xvbitseti_w:
  case LoongArch::BI__builtin_lasx_xvsat_w:
  case LoongArch::BI__builtin_lasx_xvsat_wu:
  case LoongArch::BI__builtin_lasx_xvslli_w:
  case LoongArch::BI__builtin_lasx_xvsrai_w:
  case LoongArch::BI__builtin_lasx_xvsrari_w:
  case LoongArch::BI__builtin_lasx_xvsrli_w:
  case LoongArch::BI__builtin_lasx_xvsllwil_d_w:
  case LoongArch::BI__builtin_lasx_xvsllwil_du_wu:
  case LoongArch::BI__builtin_lasx_xvsrlri_w:
  case LoongArch::BI__builtin_lasx_xvrotri_w:
  case LoongArch::BI__builtin_lasx_xvsubi_bu:
  case LoongArch::BI__builtin_lasx_xvsubi_hu:
  case LoongArch::BI__builtin_lasx_xvsubi_wu:
  case LoongArch::BI__builtin_lasx_xvsubi_du:
  case LoongArch::BI__builtin_lasx_xvbsrl_v:
  case LoongArch::BI__builtin_lasx_xvbsll_v:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 31);
  case LoongArch::BI__builtin_lasx_xvssrarni_h_w:
  case LoongArch::BI__builtin_lasx_xvssrarni_hu_w:
  case LoongArch::BI__builtin_lasx_xvssrani_h_w:
  case LoongArch::BI__builtin_lasx_xvssrani_hu_w:
  case LoongArch::BI__builtin_lasx_xvsrarni_h_w:
  case LoongArch::BI__builtin_lasx_xvsrani_h_w:
  case LoongArch::BI__builtin_lasx_xvfrstpi_b:
  case LoongArch::BI__builtin_lasx_xvfrstpi_h:
  case LoongArch::BI__builtin_lasx_xvsrlni_h_w:
  case LoongArch::BI__builtin_lasx_xvsrlrni_h_w:
  case LoongArch::BI__builtin_lasx_xvssrlni_h_w:
  case LoongArch::BI__builtin_lasx_xvssrlni_hu_w:
  case LoongArch::BI__builtin_lasx_xvssrlrni_h_w:
  case LoongArch::BI__builtin_lasx_xvssrlrni_hu_w:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 31);
  case LoongArch::BI__builtin_lasx_xvbitclri_d:
  case LoongArch::BI__builtin_lasx_xvbitrevi_d:
  case LoongArch::BI__builtin_lasx_xvbitseti_d:
  case LoongArch::BI__builtin_lasx_xvsat_d:
  case LoongArch::BI__builtin_lasx_xvsat_du:
  case LoongArch::BI__builtin_lasx_xvslli_d:
  case LoongArch::BI__builtin_lasx_xvsrai_d:
  case LoongArch::BI__builtin_lasx_xvsrli_d:
  case LoongArch::BI__builtin_lasx_xvsrari_d:
  case LoongArch::BI__builtin_lasx_xvrotri_d:
  case LoongArch::BI__builtin_lasx_xvsrlri_d:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 63);
  case LoongArch::BI__builtin_lasx_xvssrarni_w_d:
  case LoongArch::BI__builtin_lasx_xvssrarni_wu_d:
  case LoongArch::BI__builtin_lasx_xvssrani_w_d:
  case LoongArch::BI__builtin_lasx_xvssrani_wu_d:
  case LoongArch::BI__builtin_lasx_xvsrarni_w_d:
  case LoongArch::BI__builtin_lasx_xvsrlni_w_d:
  case LoongArch::BI__builtin_lasx_xvsrlrni_w_d:
  case LoongArch::BI__builtin_lasx_xvssrlni_w_d:
  case LoongArch::BI__builtin_lasx_xvssrlni_wu_d:
  case LoongArch::BI__builtin_lasx_xvssrlrni_w_d:
  case LoongArch::BI__builtin_lasx_xvssrlrni_wu_d:
  case LoongArch::BI__builtin_lasx_xvsrani_w_d:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 63);
  case LoongArch::BI__builtin_lasx_xvssrarni_d_q:
  case LoongArch::BI__builtin_lasx_xvssrarni_du_q:
  case LoongArch::BI__builtin_lasx_xvssrani_d_q:
  case LoongArch::BI__builtin_lasx_xvssrani_du_q:
  case LoongArch::BI__builtin_lasx_xvsrarni_d_q:
  case LoongArch::BI__builtin_lasx_xvssrlni_d_q:
  case LoongArch::BI__builtin_lasx_xvssrlni_du_q:
  case LoongArch::BI__builtin_lasx_xvssrlrni_d_q:
  case LoongArch::BI__builtin_lasx_xvssrlrni_du_q:
  case LoongArch::BI__builtin_lasx_xvsrani_d_q:
  case LoongArch::BI__builtin_lasx_xvsrlni_d_q:
  case LoongArch::BI__builtin_lasx_xvsrlrni_d_q:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 127);
  case LoongArch::BI__builtin_lasx_xvseqi_b:
  case LoongArch::BI__builtin_lasx_xvseqi_h:
  case LoongArch::BI__builtin_lasx_xvseqi_w:
  case LoongArch::BI__builtin_lasx_xvseqi_d:
  case LoongArch::BI__builtin_lasx_xvslti_b:
  case LoongArch::BI__builtin_lasx_xvslti_h:
  case LoongArch::BI__builtin_lasx_xvslti_w:
  case LoongArch::BI__builtin_lasx_xvslti_d:
  case LoongArch::BI__builtin_lasx_xvslei_b:
  case LoongArch::BI__builtin_lasx_xvslei_h:
  case LoongArch::BI__builtin_lasx_xvslei_w:
  case LoongArch::BI__builtin_lasx_xvslei_d:
  case LoongArch::BI__builtin_lasx_xvmaxi_b:
  case LoongArch::BI__builtin_lasx_xvmaxi_h:
  case LoongArch::BI__builtin_lasx_xvmaxi_w:
  case LoongArch::BI__builtin_lasx_xvmaxi_d:
  case LoongArch::BI__builtin_lasx_xvmini_b:
  case LoongArch::BI__builtin_lasx_xvmini_h:
  case LoongArch::BI__builtin_lasx_xvmini_w:
  case LoongArch::BI__builtin_lasx_xvmini_d:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, -16, 15);
  case LoongArch::BI__builtin_lasx_xvandi_b:
  case LoongArch::BI__builtin_lasx_xvnori_b:
  case LoongArch::BI__builtin_lasx_xvori_b:
  case LoongArch::BI__builtin_lasx_xvshuf4i_b:
  case LoongArch::BI__builtin_lasx_xvshuf4i_h:
  case LoongArch::BI__builtin_lasx_xvshuf4i_w:
  case LoongArch::BI__builtin_lasx_xvxori_b:
  case LoongArch::BI__builtin_lasx_xvpermi_d:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 255);
  case LoongArch::BI__builtin_lasx_xvbitseli_b:
  case LoongArch::BI__builtin_lasx_xvshuf4i_d:
  case LoongArch::BI__builtin_lasx_xvextrins_b:
  case LoongArch::BI__builtin_lasx_xvextrins_h:
  case LoongArch::BI__builtin_lasx_xvextrins_w:
  case LoongArch::BI__builtin_lasx_xvextrins_d:
  case LoongArch::BI__builtin_lasx_xvpermi_q:
  case LoongArch::BI__builtin_lasx_xvpermi_w:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 255);
  case LoongArch::BI__builtin_lasx_xvrepl128vei_b:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 15);
  case LoongArch::BI__builtin_lasx_xvrepl128vei_h:
  case LoongArch::BI__builtin_lasx_xvpickve2gr_w:
  case LoongArch::BI__builtin_lasx_xvpickve2gr_wu:
  case LoongArch::BI__builtin_lasx_xvpickve_w_f:
  case LoongArch::BI__builtin_lasx_xvpickve_w:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 7);
  case LoongArch::BI__builtin_lasx_xvinsgr2vr_w:
  case LoongArch::BI__builtin_lasx_xvinsve0_w:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 7);
  case LoongArch::BI__builtin_lasx_xvrepl128vei_w:
  case LoongArch::BI__builtin_lasx_xvpickve2gr_d:
  case LoongArch::BI__builtin_lasx_xvpickve2gr_du:
  case LoongArch::BI__builtin_lasx_xvpickve_d_f:
  case LoongArch::BI__builtin_lasx_xvpickve_d:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 3);
  case LoongArch::BI__builtin_lasx_xvinsve0_d:
  case LoongArch::BI__builtin_lasx_xvinsgr2vr_d:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, 0, 3);
  case LoongArch::BI__builtin_lasx_xvstelm_b:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, -128, 127) ||
           SemaRef.BuiltinConstantArgRange(TheCall, 3, 0, 31);
  case LoongArch::BI__builtin_lasx_xvstelm_h:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, -256, 254) ||
           SemaRef.BuiltinConstantArgRange(TheCall, 3, 0, 15);
  case LoongArch::BI__builtin_lasx_xvstelm_w:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, -512, 508) ||
           SemaRef.BuiltinConstantArgRange(TheCall, 3, 0, 7);
  case LoongArch::BI__builtin_lasx_xvstelm_d:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, -1024, 1016) ||
           SemaRef.BuiltinConstantArgRange(TheCall, 3, 0, 3);
  case LoongArch::BI__builtin_lasx_xvrepl128vei_d:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, 0, 1);
  case LoongArch::BI__builtin_lasx_xvldrepl_b:
  case LoongArch::BI__builtin_lasx_xvld:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, -2048, 2047);
  case LoongArch::BI__builtin_lasx_xvldrepl_h:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, -2048, 2046);
  case LoongArch::BI__builtin_lasx_xvldrepl_w:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, -2048, 2044);
  case LoongArch::BI__builtin_lasx_xvldrepl_d:
    return SemaRef.BuiltinConstantArgRange(TheCall, 1, -2048, 2040);
  case LoongArch::BI__builtin_lasx_xvst:
    return SemaRef.BuiltinConstantArgRange(TheCall, 2, -2048, 2047);
  case LoongArch::BI__builtin_lasx_xvldi:
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, -4096, 4095);
  case LoongArch::BI__builtin_lasx_xvrepli_b:
  case LoongArch::BI__builtin_lasx_xvrepli_h:
  case LoongArch::BI__builtin_lasx_xvrepli_w:
  case LoongArch::BI__builtin_lasx_xvrepli_d:
    return SemaRef.BuiltinConstantArgRange(TheCall, 0, -512, 511);
  }
  return false;
}

} // namespace clang
