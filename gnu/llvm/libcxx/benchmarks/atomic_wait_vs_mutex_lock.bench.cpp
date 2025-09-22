//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// To run this test, build libcxx and cxx-benchmarks targets
// cd third-party/benchmark/tools
// ./compare.py filters ../../../build/libcxx/benchmarks/atomic_wait_vs_mutex_lock.libcxx.out BM_atomic_wait BM_mutex

#include <atomic>
#include <mutex>
#include <numeric>
#include <thread>

#include "benchmark/benchmark.h"
#include "make_test_thread.h"

using namespace std::chrono_literals;

struct AtomicLock {
  std::atomic<bool>& locked_;

  AtomicLock(const AtomicLock&)            = delete;
  AtomicLock& operator=(const AtomicLock&) = delete;

  AtomicLock(std::atomic<bool>& l) : locked_(l) { lock(); }
  ~AtomicLock() { unlock(); }

  void lock() {
    while (true) {
      locked_.wait(true, std::memory_order_relaxed);
      bool expected = false;
      if (locked_.compare_exchange_weak(expected, true, std::memory_order_acquire, std::memory_order_relaxed))
        break;
    }
  }

  void unlock() {
    locked_.store(false, std::memory_order_release);
    locked_.notify_all();
  }
};

// using LockState = std::atomic<bool>;
// using Lock      = AtomicLock;

// using LockState = std::mutex;
// using Lock = std::unique_lock<std::mutex>;

template <class LockState, class Lock>
void test_multi_thread_lock_unlock(benchmark::State& state) {
  std::uint64_t total_loop_test_param = state.range(0);
  constexpr auto num_threads          = 15;
  std::vector<std::jthread> threads;
  threads.reserve(num_threads);

  std::atomic<std::uint64_t> start_flag = 0;
  std::atomic<std::uint64_t> done_count = 0;

  LockState lock_state{};

  auto func = [&start_flag, &done_count, &lock_state, total_loop_test_param](std::stop_token st) {
    auto old_start = 0;
    while (!st.stop_requested()) {
      start_flag.wait(old_start);
      old_start = start_flag.load();

      // main things under test: locking and unlocking in the loop
      for (std::uint64_t i = 0; i < total_loop_test_param; ++i) {
        Lock l{lock_state};
      }

      done_count.fetch_add(1);
    }
  };

  for (size_t i = 0; i < num_threads; ++i) {
    threads.emplace_back(support::make_test_jthread(func));
  }

  for (auto _ : state) {
    done_count = 0;
    start_flag.fetch_add(1);
    start_flag.notify_all();
    while (done_count < num_threads) {
      std::this_thread::yield();
    }
  }
  for (auto& t : threads) {
    t.request_stop();
  }
  start_flag.fetch_add(1);
  start_flag.notify_all();
  for (auto& t : threads) {
    t.join();
  }
}

void BM_atomic_wait(benchmark::State& state) { test_multi_thread_lock_unlock<std::atomic<bool>, AtomicLock>(state); }
BENCHMARK(BM_atomic_wait)->RangeMultiplier(2)->Range(1 << 10, 1 << 20);

void BM_mutex(benchmark::State& state) {
  test_multi_thread_lock_unlock<std::mutex, std::unique_lock<std::mutex>>(state);
}
BENCHMARK(BM_mutex)->RangeMultiplier(2)->Range(1 << 10, 1 << 20);

BENCHMARK_MAIN();
