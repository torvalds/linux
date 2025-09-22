//===-- sanitizer_stacktrace_test.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer/AddressSanitizer runtime.
//
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_stacktrace.h"

#include <string.h>

#include <algorithm>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_internal_defs.h"

using testing::ContainsRegex;
using testing::MatchesRegex;

namespace __sanitizer {

class FastUnwindTest : public ::testing::Test {
 protected:
  virtual void SetUp();
  virtual void TearDown();

  void UnwindFast();

  void *mapping;
  uhwptr *fake_stack;
  const uptr fake_stack_size = 10;
  uhwptr start_pc;

  uhwptr fake_bp;
  uhwptr fake_top;
  uhwptr fake_bottom;
  BufferedStackTrace trace;

#if defined(__loongarch__) || defined(__riscv)
  const uptr kFpOffset = 4;
  const uptr kBpOffset = 2;
#else
  const uptr kFpOffset = 2;
  const uptr kBpOffset = 0;
#endif

 private:
  CommonFlags tmp_flags_;
};

static uptr PC(uptr idx) {
  return (1<<20) + idx;
}

void FastUnwindTest::SetUp() {
  size_t ps = GetPageSize();
  mapping = MmapOrDie(2 * ps, "FastUnwindTest");
  MprotectNoAccess((uptr)mapping, ps);

  // Unwinder may peek 1 word down from the starting FP.
  fake_stack = (uhwptr *)((uptr)mapping + ps + sizeof(uhwptr));

  // Fill an array of pointers with fake fp+retaddr pairs.  Frame pointers have
  // even indices.
  for (uptr i = 0; i + 1 < fake_stack_size; i += 2) {
    fake_stack[i] = (uptr)&fake_stack[i + kFpOffset];  // fp
    fake_stack[i+1] = PC(i + 1); // retaddr
  }
  // Mark the last fp point back up to terminate the stack trace.
  fake_stack[RoundDownTo(fake_stack_size - 1, 2)] = (uhwptr)&fake_stack[0];

  // Top is two slots past the end because UnwindFast subtracts two.
  fake_top = (uhwptr)&fake_stack[fake_stack_size + kFpOffset];
  // Bottom is one slot before the start because UnwindFast uses >.
  fake_bottom = (uhwptr)mapping;
  fake_bp = (uptr)&fake_stack[kBpOffset];
  start_pc = PC(0);

  tmp_flags_.CopyFrom(*common_flags());
}

void FastUnwindTest::TearDown() {
  size_t ps = GetPageSize();
  UnmapOrDie(mapping, 2 * ps);

  // Restore default flags.
  OverrideCommonFlags(tmp_flags_);
}

#if SANITIZER_CAN_FAST_UNWIND

#ifdef __sparc__
// Fake stacks don't meet SPARC UnwindFast requirements.
#define SKIP_ON_SPARC(x) DISABLED_##x
#else
#define SKIP_ON_SPARC(x) x
#endif

void FastUnwindTest::UnwindFast() {
  trace.UnwindFast(start_pc, fake_bp, fake_top, fake_bottom, kStackTraceMax);
}

TEST_F(FastUnwindTest, SKIP_ON_SPARC(Basic)) {
  UnwindFast();
  // Should get all on-stack retaddrs and start_pc.
  EXPECT_EQ(6U, trace.size);
  EXPECT_EQ(start_pc, trace.trace[0]);
  for (uptr i = 1; i <= 5; i++) {
    EXPECT_EQ(PC(i*2 - 1), trace.trace[i]);
  }
}

// From: https://github.com/google/sanitizers/issues/162
TEST_F(FastUnwindTest, SKIP_ON_SPARC(FramePointerLoop)) {
  // Make one fp point to itself.
  fake_stack[4] = (uhwptr)&fake_stack[4];
  UnwindFast();
  // Should get all on-stack retaddrs up to the 4th slot and start_pc.
  EXPECT_EQ(4U, trace.size);
  EXPECT_EQ(start_pc, trace.trace[0]);
  for (uptr i = 1; i <= 3; i++) {
    EXPECT_EQ(PC(i*2 - 1), trace.trace[i]);
  }
}

TEST_F(FastUnwindTest, SKIP_ON_SPARC(MisalignedFramePointer)) {
  // Make one fp misaligned.
  fake_stack[4] += 3;
  UnwindFast();
  // Should get all on-stack retaddrs up to the 4th slot and start_pc.
  EXPECT_EQ(4U, trace.size);
  EXPECT_EQ(start_pc, trace.trace[0]);
  for (uptr i = 1; i < 4U; i++) {
    EXPECT_EQ(PC(i*2 - 1), trace.trace[i]);
  }
}

TEST_F(FastUnwindTest, OneFrameStackTrace) {
  trace.Unwind(start_pc, fake_bp, nullptr, true, 1);
  EXPECT_EQ(1U, trace.size);
  EXPECT_EQ(start_pc, trace.trace[0]);
  EXPECT_EQ((uhwptr)&fake_stack[kBpOffset], trace.top_frame_bp);
}

TEST_F(FastUnwindTest, ZeroFramesStackTrace) {
  trace.Unwind(start_pc, fake_bp, nullptr, true, 0);
  EXPECT_EQ(0U, trace.size);
  EXPECT_EQ(0U, trace.top_frame_bp);
}

TEST_F(FastUnwindTest, SKIP_ON_SPARC(FPBelowPrevFP)) {
  // The next FP points to unreadable memory inside the stack limits, but below
  // current FP.
  fake_stack[0] = (uhwptr)&fake_stack[-50];
  fake_stack[1] = PC(1);
  UnwindFast();
  EXPECT_EQ(2U, trace.size);
  EXPECT_EQ(PC(0), trace.trace[0]);
  EXPECT_EQ(PC(1), trace.trace[1]);
}

TEST_F(FastUnwindTest, SKIP_ON_SPARC(CloseToZeroFrame)) {
  // Make one pc a NULL pointer.
  fake_stack[5] = 0x0;
  UnwindFast();
  // The stack should be truncated at the NULL pointer (and not include it).
  EXPECT_EQ(3U, trace.size);
  EXPECT_EQ(start_pc, trace.trace[0]);
  for (uptr i = 1; i < 3U; i++) {
    EXPECT_EQ(PC(i*2 - 1), trace.trace[i]);
  }
}

using StackPrintTest = FastUnwindTest;

TEST_F(StackPrintTest, SKIP_ON_SPARC(ContainsFullTrace)) {
  // Override stack trace format to make testing code independent of default
  // flag values.
  CommonFlags flags;
  flags.CopyFrom(*common_flags());
  flags.stack_trace_format = "#%n %p";
  OverrideCommonFlags(flags);

  UnwindFast();

  char buf[3000];
  trace.PrintTo(buf, sizeof(buf));
  EXPECT_THAT(std::string(buf),
              MatchesRegex("(#[0-9]+ 0x[0-9a-f]+\n){" +
                           std::to_string(trace.size) + "}\n"));
}

TEST_F(StackPrintTest, SKIP_ON_SPARC(TruncatesContents)) {
  UnwindFast();

  char buf[3000];
  uptr actual_len = trace.PrintTo(buf, sizeof(buf));
  ASSERT_LT(actual_len, sizeof(buf));

  char tinybuf[10];
  trace.PrintTo(tinybuf, sizeof(tinybuf));

  // This the truncation case.
  ASSERT_GT(actual_len, sizeof(tinybuf));

  // The truncated contents should be a prefix of the full contents.
  size_t lastpos = sizeof(tinybuf) - 1;
  EXPECT_EQ(strncmp(buf, tinybuf, lastpos), 0);
  EXPECT_EQ(tinybuf[lastpos], '\0');

  // Full bufffer has more contents...
  EXPECT_NE(buf[lastpos], '\0');
}

TEST_F(StackPrintTest, SKIP_ON_SPARC(WorksWithEmptyStack)) {
  char buf[3000];
  trace.PrintTo(buf, sizeof(buf));
  EXPECT_NE(strstr(buf, "<empty stack>"), nullptr);
}

TEST_F(StackPrintTest, SKIP_ON_SPARC(ReturnsCorrectLength)) {
  UnwindFast();

  char buf[3000];
  uptr len = trace.PrintTo(buf, sizeof(buf));
  size_t actual_len = strlen(buf);
  ASSERT_LT(len, sizeof(buf));
  EXPECT_EQ(len, actual_len);

  char tinybuf[5];
  len = trace.PrintTo(tinybuf, sizeof(tinybuf));
  size_t truncated_len = strlen(tinybuf);
  ASSERT_GE(len, sizeof(tinybuf));
  EXPECT_EQ(len, actual_len);
  EXPECT_EQ(truncated_len, sizeof(tinybuf) - 1);
}

TEST_F(StackPrintTest, SKIP_ON_SPARC(AcceptsZeroSize)) {
  UnwindFast();
  char buf[1];
  EXPECT_GT(trace.PrintTo(buf, 0), 0u);
}

using StackPrintDeathTest = StackPrintTest;

TEST_F(StackPrintDeathTest, SKIP_ON_SPARC(RequiresNonNullBuffer)) {
  UnwindFast();
  EXPECT_DEATH(trace.PrintTo(NULL, 100), "");
}

#endif // SANITIZER_CAN_FAST_UNWIND

TEST(SlowUnwindTest, ShortStackTrace) {
  BufferedStackTrace stack;
  uptr pc = StackTrace::GetCurrentPc();
  uptr bp = GET_CURRENT_FRAME();
  stack.Unwind(pc, bp, nullptr, false, /*max_depth=*/0);
  EXPECT_EQ(0U, stack.size);
  EXPECT_EQ(0U, stack.top_frame_bp);
  stack.Unwind(pc, bp, nullptr, false, /*max_depth=*/1);
  EXPECT_EQ(1U, stack.size);
  EXPECT_EQ(pc, stack.trace[0]);
  EXPECT_EQ(bp, stack.top_frame_bp);
}

TEST(GetCurrentPc, Basic) {
  // Test that PCs obtained via GET_CURRENT_PC()
  // and StackTrace::GetCurrentPc() are all different
  // and are close to the function start.
  struct Local {
    static NOINLINE void Test() {
      const uptr pcs[] = {
          (uptr)&Local::Test,
          GET_CURRENT_PC(),
          StackTrace::GetCurrentPc(),
          StackTrace::GetCurrentPc(),
      };
      for (uptr i = 0; i < ARRAY_SIZE(pcs); i++)
        Printf("pc%zu: %p\n", i, (void *)(pcs[i]));
      for (uptr i = 1; i < ARRAY_SIZE(pcs); i++) {
        EXPECT_GT(pcs[i], pcs[0]);
        EXPECT_LT(pcs[i], pcs[0] + 1000);
        for (uptr j = 0; j < i; j++) EXPECT_NE(pcs[i], pcs[j]);
      }
    }
  };
  Local::Test();
}

// Dummy implementation. This should never be called, but is required to link
// non-optimized builds of this test.
void BufferedStackTrace::UnwindImpl(uptr pc, uptr bp, void *context,
                                    bool request_fast, u32 max_depth) {
  UNIMPLEMENTED();
}

}  // namespace __sanitizer
