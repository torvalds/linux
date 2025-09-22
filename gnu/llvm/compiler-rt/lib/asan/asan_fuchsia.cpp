//===-- asan_fuchsia.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Fuchsia-specific details.
//===---------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_fuchsia.h"
#if SANITIZER_FUCHSIA

#include <limits.h>
#include <zircon/sanitizer.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>

#  include "asan_interceptors.h"
#  include "asan_internal.h"
#  include "asan_stack.h"
#  include "asan_thread.h"
#  include "lsan/lsan_common.h"

namespace __asan {

// The system already set up the shadow memory for us.
// __sanitizer::GetMaxUserVirtualAddress has already been called by
// AsanInitInternal->InitializeHighMemEnd (asan_rtl.cpp).
// Just do some additional sanity checks here.
void InitializeShadowMemory() {
  if (Verbosity())
    PrintAddressSpaceLayout();

  // Make sure SHADOW_OFFSET doesn't use __asan_shadow_memory_dynamic_address.
  __asan_shadow_memory_dynamic_address = kDefaultShadowSentinel;
  DCHECK(kLowShadowBeg != kDefaultShadowSentinel);
  __asan_shadow_memory_dynamic_address = kLowShadowBeg;

  CHECK_EQ(kShadowGapEnd, kHighShadowBeg - 1);
  CHECK_EQ(kHighMemEnd, __sanitizer::ShadowBounds.memory_limit - 1);
  CHECK_EQ(kHighMemBeg, __sanitizer::ShadowBounds.shadow_limit);
  CHECK_EQ(kHighShadowBeg, __sanitizer::ShadowBounds.shadow_base);
  CHECK_EQ(kShadowGapEnd, __sanitizer::ShadowBounds.shadow_base - 1);
  CHECK_EQ(kLowShadowEnd, 0);
  CHECK_EQ(kLowShadowBeg, 0);
}

void AsanApplyToGlobals(globals_op_fptr op, const void *needle) {
  UNIMPLEMENTED();
}

void AsanCheckDynamicRTPrereqs() {}
void AsanCheckIncompatibleRT() {}
void InitializeAsanInterceptors() {}

void InitializePlatformExceptionHandlers() {}
void AsanOnDeadlySignal(int signo, void *siginfo, void *context) {
  UNIMPLEMENTED();
}

bool PlatformUnpoisonStacks() {
  // The current sp might not point to the default stack. This
  // could be because we are in a crash stack from fuzzing for example.
  // Unpoison the default stack and the current stack page.
  AsanThread *curr_thread = GetCurrentThread();
  CHECK(curr_thread != nullptr);
  uptr top = curr_thread->stack_top();
  uptr bottom = curr_thread->stack_bottom();
  // The default stack grows from top to bottom. (bottom < top).

  uptr local_stack = reinterpret_cast<uptr>(__builtin_frame_address(0));
  if (local_stack >= bottom && local_stack <= top) {
    // The current stack is the default stack.
    // We only need to unpoison from where we are using until the end.
    bottom = RoundDownTo(local_stack, GetPageSize());
    UnpoisonStack(bottom, top, "default");
  } else {
    // The current stack is not the default stack.
    // Unpoison the entire default stack and the current stack page.
    UnpoisonStack(bottom, top, "default");
    bottom = RoundDownTo(local_stack, GetPageSize());
    top = bottom + GetPageSize();
    UnpoisonStack(bottom, top, "unknown");
    return true;
  }

  return false;
}

// We can use a plain thread_local variable for TSD.
static thread_local void *per_thread;

void *AsanTSDGet() { return per_thread; }

void AsanTSDSet(void *tsd) { per_thread = tsd; }

// There's no initialization needed, and the passed-in destructor
// will never be called.  Instead, our own thread destruction hook
// (below) will call AsanThread::TSDDtor directly.
void AsanTSDInit(void (*destructor)(void *tsd)) {
  DCHECK(destructor == &PlatformTSDDtor);
}

void PlatformTSDDtor(void *tsd) { UNREACHABLE(__func__); }

static inline size_t AsanThreadMmapSize() {
  return RoundUpTo(sizeof(AsanThread), _zx_system_get_page_size());
}

struct AsanThread::InitOptions {
  uptr stack_bottom, stack_size;
};

// Shared setup between thread creation and startup for the initial thread.
static AsanThread *CreateAsanThread(StackTrace *stack, u32 parent_tid,
                                    bool detached, const char *name) {
  // In lieu of AsanThread::Create.
  AsanThread *thread = (AsanThread *)MmapOrDie(AsanThreadMmapSize(), __func__);

  AsanThreadContext::CreateThreadContextArgs args = {thread, stack};
  u32 tid = asanThreadRegistry().CreateThread(0, detached, parent_tid, &args);
  asanThreadRegistry().SetThreadName(tid, name);

  return thread;
}

// This gets the same arguments passed to Init by CreateAsanThread, above.
// We're in the creator thread before the new thread is actually started,
// but its stack address range is already known.  We don't bother tracking
// the static TLS address range because the system itself already uses an
// ASan-aware allocator for that.
void AsanThread::SetThreadStackAndTls(const AsanThread::InitOptions *options) {
  DCHECK_NE(GetCurrentThread(), this);
  DCHECK_NE(GetCurrentThread(), nullptr);
  CHECK_NE(options->stack_bottom, 0);
  CHECK_NE(options->stack_size, 0);
  stack_bottom_ = options->stack_bottom;
  stack_top_ = options->stack_bottom + options->stack_size;
}

// Called by __asan::AsanInitInternal (asan_rtl.c).
AsanThread *CreateMainThread() {
  thrd_t self = thrd_current();
  char name[ZX_MAX_NAME_LEN];
  CHECK_NE(__sanitizer::MainThreadStackBase, 0);
  CHECK_GT(__sanitizer::MainThreadStackSize, 0);
  AsanThread *t = CreateAsanThread(
      nullptr, 0, true,
      _zx_object_get_property(thrd_get_zx_handle(self), ZX_PROP_NAME, name,
                              sizeof(name)) == ZX_OK
          ? name
          : nullptr);
  // We need to set the current thread before calling AsanThread::Init() below,
  // since it reads the thread ID.
  SetCurrentThread(t);
  DCHECK_EQ(t->tid(), 0);

  const AsanThread::InitOptions options = {__sanitizer::MainThreadStackBase,
                                           __sanitizer::MainThreadStackSize};
  t->Init(&options);

  return t;
}

// This is called before each thread creation is attempted.  So, in
// its first call, the calling thread is the initial and sole thread.
static void *BeforeThreadCreateHook(uptr user_id, bool detached,
                                    const char *name, uptr stack_bottom,
                                    uptr stack_size) {
  EnsureMainThreadIDIsCorrect();
  // Strict init-order checking is thread-hostile.
  if (flags()->strict_init_order)
    StopInitOrderChecking();

  GET_STACK_TRACE_THREAD;
  u32 parent_tid = GetCurrentTidOrInvalid();

  AsanThread *thread = CreateAsanThread(&stack, parent_tid, detached, name);

  // On other systems, AsanThread::Init() is called from the new
  // thread itself.  But on Fuchsia we already know the stack address
  // range beforehand, so we can do most of the setup right now.
  const AsanThread::InitOptions options = {stack_bottom, stack_size};
  thread->Init(&options);
  return thread;
}

// This is called after creating a new thread (in the creating thread),
// with the pointer returned by BeforeThreadCreateHook (above).
static void ThreadCreateHook(void *hook, bool aborted) {
  AsanThread *thread = static_cast<AsanThread *>(hook);
  if (!aborted) {
    // The thread was created successfully.
    // ThreadStartHook is already running in the new thread.
  } else {
    // The thread wasn't created after all.
    // Clean up everything we set up in BeforeThreadCreateHook.
    asanThreadRegistry().FinishThread(thread->tid());
    UnmapOrDie(thread, AsanThreadMmapSize());
  }
}

// This is called in the newly-created thread before it runs anything else,
// with the pointer returned by BeforeThreadCreateHook (above).
// cf. asan_interceptors.cpp:asan_thread_start
static void ThreadStartHook(void *hook, uptr os_id) {
  AsanThread *thread = static_cast<AsanThread *>(hook);
  SetCurrentThread(thread);

  // In lieu of AsanThread::ThreadStart.
  asanThreadRegistry().StartThread(thread->tid(), os_id, ThreadType::Regular,
                                   nullptr);
}

// Each thread runs this just before it exits,
// with the pointer returned by BeforeThreadCreateHook (above).
// All per-thread destructors have already been called.
static void ThreadExitHook(void *hook, uptr os_id) {
  AsanThread::TSDDtor(per_thread);
}

bool HandleDlopenInit() {
  // Not supported on this platform.
  static_assert(!SANITIZER_SUPPORTS_INIT_FOR_DLOPEN,
                "Expected SANITIZER_SUPPORTS_INIT_FOR_DLOPEN to be false");
  return false;
}

void FlushUnneededASanShadowMemory(uptr p, uptr size) {
  __sanitizer_fill_shadow(p, size, 0, 0);
}

// On Fuchsia, leak detection is done by a special hook after atexit hooks.
// So this doesn't install any atexit hook like on other platforms.
void InstallAtExitCheckLeaks() {}

void InstallAtForkHandler() {}

}  // namespace __asan

namespace __lsan {

bool UseExitcodeOnLeak() { return __asan::flags()->halt_on_error; }

}  // namespace __lsan

// These are declared (in extern "C") by <zircon/sanitizer.h>.
// The system runtime will call our definitions directly.

void *__sanitizer_before_thread_create_hook(thrd_t thread, bool detached,
                                            const char *name, void *stack_base,
                                            size_t stack_size) {
  return __asan::BeforeThreadCreateHook(
      reinterpret_cast<uptr>(thread), detached, name,
      reinterpret_cast<uptr>(stack_base), stack_size);
}

void __sanitizer_thread_create_hook(void *hook, thrd_t thread, int error) {
  __asan::ThreadCreateHook(hook, error != thrd_success);
}

void __sanitizer_thread_start_hook(void *hook, thrd_t self) {
  __asan::ThreadStartHook(hook, reinterpret_cast<uptr>(self));
}

void __sanitizer_thread_exit_hook(void *hook, thrd_t self) {
  __asan::ThreadExitHook(hook, reinterpret_cast<uptr>(self));
}

#endif  // SANITIZER_FUCHSIA
