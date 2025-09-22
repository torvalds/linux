//===-- thread_annotations.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_THREAD_ANNOTATIONS_
#define SCUDO_THREAD_ANNOTATIONS_

// Enable thread safety attributes only with clang.
// The attributes can be safely ignored when compiling with other compilers.
#if defined(__clang__)
#define THREAD_ANNOTATION_ATTRIBUTE_(x) __attribute__((x))
#else
#define THREAD_ANNOTATION_ATTRIBUTE_(x) // no-op
#endif

#define CAPABILITY(x) THREAD_ANNOTATION_ATTRIBUTE_(capability(x))

#define SCOPED_CAPABILITY THREAD_ANNOTATION_ATTRIBUTE_(scoped_lockable)

#define GUARDED_BY(x) THREAD_ANNOTATION_ATTRIBUTE_(guarded_by(x))

#define PT_GUARDED_BY(x) THREAD_ANNOTATION_ATTRIBUTE_(pt_guarded_by(x))

#define ACQUIRED_BEFORE(...)                                                   \
  THREAD_ANNOTATION_ATTRIBUTE_(acquired_before(__VA_ARGS__))

#define ACQUIRED_AFTER(...)                                                    \
  THREAD_ANNOTATION_ATTRIBUTE_(acquired_after(__VA_ARGS__))

#define REQUIRES(...)                                                          \
  THREAD_ANNOTATION_ATTRIBUTE_(requires_capability(__VA_ARGS__))

#define REQUIRES_SHARED(...)                                                   \
  THREAD_ANNOTATION_ATTRIBUTE_(requires_shared_capability(__VA_ARGS__))

#define ACQUIRE(...)                                                           \
  THREAD_ANNOTATION_ATTRIBUTE_(acquire_capability(__VA_ARGS__))

#define ACQUIRE_SHARED(...)                                                    \
  THREAD_ANNOTATION_ATTRIBUTE_(acquire_shared_capability(__VA_ARGS__))

#define RELEASE(...)                                                           \
  THREAD_ANNOTATION_ATTRIBUTE_(release_capability(__VA_ARGS__))

#define RELEASE_SHARED(...)                                                    \
  THREAD_ANNOTATION_ATTRIBUTE_(release_shared_capability(__VA_ARGS__))

#define TRY_ACQUIRE(...)                                                       \
  THREAD_ANNOTATION_ATTRIBUTE_(try_acquire_capability(__VA_ARGS__))

#define TRY_ACQUIRE_SHARED(...)                                                \
  THREAD_ANNOTATION_ATTRIBUTE_(try_acquire_shared_capability(__VA_ARGS__))

#define EXCLUDES(...) THREAD_ANNOTATION_ATTRIBUTE_(locks_excluded(__VA_ARGS__))

#define ASSERT_CAPABILITY(x) THREAD_ANNOTATION_ATTRIBUTE_(assert_capability(x))

#define ASSERT_SHARED_CAPABILITY(x)                                            \
  THREAD_ANNOTATION_ATTRIBUTE_(assert_shared_capability(x))

#define RETURN_CAPABILITY(x) THREAD_ANNOTATION_ATTRIBUTE_(lock_returned(x))

#define NO_THREAD_SAFETY_ANALYSIS                                              \
  THREAD_ANNOTATION_ATTRIBUTE_(no_thread_safety_analysis)

#endif // SCUDO_THREAD_ANNOTATIONS_
