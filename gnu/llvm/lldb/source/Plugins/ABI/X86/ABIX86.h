//===-- ABIX86.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_ABI_X86_ABIX86_H
#define LLDB_SOURCE_PLUGINS_ABI_X86_ABIX86_H

#include "lldb/Target/ABI.h"
#include "lldb/lldb-private.h"

class ABIX86 : public lldb_private::MCBasedABI {
public:
  static void Initialize();
  static void Terminate();

protected:
  void AugmentRegisterInfo(
      std::vector<lldb_private::DynamicRegisterInfo::Register> &regs) override;

private:
  using lldb_private::MCBasedABI::MCBasedABI;
};

#endif
