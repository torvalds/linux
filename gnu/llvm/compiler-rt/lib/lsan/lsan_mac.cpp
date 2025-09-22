//===-- lsan_mac.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of LeakSanitizer, a memory leak checker.
//
// Mac-specific details.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#if SANITIZER_APPLE

#include "interception/interception.h"
#include "lsan.h"
#include "lsan_allocator.h"
#include "lsan_thread.h"

#include <pthread.h>

namespace __lsan {
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

typedef void *dispatch_group_t;
typedef void *dispatch_queue_t;
typedef void *dispatch_source_t;
typedef u64 dispatch_time_t;
typedef void (*dispatch_function_t)(void *block);
typedef void *(*worker_t)(void *block);

// A wrapper for the ObjC blocks used to support libdispatch.
typedef struct {
  void *block;
  dispatch_function_t func;
  u32 parent_tid;
} lsan_block_context_t;

ALWAYS_INLINE
void lsan_register_worker_thread(int parent_tid) {
  if (GetCurrentThreadId() == kInvalidTid) {
    u32 tid = ThreadCreate(parent_tid, true);
    ThreadStart(tid, GetTid());
  }
}

// For use by only those functions that allocated the context via
// alloc_lsan_context().
extern "C" void lsan_dispatch_call_block_and_release(void *block) {
  lsan_block_context_t *context = (lsan_block_context_t *)block;
  VReport(2,
          "lsan_dispatch_call_block_and_release(): "
          "context: %p, pthread_self: %p\n",
          block, (void*)pthread_self());
  lsan_register_worker_thread(context->parent_tid);
  // Call the original dispatcher for the block.
  context->func(context->block);
  lsan_free(context);
}

}  // namespace __lsan

using namespace __lsan;

// Wrap |ctxt| and |func| into an lsan_block_context_t.
// The caller retains control of the allocated context.
extern "C" lsan_block_context_t *alloc_lsan_context(void *ctxt,
                                                    dispatch_function_t func) {
  GET_STACK_TRACE_THREAD;
  lsan_block_context_t *lsan_ctxt =
      (lsan_block_context_t *)lsan_malloc(sizeof(lsan_block_context_t), stack);
  lsan_ctxt->block = ctxt;
  lsan_ctxt->func = func;
  lsan_ctxt->parent_tid = GetCurrentThreadId();
  return lsan_ctxt;
}

// Define interceptor for dispatch_*_f function with the three most common
// parameters: dispatch_queue_t, context, dispatch_function_t.
#define INTERCEPT_DISPATCH_X_F_3(dispatch_x_f)                        \
  INTERCEPTOR(void, dispatch_x_f, dispatch_queue_t dq, void *ctxt,    \
              dispatch_function_t func) {                             \
    lsan_block_context_t *lsan_ctxt = alloc_lsan_context(ctxt, func); \
    return REAL(dispatch_x_f)(dq, (void *)lsan_ctxt,                  \
                              lsan_dispatch_call_block_and_release);  \
  }

INTERCEPT_DISPATCH_X_F_3(dispatch_async_f)
INTERCEPT_DISPATCH_X_F_3(dispatch_sync_f)
INTERCEPT_DISPATCH_X_F_3(dispatch_barrier_async_f)

INTERCEPTOR(void, dispatch_after_f, dispatch_time_t when, dispatch_queue_t dq,
            void *ctxt, dispatch_function_t func) {
  lsan_block_context_t *lsan_ctxt = alloc_lsan_context(ctxt, func);
  return REAL(dispatch_after_f)(when, dq, (void *)lsan_ctxt,
                                lsan_dispatch_call_block_and_release);
}

INTERCEPTOR(void, dispatch_group_async_f, dispatch_group_t group,
            dispatch_queue_t dq, void *ctxt, dispatch_function_t func) {
  lsan_block_context_t *lsan_ctxt = alloc_lsan_context(ctxt, func);
  REAL(dispatch_group_async_f)
  (group, dq, (void *)lsan_ctxt, lsan_dispatch_call_block_and_release);
}

#if !defined(MISSING_BLOCKS_SUPPORT)
extern "C" {
void dispatch_async(dispatch_queue_t dq, void (^work)(void));
void dispatch_group_async(dispatch_group_t dg, dispatch_queue_t dq,
                          void (^work)(void));
void dispatch_after(dispatch_time_t when, dispatch_queue_t queue,
                    void (^work)(void));
void dispatch_source_set_cancel_handler(dispatch_source_t ds,
                                        void (^work)(void));
void dispatch_source_set_event_handler(dispatch_source_t ds,
                                       void (^work)(void));
}

#    define GET_LSAN_BLOCK(work)                 \
      void (^lsan_block)(void);                  \
      int parent_tid = GetCurrentThreadId();     \
      lsan_block = ^(void) {                     \
        lsan_register_worker_thread(parent_tid); \
        work();                                  \
      }

INTERCEPTOR(void, dispatch_async, dispatch_queue_t dq, void (^work)(void)) {
  GET_LSAN_BLOCK(work);
  REAL(dispatch_async)(dq, lsan_block);
}

INTERCEPTOR(void, dispatch_group_async, dispatch_group_t dg,
            dispatch_queue_t dq, void (^work)(void)) {
  GET_LSAN_BLOCK(work);
  REAL(dispatch_group_async)(dg, dq, lsan_block);
}

INTERCEPTOR(void, dispatch_after, dispatch_time_t when, dispatch_queue_t queue,
            void (^work)(void)) {
  GET_LSAN_BLOCK(work);
  REAL(dispatch_after)(when, queue, lsan_block);
}

INTERCEPTOR(void, dispatch_source_set_cancel_handler, dispatch_source_t ds,
            void (^work)(void)) {
  if (!work) {
    REAL(dispatch_source_set_cancel_handler)(ds, work);
    return;
  }
  GET_LSAN_BLOCK(work);
  REAL(dispatch_source_set_cancel_handler)(ds, lsan_block);
}

INTERCEPTOR(void, dispatch_source_set_event_handler, dispatch_source_t ds,
            void (^work)(void)) {
  GET_LSAN_BLOCK(work);
  REAL(dispatch_source_set_event_handler)(ds, lsan_block);
}
#endif

#endif  // SANITIZER_APPLE
