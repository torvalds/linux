//===- IntrinsicsRISCVXCV.td - CORE-V intrinsics -----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines all of the CORE-V vendor intrinsics for RISC-V.
//
//===----------------------------------------------------------------------===//

class ScalarCoreVBitManipGprGprIntrinsic
    : DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem, IntrSpeculatable]>;

class ScalarCoreVBitManipGprIntrinsic
    : DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty],
                            [IntrNoMem, IntrSpeculatable]>;

class ScalarCoreVAluGprIntrinsic
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty],
                          [IntrNoMem, IntrSpeculatable]>;

class ScalarCoreVAluGprGprIntrinsic
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
                          [IntrNoMem, IntrSpeculatable]>;

class ScalarCoreVAluGprGprGprIntrinsic
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
                          [IntrNoMem, IntrSpeculatable]>;

class ScalarCoreVMacGprGprGprIntrinsic
  : Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [IntrNoMem, IntrWillReturn, IntrSpeculatable]>;

class ScalarCoreVMacGprGPRImmIntrinsic
    : Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
                [IntrNoMem, IntrWillReturn, IntrSpeculatable, ImmArg<ArgIndex<2>>]>;

class ScalarCoreVMacGprGprGprImmIntrinsic
  : Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
              [IntrNoMem, IntrWillReturn, IntrSpeculatable, ImmArg<ArgIndex<3>>]>;

let TargetPrefix = "riscv" in {
  def int_riscv_cv_bitmanip_extract : ScalarCoreVBitManipGprGprIntrinsic;
  def int_riscv_cv_bitmanip_extractu : ScalarCoreVBitManipGprGprIntrinsic;
  def int_riscv_cv_bitmanip_bclr : ScalarCoreVBitManipGprGprIntrinsic;
  def int_riscv_cv_bitmanip_bset : ScalarCoreVBitManipGprGprIntrinsic;

  def int_riscv_cv_bitmanip_insert
    : DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem, IntrSpeculatable]>;

  def int_riscv_cv_bitmanip_clb : ScalarCoreVBitManipGprIntrinsic;

  def int_riscv_cv_bitmanip_bitrev
    : DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem, IntrWillReturn, IntrSpeculatable,
                            ImmArg<ArgIndex<1>>, ImmArg<ArgIndex<2>>]>;

  def int_riscv_cv_alu_clip   : ScalarCoreVAluGprGprIntrinsic;
  def int_riscv_cv_alu_clipu  : ScalarCoreVAluGprGprIntrinsic;
  def int_riscv_cv_alu_addn   : ScalarCoreVAluGprGprGprIntrinsic;
  def int_riscv_cv_alu_addun  : ScalarCoreVAluGprGprGprIntrinsic;
  def int_riscv_cv_alu_addrn  : ScalarCoreVAluGprGprGprIntrinsic;
  def int_riscv_cv_alu_addurn : ScalarCoreVAluGprGprGprIntrinsic;
  def int_riscv_cv_alu_subn   : ScalarCoreVAluGprGprGprIntrinsic;
  def int_riscv_cv_alu_subun  : ScalarCoreVAluGprGprGprIntrinsic;
  def int_riscv_cv_alu_subrn  : ScalarCoreVAluGprGprGprIntrinsic;
  def int_riscv_cv_alu_suburn : ScalarCoreVAluGprGprGprIntrinsic;

  def int_riscv_cv_mac_mac : ScalarCoreVMacGprGprGprIntrinsic;
  def int_riscv_cv_mac_msu : ScalarCoreVMacGprGprGprIntrinsic;

  def int_riscv_cv_mac_muluN    : ScalarCoreVMacGprGPRImmIntrinsic;
  def int_riscv_cv_mac_mulhhuN  : ScalarCoreVMacGprGPRImmIntrinsic;
  def int_riscv_cv_mac_mulsN    : ScalarCoreVMacGprGPRImmIntrinsic;
  def int_riscv_cv_mac_mulhhsN  : ScalarCoreVMacGprGPRImmIntrinsic;
  def int_riscv_cv_mac_muluRN   : ScalarCoreVMacGprGPRImmIntrinsic;
  def int_riscv_cv_mac_mulhhuRN : ScalarCoreVMacGprGPRImmIntrinsic;
  def int_riscv_cv_mac_mulsRN   : ScalarCoreVMacGprGPRImmIntrinsic;
  def int_riscv_cv_mac_mulhhsRN : ScalarCoreVMacGprGPRImmIntrinsic;

  def int_riscv_cv_mac_macuN    : ScalarCoreVMacGprGprGprImmIntrinsic;
  def int_riscv_cv_mac_machhuN  : ScalarCoreVMacGprGprGprImmIntrinsic;
  def int_riscv_cv_mac_macsN    : ScalarCoreVMacGprGprGprImmIntrinsic;
  def int_riscv_cv_mac_machhsN  : ScalarCoreVMacGprGprGprImmIntrinsic;
  def int_riscv_cv_mac_macuRN   : ScalarCoreVMacGprGprGprImmIntrinsic;
  def int_riscv_cv_mac_machhuRN : ScalarCoreVMacGprGprGprImmIntrinsic;
  def int_riscv_cv_mac_macsRN   : ScalarCoreVMacGprGprGprImmIntrinsic;
  def int_riscv_cv_mac_machhsRN : ScalarCoreVMacGprGprGprImmIntrinsic;
} // TargetPrefix = "riscv"
