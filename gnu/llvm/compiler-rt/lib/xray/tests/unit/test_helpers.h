//===-- test_helpers.h ----------------------------------------------------===//
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
#ifndef COMPILER_RT_LIB_XRAY_TESTS_TEST_HELPERS_H_
#define COMPILER_RT_LIB_XRAY_TESTS_TEST_HELPERS_H_

#include "xray_buffer_queue.h"
#include "xray_segmented_array.h"
#include "llvm/XRay/Trace.h"
#include "llvm/XRay/XRayRecord.h"
#include "gmock/gmock.h"

// TODO: Move these to llvm/include/Testing/XRay/...
namespace llvm {
namespace xray {

std::string RecordTypeAsString(RecordTypes T);
void PrintTo(RecordTypes T, std::ostream *OS);
void PrintTo(const XRayRecord &R, std::ostream *OS);
void PrintTo(const Trace &T, std::ostream *OS);

namespace testing {

MATCHER_P(FuncId, F, "") {
  *result_listener << "where the function id is " << F;
  return arg.FuncId == F;
}

MATCHER_P(RecordType, T, "") {
  *result_listener << "where the record type is " << RecordTypeAsString(T);
  return arg.Type == T;
}

MATCHER_P(HasArg, A, "") {
  *result_listener << "where args contains " << A;
  return !arg.CallArgs.empty() &&
         std::any_of(arg.CallArgs.begin(), arg.CallArgs.end(),
                     [this](decltype(A) V) { return V == A; });
}

MATCHER_P(TSCIs, M, std::string("TSC is ") + ::testing::PrintToString(M)) {
  return ::testing::Matcher<decltype(arg.TSC)>(M).MatchAndExplain(
      arg.TSC, result_listener);
}

} // namespace testing
} // namespace xray
} // namespace llvm

namespace __xray {

std::string serialize(BufferQueue &Buffers, int32_t Version);

template <class T> void PrintTo(const Array<T> &A, std::ostream *OS) {
  *OS << "[";
  bool first = true;
  for (const auto &E : A) {
    if (!first) {
      *OS << ", ";
    }
    PrintTo(E, OS);
    first = false;
  }
  *OS << "]";
}

} // namespace __xray

#endif // COMPILER_RT_LIB_XRAY_TESTS_TEST_HELPERS_H_
