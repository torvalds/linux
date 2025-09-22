//===-- ArchitectureAArch64.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/Architecture/AArch64/ArchitectureAArch64.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/DataExtractor.h"

using namespace lldb_private;
using namespace lldb;

LLDB_PLUGIN_DEFINE(ArchitectureAArch64)

void ArchitectureAArch64::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                "AArch64-specific algorithms",
                                &ArchitectureAArch64::Create);
}

void ArchitectureAArch64::Terminate() {
  PluginManager::UnregisterPlugin(&ArchitectureAArch64::Create);
}

std::unique_ptr<Architecture>
ArchitectureAArch64::Create(const ArchSpec &arch) {
  auto machine = arch.GetMachine();
  if (machine != llvm::Triple::aarch64 && machine != llvm::Triple::aarch64_be &&
      machine != llvm::Triple::aarch64_32) {
    return nullptr;
  }
  return std::unique_ptr<Architecture>(new ArchitectureAArch64());
}

static void
UpdateARM64SVERegistersInfos(DynamicRegisterInfo::reg_collection_range regs,
                             uint64_t vg) {
  // SVE Z register size is vg x 8 bytes.
  uint32_t z_reg_byte_size = vg * 8;

  // SVE vector length has changed, accordingly set size of Z, P and FFR
  // registers. Also invalidate register offsets it will be recalculated
  // after SVE register size update.
  for (auto &reg : regs) {
    if (reg.value_regs == nullptr) {
      if (reg.name[0] == 'z' && isdigit(reg.name[1]))
        reg.byte_size = z_reg_byte_size;
      else if (reg.name[0] == 'p' && isdigit(reg.name[1]))
        reg.byte_size = vg;
      else if (strcmp(reg.name, "ffr") == 0)
        reg.byte_size = vg;
    }
    reg.byte_offset = LLDB_INVALID_INDEX32;
  }
}

static void
UpdateARM64SMERegistersInfos(DynamicRegisterInfo::reg_collection_range regs,
                             uint64_t svg) {
  for (auto &reg : regs) {
    if (strcmp(reg.name, "za") == 0) {
      // ZA is a register with size (svg*8) * (svg*8). A square essentially.
      reg.byte_size = (svg * 8) * (svg * 8);
    }
    reg.byte_offset = LLDB_INVALID_INDEX32;
  }
}

bool ArchitectureAArch64::ReconfigureRegisterInfo(DynamicRegisterInfo &reg_info,
                                                  DataExtractor &reg_data,
                                                  RegisterContext &reg_context

) const {
  // Once we start to reconfigure registers, we cannot read any of them.
  // So we must read VG and SVG up front.

  const uint64_t fail_value = LLDB_INVALID_ADDRESS;
  std::optional<uint64_t> vg_reg_value;
  const RegisterInfo *vg_reg_info = reg_info.GetRegisterInfo("vg");
  if (vg_reg_info) {
    uint32_t vg_reg_num = vg_reg_info->kinds[eRegisterKindLLDB];
    uint64_t reg_value =
        reg_context.ReadRegisterAsUnsigned(vg_reg_num, fail_value);
    if (reg_value != fail_value && reg_value <= 32)
      vg_reg_value = reg_value;
  }

  std::optional<uint64_t> svg_reg_value;
  const RegisterInfo *svg_reg_info = reg_info.GetRegisterInfo("svg");
  if (svg_reg_info) {
    uint32_t svg_reg_num = svg_reg_info->kinds[eRegisterKindLLDB];
    uint64_t reg_value =
        reg_context.ReadRegisterAsUnsigned(svg_reg_num, fail_value);
    if (reg_value != fail_value && reg_value <= 32)
      svg_reg_value = reg_value;
  }

  if (!vg_reg_value && !svg_reg_value)
    return false;

  auto regs = reg_info.registers<DynamicRegisterInfo::reg_collection_range>();
  if (vg_reg_value)
    UpdateARM64SVERegistersInfos(regs, *vg_reg_value);
  if (svg_reg_value)
    UpdateARM64SMERegistersInfos(regs, *svg_reg_value);

  // At this point if we have updated any registers, their offsets will all be
  // invalid. If we did, we need to update them all.
  reg_info.ConfigureOffsets();
  // From here we are able to read registers again.

  // Make a heap based buffer that is big enough to store all registers
  reg_data.SetData(
      std::make_shared<DataBufferHeap>(reg_info.GetRegisterDataByteSize(), 0));
  reg_data.SetByteOrder(reg_context.GetByteOrder());

  return true;
}
