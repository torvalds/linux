//===-- sanitizer_addrhashmap_test.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "sanitizer_common/sanitizer_addrhashmap.h"

#include <unordered_map>

#include "gtest/gtest.h"

namespace __sanitizer {

struct Value {
  int payload;
  inline bool operator==(const Value& rhs) const {
    return payload == rhs.payload;
  }
};

using MapTy = AddrHashMap<Value, 11>;
using HandleTy = MapTy::Handle;
using RefMapTy = std::unordered_map<uptr, Value>;

static void ExistsInReferenceMap(const uptr key, const Value& val, void* arg) {
  RefMapTy* ref = reinterpret_cast<RefMapTy*>(arg);
  const RefMapTy::iterator iter = ref->find(key);
  ASSERT_NE(iter, ref->end());
  EXPECT_EQ(iter->second, val);
  ref->erase(iter);
}

TEST(AddrHashMap, Basic) {
  // Use a reference implementation to compare with.
  RefMapTy reference_map{
      {0x1000, {1}},
      {0x2000, {2}},
      {0x3000, {3}},
  };

  MapTy m;

  for (const auto& key_val : reference_map) {
    const uptr key = key_val.first;
    const Value val = key_val.second;

    // Insert all the elements.
    {
      HandleTy h(&m, key);
      ASSERT_TRUE(h.created());
      h->payload = val.payload;
    }
  }

  // Now check that all the elements are present.
  m.ForEach(ExistsInReferenceMap, &reference_map);
  EXPECT_TRUE(reference_map.empty());
}

}  // namespace __sanitizer
