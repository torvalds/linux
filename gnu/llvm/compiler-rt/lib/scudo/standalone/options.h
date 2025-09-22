//===-- options.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_OPTIONS_H_
#define SCUDO_OPTIONS_H_

#include "atomic_helpers.h"
#include "common.h"
#include "memtag.h"

namespace scudo {

enum class OptionBit {
  MayReturnNull,
  FillContents0of2,
  FillContents1of2,
  DeallocTypeMismatch,
  DeleteSizeMismatch,
  TrackAllocationStacks,
  UseOddEvenTags,
  UseMemoryTagging,
  AddLargeAllocationSlack,
};

struct Options {
  u32 Val;

  bool get(OptionBit Opt) const { return Val & (1U << static_cast<u32>(Opt)); }

  FillContentsMode getFillContentsMode() const {
    return static_cast<FillContentsMode>(
        (Val >> static_cast<u32>(OptionBit::FillContents0of2)) & 3);
  }
};

template <typename Config> bool useMemoryTagging(const Options &Options) {
  return allocatorSupportsMemoryTagging<Config>() &&
         Options.get(OptionBit::UseMemoryTagging);
}

struct AtomicOptions {
  atomic_u32 Val = {};

  Options load() const { return Options{atomic_load_relaxed(&Val)}; }

  void clear(OptionBit Opt) {
    atomic_fetch_and(&Val, ~(1U << static_cast<u32>(Opt)),
                     memory_order_relaxed);
  }

  void set(OptionBit Opt) {
    atomic_fetch_or(&Val, 1U << static_cast<u32>(Opt), memory_order_relaxed);
  }

  void setFillContentsMode(FillContentsMode FillContents) {
    u32 Opts = atomic_load_relaxed(&Val), NewOpts;
    do {
      NewOpts = Opts;
      NewOpts &= ~(3U << static_cast<u32>(OptionBit::FillContents0of2));
      NewOpts |= static_cast<u32>(FillContents)
                 << static_cast<u32>(OptionBit::FillContents0of2);
    } while (!atomic_compare_exchange_strong(&Val, &Opts, NewOpts,
                                             memory_order_relaxed));
  }
};

} // namespace scudo

#endif // SCUDO_OPTIONS_H_
