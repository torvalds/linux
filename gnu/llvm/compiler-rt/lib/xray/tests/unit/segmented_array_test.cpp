#include "test_helpers.h"
#include "xray_segmented_array.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <numeric>
#include <vector>

namespace __xray {
namespace {

using ::testing::SizeIs;

struct TestData {
  s64 First;
  s64 Second;

  // Need a constructor for emplace operations.
  TestData(s64 F, s64 S) : First(F), Second(S) {}
};

void PrintTo(const TestData &D, std::ostream *OS) {
  *OS << "{ " << D.First << ", " << D.Second << " }";
}

TEST(SegmentedArrayTest, ConstructWithAllocators) {
  using AllocatorType = typename Array<TestData>::AllocatorType;
  AllocatorType A(1 << 4);
  Array<TestData> Data(A);
  (void)Data;
}

TEST(SegmentedArrayTest, ConstructAndPopulate) {
  using AllocatorType = typename Array<TestData>::AllocatorType;
  AllocatorType A(1 << 4);
  Array<TestData> data(A);
  ASSERT_NE(data.Append(TestData{0, 0}), nullptr);
  ASSERT_NE(data.Append(TestData{1, 1}), nullptr);
  ASSERT_EQ(data.size(), 2u);
}

TEST(SegmentedArrayTest, ConstructPopulateAndLookup) {
  using AllocatorType = typename Array<TestData>::AllocatorType;
  AllocatorType A(1 << 4);
  Array<TestData> data(A);
  ASSERT_NE(data.Append(TestData{0, 1}), nullptr);
  ASSERT_EQ(data.size(), 1u);
  ASSERT_EQ(data[0].First, 0);
  ASSERT_EQ(data[0].Second, 1);
}

TEST(SegmentedArrayTest, PopulateWithMoreElements) {
  using AllocatorType = typename Array<TestData>::AllocatorType;
  AllocatorType A(1 << 24);
  Array<TestData> data(A);
  static const auto kMaxElements = 100u;
  for (auto I = 0u; I < kMaxElements; ++I) {
    ASSERT_NE(data.Append(TestData{I, I + 1}), nullptr);
  }
  ASSERT_EQ(data.size(), kMaxElements);
  for (auto I = 0u; I < kMaxElements; ++I) {
    ASSERT_EQ(data[I].First, I);
    ASSERT_EQ(data[I].Second, I + 1);
  }
}

TEST(SegmentedArrayTest, AppendEmplace) {
  using AllocatorType = typename Array<TestData>::AllocatorType;
  AllocatorType A(1 << 4);
  Array<TestData> data(A);
  ASSERT_NE(data.AppendEmplace(1, 1), nullptr);
  ASSERT_EQ(data[0].First, 1);
  ASSERT_EQ(data[0].Second, 1);
}

TEST(SegmentedArrayTest, AppendAndTrim) {
  using AllocatorType = typename Array<TestData>::AllocatorType;
  AllocatorType A(1 << 4);
  Array<TestData> data(A);
  ASSERT_NE(data.AppendEmplace(1, 1), nullptr);
  ASSERT_EQ(data.size(), 1u);
  data.trim(1);
  ASSERT_EQ(data.size(), 0u);
  ASSERT_TRUE(data.empty());
}

TEST(SegmentedArrayTest, IteratorAdvance) {
  using AllocatorType = typename Array<TestData>::AllocatorType;
  AllocatorType A(1 << 4);
  Array<TestData> data(A);
  ASSERT_TRUE(data.empty());
  ASSERT_EQ(data.begin(), data.end());
  auto I0 = data.begin();
  ASSERT_EQ(I0++, data.begin());
  ASSERT_NE(I0, data.begin());
  for (const auto &D : data) {
    (void)D;
    FAIL();
  }
  ASSERT_NE(data.AppendEmplace(1, 1), nullptr);
  ASSERT_EQ(data.size(), 1u);
  ASSERT_NE(data.begin(), data.end());
  auto &D0 = *data.begin();
  ASSERT_EQ(D0.First, 1);
  ASSERT_EQ(D0.Second, 1);
}

TEST(SegmentedArrayTest, IteratorRetreat) {
  using AllocatorType = typename Array<TestData>::AllocatorType;
  AllocatorType A(1 << 4);
  Array<TestData> data(A);
  ASSERT_TRUE(data.empty());
  ASSERT_EQ(data.begin(), data.end());
  ASSERT_NE(data.AppendEmplace(1, 1), nullptr);
  ASSERT_EQ(data.size(), 1u);
  ASSERT_NE(data.begin(), data.end());
  auto &D0 = *data.begin();
  ASSERT_EQ(D0.First, 1);
  ASSERT_EQ(D0.Second, 1);

  auto I0 = data.end();
  ASSERT_EQ(I0--, data.end());
  ASSERT_NE(I0, data.end());
  ASSERT_EQ(I0, data.begin());
  ASSERT_EQ(I0->First, 1);
  ASSERT_EQ(I0->Second, 1);
}

TEST(SegmentedArrayTest, IteratorTrimBehaviour) {
  using AllocatorType = typename Array<TestData>::AllocatorType;
  AllocatorType A(1 << 20);
  Array<TestData> Data(A);
  ASSERT_TRUE(Data.empty());
  auto I0Begin = Data.begin(), I0End = Data.end();
  // Add enough elements in Data to have more than one chunk.
  constexpr auto Segment = Array<TestData>::SegmentSize;
  constexpr auto SegmentX2 = Segment * 2;
  for (auto i = SegmentX2; i > 0u; --i) {
    Data.AppendEmplace(static_cast<s64>(i), static_cast<s64>(i));
  }
  ASSERT_EQ(Data.size(), SegmentX2);
  {
    auto &Back = Data.back();
    ASSERT_EQ(Back.First, 1);
    ASSERT_EQ(Back.Second, 1);
  }

  // Trim one chunk's elements worth.
  Data.trim(Segment);
  ASSERT_EQ(Data.size(), Segment);

  // Check that we are still able to access 'back' properly.
  {
    auto &Back = Data.back();
    ASSERT_EQ(Back.First, static_cast<s64>(Segment + 1));
    ASSERT_EQ(Back.Second, static_cast<s64>(Segment + 1));
  }

  // Then trim until it's empty.
  Data.trim(Segment);
  ASSERT_TRUE(Data.empty());

  // Here our iterators should be the same.
  auto I1Begin = Data.begin(), I1End = Data.end();
  EXPECT_EQ(I0Begin, I1Begin);
  EXPECT_EQ(I0End, I1End);

  // Then we ensure that adding elements back works just fine.
  for (auto i = SegmentX2; i > 0u; --i) {
    Data.AppendEmplace(static_cast<s64>(i), static_cast<s64>(i));
  }
  EXPECT_EQ(Data.size(), SegmentX2);
}

TEST(SegmentedArrayTest, HandleExhaustedAllocator) {
  using AllocatorType = typename Array<TestData>::AllocatorType;
  constexpr auto Segment = Array<TestData>::SegmentSize;
  constexpr auto MaxElements = Array<TestData>::ElementsPerSegment;
  AllocatorType A(Segment);
  Array<TestData> Data(A);
  for (auto i = MaxElements; i > 0u; --i)
    EXPECT_NE(Data.AppendEmplace(static_cast<s64>(i), static_cast<s64>(i)),
              nullptr);
  EXPECT_EQ(Data.AppendEmplace(0, 0), nullptr);
  EXPECT_THAT(Data, SizeIs(MaxElements));

  // Trimming more elements than there are in the container should be fine.
  Data.trim(MaxElements + 1);
  EXPECT_THAT(Data, SizeIs(0u));
}

struct ShadowStackEntry {
  uint64_t EntryTSC = 0;
  uint64_t *NodePtr = nullptr;
  ShadowStackEntry(uint64_t T, uint64_t *N) : EntryTSC(T), NodePtr(N) {}
};

TEST(SegmentedArrayTest, SimulateStackBehaviour) {
  using AllocatorType = typename Array<ShadowStackEntry>::AllocatorType;
  AllocatorType A(1 << 10);
  Array<ShadowStackEntry> Data(A);
  static uint64_t Dummy = 0;
  constexpr uint64_t Max = 9;

  for (uint64_t i = 0; i < Max; ++i) {
    auto P = Data.Append({i, &Dummy});
    ASSERT_NE(P, nullptr);
    ASSERT_EQ(P->NodePtr, &Dummy);
    auto &Back = Data.back();
    ASSERT_EQ(Back.NodePtr, &Dummy);
    ASSERT_EQ(Back.EntryTSC, i);
  }

  // Simulate a stack by checking the data from the end as we're trimming.
  auto Counter = Max;
  ASSERT_EQ(Data.size(), size_t(Max));
  while (!Data.empty()) {
    const auto &Top = Data.back();
    uint64_t *TopNode = Top.NodePtr;
    EXPECT_EQ(TopNode, &Dummy) << "Counter = " << Counter;
    Data.trim(1);
    --Counter;
    ASSERT_EQ(Data.size(), size_t(Counter));
  }
}

TEST(SegmentedArrayTest, PlacementNewOnAlignedStorage) {
  using AllocatorType = typename Array<ShadowStackEntry>::AllocatorType;
  alignas(AllocatorType) std::byte AllocatorStorage[sizeof(AllocatorType)];
  new (&AllocatorStorage) AllocatorType(1 << 10);
  auto *A = reinterpret_cast<AllocatorType *>(&AllocatorStorage);
  alignas(Array<ShadowStackEntry>)
      std::byte ArrayStorage[sizeof(Array<ShadowStackEntry>)];
  new (&ArrayStorage) Array<ShadowStackEntry>(*A);
  auto *Data = reinterpret_cast<Array<ShadowStackEntry> *>(&ArrayStorage);

  static uint64_t Dummy = 0;
  constexpr uint64_t Max = 9;

  for (uint64_t i = 0; i < Max; ++i) {
    auto P = Data->Append({i, &Dummy});
    ASSERT_NE(P, nullptr);
    ASSERT_EQ(P->NodePtr, &Dummy);
    auto &Back = Data->back();
    ASSERT_EQ(Back.NodePtr, &Dummy);
    ASSERT_EQ(Back.EntryTSC, i);
  }

  // Simulate a stack by checking the data from the end as we're trimming.
  auto Counter = Max;
  ASSERT_EQ(Data->size(), size_t(Max));
  while (!Data->empty()) {
    const auto &Top = Data->back();
    uint64_t *TopNode = Top.NodePtr;
    EXPECT_EQ(TopNode, &Dummy) << "Counter = " << Counter;
    Data->trim(1);
    --Counter;
    ASSERT_EQ(Data->size(), size_t(Counter));
  }

  // Once the stack is exhausted, we re-use the storage.
  for (uint64_t i = 0; i < Max; ++i) {
    auto P = Data->Append({i, &Dummy});
    ASSERT_NE(P, nullptr);
    ASSERT_EQ(P->NodePtr, &Dummy);
    auto &Back = Data->back();
    ASSERT_EQ(Back.NodePtr, &Dummy);
    ASSERT_EQ(Back.EntryTSC, i);
  }

  // We re-initialize the storage, by calling the destructor and
  // placement-new'ing again.
  Data->~Array();
  A->~AllocatorType();
  new (A) AllocatorType(1 << 10);
  new (Data) Array<ShadowStackEntry>(*A);

  // Then re-do the test.
  for (uint64_t i = 0; i < Max; ++i) {
    auto P = Data->Append({i, &Dummy});
    ASSERT_NE(P, nullptr);
    ASSERT_EQ(P->NodePtr, &Dummy);
    auto &Back = Data->back();
    ASSERT_EQ(Back.NodePtr, &Dummy);
    ASSERT_EQ(Back.EntryTSC, i);
  }

  // Simulate a stack by checking the data from the end as we're trimming.
  Counter = Max;
  ASSERT_EQ(Data->size(), size_t(Max));
  while (!Data->empty()) {
    const auto &Top = Data->back();
    uint64_t *TopNode = Top.NodePtr;
    EXPECT_EQ(TopNode, &Dummy) << "Counter = " << Counter;
    Data->trim(1);
    --Counter;
    ASSERT_EQ(Data->size(), size_t(Counter));
  }

  // Once the stack is exhausted, we re-use the storage.
  for (uint64_t i = 0; i < Max; ++i) {
    auto P = Data->Append({i, &Dummy});
    ASSERT_NE(P, nullptr);
    ASSERT_EQ(P->NodePtr, &Dummy);
    auto &Back = Data->back();
    ASSERT_EQ(Back.NodePtr, &Dummy);
    ASSERT_EQ(Back.EntryTSC, i);
  }
}

TEST(SegmentedArrayTest, ArrayOfPointersIteratorAccess) {
  using PtrArray = Array<int *>;
  PtrArray::AllocatorType Alloc(16384);
  Array<int *> A(Alloc);
  static constexpr size_t Count = 100;
  std::vector<int> Integers(Count);
  std::iota(Integers.begin(), Integers.end(), 0);
  for (auto &I : Integers)
    ASSERT_NE(A.Append(&I), nullptr);
  int V = 0;
  ASSERT_EQ(A.size(), Count);
  for (auto P : A) {
    ASSERT_NE(P, nullptr);
    ASSERT_EQ(*P, V++);
  }
}

TEST(SegmentedArrayTest, ArrayOfPointersIteratorAccessExhaustion) {
  using PtrArray = Array<int *>;
  PtrArray::AllocatorType Alloc(4096);
  Array<int *> A(Alloc);
  static constexpr size_t Count = 1000;
  std::vector<int> Integers(Count);
  std::iota(Integers.begin(), Integers.end(), 0);
  for (auto &I : Integers)
    if (A.Append(&I) == nullptr)
      break;
  int V = 0;
  ASSERT_LT(A.size(), Count);
  for (auto P : A) {
    ASSERT_NE(P, nullptr);
    ASSERT_EQ(*P, V++);
  }
}

} // namespace
} // namespace __xray
