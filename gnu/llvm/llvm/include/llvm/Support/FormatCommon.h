//===- FormatCommon.h - Formatters for common LLVM types --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_FORMATCOMMON_H
#define LLVM_SUPPORT_FORMATCOMMON_H

#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FormatVariadicDetails.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
enum class AlignStyle { Left, Center, Right };

struct FmtAlign {
  support::detail::format_adapter &Adapter;
  AlignStyle Where;
  size_t Amount;
  char Fill;

  FmtAlign(support::detail::format_adapter &Adapter, AlignStyle Where,
           size_t Amount, char Fill = ' ')
      : Adapter(Adapter), Where(Where), Amount(Amount), Fill(Fill) {}

  void format(raw_ostream &S, StringRef Options) {
    // If we don't need to align, we can format straight into the underlying
    // stream.  Otherwise we have to go through an intermediate stream first
    // in order to calculate how long the output is so we can align it.
    // TODO: Make the format method return the number of bytes written, that
    // way we can also skip the intermediate stream for left-aligned output.
    if (Amount == 0) {
      Adapter.format(S, Options);
      return;
    }
    SmallString<64> Item;
    raw_svector_ostream Stream(Item);

    Adapter.format(Stream, Options);
    if (Amount <= Item.size()) {
      S << Item;
      return;
    }

    size_t PadAmount = Amount - Item.size();
    switch (Where) {
    case AlignStyle::Left:
      S << Item;
      fill(S, PadAmount);
      break;
    case AlignStyle::Center: {
      size_t X = PadAmount / 2;
      fill(S, X);
      S << Item;
      fill(S, PadAmount - X);
      break;
    }
    default:
      fill(S, PadAmount);
      S << Item;
      break;
    }
  }

private:
  void fill(llvm::raw_ostream &S, size_t Count) {
    for (size_t I = 0; I < Count; ++I)
      S << Fill;
  }
};
}

#endif
