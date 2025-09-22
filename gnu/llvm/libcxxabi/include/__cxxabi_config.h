//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef ____CXXABI_CONFIG_H
#define ____CXXABI_CONFIG_H

#if defined(__arm__) && !defined(__USING_SJLJ_EXCEPTIONS__) &&                 \
    !defined(__ARM_DWARF_EH__) && !defined(__SEH__)
#define _LIBCXXABI_ARM_EHABI
#endif

#if !defined(__has_attribute)
#define __has_attribute(_attribute_) 0
#endif

#if defined(__clang__)
#  define _LIBCXXABI_COMPILER_CLANG
#  ifndef __apple_build_version__
#    define _LIBCXXABI_CLANG_VER (__clang_major__ * 100 + __clang_minor__)
#  endif
#elif defined(__GNUC__)
#  define _LIBCXXABI_COMPILER_GCC
#elif defined(_MSC_VER)
#  define _LIBCXXABI_COMPILER_MSVC
#elif defined(__IBMCPP__)
#  define _LIBCXXABI_COMPILER_IBM
#endif

#if defined(_WIN32)
 #if defined(_LIBCXXABI_DISABLE_VISIBILITY_ANNOTATIONS) || (defined(__MINGW32__) && !defined(_LIBCXXABI_BUILDING_LIBRARY))
  #define _LIBCXXABI_HIDDEN
  #define _LIBCXXABI_DATA_VIS
  #define _LIBCXXABI_FUNC_VIS
  #define _LIBCXXABI_TYPE_VIS
 #elif defined(_LIBCXXABI_BUILDING_LIBRARY)
  #define _LIBCXXABI_HIDDEN
  #define _LIBCXXABI_DATA_VIS __declspec(dllexport)
  #define _LIBCXXABI_FUNC_VIS __declspec(dllexport)
  #define _LIBCXXABI_TYPE_VIS __declspec(dllexport)
 #else
  #define _LIBCXXABI_HIDDEN
  #define _LIBCXXABI_DATA_VIS __declspec(dllimport)
  #define _LIBCXXABI_FUNC_VIS __declspec(dllimport)
  #define _LIBCXXABI_TYPE_VIS __declspec(dllimport)
 #endif
#else
 #if !defined(_LIBCXXABI_DISABLE_VISIBILITY_ANNOTATIONS)
  #define _LIBCXXABI_HIDDEN __attribute__((__visibility__("hidden")))
  #define _LIBCXXABI_DATA_VIS __attribute__((__visibility__("default")))
  #define _LIBCXXABI_FUNC_VIS __attribute__((__visibility__("default")))
  #if __has_attribute(__type_visibility__)
   #define _LIBCXXABI_TYPE_VIS __attribute__((__type_visibility__("default")))
  #else
   #define _LIBCXXABI_TYPE_VIS __attribute__((__visibility__("default")))
  #endif
 #else
  #define _LIBCXXABI_HIDDEN
  #define _LIBCXXABI_DATA_VIS
  #define _LIBCXXABI_FUNC_VIS
  #define _LIBCXXABI_TYPE_VIS
 #endif
#endif

#if defined(_LIBCXXABI_COMPILER_MSVC)
#define _LIBCXXABI_WEAK
#else
#define _LIBCXXABI_WEAK __attribute__((__weak__))
#endif

#if defined(__clang__)
#define _LIBCXXABI_COMPILER_CLANG
#elif defined(__GNUC__)
#define _LIBCXXABI_COMPILER_GCC
#endif

#if __has_attribute(__no_sanitize__) && defined(_LIBCXXABI_COMPILER_CLANG)
#define _LIBCXXABI_NO_CFI __attribute__((__no_sanitize__("cfi")))
#else
#define _LIBCXXABI_NO_CFI
#endif

// wasm32 follows the arm32 ABI convention of using 32-bit guard.
#if defined(__arm__) || defined(__wasm32__) || defined(__ARM64_ARCH_8_32__)
#  define _LIBCXXABI_GUARD_ABI_ARM
#endif

#if defined(_LIBCXXABI_COMPILER_CLANG)
#  if !__has_feature(cxx_exceptions)
#    define _LIBCXXABI_NO_EXCEPTIONS
#  endif
#elif defined(_LIBCXXABI_COMPILER_GCC) && !defined(__EXCEPTIONS)
#  define _LIBCXXABI_NO_EXCEPTIONS
#endif

#if defined(_WIN32)
#define _LIBCXXABI_DTOR_FUNC __thiscall
#else
#define _LIBCXXABI_DTOR_FUNC
#endif

#endif // ____CXXABI_CONFIG_H
