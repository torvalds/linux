//===- IntrinsicsSPIRV.td - Defines SPIRV intrinsics -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines all of the SPIRV-specific intrinsics.
//
//===----------------------------------------------------------------------===//

let TargetPrefix = "spv" in {
  def int_spv_assign_type : Intrinsic<[], [llvm_any_ty, llvm_metadata_ty]>;
  def int_spv_assign_ptr_type : Intrinsic<[], [llvm_any_ty, llvm_metadata_ty, llvm_i32_ty], [ImmArg<ArgIndex<2>>]>;
  def int_spv_assign_name : Intrinsic<[], [llvm_any_ty, llvm_vararg_ty]>;
  def int_spv_assign_decoration : Intrinsic<[], [llvm_any_ty, llvm_metadata_ty]>;

  def int_spv_track_constant : Intrinsic<[llvm_any_ty], [llvm_any_ty, llvm_metadata_ty]>;
  def int_spv_init_global : Intrinsic<[], [llvm_any_ty, llvm_any_ty]>;
  def int_spv_unref_global : Intrinsic<[], [llvm_any_ty]>;

  def int_spv_gep : Intrinsic<[llvm_anyptr_ty], [llvm_i1_ty, llvm_any_ty, llvm_vararg_ty], [ImmArg<ArgIndex<0>>]>;
  def int_spv_load : Intrinsic<[llvm_i32_ty], [llvm_anyptr_ty, llvm_i16_ty, llvm_i8_ty], [ImmArg<ArgIndex<1>>, ImmArg<ArgIndex<2>>]>;
  def int_spv_store : Intrinsic<[], [llvm_any_ty, llvm_anyptr_ty, llvm_i16_ty, llvm_i8_ty], [ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<3>>]>;
  def int_spv_extractv : Intrinsic<[llvm_any_ty], [llvm_i32_ty, llvm_vararg_ty]>;
  def int_spv_insertv : Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_any_ty, llvm_vararg_ty]>;
  def int_spv_extractelt : Intrinsic<[llvm_any_ty], [llvm_any_ty, llvm_anyint_ty]>;
  def int_spv_insertelt : Intrinsic<[llvm_any_ty], [llvm_any_ty, llvm_any_ty, llvm_anyint_ty]>;
  def int_spv_const_composite : Intrinsic<[llvm_any_ty], [llvm_vararg_ty]>;
  def int_spv_bitcast : Intrinsic<[llvm_any_ty], [llvm_any_ty]>;
  def int_spv_ptrcast : Intrinsic<[llvm_any_ty], [llvm_any_ty, llvm_metadata_ty, llvm_i32_ty], [ImmArg<ArgIndex<2>>]>;
  def int_spv_switch : Intrinsic<[], [llvm_any_ty, llvm_vararg_ty]>;
  def int_spv_cmpxchg : Intrinsic<[llvm_i32_ty], [llvm_any_ty, llvm_vararg_ty]>;
  def int_spv_unreachable : Intrinsic<[], []>;
  def int_spv_alloca : Intrinsic<[llvm_any_ty], []>;
  def int_spv_alloca_array : Intrinsic<[llvm_any_ty], [llvm_anyint_ty]>;
  def int_spv_undef : Intrinsic<[llvm_i32_ty], []>;
  def int_spv_inline_asm : Intrinsic<[], [llvm_metadata_ty, llvm_metadata_ty, llvm_vararg_ty]>;

  // Expect, Assume Intrinsics
  def int_spv_assume : Intrinsic<[], [llvm_i1_ty]>;
  def int_spv_expect : Intrinsic<[llvm_anyint_ty], [LLVMMatchType<0>, LLVMMatchType<0>]>;

  // Memory Use Markers
  def int_spv_lifetime_start : Intrinsic<[],
                                [llvm_i64_ty, llvm_anyptr_ty],
                                [IntrArgMemOnly, IntrWillReturn,
                                NoCapture<ArgIndex<1>>,
                                ImmArg<ArgIndex<0>>]>;
  def int_spv_lifetime_end   : Intrinsic<[],
                                [llvm_i64_ty, llvm_anyptr_ty],
                                [IntrArgMemOnly, IntrWillReturn,
                                NoCapture<ArgIndex<1>>,
                                ImmArg<ArgIndex<0>>]>;

  // The following intrinsic(s) are mirrored from IntrinsicsDirectX.td for HLSL support.
  def int_spv_thread_id : Intrinsic<[llvm_i32_ty], [llvm_i32_ty], [IntrNoMem, IntrWillReturn]>;
  def int_spv_create_handle : ClangBuiltin<"__builtin_hlsl_create_handle">,
      Intrinsic<[ llvm_ptr_ty ], [llvm_i8_ty], [IntrWillReturn]>;
  def int_spv_all : DefaultAttrsIntrinsic<[llvm_i1_ty], [llvm_any_ty]>;
  def int_spv_any : DefaultAttrsIntrinsic<[llvm_i1_ty], [llvm_any_ty]>;
  def int_spv_lerp : Intrinsic<[LLVMMatchType<0>], [llvm_anyfloat_ty, LLVMMatchType<0>,LLVMMatchType<0>], 
    [IntrNoMem, IntrWillReturn] >;
  def int_spv_rsqrt : DefaultAttrsIntrinsic<[LLVMMatchType<0>], [llvm_anyfloat_ty]>;
}
