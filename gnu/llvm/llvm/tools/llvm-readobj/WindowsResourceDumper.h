//===- WindowsResourceDumper.h - Windows Resource printer -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_READOBJ_WINDOWSRESOURCEDUMPER_H
#define LLVM_TOOLS_LLVM_READOBJ_WINDOWSRESOURCEDUMPER_H

#include "llvm/Object/WindowsResource.h"
#include "llvm/Support/ScopedPrinter.h"

namespace llvm {
namespace object {
namespace WindowsRes {

class Dumper {
public:
  Dumper(WindowsResource *Res, ScopedPrinter &SW) : SW(SW), WinRes(Res) {}

  Error printData();

private:
  ScopedPrinter &SW;
  WindowsResource *WinRes;

  void printEntry(const ResourceEntryRef &Ref);
};

} // namespace WindowsRes
} // namespace object
} // namespace llvm

#endif
