//===- IntrinsicsAARCH64.td - Defines AARCH64 intrinsics ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines all of the AARCH64-specific intrinsics.
//
//===----------------------------------------------------------------------===//

let TargetPrefix = "aarch64" in {

def int_aarch64_ldxr : Intrinsic<[llvm_i64_ty], [llvm_anyptr_ty],
                                 [IntrNoFree, IntrWillReturn]>;
def int_aarch64_ldaxr : Intrinsic<[llvm_i64_ty], [llvm_anyptr_ty],
                                  [IntrNoFree, IntrWillReturn]>;
def int_aarch64_stxr : Intrinsic<[llvm_i32_ty], [llvm_i64_ty, llvm_anyptr_ty],
                                 [IntrNoFree, IntrWillReturn]>;
def int_aarch64_stlxr : Intrinsic<[llvm_i32_ty], [llvm_i64_ty, llvm_anyptr_ty],
                                  [IntrNoFree, IntrWillReturn]>;

def int_aarch64_ldxp : Intrinsic<[llvm_i64_ty, llvm_i64_ty], [llvm_ptr_ty],
                                 [IntrNoFree, IntrWillReturn]>;
def int_aarch64_ldaxp : Intrinsic<[llvm_i64_ty, llvm_i64_ty], [llvm_ptr_ty],
                                  [IntrNoFree, IntrWillReturn]>;
def int_aarch64_stxp : Intrinsic<[llvm_i32_ty],
                               [llvm_i64_ty, llvm_i64_ty, llvm_ptr_ty],
                               [IntrNoFree, IntrWillReturn]>;
def int_aarch64_stlxp : Intrinsic<[llvm_i32_ty],
                                  [llvm_i64_ty, llvm_i64_ty, llvm_ptr_ty],
                                  [IntrNoFree, IntrWillReturn]>;

def int_aarch64_clrex : Intrinsic<[]>;

def int_aarch64_sdiv : DefaultAttrsIntrinsic<[llvm_anyint_ty], [LLVMMatchType<0>,
                                LLVMMatchType<0>], [IntrNoMem]>;
def int_aarch64_udiv : DefaultAttrsIntrinsic<[llvm_anyint_ty], [LLVMMatchType<0>,
                                LLVMMatchType<0>], [IntrNoMem]>;

def int_aarch64_fjcvtzs : DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_double_ty], [IntrNoMem]>;

def int_aarch64_cls: DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty], [IntrNoMem]>;
def int_aarch64_cls64: DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i64_ty], [IntrNoMem]>;

def int_aarch64_frint32z
    : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ], [ LLVMMatchType<0> ],
                            [ IntrNoMem ]>;
def int_aarch64_frint64z
    : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ], [ LLVMMatchType<0> ],
                            [ IntrNoMem ]>;
def int_aarch64_frint32x
    : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ], [ LLVMMatchType<0> ],
                            [ IntrNoMem ]>;
def int_aarch64_frint64x
    : DefaultAttrsIntrinsic<[ llvm_anyfloat_ty ], [ LLVMMatchType<0> ],
                            [ IntrNoMem ]>;


//===----------------------------------------------------------------------===//
// HINT

def int_aarch64_hint : DefaultAttrsIntrinsic<[], [llvm_i32_ty]>;

def int_aarch64_break : Intrinsic<[], [llvm_i32_ty],
    [IntrNoMem, IntrHasSideEffects, IntrNoReturn, IntrCold, ImmArg<ArgIndex<0>>]>;

def int_aarch64_hlt : Intrinsic<[], [llvm_i32_ty],
    [IntrNoMem, IntrHasSideEffects, IntrNoReturn, IntrCold, ImmArg<ArgIndex<0>>]>;

def int_aarch64_prefetch : Intrinsic<[],
    [llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
    [IntrInaccessibleMemOrArgMemOnly, IntrWillReturn, ReadOnly<ArgIndex<0>>,
     ImmArg<ArgIndex<1>>, ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<3>>, ImmArg<ArgIndex<4>>
     ]>,
    ClangBuiltin<"__builtin_arm_prefetch">;

//===----------------------------------------------------------------------===//
// Data Barrier Instructions

def int_aarch64_dmb : ClangBuiltin<"__builtin_arm_dmb">, MSBuiltin<"__dmb">,
                      Intrinsic<[], [llvm_i32_ty], [IntrNoFree, IntrWillReturn]>;
def int_aarch64_dsb : ClangBuiltin<"__builtin_arm_dsb">, MSBuiltin<"__dsb">,
                      Intrinsic<[], [llvm_i32_ty], [IntrNoFree, IntrWillReturn]>;
def int_aarch64_isb : ClangBuiltin<"__builtin_arm_isb">, MSBuiltin<"__isb">,
                      Intrinsic<[], [llvm_i32_ty], [IntrNoFree, IntrWillReturn]>;

// A space-consuming intrinsic primarily for testing block and jump table
// placements. The first argument is the number of bytes this "instruction"
// takes up, the second and return value are essentially chains, used to force
// ordering during ISel.
def int_aarch64_space : DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i32_ty, llvm_i64_ty], []>;

//===----------------------------------------------------------------------===//
// Guarded Control Stack

def int_aarch64_chkfeat : ClangBuiltin<"__builtin_arm_chkfeat">,
                          DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty],
                                                [IntrNoMem]>;

// FIXME: This should be marked as [IntrReadMem, IntrHasSideEffects], as it has
// the side-effect of updating gcspr, but this combination doesn't work
// correctly.
def int_aarch64_gcspopm : ClangBuiltin<"__builtin_arm_gcspopm">,
                          DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty],
                                                []>;

def int_aarch64_gcsss : ClangBuiltin<"__builtin_arm_gcsss">,
                        DefaultAttrsIntrinsic<[llvm_ptr_ty], [llvm_ptr_ty], []>;

}

//===----------------------------------------------------------------------===//
// Advanced SIMD (NEON)

let TargetPrefix = "aarch64" in {  // All intrinsics start with "llvm.aarch64.".
  class AdvSIMD_2Scalar_Float_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>, LLVMMatchType<0>],
                [IntrNoMem]>;

  class AdvSIMD_FPToIntRounding_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyint_ty], [llvm_anyfloat_ty], [IntrNoMem]>;

  class AdvSIMD_1IntArg_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyint_ty], [LLVMMatchType<0>], [IntrNoMem]>;
  class AdvSIMD_1FloatArg_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem]>;
  class AdvSIMD_1VectorArg_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty], [LLVMMatchType<0>], [IntrNoMem]>;
  class AdvSIMD_1VectorArg_Expand_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty], [llvm_anyvector_ty], [IntrNoMem]>;
  class AdvSIMD_1VectorArg_Long_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty], [LLVMTruncatedType<0>], [IntrNoMem]>;
  class AdvSIMD_1IntArg_Narrow_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_any_ty], [llvm_any_ty], [IntrNoMem]>;
  class AdvSIMD_1VectorArg_Narrow_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyint_ty], [LLVMExtendedType<0>], [IntrNoMem]>;
  class AdvSIMD_1VectorArg_Int_Across_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyint_ty], [llvm_anyvector_ty], [IntrNoMem]>;
  class AdvSIMD_1VectorArg_Float_Across_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [llvm_anyvector_ty], [IntrNoMem]>;

  class AdvSIMD_2IntArg_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyint_ty], [LLVMMatchType<0>, LLVMMatchType<0>],
                [IntrNoMem]>;
  class AdvSIMD_2FloatArg_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [LLVMMatchType<0>, LLVMMatchType<0>],
                [IntrNoMem]>;
  class AdvSIMD_2VectorArg_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty], [LLVMMatchType<0>, LLVMMatchType<0>],
                [IntrNoMem]>;
  class AdvSIMD_2VectorArg_Compare_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty], [llvm_anyvector_ty, LLVMMatchType<1>],
                [IntrNoMem]>;
  class AdvSIMD_2Arg_FloatCompare_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyint_ty], [llvm_anyfloat_ty, LLVMMatchType<1>],
                [IntrNoMem]>;
  class AdvSIMD_2VectorArg_Long_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMTruncatedType<0>, LLVMTruncatedType<0>],
                [IntrNoMem]>;
  class AdvSIMD_2VectorArg_Wide_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>, LLVMTruncatedType<0>],
                [IntrNoMem]>;
  class AdvSIMD_2VectorArg_Narrow_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMExtendedType<0>, LLVMExtendedType<0>],
                [IntrNoMem]>;
  class AdvSIMD_2Arg_Scalar_Narrow_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyint_ty],
                [LLVMExtendedType<0>, llvm_i32_ty],
                [IntrNoMem]>;
  class AdvSIMD_2VectorArg_Scalar_Expand_BySize_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [llvm_anyvector_ty],
                [IntrNoMem]>;
  class AdvSIMD_2VectorArg_Scalar_Wide_BySize_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMTruncatedType<0>],
                [IntrNoMem]>;
  class AdvSIMD_2VectorArg_Scalar_Wide_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMTruncatedType<0>, llvm_i32_ty],
                [IntrNoMem]>;
  class AdvSIMD_2VectorArg_Tied_Narrow_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMHalfElementsVectorType<0>, llvm_anyvector_ty],
                [IntrNoMem]>;
  class AdvSIMD_2VectorArg_Lane_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyint_ty],
                [LLVMMatchType<0>, llvm_anyint_ty, llvm_i32_ty],
                [IntrNoMem]>;

  class AdvSIMD_3IntArg_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyint_ty],
                [LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>],
                [IntrNoMem]>;
  class AdvSIMD_3VectorArg_Intrinsic
      : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
               [LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>],
               [IntrNoMem]>;
  class AdvSIMD_3VectorArg_Scalar_Intrinsic
      : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
               [LLVMMatchType<0>, LLVMMatchType<0>, llvm_i32_ty],
               [IntrNoMem]>;
  class AdvSIMD_3VectorArg_Tied_Narrow_Intrinsic
      : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
               [LLVMHalfElementsVectorType<0>, llvm_anyvector_ty,
                LLVMMatchType<1>], [IntrNoMem]>;
  class AdvSIMD_3VectorArg_Scalar_Tied_Narrow_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMHalfElementsVectorType<0>, llvm_anyvector_ty, llvm_i32_ty],
                [IntrNoMem]>;
  class AdvSIMD_CvtFxToFP_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [llvm_anyint_ty, llvm_i32_ty],
                [IntrNoMem]>;
  class AdvSIMD_CvtFPToFx_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyint_ty], [llvm_anyfloat_ty, llvm_i32_ty],
                [IntrNoMem]>;

  class AdvSIMD_1Arg_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_any_ty], [LLVMMatchType<0>], [IntrNoMem]>;

  class AdvSIMD_Dot_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>, llvm_anyvector_ty, LLVMMatchType<1>],
                [IntrNoMem]>;

  class AdvSIMD_FP16FML_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>, llvm_anyvector_ty, LLVMMatchType<1>],
                [IntrNoMem]>;

  class AdvSIMD_MatMul_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>, llvm_anyvector_ty, LLVMMatchType<1>],
                [IntrNoMem]>;

  class AdvSIMD_FML_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>, llvm_anyvector_ty, LLVMMatchType<1>],
                [IntrNoMem]>;

  class AdvSIMD_BF16FML_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                [llvm_v4f32_ty, llvm_v8bf16_ty, llvm_v8bf16_ty],
                [IntrNoMem]>;
}

// Arithmetic ops

let TargetPrefix = "aarch64", IntrProperties = [IntrNoMem] in {
  // Vector Add Across Lanes
  def int_aarch64_neon_saddv : AdvSIMD_1VectorArg_Int_Across_Intrinsic;
  def int_aarch64_neon_uaddv : AdvSIMD_1VectorArg_Int_Across_Intrinsic;
  def int_aarch64_neon_faddv : AdvSIMD_1VectorArg_Float_Across_Intrinsic;

  // Vector Long Add Across Lanes
  def int_aarch64_neon_saddlv : AdvSIMD_1VectorArg_Int_Across_Intrinsic;
  def int_aarch64_neon_uaddlv : AdvSIMD_1VectorArg_Int_Across_Intrinsic;

  // Vector Halving Add
  def int_aarch64_neon_shadd : AdvSIMD_2VectorArg_Intrinsic;
  def int_aarch64_neon_uhadd : AdvSIMD_2VectorArg_Intrinsic;

  // Vector Rounding Halving Add
  def int_aarch64_neon_srhadd : AdvSIMD_2VectorArg_Intrinsic;
  def int_aarch64_neon_urhadd : AdvSIMD_2VectorArg_Intrinsic;

  // Vector Saturating Add
  def int_aarch64_neon_sqadd : AdvSIMD_2IntArg_Intrinsic;
  def int_aarch64_neon_suqadd : AdvSIMD_2IntArg_Intrinsic;
  def int_aarch64_neon_usqadd : AdvSIMD_2IntArg_Intrinsic;
  def int_aarch64_neon_uqadd : AdvSIMD_2IntArg_Intrinsic;

  // Vector Add High-Half
  // FIXME: this is a legacy intrinsic for aarch64_simd.h. Remove it when that
  // header is no longer supported.
  def int_aarch64_neon_addhn : AdvSIMD_2VectorArg_Narrow_Intrinsic;

  // Vector Rounding Add High-Half
  def int_aarch64_neon_raddhn : AdvSIMD_2VectorArg_Narrow_Intrinsic;

  // Vector Saturating Doubling Multiply High
  def int_aarch64_neon_sqdmulh : AdvSIMD_2IntArg_Intrinsic;
  def int_aarch64_neon_sqdmulh_lane : AdvSIMD_2VectorArg_Lane_Intrinsic;
  def int_aarch64_neon_sqdmulh_laneq : AdvSIMD_2VectorArg_Lane_Intrinsic;

  // Vector Saturating Rounding Doubling Multiply High
  def int_aarch64_neon_sqrdmulh : AdvSIMD_2IntArg_Intrinsic;
  def int_aarch64_neon_sqrdmulh_lane : AdvSIMD_2VectorArg_Lane_Intrinsic;
  def int_aarch64_neon_sqrdmulh_laneq : AdvSIMD_2VectorArg_Lane_Intrinsic;

  def int_aarch64_neon_sqrdmlah : AdvSIMD_3IntArg_Intrinsic;
  def int_aarch64_neon_sqrdmlsh : AdvSIMD_3IntArg_Intrinsic;

  // Vector Polynominal Multiply
  def int_aarch64_neon_pmul : AdvSIMD_2VectorArg_Intrinsic;

  // Vector Long Multiply
  def int_aarch64_neon_smull : AdvSIMD_2VectorArg_Long_Intrinsic;
  def int_aarch64_neon_umull : AdvSIMD_2VectorArg_Long_Intrinsic;
  def int_aarch64_neon_pmull : AdvSIMD_2VectorArg_Long_Intrinsic;

  // 64-bit polynomial multiply really returns an i128, which is not legal. Fake
  // it with a v16i8.
  def int_aarch64_neon_pmull64 :
        DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_i64_ty, llvm_i64_ty], [IntrNoMem]>;

  // Vector Extending Multiply
  def int_aarch64_neon_fmulx : AdvSIMD_2FloatArg_Intrinsic {
    let IntrProperties = [IntrNoMem, Commutative];
  }

  // Vector Saturating Doubling Long Multiply
  def int_aarch64_neon_sqdmull : AdvSIMD_2VectorArg_Long_Intrinsic;
  def int_aarch64_neon_sqdmulls_scalar
    : DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem]>;

  // Vector Halving Subtract
  def int_aarch64_neon_shsub : AdvSIMD_2VectorArg_Intrinsic;
  def int_aarch64_neon_uhsub : AdvSIMD_2VectorArg_Intrinsic;

  // Vector Saturating Subtract
  def int_aarch64_neon_sqsub : AdvSIMD_2IntArg_Intrinsic;
  def int_aarch64_neon_uqsub : AdvSIMD_2IntArg_Intrinsic;

  // Vector Subtract High-Half
  // FIXME: this is a legacy intrinsic for aarch64_simd.h. Remove it when that
  // header is no longer supported.
  def int_aarch64_neon_subhn : AdvSIMD_2VectorArg_Narrow_Intrinsic;

  // Vector Rounding Subtract High-Half
  def int_aarch64_neon_rsubhn : AdvSIMD_2VectorArg_Narrow_Intrinsic;

  // Vector Compare Absolute Greater-than-or-equal
  def int_aarch64_neon_facge : AdvSIMD_2Arg_FloatCompare_Intrinsic;

  // Vector Compare Absolute Greater-than
  def int_aarch64_neon_facgt : AdvSIMD_2Arg_FloatCompare_Intrinsic;

  // Vector Absolute Difference
  def int_aarch64_neon_sabd : AdvSIMD_2VectorArg_Intrinsic;
  def int_aarch64_neon_uabd : AdvSIMD_2VectorArg_Intrinsic;
  def int_aarch64_neon_fabd : AdvSIMD_2VectorArg_Intrinsic;

  // Scalar Absolute Difference
  def int_aarch64_sisd_fabd : AdvSIMD_2Scalar_Float_Intrinsic;

  // Vector Max
  def int_aarch64_neon_smax : AdvSIMD_2VectorArg_Intrinsic;
  def int_aarch64_neon_umax : AdvSIMD_2VectorArg_Intrinsic;
  def int_aarch64_neon_fmax : AdvSIMD_2FloatArg_Intrinsic;
  def int_aarch64_neon_fmaxnmp : AdvSIMD_2VectorArg_Intrinsic;

  // Vector Max Across Lanes
  def int_aarch64_neon_smaxv : AdvSIMD_1VectorArg_Int_Across_Intrinsic;
  def int_aarch64_neon_umaxv : AdvSIMD_1VectorArg_Int_Across_Intrinsic;
  def int_aarch64_neon_fmaxv : AdvSIMD_1VectorArg_Float_Across_Intrinsic;
  def int_aarch64_neon_fmaxnmv : AdvSIMD_1VectorArg_Float_Across_Intrinsic;

  // Vector Min
  def int_aarch64_neon_smin : AdvSIMD_2VectorArg_Intrinsic;
  def int_aarch64_neon_umin : AdvSIMD_2VectorArg_Intrinsic;
  def int_aarch64_neon_fmin : AdvSIMD_2FloatArg_Intrinsic;
  def int_aarch64_neon_fminnmp : AdvSIMD_2VectorArg_Intrinsic;

  // Vector Min/Max Number
  def int_aarch64_neon_fminnm : AdvSIMD_2FloatArg_Intrinsic;
  def int_aarch64_neon_fmaxnm : AdvSIMD_2FloatArg_Intrinsic;

  // Vector Min Across Lanes
  def int_aarch64_neon_sminv : AdvSIMD_1VectorArg_Int_Across_Intrinsic;
  def int_aarch64_neon_uminv : AdvSIMD_1VectorArg_Int_Across_Intrinsic;
  def int_aarch64_neon_fminv : AdvSIMD_1VectorArg_Float_Across_Intrinsic;
  def int_aarch64_neon_fminnmv : AdvSIMD_1VectorArg_Float_Across_Intrinsic;

  // Pairwise Add
  def int_aarch64_neon_addp : AdvSIMD_2VectorArg_Intrinsic;
  def int_aarch64_neon_faddp : AdvSIMD_2VectorArg_Intrinsic;

  // Long Pairwise Add
  // FIXME: In theory, we shouldn't need intrinsics for saddlp or
  // uaddlp, but tblgen's type inference currently can't handle the
  // pattern fragments this ends up generating.
  def int_aarch64_neon_saddlp : AdvSIMD_1VectorArg_Expand_Intrinsic;
  def int_aarch64_neon_uaddlp : AdvSIMD_1VectorArg_Expand_Intrinsic;

  // Folding Maximum
  def int_aarch64_neon_smaxp : AdvSIMD_2VectorArg_Intrinsic;
  def int_aarch64_neon_umaxp : AdvSIMD_2VectorArg_Intrinsic;
  def int_aarch64_neon_fmaxp : AdvSIMD_2VectorArg_Intrinsic;

  // Folding Minimum
  def int_aarch64_neon_sminp : AdvSIMD_2VectorArg_Intrinsic;
  def int_aarch64_neon_uminp : AdvSIMD_2VectorArg_Intrinsic;
  def int_aarch64_neon_fminp : AdvSIMD_2VectorArg_Intrinsic;

  // Reciprocal Estimate/Step
  def int_aarch64_neon_frecps : AdvSIMD_2FloatArg_Intrinsic;
  def int_aarch64_neon_frsqrts : AdvSIMD_2FloatArg_Intrinsic;

  // Reciprocal Exponent
  def int_aarch64_neon_frecpx : AdvSIMD_1FloatArg_Intrinsic;

  // Vector Saturating Shift Left
  def int_aarch64_neon_sqshl : AdvSIMD_2IntArg_Intrinsic;
  def int_aarch64_neon_uqshl : AdvSIMD_2IntArg_Intrinsic;

  // Vector Rounding Shift Left
  def int_aarch64_neon_srshl : AdvSIMD_2IntArg_Intrinsic;
  def int_aarch64_neon_urshl : AdvSIMD_2IntArg_Intrinsic;

  // Vector Saturating Rounding Shift Left
  def int_aarch64_neon_sqrshl : AdvSIMD_2IntArg_Intrinsic;
  def int_aarch64_neon_uqrshl : AdvSIMD_2IntArg_Intrinsic;

  // Vector Signed->Unsigned Shift Left by Constant
  def int_aarch64_neon_sqshlu : AdvSIMD_2IntArg_Intrinsic;

  // Vector Signed->Unsigned Narrowing Saturating Shift Right by Constant
  def int_aarch64_neon_sqshrun : AdvSIMD_2Arg_Scalar_Narrow_Intrinsic;

  // Vector Signed->Unsigned Rounding Narrowing Saturating Shift Right by Const
  def int_aarch64_neon_sqrshrun : AdvSIMD_2Arg_Scalar_Narrow_Intrinsic;

  // Vector Narrowing Shift Right by Constant
  def int_aarch64_neon_sqshrn : AdvSIMD_2Arg_Scalar_Narrow_Intrinsic;
  def int_aarch64_neon_uqshrn : AdvSIMD_2Arg_Scalar_Narrow_Intrinsic;

  // Vector Rounding Narrowing Shift Right by Constant
  def int_aarch64_neon_rshrn : AdvSIMD_2Arg_Scalar_Narrow_Intrinsic;

  // Vector Rounding Narrowing Saturating Shift Right by Constant
  def int_aarch64_neon_sqrshrn : AdvSIMD_2Arg_Scalar_Narrow_Intrinsic;
  def int_aarch64_neon_uqrshrn : AdvSIMD_2Arg_Scalar_Narrow_Intrinsic;

  // Vector Shift Left
  def int_aarch64_neon_sshl : AdvSIMD_2IntArg_Intrinsic;
  def int_aarch64_neon_ushl : AdvSIMD_2IntArg_Intrinsic;

  // Vector Widening Shift Left by Constant
  def int_aarch64_neon_shll : AdvSIMD_2VectorArg_Scalar_Wide_BySize_Intrinsic;
  def int_aarch64_neon_sshll : AdvSIMD_2VectorArg_Scalar_Wide_Intrinsic;
  def int_aarch64_neon_ushll : AdvSIMD_2VectorArg_Scalar_Wide_Intrinsic;

  // Vector Shift Right by Constant and Insert
  def int_aarch64_neon_vsri : AdvSIMD_3VectorArg_Scalar_Intrinsic;

  // Vector Shift Left by Constant and Insert
  def int_aarch64_neon_vsli : AdvSIMD_3VectorArg_Scalar_Intrinsic;

  // Vector Saturating Narrow
  def int_aarch64_neon_scalar_sqxtn: AdvSIMD_1IntArg_Narrow_Intrinsic;
  def int_aarch64_neon_scalar_uqxtn : AdvSIMD_1IntArg_Narrow_Intrinsic;
  def int_aarch64_neon_sqxtn : AdvSIMD_1VectorArg_Narrow_Intrinsic;
  def int_aarch64_neon_uqxtn : AdvSIMD_1VectorArg_Narrow_Intrinsic;

  // Vector Saturating Extract and Unsigned Narrow
  def int_aarch64_neon_scalar_sqxtun : AdvSIMD_1IntArg_Narrow_Intrinsic;
  def int_aarch64_neon_sqxtun : AdvSIMD_1VectorArg_Narrow_Intrinsic;

  // Vector Absolute Value
  def int_aarch64_neon_abs : AdvSIMD_1Arg_Intrinsic;

  // Vector Saturating Absolute Value
  def int_aarch64_neon_sqabs : AdvSIMD_1IntArg_Intrinsic;

  // Vector Saturating Negation
  def int_aarch64_neon_sqneg : AdvSIMD_1IntArg_Intrinsic;

  // Vector Count Leading Sign Bits
  def int_aarch64_neon_cls : AdvSIMD_1VectorArg_Intrinsic;

  // Vector Reciprocal Estimate
  def int_aarch64_neon_urecpe : AdvSIMD_1VectorArg_Intrinsic;
  def int_aarch64_neon_frecpe : AdvSIMD_1FloatArg_Intrinsic;

  // Vector Square Root Estimate
  def int_aarch64_neon_ursqrte : AdvSIMD_1VectorArg_Intrinsic;
  def int_aarch64_neon_frsqrte : AdvSIMD_1FloatArg_Intrinsic;

  // Vector Conversions Between Half-Precision and Single-Precision.
  def int_aarch64_neon_vcvtfp2hf
    : DefaultAttrsIntrinsic<[llvm_v4i16_ty], [llvm_v4f32_ty], [IntrNoMem]>;
  def int_aarch64_neon_vcvthf2fp
    : DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4i16_ty], [IntrNoMem]>;

  // Vector Conversions Between Floating-point and Fixed-point.
  def int_aarch64_neon_vcvtfp2fxs : AdvSIMD_CvtFPToFx_Intrinsic;
  def int_aarch64_neon_vcvtfp2fxu : AdvSIMD_CvtFPToFx_Intrinsic;
  def int_aarch64_neon_vcvtfxs2fp : AdvSIMD_CvtFxToFP_Intrinsic;
  def int_aarch64_neon_vcvtfxu2fp : AdvSIMD_CvtFxToFP_Intrinsic;

  // Vector FP->Int Conversions
  def int_aarch64_neon_fcvtas : AdvSIMD_FPToIntRounding_Intrinsic;
  def int_aarch64_neon_fcvtau : AdvSIMD_FPToIntRounding_Intrinsic;
  def int_aarch64_neon_fcvtms : AdvSIMD_FPToIntRounding_Intrinsic;
  def int_aarch64_neon_fcvtmu : AdvSIMD_FPToIntRounding_Intrinsic;
  def int_aarch64_neon_fcvtns : AdvSIMD_FPToIntRounding_Intrinsic;
  def int_aarch64_neon_fcvtnu : AdvSIMD_FPToIntRounding_Intrinsic;
  def int_aarch64_neon_fcvtps : AdvSIMD_FPToIntRounding_Intrinsic;
  def int_aarch64_neon_fcvtpu : AdvSIMD_FPToIntRounding_Intrinsic;
  def int_aarch64_neon_fcvtzs : AdvSIMD_FPToIntRounding_Intrinsic;
  def int_aarch64_neon_fcvtzu : AdvSIMD_FPToIntRounding_Intrinsic;

  // v8.5-A Vector FP Rounding
  def int_aarch64_neon_frint32x : AdvSIMD_1FloatArg_Intrinsic;
  def int_aarch64_neon_frint32z : AdvSIMD_1FloatArg_Intrinsic;
  def int_aarch64_neon_frint64x : AdvSIMD_1FloatArg_Intrinsic;
  def int_aarch64_neon_frint64z : AdvSIMD_1FloatArg_Intrinsic;

  // Scalar FP->Int conversions

  // Vector FP Inexact Narrowing
  def int_aarch64_neon_fcvtxn : AdvSIMD_1VectorArg_Expand_Intrinsic;

  // Scalar FP Inexact Narrowing
  def int_aarch64_sisd_fcvtxn : DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_double_ty],
                                        [IntrNoMem]>;

  // v8.2-A Dot Product
  def int_aarch64_neon_udot : AdvSIMD_Dot_Intrinsic;
  def int_aarch64_neon_sdot : AdvSIMD_Dot_Intrinsic;

  // v8.6-A Matrix Multiply Intrinsics
  def int_aarch64_neon_ummla : AdvSIMD_MatMul_Intrinsic;
  def int_aarch64_neon_smmla : AdvSIMD_MatMul_Intrinsic;
  def int_aarch64_neon_usmmla : AdvSIMD_MatMul_Intrinsic;
  def int_aarch64_neon_usdot : AdvSIMD_Dot_Intrinsic;
  def int_aarch64_neon_bfdot : AdvSIMD_Dot_Intrinsic;
  def int_aarch64_neon_bfmmla
    : DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                [llvm_v4f32_ty, llvm_v8bf16_ty, llvm_v8bf16_ty],
                [IntrNoMem]>;
  def int_aarch64_neon_bfmlalb : AdvSIMD_BF16FML_Intrinsic;
  def int_aarch64_neon_bfmlalt : AdvSIMD_BF16FML_Intrinsic;


  // v8.6-A Bfloat Intrinsics
  def int_aarch64_neon_bfcvt
    : DefaultAttrsIntrinsic<[llvm_bfloat_ty], [llvm_float_ty], [IntrNoMem]>;
  def int_aarch64_neon_bfcvtn
    : DefaultAttrsIntrinsic<[llvm_v8bf16_ty], [llvm_v4f32_ty], [IntrNoMem]>;
  def int_aarch64_neon_bfcvtn2
    : DefaultAttrsIntrinsic<[llvm_v8bf16_ty],
                [llvm_v8bf16_ty, llvm_v4f32_ty],
                [IntrNoMem]>;

  // v8.2-A FP16 Fused Multiply-Add Long
  def int_aarch64_neon_fmlal : AdvSIMD_FP16FML_Intrinsic;
  def int_aarch64_neon_fmlsl : AdvSIMD_FP16FML_Intrinsic;
  def int_aarch64_neon_fmlal2 : AdvSIMD_FP16FML_Intrinsic;
  def int_aarch64_neon_fmlsl2 : AdvSIMD_FP16FML_Intrinsic;

  // v8.3-A Floating-point complex add
  def int_aarch64_neon_vcadd_rot90  : AdvSIMD_2VectorArg_Intrinsic;
  def int_aarch64_neon_vcadd_rot270 : AdvSIMD_2VectorArg_Intrinsic;

  def int_aarch64_neon_vcmla_rot0   : AdvSIMD_3VectorArg_Intrinsic;
  def int_aarch64_neon_vcmla_rot90  : AdvSIMD_3VectorArg_Intrinsic;
  def int_aarch64_neon_vcmla_rot180 : AdvSIMD_3VectorArg_Intrinsic;
  def int_aarch64_neon_vcmla_rot270 : AdvSIMD_3VectorArg_Intrinsic;
}

let TargetPrefix = "aarch64" in {  // All intrinsics start with "llvm.aarch64.".
  class AdvSIMD_2Vector2Index_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [llvm_anyvector_ty, llvm_i64_ty, LLVMMatchType<0>, llvm_i64_ty],
                [IntrNoMem]>;
}

// Vector element to element moves
def int_aarch64_neon_vcopy_lane: AdvSIMD_2Vector2Index_Intrinsic;

let TargetPrefix = "aarch64" in {  // All intrinsics start with "llvm.aarch64.".
  class AdvSIMD_1Vec_Load_Intrinsic
      : DefaultAttrsIntrinsic<[llvm_anyvector_ty], [llvm_anyptr_ty],
                  [IntrReadMem, IntrArgMemOnly]>;
  class AdvSIMD_1Vec_Store_Lane_Intrinsic
    : DefaultAttrsIntrinsic<[], [llvm_anyvector_ty, llvm_i64_ty, llvm_anyptr_ty],
                [IntrArgMemOnly, NoCapture<ArgIndex<2>>]>;

  class AdvSIMD_2Vec_Load_Intrinsic
    : DefaultAttrsIntrinsic<[LLVMMatchType<0>, llvm_anyvector_ty],
                [llvm_anyptr_ty],
                [IntrReadMem, IntrArgMemOnly]>;
  class AdvSIMD_2Vec_Load_Lane_Intrinsic
    : DefaultAttrsIntrinsic<[LLVMMatchType<0>, LLVMMatchType<0>],
                [LLVMMatchType<0>, llvm_anyvector_ty,
                 llvm_i64_ty, llvm_anyptr_ty],
                [IntrReadMem, IntrArgMemOnly]>;
  class AdvSIMD_2Vec_Store_Intrinsic
    : DefaultAttrsIntrinsic<[], [llvm_anyvector_ty, LLVMMatchType<0>,
                     llvm_anyptr_ty],
                [IntrArgMemOnly, NoCapture<ArgIndex<2>>]>;
  class AdvSIMD_2Vec_Store_Lane_Intrinsic
    : DefaultAttrsIntrinsic<[], [llvm_anyvector_ty, LLVMMatchType<0>,
                 llvm_i64_ty, llvm_anyptr_ty],
                [IntrArgMemOnly, NoCapture<ArgIndex<3>>]>;

  class AdvSIMD_3Vec_Load_Intrinsic
    : DefaultAttrsIntrinsic<[LLVMMatchType<0>, LLVMMatchType<0>, llvm_anyvector_ty],
                [llvm_anyptr_ty],
                [IntrReadMem, IntrArgMemOnly]>;
  class AdvSIMD_3Vec_Load_Lane_Intrinsic
    : DefaultAttrsIntrinsic<[LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>],
                [LLVMMatchType<0>, LLVMMatchType<0>, llvm_anyvector_ty,
                 llvm_i64_ty, llvm_anyptr_ty],
                [IntrReadMem, IntrArgMemOnly]>;
  class AdvSIMD_3Vec_Store_Intrinsic
    : DefaultAttrsIntrinsic<[], [llvm_anyvector_ty, LLVMMatchType<0>,
                     LLVMMatchType<0>, llvm_anyptr_ty],
                [IntrArgMemOnly, NoCapture<ArgIndex<3>>]>;
  class AdvSIMD_3Vec_Store_Lane_Intrinsic
    : DefaultAttrsIntrinsic<[], [llvm_anyvector_ty,
                 LLVMMatchType<0>, LLVMMatchType<0>,
                 llvm_i64_ty, llvm_anyptr_ty],
                [IntrArgMemOnly, NoCapture<ArgIndex<4>>]>;

  class AdvSIMD_4Vec_Load_Intrinsic
    : DefaultAttrsIntrinsic<[LLVMMatchType<0>, LLVMMatchType<0>,
                 LLVMMatchType<0>, llvm_anyvector_ty],
                [llvm_anyptr_ty],
                [IntrReadMem, IntrArgMemOnly]>;
  class AdvSIMD_4Vec_Load_Lane_Intrinsic
    : DefaultAttrsIntrinsic<[LLVMMatchType<0>, LLVMMatchType<0>,
                 LLVMMatchType<0>, LLVMMatchType<0>],
                [LLVMMatchType<0>, LLVMMatchType<0>,
                 LLVMMatchType<0>, llvm_anyvector_ty,
                 llvm_i64_ty, llvm_anyptr_ty],
                [IntrReadMem, IntrArgMemOnly]>;
  class AdvSIMD_4Vec_Store_Intrinsic
    : DefaultAttrsIntrinsic<[], [llvm_anyvector_ty, LLVMMatchType<0>,
                 LLVMMatchType<0>, LLVMMatchType<0>,
                 llvm_anyptr_ty],
                [IntrArgMemOnly, NoCapture<ArgIndex<4>>]>;
  class AdvSIMD_4Vec_Store_Lane_Intrinsic
    : DefaultAttrsIntrinsic<[], [llvm_anyvector_ty, LLVMMatchType<0>,
                 LLVMMatchType<0>, LLVMMatchType<0>,
                 llvm_i64_ty, llvm_anyptr_ty],
                [IntrArgMemOnly, NoCapture<ArgIndex<5>>]>;
}

// Memory ops

def int_aarch64_neon_ld1x2 : AdvSIMD_2Vec_Load_Intrinsic;
def int_aarch64_neon_ld1x3 : AdvSIMD_3Vec_Load_Intrinsic;
def int_aarch64_neon_ld1x4 : AdvSIMD_4Vec_Load_Intrinsic;

def int_aarch64_neon_st1x2 : AdvSIMD_2Vec_Store_Intrinsic;
def int_aarch64_neon_st1x3 : AdvSIMD_3Vec_Store_Intrinsic;
def int_aarch64_neon_st1x4 : AdvSIMD_4Vec_Store_Intrinsic;

def int_aarch64_neon_ld2 : AdvSIMD_2Vec_Load_Intrinsic;
def int_aarch64_neon_ld3 : AdvSIMD_3Vec_Load_Intrinsic;
def int_aarch64_neon_ld4 : AdvSIMD_4Vec_Load_Intrinsic;

def int_aarch64_neon_ld2lane : AdvSIMD_2Vec_Load_Lane_Intrinsic;
def int_aarch64_neon_ld3lane : AdvSIMD_3Vec_Load_Lane_Intrinsic;
def int_aarch64_neon_ld4lane : AdvSIMD_4Vec_Load_Lane_Intrinsic;

def int_aarch64_neon_ld2r : AdvSIMD_2Vec_Load_Intrinsic;
def int_aarch64_neon_ld3r : AdvSIMD_3Vec_Load_Intrinsic;
def int_aarch64_neon_ld4r : AdvSIMD_4Vec_Load_Intrinsic;

def int_aarch64_neon_st2  : AdvSIMD_2Vec_Store_Intrinsic;
def int_aarch64_neon_st3  : AdvSIMD_3Vec_Store_Intrinsic;
def int_aarch64_neon_st4  : AdvSIMD_4Vec_Store_Intrinsic;

def int_aarch64_neon_st2lane  : AdvSIMD_2Vec_Store_Lane_Intrinsic;
def int_aarch64_neon_st3lane  : AdvSIMD_3Vec_Store_Lane_Intrinsic;
def int_aarch64_neon_st4lane  : AdvSIMD_4Vec_Store_Lane_Intrinsic;

let TargetPrefix = "aarch64" in {  // All intrinsics start with "llvm.aarch64.".
  class AdvSIMD_Tbl1_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty], [llvm_v16i8_ty, LLVMMatchType<0>],
                [IntrNoMem]>;
  class AdvSIMD_Tbl2_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [llvm_v16i8_ty, llvm_v16i8_ty, LLVMMatchType<0>], [IntrNoMem]>;
  class AdvSIMD_Tbl3_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [llvm_v16i8_ty, llvm_v16i8_ty, llvm_v16i8_ty,
                 LLVMMatchType<0>],
                [IntrNoMem]>;
  class AdvSIMD_Tbl4_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [llvm_v16i8_ty, llvm_v16i8_ty, llvm_v16i8_ty, llvm_v16i8_ty,
                 LLVMMatchType<0>],
                [IntrNoMem]>;

  class AdvSIMD_Tbx1_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>, llvm_v16i8_ty, LLVMMatchType<0>],
                [IntrNoMem]>;
  class AdvSIMD_Tbx2_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>, llvm_v16i8_ty, llvm_v16i8_ty,
                 LLVMMatchType<0>],
                [IntrNoMem]>;
  class AdvSIMD_Tbx3_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>, llvm_v16i8_ty, llvm_v16i8_ty,
                 llvm_v16i8_ty, LLVMMatchType<0>],
                [IntrNoMem]>;
  class AdvSIMD_Tbx4_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>, llvm_v16i8_ty, llvm_v16i8_ty,
                 llvm_v16i8_ty, llvm_v16i8_ty, LLVMMatchType<0>],
                [IntrNoMem]>;
}
def int_aarch64_neon_tbl1 : AdvSIMD_Tbl1_Intrinsic;
def int_aarch64_neon_tbl2 : AdvSIMD_Tbl2_Intrinsic;
def int_aarch64_neon_tbl3 : AdvSIMD_Tbl3_Intrinsic;
def int_aarch64_neon_tbl4 : AdvSIMD_Tbl4_Intrinsic;

def int_aarch64_neon_tbx1 : AdvSIMD_Tbx1_Intrinsic;
def int_aarch64_neon_tbx2 : AdvSIMD_Tbx2_Intrinsic;
def int_aarch64_neon_tbx3 : AdvSIMD_Tbx3_Intrinsic;
def int_aarch64_neon_tbx4 : AdvSIMD_Tbx4_Intrinsic;

let TargetPrefix = "aarch64" in {
  class FPENV_Get_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_i64_ty], [], [IntrNoMem, IntrHasSideEffects]>;
  class FPENV_Set_Intrinsic
    : DefaultAttrsIntrinsic<[], [llvm_i64_ty], [IntrNoMem, IntrHasSideEffects]>;
  class RNDR_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_i64_ty, llvm_i1_ty], [], [IntrNoMem, IntrHasSideEffects]>;
}

// FP environment registers.
def int_aarch64_get_fpcr : FPENV_Get_Intrinsic;
def int_aarch64_set_fpcr : FPENV_Set_Intrinsic;
def int_aarch64_get_fpsr : FPENV_Get_Intrinsic;
def int_aarch64_set_fpsr : FPENV_Set_Intrinsic;

// Armv8.5-A Random number generation intrinsics
def int_aarch64_rndr : RNDR_Intrinsic;
def int_aarch64_rndrrs : RNDR_Intrinsic;

let TargetPrefix = "aarch64" in {
  class Crypto_AES_DataKey_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;

  class Crypto_AES_Data_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty], [IntrNoMem]>;

  // SHA intrinsic taking 5 words of the hash (v4i32, i32) and 4 of the schedule
  // (v4i32).
  class Crypto_SHA_5Hash4Schedule_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_i32_ty, llvm_v4i32_ty],
                [IntrNoMem]>;

  // SHA intrinsic taking 5 words of the hash (v4i32, i32) and 4 of the schedule
  // (v4i32).
  class Crypto_SHA_1Hash_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty], [IntrNoMem]>;

  // SHA intrinsic taking 8 words of the schedule
  class Crypto_SHA_8Schedule_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;

  // SHA intrinsic taking 12 words of the schedule
  class Crypto_SHA_12Schedule_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
                [IntrNoMem]>;

  // SHA intrinsic taking 8 words of the hash and 4 of the schedule.
  class Crypto_SHA_8Hash4Schedule_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
                [IntrNoMem]>;

  // SHA512 intrinsic taking 2 arguments
  class Crypto_SHA512_2Arg_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;

  // SHA512 intrinsic taking 3 Arguments
  class Crypto_SHA512_3Arg_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty],
                [IntrNoMem]>;

  // SHA3 Intrinsics taking 3 arguments
  class Crypto_SHA3_3Arg_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
               [LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>],
               [IntrNoMem]>;

  // SHA3 Intrinsic taking 2 arguments
  class Crypto_SHA3_2Arg_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
               [IntrNoMem]>;

  // SHA3 Intrinsic taking 3 Arguments 1 immediate
  class Crypto_SHA3_2ArgImm_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty, llvm_i64_ty],
               [IntrNoMem, ImmArg<ArgIndex<2>>]>;

  class Crypto_SM3_3Vector_Intrinsic
    : Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
                [IntrNoMem]>;

  class Crypto_SM3_3VectorIndexed_Intrinsic
    : Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty, llvm_i64_ty],
                [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  class Crypto_SM4_2Vector_Intrinsic
    : Intrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
}

// AES
def int_aarch64_crypto_aese   : Crypto_AES_DataKey_Intrinsic;
def int_aarch64_crypto_aesd   : Crypto_AES_DataKey_Intrinsic;
def int_aarch64_crypto_aesmc  : Crypto_AES_Data_Intrinsic;
def int_aarch64_crypto_aesimc : Crypto_AES_Data_Intrinsic;

// SHA1
def int_aarch64_crypto_sha1c  : Crypto_SHA_5Hash4Schedule_Intrinsic;
def int_aarch64_crypto_sha1p  : Crypto_SHA_5Hash4Schedule_Intrinsic;
def int_aarch64_crypto_sha1m  : Crypto_SHA_5Hash4Schedule_Intrinsic;
def int_aarch64_crypto_sha1h  : Crypto_SHA_1Hash_Intrinsic;

def int_aarch64_crypto_sha1su0 : Crypto_SHA_12Schedule_Intrinsic;
def int_aarch64_crypto_sha1su1 : Crypto_SHA_8Schedule_Intrinsic;

// SHA256
def int_aarch64_crypto_sha256h   : Crypto_SHA_8Hash4Schedule_Intrinsic;
def int_aarch64_crypto_sha256h2  : Crypto_SHA_8Hash4Schedule_Intrinsic;
def int_aarch64_crypto_sha256su0 : Crypto_SHA_8Schedule_Intrinsic;
def int_aarch64_crypto_sha256su1 : Crypto_SHA_12Schedule_Intrinsic;

//SHA3
def int_aarch64_crypto_eor3s : Crypto_SHA3_3Arg_Intrinsic;
def int_aarch64_crypto_eor3u : Crypto_SHA3_3Arg_Intrinsic;
def int_aarch64_crypto_bcaxs : Crypto_SHA3_3Arg_Intrinsic;
def int_aarch64_crypto_bcaxu : Crypto_SHA3_3Arg_Intrinsic;
def int_aarch64_crypto_rax1 : Crypto_SHA3_2Arg_Intrinsic;
def int_aarch64_crypto_xar : Crypto_SHA3_2ArgImm_Intrinsic;

// SHA512
def int_aarch64_crypto_sha512h : Crypto_SHA512_3Arg_Intrinsic;
def int_aarch64_crypto_sha512h2 : Crypto_SHA512_3Arg_Intrinsic;
def int_aarch64_crypto_sha512su0 : Crypto_SHA512_2Arg_Intrinsic;
def int_aarch64_crypto_sha512su1 : Crypto_SHA512_3Arg_Intrinsic;

//SM3 & SM4
def int_aarch64_crypto_sm3partw1 : Crypto_SM3_3Vector_Intrinsic;
def int_aarch64_crypto_sm3partw2 : Crypto_SM3_3Vector_Intrinsic;
def int_aarch64_crypto_sm3ss1    : Crypto_SM3_3Vector_Intrinsic;
def int_aarch64_crypto_sm3tt1a   : Crypto_SM3_3VectorIndexed_Intrinsic;
def int_aarch64_crypto_sm3tt1b   : Crypto_SM3_3VectorIndexed_Intrinsic;
def int_aarch64_crypto_sm3tt2a   : Crypto_SM3_3VectorIndexed_Intrinsic;
def int_aarch64_crypto_sm3tt2b   : Crypto_SM3_3VectorIndexed_Intrinsic;
def int_aarch64_crypto_sm4e      : Crypto_SM4_2Vector_Intrinsic;
def int_aarch64_crypto_sm4ekey   : Crypto_SM4_2Vector_Intrinsic;

//===----------------------------------------------------------------------===//
// CRC32

let TargetPrefix = "aarch64" in {

def int_aarch64_crc32b  : DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem]>;
def int_aarch64_crc32cb : DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem]>;
def int_aarch64_crc32h  : DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem]>;
def int_aarch64_crc32ch : DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem]>;
def int_aarch64_crc32w  : DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem]>;
def int_aarch64_crc32cw : DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem]>;
def int_aarch64_crc32x  : DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i64_ty],
    [IntrNoMem]>;
def int_aarch64_crc32cx : DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i64_ty],
    [IntrNoMem]>;
}

//===----------------------------------------------------------------------===//
// Memory Tagging Extensions (MTE) Intrinsics
let TargetPrefix = "aarch64" in {
def int_aarch64_irg   : DefaultAttrsIntrinsic<[llvm_ptr_ty], [llvm_ptr_ty, llvm_i64_ty],
    [IntrNoMem, IntrHasSideEffects]>;
def int_aarch64_addg  : DefaultAttrsIntrinsic<[llvm_ptr_ty], [llvm_ptr_ty, llvm_i64_ty],
    [IntrNoMem]>;
def int_aarch64_gmi   : DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_ptr_ty, llvm_i64_ty],
    [IntrNoMem]>;
def int_aarch64_ldg   : DefaultAttrsIntrinsic<[llvm_ptr_ty], [llvm_ptr_ty, llvm_ptr_ty],
    [IntrReadMem]>;
def int_aarch64_stg   : DefaultAttrsIntrinsic<[], [llvm_ptr_ty, llvm_ptr_ty],
    [IntrWriteMem]>;
def int_aarch64_subp :  DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_ptr_ty, llvm_ptr_ty],
    [IntrNoMem]>;

// The following are codegen-only intrinsics for stack instrumentation.

// Generate a randomly tagged stack base pointer.
def int_aarch64_irg_sp   : DefaultAttrsIntrinsic<[llvm_ptr_ty], [llvm_i64_ty],
    [IntrNoMem, IntrHasSideEffects]>;

// Transfer pointer tag with offset.
// ptr1 = tagp(ptr0, baseptr, tag_offset) returns a pointer where
// * address is the address in ptr0
// * tag is a function of (tag in baseptr, tag_offset).
// ** Beware, this is not the same function as implemented by the ADDG instruction!
//    Backend optimizations may change tag_offset; the only guarantee is that calls
//    to tagp with the same pair of (baseptr, tag_offset) will produce pointers
//    with the same tag value, assuming the set of excluded tags has not changed.
// Address bits in baseptr and tag bits in ptr0 are ignored.
// When offset between ptr0 and baseptr is a compile time constant, this can be emitted as
//   ADDG ptr1, baseptr, (ptr0 - baseptr), tag_offset
// It is intended that ptr0 is an alloca address, and baseptr is the direct output of llvm.aarch64.irg.sp.
def int_aarch64_tagp : DefaultAttrsIntrinsic<[llvm_anyptr_ty], [LLVMMatchType<0>, llvm_ptr_ty, llvm_i64_ty],
    [IntrNoMem, ImmArg<ArgIndex<2>>]>;

// Update allocation tags for the memory range to match the tag in the pointer argument.
def int_aarch64_settag  : DefaultAttrsIntrinsic<[], [llvm_ptr_ty, llvm_i64_ty],
    [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, WriteOnly<ArgIndex<0>>]>;

// Update allocation tags for the memory range to match the tag in the pointer argument,
// and set memory contents to zero.
def int_aarch64_settag_zero  : DefaultAttrsIntrinsic<[], [llvm_ptr_ty, llvm_i64_ty],
    [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, WriteOnly<ArgIndex<0>>]>;

// Update allocation tags for 16-aligned, 16-sized memory region, and store a pair 8-byte values.
def int_aarch64_stgp  : DefaultAttrsIntrinsic<[], [llvm_ptr_ty, llvm_i64_ty, llvm_i64_ty],
    [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, WriteOnly<ArgIndex<0>>]>;
}

//===----------------------------------------------------------------------===//
// Memory Operations (MOPS) Intrinsics
let TargetPrefix = "aarch64" in {
  // Sizes are chosen to correspond to the llvm.memset intrinsic: ptr, i8, i64
  def int_aarch64_mops_memset_tag : DefaultAttrsIntrinsic<[llvm_ptr_ty], [llvm_ptr_ty, llvm_i8_ty, llvm_i64_ty],
      [IntrWriteMem, IntrArgMemOnly, NoCapture<ArgIndex<0>>, WriteOnly<ArgIndex<0>>]>;
}

// Transactional Memory Extension (TME) Intrinsics
let TargetPrefix = "aarch64" in {
def int_aarch64_tstart  : ClangBuiltin<"__builtin_arm_tstart">,
                         Intrinsic<[llvm_i64_ty], [], [IntrWillReturn]>;

def int_aarch64_tcommit : ClangBuiltin<"__builtin_arm_tcommit">, Intrinsic<[], [], [IntrWillReturn]>;

def int_aarch64_tcancel : ClangBuiltin<"__builtin_arm_tcancel">,
                          Intrinsic<[], [llvm_i64_ty], [IntrWillReturn, ImmArg<ArgIndex<0>>]>;

def int_aarch64_ttest   : ClangBuiltin<"__builtin_arm_ttest">,
                          Intrinsic<[llvm_i64_ty], [],
                                    [IntrNoMem, IntrHasSideEffects, IntrWillReturn]>;

// Armv8.7-A load/store 64-byte intrinsics
defvar data512 = !listsplat(llvm_i64_ty, 8);
def int_aarch64_ld64b: Intrinsic<data512, [llvm_ptr_ty]>;
def int_aarch64_st64b: Intrinsic<[], !listconcat([llvm_ptr_ty], data512)>;
def int_aarch64_st64bv: Intrinsic<[llvm_i64_ty], !listconcat([llvm_ptr_ty], data512)>;
def int_aarch64_st64bv0: Intrinsic<[llvm_i64_ty], !listconcat([llvm_ptr_ty], data512)>;

}

def llvm_nxv1i1_ty  : LLVMType<nxv1i1>;
def llvm_nxv2i1_ty  : LLVMType<nxv2i1>;
def llvm_nxv4i1_ty  : LLVMType<nxv4i1>;
def llvm_nxv8i1_ty  : LLVMType<nxv8i1>;
def llvm_nxv16i1_ty : LLVMType<nxv16i1>;
def llvm_nxv16i8_ty : LLVMType<nxv16i8>;
def llvm_nxv4i32_ty : LLVMType<nxv4i32>;
def llvm_nxv2i64_ty : LLVMType<nxv2i64>;
def llvm_nxv8f16_ty : LLVMType<nxv8f16>;
def llvm_nxv8bf16_ty : LLVMType<nxv8bf16>;
def llvm_nxv4f32_ty : LLVMType<nxv4f32>;
def llvm_nxv2f64_ty : LLVMType<nxv2f64>;

let TargetPrefix = "aarch64" in {  // All intrinsics start with "llvm.aarch64.".

  class AdvSIMD_1Vec_PredLoad_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>, llvm_ptr_ty],
                [IntrReadMem, IntrArgMemOnly]>;

  class AdvSIMD_2Vec_PredLoad_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>],
                [LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>, llvm_ptr_ty],
                [IntrReadMem, IntrArgMemOnly]>;

  class AdvSIMD_3Vec_PredLoad_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>, LLVMMatchType<0>],
                [LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>, llvm_ptr_ty],
                [IntrReadMem, IntrArgMemOnly]>;

  class AdvSIMD_4Vec_PredLoad_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>, LLVMMatchType<0>,
                 LLVMMatchType<0>],
                [LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>, llvm_ptr_ty],
                [IntrReadMem, IntrArgMemOnly]>;

  class AdvSIMD_1Vec_PredLoad_WriteFFR_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>, llvm_ptr_ty],
                [IntrInaccessibleMemOrArgMemOnly]>;

  class AdvSIMD_1Vec_PredStore_Intrinsic
    : DefaultAttrsIntrinsic<[],
                [llvm_anyvector_ty,
                 LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>, llvm_ptr_ty],
                [IntrArgMemOnly, NoCapture<ArgIndex<2>>]>;

  class AdvSIMD_2Vec_PredStore_Intrinsic
      : DefaultAttrsIntrinsic<[],
                  [llvm_anyvector_ty, LLVMMatchType<0>,
                   LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>, llvm_ptr_ty],
                  [IntrArgMemOnly, NoCapture<ArgIndex<3>>]>;

  class AdvSIMD_3Vec_PredStore_Intrinsic
      : DefaultAttrsIntrinsic<[],
                  [llvm_anyvector_ty, LLVMMatchType<0>, LLVMMatchType<0>,
                   LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>, llvm_ptr_ty],
                  [IntrArgMemOnly, NoCapture<ArgIndex<4>>]>;

  class AdvSIMD_4Vec_PredStore_Intrinsic
      : DefaultAttrsIntrinsic<[],
                  [llvm_anyvector_ty, LLVMMatchType<0>, LLVMMatchType<0>,
                   LLVMMatchType<0>,
                   LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>, llvm_ptr_ty],
                  [IntrArgMemOnly, NoCapture<ArgIndex<5>>]>;

  class AdvSIMD_SVE_Index_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMVectorElementType<0>,
                 LLVMVectorElementType<0>],
                [IntrNoMem]>;

  class AdvSIMD_Merged1VectorArg_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>,
                 LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                 LLVMMatchType<0>],
                [IntrNoMem]>;

  class AdvSIMD_2VectorArgIndexed_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>,
                 LLVMMatchType<0>,
                 llvm_i32_ty],
                [IntrNoMem, ImmArg<ArgIndex<2>>]>;

  class AdvSIMD_3VectorArgIndexed_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>,
                 LLVMMatchType<0>,
                 LLVMMatchType<0>,
                 llvm_i32_ty],
                [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  class AdvSIMD_Pred1VectorArg_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                 LLVMMatchType<0>],
                [IntrNoMem]>;

  class AdvSIMD_Pred2VectorArg_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                 LLVMMatchType<0>,
                 LLVMMatchType<0>],
                [IntrNoMem]>;

  class AdvSIMD_Pred3VectorArg_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                 LLVMMatchType<0>,
                 LLVMMatchType<0>,
                 LLVMMatchType<0>],
                [IntrNoMem]>;

  class AdvSIMD_SVE_Compare_Intrinsic
    : DefaultAttrsIntrinsic<[LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>],
                [LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                 llvm_anyvector_ty,
                 LLVMMatchType<0>],
                [IntrNoMem]>;

  class AdvSIMD_SVE_CompareWide_Intrinsic
    : DefaultAttrsIntrinsic<[LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>],
                [LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                 llvm_anyvector_ty,
                 llvm_nxv2i64_ty],
                [IntrNoMem]>;

  class AdvSIMD_SVE_Saturating_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>,
                 LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>],
                [IntrNoMem]>;

  class AdvSIMD_SVE_SaturatingWithPattern_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>,
                 llvm_i32_ty,
                 llvm_i32_ty],
                [IntrNoMem, ImmArg<ArgIndex<1>>, ImmArg<ArgIndex<2>>]>;

  class AdvSIMD_SVE_Saturating_N_Intrinsic<LLVMType T>
    : DefaultAttrsIntrinsic<[T],
                [T, llvm_anyvector_ty],
                [IntrNoMem]>;

  class AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<LLVMType T>
    : DefaultAttrsIntrinsic<[T],
                [T, llvm_i32_ty, llvm_i32_ty],
                [IntrNoMem, ImmArg<ArgIndex<1>>, ImmArg<ArgIndex<2>>]>;

  class AdvSIMD_SVE_CNT_Intrinsic
    : DefaultAttrsIntrinsic<[LLVMVectorOfBitcastsToInt<0>],
                [LLVMVectorOfBitcastsToInt<0>,
                 LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                 llvm_anyvector_ty],
                [IntrNoMem]>;

  class AdvSIMD_SVE_ReduceWithInit_Intrinsic
    : DefaultAttrsIntrinsic<[LLVMVectorElementType<0>],
                [LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                 LLVMVectorElementType<0>,
                 llvm_anyvector_ty],
                [IntrNoMem]>;

  class AdvSIMD_SVE_ShiftByImm_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                 LLVMMatchType<0>,
                 llvm_i32_ty],
                [IntrNoMem, ImmArg<ArgIndex<2>>]>;

  class AdvSIMD_SVE_ShiftWide_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                 LLVMMatchType<0>,
                 llvm_nxv2i64_ty],
                [IntrNoMem]>;

  class AdvSIMD_SVE_Unpack_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
               [LLVMSubdivide2VectorType<0>],
               [IntrNoMem]>;

  class AdvSIMD_SVE_CADD_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                 LLVMMatchType<0>,
                 LLVMMatchType<0>,
                 llvm_i32_ty],
                [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  class AdvSIMD_SVE_CMLA_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                 LLVMMatchType<0>,
                 LLVMMatchType<0>,
                 LLVMMatchType<0>,
                 llvm_i32_ty],
                [IntrNoMem, ImmArg<ArgIndex<4>>]>;

  class AdvSIMD_SVE_CMLA_LANE_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>,
                 LLVMMatchType<0>,
                 LLVMMatchType<0>,
                 llvm_i32_ty,
                 llvm_i32_ty],
                [IntrNoMem, ImmArg<ArgIndex<3>>, ImmArg<ArgIndex<4>>]>;

  class AdvSIMD_SVE_DUP_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>,
                 LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                 LLVMVectorElementType<0>],
                [IntrNoMem]>;

  class AdvSIMD_SVE_DUP_Unpred_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty], [LLVMVectorElementType<0>],
                [IntrNoMem]>;

  class AdvSIMD_SVE_DUPQ_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>,
                 llvm_i64_ty],
                [IntrNoMem]>;

  class AdvSIMD_SVE_EXPA_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMVectorOfBitcastsToInt<0>],
                [IntrNoMem]>;

  class AdvSIMD_SVE_FCVT_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>,
                 LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                 llvm_anyvector_ty],
                [IntrNoMem]>;

  class AdvSIMD_SVE_FCVTZS_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMVectorOfBitcastsToInt<0>,
                 LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                 llvm_anyvector_ty],
                [IntrNoMem]>;

  class AdvSIMD_SVE_INSR_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>,
                 LLVMVectorElementType<0>],
                [IntrNoMem]>;

  class AdvSIMD_SVE_PTRUE_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [llvm_i32_ty],
                [IntrNoMem, ImmArg<ArgIndex<0>>]>;

  class AdvSIMD_SVE_PUNPKHI_Intrinsic
    : DefaultAttrsIntrinsic<[LLVMHalfElementsVectorType<0>],
                [llvm_anyvector_ty],
                [IntrNoMem]>;

  class AdvSIMD_SVE_SCALE_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                 LLVMMatchType<0>,
                 LLVMVectorOfBitcastsToInt<0>],
                [IntrNoMem]>;

  class AdvSIMD_SVE_SCVTF_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>,
                 LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                 llvm_anyvector_ty],
                [IntrNoMem]>;

  class AdvSIMD_SVE_TSMUL_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>,
                 LLVMVectorOfBitcastsToInt<0>],
                [IntrNoMem]>;

  class AdvSIMD_SVE_CNTB_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_i64_ty],
                [llvm_i32_ty],
                [IntrNoMem, ImmArg<ArgIndex<0>>]>;

  class AdvSIMD_SVE_CNTP_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_i64_ty],
                [llvm_anyvector_ty, LLVMMatchType<0>],
                [IntrNoMem]>;

  class AdvSIMD_SVE_DOT_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>,
                 LLVMSubdivide4VectorType<0>,
                 LLVMSubdivide4VectorType<0>],
                [IntrNoMem]>;

  class AdvSIMD_SVE_DOT_Indexed_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>,
                 LLVMSubdivide4VectorType<0>,
                 LLVMSubdivide4VectorType<0>,
                 llvm_i32_ty],
                [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  class AdvSIMD_SVE_PTEST_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_i1_ty],
                [llvm_anyvector_ty,
                 LLVMMatchType<0>],
                [IntrNoMem]>;

  class AdvSIMD_SVE_TBL_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>,
                 LLVMVectorOfBitcastsToInt<0>],
                [IntrNoMem]>;

  class AdvSIMD_SVE2_TBX_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>,
                 LLVMMatchType<0>,
                 LLVMVectorOfBitcastsToInt<0>],
                [IntrNoMem]>;

  class SVE2_1VectorArg_Long_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMSubdivide2VectorType<0>,
                 llvm_i32_ty],
                [IntrNoMem, ImmArg<ArgIndex<1>>]>;

  class SVE2_2VectorArg_Long_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMSubdivide2VectorType<0>,
                 LLVMSubdivide2VectorType<0>],
                [IntrNoMem]>;

  class SVE2_2VectorArgIndexed_Long_Intrinsic
  : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
              [LLVMSubdivide2VectorType<0>,
               LLVMSubdivide2VectorType<0>,
               llvm_i32_ty],
              [IntrNoMem, ImmArg<ArgIndex<2>>]>;

  class SVE2_2VectorArg_Wide_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>,
                 LLVMSubdivide2VectorType<0>],
                [IntrNoMem]>;

  class SVE2_2VectorArg_Pred_Long_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                 LLVMMatchType<0>,
                 LLVMSubdivide2VectorType<0>],
                [IntrNoMem]>;

  class SVE2_3VectorArg_Long_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>,
                 LLVMSubdivide2VectorType<0>,
                 LLVMSubdivide2VectorType<0>],
                [IntrNoMem]>;

  class SVE2_3VectorArgIndexed_Long_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>,
                 LLVMSubdivide2VectorType<0>,
                 LLVMSubdivide2VectorType<0>,
                 llvm_i32_ty],
                [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  class SVE2_1VectorArg_Narrowing_Intrinsic
    : DefaultAttrsIntrinsic<[LLVMSubdivide2VectorType<0>],
                [llvm_anyvector_ty],
                [IntrNoMem]>;

  class SVE2_Merged1VectorArg_Narrowing_Intrinsic
    : DefaultAttrsIntrinsic<[LLVMSubdivide2VectorType<0>],
                [LLVMSubdivide2VectorType<0>,
                 llvm_anyvector_ty],
                [IntrNoMem]>;
  class SVE2_2VectorArg_Narrowing_Intrinsic
      : DefaultAttrsIntrinsic<
            [LLVMSubdivide2VectorType<0>],
            [llvm_anyvector_ty, LLVMMatchType<0>],
            [IntrNoMem]>;

  class SVE2_Merged2VectorArg_Narrowing_Intrinsic
      : DefaultAttrsIntrinsic<
            [LLVMSubdivide2VectorType<0>],
            [LLVMSubdivide2VectorType<0>, llvm_anyvector_ty, LLVMMatchType<0>],
            [IntrNoMem]>;

  class SVE2_1VectorArg_Imm_Narrowing_Intrinsic
      : DefaultAttrsIntrinsic<[LLVMSubdivide2VectorType<0>],
                  [llvm_anyvector_ty, llvm_i32_ty],
                  [IntrNoMem, ImmArg<ArgIndex<1>>]>;

  class SVE2_2VectorArg_Imm_Narrowing_Intrinsic
      : DefaultAttrsIntrinsic<[LLVMSubdivide2VectorType<0>],
                  [LLVMSubdivide2VectorType<0>, llvm_anyvector_ty,
                   llvm_i32_ty],
                  [IntrNoMem, ImmArg<ArgIndex<2>>]>;

  class SVE2_CONFLICT_DETECT_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [llvm_anyptr_ty, LLVMMatchType<1>],
                [IntrNoMem]>;

  class SVE2_3VectorArg_Indexed_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>,
                 LLVMSubdivide2VectorType<0>,
                 LLVMSubdivide2VectorType<0>,
                 llvm_i32_ty],
                [IntrNoMem, ImmArg<ArgIndex<3>>]>;
  
  class SVE2_1VectorArgIndexed_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>,
                 llvm_i32_ty],
                [IntrNoMem, ImmArg<ArgIndex<1>>]>;

  class AdvSIMD_SVE_CDOT_LANE_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>,
                 LLVMSubdivide4VectorType<0>,
                 LLVMSubdivide4VectorType<0>,
                 llvm_i32_ty,
                 llvm_i32_ty],
                [IntrNoMem, ImmArg<ArgIndex<3>>, ImmArg<ArgIndex<4>>]>;
  
  class SVE2_1VectorArg_Pred_Intrinsic
    : DefaultAttrsIntrinsic<[LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>],
                            [llvm_anyvector_ty],
                            [IntrNoMem]>;

  class SVE2_1VectorArgIndexed_Pred_Intrinsic
    : DefaultAttrsIntrinsic<[LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>],
                            [llvm_anyvector_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  
  class SVE2_Pred_1VectorArgIndexed_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                            [LLVMMatchType<0>,
                             LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>]>;

  class SVE2_Pred_1VectorArg_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                            [LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>],
                            [IntrNoMem]>;

  // NOTE: There is no relationship between these intrinsics beyond an attempt
  // to reuse currently identical class definitions.
  class AdvSIMD_SVE_LOGB_Intrinsic  : AdvSIMD_SVE_CNT_Intrinsic;
  class AdvSIMD_SVE2_CADD_Intrinsic : AdvSIMD_2VectorArgIndexed_Intrinsic;
  class AdvSIMD_SVE2_CMLA_Intrinsic : AdvSIMD_3VectorArgIndexed_Intrinsic;

  // This class of intrinsics are not intended to be useful within LLVM IR but
  // are instead here to support some of the more regid parts of the ACLE.
  class Builtin_SVCVT<LLVMType OUT, LLVMType PRED, LLVMType IN>
      : DefaultAttrsIntrinsic<[OUT], [OUT, PRED, IN], [IntrNoMem]>;
}

//===----------------------------------------------------------------------===//
// SVE

let TargetPrefix = "aarch64" in {  // All intrinsics start with "llvm.aarch64.".

class AdvSIMD_SVE_2SVBoolArg_Intrinsic
  : DefaultAttrsIntrinsic<[llvm_nxv16i1_ty],
                          [llvm_nxv16i1_ty],
                          [IntrNoMem]>;

class AdvSIMD_SVE_3SVBoolArg_Intrinsic
  : DefaultAttrsIntrinsic<[llvm_nxv16i1_ty],
                          [llvm_nxv16i1_ty, llvm_nxv16i1_ty],
                          [IntrNoMem]>;

class AdvSIMD_SVE_Reduce_Intrinsic
  : DefaultAttrsIntrinsic<[LLVMVectorElementType<0>],
              [LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
               llvm_anyvector_ty],
              [IntrNoMem]>;

class AdvSIMD_SVE_V128_Reduce_Intrinsic
  : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
              [LLVMScalarOrSameVectorWidth<1, llvm_i1_ty>,
               llvm_anyvector_ty],
               [IntrNoMem]>;


class AdvSIMD_SVE_SADDV_Reduce_Intrinsic
  : DefaultAttrsIntrinsic<[llvm_i64_ty],
              [LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
               llvm_anyvector_ty],
              [IntrNoMem]>;

class AdvSIMD_SVE_WHILE_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [llvm_anyint_ty, LLVMMatchType<1>],
                [IntrNoMem]>;

class AdvSIMD_GatherLoad_SV_64b_Offsets_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [
                  LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                  llvm_ptr_ty,
                  LLVMScalarOrSameVectorWidth<0, llvm_i64_ty>
                ],
                [IntrReadMem, IntrArgMemOnly]>;

class AdvSIMD_GatherLoad_SV_64b_Offsets_WriteFFR_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [
                  LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                  llvm_ptr_ty,
                  LLVMScalarOrSameVectorWidth<0, llvm_i64_ty>
                ],
                [IntrInaccessibleMemOrArgMemOnly]>;

class AdvSIMD_GatherLoad_SV_32b_Offsets_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [
                  LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                  llvm_ptr_ty,
                  LLVMScalarOrSameVectorWidth<0, llvm_i32_ty>
                ],
                [IntrReadMem, IntrArgMemOnly]>;

class AdvSIMD_GatherLoad_SV_32b_Offsets_WriteFFR_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [
                  LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                  llvm_ptr_ty,
                  LLVMScalarOrSameVectorWidth<0, llvm_i32_ty>
                ],
                [IntrInaccessibleMemOrArgMemOnly]>;

class AdvSIMD_GatherLoad_VS_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [
                  LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                  llvm_anyvector_ty,
                  llvm_i64_ty
                ],
                [IntrReadMem]>;

class AdvSIMD_GatherLoadQ_VS_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [
                  llvm_nxv1i1_ty,
                  llvm_anyvector_ty,
                  llvm_i64_ty
                ],
                [IntrReadMem]>;

class AdvSIMD_GatherLoadQ_SV_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [
                  llvm_nxv1i1_ty,
                  llvm_ptr_ty,
                  llvm_nxv2i64_ty
                ],
                [IntrReadMem, IntrArgMemOnly]>;

class AdvSIMD_GatherLoad_VS_WriteFFR_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [
                  LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                  llvm_anyvector_ty,
                  llvm_i64_ty
                ],
                [IntrInaccessibleMemOrArgMemOnly]>;

class AdvSIMD_ScatterStore_SV_64b_Offsets_Intrinsic
    : DefaultAttrsIntrinsic<[],
               [
                 llvm_anyvector_ty,
                 LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                 llvm_ptr_ty,
                 LLVMScalarOrSameVectorWidth<0, llvm_i64_ty>
               ],
               [IntrWriteMem, IntrArgMemOnly]>;

class AdvSIMD_ScatterStore_SV_32b_Offsets_Intrinsic
    : DefaultAttrsIntrinsic<[],
               [
                 llvm_anyvector_ty,
                 LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                 llvm_ptr_ty,
                 LLVMScalarOrSameVectorWidth<0, llvm_i32_ty>
               ],
               [IntrWriteMem, IntrArgMemOnly]>;

class AdvSIMD_ScatterStore_VS_Intrinsic
    : DefaultAttrsIntrinsic<[],
               [
                 llvm_anyvector_ty,
                 LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
                 llvm_anyvector_ty, llvm_i64_ty
               ],
               [IntrWriteMem]>;

class AdvSIMD_ScatterStoreQ_VS_Intrinsic
    : DefaultAttrsIntrinsic<[],
               [
                 llvm_anyvector_ty,
                 llvm_nxv1i1_ty,
                 llvm_anyvector_ty,
                 llvm_i64_ty
               ],
               [IntrWriteMem]>;

class AdvSIMD_ScatterStoreQ_SV_Intrinsic
    : DefaultAttrsIntrinsic<[],
               [
                 llvm_anyvector_ty,
                 llvm_nxv1i1_ty,
                 llvm_ptr_ty,
                 llvm_nxv2i64_ty
               ],
               [IntrWriteMem, IntrArgMemOnly]>;

class SVE_gather_prf_SV
    : DefaultAttrsIntrinsic<[],
                [
                  LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>, // Predicate
                  llvm_ptr_ty, // Base address
                  llvm_anyvector_ty, // Offsets
                  llvm_i32_ty // Prfop
                ],
                [IntrInaccessibleMemOrArgMemOnly, NoCapture<ArgIndex<1>>, ImmArg<ArgIndex<3>>]>;

class SVE_gather_prf_VS
    : DefaultAttrsIntrinsic<[],
                [
                  LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>, // Predicate
                  llvm_anyvector_ty, // Base addresses
                  llvm_i64_ty, // Scalar offset
                  llvm_i32_ty // Prfop
                ],
                [IntrInaccessibleMemOrArgMemOnly, ImmArg<ArgIndex<3>>]>;

class SVE_MatMul_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                [LLVMMatchType<0>, LLVMSubdivide4VectorType<0>, LLVMSubdivide4VectorType<0>],
                [IntrNoMem]>;

class SVE_4Vec_BF16
    : DefaultAttrsIntrinsic<[llvm_nxv4f32_ty],
                [llvm_nxv4f32_ty, llvm_nxv8bf16_ty, llvm_nxv8bf16_ty],
                [IntrNoMem]>;

class SVE_4Vec_BF16_Indexed
    : DefaultAttrsIntrinsic<[llvm_nxv4f32_ty],
                [llvm_nxv4f32_ty, llvm_nxv8bf16_ty, llvm_nxv8bf16_ty, llvm_i32_ty],
                [IntrNoMem, ImmArg<ArgIndex<3>>]>;

//
// Loads
//

def int_aarch64_sve_ld1   : AdvSIMD_1Vec_PredLoad_Intrinsic;

def int_aarch64_sve_ld2_sret : AdvSIMD_2Vec_PredLoad_Intrinsic;
def int_aarch64_sve_ld3_sret : AdvSIMD_3Vec_PredLoad_Intrinsic;
def int_aarch64_sve_ld4_sret : AdvSIMD_4Vec_PredLoad_Intrinsic;

def int_aarch64_sve_ldnt1 : AdvSIMD_1Vec_PredLoad_Intrinsic;
def int_aarch64_sve_ldnf1 : AdvSIMD_1Vec_PredLoad_WriteFFR_Intrinsic;
def int_aarch64_sve_ldff1 : AdvSIMD_1Vec_PredLoad_WriteFFR_Intrinsic;

def int_aarch64_sve_ld1rq : AdvSIMD_1Vec_PredLoad_Intrinsic;
def int_aarch64_sve_ld1ro : AdvSIMD_1Vec_PredLoad_Intrinsic;

//
// Stores
//

def int_aarch64_sve_st1  : AdvSIMD_1Vec_PredStore_Intrinsic;
def int_aarch64_sve_st2  : AdvSIMD_2Vec_PredStore_Intrinsic;
def int_aarch64_sve_st3  : AdvSIMD_3Vec_PredStore_Intrinsic;
def int_aarch64_sve_st4  : AdvSIMD_4Vec_PredStore_Intrinsic;

def int_aarch64_sve_stnt1 : AdvSIMD_1Vec_PredStore_Intrinsic;

//
// Prefetches
//

def int_aarch64_sve_prf
  : DefaultAttrsIntrinsic<[], [llvm_anyvector_ty, llvm_ptr_ty, llvm_i32_ty],
                  [IntrArgMemOnly, ImmArg<ArgIndex<2>>]>;

// Scalar + 32-bit scaled offset vector, zero extend, packed and
// unpacked.
def int_aarch64_sve_prfb_gather_uxtw_index : SVE_gather_prf_SV;
def int_aarch64_sve_prfh_gather_uxtw_index : SVE_gather_prf_SV;
def int_aarch64_sve_prfw_gather_uxtw_index : SVE_gather_prf_SV;
def int_aarch64_sve_prfd_gather_uxtw_index : SVE_gather_prf_SV;

// Scalar + 32-bit scaled offset vector, sign extend, packed and
// unpacked.
def int_aarch64_sve_prfb_gather_sxtw_index : SVE_gather_prf_SV;
def int_aarch64_sve_prfw_gather_sxtw_index : SVE_gather_prf_SV;
def int_aarch64_sve_prfh_gather_sxtw_index : SVE_gather_prf_SV;
def int_aarch64_sve_prfd_gather_sxtw_index : SVE_gather_prf_SV;

// Scalar + 64-bit scaled offset vector.
def int_aarch64_sve_prfb_gather_index : SVE_gather_prf_SV;
def int_aarch64_sve_prfh_gather_index : SVE_gather_prf_SV;
def int_aarch64_sve_prfw_gather_index : SVE_gather_prf_SV;
def int_aarch64_sve_prfd_gather_index : SVE_gather_prf_SV;

// Vector + scalar.
def int_aarch64_sve_prfb_gather_scalar_offset : SVE_gather_prf_VS;
def int_aarch64_sve_prfh_gather_scalar_offset : SVE_gather_prf_VS;
def int_aarch64_sve_prfw_gather_scalar_offset : SVE_gather_prf_VS;
def int_aarch64_sve_prfd_gather_scalar_offset : SVE_gather_prf_VS;

//
// Scalar to vector operations
//

def int_aarch64_sve_dup : AdvSIMD_SVE_DUP_Intrinsic;
def int_aarch64_sve_dup_x : AdvSIMD_SVE_DUP_Unpred_Intrinsic;

def int_aarch64_sve_index : AdvSIMD_SVE_Index_Intrinsic;

//
// Address calculation
//

def int_aarch64_sve_adrb : AdvSIMD_2VectorArg_Intrinsic;
def int_aarch64_sve_adrh : AdvSIMD_2VectorArg_Intrinsic;
def int_aarch64_sve_adrw : AdvSIMD_2VectorArg_Intrinsic;
def int_aarch64_sve_adrd : AdvSIMD_2VectorArg_Intrinsic;

//
// Integer arithmetic
//

def int_aarch64_sve_add   : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_add_u : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_sub   : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_sub_u : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_subr  : AdvSIMD_Pred2VectorArg_Intrinsic;

def int_aarch64_sve_pmul       : AdvSIMD_2VectorArg_Intrinsic;

def int_aarch64_sve_mul        : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_mul_u      : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_mul_lane   : AdvSIMD_2VectorArgIndexed_Intrinsic;
def int_aarch64_sve_smulh      : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_smulh_u    : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_umulh      : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_umulh_u    : AdvSIMD_Pred2VectorArg_Intrinsic;

def int_aarch64_sve_sdiv       : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_sdiv_u     : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_udiv       : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_udiv_u     : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_sdivr      : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_udivr      : AdvSIMD_Pred2VectorArg_Intrinsic;

def int_aarch64_sve_smax       : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_smax_u     : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_umax       : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_umax_u     : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_smin       : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_smin_u     : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_umin       : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_umin_u     : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_sabd       : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_sabd_u     : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_uabd       : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_uabd_u     : AdvSIMD_Pred2VectorArg_Intrinsic;

def int_aarch64_sve_mad        : AdvSIMD_Pred3VectorArg_Intrinsic;
def int_aarch64_sve_msb        : AdvSIMD_Pred3VectorArg_Intrinsic;
def int_aarch64_sve_mla        : AdvSIMD_Pred3VectorArg_Intrinsic;
def int_aarch64_sve_mla_u      : AdvSIMD_Pred3VectorArg_Intrinsic;
def int_aarch64_sve_mla_lane   : AdvSIMD_3VectorArgIndexed_Intrinsic;
def int_aarch64_sve_mls        : AdvSIMD_Pred3VectorArg_Intrinsic;
def int_aarch64_sve_mls_u      : AdvSIMD_Pred3VectorArg_Intrinsic;
def int_aarch64_sve_mls_lane   : AdvSIMD_3VectorArgIndexed_Intrinsic;

def int_aarch64_sve_saddv      : AdvSIMD_SVE_SADDV_Reduce_Intrinsic;
def int_aarch64_sve_uaddv      : AdvSIMD_SVE_SADDV_Reduce_Intrinsic;

def int_aarch64_sve_smaxv      : AdvSIMD_SVE_Reduce_Intrinsic;
def int_aarch64_sve_umaxv      : AdvSIMD_SVE_Reduce_Intrinsic;
def int_aarch64_sve_sminv      : AdvSIMD_SVE_Reduce_Intrinsic;
def int_aarch64_sve_uminv      : AdvSIMD_SVE_Reduce_Intrinsic;

def int_aarch64_sve_orv        : AdvSIMD_SVE_Reduce_Intrinsic;
def int_aarch64_sve_eorv       : AdvSIMD_SVE_Reduce_Intrinsic;
def int_aarch64_sve_andv       : AdvSIMD_SVE_Reduce_Intrinsic;

def int_aarch64_sve_abs : AdvSIMD_Merged1VectorArg_Intrinsic;
def int_aarch64_sve_neg : AdvSIMD_Merged1VectorArg_Intrinsic;

def int_aarch64_sve_sdot      : AdvSIMD_SVE_DOT_Intrinsic;
def int_aarch64_sve_sdot_lane : AdvSIMD_SVE_DOT_Indexed_Intrinsic;

def int_aarch64_sve_udot      : AdvSIMD_SVE_DOT_Intrinsic;
def int_aarch64_sve_udot_lane : AdvSIMD_SVE_DOT_Indexed_Intrinsic;

def int_aarch64_sve_sqadd_x   : AdvSIMD_2VectorArg_Intrinsic;
def int_aarch64_sve_sqsub_x   : AdvSIMD_2VectorArg_Intrinsic;
def int_aarch64_sve_uqadd_x   : AdvSIMD_2VectorArg_Intrinsic;
def int_aarch64_sve_uqsub_x   : AdvSIMD_2VectorArg_Intrinsic;

def int_aarch64_sve_orqv      : AdvSIMD_SVE_V128_Reduce_Intrinsic;
def int_aarch64_sve_eorqv     : AdvSIMD_SVE_V128_Reduce_Intrinsic;
def int_aarch64_sve_andqv     : AdvSIMD_SVE_V128_Reduce_Intrinsic;
def int_aarch64_sve_addqv     : AdvSIMD_SVE_V128_Reduce_Intrinsic;
def int_aarch64_sve_smaxqv    : AdvSIMD_SVE_V128_Reduce_Intrinsic;
def int_aarch64_sve_umaxqv    : AdvSIMD_SVE_V128_Reduce_Intrinsic;
def int_aarch64_sve_sminqv    : AdvSIMD_SVE_V128_Reduce_Intrinsic;
def int_aarch64_sve_uminqv    : AdvSIMD_SVE_V128_Reduce_Intrinsic;


// Shifts

def int_aarch64_sve_asr      : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_asr_u    : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_asr_wide : AdvSIMD_SVE_ShiftWide_Intrinsic;
def int_aarch64_sve_asrd     : AdvSIMD_SVE_ShiftByImm_Intrinsic;
def int_aarch64_sve_insr     : AdvSIMD_SVE_INSR_Intrinsic;
def int_aarch64_sve_lsl      : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_lsl_u    : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_lsl_wide : AdvSIMD_SVE_ShiftWide_Intrinsic;
def int_aarch64_sve_lsr      : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_lsr_u    : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_lsr_wide : AdvSIMD_SVE_ShiftWide_Intrinsic;

//
// Integer comparisons
//

def int_aarch64_sve_cmpeq : AdvSIMD_SVE_Compare_Intrinsic;
def int_aarch64_sve_cmpge : AdvSIMD_SVE_Compare_Intrinsic;
def int_aarch64_sve_cmpgt : AdvSIMD_SVE_Compare_Intrinsic;
def int_aarch64_sve_cmphi : AdvSIMD_SVE_Compare_Intrinsic;
def int_aarch64_sve_cmphs : AdvSIMD_SVE_Compare_Intrinsic;
def int_aarch64_sve_cmpne : AdvSIMD_SVE_Compare_Intrinsic;

def int_aarch64_sve_cmpeq_wide : AdvSIMD_SVE_CompareWide_Intrinsic;
def int_aarch64_sve_cmpge_wide : AdvSIMD_SVE_CompareWide_Intrinsic;
def int_aarch64_sve_cmpgt_wide : AdvSIMD_SVE_CompareWide_Intrinsic;
def int_aarch64_sve_cmphi_wide : AdvSIMD_SVE_CompareWide_Intrinsic;
def int_aarch64_sve_cmphs_wide : AdvSIMD_SVE_CompareWide_Intrinsic;
def int_aarch64_sve_cmple_wide : AdvSIMD_SVE_CompareWide_Intrinsic;
def int_aarch64_sve_cmplo_wide : AdvSIMD_SVE_CompareWide_Intrinsic;
def int_aarch64_sve_cmpls_wide : AdvSIMD_SVE_CompareWide_Intrinsic;
def int_aarch64_sve_cmplt_wide : AdvSIMD_SVE_CompareWide_Intrinsic;
def int_aarch64_sve_cmpne_wide : AdvSIMD_SVE_CompareWide_Intrinsic;

//
// Counting bits
//

def int_aarch64_sve_cls : AdvSIMD_Merged1VectorArg_Intrinsic;
def int_aarch64_sve_clz : AdvSIMD_Merged1VectorArg_Intrinsic;
def int_aarch64_sve_cnt : AdvSIMD_SVE_CNT_Intrinsic;

//
// Counting elements
//

def int_aarch64_sve_cntb : AdvSIMD_SVE_CNTB_Intrinsic;
def int_aarch64_sve_cnth : AdvSIMD_SVE_CNTB_Intrinsic;
def int_aarch64_sve_cntw : AdvSIMD_SVE_CNTB_Intrinsic;
def int_aarch64_sve_cntd : AdvSIMD_SVE_CNTB_Intrinsic;

def int_aarch64_sve_cntp : AdvSIMD_SVE_CNTP_Intrinsic;

//
// FFR manipulation
//

def int_aarch64_sve_rdffr   : ClangBuiltin<"__builtin_sve_svrdffr">,   DefaultAttrsIntrinsic<[llvm_nxv16i1_ty], [], [IntrReadMem, IntrInaccessibleMemOnly]>;
def int_aarch64_sve_rdffr_z : ClangBuiltin<"__builtin_sve_svrdffr_z">, DefaultAttrsIntrinsic<[llvm_nxv16i1_ty], [llvm_nxv16i1_ty], [IntrReadMem, IntrInaccessibleMemOnly]>;
def int_aarch64_sve_setffr  : ClangBuiltin<"__builtin_sve_svsetffr">,  DefaultAttrsIntrinsic<[], [], [IntrWriteMem, IntrInaccessibleMemOnly]>;
def int_aarch64_sve_wrffr   : ClangBuiltin<"__builtin_sve_svwrffr">,   DefaultAttrsIntrinsic<[], [llvm_nxv16i1_ty], [IntrWriteMem, IntrInaccessibleMemOnly]>;

//
// Saturating scalar arithmetic
//

def int_aarch64_sve_sqdech : AdvSIMD_SVE_SaturatingWithPattern_Intrinsic;
def int_aarch64_sve_sqdecw : AdvSIMD_SVE_SaturatingWithPattern_Intrinsic;
def int_aarch64_sve_sqdecd : AdvSIMD_SVE_SaturatingWithPattern_Intrinsic;
def int_aarch64_sve_sqdecp : AdvSIMD_SVE_Saturating_Intrinsic;

def int_aarch64_sve_sqdecb_n32 : AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<llvm_i32_ty>;
def int_aarch64_sve_sqdecb_n64 : AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<llvm_i64_ty>;
def int_aarch64_sve_sqdech_n32 : AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<llvm_i32_ty>;
def int_aarch64_sve_sqdech_n64 : AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<llvm_i64_ty>;
def int_aarch64_sve_sqdecw_n32 : AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<llvm_i32_ty>;
def int_aarch64_sve_sqdecw_n64 : AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<llvm_i64_ty>;
def int_aarch64_sve_sqdecd_n32 : AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<llvm_i32_ty>;
def int_aarch64_sve_sqdecd_n64 : AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<llvm_i64_ty>;
def int_aarch64_sve_sqdecp_n32 : AdvSIMD_SVE_Saturating_N_Intrinsic<llvm_i32_ty>;
def int_aarch64_sve_sqdecp_n64 : AdvSIMD_SVE_Saturating_N_Intrinsic<llvm_i64_ty>;

def int_aarch64_sve_sqinch : AdvSIMD_SVE_SaturatingWithPattern_Intrinsic;
def int_aarch64_sve_sqincw : AdvSIMD_SVE_SaturatingWithPattern_Intrinsic;
def int_aarch64_sve_sqincd : AdvSIMD_SVE_SaturatingWithPattern_Intrinsic;
def int_aarch64_sve_sqincp : AdvSIMD_SVE_Saturating_Intrinsic;

def int_aarch64_sve_sqincb_n32 : AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<llvm_i32_ty>;
def int_aarch64_sve_sqincb_n64 : AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<llvm_i64_ty>;
def int_aarch64_sve_sqinch_n32 : AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<llvm_i32_ty>;
def int_aarch64_sve_sqinch_n64 : AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<llvm_i64_ty>;
def int_aarch64_sve_sqincw_n32 : AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<llvm_i32_ty>;
def int_aarch64_sve_sqincw_n64 : AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<llvm_i64_ty>;
def int_aarch64_sve_sqincd_n32 : AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<llvm_i32_ty>;
def int_aarch64_sve_sqincd_n64 : AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<llvm_i64_ty>;
def int_aarch64_sve_sqincp_n32 : AdvSIMD_SVE_Saturating_N_Intrinsic<llvm_i32_ty>;
def int_aarch64_sve_sqincp_n64 : AdvSIMD_SVE_Saturating_N_Intrinsic<llvm_i64_ty>;

def int_aarch64_sve_uqdech : AdvSIMD_SVE_SaturatingWithPattern_Intrinsic;
def int_aarch64_sve_uqdecw : AdvSIMD_SVE_SaturatingWithPattern_Intrinsic;
def int_aarch64_sve_uqdecd : AdvSIMD_SVE_SaturatingWithPattern_Intrinsic;
def int_aarch64_sve_uqdecp : AdvSIMD_SVE_Saturating_Intrinsic;

def int_aarch64_sve_uqdecb_n32 : AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<llvm_i32_ty>;
def int_aarch64_sve_uqdecb_n64 : AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<llvm_i64_ty>;
def int_aarch64_sve_uqdech_n32 : AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<llvm_i32_ty>;
def int_aarch64_sve_uqdech_n64 : AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<llvm_i64_ty>;
def int_aarch64_sve_uqdecw_n32 : AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<llvm_i32_ty>;
def int_aarch64_sve_uqdecw_n64 : AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<llvm_i64_ty>;
def int_aarch64_sve_uqdecd_n32 : AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<llvm_i32_ty>;
def int_aarch64_sve_uqdecd_n64 : AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<llvm_i64_ty>;
def int_aarch64_sve_uqdecp_n32 : AdvSIMD_SVE_Saturating_N_Intrinsic<llvm_i32_ty>;
def int_aarch64_sve_uqdecp_n64 : AdvSIMD_SVE_Saturating_N_Intrinsic<llvm_i64_ty>;

def int_aarch64_sve_uqinch : AdvSIMD_SVE_SaturatingWithPattern_Intrinsic;
def int_aarch64_sve_uqincw : AdvSIMD_SVE_SaturatingWithPattern_Intrinsic;
def int_aarch64_sve_uqincd : AdvSIMD_SVE_SaturatingWithPattern_Intrinsic;
def int_aarch64_sve_uqincp : AdvSIMD_SVE_Saturating_Intrinsic;

def int_aarch64_sve_uqincb_n32 : AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<llvm_i32_ty>;
def int_aarch64_sve_uqincb_n64 : AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<llvm_i64_ty>;
def int_aarch64_sve_uqinch_n32 : AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<llvm_i32_ty>;
def int_aarch64_sve_uqinch_n64 : AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<llvm_i64_ty>;
def int_aarch64_sve_uqincw_n32 : AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<llvm_i32_ty>;
def int_aarch64_sve_uqincw_n64 : AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<llvm_i64_ty>;
def int_aarch64_sve_uqincd_n32 : AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<llvm_i32_ty>;
def int_aarch64_sve_uqincd_n64 : AdvSIMD_SVE_SaturatingWithPattern_N_Intrinsic<llvm_i64_ty>;
def int_aarch64_sve_uqincp_n32 : AdvSIMD_SVE_Saturating_N_Intrinsic<llvm_i32_ty>;
def int_aarch64_sve_uqincp_n64 : AdvSIMD_SVE_Saturating_N_Intrinsic<llvm_i64_ty>;

//
// Reversal
//

def int_aarch64_sve_rbit : AdvSIMD_Merged1VectorArg_Intrinsic;
def int_aarch64_sve_revb : AdvSIMD_Merged1VectorArg_Intrinsic;
def int_aarch64_sve_revh : AdvSIMD_Merged1VectorArg_Intrinsic;
def int_aarch64_sve_revw : AdvSIMD_Merged1VectorArg_Intrinsic;

//
// Permutations and selection
//

def int_aarch64_sve_clasta    : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_clasta_n  : AdvSIMD_SVE_ReduceWithInit_Intrinsic;
def int_aarch64_sve_clastb    : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_clastb_n  : AdvSIMD_SVE_ReduceWithInit_Intrinsic;
def int_aarch64_sve_compact   : AdvSIMD_Pred1VectorArg_Intrinsic;
def int_aarch64_sve_dupq_lane : AdvSIMD_SVE_DUPQ_Intrinsic;
def int_aarch64_sve_dup_laneq : SVE2_1VectorArgIndexed_Intrinsic;
def int_aarch64_sve_ext       : AdvSIMD_2VectorArgIndexed_Intrinsic;
def int_aarch64_sve_sel       : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_lasta     : AdvSIMD_SVE_Reduce_Intrinsic;
def int_aarch64_sve_lastb     : AdvSIMD_SVE_Reduce_Intrinsic;
def int_aarch64_sve_rev       : AdvSIMD_1VectorArg_Intrinsic;
def int_aarch64_sve_rev_b16   : AdvSIMD_SVE_2SVBoolArg_Intrinsic;
def int_aarch64_sve_rev_b32   : AdvSIMD_SVE_2SVBoolArg_Intrinsic;
def int_aarch64_sve_rev_b64   : AdvSIMD_SVE_2SVBoolArg_Intrinsic;
def int_aarch64_sve_splice    : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_sunpkhi   : AdvSIMD_SVE_Unpack_Intrinsic;
def int_aarch64_sve_sunpklo   : AdvSIMD_SVE_Unpack_Intrinsic;
def int_aarch64_sve_tbl       : AdvSIMD_SVE_TBL_Intrinsic;
def int_aarch64_sve_trn1      : AdvSIMD_2VectorArg_Intrinsic;
def int_aarch64_sve_trn1_b16  : AdvSIMD_SVE_3SVBoolArg_Intrinsic;
def int_aarch64_sve_trn1_b32  : AdvSIMD_SVE_3SVBoolArg_Intrinsic;
def int_aarch64_sve_trn1_b64  : AdvSIMD_SVE_3SVBoolArg_Intrinsic;
def int_aarch64_sve_trn2      : AdvSIMD_2VectorArg_Intrinsic;
def int_aarch64_sve_trn2_b16  : AdvSIMD_SVE_3SVBoolArg_Intrinsic;
def int_aarch64_sve_trn2_b32  : AdvSIMD_SVE_3SVBoolArg_Intrinsic;
def int_aarch64_sve_trn2_b64  : AdvSIMD_SVE_3SVBoolArg_Intrinsic;
def int_aarch64_sve_trn1q     : AdvSIMD_2VectorArg_Intrinsic;
def int_aarch64_sve_trn2q     : AdvSIMD_2VectorArg_Intrinsic;
def int_aarch64_sve_uunpkhi   : AdvSIMD_SVE_Unpack_Intrinsic;
def int_aarch64_sve_uunpklo   : AdvSIMD_SVE_Unpack_Intrinsic;
def int_aarch64_sve_uzp1      : AdvSIMD_2VectorArg_Intrinsic;
def int_aarch64_sve_uzp1_b16  : AdvSIMD_SVE_3SVBoolArg_Intrinsic;
def int_aarch64_sve_uzp1_b32  : AdvSIMD_SVE_3SVBoolArg_Intrinsic;
def int_aarch64_sve_uzp1_b64  : AdvSIMD_SVE_3SVBoolArg_Intrinsic;
def int_aarch64_sve_uzp2      : AdvSIMD_2VectorArg_Intrinsic;
def int_aarch64_sve_uzp2_b16  : AdvSIMD_SVE_3SVBoolArg_Intrinsic;
def int_aarch64_sve_uzp2_b32  : AdvSIMD_SVE_3SVBoolArg_Intrinsic;
def int_aarch64_sve_uzp2_b64  : AdvSIMD_SVE_3SVBoolArg_Intrinsic;
def int_aarch64_sve_uzp1q     : AdvSIMD_2VectorArg_Intrinsic;
def int_aarch64_sve_uzp2q     : AdvSIMD_2VectorArg_Intrinsic;
def int_aarch64_sve_zip1      : AdvSIMD_2VectorArg_Intrinsic;
def int_aarch64_sve_zip1_b16  : AdvSIMD_SVE_3SVBoolArg_Intrinsic;
def int_aarch64_sve_zip1_b32  : AdvSIMD_SVE_3SVBoolArg_Intrinsic;
def int_aarch64_sve_zip1_b64  : AdvSIMD_SVE_3SVBoolArg_Intrinsic;
def int_aarch64_sve_zip2      : AdvSIMD_2VectorArg_Intrinsic;
def int_aarch64_sve_zip2_b16  : AdvSIMD_SVE_3SVBoolArg_Intrinsic;
def int_aarch64_sve_zip2_b32  : AdvSIMD_SVE_3SVBoolArg_Intrinsic;
def int_aarch64_sve_zip2_b64  : AdvSIMD_SVE_3SVBoolArg_Intrinsic;
def int_aarch64_sve_zip1q     : AdvSIMD_2VectorArg_Intrinsic;
def int_aarch64_sve_zip2q     : AdvSIMD_2VectorArg_Intrinsic;

//
// Logical operations
//

def int_aarch64_sve_and  : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_and_u: AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_bic  : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_bic_u: AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_cnot : AdvSIMD_Merged1VectorArg_Intrinsic;
def int_aarch64_sve_eor  : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_eor_u: AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_not  : AdvSIMD_Merged1VectorArg_Intrinsic;
def int_aarch64_sve_orr  : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_orr_u: AdvSIMD_Pred2VectorArg_Intrinsic;

//
// Conversion
//

def int_aarch64_sve_sxtb : AdvSIMD_Merged1VectorArg_Intrinsic;
def int_aarch64_sve_sxth : AdvSIMD_Merged1VectorArg_Intrinsic;
def int_aarch64_sve_sxtw : AdvSIMD_Merged1VectorArg_Intrinsic;
def int_aarch64_sve_uxtb : AdvSIMD_Merged1VectorArg_Intrinsic;
def int_aarch64_sve_uxth : AdvSIMD_Merged1VectorArg_Intrinsic;
def int_aarch64_sve_uxtw : AdvSIMD_Merged1VectorArg_Intrinsic;

//
// While comparisons
//

def int_aarch64_sve_whilele : AdvSIMD_SVE_WHILE_Intrinsic;
def int_aarch64_sve_whilelo : AdvSIMD_SVE_WHILE_Intrinsic;
def int_aarch64_sve_whilels : AdvSIMD_SVE_WHILE_Intrinsic;
def int_aarch64_sve_whilelt : AdvSIMD_SVE_WHILE_Intrinsic;
def int_aarch64_sve_whilege : AdvSIMD_SVE_WHILE_Intrinsic;
def int_aarch64_sve_whilegt : AdvSIMD_SVE_WHILE_Intrinsic;
def int_aarch64_sve_whilehs : AdvSIMD_SVE_WHILE_Intrinsic;
def int_aarch64_sve_whilehi : AdvSIMD_SVE_WHILE_Intrinsic;

//
// Floating-point arithmetic
//

def int_aarch64_sve_fabd       : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_fabd_u     : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_fabs       : AdvSIMD_Merged1VectorArg_Intrinsic;
def int_aarch64_sve_fadd       : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_fadd_u     : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_fcadd      : AdvSIMD_SVE_CADD_Intrinsic;
def int_aarch64_sve_fcmla      : AdvSIMD_SVE_CMLA_Intrinsic;
def int_aarch64_sve_fcmla_lane : AdvSIMD_SVE_CMLA_LANE_Intrinsic;
def int_aarch64_sve_fdiv       : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_fdiv_u     : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_fdivr      : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_fexpa_x    : AdvSIMD_SVE_EXPA_Intrinsic;
def int_aarch64_sve_fmad       : AdvSIMD_Pred3VectorArg_Intrinsic;
def int_aarch64_sve_fmax       : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_fmax_u     : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_fmaxnm     : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_fmaxnm_u   : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_fmin       : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_fmin_u     : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_fminnm     : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_fminnm_u   : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_fmla       : AdvSIMD_Pred3VectorArg_Intrinsic;
def int_aarch64_sve_fmla_lane  : AdvSIMD_3VectorArgIndexed_Intrinsic;
def int_aarch64_sve_fmla_u     : AdvSIMD_Pred3VectorArg_Intrinsic;
def int_aarch64_sve_fmls       : AdvSIMD_Pred3VectorArg_Intrinsic;
def int_aarch64_sve_fmls_lane  : AdvSIMD_3VectorArgIndexed_Intrinsic;
def int_aarch64_sve_fmls_u     : AdvSIMD_Pred3VectorArg_Intrinsic;
def int_aarch64_sve_fmsb       : AdvSIMD_Pred3VectorArg_Intrinsic;
def int_aarch64_sve_fmul       : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_fmul_lane  : AdvSIMD_2VectorArgIndexed_Intrinsic;
def int_aarch64_sve_fmul_u     : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_fmulx      : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_fmulx_u    : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_fneg       : AdvSIMD_Merged1VectorArg_Intrinsic;
def int_aarch64_sve_fnmad      : AdvSIMD_Pred3VectorArg_Intrinsic;
def int_aarch64_sve_fnmla      : AdvSIMD_Pred3VectorArg_Intrinsic;
def int_aarch64_sve_fnmla_u    : AdvSIMD_Pred3VectorArg_Intrinsic;
def int_aarch64_sve_fnmls      : AdvSIMD_Pred3VectorArg_Intrinsic;
def int_aarch64_sve_fnmls_u    : AdvSIMD_Pred3VectorArg_Intrinsic;
def int_aarch64_sve_fnmsb      : AdvSIMD_Pred3VectorArg_Intrinsic;
def int_aarch64_sve_frecpe_x   : AdvSIMD_1VectorArg_Intrinsic;
def int_aarch64_sve_frecps_x   : AdvSIMD_2VectorArg_Intrinsic;
def int_aarch64_sve_frecpx     : AdvSIMD_Merged1VectorArg_Intrinsic;
def int_aarch64_sve_frinta     : AdvSIMD_Merged1VectorArg_Intrinsic;
def int_aarch64_sve_frinti     : AdvSIMD_Merged1VectorArg_Intrinsic;
def int_aarch64_sve_frintm     : AdvSIMD_Merged1VectorArg_Intrinsic;
def int_aarch64_sve_frintn     : AdvSIMD_Merged1VectorArg_Intrinsic;
def int_aarch64_sve_frintp     : AdvSIMD_Merged1VectorArg_Intrinsic;
def int_aarch64_sve_frintx     : AdvSIMD_Merged1VectorArg_Intrinsic;
def int_aarch64_sve_frintz     : AdvSIMD_Merged1VectorArg_Intrinsic;
def int_aarch64_sve_frsqrte_x  : AdvSIMD_1VectorArg_Intrinsic;
def int_aarch64_sve_frsqrts_x  : AdvSIMD_2VectorArg_Intrinsic;
def int_aarch64_sve_fscale     : AdvSIMD_SVE_SCALE_Intrinsic;
def int_aarch64_sve_fsqrt      : AdvSIMD_Merged1VectorArg_Intrinsic;
def int_aarch64_sve_fsub       : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_fsub_u     : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_fsubr      : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_ftmad_x    : AdvSIMD_2VectorArgIndexed_Intrinsic;
def int_aarch64_sve_ftsmul_x   : AdvSIMD_SVE_TSMUL_Intrinsic;
def int_aarch64_sve_ftssel_x   : AdvSIMD_SVE_TSMUL_Intrinsic;

//
// Floating-point reductions
//

def int_aarch64_sve_fadda   : AdvSIMD_SVE_ReduceWithInit_Intrinsic;
def int_aarch64_sve_faddv   : AdvSIMD_SVE_Reduce_Intrinsic;
def int_aarch64_sve_fmaxv   : AdvSIMD_SVE_Reduce_Intrinsic;
def int_aarch64_sve_fmaxnmv : AdvSIMD_SVE_Reduce_Intrinsic;
def int_aarch64_sve_fminv   : AdvSIMD_SVE_Reduce_Intrinsic;
def int_aarch64_sve_fminnmv : AdvSIMD_SVE_Reduce_Intrinsic;

def int_aarch64_sve_faddqv   : AdvSIMD_SVE_V128_Reduce_Intrinsic;
def int_aarch64_sve_fmaxnmqv : AdvSIMD_SVE_V128_Reduce_Intrinsic;
def int_aarch64_sve_fminnmqv : AdvSIMD_SVE_V128_Reduce_Intrinsic;
def int_aarch64_sve_fmaxqv   : AdvSIMD_SVE_V128_Reduce_Intrinsic;
def int_aarch64_sve_fminqv   : AdvSIMD_SVE_V128_Reduce_Intrinsic;

//
// Floating-point conversions
//

def int_aarch64_sve_fcvt   : AdvSIMD_SVE_FCVT_Intrinsic;
def int_aarch64_sve_fcvtzs : AdvSIMD_SVE_FCVTZS_Intrinsic;
def int_aarch64_sve_fcvtzu : AdvSIMD_SVE_FCVTZS_Intrinsic;
def int_aarch64_sve_scvtf  : AdvSIMD_SVE_SCVTF_Intrinsic;
def int_aarch64_sve_ucvtf  : AdvSIMD_SVE_SCVTF_Intrinsic;

//
// Floating-point comparisons
//

def int_aarch64_sve_facge : AdvSIMD_SVE_Compare_Intrinsic;
def int_aarch64_sve_facgt : AdvSIMD_SVE_Compare_Intrinsic;

def int_aarch64_sve_fcmpeq : AdvSIMD_SVE_Compare_Intrinsic;
def int_aarch64_sve_fcmpge : AdvSIMD_SVE_Compare_Intrinsic;
def int_aarch64_sve_fcmpgt : AdvSIMD_SVE_Compare_Intrinsic;
def int_aarch64_sve_fcmpne : AdvSIMD_SVE_Compare_Intrinsic;
def int_aarch64_sve_fcmpuo : AdvSIMD_SVE_Compare_Intrinsic;

def int_aarch64_sve_fcvtzs_i32f16   : Builtin_SVCVT<llvm_nxv4i32_ty, llvm_nxv4i1_ty, llvm_nxv8f16_ty>;
def int_aarch64_sve_fcvtzs_i32f64   : Builtin_SVCVT<llvm_nxv4i32_ty, llvm_nxv2i1_ty, llvm_nxv2f64_ty>;
def int_aarch64_sve_fcvtzs_i64f16   : Builtin_SVCVT<llvm_nxv2i64_ty, llvm_nxv2i1_ty, llvm_nxv8f16_ty>;
def int_aarch64_sve_fcvtzs_i64f32   : Builtin_SVCVT<llvm_nxv2i64_ty, llvm_nxv2i1_ty, llvm_nxv4f32_ty>;

def int_aarch64_sve_fcvt_bf16f32    : Builtin_SVCVT<llvm_nxv8bf16_ty, llvm_nxv8i1_ty, llvm_nxv4f32_ty>;
def int_aarch64_sve_fcvtnt_bf16f32  : Builtin_SVCVT<llvm_nxv8bf16_ty, llvm_nxv8i1_ty, llvm_nxv4f32_ty>;

def int_aarch64_sve_fcvtzu_i32f16   : Builtin_SVCVT<llvm_nxv4i32_ty, llvm_nxv4i1_ty, llvm_nxv8f16_ty>;
def int_aarch64_sve_fcvtzu_i32f64   : Builtin_SVCVT<llvm_nxv4i32_ty, llvm_nxv2i1_ty, llvm_nxv2f64_ty>;
def int_aarch64_sve_fcvtzu_i64f16   : Builtin_SVCVT<llvm_nxv2i64_ty, llvm_nxv2i1_ty, llvm_nxv8f16_ty>;
def int_aarch64_sve_fcvtzu_i64f32   : Builtin_SVCVT<llvm_nxv2i64_ty, llvm_nxv2i1_ty, llvm_nxv4f32_ty>;

def int_aarch64_sve_fcvt_f16f32     : Builtin_SVCVT<llvm_nxv8f16_ty, llvm_nxv4i1_ty, llvm_nxv4f32_ty>;
def int_aarch64_sve_fcvt_f16f64     : Builtin_SVCVT<llvm_nxv8f16_ty, llvm_nxv2i1_ty, llvm_nxv2f64_ty>;
def int_aarch64_sve_fcvt_f32f64     : Builtin_SVCVT<llvm_nxv4f32_ty, llvm_nxv2i1_ty, llvm_nxv2f64_ty>;

def int_aarch64_sve_fcvt_f32f16     : Builtin_SVCVT<llvm_nxv4f32_ty, llvm_nxv4i1_ty, llvm_nxv8f16_ty>;
def int_aarch64_sve_fcvt_f64f16     : Builtin_SVCVT<llvm_nxv2f64_ty, llvm_nxv2i1_ty, llvm_nxv8f16_ty>;
def int_aarch64_sve_fcvt_f64f32     : Builtin_SVCVT<llvm_nxv2f64_ty, llvm_nxv2i1_ty, llvm_nxv4f32_ty>;

def int_aarch64_sve_fcvtlt_f32f16   : Builtin_SVCVT<llvm_nxv4f32_ty, llvm_nxv4i1_ty, llvm_nxv8f16_ty>;
def int_aarch64_sve_fcvtlt_f64f32   : Builtin_SVCVT<llvm_nxv2f64_ty, llvm_nxv2i1_ty, llvm_nxv4f32_ty>;
def int_aarch64_sve_fcvtnt_f16f32   : Builtin_SVCVT<llvm_nxv8f16_ty, llvm_nxv4i1_ty, llvm_nxv4f32_ty>;
def int_aarch64_sve_fcvtnt_f32f64   : Builtin_SVCVT<llvm_nxv4f32_ty, llvm_nxv2i1_ty, llvm_nxv2f64_ty>;

def int_aarch64_sve_fcvtx_f32f64    : Builtin_SVCVT<llvm_nxv4f32_ty, llvm_nxv2i1_ty, llvm_nxv2f64_ty>;
def int_aarch64_sve_fcvtxnt_f32f64  : Builtin_SVCVT<llvm_nxv4f32_ty, llvm_nxv2i1_ty, llvm_nxv2f64_ty>;

def int_aarch64_sve_scvtf_f16i32    : Builtin_SVCVT<llvm_nxv8f16_ty, llvm_nxv4i1_ty, llvm_nxv4i32_ty>;
def int_aarch64_sve_scvtf_f16i64    : Builtin_SVCVT<llvm_nxv8f16_ty, llvm_nxv2i1_ty, llvm_nxv2i64_ty>;
def int_aarch64_sve_scvtf_f32i64    : Builtin_SVCVT<llvm_nxv4f32_ty, llvm_nxv2i1_ty, llvm_nxv2i64_ty>;
def int_aarch64_sve_scvtf_f64i32    : Builtin_SVCVT<llvm_nxv2f64_ty, llvm_nxv2i1_ty, llvm_nxv4i32_ty>;

def int_aarch64_sve_ucvtf_f16i32    : Builtin_SVCVT<llvm_nxv8f16_ty, llvm_nxv4i1_ty, llvm_nxv4i32_ty>;
def int_aarch64_sve_ucvtf_f16i64    : Builtin_SVCVT<llvm_nxv8f16_ty, llvm_nxv2i1_ty, llvm_nxv2i64_ty>;
def int_aarch64_sve_ucvtf_f32i64    : Builtin_SVCVT<llvm_nxv4f32_ty, llvm_nxv2i1_ty, llvm_nxv2i64_ty>;
def int_aarch64_sve_ucvtf_f64i32    : Builtin_SVCVT<llvm_nxv2f64_ty, llvm_nxv2i1_ty, llvm_nxv4i32_ty>;

//
// Predicate creation
//

def int_aarch64_sve_ptrue : AdvSIMD_SVE_PTRUE_Intrinsic;

//
// Predicate operations
//

def int_aarch64_sve_and_z   : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_bic_z   : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_brka    : AdvSIMD_Merged1VectorArg_Intrinsic;
def int_aarch64_sve_brka_z  : AdvSIMD_Pred1VectorArg_Intrinsic;
def int_aarch64_sve_brkb    : AdvSIMD_Merged1VectorArg_Intrinsic;
def int_aarch64_sve_brkb_z  : AdvSIMD_Pred1VectorArg_Intrinsic;
def int_aarch64_sve_brkn_z  : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_brkpa_z : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_brkpb_z : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_eor_z   : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_nand_z  : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_nor_z   : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_orn_z   : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_orr_z   : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_pfirst  : AdvSIMD_Pred1VectorArg_Intrinsic;
def int_aarch64_sve_pnext   : AdvSIMD_Pred1VectorArg_Intrinsic;
def int_aarch64_sve_punpkhi : AdvSIMD_SVE_PUNPKHI_Intrinsic;
def int_aarch64_sve_punpklo : AdvSIMD_SVE_PUNPKHI_Intrinsic;

//
// Testing predicates
//

def int_aarch64_sve_ptest_any   : AdvSIMD_SVE_PTEST_Intrinsic;
def int_aarch64_sve_ptest_first : AdvSIMD_SVE_PTEST_Intrinsic;
def int_aarch64_sve_ptest_last  : AdvSIMD_SVE_PTEST_Intrinsic;

//
// Reinterpreting data
//

def int_aarch64_sve_convert_from_svbool : DefaultAttrsIntrinsic<[llvm_any_ty],
                                                    [llvm_nxv16i1_ty],
                                                    [IntrNoMem]>;

def int_aarch64_sve_convert_to_svbool : DefaultAttrsIntrinsic<[llvm_nxv16i1_ty],
                                                  [llvm_any_ty],
                                                  [IntrNoMem]>;

//
// Gather loads: scalar base + vector offsets
//

// 64 bit unscaled offsets
def int_aarch64_sve_ld1_gather : AdvSIMD_GatherLoad_SV_64b_Offsets_Intrinsic;

// 64 bit scaled offsets
def int_aarch64_sve_ld1_gather_index : AdvSIMD_GatherLoad_SV_64b_Offsets_Intrinsic;

// 32 bit unscaled offsets, sign (sxtw) or zero (zxtw) extended to 64 bits
def int_aarch64_sve_ld1_gather_sxtw : AdvSIMD_GatherLoad_SV_32b_Offsets_Intrinsic;
def int_aarch64_sve_ld1_gather_uxtw : AdvSIMD_GatherLoad_SV_32b_Offsets_Intrinsic;

// 32 bit scaled offsets, sign (sxtw) or zero (zxtw) extended to 64 bits
def int_aarch64_sve_ld1_gather_sxtw_index : AdvSIMD_GatherLoad_SV_32b_Offsets_Intrinsic;
def int_aarch64_sve_ld1_gather_uxtw_index : AdvSIMD_GatherLoad_SV_32b_Offsets_Intrinsic;

// 128-bit loads, scaled offsets (indices)
def int_aarch64_sve_ld1q_gather_index : AdvSIMD_GatherLoadQ_SV_Intrinsic;

// 128-bit loads, unscaled offsets
def int_aarch64_sve_ld1q_gather_vector_offset : AdvSIMD_GatherLoadQ_SV_Intrinsic;

//
// Gather loads: vector base + scalar offset
//

def int_aarch64_sve_ld1_gather_scalar_offset : AdvSIMD_GatherLoad_VS_Intrinsic;

// 128-bit loads, unscaled offsets
def int_aarch64_sve_ld1q_gather_scalar_offset : AdvSIMD_GatherLoadQ_VS_Intrinsic;

//
// First-faulting gather loads: scalar base + vector offsets
//

// 64 bit unscaled offsets
def int_aarch64_sve_ldff1_gather : AdvSIMD_GatherLoad_SV_64b_Offsets_WriteFFR_Intrinsic;

// 64 bit scaled offsets
def int_aarch64_sve_ldff1_gather_index : AdvSIMD_GatherLoad_SV_64b_Offsets_WriteFFR_Intrinsic;

// 32 bit unscaled offsets, sign (sxtw) or zero (uxtw) extended to 64 bits
def int_aarch64_sve_ldff1_gather_sxtw : AdvSIMD_GatherLoad_SV_32b_Offsets_WriteFFR_Intrinsic;
def int_aarch64_sve_ldff1_gather_uxtw : AdvSIMD_GatherLoad_SV_32b_Offsets_WriteFFR_Intrinsic;

// 32 bit scaled offsets, sign (sxtw) or zero (uxtw) extended to 64 bits
def int_aarch64_sve_ldff1_gather_sxtw_index : AdvSIMD_GatherLoad_SV_32b_Offsets_WriteFFR_Intrinsic;
def int_aarch64_sve_ldff1_gather_uxtw_index : AdvSIMD_GatherLoad_SV_32b_Offsets_WriteFFR_Intrinsic;

//
// First-faulting gather loads: vector base + scalar offset
//

def int_aarch64_sve_ldff1_gather_scalar_offset : AdvSIMD_GatherLoad_VS_WriteFFR_Intrinsic;


//
// Non-temporal gather loads: scalar base + vector offsets
//

// 64 bit unscaled offsets
def int_aarch64_sve_ldnt1_gather : AdvSIMD_GatherLoad_SV_64b_Offsets_Intrinsic;

// 64 bit indices
def int_aarch64_sve_ldnt1_gather_index : AdvSIMD_GatherLoad_SV_64b_Offsets_Intrinsic;

// 32 bit unscaled offsets, zero (zxtw) extended to 64 bits
def int_aarch64_sve_ldnt1_gather_uxtw : AdvSIMD_GatherLoad_SV_32b_Offsets_Intrinsic;

//
// Non-temporal gather loads: vector base + scalar offset
//

def int_aarch64_sve_ldnt1_gather_scalar_offset  : AdvSIMD_GatherLoad_VS_Intrinsic;

//
// Scatter stores: scalar base + vector offsets
//

// 64 bit unscaled offsets
def int_aarch64_sve_st1_scatter : AdvSIMD_ScatterStore_SV_64b_Offsets_Intrinsic;

// 64 bit scaled offsets
def int_aarch64_sve_st1_scatter_index
    : AdvSIMD_ScatterStore_SV_64b_Offsets_Intrinsic;

// 32 bit unscaled offsets, sign (sxtw) or zero (zxtw) extended to 64 bits
def int_aarch64_sve_st1_scatter_sxtw
    : AdvSIMD_ScatterStore_SV_32b_Offsets_Intrinsic;

def int_aarch64_sve_st1_scatter_uxtw
    : AdvSIMD_ScatterStore_SV_32b_Offsets_Intrinsic;

// 32 bit scaled offsets, sign (sxtw) or zero (zxtw) extended to 64 bits
def int_aarch64_sve_st1_scatter_sxtw_index
    : AdvSIMD_ScatterStore_SV_32b_Offsets_Intrinsic;

def int_aarch64_sve_st1_scatter_uxtw_index
    : AdvSIMD_ScatterStore_SV_32b_Offsets_Intrinsic;

// 128-bit stores, scaled offsets (indices)
def int_aarch64_sve_st1q_scatter_index : AdvSIMD_ScatterStoreQ_SV_Intrinsic;

// 128-bit stores, unscaled offsets
def int_aarch64_sve_st1q_scatter_vector_offset : AdvSIMD_ScatterStoreQ_SV_Intrinsic;

//
// Scatter stores: vector base + scalar offset
//

def int_aarch64_sve_st1_scatter_scalar_offset : AdvSIMD_ScatterStore_VS_Intrinsic;

// 128-bit stores, unscaled offsets
def int_aarch64_sve_st1q_scatter_scalar_offset : AdvSIMD_ScatterStoreQ_VS_Intrinsic;

//
// Non-temporal scatter stores: scalar base + vector offsets
//

// 64 bit unscaled offsets
def int_aarch64_sve_stnt1_scatter : AdvSIMD_ScatterStore_SV_64b_Offsets_Intrinsic;

// 64 bit indices
def int_aarch64_sve_stnt1_scatter_index
    : AdvSIMD_ScatterStore_SV_64b_Offsets_Intrinsic;

// 32 bit unscaled offsets, zero (zxtw) extended to 64 bits
def int_aarch64_sve_stnt1_scatter_uxtw : AdvSIMD_ScatterStore_SV_32b_Offsets_Intrinsic;

//
// Non-temporal scatter stores: vector base + scalar offset
//

def int_aarch64_sve_stnt1_scatter_scalar_offset  : AdvSIMD_ScatterStore_VS_Intrinsic;

//
// SVE2 - Uniform DSP operations
//

def int_aarch64_sve_saba          : AdvSIMD_3VectorArg_Intrinsic;
def int_aarch64_sve_shadd         : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_shsub         : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_shsubr        : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_sli           : AdvSIMD_2VectorArgIndexed_Intrinsic;
def int_aarch64_sve_sqabs         : AdvSIMD_Merged1VectorArg_Intrinsic;
def int_aarch64_sve_sqadd         : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_sqdmulh       : AdvSIMD_2VectorArg_Intrinsic;
def int_aarch64_sve_sqdmulh_lane  : AdvSIMD_2VectorArgIndexed_Intrinsic;
def int_aarch64_sve_sqneg         : AdvSIMD_Merged1VectorArg_Intrinsic;
def int_aarch64_sve_sqrdmlah      : AdvSIMD_3VectorArg_Intrinsic;
def int_aarch64_sve_sqrdmlah_lane : AdvSIMD_3VectorArgIndexed_Intrinsic;
def int_aarch64_sve_sqrdmlsh      : AdvSIMD_3VectorArg_Intrinsic;
def int_aarch64_sve_sqrdmlsh_lane : AdvSIMD_3VectorArgIndexed_Intrinsic;
def int_aarch64_sve_sqrdmulh      : AdvSIMD_2VectorArg_Intrinsic;
def int_aarch64_sve_sqrdmulh_lane : AdvSIMD_2VectorArgIndexed_Intrinsic;
def int_aarch64_sve_sqrshl        : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_sqshl         : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_sqshlu        : AdvSIMD_SVE_ShiftByImm_Intrinsic;
def int_aarch64_sve_sqsub         : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_sqsub_u       : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_sqsubr        : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_srhadd        : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_sri           : AdvSIMD_2VectorArgIndexed_Intrinsic;
def int_aarch64_sve_srshl         : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_srshr         : AdvSIMD_SVE_ShiftByImm_Intrinsic;
def int_aarch64_sve_srsra         : AdvSIMD_2VectorArgIndexed_Intrinsic;
def int_aarch64_sve_ssra          : AdvSIMD_2VectorArgIndexed_Intrinsic;
def int_aarch64_sve_suqadd        : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_uaba          : AdvSIMD_3VectorArg_Intrinsic;
def int_aarch64_sve_uhadd         : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_uhsub         : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_uhsubr        : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_uqadd         : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_uqrshl        : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_uqshl         : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_uqsub         : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_uqsub_u       : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_uqsubr        : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_urecpe        : AdvSIMD_Merged1VectorArg_Intrinsic;
def int_aarch64_sve_urhadd        : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_urshl         : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_urshr         : AdvSIMD_SVE_ShiftByImm_Intrinsic;
def int_aarch64_sve_ursqrte       : AdvSIMD_Merged1VectorArg_Intrinsic;
def int_aarch64_sve_ursra         : AdvSIMD_2VectorArgIndexed_Intrinsic;
def int_aarch64_sve_usqadd        : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_usra          : AdvSIMD_2VectorArgIndexed_Intrinsic;

//
// SVE2 - Widening DSP operations
//

def int_aarch64_sve_sabalb : SVE2_3VectorArg_Long_Intrinsic;
def int_aarch64_sve_sabalt : SVE2_3VectorArg_Long_Intrinsic;
def int_aarch64_sve_sabdlb : SVE2_2VectorArg_Long_Intrinsic;
def int_aarch64_sve_sabdlt : SVE2_2VectorArg_Long_Intrinsic;
def int_aarch64_sve_saddlb : SVE2_2VectorArg_Long_Intrinsic;
def int_aarch64_sve_saddlt : SVE2_2VectorArg_Long_Intrinsic;
def int_aarch64_sve_saddwb : SVE2_2VectorArg_Wide_Intrinsic;
def int_aarch64_sve_saddwt : SVE2_2VectorArg_Wide_Intrinsic;
def int_aarch64_sve_sshllb : SVE2_1VectorArg_Long_Intrinsic;
def int_aarch64_sve_sshllt : SVE2_1VectorArg_Long_Intrinsic;
def int_aarch64_sve_ssublb : SVE2_2VectorArg_Long_Intrinsic;
def int_aarch64_sve_ssublt : SVE2_2VectorArg_Long_Intrinsic;
def int_aarch64_sve_ssubwb : SVE2_2VectorArg_Wide_Intrinsic;
def int_aarch64_sve_ssubwt : SVE2_2VectorArg_Wide_Intrinsic;
def int_aarch64_sve_uabalb : SVE2_3VectorArg_Long_Intrinsic;
def int_aarch64_sve_uabalt : SVE2_3VectorArg_Long_Intrinsic;
def int_aarch64_sve_uabdlb : SVE2_2VectorArg_Long_Intrinsic;
def int_aarch64_sve_uabdlt : SVE2_2VectorArg_Long_Intrinsic;
def int_aarch64_sve_uaddlb : SVE2_2VectorArg_Long_Intrinsic;
def int_aarch64_sve_uaddlt : SVE2_2VectorArg_Long_Intrinsic;
def int_aarch64_sve_uaddwb : SVE2_2VectorArg_Wide_Intrinsic;
def int_aarch64_sve_uaddwt : SVE2_2VectorArg_Wide_Intrinsic;
def int_aarch64_sve_ushllb : SVE2_1VectorArg_Long_Intrinsic;
def int_aarch64_sve_ushllt : SVE2_1VectorArg_Long_Intrinsic;
def int_aarch64_sve_usublb : SVE2_2VectorArg_Long_Intrinsic;
def int_aarch64_sve_usublt : SVE2_2VectorArg_Long_Intrinsic;
def int_aarch64_sve_usubwb : SVE2_2VectorArg_Wide_Intrinsic;
def int_aarch64_sve_usubwt : SVE2_2VectorArg_Wide_Intrinsic;

//
// SVE2 - Non-widening pairwise arithmetic
//

def int_aarch64_sve_addp    : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_faddp   : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_fmaxp   : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_fmaxnmp : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_fminp   : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_fminnmp : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_smaxp   : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_sminp   : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_umaxp   : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_uminp   : AdvSIMD_Pred2VectorArg_Intrinsic;

//
// SVE2 - Widening pairwise arithmetic
//

def int_aarch64_sve_sadalp : SVE2_2VectorArg_Pred_Long_Intrinsic;
def int_aarch64_sve_uadalp : SVE2_2VectorArg_Pred_Long_Intrinsic;

//
// SVE2 - Uniform complex integer arithmetic
//

def int_aarch64_sve_cadd_x           : AdvSIMD_SVE2_CADD_Intrinsic;
def int_aarch64_sve_sqcadd_x         : AdvSIMD_SVE2_CADD_Intrinsic;
def int_aarch64_sve_cmla_x           : AdvSIMD_SVE2_CMLA_Intrinsic;
def int_aarch64_sve_cmla_lane_x      : AdvSIMD_SVE_CMLA_LANE_Intrinsic;
def int_aarch64_sve_sqrdcmlah_x      : AdvSIMD_SVE2_CMLA_Intrinsic;
def int_aarch64_sve_sqrdcmlah_lane_x : AdvSIMD_SVE_CMLA_LANE_Intrinsic;

//
// SVE2 - Widening complex integer arithmetic
//

def int_aarch64_sve_saddlbt   : SVE2_2VectorArg_Long_Intrinsic;
def int_aarch64_sve_ssublbt   : SVE2_2VectorArg_Long_Intrinsic;
def int_aarch64_sve_ssubltb   : SVE2_2VectorArg_Long_Intrinsic;

//
// SVE2 - Widening complex integer dot product
//

def int_aarch64_sve_cdot      : AdvSIMD_SVE_DOT_Indexed_Intrinsic;
def int_aarch64_sve_cdot_lane : AdvSIMD_SVE_CDOT_LANE_Intrinsic;

//
// SVE2 - Floating-point widening multiply-accumulate
//

def int_aarch64_sve_fmlalb        : SVE2_3VectorArg_Long_Intrinsic;
def int_aarch64_sve_fmlalb_lane   : SVE2_3VectorArgIndexed_Long_Intrinsic;
def int_aarch64_sve_fmlalt        : SVE2_3VectorArg_Long_Intrinsic;
def int_aarch64_sve_fmlalt_lane   : SVE2_3VectorArgIndexed_Long_Intrinsic;
def int_aarch64_sve_fmlslb        : SVE2_3VectorArg_Long_Intrinsic;
def int_aarch64_sve_fmlslb_lane   : SVE2_3VectorArgIndexed_Long_Intrinsic;
def int_aarch64_sve_fmlslt        : SVE2_3VectorArg_Long_Intrinsic;
def int_aarch64_sve_fmlslt_lane   : SVE2_3VectorArgIndexed_Long_Intrinsic;

//
// SVE2 - Floating-point integer binary logarithm
//

def int_aarch64_sve_flogb : AdvSIMD_SVE_LOGB_Intrinsic;

//
// SVE2 - Vector histogram count
//

def int_aarch64_sve_histcnt : AdvSIMD_Pred2VectorArg_Intrinsic;
def int_aarch64_sve_histseg : AdvSIMD_2VectorArg_Intrinsic;

//
// SVE2 - Character match
//

def int_aarch64_sve_match   : AdvSIMD_SVE_Compare_Intrinsic;
def int_aarch64_sve_nmatch  : AdvSIMD_SVE_Compare_Intrinsic;

//
// SVE2 - Unary narrowing operations
//

def int_aarch64_sve_sqxtnb  : SVE2_1VectorArg_Narrowing_Intrinsic;
def int_aarch64_sve_sqxtnt  : SVE2_Merged1VectorArg_Narrowing_Intrinsic;
def int_aarch64_sve_sqxtunb : SVE2_1VectorArg_Narrowing_Intrinsic;
def int_aarch64_sve_sqxtunt : SVE2_Merged1VectorArg_Narrowing_Intrinsic;
def int_aarch64_sve_uqxtnb  : SVE2_1VectorArg_Narrowing_Intrinsic;
def int_aarch64_sve_uqxtnt  : SVE2_Merged1VectorArg_Narrowing_Intrinsic;

//
// SVE2 - Binary narrowing DSP operations
//
def int_aarch64_sve_addhnb    : SVE2_2VectorArg_Narrowing_Intrinsic;
def int_aarch64_sve_addhnt    : SVE2_Merged2VectorArg_Narrowing_Intrinsic;

def int_aarch64_sve_raddhnb   : SVE2_2VectorArg_Narrowing_Intrinsic;
def int_aarch64_sve_raddhnt   : SVE2_Merged2VectorArg_Narrowing_Intrinsic;

def int_aarch64_sve_subhnb    : SVE2_2VectorArg_Narrowing_Intrinsic;
def int_aarch64_sve_subhnt    : SVE2_Merged2VectorArg_Narrowing_Intrinsic;

def int_aarch64_sve_rsubhnb   : SVE2_2VectorArg_Narrowing_Intrinsic;
def int_aarch64_sve_rsubhnt   : SVE2_Merged2VectorArg_Narrowing_Intrinsic;

// Narrowing shift right
def int_aarch64_sve_shrnb     : SVE2_1VectorArg_Imm_Narrowing_Intrinsic;
def int_aarch64_sve_shrnt     : SVE2_2VectorArg_Imm_Narrowing_Intrinsic;

def int_aarch64_sve_rshrnb    : SVE2_1VectorArg_Imm_Narrowing_Intrinsic;
def int_aarch64_sve_rshrnt    : SVE2_2VectorArg_Imm_Narrowing_Intrinsic;

// Saturating shift right - signed input/output
def int_aarch64_sve_sqshrnb   : SVE2_1VectorArg_Imm_Narrowing_Intrinsic;
def int_aarch64_sve_sqshrnt   : SVE2_2VectorArg_Imm_Narrowing_Intrinsic;

def int_aarch64_sve_sqrshrnb  : SVE2_1VectorArg_Imm_Narrowing_Intrinsic;
def int_aarch64_sve_sqrshrnt  : SVE2_2VectorArg_Imm_Narrowing_Intrinsic;

// Saturating shift right - unsigned input/output
def int_aarch64_sve_uqshrnb   : SVE2_1VectorArg_Imm_Narrowing_Intrinsic;
def int_aarch64_sve_uqshrnt   : SVE2_2VectorArg_Imm_Narrowing_Intrinsic;

def int_aarch64_sve_uqrshrnb  : SVE2_1VectorArg_Imm_Narrowing_Intrinsic;
def int_aarch64_sve_uqrshrnt  : SVE2_2VectorArg_Imm_Narrowing_Intrinsic;

// Saturating shift right - signed input, unsigned output
def int_aarch64_sve_sqshrunb  : SVE2_1VectorArg_Imm_Narrowing_Intrinsic;
def int_aarch64_sve_sqshrunt  : SVE2_2VectorArg_Imm_Narrowing_Intrinsic;

def int_aarch64_sve_sqrshrunb : SVE2_1VectorArg_Imm_Narrowing_Intrinsic;
def int_aarch64_sve_sqrshrunt : SVE2_2VectorArg_Imm_Narrowing_Intrinsic;

// SVE2 MLA LANE.
def int_aarch64_sve_smlalb_lane   : SVE2_3VectorArg_Indexed_Intrinsic;
def int_aarch64_sve_smlalt_lane   : SVE2_3VectorArg_Indexed_Intrinsic;
def int_aarch64_sve_umlalb_lane   : SVE2_3VectorArg_Indexed_Intrinsic;
def int_aarch64_sve_umlalt_lane   : SVE2_3VectorArg_Indexed_Intrinsic;
def int_aarch64_sve_smlslb_lane   : SVE2_3VectorArg_Indexed_Intrinsic;
def int_aarch64_sve_smlslt_lane   : SVE2_3VectorArg_Indexed_Intrinsic;
def int_aarch64_sve_umlslb_lane   : SVE2_3VectorArg_Indexed_Intrinsic;
def int_aarch64_sve_umlslt_lane   : SVE2_3VectorArg_Indexed_Intrinsic;
def int_aarch64_sve_smullb_lane   : SVE2_2VectorArgIndexed_Long_Intrinsic;
def int_aarch64_sve_smullt_lane   : SVE2_2VectorArgIndexed_Long_Intrinsic;
def int_aarch64_sve_umullb_lane   : SVE2_2VectorArgIndexed_Long_Intrinsic;
def int_aarch64_sve_umullt_lane   : SVE2_2VectorArgIndexed_Long_Intrinsic;
def int_aarch64_sve_sqdmlalb_lane : SVE2_3VectorArg_Indexed_Intrinsic;
def int_aarch64_sve_sqdmlalt_lane : SVE2_3VectorArg_Indexed_Intrinsic;
def int_aarch64_sve_sqdmlslb_lane : SVE2_3VectorArg_Indexed_Intrinsic;
def int_aarch64_sve_sqdmlslt_lane : SVE2_3VectorArg_Indexed_Intrinsic;
def int_aarch64_sve_sqdmullb_lane : SVE2_2VectorArgIndexed_Long_Intrinsic;
def int_aarch64_sve_sqdmullt_lane : SVE2_2VectorArgIndexed_Long_Intrinsic;

// SVE2 MLA Unpredicated.
def int_aarch64_sve_smlalb      : SVE2_3VectorArg_Long_Intrinsic;
def int_aarch64_sve_smlalt      : SVE2_3VectorArg_Long_Intrinsic;
def int_aarch64_sve_umlalb      : SVE2_3VectorArg_Long_Intrinsic;
def int_aarch64_sve_umlalt      : SVE2_3VectorArg_Long_Intrinsic;
def int_aarch64_sve_smlslb      : SVE2_3VectorArg_Long_Intrinsic;
def int_aarch64_sve_smlslt      : SVE2_3VectorArg_Long_Intrinsic;
def int_aarch64_sve_umlslb      : SVE2_3VectorArg_Long_Intrinsic;
def int_aarch64_sve_umlslt      : SVE2_3VectorArg_Long_Intrinsic;
def int_aarch64_sve_smullb      : SVE2_2VectorArg_Long_Intrinsic;
def int_aarch64_sve_smullt      : SVE2_2VectorArg_Long_Intrinsic;
def int_aarch64_sve_umullb      : SVE2_2VectorArg_Long_Intrinsic;
def int_aarch64_sve_umullt      : SVE2_2VectorArg_Long_Intrinsic;

def int_aarch64_sve_sqdmlalb    : SVE2_3VectorArg_Long_Intrinsic;
def int_aarch64_sve_sqdmlalt    : SVE2_3VectorArg_Long_Intrinsic;
def int_aarch64_sve_sqdmlslb    : SVE2_3VectorArg_Long_Intrinsic;
def int_aarch64_sve_sqdmlslt    : SVE2_3VectorArg_Long_Intrinsic;
def int_aarch64_sve_sqdmullb    : SVE2_2VectorArg_Long_Intrinsic;
def int_aarch64_sve_sqdmullt    : SVE2_2VectorArg_Long_Intrinsic;
def int_aarch64_sve_sqdmlalbt   : SVE2_3VectorArg_Long_Intrinsic;
def int_aarch64_sve_sqdmlslbt   : SVE2_3VectorArg_Long_Intrinsic;

// SVE2 ADDSUB Long Unpredicated.
def int_aarch64_sve_adclb       : AdvSIMD_3VectorArg_Intrinsic;
def int_aarch64_sve_adclt       : AdvSIMD_3VectorArg_Intrinsic;
def int_aarch64_sve_sbclb       : AdvSIMD_3VectorArg_Intrinsic;
def int_aarch64_sve_sbclt       : AdvSIMD_3VectorArg_Intrinsic;

//
// SVE2 - Polynomial arithmetic
//
def int_aarch64_sve_eorbt       : AdvSIMD_3VectorArg_Intrinsic;
def int_aarch64_sve_eortb       : AdvSIMD_3VectorArg_Intrinsic;
def int_aarch64_sve_pmullb_pair : AdvSIMD_2VectorArg_Intrinsic;
def int_aarch64_sve_pmullt_pair : AdvSIMD_2VectorArg_Intrinsic;

//
// SVE2 bitwise ternary operations.
//
def int_aarch64_sve_eor3   : AdvSIMD_3VectorArg_Intrinsic;
def int_aarch64_sve_bcax   : AdvSIMD_3VectorArg_Intrinsic;
def int_aarch64_sve_bsl    : AdvSIMD_3VectorArg_Intrinsic;
def int_aarch64_sve_bsl1n  : AdvSIMD_3VectorArg_Intrinsic;
def int_aarch64_sve_bsl2n  : AdvSIMD_3VectorArg_Intrinsic;
def int_aarch64_sve_nbsl   : AdvSIMD_3VectorArg_Intrinsic;
def int_aarch64_sve_xar    : AdvSIMD_2VectorArgIndexed_Intrinsic;

//
// SVE2 - Optional AES, SHA-3 and SM4
//

def int_aarch64_sve_aesd    : ClangBuiltin<"__builtin_sve_svaesd_u8">,
                              DefaultAttrsIntrinsic<[llvm_nxv16i8_ty],
                                        [llvm_nxv16i8_ty, llvm_nxv16i8_ty],
                                        [IntrNoMem]>;
def int_aarch64_sve_aesimc  : ClangBuiltin<"__builtin_sve_svaesimc_u8">,
                              DefaultAttrsIntrinsic<[llvm_nxv16i8_ty],
                                        [llvm_nxv16i8_ty],
                                        [IntrNoMem]>;
def int_aarch64_sve_aese    : ClangBuiltin<"__builtin_sve_svaese_u8">,
                              DefaultAttrsIntrinsic<[llvm_nxv16i8_ty],
                                        [llvm_nxv16i8_ty, llvm_nxv16i8_ty],
                                        [IntrNoMem]>;
def int_aarch64_sve_aesmc   : ClangBuiltin<"__builtin_sve_svaesmc_u8">,
                              DefaultAttrsIntrinsic<[llvm_nxv16i8_ty],
                                        [llvm_nxv16i8_ty],
                                        [IntrNoMem]>;
def int_aarch64_sve_rax1    : ClangBuiltin<"__builtin_sve_svrax1_u64">,
                              DefaultAttrsIntrinsic<[llvm_nxv2i64_ty],
                                        [llvm_nxv2i64_ty, llvm_nxv2i64_ty],
                                        [IntrNoMem]>;
def int_aarch64_sve_sm4e    : ClangBuiltin<"__builtin_sve_svsm4e_u32">,
                              DefaultAttrsIntrinsic<[llvm_nxv4i32_ty],
                                        [llvm_nxv4i32_ty, llvm_nxv4i32_ty],
                                        [IntrNoMem]>;
def int_aarch64_sve_sm4ekey : ClangBuiltin<"__builtin_sve_svsm4ekey_u32">,
                              DefaultAttrsIntrinsic<[llvm_nxv4i32_ty],
                                        [llvm_nxv4i32_ty, llvm_nxv4i32_ty],
                                        [IntrNoMem]>;
//
// SVE2 - Extended table lookup/permute
//

def int_aarch64_sve_tbl2 : AdvSIMD_SVE2_TBX_Intrinsic;
def int_aarch64_sve_tbx  : AdvSIMD_SVE2_TBX_Intrinsic;

//
// SVE2 - Optional bit permutation
//

def int_aarch64_sve_bdep_x : AdvSIMD_2VectorArg_Intrinsic;
def int_aarch64_sve_bext_x : AdvSIMD_2VectorArg_Intrinsic;
def int_aarch64_sve_bgrp_x : AdvSIMD_2VectorArg_Intrinsic;


//
// SVE ACLE: 7.3. INT8 matrix multiply extensions
//
def int_aarch64_sve_ummla : SVE_MatMul_Intrinsic;
def int_aarch64_sve_smmla : SVE_MatMul_Intrinsic;
def int_aarch64_sve_usmmla : SVE_MatMul_Intrinsic;

def int_aarch64_sve_usdot : AdvSIMD_SVE_DOT_Intrinsic;
def int_aarch64_sve_usdot_lane : AdvSIMD_SVE_DOT_Indexed_Intrinsic;
def int_aarch64_sve_sudot_lane : AdvSIMD_SVE_DOT_Indexed_Intrinsic;

//
// SVE ACLE: 7.4/5. FP64/FP32 matrix multiply extensions
//
def int_aarch64_sve_fmmla : AdvSIMD_3VectorArg_Intrinsic;

//
// SVE ACLE: 7.2. BFloat16 extensions
//

def int_aarch64_sve_bfdot   : SVE_4Vec_BF16;
def int_aarch64_sve_bfmlalb : SVE_4Vec_BF16;
def int_aarch64_sve_bfmlalt : SVE_4Vec_BF16;

def int_aarch64_sve_bfmmla  : SVE_4Vec_BF16;

def int_aarch64_sve_bfdot_lane_v2   : SVE_4Vec_BF16_Indexed;
def int_aarch64_sve_bfmlalb_lane_v2 : SVE_4Vec_BF16_Indexed;
def int_aarch64_sve_bfmlalt_lane_v2 : SVE_4Vec_BF16_Indexed;

//
// SVE2.1 - Contiguous loads to multiple consecutive vectors
//

  class SVE2p1_Load_PN_X2_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>],
                [llvm_aarch64_svcount_ty, llvm_ptr_ty],
                [IntrReadMem, IntrArgMemOnly]>;

  class SVE2p1_Load_PN_X4_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>,
                             LLVMMatchType<0>, LLVMMatchType<0>],
                [llvm_aarch64_svcount_ty, llvm_ptr_ty],
                [IntrReadMem, IntrArgMemOnly]>;

def int_aarch64_sve_ld1_pn_x2 : SVE2p1_Load_PN_X2_Intrinsic;
def int_aarch64_sve_ld1_pn_x4 : SVE2p1_Load_PN_X4_Intrinsic;
def int_aarch64_sve_ldnt1_pn_x2 : SVE2p1_Load_PN_X2_Intrinsic;
def int_aarch64_sve_ldnt1_pn_x4 : SVE2p1_Load_PN_X4_Intrinsic;

//
// SVE2.1 - Contiguous loads to quadword (single vector)
//

class SVE2p1_Single_Load_Quadword
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                            [llvm_nxv1i1_ty, llvm_ptr_ty],
                            [IntrReadMem, IntrArgMemOnly]>;
def int_aarch64_sve_ld1uwq : SVE2p1_Single_Load_Quadword;
def int_aarch64_sve_ld1udq : SVE2p1_Single_Load_Quadword;

//
// SVE2.1 - Contiguous store from quadword (single vector)
//

class SVE2p1_Single_Store_Quadword
    : DefaultAttrsIntrinsic<[],
                            [llvm_anyvector_ty, llvm_nxv1i1_ty, llvm_ptr_ty],
                            [IntrWriteMem, IntrArgMemOnly]>;
def int_aarch64_sve_st1wq : SVE2p1_Single_Store_Quadword;
def int_aarch64_sve_st1dq : SVE2p1_Single_Store_Quadword;


def int_aarch64_sve_ld2q_sret : AdvSIMD_2Vec_PredLoad_Intrinsic;
def int_aarch64_sve_ld3q_sret : AdvSIMD_3Vec_PredLoad_Intrinsic;
def int_aarch64_sve_ld4q_sret : AdvSIMD_4Vec_PredLoad_Intrinsic;

def int_aarch64_sve_st2q : AdvSIMD_2Vec_PredStore_Intrinsic;
def int_aarch64_sve_st3q : AdvSIMD_3Vec_PredStore_Intrinsic;
def int_aarch64_sve_st4q : AdvSIMD_4Vec_PredStore_Intrinsic;

//
// SVE2.1 - Contiguous stores to multiple consecutive vectors
//

  class SVE2p1_Store_PN_X2_Intrinsic
    : DefaultAttrsIntrinsic<[], [ llvm_anyvector_ty, LLVMMatchType<0>,
                                  llvm_aarch64_svcount_ty, llvm_ptr_ty ],
                [IntrWriteMem, IntrArgMemOnly]>;

  class SVE2p1_Store_PN_X4_Intrinsic
    : DefaultAttrsIntrinsic<[], [ llvm_anyvector_ty, LLVMMatchType<0>,
                                  LLVMMatchType<0>, LLVMMatchType<0>,
                                  llvm_aarch64_svcount_ty, llvm_ptr_ty],
                [IntrWriteMem, IntrArgMemOnly]>;

def int_aarch64_sve_st1_pn_x2 : SVE2p1_Store_PN_X2_Intrinsic;
def int_aarch64_sve_st1_pn_x4 : SVE2p1_Store_PN_X4_Intrinsic;
def int_aarch64_sve_stnt1_pn_x2 : SVE2p1_Store_PN_X2_Intrinsic;
def int_aarch64_sve_stnt1_pn_x4 : SVE2p1_Store_PN_X4_Intrinsic;
}

//
// SVE2 - Contiguous conflict detection
//

def int_aarch64_sve_whilerw_b : SVE2_CONFLICT_DETECT_Intrinsic;
def int_aarch64_sve_whilerw_h : SVE2_CONFLICT_DETECT_Intrinsic;
def int_aarch64_sve_whilerw_s : SVE2_CONFLICT_DETECT_Intrinsic;
def int_aarch64_sve_whilerw_d : SVE2_CONFLICT_DETECT_Intrinsic;
def int_aarch64_sve_whilewr_b : SVE2_CONFLICT_DETECT_Intrinsic;
def int_aarch64_sve_whilewr_h : SVE2_CONFLICT_DETECT_Intrinsic;
def int_aarch64_sve_whilewr_s : SVE2_CONFLICT_DETECT_Intrinsic;
def int_aarch64_sve_whilewr_d : SVE2_CONFLICT_DETECT_Intrinsic;

// Scalable Matrix Extension (SME) Intrinsics
let TargetPrefix = "aarch64" in {
  class SME_Load_Store_Intrinsic<LLVMType pred_ty>
    : DefaultAttrsIntrinsic<[],
        [pred_ty, llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty], [ImmArg<ArgIndex<2>>]>;

  // Loads
  def int_aarch64_sme_ld1b_horiz : SME_Load_Store_Intrinsic<llvm_nxv16i1_ty>;
  def int_aarch64_sme_ld1h_horiz : SME_Load_Store_Intrinsic<llvm_nxv8i1_ty>;
  def int_aarch64_sme_ld1w_horiz : SME_Load_Store_Intrinsic<llvm_nxv4i1_ty>;
  def int_aarch64_sme_ld1d_horiz : SME_Load_Store_Intrinsic<llvm_nxv2i1_ty>;
  def int_aarch64_sme_ld1q_horiz : SME_Load_Store_Intrinsic<llvm_nxv1i1_ty>;
  def int_aarch64_sme_ld1b_vert  : SME_Load_Store_Intrinsic<llvm_nxv16i1_ty>;
  def int_aarch64_sme_ld1h_vert  : SME_Load_Store_Intrinsic<llvm_nxv8i1_ty>;
  def int_aarch64_sme_ld1w_vert  : SME_Load_Store_Intrinsic<llvm_nxv4i1_ty>;
  def int_aarch64_sme_ld1d_vert  : SME_Load_Store_Intrinsic<llvm_nxv2i1_ty>;
  def int_aarch64_sme_ld1q_vert  : SME_Load_Store_Intrinsic<llvm_nxv1i1_ty>;

  // Stores
  def int_aarch64_sme_st1b_horiz : SME_Load_Store_Intrinsic<llvm_nxv16i1_ty>;
  def int_aarch64_sme_st1h_horiz : SME_Load_Store_Intrinsic<llvm_nxv8i1_ty>;
  def int_aarch64_sme_st1w_horiz : SME_Load_Store_Intrinsic<llvm_nxv4i1_ty>;
  def int_aarch64_sme_st1d_horiz : SME_Load_Store_Intrinsic<llvm_nxv2i1_ty>;
  def int_aarch64_sme_st1q_horiz : SME_Load_Store_Intrinsic<llvm_nxv1i1_ty>;
  def int_aarch64_sme_st1b_vert  : SME_Load_Store_Intrinsic<llvm_nxv16i1_ty>;
  def int_aarch64_sme_st1h_vert  : SME_Load_Store_Intrinsic<llvm_nxv8i1_ty>;
  def int_aarch64_sme_st1w_vert  : SME_Load_Store_Intrinsic<llvm_nxv4i1_ty>;
  def int_aarch64_sme_st1d_vert  : SME_Load_Store_Intrinsic<llvm_nxv2i1_ty>;
  def int_aarch64_sme_st1q_vert  : SME_Load_Store_Intrinsic<llvm_nxv1i1_ty>;

  // Spill + fill
  class SME_LDR_STR_ZA_Intrinsic
    : DefaultAttrsIntrinsic<[], [llvm_i32_ty, llvm_ptr_ty, llvm_i32_ty]>;
  def int_aarch64_sme_ldr : SME_LDR_STR_ZA_Intrinsic;
  def int_aarch64_sme_str : SME_LDR_STR_ZA_Intrinsic;

  class SME_TileToVector_Intrinsic
      : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
          [LLVMMatchType<0>, LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
           llvm_i32_ty, llvm_i32_ty], [ImmArg<ArgIndex<2>>]>;
  class SME_VectorToTile_Intrinsic
      : DefaultAttrsIntrinsic<[],
          [llvm_i32_ty, llvm_i32_ty, LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
           llvm_anyvector_ty], [ImmArg<ArgIndex<0>>]>;

  def int_aarch64_sme_read_horiz  : SME_TileToVector_Intrinsic;
  def int_aarch64_sme_read_vert   : SME_TileToVector_Intrinsic;
  def int_aarch64_sme_write_horiz : SME_VectorToTile_Intrinsic;
  def int_aarch64_sme_write_vert  : SME_VectorToTile_Intrinsic;

  def int_aarch64_sme_readq_horiz  : SME_TileToVector_Intrinsic;
  def int_aarch64_sme_readq_vert   : SME_TileToVector_Intrinsic;
  def int_aarch64_sme_writeq_horiz : SME_VectorToTile_Intrinsic;
  def int_aarch64_sme_writeq_vert  : SME_VectorToTile_Intrinsic;

  class SME_MOVAZ_TileToVector_X2_Intrinsic
      : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>],
          [llvm_i32_ty, llvm_i32_ty],
          [IntrNoMem, IntrHasSideEffects, ImmArg<ArgIndex<0>>]>;

  class SME_MOVAZ_TileToVector_X4_Intrinsic
      : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>,
           LLVMMatchType<0>,LLVMMatchType<0>],
          [llvm_i32_ty, llvm_i32_ty],
          [IntrNoMem, IntrHasSideEffects, ImmArg<ArgIndex<0>>]>;

  def int_aarch64_sme_readz_horiz_x2 : SME_MOVAZ_TileToVector_X2_Intrinsic;
  def int_aarch64_sme_readz_vert_x2  : SME_MOVAZ_TileToVector_X2_Intrinsic;

  def int_aarch64_sme_readz_horiz_x4 : SME_MOVAZ_TileToVector_X4_Intrinsic;
  def int_aarch64_sme_readz_vert_x4  : SME_MOVAZ_TileToVector_X4_Intrinsic;

  class SME_MOVAZ_TileToVector_Intrinsic
      : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
          [llvm_i32_ty, llvm_i32_ty],
          [IntrNoMem, IntrHasSideEffects, ImmArg<ArgIndex<0>>]>;

  def int_aarch64_sme_readz_horiz : SME_MOVAZ_TileToVector_Intrinsic;
  def int_aarch64_sme_readz_vert  : SME_MOVAZ_TileToVector_Intrinsic;

  def int_aarch64_sme_readz_q_horiz : SME_MOVAZ_TileToVector_Intrinsic;
  def int_aarch64_sme_readz_q_vert  : SME_MOVAZ_TileToVector_Intrinsic;

  def int_aarch64_sme_readz_x2
      : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>],
          [llvm_i32_ty],
          [IntrNoMem, IntrHasSideEffects]>;

  def int_aarch64_sme_readz_x4
      : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>],
          [llvm_i32_ty],
          [IntrNoMem, IntrHasSideEffects]>;

  def int_aarch64_sme_zero : DefaultAttrsIntrinsic<[], [llvm_i32_ty], [ImmArg<ArgIndex<0>>]>;

  class SME_OuterProduct_Intrinsic
      : DefaultAttrsIntrinsic<[],
          [llvm_i32_ty,
           LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
           LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
           LLVMMatchType<0>,
           llvm_anyvector_ty], [ImmArg<ArgIndex<0>>]>;

  def int_aarch64_sme_mopa : SME_OuterProduct_Intrinsic;
  def int_aarch64_sme_mops : SME_OuterProduct_Intrinsic;

  def int_aarch64_sme_mopa_wide : SME_OuterProduct_Intrinsic;
  def int_aarch64_sme_mops_wide : SME_OuterProduct_Intrinsic;

  def int_aarch64_sme_smopa_wide  : SME_OuterProduct_Intrinsic;
  def int_aarch64_sme_smops_wide  : SME_OuterProduct_Intrinsic;
  def int_aarch64_sme_umopa_wide  : SME_OuterProduct_Intrinsic;
  def int_aarch64_sme_umops_wide  : SME_OuterProduct_Intrinsic;
  def int_aarch64_sme_sumopa_wide : SME_OuterProduct_Intrinsic;
  def int_aarch64_sme_sumops_wide : SME_OuterProduct_Intrinsic;
  def int_aarch64_sme_usmopa_wide : SME_OuterProduct_Intrinsic;
  def int_aarch64_sme_usmops_wide : SME_OuterProduct_Intrinsic;

  class SME_AddVectorToTile_Intrinsic
      : DefaultAttrsIntrinsic<[],
          [llvm_i32_ty,
           LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
           LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>,
           llvm_anyvector_ty], [ImmArg<ArgIndex<0>>]>;

  def int_aarch64_sme_addha : SME_AddVectorToTile_Intrinsic;
  def int_aarch64_sme_addva : SME_AddVectorToTile_Intrinsic;

  //
  // Counting elements
  //

  class AdvSIMD_SME_CNTSB_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_i64_ty], [], [IntrNoMem]>;

  def int_aarch64_sme_cntsb : AdvSIMD_SME_CNTSB_Intrinsic;
  def int_aarch64_sme_cntsh : AdvSIMD_SME_CNTSB_Intrinsic;
  def int_aarch64_sme_cntsw : AdvSIMD_SME_CNTSB_Intrinsic;
  def int_aarch64_sme_cntsd : AdvSIMD_SME_CNTSB_Intrinsic;

  //
  // PSTATE Functions
  //

  def int_aarch64_sme_get_tpidr2
      : DefaultAttrsIntrinsic<[llvm_i64_ty], [],
                              [IntrNoMem, IntrHasSideEffects]>;
  def int_aarch64_sme_set_tpidr2
      : DefaultAttrsIntrinsic<[], [llvm_i64_ty],
                              [IntrNoMem, IntrHasSideEffects]>;

  def int_aarch64_sme_za_enable
      : DefaultAttrsIntrinsic<[], [], [IntrNoMem, IntrHasSideEffects]>;
  def int_aarch64_sme_za_disable
      : DefaultAttrsIntrinsic<[], [], [IntrNoMem, IntrHasSideEffects]>;

  // Clamp
  //

  def int_aarch64_sve_sclamp : AdvSIMD_3VectorArg_Intrinsic;
  def int_aarch64_sve_uclamp : AdvSIMD_3VectorArg_Intrinsic;
  def int_aarch64_sve_fclamp : AdvSIMD_3VectorArg_Intrinsic;


  //
  // Reversal
  //

  def int_aarch64_sve_revd : AdvSIMD_Merged1VectorArg_Intrinsic;

  //
  // Predicate selection
  //

  def int_aarch64_sve_psel
      : DefaultAttrsIntrinsic<[llvm_nxv16i1_ty],
                              [llvm_nxv16i1_ty,
                               llvm_anyvector_ty, llvm_i32_ty],
                              [IntrNoMem]>;

  //
  // Predicate-pair intrinsics
  //
  foreach cmp = ["ge", "gt", "hi", "hs", "le", "lo", "ls", "lt"] in {
    def int_aarch64_sve_while # cmp # _x2
        : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>],
                                [llvm_i64_ty, llvm_i64_ty], [IntrNoMem]>;
  }

  //
  // Predicate-as-counter intrinsics
  //

  def int_aarch64_sve_pext
      : DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                              [llvm_aarch64_svcount_ty, llvm_i32_ty],
                              [IntrNoMem, ImmArg<ArgIndex<1>>]>;

  def int_aarch64_sve_pext_x2
      : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>],
                              [llvm_aarch64_svcount_ty, llvm_i32_ty],
                              [IntrNoMem, ImmArg<ArgIndex<1>>]>;

  def int_aarch64_sve_ptrue_c8
      : DefaultAttrsIntrinsic<[llvm_aarch64_svcount_ty], [], [IntrNoMem]>;
  def int_aarch64_sve_ptrue_c16
      : DefaultAttrsIntrinsic<[llvm_aarch64_svcount_ty], [], [IntrNoMem]>;
  def int_aarch64_sve_ptrue_c32
      : DefaultAttrsIntrinsic<[llvm_aarch64_svcount_ty], [], [IntrNoMem]>;
  def int_aarch64_sve_ptrue_c64
      : DefaultAttrsIntrinsic<[llvm_aarch64_svcount_ty], [], [IntrNoMem]>;

  def int_aarch64_sve_cntp_c8
      : DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_aarch64_svcount_ty, llvm_i32_ty],
                              [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_aarch64_sve_cntp_c16
      : DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_aarch64_svcount_ty, llvm_i32_ty],
                              [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_aarch64_sve_cntp_c32
      : DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_aarch64_svcount_ty, llvm_i32_ty],
                              [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_aarch64_sve_cntp_c64
      : DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_aarch64_svcount_ty, llvm_i32_ty],
                              [IntrNoMem, ImmArg<ArgIndex<1>>]>;

  // While (predicate-as-counter) intrinsics
  foreach cmp = ["ge", "gt", "hi", "hs", "le", "lo", "ls", "lt"] in {
    foreach ty = ["c8", "c16", "c32", "c64"] in {
      def int_aarch64_sve_while # cmp # _ # ty
          : DefaultAttrsIntrinsic<[llvm_aarch64_svcount_ty],
                                  [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty],
                                  [IntrNoMem, ImmArg<ArgIndex<2>>]>;
    }
  }

  //
  // SME2 Intrinsics
  //

  class SME2_Matrix_ArrayVector_Single_Single_Intrinsic
    : DefaultAttrsIntrinsic<[],
                [llvm_i32_ty,
                 llvm_anyvector_ty, LLVMMatchType<0>],
                []>;

  class SME2_Matrix_ArrayVector_VG2_Multi_Single_Intrinsic
    : DefaultAttrsIntrinsic<[],
                [llvm_i32_ty,
                 llvm_anyvector_ty, LLVMMatchType<0>,
                 LLVMMatchType<0>],
                []>;

  class SME2_Matrix_ArrayVector_VG4_Multi_Single_Intrinsic
    : DefaultAttrsIntrinsic<[],
                [llvm_i32_ty,
                 llvm_anyvector_ty, LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>,
                 LLVMMatchType<0>],
                []>;

  class SME2_Matrix_ArrayVector_VG2_Multi_Multi_Intrinsic
    : DefaultAttrsIntrinsic<[],
                [llvm_i32_ty,
                 llvm_anyvector_ty, LLVMMatchType<0>,
                 LLVMMatchType<0>, LLVMMatchType<0>],
                []>;

  class SME2_Matrix_ArrayVector_VG4_Multi_Multi_Intrinsic
    : DefaultAttrsIntrinsic<[],
                [llvm_i32_ty,
                 llvm_anyvector_ty, LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>,
                 LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>],
                []>;

  class SME2_Matrix_ArrayVector_Single_Index_Intrinsic
    : DefaultAttrsIntrinsic<[],
                [llvm_i32_ty,
                llvm_anyvector_ty,
                LLVMMatchType<0>, llvm_i32_ty],
                [ImmArg<ArgIndex<3>>]>;

  class SME2_Matrix_ArrayVector_VG2_Multi_Index_Intrinsic
    : DefaultAttrsIntrinsic<[],
                [llvm_i32_ty,
                 llvm_anyvector_ty, LLVMMatchType<0>,
                 LLVMMatchType<0>, llvm_i32_ty],
                [ImmArg<ArgIndex<4>>]>;

  class SME2_Matrix_ArrayVector_VG4_Multi_Index_Intrinsic
    : DefaultAttrsIntrinsic<[],
                [llvm_i32_ty,
                 llvm_anyvector_ty, LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>,
                 LLVMMatchType<0>, llvm_i32_ty],
                [ImmArg<ArgIndex<6>>]>;

  class SME2_VG2_Multi_Imm_Intrinsic
    : DefaultAttrsIntrinsic<[LLVMSubdivide2VectorType<0>],
                [llvm_anyvector_ty, LLVMMatchType<0>,
                 llvm_i32_ty],
                [IntrNoMem, ImmArg<ArgIndex<2>>]>;

  class SME2_VG4_Multi_Imm_Intrinsic
    : DefaultAttrsIntrinsic<[LLVMSubdivide4VectorType<0>],
                [llvm_anyvector_ty, LLVMMatchType<0>,
                 LLVMMatchType<0>, LLVMMatchType<0>,
                 llvm_i32_ty],
                [IntrNoMem, ImmArg<ArgIndex<4>>]>;

  class SME2_ZA_Write_VG2_Intrinsic
   : DefaultAttrsIntrinsic<[],
               [llvm_i32_ty,
                llvm_anyvector_ty, LLVMMatchType<0>],
               []>;

  class SME2_ZA_Write_VG4_Intrinsic
   : DefaultAttrsIntrinsic<[],
               [llvm_i32_ty,
                llvm_anyvector_ty, LLVMMatchType<0>,
                LLVMMatchType<0>,  LLVMMatchType<0>],
               []>;

  class SME2_VG2_Multi_Single_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>],
                [LLVMMatchType<0>, LLVMMatchType<0>,
                 LLVMMatchType<0>],
                [IntrNoMem]>;

  class SME2_VG4_Multi_Single_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>,
                             LLVMMatchType<0>,  LLVMMatchType<0>],
                            [LLVMMatchType<0>,  LLVMMatchType<0>,
                             LLVMMatchType<0>,  LLVMMatchType<0>,
                             LLVMMatchType<0>],
                            [IntrNoMem]>;

  class SME2_VG2_Multi_Multi_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>],
                [LLVMMatchType<0>, LLVMMatchType<0>,
                 LLVMMatchType<0>, LLVMMatchType<0>],
                [IntrNoMem]>;

  class SME2_VG4_Multi_Multi_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>,
                             LLVMMatchType<0>,  LLVMMatchType<0>],
                            [LLVMMatchType<0>,  LLVMMatchType<0>,
                             LLVMMatchType<0>,  LLVMMatchType<0>,
                             LLVMMatchType<0>, LLVMMatchType<0>,
                             LLVMMatchType<0>, LLVMMatchType<0>],
                            [IntrNoMem]>;

  class SVE2_VG2_Sel_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>],
                [llvm_aarch64_svcount_ty, LLVMMatchType<0>,
                 LLVMMatchType<0>, LLVMMatchType<0>,
                 LLVMMatchType<0>], [IntrNoMem]>;

  class SVE2_VG4_Sel_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>,
                             LLVMMatchType<0>, LLVMMatchType<0>],
                [llvm_aarch64_svcount_ty, LLVMMatchType<0>,
                 LLVMMatchType<0>, LLVMMatchType<0>,
                 LLVMMatchType<0>, LLVMMatchType<0>,
                 LLVMMatchType<0>, LLVMMatchType<0>,
                 LLVMMatchType<0>], [IntrNoMem]>;

  class SME2_CVT_VG2_SINGLE_Intrinsic
    : DefaultAttrsIntrinsic<[LLVMSubdivide2VectorType<0>],
                            [llvm_anyvector_ty, LLVMMatchType<0>],
                            [IntrNoMem]>;

  class SME2_CVT_VG2_SINGLE_BF16_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_nxv8bf16_ty],
                            [llvm_nxv4f32_ty, llvm_nxv4f32_ty],
                            [IntrNoMem]>;
  
  class SME2_CVT_WIDENING_VG2_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>],
                            [LLVMSubdivide2VectorType<0>], [IntrNoMem]>;
  

  class SME2_CVT_VG4_SINGLE_Intrinsic
    : DefaultAttrsIntrinsic<[LLVMSubdivide4VectorType<0>],
                            [llvm_anyvector_ty, LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>],
                            [IntrNoMem]>;

  class SME2_CVT_X2_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>],
                            [llvm_anyvector_ty, LLVMMatchType<1>],
                            [IntrNoMem]>;

  class SME2_CVT_X4_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>],
                            [llvm_anyvector_ty, LLVMMatchType<1>, LLVMMatchType<1>, LLVMMatchType<1>],
                            [IntrNoMem]>;

  class SME2_BFMLS_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_nxv4f32_ty],
                            [llvm_nxv4f32_ty, llvm_nxv8bf16_ty, llvm_nxv8bf16_ty],
                            [IntrNoMem]>;

  class SME2_BFMLS_Lane_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_nxv4f32_ty],
                            [llvm_nxv4f32_ty, llvm_nxv8bf16_ty, llvm_nxv8bf16_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  class SME2_ZA_ArrayVector_Read_VG2_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>],
                [llvm_i32_ty],
                []>;

  class SME2_ZA_ArrayVector_Read_VG4_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>,
                             LLVMMatchType<0>,  LLVMMatchType<0>],
                [llvm_i32_ty],
                []>;

  class SME2_Matrix_TileVector_Read_VG2_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>],
                [llvm_i32_ty, llvm_i32_ty],
                []>;

  class SME2_Matrix_TileVector_Read_VG4_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>,
                             LLVMMatchType<0>,  LLVMMatchType<0>],
                [llvm_i32_ty, llvm_i32_ty],
                []>;

  class SME2_ZA_ArrayVector_Write_VG2_Intrinsic
   : DefaultAttrsIntrinsic<[],
               [llvm_i32_ty,
                llvm_anyvector_ty, LLVMMatchType<0>],
               []>;

  class SME2_ZA_ArrayVector_Write_VG4_Intrinsic
   : DefaultAttrsIntrinsic<[],
               [llvm_i32_ty,
                llvm_anyvector_ty, LLVMMatchType<0>,
                LLVMMatchType<0>,  LLVMMatchType<0>],
               []>;

  class SME2_Matrix_TileVector_Write_VG2_Intrinsic
   : DefaultAttrsIntrinsic<[],
               [llvm_i32_ty, llvm_i32_ty,
                llvm_anyvector_ty, LLVMMatchType<0>],
               [ImmArg<ArgIndex<0>>]>;

  class SME2_Matrix_TileVector_Write_VG4_Intrinsic
   : DefaultAttrsIntrinsic<[],
               [llvm_i32_ty, llvm_i32_ty,
                llvm_anyvector_ty, LLVMMatchType<0>,
                LLVMMatchType<0>,  LLVMMatchType<0>],
               [ImmArg<ArgIndex<0>>]>;

  class SME2_VG2_Multi_Single_Single_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>],
                [LLVMMatchType<0>, LLVMMatchType<0>,
                 LLVMMatchType<0>, LLVMMatchType<0>],
                [IntrNoMem]>;

  class SME2_VG4_Multi_Single_Single_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>,
                             LLVMMatchType<0>, LLVMMatchType<0>],
                [LLVMMatchType<0>, LLVMMatchType<0>,
                 LLVMMatchType<0>, LLVMMatchType<0>,
                 LLVMMatchType<0>, LLVMMatchType<0>],
                [IntrNoMem]>;

  class SVE2_VG2_ZipUzp_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>],
                [LLVMMatchType<0>, LLVMMatchType<0>], [IntrNoMem]>;

  class SVE2_VG4_ZipUzp_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>,
                             LLVMMatchType<0>, LLVMMatchType<0>],
                [LLVMMatchType<0>, LLVMMatchType<0>,
                 LLVMMatchType<0>, LLVMMatchType<0>], [IntrNoMem]>;

  class SME2_VG2_Unpk_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>],
                [LLVMSubdivide2VectorType<0>], [IntrNoMem]>;

  class SME2_VG4_Unpk_Intrinsic
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>,
                             LLVMMatchType<0>, LLVMMatchType<0>],
                [LLVMSubdivide2VectorType<0>, LLVMSubdivide2VectorType<0>],
                [IntrNoMem]>;

  //
  // Multi-vector fused multiply-add/subtract
  //

  def int_aarch64_sme_fmla_single_vg1x2 : SME2_Matrix_ArrayVector_VG2_Multi_Single_Intrinsic;
  def int_aarch64_sme_fmls_single_vg1x2 : SME2_Matrix_ArrayVector_VG2_Multi_Single_Intrinsic;
  def int_aarch64_sme_fmla_single_vg1x4 : SME2_Matrix_ArrayVector_VG4_Multi_Single_Intrinsic;
  def int_aarch64_sme_fmls_single_vg1x4 : SME2_Matrix_ArrayVector_VG4_Multi_Single_Intrinsic;

  def int_aarch64_sme_fmla_vg1x2 : SME2_Matrix_ArrayVector_VG2_Multi_Multi_Intrinsic;
  def int_aarch64_sme_fmls_vg1x2 : SME2_Matrix_ArrayVector_VG2_Multi_Multi_Intrinsic;
  def int_aarch64_sme_fmla_vg1x4 : SME2_Matrix_ArrayVector_VG4_Multi_Multi_Intrinsic;
  def int_aarch64_sme_fmls_vg1x4 : SME2_Matrix_ArrayVector_VG4_Multi_Multi_Intrinsic;

  def int_aarch64_sme_fmla_lane_vg1x2 : SME2_Matrix_ArrayVector_VG2_Multi_Index_Intrinsic;
  def int_aarch64_sme_fmls_lane_vg1x2 : SME2_Matrix_ArrayVector_VG2_Multi_Index_Intrinsic;
  def int_aarch64_sme_fmla_lane_vg1x4 : SME2_Matrix_ArrayVector_VG4_Multi_Index_Intrinsic;
  def int_aarch64_sme_fmls_lane_vg1x4 : SME2_Matrix_ArrayVector_VG4_Multi_Index_Intrinsic;

  //
  // Outer product and accumulate/subtract intrinsics
  //

  def int_aarch64_sme_smopa_za32 : SME_OuterProduct_Intrinsic;
  def int_aarch64_sme_umopa_za32 : SME_OuterProduct_Intrinsic;
  def int_aarch64_sme_smops_za32 : SME_OuterProduct_Intrinsic;
  def int_aarch64_sme_umops_za32 : SME_OuterProduct_Intrinsic;

  def int_aarch64_sme_bmopa_za32 : SME_OuterProduct_Intrinsic;
  def int_aarch64_sme_bmops_za32 : SME_OuterProduct_Intrinsic;

  //
  // Multi-vector rounding shift left intrinsics
  //

  def int_aarch64_sve_srshl_single_x2 : SME2_VG2_Multi_Single_Intrinsic;
  def int_aarch64_sve_urshl_single_x2 : SME2_VG2_Multi_Single_Intrinsic;
  def int_aarch64_sve_srshl_single_x4 : SME2_VG4_Multi_Single_Intrinsic;
  def int_aarch64_sve_urshl_single_x4 : SME2_VG4_Multi_Single_Intrinsic;

  def int_aarch64_sve_srshl_x2 : SME2_VG2_Multi_Multi_Intrinsic;
  def int_aarch64_sve_urshl_x2 : SME2_VG2_Multi_Multi_Intrinsic;
  def int_aarch64_sve_srshl_x4 : SME2_VG4_Multi_Multi_Intrinsic;
  def int_aarch64_sve_urshl_x4 : SME2_VG4_Multi_Multi_Intrinsic;

  // Multi-vector saturating rounding shift right intrinsics

  def int_aarch64_sve_sqrshr_x2 : SME2_VG2_Multi_Imm_Intrinsic;
  def int_aarch64_sve_uqrshr_x2 : SME2_VG2_Multi_Imm_Intrinsic;
  def int_aarch64_sve_sqrshr_x4 : SME2_VG4_Multi_Imm_Intrinsic;
  def int_aarch64_sve_uqrshr_x4 : SME2_VG4_Multi_Imm_Intrinsic;

  def int_aarch64_sve_sqrshrn_x2 : SME2_VG2_Multi_Imm_Intrinsic;
  def int_aarch64_sve_uqrshrn_x2 : SME2_VG2_Multi_Imm_Intrinsic;
  def int_aarch64_sve_sqrshrn_x4 : SME2_VG4_Multi_Imm_Intrinsic;
  def int_aarch64_sve_uqrshrn_x4 : SME2_VG4_Multi_Imm_Intrinsic;

  def int_aarch64_sve_sqrshru_x2 : SME2_VG2_Multi_Imm_Intrinsic;
  def int_aarch64_sve_sqrshru_x4 : SME2_VG4_Multi_Imm_Intrinsic;

  def int_aarch64_sve_sqrshrun_x2 : SME2_VG2_Multi_Imm_Intrinsic;
  def int_aarch64_sve_sqrshrun_x4 : SME2_VG4_Multi_Imm_Intrinsic;

  //
  // Multi-vector multiply-add/subtract long
  //

  foreach ty = ["f", "s", "u"] in {
    foreach instr = ["mlal", "mlsl"] in {
      def int_aarch64_sme_ # ty # instr # _single_vg2x1  : SME2_Matrix_ArrayVector_Single_Single_Intrinsic;
      def int_aarch64_sme_ # ty # instr # _single_vg2x2  : SME2_Matrix_ArrayVector_VG2_Multi_Single_Intrinsic;
      def int_aarch64_sme_ # ty # instr # _single_vg2x4  : SME2_Matrix_ArrayVector_VG4_Multi_Single_Intrinsic;

      def int_aarch64_sme_ # ty # instr # _vg2x2 : SME2_Matrix_ArrayVector_VG2_Multi_Multi_Intrinsic;
      def int_aarch64_sme_ # ty # instr # _vg2x4 : SME2_Matrix_ArrayVector_VG4_Multi_Multi_Intrinsic;

      def int_aarch64_sme_ # ty # instr # _lane_vg2x1  : SME2_Matrix_ArrayVector_Single_Index_Intrinsic;
      def int_aarch64_sme_ # ty # instr # _lane_vg2x2  : SME2_Matrix_ArrayVector_VG2_Multi_Index_Intrinsic;
      def int_aarch64_sme_ # ty # instr # _lane_vg2x4  : SME2_Matrix_ArrayVector_VG4_Multi_Index_Intrinsic;
    }
  }

  //
  // Multi-vector multiply-add long long
  //

  foreach ty = ["s", "u"] in {
    foreach instr = ["mla", "mls"] in {
      foreach za = ["za32", "za64"] in {
        def int_aarch64_sme_ # ty # instr # _ # za # _single_vg4x1 : SME2_Matrix_ArrayVector_Single_Single_Intrinsic;
        def int_aarch64_sme_ # ty # instr # _ # za # _single_vg4x2 : SME2_Matrix_ArrayVector_VG2_Multi_Single_Intrinsic;
        def int_aarch64_sme_ # ty # instr # _ # za # _single_vg4x4 : SME2_Matrix_ArrayVector_VG4_Multi_Single_Intrinsic;

        def int_aarch64_sme_ # ty # instr # _ # za # _vg4x2 : SME2_Matrix_ArrayVector_VG2_Multi_Multi_Intrinsic;
        def int_aarch64_sme_ # ty # instr # _ # za # _vg4x4 : SME2_Matrix_ArrayVector_VG4_Multi_Multi_Intrinsic;

        def int_aarch64_sme_ # ty # instr # _ # za # _lane_vg4x1 : SME2_Matrix_ArrayVector_Single_Index_Intrinsic;
        def int_aarch64_sme_ # ty # instr # _ # za # _lane_vg4x2 : SME2_Matrix_ArrayVector_VG2_Multi_Index_Intrinsic;
        def int_aarch64_sme_ # ty # instr # _ # za # _lane_vg4x4 : SME2_Matrix_ArrayVector_VG4_Multi_Index_Intrinsic;
      }
    }
  }

  def int_aarch64_sme_sumla_za32_single_vg4x2 : SME2_Matrix_ArrayVector_VG2_Multi_Single_Intrinsic;
  def int_aarch64_sme_sumla_za32_single_vg4x4 : SME2_Matrix_ArrayVector_VG4_Multi_Single_Intrinsic;

  def int_aarch64_sme_sumla_za32_lane_vg4x1 : SME2_Matrix_ArrayVector_Single_Index_Intrinsic;
  def int_aarch64_sme_sumla_za32_lane_vg4x2 : SME2_Matrix_ArrayVector_VG2_Multi_Index_Intrinsic;
  def int_aarch64_sme_sumla_za32_lane_vg4x4 : SME2_Matrix_ArrayVector_VG4_Multi_Index_Intrinsic;

  def int_aarch64_sme_usmla_za32_single_vg4x1 : SME2_Matrix_ArrayVector_Single_Single_Intrinsic;
  def int_aarch64_sme_usmla_za32_single_vg4x2 : SME2_Matrix_ArrayVector_VG2_Multi_Single_Intrinsic;
  def int_aarch64_sme_usmla_za32_single_vg4x4 : SME2_Matrix_ArrayVector_VG4_Multi_Single_Intrinsic;

  def int_aarch64_sme_usmla_za32_vg4x2 : SME2_Matrix_ArrayVector_VG2_Multi_Multi_Intrinsic;
  def int_aarch64_sme_usmla_za32_vg4x4 : SME2_Matrix_ArrayVector_VG4_Multi_Multi_Intrinsic;

  def int_aarch64_sme_usmla_za32_lane_vg4x1 : SME2_Matrix_ArrayVector_Single_Index_Intrinsic;
  def int_aarch64_sme_usmla_za32_lane_vg4x2 : SME2_Matrix_ArrayVector_VG2_Multi_Index_Intrinsic;
  def int_aarch64_sme_usmla_za32_lane_vg4x4 : SME2_Matrix_ArrayVector_VG4_Multi_Index_Intrinsic;

  def int_aarch64_sve_bfmlslb : SME2_BFMLS_Intrinsic;
  def int_aarch64_sve_bfmlslb_lane : SME2_BFMLS_Lane_Intrinsic;

  def int_aarch64_sve_bfmlslt : SME2_BFMLS_Intrinsic;
  def int_aarch64_sve_bfmlslt_lane : SME2_BFMLS_Lane_Intrinsic;

  // Multi-vector zeroing

  foreach vg = ["vg1x2", "vg1x4", "vg2x1", "vg2x2", "vg2x4", "vg4x1", "vg4x2", "vg4x4"] in {
    def int_aarch64_sme_zero_za64_ # vg : DefaultAttrsIntrinsic<[], [llvm_i32_ty],  [IntrNoMem, IntrHasSideEffects]>;
  }
  
  // Multi-vector signed saturating doubling multiply high

  def int_aarch64_sve_sqdmulh_single_vgx2 : SME2_VG2_Multi_Single_Intrinsic;
  def int_aarch64_sve_sqdmulh_single_vgx4 : SME2_VG4_Multi_Single_Intrinsic;

  def int_aarch64_sve_sqdmulh_vgx2 : SME2_VG2_Multi_Multi_Intrinsic;
  def int_aarch64_sve_sqdmulh_vgx4 : SME2_VG4_Multi_Multi_Intrinsic;

  // Multi-vector floating-point round to integral value

  foreach inst = ["a", "m", "n", "p"] in {
    def int_aarch64_sve_frint # inst # _x2 : SVE2_VG2_ZipUzp_Intrinsic;
    def int_aarch64_sve_frint # inst # _x4 : SVE2_VG4_ZipUzp_Intrinsic;
  }

  //
  // Multi-vector min/max
  //

  foreach ty = ["f", "s", "u"] in {
    foreach instr = ["max", "min"] in {
      def int_aarch64_sve_ # ty # instr # _single_x2 : SME2_VG2_Multi_Single_Intrinsic;
      def int_aarch64_sve_ # ty # instr # _single_x4 : SME2_VG4_Multi_Single_Intrinsic;

      def int_aarch64_sve_ # ty # instr # _x2 : SME2_VG2_Multi_Multi_Intrinsic;
      def int_aarch64_sve_ # ty # instr # _x4 : SME2_VG4_Multi_Multi_Intrinsic;
    }
  }

  //
  // Multi-vector floating point min/max number
  //

  foreach instr = ["fmaxnm", "fminnm"] in {
    def int_aarch64_sve_ # instr # _single_x2 : SME2_VG2_Multi_Single_Intrinsic;
    def int_aarch64_sve_ # instr # _single_x4 : SME2_VG4_Multi_Single_Intrinsic;

    def int_aarch64_sve_ # instr # _x2 : SME2_VG2_Multi_Multi_Intrinsic;
    def int_aarch64_sve_ # instr # _x4 : SME2_VG4_Multi_Multi_Intrinsic;
  }

  //
  // Multi-vector vertical dot-products
  //

  def int_aarch64_sme_fvdot_lane_za32_vg1x2 : SME2_Matrix_ArrayVector_VG2_Multi_Index_Intrinsic;

  foreach ty = ["s", "u"] in {
    def int_aarch64_sme_ #ty # vdot_lane_za32_vg1x2 : SME2_Matrix_ArrayVector_VG2_Multi_Index_Intrinsic;
    def int_aarch64_sme_ #ty # vdot_lane_za32_vg1x4 : SME2_Matrix_ArrayVector_VG4_Multi_Index_Intrinsic;
    def int_aarch64_sme_ #ty # vdot_lane_za64_vg1x4 : SME2_Matrix_ArrayVector_VG4_Multi_Index_Intrinsic;
  }

  def int_aarch64_sme_suvdot_lane_za32_vg1x4 : SME2_Matrix_ArrayVector_VG4_Multi_Index_Intrinsic;
  def int_aarch64_sme_usvdot_lane_za32_vg1x4 : SME2_Matrix_ArrayVector_VG4_Multi_Index_Intrinsic;


  //
  //Multi-vector floating-point convert from half-precision to deinterleaved single-precision.
  //
  
  def int_aarch64_sve_fcvtl_widen_x2  : SME2_CVT_WIDENING_VG2_Intrinsic;

  //
  // Multi-vector floating-point CVT from single-precision to interleaved half-precision/BFloat16
  //
  def int_aarch64_sve_fcvtn_x2  : SME2_CVT_VG2_SINGLE_Intrinsic;
  def int_aarch64_sve_bfcvtn_x2 : SME2_CVT_VG2_SINGLE_BF16_Intrinsic;

  //
  // Multi-vector convert to/from floating-point.
  //
  def int_aarch64_sve_fcvt_x2  : SME2_CVT_VG2_SINGLE_Intrinsic;
  def int_aarch64_sve_bfcvt_x2 : SME2_CVT_VG2_SINGLE_BF16_Intrinsic;
  def int_aarch64_sve_fcvtzs_x2 : SME2_CVT_X2_Intrinsic;
  def int_aarch64_sve_fcvtzu_x2 : SME2_CVT_X2_Intrinsic;
  def int_aarch64_sve_scvtf_x2  : SME2_CVT_X2_Intrinsic;
  def int_aarch64_sve_ucvtf_x2  : SME2_CVT_X2_Intrinsic;
  def int_aarch64_sve_fcvtzs_x4 : SME2_CVT_X4_Intrinsic;
  def int_aarch64_sve_fcvtzu_x4 : SME2_CVT_X4_Intrinsic;
  def int_aarch64_sve_scvtf_x4  : SME2_CVT_X4_Intrinsic;
  def int_aarch64_sve_ucvtf_x4  : SME2_CVT_X4_Intrinsic;
  def int_aarch64_sve_fcvt_widen_x2 : SME2_CVT_WIDENING_VG2_Intrinsic;
  //
  // Multi-vector saturating extract narrow
  //
  def int_aarch64_sve_sqcvt_x2  : SME2_CVT_VG2_SINGLE_Intrinsic;
  def int_aarch64_sve_uqcvt_x2  : SME2_CVT_VG2_SINGLE_Intrinsic;
  def int_aarch64_sve_sqcvtu_x2 : SME2_CVT_VG2_SINGLE_Intrinsic;
  def int_aarch64_sve_sqcvt_x4  : SME2_CVT_VG4_SINGLE_Intrinsic;
  def int_aarch64_sve_uqcvt_x4  : SME2_CVT_VG4_SINGLE_Intrinsic;
  def int_aarch64_sve_sqcvtu_x4 : SME2_CVT_VG4_SINGLE_Intrinsic;

  //
  // Multi-vector saturating extract narrow and interleave
  //
  def int_aarch64_sve_sqcvtn_x2  : SME2_CVT_VG2_SINGLE_Intrinsic;
  def int_aarch64_sve_uqcvtn_x2  : SME2_CVT_VG2_SINGLE_Intrinsic;
  def int_aarch64_sve_sqcvtun_x2 : SME2_CVT_VG2_SINGLE_Intrinsic;
  def int_aarch64_sve_sqcvtn_x4  : SME2_CVT_VG4_SINGLE_Intrinsic;
  def int_aarch64_sve_uqcvtn_x4  : SME2_CVT_VG4_SINGLE_Intrinsic;
  def int_aarch64_sve_sqcvtun_x4 : SME2_CVT_VG4_SINGLE_Intrinsic;

  //
  // Multi-Single add/sub
  //
  def int_aarch64_sme_add_write_single_za_vg1x2 : SME2_Matrix_ArrayVector_VG2_Multi_Single_Intrinsic;
  def int_aarch64_sme_sub_write_single_za_vg1x2 : SME2_Matrix_ArrayVector_VG2_Multi_Single_Intrinsic;
  def int_aarch64_sme_add_write_single_za_vg1x4 : SME2_Matrix_ArrayVector_VG4_Multi_Single_Intrinsic;
  def int_aarch64_sme_sub_write_single_za_vg1x4 : SME2_Matrix_ArrayVector_VG4_Multi_Single_Intrinsic;

  //
  // Multi-Multi add/sub
  //
  def int_aarch64_sme_add_write_za_vg1x2 : SME2_Matrix_ArrayVector_VG2_Multi_Multi_Intrinsic;
  def int_aarch64_sme_sub_write_za_vg1x2 : SME2_Matrix_ArrayVector_VG2_Multi_Multi_Intrinsic;
  def int_aarch64_sme_add_write_za_vg1x4 : SME2_Matrix_ArrayVector_VG4_Multi_Multi_Intrinsic;
  def int_aarch64_sme_sub_write_za_vg1x4 : SME2_Matrix_ArrayVector_VG4_Multi_Multi_Intrinsic;

  // Multi-vector clamps
  def int_aarch64_sve_sclamp_single_x2 : SME2_VG2_Multi_Single_Single_Intrinsic;
  def int_aarch64_sve_uclamp_single_x2 : SME2_VG2_Multi_Single_Single_Intrinsic;
  def int_aarch64_sve_fclamp_single_x2 : SME2_VG2_Multi_Single_Single_Intrinsic;
  def int_aarch64_sve_bfclamp_single_x2 : SME2_VG2_Multi_Single_Single_Intrinsic;

  def int_aarch64_sve_sclamp_single_x4 : SME2_VG4_Multi_Single_Single_Intrinsic;
  def int_aarch64_sve_uclamp_single_x4 : SME2_VG4_Multi_Single_Single_Intrinsic;
  def int_aarch64_sve_fclamp_single_x4 : SME2_VG4_Multi_Single_Single_Intrinsic;
  def int_aarch64_sve_bfclamp_single_x4 : SME2_VG4_Multi_Single_Single_Intrinsic;

  //
  // Multi-vector add/sub and accumulate into ZA
  //
  foreach intr = ["add", "sub"] in {
    foreach za = ["za16","za32", "za64"] in {
      def int_aarch64_sme_ # intr # _ # za # _vg1x2 : SME2_ZA_Write_VG2_Intrinsic;
      def int_aarch64_sme_ # intr # _ # za # _vg1x4 : SME2_ZA_Write_VG4_Intrinsic;
    }
  }

  //
  // Move multi-vectors to/from ZA
  //

  def int_aarch64_sme_read_hor_vg2   : SME2_Matrix_TileVector_Read_VG2_Intrinsic;
  def int_aarch64_sme_read_hor_vg4   : SME2_Matrix_TileVector_Read_VG4_Intrinsic;

  def int_aarch64_sme_read_ver_vg2   : SME2_Matrix_TileVector_Read_VG2_Intrinsic;
  def int_aarch64_sme_read_ver_vg4   : SME2_Matrix_TileVector_Read_VG4_Intrinsic;

  def int_aarch64_sme_read_vg1x2 : SME2_ZA_ArrayVector_Read_VG2_Intrinsic;
  def int_aarch64_sme_read_vg1x4 : SME2_ZA_ArrayVector_Read_VG4_Intrinsic;

  def int_aarch64_sme_write_hor_vg2 : SME2_Matrix_TileVector_Write_VG2_Intrinsic;
  def int_aarch64_sme_write_hor_vg4 : SME2_Matrix_TileVector_Write_VG4_Intrinsic;

  def int_aarch64_sme_write_ver_vg2 : SME2_Matrix_TileVector_Write_VG2_Intrinsic;
  def int_aarch64_sme_write_ver_vg4 : SME2_Matrix_TileVector_Write_VG4_Intrinsic;

  def int_aarch64_sme_write_vg1x2 : SME2_ZA_ArrayVector_Write_VG2_Intrinsic;
  def int_aarch64_sme_write_vg1x4 : SME2_ZA_ArrayVector_Write_VG4_Intrinsic;

  //
  // Multi-Single Vector add
  //
  def int_aarch64_sve_add_single_x2 : SME2_VG2_Multi_Single_Intrinsic;
  def int_aarch64_sve_add_single_x4 : SME2_VG4_Multi_Single_Intrinsic;

  // 2-way and 4-way multi-vector signed/unsigned integer dot-product
  foreach ty = ["s", "u"] in {
    foreach sz = ["za32", "za64"] in {
      def int_aarch64_sme_ # ty # dot_single_ # sz # _vg1x2 : SME2_Matrix_ArrayVector_VG2_Multi_Single_Intrinsic;
      def int_aarch64_sme_ # ty # dot_single_ # sz # _vg1x4 : SME2_Matrix_ArrayVector_VG4_Multi_Single_Intrinsic;

      def int_aarch64_sme_ # ty # dot_ # sz # _vg1x2 : SME2_Matrix_ArrayVector_VG2_Multi_Multi_Intrinsic;
      def int_aarch64_sme_ # ty # dot_ # sz # _vg1x4 : SME2_Matrix_ArrayVector_VG4_Multi_Multi_Intrinsic;

      def int_aarch64_sme_ # ty # dot_lane_ # sz # _vg1x2 : SME2_Matrix_ArrayVector_VG2_Multi_Index_Intrinsic;
      def int_aarch64_sme_ # ty # dot_lane_ # sz # _vg1x4 : SME2_Matrix_ArrayVector_VG4_Multi_Index_Intrinsic;
    }
  }

  foreach ty = ["su", "us"] in {
    def int_aarch64_sme_ # ty # dot_single_za32_vg1x2 : SME2_Matrix_ArrayVector_VG2_Multi_Single_Intrinsic;
    def int_aarch64_sme_ # ty # dot_single_za32_vg1x4 : SME2_Matrix_ArrayVector_VG4_Multi_Single_Intrinsic;

    def int_aarch64_sme_ # ty # dot_lane_za32_vg1x2 : SME2_Matrix_ArrayVector_VG2_Multi_Index_Intrinsic;
    def int_aarch64_sme_ # ty # dot_lane_za32_vg1x4 : SME2_Matrix_ArrayVector_VG4_Multi_Index_Intrinsic;
  }

  def int_aarch64_sme_usdot_za32_vg1x2 : SME2_Matrix_ArrayVector_VG2_Multi_Multi_Intrinsic;
  def int_aarch64_sme_usdot_za32_vg1x4 : SME2_Matrix_ArrayVector_VG4_Multi_Multi_Intrinsic;

  // Multi-vector half-precision or bfloat floating-point dot-product
  def int_aarch64_sme_fdot_single_za32_vg1x2 : SME2_Matrix_ArrayVector_VG2_Multi_Single_Intrinsic;
  def int_aarch64_sme_fdot_single_za32_vg1x4 : SME2_Matrix_ArrayVector_VG4_Multi_Single_Intrinsic;

  def int_aarch64_sme_fdot_za32_vg1x2 : SME2_Matrix_ArrayVector_VG2_Multi_Multi_Intrinsic;
  def int_aarch64_sme_fdot_za32_vg1x4 : SME2_Matrix_ArrayVector_VG4_Multi_Multi_Intrinsic;

  def int_aarch64_sme_fdot_lane_za32_vg1x2 : SME2_Matrix_ArrayVector_VG2_Multi_Index_Intrinsic;
  def int_aarch64_sme_fdot_lane_za32_vg1x4 : SME2_Matrix_ArrayVector_VG4_Multi_Index_Intrinsic;

  // Multi-vector zip and unzips
  def int_aarch64_sve_zip_x2  : SVE2_VG2_ZipUzp_Intrinsic;
  def int_aarch64_sve_zipq_x2 : SVE2_VG2_ZipUzp_Intrinsic;
  def int_aarch64_sve_zip_x4  : SVE2_VG4_ZipUzp_Intrinsic;
  def int_aarch64_sve_zipq_x4 : SVE2_VG4_ZipUzp_Intrinsic;
  def int_aarch64_sve_uzp_x2  : SVE2_VG2_ZipUzp_Intrinsic;
  def int_aarch64_sve_uzpq_x2 : SVE2_VG2_ZipUzp_Intrinsic;
  def int_aarch64_sve_uzp_x4  : SVE2_VG4_ZipUzp_Intrinsic;
  def int_aarch64_sve_uzpq_x4 : SVE2_VG4_ZipUzp_Intrinsic;

  // Vector dot-products (2-way)
  def int_aarch64_sve_sdot_x2 : SVE2_3VectorArg_Long_Intrinsic;
  def int_aarch64_sve_udot_x2 : SVE2_3VectorArg_Long_Intrinsic;
  def int_aarch64_sve_fdot_x2 : SVE2_3VectorArg_Long_Intrinsic;
  def int_aarch64_sve_sdot_lane_x2 : SVE2_3VectorArgIndexed_Long_Intrinsic;
  def int_aarch64_sve_udot_lane_x2 : SVE2_3VectorArgIndexed_Long_Intrinsic;
  def int_aarch64_sve_fdot_lane_x2 : SVE2_3VectorArgIndexed_Long_Intrinsic;

  //
  // Signed/unsigned multi-vector unpacks
  //
  def int_aarch64_sve_sunpk_x2 : SME2_VG2_Unpk_Intrinsic;
  def int_aarch64_sve_uunpk_x2 : SME2_VG2_Unpk_Intrinsic;
  def int_aarch64_sve_sunpk_x4 : SME2_VG4_Unpk_Intrinsic;
  def int_aarch64_sve_uunpk_x4 : SME2_VG4_Unpk_Intrinsic;

  // 2-way and 4-way vector selects
  def int_aarch64_sve_sel_x2  : SVE2_VG2_Sel_Intrinsic;
  def int_aarch64_sve_sel_x4  : SVE2_VG4_Sel_Intrinsic;

  class SME_LDR_STR_ZT_Intrinsic
    : DefaultAttrsIntrinsic<[], [llvm_i32_ty, llvm_ptr_ty]>;
  def int_aarch64_sme_ldr_zt : SME_LDR_STR_ZT_Intrinsic;
  def int_aarch64_sme_str_zt : SME_LDR_STR_ZT_Intrinsic;

  //
  //  Zero ZT0
  //
  def int_aarch64_sme_zero_zt : DefaultAttrsIntrinsic<[], [llvm_i32_ty], [ImmArg<ArgIndex<0>>, IntrWriteMem]>;

  //
  // Lookup table expand one register
  //
  def int_aarch64_sme_luti2_lane_zt
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty], [llvm_i32_ty, llvm_nxv16i8_ty, llvm_i32_ty],
                            [ImmArg<ArgIndex<0>>, ImmArg<ArgIndex<2>>, IntrReadMem]>;
  def int_aarch64_sme_luti4_lane_zt
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty], [llvm_i32_ty, llvm_nxv16i8_ty, llvm_i32_ty],
                            [ImmArg<ArgIndex<0>>, ImmArg<ArgIndex<2>>, IntrReadMem]>;
                            
  // Lookup table expand two registers
  //
  def int_aarch64_sme_luti2_lane_zt_x2
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>], [llvm_i32_ty, llvm_nxv16i8_ty, llvm_i32_ty],
                            [ImmArg<ArgIndex<0>>, ImmArg<ArgIndex<2>>, IntrReadMem]>;
  def int_aarch64_sme_luti4_lane_zt_x2
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>], [llvm_i32_ty, llvm_nxv16i8_ty, llvm_i32_ty],
                            [ImmArg<ArgIndex<0>>, ImmArg<ArgIndex<2>>, IntrReadMem]>;
                            
  //
  // Lookup table expand four registers
  //
  def int_aarch64_sme_luti2_lane_zt_x4
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>],
                            [llvm_i32_ty, llvm_nxv16i8_ty, llvm_i32_ty],
                            [ImmArg<ArgIndex<0>>, ImmArg<ArgIndex<2>>, IntrReadMem]>;
  def int_aarch64_sme_luti4_lane_zt_x4
    : DefaultAttrsIntrinsic<[llvm_anyvector_ty, LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>],
                            [llvm_i32_ty, llvm_nxv16i8_ty, llvm_i32_ty],
                            [ImmArg<ArgIndex<0>>, ImmArg<ArgIndex<2>>, IntrReadMem]>;
}

// SVE2.1 - ZIPQ1, ZIPQ2, UZPQ1, UZPQ2
//
def int_aarch64_sve_zipq1     : AdvSIMD_2VectorArg_Intrinsic;
def int_aarch64_sve_zipq2     : AdvSIMD_2VectorArg_Intrinsic;
def int_aarch64_sve_uzpq1     : AdvSIMD_2VectorArg_Intrinsic;
def int_aarch64_sve_uzpq2     : AdvSIMD_2VectorArg_Intrinsic;

// SVE2.1 - Programmable table lookup within each quadword vector segment
// (zeroing)/(merging)
//
def int_aarch64_sve_tblq : AdvSIMD_SVE_TBL_Intrinsic;
def int_aarch64_sve_tbxq : AdvSIMD_SVE2_TBX_Intrinsic;

// SVE2.1 - Extract vector segment from each pair of quadword segments.
//
def int_aarch64_sve_extq : AdvSIMD_2VectorArgIndexed_Intrinsic;

//
// SVE2.1 - Move predicate to/from vector
//
def int_aarch64_sve_pmov_to_pred_lane : SVE2_1VectorArgIndexed_Pred_Intrinsic; 

def int_aarch64_sve_pmov_to_pred_lane_zero : SVE2_1VectorArg_Pred_Intrinsic;

def int_aarch64_sve_pmov_to_vector_lane_merging : SVE2_Pred_1VectorArgIndexed_Intrinsic;
def int_aarch64_sve_pmov_to_vector_lane_zeroing : SVE2_Pred_1VectorArg_Intrinsic;

def int_aarch64_sme_mopa_nonwide : SME_OuterProduct_Intrinsic;
def int_aarch64_sme_mops_nonwide : SME_OuterProduct_Intrinsic;

