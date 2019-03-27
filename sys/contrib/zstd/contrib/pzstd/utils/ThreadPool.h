/*
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */
#pragma once

#include "utils/WorkQueue.h"

#include <cstddef>
#include <functional>
#include <thread>
#include <vector>

namespace pzstd {
/// A simple thread pool that pulls tasks off its queue in FIFO order.
class ThreadPool {
  std::vector<std::thread> threads_;

  WorkQueue<std::function<void()>> tasks_;

 public:
  /// Constructs a thread pool with `numThreads` threads.
  explicit ThreadPool(std::size_t numThreads) {
    threads_.reserve(numThreads);
    for (std::size_t i = 0; i < numThreads; ++i) {
      threads_.emplace_back([this] {
        std::function<void()> task;
        while (tasks_.pop(task)) {
          task();
        }
      });
    }
  }

  /// Finishes all tasks currently in the queue.
  ~ThreadPool() {
    tasks_.finish();
    for (auto& thread : threads_) {
      thread.join();
    }
  }

  /**
   * Adds `task` to the queue of tasks to execute. Since `task` is a
   * `std::function<>`, it cannot be a move only type. So any lambda passed must
   * not capture move only types (like `std::unique_ptr`).
   *
   * @param task  The task to execute.
   */
  void add(std::function<void()> task) {
    tasks_.push(std::move(task));
  }
};
}
