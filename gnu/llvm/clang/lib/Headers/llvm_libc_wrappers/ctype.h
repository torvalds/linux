//===-- Wrapper for C standard ctype.h declarations on the GPU ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef __CLANG_LLVM_LIBC_WRAPPERS_CTYPE_H__
#define __CLANG_LLVM_LIBC_WRAPPERS_CTYPE_H__

#if !defined(_OPENMP) && !defined(__HIP__) && !defined(__CUDA__)
#error "This file is for GPU offloading compilation only"
#endif

// The GNU headers like to define 'toupper' and 'tolower' redundantly. This is
// necessary to prevent it from doing that and remapping our implementation.
#if (defined(__NVPTX__) || defined(__AMDGPU__)) && defined(__GLIBC__)
#pragma push_macro("__USE_EXTERN_INLINES")
#undef __USE_EXTERN_INLINES
#endif

#include_next <ctype.h>

#if (defined(__NVPTX__) || defined(__AMDGPU__)) && defined(__GLIBC__)
#pragma pop_macro("__USE_EXTERN_INLINES")
#endif

#if __has_include(<llvm-libc-decls/ctype.h>)

#if defined(__HIP__) || defined(__CUDA__)
#define __LIBC_ATTRS __attribute__((device))
#endif

// The GNU headers like to provide these as macros, we need to undefine them so
// they do not conflict with the following definitions for the GPU.

#pragma push_macro("isalnum")
#pragma push_macro("isalpha")
#pragma push_macro("isascii")
#pragma push_macro("isblank")
#pragma push_macro("iscntrl")
#pragma push_macro("isdigit")
#pragma push_macro("isgraph")
#pragma push_macro("islower")
#pragma push_macro("isprint")
#pragma push_macro("ispunct")
#pragma push_macro("isspace")
#pragma push_macro("isupper")
#pragma push_macro("isxdigit")
#pragma push_macro("toascii")
#pragma push_macro("tolower")
#pragma push_macro("toupper")

#undef isalnum
#undef isalpha
#undef isascii
#undef iscntrl
#undef isdigit
#undef islower
#undef isgraph
#undef isprint
#undef ispunct
#undef isspace
#undef isupper
#undef isblank
#undef isxdigit
#undef toascii
#undef tolower
#undef toupper

#pragma omp begin declare target

#include <llvm-libc-decls/ctype.h>

#pragma omp end declare target

// Restore the original macros when compiling on the host.
#if !defined(__NVPTX__) && !defined(__AMDGPU__)
#pragma pop_macro("isalnum")
#pragma pop_macro("isalpha")
#pragma pop_macro("isascii")
#pragma pop_macro("isblank")
#pragma pop_macro("iscntrl")
#pragma pop_macro("isdigit")
#pragma pop_macro("isgraph")
#pragma pop_macro("islower")
#pragma pop_macro("isprint")
#pragma pop_macro("ispunct")
#pragma pop_macro("isspace")
#pragma pop_macro("isupper")
#pragma pop_macro("isxdigit")
#pragma pop_macro("toascii")
#pragma pop_macro("tolower")
#pragma pop_macro("toupper")
#endif

#undef __LIBC_ATTRS

#endif

#endif // __CLANG_LLVM_LIBC_WRAPPERS_CTYPE_H__
