//===-- SystemInitializerLLGS.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_LLDB_SERVER_SYSTEMINITIALIZERLLGS_H
#define LLDB_TOOLS_LLDB_SERVER_SYSTEMINITIALIZERLLGS_H

#include "lldb/Initialization/SystemInitializer.h"
#include "lldb/Initialization/SystemInitializerCommon.h"

class SystemInitializerLLGS : public lldb_private::SystemInitializerCommon {
public:
  SystemInitializerLLGS() : SystemInitializerCommon(nullptr) {}

  llvm::Error Initialize() override;
  void Terminate() override;
};

#endif // LLDB_TOOLS_LLDB_SERVER_SYSTEMINITIALIZERLLGS_H
