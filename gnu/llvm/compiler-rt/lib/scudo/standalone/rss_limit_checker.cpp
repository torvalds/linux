//===-- common.cpp ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "rss_limit_checker.h"
#include "atomic_helpers.h"
#include "string_utils.h"

namespace scudo {

void RssLimitChecker::check(u64 NextCheck) {
  // The interval for the checks is 250ms.
  static constexpr u64 CheckInterval = 250 * 1000000;

  // Early return in case another thread already did the calculation.
  if (!atomic_compare_exchange_strong(&RssNextCheckAtNS, &NextCheck,
                                      getMonotonicTime() + CheckInterval,
                                      memory_order_relaxed)) {
    return;
  }

  const uptr CurrentRssMb = GetRSS() >> 20;

  RssLimitExceeded Result = RssLimitExceeded::Neither;
  if (UNLIKELY(HardRssLimitMb && HardRssLimitMb < CurrentRssMb))
    Result = RssLimitExceeded::Hard;
  else if (UNLIKELY(SoftRssLimitMb && SoftRssLimitMb < CurrentRssMb))
    Result = RssLimitExceeded::Soft;

  atomic_store_relaxed(&RssLimitStatus, static_cast<u8>(Result));
}

} // namespace scudo
