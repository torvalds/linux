//===-- ARMBuildAttrs.cpp - ARM Build Attributes --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/ARMBuildAttributes.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/LEB128.h"
#include <iomanip>
#include <sstream>

using namespace llvm;

static const TagNameItem tagData[] = {
    {ARMBuildAttrs::File, "Tag_File"},
    {ARMBuildAttrs::Section, "Tag_Section"},
    {ARMBuildAttrs::Symbol, "Tag_Symbol"},
    {ARMBuildAttrs::CPU_raw_name, "Tag_CPU_raw_name"},
    {ARMBuildAttrs::CPU_name, "Tag_CPU_name"},
    {ARMBuildAttrs::CPU_arch, "Tag_CPU_arch"},
    {ARMBuildAttrs::CPU_arch_profile, "Tag_CPU_arch_profile"},
    {ARMBuildAttrs::ARM_ISA_use, "Tag_ARM_ISA_use"},
    {ARMBuildAttrs::THUMB_ISA_use, "Tag_THUMB_ISA_use"},
    {ARMBuildAttrs::FP_arch, "Tag_FP_arch"},
    {ARMBuildAttrs::WMMX_arch, "Tag_WMMX_arch"},
    {ARMBuildAttrs::Advanced_SIMD_arch, "Tag_Advanced_SIMD_arch"},
    {ARMBuildAttrs::MVE_arch, "Tag_MVE_arch"},
    {ARMBuildAttrs::PCS_config, "Tag_PCS_config"},
    {ARMBuildAttrs::ABI_PCS_R9_use, "Tag_ABI_PCS_R9_use"},
    {ARMBuildAttrs::ABI_PCS_RW_data, "Tag_ABI_PCS_RW_data"},
    {ARMBuildAttrs::ABI_PCS_RO_data, "Tag_ABI_PCS_RO_data"},
    {ARMBuildAttrs::ABI_PCS_GOT_use, "Tag_ABI_PCS_GOT_use"},
    {ARMBuildAttrs::ABI_PCS_wchar_t, "Tag_ABI_PCS_wchar_t"},
    {ARMBuildAttrs::ABI_FP_rounding, "Tag_ABI_FP_rounding"},
    {ARMBuildAttrs::ABI_FP_denormal, "Tag_ABI_FP_denormal"},
    {ARMBuildAttrs::ABI_FP_exceptions, "Tag_ABI_FP_exceptions"},
    {ARMBuildAttrs::ABI_FP_user_exceptions, "Tag_ABI_FP_user_exceptions"},
    {ARMBuildAttrs::ABI_FP_number_model, "Tag_ABI_FP_number_model"},
    {ARMBuildAttrs::ABI_align_needed, "Tag_ABI_align_needed"},
    {ARMBuildAttrs::ABI_align_preserved, "Tag_ABI_align_preserved"},
    {ARMBuildAttrs::ABI_enum_size, "Tag_ABI_enum_size"},
    {ARMBuildAttrs::ABI_HardFP_use, "Tag_ABI_HardFP_use"},
    {ARMBuildAttrs::ABI_VFP_args, "Tag_ABI_VFP_args"},
    {ARMBuildAttrs::ABI_WMMX_args, "Tag_ABI_WMMX_args"},
    {ARMBuildAttrs::ABI_optimization_goals, "Tag_ABI_optimization_goals"},
    {ARMBuildAttrs::ABI_FP_optimization_goals, "Tag_ABI_FP_optimization_goals"},
    {ARMBuildAttrs::compatibility, "Tag_compatibility"},
    {ARMBuildAttrs::CPU_unaligned_access, "Tag_CPU_unaligned_access"},
    {ARMBuildAttrs::FP_HP_extension, "Tag_FP_HP_extension"},
    {ARMBuildAttrs::ABI_FP_16bit_format, "Tag_ABI_FP_16bit_format"},
    {ARMBuildAttrs::MPextension_use, "Tag_MPextension_use"},
    {ARMBuildAttrs::DIV_use, "Tag_DIV_use"},
    {ARMBuildAttrs::DSP_extension, "Tag_DSP_extension"},
    {ARMBuildAttrs::PAC_extension, "Tag_PAC_extension"},
    {ARMBuildAttrs::BTI_extension, "Tag_BTI_extension"},
    {ARMBuildAttrs::BTI_use, "Tag_BTI_use"},
    {ARMBuildAttrs::PACRET_use, "Tag_PACRET_use"},
    {ARMBuildAttrs::nodefaults, "Tag_nodefaults"},
    {ARMBuildAttrs::also_compatible_with, "Tag_also_compatible_with"},
    {ARMBuildAttrs::T2EE_use, "Tag_T2EE_use"},
    {ARMBuildAttrs::conformance, "Tag_conformance"},
    {ARMBuildAttrs::Virtualization_use, "Tag_Virtualization_use"},

    // Legacy Names
    {ARMBuildAttrs::FP_arch, "Tag_VFP_arch"},
    {ARMBuildAttrs::FP_HP_extension, "Tag_VFP_HP_extension"},
    {ARMBuildAttrs::ABI_align_needed, "Tag_ABI_align8_needed"},
    {ARMBuildAttrs::ABI_align_preserved, "Tag_ABI_align8_preserved"},
};

constexpr TagNameMap ARMAttributeTags{tagData};
const TagNameMap &llvm::ARMBuildAttrs::getARMAttributeTags() {
  return ARMAttributeTags;
}
