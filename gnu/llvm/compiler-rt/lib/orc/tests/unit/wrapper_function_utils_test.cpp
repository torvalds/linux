//===-- wrapper_function_utils_test.cpp -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of the ORC runtime.
//
//===----------------------------------------------------------------------===//

#include "wrapper_function_utils.h"
#include "gtest/gtest.h"

using namespace __orc_rt;

namespace {
constexpr const char *TestString = "test string";
} // end anonymous namespace

TEST(WrapperFunctionUtilsTest, DefaultWrapperFunctionResult) {
  WrapperFunctionResult R;
  EXPECT_TRUE(R.empty());
  EXPECT_EQ(R.size(), 0U);
  EXPECT_EQ(R.getOutOfBandError(), nullptr);
}

TEST(WrapperFunctionUtilsTest, WrapperFunctionResultFromCStruct) {
  orc_rt_CWrapperFunctionResult CR =
      orc_rt_CreateCWrapperFunctionResultFromString(TestString);
  WrapperFunctionResult R(CR);
  EXPECT_EQ(R.size(), strlen(TestString) + 1);
  EXPECT_TRUE(strcmp(R.data(), TestString) == 0);
  EXPECT_FALSE(R.empty());
  EXPECT_EQ(R.getOutOfBandError(), nullptr);
}

TEST(WrapperFunctionUtilsTest, WrapperFunctionResultFromRange) {
  auto R = WrapperFunctionResult::copyFrom(TestString, strlen(TestString) + 1);
  EXPECT_EQ(R.size(), strlen(TestString) + 1);
  EXPECT_TRUE(strcmp(R.data(), TestString) == 0);
  EXPECT_FALSE(R.empty());
  EXPECT_EQ(R.getOutOfBandError(), nullptr);
}

TEST(WrapperFunctionUtilsTest, WrapperFunctionResultFromCString) {
  auto R = WrapperFunctionResult::copyFrom(TestString);
  EXPECT_EQ(R.size(), strlen(TestString) + 1);
  EXPECT_TRUE(strcmp(R.data(), TestString) == 0);
  EXPECT_FALSE(R.empty());
  EXPECT_EQ(R.getOutOfBandError(), nullptr);
}

TEST(WrapperFunctionUtilsTest, WrapperFunctionResultFromStdString) {
  auto R = WrapperFunctionResult::copyFrom(std::string(TestString));
  EXPECT_EQ(R.size(), strlen(TestString) + 1);
  EXPECT_TRUE(strcmp(R.data(), TestString) == 0);
  EXPECT_FALSE(R.empty());
  EXPECT_EQ(R.getOutOfBandError(), nullptr);
}

TEST(WrapperFunctionUtilsTest, WrapperFunctionResultFromOutOfBandError) {
  auto R = WrapperFunctionResult::createOutOfBandError(TestString);
  EXPECT_FALSE(R.empty());
  EXPECT_TRUE(strcmp(R.getOutOfBandError(), TestString) == 0);
}

TEST(WrapperFunctionUtilsTest, WrapperFunctionCCallCreateEmpty) {
  EXPECT_TRUE(!!WrapperFunctionCall::Create<SPSArgList<>>(ExecutorAddr()));
}

static void voidNoop() {}

static orc_rt_CWrapperFunctionResult voidNoopWrapper(const char *ArgData,
                                                     size_t ArgSize) {
  return WrapperFunction<void()>::handle(ArgData, ArgSize, voidNoop).release();
}

static orc_rt_CWrapperFunctionResult addWrapper(const char *ArgData,
                                                size_t ArgSize) {
  return WrapperFunction<int32_t(int32_t, int32_t)>::handle(
             ArgData, ArgSize,
             [](int32_t X, int32_t Y) -> int32_t { return X + Y; })
      .release();
}

extern "C" __orc_rt_Opaque __orc_rt_jit_dispatch_ctx{};

extern "C" orc_rt_CWrapperFunctionResult
__orc_rt_jit_dispatch(__orc_rt_Opaque *Ctx, const void *FnTag,
                      const char *ArgData, size_t ArgSize) {
  using WrapperFunctionType =
      orc_rt_CWrapperFunctionResult (*)(const char *, size_t);

  return reinterpret_cast<WrapperFunctionType>(const_cast<void *>(FnTag))(
      ArgData, ArgSize);
}

TEST(WrapperFunctionUtilsTest, WrapperFunctionCallVoidNoopAndHandle) {
  EXPECT_FALSE(!!WrapperFunction<void()>::call((void *)&voidNoopWrapper));
}

TEST(WrapperFunctionUtilsTest, WrapperFunctionCallAddWrapperAndHandle) {
  int32_t Result;
  EXPECT_FALSE(!!WrapperFunction<int32_t(int32_t, int32_t)>::call(
      (void *)&addWrapper, Result, 1, 2));
  EXPECT_EQ(Result, (int32_t)3);
}

class AddClass {
public:
  AddClass(int32_t X) : X(X) {}
  int32_t addMethod(int32_t Y) { return X + Y; }

private:
  int32_t X;
};

static orc_rt_CWrapperFunctionResult addMethodWrapper(const char *ArgData,
                                                      size_t ArgSize) {
  return WrapperFunction<int32_t(SPSExecutorAddr, int32_t)>::handle(
             ArgData, ArgSize, makeMethodWrapperHandler(&AddClass::addMethod))
      .release();
}

TEST(WrapperFunctionUtilsTest, WrapperFunctionMethodCallAndHandleRet) {
  int32_t Result;
  AddClass AddObj(1);
  EXPECT_FALSE(!!WrapperFunction<int32_t(SPSExecutorAddr, int32_t)>::call(
      (void *)&addMethodWrapper, Result, ExecutorAddr::fromPtr(&AddObj), 2));
  EXPECT_EQ(Result, (int32_t)3);
}

static orc_rt_CWrapperFunctionResult sumArrayWrapper(const char *ArgData,
                                                     size_t ArgSize) {
  return WrapperFunction<int8_t(SPSExecutorAddrRange)>::handle(
             ArgData, ArgSize,
             [](ExecutorAddrRange R) {
               int8_t Sum = 0;
               for (char C : R.toSpan<char>())
                 Sum += C;
               return Sum;
             })
      .release();
}

TEST(WrapperFunctionUtilsTest, SerializedWrapperFunctionCallTest) {
  {
    // Check wrapper function calls.
    char A[] = {1, 2, 3, 4};

    auto WFC =
        cantFail(WrapperFunctionCall::Create<SPSArgList<SPSExecutorAddrRange>>(
            ExecutorAddr::fromPtr(sumArrayWrapper),
            ExecutorAddrRange(ExecutorAddr::fromPtr(A),
                              ExecutorAddrDiff(sizeof(A)))));

    WrapperFunctionResult WFR(WFC.run());
    EXPECT_EQ(WFR.size(), 1U);
    EXPECT_EQ(WFR.data()[0], 10);
  }

  {
    // Check calls to void functions.
    auto WFC =
        cantFail(WrapperFunctionCall::Create<SPSArgList<SPSExecutorAddrRange>>(
            ExecutorAddr::fromPtr(voidNoopWrapper), ExecutorAddrRange()));
    auto Err = WFC.runWithSPSRet<void>();
    EXPECT_FALSE(!!Err);
  }

  {
    // Check calls with arguments and return values.
    auto WFC =
        cantFail(WrapperFunctionCall::Create<SPSArgList<int32_t, int32_t>>(
            ExecutorAddr::fromPtr(addWrapper), 2, 4));

    int32_t Result = 0;
    auto Err = WFC.runWithSPSRet<int32_t>(Result);
    EXPECT_FALSE(!!Err);
    EXPECT_EQ(Result, 6);
  }
}
