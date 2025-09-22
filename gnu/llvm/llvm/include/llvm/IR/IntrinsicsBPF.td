//===- IntrinsicsBPF.td - Defines BPF intrinsics -----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines all of the BPF-specific intrinsics.
//
//===----------------------------------------------------------------------===//

// Specialized loads from packet
let TargetPrefix = "bpf" in {  // All intrinsics start with "llvm.bpf."
  def int_bpf_load_byte : ClangBuiltin<"__builtin_bpf_load_byte">,
              DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_ptr_ty, llvm_i64_ty], [IntrReadMem]>;
  def int_bpf_load_half : ClangBuiltin<"__builtin_bpf_load_half">,
              DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_ptr_ty, llvm_i64_ty], [IntrReadMem]>;
  def int_bpf_load_word : ClangBuiltin<"__builtin_bpf_load_word">,
              DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_ptr_ty, llvm_i64_ty], [IntrReadMem]>;
  def int_bpf_pseudo : ClangBuiltin<"__builtin_bpf_pseudo">,
              Intrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty]>;
  def int_bpf_preserve_field_info : ClangBuiltin<"__builtin_bpf_preserve_field_info">,
              Intrinsic<[llvm_i32_ty], [llvm_anyptr_ty, llvm_i64_ty],
              [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_bpf_btf_type_id : ClangBuiltin<"__builtin_bpf_btf_type_id">,
              Intrinsic<[llvm_i64_ty], [llvm_i32_ty, llvm_i64_ty],
              [IntrNoMem]>;
  def int_bpf_preserve_type_info : ClangBuiltin<"__builtin_bpf_preserve_type_info">,
              Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i64_ty],
              [IntrNoMem]>;
  def int_bpf_preserve_enum_value : ClangBuiltin<"__builtin_bpf_preserve_enum_value">,
              Intrinsic<[llvm_i64_ty], [llvm_i32_ty, llvm_ptr_ty, llvm_i64_ty],
              [IntrNoMem]>;
  def int_bpf_passthrough : ClangBuiltin<"__builtin_bpf_passthrough">,
              Intrinsic<[llvm_any_ty], [llvm_i32_ty, llvm_any_ty], [IntrNoMem]>;
  def int_bpf_compare : ClangBuiltin<"__builtin_bpf_compare">,
              Intrinsic<[llvm_i1_ty], [llvm_i32_ty, llvm_anyint_ty, llvm_anyint_ty],
              [IntrNoMem]>;
  def int_bpf_getelementptr_and_load : ClangBuiltin<"__builtin_bpf_getelementptr_and_load">,
              Intrinsic<[llvm_any_ty],
                        [llvm_ptr_ty,     // base ptr for getelementptr
                         llvm_i1_ty,      // volatile
                         llvm_i8_ty,      // atomic order
                         llvm_i8_ty,      // synscope id
                         llvm_i8_ty,      // alignment
                         llvm_i1_ty,      // inbounds
                         llvm_vararg_ty], // indices for getelementptr insn
                        [IntrNoCallback,
                         IntrNoFree,
                         IntrWillReturn,
                         NoCapture <ArgIndex<0>>,
                         ImmArg    <ArgIndex<1>>, // volatile
                         ImmArg    <ArgIndex<2>>, // atomic order
                         ImmArg    <ArgIndex<3>>, // synscope id
                         ImmArg    <ArgIndex<4>>, // alignment
                         ImmArg    <ArgIndex<5>>, // inbounds
                        ]>;
  def int_bpf_getelementptr_and_store : ClangBuiltin<"__builtin_bpf_getelementptr_and_store">,
              Intrinsic<[],
                        [llvm_any_ty,     // value to store
                         llvm_ptr_ty,     // base ptr for getelementptr
                         llvm_i1_ty,      // volatile
                         llvm_i8_ty,      // atomic order
                         llvm_i8_ty,      // syncscope id
                         llvm_i8_ty,      // alignment
                         llvm_i1_ty,      // inbounds
                         llvm_vararg_ty], // indexes for getelementptr insn
                        [IntrNoCallback,
                         IntrNoFree,
                         IntrWillReturn,
                         NoCapture <ArgIndex<1>>,
                         ImmArg    <ArgIndex<2>>, // volatile
                         ImmArg    <ArgIndex<3>>, // atomic order
                         ImmArg    <ArgIndex<4>>, // syncscope id
                         ImmArg    <ArgIndex<5>>, // alignment
                         ImmArg    <ArgIndex<6>>, // inbounds
                        ]>;
}
