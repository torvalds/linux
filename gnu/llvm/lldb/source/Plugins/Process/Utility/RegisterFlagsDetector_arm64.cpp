//===-- RegisterFlagsDetector_arm64.cpp -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RegisterFlagsDetector_arm64.h"
#include "lldb/lldb-private-types.h"

// This file is built on all systems because it is used by native processes and
// core files, so we manually define the needed HWCAP values here.
// These values are the same for Linux and FreeBSD.

#define HWCAP_FPHP (1ULL << 9)
#define HWCAP_ASIMDHP (1ULL << 10)
#define HWCAP_DIT (1ULL << 24)
#define HWCAP_SSBS (1ULL << 28)

#define HWCAP2_BTI (1ULL << 17)
#define HWCAP2_MTE (1ULL << 18)
#define HWCAP2_AFP (1ULL << 20)
#define HWCAP2_SME (1ULL << 23)
#define HWCAP2_EBF16 (1ULL << 32)

using namespace lldb_private;

Arm64RegisterFlagsDetector::Fields
Arm64RegisterFlagsDetector::DetectSVCRFields(uint64_t hwcap, uint64_t hwcap2) {
  (void)hwcap;

  if (!(hwcap2 & HWCAP2_SME))
    return {};

  // Represents the pseudo register that lldb-server builds, which itself
  // matches the architectural register SCVR. The fields match SVCR in the Arm
  // manual.
  return {
      {"ZA", 1},
      {"SM", 0},
  };
}

Arm64RegisterFlagsDetector::Fields
Arm64RegisterFlagsDetector::DetectMTECtrlFields(uint64_t hwcap,
                                                uint64_t hwcap2) {
  (void)hwcap;

  if (!(hwcap2 & HWCAP2_MTE))
    return {};

  // Represents the contents of NT_ARM_TAGGED_ADDR_CTRL and the value passed
  // to prctl(PR_TAGGED_ADDR_CTRL...). Fields are derived from the defines
  // used to build the value.

  static const FieldEnum tcf_enum(
      "tcf_enum",
      {{0, "TCF_NONE"}, {1, "TCF_SYNC"}, {2, "TCF_ASYNC"}, {3, "TCF_ASYMM"}});
  return {{"TAGS", 3, 18}, // 16 bit bitfield shifted up by PR_MTE_TAG_SHIFT.
          {"TCF", 1, 2, &tcf_enum},
          {"TAGGED_ADDR_ENABLE", 0}};
}

Arm64RegisterFlagsDetector::Fields
Arm64RegisterFlagsDetector::DetectFPCRFields(uint64_t hwcap, uint64_t hwcap2) {
  static const FieldEnum rmode_enum(
      "rmode_enum", {{0, "RN"}, {1, "RP"}, {2, "RM"}, {3, "RZ"}});

  std::vector<RegisterFlags::Field> fpcr_fields{
      {"AHP", 26}, {"DN", 25}, {"FZ", 24}, {"RMode", 22, 23, &rmode_enum},
      // Bits 21-20 are "Stride" which is unused in AArch64 state.
  };

  // FEAT_FP16 is indicated by the presence of FPHP (floating point half
  // precision) and ASIMDHP (Advanced SIMD half precision) features.
  if ((hwcap & HWCAP_FPHP) && (hwcap & HWCAP_ASIMDHP))
    fpcr_fields.push_back({"FZ16", 19});

  // Bits 18-16 are "Len" which is unused in AArch64 state.

  fpcr_fields.push_back({"IDE", 15});

  // Bit 14 is unused.
  if (hwcap2 & HWCAP2_EBF16)
    fpcr_fields.push_back({"EBF", 13});

  fpcr_fields.push_back({"IXE", 12});
  fpcr_fields.push_back({"UFE", 11});
  fpcr_fields.push_back({"OFE", 10});
  fpcr_fields.push_back({"DZE", 9});
  fpcr_fields.push_back({"IOE", 8});
  // Bits 7-3 reserved.

  if (hwcap2 & HWCAP2_AFP) {
    fpcr_fields.push_back({"NEP", 2});
    fpcr_fields.push_back({"AH", 1});
    fpcr_fields.push_back({"FIZ", 0});
  }

  return fpcr_fields;
}

Arm64RegisterFlagsDetector::Fields
Arm64RegisterFlagsDetector::DetectFPSRFields(uint64_t hwcap, uint64_t hwcap2) {
  // fpsr's contents are constant.
  (void)hwcap;
  (void)hwcap2;

  return {
      // Bits 31-28 are N/Z/C/V, only used by AArch32.
      {"QC", 27},
      // Bits 26-8 reserved.
      {"IDC", 7},
      // Bits 6-5 reserved.
      {"IXC", 4},
      {"UFC", 3},
      {"OFC", 2},
      {"DZC", 1},
      {"IOC", 0},
  };
}

Arm64RegisterFlagsDetector::Fields
Arm64RegisterFlagsDetector::DetectCPSRFields(uint64_t hwcap, uint64_t hwcap2) {
  // The fields here are a combination of the Arm manual's SPSR_EL1,
  // plus a few changes where Linux has decided not to make use of them at all,
  // or at least not from userspace.

  // Status bits that are always present.
  std::vector<RegisterFlags::Field> cpsr_fields{
      {"N", 31}, {"Z", 30}, {"C", 29}, {"V", 28},
      // Bits 27-26 reserved.
  };

  if (hwcap2 & HWCAP2_MTE)
    cpsr_fields.push_back({"TCO", 25});
  if (hwcap & HWCAP_DIT)
    cpsr_fields.push_back({"DIT", 24});

  // UAO and PAN are bits 23 and 22 and have no meaning for userspace so
  // are treated as reserved by the kernels.

  cpsr_fields.push_back({"SS", 21});
  cpsr_fields.push_back({"IL", 20});
  // Bits 19-14 reserved.

  // Bit 13, ALLINT, requires FEAT_NMI that isn't relevant to userspace, and we
  // can't detect either, don't show this field.
  if (hwcap & HWCAP_SSBS)
    cpsr_fields.push_back({"SSBS", 12});
  if (hwcap2 & HWCAP2_BTI)
    cpsr_fields.push_back({"BTYPE", 10, 11});

  cpsr_fields.push_back({"D", 9});
  cpsr_fields.push_back({"A", 8});
  cpsr_fields.push_back({"I", 7});
  cpsr_fields.push_back({"F", 6});
  // Bit 5 reserved
  // Called "M" in the ARMARM.
  cpsr_fields.push_back({"nRW", 4});
  // This is a 4 bit field M[3:0] in the ARMARM, we split it into parts.
  cpsr_fields.push_back({"EL", 2, 3});
  // Bit 1 is unused and expected to be 0.
  cpsr_fields.push_back({"SP", 0});

  return cpsr_fields;
}

void Arm64RegisterFlagsDetector::DetectFields(uint64_t hwcap, uint64_t hwcap2) {
  for (auto &reg : m_registers)
    reg.m_flags.SetFields(reg.m_detector(hwcap, hwcap2));
  m_has_detected = true;
}

void Arm64RegisterFlagsDetector::UpdateRegisterInfo(
    const RegisterInfo *reg_info, uint32_t num_regs) {
  assert(m_has_detected &&
         "Must call DetectFields before updating register info.");

  // Register names will not be duplicated, so we do not want to compare against
  // one if it has already been found. Each time we find one, we erase it from
  // this list.
  std::vector<std::pair<llvm::StringRef, const RegisterFlags *>>
      search_registers;
  for (const auto &reg : m_registers) {
    // It is possible that a register is all extension dependent fields, and
    // none of them are present.
    if (reg.m_flags.GetFields().size())
      search_registers.push_back({reg.m_name, &reg.m_flags});
  }

  // Walk register information while there are registers we know need
  // to be updated. Example:
  // Register information: [a, b, c, d]
  // To be patched: [b, c]
  // * a != b, a != c, do nothing and move on.
  // * b == b, patch b, new patch list is [c], move on.
  // * c == c, patch c, patch list is empty, exit early without looking at d.
  for (uint32_t idx = 0; idx < num_regs && search_registers.size();
       ++idx, ++reg_info) {
    auto reg_it = std::find_if(
        search_registers.cbegin(), search_registers.cend(),
        [reg_info](auto reg) { return reg.first == reg_info->name; });

    if (reg_it != search_registers.end()) {
      // Attach the field information.
      reg_info->flags_type = reg_it->second;
      // We do not expect to see this name again so don't look for it again.
      search_registers.erase(reg_it);
    }
  }

  // We do not assert that search_registers is empty here, because it may
  // contain registers from optional extensions that are not present on the
  // current target.
}
