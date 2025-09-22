//===-- buffer_queue_test.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a function call tracing system.
//
//===----------------------------------------------------------------------===//
#include "xray_buffer_queue.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <atomic>
#include <future>
#include <thread>
#include <unistd.h>

namespace __xray {
namespace {

static constexpr size_t kSize = 4096;

using ::testing::Eq;

TEST(BufferQueueTest, API) {
  bool Success = false;
  BufferQueue Buffers(kSize, 1, Success);
  ASSERT_TRUE(Success);
}

TEST(BufferQueueTest, GetAndRelease) {
  bool Success = false;
  BufferQueue Buffers(kSize, 1, Success);
  ASSERT_TRUE(Success);
  BufferQueue::Buffer Buf;
  ASSERT_EQ(Buffers.getBuffer(Buf), BufferQueue::ErrorCode::Ok);
  ASSERT_NE(nullptr, Buf.Data);
  ASSERT_EQ(Buffers.releaseBuffer(Buf), BufferQueue::ErrorCode::Ok);
  ASSERT_EQ(nullptr, Buf.Data);
}

TEST(BufferQueueTest, GetUntilFailed) {
  bool Success = false;
  BufferQueue Buffers(kSize, 1, Success);
  ASSERT_TRUE(Success);
  BufferQueue::Buffer Buf0;
  EXPECT_EQ(Buffers.getBuffer(Buf0), BufferQueue::ErrorCode::Ok);
  BufferQueue::Buffer Buf1;
  EXPECT_EQ(BufferQueue::ErrorCode::NotEnoughMemory, Buffers.getBuffer(Buf1));
  EXPECT_EQ(Buffers.releaseBuffer(Buf0), BufferQueue::ErrorCode::Ok);
}

TEST(BufferQueueTest, ReleaseUnknown) {
  bool Success = false;
  BufferQueue Buffers(kSize, 1, Success);
  ASSERT_TRUE(Success);
  BufferQueue::Buffer Buf;
  Buf.Data = reinterpret_cast<void *>(0xdeadbeef);
  Buf.Size = kSize;
  Buf.Generation = Buffers.generation();

  BufferQueue::Buffer Known;
  EXPECT_THAT(Buffers.getBuffer(Known), Eq(BufferQueue::ErrorCode::Ok));
  EXPECT_THAT(Buffers.releaseBuffer(Buf),
              Eq(BufferQueue::ErrorCode::UnrecognizedBuffer));
  EXPECT_THAT(Buffers.releaseBuffer(Known), Eq(BufferQueue::ErrorCode::Ok));
}

TEST(BufferQueueTest, ErrorsWhenFinalising) {
  bool Success = false;
  BufferQueue Buffers(kSize, 2, Success);
  ASSERT_TRUE(Success);
  BufferQueue::Buffer Buf;
  ASSERT_EQ(Buffers.getBuffer(Buf), BufferQueue::ErrorCode::Ok);
  ASSERT_NE(nullptr, Buf.Data);
  ASSERT_EQ(Buffers.finalize(), BufferQueue::ErrorCode::Ok);
  BufferQueue::Buffer OtherBuf;
  ASSERT_EQ(BufferQueue::ErrorCode::QueueFinalizing,
            Buffers.getBuffer(OtherBuf));
  ASSERT_EQ(BufferQueue::ErrorCode::QueueFinalizing, Buffers.finalize());
  ASSERT_EQ(Buffers.releaseBuffer(Buf), BufferQueue::ErrorCode::Ok);
}

TEST(BufferQueueTest, MultiThreaded) {
  bool Success = false;
  BufferQueue Buffers(kSize, 100, Success);
  ASSERT_TRUE(Success);
  auto F = [&] {
    BufferQueue::Buffer B;
    while (true) {
      auto EC = Buffers.getBuffer(B);
      if (EC != BufferQueue::ErrorCode::Ok)
        return;
      Buffers.releaseBuffer(B);
    }
  };
  auto T0 = std::async(std::launch::async, F);
  auto T1 = std::async(std::launch::async, F);
  auto T2 = std::async(std::launch::async, [&] {
    while (Buffers.finalize() != BufferQueue::ErrorCode::Ok)
      ;
  });
  F();
}

TEST(BufferQueueTest, Apply) {
  bool Success = false;
  BufferQueue Buffers(kSize, 10, Success);
  ASSERT_TRUE(Success);
  auto Count = 0;
  BufferQueue::Buffer B;
  for (int I = 0; I < 10; ++I) {
    ASSERT_EQ(Buffers.getBuffer(B), BufferQueue::ErrorCode::Ok);
    ASSERT_EQ(Buffers.releaseBuffer(B), BufferQueue::ErrorCode::Ok);
  }
  Buffers.apply([&](const BufferQueue::Buffer &B) { ++Count; });
  ASSERT_EQ(Count, 10);
}

TEST(BufferQueueTest, GenerationalSupport) {
  bool Success = false;
  BufferQueue Buffers(kSize, 10, Success);
  ASSERT_TRUE(Success);
  BufferQueue::Buffer B0;
  ASSERT_EQ(Buffers.getBuffer(B0), BufferQueue::ErrorCode::Ok);
  ASSERT_EQ(Buffers.finalize(),
            BufferQueue::ErrorCode::Ok); // No more new buffers.

  // Re-initialise the queue.
  ASSERT_EQ(Buffers.init(kSize, 10), BufferQueue::ErrorCode::Ok);

  BufferQueue::Buffer B1;
  ASSERT_EQ(Buffers.getBuffer(B1), BufferQueue::ErrorCode::Ok);

  // Validate that the buffers come from different generations.
  ASSERT_NE(B0.Generation, B1.Generation);

  // We stash the current generation, for use later.
  auto PrevGen = B1.Generation;

  // At this point, we want to ensure that we can return the buffer from the
  // first "generation" would still be accepted in the new generation...
  EXPECT_EQ(Buffers.releaseBuffer(B0), BufferQueue::ErrorCode::Ok);

  // ... and that the new buffer is also accepted.
  EXPECT_EQ(Buffers.releaseBuffer(B1), BufferQueue::ErrorCode::Ok);

  // A next round will do the same, ensure that we are able to do multiple
  // rounds in this case.
  ASSERT_EQ(Buffers.finalize(), BufferQueue::ErrorCode::Ok);
  ASSERT_EQ(Buffers.init(kSize, 10), BufferQueue::ErrorCode::Ok);
  EXPECT_EQ(Buffers.getBuffer(B0), BufferQueue::ErrorCode::Ok);
  EXPECT_EQ(Buffers.getBuffer(B1), BufferQueue::ErrorCode::Ok);

  // Here we ensure that the generation is different from the previous
  // generation.
  EXPECT_NE(B0.Generation, PrevGen);
  EXPECT_EQ(B1.Generation, B1.Generation);
  ASSERT_EQ(Buffers.finalize(), BufferQueue::ErrorCode::Ok);
  EXPECT_EQ(Buffers.releaseBuffer(B0), BufferQueue::ErrorCode::Ok);
  EXPECT_EQ(Buffers.releaseBuffer(B1), BufferQueue::ErrorCode::Ok);
}

TEST(BufferQueueTest, GenerationalSupportAcrossThreads) {
  bool Success = false;
  BufferQueue Buffers(kSize, 10, Success);
  ASSERT_TRUE(Success);

  std::atomic<int> Counter{0};

  // This function allows us to use thread-local storage to isolate the
  // instances of the buffers to be used. It also allows us signal the threads
  // of a new generation, and allow those to get new buffers. This is
  // representative of how we expect the buffer queue to be used by the XRay
  // runtime.
  auto Process = [&] {
    thread_local BufferQueue::Buffer B;
    ASSERT_EQ(Buffers.getBuffer(B), BufferQueue::ErrorCode::Ok);
    auto FirstGen = B.Generation;

    // Signal that we've gotten a buffer in the thread.
    Counter.fetch_add(1, std::memory_order_acq_rel);
    while (!Buffers.finalizing()) {
      Buffers.releaseBuffer(B);
      Buffers.getBuffer(B);
    }

    // Signal that we've exited the get/release buffer loop.
    Counter.fetch_sub(1, std::memory_order_acq_rel);
    if (B.Data != nullptr)
      Buffers.releaseBuffer(B);

    // Spin until we find that the Buffer Queue is no longer finalizing.
    while (Buffers.getBuffer(B) != BufferQueue::ErrorCode::Ok)
      ;

    // Signal that we've successfully gotten a buffer in the thread.
    Counter.fetch_add(1, std::memory_order_acq_rel);

    EXPECT_NE(FirstGen, B.Generation);
    EXPECT_EQ(Buffers.releaseBuffer(B), BufferQueue::ErrorCode::Ok);

    // Signal that we've successfully exited.
    Counter.fetch_sub(1, std::memory_order_acq_rel);
  };

  // Spawn two threads running Process.
  std::thread T0(Process), T1(Process);

  // Spin until we find the counter is up to 2.
  while (Counter.load(std::memory_order_acquire) != 2)
    ;

  // Then we finalize, then re-initialize immediately.
  Buffers.finalize();

  // Spin until we find the counter is down to 0.
  while (Counter.load(std::memory_order_acquire) != 0)
    ;

  // Then we re-initialize.
  EXPECT_EQ(Buffers.init(kSize, 10), BufferQueue::ErrorCode::Ok);

  T0.join();
  T1.join();

  ASSERT_EQ(Counter.load(std::memory_order_acquire), 0);
}

} // namespace
} // namespace __xray
