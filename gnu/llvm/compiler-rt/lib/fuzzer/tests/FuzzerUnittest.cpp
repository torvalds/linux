// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Avoid ODR violations (LibFuzzer is built without ASan and this test is built
// with ASan) involving C++ standard library types when using libcxx.
#define _LIBCPP_HAS_NO_ASAN

// Do not attempt to use LLVM ostream etc from gtest.
#define GTEST_NO_LLVM_SUPPORT 1

#include "FuzzerCorpus.h"
#include "FuzzerDictionary.h"
#include "FuzzerInternal.h"
#include "FuzzerMerge.h"
#include "FuzzerMutate.h"
#include "FuzzerRandom.h"
#include "FuzzerTracePC.h"
#include "gtest/gtest.h"
#include <memory>
#include <set>
#include <sstream>

using namespace fuzzer;

// For now, have LLVMFuzzerTestOneInput just to make it link.
// Later we may want to make unittests that actually call
// LLVMFuzzerTestOneInput.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
  abort();
}

TEST(Fuzzer, Basename) {
  EXPECT_EQ(Basename("foo/bar"), "bar");
  EXPECT_EQ(Basename("bar"), "bar");
  EXPECT_EQ(Basename("/bar"), "bar");
  EXPECT_EQ(Basename("foo/x"), "x");
  EXPECT_EQ(Basename("foo/"), "");
#if LIBFUZZER_WINDOWS
  EXPECT_EQ(Basename("foo\\bar"), "bar");
  EXPECT_EQ(Basename("foo\\bar/baz"), "baz");
  EXPECT_EQ(Basename("\\bar"), "bar");
  EXPECT_EQ(Basename("foo\\x"), "x");
  EXPECT_EQ(Basename("foo\\"), "");
#endif
}

TEST(Fuzzer, CrossOver) {
  std::unique_ptr<ExternalFunctions> t(new ExternalFunctions());
  fuzzer::EF = t.get();
  Random Rand(0);
  std::unique_ptr<MutationDispatcher> MD(new MutationDispatcher(Rand, {}));
  Unit A({0, 1, 2}), B({5, 6, 7});
  Unit C;
  Unit Expected[] = {
       { 0 },
       { 0, 1 },
       { 0, 5 },
       { 0, 1, 2 },
       { 0, 1, 5 },
       { 0, 5, 1 },
       { 0, 5, 6 },
       { 0, 1, 2, 5 },
       { 0, 1, 5, 2 },
       { 0, 1, 5, 6 },
       { 0, 5, 1, 2 },
       { 0, 5, 1, 6 },
       { 0, 5, 6, 1 },
       { 0, 5, 6, 7 },
       { 0, 1, 2, 5, 6 },
       { 0, 1, 5, 2, 6 },
       { 0, 1, 5, 6, 2 },
       { 0, 1, 5, 6, 7 },
       { 0, 5, 1, 2, 6 },
       { 0, 5, 1, 6, 2 },
       { 0, 5, 1, 6, 7 },
       { 0, 5, 6, 1, 2 },
       { 0, 5, 6, 1, 7 },
       { 0, 5, 6, 7, 1 },
       { 0, 1, 2, 5, 6, 7 },
       { 0, 1, 5, 2, 6, 7 },
       { 0, 1, 5, 6, 2, 7 },
       { 0, 1, 5, 6, 7, 2 },
       { 0, 5, 1, 2, 6, 7 },
       { 0, 5, 1, 6, 2, 7 },
       { 0, 5, 1, 6, 7, 2 },
       { 0, 5, 6, 1, 2, 7 },
       { 0, 5, 6, 1, 7, 2 },
       { 0, 5, 6, 7, 1, 2 }
  };
  for (size_t Len = 1; Len < 8; Len++) {
    std::set<Unit> FoundUnits, ExpectedUnitsWitThisLength;
    for (int Iter = 0; Iter < 3000; Iter++) {
      C.resize(Len);
      size_t NewSize = MD->CrossOver(A.data(), A.size(), B.data(), B.size(),
                                     C.data(), C.size());
      C.resize(NewSize);
      FoundUnits.insert(C);
    }
    for (const Unit &U : Expected)
      if (U.size() <= Len)
        ExpectedUnitsWitThisLength.insert(U);
    EXPECT_EQ(ExpectedUnitsWitThisLength, FoundUnits);
  }
}

TEST(Fuzzer, Hash) {
  uint8_t A[] = {'a', 'b', 'c'};
  fuzzer::Unit U(A, A + sizeof(A));
  EXPECT_EQ("a9993e364706816aba3e25717850c26c9cd0d89d", fuzzer::Hash(U));
  U.push_back('d');
  EXPECT_EQ("81fe8bfe87576c3ecb22426f8e57847382917acf", fuzzer::Hash(U));
}

typedef size_t (MutationDispatcher::*Mutator)(uint8_t *Data, size_t Size,
                                              size_t MaxSize);

void TestEraseBytes(Mutator M, int NumIter) {
  std::unique_ptr<ExternalFunctions> t(new ExternalFunctions());
  fuzzer::EF = t.get();
  uint8_t REM0[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
  uint8_t REM1[8] = {0x00, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
  uint8_t REM2[8] = {0x00, 0x11, 0x33, 0x44, 0x55, 0x66, 0x77};
  uint8_t REM3[8] = {0x00, 0x11, 0x22, 0x44, 0x55, 0x66, 0x77};
  uint8_t REM4[8] = {0x00, 0x11, 0x22, 0x33, 0x55, 0x66, 0x77};
  uint8_t REM5[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x66, 0x77};
  uint8_t REM6[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x77};
  uint8_t REM7[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

  uint8_t REM8[6] = {0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
  uint8_t REM9[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
  uint8_t REM10[6] = {0x00, 0x11, 0x22, 0x55, 0x66, 0x77};

  uint8_t REM11[5] = {0x33, 0x44, 0x55, 0x66, 0x77};
  uint8_t REM12[5] = {0x00, 0x11, 0x22, 0x33, 0x44};
  uint8_t REM13[5] = {0x00, 0x44, 0x55, 0x66, 0x77};


  Random Rand(0);
  std::unique_ptr<MutationDispatcher> MD(new MutationDispatcher(Rand, {}));
  int FoundMask = 0;
  for (int i = 0; i < NumIter; i++) {
    uint8_t T[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    size_t NewSize = (*MD.*M)(T, sizeof(T), sizeof(T));
    if (NewSize == 7 && !memcmp(REM0, T, 7)) FoundMask |= 1 << 0;
    if (NewSize == 7 && !memcmp(REM1, T, 7)) FoundMask |= 1 << 1;
    if (NewSize == 7 && !memcmp(REM2, T, 7)) FoundMask |= 1 << 2;
    if (NewSize == 7 && !memcmp(REM3, T, 7)) FoundMask |= 1 << 3;
    if (NewSize == 7 && !memcmp(REM4, T, 7)) FoundMask |= 1 << 4;
    if (NewSize == 7 && !memcmp(REM5, T, 7)) FoundMask |= 1 << 5;
    if (NewSize == 7 && !memcmp(REM6, T, 7)) FoundMask |= 1 << 6;
    if (NewSize == 7 && !memcmp(REM7, T, 7)) FoundMask |= 1 << 7;

    if (NewSize == 6 && !memcmp(REM8, T, 6)) FoundMask |= 1 << 8;
    if (NewSize == 6 && !memcmp(REM9, T, 6)) FoundMask |= 1 << 9;
    if (NewSize == 6 && !memcmp(REM10, T, 6)) FoundMask |= 1 << 10;

    if (NewSize == 5 && !memcmp(REM11, T, 5)) FoundMask |= 1 << 11;
    if (NewSize == 5 && !memcmp(REM12, T, 5)) FoundMask |= 1 << 12;
    if (NewSize == 5 && !memcmp(REM13, T, 5)) FoundMask |= 1 << 13;
  }
  EXPECT_EQ(FoundMask, (1 << 14) - 1);
}

TEST(FuzzerMutate, EraseBytes1) {
  TestEraseBytes(&MutationDispatcher::Mutate_EraseBytes, 200);
}
TEST(FuzzerMutate, EraseBytes2) {
  TestEraseBytes(&MutationDispatcher::Mutate, 2000);
}

void TestInsertByte(Mutator M, int NumIter) {
  std::unique_ptr<ExternalFunctions> t(new ExternalFunctions());
  fuzzer::EF = t.get();
  Random Rand(0);
  std::unique_ptr<MutationDispatcher> MD(new MutationDispatcher(Rand, {}));
  int FoundMask = 0;
  uint8_t INS0[8] = {0xF1, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  uint8_t INS1[8] = {0x00, 0xF2, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  uint8_t INS2[8] = {0x00, 0x11, 0xF3, 0x22, 0x33, 0x44, 0x55, 0x66};
  uint8_t INS3[8] = {0x00, 0x11, 0x22, 0xF4, 0x33, 0x44, 0x55, 0x66};
  uint8_t INS4[8] = {0x00, 0x11, 0x22, 0x33, 0xF5, 0x44, 0x55, 0x66};
  uint8_t INS5[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0xF6, 0x55, 0x66};
  uint8_t INS6[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0xF7, 0x66};
  uint8_t INS7[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0xF8};
  for (int i = 0; i < NumIter; i++) {
    uint8_t T[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    size_t NewSize = (*MD.*M)(T, 7, 8);
    if (NewSize == 8 && !memcmp(INS0, T, 8)) FoundMask |= 1 << 0;
    if (NewSize == 8 && !memcmp(INS1, T, 8)) FoundMask |= 1 << 1;
    if (NewSize == 8 && !memcmp(INS2, T, 8)) FoundMask |= 1 << 2;
    if (NewSize == 8 && !memcmp(INS3, T, 8)) FoundMask |= 1 << 3;
    if (NewSize == 8 && !memcmp(INS4, T, 8)) FoundMask |= 1 << 4;
    if (NewSize == 8 && !memcmp(INS5, T, 8)) FoundMask |= 1 << 5;
    if (NewSize == 8 && !memcmp(INS6, T, 8)) FoundMask |= 1 << 6;
    if (NewSize == 8 && !memcmp(INS7, T, 8)) FoundMask |= 1 << 7;
  }
  EXPECT_EQ(FoundMask, 255);
}

TEST(FuzzerMutate, InsertByte1) {
  TestInsertByte(&MutationDispatcher::Mutate_InsertByte, 1 << 15);
}
TEST(FuzzerMutate, InsertByte2) {
  TestInsertByte(&MutationDispatcher::Mutate, 1 << 17);
}

void TestInsertRepeatedBytes(Mutator M, int NumIter) {
  std::unique_ptr<ExternalFunctions> t(new ExternalFunctions());
  fuzzer::EF = t.get();
  Random Rand(0);
  std::unique_ptr<MutationDispatcher> MD(new MutationDispatcher(Rand, {}));
  int FoundMask = 0;
  uint8_t INS0[7] = {0x00, 0x11, 0x22, 0x33, 'a', 'a', 'a'};
  uint8_t INS1[7] = {0x00, 0x11, 0x22, 'a', 'a', 'a', 0x33};
  uint8_t INS2[7] = {0x00, 0x11, 'a', 'a', 'a', 0x22, 0x33};
  uint8_t INS3[7] = {0x00, 'a', 'a', 'a', 0x11, 0x22, 0x33};
  uint8_t INS4[7] = {'a', 'a', 'a', 0x00, 0x11, 0x22, 0x33};

  uint8_t INS5[8] = {0x00, 0x11, 0x22, 0x33, 'b', 'b', 'b', 'b'};
  uint8_t INS6[8] = {0x00, 0x11, 0x22, 'b', 'b', 'b', 'b', 0x33};
  uint8_t INS7[8] = {0x00, 0x11, 'b', 'b', 'b', 'b', 0x22, 0x33};
  uint8_t INS8[8] = {0x00, 'b', 'b', 'b', 'b', 0x11, 0x22, 0x33};
  uint8_t INS9[8] = {'b', 'b', 'b', 'b', 0x00, 0x11, 0x22, 0x33};

  for (int i = 0; i < NumIter; i++) {
    uint8_t T[8] = {0x00, 0x11, 0x22, 0x33};
    size_t NewSize = (*MD.*M)(T, 4, 8);
    if (NewSize == 7 && !memcmp(INS0, T, 7)) FoundMask |= 1 << 0;
    if (NewSize == 7 && !memcmp(INS1, T, 7)) FoundMask |= 1 << 1;
    if (NewSize == 7 && !memcmp(INS2, T, 7)) FoundMask |= 1 << 2;
    if (NewSize == 7 && !memcmp(INS3, T, 7)) FoundMask |= 1 << 3;
    if (NewSize == 7 && !memcmp(INS4, T, 7)) FoundMask |= 1 << 4;

    if (NewSize == 8 && !memcmp(INS5, T, 8)) FoundMask |= 1 << 5;
    if (NewSize == 8 && !memcmp(INS6, T, 8)) FoundMask |= 1 << 6;
    if (NewSize == 8 && !memcmp(INS7, T, 8)) FoundMask |= 1 << 7;
    if (NewSize == 8 && !memcmp(INS8, T, 8)) FoundMask |= 1 << 8;
    if (NewSize == 8 && !memcmp(INS9, T, 8)) FoundMask |= 1 << 9;

  }
  EXPECT_EQ(FoundMask, (1 << 10) - 1);
}

TEST(FuzzerMutate, InsertRepeatedBytes1) {
  TestInsertRepeatedBytes(&MutationDispatcher::Mutate_InsertRepeatedBytes,
                          10000);
}
TEST(FuzzerMutate, InsertRepeatedBytes2) {
  TestInsertRepeatedBytes(&MutationDispatcher::Mutate, 300000);
}

void TestChangeByte(Mutator M, int NumIter) {
  std::unique_ptr<ExternalFunctions> t(new ExternalFunctions());
  fuzzer::EF = t.get();
  Random Rand(0);
  std::unique_ptr<MutationDispatcher> MD(new MutationDispatcher(Rand, {}));
  int FoundMask = 0;
  uint8_t CH0[8] = {0xF0, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
  uint8_t CH1[8] = {0x00, 0xF1, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
  uint8_t CH2[8] = {0x00, 0x11, 0xF2, 0x33, 0x44, 0x55, 0x66, 0x77};
  uint8_t CH3[8] = {0x00, 0x11, 0x22, 0xF3, 0x44, 0x55, 0x66, 0x77};
  uint8_t CH4[8] = {0x00, 0x11, 0x22, 0x33, 0xF4, 0x55, 0x66, 0x77};
  uint8_t CH5[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0xF5, 0x66, 0x77};
  uint8_t CH6[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0xF5, 0x77};
  uint8_t CH7[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0xF7};
  for (int i = 0; i < NumIter; i++) {
    uint8_t T[9] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    size_t NewSize = (*MD.*M)(T, 8, 9);
    if (NewSize == 8 && !memcmp(CH0, T, 8)) FoundMask |= 1 << 0;
    if (NewSize == 8 && !memcmp(CH1, T, 8)) FoundMask |= 1 << 1;
    if (NewSize == 8 && !memcmp(CH2, T, 8)) FoundMask |= 1 << 2;
    if (NewSize == 8 && !memcmp(CH3, T, 8)) FoundMask |= 1 << 3;
    if (NewSize == 8 && !memcmp(CH4, T, 8)) FoundMask |= 1 << 4;
    if (NewSize == 8 && !memcmp(CH5, T, 8)) FoundMask |= 1 << 5;
    if (NewSize == 8 && !memcmp(CH6, T, 8)) FoundMask |= 1 << 6;
    if (NewSize == 8 && !memcmp(CH7, T, 8)) FoundMask |= 1 << 7;
  }
  EXPECT_EQ(FoundMask, 255);
}

TEST(FuzzerMutate, ChangeByte1) {
  TestChangeByte(&MutationDispatcher::Mutate_ChangeByte, 1 << 15);
}
TEST(FuzzerMutate, ChangeByte2) {
  TestChangeByte(&MutationDispatcher::Mutate, 1 << 17);
}

void TestChangeBit(Mutator M, int NumIter) {
  std::unique_ptr<ExternalFunctions> t(new ExternalFunctions());
  fuzzer::EF = t.get();
  Random Rand(0);
  std::unique_ptr<MutationDispatcher> MD(new MutationDispatcher(Rand, {}));
  int FoundMask = 0;
  uint8_t CH0[8] = {0x01, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
  uint8_t CH1[8] = {0x00, 0x13, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
  uint8_t CH2[8] = {0x00, 0x11, 0x02, 0x33, 0x44, 0x55, 0x66, 0x77};
  uint8_t CH3[8] = {0x00, 0x11, 0x22, 0x37, 0x44, 0x55, 0x66, 0x77};
  uint8_t CH4[8] = {0x00, 0x11, 0x22, 0x33, 0x54, 0x55, 0x66, 0x77};
  uint8_t CH5[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x54, 0x66, 0x77};
  uint8_t CH6[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x76, 0x77};
  uint8_t CH7[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0xF7};
  for (int i = 0; i < NumIter; i++) {
    uint8_t T[9] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    size_t NewSize = (*MD.*M)(T, 8, 9);
    if (NewSize == 8 && !memcmp(CH0, T, 8)) FoundMask |= 1 << 0;
    if (NewSize == 8 && !memcmp(CH1, T, 8)) FoundMask |= 1 << 1;
    if (NewSize == 8 && !memcmp(CH2, T, 8)) FoundMask |= 1 << 2;
    if (NewSize == 8 && !memcmp(CH3, T, 8)) FoundMask |= 1 << 3;
    if (NewSize == 8 && !memcmp(CH4, T, 8)) FoundMask |= 1 << 4;
    if (NewSize == 8 && !memcmp(CH5, T, 8)) FoundMask |= 1 << 5;
    if (NewSize == 8 && !memcmp(CH6, T, 8)) FoundMask |= 1 << 6;
    if (NewSize == 8 && !memcmp(CH7, T, 8)) FoundMask |= 1 << 7;
  }
  EXPECT_EQ(FoundMask, 255);
}

TEST(FuzzerMutate, ChangeBit1) {
  TestChangeBit(&MutationDispatcher::Mutate_ChangeBit, 1 << 16);
}
TEST(FuzzerMutate, ChangeBit2) {
  TestChangeBit(&MutationDispatcher::Mutate, 1 << 18);
}

void TestShuffleBytes(Mutator M, int NumIter) {
  std::unique_ptr<ExternalFunctions> t(new ExternalFunctions());
  fuzzer::EF = t.get();
  Random Rand(0);
  std::unique_ptr<MutationDispatcher> MD(new MutationDispatcher(Rand, {}));
  int FoundMask = 0;
  uint8_t CH0[7] = {0x00, 0x22, 0x11, 0x33, 0x44, 0x55, 0x66};
  uint8_t CH1[7] = {0x11, 0x00, 0x33, 0x22, 0x44, 0x55, 0x66};
  uint8_t CH2[7] = {0x00, 0x33, 0x11, 0x22, 0x44, 0x55, 0x66};
  uint8_t CH3[7] = {0x00, 0x11, 0x22, 0x44, 0x55, 0x66, 0x33};
  uint8_t CH4[7] = {0x00, 0x11, 0x22, 0x33, 0x55, 0x44, 0x66};
  for (int i = 0; i < NumIter; i++) {
    uint8_t T[7] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    size_t NewSize = (*MD.*M)(T, 7, 7);
    if (NewSize == 7 && !memcmp(CH0, T, 7)) FoundMask |= 1 << 0;
    if (NewSize == 7 && !memcmp(CH1, T, 7)) FoundMask |= 1 << 1;
    if (NewSize == 7 && !memcmp(CH2, T, 7)) FoundMask |= 1 << 2;
    if (NewSize == 7 && !memcmp(CH3, T, 7)) FoundMask |= 1 << 3;
    if (NewSize == 7 && !memcmp(CH4, T, 7)) FoundMask |= 1 << 4;
  }
  EXPECT_EQ(FoundMask, 31);
}

TEST(FuzzerMutate, ShuffleBytes1) {
  TestShuffleBytes(&MutationDispatcher::Mutate_ShuffleBytes, 1 << 17);
}
TEST(FuzzerMutate, ShuffleBytes2) {
  TestShuffleBytes(&MutationDispatcher::Mutate, 1 << 20);
}

void TestCopyPart(Mutator M, int NumIter) {
  std::unique_ptr<ExternalFunctions> t(new ExternalFunctions());
  fuzzer::EF = t.get();
  Random Rand(0);
  std::unique_ptr<MutationDispatcher> MD(new MutationDispatcher(Rand, {}));
  int FoundMask = 0;
  uint8_t CH0[7] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x00, 0x11};
  uint8_t CH1[7] = {0x55, 0x66, 0x22, 0x33, 0x44, 0x55, 0x66};
  uint8_t CH2[7] = {0x00, 0x55, 0x66, 0x33, 0x44, 0x55, 0x66};
  uint8_t CH3[7] = {0x00, 0x11, 0x22, 0x00, 0x11, 0x22, 0x66};
  uint8_t CH4[7] = {0x00, 0x11, 0x11, 0x22, 0x33, 0x55, 0x66};

  for (int i = 0; i < NumIter; i++) {
    uint8_t T[7] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    size_t NewSize = (*MD.*M)(T, 7, 7);
    if (NewSize == 7 && !memcmp(CH0, T, 7)) FoundMask |= 1 << 0;
    if (NewSize == 7 && !memcmp(CH1, T, 7)) FoundMask |= 1 << 1;
    if (NewSize == 7 && !memcmp(CH2, T, 7)) FoundMask |= 1 << 2;
    if (NewSize == 7 && !memcmp(CH3, T, 7)) FoundMask |= 1 << 3;
    if (NewSize == 7 && !memcmp(CH4, T, 7)) FoundMask |= 1 << 4;
  }

  uint8_t CH5[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x00, 0x11, 0x22};
  uint8_t CH6[8] = {0x22, 0x33, 0x44, 0x00, 0x11, 0x22, 0x33, 0x44};
  uint8_t CH7[8] = {0x00, 0x11, 0x22, 0x00, 0x11, 0x22, 0x33, 0x44};
  uint8_t CH8[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x22, 0x33, 0x44};
  uint8_t CH9[8] = {0x00, 0x11, 0x22, 0x22, 0x33, 0x44, 0x33, 0x44};

  for (int i = 0; i < NumIter; i++) {
    uint8_t T[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    size_t NewSize = (*MD.*M)(T, 5, 8);
    if (NewSize == 8 && !memcmp(CH5, T, 8)) FoundMask |= 1 << 5;
    if (NewSize == 8 && !memcmp(CH6, T, 8)) FoundMask |= 1 << 6;
    if (NewSize == 8 && !memcmp(CH7, T, 8)) FoundMask |= 1 << 7;
    if (NewSize == 8 && !memcmp(CH8, T, 8)) FoundMask |= 1 << 8;
    if (NewSize == 8 && !memcmp(CH9, T, 8)) FoundMask |= 1 << 9;
  }

  EXPECT_EQ(FoundMask, 1023);
}

TEST(FuzzerMutate, CopyPart1) {
  TestCopyPart(&MutationDispatcher::Mutate_CopyPart, 1 << 10);
}
TEST(FuzzerMutate, CopyPart2) {
  TestCopyPart(&MutationDispatcher::Mutate, 1 << 13);
}
TEST(FuzzerMutate, CopyPartNoInsertAtMaxSize) {
  // This (non exhaustively) tests if `Mutate_CopyPart` tries to perform an
  // insert on an input of size `MaxSize`.  Performing an insert in this case
  // will lead to the mutation failing.
  std::unique_ptr<ExternalFunctions> t(new ExternalFunctions());
  fuzzer::EF = t.get();
  Random Rand(0);
  std::unique_ptr<MutationDispatcher> MD(new MutationDispatcher(Rand, {}));
  uint8_t Data[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x00, 0x11, 0x22};
  size_t MaxSize = sizeof(Data);
  for (int count = 0; count < (1 << 18); ++count) {
    size_t NewSize = MD->Mutate_CopyPart(Data, MaxSize, MaxSize);
    ASSERT_EQ(NewSize, MaxSize);
  }
}

void TestAddWordFromDictionary(Mutator M, int NumIter) {
  std::unique_ptr<ExternalFunctions> t(new ExternalFunctions());
  fuzzer::EF = t.get();
  Random Rand(0);
  std::unique_ptr<MutationDispatcher> MD(new MutationDispatcher(Rand, {}));
  uint8_t Word1[4] = {0xAA, 0xBB, 0xCC, 0xDD};
  uint8_t Word2[3] = {0xFF, 0xEE, 0xEF};
  MD->AddWordToManualDictionary(Word(Word1, sizeof(Word1)));
  MD->AddWordToManualDictionary(Word(Word2, sizeof(Word2)));
  int FoundMask = 0;
  uint8_t CH0[7] = {0x00, 0x11, 0x22, 0xAA, 0xBB, 0xCC, 0xDD};
  uint8_t CH1[7] = {0x00, 0x11, 0xAA, 0xBB, 0xCC, 0xDD, 0x22};
  uint8_t CH2[7] = {0x00, 0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22};
  uint8_t CH3[7] = {0xAA, 0xBB, 0xCC, 0xDD, 0x00, 0x11, 0x22};
  uint8_t CH4[6] = {0x00, 0x11, 0x22, 0xFF, 0xEE, 0xEF};
  uint8_t CH5[6] = {0x00, 0x11, 0xFF, 0xEE, 0xEF, 0x22};
  uint8_t CH6[6] = {0x00, 0xFF, 0xEE, 0xEF, 0x11, 0x22};
  uint8_t CH7[6] = {0xFF, 0xEE, 0xEF, 0x00, 0x11, 0x22};
  for (int i = 0; i < NumIter; i++) {
    uint8_t T[7] = {0x00, 0x11, 0x22};
    size_t NewSize = (*MD.*M)(T, 3, 7);
    if (NewSize == 7 && !memcmp(CH0, T, 7)) FoundMask |= 1 << 0;
    if (NewSize == 7 && !memcmp(CH1, T, 7)) FoundMask |= 1 << 1;
    if (NewSize == 7 && !memcmp(CH2, T, 7)) FoundMask |= 1 << 2;
    if (NewSize == 7 && !memcmp(CH3, T, 7)) FoundMask |= 1 << 3;
    if (NewSize == 6 && !memcmp(CH4, T, 6)) FoundMask |= 1 << 4;
    if (NewSize == 6 && !memcmp(CH5, T, 6)) FoundMask |= 1 << 5;
    if (NewSize == 6 && !memcmp(CH6, T, 6)) FoundMask |= 1 << 6;
    if (NewSize == 6 && !memcmp(CH7, T, 6)) FoundMask |= 1 << 7;
  }
  EXPECT_EQ(FoundMask, 255);
}

TEST(FuzzerMutate, AddWordFromDictionary1) {
  TestAddWordFromDictionary(
      &MutationDispatcher::Mutate_AddWordFromManualDictionary, 1 << 15);
}

TEST(FuzzerMutate, AddWordFromDictionary2) {
  TestAddWordFromDictionary(&MutationDispatcher::Mutate, 1 << 15);
}

void TestChangeASCIIInteger(Mutator M, int NumIter) {
  std::unique_ptr<ExternalFunctions> t(new ExternalFunctions());
  fuzzer::EF = t.get();
  Random Rand(0);
  std::unique_ptr<MutationDispatcher> MD(new MutationDispatcher(Rand, {}));

  uint8_t CH0[8] = {'1', '2', '3', '4', '5', '6', '7', '7'};
  uint8_t CH1[8] = {'1', '2', '3', '4', '5', '6', '7', '9'};
  uint8_t CH2[8] = {'2', '4', '6', '9', '1', '3', '5', '6'};
  uint8_t CH3[8] = {'0', '6', '1', '7', '2', '8', '3', '9'};
  int FoundMask = 0;
  for (int i = 0; i < NumIter; i++) {
    uint8_t T[8] = {'1', '2', '3', '4', '5', '6', '7', '8'};
    size_t NewSize = (*MD.*M)(T, 8, 8);
    /**/ if (NewSize == 8 && !memcmp(CH0, T, 8)) FoundMask |= 1 << 0;
    else if (NewSize == 8 && !memcmp(CH1, T, 8)) FoundMask |= 1 << 1;
    else if (NewSize == 8 && !memcmp(CH2, T, 8)) FoundMask |= 1 << 2;
    else if (NewSize == 8 && !memcmp(CH3, T, 8)) FoundMask |= 1 << 3;
    else if (NewSize == 8)                       FoundMask |= 1 << 4;
  }
  EXPECT_EQ(FoundMask, 31);
}

TEST(FuzzerMutate, ChangeASCIIInteger1) {
  TestChangeASCIIInteger(&MutationDispatcher::Mutate_ChangeASCIIInteger,
                         1 << 15);
}

TEST(FuzzerMutate, ChangeASCIIInteger2) {
  TestChangeASCIIInteger(&MutationDispatcher::Mutate, 1 << 15);
}

void TestChangeBinaryInteger(Mutator M, int NumIter) {
  std::unique_ptr<ExternalFunctions> t(new ExternalFunctions());
  fuzzer::EF = t.get();
  Random Rand(0);
  std::unique_ptr<MutationDispatcher> MD(new MutationDispatcher(Rand, {}));

  uint8_t CH0[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x79};
  uint8_t CH1[8] = {0x00, 0x11, 0x22, 0x31, 0x44, 0x55, 0x66, 0x77};
  uint8_t CH2[8] = {0xff, 0x10, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
  uint8_t CH3[8] = {0x00, 0x11, 0x2a, 0x33, 0x44, 0x55, 0x66, 0x77};
  uint8_t CH4[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x4f, 0x66, 0x77};
  uint8_t CH5[8] = {0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88};
  uint8_t CH6[8] = {0x00, 0x11, 0x22, 0x00, 0x00, 0x00, 0x08, 0x77}; // Size
  uint8_t CH7[8] = {0x00, 0x08, 0x00, 0x33, 0x44, 0x55, 0x66, 0x77}; // Sw(Size)

  int FoundMask = 0;
  for (int i = 0; i < NumIter; i++) {
    uint8_t T[8] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
    size_t NewSize = (*MD.*M)(T, 8, 8);
    /**/ if (NewSize == 8 && !memcmp(CH0, T, 8)) FoundMask |= 1 << 0;
    else if (NewSize == 8 && !memcmp(CH1, T, 8)) FoundMask |= 1 << 1;
    else if (NewSize == 8 && !memcmp(CH2, T, 8)) FoundMask |= 1 << 2;
    else if (NewSize == 8 && !memcmp(CH3, T, 8)) FoundMask |= 1 << 3;
    else if (NewSize == 8 && !memcmp(CH4, T, 8)) FoundMask |= 1 << 4;
    else if (NewSize == 8 && !memcmp(CH5, T, 8)) FoundMask |= 1 << 5;
    else if (NewSize == 8 && !memcmp(CH6, T, 8)) FoundMask |= 1 << 6;
    else if (NewSize == 8 && !memcmp(CH7, T, 8)) FoundMask |= 1 << 7;
  }
  EXPECT_EQ(FoundMask, 255);
}

TEST(FuzzerMutate, ChangeBinaryInteger1) {
  TestChangeBinaryInteger(&MutationDispatcher::Mutate_ChangeBinaryInteger,
                         1 << 12);
}

TEST(FuzzerMutate, ChangeBinaryInteger2) {
  TestChangeBinaryInteger(&MutationDispatcher::Mutate, 1 << 15);
}


TEST(FuzzerDictionary, ParseOneDictionaryEntry) {
  Unit U;
  EXPECT_FALSE(ParseOneDictionaryEntry("", &U));
  EXPECT_FALSE(ParseOneDictionaryEntry(" ", &U));
  EXPECT_FALSE(ParseOneDictionaryEntry("\t  ", &U));
  EXPECT_FALSE(ParseOneDictionaryEntry("  \" ", &U));
  EXPECT_FALSE(ParseOneDictionaryEntry("  zz\" ", &U));
  EXPECT_FALSE(ParseOneDictionaryEntry("  \"zz ", &U));
  EXPECT_FALSE(ParseOneDictionaryEntry("  \"\" ", &U));
  EXPECT_TRUE(ParseOneDictionaryEntry("\"a\"", &U));
  EXPECT_EQ(U, Unit({'a'}));
  EXPECT_TRUE(ParseOneDictionaryEntry("\"abc\"", &U));
  EXPECT_EQ(U, Unit({'a', 'b', 'c'}));
  EXPECT_TRUE(ParseOneDictionaryEntry("abc=\"abc\"", &U));
  EXPECT_EQ(U, Unit({'a', 'b', 'c'}));
  EXPECT_FALSE(ParseOneDictionaryEntry("\"\\\"", &U));
  EXPECT_TRUE(ParseOneDictionaryEntry("\"\\\\\"", &U));
  EXPECT_EQ(U, Unit({'\\'}));
  EXPECT_TRUE(ParseOneDictionaryEntry("\"\\xAB\"", &U));
  EXPECT_EQ(U, Unit({0xAB}));
  EXPECT_TRUE(ParseOneDictionaryEntry("\"\\xABz\\xDE\"", &U));
  EXPECT_EQ(U, Unit({0xAB, 'z', 0xDE}));
  EXPECT_TRUE(ParseOneDictionaryEntry("\"#\"", &U));
  EXPECT_EQ(U, Unit({'#'}));
  EXPECT_TRUE(ParseOneDictionaryEntry("\"\\\"\"", &U));
  EXPECT_EQ(U, Unit({'"'}));
}

TEST(FuzzerDictionary, ParseDictionaryFile) {
  std::vector<Unit> Units;
  EXPECT_FALSE(ParseDictionaryFile("zzz\n", &Units));
  EXPECT_FALSE(ParseDictionaryFile("", &Units));
  EXPECT_TRUE(ParseDictionaryFile("\n", &Units));
  EXPECT_EQ(Units.size(), 0U);
  EXPECT_TRUE(ParseDictionaryFile("#zzzz a b c d\n", &Units));
  EXPECT_EQ(Units.size(), 0U);
  EXPECT_TRUE(ParseDictionaryFile(" #zzzz\n", &Units));
  EXPECT_EQ(Units.size(), 0U);
  EXPECT_TRUE(ParseDictionaryFile("  #zzzz\n", &Units));
  EXPECT_EQ(Units.size(), 0U);
  EXPECT_TRUE(ParseDictionaryFile("  #zzzz\naaa=\"aa\"", &Units));
  EXPECT_EQ(Units, std::vector<Unit>({Unit({'a', 'a'})}));
  EXPECT_TRUE(
      ParseDictionaryFile("  #zzzz\naaa=\"aa\"\n\nabc=\"abc\"", &Units));
  EXPECT_EQ(Units,
            std::vector<Unit>({Unit({'a', 'a'}), Unit({'a', 'b', 'c'})}));
}

TEST(FuzzerUtil, Base64) {
  EXPECT_EQ("", Base64({}));
  EXPECT_EQ("YQ==", Base64({'a'}));
  EXPECT_EQ("eA==", Base64({'x'}));
  EXPECT_EQ("YWI=", Base64({'a', 'b'}));
  EXPECT_EQ("eHk=", Base64({'x', 'y'}));
  EXPECT_EQ("YWJj", Base64({'a', 'b', 'c'}));
  EXPECT_EQ("eHl6", Base64({'x', 'y', 'z'}));
  EXPECT_EQ("YWJjeA==", Base64({'a', 'b', 'c', 'x'}));
  EXPECT_EQ("YWJjeHk=", Base64({'a', 'b', 'c', 'x', 'y'}));
  EXPECT_EQ("YWJjeHl6", Base64({'a', 'b', 'c', 'x', 'y', 'z'}));
}

#ifdef __GLIBC__
class PrintfCapture {
 public:
  PrintfCapture() {
    OldOutputFile = GetOutputFile();
    SetOutputFile(open_memstream(&Buffer, &Size));
  }
  ~PrintfCapture() {
    fclose(GetOutputFile());
    SetOutputFile(OldOutputFile);
    free(Buffer);
  }
  std::string str() { return std::string(Buffer, Size); }

 private:
  char *Buffer;
  size_t Size;
  FILE *OldOutputFile;
};

TEST(FuzzerUtil, PrintASCII) {
  auto f = [](const char *Str, const char *PrintAfter = "") {
    PrintfCapture Capture;
    PrintASCII(reinterpret_cast<const uint8_t*>(Str), strlen(Str), PrintAfter);
    return Capture.str();
  };
  EXPECT_EQ("hello", f("hello"));
  EXPECT_EQ("c:\\\\", f("c:\\"));
  EXPECT_EQ("\\\"hi\\\"", f("\"hi\""));
  EXPECT_EQ("\\011a", f("\ta"));
  EXPECT_EQ("\\0111", f("\t1"));
  EXPECT_EQ("hello\\012", f("hello\n"));
  EXPECT_EQ("hello\n", f("hello", "\n"));
}
#endif

TEST(Corpus, Distribution) {
  DataFlowTrace DFT;
  Random Rand(0);
  struct EntropicOptions Entropic = {false, 0xFF, 100, false};
  std::unique_ptr<InputCorpus> C(new InputCorpus("", Entropic));
  size_t N = 10;
  size_t TriesPerUnit = 1<<16;
  for (size_t i = 0; i < N; i++)
    C->AddToCorpus(Unit{static_cast<uint8_t>(i)}, /*NumFeatures*/ 1,
                   /*MayDeleteFile*/ false, /*HasFocusFunction*/ false,
                   /*ForceAddToCorpus*/ false,
                   /*TimeOfUnit*/ std::chrono::microseconds(0),
                   /*FeatureSet*/ {}, DFT,
                   /*BaseII*/ nullptr);

  std::vector<size_t> Hist(N);
  for (size_t i = 0; i < N * TriesPerUnit; i++) {
    Hist[C->ChooseUnitIdxToMutate(Rand)]++;
  }
  for (size_t i = 0; i < N; i++) {
    // A weak sanity check that every unit gets invoked.
    EXPECT_GT(Hist[i], TriesPerUnit / N / 3);
  }
}

TEST(Corpus, Replace) {
  DataFlowTrace DFT;
  struct EntropicOptions Entropic = {false, 0xFF, 100, false};
  std::unique_ptr<InputCorpus> C(
      new InputCorpus(/*OutputCorpus*/ "", Entropic));
  InputInfo *FirstII =
      C->AddToCorpus(Unit{0x01, 0x00}, /*NumFeatures*/ 1,
                     /*MayDeleteFile*/ false, /*HasFocusFunction*/ false,
                     /*ForceAddToCorpus*/ false,
                     /*TimeOfUnit*/ std::chrono::microseconds(1234),
                     /*FeatureSet*/ {}, DFT,
                     /*BaseII*/ nullptr);
  InputInfo *SecondII =
      C->AddToCorpus(Unit{0x02}, /*NumFeatures*/ 1,
                     /*MayDeleteFile*/ false, /*HasFocusFunction*/ false,
                     /*ForceAddToCorpus*/ false,
                     /*TimeOfUnit*/ std::chrono::microseconds(5678),
                     /*FeatureSet*/ {}, DFT,
                     /*BaseII*/ nullptr);
  Unit ReplacedU = Unit{0x03};

  C->Replace(FirstII, ReplacedU,
             /*TimeOfUnit*/ std::chrono::microseconds(321));

  // FirstII should be replaced.
  EXPECT_EQ(FirstII->U, Unit{0x03});
  EXPECT_EQ(FirstII->Reduced, true);
  EXPECT_EQ(FirstII->TimeOfUnit, std::chrono::microseconds(321));
  std::vector<uint8_t> ExpectedSha1(kSHA1NumBytes);
  ComputeSHA1(ReplacedU.data(), ReplacedU.size(), ExpectedSha1.data());
  std::vector<uint8_t> IISha1(FirstII->Sha1, FirstII->Sha1 + kSHA1NumBytes);
  EXPECT_EQ(IISha1, ExpectedSha1);

  // SecondII should not be replaced.
  EXPECT_EQ(SecondII->U, Unit{0x02});
  EXPECT_EQ(SecondII->Reduced, false);
  EXPECT_EQ(SecondII->TimeOfUnit, std::chrono::microseconds(5678));
}

template <typename T>
void EQ(const std::vector<T> &A, const std::vector<T> &B) {
  EXPECT_EQ(A, B);
}

template <typename T> void EQ(const std::set<T> &A, const std::vector<T> &B) {
  EXPECT_EQ(A, std::set<T>(B.begin(), B.end()));
}

void EQ(const std::vector<MergeFileInfo> &A,
        const std::vector<std::string> &B) {
  std::set<std::string> a;
  for (const auto &File : A)
    a.insert(File.Name);
  std::set<std::string> b(B.begin(), B.end());
  EXPECT_EQ(a, b);
}

#define TRACED_EQ(A, ...)                                                      \
  {                                                                            \
    SCOPED_TRACE(#A);                                                          \
    EQ(A, __VA_ARGS__);                                                        \
  }

TEST(Merger, Parse) {
  Merger M;

  const char *kInvalidInputs[] = {
      // Bad file numbers
      "",
      "x",
      "0\n0",
      "3\nx",
      "2\n3",
      "2\n2",
      // Bad file names
      "2\n2\nA\n",
      "2\n2\nA\nB\nC\n",
      // Unknown markers
      "2\n1\nA\nSTARTED 0\nBAD 0 0x0",
      // Bad file IDs
      "1\n1\nA\nSTARTED 1",
      "2\n1\nA\nSTARTED 0\nFT 1 0x0",
  };
  for (auto S : kInvalidInputs) {
    SCOPED_TRACE(S);
    EXPECT_FALSE(M.Parse(S, false));
  }

  // Parse initial control file
  EXPECT_TRUE(M.Parse("1\n0\nAA\n", false));
  ASSERT_EQ(M.Files.size(), 1U);
  EXPECT_EQ(M.NumFilesInFirstCorpus, 0U);
  EXPECT_EQ(M.Files[0].Name, "AA");
  EXPECT_TRUE(M.LastFailure.empty());
  EXPECT_EQ(M.FirstNotProcessedFile, 0U);

  // Parse control file that failed on first attempt
  EXPECT_TRUE(M.Parse("2\n1\nAA\nBB\nSTARTED 0 42\n", false));
  ASSERT_EQ(M.Files.size(), 2U);
  EXPECT_EQ(M.NumFilesInFirstCorpus, 1U);
  EXPECT_EQ(M.Files[0].Name, "AA");
  EXPECT_EQ(M.Files[1].Name, "BB");
  EXPECT_EQ(M.LastFailure, "AA");
  EXPECT_EQ(M.FirstNotProcessedFile, 1U);

  // Parse control file that failed on later attempt
  EXPECT_TRUE(M.Parse("3\n1\nAA\nBB\nC\n"
                      "STARTED 0 1000\n"
                      "FT 0 1 2 3\n"
                      "STARTED 1 1001\n"
                      "FT 1 4 5 6 \n"
                      "STARTED 2 1002\n"
                      "",
                      true));
  ASSERT_EQ(M.Files.size(), 3U);
  EXPECT_EQ(M.NumFilesInFirstCorpus, 1U);
  EXPECT_EQ(M.Files[0].Name, "AA");
  EXPECT_EQ(M.Files[0].Size, 1000U);
  EXPECT_EQ(M.Files[1].Name, "BB");
  EXPECT_EQ(M.Files[1].Size, 1001U);
  EXPECT_EQ(M.Files[2].Name, "C");
  EXPECT_EQ(M.Files[2].Size, 1002U);
  EXPECT_EQ(M.LastFailure, "C");
  EXPECT_EQ(M.FirstNotProcessedFile, 3U);
  TRACED_EQ(M.Files[0].Features, {1, 2, 3});
  TRACED_EQ(M.Files[1].Features, {4, 5, 6});

  // Parse control file without features or PCs
  EXPECT_TRUE(M.Parse("2\n0\nAA\nBB\n"
                      "STARTED 0 1000\n"
                      "FT 0\n"
                      "COV 0\n"
                      "STARTED 1 1001\n"
                      "FT 1\n"
                      "COV 1\n"
                      "",
                      true));
  ASSERT_EQ(M.Files.size(), 2U);
  EXPECT_EQ(M.NumFilesInFirstCorpus, 0U);
  EXPECT_TRUE(M.LastFailure.empty());
  EXPECT_EQ(M.FirstNotProcessedFile, 2U);
  EXPECT_TRUE(M.Files[0].Features.empty());
  EXPECT_TRUE(M.Files[0].Cov.empty());
  EXPECT_TRUE(M.Files[1].Features.empty());
  EXPECT_TRUE(M.Files[1].Cov.empty());

  // Parse features and PCs
  EXPECT_TRUE(M.Parse("3\n2\nAA\nBB\nC\n"
                      "STARTED 0 1000\n"
                      "FT 0 1 2 3\n"
                      "COV 0 11 12 13\n"
                      "STARTED 1 1001\n"
                      "FT 1 4 5 6\n"
                      "COV 1 7 8 9\n"
                      "STARTED 2 1002\n"
                      "FT 2 6 1 3\n"
                      "COV 2 16 11 13\n"
                      "",
                      true));
  ASSERT_EQ(M.Files.size(), 3U);
  EXPECT_EQ(M.NumFilesInFirstCorpus, 2U);
  EXPECT_TRUE(M.LastFailure.empty());
  EXPECT_EQ(M.FirstNotProcessedFile, 3U);
  TRACED_EQ(M.Files[0].Features, {1, 2, 3});
  TRACED_EQ(M.Files[0].Cov, {11, 12, 13});
  TRACED_EQ(M.Files[1].Features, {4, 5, 6});
  TRACED_EQ(M.Files[1].Cov, {7, 8, 9});
  TRACED_EQ(M.Files[2].Features, {1, 3, 6});
  TRACED_EQ(M.Files[2].Cov, {16});
}

TEST(Merger, Merge) {
  Merger M;
  std::set<uint32_t> Features, NewFeatures;
  std::set<uint32_t> Cov, NewCov;
  std::vector<std::string> NewFiles;

  // Adds new files and features
  EXPECT_TRUE(M.Parse("3\n0\nA\nB\nC\n"
                      "STARTED 0 1000\n"
                      "FT 0 1 2 3\n"
                      "STARTED 1 1001\n"
                      "FT 1 4 5 6 \n"
                      "STARTED 2 1002\n"
                      "FT 2 6 1 3\n"
                      "",
                      true));
  EXPECT_EQ(M.Merge(Features, &NewFeatures, Cov, &NewCov, &NewFiles), 6U);
  TRACED_EQ(M.Files, {"A", "B", "C"});
  TRACED_EQ(NewFiles, {"A", "B"});
  TRACED_EQ(NewFeatures, {1, 2, 3, 4, 5, 6});

  // Doesn't return features or files in the initial corpus.
  EXPECT_TRUE(M.Parse("3\n1\nA\nB\nC\n"
                      "STARTED 0 1000\n"
                      "FT 0 1 2 3\n"
                      "STARTED 1 1001\n"
                      "FT 1 4 5 6 \n"
                      "STARTED 2 1002\n"
                      "FT 2 6 1 3\n"
                      "",
                      true));
  EXPECT_EQ(M.Merge(Features, &NewFeatures, Cov, &NewCov, &NewFiles), 3U);
  TRACED_EQ(M.Files, {"A", "B", "C"});
  TRACED_EQ(NewFiles, {"B"});
  TRACED_EQ(NewFeatures, {4, 5, 6});

  // No new features, so no new files
  EXPECT_TRUE(M.Parse("3\n2\nA\nB\nC\n"
                      "STARTED 0 1000\n"
                      "FT 0 1 2 3\n"
                      "STARTED 1 1001\n"
                      "FT 1 4 5 6 \n"
                      "STARTED 2 1002\n"
                      "FT 2 6 1 3\n"
                      "",
                      true));
  EXPECT_EQ(M.Merge(Features, &NewFeatures, Cov, &NewCov, &NewFiles), 0U);
  TRACED_EQ(M.Files, {"A", "B", "C"});
  TRACED_EQ(NewFiles, {});
  TRACED_EQ(NewFeatures, {});

  // Can pass initial features and coverage.
  Features = {1, 2, 3};
  Cov = {};
  EXPECT_TRUE(M.Parse("2\n0\nA\nB\n"
                      "STARTED 0 1000\n"
                      "FT 0 1 2 3\n"
                      "STARTED 1 1001\n"
                      "FT 1 4 5 6\n"
                      "",
                      true));
  EXPECT_EQ(M.Merge(Features, &NewFeatures, Cov, &NewCov, &NewFiles), 3U);
  TRACED_EQ(M.Files, {"A", "B"});
  TRACED_EQ(NewFiles, {"B"});
  TRACED_EQ(NewFeatures, {4, 5, 6});
  Features.clear();
  Cov.clear();

  // Parse smaller files first
  EXPECT_TRUE(M.Parse("3\n0\nA\nB\nC\n"
                      "STARTED 0 2000\n"
                      "FT 0 1 2 3\n"
                      "STARTED 1 1001\n"
                      "FT 1 4 5 6 \n"
                      "STARTED 2 1002\n"
                      "FT 2 6 1 3 \n"
                      "",
                      true));
  EXPECT_EQ(M.Merge(Features, &NewFeatures, Cov, &NewCov, &NewFiles), 6U);
  TRACED_EQ(M.Files, {"B", "C", "A"});
  TRACED_EQ(NewFiles, {"B", "C", "A"});
  TRACED_EQ(NewFeatures, {1, 2, 3, 4, 5, 6});

  EXPECT_TRUE(M.Parse("4\n0\nA\nB\nC\nD\n"
                      "STARTED 0 2000\n"
                      "FT 0 1 2 3\n"
                      "STARTED 1 1101\n"
                      "FT 1 4 5 6 \n"
                      "STARTED 2 1102\n"
                      "FT 2 6 1 3 100 \n"
                      "STARTED 3 1000\n"
                      "FT 3 1  \n"
                      "",
                      true));
  EXPECT_EQ(M.Merge(Features, &NewFeatures, Cov, &NewCov, &NewFiles), 7U);
  TRACED_EQ(M.Files, {"A", "B", "C", "D"});
  TRACED_EQ(NewFiles, {"D", "B", "C", "A"});
  TRACED_EQ(NewFeatures, {1, 2, 3, 4, 5, 6, 100});

  // For same sized file, parse more features first
  EXPECT_TRUE(M.Parse("4\n1\nA\nB\nC\nD\n"
                      "STARTED 0 2000\n"
                      "FT 0 4 5 6 7 8\n"
                      "STARTED 1 1100\n"
                      "FT 1 1 2 3 \n"
                      "STARTED 2 1100\n"
                      "FT 2 2 3 \n"
                      "STARTED 3 1000\n"
                      "FT 3 1  \n"
                      "",
                      true));
  EXPECT_EQ(M.Merge(Features, &NewFeatures, Cov, &NewCov, &NewFiles), 3U);
  TRACED_EQ(M.Files, {"A", "B", "C", "D"});
  TRACED_EQ(NewFiles, {"D", "B"});
  TRACED_EQ(NewFeatures, {1, 2, 3});
}

TEST(Merger, SetCoverMerge) {
  Merger M;
  std::set<uint32_t> Features, NewFeatures;
  std::set<uint32_t> Cov, NewCov;
  std::vector<std::string> NewFiles;

  // Adds new files and features
  EXPECT_TRUE(M.Parse("3\n0\nA\nB\nC\n"
                      "STARTED 0 1000\n"
                      "FT 0 1 2 3\n"
                      "STARTED 1 1001\n"
                      "FT 1 4 5 6 \n"
                      "STARTED 2 1002\n"
                      "FT 2 6 1 3\n"
                      "",
                      true));
  EXPECT_EQ(M.SetCoverMerge(Features, &NewFeatures, Cov, &NewCov, &NewFiles),
            6U);
  TRACED_EQ(M.Files, {"A", "B", "C"});
  TRACED_EQ(NewFiles, {"A", "B"});
  TRACED_EQ(NewFeatures, {1, 2, 3, 4, 5, 6});

  // Doesn't return features or files in the initial corpus.
  EXPECT_TRUE(M.Parse("3\n1\nA\nB\nC\n"
                      "STARTED 0 1000\n"
                      "FT 0 1 2 3\n"
                      "STARTED 1 1001\n"
                      "FT 1 4 5 6 \n"
                      "STARTED 2 1002\n"
                      "FT 2 6 1 3\n"
                      "",
                      true));
  EXPECT_EQ(M.SetCoverMerge(Features, &NewFeatures, Cov, &NewCov, &NewFiles),
            3U);
  TRACED_EQ(M.Files, {"A", "B", "C"});
  TRACED_EQ(NewFiles, {"B"});
  TRACED_EQ(NewFeatures, {4, 5, 6});

  // No new features, so no new files
  EXPECT_TRUE(M.Parse("3\n2\nA\nB\nC\n"
                      "STARTED 0 1000\n"
                      "FT 0 1 2 3\n"
                      "STARTED 1 1001\n"
                      "FT 1 4 5 6 \n"
                      "STARTED 2 1002\n"
                      "FT 2 6 1 3\n"
                      "",
                      true));
  EXPECT_EQ(M.SetCoverMerge(Features, &NewFeatures, Cov, &NewCov, &NewFiles),
            0U);
  TRACED_EQ(M.Files, {"A", "B", "C"});
  TRACED_EQ(NewFiles, {});
  TRACED_EQ(NewFeatures, {});

  // Can pass initial features and coverage.
  Features = {1, 2, 3};
  Cov = {};
  EXPECT_TRUE(M.Parse("2\n0\nA\nB\n"
                      "STARTED 0 1000\n"
                      "FT 0 1 2 3\n"
                      "STARTED 1 1001\n"
                      "FT 1 4 5 6\n"
                      "",
                      true));
  EXPECT_EQ(M.SetCoverMerge(Features, &NewFeatures, Cov, &NewCov, &NewFiles),
            3U);
  TRACED_EQ(M.Files, {"A", "B"});
  TRACED_EQ(NewFiles, {"B"});
  TRACED_EQ(NewFeatures, {4, 5, 6});
  Features.clear();
  Cov.clear();

  // Prefer files with a lot of features first (C has 4 features)
  // Then prefer B over A due to the smaller size. After choosing C and B,
  // A and D have no new features to contribute.
  EXPECT_TRUE(M.Parse("4\n0\nA\nB\nC\nD\n"
                      "STARTED 0 2000\n"
                      "FT 0 3 5 6\n"
                      "STARTED 1 1000\n"
                      "FT 1 4 5 6 \n"
                      "STARTED 2 1000\n"
                      "FT 2 1 2 3 4 \n"
                      "STARTED 3 500\n"
                      "FT 3 1  \n"
                      "",
                      true));
  EXPECT_EQ(M.SetCoverMerge(Features, &NewFeatures, Cov, &NewCov, &NewFiles),
            6U);
  TRACED_EQ(M.Files, {"A", "B", "C", "D"});
  TRACED_EQ(NewFiles, {"C", "B"});
  TRACED_EQ(NewFeatures, {1, 2, 3, 4, 5, 6});

  // Only 1 file covers all features.
  EXPECT_TRUE(M.Parse("4\n1\nA\nB\nC\nD\n"
                      "STARTED 0 2000\n"
                      "FT 0 4 5 6 7 8\n"
                      "STARTED 1 1100\n"
                      "FT 1 1 2 3 \n"
                      "STARTED 2 1100\n"
                      "FT 2 2 3 \n"
                      "STARTED 3 1000\n"
                      "FT 3 1  \n"
                      "",
                      true));
  EXPECT_EQ(M.SetCoverMerge(Features, &NewFeatures, Cov, &NewCov, &NewFiles),
            3U);
  TRACED_EQ(M.Files, {"A", "B", "C", "D"});
  TRACED_EQ(NewFiles, {"B"});
  TRACED_EQ(NewFeatures, {1, 2, 3});

  // A Feature has a value greater than (1 << 21) and hence
  // there are collisions in the underlying `covered features`
  // bitvector.
  EXPECT_TRUE(M.Parse("3\n0\nA\nB\nC\n"
                      "STARTED 0 2000\n"
                      "FT 0 1 2 3\n"
                      "STARTED 1 1000\n"
                      "FT 1 3 4 5 \n"
                      "STARTED 2 1000\n"
                      "FT 2 3 2097153 \n" // Last feature is (2^21 + 1).
                      "",
                      true));
  EXPECT_EQ(M.SetCoverMerge(Features, &NewFeatures, Cov, &NewCov, &NewFiles),
            5U);
  TRACED_EQ(M.Files, {"A", "B", "C"});
  // File 'C' is not added because it's last feature is considered
  // covered due to collision with feature 1.
  TRACED_EQ(NewFiles, {"B", "A"});
  TRACED_EQ(NewFeatures, {1, 2, 3, 4, 5});
}

#undef TRACED_EQ

TEST(DFT, BlockCoverage) {
  BlockCoverage Cov;
  // Assuming C0 has 5 instrumented blocks,
  // C1: 7 blocks, C2: 4, C3: 9, C4 never covered, C5: 15,

  // Add C0
  EXPECT_TRUE(Cov.AppendCoverage("C0 5\n"));
  EXPECT_EQ(Cov.GetCounter(0, 0), 1U);
  EXPECT_EQ(Cov.GetCounter(0, 1), 0U);  // not seen this BB yet.
  EXPECT_EQ(Cov.GetCounter(0, 5), 0U);  // BB ID out of bounds.
  EXPECT_EQ(Cov.GetCounter(1, 0), 0U);  // not seen this function yet.

  EXPECT_EQ(Cov.GetNumberOfBlocks(0), 5U);
  EXPECT_EQ(Cov.GetNumberOfCoveredBlocks(0), 1U);
  EXPECT_EQ(Cov.GetNumberOfBlocks(1), 0U);

  // Various errors.
  EXPECT_FALSE(Cov.AppendCoverage("C0\n"));  // No total number.
  EXPECT_FALSE(Cov.AppendCoverage("C0 7\n"));  // No total number.
  EXPECT_FALSE(Cov.AppendCoverage("CZ\n"));  // Wrong function number.
  EXPECT_FALSE(Cov.AppendCoverage("C1 7 7"));  // BB ID is too big.
  EXPECT_FALSE(Cov.AppendCoverage("C1 100 7")); // BB ID is too big.

  // Add C0 more times.
  EXPECT_TRUE(Cov.AppendCoverage("C0 5\n"));
  EXPECT_EQ(Cov.GetCounter(0, 0), 2U);
  EXPECT_TRUE(Cov.AppendCoverage("C0 1 2 5\n"));
  EXPECT_EQ(Cov.GetCounter(0, 0), 3U);
  EXPECT_EQ(Cov.GetCounter(0, 1), 1U);
  EXPECT_EQ(Cov.GetCounter(0, 2), 1U);
  EXPECT_EQ(Cov.GetCounter(0, 3), 0U);
  EXPECT_EQ(Cov.GetCounter(0, 4), 0U);
  EXPECT_EQ(Cov.GetNumberOfCoveredBlocks(0), 3U);
  EXPECT_TRUE(Cov.AppendCoverage("C0 1 3 4 5\n"));
  EXPECT_EQ(Cov.GetCounter(0, 0), 4U);
  EXPECT_EQ(Cov.GetCounter(0, 1), 2U);
  EXPECT_EQ(Cov.GetCounter(0, 2), 1U);
  EXPECT_EQ(Cov.GetCounter(0, 3), 1U);
  EXPECT_EQ(Cov.GetCounter(0, 4), 1U);
  EXPECT_EQ(Cov.GetNumberOfCoveredBlocks(0), 5U);

  EXPECT_TRUE(Cov.AppendCoverage("C1 7\nC2 4\nC3 9\nC5 15\nC0 5\n"));
  EXPECT_EQ(Cov.GetCounter(0, 0), 5U);
  EXPECT_EQ(Cov.GetCounter(1, 0), 1U);
  EXPECT_EQ(Cov.GetCounter(2, 0), 1U);
  EXPECT_EQ(Cov.GetCounter(3, 0), 1U);
  EXPECT_EQ(Cov.GetCounter(4, 0), 0U);
  EXPECT_EQ(Cov.GetCounter(5, 0), 1U);

  EXPECT_TRUE(Cov.AppendCoverage("C3 4 5 9\nC5 11 12 15"));
  EXPECT_EQ(Cov.GetCounter(0, 0), 5U);
  EXPECT_EQ(Cov.GetCounter(1, 0), 1U);
  EXPECT_EQ(Cov.GetCounter(2, 0), 1U);
  EXPECT_EQ(Cov.GetCounter(3, 0), 2U);
  EXPECT_EQ(Cov.GetCounter(3, 4), 1U);
  EXPECT_EQ(Cov.GetCounter(3, 5), 1U);
  EXPECT_EQ(Cov.GetCounter(3, 6), 0U);
  EXPECT_EQ(Cov.GetCounter(4, 0), 0U);
  EXPECT_EQ(Cov.GetCounter(5, 0), 2U);
  EXPECT_EQ(Cov.GetCounter(5, 10), 0U);
  EXPECT_EQ(Cov.GetCounter(5, 11), 1U);
  EXPECT_EQ(Cov.GetCounter(5, 12), 1U);
}

TEST(DFT, FunctionWeights) {
  BlockCoverage Cov;
  // unused function gets zero weight.
  EXPECT_TRUE(Cov.AppendCoverage("C0 5\n"));
  auto Weights = Cov.FunctionWeights(2);
  EXPECT_GT(Weights[0], 0.);
  EXPECT_EQ(Weights[1], 0.);

  // Less frequently used function gets less weight.
  Cov.clear();
  EXPECT_TRUE(Cov.AppendCoverage("C0 5\nC1 5\nC1 5\n"));
  Weights = Cov.FunctionWeights(2);
  EXPECT_GT(Weights[0], Weights[1]);

  // A function with more uncovered blocks gets more weight.
  Cov.clear();
  EXPECT_TRUE(Cov.AppendCoverage("C0 1 2 3 5\nC1 2 4\n"));
  Weights = Cov.FunctionWeights(2);
  EXPECT_GT(Weights[1], Weights[0]);

  // A function with DFT gets more weight than the function w/o DFT.
  Cov.clear();
  EXPECT_TRUE(Cov.AppendCoverage("F1 111\nC0 3\nC1 1 2 3\n"));
  Weights = Cov.FunctionWeights(2);
  EXPECT_GT(Weights[1], Weights[0]);
}


TEST(Fuzzer, ForEachNonZeroByte) {
  const size_t N = 64;
  alignas(64) uint8_t Ar[N + 8] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    1, 2, 0, 0, 0, 0, 0, 0,
    0, 0, 3, 0, 4, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 5, 0, 6, 0, 0,
    0, 0, 0, 0, 0, 0, 7, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 8,
    9, 9, 9, 9, 9, 9, 9, 9,
  };
  typedef std::vector<std::pair<size_t, uint8_t>> Vec;
  Vec Res, Expected;
  auto CB = [&](size_t FirstFeature, size_t Idx, uint8_t V) {
    Res.push_back({FirstFeature + Idx, V});
  };
  ForEachNonZeroByte(Ar, Ar + N, 100, CB);
  Expected = {{108, 1}, {109, 2}, {118, 3}, {120, 4},
              {135, 5}, {137, 6}, {146, 7}, {163, 8}};
  EXPECT_EQ(Res, Expected);

  Res.clear();
  ForEachNonZeroByte(Ar + 9, Ar + N, 109, CB);
  Expected = {          {109, 2}, {118, 3}, {120, 4},
              {135, 5}, {137, 6}, {146, 7}, {163, 8}};
  EXPECT_EQ(Res, Expected);

  Res.clear();
  ForEachNonZeroByte(Ar + 9, Ar + N - 9, 109, CB);
  Expected = {          {109, 2}, {118, 3}, {120, 4},
              {135, 5}, {137, 6}, {146, 7}};
  EXPECT_EQ(Res, Expected);
}

// FuzzerCommand unit tests. The arguments in the two helper methods below must
// match.
static void makeCommandArgs(std::vector<std::string> *ArgsToAdd) {
  assert(ArgsToAdd);
  ArgsToAdd->clear();
  ArgsToAdd->push_back("foo");
  ArgsToAdd->push_back("-bar=baz");
  ArgsToAdd->push_back("qux");
  ArgsToAdd->push_back(Command::ignoreRemainingArgs());
  ArgsToAdd->push_back("quux");
  ArgsToAdd->push_back("-grault=garply");
}

static std::string makeCmdLine(const char *separator, const char *suffix) {
  std::string CmdLine("foo -bar=baz qux ");
  if (strlen(separator) != 0) {
    CmdLine += separator;
    CmdLine += " ";
  }
  CmdLine += Command::ignoreRemainingArgs();
  CmdLine += " quux -grault=garply";
  if (strlen(suffix) != 0) {
    CmdLine += " ";
    CmdLine += suffix;
  }
  return CmdLine;
}

TEST(FuzzerCommand, Create) {
  std::string CmdLine;

  // Default constructor
  Command DefaultCmd;

  CmdLine = DefaultCmd.toString();
  EXPECT_EQ(CmdLine, "");

  // Explicit constructor
  std::vector<std::string> ArgsToAdd;
  makeCommandArgs(&ArgsToAdd);
  Command InitializedCmd(ArgsToAdd);

  CmdLine = InitializedCmd.toString();
  EXPECT_EQ(CmdLine, makeCmdLine("", ""));

  // Compare each argument
  auto InitializedArgs = InitializedCmd.getArguments();
  auto i = ArgsToAdd.begin();
  auto j = InitializedArgs.begin();
  while (i != ArgsToAdd.end() && j != InitializedArgs.end()) {
    EXPECT_EQ(*i++, *j++);
  }
  EXPECT_EQ(i, ArgsToAdd.end());
  EXPECT_EQ(j, InitializedArgs.end());

  // Copy constructor
  Command CopiedCmd(InitializedCmd);

  CmdLine = CopiedCmd.toString();
  EXPECT_EQ(CmdLine, makeCmdLine("", ""));

  // Assignment operator
  Command AssignedCmd;
  AssignedCmd = CopiedCmd;

  CmdLine = AssignedCmd.toString();
  EXPECT_EQ(CmdLine, makeCmdLine("", ""));
}

TEST(FuzzerCommand, ModifyArguments) {
  std::vector<std::string> ArgsToAdd;
  makeCommandArgs(&ArgsToAdd);
  Command Cmd;
  std::string CmdLine;

  Cmd.addArguments(ArgsToAdd);
  CmdLine = Cmd.toString();
  EXPECT_EQ(CmdLine, makeCmdLine("", ""));

  Cmd.addArgument("waldo");
  EXPECT_TRUE(Cmd.hasArgument("waldo"));

  CmdLine = Cmd.toString();
  EXPECT_EQ(CmdLine, makeCmdLine("waldo", ""));

  Cmd.removeArgument("waldo");
  EXPECT_FALSE(Cmd.hasArgument("waldo"));

  CmdLine = Cmd.toString();
  EXPECT_EQ(CmdLine, makeCmdLine("", ""));
}

TEST(FuzzerCommand, ModifyFlags) {
  std::vector<std::string> ArgsToAdd;
  makeCommandArgs(&ArgsToAdd);
  Command Cmd(ArgsToAdd);
  std::string Value, CmdLine;
  ASSERT_FALSE(Cmd.hasFlag("fred"));

  Value = Cmd.getFlagValue("fred");
  EXPECT_EQ(Value, "");

  CmdLine = Cmd.toString();
  EXPECT_EQ(CmdLine, makeCmdLine("", ""));

  Cmd.addFlag("fred", "plugh");
  EXPECT_TRUE(Cmd.hasFlag("fred"));

  Value = Cmd.getFlagValue("fred");
  EXPECT_EQ(Value, "plugh");

  CmdLine = Cmd.toString();
  EXPECT_EQ(CmdLine, makeCmdLine("-fred=plugh", ""));

  Cmd.removeFlag("fred");
  EXPECT_FALSE(Cmd.hasFlag("fred"));

  Value = Cmd.getFlagValue("fred");
  EXPECT_EQ(Value, "");

  CmdLine = Cmd.toString();
  EXPECT_EQ(CmdLine, makeCmdLine("", ""));
}

TEST(FuzzerCommand, SetOutput) {
  std::vector<std::string> ArgsToAdd;
  makeCommandArgs(&ArgsToAdd);
  Command Cmd(ArgsToAdd);
  std::string CmdLine;
  ASSERT_FALSE(Cmd.hasOutputFile());
  ASSERT_FALSE(Cmd.isOutAndErrCombined());

  Cmd.combineOutAndErr(true);
  EXPECT_TRUE(Cmd.isOutAndErrCombined());

  CmdLine = Cmd.toString();
  EXPECT_EQ(CmdLine, makeCmdLine("", "2>&1"));

  Cmd.combineOutAndErr(false);
  EXPECT_FALSE(Cmd.isOutAndErrCombined());

  Cmd.setOutputFile("xyzzy");
  EXPECT_TRUE(Cmd.hasOutputFile());

  CmdLine = Cmd.toString();
  EXPECT_EQ(CmdLine, makeCmdLine("", ">xyzzy"));

  Cmd.setOutputFile("thud");
  EXPECT_TRUE(Cmd.hasOutputFile());

  CmdLine = Cmd.toString();
  EXPECT_EQ(CmdLine, makeCmdLine("", ">thud"));

  Cmd.combineOutAndErr();
  EXPECT_TRUE(Cmd.isOutAndErrCombined());

  CmdLine = Cmd.toString();
  EXPECT_EQ(CmdLine, makeCmdLine("", ">thud 2>&1"));
}

TEST(Entropic, UpdateFrequency) {
  const size_t One = 1, Two = 2;
  const size_t FeatIdx1 = 0, FeatIdx2 = 42, FeatIdx3 = 12, FeatIdx4 = 26;
  size_t Index;
  // Create input corpus with default entropic configuration
  struct EntropicOptions Entropic = {true, 0xFF, 100, false};
  std::unique_ptr<InputCorpus> C(new InputCorpus("", Entropic));
  std::unique_ptr<InputInfo> II(new InputInfo());

  C->AddRareFeature(FeatIdx1);
  C->UpdateFeatureFrequency(II.get(), FeatIdx1);
  EXPECT_EQ(II->FeatureFreqs.size(), One);
  C->AddRareFeature(FeatIdx2);
  C->UpdateFeatureFrequency(II.get(), FeatIdx1);
  C->UpdateFeatureFrequency(II.get(), FeatIdx2);
  EXPECT_EQ(II->FeatureFreqs.size(), Two);
  EXPECT_EQ(II->FeatureFreqs[0].second, 2);
  EXPECT_EQ(II->FeatureFreqs[1].second, 1);

  C->AddRareFeature(FeatIdx3);
  C->AddRareFeature(FeatIdx4);
  C->UpdateFeatureFrequency(II.get(), FeatIdx3);
  C->UpdateFeatureFrequency(II.get(), FeatIdx3);
  C->UpdateFeatureFrequency(II.get(), FeatIdx3);
  C->UpdateFeatureFrequency(II.get(), FeatIdx4);

  for (Index = 1; Index < II->FeatureFreqs.size(); Index++)
    EXPECT_LT(II->FeatureFreqs[Index - 1].first, II->FeatureFreqs[Index].first);

  II->DeleteFeatureFreq(FeatIdx3);
  for (Index = 1; Index < II->FeatureFreqs.size(); Index++)
    EXPECT_LT(II->FeatureFreqs[Index - 1].first, II->FeatureFreqs[Index].first);
}

double SubAndSquare(double X, double Y) {
  double R = X - Y;
  R = R * R;
  return R;
}

TEST(Entropic, ComputeEnergy) {
  const double Precision = 0.01;
  struct EntropicOptions Entropic = {true, 0xFF, 100, false};
  std::unique_ptr<InputCorpus> C(new InputCorpus("", Entropic));
  std::unique_ptr<InputInfo> II(new InputInfo());
  std::vector<std::pair<uint32_t, uint16_t>> FeatureFreqs = {
      {1, 3}, {2, 3}, {3, 3}};
  II->FeatureFreqs = FeatureFreqs;
  II->NumExecutedMutations = 0;
  II->UpdateEnergy(4, false, std::chrono::microseconds(0));
  EXPECT_LT(SubAndSquare(II->Energy, 1.450805), Precision);

  II->NumExecutedMutations = 9;
  II->UpdateEnergy(5, false, std::chrono::microseconds(0));
  EXPECT_LT(SubAndSquare(II->Energy, 1.525496), Precision);

  II->FeatureFreqs[0].second++;
  II->FeatureFreqs.push_back(std::pair<uint32_t, uint16_t>(42, 6));
  II->NumExecutedMutations = 20;
  II->UpdateEnergy(10, false, std::chrono::microseconds(0));
  EXPECT_LT(SubAndSquare(II->Energy, 1.792831), Precision);
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
