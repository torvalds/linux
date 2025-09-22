//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <numeric>
#include <stop_token>
#include <thread>

#include "benchmark/benchmark.h"
#include "make_test_thread.h"

using namespace std::chrono_literals;

// We have a single thread created by std::jthread consuming the stop_token:
// polling for stop_requested.
void BM_stop_token_single_thread_polling_stop_requested(benchmark::State& state) {
  auto thread_func = [&](std::stop_token st, std::atomic<std::uint64_t>* loop_count) {
    while (!st.stop_requested()) {
      // doing some work
      loop_count->fetch_add(1, std::memory_order_relaxed);
    }
  };

  std::atomic<std::uint64_t> loop_count(0);
  std::uint64_t total_loop_test_param = state.range(0);

  auto thread = support::make_test_jthread(thread_func, &loop_count);

  for (auto _ : state) {
    auto start_total = loop_count.load(std::memory_order_relaxed);

    while (loop_count.load(std::memory_order_relaxed) - start_total < total_loop_test_param) {
      std::this_thread::yield();
    }
  }
}

BENCHMARK(BM_stop_token_single_thread_polling_stop_requested)->RangeMultiplier(2)->Range(1 << 10, 1 << 24);

// We have multiple threads polling for stop_requested of the same stop_token.
void BM_stop_token_multi_thread_polling_stop_requested(benchmark::State& state) {
  std::atomic<bool> start{false};

  auto thread_func = [&start](std::atomic<std::uint64_t>* loop_count, std::stop_token st) {
    start.wait(false);
    while (!st.stop_requested()) {
      // doing some work
      loop_count->fetch_add(1, std::memory_order_relaxed);
    }
  };

  constexpr size_t thread_count = 20;

  std::uint64_t total_loop_test_param = state.range(0);

  std::vector<std::atomic<std::uint64_t>> loop_counts(thread_count);
  std::stop_source ss;
  std::vector<std::jthread> threads;
  threads.reserve(thread_count);

  for (size_t i = 0; i < thread_count; ++i) {
    threads.emplace_back(support::make_test_jthread(thread_func, &loop_counts[i], ss.get_token()));
  }

  auto get_total_loop = [&loop_counts] {
    std::uint64_t total = 0;
    for (const auto& loop_count : loop_counts) {
      total += loop_count.load(std::memory_order_relaxed);
    }
    return total;
  };

  start = true;
  start.notify_all();

  for (auto _ : state) {
    auto start_total = get_total_loop();

    while (get_total_loop() - start_total < total_loop_test_param) {
      std::this_thread::yield();
    }
  }

  ss.request_stop();
}

BENCHMARK(BM_stop_token_multi_thread_polling_stop_requested)->RangeMultiplier(2)->Range(1 << 10, 1 << 24);

// We have a single thread created by std::jthread consuming the stop_token:
// registering/deregistering callbacks, one at a time.
void BM_stop_token_single_thread_reg_unreg_callback(benchmark::State& state) {
  auto thread_func = [&](std::stop_token st, std::atomic<std::uint64_t>* reg_count) {
    while (!st.stop_requested()) {
      std::stop_callback cb{st, [&]() noexcept {}};
      benchmark::DoNotOptimize(cb);
      reg_count->fetch_add(1, std::memory_order_relaxed);
    }
  };

  std::atomic<std::uint64_t> reg_count(0);
  std::uint64_t total_reg_test_param = state.range(0);

  auto thread = support::make_test_jthread(thread_func, &reg_count);

  for (auto _ : state) {
    auto start_total = reg_count.load(std::memory_order_relaxed);

    while (reg_count.load(std::memory_order_relaxed) - start_total < total_reg_test_param) {
      std::this_thread::yield();
    }
  }
}
BENCHMARK(BM_stop_token_single_thread_reg_unreg_callback)->RangeMultiplier(2)->Range(1 << 10, 1 << 24);

// At startup, it creates a single stop_source which it will then pass an associated stop_token to every
// request.
//
// Assume a thread-pool handles these requests and for each request it polls for stop_requested(), then attaches a
// stop-callback, does some work, then detaches the stop-callback some time later. The lifetime of requests/callbacks
// would overlap with other requests/callback from the same thread.
//
// Say something like each thread keeping a circular buffer of N stop-callbacks and destroying the stop-callbacks in
// FIFO order
void BM_stop_token_async_reg_unreg_callback(benchmark::State& state) {
  struct dummy_stop_callback {
    void operator()() const noexcept {}
  };

  constexpr size_t thread_count             = 20;
  constexpr size_t concurrent_request_count = 1000;
  std::atomic<bool> start{false};

  std::uint64_t total_reg_test_param = state.range(0);
  std::vector<std::atomic<std::uint64_t>> reg_counts(thread_count);

  std::stop_source ss;
  std::vector<std::jthread> threads;
  threads.reserve(thread_count);

  auto thread_func = [&start](std::atomic<std::uint64_t>* count, std::stop_token st) {
    std::vector<std::optional<std::stop_callback<dummy_stop_callback>>> cbs(concurrent_request_count);

    start.wait(false);

    std::uint32_t index = 0;
    while (!st.stop_requested()) {
      cbs[index].emplace(st, dummy_stop_callback{});
      index = (index + 1) % concurrent_request_count;
      count->fetch_add(1, std::memory_order_relaxed);
    }
  };

  for (size_t i = 0; i < thread_count; ++i) {
    threads.emplace_back(support::make_test_jthread(thread_func, &reg_counts[i], ss.get_token()));
  }

  auto get_total_reg = [&] {
    std::uint64_t total = 0;
    for (const auto& reg_count : reg_counts) {
      total += reg_count.load(std::memory_order_relaxed);
    }
    return total;
  };

  start = true;
  start.notify_all();

  for (auto _ : state) {
    auto start_total = get_total_reg();

    while (get_total_reg() - start_total < total_reg_test_param) {
      std::this_thread::yield();
    }
  }

  ss.request_stop();
}
BENCHMARK(BM_stop_token_async_reg_unreg_callback)->RangeMultiplier(2)->Range(1 << 10, 1 << 24);

BENCHMARK_MAIN();
