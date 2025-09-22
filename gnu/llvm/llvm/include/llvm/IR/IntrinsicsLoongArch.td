//===- IntrinsicsLoongArch.td - Defines LoongArch intrinsics *- tablegen -*===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines all of the LoongArch-specific intrinsics.
//
//===----------------------------------------------------------------------===//

let TargetPrefix = "loongarch" in {

//===----------------------------------------------------------------------===//
// Atomics

// T @llvm.<name>.T.<p>(any*, T, T, T imm);
class MaskedAtomicRMW<LLVMType itype>
    : Intrinsic<[itype], [llvm_anyptr_ty, itype, itype, itype],
                [IntrArgMemOnly, NoCapture<ArgIndex<0>>, ImmArg<ArgIndex<3>>]>;

// We define 32-bit and 64-bit variants of the above, where T stands for i32
// or i64 respectively:
multiclass MaskedAtomicRMWIntrinsics {
  // i32 @llvm.<name>.i32.<p>(any*, i32, i32, i32 imm);
  def _i32 : MaskedAtomicRMW<llvm_i32_ty>;
  // i64 @llvm.<name>.i32.<p>(any*, i64, i64, i64 imm);
  def _i64 : MaskedAtomicRMW<llvm_i64_ty>;
}

multiclass MaskedAtomicRMWFiveOpIntrinsics {
  // TODO: Support cmpxchg on LA32.
  // i64 @llvm.<name>.i64.<p>(any*, i64, i64, i64, i64 imm);
  def _i64 : MaskedAtomicRMWFiveArg<llvm_i64_ty>;
}

defm int_loongarch_masked_atomicrmw_xchg : MaskedAtomicRMWIntrinsics;
defm int_loongarch_masked_atomicrmw_add : MaskedAtomicRMWIntrinsics;
defm int_loongarch_masked_atomicrmw_sub : MaskedAtomicRMWIntrinsics;
defm int_loongarch_masked_atomicrmw_nand : MaskedAtomicRMWIntrinsics;
defm int_loongarch_masked_atomicrmw_umax : MaskedAtomicRMWIntrinsics;
defm int_loongarch_masked_atomicrmw_umin : MaskedAtomicRMWIntrinsics;
defm int_loongarch_masked_atomicrmw_max : MaskedAtomicRMWFiveOpIntrinsics;
defm int_loongarch_masked_atomicrmw_min : MaskedAtomicRMWFiveOpIntrinsics;

// @llvm.loongarch.masked.cmpxchg.i64.<p>(
//   ptr addr, grlen cmpval, grlen newval, grlen mask, grlenimm ordering)
defm int_loongarch_masked_cmpxchg : MaskedAtomicRMWFiveOpIntrinsics;

//===----------------------------------------------------------------------===//
// LoongArch BASE

class BaseInt<list<LLVMType> ret_types, list<LLVMType> param_types,
              list<IntrinsicProperty> intr_properties = []>
    : Intrinsic<ret_types, param_types, intr_properties>,
      ClangBuiltin<!subst("int_loongarch", "__builtin_loongarch", NAME)>;

def int_loongarch_break : BaseInt<[], [llvm_i32_ty], [ImmArg<ArgIndex<0>>]>;
def int_loongarch_cacop_d : BaseInt<[], [llvm_i64_ty, llvm_i64_ty, llvm_i64_ty],
                                    [ImmArg<ArgIndex<0>>, ImmArg<ArgIndex<2>>]>;
def int_loongarch_cacop_w : BaseInt<[], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
                                    [ImmArg<ArgIndex<0>>, ImmArg<ArgIndex<2>>]>;
def int_loongarch_dbar : BaseInt<[], [llvm_i32_ty], [ImmArg<ArgIndex<0>>]>;

def int_loongarch_ibar : BaseInt<[], [llvm_i32_ty], [ImmArg<ArgIndex<0>>]>;
def int_loongarch_movfcsr2gr : BaseInt<[llvm_i32_ty], [llvm_i32_ty],
                                       [ImmArg<ArgIndex<0>>]>;
def int_loongarch_movgr2fcsr : BaseInt<[], [llvm_i32_ty, llvm_i32_ty],
                                       [ImmArg<ArgIndex<0>>]>;
def int_loongarch_syscall : BaseInt<[], [llvm_i32_ty], [ImmArg<ArgIndex<0>>]>;

def int_loongarch_crc_w_b_w : BaseInt<[llvm_i32_ty],
                                      [llvm_i32_ty, llvm_i32_ty]>;
def int_loongarch_crc_w_h_w : BaseInt<[llvm_i32_ty],
                                      [llvm_i32_ty, llvm_i32_ty]>;
def int_loongarch_crc_w_w_w : BaseInt<[llvm_i32_ty],
                                      [llvm_i32_ty, llvm_i32_ty]>;
def int_loongarch_crc_w_d_w : BaseInt<[llvm_i32_ty],
                                      [llvm_i64_ty, llvm_i32_ty]>;

def int_loongarch_crcc_w_b_w : BaseInt<[llvm_i32_ty],
                                       [llvm_i32_ty, llvm_i32_ty]>;
def int_loongarch_crcc_w_h_w : BaseInt<[llvm_i32_ty],
                                       [llvm_i32_ty, llvm_i32_ty]>;
def int_loongarch_crcc_w_w_w : BaseInt<[llvm_i32_ty],
                                       [llvm_i32_ty, llvm_i32_ty]>;
def int_loongarch_crcc_w_d_w : BaseInt<[llvm_i32_ty],
                                       [llvm_i64_ty, llvm_i32_ty]>;

def int_loongarch_csrrd_w : BaseInt<[llvm_i32_ty], [llvm_i32_ty],
                                    [ImmArg<ArgIndex<0>>]>;
def int_loongarch_csrrd_d : BaseInt<[llvm_i64_ty], [llvm_i32_ty],
                                    [ImmArg<ArgIndex<0>>]>;
def int_loongarch_csrwr_w : BaseInt<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
                                    [ImmArg<ArgIndex<1>>]>;
def int_loongarch_csrwr_d : BaseInt<[llvm_i64_ty], [llvm_i64_ty, llvm_i32_ty],
                                    [ImmArg<ArgIndex<1>>]>;
def int_loongarch_csrxchg_w : BaseInt<[llvm_i32_ty],
                                      [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
                                      [ImmArg<ArgIndex<2>>]>;
def int_loongarch_csrxchg_d : BaseInt<[llvm_i64_ty],
                                      [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty],
                                      [ImmArg<ArgIndex<2>>]>;

def int_loongarch_iocsrrd_b : BaseInt<[llvm_i32_ty], [llvm_i32_ty]>;
def int_loongarch_iocsrrd_h : BaseInt<[llvm_i32_ty], [llvm_i32_ty]>;
def int_loongarch_iocsrrd_w : BaseInt<[llvm_i32_ty], [llvm_i32_ty]>;
def int_loongarch_iocsrrd_d : BaseInt<[llvm_i64_ty], [llvm_i32_ty]>;

def int_loongarch_iocsrwr_b : BaseInt<[], [llvm_i32_ty, llvm_i32_ty]>;
def int_loongarch_iocsrwr_h : BaseInt<[], [llvm_i32_ty, llvm_i32_ty]>;
def int_loongarch_iocsrwr_w : BaseInt<[], [llvm_i32_ty, llvm_i32_ty]>;
def int_loongarch_iocsrwr_d : BaseInt<[], [llvm_i64_ty, llvm_i32_ty]>;

def int_loongarch_cpucfg : BaseInt<[llvm_i32_ty], [llvm_i32_ty]>;

def int_loongarch_asrtle_d : BaseInt<[], [llvm_i64_ty, llvm_i64_ty]>;
def int_loongarch_asrtgt_d : BaseInt<[], [llvm_i64_ty, llvm_i64_ty]>;

def int_loongarch_lddir_d : BaseInt<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty],
                                    [ImmArg<ArgIndex<1>>]>;
def int_loongarch_ldpte_d : BaseInt<[], [llvm_i64_ty, llvm_i64_ty],
                                    [ImmArg<ArgIndex<1>>]>;

def int_loongarch_frecipe_s : BaseInt<[llvm_float_ty], [llvm_float_ty],
                                      [IntrNoMem]>;
def int_loongarch_frecipe_d : BaseInt<[llvm_double_ty], [llvm_double_ty],
                                      [IntrNoMem]>;
def int_loongarch_frsqrte_s : BaseInt<[llvm_float_ty], [llvm_float_ty],
                                      [IntrNoMem]>;
def int_loongarch_frsqrte_d : BaseInt<[llvm_double_ty], [llvm_double_ty],
                                      [IntrNoMem]>;
} // TargetPrefix = "loongarch"

/// Vector intrinsic

class VecInt<list<LLVMType> ret_types, list<LLVMType> param_types,
             list<IntrinsicProperty> intr_properties = []>
    : Intrinsic<ret_types, param_types, intr_properties>,
      ClangBuiltin<!subst("int_loongarch", "__builtin", NAME)>;

//===----------------------------------------------------------------------===//
// LSX

let TargetPrefix = "loongarch" in {

foreach inst = ["vadd_b", "vsub_b",
                "vsadd_b", "vsadd_bu", "vssub_b", "vssub_bu",
                "vavg_b", "vavg_bu", "vavgr_b", "vavgr_bu",
                "vabsd_b", "vabsd_bu", "vadda_b",
                "vmax_b", "vmax_bu", "vmin_b", "vmin_bu",
                "vmul_b", "vmuh_b", "vmuh_bu",
                "vdiv_b", "vdiv_bu", "vmod_b", "vmod_bu", "vsigncov_b",
                "vand_v", "vor_v", "vxor_v", "vnor_v", "vandn_v", "vorn_v",
                "vsll_b", "vsrl_b", "vsra_b", "vrotr_b", "vsrlr_b", "vsrar_b",
                "vbitclr_b", "vbitset_b", "vbitrev_b",
                "vseq_b", "vsle_b", "vsle_bu", "vslt_b", "vslt_bu",
                "vpackev_b", "vpackod_b", "vpickev_b", "vpickod_b",
                "vilvl_b", "vilvh_b"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v16i8_ty],
                                       [llvm_v16i8_ty, llvm_v16i8_ty],
                                       [IntrNoMem]>;

foreach inst = ["vadd_h", "vsub_h",
                "vsadd_h", "vsadd_hu", "vssub_h", "vssub_hu",
                "vavg_h", "vavg_hu", "vavgr_h", "vavgr_hu",
                "vabsd_h", "vabsd_hu", "vadda_h",
                "vmax_h", "vmax_hu", "vmin_h", "vmin_hu",
                "vmul_h", "vmuh_h", "vmuh_hu",
                "vdiv_h", "vdiv_hu", "vmod_h", "vmod_hu", "vsigncov_h",
                "vsll_h", "vsrl_h", "vsra_h", "vrotr_h", "vsrlr_h", "vsrar_h",
                "vbitclr_h", "vbitset_h", "vbitrev_h",
                "vseq_h", "vsle_h", "vsle_hu", "vslt_h", "vslt_hu",
                "vpackev_h", "vpackod_h", "vpickev_h", "vpickod_h",
                "vilvl_h", "vilvh_h"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v8i16_ty],
                                       [llvm_v8i16_ty, llvm_v8i16_ty],
                                       [IntrNoMem]>;

foreach inst = ["vadd_w", "vsub_w",
                "vsadd_w", "vsadd_wu", "vssub_w", "vssub_wu",
                "vavg_w", "vavg_wu", "vavgr_w", "vavgr_wu",
                "vabsd_w", "vabsd_wu", "vadda_w",
                "vmax_w", "vmax_wu", "vmin_w", "vmin_wu",
                "vmul_w", "vmuh_w", "vmuh_wu",
                "vdiv_w", "vdiv_wu", "vmod_w", "vmod_wu", "vsigncov_w",
                "vsll_w", "vsrl_w", "vsra_w", "vrotr_w", "vsrlr_w", "vsrar_w",
                "vbitclr_w", "vbitset_w", "vbitrev_w",
                "vseq_w", "vsle_w", "vsle_wu", "vslt_w", "vslt_wu",
                "vpackev_w", "vpackod_w", "vpickev_w", "vpickod_w",
                "vilvl_w", "vilvh_w"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v4i32_ty],
                                       [llvm_v4i32_ty, llvm_v4i32_ty],
                                       [IntrNoMem]>;

foreach inst = ["vadd_d", "vadd_q", "vsub_d", "vsub_q",
                "vsadd_d", "vsadd_du", "vssub_d", "vssub_du",
                "vhaddw_q_d", "vhaddw_qu_du", "vhsubw_q_d", "vhsubw_qu_du",
                "vaddwev_q_d", "vaddwod_q_d", "vsubwev_q_d", "vsubwod_q_d",
                "vaddwev_q_du", "vaddwod_q_du", "vsubwev_q_du", "vsubwod_q_du",
                "vaddwev_q_du_d", "vaddwod_q_du_d",
                "vavg_d", "vavg_du", "vavgr_d", "vavgr_du",
                "vabsd_d", "vabsd_du", "vadda_d",
                "vmax_d", "vmax_du", "vmin_d", "vmin_du",
                "vmul_d", "vmuh_d", "vmuh_du",
                "vmulwev_q_d", "vmulwod_q_d", "vmulwev_q_du", "vmulwod_q_du",
                "vmulwev_q_du_d", "vmulwod_q_du_d",
                "vdiv_d", "vdiv_du", "vmod_d", "vmod_du", "vsigncov_d",
                "vsll_d", "vsrl_d", "vsra_d", "vrotr_d", "vsrlr_d", "vsrar_d",
                "vbitclr_d", "vbitset_d", "vbitrev_d",
                "vseq_d", "vsle_d", "vsle_du", "vslt_d", "vslt_du",
                "vpackev_d", "vpackod_d", "vpickev_d", "vpickod_d",
                "vilvl_d", "vilvh_d"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v2i64_ty],
                                       [llvm_v2i64_ty, llvm_v2i64_ty],
                                       [IntrNoMem]>;

foreach inst = ["vaddi_bu", "vsubi_bu",
                "vmaxi_b", "vmaxi_bu", "vmini_b", "vmini_bu",
                "vsat_b", "vsat_bu",
                "vandi_b", "vori_b", "vxori_b", "vnori_b",
                "vslli_b", "vsrli_b", "vsrai_b", "vrotri_b",
                "vsrlri_b", "vsrari_b",
                "vbitclri_b", "vbitseti_b", "vbitrevi_b",
                "vseqi_b", "vslei_b", "vslei_bu", "vslti_b", "vslti_bu",
                "vreplvei_b", "vbsll_v", "vbsrl_v", "vshuf4i_b"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v16i8_ty],
                                       [llvm_v16i8_ty, llvm_i32_ty],
                                       [IntrNoMem, ImmArg<ArgIndex<1>>]>;
foreach inst = ["vaddi_hu", "vsubi_hu",
                "vmaxi_h", "vmaxi_hu", "vmini_h", "vmini_hu",
                "vsat_h", "vsat_hu",
                "vslli_h", "vsrli_h", "vsrai_h", "vrotri_h",
                "vsrlri_h", "vsrari_h",
                "vbitclri_h", "vbitseti_h", "vbitrevi_h",
                "vseqi_h", "vslei_h", "vslei_hu", "vslti_h", "vslti_hu",
                "vreplvei_h", "vshuf4i_h"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v8i16_ty],
                                       [llvm_v8i16_ty, llvm_i32_ty],
                                       [IntrNoMem, ImmArg<ArgIndex<1>>]>;
foreach inst = ["vaddi_wu", "vsubi_wu",
                "vmaxi_w", "vmaxi_wu", "vmini_w", "vmini_wu",
                "vsat_w", "vsat_wu",
                "vslli_w", "vsrli_w", "vsrai_w", "vrotri_w",
                "vsrlri_w", "vsrari_w",
                "vbitclri_w", "vbitseti_w", "vbitrevi_w",
                "vseqi_w", "vslei_w", "vslei_wu", "vslti_w", "vslti_wu",
                "vreplvei_w", "vshuf4i_w"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v4i32_ty],
                                       [llvm_v4i32_ty, llvm_i32_ty],
                                       [IntrNoMem, ImmArg<ArgIndex<1>>]>;
foreach inst = ["vaddi_du", "vsubi_du",
                "vmaxi_d", "vmaxi_du", "vmini_d", "vmini_du",
                "vsat_d", "vsat_du",
                "vslli_d", "vsrli_d", "vsrai_d", "vrotri_d",
                "vsrlri_d", "vsrari_d",
                "vbitclri_d", "vbitseti_d", "vbitrevi_d",
                "vseqi_d", "vslei_d", "vslei_du", "vslti_d", "vslti_du",
                "vreplvei_d"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v2i64_ty],
                                       [llvm_v2i64_ty, llvm_i32_ty],
                                       [IntrNoMem, ImmArg<ArgIndex<1>>]>;

foreach inst = ["vhaddw_h_b", "vhaddw_hu_bu", "vhsubw_h_b", "vhsubw_hu_bu",
                "vaddwev_h_b", "vaddwod_h_b", "vsubwev_h_b", "vsubwod_h_b",
                "vaddwev_h_bu", "vaddwod_h_bu", "vsubwev_h_bu", "vsubwod_h_bu",
                "vaddwev_h_bu_b", "vaddwod_h_bu_b",
                "vmulwev_h_b", "vmulwod_h_b", "vmulwev_h_bu", "vmulwod_h_bu",
                "vmulwev_h_bu_b", "vmulwod_h_bu_b"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v8i16_ty],
                                       [llvm_v16i8_ty, llvm_v16i8_ty],
                                       [IntrNoMem]>;

foreach inst = ["vhaddw_w_h", "vhaddw_wu_hu", "vhsubw_w_h", "vhsubw_wu_hu",
                "vaddwev_w_h", "vaddwod_w_h", "vsubwev_w_h", "vsubwod_w_h",
                "vaddwev_w_hu", "vaddwod_w_hu", "vsubwev_w_hu", "vsubwod_w_hu",
                "vaddwev_w_hu_h", "vaddwod_w_hu_h",
                "vmulwev_w_h", "vmulwod_w_h", "vmulwev_w_hu", "vmulwod_w_hu",
                "vmulwev_w_hu_h", "vmulwod_w_hu_h"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v4i32_ty],
                                       [llvm_v8i16_ty, llvm_v8i16_ty],
                                       [IntrNoMem]>;

foreach inst = ["vhaddw_d_w", "vhaddw_du_wu", "vhsubw_d_w", "vhsubw_du_wu",
                "vaddwev_d_w", "vaddwod_d_w", "vsubwev_d_w", "vsubwod_d_w",
                "vaddwev_d_wu", "vaddwod_d_wu", "vsubwev_d_wu", "vsubwod_d_wu",
                "vaddwev_d_wu_w", "vaddwod_d_wu_w",
                "vmulwev_d_w", "vmulwod_d_w", "vmulwev_d_wu", "vmulwod_d_wu",
                "vmulwev_d_wu_w", "vmulwod_d_wu_w"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v2i64_ty],
                                       [llvm_v4i32_ty, llvm_v4i32_ty],
                                       [IntrNoMem]>;

foreach inst = ["vsrln_b_h", "vsran_b_h", "vsrlrn_b_h", "vsrarn_b_h",
                "vssrln_b_h", "vssran_b_h", "vssrln_bu_h", "vssran_bu_h",
                "vssrlrn_b_h", "vssrarn_b_h", "vssrlrn_bu_h", "vssrarn_bu_h"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v16i8_ty],
                                       [llvm_v8i16_ty, llvm_v8i16_ty],
                                       [IntrNoMem]>;

foreach inst = ["vsrln_h_w", "vsran_h_w", "vsrlrn_h_w", "vsrarn_h_w",
                "vssrln_h_w", "vssran_h_w", "vssrln_hu_w", "vssran_hu_w",
                "vssrlrn_h_w", "vssrarn_h_w", "vssrlrn_hu_w", "vssrarn_hu_w"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v8i16_ty],
                                       [llvm_v4i32_ty, llvm_v4i32_ty],
                                       [IntrNoMem]>;

foreach inst = ["vsrln_w_d", "vsran_w_d", "vsrlrn_w_d", "vsrarn_w_d",
                "vssrln_w_d", "vssran_w_d", "vssrln_wu_d", "vssran_wu_d",
                "vssrlrn_w_d", "vssrarn_w_d", "vssrlrn_wu_d", "vssrarn_wu_d"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v4i32_ty],
                                       [llvm_v2i64_ty, llvm_v2i64_ty],
                                       [IntrNoMem]>;

foreach inst = ["vmadd_b", "vmsub_b", "vfrstp_b", "vbitsel_v", "vshuf_b"] in
  def int_loongarch_lsx_#inst
    : VecInt<[llvm_v16i8_ty],
             [llvm_v16i8_ty, llvm_v16i8_ty, llvm_v16i8_ty],
             [IntrNoMem]>;
foreach inst = ["vmadd_h", "vmsub_h", "vfrstp_h", "vshuf_h"] in
  def int_loongarch_lsx_#inst
    : VecInt<[llvm_v8i16_ty],
             [llvm_v8i16_ty, llvm_v8i16_ty, llvm_v8i16_ty],
             [IntrNoMem]>;
foreach inst = ["vmadd_w", "vmsub_w", "vshuf_w"] in
  def int_loongarch_lsx_#inst
    : VecInt<[llvm_v4i32_ty],
             [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
             [IntrNoMem]>;
foreach inst = ["vmadd_d", "vmsub_d", "vshuf_d"] in
  def int_loongarch_lsx_#inst
    : VecInt<[llvm_v2i64_ty],
             [llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty],
             [IntrNoMem]>;

foreach inst = ["vsrlni_b_h", "vsrani_b_h", "vsrlrni_b_h", "vsrarni_b_h",
                "vssrlni_b_h", "vssrani_b_h", "vssrlni_bu_h", "vssrani_bu_h",
                "vssrlrni_b_h", "vssrarni_b_h", "vssrlrni_bu_h", "vssrarni_bu_h",
                "vfrstpi_b", "vbitseli_b", "vextrins_b"] in
  def int_loongarch_lsx_#inst
    : VecInt<[llvm_v16i8_ty],
             [llvm_v16i8_ty, llvm_v16i8_ty, llvm_i32_ty],
             [IntrNoMem, ImmArg<ArgIndex<2>>]>;
foreach inst = ["vsrlni_h_w", "vsrani_h_w", "vsrlrni_h_w", "vsrarni_h_w",
                "vssrlni_h_w", "vssrani_h_w", "vssrlni_hu_w", "vssrani_hu_w",
                "vssrlrni_h_w", "vssrarni_h_w", "vssrlrni_hu_w", "vssrarni_hu_w",
                "vfrstpi_h", "vextrins_h"] in
  def int_loongarch_lsx_#inst
    : VecInt<[llvm_v8i16_ty],
             [llvm_v8i16_ty, llvm_v8i16_ty, llvm_i32_ty],
             [IntrNoMem, ImmArg<ArgIndex<2>>]>;
foreach inst = ["vsrlni_w_d", "vsrani_w_d", "vsrlrni_w_d", "vsrarni_w_d",
                "vssrlni_w_d", "vssrani_w_d", "vssrlni_wu_d", "vssrani_wu_d",
                "vssrlrni_w_d", "vssrarni_w_d", "vssrlrni_wu_d", "vssrarni_wu_d",
                "vpermi_w", "vextrins_w"] in
  def int_loongarch_lsx_#inst
    : VecInt<[llvm_v4i32_ty],
             [llvm_v4i32_ty, llvm_v4i32_ty, llvm_i32_ty],
             [IntrNoMem, ImmArg<ArgIndex<2>>]>;
foreach inst = ["vsrlni_d_q", "vsrani_d_q", "vsrlrni_d_q", "vsrarni_d_q",
                "vssrlni_d_q", "vssrani_d_q", "vssrlni_du_q", "vssrani_du_q",
                "vssrlrni_d_q", "vssrarni_d_q", "vssrlrni_du_q", "vssrarni_du_q",
                "vshuf4i_d", "vextrins_d"] in
  def int_loongarch_lsx_#inst
    : VecInt<[llvm_v2i64_ty],
             [llvm_v2i64_ty, llvm_v2i64_ty, llvm_i32_ty],
             [IntrNoMem, ImmArg<ArgIndex<2>>]>;

foreach inst = ["vmaddwev_h_b", "vmaddwod_h_b", "vmaddwev_h_bu",
                "vmaddwod_h_bu", "vmaddwev_h_bu_b", "vmaddwod_h_bu_b"] in
  def int_loongarch_lsx_#inst
    : VecInt<[llvm_v8i16_ty],
             [llvm_v8i16_ty, llvm_v16i8_ty, llvm_v16i8_ty],
             [IntrNoMem]>;
foreach inst = ["vmaddwev_w_h", "vmaddwod_w_h", "vmaddwev_w_hu",
                "vmaddwod_w_hu", "vmaddwev_w_hu_h", "vmaddwod_w_hu_h"] in
  def int_loongarch_lsx_#inst
    : VecInt<[llvm_v4i32_ty],
             [llvm_v4i32_ty, llvm_v8i16_ty, llvm_v8i16_ty],
             [IntrNoMem]>;
foreach inst = ["vmaddwev_d_w", "vmaddwod_d_w", "vmaddwev_d_wu",
                "vmaddwod_d_wu", "vmaddwev_d_wu_w", "vmaddwod_d_wu_w"] in
  def int_loongarch_lsx_#inst
    : VecInt<[llvm_v2i64_ty],
             [llvm_v2i64_ty, llvm_v4i32_ty, llvm_v4i32_ty],
             [IntrNoMem]>;
foreach inst = ["vmaddwev_q_d", "vmaddwod_q_d", "vmaddwev_q_du",
                "vmaddwod_q_du", "vmaddwev_q_du_d", "vmaddwod_q_du_d"] in
  def int_loongarch_lsx_#inst
    : VecInt<[llvm_v2i64_ty],
             [llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty],
             [IntrNoMem]>;

foreach inst = ["vsllwil_h_b", "vsllwil_hu_bu"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v8i16_ty],
                                       [llvm_v16i8_ty, llvm_i32_ty],
                                       [IntrNoMem, ImmArg<ArgIndex<1>>]>;
foreach inst = ["vsllwil_w_h", "vsllwil_wu_hu"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v4i32_ty],
                                       [llvm_v8i16_ty, llvm_i32_ty],
                                       [IntrNoMem, ImmArg<ArgIndex<1>>]>;
foreach inst = ["vsllwil_d_w", "vsllwil_du_wu"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v2i64_ty],
                                       [llvm_v4i32_ty, llvm_i32_ty],
                                       [IntrNoMem, ImmArg<ArgIndex<1>>]>;

foreach inst = ["vneg_b", "vmskltz_b", "vmskgez_b", "vmsknz_b",
                "vclo_b", "vclz_b", "vpcnt_b"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v16i8_ty], [llvm_v16i8_ty],
                                       [IntrNoMem]>;
foreach inst = ["vneg_h", "vmskltz_h", "vclo_h", "vclz_h", "vpcnt_h"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v8i16_ty], [llvm_v8i16_ty],
                                       [IntrNoMem]>;
foreach inst = ["vneg_w", "vmskltz_w", "vclo_w", "vclz_w", "vpcnt_w"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v4i32_ty], [llvm_v4i32_ty],
                                       [IntrNoMem]>;
foreach inst = ["vneg_d", "vexth_q_d", "vexth_qu_du", "vmskltz_d",
                "vextl_q_d", "vextl_qu_du", "vclo_d", "vclz_d", "vpcnt_d"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v2i64_ty], [llvm_v2i64_ty],
                                       [IntrNoMem]>;

foreach inst = ["vexth_h_b", "vexth_hu_bu"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v8i16_ty], [llvm_v16i8_ty],
                                       [IntrNoMem]>;
foreach inst = ["vexth_w_h", "vexth_wu_hu"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v4i32_ty], [llvm_v8i16_ty],
                                       [IntrNoMem]>;
foreach inst = ["vexth_d_w", "vexth_du_wu"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v2i64_ty], [llvm_v4i32_ty],
                                       [IntrNoMem]>;

def int_loongarch_lsx_vldi : VecInt<[llvm_v2i64_ty], [llvm_i32_ty],
                                    [IntrNoMem, ImmArg<ArgIndex<0>>]>;
def int_loongarch_lsx_vrepli_b : VecInt<[llvm_v16i8_ty], [llvm_i32_ty],
                                        [IntrNoMem, ImmArg<ArgIndex<0>>]>;
def int_loongarch_lsx_vrepli_h : VecInt<[llvm_v8i16_ty], [llvm_i32_ty],
                                        [IntrNoMem, ImmArg<ArgIndex<0>>]>;
def int_loongarch_lsx_vrepli_w : VecInt<[llvm_v4i32_ty], [llvm_i32_ty],
                                        [IntrNoMem, ImmArg<ArgIndex<0>>]>;
def int_loongarch_lsx_vrepli_d : VecInt<[llvm_v2i64_ty], [llvm_i32_ty],
                                        [IntrNoMem, ImmArg<ArgIndex<0>>]>;

def int_loongarch_lsx_vreplgr2vr_b : VecInt<[llvm_v16i8_ty], [llvm_i32_ty],
                                            [IntrNoMem]>;
def int_loongarch_lsx_vreplgr2vr_h : VecInt<[llvm_v8i16_ty], [llvm_i32_ty],
                                            [IntrNoMem]>;
def int_loongarch_lsx_vreplgr2vr_w : VecInt<[llvm_v4i32_ty], [llvm_i32_ty],
                                            [IntrNoMem]>;
def int_loongarch_lsx_vreplgr2vr_d : VecInt<[llvm_v2i64_ty], [llvm_i64_ty],
                                            [IntrNoMem]>;

def int_loongarch_lsx_vinsgr2vr_b
  : VecInt<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty, llvm_i32_ty],
           [IntrNoMem, ImmArg<ArgIndex<2>>]>;
def int_loongarch_lsx_vinsgr2vr_h
  : VecInt<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_i32_ty, llvm_i32_ty],
           [IntrNoMem, ImmArg<ArgIndex<2>>]>;
def int_loongarch_lsx_vinsgr2vr_w
  : VecInt<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_i32_ty, llvm_i32_ty],
           [IntrNoMem, ImmArg<ArgIndex<2>>]>;
def int_loongarch_lsx_vinsgr2vr_d
  : VecInt<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_i64_ty, llvm_i32_ty],
           [IntrNoMem, ImmArg<ArgIndex<2>>]>;

def int_loongarch_lsx_vreplve_b
  : VecInt<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem]>;
def int_loongarch_lsx_vreplve_h
  : VecInt<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_i32_ty], [IntrNoMem]>;
def int_loongarch_lsx_vreplve_w
  : VecInt<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_loongarch_lsx_vreplve_d
  : VecInt<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_i32_ty], [IntrNoMem]>;

foreach inst = ["vpickve2gr_b", "vpickve2gr_bu" ] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_i32_ty],
                                       [llvm_v16i8_ty, llvm_i32_ty],
                                       [IntrNoMem, ImmArg<ArgIndex<1>>]>;
foreach inst = ["vpickve2gr_h", "vpickve2gr_hu" ] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_i32_ty],
                                       [llvm_v8i16_ty, llvm_i32_ty],
                                       [IntrNoMem, ImmArg<ArgIndex<1>>]>;
foreach inst = ["vpickve2gr_w", "vpickve2gr_wu" ] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_i32_ty],
                                       [llvm_v4i32_ty, llvm_i32_ty],
                                       [IntrNoMem, ImmArg<ArgIndex<1>>]>;
foreach inst = ["vpickve2gr_d", "vpickve2gr_du" ] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_i64_ty],
                                       [llvm_v2i64_ty, llvm_i32_ty],
                                       [IntrNoMem, ImmArg<ArgIndex<1>>]>;

def int_loongarch_lsx_bz_b : VecInt<[llvm_i32_ty], [llvm_v16i8_ty],
                                    [IntrNoMem]>;
def int_loongarch_lsx_bz_h : VecInt<[llvm_i32_ty], [llvm_v8i16_ty],
                                    [IntrNoMem]>;
def int_loongarch_lsx_bz_w : VecInt<[llvm_i32_ty], [llvm_v4i32_ty],
                                    [IntrNoMem]>;
def int_loongarch_lsx_bz_d : VecInt<[llvm_i32_ty], [llvm_v2i64_ty],
                                    [IntrNoMem]>;
def int_loongarch_lsx_bz_v : VecInt<[llvm_i32_ty], [llvm_v16i8_ty],
                                    [IntrNoMem]>;

def int_loongarch_lsx_bnz_v : VecInt<[llvm_i32_ty], [llvm_v16i8_ty],
                                     [IntrNoMem]>;
def int_loongarch_lsx_bnz_b : VecInt<[llvm_i32_ty], [llvm_v16i8_ty],
                                     [IntrNoMem]>;
def int_loongarch_lsx_bnz_h : VecInt<[llvm_i32_ty], [llvm_v8i16_ty],
                                     [IntrNoMem]>;
def int_loongarch_lsx_bnz_w : VecInt<[llvm_i32_ty], [llvm_v4i32_ty],
                                     [IntrNoMem]>;
def int_loongarch_lsx_bnz_d : VecInt<[llvm_i32_ty], [llvm_v2i64_ty],
                                     [IntrNoMem]>;

// LSX Float

foreach inst = ["vfadd_s", "vfsub_s", "vfmul_s", "vfdiv_s",
                "vfmax_s", "vfmin_s", "vfmaxa_s", "vfmina_s"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v4f32_ty],
                                       [llvm_v4f32_ty, llvm_v4f32_ty],
                                       [IntrNoMem]>;
foreach inst = ["vfadd_d", "vfsub_d", "vfmul_d", "vfdiv_d",
                "vfmax_d", "vfmin_d", "vfmaxa_d", "vfmina_d"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v2f64_ty],
                                       [llvm_v2f64_ty, llvm_v2f64_ty],
                                       [IntrNoMem]>;

foreach inst = ["vfmadd_s", "vfmsub_s", "vfnmadd_s", "vfnmsub_s"] in
  def int_loongarch_lsx_#inst
    : VecInt<[llvm_v4f32_ty],
             [llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4f32_ty],
             [IntrNoMem]>;
foreach inst = ["vfmadd_d", "vfmsub_d", "vfnmadd_d", "vfnmsub_d"] in
  def int_loongarch_lsx_#inst
    : VecInt<[llvm_v2f64_ty],
             [llvm_v2f64_ty, llvm_v2f64_ty, llvm_v2f64_ty],
             [IntrNoMem]>;

foreach inst = ["vflogb_s", "vfsqrt_s", "vfrecip_s", "vfrsqrt_s", "vfrint_s",
                "vfrecipe_s", "vfrsqrte_s",
                "vfrintrne_s", "vfrintrz_s", "vfrintrp_s", "vfrintrm_s"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v4f32_ty], [llvm_v4f32_ty],
                                       [IntrNoMem]>;
foreach inst = ["vflogb_d", "vfsqrt_d", "vfrecip_d", "vfrsqrt_d", "vfrint_d",
                "vfrecipe_d", "vfrsqrte_d",
                "vfrintrne_d", "vfrintrz_d", "vfrintrp_d", "vfrintrm_d"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v2f64_ty], [llvm_v2f64_ty],
                                       [IntrNoMem]>;

foreach inst = ["vfcvtl_s_h", "vfcvth_s_h"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v4f32_ty], [llvm_v8i16_ty],
                                       [IntrNoMem]>;
foreach inst = ["vfcvtl_d_s", "vfcvth_d_s"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v2f64_ty], [llvm_v4f32_ty],
                                       [IntrNoMem]>;

foreach inst = ["vftintrne_w_s", "vftintrz_w_s", "vftintrp_w_s", "vftintrm_w_s",
                "vftint_w_s", "vftintrz_wu_s", "vftint_wu_s", "vfclass_s"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v4i32_ty], [llvm_v4f32_ty],
                                       [IntrNoMem]>;
foreach inst = ["vftintrne_l_d", "vftintrz_l_d", "vftintrp_l_d", "vftintrm_l_d",
                "vftint_l_d", "vftintrz_lu_d", "vftint_lu_d", "vfclass_d"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v2i64_ty], [llvm_v2f64_ty],
                                       [IntrNoMem]>;

foreach inst = ["vftintrnel_l_s", "vftintrneh_l_s", "vftintrzl_l_s",
                "vftintrzh_l_s", "vftintrpl_l_s", "vftintrph_l_s",
                "vftintrml_l_s", "vftintrmh_l_s", "vftintl_l_s",
                "vftinth_l_s"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v2i64_ty], [llvm_v4f32_ty],
                                       [IntrNoMem]>;

foreach inst = ["vffint_s_w", "vffint_s_wu"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v4f32_ty], [llvm_v4i32_ty],
                                       [IntrNoMem]>;
foreach inst = ["vffint_d_l", "vffint_d_lu"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v2f64_ty], [llvm_v2i64_ty],
                                       [IntrNoMem]>;

foreach inst = ["vffintl_d_w", "vffinth_d_w"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v2f64_ty], [llvm_v4i32_ty],
                                       [IntrNoMem]>;

foreach inst = ["vffint_s_l"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v4f32_ty],
                                       [llvm_v2i64_ty, llvm_v2i64_ty],
                                       [IntrNoMem]>;
foreach inst = ["vftintrne_w_d", "vftintrz_w_d", "vftintrp_w_d", "vftintrm_w_d",
                "vftint_w_d"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v4i32_ty],
                                       [llvm_v2f64_ty, llvm_v2f64_ty],
                                       [IntrNoMem]>;

foreach inst = ["vfcvt_h_s"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v8i16_ty],
                                       [llvm_v4f32_ty, llvm_v4f32_ty],
                                       [IntrNoMem]>;
foreach inst = ["vfcvt_s_d"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v4f32_ty],
                                       [llvm_v2f64_ty, llvm_v2f64_ty],
                                       [IntrNoMem]>;

foreach inst = ["vfcmp_caf_s", "vfcmp_cun_s", "vfcmp_ceq_s", "vfcmp_cueq_s",
                "vfcmp_clt_s", "vfcmp_cult_s", "vfcmp_cle_s", "vfcmp_cule_s",
                "vfcmp_cne_s", "vfcmp_cor_s", "vfcmp_cune_s",
                "vfcmp_saf_s", "vfcmp_sun_s", "vfcmp_seq_s", "vfcmp_sueq_s",
                "vfcmp_slt_s", "vfcmp_sult_s", "vfcmp_sle_s", "vfcmp_sule_s",
                "vfcmp_sne_s", "vfcmp_sor_s", "vfcmp_sune_s"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v4i32_ty],
                                       [llvm_v4f32_ty, llvm_v4f32_ty],
                                       [IntrNoMem]>;
foreach inst = ["vfcmp_caf_d", "vfcmp_cun_d", "vfcmp_ceq_d", "vfcmp_cueq_d",
                "vfcmp_clt_d", "vfcmp_cult_d", "vfcmp_cle_d", "vfcmp_cule_d",
                "vfcmp_cne_d", "vfcmp_cor_d", "vfcmp_cune_d",
                "vfcmp_saf_d", "vfcmp_sun_d", "vfcmp_seq_d", "vfcmp_sueq_d",
                "vfcmp_slt_d", "vfcmp_sult_d", "vfcmp_sle_d", "vfcmp_sule_d",
                "vfcmp_sne_d", "vfcmp_sor_d", "vfcmp_sune_d"] in
  def int_loongarch_lsx_#inst : VecInt<[llvm_v2i64_ty],
                                       [llvm_v2f64_ty, llvm_v2f64_ty],
                                       [IntrNoMem]>;

// LSX load/store
def int_loongarch_lsx_vld
  : VecInt<[llvm_v16i8_ty], [llvm_ptr_ty, llvm_i32_ty],
           [IntrReadMem, IntrArgMemOnly, ImmArg<ArgIndex<1>>]>;
def int_loongarch_lsx_vldx
  : VecInt<[llvm_v16i8_ty], [llvm_ptr_ty, llvm_i64_ty],
           [IntrReadMem, IntrArgMemOnly]>;
def int_loongarch_lsx_vldrepl_b
  : VecInt<[llvm_v16i8_ty], [llvm_ptr_ty, llvm_i32_ty],
           [IntrReadMem, IntrArgMemOnly, ImmArg<ArgIndex<1>>]>;
def int_loongarch_lsx_vldrepl_h
  : VecInt<[llvm_v8i16_ty], [llvm_ptr_ty, llvm_i32_ty],
           [IntrReadMem, IntrArgMemOnly, ImmArg<ArgIndex<1>>]>;
def int_loongarch_lsx_vldrepl_w
  : VecInt<[llvm_v4i32_ty], [llvm_ptr_ty, llvm_i32_ty],
           [IntrReadMem, IntrArgMemOnly, ImmArg<ArgIndex<1>>]>;
def int_loongarch_lsx_vldrepl_d
  : VecInt<[llvm_v2i64_ty], [llvm_ptr_ty, llvm_i32_ty],
           [IntrReadMem, IntrArgMemOnly, ImmArg<ArgIndex<1>>]>;

def int_loongarch_lsx_vst
  : VecInt<[], [llvm_v16i8_ty, llvm_ptr_ty, llvm_i32_ty],
           [IntrWriteMem, IntrArgMemOnly, ImmArg<ArgIndex<2>>]>;
def int_loongarch_lsx_vstx
  : VecInt<[], [llvm_v16i8_ty, llvm_ptr_ty, llvm_i64_ty],
           [IntrWriteMem, IntrArgMemOnly]>;
def int_loongarch_lsx_vstelm_b
  : VecInt<[], [llvm_v16i8_ty, llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty],
           [IntrWriteMem, IntrArgMemOnly, ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<3>>]>;
def int_loongarch_lsx_vstelm_h
  : VecInt<[], [llvm_v8i16_ty, llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty],
           [IntrWriteMem, IntrArgMemOnly, ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<3>>]>;
def int_loongarch_lsx_vstelm_w
  : VecInt<[], [llvm_v4i32_ty, llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty],
           [IntrWriteMem, IntrArgMemOnly, ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<3>>]>;
def int_loongarch_lsx_vstelm_d
  : VecInt<[], [llvm_v2i64_ty, llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty],
           [IntrWriteMem, IntrArgMemOnly, ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<3>>]>;

} // TargetPrefix = "loongarch"

//===----------------------------------------------------------------------===//
// LASX

let TargetPrefix = "loongarch" in {
foreach inst = ["xvadd_b", "xvsub_b",
                "xvsadd_b", "xvsadd_bu", "xvssub_b", "xvssub_bu",
                "xvavg_b", "xvavg_bu", "xvavgr_b", "xvavgr_bu",
                "xvabsd_b", "xvabsd_bu", "xvadda_b",
                "xvmax_b", "xvmax_bu", "xvmin_b", "xvmin_bu",
                "xvmul_b", "xvmuh_b", "xvmuh_bu",
                "xvdiv_b", "xvdiv_bu", "xvmod_b", "xvmod_bu", "xvsigncov_b",
                "xvand_v", "xvor_v", "xvxor_v", "xvnor_v", "xvandn_v", "xvorn_v",
                "xvsll_b", "xvsrl_b", "xvsra_b", "xvrotr_b", "xvsrlr_b", "xvsrar_b",
                "xvbitclr_b", "xvbitset_b", "xvbitrev_b",
                "xvseq_b", "xvsle_b", "xvsle_bu", "xvslt_b", "xvslt_bu",
                "xvpackev_b", "xvpackod_b", "xvpickev_b", "xvpickod_b",
                "xvilvl_b", "xvilvh_b"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v32i8_ty],
                                        [llvm_v32i8_ty, llvm_v32i8_ty],
                                        [IntrNoMem]>;

foreach inst = ["xvadd_h", "xvsub_h",
                "xvsadd_h", "xvsadd_hu", "xvssub_h", "xvssub_hu",
                "xvavg_h", "xvavg_hu", "xvavgr_h", "xvavgr_hu",
                "xvabsd_h", "xvabsd_hu", "xvadda_h",
                "xvmax_h", "xvmax_hu", "xvmin_h", "xvmin_hu",
                "xvmul_h", "xvmuh_h", "xvmuh_hu",
                "xvdiv_h", "xvdiv_hu", "xvmod_h", "xvmod_hu", "xvsigncov_h",
                "xvsll_h", "xvsrl_h", "xvsra_h", "xvrotr_h", "xvsrlr_h", "xvsrar_h",
                "xvbitclr_h", "xvbitset_h", "xvbitrev_h",
                "xvseq_h", "xvsle_h", "xvsle_hu", "xvslt_h", "xvslt_hu",
                "xvpackev_h", "xvpackod_h", "xvpickev_h", "xvpickod_h",
                "xvilvl_h", "xvilvh_h"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v16i16_ty],
                                        [llvm_v16i16_ty, llvm_v16i16_ty],
                                        [IntrNoMem]>;

foreach inst = ["xvadd_w", "xvsub_w",
                "xvsadd_w", "xvsadd_wu", "xvssub_w", "xvssub_wu",
                "xvavg_w", "xvavg_wu", "xvavgr_w", "xvavgr_wu",
                "xvabsd_w", "xvabsd_wu", "xvadda_w",
                "xvmax_w", "xvmax_wu", "xvmin_w", "xvmin_wu",
                "xvmul_w", "xvmuh_w", "xvmuh_wu",
                "xvdiv_w", "xvdiv_wu", "xvmod_w", "xvmod_wu", "xvsigncov_w",
                "xvsll_w", "xvsrl_w", "xvsra_w", "xvrotr_w", "xvsrlr_w", "xvsrar_w",
                "xvbitclr_w", "xvbitset_w", "xvbitrev_w",
                "xvseq_w", "xvsle_w", "xvsle_wu", "xvslt_w", "xvslt_wu",
                "xvpackev_w", "xvpackod_w", "xvpickev_w", "xvpickod_w",
                "xvilvl_w", "xvilvh_w", "xvperm_w"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v8i32_ty],
                                        [llvm_v8i32_ty, llvm_v8i32_ty],
                                        [IntrNoMem]>;

foreach inst = ["xvadd_d", "xvadd_q", "xvsub_d", "xvsub_q",
                "xvsadd_d", "xvsadd_du", "xvssub_d", "xvssub_du",
                "xvhaddw_q_d", "xvhaddw_qu_du", "xvhsubw_q_d", "xvhsubw_qu_du",
                "xvaddwev_q_d", "xvaddwod_q_d", "xvsubwev_q_d", "xvsubwod_q_d",
                "xvaddwev_q_du", "xvaddwod_q_du", "xvsubwev_q_du", "xvsubwod_q_du",
                "xvaddwev_q_du_d", "xvaddwod_q_du_d",
                "xvavg_d", "xvavg_du", "xvavgr_d", "xvavgr_du",
                "xvabsd_d", "xvabsd_du", "xvadda_d",
                "xvmax_d", "xvmax_du", "xvmin_d", "xvmin_du",
                "xvmul_d", "xvmuh_d", "xvmuh_du",
                "xvmulwev_q_d", "xvmulwod_q_d", "xvmulwev_q_du", "xvmulwod_q_du",
                "xvmulwev_q_du_d", "xvmulwod_q_du_d",
                "xvdiv_d", "xvdiv_du", "xvmod_d", "xvmod_du", "xvsigncov_d",
                "xvsll_d", "xvsrl_d", "xvsra_d", "xvrotr_d", "xvsrlr_d", "xvsrar_d",
                "xvbitclr_d", "xvbitset_d", "xvbitrev_d",
                "xvseq_d", "xvsle_d", "xvsle_du", "xvslt_d", "xvslt_du",
                "xvpackev_d", "xvpackod_d", "xvpickev_d", "xvpickod_d",
                "xvilvl_d", "xvilvh_d"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v4i64_ty],
                                        [llvm_v4i64_ty, llvm_v4i64_ty],
                                        [IntrNoMem]>;

foreach inst = ["xvaddi_bu", "xvsubi_bu",
                "xvmaxi_b", "xvmaxi_bu", "xvmini_b", "xvmini_bu",
                "xvsat_b", "xvsat_bu",
                "xvandi_b", "xvori_b", "xvxori_b", "xvnori_b",
                "xvslli_b", "xvsrli_b", "xvsrai_b", "xvrotri_b",
                "xvsrlri_b", "xvsrari_b",
                "xvbitclri_b", "xvbitseti_b", "xvbitrevi_b",
                "xvseqi_b", "xvslei_b", "xvslei_bu", "xvslti_b", "xvslti_bu",
                "xvrepl128vei_b", "xvbsll_v", "xvbsrl_v", "xvshuf4i_b"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v32i8_ty],
                                        [llvm_v32i8_ty, llvm_i32_ty],
                                        [IntrNoMem, ImmArg<ArgIndex<1>>]>;
foreach inst = ["xvaddi_hu", "xvsubi_hu",
                "xvmaxi_h", "xvmaxi_hu", "xvmini_h", "xvmini_hu",
                "xvsat_h", "xvsat_hu",
                "xvslli_h", "xvsrli_h", "xvsrai_h", "xvrotri_h",
                "xvsrlri_h", "xvsrari_h",
                "xvbitclri_h", "xvbitseti_h", "xvbitrevi_h",
                "xvseqi_h", "xvslei_h", "xvslei_hu", "xvslti_h", "xvslti_hu",
                "xvrepl128vei_h", "xvshuf4i_h"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v16i16_ty],
                                        [llvm_v16i16_ty, llvm_i32_ty],
                                        [IntrNoMem, ImmArg<ArgIndex<1>>]>;
foreach inst = ["xvaddi_wu", "xvsubi_wu",
                "xvmaxi_w", "xvmaxi_wu", "xvmini_w", "xvmini_wu",
                "xvsat_w", "xvsat_wu",
                "xvslli_w", "xvsrli_w", "xvsrai_w", "xvrotri_w",
                "xvsrlri_w", "xvsrari_w",
                "xvbitclri_w", "xvbitseti_w", "xvbitrevi_w",
                "xvseqi_w", "xvslei_w", "xvslei_wu", "xvslti_w", "xvslti_wu",
                "xvrepl128vei_w", "xvshuf4i_w", "xvpickve_w"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v8i32_ty],
                                        [llvm_v8i32_ty, llvm_i32_ty],
                                        [IntrNoMem, ImmArg<ArgIndex<1>>]>;
foreach inst = ["xvaddi_du", "xvsubi_du",
                "xvmaxi_d", "xvmaxi_du", "xvmini_d", "xvmini_du",
                "xvsat_d", "xvsat_du",
                "xvslli_d", "xvsrli_d", "xvsrai_d", "xvrotri_d",
                "xvsrlri_d", "xvsrari_d",
                "xvbitclri_d", "xvbitseti_d", "xvbitrevi_d",
                "xvseqi_d", "xvslei_d", "xvslei_du", "xvslti_d", "xvslti_du",
                "xvrepl128vei_d", "xvpermi_d", "xvpickve_d"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v4i64_ty],
                                        [llvm_v4i64_ty, llvm_i32_ty],
                                        [IntrNoMem, ImmArg<ArgIndex<1>>]>;

foreach inst = ["xvhaddw_h_b", "xvhaddw_hu_bu", "xvhsubw_h_b", "xvhsubw_hu_bu",
                "xvaddwev_h_b", "xvaddwod_h_b", "xvsubwev_h_b", "xvsubwod_h_b",
                "xvaddwev_h_bu", "xvaddwod_h_bu", "xvsubwev_h_bu", "xvsubwod_h_bu",
                "xvaddwev_h_bu_b", "xvaddwod_h_bu_b",
                "xvmulwev_h_b", "xvmulwod_h_b", "xvmulwev_h_bu", "xvmulwod_h_bu",
                "xvmulwev_h_bu_b", "xvmulwod_h_bu_b"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v16i16_ty],
                                        [llvm_v32i8_ty, llvm_v32i8_ty],
                                        [IntrNoMem]>;

foreach inst = ["xvhaddw_w_h", "xvhaddw_wu_hu", "xvhsubw_w_h", "xvhsubw_wu_hu",
                "xvaddwev_w_h", "xvaddwod_w_h", "xvsubwev_w_h", "xvsubwod_w_h",
                "xvaddwev_w_hu", "xvaddwod_w_hu", "xvsubwev_w_hu", "xvsubwod_w_hu",
                "xvaddwev_w_hu_h", "xvaddwod_w_hu_h",
                "xvmulwev_w_h", "xvmulwod_w_h", "xvmulwev_w_hu", "xvmulwod_w_hu",
                "xvmulwev_w_hu_h", "xvmulwod_w_hu_h"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v8i32_ty],
                                        [llvm_v16i16_ty, llvm_v16i16_ty],
                                        [IntrNoMem]>;

foreach inst = ["xvhaddw_d_w", "xvhaddw_du_wu", "xvhsubw_d_w", "xvhsubw_du_wu",
                "xvaddwev_d_w", "xvaddwod_d_w", "xvsubwev_d_w", "xvsubwod_d_w",
                "xvaddwev_d_wu", "xvaddwod_d_wu", "xvsubwev_d_wu", "xvsubwod_d_wu",
                "xvaddwev_d_wu_w", "xvaddwod_d_wu_w",
                "xvmulwev_d_w", "xvmulwod_d_w", "xvmulwev_d_wu", "xvmulwod_d_wu",
                "xvmulwev_d_wu_w", "xvmulwod_d_wu_w"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v4i64_ty],
                                        [llvm_v8i32_ty, llvm_v8i32_ty],
                                        [IntrNoMem]>;

foreach inst = ["xvsrln_b_h", "xvsran_b_h", "xvsrlrn_b_h", "xvsrarn_b_h",
                "xvssrln_b_h", "xvssran_b_h", "xvssrln_bu_h", "xvssran_bu_h",
                "xvssrlrn_b_h", "xvssrarn_b_h", "xvssrlrn_bu_h", "xvssrarn_bu_h"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v32i8_ty],
                                        [llvm_v16i16_ty, llvm_v16i16_ty],
                                        [IntrNoMem]>;

foreach inst = ["xvsrln_h_w", "xvsran_h_w", "xvsrlrn_h_w", "xvsrarn_h_w",
                "xvssrln_h_w", "xvssran_h_w", "xvssrln_hu_w", "xvssran_hu_w",
                "xvssrlrn_h_w", "xvssrarn_h_w", "xvssrlrn_hu_w", "xvssrarn_hu_w"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v16i16_ty],
                                        [llvm_v8i32_ty, llvm_v8i32_ty],
                                        [IntrNoMem]>;

foreach inst = ["xvsrln_w_d", "xvsran_w_d", "xvsrlrn_w_d", "xvsrarn_w_d",
                "xvssrln_w_d", "xvssran_w_d", "xvssrln_wu_d", "xvssran_wu_d",
                "xvssrlrn_w_d", "xvssrarn_w_d", "xvssrlrn_wu_d", "xvssrarn_wu_d"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v8i32_ty],
                                        [llvm_v4i64_ty, llvm_v4i64_ty],
                                        [IntrNoMem]>;

foreach inst = ["xvmadd_b", "xvmsub_b", "xvfrstp_b", "xvbitsel_v", "xvshuf_b"] in
  def int_loongarch_lasx_#inst
    : VecInt<[llvm_v32i8_ty],
             [llvm_v32i8_ty, llvm_v32i8_ty, llvm_v32i8_ty],
             [IntrNoMem]>;
foreach inst = ["xvmadd_h", "xvmsub_h", "xvfrstp_h", "xvshuf_h"] in
  def int_loongarch_lasx_#inst
    : VecInt<[llvm_v16i16_ty],
             [llvm_v16i16_ty, llvm_v16i16_ty, llvm_v16i16_ty],
             [IntrNoMem]>;
foreach inst = ["xvmadd_w", "xvmsub_w", "xvshuf_w"] in
  def int_loongarch_lasx_#inst
    : VecInt<[llvm_v8i32_ty],
             [llvm_v8i32_ty, llvm_v8i32_ty, llvm_v8i32_ty],
             [IntrNoMem]>;
foreach inst = ["xvmadd_d", "xvmsub_d", "xvshuf_d"] in
  def int_loongarch_lasx_#inst
    : VecInt<[llvm_v4i64_ty],
             [llvm_v4i64_ty, llvm_v4i64_ty, llvm_v4i64_ty],
             [IntrNoMem]>;

foreach inst = ["xvsrlni_b_h", "xvsrani_b_h", "xvsrlrni_b_h", "xvsrarni_b_h",
                "xvssrlni_b_h", "xvssrani_b_h", "xvssrlni_bu_h", "xvssrani_bu_h",
                "xvssrlrni_b_h", "xvssrarni_b_h", "xvssrlrni_bu_h", "xvssrarni_bu_h",
                "xvfrstpi_b", "xvbitseli_b", "xvextrins_b", "xvpermi_q"] in
  def int_loongarch_lasx_#inst
    : VecInt<[llvm_v32i8_ty],
             [llvm_v32i8_ty, llvm_v32i8_ty, llvm_i32_ty],
             [IntrNoMem, ImmArg<ArgIndex<2>>]>;
foreach inst = ["xvsrlni_h_w", "xvsrani_h_w", "xvsrlrni_h_w", "xvsrarni_h_w",
                "xvssrlni_h_w", "xvssrani_h_w", "xvssrlni_hu_w", "xvssrani_hu_w",
                "xvssrlrni_h_w", "xvssrarni_h_w", "xvssrlrni_hu_w", "xvssrarni_hu_w",
                "xvfrstpi_h", "xvextrins_h"] in
  def int_loongarch_lasx_#inst
    : VecInt<[llvm_v16i16_ty],
             [llvm_v16i16_ty, llvm_v16i16_ty, llvm_i32_ty],
             [IntrNoMem, ImmArg<ArgIndex<2>>]>;
foreach inst = ["xvsrlni_w_d", "xvsrani_w_d", "xvsrlrni_w_d", "xvsrarni_w_d",
                "xvssrlni_w_d", "xvssrani_w_d", "xvssrlni_wu_d", "xvssrani_wu_d",
                "xvssrlrni_w_d", "xvssrarni_w_d", "xvssrlrni_wu_d", "xvssrarni_wu_d",
                "xvpermi_w", "xvextrins_w", "xvinsve0_w"] in
  def int_loongarch_lasx_#inst
    : VecInt<[llvm_v8i32_ty],
             [llvm_v8i32_ty, llvm_v8i32_ty, llvm_i32_ty],
             [IntrNoMem, ImmArg<ArgIndex<2>>]>;
foreach inst = ["xvsrlni_d_q", "xvsrani_d_q", "xvsrlrni_d_q", "xvsrarni_d_q",
                "xvssrlni_d_q", "xvssrani_d_q", "xvssrlni_du_q", "xvssrani_du_q",
                "xvssrlrni_d_q", "xvssrarni_d_q", "xvssrlrni_du_q", "xvssrarni_du_q",
                "xvshuf4i_d", "xvextrins_d", "xvinsve0_d"] in
  def int_loongarch_lasx_#inst
    : VecInt<[llvm_v4i64_ty],
             [llvm_v4i64_ty, llvm_v4i64_ty, llvm_i32_ty],
             [IntrNoMem, ImmArg<ArgIndex<2>>]>;

foreach inst = ["xvmaddwev_h_b", "xvmaddwod_h_b", "xvmaddwev_h_bu",
                "xvmaddwod_h_bu", "xvmaddwev_h_bu_b", "xvmaddwod_h_bu_b"] in
  def int_loongarch_lasx_#inst
    : VecInt<[llvm_v16i16_ty],
             [llvm_v16i16_ty, llvm_v32i8_ty, llvm_v32i8_ty],
             [IntrNoMem]>;
foreach inst = ["xvmaddwev_w_h", "xvmaddwod_w_h", "xvmaddwev_w_hu",
                "xvmaddwod_w_hu", "xvmaddwev_w_hu_h", "xvmaddwod_w_hu_h"] in
  def int_loongarch_lasx_#inst
    : VecInt<[llvm_v8i32_ty],
             [llvm_v8i32_ty, llvm_v16i16_ty, llvm_v16i16_ty],
             [IntrNoMem]>;
foreach inst = ["xvmaddwev_d_w", "xvmaddwod_d_w", "xvmaddwev_d_wu",
                "xvmaddwod_d_wu", "xvmaddwev_d_wu_w", "xvmaddwod_d_wu_w"] in
  def int_loongarch_lasx_#inst
    : VecInt<[llvm_v4i64_ty],
             [llvm_v4i64_ty, llvm_v8i32_ty, llvm_v8i32_ty],
             [IntrNoMem]>;
foreach inst = ["xvmaddwev_q_d", "xvmaddwod_q_d", "xvmaddwev_q_du",
                "xvmaddwod_q_du", "xvmaddwev_q_du_d", "xvmaddwod_q_du_d"] in
  def int_loongarch_lasx_#inst
    : VecInt<[llvm_v4i64_ty],
             [llvm_v4i64_ty, llvm_v4i64_ty, llvm_v4i64_ty],
             [IntrNoMem]>;

foreach inst = ["xvsllwil_h_b", "xvsllwil_hu_bu"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v16i16_ty],
                                        [llvm_v32i8_ty, llvm_i32_ty],
                                        [IntrNoMem, ImmArg<ArgIndex<1>>]>;
foreach inst = ["xvsllwil_w_h", "xvsllwil_wu_hu"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v8i32_ty],
                                        [llvm_v16i16_ty, llvm_i32_ty],
                                        [IntrNoMem, ImmArg<ArgIndex<1>>]>;
foreach inst = ["xvsllwil_d_w", "xvsllwil_du_wu"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v4i64_ty],
                                        [llvm_v8i32_ty, llvm_i32_ty],
                                        [IntrNoMem, ImmArg<ArgIndex<1>>]>;

foreach inst = ["xvneg_b", "xvmskltz_b", "xvmskgez_b", "xvmsknz_b",
                "xvclo_b", "xvclz_b", "xvpcnt_b",
                "xvreplve0_b", "xvreplve0_q"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v32i8_ty], [llvm_v32i8_ty],
                                        [IntrNoMem]>;
foreach inst = ["xvneg_h", "xvmskltz_h", "xvclo_h", "xvclz_h", "xvpcnt_h",
                "xvreplve0_h"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v16i16_ty], [llvm_v16i16_ty],
                                        [IntrNoMem]>;
foreach inst = ["xvneg_w", "xvmskltz_w", "xvclo_w", "xvclz_w", "xvpcnt_w",
                "xvreplve0_w"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v8i32_ty], [llvm_v8i32_ty],
                                        [IntrNoMem]>;
foreach inst = ["xvneg_d", "xvexth_q_d", "xvexth_qu_du", "xvmskltz_d",
                "xvextl_q_d", "xvextl_qu_du", "xvclo_d", "xvclz_d", "xvpcnt_d",
                "xvreplve0_d"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v4i64_ty], [llvm_v4i64_ty],
                                        [IntrNoMem]>;

foreach inst = ["xvexth_h_b", "xvexth_hu_bu", "vext2xv_h_b", "vext2xv_hu_bu"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v16i16_ty], [llvm_v32i8_ty],
                                        [IntrNoMem]>;
foreach inst = ["xvexth_w_h", "xvexth_wu_hu", "vext2xv_w_h", "vext2xv_wu_hu"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v8i32_ty], [llvm_v16i16_ty],
                                        [IntrNoMem]>;
foreach inst = ["xvexth_d_w", "xvexth_du_wu", "vext2xv_d_w", "vext2xv_du_wu"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v4i64_ty], [llvm_v8i32_ty],
                                        [IntrNoMem]>;

foreach inst = ["vext2xv_w_b", "vext2xv_wu_bu"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v8i32_ty], [llvm_v32i8_ty],
                                        [IntrNoMem]>;
foreach inst = ["vext2xv_d_h", "vext2xv_du_hu"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v4i64_ty], [llvm_v16i16_ty],
                                        [IntrNoMem]>;

foreach inst = ["vext2xv_d_b", "vext2xv_du_bu"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v4i64_ty], [llvm_v32i8_ty],
                                        [IntrNoMem]>;

def int_loongarch_lasx_xvldi : VecInt<[llvm_v4i64_ty], [llvm_i32_ty],
                                      [IntrNoMem, ImmArg<ArgIndex<0>>]>;
def int_loongarch_lasx_xvrepli_b : VecInt<[llvm_v32i8_ty], [llvm_i32_ty],
                                          [IntrNoMem, ImmArg<ArgIndex<0>>]>;
def int_loongarch_lasx_xvrepli_h : VecInt<[llvm_v16i16_ty], [llvm_i32_ty],
                                          [IntrNoMem, ImmArg<ArgIndex<0>>]>;
def int_loongarch_lasx_xvrepli_w : VecInt<[llvm_v8i32_ty], [llvm_i32_ty],
                                          [IntrNoMem, ImmArg<ArgIndex<0>>]>;
def int_loongarch_lasx_xvrepli_d : VecInt<[llvm_v4i64_ty], [llvm_i32_ty],
                                          [IntrNoMem, ImmArg<ArgIndex<0>>]>;

def int_loongarch_lasx_xvreplgr2vr_b : VecInt<[llvm_v32i8_ty], [llvm_i32_ty],
                                             [IntrNoMem]>;
def int_loongarch_lasx_xvreplgr2vr_h : VecInt<[llvm_v16i16_ty], [llvm_i32_ty],
                                             [IntrNoMem]>;
def int_loongarch_lasx_xvreplgr2vr_w : VecInt<[llvm_v8i32_ty], [llvm_i32_ty],
                                             [IntrNoMem]>;
def int_loongarch_lasx_xvreplgr2vr_d : VecInt<[llvm_v4i64_ty], [llvm_i64_ty],
                                             [IntrNoMem]>;

def int_loongarch_lasx_xvinsgr2vr_w
  : VecInt<[llvm_v8i32_ty], [llvm_v8i32_ty, llvm_i32_ty, llvm_i32_ty],
           [IntrNoMem, ImmArg<ArgIndex<2>>]>;
def int_loongarch_lasx_xvinsgr2vr_d
  : VecInt<[llvm_v4i64_ty], [llvm_v4i64_ty, llvm_i64_ty, llvm_i32_ty],
           [IntrNoMem, ImmArg<ArgIndex<2>>]>;

def int_loongarch_lasx_xvreplve_b
  : VecInt<[llvm_v32i8_ty], [llvm_v32i8_ty, llvm_i32_ty], [IntrNoMem]>;
def int_loongarch_lasx_xvreplve_h
  : VecInt<[llvm_v16i16_ty], [llvm_v16i16_ty, llvm_i32_ty], [IntrNoMem]>;
def int_loongarch_lasx_xvreplve_w
  : VecInt<[llvm_v8i32_ty], [llvm_v8i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_loongarch_lasx_xvreplve_d
  : VecInt<[llvm_v4i64_ty], [llvm_v4i64_ty, llvm_i32_ty], [IntrNoMem]>;

foreach inst = ["xvpickve2gr_w", "xvpickve2gr_wu" ] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_i32_ty],
                                        [llvm_v8i32_ty, llvm_i32_ty],
                                        [IntrNoMem, ImmArg<ArgIndex<1>>]>;
foreach inst = ["xvpickve2gr_d", "xvpickve2gr_du" ] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_i64_ty],
                                        [llvm_v4i64_ty, llvm_i32_ty],
                                        [IntrNoMem, ImmArg<ArgIndex<1>>]>;

def int_loongarch_lasx_xbz_b : VecInt<[llvm_i32_ty], [llvm_v32i8_ty],
                                      [IntrNoMem]>;
def int_loongarch_lasx_xbz_h : VecInt<[llvm_i32_ty], [llvm_v16i16_ty],
                                      [IntrNoMem]>;
def int_loongarch_lasx_xbz_w : VecInt<[llvm_i32_ty], [llvm_v8i32_ty],
                                      [IntrNoMem]>;
def int_loongarch_lasx_xbz_d : VecInt<[llvm_i32_ty], [llvm_v4i64_ty],
                                      [IntrNoMem]>;
def int_loongarch_lasx_xbz_v : VecInt<[llvm_i32_ty], [llvm_v32i8_ty],
                                      [IntrNoMem]>;

def int_loongarch_lasx_xbnz_v : VecInt<[llvm_i32_ty], [llvm_v32i8_ty],
                                       [IntrNoMem]>;
def int_loongarch_lasx_xbnz_b : VecInt<[llvm_i32_ty], [llvm_v32i8_ty],
                                       [IntrNoMem]>;
def int_loongarch_lasx_xbnz_h : VecInt<[llvm_i32_ty], [llvm_v16i16_ty],
                                       [IntrNoMem]>;
def int_loongarch_lasx_xbnz_w : VecInt<[llvm_i32_ty], [llvm_v8i32_ty],
                                       [IntrNoMem]>;
def int_loongarch_lasx_xbnz_d : VecInt<[llvm_i32_ty], [llvm_v4i64_ty],
                                       [IntrNoMem]>;

// LASX Float

foreach inst = ["xvfadd_s", "xvfsub_s", "xvfmul_s", "xvfdiv_s",
                "xvfmax_s", "xvfmin_s", "xvfmaxa_s", "xvfmina_s"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v8f32_ty],
                                        [llvm_v8f32_ty, llvm_v8f32_ty],
                                        [IntrNoMem]>;
foreach inst = ["xvfadd_d", "xvfsub_d", "xvfmul_d", "xvfdiv_d",
                "xvfmax_d", "xvfmin_d", "xvfmaxa_d", "xvfmina_d"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v4f64_ty],
                                        [llvm_v4f64_ty, llvm_v4f64_ty],
                                        [IntrNoMem]>;

foreach inst = ["xvfmadd_s", "xvfmsub_s", "xvfnmadd_s", "xvfnmsub_s"] in
  def int_loongarch_lasx_#inst
    : VecInt<[llvm_v8f32_ty],
             [llvm_v8f32_ty, llvm_v8f32_ty, llvm_v8f32_ty],
             [IntrNoMem]>;
foreach inst = ["xvfmadd_d", "xvfmsub_d", "xvfnmadd_d", "xvfnmsub_d"] in
  def int_loongarch_lasx_#inst
    : VecInt<[llvm_v4f64_ty],
             [llvm_v4f64_ty, llvm_v4f64_ty, llvm_v4f64_ty],
             [IntrNoMem]>;

foreach inst = ["xvflogb_s", "xvfsqrt_s", "xvfrecip_s", "xvfrsqrt_s", "xvfrint_s",
                "xvfrecipe_s", "xvfrsqrte_s",
                "xvfrintrne_s", "xvfrintrz_s", "xvfrintrp_s", "xvfrintrm_s"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v8f32_ty], [llvm_v8f32_ty],
                                        [IntrNoMem]>;
foreach inst = ["xvflogb_d", "xvfsqrt_d", "xvfrecip_d", "xvfrsqrt_d", "xvfrint_d",
                "xvfrecipe_d", "xvfrsqrte_d",
                "xvfrintrne_d", "xvfrintrz_d", "xvfrintrp_d", "xvfrintrm_d"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v4f64_ty], [llvm_v4f64_ty],
                                        [IntrNoMem]>;

foreach inst = ["xvfcvtl_s_h", "xvfcvth_s_h"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v8f32_ty], [llvm_v16i16_ty],
                                        [IntrNoMem]>;
foreach inst = ["xvfcvtl_d_s", "xvfcvth_d_s"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v4f64_ty], [llvm_v8f32_ty],
                                        [IntrNoMem]>;

foreach inst = ["xvftintrne_w_s", "xvftintrz_w_s", "xvftintrp_w_s", "xvftintrm_w_s",
                "xvftint_w_s", "xvftintrz_wu_s", "xvftint_wu_s", "xvfclass_s"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v8i32_ty], [llvm_v8f32_ty],
                                        [IntrNoMem]>;
foreach inst = ["xvftintrne_l_d", "xvftintrz_l_d", "xvftintrp_l_d", "xvftintrm_l_d",
                "xvftint_l_d", "xvftintrz_lu_d", "xvftint_lu_d", "xvfclass_d"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v4i64_ty], [llvm_v4f64_ty],
                                        [IntrNoMem]>;

foreach inst = ["xvftintrnel_l_s", "xvftintrneh_l_s", "xvftintrzl_l_s",
                "xvftintrzh_l_s", "xvftintrpl_l_s", "xvftintrph_l_s",
                "xvftintrml_l_s", "xvftintrmh_l_s", "xvftintl_l_s",
                "xvftinth_l_s"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v4i64_ty], [llvm_v8f32_ty],
                                        [IntrNoMem]>;

foreach inst = ["xvffint_s_w", "xvffint_s_wu"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v8f32_ty], [llvm_v8i32_ty],
                                        [IntrNoMem]>;
foreach inst = ["xvffint_d_l", "xvffint_d_lu"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v4f64_ty], [llvm_v4i64_ty],
                                        [IntrNoMem]>;

foreach inst = ["xvffintl_d_w", "xvffinth_d_w"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v4f64_ty], [llvm_v8i32_ty],
                                        [IntrNoMem]>;

foreach inst = ["xvffint_s_l"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v8f32_ty],
                                        [llvm_v4i64_ty, llvm_v4i64_ty],
                                        [IntrNoMem]>;
foreach inst = ["xvftintrne_w_d", "xvftintrz_w_d", "xvftintrp_w_d", "xvftintrm_w_d",
                "xvftint_w_d"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v8i32_ty],
                                        [llvm_v4f64_ty, llvm_v4f64_ty],
                                        [IntrNoMem]>;

foreach inst = ["xvfcvt_h_s"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v16i16_ty],
                                        [llvm_v8f32_ty, llvm_v8f32_ty],
                                        [IntrNoMem]>;
foreach inst = ["xvfcvt_s_d"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v8f32_ty],
                                        [llvm_v4f64_ty, llvm_v4f64_ty],
                                        [IntrNoMem]>;

foreach inst = ["xvfcmp_caf_s", "xvfcmp_cun_s", "xvfcmp_ceq_s", "xvfcmp_cueq_s",
                "xvfcmp_clt_s", "xvfcmp_cult_s", "xvfcmp_cle_s", "xvfcmp_cule_s",
                "xvfcmp_cne_s", "xvfcmp_cor_s", "xvfcmp_cune_s",
                "xvfcmp_saf_s", "xvfcmp_sun_s", "xvfcmp_seq_s", "xvfcmp_sueq_s",
                "xvfcmp_slt_s", "xvfcmp_sult_s", "xvfcmp_sle_s", "xvfcmp_sule_s",
                "xvfcmp_sne_s", "xvfcmp_sor_s", "xvfcmp_sune_s"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v8i32_ty],
                                        [llvm_v8f32_ty, llvm_v8f32_ty],
                                        [IntrNoMem]>;
foreach inst = ["xvfcmp_caf_d", "xvfcmp_cun_d", "xvfcmp_ceq_d", "xvfcmp_cueq_d",
                "xvfcmp_clt_d", "xvfcmp_cult_d", "xvfcmp_cle_d", "xvfcmp_cule_d",
                "xvfcmp_cne_d", "xvfcmp_cor_d", "xvfcmp_cune_d",
                "xvfcmp_saf_d", "xvfcmp_sun_d", "xvfcmp_seq_d", "xvfcmp_sueq_d",
                "xvfcmp_slt_d", "xvfcmp_sult_d", "xvfcmp_sle_d", "xvfcmp_sule_d",
                "xvfcmp_sne_d", "xvfcmp_sor_d", "xvfcmp_sune_d"] in
  def int_loongarch_lasx_#inst : VecInt<[llvm_v4i64_ty],
                                        [llvm_v4f64_ty, llvm_v4f64_ty],
                                        [IntrNoMem]>;

def int_loongarch_lasx_xvpickve_w_f
  : VecInt<[llvm_v8f32_ty], [llvm_v8f32_ty, llvm_i32_ty],
           [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_loongarch_lasx_xvpickve_d_f
  : VecInt<[llvm_v4f64_ty], [llvm_v4f64_ty, llvm_i32_ty],
           [IntrNoMem, ImmArg<ArgIndex<1>>]>;

// LASX load/store
def int_loongarch_lasx_xvld
  : VecInt<[llvm_v32i8_ty], [llvm_ptr_ty, llvm_i32_ty],
           [IntrReadMem, IntrArgMemOnly, ImmArg<ArgIndex<1>>]>;
def int_loongarch_lasx_xvldx
  : VecInt<[llvm_v32i8_ty], [llvm_ptr_ty, llvm_i64_ty],
           [IntrReadMem, IntrArgMemOnly]>;
def int_loongarch_lasx_xvldrepl_b
  : VecInt<[llvm_v32i8_ty], [llvm_ptr_ty, llvm_i32_ty],
           [IntrReadMem, IntrArgMemOnly, ImmArg<ArgIndex<1>>]>;
def int_loongarch_lasx_xvldrepl_h
  : VecInt<[llvm_v16i16_ty], [llvm_ptr_ty, llvm_i32_ty],
           [IntrReadMem, IntrArgMemOnly, ImmArg<ArgIndex<1>>]>;
def int_loongarch_lasx_xvldrepl_w
  : VecInt<[llvm_v8i32_ty], [llvm_ptr_ty, llvm_i32_ty],
           [IntrReadMem, IntrArgMemOnly, ImmArg<ArgIndex<1>>]>;
def int_loongarch_lasx_xvldrepl_d
  : VecInt<[llvm_v4i64_ty], [llvm_ptr_ty, llvm_i32_ty],
           [IntrReadMem, IntrArgMemOnly, ImmArg<ArgIndex<1>>]>;

def int_loongarch_lasx_xvst
  : VecInt<[], [llvm_v32i8_ty, llvm_ptr_ty, llvm_i32_ty],
           [IntrWriteMem, IntrArgMemOnly, ImmArg<ArgIndex<2>>]>;
def int_loongarch_lasx_xvstx
  : VecInt<[], [llvm_v32i8_ty, llvm_ptr_ty, llvm_i64_ty],
           [IntrWriteMem, IntrArgMemOnly]>;
def int_loongarch_lasx_xvstelm_b
  : VecInt<[], [llvm_v32i8_ty, llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty],
           [IntrWriteMem, IntrArgMemOnly, ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<3>>]>;
def int_loongarch_lasx_xvstelm_h
  : VecInt<[], [llvm_v16i16_ty, llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty],
           [IntrWriteMem, IntrArgMemOnly, ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<3>>]>;
def int_loongarch_lasx_xvstelm_w
  : VecInt<[], [llvm_v8i32_ty, llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty],
           [IntrWriteMem, IntrArgMemOnly, ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<3>>]>;
def int_loongarch_lasx_xvstelm_d
  : VecInt<[], [llvm_v4i64_ty, llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty],
           [IntrWriteMem, IntrArgMemOnly, ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<3>>]>;
} // TargetPrefix = "loongarch"
