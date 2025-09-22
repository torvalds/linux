//===- IntrinsicsAMDGPU.td - Defines AMDGPU intrinsics -----*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines all of the R600-specific intrinsics.
//
//===----------------------------------------------------------------------===//

def global_ptr_ty : LLVMQualPointerType<1>;

// The amdgpu-no-* attributes (ex amdgpu-no-workitem-id-z) typically inferred
// by the backend cause whole-program undefined behavior when violated, such as
// by causing all other preload register intrinsics to return arbitrarily incorrect
// values. In non-entry-point functions, attempting to call a function that needs
// some preloaded register from a function that is known to not need it is a violation
// of the calling convention and also program-level UB. Outside of such IR-level UB,
// these preloaded registers are always set to a well-defined value and are thus `noundef`.
class AMDGPUReadPreloadRegisterIntrinsic
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [NoUndef<RetIndex>, IntrNoMem, IntrSpeculatable]>;

class AMDGPUReadPreloadRegisterIntrinsicNamed<string name>
  : DefaultAttrsIntrinsic<[llvm_i32_ty], [], [NoUndef<RetIndex>, IntrNoMem, IntrSpeculatable]>, ClangBuiltin<name>;

// Used to tag image and resource intrinsics with information used to generate
// mem operands.
class AMDGPURsrcIntrinsic<int rsrcarg, bit isimage = false> {
  int RsrcArg = rsrcarg;
  bit IsImage = isimage;
}

let TargetPrefix = "r600" in {

multiclass AMDGPUReadPreloadRegisterIntrinsic_xyz {
  def _x : AMDGPUReadPreloadRegisterIntrinsic;
  def _y : AMDGPUReadPreloadRegisterIntrinsic;
  def _z : AMDGPUReadPreloadRegisterIntrinsic;
}

multiclass AMDGPUReadPreloadRegisterIntrinsic_xyz_named<string prefix> {
  def _x : AMDGPUReadPreloadRegisterIntrinsicNamed<!strconcat(prefix, "_x")>;
  def _y : AMDGPUReadPreloadRegisterIntrinsicNamed<!strconcat(prefix, "_y")>;
  def _z : AMDGPUReadPreloadRegisterIntrinsicNamed<!strconcat(prefix, "_z")>;
}

defm int_r600_read_global_size : AMDGPUReadPreloadRegisterIntrinsic_xyz_named
                                 <"__builtin_r600_read_global_size">;
defm int_r600_read_ngroups : AMDGPUReadPreloadRegisterIntrinsic_xyz_named
                             <"__builtin_r600_read_ngroups">;
defm int_r600_read_tgid : AMDGPUReadPreloadRegisterIntrinsic_xyz_named
                          <"__builtin_r600_read_tgid">;

defm int_r600_read_local_size : AMDGPUReadPreloadRegisterIntrinsic_xyz;
defm int_r600_read_tidig : AMDGPUReadPreloadRegisterIntrinsic_xyz;

def int_r600_group_barrier : ClangBuiltin<"__builtin_r600_group_barrier">,
  Intrinsic<[], [], [IntrConvergent, IntrWillReturn]>;

// AS 7 is PARAM_I_ADDRESS, used for kernel arguments
def int_r600_implicitarg_ptr :
  ClangBuiltin<"__builtin_r600_implicitarg_ptr">,
  DefaultAttrsIntrinsic<[LLVMQualPointerType<7>], [],
  [NoUndef<RetIndex>, IntrNoMem, IntrSpeculatable]>;

def int_r600_rat_store_typed :
  // 1st parameter: Data
  // 2nd parameter: Index
  // 3rd parameter: Constant RAT ID
  DefaultAttrsIntrinsic<[], [llvm_v4i32_ty, llvm_v4i32_ty, llvm_i32_ty], []>,
  ClangBuiltin<"__builtin_r600_rat_store_typed">;

def int_r600_recipsqrt_ieee :  DefaultAttrsIntrinsic<
  [llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem, IntrSpeculatable]
>;

def int_r600_recipsqrt_clamped : DefaultAttrsIntrinsic<
  [llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem, IntrSpeculatable]
>;

def int_r600_cube : DefaultAttrsIntrinsic<
  [llvm_v4f32_ty], [llvm_v4f32_ty], [IntrNoMem, IntrSpeculatable]
>;

def int_r600_store_stream_output : DefaultAttrsIntrinsic<
  [], [llvm_v4f32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty], []
>;

class TextureIntrinsicFloatInput : DefaultAttrsIntrinsic<[llvm_v4f32_ty], [
  llvm_v4f32_ty, // Coord
  llvm_i32_ty,   // offset_x
  llvm_i32_ty,   // offset_y,
  llvm_i32_ty,   // offset_z,
  llvm_i32_ty,   // resource_id
  llvm_i32_ty,   // samplerid
  llvm_i32_ty,   // coord_type_x
  llvm_i32_ty,   // coord_type_y
  llvm_i32_ty,   // coord_type_z
  llvm_i32_ty],  // coord_type_w
  [IntrNoMem]
>;

class TextureIntrinsicInt32Input : DefaultAttrsIntrinsic<[llvm_v4i32_ty], [
    llvm_v4i32_ty, // Coord
    llvm_i32_ty,   // offset_x
    llvm_i32_ty,   // offset_y,
    llvm_i32_ty,   // offset_z,
    llvm_i32_ty,   // resource_id
    llvm_i32_ty,   // samplerid
    llvm_i32_ty,   // coord_type_x
    llvm_i32_ty,   // coord_type_y
    llvm_i32_ty,   // coord_type_z
    llvm_i32_ty],  // coord_type_w
    [IntrNoMem]
>;

def int_r600_store_swizzle :
  Intrinsic<[], [llvm_v4f32_ty, llvm_i32_ty, llvm_i32_ty], [IntrWillReturn, IntrNoCallback, IntrNoFree]
>;

def int_r600_tex : TextureIntrinsicFloatInput;
def int_r600_texc : TextureIntrinsicFloatInput;
def int_r600_txl : TextureIntrinsicFloatInput;
def int_r600_txlc : TextureIntrinsicFloatInput;
def int_r600_txb : TextureIntrinsicFloatInput;
def int_r600_txbc : TextureIntrinsicFloatInput;
def int_r600_txf : TextureIntrinsicInt32Input;
def int_r600_txq : TextureIntrinsicInt32Input;
def int_r600_ddx : TextureIntrinsicFloatInput;
def int_r600_ddy : TextureIntrinsicFloatInput;

def int_r600_dot4 : DefaultAttrsIntrinsic<[llvm_float_ty],
  [llvm_v4f32_ty, llvm_v4f32_ty], [IntrNoMem, IntrSpeculatable]
>;

def int_r600_kill : DefaultAttrsIntrinsic<[], [llvm_float_ty], []>;

} // End TargetPrefix = "r600"

let TargetPrefix = "amdgcn" in {

//===----------------------------------------------------------------------===//
// ABI Special Intrinsics
//===----------------------------------------------------------------------===//

defm int_amdgcn_workitem_id : AMDGPUReadPreloadRegisterIntrinsic_xyz;
defm int_amdgcn_workgroup_id : AMDGPUReadPreloadRegisterIntrinsic_xyz_named
                               <"__builtin_amdgcn_workgroup_id">;

def int_amdgcn_dispatch_ptr :
  DefaultAttrsIntrinsic<[LLVMQualPointerType<4>], [],
  [Align<RetIndex, 4>, NoUndef<RetIndex>, NonNull<RetIndex>, IntrNoMem, IntrSpeculatable]>;

def int_amdgcn_queue_ptr :
  ClangBuiltin<"__builtin_amdgcn_queue_ptr">,
  DefaultAttrsIntrinsic<[LLVMQualPointerType<4>], [],
  [Align<RetIndex, 4>, NoUndef<RetIndex>, NonNull<RetIndex>, IntrNoMem, IntrSpeculatable]>;

def int_amdgcn_kernarg_segment_ptr :
  ClangBuiltin<"__builtin_amdgcn_kernarg_segment_ptr">,
  DefaultAttrsIntrinsic<[LLVMQualPointerType<4>], [],
  [Align<RetIndex, 4>, NoUndef<RetIndex>, IntrNoMem, IntrSpeculatable]>;

def int_amdgcn_implicitarg_ptr :
  ClangBuiltin<"__builtin_amdgcn_implicitarg_ptr">,
  DefaultAttrsIntrinsic<[LLVMQualPointerType<4>], [],
  [Align<RetIndex, 4>, NoUndef<RetIndex>, IntrNoMem, IntrSpeculatable]>;

// Returns the amount of LDS statically allocated for this program.
// This is no longer guaranteed to be a compile-time constant due to linking
// support.
def int_amdgcn_groupstaticsize :
  ClangBuiltin<"__builtin_amdgcn_groupstaticsize">,
  DefaultAttrsIntrinsic<[llvm_i32_ty], [], [NoUndef<RetIndex>, IntrNoMem, IntrSpeculatable]>;

def int_amdgcn_dispatch_id :
  ClangBuiltin<"__builtin_amdgcn_dispatch_id">,
  DefaultAttrsIntrinsic<[llvm_i64_ty], [], [NoUndef<RetIndex>, IntrNoMem, IntrSpeculatable]>;

// For internal use. Coordinates LDS lowering between IR transform and backend.
def int_amdgcn_lds_kernel_id :
  DefaultAttrsIntrinsic<[llvm_i32_ty], [], [NoUndef<RetIndex>, IntrNoMem, IntrSpeculatable]>;

def int_amdgcn_implicit_buffer_ptr :
  ClangBuiltin<"__builtin_amdgcn_implicit_buffer_ptr">,
  DefaultAttrsIntrinsic<[LLVMQualPointerType<4>], [],
  [Align<RetIndex, 4>, NoUndef<RetIndex>,
    IntrNoMem, IntrSpeculatable]>;

// Set EXEC to the 64-bit value given.
// This is always moved to the beginning of the basic block.
// FIXME: Should be mangled for wave size.
def int_amdgcn_init_exec : Intrinsic<[],
  [llvm_i64_ty],      // 64-bit literal constant
  [IntrConvergent, IntrNoMem, IntrHasSideEffects, IntrNoCallback,
   IntrNoFree, IntrWillReturn, ImmArg<ArgIndex<0>>]>;

// Set EXEC according to a thread count packed in an SGPR input:
//    thread_count = (input >> bitoffset) & 0x7f;
// This is always moved to the beginning of the basic block.
// Note: only inreg arguments to the parent function are valid as
// inputs to this intrinsic, computed values cannot be used.
def int_amdgcn_init_exec_from_input : Intrinsic<[],
  [llvm_i32_ty,       // 32-bit SGPR input
   llvm_i32_ty],      // bit offset of the thread count
  [IntrConvergent, IntrHasSideEffects, IntrNoMem, IntrNoCallback,
   IntrNoFree, IntrWillReturn, ImmArg<ArgIndex<1>>]>;

def int_amdgcn_wavefrontsize :
  ClangBuiltin<"__builtin_amdgcn_wavefrontsize">,
  DefaultAttrsIntrinsic<[llvm_i32_ty], [], [NoUndef<RetIndex>, IntrNoMem, IntrSpeculatable]>;

// Represent a relocation constant.
def int_amdgcn_reloc_constant : DefaultAttrsIntrinsic<
  [llvm_i32_ty], [llvm_metadata_ty],
  [IntrNoMem, IntrSpeculatable]
>;

//===----------------------------------------------------------------------===//
// Instruction Intrinsics
//===----------------------------------------------------------------------===//

// The first parameter is s_sendmsg immediate (i16),
// the second one is copied to m0
def int_amdgcn_s_sendmsg : ClangBuiltin<"__builtin_amdgcn_s_sendmsg">,
  Intrinsic <[], [llvm_i32_ty, llvm_i32_ty],
  [ImmArg<ArgIndex<0>>, IntrNoMem, IntrHasSideEffects]>;
def int_amdgcn_s_sendmsghalt : ClangBuiltin<"__builtin_amdgcn_s_sendmsghalt">,
  Intrinsic <[], [llvm_i32_ty, llvm_i32_ty],
  [ImmArg<ArgIndex<0>>, IntrNoMem, IntrHasSideEffects]>;


// gfx11 intrinsic
// The first parameter is s_sendmsg immediate (i16). Return type is i32 or i64.
def int_amdgcn_s_sendmsg_rtn : Intrinsic <[llvm_anyint_ty], [llvm_i32_ty],
  [ImmArg<ArgIndex<0>>, IntrNoMem, IntrHasSideEffects]>;

def int_amdgcn_s_barrier : ClangBuiltin<"__builtin_amdgcn_s_barrier">,
  Intrinsic<[], [], [IntrNoMem, IntrHasSideEffects, IntrConvergent, IntrWillReturn, IntrNoCallback, IntrNoFree]>;

def int_amdgcn_s_barrier_signal : ClangBuiltin<"__builtin_amdgcn_s_barrier_signal">,
  Intrinsic<[], [llvm_i32_ty], [ImmArg<ArgIndex<0>>, IntrNoMem, IntrHasSideEffects, IntrConvergent, IntrWillReturn,
                                IntrNoCallback, IntrNoFree]>;

def int_amdgcn_s_barrier_signal_var : ClangBuiltin<"__builtin_amdgcn_s_barrier_signal_var">,
  Intrinsic<[], [llvm_i32_ty], [IntrNoMem, IntrHasSideEffects, IntrConvergent, IntrWillReturn,
                                IntrNoCallback, IntrNoFree]>;

def int_amdgcn_s_barrier_signal_isfirst : ClangBuiltin<"__builtin_amdgcn_s_barrier_signal_isfirst">,
  Intrinsic<[llvm_i1_ty], [llvm_i32_ty], [ImmArg<ArgIndex<0>>, IntrNoMem, IntrHasSideEffects, IntrConvergent,
                                IntrWillReturn, IntrNoCallback, IntrNoFree]>;

def int_amdgcn_s_barrier_signal_isfirst_var : ClangBuiltin<"__builtin_amdgcn_s_barrier_signal_isfirst_var">,
  Intrinsic<[llvm_i1_ty], [llvm_i32_ty], [IntrNoMem, IntrHasSideEffects, IntrConvergent, IntrWillReturn,
                                IntrNoCallback, IntrNoFree]>;

def int_amdgcn_s_barrier_init : ClangBuiltin<"__builtin_amdgcn_s_barrier_init">,
  Intrinsic<[], [llvm_i32_ty, llvm_i32_ty], [IntrNoMem, IntrHasSideEffects, IntrConvergent,
                                IntrWillReturn, IntrNoCallback, IntrNoFree]>;

def int_amdgcn_s_barrier_join : ClangBuiltin<"__builtin_amdgcn_s_barrier_join">,
  Intrinsic<[], [llvm_i32_ty], [IntrNoMem, IntrHasSideEffects, IntrConvergent, IntrWillReturn,
                                IntrNoCallback, IntrNoFree]>;

def int_amdgcn_s_wakeup_barrier : ClangBuiltin<"__builtin_amdgcn_s_wakeup_barrier">,
  Intrinsic<[], [llvm_i32_ty], [IntrNoMem, IntrHasSideEffects, IntrConvergent, IntrWillReturn,
                                IntrNoCallback, IntrNoFree]>;

def int_amdgcn_s_barrier_wait : ClangBuiltin<"__builtin_amdgcn_s_barrier_wait">,
  Intrinsic<[], [llvm_i16_ty], [ImmArg<ArgIndex<0>>, IntrNoMem, IntrHasSideEffects, IntrConvergent,
                                IntrWillReturn, IntrNoCallback, IntrNoFree]>;

def int_amdgcn_s_barrier_leave : ClangBuiltin<"__builtin_amdgcn_s_barrier_leave">,
  Intrinsic<[llvm_i1_ty], [], [IntrNoMem, IntrHasSideEffects, IntrConvergent, IntrWillReturn, IntrNoCallback, IntrNoFree]>;

def int_amdgcn_s_get_barrier_state : ClangBuiltin<"__builtin_amdgcn_s_get_barrier_state">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty], [IntrNoMem, IntrHasSideEffects, IntrConvergent, IntrWillReturn,
                                IntrNoCallback, IntrNoFree]>;

def int_amdgcn_wave_barrier : ClangBuiltin<"__builtin_amdgcn_wave_barrier">,
  Intrinsic<[], [], [IntrNoMem, IntrHasSideEffects, IntrConvergent, IntrWillReturn, IntrNoCallback, IntrNoFree]>;

// The 1st parameter is a mask for the types of instructions that may be allowed
// to cross the SCHED_BARRIER during scheduling.
//     MASK = 0x0000 0000: No instructions may be scheduled across SCHED_BARRIER.
//     MASK = 0x0000 0001: ALL, non-memory, non-side-effect producing instructions may be
//                         scheduled across SCHED_BARRIER, i.e. allow ALU instructions to pass.
//     MASK = 0x0000 0002: VALU instructions may be scheduled across SCHED_BARRIER.
//     MASK = 0x0000 0004: SALU instructions may be scheduled across SCHED_BARRIER.
//     MASK = 0x0000 0008: MFMA/WMMA instructions may be scheduled across SCHED_BARRIER.
//     MASK = 0x0000 0010: ALL VMEM instructions may be scheduled across SCHED_BARRIER.
//     MASK = 0x0000 0020: VMEM read instructions may be scheduled across SCHED_BARRIER.
//     MASK = 0x0000 0040: VMEM write instructions may be scheduled across SCHED_BARRIER.
//     MASK = 0x0000 0080: ALL DS instructions may be scheduled across SCHED_BARRIER.
//     MASK = 0x0000 0100: ALL DS read instructions may be scheduled accoss SCHED_BARRIER.
//     MASK = 0x0000 0200: ALL DS write instructions may be scheduled across SCHED_BARRIER.
def int_amdgcn_sched_barrier : ClangBuiltin<"__builtin_amdgcn_sched_barrier">,
  Intrinsic<[], [llvm_i32_ty], [ImmArg<ArgIndex<0>>, IntrNoMem, IntrHasSideEffects, IntrConvergent,
                                IntrWillReturn, IntrNoCallback, IntrNoFree]>;

// The first parameter is a mask that determines the types of instructions that
// you would like to synchronize around and add to a scheduling group. The
// values of the mask are defined above for sched_barrier. These instructions
// will be selected from the bottom up starting from the sched_group_barrier's
// location during instruction scheduling. The second parameter is the number of
// matching instructions that will be associated with this sched_group_barrier.
// The third parameter is an identifier which is used to describe what other
// sched_group_barriers should be synchronized with.
def int_amdgcn_sched_group_barrier : ClangBuiltin<"__builtin_amdgcn_sched_group_barrier">,
  Intrinsic<[], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
  [ImmArg<ArgIndex<0>>, ImmArg<ArgIndex<1>>, ImmArg<ArgIndex<2>>, IntrNoMem, IntrHasSideEffects,
   IntrConvergent, IntrWillReturn, IntrNoCallback, IntrNoFree]>;

// Scheduler optimization hint.
//     MASK = 0: Small gemm opt
def int_amdgcn_iglp_opt : ClangBuiltin<"__builtin_amdgcn_iglp_opt">,
  Intrinsic<[], [llvm_i32_ty], [ImmArg<ArgIndex<0>>, IntrNoMem, IntrHasSideEffects, IntrConvergent,
                                IntrWillReturn, IntrNoCallback, IntrNoFree]>;

def int_amdgcn_s_waitcnt : ClangBuiltin<"__builtin_amdgcn_s_waitcnt">,
  Intrinsic<[], [llvm_i32_ty], [ImmArg<ArgIndex<0>>, IntrNoMem, IntrHasSideEffects, IntrWillReturn, IntrNoCallback, IntrNoFree]>;

// GFX12 intrinsics
class AMDGPUWaitIntrinsic :
  Intrinsic<[], [llvm_i16_ty], [ImmArg<ArgIndex<0>>, IntrNoMem, IntrHasSideEffects, IntrWillReturn, IntrNoCallback, IntrNoFree]>;
def int_amdgcn_s_wait_bvhcnt         : AMDGPUWaitIntrinsic;
def int_amdgcn_s_wait_dscnt          : AMDGPUWaitIntrinsic;
def int_amdgcn_s_wait_expcnt         : AMDGPUWaitIntrinsic;
def int_amdgcn_s_wait_kmcnt          : AMDGPUWaitIntrinsic;
def int_amdgcn_s_wait_loadcnt        : AMDGPUWaitIntrinsic;
def int_amdgcn_s_wait_samplecnt      : AMDGPUWaitIntrinsic;
def int_amdgcn_s_wait_storecnt       : AMDGPUWaitIntrinsic;

def int_amdgcn_div_scale : DefaultAttrsIntrinsic<
  // 1st parameter: Numerator
  // 2nd parameter: Denominator
  // 3rd parameter: Select quotient. Must equal Numerator or Denominator.
  //                (0 = Denominator, 1 = Numerator).
  [llvm_anyfloat_ty, llvm_i1_ty],
  [LLVMMatchType<0>, LLVMMatchType<0>, llvm_i1_ty],
  [IntrNoMem, IntrSpeculatable, ImmArg<ArgIndex<2>>]
>;

def int_amdgcn_div_fmas : DefaultAttrsIntrinsic<[llvm_anyfloat_ty],
  [LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>, llvm_i1_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_div_fixup : DefaultAttrsIntrinsic<[llvm_anyfloat_ty],
  [LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>],
  [IntrNoMem, IntrSpeculatable]
>;

// Look Up 2.0 / pi src0 with segment select src1[4:0]
def int_amdgcn_trig_preop : DefaultAttrsIntrinsic<
  [llvm_anyfloat_ty], [LLVMMatchType<0>, llvm_i32_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_sin : DefaultAttrsIntrinsic<
  [llvm_anyfloat_ty], [LLVMMatchType<0>],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_cos : DefaultAttrsIntrinsic<
  [llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem, IntrSpeculatable]
>;

// v_log_{f16|f32}, performs log2. f32 version does not handle
// denormals. There is no reason to use this for f16 as it does
// support denormals, and the generic log2 intrinsic should be
// preferred.
def int_amdgcn_log : DefaultAttrsIntrinsic<
  [llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem, IntrSpeculatable]
>;

// v_exp_{f16|f32} (int_amdgcn_exp was taken by export
// already). Performs exp2. f32 version does not handle
// denormals. There is no reason to use this for f16 as it does
// support denormals, and the generic exp2 intrinsic should be
// preferred.
def int_amdgcn_exp2 : DefaultAttrsIntrinsic<
  [llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_log_clamp : DefaultAttrsIntrinsic<
  [llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_fmul_legacy : ClangBuiltin<"__builtin_amdgcn_fmul_legacy">,
  DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty, llvm_float_ty],
  [IntrNoMem, IntrSpeculatable, Commutative]
>;

// Fused single-precision multiply-add with legacy behaviour for the multiply,
// which is that +/- 0.0 * anything (even NaN or infinity) is +0.0. This is
// intended for use on subtargets that have the v_fma_legacy_f32 and/or
// v_fmac_legacy_f32 instructions. (Note that v_fma_legacy_f16 is unrelated and
// has a completely different kind of legacy behaviour.)
def int_amdgcn_fma_legacy :
  DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty, llvm_float_ty, llvm_float_ty],
  [IntrNoMem, IntrSpeculatable, Commutative]
>;

def int_amdgcn_rcp : DefaultAttrsIntrinsic<
  [llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_rcp_legacy : ClangBuiltin<"__builtin_amdgcn_rcp_legacy">,
  DefaultAttrsIntrinsic<[llvm_float_ty], [llvm_float_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_sqrt :  DefaultAttrsIntrinsic<
  [llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_rsq :  DefaultAttrsIntrinsic<
  [llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_rsq_legacy :  ClangBuiltin<"__builtin_amdgcn_rsq_legacy">,
  DefaultAttrsIntrinsic<
  [llvm_float_ty], [llvm_float_ty], [IntrNoMem, IntrSpeculatable]
>;

// out = 1.0 / sqrt(a) result clamped to +/- max_float.
def int_amdgcn_rsq_clamp : DefaultAttrsIntrinsic<
  [llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem, IntrSpeculatable]>;

def int_amdgcn_frexp_mant : DefaultAttrsIntrinsic<
  [llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_frexp_exp : DefaultAttrsIntrinsic<
  [llvm_anyint_ty], [llvm_anyfloat_ty], [IntrNoMem, IntrSpeculatable]
>;

// v_fract is buggy on SI/CI. It mishandles infinities, may return 1.0
// and always uses rtz, so is not suitable for implementing the OpenCL
// fract function. It should be ok on VI.
def int_amdgcn_fract : DefaultAttrsIntrinsic<
  [llvm_anyfloat_ty], [LLVMMatchType<0>], [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_cvt_pkrtz : ClangBuiltin<"__builtin_amdgcn_cvt_pkrtz">,
  DefaultAttrsIntrinsic<[llvm_v2f16_ty], [llvm_float_ty, llvm_float_ty],
            [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_cvt_pknorm_i16 :
  ClangBuiltin<"__builtin_amdgcn_cvt_pknorm_i16">,
  DefaultAttrsIntrinsic<[llvm_v2i16_ty], [llvm_float_ty, llvm_float_ty],
            [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_cvt_pknorm_u16 :
  ClangBuiltin<"__builtin_amdgcn_cvt_pknorm_u16">,
  DefaultAttrsIntrinsic<[llvm_v2i16_ty], [llvm_float_ty, llvm_float_ty],
            [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_cvt_pk_i16 :
    ClangBuiltin<"__builtin_amdgcn_cvt_pk_i16">,
    DefaultAttrsIntrinsic<
  [llvm_v2i16_ty], [llvm_i32_ty, llvm_i32_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_cvt_pk_u16 : ClangBuiltin<"__builtin_amdgcn_cvt_pk_u16">,
  DefaultAttrsIntrinsic<[llvm_v2i16_ty], [llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_class : DefaultAttrsIntrinsic<
  [llvm_i1_ty], [llvm_anyfloat_ty, llvm_i32_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_fmed3 :
  DefaultAttrsIntrinsic<[llvm_anyfloat_ty],
    [LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>],
    [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_cubeid : ClangBuiltin<"__builtin_amdgcn_cubeid">,
  DefaultAttrsIntrinsic<[llvm_float_ty],
    [llvm_float_ty, llvm_float_ty, llvm_float_ty],
    [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_cubema : ClangBuiltin<"__builtin_amdgcn_cubema">,
  DefaultAttrsIntrinsic<[llvm_float_ty],
  [llvm_float_ty, llvm_float_ty, llvm_float_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_cubesc : ClangBuiltin<"__builtin_amdgcn_cubesc">,
  DefaultAttrsIntrinsic<[llvm_float_ty],
    [llvm_float_ty, llvm_float_ty, llvm_float_ty],
    [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_cubetc : ClangBuiltin<"__builtin_amdgcn_cubetc">,
  DefaultAttrsIntrinsic<[llvm_float_ty],
    [llvm_float_ty, llvm_float_ty, llvm_float_ty],
    [IntrNoMem, IntrSpeculatable]
>;

// v_ffbh_i32, as opposed to v_ffbh_u32. For v_ffbh_u32, llvm.ctlz
// should be used.
def int_amdgcn_sffbh :
  DefaultAttrsIntrinsic<[llvm_anyint_ty], [LLVMMatchType<0>],
  [IntrNoMem, IntrSpeculatable]
>;

// v_mad_f32|f16/v_mac_f32|f16, selected regardless of denorm support.
def int_amdgcn_fmad_ftz :
  DefaultAttrsIntrinsic<[llvm_anyfloat_ty],
            [LLVMMatchType<0>, LLVMMatchType<0>, LLVMMatchType<0>],
            [IntrNoMem, IntrSpeculatable]
>;

// FIXME: The m0 argument should be moved after the normal arguments
class AMDGPUDSOrderedIntrinsic : Intrinsic<
  [llvm_i32_ty],
  // M0 = {hi16:address, lo16:waveID}. Allow passing M0 as a pointer, so that
  // the bit packing can be optimized at the IR level.
  [LLVMQualPointerType<2>, // IntToPtr(M0)
   llvm_i32_ty, // value to add or swap
   llvm_i32_ty, // ordering
   llvm_i32_ty, // scope
   llvm_i1_ty,  // isVolatile
   llvm_i32_ty, // ordered count index (OA index), also added to the address
                // gfx10: bits 24-27 indicate the number of active threads/dwords
   llvm_i1_ty,  // wave release, usually set to 1
   llvm_i1_ty], // wave done, set to 1 for the last ordered instruction
  [IntrWillReturn, NoCapture<ArgIndex<0>>,
   ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<3>>, ImmArg<ArgIndex<4>>,
   ImmArg<ArgIndex<5>>, ImmArg<ArgIndex<6>>, ImmArg<ArgIndex<7>>, IntrNoCallback, IntrNoFree
  ]
>;

class AMDGPUDSAppendConsumedIntrinsic : Intrinsic<
  [llvm_i32_ty],
  [llvm_anyptr_ty, // LDS or GDS ptr
   llvm_i1_ty], // isVolatile
   [IntrConvergent, IntrWillReturn, IntrArgMemOnly,
    NoCapture<ArgIndex<0>>, ImmArg<ArgIndex<1>>, IntrNoCallback, IntrNoFree],
   "",
   [SDNPMemOperand]
>;

def int_amdgcn_ds_ordered_add : AMDGPUDSOrderedIntrinsic;
def int_amdgcn_ds_ordered_swap : AMDGPUDSOrderedIntrinsic;

// The pointer argument is assumed to be dynamically uniform if a VGPR.
def int_amdgcn_ds_append : AMDGPUDSAppendConsumedIntrinsic;
def int_amdgcn_ds_consume : AMDGPUDSAppendConsumedIntrinsic;

} // TargetPrefix = "amdgcn"

// New-style image intrinsics

//////////////////////////////////////////////////////////////////////////
// Dimension-aware image intrinsics framework
//////////////////////////////////////////////////////////////////////////

// Helper class to represent (type, name) combinations of arguments. The
// argument names are explanatory and used as DAG operand names for codegen
// pattern matching.
class AMDGPUArg<LLVMType ty, string name> {
  LLVMType Type = ty;
  string Name = name;
}

// Return [AMDGPUArg<basety, names[0]>, AMDGPUArg<LLVMMatchType<0>, names[1]>, ...]
class makeArgList<list<string> names, LLVMType basety> {
  list<AMDGPUArg> ret =
    !listconcat([AMDGPUArg<basety, names[0]>],
                !foreach(name, !tail(names), AMDGPUArg<LLVMMatchType<0>, name>));
}

// Return arglist, with LLVMMatchType's references shifted by 'shift'.
class arglistmatchshift<list<AMDGPUArg> arglist, int shift> {
  list<AMDGPUArg> ret =
    !foreach(arg, arglist,
             !if(!isa<LLVMMatchType>(arg.Type),
                 AMDGPUArg<LLVMMatchType<!add(!cast<LLVMMatchType>(arg.Type).Number, shift)>,
                           arg.Name>,
                 arg));
}

// Return the concatenation of the given arglists. LLVMMatchType's are adjusted
// accordingly, and shifted by an additional 'shift'.
class arglistconcat<list<list<AMDGPUArg>> arglists, int shift = 0> {
  list<AMDGPUArg> ret =
    !foldl([]<AMDGPUArg>, arglists, lhs, rhs,
           !listconcat(
             lhs,
             arglistmatchshift<rhs,
                               !add(shift, !foldl(0, lhs, a, b,
                                                  !add(a, b.Type.isAny)))>.ret));
}

// Represent texture/image types / dimensionality.
class AMDGPUDimProps<bits<3> enc, string name, string asmsuffix,
                     list<string> coord_names, list<string> slice_names,
                     bit msaa = 0> {
  AMDGPUDimProps Dim = !cast<AMDGPUDimProps>(NAME);
  string Name = name; // e.g. "2darraymsaa"
  string AsmSuffix = asmsuffix; // e.g. 2D_MSAA_ARRAY (used in assembly strings)
  bits<3> Encoding = enc;
  bit DA = 0; // DA bit in MIMG encoding
  bit MSAA = msaa;

  list<AMDGPUArg> CoordSliceArgs =
    makeArgList<!listconcat(coord_names, slice_names), llvm_anyfloat_ty>.ret;
  list<AMDGPUArg> CoordSliceIntArgs =
    makeArgList<!listconcat(coord_names, slice_names), llvm_anyint_ty>.ret;
  list<AMDGPUArg> GradientArgs =
    makeArgList<!listconcat(!foreach(name, coord_names, "d" # name # "dh"),
                            !foreach(name, coord_names, "d" # name # "dv")),
                llvm_anyfloat_ty>.ret;

  bits<8> NumCoords = !size(CoordSliceArgs);
  bits<8> NumGradients = !size(GradientArgs);
}

def AMDGPUDim1D : AMDGPUDimProps<0x0, "1d", "1D", ["s"], []>;
def AMDGPUDim2D : AMDGPUDimProps<0x1, "2d", "2D", ["s", "t"], []>;
def AMDGPUDim3D : AMDGPUDimProps<0x2, "3d", "3D", ["s", "t", "r"], []>;
let DA = 1 in {
  def AMDGPUDimCube : AMDGPUDimProps<0x3, "cube", "CUBE", ["s", "t"], ["face"]>;
  def AMDGPUDim1DArray : AMDGPUDimProps<0x4, "1darray", "1D_ARRAY", ["s"], ["slice"]>;
  def AMDGPUDim2DArray : AMDGPUDimProps<0x5, "2darray", "2D_ARRAY", ["s", "t"], ["slice"]>;
}
def AMDGPUDim2DMsaa : AMDGPUDimProps<0x6, "2dmsaa", "2D_MSAA", ["s", "t"], ["fragid"], 1>;
let DA = 1 in {
  def AMDGPUDim2DArrayMsaa : AMDGPUDimProps<0x7, "2darraymsaa", "2D_MSAA_ARRAY", ["s", "t"], ["slice", "fragid"], 1>;
}

def AMDGPUDims {
  list<AMDGPUDimProps> NoMsaa = [AMDGPUDim1D, AMDGPUDim2D, AMDGPUDim3D,
                                 AMDGPUDimCube, AMDGPUDim1DArray,
                                 AMDGPUDim2DArray];
  list<AMDGPUDimProps> Msaa = [AMDGPUDim2DMsaa, AMDGPUDim2DArrayMsaa];
  list<AMDGPUDimProps> All = !listconcat(NoMsaa, Msaa);
}

// Represent sample variants, i.e. _C, _O, _B, ... and combinations thereof.
class AMDGPUSampleVariant<string ucmod, string lcmod, list<AMDGPUArg> extra_addr> {
  string UpperCaseMod = ucmod;
  string LowerCaseMod = lcmod;

  // {offset} {bias} {z-compare}
  list<AMDGPUArg> ExtraAddrArgs = extra_addr;
  bit Offset = false;
  bit Bias = false;
  bit ZCompare = false;
  bit Gradients = false;

  // Name of the {lod} or {clamp} argument that is appended to the coordinates,
  // if any.
  string LodOrClamp = "";
}

// AMDGPUSampleVariants: all variants supported by IMAGE_SAMPLE
// AMDGPUSampleVariantsNoGradients: variants supported by IMAGE_GATHER4
defset list<AMDGPUSampleVariant> AMDGPUSampleVariants = {
  multiclass AMDGPUSampleHelper_Offset<string ucmod, string lcmod,
                                       list<AMDGPUArg> extra_addr> {
    def NAME#lcmod : AMDGPUSampleVariant<ucmod, lcmod, extra_addr>;
    let Offset = true in
    def NAME#lcmod#_o : AMDGPUSampleVariant<
        ucmod#"_O", lcmod#"_o", !listconcat([AMDGPUArg<llvm_i32_ty, "offset">], extra_addr)>;
  }

  multiclass AMDGPUSampleHelper_Compare<string ucmod, string lcmod,
                                        list<AMDGPUArg> extra_addr> {
    defm NAME : AMDGPUSampleHelper_Offset<ucmod, lcmod, extra_addr>;
    let ZCompare = true in
    defm NAME : AMDGPUSampleHelper_Offset<
        "_C"#ucmod, "_c"#lcmod, !listconcat(extra_addr, [AMDGPUArg<llvm_float_ty, "zcompare">])>;
  }

  multiclass AMDGPUSampleHelper_Clamp<string ucmod, string lcmod,
                                      list<AMDGPUArg> extra_addr> {
    defm NAME : AMDGPUSampleHelper_Compare<ucmod, lcmod, extra_addr>;
    let LodOrClamp = "clamp" in
    defm NAME : AMDGPUSampleHelper_Compare<ucmod#"_CL", lcmod#"_cl", extra_addr>;
  }

  defset list<AMDGPUSampleVariant> AMDGPUSampleVariantsNoGradients = {
    defm AMDGPUSample : AMDGPUSampleHelper_Clamp<"", "", []>;
    let Bias = true in
    defm AMDGPUSample : AMDGPUSampleHelper_Clamp<
        "_B", "_b", [AMDGPUArg<llvm_anyfloat_ty, "bias">]>;
    let LodOrClamp = "lod" in
    defm AMDGPUSample : AMDGPUSampleHelper_Compare<"_L", "_l", []>;
    defm AMDGPUSample : AMDGPUSampleHelper_Compare<"_LZ", "_lz", []>;
  }

  let Gradients = true in {
    defm AMDGPUSample : AMDGPUSampleHelper_Clamp<"_D", "_d", []>;
    defm AMDGPUSample : AMDGPUSampleHelper_Clamp<"_CD", "_cd", []>;
  }
}

// Helper class to capture the profile of a dimension-aware image intrinsic.
// This information is used to generate the intrinsic's type and to inform
// codegen pattern matching.
class AMDGPUDimProfile<string opmod,
                       AMDGPUDimProps dim> {
  AMDGPUDimProps Dim = dim;
  string OpMod = opmod; // the corresponding instruction is named IMAGE_OpMod

  // These are intended to be overwritten by subclasses
  bit IsSample = false;
  bit IsAtomic = false;
  list<LLVMType> RetTypes = [];
  list<AMDGPUArg> DataArgs = [];
  list<AMDGPUArg> ExtraAddrArgs = [];
  bit Offset = false;
  bit Bias = false;
  bit ZCompare = false;
  bit Gradients = false;
  string LodClampMip = "";

  int NumRetAndDataAnyTypes =
    !foldl(0, !listconcat(RetTypes, !foreach(arg, DataArgs, arg.Type)), a, b,
           !add(a, b.isAny));

  list<AMDGPUArg> AddrArgs =
    arglistconcat<[ExtraAddrArgs,
                   !if(Gradients, dim.GradientArgs, []),
                   !listconcat(!if(IsSample, dim.CoordSliceArgs, dim.CoordSliceIntArgs),
                               !if(!empty(LodClampMip),
                                   []<AMDGPUArg>,
                                   [AMDGPUArg<LLVMMatchType<0>, LodClampMip>]))],
                  NumRetAndDataAnyTypes>.ret;
  list<LLVMType> AddrTypes = !foreach(arg, AddrArgs, arg.Type);
  list<AMDGPUArg> AddrDefaultArgs =
    !foreach(arg, AddrArgs,
             AMDGPUArg<!if(!or(arg.Type.isAny, !isa<LLVMMatchType>(arg.Type)),
                           !if(IsSample, llvm_float_ty, llvm_i32_ty), arg.Type),
                       arg.Name>);
  list<AMDGPUArg> AddrA16Args =
    !foreach(arg, AddrArgs,
             AMDGPUArg<!if(!or(arg.Type.isAny, !isa<LLVMMatchType>(arg.Type)),
                           !if(IsSample, llvm_half_ty, llvm_i16_ty), arg.Type),
                       arg.Name>);
}

class AMDGPUDimProfileCopy<AMDGPUDimProfile base> : AMDGPUDimProfile<base.OpMod, base.Dim> {
  let IsSample = base.IsSample;
  let IsAtomic = base.IsAtomic;
  let RetTypes = base.RetTypes;
  let DataArgs = base.DataArgs;
  let ExtraAddrArgs = base.ExtraAddrArgs;
  let Offset = base.Offset;
  let Bias = base.Bias;
  let ZCompare = base.ZCompare;
  let Gradients = base.Gradients;
  let LodClampMip = base.LodClampMip;
}

class AMDGPUDimSampleProfile<string opmod,
                             AMDGPUDimProps dim,
                             AMDGPUSampleVariant sample,
                             bit has_return = true> : AMDGPUDimProfile<opmod, dim> {
  let IsSample = true;
  let RetTypes = !if(has_return, [llvm_any_ty], []);
  let ExtraAddrArgs = sample.ExtraAddrArgs;
  let Offset = sample.Offset;
  let Bias = sample.Bias;
  let ZCompare = sample.ZCompare;
  let Gradients = sample.Gradients;
  let LodClampMip = sample.LodOrClamp;
}

class AMDGPUDimSampleNoReturnProfile<string opmod,
                             AMDGPUDimProps dim,
                             AMDGPUSampleVariant sample>
    : AMDGPUDimSampleProfile<opmod, dim, sample, false> {
}

class AMDGPUDimNoSampleProfile<string opmod,
                               AMDGPUDimProps dim,
                               list<LLVMType> retty,
                               list<AMDGPUArg> dataargs,
                               bit Mip = false> : AMDGPUDimProfile<opmod, dim> {
  let RetTypes = retty;
  let DataArgs = dataargs;
  let LodClampMip = !if(Mip, "mip", "");
}

class AMDGPUDimAtomicProfile<string opmod,
                             AMDGPUDimProps dim,
                             list<AMDGPUArg> dataargs> : AMDGPUDimProfile<opmod, dim> {
  let RetTypes = [llvm_anyint_ty];
  let DataArgs = dataargs;
  let IsAtomic = true;
}

class AMDGPUDimAtomicFloatProfile<string opmod, AMDGPUDimProps dim,
                                  list<AMDGPUArg> dataargs>
    : AMDGPUDimAtomicProfile<opmod, dim, dataargs> {
  let RetTypes = [llvm_anyfloat_ty];
}

class AMDGPUDimGetResInfoProfile<AMDGPUDimProps dim>
    : AMDGPUDimProfile<"GET_RESINFO", dim> {
  let RetTypes = [llvm_anyfloat_ty];
  let DataArgs = [];
  let AddrArgs = [AMDGPUArg<llvm_anyint_ty, "mip">];
  let LodClampMip = "mip";
}

// Helper class for figuring out image intrinsic argument indexes.
class AMDGPUImageDimIntrinsicEval<AMDGPUDimProfile P_> {
  int NumDataArgs = !size(P_.DataArgs);
  int NumDmaskArgs = !not(P_.IsAtomic);
  int NumOffsetArgs = !if(P_.Offset, 1, 0);
  int NumBiasArgs = !if(P_.Bias, 1, 0);
  int NumZCompareArgs = !if(P_.ZCompare, 1, 0);
  int NumExtraAddrArgs = !add(NumOffsetArgs, NumBiasArgs, NumZCompareArgs);
  int NumVAddrArgs = !size(P_.AddrArgs);
  int NumGradientArgs = !if(P_.Gradients, !size(P_.Dim.GradientArgs), 0);
  int NumCoordArgs = !if(P_.IsSample, !size(P_.Dim.CoordSliceArgs), !size(P_.Dim.CoordSliceIntArgs));
  int NumRSrcArgs = 1;
  int NumSampArgs = !if(P_.IsSample, 2, 0);
  int DmaskArgIndex = NumDataArgs;
  int VAddrArgIndex = !add(DmaskArgIndex, NumDmaskArgs);
  int OffsetArgIndex = VAddrArgIndex;
  int BiasArgIndex = !add(VAddrArgIndex, NumOffsetArgs);
  int ZCompareArgIndex = !add(BiasArgIndex, NumBiasArgs);
  int GradientArgIndex = !add(VAddrArgIndex, NumExtraAddrArgs);
  int CoordArgIndex = !add(GradientArgIndex, NumGradientArgs);
  int LodArgIndex = !add(VAddrArgIndex, NumVAddrArgs, -1);
  int MipArgIndex = LodArgIndex;
  int RsrcArgIndex = !add(VAddrArgIndex, NumVAddrArgs);
  int SampArgIndex = !add(RsrcArgIndex, NumRSrcArgs);
  int UnormArgIndex = !add(SampArgIndex, 1);
  int TexFailCtrlArgIndex = !add(SampArgIndex, NumSampArgs);
  int CachePolicyArgIndex = !add(TexFailCtrlArgIndex, 1);
}

// All dimension-aware intrinsics are derived from this class.
class AMDGPUImageDimIntrinsic<AMDGPUDimProfile P_,
                              list<IntrinsicProperty> props,
                              list<SDNodeProperty> sdnodeprops> : Intrinsic<
    P_.RetTypes,        // vdata(VGPR) -- for load/atomic-with-return
    !listconcat(
      !foreach(arg, P_.DataArgs, arg.Type),    // vdata(VGPR) -- for store/atomic
      !if(P_.IsAtomic, [], [llvm_i32_ty]),     // dmask(imm)
      P_.AddrTypes,                            // vaddr(VGPR)
      [llvm_v8i32_ty],                         // rsrc(SGPR)
      !if(P_.IsSample, [llvm_v4i32_ty,         // samp(SGPR)
                        llvm_i1_ty], []),      // unorm(imm)
      [llvm_i32_ty,                            // texfailctrl(imm; bit 0 = tfe, bit 1 = lwe)
       llvm_i32_ty]),                          // auxiliary/cachepolicy(imm):
                                               //                bit 0 = glc, bit 1 = slc,
                                               //                bit 2 = dlc (gfx10/gfx11),
                                               //                bit 4 = scc (gfx90a)
                                               //        gfx940: bit 0 = sc0, bit 1 = nt, bit 4 = sc1
                                               //        gfx12+: bits [0-2] = th, bits [3-4] = scope
     !listconcat(props, [IntrNoCallback, IntrNoFree, IntrWillReturn],
          !if(P_.IsAtomic, [], [ImmArg<ArgIndex<AMDGPUImageDimIntrinsicEval<P_>.DmaskArgIndex>>]),
          !if(P_.IsSample, [ImmArg<ArgIndex<AMDGPUImageDimIntrinsicEval<P_>.UnormArgIndex>>], []),
          [ImmArg<ArgIndex<AMDGPUImageDimIntrinsicEval<P_>.TexFailCtrlArgIndex>>,
           ImmArg<ArgIndex<AMDGPUImageDimIntrinsicEval<P_>.CachePolicyArgIndex>>],
          !if(P_.IsAtomic, [], [IntrNoSync])),


      "", sdnodeprops>,
  AMDGPURsrcIntrinsic<!add(!size(P_.DataArgs), !size(P_.AddrTypes),
                           !if(P_.IsAtomic, 0, 1)), 1> {
  AMDGPUDimProfile P = P_;

  AMDGPUImageDimIntrinsic Intr = !cast<AMDGPUImageDimIntrinsic>(NAME);

  let TargetPrefix = "amdgcn";
}

// Marker class for intrinsics with a DMask that determines the returned
// channels.
class AMDGPUImageDMaskIntrinsic;

defset list<AMDGPUImageDimIntrinsic> AMDGPUImageDimIntrinsics = {

  //////////////////////////////////////////////////////////////////////////
  // Load and store intrinsics
  //////////////////////////////////////////////////////////////////////////
  multiclass AMDGPUImageDimIntrinsicsNoMsaa<string opmod,
                                            list<LLVMType> retty,
                                            list<AMDGPUArg> dataargs,
                                            list<IntrinsicProperty> props,
                                            list<SDNodeProperty> sdnodeprops,
                                            bit Mip = false> {
    foreach dim = AMDGPUDims.NoMsaa in {
      def !strconcat(NAME, "_", dim.Name)
        : AMDGPUImageDimIntrinsic<
            AMDGPUDimNoSampleProfile<opmod, dim, retty, dataargs, Mip>,
            props, sdnodeprops>;
    }
  }

  multiclass AMDGPUImageDimIntrinsicsAll<string opmod,
                                         list<LLVMType> retty,
                                         list<AMDGPUArg> dataargs,
                                         list<IntrinsicProperty> props,
                                         list<SDNodeProperty> sdnodeprops,
                                         bit Mip = false> {
    foreach dim = AMDGPUDims.All in {
      def !strconcat(NAME, "_", dim.Name)
        : AMDGPUImageDimIntrinsic<
            AMDGPUDimNoSampleProfile<opmod, dim, retty, dataargs, Mip>,
            props, sdnodeprops>;
    }
  }

  defm int_amdgcn_image_load
    : AMDGPUImageDimIntrinsicsAll<"LOAD", [llvm_any_ty], [], [IntrReadMem],
                                  [SDNPMemOperand]>,
      AMDGPUImageDMaskIntrinsic;
  defm int_amdgcn_image_load_mip
    : AMDGPUImageDimIntrinsicsNoMsaa<"LOAD_MIP", [llvm_any_ty], [],
                                     [IntrReadMem, IntrWillReturn], [SDNPMemOperand], 1>,
      AMDGPUImageDMaskIntrinsic;

  defm int_amdgcn_image_store : AMDGPUImageDimIntrinsicsAll<
              "STORE", [], [AMDGPUArg<llvm_anyfloat_ty, "vdata">],
              [IntrWriteMem, IntrWillReturn], [SDNPMemOperand]>,
              AMDGPUImageDMaskIntrinsic;
  defm int_amdgcn_image_store_mip : AMDGPUImageDimIntrinsicsNoMsaa<
              "STORE_MIP", [], [AMDGPUArg<llvm_anyfloat_ty, "vdata">],
              [IntrWriteMem, IntrWillReturn], [SDNPMemOperand], 1>,
              AMDGPUImageDMaskIntrinsic;

  //////////////////////////////////////////////////////////////////////////
  // MSAA intrinsics
  //////////////////////////////////////////////////////////////////////////
  foreach dim = AMDGPUDims.Msaa in {
    def int_amdgcn_image_msaa_load_x # _ # dim.Name:
        AMDGPUImageDimIntrinsic<
            AMDGPUDimNoSampleProfile<"MSAA_LOAD_X", dim, [llvm_any_ty], []>,
            [IntrReadMem], [SDNPMemOperand]>;
  }

  foreach dim = AMDGPUDims.Msaa in {
    def int_amdgcn_image_msaa_load # _ # dim.Name:
        AMDGPUImageDimIntrinsic<
            AMDGPUDimNoSampleProfile<"MSAA_LOAD", dim, [llvm_any_ty], []>,
            [IntrReadMem], [SDNPMemOperand]>;
  }

  //////////////////////////////////////////////////////////////////////////
  // sample and getlod intrinsics
  //////////////////////////////////////////////////////////////////////////
  multiclass AMDGPUImageDimSampleDims<string opmod,
                                      AMDGPUSampleVariant sample,
                                      bit NoMem = false> {
    foreach dim = AMDGPUDims.NoMsaa in {
      def !strconcat(NAME, "_", dim.Name) : AMDGPUImageDimIntrinsic<
          AMDGPUDimSampleProfile<opmod, dim, sample>,
          !if(NoMem, [IntrNoMem], [IntrReadMem]),
          !if(NoMem, [], [SDNPMemOperand])>;
    }
  }

  foreach sample = AMDGPUSampleVariants in {
    defm int_amdgcn_image_sample # sample.LowerCaseMod
      : AMDGPUImageDimSampleDims<"SAMPLE" # sample.UpperCaseMod, sample>,
        AMDGPUImageDMaskIntrinsic;
  }

  multiclass AMDGPUImageDimSampleNoReturnDims<string opmod,
                                      AMDGPUSampleVariant sample> {
    foreach dim = AMDGPUDims.NoMsaa in {
      def !strconcat(NAME, "_", dim.Name, "_nortn") : AMDGPUImageDimIntrinsic<
          AMDGPUDimSampleNoReturnProfile<opmod, dim, sample>,
          [IntrWillReturn], [SDNPMemOperand]>;
    }
  }
  foreach sample = AMDGPUSampleVariants in {
    defm int_amdgcn_image_sample # sample.LowerCaseMod
      : AMDGPUImageDimSampleNoReturnDims<
        "SAMPLE" # sample.UpperCaseMod # "_nortn", sample>,
        AMDGPUImageDMaskIntrinsic;
  }

  defm int_amdgcn_image_getlod
    : AMDGPUImageDimSampleDims<"GET_LOD", AMDGPUSample, 1>,
      AMDGPUImageDMaskIntrinsic;

  //////////////////////////////////////////////////////////////////////////
  // getresinfo intrinsics
  //////////////////////////////////////////////////////////////////////////
  foreach dim = AMDGPUDims.All in {
    def !strconcat("int_amdgcn_image_getresinfo_", dim.Name)
      : AMDGPUImageDimIntrinsic<AMDGPUDimGetResInfoProfile<dim>, [IntrNoMem], []>,
        AMDGPUImageDMaskIntrinsic;
  }

  //////////////////////////////////////////////////////////////////////////
  // gather4 intrinsics
  //////////////////////////////////////////////////////////////////////////
  foreach sample = AMDGPUSampleVariantsNoGradients in {
    foreach dim = [AMDGPUDim2D, AMDGPUDimCube, AMDGPUDim2DArray] in {
      def int_amdgcn_image_gather4 # sample.LowerCaseMod # _ # dim.Name:
          AMDGPUImageDimIntrinsic<
              AMDGPUDimSampleProfile<"GATHER4" # sample.UpperCaseMod, dim, sample>,
              [IntrReadMem], [SDNPMemOperand]>;
    }
  }
}

//////////////////////////////////////////////////////////////////////////
// atomic intrinsics
//////////////////////////////////////////////////////////////////////////
defset list<AMDGPUImageDimIntrinsic> AMDGPUImageDimAtomicIntrinsics = {
  multiclass AMDGPUImageDimAtomicX<string opmod, list<AMDGPUArg> dataargs,
                                   int isFloat = 0> {
        foreach dim = AMDGPUDims.All in {
          def !strconcat(NAME, "_", dim.Name): AMDGPUImageDimIntrinsic<
              !if (isFloat, AMDGPUDimAtomicFloatProfile<opmod, dim, dataargs>,
                   AMDGPUDimAtomicProfile<opmod, dim, dataargs>),
              [], [SDNPMemOperand]>;
        }
  }

  multiclass AMDGPUImageDimAtomic<string opmod, int isFloat = 0> {
    defm ""
        : AMDGPUImageDimAtomicX<opmod, [AMDGPUArg<LLVMMatchType<0>, "vdata">],
                                isFloat>;
  }

  multiclass AMDGPUImageDimFloatAtomic<string opmod> {
    defm "" : AMDGPUImageDimAtomic<opmod, 1 /*isFloat*/>;
  }

  defm int_amdgcn_image_atomic_swap : AMDGPUImageDimAtomic<"ATOMIC_SWAP">;
  defm int_amdgcn_image_atomic_add : AMDGPUImageDimAtomic<"ATOMIC_ADD">;
  defm int_amdgcn_image_atomic_sub : AMDGPUImageDimAtomic<"ATOMIC_SUB">;
  defm int_amdgcn_image_atomic_smin : AMDGPUImageDimAtomic<"ATOMIC_SMIN">;
  defm int_amdgcn_image_atomic_umin : AMDGPUImageDimAtomic<"ATOMIC_UMIN">;
  defm int_amdgcn_image_atomic_fmin : AMDGPUImageDimFloatAtomic<"ATOMIC_FMIN">;
  defm int_amdgcn_image_atomic_smax : AMDGPUImageDimAtomic<"ATOMIC_SMAX">;
  defm int_amdgcn_image_atomic_umax : AMDGPUImageDimAtomic<"ATOMIC_UMAX">;
  defm int_amdgcn_image_atomic_fmax : AMDGPUImageDimFloatAtomic<"ATOMIC_FMAX">;
  defm int_amdgcn_image_atomic_and : AMDGPUImageDimAtomic<"ATOMIC_AND">;
  defm int_amdgcn_image_atomic_or : AMDGPUImageDimAtomic<"ATOMIC_OR">;
  defm int_amdgcn_image_atomic_xor : AMDGPUImageDimAtomic<"ATOMIC_XOR">;
  defm int_amdgcn_image_atomic_inc : AMDGPUImageDimAtomic<"ATOMIC_INC">;
  defm int_amdgcn_image_atomic_dec : AMDGPUImageDimAtomic<"ATOMIC_DEC">;
  defm int_amdgcn_image_atomic_add_flt : AMDGPUImageDimFloatAtomic<"ATOMIC_ADD_FLT">;
  defm int_amdgcn_image_atomic_min_flt : AMDGPUImageDimFloatAtomic<"ATOMIC_MIN_FLT">;
  defm int_amdgcn_image_atomic_max_flt : AMDGPUImageDimFloatAtomic<"ATOMIC_MAX_FLT">;

  defm int_amdgcn_image_atomic_cmpswap :
      AMDGPUImageDimAtomicX<"ATOMIC_CMPSWAP", [AMDGPUArg<LLVMMatchType<0>, "src">,
                                               AMDGPUArg<LLVMMatchType<0>, "cmp">]>;

  defm int_amdgcn_image_atomic_pk_add_f16 : AMDGPUImageDimFloatAtomic<"ATOMIC_PK_ADD_F16">;
  defm int_amdgcn_image_atomic_pk_add_bf16 : AMDGPUImageDimFloatAtomic<"ATOMIC_PK_ADD_BF16">;
}

//////////////////////////////////////////////////////////////////////////
// Buffer intrinsics
//////////////////////////////////////////////////////////////////////////

// Data type for buffer resources (V#). Maybe, in the future, we can create a
// similar one for textures (T#).
def AMDGPUBufferRsrcTy : LLVMQualPointerType<8>;

let TargetPrefix = "amdgcn" in {

def int_amdgcn_make_buffer_rsrc : DefaultAttrsIntrinsic <
  [AMDGPUBufferRsrcTy],
  [llvm_anyptr_ty, // base
   llvm_i16_ty,    // stride (and swizzle control)
   llvm_i32_ty,    // NumRecords / extent
   llvm_i32_ty],   // flags
  // Attributes lifted from ptrmask + some extra argument attributes.
  [IntrNoMem, ReadNone<ArgIndex<0>>,
   IntrSpeculatable, IntrWillReturn]>;

defset list<AMDGPURsrcIntrinsic> AMDGPUBufferIntrinsics = {

// Generate a buffer_load instruction that may be optimized to s_buffer_load if
// the offset argument is uniform.
def int_amdgcn_s_buffer_load : DefaultAttrsIntrinsic <
  [llvm_any_ty],
  [llvm_v4i32_ty,    // rsrc(SGPR)
   llvm_i32_ty,      // byte offset
   llvm_i32_ty],     // auxiliary/cachepolicy(imm):
                     //                bit 0 = glc, bit 1 = slc, bit 2 = dlc (gfx10/gfx11),
                     //                bit 3 = swz, bit 4 = scc (gfx90a)
                     //        gfx940: bit 0 = sc0, bit 1 = nt, bit 3 = swz, bit 4 = sc1
                     //        gfx12+: bits [0-2] = th, bits [3-4] = scope,
                     //                bit 6 = swz
                     // Note: volatile bit is **not** permitted here.
  [IntrNoMem, ImmArg<ArgIndex<2>>]>,
  AMDGPURsrcIntrinsic<0>;

// Buffer intrinsics with separate raw and struct variants.  The raw
// variant never has an index. The struct variant always has an index, even if
// it is const 0. A struct intrinsic with constant 0 index is different to the
// corresponding raw intrinsic on gfx9+ because the behavior of bound checking
// and swizzling changes depending on whether idxen is set in the instruction.
// These instrinsics also keep the offset and soffset arguments separate as
// they behave differently in bounds checking and swizzling.

// The versions of these intrinsics that take <4 x i32> arguments are deprecated
// in favor of their .ptr.buffer variants that take ptr addrspace(8) arguments,
// which allow for improved reasoning about memory accesses.
//
// Note that in the cachepolicy for all these intrinsics, bit 31 is not preserved
// through to final assembly selection and is used to signal that the buffer
// operation is volatile.
class AMDGPURawBufferLoad<LLVMType data_ty = llvm_any_ty> : DefaultAttrsIntrinsic <
  [data_ty],
  [llvm_v4i32_ty,    // rsrc(SGPR)
   llvm_i32_ty,      // offset(VGPR/imm, included in bounds checking and swizzling)
   llvm_i32_ty,      // soffset(SGPR/imm, excluded from bounds checking and swizzling)
   llvm_i32_ty],     // auxiliary/cachepolicy(imm):
                     //                bit 0 = glc, bit 1 = slc, bit 2 = dlc (gfx10/gfx11),
                     //                bit 3 = swz, bit 4 = scc (gfx90a)
                     //        gfx940: bit 0 = sc0, bit 1 = nt, bit 3 = swz, bit 4 = sc1
                     //        gfx12+: bits [0-2] = th, bits [3-4] = scope,
                     //                bit 6 = swz
                     //           all: volatile op (bit 31, stripped at lowering)
  [IntrReadMem, ImmArg<ArgIndex<3>>], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<0>;
def int_amdgcn_raw_buffer_load_format : AMDGPURawBufferLoad<llvm_anyfloat_ty>;
def int_amdgcn_raw_buffer_load : AMDGPURawBufferLoad;

class AMDGPURawAtomicBufferLoad<LLVMType data_ty = llvm_any_ty> : Intrinsic <
  [data_ty],
  [llvm_v4i32_ty,     // rsrc(SGPR)
   llvm_i32_ty,       // offset(VGPR/imm, included in bounds checking and swizzling)
   llvm_i32_ty,       // soffset(SGPR/imm, excluded from bounds checking and swizzling)
   llvm_i32_ty],      // auxiliary data (imm, cachepolicy     (bit 0 = glc,
                      //                                       bit 1 = slc,
                      //                                       bit 2 = dlc on gfx10+),
                      //                      swizzled buffer (bit 3 = swz))
  [ImmArg<ArgIndex<3>>, IntrWillReturn, IntrNoCallback, IntrNoFree], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<0>;
def int_amdgcn_raw_atomic_buffer_load : AMDGPURawAtomicBufferLoad;

class AMDGPURawPtrBufferLoad<LLVMType data_ty = llvm_any_ty> : DefaultAttrsIntrinsic <
  [data_ty],
  [AMDGPUBufferRsrcTy,    // rsrc(SGPR)
   llvm_i32_ty,           // offset(VGPR/imm, included in bounds checking and swizzling)
   llvm_i32_ty,           // soffset(SGPR/imm, excluded from bounds checking and swizzling)
   llvm_i32_ty],          // auxiliary/cachepolicy(imm):
                          //                bit 0 = glc, bit 1 = slc, bit 2 = dlc (gfx10/gfx11),
                          //                bit 3 = swz, bit 4 = scc (gfx90a)
                          //        gfx940: bit 0 = sc0, bit 1 = nt, bit 3 = swz, bit 4 = sc1
                          //        gfx12+: bits [0-2] = th, bits [3-4] = scope,
                          //                bit 6 = swz
                          //           all: volatile op (bit 31, stripped at lowering)
  [IntrArgMemOnly, IntrReadMem, ReadOnly<ArgIndex<0>>, NoCapture<ArgIndex<0>>,
  ImmArg<ArgIndex<3>>], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<0>;
def int_amdgcn_raw_ptr_buffer_load_format : AMDGPURawPtrBufferLoad<llvm_anyfloat_ty>;
def int_amdgcn_raw_ptr_buffer_load : AMDGPURawPtrBufferLoad;

class AMDGPURawPtrAtomicBufferLoad<LLVMType data_ty = llvm_any_ty> : Intrinsic <
  [data_ty],
  [AMDGPUBufferRsrcTy,// rsrc(SGPR)
   llvm_i32_ty,       // offset(VGPR/imm, included in bounds checking and swizzling)
   llvm_i32_ty,       // soffset(SGPR/imm, excluded from bounds checking and swizzling)
   llvm_i32_ty],      // auxiliary data (imm, cachepolicy     (bit 0 = glc,
                      //                                       bit 1 = slc,
                      //                                       bit 2 = dlc on gfx10+),
                      //                      swizzled buffer (bit 3 = swz))
  [IntrArgMemOnly, NoCapture<ArgIndex<0>>, ImmArg<ArgIndex<3>>, IntrWillReturn, IntrNoCallback, IntrNoFree], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<0>;
def int_amdgcn_raw_ptr_atomic_buffer_load : AMDGPURawPtrAtomicBufferLoad;

class AMDGPUStructBufferLoad<LLVMType data_ty = llvm_any_ty> : DefaultAttrsIntrinsic <
  [data_ty],
  [llvm_v4i32_ty,    // rsrc(SGPR)
   llvm_i32_ty,      // vindex(VGPR)
   llvm_i32_ty,      // offset(VGPR/imm, included in bounds checking and swizzling)
   llvm_i32_ty,      // soffset(SGPR/imm, excluded from bounds checking and swizzling)
   llvm_i32_ty],     // auxiliary/cachepolicy(imm):
                     //                bit 0 = glc, bit 1 = slc, bit 2 = dlc (gfx10/gfx11),
                     //                bit 3 = swz, bit 4 = scc (gfx90a)
                     //        gfx940: bit 0 = sc0, bit 1 = nt, bit 3 = swz, bit 4 = sc1
                     //        gfx12+: bits [0-2] = th, bits [3-4] = scope,
                     //                bit 6 = swz
                     //           all: volatile op (bit 31, stripped at lowering)
  [IntrReadMem, ImmArg<ArgIndex<4>>], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<0>;
def int_amdgcn_struct_buffer_load_format : AMDGPUStructBufferLoad;
def int_amdgcn_struct_buffer_load : AMDGPUStructBufferLoad;

class AMDGPUStructPtrBufferLoad<LLVMType data_ty = llvm_any_ty> : DefaultAttrsIntrinsic <
  [data_ty],
  [AMDGPUBufferRsrcTy,    // rsrc(SGPR)
   llvm_i32_ty,           // vindex(VGPR)
   llvm_i32_ty,           // offset(VGPR/imm, included in bounds checking and swizzling)
   llvm_i32_ty,           // soffset(SGPR/imm, excluded from bounds checking and swizzling)
   llvm_i32_ty],          // auxiliary/cachepolicy(imm):
                          //                bit 0 = glc, bit 1 = slc, bit 2 = dlc (gfx10/gfx11),
                          //                bit 3 = swz, bit 4 = scc (gfx90a)
                          //        gfx940: bit 0 = sc0, bit 1 = nt, bit 3 = swz, bit 4 = sc1
                          //        gfx12+: bits [0-2] = th, bits [3-4] = scope,
                          //                bit 6 = swz
                          //           all: volatile op (bit 31, stripped at lowering)
  [IntrArgMemOnly, IntrReadMem, ReadOnly<ArgIndex<0>>, NoCapture<ArgIndex<0>>,
   ImmArg<ArgIndex<4>>], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<0>;
def int_amdgcn_struct_ptr_buffer_load_format : AMDGPUStructPtrBufferLoad;
def int_amdgcn_struct_ptr_buffer_load : AMDGPUStructPtrBufferLoad;

class AMDGPURawBufferStore<LLVMType data_ty = llvm_any_ty> : DefaultAttrsIntrinsic <
  [],
  [data_ty,          // vdata(VGPR)
   llvm_v4i32_ty,    // rsrc(SGPR)
   llvm_i32_ty,      // offset(VGPR/imm, included in bounds checking and swizzling)
   llvm_i32_ty,      // soffset(SGPR/imm, excluded from bounds checking and swizzling)
   llvm_i32_ty],     // auxiliary/cachepolicy(imm):
                     //                bit 0 = glc, bit 1 = slc, bit 2 = dlc (gfx10/gfx11),
                     //                bit 3 = swz, bit 4 = scc (gfx90a)
                     //        gfx940: bit 0 = sc0, bit 1 = nt, bit 3 = swz, bit 4 = sc1
                     //        gfx12+: bits [0-2] = th, bits [3-4] = scope,
                     //                bit 6 = swz
                     //           all: volatile op (bit 31, stripped at lowering)
  [IntrWriteMem, ImmArg<ArgIndex<4>>], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<1>;
def int_amdgcn_raw_buffer_store_format : AMDGPURawBufferStore<llvm_anyfloat_ty>;
def int_amdgcn_raw_buffer_store : AMDGPURawBufferStore;

class AMDGPURawPtrBufferStore<LLVMType data_ty = llvm_any_ty> : DefaultAttrsIntrinsic <
  [],
  [data_ty,               // vdata(VGPR)
   AMDGPUBufferRsrcTy,    // rsrc(SGPR)
   llvm_i32_ty,           // offset(VGPR/imm, included in bounds checking and swizzling)
   llvm_i32_ty,           // soffset(SGPR/imm, excluded from bounds checking and swizzling)
   llvm_i32_ty],          // auxiliary/cachepolicy(imm):
                          //                bit 0 = glc, bit 1 = slc, bit 2 = dlc (gfx10/gfx11),
                          //                bit 3 = swz, bit 4 = scc (gfx90a)
                          //        gfx940: bit 0 = sc0, bit 1 = nt, bit 3 = swz, bit 4 = sc1
                          //        gfx12+: bits [0-2] = th, bits [3-4] = scope,
                          //                bit 6 = swz
                          //           all: volatile op (bit 31, stripped at lowering)
  [IntrArgMemOnly, IntrWriteMem, WriteOnly<ArgIndex<1>>, NoCapture<ArgIndex<1>>,
  ImmArg<ArgIndex<4>>], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<1>;
def int_amdgcn_raw_ptr_buffer_store_format : AMDGPURawPtrBufferStore<llvm_anyfloat_ty>;
def int_amdgcn_raw_ptr_buffer_store : AMDGPURawPtrBufferStore;

class AMDGPUStructBufferStore<LLVMType data_ty = llvm_any_ty> : DefaultAttrsIntrinsic <
  [],
  [data_ty,          // vdata(VGPR)
   llvm_v4i32_ty,    // rsrc(SGPR)
   llvm_i32_ty,      // vindex(VGPR)
   llvm_i32_ty,      // offset(VGPR/imm, included in bounds checking and swizzling)
   llvm_i32_ty,      // soffset(SGPR/imm, excluded from bounds checking and swizzling)
   llvm_i32_ty],     // auxiliary/cachepolicy(imm):
                     //                bit 0 = glc, bit 1 = slc, bit 2 = dlc (gfx10/gfx11),
                     //                bit 3 = swz, bit 4 = scc (gfx90a)
                     //        gfx940: bit 0 = sc0, bit 1 = nt, bit 3 = swz, bit 4 = sc1
                     //        gfx12+: bits [0-2] = th, bits [3-4] = scope,
                     //                bit 6 = swz
                     //           all: volatile op (bit 31, stripped at lowering)
  [IntrWriteMem, ImmArg<ArgIndex<5>>], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<1>;
def int_amdgcn_struct_buffer_store_format : AMDGPUStructBufferStore;
def int_amdgcn_struct_buffer_store : AMDGPUStructBufferStore;

class AMDGPUStructPtrBufferStore<LLVMType data_ty = llvm_any_ty> : DefaultAttrsIntrinsic <
  [],
  [data_ty,               // vdata(VGPR)
   AMDGPUBufferRsrcTy,    // rsrc(SGPR)
   llvm_i32_ty,           // vindex(VGPR)
   llvm_i32_ty,           // offset(VGPR/imm, included in bounds checking and swizzling)
   llvm_i32_ty,           // soffset(SGPR/imm, excluded from bounds checking and swizzling)
   llvm_i32_ty],          // auxiliary/cachepolicy(imm):
                          //                bit 0 = glc, bit 1 = slc, bit 2 = dlc (gfx10/gfx11),
                          //                bit 3 = swz, bit 4 = scc (gfx90a)
                          //        gfx940: bit 0 = sc0, bit 1 = nt, bit 3 = swz, bit 4 = sc1
                          //        gfx12+: bits [0-2] = th, bits [3-4] = scope,
                          //                bit 6 = swz
                          //           all: volatile op (bit 31, stripped at lowering)
  [IntrArgMemOnly, IntrWriteMem, WriteOnly<ArgIndex<1>>, NoCapture<ArgIndex<1>>,
   ImmArg<ArgIndex<5>>], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<1>;
def int_amdgcn_struct_ptr_buffer_store_format : AMDGPUStructPtrBufferStore;
def int_amdgcn_struct_ptr_buffer_store : AMDGPUStructPtrBufferStore;

class AMDGPURawBufferAtomic<LLVMType data_ty = llvm_any_ty> : Intrinsic <
  [data_ty],
  [LLVMMatchType<0>,  // vdata(VGPR)
   llvm_v4i32_ty,     // rsrc(SGPR)
   llvm_i32_ty,       // offset(VGPR/imm, included in bounds checking and swizzling)
   llvm_i32_ty,       // soffset(SGPR/imm, excluded from bounds checking and swizzling)
   llvm_i32_ty],      // cachepolicy(imm; bit 1 = slc, ..., bit 31 = volatile)
  [ImmArg<ArgIndex<4>>, IntrWillReturn, IntrNoCallback, IntrNoFree], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<1, 0>;
def int_amdgcn_raw_buffer_atomic_swap : AMDGPURawBufferAtomic;
def int_amdgcn_raw_buffer_atomic_add : AMDGPURawBufferAtomic;
def int_amdgcn_raw_buffer_atomic_sub : AMDGPURawBufferAtomic;
def int_amdgcn_raw_buffer_atomic_smin : AMDGPURawBufferAtomic;
def int_amdgcn_raw_buffer_atomic_umin : AMDGPURawBufferAtomic;
def int_amdgcn_raw_buffer_atomic_fmin : AMDGPURawBufferAtomic<llvm_anyfloat_ty>;
def int_amdgcn_raw_buffer_atomic_smax : AMDGPURawBufferAtomic;
def int_amdgcn_raw_buffer_atomic_umax : AMDGPURawBufferAtomic;
def int_amdgcn_raw_buffer_atomic_fmax : AMDGPURawBufferAtomic<llvm_anyfloat_ty>;
def int_amdgcn_raw_buffer_atomic_and : AMDGPURawBufferAtomic;
def int_amdgcn_raw_buffer_atomic_or : AMDGPURawBufferAtomic;
def int_amdgcn_raw_buffer_atomic_xor : AMDGPURawBufferAtomic;
def int_amdgcn_raw_buffer_atomic_inc : AMDGPURawBufferAtomic;
def int_amdgcn_raw_buffer_atomic_dec : AMDGPURawBufferAtomic;
def int_amdgcn_raw_buffer_atomic_cond_sub_u32 : AMDGPURawBufferAtomic;
def int_amdgcn_raw_buffer_atomic_cmpswap : Intrinsic<
  [llvm_anyint_ty],
  [LLVMMatchType<0>,  // src(VGPR)
   LLVMMatchType<0>,  // cmp(VGPR)
   llvm_v4i32_ty,     // rsrc(SGPR)
   llvm_i32_ty,       // offset(VGPR/imm, included in bounds checking and swizzling)
   llvm_i32_ty,       // soffset(SGPR/imm, excluded from bounds checking and swizzling)
   llvm_i32_ty],      // cachepolicy(imm; bit 1 = slc, ..., bit 31 = volatile)
  [ImmArg<ArgIndex<5>>, IntrWillReturn, IntrNoCallback, IntrNoFree], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<2, 0>;

class AMDGPURawPtrBufferAtomic<LLVMType data_ty = llvm_any_ty> : Intrinsic <
  [data_ty],
  [LLVMMatchType<0>,            // vdata(VGPR)
   AMDGPUBufferRsrcTy,          // rsrc(SGPR)
   llvm_i32_ty,                 // offset(VGPR/imm, included in bounds checking and swizzling)
   llvm_i32_ty,                 // soffset(SGPR/imm, excluded from bounds checking and swizzling)
   llvm_i32_ty],                // cachepolicy(imm; bit 1 = slc, ..., bit 31 = volatile)
  [IntrArgMemOnly, NoCapture<ArgIndex<1>>,
   ImmArg<ArgIndex<4>>, IntrWillReturn, IntrNoCallback, IntrNoFree], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<1, 0>;

def int_amdgcn_raw_ptr_buffer_atomic_swap : AMDGPURawPtrBufferAtomic;
def int_amdgcn_raw_ptr_buffer_atomic_add : AMDGPURawPtrBufferAtomic;
def int_amdgcn_raw_ptr_buffer_atomic_sub : AMDGPURawPtrBufferAtomic;
def int_amdgcn_raw_ptr_buffer_atomic_smin : AMDGPURawPtrBufferAtomic;
def int_amdgcn_raw_ptr_buffer_atomic_umin : AMDGPURawPtrBufferAtomic;
def int_amdgcn_raw_ptr_buffer_atomic_fmin : AMDGPURawPtrBufferAtomic<llvm_anyfloat_ty>;
def int_amdgcn_raw_ptr_buffer_atomic_smax : AMDGPURawPtrBufferAtomic;
def int_amdgcn_raw_ptr_buffer_atomic_umax : AMDGPURawPtrBufferAtomic;
def int_amdgcn_raw_ptr_buffer_atomic_fmax : AMDGPURawPtrBufferAtomic<llvm_anyfloat_ty>;
def int_amdgcn_raw_ptr_buffer_atomic_and : AMDGPURawPtrBufferAtomic;
def int_amdgcn_raw_ptr_buffer_atomic_or : AMDGPURawPtrBufferAtomic;
def int_amdgcn_raw_ptr_buffer_atomic_xor : AMDGPURawPtrBufferAtomic;
def int_amdgcn_raw_ptr_buffer_atomic_inc : AMDGPURawPtrBufferAtomic;
def int_amdgcn_raw_ptr_buffer_atomic_dec : AMDGPURawPtrBufferAtomic;
def int_amdgcn_raw_ptr_buffer_atomic_cond_sub_u32 : AMDGPURawPtrBufferAtomic;
def int_amdgcn_raw_ptr_buffer_atomic_cmpswap : Intrinsic<
  [llvm_anyint_ty],
  [LLVMMatchType<0>,  // src(VGPR)
   LLVMMatchType<0>,  // cmp(VGPR)
   AMDGPUBufferRsrcTy, // rsrc(SGPR)
   llvm_i32_ty,       // offset(VGPR/imm, included in bounds checking and swizzling)
   llvm_i32_ty,       // soffset(SGPR/imm, excluded from bounds checking and swizzling)
   llvm_i32_ty],      // cachepolicy(imm; bit 1 = slc, ..., bit 31 = volatile)
  [IntrArgMemOnly, NoCapture<ArgIndex<2>>,
   ImmArg<ArgIndex<5>>, IntrWillReturn, IntrNoCallback, IntrNoFree], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<2, 0>;

// gfx908 intrinsic
def int_amdgcn_raw_buffer_atomic_fadd : AMDGPURawBufferAtomic<llvm_anyfloat_ty>;

// Supports float and <2 x half> on gfx908. Supports v2bf16 on gfx90a, gfx940, gfx12+.
def int_amdgcn_raw_ptr_buffer_atomic_fadd : AMDGPURawPtrBufferAtomic<llvm_anyfloat_ty>;

class AMDGPUStructBufferAtomic<LLVMType data_ty = llvm_any_ty> : Intrinsic <
  [data_ty],
  [LLVMMatchType<0>,  // vdata(VGPR)
   llvm_v4i32_ty,     // rsrc(SGPR)
   llvm_i32_ty,       // vindex(VGPR)
   llvm_i32_ty,       // offset(VGPR/imm, included in bounds checking and swizzling)
   llvm_i32_ty,       // soffset(SGPR/imm, excluded from bounds checking and swizzling)
   llvm_i32_ty],      // cachepolicy(imm; bit 1 = slc, ..., bit 31 = volatile)
  [ImmArg<ArgIndex<5>>, IntrWillReturn, IntrNoCallback, IntrNoFree], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<1, 0>;
def int_amdgcn_struct_buffer_atomic_swap : AMDGPUStructBufferAtomic;
def int_amdgcn_struct_buffer_atomic_add : AMDGPUStructBufferAtomic;
def int_amdgcn_struct_buffer_atomic_sub : AMDGPUStructBufferAtomic;
def int_amdgcn_struct_buffer_atomic_smin : AMDGPUStructBufferAtomic;
def int_amdgcn_struct_buffer_atomic_umin : AMDGPUStructBufferAtomic;
def int_amdgcn_struct_buffer_atomic_smax : AMDGPUStructBufferAtomic;
def int_amdgcn_struct_buffer_atomic_umax : AMDGPUStructBufferAtomic;
def int_amdgcn_struct_buffer_atomic_and : AMDGPUStructBufferAtomic;
def int_amdgcn_struct_buffer_atomic_or : AMDGPUStructBufferAtomic;
def int_amdgcn_struct_buffer_atomic_xor : AMDGPUStructBufferAtomic;
def int_amdgcn_struct_buffer_atomic_inc : AMDGPUStructBufferAtomic;
def int_amdgcn_struct_buffer_atomic_dec : AMDGPUStructBufferAtomic;
def int_amdgcn_struct_buffer_atomic_cond_sub_u32 : AMDGPUStructBufferAtomic;
def int_amdgcn_struct_buffer_atomic_cmpswap : Intrinsic<
  [llvm_anyint_ty],
  [LLVMMatchType<0>,  // src(VGPR)
   LLVMMatchType<0>,  // cmp(VGPR)
   llvm_v4i32_ty,     // rsrc(SGPR)
   llvm_i32_ty,       // vindex(VGPR)
   llvm_i32_ty,       // offset(VGPR/imm, included in bounds checking and swizzling)
   llvm_i32_ty,       // soffset(SGPR/imm, excluded from bounds checking and swizzling)
   llvm_i32_ty],      // cachepolicy(imm; bit 1 = slc, ..., bit 31 = volatile)
  [ImmArg<ArgIndex<6>>, IntrWillReturn, IntrNoCallback, IntrNoFree], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<2, 0>;

class AMDGPUStructPtrBufferAtomic<LLVMType data_ty = llvm_any_ty> : Intrinsic <
  [data_ty],
  [LLVMMatchType<0>,            // vdata(VGPR)
   AMDGPUBufferRsrcTy,          // rsrc(SGPR)
   llvm_i32_ty,                 // vindex(VGPR)
   llvm_i32_ty,                 // offset(VGPR/imm, included in bounds checking and swizzling)
   llvm_i32_ty,                 // soffset(SGPR/imm, excluded from bounds checking and swizzling)
   llvm_i32_ty],                // cachepolicy(imm; bit 1 = slc, ..., bit 31 = volatile)
  [IntrArgMemOnly, NoCapture<ArgIndex<1>>,
   ImmArg<ArgIndex<5>>, IntrWillReturn, IntrNoCallback, IntrNoFree], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<1, 0>;
def int_amdgcn_struct_ptr_buffer_atomic_swap : AMDGPUStructPtrBufferAtomic;
def int_amdgcn_struct_ptr_buffer_atomic_add : AMDGPUStructPtrBufferAtomic;
def int_amdgcn_struct_ptr_buffer_atomic_sub : AMDGPUStructPtrBufferAtomic;
def int_amdgcn_struct_ptr_buffer_atomic_smin : AMDGPUStructPtrBufferAtomic;
def int_amdgcn_struct_ptr_buffer_atomic_umin : AMDGPUStructPtrBufferAtomic;
def int_amdgcn_struct_ptr_buffer_atomic_smax : AMDGPUStructPtrBufferAtomic;
def int_amdgcn_struct_ptr_buffer_atomic_umax : AMDGPUStructPtrBufferAtomic;
def int_amdgcn_struct_ptr_buffer_atomic_and : AMDGPUStructPtrBufferAtomic;
def int_amdgcn_struct_ptr_buffer_atomic_or : AMDGPUStructPtrBufferAtomic;
def int_amdgcn_struct_ptr_buffer_atomic_xor : AMDGPUStructPtrBufferAtomic;
def int_amdgcn_struct_ptr_buffer_atomic_inc : AMDGPUStructPtrBufferAtomic;
def int_amdgcn_struct_ptr_buffer_atomic_dec : AMDGPUStructPtrBufferAtomic;
def int_amdgcn_struct_ptr_buffer_atomic_cond_sub_u32 : AMDGPUStructPtrBufferAtomic;
def int_amdgcn_struct_ptr_buffer_atomic_cmpswap : Intrinsic<
  [llvm_anyint_ty],
  [LLVMMatchType<0>,  // src(VGPR)
   LLVMMatchType<0>,  // cmp(VGPR)
   AMDGPUBufferRsrcTy, // rsrc(SGPR)
   llvm_i32_ty,       // vindex(VGPR)
   llvm_i32_ty,       // offset(VGPR/imm, included in bounds checking and swizzling)
   llvm_i32_ty,       // soffset(SGPR/imm, excluded from bounds checking and swizzling)
   llvm_i32_ty],      // cachepolicy(imm; bit 1 = slc, ..., bit 31 = volatile)
  [IntrArgMemOnly, NoCapture<ArgIndex<2>>,
   ImmArg<ArgIndex<6>>, IntrWillReturn, IntrNoCallback, IntrNoFree], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<2, 0>;

// gfx908 intrinsic
def int_amdgcn_struct_buffer_atomic_fadd : AMDGPUStructBufferAtomic<llvm_anyfloat_ty>;
def int_amdgcn_struct_ptr_buffer_atomic_fadd : AMDGPUStructPtrBufferAtomic<llvm_anyfloat_ty>;

// gfx90a intrinsics
def int_amdgcn_struct_buffer_atomic_fmin : AMDGPUStructBufferAtomic<llvm_anyfloat_ty>;
def int_amdgcn_struct_buffer_atomic_fmax : AMDGPUStructBufferAtomic<llvm_anyfloat_ty>;

def int_amdgcn_struct_ptr_buffer_atomic_fmin : AMDGPUStructPtrBufferAtomic<llvm_anyfloat_ty>;
def int_amdgcn_struct_ptr_buffer_atomic_fmax : AMDGPUStructPtrBufferAtomic<llvm_anyfloat_ty>;

// tbuffer intrinsics, with:
// - raw and struct variants
// - joint format field
// - joint cachepolicy field
def int_amdgcn_raw_tbuffer_load : DefaultAttrsIntrinsic <
    [llvm_any_ty],    // overloaded for types f32/i32, v2f32/v2i32, v4f32/v4i32
    [llvm_v4i32_ty,   // rsrc(SGPR)
     llvm_i32_ty,     // offset(VGPR/imm, included in bounds checking and swizzling)
     llvm_i32_ty,     // soffset(SGPR/imm, excluded from bounds checking and swizzling)
     llvm_i32_ty,     // format(imm; bits 3..0 = dfmt, bits 6..4 = nfmt)
     llvm_i32_ty],    // auxiliary/cachepolicy(imm):
                      //                bit 0 = glc, bit 1 = slc, bit 2 = dlc (gfx10/gfx11),
                      //                bit 3 = swz, bit 4 = scc (gfx90a)
                      //        gfx940: bit 0 = sc0, bit 1 = nt, bit 3 = swz, bit 4 = sc1
                      //        gfx12+: bits [0-2] = th, bits [3-4] = scope,
                      //                bit 6 = swz
    [IntrReadMem,
     ImmArg<ArgIndex<3>>, ImmArg<ArgIndex<4>>], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<0>;

def int_amdgcn_raw_ptr_tbuffer_load : DefaultAttrsIntrinsic <
    [llvm_any_ty],       // overloaded for types f32/i32, v2f32/v2i32, v4f32/v4i32
    [AMDGPUBufferRsrcTy, // rsrc(SGPR)
     llvm_i32_ty,       // offset(VGPR/imm, included in bounds` checking and swizzling)
     llvm_i32_ty,       // soffset(SGPR/imm, excluded from bounds checking and swizzling)
     llvm_i32_ty,       // format(imm; bits 3..0 = dfmt, bits 6..4 = nfmt)
     llvm_i32_ty],      // auxiliary/cachepolicy(imm):
                        //                bit 0 = glc, bit 1 = slc, bit 2 = dlc (gfx10/gfx11),
                        //                bit 3 = swz, bit 4 = scc (gfx90a)
                        //        gfx940: bit 0 = sc0, bit 1 = nt, bit 3 = swz, bit 4 = sc1
                        //        gfx12+: bits [0-2] = th, bits [3-4] = scope,
                        //                bit 6 = swz
                        //           all: volatile op (bit 31, stripped at lowering)
    [IntrArgMemOnly, IntrReadMem, ReadOnly<ArgIndex<0>>, NoCapture<ArgIndex<0>>,
     ImmArg<ArgIndex<3>>, ImmArg<ArgIndex<4>>], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<0>;

def int_amdgcn_raw_tbuffer_store : DefaultAttrsIntrinsic <
    [],
    [llvm_any_ty,    // vdata(VGPR), overloaded for types f32/i32, v2f32/v2i32, v4f32/v4i32
     llvm_v4i32_ty,  // rsrc(SGPR)
     llvm_i32_ty,    // offset(VGPR/imm, included in bounds checking and swizzling)
     llvm_i32_ty,    // soffset(SGPR/imm, excluded from bounds checking and swizzling)
     llvm_i32_ty,    // format(imm; bits 3..0 = dfmt, bits 6..4 = nfmt)
     llvm_i32_ty],   // auxiliary/cachepolicy(imm):
                     //                bit 0 = glc, bit 1 = slc, bit 2 = dlc (gfx10/gfx11),
                     //                bit 3 = swz, bit 4 = scc (gfx90a)
                     //        gfx940: bit 0 = sc0, bit 1 = nt, bit 3 = swz, bit 4 = sc1
                     //        gfx12+: bits [0-2] = th, bits [3-4] = scope,
                     //                bit 6 = swz
                     //           all: volatile op (bit 31, stripped at lowering)
    [IntrWriteMem,
     ImmArg<ArgIndex<4>>, ImmArg<ArgIndex<5>>], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<1>;

def int_amdgcn_raw_ptr_tbuffer_store : DefaultAttrsIntrinsic <
    [],
    [llvm_any_ty,    // vdata(VGPR), overloaded for types f32/i32, v2f32/v2i32, v4f32/v4i32
     AMDGPUBufferRsrcTy, // rsrc(SGPR)
     llvm_i32_ty,    // offset(VGPR/imm, included in bounds checking and swizzling)
     llvm_i32_ty,    // soffset(SGPR/imm, excluded from bounds checking and swizzling)
     llvm_i32_ty,    // format(imm; bits 3..0 = dfmt, bits 6..4 = nfmt)
     llvm_i32_ty],   // auxiliary/cachepolicy(imm):
                     //                bit 0 = glc, bit 1 = slc, bit 2 = dlc (gfx10/gfx11),
                     //                bit 3 = swz, bit 4 = scc (gfx90a)
                     //        gfx940: bit 0 = sc0, bit 1 = nt, bit 3 = swz, bit 4 = sc1
                     //        gfx12+: bits [0-2] = th, bits [3-4] = scope,
                     //                bit 6 = swz
                     //           all: volatile op (bit 31, stripped at lowering)
    [IntrArgMemOnly, IntrWriteMem, WriteOnly<ArgIndex<1>>, NoCapture<ArgIndex<1>>,
     ImmArg<ArgIndex<4>>, ImmArg<ArgIndex<5>>], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<1>;

def int_amdgcn_struct_tbuffer_load : DefaultAttrsIntrinsic <
    [llvm_any_ty],    // overloaded for types f32/i32, v2f32/v2i32, v4f32/v4i32
    [llvm_v4i32_ty,   // rsrc(SGPR)
     llvm_i32_ty,     // vindex(VGPR)
     llvm_i32_ty,     // offset(VGPR/imm, included in bounds checking and swizzling)
     llvm_i32_ty,     // soffset(SGPR/imm, excluded from bounds checking and swizzling)
     llvm_i32_ty,     // format(imm; bits 3..0 = dfmt, bits 6..4 = nfmt)
     llvm_i32_ty],    // auxiliary/cachepolicy(imm):
                      //                bit 0 = glc, bit 1 = slc, bit 2 = dlc (gfx10/gfx11),
                      //                bit 3 = swz, bit 4 = scc (gfx90a)
                      //        gfx940: bit 0 = sc0, bit 1 = nt, bit 3 = swz, bit 4 = sc1
                      //        gfx12+: bits [0-2] = th, bits [3-4] = scope,
                      //                bit 6 = swz
                      //           all: volatile op (bit 31, stripped at lowering)
    [IntrReadMem,
     ImmArg<ArgIndex<4>>, ImmArg<ArgIndex<5>>], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<0>;

def int_amdgcn_struct_ptr_tbuffer_load : DefaultAttrsIntrinsic <
    [llvm_any_ty],       // overloaded for types f32/i32, v2f32/v2i32, v4f32/v4i32
    [AMDGPUBufferRsrcTy, // rsrc(SGPR)
     llvm_i32_ty,        // vindex(VGPR)
     llvm_i32_ty,        // offset(VGPR/imm, included in bounds checking and swizzling)
     llvm_i32_ty,        // soffset(SGPR/imm, excluded from bounds checking and swizzling)
     llvm_i32_ty,        // format(imm; bits 3..0 = dfmt, bits 6..4 = nfmt)
     llvm_i32_ty],       // auxiliary/cachepolicy(imm):
                         //                bit 0 = glc, bit 1 = slc, bit 2 = dlc (gfx10/gfx11),
                         //                bit 3 = swz, bit 4 = scc (gfx90a)
                         //        gfx940: bit 0 = sc0, bit 1 = nt, bit 3 = swz, bit 4 = sc1
                         //        gfx12+: bits [0-2] = th, bits [3-4] = scope,
                         //                bit 6 = swz
                         //           all: volatile op (bit 31, stripped at lowering)
    [IntrArgMemOnly, IntrReadMem, ReadOnly<ArgIndex<0>>, NoCapture<ArgIndex<0>>,
     ImmArg<ArgIndex<4>>, ImmArg<ArgIndex<5>>], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<0>;

def int_amdgcn_struct_ptr_tbuffer_store : DefaultAttrsIntrinsic <
    [],
    [llvm_any_ty,        // vdata(VGPR), overloaded for types f32/i32, v2f32/v2i32, v4f32/v4i32
     AMDGPUBufferRsrcTy, // rsrc(SGPR)
     llvm_i32_ty,        // vindex(VGPR)
     llvm_i32_ty,        // offset(VGPR/imm, included in bounds checking and swizzling)
     llvm_i32_ty,        // soffset(SGPR/imm, excluded from bounds checking and swizzling)
     llvm_i32_ty,        // format(imm; bits 3..0 = dfmt, bits 6..4 = nfmt)
     llvm_i32_ty],       // auxiliary/cachepolicy(imm):
                         //                bit 0 = glc, bit 1 = slc, bit 2 = dlc (gfx10/gfx11),
                         //                bit 3 = swz, bit 4 = scc (gfx90a)
                         //        gfx940: bit 0 = sc0, bit 1 = nt, bit 3 = swz, bit 4 = sc1
                         //        gfx12+: bits [0-2] = th, bits [3-4] = scope,
                         //                bit 6 = swz
                         //           all: volatile op (bit 31, stripped at lowering)
    [IntrArgMemOnly, IntrWriteMem, WriteOnly<ArgIndex<1>>, NoCapture<ArgIndex<1>>,
     ImmArg<ArgIndex<5>>, ImmArg<ArgIndex<6>>], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<1>;

def int_amdgcn_struct_tbuffer_store : DefaultAttrsIntrinsic <
    [],
    [llvm_any_ty,    // vdata(VGPR), overloaded for types f32/i32, v2f32/v2i32, v4f32/v4i32
     llvm_v4i32_ty,  // rsrc(SGPR)
     llvm_i32_ty,    // vindex(VGPR)
     llvm_i32_ty,    // offset(VGPR/imm, included in bounds checking and swizzling)
     llvm_i32_ty,    // soffset(SGPR/imm, excluded from bounds checking and swizzling)
     llvm_i32_ty,    // format(imm; bits 3..0 = dfmt, bits 6..4 = nfmt)
     llvm_i32_ty],   // auxiliary/cachepolicy(imm):
                     //                bit 0 = glc, bit 1 = slc, bit 2 = dlc (gfx10/gfx11),
                     //                bit 3 = swz, bit 4 = scc (gfx90a)
                     //        gfx940: bit 0 = sc0, bit 1 = nt, bit 3 = swz, bit 4 = sc1
                     //        gfx12+: bits [0-2] = th, bits [3-4] = scope,
                     //                bit 6 = swz
                     //           all: volatile op (bit 31, stripped at lowering)
    [IntrWriteMem,
     ImmArg<ArgIndex<5>>, ImmArg<ArgIndex<6>>], "", [SDNPMemOperand]>,
  AMDGPURsrcIntrinsic<1>;

class AMDGPURawBufferLoadLDS : Intrinsic <
  [],
  [llvm_v4i32_ty,             // rsrc(SGPR)
   LLVMQualPointerType<3>,    // LDS base offset
   llvm_i32_ty,               // Data byte size: 1/2/4
   llvm_i32_ty,               // voffset(VGPR, included in bounds checking and swizzling)
   llvm_i32_ty,               // soffset(SGPR/imm, excluded from bounds checking and swizzling)
   llvm_i32_ty,               // imm offset(imm, included in bounds checking and swizzling)
   llvm_i32_ty],              // auxiliary/cachepolicy(imm):
                              //                bit 0 = glc, bit 1 = slc, bit 2 = dlc (gfx10/gfx11),
                              //                bit 3 = swz, bit 4 = scc (gfx90a)
                              //        gfx940: bit 0 = sc0, bit 1 = nt, bit 3 = swz, bit 4 = sc1
                              //        gfx12+: bits [0-2] = th, bits [3-4] = scope,
                              //                bit 6 = swz
                              //           all: volatile op (bit 31, stripped at lowering)
  [IntrWillReturn, NoCapture<ArgIndex<1>>, ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<5>>,
   ImmArg<ArgIndex<6>>, IntrNoCallback, IntrNoFree], "", [SDNPMemOperand]>, AMDGPURsrcIntrinsic<0>;
def int_amdgcn_raw_buffer_load_lds : AMDGPURawBufferLoadLDS;

class AMDGPURawPtrBufferLoadLDS : Intrinsic <
  [],
  [AMDGPUBufferRsrcTy,        // rsrc(SGPR)
   LLVMQualPointerType<3>,    // LDS base offset
   llvm_i32_ty,               // Data byte size: 1/2/4
   llvm_i32_ty,               // voffset(VGPR, included in bounds checking and swizzling)
   llvm_i32_ty,               // soffset(SGPR/imm, excluded from bounds checking and swizzling)
   llvm_i32_ty,               // imm offset(imm, included in bounds checking and swizzling)
   llvm_i32_ty],              // auxiliary/cachepolicy(imm):
                              //                bit 0 = glc, bit 1 = slc, bit 2 = dlc (gfx10/gfx11),
                              //                bit 3 = swz, bit 4 = scc (gfx90a)
                              //        gfx940: bit 0 = sc0, bit 1 = nt, bit 3 = swz, bit 4 = sc1
                              //        gfx12+: bits [0-2] = th, bits [3-4] = scope,
                              //                bit 6 = swz
                              //           all: volatile op (bit 31, stripped at lowering)
  [IntrWillReturn, IntrArgMemOnly,
   ReadOnly<ArgIndex<0>>, NoCapture<ArgIndex<0>>,
   WriteOnly<ArgIndex<1>>, NoCapture<ArgIndex<1>>,
   ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<5>>,
   ImmArg<ArgIndex<6>>, IntrNoCallback, IntrNoFree], "", [SDNPMemOperand]>, AMDGPURsrcIntrinsic<0>;
def int_amdgcn_raw_ptr_buffer_load_lds : AMDGPURawPtrBufferLoadLDS;

class AMDGPUStructBufferLoadLDS : Intrinsic <
  [],
  [llvm_v4i32_ty,             // rsrc(SGPR)
   LLVMQualPointerType<3>,    // LDS base offset
   llvm_i32_ty,               // Data byte size: 1/2/4
   llvm_i32_ty,               // vindex(VGPR)
   llvm_i32_ty,               // voffset(VGPR, included in bounds checking and swizzling)
   llvm_i32_ty,               // soffset(SGPR/imm, excluded from bounds checking and swizzling)
   llvm_i32_ty,               // imm offset(imm, included in bounds checking and swizzling)
   llvm_i32_ty],              // auxiliary/cachepolicy(imm):
                              //                bit 0 = glc, bit 1 = slc, bit 2 = dlc (gfx10/gfx11),
                              //                bit 3 = swz, bit 4 = scc (gfx90a)
                              //        gfx940: bit 0 = sc0, bit 1 = nt, bit 3 = swz, bit 4 = sc1
                              //        gfx12+: bits [0-2] = th, bits [3-4] = scope,
                              //                bit 6 = swz
                              //           all: volatile op (bit 31, stripped at lowering)
  [IntrWillReturn, NoCapture<ArgIndex<1>>, ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<6>>,
   ImmArg<ArgIndex<7>>, IntrNoCallback, IntrNoFree], "", [SDNPMemOperand]>, AMDGPURsrcIntrinsic<0>;
def int_amdgcn_struct_buffer_load_lds : AMDGPUStructBufferLoadLDS;

class AMDGPUStructPtrBufferLoadLDS : Intrinsic <
  [],
  [AMDGPUBufferRsrcTy,        // rsrc(SGPR)
   LLVMQualPointerType<3>,    // LDS base offset
   llvm_i32_ty,               // Data byte size: 1/2/4
   llvm_i32_ty,               // vindex(VGPR)
   llvm_i32_ty,               // voffset(VGPR, included in bounds checking and swizzling)
   llvm_i32_ty,               // soffset(SGPR/imm, excluded from bounds checking and swizzling)
   llvm_i32_ty,               // imm offset(imm, included in bounds checking and swizzling)
   llvm_i32_ty],              // auxiliary/cachepolicy(imm):
                              //                bit 0 = glc, bit 1 = slc, bit 2 = dlc (gfx10/gfx11),
                              //                bit 3 = swz, bit 4 = scc (gfx90a)
                              //        gfx940: bit 0 = sc0, bit 1 = nt, bit 3 = swz, bit 4 = sc1
                              //        gfx12+: bits [0-2] = th, bits [3-4] = scope,
                              //                bit 6 = swz
                              //           all: volatile op (bit 31, stripped at lowering)
  [IntrWillReturn, IntrArgMemOnly,
   ReadOnly<ArgIndex<0>>, NoCapture<ArgIndex<0>>,
   WriteOnly<ArgIndex<1>>, NoCapture<ArgIndex<1>>,
   ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<6>>,
   ImmArg<ArgIndex<7>>, IntrNoCallback, IntrNoFree], "", [SDNPMemOperand]>, AMDGPURsrcIntrinsic<0>;
def int_amdgcn_struct_ptr_buffer_load_lds : AMDGPUStructPtrBufferLoadLDS;

} // defset AMDGPUBufferIntrinsics

// Uses that do not set the done bit should set IntrWriteMem on the
// call site.
def int_amdgcn_exp : DefaultAttrsIntrinsic <[], [
  llvm_i32_ty,       // tgt,
  llvm_i32_ty,       // en
  llvm_any_ty,       // src0 (f32 or i32)
  LLVMMatchType<0>,  // src1
  LLVMMatchType<0>,  // src2
  LLVMMatchType<0>,  // src3
  llvm_i1_ty,        // done
  llvm_i1_ty         // vm (ignored on GFX11+)
  ],
  [ImmArg<ArgIndex<0>>, ImmArg<ArgIndex<1>>, ImmArg<ArgIndex<6>>,
   ImmArg<ArgIndex<7>>, IntrWriteMem, IntrInaccessibleMemOnly]
>;

// exp with row_en bit set. Only supported on GFX11+.
def int_amdgcn_exp_row : DefaultAttrsIntrinsic <[], [
  llvm_i32_ty,       // tgt,
  llvm_i32_ty,       // en
  llvm_any_ty,       // src0 (f32 or i32)
  LLVMMatchType<0>,  // src1
  LLVMMatchType<0>,  // src2
  LLVMMatchType<0>,  // src3
  llvm_i1_ty,        // done
  llvm_i32_ty],      // row number
  [ImmArg<ArgIndex<0>>, ImmArg<ArgIndex<1>>, ImmArg<ArgIndex<6>>,
   IntrWriteMem, IntrInaccessibleMemOnly]
>;

// exp with compr bit set. Not supported on GFX11+.
def int_amdgcn_exp_compr : DefaultAttrsIntrinsic <[], [
  llvm_i32_ty,       // tgt,
  llvm_i32_ty,       // en
  llvm_anyvector_ty, // src0 (v2f16 or v2i16)
  LLVMMatchType<0>,  // src1
  llvm_i1_ty,        // done
  llvm_i1_ty],       // vm
  [ImmArg<ArgIndex<0>>, ImmArg<ArgIndex<1>>, ImmArg<ArgIndex<4>>,
   ImmArg<ArgIndex<5>>, IntrWriteMem, IntrInaccessibleMemOnly]
>;

def int_amdgcn_buffer_wbinvl1_sc :
  ClangBuiltin<"__builtin_amdgcn_buffer_wbinvl1_sc">,
  DefaultAttrsIntrinsic<[], [], [IntrNoMem, IntrHasSideEffects]>;

def int_amdgcn_buffer_wbinvl1 :
  ClangBuiltin<"__builtin_amdgcn_buffer_wbinvl1">,
  DefaultAttrsIntrinsic<[], [], [IntrNoMem, IntrHasSideEffects]>;

def int_amdgcn_s_dcache_inv :
  ClangBuiltin<"__builtin_amdgcn_s_dcache_inv">,
  DefaultAttrsIntrinsic<[], [], [IntrNoMem, IntrHasSideEffects]>;

def int_amdgcn_s_memtime :
  ClangBuiltin<"__builtin_amdgcn_s_memtime">,
  DefaultAttrsIntrinsic<[llvm_i64_ty], [], [IntrNoMem, IntrHasSideEffects]>;

def int_amdgcn_s_sleep :
  ClangBuiltin<"__builtin_amdgcn_s_sleep">,
  DefaultAttrsIntrinsic<[], [llvm_i32_ty], [ImmArg<ArgIndex<0>>, IntrNoMem,
                                IntrHasSideEffects]> {
}

def int_amdgcn_s_sleep_var
    : ClangBuiltin<"__builtin_amdgcn_s_sleep_var">,
      Intrinsic<[], [llvm_i32_ty],
                [IntrNoMem, IntrHasSideEffects, IntrWillReturn]> {
}

def int_amdgcn_s_nop :
  DefaultAttrsIntrinsic<[], [llvm_i16_ty], [ImmArg<ArgIndex<0>>, IntrNoMem,
                                IntrHasSideEffects]> {
}

def int_amdgcn_s_incperflevel :
  ClangBuiltin<"__builtin_amdgcn_s_incperflevel">,
  DefaultAttrsIntrinsic<[], [llvm_i32_ty], [ImmArg<ArgIndex<0>>, IntrNoMem,
                                IntrHasSideEffects]> {
}

def int_amdgcn_s_decperflevel :
  ClangBuiltin<"__builtin_amdgcn_s_decperflevel">,
  DefaultAttrsIntrinsic<[], [llvm_i32_ty], [ImmArg<ArgIndex<0>>, IntrNoMem,
                                IntrHasSideEffects]> {
}

def int_amdgcn_s_sethalt :
  DefaultAttrsIntrinsic<[], [llvm_i32_ty], [ImmArg<ArgIndex<0>>, IntrNoMem,
                                IntrHasSideEffects]>;

def int_amdgcn_s_setprio :
  ClangBuiltin<"__builtin_amdgcn_s_setprio">,
  DefaultAttrsIntrinsic<[], [llvm_i16_ty], [ImmArg<ArgIndex<0>>, IntrNoMem,
                                IntrHasSideEffects]>;

def int_amdgcn_s_ttracedata :
  ClangBuiltin<"__builtin_amdgcn_s_ttracedata">,
  DefaultAttrsIntrinsic<[], [llvm_i32_ty],
                        [IntrNoMem, IntrHasSideEffects]>;

def int_amdgcn_s_ttracedata_imm :
  ClangBuiltin<"__builtin_amdgcn_s_ttracedata_imm">,
  DefaultAttrsIntrinsic<[], [llvm_i16_ty],
                        [IntrNoMem, IntrHasSideEffects, ImmArg<ArgIndex<0>>]>;

// This is IntrHasSideEffects so it can be used to read cycle counters.
def int_amdgcn_s_getreg :
  ClangBuiltin<"__builtin_amdgcn_s_getreg">,
  DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty],
  [IntrNoMem, IntrHasSideEffects, ImmArg<ArgIndex<0>>]
>;

// Note this can be used to set FP environment properties that are
// unsafe to change in non-strictfp functions. The register properties
// available (and value required to access them) may differ per
// subtarget. llvm.amdgcn.s.setreg(hwmode, value)
def int_amdgcn_s_setreg :
  ClangBuiltin<"__builtin_amdgcn_s_setreg">,
  DefaultAttrsIntrinsic<[], [llvm_i32_ty, llvm_i32_ty],
  [IntrNoMem, IntrHasSideEffects, ImmArg<ArgIndex<0>>]
>;

// int_amdgcn_s_getpc is provided to allow a specific style of position
// independent code to determine the high part of its address when it is
// known (through convention) that the code and any data of interest does
// not cross a 4Gb address boundary. Use for any other purpose may not
// produce the desired results as optimizations may cause code movement,
// especially as we explicitly use IntrNoMem to allow optimizations.
// This intrinsic always returns PC sign-extended from 48 bits even if the
// s_getpc_b64 instruction returns a zero-extended value.
def int_amdgcn_s_getpc :
  ClangBuiltin<"__builtin_amdgcn_s_getpc">,
  DefaultAttrsIntrinsic<[llvm_i64_ty], [], [NoUndef<RetIndex>, IntrNoMem,
                                IntrSpeculatable, IntrWillReturn]>;

// __builtin_amdgcn_interp_mov <param>, <attr_chan>, <attr>, <m0>
// param values: 0 = P10, 1 = P20, 2 = P0
def int_amdgcn_interp_mov :
  ClangBuiltin<"__builtin_amdgcn_interp_mov">,
  DefaultAttrsIntrinsic<[llvm_float_ty],
            [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
            [IntrNoMem, IntrSpeculatable,
              ImmArg<ArgIndex<0>>, ImmArg<ArgIndex<1>>, ImmArg<ArgIndex<2>>]>;

// __builtin_amdgcn_interp_p1 <i>, <attr_chan>, <attr>, <m0>
// This intrinsic reads from lds, but the memory values are constant,
// so it behaves like IntrNoMem.
def int_amdgcn_interp_p1 :
  ClangBuiltin<"__builtin_amdgcn_interp_p1">,
  DefaultAttrsIntrinsic<[llvm_float_ty],
            [llvm_float_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
            [IntrNoMem, IntrSpeculatable,
             ImmArg<ArgIndex<1>>, ImmArg<ArgIndex<2>>]>;

// __builtin_amdgcn_interp_p2 <p1>, <j>, <attr_chan>, <attr>, <m0>
def int_amdgcn_interp_p2 :
  ClangBuiltin<"__builtin_amdgcn_interp_p2">,
  DefaultAttrsIntrinsic<[llvm_float_ty],
            [llvm_float_ty, llvm_float_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
            [IntrNoMem, IntrSpeculatable,
             ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<3>>]>;
          // See int_amdgcn_v_interp_p1 for why this is IntrNoMem.

// __builtin_amdgcn_interp_p1_f16 <i>, <attr_chan>, <attr>, <high>, <m0>
// high selects whether high or low 16-bits are loaded from LDS
def int_amdgcn_interp_p1_f16 :
  ClangBuiltin<"__builtin_amdgcn_interp_p1_f16">,
  DefaultAttrsIntrinsic<[llvm_float_ty],
            [llvm_float_ty, llvm_i32_ty, llvm_i32_ty, llvm_i1_ty, llvm_i32_ty],
            [IntrNoMem, IntrSpeculatable,
             ImmArg<ArgIndex<1>>, ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<3>>]>;

// __builtin_amdgcn_interp_p2_f16 <p1>, <j>, <attr_chan>, <attr>, <high>, <m0>
// high selects whether high or low 16-bits are loaded from LDS
def int_amdgcn_interp_p2_f16 :
  ClangBuiltin<"__builtin_amdgcn_interp_p2_f16">,
  DefaultAttrsIntrinsic<[llvm_half_ty],
            [llvm_float_ty, llvm_float_ty, llvm_i32_ty, llvm_i32_ty, llvm_i1_ty, llvm_i32_ty],
            [IntrNoMem, IntrSpeculatable,
             ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<3>>, ImmArg<ArgIndex<4>>]>;

// llvm.amdgcn.lds.direct.load <m0>
// The input argument is m0, which contains a packed combination of address
// offset and flags describing the data type.
def int_amdgcn_lds_direct_load :
  DefaultAttrsIntrinsic<[llvm_any_ty], // overloaded for types u8, u16, i32/f32, i8, i16
            [llvm_i32_ty],
            [IntrReadMem, IntrSpeculatable]>;

// llvm.amdgcn.lds.param.load <attr_chan>, <attr>, <m0>
// Like interp intrinsics, this reads from lds, but the memory values are constant,
// so it behaves like IntrNoMem.
def int_amdgcn_lds_param_load :
  DefaultAttrsIntrinsic<[llvm_float_ty],
            [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
            [IntrNoMem, IntrSpeculatable,
             ImmArg<ArgIndex<0>>, ImmArg<ArgIndex<1>>]>;

// llvm.amdgcn.interp.inreg.p10 <p>, <i>, <p0>
def int_amdgcn_interp_inreg_p10 :
  DefaultAttrsIntrinsic<[llvm_float_ty],
            [llvm_float_ty, llvm_float_ty, llvm_float_ty],
            [IntrNoMem, IntrSpeculatable]>;

// llvm.amdgcn.interp.inreg.p2 <p>, <j>, <tmp>
def int_amdgcn_interp_inreg_p2 :
  DefaultAttrsIntrinsic<[llvm_float_ty],
            [llvm_float_ty, llvm_float_ty, llvm_float_ty],
            [IntrNoMem, IntrSpeculatable]>;

// llvm.amdgcn.interp.inreg.p10.f16 <p>, <i>, <p0>, <high>
// high selects whether high or low 16-bits are used for p and p0 operands
def int_amdgcn_interp_inreg_p10_f16:
  DefaultAttrsIntrinsic<[llvm_float_ty],
            [llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_i1_ty],
            [IntrNoMem, IntrSpeculatable,
             ImmArg<ArgIndex<3>>]>;

// llvm.amdgcn.interp.inreg.p2.f16 <p>, <j>, <tmp>, <high>
// high selects whether high or low 16-bits are used for p operand
def int_amdgcn_interp_inreg_p2_f16 :
  DefaultAttrsIntrinsic<[llvm_half_ty],
            [llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_i1_ty],
            [IntrNoMem, IntrSpeculatable,
             ImmArg<ArgIndex<3>>]>;

// llvm.amdgcn.interp.p10.rtz.f16 <p>, <i>, <p0>, <high>
// gfx11+ fp16 interpolation intrinsic, with round-toward-zero rounding mode.
// high selects whether high or low 16-bits are used for p and p0 operands
def int_amdgcn_interp_p10_rtz_f16:
  DefaultAttrsIntrinsic<[llvm_float_ty],
            [llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_i1_ty],
            [IntrNoMem, IntrSpeculatable,
             ImmArg<ArgIndex<3>>]>;

// llvm.amdgcn.interp.p2.rtz.f16 <p>, <j>, <tmp>, <high>
// gfx11+ fp16 interpolation intrinsic, with round-toward-zero rounding mode.
// high selects whether high or low 16-bits are used for p operand
def int_amdgcn_interp_p2_rtz_f16 :
  DefaultAttrsIntrinsic<[llvm_half_ty],
            [llvm_float_ty, llvm_float_ty, llvm_float_ty, llvm_i1_ty],
            [IntrNoMem, IntrSpeculatable,
             ImmArg<ArgIndex<3>>]>;

// Deprecated: use llvm.amdgcn.live.mask instead.
def int_amdgcn_ps_live : DefaultAttrsIntrinsic <
  [llvm_i1_ty],
  [],
  [IntrNoMem]>;

// Query currently live lanes.
// Returns true if lane is live (and not a helper lane).
def int_amdgcn_live_mask : DefaultAttrsIntrinsic <[llvm_i1_ty],
  [], [NoUndef<RetIndex>, IntrReadMem, IntrInaccessibleMemOnly]
>;

def int_amdgcn_mbcnt_lo :
  ClangBuiltin<"__builtin_amdgcn_mbcnt_lo">,
  DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
   [IntrNoMem]>;

def int_amdgcn_mbcnt_hi :
  ClangBuiltin<"__builtin_amdgcn_mbcnt_hi">,
  DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
            [IntrNoMem]>;

// llvm.amdgcn.ds.swizzle src offset
def int_amdgcn_ds_swizzle :
  ClangBuiltin<"__builtin_amdgcn_ds_swizzle">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
            [IntrNoMem, IntrConvergent, IntrWillReturn, IntrNoCallback, IntrNoFree,
             ImmArg<ArgIndex<1>>]>;

def int_amdgcn_ubfe : DefaultAttrsIntrinsic<[llvm_anyint_ty],
    [LLVMMatchType<0>, llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_sbfe : DefaultAttrsIntrinsic<[llvm_anyint_ty],
    [LLVMMatchType<0>, llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_lerp :
  ClangBuiltin<"__builtin_amdgcn_lerp">,
  DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_sad_u8 :
  ClangBuiltin<"__builtin_amdgcn_sad_u8">,
  DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_msad_u8 :
  ClangBuiltin<"__builtin_amdgcn_msad_u8">,
  DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_sad_hi_u8 :
  ClangBuiltin<"__builtin_amdgcn_sad_hi_u8">,
  DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_sad_u16 :
  ClangBuiltin<"__builtin_amdgcn_sad_u16">,
  DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_qsad_pk_u16_u8 :
  ClangBuiltin<"__builtin_amdgcn_qsad_pk_u16_u8">,
  DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i32_ty, llvm_i64_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_mqsad_pk_u16_u8 :
  ClangBuiltin<"__builtin_amdgcn_mqsad_pk_u16_u8">,
  DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i64_ty, llvm_i32_ty, llvm_i64_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_mqsad_u32_u8 :
  ClangBuiltin<"__builtin_amdgcn_mqsad_u32_u8">,
  DefaultAttrsIntrinsic<[llvm_v4i32_ty], [llvm_i64_ty, llvm_i32_ty, llvm_v4i32_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_cvt_pk_u8_f32 :
  ClangBuiltin<"__builtin_amdgcn_cvt_pk_u8_f32">,
  DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_float_ty, llvm_i32_ty, llvm_i32_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_icmp :
  Intrinsic<[llvm_anyint_ty], [llvm_anyint_ty, LLVMMatchType<1>, llvm_i32_ty],
            [IntrNoMem, IntrConvergent,
             ImmArg<ArgIndex<2>>, IntrWillReturn, IntrNoCallback, IntrNoFree]>;

def int_amdgcn_fcmp :
  Intrinsic<[llvm_anyint_ty], [llvm_anyfloat_ty, LLVMMatchType<1>, llvm_i32_ty],
            [IntrNoMem, IntrConvergent,
             ImmArg<ArgIndex<2>>, IntrWillReturn, IntrNoCallback, IntrNoFree]>;

def int_amdgcn_ballot :
  Intrinsic<[llvm_anyint_ty], [llvm_i1_ty],
            [IntrNoMem, IntrConvergent, IntrWillReturn, IntrNoCallback, IntrNoFree]>;

def int_amdgcn_inverse_ballot :
  Intrinsic<[llvm_i1_ty], [llvm_anyint_ty],
            [IntrNoMem, IntrWillReturn, IntrNoCallback, IntrNoFree]>;

// Lowers to S_BITREPLICATE_B64_B32.
// The argument must be uniform; otherwise, the result is undefined.
def int_amdgcn_s_bitreplicate :
  DefaultAttrsIntrinsic<[llvm_i64_ty], [llvm_i32_ty], [IntrNoMem, IntrConvergent]>;

// Lowers to S_QUADMASK_B{32,64}
// The argument must be uniform; otherwise, the result is undefined.
def int_amdgcn_s_quadmask :
  DefaultAttrsIntrinsic<[llvm_anyint_ty], [llvm_anyint_ty], [IntrNoMem, IntrConvergent]>;

// Lowers to S_WQM_B{32,64}
// The argument must be uniform; otherwise, the result is undefined.
// Does not set WQM; merely calculates the bitmask.
def int_amdgcn_s_wqm :
  DefaultAttrsIntrinsic<[llvm_anyint_ty], [llvm_anyint_ty], [IntrNoMem, IntrConvergent]>;

class AMDGPUWaveReduce<LLVMType data_ty = llvm_anyint_ty> : Intrinsic<
    [data_ty],
    [
      LLVMMatchType<0>,   // llvm value to reduce (SGPR/VGPR)
      llvm_i32_ty         // Reduction Strategy Switch for lowering ( 0: Default,
                          //                                          1: Iterative strategy, and
                          //                                          2. DPP)
    ],
    [IntrNoMem, IntrConvergent, IntrWillReturn, IntrNoCallback, IntrNoFree, ImmArg<ArgIndex<1>>]>;

def int_amdgcn_wave_reduce_umin : AMDGPUWaveReduce;
def int_amdgcn_wave_reduce_umax : AMDGPUWaveReduce;

def int_amdgcn_readfirstlane :
  Intrinsic<[llvm_any_ty], [LLVMMatchType<0>],
            [IntrNoMem, IntrConvergent, IntrWillReturn, IntrNoCallback, IntrNoFree]>;

// The lane argument must be uniform across the currently active threads of the
// current wave. Otherwise, the result is undefined.
def int_amdgcn_readlane :
  Intrinsic<[llvm_any_ty], [LLVMMatchType<0>, llvm_i32_ty],
            [IntrNoMem, IntrConvergent, IntrWillReturn, IntrNoCallback, IntrNoFree]>;

// The value to write and lane select arguments must be uniform across the
// currently active threads of the current wave. Otherwise, the result is
// undefined.
def int_amdgcn_writelane :
  Intrinsic<[llvm_any_ty], [
    LLVMMatchType<0>,   // uniform value to write: returned by the selected lane
    llvm_i32_ty,        // uniform lane select
    LLVMMatchType<0>    // returned by all lanes other than the selected one
  ],
  [IntrNoMem, IntrConvergent, IntrWillReturn, IntrNoCallback, IntrNoFree]
>;

def int_amdgcn_alignbyte : ClangBuiltin<"__builtin_amdgcn_alignbyte">,
  DefaultAttrsIntrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
  [IntrNoMem, IntrSpeculatable]
>;

// mul24 intrinsics can return i32 or i64.
// When returning i64, they're lowered to a mul24/mulhi24 pair.
def int_amdgcn_mul_i24 : DefaultAttrsIntrinsic<[llvm_anyint_ty],
  [llvm_i32_ty, llvm_i32_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_mul_u24 : DefaultAttrsIntrinsic<[llvm_anyint_ty],
  [llvm_i32_ty, llvm_i32_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_mulhi_i24 : DefaultAttrsIntrinsic<[llvm_i32_ty],
  [llvm_i32_ty, llvm_i32_ty],
  [IntrNoMem, IntrSpeculatable]
>;

def int_amdgcn_mulhi_u24 : DefaultAttrsIntrinsic<[llvm_i32_ty],
  [llvm_i32_ty, llvm_i32_ty],
  [IntrNoMem, IntrSpeculatable]
>;

// llvm.amdgcn.ds.gws.init(i32 bar_val, i32 resource_id)
//
// bar_val is the total number of waves that will wait on this
// barrier, minus 1.
def int_amdgcn_ds_gws_init :
  ClangBuiltin<"__builtin_amdgcn_ds_gws_init">,
  Intrinsic<[],
  [llvm_i32_ty, llvm_i32_ty],
  [IntrConvergent, IntrWriteMem,
   IntrInaccessibleMemOnly, IntrWillReturn, IntrNoCallback, IntrNoFree], "",
  [SDNPMemOperand]
>;

// llvm.amdgcn.ds.gws.barrier(i32 vsrc0, i32 resource_id)
// bar_val is the total number of waves that will wait on this
// barrier, minus 1.
def int_amdgcn_ds_gws_barrier :
  ClangBuiltin<"__builtin_amdgcn_ds_gws_barrier">,
  Intrinsic<[],
  [llvm_i32_ty, llvm_i32_ty],
  [IntrConvergent, IntrInaccessibleMemOnly, IntrWillReturn, IntrNoCallback, IntrNoFree], "",
  [SDNPMemOperand]
>;

// llvm.amdgcn.ds.gws.sema.v(i32 resource_id)
def int_amdgcn_ds_gws_sema_v :
  ClangBuiltin<"__builtin_amdgcn_ds_gws_sema_v">,
  Intrinsic<[],
  [llvm_i32_ty],
  [IntrConvergent, IntrInaccessibleMemOnly, IntrWillReturn, IntrNoCallback, IntrNoFree], "",
  [SDNPMemOperand]
>;

// llvm.amdgcn.ds.gws.sema.br(i32 vsrc, i32 resource_id)
def int_amdgcn_ds_gws_sema_br :
  ClangBuiltin<"__builtin_amdgcn_ds_gws_sema_br">,
  Intrinsic<[],
  [llvm_i32_ty, llvm_i32_ty],
  [IntrConvergent, IntrInaccessibleMemOnly, IntrWillReturn, IntrNoCallback, IntrNoFree], "",
  [SDNPMemOperand]
>;

// llvm.amdgcn.ds.gws.sema.p(i32 resource_id)
def int_amdgcn_ds_gws_sema_p :
  ClangBuiltin<"__builtin_amdgcn_ds_gws_sema_p">,
  Intrinsic<[],
  [llvm_i32_ty],
  [IntrConvergent, IntrInaccessibleMemOnly, IntrWillReturn, IntrNoCallback, IntrNoFree], "",
  [SDNPMemOperand]
>;

// llvm.amdgcn.ds.gws.sema.release.all(i32 resource_id)
def int_amdgcn_ds_gws_sema_release_all :
  ClangBuiltin<"__builtin_amdgcn_ds_gws_sema_release_all">,
  Intrinsic<[],
  [llvm_i32_ty],
  [IntrConvergent, IntrInaccessibleMemOnly, IntrWillReturn, IntrNoCallback, IntrNoFree], "",
  [SDNPMemOperand]
>;


// Copies the source value to the destination value, with the guarantee that
// the source value is computed as if the entire program were executed in WQM.
def int_amdgcn_wqm : Intrinsic<[llvm_any_ty],
  [LLVMMatchType<0>], [IntrNoMem, IntrSpeculatable, IntrWillReturn, IntrNoCallback, IntrNoFree]
>;

// Copies the source value to the destination value, such that the source
// is computed as if the entire program were executed in WQM if any other
// program code executes in WQM.
def int_amdgcn_softwqm : Intrinsic<[llvm_any_ty],
  [LLVMMatchType<0>], [IntrNoMem, IntrSpeculatable, IntrWillReturn, IntrNoCallback, IntrNoFree]
>;

// Return true if at least one thread within the pixel quad passes true into
// the function.
def int_amdgcn_wqm_vote : Intrinsic<[llvm_i1_ty],
  [llvm_i1_ty], [IntrNoMem, IntrConvergent, IntrWillReturn, IntrNoCallback, IntrNoFree]
>;

// If false, set EXEC=0 for the current thread until the end of program.
// FIXME: Should this be IntrNoMem, IntrHasSideEffects, or IntrWillReturn?
def int_amdgcn_kill : Intrinsic<[], [llvm_i1_ty], [IntrNoCallback, IntrNoFree]>;

def int_amdgcn_endpgm : ClangBuiltin<"__builtin_amdgcn_endpgm">,
  Intrinsic<[], [], [IntrNoReturn, IntrCold, IntrNoMem, IntrHasSideEffects, IntrConvergent,
                     IntrNoCallback, IntrNoFree]
>;

// If false, mark all active lanes as helper lanes until the end of program.
def int_amdgcn_wqm_demote : Intrinsic<[],
  [llvm_i1_ty], [IntrWriteMem, IntrInaccessibleMemOnly, IntrNoCallback, IntrNoFree]
>;

// Copies the active channels of the source value to the destination value,
// with the guarantee that the source value is computed as if the entire
// program were executed in Whole Wavefront Mode, i.e. with all channels
// enabled, with a few exceptions: - Phi nodes which require WWM return an
// undefined value.
def int_amdgcn_strict_wwm : Intrinsic<[llvm_any_ty],
  [LLVMMatchType<0>], [IntrNoMem, IntrSpeculatable,
                       IntrConvergent, IntrWillReturn, IntrNoCallback, IntrNoFree]
>;
// Deprecated. Use int_amdgcn_strict_wwm instead.
def int_amdgcn_wwm : Intrinsic<[llvm_any_ty],
  [LLVMMatchType<0>], [IntrNoMem, IntrSpeculatable,
                       IntrConvergent, IntrWillReturn, IntrNoCallback, IntrNoFree]
>;
def int_amdgcn_strict_wqm : Intrinsic<[llvm_any_ty],
  [LLVMMatchType<0>], [IntrNoMem, IntrSpeculatable,
                       IntrConvergent, IntrWillReturn, IntrNoCallback, IntrNoFree]
>;

// Given a value, copies it while setting all the inactive lanes to a given
// value. Note that OpenGL helper lanes are considered active, so if the
// program ever uses WQM, then the instruction and the first source will be
// computed in WQM.
def int_amdgcn_set_inactive :
  Intrinsic<[llvm_any_ty],
            [LLVMMatchType<0>, // value to be copied
             LLVMMatchType<0>], // value for the inactive lanes to take
            [IntrNoMem, IntrConvergent, IntrWillReturn, IntrNoCallback, IntrNoFree]>;

// Similar to int_amdgcn_set_inactive, but the value for the inactive lanes must
// be a VGPR function argument.
// Can only be used in functions with the `amdgpu_cs_chain` or
// `amdgpu_cs_chain_preserve` calling conventions, and only in uniform control
// flow.
def int_amdgcn_set_inactive_chain_arg :
  Intrinsic<[llvm_anyint_ty],
            [LLVMMatchType<0>, // value to be copied
             LLVMMatchType<0>], // value for the inactive lanes to take
            [IntrNoMem, IntrConvergent, IntrWillReturn, IntrNoCallback, IntrNoFree]>;

// Return if the given flat pointer points to a local memory address.
def int_amdgcn_is_shared : ClangBuiltin<"__builtin_amdgcn_is_shared">,
  DefaultAttrsIntrinsic<[llvm_i1_ty], [llvm_ptr_ty],
  [IntrNoMem, IntrSpeculatable, NoCapture<ArgIndex<0>>]
>;

// Return if the given flat pointer points to a prvate memory address.
def int_amdgcn_is_private : ClangBuiltin<"__builtin_amdgcn_is_private">,
  DefaultAttrsIntrinsic<[llvm_i1_ty], [llvm_ptr_ty],
  [IntrNoMem, IntrSpeculatable, NoCapture<ArgIndex<0>>]
>;

// A uniform tail call to a function with the `amdgpu_cs_chain` or
// `amdgpu_cs_chain_preserve` calling convention. It will populate the SGPRs
// starting at s0 and the VGPRs starting at v8, set EXEC and perform a jump to
// the given function.
// Can only be used in functions with the `amdgpu_cs`, `amdgpu_cs_chain` or
// `amdgpu_cs_chain_preserve` calling conventions, and only in uniform control
// flow.
def int_amdgcn_cs_chain:
  Intrinsic<[],
            [llvm_anyptr_ty, // The function to jump to.
             llvm_anyint_ty, // Value to put in EXEC (should be i32 or i64).
             llvm_any_ty, // Arguments that will be copied into SGPRs (s0+).
                          // Must be uniform.
             llvm_any_ty, // Arguments that will be copied into VGPRs (v8+).
                          // Need not be uniform.
             llvm_i32_ty, // Flags.
             llvm_vararg_ty // Additional arguments. Only present if Flags is
                            // non-zero.
            ],
            [IntrConvergent, IntrNoReturn, ImmArg<ArgIndex<4>>]>;


//===----------------------------------------------------------------------===//
// CI+ Intrinsics
//===----------------------------------------------------------------------===//

def int_amdgcn_s_dcache_inv_vol :
  ClangBuiltin<"__builtin_amdgcn_s_dcache_inv_vol">,
  DefaultAttrsIntrinsic<[], [], [IntrNoMem, IntrHasSideEffects]>;

def int_amdgcn_buffer_wbinvl1_vol :
  ClangBuiltin<"__builtin_amdgcn_buffer_wbinvl1_vol">,
  DefaultAttrsIntrinsic<[], [], [IntrNoMem, IntrHasSideEffects]>;

//===----------------------------------------------------------------------===//
// VI Intrinsics
//===----------------------------------------------------------------------===//

// llvm.amdgcn.mov.dpp.i32 <src> <dpp_ctrl> <row_mask> <bank_mask> <bound_ctrl>
def int_amdgcn_mov_dpp :
  Intrinsic<[llvm_anyint_ty],
            [LLVMMatchType<0>, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty,
             llvm_i1_ty],
             [IntrNoMem, IntrConvergent, IntrWillReturn,
             ImmArg<ArgIndex<1>>, ImmArg<ArgIndex<2>>,
             ImmArg<ArgIndex<3>>, ImmArg<ArgIndex<4>>, IntrNoCallback, IntrNoFree]>;

// llvm.amdgcn.update.dpp.i32 <old> <src> <dpp_ctrl> <row_mask> <bank_mask> <bound_ctrl>
// Should be equivalent to:
// v_mov_b32 <dest> <old>
// v_mov_b32 <dest> <src> <dpp_ctrl> <row_mask> <bank_mask> <bound_ctrl>
def int_amdgcn_update_dpp :
  Intrinsic<[llvm_any_ty],
            [LLVMMatchType<0>, LLVMMatchType<0>, llvm_i32_ty,
            llvm_i32_ty, llvm_i32_ty, llvm_i1_ty],
             [IntrNoMem, IntrConvergent, IntrWillReturn,
              ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<3>>,
              ImmArg<ArgIndex<4>>, ImmArg<ArgIndex<5>>, IntrNoCallback, IntrNoFree]>;

def int_amdgcn_s_dcache_wb :
  ClangBuiltin<"__builtin_amdgcn_s_dcache_wb">,
  Intrinsic<[], [], [IntrNoMem, IntrHasSideEffects, IntrWillReturn, IntrNoCallback, IntrNoFree]>;

def int_amdgcn_s_dcache_wb_vol :
  ClangBuiltin<"__builtin_amdgcn_s_dcache_wb_vol">,
  Intrinsic<[], [], [IntrNoMem, IntrHasSideEffects, IntrWillReturn, IntrNoCallback, IntrNoFree]>;

def int_amdgcn_s_memrealtime :
  ClangBuiltin<"__builtin_amdgcn_s_memrealtime">,
  Intrinsic<[llvm_i64_ty], [], [IntrNoMem, IntrHasSideEffects, IntrWillReturn, IntrNoCallback, IntrNoFree]>;

// llvm.amdgcn.ds.permute <index> <src>
def int_amdgcn_ds_permute :
  ClangBuiltin<"__builtin_amdgcn_ds_permute">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
    [IntrNoMem, IntrConvergent, IntrWillReturn, IntrNoCallback, IntrNoFree]>;

// llvm.amdgcn.ds.bpermute <index> <src>
def int_amdgcn_ds_bpermute :
  ClangBuiltin<"__builtin_amdgcn_ds_bpermute">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty],
     [IntrNoMem, IntrConvergent, IntrWillReturn, IntrNoCallback, IntrNoFree]>;

// llvm.amdgcn.perm <src0> <src1> <selector>
def int_amdgcn_perm :
  ClangBuiltin<"__builtin_amdgcn_perm">,
  Intrinsic<[llvm_i32_ty], [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
     [IntrNoMem, IntrSpeculatable, IntrWillReturn, IntrNoCallback, IntrNoFree]>;

//===----------------------------------------------------------------------===//
// GFX9 Intrinsics
//===----------------------------------------------------------------------===//

class AMDGPUGlobalLoadLDS :
  ClangBuiltin<"__builtin_amdgcn_global_load_lds">,
  Intrinsic <
    [],
    [LLVMQualPointerType<1>,            // Base global pointer to load from
     LLVMQualPointerType<3>,            // LDS base pointer to store to
     llvm_i32_ty,                       // Data byte size: 1/2/4
     llvm_i32_ty,                       // imm offset (applied to both global and LDS address)
     llvm_i32_ty],                      // auxiliary data (imm, cachepolicy (bit 0 = sc0,
                                        //                                   bit 1 = sc1,
                                        //                                   bit 4 = scc))
    [IntrWillReturn, NoCapture<ArgIndex<0>>, NoCapture<ArgIndex<1>>,
     ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<3>>, ImmArg<ArgIndex<4>>, IntrNoCallback, IntrNoFree],
     "", [SDNPMemOperand]>;
def int_amdgcn_global_load_lds : AMDGPUGlobalLoadLDS;

// This is IntrHasSideEffects because it reads from a volatile hardware register.
def int_amdgcn_pops_exiting_wave_id :
  DefaultAttrsIntrinsic<[llvm_i32_ty], [], [IntrNoMem, IntrHasSideEffects]>;

//===----------------------------------------------------------------------===//
// GFX10 Intrinsics
//===----------------------------------------------------------------------===//

// llvm.amdgcn.permlane16 <old> <src0> <src1> <src2> <fi> <bound_control>
def int_amdgcn_permlane16 :
  Intrinsic<[llvm_any_ty],
            [LLVMMatchType<0>, LLVMMatchType<0>, llvm_i32_ty, llvm_i32_ty, llvm_i1_ty, llvm_i1_ty],
            [IntrNoMem, IntrConvergent, IntrWillReturn,
             ImmArg<ArgIndex<4>>, ImmArg<ArgIndex<5>>, IntrNoCallback, IntrNoFree]>;

// llvm.amdgcn.permlanex16 <old> <src0> <src1> <src2> <fi> <bound_control>
def int_amdgcn_permlanex16 :
  Intrinsic<[llvm_any_ty],
            [LLVMMatchType<0>, LLVMMatchType<0>, llvm_i32_ty, llvm_i32_ty, llvm_i1_ty, llvm_i1_ty],
            [IntrNoMem, IntrConvergent, IntrWillReturn,
             ImmArg<ArgIndex<4>>, ImmArg<ArgIndex<5>>, IntrNoCallback, IntrNoFree]>;

// llvm.amdgcn.mov.dpp8.i32 <src> <sel>
// <sel> is a 32-bit constant whose high 8 bits must be zero which selects
// the lanes to read from.
def int_amdgcn_mov_dpp8 :
  Intrinsic<[llvm_anyint_ty],
            [LLVMMatchType<0>, llvm_i32_ty],
            [IntrNoMem, IntrConvergent, IntrWillReturn,
             ImmArg<ArgIndex<1>>, IntrNoCallback, IntrNoFree]>;

def int_amdgcn_s_get_waveid_in_workgroup :
  ClangBuiltin<"__builtin_amdgcn_s_get_waveid_in_workgroup">,
  Intrinsic<[llvm_i32_ty], [],
    [NoUndef<RetIndex>, IntrNoMem, IntrHasSideEffects, IntrWillReturn, IntrNoCallback, IntrNoFree]>;

class AMDGPUAtomicRtn<LLVMType vt, LLVMType pt = llvm_anyptr_ty> : Intrinsic <
  [vt],
  [pt,  // vaddr
   vt], // vdata(VGPR)
  [IntrArgMemOnly, IntrWillReturn, NoCapture<ArgIndex<0>>, IntrNoCallback, IntrNoFree], "",
  [SDNPMemOperand]>;

def int_amdgcn_global_atomic_csub : AMDGPUAtomicRtn<llvm_i32_ty>;

// uint4 llvm.amdgcn.image.bvh.intersect.ray <node_ptr>, <ray_extent>, <ray_origin>,
//                                           <ray_dir>, <ray_inv_dir>, <texture_descr>
// <node_ptr> is i32 or i64.
// <ray_dir> and <ray_inv_dir> are both v3f16 or both v3f32.
def int_amdgcn_image_bvh_intersect_ray :
  DefaultAttrsIntrinsic<[llvm_v4i32_ty],
            [llvm_anyint_ty, llvm_float_ty, llvm_v3f32_ty, llvm_anyvector_ty,
             LLVMMatchType<1>, llvm_v4i32_ty],
            [IntrReadMem, IntrWillReturn, IntrNoCallback, IntrNoFree]>;

//===----------------------------------------------------------------------===//
// GFX11 Intrinsics
//===----------------------------------------------------------------------===//

// llvm.amdgcn.permlane64 <src0>
def int_amdgcn_permlane64 :
  Intrinsic<[llvm_any_ty], [LLVMMatchType<0>],
            [IntrNoMem, IntrConvergent, IntrWillReturn, IntrNoCallback, IntrNoFree]>;

def int_amdgcn_ds_add_gs_reg_rtn :
  ClangBuiltin<"__builtin_amdgcn_ds_add_gs_reg_rtn">,
  Intrinsic<[llvm_anyint_ty], [llvm_i32_ty, llvm_i32_ty],
            [ImmArg<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn, IntrNoCallback, IntrNoFree],
            "", [SDNPMemOperand]>;

def int_amdgcn_ds_sub_gs_reg_rtn :
  ClangBuiltin<"__builtin_amdgcn_ds_sub_gs_reg_rtn">,
  Intrinsic<[llvm_anyint_ty], [llvm_i32_ty, llvm_i32_ty],
            [ImmArg<ArgIndex<1>>, IntrHasSideEffects, IntrWillReturn, IntrNoCallback, IntrNoFree],
            "", [SDNPMemOperand]>;

def int_amdgcn_ds_bvh_stack_rtn :
  Intrinsic<
    [llvm_i32_ty, llvm_i32_ty], // %vdst, %addr
    [
      llvm_i32_ty,   // %addr
      llvm_i32_ty,   // %data0
      llvm_v4i32_ty, // %data1
      llvm_i32_ty,   // %offset
    ],
    [ImmArg<ArgIndex<3>>, IntrWillReturn, IntrNoCallback, IntrNoFree]
  >;

def int_amdgcn_s_wait_event_export_ready :
  ClangBuiltin<"__builtin_amdgcn_s_wait_event_export_ready">,
  Intrinsic<[], [], [IntrNoMem, IntrHasSideEffects, IntrWillReturn]
>;

// WMMA (Wave Matrix Multiply-Accumulate) intrinsics
//
// These operations perform a matrix multiplication and accumulation of
// the form: D = A * B + C .

class AMDGPUWmmaIntrinsic<LLVMType AB, LLVMType CD> :
  Intrinsic<
    [CD], // %D
    [
      AB,               // %A
      LLVMMatchType<1>, // %B
      LLVMMatchType<0>, // %C
    ],
    [IntrNoMem, IntrConvergent, IntrWillReturn, IntrNoCallback, IntrNoFree]
>;

class AMDGPUWmmaIntrinsicOPSEL<LLVMType AB, LLVMType CD> :
  Intrinsic<
    [CD], // %D
    [
      AB,               // %A
      LLVMMatchType<1>, // %B
      LLVMMatchType<0>, // %C
      llvm_i1_ty,       // %high (op_sel) for GFX11, 0 for GFX12
    ],
    [IntrNoMem, IntrConvergent, ImmArg<ArgIndex<3>>, IntrWillReturn, IntrNoCallback, IntrNoFree]
>;

class AMDGPUWmmaIntrinsicIU<LLVMType AB, LLVMType CD> :
  Intrinsic<
    [CD], // %D
    [
      llvm_i1_ty,       // %A_sign
      AB,               // %A
      llvm_i1_ty,       // %B_sign
      LLVMMatchType<1>, // %B
      LLVMMatchType<0>, // %C
      llvm_i1_ty,       // %clamp
    ],
    [IntrNoMem, IntrConvergent, ImmArg<ArgIndex<0>>, ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<5>>, IntrWillReturn, IntrNoCallback, IntrNoFree]
>;

// WMMA GFX11Only

// The OPSEL intrinsics read from and write to one half of the registers, selected by the op_sel bit.
// The tied versions of the f16/bf16 wmma intrinsics tie the destination matrix registers to the input accumulator registers.
// The content of the other 16-bit half is preserved from the input.

defset list<Intrinsic> AMDGPUWMMAIntrinsicsGFX11 = {
def int_amdgcn_wmma_f16_16x16x16_f16_tied   : AMDGPUWmmaIntrinsicOPSEL<llvm_anyfloat_ty, llvm_anyfloat_ty>;
def int_amdgcn_wmma_bf16_16x16x16_bf16_tied : AMDGPUWmmaIntrinsicOPSEL<llvm_anyint_ty, llvm_anyint_ty>;

// WMMA GFX11Plus

def int_amdgcn_wmma_f32_16x16x16_f16   : AMDGPUWmmaIntrinsic<llvm_anyfloat_ty, llvm_anyfloat_ty>;
def int_amdgcn_wmma_f32_16x16x16_bf16  : AMDGPUWmmaIntrinsic<llvm_anyint_ty, llvm_anyfloat_ty>;
def int_amdgcn_wmma_i32_16x16x16_iu8   : AMDGPUWmmaIntrinsicIU<llvm_anyint_ty, llvm_anyint_ty>;
def int_amdgcn_wmma_i32_16x16x16_iu4   : AMDGPUWmmaIntrinsicIU<llvm_anyint_ty, llvm_anyint_ty>;

// GFX11: The OPSEL intrinsics read from and write to one half of the registers, selected by the op_sel bit.
//        The content of the other 16-bit half is undefined.
// GFX12: The op_sel bit must be 0.
def int_amdgcn_wmma_f16_16x16x16_f16   : AMDGPUWmmaIntrinsicOPSEL<llvm_anyfloat_ty, llvm_anyfloat_ty>;
def int_amdgcn_wmma_bf16_16x16x16_bf16 : AMDGPUWmmaIntrinsicOPSEL<llvm_anyint_ty, llvm_anyint_ty>;
}

//===----------------------------------------------------------------------===//
// GFX12 Intrinsics
//===----------------------------------------------------------------------===//

// llvm.amdgcn.permlane16.var <old> <src0> <src1> <fi> <bound_control>
def int_amdgcn_permlane16_var : ClangBuiltin<"__builtin_amdgcn_permlane16_var">,
  Intrinsic<[llvm_i32_ty],
            [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i1_ty, llvm_i1_ty],
            [IntrNoMem, IntrConvergent, IntrWillReturn,
             ImmArg<ArgIndex<3>>, ImmArg<ArgIndex<4>>, IntrNoCallback, IntrNoFree]>;

// llvm.amdgcn.permlanex16.var <old> <src0> <src1> <fi> <bound_control>
def int_amdgcn_permlanex16_var : ClangBuiltin<"__builtin_amdgcn_permlanex16_var">,
  Intrinsic<[llvm_i32_ty],
            [llvm_i32_ty, llvm_i32_ty, llvm_i32_ty, llvm_i1_ty, llvm_i1_ty],
            [IntrNoMem, IntrConvergent, IntrWillReturn,
             ImmArg<ArgIndex<3>>, ImmArg<ArgIndex<4>>, IntrNoCallback, IntrNoFree]>;

// SWMMAC (Wave Matrix(sparse) Multiply-Accumulate) intrinsics
//
// These operations perform a sparse matrix multiplication and accumulation of
// the form: D = A * B + C.
// A is sparse matrix, half the size of B, and is expanded using sparsity index.

class AMDGPUSWmmacIntrinsicIdx<LLVMType A, LLVMType B, LLVMType CD, LLVMType Index> :
  Intrinsic<
    [CD],               // %D
    [
      A,                // %A
      B,                // %B
      LLVMMatchType<0>, // %C
      Index             // %Sparsity index for A
    ],
    [IntrNoMem, IntrConvergent, IntrWillReturn]
>;

class AMDGPUSWmmacIntrinsicIUIdx<LLVMType A, LLVMType B, LLVMType CD, LLVMType Index> :
  Intrinsic<
    [CD],               // %D
    [
      llvm_i1_ty,       // %A_sign
      A,                // %A
      llvm_i1_ty,       // %B_sign
      B,                // %B
      LLVMMatchType<0>, // %C
      Index,            // %Sparsity index for A
      llvm_i1_ty,       // %clamp
    ],
    [IntrNoMem, IntrConvergent, IntrWillReturn, ImmArg<ArgIndex<0>>, ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<6>>]
>;

defset list<Intrinsic> AMDGPUWMMAIntrinsicsGFX12 = {
// WMMA (Wave Matrix Multiply-Accumulate) intrinsics
//
// These operations perform a matrix multiplication and accumulation of
// the form: D = A * B + C .

// A and B are <8 x fp8> or <8 x bf8>, but since fp8 and bf8 are not supported by llvm we use <2 x i32>.
def int_amdgcn_wmma_f32_16x16x16_fp8_fp8 : AMDGPUWmmaIntrinsic<llvm_anyint_ty, llvm_anyfloat_ty>;
def int_amdgcn_wmma_f32_16x16x16_fp8_bf8 : AMDGPUWmmaIntrinsic<llvm_anyint_ty, llvm_anyfloat_ty>;
def int_amdgcn_wmma_f32_16x16x16_bf8_fp8 : AMDGPUWmmaIntrinsic<llvm_anyint_ty, llvm_anyfloat_ty>;
def int_amdgcn_wmma_f32_16x16x16_bf8_bf8 : AMDGPUWmmaIntrinsic<llvm_anyint_ty, llvm_anyfloat_ty>;
// A and B are <16 x iu4>.
def int_amdgcn_wmma_i32_16x16x32_iu4     : AMDGPUWmmaIntrinsicIU<llvm_anyint_ty, llvm_anyint_ty>;

def int_amdgcn_swmmac_f32_16x16x32_f16     : AMDGPUSWmmacIntrinsicIdx<llvm_anyfloat_ty, llvm_anyfloat_ty, llvm_anyfloat_ty, llvm_anyint_ty>;
def int_amdgcn_swmmac_f32_16x16x32_bf16    : AMDGPUSWmmacIntrinsicIdx<llvm_anyint_ty, llvm_anyint_ty, llvm_anyfloat_ty, llvm_anyint_ty>;
def int_amdgcn_swmmac_f16_16x16x32_f16     : AMDGPUSWmmacIntrinsicIdx<llvm_anyfloat_ty, llvm_anyfloat_ty, llvm_anyfloat_ty, llvm_anyint_ty>;
def int_amdgcn_swmmac_bf16_16x16x32_bf16   : AMDGPUSWmmacIntrinsicIdx<llvm_anyint_ty, llvm_anyint_ty, llvm_anyint_ty, llvm_anyint_ty>;
def int_amdgcn_swmmac_i32_16x16x32_iu8     : AMDGPUSWmmacIntrinsicIUIdx<llvm_anyint_ty, llvm_anyint_ty, llvm_anyint_ty, llvm_anyint_ty>;
def int_amdgcn_swmmac_i32_16x16x32_iu4     : AMDGPUSWmmacIntrinsicIUIdx<llvm_anyint_ty, llvm_anyint_ty, llvm_anyint_ty, llvm_anyint_ty>;
def int_amdgcn_swmmac_i32_16x16x64_iu4     : AMDGPUSWmmacIntrinsicIUIdx<llvm_anyint_ty, llvm_anyint_ty, llvm_anyint_ty, llvm_anyint_ty>;
def int_amdgcn_swmmac_f32_16x16x32_fp8_fp8 : AMDGPUSWmmacIntrinsicIdx<llvm_anyint_ty, llvm_anyint_ty, llvm_anyfloat_ty, llvm_anyint_ty>;
def int_amdgcn_swmmac_f32_16x16x32_fp8_bf8 : AMDGPUSWmmacIntrinsicIdx<llvm_anyint_ty, llvm_anyint_ty, llvm_anyfloat_ty, llvm_anyint_ty>;
def int_amdgcn_swmmac_f32_16x16x32_bf8_fp8 : AMDGPUSWmmacIntrinsicIdx<llvm_anyint_ty, llvm_anyint_ty, llvm_anyfloat_ty, llvm_anyint_ty>;
def int_amdgcn_swmmac_f32_16x16x32_bf8_bf8 : AMDGPUSWmmacIntrinsicIdx<llvm_anyint_ty, llvm_anyint_ty, llvm_anyfloat_ty, llvm_anyint_ty>;
}

def int_amdgcn_global_atomic_ordered_add_b64 : AMDGPUAtomicRtn<llvm_i64_ty, global_ptr_ty>;

def int_amdgcn_flat_atomic_fmin_num   : AMDGPUAtomicRtn<llvm_anyfloat_ty>;
def int_amdgcn_flat_atomic_fmax_num   : AMDGPUAtomicRtn<llvm_anyfloat_ty>;
def int_amdgcn_global_atomic_fmin_num : AMDGPUAtomicRtn<llvm_anyfloat_ty>;
def int_amdgcn_global_atomic_fmax_num : AMDGPUAtomicRtn<llvm_anyfloat_ty>;

def int_amdgcn_atomic_cond_sub_u32 : AMDGPUAtomicRtn<llvm_i32_ty>;

class AMDGPULoadIntrinsic<LLVMType ptr_ty>:
  Intrinsic<
    [llvm_any_ty],
    [ptr_ty],
    [IntrReadMem, IntrWillReturn, IntrConvergent, NoCapture<ArgIndex<0>>, IntrNoCallback, IntrNoFree],
    "",
    [SDNPMemOperand]
  >;

// Wave32
// <2 x i32>    @llvm.amdgcn.global.load.tr.b64.v2i32(ptr addrspace(1))  -> global_load_tr_b64
// <8 x i16>    @llvm.amdgcn.global.load.tr.b128.v8i16(ptr addrspace(1))  -> global_load_tr_b128
// Wave64
// i32          @llvm.amdgcn.global.load.tr.b64.i32(ptr addrspace(1))    -> global_load_tr_b64
// <4 x i16>    @llvm.amdgcn.global.load.tr.b128.v4i16(ptr addrspace(1))  -> global_load_tr_b128

def int_amdgcn_global_load_tr_b64  : AMDGPULoadIntrinsic<global_ptr_ty>;
def int_amdgcn_global_load_tr_b128 : AMDGPULoadIntrinsic<global_ptr_ty>;

// i32 @llvm.amdgcn.wave.id()
def int_amdgcn_wave_id :
  DefaultAttrsIntrinsic<[llvm_i32_ty], [], [NoUndef<RetIndex>, IntrNoMem, IntrSpeculatable]>;

//===----------------------------------------------------------------------===//
// Deep learning intrinsics.
//===----------------------------------------------------------------------===//

// f32 %r = llvm.amdgcn.fdot2(v2f16 %a, v2f16 %b, f32 %c, i1 %clamp)
//   %r = %a[0] * %b[0] + %a[1] * %b[1] + %c
def int_amdgcn_fdot2 :
  ClangBuiltin<"__builtin_amdgcn_fdot2">,
  DefaultAttrsIntrinsic<
    [llvm_float_ty], // %r
    [
      llvm_v2f16_ty, // %a
      llvm_v2f16_ty, // %b
      llvm_float_ty, // %c
      llvm_i1_ty     // %clamp
    ],
    [IntrNoMem, IntrSpeculatable, ImmArg<ArgIndex<3>>]
  >;

// f16 %r = llvm.amdgcn.fdot2.f16.f16(v2f16 %a, v2f16 %b, f16 %c)
//   %r = %a[0] * %b[0] + %a[1] * %b[1] + %c
def int_amdgcn_fdot2_f16_f16 :
  ClangBuiltin<"__builtin_amdgcn_fdot2_f16_f16">,
  DefaultAttrsIntrinsic<
    [llvm_half_ty],  // %r
    [
      llvm_v2f16_ty, // %a
      llvm_v2f16_ty, // %b
      llvm_half_ty   // %c
    ],
    [IntrNoMem, IntrSpeculatable]
  >;

// bf16 %r = llvm.amdgcn.fdot2.bf16.bf16(v2bf16 %a, v2bf16 %b, bf16 %c)
//   %r = %a[0] * %b[0] + %a[1] * %b[1] + %c
def int_amdgcn_fdot2_bf16_bf16 :
  ClangBuiltin<"__builtin_amdgcn_fdot2_bf16_bf16">,
  DefaultAttrsIntrinsic<
    [llvm_bfloat_ty],   // %r
    [
      llvm_v2bf16_ty, // %a
      llvm_v2bf16_ty, // %b
      llvm_bfloat_ty    // %c
    ],
    [IntrNoMem, IntrSpeculatable]
  >;

// f32 %r = llvm.amdgcn.fdot2.f32.bf16(v2bf16 %a, v2bf16 %b, f32 %c, i1 %clamp)
//   %r = %a[0] * %b[0] + %a[1] * %b[1] + %c
def int_amdgcn_fdot2_f32_bf16 :
  ClangBuiltin<"__builtin_amdgcn_fdot2_f32_bf16">,
  DefaultAttrsIntrinsic<
    [llvm_float_ty], // %r
    [
      llvm_v2bf16_ty, // %a
      llvm_v2bf16_ty, // %b
      llvm_float_ty, // %c
      llvm_i1_ty     // %clamp
    ],
    [IntrNoMem, IntrSpeculatable, ImmArg<ArgIndex<3>>]
  >;

// i32 %r = llvm.amdgcn.sdot2(v2i16 %a, v2i16 %b, i32 %c, i1 %clamp)
//   %r = %a[0] * %b[0] + %a[1] * %b[1] + %c
def int_amdgcn_sdot2 :
  ClangBuiltin<"__builtin_amdgcn_sdot2">,
  DefaultAttrsIntrinsic<
    [llvm_i32_ty], // %r
    [
      llvm_v2i16_ty, // %a
      llvm_v2i16_ty, // %b
      llvm_i32_ty,   // %c
      llvm_i1_ty     // %clamp
    ],
    [IntrNoMem, IntrSpeculatable, ImmArg<ArgIndex<3>>]
  >;

// u32 %r = llvm.amdgcn.udot2(v2u16 %a, v2u16 %b, u32 %c, i1 %clamp)
//   %r = %a[0] * %b[0] + %a[1] * %b[1] + %c
def int_amdgcn_udot2 :
  ClangBuiltin<"__builtin_amdgcn_udot2">,
  DefaultAttrsIntrinsic<
    [llvm_i32_ty], // %r
    [
      llvm_v2i16_ty, // %a
      llvm_v2i16_ty, // %b
      llvm_i32_ty,   // %c
      llvm_i1_ty     // %clamp
    ],
    [IntrNoMem, IntrSpeculatable, ImmArg<ArgIndex<3>>]
  >;

// i32 %r = llvm.amdgcn.sdot4(v4i8 (as i32) %a, v4i8 (as i32) %b, i32 %c, i1 %clamp)
//   %r = %a[0] * %b[0] + %a[1] * %b[1] + %a[2] * %b[2] + %a[3] * %b[3] + %c
def int_amdgcn_sdot4 :
  ClangBuiltin<"__builtin_amdgcn_sdot4">,
  DefaultAttrsIntrinsic<
    [llvm_i32_ty], // %r
    [
      llvm_i32_ty, // %a
      llvm_i32_ty, // %b
      llvm_i32_ty, // %c
      llvm_i1_ty   // %clamp
    ],
    [IntrNoMem, IntrSpeculatable, ImmArg<ArgIndex<3>>]
  >;

// u32 %r = llvm.amdgcn.udot4(v4u8 (as u32) %a, v4u8 (as u32) %b, u32 %c, i1 %clamp)
//   %r = %a[0] * %b[0] + %a[1] * %b[1] + %a[2] * %b[2] + %a[3] * %b[3] + %c
def int_amdgcn_udot4 :
  ClangBuiltin<"__builtin_amdgcn_udot4">,
  DefaultAttrsIntrinsic<
    [llvm_i32_ty], // %r
    [
      llvm_i32_ty, // %a
      llvm_i32_ty, // %b
      llvm_i32_ty, // %c
      llvm_i1_ty   // %clamp
    ],
    [IntrNoMem, IntrSpeculatable, ImmArg<ArgIndex<3>>]
  >;

// i32 %r = llvm.amdgcn.sudot4(i1 %a_sign, v4i8 (as i32) %a, i1 %b_sign, v4i8 (as i32) %b, i32 %c, i1 %clamp)
// Treat input as signed (_sign = 1) or unsigned (_sign = 0).
// a[i in 0. . . 3] = (%a_sign ? a.i8[i] : promoteToSigned(a.u8[i]));
// b[i in 0. . . 3] = (%b_sign ? b.i8[i] : promoteToSigned(b.u8[i]));
//   %r = %a[0] * %b[0] + %a[1] * %b[1] + %a[2] * %b[2] + %a[3] * %b[3] + %c
def int_amdgcn_sudot4 :
  ClangBuiltin<"__builtin_amdgcn_sudot4">,
  DefaultAttrsIntrinsic<
    [llvm_i32_ty], // %r
    [
      llvm_i1_ty,  // %a_sign
      llvm_i32_ty, // %a
      llvm_i1_ty,  // %b_sign
      llvm_i32_ty, // %b
      llvm_i32_ty, // %c
      llvm_i1_ty   // %clamp
    ],
    [IntrNoMem, IntrSpeculatable,
     ImmArg<ArgIndex<0>>, ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<5>>]
  >;

// i32 %r = llvm.amdgcn.sdot8(v8i4 (as i32) %a, v8i4 (as i32) %b, i32 %c, i1 %clamp)
//   %r = %a[0] * %b[0] + %a[1] * %b[1] + %a[2] * %b[2] + %a[3] * %b[3] +
//        %a[4] * %b[4] + %a[5] * %b[5] + %a[6] * %b[6] + %a[7] * %b[7] + %c
def int_amdgcn_sdot8 :
  ClangBuiltin<"__builtin_amdgcn_sdot8">,
  DefaultAttrsIntrinsic<
    [llvm_i32_ty], // %r
    [
      llvm_i32_ty, // %a
      llvm_i32_ty, // %b
      llvm_i32_ty, // %c
      llvm_i1_ty   // %clamp
    ],
    [IntrNoMem, IntrSpeculatable, ImmArg<ArgIndex<3>>]
  >;

// u32 %r = llvm.amdgcn.udot8(v8u4 (as u32) %a, v8u4 (as u32) %b, u32 %c, i1 %clamp)
//   %r = %a[0] * %b[0] + %a[1] * %b[1] + %a[2] * %b[2] + %a[3] * %b[3] +
//        %a[4] * %b[4] + %a[5] * %b[5] + %a[6] * %b[6] + %a[7] * %b[7] + %c
def int_amdgcn_udot8 :
  ClangBuiltin<"__builtin_amdgcn_udot8">,
  DefaultAttrsIntrinsic<
    [llvm_i32_ty], // %r
    [
      llvm_i32_ty, // %a
      llvm_i32_ty, // %b
      llvm_i32_ty, // %c
      llvm_i1_ty   // %clamp
    ],
    [IntrNoMem, IntrSpeculatable, ImmArg<ArgIndex<3>>]
  >;

// i32 %r = llvm.amdgcn.sudot8(i1 %a_sign, v8i4 (as i32) %a, i1 %b_sign, v8i4 (as i32) %b, i32 %c, i1 %clamp)
// Treat input as signed (_sign = 1) or unsigned (_sign = 0).
// a[i in 0. . . 7] = (%a_sign ? a.i4[i] : promoteToSigned(a.u4[i]));
// b[i in 0. . . 7] = (%b_sign ? b.i4[i] : promoteToSigned(b.u4[i]));
//   %r = %a[0] * %b[0] + %a[1] * %b[1] + %a[2] * %b[2] + %a[3] * %b[3] +
//        %a[4] * %b[4] + %a[5] * %b[5] + %a[6] * %b[6] + %a[7] * %b[7] + %c
  def int_amdgcn_sudot8 :
  ClangBuiltin<"__builtin_amdgcn_sudot8">,
  DefaultAttrsIntrinsic<
    [llvm_i32_ty], // %r
    [
      llvm_i1_ty,  // %a_sign
      llvm_i32_ty, // %a
      llvm_i1_ty,  // %b_sign
      llvm_i32_ty, // %b
      llvm_i32_ty, // %c
      llvm_i1_ty   // %clamp
    ],
    [IntrNoMem, IntrSpeculatable,
     ImmArg<ArgIndex<0>>,  ImmArg<ArgIndex<2>>, ImmArg<ArgIndex<5>>]
  >;

// f32 %r = llvm.amdgcn.dot4.f32.type_a.type_b (v4type_a (as i32) %a, v4type_b (as i32) %b, f32 %c)
//   %r = %a[0] * %b[0] + %a[1] * %b[1] + %a[2] * %b[2] + %a[3] * %b[3] + %c
class AMDGPU8bitFloatDot4Intrinsic :
  ClangBuiltin<!subst("int", "__builtin", NAME)>,
  DefaultAttrsIntrinsic<
    [llvm_float_ty], // %r
    [
      llvm_i32_ty,   // %a
      llvm_i32_ty,   // %b
      llvm_float_ty, // %c
    ],
    [IntrNoMem, IntrSpeculatable]
  >;

def int_amdgcn_dot4_f32_fp8_bf8 : AMDGPU8bitFloatDot4Intrinsic;
def int_amdgcn_dot4_f32_bf8_fp8 : AMDGPU8bitFloatDot4Intrinsic;
def int_amdgcn_dot4_f32_fp8_fp8 : AMDGPU8bitFloatDot4Intrinsic;
def int_amdgcn_dot4_f32_bf8_bf8 : AMDGPU8bitFloatDot4Intrinsic;

//===----------------------------------------------------------------------===//
// gfx908 intrinsics
// ===----------------------------------------------------------------------===//

def int_amdgcn_global_atomic_fadd : AMDGPUAtomicRtn<llvm_anyfloat_ty>;

// llvm.amdgcn.mfma.*.* vdst, srcA, srcB, srcC, cbsz, abid, blgp
class AMDGPUMfmaIntrinsic<LLVMType DestTy, LLVMType SrcABTy> :
  ClangBuiltin<!subst("int", "__builtin", NAME)>,
  DefaultAttrsIntrinsic<[DestTy],
            [SrcABTy, SrcABTy, DestTy,
             llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
            [IntrConvergent, IntrNoMem,
             ImmArg<ArgIndex<3>>, ImmArg<ArgIndex<4>>, ImmArg<ArgIndex<5>>]>;

defset list<Intrinsic> AMDGPUMFMAIntrinsics908 = {
def int_amdgcn_mfma_f32_32x32x1f32  : AMDGPUMfmaIntrinsic<llvm_v32f32_ty, llvm_float_ty>;
def int_amdgcn_mfma_f32_16x16x1f32  : AMDGPUMfmaIntrinsic<llvm_v16f32_ty, llvm_float_ty>;
def int_amdgcn_mfma_f32_4x4x1f32    : AMDGPUMfmaIntrinsic<llvm_v4f32_ty,  llvm_float_ty>;
def int_amdgcn_mfma_f32_32x32x2f32  : AMDGPUMfmaIntrinsic<llvm_v16f32_ty, llvm_float_ty>;
def int_amdgcn_mfma_f32_16x16x4f32  : AMDGPUMfmaIntrinsic<llvm_v4f32_ty,  llvm_float_ty>;
def int_amdgcn_mfma_f32_32x32x4f16  : AMDGPUMfmaIntrinsic<llvm_v32f32_ty, llvm_v4f16_ty>;
def int_amdgcn_mfma_f32_16x16x4f16  : AMDGPUMfmaIntrinsic<llvm_v16f32_ty, llvm_v4f16_ty>;
def int_amdgcn_mfma_f32_4x4x4f16    : AMDGPUMfmaIntrinsic<llvm_v4f32_ty,  llvm_v4f16_ty>;
def int_amdgcn_mfma_f32_32x32x8f16  : AMDGPUMfmaIntrinsic<llvm_v16f32_ty, llvm_v4f16_ty>;
def int_amdgcn_mfma_f32_16x16x16f16 : AMDGPUMfmaIntrinsic<llvm_v4f32_ty,  llvm_v4f16_ty>;
def int_amdgcn_mfma_i32_32x32x4i8   : AMDGPUMfmaIntrinsic<llvm_v32i32_ty, llvm_i32_ty>;
def int_amdgcn_mfma_i32_16x16x4i8   : AMDGPUMfmaIntrinsic<llvm_v16i32_ty, llvm_i32_ty>;
def int_amdgcn_mfma_i32_4x4x4i8     : AMDGPUMfmaIntrinsic<llvm_v4i32_ty,  llvm_i32_ty>;
def int_amdgcn_mfma_i32_32x32x8i8   : AMDGPUMfmaIntrinsic<llvm_v16i32_ty, llvm_i32_ty>;
def int_amdgcn_mfma_i32_16x16x16i8  : AMDGPUMfmaIntrinsic<llvm_v4i32_ty,  llvm_i32_ty>;
def int_amdgcn_mfma_f32_32x32x2bf16 : AMDGPUMfmaIntrinsic<llvm_v32f32_ty, llvm_v2i16_ty>;
def int_amdgcn_mfma_f32_16x16x2bf16 : AMDGPUMfmaIntrinsic<llvm_v16f32_ty, llvm_v2i16_ty>;
def int_amdgcn_mfma_f32_4x4x2bf16   : AMDGPUMfmaIntrinsic<llvm_v4f32_ty,  llvm_v2i16_ty>;
def int_amdgcn_mfma_f32_32x32x4bf16 : AMDGPUMfmaIntrinsic<llvm_v16f32_ty, llvm_v2i16_ty>;
def int_amdgcn_mfma_f32_16x16x8bf16 : AMDGPUMfmaIntrinsic<llvm_v4f32_ty,  llvm_v2i16_ty>;
}

//===----------------------------------------------------------------------===//
// gfx90a intrinsics
// ===----------------------------------------------------------------------===//

def int_amdgcn_global_atomic_fmin : AMDGPUAtomicRtn<llvm_anyfloat_ty>;
def int_amdgcn_global_atomic_fmax : AMDGPUAtomicRtn<llvm_anyfloat_ty>;
def int_amdgcn_flat_atomic_fadd   : AMDGPUAtomicRtn<llvm_anyfloat_ty>;
def int_amdgcn_flat_atomic_fmin   : AMDGPUAtomicRtn<llvm_anyfloat_ty>;
def int_amdgcn_flat_atomic_fmax   : AMDGPUAtomicRtn<llvm_anyfloat_ty>;

defset list<Intrinsic> AMDGPUMFMAIntrinsics90A = {
def int_amdgcn_mfma_f32_32x32x4bf16_1k  : AMDGPUMfmaIntrinsic<llvm_v32f32_ty, llvm_v4i16_ty>;
def int_amdgcn_mfma_f32_16x16x4bf16_1k  : AMDGPUMfmaIntrinsic<llvm_v16f32_ty, llvm_v4i16_ty>;
def int_amdgcn_mfma_f32_4x4x4bf16_1k    : AMDGPUMfmaIntrinsic<llvm_v4f32_ty,  llvm_v4i16_ty>;
def int_amdgcn_mfma_f32_32x32x8bf16_1k  : AMDGPUMfmaIntrinsic<llvm_v16f32_ty, llvm_v4i16_ty>;
def int_amdgcn_mfma_f32_16x16x16bf16_1k : AMDGPUMfmaIntrinsic<llvm_v4f32_ty,  llvm_v4i16_ty>;

// Note: in gfx940 BLGP argument is replaced by NEG bitfield in the DGEMM MFMA.
//       Three bits corresponding to the neg modifier applied to the respective
//       source operand.
def int_amdgcn_mfma_f64_16x16x4f64      : AMDGPUMfmaIntrinsic<llvm_v4f64_ty,  llvm_double_ty>;
def int_amdgcn_mfma_f64_4x4x4f64        : AMDGPUMfmaIntrinsic<llvm_double_ty, llvm_double_ty>;
}

//===----------------------------------------------------------------------===//
// gfx940 intrinsics
// ===----------------------------------------------------------------------===//

class AMDGPUMFp8MfmaIntrinsic<LLVMType DestTy> :
  AMDGPUMfmaIntrinsic<DestTy, llvm_i64_ty>;

multiclass AMDGPUMFp8MfmaIntrinsic<LLVMType DestTy> {
  foreach kind = ["bf8_bf8", "bf8_fp8", "fp8_bf8", "fp8_fp8"] in
    def NAME#"_"#kind : AMDGPUMFp8MfmaIntrinsic<DestTy>;
}

// llvm.amdgcn.smfmac.?32.* vdst, srcA, srcB, srcC, index, cbsz, abid
class AMDGPUMSmfmacIntrinsic<LLVMType DestTy, LLVMType SrcA, LLVMType SrcB> :
  ClangBuiltin<!subst("int", "__builtin", NAME)>,
  DefaultAttrsIntrinsic<[DestTy],
            [SrcA, SrcB, DestTy, llvm_i32_ty,
             llvm_i32_ty, llvm_i32_ty],
            [IntrConvergent, IntrNoMem,
             ImmArg<ArgIndex<4>>, ImmArg<ArgIndex<5>>]>;

class AMDGPUMFp8SmfmacIntrinsic<LLVMType DestTy> :
  AMDGPUMSmfmacIntrinsic<DestTy, llvm_v2i32_ty, llvm_v4i32_ty>;

multiclass AMDGPUMFp8SmfmacIntrinsic<LLVMType DestTy> {
  foreach kind = ["bf8_bf8", "bf8_fp8", "fp8_bf8", "fp8_fp8"] in
    def NAME#"_"#kind : AMDGPUMFp8SmfmacIntrinsic<DestTy>;
}

// bf16 atomics use v2i16 argument since there is no bf16 data type in the llvm.
def int_amdgcn_global_atomic_fadd_v2bf16 : AMDGPUAtomicRtn<llvm_v2i16_ty>;
def int_amdgcn_flat_atomic_fadd_v2bf16   : AMDGPUAtomicRtn<llvm_v2i16_ty>;

defset list<Intrinsic> AMDGPUMFMAIntrinsics940 = {
def int_amdgcn_mfma_i32_16x16x32_i8     : AMDGPUMfmaIntrinsic<llvm_v4i32_ty,  llvm_i64_ty>;
def int_amdgcn_mfma_i32_32x32x16_i8     : AMDGPUMfmaIntrinsic<llvm_v16i32_ty, llvm_i64_ty>;
def int_amdgcn_mfma_f32_16x16x8_xf32    : AMDGPUMfmaIntrinsic<llvm_v4f32_ty,  llvm_v2f32_ty>;
def int_amdgcn_mfma_f32_32x32x4_xf32    : AMDGPUMfmaIntrinsic<llvm_v16f32_ty, llvm_v2f32_ty>;

defm int_amdgcn_mfma_f32_16x16x32 : AMDGPUMFp8MfmaIntrinsic<llvm_v4f32_ty>;
defm int_amdgcn_mfma_f32_32x32x16 : AMDGPUMFp8MfmaIntrinsic<llvm_v16f32_ty>;

def int_amdgcn_smfmac_f32_16x16x32_f16  : AMDGPUMSmfmacIntrinsic<llvm_v4f32_ty,  llvm_v4f16_ty, llvm_v8f16_ty>;
def int_amdgcn_smfmac_f32_32x32x16_f16  : AMDGPUMSmfmacIntrinsic<llvm_v16f32_ty, llvm_v4f16_ty, llvm_v8f16_ty>;
def int_amdgcn_smfmac_f32_16x16x32_bf16 : AMDGPUMSmfmacIntrinsic<llvm_v4f32_ty,  llvm_v4i16_ty, llvm_v8i16_ty>;
def int_amdgcn_smfmac_f32_32x32x16_bf16 : AMDGPUMSmfmacIntrinsic<llvm_v16f32_ty, llvm_v4i16_ty, llvm_v8i16_ty>;
def int_amdgcn_smfmac_i32_16x16x64_i8   : AMDGPUMSmfmacIntrinsic<llvm_v4i32_ty,  llvm_v2i32_ty, llvm_v4i32_ty>;
def int_amdgcn_smfmac_i32_32x32x32_i8   : AMDGPUMSmfmacIntrinsic<llvm_v16i32_ty, llvm_v2i32_ty, llvm_v4i32_ty>;

defm int_amdgcn_smfmac_f32_16x16x64 : AMDGPUMFp8SmfmacIntrinsic<llvm_v4f32_ty>;
defm int_amdgcn_smfmac_f32_32x32x32 : AMDGPUMFp8SmfmacIntrinsic<llvm_v16f32_ty>;
}

// llvm.amdgcn.cvt.f32.bf8 float vdst, int srcA, imm byte_sel [0..3]
// byte_sel selects byte from srcA.
def int_amdgcn_cvt_f32_bf8 : ClangBuiltin<"__builtin_amdgcn_cvt_f32_bf8">,
  DefaultAttrsIntrinsic<[llvm_float_ty],
            [llvm_i32_ty, llvm_i32_ty],
            [IntrNoMem, ImmArg<ArgIndex<1>>]>;

// llvm.amdgcn.cvt.f32.fp8 float vdst, int srcA, imm byte_sel [0..3]
def int_amdgcn_cvt_f32_fp8 : ClangBuiltin<"__builtin_amdgcn_cvt_f32_fp8">,
  DefaultAttrsIntrinsic<[llvm_float_ty],
            [llvm_i32_ty, llvm_i32_ty],
            [IntrNoMem, ImmArg<ArgIndex<1>>]>;

// llvm.amdgcn.cvt.pk.f32.bf8 float2 vdst, int srcA, imm word_sel
// word_sel = 1 selects 2 high bytes, 0 selects 2 low bytes.
def int_amdgcn_cvt_pk_f32_bf8 : ClangBuiltin<"__builtin_amdgcn_cvt_pk_f32_bf8">,
  DefaultAttrsIntrinsic<[llvm_v2f32_ty],
            [llvm_i32_ty, llvm_i1_ty],
            [IntrNoMem, ImmArg<ArgIndex<1>>]>;

// llvm.amdgcn.cvt.pk.f32.fp8 float2 vdst, int srcA, imm word_sel.
def int_amdgcn_cvt_pk_f32_fp8 : ClangBuiltin<"__builtin_amdgcn_cvt_pk_f32_fp8">,
  DefaultAttrsIntrinsic<[llvm_v2f32_ty],
            [llvm_i32_ty, llvm_i1_ty],
            [IntrNoMem, ImmArg<ArgIndex<1>>]>;

// llvm.amdgcn.cvt.pk.bf8.f32 int vdst, float srcA, float srcB, int old, imm word_sel
// word_sel = 1 selects 2 high bytes in the vdst, 0 selects 2 low bytes.
def int_amdgcn_cvt_pk_bf8_f32 : ClangBuiltin<"__builtin_amdgcn_cvt_pk_bf8_f32">,
  DefaultAttrsIntrinsic<[llvm_i32_ty],
            [llvm_float_ty, llvm_float_ty, llvm_i32_ty, llvm_i1_ty],
            [IntrNoMem, ImmArg<ArgIndex<3>>]>;

// llvm.amdgcn.cvt.pk.fp8.f32 int vdst, float srcA, float srcB, int old, imm word_sel
def int_amdgcn_cvt_pk_fp8_f32 : ClangBuiltin<"__builtin_amdgcn_cvt_pk_fp8_f32">,
  DefaultAttrsIntrinsic<[llvm_i32_ty],
            [llvm_float_ty, llvm_float_ty, llvm_i32_ty, llvm_i1_ty],
            [IntrNoMem, ImmArg<ArgIndex<3>>]>;

// llvm.amdgcn.cvt.sr.bf8.f32 int vdst, float srcA, int srcB, int old, imm byte_sel [0..3]
// byte_sel selects byte to write into vdst.
def int_amdgcn_cvt_sr_bf8_f32 : ClangBuiltin<"__builtin_amdgcn_cvt_sr_bf8_f32">,
  DefaultAttrsIntrinsic<[llvm_i32_ty],
            [llvm_float_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
            [IntrNoMem, ImmArg<ArgIndex<3>>]>;

// llvm.amdgcn.cvt.sr.fp8.f32 int vdst, float srcA, int srcB, int old, imm byte_sel [0..3]
def int_amdgcn_cvt_sr_fp8_f32 : ClangBuiltin<"__builtin_amdgcn_cvt_sr_fp8_f32">,
  DefaultAttrsIntrinsic<[llvm_i32_ty],
            [llvm_float_ty, llvm_i32_ty, llvm_i32_ty, llvm_i32_ty],
            [IntrNoMem, ImmArg<ArgIndex<3>>]>;

//===----------------------------------------------------------------------===//
// Special Intrinsics for backend internal use only. No frontend
// should emit calls to these.
// ===----------------------------------------------------------------------===//
//
// Control-flow intrinsics in LLVM IR are convergent because they represent the
// wave CFG, i.e., sets of threads that are "converged" or "execute in
// lock-step". But they exist during a small window in the lowering process,
// inserted after the structurizer and then translated to equivalent MIR
// pseudos. So rather than create convergence tokens for these builtins, we
// simply mark them as not convergent.
//
// This is really a workaround to allow control flow lowering in the presence of
// convergence control tokens. The corresponding MIR pseudos are marked as
// having side effects, which is sufficient to prevent optimizations without
// having to mark them as convergent.
def int_amdgcn_if : Intrinsic<[llvm_i1_ty, llvm_anyint_ty],
  [llvm_i1_ty], [IntrWillReturn, IntrNoCallback, IntrNoFree]
>;

def int_amdgcn_else : Intrinsic<[llvm_i1_ty, llvm_anyint_ty],
  [llvm_anyint_ty], [IntrWillReturn, IntrNoCallback, IntrNoFree]
>;

def int_amdgcn_if_break : Intrinsic<[llvm_anyint_ty],
  [llvm_i1_ty, LLVMMatchType<0>],
  [IntrNoMem, IntrWillReturn, IntrNoCallback, IntrNoFree]
>;

def int_amdgcn_loop : Intrinsic<[llvm_i1_ty],
  [llvm_anyint_ty], [IntrWillReturn, IntrNoCallback, IntrNoFree]
>;

def int_amdgcn_end_cf : Intrinsic<[], [llvm_anyint_ty],
  [IntrWillReturn, IntrNoCallback, IntrNoFree]>;

// Represent unreachable in a divergent region.
def int_amdgcn_unreachable : Intrinsic<[], [], [IntrConvergent, IntrNoCallback, IntrNoFree]>;

// Emit 2.5 ulp, no denormal division. Should only be inserted by
// pass based on !fpmath metadata.
def int_amdgcn_fdiv_fast : DefaultAttrsIntrinsic<
  [llvm_float_ty], [llvm_float_ty, llvm_float_ty],
  [IntrNoMem, IntrSpeculatable]
>;

/// Emit an addrspacecast without null pointer checking.
/// Should only be inserted by a pass based on analysis of an addrspacecast's src.
def int_amdgcn_addrspacecast_nonnull : DefaultAttrsIntrinsic<
  [llvm_anyptr_ty], [llvm_anyptr_ty],
  [IntrNoMem, IntrSpeculatable]
>;
}
