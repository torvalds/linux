//===-- sanitizer_ioctl_test.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Tests for ioctl interceptor implementation in sanitizer_common.
//
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#if SANITIZER_LINUX

#include <linux/input.h>
#include <vector>

#include "interception/interception.h"
#include "sanitizer_test_utils.h"
#include "sanitizer_common/sanitizer_platform_limits_posix.h"
#include "sanitizer_common/sanitizer_common.h"
#include "gtest/gtest.h"


using namespace __sanitizer;

#define COMMON_INTERCEPTOR_READ_RANGE(ctx, ptr, sz) \
  do {                                              \
    (void) ctx;                                     \
    (void) ptr;                                     \
    (void) sz;                                      \
  } while (0)
#define COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ptr, sz) \
  do {                                               \
    (void) ctx;                                      \
    (void) ptr;                                      \
    (void) sz;                                       \
  } while (0)

#include "sanitizer_common/sanitizer_common_interceptors_ioctl.inc"

static struct IoctlInit {
  IoctlInit() {
    ioctl_init();
    // Avoid unused function warnings.
    (void)&ioctl_common_pre;
    (void)&ioctl_common_post;
    (void)&ioctl_decode;
  }
} ioctl_static_initializer;

TEST(SanitizerIoctl, Fixup) {
  EXPECT_EQ((unsigned)FIONBIO, ioctl_request_fixup(FIONBIO));

  EXPECT_EQ(EVIOCGBIT(0, 0), ioctl_request_fixup(EVIOCGBIT(0, 16)));
  EXPECT_EQ(EVIOCGBIT(0, 0), ioctl_request_fixup(EVIOCGBIT(1, 16)));
  EXPECT_EQ(EVIOCGBIT(0, 0), ioctl_request_fixup(EVIOCGBIT(1, 17)));
  EXPECT_EQ(EVIOCGBIT(0, 0), ioctl_request_fixup(EVIOCGBIT(31, 16)));
  EXPECT_NE(EVIOCGBIT(0, 0), ioctl_request_fixup(EVIOCGBIT(32, 16)));

  EXPECT_EQ(EVIOCGABS(0), ioctl_request_fixup(EVIOCGABS(0)));
  EXPECT_EQ(EVIOCGABS(0), ioctl_request_fixup(EVIOCGABS(5)));
  EXPECT_EQ(EVIOCGABS(0), ioctl_request_fixup(EVIOCGABS(63)));
  EXPECT_NE(EVIOCGABS(0), ioctl_request_fixup(EVIOCGABS(64)));

  EXPECT_EQ(EVIOCSABS(0), ioctl_request_fixup(EVIOCSABS(0)));
  EXPECT_EQ(EVIOCSABS(0), ioctl_request_fixup(EVIOCSABS(5)));
  EXPECT_EQ(EVIOCSABS(0), ioctl_request_fixup(EVIOCSABS(63)));
  EXPECT_NE(EVIOCSABS(0), ioctl_request_fixup(EVIOCSABS(64)));

  const ioctl_desc *desc = ioctl_lookup(EVIOCGKEY(16));
  EXPECT_NE((void *)0, desc);
  EXPECT_EQ(EVIOCGKEY(0), desc->req);
}

// Test decoding KVM ioctl numbers.
TEST(SanitizerIoctl, KVM_GET_MP_STATE) {
  ioctl_desc desc;
  unsigned int desc_value =
      SANITIZER_MIPS || SANITIZER_SPARC ? 0x4004ae98U : 0x8004ae98U;
  bool res = ioctl_decode(desc_value, &desc);
  EXPECT_TRUE(res);
  EXPECT_EQ(ioctl_desc::WRITE, desc.type);
  EXPECT_EQ(4U, desc.size);
}

TEST(SanitizerIoctl, KVM_GET_LAPIC) {
  ioctl_desc desc;
  unsigned int desc_value =
      SANITIZER_MIPS || SANITIZER_SPARC ? 0x4400ae8eU : 0x8400ae8eU;
  bool res = ioctl_decode(desc_value, &desc);
  EXPECT_TRUE(res);
  EXPECT_EQ(ioctl_desc::WRITE, desc.type);
  EXPECT_EQ(1024U, desc.size);
}

TEST(SanitizerIoctl, KVM_GET_MSR_INDEX_LIST) {
  ioctl_desc desc;
  bool res = ioctl_decode(0xc004ae02U, &desc);
  EXPECT_TRUE(res);
  EXPECT_EQ(ioctl_desc::READWRITE, desc.type);
  EXPECT_EQ(4U, desc.size);
}

#endif // SANITIZER_LINUX
