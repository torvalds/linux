//===-- RegisterInfoPOSIX_ppc64le.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//

#include <cassert>
#include <cstddef>
#include <vector>

#include "lldb/lldb-defines.h"
#include "llvm/Support/Compiler.h"

#include "RegisterInfoPOSIX_ppc64le.h"

// Include RegisterInfoPOSIX_ppc64le to declare our g_register_infos_ppc64le
#define DECLARE_REGISTER_INFOS_PPC64LE_STRUCT
#include "RegisterInfos_ppc64le.h"
#undef DECLARE_REGISTER_INFOS_PPC64LE_STRUCT

static const lldb_private::RegisterInfo *
GetRegisterInfoPtr(const lldb_private::ArchSpec &target_arch) {
  switch (target_arch.GetMachine()) {
  case llvm::Triple::ppc64le:
    return g_register_infos_ppc64le;
  default:
    assert(false && "Unhandled target architecture.");
    return nullptr;
  }
}

static uint32_t
GetRegisterInfoCount(const lldb_private::ArchSpec &target_arch) {
  switch (target_arch.GetMachine()) {
  case llvm::Triple::ppc64le:
    return static_cast<uint32_t>(sizeof(g_register_infos_ppc64le) /
                                 sizeof(g_register_infos_ppc64le[0]));
  default:
    assert(false && "Unhandled target architecture.");
    return 0;
  }
}

RegisterInfoPOSIX_ppc64le::RegisterInfoPOSIX_ppc64le(
    const lldb_private::ArchSpec &target_arch)
    : lldb_private::RegisterInfoInterface(target_arch),
      m_register_info_p(GetRegisterInfoPtr(target_arch)),
      m_register_info_count(GetRegisterInfoCount(target_arch)) {}

size_t RegisterInfoPOSIX_ppc64le::GetGPRSize() const {
  return sizeof(GPR);
}

const lldb_private::RegisterInfo *
RegisterInfoPOSIX_ppc64le::GetRegisterInfo() const {
  return m_register_info_p;
}

uint32_t RegisterInfoPOSIX_ppc64le::GetRegisterCount() const {
  return m_register_info_count;
}
