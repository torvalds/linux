
#include "hwasan_thread.h"

#include "hwasan.h"
#include "hwasan_interface_internal.h"
#include "hwasan_mapping.h"
#include "hwasan_poisoning.h"
#include "hwasan_thread_list.h"
#include "sanitizer_common/sanitizer_atomic.h"
#include "sanitizer_common/sanitizer_file.h"
#include "sanitizer_common/sanitizer_placement_new.h"
#include "sanitizer_common/sanitizer_tls_get_addr.h"

namespace __hwasan {

static u32 RandomSeed() {
  u32 seed;
  do {
    if (UNLIKELY(!GetRandom(reinterpret_cast<void *>(&seed), sizeof(seed),
                            /*blocking=*/false))) {
      seed = static_cast<u32>(
          (NanoTime() >> 12) ^
          (reinterpret_cast<uptr>(__builtin_frame_address(0)) >> 4));
    }
  } while (!seed);
  return seed;
}

void Thread::InitRandomState() {
  random_state_ = flags()->random_tags ? RandomSeed() : unique_id_;
  random_state_inited_ = true;

  // Push a random number of zeros onto the ring buffer so that the first stack
  // tag base will be random.
  for (tag_t i = 0, e = GenerateRandomTag(); i != e; ++i)
    stack_allocations_->push(0);
}

void Thread::Init(uptr stack_buffer_start, uptr stack_buffer_size,
                  const InitState *state) {
  CHECK_EQ(0, unique_id_);  // try to catch bad stack reuse
  CHECK_EQ(0, stack_top_);
  CHECK_EQ(0, stack_bottom_);

  static atomic_uint64_t unique_id;
  unique_id_ = atomic_fetch_add(&unique_id, 1, memory_order_relaxed);
  if (!IsMainThread())
    os_id_ = GetTid();

  if (auto sz = flags()->heap_history_size)
    heap_allocations_ = HeapAllocationsRingBuffer::New(sz);

#if !SANITIZER_FUCHSIA
  // Do not initialize the stack ring buffer just yet on Fuchsia. Threads will
  // be initialized before we enter the thread itself, so we will instead call
  // this later.
  InitStackRingBuffer(stack_buffer_start, stack_buffer_size);
#endif
  InitStackAndTls(state);
  dtls_ = DTLS_Get();
  AllocatorThreadStart(allocator_cache());

  if (flags()->verbose_threads) {
    if (IsMainThread()) {
      Printf("sizeof(Thread): %zd sizeof(HeapRB): %zd sizeof(StackRB): %zd\n",
             sizeof(Thread), heap_allocations_->SizeInBytes(),
             stack_allocations_->size() * sizeof(uptr));
    }
    Print("Creating  : ");
  }
  ClearShadowForThreadStackAndTLS();
}

void Thread::InitStackRingBuffer(uptr stack_buffer_start,
                                 uptr stack_buffer_size) {
  HwasanTSDThreadInit();  // Only needed with interceptors.
  uptr *ThreadLong = GetCurrentThreadLongPtr();
  // The following implicitly sets (this) as the current thread.
  stack_allocations_ = new (ThreadLong)
      StackAllocationsRingBuffer((void *)stack_buffer_start, stack_buffer_size);
  // Check that it worked.
  CHECK_EQ(GetCurrentThread(), this);

  // ScopedTaggingDisable needs GetCurrentThread to be set up.
  ScopedTaggingDisabler disabler;

  if (stack_bottom_) {
    int local;
    CHECK(AddrIsInStack((uptr)&local));
    CHECK(MemIsApp(stack_bottom_));
    CHECK(MemIsApp(stack_top_ - 1));
  }
}

void Thread::ClearShadowForThreadStackAndTLS() {
  if (stack_top_ != stack_bottom_)
    TagMemory(UntagAddr(stack_bottom_),
              UntagAddr(stack_top_) - UntagAddr(stack_bottom_),
              GetTagFromPointer(stack_top_));
  if (tls_begin_ != tls_end_)
    TagMemory(UntagAddr(tls_begin_),
              UntagAddr(tls_end_) - UntagAddr(tls_begin_),
              GetTagFromPointer(tls_begin_));
}

void Thread::Destroy() {
  if (flags()->verbose_threads)
    Print("Destroying: ");
  AllocatorThreadFinish(allocator_cache());
  ClearShadowForThreadStackAndTLS();
  if (heap_allocations_)
    heap_allocations_->Delete();
  DTLS_Destroy();
  // Unregister this as the current thread.
  // Instrumented code can not run on this thread from this point onwards, but
  // malloc/free can still be served. Glibc may call free() very late, after all
  // TSD destructors are done.
  CHECK_EQ(GetCurrentThread(), this);
  *GetCurrentThreadLongPtr() = 0;
}

void Thread::Print(const char *Prefix) {
  Printf("%sT%zd %p stack: [%p,%p) sz: %zd tls: [%p,%p)\n", Prefix, unique_id_,
         (void *)this, stack_bottom(), stack_top(),
         stack_top() - stack_bottom(), tls_begin(), tls_end());
}

static u32 xorshift(u32 state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

// Generate a (pseudo-)random non-zero tag.
tag_t Thread::GenerateRandomTag(uptr num_bits) {
  DCHECK_GT(num_bits, 0);
  if (tagging_disabled_)
    return 0;
  tag_t tag;
  const uptr tag_mask = (1ULL << num_bits) - 1;
  do {
    if (flags()->random_tags) {
      if (!random_buffer_) {
        EnsureRandomStateInited();
        random_buffer_ = random_state_ = xorshift(random_state_);
      }
      CHECK(random_buffer_);
      tag = random_buffer_ & tag_mask;
      random_buffer_ >>= num_bits;
    } else {
      EnsureRandomStateInited();
      random_state_ += 1;
      tag = random_state_ & tag_mask;
    }
  } while (!tag);
  return tag;
}

void EnsureMainThreadIDIsCorrect() {
  auto *t = __hwasan::GetCurrentThread();
  if (t && (t->IsMainThread()))
    t->set_os_id(GetTid());
}

} // namespace __hwasan

// --- Implementation of LSan-specific functions --- {{{1
namespace __lsan {

static __hwasan::HwasanThreadList *GetHwasanThreadListLocked() {
  auto &tl = __hwasan::hwasanThreadList();
  tl.CheckLocked();
  return &tl;
}

static __hwasan::Thread *GetThreadByOsIDLocked(tid_t os_id) {
  return GetHwasanThreadListLocked()->FindThreadLocked(
      [os_id](__hwasan::Thread *t) { return t->os_id() == os_id; });
}

void LockThreads() {
  __hwasan::hwasanThreadList().Lock();
  __hwasan::hwasanThreadArgRetval().Lock();
}

void UnlockThreads() {
  __hwasan::hwasanThreadArgRetval().Unlock();
  __hwasan::hwasanThreadList().Unlock();
}

void EnsureMainThreadIDIsCorrect() { __hwasan::EnsureMainThreadIDIsCorrect(); }

bool GetThreadRangesLocked(tid_t os_id, uptr *stack_begin, uptr *stack_end,
                           uptr *tls_begin, uptr *tls_end, uptr *cache_begin,
                           uptr *cache_end, DTLS **dtls) {
  auto *t = GetThreadByOsIDLocked(os_id);
  if (!t)
    return false;
  *stack_begin = t->stack_bottom();
  *stack_end = t->stack_top();
  *tls_begin = t->tls_begin();
  *tls_end = t->tls_end();
  // Fixme: is this correct for HWASan.
  *cache_begin = 0;
  *cache_end = 0;
  *dtls = t->dtls();
  return true;
}

void GetAllThreadAllocatorCachesLocked(InternalMmapVector<uptr> *caches) {}

void GetThreadExtraStackRangesLocked(tid_t os_id,
                                     InternalMmapVector<Range> *ranges) {}
void GetThreadExtraStackRangesLocked(InternalMmapVector<Range> *ranges) {}

void GetAdditionalThreadContextPtrsLocked(InternalMmapVector<uptr> *ptrs) {
  __hwasan::hwasanThreadArgRetval().GetAllPtrsLocked(ptrs);
}

void GetRunningThreadsLocked(InternalMmapVector<tid_t> *threads) {}

}  // namespace __lsan
