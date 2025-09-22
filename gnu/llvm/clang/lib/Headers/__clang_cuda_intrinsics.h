/*===--- __clang_cuda_intrinsics.h - Device-side CUDA intrinsic wrappers ---===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */
#ifndef __CLANG_CUDA_INTRINSICS_H__
#define __CLANG_CUDA_INTRINSICS_H__
#ifndef __CUDA__
#error "This file is for CUDA compilation only."
#endif

// sm_30 intrinsics: __shfl_{up,down,xor}.

#define __SM_30_INTRINSICS_H__
#define __SM_30_INTRINSICS_HPP__

#if !defined(__CUDA_ARCH__) || __CUDA_ARCH__ >= 300

#pragma push_macro("__MAKE_SHUFFLES")
#define __MAKE_SHUFFLES(__FnName, __IntIntrinsic, __FloatIntrinsic, __Mask,    \
                        __Type)                                                \
  inline __device__ int __FnName(int __val, __Type __offset,                   \
                                 int __width = warpSize) {                     \
    return __IntIntrinsic(__val, __offset,                                     \
                          ((warpSize - __width) << 8) | (__Mask));             \
  }                                                                            \
  inline __device__ float __FnName(float __val, __Type __offset,               \
                                   int __width = warpSize) {                   \
    return __FloatIntrinsic(__val, __offset,                                   \
                            ((warpSize - __width) << 8) | (__Mask));           \
  }                                                                            \
  inline __device__ unsigned int __FnName(unsigned int __val, __Type __offset, \
                                          int __width = warpSize) {            \
    return static_cast<unsigned int>(                                          \
        ::__FnName(static_cast<int>(__val), __offset, __width));               \
  }                                                                            \
  inline __device__ long long __FnName(long long __val, __Type __offset,       \
                                       int __width = warpSize) {               \
    struct __Bits {                                                            \
      int __a, __b;                                                            \
    };                                                                         \
    _Static_assert(sizeof(__val) == sizeof(__Bits));                           \
    _Static_assert(sizeof(__Bits) == 2 * sizeof(int));                         \
    __Bits __tmp;                                                              \
    memcpy(&__tmp, &__val, sizeof(__val));                                \
    __tmp.__a = ::__FnName(__tmp.__a, __offset, __width);                      \
    __tmp.__b = ::__FnName(__tmp.__b, __offset, __width);                      \
    long long __ret;                                                           \
    memcpy(&__ret, &__tmp, sizeof(__tmp));                                     \
    return __ret;                                                              \
  }                                                                            \
  inline __device__ long __FnName(long __val, __Type __offset,                 \
                                  int __width = warpSize) {                    \
    _Static_assert(sizeof(long) == sizeof(long long) ||                        \
                   sizeof(long) == sizeof(int));                               \
    if (sizeof(long) == sizeof(long long)) {                                   \
      return static_cast<long>(                                                \
          ::__FnName(static_cast<long long>(__val), __offset, __width));       \
    } else if (sizeof(long) == sizeof(int)) {                                  \
      return static_cast<long>(                                                \
          ::__FnName(static_cast<int>(__val), __offset, __width));             \
    }                                                                          \
  }                                                                            \
  inline __device__ unsigned long __FnName(                                    \
      unsigned long __val, __Type __offset, int __width = warpSize) {          \
    return static_cast<unsigned long>(                                         \
        ::__FnName(static_cast<long>(__val), __offset, __width));              \
  }                                                                            \
  inline __device__ unsigned long long __FnName(                               \
      unsigned long long __val, __Type __offset, int __width = warpSize) {     \
    return static_cast<unsigned long long>(                                    \
        ::__FnName(static_cast<long long>(__val), __offset, __width));         \
  }                                                                            \
  inline __device__ double __FnName(double __val, __Type __offset,             \
                                    int __width = warpSize) {                  \
    long long __tmp;                                                           \
    _Static_assert(sizeof(__tmp) == sizeof(__val));                            \
    memcpy(&__tmp, &__val, sizeof(__val));                                     \
    __tmp = ::__FnName(__tmp, __offset, __width);                              \
    double __ret;                                                              \
    memcpy(&__ret, &__tmp, sizeof(__ret));                                     \
    return __ret;                                                              \
  }

__MAKE_SHUFFLES(__shfl, __nvvm_shfl_idx_i32, __nvvm_shfl_idx_f32, 0x1f, int);
// We use 0 rather than 31 as our mask, because shfl.up applies to lanes >=
// maxLane.
__MAKE_SHUFFLES(__shfl_up, __nvvm_shfl_up_i32, __nvvm_shfl_up_f32, 0,
                unsigned int);
__MAKE_SHUFFLES(__shfl_down, __nvvm_shfl_down_i32, __nvvm_shfl_down_f32, 0x1f,
                unsigned int);
__MAKE_SHUFFLES(__shfl_xor, __nvvm_shfl_bfly_i32, __nvvm_shfl_bfly_f32, 0x1f,
                int);
#pragma pop_macro("__MAKE_SHUFFLES")

#endif // !defined(__CUDA_ARCH__) || __CUDA_ARCH__ >= 300

#if CUDA_VERSION >= 9000
#if (!defined(__CUDA_ARCH__) || __CUDA_ARCH__ >= 300)
// __shfl_sync_* variants available in CUDA-9
#pragma push_macro("__MAKE_SYNC_SHUFFLES")
#define __MAKE_SYNC_SHUFFLES(__FnName, __IntIntrinsic, __FloatIntrinsic,       \
                             __Mask, __Type)                                   \
  inline __device__ int __FnName(unsigned int __mask, int __val,               \
                                 __Type __offset, int __width = warpSize) {    \
    return __IntIntrinsic(__mask, __val, __offset,                             \
                          ((warpSize - __width) << 8) | (__Mask));             \
  }                                                                            \
  inline __device__ float __FnName(unsigned int __mask, float __val,           \
                                   __Type __offset, int __width = warpSize) {  \
    return __FloatIntrinsic(__mask, __val, __offset,                           \
                            ((warpSize - __width) << 8) | (__Mask));           \
  }                                                                            \
  inline __device__ unsigned int __FnName(unsigned int __mask,                 \
                                          unsigned int __val, __Type __offset, \
                                          int __width = warpSize) {            \
    return static_cast<unsigned int>(                                          \
        ::__FnName(__mask, static_cast<int>(__val), __offset, __width));       \
  }                                                                            \
  inline __device__ long long __FnName(unsigned int __mask, long long __val,   \
                                       __Type __offset,                        \
                                       int __width = warpSize) {               \
    struct __Bits {                                                            \
      int __a, __b;                                                            \
    };                                                                         \
    _Static_assert(sizeof(__val) == sizeof(__Bits));                           \
    _Static_assert(sizeof(__Bits) == 2 * sizeof(int));                         \
    __Bits __tmp;                                                              \
    memcpy(&__tmp, &__val, sizeof(__val));                                     \
    __tmp.__a = ::__FnName(__mask, __tmp.__a, __offset, __width);              \
    __tmp.__b = ::__FnName(__mask, __tmp.__b, __offset, __width);              \
    long long __ret;                                                           \
    memcpy(&__ret, &__tmp, sizeof(__tmp));                                     \
    return __ret;                                                              \
  }                                                                            \
  inline __device__ unsigned long long __FnName(                               \
      unsigned int __mask, unsigned long long __val, __Type __offset,          \
      int __width = warpSize) {                                                \
    return static_cast<unsigned long long>(                                    \
        ::__FnName(__mask, static_cast<long long>(__val), __offset, __width)); \
  }                                                                            \
  inline __device__ long __FnName(unsigned int __mask, long __val,             \
                                  __Type __offset, int __width = warpSize) {   \
    _Static_assert(sizeof(long) == sizeof(long long) ||                        \
                   sizeof(long) == sizeof(int));                               \
    if (sizeof(long) == sizeof(long long)) {                                   \
      return static_cast<long>(::__FnName(                                     \
          __mask, static_cast<long long>(__val), __offset, __width));          \
    } else if (sizeof(long) == sizeof(int)) {                                  \
      return static_cast<long>(                                                \
          ::__FnName(__mask, static_cast<int>(__val), __offset, __width));     \
    }                                                                          \
  }                                                                            \
  inline __device__ unsigned long __FnName(                                    \
      unsigned int __mask, unsigned long __val, __Type __offset,               \
      int __width = warpSize) {                                                \
    return static_cast<unsigned long>(                                         \
        ::__FnName(__mask, static_cast<long>(__val), __offset, __width));      \
  }                                                                            \
  inline __device__ double __FnName(unsigned int __mask, double __val,         \
                                    __Type __offset, int __width = warpSize) { \
    long long __tmp;                                                           \
    _Static_assert(sizeof(__tmp) == sizeof(__val));                            \
    memcpy(&__tmp, &__val, sizeof(__val));                                     \
    __tmp = ::__FnName(__mask, __tmp, __offset, __width);                      \
    double __ret;                                                              \
    memcpy(&__ret, &__tmp, sizeof(__ret));                                     \
    return __ret;                                                              \
  }
__MAKE_SYNC_SHUFFLES(__shfl_sync, __nvvm_shfl_sync_idx_i32,
                     __nvvm_shfl_sync_idx_f32, 0x1f, int);
// We use 0 rather than 31 as our mask, because shfl.up applies to lanes >=
// maxLane.
__MAKE_SYNC_SHUFFLES(__shfl_up_sync, __nvvm_shfl_sync_up_i32,
                     __nvvm_shfl_sync_up_f32, 0, unsigned int);
__MAKE_SYNC_SHUFFLES(__shfl_down_sync, __nvvm_shfl_sync_down_i32,
                     __nvvm_shfl_sync_down_f32, 0x1f, unsigned int);
__MAKE_SYNC_SHUFFLES(__shfl_xor_sync, __nvvm_shfl_sync_bfly_i32,
                     __nvvm_shfl_sync_bfly_f32, 0x1f, int);
#pragma pop_macro("__MAKE_SYNC_SHUFFLES")

inline __device__ void __syncwarp(unsigned int mask = 0xffffffff) {
  return __nvvm_bar_warp_sync(mask);
}

inline __device__ void __barrier_sync(unsigned int id) {
  __nvvm_barrier_sync(id);
}

inline __device__ void __barrier_sync_count(unsigned int id,
                                            unsigned int count) {
  __nvvm_barrier_sync_cnt(id, count);
}

inline __device__ int __all_sync(unsigned int mask, int pred) {
  return __nvvm_vote_all_sync(mask, pred);
}

inline __device__ int __any_sync(unsigned int mask, int pred) {
  return __nvvm_vote_any_sync(mask, pred);
}

inline __device__ int __uni_sync(unsigned int mask, int pred) {
  return __nvvm_vote_uni_sync(mask, pred);
}

inline __device__ unsigned int __ballot_sync(unsigned int mask, int pred) {
  return __nvvm_vote_ballot_sync(mask, pred);
}

inline __device__ unsigned int __activemask() {
#if CUDA_VERSION < 9020
  return __nvvm_vote_ballot(1);
#else
  return __nvvm_activemask();
#endif
}

inline __device__ unsigned int __fns(unsigned mask, unsigned base, int offset) {
  return __nvvm_fns(mask, base, offset);
}

#endif // !defined(__CUDA_ARCH__) || __CUDA_ARCH__ >= 300

// Define __match* builtins CUDA-9 headers expect to see.
#if !defined(__CUDA_ARCH__) || __CUDA_ARCH__ >= 700
inline __device__ unsigned int __match32_any_sync(unsigned int mask,
                                                  unsigned int value) {
  return __nvvm_match_any_sync_i32(mask, value);
}

inline __device__ unsigned int
__match64_any_sync(unsigned int mask, unsigned long long value) {
  return __nvvm_match_any_sync_i64(mask, value);
}

inline __device__ unsigned int
__match32_all_sync(unsigned int mask, unsigned int value, int *pred) {
  return __nvvm_match_all_sync_i32p(mask, value, pred);
}

inline __device__ unsigned int
__match64_all_sync(unsigned int mask, unsigned long long value, int *pred) {
  return __nvvm_match_all_sync_i64p(mask, value, pred);
}
#include "crt/sm_70_rt.hpp"

#endif // !defined(__CUDA_ARCH__) || __CUDA_ARCH__ >= 700
#endif // __CUDA_VERSION >= 9000

// sm_32 intrinsics: __ldg and __funnelshift_{l,lc,r,rc}.

// Prevent the vanilla sm_32 intrinsics header from being included.
#define __SM_32_INTRINSICS_H__
#define __SM_32_INTRINSICS_HPP__

#if !defined(__CUDA_ARCH__) || __CUDA_ARCH__ >= 320

inline __device__ char __ldg(const char *ptr) { return __nvvm_ldg_c(ptr); }
inline __device__ short __ldg(const short *ptr) { return __nvvm_ldg_s(ptr); }
inline __device__ int __ldg(const int *ptr) { return __nvvm_ldg_i(ptr); }
inline __device__ long __ldg(const long *ptr) { return __nvvm_ldg_l(ptr); }
inline __device__ long long __ldg(const long long *ptr) {
  return __nvvm_ldg_ll(ptr);
}
inline __device__ unsigned char __ldg(const unsigned char *ptr) {
  return __nvvm_ldg_uc(ptr);
}
inline __device__ signed char __ldg(const signed char *ptr) {
  return __nvvm_ldg_uc((const unsigned char *)ptr);
}
inline __device__ unsigned short __ldg(const unsigned short *ptr) {
  return __nvvm_ldg_us(ptr);
}
inline __device__ unsigned int __ldg(const unsigned int *ptr) {
  return __nvvm_ldg_ui(ptr);
}
inline __device__ unsigned long __ldg(const unsigned long *ptr) {
  return __nvvm_ldg_ul(ptr);
}
inline __device__ unsigned long long __ldg(const unsigned long long *ptr) {
  return __nvvm_ldg_ull(ptr);
}
inline __device__ float __ldg(const float *ptr) { return __nvvm_ldg_f(ptr); }
inline __device__ double __ldg(const double *ptr) { return __nvvm_ldg_d(ptr); }

inline __device__ char2 __ldg(const char2 *ptr) {
  typedef char c2 __attribute__((ext_vector_type(2)));
  // We can assume that ptr is aligned at least to char2's alignment, but the
  // load will assume that ptr is aligned to char2's alignment.  This is only
  // safe if alignof(c2) <= alignof(char2).
  c2 rv = __nvvm_ldg_c2(reinterpret_cast<const c2 *>(ptr));
  char2 ret;
  ret.x = rv[0];
  ret.y = rv[1];
  return ret;
}
inline __device__ char4 __ldg(const char4 *ptr) {
  typedef char c4 __attribute__((ext_vector_type(4)));
  c4 rv = __nvvm_ldg_c4(reinterpret_cast<const c4 *>(ptr));
  char4 ret;
  ret.x = rv[0];
  ret.y = rv[1];
  ret.z = rv[2];
  ret.w = rv[3];
  return ret;
}
inline __device__ short2 __ldg(const short2 *ptr) {
  typedef short s2 __attribute__((ext_vector_type(2)));
  s2 rv = __nvvm_ldg_s2(reinterpret_cast<const s2 *>(ptr));
  short2 ret;
  ret.x = rv[0];
  ret.y = rv[1];
  return ret;
}
inline __device__ short4 __ldg(const short4 *ptr) {
  typedef short s4 __attribute__((ext_vector_type(4)));
  s4 rv = __nvvm_ldg_s4(reinterpret_cast<const s4 *>(ptr));
  short4 ret;
  ret.x = rv[0];
  ret.y = rv[1];
  ret.z = rv[2];
  ret.w = rv[3];
  return ret;
}
inline __device__ int2 __ldg(const int2 *ptr) {
  typedef int i2 __attribute__((ext_vector_type(2)));
  i2 rv = __nvvm_ldg_i2(reinterpret_cast<const i2 *>(ptr));
  int2 ret;
  ret.x = rv[0];
  ret.y = rv[1];
  return ret;
}
inline __device__ int4 __ldg(const int4 *ptr) {
  typedef int i4 __attribute__((ext_vector_type(4)));
  i4 rv = __nvvm_ldg_i4(reinterpret_cast<const i4 *>(ptr));
  int4 ret;
  ret.x = rv[0];
  ret.y = rv[1];
  ret.z = rv[2];
  ret.w = rv[3];
  return ret;
}
inline __device__ longlong2 __ldg(const longlong2 *ptr) {
  typedef long long ll2 __attribute__((ext_vector_type(2)));
  ll2 rv = __nvvm_ldg_ll2(reinterpret_cast<const ll2 *>(ptr));
  longlong2 ret;
  ret.x = rv[0];
  ret.y = rv[1];
  return ret;
}

inline __device__ uchar2 __ldg(const uchar2 *ptr) {
  typedef unsigned char uc2 __attribute__((ext_vector_type(2)));
  uc2 rv = __nvvm_ldg_uc2(reinterpret_cast<const uc2 *>(ptr));
  uchar2 ret;
  ret.x = rv[0];
  ret.y = rv[1];
  return ret;
}
inline __device__ uchar4 __ldg(const uchar4 *ptr) {
  typedef unsigned char uc4 __attribute__((ext_vector_type(4)));
  uc4 rv = __nvvm_ldg_uc4(reinterpret_cast<const uc4 *>(ptr));
  uchar4 ret;
  ret.x = rv[0];
  ret.y = rv[1];
  ret.z = rv[2];
  ret.w = rv[3];
  return ret;
}
inline __device__ ushort2 __ldg(const ushort2 *ptr) {
  typedef unsigned short us2 __attribute__((ext_vector_type(2)));
  us2 rv = __nvvm_ldg_us2(reinterpret_cast<const us2 *>(ptr));
  ushort2 ret;
  ret.x = rv[0];
  ret.y = rv[1];
  return ret;
}
inline __device__ ushort4 __ldg(const ushort4 *ptr) {
  typedef unsigned short us4 __attribute__((ext_vector_type(4)));
  us4 rv = __nvvm_ldg_us4(reinterpret_cast<const us4 *>(ptr));
  ushort4 ret;
  ret.x = rv[0];
  ret.y = rv[1];
  ret.z = rv[2];
  ret.w = rv[3];
  return ret;
}
inline __device__ uint2 __ldg(const uint2 *ptr) {
  typedef unsigned int ui2 __attribute__((ext_vector_type(2)));
  ui2 rv = __nvvm_ldg_ui2(reinterpret_cast<const ui2 *>(ptr));
  uint2 ret;
  ret.x = rv[0];
  ret.y = rv[1];
  return ret;
}
inline __device__ uint4 __ldg(const uint4 *ptr) {
  typedef unsigned int ui4 __attribute__((ext_vector_type(4)));
  ui4 rv = __nvvm_ldg_ui4(reinterpret_cast<const ui4 *>(ptr));
  uint4 ret;
  ret.x = rv[0];
  ret.y = rv[1];
  ret.z = rv[2];
  ret.w = rv[3];
  return ret;
}
inline __device__ ulonglong2 __ldg(const ulonglong2 *ptr) {
  typedef unsigned long long ull2 __attribute__((ext_vector_type(2)));
  ull2 rv = __nvvm_ldg_ull2(reinterpret_cast<const ull2 *>(ptr));
  ulonglong2 ret;
  ret.x = rv[0];
  ret.y = rv[1];
  return ret;
}

inline __device__ float2 __ldg(const float2 *ptr) {
  typedef float f2 __attribute__((ext_vector_type(2)));
  f2 rv = __nvvm_ldg_f2(reinterpret_cast<const f2 *>(ptr));
  float2 ret;
  ret.x = rv[0];
  ret.y = rv[1];
  return ret;
}
inline __device__ float4 __ldg(const float4 *ptr) {
  typedef float f4 __attribute__((ext_vector_type(4)));
  f4 rv = __nvvm_ldg_f4(reinterpret_cast<const f4 *>(ptr));
  float4 ret;
  ret.x = rv[0];
  ret.y = rv[1];
  ret.z = rv[2];
  ret.w = rv[3];
  return ret;
}
inline __device__ double2 __ldg(const double2 *ptr) {
  typedef double d2 __attribute__((ext_vector_type(2)));
  d2 rv = __nvvm_ldg_d2(reinterpret_cast<const d2 *>(ptr));
  double2 ret;
  ret.x = rv[0];
  ret.y = rv[1];
  return ret;
}

// TODO: Implement these as intrinsics, so the backend can work its magic on
// these.  Alternatively, we could implement these as plain C and try to get
// llvm to recognize the relevant patterns.
inline __device__ unsigned __funnelshift_l(unsigned low32, unsigned high32,
                                           unsigned shiftWidth) {
  unsigned result;
  asm("shf.l.wrap.b32 %0, %1, %2, %3;"
      : "=r"(result)
      : "r"(low32), "r"(high32), "r"(shiftWidth));
  return result;
}
inline __device__ unsigned __funnelshift_lc(unsigned low32, unsigned high32,
                                            unsigned shiftWidth) {
  unsigned result;
  asm("shf.l.clamp.b32 %0, %1, %2, %3;"
      : "=r"(result)
      : "r"(low32), "r"(high32), "r"(shiftWidth));
  return result;
}
inline __device__ unsigned __funnelshift_r(unsigned low32, unsigned high32,
                                           unsigned shiftWidth) {
  unsigned result;
  asm("shf.r.wrap.b32 %0, %1, %2, %3;"
      : "=r"(result)
      : "r"(low32), "r"(high32), "r"(shiftWidth));
  return result;
}
inline __device__ unsigned __funnelshift_rc(unsigned low32, unsigned high32,
                                            unsigned shiftWidth) {
  unsigned ret;
  asm("shf.r.clamp.b32 %0, %1, %2, %3;"
      : "=r"(ret)
      : "r"(low32), "r"(high32), "r"(shiftWidth));
  return ret;
}

#endif // !defined(__CUDA_ARCH__) || __CUDA_ARCH__ >= 320

#if CUDA_VERSION >= 11000
extern "C" {
__device__ inline size_t __nv_cvta_generic_to_global_impl(const void *__ptr) {
  return (size_t)(void __attribute__((address_space(1))) *)__ptr;
}
__device__ inline size_t __nv_cvta_generic_to_shared_impl(const void *__ptr) {
  return (size_t)(void __attribute__((address_space(3))) *)__ptr;
}
__device__ inline size_t __nv_cvta_generic_to_constant_impl(const void *__ptr) {
  return (size_t)(void __attribute__((address_space(4))) *)__ptr;
}
__device__ inline size_t __nv_cvta_generic_to_local_impl(const void *__ptr) {
  return (size_t)(void __attribute__((address_space(5))) *)__ptr;
}
__device__ inline void *__nv_cvta_global_to_generic_impl(size_t __ptr) {
  return (void *)(void __attribute__((address_space(1))) *)__ptr;
}
__device__ inline void *__nv_cvta_shared_to_generic_impl(size_t __ptr) {
  return (void *)(void __attribute__((address_space(3))) *)__ptr;
}
__device__ inline void *__nv_cvta_constant_to_generic_impl(size_t __ptr) {
  return (void *)(void __attribute__((address_space(4))) *)__ptr;
}
__device__ inline void *__nv_cvta_local_to_generic_impl(size_t __ptr) {
  return (void *)(void __attribute__((address_space(5))) *)__ptr;
}
__device__ inline cuuint32_t __nvvm_get_smem_pointer(void *__ptr) {
  return __nv_cvta_generic_to_shared_impl(__ptr);
}
} // extern "C"

#if !defined(__CUDA_ARCH__) || __CUDA_ARCH__ >= 800
__device__ inline unsigned __reduce_add_sync(unsigned __mask,
                                             unsigned __value) {
  return __nvvm_redux_sync_add(__mask, __value);
}
__device__ inline unsigned __reduce_min_sync(unsigned __mask,
                                             unsigned __value) {
  return __nvvm_redux_sync_umin(__mask, __value);
}
__device__ inline unsigned __reduce_max_sync(unsigned __mask,
                                             unsigned __value) {
  return __nvvm_redux_sync_umax(__mask, __value);
}
__device__ inline int __reduce_min_sync(unsigned __mask, int __value) {
  return __nvvm_redux_sync_min(__mask, __value);
}
__device__ inline int __reduce_max_sync(unsigned __mask, int __value) {
  return __nvvm_redux_sync_max(__mask, __value);
}
__device__ inline unsigned __reduce_or_sync(unsigned __mask, unsigned __value) {
  return __nvvm_redux_sync_or(__mask, __value);
}
__device__ inline unsigned __reduce_and_sync(unsigned __mask,
                                             unsigned __value) {
  return __nvvm_redux_sync_and(__mask, __value);
}
__device__ inline unsigned __reduce_xor_sync(unsigned __mask,
                                             unsigned __value) {
  return __nvvm_redux_sync_xor(__mask, __value);
}

__device__ inline void __nv_memcpy_async_shared_global_4(void *__dst,
                                                         const void *__src,
                                                         unsigned __src_size) {
  __nvvm_cp_async_ca_shared_global_4(
      (void __attribute__((address_space(3))) *)__dst,
      (const void __attribute__((address_space(1))) *)__src, __src_size);
}
__device__ inline void __nv_memcpy_async_shared_global_8(void *__dst,
                                                         const void *__src,
                                                         unsigned __src_size) {
  __nvvm_cp_async_ca_shared_global_8(
      (void __attribute__((address_space(3))) *)__dst,
      (const void __attribute__((address_space(1))) *)__src, __src_size);
}
__device__ inline void __nv_memcpy_async_shared_global_16(void *__dst,
                                                          const void *__src,
                                                          unsigned __src_size) {
  __nvvm_cp_async_ca_shared_global_16(
      (void __attribute__((address_space(3))) *)__dst,
      (const void __attribute__((address_space(1))) *)__src, __src_size);
}

__device__ inline void *
__nv_associate_access_property(const void *__ptr, unsigned long long __prop) {
  // TODO: it appears to provide compiler with some sort of a hint. We do not
  // know what exactly it is supposed to do. However, CUDA headers suggest that
  // just passing through __ptr should not affect correctness. They do so on
  // pre-sm80 GPUs where this builtin is not available.
  return (void*)__ptr;
}
#endif // !defined(__CUDA_ARCH__) || __CUDA_ARCH__ >= 800

#if !defined(__CUDA_ARCH__) || __CUDA_ARCH__ >= 900
__device__ inline unsigned __isCtaShared(const void *ptr) {
  return __isShared(ptr);
}

__device__ inline unsigned __isClusterShared(const void *__ptr) {
  return __nvvm_isspacep_shared_cluster(__ptr);
}

__device__ inline void *__cluster_map_shared_rank(const void *__ptr,
                                                  unsigned __rank) {
  return __nvvm_mapa((void *)__ptr, __rank);
}

__device__ inline unsigned __cluster_query_shared_rank(const void *__ptr) {
  return __nvvm_getctarank((void *)__ptr);
}

__device__ inline uint2
__cluster_map_shared_multicast(const void *__ptr,
                               unsigned int __cluster_cta_mask) {
  return make_uint2((unsigned)__cvta_generic_to_shared(__ptr),
                    __cluster_cta_mask);
}

__device__ inline unsigned __clusterDimIsSpecified() {
  return __nvvm_is_explicit_cluster();
}

__device__ inline dim3 __clusterDim() {
  return dim3(__nvvm_read_ptx_sreg_cluster_nctaid_x(),
              __nvvm_read_ptx_sreg_cluster_nctaid_y(),
              __nvvm_read_ptx_sreg_cluster_nctaid_z());
}

__device__ inline dim3 __clusterRelativeBlockIdx() {
  return dim3(__nvvm_read_ptx_sreg_cluster_ctaid_x(),
              __nvvm_read_ptx_sreg_cluster_ctaid_y(),
              __nvvm_read_ptx_sreg_cluster_ctaid_z());
}

__device__ inline dim3 __clusterGridDimInClusters() {
  return dim3(__nvvm_read_ptx_sreg_nclusterid_x(),
              __nvvm_read_ptx_sreg_nclusterid_y(),
              __nvvm_read_ptx_sreg_nclusterid_z());
}

__device__ inline dim3 __clusterIdx() {
  return dim3(__nvvm_read_ptx_sreg_clusterid_x(),
              __nvvm_read_ptx_sreg_clusterid_y(),
              __nvvm_read_ptx_sreg_clusterid_z());
}

__device__ inline unsigned __clusterRelativeBlockRank() {
  return __nvvm_read_ptx_sreg_cluster_ctarank();
}

__device__ inline unsigned __clusterSizeInBlocks() {
  return __nvvm_read_ptx_sreg_cluster_nctarank();
}

__device__ inline void __cluster_barrier_arrive() {
  __nvvm_barrier_cluster_arrive();
}

__device__ inline void __cluster_barrier_arrive_relaxed() {
  __nvvm_barrier_cluster_arrive_relaxed();
}

__device__ inline void __cluster_barrier_wait() {
  __nvvm_barrier_cluster_wait();
}

__device__ inline void __threadfence_cluster() { __nvvm_fence_sc_cluster(); }

__device__ inline float2 atomicAdd(float2 *__ptr, float2 __val) {
  float2 __ret;
  __asm__("atom.add.v2.f32         {%0, %1}, [%2], {%3, %4};"
          : "=f"(__ret.x), "=f"(__ret.y)
          : "l"(__ptr), "f"(__val.x), "f"(__val.y));
  return __ret;
}

__device__ inline float2 atomicAdd_block(float2 *__ptr, float2 __val) {
  float2 __ret;
  __asm__("atom.cta.add.v2.f32         {%0, %1}, [%2], {%3, %4};"
          : "=f"(__ret.x), "=f"(__ret.y)
          : "l"(__ptr), "f"(__val.x), "f"(__val.y));
  return __ret;
}

__device__ inline float2 atomicAdd_system(float2 *__ptr, float2 __val) {
  float2 __ret;
  __asm__("atom.sys.add.v2.f32         {%0, %1}, [%2], {%3, %4};"
          : "=f"(__ret.x), "=f"(__ret.y)
          : "l"(__ptr), "f"(__val.x), "f"(__val.y));
  return __ret;
}

__device__ inline float4 atomicAdd(float4 *__ptr, float4 __val) {
  float4 __ret;
  __asm__("atom.add.v4.f32         {%0, %1, %2, %3}, [%4], {%5, %6, %7, %8};"
          : "=f"(__ret.x), "=f"(__ret.y), "=f"(__ret.z), "=f"(__ret.w)
          : "l"(__ptr), "f"(__val.x), "f"(__val.y), "f"(__val.z), "f"(__val.w));
  return __ret;
}

__device__ inline float4 atomicAdd_block(float4 *__ptr, float4 __val) {
  float4 __ret;
  __asm__(
      "atom.cta.add.v4.f32         {%0, %1, %2, %3}, [%4], {%5, %6, %7, %8};"
      : "=f"(__ret.x), "=f"(__ret.y), "=f"(__ret.z), "=f"(__ret.w)
      : "l"(__ptr), "f"(__val.x), "f"(__val.y), "f"(__val.z), "f"(__val.w));
  return __ret;
}

__device__ inline float4 atomicAdd_system(float4 *__ptr, float4 __val) {
  float4 __ret;
  __asm__(
      "atom.sys.add.v4.f32         {%0, %1, %2, %3}, [%4], {%5, %6, %7, %8};"
      : "=f"(__ret.x), "=f"(__ret.y), "=f"(__ret.z), "=f"(__ret.w)
      : "l"(__ptr), "f"(__val.x), "f"(__val.y), "f"(__val.z), "f"(__val.w)
      :);
  return __ret;
}

#endif // !defined(__CUDA_ARCH__) || __CUDA_ARCH__ >= 900
#endif // CUDA_VERSION >= 11000

#endif // defined(__CLANG_CUDA_INTRINSICS_H__)
