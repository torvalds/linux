//===-- sanitizer_lzw.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Lempel–Ziv–Welch encoding/decoding
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_LZW_H
#define SANITIZER_LZW_H

#include "sanitizer_dense_map.h"

namespace __sanitizer {

using LzwCodeType = u32;

template <class T, class ItIn, class ItOut>
ItOut LzwEncode(ItIn begin, ItIn end, ItOut out) {
  using Substring =
      detail::DenseMapPair<LzwCodeType /* Prefix */, T /* Next input */>;

  // Sentinel value for substrings of len 1.
  static constexpr LzwCodeType kNoPrefix =
      Min(DenseMapInfo<Substring>::getEmptyKey().first,
          DenseMapInfo<Substring>::getTombstoneKey().first) -
      1;
  DenseMap<Substring, LzwCodeType> prefix_to_code;
  {
    // Add all substring of len 1 as initial dictionary.
    InternalMmapVector<T> dict_len1;
    for (auto it = begin; it != end; ++it)
      if (prefix_to_code.try_emplace({kNoPrefix, *it}, 0).second)
        dict_len1.push_back(*it);

    // Slightly helps with later delta encoding.
    Sort(dict_len1.data(), dict_len1.size());

    // For large sizeof(T) we have to store dict_len1. Smaller types like u8 can
    // just generate them.
    *out = dict_len1.size();
    ++out;

    for (uptr i = 0; i != dict_len1.size(); ++i) {
      // Remap after the Sort.
      prefix_to_code[{kNoPrefix, dict_len1[i]}] = i;
      *out = dict_len1[i];
      ++out;
    }
    CHECK_EQ(prefix_to_code.size(), dict_len1.size());
  }

  if (begin == end)
    return out;

  // Main LZW encoding loop.
  LzwCodeType match = prefix_to_code.find({kNoPrefix, *begin})->second;
  ++begin;
  for (auto it = begin; it != end; ++it) {
    // Extend match with the new item.
    auto ins = prefix_to_code.try_emplace({match, *it}, prefix_to_code.size());
    if (ins.second) {
      // This is a new substring, but emit the code for the current match
      // (before extend). This allows LZW decoder to recover the dictionary.
      *out = match;
      ++out;
      // Reset the match to a single item, which must be already in the map.
      match = prefix_to_code.find({kNoPrefix, *it})->second;
    } else {
      // Already known, use as the current match.
      match = ins.first->second;
    }
  }

  *out = match;
  ++out;

  return out;
}

template <class T, class ItIn, class ItOut>
ItOut LzwDecode(ItIn begin, ItIn end, ItOut out) {
  if (begin == end)
    return out;

  // Load dictionary of len 1 substrings. Theses correspont to lowest codes.
  InternalMmapVector<T> dict_len1(*begin);
  ++begin;

  if (begin == end)
    return out;

  for (auto& v : dict_len1) {
    v = *begin;
    ++begin;
  }

  // Substrings of len 2 and up. Indexes are shifted because [0,
  // dict_len1.size()) stored in dict_len1. Substings get here after being
  // emitted to the output, so we can use output position.
  InternalMmapVector<detail::DenseMapPair<ItOut /* begin. */, ItOut /* end */>>
      code_to_substr;

  // Copies already emitted substrings into the output again.
  auto copy = [&code_to_substr, &dict_len1](LzwCodeType code, ItOut out) {
    if (code < dict_len1.size()) {
      *out = dict_len1[code];
      ++out;
      return out;
    }
    const auto& s = code_to_substr[code - dict_len1.size()];

    for (ItOut it = s.first; it != s.second; ++it, ++out) *out = *it;
    return out;
  };

  // Returns lens of the substring with the given code.
  auto code_to_len = [&code_to_substr, &dict_len1](LzwCodeType code) -> uptr {
    if (code < dict_len1.size())
      return 1;
    const auto& s = code_to_substr[code - dict_len1.size()];
    return s.second - s.first;
  };

  // Main LZW decoding loop.
  LzwCodeType prev_code = *begin;
  ++begin;
  out = copy(prev_code, out);
  for (auto it = begin; it != end; ++it) {
    LzwCodeType code = *it;
    auto start = out;
    if (code == dict_len1.size() + code_to_substr.size()) {
      // Special LZW case. The code is not in the dictionary yet. This is
      // possible only when the new substring is the same as previous one plus
      // the first item of the previous substring. We can emit that in two
      // steps.
      out = copy(prev_code, out);
      *out = *start;
      ++out;
    } else {
      out = copy(code, out);
    }

    // Every time encoded emits the code, it also creates substing of len + 1
    // including the first item of the just emmited substring. Do the same here.
    uptr len = code_to_len(prev_code);
    code_to_substr.push_back({start - len, start + 1});

    prev_code = code;
  }
  return out;
}

}  // namespace __sanitizer
#endif
