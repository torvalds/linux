//===--- arm_fp16.td - ARM FP16 compiler interface ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the TableGen definitions from which the ARM FP16 header
//  file will be generated.
//
//===----------------------------------------------------------------------===//

include "arm_neon_incl.td"

// ARMv8.2-A FP16 intrinsics.
let ArchGuard = "defined(__aarch64__) || defined(__arm64ec__)", TargetGuard = "fullfp16" in {

  // Negate
  def VNEGSH          : SInst<"vneg", "11", "Sh">;

  // Reciprocal/Sqrt
  def SCALAR_FRECPSH  : IInst<"vrecps", "111", "Sh">;
  def FSQRTSH         : SInst<"vsqrt", "11", "Sh">;
  def SCALAR_FRSQRTSH : IInst<"vrsqrts", "111", "Sh">;

  // Reciprocal Estimate
  def SCALAR_FRECPEH  : IInst<"vrecpe", "11", "Sh">;

  // Reciprocal Exponent
  def SCALAR_FRECPXH  : IInst<"vrecpx", "11", "Sh">;

  // Reciprocal Square Root Estimate
  def SCALAR_FRSQRTEH : IInst<"vrsqrte", "11", "Sh">;

  // Rounding
  def FRINTZ_S64H     : SInst<"vrnd", "11", "Sh">;
  def FRINTA_S64H     : SInst<"vrnda", "11", "Sh">;
  def FRINTI_S64H     : SInst<"vrndi", "11", "Sh">;
  def FRINTM_S64H     : SInst<"vrndm", "11", "Sh">;
  def FRINTN_S64H     : SInst<"vrndn", "11", "Sh">;
  def FRINTP_S64H     : SInst<"vrndp", "11", "Sh">;
  def FRINTX_S64H     : SInst<"vrndx", "11", "Sh">;

  // Conversion
  def SCALAR_SCVTFSH  : SInst<"vcvth_f16", "(1F)(1!)", "sUs">;
  def SCALAR_SCVTFSH1 : SInst<"vcvth_f16", "(1F<)(1!)", "iUi">;
  def SCALAR_SCVTFSH2 : SInst<"vcvth_f16", "(1F<<)(1!)", "lUl">;
  def SCALAR_FCVTZSH  : SInst<"vcvt_s16", "(1S)1", "Sh">;
  def SCALAR_FCVTZSH1 : SInst<"vcvt_s32", "(1S>)1", "Sh">;
  def SCALAR_FCVTZSH2 : SInst<"vcvt_s64", "(1S>>)1", "Sh">;
  def SCALAR_FCVTZUH  : SInst<"vcvt_u16", "(1U)1", "Sh">;
  def SCALAR_FCVTZUH1 : SInst<"vcvt_u32", "(1U>)1", "Sh">;
  def SCALAR_FCVTZUH2 : SInst<"vcvt_u64", "(1U>>)1", "Sh">;
  def SCALAR_FCVTASH  : SInst<"vcvta_s16", "(1S)1", "Sh">;
  def SCALAR_FCVTASH1 : SInst<"vcvta_s32", "(1S>)1", "Sh">;
  def SCALAR_FCVTASH2 : SInst<"vcvta_s64", "(1S>>)1", "Sh">;
  def SCALAR_FCVTAUH  : SInst<"vcvta_u16", "(1U)1", "Sh">;
  def SCALAR_FCVTAUH1 : SInst<"vcvta_u32", "(1U>)1", "Sh">;
  def SCALAR_FCVTAUH2 : SInst<"vcvta_u64", "(1U>>)1", "Sh">;
  def SCALAR_FCVTMSH  : SInst<"vcvtm_s16", "(1S)1", "Sh">;
  def SCALAR_FCVTMSH1 : SInst<"vcvtm_s32", "(1S>)1", "Sh">;
  def SCALAR_FCVTMSH2 : SInst<"vcvtm_s64", "(1S>>)1", "Sh">;
  def SCALAR_FCVTMUH  : SInst<"vcvtm_u16", "(1U)1", "Sh">;
  def SCALAR_FCVTMUH1 : SInst<"vcvtm_u32", "(1U>)1", "Sh">;
  def SCALAR_FCVTMUH2 : SInst<"vcvtm_u64", "(1U>>)1", "Sh">;
  def SCALAR_FCVTNSH  : SInst<"vcvtn_s16", "(1S)1", "Sh">;
  def SCALAR_FCVTNSH1 : SInst<"vcvtn_s32", "(1S>)1", "Sh">;
  def SCALAR_FCVTNSH2 : SInst<"vcvtn_s64", "(1S>>)1", "Sh">;
  def SCALAR_FCVTNUH  : SInst<"vcvtn_u16", "(1U)1", "Sh">;
  def SCALAR_FCVTNUH1 : SInst<"vcvtn_u32", "(1U>)1", "Sh">;
  def SCALAR_FCVTNUH2 : SInst<"vcvtn_u64", "(1U>>)1", "Sh">;
  def SCALAR_FCVTPSH  : SInst<"vcvtp_s16", "(1S)1", "Sh">;
  def SCALAR_FCVTPSH1 : SInst<"vcvtp_s32", "(1S>)1", "Sh">;
  def SCALAR_FCVTPSH2 : SInst<"vcvtp_s64", "(1S>>)1", "Sh">;
  def SCALAR_FCVTPUH  : SInst<"vcvtp_u16", "(1U)1", "Sh">;
  def SCALAR_FCVTPUH1 : SInst<"vcvtp_u32", "(1U>)1", "Sh">;
  def SCALAR_FCVTPUH2 : SInst<"vcvtp_u64", "(1U>>)1", "Sh">;
  let isVCVT_N = 1 in {
    def SCALAR_SCVTFSHO : SInst<"vcvth_n_f16", "(1F)(1!)I", "sUs">;
    def SCALAR_SCVTFSH1O: SInst<"vcvth_n_f16", "(1F<)(1!)I", "iUi">;
    def SCALAR_SCVTFSH2O: SInst<"vcvth_n_f16", "(1F<<)(1!)I", "lUl">;
    def SCALAR_FCVTZSHO : SInst<"vcvt_n_s16", "(1S)1I", "Sh">;
    def SCALAR_FCVTZSH1O: SInst<"vcvt_n_s32", "(1S>)1I", "Sh">;
    def SCALAR_FCVTZSH2O: SInst<"vcvt_n_s64", "(1S>>)1I", "Sh">;
    def SCALAR_FCVTZUHO : SInst<"vcvt_n_u16", "(1U)1I", "Sh">;
    def SCALAR_FCVTZUH1O: SInst<"vcvt_n_u32", "(1U>)1I", "Sh">;
    def SCALAR_FCVTZUH2O: SInst<"vcvt_n_u64", "(1U>>)1I", "Sh">;
  }
  // Comparison
  def SCALAR_CMEQRH   : SInst<"vceq", "(1U)11", "Sh">;
  def SCALAR_CMEQZH   : SInst<"vceqz", "(1U)1", "Sh">;
  def SCALAR_CMGERH   : SInst<"vcge", "(1U)11", "Sh">;
  def SCALAR_CMGEZH   : SInst<"vcgez", "(1U)1", "Sh">;
  def SCALAR_CMGTRH   : SInst<"vcgt", "(1U)11", "Sh">;
  def SCALAR_CMGTZH   : SInst<"vcgtz", "(1U)1", "Sh">;
  def SCALAR_CMLERH   : SInst<"vcle", "(1U)11", "Sh">;
  def SCALAR_CMLEZH   : SInst<"vclez", "(1U)1", "Sh">;
  def SCALAR_CMLTH    : SInst<"vclt", "(1U)11", "Sh">;
  def SCALAR_CMLTZH   : SInst<"vcltz", "(1U)1", "Sh">;

  // Absolute Compare Mask Greater Than Or Equal
  def SCALAR_FACGEH   : IInst<"vcage", "(1U)11", "Sh">;
  def SCALAR_FACLEH   : IInst<"vcale", "(1U)11", "Sh">;

  // Absolute Compare Mask Greater Than
  def SCALAR_FACGT    : IInst<"vcagt", "(1U)11", "Sh">;
  def SCALAR_FACLT    : IInst<"vcalt", "(1U)11", "Sh">;

  // Scalar Absolute Value
  def SCALAR_ABSH     : SInst<"vabs", "11", "Sh">;

  // Scalar Absolute Difference
  def SCALAR_ABDH: IInst<"vabd", "111", "Sh">;

  // Add/Sub
  def VADDSH          : SInst<"vadd", "111", "Sh">;
  def VSUBHS          : SInst<"vsub", "111", "Sh">;

  // Max/Min
  def VMAXHS          : SInst<"vmax", "111", "Sh">;
  def VMINHS          : SInst<"vmin", "111", "Sh">;
  def FMAXNMHS        : SInst<"vmaxnm", "111", "Sh">;
  def FMINNMHS        : SInst<"vminnm", "111", "Sh">;

  // Multiplication/Division
  def VMULHS          : SInst<"vmul", "111", "Sh">;
  def MULXHS          : SInst<"vmulx", "111", "Sh">;
  def FDIVHS          : SInst<"vdiv", "111",  "Sh">;

  // Vector fused multiply-add operations
  def VFMAHS          : SInst<"vfma", "1111", "Sh">;
  def VFMSHS          : SInst<"vfms", "1111", "Sh">;
}
