//===------ SemaMIPS.cpp -------- MIPS target-specific routines -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements semantic analysis functions specific to MIPS.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaMIPS.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Basic/TargetBuiltins.h"
#include "clang/Sema/Attr.h"
#include "clang/Sema/ParsedAttr.h"
#include "clang/Sema/Sema.h"

namespace clang {

SemaMIPS::SemaMIPS(Sema &S) : SemaBase(S) {}

bool SemaMIPS::CheckMipsBuiltinFunctionCall(const TargetInfo &TI,
                                            unsigned BuiltinID,
                                            CallExpr *TheCall) {
  return CheckMipsBuiltinCpu(TI, BuiltinID, TheCall) ||
         CheckMipsBuiltinArgument(BuiltinID, TheCall);
}

bool SemaMIPS::CheckMipsBuiltinCpu(const TargetInfo &TI, unsigned BuiltinID,
                                   CallExpr *TheCall) {

  if (Mips::BI__builtin_mips_addu_qb <= BuiltinID &&
      BuiltinID <= Mips::BI__builtin_mips_lwx) {
    if (!TI.hasFeature("dsp"))
      return Diag(TheCall->getBeginLoc(), diag::err_mips_builtin_requires_dsp);
  }

  if (Mips::BI__builtin_mips_absq_s_qb <= BuiltinID &&
      BuiltinID <= Mips::BI__builtin_mips_subuh_r_qb) {
    if (!TI.hasFeature("dspr2"))
      return Diag(TheCall->getBeginLoc(),
                  diag::err_mips_builtin_requires_dspr2);
  }

  if (Mips::BI__builtin_msa_add_a_b <= BuiltinID &&
      BuiltinID <= Mips::BI__builtin_msa_xori_b) {
    if (!TI.hasFeature("msa"))
      return Diag(TheCall->getBeginLoc(), diag::err_mips_builtin_requires_msa);
  }

  return false;
}

// CheckMipsBuiltinArgument - Checks the constant value passed to the
// intrinsic is correct. The switch statement is ordered by DSP, MSA. The
// ordering for DSP is unspecified. MSA is ordered by the data format used
// by the underlying instruction i.e., df/m, df/n and then by size.
//
// FIXME: The size tests here should instead be tablegen'd along with the
//        definitions from include/clang/Basic/BuiltinsMips.def.
// FIXME: GCC is strict on signedness for some of these intrinsics, we should
//        be too.
bool SemaMIPS::CheckMipsBuiltinArgument(unsigned BuiltinID, CallExpr *TheCall) {
  unsigned i = 0, l = 0, u = 0, m = 0;
  switch (BuiltinID) {
  default: return false;
  case Mips::BI__builtin_mips_wrdsp: i = 1; l = 0; u = 63; break;
  case Mips::BI__builtin_mips_rddsp: i = 0; l = 0; u = 63; break;
  case Mips::BI__builtin_mips_append: i = 2; l = 0; u = 31; break;
  case Mips::BI__builtin_mips_balign: i = 2; l = 0; u = 3; break;
  case Mips::BI__builtin_mips_precr_sra_ph_w: i = 2; l = 0; u = 31; break;
  case Mips::BI__builtin_mips_precr_sra_r_ph_w: i = 2; l = 0; u = 31; break;
  case Mips::BI__builtin_mips_prepend: i = 2; l = 0; u = 31; break;
  // MSA intrinsics. Instructions (which the intrinsics maps to) which use the
  // df/m field.
  // These intrinsics take an unsigned 3 bit immediate.
  case Mips::BI__builtin_msa_bclri_b:
  case Mips::BI__builtin_msa_bnegi_b:
  case Mips::BI__builtin_msa_bseti_b:
  case Mips::BI__builtin_msa_sat_s_b:
  case Mips::BI__builtin_msa_sat_u_b:
  case Mips::BI__builtin_msa_slli_b:
  case Mips::BI__builtin_msa_srai_b:
  case Mips::BI__builtin_msa_srari_b:
  case Mips::BI__builtin_msa_srli_b:
  case Mips::BI__builtin_msa_srlri_b: i = 1; l = 0; u = 7; break;
  case Mips::BI__builtin_msa_binsli_b:
  case Mips::BI__builtin_msa_binsri_b: i = 2; l = 0; u = 7; break;
  // These intrinsics take an unsigned 4 bit immediate.
  case Mips::BI__builtin_msa_bclri_h:
  case Mips::BI__builtin_msa_bnegi_h:
  case Mips::BI__builtin_msa_bseti_h:
  case Mips::BI__builtin_msa_sat_s_h:
  case Mips::BI__builtin_msa_sat_u_h:
  case Mips::BI__builtin_msa_slli_h:
  case Mips::BI__builtin_msa_srai_h:
  case Mips::BI__builtin_msa_srari_h:
  case Mips::BI__builtin_msa_srli_h:
  case Mips::BI__builtin_msa_srlri_h: i = 1; l = 0; u = 15; break;
  case Mips::BI__builtin_msa_binsli_h:
  case Mips::BI__builtin_msa_binsri_h: i = 2; l = 0; u = 15; break;
  // These intrinsics take an unsigned 5 bit immediate.
  // The first block of intrinsics actually have an unsigned 5 bit field,
  // not a df/n field.
  case Mips::BI__builtin_msa_cfcmsa:
  case Mips::BI__builtin_msa_ctcmsa: i = 0; l = 0; u = 31; break;
  case Mips::BI__builtin_msa_clei_u_b:
  case Mips::BI__builtin_msa_clei_u_h:
  case Mips::BI__builtin_msa_clei_u_w:
  case Mips::BI__builtin_msa_clei_u_d:
  case Mips::BI__builtin_msa_clti_u_b:
  case Mips::BI__builtin_msa_clti_u_h:
  case Mips::BI__builtin_msa_clti_u_w:
  case Mips::BI__builtin_msa_clti_u_d:
  case Mips::BI__builtin_msa_maxi_u_b:
  case Mips::BI__builtin_msa_maxi_u_h:
  case Mips::BI__builtin_msa_maxi_u_w:
  case Mips::BI__builtin_msa_maxi_u_d:
  case Mips::BI__builtin_msa_mini_u_b:
  case Mips::BI__builtin_msa_mini_u_h:
  case Mips::BI__builtin_msa_mini_u_w:
  case Mips::BI__builtin_msa_mini_u_d:
  case Mips::BI__builtin_msa_addvi_b:
  case Mips::BI__builtin_msa_addvi_h:
  case Mips::BI__builtin_msa_addvi_w:
  case Mips::BI__builtin_msa_addvi_d:
  case Mips::BI__builtin_msa_bclri_w:
  case Mips::BI__builtin_msa_bnegi_w:
  case Mips::BI__builtin_msa_bseti_w:
  case Mips::BI__builtin_msa_sat_s_w:
  case Mips::BI__builtin_msa_sat_u_w:
  case Mips::BI__builtin_msa_slli_w:
  case Mips::BI__builtin_msa_srai_w:
  case Mips::BI__builtin_msa_srari_w:
  case Mips::BI__builtin_msa_srli_w:
  case Mips::BI__builtin_msa_srlri_w:
  case Mips::BI__builtin_msa_subvi_b:
  case Mips::BI__builtin_msa_subvi_h:
  case Mips::BI__builtin_msa_subvi_w:
  case Mips::BI__builtin_msa_subvi_d: i = 1; l = 0; u = 31; break;
  case Mips::BI__builtin_msa_binsli_w:
  case Mips::BI__builtin_msa_binsri_w: i = 2; l = 0; u = 31; break;
  // These intrinsics take an unsigned 6 bit immediate.
  case Mips::BI__builtin_msa_bclri_d:
  case Mips::BI__builtin_msa_bnegi_d:
  case Mips::BI__builtin_msa_bseti_d:
  case Mips::BI__builtin_msa_sat_s_d:
  case Mips::BI__builtin_msa_sat_u_d:
  case Mips::BI__builtin_msa_slli_d:
  case Mips::BI__builtin_msa_srai_d:
  case Mips::BI__builtin_msa_srari_d:
  case Mips::BI__builtin_msa_srli_d:
  case Mips::BI__builtin_msa_srlri_d: i = 1; l = 0; u = 63; break;
  case Mips::BI__builtin_msa_binsli_d:
  case Mips::BI__builtin_msa_binsri_d: i = 2; l = 0; u = 63; break;
  // These intrinsics take a signed 5 bit immediate.
  case Mips::BI__builtin_msa_ceqi_b:
  case Mips::BI__builtin_msa_ceqi_h:
  case Mips::BI__builtin_msa_ceqi_w:
  case Mips::BI__builtin_msa_ceqi_d:
  case Mips::BI__builtin_msa_clti_s_b:
  case Mips::BI__builtin_msa_clti_s_h:
  case Mips::BI__builtin_msa_clti_s_w:
  case Mips::BI__builtin_msa_clti_s_d:
  case Mips::BI__builtin_msa_clei_s_b:
  case Mips::BI__builtin_msa_clei_s_h:
  case Mips::BI__builtin_msa_clei_s_w:
  case Mips::BI__builtin_msa_clei_s_d:
  case Mips::BI__builtin_msa_maxi_s_b:
  case Mips::BI__builtin_msa_maxi_s_h:
  case Mips::BI__builtin_msa_maxi_s_w:
  case Mips::BI__builtin_msa_maxi_s_d:
  case Mips::BI__builtin_msa_mini_s_b:
  case Mips::BI__builtin_msa_mini_s_h:
  case Mips::BI__builtin_msa_mini_s_w:
  case Mips::BI__builtin_msa_mini_s_d: i = 1; l = -16; u = 15; break;
  // These intrinsics take an unsigned 8 bit immediate.
  case Mips::BI__builtin_msa_andi_b:
  case Mips::BI__builtin_msa_nori_b:
  case Mips::BI__builtin_msa_ori_b:
  case Mips::BI__builtin_msa_shf_b:
  case Mips::BI__builtin_msa_shf_h:
  case Mips::BI__builtin_msa_shf_w:
  case Mips::BI__builtin_msa_xori_b: i = 1; l = 0; u = 255; break;
  case Mips::BI__builtin_msa_bseli_b:
  case Mips::BI__builtin_msa_bmnzi_b:
  case Mips::BI__builtin_msa_bmzi_b: i = 2; l = 0; u = 255; break;
  // df/n format
  // These intrinsics take an unsigned 4 bit immediate.
  case Mips::BI__builtin_msa_copy_s_b:
  case Mips::BI__builtin_msa_copy_u_b:
  case Mips::BI__builtin_msa_insve_b:
  case Mips::BI__builtin_msa_splati_b: i = 1; l = 0; u = 15; break;
  case Mips::BI__builtin_msa_sldi_b: i = 2; l = 0; u = 15; break;
  // These intrinsics take an unsigned 3 bit immediate.
  case Mips::BI__builtin_msa_copy_s_h:
  case Mips::BI__builtin_msa_copy_u_h:
  case Mips::BI__builtin_msa_insve_h:
  case Mips::BI__builtin_msa_splati_h: i = 1; l = 0; u = 7; break;
  case Mips::BI__builtin_msa_sldi_h: i = 2; l = 0; u = 7; break;
  // These intrinsics take an unsigned 2 bit immediate.
  case Mips::BI__builtin_msa_copy_s_w:
  case Mips::BI__builtin_msa_copy_u_w:
  case Mips::BI__builtin_msa_insve_w:
  case Mips::BI__builtin_msa_splati_w: i = 1; l = 0; u = 3; break;
  case Mips::BI__builtin_msa_sldi_w: i = 2; l = 0; u = 3; break;
  // These intrinsics take an unsigned 1 bit immediate.
  case Mips::BI__builtin_msa_copy_s_d:
  case Mips::BI__builtin_msa_copy_u_d:
  case Mips::BI__builtin_msa_insve_d:
  case Mips::BI__builtin_msa_splati_d: i = 1; l = 0; u = 1; break;
  case Mips::BI__builtin_msa_sldi_d: i = 2; l = 0; u = 1; break;
  // Memory offsets and immediate loads.
  // These intrinsics take a signed 10 bit immediate.
  case Mips::BI__builtin_msa_ldi_b: i = 0; l = -128; u = 255; break;
  case Mips::BI__builtin_msa_ldi_h:
  case Mips::BI__builtin_msa_ldi_w:
  case Mips::BI__builtin_msa_ldi_d: i = 0; l = -512; u = 511; break;
  case Mips::BI__builtin_msa_ld_b: i = 1; l = -512; u = 511; m = 1; break;
  case Mips::BI__builtin_msa_ld_h: i = 1; l = -1024; u = 1022; m = 2; break;
  case Mips::BI__builtin_msa_ld_w: i = 1; l = -2048; u = 2044; m = 4; break;
  case Mips::BI__builtin_msa_ld_d: i = 1; l = -4096; u = 4088; m = 8; break;
  case Mips::BI__builtin_msa_ldr_d: i = 1; l = -4096; u = 4088; m = 8; break;
  case Mips::BI__builtin_msa_ldr_w: i = 1; l = -2048; u = 2044; m = 4; break;
  case Mips::BI__builtin_msa_st_b: i = 2; l = -512; u = 511; m = 1; break;
  case Mips::BI__builtin_msa_st_h: i = 2; l = -1024; u = 1022; m = 2; break;
  case Mips::BI__builtin_msa_st_w: i = 2; l = -2048; u = 2044; m = 4; break;
  case Mips::BI__builtin_msa_st_d: i = 2; l = -4096; u = 4088; m = 8; break;
  case Mips::BI__builtin_msa_str_d: i = 2; l = -4096; u = 4088; m = 8; break;
  case Mips::BI__builtin_msa_str_w: i = 2; l = -2048; u = 2044; m = 4; break;
  }

  if (!m)
    return SemaRef.BuiltinConstantArgRange(TheCall, i, l, u);

  return SemaRef.BuiltinConstantArgRange(TheCall, i, l, u) ||
         SemaRef.BuiltinConstantArgMultiple(TheCall, i, m);
}

void SemaMIPS::handleInterruptAttr(Decl *D, const ParsedAttr &AL) {
  // Only one optional argument permitted.
  if (AL.getNumArgs() > 1) {
    Diag(AL.getLoc(), diag::err_attribute_too_many_arguments) << AL << 1;
    return;
  }

  StringRef Str;
  SourceLocation ArgLoc;

  if (AL.getNumArgs() == 0)
    Str = "";
  else if (!SemaRef.checkStringLiteralArgumentAttr(AL, 0, Str, &ArgLoc))
    return;

  // Semantic checks for a function with the 'interrupt' attribute for MIPS:
  // a) Must be a function.
  // b) Must have no parameters.
  // c) Must have the 'void' return type.
  // d) Cannot have the 'mips16' attribute, as that instruction set
  //    lacks the 'eret' instruction.
  // e) The attribute itself must either have no argument or one of the
  //    valid interrupt types, see [MipsInterruptDocs].

  if (!isFuncOrMethodForAttrSubject(D)) {
    Diag(D->getLocation(), diag::warn_attribute_wrong_decl_type)
        << AL << AL.isRegularKeywordAttribute() << ExpectedFunctionOrMethod;
    return;
  }

  if (hasFunctionProto(D) && getFunctionOrMethodNumParams(D) != 0) {
    Diag(D->getLocation(), diag::warn_interrupt_attribute_invalid)
        << /*MIPS*/ 0 << 0;
    return;
  }

  if (!getFunctionOrMethodResultType(D)->isVoidType()) {
    Diag(D->getLocation(), diag::warn_interrupt_attribute_invalid)
        << /*MIPS*/ 0 << 1;
    return;
  }

  // We still have to do this manually because the Interrupt attributes are
  // a bit special due to sharing their spellings across targets.
  if (checkAttrMutualExclusion<Mips16Attr>(*this, D, AL))
    return;

  MipsInterruptAttr::InterruptType Kind;
  if (!MipsInterruptAttr::ConvertStrToInterruptType(Str, Kind)) {
    Diag(AL.getLoc(), diag::warn_attribute_type_not_supported)
        << AL << "'" + std::string(Str) + "'";
    return;
  }

  D->addAttr(::new (getASTContext())
                 MipsInterruptAttr(getASTContext(), AL, Kind));
}

} // namespace clang
