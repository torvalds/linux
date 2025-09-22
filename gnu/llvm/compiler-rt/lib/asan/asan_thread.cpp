//===-- asan_thread.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Thread-related code.
//===----------------------------------------------------------------------===//
#include "asan_thread.h"

#include "asan_allocator.h"
#include "asan_interceptors.h"
#include "asan_mapping.h"
#include "asan_poisoning.h"
#include "asan_stack.h"
#include "lsan/lsan_common.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_placement_new.h"
#include "sanitizer_common/sanitizer_stackdepot.h"
#include "sanitizer_common/sanitizer_tls_get_addr.h"

namespace __asan {

// AsanThreadContext implementation.

void AsanThreadContext::OnCreated(void *arg) {
  CreateThreadContextArgs *args = static_cast<CreateThreadContextArgs *>(arg);
  if (args->stack)
    stack_id = StackDepotPut(*args->stack);
  thread = args->thread;
  thread->set_context(this);
}

void AsanThreadContext::OnFinished() {
  // Drop the link to the AsanThread object.
  thread = nullptr;
}

static ThreadRegistry *asan_thread_registry;
static ThreadArgRetval *thread_data;

static Mutex mu_for_thread_context;
// TODO(leonardchan@): It should be possible to make LowLevelAllocator
// threadsafe and consolidate this one into the GlobalLoweLevelAllocator.
// We should be able to do something similar to what's in
// sanitizer_stack_store.cpp.
static LowLevelAllocator allocator_for_thread_context;

static ThreadContextBase *GetAsanThreadContext(u32 tid) {
  Lock lock(&mu_for_thread_context);
  return new (allocator_for_thread_context) AsanThreadContext(tid);
}

static void InitThreads() {
  static bool initialized;
  // Don't worry about thread_safety - this should be called when there is
  // a single thread.
  if (LIKELY(initialized))
    return;
  // Never reuse ASan threads: we store pointer to AsanThreadContext
  // in TSD and can't reliably tell when no more TSD destructors will
  // be called. It would be wrong to reuse AsanThreadContext for another
  // thread before all TSD destructors will be called for it.

  // MIPS requires aligned address
  alignas(alignof(ThreadRegistry)) static char
      thread_registry_placeholder[sizeof(ThreadRegistry)];
  alignas(alignof(ThreadArgRetval)) static char
      thread_data_placeholder[sizeof(ThreadArgRetval)];

  asan_thread_registry =
      new (thread_registry_placeholder) ThreadRegistry(GetAsanThreadContext);
  thread_data = new (thread_data_placeholder) ThreadArgRetval();
  initialized = true;
}

ThreadRegistry &asanThreadRegistry() {
  InitThreads();
  return *asan_thread_registry;
}

ThreadArgRetval &asanThreadArgRetval() {
  InitThreads();
  return *thread_data;
}

AsanThreadContext *GetThreadContextByTidLocked(u32 tid) {
  return static_cast<AsanThreadContext *>(
      asanThreadRegistry().GetThreadLocked(tid));
}

// AsanThread implementation.

AsanThread *AsanThread::Create(const void *start_data, uptr data_size,
                               u32 parent_tid, StackTrace *stack,
                               bool detached) {
  uptr PageSize = GetPageSizeCached();
  uptr size = RoundUpTo(sizeof(AsanThread), PageSize);
  AsanThread *thread = (AsanThread *)MmapOrDie(size, __func__);
  if (data_size) {
    uptr availible_size = (uptr)thread + size - (uptr)(thread->start_data_);
    CHECK_LE(data_size, availible_size);
    internal_memcpy(thread->start_data_, start_data, data_size);
  }
  AsanThreadContext::CreateThreadContextArgs args = {thread, stack};
  asanThreadRegistry().CreateThread(0, detached, parent_tid, &args);

  return thread;
}

void AsanThread::GetStartData(void *out, uptr out_size) const {
  internal_memcpy(out, start_data_, out_size);
}

void AsanThread::TSDDtor(void *tsd) {
  AsanThreadContext *context = (AsanThreadContext *)tsd;
  VReport(1, "T%d TSDDtor\n", context->tid);
  if (context->thread)
    context->thread->Destroy();
}

void AsanThread::Destroy() {
  int tid = this->tid();
  VReport(1, "T%d exited\n", tid);

  bool was_running =
      (asanThreadRegistry().FinishThread(tid) == ThreadStatusRunning);
  if (was_running) {
    if (AsanThread *thread = GetCurrentThread())
      CHECK_EQ(this, thread);
    malloc_storage().CommitBack();
    if (common_flags()->use_sigaltstack)
      UnsetAlternateSignalStack();
    FlushToDeadThreadStats(&stats_);
    // We also clear the shadow on thread destruction because
    // some code may still be executing in later TSD destructors
    // and we don't want it to have any poisoned stack.
    ClearShadowForThreadStackAndTLS();
    DeleteFakeStack(tid);
  } else {
    CHECK_NE(this, GetCurrentThread());
  }
  uptr size = RoundUpTo(sizeof(AsanThread), GetPageSizeCached());
  UnmapOrDie(this, size);
  if (was_running)
    DTLS_Destroy();
}

void AsanThread::StartSwitchFiber(FakeStack **fake_stack_save, uptr bottom,
                                  uptr size) {
  if (atomic_load(&stack_switching_, memory_order_relaxed)) {
    Report("ERROR: starting fiber switch while in fiber switch\n");
    Die();
  }

  next_stack_bottom_ = bottom;
  next_stack_top_ = bottom + size;
  atomic_store(&stack_switching_, 1, memory_order_release);

  FakeStack *current_fake_stack = fake_stack_;
  if (fake_stack_save)
    *fake_stack_save = fake_stack_;
  fake_stack_ = nullptr;
  SetTLSFakeStack(nullptr);
  // if fake_stack_save is null, the fiber will die, delete the fakestack
  if (!fake_stack_save && current_fake_stack)
    current_fake_stack->Destroy(this->tid());
}

void AsanThread::FinishSwitchFiber(FakeStack *fake_stack_save, uptr *bottom_old,
                                   uptr *size_old) {
  if (!atomic_load(&stack_switching_, memory_order_relaxed)) {
    Report("ERROR: finishing a fiber switch that has not started\n");
    Die();
  }

  if (fake_stack_save) {
    SetTLSFakeStack(fake_stack_save);
    fake_stack_ = fake_stack_save;
  }

  if (bottom_old)
    *bottom_old = stack_bottom_;
  if (size_old)
    *size_old = stack_top_ - stack_bottom_;
  stack_bottom_ = next_stack_bottom_;
  stack_top_ = next_stack_top_;
  atomic_store(&stack_switching_, 0, memory_order_release);
  next_stack_top_ = 0;
  next_stack_bottom_ = 0;
}

inline AsanThread::StackBounds AsanThread::GetStackBounds() const {
  if (!atomic_load(&stack_switching_, memory_order_acquire)) {
    // Make sure the stack bounds are fully initialized.
    if (stack_bottom_ >= stack_top_)
      return {0, 0};
    return {stack_bottom_, stack_top_};
  }
  char local;
  const uptr cur_stack = (uptr)&local;
  // Note: need to check next stack first, because FinishSwitchFiber
  // may be in process of overwriting stack_top_/bottom_. But in such case
  // we are already on the next stack.
  if (cur_stack >= next_stack_bottom_ && cur_stack < next_stack_top_)
    return {next_stack_bottom_, next_stack_top_};
  return {stack_bottom_, stack_top_};
}

uptr AsanThread::stack_top() { return GetStackBounds().top; }

uptr AsanThread::stack_bottom() { return GetStackBounds().bottom; }

uptr AsanThread::stack_size() {
  const auto bounds = GetStackBounds();
  return bounds.top - bounds.bottom;
}

// We want to create the FakeStack lazily on the first use, but not earlier
// than the stack size is known and the procedure has to be async-signal safe.
FakeStack *AsanThread::AsyncSignalSafeLazyInitFakeStack() {
  uptr stack_size = this->stack_size();
  if (stack_size == 0)  // stack_size is not yet available, don't use FakeStack.
    return nullptr;
  uptr old_val = 0;
  // fake_stack_ has 3 states:
  // 0   -- not initialized
  // 1   -- being initialized
  // ptr -- initialized
  // This CAS checks if the state was 0 and if so changes it to state 1,
  // if that was successful, it initializes the pointer.
  if (atomic_compare_exchange_strong(
          reinterpret_cast<atomic_uintptr_t *>(&fake_stack_), &old_val, 1UL,
          memory_order_relaxed)) {
    uptr stack_size_log = Log2(RoundUpToPowerOfTwo(stack_size));
    CHECK_LE(flags()->min_uar_stack_size_log, flags()->max_uar_stack_size_log);
    stack_size_log =
        Min(stack_size_log, static_cast<uptr>(flags()->max_uar_stack_size_log));
    stack_size_log =
        Max(stack_size_log, static_cast<uptr>(flags()->min_uar_stack_size_log));
    fake_stack_ = FakeStack::Create(stack_size_log);
    DCHECK_EQ(GetCurrentThread(), this);
    SetTLSFakeStack(fake_stack_);
    return fake_stack_;
  }
  return nullptr;
}

void AsanThread::Init(const InitOptions *options) {
  DCHECK_NE(tid(), kInvalidTid);
  next_stack_top_ = next_stack_bottom_ = 0;
  atomic_store(&stack_switching_, false, memory_order_release);
  CHECK_EQ(this->stack_size(), 0U);
  SetThreadStackAndTls(options);
  if (stack_top_ != stack_bottom_) {
    CHECK_GT(this->stack_size(), 0U);
    CHECK(AddrIsInMem(stack_bottom_));
    CHECK(AddrIsInMem(stack_top_ - 1));
  }
  ClearShadowForThreadStackAndTLS();
  fake_stack_ = nullptr;
  if (__asan_option_detect_stack_use_after_return &&
      tid() == GetCurrentTidOrInvalid()) {
    // AsyncSignalSafeLazyInitFakeStack makes use of threadlocals and must be
    // called from the context of the thread it is initializing, not its parent.
    // Most platforms call AsanThread::Init on the newly-spawned thread, but
    // Fuchsia calls this function from the parent thread.  To support that
    // approach, we avoid calling AsyncSignalSafeLazyInitFakeStack here; it will
    // be called by the new thread when it first attempts to access the fake
    // stack.
    AsyncSignalSafeLazyInitFakeStack();
  }
  int local = 0;
  VReport(1, "T%d: stack [%p,%p) size 0x%zx; local=%p\n", tid(),
          (void *)stack_bottom_, (void *)stack_top_, stack_top_ - stack_bottom_,
          (void *)&local);
}

// Fuchsia doesn't use ThreadStart.
// asan_fuchsia.c definies CreateMainThread and SetThreadStackAndTls.
#if !SANITIZER_FUCHSIA

void AsanThread::ThreadStart(tid_t os_id) {
  Init();
  asanThreadRegistry().StartThread(tid(), os_id, ThreadType::Regular, nullptr);

  if (common_flags()->use_sigaltstack)
    SetAlternateSignalStack();
}

AsanThread *CreateMainThread() {
  AsanThread *main_thread = AsanThread::Create(
      /* parent_tid */ kMainTid,
      /* stack */ nullptr, /* detached */ true);
  SetCurrentThread(main_thread);
  main_thread->ThreadStart(internal_getpid());
  return main_thread;
}

// This implementation doesn't use the argument, which is just passed down
// from the caller of Init (which see, above).  It's only there to support
// OS-specific implementations that need more information passed through.
void AsanThread::SetThreadStackAndTls(const InitOptions *options) {
  DCHECK_EQ(options, nullptr);
  uptr tls_size = 0;
  uptr stack_size = 0;
  GetThreadStackAndTls(tid() == kMainTid, &stack_bottom_, &stack_size,
                       &tls_begin_, &tls_size);
  stack_top_ = RoundDownTo(stack_bottom_ + stack_size, ASAN_SHADOW_GRANULARITY);
  stack_bottom_ = RoundDownTo(stack_bottom_, ASAN_SHADOW_GRANULARITY);
  tls_end_ = tls_begin_ + tls_size;
  dtls_ = DTLS_Get();

  if (stack_top_ != stack_bottom_) {
    int local;
    CHECK(AddrIsInStack((uptr)&local));
  }
}

#endif  // !SANITIZER_FUCHSIA

void AsanThread::ClearShadowForThreadStackAndTLS() {
  if (stack_top_ != stack_bottom_)
    PoisonShadow(stack_bottom_, stack_top_ - stack_bottom_, 0);
  if (tls_begin_ != tls_end_) {
    uptr tls_begin_aligned = RoundDownTo(tls_begin_, ASAN_SHADOW_GRANULARITY);
    uptr tls_end_aligned = RoundUpTo(tls_end_, ASAN_SHADOW_GRANULARITY);
    FastPoisonShadow(tls_begin_aligned, tls_end_aligned - tls_begin_aligned, 0);
  }
}

bool AsanThread::GetStackFrameAccessByAddr(uptr addr,
                                           StackFrameAccess *access) {
  if (stack_top_ == stack_bottom_)
    return false;

  uptr bottom = 0;
  if (AddrIsInStack(addr)) {
    bottom = stack_bottom();
  } else if (FakeStack *fake_stack = get_fake_stack()) {
    bottom = fake_stack->AddrIsInFakeStack(addr);
    CHECK(bottom);
    access->offset = addr - bottom;
    access->frame_pc = ((uptr *)bottom)[2];
    access->frame_descr = (const char *)((uptr *)bottom)[1];
    return true;
  }
  uptr aligned_addr = RoundDownTo(addr, SANITIZER_WORDSIZE / 8);  // align addr.
  uptr mem_ptr = RoundDownTo(aligned_addr, ASAN_SHADOW_GRANULARITY);
  u8 *shadow_ptr = (u8 *)MemToShadow(aligned_addr);
  u8 *shadow_bottom = (u8 *)MemToShadow(bottom);

  while (shadow_ptr >= shadow_bottom &&
         *shadow_ptr != kAsanStackLeftRedzoneMagic) {
    shadow_ptr--;
    mem_ptr -= ASAN_SHADOW_GRANULARITY;
  }

  while (shadow_ptr >= shadow_bottom &&
         *shadow_ptr == kAsanStackLeftRedzoneMagic) {
    shadow_ptr--;
    mem_ptr -= ASAN_SHADOW_GRANULARITY;
  }

  if (shadow_ptr < shadow_bottom) {
    return false;
  }

  uptr *ptr = (uptr *)(mem_ptr + ASAN_SHADOW_GRANULARITY);
  CHECK(ptr[0] == kCurrentStackFrameMagic);
  access->offset = addr - (uptr)ptr;
  access->frame_pc = ptr[2];
  access->frame_descr = (const char *)ptr[1];
  return true;
}

uptr AsanThread::GetStackVariableShadowStart(uptr addr) {
  uptr bottom = 0;
  if (AddrIsInStack(addr)) {
    bottom = stack_bottom();
  } else if (FakeStack *fake_stack = get_fake_stack()) {
    bottom = fake_stack->AddrIsInFakeStack(addr);
    if (bottom == 0) {
      return 0;
    }
  } else {
    return 0;
  }

  uptr aligned_addr = RoundDownTo(addr, SANITIZER_WORDSIZE / 8);  // align addr.
  u8 *shadow_ptr = (u8 *)MemToShadow(aligned_addr);
  u8 *shadow_bottom = (u8 *)MemToShadow(bottom);

  while (shadow_ptr >= shadow_bottom &&
         (*shadow_ptr != kAsanStackLeftRedzoneMagic &&
          *shadow_ptr != kAsanStackMidRedzoneMagic &&
          *shadow_ptr != kAsanStackRightRedzoneMagic))
    shadow_ptr--;

  return (uptr)shadow_ptr + 1;
}

bool AsanThread::AddrIsInStack(uptr addr) {
  const auto bounds = GetStackBounds();
  return addr >= bounds.bottom && addr < bounds.top;
}

static bool ThreadStackContainsAddress(ThreadContextBase *tctx_base,
                                       void *addr) {
  AsanThreadContext *tctx = static_cast<AsanThreadContext *>(tctx_base);
  AsanThread *t = tctx->thread;
  if (!t)
    return false;
  if (t->AddrIsInStack((uptr)addr))
    return true;
  FakeStack *fake_stack = t->get_fake_stack();
  if (!fake_stack)
    return false;
  return fake_stack->AddrIsInFakeStack((uptr)addr);
}

AsanThread *GetCurrentThread() {
  AsanThreadContext *context =
      reinterpret_cast<AsanThreadContext *>(AsanTSDGet());
  if (!context) {
    if (SANITIZER_ANDROID) {
      // On Android, libc constructor is called _after_ asan_init, and cleans up
      // TSD. Try to figure out if this is still the main thread by the stack
      // address. We are not entirely sure that we have correct main thread
      // limits, so only do this magic on Android, and only if the found thread
      // is the main thread.
      AsanThreadContext *tctx = GetThreadContextByTidLocked(kMainTid);
      if (tctx && ThreadStackContainsAddress(tctx, &context)) {
        SetCurrentThread(tctx->thread);
        return tctx->thread;
      }
    }
    return nullptr;
  }
  return context->thread;
}

void SetCurrentThread(AsanThread *t) {
  CHECK(t->context());
  VReport(2, "SetCurrentThread: %p for thread %p\n", (void *)t->context(),
          (void *)GetThreadSelf());
  // Make sure we do not reset the current AsanThread.
  CHECK_EQ(0, AsanTSDGet());
  AsanTSDSet(t->context());
  CHECK_EQ(t->context(), AsanTSDGet());
}

u32 GetCurrentTidOrInvalid() {
  AsanThread *t = GetCurrentThread();
  return t ? t->tid() : kInvalidTid;
}

AsanThread *FindThreadByStackAddress(uptr addr) {
  asanThreadRegistry().CheckLocked();
  AsanThreadContext *tctx = static_cast<AsanThreadContext *>(
      asanThreadRegistry().FindThreadContextLocked(ThreadStackContainsAddress,
                                                   (void *)addr));
  return tctx ? tctx->thread : nullptr;
}

void EnsureMainThreadIDIsCorrect() {
  AsanThreadContext *context =
      reinterpret_cast<AsanThreadContext *>(AsanTSDGet());
  if (context && (context->tid == kMainTid))
    context->os_id = GetTid();
}

__asan::AsanThread *GetAsanThreadByOsIDLocked(tid_t os_id) {
  __asan::AsanThreadContext *context = static_cast<__asan::AsanThreadContext *>(
      __asan::asanThreadRegistry().FindThreadContextByOsIDLocked(os_id));
  if (!context)
    return nullptr;
  return context->thread;
}
}  // namespace __asan

// --- Implementation of LSan-specific functions --- {{{1
namespace __lsan {
void LockThreads() {
  __asan::asanThreadRegistry().Lock();
  __asan::asanThreadArgRetval().Lock();
}

void UnlockThreads() {
  __asan::asanThreadArgRetval().Unlock();
  __asan::asanThreadRegistry().Unlock();
}

static ThreadRegistry *GetAsanThreadRegistryLocked() {
  __asan::asanThreadRegistry().CheckLocked();
  return &__asan::asanThreadRegistry();
}

void EnsureMainThreadIDIsCorrect() { __asan::EnsureMainThreadIDIsCorrect(); }

bool GetThreadRangesLocked(tid_t os_id, uptr *stack_begin, uptr *stack_end,
                           uptr *tls_begin, uptr *tls_end, uptr *cache_begin,
                           uptr *cache_end, DTLS **dtls) {
  __asan::AsanThread *t = __asan::GetAsanThreadByOsIDLocked(os_id);
  if (!t)
    return false;
  *stack_begin = t->stack_bottom();
  *stack_end = t->stack_top();
  *tls_begin = t->tls_begin();
  *tls_end = t->tls_end();
  // ASan doesn't keep allocator caches in TLS, so these are unused.
  *cache_begin = 0;
  *cache_end = 0;
  *dtls = t->dtls();
  return true;
}

void GetAllThreadAllocatorCachesLocked(InternalMmapVector<uptr> *caches) {}

void GetThreadExtraStackRangesLocked(tid_t os_id,
                                     InternalMmapVector<Range> *ranges) {
  __asan::AsanThread *t = __asan::GetAsanThreadByOsIDLocked(os_id);
  if (!t)
    return;
  __asan::FakeStack *fake_stack = t->get_fake_stack();
  if (!fake_stack)
    return;

  fake_stack->ForEachFakeFrame(
      [](uptr begin, uptr end, void *arg) {
        reinterpret_cast<InternalMmapVector<Range> *>(arg)->push_back(
            {begin, end});
      },
      ranges);
}

void GetThreadExtraStackRangesLocked(InternalMmapVector<Range> *ranges) {
  GetAsanThreadRegistryLocked()->RunCallbackForEachThreadLocked(
      [](ThreadContextBase *tctx, void *arg) {
        GetThreadExtraStackRangesLocked(
            tctx->os_id, reinterpret_cast<InternalMmapVector<Range> *>(arg));
      },
      ranges);
}

void GetAdditionalThreadContextPtrsLocked(InternalMmapVector<uptr> *ptrs) {
  __asan::asanThreadArgRetval().GetAllPtrsLocked(ptrs);
}

void GetRunningThreadsLocked(InternalMmapVector<tid_t> *threads) {
  GetAsanThreadRegistryLocked()->RunCallbackForEachThreadLocked(
      [](ThreadContextBase *tctx, void *threads) {
        if (tctx->status == ThreadStatusRunning)
          reinterpret_cast<InternalMmapVector<tid_t> *>(threads)->push_back(
              tctx->os_id);
      },
      threads);
}

}  // namespace __lsan

// ---------------------- Interface ---------------- {{{1
using namespace __asan;

extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_start_switch_fiber(void **fakestacksave, const void *bottom,
                                    uptr size) {
  AsanThread *t = GetCurrentThread();
  if (!t) {
    VReport(1, "__asan_start_switch_fiber called from unknown thread\n");
    return;
  }
  t->StartSwitchFiber((FakeStack **)fakestacksave, (uptr)bottom, size);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_finish_switch_fiber(void *fakestack, const void **bottom_old,
                                     uptr *size_old) {
  AsanThread *t = GetCurrentThread();
  if (!t) {
    VReport(1, "__asan_finish_switch_fiber called from unknown thread\n");
    return;
  }
  t->FinishSwitchFiber((FakeStack *)fakestack, (uptr *)bottom_old,
                       (uptr *)size_old);
}
}
