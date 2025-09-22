/*===---- __clang_cuda_device_functions.h - CUDA runtime support -----------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __CLANG_CUDA_DEVICE_FUNCTIONS_H__
#define __CLANG_CUDA_DEVICE_FUNCTIONS_H__

#ifndef __OPENMP_NVPTX__
#if CUDA_VERSION < 9000
#error This file is intended to be used with CUDA-9+ only.
#endif
#endif

// __DEVICE__ is a helper macro with common set of attributes for the wrappers
// we implement in this file. We need static in order to avoid emitting unused
// functions and __forceinline__ helps inlining these wrappers at -O1.
#pragma push_macro("__DEVICE__")
#ifdef __OPENMP_NVPTX__
#define __DEVICE__ static __attribute__((always_inline, nothrow))
#else
#define __DEVICE__ static __device__ __forceinline__
#endif

__DEVICE__ int __all(int __a) { return __nvvm_vote_all(__a); }
__DEVICE__ int __any(int __a) { return __nvvm_vote_any(__a); }
__DEVICE__ unsigned int __ballot(int __a) { return __nvvm_vote_ballot(__a); }
__DEVICE__ unsigned int __brev(unsigned int __a) { return __nv_brev(__a); }
__DEVICE__ unsigned long long __brevll(unsigned long long __a) {
  return __nv_brevll(__a);
}
#if defined(__cplusplus)
__DEVICE__ void __brkpt() { __asm__ __volatile__("brkpt;"); }
__DEVICE__ void __brkpt(int __a) { __brkpt(); }
#else
__DEVICE__ void __attribute__((overloadable)) __brkpt(void) {
  __asm__ __volatile__("brkpt;");
}
__DEVICE__ void __attribute__((overloadable)) __brkpt(int __a) { __brkpt(); }
#endif
__DEVICE__ unsigned int __byte_perm(unsigned int __a, unsigned int __b,
                                    unsigned int __c) {
  return __nv_byte_perm(__a, __b, __c);
}
__DEVICE__ int __clz(int __a) { return __nv_clz(__a); }
__DEVICE__ int __clzll(long long __a) { return __nv_clzll(__a); }
__DEVICE__ float __cosf(float __a) { return __nv_fast_cosf(__a); }
__DEVICE__ double __dAtomicAdd(double *__p, double __v) {
  return __nvvm_atom_add_gen_d(__p, __v);
}
__DEVICE__ double __dAtomicAdd_block(double *__p, double __v) {
  return __nvvm_atom_cta_add_gen_d(__p, __v);
}
__DEVICE__ double __dAtomicAdd_system(double *__p, double __v) {
  return __nvvm_atom_sys_add_gen_d(__p, __v);
}
__DEVICE__ double __dadd_rd(double __a, double __b) {
  return __nv_dadd_rd(__a, __b);
}
__DEVICE__ double __dadd_rn(double __a, double __b) {
  return __nv_dadd_rn(__a, __b);
}
__DEVICE__ double __dadd_ru(double __a, double __b) {
  return __nv_dadd_ru(__a, __b);
}
__DEVICE__ double __dadd_rz(double __a, double __b) {
  return __nv_dadd_rz(__a, __b);
}
__DEVICE__ double __ddiv_rd(double __a, double __b) {
  return __nv_ddiv_rd(__a, __b);
}
__DEVICE__ double __ddiv_rn(double __a, double __b) {
  return __nv_ddiv_rn(__a, __b);
}
__DEVICE__ double __ddiv_ru(double __a, double __b) {
  return __nv_ddiv_ru(__a, __b);
}
__DEVICE__ double __ddiv_rz(double __a, double __b) {
  return __nv_ddiv_rz(__a, __b);
}
__DEVICE__ double __dmul_rd(double __a, double __b) {
  return __nv_dmul_rd(__a, __b);
}
__DEVICE__ double __dmul_rn(double __a, double __b) {
  return __nv_dmul_rn(__a, __b);
}
__DEVICE__ double __dmul_ru(double __a, double __b) {
  return __nv_dmul_ru(__a, __b);
}
__DEVICE__ double __dmul_rz(double __a, double __b) {
  return __nv_dmul_rz(__a, __b);
}
__DEVICE__ float __double2float_rd(double __a) {
  return __nv_double2float_rd(__a);
}
__DEVICE__ float __double2float_rn(double __a) {
  return __nv_double2float_rn(__a);
}
__DEVICE__ float __double2float_ru(double __a) {
  return __nv_double2float_ru(__a);
}
__DEVICE__ float __double2float_rz(double __a) {
  return __nv_double2float_rz(__a);
}
__DEVICE__ int __double2hiint(double __a) { return __nv_double2hiint(__a); }
__DEVICE__ int __double2int_rd(double __a) { return __nv_double2int_rd(__a); }
__DEVICE__ int __double2int_rn(double __a) { return __nv_double2int_rn(__a); }
__DEVICE__ int __double2int_ru(double __a) { return __nv_double2int_ru(__a); }
__DEVICE__ int __double2int_rz(double __a) { return __nv_double2int_rz(__a); }
__DEVICE__ long long __double2ll_rd(double __a) {
  return __nv_double2ll_rd(__a);
}
__DEVICE__ long long __double2ll_rn(double __a) {
  return __nv_double2ll_rn(__a);
}
__DEVICE__ long long __double2ll_ru(double __a) {
  return __nv_double2ll_ru(__a);
}
__DEVICE__ long long __double2ll_rz(double __a) {
  return __nv_double2ll_rz(__a);
}
__DEVICE__ int __double2loint(double __a) { return __nv_double2loint(__a); }
__DEVICE__ unsigned int __double2uint_rd(double __a) {
  return __nv_double2uint_rd(__a);
}
__DEVICE__ unsigned int __double2uint_rn(double __a) {
  return __nv_double2uint_rn(__a);
}
__DEVICE__ unsigned int __double2uint_ru(double __a) {
  return __nv_double2uint_ru(__a);
}
__DEVICE__ unsigned int __double2uint_rz(double __a) {
  return __nv_double2uint_rz(__a);
}
__DEVICE__ unsigned long long __double2ull_rd(double __a) {
  return __nv_double2ull_rd(__a);
}
__DEVICE__ unsigned long long __double2ull_rn(double __a) {
  return __nv_double2ull_rn(__a);
}
__DEVICE__ unsigned long long __double2ull_ru(double __a) {
  return __nv_double2ull_ru(__a);
}
__DEVICE__ unsigned long long __double2ull_rz(double __a) {
  return __nv_double2ull_rz(__a);
}
__DEVICE__ long long __double_as_longlong(double __a) {
  return __nv_double_as_longlong(__a);
}
__DEVICE__ double __drcp_rd(double __a) { return __nv_drcp_rd(__a); }
__DEVICE__ double __drcp_rn(double __a) { return __nv_drcp_rn(__a); }
__DEVICE__ double __drcp_ru(double __a) { return __nv_drcp_ru(__a); }
__DEVICE__ double __drcp_rz(double __a) { return __nv_drcp_rz(__a); }
__DEVICE__ double __dsqrt_rd(double __a) { return __nv_dsqrt_rd(__a); }
__DEVICE__ double __dsqrt_rn(double __a) { return __nv_dsqrt_rn(__a); }
__DEVICE__ double __dsqrt_ru(double __a) { return __nv_dsqrt_ru(__a); }
__DEVICE__ double __dsqrt_rz(double __a) { return __nv_dsqrt_rz(__a); }
__DEVICE__ double __dsub_rd(double __a, double __b) {
  return __nv_dsub_rd(__a, __b);
}
__DEVICE__ double __dsub_rn(double __a, double __b) {
  return __nv_dsub_rn(__a, __b);
}
__DEVICE__ double __dsub_ru(double __a, double __b) {
  return __nv_dsub_ru(__a, __b);
}
__DEVICE__ double __dsub_rz(double __a, double __b) {
  return __nv_dsub_rz(__a, __b);
}
__DEVICE__ float __exp10f(float __a) { return __nv_fast_exp10f(__a); }
__DEVICE__ float __expf(float __a) { return __nv_fast_expf(__a); }
__DEVICE__ float __fAtomicAdd(float *__p, float __v) {
  return __nvvm_atom_add_gen_f(__p, __v);
}
__DEVICE__ float __fAtomicAdd_block(float *__p, float __v) {
  return __nvvm_atom_cta_add_gen_f(__p, __v);
}
__DEVICE__ float __fAtomicAdd_system(float *__p, float __v) {
  return __nvvm_atom_sys_add_gen_f(__p, __v);
}
__DEVICE__ float __fAtomicExch(float *__p, float __v) {
  return __nv_int_as_float(
      __nvvm_atom_xchg_gen_i((int *)__p, __nv_float_as_int(__v)));
}
__DEVICE__ float __fAtomicExch_block(float *__p, float __v) {
  return __nv_int_as_float(
      __nvvm_atom_cta_xchg_gen_i((int *)__p, __nv_float_as_int(__v)));
}
__DEVICE__ float __fAtomicExch_system(float *__p, float __v) {
  return __nv_int_as_float(
      __nvvm_atom_sys_xchg_gen_i((int *)__p, __nv_float_as_int(__v)));
}
__DEVICE__ float __fadd_rd(float __a, float __b) {
  return __nv_fadd_rd(__a, __b);
}
__DEVICE__ float __fadd_rn(float __a, float __b) {
  return __nv_fadd_rn(__a, __b);
}
__DEVICE__ float __fadd_ru(float __a, float __b) {
  return __nv_fadd_ru(__a, __b);
}
__DEVICE__ float __fadd_rz(float __a, float __b) {
  return __nv_fadd_rz(__a, __b);
}
__DEVICE__ float __fdiv_rd(float __a, float __b) {
  return __nv_fdiv_rd(__a, __b);
}
__DEVICE__ float __fdiv_rn(float __a, float __b) {
  return __nv_fdiv_rn(__a, __b);
}
__DEVICE__ float __fdiv_ru(float __a, float __b) {
  return __nv_fdiv_ru(__a, __b);
}
__DEVICE__ float __fdiv_rz(float __a, float __b) {
  return __nv_fdiv_rz(__a, __b);
}
__DEVICE__ float __fdividef(float __a, float __b) {
  return __nv_fast_fdividef(__a, __b);
}
__DEVICE__ int __ffs(int __a) { return __nv_ffs(__a); }
__DEVICE__ int __ffsll(long long __a) { return __nv_ffsll(__a); }
__DEVICE__ int __finite(double __a) { return __nv_isfinited(__a); }
__DEVICE__ int __finitef(float __a) { return __nv_finitef(__a); }
#ifdef _MSC_VER
__DEVICE__ int __finitel(long double __a);
#endif
__DEVICE__ int __float2int_rd(float __a) { return __nv_float2int_rd(__a); }
__DEVICE__ int __float2int_rn(float __a) { return __nv_float2int_rn(__a); }
__DEVICE__ int __float2int_ru(float __a) { return __nv_float2int_ru(__a); }
__DEVICE__ int __float2int_rz(float __a) { return __nv_float2int_rz(__a); }
__DEVICE__ long long __float2ll_rd(float __a) { return __nv_float2ll_rd(__a); }
__DEVICE__ long long __float2ll_rn(float __a) { return __nv_float2ll_rn(__a); }
__DEVICE__ long long __float2ll_ru(float __a) { return __nv_float2ll_ru(__a); }
__DEVICE__ long long __float2ll_rz(float __a) { return __nv_float2ll_rz(__a); }
__DEVICE__ unsigned int __float2uint_rd(float __a) {
  return __nv_float2uint_rd(__a);
}
__DEVICE__ unsigned int __float2uint_rn(float __a) {
  return __nv_float2uint_rn(__a);
}
__DEVICE__ unsigned int __float2uint_ru(float __a) {
  return __nv_float2uint_ru(__a);
}
__DEVICE__ unsigned int __float2uint_rz(float __a) {
  return __nv_float2uint_rz(__a);
}
__DEVICE__ unsigned long long __float2ull_rd(float __a) {
  return __nv_float2ull_rd(__a);
}
__DEVICE__ unsigned long long __float2ull_rn(float __a) {
  return __nv_float2ull_rn(__a);
}
__DEVICE__ unsigned long long __float2ull_ru(float __a) {
  return __nv_float2ull_ru(__a);
}
__DEVICE__ unsigned long long __float2ull_rz(float __a) {
  return __nv_float2ull_rz(__a);
}
__DEVICE__ int __float_as_int(float __a) { return __nv_float_as_int(__a); }
__DEVICE__ unsigned int __float_as_uint(float __a) {
  return __nv_float_as_uint(__a);
}
__DEVICE__ double __fma_rd(double __a, double __b, double __c) {
  return __nv_fma_rd(__a, __b, __c);
}
__DEVICE__ double __fma_rn(double __a, double __b, double __c) {
  return __nv_fma_rn(__a, __b, __c);
}
__DEVICE__ double __fma_ru(double __a, double __b, double __c) {
  return __nv_fma_ru(__a, __b, __c);
}
__DEVICE__ double __fma_rz(double __a, double __b, double __c) {
  return __nv_fma_rz(__a, __b, __c);
}
__DEVICE__ float __fmaf_ieee_rd(float __a, float __b, float __c) {
  return __nv_fmaf_ieee_rd(__a, __b, __c);
}
__DEVICE__ float __fmaf_ieee_rn(float __a, float __b, float __c) {
  return __nv_fmaf_ieee_rn(__a, __b, __c);
}
__DEVICE__ float __fmaf_ieee_ru(float __a, float __b, float __c) {
  return __nv_fmaf_ieee_ru(__a, __b, __c);
}
__DEVICE__ float __fmaf_ieee_rz(float __a, float __b, float __c) {
  return __nv_fmaf_ieee_rz(__a, __b, __c);
}
__DEVICE__ float __fmaf_rd(float __a, float __b, float __c) {
  return __nv_fmaf_rd(__a, __b, __c);
}
__DEVICE__ float __fmaf_rn(float __a, float __b, float __c) {
  return __nv_fmaf_rn(__a, __b, __c);
}
__DEVICE__ float __fmaf_ru(float __a, float __b, float __c) {
  return __nv_fmaf_ru(__a, __b, __c);
}
__DEVICE__ float __fmaf_rz(float __a, float __b, float __c) {
  return __nv_fmaf_rz(__a, __b, __c);
}
__DEVICE__ float __fmul_rd(float __a, float __b) {
  return __nv_fmul_rd(__a, __b);
}
__DEVICE__ float __fmul_rn(float __a, float __b) {
  return __nv_fmul_rn(__a, __b);
}
__DEVICE__ float __fmul_ru(float __a, float __b) {
  return __nv_fmul_ru(__a, __b);
}
__DEVICE__ float __fmul_rz(float __a, float __b) {
  return __nv_fmul_rz(__a, __b);
}
__DEVICE__ float __frcp_rd(float __a) { return __nv_frcp_rd(__a); }
__DEVICE__ float __frcp_rn(float __a) { return __nv_frcp_rn(__a); }
__DEVICE__ float __frcp_ru(float __a) { return __nv_frcp_ru(__a); }
__DEVICE__ float __frcp_rz(float __a) { return __nv_frcp_rz(__a); }
__DEVICE__ float __frsqrt_rn(float __a) { return __nv_frsqrt_rn(__a); }
__DEVICE__ float __fsqrt_rd(float __a) { return __nv_fsqrt_rd(__a); }
__DEVICE__ float __fsqrt_rn(float __a) { return __nv_fsqrt_rn(__a); }
__DEVICE__ float __fsqrt_ru(float __a) { return __nv_fsqrt_ru(__a); }
__DEVICE__ float __fsqrt_rz(float __a) { return __nv_fsqrt_rz(__a); }
__DEVICE__ float __fsub_rd(float __a, float __b) {
  return __nv_fsub_rd(__a, __b);
}
__DEVICE__ float __fsub_rn(float __a, float __b) {
  return __nv_fsub_rn(__a, __b);
}
__DEVICE__ float __fsub_ru(float __a, float __b) {
  return __nv_fsub_ru(__a, __b);
}
__DEVICE__ float __fsub_rz(float __a, float __b) {
  return __nv_fsub_rz(__a, __b);
}
__DEVICE__ int __hadd(int __a, int __b) { return __nv_hadd(__a, __b); }
__DEVICE__ double __hiloint2double(int __a, int __b) {
  return __nv_hiloint2double(__a, __b);
}
__DEVICE__ int __iAtomicAdd(int *__p, int __v) {
  return __nvvm_atom_add_gen_i(__p, __v);
}
__DEVICE__ int __iAtomicAdd_block(int *__p, int __v) {
  return __nvvm_atom_cta_add_gen_i(__p, __v);
}
__DEVICE__ int __iAtomicAdd_system(int *__p, int __v) {
  return __nvvm_atom_sys_add_gen_i(__p, __v);
}
__DEVICE__ int __iAtomicAnd(int *__p, int __v) {
  return __nvvm_atom_and_gen_i(__p, __v);
}
__DEVICE__ int __iAtomicAnd_block(int *__p, int __v) {
  return __nvvm_atom_cta_and_gen_i(__p, __v);
}
__DEVICE__ int __iAtomicAnd_system(int *__p, int __v) {
  return __nvvm_atom_sys_and_gen_i(__p, __v);
}
__DEVICE__ int __iAtomicCAS(int *__p, int __cmp, int __v) {
  return __nvvm_atom_cas_gen_i(__p, __cmp, __v);
}
__DEVICE__ int __iAtomicCAS_block(int *__p, int __cmp, int __v) {
  return __nvvm_atom_cta_cas_gen_i(__p, __cmp, __v);
}
__DEVICE__ int __iAtomicCAS_system(int *__p, int __cmp, int __v) {
  return __nvvm_atom_sys_cas_gen_i(__p, __cmp, __v);
}
__DEVICE__ int __iAtomicExch(int *__p, int __v) {
  return __nvvm_atom_xchg_gen_i(__p, __v);
}
__DEVICE__ int __iAtomicExch_block(int *__p, int __v) {
  return __nvvm_atom_cta_xchg_gen_i(__p, __v);
}
__DEVICE__ int __iAtomicExch_system(int *__p, int __v) {
  return __nvvm_atom_sys_xchg_gen_i(__p, __v);
}
__DEVICE__ int __iAtomicMax(int *__p, int __v) {
  return __nvvm_atom_max_gen_i(__p, __v);
}
__DEVICE__ int __iAtomicMax_block(int *__p, int __v) {
  return __nvvm_atom_cta_max_gen_i(__p, __v);
}
__DEVICE__ int __iAtomicMax_system(int *__p, int __v) {
  return __nvvm_atom_sys_max_gen_i(__p, __v);
}
__DEVICE__ int __iAtomicMin(int *__p, int __v) {
  return __nvvm_atom_min_gen_i(__p, __v);
}
__DEVICE__ int __iAtomicMin_block(int *__p, int __v) {
  return __nvvm_atom_cta_min_gen_i(__p, __v);
}
__DEVICE__ int __iAtomicMin_system(int *__p, int __v) {
  return __nvvm_atom_sys_min_gen_i(__p, __v);
}
__DEVICE__ int __iAtomicOr(int *__p, int __v) {
  return __nvvm_atom_or_gen_i(__p, __v);
}
__DEVICE__ int __iAtomicOr_block(int *__p, int __v) {
  return __nvvm_atom_cta_or_gen_i(__p, __v);
}
__DEVICE__ int __iAtomicOr_system(int *__p, int __v) {
  return __nvvm_atom_sys_or_gen_i(__p, __v);
}
__DEVICE__ int __iAtomicXor(int *__p, int __v) {
  return __nvvm_atom_xor_gen_i(__p, __v);
}
__DEVICE__ int __iAtomicXor_block(int *__p, int __v) {
  return __nvvm_atom_cta_xor_gen_i(__p, __v);
}
__DEVICE__ int __iAtomicXor_system(int *__p, int __v) {
  return __nvvm_atom_sys_xor_gen_i(__p, __v);
}
__DEVICE__ long long __illAtomicMax(long long *__p, long long __v) {
  return __nvvm_atom_max_gen_ll(__p, __v);
}
__DEVICE__ long long __illAtomicMax_block(long long *__p, long long __v) {
  return __nvvm_atom_cta_max_gen_ll(__p, __v);
}
__DEVICE__ long long __illAtomicMax_system(long long *__p, long long __v) {
  return __nvvm_atom_sys_max_gen_ll(__p, __v);
}
__DEVICE__ long long __illAtomicMin(long long *__p, long long __v) {
  return __nvvm_atom_min_gen_ll(__p, __v);
}
__DEVICE__ long long __illAtomicMin_block(long long *__p, long long __v) {
  return __nvvm_atom_cta_min_gen_ll(__p, __v);
}
__DEVICE__ long long __illAtomicMin_system(long long *__p, long long __v) {
  return __nvvm_atom_sys_min_gen_ll(__p, __v);
}
__DEVICE__ double __int2double_rn(int __a) { return __nv_int2double_rn(__a); }
__DEVICE__ float __int2float_rd(int __a) { return __nv_int2float_rd(__a); }
__DEVICE__ float __int2float_rn(int __a) { return __nv_int2float_rn(__a); }
__DEVICE__ float __int2float_ru(int __a) { return __nv_int2float_ru(__a); }
__DEVICE__ float __int2float_rz(int __a) { return __nv_int2float_rz(__a); }
__DEVICE__ float __int_as_float(int __a) { return __nv_int_as_float(__a); }
__DEVICE__ int __isfinited(double __a) { return __nv_isfinited(__a); }
__DEVICE__ int __isinf(double __a) { return __nv_isinfd(__a); }
__DEVICE__ int __isinff(float __a) { return __nv_isinff(__a); }
#ifdef _MSC_VER
__DEVICE__ int __isinfl(long double __a);
#endif
__DEVICE__ int __isnan(double __a) { return __nv_isnand(__a); }
__DEVICE__ int __isnanf(float __a) { return __nv_isnanf(__a); }
#ifdef _MSC_VER
__DEVICE__ int __isnanl(long double __a);
#endif
__DEVICE__ double __ll2double_rd(long long __a) {
  return __nv_ll2double_rd(__a);
}
__DEVICE__ double __ll2double_rn(long long __a) {
  return __nv_ll2double_rn(__a);
}
__DEVICE__ double __ll2double_ru(long long __a) {
  return __nv_ll2double_ru(__a);
}
__DEVICE__ double __ll2double_rz(long long __a) {
  return __nv_ll2double_rz(__a);
}
__DEVICE__ float __ll2float_rd(long long __a) { return __nv_ll2float_rd(__a); }
__DEVICE__ float __ll2float_rn(long long __a) { return __nv_ll2float_rn(__a); }
__DEVICE__ float __ll2float_ru(long long __a) { return __nv_ll2float_ru(__a); }
__DEVICE__ float __ll2float_rz(long long __a) { return __nv_ll2float_rz(__a); }
__DEVICE__ long long __llAtomicAnd(long long *__p, long long __v) {
  return __nvvm_atom_and_gen_ll(__p, __v);
}
__DEVICE__ long long __llAtomicAnd_block(long long *__p, long long __v) {
  return __nvvm_atom_cta_and_gen_ll(__p, __v);
}
__DEVICE__ long long __llAtomicAnd_system(long long *__p, long long __v) {
  return __nvvm_atom_sys_and_gen_ll(__p, __v);
}
__DEVICE__ long long __llAtomicOr(long long *__p, long long __v) {
  return __nvvm_atom_or_gen_ll(__p, __v);
}
__DEVICE__ long long __llAtomicOr_block(long long *__p, long long __v) {
  return __nvvm_atom_cta_or_gen_ll(__p, __v);
}
__DEVICE__ long long __llAtomicOr_system(long long *__p, long long __v) {
  return __nvvm_atom_sys_or_gen_ll(__p, __v);
}
__DEVICE__ long long __llAtomicXor(long long *__p, long long __v) {
  return __nvvm_atom_xor_gen_ll(__p, __v);
}
__DEVICE__ long long __llAtomicXor_block(long long *__p, long long __v) {
  return __nvvm_atom_cta_xor_gen_ll(__p, __v);
}
__DEVICE__ long long __llAtomicXor_system(long long *__p, long long __v) {
  return __nvvm_atom_sys_xor_gen_ll(__p, __v);
}
__DEVICE__ float __log10f(float __a) { return __nv_fast_log10f(__a); }
__DEVICE__ float __log2f(float __a) { return __nv_fast_log2f(__a); }
__DEVICE__ float __logf(float __a) { return __nv_fast_logf(__a); }
__DEVICE__ double __longlong_as_double(long long __a) {
  return __nv_longlong_as_double(__a);
}
__DEVICE__ int __mul24(int __a, int __b) { return __nv_mul24(__a, __b); }
__DEVICE__ long long __mul64hi(long long __a, long long __b) {
  return __nv_mul64hi(__a, __b);
}
__DEVICE__ int __mulhi(int __a, int __b) { return __nv_mulhi(__a, __b); }
__DEVICE__ unsigned int __pm0(void) { return __nvvm_read_ptx_sreg_pm0(); }
__DEVICE__ unsigned int __pm1(void) { return __nvvm_read_ptx_sreg_pm1(); }
__DEVICE__ unsigned int __pm2(void) { return __nvvm_read_ptx_sreg_pm2(); }
__DEVICE__ unsigned int __pm3(void) { return __nvvm_read_ptx_sreg_pm3(); }
__DEVICE__ int __popc(unsigned int __a) { return __nv_popc(__a); }
__DEVICE__ int __popcll(unsigned long long __a) { return __nv_popcll(__a); }
__DEVICE__ float __powf(float __a, float __b) {
  return __nv_fast_powf(__a, __b);
}

// Parameter must have a known integer value.
#define __prof_trigger(__a) __asm__ __volatile__("pmevent \t%0;" ::"i"(__a))
__DEVICE__ int __rhadd(int __a, int __b) { return __nv_rhadd(__a, __b); }
__DEVICE__ unsigned int __sad(int __a, int __b, unsigned int __c) {
  return __nv_sad(__a, __b, __c);
}
__DEVICE__ float __saturatef(float __a) { return __nv_saturatef(__a); }
__DEVICE__ int __signbitd(double __a) { return __nv_signbitd(__a); }
__DEVICE__ int __signbitf(float __a) { return __nv_signbitf(__a); }
__DEVICE__ void __sincosf(float __a, float *__s, float *__c) {
  return __nv_fast_sincosf(__a, __s, __c);
}
__DEVICE__ float __sinf(float __a) { return __nv_fast_sinf(__a); }
__DEVICE__ int __syncthreads_and(int __a) { return __nvvm_bar0_and(__a); }
__DEVICE__ int __syncthreads_count(int __a) { return __nvvm_bar0_popc(__a); }
__DEVICE__ int __syncthreads_or(int __a) { return __nvvm_bar0_or(__a); }
__DEVICE__ float __tanf(float __a) { return __nv_fast_tanf(__a); }
__DEVICE__ void __threadfence(void) { __nvvm_membar_gl(); }
__DEVICE__ void __threadfence_block(void) { __nvvm_membar_cta(); };
__DEVICE__ void __threadfence_system(void) { __nvvm_membar_sys(); };
__DEVICE__ void __trap(void) { __asm__ __volatile__("trap;"); }
__DEVICE__ unsigned int __uAtomicAdd(unsigned int *__p, unsigned int __v) {
  return __nvvm_atom_add_gen_i((int *)__p, __v);
}
__DEVICE__ unsigned int __uAtomicAdd_block(unsigned int *__p,
                                           unsigned int __v) {
  return __nvvm_atom_cta_add_gen_i((int *)__p, __v);
}
__DEVICE__ unsigned int __uAtomicAdd_system(unsigned int *__p,
                                            unsigned int __v) {
  return __nvvm_atom_sys_add_gen_i((int *)__p, __v);
}
__DEVICE__ unsigned int __uAtomicAnd(unsigned int *__p, unsigned int __v) {
  return __nvvm_atom_and_gen_i((int *)__p, __v);
}
__DEVICE__ unsigned int __uAtomicAnd_block(unsigned int *__p,
                                           unsigned int __v) {
  return __nvvm_atom_cta_and_gen_i((int *)__p, __v);
}
__DEVICE__ unsigned int __uAtomicAnd_system(unsigned int *__p,
                                            unsigned int __v) {
  return __nvvm_atom_sys_and_gen_i((int *)__p, __v);
}
__DEVICE__ unsigned int __uAtomicCAS(unsigned int *__p, unsigned int __cmp,
                                     unsigned int __v) {
  return __nvvm_atom_cas_gen_i((int *)__p, __cmp, __v);
}
__DEVICE__ unsigned int
__uAtomicCAS_block(unsigned int *__p, unsigned int __cmp, unsigned int __v) {
  return __nvvm_atom_cta_cas_gen_i((int *)__p, __cmp, __v);
}
__DEVICE__ unsigned int
__uAtomicCAS_system(unsigned int *__p, unsigned int __cmp, unsigned int __v) {
  return __nvvm_atom_sys_cas_gen_i((int *)__p, __cmp, __v);
}
__DEVICE__ unsigned int __uAtomicDec(unsigned int *__p, unsigned int __v) {
  return __nvvm_atom_dec_gen_ui(__p, __v);
}
__DEVICE__ unsigned int __uAtomicDec_block(unsigned int *__p,
                                           unsigned int __v) {
  return __nvvm_atom_cta_dec_gen_ui(__p, __v);
}
__DEVICE__ unsigned int __uAtomicDec_system(unsigned int *__p,
                                            unsigned int __v) {
  return __nvvm_atom_sys_dec_gen_ui(__p, __v);
}
__DEVICE__ unsigned int __uAtomicExch(unsigned int *__p, unsigned int __v) {
  return __nvvm_atom_xchg_gen_i((int *)__p, __v);
}
__DEVICE__ unsigned int __uAtomicExch_block(unsigned int *__p,
                                            unsigned int __v) {
  return __nvvm_atom_cta_xchg_gen_i((int *)__p, __v);
}
__DEVICE__ unsigned int __uAtomicExch_system(unsigned int *__p,
                                             unsigned int __v) {
  return __nvvm_atom_sys_xchg_gen_i((int *)__p, __v);
}
__DEVICE__ unsigned int __uAtomicInc(unsigned int *__p, unsigned int __v) {
  return __nvvm_atom_inc_gen_ui(__p, __v);
}
__DEVICE__ unsigned int __uAtomicInc_block(unsigned int *__p,
                                           unsigned int __v) {
  return __nvvm_atom_cta_inc_gen_ui(__p, __v);
}
__DEVICE__ unsigned int __uAtomicInc_system(unsigned int *__p,
                                            unsigned int __v) {
  return __nvvm_atom_sys_inc_gen_ui(__p, __v);
}
__DEVICE__ unsigned int __uAtomicMax(unsigned int *__p, unsigned int __v) {
  return __nvvm_atom_max_gen_ui(__p, __v);
}
__DEVICE__ unsigned int __uAtomicMax_block(unsigned int *__p,
                                           unsigned int __v) {
  return __nvvm_atom_cta_max_gen_ui(__p, __v);
}
__DEVICE__ unsigned int __uAtomicMax_system(unsigned int *__p,
                                            unsigned int __v) {
  return __nvvm_atom_sys_max_gen_ui(__p, __v);
}
__DEVICE__ unsigned int __uAtomicMin(unsigned int *__p, unsigned int __v) {
  return __nvvm_atom_min_gen_ui(__p, __v);
}
__DEVICE__ unsigned int __uAtomicMin_block(unsigned int *__p,
                                           unsigned int __v) {
  return __nvvm_atom_cta_min_gen_ui(__p, __v);
}
__DEVICE__ unsigned int __uAtomicMin_system(unsigned int *__p,
                                            unsigned int __v) {
  return __nvvm_atom_sys_min_gen_ui(__p, __v);
}
__DEVICE__ unsigned int __uAtomicOr(unsigned int *__p, unsigned int __v) {
  return __nvvm_atom_or_gen_i((int *)__p, __v);
}
__DEVICE__ unsigned int __uAtomicOr_block(unsigned int *__p, unsigned int __v) {
  return __nvvm_atom_cta_or_gen_i((int *)__p, __v);
}
__DEVICE__ unsigned int __uAtomicOr_system(unsigned int *__p,
                                           unsigned int __v) {
  return __nvvm_atom_sys_or_gen_i((int *)__p, __v);
}
__DEVICE__ unsigned int __uAtomicXor(unsigned int *__p, unsigned int __v) {
  return __nvvm_atom_xor_gen_i((int *)__p, __v);
}
__DEVICE__ unsigned int __uAtomicXor_block(unsigned int *__p,
                                           unsigned int __v) {
  return __nvvm_atom_cta_xor_gen_i((int *)__p, __v);
}
__DEVICE__ unsigned int __uAtomicXor_system(unsigned int *__p,
                                            unsigned int __v) {
  return __nvvm_atom_sys_xor_gen_i((int *)__p, __v);
}
__DEVICE__ unsigned int __uhadd(unsigned int __a, unsigned int __b) {
  return __nv_uhadd(__a, __b);
}
__DEVICE__ double __uint2double_rn(unsigned int __a) {
  return __nv_uint2double_rn(__a);
}
__DEVICE__ float __uint2float_rd(unsigned int __a) {
  return __nv_uint2float_rd(__a);
}
__DEVICE__ float __uint2float_rn(unsigned int __a) {
  return __nv_uint2float_rn(__a);
}
__DEVICE__ float __uint2float_ru(unsigned int __a) {
  return __nv_uint2float_ru(__a);
}
__DEVICE__ float __uint2float_rz(unsigned int __a) {
  return __nv_uint2float_rz(__a);
}
__DEVICE__ float __uint_as_float(unsigned int __a) {
  return __nv_uint_as_float(__a);
} //
__DEVICE__ double __ull2double_rd(unsigned long long __a) {
  return __nv_ull2double_rd(__a);
}
__DEVICE__ double __ull2double_rn(unsigned long long __a) {
  return __nv_ull2double_rn(__a);
}
__DEVICE__ double __ull2double_ru(unsigned long long __a) {
  return __nv_ull2double_ru(__a);
}
__DEVICE__ double __ull2double_rz(unsigned long long __a) {
  return __nv_ull2double_rz(__a);
}
__DEVICE__ float __ull2float_rd(unsigned long long __a) {
  return __nv_ull2float_rd(__a);
}
__DEVICE__ float __ull2float_rn(unsigned long long __a) {
  return __nv_ull2float_rn(__a);
}
__DEVICE__ float __ull2float_ru(unsigned long long __a) {
  return __nv_ull2float_ru(__a);
}
__DEVICE__ float __ull2float_rz(unsigned long long __a) {
  return __nv_ull2float_rz(__a);
}
__DEVICE__ unsigned long long __ullAtomicAdd(unsigned long long *__p,
                                             unsigned long long __v) {
  return __nvvm_atom_add_gen_ll((long long *)__p, __v);
}
__DEVICE__ unsigned long long __ullAtomicAdd_block(unsigned long long *__p,
                                                   unsigned long long __v) {
  return __nvvm_atom_cta_add_gen_ll((long long *)__p, __v);
}
__DEVICE__ unsigned long long __ullAtomicAdd_system(unsigned long long *__p,
                                                    unsigned long long __v) {
  return __nvvm_atom_sys_add_gen_ll((long long *)__p, __v);
}
__DEVICE__ unsigned long long __ullAtomicAnd(unsigned long long *__p,
                                             unsigned long long __v) {
  return __nvvm_atom_and_gen_ll((long long *)__p, __v);
}
__DEVICE__ unsigned long long __ullAtomicAnd_block(unsigned long long *__p,
                                                   unsigned long long __v) {
  return __nvvm_atom_cta_and_gen_ll((long long *)__p, __v);
}
__DEVICE__ unsigned long long __ullAtomicAnd_system(unsigned long long *__p,
                                                    unsigned long long __v) {
  return __nvvm_atom_sys_and_gen_ll((long long *)__p, __v);
}
__DEVICE__ unsigned long long __ullAtomicCAS(unsigned long long *__p,
                                             unsigned long long __cmp,
                                             unsigned long long __v) {
  return __nvvm_atom_cas_gen_ll((long long *)__p, __cmp, __v);
}
__DEVICE__ unsigned long long __ullAtomicCAS_block(unsigned long long *__p,
                                                   unsigned long long __cmp,
                                                   unsigned long long __v) {
  return __nvvm_atom_cta_cas_gen_ll((long long *)__p, __cmp, __v);
}
__DEVICE__ unsigned long long __ullAtomicCAS_system(unsigned long long *__p,
                                                    unsigned long long __cmp,
                                                    unsigned long long __v) {
  return __nvvm_atom_sys_cas_gen_ll((long long *)__p, __cmp, __v);
}
__DEVICE__ unsigned long long __ullAtomicExch(unsigned long long *__p,
                                              unsigned long long __v) {
  return __nvvm_atom_xchg_gen_ll((long long *)__p, __v);
}
__DEVICE__ unsigned long long __ullAtomicExch_block(unsigned long long *__p,
                                                    unsigned long long __v) {
  return __nvvm_atom_cta_xchg_gen_ll((long long *)__p, __v);
}
__DEVICE__ unsigned long long __ullAtomicExch_system(unsigned long long *__p,
                                                     unsigned long long __v) {
  return __nvvm_atom_sys_xchg_gen_ll((long long *)__p, __v);
}
__DEVICE__ unsigned long long __ullAtomicMax(unsigned long long *__p,
                                             unsigned long long __v) {
  return __nvvm_atom_max_gen_ull(__p, __v);
}
__DEVICE__ unsigned long long __ullAtomicMax_block(unsigned long long *__p,
                                                   unsigned long long __v) {
  return __nvvm_atom_cta_max_gen_ull(__p, __v);
}
__DEVICE__ unsigned long long __ullAtomicMax_system(unsigned long long *__p,
                                                    unsigned long long __v) {
  return __nvvm_atom_sys_max_gen_ull(__p, __v);
}
__DEVICE__ unsigned long long __ullAtomicMin(unsigned long long *__p,
                                             unsigned long long __v) {
  return __nvvm_atom_min_gen_ull(__p, __v);
}
__DEVICE__ unsigned long long __ullAtomicMin_block(unsigned long long *__p,
                                                   unsigned long long __v) {
  return __nvvm_atom_cta_min_gen_ull(__p, __v);
}
__DEVICE__ unsigned long long __ullAtomicMin_system(unsigned long long *__p,
                                                    unsigned long long __v) {
  return __nvvm_atom_sys_min_gen_ull(__p, __v);
}
__DEVICE__ unsigned long long __ullAtomicOr(unsigned long long *__p,
                                            unsigned long long __v) {
  return __nvvm_atom_or_gen_ll((long long *)__p, __v);
}
__DEVICE__ unsigned long long __ullAtomicOr_block(unsigned long long *__p,
                                                  unsigned long long __v) {
  return __nvvm_atom_cta_or_gen_ll((long long *)__p, __v);
}
__DEVICE__ unsigned long long __ullAtomicOr_system(unsigned long long *__p,
                                                   unsigned long long __v) {
  return __nvvm_atom_sys_or_gen_ll((long long *)__p, __v);
}
__DEVICE__ unsigned long long __ullAtomicXor(unsigned long long *__p,
                                             unsigned long long __v) {
  return __nvvm_atom_xor_gen_ll((long long *)__p, __v);
}
__DEVICE__ unsigned long long __ullAtomicXor_block(unsigned long long *__p,
                                                   unsigned long long __v) {
  return __nvvm_atom_cta_xor_gen_ll((long long *)__p, __v);
}
__DEVICE__ unsigned long long __ullAtomicXor_system(unsigned long long *__p,
                                                    unsigned long long __v) {
  return __nvvm_atom_sys_xor_gen_ll((long long *)__p, __v);
}
__DEVICE__ unsigned int __umul24(unsigned int __a, unsigned int __b) {
  return __nv_umul24(__a, __b);
}
__DEVICE__ unsigned long long __umul64hi(unsigned long long __a,
                                         unsigned long long __b) {
  return __nv_umul64hi(__a, __b);
}
__DEVICE__ unsigned int __umulhi(unsigned int __a, unsigned int __b) {
  return __nv_umulhi(__a, __b);
}
__DEVICE__ unsigned int __urhadd(unsigned int __a, unsigned int __b) {
  return __nv_urhadd(__a, __b);
}
__DEVICE__ unsigned int __usad(unsigned int __a, unsigned int __b,
                               unsigned int __c) {
  return __nv_usad(__a, __b, __c);
}

#if CUDA_VERSION >= 9000 && CUDA_VERSION < 9020
__DEVICE__ unsigned int __vabs2(unsigned int __a) { return __nv_vabs2(__a); }
__DEVICE__ unsigned int __vabs4(unsigned int __a) { return __nv_vabs4(__a); }
__DEVICE__ unsigned int __vabsdiffs2(unsigned int __a, unsigned int __b) {
  return __nv_vabsdiffs2(__a, __b);
}
__DEVICE__ unsigned int __vabsdiffs4(unsigned int __a, unsigned int __b) {
  return __nv_vabsdiffs4(__a, __b);
}
__DEVICE__ unsigned int __vabsdiffu2(unsigned int __a, unsigned int __b) {
  return __nv_vabsdiffu2(__a, __b);
}
__DEVICE__ unsigned int __vabsdiffu4(unsigned int __a, unsigned int __b) {
  return __nv_vabsdiffu4(__a, __b);
}
__DEVICE__ unsigned int __vabsss2(unsigned int __a) {
  return __nv_vabsss2(__a);
}
__DEVICE__ unsigned int __vabsss4(unsigned int __a) {
  return __nv_vabsss4(__a);
}
__DEVICE__ unsigned int __vadd2(unsigned int __a, unsigned int __b) {
  return __nv_vadd2(__a, __b);
}
__DEVICE__ unsigned int __vadd4(unsigned int __a, unsigned int __b) {
  return __nv_vadd4(__a, __b);
}
__DEVICE__ unsigned int __vaddss2(unsigned int __a, unsigned int __b) {
  return __nv_vaddss2(__a, __b);
}
__DEVICE__ unsigned int __vaddss4(unsigned int __a, unsigned int __b) {
  return __nv_vaddss4(__a, __b);
}
__DEVICE__ unsigned int __vaddus2(unsigned int __a, unsigned int __b) {
  return __nv_vaddus2(__a, __b);
}
__DEVICE__ unsigned int __vaddus4(unsigned int __a, unsigned int __b) {
  return __nv_vaddus4(__a, __b);
}
__DEVICE__ unsigned int __vavgs2(unsigned int __a, unsigned int __b) {
  return __nv_vavgs2(__a, __b);
}
__DEVICE__ unsigned int __vavgs4(unsigned int __a, unsigned int __b) {
  return __nv_vavgs4(__a, __b);
}
__DEVICE__ unsigned int __vavgu2(unsigned int __a, unsigned int __b) {
  return __nv_vavgu2(__a, __b);
}
__DEVICE__ unsigned int __vavgu4(unsigned int __a, unsigned int __b) {
  return __nv_vavgu4(__a, __b);
}
__DEVICE__ unsigned int __vcmpeq2(unsigned int __a, unsigned int __b) {
  return __nv_vcmpeq2(__a, __b);
}
__DEVICE__ unsigned int __vcmpeq4(unsigned int __a, unsigned int __b) {
  return __nv_vcmpeq4(__a, __b);
}
__DEVICE__ unsigned int __vcmpges2(unsigned int __a, unsigned int __b) {
  return __nv_vcmpges2(__a, __b);
}
__DEVICE__ unsigned int __vcmpges4(unsigned int __a, unsigned int __b) {
  return __nv_vcmpges4(__a, __b);
}
__DEVICE__ unsigned int __vcmpgeu2(unsigned int __a, unsigned int __b) {
  return __nv_vcmpgeu2(__a, __b);
}
__DEVICE__ unsigned int __vcmpgeu4(unsigned int __a, unsigned int __b) {
  return __nv_vcmpgeu4(__a, __b);
}
__DEVICE__ unsigned int __vcmpgts2(unsigned int __a, unsigned int __b) {
  return __nv_vcmpgts2(__a, __b);
}
__DEVICE__ unsigned int __vcmpgts4(unsigned int __a, unsigned int __b) {
  return __nv_vcmpgts4(__a, __b);
}
__DEVICE__ unsigned int __vcmpgtu2(unsigned int __a, unsigned int __b) {
  return __nv_vcmpgtu2(__a, __b);
}
__DEVICE__ unsigned int __vcmpgtu4(unsigned int __a, unsigned int __b) {
  return __nv_vcmpgtu4(__a, __b);
}
__DEVICE__ unsigned int __vcmples2(unsigned int __a, unsigned int __b) {
  return __nv_vcmples2(__a, __b);
}
__DEVICE__ unsigned int __vcmples4(unsigned int __a, unsigned int __b) {
  return __nv_vcmples4(__a, __b);
}
__DEVICE__ unsigned int __vcmpleu2(unsigned int __a, unsigned int __b) {
  return __nv_vcmpleu2(__a, __b);
}
__DEVICE__ unsigned int __vcmpleu4(unsigned int __a, unsigned int __b) {
  return __nv_vcmpleu4(__a, __b);
}
__DEVICE__ unsigned int __vcmplts2(unsigned int __a, unsigned int __b) {
  return __nv_vcmplts2(__a, __b);
}
__DEVICE__ unsigned int __vcmplts4(unsigned int __a, unsigned int __b) {
  return __nv_vcmplts4(__a, __b);
}
__DEVICE__ unsigned int __vcmpltu2(unsigned int __a, unsigned int __b) {
  return __nv_vcmpltu2(__a, __b);
}
__DEVICE__ unsigned int __vcmpltu4(unsigned int __a, unsigned int __b) {
  return __nv_vcmpltu4(__a, __b);
}
__DEVICE__ unsigned int __vcmpne2(unsigned int __a, unsigned int __b) {
  return __nv_vcmpne2(__a, __b);
}
__DEVICE__ unsigned int __vcmpne4(unsigned int __a, unsigned int __b) {
  return __nv_vcmpne4(__a, __b);
}
__DEVICE__ unsigned int __vhaddu2(unsigned int __a, unsigned int __b) {
  return __nv_vhaddu2(__a, __b);
}
__DEVICE__ unsigned int __vhaddu4(unsigned int __a, unsigned int __b) {
  return __nv_vhaddu4(__a, __b);
}
__DEVICE__ unsigned int __vmaxs2(unsigned int __a, unsigned int __b) {
  return __nv_vmaxs2(__a, __b);
}
__DEVICE__ unsigned int __vmaxs4(unsigned int __a, unsigned int __b) {
  return __nv_vmaxs4(__a, __b);
}
__DEVICE__ unsigned int __vmaxu2(unsigned int __a, unsigned int __b) {
  return __nv_vmaxu2(__a, __b);
}
__DEVICE__ unsigned int __vmaxu4(unsigned int __a, unsigned int __b) {
  return __nv_vmaxu4(__a, __b);
}
__DEVICE__ unsigned int __vmins2(unsigned int __a, unsigned int __b) {
  return __nv_vmins2(__a, __b);
}
__DEVICE__ unsigned int __vmins4(unsigned int __a, unsigned int __b) {
  return __nv_vmins4(__a, __b);
}
__DEVICE__ unsigned int __vminu2(unsigned int __a, unsigned int __b) {
  return __nv_vminu2(__a, __b);
}
__DEVICE__ unsigned int __vminu4(unsigned int __a, unsigned int __b) {
  return __nv_vminu4(__a, __b);
}
__DEVICE__ unsigned int __vneg2(unsigned int __a) { return __nv_vneg2(__a); }
__DEVICE__ unsigned int __vneg4(unsigned int __a) { return __nv_vneg4(__a); }
__DEVICE__ unsigned int __vnegss2(unsigned int __a) {
  return __nv_vnegss2(__a);
}
__DEVICE__ unsigned int __vnegss4(unsigned int __a) {
  return __nv_vnegss4(__a);
}
__DEVICE__ unsigned int __vsads2(unsigned int __a, unsigned int __b) {
  return __nv_vsads2(__a, __b);
}
__DEVICE__ unsigned int __vsads4(unsigned int __a, unsigned int __b) {
  return __nv_vsads4(__a, __b);
}
__DEVICE__ unsigned int __vsadu2(unsigned int __a, unsigned int __b) {
  return __nv_vsadu2(__a, __b);
}
__DEVICE__ unsigned int __vsadu4(unsigned int __a, unsigned int __b) {
  return __nv_vsadu4(__a, __b);
}
__DEVICE__ unsigned int __vseteq2(unsigned int __a, unsigned int __b) {
  return __nv_vseteq2(__a, __b);
}
__DEVICE__ unsigned int __vseteq4(unsigned int __a, unsigned int __b) {
  return __nv_vseteq4(__a, __b);
}
__DEVICE__ unsigned int __vsetges2(unsigned int __a, unsigned int __b) {
  return __nv_vsetges2(__a, __b);
}
__DEVICE__ unsigned int __vsetges4(unsigned int __a, unsigned int __b) {
  return __nv_vsetges4(__a, __b);
}
__DEVICE__ unsigned int __vsetgeu2(unsigned int __a, unsigned int __b) {
  return __nv_vsetgeu2(__a, __b);
}
__DEVICE__ unsigned int __vsetgeu4(unsigned int __a, unsigned int __b) {
  return __nv_vsetgeu4(__a, __b);
}
__DEVICE__ unsigned int __vsetgts2(unsigned int __a, unsigned int __b) {
  return __nv_vsetgts2(__a, __b);
}
__DEVICE__ unsigned int __vsetgts4(unsigned int __a, unsigned int __b) {
  return __nv_vsetgts4(__a, __b);
}
__DEVICE__ unsigned int __vsetgtu2(unsigned int __a, unsigned int __b) {
  return __nv_vsetgtu2(__a, __b);
}
__DEVICE__ unsigned int __vsetgtu4(unsigned int __a, unsigned int __b) {
  return __nv_vsetgtu4(__a, __b);
}
__DEVICE__ unsigned int __vsetles2(unsigned int __a, unsigned int __b) {
  return __nv_vsetles2(__a, __b);
}
__DEVICE__ unsigned int __vsetles4(unsigned int __a, unsigned int __b) {
  return __nv_vsetles4(__a, __b);
}
__DEVICE__ unsigned int __vsetleu2(unsigned int __a, unsigned int __b) {
  return __nv_vsetleu2(__a, __b);
}
__DEVICE__ unsigned int __vsetleu4(unsigned int __a, unsigned int __b) {
  return __nv_vsetleu4(__a, __b);
}
__DEVICE__ unsigned int __vsetlts2(unsigned int __a, unsigned int __b) {
  return __nv_vsetlts2(__a, __b);
}
__DEVICE__ unsigned int __vsetlts4(unsigned int __a, unsigned int __b) {
  return __nv_vsetlts4(__a, __b);
}
__DEVICE__ unsigned int __vsetltu2(unsigned int __a, unsigned int __b) {
  return __nv_vsetltu2(__a, __b);
}
__DEVICE__ unsigned int __vsetltu4(unsigned int __a, unsigned int __b) {
  return __nv_vsetltu4(__a, __b);
}
__DEVICE__ unsigned int __vsetne2(unsigned int __a, unsigned int __b) {
  return __nv_vsetne2(__a, __b);
}
__DEVICE__ unsigned int __vsetne4(unsigned int __a, unsigned int __b) {
  return __nv_vsetne4(__a, __b);
}
__DEVICE__ unsigned int __vsub2(unsigned int __a, unsigned int __b) {
  return __nv_vsub2(__a, __b);
}
__DEVICE__ unsigned int __vsub4(unsigned int __a, unsigned int __b) {
  return __nv_vsub4(__a, __b);
}
__DEVICE__ unsigned int __vsubss2(unsigned int __a, unsigned int __b) {
  return __nv_vsubss2(__a, __b);
}
__DEVICE__ unsigned int __vsubss4(unsigned int __a, unsigned int __b) {
  return __nv_vsubss4(__a, __b);
}
__DEVICE__ unsigned int __vsubus2(unsigned int __a, unsigned int __b) {
  return __nv_vsubus2(__a, __b);
}
__DEVICE__ unsigned int __vsubus4(unsigned int __a, unsigned int __b) {
  return __nv_vsubus4(__a, __b);
}
#else // CUDA_VERSION >= 9020
// CUDA no longer provides inline assembly (or bitcode) implementation of these
// functions, so we have to reimplment them. The implementation is naive and is
// not optimized for performance.

// Helper function to convert N-bit boolean subfields into all-0 or all-1.
// E.g. __bool2mask(0x01000100,8) -> 0xff00ff00
//      __bool2mask(0x00010000,16) -> 0xffff0000
__DEVICE__ unsigned int __bool2mask(unsigned int __a, int shift) {
  return (__a << shift) - __a;
}
__DEVICE__ unsigned int __vabs2(unsigned int __a) {
  unsigned int r;
  __asm__("vabsdiff2.s32.s32.s32 %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(0), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vabs4(unsigned int __a) {
  unsigned int r;
  __asm__("vabsdiff4.s32.s32.s32 %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(0), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vabsdiffs2(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vabsdiff2.s32.s32.s32 %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}

__DEVICE__ unsigned int __vabsdiffs4(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vabsdiff4.s32.s32.s32 %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vabsdiffu2(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vabsdiff2.u32.u32.u32 %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vabsdiffu4(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vabsdiff4.u32.u32.u32 %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vabsss2(unsigned int __a) {
  unsigned int r;
  __asm__("vabsdiff2.s32.s32.s32.sat %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(0), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vabsss4(unsigned int __a) {
  unsigned int r;
  __asm__("vabsdiff4.s32.s32.s32.sat %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(0), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vadd2(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vadd2.u32.u32.u32 %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vadd4(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vadd4.u32.u32.u32 %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vaddss2(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vadd2.s32.s32.s32.sat %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vaddss4(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vadd4.s32.s32.s32.sat %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vaddus2(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vadd2.u32.u32.u32.sat %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vaddus4(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vadd4.u32.u32.u32.sat %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vavgs2(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vavrg2.s32.s32.s32 %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vavgs4(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vavrg4.s32.s32.s32 %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vavgu2(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vavrg2.u32.u32.u32 %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vavgu4(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vavrg4.u32.u32.u32 %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vseteq2(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vset2.u32.u32.eq %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vcmpeq2(unsigned int __a, unsigned int __b) {
  return __bool2mask(__vseteq2(__a, __b), 16);
}
__DEVICE__ unsigned int __vseteq4(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vset4.u32.u32.eq %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vcmpeq4(unsigned int __a, unsigned int __b) {
  return __bool2mask(__vseteq4(__a, __b), 8);
}
__DEVICE__ unsigned int __vsetges2(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vset2.s32.s32.ge %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vcmpges2(unsigned int __a, unsigned int __b) {
  return __bool2mask(__vsetges2(__a, __b), 16);
}
__DEVICE__ unsigned int __vsetges4(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vset4.s32.s32.ge %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vcmpges4(unsigned int __a, unsigned int __b) {
  return __bool2mask(__vsetges4(__a, __b), 8);
}
__DEVICE__ unsigned int __vsetgeu2(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vset2.u32.u32.ge %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vcmpgeu2(unsigned int __a, unsigned int __b) {
  return __bool2mask(__vsetgeu2(__a, __b), 16);
}
__DEVICE__ unsigned int __vsetgeu4(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vset4.u32.u32.ge %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vcmpgeu4(unsigned int __a, unsigned int __b) {
  return __bool2mask(__vsetgeu4(__a, __b), 8);
}
__DEVICE__ unsigned int __vsetgts2(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vset2.s32.s32.gt %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vcmpgts2(unsigned int __a, unsigned int __b) {
  return __bool2mask(__vsetgts2(__a, __b), 16);
}
__DEVICE__ unsigned int __vsetgts4(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vset4.s32.s32.gt %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vcmpgts4(unsigned int __a, unsigned int __b) {
  return __bool2mask(__vsetgts4(__a, __b), 8);
}
__DEVICE__ unsigned int __vsetgtu2(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vset2.u32.u32.gt %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vcmpgtu2(unsigned int __a, unsigned int __b) {
  return __bool2mask(__vsetgtu2(__a, __b), 16);
}
__DEVICE__ unsigned int __vsetgtu4(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vset4.u32.u32.gt %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vcmpgtu4(unsigned int __a, unsigned int __b) {
  return __bool2mask(__vsetgtu4(__a, __b), 8);
}
__DEVICE__ unsigned int __vsetles2(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vset2.s32.s32.le %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vcmples2(unsigned int __a, unsigned int __b) {
  return __bool2mask(__vsetles2(__a, __b), 16);
}
__DEVICE__ unsigned int __vsetles4(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vset4.s32.s32.le %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vcmples4(unsigned int __a, unsigned int __b) {
  return __bool2mask(__vsetles4(__a, __b), 8);
}
__DEVICE__ unsigned int __vsetleu2(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vset2.u32.u32.le %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vcmpleu2(unsigned int __a, unsigned int __b) {
  return __bool2mask(__vsetleu2(__a, __b), 16);
}
__DEVICE__ unsigned int __vsetleu4(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vset4.u32.u32.le %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vcmpleu4(unsigned int __a, unsigned int __b) {
  return __bool2mask(__vsetleu4(__a, __b), 8);
}
__DEVICE__ unsigned int __vsetlts2(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vset2.s32.s32.lt %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vcmplts2(unsigned int __a, unsigned int __b) {
  return __bool2mask(__vsetlts2(__a, __b), 16);
}
__DEVICE__ unsigned int __vsetlts4(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vset4.s32.s32.lt %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vcmplts4(unsigned int __a, unsigned int __b) {
  return __bool2mask(__vsetlts4(__a, __b), 8);
}
__DEVICE__ unsigned int __vsetltu2(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vset2.u32.u32.lt %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vcmpltu2(unsigned int __a, unsigned int __b) {
  return __bool2mask(__vsetltu2(__a, __b), 16);
}
__DEVICE__ unsigned int __vsetltu4(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vset4.u32.u32.lt %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vcmpltu4(unsigned int __a, unsigned int __b) {
  return __bool2mask(__vsetltu4(__a, __b), 8);
}
__DEVICE__ unsigned int __vsetne2(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vset2.u32.u32.ne %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vcmpne2(unsigned int __a, unsigned int __b) {
  return __bool2mask(__vsetne2(__a, __b), 16);
}
__DEVICE__ unsigned int __vsetne4(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vset4.u32.u32.ne %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vcmpne4(unsigned int __a, unsigned int __b) {
  return __bool2mask(__vsetne4(__a, __b), 8);
}

// Based on ITEM 23 in AIM-239: http://dspace.mit.edu/handle/1721.1/6086
// (a & b) + (a | b) = a + b = (a ^ b) + 2 * (a & b) =>
// (a + b) / 2 = ((a ^ b) >> 1) + (a & b)
// To operate on multiple sub-elements we need to make sure to mask out bits
// that crossed over into adjacent elements during the shift.
__DEVICE__ unsigned int __vhaddu2(unsigned int __a, unsigned int __b) {
  return (((__a ^ __b) >> 1) & ~0x80008000u) + (__a & __b);
}
__DEVICE__ unsigned int __vhaddu4(unsigned int __a, unsigned int __b) {
  return (((__a ^ __b) >> 1) & ~0x80808080u) + (__a & __b);
}

__DEVICE__ unsigned int __vmaxs2(unsigned int __a, unsigned int __b) {
  unsigned int r;
  if ((__a & 0x8000) && (__b & 0x8000)) {
    // Work around a bug in ptxas which produces invalid result if low element
    // is negative.
    unsigned mask = __vcmpgts2(__a, __b);
    r = (__a & mask) | (__b & ~mask);
  } else {
    __asm__("vmax2.s32.s32.s32 %0,%1,%2,%3;"
            : "=r"(r)
            : "r"(__a), "r"(__b), "r"(0));
  }
  return r;
}
__DEVICE__ unsigned int __vmaxs4(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vmax4.s32.s32.s32 %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vmaxu2(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vmax2.u32.u32.u32 %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vmaxu4(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vmax4.u32.u32.u32 %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vmins2(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vmin2.s32.s32.s32 %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vmins4(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vmin4.s32.s32.s32 %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vminu2(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vmin2.u32.u32.u32 %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vminu4(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vmin4.u32.u32.u32 %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vsads2(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vabsdiff2.s32.s32.s32.add %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vsads4(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vabsdiff4.s32.s32.s32.add %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vsadu2(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vabsdiff2.u32.u32.u32.add %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vsadu4(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vabsdiff4.u32.u32.u32.add %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}

__DEVICE__ unsigned int __vsub2(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vsub2.u32.u32.u32 %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vneg2(unsigned int __a) { return __vsub2(0, __a); }

__DEVICE__ unsigned int __vsub4(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vsub4.u32.u32.u32 %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vneg4(unsigned int __a) { return __vsub4(0, __a); }
__DEVICE__ unsigned int __vsubss2(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vsub2.s32.s32.s32.sat %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vnegss2(unsigned int __a) {
  return __vsubss2(0, __a);
}
__DEVICE__ unsigned int __vsubss4(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vsub4.s32.s32.s32.sat %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vnegss4(unsigned int __a) {
  return __vsubss4(0, __a);
}
__DEVICE__ unsigned int __vsubus2(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vsub2.u32.u32.u32.sat %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
__DEVICE__ unsigned int __vsubus4(unsigned int __a, unsigned int __b) {
  unsigned int r;
  __asm__("vsub4.u32.u32.u32.sat %0,%1,%2,%3;"
          : "=r"(r)
          : "r"(__a), "r"(__b), "r"(0));
  return r;
}
#endif // CUDA_VERSION >= 9020

// For OpenMP we require the user to include <time.h> as we need to know what
// clock_t is on the system.
#ifndef __OPENMP_NVPTX__
__DEVICE__ /* clock_t= */ int clock() { return __nvvm_read_ptx_sreg_clock(); }
#endif
__DEVICE__ long long clock64() { return __nvvm_read_ptx_sreg_clock64(); }

// These functions shouldn't be declared when including this header
// for math function resolution purposes.
#ifndef __OPENMP_NVPTX__
__DEVICE__ void *memcpy(void *__a, const void *__b, size_t __c) {
  return __builtin_memcpy(__a, __b, __c);
}
__DEVICE__ void *memset(void *__a, int __b, size_t __c) {
  return __builtin_memset(__a, __b, __c);
}
#endif

#pragma pop_macro("__DEVICE__")
#endif // __CLANG_CUDA_DEVICE_FUNCTIONS_H__
