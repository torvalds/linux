/*===-- passbuilder_ocaml.c - LLVM OCaml Glue -------------------*- C++ -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This file glues LLVM's OCaml interface to its C interface. These functions *|
|* are by and large transparent wrappers to the corresponding C functions.    *|
|*                                                                            *|
|* Note that these functions intentionally take liberties with the CAMLparamX *|
|* macros, since most of the parameters are not GC heap objects.              *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#include "llvm_ocaml.h"
#include "target_ocaml.h"
#include "llvm-c/Error.h"
#include "llvm-c/Transforms/PassBuilder.h"
#include <caml/memory.h>

#define PassBuilderOptions_val(v) ((LLVMPassBuilderOptionsRef)from_val(v))

value llvm_run_passes(value M, value Passes, value TM, value Options) {
  LLVMErrorRef Err =
      LLVMRunPasses(Module_val(M), String_val(Passes), TargetMachine_val(TM),
                    PassBuilderOptions_val(Options));
  if (Err == LLVMErrorSuccess) {
    value result = caml_alloc(1, 0);
    Store_field(result, 0, Val_unit);
    return result;
  } else {
    char *str = LLVMGetErrorMessage(Err);
    value v = caml_copy_string(str);
    LLVMDisposeErrorMessage(str);
    value result = caml_alloc(1, 1);
    Store_field(result, 0, v);
    return result;
  }
}

value llvm_create_passbuilder_options(value Unit) {
  LLVMPassBuilderOptionsRef PBO = LLVMCreatePassBuilderOptions();
  return to_val(PBO);
}

value llvm_passbuilder_options_set_verify_each(value PBO, value VerifyEach) {
  LLVMPassBuilderOptionsSetVerifyEach(PassBuilderOptions_val(PBO),
                                      Bool_val(VerifyEach));
  return Val_unit;
}

value llvm_passbuilder_options_set_debug_logging(value PBO,
                                                 value DebugLogging) {
  LLVMPassBuilderOptionsSetDebugLogging(PassBuilderOptions_val(PBO),
                                        Bool_val(DebugLogging));
  return Val_unit;
}

value llvm_passbuilder_options_set_loop_interleaving(value PBO,
                                                     value LoopInterleaving) {
  LLVMPassBuilderOptionsSetLoopInterleaving(PassBuilderOptions_val(PBO),
                                            Bool_val(LoopInterleaving));
  return Val_unit;
}

value llvm_passbuilder_options_set_loop_vectorization(value PBO,
                                                      value LoopVectorization) {
  LLVMPassBuilderOptionsSetLoopVectorization(PassBuilderOptions_val(PBO),
                                             Bool_val(LoopVectorization));
  return Val_unit;
}

value llvm_passbuilder_options_set_slp_vectorization(value PBO,
                                                     value SLPVectorization) {
  LLVMPassBuilderOptionsSetSLPVectorization(PassBuilderOptions_val(PBO),
                                            Bool_val(SLPVectorization));
  return Val_unit;
}

value llvm_passbuilder_options_set_loop_unrolling(value PBO,
                                                  value LoopUnrolling) {
  LLVMPassBuilderOptionsSetLoopUnrolling(PassBuilderOptions_val(PBO),
                                         Bool_val(LoopUnrolling));
  return Val_unit;
}

value llvm_passbuilder_options_set_forget_all_scev_in_loop_unroll(
    value PBO, value ForgetAllSCEVInLoopUnroll) {
  LLVMPassBuilderOptionsSetForgetAllSCEVInLoopUnroll(
      PassBuilderOptions_val(PBO), Bool_val(ForgetAllSCEVInLoopUnroll));
  return Val_unit;
}

value llvm_passbuilder_options_set_licm_mssa_opt_cap(value PBO,
                                                     value LicmMssaOptCap) {
  LLVMPassBuilderOptionsSetLicmMssaOptCap(PassBuilderOptions_val(PBO),
                                          Int_val(LicmMssaOptCap));
  return Val_unit;
}

value llvm_passbuilder_options_set_licm_mssa_no_acc_for_promotion_cap(
    value PBO, value LicmMssaNoAccForPromotionCap) {
  LLVMPassBuilderOptionsSetLicmMssaNoAccForPromotionCap(
      PassBuilderOptions_val(PBO), Int_val(LicmMssaNoAccForPromotionCap));
  return Val_unit;
}

value llvm_passbuilder_options_set_call_graph_profile(value PBO,
                                                      value CallGraphProfile) {
  LLVMPassBuilderOptionsSetCallGraphProfile(PassBuilderOptions_val(PBO),
                                            Bool_val(CallGraphProfile));
  return Val_unit;
}

value llvm_passbuilder_options_set_merge_functions(value PBO,
                                                   value MergeFunctions) {
  LLVMPassBuilderOptionsSetMergeFunctions(PassBuilderOptions_val(PBO),
                                          Bool_val(MergeFunctions));
  return Val_unit;
}

value llvm_passbuilder_options_set_inliner_threshold(value PBO,
                                                     value InlinerThreshold) {
  LLVMPassBuilderOptionsSetInlinerThreshold(PassBuilderOptions_val(PBO),
                                            Int_val(InlinerThreshold));
  return Val_unit;
}

value llvm_dispose_passbuilder_options(value PBO) {
  LLVMDisposePassBuilderOptions(PassBuilderOptions_val(PBO));
  return Val_unit;
}
