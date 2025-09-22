//===- IntrinsicsX86.td - Defines X86 intrinsics -----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines all of the X86-specific intrinsics.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Interrupt traps
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_int : Intrinsic<[], [llvm_i8_ty], [ImmArg<ArgIndex<0>>]>;
}

//===----------------------------------------------------------------------===//
// SEH intrinsics for Windows
let TargetPrefix = "x86" in {
  def int_x86_seh_lsda : Intrinsic<[llvm_ptr_ty], [llvm_ptr_ty], [IntrNoMem]>;

  // Marks the EH registration node created in LLVM IR prior to code generation.
  def int_x86_seh_ehregnode : Intrinsic<[], [llvm_ptr_ty], []>;

  // Marks the EH guard slot node created in LLVM IR prior to code generation.
  def int_x86_seh_ehguard : Intrinsic<[], [llvm_ptr_ty], []>;
}

//===----------------------------------------------------------------------===//
// FLAGS.
let TargetPrefix = "x86" in {
  def int_x86_flags_read_u32 : ClangBuiltin<"__builtin_ia32_readeflags_u32">,
        Intrinsic<[llvm_i32_ty], [], []>;
  def int_x86_flags_read_u64 : ClangBuiltin<"__builtin_ia32_readeflags_u64">,
        Intrinsic<[llvm_i64_ty], [], []>;
  def int_x86_flags_write_u32 : ClangBuiltin<"__builtin_ia32_writeeflags_u32">,
        Intrinsic<[], [llvm_i32_ty], []>;
  def int_x86_flags_write_u64 : ClangBuiltin<"__builtin_ia32_writeeflags_u64">,
        Intrinsic<[], [llvm_i64_ty], []>;
}

//===----------------------------------------------------------------------===//
// Read Time Stamp Counter.
let TargetPrefix = "x86" in {
  def int_x86_rdtsc : ClangBuiltin<"__builtin_ia32_rdtsc">,
              Intrinsic<[llvm_i64_ty], [], []>;
  def int_x86_rdtscp :
              Intrinsic<[llvm_i64_ty, llvm_i32_ty], [], []>;
}

// Read Performance-Monitoring Counter.
let TargetPrefix = "x86" in {
  def int_x86_rdpmc : ClangBuiltin<"__builtin_ia32_rdpmc">,
              Intrinsic<[llvm_i64_ty], [llvm_i32_ty], []>;
}

// Read processor ID.
let TargetPrefix = "x86" in {
  def int_x86_rdpid : ClangBuiltin<"__builtin_ia32_rdpid">,
              Intrinsic<[llvm_i32_ty], [], []>;
}

// Lock bit test.
let TargetPrefix = "x86" in {
  def int_x86_atomic_bts : Intrinsic<[llvm_anyint_ty], [llvm_ptr_ty, llvm_i8_ty],
                                     [ImmArg<ArgIndex<1>>]>;
  def int_x86_atomic_btc : Intrinsic<[llvm_anyint_ty], [llvm_ptr_ty, llvm_i8_ty],
                                     [ImmArg<ArgIndex<1>>]>;
  def int_x86_atomic_btr : Intrinsic<[llvm_anyint_ty], [llvm_ptr_ty, llvm_i8_ty],
                                     [ImmArg<ArgIndex<1>>]>;
  def int_x86_atomic_bts_rm  : Intrinsic<[llvm_i8_ty], [llvm_ptr_ty, llvm_anyint_ty],
                                         []>;
  def int_x86_atomic_btc_rm  : Intrinsic<[llvm_i8_ty], [llvm_ptr_ty, llvm_anyint_ty],
                                         []>;
  def int_x86_atomic_btr_rm  : Intrinsic<[llvm_i8_ty], [llvm_ptr_ty, llvm_anyint_ty],
                                         []>;


}

// Lock binary arith with CC.
let TargetPrefix = "x86" in {
  def int_x86_atomic_add_cc  : Intrinsic<[llvm_i8_ty], [llvm_ptr_ty, llvm_anyint_ty, llvm_i32_ty],
                                         [ImmArg<ArgIndex<2>>]>;
  def int_x86_atomic_sub_cc  : Intrinsic<[llvm_i8_ty], [llvm_ptr_ty, llvm_anyint_ty, llvm_i32_ty],
                                         [ImmArg<ArgIndex<2>>]>;
  def int_x86_atomic_or_cc  : Intrinsic<[llvm_i8_ty], [llvm_ptr_ty, llvm_anyint_ty, llvm_i32_ty],
                                         [ImmArg<ArgIndex<2>>]>;
  def int_x86_atomic_and_cc  : Intrinsic<[llvm_i8_ty], [llvm_ptr_ty, llvm_anyint_ty, llvm_i32_ty],
                                         [ImmArg<ArgIndex<2>>]>;
  def int_x86_atomic_xor_cc  : Intrinsic<[llvm_i8_ty], [llvm_ptr_ty, llvm_anyint_ty, llvm_i32_ty],
                                         [ImmArg<ArgIndex<2>>]>;
}

// Read Processor Register.
let TargetPrefix = "x86" in {
  def int_x86_rdpru : ClangBuiltin<"__builtin_ia32_rdpru">,
              Intrinsic<[llvm_i64_ty], [llvm_i32_ty], []>;
}

//===----------------------------------------------------------------------===//
// CET SS
let TargetPrefix = "x86" in {
  def int_x86_incsspd : ClangBuiltin<"__builtin_ia32_incsspd">,
              Intrinsic<[], [llvm_i32_ty], []>;
  def int_x86_incsspq : ClangBuiltin<"__builtin_ia32_incsspq">,
              Intrinsic<[], [llvm_i64_ty], []>;
  def int_x86_rdsspd : ClangBuiltin<"__builtin_ia32_rdsspd">,
              Intrinsic<[llvm_i32_ty], [llvm_i32_ty], []>;
  def int_x86_rdsspq : ClangBuiltin<"__builtin_ia32_rdsspq">,
              Intrinsic<[llvm_i64_ty], [llvm_i64_ty], []>;
  def int_x86_saveprevssp : ClangBuiltin<"__builtin_ia32_saveprevssp">,
              Intrinsic<[], [], []>;
  def int_x86_rstorssp : ClangBuiltin<"__builtin_ia32_rstorssp">,
              Intrinsic<[], [llvm_ptr_ty], []>;
  def int_x86_wrssd : ClangBuiltin<"__builtin_ia32_wrssd">,
              Intrinsic<[], [llvm_i32_ty, llvm_ptr_ty], []>;
  def int_x86_wrssq : ClangBuiltin<"__builtin_ia32_wrssq">,
              Intrinsic<[], [llvm_i64_ty, llvm_ptr_ty], []>;
  def int_x86_wrussd : ClangBuiltin<"__builtin_ia32_wrussd">,
              Intrinsic<[], [llvm_i32_ty, llvm_ptr_ty], []>;
  def int_x86_wrussq : ClangBuiltin<"__builtin_ia32_wrussq">,
              Intrinsic<[], [llvm_i64_ty, llvm_ptr_ty], []>;
  def int_x86_setssbsy : ClangBuiltin<"__builtin_ia32_setssbsy">,
              Intrinsic<[], [], []>;
  def int_x86_clrssbsy : ClangBuiltin<"__builtin_ia32_clrssbsy">,
              Intrinsic<[], [llvm_ptr_ty], []>;
}

//===----------------------------------------------------------------------===//
// SSE1

// Arithmetic ops
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_sse_rcp_ss : ClangBuiltin<"__builtin_ia32_rcpss">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty],
                            [IntrNoMem]>;
  def int_x86_sse_rcp_ps : ClangBuiltin<"__builtin_ia32_rcpps">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty],
                            [IntrNoMem]>;
  def int_x86_sse_rsqrt_ss : ClangBuiltin<"__builtin_ia32_rsqrtss">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty],
                            [IntrNoMem]>;
  def int_x86_sse_rsqrt_ps : ClangBuiltin<"__builtin_ia32_rsqrtps">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty],
                            [IntrNoMem]>;
  def int_x86_sse_min_ss : ClangBuiltin<"__builtin_ia32_minss">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty,
                             llvm_v4f32_ty], [IntrNoMem]>;
  def int_x86_sse_min_ps : ClangBuiltin<"__builtin_ia32_minps">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty,
                             llvm_v4f32_ty], [IntrNoMem]>;
  def int_x86_sse_max_ss : ClangBuiltin<"__builtin_ia32_maxss">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty,
                             llvm_v4f32_ty], [IntrNoMem]>;
  def int_x86_sse_max_ps : ClangBuiltin<"__builtin_ia32_maxps">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty,
                             llvm_v4f32_ty], [IntrNoMem]>;
}

// Comparison ops
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_sse_cmp_ss : ClangBuiltin<"__builtin_ia32_cmpss">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty,
                             llvm_v4f32_ty, llvm_i8_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  // NOTE: This comparison intrinsic is not used by clang as long as the
  //       distinction in signaling behaviour is not implemented.
  def int_x86_sse_cmp_ps :
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty,
                             llvm_v4f32_ty, llvm_i8_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_sse_comieq_ss : ClangBuiltin<"__builtin_ia32_comieq">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v4f32_ty,
                             llvm_v4f32_ty], [IntrNoMem]>;
  def int_x86_sse_comilt_ss : ClangBuiltin<"__builtin_ia32_comilt">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v4f32_ty,
                             llvm_v4f32_ty], [IntrNoMem]>;
  def int_x86_sse_comile_ss : ClangBuiltin<"__builtin_ia32_comile">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v4f32_ty,
                             llvm_v4f32_ty], [IntrNoMem]>;
  def int_x86_sse_comigt_ss : ClangBuiltin<"__builtin_ia32_comigt">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v4f32_ty,
                             llvm_v4f32_ty], [IntrNoMem]>;
  def int_x86_sse_comige_ss : ClangBuiltin<"__builtin_ia32_comige">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v4f32_ty,
                             llvm_v4f32_ty], [IntrNoMem]>;
  def int_x86_sse_comineq_ss : ClangBuiltin<"__builtin_ia32_comineq">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v4f32_ty,
                             llvm_v4f32_ty], [IntrNoMem]>;
  def int_x86_sse_ucomieq_ss : ClangBuiltin<"__builtin_ia32_ucomieq">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v4f32_ty,
                             llvm_v4f32_ty], [IntrNoMem]>;
  def int_x86_sse_ucomilt_ss : ClangBuiltin<"__builtin_ia32_ucomilt">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v4f32_ty,
                             llvm_v4f32_ty], [IntrNoMem]>;
  def int_x86_sse_ucomile_ss : ClangBuiltin<"__builtin_ia32_ucomile">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v4f32_ty,
                             llvm_v4f32_ty], [IntrNoMem]>;
  def int_x86_sse_ucomigt_ss : ClangBuiltin<"__builtin_ia32_ucomigt">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v4f32_ty,
                             llvm_v4f32_ty], [IntrNoMem]>;
  def int_x86_sse_ucomige_ss : ClangBuiltin<"__builtin_ia32_ucomige">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v4f32_ty,
                             llvm_v4f32_ty], [IntrNoMem]>;
  def int_x86_sse_ucomineq_ss : ClangBuiltin<"__builtin_ia32_ucomineq">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v4f32_ty,
                             llvm_v4f32_ty], [IntrNoMem]>;
}


// Conversion ops
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_sse_cvtss2si : ClangBuiltin<"__builtin_ia32_cvtss2si">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v4f32_ty], [IntrNoMem]>;
  def int_x86_sse_cvtss2si64 : ClangBuiltin<"__builtin_ia32_cvtss2si64">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_v4f32_ty], [IntrNoMem]>;
  def int_x86_sse_cvttss2si : ClangBuiltin<"__builtin_ia32_cvttss2si">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v4f32_ty], [IntrNoMem]>;
  def int_x86_sse_cvttss2si64 : ClangBuiltin<"__builtin_ia32_cvttss2si64">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_v4f32_ty], [IntrNoMem]>;

  def int_x86_sse_cvtps2pi : ClangBuiltin<"__builtin_ia32_cvtps2pi">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_v4f32_ty], [IntrNoMem]>;
  def int_x86_sse_cvttps2pi: ClangBuiltin<"__builtin_ia32_cvttps2pi">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_v4f32_ty], [IntrNoMem]>;
  def int_x86_sse_cvtpi2ps : ClangBuiltin<"__builtin_ia32_cvtpi2ps">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty,
                             llvm_x86mmx_ty], [IntrNoMem]>;
}

// Cacheability support ops
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_sse_sfence : ClangBuiltin<"__builtin_ia32_sfence">,
              Intrinsic<[], [], []>;
}

// Control register.
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_sse_stmxcsr :
              Intrinsic<[], [llvm_ptr_ty],
                         [IntrWriteMem, IntrArgMemOnly,
                         // This prevents reordering with ldmxcsr
                         IntrHasSideEffects]>;
  def int_x86_sse_ldmxcsr :
              Intrinsic<[], [llvm_ptr_ty],
                         // FIXME: LDMXCSR does not actually write to memory,
                         // but intrinsic properties are generated incorrectly
                         // for IntrReadMem+IntrHasSideEffects.
                        [/*IntrReadMem, IntrArgMemOnly,*/ IntrHasSideEffects]>;
}

// Misc.
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_sse_movmsk_ps : ClangBuiltin<"__builtin_ia32_movmskps">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v4f32_ty], [IntrNoMem]>;
}

//===----------------------------------------------------------------------===//
// SSE2

// FP arithmetic ops
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_sse2_min_sd : ClangBuiltin<"__builtin_ia32_minsd">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty,
                             llvm_v2f64_ty], [IntrNoMem]>;
  def int_x86_sse2_min_pd : ClangBuiltin<"__builtin_ia32_minpd">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty,
                             llvm_v2f64_ty], [IntrNoMem]>;
  def int_x86_sse2_max_sd : ClangBuiltin<"__builtin_ia32_maxsd">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty,
                             llvm_v2f64_ty], [IntrNoMem]>;
  def int_x86_sse2_max_pd : ClangBuiltin<"__builtin_ia32_maxpd">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty,
                             llvm_v2f64_ty], [IntrNoMem]>;
}

// FP comparison ops
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_sse2_cmp_sd : ClangBuiltin<"__builtin_ia32_cmpsd">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty,
                             llvm_v2f64_ty, llvm_i8_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  // NOTE: This comparison intrinsic is not used by clang as long as the
  //       distinction in signaling behaviour is not implemented.
  def int_x86_sse2_cmp_pd :
      DefaultAttrsIntrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty,
                             llvm_v2f64_ty, llvm_i8_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_sse2_comieq_sd : ClangBuiltin<"__builtin_ia32_comisdeq">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v2f64_ty,
                             llvm_v2f64_ty], [IntrNoMem]>;
  def int_x86_sse2_comilt_sd : ClangBuiltin<"__builtin_ia32_comisdlt">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v2f64_ty,
                             llvm_v2f64_ty], [IntrNoMem]>;
  def int_x86_sse2_comile_sd : ClangBuiltin<"__builtin_ia32_comisdle">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v2f64_ty,
                             llvm_v2f64_ty], [IntrNoMem]>;
  def int_x86_sse2_comigt_sd : ClangBuiltin<"__builtin_ia32_comisdgt">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v2f64_ty,
                             llvm_v2f64_ty], [IntrNoMem]>;
  def int_x86_sse2_comige_sd : ClangBuiltin<"__builtin_ia32_comisdge">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v2f64_ty,
                             llvm_v2f64_ty], [IntrNoMem]>;
  def int_x86_sse2_comineq_sd : ClangBuiltin<"__builtin_ia32_comisdneq">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v2f64_ty,
                             llvm_v2f64_ty], [IntrNoMem]>;
  def int_x86_sse2_ucomieq_sd : ClangBuiltin<"__builtin_ia32_ucomisdeq">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v2f64_ty,
                             llvm_v2f64_ty], [IntrNoMem]>;
  def int_x86_sse2_ucomilt_sd : ClangBuiltin<"__builtin_ia32_ucomisdlt">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v2f64_ty,
                             llvm_v2f64_ty], [IntrNoMem]>;
  def int_x86_sse2_ucomile_sd : ClangBuiltin<"__builtin_ia32_ucomisdle">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v2f64_ty,
                             llvm_v2f64_ty], [IntrNoMem]>;
  def int_x86_sse2_ucomigt_sd : ClangBuiltin<"__builtin_ia32_ucomisdgt">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v2f64_ty,
                             llvm_v2f64_ty], [IntrNoMem]>;
  def int_x86_sse2_ucomige_sd : ClangBuiltin<"__builtin_ia32_ucomisdge">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v2f64_ty,
                             llvm_v2f64_ty], [IntrNoMem]>;
  def int_x86_sse2_ucomineq_sd : ClangBuiltin<"__builtin_ia32_ucomisdneq">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v2f64_ty,
                             llvm_v2f64_ty], [IntrNoMem]>;
}

// Integer arithmetic ops.
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_sse2_pmulhu_w : ClangBuiltin<"__builtin_ia32_pmulhuw128">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty,
                             llvm_v8i16_ty], [IntrNoMem, Commutative]>;
  def int_x86_sse2_pmulh_w : ClangBuiltin<"__builtin_ia32_pmulhw128">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty,
                             llvm_v8i16_ty], [IntrNoMem, Commutative]>;
  def int_x86_sse2_pmadd_wd : ClangBuiltin<"__builtin_ia32_pmaddwd128">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v8i16_ty,
                             llvm_v8i16_ty], [IntrNoMem, Commutative]>;
  def int_x86_sse2_pavg_b : ClangBuiltin<"__builtin_ia32_pavgb128">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty,
                             llvm_v16i8_ty], [IntrNoMem, Commutative]>;
  def int_x86_sse2_pavg_w : ClangBuiltin<"__builtin_ia32_pavgw128">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty,
                             llvm_v8i16_ty], [IntrNoMem, Commutative]>;
  def int_x86_sse2_psad_bw : ClangBuiltin<"__builtin_ia32_psadbw128">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v16i8_ty,
                             llvm_v16i8_ty], [IntrNoMem, Commutative]>;
}

// Integer shift ops.
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_sse2_psll_w : ClangBuiltin<"__builtin_ia32_psllw128">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty,
                             llvm_v8i16_ty], [IntrNoMem]>;
  def int_x86_sse2_psll_d : ClangBuiltin<"__builtin_ia32_pslld128">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty,
                             llvm_v4i32_ty], [IntrNoMem]>;
  def int_x86_sse2_psll_q : ClangBuiltin<"__builtin_ia32_psllq128">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty,
                             llvm_v2i64_ty], [IntrNoMem]>;
  def int_x86_sse2_psrl_w : ClangBuiltin<"__builtin_ia32_psrlw128">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty,
                             llvm_v8i16_ty], [IntrNoMem]>;
  def int_x86_sse2_psrl_d : ClangBuiltin<"__builtin_ia32_psrld128">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty,
                             llvm_v4i32_ty], [IntrNoMem]>;
  def int_x86_sse2_psrl_q : ClangBuiltin<"__builtin_ia32_psrlq128">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty,
                             llvm_v2i64_ty], [IntrNoMem]>;
  def int_x86_sse2_psra_w : ClangBuiltin<"__builtin_ia32_psraw128">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty,
                             llvm_v8i16_ty], [IntrNoMem]>;
  def int_x86_sse2_psra_d : ClangBuiltin<"__builtin_ia32_psrad128">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty,
                             llvm_v4i32_ty], [IntrNoMem]>;

  // Oddly these don't require an immediate due to a gcc compatibility issue.
  def int_x86_sse2_pslli_w : ClangBuiltin<"__builtin_ia32_psllwi128">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty,
                             llvm_i32_ty], [IntrNoMem]>;
  def int_x86_sse2_pslli_d : ClangBuiltin<"__builtin_ia32_pslldi128">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty,
                             llvm_i32_ty], [IntrNoMem]>;
  def int_x86_sse2_pslli_q : ClangBuiltin<"__builtin_ia32_psllqi128">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty,
                             llvm_i32_ty], [IntrNoMem]>;
  def int_x86_sse2_psrli_w : ClangBuiltin<"__builtin_ia32_psrlwi128">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty,
                             llvm_i32_ty], [IntrNoMem]>;
  def int_x86_sse2_psrli_d : ClangBuiltin<"__builtin_ia32_psrldi128">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty,
                             llvm_i32_ty], [IntrNoMem]>;
  def int_x86_sse2_psrli_q : ClangBuiltin<"__builtin_ia32_psrlqi128">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty,
                             llvm_i32_ty], [IntrNoMem]>;
  def int_x86_sse2_psrai_w : ClangBuiltin<"__builtin_ia32_psrawi128">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty,
                             llvm_i32_ty], [IntrNoMem]>;
  def int_x86_sse2_psrai_d : ClangBuiltin<"__builtin_ia32_psradi128">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty,
                             llvm_i32_ty], [IntrNoMem]>;
}

// Conversion ops
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_sse2_cvtpd2dq : ClangBuiltin<"__builtin_ia32_cvtpd2dq">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v2f64_ty], [IntrNoMem]>;
  def int_x86_sse2_cvttpd2dq : ClangBuiltin<"__builtin_ia32_cvttpd2dq">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v2f64_ty], [IntrNoMem]>;
  def int_x86_sse2_cvtpd2ps : ClangBuiltin<"__builtin_ia32_cvtpd2ps">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v2f64_ty], [IntrNoMem]>;
  def int_x86_sse2_cvtps2dq : ClangBuiltin<"__builtin_ia32_cvtps2dq">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty], [IntrNoMem]>;
  def int_x86_sse2_cvttps2dq : ClangBuiltin<"__builtin_ia32_cvttps2dq">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty], [IntrNoMem]>;
  def int_x86_sse2_cvtsd2si : ClangBuiltin<"__builtin_ia32_cvtsd2si">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v2f64_ty], [IntrNoMem]>;
  def int_x86_sse2_cvtsd2si64 : ClangBuiltin<"__builtin_ia32_cvtsd2si64">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_v2f64_ty], [IntrNoMem]>;
  def int_x86_sse2_cvttsd2si : ClangBuiltin<"__builtin_ia32_cvttsd2si">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v2f64_ty], [IntrNoMem]>;
  def int_x86_sse2_cvttsd2si64 : ClangBuiltin<"__builtin_ia32_cvttsd2si64">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_v2f64_ty], [IntrNoMem]>;
  def int_x86_sse2_cvtsd2ss : ClangBuiltin<"__builtin_ia32_cvtsd2ss">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty,
                             llvm_v2f64_ty], [IntrNoMem]>;
  def int_x86_sse_cvtpd2pi : ClangBuiltin<"__builtin_ia32_cvtpd2pi">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_v2f64_ty], [IntrNoMem]>;
  def int_x86_sse_cvttpd2pi: ClangBuiltin<"__builtin_ia32_cvttpd2pi">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_v2f64_ty], [IntrNoMem]>;
  def int_x86_sse_cvtpi2pd : ClangBuiltin<"__builtin_ia32_cvtpi2pd">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty], [llvm_x86mmx_ty], [IntrNoMem]>;
}

// Misc.
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_sse2_packsswb_128 : ClangBuiltin<"__builtin_ia32_packsswb128">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_v8i16_ty,
                             llvm_v8i16_ty], [IntrNoMem]>;
  def int_x86_sse2_packssdw_128 : ClangBuiltin<"__builtin_ia32_packssdw128">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v4i32_ty,
                             llvm_v4i32_ty], [IntrNoMem]>;
  def int_x86_sse2_packuswb_128 : ClangBuiltin<"__builtin_ia32_packuswb128">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_v8i16_ty,
                             llvm_v8i16_ty], [IntrNoMem]>;
  def int_x86_sse2_movmsk_pd : ClangBuiltin<"__builtin_ia32_movmskpd">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v2f64_ty], [IntrNoMem]>;
  def int_x86_sse2_pmovmskb_128 : ClangBuiltin<"__builtin_ia32_pmovmskb128">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v16i8_ty], [IntrNoMem]>;
  def int_x86_sse2_maskmov_dqu : ClangBuiltin<"__builtin_ia32_maskmovdqu">,
              Intrinsic<[], [llvm_v16i8_ty,
                         llvm_v16i8_ty, llvm_ptr_ty], []>;
  def int_x86_sse2_clflush : ClangBuiltin<"__builtin_ia32_clflush">,
              Intrinsic<[], [llvm_ptr_ty], []>;
  def int_x86_sse2_lfence : ClangBuiltin<"__builtin_ia32_lfence">,
              Intrinsic<[], [], []>;
  def int_x86_sse2_mfence : ClangBuiltin<"__builtin_ia32_mfence">,
              Intrinsic<[], [], []>;
  def int_x86_sse2_pause : ClangBuiltin<"__builtin_ia32_pause">,
              Intrinsic<[], [], []>;
}

//===----------------------------------------------------------------------===//
// SSE3

// Addition / subtraction ops.
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_sse3_addsub_ps : ClangBuiltin<"__builtin_ia32_addsubps">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty,
                             llvm_v4f32_ty], [IntrNoMem]>;
  def int_x86_sse3_addsub_pd : ClangBuiltin<"__builtin_ia32_addsubpd">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty,
                             llvm_v2f64_ty], [IntrNoMem]>;
}

// Horizontal ops.
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_sse3_hadd_ps : ClangBuiltin<"__builtin_ia32_haddps">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty,
                             llvm_v4f32_ty], [IntrNoMem]>;
  def int_x86_sse3_hadd_pd : ClangBuiltin<"__builtin_ia32_haddpd">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty,
                             llvm_v2f64_ty], [IntrNoMem]>;
  def int_x86_sse3_hsub_ps : ClangBuiltin<"__builtin_ia32_hsubps">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty,
                             llvm_v4f32_ty], [IntrNoMem]>;
  def int_x86_sse3_hsub_pd : ClangBuiltin<"__builtin_ia32_hsubpd">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty,
                             llvm_v2f64_ty], [IntrNoMem]>;
}

// Specialized unaligned load.
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_sse3_ldu_dq : ClangBuiltin<"__builtin_ia32_lddqu">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_ptr_ty], [IntrReadMem]>;
}

// Thread synchronization ops.
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_sse3_monitor : ClangBuiltin<"__builtin_ia32_monitor">,
              Intrinsic<[], [llvm_ptr_ty,
                         llvm_i32_ty, llvm_i32_ty], []>;
  def int_x86_sse3_mwait : ClangBuiltin<"__builtin_ia32_mwait">,
              Intrinsic<[], [llvm_i32_ty,
                         llvm_i32_ty], []>;
}

//===----------------------------------------------------------------------===//
// SSSE3

// Horizontal arithmetic ops
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_ssse3_phadd_w         : ClangBuiltin<"__builtin_ia32_phaddw">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty,
                             llvm_x86mmx_ty], [IntrNoMem]>;
  def int_x86_ssse3_phadd_w_128     : ClangBuiltin<"__builtin_ia32_phaddw128">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty,
                             llvm_v8i16_ty], [IntrNoMem]>;

  def int_x86_ssse3_phadd_d         : ClangBuiltin<"__builtin_ia32_phaddd">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty,
                             llvm_x86mmx_ty], [IntrNoMem]>;
  def int_x86_ssse3_phadd_d_128     : ClangBuiltin<"__builtin_ia32_phaddd128">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty,
                             llvm_v4i32_ty], [IntrNoMem]>;

  def int_x86_ssse3_phadd_sw        : ClangBuiltin<"__builtin_ia32_phaddsw">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty,
                             llvm_x86mmx_ty], [IntrNoMem]>;
  def int_x86_ssse3_phadd_sw_128    : ClangBuiltin<"__builtin_ia32_phaddsw128">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty,
                             llvm_v8i16_ty], [IntrNoMem]>;

  def int_x86_ssse3_phsub_w         : ClangBuiltin<"__builtin_ia32_phsubw">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty,
                             llvm_x86mmx_ty], [IntrNoMem]>;
  def int_x86_ssse3_phsub_w_128     : ClangBuiltin<"__builtin_ia32_phsubw128">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty,
                             llvm_v8i16_ty], [IntrNoMem]>;

  def int_x86_ssse3_phsub_d         : ClangBuiltin<"__builtin_ia32_phsubd">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty,
                             llvm_x86mmx_ty], [IntrNoMem]>;
  def int_x86_ssse3_phsub_d_128     : ClangBuiltin<"__builtin_ia32_phsubd128">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty,
                             llvm_v4i32_ty], [IntrNoMem]>;

  def int_x86_ssse3_phsub_sw        : ClangBuiltin<"__builtin_ia32_phsubsw">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty,
                             llvm_x86mmx_ty], [IntrNoMem]>;
  def int_x86_ssse3_phsub_sw_128    : ClangBuiltin<"__builtin_ia32_phsubsw128">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty,
                             llvm_v8i16_ty], [IntrNoMem]>;

  def int_x86_ssse3_pmadd_ub_sw     : ClangBuiltin<"__builtin_ia32_pmaddubsw">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty,
                             llvm_x86mmx_ty], [IntrNoMem]>;
  def int_x86_ssse3_pmadd_ub_sw_128 : ClangBuiltin<"__builtin_ia32_pmaddubsw128">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v16i8_ty,
                             llvm_v16i8_ty], [IntrNoMem]>;
}

// Packed multiply high with round and scale
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_ssse3_pmul_hr_sw      : ClangBuiltin<"__builtin_ia32_pmulhrsw">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty,
                             llvm_x86mmx_ty], [IntrNoMem, Commutative]>;
  def int_x86_ssse3_pmul_hr_sw_128  : ClangBuiltin<"__builtin_ia32_pmulhrsw128">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty,
                             llvm_v8i16_ty], [IntrNoMem, Commutative]>;
}

// Shuffle ops
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_ssse3_pshuf_b         : ClangBuiltin<"__builtin_ia32_pshufb">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty,
                             llvm_x86mmx_ty], [IntrNoMem]>;
  def int_x86_ssse3_pshuf_b_128     : ClangBuiltin<"__builtin_ia32_pshufb128">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty,
                             llvm_v16i8_ty], [IntrNoMem]>;
  def int_x86_sse_pshuf_w           : ClangBuiltin<"__builtin_ia32_pshufw">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_i8_ty],
                             [IntrNoMem, ImmArg<ArgIndex<1>>]>;
}

// Sign ops
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_ssse3_psign_b         : ClangBuiltin<"__builtin_ia32_psignb">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty,
                             llvm_x86mmx_ty], [IntrNoMem]>;
  def int_x86_ssse3_psign_b_128     : ClangBuiltin<"__builtin_ia32_psignb128">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty,
                             llvm_v16i8_ty], [IntrNoMem]>;

  def int_x86_ssse3_psign_w         : ClangBuiltin<"__builtin_ia32_psignw">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty,
                             llvm_x86mmx_ty], [IntrNoMem]>;
  def int_x86_ssse3_psign_w_128     : ClangBuiltin<"__builtin_ia32_psignw128">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty,
                             llvm_v8i16_ty], [IntrNoMem]>;

  def int_x86_ssse3_psign_d         : ClangBuiltin<"__builtin_ia32_psignd">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty,
                             llvm_x86mmx_ty], [IntrNoMem]>;
  def int_x86_ssse3_psign_d_128     : ClangBuiltin<"__builtin_ia32_psignd128">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty,
                             llvm_v4i32_ty], [IntrNoMem]>;
}

// Absolute value ops
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_ssse3_pabs_b     : ClangBuiltin<"__builtin_ia32_pabsb">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty], [IntrNoMem]>;

  def int_x86_ssse3_pabs_w     : ClangBuiltin<"__builtin_ia32_pabsw">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty], [IntrNoMem]>;

  def int_x86_ssse3_pabs_d     : ClangBuiltin<"__builtin_ia32_pabsd">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty], [IntrNoMem]>;
}

//===----------------------------------------------------------------------===//
// SSE4.1

// FP rounding ops
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_sse41_round_ss        : ClangBuiltin<"__builtin_ia32_roundss">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty, llvm_v4f32_ty,
                             llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_sse41_round_ps        : ClangBuiltin<"__builtin_ia32_roundps">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty,
                             llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_sse41_round_sd        : ClangBuiltin<"__builtin_ia32_roundsd">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty, llvm_v2f64_ty,
                             llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_sse41_round_pd        : ClangBuiltin<"__builtin_ia32_roundpd">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty,
                             llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
}

// Vector min element
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_sse41_phminposuw     : ClangBuiltin<"__builtin_ia32_phminposuw128">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty], [IntrNoMem]>;
}

// Advanced Encryption Standard (AES) Instructions
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_aesni_aesimc          : ClangBuiltin<"__builtin_ia32_aesimc128">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty], [IntrNoMem]>;

  def int_x86_aesni_aesenc          : ClangBuiltin<"__builtin_ia32_aesenc128">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
                            [IntrNoMem]>;
  def int_x86_aesni_aesenc_256      : ClangBuiltin<"__builtin_ia32_aesenc256">,
      DefaultAttrsIntrinsic<[llvm_v4i64_ty], [llvm_v4i64_ty, llvm_v4i64_ty],
                            [IntrNoMem]>;
  def int_x86_aesni_aesenc_512      : ClangBuiltin<"__builtin_ia32_aesenc512">,
      DefaultAttrsIntrinsic<[llvm_v8i64_ty], [llvm_v8i64_ty, llvm_v8i64_ty],
                            [IntrNoMem]>;

  def int_x86_aesni_aesenclast : ClangBuiltin<"__builtin_ia32_aesenclast128">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
                            [IntrNoMem]>;
  def int_x86_aesni_aesenclast_256 :
    ClangBuiltin<"__builtin_ia32_aesenclast256">,
    DefaultAttrsIntrinsic<[llvm_v4i64_ty], [llvm_v4i64_ty, llvm_v4i64_ty],
                          [IntrNoMem]>;
  def int_x86_aesni_aesenclast_512 :
    ClangBuiltin<"__builtin_ia32_aesenclast512">,
    DefaultAttrsIntrinsic<[llvm_v8i64_ty], [llvm_v8i64_ty, llvm_v8i64_ty],
                          [IntrNoMem]>;

  def int_x86_aesni_aesdec          : ClangBuiltin<"__builtin_ia32_aesdec128">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
                            [IntrNoMem]>;
  def int_x86_aesni_aesdec_256      : ClangBuiltin<"__builtin_ia32_aesdec256">,
      DefaultAttrsIntrinsic<[llvm_v4i64_ty], [llvm_v4i64_ty, llvm_v4i64_ty],
                            [IntrNoMem]>;
  def int_x86_aesni_aesdec_512      : ClangBuiltin<"__builtin_ia32_aesdec512">,
      DefaultAttrsIntrinsic<[llvm_v8i64_ty], [llvm_v8i64_ty, llvm_v8i64_ty],
                            [IntrNoMem]>;

  def int_x86_aesni_aesdeclast : ClangBuiltin<"__builtin_ia32_aesdeclast128">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
                            [IntrNoMem]>;
  def int_x86_aesni_aesdeclast_256 :
    ClangBuiltin<"__builtin_ia32_aesdeclast256">,
    DefaultAttrsIntrinsic<[llvm_v4i64_ty], [llvm_v4i64_ty, llvm_v4i64_ty],
                          [IntrNoMem]>;
  def int_x86_aesni_aesdeclast_512 :
    ClangBuiltin<"__builtin_ia32_aesdeclast512">,
    DefaultAttrsIntrinsic<[llvm_v8i64_ty], [llvm_v8i64_ty, llvm_v8i64_ty],
                          [IntrNoMem]>;

  def int_x86_aesni_aeskeygenassist :
    ClangBuiltin<"__builtin_ia32_aeskeygenassist128">,
    DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_i8_ty],
                          [IntrNoMem, ImmArg<ArgIndex<1>>]>;
}

// PCLMUL instructions
let TargetPrefix = "x86" in { // All intrinsics start with "llvm.x86.".
  def int_x86_pclmulqdq : ClangBuiltin<"__builtin_ia32_pclmulqdq128">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty],
                            [llvm_v2i64_ty, llvm_v2i64_ty, llvm_i8_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_pclmulqdq_256 : ClangBuiltin<"__builtin_ia32_pclmulqdq256">,
          DefaultAttrsIntrinsic<[llvm_v4i64_ty],
                                [llvm_v4i64_ty, llvm_v4i64_ty, llvm_i8_ty],
                                [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_pclmulqdq_512 : ClangBuiltin<"__builtin_ia32_pclmulqdq512">,
          DefaultAttrsIntrinsic<[llvm_v8i64_ty],
                                [llvm_v8i64_ty, llvm_v8i64_ty, llvm_i8_ty],
                                [IntrNoMem, ImmArg<ArgIndex<2>>]>;
}

// Vector pack
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_sse41_packusdw : ClangBuiltin<"__builtin_ia32_packusdw128">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
                            [IntrNoMem]>;
}

// Vector insert
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_sse41_insertps       : ClangBuiltin<"__builtin_ia32_insertps128">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                            [llvm_v4f32_ty, llvm_v4f32_ty, llvm_i8_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
}

// Vector blend
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_sse41_pblendvb       : ClangBuiltin<"__builtin_ia32_pblendvb128">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                            [llvm_v16i8_ty, llvm_v16i8_ty,llvm_v16i8_ty],
                            [IntrNoMem]>;
  def int_x86_sse41_blendvpd       : ClangBuiltin<"__builtin_ia32_blendvpd">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty],
                            [llvm_v2f64_ty, llvm_v2f64_ty,llvm_v2f64_ty],
                            [IntrNoMem]>;
  def int_x86_sse41_blendvps       : ClangBuiltin<"__builtin_ia32_blendvps">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                            [llvm_v4f32_ty, llvm_v4f32_ty,llvm_v4f32_ty],
                            [IntrNoMem]>;
}

// Vector dot product
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_sse41_dppd            : ClangBuiltin<"__builtin_ia32_dppd">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty],
                            [llvm_v2f64_ty, llvm_v2f64_ty, llvm_i8_ty],
                            [IntrNoMem, Commutative, ImmArg<ArgIndex<2>>]>;
  def int_x86_sse41_dpps            : ClangBuiltin<"__builtin_ia32_dpps">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                            [llvm_v4f32_ty, llvm_v4f32_ty, llvm_i8_ty],
                            [IntrNoMem, Commutative, ImmArg<ArgIndex<2>>]>;
}

// Vector sum of absolute differences
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_sse41_mpsadbw         : ClangBuiltin<"__builtin_ia32_mpsadbw128">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty],
                            [llvm_v16i8_ty, llvm_v16i8_ty,llvm_i8_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
}

// Test instruction with bitwise comparison.
let TargetPrefix = "x86" in { // All intrinsics start with "llvm.x86.".
  def int_x86_sse41_ptestz          : ClangBuiltin<"__builtin_ia32_ptestz128">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
                            [IntrNoMem]>;
  def int_x86_sse41_ptestc          : ClangBuiltin<"__builtin_ia32_ptestc128">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
                            [IntrNoMem]>;
  def int_x86_sse41_ptestnzc        : ClangBuiltin<"__builtin_ia32_ptestnzc128">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
                            [IntrNoMem]>;
}

//===----------------------------------------------------------------------===//
// SSE4.2

// Miscellaneous
// CRC Instruction
let TargetPrefix = "x86" in { // All intrinsics start with "llvm.x86.".
  def int_x86_sse42_crc32_32_8       : ClangBuiltin<"__builtin_ia32_crc32qi">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_sse42_crc32_32_16      : ClangBuiltin<"__builtin_ia32_crc32hi">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i16_ty],
                            [IntrNoMem]>;
  def int_x86_sse42_crc32_32_32      : ClangBuiltin<"__builtin_ia32_crc32si">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem]>;
  def int_x86_sse42_crc32_64_64      : ClangBuiltin<"__builtin_ia32_crc32di">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty],
                            [IntrNoMem]>;
}

// String/text processing ops.
let TargetPrefix = "x86" in { // All intrinsics start with "llvm.x86.".
  def int_x86_sse42_pcmpistrm128  : ClangBuiltin<"__builtin_ia32_pcmpistrm128">,
    DefaultAttrsIntrinsic<[llvm_v16i8_ty],
        [llvm_v16i8_ty, llvm_v16i8_ty, llvm_i8_ty],
        [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_sse42_pcmpistri128  : ClangBuiltin<"__builtin_ia32_pcmpistri128">,
    DefaultAttrsIntrinsic<[llvm_i32_ty],
        [llvm_v16i8_ty, llvm_v16i8_ty, llvm_i8_ty],
        [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_sse42_pcmpistria128 : ClangBuiltin<"__builtin_ia32_pcmpistria128">,
    DefaultAttrsIntrinsic<[llvm_i32_ty],
        [llvm_v16i8_ty, llvm_v16i8_ty, llvm_i8_ty],
        [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_sse42_pcmpistric128 : ClangBuiltin<"__builtin_ia32_pcmpistric128">,
    DefaultAttrsIntrinsic<[llvm_i32_ty],
        [llvm_v16i8_ty, llvm_v16i8_ty, llvm_i8_ty],
        [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_sse42_pcmpistrio128 : ClangBuiltin<"__builtin_ia32_pcmpistrio128">,
    DefaultAttrsIntrinsic<[llvm_i32_ty],
        [llvm_v16i8_ty, llvm_v16i8_ty, llvm_i8_ty],
        [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_sse42_pcmpistris128 : ClangBuiltin<"__builtin_ia32_pcmpistris128">,
    DefaultAttrsIntrinsic<[llvm_i32_ty],
        [llvm_v16i8_ty, llvm_v16i8_ty, llvm_i8_ty],
        [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_sse42_pcmpistriz128 : ClangBuiltin<"__builtin_ia32_pcmpistriz128">,
    DefaultAttrsIntrinsic<[llvm_i32_ty],
        [llvm_v16i8_ty, llvm_v16i8_ty, llvm_i8_ty],
        [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_sse42_pcmpestrm128  : ClangBuiltin<"__builtin_ia32_pcmpestrm128">,
    DefaultAttrsIntrinsic<[llvm_v16i8_ty],
        [llvm_v16i8_ty, llvm_i32_ty, llvm_v16i8_ty, llvm_i32_ty,
         llvm_i8_ty],
        [IntrNoMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_sse42_pcmpestri128  : ClangBuiltin<"__builtin_ia32_pcmpestri128">,
    DefaultAttrsIntrinsic<[llvm_i32_ty],
        [llvm_v16i8_ty, llvm_i32_ty, llvm_v16i8_ty, llvm_i32_ty,
         llvm_i8_ty],
        [IntrNoMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_sse42_pcmpestria128 : ClangBuiltin<"__builtin_ia32_pcmpestria128">,
    DefaultAttrsIntrinsic<[llvm_i32_ty],
        [llvm_v16i8_ty, llvm_i32_ty, llvm_v16i8_ty, llvm_i32_ty,
         llvm_i8_ty],
        [IntrNoMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_sse42_pcmpestric128 : ClangBuiltin<"__builtin_ia32_pcmpestric128">,
    DefaultAttrsIntrinsic<[llvm_i32_ty],
        [llvm_v16i8_ty, llvm_i32_ty, llvm_v16i8_ty, llvm_i32_ty,
         llvm_i8_ty],
        [IntrNoMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_sse42_pcmpestrio128 : ClangBuiltin<"__builtin_ia32_pcmpestrio128">,
    DefaultAttrsIntrinsic<[llvm_i32_ty],
        [llvm_v16i8_ty, llvm_i32_ty, llvm_v16i8_ty, llvm_i32_ty,
         llvm_i8_ty],
        [IntrNoMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_sse42_pcmpestris128 : ClangBuiltin<"__builtin_ia32_pcmpestris128">,
    DefaultAttrsIntrinsic<[llvm_i32_ty],
        [llvm_v16i8_ty, llvm_i32_ty, llvm_v16i8_ty, llvm_i32_ty,
         llvm_i8_ty],
        [IntrNoMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_sse42_pcmpestriz128 : ClangBuiltin<"__builtin_ia32_pcmpestriz128">,
    DefaultAttrsIntrinsic<[llvm_i32_ty],
        [llvm_v16i8_ty, llvm_i32_ty, llvm_v16i8_ty, llvm_i32_ty,
         llvm_i8_ty],
        [IntrNoMem, ImmArg<ArgIndex<4>>]>;
}

//===----------------------------------------------------------------------===//
// SSE4A

let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_sse4a_extrqi : ClangBuiltin<"__builtin_ia32_extrqi">,
    DefaultAttrsIntrinsic<[llvm_v2i64_ty],
                          [llvm_v2i64_ty, llvm_i8_ty, llvm_i8_ty],
                          [IntrNoMem, ImmArg<ArgIndex<1>>,
                           ImmArg<ArgIndex<2>>]>;
  def int_x86_sse4a_extrq  : ClangBuiltin<"__builtin_ia32_extrq">,
    DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v16i8_ty],
                          [IntrNoMem]>;

  def int_x86_sse4a_insertqi : ClangBuiltin<"__builtin_ia32_insertqi">,
    DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty,
                                            llvm_i8_ty, llvm_i8_ty],
                          [IntrNoMem, ImmArg<ArgIndex<2>>,
                           ImmArg<ArgIndex<3>>]>;
  def int_x86_sse4a_insertq  : ClangBuiltin<"__builtin_ia32_insertq">,
    DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
                          [IntrNoMem]>;
}

//===----------------------------------------------------------------------===//
// AVX

// Arithmetic ops
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_avx_addsub_pd_256 : ClangBuiltin<"__builtin_ia32_addsubpd256">,
      DefaultAttrsIntrinsic<[llvm_v4f64_ty], [llvm_v4f64_ty, llvm_v4f64_ty],
                            [IntrNoMem]>;
  def int_x86_avx_addsub_ps_256 : ClangBuiltin<"__builtin_ia32_addsubps256">,
      DefaultAttrsIntrinsic<[llvm_v8f32_ty], [llvm_v8f32_ty, llvm_v8f32_ty],
                            [IntrNoMem]>;
  def int_x86_avx_max_pd_256 : ClangBuiltin<"__builtin_ia32_maxpd256">,
      DefaultAttrsIntrinsic<[llvm_v4f64_ty], [llvm_v4f64_ty, llvm_v4f64_ty],
                            [IntrNoMem]>;
  def int_x86_avx_max_ps_256 : ClangBuiltin<"__builtin_ia32_maxps256">,
      DefaultAttrsIntrinsic<[llvm_v8f32_ty], [llvm_v8f32_ty, llvm_v8f32_ty],
                            [IntrNoMem]>;
  def int_x86_avx_min_pd_256 : ClangBuiltin<"__builtin_ia32_minpd256">,
      DefaultAttrsIntrinsic<[llvm_v4f64_ty], [llvm_v4f64_ty, llvm_v4f64_ty],
                            [IntrNoMem]>;
  def int_x86_avx_min_ps_256 : ClangBuiltin<"__builtin_ia32_minps256">,
      DefaultAttrsIntrinsic<[llvm_v8f32_ty], [llvm_v8f32_ty, llvm_v8f32_ty],
                            [IntrNoMem]>;

  def int_x86_avx_rsqrt_ps_256 : ClangBuiltin<"__builtin_ia32_rsqrtps256">,
      DefaultAttrsIntrinsic<[llvm_v8f32_ty], [llvm_v8f32_ty], [IntrNoMem]>;

  def int_x86_avx_rcp_ps_256 : ClangBuiltin<"__builtin_ia32_rcpps256">,
      DefaultAttrsIntrinsic<[llvm_v8f32_ty], [llvm_v8f32_ty], [IntrNoMem]>;

  def int_x86_avx_round_pd_256 : ClangBuiltin<"__builtin_ia32_roundpd256">,
      DefaultAttrsIntrinsic<[llvm_v4f64_ty], [llvm_v4f64_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx_round_ps_256 : ClangBuiltin<"__builtin_ia32_roundps256">,
      DefaultAttrsIntrinsic<[llvm_v8f32_ty], [llvm_v8f32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
}

// Horizontal ops
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_avx_hadd_pd_256 : ClangBuiltin<"__builtin_ia32_haddpd256">,
      DefaultAttrsIntrinsic<[llvm_v4f64_ty], [llvm_v4f64_ty, llvm_v4f64_ty],
                            [IntrNoMem]>;
  def int_x86_avx_hsub_ps_256 : ClangBuiltin<"__builtin_ia32_hsubps256">,
      DefaultAttrsIntrinsic<[llvm_v8f32_ty], [llvm_v8f32_ty, llvm_v8f32_ty],
                            [IntrNoMem]>;
  def int_x86_avx_hsub_pd_256 : ClangBuiltin<"__builtin_ia32_hsubpd256">,
      DefaultAttrsIntrinsic<[llvm_v4f64_ty], [llvm_v4f64_ty, llvm_v4f64_ty],
                            [IntrNoMem]>;
  def int_x86_avx_hadd_ps_256 : ClangBuiltin<"__builtin_ia32_haddps256">,
      DefaultAttrsIntrinsic<[llvm_v8f32_ty], [llvm_v8f32_ty, llvm_v8f32_ty],
                            [IntrNoMem]>;
}

// Vector permutation
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_avx_vpermilvar_pd : ClangBuiltin<"__builtin_ia32_vpermilvarpd">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty, llvm_v2i64_ty],
                            [IntrNoMem]>;
  def int_x86_avx_vpermilvar_ps : ClangBuiltin<"__builtin_ia32_vpermilvarps">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty, llvm_v4i32_ty],
                            [IntrNoMem]>;

  def int_x86_avx_vpermilvar_pd_256 :
        ClangBuiltin<"__builtin_ia32_vpermilvarpd256">,
        DefaultAttrsIntrinsic<[llvm_v4f64_ty], [llvm_v4f64_ty, llvm_v4i64_ty],
                              [IntrNoMem]>;
  def int_x86_avx_vpermilvar_ps_256 :
        ClangBuiltin<"__builtin_ia32_vpermilvarps256">,
        DefaultAttrsIntrinsic<[llvm_v8f32_ty], [llvm_v8f32_ty, llvm_v8i32_ty],
                              [IntrNoMem]>;

  def int_x86_avx512_vpermi2var_d_128 :
       ClangBuiltin<"__builtin_ia32_vpermi2vard128">,
       DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                             [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
                             [IntrNoMem]>;

  def int_x86_avx512_vpermi2var_d_256 :
        ClangBuiltin<"__builtin_ia32_vpermi2vard256">,
        DefaultAttrsIntrinsic<[llvm_v8i32_ty],
                              [llvm_v8i32_ty, llvm_v8i32_ty, llvm_v8i32_ty],
                              [IntrNoMem]>;

  def int_x86_avx512_vpermi2var_d_512 :
        ClangBuiltin<"__builtin_ia32_vpermi2vard512">,
        DefaultAttrsIntrinsic<[llvm_v16i32_ty],
                              [llvm_v16i32_ty, llvm_v16i32_ty, llvm_v16i32_ty],
                              [IntrNoMem]>;

  def int_x86_avx512_vpermi2var_hi_128 :
        ClangBuiltin<"__builtin_ia32_vpermi2varhi128">,
        DefaultAttrsIntrinsic<[llvm_v8i16_ty],
                              [llvm_v8i16_ty, llvm_v8i16_ty, llvm_v8i16_ty],
                              [IntrNoMem]>;

  def int_x86_avx512_vpermi2var_hi_256 :
        ClangBuiltin<"__builtin_ia32_vpermi2varhi256">,
        DefaultAttrsIntrinsic<[llvm_v16i16_ty],
                              [llvm_v16i16_ty, llvm_v16i16_ty, llvm_v16i16_ty],
                              [IntrNoMem]>;

  def int_x86_avx512_vpermi2var_hi_512 :
        ClangBuiltin<"__builtin_ia32_vpermi2varhi512">,
        DefaultAttrsIntrinsic<[llvm_v32i16_ty],
                              [llvm_v32i16_ty, llvm_v32i16_ty, llvm_v32i16_ty],
                              [IntrNoMem]>;

  def int_x86_avx512_vpermi2var_pd_128 :
        ClangBuiltin<"__builtin_ia32_vpermi2varpd128">,
        DefaultAttrsIntrinsic<[llvm_v2f64_ty],
                              [llvm_v2f64_ty, llvm_v2i64_ty, llvm_v2f64_ty],
                              [IntrNoMem]>;

  def int_x86_avx512_vpermi2var_pd_256 :
        ClangBuiltin<"__builtin_ia32_vpermi2varpd256">,
        DefaultAttrsIntrinsic<[llvm_v4f64_ty],
                              [llvm_v4f64_ty, llvm_v4i64_ty, llvm_v4f64_ty],
                              [IntrNoMem]>;

  def int_x86_avx512_vpermi2var_pd_512 :
        ClangBuiltin<"__builtin_ia32_vpermi2varpd512">,
        DefaultAttrsIntrinsic<[llvm_v8f64_ty],
                              [llvm_v8f64_ty, llvm_v8i64_ty, llvm_v8f64_ty],
                              [IntrNoMem]>;

  def int_x86_avx512_vpermi2var_ps_128 :
        ClangBuiltin<"__builtin_ia32_vpermi2varps128">,
        DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                              [llvm_v4f32_ty, llvm_v4i32_ty, llvm_v4f32_ty],
                              [IntrNoMem]>;

  def int_x86_avx512_vpermi2var_ps_256 :
        ClangBuiltin<"__builtin_ia32_vpermi2varps256">,
        DefaultAttrsIntrinsic<[llvm_v8f32_ty],
                              [llvm_v8f32_ty, llvm_v8i32_ty, llvm_v8f32_ty],
                              [IntrNoMem]>;

  def int_x86_avx512_vpermi2var_ps_512 :
        ClangBuiltin<"__builtin_ia32_vpermi2varps512">,
        DefaultAttrsIntrinsic<[llvm_v16f32_ty],
                              [llvm_v16f32_ty, llvm_v16i32_ty, llvm_v16f32_ty],
                              [IntrNoMem]>;

  def int_x86_avx512_vpermi2var_q_128 :
        ClangBuiltin<"__builtin_ia32_vpermi2varq128">,
        DefaultAttrsIntrinsic<[llvm_v2i64_ty],
                              [llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty],
                              [IntrNoMem]>;

  def int_x86_avx512_vpermi2var_q_256 :
        ClangBuiltin<"__builtin_ia32_vpermi2varq256">,
        DefaultAttrsIntrinsic<[llvm_v4i64_ty],
                              [llvm_v4i64_ty, llvm_v4i64_ty, llvm_v4i64_ty],
                              [IntrNoMem]>;

  def int_x86_avx512_vpermi2var_q_512 :
        ClangBuiltin<"__builtin_ia32_vpermi2varq512">,
        DefaultAttrsIntrinsic<[llvm_v8i64_ty],
                              [llvm_v8i64_ty, llvm_v8i64_ty, llvm_v8i64_ty],
                              [IntrNoMem]>;

  def int_x86_avx512_vpermi2var_qi_128 :
        ClangBuiltin<"__builtin_ia32_vpermi2varqi128">,
        DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                              [llvm_v16i8_ty, llvm_v16i8_ty, llvm_v16i8_ty],
                              [IntrNoMem]>;

  def int_x86_avx512_vpermi2var_qi_256 :
        ClangBuiltin<"__builtin_ia32_vpermi2varqi256">,
        DefaultAttrsIntrinsic<[llvm_v32i8_ty],
                              [llvm_v32i8_ty, llvm_v32i8_ty, llvm_v32i8_ty],
                              [IntrNoMem]>;

  def int_x86_avx512_vpermi2var_qi_512 :
        ClangBuiltin<"__builtin_ia32_vpermi2varqi512">,
        DefaultAttrsIntrinsic<[llvm_v64i8_ty],
                              [llvm_v64i8_ty, llvm_v64i8_ty, llvm_v64i8_ty],
                              [IntrNoMem]>;

  def int_x86_avx512_vpermilvar_pd_512 :
        ClangBuiltin<"__builtin_ia32_vpermilvarpd512">,
        DefaultAttrsIntrinsic<[llvm_v8f64_ty], [llvm_v8f64_ty, llvm_v8i64_ty],
                              [IntrNoMem]>;

  def int_x86_avx512_vpermilvar_ps_512 :
        ClangBuiltin<"__builtin_ia32_vpermilvarps512">,
        DefaultAttrsIntrinsic<[llvm_v16f32_ty],
                              [llvm_v16f32_ty, llvm_v16i32_ty], [IntrNoMem]>;

  def int_x86_avx512_pshuf_b_512 :
        ClangBuiltin<"__builtin_ia32_pshufb512">,
        DefaultAttrsIntrinsic<[llvm_v64i8_ty], [llvm_v64i8_ty, llvm_v64i8_ty],
                              [IntrNoMem]>;

}

// GFNI Instructions
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_vgf2p8affineinvqb_128 :
         ClangBuiltin<"__builtin_ia32_vgf2p8affineinvqb_v16qi">,
         DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                               [llvm_v16i8_ty, llvm_v16i8_ty, llvm_i8_ty],
                               [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_vgf2p8affineinvqb_256 :
         ClangBuiltin<"__builtin_ia32_vgf2p8affineinvqb_v32qi">,
         DefaultAttrsIntrinsic<[llvm_v32i8_ty],
                               [llvm_v32i8_ty, llvm_v32i8_ty, llvm_i8_ty],
                               [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_vgf2p8affineinvqb_512 :
         ClangBuiltin<"__builtin_ia32_vgf2p8affineinvqb_v64qi">,
         DefaultAttrsIntrinsic<[llvm_v64i8_ty],
                               [llvm_v64i8_ty, llvm_v64i8_ty, llvm_i8_ty],
                               [IntrNoMem, ImmArg<ArgIndex<2>>]>;

  def int_x86_vgf2p8affineqb_128 :
         ClangBuiltin<"__builtin_ia32_vgf2p8affineqb_v16qi">,
         DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                               [llvm_v16i8_ty, llvm_v16i8_ty, llvm_i8_ty],
                               [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_vgf2p8affineqb_256 :
         ClangBuiltin<"__builtin_ia32_vgf2p8affineqb_v32qi">,
         DefaultAttrsIntrinsic<[llvm_v32i8_ty],
                               [llvm_v32i8_ty, llvm_v32i8_ty, llvm_i8_ty],
                               [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_vgf2p8affineqb_512 :
         ClangBuiltin<"__builtin_ia32_vgf2p8affineqb_v64qi">,
         DefaultAttrsIntrinsic<[llvm_v64i8_ty],
                               [llvm_v64i8_ty, llvm_v64i8_ty, llvm_i8_ty],
                               [IntrNoMem, ImmArg<ArgIndex<2>>]>;

  def int_x86_vgf2p8mulb_128     :
         ClangBuiltin<"__builtin_ia32_vgf2p8mulb_v16qi">,
         DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty],
                               [IntrNoMem]>;
  def int_x86_vgf2p8mulb_256     :
         ClangBuiltin<"__builtin_ia32_vgf2p8mulb_v32qi">,
         DefaultAttrsIntrinsic<[llvm_v32i8_ty], [llvm_v32i8_ty, llvm_v32i8_ty],
                               [IntrNoMem]>;
  def int_x86_vgf2p8mulb_512     :
         ClangBuiltin<"__builtin_ia32_vgf2p8mulb_v64qi">,
         DefaultAttrsIntrinsic<[llvm_v64i8_ty], [llvm_v64i8_ty, llvm_v64i8_ty],
                               [IntrNoMem]>;
}

// Vector blend
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_avx_blendv_pd_256 : ClangBuiltin<"__builtin_ia32_blendvpd256">,
      DefaultAttrsIntrinsic<[llvm_v4f64_ty],
                            [llvm_v4f64_ty, llvm_v4f64_ty, llvm_v4f64_ty],
                            [IntrNoMem]>;
  def int_x86_avx_blendv_ps_256 : ClangBuiltin<"__builtin_ia32_blendvps256">,
      DefaultAttrsIntrinsic<[llvm_v8f32_ty],
                            [llvm_v8f32_ty, llvm_v8f32_ty, llvm_v8f32_ty],
                            [IntrNoMem]>;
}

// Vector dot product
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_avx_dp_ps_256 : ClangBuiltin<"__builtin_ia32_dpps256">,
      DefaultAttrsIntrinsic<[llvm_v8f32_ty],
                            [llvm_v8f32_ty, llvm_v8f32_ty, llvm_i8_ty],
                            [IntrNoMem, Commutative, ImmArg<ArgIndex<2>>]>;
}

// Vector compare
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_avx_cmp_pd_256 :
      DefaultAttrsIntrinsic<[llvm_v4f64_ty],
                            [llvm_v4f64_ty, llvm_v4f64_ty, llvm_i8_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_avx_cmp_ps_256 :
      DefaultAttrsIntrinsic<[llvm_v8f32_ty],
                            [llvm_v8f32_ty, llvm_v8f32_ty, llvm_i8_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
}

// Vector convert
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_avx_cvt_pd2_ps_256 : ClangBuiltin<"__builtin_ia32_cvtpd2ps256">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4f64_ty], [IntrNoMem]>;
  def int_x86_avx_cvt_ps2dq_256 : ClangBuiltin<"__builtin_ia32_cvtps2dq256">,
      DefaultAttrsIntrinsic<[llvm_v8i32_ty], [llvm_v8f32_ty], [IntrNoMem]>;
  def int_x86_avx_cvtt_pd2dq_256 : ClangBuiltin<"__builtin_ia32_cvttpd2dq256">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4f64_ty], [IntrNoMem]>;
  def int_x86_avx_cvt_pd2dq_256 : ClangBuiltin<"__builtin_ia32_cvtpd2dq256">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4f64_ty], [IntrNoMem]>;
  def int_x86_avx_cvtt_ps2dq_256 : ClangBuiltin<"__builtin_ia32_cvttps2dq256">,
      DefaultAttrsIntrinsic<[llvm_v8i32_ty], [llvm_v8f32_ty], [IntrNoMem]>;
}

// Vector bit test
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_avx_vtestz_pd : ClangBuiltin<"__builtin_ia32_vtestzpd">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v2f64_ty, llvm_v2f64_ty],
                            [IntrNoMem]>;
  def int_x86_avx_vtestc_pd : ClangBuiltin<"__builtin_ia32_vtestcpd">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v2f64_ty, llvm_v2f64_ty],
                            [IntrNoMem]>;
  def int_x86_avx_vtestnzc_pd : ClangBuiltin<"__builtin_ia32_vtestnzcpd">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v2f64_ty, llvm_v2f64_ty],
                            [IntrNoMem]>;
  def int_x86_avx_vtestz_ps : ClangBuiltin<"__builtin_ia32_vtestzps">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v4f32_ty, llvm_v4f32_ty],
                            [IntrNoMem]>;
  def int_x86_avx_vtestc_ps : ClangBuiltin<"__builtin_ia32_vtestcps">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v4f32_ty, llvm_v4f32_ty],
                            [IntrNoMem]>;
  def int_x86_avx_vtestnzc_ps : ClangBuiltin<"__builtin_ia32_vtestnzcps">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v4f32_ty, llvm_v4f32_ty],
                            [IntrNoMem]>;
  def int_x86_avx_vtestz_pd_256 : ClangBuiltin<"__builtin_ia32_vtestzpd256">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v4f64_ty, llvm_v4f64_ty],
                            [IntrNoMem]>;
  def int_x86_avx_vtestc_pd_256 : ClangBuiltin<"__builtin_ia32_vtestcpd256">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v4f64_ty, llvm_v4f64_ty],
                            [IntrNoMem]>;
  def int_x86_avx_vtestnzc_pd_256 : ClangBuiltin<"__builtin_ia32_vtestnzcpd256">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v4f64_ty, llvm_v4f64_ty],
                            [IntrNoMem]>;
  def int_x86_avx_vtestz_ps_256 : ClangBuiltin<"__builtin_ia32_vtestzps256">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v8f32_ty, llvm_v8f32_ty],
                            [IntrNoMem]>;
  def int_x86_avx_vtestc_ps_256 : ClangBuiltin<"__builtin_ia32_vtestcps256">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v8f32_ty, llvm_v8f32_ty],
                            [IntrNoMem]>;
  def int_x86_avx_vtestnzc_ps_256 : ClangBuiltin<"__builtin_ia32_vtestnzcps256">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v8f32_ty, llvm_v8f32_ty],
                            [IntrNoMem]>;
  def int_x86_avx_ptestz_256 : ClangBuiltin<"__builtin_ia32_ptestz256">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v4i64_ty, llvm_v4i64_ty],
                            [IntrNoMem]>;
  def int_x86_avx_ptestc_256 : ClangBuiltin<"__builtin_ia32_ptestc256">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v4i64_ty, llvm_v4i64_ty],
                            [IntrNoMem]>;
  def int_x86_avx_ptestnzc_256 : ClangBuiltin<"__builtin_ia32_ptestnzc256">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v4i64_ty, llvm_v4i64_ty],
                            [IntrNoMem]>;

  def int_x86_avx512_fpclass_pd_128 :
      DefaultAttrsIntrinsic<[llvm_v2i1_ty], [llvm_v2f64_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_fpclass_pd_256 :
      DefaultAttrsIntrinsic<[llvm_v4i1_ty], [llvm_v4f64_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_fpclass_pd_512 :
      DefaultAttrsIntrinsic<[llvm_v8i1_ty], [llvm_v8f64_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_fpclass_ps_128 :
      DefaultAttrsIntrinsic<[llvm_v4i1_ty], [llvm_v4f32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_fpclass_ps_256 :
      DefaultAttrsIntrinsic<[llvm_v8i1_ty], [llvm_v8f32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_fpclass_ps_512 :
      DefaultAttrsIntrinsic<[llvm_v16i1_ty], [llvm_v16f32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_mask_fpclass_sd :
      ClangBuiltin<"__builtin_ia32_fpclasssd_mask">,
      DefaultAttrsIntrinsic<[llvm_i8_ty],
                            [llvm_v2f64_ty, llvm_i32_ty, llvm_i8_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_mask_fpclass_ss :
      ClangBuiltin<"__builtin_ia32_fpclassss_mask">,
      DefaultAttrsIntrinsic<[llvm_i8_ty],
                            [llvm_v4f32_ty, llvm_i32_ty, llvm_i8_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
}

// Vector extract sign mask
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_avx_movmsk_pd_256 : ClangBuiltin<"__builtin_ia32_movmskpd256">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v4f64_ty], [IntrNoMem]>;
  def int_x86_avx_movmsk_ps_256 : ClangBuiltin<"__builtin_ia32_movmskps256">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v8f32_ty], [IntrNoMem]>;
}

// Vector zero
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_avx_vzeroall : ClangBuiltin<"__builtin_ia32_vzeroall">,
        Intrinsic<[], [], [IntrNoMem, IntrHasSideEffects]>;
  def int_x86_avx_vzeroupper : ClangBuiltin<"__builtin_ia32_vzeroupper">,
        Intrinsic<[], [], [IntrNoMem, IntrHasSideEffects]>;
}

// SIMD load ops
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_avx_ldu_dq_256 : ClangBuiltin<"__builtin_ia32_lddqu256">,
      DefaultAttrsIntrinsic<[llvm_v32i8_ty], [llvm_ptr_ty], [IntrReadMem]>;
}

// Conditional load ops
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_avx_maskload_pd : ClangBuiltin<"__builtin_ia32_maskloadpd">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty], [llvm_ptr_ty, llvm_v2i64_ty],
                            [IntrReadMem, IntrArgMemOnly]>;
  def int_x86_avx_maskload_ps : ClangBuiltin<"__builtin_ia32_maskloadps">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_ptr_ty, llvm_v4i32_ty],
                            [IntrReadMem, IntrArgMemOnly]>;
  def int_x86_avx_maskload_pd_256 : ClangBuiltin<"__builtin_ia32_maskloadpd256">,
      DefaultAttrsIntrinsic<[llvm_v4f64_ty], [llvm_ptr_ty, llvm_v4i64_ty],
                            [IntrReadMem, IntrArgMemOnly]>;
  def int_x86_avx_maskload_ps_256 : ClangBuiltin<"__builtin_ia32_maskloadps256">,
      DefaultAttrsIntrinsic<[llvm_v8f32_ty], [llvm_ptr_ty, llvm_v8i32_ty],
                            [IntrReadMem, IntrArgMemOnly]>;
}

// Conditional store ops
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_avx_maskstore_pd : ClangBuiltin<"__builtin_ia32_maskstorepd">,
        Intrinsic<[], [llvm_ptr_ty,
                  llvm_v2i64_ty, llvm_v2f64_ty], [IntrArgMemOnly]>;
  def int_x86_avx_maskstore_ps : ClangBuiltin<"__builtin_ia32_maskstoreps">,
        Intrinsic<[], [llvm_ptr_ty,
                  llvm_v4i32_ty, llvm_v4f32_ty], [IntrArgMemOnly]>;
  def int_x86_avx_maskstore_pd_256 :
        ClangBuiltin<"__builtin_ia32_maskstorepd256">,
        Intrinsic<[], [llvm_ptr_ty,
                  llvm_v4i64_ty, llvm_v4f64_ty], [IntrArgMemOnly]>;
  def int_x86_avx_maskstore_ps_256 :
        ClangBuiltin<"__builtin_ia32_maskstoreps256">,
        Intrinsic<[], [llvm_ptr_ty,
                  llvm_v8i32_ty, llvm_v8f32_ty], [IntrArgMemOnly]>;
}

// BITALG bits shuffle
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_avx512_vpshufbitqmb_128 :
    DefaultAttrsIntrinsic<[llvm_v16i1_ty], [llvm_v16i8_ty, llvm_v16i8_ty],
                          [IntrNoMem]>;
  def int_x86_avx512_vpshufbitqmb_256 :
    DefaultAttrsIntrinsic<[llvm_v32i1_ty], [llvm_v32i8_ty, llvm_v32i8_ty],
                          [IntrNoMem]>;
  def int_x86_avx512_vpshufbitqmb_512 :
    DefaultAttrsIntrinsic<[llvm_v64i1_ty], [llvm_v64i8_ty, llvm_v64i8_ty],
                          [IntrNoMem]>;
}

//===----------------------------------------------------------------------===//
// AVX2

// Integer arithmetic ops.
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_avx2_pmulhu_w : ClangBuiltin<"__builtin_ia32_pmulhuw256">,
      DefaultAttrsIntrinsic<[llvm_v16i16_ty], [llvm_v16i16_ty,
                             llvm_v16i16_ty], [IntrNoMem, Commutative]>;
  def int_x86_avx2_pmulh_w : ClangBuiltin<"__builtin_ia32_pmulhw256">,
      DefaultAttrsIntrinsic<[llvm_v16i16_ty], [llvm_v16i16_ty,
                             llvm_v16i16_ty], [IntrNoMem, Commutative]>;
  def int_x86_avx2_pmadd_wd : ClangBuiltin<"__builtin_ia32_pmaddwd256">,
      DefaultAttrsIntrinsic<[llvm_v8i32_ty], [llvm_v16i16_ty,
                             llvm_v16i16_ty], [IntrNoMem, Commutative]>;
  def int_x86_avx2_pavg_b : ClangBuiltin<"__builtin_ia32_pavgb256">,
      DefaultAttrsIntrinsic<[llvm_v32i8_ty], [llvm_v32i8_ty,
                             llvm_v32i8_ty], [IntrNoMem, Commutative]>;
  def int_x86_avx2_pavg_w : ClangBuiltin<"__builtin_ia32_pavgw256">,
      DefaultAttrsIntrinsic<[llvm_v16i16_ty], [llvm_v16i16_ty,
                             llvm_v16i16_ty], [IntrNoMem, Commutative]>;
  def int_x86_avx2_psad_bw : ClangBuiltin<"__builtin_ia32_psadbw256">,
      DefaultAttrsIntrinsic<[llvm_v4i64_ty], [llvm_v32i8_ty,
                             llvm_v32i8_ty], [IntrNoMem, Commutative]>;
}

// Integer shift ops.
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_avx2_psll_w : ClangBuiltin<"__builtin_ia32_psllw256">,
      DefaultAttrsIntrinsic<[llvm_v16i16_ty], [llvm_v16i16_ty,
                             llvm_v8i16_ty], [IntrNoMem]>;
  def int_x86_avx2_psll_d : ClangBuiltin<"__builtin_ia32_pslld256">,
      DefaultAttrsIntrinsic<[llvm_v8i32_ty], [llvm_v8i32_ty,
                             llvm_v4i32_ty], [IntrNoMem]>;
  def int_x86_avx2_psll_q : ClangBuiltin<"__builtin_ia32_psllq256">,
      DefaultAttrsIntrinsic<[llvm_v4i64_ty], [llvm_v4i64_ty,
                             llvm_v2i64_ty], [IntrNoMem]>;
  def int_x86_avx2_psrl_w : ClangBuiltin<"__builtin_ia32_psrlw256">,
      DefaultAttrsIntrinsic<[llvm_v16i16_ty], [llvm_v16i16_ty,
                             llvm_v8i16_ty], [IntrNoMem]>;
  def int_x86_avx2_psrl_d : ClangBuiltin<"__builtin_ia32_psrld256">,
      DefaultAttrsIntrinsic<[llvm_v8i32_ty], [llvm_v8i32_ty,
                             llvm_v4i32_ty], [IntrNoMem]>;
  def int_x86_avx2_psrl_q : ClangBuiltin<"__builtin_ia32_psrlq256">,
      DefaultAttrsIntrinsic<[llvm_v4i64_ty], [llvm_v4i64_ty,
                             llvm_v2i64_ty], [IntrNoMem]>;
  def int_x86_avx2_psra_w : ClangBuiltin<"__builtin_ia32_psraw256">,
      DefaultAttrsIntrinsic<[llvm_v16i16_ty], [llvm_v16i16_ty,
                             llvm_v8i16_ty], [IntrNoMem]>;
  def int_x86_avx2_psra_d : ClangBuiltin<"__builtin_ia32_psrad256">,
      DefaultAttrsIntrinsic<[llvm_v8i32_ty], [llvm_v8i32_ty,
                             llvm_v4i32_ty], [IntrNoMem]>;

  // Oddly these don't require an immediate due to a gcc compatibility issue.
  def int_x86_avx2_pslli_w : ClangBuiltin<"__builtin_ia32_psllwi256">,
      DefaultAttrsIntrinsic<[llvm_v16i16_ty], [llvm_v16i16_ty,
                             llvm_i32_ty], [IntrNoMem]>;
  def int_x86_avx2_pslli_d : ClangBuiltin<"__builtin_ia32_pslldi256">,
      DefaultAttrsIntrinsic<[llvm_v8i32_ty], [llvm_v8i32_ty,
                             llvm_i32_ty], [IntrNoMem]>;
  def int_x86_avx2_pslli_q : ClangBuiltin<"__builtin_ia32_psllqi256">,
      DefaultAttrsIntrinsic<[llvm_v4i64_ty], [llvm_v4i64_ty,
                             llvm_i32_ty], [IntrNoMem]>;
  def int_x86_avx2_psrli_w : ClangBuiltin<"__builtin_ia32_psrlwi256">,
      DefaultAttrsIntrinsic<[llvm_v16i16_ty], [llvm_v16i16_ty,
                             llvm_i32_ty], [IntrNoMem]>;
  def int_x86_avx2_psrli_d : ClangBuiltin<"__builtin_ia32_psrldi256">,
      DefaultAttrsIntrinsic<[llvm_v8i32_ty], [llvm_v8i32_ty,
                             llvm_i32_ty], [IntrNoMem]>;
  def int_x86_avx2_psrli_q : ClangBuiltin<"__builtin_ia32_psrlqi256">,
      DefaultAttrsIntrinsic<[llvm_v4i64_ty], [llvm_v4i64_ty,
                             llvm_i32_ty], [IntrNoMem]>;
  def int_x86_avx2_psrai_w : ClangBuiltin<"__builtin_ia32_psrawi256">,
      DefaultAttrsIntrinsic<[llvm_v16i16_ty], [llvm_v16i16_ty,
                             llvm_i32_ty], [IntrNoMem]>;
  def int_x86_avx2_psrai_d : ClangBuiltin<"__builtin_ia32_psradi256">,
      DefaultAttrsIntrinsic<[llvm_v8i32_ty], [llvm_v8i32_ty,
                             llvm_i32_ty], [IntrNoMem]>;

  def int_x86_avx512_psra_q_128 : ClangBuiltin<"__builtin_ia32_psraq128">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty,
                             llvm_v2i64_ty], [IntrNoMem]>;
  def int_x86_avx512_psra_q_256 : ClangBuiltin<"__builtin_ia32_psraq256">,
      DefaultAttrsIntrinsic<[llvm_v4i64_ty], [llvm_v4i64_ty,
                             llvm_v2i64_ty], [IntrNoMem]>;

  // Oddly these don't require an immediate due to a gcc compatibility issue.
  def int_x86_avx512_psrai_q_128 : ClangBuiltin<"__builtin_ia32_psraqi128">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty,
                             llvm_i32_ty], [IntrNoMem]>;
  def int_x86_avx512_psrai_q_256 : ClangBuiltin<"__builtin_ia32_psraqi256">,
      DefaultAttrsIntrinsic<[llvm_v4i64_ty], [llvm_v4i64_ty,
                             llvm_i32_ty], [IntrNoMem]>;

  def int_x86_avx512_psll_w_512 : ClangBuiltin<"__builtin_ia32_psllw512">,
      DefaultAttrsIntrinsic<[llvm_v32i16_ty], [llvm_v32i16_ty,
                             llvm_v8i16_ty], [IntrNoMem]>;
  def int_x86_avx512_psll_d_512 : ClangBuiltin<"__builtin_ia32_pslld512">,
      DefaultAttrsIntrinsic<[llvm_v16i32_ty], [llvm_v16i32_ty,
                             llvm_v4i32_ty], [IntrNoMem]>;
  def int_x86_avx512_psll_q_512 : ClangBuiltin<"__builtin_ia32_psllq512">,
      DefaultAttrsIntrinsic<[llvm_v8i64_ty], [llvm_v8i64_ty,
                             llvm_v2i64_ty], [IntrNoMem]>;
  def int_x86_avx512_psrl_w_512 : ClangBuiltin<"__builtin_ia32_psrlw512">,
      DefaultAttrsIntrinsic<[llvm_v32i16_ty], [llvm_v32i16_ty,
                             llvm_v8i16_ty], [IntrNoMem]>;
  def int_x86_avx512_psrl_d_512 : ClangBuiltin<"__builtin_ia32_psrld512">,
      DefaultAttrsIntrinsic<[llvm_v16i32_ty], [llvm_v16i32_ty,
                             llvm_v4i32_ty], [IntrNoMem]>;
  def int_x86_avx512_psrl_q_512 : ClangBuiltin<"__builtin_ia32_psrlq512">,
      DefaultAttrsIntrinsic<[llvm_v8i64_ty], [llvm_v8i64_ty,
                             llvm_v2i64_ty], [IntrNoMem]>;
  def int_x86_avx512_psra_w_512 : ClangBuiltin<"__builtin_ia32_psraw512">,
      DefaultAttrsIntrinsic<[llvm_v32i16_ty], [llvm_v32i16_ty,
                             llvm_v8i16_ty], [IntrNoMem]>;
  def int_x86_avx512_psra_d_512 : ClangBuiltin<"__builtin_ia32_psrad512">,
      DefaultAttrsIntrinsic<[llvm_v16i32_ty], [llvm_v16i32_ty,
                             llvm_v4i32_ty], [IntrNoMem]>;
  def int_x86_avx512_psra_q_512 : ClangBuiltin<"__builtin_ia32_psraq512">,
      DefaultAttrsIntrinsic<[llvm_v8i64_ty], [llvm_v8i64_ty,
                             llvm_v2i64_ty], [IntrNoMem]>;

  // Oddly these don't require an immediate due to a gcc compatibility issue.
  def int_x86_avx512_pslli_w_512 : ClangBuiltin<"__builtin_ia32_psllwi512">,
      DefaultAttrsIntrinsic<[llvm_v32i16_ty], [llvm_v32i16_ty,
                             llvm_i32_ty], [IntrNoMem]>;
  def int_x86_avx512_pslli_d_512 : ClangBuiltin<"__builtin_ia32_pslldi512">,
      DefaultAttrsIntrinsic<[llvm_v16i32_ty], [llvm_v16i32_ty,
                             llvm_i32_ty], [IntrNoMem]>;
  def int_x86_avx512_pslli_q_512 : ClangBuiltin<"__builtin_ia32_psllqi512">,
      DefaultAttrsIntrinsic<[llvm_v8i64_ty], [llvm_v8i64_ty,
                             llvm_i32_ty], [IntrNoMem]>;
  def int_x86_avx512_psrli_w_512 : ClangBuiltin<"__builtin_ia32_psrlwi512">,
      DefaultAttrsIntrinsic<[llvm_v32i16_ty], [llvm_v32i16_ty,
                             llvm_i32_ty], [IntrNoMem]>;
  def int_x86_avx512_psrli_d_512 : ClangBuiltin<"__builtin_ia32_psrldi512">,
      DefaultAttrsIntrinsic<[llvm_v16i32_ty], [llvm_v16i32_ty,
                             llvm_i32_ty], [IntrNoMem]>;
  def int_x86_avx512_psrli_q_512 : ClangBuiltin<"__builtin_ia32_psrlqi512">,
      DefaultAttrsIntrinsic<[llvm_v8i64_ty], [llvm_v8i64_ty,
                             llvm_i32_ty], [IntrNoMem]>;
  def int_x86_avx512_psrai_w_512 : ClangBuiltin<"__builtin_ia32_psrawi512">,
      DefaultAttrsIntrinsic<[llvm_v32i16_ty], [llvm_v32i16_ty,
                             llvm_i32_ty], [IntrNoMem]>;
  def int_x86_avx512_psrai_d_512 : ClangBuiltin<"__builtin_ia32_psradi512">,
      DefaultAttrsIntrinsic<[llvm_v16i32_ty], [llvm_v16i32_ty,
                             llvm_i32_ty], [IntrNoMem]>;
  def int_x86_avx512_psrai_q_512 : ClangBuiltin<"__builtin_ia32_psraqi512">,
      DefaultAttrsIntrinsic<[llvm_v8i64_ty], [llvm_v8i64_ty,
                             llvm_i32_ty], [IntrNoMem]>;

  def int_x86_avx512_pmultishift_qb_128:
        ClangBuiltin<"__builtin_ia32_vpmultishiftqb128">,
        DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty],
                              [IntrNoMem]>;
  def int_x86_avx512_pmultishift_qb_256:
        ClangBuiltin<"__builtin_ia32_vpmultishiftqb256">,
        DefaultAttrsIntrinsic<[llvm_v32i8_ty], [llvm_v32i8_ty, llvm_v32i8_ty],
                              [IntrNoMem]>;
  def int_x86_avx512_pmultishift_qb_512:
        ClangBuiltin<"__builtin_ia32_vpmultishiftqb512">,
        DefaultAttrsIntrinsic<[llvm_v64i8_ty], [llvm_v64i8_ty, llvm_v64i8_ty],
                              [IntrNoMem]>;
}

// Pack ops.
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_avx2_packsswb : ClangBuiltin<"__builtin_ia32_packsswb256">,
      DefaultAttrsIntrinsic<[llvm_v32i8_ty], [llvm_v16i16_ty,
                             llvm_v16i16_ty], [IntrNoMem]>;
  def int_x86_avx2_packssdw : ClangBuiltin<"__builtin_ia32_packssdw256">,
      DefaultAttrsIntrinsic<[llvm_v16i16_ty], [llvm_v8i32_ty,
                             llvm_v8i32_ty], [IntrNoMem]>;
  def int_x86_avx2_packuswb : ClangBuiltin<"__builtin_ia32_packuswb256">,
      DefaultAttrsIntrinsic<[llvm_v32i8_ty], [llvm_v16i16_ty,
                             llvm_v16i16_ty], [IntrNoMem]>;
  def int_x86_avx2_packusdw : ClangBuiltin<"__builtin_ia32_packusdw256">,
      DefaultAttrsIntrinsic<[llvm_v16i16_ty], [llvm_v8i32_ty,
                             llvm_v8i32_ty], [IntrNoMem]>;
}

// Horizontal arithmetic ops
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_avx2_phadd_w : ClangBuiltin<"__builtin_ia32_phaddw256">,
      DefaultAttrsIntrinsic<[llvm_v16i16_ty], [llvm_v16i16_ty,
                             llvm_v16i16_ty], [IntrNoMem]>;
  def int_x86_avx2_phadd_d : ClangBuiltin<"__builtin_ia32_phaddd256">,
      DefaultAttrsIntrinsic<[llvm_v8i32_ty], [llvm_v8i32_ty,
                             llvm_v8i32_ty], [IntrNoMem]>;
  def int_x86_avx2_phadd_sw : ClangBuiltin<"__builtin_ia32_phaddsw256">,
      DefaultAttrsIntrinsic<[llvm_v16i16_ty], [llvm_v16i16_ty,
                             llvm_v16i16_ty], [IntrNoMem]>;
  def int_x86_avx2_phsub_w : ClangBuiltin<"__builtin_ia32_phsubw256">,
      DefaultAttrsIntrinsic<[llvm_v16i16_ty], [llvm_v16i16_ty,
                             llvm_v16i16_ty], [IntrNoMem]>;
  def int_x86_avx2_phsub_d : ClangBuiltin<"__builtin_ia32_phsubd256">,
      DefaultAttrsIntrinsic<[llvm_v8i32_ty], [llvm_v8i32_ty,
                             llvm_v8i32_ty], [IntrNoMem]>;
  def int_x86_avx2_phsub_sw : ClangBuiltin<"__builtin_ia32_phsubsw256">,
      DefaultAttrsIntrinsic<[llvm_v16i16_ty], [llvm_v16i16_ty,
                             llvm_v16i16_ty], [IntrNoMem]>;
  def int_x86_avx2_pmadd_ub_sw : ClangBuiltin<"__builtin_ia32_pmaddubsw256">,
      DefaultAttrsIntrinsic<[llvm_v16i16_ty], [llvm_v32i8_ty,
                             llvm_v32i8_ty], [IntrNoMem]>;
}

// Sign ops
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_avx2_psign_b : ClangBuiltin<"__builtin_ia32_psignb256">,
      DefaultAttrsIntrinsic<[llvm_v32i8_ty], [llvm_v32i8_ty,
                             llvm_v32i8_ty], [IntrNoMem]>;
  def int_x86_avx2_psign_w : ClangBuiltin<"__builtin_ia32_psignw256">,
      DefaultAttrsIntrinsic<[llvm_v16i16_ty], [llvm_v16i16_ty,
                             llvm_v16i16_ty], [IntrNoMem]>;
  def int_x86_avx2_psign_d : ClangBuiltin<"__builtin_ia32_psignd256">,
      DefaultAttrsIntrinsic<[llvm_v8i32_ty], [llvm_v8i32_ty,
                             llvm_v8i32_ty], [IntrNoMem]>;
}

// Packed multiply high with round and scale
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_avx2_pmul_hr_sw : ClangBuiltin<"__builtin_ia32_pmulhrsw256">,
      DefaultAttrsIntrinsic<[llvm_v16i16_ty], [llvm_v16i16_ty,
                             llvm_v16i16_ty], [IntrNoMem, Commutative]>;
  def int_x86_avx512_pmul_hr_sw_512 : ClangBuiltin<"__builtin_ia32_pmulhrsw512">,
      DefaultAttrsIntrinsic<[llvm_v32i16_ty], [llvm_v32i16_ty,
                             llvm_v32i16_ty], [IntrNoMem, Commutative]>;
}

// Vector blend
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_avx2_pblendvb : ClangBuiltin<"__builtin_ia32_pblendvb256">,
      DefaultAttrsIntrinsic<[llvm_v32i8_ty], [llvm_v32i8_ty, llvm_v32i8_ty,
                             llvm_v32i8_ty], [IntrNoMem]>;
}


// Vector permutation
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_avx2_permd : ClangBuiltin<"__builtin_ia32_permvarsi256">,
      DefaultAttrsIntrinsic<[llvm_v8i32_ty], [llvm_v8i32_ty, llvm_v8i32_ty],
                            [IntrNoMem]>;
  def int_x86_avx2_permps : ClangBuiltin<"__builtin_ia32_permvarsf256">,
      DefaultAttrsIntrinsic<[llvm_v8f32_ty], [llvm_v8f32_ty, llvm_v8i32_ty],
                            [IntrNoMem]>;
}

// Conditional load ops
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_avx2_maskload_d : ClangBuiltin<"__builtin_ia32_maskloadd">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_ptr_ty, llvm_v4i32_ty],
                            [IntrReadMem, IntrArgMemOnly]>;
  def int_x86_avx2_maskload_q : ClangBuiltin<"__builtin_ia32_maskloadq">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_ptr_ty, llvm_v2i64_ty],
                            [IntrReadMem, IntrArgMemOnly]>;
  def int_x86_avx2_maskload_d_256 : ClangBuiltin<"__builtin_ia32_maskloadd256">,
      DefaultAttrsIntrinsic<[llvm_v8i32_ty], [llvm_ptr_ty, llvm_v8i32_ty],
                            [IntrReadMem, IntrArgMemOnly]>;
  def int_x86_avx2_maskload_q_256 : ClangBuiltin<"__builtin_ia32_maskloadq256">,
      DefaultAttrsIntrinsic<[llvm_v4i64_ty], [llvm_ptr_ty, llvm_v4i64_ty],
                            [IntrReadMem, IntrArgMemOnly]>;
}

// Conditional store ops
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_avx2_maskstore_d : ClangBuiltin<"__builtin_ia32_maskstored">,
        Intrinsic<[], [llvm_ptr_ty, llvm_v4i32_ty, llvm_v4i32_ty],
                  [IntrArgMemOnly]>;
  def int_x86_avx2_maskstore_q : ClangBuiltin<"__builtin_ia32_maskstoreq">,
        Intrinsic<[], [llvm_ptr_ty, llvm_v2i64_ty, llvm_v2i64_ty],
                  [IntrArgMemOnly]>;
  def int_x86_avx2_maskstore_d_256 :
        ClangBuiltin<"__builtin_ia32_maskstored256">,
        Intrinsic<[], [llvm_ptr_ty, llvm_v8i32_ty, llvm_v8i32_ty],
                  [IntrArgMemOnly]>;
  def int_x86_avx2_maskstore_q_256 :
        ClangBuiltin<"__builtin_ia32_maskstoreq256">,
        Intrinsic<[], [llvm_ptr_ty, llvm_v4i64_ty, llvm_v4i64_ty],
                  [IntrArgMemOnly]>;
}

// Variable bit shift ops
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_avx2_psllv_d : ClangBuiltin<"__builtin_ia32_psllv4si">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
                            [IntrNoMem]>;
  def int_x86_avx2_psllv_d_256 : ClangBuiltin<"__builtin_ia32_psllv8si">,
      DefaultAttrsIntrinsic<[llvm_v8i32_ty], [llvm_v8i32_ty, llvm_v8i32_ty],
                            [IntrNoMem]>;
  def int_x86_avx2_psllv_q : ClangBuiltin<"__builtin_ia32_psllv2di">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
                            [IntrNoMem]>;
  def int_x86_avx2_psllv_q_256 : ClangBuiltin<"__builtin_ia32_psllv4di">,
      DefaultAttrsIntrinsic<[llvm_v4i64_ty], [llvm_v4i64_ty, llvm_v4i64_ty],
                            [IntrNoMem]>;

  def int_x86_avx512_psllv_d_512 : ClangBuiltin<"__builtin_ia32_psllv16si">,
      DefaultAttrsIntrinsic<[llvm_v16i32_ty], [llvm_v16i32_ty, llvm_v16i32_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_psllv_q_512 : ClangBuiltin<"__builtin_ia32_psllv8di">,
      DefaultAttrsIntrinsic<[llvm_v8i64_ty], [llvm_v8i64_ty, llvm_v8i64_ty],
                            [IntrNoMem]>;

  def int_x86_avx2_psrlv_d : ClangBuiltin<"__builtin_ia32_psrlv4si">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
                            [IntrNoMem]>;
  def int_x86_avx2_psrlv_d_256 : ClangBuiltin<"__builtin_ia32_psrlv8si">,
      DefaultAttrsIntrinsic<[llvm_v8i32_ty], [llvm_v8i32_ty, llvm_v8i32_ty],
                            [IntrNoMem]>;
  def int_x86_avx2_psrlv_q : ClangBuiltin<"__builtin_ia32_psrlv2di">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
                            [IntrNoMem]>;
  def int_x86_avx2_psrlv_q_256 : ClangBuiltin<"__builtin_ia32_psrlv4di">,
      DefaultAttrsIntrinsic<[llvm_v4i64_ty], [llvm_v4i64_ty, llvm_v4i64_ty],
                            [IntrNoMem]>;

  def int_x86_avx512_psrlv_d_512 : ClangBuiltin<"__builtin_ia32_psrlv16si">,
      DefaultAttrsIntrinsic<[llvm_v16i32_ty], [llvm_v16i32_ty, llvm_v16i32_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_psrlv_q_512 : ClangBuiltin<"__builtin_ia32_psrlv8di">,
      DefaultAttrsIntrinsic<[llvm_v8i64_ty], [llvm_v8i64_ty, llvm_v8i64_ty],
                            [IntrNoMem]>;

  def int_x86_avx2_psrav_d : ClangBuiltin<"__builtin_ia32_psrav4si">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
                            [IntrNoMem]>;
  def int_x86_avx2_psrav_d_256 : ClangBuiltin<"__builtin_ia32_psrav8si">,
      DefaultAttrsIntrinsic<[llvm_v8i32_ty], [llvm_v8i32_ty, llvm_v8i32_ty],
                            [IntrNoMem]>;

  def int_x86_avx512_psrav_d_512 : ClangBuiltin<"__builtin_ia32_psrav16si">,
      DefaultAttrsIntrinsic<[llvm_v16i32_ty], [llvm_v16i32_ty, llvm_v16i32_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_psrav_q_128 : ClangBuiltin<"__builtin_ia32_psravq128">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_psrav_q_256 : ClangBuiltin<"__builtin_ia32_psravq256">,
      DefaultAttrsIntrinsic<[llvm_v4i64_ty], [llvm_v4i64_ty, llvm_v4i64_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_psrav_q_512 : ClangBuiltin<"__builtin_ia32_psrav8di">,
      DefaultAttrsIntrinsic<[llvm_v8i64_ty], [llvm_v8i64_ty, llvm_v8i64_ty],
                            [IntrNoMem]>;

  def int_x86_avx512_psllv_w_128 : ClangBuiltin<"__builtin_ia32_psllv8hi">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_psllv_w_256 : ClangBuiltin<"__builtin_ia32_psllv16hi">,
      DefaultAttrsIntrinsic<[llvm_v16i16_ty], [llvm_v16i16_ty, llvm_v16i16_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_psllv_w_512 : ClangBuiltin<"__builtin_ia32_psllv32hi">,
      DefaultAttrsIntrinsic<[llvm_v32i16_ty], [llvm_v32i16_ty, llvm_v32i16_ty],
                            [IntrNoMem]>;

  def int_x86_avx512_psrlv_w_128 : ClangBuiltin<"__builtin_ia32_psrlv8hi">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_psrlv_w_256 : ClangBuiltin<"__builtin_ia32_psrlv16hi">,
      DefaultAttrsIntrinsic<[llvm_v16i16_ty], [llvm_v16i16_ty, llvm_v16i16_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_psrlv_w_512 : ClangBuiltin<"__builtin_ia32_psrlv32hi">,
      DefaultAttrsIntrinsic<[llvm_v32i16_ty], [llvm_v32i16_ty, llvm_v32i16_ty],
                            [IntrNoMem]>;

  def int_x86_avx512_psrav_w_128 : ClangBuiltin<"__builtin_ia32_psrav8hi">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_psrav_w_256 : ClangBuiltin<"__builtin_ia32_psrav16hi">,
      DefaultAttrsIntrinsic<[llvm_v16i16_ty], [llvm_v16i16_ty, llvm_v16i16_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_psrav_w_512 : ClangBuiltin<"__builtin_ia32_psrav32hi">,
      DefaultAttrsIntrinsic<[llvm_v32i16_ty], [llvm_v32i16_ty, llvm_v32i16_ty],
                            [IntrNoMem]>;
}

// Gather ops
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  // NOTE: These can't be ArgMemOnly because you can put the address completely
  // in the index register.
  def int_x86_avx2_gather_d_pd : ClangBuiltin<"__builtin_ia32_gatherd_pd">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty],
        [llvm_v2f64_ty, llvm_ptr_ty, llvm_v4i32_ty, llvm_v2f64_ty, llvm_i8_ty],
        [IntrReadMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx2_gather_d_pd_256 : ClangBuiltin<"__builtin_ia32_gatherd_pd256">,
      DefaultAttrsIntrinsic<[llvm_v4f64_ty],
        [llvm_v4f64_ty, llvm_ptr_ty, llvm_v4i32_ty, llvm_v4f64_ty, llvm_i8_ty],
        [IntrReadMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx2_gather_q_pd : ClangBuiltin<"__builtin_ia32_gatherq_pd">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty],
        [llvm_v2f64_ty, llvm_ptr_ty, llvm_v2i64_ty, llvm_v2f64_ty, llvm_i8_ty],
        [IntrReadMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx2_gather_q_pd_256 : ClangBuiltin<"__builtin_ia32_gatherq_pd256">,
      DefaultAttrsIntrinsic<[llvm_v4f64_ty],
        [llvm_v4f64_ty, llvm_ptr_ty, llvm_v4i64_ty, llvm_v4f64_ty, llvm_i8_ty],
        [IntrReadMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx2_gather_d_ps : ClangBuiltin<"__builtin_ia32_gatherd_ps">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
        [llvm_v4f32_ty, llvm_ptr_ty, llvm_v4i32_ty, llvm_v4f32_ty, llvm_i8_ty],
        [IntrReadMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx2_gather_d_ps_256 : ClangBuiltin<"__builtin_ia32_gatherd_ps256">,
      DefaultAttrsIntrinsic<[llvm_v8f32_ty],
        [llvm_v8f32_ty, llvm_ptr_ty, llvm_v8i32_ty, llvm_v8f32_ty, llvm_i8_ty],
        [IntrReadMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx2_gather_q_ps : ClangBuiltin<"__builtin_ia32_gatherq_ps">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
        [llvm_v4f32_ty, llvm_ptr_ty, llvm_v2i64_ty, llvm_v4f32_ty, llvm_i8_ty],
        [IntrReadMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx2_gather_q_ps_256 : ClangBuiltin<"__builtin_ia32_gatherq_ps256">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
        [llvm_v4f32_ty, llvm_ptr_ty, llvm_v4i64_ty, llvm_v4f32_ty, llvm_i8_ty],
        [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx2_gather_d_q : ClangBuiltin<"__builtin_ia32_gatherd_q">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty],
        [llvm_v2i64_ty, llvm_ptr_ty, llvm_v4i32_ty, llvm_v2i64_ty, llvm_i8_ty],
        [IntrReadMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx2_gather_d_q_256 : ClangBuiltin<"__builtin_ia32_gatherd_q256">,
      DefaultAttrsIntrinsic<[llvm_v4i64_ty],
        [llvm_v4i64_ty, llvm_ptr_ty, llvm_v4i32_ty, llvm_v4i64_ty, llvm_i8_ty],
        [IntrReadMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx2_gather_q_q : ClangBuiltin<"__builtin_ia32_gatherq_q">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty],
        [llvm_v2i64_ty, llvm_ptr_ty, llvm_v2i64_ty, llvm_v2i64_ty, llvm_i8_ty],
        [IntrReadMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx2_gather_q_q_256 : ClangBuiltin<"__builtin_ia32_gatherq_q256">,
      DefaultAttrsIntrinsic<[llvm_v4i64_ty],
        [llvm_v4i64_ty, llvm_ptr_ty, llvm_v4i64_ty, llvm_v4i64_ty, llvm_i8_ty],
        [IntrReadMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx2_gather_d_d : ClangBuiltin<"__builtin_ia32_gatherd_d">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty],
        [llvm_v4i32_ty, llvm_ptr_ty, llvm_v4i32_ty, llvm_v4i32_ty, llvm_i8_ty],
        [IntrReadMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx2_gather_d_d_256 : ClangBuiltin<"__builtin_ia32_gatherd_d256">,
      DefaultAttrsIntrinsic<[llvm_v8i32_ty],
        [llvm_v8i32_ty, llvm_ptr_ty, llvm_v8i32_ty, llvm_v8i32_ty, llvm_i8_ty],
        [IntrReadMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx2_gather_q_d : ClangBuiltin<"__builtin_ia32_gatherq_d">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty],
        [llvm_v4i32_ty, llvm_ptr_ty, llvm_v2i64_ty, llvm_v4i32_ty, llvm_i8_ty],
        [IntrReadMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx2_gather_q_d_256 : ClangBuiltin<"__builtin_ia32_gatherq_d256">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty],
        [llvm_v4i32_ty, llvm_ptr_ty, llvm_v4i64_ty, llvm_v4i32_ty, llvm_i8_ty],
        [IntrReadMem, ImmArg<ArgIndex<4>>]>;
}

// Misc.
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_avx2_pmovmskb : ClangBuiltin<"__builtin_ia32_pmovmskb256">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v32i8_ty], [IntrNoMem]>;
  def int_x86_avx2_pshuf_b : ClangBuiltin<"__builtin_ia32_pshufb256">,
      DefaultAttrsIntrinsic<[llvm_v32i8_ty], [llvm_v32i8_ty,
                             llvm_v32i8_ty], [IntrNoMem]>;
  def int_x86_avx2_mpsadbw : ClangBuiltin<"__builtin_ia32_mpsadbw256">,
      DefaultAttrsIntrinsic<[llvm_v16i16_ty], [llvm_v32i8_ty, llvm_v32i8_ty,
                             llvm_i8_ty], [IntrNoMem, ImmArg<ArgIndex<2>>]>;
}

//===----------------------------------------------------------------------===//
// FMA3 and FMA4

let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_fma_vfmaddsub_ps : ClangBuiltin<"__builtin_ia32_vfmaddsubps">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                            [llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4f32_ty],
                            [IntrNoMem]>;
  def int_x86_fma_vfmaddsub_pd : ClangBuiltin<"__builtin_ia32_vfmaddsubpd">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty],
                            [llvm_v2f64_ty, llvm_v2f64_ty, llvm_v2f64_ty],
                            [IntrNoMem]>;
  def int_x86_fma_vfmaddsub_ps_256 :
      ClangBuiltin<"__builtin_ia32_vfmaddsubps256">,
      DefaultAttrsIntrinsic<[llvm_v8f32_ty],
                            [llvm_v8f32_ty, llvm_v8f32_ty, llvm_v8f32_ty],
                            [IntrNoMem]>;
  def int_x86_fma_vfmaddsub_pd_256 :
      ClangBuiltin<"__builtin_ia32_vfmaddsubpd256">,
      DefaultAttrsIntrinsic<[llvm_v4f64_ty],
                            [llvm_v4f64_ty, llvm_v4f64_ty, llvm_v4f64_ty],
                            [IntrNoMem]>;

  def int_x86_avx512_vfmadd_pd_512 :
      DefaultAttrsIntrinsic<[llvm_v8f64_ty],
          [llvm_v8f64_ty, llvm_v8f64_ty, llvm_v8f64_ty, llvm_i32_ty],
          [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  def int_x86_avx512_vfmadd_ps_512 :
      DefaultAttrsIntrinsic<[llvm_v16f32_ty],
          [llvm_v16f32_ty, llvm_v16f32_ty, llvm_v16f32_ty, llvm_i32_ty],
          [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  def int_x86_avx512_vfmaddsub_pd_512 :
      DefaultAttrsIntrinsic<[llvm_v8f64_ty],
          [llvm_v8f64_ty, llvm_v8f64_ty, llvm_v8f64_ty, llvm_i32_ty],
          [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  def int_x86_avx512_vfmaddsub_ps_512 :
      DefaultAttrsIntrinsic<[llvm_v16f32_ty],
          [llvm_v16f32_ty, llvm_v16f32_ty, llvm_v16f32_ty, llvm_i32_ty],
          [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  def int_x86_avx512_vfmadd_f64 :
      DefaultAttrsIntrinsic<[llvm_double_ty],
          [llvm_double_ty, llvm_double_ty, llvm_double_ty, llvm_i32_ty],
          [IntrNoMem, ImmArg<ArgIndex<3>>]>;
  def int_x86_avx512_vfmadd_f32 :
      DefaultAttrsIntrinsic<[llvm_float_ty],
          [llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_i32_ty],
          [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  def int_x86_avx512_vpmadd52h_uq_128 :
      ClangBuiltin<"__builtin_ia32_vpmadd52huq128">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty,
                             llvm_v2i64_ty], [IntrNoMem]>;
  def int_x86_avx512_vpmadd52l_uq_128 :
      ClangBuiltin<"__builtin_ia32_vpmadd52luq128">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty,
                             llvm_v2i64_ty], [IntrNoMem]>;
  def int_x86_avx512_vpmadd52h_uq_256 :
      ClangBuiltin<"__builtin_ia32_vpmadd52huq256">,
      DefaultAttrsIntrinsic<[llvm_v4i64_ty], [llvm_v4i64_ty, llvm_v4i64_ty,
                             llvm_v4i64_ty], [IntrNoMem]>;
  def int_x86_avx512_vpmadd52l_uq_256 :
      ClangBuiltin<"__builtin_ia32_vpmadd52luq256">,
      DefaultAttrsIntrinsic<[llvm_v4i64_ty], [llvm_v4i64_ty, llvm_v4i64_ty,
                             llvm_v4i64_ty], [IntrNoMem]>;
  def int_x86_avx512_vpmadd52h_uq_512 :
      ClangBuiltin<"__builtin_ia32_vpmadd52huq512">,
      DefaultAttrsIntrinsic<[llvm_v8i64_ty], [llvm_v8i64_ty, llvm_v8i64_ty,
                             llvm_v8i64_ty], [IntrNoMem]>;
  def int_x86_avx512_vpmadd52l_uq_512 :
      ClangBuiltin<"__builtin_ia32_vpmadd52luq512">,
      DefaultAttrsIntrinsic<[llvm_v8i64_ty], [llvm_v8i64_ty, llvm_v8i64_ty,
                             llvm_v8i64_ty], [IntrNoMem]>;
}

// VNNI
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_avx512_vpdpbusd_128 :
      ClangBuiltin<"__builtin_ia32_vpdpbusd128">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty,
                             llvm_v4i32_ty], [IntrNoMem]>;
  def int_x86_avx512_vpdpbusd_256 :
      ClangBuiltin<"__builtin_ia32_vpdpbusd256">,
      DefaultAttrsIntrinsic<[llvm_v8i32_ty], [llvm_v8i32_ty, llvm_v8i32_ty,
                             llvm_v8i32_ty], [IntrNoMem]>;
  def int_x86_avx512_vpdpbusd_512 :
      ClangBuiltin<"__builtin_ia32_vpdpbusd512">,
      DefaultAttrsIntrinsic<[llvm_v16i32_ty], [llvm_v16i32_ty, llvm_v16i32_ty,
                             llvm_v16i32_ty], [IntrNoMem]>;

  def int_x86_avx512_vpdpbusds_128 :
      ClangBuiltin<"__builtin_ia32_vpdpbusds128">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty,
                             llvm_v4i32_ty], [IntrNoMem]>;
  def int_x86_avx512_vpdpbusds_256 :
      ClangBuiltin<"__builtin_ia32_vpdpbusds256">,
      DefaultAttrsIntrinsic<[llvm_v8i32_ty], [llvm_v8i32_ty, llvm_v8i32_ty,
                             llvm_v8i32_ty], [IntrNoMem]>;
  def int_x86_avx512_vpdpbusds_512 :
      ClangBuiltin<"__builtin_ia32_vpdpbusds512">,
      DefaultAttrsIntrinsic<[llvm_v16i32_ty], [llvm_v16i32_ty, llvm_v16i32_ty,
                             llvm_v16i32_ty], [IntrNoMem]>;

  def int_x86_avx512_vpdpwssd_128 :
      ClangBuiltin<"__builtin_ia32_vpdpwssd128">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty,
                             llvm_v4i32_ty], [IntrNoMem]>;
  def int_x86_avx512_vpdpwssd_256 :
      ClangBuiltin<"__builtin_ia32_vpdpwssd256">,
      DefaultAttrsIntrinsic<[llvm_v8i32_ty], [llvm_v8i32_ty, llvm_v8i32_ty,
                             llvm_v8i32_ty], [IntrNoMem]>;
  def int_x86_avx512_vpdpwssd_512 :
      ClangBuiltin<"__builtin_ia32_vpdpwssd512">,
      DefaultAttrsIntrinsic<[llvm_v16i32_ty], [llvm_v16i32_ty, llvm_v16i32_ty,
                             llvm_v16i32_ty], [IntrNoMem]>;

  def int_x86_avx512_vpdpwssds_128 :
      ClangBuiltin<"__builtin_ia32_vpdpwssds128">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty,
                             llvm_v4i32_ty], [IntrNoMem]>;
  def int_x86_avx512_vpdpwssds_256 :
      ClangBuiltin<"__builtin_ia32_vpdpwssds256">,
      DefaultAttrsIntrinsic<[llvm_v8i32_ty], [llvm_v8i32_ty, llvm_v8i32_ty,
                             llvm_v8i32_ty], [IntrNoMem]>;
  def int_x86_avx512_vpdpwssds_512 :
      ClangBuiltin<"__builtin_ia32_vpdpwssds512">,
      DefaultAttrsIntrinsic<[llvm_v16i32_ty], [llvm_v16i32_ty, llvm_v16i32_ty,
                             llvm_v16i32_ty], [IntrNoMem]>;
  def int_x86_avx2_vpdpbssd_128
      : ClangBuiltin<"__builtin_ia32_vpdpbssd128">,
        DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                              [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
                              [IntrNoMem]>;
  def int_x86_avx2_vpdpbssd_256
      : ClangBuiltin<"__builtin_ia32_vpdpbssd256">,
        DefaultAttrsIntrinsic<[llvm_v8i32_ty],
                              [llvm_v8i32_ty, llvm_v8i32_ty, llvm_v8i32_ty],
                              [IntrNoMem]>;
  def int_x86_avx2_vpdpbssds_128
      : ClangBuiltin<"__builtin_ia32_vpdpbssds128">,
        DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                              [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
                              [IntrNoMem]>;
  def int_x86_avx2_vpdpbssds_256
      : ClangBuiltin<"__builtin_ia32_vpdpbssds256">,
        DefaultAttrsIntrinsic<[llvm_v8i32_ty],
                              [llvm_v8i32_ty, llvm_v8i32_ty, llvm_v8i32_ty],
                              [IntrNoMem]>;
  def int_x86_avx2_vpdpbsud_128
      : ClangBuiltin<"__builtin_ia32_vpdpbsud128">,
        DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                              [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
                              [IntrNoMem]>;
  def int_x86_avx2_vpdpbsud_256
      : ClangBuiltin<"__builtin_ia32_vpdpbsud256">,
        DefaultAttrsIntrinsic<[llvm_v8i32_ty],
                              [llvm_v8i32_ty, llvm_v8i32_ty, llvm_v8i32_ty],
                              [IntrNoMem]>;
  def int_x86_avx2_vpdpbsuds_128
      : ClangBuiltin<"__builtin_ia32_vpdpbsuds128">,
        DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                              [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
                              [IntrNoMem]>;
  def int_x86_avx2_vpdpbsuds_256
      : ClangBuiltin<"__builtin_ia32_vpdpbsuds256">,
        DefaultAttrsIntrinsic<[llvm_v8i32_ty],
                              [llvm_v8i32_ty, llvm_v8i32_ty, llvm_v8i32_ty],
                              [IntrNoMem]>;
  def int_x86_avx2_vpdpbuud_128
      : ClangBuiltin<"__builtin_ia32_vpdpbuud128">,
        DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                              [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
                              [IntrNoMem]>;
  def int_x86_avx2_vpdpbuud_256
      : ClangBuiltin<"__builtin_ia32_vpdpbuud256">,
        DefaultAttrsIntrinsic<[llvm_v8i32_ty],
                              [llvm_v8i32_ty, llvm_v8i32_ty, llvm_v8i32_ty],
                              [IntrNoMem]>;
  def int_x86_avx2_vpdpbuuds_128
      : ClangBuiltin<"__builtin_ia32_vpdpbuuds128">,
        DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                              [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
                              [IntrNoMem]>;
  def int_x86_avx2_vpdpbuuds_256
      : ClangBuiltin<"__builtin_ia32_vpdpbuuds256">,
        DefaultAttrsIntrinsic<[llvm_v8i32_ty],
                              [llvm_v8i32_ty, llvm_v8i32_ty, llvm_v8i32_ty],
                              [IntrNoMem]>;

  def int_x86_avx2_vpdpwsud_128
      : ClangBuiltin<"__builtin_ia32_vpdpwsud128">,
        DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                              [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
                              [IntrNoMem]>;
  def int_x86_avx2_vpdpwsud_256
      : ClangBuiltin<"__builtin_ia32_vpdpwsud256">,
        DefaultAttrsIntrinsic<[llvm_v8i32_ty],
                              [llvm_v8i32_ty, llvm_v8i32_ty, llvm_v8i32_ty],
                              [IntrNoMem]>;
  def int_x86_avx2_vpdpwsuds_128
      : ClangBuiltin<"__builtin_ia32_vpdpwsuds128">,
        DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                              [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
                              [IntrNoMem]>;
  def int_x86_avx2_vpdpwsuds_256
      : ClangBuiltin<"__builtin_ia32_vpdpwsuds256">,
        DefaultAttrsIntrinsic<[llvm_v8i32_ty],
                              [llvm_v8i32_ty, llvm_v8i32_ty, llvm_v8i32_ty],
                              [IntrNoMem]>;
  def int_x86_avx2_vpdpwusd_128
      : ClangBuiltin<"__builtin_ia32_vpdpwusd128">,
        DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                              [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
                              [IntrNoMem]>;
  def int_x86_avx2_vpdpwusd_256
      : ClangBuiltin<"__builtin_ia32_vpdpwusd256">,
        DefaultAttrsIntrinsic<[llvm_v8i32_ty],
                              [llvm_v8i32_ty, llvm_v8i32_ty, llvm_v8i32_ty],
                              [IntrNoMem]>;
  def int_x86_avx2_vpdpwusds_128
      : ClangBuiltin<"__builtin_ia32_vpdpwusds128">,
        DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                              [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
                              [IntrNoMem]>;
  def int_x86_avx2_vpdpwusds_256
      : ClangBuiltin<"__builtin_ia32_vpdpwusds256">,
        DefaultAttrsIntrinsic<[llvm_v8i32_ty],
                              [llvm_v8i32_ty, llvm_v8i32_ty, llvm_v8i32_ty],
                              [IntrNoMem]>;
  def int_x86_avx2_vpdpwuud_128
      : ClangBuiltin<"__builtin_ia32_vpdpwuud128">,
        DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                              [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
                              [IntrNoMem]>;
  def int_x86_avx2_vpdpwuud_256
      : ClangBuiltin<"__builtin_ia32_vpdpwuud256">,
        DefaultAttrsIntrinsic<[llvm_v8i32_ty],
                              [llvm_v8i32_ty, llvm_v8i32_ty, llvm_v8i32_ty],
                              [IntrNoMem]>;
  def int_x86_avx2_vpdpwuuds_128
      : ClangBuiltin<"__builtin_ia32_vpdpwuuds128">,
        DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                              [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
                              [IntrNoMem]>;
  def int_x86_avx2_vpdpwuuds_256
      : ClangBuiltin<"__builtin_ia32_vpdpwuuds256">,
        DefaultAttrsIntrinsic<[llvm_v8i32_ty],
                              [llvm_v8i32_ty, llvm_v8i32_ty, llvm_v8i32_ty],
                              [IntrNoMem]>;
}

//===----------------------------------------------------------------------===//
// XOP

let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_xop_vpermil2pd : ClangBuiltin<"__builtin_ia32_vpermil2pd">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty, llvm_v2f64_ty,
                                              llvm_v2i64_ty, llvm_i8_ty],
                            [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  def int_x86_xop_vpermil2pd_256 :
      ClangBuiltin<"__builtin_ia32_vpermil2pd256">,
      DefaultAttrsIntrinsic<[llvm_v4f64_ty], [llvm_v4f64_ty, llvm_v4f64_ty,
                                              llvm_v4i64_ty, llvm_i8_ty],
                            [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  def int_x86_xop_vpermil2ps : ClangBuiltin<"__builtin_ia32_vpermil2ps">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty, llvm_v4f32_ty,
                                              llvm_v4i32_ty, llvm_i8_ty],
                            [IntrNoMem, ImmArg<ArgIndex<3>>]>;
  def int_x86_xop_vpermil2ps_256 :
      ClangBuiltin<"__builtin_ia32_vpermil2ps256">,
      DefaultAttrsIntrinsic<[llvm_v8f32_ty], [llvm_v8f32_ty, llvm_v8f32_ty,
                                              llvm_v8i32_ty, llvm_i8_ty],
                            [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  def int_x86_xop_vfrcz_pd : ClangBuiltin<"__builtin_ia32_vfrczpd">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty], [IntrNoMem]>;
  def int_x86_xop_vfrcz_ps : ClangBuiltin<"__builtin_ia32_vfrczps">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty], [IntrNoMem]>;
  def int_x86_xop_vfrcz_sd : ClangBuiltin<"__builtin_ia32_vfrczsd">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty], [IntrNoMem]>;
  def int_x86_xop_vfrcz_ss : ClangBuiltin<"__builtin_ia32_vfrczss">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty], [IntrNoMem]>;
  def int_x86_xop_vfrcz_pd_256 : ClangBuiltin<"__builtin_ia32_vfrczpd256">,
      DefaultAttrsIntrinsic<[llvm_v4f64_ty], [llvm_v4f64_ty], [IntrNoMem]>;
  def int_x86_xop_vfrcz_ps_256 : ClangBuiltin<"__builtin_ia32_vfrczps256">,
      DefaultAttrsIntrinsic<[llvm_v8f32_ty], [llvm_v8f32_ty], [IntrNoMem]>;

  def int_x86_xop_vphaddbd :
      ClangBuiltin<"__builtin_ia32_vphaddbd">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v16i8_ty], [IntrNoMem]>;
  def int_x86_xop_vphaddbq :
      ClangBuiltin<"__builtin_ia32_vphaddbq">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v16i8_ty], [IntrNoMem]>;
  def int_x86_xop_vphaddbw :
      ClangBuiltin<"__builtin_ia32_vphaddbw">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v16i8_ty], [IntrNoMem]>;
  def int_x86_xop_vphadddq :
      ClangBuiltin<"__builtin_ia32_vphadddq">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v4i32_ty], [IntrNoMem]>;
  def int_x86_xop_vphaddubd :
      ClangBuiltin<"__builtin_ia32_vphaddubd">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v16i8_ty], [IntrNoMem]>;
  def int_x86_xop_vphaddubq :
      ClangBuiltin<"__builtin_ia32_vphaddubq">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v16i8_ty], [IntrNoMem]>;
  def int_x86_xop_vphaddubw :
      ClangBuiltin<"__builtin_ia32_vphaddubw">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v16i8_ty], [IntrNoMem]>;
  def int_x86_xop_vphaddudq :
      ClangBuiltin<"__builtin_ia32_vphaddudq">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v4i32_ty], [IntrNoMem]>;
  def int_x86_xop_vphadduwd :
      ClangBuiltin<"__builtin_ia32_vphadduwd">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v8i16_ty], [IntrNoMem]>;
  def int_x86_xop_vphadduwq :
      ClangBuiltin<"__builtin_ia32_vphadduwq">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v8i16_ty], [IntrNoMem]>;
  def int_x86_xop_vphaddwd :
      ClangBuiltin<"__builtin_ia32_vphaddwd">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v8i16_ty], [IntrNoMem]>;
  def int_x86_xop_vphaddwq :
      ClangBuiltin<"__builtin_ia32_vphaddwq">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v8i16_ty], [IntrNoMem]>;
  def int_x86_xop_vphsubbw :
      ClangBuiltin<"__builtin_ia32_vphsubbw">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v16i8_ty], [IntrNoMem]>;
  def int_x86_xop_vphsubdq :
      ClangBuiltin<"__builtin_ia32_vphsubdq">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v4i32_ty], [IntrNoMem]>;
  def int_x86_xop_vphsubwd :
      ClangBuiltin<"__builtin_ia32_vphsubwd">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v8i16_ty], [IntrNoMem]>;
  def int_x86_xop_vpmacsdd :
      ClangBuiltin<"__builtin_ia32_vpmacsdd">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                            [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
                            [IntrNoMem, Commutative]>;
  def int_x86_xop_vpmacsdqh :
      ClangBuiltin<"__builtin_ia32_vpmacsdqh">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty],
                            [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v2i64_ty],
                            [IntrNoMem, Commutative]>;
  def int_x86_xop_vpmacsdql :
      ClangBuiltin<"__builtin_ia32_vpmacsdql">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty],
                            [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v2i64_ty],
                            [IntrNoMem, Commutative]>;
  def int_x86_xop_vpmacssdd :
      ClangBuiltin<"__builtin_ia32_vpmacssdd">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                            [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
                            [IntrNoMem, Commutative]>;
  def int_x86_xop_vpmacssdqh :
      ClangBuiltin<"__builtin_ia32_vpmacssdqh">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty],
                            [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v2i64_ty],
                            [IntrNoMem, Commutative]>;
  def int_x86_xop_vpmacssdql :
      ClangBuiltin<"__builtin_ia32_vpmacssdql">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty],
                            [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v2i64_ty],
                            [IntrNoMem, Commutative]>;
  def int_x86_xop_vpmacsswd :
      ClangBuiltin<"__builtin_ia32_vpmacsswd">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                            [llvm_v8i16_ty, llvm_v8i16_ty, llvm_v4i32_ty],
                            [IntrNoMem, Commutative]>;
  def int_x86_xop_vpmacssww :
      ClangBuiltin<"__builtin_ia32_vpmacssww">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty],
                            [llvm_v8i16_ty, llvm_v8i16_ty, llvm_v8i16_ty],
                            [IntrNoMem, Commutative]>;
  def int_x86_xop_vpmacswd :
      ClangBuiltin<"__builtin_ia32_vpmacswd">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                            [llvm_v8i16_ty, llvm_v8i16_ty, llvm_v4i32_ty],
                            [IntrNoMem, Commutative]>;
  def int_x86_xop_vpmacsww :
      ClangBuiltin<"__builtin_ia32_vpmacsww">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty],
                            [llvm_v8i16_ty, llvm_v8i16_ty, llvm_v8i16_ty],
                            [IntrNoMem, Commutative]>;
  def int_x86_xop_vpmadcsswd :
      ClangBuiltin<"__builtin_ia32_vpmadcsswd">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                            [llvm_v8i16_ty, llvm_v8i16_ty, llvm_v4i32_ty],
                            [IntrNoMem, Commutative]>;
  def int_x86_xop_vpmadcswd :
      ClangBuiltin<"__builtin_ia32_vpmadcswd">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                            [llvm_v8i16_ty, llvm_v8i16_ty, llvm_v4i32_ty],
                            [IntrNoMem, Commutative]>;
  def int_x86_xop_vpperm :
      ClangBuiltin<"__builtin_ia32_vpperm">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                            [llvm_v16i8_ty, llvm_v16i8_ty, llvm_v16i8_ty],
                            [IntrNoMem]>;
  def int_x86_xop_vpshab :
      ClangBuiltin<"__builtin_ia32_vpshab">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty],
                            [IntrNoMem]>;
  def int_x86_xop_vpshad :
      ClangBuiltin<"__builtin_ia32_vpshad">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
                            [IntrNoMem]>;
  def int_x86_xop_vpshaq :
      ClangBuiltin<"__builtin_ia32_vpshaq">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
                            [IntrNoMem]>;
  def int_x86_xop_vpshaw :
      ClangBuiltin<"__builtin_ia32_vpshaw">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty],
                            [IntrNoMem]>;
  def int_x86_xop_vpshlb :
      ClangBuiltin<"__builtin_ia32_vpshlb">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty],
                            [IntrNoMem]>;
  def int_x86_xop_vpshld :
      ClangBuiltin<"__builtin_ia32_vpshld">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
                            [IntrNoMem]>;
  def int_x86_xop_vpshlq :
      ClangBuiltin<"__builtin_ia32_vpshlq">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
                            [IntrNoMem]>;
  def int_x86_xop_vpshlw :
      ClangBuiltin<"__builtin_ia32_vpshlw">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty],
                            [IntrNoMem]>;
}

//===----------------------------------------------------------------------===//
// LWP
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_llwpcb :
              ClangBuiltin<"__builtin_ia32_llwpcb">,
              Intrinsic<[], [llvm_ptr_ty], []>;
  def int_x86_slwpcb :
              ClangBuiltin<"__builtin_ia32_slwpcb">,
              Intrinsic<[llvm_ptr_ty], [], []>;
  def int_x86_lwpins32 :
              ClangBuiltin<"__builtin_ia32_lwpins32">,
              Intrinsic<[llvm_i8_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
                        [ImmArg<ArgIndex<2>>]>;
  def int_x86_lwpins64 :
              ClangBuiltin<"__builtin_ia32_lwpins64">,
              Intrinsic<[llvm_i8_ty], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty],
                        [ImmArg<ArgIndex<2>>]>;
  def int_x86_lwpval32 :
              ClangBuiltin<"__builtin_ia32_lwpval32">,
              Intrinsic<[], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
                        [ImmArg<ArgIndex<2>>]>;
  def int_x86_lwpval64 :
              ClangBuiltin<"__builtin_ia32_lwpval64">,
              Intrinsic<[], [llvm_i64_ty, llvm_i32_ty, llvm_i32_ty],
                        [ImmArg<ArgIndex<2>>]>;
}

//===----------------------------------------------------------------------===//
// MMX

// Empty MMX state op.
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_mmx_emms  : ClangBuiltin<"__builtin_ia32_emms">,
              Intrinsic<[], [], []>;
}

// Integer arithmetic ops.
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  // Addition
  def int_x86_mmx_padd_b : ClangBuiltin<"__builtin_ia32_paddb">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem, Commutative]>;
  def int_x86_mmx_padd_w : ClangBuiltin<"__builtin_ia32_paddw">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem, Commutative]>;
  def int_x86_mmx_padd_d : ClangBuiltin<"__builtin_ia32_paddd">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem, Commutative]>;
  def int_x86_mmx_padd_q : ClangBuiltin<"__builtin_ia32_paddq">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem, Commutative]>;

  def int_x86_mmx_padds_b : ClangBuiltin<"__builtin_ia32_paddsb">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem, Commutative]>;
  def int_x86_mmx_padds_w : ClangBuiltin<"__builtin_ia32_paddsw">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem, Commutative]>;

  def int_x86_mmx_paddus_b : ClangBuiltin<"__builtin_ia32_paddusb">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem, Commutative]>;
  def int_x86_mmx_paddus_w : ClangBuiltin<"__builtin_ia32_paddusw">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem, Commutative]>;

  // Subtraction
  def int_x86_mmx_psub_b : ClangBuiltin<"__builtin_ia32_psubb">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem]>;
  def int_x86_mmx_psub_w : ClangBuiltin<"__builtin_ia32_psubw">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem]>;
  def int_x86_mmx_psub_d : ClangBuiltin<"__builtin_ia32_psubd">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem]>;
  def int_x86_mmx_psub_q : ClangBuiltin<"__builtin_ia32_psubq">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem]>;

  def int_x86_mmx_psubs_b : ClangBuiltin<"__builtin_ia32_psubsb">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem]>;
  def int_x86_mmx_psubs_w : ClangBuiltin<"__builtin_ia32_psubsw">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem]>;

  def int_x86_mmx_psubus_b : ClangBuiltin<"__builtin_ia32_psubusb">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem]>;
  def int_x86_mmx_psubus_w : ClangBuiltin<"__builtin_ia32_psubusw">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem]>;

  // Multiplication
  def int_x86_mmx_pmulh_w : ClangBuiltin<"__builtin_ia32_pmulhw">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem, Commutative]>;
  def int_x86_mmx_pmull_w : ClangBuiltin<"__builtin_ia32_pmullw">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem, Commutative]>;
  def int_x86_mmx_pmulhu_w : ClangBuiltin<"__builtin_ia32_pmulhuw">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem, Commutative]>;
  def int_x86_mmx_pmulu_dq : ClangBuiltin<"__builtin_ia32_pmuludq">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem, Commutative]>;
  def int_x86_mmx_pmadd_wd : ClangBuiltin<"__builtin_ia32_pmaddwd">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem, Commutative]>;

  // Bitwise operations
  def int_x86_mmx_pand : ClangBuiltin<"__builtin_ia32_pand">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem, Commutative]>;
  def int_x86_mmx_pandn : ClangBuiltin<"__builtin_ia32_pandn">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem]>;
  def int_x86_mmx_por : ClangBuiltin<"__builtin_ia32_por">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem, Commutative]>;
  def int_x86_mmx_pxor : ClangBuiltin<"__builtin_ia32_pxor">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem, Commutative]>;

  // Averages
  def int_x86_mmx_pavg_b : ClangBuiltin<"__builtin_ia32_pavgb">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem, Commutative]>;
  def int_x86_mmx_pavg_w : ClangBuiltin<"__builtin_ia32_pavgw">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem, Commutative]>;

  // Maximum
  def int_x86_mmx_pmaxu_b : ClangBuiltin<"__builtin_ia32_pmaxub">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem, Commutative]>;
  def int_x86_mmx_pmaxs_w : ClangBuiltin<"__builtin_ia32_pmaxsw">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem, Commutative]>;

  // Minimum
  def int_x86_mmx_pminu_b : ClangBuiltin<"__builtin_ia32_pminub">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem, Commutative]>;
  def int_x86_mmx_pmins_w : ClangBuiltin<"__builtin_ia32_pminsw">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem, Commutative]>;

  // Packed sum of absolute differences
  def int_x86_mmx_psad_bw : ClangBuiltin<"__builtin_ia32_psadbw">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem, Commutative]>;
}

// Integer shift ops.
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  // Shift left logical
  def int_x86_mmx_psll_w : ClangBuiltin<"__builtin_ia32_psllw">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem]>;
  def int_x86_mmx_psll_d : ClangBuiltin<"__builtin_ia32_pslld">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem]>;
  def int_x86_mmx_psll_q : ClangBuiltin<"__builtin_ia32_psllq">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem]>;

  def int_x86_mmx_psrl_w : ClangBuiltin<"__builtin_ia32_psrlw">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem]>;
  def int_x86_mmx_psrl_d : ClangBuiltin<"__builtin_ia32_psrld">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem]>;
  def int_x86_mmx_psrl_q : ClangBuiltin<"__builtin_ia32_psrlq">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem]>;

  def int_x86_mmx_psra_w : ClangBuiltin<"__builtin_ia32_psraw">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem]>;
  def int_x86_mmx_psra_d : ClangBuiltin<"__builtin_ia32_psrad">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem]>;

  // Oddly these don't require an immediate due to a gcc compatibility issue.
  def int_x86_mmx_pslli_w : ClangBuiltin<"__builtin_ia32_psllwi">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_i32_ty],
                            [IntrNoMem]>;
  def int_x86_mmx_pslli_d : ClangBuiltin<"__builtin_ia32_pslldi">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_i32_ty],
                            [IntrNoMem]>;
  def int_x86_mmx_pslli_q : ClangBuiltin<"__builtin_ia32_psllqi">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_i32_ty],
                            [IntrNoMem]>;

  def int_x86_mmx_psrli_w : ClangBuiltin<"__builtin_ia32_psrlwi">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_i32_ty],
                            [IntrNoMem]>;
  def int_x86_mmx_psrli_d : ClangBuiltin<"__builtin_ia32_psrldi">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_i32_ty],
                            [IntrNoMem]>;
  def int_x86_mmx_psrli_q : ClangBuiltin<"__builtin_ia32_psrlqi">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_i32_ty],
                            [IntrNoMem]>;

  def int_x86_mmx_psrai_w : ClangBuiltin<"__builtin_ia32_psrawi">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_i32_ty],
                            [IntrNoMem]>;
  def int_x86_mmx_psrai_d : ClangBuiltin<"__builtin_ia32_psradi">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_i32_ty],
                            [IntrNoMem]>;
}
// Permute
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_avx512_permvar_df_256 : ClangBuiltin<"__builtin_ia32_permvardf256">,
      DefaultAttrsIntrinsic<[llvm_v4f64_ty], [llvm_v4f64_ty, llvm_v4i64_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_permvar_df_512 : ClangBuiltin<"__builtin_ia32_permvardf512">,
      DefaultAttrsIntrinsic<[llvm_v8f64_ty], [llvm_v8f64_ty, llvm_v8i64_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_permvar_di_256 : ClangBuiltin<"__builtin_ia32_permvardi256">,
      DefaultAttrsIntrinsic<[llvm_v4i64_ty], [llvm_v4i64_ty, llvm_v4i64_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_permvar_di_512 : ClangBuiltin<"__builtin_ia32_permvardi512">,
      DefaultAttrsIntrinsic<[llvm_v8i64_ty], [llvm_v8i64_ty, llvm_v8i64_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_permvar_hi_128 : ClangBuiltin<"__builtin_ia32_permvarhi128">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_permvar_hi_256 : ClangBuiltin<"__builtin_ia32_permvarhi256">,
      DefaultAttrsIntrinsic<[llvm_v16i16_ty], [llvm_v16i16_ty, llvm_v16i16_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_permvar_hi_512 : ClangBuiltin<"__builtin_ia32_permvarhi512">,
      DefaultAttrsIntrinsic<[llvm_v32i16_ty], [llvm_v32i16_ty, llvm_v32i16_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_permvar_qi_128 : ClangBuiltin<"__builtin_ia32_permvarqi128">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_permvar_qi_256 : ClangBuiltin<"__builtin_ia32_permvarqi256">,
      DefaultAttrsIntrinsic<[llvm_v32i8_ty], [llvm_v32i8_ty, llvm_v32i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_permvar_qi_512 : ClangBuiltin<"__builtin_ia32_permvarqi512">,
      DefaultAttrsIntrinsic<[llvm_v64i8_ty], [llvm_v64i8_ty, llvm_v64i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_permvar_sf_512 : ClangBuiltin<"__builtin_ia32_permvarsf512">,
      DefaultAttrsIntrinsic<[llvm_v16f32_ty], [llvm_v16f32_ty, llvm_v16i32_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_permvar_si_512 : ClangBuiltin<"__builtin_ia32_permvarsi512">,
      DefaultAttrsIntrinsic<[llvm_v16i32_ty], [llvm_v16i32_ty, llvm_v16i32_ty],
                            [IntrNoMem]>;
}
// Pack ops.
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_mmx_packsswb : ClangBuiltin<"__builtin_ia32_packsswb">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem]>;
  def int_x86_mmx_packssdw : ClangBuiltin<"__builtin_ia32_packssdw">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem]>;
  def int_x86_mmx_packuswb : ClangBuiltin<"__builtin_ia32_packuswb">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem]>;
}

// Unpacking ops.
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_mmx_punpckhbw : ClangBuiltin<"__builtin_ia32_punpckhbw">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem]>;
  def int_x86_mmx_punpckhwd : ClangBuiltin<"__builtin_ia32_punpckhwd">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem]>;
  def int_x86_mmx_punpckhdq : ClangBuiltin<"__builtin_ia32_punpckhdq">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem]>;
  def int_x86_mmx_punpcklbw : ClangBuiltin<"__builtin_ia32_punpcklbw">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem]>;
  def int_x86_mmx_punpcklwd : ClangBuiltin<"__builtin_ia32_punpcklwd">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem]>;
  def int_x86_mmx_punpckldq : ClangBuiltin<"__builtin_ia32_punpckldq">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem]>;
}

// Integer comparison ops
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_mmx_pcmpeq_b : ClangBuiltin<"__builtin_ia32_pcmpeqb">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem, Commutative]>;
  def int_x86_mmx_pcmpeq_w : ClangBuiltin<"__builtin_ia32_pcmpeqw">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem, Commutative]>;
  def int_x86_mmx_pcmpeq_d : ClangBuiltin<"__builtin_ia32_pcmpeqd">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem, Commutative]>;

  def int_x86_mmx_pcmpgt_b : ClangBuiltin<"__builtin_ia32_pcmpgtb">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem]>;
  def int_x86_mmx_pcmpgt_w : ClangBuiltin<"__builtin_ia32_pcmpgtw">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem]>;
  def int_x86_mmx_pcmpgt_d : ClangBuiltin<"__builtin_ia32_pcmpgtd">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty], [llvm_x86mmx_ty, llvm_x86mmx_ty],
                            [IntrNoMem]>;
}

// Misc.
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_mmx_maskmovq : ClangBuiltin<"__builtin_ia32_maskmovq">,
              Intrinsic<[], [llvm_x86mmx_ty, llvm_x86mmx_ty, llvm_ptr_ty], []>;

  def int_x86_mmx_pmovmskb : ClangBuiltin<"__builtin_ia32_pmovmskb">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_x86mmx_ty], [IntrNoMem]>;

  def int_x86_mmx_movnt_dq : ClangBuiltin<"__builtin_ia32_movntq">,
              Intrinsic<[], [llvm_ptr_ty, llvm_x86mmx_ty], []>;

  def int_x86_mmx_palignr_b : ClangBuiltin<"__builtin_ia32_palignr">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty],
                            [llvm_x86mmx_ty, llvm_x86mmx_ty, llvm_i8_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>]>;

  def int_x86_mmx_pextr_w : ClangBuiltin<"__builtin_ia32_vec_ext_v4hi">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_x86mmx_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;

  def int_x86_mmx_pinsr_w : ClangBuiltin<"__builtin_ia32_vec_set_v4hi">,
      DefaultAttrsIntrinsic<[llvm_x86mmx_ty],
                            [llvm_x86mmx_ty, llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
}

//===----------------------------------------------------------------------===//
// BMI

let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_bmi_bextr_32 : ClangBuiltin<"__builtin_ia32_bextr_u32">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem]>;
  def int_x86_bmi_bextr_64 : ClangBuiltin<"__builtin_ia32_bextr_u64">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty],
                            [IntrNoMem]>;
  def int_x86_bmi_bzhi_32 : ClangBuiltin<"__builtin_ia32_bzhi_si">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem]>;
  def int_x86_bmi_bzhi_64 : ClangBuiltin<"__builtin_ia32_bzhi_di">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty],
                            [IntrNoMem]>;
  def int_x86_bmi_pdep_32 : ClangBuiltin<"__builtin_ia32_pdep_si">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem]>;
  def int_x86_bmi_pdep_64 : ClangBuiltin<"__builtin_ia32_pdep_di">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty],
                            [IntrNoMem]>;
  def int_x86_bmi_pext_32 : ClangBuiltin<"__builtin_ia32_pext_si">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem]>;
  def int_x86_bmi_pext_64 : ClangBuiltin<"__builtin_ia32_pext_di">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty],
                            [IntrNoMem]>;
}

//===----------------------------------------------------------------------===//
// FS/GS Base

let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_rdfsbase_32 : ClangBuiltin<"__builtin_ia32_rdfsbase32">,
              Intrinsic<[llvm_i32_ty], []>;
  def int_x86_rdgsbase_32 : ClangBuiltin<"__builtin_ia32_rdgsbase32">,
              Intrinsic<[llvm_i32_ty], []>;
  def int_x86_rdfsbase_64 : ClangBuiltin<"__builtin_ia32_rdfsbase64">,
              Intrinsic<[llvm_i64_ty], []>;
  def int_x86_rdgsbase_64 : ClangBuiltin<"__builtin_ia32_rdgsbase64">,
              Intrinsic<[llvm_i64_ty], []>;
  def int_x86_wrfsbase_32 : ClangBuiltin<"__builtin_ia32_wrfsbase32">,
              Intrinsic<[], [llvm_i32_ty]>;
  def int_x86_wrgsbase_32 : ClangBuiltin<"__builtin_ia32_wrgsbase32">,
              Intrinsic<[], [llvm_i32_ty]>;
  def int_x86_wrfsbase_64 : ClangBuiltin<"__builtin_ia32_wrfsbase64">,
              Intrinsic<[], [llvm_i64_ty]>;
  def int_x86_wrgsbase_64 : ClangBuiltin<"__builtin_ia32_wrgsbase64">,
              Intrinsic<[], [llvm_i64_ty]>;
}

//===----------------------------------------------------------------------===//
// FXSR
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_fxrstor : ClangBuiltin<"__builtin_ia32_fxrstor">,
              Intrinsic<[], [llvm_ptr_ty], []>;
  def int_x86_fxrstor64 : ClangBuiltin<"__builtin_ia32_fxrstor64">,
              Intrinsic<[], [llvm_ptr_ty], []>;
  def int_x86_fxsave : ClangBuiltin<"__builtin_ia32_fxsave">,
              Intrinsic<[], [llvm_ptr_ty], []>;
  def int_x86_fxsave64 : ClangBuiltin<"__builtin_ia32_fxsave64">,
              Intrinsic<[], [llvm_ptr_ty], []>;
}

//===----------------------------------------------------------------------===//
// XSAVE
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_xsave :
              Intrinsic<[], [llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty], []>;
  def int_x86_xsave64 :
              Intrinsic<[], [llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty], []>;
  def int_x86_xrstor :
              Intrinsic<[], [llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty], []>;
  def int_x86_xrstor64 :
              Intrinsic<[], [llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty], []>;
  def int_x86_xsaveopt :
              Intrinsic<[], [llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty], []>;
  def int_x86_xsaveopt64 :
              Intrinsic<[], [llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty], []>;
  def int_x86_xrstors :
              Intrinsic<[], [llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty], []>;
  def int_x86_xrstors64 :
              Intrinsic<[], [llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty], []>;
  def int_x86_xsavec :
              Intrinsic<[], [llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty], []>;
  def int_x86_xsavec64 :
              Intrinsic<[], [llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty], []>;
  def int_x86_xsaves :
              Intrinsic<[], [llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty], []>;
  def int_x86_xsaves64 :
              Intrinsic<[], [llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty], []>;
  def int_x86_xgetbv :
              Intrinsic<[llvm_i64_ty], [llvm_i32_ty], []>;
  def int_x86_xsetbv :
              Intrinsic<[], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], []>;
}

//===----------------------------------------------------------------------===//
// CLFLUSHOPT and CLWB
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_clflushopt : ClangBuiltin<"__builtin_ia32_clflushopt">,
              Intrinsic<[], [llvm_ptr_ty], []>;

  def int_x86_clwb : ClangBuiltin<"__builtin_ia32_clwb">,
              Intrinsic<[], [llvm_ptr_ty], []>;
}

//===----------------------------------------------------------------------===//
// Support protection key
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_rdpkru : ClangBuiltin <"__builtin_ia32_rdpkru">,
              Intrinsic<[llvm_i32_ty], [], []>;
  def int_x86_wrpkru : ClangBuiltin<"__builtin_ia32_wrpkru">,
              Intrinsic<[], [llvm_i32_ty], []>;
}
//===----------------------------------------------------------------------===//
// Half float conversion

let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_vcvtps2ph_128 : ClangBuiltin<"__builtin_ia32_vcvtps2ph">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v4f32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_vcvtps2ph_256 : ClangBuiltin<"__builtin_ia32_vcvtps2ph256">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8f32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_mask_vcvtph2ps_512 :
      DefaultAttrsIntrinsic<[llvm_v16f32_ty], [llvm_v16i16_ty, llvm_v16f32_ty,
                                           llvm_i16_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<3>>]>;
  def int_x86_avx512_mask_vcvtps2ph_512 : ClangBuiltin<"__builtin_ia32_vcvtps2ph512_mask">,
      DefaultAttrsIntrinsic<[llvm_v16i16_ty], [llvm_v16f32_ty, llvm_i32_ty,
                                               llvm_v16i16_ty, llvm_i16_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_mask_vcvtps2ph_256 : ClangBuiltin<"__builtin_ia32_vcvtps2ph256_mask">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8f32_ty, llvm_i32_ty,
                                           llvm_v8i16_ty, llvm_i8_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_mask_vcvtps2ph_128 : ClangBuiltin<"__builtin_ia32_vcvtps2ph_mask">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v4f32_ty, llvm_i32_ty,
                                               llvm_v8i16_ty, llvm_i8_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
}

//===----------------------------------------------------------------------===//
// TBM

let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_tbm_bextri_u32 : ClangBuiltin<"__builtin_ia32_bextri_u32">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_tbm_bextri_u64 : ClangBuiltin<"__builtin_ia32_bextri_u64">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
}

//===----------------------------------------------------------------------===//
// RDRAND intrinsics - Return a random value and whether it is valid.
// RDSEED intrinsics - Return a NIST SP800-90B & C compliant random value and
// whether it is valid.

let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  // These are declared side-effecting so they don't get eliminated by CSE or
  // LICM.
  def int_x86_rdrand_16 : Intrinsic<[llvm_i16_ty, llvm_i32_ty], [], []>;
  def int_x86_rdrand_32 : Intrinsic<[llvm_i32_ty, llvm_i32_ty], [], []>;
  def int_x86_rdrand_64 : Intrinsic<[llvm_i64_ty, llvm_i32_ty], [], []>;
  def int_x86_rdseed_16 : Intrinsic<[llvm_i16_ty, llvm_i32_ty], [], []>;
  def int_x86_rdseed_32 : Intrinsic<[llvm_i32_ty, llvm_i32_ty], [], []>;
  def int_x86_rdseed_64 : Intrinsic<[llvm_i64_ty, llvm_i32_ty], [], []>;
}

//===----------------------------------------------------------------------===//
// ADX

let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_addcarry_32:
      DefaultAttrsIntrinsic<[llvm_i8_ty, llvm_i32_ty],
                            [llvm_i8_ty, llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem]>;
  def int_x86_addcarry_64:
      DefaultAttrsIntrinsic<[llvm_i8_ty, llvm_i64_ty],
                            [llvm_i8_ty, llvm_i64_ty, llvm_i64_ty],
                            [IntrNoMem]>;
  def int_x86_subborrow_32:
      DefaultAttrsIntrinsic<[llvm_i8_ty, llvm_i32_ty],
                            [llvm_i8_ty, llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem]>;
  def int_x86_subborrow_64:
      DefaultAttrsIntrinsic<[llvm_i8_ty, llvm_i64_ty],
                            [llvm_i8_ty, llvm_i64_ty, llvm_i64_ty],
                            [IntrNoMem]>;
}

//===----------------------------------------------------------------------===//
// RTM intrinsics. Transactional Memory support.

let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_xbegin : ClangBuiltin<"__builtin_ia32_xbegin">,
              Intrinsic<[llvm_i32_ty], [], []>;
  def int_x86_xend : ClangBuiltin<"__builtin_ia32_xend">,
              Intrinsic<[], [], []>;
  def int_x86_xabort : ClangBuiltin<"__builtin_ia32_xabort">,
              Intrinsic<[], [llvm_i8_ty], [ImmArg<ArgIndex<0>>]>;
  def int_x86_xtest : ClangBuiltin<"__builtin_ia32_xtest">,
              Intrinsic<[llvm_i32_ty], [], []>;
}

//===----------------------------------------------------------------------===//
// AVX512

// Mask ops
let TargetPrefix = "x86" in {
  def int_x86_avx512_kadd_b :
      DefaultAttrsIntrinsic<[llvm_v8i1_ty], [llvm_v8i1_ty, llvm_v8i1_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_kadd_w :
      DefaultAttrsIntrinsic<[llvm_v16i1_ty], [llvm_v16i1_ty, llvm_v16i1_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_kadd_d :
      DefaultAttrsIntrinsic<[llvm_v32i1_ty], [llvm_v32i1_ty, llvm_v32i1_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_kadd_q :
      DefaultAttrsIntrinsic<[llvm_v64i1_ty], [llvm_v64i1_ty, llvm_v64i1_ty],
                            [IntrNoMem]>;

  def int_x86_avx512_ktestc_b :
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v8i1_ty, llvm_v8i1_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_ktestc_w :
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v16i1_ty, llvm_v16i1_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_ktestc_d :
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v32i1_ty, llvm_v32i1_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_ktestc_q :
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v64i1_ty, llvm_v64i1_ty],
                            [IntrNoMem]>;

  def int_x86_avx512_ktestz_b :
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v8i1_ty, llvm_v8i1_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_ktestz_w :
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v16i1_ty, llvm_v16i1_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_ktestz_d :
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v32i1_ty, llvm_v32i1_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_ktestz_q :
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v64i1_ty, llvm_v64i1_ty],
                            [IntrNoMem]>;
}

// Conversion ops
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_avx512_cvttss2si : ClangBuiltin<"__builtin_ia32_vcvttss2si32">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v4f32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_cvttss2si64 : ClangBuiltin<"__builtin_ia32_vcvttss2si64">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_v4f32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_cvttss2usi : ClangBuiltin<"__builtin_ia32_vcvttss2usi32">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v4f32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_cvttss2usi64 : ClangBuiltin<"__builtin_ia32_vcvttss2usi64">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_v4f32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_cvtusi2ss : ClangBuiltin<"__builtin_ia32_cvtusi2ss32">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                            [llvm_v4f32_ty, llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_avx512_cvtusi642ss : ClangBuiltin<"__builtin_ia32_cvtusi2ss64">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                            [llvm_v4f32_ty, llvm_i64_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_avx512_cvttsd2si : ClangBuiltin<"__builtin_ia32_vcvttsd2si32">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v2f64_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_cvttsd2si64 : ClangBuiltin<"__builtin_ia32_vcvttsd2si64">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_v2f64_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_cvttsd2usi : ClangBuiltin<"__builtin_ia32_vcvttsd2usi32">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v2f64_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_cvttsd2usi64 : ClangBuiltin<"__builtin_ia32_vcvttsd2usi64">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_v2f64_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_cvtusi642sd : ClangBuiltin<"__builtin_ia32_cvtusi2sd64">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty],
                            [llvm_v2f64_ty, llvm_i64_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_avx512_vcvtss2usi32 : ClangBuiltin<"__builtin_ia32_vcvtss2usi32">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v4f32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_vcvtss2usi64 : ClangBuiltin<"__builtin_ia32_vcvtss2usi64">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_v4f32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_vcvtss2si32 : ClangBuiltin<"__builtin_ia32_vcvtss2si32">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v4f32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_vcvtss2si64 : ClangBuiltin<"__builtin_ia32_vcvtss2si64">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_v4f32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_vcvtsd2usi32 : ClangBuiltin<"__builtin_ia32_vcvtsd2usi32">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v2f64_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_vcvtsd2usi64 : ClangBuiltin<"__builtin_ia32_vcvtsd2usi64">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_v2f64_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_vcvtsd2si32 : ClangBuiltin<"__builtin_ia32_vcvtsd2si32">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v2f64_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_vcvtsd2si64 : ClangBuiltin<"__builtin_ia32_vcvtsd2si64">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_v2f64_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_cvtsi2ss32 : ClangBuiltin<"__builtin_ia32_cvtsi2ss32">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                            [llvm_v4f32_ty, llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_avx512_cvtsi2ss64 : ClangBuiltin<"__builtin_ia32_cvtsi2ss64">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                            [llvm_v4f32_ty, llvm_i64_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_avx512_cvtsi2sd64 : ClangBuiltin<"__builtin_ia32_cvtsi2sd64">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty],
                            [llvm_v2f64_ty, llvm_i64_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
}

// Pack ops.
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_avx512_packsswb_512 : ClangBuiltin<"__builtin_ia32_packsswb512">,
      DefaultAttrsIntrinsic<[llvm_v64i8_ty], [llvm_v32i16_ty,llvm_v32i16_ty],
                           [IntrNoMem]>;
  def int_x86_avx512_packssdw_512 : ClangBuiltin<"__builtin_ia32_packssdw512">,
      DefaultAttrsIntrinsic<[llvm_v32i16_ty], [llvm_v16i32_ty, llvm_v16i32_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_packuswb_512 : ClangBuiltin<"__builtin_ia32_packuswb512">,
      DefaultAttrsIntrinsic<[llvm_v64i8_ty], [llvm_v32i16_ty,llvm_v32i16_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_packusdw_512 : ClangBuiltin<"__builtin_ia32_packusdw512">,
      DefaultAttrsIntrinsic<[llvm_v32i16_ty], [llvm_v16i32_ty, llvm_v16i32_ty],
                            [IntrNoMem]>;
}

// Vector convert
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
  def int_x86_avx512_sitofp_round :
      DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [llvm_anyint_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;

  def int_x86_avx512_uitofp_round :
      DefaultAttrsIntrinsic<[llvm_anyfloat_ty], [llvm_anyint_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;

  def int_x86_avx512_mask_cvtpd2dq_128 :
        ClangBuiltin<"__builtin_ia32_cvtpd2dq128_mask">,
          DefaultAttrsIntrinsic<[llvm_v4i32_ty],
          [llvm_v2f64_ty, llvm_v4i32_ty,  llvm_i8_ty],
          [IntrNoMem]>;

  def int_x86_avx512_mask_cvtpd2dq_512 :
        ClangBuiltin<"__builtin_ia32_cvtpd2dq512_mask">,
          DefaultAttrsIntrinsic<[llvm_v8i32_ty],
          [llvm_v8f64_ty, llvm_v8i32_ty,  llvm_i8_ty,  llvm_i32_ty],
          [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  def int_x86_avx512_mask_cvtpd2ps_512 :
        ClangBuiltin<"__builtin_ia32_cvtpd2ps512_mask">,
          DefaultAttrsIntrinsic<[llvm_v8f32_ty],
          [llvm_v8f64_ty, llvm_v8f32_ty,  llvm_i8_ty,  llvm_i32_ty],
          [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  def int_x86_avx512_mask_cvtsd2ss_round :
        ClangBuiltin<"__builtin_ia32_cvtsd2ss_round_mask">,
          DefaultAttrsIntrinsic<[llvm_v4f32_ty],
          [llvm_v4f32_ty, llvm_v2f64_ty, llvm_v4f32_ty, llvm_i8_ty, llvm_i32_ty],
          [IntrNoMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_cvtss2sd_round :
        ClangBuiltin<"__builtin_ia32_cvtss2sd_round_mask">,
          DefaultAttrsIntrinsic<[llvm_v2f64_ty],
          [llvm_v2f64_ty, llvm_v4f32_ty, llvm_v2f64_ty, llvm_i8_ty, llvm_i32_ty],
          [IntrNoMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_cvtpd2ps :
        ClangBuiltin<"__builtin_ia32_cvtpd2ps_mask">,
          DefaultAttrsIntrinsic<[llvm_v4f32_ty],
          [llvm_v2f64_ty, llvm_v4f32_ty,  llvm_i8_ty],
          [IntrNoMem]>;

  def int_x86_avx512_mask_cvtpd2qq_128 :
        ClangBuiltin<"__builtin_ia32_cvtpd2qq128_mask">,
          DefaultAttrsIntrinsic<[llvm_v2i64_ty],
          [llvm_v2f64_ty, llvm_v2i64_ty,  llvm_i8_ty],
          [IntrNoMem]>;

  def int_x86_avx512_mask_cvtpd2qq_256 :
        ClangBuiltin<"__builtin_ia32_cvtpd2qq256_mask">,
          DefaultAttrsIntrinsic<[llvm_v4i64_ty],
          [llvm_v4f64_ty, llvm_v4i64_ty,  llvm_i8_ty],
          [IntrNoMem]>;

  def int_x86_avx512_mask_cvtpd2qq_512 :
        ClangBuiltin<"__builtin_ia32_cvtpd2qq512_mask">,
          DefaultAttrsIntrinsic<[llvm_v8i64_ty],
          [llvm_v8f64_ty, llvm_v8i64_ty,  llvm_i8_ty,  llvm_i32_ty],
          [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  def int_x86_avx512_mask_cvtpd2udq_128 :
        ClangBuiltin<"__builtin_ia32_cvtpd2udq128_mask">,
          DefaultAttrsIntrinsic<[llvm_v4i32_ty],
          [llvm_v2f64_ty, llvm_v4i32_ty,  llvm_i8_ty],
          [IntrNoMem]>;

  def int_x86_avx512_mask_cvtpd2udq_256 :
        ClangBuiltin<"__builtin_ia32_cvtpd2udq256_mask">,
          DefaultAttrsIntrinsic<[llvm_v4i32_ty],
          [llvm_v4f64_ty, llvm_v4i32_ty,  llvm_i8_ty],
          [IntrNoMem]>;

  def int_x86_avx512_mask_cvtpd2udq_512 :
        ClangBuiltin<"__builtin_ia32_cvtpd2udq512_mask">,
          DefaultAttrsIntrinsic<[llvm_v8i32_ty],
          [llvm_v8f64_ty, llvm_v8i32_ty,  llvm_i8_ty,  llvm_i32_ty],
          [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  def int_x86_avx512_mask_cvtpd2uqq_128 :
        ClangBuiltin<"__builtin_ia32_cvtpd2uqq128_mask">,
          DefaultAttrsIntrinsic<[llvm_v2i64_ty],
          [llvm_v2f64_ty, llvm_v2i64_ty,  llvm_i8_ty],
          [IntrNoMem]>;

  def int_x86_avx512_mask_cvtpd2uqq_256 :
        ClangBuiltin<"__builtin_ia32_cvtpd2uqq256_mask">,
          DefaultAttrsIntrinsic<[llvm_v4i64_ty],
          [llvm_v4f64_ty, llvm_v4i64_ty,  llvm_i8_ty],
          [IntrNoMem]>;

  def int_x86_avx512_mask_cvtpd2uqq_512 :
        ClangBuiltin<"__builtin_ia32_cvtpd2uqq512_mask">,
          DefaultAttrsIntrinsic<[llvm_v8i64_ty],
          [llvm_v8f64_ty, llvm_v8i64_ty,  llvm_i8_ty,  llvm_i32_ty],
          [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  def int_x86_avx512_mask_cvtps2dq_128 :
        ClangBuiltin<"__builtin_ia32_cvtps2dq128_mask">,
          DefaultAttrsIntrinsic<[llvm_v4i32_ty],
          [llvm_v4f32_ty, llvm_v4i32_ty,  llvm_i8_ty],
          [IntrNoMem]>;

  def int_x86_avx512_mask_cvtps2dq_256 :
        ClangBuiltin<"__builtin_ia32_cvtps2dq256_mask">,
          DefaultAttrsIntrinsic<[llvm_v8i32_ty],
          [llvm_v8f32_ty, llvm_v8i32_ty,  llvm_i8_ty],
          [IntrNoMem]>;

  def int_x86_avx512_mask_cvtps2dq_512 :
        ClangBuiltin<"__builtin_ia32_cvtps2dq512_mask">,
          DefaultAttrsIntrinsic<[llvm_v16i32_ty],
          [llvm_v16f32_ty, llvm_v16i32_ty,  llvm_i16_ty,  llvm_i32_ty],
          [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  def int_x86_avx512_mask_cvtps2pd_512 :
        ClangBuiltin<"__builtin_ia32_cvtps2pd512_mask">,
          DefaultAttrsIntrinsic<[llvm_v8f64_ty],
          [llvm_v8f32_ty, llvm_v8f64_ty,  llvm_i8_ty,  llvm_i32_ty],
          [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  def int_x86_avx512_mask_cvtps2qq_128 :
        ClangBuiltin<"__builtin_ia32_cvtps2qq128_mask">,
          DefaultAttrsIntrinsic<[llvm_v2i64_ty],
          [llvm_v4f32_ty, llvm_v2i64_ty,  llvm_i8_ty],
          [IntrNoMem]>;

  def int_x86_avx512_mask_cvtps2qq_256 :
        ClangBuiltin<"__builtin_ia32_cvtps2qq256_mask">,
          DefaultAttrsIntrinsic<[llvm_v4i64_ty],
          [llvm_v4f32_ty, llvm_v4i64_ty,  llvm_i8_ty],
          [IntrNoMem]>;

  def int_x86_avx512_mask_cvtps2qq_512 :
        ClangBuiltin<"__builtin_ia32_cvtps2qq512_mask">,
          DefaultAttrsIntrinsic<[llvm_v8i64_ty],
          [llvm_v8f32_ty, llvm_v8i64_ty,  llvm_i8_ty,  llvm_i32_ty],
          [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  def int_x86_avx512_mask_cvtps2udq_128 :
        ClangBuiltin<"__builtin_ia32_cvtps2udq128_mask">,
          DefaultAttrsIntrinsic<[llvm_v4i32_ty],
          [llvm_v4f32_ty, llvm_v4i32_ty,  llvm_i8_ty],
          [IntrNoMem]>;

  def int_x86_avx512_mask_cvtps2udq_256 :
        ClangBuiltin<"__builtin_ia32_cvtps2udq256_mask">,
          DefaultAttrsIntrinsic<[llvm_v8i32_ty],
          [llvm_v8f32_ty, llvm_v8i32_ty,  llvm_i8_ty],
          [IntrNoMem]>;

  def int_x86_avx512_mask_cvtps2udq_512 :
        ClangBuiltin<"__builtin_ia32_cvtps2udq512_mask">,
          DefaultAttrsIntrinsic<[llvm_v16i32_ty],
          [llvm_v16f32_ty, llvm_v16i32_ty,  llvm_i16_ty,  llvm_i32_ty],
          [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  def int_x86_avx512_mask_cvtps2uqq_128 :
        ClangBuiltin<"__builtin_ia32_cvtps2uqq128_mask">,
          DefaultAttrsIntrinsic<[llvm_v2i64_ty],
          [llvm_v4f32_ty, llvm_v2i64_ty,  llvm_i8_ty],
          [IntrNoMem]>;

  def int_x86_avx512_mask_cvtps2uqq_256 :
        ClangBuiltin<"__builtin_ia32_cvtps2uqq256_mask">,
          DefaultAttrsIntrinsic<[llvm_v4i64_ty],
          [llvm_v4f32_ty, llvm_v4i64_ty,  llvm_i8_ty],
          [IntrNoMem]>;

  def int_x86_avx512_mask_cvtps2uqq_512 :
        ClangBuiltin<"__builtin_ia32_cvtps2uqq512_mask">,
          DefaultAttrsIntrinsic<[llvm_v8i64_ty],
          [llvm_v8f32_ty, llvm_v8i64_ty,  llvm_i8_ty,  llvm_i32_ty],
          [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  def int_x86_avx512_mask_cvtqq2ps_128 :
        ClangBuiltin<"__builtin_ia32_cvtqq2ps128_mask">,
          DefaultAttrsIntrinsic<[llvm_v4f32_ty],
          [llvm_v2i64_ty, llvm_v4f32_ty,  llvm_i8_ty],
          [IntrNoMem]>;

  def int_x86_avx512_mask_cvttpd2dq_128 :
        ClangBuiltin<"__builtin_ia32_cvttpd2dq128_mask">,
          DefaultAttrsIntrinsic<[llvm_v4i32_ty],
          [llvm_v2f64_ty, llvm_v4i32_ty,  llvm_i8_ty],
          [IntrNoMem]>;

  def int_x86_avx512_mask_cvttpd2dq_512 :
        ClangBuiltin<"__builtin_ia32_cvttpd2dq512_mask">,
          DefaultAttrsIntrinsic<[llvm_v8i32_ty],
          [llvm_v8f64_ty, llvm_v8i32_ty,  llvm_i8_ty,  llvm_i32_ty],
          [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  def int_x86_avx512_mask_cvttpd2qq_128 :
        ClangBuiltin<"__builtin_ia32_cvttpd2qq128_mask">,
          DefaultAttrsIntrinsic<[llvm_v2i64_ty],
          [llvm_v2f64_ty, llvm_v2i64_ty,  llvm_i8_ty],
          [IntrNoMem]>;

  def int_x86_avx512_mask_cvttpd2qq_256 :
        ClangBuiltin<"__builtin_ia32_cvttpd2qq256_mask">,
          DefaultAttrsIntrinsic<[llvm_v4i64_ty],
          [llvm_v4f64_ty, llvm_v4i64_ty,  llvm_i8_ty],
          [IntrNoMem]>;

  def int_x86_avx512_mask_cvttpd2qq_512 :
        ClangBuiltin<"__builtin_ia32_cvttpd2qq512_mask">,
          DefaultAttrsIntrinsic<[llvm_v8i64_ty],
          [llvm_v8f64_ty, llvm_v8i64_ty,  llvm_i8_ty,  llvm_i32_ty],
          [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  def int_x86_avx512_mask_cvttpd2udq_128 :
        ClangBuiltin<"__builtin_ia32_cvttpd2udq128_mask">,
          DefaultAttrsIntrinsic<[llvm_v4i32_ty],
          [llvm_v2f64_ty, llvm_v4i32_ty,  llvm_i8_ty],
          [IntrNoMem]>;

  def int_x86_avx512_mask_cvttpd2udq_256 :
        ClangBuiltin<"__builtin_ia32_cvttpd2udq256_mask">,
          DefaultAttrsIntrinsic<[llvm_v4i32_ty],
          [llvm_v4f64_ty, llvm_v4i32_ty,  llvm_i8_ty],
          [IntrNoMem]>;

  def int_x86_avx512_mask_cvttpd2udq_512 :
        ClangBuiltin<"__builtin_ia32_cvttpd2udq512_mask">,
          DefaultAttrsIntrinsic<[llvm_v8i32_ty],
          [llvm_v8f64_ty, llvm_v8i32_ty,  llvm_i8_ty,  llvm_i32_ty],
          [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  def int_x86_avx512_mask_cvttpd2uqq_128 :
        ClangBuiltin<"__builtin_ia32_cvttpd2uqq128_mask">,
          DefaultAttrsIntrinsic<[llvm_v2i64_ty],
          [llvm_v2f64_ty, llvm_v2i64_ty,  llvm_i8_ty],
          [IntrNoMem]>;

  def int_x86_avx512_mask_cvttpd2uqq_256 :
        ClangBuiltin<"__builtin_ia32_cvttpd2uqq256_mask">,
          DefaultAttrsIntrinsic<[llvm_v4i64_ty],
          [llvm_v4f64_ty, llvm_v4i64_ty,  llvm_i8_ty],
          [IntrNoMem]>;

  def int_x86_avx512_mask_cvttpd2uqq_512 :
        ClangBuiltin<"__builtin_ia32_cvttpd2uqq512_mask">,
          DefaultAttrsIntrinsic<[llvm_v8i64_ty],
          [llvm_v8f64_ty, llvm_v8i64_ty,  llvm_i8_ty,  llvm_i32_ty],
          [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  def int_x86_avx512_mask_cvttps2dq_512 :
        ClangBuiltin<"__builtin_ia32_cvttps2dq512_mask">,
          DefaultAttrsIntrinsic<[llvm_v16i32_ty],
          [llvm_v16f32_ty, llvm_v16i32_ty,  llvm_i16_ty,  llvm_i32_ty],
          [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  def int_x86_avx512_mask_cvttps2qq_128 :
        ClangBuiltin<"__builtin_ia32_cvttps2qq128_mask">,
          DefaultAttrsIntrinsic<[llvm_v2i64_ty],
          [llvm_v4f32_ty, llvm_v2i64_ty,  llvm_i8_ty],
          [IntrNoMem]>;

  def int_x86_avx512_mask_cvttps2qq_256 :
        ClangBuiltin<"__builtin_ia32_cvttps2qq256_mask">,
          DefaultAttrsIntrinsic<[llvm_v4i64_ty],
          [llvm_v4f32_ty, llvm_v4i64_ty,  llvm_i8_ty],
          [IntrNoMem]>;

  def int_x86_avx512_mask_cvttps2qq_512 :
        ClangBuiltin<"__builtin_ia32_cvttps2qq512_mask">,
          DefaultAttrsIntrinsic<[llvm_v8i64_ty],
          [llvm_v8f32_ty, llvm_v8i64_ty,  llvm_i8_ty,  llvm_i32_ty],
          [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  def int_x86_avx512_mask_cvttps2udq_128 :
        ClangBuiltin<"__builtin_ia32_cvttps2udq128_mask">,
          DefaultAttrsIntrinsic<[llvm_v4i32_ty],
          [llvm_v4f32_ty, llvm_v4i32_ty,  llvm_i8_ty],
          [IntrNoMem]>;

  def int_x86_avx512_mask_cvttps2udq_256 :
        ClangBuiltin<"__builtin_ia32_cvttps2udq256_mask">,
          DefaultAttrsIntrinsic<[llvm_v8i32_ty],
          [llvm_v8f32_ty, llvm_v8i32_ty,  llvm_i8_ty],
          [IntrNoMem]>;

  def int_x86_avx512_mask_cvttps2udq_512 :
        ClangBuiltin<"__builtin_ia32_cvttps2udq512_mask">,
          DefaultAttrsIntrinsic<[llvm_v16i32_ty],
          [llvm_v16f32_ty, llvm_v16i32_ty,  llvm_i16_ty,  llvm_i32_ty],
          [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  def int_x86_avx512_mask_cvttps2uqq_128 :
        ClangBuiltin<"__builtin_ia32_cvttps2uqq128_mask">,
          DefaultAttrsIntrinsic<[llvm_v2i64_ty],
          [llvm_v4f32_ty, llvm_v2i64_ty,  llvm_i8_ty],
          [IntrNoMem]>;

  def int_x86_avx512_mask_cvttps2uqq_256 :
        ClangBuiltin<"__builtin_ia32_cvttps2uqq256_mask">,
          DefaultAttrsIntrinsic<[llvm_v4i64_ty],
          [llvm_v4f32_ty, llvm_v4i64_ty,  llvm_i8_ty],
          [IntrNoMem]>;

  def int_x86_avx512_mask_cvttps2uqq_512 :
        ClangBuiltin<"__builtin_ia32_cvttps2uqq512_mask">,
          DefaultAttrsIntrinsic<[llvm_v8i64_ty],
          [llvm_v8f32_ty, llvm_v8i64_ty,  llvm_i8_ty,  llvm_i32_ty],
          [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  def int_x86_avx512_mask_cvtuqq2ps_128 :
        ClangBuiltin<"__builtin_ia32_cvtuqq2ps128_mask">,
          DefaultAttrsIntrinsic<[llvm_v4f32_ty],
          [llvm_v2i64_ty, llvm_v4f32_ty,  llvm_i8_ty],
          [IntrNoMem]>;

  def int_x86_avx512_mask_rndscale_pd_128 : ClangBuiltin<"__builtin_ia32_rndscalepd_128_mask">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty],
                            [llvm_v2f64_ty, llvm_i32_ty, llvm_v2f64_ty,
                             llvm_i8_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_mask_rndscale_pd_256 : ClangBuiltin<"__builtin_ia32_rndscalepd_256_mask">,
      DefaultAttrsIntrinsic<[llvm_v4f64_ty],
                            [llvm_v4f64_ty, llvm_i32_ty, llvm_v4f64_ty,
                             llvm_i8_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_mask_rndscale_pd_512 : ClangBuiltin<"__builtin_ia32_rndscalepd_mask">,
      DefaultAttrsIntrinsic<[llvm_v8f64_ty],
                            [llvm_v8f64_ty, llvm_i32_ty, llvm_v8f64_ty,
                             llvm_i8_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>,
                             ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_rndscale_ps_128 : ClangBuiltin<"__builtin_ia32_rndscaleps_128_mask">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                            [llvm_v4f32_ty, llvm_i32_ty, llvm_v4f32_ty,
                             llvm_i8_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_mask_rndscale_ps_256 : ClangBuiltin<"__builtin_ia32_rndscaleps_256_mask">,
      DefaultAttrsIntrinsic<[llvm_v8f32_ty],
                            [llvm_v8f32_ty, llvm_i32_ty, llvm_v8f32_ty,
                             llvm_i8_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_mask_rndscale_ps_512 : ClangBuiltin<"__builtin_ia32_rndscaleps_mask">,
      DefaultAttrsIntrinsic<[llvm_v16f32_ty],
                            [llvm_v16f32_ty, llvm_i32_ty, llvm_v16f32_ty,
                             llvm_i16_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>,
                             ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_reduce_pd_128 : ClangBuiltin<"__builtin_ia32_reducepd128_mask">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty],
                            [llvm_v2f64_ty, llvm_i32_ty, llvm_v2f64_ty,
                             llvm_i8_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_mask_reduce_pd_256 : ClangBuiltin<"__builtin_ia32_reducepd256_mask">,
      DefaultAttrsIntrinsic<[llvm_v4f64_ty],
                            [llvm_v4f64_ty, llvm_i32_ty, llvm_v4f64_ty,
                             llvm_i8_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_mask_reduce_pd_512 : ClangBuiltin<"__builtin_ia32_reducepd512_mask">,
      DefaultAttrsIntrinsic<[llvm_v8f64_ty],
                            [llvm_v8f64_ty, llvm_i32_ty, llvm_v8f64_ty,
                             llvm_i8_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>,
                             ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_reduce_ps_128 : ClangBuiltin<"__builtin_ia32_reduceps128_mask">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                            [llvm_v4f32_ty, llvm_i32_ty, llvm_v4f32_ty,
                             llvm_i8_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_mask_reduce_ps_256 : ClangBuiltin<"__builtin_ia32_reduceps256_mask">,
      DefaultAttrsIntrinsic<[llvm_v8f32_ty],
                            [llvm_v8f32_ty, llvm_i32_ty, llvm_v8f32_ty,
                             llvm_i8_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_mask_reduce_ps_512 : ClangBuiltin<"__builtin_ia32_reduceps512_mask">,
      DefaultAttrsIntrinsic<[llvm_v16f32_ty],
                            [llvm_v16f32_ty, llvm_i32_ty, llvm_v16f32_ty,
                             llvm_i16_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>,
                             ImmArg<ArgIndex<4>>]>;
def int_x86_avx512_mask_range_pd_128 : ClangBuiltin<"__builtin_ia32_rangepd128_mask">,
    DefaultAttrsIntrinsic<[llvm_v2f64_ty],
                          [llvm_v2f64_ty, llvm_v2f64_ty, llvm_i32_ty,
                           llvm_v2f64_ty,  llvm_i8_ty],
                          [IntrNoMem, ImmArg<ArgIndex<2>>]>;
def int_x86_avx512_mask_range_pd_256 : ClangBuiltin<"__builtin_ia32_rangepd256_mask">,
    DefaultAttrsIntrinsic<[llvm_v4f64_ty],
                          [llvm_v4f64_ty, llvm_v4f64_ty, llvm_i32_ty,
                           llvm_v4f64_ty,  llvm_i8_ty],
                          [IntrNoMem, ImmArg<ArgIndex<2>>]>;
def int_x86_avx512_mask_range_pd_512 : ClangBuiltin<"__builtin_ia32_rangepd512_mask">,
    DefaultAttrsIntrinsic<[llvm_v8f64_ty],
                          [llvm_v8f64_ty, llvm_v8f64_ty, llvm_i32_ty,
                           llvm_v8f64_ty, llvm_i8_ty, llvm_i32_ty],
                          [IntrNoMem, ImmArg<ArgIndex<2>>,
                           ImmArg<ArgIndex<5>>]>;
def int_x86_avx512_mask_range_ps_128 : ClangBuiltin<"__builtin_ia32_rangeps128_mask">,
    DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                          [llvm_v4f32_ty, llvm_v4f32_ty, llvm_i32_ty,
                           llvm_v4f32_ty,  llvm_i8_ty],
                          [IntrNoMem, ImmArg<ArgIndex<2>>]>;
def int_x86_avx512_mask_range_ps_256 : ClangBuiltin<"__builtin_ia32_rangeps256_mask">,
    DefaultAttrsIntrinsic<[llvm_v8f32_ty],
                          [llvm_v8f32_ty, llvm_v8f32_ty, llvm_i32_ty,
                           llvm_v8f32_ty,  llvm_i8_ty],
                          [IntrNoMem, ImmArg<ArgIndex<2>>]>;
def int_x86_avx512_mask_range_ps_512 : ClangBuiltin<"__builtin_ia32_rangeps512_mask">,
    DefaultAttrsIntrinsic<[llvm_v16f32_ty],
                          [llvm_v16f32_ty, llvm_v16f32_ty, llvm_i32_ty,
                           llvm_v16f32_ty, llvm_i16_ty, llvm_i32_ty],
                          [IntrNoMem, ImmArg<ArgIndex<2>>,
                           ImmArg<ArgIndex<5>>]>;
}

// Vector broadcast from mask
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".
   def int_x86_avx512_broadcastmw_512 :
       ClangBuiltin<"__builtin_ia32_broadcastmw512">,
       DefaultAttrsIntrinsic<[llvm_v16i32_ty], [llvm_i16_ty], [IntrNoMem]>;
   def int_x86_avx512_broadcastmw_256 :
       ClangBuiltin<"__builtin_ia32_broadcastmw256">,
       DefaultAttrsIntrinsic<[llvm_v8i32_ty], [llvm_i16_ty], [IntrNoMem]>;
   def int_x86_avx512_broadcastmw_128 :
       ClangBuiltin<"__builtin_ia32_broadcastmw128">,
       DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_i16_ty], [IntrNoMem]>;
   def int_x86_avx512_broadcastmb_512 :
       ClangBuiltin<"__builtin_ia32_broadcastmb512">,
       DefaultAttrsIntrinsic<[llvm_v8i64_ty], [llvm_i8_ty], [IntrNoMem]>;
   def int_x86_avx512_broadcastmb_256 :
       ClangBuiltin<"__builtin_ia32_broadcastmb256">,
       DefaultAttrsIntrinsic<[llvm_v4i64_ty], [llvm_i8_ty], [IntrNoMem]>;
   def int_x86_avx512_broadcastmb_128 :
       ClangBuiltin<"__builtin_ia32_broadcastmb128">,
       DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_i8_ty], [IntrNoMem]>;
}

// Arithmetic ops
let TargetPrefix = "x86" in {  // All intrinsics start with "llvm.x86.".

  def int_x86_avx512_add_ps_512 : ClangBuiltin<"__builtin_ia32_addps512">,
      DefaultAttrsIntrinsic<[llvm_v16f32_ty], [llvm_v16f32_ty, llvm_v16f32_ty,
                             llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_avx512_add_pd_512 : ClangBuiltin<"__builtin_ia32_addpd512">,
      DefaultAttrsIntrinsic<[llvm_v8f64_ty], [llvm_v8f64_ty, llvm_v8f64_ty,
                             llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_avx512_sub_ps_512 : ClangBuiltin<"__builtin_ia32_subps512">,
      DefaultAttrsIntrinsic<[llvm_v16f32_ty], [llvm_v16f32_ty, llvm_v16f32_ty,
                             llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_avx512_sub_pd_512 : ClangBuiltin<"__builtin_ia32_subpd512">,
      DefaultAttrsIntrinsic<[llvm_v8f64_ty], [llvm_v8f64_ty, llvm_v8f64_ty,
                             llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_avx512_mul_ps_512 : ClangBuiltin<"__builtin_ia32_mulps512">,
      DefaultAttrsIntrinsic<[llvm_v16f32_ty], [llvm_v16f32_ty, llvm_v16f32_ty,
                             llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_avx512_mul_pd_512 : ClangBuiltin<"__builtin_ia32_mulpd512">,
      DefaultAttrsIntrinsic<[llvm_v8f64_ty], [llvm_v8f64_ty, llvm_v8f64_ty,
                             llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_avx512_div_ps_512 : ClangBuiltin<"__builtin_ia32_divps512">,
      DefaultAttrsIntrinsic<[llvm_v16f32_ty], [llvm_v16f32_ty, llvm_v16f32_ty,
                             llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_avx512_div_pd_512 : ClangBuiltin<"__builtin_ia32_divpd512">,
      DefaultAttrsIntrinsic<[llvm_v8f64_ty], [llvm_v8f64_ty, llvm_v8f64_ty,
                             llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<2>>]>;

  def int_x86_avx512_max_ps_512 : ClangBuiltin<"__builtin_ia32_maxps512">,
      DefaultAttrsIntrinsic<[llvm_v16f32_ty], [llvm_v16f32_ty, llvm_v16f32_ty,
                             llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_avx512_max_pd_512 : ClangBuiltin<"__builtin_ia32_maxpd512">,
      DefaultAttrsIntrinsic<[llvm_v8f64_ty], [llvm_v8f64_ty, llvm_v8f64_ty,
                             llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_avx512_min_ps_512 : ClangBuiltin<"__builtin_ia32_minps512">,
      DefaultAttrsIntrinsic<[llvm_v16f32_ty], [llvm_v16f32_ty, llvm_v16f32_ty,
                             llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_avx512_min_pd_512 : ClangBuiltin<"__builtin_ia32_minpd512">,
      DefaultAttrsIntrinsic<[llvm_v8f64_ty], [llvm_v8f64_ty, llvm_v8f64_ty,
                             llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<2>>]>;

  def int_x86_avx512_mask_add_ss_round : ClangBuiltin<"__builtin_ia32_addss_round_mask">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                            [llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4f32_ty,
                             llvm_i8_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_div_ss_round : ClangBuiltin<"__builtin_ia32_divss_round_mask">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                            [llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4f32_ty,
                             llvm_i8_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_mul_ss_round : ClangBuiltin<"__builtin_ia32_mulss_round_mask">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                            [llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4f32_ty,
                             llvm_i8_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_sub_ss_round : ClangBuiltin<"__builtin_ia32_subss_round_mask">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                            [llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4f32_ty,
                             llvm_i8_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_max_ss_round : ClangBuiltin<"__builtin_ia32_maxss_round_mask">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                            [llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4f32_ty,
                             llvm_i8_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_min_ss_round : ClangBuiltin<"__builtin_ia32_minss_round_mask">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                            [llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4f32_ty,
                             llvm_i8_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_add_sd_round : ClangBuiltin<"__builtin_ia32_addsd_round_mask">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty],
                            [llvm_v2f64_ty, llvm_v2f64_ty, llvm_v2f64_ty,
                             llvm_i8_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_div_sd_round : ClangBuiltin<"__builtin_ia32_divsd_round_mask">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty],
                            [llvm_v2f64_ty, llvm_v2f64_ty, llvm_v2f64_ty,
                             llvm_i8_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_mul_sd_round : ClangBuiltin<"__builtin_ia32_mulsd_round_mask">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty],
                            [llvm_v2f64_ty, llvm_v2f64_ty, llvm_v2f64_ty,
                             llvm_i8_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_sub_sd_round : ClangBuiltin<"__builtin_ia32_subsd_round_mask">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty],
                            [llvm_v2f64_ty, llvm_v2f64_ty, llvm_v2f64_ty,
                             llvm_i8_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_max_sd_round : ClangBuiltin<"__builtin_ia32_maxsd_round_mask">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty],
                            [llvm_v2f64_ty, llvm_v2f64_ty, llvm_v2f64_ty,
                             llvm_i8_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_min_sd_round : ClangBuiltin<"__builtin_ia32_minsd_round_mask">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty],
                            [llvm_v2f64_ty, llvm_v2f64_ty, llvm_v2f64_ty,
                             llvm_i8_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_rndscale_ss : ClangBuiltin<"__builtin_ia32_rndscaless_round_mask">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                            [llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4f32_ty,
                             llvm_i8_ty, llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<4>>,
                             ImmArg<ArgIndex<5>>]>;
  def int_x86_avx512_mask_rndscale_sd : ClangBuiltin<"__builtin_ia32_rndscalesd_round_mask">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty],
                            [llvm_v2f64_ty, llvm_v2f64_ty, llvm_v2f64_ty,
                             llvm_i8_ty, llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<4>>,
                             ImmArg<ArgIndex<5>>]>;
  def int_x86_avx512_mask_range_ss : ClangBuiltin<"__builtin_ia32_rangess128_round_mask">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                            [llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4f32_ty,
                             llvm_i8_ty, llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<4>>,
                             ImmArg<ArgIndex<5>>]>;
  def int_x86_avx512_mask_range_sd : ClangBuiltin<"__builtin_ia32_rangesd128_round_mask">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty],
                            [llvm_v2f64_ty, llvm_v2f64_ty, llvm_v2f64_ty,
                             llvm_i8_ty, llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<4>>,
                             ImmArg<ArgIndex<5>>]>;
  def int_x86_avx512_mask_reduce_ss : ClangBuiltin<"__builtin_ia32_reducess_mask">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                            [llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4f32_ty,
                             llvm_i8_ty, llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<4>>,
                             ImmArg<ArgIndex<5>>]>;
  def int_x86_avx512_mask_reduce_sd : ClangBuiltin<"__builtin_ia32_reducesd_mask">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty],
                            [llvm_v2f64_ty, llvm_v2f64_ty, llvm_v2f64_ty,
                             llvm_i8_ty, llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<4>>,
                             ImmArg<ArgIndex<5>>]>;
  def int_x86_avx512_mask_scalef_sd : ClangBuiltin<"__builtin_ia32_scalefsd_round_mask">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty],
                            [llvm_v2f64_ty, llvm_v2f64_ty, llvm_v2f64_ty,
                             llvm_i8_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_scalef_ss : ClangBuiltin<"__builtin_ia32_scalefss_round_mask">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                            [llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4f32_ty,
                             llvm_i8_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_scalef_pd_128 : ClangBuiltin<"__builtin_ia32_scalefpd128_mask">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty, llvm_v2f64_ty,
                             llvm_v2f64_ty, llvm_i8_ty], [IntrNoMem]>;
  def int_x86_avx512_mask_scalef_pd_256 : ClangBuiltin<"__builtin_ia32_scalefpd256_mask">,
      DefaultAttrsIntrinsic<[llvm_v4f64_ty],
                            [llvm_v4f64_ty, llvm_v4f64_ty, llvm_v4f64_ty,
                             llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_scalef_pd_512 : ClangBuiltin<"__builtin_ia32_scalefpd512_mask">,
      DefaultAttrsIntrinsic<[llvm_v8f64_ty],
                            [llvm_v8f64_ty, llvm_v8f64_ty, llvm_v8f64_ty,
                             llvm_i8_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_scalef_ps_128 : ClangBuiltin<"__builtin_ia32_scalefps128_mask">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty, llvm_v4f32_ty,
                             llvm_v4f32_ty, llvm_i8_ty], [IntrNoMem]>;
  def int_x86_avx512_mask_scalef_ps_256 : ClangBuiltin<"__builtin_ia32_scalefps256_mask">,
      DefaultAttrsIntrinsic<[llvm_v8f32_ty], [llvm_v8f32_ty, llvm_v8f32_ty,
                             llvm_v8f32_ty, llvm_i8_ty], [IntrNoMem]>;
  def int_x86_avx512_mask_scalef_ps_512 : ClangBuiltin<"__builtin_ia32_scalefps512_mask">,
      DefaultAttrsIntrinsic<[llvm_v16f32_ty],
                            [llvm_v16f32_ty, llvm_v16f32_ty, llvm_v16f32_ty,
                             llvm_i16_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_sqrt_ss :
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                            [llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4f32_ty,
                             llvm_i8_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_sqrt_sd :
      DefaultAttrsIntrinsic<[llvm_v2f64_ty],
                            [llvm_v2f64_ty, llvm_v2f64_ty, llvm_v2f64_ty,
                             llvm_i8_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_sqrt_pd_512 :
      DefaultAttrsIntrinsic<[llvm_v8f64_ty], [llvm_v8f64_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_sqrt_ps_512 :
      DefaultAttrsIntrinsic<[llvm_v16f32_ty], [llvm_v16f32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_x86_avx512_mask_fixupimm_pd_128 :
         ClangBuiltin<"__builtin_ia32_fixupimmpd128_mask">,
          DefaultAttrsIntrinsic<[llvm_v2f64_ty],
          [llvm_v2f64_ty, llvm_v2f64_ty, llvm_v2i64_ty, llvm_i32_ty, llvm_i8_ty],
          [IntrNoMem, ImmArg<ArgIndex<3>>]>;
  def int_x86_avx512_maskz_fixupimm_pd_128 :
         ClangBuiltin<"__builtin_ia32_fixupimmpd128_maskz">,
          DefaultAttrsIntrinsic<[llvm_v2f64_ty],
          [llvm_v2f64_ty, llvm_v2f64_ty, llvm_v2i64_ty, llvm_i32_ty, llvm_i8_ty],
          [IntrNoMem, ImmArg<ArgIndex<3>>]>;
  def int_x86_avx512_mask_fixupimm_pd_256 :
         ClangBuiltin<"__builtin_ia32_fixupimmpd256_mask">,
          DefaultAttrsIntrinsic<[llvm_v4f64_ty],
          [llvm_v4f64_ty, llvm_v4f64_ty, llvm_v4i64_ty, llvm_i32_ty, llvm_i8_ty],
          [IntrNoMem, ImmArg<ArgIndex<3>>]>;
  def int_x86_avx512_maskz_fixupimm_pd_256 :
         ClangBuiltin<"__builtin_ia32_fixupimmpd256_maskz">,
          DefaultAttrsIntrinsic<[llvm_v4f64_ty],
          [llvm_v4f64_ty, llvm_v4f64_ty, llvm_v4i64_ty, llvm_i32_ty, llvm_i8_ty],
          [IntrNoMem, ImmArg<ArgIndex<3>>]>;
  def int_x86_avx512_mask_fixupimm_pd_512 :
         ClangBuiltin<"__builtin_ia32_fixupimmpd512_mask">,
          DefaultAttrsIntrinsic<[llvm_v8f64_ty],
          [llvm_v8f64_ty, llvm_v8f64_ty, llvm_v8i64_ty, llvm_i32_ty, llvm_i8_ty,
          llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<3>>, ImmArg<ArgIndex<5>>]>;
  def int_x86_avx512_maskz_fixupimm_pd_512 :
         ClangBuiltin<"__builtin_ia32_fixupimmpd512_maskz">,
          DefaultAttrsIntrinsic<[llvm_v8f64_ty],
          [llvm_v8f64_ty, llvm_v8f64_ty, llvm_v8i64_ty, llvm_i32_ty, llvm_i8_ty,
          llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<3>>, ImmArg<ArgIndex<5>>]>;
  def int_x86_avx512_mask_fixupimm_ps_128 :
         ClangBuiltin<"__builtin_ia32_fixupimmps128_mask">,
          DefaultAttrsIntrinsic<[llvm_v4f32_ty],
          [llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4i32_ty, llvm_i32_ty, llvm_i8_ty],
          [IntrNoMem, ImmArg<ArgIndex<3>>]>;
  def int_x86_avx512_maskz_fixupimm_ps_128 :
         ClangBuiltin<"__builtin_ia32_fixupimmps128_maskz">,
          DefaultAttrsIntrinsic<[llvm_v4f32_ty],
          [llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4i32_ty, llvm_i32_ty, llvm_i8_ty],
          [IntrNoMem, ImmArg<ArgIndex<3>>]>;
  def int_x86_avx512_mask_fixupimm_ps_256 :
         ClangBuiltin<"__builtin_ia32_fixupimmps256_mask">,
          DefaultAttrsIntrinsic<[llvm_v8f32_ty],
          [llvm_v8f32_ty, llvm_v8f32_ty, llvm_v8i32_ty, llvm_i32_ty, llvm_i8_ty],
          [IntrNoMem, ImmArg<ArgIndex<3>>]>;
  def int_x86_avx512_maskz_fixupimm_ps_256 :
         ClangBuiltin<"__builtin_ia32_fixupimmps256_maskz">,
          DefaultAttrsIntrinsic<[llvm_v8f32_ty],
          [llvm_v8f32_ty, llvm_v8f32_ty, llvm_v8i32_ty, llvm_i32_ty, llvm_i8_ty],
          [IntrNoMem, ImmArg<ArgIndex<3>>]>;
  def int_x86_avx512_mask_fixupimm_ps_512 :
         ClangBuiltin<"__builtin_ia32_fixupimmps512_mask">,
          DefaultAttrsIntrinsic<[llvm_v16f32_ty],
          [llvm_v16f32_ty, llvm_v16f32_ty, llvm_v16i32_ty, llvm_i32_ty,
          llvm_i16_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<3>>, ImmArg<ArgIndex<5>>]>;
  def int_x86_avx512_maskz_fixupimm_ps_512 :
         ClangBuiltin<"__builtin_ia32_fixupimmps512_maskz">,
          DefaultAttrsIntrinsic<[llvm_v16f32_ty],
          [llvm_v16f32_ty, llvm_v16f32_ty, llvm_v16i32_ty, llvm_i32_ty,
          llvm_i16_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<3>>, ImmArg<ArgIndex<5>>]>;
  def int_x86_avx512_mask_fixupimm_sd :
         ClangBuiltin<"__builtin_ia32_fixupimmsd_mask">,
          DefaultAttrsIntrinsic<[llvm_v2f64_ty],
          [llvm_v2f64_ty, llvm_v2f64_ty, llvm_v2i64_ty, llvm_i32_ty, llvm_i8_ty,
          llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<3>>, ImmArg<ArgIndex<5>>]>;
  def int_x86_avx512_maskz_fixupimm_sd :
         ClangBuiltin<"__builtin_ia32_fixupimmsd_maskz">,
          DefaultAttrsIntrinsic<[llvm_v2f64_ty],
          [llvm_v2f64_ty, llvm_v2f64_ty, llvm_v2i64_ty, llvm_i32_ty, llvm_i8_ty,
          llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<3>>, ImmArg<ArgIndex<5>>]>;
  def int_x86_avx512_mask_fixupimm_ss :
         ClangBuiltin<"__builtin_ia32_fixupimmss_mask">,
          DefaultAttrsIntrinsic<[llvm_v4f32_ty],
          [llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4i32_ty, llvm_i32_ty, llvm_i8_ty,
          llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<3>>, ImmArg<ArgIndex<5>>]>;
  def int_x86_avx512_maskz_fixupimm_ss :
         ClangBuiltin<"__builtin_ia32_fixupimmss_maskz">,
          DefaultAttrsIntrinsic<[llvm_v4f32_ty],
          [llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4i32_ty, llvm_i32_ty, llvm_i8_ty,
          llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<3>>, ImmArg<ArgIndex<5>>]>;
  def int_x86_avx512_mask_getexp_pd_128 : ClangBuiltin<"__builtin_ia32_getexppd128_mask">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty],
                            [llvm_v2f64_ty, llvm_v2f64_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_getexp_pd_256 : ClangBuiltin<"__builtin_ia32_getexppd256_mask">,
      DefaultAttrsIntrinsic<[llvm_v4f64_ty],
                            [llvm_v4f64_ty, llvm_v4f64_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_getexp_pd_512 : ClangBuiltin<"__builtin_ia32_getexppd512_mask">,
      DefaultAttrsIntrinsic<[llvm_v8f64_ty],
                            [llvm_v8f64_ty, llvm_v8f64_ty, llvm_i8_ty,
                             llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<3>>]>;
  def int_x86_avx512_mask_getexp_ps_128 : ClangBuiltin<"__builtin_ia32_getexpps128_mask">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                            [llvm_v4f32_ty, llvm_v4f32_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_getexp_ps_256 : ClangBuiltin<"__builtin_ia32_getexpps256_mask">,
      DefaultAttrsIntrinsic<[llvm_v8f32_ty],
                            [llvm_v8f32_ty, llvm_v8f32_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_getexp_ps_512 : ClangBuiltin<"__builtin_ia32_getexpps512_mask">,
      DefaultAttrsIntrinsic<[llvm_v16f32_ty],
                            [llvm_v16f32_ty, llvm_v16f32_ty, llvm_i16_ty,
                             llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  def int_x86_avx512_mask_getexp_ss : ClangBuiltin<"__builtin_ia32_getexpss128_round_mask">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                            [llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4f32_ty,
                             llvm_i8_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_getexp_sd : ClangBuiltin<"__builtin_ia32_getexpsd128_round_mask">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty],
                            [llvm_v2f64_ty, llvm_v2f64_ty, llvm_v2f64_ty,
                             llvm_i8_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_getmant_pd_128 :
         ClangBuiltin<"__builtin_ia32_getmantpd128_mask">,
          DefaultAttrsIntrinsic<[llvm_v2f64_ty],
          [llvm_v2f64_ty,llvm_i32_ty, llvm_v2f64_ty,  llvm_i8_ty],
          [IntrNoMem, ImmArg<ArgIndex<1>>]>;

  def int_x86_avx512_mask_getmant_pd_256 :
         ClangBuiltin<"__builtin_ia32_getmantpd256_mask">,
          DefaultAttrsIntrinsic<[llvm_v4f64_ty],
          [llvm_v4f64_ty,llvm_i32_ty, llvm_v4f64_ty,  llvm_i8_ty],
          [IntrNoMem, ImmArg<ArgIndex<1>>]>;

  def int_x86_avx512_mask_getmant_pd_512 :
         ClangBuiltin<"__builtin_ia32_getmantpd512_mask">,
          DefaultAttrsIntrinsic<[llvm_v8f64_ty],
          [llvm_v8f64_ty,llvm_i32_ty, llvm_v8f64_ty,  llvm_i8_ty,llvm_i32_ty ],
          [IntrNoMem, ImmArg<ArgIndex<1>>, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_getmant_ps_128 :
         ClangBuiltin<"__builtin_ia32_getmantps128_mask">,
          DefaultAttrsIntrinsic<[llvm_v4f32_ty],
          [llvm_v4f32_ty, llvm_i32_ty, llvm_v4f32_ty,  llvm_i8_ty],
          [IntrNoMem, ImmArg<ArgIndex<1>>]>;

  def int_x86_avx512_mask_getmant_ps_256 :
         ClangBuiltin<"__builtin_ia32_getmantps256_mask">,
          DefaultAttrsIntrinsic<[llvm_v8f32_ty],
          [llvm_v8f32_ty, llvm_i32_ty, llvm_v8f32_ty,  llvm_i8_ty],
          [IntrNoMem, ImmArg<ArgIndex<1>>]>;

  def int_x86_avx512_mask_getmant_ps_512 :
         ClangBuiltin<"__builtin_ia32_getmantps512_mask">,
          DefaultAttrsIntrinsic<[llvm_v16f32_ty],
          [llvm_v16f32_ty,llvm_i32_ty, llvm_v16f32_ty,llvm_i16_ty,llvm_i32_ty],
          [IntrNoMem, ImmArg<ArgIndex<1>>, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_getmant_ss :
         ClangBuiltin<"__builtin_ia32_getmantss_round_mask">,
          DefaultAttrsIntrinsic<[llvm_v4f32_ty],
          [llvm_v4f32_ty, llvm_v4f32_ty, llvm_i32_ty, llvm_v4f32_ty,
           llvm_i8_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<5>>]>;

  def int_x86_avx512_mask_getmant_sd :
         ClangBuiltin<"__builtin_ia32_getmantsd_round_mask">,
          DefaultAttrsIntrinsic<[llvm_v2f64_ty],
          [llvm_v2f64_ty, llvm_v2f64_ty, llvm_i32_ty, llvm_v2f64_ty,
           llvm_i8_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<5>>]>;

  def int_x86_avx512_rsqrt14_ss : ClangBuiltin<"__builtin_ia32_rsqrt14ss_mask">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                            [llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4f32_ty,
                             llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_rsqrt14_sd : ClangBuiltin<"__builtin_ia32_rsqrt14sd_mask">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty],
                            [llvm_v2f64_ty, llvm_v2f64_ty, llvm_v2f64_ty,
                             llvm_i8_ty],
                            [IntrNoMem]>;

  def int_x86_avx512_rsqrt14_pd_128 : ClangBuiltin<"__builtin_ia32_rsqrt14pd128_mask">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty],
                            [llvm_v2f64_ty, llvm_v2f64_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_rsqrt14_pd_256 : ClangBuiltin<"__builtin_ia32_rsqrt14pd256_mask">,
      DefaultAttrsIntrinsic<[llvm_v4f64_ty],
                            [llvm_v4f64_ty, llvm_v4f64_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_rsqrt14_pd_512 : ClangBuiltin<"__builtin_ia32_rsqrt14pd512_mask">,
      DefaultAttrsIntrinsic<[llvm_v8f64_ty],
                            [llvm_v8f64_ty, llvm_v8f64_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_rsqrt14_ps_128 : ClangBuiltin<"__builtin_ia32_rsqrt14ps128_mask">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                            [llvm_v4f32_ty, llvm_v4f32_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_rsqrt14_ps_256 : ClangBuiltin<"__builtin_ia32_rsqrt14ps256_mask">,
      DefaultAttrsIntrinsic<[llvm_v8f32_ty],
                            [llvm_v8f32_ty, llvm_v8f32_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_rsqrt14_ps_512 : ClangBuiltin<"__builtin_ia32_rsqrt14ps512_mask">,
      DefaultAttrsIntrinsic<[llvm_v16f32_ty],
                            [llvm_v16f32_ty, llvm_v16f32_ty, llvm_i16_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_rcp14_ss : ClangBuiltin<"__builtin_ia32_rcp14ss_mask">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                            [llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4f32_ty,
                             llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_rcp14_sd : ClangBuiltin<"__builtin_ia32_rcp14sd_mask">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty],
                            [llvm_v2f64_ty, llvm_v2f64_ty, llvm_v2f64_ty,
                             llvm_i8_ty],
                            [IntrNoMem]>;

  def int_x86_avx512_rcp14_pd_128 : ClangBuiltin<"__builtin_ia32_rcp14pd128_mask">,
      DefaultAttrsIntrinsic<[llvm_v2f64_ty],
                            [llvm_v2f64_ty, llvm_v2f64_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_rcp14_pd_256 : ClangBuiltin<"__builtin_ia32_rcp14pd256_mask">,
      DefaultAttrsIntrinsic<[llvm_v4f64_ty],
                            [llvm_v4f64_ty, llvm_v4f64_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_rcp14_pd_512 : ClangBuiltin<"__builtin_ia32_rcp14pd512_mask">,
      DefaultAttrsIntrinsic<[llvm_v8f64_ty],
                            [llvm_v8f64_ty, llvm_v8f64_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_rcp14_ps_128 : ClangBuiltin<"__builtin_ia32_rcp14ps128_mask">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                            [llvm_v4f32_ty, llvm_v4f32_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_rcp14_ps_256 : ClangBuiltin<"__builtin_ia32_rcp14ps256_mask">,
      DefaultAttrsIntrinsic<[llvm_v8f32_ty],
                            [llvm_v8f32_ty, llvm_v8f32_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_rcp14_ps_512 : ClangBuiltin<"__builtin_ia32_rcp14ps512_mask">,
      DefaultAttrsIntrinsic<[llvm_v16f32_ty],
                            [llvm_v16f32_ty, llvm_v16f32_ty, llvm_i16_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_psad_bw_512 : ClangBuiltin<"__builtin_ia32_psadbw512">,
      DefaultAttrsIntrinsic<[llvm_v8i64_ty], [llvm_v64i8_ty, llvm_v64i8_ty],
                            [IntrNoMem, Commutative]>;
}
// Integer arithmetic ops
let TargetPrefix = "x86" in {
  def int_x86_avx512_pmulhu_w_512 : ClangBuiltin<"__builtin_ia32_pmulhuw512">,
      DefaultAttrsIntrinsic<[llvm_v32i16_ty], [llvm_v32i16_ty, llvm_v32i16_ty],
                            [IntrNoMem, Commutative]>;
  def int_x86_avx512_pmulh_w_512 : ClangBuiltin<"__builtin_ia32_pmulhw512">,
      DefaultAttrsIntrinsic<[llvm_v32i16_ty], [llvm_v32i16_ty, llvm_v32i16_ty],
                            [IntrNoMem, Commutative]>;
  def int_x86_avx512_pavg_b_512 : ClangBuiltin<"__builtin_ia32_pavgb512">,
      DefaultAttrsIntrinsic<[llvm_v64i8_ty], [llvm_v64i8_ty, llvm_v64i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_pavg_w_512 : ClangBuiltin<"__builtin_ia32_pavgw512">,
      DefaultAttrsIntrinsic<[llvm_v32i16_ty], [llvm_v32i16_ty, llvm_v32i16_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_pmaddw_d_512 : ClangBuiltin<"__builtin_ia32_pmaddwd512">,
      DefaultAttrsIntrinsic<[llvm_v16i32_ty], [llvm_v32i16_ty, llvm_v32i16_ty],
                            [IntrNoMem, Commutative]>;
  def int_x86_avx512_pmaddubs_w_512 : ClangBuiltin<"__builtin_ia32_pmaddubsw512">,
      DefaultAttrsIntrinsic<[llvm_v32i16_ty], [llvm_v64i8_ty, llvm_v64i8_ty],
                            [IntrNoMem]>;

  def int_x86_avx512_dbpsadbw_128 :
      ClangBuiltin<"__builtin_ia32_dbpsadbw128">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty],
                            [llvm_v16i8_ty, llvm_v16i8_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>]>;

  def int_x86_avx512_dbpsadbw_256 :
      ClangBuiltin<"__builtin_ia32_dbpsadbw256">,
      DefaultAttrsIntrinsic<[llvm_v16i16_ty],
                            [llvm_v32i8_ty, llvm_v32i8_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>]>;

  def int_x86_avx512_dbpsadbw_512 :
      ClangBuiltin<"__builtin_ia32_dbpsadbw512">,
      DefaultAttrsIntrinsic<[llvm_v32i16_ty],
                            [llvm_v64i8_ty, llvm_v64i8_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
}

// Gather and Scatter ops
let TargetPrefix = "x86" in {
  // NOTE: These are deprecated in favor of the versions that take a vXi1 mask.
  // NOTE: These can't be ArgMemOnly because you can put the address completely
  // in the index register.
  def int_x86_avx512_gather_dpd_512  :
      DefaultAttrsIntrinsic<[llvm_v8f64_ty], [llvm_v8f64_ty, llvm_ptr_ty,
                             llvm_v8i32_ty, llvm_i8_ty, llvm_i32_ty],
                            [IntrReadMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_gather_dps_512  :
      DefaultAttrsIntrinsic<[llvm_v16f32_ty], [llvm_v16f32_ty, llvm_ptr_ty,
                             llvm_v16i32_ty, llvm_i16_ty, llvm_i32_ty],
                            [IntrReadMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_gather_qpd_512  :
      DefaultAttrsIntrinsic<[llvm_v8f64_ty], [llvm_v8f64_ty, llvm_ptr_ty,
                             llvm_v8i64_ty, llvm_i8_ty, llvm_i32_ty],
                            [IntrReadMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_gather_qps_512  :
      DefaultAttrsIntrinsic<[llvm_v8f32_ty], [llvm_v8f32_ty, llvm_ptr_ty,
                             llvm_v8i64_ty, llvm_i8_ty, llvm_i32_ty],
                            [IntrReadMem, ImmArg<ArgIndex<4>>]>;


  def int_x86_avx512_gather_dpq_512  :
      DefaultAttrsIntrinsic<[llvm_v8i64_ty], [llvm_v8i64_ty, llvm_ptr_ty,
                             llvm_v8i32_ty, llvm_i8_ty, llvm_i32_ty],
                            [IntrReadMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_gather_dpi_512  :
      DefaultAttrsIntrinsic<[llvm_v16i32_ty], [llvm_v16i32_ty, llvm_ptr_ty,
                             llvm_v16i32_ty, llvm_i16_ty, llvm_i32_ty],
                            [IntrReadMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_gather_qpq_512  :
      DefaultAttrsIntrinsic<[llvm_v8i64_ty], [llvm_v8i64_ty, llvm_ptr_ty,
                             llvm_v8i64_ty, llvm_i8_ty, llvm_i32_ty],
                            [IntrReadMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_gather_qpi_512  :
      DefaultAttrsIntrinsic<[llvm_v8i32_ty], [llvm_v8i32_ty, llvm_ptr_ty,
                             llvm_v8i64_ty, llvm_i8_ty, llvm_i32_ty],
                            [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_gather3div2_df :
      DefaultAttrsIntrinsic<[llvm_v2f64_ty],
          [llvm_v2f64_ty, llvm_ptr_ty, llvm_v2i64_ty, llvm_i8_ty, llvm_i32_ty],
          [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_gather3div2_di :
      DefaultAttrsIntrinsic<[llvm_v2i64_ty],
          [llvm_v2i64_ty, llvm_ptr_ty, llvm_v2i64_ty, llvm_i8_ty, llvm_i32_ty],
          [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_gather3div4_df :
      DefaultAttrsIntrinsic<[llvm_v4f64_ty],
          [llvm_v4f64_ty, llvm_ptr_ty, llvm_v4i64_ty, llvm_i8_ty, llvm_i32_ty],
          [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_gather3div4_di :
      DefaultAttrsIntrinsic<[llvm_v4i64_ty],
          [llvm_v4i64_ty, llvm_ptr_ty, llvm_v4i64_ty, llvm_i8_ty, llvm_i32_ty],
          [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_gather3div4_sf :
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
          [llvm_v4f32_ty, llvm_ptr_ty, llvm_v2i64_ty, llvm_i8_ty, llvm_i32_ty],
          [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_gather3div4_si :
      DefaultAttrsIntrinsic<[llvm_v4i32_ty],
          [llvm_v4i32_ty, llvm_ptr_ty, llvm_v2i64_ty, llvm_i8_ty, llvm_i32_ty],
          [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_gather3div8_sf :
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
          [llvm_v4f32_ty, llvm_ptr_ty, llvm_v4i64_ty, llvm_i8_ty, llvm_i32_ty],
          [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_gather3div8_si :
      DefaultAttrsIntrinsic<[llvm_v4i32_ty],
          [llvm_v4i32_ty, llvm_ptr_ty, llvm_v4i64_ty, llvm_i8_ty, llvm_i32_ty],
          [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_gather3siv2_df :
      DefaultAttrsIntrinsic<[llvm_v2f64_ty],
          [llvm_v2f64_ty, llvm_ptr_ty, llvm_v4i32_ty, llvm_i8_ty, llvm_i32_ty],
          [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_gather3siv2_di:
      DefaultAttrsIntrinsic<[llvm_v2i64_ty],
          [llvm_v2i64_ty, llvm_ptr_ty, llvm_v4i32_ty, llvm_i8_ty, llvm_i32_ty],
          [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_gather3siv4_df :
      DefaultAttrsIntrinsic<[llvm_v4f64_ty],
          [llvm_v4f64_ty, llvm_ptr_ty, llvm_v4i32_ty, llvm_i8_ty, llvm_i32_ty],
          [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_gather3siv4_di :
      DefaultAttrsIntrinsic<[llvm_v4i64_ty],
          [llvm_v4i64_ty, llvm_ptr_ty, llvm_v4i32_ty, llvm_i8_ty, llvm_i32_ty],
          [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_gather3siv4_sf :
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
          [llvm_v4f32_ty, llvm_ptr_ty, llvm_v4i32_ty, llvm_i8_ty, llvm_i32_ty],
          [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_gather3siv4_si :
      DefaultAttrsIntrinsic<[llvm_v4i32_ty],
          [llvm_v4i32_ty, llvm_ptr_ty, llvm_v4i32_ty, llvm_i8_ty, llvm_i32_ty],
          [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_gather3siv8_sf :
      DefaultAttrsIntrinsic<[llvm_v8f32_ty],
          [llvm_v8f32_ty, llvm_ptr_ty, llvm_v8i32_ty, llvm_i8_ty, llvm_i32_ty],
          [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_gather3siv8_si :
      DefaultAttrsIntrinsic<[llvm_v8i32_ty],
          [llvm_v8i32_ty, llvm_ptr_ty, llvm_v8i32_ty, llvm_i8_ty, llvm_i32_ty],
          [IntrReadMem, ImmArg<ArgIndex<4>>]>;

// scatter
  // NOTE: These are deprecated in favor of the versions that take a vXi1 mask.
  // NOTE: These can't be ArgMemOnly because you can put the address completely
  // in the index register.
  def int_x86_avx512_scatter_dpd_512  :
          Intrinsic<[], [llvm_ptr_ty, llvm_i8_ty,
                        llvm_v8i32_ty, llvm_v8f64_ty, llvm_i32_ty],
                    [ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_scatter_dps_512  :
          Intrinsic<[], [llvm_ptr_ty, llvm_i16_ty,
                       llvm_v16i32_ty, llvm_v16f32_ty, llvm_i32_ty],
                    [ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_scatter_qpd_512  :
          Intrinsic<[], [llvm_ptr_ty, llvm_i8_ty,
                     llvm_v8i64_ty, llvm_v8f64_ty, llvm_i32_ty],
                    [ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_scatter_qps_512  :
          Intrinsic<[], [llvm_ptr_ty, llvm_i8_ty,
                     llvm_v8i64_ty, llvm_v8f32_ty, llvm_i32_ty],
                    [ImmArg<ArgIndex<4>>]>;


  def int_x86_avx512_scatter_dpq_512  :
          Intrinsic<[], [llvm_ptr_ty, llvm_i8_ty,
                         llvm_v8i32_ty, llvm_v8i64_ty, llvm_i32_ty],
                    [ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_scatter_dpi_512  :
          Intrinsic<[], [llvm_ptr_ty, llvm_i16_ty,
                     llvm_v16i32_ty, llvm_v16i32_ty, llvm_i32_ty],
                    [ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_scatter_qpq_512  :
          Intrinsic<[], [llvm_ptr_ty, llvm_i8_ty,llvm_v8i64_ty, llvm_v8i64_ty,
                         llvm_i32_ty],
                    [ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_scatter_qpi_512  :
          Intrinsic<[], [llvm_ptr_ty, llvm_i8_ty, llvm_v8i64_ty, llvm_v8i32_ty,
                         llvm_i32_ty],
                    [ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_scatterdiv2_df :
        Intrinsic<[],
        [llvm_ptr_ty, llvm_i8_ty, llvm_v2i64_ty, llvm_v2f64_ty, llvm_i32_ty],
        [ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_scatterdiv2_di :
          Intrinsic<[],
          [llvm_ptr_ty, llvm_i8_ty, llvm_v2i64_ty, llvm_v2i64_ty, llvm_i32_ty],
          [ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_scatterdiv4_df :
          Intrinsic<[],
          [llvm_ptr_ty, llvm_i8_ty, llvm_v4i64_ty, llvm_v4f64_ty, llvm_i32_ty],
          [ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_scatterdiv4_di :
          Intrinsic<[],
          [llvm_ptr_ty, llvm_i8_ty, llvm_v4i64_ty, llvm_v4i64_ty, llvm_i32_ty],
          [ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_scatterdiv4_sf :
          Intrinsic<[],
          [llvm_ptr_ty, llvm_i8_ty, llvm_v2i64_ty, llvm_v4f32_ty, llvm_i32_ty],
          [ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_scatterdiv4_si :
          Intrinsic<[],
          [llvm_ptr_ty, llvm_i8_ty, llvm_v2i64_ty, llvm_v4i32_ty, llvm_i32_ty],
          [ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_scatterdiv8_sf :
          Intrinsic<[],
          [llvm_ptr_ty, llvm_i8_ty, llvm_v4i64_ty, llvm_v4f32_ty, llvm_i32_ty],
          [ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_scatterdiv8_si :
          Intrinsic<[],
          [llvm_ptr_ty, llvm_i8_ty, llvm_v4i64_ty, llvm_v4i32_ty, llvm_i32_ty],
          [ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_scattersiv2_df :
          Intrinsic<[],
          [llvm_ptr_ty, llvm_i8_ty, llvm_v4i32_ty, llvm_v2f64_ty, llvm_i32_ty],
          [ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_scattersiv2_di :
          Intrinsic<[],
          [llvm_ptr_ty, llvm_i8_ty, llvm_v4i32_ty, llvm_v2i64_ty, llvm_i32_ty],
          [ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_scattersiv4_df :
          Intrinsic<[],
          [llvm_ptr_ty, llvm_i8_ty, llvm_v4i32_ty, llvm_v4f64_ty, llvm_i32_ty],
          [ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_scattersiv4_di :
          Intrinsic<[],
          [llvm_ptr_ty, llvm_i8_ty, llvm_v4i32_ty, llvm_v4i64_ty, llvm_i32_ty],
          [ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_scattersiv4_sf :
          Intrinsic<[],
          [llvm_ptr_ty, llvm_i8_ty, llvm_v4i32_ty, llvm_v4f32_ty, llvm_i32_ty],
          [ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_scattersiv4_si :
          Intrinsic<[],
          [llvm_ptr_ty, llvm_i8_ty, llvm_v4i32_ty, llvm_v4i32_ty, llvm_i32_ty],
          [ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_scattersiv8_sf :
          Intrinsic<[],
          [llvm_ptr_ty, llvm_i8_ty, llvm_v8i32_ty, llvm_v8f32_ty, llvm_i32_ty],
          [ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_scattersiv8_si :
          Intrinsic<[],
          [llvm_ptr_ty, llvm_i8_ty, llvm_v8i32_ty, llvm_v8i32_ty, llvm_i32_ty],
          [ImmArg<ArgIndex<4>>]>;
}

// AVX512 gather/scatter intrinsics that use vXi1 masks.
let TargetPrefix = "x86" in {
  // NOTE: These can't be ArgMemOnly because you can put the address completely
  // in the index register.
  def int_x86_avx512_mask_gather_dpd_512  :
      DefaultAttrsIntrinsic<[llvm_v8f64_ty], [llvm_v8f64_ty, llvm_ptr_ty,
                             llvm_v8i32_ty, llvm_v8i1_ty, llvm_i32_ty],
                            [IntrReadMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_gather_dps_512  :
      DefaultAttrsIntrinsic<[llvm_v16f32_ty], [llvm_v16f32_ty, llvm_ptr_ty,
                             llvm_v16i32_ty, llvm_v16i1_ty, llvm_i32_ty],
                            [IntrReadMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_gather_qpd_512  :
      DefaultAttrsIntrinsic<[llvm_v8f64_ty], [llvm_v8f64_ty, llvm_ptr_ty,
                             llvm_v8i64_ty, llvm_v8i1_ty, llvm_i32_ty],
                            [IntrReadMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_gather_qps_512  :
      DefaultAttrsIntrinsic<[llvm_v8f32_ty], [llvm_v8f32_ty, llvm_ptr_ty,
                             llvm_v8i64_ty, llvm_v8i1_ty, llvm_i32_ty],
                            [IntrReadMem, ImmArg<ArgIndex<4>>]>;


  def int_x86_avx512_mask_gather_dpq_512  :
      DefaultAttrsIntrinsic<[llvm_v8i64_ty], [llvm_v8i64_ty, llvm_ptr_ty,
                             llvm_v8i32_ty, llvm_v8i1_ty, llvm_i32_ty],
                            [IntrReadMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_gather_dpi_512  :
      DefaultAttrsIntrinsic<[llvm_v16i32_ty], [llvm_v16i32_ty, llvm_ptr_ty,
                             llvm_v16i32_ty, llvm_v16i1_ty, llvm_i32_ty],
                            [IntrReadMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_gather_qpq_512  :
      DefaultAttrsIntrinsic<[llvm_v8i64_ty], [llvm_v8i64_ty, llvm_ptr_ty,
                             llvm_v8i64_ty, llvm_v8i1_ty, llvm_i32_ty],
                            [IntrReadMem, ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_gather_qpi_512  :
      DefaultAttrsIntrinsic<[llvm_v8i32_ty], [llvm_v8i32_ty, llvm_ptr_ty,
                             llvm_v8i64_ty, llvm_v8i1_ty, llvm_i32_ty],
                            [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_gather3div2_df :
      DefaultAttrsIntrinsic<[llvm_v2f64_ty],
          [llvm_v2f64_ty, llvm_ptr_ty, llvm_v2i64_ty, llvm_v2i1_ty, llvm_i32_ty],
          [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_gather3div2_di :
      DefaultAttrsIntrinsic<[llvm_v2i64_ty],
          [llvm_v2i64_ty, llvm_ptr_ty, llvm_v2i64_ty, llvm_v2i1_ty, llvm_i32_ty],
          [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_gather3div4_df :
      DefaultAttrsIntrinsic<[llvm_v4f64_ty],
          [llvm_v4f64_ty, llvm_ptr_ty, llvm_v4i64_ty, llvm_v4i1_ty, llvm_i32_ty],
          [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_gather3div4_di :
      DefaultAttrsIntrinsic<[llvm_v4i64_ty],
          [llvm_v4i64_ty, llvm_ptr_ty, llvm_v4i64_ty, llvm_v4i1_ty, llvm_i32_ty],
          [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_gather3div4_sf :
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
          [llvm_v4f32_ty, llvm_ptr_ty, llvm_v2i64_ty, llvm_v2i1_ty, llvm_i32_ty],
          [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_gather3div4_si :
      DefaultAttrsIntrinsic<[llvm_v4i32_ty],
          [llvm_v4i32_ty, llvm_ptr_ty, llvm_v2i64_ty, llvm_v2i1_ty, llvm_i32_ty],
          [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_gather3div8_sf :
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
          [llvm_v4f32_ty, llvm_ptr_ty, llvm_v4i64_ty, llvm_v4i1_ty, llvm_i32_ty],
          [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_gather3div8_si :
      DefaultAttrsIntrinsic<[llvm_v4i32_ty],
          [llvm_v4i32_ty, llvm_ptr_ty, llvm_v4i64_ty, llvm_v4i1_ty, llvm_i32_ty],
          [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_gather3siv2_df :
      DefaultAttrsIntrinsic<[llvm_v2f64_ty],
          [llvm_v2f64_ty, llvm_ptr_ty, llvm_v4i32_ty, llvm_v2i1_ty, llvm_i32_ty],
          [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_gather3siv2_di :
      DefaultAttrsIntrinsic<[llvm_v2i64_ty],
          [llvm_v2i64_ty, llvm_ptr_ty, llvm_v4i32_ty, llvm_v2i1_ty, llvm_i32_ty],
          [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_gather3siv4_df :
      DefaultAttrsIntrinsic<[llvm_v4f64_ty],
          [llvm_v4f64_ty, llvm_ptr_ty, llvm_v4i32_ty, llvm_v4i1_ty, llvm_i32_ty],
          [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_gather3siv4_di :
      DefaultAttrsIntrinsic<[llvm_v4i64_ty],
          [llvm_v4i64_ty, llvm_ptr_ty, llvm_v4i32_ty, llvm_v4i1_ty, llvm_i32_ty],
          [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_gather3siv4_sf :
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
          [llvm_v4f32_ty, llvm_ptr_ty, llvm_v4i32_ty, llvm_v4i1_ty, llvm_i32_ty],
          [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_gather3siv4_si :
      DefaultAttrsIntrinsic<[llvm_v4i32_ty],
          [llvm_v4i32_ty, llvm_ptr_ty, llvm_v4i32_ty, llvm_v4i1_ty, llvm_i32_ty],
          [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_gather3siv8_sf :
      DefaultAttrsIntrinsic<[llvm_v8f32_ty],
          [llvm_v8f32_ty, llvm_ptr_ty, llvm_v8i32_ty, llvm_v8i1_ty, llvm_i32_ty],
          [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_gather3siv8_si :
      DefaultAttrsIntrinsic<[llvm_v8i32_ty],
          [llvm_v8i32_ty, llvm_ptr_ty, llvm_v8i32_ty, llvm_v8i1_ty, llvm_i32_ty],
          [IntrReadMem, ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_scatter_dpd_512  :
          Intrinsic<[], [llvm_ptr_ty, llvm_v8i1_ty,
                        llvm_v8i32_ty, llvm_v8f64_ty, llvm_i32_ty],
                    [ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_scatter_dps_512  :
          Intrinsic<[], [llvm_ptr_ty, llvm_v16i1_ty,
                       llvm_v16i32_ty, llvm_v16f32_ty, llvm_i32_ty],
                    [ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_scatter_qpd_512  :
          Intrinsic<[], [llvm_ptr_ty, llvm_v8i1_ty,
                     llvm_v8i64_ty, llvm_v8f64_ty, llvm_i32_ty],
                    [ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_scatter_qps_512  :
          Intrinsic<[], [llvm_ptr_ty, llvm_v8i1_ty,
                     llvm_v8i64_ty, llvm_v8f32_ty, llvm_i32_ty],
                    [ImmArg<ArgIndex<4>>]>;


  // NOTE: These can't be ArgMemOnly because you can put the address completely
  // in the index register.
  def int_x86_avx512_mask_scatter_dpq_512  :
          Intrinsic<[], [llvm_ptr_ty, llvm_v8i1_ty,
                         llvm_v8i32_ty, llvm_v8i64_ty, llvm_i32_ty],
                    [ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_scatter_dpi_512  :
          Intrinsic<[], [llvm_ptr_ty, llvm_v16i1_ty,
                     llvm_v16i32_ty, llvm_v16i32_ty, llvm_i32_ty],
                    [ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_scatter_qpq_512  :
          Intrinsic<[], [llvm_ptr_ty, llvm_v8i1_ty,llvm_v8i64_ty, llvm_v8i64_ty,
                         llvm_i32_ty],
                    [ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_scatter_qpi_512  :
          Intrinsic<[], [llvm_ptr_ty, llvm_v8i1_ty, llvm_v8i64_ty, llvm_v8i32_ty,
                         llvm_i32_ty],
                    [ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_scatterdiv2_df :
        Intrinsic<[],
        [llvm_ptr_ty, llvm_v2i1_ty, llvm_v2i64_ty, llvm_v2f64_ty, llvm_i32_ty],
        [ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_scatterdiv2_di :
          Intrinsic<[],
          [llvm_ptr_ty, llvm_v2i1_ty, llvm_v2i64_ty, llvm_v2i64_ty, llvm_i32_ty],
          [ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_scatterdiv4_df :
          Intrinsic<[],
          [llvm_ptr_ty, llvm_v4i1_ty, llvm_v4i64_ty, llvm_v4f64_ty, llvm_i32_ty],
          [ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_scatterdiv4_di :
          Intrinsic<[],
          [llvm_ptr_ty, llvm_v4i1_ty, llvm_v4i64_ty, llvm_v4i64_ty, llvm_i32_ty],
          [ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_scatterdiv4_sf :
          Intrinsic<[],
          [llvm_ptr_ty, llvm_v2i1_ty, llvm_v2i64_ty, llvm_v4f32_ty, llvm_i32_ty],
          [ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_scatterdiv4_si :
          Intrinsic<[],
          [llvm_ptr_ty, llvm_v2i1_ty, llvm_v2i64_ty, llvm_v4i32_ty, llvm_i32_ty],
          [ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_scatterdiv8_sf :
          Intrinsic<[],
          [llvm_ptr_ty, llvm_v4i1_ty, llvm_v4i64_ty, llvm_v4f32_ty, llvm_i32_ty],
          [ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_scatterdiv8_si :
          Intrinsic<[],
          [llvm_ptr_ty, llvm_v4i1_ty, llvm_v4i64_ty, llvm_v4i32_ty, llvm_i32_ty],
          [ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_scattersiv2_df :
          Intrinsic<[],
          [llvm_ptr_ty, llvm_v2i1_ty, llvm_v4i32_ty, llvm_v2f64_ty, llvm_i32_ty],
          [ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_scattersiv2_di :
          Intrinsic<[],
          [llvm_ptr_ty, llvm_v2i1_ty, llvm_v4i32_ty, llvm_v2i64_ty, llvm_i32_ty],
          [ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_scattersiv4_df :
          Intrinsic<[],
          [llvm_ptr_ty, llvm_v4i1_ty, llvm_v4i32_ty, llvm_v4f64_ty, llvm_i32_ty],
          [ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_scattersiv4_di :
          Intrinsic<[],
          [llvm_ptr_ty, llvm_v4i1_ty, llvm_v4i32_ty, llvm_v4i64_ty, llvm_i32_ty],
          [ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_scattersiv4_sf :
          Intrinsic<[],
          [llvm_ptr_ty, llvm_v4i1_ty, llvm_v4i32_ty, llvm_v4f32_ty, llvm_i32_ty],
          [ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_scattersiv4_si :
          Intrinsic<[],
          [llvm_ptr_ty, llvm_v4i1_ty, llvm_v4i32_ty, llvm_v4i32_ty, llvm_i32_ty],
          [ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_scattersiv8_sf :
          Intrinsic<[],
          [llvm_ptr_ty, llvm_v8i1_ty, llvm_v8i32_ty, llvm_v8f32_ty, llvm_i32_ty],
          [ImmArg<ArgIndex<4>>]>;

  def int_x86_avx512_mask_scattersiv8_si :
          Intrinsic<[],
          [llvm_ptr_ty, llvm_v8i1_ty, llvm_v8i32_ty, llvm_v8i32_ty, llvm_i32_ty],
          [ImmArg<ArgIndex<4>>]>;
}

// AVX-512 conflict detection instruction
// Instructions that count the number of leading zero bits
let TargetPrefix = "x86" in {
  def int_x86_avx512_conflict_d_128 :
          ClangBuiltin<"__builtin_ia32_vpconflictsi_128">,
          DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty], [IntrNoMem]>;
  def int_x86_avx512_conflict_d_256 :
          ClangBuiltin<"__builtin_ia32_vpconflictsi_256">,
          DefaultAttrsIntrinsic<[llvm_v8i32_ty], [llvm_v8i32_ty], [IntrNoMem]>;
  def int_x86_avx512_conflict_d_512 :
          ClangBuiltin<"__builtin_ia32_vpconflictsi_512">,
          DefaultAttrsIntrinsic<[llvm_v16i32_ty], [llvm_v16i32_ty],
                                [IntrNoMem]>;

  def int_x86_avx512_conflict_q_128 :
          ClangBuiltin<"__builtin_ia32_vpconflictdi_128">,
          DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty], [IntrNoMem]>;
  def int_x86_avx512_conflict_q_256 :
          ClangBuiltin<"__builtin_ia32_vpconflictdi_256">,
          DefaultAttrsIntrinsic<[llvm_v4i64_ty], [llvm_v4i64_ty], [IntrNoMem]>;
  def int_x86_avx512_conflict_q_512 :
          ClangBuiltin<"__builtin_ia32_vpconflictdi_512">,
          DefaultAttrsIntrinsic<[llvm_v8i64_ty], [llvm_v8i64_ty], [IntrNoMem]>;
}

// Compares
let TargetPrefix = "x86" in {
  // 512-bit
  def int_x86_avx512_vcomi_sd : ClangBuiltin<"__builtin_ia32_vcomisd">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v2f64_ty,
                             llvm_v2f64_ty, llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>,
                             ImmArg<ArgIndex<3>>]>;
  def int_x86_avx512_vcomi_ss : ClangBuiltin<"__builtin_ia32_vcomiss">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v4f32_ty,
                             llvm_v4f32_ty, llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>,
                             ImmArg<ArgIndex<3>>]>;
}

// Compress, Expand
let TargetPrefix = "x86" in {
  def int_x86_avx512_mask_compress :
      DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                            [LLVMMatchType<0>, LLVMMatchType<0>,
                             LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_expand :
      DefaultAttrsIntrinsic<[llvm_anyvector_ty],
                            [LLVMMatchType<0>, LLVMMatchType<0>,
                             LLVMScalarOrSameVectorWidth<0, llvm_i1_ty>],
                            [IntrNoMem]>;
}

// truncate
let TargetPrefix = "x86" in {
  def int_x86_avx512_mask_pmov_qb_128 :
      ClangBuiltin<"__builtin_ia32_pmovqb128_mask">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                            [llvm_v2i64_ty, llvm_v16i8_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmov_qb_mem_128 :
      ClangBuiltin<"__builtin_ia32_pmovqb128mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v2i64_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovs_qb_128 :
      ClangBuiltin<"__builtin_ia32_pmovsqb128_mask">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                            [llvm_v2i64_ty, llvm_v16i8_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovs_qb_mem_128 :
      ClangBuiltin<"__builtin_ia32_pmovsqb128mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v2i64_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovus_qb_128 :
      ClangBuiltin<"__builtin_ia32_pmovusqb128_mask">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                            [llvm_v2i64_ty, llvm_v16i8_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovus_qb_mem_128 :
      ClangBuiltin<"__builtin_ia32_pmovusqb128mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v2i64_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmov_qb_256 :
      ClangBuiltin<"__builtin_ia32_pmovqb256_mask">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                            [llvm_v4i64_ty, llvm_v16i8_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmov_qb_mem_256 :
      ClangBuiltin<"__builtin_ia32_pmovqb256mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v4i64_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovs_qb_256 :
      ClangBuiltin<"__builtin_ia32_pmovsqb256_mask">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                            [llvm_v4i64_ty, llvm_v16i8_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovs_qb_mem_256 :
      ClangBuiltin<"__builtin_ia32_pmovsqb256mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v4i64_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovus_qb_256 :
      ClangBuiltin<"__builtin_ia32_pmovusqb256_mask">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                            [llvm_v4i64_ty, llvm_v16i8_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovus_qb_mem_256 :
      ClangBuiltin<"__builtin_ia32_pmovusqb256mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v4i64_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmov_qb_512 :
      ClangBuiltin<"__builtin_ia32_pmovqb512_mask">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                            [llvm_v8i64_ty, llvm_v16i8_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmov_qb_mem_512 :
      ClangBuiltin<"__builtin_ia32_pmovqb512mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v8i64_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovs_qb_512 :
      ClangBuiltin<"__builtin_ia32_pmovsqb512_mask">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                            [llvm_v8i64_ty, llvm_v16i8_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovs_qb_mem_512 :
      ClangBuiltin<"__builtin_ia32_pmovsqb512mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v8i64_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovus_qb_512 :
      ClangBuiltin<"__builtin_ia32_pmovusqb512_mask">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                            [llvm_v8i64_ty, llvm_v16i8_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovus_qb_mem_512 :
      ClangBuiltin<"__builtin_ia32_pmovusqb512mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v8i64_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmov_qw_128 :
      ClangBuiltin<"__builtin_ia32_pmovqw128_mask">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty],
                            [llvm_v2i64_ty, llvm_v8i16_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmov_qw_mem_128 :
      ClangBuiltin<"__builtin_ia32_pmovqw128mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v2i64_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovs_qw_128 :
      ClangBuiltin<"__builtin_ia32_pmovsqw128_mask">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty],
                            [llvm_v2i64_ty, llvm_v8i16_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovs_qw_mem_128 :
      ClangBuiltin<"__builtin_ia32_pmovsqw128mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v2i64_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovus_qw_128 :
      ClangBuiltin<"__builtin_ia32_pmovusqw128_mask">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty],
                            [llvm_v2i64_ty, llvm_v8i16_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovus_qw_mem_128 :
      ClangBuiltin<"__builtin_ia32_pmovusqw128mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v2i64_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmov_qw_256 :
      ClangBuiltin<"__builtin_ia32_pmovqw256_mask">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty],
                            [llvm_v4i64_ty, llvm_v8i16_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmov_qw_mem_256 :
      ClangBuiltin<"__builtin_ia32_pmovqw256mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v4i64_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovs_qw_256 :
      ClangBuiltin<"__builtin_ia32_pmovsqw256_mask">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty],
                            [llvm_v4i64_ty, llvm_v8i16_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovs_qw_mem_256 :
      ClangBuiltin<"__builtin_ia32_pmovsqw256mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v4i64_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovus_qw_256 :
      ClangBuiltin<"__builtin_ia32_pmovusqw256_mask">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty],
                            [llvm_v4i64_ty, llvm_v8i16_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovus_qw_mem_256 :
      ClangBuiltin<"__builtin_ia32_pmovusqw256mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v4i64_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmov_qw_512 :
      DefaultAttrsIntrinsic<[llvm_v8i16_ty],
                            [llvm_v8i64_ty, llvm_v8i16_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmov_qw_mem_512 :
      ClangBuiltin<"__builtin_ia32_pmovqw512mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v8i64_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovs_qw_512 :
      ClangBuiltin<"__builtin_ia32_pmovsqw512_mask">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty],
                            [llvm_v8i64_ty, llvm_v8i16_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovs_qw_mem_512 :
      ClangBuiltin<"__builtin_ia32_pmovsqw512mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v8i64_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovus_qw_512 :
      ClangBuiltin<"__builtin_ia32_pmovusqw512_mask">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty],
                            [llvm_v8i64_ty, llvm_v8i16_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovus_qw_mem_512 :
      ClangBuiltin<"__builtin_ia32_pmovusqw512mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v8i64_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmov_qd_128 :
      ClangBuiltin<"__builtin_ia32_pmovqd128_mask">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                            [llvm_v2i64_ty, llvm_v4i32_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmov_qd_mem_128 :
      ClangBuiltin<"__builtin_ia32_pmovqd128mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v2i64_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovs_qd_128 :
      ClangBuiltin<"__builtin_ia32_pmovsqd128_mask">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                            [llvm_v2i64_ty, llvm_v4i32_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovs_qd_mem_128 :
      ClangBuiltin<"__builtin_ia32_pmovsqd128mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v2i64_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovus_qd_128 :
      ClangBuiltin<"__builtin_ia32_pmovusqd128_mask">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                            [llvm_v2i64_ty, llvm_v4i32_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovus_qd_mem_128 :
      ClangBuiltin<"__builtin_ia32_pmovusqd128mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v2i64_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmov_qd_mem_256 :
      ClangBuiltin<"__builtin_ia32_pmovqd256mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v4i64_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovs_qd_256 :
      ClangBuiltin<"__builtin_ia32_pmovsqd256_mask">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                            [llvm_v4i64_ty, llvm_v4i32_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovs_qd_mem_256 :
      ClangBuiltin<"__builtin_ia32_pmovsqd256mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v4i64_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovus_qd_256 :
      ClangBuiltin<"__builtin_ia32_pmovusqd256_mask">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                            [llvm_v4i64_ty, llvm_v4i32_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovus_qd_mem_256 :
      ClangBuiltin<"__builtin_ia32_pmovusqd256mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v4i64_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmov_qd_mem_512 :
      ClangBuiltin<"__builtin_ia32_pmovqd512mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v8i64_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovs_qd_512 :
      ClangBuiltin<"__builtin_ia32_pmovsqd512_mask">,
      DefaultAttrsIntrinsic<[llvm_v8i32_ty],
                            [llvm_v8i64_ty, llvm_v8i32_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovs_qd_mem_512 :
      ClangBuiltin<"__builtin_ia32_pmovsqd512mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v8i64_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovus_qd_512 :
      ClangBuiltin<"__builtin_ia32_pmovusqd512_mask">,
      DefaultAttrsIntrinsic<[llvm_v8i32_ty],
                            [llvm_v8i64_ty, llvm_v8i32_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovus_qd_mem_512 :
      ClangBuiltin<"__builtin_ia32_pmovusqd512mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v8i64_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmov_db_128 :
      ClangBuiltin<"__builtin_ia32_pmovdb128_mask">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                            [llvm_v4i32_ty, llvm_v16i8_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmov_db_mem_128 :
      ClangBuiltin<"__builtin_ia32_pmovdb128mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v4i32_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovs_db_128 :
      ClangBuiltin<"__builtin_ia32_pmovsdb128_mask">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                            [llvm_v4i32_ty, llvm_v16i8_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovs_db_mem_128 :
      ClangBuiltin<"__builtin_ia32_pmovsdb128mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v4i32_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovus_db_128 :
      ClangBuiltin<"__builtin_ia32_pmovusdb128_mask">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                            [llvm_v4i32_ty, llvm_v16i8_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovus_db_mem_128 :
      ClangBuiltin<"__builtin_ia32_pmovusdb128mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v4i32_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmov_db_256 :
      ClangBuiltin<"__builtin_ia32_pmovdb256_mask">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                            [llvm_v8i32_ty, llvm_v16i8_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmov_db_mem_256 :
      ClangBuiltin<"__builtin_ia32_pmovdb256mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v8i32_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovs_db_256 :
      ClangBuiltin<"__builtin_ia32_pmovsdb256_mask">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                            [llvm_v8i32_ty, llvm_v16i8_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovs_db_mem_256 :
      ClangBuiltin<"__builtin_ia32_pmovsdb256mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v8i32_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovus_db_256 :
      ClangBuiltin<"__builtin_ia32_pmovusdb256_mask">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                            [llvm_v8i32_ty, llvm_v16i8_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovus_db_mem_256 :
      ClangBuiltin<"__builtin_ia32_pmovusdb256mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v8i32_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmov_db_512 :
      DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                            [llvm_v16i32_ty, llvm_v16i8_ty, llvm_i16_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmov_db_mem_512 :
      ClangBuiltin<"__builtin_ia32_pmovdb512mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v16i32_ty, llvm_i16_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovs_db_512 :
      ClangBuiltin<"__builtin_ia32_pmovsdb512_mask">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                            [llvm_v16i32_ty, llvm_v16i8_ty, llvm_i16_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovs_db_mem_512 :
      ClangBuiltin<"__builtin_ia32_pmovsdb512mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v16i32_ty, llvm_i16_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovus_db_512 :
      ClangBuiltin<"__builtin_ia32_pmovusdb512_mask">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                            [llvm_v16i32_ty, llvm_v16i8_ty, llvm_i16_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovus_db_mem_512 :
      ClangBuiltin<"__builtin_ia32_pmovusdb512mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v16i32_ty, llvm_i16_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmov_dw_128 :
      ClangBuiltin<"__builtin_ia32_pmovdw128_mask">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty],
                            [llvm_v4i32_ty, llvm_v8i16_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmov_dw_mem_128 :
      ClangBuiltin<"__builtin_ia32_pmovdw128mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v4i32_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovs_dw_128 :
      ClangBuiltin<"__builtin_ia32_pmovsdw128_mask">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty],
                            [llvm_v4i32_ty, llvm_v8i16_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovs_dw_mem_128 :
      ClangBuiltin<"__builtin_ia32_pmovsdw128mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v4i32_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovus_dw_128 :
      ClangBuiltin<"__builtin_ia32_pmovusdw128_mask">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty],
                            [llvm_v4i32_ty, llvm_v8i16_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovus_dw_mem_128 :
      ClangBuiltin<"__builtin_ia32_pmovusdw128mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v4i32_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmov_dw_256 :
      ClangBuiltin<"__builtin_ia32_pmovdw256_mask">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty],
                            [llvm_v8i32_ty, llvm_v8i16_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmov_dw_mem_256 :
      ClangBuiltin<"__builtin_ia32_pmovdw256mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v8i32_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovs_dw_256 :
      ClangBuiltin<"__builtin_ia32_pmovsdw256_mask">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty],
                            [llvm_v8i32_ty, llvm_v8i16_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovs_dw_mem_256 :
      ClangBuiltin<"__builtin_ia32_pmovsdw256mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v8i32_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovus_dw_256 :
      ClangBuiltin<"__builtin_ia32_pmovusdw256_mask">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty],
                            [llvm_v8i32_ty, llvm_v8i16_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovus_dw_mem_256 :
      ClangBuiltin<"__builtin_ia32_pmovusdw256mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v8i32_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmov_dw_512 :
      DefaultAttrsIntrinsic<[llvm_v16i16_ty],
                            [llvm_v16i32_ty, llvm_v16i16_ty, llvm_i16_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmov_dw_mem_512 :
      ClangBuiltin<"__builtin_ia32_pmovdw512mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v16i32_ty, llvm_i16_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovs_dw_512 :
      ClangBuiltin<"__builtin_ia32_pmovsdw512_mask">,
      DefaultAttrsIntrinsic<[llvm_v16i16_ty],
                            [llvm_v16i32_ty, llvm_v16i16_ty, llvm_i16_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovs_dw_mem_512 :
      ClangBuiltin<"__builtin_ia32_pmovsdw512mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v16i32_ty, llvm_i16_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovus_dw_512 :
      ClangBuiltin<"__builtin_ia32_pmovusdw512_mask">,
      DefaultAttrsIntrinsic<[llvm_v16i16_ty],
                            [llvm_v16i32_ty, llvm_v16i16_ty, llvm_i16_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovus_dw_mem_512 :
      ClangBuiltin<"__builtin_ia32_pmovusdw512mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v16i32_ty, llvm_i16_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmov_wb_128 :
      ClangBuiltin<"__builtin_ia32_pmovwb128_mask">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                            [llvm_v8i16_ty, llvm_v16i8_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmov_wb_mem_128 :
      ClangBuiltin<"__builtin_ia32_pmovwb128mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v8i16_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovs_wb_128 :
      ClangBuiltin<"__builtin_ia32_pmovswb128_mask">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                            [llvm_v8i16_ty, llvm_v16i8_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovs_wb_mem_128 :
      ClangBuiltin<"__builtin_ia32_pmovswb128mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v8i16_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovus_wb_128 :
      ClangBuiltin<"__builtin_ia32_pmovuswb128_mask">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                            [llvm_v8i16_ty, llvm_v16i8_ty, llvm_i8_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovus_wb_mem_128 :
      ClangBuiltin<"__builtin_ia32_pmovuswb128mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v8i16_ty, llvm_i8_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmov_wb_mem_256 :
      ClangBuiltin<"__builtin_ia32_pmovwb256mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v16i16_ty, llvm_i16_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovs_wb_256 :
      ClangBuiltin<"__builtin_ia32_pmovswb256_mask">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                            [llvm_v16i16_ty, llvm_v16i8_ty, llvm_i16_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovs_wb_mem_256 :
      ClangBuiltin<"__builtin_ia32_pmovswb256mem_mask">,
      DefaultAttrsIntrinsic<[],
                          [llvm_ptr_ty, llvm_v16i16_ty, llvm_i16_ty],
                          [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovus_wb_256 :
      ClangBuiltin<"__builtin_ia32_pmovuswb256_mask">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                            [llvm_v16i16_ty, llvm_v16i8_ty, llvm_i16_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovus_wb_mem_256 :
      ClangBuiltin<"__builtin_ia32_pmovuswb256mem_mask">,
      DefaultAttrsIntrinsic<[],
                          [llvm_ptr_ty, llvm_v16i16_ty, llvm_i16_ty],
                          [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmov_wb_mem_512 :
      ClangBuiltin<"__builtin_ia32_pmovwb512mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v32i16_ty, llvm_i32_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovs_wb_512 :
      ClangBuiltin<"__builtin_ia32_pmovswb512_mask">,
      DefaultAttrsIntrinsic<[llvm_v32i8_ty],
                            [llvm_v32i16_ty, llvm_v32i8_ty, llvm_i32_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovs_wb_mem_512 :
      ClangBuiltin<"__builtin_ia32_pmovswb512mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v32i16_ty, llvm_i32_ty],
                            [IntrArgMemOnly]>;
  def int_x86_avx512_mask_pmovus_wb_512 :
      ClangBuiltin<"__builtin_ia32_pmovuswb512_mask">,
      DefaultAttrsIntrinsic<[llvm_v32i8_ty],
                            [llvm_v32i16_ty, llvm_v32i8_ty, llvm_i32_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_mask_pmovus_wb_mem_512 :
      ClangBuiltin<"__builtin_ia32_pmovuswb512mem_mask">,
      DefaultAttrsIntrinsic<[],
                            [llvm_ptr_ty, llvm_v32i16_ty, llvm_i32_ty],
                            [IntrArgMemOnly]>;
}

// Bitwise ternary logic
let TargetPrefix = "x86" in {
  def int_x86_avx512_pternlog_d_128 :
      ClangBuiltin<"__builtin_ia32_pternlogd128">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                            [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty,
                             llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  def int_x86_avx512_pternlog_d_256 :
      ClangBuiltin<"__builtin_ia32_pternlogd256">,
      DefaultAttrsIntrinsic<[llvm_v8i32_ty],
                            [llvm_v8i32_ty, llvm_v8i32_ty, llvm_v8i32_ty,
                             llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  def int_x86_avx512_pternlog_d_512 :
      ClangBuiltin<"__builtin_ia32_pternlogd512">,
      DefaultAttrsIntrinsic<[llvm_v16i32_ty],
                            [llvm_v16i32_ty, llvm_v16i32_ty, llvm_v16i32_ty,
                             llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  def int_x86_avx512_pternlog_q_128 :
      ClangBuiltin<"__builtin_ia32_pternlogq128">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty],
                            [llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty,
                             llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  def int_x86_avx512_pternlog_q_256 :
      ClangBuiltin<"__builtin_ia32_pternlogq256">,
      DefaultAttrsIntrinsic<[llvm_v4i64_ty],
                            [llvm_v4i64_ty, llvm_v4i64_ty, llvm_v4i64_ty,
                             llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<3>>]>;

  def int_x86_avx512_pternlog_q_512 :
      ClangBuiltin<"__builtin_ia32_pternlogq512">,
      DefaultAttrsIntrinsic<[llvm_v8i64_ty],
                            [llvm_v8i64_ty, llvm_v8i64_ty, llvm_v8i64_ty,
                             llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<3>>]>;
}

// vp2intersect
let TargetPrefix = "x86" in {
  def int_x86_avx512_vp2intersect_q_512 :
      DefaultAttrsIntrinsic<[llvm_v8i1_ty, llvm_v8i1_ty],
                            [llvm_v8i64_ty, llvm_v8i64_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_vp2intersect_q_256 :
      DefaultAttrsIntrinsic<[llvm_v4i1_ty, llvm_v4i1_ty],
                            [llvm_v4i64_ty, llvm_v4i64_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_vp2intersect_q_128 :
      DefaultAttrsIntrinsic<[llvm_v2i1_ty, llvm_v2i1_ty],
                            [llvm_v2i64_ty, llvm_v2i64_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_vp2intersect_d_512 :
      DefaultAttrsIntrinsic<[llvm_v16i1_ty, llvm_v16i1_ty],
                            [llvm_v16i32_ty, llvm_v16i32_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_vp2intersect_d_256 :
      DefaultAttrsIntrinsic<[llvm_v8i1_ty, llvm_v8i1_ty],
                            [llvm_v8i32_ty, llvm_v8i32_ty],
                            [IntrNoMem]>;
  def int_x86_avx512_vp2intersect_d_128 :
      DefaultAttrsIntrinsic<[llvm_v4i1_ty, llvm_v4i1_ty],
                            [llvm_v4i32_ty, llvm_v4i32_ty],
                            [IntrNoMem]>;
}

// Misc.
let TargetPrefix = "x86" in {
  // NOTE: These comparison intrinsics are not used by clang as long as the
  //       distinction in signaling behaviour is not implemented.
  def int_x86_avx512_mask_cmp_ps_512 :
      DefaultAttrsIntrinsic<[llvm_v16i1_ty], [llvm_v16f32_ty, llvm_v16f32_ty,
                             llvm_i32_ty, llvm_v16i1_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>,
                             ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_cmp_pd_512 :
      DefaultAttrsIntrinsic<[llvm_v8i1_ty], [llvm_v8f64_ty, llvm_v8f64_ty,
                             llvm_i32_ty, llvm_v8i1_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>,
                             ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_cmp_ps_256 :
      DefaultAttrsIntrinsic<[llvm_v8i1_ty], [llvm_v8f32_ty, llvm_v8f32_ty,
                             llvm_i32_ty, llvm_v8i1_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_avx512_mask_cmp_pd_256 :
      DefaultAttrsIntrinsic<[llvm_v4i1_ty], [llvm_v4f64_ty, llvm_v4f64_ty,
                             llvm_i32_ty, llvm_v4i1_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_avx512_mask_cmp_ps_128 :
      DefaultAttrsIntrinsic<[llvm_v4i1_ty], [llvm_v4f32_ty, llvm_v4f32_ty,
                             llvm_i32_ty, llvm_v4i1_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_avx512_mask_cmp_pd_128 :
      DefaultAttrsIntrinsic<[llvm_v2i1_ty], [llvm_v2f64_ty, llvm_v2f64_ty,
                             llvm_i32_ty, llvm_v2i1_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>]>;

  def int_x86_avx512_mask_cmp_ss :
      ClangBuiltin<"__builtin_ia32_cmpss_mask">,
      DefaultAttrsIntrinsic<[llvm_i8_ty], [llvm_v4f32_ty, llvm_v4f32_ty,
                             llvm_i32_ty, llvm_i8_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>,
                             ImmArg<ArgIndex<4>>]>;
  def int_x86_avx512_mask_cmp_sd :
      ClangBuiltin<"__builtin_ia32_cmpsd_mask">,
      DefaultAttrsIntrinsic<[llvm_i8_ty], [llvm_v2f64_ty, llvm_v2f64_ty,
                             llvm_i32_ty, llvm_i8_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>,
                             ImmArg<ArgIndex<4>>]>;
}

//===----------------------------------------------------------------------===//
// SHA intrinsics
let TargetPrefix = "x86" in {
  def int_x86_sha1rnds4 : ClangBuiltin<"__builtin_ia32_sha1rnds4">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty,
                             llvm_i8_ty], [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_x86_sha1nexte : ClangBuiltin<"__builtin_ia32_sha1nexte">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
                            [IntrNoMem]>;
  def int_x86_sha1msg1 : ClangBuiltin<"__builtin_ia32_sha1msg1">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
                            [IntrNoMem]>;
  def int_x86_sha1msg2 : ClangBuiltin<"__builtin_ia32_sha1msg2">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
                            [IntrNoMem]>;
  def int_x86_sha256rnds2 : ClangBuiltin<"__builtin_ia32_sha256rnds2">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty,
                             llvm_v4i32_ty], [IntrNoMem]>;
  def int_x86_sha256msg1 : ClangBuiltin<"__builtin_ia32_sha256msg1">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
                            [IntrNoMem]>;
  def int_x86_sha256msg2 : ClangBuiltin<"__builtin_ia32_sha256msg2">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
                            [IntrNoMem]>;
}

//===----------------------------------------------------------------------===//
// SHA512 intrinsics
let TargetPrefix = "x86" in {
def int_x86_vsha512msg1 : ClangBuiltin<"__builtin_ia32_vsha512msg1">,
    DefaultAttrsIntrinsic<[llvm_v4i64_ty], [llvm_v4i64_ty, llvm_v2i64_ty],
                          [IntrNoMem]>;
def int_x86_vsha512msg2 : ClangBuiltin<"__builtin_ia32_vsha512msg2">,
    DefaultAttrsIntrinsic<[llvm_v4i64_ty], [llvm_v4i64_ty, llvm_v4i64_ty],
                          [IntrNoMem]>;
def int_x86_vsha512rnds2 : ClangBuiltin<"__builtin_ia32_vsha512rnds2">,
    DefaultAttrsIntrinsic<[llvm_v4i64_ty], [llvm_v4i64_ty, llvm_v4i64_ty, llvm_v2i64_ty],
                          [IntrNoMem]>;
}

//===----------------------------------------------------------------------===//
// Thread synchronization ops with timer.
let TargetPrefix = "x86" in {
  def int_x86_monitorx
      : ClangBuiltin<"__builtin_ia32_monitorx">,
        Intrinsic<[], [ llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty ], []>;
  def int_x86_mwaitx
      : ClangBuiltin<"__builtin_ia32_mwaitx">,
        Intrinsic<[], [ llvm_i32_ty, llvm_i32_ty, llvm_i32_ty ], []>;
}

//===----------------------------------------------------------------------===//
// Cache-line zero
let TargetPrefix = "x86" in {
  def int_x86_clzero : ClangBuiltin<"__builtin_ia32_clzero">,
      Intrinsic<[], [llvm_ptr_ty], []>;
}

//===----------------------------------------------------------------------===//
// Cache write back intrinsics

let TargetPrefix = "x86" in {
  // Write back and invalidate
  def int_x86_wbinvd : ClangBuiltin<"__builtin_ia32_wbinvd">,
      Intrinsic<[], [], []>;

  // Write back no-invalidate
  def int_x86_wbnoinvd : ClangBuiltin<"__builtin_ia32_wbnoinvd">,
      Intrinsic<[], [], []>;
}

//===----------------------------------------------------------------------===//
// Cache-line demote

let TargetPrefix = "x86" in {
  def int_x86_cldemote : ClangBuiltin<"__builtin_ia32_cldemote">,
      Intrinsic<[], [llvm_ptr_ty], []>;
}

//===----------------------------------------------------------------------===//
// Wait and pause enhancements
let TargetPrefix = "x86" in {
  def int_x86_umonitor : ClangBuiltin<"__builtin_ia32_umonitor">,
              Intrinsic<[], [llvm_ptr_ty], []>;
  def int_x86_umwait : ClangBuiltin<"__builtin_ia32_umwait">,
              Intrinsic<[llvm_i8_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], []>;
  def int_x86_tpause : ClangBuiltin<"__builtin_ia32_tpause">,
              Intrinsic<[llvm_i8_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], []>;
}

//===----------------------------------------------------------------------===//
// Direct Move Instructions

let TargetPrefix = "x86" in {
  def int_x86_directstore32 : ClangBuiltin<"__builtin_ia32_directstore_u32">,
      Intrinsic<[], [llvm_ptr_ty, llvm_i32_ty], []>;
  def int_x86_directstore64 : ClangBuiltin<"__builtin_ia32_directstore_u64">,
      Intrinsic<[], [llvm_ptr_ty, llvm_i64_ty], []>;
  def int_x86_movdir64b : ClangBuiltin<"__builtin_ia32_movdir64b">,
      Intrinsic<[], [llvm_ptr_ty, llvm_ptr_ty], []>;
}

//===----------------------------------------------------------------------===//
// PTWrite - Write data to processor trace pocket

let TargetPrefix = "x86" in {
  def int_x86_ptwrite32 : ClangBuiltin<"__builtin_ia32_ptwrite32">,
              Intrinsic<[], [llvm_i32_ty], []>;
  def int_x86_ptwrite64 : ClangBuiltin<"__builtin_ia32_ptwrite64">,
              Intrinsic<[], [llvm_i64_ty], []>;
}

//===----------------------------------------------------------------------===//
// INVPCID - Invalidate Process-Context Identifier

let TargetPrefix = "x86" in {
  def int_x86_invpcid : ClangBuiltin<"__builtin_ia32_invpcid">,
              Intrinsic<[], [llvm_i32_ty, llvm_ptr_ty], []>;
}

let TargetPrefix = "x86" in {
  def int_x86_avx512bf16_cvtne2ps2bf16_128:
      ClangBuiltin<"__builtin_ia32_cvtne2ps2bf16_128">,
      DefaultAttrsIntrinsic<[llvm_v8bf16_ty], [llvm_v4f32_ty, llvm_v4f32_ty],
                            [IntrNoMem]>;
  def int_x86_avx512bf16_cvtne2ps2bf16_256:
      ClangBuiltin<"__builtin_ia32_cvtne2ps2bf16_256">,
      DefaultAttrsIntrinsic<[llvm_v16bf16_ty], [llvm_v8f32_ty, llvm_v8f32_ty],
                            [IntrNoMem]>;
  def int_x86_avx512bf16_cvtne2ps2bf16_512:
      ClangBuiltin<"__builtin_ia32_cvtne2ps2bf16_512">,
      DefaultAttrsIntrinsic<[llvm_v32bf16_ty], [llvm_v16f32_ty, llvm_v16f32_ty],
                            [IntrNoMem]>;
  // Intrinsic must be masked due to it producing less than 128 bits of results.
  def int_x86_avx512bf16_mask_cvtneps2bf16_128:
      DefaultAttrsIntrinsic<[llvm_v8bf16_ty],
                            [llvm_v4f32_ty, llvm_v8bf16_ty, llvm_v4i1_ty],
                            [IntrNoMem]>;
  def int_x86_avx512bf16_cvtneps2bf16_256:
      ClangBuiltin<"__builtin_ia32_cvtneps2bf16_256">,
      DefaultAttrsIntrinsic<[llvm_v8bf16_ty], [llvm_v8f32_ty], [IntrNoMem]>;
  def int_x86_avx512bf16_cvtneps2bf16_512:
      ClangBuiltin<"__builtin_ia32_cvtneps2bf16_512">,
      DefaultAttrsIntrinsic<[llvm_v16bf16_ty], [llvm_v16f32_ty], [IntrNoMem]>;
  def int_x86_avx512bf16_dpbf16ps_128:
      ClangBuiltin<"__builtin_ia32_dpbf16ps_128">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty],
                            [llvm_v4f32_ty, llvm_v8bf16_ty, llvm_v8bf16_ty],
                            [IntrNoMem]>;
  def int_x86_avx512bf16_dpbf16ps_256:
      ClangBuiltin<"__builtin_ia32_dpbf16ps_256">,
      DefaultAttrsIntrinsic<[llvm_v8f32_ty],
                            [llvm_v8f32_ty, llvm_v16bf16_ty, llvm_v16bf16_ty],
                            [IntrNoMem]>;
  def int_x86_avx512bf16_dpbf16ps_512:
      ClangBuiltin<"__builtin_ia32_dpbf16ps_512">,
      DefaultAttrsIntrinsic<[llvm_v16f32_ty],
                            [llvm_v16f32_ty, llvm_v32bf16_ty, llvm_v32bf16_ty],
                            [IntrNoMem]>;
}

//===----------------------------------------------------------------------===//
// ENQCMD - Enqueue Stores Instructions

let TargetPrefix = "x86" in {
  def int_x86_enqcmd : ClangBuiltin<"__builtin_ia32_enqcmd">,
              Intrinsic<[llvm_i8_ty], [llvm_ptr_ty, llvm_ptr_ty], []>;
  def int_x86_enqcmds : ClangBuiltin<"__builtin_ia32_enqcmds">,
              Intrinsic<[llvm_i8_ty], [llvm_ptr_ty, llvm_ptr_ty], []>;
}

//===----------------------------------------------------------------------===//
// SERIALIZE - Serialize instruction fetch and execution

let TargetPrefix = "x86" in {
  def int_x86_serialize : ClangBuiltin<"__builtin_ia32_serialize">,
              Intrinsic<[], [], []>;
}

//===----------------------------------------------------------------------===//
// TSXLDTRK - TSX Suspend Load Address Tracking

let TargetPrefix = "x86" in {
  def int_x86_xsusldtrk : ClangBuiltin<"__builtin_ia32_xsusldtrk">,
              Intrinsic<[], [], []>;
  def int_x86_xresldtrk : ClangBuiltin<"__builtin_ia32_xresldtrk">,
              Intrinsic<[], [], []>;
}

//===----------------------------------------------------------------------===//
// Key Locker
let TargetPrefix = "x86" in {
  def int_x86_loadiwkey : ClangBuiltin<"__builtin_ia32_loadiwkey">,
      Intrinsic<[], [llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty, llvm_i32_ty],
                []>;
  def int_x86_encodekey128 :
      Intrinsic<[llvm_i32_ty, llvm_v2i64_ty, llvm_v2i64_ty,
                 llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty],
                [llvm_i32_ty, llvm_v2i64_ty], []>;
  def int_x86_encodekey256 :
      Intrinsic<[llvm_i32_ty, llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty,
                 llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty],
                [llvm_i32_ty, llvm_v2i64_ty, llvm_v2i64_ty], []>;
  def int_x86_aesenc128kl :
      Intrinsic<[llvm_i8_ty, llvm_v2i64_ty], [llvm_v2i64_ty, llvm_ptr_ty], []>;
  def int_x86_aesdec128kl :
      Intrinsic<[llvm_i8_ty, llvm_v2i64_ty], [llvm_v2i64_ty, llvm_ptr_ty], []>;
  def int_x86_aesenc256kl :
      Intrinsic<[llvm_i8_ty, llvm_v2i64_ty], [llvm_v2i64_ty, llvm_ptr_ty], []>;
  def int_x86_aesdec256kl :
      Intrinsic<[llvm_i8_ty, llvm_v2i64_ty], [llvm_v2i64_ty, llvm_ptr_ty], []>;
  def int_x86_aesencwide128kl :
      Intrinsic<[llvm_i8_ty, llvm_v2i64_ty, llvm_v2i64_ty,
                 llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty,
                 llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty],
                [llvm_ptr_ty, llvm_v2i64_ty, llvm_v2i64_ty,
                 llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty,
                 llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty], []>;
  def int_x86_aesdecwide128kl :
      Intrinsic<[llvm_i8_ty, llvm_v2i64_ty, llvm_v2i64_ty,
                 llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty,
                 llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty],
                [llvm_ptr_ty, llvm_v2i64_ty, llvm_v2i64_ty,
                 llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty,
                 llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty], []>;
  def int_x86_aesencwide256kl :
      Intrinsic<[llvm_i8_ty, llvm_v2i64_ty, llvm_v2i64_ty,
                 llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty,
                 llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty],
                [llvm_ptr_ty, llvm_v2i64_ty, llvm_v2i64_ty,
                 llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty,
                 llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty], []>;
  def int_x86_aesdecwide256kl :
      Intrinsic<[llvm_i8_ty, llvm_v2i64_ty, llvm_v2i64_ty,
                 llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty,
                 llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty],
                [llvm_ptr_ty, llvm_v2i64_ty, llvm_v2i64_ty,
                 llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty,
                 llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty], []>;
}

//===----------------------------------------------------------------------===//
// AMX - Intel AMX extensions

let TargetPrefix = "x86" in {
  def int_x86_ldtilecfg : ClangBuiltin<"__builtin_ia32_tile_loadconfig">,
              Intrinsic<[], [llvm_ptr_ty], []>;
  def int_x86_sttilecfg : ClangBuiltin<"__builtin_ia32_tile_storeconfig">,
              Intrinsic<[], [llvm_ptr_ty], []>;
  def int_x86_tilerelease : ClangBuiltin<"__builtin_ia32_tilerelease">,
              Intrinsic<[], [], []>;
  def int_x86_tilezero : ClangBuiltin<"__builtin_ia32_tilezero">,
              Intrinsic<[], [llvm_i8_ty], [ImmArg<ArgIndex<0>>]>;
  def int_x86_tileloadd64 : ClangBuiltin<"__builtin_ia32_tileloadd64">,
              Intrinsic<[], [llvm_i8_ty, llvm_ptr_ty, llvm_i64_ty],
                        [ImmArg<ArgIndex<0>>]>;
  def int_x86_tileloaddt164 : ClangBuiltin<"__builtin_ia32_tileloaddt164">,
              Intrinsic<[], [llvm_i8_ty, llvm_ptr_ty, llvm_i64_ty],
                        [ImmArg<ArgIndex<0>>]>;
  def int_x86_tilestored64 : ClangBuiltin<"__builtin_ia32_tilestored64">,
              Intrinsic<[], [llvm_i8_ty, llvm_ptr_ty, llvm_i64_ty],
                        [ImmArg<ArgIndex<0>>]>;
  def int_x86_tdpbssd : ClangBuiltin<"__builtin_ia32_tdpbssd">,
              Intrinsic<[], [llvm_i8_ty, llvm_i8_ty, llvm_i8_ty],
                        [ImmArg<ArgIndex<0>>, ImmArg<ArgIndex<1>>,
                         ImmArg<ArgIndex<2>>]>;
  def int_x86_tdpbsud : ClangBuiltin<"__builtin_ia32_tdpbsud">,
              Intrinsic<[], [llvm_i8_ty, llvm_i8_ty, llvm_i8_ty],
                        [ImmArg<ArgIndex<0>>, ImmArg<ArgIndex<1>>,
                         ImmArg<ArgIndex<2>>]>;
  def int_x86_tdpbusd : ClangBuiltin<"__builtin_ia32_tdpbusd">,
              Intrinsic<[], [llvm_i8_ty, llvm_i8_ty, llvm_i8_ty],
                        [ImmArg<ArgIndex<0>>, ImmArg<ArgIndex<1>>,
                         ImmArg<ArgIndex<2>>]>;
  def int_x86_tdpbuud : ClangBuiltin<"__builtin_ia32_tdpbuud">,
              Intrinsic<[], [llvm_i8_ty, llvm_i8_ty, llvm_i8_ty],
                        [ImmArg<ArgIndex<0>>, ImmArg<ArgIndex<1>>,
                         ImmArg<ArgIndex<2>>]>;
  def int_x86_tdpbf16ps : ClangBuiltin<"__builtin_ia32_tdpbf16ps">,
              Intrinsic<[], [llvm_i8_ty, llvm_i8_ty, llvm_i8_ty],
                        [ImmArg<ArgIndex<0>>, ImmArg<ArgIndex<1>>,
                         ImmArg<ArgIndex<2>>]>;
  // AMX-FP16 - Intel FP16 AMX extensions
  def int_x86_tdpfp16ps : ClangBuiltin<"__builtin_ia32_tdpfp16ps">,
              Intrinsic<[], [llvm_i8_ty, llvm_i8_ty, llvm_i8_ty],
                        [ImmArg<ArgIndex<0>>, ImmArg<ArgIndex<1>>,
                         ImmArg<ArgIndex<2>>]>;
  // AMX-COMPLEX
  def int_x86_tcmmimfp16ps : ClangBuiltin<"__builtin_ia32_tcmmimfp16ps">,
              Intrinsic<[], [llvm_i8_ty, llvm_i8_ty, llvm_i8_ty],
                        [ImmArg<ArgIndex<0>>, ImmArg<ArgIndex<1>>,
                         ImmArg<ArgIndex<2>>]>;
  def int_x86_tcmmrlfp16ps : ClangBuiltin<"__builtin_ia32_tcmmrlfp16ps">,
              Intrinsic<[], [llvm_i8_ty, llvm_i8_ty, llvm_i8_ty],
                        [ImmArg<ArgIndex<0>>, ImmArg<ArgIndex<1>>,
                         ImmArg<ArgIndex<2>>]>;

  // AMX - internal intrinsics
  def int_x86_ldtilecfg_internal :
              ClangBuiltin<"__builtin_ia32_tile_loadconfig_internal">,
              Intrinsic<[], [llvm_ptr_ty], []>;
  def int_x86_tileloadd64_internal :
              ClangBuiltin<"__builtin_ia32_tileloadd64_internal">,
              Intrinsic<[llvm_x86amx_ty],
                        [llvm_i16_ty, llvm_i16_ty, llvm_ptr_ty, llvm_i64_ty],
                        []>;
  def int_x86_tileloaddt164_internal :
              ClangBuiltin<"__builtin_ia32_tileloaddt164_internal">,
              Intrinsic<[llvm_x86amx_ty],
                        [llvm_i16_ty, llvm_i16_ty, llvm_ptr_ty, llvm_i64_ty],
                        []>;
  def int_x86_tdpbssd_internal :
              ClangBuiltin<"__builtin_ia32_tdpbssd_internal">,
              Intrinsic<[llvm_x86amx_ty],
                        [llvm_i16_ty, llvm_i16_ty, llvm_i16_ty,
                         llvm_x86amx_ty, llvm_x86amx_ty,
                         llvm_x86amx_ty], []>;
  def int_x86_tdpbsud_internal :
              ClangBuiltin<"__builtin_ia32_tdpbsud_internal">,
              Intrinsic<[llvm_x86amx_ty],
                        [llvm_i16_ty, llvm_i16_ty, llvm_i16_ty,
                         llvm_x86amx_ty, llvm_x86amx_ty,
                         llvm_x86amx_ty], []>;
  def int_x86_tdpbusd_internal :
              ClangBuiltin<"__builtin_ia32_tdpbusd_internal">,
              Intrinsic<[llvm_x86amx_ty],
                        [llvm_i16_ty, llvm_i16_ty, llvm_i16_ty,
                         llvm_x86amx_ty, llvm_x86amx_ty,
                         llvm_x86amx_ty], []>;
  def int_x86_tdpbuud_internal :
              ClangBuiltin<"__builtin_ia32_tdpbuud_internal">,
              Intrinsic<[llvm_x86amx_ty],
                        [llvm_i16_ty, llvm_i16_ty, llvm_i16_ty,
                         llvm_x86amx_ty, llvm_x86amx_ty,
                         llvm_x86amx_ty], []>;
  def int_x86_tilestored64_internal :
              ClangBuiltin<"__builtin_ia32_tilestored64_internal">,
              Intrinsic<[], [llvm_i16_ty, llvm_i16_ty, llvm_ptr_ty,
                             llvm_i64_ty, llvm_x86amx_ty], []>;
  def int_x86_tilezero_internal :
              ClangBuiltin<"__builtin_ia32_tilezero_internal">,
              Intrinsic<[llvm_x86amx_ty], [llvm_i16_ty, llvm_i16_ty],
                        []>;
  def int_x86_tdpbf16ps_internal :
              ClangBuiltin<"__builtin_ia32_tdpbf16ps_internal">,
              Intrinsic<[llvm_x86amx_ty],
                        [llvm_i16_ty, llvm_i16_ty, llvm_i16_ty,
                         llvm_x86amx_ty, llvm_x86amx_ty,
                         llvm_x86amx_ty], []>;
  def int_x86_tdpfp16ps_internal :
              ClangBuiltin<"__builtin_ia32_tdpfp16ps_internal">,
              Intrinsic<[llvm_x86amx_ty],
                        [llvm_i16_ty, llvm_i16_ty, llvm_i16_ty,
                         llvm_x86amx_ty, llvm_x86amx_ty,
                         llvm_x86amx_ty], []>;
  // the vector size can be smaller than AMX register size (1024 bytes)
  def int_x86_cast_vector_to_tile:
      DefaultAttrsIntrinsic<[llvm_x86amx_ty], [llvm_anyvector_ty], [IntrNoMem]>;
  // the vector size can be smaller than AMX register size (1024 bytes)
  def int_x86_cast_tile_to_vector:
      DefaultAttrsIntrinsic<[llvm_anyvector_ty], [llvm_x86amx_ty], [IntrNoMem]>;

  def int_x86_tcmmimfp16ps_internal :
              ClangBuiltin<"__builtin_ia32_tcmmimfp16ps_internal">,
              Intrinsic<[llvm_x86amx_ty],
                        [llvm_i16_ty, llvm_i16_ty, llvm_i16_ty,
                         llvm_x86amx_ty, llvm_x86amx_ty,
                         llvm_x86amx_ty], []>;
  def int_x86_tcmmrlfp16ps_internal :
              ClangBuiltin<"__builtin_ia32_tcmmrlfp16ps_internal">,
              Intrinsic<[llvm_x86amx_ty],
                        [llvm_i16_ty, llvm_i16_ty, llvm_i16_ty,
                         llvm_x86amx_ty, llvm_x86amx_ty,
                         llvm_x86amx_ty], []>;
}

//===----------------------------------------------------------------------===//
let TargetPrefix = "x86" in {
// CMPCCXADD
def int_x86_cmpccxadd32
    : ClangBuiltin<"__builtin_ia32_cmpccxadd32">,
      Intrinsic<[llvm_i32_ty],
                [llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
                [IntrArgMemOnly, ImmArg<ArgIndex<3>>]>;

def int_x86_cmpccxadd64
    : ClangBuiltin<"__builtin_ia32_cmpccxadd64">,
      Intrinsic<[llvm_i64_ty],
                [llvm_ptr_ty, llvm_i64_ty, llvm_i64_ty, llvm_i32_ty],
                [IntrArgMemOnly, ImmArg<ArgIndex<3>>]>;

// AVX-NE-CONVERT
def int_x86_vbcstnebf162ps128
    : ClangBuiltin<"__builtin_ia32_vbcstnebf162ps128">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_ptr_ty],
                            [IntrReadMem, IntrArgMemOnly]>;
def int_x86_vbcstnebf162ps256
    : ClangBuiltin<"__builtin_ia32_vbcstnebf162ps256">,
      DefaultAttrsIntrinsic<[llvm_v8f32_ty], [llvm_ptr_ty],
                            [IntrReadMem, IntrArgMemOnly]>;
def int_x86_vbcstnesh2ps128
    : ClangBuiltin<"__builtin_ia32_vbcstnesh2ps128">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_ptr_ty],
                            [IntrReadMem, IntrArgMemOnly]>;
def int_x86_vbcstnesh2ps256
    : ClangBuiltin<"__builtin_ia32_vbcstnesh2ps256">,
      DefaultAttrsIntrinsic<[llvm_v8f32_ty], [llvm_ptr_ty],
                            [IntrReadMem, IntrArgMemOnly]>;
def int_x86_vcvtneebf162ps128
    : ClangBuiltin<"__builtin_ia32_vcvtneebf162ps128">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_ptr_ty],
                            [IntrReadMem, IntrArgMemOnly]>;
def int_x86_vcvtneebf162ps256
    : ClangBuiltin<"__builtin_ia32_vcvtneebf162ps256">,
      DefaultAttrsIntrinsic<[llvm_v8f32_ty], [llvm_ptr_ty],
                            [IntrReadMem, IntrArgMemOnly]>;
def int_x86_vcvtneeph2ps128
    : ClangBuiltin<"__builtin_ia32_vcvtneeph2ps128">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_ptr_ty],
                            [IntrReadMem, IntrArgMemOnly]>;
def int_x86_vcvtneeph2ps256
    : ClangBuiltin<"__builtin_ia32_vcvtneeph2ps256">,
      DefaultAttrsIntrinsic<[llvm_v8f32_ty], [llvm_ptr_ty],
                            [IntrReadMem, IntrArgMemOnly]>;
def int_x86_vcvtneobf162ps128
    : ClangBuiltin<"__builtin_ia32_vcvtneobf162ps128">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_ptr_ty],
                            [IntrReadMem, IntrArgMemOnly]>;
def int_x86_vcvtneobf162ps256
    : ClangBuiltin<"__builtin_ia32_vcvtneobf162ps256">,
      DefaultAttrsIntrinsic<[llvm_v8f32_ty], [llvm_ptr_ty],
                            [IntrReadMem, IntrArgMemOnly]>;
def int_x86_vcvtneoph2ps128
    : ClangBuiltin<"__builtin_ia32_vcvtneoph2ps128">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_ptr_ty],
                            [IntrReadMem, IntrArgMemOnly]>;
def int_x86_vcvtneoph2ps256
    : ClangBuiltin<"__builtin_ia32_vcvtneoph2ps256">,
      DefaultAttrsIntrinsic<[llvm_v8f32_ty], [llvm_ptr_ty],
                            [IntrReadMem, IntrArgMemOnly]>;
def int_x86_vcvtneps2bf16128
    : ClangBuiltin<"__builtin_ia32_vcvtneps2bf16128">,
      DefaultAttrsIntrinsic<[llvm_v8bf16_ty], [llvm_v4f32_ty], [IntrNoMem]>;
def int_x86_vcvtneps2bf16256
    : ClangBuiltin<"__builtin_ia32_vcvtneps2bf16256">,
      DefaultAttrsIntrinsic<[llvm_v8bf16_ty], [llvm_v8f32_ty], [IntrNoMem]>;
}
//===----------------------------------------------------------------------===//
// SM3 intrinsics
let TargetPrefix = "x86" in {
  def int_x86_vsm3msg1
      : ClangBuiltin<"__builtin_ia32_vsm3msg1">,
        DefaultAttrsIntrinsic<[llvm_v4i32_ty],
        [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
        [IntrNoMem]>;
  def int_x86_vsm3msg2
      : ClangBuiltin<"__builtin_ia32_vsm3msg2">,
        DefaultAttrsIntrinsic<[llvm_v4i32_ty],
        [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
        [IntrNoMem]>;
  def int_x86_vsm3rnds2
      : ClangBuiltin<"__builtin_ia32_vsm3rnds2">,
        DefaultAttrsIntrinsic<[llvm_v4i32_ty],
        [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty, llvm_i32_ty],
        [ImmArg<ArgIndex<3>>, IntrNoMem]>;
}
//===----------------------------------------------------------------------===//
// SM4 intrinsics
let TargetPrefix = "x86" in {
  def int_x86_vsm4key4128
      : ClangBuiltin<"__builtin_ia32_vsm4key4128">,
        DefaultAttrsIntrinsic<[llvm_v4i32_ty],
        [llvm_v4i32_ty, llvm_v4i32_ty],
        [IntrNoMem]>;
  def int_x86_vsm4key4256
      : ClangBuiltin<"__builtin_ia32_vsm4key4256">,
        DefaultAttrsIntrinsic<[llvm_v8i32_ty],
        [llvm_v8i32_ty, llvm_v8i32_ty],
        [IntrNoMem]>;
  def int_x86_vsm4rnds4128
      : ClangBuiltin<"__builtin_ia32_vsm4rnds4128">,
        DefaultAttrsIntrinsic<[llvm_v4i32_ty],
        [llvm_v4i32_ty, llvm_v4i32_ty],
        [IntrNoMem]>;
  def int_x86_vsm4rnds4256
      : ClangBuiltin<"__builtin_ia32_vsm4rnds4256">,
        DefaultAttrsIntrinsic<[llvm_v8i32_ty],
        [llvm_v8i32_ty, llvm_v8i32_ty],
        [IntrNoMem]>;
}
//===----------------------------------------------------------------------===//
// RAO-INT intrinsics
let TargetPrefix = "x86" in {
  def int_x86_aadd32
      : ClangBuiltin<"__builtin_ia32_aadd32">,
        Intrinsic<[], [llvm_ptr_ty, llvm_i32_ty], [IntrArgMemOnly]>;
  def int_x86_aadd64
      : ClangBuiltin<"__builtin_ia32_aadd64">,
        Intrinsic<[], [llvm_ptr_ty, llvm_i64_ty], [IntrArgMemOnly]>;
  def int_x86_aand32
      : ClangBuiltin<"__builtin_ia32_aand32">,
        Intrinsic<[], [llvm_ptr_ty, llvm_i32_ty], [IntrArgMemOnly]>;
  def int_x86_aand64
      : ClangBuiltin<"__builtin_ia32_aand64">,
        Intrinsic<[], [llvm_ptr_ty, llvm_i64_ty], [IntrArgMemOnly]>;
  def int_x86_aor32
      : ClangBuiltin<"__builtin_ia32_aor32">,
        Intrinsic<[], [llvm_ptr_ty, llvm_i32_ty], [IntrArgMemOnly]>;
  def int_x86_aor64
      : ClangBuiltin<"__builtin_ia32_aor64">,
        Intrinsic<[], [llvm_ptr_ty, llvm_i64_ty], [IntrArgMemOnly]>;
  def int_x86_axor32
      : ClangBuiltin<"__builtin_ia32_axor32">,
        Intrinsic<[], [llvm_ptr_ty, llvm_i32_ty], [IntrArgMemOnly]>;
  def int_x86_axor64
      : ClangBuiltin<"__builtin_ia32_axor64">,
        Intrinsic<[], [llvm_ptr_ty, llvm_i64_ty], [IntrArgMemOnly]>;
}

//===----------------------------------------------------------------------===//
// UINTR - User Level Interrupt

let TargetPrefix = "x86" in {
  def int_x86_clui : ClangBuiltin<"__builtin_ia32_clui">,
              Intrinsic<[], [], []>;
  def int_x86_stui : ClangBuiltin<"__builtin_ia32_stui">,
              Intrinsic<[], [], []>;
  def int_x86_testui : ClangBuiltin<"__builtin_ia32_testui">,
              Intrinsic<[llvm_i8_ty], [], []>;
  def int_x86_senduipi : ClangBuiltin<"__builtin_ia32_senduipi">,
              Intrinsic<[], [llvm_i64_ty], []>;
}

let TargetPrefix = "x86" in {
def int_x86_urdmsr : ClangBuiltin<"__builtin_ia32_urdmsr">,
        Intrinsic<[llvm_i64_ty], [llvm_i64_ty],
                  [IntrInaccessibleMemOnly]>;
def int_x86_uwrmsr : ClangBuiltin<"__builtin_ia32_uwrmsr">,
        Intrinsic<[], [llvm_i64_ty, llvm_i64_ty],
                  [IntrInaccessibleMemOnly]>;
}

//===----------------------------------------------------------------------===//
let TargetPrefix = "x86" in {
  def int_x86_avx512fp16_add_ph_512
      : ClangBuiltin<"__builtin_ia32_addph512">,
        DefaultAttrsIntrinsic<[ llvm_v32f16_ty ],
                              [ llvm_v32f16_ty, llvm_v32f16_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<2>> ]>;
  def int_x86_avx512fp16_sub_ph_512
      : ClangBuiltin<"__builtin_ia32_subph512">,
        DefaultAttrsIntrinsic<[ llvm_v32f16_ty ],
                              [ llvm_v32f16_ty, llvm_v32f16_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<2>> ]>;
  def int_x86_avx512fp16_mul_ph_512
      : ClangBuiltin<"__builtin_ia32_mulph512">,
        DefaultAttrsIntrinsic<[ llvm_v32f16_ty ],
                              [ llvm_v32f16_ty, llvm_v32f16_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<2>> ]>;
  def int_x86_avx512fp16_div_ph_512
      : ClangBuiltin<"__builtin_ia32_divph512">,
        DefaultAttrsIntrinsic<[ llvm_v32f16_ty ],
                              [ llvm_v32f16_ty, llvm_v32f16_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<2>> ]>;
  def int_x86_avx512fp16_max_ph_128
      : ClangBuiltin<"__builtin_ia32_maxph128">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v8f16_ty, llvm_v8f16_ty ], [ IntrNoMem ]>;
  def int_x86_avx512fp16_max_ph_256
      : ClangBuiltin<"__builtin_ia32_maxph256">,
        DefaultAttrsIntrinsic<[ llvm_v16f16_ty ],
                              [ llvm_v16f16_ty, llvm_v16f16_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_max_ph_512
      : ClangBuiltin<"__builtin_ia32_maxph512">,
        DefaultAttrsIntrinsic<[ llvm_v32f16_ty ],
                              [ llvm_v32f16_ty, llvm_v32f16_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<2>> ]>;
  def int_x86_avx512fp16_min_ph_128
      : ClangBuiltin<"__builtin_ia32_minph128">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v8f16_ty, llvm_v8f16_ty ], [ IntrNoMem ]>;
  def int_x86_avx512fp16_min_ph_256
      : ClangBuiltin<"__builtin_ia32_minph256">,
        DefaultAttrsIntrinsic<[ llvm_v16f16_ty ],
                              [ llvm_v16f16_ty, llvm_v16f16_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_min_ph_512
      : ClangBuiltin<"__builtin_ia32_minph512">,
        DefaultAttrsIntrinsic<[ llvm_v32f16_ty ],
                              [ llvm_v32f16_ty, llvm_v32f16_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<2>> ]>;

  def int_x86_avx512fp16_mask_cmp_ph_512
      : DefaultAttrsIntrinsic<[ llvm_v32i1_ty ],
                              [ llvm_v32f16_ty, llvm_v32f16_ty, llvm_i32_ty,
                                llvm_v32i1_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<4>> ]>;
  def int_x86_avx512fp16_mask_cmp_ph_256
      : DefaultAttrsIntrinsic<[ llvm_v16i1_ty ],
                              [ llvm_v16f16_ty, llvm_v16f16_ty, llvm_i32_ty,
                                llvm_v16i1_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<2>> ]>;
  def int_x86_avx512fp16_mask_cmp_ph_128
      : DefaultAttrsIntrinsic<[ llvm_v8i1_ty ],
                              [ llvm_v8f16_ty, llvm_v8f16_ty, llvm_i32_ty,
                                llvm_v8i1_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<2>> ]>;

  def int_x86_avx512fp16_mask_add_sh_round
      : ClangBuiltin<"__builtin_ia32_addsh_round_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v8f16_ty, llvm_v8f16_ty, llvm_v8f16_ty,
                                llvm_i8_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<4>> ]>;
  def int_x86_avx512fp16_mask_sub_sh_round
      : ClangBuiltin<"__builtin_ia32_subsh_round_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v8f16_ty, llvm_v8f16_ty, llvm_v8f16_ty,
                                llvm_i8_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<4>> ]>;
  def int_x86_avx512fp16_mask_mul_sh_round
      : ClangBuiltin<"__builtin_ia32_mulsh_round_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v8f16_ty, llvm_v8f16_ty, llvm_v8f16_ty,
                                llvm_i8_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<4>> ]>;
  def int_x86_avx512fp16_mask_div_sh_round
      : ClangBuiltin<"__builtin_ia32_divsh_round_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v8f16_ty, llvm_v8f16_ty, llvm_v8f16_ty,
                                llvm_i8_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<4>> ]>;
  def int_x86_avx512fp16_mask_min_sh_round
      : ClangBuiltin<"__builtin_ia32_minsh_round_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v8f16_ty, llvm_v8f16_ty, llvm_v8f16_ty,
                                llvm_i8_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<4>> ]>;
  def int_x86_avx512fp16_mask_max_sh_round
      : ClangBuiltin<"__builtin_ia32_maxsh_round_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v8f16_ty, llvm_v8f16_ty, llvm_v8f16_ty,
                                llvm_i8_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<4>> ]>;
  def int_x86_avx512fp16_mask_cmp_sh
      : ClangBuiltin<"__builtin_ia32_cmpsh_mask">,
        DefaultAttrsIntrinsic<[ llvm_i8_ty ],
                              [ llvm_v8f16_ty, llvm_v8f16_ty, llvm_i32_ty,
                                llvm_i8_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<2>>,
                                ImmArg<ArgIndex<4>> ]>;
  def int_x86_avx512fp16_vcomi_sh
      : ClangBuiltin<"__builtin_ia32_vcomish">,
        DefaultAttrsIntrinsic<[ llvm_i32_ty ],
                              [ llvm_v8f16_ty, llvm_v8f16_ty, llvm_i32_ty,
                                llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<2>>,
                                ImmArg<ArgIndex<3>> ]>;

  def int_x86_avx512fp16_mask_vcvtph2psx_128
      : ClangBuiltin<"__builtin_ia32_vcvtph2psx128_mask">,
        DefaultAttrsIntrinsic<[ llvm_v4f32_ty ],
                              [ llvm_v8f16_ty, llvm_v4f32_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvtph2psx_256
      : ClangBuiltin<"__builtin_ia32_vcvtph2psx256_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f32_ty ],
                              [ llvm_v8f16_ty, llvm_v8f32_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvtph2psx_512
      : ClangBuiltin<"__builtin_ia32_vcvtph2psx512_mask">,
        DefaultAttrsIntrinsic<[ llvm_v16f32_ty ],
                              [ llvm_v16f16_ty, llvm_v16f32_ty, llvm_i16_ty,
                                llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<3>> ]>;
  def int_x86_avx512fp16_mask_vcvtps2phx_128
      : ClangBuiltin<"__builtin_ia32_vcvtps2phx128_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v4f32_ty, llvm_v8f16_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvtps2phx_256
      : ClangBuiltin<"__builtin_ia32_vcvtps2phx256_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v8f32_ty, llvm_v8f16_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvtps2phx_512
      : ClangBuiltin<"__builtin_ia32_vcvtps2phx512_mask">,
        DefaultAttrsIntrinsic<[ llvm_v16f16_ty ],
                              [ llvm_v16f32_ty, llvm_v16f16_ty, llvm_i16_ty,
                                llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<3>> ]>;
  def int_x86_avx512fp16_mask_vcvtpd2ph_128
      : ClangBuiltin<"__builtin_ia32_vcvtpd2ph128_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v2f64_ty, llvm_v8f16_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvtpd2ph_256
      : ClangBuiltin<"__builtin_ia32_vcvtpd2ph256_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v4f64_ty, llvm_v8f16_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvtpd2ph_512
      : ClangBuiltin<"__builtin_ia32_vcvtpd2ph512_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v8f64_ty, llvm_v8f16_ty, llvm_i8_ty,
                                llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<3>> ]>;
  def int_x86_avx512fp16_mask_vcvtph2pd_128
      : ClangBuiltin<"__builtin_ia32_vcvtph2pd128_mask">,
        DefaultAttrsIntrinsic<[ llvm_v2f64_ty ],
                              [ llvm_v8f16_ty, llvm_v2f64_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvtph2pd_256
      : ClangBuiltin<"__builtin_ia32_vcvtph2pd256_mask">,
        DefaultAttrsIntrinsic<[ llvm_v4f64_ty ],
                              [ llvm_v8f16_ty, llvm_v4f64_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvtph2pd_512
      : ClangBuiltin<"__builtin_ia32_vcvtph2pd512_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f64_ty ],
                              [ llvm_v8f16_ty, llvm_v8f64_ty, llvm_i8_ty,
                                llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<3>> ]>;
  def int_x86_avx512fp16_mask_vcvtsh2ss_round
      : ClangBuiltin<"__builtin_ia32_vcvtsh2ss_round_mask">,
        DefaultAttrsIntrinsic<[ llvm_v4f32_ty ],
                              [ llvm_v4f32_ty, llvm_v8f16_ty, llvm_v4f32_ty,
                                llvm_i8_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<4>> ]>;
  def int_x86_avx512fp16_mask_vcvtss2sh_round
      : ClangBuiltin<"__builtin_ia32_vcvtss2sh_round_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v8f16_ty, llvm_v4f32_ty, llvm_v8f16_ty,
                                llvm_i8_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<4>> ]>;
  def int_x86_avx512fp16_mask_vcvtsd2sh_round
      : ClangBuiltin<"__builtin_ia32_vcvtsd2sh_round_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v8f16_ty, llvm_v2f64_ty, llvm_v8f16_ty,
                                llvm_i8_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<4>> ]>;
  def int_x86_avx512fp16_mask_vcvtsh2sd_round
      : ClangBuiltin<"__builtin_ia32_vcvtsh2sd_round_mask">,
        DefaultAttrsIntrinsic<[ llvm_v2f64_ty ],
                              [ llvm_v2f64_ty, llvm_v8f16_ty, llvm_v2f64_ty,
                                llvm_i8_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<4>> ]>;

  def int_x86_avx512fp16_mask_vcvtph2w_128
      : ClangBuiltin<"__builtin_ia32_vcvtph2w128_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8i16_ty ],
                              [ llvm_v8f16_ty, llvm_v8i16_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvtph2w_256
      : ClangBuiltin<"__builtin_ia32_vcvtph2w256_mask">,
        DefaultAttrsIntrinsic<[ llvm_v16i16_ty ],
                              [ llvm_v16f16_ty, llvm_v16i16_ty, llvm_i16_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvtph2w_512
      : ClangBuiltin<"__builtin_ia32_vcvtph2w512_mask">,
        DefaultAttrsIntrinsic<[ llvm_v32i16_ty ],
                              [ llvm_v32f16_ty, llvm_v32i16_ty, llvm_i32_ty,
                                llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<3>> ]>;
  def int_x86_avx512fp16_mask_vcvttph2w_128
      : ClangBuiltin<"__builtin_ia32_vcvttph2w128_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8i16_ty ],
                              [ llvm_v8f16_ty, llvm_v8i16_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvttph2w_256
      : ClangBuiltin<"__builtin_ia32_vcvttph2w256_mask">,
        DefaultAttrsIntrinsic<[ llvm_v16i16_ty ],
                              [ llvm_v16f16_ty, llvm_v16i16_ty, llvm_i16_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvttph2w_512
      : ClangBuiltin<"__builtin_ia32_vcvttph2w512_mask">,
        DefaultAttrsIntrinsic<[ llvm_v32i16_ty ],
                              [ llvm_v32f16_ty, llvm_v32i16_ty, llvm_i32_ty,
                                llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<3>> ]>;
  def int_x86_avx512fp16_mask_vcvtph2uw_128
      : ClangBuiltin<"__builtin_ia32_vcvtph2uw128_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8i16_ty ],
                              [ llvm_v8f16_ty, llvm_v8i16_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvtph2uw_256
      : ClangBuiltin<"__builtin_ia32_vcvtph2uw256_mask">,
        DefaultAttrsIntrinsic<[ llvm_v16i16_ty ],
                              [ llvm_v16f16_ty, llvm_v16i16_ty, llvm_i16_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvtph2uw_512
      : ClangBuiltin<"__builtin_ia32_vcvtph2uw512_mask">,
        DefaultAttrsIntrinsic<[ llvm_v32i16_ty ],
                              [ llvm_v32f16_ty, llvm_v32i16_ty, llvm_i32_ty,
                                llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<3>> ]>;
  def int_x86_avx512fp16_mask_vcvttph2uw_128
      : ClangBuiltin<"__builtin_ia32_vcvttph2uw128_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8i16_ty ],
                              [ llvm_v8f16_ty, llvm_v8i16_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvttph2uw_256
      : ClangBuiltin<"__builtin_ia32_vcvttph2uw256_mask">,
        DefaultAttrsIntrinsic<[ llvm_v16i16_ty ],
                              [ llvm_v16f16_ty, llvm_v16i16_ty, llvm_i16_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvttph2uw_512
      : ClangBuiltin<"__builtin_ia32_vcvttph2uw512_mask">,
        DefaultAttrsIntrinsic<[ llvm_v32i16_ty ],
                              [ llvm_v32f16_ty, llvm_v32i16_ty, llvm_i32_ty,
                                llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<3>> ]>;

  def int_x86_avx512fp16_mask_vcvtph2dq_128
      : ClangBuiltin<"__builtin_ia32_vcvtph2dq128_mask">,
        DefaultAttrsIntrinsic<[ llvm_v4i32_ty ],
                              [ llvm_v8f16_ty, llvm_v4i32_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvtph2dq_256
      : ClangBuiltin<"__builtin_ia32_vcvtph2dq256_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8i32_ty ],
                              [ llvm_v8f16_ty, llvm_v8i32_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvtph2dq_512
      : ClangBuiltin<"__builtin_ia32_vcvtph2dq512_mask">,
        DefaultAttrsIntrinsic<[ llvm_v16i32_ty ],
                              [ llvm_v16f16_ty, llvm_v16i32_ty, llvm_i16_ty,
                                llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<3>> ]>;
  def int_x86_avx512fp16_mask_vcvtph2udq_128
      : ClangBuiltin<"__builtin_ia32_vcvtph2udq128_mask">,
        DefaultAttrsIntrinsic<[ llvm_v4i32_ty ],
                              [ llvm_v8f16_ty, llvm_v4i32_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvtph2udq_256
      : ClangBuiltin<"__builtin_ia32_vcvtph2udq256_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8i32_ty ],
                              [ llvm_v8f16_ty, llvm_v8i32_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvtph2udq_512
      : ClangBuiltin<"__builtin_ia32_vcvtph2udq512_mask">,
        DefaultAttrsIntrinsic<[ llvm_v16i32_ty ],
                              [ llvm_v16f16_ty, llvm_v16i32_ty, llvm_i16_ty,
                                llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<3>> ]>;
  def int_x86_avx512fp16_mask_vcvtdq2ph_128
      : ClangBuiltin<"__builtin_ia32_vcvtdq2ph128_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v4i32_ty, llvm_v8f16_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvtudq2ph_128
      : ClangBuiltin<"__builtin_ia32_vcvtudq2ph128_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v4i32_ty, llvm_v8f16_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvttph2dq_128
      : ClangBuiltin<"__builtin_ia32_vcvttph2dq128_mask">,
        DefaultAttrsIntrinsic<[ llvm_v4i32_ty ],
                              [ llvm_v8f16_ty, llvm_v4i32_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvttph2dq_256
      : ClangBuiltin<"__builtin_ia32_vcvttph2dq256_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8i32_ty ],
                              [ llvm_v8f16_ty, llvm_v8i32_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvttph2dq_512
      : ClangBuiltin<"__builtin_ia32_vcvttph2dq512_mask">,
        DefaultAttrsIntrinsic<[ llvm_v16i32_ty ],
                              [ llvm_v16f16_ty, llvm_v16i32_ty, llvm_i16_ty,
                                llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<3>> ]>;
  def int_x86_avx512fp16_mask_vcvttph2udq_128
      : ClangBuiltin<"__builtin_ia32_vcvttph2udq128_mask">,
        DefaultAttrsIntrinsic<[ llvm_v4i32_ty ],
                              [ llvm_v8f16_ty, llvm_v4i32_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvttph2udq_256
      : ClangBuiltin<"__builtin_ia32_vcvttph2udq256_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8i32_ty ],
                              [ llvm_v8f16_ty, llvm_v8i32_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvttph2udq_512
      : ClangBuiltin<"__builtin_ia32_vcvttph2udq512_mask">,
        DefaultAttrsIntrinsic<[ llvm_v16i32_ty ],
                              [ llvm_v16f16_ty, llvm_v16i32_ty, llvm_i16_ty,
                                llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<3>> ]>;

  def int_x86_avx512fp16_mask_vcvtqq2ph_128
      : ClangBuiltin<"__builtin_ia32_vcvtqq2ph128_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v2i64_ty, llvm_v8f16_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvtqq2ph_256
      : ClangBuiltin<"__builtin_ia32_vcvtqq2ph256_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v4i64_ty, llvm_v8f16_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvtph2qq_128
      : ClangBuiltin<"__builtin_ia32_vcvtph2qq128_mask">,
        DefaultAttrsIntrinsic<[ llvm_v2i64_ty ],
                              [ llvm_v8f16_ty, llvm_v2i64_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvtph2qq_256
      : ClangBuiltin<"__builtin_ia32_vcvtph2qq256_mask">,
        DefaultAttrsIntrinsic<[ llvm_v4i64_ty ],
                              [ llvm_v8f16_ty, llvm_v4i64_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvtph2qq_512
      : ClangBuiltin<"__builtin_ia32_vcvtph2qq512_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8i64_ty ],
                              [ llvm_v8f16_ty, llvm_v8i64_ty, llvm_i8_ty,
                                llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<3>> ]>;
  def int_x86_avx512fp16_mask_vcvtuqq2ph_128
      : ClangBuiltin<"__builtin_ia32_vcvtuqq2ph128_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v2i64_ty, llvm_v8f16_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvtuqq2ph_256
      : ClangBuiltin<"__builtin_ia32_vcvtuqq2ph256_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v4i64_ty, llvm_v8f16_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvtph2uqq_128
      : ClangBuiltin<"__builtin_ia32_vcvtph2uqq128_mask">,
        DefaultAttrsIntrinsic<[ llvm_v2i64_ty ],
                              [ llvm_v8f16_ty, llvm_v2i64_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvtph2uqq_256
      : ClangBuiltin<"__builtin_ia32_vcvtph2uqq256_mask">,
        DefaultAttrsIntrinsic<[ llvm_v4i64_ty ],
                              [ llvm_v8f16_ty, llvm_v4i64_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvtph2uqq_512
      : ClangBuiltin<"__builtin_ia32_vcvtph2uqq512_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8i64_ty ],
                              [ llvm_v8f16_ty, llvm_v8i64_ty, llvm_i8_ty,
                                llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<3>> ]>;
  def int_x86_avx512fp16_mask_vcvttph2qq_128
      : ClangBuiltin<"__builtin_ia32_vcvttph2qq128_mask">,
        DefaultAttrsIntrinsic<[ llvm_v2i64_ty ],
                              [ llvm_v8f16_ty, llvm_v2i64_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvttph2qq_256
      : ClangBuiltin<"__builtin_ia32_vcvttph2qq256_mask">,
        DefaultAttrsIntrinsic<[ llvm_v4i64_ty ],
                              [ llvm_v8f16_ty, llvm_v4i64_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvttph2qq_512
      : ClangBuiltin<"__builtin_ia32_vcvttph2qq512_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8i64_ty ],
                              [ llvm_v8f16_ty, llvm_v8i64_ty, llvm_i8_ty,
                                llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<3>> ]>;
  def int_x86_avx512fp16_mask_vcvttph2uqq_128
      : ClangBuiltin<"__builtin_ia32_vcvttph2uqq128_mask">,
        DefaultAttrsIntrinsic<[ llvm_v2i64_ty ],
                              [ llvm_v8f16_ty, llvm_v2i64_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvttph2uqq_256
      : ClangBuiltin<"__builtin_ia32_vcvttph2uqq256_mask">,
        DefaultAttrsIntrinsic<[ llvm_v4i64_ty ],
                              [ llvm_v8f16_ty, llvm_v4i64_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vcvttph2uqq_512
      : ClangBuiltin<"__builtin_ia32_vcvttph2uqq512_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8i64_ty ],
                              [ llvm_v8f16_ty, llvm_v8i64_ty, llvm_i8_ty,
                                llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<3>> ]>;

  def int_x86_avx512fp16_vcvtsh2si32
      : ClangBuiltin<"__builtin_ia32_vcvtsh2si32">,
        DefaultAttrsIntrinsic<[ llvm_i32_ty ], [ llvm_v8f16_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<1>> ]>;
  def int_x86_avx512fp16_vcvtsh2usi32
      : ClangBuiltin<"__builtin_ia32_vcvtsh2usi32">,
        DefaultAttrsIntrinsic<[ llvm_i32_ty ], [ llvm_v8f16_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<1>> ]>;
  def int_x86_avx512fp16_vcvtsh2si64
      : ClangBuiltin<"__builtin_ia32_vcvtsh2si64">,
        DefaultAttrsIntrinsic<[ llvm_i64_ty ], [ llvm_v8f16_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<1>> ]>;
  def int_x86_avx512fp16_vcvtsh2usi64
      : ClangBuiltin<"__builtin_ia32_vcvtsh2usi64">,
        DefaultAttrsIntrinsic<[ llvm_i64_ty ], [ llvm_v8f16_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<1>> ]>;
  def int_x86_avx512fp16_vcvtusi2sh
      : ClangBuiltin<"__builtin_ia32_vcvtusi2sh">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v8f16_ty, llvm_i32_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<2>> ]>;
  def int_x86_avx512fp16_vcvtusi642sh
      : ClangBuiltin<"__builtin_ia32_vcvtusi642sh">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v8f16_ty, llvm_i64_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<2>> ]>;
  def int_x86_avx512fp16_vcvtsi2sh
      : ClangBuiltin<"__builtin_ia32_vcvtsi2sh">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v8f16_ty, llvm_i32_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<2>> ]>;
  def int_x86_avx512fp16_vcvtsi642sh
      : ClangBuiltin<"__builtin_ia32_vcvtsi642sh">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v8f16_ty, llvm_i64_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<2>> ]>;
  def int_x86_avx512fp16_vcvttsh2si32
      : ClangBuiltin<"__builtin_ia32_vcvttsh2si32">,
        DefaultAttrsIntrinsic<[ llvm_i32_ty ], [ llvm_v8f16_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<1>> ]>;
  def int_x86_avx512fp16_vcvttsh2si64
      : ClangBuiltin<"__builtin_ia32_vcvttsh2si64">,
        DefaultAttrsIntrinsic<[ llvm_i64_ty ], [ llvm_v8f16_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<1>> ]>;
  def int_x86_avx512fp16_vcvttsh2usi32
      : ClangBuiltin<"__builtin_ia32_vcvttsh2usi32">,
        DefaultAttrsIntrinsic<[ llvm_i32_ty ], [ llvm_v8f16_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<1>> ]>;
  def int_x86_avx512fp16_vcvttsh2usi64
      : ClangBuiltin<"__builtin_ia32_vcvttsh2usi64">,
        DefaultAttrsIntrinsic<[ llvm_i64_ty ], [ llvm_v8f16_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<1>> ]>;

  def int_x86_avx512fp16_sqrt_ph_512
      : DefaultAttrsIntrinsic<[ llvm_v32f16_ty ],
                              [ llvm_v32f16_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<1>> ]>;
  def int_x86_avx512fp16_mask_sqrt_sh
      : DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v8f16_ty, llvm_v8f16_ty, llvm_v8f16_ty,
                                llvm_i8_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<4>> ]>;
  def int_x86_avx512fp16_mask_rsqrt_ph_128
      : ClangBuiltin<"__builtin_ia32_rsqrtph128_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v8f16_ty, llvm_v8f16_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_rsqrt_ph_256
      : ClangBuiltin<"__builtin_ia32_rsqrtph256_mask">,
        DefaultAttrsIntrinsic<[ llvm_v16f16_ty ],
                              [ llvm_v16f16_ty, llvm_v16f16_ty, llvm_i16_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_rsqrt_ph_512
      : ClangBuiltin<"__builtin_ia32_rsqrtph512_mask">,
        DefaultAttrsIntrinsic<[ llvm_v32f16_ty ],
                              [ llvm_v32f16_ty, llvm_v32f16_ty, llvm_i32_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_rsqrt_sh
      : ClangBuiltin<"__builtin_ia32_rsqrtsh_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v8f16_ty, llvm_v8f16_ty, llvm_v8f16_ty,
                                llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_rcp_ph_128
      : ClangBuiltin<"__builtin_ia32_rcpph128_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v8f16_ty, llvm_v8f16_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_rcp_ph_256
      : ClangBuiltin<"__builtin_ia32_rcpph256_mask">,
        DefaultAttrsIntrinsic<[ llvm_v16f16_ty ],
                              [ llvm_v16f16_ty, llvm_v16f16_ty, llvm_i16_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_rcp_ph_512
      : ClangBuiltin<"__builtin_ia32_rcpph512_mask">,
        DefaultAttrsIntrinsic<[ llvm_v32f16_ty ],
                              [ llvm_v32f16_ty, llvm_v32f16_ty, llvm_i32_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_rcp_sh
      : ClangBuiltin<"__builtin_ia32_rcpsh_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v8f16_ty, llvm_v8f16_ty, llvm_v8f16_ty,
                                llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_reduce_ph_128
      : ClangBuiltin<"__builtin_ia32_reduceph128_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v8f16_ty, llvm_i32_ty, llvm_v8f16_ty,
                                llvm_i8_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<1>> ]>;
  def int_x86_avx512fp16_mask_reduce_ph_256
      : ClangBuiltin<"__builtin_ia32_reduceph256_mask">,
        DefaultAttrsIntrinsic<[ llvm_v16f16_ty ],
                              [ llvm_v16f16_ty, llvm_i32_ty, llvm_v16f16_ty,
                                llvm_i16_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<1>> ]>;
  def int_x86_avx512fp16_mask_reduce_ph_512
      : ClangBuiltin<"__builtin_ia32_reduceph512_mask">,
        DefaultAttrsIntrinsic<[ llvm_v32f16_ty ],
                              [ llvm_v32f16_ty, llvm_i32_ty, llvm_v32f16_ty,
                                llvm_i32_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<1>>,
                                ImmArg<ArgIndex<4>> ]>;
  def int_x86_avx512fp16_mask_reduce_sh
      : ClangBuiltin<"__builtin_ia32_reducesh_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v8f16_ty, llvm_v8f16_ty, llvm_v8f16_ty,
                                llvm_i8_ty, llvm_i32_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<4>>,
                                ImmArg<ArgIndex<5>> ]>;
  def int_x86_avx512fp16_fpclass_ph_128
      : DefaultAttrsIntrinsic<[ llvm_v8i1_ty ], [ llvm_v8f16_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<1>> ]>;
  def int_x86_avx512fp16_fpclass_ph_256
      : DefaultAttrsIntrinsic<[ llvm_v16i1_ty ],
                              [ llvm_v16f16_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<1>> ]>;
  def int_x86_avx512fp16_fpclass_ph_512
      : DefaultAttrsIntrinsic<[ llvm_v32i1_ty ],
                              [ llvm_v32f16_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<1>> ]>;
  def int_x86_avx512fp16_mask_fpclass_sh
      : ClangBuiltin<"__builtin_ia32_fpclasssh_mask">,
        DefaultAttrsIntrinsic<[ llvm_i8_ty ],
                              [ llvm_v8f16_ty, llvm_i32_ty, llvm_i8_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<1>> ]>;
  def int_x86_avx512fp16_mask_getexp_ph_128
      : ClangBuiltin<"__builtin_ia32_getexpph128_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v8f16_ty, llvm_v8f16_ty, llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_getexp_ph_256
      : ClangBuiltin<"__builtin_ia32_getexpph256_mask">,
        DefaultAttrsIntrinsic<[ llvm_v16f16_ty ],
                              [ llvm_v16f16_ty, llvm_v16f16_ty, llvm_i16_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_getexp_ph_512
      : ClangBuiltin<"__builtin_ia32_getexpph512_mask">,
        DefaultAttrsIntrinsic<[ llvm_v32f16_ty ],
                              [ llvm_v32f16_ty, llvm_v32f16_ty, llvm_i32_ty,
                                llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<3>> ]>;
  def int_x86_avx512fp16_mask_getexp_sh
      : ClangBuiltin<"__builtin_ia32_getexpsh128_round_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v8f16_ty, llvm_v8f16_ty, llvm_v8f16_ty,
                                llvm_i8_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<4>> ]>;
  def int_x86_avx512fp16_mask_getmant_ph_128
      : ClangBuiltin<"__builtin_ia32_getmantph128_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v8f16_ty, llvm_i32_ty, llvm_v8f16_ty,
                                llvm_i8_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<1>> ]>;
  def int_x86_avx512fp16_mask_getmant_ph_256
      : ClangBuiltin<"__builtin_ia32_getmantph256_mask">,
        DefaultAttrsIntrinsic<[ llvm_v16f16_ty ],
                              [ llvm_v16f16_ty, llvm_i32_ty, llvm_v16f16_ty,
                                llvm_i16_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<1>> ]>;
  def int_x86_avx512fp16_mask_getmant_ph_512
      : ClangBuiltin<"__builtin_ia32_getmantph512_mask">,
        DefaultAttrsIntrinsic<[ llvm_v32f16_ty ],
                              [ llvm_v32f16_ty, llvm_i32_ty, llvm_v32f16_ty,
                                llvm_i32_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<1>>,
                                ImmArg<ArgIndex<4>> ]>;
  def int_x86_avx512fp16_mask_getmant_sh
      : ClangBuiltin<"__builtin_ia32_getmantsh_round_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v8f16_ty, llvm_v8f16_ty, llvm_i32_ty,
                                llvm_v8f16_ty, llvm_i8_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<2>>,
                                ImmArg<ArgIndex<5>> ]>;
  def int_x86_avx512fp16_mask_rndscale_ph_128
      : ClangBuiltin<"__builtin_ia32_rndscaleph_128_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v8f16_ty, llvm_i32_ty, llvm_v8f16_ty,
                                llvm_i8_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<1>> ]>;
  def int_x86_avx512fp16_mask_rndscale_ph_256
      : ClangBuiltin<"__builtin_ia32_rndscaleph_256_mask">,
        DefaultAttrsIntrinsic<[ llvm_v16f16_ty ],
                              [ llvm_v16f16_ty, llvm_i32_ty, llvm_v16f16_ty,
                                llvm_i16_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<1>> ]>;
  def int_x86_avx512fp16_mask_rndscale_ph_512
      : ClangBuiltin<"__builtin_ia32_rndscaleph_mask">,
        DefaultAttrsIntrinsic<[ llvm_v32f16_ty ],
                              [ llvm_v32f16_ty, llvm_i32_ty, llvm_v32f16_ty,
                                llvm_i32_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<1>>,
                                ImmArg<ArgIndex<4>> ]>;
  def int_x86_avx512fp16_mask_rndscale_sh
      : ClangBuiltin<"__builtin_ia32_rndscalesh_round_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v8f16_ty, llvm_v8f16_ty, llvm_v8f16_ty,
                                llvm_i8_ty, llvm_i32_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<4>>,
                                ImmArg<ArgIndex<5>> ]>;
  def int_x86_avx512fp16_mask_scalef_ph_128
      : ClangBuiltin<"__builtin_ia32_scalefph128_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v8f16_ty, llvm_v8f16_ty, llvm_v8f16_ty,
                                llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_scalef_ph_256
      : ClangBuiltin<"__builtin_ia32_scalefph256_mask">,
        DefaultAttrsIntrinsic<[ llvm_v16f16_ty ],
                              [ llvm_v16f16_ty, llvm_v16f16_ty, llvm_v16f16_ty,
                                llvm_i16_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_scalef_ph_512
      : ClangBuiltin<"__builtin_ia32_scalefph512_mask">,
        DefaultAttrsIntrinsic<[ llvm_v32f16_ty ],
                              [ llvm_v32f16_ty, llvm_v32f16_ty, llvm_v32f16_ty,
                                llvm_i32_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<4>> ]>;
  def int_x86_avx512fp16_mask_scalef_sh
      : ClangBuiltin<"__builtin_ia32_scalefsh_round_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v8f16_ty, llvm_v8f16_ty, llvm_v8f16_ty,
                                llvm_i8_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<4>> ]>;

  def int_x86_avx512fp16_vfmadd_ph_512
      : DefaultAttrsIntrinsic<[ llvm_v32f16_ty ],
                              [ llvm_v32f16_ty, llvm_v32f16_ty, llvm_v32f16_ty,
                                llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<3>> ]>;
  def int_x86_avx512fp16_vfmaddsub_ph_128
      : ClangBuiltin<"__builtin_ia32_vfmaddsubph">,
        DefaultAttrsIntrinsic<[ llvm_v8f16_ty ],
                              [ llvm_v8f16_ty, llvm_v8f16_ty, llvm_v8f16_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_vfmaddsub_ph_256
      : ClangBuiltin<"__builtin_ia32_vfmaddsubph256">,
        DefaultAttrsIntrinsic<[ llvm_v16f16_ty ],
                              [ llvm_v16f16_ty, llvm_v16f16_ty,
                                llvm_v16f16_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_vfmaddsub_ph_512
      : DefaultAttrsIntrinsic<[ llvm_v32f16_ty ],
                              [ llvm_v32f16_ty, llvm_v32f16_ty, llvm_v32f16_ty,
                                llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<3>> ]>;
  def int_x86_avx512fp16_vfmadd_f16
      : DefaultAttrsIntrinsic<[ llvm_half_ty ],
                              [ llvm_half_ty, llvm_half_ty, llvm_half_ty,
                                llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<3>> ]>;

  def int_x86_avx512fp16_mask_vfcmadd_cph_128
      : ClangBuiltin<"__builtin_ia32_vfcmaddcph128_mask">,
        DefaultAttrsIntrinsic<[ llvm_v4f32_ty ],
                              [ llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4f32_ty,
                                llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_maskz_vfcmadd_cph_128
      : ClangBuiltin<"__builtin_ia32_vfcmaddcph128_maskz">,
        DefaultAttrsIntrinsic<[ llvm_v4f32_ty ],
                              [ llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4f32_ty,
                                llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vfcmadd_cph_256
      : ClangBuiltin<"__builtin_ia32_vfcmaddcph256_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f32_ty ],
                              [ llvm_v8f32_ty, llvm_v8f32_ty, llvm_v8f32_ty,
                                llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_maskz_vfcmadd_cph_256
      : ClangBuiltin<"__builtin_ia32_vfcmaddcph256_maskz">,
        DefaultAttrsIntrinsic<[ llvm_v8f32_ty ],
                              [ llvm_v8f32_ty, llvm_v8f32_ty, llvm_v8f32_ty,
                                llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vfcmadd_cph_512
      : ClangBuiltin<"__builtin_ia32_vfcmaddcph512_mask3">,
        DefaultAttrsIntrinsic<[ llvm_v16f32_ty ],
                              [ llvm_v16f32_ty, llvm_v16f32_ty, llvm_v16f32_ty,
                                llvm_i16_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<4>> ]>;
  def int_x86_avx512fp16_maskz_vfcmadd_cph_512
      : ClangBuiltin<"__builtin_ia32_vfcmaddcph512_maskz">,
        DefaultAttrsIntrinsic<[ llvm_v16f32_ty ],
                              [ llvm_v16f32_ty, llvm_v16f32_ty, llvm_v16f32_ty,
                                llvm_i16_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<4>> ]>;
  def int_x86_avx512fp16_mask_vfmadd_cph_128
      : ClangBuiltin<"__builtin_ia32_vfmaddcph128_mask">,
        DefaultAttrsIntrinsic<[ llvm_v4f32_ty ],
                              [ llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4f32_ty,
                                llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_maskz_vfmadd_cph_128
      : ClangBuiltin<"__builtin_ia32_vfmaddcph128_maskz">,
        DefaultAttrsIntrinsic<[ llvm_v4f32_ty ],
                              [ llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4f32_ty,
                                llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vfmadd_cph_256
      : ClangBuiltin<"__builtin_ia32_vfmaddcph256_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f32_ty ],
                              [ llvm_v8f32_ty, llvm_v8f32_ty, llvm_v8f32_ty,
                                llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_maskz_vfmadd_cph_256
      : ClangBuiltin<"__builtin_ia32_vfmaddcph256_maskz">,
        DefaultAttrsIntrinsic<[ llvm_v8f32_ty ],
                              [ llvm_v8f32_ty, llvm_v8f32_ty, llvm_v8f32_ty,
                                llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vfmadd_cph_512
      : ClangBuiltin<"__builtin_ia32_vfmaddcph512_mask3">,
        DefaultAttrsIntrinsic<[ llvm_v16f32_ty ],
                              [ llvm_v16f32_ty, llvm_v16f32_ty, llvm_v16f32_ty,
                                llvm_i16_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<4>> ]>;
  def int_x86_avx512fp16_maskz_vfmadd_cph_512
      : ClangBuiltin<"__builtin_ia32_vfmaddcph512_maskz">,
        DefaultAttrsIntrinsic<[ llvm_v16f32_ty ],
                              [ llvm_v16f32_ty, llvm_v16f32_ty, llvm_v16f32_ty,
                                llvm_i16_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<4>> ]>;
  def int_x86_avx512fp16_mask_vfmadd_csh
      : ClangBuiltin<"__builtin_ia32_vfmaddcsh_mask">,
        DefaultAttrsIntrinsic<[ llvm_v4f32_ty ],
                              [ llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4f32_ty,
                                llvm_i8_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<4>> ]>;
  def int_x86_avx512fp16_maskz_vfmadd_csh
      : ClangBuiltin<"__builtin_ia32_vfmaddcsh_maskz">,
        DefaultAttrsIntrinsic<[ llvm_v4f32_ty ],
                              [ llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4f32_ty,
                                llvm_i8_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<4>> ]>;
  def int_x86_avx512fp16_mask_vfcmadd_csh
      : ClangBuiltin<"__builtin_ia32_vfcmaddcsh_mask">,
        DefaultAttrsIntrinsic<[ llvm_v4f32_ty ],
                              [ llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4f32_ty,
                                llvm_i8_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<4>> ]>;
  def int_x86_avx512fp16_maskz_vfcmadd_csh
      : ClangBuiltin<"__builtin_ia32_vfcmaddcsh_maskz">,
        DefaultAttrsIntrinsic<[ llvm_v4f32_ty ],
                              [ llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4f32_ty,
                                llvm_i8_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<4>> ]>;
  def int_x86_avx512fp16_mask_vfmul_cph_128
      : ClangBuiltin<"__builtin_ia32_vfmulcph128_mask">,
        DefaultAttrsIntrinsic<[ llvm_v4f32_ty ],
                              [ llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4f32_ty,
                                llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vfcmul_cph_128
      : ClangBuiltin<"__builtin_ia32_vfcmulcph128_mask">,
        DefaultAttrsIntrinsic<[ llvm_v4f32_ty ],
                              [ llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4f32_ty,
                                llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vfmul_cph_256
      : ClangBuiltin<"__builtin_ia32_vfmulcph256_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f32_ty ],
                              [ llvm_v8f32_ty, llvm_v8f32_ty, llvm_v8f32_ty,
                                llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vfcmul_cph_256
      : ClangBuiltin<"__builtin_ia32_vfcmulcph256_mask">,
        DefaultAttrsIntrinsic<[ llvm_v8f32_ty ],
                              [ llvm_v8f32_ty, llvm_v8f32_ty, llvm_v8f32_ty,
                                llvm_i8_ty ],
                              [ IntrNoMem ]>;
  def int_x86_avx512fp16_mask_vfmul_cph_512
      : ClangBuiltin<"__builtin_ia32_vfmulcph512_mask">,
        DefaultAttrsIntrinsic<[ llvm_v16f32_ty ],
                              [ llvm_v16f32_ty, llvm_v16f32_ty, llvm_v16f32_ty,
                                llvm_i16_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<4>> ]>;
  def int_x86_avx512fp16_mask_vfcmul_cph_512
      : ClangBuiltin<"__builtin_ia32_vfcmulcph512_mask">,
        DefaultAttrsIntrinsic<[ llvm_v16f32_ty ],
                              [ llvm_v16f32_ty, llvm_v16f32_ty, llvm_v16f32_ty,
                                llvm_i16_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<4>> ]>;
  def int_x86_avx512fp16_mask_vfmul_csh
      : ClangBuiltin<"__builtin_ia32_vfmulcsh_mask">,
        DefaultAttrsIntrinsic<[ llvm_v4f32_ty ],
                              [ llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4f32_ty,
                                llvm_i8_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<4>> ]>;
  def int_x86_avx512fp16_mask_vfcmul_csh
      : ClangBuiltin<"__builtin_ia32_vfcmulcsh_mask">,
        DefaultAttrsIntrinsic<[ llvm_v4f32_ty ],
                              [ llvm_v4f32_ty, llvm_v4f32_ty, llvm_v4f32_ty,
                                llvm_i8_ty, llvm_i32_ty ],
                              [ IntrNoMem, ImmArg<ArgIndex<4>> ]>;
}
