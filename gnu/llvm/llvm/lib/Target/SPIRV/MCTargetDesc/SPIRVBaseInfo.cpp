//===-- SPIRVBaseInfo.cpp - Top level SPIRV definitions ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation for helper mnemonic lookup functions,
// versioning/capabilities/extensions getters for symbolic/named operands used
// in various SPIR-V instructions.
//
//===----------------------------------------------------------------------===//

#include "SPIRVBaseInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

namespace llvm {
namespace SPIRV {
struct SymbolicOperand {
  OperandCategory::OperandCategory Category;
  uint32_t Value;
  StringRef Mnemonic;
  uint32_t MinVersion;
  uint32_t MaxVersion;
};

struct ExtensionEntry {
  OperandCategory::OperandCategory Category;
  uint32_t Value;
  Extension::Extension ReqExtension;
};

struct CapabilityEntry {
  OperandCategory::OperandCategory Category;
  uint32_t Value;
  Capability::Capability ReqCapability;
};

using namespace OperandCategory;
using namespace Extension;
using namespace Capability;
using namespace InstructionSet;
#define GET_SymbolicOperands_DECL
#define GET_SymbolicOperands_IMPL
#define GET_ExtensionEntries_DECL
#define GET_ExtensionEntries_IMPL
#define GET_CapabilityEntries_DECL
#define GET_CapabilityEntries_IMPL
#define GET_ExtendedBuiltins_DECL
#define GET_ExtendedBuiltins_IMPL
#include "SPIRVGenTables.inc"
} // namespace SPIRV

std::string
getSymbolicOperandMnemonic(SPIRV::OperandCategory::OperandCategory Category,
                           int32_t Value) {
  const SPIRV::SymbolicOperand *Lookup =
      SPIRV::lookupSymbolicOperandByCategoryAndValue(Category, Value);
  // Value that encodes just one enum value.
  if (Lookup)
    return Lookup->Mnemonic.str();
  if (Category != SPIRV::OperandCategory::ImageOperandOperand &&
      Category != SPIRV::OperandCategory::FPFastMathModeOperand &&
      Category != SPIRV::OperandCategory::SelectionControlOperand &&
      Category != SPIRV::OperandCategory::LoopControlOperand &&
      Category != SPIRV::OperandCategory::FunctionControlOperand &&
      Category != SPIRV::OperandCategory::MemorySemanticsOperand &&
      Category != SPIRV::OperandCategory::MemoryOperandOperand &&
      Category != SPIRV::OperandCategory::KernelProfilingInfoOperand)
    return "UNKNOWN";
  // Value that encodes many enum values (one bit per enum value).
  std::string Name;
  std::string Separator;
  const SPIRV::SymbolicOperand *EnumValueInCategory =
      SPIRV::lookupSymbolicOperandByCategory(Category);

  while (EnumValueInCategory && EnumValueInCategory->Category == Category) {
    if ((EnumValueInCategory->Value != 0) &&
        (Value & EnumValueInCategory->Value)) {
      Name += Separator + EnumValueInCategory->Mnemonic.str();
      Separator = "|";
    }
    ++EnumValueInCategory;
  }

  return Name;
}

VersionTuple
getSymbolicOperandMinVersion(SPIRV::OperandCategory::OperandCategory Category,
                             uint32_t Value) {
  const SPIRV::SymbolicOperand *Lookup =
      SPIRV::lookupSymbolicOperandByCategoryAndValue(Category, Value);

  if (Lookup)
    return VersionTuple(Lookup->MinVersion / 10, Lookup->MinVersion % 10);

  return VersionTuple(0);
}

VersionTuple
getSymbolicOperandMaxVersion(SPIRV::OperandCategory::OperandCategory Category,
                             uint32_t Value) {
  const SPIRV::SymbolicOperand *Lookup =
      SPIRV::lookupSymbolicOperandByCategoryAndValue(Category, Value);

  if (Lookup)
    return VersionTuple(Lookup->MaxVersion / 10, Lookup->MaxVersion % 10);

  return VersionTuple();
}

CapabilityList
getSymbolicOperandCapabilities(SPIRV::OperandCategory::OperandCategory Category,
                               uint32_t Value) {
  const SPIRV::CapabilityEntry *Capability =
      SPIRV::lookupCapabilityByCategoryAndValue(Category, Value);

  CapabilityList Capabilities;
  while (Capability && Capability->Category == Category &&
         Capability->Value == Value) {
    Capabilities.push_back(
        static_cast<SPIRV::Capability::Capability>(Capability->ReqCapability));
    ++Capability;
  }

  return Capabilities;
}

CapabilityList
getCapabilitiesEnabledByExtension(SPIRV::Extension::Extension Extension) {
  const SPIRV::ExtensionEntry *Entry =
      SPIRV::lookupSymbolicOperandsEnabledByExtension(
          Extension, SPIRV::OperandCategory::CapabilityOperand);

  CapabilityList Capabilities;
  while (Entry &&
         Entry->Category == SPIRV::OperandCategory::CapabilityOperand &&
         Entry->ReqExtension == Extension) {
    Capabilities.push_back(
        static_cast<SPIRV::Capability::Capability>(Entry->Value));
    ++Entry;
  }

  return Capabilities;
}

ExtensionList
getSymbolicOperandExtensions(SPIRV::OperandCategory::OperandCategory Category,
                             uint32_t Value) {
  const SPIRV::ExtensionEntry *Extension =
      SPIRV::lookupExtensionByCategoryAndValue(Category, Value);

  ExtensionList Extensions;
  while (Extension && Extension->Category == Category &&
         Extension->Value == Value) {
    Extensions.push_back(
        static_cast<SPIRV::Extension::Extension>(Extension->ReqExtension));
    ++Extension;
  }

  return Extensions;
}

std::string getLinkStringForBuiltIn(SPIRV::BuiltIn::BuiltIn BuiltInValue) {
  const SPIRV::SymbolicOperand *Lookup =
      SPIRV::lookupSymbolicOperandByCategoryAndValue(
          SPIRV::OperandCategory::BuiltInOperand, BuiltInValue);

  if (Lookup)
    return "__spirv_BuiltIn" + Lookup->Mnemonic.str();
  return "UNKNOWN_BUILTIN";
}

bool getSpirvBuiltInIdByName(llvm::StringRef Name,
                             SPIRV::BuiltIn::BuiltIn &BI) {
  const std::string Prefix = "__spirv_BuiltIn";
  if (!Name.starts_with(Prefix))
    return false;

  const SPIRV::SymbolicOperand *Lookup =
      SPIRV::lookupSymbolicOperandByCategoryAndMnemonic(
          SPIRV::OperandCategory::BuiltInOperand,
          Name.drop_front(Prefix.length()));

  if (!Lookup)
    return false;

  BI = static_cast<SPIRV::BuiltIn::BuiltIn>(Lookup->Value);
  return true;
}

std::string getExtInstSetName(SPIRV::InstructionSet::InstructionSet Set) {
  switch (Set) {
  case SPIRV::InstructionSet::OpenCL_std:
    return "OpenCL.std";
  case SPIRV::InstructionSet::GLSL_std_450:
    return "GLSL.std.450";
  case SPIRV::InstructionSet::NonSemantic_Shader_DebugInfo_100:
    return "NonSemantic.Shader.DebugInfo.100";
  case SPIRV::InstructionSet::SPV_AMD_shader_trinary_minmax:
    return "SPV_AMD_shader_trinary_minmax";
  }
  return "UNKNOWN_EXT_INST_SET";
}

SPIRV::InstructionSet::InstructionSet
getExtInstSetFromString(std::string SetName) {
  for (auto Set :
       {SPIRV::InstructionSet::GLSL_std_450, SPIRV::InstructionSet::OpenCL_std,
        SPIRV::InstructionSet::NonSemantic_Shader_DebugInfo_100}) {
    if (SetName == getExtInstSetName(Set))
      return Set;
  }
  llvm_unreachable("UNKNOWN_EXT_INST_SET");
}

std::string getExtInstName(SPIRV::InstructionSet::InstructionSet Set,
                           uint32_t InstructionNumber) {
  const SPIRV::ExtendedBuiltin *Lookup =
      SPIRV::lookupExtendedBuiltinBySetAndNumber(Set, InstructionNumber);

  if (!Lookup)
    return "UNKNOWN_EXT_INST";

  return Lookup->Name.str();
}
} // namespace llvm
