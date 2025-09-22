//===-- ARM.h -------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIARM.h"
#include "ABIMacOSX_arm.h"
#include "ABISysV_arm.h"
#include "lldb/Core/PluginManager.h"

LLDB_PLUGIN_DEFINE(ABIARM)

void ABIARM::Initialize() {
  ABISysV_arm::Initialize();
  ABIMacOSX_arm::Initialize();
}

void ABIARM::Terminate() {
  ABISysV_arm::Terminate();
  ABIMacOSX_arm::Terminate();
}
