//===- IntrinsicsRISCVXTHead.td - T-Head intrinsics --------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines all of the T-Head vendor intrinsics for RISC-V.
//
//===----------------------------------------------------------------------===//

let TargetPrefix = "riscv" in {

  class TH_VdotTernaryWideMasked
        : DefaultAttrsIntrinsic< [llvm_anyvector_ty],
                     [LLVMMatchType<0>, llvm_any_ty, llvm_anyvector_ty,
                      LLVMScalarOrSameVectorWidth<2, llvm_i1_ty>,
                      llvm_anyint_ty, LLVMMatchType<3>],
                     [ImmArg<ArgIndex<5>>, IntrNoMem]>, RISCVVIntrinsic {
    let ScalarOperand = 1;
    let VLOperand = 4;
  }

  multiclass TH_VdotTernaryWide {
    def "int_riscv_" # NAME : RISCVTernaryWideUnMasked;
    def "int_riscv_" # NAME # "_mask" : TH_VdotTernaryWideMasked;
  }

  defm th_vmaqa    : TH_VdotTernaryWide;
  defm th_vmaqau   : TH_VdotTernaryWide;
  defm th_vmaqasu  : TH_VdotTernaryWide;
  defm th_vmaqaus  : TH_VdotTernaryWide;
}
