//===--- AMDGPUMetadata.cpp -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// AMDGPU metadata definitions and in-memory representations.
///
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/AMDGPUMetadata.h"
#include "llvm/Support/YAMLTraits.h"
#include <optional>

using namespace llvm::AMDGPU;
using namespace llvm::AMDGPU::HSAMD;

LLVM_YAML_IS_SEQUENCE_VECTOR(Kernel::Arg::Metadata)
LLVM_YAML_IS_SEQUENCE_VECTOR(Kernel::Metadata)

namespace llvm {
namespace yaml {

template <>
struct ScalarEnumerationTraits<AccessQualifier> {
  static void enumeration(IO &YIO, AccessQualifier &EN) {
    YIO.enumCase(EN, "Default", AccessQualifier::Default);
    YIO.enumCase(EN, "ReadOnly", AccessQualifier::ReadOnly);
    YIO.enumCase(EN, "WriteOnly", AccessQualifier::WriteOnly);
    YIO.enumCase(EN, "ReadWrite", AccessQualifier::ReadWrite);
  }
};

template <>
struct ScalarEnumerationTraits<AddressSpaceQualifier> {
  static void enumeration(IO &YIO, AddressSpaceQualifier &EN) {
    YIO.enumCase(EN, "Private", AddressSpaceQualifier::Private);
    YIO.enumCase(EN, "Global", AddressSpaceQualifier::Global);
    YIO.enumCase(EN, "Constant", AddressSpaceQualifier::Constant);
    YIO.enumCase(EN, "Local", AddressSpaceQualifier::Local);
    YIO.enumCase(EN, "Generic", AddressSpaceQualifier::Generic);
    YIO.enumCase(EN, "Region", AddressSpaceQualifier::Region);
  }
};

template <>
struct ScalarEnumerationTraits<ValueKind> {
  static void enumeration(IO &YIO, ValueKind &EN) {
    YIO.enumCase(EN, "ByValue", ValueKind::ByValue);
    YIO.enumCase(EN, "GlobalBuffer", ValueKind::GlobalBuffer);
    YIO.enumCase(EN, "DynamicSharedPointer", ValueKind::DynamicSharedPointer);
    YIO.enumCase(EN, "Sampler", ValueKind::Sampler);
    YIO.enumCase(EN, "Image", ValueKind::Image);
    YIO.enumCase(EN, "Pipe", ValueKind::Pipe);
    YIO.enumCase(EN, "Queue", ValueKind::Queue);
    YIO.enumCase(EN, "HiddenGlobalOffsetX", ValueKind::HiddenGlobalOffsetX);
    YIO.enumCase(EN, "HiddenGlobalOffsetY", ValueKind::HiddenGlobalOffsetY);
    YIO.enumCase(EN, "HiddenGlobalOffsetZ", ValueKind::HiddenGlobalOffsetZ);
    YIO.enumCase(EN, "HiddenNone", ValueKind::HiddenNone);
    YIO.enumCase(EN, "HiddenPrintfBuffer", ValueKind::HiddenPrintfBuffer);
    YIO.enumCase(EN, "HiddenHostcallBuffer", ValueKind::HiddenHostcallBuffer);
    YIO.enumCase(EN, "HiddenDefaultQueue", ValueKind::HiddenDefaultQueue);
    YIO.enumCase(EN, "HiddenCompletionAction",
                 ValueKind::HiddenCompletionAction);
    YIO.enumCase(EN, "HiddenMultiGridSyncArg",
		 ValueKind::HiddenMultiGridSyncArg);
  }
};

template <>
struct ScalarEnumerationTraits<ValueType> {
  static void enumeration(IO &YIO, ValueType &EN) {
    YIO.enumCase(EN, "Struct", ValueType::Struct);
    YIO.enumCase(EN, "I8", ValueType::I8);
    YIO.enumCase(EN, "U8", ValueType::U8);
    YIO.enumCase(EN, "I16", ValueType::I16);
    YIO.enumCase(EN, "U16", ValueType::U16);
    YIO.enumCase(EN, "F16", ValueType::F16);
    YIO.enumCase(EN, "I32", ValueType::I32);
    YIO.enumCase(EN, "U32", ValueType::U32);
    YIO.enumCase(EN, "F32", ValueType::F32);
    YIO.enumCase(EN, "I64", ValueType::I64);
    YIO.enumCase(EN, "U64", ValueType::U64);
    YIO.enumCase(EN, "F64", ValueType::F64);
  }
};

template <>
struct MappingTraits<Kernel::Attrs::Metadata> {
  static void mapping(IO &YIO, Kernel::Attrs::Metadata &MD) {
    YIO.mapOptional(Kernel::Attrs::Key::ReqdWorkGroupSize,
                    MD.mReqdWorkGroupSize, std::vector<uint32_t>());
    YIO.mapOptional(Kernel::Attrs::Key::WorkGroupSizeHint,
                    MD.mWorkGroupSizeHint, std::vector<uint32_t>());
    YIO.mapOptional(Kernel::Attrs::Key::VecTypeHint,
                    MD.mVecTypeHint, std::string());
    YIO.mapOptional(Kernel::Attrs::Key::RuntimeHandle, MD.mRuntimeHandle,
                    std::string());
  }
};

template <>
struct MappingTraits<Kernel::Arg::Metadata> {
  static void mapping(IO &YIO, Kernel::Arg::Metadata &MD) {
    YIO.mapOptional(Kernel::Arg::Key::Name, MD.mName, std::string());
    YIO.mapOptional(Kernel::Arg::Key::TypeName, MD.mTypeName, std::string());
    YIO.mapRequired(Kernel::Arg::Key::Size, MD.mSize);
    YIO.mapRequired(Kernel::Arg::Key::Align, MD.mAlign);
    YIO.mapRequired(Kernel::Arg::Key::ValueKind, MD.mValueKind);

    // Removed. Accepted for parsing compatibility, but not emitted.
    std::optional<ValueType> Unused;
    YIO.mapOptional(Kernel::Arg::Key::ValueType, Unused);

    YIO.mapOptional(Kernel::Arg::Key::PointeeAlign, MD.mPointeeAlign,
                    uint32_t(0));
    YIO.mapOptional(Kernel::Arg::Key::AddrSpaceQual, MD.mAddrSpaceQual,
                    AddressSpaceQualifier::Unknown);
    YIO.mapOptional(Kernel::Arg::Key::AccQual, MD.mAccQual,
                    AccessQualifier::Unknown);
    YIO.mapOptional(Kernel::Arg::Key::ActualAccQual, MD.mActualAccQual,
                    AccessQualifier::Unknown);
    YIO.mapOptional(Kernel::Arg::Key::IsConst, MD.mIsConst, false);
    YIO.mapOptional(Kernel::Arg::Key::IsRestrict, MD.mIsRestrict, false);
    YIO.mapOptional(Kernel::Arg::Key::IsVolatile, MD.mIsVolatile, false);
    YIO.mapOptional(Kernel::Arg::Key::IsPipe, MD.mIsPipe, false);
  }
};

template <>
struct MappingTraits<Kernel::CodeProps::Metadata> {
  static void mapping(IO &YIO, Kernel::CodeProps::Metadata &MD) {
    YIO.mapRequired(Kernel::CodeProps::Key::KernargSegmentSize,
                    MD.mKernargSegmentSize);
    YIO.mapRequired(Kernel::CodeProps::Key::GroupSegmentFixedSize,
                    MD.mGroupSegmentFixedSize);
    YIO.mapRequired(Kernel::CodeProps::Key::PrivateSegmentFixedSize,
                    MD.mPrivateSegmentFixedSize);
    YIO.mapRequired(Kernel::CodeProps::Key::KernargSegmentAlign,
                    MD.mKernargSegmentAlign);
    YIO.mapRequired(Kernel::CodeProps::Key::WavefrontSize,
                    MD.mWavefrontSize);
    YIO.mapOptional(Kernel::CodeProps::Key::NumSGPRs,
                    MD.mNumSGPRs, uint16_t(0));
    YIO.mapOptional(Kernel::CodeProps::Key::NumVGPRs,
                    MD.mNumVGPRs, uint16_t(0));
    YIO.mapOptional(Kernel::CodeProps::Key::MaxFlatWorkGroupSize,
                    MD.mMaxFlatWorkGroupSize, uint32_t(0));
    YIO.mapOptional(Kernel::CodeProps::Key::IsDynamicCallStack,
                    MD.mIsDynamicCallStack, false);
    YIO.mapOptional(Kernel::CodeProps::Key::IsXNACKEnabled,
                    MD.mIsXNACKEnabled, false);
    YIO.mapOptional(Kernel::CodeProps::Key::NumSpilledSGPRs,
                    MD.mNumSpilledSGPRs, uint16_t(0));
    YIO.mapOptional(Kernel::CodeProps::Key::NumSpilledVGPRs,
                    MD.mNumSpilledVGPRs, uint16_t(0));
  }
};

template <>
struct MappingTraits<Kernel::DebugProps::Metadata> {
  static void mapping(IO &YIO, Kernel::DebugProps::Metadata &MD) {
    YIO.mapOptional(Kernel::DebugProps::Key::DebuggerABIVersion,
                    MD.mDebuggerABIVersion, std::vector<uint32_t>());
    YIO.mapOptional(Kernel::DebugProps::Key::ReservedNumVGPRs,
                    MD.mReservedNumVGPRs, uint16_t(0));
    YIO.mapOptional(Kernel::DebugProps::Key::ReservedFirstVGPR,
                    MD.mReservedFirstVGPR, uint16_t(-1));
    YIO.mapOptional(Kernel::DebugProps::Key::PrivateSegmentBufferSGPR,
                    MD.mPrivateSegmentBufferSGPR, uint16_t(-1));
    YIO.mapOptional(Kernel::DebugProps::Key::WavefrontPrivateSegmentOffsetSGPR,
                    MD.mWavefrontPrivateSegmentOffsetSGPR, uint16_t(-1));
  }
};

template <>
struct MappingTraits<Kernel::Metadata> {
  static void mapping(IO &YIO, Kernel::Metadata &MD) {
    YIO.mapRequired(Kernel::Key::Name, MD.mName);
    YIO.mapRequired(Kernel::Key::SymbolName, MD.mSymbolName);
    YIO.mapOptional(Kernel::Key::Language, MD.mLanguage, std::string());
    YIO.mapOptional(Kernel::Key::LanguageVersion, MD.mLanguageVersion,
                    std::vector<uint32_t>());
    if (!MD.mAttrs.empty() || !YIO.outputting())
      YIO.mapOptional(Kernel::Key::Attrs, MD.mAttrs);
    if (!MD.mArgs.empty() || !YIO.outputting())
      YIO.mapOptional(Kernel::Key::Args, MD.mArgs);
    if (!MD.mCodeProps.empty() || !YIO.outputting())
      YIO.mapOptional(Kernel::Key::CodeProps, MD.mCodeProps);
    if (!MD.mDebugProps.empty() || !YIO.outputting())
      YIO.mapOptional(Kernel::Key::DebugProps, MD.mDebugProps);
  }
};

template <>
struct MappingTraits<HSAMD::Metadata> {
  static void mapping(IO &YIO, HSAMD::Metadata &MD) {
    YIO.mapRequired(Key::Version, MD.mVersion);
    YIO.mapOptional(Key::Printf, MD.mPrintf, std::vector<std::string>());
    if (!MD.mKernels.empty() || !YIO.outputting())
      YIO.mapOptional(Key::Kernels, MD.mKernels);
  }
};

} // end namespace yaml

namespace AMDGPU {
namespace HSAMD {

std::error_code fromString(StringRef String, Metadata &HSAMetadata) {
  yaml::Input YamlInput(String);
  YamlInput >> HSAMetadata;
  return YamlInput.error();
}

std::error_code toString(Metadata HSAMetadata, std::string &String) {
  raw_string_ostream YamlStream(String);
  yaml::Output YamlOutput(YamlStream, nullptr, std::numeric_limits<int>::max());
  YamlOutput << HSAMetadata;
  return std::error_code();
}

} // end namespace HSAMD
} // end namespace AMDGPU
} // end namespace llvm
