//===- RenderingSupport.h - output stream rendering support functions  ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_COV_RENDERINGSUPPORT_H
#define LLVM_COV_RENDERINGSUPPORT_H

#include "llvm/Support/raw_ostream.h"
#include <utility>

namespace llvm {

/// A helper class that resets the output stream's color if needed
/// when destroyed.
class ColoredRawOstream {
  ColoredRawOstream(const ColoredRawOstream &OS) = delete;

public:
  raw_ostream &OS;
  bool IsColorUsed;

  ColoredRawOstream(raw_ostream &OS, bool IsColorUsed)
      : OS(OS), IsColorUsed(IsColorUsed) {}

  ColoredRawOstream(ColoredRawOstream &&Other)
      : OS(Other.OS), IsColorUsed(Other.IsColorUsed) {
    // Reset the other IsColorUsed so that the other object won't reset the
    // color when destroyed.
    Other.IsColorUsed = false;
  }

  ~ColoredRawOstream() {
    if (IsColorUsed)
      OS.resetColor();
  }
};

template <typename T>
inline raw_ostream &operator<<(const ColoredRawOstream &OS, T &&Value) {
  return OS.OS << std::forward<T>(Value);
}

/// Change the color of the output stream if the `IsColorUsed` flag
/// is true. Returns an object that resets the color when destroyed.
inline ColoredRawOstream colored_ostream(raw_ostream &OS,
                                         raw_ostream::Colors Color,
                                         bool IsColorUsed = true,
                                         bool Bold = false, bool BG = false) {
  if (IsColorUsed)
    OS.changeColor(Color, Bold, BG);
  return ColoredRawOstream(OS, IsColorUsed);
}

} // namespace llvm

#endif // LLVM_COV_RENDERINGSUPPORT_H
