//===-- size_class_map_test.cpp ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "tests/scudo_unit_test.h"

#include "size_class_map.h"

template <class SizeClassMap> void testSizeClassMap() {
  typedef SizeClassMap SCMap;
  scudo::printMap<SCMap>();
  scudo::validateMap<SCMap>();
}

TEST(ScudoSizeClassMapTest, DefaultSizeClassMap) {
  testSizeClassMap<scudo::DefaultSizeClassMap>();
}

TEST(ScudoSizeClassMapTest, AndroidSizeClassMap) {
  testSizeClassMap<scudo::AndroidSizeClassMap>();
}

struct OneClassSizeClassConfig {
  static const scudo::uptr NumBits = 1;
  static const scudo::uptr MinSizeLog = 5;
  static const scudo::uptr MidSizeLog = 5;
  static const scudo::uptr MaxSizeLog = 5;
  static const scudo::u16 MaxNumCachedHint = 0;
  static const scudo::uptr MaxBytesCachedLog = 0;
  static const scudo::uptr SizeDelta = 0;
};

TEST(ScudoSizeClassMapTest, OneClassSizeClassMap) {
  testSizeClassMap<scudo::FixedSizeClassMap<OneClassSizeClassConfig>>();
}

#if SCUDO_CAN_USE_PRIMARY64
struct LargeMaxSizeClassConfig {
  static const scudo::uptr NumBits = 3;
  static const scudo::uptr MinSizeLog = 4;
  static const scudo::uptr MidSizeLog = 8;
  static const scudo::uptr MaxSizeLog = 63;
  static const scudo::u16 MaxNumCachedHint = 128;
  static const scudo::uptr MaxBytesCachedLog = 16;
  static const scudo::uptr SizeDelta = 0;
};

TEST(ScudoSizeClassMapTest, LargeMaxSizeClassMap) {
  testSizeClassMap<scudo::FixedSizeClassMap<LargeMaxSizeClassConfig>>();
}
#endif
