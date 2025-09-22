//===-- PowerPC.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_ABI_POWERPC_ABIPOWERPC_H
#define LLDB_SOURCE_PLUGINS_ABI_POWERPC_ABIPOWERPC_H

class ABIPowerPC {
public:
  static void Initialize();
  static void Terminate();
};
#endif
