//===-- executor_symbol_def_test.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "executor_symbol_def.h"
#include "simple_packed_serialization_utils.h"
#include "gtest/gtest.h"

using namespace __orc_rt;

TEST(ExecutorSymbolDefTest, Serialization) {
  blobSerializationRoundTrip<SPSExecutorSymbolDef>(ExecutorSymbolDef{});
  blobSerializationRoundTrip<SPSExecutorSymbolDef>(
      ExecutorSymbolDef{ExecutorAddr{0x70}, {JITSymbolFlags::Callable, 9}});
}