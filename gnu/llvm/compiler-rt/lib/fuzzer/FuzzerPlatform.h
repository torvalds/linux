//===-- FuzzerPlatform.h --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Common platform macros.
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZER_PLATFORM_H
#define LLVM_FUZZER_PLATFORM_H

// Platform detection.
#ifdef __linux__
#define LIBFUZZER_APPLE 0
#define LIBFUZZER_FUCHSIA 0
#define LIBFUZZER_LINUX 1
#define LIBFUZZER_NETBSD 0
#define LIBFUZZER_FREEBSD 0
#define LIBFUZZER_WINDOWS 0
#define LIBFUZZER_EMSCRIPTEN 0
#elif __APPLE__
#define LIBFUZZER_APPLE 1
#define LIBFUZZER_FUCHSIA 0
#define LIBFUZZER_LINUX 0
#define LIBFUZZER_NETBSD 0
#define LIBFUZZER_FREEBSD 0
#define LIBFUZZER_WINDOWS 0
#define LIBFUZZER_EMSCRIPTEN 0
#elif __NetBSD__
#define LIBFUZZER_APPLE 0
#define LIBFUZZER_FUCHSIA 0
#define LIBFUZZER_LINUX 0
#define LIBFUZZER_NETBSD 1
#define LIBFUZZER_FREEBSD 0
#define LIBFUZZER_WINDOWS 0
#define LIBFUZZER_EMSCRIPTEN 0
#elif __FreeBSD__
#define LIBFUZZER_APPLE 0
#define LIBFUZZER_FUCHSIA 0
#define LIBFUZZER_LINUX 0
#define LIBFUZZER_NETBSD 0
#define LIBFUZZER_FREEBSD 1
#define LIBFUZZER_WINDOWS 0
#define LIBFUZZER_EMSCRIPTEN 0
#elif _WIN32
#define LIBFUZZER_APPLE 0
#define LIBFUZZER_FUCHSIA 0
#define LIBFUZZER_LINUX 0
#define LIBFUZZER_NETBSD 0
#define LIBFUZZER_FREEBSD 0
#define LIBFUZZER_WINDOWS 1
#define LIBFUZZER_EMSCRIPTEN 0
#elif __Fuchsia__
#define LIBFUZZER_APPLE 0
#define LIBFUZZER_FUCHSIA 1
#define LIBFUZZER_LINUX 0
#define LIBFUZZER_NETBSD 0
#define LIBFUZZER_FREEBSD 0
#define LIBFUZZER_WINDOWS 0
#define LIBFUZZER_EMSCRIPTEN 0
#elif __EMSCRIPTEN__
#define LIBFUZZER_APPLE 0
#define LIBFUZZER_FUCHSIA 0
#define LIBFUZZER_LINUX 0
#define LIBFUZZER_NETBSD 0
#define LIBFUZZER_FREEBSD 0
#define LIBFUZZER_WINDOWS 0
#define LIBFUZZER_EMSCRIPTEN 1
#else
#error "Support for your platform has not been implemented"
#endif

#if defined(_MSC_VER) && !defined(__clang__)
// MSVC compiler is being used.
#define LIBFUZZER_MSVC 1
#else
#define LIBFUZZER_MSVC 0
#endif

#ifndef __has_attribute
#define __has_attribute(x) 0
#endif

#define LIBFUZZER_POSIX                                                        \
  (LIBFUZZER_APPLE || LIBFUZZER_LINUX || LIBFUZZER_NETBSD ||                   \
   LIBFUZZER_FREEBSD || LIBFUZZER_EMSCRIPTEN)

#ifdef __x86_64
#if __has_attribute(target)
#define ATTRIBUTE_TARGET_POPCNT __attribute__((target("popcnt")))
#else
#define ATTRIBUTE_TARGET_POPCNT
#endif
#else
#define ATTRIBUTE_TARGET_POPCNT
#endif

#ifdef __clang__ // avoid gcc warning.
#if __has_attribute(no_sanitize)
#define ATTRIBUTE_NO_SANITIZE_MEMORY __attribute__((no_sanitize("memory")))
#else
#define ATTRIBUTE_NO_SANITIZE_MEMORY
#endif
#define ALWAYS_INLINE __attribute__((always_inline))
#else
#define ATTRIBUTE_NO_SANITIZE_MEMORY
#define ALWAYS_INLINE
#endif // __clang__

#if LIBFUZZER_WINDOWS
#define ATTRIBUTE_NO_SANITIZE_ADDRESS
#else
#define ATTRIBUTE_NO_SANITIZE_ADDRESS __attribute__((no_sanitize_address))
#endif

#if LIBFUZZER_WINDOWS
#define ATTRIBUTE_ALIGNED(X) __declspec(align(X))
#define ATTRIBUTE_INTERFACE __declspec(dllexport)
// This is used for __sancov_lowest_stack which is needed for
// -fsanitize-coverage=stack-depth. That feature is not yet available on
// Windows, so make the symbol static to avoid linking errors.
#define ATTRIBUTES_INTERFACE_TLS_INITIAL_EXEC static
#define ATTRIBUTE_NOINLINE __declspec(noinline)
#else
#define ATTRIBUTE_ALIGNED(X) __attribute__((aligned(X)))
#define ATTRIBUTE_INTERFACE __attribute__((visibility("default")))
#define ATTRIBUTES_INTERFACE_TLS_INITIAL_EXEC                                  \
  ATTRIBUTE_INTERFACE __attribute__((tls_model("initial-exec"))) thread_local

#define ATTRIBUTE_NOINLINE __attribute__((noinline))
#endif

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ATTRIBUTE_NO_SANITIZE_ALL ATTRIBUTE_NO_SANITIZE_ADDRESS
#elif __has_feature(memory_sanitizer)
#define ATTRIBUTE_NO_SANITIZE_ALL ATTRIBUTE_NO_SANITIZE_MEMORY
#else
#define ATTRIBUTE_NO_SANITIZE_ALL
#endif
#else
#define ATTRIBUTE_NO_SANITIZE_ALL
#endif

#endif // LLVM_FUZZER_PLATFORM_H
