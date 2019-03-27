/*
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */
#include "utils/ScopeGuard.h"

#include <gtest/gtest.h>

using namespace pzstd;

TEST(ScopeGuard, Dismiss) {
  {
    auto guard = makeScopeGuard([&] { EXPECT_TRUE(false); });
    guard.dismiss();
  }
}

TEST(ScopeGuard, Executes) {
  bool executed = false;
  {
    auto guard = makeScopeGuard([&] { executed = true; });
  }
  EXPECT_TRUE(executed);
}
