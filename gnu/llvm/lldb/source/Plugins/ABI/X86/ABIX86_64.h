//===-- ABIX86_64.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_ABI_X86_ABIX86_64_H
#define LLDB_SOURCE_PLUGINS_ABI_X86_ABIX86_64_H

#include "Plugins/ABI/X86/ABIX86.h"

class ABIX86_64 : public ABIX86 {
protected:
  std::string GetMCName(std::string name) override {
    MapRegisterName(name, "stmm", "st");
    return name;
  }

private:
  using ABIX86::ABIX86;
};

#endif // LLDB_SOURCE_PLUGINS_ABI_X86_ABIX86_64_H
