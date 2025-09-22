//===-- sanitizer_mac_test.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Tests for sanitizer_mac.{h,cpp}
//
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#if SANITIZER_APPLE

#include "sanitizer_common/sanitizer_mac.h"

#include "gtest/gtest.h"

#include <sys/sysctl.h>  // sysctlbyname
#include <mach/kern_return.h>  // KERN_SUCCESS

namespace __sanitizer {

void ParseVersion(const char *vers, u16 *major, u16 *minor);

TEST(SanitizerMac, ParseVersion) {
  u16 major, minor;

  ParseVersion("11.22.33", &major, &minor);
  EXPECT_EQ(major, 11);
  EXPECT_EQ(minor, 22);

  ParseVersion("1.2", &major, &minor);
  EXPECT_EQ(major, 1);
  EXPECT_EQ(minor, 2);
}

// TODO(yln): Run sanitizer unit tests for the simulators (rdar://65680742)
#if SANITIZER_IOSSIM
TEST(SanitizerMac, GetMacosAlignedVersion) {
  const char *vers_str;
  if (SANITIZER_IOS || SANITIZER_TVOS) {
    vers_str = "13.0";
  } else if (SANITIZER_WATCHOS) {
    vers_str = "6.5";
  } else {
    FAIL() << "unsupported simulator runtime";
  }
  setenv("SIMULATOR_RUNTIME_VERSION", vers_str, /*overwrite=*/1);

  MacosVersion vers = GetMacosAlignedVersion();
  EXPECT_EQ(vers.major, 10);
  EXPECT_EQ(vers.minor, 15);
}
#else
TEST(SanitizerMac, GetMacosAlignedVersion) {
  MacosVersion vers = GetMacosAlignedVersion();
  std::ostringstream oss;
  oss << vers.major << '.' << vers.minor;
  std::string actual = oss.str();

  char buf[100];
  size_t len = sizeof(buf);
  int res = sysctlbyname("kern.osproductversion", buf, &len, nullptr, 0);
  ASSERT_EQ(res, KERN_SUCCESS);
  std::string expected(buf);

  // Prefix match
  ASSERT_EQ(expected.compare(0, actual.size(), actual), 0);
}
#endif

TEST(SanitizerMac, GetDarwinKernelVersion) {
  DarwinKernelVersion vers = GetDarwinKernelVersion();
  std::ostringstream oss;
  oss << vers.major << '.' << vers.minor;
  std::string actual = oss.str();

  char buf[100];
  size_t len = sizeof(buf);
  int res = sysctlbyname("kern.osrelease", buf, &len, nullptr, 0);
  ASSERT_EQ(res, KERN_SUCCESS);
  std::string expected(buf);

  // Prefix match
  ASSERT_EQ(expected.compare(0, actual.size(), actual), 0);
}

}  // namespace __sanitizer

#endif  // SANITIZER_APPLE
