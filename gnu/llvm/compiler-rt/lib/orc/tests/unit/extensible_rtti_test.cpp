//===-- extensible_rtti_test.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of the ORC runtime.
//
// Note:
//   This unit test was adapted from
//   llvm/unittests/Support/ExtensibleRTTITest.cpp
//
//===----------------------------------------------------------------------===//

#include "extensible_rtti.h"
#include "gtest/gtest.h"

using namespace __orc_rt;

namespace {

class MyBase : public RTTIExtends<MyBase, RTTIRoot> {};

class MyDerivedA : public RTTIExtends<MyDerivedA, MyBase> {};

class MyDerivedB : public RTTIExtends<MyDerivedB, MyBase> {};

} // end anonymous namespace

TEST(ExtensibleRTTITest, BaseCheck) {
  MyBase MB;
  MyDerivedA MDA;
  MyDerivedB MDB;

  // Check MB properties.
  EXPECT_TRUE(isa<RTTIRoot>(MB));
  EXPECT_TRUE(isa<MyBase>(MB));
  EXPECT_FALSE(isa<MyDerivedA>(MB));
  EXPECT_FALSE(isa<MyDerivedB>(MB));

  // Check MDA properties.
  EXPECT_TRUE(isa<RTTIRoot>(MDA));
  EXPECT_TRUE(isa<MyBase>(MDA));
  EXPECT_TRUE(isa<MyDerivedA>(MDA));
  EXPECT_FALSE(isa<MyDerivedB>(MDA));

  // Check MDB properties.
  EXPECT_TRUE(isa<RTTIRoot>(MDB));
  EXPECT_TRUE(isa<MyBase>(MDB));
  EXPECT_FALSE(isa<MyDerivedA>(MDB));
  EXPECT_TRUE(isa<MyDerivedB>(MDB));
}
