//===-- function_call_trie_test.cpp ---------------------------------------===//
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
#include "xray_function_call_trie.h"
#include "gtest/gtest.h"
#include <cstdint>

namespace __xray {

namespace {

TEST(FunctionCallTrieTest, ConstructWithTLSAllocators) {
  profilingFlags()->setDefaults();
  FunctionCallTrie::Allocators Allocators = FunctionCallTrie::InitAllocators();
  FunctionCallTrie Trie(Allocators);
}

TEST(FunctionCallTrieTest, EnterAndExitFunction) {
  profilingFlags()->setDefaults();
  auto A = FunctionCallTrie::InitAllocators();
  FunctionCallTrie Trie(A);

  uint64_t TSC = 1;
  uint16_t CPU = 0;
  Trie.enterFunction(1, TSC++, CPU++);
  Trie.exitFunction(1, TSC++, CPU++);
  const auto &R = Trie.getRoots();

  ASSERT_EQ(R.size(), 1u);
  ASSERT_EQ(R.front()->FId, 1);
  ASSERT_EQ(R.front()->CallCount, 1u);
  ASSERT_EQ(R.front()->CumulativeLocalTime, 1u);
}

TEST(FunctionCallTrieTest, HandleTSCOverflow) {
  profilingFlags()->setDefaults();
  auto A = FunctionCallTrie::InitAllocators();
  FunctionCallTrie Trie(A);

  Trie.enterFunction(1, std::numeric_limits<uint64_t>::max(), 0);
  Trie.exitFunction(1, 1, 0);
  const auto &R = Trie.getRoots();

  ASSERT_EQ(R.size(), 1u);
  ASSERT_EQ(R.front()->FId, 1);
  ASSERT_EQ(R.front()->CallCount, 1u);
  ASSERT_EQ(R.front()->CumulativeLocalTime, 1u);
}

TEST(FunctionCallTrieTest, MaximalCumulativeTime) {
  profilingFlags()->setDefaults();
  auto A = FunctionCallTrie::InitAllocators();
  FunctionCallTrie Trie(A);

  Trie.enterFunction(1, 1, 0);
  Trie.exitFunction(1, 0, 0);
  const auto &R = Trie.getRoots();

  ASSERT_EQ(R.size(), 1u);
  ASSERT_EQ(R.front()->FId, 1);
  ASSERT_EQ(R.front()->CallCount, 1u);
  ASSERT_EQ(R.front()->CumulativeLocalTime,
            std::numeric_limits<uint64_t>::max() - 1);
}

TEST(FunctionCallTrieTest, MissingFunctionEntry) {
  profilingFlags()->setDefaults();
  auto A = FunctionCallTrie::InitAllocators();
  FunctionCallTrie Trie(A);
  Trie.exitFunction(1, 1, 0);
  const auto &R = Trie.getRoots();

  ASSERT_TRUE(R.empty());
}

TEST(FunctionCallTrieTest, NoMatchingEntersForExit) {
  profilingFlags()->setDefaults();
  auto A = FunctionCallTrie::InitAllocators();
  FunctionCallTrie Trie(A);
  Trie.enterFunction(2, 1, 0);
  Trie.enterFunction(3, 3, 0);
  Trie.exitFunction(1, 5, 0);
  const auto &R = Trie.getRoots();

  ASSERT_FALSE(R.empty());
  EXPECT_EQ(R.size(), size_t{1});
}

TEST(FunctionCallTrieTest, MissingFunctionExit) {
  profilingFlags()->setDefaults();
  auto A = FunctionCallTrie::InitAllocators();
  FunctionCallTrie Trie(A);
  Trie.enterFunction(1, 1, 0);
  const auto &R = Trie.getRoots();

  ASSERT_FALSE(R.empty());
  EXPECT_EQ(R.size(), size_t{1});
}

TEST(FunctionCallTrieTest, MultipleRoots) {
  profilingFlags()->setDefaults();
  auto A = FunctionCallTrie::InitAllocators();
  FunctionCallTrie Trie(A);

  // Enter and exit FId = 1.
  Trie.enterFunction(1, 1, 0);
  Trie.exitFunction(1, 2, 0);

  // Enter and exit FId = 2.
  Trie.enterFunction(2, 3, 0);
  Trie.exitFunction(2, 4, 0);

  const auto &R = Trie.getRoots();
  ASSERT_FALSE(R.empty());
  ASSERT_EQ(R.size(), 2u);

  // Make sure the roots have different IDs.
  const auto R0 = R[0];
  const auto R1 = R[1];
  ASSERT_NE(R0->FId, R1->FId);

  // Inspect the roots that they have the right data.
  ASSERT_NE(R0, nullptr);
  EXPECT_EQ(R0->CallCount, 1u);
  EXPECT_EQ(R0->CumulativeLocalTime, 1u);

  ASSERT_NE(R1, nullptr);
  EXPECT_EQ(R1->CallCount, 1u);
  EXPECT_EQ(R1->CumulativeLocalTime, 1u);
}

// While missing an intermediary entry may be rare in practice, we still enforce
// that we can handle the case where we've missed the entry event somehow, in
// between call entry/exits. To illustrate, imagine the following shadow call
// stack:
//
//   f0@t0 -> f1@t1 -> f2@t2
//
// If for whatever reason we see an exit for `f2` @ t3, followed by an exit for
// `f0` @ t4 (i.e. no `f1` exit in between) then we need to handle the case of
// accounting local time to `f2` from d = (t3 - t2), then local time to `f1`
// as d' = (t3 - t1) - d, and then local time to `f0` as d'' = (t3 - t0) - d'.
TEST(FunctionCallTrieTest, MissingIntermediaryExit) {
  profilingFlags()->setDefaults();
  auto A = FunctionCallTrie::InitAllocators();
  FunctionCallTrie Trie(A);

  Trie.enterFunction(1, 0, 0);
  Trie.enterFunction(2, 100, 0);
  Trie.enterFunction(3, 200, 0);
  Trie.exitFunction(3, 300, 0);
  Trie.exitFunction(1, 400, 0);

  // What we should see at this point is all the functions in the trie in a
  // specific order (1 -> 2 -> 3) with the appropriate count(s) and local
  // latencies.
  const auto &R = Trie.getRoots();
  ASSERT_FALSE(R.empty());
  ASSERT_EQ(R.size(), 1u);

  const auto &F1 = *R[0];
  ASSERT_EQ(F1.FId, 1);
  ASSERT_FALSE(F1.Callees.empty());

  const auto &F2 = *F1.Callees[0].NodePtr;
  ASSERT_EQ(F2.FId, 2);
  ASSERT_FALSE(F2.Callees.empty());

  const auto &F3 = *F2.Callees[0].NodePtr;
  ASSERT_EQ(F3.FId, 3);
  ASSERT_TRUE(F3.Callees.empty());

  // Now that we've established the preconditions, we check for specific aspects
  // of the nodes.
  EXPECT_EQ(F3.CallCount, 1u);
  EXPECT_EQ(F2.CallCount, 1u);
  EXPECT_EQ(F1.CallCount, 1u);
  EXPECT_EQ(F3.CumulativeLocalTime, 100u);
  EXPECT_EQ(F2.CumulativeLocalTime, 300u);
  EXPECT_EQ(F1.CumulativeLocalTime, 100u);
}

TEST(FunctionCallTrieTest, DeepCallStack) {
  // Simulate a relatively deep call stack (32 levels) and ensure that we can
  // properly pop all the way up the stack.
  profilingFlags()->setDefaults();
  auto A = FunctionCallTrie::InitAllocators();
  FunctionCallTrie Trie(A);
  for (int i = 0; i < 32; ++i)
    Trie.enterFunction(i + 1, i, 0);
  Trie.exitFunction(1, 33, 0);

  // Here, validate that we have a 32-level deep function call path from the
  // root (1) down to the leaf (33).
  const auto &R = Trie.getRoots();
  ASSERT_EQ(R.size(), 1u);
  auto F = R[0];
  for (int i = 0; i < 32; ++i) {
    EXPECT_EQ(F->FId, i + 1);
    EXPECT_EQ(F->CallCount, 1u);
    if (F->Callees.empty() && i != 31)
      FAIL() << "Empty callees for FId " << F->FId;
    if (i != 31)
      F = F->Callees[0].NodePtr;
  }
}

// TODO: Test that we can handle cross-CPU migrations, where TSCs are not
// guaranteed to be synchronised.
TEST(FunctionCallTrieTest, DeepCopy) {
  profilingFlags()->setDefaults();
  auto A = FunctionCallTrie::InitAllocators();
  FunctionCallTrie Trie(A);

  Trie.enterFunction(1, 0, 0);
  Trie.enterFunction(2, 1, 0);
  Trie.exitFunction(2, 2, 0);
  Trie.enterFunction(3, 3, 0);
  Trie.exitFunction(3, 4, 0);
  Trie.exitFunction(1, 5, 0);

  // We want to make a deep copy and compare notes.
  auto B = FunctionCallTrie::InitAllocators();
  FunctionCallTrie Copy(B);
  Trie.deepCopyInto(Copy);

  ASSERT_NE(Trie.getRoots().size(), 0u);
  ASSERT_EQ(Trie.getRoots().size(), Copy.getRoots().size());
  const auto &R0Orig = *Trie.getRoots()[0];
  const auto &R0Copy = *Copy.getRoots()[0];
  EXPECT_EQ(R0Orig.FId, 1);
  EXPECT_EQ(R0Orig.FId, R0Copy.FId);

  ASSERT_EQ(R0Orig.Callees.size(), 2u);
  ASSERT_EQ(R0Copy.Callees.size(), 2u);

  const auto &F1Orig =
      *R0Orig.Callees
           .find_element(
               [](const FunctionCallTrie::NodeIdPair &R) { return R.FId == 2; })
           ->NodePtr;
  const auto &F1Copy =
      *R0Copy.Callees
           .find_element(
               [](const FunctionCallTrie::NodeIdPair &R) { return R.FId == 2; })
           ->NodePtr;
  EXPECT_EQ(&R0Orig, F1Orig.Parent);
  EXPECT_EQ(&R0Copy, F1Copy.Parent);
}

TEST(FunctionCallTrieTest, MergeInto) {
  profilingFlags()->setDefaults();
  auto A = FunctionCallTrie::InitAllocators();
  FunctionCallTrie T0(A);
  FunctionCallTrie T1(A);

  // 1 -> 2 -> 3
  T0.enterFunction(1, 0, 0);
  T0.enterFunction(2, 1, 0);
  T0.enterFunction(3, 2, 0);
  T0.exitFunction(3, 3, 0);
  T0.exitFunction(2, 4, 0);
  T0.exitFunction(1, 5, 0);

  // 1 -> 2 -> 3
  T1.enterFunction(1, 0, 0);
  T1.enterFunction(2, 1, 0);
  T1.enterFunction(3, 2, 0);
  T1.exitFunction(3, 3, 0);
  T1.exitFunction(2, 4, 0);
  T1.exitFunction(1, 5, 0);

  // We use a different allocator here to make sure that we're able to transfer
  // data into a FunctionCallTrie which uses a different allocator. This
  // reflects the intended usage scenario for when we're collecting profiles
  // that aggregate across threads.
  auto B = FunctionCallTrie::InitAllocators();
  FunctionCallTrie Merged(B);

  T0.mergeInto(Merged);
  T1.mergeInto(Merged);

  ASSERT_EQ(Merged.getRoots().size(), 1u);
  const auto &R0 = *Merged.getRoots()[0];
  EXPECT_EQ(R0.FId, 1);
  EXPECT_EQ(R0.CallCount, 2u);
  EXPECT_EQ(R0.CumulativeLocalTime, 10u);
  EXPECT_EQ(R0.Callees.size(), 1u);

  const auto &F1 = *R0.Callees[0].NodePtr;
  EXPECT_EQ(F1.FId, 2);
  EXPECT_EQ(F1.CallCount, 2u);
  EXPECT_EQ(F1.CumulativeLocalTime, 6u);
  EXPECT_EQ(F1.Callees.size(), 1u);

  const auto &F2 = *F1.Callees[0].NodePtr;
  EXPECT_EQ(F2.FId, 3);
  EXPECT_EQ(F2.CallCount, 2u);
  EXPECT_EQ(F2.CumulativeLocalTime, 2u);
  EXPECT_EQ(F2.Callees.size(), 0u);
}

TEST(FunctionCallTrieTest, PlacementNewOnAlignedStorage) {
  profilingFlags()->setDefaults();
  alignas(FunctionCallTrie::Allocators)
      std::byte AllocatorsStorage[sizeof(FunctionCallTrie::Allocators)];
  new (&AllocatorsStorage)
      FunctionCallTrie::Allocators(FunctionCallTrie::InitAllocators());
  auto *A =
      reinterpret_cast<FunctionCallTrie::Allocators *>(&AllocatorsStorage);

  alignas(FunctionCallTrie) std::byte FCTStorage[sizeof(FunctionCallTrie)];
  new (&FCTStorage) FunctionCallTrie(*A);
  auto *T = reinterpret_cast<FunctionCallTrie *>(&FCTStorage);

  // Put some data into it.
  T->enterFunction(1, 0, 0);
  T->exitFunction(1, 1, 0);

  // Re-initialize the objects in storage.
  T->~FunctionCallTrie();
  A->~Allocators();
  new (A) FunctionCallTrie::Allocators(FunctionCallTrie::InitAllocators());
  new (T) FunctionCallTrie(*A);

  // Then put some data into it again.
  T->enterFunction(1, 0, 0);
  T->exitFunction(1, 1, 0);
}

} // namespace

} // namespace __xray
