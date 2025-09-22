//===- IntrinsicsWebAssembly.td - Defines wasm intrinsics --*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines all of the WebAssembly-specific intrinsics.
///
//===----------------------------------------------------------------------===//

// Type definition for a table in an intrinsic
def llvm_table_ty : LLVMQualPointerType<1>;

let TargetPrefix = "wasm" in {  // All intrinsics start with "llvm.wasm.".

// Query the current memory size, and increase the current memory size.
// Note that memory.size is not IntrNoMem because it must be sequenced with
// respect to memory.grow calls.
def int_wasm_memory_size :
  DefaultAttrsIntrinsic<[llvm_anyint_ty], [llvm_i32_ty], [IntrReadMem]>;
def int_wasm_memory_grow :
  DefaultAttrsIntrinsic<[llvm_anyint_ty], [llvm_i32_ty, LLVMMatchType<0>], []>;

//===----------------------------------------------------------------------===//
// ref.null intrinsics
//===----------------------------------------------------------------------===//
def int_wasm_ref_null_extern :
  DefaultAttrsIntrinsic<[llvm_externref_ty], [], [IntrNoMem]>;
def int_wasm_ref_null_func :
  DefaultAttrsIntrinsic<[llvm_funcref_ty], [], [IntrNoMem]>;
def int_wasm_ref_null_exn:
  DefaultAttrsIntrinsic<[llvm_exnref_ty], [], [IntrNoMem]>;
def int_wasm_ref_is_null_extern :
  DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_externref_ty], [IntrNoMem],
                        "llvm.wasm.ref.is_null.extern">;
def int_wasm_ref_is_null_func :
  DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_funcref_ty],
                        [IntrNoMem], "llvm.wasm.ref.is_null.func">;
def int_wasm_ref_is_null_exn :
  DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_exnref_ty], [IntrNoMem],
                        "llvm.wasm.ref.is_null.exn">;

//===----------------------------------------------------------------------===//
// Table intrinsics
//===----------------------------------------------------------------------===//
def int_wasm_table_set_externref :
  DefaultAttrsIntrinsic<[], [llvm_table_ty, llvm_i32_ty, llvm_externref_ty],
                        [IntrWriteMem]>;
def int_wasm_table_set_funcref :
  DefaultAttrsIntrinsic<[], [llvm_table_ty, llvm_i32_ty, llvm_funcref_ty],
                        [IntrWriteMem]>;
def int_wasm_table_set_exnref :
  DefaultAttrsIntrinsic<[], [llvm_table_ty, llvm_i32_ty, llvm_exnref_ty],
                        [IntrWriteMem]>;

def int_wasm_table_get_externref :
  DefaultAttrsIntrinsic<[llvm_externref_ty], [llvm_table_ty, llvm_i32_ty],
                        [IntrReadMem]>;
def int_wasm_table_get_funcref :
  DefaultAttrsIntrinsic<[llvm_funcref_ty], [llvm_table_ty, llvm_i32_ty],
                        [IntrReadMem]>;
def int_wasm_table_get_exnref :
  DefaultAttrsIntrinsic<[llvm_exnref_ty], [llvm_table_ty, llvm_i32_ty],
                        [IntrReadMem]>;

// Query the current table size, and increase the current table size.
def int_wasm_table_size :
  DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_table_ty], [IntrReadMem]>;
def int_wasm_table_copy :
  DefaultAttrsIntrinsic<[],
                        [llvm_table_ty, llvm_table_ty, llvm_i32_ty, llvm_i32_ty,
                         llvm_i32_ty], []>;
def int_wasm_table_grow_externref :
  DefaultAttrsIntrinsic<[llvm_i32_ty],
                        [llvm_table_ty, llvm_externref_ty, llvm_i32_ty], []>;
def int_wasm_table_grow_funcref :
  DefaultAttrsIntrinsic<[llvm_i32_ty],
                        [llvm_table_ty, llvm_funcref_ty, llvm_i32_ty], []>;
def int_wasm_table_grow_exnref :
  DefaultAttrsIntrinsic<[llvm_i32_ty],
                        [llvm_table_ty, llvm_exnref_ty, llvm_i32_ty], []>;
def int_wasm_table_fill_externref :
  DefaultAttrsIntrinsic<[],
                        [llvm_table_ty, llvm_i32_ty, llvm_externref_ty,
                         llvm_i32_ty], []>;
def int_wasm_table_fill_funcref :
  DefaultAttrsIntrinsic<[],
                        [llvm_table_ty, llvm_i32_ty, llvm_funcref_ty,
                         llvm_i32_ty], []>;
def int_wasm_table_fill_exnref :
  DefaultAttrsIntrinsic<[],
                        [llvm_table_ty, llvm_i32_ty, llvm_exnref_ty,
                         llvm_i32_ty], []>;

//===----------------------------------------------------------------------===//
// Trapping float-to-int conversions
//===----------------------------------------------------------------------===//

// These don't use default attributes, because they are not willreturn.
def int_wasm_trunc_signed : Intrinsic<[llvm_anyint_ty],
                                      [llvm_anyfloat_ty],
                                      [IntrNoMem]>;
def int_wasm_trunc_unsigned : Intrinsic<[llvm_anyint_ty],
                                        [llvm_anyfloat_ty],
                                        [IntrNoMem]>;

//===----------------------------------------------------------------------===//
// Saturating float-to-int conversions
//===----------------------------------------------------------------------===//

def int_wasm_trunc_saturate_signed :
  DefaultAttrsIntrinsic<[llvm_anyint_ty], [llvm_anyfloat_ty],
                        [IntrNoMem, IntrSpeculatable]>;
def int_wasm_trunc_saturate_unsigned :
  DefaultAttrsIntrinsic<[llvm_anyint_ty], [llvm_anyfloat_ty],
                        [IntrNoMem, IntrSpeculatable]>;

//===----------------------------------------------------------------------===//
// Exception handling intrinsics
//===----------------------------------------------------------------------===//

// throw / rethrow
// The first immediate argument is an index to a tag, which is 0 for C++
// exception. The second argument is the thrown exception pointer.
def int_wasm_throw : Intrinsic<[], [llvm_i32_ty, llvm_ptr_ty],
                               [Throws, IntrNoReturn, ImmArg<ArgIndex<0>>]>;
def int_wasm_rethrow : Intrinsic<[], [], [Throws, IntrNoReturn]>;

// Since wasm does not use landingpad instructions, these instructions return
// exception pointer and selector values until we lower them in WasmEHPrepare.
def int_wasm_get_exception :
  DefaultAttrsIntrinsic<[llvm_ptr_ty], [llvm_token_ty], [IntrHasSideEffects]>;
def int_wasm_get_ehselector :
  DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_token_ty], [IntrHasSideEffects]>;

// wasm.catch returns the pointer to the exception object caught by wasm 'catch'
// instruction. This returns a single pointer, which is the case for C++
// exceptions. The immediate argument is an index to for a tag, which is 0 for
// C++ exceptions.
def int_wasm_catch :
  DefaultAttrsIntrinsic<[llvm_ptr_ty], [llvm_i32_ty],
                        [IntrHasSideEffects, ImmArg<ArgIndex<0>>]>;

// WebAssembly EH must maintain the landingpads in the order assigned to them
// by WasmEHPrepare pass to generate landingpad table in EHStreamer. This is
// used in order to give them the indices in WasmEHPrepare.
def int_wasm_landingpad_index :
  DefaultAttrsIntrinsic<[], [llvm_token_ty, llvm_i32_ty],
                        [IntrNoMem, ImmArg<ArgIndex<1>>]>;

// Returns LSDA address of the current function.
def int_wasm_lsda : DefaultAttrsIntrinsic<[llvm_ptr_ty], [], [IntrNoMem]>;

//===----------------------------------------------------------------------===//
// Atomic intrinsics
//===----------------------------------------------------------------------===//

// wait / notify
// These don't use default attributes, because they are not nosync.
def int_wasm_memory_atomic_wait32 :
  Intrinsic<[llvm_i32_ty],
            [llvm_ptr_ty, llvm_i32_ty, llvm_i64_ty],
            [IntrInaccessibleMemOrArgMemOnly, ReadOnly<ArgIndex<0>>,
             NoCapture<ArgIndex<0>>, IntrHasSideEffects],
            "", [SDNPMemOperand]>;
def int_wasm_memory_atomic_wait64 :
  Intrinsic<[llvm_i32_ty],
            [llvm_ptr_ty, llvm_i64_ty, llvm_i64_ty],
            [IntrInaccessibleMemOrArgMemOnly, ReadOnly<ArgIndex<0>>,
             NoCapture<ArgIndex<0>>, IntrHasSideEffects],
            "", [SDNPMemOperand]>;
def int_wasm_memory_atomic_notify:
  Intrinsic<[llvm_i32_ty], [llvm_ptr_ty, llvm_i32_ty],
            [IntrInaccessibleMemOnly, NoCapture<ArgIndex<0>>,
             IntrHasSideEffects],
            "", [SDNPMemOperand]>;

//===----------------------------------------------------------------------===//
// SIMD intrinsics
//===----------------------------------------------------------------------===//

def int_wasm_swizzle :
  DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                        [llvm_v16i8_ty, llvm_v16i8_ty],
                        [IntrNoMem, IntrSpeculatable]>;
def int_wasm_shuffle :
  DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                        [llvm_v16i8_ty, llvm_v16i8_ty, llvm_i32_ty,
                         llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                         llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                         llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
                         llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
                        [IntrNoMem, IntrSpeculatable,
                         ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<3>>,
                         ImmArg<ArgIndex<4>>, ImmArg<ArgIndex<5>>,
                         ImmArg<ArgIndex<6>>, ImmArg<ArgIndex<7>>,
                         ImmArg<ArgIndex<8>>, ImmArg<ArgIndex<9>>,
                         ImmArg<ArgIndex<10>>, ImmArg<ArgIndex<11>>,
                         ImmArg<ArgIndex<12>>, ImmArg<ArgIndex<13>>,
                         ImmArg<ArgIndex<14>>, ImmArg<ArgIndex<15>>,
                         ImmArg<ArgIndex<16>>, ImmArg<ArgIndex<17>>]>;
def int_wasm_sub_sat_signed :
  DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                        [LLVMMatchType<0>, LLVMMatchType<0>],
                        [IntrNoMem, IntrSpeculatable]>;
def int_wasm_sub_sat_unsigned :
  DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                        [LLVMMatchType<0>, LLVMMatchType<0>],
                        [IntrNoMem, IntrSpeculatable]>;
def int_wasm_avgr_unsigned :
  DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                        [LLVMMatchType<0>, LLVMMatchType<0>],
                        [IntrNoMem, IntrSpeculatable]>;
def int_wasm_bitselect :
  DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                        [LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>],
                        [IntrNoMem, IntrSpeculatable]>;
def int_wasm_anytrue :
  DefaultAttrsIntrinsic<[llvm_i32_ty],
                        [llvm_anyvector_ty],
                        [IntrNoMem, IntrSpeculatable]>;
def int_wasm_alltrue :
  DefaultAttrsIntrinsic<[llvm_i32_ty],
                        [llvm_anyvector_ty],
                        [IntrNoMem, IntrSpeculatable]>;
def int_wasm_bitmask :
  DefaultAttrsIntrinsic<[llvm_i32_ty],
                        [llvm_anyvector_ty],
                        [IntrNoMem, IntrSpeculatable]>;
def int_wasm_dot :
  DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                        [llvm_v8i16_ty, llvm_v8i16_ty],
                        [IntrNoMem, IntrSpeculatable]>;

def int_wasm_narrow_signed :
  DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                        [llvm_anyvector_ty, LLVMMatchType<1>],
                        [IntrNoMem, IntrSpeculatable]>;
def int_wasm_narrow_unsigned :
  DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                        [llvm_anyvector_ty, LLVMMatchType<1>],
                        [IntrNoMem, IntrSpeculatable]>;

def int_wasm_q15mulr_sat_signed :
  DefaultAttrsIntrinsic<[llvm_v8i16_ty],
                        [llvm_v8i16_ty, llvm_v8i16_ty],
                        [IntrNoMem, IntrSpeculatable]>;

def int_wasm_pmin :
  DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                        [LLVMMatchType<0>, LLVMMatchType<0>],
                        [IntrNoMem, IntrSpeculatable]>;
def int_wasm_pmax :
  DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                        [LLVMMatchType<0>, LLVMMatchType<0>],
                        [IntrNoMem, IntrSpeculatable]>;

def int_wasm_extadd_pairwise_signed :
  DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                        [LLVMSubdivide2VectorType<0>],
                        [IntrNoMem, IntrSpeculatable]>;
def int_wasm_extadd_pairwise_unsigned :
  DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                        [LLVMSubdivide2VectorType<0>],
                        [IntrNoMem, IntrSpeculatable]>;

//===----------------------------------------------------------------------===//
// Relaxed SIMD intrinsics (experimental)
//===----------------------------------------------------------------------===//

def int_wasm_relaxed_madd :
  DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                        [LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>],
                        [IntrNoMem, IntrSpeculatable]>;
def int_wasm_relaxed_nmadd :
  DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                        [LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>],
                        [IntrNoMem, IntrSpeculatable]>;

def int_wasm_relaxed_laneselect :
  DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                        [LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>],
                        [IntrNoMem, IntrSpeculatable]>;

def int_wasm_relaxed_swizzle :
  DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                        [llvm_v16i8_ty, llvm_v16i8_ty],
                        [IntrNoMem, IntrSpeculatable]>;

def int_wasm_relaxed_min :
  DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                        [LLVMMatchType<0>, LLVMMatchType<0>],
                        [IntrNoMem, IntrSpeculatable]>;
def int_wasm_relaxed_max :
  DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                        [LLVMMatchType<0>, LLVMMatchType<0>],
                        [IntrNoMem, IntrSpeculatable]>;

def int_wasm_relaxed_trunc_signed:
  DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                        [llvm_v4f32_ty],
                        [IntrNoMem, IntrSpeculatable]>;

def int_wasm_relaxed_trunc_unsigned:
  DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                        [llvm_v4f32_ty],
                        [IntrNoMem, IntrSpeculatable]>;

def int_wasm_relaxed_trunc_signed_zero:
  DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                        [llvm_v2f64_ty],
                        [IntrNoMem, IntrSpeculatable]>;

def int_wasm_relaxed_trunc_unsigned_zero:
  DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                        [llvm_v2f64_ty],
                        [IntrNoMem, IntrSpeculatable]>;

def int_wasm_relaxed_q15mulr_signed:
  DefaultAttrsIntrinsic<[llvm_v8i16_ty],
                        [llvm_v8i16_ty, llvm_v8i16_ty],
                        [IntrNoMem, IntrSpeculatable]>;

def int_wasm_relaxed_dot_i8x16_i7x16_signed:
  DefaultAttrsIntrinsic<[llvm_v8i16_ty],
                        [llvm_v16i8_ty, llvm_v16i8_ty],
                        [IntrNoMem, IntrSpeculatable]>;

def int_wasm_relaxed_dot_i8x16_i7x16_add_signed:
  DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                        [llvm_v16i8_ty, llvm_v16i8_ty, llvm_v4i32_ty],
                        [IntrNoMem, IntrSpeculatable]>;

def int_wasm_relaxed_dot_bf16x8_add_f32:
  DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                        [llvm_v8i16_ty, llvm_v8i16_ty, llvm_v4f32_ty],
                        [IntrNoMem, IntrSpeculatable]>;

//===----------------------------------------------------------------------===//
// Half-precision intrinsics (experimental)
//===----------------------------------------------------------------------===//

// TODO: Replace these intrinsic with normal ISel patterns once the XXX
// instructions are merged to the proposal.
def int_wasm_loadf16_f32:
  Intrinsic<[llvm_float_ty],
            [llvm_ptr_ty],
            [IntrReadMem, IntrArgMemOnly],
             "", [SDNPMemOperand]>;
def int_wasm_storef16_f32:
  Intrinsic<[],
            [llvm_float_ty, llvm_ptr_ty],
            [IntrWriteMem, IntrArgMemOnly],
             "", [SDNPMemOperand]>;
def int_wasm_splat_f16x8:
  DefaultAttrsIntrinsic<[llvm_v8f16_ty],
                        [llvm_float_ty],
                        [IntrNoMem, IntrSpeculatable]>;
def int_wasm_extract_lane_f16x8:
  DefaultAttrsIntrinsic<[llvm_float_ty],
                        [llvm_v8f16_ty, llvm_i32_ty],
                        [IntrNoMem, IntrSpeculatable]>;


//===----------------------------------------------------------------------===//
// Thread-local storage intrinsics
//===----------------------------------------------------------------------===//

def int_wasm_tls_size :
  DefaultAttrsIntrinsic<[llvm_anyint_ty],
                        [],
                        [IntrNoMem, IntrSpeculatable]>;

def int_wasm_tls_align :
  DefaultAttrsIntrinsic<[llvm_anyint_ty],
                        [],
                        [IntrNoMem, IntrSpeculatable]>;

def int_wasm_tls_base :
  DefaultAttrsIntrinsic<[llvm_ptr_ty],
                        [],
                        [IntrReadMem]>;

} // TargetPrefix = "wasm"
