//===-- asan_mac.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Mac-specific details.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#if SANITIZER_APPLE

#include "asan_interceptors.h"
#include "asan_internal.h"
#include "asan_mapping.h"
#include "asan_stack.h"
#include "asan_thread.h"
#include "sanitizer_common/sanitizer_atomic.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_mac.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <libkern/OSAtomic.h>
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#include <mach-o/loader.h>
#include <pthread.h>
#include <stdlib.h>  // for free()
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <sys/ucontext.h>
#include <unistd.h>

// from <crt_externs.h>, but we don't have that file on iOS
extern "C" {
  extern char ***_NSGetArgv(void);
  extern char ***_NSGetEnviron(void);
}

namespace __asan {

void InitializePlatformInterceptors() {}
void InitializePlatformExceptionHandlers() {}
bool IsSystemHeapAddress (uptr addr) { return false; }

uptr FindDynamicShadowStart() {
  return MapDynamicShadow(MemToShadowSize(kHighMemEnd), ASAN_SHADOW_SCALE,
                          /*min_shadow_base_alignment*/ 0, kHighMemEnd,
                          GetMmapGranularity());
}

// No-op. Mac does not support static linkage anyway.
void AsanCheckDynamicRTPrereqs() {}

// No-op. Mac does not support static linkage anyway.
void AsanCheckIncompatibleRT() {}

void AsanApplyToGlobals(globals_op_fptr op, const void *needle) {
  // Find the Mach-O header for the image containing the needle
  Dl_info info;
  int err = dladdr(needle, &info);
  if (err == 0) return;

#if __LP64__
  const struct mach_header_64 *mh = (struct mach_header_64 *)info.dli_fbase;
#else
  const struct mach_header *mh = (struct mach_header *)info.dli_fbase;
#endif

  // Look up the __asan_globals section in that image and register its globals
  unsigned long size = 0;
  __asan_global *globals = (__asan_global *)getsectiondata(
      mh,
      "__DATA", "__asan_globals",
      &size);

  if (!globals) return;
  if (size % sizeof(__asan_global) != 0) return;
  op(globals, size / sizeof(__asan_global));
}

void FlushUnneededASanShadowMemory(uptr p, uptr size) {
  // Since asan's mapping is compacting, the shadow chunk may be
  // not page-aligned, so we only flush the page-aligned portion.
  ReleaseMemoryPagesToOS(MemToShadow(p), MemToShadow(p + size));
}

// Support for the following functions from libdispatch on Mac OS:
//   dispatch_async_f()
//   dispatch_async()
//   dispatch_sync_f()
//   dispatch_sync()
//   dispatch_after_f()
//   dispatch_after()
//   dispatch_group_async_f()
//   dispatch_group_async()
// TODO(glider): libdispatch API contains other functions that we don't support
// yet.
//
// dispatch_sync() and dispatch_sync_f() are synchronous, although chances are
// they can cause jobs to run on a thread different from the current one.
// TODO(glider): if so, we need a test for this (otherwise we should remove
// them).
//
// The following functions use dispatch_barrier_async_f() (which isn't a library
// function but is exported) and are thus supported:
//   dispatch_source_set_cancel_handler_f()
//   dispatch_source_set_cancel_handler()
//   dispatch_source_set_event_handler_f()
//   dispatch_source_set_event_handler()
//
// The reference manual for Grand Central Dispatch is available at
//   http://developer.apple.com/library/mac/#documentation/Performance/Reference/GCD_libdispatch_Ref/Reference/reference.html
// The implementation details are at
//   http://libdispatch.macosforge.org/trac/browser/trunk/src/queue.c

typedef void* dispatch_group_t;
typedef void* dispatch_queue_t;
typedef void* dispatch_source_t;
typedef u64 dispatch_time_t;
typedef void (*dispatch_function_t)(void *block);
typedef void* (*worker_t)(void *block);
typedef unsigned long dispatch_mach_reason;
typedef void *dispatch_mach_msg_t;
typedef int mach_error_t;
typedef void *dispatch_mach_t;

typedef void (*dispatch_mach_handler_function_t)(void *context,
                                                 dispatch_mach_reason reason,
                                                 dispatch_mach_msg_t message,
                                                 mach_error_t error);
#  if !defined(MISSING_BLOCKS_SUPPORT)
typedef void (^dispatch_mach_handler_t)(dispatch_mach_reason reason,
                                        dispatch_mach_msg_t message,
                                        mach_error_t error);
#  endif

// A wrapper for the ObjC blocks used to support libdispatch.
typedef struct {
  void *block;
  dispatch_function_t func;
  u32 parent_tid;
} asan_block_context_t;

ALWAYS_INLINE
void asan_register_worker_thread(int parent_tid, StackTrace *stack) {
  AsanThread *t = GetCurrentThread();
  if (!t) {
    t = AsanThread::Create(parent_tid, stack, /* detached */ true);
    t->Init();
    asanThreadRegistry().StartThread(t->tid(), GetTid(), ThreadType::Worker,
                                     nullptr);
    SetCurrentThread(t);
  }
}

// For use by only those functions that allocated the context via
// alloc_asan_context().
extern "C"
void asan_dispatch_call_block_and_release(void *block) {
  GET_STACK_TRACE_THREAD;
  asan_block_context_t *context = (asan_block_context_t*)block;
  VReport(2,
          "asan_dispatch_call_block_and_release(): "
          "context: %p, pthread_self: %p\n",
          block, (void*)pthread_self());
  asan_register_worker_thread(context->parent_tid, &stack);
  // Call the original dispatcher for the block.
  context->func(context->block);
  asan_free(context, &stack, FROM_MALLOC);
}

}  // namespace __asan

using namespace __asan;

// Wrap |ctxt| and |func| into an asan_block_context_t.
// The caller retains control of the allocated context.
extern "C"
asan_block_context_t *alloc_asan_context(void *ctxt, dispatch_function_t func,
                                         BufferedStackTrace *stack) {
  asan_block_context_t *asan_ctxt =
      (asan_block_context_t*) asan_malloc(sizeof(asan_block_context_t), stack);
  asan_ctxt->block = ctxt;
  asan_ctxt->func = func;
  asan_ctxt->parent_tid = GetCurrentTidOrInvalid();
  return asan_ctxt;
}

// Define interceptor for dispatch_*_f function with the three most common
// parameters: dispatch_queue_t, context, dispatch_function_t.
#define INTERCEPT_DISPATCH_X_F_3(dispatch_x_f)                                \
  INTERCEPTOR(void, dispatch_x_f, dispatch_queue_t dq, void *ctxt,            \
                                  dispatch_function_t func) {                 \
    GET_STACK_TRACE_THREAD;                                                   \
    asan_block_context_t *asan_ctxt = alloc_asan_context(ctxt, func, &stack); \
    if (Verbosity() >= 2) {                                     \
      Report(#dispatch_x_f "(): context: %p, pthread_self: %p\n",             \
             (void*)asan_ctxt, (void*)pthread_self());                        \
      PRINT_CURRENT_STACK();                                                  \
    }                                                                         \
    return REAL(dispatch_x_f)(dq, (void*)asan_ctxt,                           \
                              asan_dispatch_call_block_and_release);          \
  }

INTERCEPT_DISPATCH_X_F_3(dispatch_async_f)
INTERCEPT_DISPATCH_X_F_3(dispatch_sync_f)
INTERCEPT_DISPATCH_X_F_3(dispatch_barrier_async_f)

INTERCEPTOR(void, dispatch_after_f, dispatch_time_t when,
                                    dispatch_queue_t dq, void *ctxt,
                                    dispatch_function_t func) {
  GET_STACK_TRACE_THREAD;
  asan_block_context_t *asan_ctxt = alloc_asan_context(ctxt, func, &stack);
  if (Verbosity() >= 2) {
    Report("dispatch_after_f: %p\n", (void*)asan_ctxt);
    PRINT_CURRENT_STACK();
  }
  return REAL(dispatch_after_f)(when, dq, (void*)asan_ctxt,
                                asan_dispatch_call_block_and_release);
}

INTERCEPTOR(void, dispatch_group_async_f, dispatch_group_t group,
                                          dispatch_queue_t dq, void *ctxt,
                                          dispatch_function_t func) {
  GET_STACK_TRACE_THREAD;
  asan_block_context_t *asan_ctxt = alloc_asan_context(ctxt, func, &stack);
  if (Verbosity() >= 2) {
    Report("dispatch_group_async_f(): context: %p, pthread_self: %p\n",
           (void*)asan_ctxt, (void*)pthread_self());
    PRINT_CURRENT_STACK();
  }
  REAL(dispatch_group_async_f)(group, dq, (void*)asan_ctxt,
                               asan_dispatch_call_block_and_release);
}

#if !defined(MISSING_BLOCKS_SUPPORT)
extern "C" {
void dispatch_async(dispatch_queue_t dq, void(^work)(void));
void dispatch_group_async(dispatch_group_t dg, dispatch_queue_t dq,
                          void(^work)(void));
void dispatch_after(dispatch_time_t when, dispatch_queue_t queue,
                    void(^work)(void));
void dispatch_source_set_cancel_handler(dispatch_source_t ds,
                                        void(^work)(void));
void dispatch_source_set_event_handler(dispatch_source_t ds, void(^work)(void));
dispatch_mach_t dispatch_mach_create(const char *label, dispatch_queue_t queue,
                                     dispatch_mach_handler_t handler);
}

#define GET_ASAN_BLOCK(work) \
  void (^asan_block)(void);  \
  int parent_tid = GetCurrentTidOrInvalid(); \
  asan_block = ^(void) { \
    GET_STACK_TRACE_THREAD; \
    asan_register_worker_thread(parent_tid, &stack); \
    work(); \
  }

INTERCEPTOR(void, dispatch_async,
            dispatch_queue_t dq, void(^work)(void)) {
  ENABLE_FRAME_POINTER;
  GET_ASAN_BLOCK(work);
  REAL(dispatch_async)(dq, asan_block);
}

INTERCEPTOR(void, dispatch_group_async,
            dispatch_group_t dg, dispatch_queue_t dq, void(^work)(void)) {
  ENABLE_FRAME_POINTER;
  GET_ASAN_BLOCK(work);
  REAL(dispatch_group_async)(dg, dq, asan_block);
}

INTERCEPTOR(void, dispatch_after,
            dispatch_time_t when, dispatch_queue_t queue, void(^work)(void)) {
  ENABLE_FRAME_POINTER;
  GET_ASAN_BLOCK(work);
  REAL(dispatch_after)(when, queue, asan_block);
}

INTERCEPTOR(void, dispatch_source_set_cancel_handler,
            dispatch_source_t ds, void(^work)(void)) {
  if (!work) {
    REAL(dispatch_source_set_cancel_handler)(ds, work);
    return;
  }
  ENABLE_FRAME_POINTER;
  GET_ASAN_BLOCK(work);
  REAL(dispatch_source_set_cancel_handler)(ds, asan_block);
}

INTERCEPTOR(void, dispatch_source_set_event_handler,
            dispatch_source_t ds, void(^work)(void)) {
  ENABLE_FRAME_POINTER;
  GET_ASAN_BLOCK(work);
  REAL(dispatch_source_set_event_handler)(ds, asan_block);
}

INTERCEPTOR(void *, dispatch_mach_create, const char *label,
            dispatch_queue_t dq, dispatch_mach_handler_t handler) {
  int parent_tid = GetCurrentTidOrInvalid();
  return REAL(dispatch_mach_create)(
      label, dq,
      ^(dispatch_mach_reason reason, dispatch_mach_msg_t message,
        mach_error_t error) {
        GET_STACK_TRACE_THREAD;
        asan_register_worker_thread(parent_tid, &stack);
        handler(reason, message, error);
      });
}

INTERCEPTOR(void *, dispatch_mach_create_f, const char *label,
            dispatch_queue_t dq, void *ctxt,
            dispatch_mach_handler_function_t handler) {
  int parent_tid = GetCurrentTidOrInvalid();
  return REAL(dispatch_mach_create)(
      label, dq,
      ^(dispatch_mach_reason reason, dispatch_mach_msg_t message,
        mach_error_t error) {
        GET_STACK_TRACE_THREAD;
        asan_register_worker_thread(parent_tid, &stack);
        handler(ctxt, reason, message, error);
      });
}

#endif

#endif  // SANITIZER_APPLE
