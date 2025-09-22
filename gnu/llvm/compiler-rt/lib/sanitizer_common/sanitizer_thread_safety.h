//===-- sanitizer_thread_safety.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is shared between sanitizer tools.
//
// Wrappers around thread safety annotations.
// https://clang.llvm.org/docs/ThreadSafetyAnalysis.html
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_THREAD_SAFETY_H
#define SANITIZER_THREAD_SAFETY_H

#if defined(__clang__)
#  define SANITIZER_THREAD_ANNOTATION(x) __attribute__((x))
#else
#  define SANITIZER_THREAD_ANNOTATION(x)
#endif

#define SANITIZER_MUTEX SANITIZER_THREAD_ANNOTATION(capability("mutex"))
#define SANITIZER_SCOPED_LOCK SANITIZER_THREAD_ANNOTATION(scoped_lockable)
#define SANITIZER_GUARDED_BY(x) SANITIZER_THREAD_ANNOTATION(guarded_by(x))
#define SANITIZER_PT_GUARDED_BY(x) SANITIZER_THREAD_ANNOTATION(pt_guarded_by(x))
#define SANITIZER_REQUIRES(...) \
  SANITIZER_THREAD_ANNOTATION(requires_capability(__VA_ARGS__))
#define SANITIZER_REQUIRES_SHARED(...) \
  SANITIZER_THREAD_ANNOTATION(requires_shared_capability(__VA_ARGS__))
#define SANITIZER_ACQUIRE(...) \
  SANITIZER_THREAD_ANNOTATION(acquire_capability(__VA_ARGS__))
#define SANITIZER_ACQUIRE_SHARED(...) \
  SANITIZER_THREAD_ANNOTATION(acquire_shared_capability(__VA_ARGS__))
#define SANITIZER_TRY_ACQUIRE(...) \
  SANITIZER_THREAD_ANNOTATION(try_acquire_capability(__VA_ARGS__))
#define SANITIZER_RELEASE(...) \
  SANITIZER_THREAD_ANNOTATION(release_capability(__VA_ARGS__))
#define SANITIZER_RELEASE_SHARED(...) \
  SANITIZER_THREAD_ANNOTATION(release_shared_capability(__VA_ARGS__))
#define SANITIZER_EXCLUDES(...) \
  SANITIZER_THREAD_ANNOTATION(locks_excluded(__VA_ARGS__))
#define SANITIZER_CHECK_LOCKED(...) \
  SANITIZER_THREAD_ANNOTATION(assert_capability(__VA_ARGS__))
#define SANITIZER_NO_THREAD_SAFETY_ANALYSIS \
  SANITIZER_THREAD_ANNOTATION(no_thread_safety_analysis)

#endif
