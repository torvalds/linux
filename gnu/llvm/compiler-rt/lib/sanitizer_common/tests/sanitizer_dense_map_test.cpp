//===- sanitizer_dense_map_test.cpp -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_dense_map.h"

#include <initializer_list>
#include <map>
#include <set>

#include "gtest/gtest.h"

using namespace __sanitizer;

namespace {

// Helps to keep some tests.
template <typename KeyT, typename ValueT,
          typename KeyInfoT = DenseMapInfo<KeyT>>
class TestDenseMap : public DenseMap<KeyT, ValueT, KeyInfoT> {
  using BaseT = DenseMap<KeyT, ValueT, KeyInfoT>;

 public:
  using BaseT::BaseT;

  TestDenseMap(std::initializer_list<typename BaseT::value_type> Vals)
      : BaseT(Vals.size()) {
    for (const auto &V : Vals) this->BaseT::insert(V);
  }

  template <typename I>
  TestDenseMap(I B, I E) : BaseT(std::distance(B, E)) {
    for (; B != E; ++B) this->BaseT::insert(*B);
  }
};

template <typename... T>
using DenseMap = TestDenseMap<T...>;

uint32_t getTestKey(int i, uint32_t *) { return i; }
uint32_t getTestValue(int i, uint32_t *) { return 42 + i; }

uint32_t *getTestKey(int i, uint32_t **) {
  static uint32_t dummy_arr1[8192];
  assert(i < 8192 && "Only support 8192 dummy keys.");
  return &dummy_arr1[i];
}
uint32_t *getTestValue(int i, uint32_t **) {
  static uint32_t dummy_arr1[8192];
  assert(i < 8192 && "Only support 8192 dummy keys.");
  return &dummy_arr1[i];
}

/// A test class that tries to check that construction and destruction
/// occur correctly.
class CtorTester {
  static std::set<CtorTester *> Constructed;
  int Value;

 public:
  explicit CtorTester(int Value = 0) : Value(Value) {
    EXPECT_TRUE(Constructed.insert(this).second);
  }
  CtorTester(uint32_t Value) : Value(Value) {
    EXPECT_TRUE(Constructed.insert(this).second);
  }
  CtorTester(const CtorTester &Arg) : Value(Arg.Value) {
    EXPECT_TRUE(Constructed.insert(this).second);
  }
  CtorTester &operator=(const CtorTester &) = default;
  ~CtorTester() { EXPECT_EQ(1u, Constructed.erase(this)); }
  operator uint32_t() const { return Value; }

  int getValue() const { return Value; }
  bool operator==(const CtorTester &RHS) const { return Value == RHS.Value; }
};

std::set<CtorTester *> CtorTester::Constructed;

struct CtorTesterMapInfo {
  static inline CtorTester getEmptyKey() { return CtorTester(-1); }
  static inline CtorTester getTombstoneKey() { return CtorTester(-2); }
  static unsigned getHashValue(const CtorTester &Val) {
    return Val.getValue() * 37u;
  }
  static bool isEqual(const CtorTester &LHS, const CtorTester &RHS) {
    return LHS == RHS;
  }
};

CtorTester getTestKey(int i, CtorTester *) { return CtorTester(i); }
CtorTester getTestValue(int i, CtorTester *) { return CtorTester(42 + i); }

// Test fixture, with helper functions implemented by forwarding to global
// function overloads selected by component types of the type parameter. This
// allows all of the map implementations to be tested with shared
// implementations of helper routines.
template <typename T>
class DenseMapTest : public ::testing::Test {
 protected:
  T Map;

  static typename T::key_type *const dummy_key_ptr;
  static typename T::mapped_type *const dummy_value_ptr;

  typename T::key_type getKey(int i = 0) {
    return getTestKey(i, dummy_key_ptr);
  }
  typename T::mapped_type getValue(int i = 0) {
    return getTestValue(i, dummy_value_ptr);
  }
};

template <typename T>
typename T::key_type *const DenseMapTest<T>::dummy_key_ptr = nullptr;
template <typename T>
typename T::mapped_type *const DenseMapTest<T>::dummy_value_ptr = nullptr;

// Register these types for testing.
typedef ::testing::Types<DenseMap<uint32_t, uint32_t>,
                         DenseMap<uint32_t *, uint32_t *>,
                         DenseMap<CtorTester, CtorTester, CtorTesterMapInfo>>
    DenseMapTestTypes;
TYPED_TEST_SUITE(DenseMapTest, DenseMapTestTypes, );

// Empty map tests
TYPED_TEST(DenseMapTest, EmptyIntMapTest) {
  // Size tests
  EXPECT_EQ(0u, this->Map.size());
  EXPECT_TRUE(this->Map.empty());

  // Lookup tests
  EXPECT_FALSE(this->Map.count(this->getKey()));
  EXPECT_EQ(nullptr, this->Map.find(this->getKey()));
  EXPECT_EQ(typename TypeParam::mapped_type(),
            this->Map.lookup(this->getKey()));
}

// Constant map tests
TYPED_TEST(DenseMapTest, ConstEmptyMapTest) {
  const TypeParam &ConstMap = this->Map;
  EXPECT_EQ(0u, ConstMap.size());
  EXPECT_TRUE(ConstMap.empty());
}

// A map with a single entry
TYPED_TEST(DenseMapTest, SingleEntryMapTest) {
  this->Map[this->getKey()] = this->getValue();

  // Size tests
  EXPECT_EQ(1u, this->Map.size());
  EXPECT_FALSE(this->Map.empty());

  // Lookup tests
  EXPECT_TRUE(this->Map.count(this->getKey()));
  EXPECT_NE(nullptr, this->Map.find(this->getKey()));
  EXPECT_EQ(this->getValue(), this->Map.lookup(this->getKey()));
  EXPECT_EQ(this->getValue(), this->Map[this->getKey()]);
}

// Test clear() method
TYPED_TEST(DenseMapTest, ClearTest) {
  this->Map[this->getKey()] = this->getValue();
  this->Map.clear();

  EXPECT_EQ(0u, this->Map.size());
  EXPECT_TRUE(this->Map.empty());
}

// Test erase(iterator) method
TYPED_TEST(DenseMapTest, EraseTest) {
  this->Map[this->getKey()] = this->getValue();
  this->Map.erase(this->Map.find(this->getKey()));

  EXPECT_EQ(0u, this->Map.size());
  EXPECT_TRUE(this->Map.empty());
}

// Test erase(value) method
TYPED_TEST(DenseMapTest, EraseTest2) {
  this->Map[this->getKey()] = this->getValue();
  this->Map.erase(this->getKey());

  EXPECT_EQ(0u, this->Map.size());
  EXPECT_TRUE(this->Map.empty());
}

// Test insert() method
TYPED_TEST(DenseMapTest, InsertTest) {
  this->Map.insert(
      typename TypeParam::value_type(this->getKey(), this->getValue()));
  EXPECT_EQ(1u, this->Map.size());
  EXPECT_EQ(this->getValue(), this->Map[this->getKey()]);
}

// Test copy constructor method
TYPED_TEST(DenseMapTest, CopyConstructorTest) {
  this->Map[this->getKey()] = this->getValue();
  TypeParam copyMap(this->Map);

  EXPECT_EQ(1u, copyMap.size());
  EXPECT_EQ(this->getValue(), copyMap[this->getKey()]);
}

// Test copy constructor method where SmallDenseMap isn't small.
TYPED_TEST(DenseMapTest, CopyConstructorNotSmallTest) {
  for (int Key = 0; Key < 5; ++Key)
    this->Map[this->getKey(Key)] = this->getValue(Key);
  TypeParam copyMap(this->Map);

  EXPECT_EQ(5u, copyMap.size());
  for (int Key = 0; Key < 5; ++Key)
    EXPECT_EQ(this->getValue(Key), copyMap[this->getKey(Key)]);
}

// Test copying from a default-constructed map.
TYPED_TEST(DenseMapTest, CopyConstructorFromDefaultTest) {
  TypeParam copyMap(this->Map);

  EXPECT_TRUE(copyMap.empty());
}

// Test copying from an empty map where SmallDenseMap isn't small.
TYPED_TEST(DenseMapTest, CopyConstructorFromEmptyTest) {
  for (int Key = 0; Key < 5; ++Key)
    this->Map[this->getKey(Key)] = this->getValue(Key);
  this->Map.clear();
  TypeParam copyMap(this->Map);

  EXPECT_TRUE(copyMap.empty());
}

// Test assignment operator method
TYPED_TEST(DenseMapTest, AssignmentTest) {
  this->Map[this->getKey()] = this->getValue();
  TypeParam copyMap = this->Map;

  EXPECT_EQ(1u, copyMap.size());
  EXPECT_EQ(this->getValue(), copyMap[this->getKey()]);

  // test self-assignment.
  copyMap = static_cast<TypeParam &>(copyMap);
  EXPECT_EQ(1u, copyMap.size());
  EXPECT_EQ(this->getValue(), copyMap[this->getKey()]);
}

TYPED_TEST(DenseMapTest, AssignmentTestNotSmall) {
  for (int Key = 0; Key < 5; ++Key)
    this->Map[this->getKey(Key)] = this->getValue(Key);
  TypeParam copyMap = this->Map;

  EXPECT_EQ(5u, copyMap.size());
  for (int Key = 0; Key < 5; ++Key)
    EXPECT_EQ(this->getValue(Key), copyMap[this->getKey(Key)]);

  // test self-assignment.
  copyMap = static_cast<TypeParam &>(copyMap);
  EXPECT_EQ(5u, copyMap.size());
  for (int Key = 0; Key < 5; ++Key)
    EXPECT_EQ(this->getValue(Key), copyMap[this->getKey(Key)]);
}

// Test swap method
TYPED_TEST(DenseMapTest, SwapTest) {
  this->Map[this->getKey()] = this->getValue();
  TypeParam otherMap;

  this->Map.swap(otherMap);
  EXPECT_EQ(0u, this->Map.size());
  EXPECT_TRUE(this->Map.empty());
  EXPECT_EQ(1u, otherMap.size());
  EXPECT_EQ(this->getValue(), otherMap[this->getKey()]);

  this->Map.swap(otherMap);
  EXPECT_EQ(0u, otherMap.size());
  EXPECT_TRUE(otherMap.empty());
  EXPECT_EQ(1u, this->Map.size());
  EXPECT_EQ(this->getValue(), this->Map[this->getKey()]);

  // Make this more interesting by inserting 100 numbers into the map.
  for (int i = 0; i < 100; ++i) this->Map[this->getKey(i)] = this->getValue(i);

  this->Map.swap(otherMap);
  EXPECT_EQ(0u, this->Map.size());
  EXPECT_TRUE(this->Map.empty());
  EXPECT_EQ(100u, otherMap.size());
  for (int i = 0; i < 100; ++i)
    EXPECT_EQ(this->getValue(i), otherMap[this->getKey(i)]);

  this->Map.swap(otherMap);
  EXPECT_EQ(0u, otherMap.size());
  EXPECT_TRUE(otherMap.empty());
  EXPECT_EQ(100u, this->Map.size());
  for (int i = 0; i < 100; ++i)
    EXPECT_EQ(this->getValue(i), this->Map[this->getKey(i)]);
}

// A more complex iteration test
TYPED_TEST(DenseMapTest, IterationTest) {
  int visited[100];
  std::map<typename TypeParam::key_type, unsigned> visitedIndex;

  // Insert 100 numbers into the map
  for (int i = 0; i < 100; ++i) {
    visited[i] = 0;
    visitedIndex[this->getKey(i)] = i;

    this->Map[this->getKey(i)] = this->getValue(i);
  }

  // Iterate over all numbers and mark each one found.
  this->Map.forEach([&](const typename TypeParam::value_type &kv) {
    ++visited[visitedIndex[kv.first]];
    return true;
  });

  // Ensure every number was visited.
  for (int i = 0; i < 100; ++i) ASSERT_EQ(1, visited[i]);
}

namespace {
// Simple class that counts how many moves and copy happens when growing a map
struct CountCopyAndMove {
  static int Move;
  static int Copy;
  CountCopyAndMove() {}

  CountCopyAndMove(const CountCopyAndMove &) { Copy++; }
  CountCopyAndMove &operator=(const CountCopyAndMove &) {
    Copy++;
    return *this;
  }
  CountCopyAndMove(CountCopyAndMove &&) { Move++; }
  CountCopyAndMove &operator=(const CountCopyAndMove &&) {
    Move++;
    return *this;
  }
};
int CountCopyAndMove::Copy = 0;
int CountCopyAndMove::Move = 0;

}  // anonymous namespace

// Test initializer list construction.
TEST(DenseMapCustomTest, InitializerList) {
  DenseMap<int, int> M({{0, 0}, {0, 1}, {1, 2}});
  EXPECT_EQ(2u, M.size());
  EXPECT_EQ(1u, M.count(0));
  EXPECT_EQ(0, M[0]);
  EXPECT_EQ(1u, M.count(1));
  EXPECT_EQ(2, M[1]);
}

// Test initializer list construction.
TEST(DenseMapCustomTest, EqualityComparison) {
  DenseMap<int, int> M1({{0, 0}, {1, 2}});
  DenseMap<int, int> M2({{0, 0}, {1, 2}});
  DenseMap<int, int> M3({{0, 0}, {1, 3}});

  EXPECT_EQ(M1, M2);
  EXPECT_NE(M1, M3);
}

const int ExpectedInitialBucketCount = GetPageSizeCached() / /* sizeof(KV) */ 8;

// Test for the default minimum size of a DenseMap
TEST(DenseMapCustomTest, DefaultMinReservedSizeTest) {
  // Formula from DenseMap::getMinBucketToReserveForEntries()
  const int ExpectedMaxInitialEntries = ExpectedInitialBucketCount * 3 / 4 - 1;

  DenseMap<int, CountCopyAndMove> Map;
  // Will allocate 64 buckets
  Map.reserve(1);
  unsigned MemorySize = Map.getMemorySize();
  CountCopyAndMove::Copy = 0;
  CountCopyAndMove::Move = 0;
  for (int i = 0; i < ExpectedMaxInitialEntries; ++i) {
    detail::DenseMapPair<int, CountCopyAndMove> KV;
    KV.first = i;
    Map.insert(move(KV));
  }
  // Check that we didn't grow
  EXPECT_EQ(MemorySize, Map.getMemorySize());
  // Check that move was called the expected number of times
  EXPECT_EQ(ExpectedMaxInitialEntries, CountCopyAndMove::Move);
  // Check that no copy occurred
  EXPECT_EQ(0, CountCopyAndMove::Copy);

  // Adding one extra element should grow the map
  detail::DenseMapPair<int, CountCopyAndMove> KV;
  KV.first = ExpectedMaxInitialEntries;
  Map.insert(move(KV));
  // Check that we grew
  EXPECT_NE(MemorySize, Map.getMemorySize());
  // Check that move was called the expected number of times
  //  This relies on move-construction elision, and cannot be reliably tested.
  //   EXPECT_EQ(ExpectedMaxInitialEntries + 2, CountCopyAndMove::Move);
  // Check that no copy occurred
  EXPECT_EQ(0, CountCopyAndMove::Copy);
}

// Make sure creating the map with an initial size of N actually gives us enough
// buckets to insert N items without increasing allocation size.
TEST(DenseMapCustomTest, InitialSizeTest) {
  // Test a few different size, 341 is *not* a random choice: we need a value
  // that is 2/3 of a power of two to stress the grow() condition, and the power
  // of two has to be at least 512 because of minimum size allocation in the
  // DenseMap (see DefaultMinReservedSizeTest).
  for (auto Size : {1, 2, 48, 66, 341, ExpectedInitialBucketCount + 1}) {
    DenseMap<int, CountCopyAndMove> Map(Size);
    unsigned MemorySize = Map.getMemorySize();
    CountCopyAndMove::Copy = 0;
    CountCopyAndMove::Move = 0;
    for (int i = 0; i < Size; ++i) {
      detail::DenseMapPair<int, CountCopyAndMove> KV;
      KV.first = i;
      Map.insert(move(KV));
    }
    // Check that we didn't grow
    EXPECT_EQ(MemorySize, Map.getMemorySize());
    // Check that move was called the expected number of times
    EXPECT_EQ(Size, CountCopyAndMove::Move);
    // Check that no copy occurred
    EXPECT_EQ(0, CountCopyAndMove::Copy);
  }
}

// Make sure creating the map with a iterator range does not trigger grow()
TEST(DenseMapCustomTest, InitFromIterator) {
  std::vector<detail::DenseMapPair<int, CountCopyAndMove>> Values;
  // The size is a random value greater than 64 (hardcoded DenseMap min init)
  const int Count = 65;
  for (int i = 0; i < Count; i++) Values.emplace_back(i, CountCopyAndMove());

  CountCopyAndMove::Move = 0;
  CountCopyAndMove::Copy = 0;
  DenseMap<int, CountCopyAndMove> Map(Values.begin(), Values.end());
  // Check that no move occurred
  EXPECT_EQ(0, CountCopyAndMove::Move);
  // Check that copy was called the expected number of times
  EXPECT_EQ(Count, CountCopyAndMove::Copy);
}

// Make sure reserve actually gives us enough buckets to insert N items
// without increasing allocation size.
TEST(DenseMapCustomTest, ReserveTest) {
  // Test a few different size, 341 is *not* a random choice: we need a value
  // that is 2/3 of a power of two to stress the grow() condition, and the power
  // of two has to be at least 512 because of minimum size allocation in the
  // DenseMap (see DefaultMinReservedSizeTest).
  for (auto Size : {1, 2, 48, 66, 341, ExpectedInitialBucketCount + 1}) {
    DenseMap<int, CountCopyAndMove> Map;
    Map.reserve(Size);
    unsigned MemorySize = Map.getMemorySize();
    CountCopyAndMove::Copy = 0;
    CountCopyAndMove::Move = 0;
    for (int i = 0; i < Size; ++i) {
      detail::DenseMapPair<int, CountCopyAndMove> KV;
      KV.first = i;
      Map.insert(move(KV));
    }
    // Check that we didn't grow
    EXPECT_EQ(MemorySize, Map.getMemorySize());
    // Check that move was called the expected number of times
    EXPECT_EQ(Size, CountCopyAndMove::Move);
    // Check that no copy occurred
    EXPECT_EQ(0, CountCopyAndMove::Copy);
  }
}

// Key traits that allows lookup with either an unsigned or char* key;
// In the latter case, "a" == 0, "b" == 1 and so on.
struct TestDenseMapInfo {
  static inline unsigned getEmptyKey() { return ~0; }
  static inline unsigned getTombstoneKey() { return ~0U - 1; }
  static unsigned getHashValue(const unsigned &Val) { return Val * 37U; }
  static unsigned getHashValue(const char *Val) {
    return (unsigned)(Val[0] - 'a') * 37U;
  }
  static bool isEqual(const unsigned &LHS, const unsigned &RHS) {
    return LHS == RHS;
  }
  static bool isEqual(const char *LHS, const unsigned &RHS) {
    return (unsigned)(LHS[0] - 'a') == RHS;
  }
};

// find_as() tests
TEST(DenseMapCustomTest, FindAsTest) {
  DenseMap<unsigned, unsigned, TestDenseMapInfo> map;
  map[0] = 1;
  map[1] = 2;
  map[2] = 3;

  // Size tests
  EXPECT_EQ(3u, map.size());

  // Normal lookup tests
  EXPECT_EQ(1u, map.count(1));
  EXPECT_EQ(1u, map.find(0)->second);
  EXPECT_EQ(2u, map.find(1)->second);
  EXPECT_EQ(3u, map.find(2)->second);
  EXPECT_EQ(nullptr, map.find(3));

  // find_as() tests
  EXPECT_EQ(1u, map.find_as("a")->second);
  EXPECT_EQ(2u, map.find_as("b")->second);
  EXPECT_EQ(3u, map.find_as("c")->second);
  EXPECT_EQ(nullptr, map.find_as("d"));
}

TEST(DenseMapCustomTest, TryEmplaceTest) {
  DenseMap<int, std::unique_ptr<int>> Map;
  std::unique_ptr<int> P(new int(2));
  auto Try1 = Map.try_emplace(0, new int(1));
  EXPECT_TRUE(Try1.second);
  auto Try2 = Map.try_emplace(0, std::move(P));
  EXPECT_FALSE(Try2.second);
  EXPECT_EQ(Try1.first, Try2.first);
  EXPECT_NE(nullptr, P);
}

struct IncompleteStruct;

TEST(DenseMapCustomTest, OpaquePointerKey) {
  // Test that we can use a pointer to an incomplete type as a DenseMap key.
  // This is an important build time optimization, since many classes have
  // DenseMap members.
  DenseMap<IncompleteStruct *, int> Map;
  int Keys[3] = {0, 0, 0};
  IncompleteStruct *K1 = reinterpret_cast<IncompleteStruct *>(&Keys[0]);
  IncompleteStruct *K2 = reinterpret_cast<IncompleteStruct *>(&Keys[1]);
  IncompleteStruct *K3 = reinterpret_cast<IncompleteStruct *>(&Keys[2]);
  Map.insert({K1, 1});
  Map.insert({K2, 2});
  Map.insert({K3, 3});
  EXPECT_EQ(Map.count(K1), 1u);
  EXPECT_EQ(Map[K1], 1);
  EXPECT_EQ(Map[K2], 2);
  EXPECT_EQ(Map[K3], 3);
  Map.clear();
  EXPECT_EQ(nullptr, Map.find(K1));
  EXPECT_EQ(nullptr, Map.find(K2));
  EXPECT_EQ(nullptr, Map.find(K3));
}
}  // namespace
