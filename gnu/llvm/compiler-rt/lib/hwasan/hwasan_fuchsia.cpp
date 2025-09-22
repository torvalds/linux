//===-- hwasan_fuchsia.cpp --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file is a part of HWAddressSanitizer and contains Fuchsia-specific
/// code.
///
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_fuchsia.h"
#if SANITIZER_FUCHSIA

#include <zircon/features.h>
#include <zircon/syscalls.h>

#include "hwasan.h"
#include "hwasan_interface_internal.h"
#include "hwasan_report.h"
#include "hwasan_thread.h"
#include "hwasan_thread_list.h"

// This TLS variable contains the location of the stack ring buffer and can be
// used to always find the hwasan thread object associated with the current
// running thread.
[[gnu::tls_model("initial-exec")]]
SANITIZER_INTERFACE_ATTRIBUTE
THREADLOCAL uptr __hwasan_tls;

namespace __hwasan {

bool InitShadow() {
  __sanitizer::InitShadowBounds();
  CHECK_NE(__sanitizer::ShadowBounds.shadow_limit, 0);

  // These variables are used by MemIsShadow for asserting we have a correct
  // shadow address. On Fuchsia, we only have one region of shadow, so the
  // bounds of Low shadow can be zero while High shadow represents the true
  // bounds. Note that these are inclusive ranges.
  kLowShadowStart = 0;
  kLowShadowEnd = 0;
  kHighShadowStart = __sanitizer::ShadowBounds.shadow_base;
  kHighShadowEnd = __sanitizer::ShadowBounds.shadow_limit - 1;

  return true;
}

bool MemIsApp(uptr p) {
  CHECK(GetTagFromPointer(p) == 0);
  return __sanitizer::ShadowBounds.shadow_limit <= p &&
         p <= (__sanitizer::ShadowBounds.memory_limit - 1);
}

// These are known parameters passed to the hwasan runtime on thread creation.
struct Thread::InitState {
  uptr stack_bottom, stack_top;
};

static void FinishThreadInitialization(Thread *thread);

void InitThreads() {
  // This is the minimal alignment needed for the storage where hwasan threads
  // and their stack ring buffers are placed. This alignment is necessary so the
  // stack ring buffer can perform a simple calculation to get the next element
  // in the RB. The instructions for this calculation are emitted by the
  // compiler. (Full explanation in hwasan_thread_list.h.)
  uptr alloc_size = UINT64_C(1) << kShadowBaseAlignment;
  uptr thread_start = reinterpret_cast<uptr>(
      MmapAlignedOrDieOnFatalError(alloc_size, alloc_size, __func__));

  InitThreadList(thread_start, alloc_size);

  // Create the hwasan thread object for the current (main) thread. Stack info
  // for this thread is known from information passed via
  // __sanitizer_startup_hook.
  const Thread::InitState state = {
      .stack_bottom = __sanitizer::MainThreadStackBase,
      .stack_top =
          __sanitizer::MainThreadStackBase + __sanitizer::MainThreadStackSize,
  };
  FinishThreadInitialization(hwasanThreadList().CreateCurrentThread(&state));
}

uptr *GetCurrentThreadLongPtr() { return &__hwasan_tls; }

// This is called from the parent thread before the new thread is created. Here
// we can propagate known info like the stack bounds to Thread::Init before
// jumping into the thread. We cannot initialize the stack ring buffer yet since
// we have not entered the new thread.
static void *BeforeThreadCreateHook(uptr user_id, bool detached,
                                    const char *name, uptr stack_bottom,
                                    uptr stack_size) {
  const Thread::InitState state = {
      .stack_bottom = stack_bottom,
      .stack_top = stack_bottom + stack_size,
  };
  return hwasanThreadList().CreateCurrentThread(&state);
}

// This sets the stack top and bottom according to the InitState passed to
// CreateCurrentThread above.
void Thread::InitStackAndTls(const InitState *state) {
  CHECK_NE(state->stack_bottom, 0);
  CHECK_NE(state->stack_top, 0);
  stack_bottom_ = state->stack_bottom;
  stack_top_ = state->stack_top;
  tls_end_ = tls_begin_ = 0;
}

// This is called after creating a new thread with the pointer returned by
// BeforeThreadCreateHook. We are still in the creating thread and should check
// if it was actually created correctly.
static void ThreadCreateHook(void *hook, bool aborted) {
  Thread *thread = static_cast<Thread *>(hook);
  if (!aborted) {
    // The thread was created successfully.
    // ThreadStartHook can already be running in the new thread.
  } else {
    // The thread wasn't created after all.
    // Clean up everything we set up in BeforeThreadCreateHook.
    atomic_signal_fence(memory_order_seq_cst);
    hwasanThreadList().ReleaseThread(thread);
  }
}

// This is called in the newly-created thread before it runs anything else,
// with the pointer returned by BeforeThreadCreateHook (above). Here we can
// setup the stack ring buffer.
static void ThreadStartHook(void *hook, thrd_t self) {
  Thread *thread = static_cast<Thread *>(hook);
  FinishThreadInitialization(thread);
  thread->EnsureRandomStateInited();
}

// This is the function that sets up the stack ring buffer and enables us to use
// GetCurrentThread. This function should only be called while IN the thread
// that we want to create the hwasan thread object for so __hwasan_tls can be
// properly referenced.
static void FinishThreadInitialization(Thread *thread) {
  CHECK_NE(thread, nullptr);

  // The ring buffer is located immediately before the thread object.
  uptr stack_buffer_size = hwasanThreadList().GetRingBufferSize();
  uptr stack_buffer_start = reinterpret_cast<uptr>(thread) - stack_buffer_size;
  thread->InitStackRingBuffer(stack_buffer_start, stack_buffer_size);
}

static void ThreadExitHook(void *hook, thrd_t self) {
  Thread *thread = static_cast<Thread *>(hook);
  atomic_signal_fence(memory_order_seq_cst);
  hwasanThreadList().ReleaseThread(thread);
}

uptr TagMemoryAligned(uptr p, uptr size, tag_t tag) {
  CHECK(IsAligned(p, kShadowAlignment));
  CHECK(IsAligned(size, kShadowAlignment));
  __sanitizer_fill_shadow(p, size, tag,
                          common_flags()->clear_shadow_mmap_threshold);
  return AddTagToPointer(p, tag);
}

// Not implemented because Fuchsia does not use signal handlers.
void HwasanOnDeadlySignal(int signo, void *info, void *context) {}

// Not implemented because Fuchsia does not use interceptors.
void InitializeInterceptors() {}

// Not implemented because this is only relevant for Android.
void AndroidTestTlsSlot() {}

// TSD was normally used on linux as a means of calling the hwasan thread exit
// handler passed to pthread_key_create. This is not needed on Fuchsia because
// we will be using __sanitizer_thread_exit_hook.
void HwasanTSDInit() {}
void HwasanTSDThreadInit() {}

// On linux, this just would call `atexit(HwasanAtExit)`. The functions in
// HwasanAtExit are unimplemented for Fuchsia and effectively no-ops, so this
// function is unneeded.
void InstallAtExitHandler() {}

void HwasanInstallAtForkHandler() {}

void InstallAtExitCheckLeaks() {}

void InitializeOsSupport() {
#ifdef __aarch64__
  uint32_t features = 0;
  CHECK_EQ(zx_system_get_features(ZX_FEATURE_KIND_ADDRESS_TAGGING, &features),
           ZX_OK);
  if (!(features & ZX_ARM64_FEATURE_ADDRESS_TAGGING_TBI) &&
      flags()->fail_without_syscall_abi) {
    Printf(
        "FATAL: HWAddressSanitizer requires "
        "ZX_ARM64_FEATURE_ADDRESS_TAGGING_TBI.\n");
    Die();
  }
#endif
}

}  // namespace __hwasan

namespace __lsan {

bool UseExitcodeOnLeak() { return __hwasan::flags()->halt_on_error; }

}  // namespace __lsan

extern "C" {

void *__sanitizer_before_thread_create_hook(thrd_t thread, bool detached,
                                            const char *name, void *stack_base,
                                            size_t stack_size) {
  return __hwasan::BeforeThreadCreateHook(
      reinterpret_cast<uptr>(thread), detached, name,
      reinterpret_cast<uptr>(stack_base), stack_size);
}

void __sanitizer_thread_create_hook(void *hook, thrd_t thread, int error) {
  __hwasan::ThreadCreateHook(hook, error != thrd_success);
}

void __sanitizer_thread_start_hook(void *hook, thrd_t self) {
  __hwasan::ThreadStartHook(hook, reinterpret_cast<uptr>(self));
}

void __sanitizer_thread_exit_hook(void *hook, thrd_t self) {
  __hwasan::ThreadExitHook(hook, self);
}

void __sanitizer_module_loaded(const struct dl_phdr_info *info, size_t) {
  __hwasan_library_loaded(info->dlpi_addr, info->dlpi_phdr, info->dlpi_phnum);
}

}  // extern "C"

#endif  // SANITIZER_FUCHSIA
