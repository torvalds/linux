//===-- sanitizer_thread_registry_test.cpp --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of shared sanitizer runtime.
//
//===----------------------------------------------------------------------===//
#include "sanitizer_common/sanitizer_thread_registry.h"

#include <vector>

#include "gtest/gtest.h"
#include "sanitizer_pthread_wrappers.h"

namespace __sanitizer {

static Mutex tctx_allocator_lock;
static LowLevelAllocator tctx_allocator;

template<typename TCTX>
static ThreadContextBase *GetThreadContext(u32 tid) {
  Lock l(&tctx_allocator_lock);
  return new(tctx_allocator) TCTX(tid);
}

static const u32 kMaxRegistryThreads = 1000;
static const u32 kRegistryQuarantine = 2;

static void CheckThreadQuantity(ThreadRegistry *registry, uptr exp_total,
                                uptr exp_running, uptr exp_alive) {
  uptr total, running, alive;
  registry->GetNumberOfThreads(&total, &running, &alive);
  EXPECT_EQ(exp_total, total);
  EXPECT_EQ(exp_running, running);
  EXPECT_EQ(exp_alive, alive);
}

static bool is_detached(u32 tid) {
  return (tid % 2 == 0);
}

static uptr get_uid(u32 tid) {
  return tid * 2;
}

static bool HasName(ThreadContextBase *tctx, void *arg) {
  char *name = (char*)arg;
  return (0 == internal_strcmp(tctx->name, name));
}

static bool HasUid(ThreadContextBase *tctx, void *arg) {
  uptr uid = (uptr)arg;
  return (tctx->user_id == uid);
}

static void MarkUidAsPresent(ThreadContextBase *tctx, void *arg) {
  bool *arr = (bool*)arg;
  arr[tctx->tid] = true;
}

static void TestRegistry(ThreadRegistry *registry, bool has_quarantine) {
  // Create and start a main thread.
  EXPECT_EQ(0U, registry->CreateThread(get_uid(0), true, -1, 0));
  registry->StartThread(0, 0, ThreadType::Regular, 0);
  // Create a bunch of threads.
  for (u32 i = 1; i <= 10; i++) {
    EXPECT_EQ(i, registry->CreateThread(get_uid(i), is_detached(i), 0, 0));
  }
  CheckThreadQuantity(registry, 11, 1, 11);
  // Start some of them.
  for (u32 i = 1; i <= 5; i++) {
    registry->StartThread(i, 0, ThreadType::Regular, 0);
  }
  CheckThreadQuantity(registry, 11, 6, 11);
  // Finish, create and start more threads.
  for (u32 i = 1; i <= 5; i++) {
    registry->FinishThread(i);
    if (!is_detached(i))
      registry->JoinThread(i, 0);
  }
  for (u32 i = 6; i <= 10; i++) {
    registry->StartThread(i, 0, ThreadType::Regular, 0);
  }
  std::vector<u32> new_tids;
  for (u32 i = 11; i <= 15; i++) {
    new_tids.push_back(
        registry->CreateThread(get_uid(i), is_detached(i), 0, 0));
  }
  ASSERT_LE(kRegistryQuarantine, 5U);
  u32 exp_total = 16 - (has_quarantine ? 5 - kRegistryQuarantine  : 0);
  CheckThreadQuantity(registry, exp_total, 6, 11);
  // Test SetThreadName and FindThread.
  registry->SetThreadName(6, "six");
  registry->SetThreadName(7, "seven");
  EXPECT_EQ(7U, registry->FindThread(HasName, (void*)"seven"));
  EXPECT_EQ(kInvalidTid, registry->FindThread(HasName, (void *)"none"));
  EXPECT_EQ(0U, registry->FindThread(HasUid, (void*)get_uid(0)));
  EXPECT_EQ(10U, registry->FindThread(HasUid, (void*)get_uid(10)));
  EXPECT_EQ(kInvalidTid, registry->FindThread(HasUid, (void *)0x1234));
  // Detach and finish and join remaining threads.
  for (u32 i = 6; i <= 10; i++) {
    registry->DetachThread(i, 0);
    registry->FinishThread(i);
  }
  for (u32 i = 0; i < new_tids.size(); i++) {
    u32 tid = new_tids[i];
    registry->StartThread(tid, 0, ThreadType::Regular, 0);
    registry->DetachThread(tid, 0);
    registry->FinishThread(tid);
  }
  CheckThreadQuantity(registry, exp_total, 1, 1);
  // Test methods that require the caller to hold a ThreadRegistryLock.
  bool has_tid[16];
  internal_memset(&has_tid[0], 0, sizeof(has_tid));
  {
    ThreadRegistryLock l(registry);
    registry->RunCallbackForEachThreadLocked(MarkUidAsPresent, &has_tid[0]);
  }
  for (u32 i = 0; i < exp_total; i++) {
    EXPECT_TRUE(has_tid[i]);
  }
  {
    ThreadRegistryLock l(registry);
    registry->CheckLocked();
    ThreadContextBase *main_thread = registry->GetThreadLocked(0);
    EXPECT_EQ(main_thread, registry->FindThreadContextLocked(
        HasUid, (void*)get_uid(0)));
  }
  EXPECT_EQ(11U, registry->GetMaxAliveThreads());
}

TEST(SanitizerCommon, ThreadRegistryTest) {
  ThreadRegistry quarantine_registry(GetThreadContext<ThreadContextBase>,
                                     kMaxRegistryThreads, kRegistryQuarantine,
                                     0);
  TestRegistry(&quarantine_registry, true);

  ThreadRegistry no_quarantine_registry(GetThreadContext<ThreadContextBase>,
                                        kMaxRegistryThreads,
                                        kMaxRegistryThreads, 0);
  TestRegistry(&no_quarantine_registry, false);
}

static const int kThreadsPerShard = 20;
static const int kNumShards = 25;

static int num_created[kNumShards + 1];
static int num_started[kNumShards + 1];
static int num_joined[kNumShards + 1];

namespace {

struct RunThreadArgs {
  ThreadRegistry *registry;
  uptr shard;  // started from 1.
};

class TestThreadContext final : public ThreadContextBase {
 public:
  explicit TestThreadContext(int tid) : ThreadContextBase(tid) {}
  void OnJoined(void *arg) {
    uptr shard = (uptr)arg;
    num_joined[shard]++;
  }
  void OnStarted(void *arg) {
    uptr shard = (uptr)arg;
    num_started[shard]++;
  }
  void OnCreated(void *arg) {
    uptr shard = (uptr)arg;
    num_created[shard]++;
  }
};

}  // namespace

void *RunThread(void *arg) {
  RunThreadArgs *args = static_cast<RunThreadArgs*>(arg);
  std::vector<int> tids;
  for (int i = 0; i < kThreadsPerShard; i++)
    tids.push_back(
        args->registry->CreateThread(0, false, 0, (void*)args->shard));
  for (int i = 0; i < kThreadsPerShard; i++)
    args->registry->StartThread(tids[i], 0, ThreadType::Regular,
        (void*)args->shard);
  for (int i = 0; i < kThreadsPerShard; i++)
    args->registry->FinishThread(tids[i]);
  for (int i = 0; i < kThreadsPerShard; i++)
    args->registry->JoinThread(tids[i], (void*)args->shard);
  return 0;
}

static void ThreadedTestRegistry(ThreadRegistry *registry) {
  // Create and start a main thread.
  EXPECT_EQ(0U, registry->CreateThread(0, true, -1, 0));
  registry->StartThread(0, 0, ThreadType::Regular, 0);
  pthread_t threads[kNumShards];
  RunThreadArgs args[kNumShards];
  for (int i = 0; i < kNumShards; i++) {
    args[i].registry = registry;
    args[i].shard = i + 1;
    PTHREAD_CREATE(&threads[i], 0, RunThread, &args[i]);
  }
  for (int i = 0; i < kNumShards; i++) {
    PTHREAD_JOIN(threads[i], 0);
  }
  // Check that each thread created/started/joined correct amount
  // of "threads" in thread_registry.
  EXPECT_EQ(1, num_created[0]);
  EXPECT_EQ(1, num_started[0]);
  EXPECT_EQ(0, num_joined[0]);
  for (int i = 1; i <= kNumShards; i++) {
    EXPECT_EQ(kThreadsPerShard, num_created[i]);
    EXPECT_EQ(kThreadsPerShard, num_started[i]);
    EXPECT_EQ(kThreadsPerShard, num_joined[i]);
  }
}

TEST(SanitizerCommon, ThreadRegistryThreadedTest) {
  memset(&num_created, 0, sizeof(num_created));
  memset(&num_started, 0, sizeof(num_created));
  memset(&num_joined, 0, sizeof(num_created));

  ThreadRegistry registry(GetThreadContext<TestThreadContext>,
                          kThreadsPerShard * kNumShards + 1, 10, 0);
  ThreadedTestRegistry(&registry);
}

}  // namespace __sanitizer
