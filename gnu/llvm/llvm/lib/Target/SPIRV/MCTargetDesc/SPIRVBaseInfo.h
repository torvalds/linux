//===-- SPIRVBaseInfo.h - Top level SPIRV definitions -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains TableGen generated enum definitions, mnemonic lookup
// functions, versioning/capabilities/extensions getters for symbolic/named
// operands for various SPIR-V instructions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SPIRV_SPIRVSYMBOLICOPERANDS_H
#define LLVM_LIB_TARGET_SPIRV_SPIRVSYMBOLICOPERANDS_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/VersionTuple.h"
#include <string>

namespace llvm {
namespace SPIRV {
namespace OperandCategory {
#define GET_OperandCategory_DECL
#include "SPIRVGenTables.inc"
} // namespace OperandCategory

namespace Extension {
#define GET_Extension_DECL
#include "SPIRVGenTables.inc"
} // namespace Extension

namespace Capability {
#define GET_Capability_DECL
#include "SPIRVGenTables.inc"
} // namespace Capability

namespace SourceLanguage {
#define GET_SourceLanguage_DECL
#include "SPIRVGenTables.inc"
} // namespace SourceLanguage

namespace AddressingModel {
#define GET_AddressingModel_DECL
#include "SPIRVGenTables.inc"
} // namespace AddressingModel

namespace ExecutionModel {
#define GET_ExecutionModel_DECL
#include "SPIRVGenTables.inc"
} // namespace ExecutionModel

namespace MemoryModel {
#define GET_MemoryModel_DECL
#include "SPIRVGenTables.inc"
} // namespace MemoryModel

namespace ExecutionMode {
#define GET_ExecutionMode_DECL
#include "SPIRVGenTables.inc"
} // namespace ExecutionMode

namespace StorageClass {
#define GET_StorageClass_DECL
#include "SPIRVGenTables.inc"
} // namespace StorageClass

namespace Dim {
#define GET_Dim_DECL
#include "SPIRVGenTables.inc"
} // namespace Dim

namespace SamplerAddressingMode {
#define GET_SamplerAddressingMode_DECL
#include "SPIRVGenTables.inc"
} // namespace SamplerAddressingMode

namespace SamplerFilterMode {
#define GET_SamplerFilterMode_DECL
#include "SPIRVGenTables.inc"
} // namespace SamplerFilterMode

namespace ImageFormat {
#define GET_ImageFormat_DECL
#include "SPIRVGenTables.inc"
} // namespace ImageFormat

namespace ImageChannelOrder {
#define GET_ImageChannelOrder_DECL
#include "SPIRVGenTables.inc"
} // namespace ImageChannelOrder

namespace ImageChannelDataType {
#define GET_ImageChannelDataType_DECL
#include "SPIRVGenTables.inc"
} // namespace ImageChannelDataType

namespace ImageOperand {
#define GET_ImageOperand_DECL
#include "SPIRVGenTables.inc"
} // namespace ImageOperand

namespace FPFastMathMode {
#define GET_FPFastMathMode_DECL
#include "SPIRVGenTables.inc"
} // namespace FPFastMathMode

namespace FPRoundingMode {
#define GET_FPRoundingMode_DECL
#include "SPIRVGenTables.inc"
} // namespace FPRoundingMode

namespace LinkageType {
#define GET_LinkageType_DECL
#include "SPIRVGenTables.inc"
} // namespace LinkageType

namespace AccessQualifier {
#define GET_AccessQualifier_DECL
#include "SPIRVGenTables.inc"
} // namespace AccessQualifier

namespace FunctionParameterAttribute {
#define GET_FunctionParameterAttribute_DECL
#include "SPIRVGenTables.inc"
} // namespace FunctionParameterAttribute

namespace Decoration {
#define GET_Decoration_DECL
#include "SPIRVGenTables.inc"
} // namespace Decoration

namespace BuiltIn {
#define GET_BuiltIn_DECL
#include "SPIRVGenTables.inc"
} // namespace BuiltIn

namespace SelectionControl {
#define GET_SelectionControl_DECL
#include "SPIRVGenTables.inc"
} // namespace SelectionControl

namespace LoopControl {
#define GET_LoopControl_DECL
#include "SPIRVGenTables.inc"
} // namespace LoopControl

namespace FunctionControl {
#define GET_FunctionControl_DECL
#include "SPIRVGenTables.inc"
} // namespace FunctionControl

namespace MemorySemantics {
#define GET_MemorySemantics_DECL
#include "SPIRVGenTables.inc"
} // namespace MemorySemantics

namespace MemoryOperand {
#define GET_MemoryOperand_DECL
#include "SPIRVGenTables.inc"
} // namespace MemoryOperand

namespace Scope {
#define GET_Scope_DECL
#include "SPIRVGenTables.inc"
} // namespace Scope

namespace GroupOperation {
#define GET_GroupOperation_DECL
#include "SPIRVGenTables.inc"
} // namespace GroupOperation

namespace KernelEnqueueFlags {
#define GET_KernelEnqueueFlags_DECL
#include "SPIRVGenTables.inc"
} // namespace KernelEnqueueFlags

namespace KernelProfilingInfo {
#define GET_KernelProfilingInfo_DECL
#include "SPIRVGenTables.inc"
} // namespace KernelProfilingInfo

namespace InstructionSet {
#define GET_InstructionSet_DECL
#include "SPIRVGenTables.inc"
} // namespace InstructionSet

namespace OpenCLExtInst {
#define GET_OpenCLExtInst_DECL
#include "SPIRVGenTables.inc"
} // namespace OpenCLExtInst

namespace GLSLExtInst {
#define GET_GLSLExtInst_DECL
#include "SPIRVGenTables.inc"
} // namespace GLSLExtInst

namespace NonSemanticExtInst {
#define GET_NonSemanticExtInst_DECL
#include "SPIRVGenTables.inc"
} // namespace NonSemanticExtInst

namespace Opcode {
#define GET_Opcode_DECL
#include "SPIRVGenTables.inc"
} // namespace Opcode

struct ExtendedBuiltin {
  StringRef Name;
  InstructionSet::InstructionSet Set;
  uint32_t Number;
};
} // namespace SPIRV

using CapabilityList = SmallVector<SPIRV::Capability::Capability, 8>;
using ExtensionList = SmallVector<SPIRV::Extension::Extension, 8>;

std::string
getSymbolicOperandMnemonic(SPIRV::OperandCategory::OperandCategory Category,
                           int32_t Value);
VersionTuple
getSymbolicOperandMinVersion(SPIRV::OperandCategory::OperandCategory Category,
                             uint32_t Value);
VersionTuple
getSymbolicOperandMaxVersion(SPIRV::OperandCategory::OperandCategory Category,
                             uint32_t Value);
CapabilityList
getSymbolicOperandCapabilities(SPIRV::OperandCategory::OperandCategory Category,
                               uint32_t Value);
CapabilityList
getCapabilitiesEnabledByExtension(SPIRV::Extension::Extension Extension);
ExtensionList
getSymbolicOperandExtensions(SPIRV::OperandCategory::OperandCategory Category,
                             uint32_t Value);
std::string getLinkStringForBuiltIn(SPIRV::BuiltIn::BuiltIn BuiltInValue);

bool getSpirvBuiltInIdByName(StringRef Name, SPIRV::BuiltIn::BuiltIn &BI);

std::string getExtInstSetName(SPIRV::InstructionSet::InstructionSet Set);
SPIRV::InstructionSet::InstructionSet
getExtInstSetFromString(std::string SetName);
std::string getExtInstName(SPIRV::InstructionSet::InstructionSet Set,
                           uint32_t InstructionNumber);

// Return a string representation of the operands from startIndex onwards.
// Templated to allow both MachineInstr and MCInst to use the same logic.
template <class InstType>
std::string getSPIRVStringOperand(const InstType &MI, unsigned StartIndex) {
  std::string s; // Iteratively append to this string.

  const unsigned NumOps = MI.getNumOperands();
  bool IsFinished = false;
  for (unsigned i = StartIndex; i < NumOps && !IsFinished; ++i) {
    const auto &Op = MI.getOperand(i);
    if (!Op.isImm()) // Stop if we hit a register operand.
      break;
    assert((Op.getImm() >> 32) == 0 && "Imm operand should be i32 word");
    const uint32_t Imm = Op.getImm(); // Each i32 word is up to 4 characters.
    for (unsigned ShiftAmount = 0; ShiftAmount < 32; ShiftAmount += 8) {
      char c = (Imm >> ShiftAmount) & 0xff;
      if (c == 0) { // Stop if we hit a null-terminator character.
        IsFinished = true;
        break;
      }
      s += c; // Otherwise, append the character to the result string.
    }
  }
  return s;
}
} // namespace llvm
#endif // LLVM_LIB_TARGET_SPIRV_SPIRVSYMBOLICOPERANDS_H
