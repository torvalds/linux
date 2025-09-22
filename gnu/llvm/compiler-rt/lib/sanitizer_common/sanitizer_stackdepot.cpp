//===-- sanitizer_stackdepot.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is shared between AddressSanitizer and ThreadSanitizer
// run-time libraries.
//===----------------------------------------------------------------------===//

#include "sanitizer_stackdepot.h"

#include "sanitizer_atomic.h"
#include "sanitizer_common.h"
#include "sanitizer_hash.h"
#include "sanitizer_mutex.h"
#include "sanitizer_stack_store.h"
#include "sanitizer_stackdepotbase.h"

namespace __sanitizer {

struct StackDepotNode {
  using hash_type = u64;
  hash_type stack_hash;
  u32 link;
  StackStore::Id store_id;

  static const u32 kTabSizeLog = SANITIZER_ANDROID ? 16 : 20;

  typedef StackTrace args_type;
  bool eq(hash_type hash, const args_type &args) const {
    return hash == stack_hash;
  }
  static uptr allocated();
  static hash_type hash(const args_type &args) {
    MurMur2Hash64Builder H(args.size * sizeof(uptr));
    for (uptr i = 0; i < args.size; i++) H.add(args.trace[i]);
    H.add(args.tag);
    return H.get();
  }
  static bool is_valid(const args_type &args) {
    return args.size > 0 && args.trace;
  }
  void store(u32 id, const args_type &args, hash_type hash);
  args_type load(u32 id) const;
  static StackDepotHandle get_handle(u32 id);

  typedef StackDepotHandle handle_type;
};

static StackStore stackStore;

// FIXME(dvyukov): this single reserved bit is used in TSan.
typedef StackDepotBase<StackDepotNode, 1, StackDepotNode::kTabSizeLog>
    StackDepot;
static StackDepot theDepot;
// Keep mutable data out of frequently access nodes to improve caching
// efficiency.
static TwoLevelMap<atomic_uint32_t, StackDepot::kNodesSize1,
                   StackDepot::kNodesSize2>
    useCounts;

int StackDepotHandle::use_count() const {
  return atomic_load_relaxed(&useCounts[id_]);
}

void StackDepotHandle::inc_use_count_unsafe() {
  atomic_fetch_add(&useCounts[id_], 1, memory_order_relaxed);
}

uptr StackDepotNode::allocated() {
  return stackStore.Allocated() + useCounts.MemoryUsage();
}

static void CompressStackStore() {
  u64 start = Verbosity() >= 1 ? MonotonicNanoTime() : 0;
  uptr diff = stackStore.Pack(static_cast<StackStore::Compression>(
      Abs(common_flags()->compress_stack_depot)));
  if (!diff)
    return;
  if (Verbosity() >= 1) {
    u64 finish = MonotonicNanoTime();
    uptr total_before = theDepot.GetStats().allocated + diff;
    VPrintf(1, "%s: StackDepot released %zu KiB out of %zu KiB in %llu ms\n",
            SanitizerToolName, diff >> 10, total_before >> 10,
            (finish - start) / 1000000);
  }
}

namespace {

class CompressThread {
 public:
  constexpr CompressThread() = default;
  void NewWorkNotify();
  void Stop();
  void LockAndStop() SANITIZER_NO_THREAD_SAFETY_ANALYSIS;
  void Unlock() SANITIZER_NO_THREAD_SAFETY_ANALYSIS;

 private:
  enum class State {
    NotStarted = 0,
    Started,
    Failed,
    Stopped,
  };

  void Run();

  bool WaitForWork() {
    semaphore_.Wait();
    return atomic_load(&run_, memory_order_acquire);
  }

  Semaphore semaphore_ = {};
  StaticSpinMutex mutex_ = {};
  State state_ SANITIZER_GUARDED_BY(mutex_) = State::NotStarted;
  void *thread_ SANITIZER_GUARDED_BY(mutex_) = nullptr;
  atomic_uint8_t run_ = {};
};

static CompressThread compress_thread;

void CompressThread::NewWorkNotify() {
  int compress = common_flags()->compress_stack_depot;
  if (!compress)
    return;
  if (compress > 0 /* for testing or debugging */) {
    SpinMutexLock l(&mutex_);
    if (state_ == State::NotStarted) {
      atomic_store(&run_, 1, memory_order_release);
      CHECK_EQ(nullptr, thread_);
      thread_ = internal_start_thread(
          [](void *arg) -> void * {
            reinterpret_cast<CompressThread *>(arg)->Run();
            return nullptr;
          },
          this);
      state_ = thread_ ? State::Started : State::Failed;
    }
    if (state_ == State::Started) {
      semaphore_.Post();
      return;
    }
  }
  CompressStackStore();
}

void CompressThread::Run() {
  VPrintf(1, "%s: StackDepot compression thread started\n", SanitizerToolName);
  while (WaitForWork()) CompressStackStore();
  VPrintf(1, "%s: StackDepot compression thread stopped\n", SanitizerToolName);
}

void CompressThread::Stop() {
  void *t = nullptr;
  {
    SpinMutexLock l(&mutex_);
    if (state_ != State::Started)
      return;
    state_ = State::Stopped;
    CHECK_NE(nullptr, thread_);
    t = thread_;
    thread_ = nullptr;
  }
  atomic_store(&run_, 0, memory_order_release);
  semaphore_.Post();
  internal_join_thread(t);
}

void CompressThread::LockAndStop() {
  mutex_.Lock();
  if (state_ != State::Started)
    return;
  CHECK_NE(nullptr, thread_);

  atomic_store(&run_, 0, memory_order_release);
  semaphore_.Post();
  internal_join_thread(thread_);
  // Allow to restart after Unlock() if needed.
  state_ = State::NotStarted;
  thread_ = nullptr;
}

void CompressThread::Unlock() { mutex_.Unlock(); }

}  // namespace

void StackDepotNode::store(u32 id, const args_type &args, hash_type hash) {
  stack_hash = hash;
  uptr pack = 0;
  store_id = stackStore.Store(args, &pack);
  if (LIKELY(!pack))
    return;
  compress_thread.NewWorkNotify();
}

StackDepotNode::args_type StackDepotNode::load(u32 id) const {
  if (!store_id)
    return {};
  return stackStore.Load(store_id);
}

StackDepotStats StackDepotGetStats() { return theDepot.GetStats(); }

u32 StackDepotPut(StackTrace stack) { return theDepot.Put(stack); }

StackDepotHandle StackDepotPut_WithHandle(StackTrace stack) {
  return StackDepotNode::get_handle(theDepot.Put(stack));
}

StackTrace StackDepotGet(u32 id) {
  return theDepot.Get(id);
}

void StackDepotLockBeforeFork() {
  theDepot.LockBeforeFork();
  compress_thread.LockAndStop();
  stackStore.LockAll();
}

void StackDepotUnlockAfterFork(bool fork_child) {
  stackStore.UnlockAll();
  compress_thread.Unlock();
  theDepot.UnlockAfterFork(fork_child);
}

void StackDepotPrintAll() {
#if !SANITIZER_GO
  theDepot.PrintAll();
#endif
}

void StackDepotStopBackgroundThread() { compress_thread.Stop(); }

StackDepotHandle StackDepotNode::get_handle(u32 id) {
  return StackDepotHandle(&theDepot.nodes[id], id);
}

void StackDepotTestOnlyUnmap() {
  theDepot.TestOnlyUnmap();
  stackStore.TestOnlyUnmap();
}

} // namespace __sanitizer
