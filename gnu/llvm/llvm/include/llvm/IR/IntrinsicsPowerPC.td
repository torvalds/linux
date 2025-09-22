//===- IntrinsicsPowerPC.td - Defines PowerPC intrinsics ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines all of the PowerPC-specific intrinsics.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Definitions for all PowerPC intrinsics.
//

// Non-altivec intrinsics.
let TargetPrefix = "ppc" in {  // All intrinsics start with "llvm.ppc.".
  // dcba/dcbf/dcbi/dcbst/dcbt/dcbz/dcbzl(PPC970) instructions.
  def int_ppc_dcba  : Intrinsic<[], [llvm_ptr_ty], []>;
  def int_ppc_dcbf : ClangBuiltin<"__builtin_dcbf">,
                      Intrinsic<[], [llvm_ptr_ty], [IntrArgMemOnly]>;
  def int_ppc_dcbfps : Intrinsic<[], [llvm_ptr_ty], [IntrArgMemOnly]>;
  def int_ppc_dcbstps : Intrinsic<[], [llvm_ptr_ty], [IntrArgMemOnly]>;
  def int_ppc_dcbi  : Intrinsic<[], [llvm_ptr_ty], []>;
  def int_ppc_dcbt_with_hint: Intrinsic<[], [llvm_ptr_ty, llvm_i32_ty],
    [IntrArgMemOnly, NoCapture<ArgIndex<0>>, ImmArg<ArgIndex<1>>]>;
  def int_ppc_dcbtst_with_hint: Intrinsic<[], [llvm_ptr_ty, llvm_i32_ty],
    [IntrArgMemOnly, NoCapture<ArgIndex<0>>, ImmArg<ArgIndex<1>>]>;
  def int_ppc_dcbzl : Intrinsic<[], [llvm_ptr_ty], []>;

  // Emit pseudo instruction as fence of instruction motion
  def int_ppc_fence : ClangBuiltin<"__builtin_ppc_fence">,
                      DefaultAttrsIntrinsic<[], [],
                                            [IntrNoMerge, IntrHasSideEffects]>;

  // Get content from current FPSCR register
  def int_ppc_readflm : ClangBuiltin<"__builtin_readflm">,
                        DefaultAttrsIntrinsic<[llvm_double_ty], [],
                                              [IntrNoMerge, IntrHasSideEffects]>;
  def int_ppc_mffsl : ClangBuiltin<"__builtin_ppc_mffsl">,
                      DefaultAttrsIntrinsic<[llvm_double_ty], [],
                                            [IntrNoMerge, IntrHasSideEffects]>;

  // Set FPSCR register, and return previous content
  def int_ppc_setflm : ClangBuiltin<"__builtin_setflm">,
                       DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty],
                                             [IntrHasSideEffects]>;

  // Intrinsics for [double]word extended forms of divide instructions
  def int_ppc_divwe : ClangBuiltin<"__builtin_divwe">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem]>;
  def int_ppc_divweu : ClangBuiltin<"__builtin_divweu">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem]>;
  def int_ppc_divde : ClangBuiltin<"__builtin_divde">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty],
                            [IntrNoMem]>;
  def int_ppc_divdeu : ClangBuiltin<"__builtin_divdeu">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty],
                            [IntrNoMem]>;

  def int_ppc_unpack_longdouble : ClangBuiltin<"__builtin_unpack_longdouble">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_ppcf128_ty, llvm_i32_ty],
                            [IntrNoMem]>;
  def int_ppc_pack_longdouble : ClangBuiltin<"__builtin_pack_longdouble">,
      DefaultAttrsIntrinsic<[llvm_ppcf128_ty], [llvm_double_ty, llvm_double_ty],
                            [IntrNoMem]>;

  // Generate a random number
  def int_ppc_darn : ClangBuiltin<"__builtin_darn">,
                     DefaultAttrsIntrinsic<[llvm_i64_ty], [],
                                           [IntrNoMerge, IntrHasSideEffects]>;
  def int_ppc_darnraw : ClangBuiltin<"__builtin_darn_raw">,
                     DefaultAttrsIntrinsic<[llvm_i64_ty], [],
                                           [IntrNoMerge, IntrHasSideEffects]>;
  def int_ppc_darn32 : ClangBuiltin<"__builtin_darn_32">,
                     DefaultAttrsIntrinsic<[llvm_i32_ty], [],
                                           [IntrNoMerge, IntrHasSideEffects]>;

  // Bit permute doubleword
  def int_ppc_bpermd : ClangBuiltin<"__builtin_bpermd">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty],
                            [IntrNoMem]>;

  // Parallel Bits Deposit/Extract Doubleword Builtins.
  def int_ppc_pdepd
      : ClangBuiltin<"__builtin_pdepd">,
        DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty],
                              [IntrNoMem]>;
  def int_ppc_pextd
      : ClangBuiltin<"__builtin_pextd">,
        DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty],
                              [IntrNoMem]>;

  // Centrifuge Doubleword Builtin.
  def int_ppc_cfuged
      : ClangBuiltin<"__builtin_cfuged">,
        DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty],
                              [IntrNoMem]>;

  // Count Leading / Trailing Zeroes under bit Mask Builtins.
  def int_ppc_cntlzdm
      : ClangBuiltin<"__builtin_cntlzdm">,
        DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty],
                              [IntrNoMem]>;
  def int_ppc_cnttzdm
      : ClangBuiltin<"__builtin_cnttzdm">,
        DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty],
                              [IntrNoMem]>;

  def int_ppc_truncf128_round_to_odd
      : ClangBuiltin<"__builtin_truncf128_round_to_odd">,
        DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_f128_ty], [IntrNoMem]>;
  def int_ppc_sqrtf128_round_to_odd
      : ClangBuiltin<"__builtin_sqrtf128_round_to_odd">,
        DefaultAttrsIntrinsic<[llvm_f128_ty], [llvm_f128_ty], [IntrNoMem]>;
  def int_ppc_addf128_round_to_odd
      : ClangBuiltin<"__builtin_addf128_round_to_odd">,
        DefaultAttrsIntrinsic<[llvm_f128_ty], [llvm_f128_ty,llvm_f128_ty],
                              [IntrNoMem]>;
  def int_ppc_subf128_round_to_odd
      : ClangBuiltin<"__builtin_subf128_round_to_odd">,
        DefaultAttrsIntrinsic<[llvm_f128_ty], [llvm_f128_ty,llvm_f128_ty],
                              [IntrNoMem]>;
  def int_ppc_mulf128_round_to_odd
      : ClangBuiltin<"__builtin_mulf128_round_to_odd">,
        DefaultAttrsIntrinsic<[llvm_f128_ty], [llvm_f128_ty,llvm_f128_ty],
                              [IntrNoMem]>;
  def int_ppc_divf128_round_to_odd
      : ClangBuiltin<"__builtin_divf128_round_to_odd">,
        DefaultAttrsIntrinsic<[llvm_f128_ty], [llvm_f128_ty,llvm_f128_ty],
                              [IntrNoMem]>;
  def int_ppc_fmaf128_round_to_odd
      : ClangBuiltin<"__builtin_fmaf128_round_to_odd">,
        DefaultAttrsIntrinsic<[llvm_f128_ty],
                              [llvm_f128_ty,llvm_f128_ty,llvm_f128_ty],
                              [IntrNoMem]>;
  def int_ppc_scalar_extract_expq
      : ClangBuiltin<"__builtin_vsx_scalar_extract_expq">,
        DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_f128_ty], [IntrNoMem]>;
  def int_ppc_scalar_insert_exp_qp
      : ClangBuiltin<"__builtin_vsx_scalar_insert_exp_qp">,
        DefaultAttrsIntrinsic<[llvm_f128_ty], [llvm_f128_ty, llvm_i64_ty],
                              [IntrNoMem]>;

  // Intrinsics defined to maintain XL compatibility
  def int_ppc_tdw
      : ClangBuiltin<"__builtin_ppc_tdw">,
        Intrinsic <[], [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty], [ImmArg<ArgIndex<2>>]>;
  def int_ppc_tw
      : ClangBuiltin<"__builtin_ppc_tw">,
        Intrinsic <[], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], [ImmArg<ArgIndex<2>>]>;
  def int_ppc_trapd
      : ClangBuiltin<"__builtin_ppc_trapd">,
        Intrinsic <[], [llvm_i64_ty], []>;
  def int_ppc_trap
      : ClangBuiltin<"__builtin_ppc_trap">,
        Intrinsic <[], [llvm_i32_ty], []>;
  def int_ppc_fcfid
      : ClangBuiltin<"__builtin_ppc_fcfid">,
        DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty], [IntrNoMem]>;
  def int_ppc_fcfud
      : ClangBuiltin<"__builtin_ppc_fcfud">,
        DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty], [IntrNoMem]>;
  def int_ppc_fctid
      : ClangBuiltin<"__builtin_ppc_fctid">,
        DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty], [IntrNoMem]>;
  def int_ppc_fctidz
      : ClangBuiltin<"__builtin_ppc_fctidz">,
        DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty], [IntrNoMem]>;
  def int_ppc_fctiw
      : ClangBuiltin<"__builtin_ppc_fctiw">,
        DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty], [IntrNoMem]>;
  def int_ppc_fctiwz
      : ClangBuiltin<"__builtin_ppc_fctiwz">,
        DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty], [IntrNoMem]>;
  def int_ppc_fctudz
      : ClangBuiltin<"__builtin_ppc_fctudz">,
        DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty], [IntrNoMem]>;
  def int_ppc_fctuwz
      : ClangBuiltin<"__builtin_ppc_fctuwz">,
        DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty], [IntrNoMem]>;
  def int_ppc_rlwimi
      : ClangBuiltin<"__builtin_ppc_rlwimi">,
        DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
                              [IntrNoMem, ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<3>>]>;
  def int_ppc_rlwnm
      : ClangBuiltin<"__builtin_ppc_rlwnm">,
        DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
                              [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_ppc_rldimi
      : DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty, llvm_i64_ty],
                              [IntrNoMem, ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<3>>]>;

  // XL compatible select functions
  // TODO: Add llvm_f128_ty support.
  def int_ppc_maxfe
      : DefaultAttrsIntrinsic<
            [llvm_ppcf128_ty],
            [llvm_ppcf128_ty, llvm_ppcf128_ty, llvm_ppcf128_ty, llvm_vararg_ty],
            [IntrNoMem]>;
  def int_ppc_maxfl
      : DefaultAttrsIntrinsic<
            [llvm_double_ty],
            [llvm_double_ty, llvm_double_ty, llvm_double_ty, llvm_vararg_ty],
            [IntrNoMem]>;
  def int_ppc_maxfs
      : DefaultAttrsIntrinsic<
            [llvm_float_ty],
            [llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_vararg_ty],
            [IntrNoMem]>;
  def int_ppc_minfe
      : DefaultAttrsIntrinsic<
            [llvm_ppcf128_ty],
            [llvm_ppcf128_ty, llvm_ppcf128_ty, llvm_ppcf128_ty, llvm_vararg_ty],
            [IntrNoMem]>;
  def int_ppc_minfl
      : DefaultAttrsIntrinsic<
            [llvm_double_ty],
            [llvm_double_ty, llvm_double_ty, llvm_double_ty, llvm_vararg_ty],
            [IntrNoMem]>;
  def int_ppc_minfs
      : DefaultAttrsIntrinsic<
            [llvm_float_ty],
            [llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_vararg_ty],
            [IntrNoMem]>;
  // Load of a value provided by the system library at a fixed address. Used for
  // accessing things like the HWCAP word provided by Glibc. The immediate
  // argument is not an address but a value defined in
  // include/llvm/TargetParser/PPCTargetParser.def. Each of the values provided
  // by Glibc is a 32-bit word.
  def int_ppc_fixed_addr_ld
      : DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty],
                              [IntrInaccessibleMemOnly, ImmArg<ArgIndex<0>>]>;

}

let TargetPrefix = "ppc" in {  // All PPC intrinsics start with "llvm.ppc.".
  /// PowerPC_Vec_Intrinsic - Base class for all altivec intrinsics.
  class PowerPC_Vec_Intrinsic<string GCCIntSuffix, list<LLVMType> ret_types,
                              list<LLVMType> param_types,
                              list<IntrinsicProperty> properties>
    : ClangBuiltin<!strconcat("__builtin_altivec_", GCCIntSuffix)>,
      DefaultAttrsIntrinsic<ret_types, param_types, properties>;

  /// PowerPC_VSX_Intrinsic - Base class for all VSX intrinsics.
  class PowerPC_VSX_Intrinsic<string GCCIntSuffix, list<LLVMType> ret_types,
                              list<LLVMType> param_types,
                              list<IntrinsicProperty> properties>
    : ClangBuiltin<!strconcat("__builtin_vsx_", GCCIntSuffix)>,
      DefaultAttrsIntrinsic<ret_types, param_types, properties>;
}

//===----------------------------------------------------------------------===//
// PowerPC MMA Intrinsic Multi Class Definitions.
//

multiclass PowerPC_MMA_ACC_Intrinsic<list<LLVMType> args> {
  def NAME: DefaultAttrsIntrinsic<[llvm_v512i1_ty], args, [IntrNoMem]>;
  def pp : DefaultAttrsIntrinsic<[llvm_v512i1_ty],
                                 !listconcat([llvm_v512i1_ty], args),
                                 [IntrNoMem]>;
  def pn : DefaultAttrsIntrinsic<[llvm_v512i1_ty],
                                 !listconcat([llvm_v512i1_ty], args),
                                 [IntrNoMem]>;
  def np : DefaultAttrsIntrinsic<[llvm_v512i1_ty],
                                 !listconcat([llvm_v512i1_ty], args),
                                 [IntrNoMem]>;
  def nn : DefaultAttrsIntrinsic<[llvm_v512i1_ty],
                                 !listconcat([llvm_v512i1_ty], args),
                                 [IntrNoMem]>;
}

multiclass PowerPC_MMA_ACC_PP_Intrinsic<list<LLVMType> args> {
  def NAME: DefaultAttrsIntrinsic<[llvm_v512i1_ty], args, [IntrNoMem]>;
  def pp : DefaultAttrsIntrinsic<[llvm_v512i1_ty],
                                 !listconcat([llvm_v512i1_ty], args),
                                 [IntrNoMem]>;
}

//===----------------------------------------------------------------------===//
// PowerPC Altivec Intrinsic Class Definitions.
//

/// PowerPC_Vec_FF_Intrinsic - A PowerPC intrinsic that takes one v4f32
/// vector and returns one.  These intrinsics have no side effects.
class PowerPC_Vec_FF_Intrinsic<string GCCIntSuffix>
  : PowerPC_Vec_Intrinsic<GCCIntSuffix,
                          [llvm_v4f32_ty], [llvm_v4f32_ty], [IntrNoMem]>;

/// PowerPC_Vec_FFF_Intrinsic - A PowerPC intrinsic that takes two v4f32
/// vectors and returns one.  These intrinsics have no side effects.
class PowerPC_Vec_FFF_Intrinsic<string GCCIntSuffix>
  : PowerPC_Vec_Intrinsic<GCCIntSuffix,
                          [llvm_v4f32_ty], [llvm_v4f32_ty, llvm_v4f32_ty],
                          [IntrNoMem]>;

/// PowerPC_Vec_BBB_Intrinsic - A PowerPC intrinsic that takes two v16i8
/// vectors and returns one.  These intrinsics have no side effects.
class PowerPC_Vec_BBB_Intrinsic<string GCCIntSuffix>
  : PowerPC_Vec_Intrinsic<GCCIntSuffix,
                          [llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty],
                          [IntrNoMem]>;

/// PowerPC_Vec_HHH_Intrinsic - A PowerPC intrinsic that takes two v8i16
/// vectors and returns one.  These intrinsics have no side effects.
class PowerPC_Vec_HHH_Intrinsic<string GCCIntSuffix>
  : PowerPC_Vec_Intrinsic<GCCIntSuffix,
                          [llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty],
                          [IntrNoMem]>;

/// PowerPC_Vec_WWW_Intrinsic - A PowerPC intrinsic that takes two v4i32
/// vectors and returns one.  These intrinsics have no side effects.
class PowerPC_Vec_WWW_Intrinsic<string GCCIntSuffix>
  : PowerPC_Vec_Intrinsic<GCCIntSuffix,
                          [llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
                          [IntrNoMem]>;

/// PowerPC_Vec_DDD_Intrinsic - A PowerPC intrinsic that takes two v2i64
/// vectors and returns one.  These intrinsics have no side effects.
class PowerPC_Vec_DDD_Intrinsic<string GCCIntSuffix>
  : PowerPC_Vec_Intrinsic<GCCIntSuffix,
                          [llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
                          [IntrNoMem]>;

/// PowerPC_Vec_QQQ_Intrinsic - A PowerPC intrinsic that takes two v1i128
/// vectors and returns one. These intrinsics have no side effects.
class PowerPC_Vec_QQQ_Intrinsic<string GCCIntSuffix>
  : PowerPC_Vec_Intrinsic<GCCIntSuffix,
                         [llvm_v1i128_ty], [llvm_v1i128_ty, llvm_v1i128_ty],
                         [IntrNoMem]>;

/// PowerPC_Vec_QDD_Intrinsic - A PowerPC intrinsic that takes two v2i64
/// vectors and returns one v1i128. These intrinsics have no side effects.
class PowerPC_Vec_QDD_Intrinsic<string GCCIntSuffix>
  : PowerPC_Vec_Intrinsic<GCCIntSuffix,
                          [llvm_v1i128_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
                          [IntrNoMem]>;

//===----------------------------------------------------------------------===//
// PowerPC VSX Intrinsic Class Definitions.
//

/// PowerPC_VSX_Vec_DDD_Intrinsic - A PowerPC intrinsic that takes two v2f64
/// vectors and returns one.  These intrinsics have no side effects.
class PowerPC_VSX_Vec_DDD_Intrinsic<string GCCIntSuffix>
  : PowerPC_VSX_Intrinsic<GCCIntSuffix,
                          [llvm_v2f64_ty], [llvm_v2f64_ty, llvm_v2f64_ty],
                          [IntrNoMem]>;

/// PowerPC_VSX_Vec_FFF_Intrinsic - A PowerPC intrinsic that takes two v4f32
/// vectors and returns one.  These intrinsics have no side effects.
class PowerPC_VSX_Vec_FFF_Intrinsic<string GCCIntSuffix>
  : PowerPC_VSX_Intrinsic<GCCIntSuffix,
                          [llvm_v4f32_ty], [llvm_v4f32_ty, llvm_v4f32_ty],
                          [IntrNoMem]>;

/// PowerPC_VSX_Sca_DDD_Intrinsic - A PowerPC intrinsic that takes two f64
/// scalars and returns one.  These intrinsics have no side effects.
class PowerPC_VSX_Sca_DDD_Intrinsic<string GCCIntSuffix>
  : PowerPC_VSX_Intrinsic<GCCIntSuffix,
                          [llvm_double_ty], [llvm_double_ty, llvm_double_ty],
                          [IntrNoMem]>;

//===----------------------------------------------------------------------===//
// PowerPC Altivec Intrinsic Definitions.

let TargetPrefix = "ppc" in {  // All intrinsics start with "llvm.ppc.".
  // Data Stream Control.
  def int_ppc_altivec_dss : ClangBuiltin<"__builtin_altivec_dss">,
              Intrinsic<[], [llvm_i32_ty], []>;
  def int_ppc_altivec_dssall : ClangBuiltin<"__builtin_altivec_dssall">,
              Intrinsic<[], [], []>;
  def int_ppc_altivec_dst : ClangBuiltin<"__builtin_altivec_dst">,
              Intrinsic<[],
                        [llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty],
                        []>;
  def int_ppc_altivec_dstt : ClangBuiltin<"__builtin_altivec_dstt">,
              Intrinsic<[],
                        [llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty],
                        []>;
  def int_ppc_altivec_dstst : ClangBuiltin<"__builtin_altivec_dstst">,
              Intrinsic<[],
                        [llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty],
                        []>;
  def int_ppc_altivec_dststt : ClangBuiltin<"__builtin_altivec_dststt">,
              Intrinsic<[],
                        [llvm_ptr_ty, llvm_i32_ty, llvm_i32_ty],
                        []>;

  // VSCR access.
  def int_ppc_altivec_mfvscr : ClangBuiltin<"__builtin_altivec_mfvscr">,
              Intrinsic<[llvm_v8i16_ty], [], [IntrNoMem, IntrHasSideEffects]>;
  def int_ppc_altivec_mtvscr : ClangBuiltin<"__builtin_altivec_mtvscr">,
              Intrinsic<[], [llvm_v4i32_ty], [IntrNoMem, IntrHasSideEffects]>;


  // Loads.  These don't map directly to GCC builtins because they represent the
  // source address with a single pointer.
  def int_ppc_altivec_lvx :
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_ptr_ty],
                            [IntrReadMem, IntrArgMemOnly]>;
  def int_ppc_altivec_lvxl :
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_ptr_ty],
                            [IntrReadMem, IntrArgMemOnly]>;
  def int_ppc_altivec_lvebx :
      DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_ptr_ty],
                            [IntrReadMem, IntrArgMemOnly]>;
  def int_ppc_altivec_lvehx :
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_ptr_ty],
                            [IntrReadMem, IntrArgMemOnly]>;
  def int_ppc_altivec_lvewx :
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_ptr_ty],
                            [IntrReadMem, IntrArgMemOnly]>;

  // Stores.  These don't map directly to GCC builtins because they represent the
  // source address with a single pointer.
  def int_ppc_altivec_stvx :
              DefaultAttrsIntrinsic<[], [llvm_v4i32_ty, llvm_ptr_ty],
                                    [IntrWriteMem, IntrArgMemOnly]>;
  def int_ppc_altivec_stvxl :
              DefaultAttrsIntrinsic<[], [llvm_v4i32_ty, llvm_ptr_ty],
                                    [IntrWriteMem, IntrArgMemOnly]>;
  def int_ppc_altivec_stvebx :
              DefaultAttrsIntrinsic<[], [llvm_v16i8_ty, llvm_ptr_ty],
                                    [IntrWriteMem, IntrArgMemOnly]>;
  def int_ppc_altivec_stvehx :
              DefaultAttrsIntrinsic<[], [llvm_v8i16_ty, llvm_ptr_ty],
                                    [IntrWriteMem, IntrArgMemOnly]>;
  def int_ppc_altivec_stvewx :
              DefaultAttrsIntrinsic<[], [llvm_v4i32_ty, llvm_ptr_ty],
                                    [IntrWriteMem, IntrArgMemOnly]>;

  // Comparisons setting a vector.
  def int_ppc_altivec_vcmpbfp : ClangBuiltin<"__builtin_altivec_vcmpbfp">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty, llvm_v4f32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpeqfp : ClangBuiltin<"__builtin_altivec_vcmpeqfp">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty, llvm_v4f32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpgefp : ClangBuiltin<"__builtin_altivec_vcmpgefp">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty, llvm_v4f32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpgtfp : ClangBuiltin<"__builtin_altivec_vcmpgtfp">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty, llvm_v4f32_ty],
                            [IntrNoMem]>;

  def int_ppc_altivec_vcmpequd : ClangBuiltin<"__builtin_altivec_vcmpequd">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpgtsd : ClangBuiltin<"__builtin_altivec_vcmpgtsd">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpgtud : ClangBuiltin<"__builtin_altivec_vcmpgtud">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
                            [IntrNoMem]>;

  def int_ppc_altivec_vcmpequw : ClangBuiltin<"__builtin_altivec_vcmpequw">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpgtsw : ClangBuiltin<"__builtin_altivec_vcmpgtsw">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpgtuw : ClangBuiltin<"__builtin_altivec_vcmpgtuw">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpnew : ClangBuiltin<"__builtin_altivec_vcmpnew">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpnezw : ClangBuiltin<"__builtin_altivec_vcmpnezw">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
                            [IntrNoMem]>;

  def int_ppc_altivec_vcmpequh : ClangBuiltin<"__builtin_altivec_vcmpequh">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpgtsh : ClangBuiltin<"__builtin_altivec_vcmpgtsh">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpgtuh : ClangBuiltin<"__builtin_altivec_vcmpgtuh">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpneh : ClangBuiltin<"__builtin_altivec_vcmpneh">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpnezh : ClangBuiltin<"__builtin_altivec_vcmpnezh">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty],
                            [IntrNoMem]>;

  def int_ppc_altivec_vcmpequb : ClangBuiltin<"__builtin_altivec_vcmpequb">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpgtsb : ClangBuiltin<"__builtin_altivec_vcmpgtsb">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpgtub : ClangBuiltin<"__builtin_altivec_vcmpgtub">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpneb : ClangBuiltin<"__builtin_altivec_vcmpneb">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpnezb : ClangBuiltin<"__builtin_altivec_vcmpnezb">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty],
                            [IntrNoMem]>;

  def int_ppc_altivec_vcmpequq : ClangBuiltin<"__builtin_altivec_vcmpequq">,
      DefaultAttrsIntrinsic<[llvm_v1i128_ty], [llvm_v1i128_ty, llvm_v1i128_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpgtsq : ClangBuiltin<"__builtin_altivec_vcmpgtsq">,
      DefaultAttrsIntrinsic<[llvm_v1i128_ty], [llvm_v1i128_ty, llvm_v1i128_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpgtuq : ClangBuiltin<"__builtin_altivec_vcmpgtuq">,
      DefaultAttrsIntrinsic<[llvm_v1i128_ty], [llvm_v1i128_ty, llvm_v1i128_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpequq_p : ClangBuiltin<"__builtin_altivec_vcmpequq_p">,
      DefaultAttrsIntrinsic<[llvm_i32_ty],
                            [llvm_i32_ty,llvm_v1i128_ty,llvm_v1i128_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpgtsq_p : ClangBuiltin<"__builtin_altivec_vcmpgtsq_p">,
      DefaultAttrsIntrinsic<[llvm_i32_ty],
                            [llvm_i32_ty,llvm_v1i128_ty,llvm_v1i128_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpgtuq_p : ClangBuiltin<"__builtin_altivec_vcmpgtuq_p">,
      DefaultAttrsIntrinsic<[llvm_i32_ty],
                            [llvm_i32_ty,llvm_v1i128_ty,llvm_v1i128_ty],
                            [IntrNoMem]>;

  // Predicate Comparisons.  The first operand specifies interpretation of CR6.
  def int_ppc_altivec_vcmpbfp_p : ClangBuiltin<"__builtin_altivec_vcmpbfp_p">,
      DefaultAttrsIntrinsic<[llvm_i32_ty],
                            [llvm_i32_ty,llvm_v4f32_ty,llvm_v4f32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpeqfp_p : ClangBuiltin<"__builtin_altivec_vcmpeqfp_p">,
      DefaultAttrsIntrinsic<[llvm_i32_ty],
                            [llvm_i32_ty,llvm_v4f32_ty,llvm_v4f32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpgefp_p : ClangBuiltin<"__builtin_altivec_vcmpgefp_p">,
      DefaultAttrsIntrinsic<[llvm_i32_ty],
                            [llvm_i32_ty,llvm_v4f32_ty,llvm_v4f32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpgtfp_p : ClangBuiltin<"__builtin_altivec_vcmpgtfp_p">,
      DefaultAttrsIntrinsic<[llvm_i32_ty],
                            [llvm_i32_ty,llvm_v4f32_ty,llvm_v4f32_ty],
                            [IntrNoMem]>;

  def int_ppc_altivec_vcmpequd_p : ClangBuiltin<"__builtin_altivec_vcmpequd_p">,
      DefaultAttrsIntrinsic<[llvm_i32_ty],
                            [llvm_i32_ty,llvm_v2i64_ty,llvm_v2i64_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpgtsd_p : ClangBuiltin<"__builtin_altivec_vcmpgtsd_p">,
      DefaultAttrsIntrinsic<[llvm_i32_ty],
                            [llvm_i32_ty,llvm_v2i64_ty,llvm_v2i64_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpgtud_p : ClangBuiltin<"__builtin_altivec_vcmpgtud_p">,
      DefaultAttrsIntrinsic<[llvm_i32_ty],
                            [llvm_i32_ty,llvm_v2i64_ty,llvm_v2i64_ty],
                            [IntrNoMem]>;

  def int_ppc_altivec_vcmpequw_p : ClangBuiltin<"__builtin_altivec_vcmpequw_p">,
      DefaultAttrsIntrinsic<[llvm_i32_ty],
                            [llvm_i32_ty,llvm_v4i32_ty,llvm_v4i32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpgtsw_p : ClangBuiltin<"__builtin_altivec_vcmpgtsw_p">,
      DefaultAttrsIntrinsic<[llvm_i32_ty],
                            [llvm_i32_ty,llvm_v4i32_ty,llvm_v4i32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpgtuw_p : ClangBuiltin<"__builtin_altivec_vcmpgtuw_p">,
      DefaultAttrsIntrinsic<[llvm_i32_ty],
                            [llvm_i32_ty,llvm_v4i32_ty,llvm_v4i32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpnew_p : ClangBuiltin<"__builtin_altivec_vcmpnew_p">,
      DefaultAttrsIntrinsic<[llvm_i32_ty],
                            [llvm_i32_ty,llvm_v4i32_ty,llvm_v4i32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpnezw_p : ClangBuiltin<"__builtin_altivec_vcmpnezw_p">,
      DefaultAttrsIntrinsic<[llvm_i32_ty],
                            [llvm_i32_ty,llvm_v4i32_ty,llvm_v4i32_ty],
                            [IntrNoMem]>;

  def int_ppc_altivec_vcmpequh_p : ClangBuiltin<"__builtin_altivec_vcmpequh_p">,
      DefaultAttrsIntrinsic<[llvm_i32_ty],
                            [llvm_i32_ty,llvm_v8i16_ty,llvm_v8i16_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpgtsh_p : ClangBuiltin<"__builtin_altivec_vcmpgtsh_p">,
      DefaultAttrsIntrinsic<[llvm_i32_ty],
                            [llvm_i32_ty,llvm_v8i16_ty,llvm_v8i16_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpgtuh_p : ClangBuiltin<"__builtin_altivec_vcmpgtuh_p">,
      DefaultAttrsIntrinsic<[llvm_i32_ty],
                            [llvm_i32_ty,llvm_v8i16_ty,llvm_v8i16_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpneh_p : ClangBuiltin<"__builtin_altivec_vcmpneh_p">,
      DefaultAttrsIntrinsic<[llvm_i32_ty],
                            [llvm_i32_ty,llvm_v8i16_ty,llvm_v8i16_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpnezh_p : ClangBuiltin<"__builtin_altivec_vcmpnezh_p">,
      DefaultAttrsIntrinsic<[llvm_i32_ty],
                            [llvm_i32_ty,llvm_v8i16_ty,llvm_v8i16_ty],
                            [IntrNoMem]>;

  def int_ppc_altivec_vcmpequb_p : ClangBuiltin<"__builtin_altivec_vcmpequb_p">,
      DefaultAttrsIntrinsic<[llvm_i32_ty],
                            [llvm_i32_ty,llvm_v16i8_ty,llvm_v16i8_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpgtsb_p : ClangBuiltin<"__builtin_altivec_vcmpgtsb_p">,
      DefaultAttrsIntrinsic<[llvm_i32_ty],
                            [llvm_i32_ty,llvm_v16i8_ty,llvm_v16i8_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpgtub_p : ClangBuiltin<"__builtin_altivec_vcmpgtub_p">,
      DefaultAttrsIntrinsic<[llvm_i32_ty],
                            [llvm_i32_ty,llvm_v16i8_ty,llvm_v16i8_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpneb_p : ClangBuiltin<"__builtin_altivec_vcmpneb_p">,
      DefaultAttrsIntrinsic<[llvm_i32_ty],
                            [llvm_i32_ty,llvm_v16i8_ty,llvm_v16i8_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vcmpnezb_p : ClangBuiltin<"__builtin_altivec_vcmpnezb_p">,
      DefaultAttrsIntrinsic<[llvm_i32_ty],
                            [llvm_i32_ty,llvm_v16i8_ty,llvm_v16i8_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vclzlsbb : ClangBuiltin<"__builtin_altivec_vclzlsbb">,
      DefaultAttrsIntrinsic<[llvm_i32_ty],[llvm_v16i8_ty],[IntrNoMem]>;
  def int_ppc_altivec_vctzlsbb : ClangBuiltin<"__builtin_altivec_vctzlsbb">,
      DefaultAttrsIntrinsic<[llvm_i32_ty],[llvm_v16i8_ty],[IntrNoMem]>;
  def int_ppc_altivec_vprtybw : ClangBuiltin<"__builtin_altivec_vprtybw">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty],[llvm_v4i32_ty],[IntrNoMem]>;
  def int_ppc_altivec_vprtybd : ClangBuiltin<"__builtin_altivec_vprtybd">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty],[llvm_v2i64_ty],[IntrNoMem]>;
  def int_ppc_altivec_vprtybq : ClangBuiltin<"__builtin_altivec_vprtybq">,
      DefaultAttrsIntrinsic<[llvm_v1i128_ty],[llvm_v1i128_ty],[IntrNoMem]>;

  // BCD intrinsics.
  def int_ppc_bcdadd : ClangBuiltin<"__builtin_ppc_bcdadd">,
    DefaultAttrsIntrinsic<
    [llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty, llvm_i32_ty],
    [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_ppc_bcdadd_p : ClangBuiltin<"__builtin_ppc_bcdadd_p">,
    DefaultAttrsIntrinsic<
    [llvm_i32_ty], [llvm_i32_ty, llvm_v16i8_ty, llvm_v16i8_ty],
    [IntrNoMem, ImmArg<ArgIndex<0>>]>;
  def int_ppc_bcdsub : ClangBuiltin<"__builtin_ppc_bcdsub">,
    DefaultAttrsIntrinsic<
    [llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty, llvm_i32_ty],
    [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_ppc_bcdsub_p : ClangBuiltin<"__builtin_ppc_bcdsub_p">,
    DefaultAttrsIntrinsic<
    [llvm_i32_ty], [llvm_i32_ty, llvm_v16i8_ty, llvm_v16i8_ty],
    [IntrNoMem, ImmArg<ArgIndex<0>>]>;

  // P10 Vector Extract with Mask
  def int_ppc_altivec_vextractbm : ClangBuiltin<"__builtin_altivec_vextractbm">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v16i8_ty], [IntrNoMem]>;
  def int_ppc_altivec_vextracthm : ClangBuiltin<"__builtin_altivec_vextracthm">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v8i16_ty], [IntrNoMem]>;
  def int_ppc_altivec_vextractwm : ClangBuiltin<"__builtin_altivec_vextractwm">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v4i32_ty], [IntrNoMem]>;
  def int_ppc_altivec_vextractdm : ClangBuiltin<"__builtin_altivec_vextractdm">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v2i64_ty], [IntrNoMem]>;
  def int_ppc_altivec_vextractqm : ClangBuiltin<"__builtin_altivec_vextractqm">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_v1i128_ty], [IntrNoMem]>;

  // P10 Vector Expand with Mask
  def int_ppc_altivec_vexpandbm : ClangBuiltin<"__builtin_altivec_vexpandbm">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty], [IntrNoMem]>;
  def int_ppc_altivec_vexpandhm : ClangBuiltin<"__builtin_altivec_vexpandhm">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty], [IntrNoMem]>;
  def int_ppc_altivec_vexpandwm : ClangBuiltin<"__builtin_altivec_vexpandwm">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty], [IntrNoMem]>;
  def int_ppc_altivec_vexpanddm : ClangBuiltin<"__builtin_altivec_vexpanddm">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty], [IntrNoMem]>;
  def int_ppc_altivec_vexpandqm : ClangBuiltin<"__builtin_altivec_vexpandqm">,
      DefaultAttrsIntrinsic<[llvm_v1i128_ty], [llvm_v1i128_ty], [IntrNoMem]>;

  // P10 Vector Count with Mask intrinsics.
  def int_ppc_altivec_vcntmbb : ClangBuiltin<"__builtin_altivec_vcntmbb">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_v16i8_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_ppc_altivec_vcntmbh : ClangBuiltin<"__builtin_altivec_vcntmbh">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_v8i16_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_ppc_altivec_vcntmbw : ClangBuiltin<"__builtin_altivec_vcntmbw">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_v4i32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_ppc_altivec_vcntmbd : ClangBuiltin<"__builtin_altivec_vcntmbd">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_v2i64_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;

  // P10 Move to VSR with Mask Intrinsics.
  def int_ppc_altivec_mtvsrbm : ClangBuiltin<"__builtin_altivec_mtvsrbm">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_i64_ty], [IntrNoMem]>;
  def int_ppc_altivec_mtvsrhm : ClangBuiltin<"__builtin_altivec_mtvsrhm">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_i64_ty], [IntrNoMem]>;
  def int_ppc_altivec_mtvsrwm : ClangBuiltin<"__builtin_altivec_mtvsrwm">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_i64_ty], [IntrNoMem]>;
  def int_ppc_altivec_mtvsrdm : ClangBuiltin<"__builtin_altivec_mtvsrdm">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_i64_ty], [IntrNoMem]>;
  def int_ppc_altivec_mtvsrqm : ClangBuiltin<"__builtin_altivec_mtvsrqm">,
      DefaultAttrsIntrinsic<[llvm_v1i128_ty], [llvm_i64_ty], [IntrNoMem]>;

  // P10 Vector Parallel Bits Deposit/Extract Doubleword Builtins.
  def int_ppc_altivec_vpdepd : ClangBuiltin<"__builtin_altivec_vpdepd">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vpextd : ClangBuiltin<"__builtin_altivec_vpextd">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
                            [IntrNoMem]>;

  // P10 Vector String Isolate Intrinsics.
  def int_ppc_altivec_vstribr : ClangBuiltin<"__builtin_altivec_vstribr">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty], [IntrNoMem]>;
  def int_ppc_altivec_vstribl : ClangBuiltin<"__builtin_altivec_vstribl">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty], [IntrNoMem]>;
  def int_ppc_altivec_vstrihr : ClangBuiltin<"__builtin_altivec_vstrihr">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty], [IntrNoMem]>;
  def int_ppc_altivec_vstrihl : ClangBuiltin<"__builtin_altivec_vstrihl">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty], [IntrNoMem]>;
  // Predicate Intrinsics: The first operand specifies interpretation of CR6.
  def int_ppc_altivec_vstribr_p : ClangBuiltin<"__builtin_altivec_vstribr_p">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_v16i8_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vstribl_p : ClangBuiltin<"__builtin_altivec_vstribl_p">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_v16i8_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vstrihr_p : ClangBuiltin<"__builtin_altivec_vstrihr_p">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_v8i16_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vstrihl_p : ClangBuiltin<"__builtin_altivec_vstrihl_p">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_v8i16_ty],
                            [IntrNoMem]>;

  // P10 Vector Centrifuge Builtin.
  def int_ppc_altivec_vcfuged : ClangBuiltin<"__builtin_altivec_vcfuged">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
                            [IntrNoMem]>;

  // P10 Vector Gather Every Nth Bit Builtin.
  def int_ppc_altivec_vgnb : ClangBuiltin<"__builtin_altivec_vgnb">,
      DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_v1i128_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;

   // P10 Vector Clear Bytes
   def int_ppc_altivec_vclrlb :  ClangBuiltin<"__builtin_altivec_vclrlb">,
       DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty],
                             [IntrNoMem]>;
   def int_ppc_altivec_vclrrb :  ClangBuiltin<"__builtin_altivec_vclrrb">,
       DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_i32_ty],
                             [IntrNoMem]>;

  // P10 Vector Shift Double Bit Immediate.
  def int_ppc_altivec_vsldbi : ClangBuiltin<"__builtin_altivec_vsldbi">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                            [llvm_v16i8_ty, llvm_v16i8_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_ppc_altivec_vsrdbi : ClangBuiltin<"__builtin_altivec_vsrdbi">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                            [llvm_v16i8_ty, llvm_v16i8_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>]>;

  // P10 Vector Insert.
  def int_ppc_altivec_vinsblx : ClangBuiltin<"__builtin_altivec_vinsblx">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                            [llvm_v16i8_ty, llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vinsbrx : ClangBuiltin<"__builtin_altivec_vinsbrx">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                            [llvm_v16i8_ty, llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vinshlx : ClangBuiltin<"__builtin_altivec_vinshlx">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty],
                            [llvm_v8i16_ty, llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vinshrx : ClangBuiltin<"__builtin_altivec_vinshrx">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty],
                            [llvm_v8i16_ty, llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vinswlx : ClangBuiltin<"__builtin_altivec_vinswlx">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                            [llvm_v4i32_ty, llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vinswrx : ClangBuiltin<"__builtin_altivec_vinswrx">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                            [llvm_v4i32_ty, llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vinsdlx : ClangBuiltin<"__builtin_altivec_vinsdlx">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty],
                            [llvm_v2i64_ty, llvm_i64_ty, llvm_i64_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vinsdrx : ClangBuiltin<"__builtin_altivec_vinsdrx">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty],
                            [llvm_v2i64_ty, llvm_i64_ty, llvm_i64_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vinsbvlx : ClangBuiltin<"__builtin_altivec_vinsbvlx">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                            [llvm_v16i8_ty, llvm_i32_ty, llvm_v16i8_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vinsbvrx : ClangBuiltin<"__builtin_altivec_vinsbvrx">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                            [llvm_v16i8_ty, llvm_i32_ty, llvm_v16i8_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vinshvlx : ClangBuiltin<"__builtin_altivec_vinshvlx">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty],
                            [llvm_v8i16_ty, llvm_i32_ty, llvm_v8i16_ty],
                            [IntrNoMem]>;
 def int_ppc_altivec_vinshvrx : ClangBuiltin<"__builtin_altivec_vinshvrx">,
     DefaultAttrsIntrinsic<[llvm_v8i16_ty],
                            [llvm_v8i16_ty, llvm_i32_ty, llvm_v8i16_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vinswvlx : ClangBuiltin<"__builtin_altivec_vinswvlx">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                            [llvm_v4i32_ty, llvm_i32_ty, llvm_v4i32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vinswvrx : ClangBuiltin<"__builtin_altivec_vinswvrx">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                            [llvm_v4i32_ty, llvm_i32_ty, llvm_v4i32_ty],
                            [IntrNoMem]>;
  // P10 Vector Insert with immediate.
  def int_ppc_altivec_vinsw :
      DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                            [llvm_v4i32_ty, llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  def int_ppc_altivec_vinsd :
      DefaultAttrsIntrinsic<[llvm_v2i64_ty],
                            [llvm_v2i64_ty, llvm_i64_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<2>>]>;
  // P10 Vector Extract.
  def int_ppc_altivec_vextdubvlx : ClangBuiltin<"__builtin_altivec_vextdubvlx">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty],
                            [llvm_v16i8_ty, llvm_v16i8_ty, llvm_i32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vextdubvrx : ClangBuiltin<"__builtin_altivec_vextdubvrx">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty],
                            [llvm_v16i8_ty, llvm_v16i8_ty, llvm_i32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vextduhvlx : ClangBuiltin<"__builtin_altivec_vextduhvlx">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty],
                            [llvm_v8i16_ty, llvm_v8i16_ty, llvm_i32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vextduhvrx : ClangBuiltin<"__builtin_altivec_vextduhvrx">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty],
                            [llvm_v8i16_ty, llvm_v8i16_ty, llvm_i32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vextduwvlx : ClangBuiltin<"__builtin_altivec_vextduwvlx">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty],
                            [llvm_v4i32_ty, llvm_v4i32_ty, llvm_i32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vextduwvrx : ClangBuiltin<"__builtin_altivec_vextduwvrx">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty],
                            [llvm_v4i32_ty, llvm_v4i32_ty, llvm_i32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vextddvlx : ClangBuiltin<"__builtin_altivec_vextddvlx">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty],
                            [llvm_v2i64_ty, llvm_v2i64_ty, llvm_i32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vextddvrx : ClangBuiltin<"__builtin_altivec_vextddvrx">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty],
                            [llvm_v2i64_ty, llvm_v2i64_ty, llvm_i32_ty],
                            [IntrNoMem]>;
}

// Vector average.
def int_ppc_altivec_vavgsb : PowerPC_Vec_BBB_Intrinsic<"vavgsb">;
def int_ppc_altivec_vavgsh : PowerPC_Vec_HHH_Intrinsic<"vavgsh">;
def int_ppc_altivec_vavgsw : PowerPC_Vec_WWW_Intrinsic<"vavgsw">;
def int_ppc_altivec_vavgub : PowerPC_Vec_BBB_Intrinsic<"vavgub">;
def int_ppc_altivec_vavguh : PowerPC_Vec_HHH_Intrinsic<"vavguh">;
def int_ppc_altivec_vavguw : PowerPC_Vec_WWW_Intrinsic<"vavguw">;

// Vector maximum.
def int_ppc_altivec_vmaxfp : PowerPC_Vec_FFF_Intrinsic<"vmaxfp">;
def int_ppc_altivec_vmaxsb : PowerPC_Vec_BBB_Intrinsic<"vmaxsb">;
def int_ppc_altivec_vmaxsh : PowerPC_Vec_HHH_Intrinsic<"vmaxsh">;
def int_ppc_altivec_vmaxsw : PowerPC_Vec_WWW_Intrinsic<"vmaxsw">;
def int_ppc_altivec_vmaxsd : PowerPC_Vec_DDD_Intrinsic<"vmaxsd">;
def int_ppc_altivec_vmaxub : PowerPC_Vec_BBB_Intrinsic<"vmaxub">;
def int_ppc_altivec_vmaxuh : PowerPC_Vec_HHH_Intrinsic<"vmaxuh">;
def int_ppc_altivec_vmaxuw : PowerPC_Vec_WWW_Intrinsic<"vmaxuw">;
def int_ppc_altivec_vmaxud : PowerPC_Vec_DDD_Intrinsic<"vmaxud">;

// Vector minimum.
def int_ppc_altivec_vminfp : PowerPC_Vec_FFF_Intrinsic<"vminfp">;
def int_ppc_altivec_vminsb : PowerPC_Vec_BBB_Intrinsic<"vminsb">;
def int_ppc_altivec_vminsh : PowerPC_Vec_HHH_Intrinsic<"vminsh">;
def int_ppc_altivec_vminsw : PowerPC_Vec_WWW_Intrinsic<"vminsw">;
def int_ppc_altivec_vminsd : PowerPC_Vec_DDD_Intrinsic<"vminsd">;
def int_ppc_altivec_vminub : PowerPC_Vec_BBB_Intrinsic<"vminub">;
def int_ppc_altivec_vminuh : PowerPC_Vec_HHH_Intrinsic<"vminuh">;
def int_ppc_altivec_vminuw : PowerPC_Vec_WWW_Intrinsic<"vminuw">;
def int_ppc_altivec_vminud : PowerPC_Vec_DDD_Intrinsic<"vminud">;

// Saturating adds.
def int_ppc_altivec_vaddubs : PowerPC_Vec_BBB_Intrinsic<"vaddubs">;
def int_ppc_altivec_vaddsbs : PowerPC_Vec_BBB_Intrinsic<"vaddsbs">;
def int_ppc_altivec_vadduhs : PowerPC_Vec_HHH_Intrinsic<"vadduhs">;
def int_ppc_altivec_vaddshs : PowerPC_Vec_HHH_Intrinsic<"vaddshs">;
def int_ppc_altivec_vadduws : PowerPC_Vec_WWW_Intrinsic<"vadduws">;
def int_ppc_altivec_vaddsws : PowerPC_Vec_WWW_Intrinsic<"vaddsws">;
def int_ppc_altivec_vaddcuw : PowerPC_Vec_WWW_Intrinsic<"vaddcuw">;
def int_ppc_altivec_vaddcuq : PowerPC_Vec_QQQ_Intrinsic<"vaddcuq">;

// Saturating subs.
def int_ppc_altivec_vsububs : PowerPC_Vec_BBB_Intrinsic<"vsububs">;
def int_ppc_altivec_vsubsbs : PowerPC_Vec_BBB_Intrinsic<"vsubsbs">;
def int_ppc_altivec_vsubuhs : PowerPC_Vec_HHH_Intrinsic<"vsubuhs">;
def int_ppc_altivec_vsubshs : PowerPC_Vec_HHH_Intrinsic<"vsubshs">;
def int_ppc_altivec_vsubuws : PowerPC_Vec_WWW_Intrinsic<"vsubuws">;
def int_ppc_altivec_vsubsws : PowerPC_Vec_WWW_Intrinsic<"vsubsws">;
def int_ppc_altivec_vsubcuw : PowerPC_Vec_WWW_Intrinsic<"vsubcuw">;
def int_ppc_altivec_vsubcuq : PowerPC_Vec_QQQ_Intrinsic<"vsubcuq">;

let TargetPrefix = "ppc" in {  // All PPC intrinsics start with "llvm.ppc.".
  // Saturating multiply-adds.
  def int_ppc_altivec_vmhaddshs : ClangBuiltin<"__builtin_altivec_vmhaddshs">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty,
                             llvm_v8i16_ty, llvm_v8i16_ty],
                             [IntrNoMem, IntrHasSideEffects]>;
  def int_ppc_altivec_vmhraddshs : ClangBuiltin<"__builtin_altivec_vmhraddshs">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty,
                             llvm_v8i16_ty, llvm_v8i16_ty],
                             [IntrNoMem, IntrHasSideEffects]>;

  def int_ppc_altivec_vmaddfp : ClangBuiltin<"__builtin_altivec_vmaddfp">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty,
                             llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
  def int_ppc_altivec_vnmsubfp : ClangBuiltin<"__builtin_altivec_vnmsubfp">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty,
                             llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;

  // Vector Multiply Sum Instructions.
  def int_ppc_altivec_vmsummbm : ClangBuiltin<"__builtin_altivec_vmsummbm">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v16i8_ty, llvm_v16i8_ty,
                             llvm_v4i32_ty], [IntrNoMem]>;
  def int_ppc_altivec_vmsumshm : ClangBuiltin<"__builtin_altivec_vmsumshm">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v8i16_ty, llvm_v8i16_ty,
                             llvm_v4i32_ty], [IntrNoMem]>;
  def int_ppc_altivec_vmsumshs : ClangBuiltin<"__builtin_altivec_vmsumshs">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v8i16_ty, llvm_v8i16_ty,
                             llvm_v4i32_ty], [IntrNoMem, IntrHasSideEffects]>;
  def int_ppc_altivec_vmsumubm : ClangBuiltin<"__builtin_altivec_vmsumubm">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v16i8_ty, llvm_v16i8_ty,
                             llvm_v4i32_ty], [IntrNoMem]>;
  def int_ppc_altivec_vmsumuhm : ClangBuiltin<"__builtin_altivec_vmsumuhm">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v8i16_ty, llvm_v8i16_ty,
                             llvm_v4i32_ty], [IntrNoMem]>;
  def int_ppc_altivec_vmsumudm : ClangBuiltin<"__builtin_altivec_vmsumudm">,
      DefaultAttrsIntrinsic<[llvm_v1i128_ty], [llvm_v2i64_ty, llvm_v2i64_ty,
                             llvm_v1i128_ty], [IntrNoMem]>;
  def int_ppc_altivec_vmsumuhs : ClangBuiltin<"__builtin_altivec_vmsumuhs">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v8i16_ty, llvm_v8i16_ty,
                             llvm_v4i32_ty], [IntrNoMem, IntrHasSideEffects]>;
  def int_ppc_altivec_vmsumcud : ClangBuiltin<"__builtin_altivec_vmsumcud">,
      DefaultAttrsIntrinsic<[llvm_v1i128_ty],
                            [llvm_v2i64_ty, llvm_v2i64_ty, llvm_v1i128_ty],
                            [IntrNoMem]>;

  // Vector Multiply Instructions.
  def int_ppc_altivec_vmulesb : ClangBuiltin<"__builtin_altivec_vmulesb">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v16i8_ty, llvm_v16i8_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vmulesh : ClangBuiltin<"__builtin_altivec_vmulesh">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v8i16_ty, llvm_v8i16_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vmulesw : ClangBuiltin<"__builtin_altivec_vmulesw">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vmulesd : PowerPC_Vec_QDD_Intrinsic<"vmulesd">;
  def int_ppc_altivec_vmuleub : ClangBuiltin<"__builtin_altivec_vmuleub">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v16i8_ty, llvm_v16i8_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vmuleuh : ClangBuiltin<"__builtin_altivec_vmuleuh">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v8i16_ty, llvm_v8i16_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vmuleuw : ClangBuiltin<"__builtin_altivec_vmuleuw">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vmuleud : PowerPC_Vec_QDD_Intrinsic<"vmuleud">;

  def int_ppc_altivec_vmulosb : ClangBuiltin<"__builtin_altivec_vmulosb">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v16i8_ty, llvm_v16i8_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vmulosh : ClangBuiltin<"__builtin_altivec_vmulosh">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v8i16_ty, llvm_v8i16_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vmulosw : ClangBuiltin<"__builtin_altivec_vmulosw">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vmulosd : PowerPC_Vec_QDD_Intrinsic<"vmulosd">;
  def int_ppc_altivec_vmuloub : ClangBuiltin<"__builtin_altivec_vmuloub">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v16i8_ty, llvm_v16i8_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vmulouh : ClangBuiltin<"__builtin_altivec_vmulouh">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v8i16_ty, llvm_v8i16_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vmulouw : ClangBuiltin<"__builtin_altivec_vmulouw">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vmuloud : PowerPC_Vec_QDD_Intrinsic<"vmuloud">;

  // Vector Sum Instructions.
  def int_ppc_altivec_vsumsws : ClangBuiltin<"__builtin_altivec_vsumsws">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
                            [IntrNoMem, IntrHasSideEffects]>;
  def int_ppc_altivec_vsum2sws : ClangBuiltin<"__builtin_altivec_vsum2sws">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
                            [IntrNoMem, IntrHasSideEffects]>;
  def int_ppc_altivec_vsum4sbs : ClangBuiltin<"__builtin_altivec_vsum4sbs">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v16i8_ty, llvm_v4i32_ty],
                            [IntrNoMem, IntrHasSideEffects]>;
  def int_ppc_altivec_vsum4shs : ClangBuiltin<"__builtin_altivec_vsum4shs">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v8i16_ty, llvm_v4i32_ty],
                            [IntrNoMem, IntrHasSideEffects]>;
  def int_ppc_altivec_vsum4ubs : ClangBuiltin<"__builtin_altivec_vsum4ubs">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v16i8_ty, llvm_v4i32_ty],
                            [IntrNoMem, IntrHasSideEffects]>;

  // Vector Sign Extension Instructions
  def int_ppc_altivec_vextsb2w : ClangBuiltin<"__builtin_altivec_vextsb2w">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v16i8_ty], [IntrNoMem]>;
  def int_ppc_altivec_vextsb2d : ClangBuiltin<"__builtin_altivec_vextsb2d">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v16i8_ty], [IntrNoMem]>;
  def int_ppc_altivec_vextsh2w : ClangBuiltin<"__builtin_altivec_vextsh2w">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v8i16_ty], [IntrNoMem]>;
  def int_ppc_altivec_vextsh2d : ClangBuiltin<"__builtin_altivec_vextsh2d">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v8i16_ty], [IntrNoMem]>;
  def int_ppc_altivec_vextsw2d : ClangBuiltin<"__builtin_altivec_vextsw2d">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v4i32_ty], [IntrNoMem]>;
  def int_ppc_altivec_vextsd2q : ClangBuiltin<"__builtin_altivec_vextsd2q">,
      DefaultAttrsIntrinsic<[llvm_v1i128_ty], [llvm_v2i64_ty], [IntrNoMem]>;

  // Other multiplies.
  def int_ppc_altivec_vmladduhm : ClangBuiltin<"__builtin_altivec_vmladduhm">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v8i16_ty, llvm_v8i16_ty,
                             llvm_v8i16_ty], [IntrNoMem]>;

  // Packs.
  def int_ppc_altivec_vpkpx : ClangBuiltin<"__builtin_altivec_vpkpx">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vpkshss : ClangBuiltin<"__builtin_altivec_vpkshss">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_v8i16_ty, llvm_v8i16_ty],
                            [IntrNoMem, IntrHasSideEffects]>;
  def int_ppc_altivec_vpkshus : ClangBuiltin<"__builtin_altivec_vpkshus">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_v8i16_ty, llvm_v8i16_ty],
                            [IntrNoMem, IntrHasSideEffects]>;
  def int_ppc_altivec_vpkswss : ClangBuiltin<"__builtin_altivec_vpkswss">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
                            [IntrNoMem, IntrHasSideEffects]>;
  def int_ppc_altivec_vpkswus : ClangBuiltin<"__builtin_altivec_vpkswus">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
                            [IntrNoMem, IntrHasSideEffects]>;
  def int_ppc_altivec_vpksdss : ClangBuiltin<"__builtin_altivec_vpksdss">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
                            [IntrNoMem, IntrHasSideEffects]>;
  def int_ppc_altivec_vpksdus : ClangBuiltin<"__builtin_altivec_vpksdus">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
                            [IntrNoMem, IntrHasSideEffects]>;
  // vpkuhum is lowered to a shuffle.
  def int_ppc_altivec_vpkuhus : ClangBuiltin<"__builtin_altivec_vpkuhus">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_v8i16_ty, llvm_v8i16_ty],
                            [IntrNoMem, IntrHasSideEffects]>;
  // vpkuwum is lowered to a shuffle.
  def int_ppc_altivec_vpkuwus : ClangBuiltin<"__builtin_altivec_vpkuwus">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v4i32_ty, llvm_v4i32_ty],
                            [IntrNoMem, IntrHasSideEffects]>;
  // vpkudum is lowered to a shuffle.
  def int_ppc_altivec_vpkudus : ClangBuiltin<"__builtin_altivec_vpkudus">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
                            [IntrNoMem, IntrHasSideEffects]>;

  // Unpacks.
  def int_ppc_altivec_vupkhpx : ClangBuiltin<"__builtin_altivec_vupkhpx">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v8i16_ty], [IntrNoMem]>;
  def int_ppc_altivec_vupkhsb : ClangBuiltin<"__builtin_altivec_vupkhsb">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v16i8_ty], [IntrNoMem]>;
  def int_ppc_altivec_vupkhsh : ClangBuiltin<"__builtin_altivec_vupkhsh">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v8i16_ty], [IntrNoMem]>;
  def int_ppc_altivec_vupkhsw : ClangBuiltin<"__builtin_altivec_vupkhsw">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v4i32_ty], [IntrNoMem]>;
  def int_ppc_altivec_vupklpx : ClangBuiltin<"__builtin_altivec_vupklpx">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v8i16_ty], [IntrNoMem]>;
  def int_ppc_altivec_vupklsb : ClangBuiltin<"__builtin_altivec_vupklsb">,
      DefaultAttrsIntrinsic<[llvm_v8i16_ty], [llvm_v16i8_ty], [IntrNoMem]>;
  def int_ppc_altivec_vupklsh : ClangBuiltin<"__builtin_altivec_vupklsh">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v8i16_ty], [IntrNoMem]>;
  def int_ppc_altivec_vupklsw : ClangBuiltin<"__builtin_altivec_vupklsw">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v4i32_ty], [IntrNoMem]>;


  // FP <-> integer conversion.
  def int_ppc_altivec_vcfsx : ClangBuiltin<"__builtin_altivec_vcfsx">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4i32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_ppc_altivec_vcfux : ClangBuiltin<"__builtin_altivec_vcfux">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4i32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_ppc_altivec_vctsxs : ClangBuiltin<"__builtin_altivec_vctsxs">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_ppc_altivec_vctuxs : ClangBuiltin<"__builtin_altivec_vctuxs">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4f32_ty, llvm_i32_ty],
                            [IntrNoMem, ImmArg<ArgIndex<1>>]>;

  def int_ppc_altivec_vrfim : ClangBuiltin<"__builtin_altivec_vrfim">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty], [IntrNoMem]>;
  def int_ppc_altivec_vrfin : ClangBuiltin<"__builtin_altivec_vrfin">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty], [IntrNoMem]>;
  def int_ppc_altivec_vrfip : ClangBuiltin<"__builtin_altivec_vrfip">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty], [IntrNoMem]>;
  def int_ppc_altivec_vrfiz : ClangBuiltin<"__builtin_altivec_vrfiz">,
      DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty], [IntrNoMem]>;

  // Add Extended Quadword
  def int_ppc_altivec_vaddeuqm : ClangBuiltin<"__builtin_altivec_vaddeuqm">,
      DefaultAttrsIntrinsic<[llvm_v1i128_ty],
                            [llvm_v1i128_ty, llvm_v1i128_ty, llvm_v1i128_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vaddecuq : ClangBuiltin<"__builtin_altivec_vaddecuq">,
      DefaultAttrsIntrinsic<[llvm_v1i128_ty],
                            [llvm_v1i128_ty, llvm_v1i128_ty, llvm_v1i128_ty],
                            [IntrNoMem]>;

  // Sub Extended Quadword
  def int_ppc_altivec_vsubeuqm : ClangBuiltin<"__builtin_altivec_vsubeuqm">,
      DefaultAttrsIntrinsic<[llvm_v1i128_ty],
                            [llvm_v1i128_ty, llvm_v1i128_ty, llvm_v1i128_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vsubecuq : ClangBuiltin<"__builtin_altivec_vsubecuq">,
      DefaultAttrsIntrinsic<[llvm_v1i128_ty],
                            [llvm_v1i128_ty, llvm_v1i128_ty, llvm_v1i128_ty],
                            [IntrNoMem]>;

  // P10 Vector Count Leading / Trailing Zeroes under bit Mask Builtins.
  def int_ppc_altivec_vclzdm : ClangBuiltin<"__builtin_altivec_vclzdm">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vctzdm : ClangBuiltin<"__builtin_altivec_vctzdm">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v2i64_ty],
                            [IntrNoMem]>;
}

def int_ppc_altivec_vsl   : PowerPC_Vec_WWW_Intrinsic<"vsl">;
def int_ppc_altivec_vslo  : PowerPC_Vec_WWW_Intrinsic<"vslo">;

def int_ppc_altivec_vslb  : PowerPC_Vec_BBB_Intrinsic<"vslb">;
def int_ppc_altivec_vslv  : PowerPC_Vec_BBB_Intrinsic<"vslv">;
def int_ppc_altivec_vsrv  : PowerPC_Vec_BBB_Intrinsic<"vsrv">;
def int_ppc_altivec_vslh  : PowerPC_Vec_HHH_Intrinsic<"vslh">;
def int_ppc_altivec_vslw  : PowerPC_Vec_WWW_Intrinsic<"vslw">;

// Right Shifts.
def int_ppc_altivec_vsr   : PowerPC_Vec_WWW_Intrinsic<"vsr">;
def int_ppc_altivec_vsro  : PowerPC_Vec_WWW_Intrinsic<"vsro">;

def int_ppc_altivec_vsrb  : PowerPC_Vec_BBB_Intrinsic<"vsrb">;
def int_ppc_altivec_vsrh  : PowerPC_Vec_HHH_Intrinsic<"vsrh">;
def int_ppc_altivec_vsrw  : PowerPC_Vec_WWW_Intrinsic<"vsrw">;
def int_ppc_altivec_vsrab : PowerPC_Vec_BBB_Intrinsic<"vsrab">;
def int_ppc_altivec_vsrah : PowerPC_Vec_HHH_Intrinsic<"vsrah">;
def int_ppc_altivec_vsraw : PowerPC_Vec_WWW_Intrinsic<"vsraw">;

// Rotates.
def int_ppc_altivec_vrlb  : PowerPC_Vec_BBB_Intrinsic<"vrlb">;
def int_ppc_altivec_vrlh  : PowerPC_Vec_HHH_Intrinsic<"vrlh">;
def int_ppc_altivec_vrlw  : PowerPC_Vec_WWW_Intrinsic<"vrlw">;
def int_ppc_altivec_vrld  : PowerPC_Vec_DDD_Intrinsic<"vrld">;

let TargetPrefix = "ppc" in {  // All PPC intrinsics start with "llvm.ppc.".
  // Miscellaneous.
  def int_ppc_altivec_lvsl :
      DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_ptr_ty], [IntrNoMem]>;
  def int_ppc_altivec_lvsr :
      DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_ptr_ty], [IntrNoMem]>;

  def int_ppc_altivec_vperm : ClangBuiltin<"__builtin_altivec_vperm_4si">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty,
                             llvm_v4i32_ty, llvm_v16i8_ty], [IntrNoMem]>;
  def int_ppc_altivec_vsel : ClangBuiltin<"__builtin_altivec_vsel_4si">,
      DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty,
                             llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
  def int_ppc_altivec_vgbbd : ClangBuiltin<"__builtin_altivec_vgbbd">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty], [IntrNoMem]>;
  def int_ppc_altivec_vbpermq : ClangBuiltin<"__builtin_altivec_vbpermq">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v16i8_ty, llvm_v16i8_ty],
                            [IntrNoMem]>;
  def int_ppc_altivec_vbpermd : ClangBuiltin<"__builtin_altivec_vbpermd">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_v16i8_ty],
                            [IntrNoMem]>;
}

def int_ppc_altivec_vexptefp  : PowerPC_Vec_FF_Intrinsic<"vexptefp">;
def int_ppc_altivec_vlogefp   : PowerPC_Vec_FF_Intrinsic<"vlogefp">;
def int_ppc_altivec_vrefp     : PowerPC_Vec_FF_Intrinsic<"vrefp">;
def int_ppc_altivec_vrsqrtefp : PowerPC_Vec_FF_Intrinsic<"vrsqrtefp">;

// Power8 Intrinsics
// Crypto
let TargetPrefix = "ppc" in {  // All PPC intrinsics start with "llvm.ppc.".
  def int_ppc_altivec_crypto_vsbox :
      ClangBuiltin<"__builtin_altivec_crypto_vsbox">,
      DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty], [IntrNoMem]>;
  def int_ppc_altivec_crypto_vpermxor :
      ClangBuiltin<"__builtin_altivec_crypto_vpermxor">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty,
                             llvm_v16i8_ty], [IntrNoMem]>;
  def int_ppc_altivec_crypto_vpermxor_be :
      ClangBuiltin<"__builtin_altivec_crypto_vpermxor_be">,
      DefaultAttrsIntrinsic<[llvm_v16i8_ty], [llvm_v16i8_ty, llvm_v16i8_ty,
                             llvm_v16i8_ty], [IntrNoMem]>;

def int_ppc_altivec_crypto_vshasigmad :
    ClangBuiltin<"__builtin_altivec_crypto_vshasigmad">,
    DefaultAttrsIntrinsic<[llvm_v2i64_ty], [llvm_v2i64_ty, llvm_i32_ty,
                           llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>,
                           ImmArg<ArgIndex<2>>]>;
def int_ppc_altivec_crypto_vshasigmaw :
    ClangBuiltin<"__builtin_altivec_crypto_vshasigmaw">,
    DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_v4i32_ty, llvm_i32_ty,
                           llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>,
                           ImmArg<ArgIndex<2>>]>;
}
def int_ppc_altivec_crypto_vcipher :
            PowerPC_Vec_DDD_Intrinsic<"crypto_vcipher">;
def int_ppc_altivec_crypto_vcipherlast :
            PowerPC_Vec_DDD_Intrinsic<"crypto_vcipherlast">;
def int_ppc_altivec_crypto_vncipher :
            PowerPC_Vec_DDD_Intrinsic<"crypto_vncipher">;
def int_ppc_altivec_crypto_vncipherlast :
            PowerPC_Vec_DDD_Intrinsic<"crypto_vncipherlast">;
def int_ppc_altivec_crypto_vpmsumb :
            PowerPC_Vec_BBB_Intrinsic<"crypto_vpmsumb">;
def int_ppc_altivec_crypto_vpmsumh :
            PowerPC_Vec_HHH_Intrinsic<"crypto_vpmsumh">;
def int_ppc_altivec_crypto_vpmsumw :
            PowerPC_Vec_WWW_Intrinsic<"crypto_vpmsumw">;
def int_ppc_altivec_crypto_vpmsumd :
            PowerPC_Vec_DDD_Intrinsic<"crypto_vpmsumd">;

// Absolute Difference intrinsics
def int_ppc_altivec_vabsdub : PowerPC_Vec_BBB_Intrinsic<"vabsdub">;
def int_ppc_altivec_vabsduh : PowerPC_Vec_HHH_Intrinsic<"vabsduh">;
def int_ppc_altivec_vabsduw : PowerPC_Vec_WWW_Intrinsic<"vabsduw">;

// Vector rotates
def int_ppc_altivec_vrlwnm :
      PowerPC_Vec_Intrinsic<"vrlwnm", [llvm_v4i32_ty],
                            [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_ppc_altivec_vrlwmi :
      PowerPC_Vec_Intrinsic<"vrlwmi", [llvm_v4i32_ty],
                            [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
                            [IntrNoMem]>;
def int_ppc_altivec_vrldnm :
      PowerPC_Vec_Intrinsic<"vrldnm", [llvm_v2i64_ty],
                            [llvm_v2i64_ty, llvm_v2i64_ty], [IntrNoMem]>;
def int_ppc_altivec_vrldmi :
      PowerPC_Vec_Intrinsic<"vrldmi", [llvm_v2i64_ty],
                            [llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty],
                            [IntrNoMem]>;

def int_ppc_altivec_vrlqnm :
      PowerPC_Vec_Intrinsic<"vrlqnm", [llvm_v1i128_ty],
                           [llvm_v1i128_ty, llvm_v1i128_ty],
                            [IntrNoMem]>;
def int_ppc_altivec_vrlqmi :
      PowerPC_Vec_Intrinsic<"vrlqmi", [llvm_v1i128_ty],
                            [llvm_v1i128_ty, llvm_v1i128_ty, llvm_v1i128_ty],
                            [IntrNoMem]>;

// Vector Divide Extended Intrinsics.
def int_ppc_altivec_vdivesw : PowerPC_Vec_WWW_Intrinsic<"vdivesw">;
def int_ppc_altivec_vdiveuw : PowerPC_Vec_WWW_Intrinsic<"vdiveuw">;
def int_ppc_altivec_vdivesd : PowerPC_Vec_DDD_Intrinsic<"vdivesd">;
def int_ppc_altivec_vdiveud : PowerPC_Vec_DDD_Intrinsic<"vdiveud">;
def int_ppc_altivec_vdivesq : PowerPC_Vec_QQQ_Intrinsic<"vdivesq">;
def int_ppc_altivec_vdiveuq : PowerPC_Vec_QQQ_Intrinsic<"vdiveuq">;

// Vector Multiply High Intrinsics.
def int_ppc_altivec_vmulhsw : PowerPC_Vec_WWW_Intrinsic<"vmulhsw">;
def int_ppc_altivec_vmulhuw : PowerPC_Vec_WWW_Intrinsic<"vmulhuw">;
def int_ppc_altivec_vmulhsd : PowerPC_Vec_DDD_Intrinsic<"vmulhsd">;
def int_ppc_altivec_vmulhud : PowerPC_Vec_DDD_Intrinsic<"vmulhud">;

//===----------------------------------------------------------------------===//
// PowerPC VSX Intrinsic Definitions.

let TargetPrefix = "ppc" in {  // All intrinsics start with "llvm.ppc.".

// Vector load.
def int_ppc_vsx_lxvw4x :
    DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_ptr_ty],
                          [IntrReadMem, IntrArgMemOnly]>;
def int_ppc_vsx_lxvd2x :
    DefaultAttrsIntrinsic<[llvm_v2f64_ty], [llvm_ptr_ty],
                          [IntrReadMem, IntrArgMemOnly]>;
def int_ppc_vsx_lxvw4x_be :
    DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_ptr_ty],
                          [IntrReadMem, IntrArgMemOnly]>;
def int_ppc_vsx_lxvd2x_be :
    DefaultAttrsIntrinsic<[llvm_v2f64_ty], [llvm_ptr_ty],
                          [IntrReadMem, IntrArgMemOnly]>;
def int_ppc_vsx_lxvl :
    DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_ptr_ty, llvm_i64_ty],
                          [IntrReadMem, IntrArgMemOnly]>;
def int_ppc_vsx_lxvll :
    DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_ptr_ty, llvm_i64_ty],
                          [IntrReadMem, IntrArgMemOnly]>;
def int_ppc_vsx_lxvp :
    DefaultAttrsIntrinsic<[llvm_v256i1_ty], [llvm_ptr_ty],
                          [IntrReadMem, IntrArgMemOnly]>;

// Vector store.
def int_ppc_vsx_stxvw4x : Intrinsic<[], [llvm_v4i32_ty, llvm_ptr_ty],
                                    [IntrWriteMem, IntrArgMemOnly]>;
def int_ppc_vsx_stxvd2x : Intrinsic<[], [llvm_v2f64_ty, llvm_ptr_ty],
                                    [IntrWriteMem, IntrArgMemOnly]>;
def int_ppc_vsx_stxvw4x_be : Intrinsic<[], [llvm_v4i32_ty, llvm_ptr_ty],
                                       [IntrWriteMem, IntrArgMemOnly]>;
def int_ppc_vsx_stxvd2x_be : Intrinsic<[], [llvm_v2f64_ty, llvm_ptr_ty],
                                       [IntrWriteMem, IntrArgMemOnly]>;
def int_ppc_vsx_stxvl :
      Intrinsic<[], [llvm_v4i32_ty, llvm_ptr_ty, llvm_i64_ty],
      [IntrWriteMem, IntrArgMemOnly]>;
def int_ppc_vsx_stxvll :
      Intrinsic<[], [llvm_v4i32_ty, llvm_ptr_ty, llvm_i64_ty],
      [IntrWriteMem, IntrArgMemOnly]>;
def int_ppc_vsx_stxvp :
      Intrinsic<[], [llvm_v256i1_ty, llvm_ptr_ty], [IntrWriteMem,
      IntrArgMemOnly]>;
// Vector and scalar maximum.
def int_ppc_vsx_xvmaxdp : PowerPC_VSX_Vec_DDD_Intrinsic<"xvmaxdp">;
def int_ppc_vsx_xvmaxsp : PowerPC_VSX_Vec_FFF_Intrinsic<"xvmaxsp">;
def int_ppc_vsx_xsmaxdp : PowerPC_VSX_Sca_DDD_Intrinsic<"xsmaxdp">;

// Vector and scalar minimum.
def int_ppc_vsx_xvmindp : PowerPC_VSX_Vec_DDD_Intrinsic<"xvmindp">;
def int_ppc_vsx_xvminsp : PowerPC_VSX_Vec_FFF_Intrinsic<"xvminsp">;
def int_ppc_vsx_xsmindp : PowerPC_VSX_Sca_DDD_Intrinsic<"xsmindp">;

// Vector divide.
def int_ppc_vsx_xvdivdp : PowerPC_VSX_Vec_DDD_Intrinsic<"xvdivdp">;
def int_ppc_vsx_xvdivsp : PowerPC_VSX_Vec_FFF_Intrinsic<"xvdivsp">;

// Vector round-to-infinity (ceil)
def int_ppc_vsx_xvrspip :
    DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty], [IntrNoMem]>;
def int_ppc_vsx_xvrdpip :
    DefaultAttrsIntrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty], [IntrNoMem]>;

// Vector reciprocal estimate
def int_ppc_vsx_xvresp : ClangBuiltin<"__builtin_vsx_xvresp">,
    DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty], [IntrNoMem]>;
def int_ppc_vsx_xvredp : ClangBuiltin<"__builtin_vsx_xvredp">,
    DefaultAttrsIntrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty], [IntrNoMem]>;

// Vector rsqrte
def int_ppc_vsx_xvrsqrtesp : ClangBuiltin<"__builtin_vsx_xvrsqrtesp">,
    DefaultAttrsIntrinsic<[llvm_v4f32_ty], [llvm_v4f32_ty], [IntrNoMem]>;
def int_ppc_vsx_xvrsqrtedp : ClangBuiltin<"__builtin_vsx_xvrsqrtedp">,
    DefaultAttrsIntrinsic<[llvm_v2f64_ty], [llvm_v2f64_ty], [IntrNoMem]>;

// Vector compare
def int_ppc_vsx_xvcmpeqdp :
      PowerPC_VSX_Intrinsic<"xvcmpeqdp", [llvm_v2i64_ty],
                            [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;
def int_ppc_vsx_xvcmpeqdp_p : ClangBuiltin<"__builtin_vsx_xvcmpeqdp_p">,
    DefaultAttrsIntrinsic<[llvm_i32_ty],
                          [llvm_i32_ty,llvm_v2f64_ty,llvm_v2f64_ty],
                          [IntrNoMem]>;
def int_ppc_vsx_xvcmpeqsp :
      PowerPC_VSX_Intrinsic<"xvcmpeqsp", [llvm_v4i32_ty],
                            [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_ppc_vsx_xvcmpeqsp_p : ClangBuiltin<"__builtin_vsx_xvcmpeqsp_p">,
    DefaultAttrsIntrinsic<[llvm_i32_ty],
                          [llvm_i32_ty,llvm_v4f32_ty,llvm_v4f32_ty],
                          [IntrNoMem]>;
def int_ppc_vsx_xvcmpgedp :
      PowerPC_VSX_Intrinsic<"xvcmpgedp", [llvm_v2i64_ty],
                            [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;
def int_ppc_vsx_xvcmpgedp_p : ClangBuiltin<"__builtin_vsx_xvcmpgedp_p">,
    DefaultAttrsIntrinsic<[llvm_i32_ty],
                          [llvm_i32_ty,llvm_v2f64_ty,llvm_v2f64_ty],
                          [IntrNoMem]>;
def int_ppc_vsx_xvcmpgesp :
      PowerPC_VSX_Intrinsic<"xvcmpgesp", [llvm_v4i32_ty],
                            [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_ppc_vsx_xvcmpgesp_p : ClangBuiltin<"__builtin_vsx_xvcmpgesp_p">,
    DefaultAttrsIntrinsic<[llvm_i32_ty],
                          [llvm_i32_ty,llvm_v4f32_ty,llvm_v4f32_ty],
                          [IntrNoMem]>;
def int_ppc_vsx_xvcmpgtdp :
      PowerPC_VSX_Intrinsic<"xvcmpgtdp", [llvm_v2i64_ty],
                            [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;
def int_ppc_vsx_xvcmpgtdp_p : ClangBuiltin<"__builtin_vsx_xvcmpgtdp_p">,
    DefaultAttrsIntrinsic<[llvm_i32_ty],
                          [llvm_i32_ty,llvm_v2f64_ty,llvm_v2f64_ty],
                          [IntrNoMem]>;
def int_ppc_vsx_xvcmpgtsp :
      PowerPC_VSX_Intrinsic<"xvcmpgtsp", [llvm_v4i32_ty],
                            [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_ppc_vsx_xvcmpgtsp_p : ClangBuiltin<"__builtin_vsx_xvcmpgtsp_p">,
    DefaultAttrsIntrinsic<[llvm_i32_ty],
                          [llvm_i32_ty,llvm_v4f32_ty,llvm_v4f32_ty],
                          [IntrNoMem]>;
def int_ppc_vsx_xxleqv :
      PowerPC_VSX_Intrinsic<"xxleqv", [llvm_v4i32_ty],
                            [llvm_v4i32_ty, llvm_v4i32_ty], [IntrNoMem]>;
def int_ppc_vsx_xviexpdp :
      PowerPC_VSX_Intrinsic<"xviexpdp",[llvm_v2f64_ty],
                            [llvm_v2i64_ty, llvm_v2i64_ty],[IntrNoMem]>;
def int_ppc_vsx_xviexpsp :
      PowerPC_VSX_Intrinsic<"xviexpsp",[llvm_v4f32_ty],
                            [llvm_v4i32_ty, llvm_v4i32_ty],[IntrNoMem]>;
def int_ppc_vsx_xvcvdpsxws :
      PowerPC_VSX_Intrinsic<"xvcvdpsxws", [llvm_v4i32_ty],
                            [llvm_v2f64_ty], [IntrNoMem]>;
def int_ppc_vsx_xvcvdpuxws :
      PowerPC_VSX_Intrinsic<"xvcvdpuxws", [llvm_v4i32_ty],
                            [llvm_v2f64_ty], [IntrNoMem]>;
def int_ppc_vsx_xvcvspsxds :
      PowerPC_VSX_Intrinsic<"xvcvspsxds", [llvm_v2i64_ty],
                            [llvm_v4f32_ty], [IntrNoMem]>;
def int_ppc_vsx_xvcvspuxds :
      PowerPC_VSX_Intrinsic<"xvcvspuxds", [llvm_v2i64_ty],
                            [llvm_v4f32_ty], [IntrNoMem]>;
def int_ppc_vsx_xvcvsxwdp :
      PowerPC_VSX_Intrinsic<"xvcvsxwdp", [llvm_v2f64_ty],
                            [llvm_v4i32_ty], [IntrNoMem]>;
def int_ppc_vsx_xvcvuxwdp :
      PowerPC_VSX_Intrinsic<"xvcvuxwdp", [llvm_v2f64_ty],
                            [llvm_v4i32_ty], [IntrNoMem]>;
def int_ppc_vsx_xvcvspdp :
      PowerPC_VSX_Intrinsic<"xvcvspdp", [llvm_v2f64_ty],
                            [llvm_v4f32_ty], [IntrNoMem]>;
def int_ppc_vsx_xvcvsxdsp :
      PowerPC_VSX_Intrinsic<"xvcvsxdsp", [llvm_v4f32_ty],
                            [llvm_v2i64_ty], [IntrNoMem]>;
def int_ppc_vsx_xvcvuxdsp :
      PowerPC_VSX_Intrinsic<"xvcvuxdsp", [llvm_v4f32_ty],
                            [llvm_v2i64_ty], [IntrNoMem]>;
def int_ppc_vsx_xvcvdpsp :
      PowerPC_VSX_Intrinsic<"xvcvdpsp", [llvm_v4f32_ty],
                            [llvm_v2f64_ty], [IntrNoMem]>;
def int_ppc_vsx_xvcvsphp :
      PowerPC_VSX_Intrinsic<"xvcvsphp", [llvm_v4f32_ty],
                            [llvm_v4f32_ty], [IntrNoMem]>;
def int_ppc_vsx_xvxexpdp :
      PowerPC_VSX_Intrinsic<"xvxexpdp", [llvm_v2i64_ty],
                            [llvm_v2f64_ty], [IntrNoMem]>;
def int_ppc_vsx_xvxexpsp :
      PowerPC_VSX_Intrinsic<"xvxexpsp", [llvm_v4i32_ty],
                            [llvm_v4f32_ty], [IntrNoMem]>;
def int_ppc_vsx_xvxsigdp :
      PowerPC_VSX_Intrinsic<"xvxsigdp", [llvm_v2i64_ty],
                            [llvm_v2f64_ty], [IntrNoMem]>;
def int_ppc_vsx_xvxsigsp :
      PowerPC_VSX_Intrinsic<"xvxsigsp", [llvm_v4i32_ty],
                            [llvm_v4f32_ty], [IntrNoMem]>;
def int_ppc_vsx_xvtstdcdp :
      PowerPC_VSX_Intrinsic<"xvtstdcdp", [llvm_v2i64_ty],
                            [llvm_v2f64_ty, llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_ppc_vsx_xvtstdcsp :
      PowerPC_VSX_Intrinsic<"xvtstdcsp", [llvm_v4i32_ty],
                            [llvm_v4f32_ty,llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<1>>]>;
def int_ppc_vsx_xvcvhpsp :
      PowerPC_VSX_Intrinsic<"xvcvhpsp", [llvm_v4f32_ty],
                            [llvm_v8i16_ty],[IntrNoMem]>;
def int_ppc_vsx_xvcvspbf16 :
      PowerPC_VSX_Intrinsic<"xvcvspbf16", [llvm_v16i8_ty],
                            [llvm_v16i8_ty], [IntrNoMem]>;
def int_ppc_vsx_xvcvbf16spn :
      PowerPC_VSX_Intrinsic<"xvcvbf16spn", [llvm_v16i8_ty],
                            [llvm_v16i8_ty], [IntrNoMem]>;
def int_ppc_vsx_xxextractuw :
      PowerPC_VSX_Intrinsic<"xxextractuw",[llvm_v2i64_ty],
                            [llvm_v2i64_ty,llvm_i32_ty], [IntrNoMem]>;
def int_ppc_vsx_xxinsertw :
      PowerPC_VSX_Intrinsic<"xxinsertw",[llvm_v4i32_ty],
                            [llvm_v4i32_ty,llvm_v2i64_ty,llvm_i32_ty],
                            [IntrNoMem]>;
def int_ppc_vsx_xvtlsbb :
      PowerPC_VSX_Intrinsic<"xvtlsbb", [llvm_i32_ty],
                            [llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem]>;
def int_ppc_vsx_xvtdivdp :
      PowerPC_VSX_Intrinsic<"xvtdivdp", [llvm_i32_ty],
                            [llvm_v2f64_ty, llvm_v2f64_ty], [IntrNoMem]>;
def int_ppc_vsx_xvtdivsp :
      PowerPC_VSX_Intrinsic<"xvtdivsp", [llvm_i32_ty],
                            [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem]>;
def int_ppc_vsx_xvtsqrtdp :
      PowerPC_VSX_Intrinsic<"xvtsqrtdp", [llvm_i32_ty], [llvm_v2f64_ty], [IntrNoMem]>;
def int_ppc_vsx_xvtsqrtsp :
      PowerPC_VSX_Intrinsic<"xvtsqrtsp", [llvm_i32_ty], [llvm_v4f32_ty], [IntrNoMem]>;
def int_ppc_vsx_xxeval :
      PowerPC_VSX_Intrinsic<"xxeval", [llvm_v2i64_ty],
                           [llvm_v2i64_ty, llvm_v2i64_ty,
                            llvm_v2i64_ty, llvm_i32_ty],
                           [IntrNoMem, ImmArg<ArgIndex<3>>]>;
def int_ppc_vsx_xxgenpcvbm :
      PowerPC_VSX_Intrinsic<"xxgenpcvbm", [llvm_v16i8_ty],
                            [llvm_v16i8_ty, llvm_i32_ty], [IntrNoMem]>;
def int_ppc_vsx_xxgenpcvhm :
      PowerPC_VSX_Intrinsic<"xxgenpcvhm", [llvm_v8i16_ty],
                            [llvm_v8i16_ty, llvm_i32_ty], [IntrNoMem]>;
def int_ppc_vsx_xxgenpcvwm :
      PowerPC_VSX_Intrinsic<"xxgenpcvwm", [llvm_v4i32_ty],
                            [llvm_v4i32_ty, llvm_i32_ty], [IntrNoMem]>;
def int_ppc_vsx_xxgenpcvdm :
      PowerPC_VSX_Intrinsic<"xxgenpcvdm", [llvm_v2i64_ty],
                            [llvm_v2i64_ty, llvm_i32_ty], [IntrNoMem]>;

// P10 VSX Vector permute extended.
def int_ppc_vsx_xxpermx : 
    ClangBuiltin<"__builtin_vsx_xxpermx">,
    DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                          [llvm_v16i8_ty,llvm_v16i8_ty,llvm_v16i8_ty,
                           llvm_i32_ty],
                          [IntrNoMem, ImmArg<ArgIndex<3>>]>;
// P10 VSX Vector Blend Variable.
def int_ppc_vsx_xxblendvb: ClangBuiltin<"__builtin_vsx_xxblendvb">,
    DefaultAttrsIntrinsic<[llvm_v16i8_ty],
                          [llvm_v16i8_ty, llvm_v16i8_ty, llvm_v16i8_ty],
                          [IntrNoMem]>;
def int_ppc_vsx_xxblendvh: ClangBuiltin<"__builtin_vsx_xxblendvh">,
    DefaultAttrsIntrinsic<[llvm_v8i16_ty],
                          [llvm_v8i16_ty, llvm_v8i16_ty,llvm_v8i16_ty],
                          [IntrNoMem]>;
def int_ppc_vsx_xxblendvw: ClangBuiltin<"__builtin_vsx_xxblendvw">,
    DefaultAttrsIntrinsic<[llvm_v4i32_ty],
                          [llvm_v4i32_ty, llvm_v4i32_ty, llvm_v4i32_ty],
                          [IntrNoMem]>;
def int_ppc_vsx_xxblendvd: ClangBuiltin<"__builtin_vsx_xxblendvd">,
    DefaultAttrsIntrinsic<[llvm_v2i64_ty],
                          [llvm_v2i64_ty, llvm_v2i64_ty, llvm_v2i64_ty],
                          [IntrNoMem]>;
}

//===----------------------------------------------------------------------===//
// PowerPC HTM Intrinsic Definitions.

let TargetPrefix = "ppc" in {  // All intrinsics start with "llvm.ppc.".

def int_ppc_tbegin : ClangBuiltin<"__builtin_tbegin">,
      Intrinsic<[llvm_i32_ty], [llvm_i32_ty], [ImmArg<ArgIndex<0>>]>;
def int_ppc_tend : ClangBuiltin<"__builtin_tend">,
      Intrinsic<[llvm_i32_ty], [llvm_i32_ty], [ImmArg<ArgIndex<0>>]>;

def int_ppc_tabort : ClangBuiltin<"__builtin_tabort">,
      Intrinsic<[llvm_i32_ty], [llvm_i32_ty], []>;
def int_ppc_tabortwc : ClangBuiltin<"__builtin_tabortwc">,
      Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], []>;
def int_ppc_tabortwci : ClangBuiltin<"__builtin_tabortwci">,
      Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], []>;
def int_ppc_tabortdc : ClangBuiltin<"__builtin_tabortdc">,
      Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], []>;
def int_ppc_tabortdci : ClangBuiltin<"__builtin_tabortdci">,
      Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], []>;

def int_ppc_tcheck : ClangBuiltin<"__builtin_tcheck">,
      Intrinsic<[llvm_i32_ty], [], []>;
def int_ppc_treclaim : ClangBuiltin<"__builtin_treclaim">,
      Intrinsic<[llvm_i32_ty], [llvm_i32_ty], []>;
def int_ppc_trechkpt : ClangBuiltin<"__builtin_trechkpt">,
      Intrinsic<[llvm_i32_ty], [], []>;
def int_ppc_tsr : ClangBuiltin<"__builtin_tsr">,
      Intrinsic<[llvm_i32_ty], [llvm_i32_ty], []>;

def int_ppc_get_texasr : ClangBuiltin<"__builtin_get_texasr">,
      Intrinsic<[llvm_i64_ty], [], []>;
def int_ppc_get_texasru : ClangBuiltin<"__builtin_get_texasru">,
      Intrinsic<[llvm_i64_ty], [], []>;
def int_ppc_get_tfhar : ClangBuiltin<"__builtin_get_tfhar">,
      Intrinsic<[llvm_i64_ty], [], []>;
def int_ppc_get_tfiar : ClangBuiltin<"__builtin_get_tfiar">,
      Intrinsic<[llvm_i64_ty], [], []>;

def int_ppc_set_texasr : ClangBuiltin<"__builtin_set_texasr">,
      Intrinsic<[], [llvm_i64_ty], []>;
def int_ppc_set_texasru : ClangBuiltin<"__builtin_set_texasru">,
      Intrinsic<[], [llvm_i64_ty], []>;
def int_ppc_set_tfhar : ClangBuiltin<"__builtin_set_tfhar">,
      Intrinsic<[], [llvm_i64_ty], []>;
def int_ppc_set_tfiar : ClangBuiltin<"__builtin_set_tfiar">,
      Intrinsic<[], [llvm_i64_ty], []>;

// Extended mnemonics
def int_ppc_tendall : ClangBuiltin<"__builtin_tendall">,
      Intrinsic<[llvm_i32_ty], [], []>;
def int_ppc_tresume : ClangBuiltin<"__builtin_tresume">,
      Intrinsic<[llvm_i32_ty], [], []>;
def int_ppc_tsuspend : ClangBuiltin<"__builtin_tsuspend">,
      Intrinsic<[llvm_i32_ty], [], []>;

def int_ppc_ttest : ClangBuiltin<"__builtin_ttest">,
      Intrinsic<[llvm_i64_ty], [], []>;

// We currently use llvm.ppc.cfence in the context of atomic load which
// in LLVM IR requires its type to be one of integer, pointer and
// float point type. So llvm_any_ty here refers to type mentioned above.
// Backend is supposed to lower these types to appropriate MVTs.
def int_ppc_cfence : Intrinsic<[], [llvm_any_ty], []>;

// PowerPC set FPSCR Intrinsic Definitions.
def int_ppc_setrnd : ClangBuiltin<"__builtin_setrnd">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_i32_ty], [IntrHasSideEffects]>;
}

let TargetPrefix = "ppc" in {
  def int_ppc_vsx_assemble_pair :
      DefaultAttrsIntrinsic<[llvm_v256i1_ty],
                            [llvm_v16i8_ty, llvm_v16i8_ty], [IntrNoMem]>;

  def int_ppc_vsx_disassemble_pair :
      DefaultAttrsIntrinsic<[llvm_v16i8_ty, llvm_v16i8_ty],
                            [llvm_v256i1_ty], [IntrNoMem]>;

  def int_ppc_mma_assemble_acc :
      DefaultAttrsIntrinsic<[llvm_v512i1_ty],
                            [llvm_v16i8_ty, llvm_v16i8_ty, llvm_v16i8_ty,
                             llvm_v16i8_ty], [IntrNoMem]>;

  def int_ppc_mma_disassemble_acc :
      DefaultAttrsIntrinsic<[llvm_v16i8_ty, llvm_v16i8_ty, llvm_v16i8_ty,
                             llvm_v16i8_ty], [llvm_v512i1_ty], [IntrNoMem]>;

  def int_ppc_mma_xxmtacc :
      DefaultAttrsIntrinsic<[llvm_v512i1_ty], [llvm_v512i1_ty], [IntrNoMem]>;

  def int_ppc_mma_xxmfacc :
      DefaultAttrsIntrinsic<[llvm_v512i1_ty], [llvm_v512i1_ty], [IntrNoMem]>;

  def int_ppc_mma_xxsetaccz :
      DefaultAttrsIntrinsic<[llvm_v512i1_ty], [], [IntrNoMem]>;

  // MMA Reduced-Precision: Outer Product Intrinsic Definitions.
  defm int_ppc_mma_xvi4ger8 :
        PowerPC_MMA_ACC_PP_Intrinsic<[llvm_v16i8_ty, llvm_v16i8_ty]>;
  defm int_ppc_mma_pmxvi4ger8 :
        PowerPC_MMA_ACC_PP_Intrinsic<[llvm_v16i8_ty, llvm_v16i8_ty, llvm_i32_ty,
                                      llvm_i32_ty, llvm_i32_ty]>;

  defm int_ppc_mma_xvi8ger4 :
       PowerPC_MMA_ACC_PP_Intrinsic<[llvm_v16i8_ty, llvm_v16i8_ty]>;
  defm int_ppc_mma_pmxvi8ger4 :
       PowerPC_MMA_ACC_PP_Intrinsic<[llvm_v16i8_ty, llvm_v16i8_ty, llvm_i32_ty,
                                     llvm_i32_ty, llvm_i32_ty]>;

  defm int_ppc_mma_xvi16ger2s :
       PowerPC_MMA_ACC_PP_Intrinsic<[llvm_v16i8_ty, llvm_v16i8_ty]>;
  defm int_ppc_mma_pmxvi16ger2s :
       PowerPC_MMA_ACC_PP_Intrinsic<[llvm_v16i8_ty, llvm_v16i8_ty, llvm_i32_ty,
                                     llvm_i32_ty, llvm_i32_ty]>;

  defm int_ppc_mma_xvf16ger2 :
       PowerPC_MMA_ACC_Intrinsic<[llvm_v16i8_ty, llvm_v16i8_ty]>;
  defm int_ppc_mma_pmxvf16ger2 :
       PowerPC_MMA_ACC_Intrinsic<[llvm_v16i8_ty, llvm_v16i8_ty, llvm_i32_ty,
                                  llvm_i32_ty, llvm_i32_ty]>;
  defm int_ppc_mma_xvf32ger :
       PowerPC_MMA_ACC_Intrinsic<[llvm_v16i8_ty, llvm_v16i8_ty]>;
  defm int_ppc_mma_pmxvf32ger :
       PowerPC_MMA_ACC_Intrinsic<[llvm_v16i8_ty, llvm_v16i8_ty, llvm_i32_ty,
                                  llvm_i32_ty]>;
  defm int_ppc_mma_xvf64ger :
       PowerPC_MMA_ACC_Intrinsic<[llvm_v256i1_ty, llvm_v16i8_ty]>;
  defm int_ppc_mma_pmxvf64ger :
       PowerPC_MMA_ACC_Intrinsic<[llvm_v256i1_ty, llvm_v16i8_ty, llvm_i32_ty,
                                  llvm_i32_ty]>;

  // MMA Reduced-Precision: bfloat16 Outer Product Intrinsic Definitions.
  defm int_ppc_mma_xvbf16ger2 :
         PowerPC_MMA_ACC_Intrinsic<[llvm_v16i8_ty, llvm_v16i8_ty]>;
  defm int_ppc_mma_pmxvbf16ger2 :
         PowerPC_MMA_ACC_Intrinsic<
           [llvm_v16i8_ty, llvm_v16i8_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty]>;

  // MMA Reduced-Precision: Missing Integer-based Outer Product Operations.
  defm int_ppc_mma_xvi16ger2 :
         PowerPC_MMA_ACC_PP_Intrinsic<[llvm_v16i8_ty, llvm_v16i8_ty]>;
  defm int_ppc_mma_pmxvi16ger2 :
         PowerPC_MMA_ACC_PP_Intrinsic<[llvm_v16i8_ty, llvm_v16i8_ty, llvm_i32_ty,
                                       llvm_i32_ty, llvm_i32_ty]>;
  def int_ppc_mma_xvi8ger4spp :
      DefaultAttrsIntrinsic<[llvm_v512i1_ty],
                            [llvm_v512i1_ty, llvm_v16i8_ty, llvm_v16i8_ty],
                            [IntrNoMem]>;
  def int_ppc_mma_pmxvi8ger4spp :
      DefaultAttrsIntrinsic<[llvm_v512i1_ty],
                            [llvm_v512i1_ty, llvm_v16i8_ty, llvm_v16i8_ty,
                             llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
                            [IntrNoMem]>;
}

// XL Compat intrinsics.
let TargetPrefix = "ppc" in {
  def int_ppc_dcbfl : ClangBuiltin<"__builtin_ppc_dcbfl">,
                      Intrinsic<[], [llvm_ptr_ty], [IntrArgMemOnly]>;
  def int_ppc_dcbflp : ClangBuiltin<"__builtin_ppc_dcbflp">,
                       Intrinsic<[], [llvm_ptr_ty], [IntrArgMemOnly]>;
  def int_ppc_dcbst : ClangBuiltin<"__builtin_ppc_dcbst">,
                      Intrinsic<[], [llvm_ptr_ty], []>;
  def int_ppc_dcbt  : ClangBuiltin<"__builtin_ppc_dcbt">,
                      Intrinsic<[], [llvm_ptr_ty],
    [IntrArgMemOnly, NoCapture<ArgIndex<0>>]>;
  def int_ppc_dcbtst : ClangBuiltin<"__builtin_ppc_dcbtst">,
                       Intrinsic<[], [llvm_ptr_ty],
    [IntrArgMemOnly, NoCapture<ArgIndex<0>>]>;
  def int_ppc_dcbz  : ClangBuiltin<"__builtin_ppc_dcbz">,
                      Intrinsic<[], [llvm_ptr_ty], []>;
  def int_ppc_icbt : ClangBuiltin<"__builtin_ppc_icbt">,
                     Intrinsic<[], [llvm_ptr_ty], []>;
  
  // Population Count in each Byte.
  def int_ppc_popcntb :
      DefaultAttrsIntrinsic<[llvm_anyint_ty], [llvm_anyint_ty], [IntrNoMem]>;
  
  // sync instruction (i.e. sync 0, a.k.a hwsync)
  def int_ppc_sync : ClangBuiltin<"__builtin_ppc_sync">,
                     Intrinsic<[], [], []>;
  def int_ppc_iospace_sync : ClangBuiltin<"__builtin_ppc_iospace_sync">,
                             Intrinsic<[], [], []>;
  // isync instruction
  def int_ppc_isync : ClangBuiltin<"__builtin_ppc_isync">,
                      Intrinsic<[], [], []>;
  // lwsync is sync 1
  def int_ppc_lwsync : ClangBuiltin<"__builtin_ppc_lwsync">,
                       Intrinsic<[], [], []>;
  def int_ppc_iospace_lwsync : ClangBuiltin<"__builtin_ppc_iospace_lwsync">,
                               Intrinsic<[], [], []>;
  // eieio instruction
  def int_ppc_eieio : ClangBuiltin<"__builtin_ppc_eieio">,
                      Intrinsic<[],[],[]>;
  def int_ppc_iospace_eieio : ClangBuiltin<"__builtin_ppc_iospace_eieio">,
                              Intrinsic<[],[],[]>;
  def int_ppc_stdcx :
    ClangBuiltin<"__builtin_ppc_stdcx">,
    Intrinsic<[llvm_i32_ty], [llvm_ptr_ty, llvm_i64_ty],
              [IntrWriteMem, IntrArgMemOnly, IntrNoDuplicate]>;
  def int_ppc_stwcx :
    ClangBuiltin<"__builtin_ppc_stwcx">,
    Intrinsic<[llvm_i32_ty], [llvm_ptr_ty, llvm_i32_ty],
              [IntrWriteMem, IntrArgMemOnly]>;
  def int_ppc_sthcx :
    Intrinsic<[llvm_i32_ty], [ llvm_ptr_ty, llvm_i32_ty ],
              [IntrWriteMem, IntrArgMemOnly, IntrNoDuplicate]>;
  def int_ppc_stbcx :
    ClangBuiltin<"__builtin_ppc_stbcx">,
    Intrinsic<[llvm_i32_ty], [llvm_ptr_ty, llvm_i32_ty],
              [IntrWriteMem, IntrArgMemOnly, IntrNoDuplicate]>;
  def int_ppc_dcbtstt : ClangBuiltin<"__builtin_ppc_dcbtstt">,
                        Intrinsic<[], [llvm_ptr_ty],
                                  [IntrArgMemOnly, NoCapture<ArgIndex<0>>]>;
  def int_ppc_dcbtt : ClangBuiltin<"__builtin_ppc_dcbtt">,
                      Intrinsic<[], [llvm_ptr_ty],
                                [IntrArgMemOnly, NoCapture<ArgIndex<0>>]>;
  def int_ppc_mftbu : ClangBuiltin<"__builtin_ppc_mftbu">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem]>;
  def int_ppc_mfmsr : ClangBuiltin<"__builtin_ppc_mfmsr">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem]>;
  def int_ppc_mfspr
      : DefaultAttrsIntrinsic<[llvm_anyint_ty], [llvm_i32_ty], [ImmArg<ArgIndex<0>>]>;
  def int_ppc_mtmsr
      : ClangBuiltin<"__builtin_ppc_mtmsr">, Intrinsic<[], [llvm_i32_ty], []>;
  def int_ppc_mtspr
      : DefaultAttrsIntrinsic<[], [llvm_i32_ty, llvm_anyint_ty], [ImmArg<ArgIndex<0>>]>;
  def int_ppc_stfiw : ClangBuiltin<"__builtin_ppc_stfiw">,
                      DefaultAttrsIntrinsic<[], [llvm_ptr_ty, llvm_double_ty],
                                            [IntrWriteMem]>;
  // compare
  def int_ppc_cmpeqb
      : ClangBuiltin<"__builtin_ppc_cmpeqb">,
        DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty],
                              [IntrNoMem]>;
  def int_ppc_cmprb
      : ClangBuiltin<"__builtin_ppc_cmprb">,
        DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty,
                              llvm_i32_ty], [IntrNoMem, ImmArg<ArgIndex<0>>]>;
  def int_ppc_setb
      : ClangBuiltin<"__builtin_ppc_setb">,
        DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty],
                              [IntrNoMem]>;
  def int_ppc_cmpb
      : DefaultAttrsIntrinsic<[llvm_anyint_ty],
                              [llvm_anyint_ty, llvm_anyint_ty], [IntrNoMem]>;
  // multiply
  def int_ppc_mulhd
      : ClangBuiltin<"__builtin_ppc_mulhd">,
        DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty],
                              [IntrNoMem]>;
  def int_ppc_mulhdu
      : ClangBuiltin<"__builtin_ppc_mulhdu">,
        DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty],
                              [IntrNoMem]>;
  def int_ppc_mulhw
      : ClangBuiltin<"__builtin_ppc_mulhw">,
        DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
                              [IntrNoMem]>;
  def int_ppc_mulhwu
      : ClangBuiltin<"__builtin_ppc_mulhwu">,
        DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
                              [IntrNoMem]>;
  def int_ppc_maddhd
      : ClangBuiltin<"__builtin_ppc_maddhd">,
        DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty,
                               llvm_i64_ty], [IntrNoMem]>;
  def int_ppc_maddhdu
      : ClangBuiltin<"__builtin_ppc_maddhdu">,
        DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty,
                               llvm_i64_ty], [IntrNoMem]>;
  def int_ppc_maddld
      : ClangBuiltin<"__builtin_ppc_maddld">,
        DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty,
                               llvm_i64_ty], [IntrNoMem]>;
  // load
  def int_ppc_load2r
      : DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_ptr_ty],
                              [IntrReadMem, IntrArgMemOnly]>;
  def int_ppc_load4r
      : ClangBuiltin<"__builtin_ppc_load4r">,
        DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_ptr_ty],
                              [IntrReadMem, IntrArgMemOnly]>;
  def int_ppc_load8r
      : ClangBuiltin<"__builtin_ppc_load8r">,
        DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_ptr_ty],
                              [IntrReadMem, IntrArgMemOnly]>;
  // store
  def int_ppc_store2r
      : ClangBuiltin<"__builtin_ppc_store2r">,
        Intrinsic<[], [llvm_i32_ty, llvm_ptr_ty], [IntrWriteMem]>;
  def int_ppc_store4r
      : ClangBuiltin<"__builtin_ppc_store4r">,
        Intrinsic<[], [llvm_i32_ty, llvm_ptr_ty], [IntrWriteMem]>;
  def int_ppc_store8r
      : ClangBuiltin<"__builtin_ppc_store8r">,
        Intrinsic<[], [llvm_i64_ty, llvm_ptr_ty], [IntrWriteMem]>;
  def int_ppc_insert_exp
      : ClangBuiltin<"__builtin_ppc_insert_exp">,
        DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty, llvm_i64_ty],
                              [IntrNoMem]>;
  def int_ppc_extract_exp
      : ClangBuiltin<"__builtin_ppc_extract_exp">,
        DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_double_ty], [IntrNoMem]>;
  def int_ppc_extract_sig
      : ClangBuiltin<"__builtin_ppc_extract_sig">,
        DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_double_ty], [IntrNoMem]>;
  def int_ppc_mtfsb0
      : ClangBuiltin<"__builtin_ppc_mtfsb0">,
        DefaultAttrsIntrinsic<[], [llvm_i32_ty],
                              [IntrNoMem, IntrHasSideEffects,
                               ImmArg<ArgIndex<0>>]>;
  def int_ppc_mtfsb1
      : ClangBuiltin<"__builtin_ppc_mtfsb1">,
        DefaultAttrsIntrinsic<[], [llvm_i32_ty],
                              [IntrNoMem, IntrHasSideEffects,
                               ImmArg<ArgIndex<0>>]>;
  def int_ppc_mtfsf :
        DefaultAttrsIntrinsic<[], [llvm_i32_ty, llvm_double_ty],
                              [IntrNoMem, IntrHasSideEffects,
                               ImmArg<ArgIndex<0>>]>;
  def int_ppc_mtfsfi
      : ClangBuiltin<"__builtin_ppc_mtfsfi">,
        DefaultAttrsIntrinsic<[], [llvm_i32_ty, llvm_i32_ty],
                              [IntrNoMem, IntrHasSideEffects,
                               ImmArg<ArgIndex<0>>,ImmArg<ArgIndex<1>>]>;
  def int_ppc_fmsub
      : ClangBuiltin<"__builtin_ppc_fmsub">,
        DefaultAttrsIntrinsic<[llvm_double_ty],
                              [llvm_double_ty, llvm_double_ty, llvm_double_ty],
                              [IntrNoMem]>;
  def int_ppc_fmsubs
      : ClangBuiltin<"__builtin_ppc_fmsubs">,
        DefaultAttrsIntrinsic<[llvm_float_ty],
                              [llvm_float_ty, llvm_float_ty, llvm_float_ty],
                              [IntrNoMem]>;
  def int_ppc_fnmadd
      : ClangBuiltin<"__builtin_ppc_fnmadd">,
        DefaultAttrsIntrinsic<[llvm_double_ty],
                              [llvm_double_ty, llvm_double_ty, llvm_double_ty],
                              [IntrNoMem]>;
  def int_ppc_fnmadds
      : ClangBuiltin<"__builtin_ppc_fnmadds">,
        DefaultAttrsIntrinsic<[llvm_float_ty],
                              [llvm_float_ty, llvm_float_ty, llvm_float_ty],
                              [IntrNoMem]>;
  def int_ppc_fnmsub
      : DefaultAttrsIntrinsic<[llvm_anyfloat_ty],
                              [LLVMMatchType<0>, LLVMMatchType<0>,
                               LLVMMatchType<0>],
                              [IntrNoMem]>;
  def int_ppc_fre
      : ClangBuiltin<"__builtin_ppc_fre">,
        DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty], [IntrNoMem]>;
  def int_ppc_fres
      : ClangBuiltin<"__builtin_ppc_fres">,
        DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;
  def int_ppc_addex
      : ClangBuiltin<"__builtin_ppc_addex">,
        DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i64_ty, llvm_i32_ty],
                              [IntrNoMem, IntrHasSideEffects, ImmArg<ArgIndex<2>>]>;
  def int_ppc_fsel : ClangBuiltin<"__builtin_ppc_fsel">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty, llvm_double_ty, 
                             llvm_double_ty], [IntrNoMem]>;
  def int_ppc_fsels : ClangBuiltin<"__builtin_ppc_fsels">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty, llvm_float_ty,
                             llvm_float_ty], [IntrNoMem]>;
  def int_ppc_frsqrte : ClangBuiltin<"__builtin_ppc_frsqrte">,
      DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty], [IntrNoMem]>;
  def int_ppc_frsqrtes : ClangBuiltin<"__builtin_ppc_frsqrtes">,
      DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;
  def int_ppc_compare_exp_uo : ClangBuiltin<"__builtin_ppc_compare_exp_uo">,
      DefaultAttrsIntrinsic<[llvm_i32_ty],
                            [llvm_double_ty, llvm_double_ty],
                            [IntrNoMem]>;
  def int_ppc_compare_exp_lt : ClangBuiltin<"__builtin_ppc_compare_exp_lt">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], 
                            [llvm_double_ty, llvm_double_ty],
                            [IntrNoMem]>;
  def int_ppc_compare_exp_gt : ClangBuiltin<"__builtin_ppc_compare_exp_gt">,
      DefaultAttrsIntrinsic<[llvm_i32_ty],
                            [llvm_double_ty, llvm_double_ty],
                            [IntrNoMem]>;
  def int_ppc_compare_exp_eq : ClangBuiltin<"__builtin_ppc_compare_exp_eq">,
      DefaultAttrsIntrinsic<[llvm_i32_ty], 
                            [llvm_double_ty, llvm_double_ty],
                            [IntrNoMem]>;
  def int_ppc_test_data_class
      : DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_anyfloat_ty, llvm_i32_ty],
                              [IntrNoMem, ImmArg<ArgIndex<1>>]>;
  def int_ppc_fnabs
      : ClangBuiltin<"__builtin_ppc_fnabs">,
        DefaultAttrsIntrinsic<[llvm_double_ty], [llvm_double_ty], [IntrNoMem]>;
  def int_ppc_fnabss
      : ClangBuiltin<"__builtin_ppc_fnabss">,
        DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty], [IntrNoMem]>;

  def int_ppc_convert_f128_to_ppcf128
      : DefaultAttrsIntrinsic<[llvm_ppcf128_ty], [llvm_f128_ty], [IntrNoMem]>;
  def int_ppc_convert_ppcf128_to_f128
      : DefaultAttrsIntrinsic<[llvm_f128_ty], [llvm_ppcf128_ty], [IntrNoMem]>;
}

//===----------------------------------------------------------------------===//
// PowerPC Atomic Intrinsic Definitions.
let TargetPrefix = "ppc" in {
  class AtomicRMW128Intrinsic
    : Intrinsic<[llvm_i64_ty, llvm_i64_ty],
                [llvm_ptr_ty, llvm_i64_ty, llvm_i64_ty],
                [IntrArgMemOnly, NoCapture<ArgIndex<0>>]>;
  def int_ppc_atomicrmw_xchg_i128 : AtomicRMW128Intrinsic;
  def int_ppc_atomicrmw_add_i128  : AtomicRMW128Intrinsic;
  def int_ppc_atomicrmw_sub_i128  : AtomicRMW128Intrinsic;
  def int_ppc_atomicrmw_and_i128  : AtomicRMW128Intrinsic;
  def int_ppc_atomicrmw_or_i128   : AtomicRMW128Intrinsic;
  def int_ppc_atomicrmw_xor_i128  : AtomicRMW128Intrinsic;
  def int_ppc_atomicrmw_nand_i128 : AtomicRMW128Intrinsic;
  def int_ppc_cmpxchg_i128 : Intrinsic<[llvm_i64_ty, llvm_i64_ty],
                                       [llvm_ptr_ty,
                                        llvm_i64_ty, llvm_i64_ty,
                                        llvm_i64_ty, llvm_i64_ty],
                                       [IntrArgMemOnly, NoCapture<ArgIndex<0>>]>;
  def int_ppc_atomic_load_i128 :
    Intrinsic<[llvm_i64_ty, llvm_i64_ty],
              [llvm_ptr_ty],
              [IntrArgMemOnly, IntrReadMem, NoCapture<ArgIndex<0>>]>;
  def int_ppc_atomic_store_i128 :
    Intrinsic<[], [llvm_i64_ty, llvm_i64_ty, llvm_ptr_ty],
              [IntrArgMemOnly, IntrWriteMem, NoCapture<ArgIndex<2>>]>;
}
