//===-- ABIX86.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIMacOSX_i386.h"
#include "ABISysV_i386.h"
#include "ABISysV_x86_64.h"
#include "ABIWindows_x86_64.h"
#include "ABIX86.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Target/Process.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(ABIX86)

void ABIX86::Initialize() {
  ABIMacOSX_i386::Initialize();
  ABISysV_i386::Initialize();
  ABISysV_x86_64::Initialize();
  ABIWindows_x86_64::Initialize();
}

void ABIX86::Terminate() {
  ABIMacOSX_i386::Terminate();
  ABISysV_i386::Terminate();
  ABISysV_x86_64::Terminate();
  ABIWindows_x86_64::Terminate();
}

namespace {
enum RegKind {
  GPR32,
  GPR16,
  GPR8h,
  GPR8,
  MM,
  YMM_YMMh,
  YMM_XMM,

  RegKindCount
};
}

struct RegData {
  RegKind subreg_kind;
  llvm::StringRef subreg_name;
  std::optional<uint32_t> base_index;
};

static void
addPartialRegisters(std::vector<DynamicRegisterInfo::Register> &regs,
                    llvm::ArrayRef<RegData *> subregs, uint32_t base_size,
                    lldb::Encoding encoding, lldb::Format format,
                    uint32_t subreg_size, uint32_t subreg_offset = 0) {
  for (const RegData *subreg : subregs) {
    assert(subreg);
    uint32_t base_index = *subreg->base_index;
    DynamicRegisterInfo::Register &full_reg = regs[base_index];
    if (full_reg.byte_size != base_size)
      continue;

    lldb_private::DynamicRegisterInfo::Register new_reg{
        lldb_private::ConstString(subreg->subreg_name),
        lldb_private::ConstString(),
        lldb_private::ConstString("supplementary registers"),
        subreg_size,
        LLDB_INVALID_INDEX32,
        encoding,
        format,
        LLDB_INVALID_REGNUM,
        LLDB_INVALID_REGNUM,
        LLDB_INVALID_REGNUM,
        LLDB_INVALID_REGNUM,
        {base_index},
        {},
        subreg_offset};

    addSupplementaryRegister(regs, new_reg);
  }
}

static void
addCombinedRegisters(std::vector<DynamicRegisterInfo::Register> &regs,
                     llvm::ArrayRef<RegData *> subregs1,
                     llvm::ArrayRef<RegData *> subregs2, uint32_t base_size,
                     lldb::Encoding encoding, lldb::Format format) {
  for (auto it : llvm::zip(subregs1, subregs2)) {
    RegData *regdata1, *regdata2;
    std::tie(regdata1, regdata2) = it;
    assert(regdata1);
    assert(regdata2);

    // verify that we've got matching target registers
    if (regdata1->subreg_name != regdata2->subreg_name)
      continue;

    uint32_t base_index1 = *regdata1->base_index;
    uint32_t base_index2 = *regdata2->base_index;
    if (regs[base_index1].byte_size != base_size ||
        regs[base_index2].byte_size != base_size)
      continue;

    lldb_private::DynamicRegisterInfo::Register new_reg{
        lldb_private::ConstString(regdata1->subreg_name),
        lldb_private::ConstString(),
        lldb_private::ConstString("supplementary registers"),
        base_size * 2,
        LLDB_INVALID_INDEX32,
        encoding,
        format,
        LLDB_INVALID_REGNUM,
        LLDB_INVALID_REGNUM,
        LLDB_INVALID_REGNUM,
        LLDB_INVALID_REGNUM,
        {base_index1, base_index2},
        {}};

    addSupplementaryRegister(regs, new_reg);
  }
}

typedef llvm::SmallDenseMap<llvm::StringRef, llvm::SmallVector<RegData, 4>, 64>
    BaseRegToRegsMap;

#define GPRh(l)                                                                \
  {                                                                            \
    is64bit ? BaseRegToRegsMap::value_type("r" l "x",                          \
                                           {{GPR32, "e" l "x", std::nullopt},  \
                                            {GPR16, l "x", std::nullopt},      \
                                            {GPR8h, l "h", std::nullopt},      \
                                            {GPR8, l "l", std::nullopt}})      \
            : BaseRegToRegsMap::value_type("e" l "x",                          \
                                           {{GPR16, l "x", std::nullopt},      \
                                            {GPR8h, l "h", std::nullopt},      \
                                            {GPR8, l "l", std::nullopt}})      \
  }

#define GPR(r16)                                                               \
  {                                                                            \
    is64bit ? BaseRegToRegsMap::value_type("r" r16,                            \
                                           {{GPR32, "e" r16, std::nullopt},    \
                                            {GPR16, r16, std::nullopt},        \
                                            {GPR8, r16 "l", std::nullopt}})    \
            : BaseRegToRegsMap::value_type(                                    \
                  "e" r16,                                                     \
                  {{GPR16, r16, std::nullopt}, {GPR8, r16 "l", std::nullopt}}) \
  }

#define GPR64(n)                                                               \
  {                                                                            \
    BaseRegToRegsMap::value_type("r" #n, {{GPR32, "r" #n "d", std::nullopt},   \
                                          {GPR16, "r" #n "w", std::nullopt},   \
                                          {GPR8, "r" #n "l", std::nullopt}})   \
  }

#define STMM(n)                                                                \
  { BaseRegToRegsMap::value_type("st" #n, {{MM, "mm" #n, std::nullopt}}) }

#define YMM(n)                                                                 \
  {BaseRegToRegsMap::value_type("ymm" #n "h",                                  \
                                {{YMM_YMMh, "ymm" #n, std::nullopt}})},        \
  {                                                                            \
    BaseRegToRegsMap::value_type("xmm" #n,                                     \
                                 {{YMM_XMM, "ymm" #n, std::nullopt}})          \
  }

BaseRegToRegsMap makeBaseRegMap(bool is64bit) {
  BaseRegToRegsMap out{
      {// GPRs common to amd64 & i386
       GPRh("a"), GPRh("b"), GPRh("c"), GPRh("d"), GPR("si"), GPR("di"),
       GPR("bp"), GPR("sp"),

       // ST/MM registers
       STMM(0), STMM(1), STMM(2), STMM(3), STMM(4), STMM(5), STMM(6), STMM(7),

       // lower YMM registers (common to amd64 & i386)
       YMM(0), YMM(1), YMM(2), YMM(3), YMM(4), YMM(5), YMM(6), YMM(7)}};

  if (is64bit) {
    BaseRegToRegsMap amd64_regs{{// GPRs specific to amd64
                                 GPR64(8), GPR64(9), GPR64(10), GPR64(11),
                                 GPR64(12), GPR64(13), GPR64(14), GPR64(15),

                                 // higher YMM registers (specific to amd64)
                                 YMM(8), YMM(9), YMM(10), YMM(11), YMM(12),
                                 YMM(13), YMM(14), YMM(15)}};
    out.insert(amd64_regs.begin(), amd64_regs.end());
  }

  return out;
}

void ABIX86::AugmentRegisterInfo(
    std::vector<DynamicRegisterInfo::Register> &regs) {
  MCBasedABI::AugmentRegisterInfo(regs);

  ProcessSP process_sp = GetProcessSP();
  if (!process_sp)
    return;

  uint32_t gpr_base_size =
      process_sp->GetTarget().GetArchitecture().GetAddressByteSize();

  // primary map from a base register to its subregisters
  BaseRegToRegsMap base_reg_map = makeBaseRegMap(gpr_base_size == 8);
  // set used for fast matching of register names to subregisters
  llvm::SmallDenseSet<llvm::StringRef, 64> subreg_name_set;
  // convenience array providing access to all subregisters of given kind,
  // sorted by base register index
  std::array<llvm::SmallVector<RegData *, 16>, RegKindCount> subreg_by_kind;

  // prepare the set of all known subregisters
  for (const auto &x : base_reg_map) {
    for (const auto &subreg : x.second)
      subreg_name_set.insert(subreg.subreg_name);
  }

  // iterate over all registers
  for (const auto &x : llvm::enumerate(regs)) {
    llvm::StringRef reg_name = x.value().name.GetStringRef();
    // abort if at least one sub-register is already present
    if (llvm::is_contained(subreg_name_set, reg_name))
      return;

    auto found = base_reg_map.find(reg_name);
    if (found == base_reg_map.end())
      continue;

    for (auto &subreg : found->second) {
      // fill in base register indices
      subreg.base_index = x.index();
      // fill subreg_by_kind map-array
      subreg_by_kind[static_cast<size_t>(subreg.subreg_kind)].push_back(
          &subreg);
    }
  }

  // now add registers by kind
  addPartialRegisters(regs, subreg_by_kind[GPR32], gpr_base_size, eEncodingUint,
                      eFormatHex, 4);
  addPartialRegisters(regs, subreg_by_kind[GPR16], gpr_base_size, eEncodingUint,
                      eFormatHex, 2);
  addPartialRegisters(regs, subreg_by_kind[GPR8h], gpr_base_size, eEncodingUint,
                      eFormatHex, 1, 1);
  addPartialRegisters(regs, subreg_by_kind[GPR8], gpr_base_size, eEncodingUint,
                      eFormatHex, 1);

  addPartialRegisters(regs, subreg_by_kind[MM], 10, eEncodingUint, eFormatHex,
                      8);

  addCombinedRegisters(regs, subreg_by_kind[YMM_XMM], subreg_by_kind[YMM_YMMh],
                       16, eEncodingVector, eFormatVectorOfUInt8);
}
