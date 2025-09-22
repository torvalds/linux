//===-- tsan_dispatch_defs.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
//===----------------------------------------------------------------------===//
#ifndef TSAN_DISPATCH_DEFS_H
#define TSAN_DISPATCH_DEFS_H

#include "sanitizer_common/sanitizer_internal_defs.h"

typedef struct dispatch_object_s {} *dispatch_object_t;

#define DISPATCH_DECL(name) \
  typedef struct name##_s : public dispatch_object_s {} *name##_t

DISPATCH_DECL(dispatch_queue);
DISPATCH_DECL(dispatch_source);
DISPATCH_DECL(dispatch_group);
DISPATCH_DECL(dispatch_data);
DISPATCH_DECL(dispatch_semaphore);
DISPATCH_DECL(dispatch_io);

typedef void (*dispatch_function_t)(void *arg);
typedef void (^dispatch_block_t)(void);
typedef void (^dispatch_io_handler_t)(bool done, dispatch_data_t data,
                                      int error);

typedef long dispatch_once_t;
typedef __sanitizer::u64 dispatch_time_t;
typedef int dispatch_fd_t;
typedef unsigned long dispatch_io_type_t;
typedef unsigned long dispatch_io_close_flags_t;

extern "C" {
void *dispatch_get_context(dispatch_object_t object);
void dispatch_retain(dispatch_object_t object);
void dispatch_release(dispatch_object_t object);

extern const dispatch_block_t _dispatch_data_destructor_free;
extern const dispatch_block_t _dispatch_data_destructor_munmap;
} // extern "C"

#define DISPATCH_DATA_DESTRUCTOR_DEFAULT nullptr
#define DISPATCH_DATA_DESTRUCTOR_FREE    _dispatch_data_destructor_free
#define DISPATCH_DATA_DESTRUCTOR_MUNMAP  _dispatch_data_destructor_munmap

#if __has_attribute(noescape)
# define DISPATCH_NOESCAPE __attribute__((__noescape__))
#else
# define DISPATCH_NOESCAPE
#endif

// Data types used in dispatch APIs
typedef unsigned long size_t;
typedef unsigned long uintptr_t;
typedef __sanitizer::s64 off_t;
typedef __sanitizer::u16 mode_t;
typedef long long_t;

#endif  // TSAN_DISPATCH_DEFS_H
