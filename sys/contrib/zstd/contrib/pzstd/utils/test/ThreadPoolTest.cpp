/*
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */
#include "utils/ThreadPool.h"

#include <gtest/gtest.h>
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

using namespace pzstd;

TEST(ThreadPool, Ordering) {
  std::vector<int> results;

  {
    ThreadPool executor(1);
    for (int i = 0; i < 10; ++i) {
      executor.add([ &results, i ] { results.push_back(i); });
    }
  }

  for (int i = 0; i < 10; ++i) {
    EXPECT_EQ(i, results[i]);
  }
}

TEST(ThreadPool, AllJobsFinished) {
  std::atomic<unsigned> numFinished{0};
  std::atomic<bool> start{false};
  {
    std::cerr << "Creating executor" << std::endl;
    ThreadPool executor(5);
    for (int i = 0; i < 10; ++i) {
      executor.add([ &numFinished, &start ] {
        while (!start.load()) {
          std::this_thread::yield();
        }
        ++numFinished;
      });
    }
    std::cerr << "Starting" << std::endl;
    start.store(true);
    std::cerr << "Finishing" << std::endl;
  }
  EXPECT_EQ(10, numFinished.load());
}

TEST(ThreadPool, AddJobWhileJoining) {
  std::atomic<bool> done{false};
  {
    ThreadPool executor(1);
    executor.add([&executor, &done] {
      while (!done.load()) {
        std::this_thread::yield();
      }
      // Sleep for a second to be sure that we are joining
      std::this_thread::sleep_for(std::chrono::seconds(1));
      executor.add([] {
        EXPECT_TRUE(false);
      });
    });
    done.store(true);
  }
}
