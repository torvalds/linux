//===-- sanitizer_range.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "sanitizer_range.h"

#include "sanitizer_common/sanitizer_array_ref.h"

namespace __sanitizer {

void Intersect(ArrayRef<Range> a, ArrayRef<Range> b,
               InternalMmapVectorNoCtor<Range> &output) {
  output.clear();

  struct Event {
    uptr val;
    s8 diff1;
    s8 diff2;
  };

  InternalMmapVector<Event> events;
  for (const Range &r : a) {
    CHECK_LE(r.begin, r.end);
    events.push_back({r.begin, 1, 0});
    events.push_back({r.end, -1, 0});
  }

  for (const Range &r : b) {
    CHECK_LE(r.begin, r.end);
    events.push_back({r.begin, 0, 1});
    events.push_back({r.end, 0, -1});
  }

  Sort(events.data(), events.size(),
       [](const Event &lh, const Event &rh) { return lh.val < rh.val; });

  uptr start = 0;
  sptr state1 = 0;
  sptr state2 = 0;
  for (const auto &e : events) {
    if (e.val != start) {
      DCHECK_GE(state1, 0);
      DCHECK_GE(state2, 0);
      if (state1 && state2) {
        if (!output.empty() && start == output.back().end)
          output.back().end = e.val;
        else
          output.push_back({start, e.val});
      }
      start = e.val;
    }

    state1 += e.diff1;
    state2 += e.diff2;
  }
}

}  // namespace __sanitizer
