/*===---- __clang_cuda_runtime_wrapper.h - CUDA runtime support -------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

/*
 * WARNING: This header is intended to be directly -include'd by
 * the compiler and is not supposed to be included by users.
 *
 * CUDA headers are implemented in a way that currently makes it
 * impossible for user code to #include directly when compiling with
 * Clang. They present different view of CUDA-supplied functions
 * depending on where in NVCC's compilation pipeline the headers are
 * included. Neither of these modes provides function definitions with
 * correct attributes, so we use preprocessor to force the headers
 * into a form that Clang can use.
 *
 * Similarly to NVCC which -include's cuda_runtime.h, Clang -include's
 * this file during every CUDA compilation.
 */

#ifndef __CLANG_CUDA_RUNTIME_WRAPPER_H__
#define __CLANG_CUDA_RUNTIME_WRAPPER_H__

#if defined(__CUDA__) && defined(__clang__)

// Include some forward declares that must come before cmath.
#include <__clang_cuda_math_forward_declares.h>

// Define __CUDACC__ early as libstdc++ standard headers with GNU extensions
// enabled depend on it to avoid using __float128, which is unsupported in
// CUDA.
#define __CUDACC__

// Include some standard headers to avoid CUDA headers including them
// while some required macros (like __THROW) are in a weird state.
#include <cmath>
#include <cstdlib>
#include <stdlib.h>
#include <string.h>
#undef __CUDACC__

// Preserve common macros that will be changed below by us or by CUDA
// headers.
#pragma push_macro("__THROW")
#pragma push_macro("__CUDA_ARCH__")

// WARNING: Preprocessor hacks below are based on specific details of
// CUDA-7.x headers and are not expected to work with any other
// version of CUDA headers.
#include "cuda.h"
#if !defined(CUDA_VERSION)
#error "cuda.h did not define CUDA_VERSION"
#elif CUDA_VERSION < 7000
#error "Unsupported CUDA version!"
#endif

#pragma push_macro("__CUDA_INCLUDE_COMPILER_INTERNAL_HEADERS__")
#if CUDA_VERSION >= 10000
#define __CUDA_INCLUDE_COMPILER_INTERNAL_HEADERS__
#endif

// Make largest subset of device functions available during host
// compilation.
#ifndef __CUDA_ARCH__
#define __CUDA_ARCH__ 9999
#endif

#include "__clang_cuda_builtin_vars.h"

// No need for device_launch_parameters.h as __clang_cuda_builtin_vars.h above
// has taken care of builtin variables declared in the file.
#define __DEVICE_LAUNCH_PARAMETERS_H__

// {math,device}_functions.h only have declarations of the
// functions. We don't need them as we're going to pull in their
// definitions from .hpp files.
#define __DEVICE_FUNCTIONS_H__
#define __MATH_FUNCTIONS_H__
#define __COMMON_FUNCTIONS_H__
// device_functions_decls is replaced by __clang_cuda_device_functions.h
// included below.
#define __DEVICE_FUNCTIONS_DECLS_H__

#undef __CUDACC__
#if CUDA_VERSION < 9000
#define __CUDABE__
#else
#define __CUDACC__
#define __CUDA_LIBDEVICE__
#endif
// Disables definitions of device-side runtime support stubs in
// cuda_device_runtime_api.h
#include "host_defines.h"
#undef __CUDACC__
#include "driver_types.h"
#include "host_config.h"

// Temporarily replace "nv_weak" with weak, so __attribute__((nv_weak)) in
// cuda_device_runtime_api.h ends up being __attribute__((weak)) which is the
// functional equivalent of what we need.
#pragma push_macro("nv_weak")
#define nv_weak weak
#undef __CUDABE__
#undef __CUDA_LIBDEVICE__
#define __CUDACC__
#include "cuda_runtime.h"

#pragma pop_macro("nv_weak")
#undef __CUDACC__
#define __CUDABE__

// CUDA headers use __nvvm_memcpy and __nvvm_memset which Clang does
// not have at the moment. Emulate them with a builtin memcpy/memset.
#define __nvvm_memcpy(s, d, n, a) __builtin_memcpy(s, d, n)
#define __nvvm_memset(d, c, n, a) __builtin_memset(d, c, n)

#if CUDA_VERSION < 9000
#include "crt/device_runtime.h"
#endif
#include "crt/host_runtime.h"
// device_runtime.h defines __cxa_* macros that will conflict with
// cxxabi.h.
// FIXME: redefine these as __device__ functions.
#undef __cxa_vec_ctor
#undef __cxa_vec_cctor
#undef __cxa_vec_dtor
#undef __cxa_vec_new
#undef __cxa_vec_new2
#undef __cxa_vec_new3
#undef __cxa_vec_delete2
#undef __cxa_vec_delete
#undef __cxa_vec_delete3
#undef __cxa_pure_virtual

// math_functions.hpp expects this host function be defined on MacOS, but it
// ends up not being there because of the games we play here.  Just define it
// ourselves; it's simple enough.
#ifdef __APPLE__
inline __host__ double __signbitd(double x) {
  return std::signbit(x);
}
#endif

// CUDA 9.1 no longer provides declarations for libdevice functions, so we need
// to provide our own.
#include <__clang_cuda_libdevice_declares.h>

// Wrappers for many device-side standard library functions, incl. math
// functions, became compiler builtins in CUDA-9 and have been removed from the
// CUDA headers. Clang now provides its own implementation of the wrappers.
#if CUDA_VERSION >= 9000
#include <__clang_cuda_device_functions.h>
#include <__clang_cuda_math.h>
#endif

// __THROW is redefined to be empty by device_functions_decls.h in CUDA. Clang's
// counterpart does not do it, so we need to make it empty here to keep
// following CUDA includes happy.
#undef __THROW
#define __THROW

// CUDA 8.0.41 relies on __USE_FAST_MATH__ and __CUDA_PREC_DIV's values.
// Previous versions used to check whether they are defined or not.
// CU_DEVICE_INVALID macro is only defined in 8.0.41, so we use it
// here to detect the switch.

#if defined(CU_DEVICE_INVALID)
#if !defined(__USE_FAST_MATH__)
#define __USE_FAST_MATH__ 0
#endif

#if !defined(__CUDA_PREC_DIV)
#define __CUDA_PREC_DIV 0
#endif
#endif

// Temporarily poison __host__ macro to ensure it's not used by any of
// the headers we're about to include.
#pragma push_macro("__host__")
#define __host__ UNEXPECTED_HOST_ATTRIBUTE

// device_functions.hpp and math_functions*.hpp use 'static
// __forceinline__' (with no __device__) for definitions of device
// functions. Temporarily redefine __forceinline__ to include
// __device__.
#pragma push_macro("__forceinline__")
#define __forceinline__ __device__ __inline__ __attribute__((always_inline))
#if CUDA_VERSION < 9000
#include "device_functions.hpp"
#endif

// math_function.hpp uses the __USE_FAST_MATH__ macro to determine whether we
// get the slow-but-accurate or fast-but-inaccurate versions of functions like
// sin and exp.  This is controlled in clang by -fgpu-approx-transcendentals.
//
// device_functions.hpp uses __USE_FAST_MATH__ for a different purpose (fast vs.
// slow divides), so we need to scope our define carefully here.
#pragma push_macro("__USE_FAST_MATH__")
#if defined(__CLANG_GPU_APPROX_TRANSCENDENTALS__)
#define __USE_FAST_MATH__ 1
#endif

#if CUDA_VERSION >= 9000
#include "crt/math_functions.hpp"
#else
#include "math_functions.hpp"
#endif

#pragma pop_macro("__USE_FAST_MATH__")

#if CUDA_VERSION < 9000
#include "math_functions_dbl_ptx3.hpp"
#endif
#pragma pop_macro("__forceinline__")

// Pull in host-only functions that are only available when neither
// __CUDACC__ nor __CUDABE__ are defined.
#undef __MATH_FUNCTIONS_HPP__
#undef __CUDABE__
#if CUDA_VERSION < 9000
#include "math_functions.hpp"
#endif
// Alas, additional overloads for these functions are hard to get to.
// Considering that we only need these overloads for a few functions,
// we can provide them here.
static inline float rsqrt(float __a) { return rsqrtf(__a); }
static inline float rcbrt(float __a) { return rcbrtf(__a); }
static inline float sinpi(float __a) { return sinpif(__a); }
static inline float cospi(float __a) { return cospif(__a); }
static inline void sincospi(float __a, float *__b, float *__c) {
  return sincospif(__a, __b, __c);
}
static inline float erfcinv(float __a) { return erfcinvf(__a); }
static inline float normcdfinv(float __a) { return normcdfinvf(__a); }
static inline float normcdf(float __a) { return normcdff(__a); }
static inline float erfcx(float __a) { return erfcxf(__a); }

#if CUDA_VERSION < 9000
// For some reason single-argument variant is not always declared by
// CUDA headers. Alas, device_functions.hpp included below needs it.
static inline __device__ void __brkpt(int __c) { __brkpt(); }
#endif

// Now include *.hpp with definitions of various GPU functions.  Alas,
// a lot of thins get declared/defined with __host__ attribute which
// we don't want and we have to define it out. We also have to include
// {device,math}_functions.hpp again in order to extract the other
// branch of #if/else inside.
#define __host__
#undef __CUDABE__
#define __CUDACC__
#if CUDA_VERSION >= 9000
// Some atomic functions became compiler builtins in CUDA-9 , so we need their
// declarations.
#include "device_atomic_functions.h"
#endif
#undef __DEVICE_FUNCTIONS_HPP__
#include "device_atomic_functions.hpp"
#if CUDA_VERSION >= 9000
#include "crt/device_functions.hpp"
#include "crt/device_double_functions.hpp"
#else
#include "device_functions.hpp"
#define __CUDABE__
#include "device_double_functions.h"
#undef __CUDABE__
#endif
#include "sm_20_atomic_functions.hpp"
// Predicate functions used in `__builtin_assume` need to have no side effect.
// However, sm_20_intrinsics.hpp doesn't define them with neither pure nor
// const attribute. Rename definitions from sm_20_intrinsics.hpp and re-define
// them as pure ones.
#pragma push_macro("__isGlobal")
#pragma push_macro("__isShared")
#pragma push_macro("__isConstant")
#pragma push_macro("__isLocal")
#define __isGlobal __ignored_cuda___isGlobal
#define __isShared __ignored_cuda___isShared
#define __isConstant __ignored_cuda___isConstant
#define __isLocal __ignored_cuda___isLocal
#include "sm_20_intrinsics.hpp"
#pragma pop_macro("__isGlobal")
#pragma pop_macro("__isShared")
#pragma pop_macro("__isConstant")
#pragma pop_macro("__isLocal")
#pragma push_macro("__DEVICE__")
#define __DEVICE__ static __device__ __forceinline__ __attribute__((const))
__DEVICE__ unsigned int __isGlobal(const void *p) {
  return __nvvm_isspacep_global(p);
}
__DEVICE__ unsigned int __isShared(const void *p) {
  return __nvvm_isspacep_shared(p);
}
__DEVICE__ unsigned int __isConstant(const void *p) {
  return __nvvm_isspacep_const(p);
}
__DEVICE__ unsigned int __isLocal(const void *p) {
  return __nvvm_isspacep_local(p);
}
#pragma pop_macro("__DEVICE__")
#include "sm_32_atomic_functions.hpp"

// Don't include sm_30_intrinsics.h and sm_32_intrinsics.h.  These define the
// __shfl and __ldg intrinsics using inline (volatile) asm, but we want to
// define them using builtins so that the optimizer can reason about and across
// these instructions.  In particular, using intrinsics for ldg gets us the
// [addr+imm] addressing mode, which, although it doesn't actually exist in the
// hardware, seems to generate faster machine code because ptxas can more easily
// reason about our code.

#if CUDA_VERSION >= 8000
#pragma push_macro("__CUDA_ARCH__")
#undef __CUDA_ARCH__
#include "sm_60_atomic_functions.hpp"
#include "sm_61_intrinsics.hpp"
#pragma pop_macro("__CUDA_ARCH__")
#endif

#undef __MATH_FUNCTIONS_HPP__

// math_functions.hpp defines ::signbit as a __host__ __device__ function.  This
// conflicts with libstdc++'s constexpr ::signbit, so we have to rename
// math_function.hpp's ::signbit.  It's guarded by #undef signbit, but that's
// conditional on __GNUC__.  :)
#pragma push_macro("signbit")
#pragma push_macro("__GNUC__")
#undef __GNUC__
#define signbit __ignored_cuda_signbit

// CUDA-9 omits device-side definitions of some math functions if it sees
// include guard from math.h wrapper from libstdc++. We have to undo the header
// guard temporarily to get the definitions we need.
#pragma push_macro("_GLIBCXX_MATH_H")
#pragma push_macro("_LIBCPP_VERSION")
#if CUDA_VERSION >= 9000
#undef _GLIBCXX_MATH_H
// We also need to undo another guard that checks for libc++ 3.8+
#ifdef _LIBCPP_VERSION
#define _LIBCPP_VERSION 3700
#endif
#endif

#if CUDA_VERSION >= 9000
#include "crt/math_functions.hpp"
#else
#include "math_functions.hpp"
#endif
#pragma pop_macro("_GLIBCXX_MATH_H")
#pragma pop_macro("_LIBCPP_VERSION")
#pragma pop_macro("__GNUC__")
#pragma pop_macro("signbit")

#pragma pop_macro("__host__")

// __clang_cuda_texture_intrinsics.h must be included first in order to provide
// implementation for __nv_tex_surf_handler that CUDA's headers depend on.
// The implementation requires c++11 and only works with CUDA-9 or newer.
#if __cplusplus >= 201103L && CUDA_VERSION >= 9000
// clang-format off
#include <__clang_cuda_texture_intrinsics.h>
// clang-format on
#else
#if CUDA_VERSION >= 9000
// Provide a hint that texture support needs C++11.
template <typename T> struct __nv_tex_needs_cxx11 {
  const static bool value = false;
};
template <class T>
__host__ __device__ void __nv_tex_surf_handler(const char *name, T *ptr,
                                               cudaTextureObject_t obj,
                                               float x) {
  _Static_assert(__nv_tex_needs_cxx11<T>::value,
                 "Texture support requires C++11");
}
#else
// Textures in CUDA-8 and older are not supported by clang.There's no
// convenient way to intercept texture use in these versions, so we can't
// produce a meaningful error. The source code that attempts to use textures
// will continue to fail as it does now.
#endif // CUDA_VERSION
#endif // __cplusplus >= 201103L && CUDA_VERSION >= 9000
#include "texture_fetch_functions.h"
#include "texture_indirect_functions.h"

// Restore state of __CUDA_ARCH__ and __THROW we had on entry.
#pragma pop_macro("__CUDA_ARCH__")
#pragma pop_macro("__THROW")

// Set up compiler macros expected to be seen during compilation.
#undef __CUDABE__
#define __CUDACC__

extern "C" {
// Device-side CUDA system calls.
// http://docs.nvidia.com/cuda/ptx-writers-guide-to-interoperability/index.html#system-calls
// We need these declarations and wrappers for device-side
// malloc/free/printf calls to work without relying on
// -fcuda-disable-target-call-checks option.
__device__ int vprintf(const char *, const char *);
__device__ void free(void *) __attribute((nothrow));
__device__ void *malloc(size_t) __attribute((nothrow)) __attribute__((malloc));

// __assertfail() used to have a `noreturn` attribute. Unfortunately that
// contributed to triggering the longstanding bug in ptxas when assert was used
// in sufficiently convoluted code. See
// https://bugs.llvm.org/show_bug.cgi?id=27738 for the details.
__device__ void __assertfail(const char *__message, const char *__file,
                             unsigned __line, const char *__function,
                             size_t __charSize);

// In order for standard assert() macro on linux to work we need to
// provide device-side __assert_fail()
__device__ static inline void __assert_fail(const char *__message,
                                            const char *__file, unsigned __line,
                                            const char *__function) {
  __assertfail(__message, __file, __line, __function, sizeof(char));
}

// Clang will convert printf into vprintf, but we still need
// device-side declaration for it.
__device__ int printf(const char *, ...);
} // extern "C"

// We also need device-side std::malloc and std::free.
namespace std {
__device__ static inline void free(void *__ptr) { ::free(__ptr); }
__device__ static inline void *malloc(size_t __size) {
  return ::malloc(__size);
}
} // namespace std

// Out-of-line implementations from __clang_cuda_builtin_vars.h.  These need to
// come after we've pulled in the definition of uint3 and dim3.

__device__ inline __cuda_builtin_threadIdx_t::operator dim3() const {
  return dim3(x, y, z);
}

__device__ inline __cuda_builtin_threadIdx_t::operator uint3() const {
  return {x, y, z};
}

__device__ inline __cuda_builtin_blockIdx_t::operator dim3() const {
  return dim3(x, y, z);
}

__device__ inline __cuda_builtin_blockIdx_t::operator uint3() const {
  return {x, y, z};
}

__device__ inline __cuda_builtin_blockDim_t::operator dim3() const {
  return dim3(x, y, z);
}

__device__ inline __cuda_builtin_blockDim_t::operator uint3() const {
  return {x, y, z};
}

__device__ inline __cuda_builtin_gridDim_t::operator dim3() const {
  return dim3(x, y, z);
}

__device__ inline __cuda_builtin_gridDim_t::operator uint3() const {
  return {x, y, z};
}

#include <__clang_cuda_cmath.h>
#include <__clang_cuda_intrinsics.h>
#include <__clang_cuda_complex_builtins.h>

// curand_mtgp32_kernel helpfully redeclares blockDim and threadIdx in host
// mode, giving them their "proper" types of dim3 and uint3.  This is
// incompatible with the types we give in __clang_cuda_builtin_vars.h.  As as
// hack, force-include the header (nvcc doesn't include it by default) but
// redefine dim3 and uint3 to our builtin types.  (Thankfully dim3 and uint3 are
// only used here for the redeclarations of blockDim and threadIdx.)
#pragma push_macro("dim3")
#pragma push_macro("uint3")
#define dim3 __cuda_builtin_blockDim_t
#define uint3 __cuda_builtin_threadIdx_t
#include "curand_mtgp32_kernel.h"
#pragma pop_macro("dim3")
#pragma pop_macro("uint3")
#pragma pop_macro("__USE_FAST_MATH__")
#pragma pop_macro("__CUDA_INCLUDE_COMPILER_INTERNAL_HEADERS__")

// CUDA runtime uses this undocumented function to access kernel launch
// configuration. The declaration is in crt/device_functions.h but that file
// includes a lot of other stuff we don't want. Instead, we'll provide our own
// declaration for it here.
#if CUDA_VERSION >= 9020
extern "C" unsigned __cudaPushCallConfiguration(dim3 gridDim, dim3 blockDim,
                                                size_t sharedMem = 0,
                                                void *stream = 0);
#endif

#endif // __CUDA__
#endif // __CLANG_CUDA_RUNTIME_WRAPPER_H__
