//===- ARMAttributeParser.h - ARM Attribute Information Printer -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ARMATTRIBUTEPARSER_H
#define LLVM_SUPPORT_ARMATTRIBUTEPARSER_H

#include "ARMBuildAttributes.h"
#include "ELFAttributeParser.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

namespace llvm {

class ScopedPrinter;

class ARMAttributeParser : public ELFAttributeParser {
  struct DisplayHandler {
    ARMBuildAttrs::AttrType attribute;
    Error (ARMAttributeParser::*routine)(ARMBuildAttrs::AttrType);
  };
  static const DisplayHandler displayRoutines[];

  Error handler(uint64_t tag, bool &handled) override;

  Error stringAttribute(ARMBuildAttrs::AttrType tag);

  Error CPU_arch(ARMBuildAttrs::AttrType tag);
  Error CPU_arch_profile(ARMBuildAttrs::AttrType tag);
  Error ARM_ISA_use(ARMBuildAttrs::AttrType tag);
  Error THUMB_ISA_use(ARMBuildAttrs::AttrType tag);
  Error FP_arch(ARMBuildAttrs::AttrType tag);
  Error WMMX_arch(ARMBuildAttrs::AttrType tag);
  Error Advanced_SIMD_arch(ARMBuildAttrs::AttrType tag);
  Error MVE_arch(ARMBuildAttrs::AttrType tag);
  Error PCS_config(ARMBuildAttrs::AttrType tag);
  Error ABI_PCS_R9_use(ARMBuildAttrs::AttrType tag);
  Error ABI_PCS_RW_data(ARMBuildAttrs::AttrType tag);
  Error ABI_PCS_RO_data(ARMBuildAttrs::AttrType tag);
  Error ABI_PCS_GOT_use(ARMBuildAttrs::AttrType tag);
  Error ABI_PCS_wchar_t(ARMBuildAttrs::AttrType tag);
  Error ABI_FP_rounding(ARMBuildAttrs::AttrType tag);
  Error ABI_FP_denormal(ARMBuildAttrs::AttrType tag);
  Error ABI_FP_exceptions(ARMBuildAttrs::AttrType tag);
  Error ABI_FP_user_exceptions(ARMBuildAttrs::AttrType tag);
  Error ABI_FP_number_model(ARMBuildAttrs::AttrType tag);
  Error ABI_align_needed(ARMBuildAttrs::AttrType tag);
  Error ABI_align_preserved(ARMBuildAttrs::AttrType tag);
  Error ABI_enum_size(ARMBuildAttrs::AttrType tag);
  Error ABI_HardFP_use(ARMBuildAttrs::AttrType tag);
  Error ABI_VFP_args(ARMBuildAttrs::AttrType tag);
  Error ABI_WMMX_args(ARMBuildAttrs::AttrType tag);
  Error ABI_optimization_goals(ARMBuildAttrs::AttrType tag);
  Error ABI_FP_optimization_goals(ARMBuildAttrs::AttrType tag);
  Error compatibility(ARMBuildAttrs::AttrType tag);
  Error CPU_unaligned_access(ARMBuildAttrs::AttrType tag);
  Error FP_HP_extension(ARMBuildAttrs::AttrType tag);
  Error ABI_FP_16bit_format(ARMBuildAttrs::AttrType tag);
  Error MPextension_use(ARMBuildAttrs::AttrType tag);
  Error DIV_use(ARMBuildAttrs::AttrType tag);
  Error DSP_extension(ARMBuildAttrs::AttrType tag);
  Error T2EE_use(ARMBuildAttrs::AttrType tag);
  Error Virtualization_use(ARMBuildAttrs::AttrType tag);
  Error PAC_extension(ARMBuildAttrs::AttrType tag);
  Error BTI_extension(ARMBuildAttrs::AttrType tag);
  Error PACRET_use(ARMBuildAttrs::AttrType tag);
  Error BTI_use(ARMBuildAttrs::AttrType tag);
  Error nodefaults(ARMBuildAttrs::AttrType tag);
  Error also_compatible_with(ARMBuildAttrs::AttrType tag);

public:
  ARMAttributeParser(ScopedPrinter *sw)
      : ELFAttributeParser(sw, ARMBuildAttrs::getARMAttributeTags(), "aeabi") {}
  ARMAttributeParser()
      : ELFAttributeParser(ARMBuildAttrs::getARMAttributeTags(), "aeabi") {}
};
}

#endif
