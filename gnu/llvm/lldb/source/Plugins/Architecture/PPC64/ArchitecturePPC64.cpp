//===-- ArchitecturePPC64.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/Architecture/PPC64/ArchitecturePPC64.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/ArchSpec.h"

#include "llvm/BinaryFormat/ELF.h"

using namespace lldb_private;
using namespace lldb;

LLDB_PLUGIN_DEFINE(ArchitecturePPC64)

void ArchitecturePPC64::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                "PPC64-specific algorithms",
                                &ArchitecturePPC64::Create);
}

void ArchitecturePPC64::Terminate() {
  PluginManager::UnregisterPlugin(&ArchitecturePPC64::Create);
}

std::unique_ptr<Architecture> ArchitecturePPC64::Create(const ArchSpec &arch) {
  if (arch.GetTriple().isPPC64() &&
      arch.GetTriple().getObjectFormat() == llvm::Triple::ObjectFormatType::ELF)
    return std::unique_ptr<Architecture>(new ArchitecturePPC64());
  return nullptr;
}

static int32_t GetLocalEntryOffset(const Symbol &sym) {
  unsigned char other = sym.GetFlags() >> 8 & 0xFF;
  return llvm::ELF::decodePPC64LocalEntryOffset(other);
}

size_t ArchitecturePPC64::GetBytesToSkip(Symbol &func,
                                         const Address &curr_addr) const {
  if (curr_addr.GetFileAddress() ==
      func.GetFileAddress() + GetLocalEntryOffset(func))
    return func.GetPrologueByteSize();
  return 0;
}

void ArchitecturePPC64::AdjustBreakpointAddress(const Symbol &func,
                                                Address &addr) const {
  int32_t loffs = GetLocalEntryOffset(func);
  if (!loffs)
    return;

  addr.SetOffset(addr.GetOffset() + loffs);
}
