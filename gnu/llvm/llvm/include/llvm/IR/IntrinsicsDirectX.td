//===- IntrinsicsDirectX.td - Defines DirectX intrinsics ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines all of the DirectX-specific intrinsics.
//
//===----------------------------------------------------------------------===//

let TargetPrefix = "dx" in {

def int_dx_thread_id : Intrinsic<[llvm_i32_ty], [llvm_i32_ty], [IntrNoMem, IntrWillReturn]>;
def int_dx_group_id : Intrinsic<[llvm_i32_ty], [llvm_i32_ty], [IntrNoMem, IntrWillReturn]>;
def int_dx_thread_id_in_group : Intrinsic<[llvm_i32_ty], [llvm_i32_ty], [IntrNoMem, IntrWillReturn]>;
def int_dx_flattened_thread_id_in_group : Intrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrWillReturn]>;

def int_dx_create_handle : ClangBuiltin<"__builtin_hlsl_create_handle">,
    Intrinsic<[ llvm_ptr_ty ], [llvm_i8_ty], [IntrWillReturn]>;

def int_dx_all : DefaultAttrsIntrinsic<[llvm_i1_ty], [llvm_any_ty]>;
def int_dx_any : DefaultAttrsIntrinsic<[llvm_i1_ty], [llvm_any_ty]>;
def int_dx_clamp : DefaultAttrsIntrinsic<[llvm_any_ty], [LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>]>;
def int_dx_uclamp : DefaultAttrsIntrinsic<[llvm_anyint_ty], [LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>]>; 

def int_dx_dot2 : 
    Intrinsic<[LLVMVectorElementType<0>], 
    [llvm_anyfloat_ty, LLVMScalarOrSameVectorWidth<0, LLVMVectorElementType<0>>],
    [IntrNoMem, IntrWillReturn, Commutative] >;
def int_dx_dot3 : 
    Intrinsic<[LLVMVectorElementType<0>], 
    [llvm_anyfloat_ty, LLVMScalarOrSameVectorWidth<0, LLVMVectorElementType<0>>],
    [IntrNoMem, IntrWillReturn, Commutative] >;
def int_dx_dot4 : 
    Intrinsic<[LLVMVectorElementType<0>], 
    [llvm_anyfloat_ty, LLVMScalarOrSameVectorWidth<0, LLVMVectorElementType<0>>],
    [IntrNoMem, IntrWillReturn, Commutative] >;
def int_dx_sdot : 
    Intrinsic<[LLVMVectorElementType<0>], 
    [llvm_anyint_ty, LLVMScalarOrSameVectorWidth<0, LLVMVectorElementType<0>>],
    [IntrNoMem, IntrWillReturn, Commutative] >;
def int_dx_udot : 
    Intrinsic<[LLVMVectorElementType<0>], 
    [llvm_anyint_ty, LLVMScalarOrSameVectorWidth<0, LLVMVectorElementType<0>>],
    [IntrNoMem, IntrWillReturn, Commutative] >;

def int_dx_frac  : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;

def int_dx_isinf :
    DefaultAttrsIntrinsic<[LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>],
    [llvm_anyfloat_ty]>;

def int_dx_lerp : Intrinsic<[LLVMMatchType<0>], [llvm_anyfloat_ty, LLVMMatchType<0>,LLVMMatchType<0>], 
    [IntrNoMem, IntrWillReturn] >;

def int_dx_imad : DefaultAttrsIntrinsic<[llvm_anyint_ty], [LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>]>;
def int_dx_umad : DefaultAttrsIntrinsic<[llvm_anyint_ty], [LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>]>;
def int_dx_rcp  : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
def int_dx_rsqrt  : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>]>;
}
