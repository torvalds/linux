//===-- sanitizer_symbolizer_mac.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is shared between various sanitizers' runtime libraries.
//
// Header for Mac-specific "atos" symbolizer.
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_SYMBOLIZER_MAC_H
#define SANITIZER_SYMBOLIZER_MAC_H

#include "sanitizer_platform.h"
#if SANITIZER_APPLE

#include "sanitizer_symbolizer_internal.h"

namespace __sanitizer {

class DlAddrSymbolizer final : public SymbolizerTool {
 public:
  bool SymbolizePC(uptr addr, SymbolizedStack *stack) override;
  bool SymbolizeData(uptr addr, DataInfo *info) override;
};

class AtosSymbolizerProcess;

class AtosSymbolizer final : public SymbolizerTool {
 public:
  explicit AtosSymbolizer(const char *path, LowLevelAllocator *allocator);

  bool SymbolizePC(uptr addr, SymbolizedStack *stack) override;
  bool SymbolizeData(uptr addr, DataInfo *info) override;

 private:
  AtosSymbolizerProcess *process_;
};

} // namespace __sanitizer

#endif  // SANITIZER_APPLE

#endif // SANITIZER_SYMBOLIZER_MAC_H
