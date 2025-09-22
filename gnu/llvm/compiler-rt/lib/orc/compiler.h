//===--------- compiler.h - Compiler abstraction support --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of the ORC runtime support library.
//
// Most functionality in this file was swiped from llvm/Support/Compiler.h.
//
//===----------------------------------------------------------------------===//

#ifndef ORC_RT_COMPILER_H
#define ORC_RT_COMPILER_H

#if defined(_WIN32)
#define ORC_RT_INTERFACE extern "C"
#define ORC_RT_HIDDEN
#define ORC_RT_IMPORT extern "C" __declspec(dllimport)
#else
#define ORC_RT_INTERFACE extern "C" __attribute__((visibility("default")))
#define ORC_RT_HIDDEN __attribute__((visibility("hidden")))
#define ORC_RT_IMPORT extern "C"
#endif

#ifndef __has_builtin
# define __has_builtin(x) 0
#endif

// Only use __has_cpp_attribute in C++ mode. GCC defines __has_cpp_attribute in
// C mode, but the :: in __has_cpp_attribute(scoped::attribute) is invalid.
#ifndef ORC_RT_HAS_CPP_ATTRIBUTE
#if defined(__cplusplus) && defined(__has_cpp_attribute)
#define ORC_RT_HAS_CPP_ATTRIBUTE(x) __has_cpp_attribute(x)
#else
#define ORC_RT_HAS_CPP_ATTRIBUTE(x) 0
#endif
#endif

// Use the 'nodiscard' attribute in C++17 or newer mode.
#if defined(__cplusplus) && __cplusplus > 201402L &&                           \
    ORC_RT_HAS_CPP_ATTRIBUTE(nodiscard)
#define ORC_RT_NODISCARD [[nodiscard]]
#elif ORC_RT_HAS_CPP_ATTRIBUTE(clang::warn_unused_result)
#define ORC_RT_NODISCARD [[clang::warn_unused_result]]
// Clang in C++14 mode claims that it has the 'nodiscard' attribute, but also
// warns in the pedantic mode that 'nodiscard' is a C++17 extension (PR33518).
// Use the 'nodiscard' attribute in C++14 mode only with GCC.
// TODO: remove this workaround when PR33518 is resolved.
#elif defined(__GNUC__) && ORC_RT_HAS_CPP_ATTRIBUTE(nodiscard)
#define ORC_RT_NODISCARD [[nodiscard]]
#else
#define ORC_RT_NODISCARD
#endif

#if __has_builtin(__builtin_expect)
#define ORC_RT_LIKELY(EXPR) __builtin_expect((bool)(EXPR), true)
#define ORC_RT_UNLIKELY(EXPR) __builtin_expect((bool)(EXPR), false)
#else
#define ORC_RT_LIKELY(EXPR) (EXPR)
#define ORC_RT_UNLIKELY(EXPR) (EXPR)
#endif

#if defined(__APPLE__)
#define ORC_RT_WEAK_IMPORT __attribute__((weak_import))
#elif defined(_WIN32)
#define ORC_RT_WEAK_IMPORT
#else
#define ORC_RT_WEAK_IMPORT __attribute__((weak))
#endif

#endif // ORC_RT_COMPILER_H
