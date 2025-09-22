//===-- Mips.h ------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIMips.h"
#include "ABISysV_mips.h"
#include "ABISysV_mips64.h"
#include "lldb/Core/PluginManager.h"

LLDB_PLUGIN_DEFINE(ABIMips)

void ABIMips::Initialize() {
  ABISysV_mips::Initialize();
  ABISysV_mips64::Initialize();
}

void ABIMips::Terminate() {
  ABISysV_mips::Terminate();
  ABISysV_mips64::Terminate();
}
