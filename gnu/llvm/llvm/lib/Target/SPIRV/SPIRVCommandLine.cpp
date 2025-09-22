//===--- SPIRVCommandLine.cpp ---- Command Line Options ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains definitions of classes and functions needed for
// processing, parsing, and using CLI options for the SPIR-V backend.
//
//===----------------------------------------------------------------------===//

#include "SPIRVCommandLine.h"
#include "llvm/ADT/StringRef.h"
#include <algorithm>
#include <map>

#define DEBUG_TYPE "spirv-commandline"

using namespace llvm;

static const std::map<std::string, SPIRV::Extension::Extension>
    SPIRVExtensionMap = {
        {"SPV_EXT_shader_atomic_float_add",
         SPIRV::Extension::Extension::SPV_EXT_shader_atomic_float_add},
        {"SPV_EXT_shader_atomic_float16_add",
         SPIRV::Extension::Extension::SPV_EXT_shader_atomic_float16_add},
        {"SPV_EXT_shader_atomic_float_min_max",
         SPIRV::Extension::Extension::SPV_EXT_shader_atomic_float_min_max},
        {"SPV_INTEL_arbitrary_precision_integers",
         SPIRV::Extension::Extension::SPV_INTEL_arbitrary_precision_integers},
        {"SPV_INTEL_cache_controls",
         SPIRV::Extension::Extension::SPV_INTEL_cache_controls},
        {"SPV_INTEL_global_variable_fpga_decorations",
         SPIRV::Extension::Extension::
             SPV_INTEL_global_variable_fpga_decorations},
        {"SPV_INTEL_global_variable_host_access",
         SPIRV::Extension::Extension::SPV_INTEL_global_variable_host_access},
        {"SPV_INTEL_optnone", SPIRV::Extension::Extension::SPV_INTEL_optnone},
        {"SPV_INTEL_usm_storage_classes",
         SPIRV::Extension::Extension::SPV_INTEL_usm_storage_classes},
        {"SPV_INTEL_subgroups",
         SPIRV::Extension::Extension::SPV_INTEL_subgroups},
        {"SPV_KHR_uniform_group_instructions",
         SPIRV::Extension::Extension::SPV_KHR_uniform_group_instructions},
        {"SPV_KHR_no_integer_wrap_decoration",
         SPIRV::Extension::Extension::SPV_KHR_no_integer_wrap_decoration},
        {"SPV_KHR_float_controls",
         SPIRV::Extension::Extension::SPV_KHR_float_controls},
        {"SPV_KHR_expect_assume",
         SPIRV::Extension::Extension::SPV_KHR_expect_assume},
        {"SPV_KHR_bit_instructions",
         SPIRV::Extension::Extension::SPV_KHR_bit_instructions},
        {"SPV_KHR_linkonce_odr",
         SPIRV::Extension::Extension::SPV_KHR_linkonce_odr},
        {"SPV_INTEL_inline_assembly",
         SPIRV::Extension::Extension::SPV_INTEL_inline_assembly},
        {"SPV_INTEL_bfloat16_conversion",
         SPIRV::Extension::Extension::SPV_INTEL_bfloat16_conversion},
        {"SPV_KHR_subgroup_rotate",
         SPIRV::Extension::Extension::SPV_KHR_subgroup_rotate},
        {"SPV_INTEL_variable_length_array",
         SPIRV::Extension::Extension::SPV_INTEL_variable_length_array},
        {"SPV_INTEL_function_pointers",
         SPIRV::Extension::Extension::SPV_INTEL_function_pointers},
        {"SPV_KHR_shader_clock",
         SPIRV::Extension::Extension::SPV_KHR_shader_clock},
        {"SPV_KHR_cooperative_matrix",
         SPIRV::Extension::Extension::SPV_KHR_cooperative_matrix},
};

bool SPIRVExtensionsParser::parse(cl::Option &O, llvm::StringRef ArgName,
                                  llvm::StringRef ArgValue,
                                  std::set<SPIRV::Extension::Extension> &Vals) {
  llvm::SmallVector<llvm::StringRef, 10> Tokens;
  ArgValue.split(Tokens, ",", -1, false);
  std::sort(Tokens.begin(), Tokens.end());

  std::set<SPIRV::Extension::Extension> EnabledExtensions;

  for (const auto &Token : Tokens) {
    if (Token == "all") {
      for (const auto &[ExtensionName, ExtensionEnum] : SPIRVExtensionMap)
        EnabledExtensions.insert(ExtensionEnum);

      continue;
    }

    if (Token.empty() || (!Token.starts_with("+") && !Token.starts_with("-")))
      return O.error("Invalid extension list format: " + Token.str());

    llvm::StringRef ExtensionName = Token.substr(1);
    auto NameValuePair = SPIRVExtensionMap.find(ExtensionName.str());

    if (NameValuePair == SPIRVExtensionMap.end())
      return O.error("Unknown SPIR-V extension: " + Token.str());

    if (Token.starts_with("+")) {
      EnabledExtensions.insert(NameValuePair->second);
    } else if (EnabledExtensions.count(NameValuePair->second)) {
      if (std::find(Tokens.begin(), Tokens.end(), "+" + ExtensionName.str()) !=
          Tokens.end())
        return O.error(
            "Extension cannot be allowed and disallowed at the same time: " +
            ExtensionName.str());

      EnabledExtensions.erase(NameValuePair->second);
    }
  }

  Vals = std::move(EnabledExtensions);
  return false;
}
