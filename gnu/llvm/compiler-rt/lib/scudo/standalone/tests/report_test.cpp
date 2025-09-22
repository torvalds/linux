//===-- report_test.cpp -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "tests/scudo_unit_test.h"

#include "report.h"

TEST(ScudoReportDeathTest, Check) {
  CHECK_LT(-1, 1);
  EXPECT_DEATH(CHECK_GT(-1, 1),
               "\\(-1\\) > \\(1\\) \\(\\(u64\\)op1=18446744073709551615, "
               "\\(u64\\)op2=1");
}

TEST(ScudoReportDeathTest, Generic) {
  // Potentially unused if EXPECT_DEATH isn't defined.
  UNUSED void *P = reinterpret_cast<void *>(0x42424242U);
  EXPECT_DEATH(scudo::reportError("TEST123"), "Scudo ERROR.*TEST123");
  EXPECT_DEATH(scudo::reportInvalidFlag("ABC", "DEF"), "Scudo ERROR.*ABC.*DEF");
  EXPECT_DEATH(scudo::reportHeaderCorruption(P), "Scudo ERROR.*42424242");
  EXPECT_DEATH(scudo::reportSanityCheckError("XYZ"), "Scudo ERROR.*XYZ");
  EXPECT_DEATH(scudo::reportAlignmentTooBig(123, 456), "Scudo ERROR.*123.*456");
  EXPECT_DEATH(scudo::reportAllocationSizeTooBig(123, 456, 789),
               "Scudo ERROR.*123.*456.*789");
  EXPECT_DEATH(scudo::reportOutOfMemory(4242), "Scudo ERROR.*4242");
  EXPECT_DEATH(
      scudo::reportInvalidChunkState(scudo::AllocatorAction::Recycling, P),
      "Scudo ERROR.*recycling.*42424242");
  EXPECT_DEATH(
      scudo::reportInvalidChunkState(scudo::AllocatorAction::Sizing, P),
      "Scudo ERROR.*sizing.*42424242");
  EXPECT_DEATH(
      scudo::reportMisalignedPointer(scudo::AllocatorAction::Deallocating, P),
      "Scudo ERROR.*deallocating.*42424242");
  EXPECT_DEATH(scudo::reportDeallocTypeMismatch(
                   scudo::AllocatorAction::Reallocating, P, 0, 1),
               "Scudo ERROR.*reallocating.*42424242");
  EXPECT_DEATH(scudo::reportDeleteSizeMismatch(P, 123, 456),
               "Scudo ERROR.*42424242.*123.*456");
}

TEST(ScudoReportDeathTest, CSpecific) {
  EXPECT_DEATH(scudo::reportAlignmentNotPowerOfTwo(123), "Scudo ERROR.*123");
  EXPECT_DEATH(scudo::reportCallocOverflow(123, 456), "Scudo ERROR.*123.*456");
  EXPECT_DEATH(scudo::reportInvalidPosixMemalignAlignment(789),
               "Scudo ERROR.*789");
  EXPECT_DEATH(scudo::reportPvallocOverflow(123), "Scudo ERROR.*123");
  EXPECT_DEATH(scudo::reportInvalidAlignedAllocAlignment(123, 456),
               "Scudo ERROR.*123.*456");
}

#if SCUDO_LINUX || SCUDO_TRUSTY || SCUDO_ANDROID
#include "report_linux.h"

#include <errno.h>
#include <sys/mman.h>

TEST(ScudoReportDeathTest, Linux) {
  errno = ENOMEM;
  EXPECT_DEATH(scudo::reportMapError(),
               "Scudo ERROR:.*internal map failure \\(error desc=.*\\)");
  errno = ENOMEM;
  EXPECT_DEATH(scudo::reportMapError(1024U),
               "Scudo ERROR:.*internal map failure \\(error desc=.*\\) "
               "requesting 1KB");
  errno = ENOMEM;
  EXPECT_DEATH(scudo::reportUnmapError(0x1000U, 100U),
               "Scudo ERROR:.*internal unmap failure \\(error desc=.*\\) Addr "
               "0x1000 Size 100");
  errno = ENOMEM;
  EXPECT_DEATH(scudo::reportProtectError(0x1000U, 100U, PROT_READ),
               "Scudo ERROR:.*internal protect failure \\(error desc=.*\\) "
               "Addr 0x1000 Size 100 Prot 1");
}
#endif
