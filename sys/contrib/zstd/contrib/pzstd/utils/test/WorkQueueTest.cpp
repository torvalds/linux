/*
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */
#include "utils/Buffer.h"
#include "utils/WorkQueue.h"

#include <gtest/gtest.h>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace pzstd;

namespace {
struct Popper {
  WorkQueue<int>* queue;
  int* results;
  std::mutex* mutex;

  void operator()() {
    int result;
    while (queue->pop(result)) {
      std::lock_guard<std::mutex> lock(*mutex);
      results[result] = result;
    }
  }
};
}

TEST(WorkQueue, SingleThreaded) {
  WorkQueue<int> queue;
  int result;

  queue.push(5);
  EXPECT_TRUE(queue.pop(result));
  EXPECT_EQ(5, result);

  queue.push(1);
  queue.push(2);
  EXPECT_TRUE(queue.pop(result));
  EXPECT_EQ(1, result);
  EXPECT_TRUE(queue.pop(result));
  EXPECT_EQ(2, result);

  queue.push(1);
  queue.push(2);
  queue.finish();
  EXPECT_TRUE(queue.pop(result));
  EXPECT_EQ(1, result);
  EXPECT_TRUE(queue.pop(result));
  EXPECT_EQ(2, result);
  EXPECT_FALSE(queue.pop(result));

  queue.waitUntilFinished();
}

TEST(WorkQueue, SPSC) {
  WorkQueue<int> queue;
  const int max = 100;

  for (int i = 0; i < 10; ++i) {
    queue.push(int{i});
  }

  std::thread thread([ &queue, max ] {
    int result;
    for (int i = 0;; ++i) {
      if (!queue.pop(result)) {
        EXPECT_EQ(i, max);
        break;
      }
      EXPECT_EQ(i, result);
    }
  });

  std::this_thread::yield();
  for (int i = 10; i < max; ++i) {
    queue.push(int{i});
  }
  queue.finish();

  thread.join();
}

TEST(WorkQueue, SPMC) {
  WorkQueue<int> queue;
  std::vector<int> results(50, -1);
  std::mutex mutex;
  std::vector<std::thread> threads;
  for (int i = 0; i < 5; ++i) {
    threads.emplace_back(Popper{&queue, results.data(), &mutex});
  }

  for (int i = 0; i < 50; ++i) {
    queue.push(int{i});
  }
  queue.finish();

  for (auto& thread : threads) {
    thread.join();
  }

  for (int i = 0; i < 50; ++i) {
    EXPECT_EQ(i, results[i]);
  }
}

TEST(WorkQueue, MPMC) {
  WorkQueue<int> queue;
  std::vector<int> results(100, -1);
  std::mutex mutex;
  std::vector<std::thread> popperThreads;
  for (int i = 0; i < 4; ++i) {
    popperThreads.emplace_back(Popper{&queue, results.data(), &mutex});
  }

  std::vector<std::thread> pusherThreads;
  for (int i = 0; i < 2; ++i) {
    auto min = i * 50;
    auto max = (i + 1) * 50;
    pusherThreads.emplace_back(
        [ &queue, min, max ] {
          for (int i = min; i < max; ++i) {
            queue.push(int{i});
          }
        });
  }

  for (auto& thread : pusherThreads) {
    thread.join();
  }
  queue.finish();

  for (auto& thread : popperThreads) {
    thread.join();
  }

  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(i, results[i]);
  }
}

TEST(WorkQueue, BoundedSizeWorks) {
  WorkQueue<int> queue(1);
  int result;
  queue.push(5);
  queue.pop(result);
  queue.push(5);
  queue.pop(result);
  queue.push(5);
  queue.finish();
  queue.pop(result);
  EXPECT_EQ(5, result);
}

TEST(WorkQueue, BoundedSizePushAfterFinish) {
  WorkQueue<int> queue(1);
  int result;
  queue.push(5);
  std::thread pusher([&queue] {
    queue.push(6);
  });
  // Dirtily try and make sure that pusher has run.
  std::this_thread::sleep_for(std::chrono::seconds(1));
  queue.finish();
  EXPECT_TRUE(queue.pop(result));
  EXPECT_EQ(5, result);
  EXPECT_FALSE(queue.pop(result));

  pusher.join();
}

TEST(WorkQueue, SetMaxSize) {
  WorkQueue<int> queue(2);
  int result;
  queue.push(5);
  queue.push(6);
  queue.setMaxSize(1);
  std::thread pusher([&queue] {
    queue.push(7);
  });
  // Dirtily try and make sure that pusher has run.
  std::this_thread::sleep_for(std::chrono::seconds(1));
  queue.finish();
  EXPECT_TRUE(queue.pop(result));
  EXPECT_EQ(5, result);
  EXPECT_TRUE(queue.pop(result));
  EXPECT_EQ(6, result);
  EXPECT_FALSE(queue.pop(result));

  pusher.join();
}

TEST(WorkQueue, BoundedSizeMPMC) {
  WorkQueue<int> queue(10);
  std::vector<int> results(200, -1);
  std::mutex mutex;
  std::cerr << "Creating popperThreads" << std::endl;
  std::vector<std::thread> popperThreads;
  for (int i = 0; i < 4; ++i) {
    popperThreads.emplace_back(Popper{&queue, results.data(), &mutex});
  }

  std::cerr << "Creating pusherThreads" << std::endl;
  std::vector<std::thread> pusherThreads;
  for (int i = 0; i < 2; ++i) {
    auto min = i * 100;
    auto max = (i + 1) * 100;
    pusherThreads.emplace_back(
        [ &queue, min, max ] {
          for (int i = min; i < max; ++i) {
            queue.push(int{i});
          }
        });
  }

  std::cerr << "Joining pusherThreads" << std::endl;
  for (auto& thread : pusherThreads) {
    thread.join();
  }
  std::cerr << "Finishing queue" << std::endl;
  queue.finish();

  std::cerr << "Joining popperThreads" << std::endl;
  for (auto& thread : popperThreads) {
    thread.join();
  }

  std::cerr << "Inspecting results" << std::endl;
  for (int i = 0; i < 200; ++i) {
    EXPECT_EQ(i, results[i]);
  }
}

TEST(WorkQueue, FailedPush) {
  WorkQueue<std::unique_ptr<int>> queue;
  std::unique_ptr<int> x(new int{5});
  EXPECT_TRUE(queue.push(std::move(x)));
  EXPECT_EQ(nullptr, x);
  queue.finish();
  x.reset(new int{6});
  EXPECT_FALSE(queue.push(std::move(x)));
  EXPECT_NE(nullptr, x);
  EXPECT_EQ(6, *x);
}

TEST(BufferWorkQueue, SizeCalculatedCorrectly) {
  {
    BufferWorkQueue queue;
    queue.finish();
    EXPECT_EQ(0, queue.size());
  }
  {
    BufferWorkQueue queue;
    queue.push(Buffer(10));
    queue.finish();
    EXPECT_EQ(10, queue.size());
  }
  {
    BufferWorkQueue queue;
    queue.push(Buffer(10));
    queue.push(Buffer(5));
    queue.finish();
    EXPECT_EQ(15, queue.size());
  }
  {
    BufferWorkQueue queue;
    queue.push(Buffer(10));
    queue.push(Buffer(5));
    queue.finish();
    Buffer buffer;
    queue.pop(buffer);
    EXPECT_EQ(5, queue.size());
  }
}
