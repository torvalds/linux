//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <atomic>
#include <numeric>
#include <thread>

#include "benchmark/benchmark.h"
#include "make_test_thread.h"

using namespace std::chrono_literals;

void BM_atomic_wait_one_thread_one_atomic_wait(benchmark::State& state) {
  std::atomic<std::uint64_t> a;
  auto thread_func = [&](std::stop_token st) {
    while (!st.stop_requested()) {
      a.fetch_add(1, std::memory_order_relaxed);
      a.notify_all();
    }
  };

  std::uint64_t total_loop_test_param = state.range(0);

  auto thread = support::make_test_jthread(thread_func);

  for (auto _ : state) {
    for (std::uint64_t i = 0; i < total_loop_test_param; ++i) {
      auto old = a.load(std::memory_order_relaxed);
      a.wait(old);
    }
  }
}
BENCHMARK(BM_atomic_wait_one_thread_one_atomic_wait)->RangeMultiplier(2)->Range(1 << 10, 1 << 24);

void BM_atomic_wait_multi_thread_one_atomic_wait(benchmark::State& state) {
  std::atomic<std::uint64_t> a;
  auto notify_func = [&](std::stop_token st) {
    while (!st.stop_requested()) {
      a.fetch_add(1, std::memory_order_relaxed);
      a.notify_all();
    }
  };

  std::uint64_t total_loop_test_param = state.range(0);
  constexpr auto num_waiting_threads  = 15;
  std::vector<std::jthread> wait_threads;
  wait_threads.reserve(num_waiting_threads);

  auto notify_thread = support::make_test_jthread(notify_func);

  std::atomic<std::uint64_t> start_flag = 0;
  std::atomic<std::uint64_t> done_count = 0;
  auto wait_func                        = [&a, &start_flag, &done_count, total_loop_test_param](std::stop_token st) {
    auto old_start = 0;
    while (!st.stop_requested()) {
      start_flag.wait(old_start);
      old_start = start_flag.load();
      for (std::uint64_t i = 0; i < total_loop_test_param; ++i) {
        auto old = a.load(std::memory_order_relaxed);
        a.wait(old);
      }
      done_count.fetch_add(1);
    }
  };

  for (size_t i = 0; i < num_waiting_threads; ++i) {
    wait_threads.emplace_back(support::make_test_jthread(wait_func));
  }

  for (auto _ : state) {
    done_count = 0;
    start_flag.fetch_add(1);
    start_flag.notify_all();
    while (done_count < num_waiting_threads) {
      std::this_thread::yield();
    }
  }
  for (auto& t : wait_threads) {
    t.request_stop();
  }
  start_flag.fetch_add(1);
  start_flag.notify_all();
  for (auto& t : wait_threads) {
    t.join();
  }
}
BENCHMARK(BM_atomic_wait_multi_thread_one_atomic_wait)->RangeMultiplier(2)->Range(1 << 10, 1 << 20);

void BM_atomic_wait_multi_thread_wait_different_atomics(benchmark::State& state) {
  const std::uint64_t total_loop_test_param = state.range(0);
  constexpr std::uint64_t num_atomics       = 7;
  std::vector<std::atomic<std::uint64_t>> atomics(num_atomics);

  auto notify_func = [&](std::stop_token st, size_t idx) {
    while (!st.stop_requested()) {
      atomics[idx].fetch_add(1, std::memory_order_relaxed);
      atomics[idx].notify_all();
    }
  };

  std::atomic<std::uint64_t> start_flag = 0;
  std::atomic<std::uint64_t> done_count = 0;

  auto wait_func = [&, total_loop_test_param](std::stop_token st, size_t idx) {
    auto old_start = 0;
    while (!st.stop_requested()) {
      start_flag.wait(old_start);
      old_start = start_flag.load();
      for (std::uint64_t i = 0; i < total_loop_test_param; ++i) {
        auto old = atomics[idx].load(std::memory_order_relaxed);
        atomics[idx].wait(old);
      }
      done_count.fetch_add(1);
    }
  };

  std::vector<std::jthread> notify_threads;
  notify_threads.reserve(num_atomics);

  std::vector<std::jthread> wait_threads;
  wait_threads.reserve(num_atomics);

  for (size_t i = 0; i < num_atomics; ++i) {
    notify_threads.emplace_back(support::make_test_jthread(notify_func, i));
  }

  for (size_t i = 0; i < num_atomics; ++i) {
    wait_threads.emplace_back(support::make_test_jthread(wait_func, i));
  }

  for (auto _ : state) {
    done_count = 0;
    start_flag.fetch_add(1);
    start_flag.notify_all();
    while (done_count < num_atomics) {
      std::this_thread::yield();
    }
  }
  for (auto& t : wait_threads) {
    t.request_stop();
  }
  start_flag.fetch_add(1);
  start_flag.notify_all();
  for (auto& t : wait_threads) {
    t.join();
  }
}
BENCHMARK(BM_atomic_wait_multi_thread_wait_different_atomics)->RangeMultiplier(2)->Range(1 << 10, 1 << 20);

BENCHMARK_MAIN();
