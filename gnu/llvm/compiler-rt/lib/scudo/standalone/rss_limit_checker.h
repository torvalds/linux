//===-- common.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_RSS_LIMIT_CHECKER_H_
#define SCUDO_RSS_LIMIT_CHECKER_H_

#include "atomic_helpers.h"
#include "common.h"
#include "internal_defs.h"

namespace scudo {

class RssLimitChecker {
public:
  enum RssLimitExceeded {
    Neither,
    Soft,
    Hard,
  };

  void init(int SoftRssLimitMb, int HardRssLimitMb) {
    CHECK_GE(SoftRssLimitMb, 0);
    CHECK_GE(HardRssLimitMb, 0);
    this->SoftRssLimitMb = static_cast<uptr>(SoftRssLimitMb);
    this->HardRssLimitMb = static_cast<uptr>(HardRssLimitMb);
  }

  // Opportunistic RSS limit check. This will update the RSS limit status, if
  // it can, every 250ms, otherwise it will just return the current one.
  RssLimitExceeded getRssLimitExceeded() {
    if (!HardRssLimitMb && !SoftRssLimitMb)
      return RssLimitExceeded::Neither;

    u64 NextCheck = atomic_load_relaxed(&RssNextCheckAtNS);
    u64 Now = getMonotonicTime();

    if (UNLIKELY(Now >= NextCheck))
      check(NextCheck);

    return static_cast<RssLimitExceeded>(atomic_load_relaxed(&RssLimitStatus));
  }

  uptr getSoftRssLimit() const { return SoftRssLimitMb; }
  uptr getHardRssLimit() const { return HardRssLimitMb; }

private:
  void check(u64 NextCheck);

  uptr SoftRssLimitMb = 0;
  uptr HardRssLimitMb = 0;

  atomic_u64 RssNextCheckAtNS = {};
  atomic_u8 RssLimitStatus = {};
};

} // namespace scudo

#endif // SCUDO_RSS_LIMIT_CHECKER_H_
