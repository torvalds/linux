//===-- PowerPC.h ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIPowerPC.h"
#include "ABISysV_ppc.h"
#include "ABISysV_ppc64.h"
#include "lldb/Core/PluginManager.h"

LLDB_PLUGIN_DEFINE(ABIPowerPC)

void ABIPowerPC::Initialize() {
  ABISysV_ppc::Initialize();
  ABISysV_ppc64::Initialize();
}

void ABIPowerPC::Terminate() {
  ABISysV_ppc::Terminate();
  ABISysV_ppc64::Terminate();
}
