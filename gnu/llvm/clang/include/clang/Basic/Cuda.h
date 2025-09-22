//===--- Cuda.h - Utilities for compiling CUDA code  ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_CUDA_H
#define LLVM_CLANG_BASIC_CUDA_H

namespace llvm {
class StringRef;
class Twine;
class VersionTuple;
} // namespace llvm

namespace clang {

enum class CudaVersion {
  UNKNOWN,
  CUDA_70,
  CUDA_75,
  CUDA_80,
  CUDA_90,
  CUDA_91,
  CUDA_92,
  CUDA_100,
  CUDA_101,
  CUDA_102,
  CUDA_110,
  CUDA_111,
  CUDA_112,
  CUDA_113,
  CUDA_114,
  CUDA_115,
  CUDA_116,
  CUDA_117,
  CUDA_118,
  CUDA_120,
  CUDA_121,
  CUDA_122,
  CUDA_123,
  CUDA_124,
  CUDA_125,
  FULLY_SUPPORTED = CUDA_123,
  PARTIALLY_SUPPORTED =
      CUDA_125, // Partially supported. Proceed with a warning.
  NEW = 10000,  // Too new. Issue a warning, but allow using it.
};
const char *CudaVersionToString(CudaVersion V);
// Input is "Major.Minor"
CudaVersion CudaStringToVersion(const llvm::Twine &S);

enum class OffloadArch {
  UNUSED,
  UNKNOWN,
  // TODO: Deprecate and remove GPU architectures older than sm_52.
  SM_20,
  SM_21,
  SM_30,
  // This has a name conflict with sys/mac.h on AIX, rename it as a workaround.
  SM_32_,
  SM_35,
  SM_37,
  SM_50,
  SM_52,
  SM_53,
  SM_60,
  SM_61,
  SM_62,
  SM_70,
  SM_72,
  SM_75,
  SM_80,
  SM_86,
  SM_87,
  SM_89,
  SM_90,
  SM_90a,
  GFX600,
  GFX601,
  GFX602,
  GFX700,
  GFX701,
  GFX702,
  GFX703,
  GFX704,
  GFX705,
  GFX801,
  GFX802,
  GFX803,
  GFX805,
  GFX810,
  GFX9_GENERIC,
  GFX900,
  GFX902,
  GFX904,
  GFX906,
  GFX908,
  GFX909,
  GFX90a,
  GFX90c,
  GFX940,
  GFX941,
  GFX942,
  GFX10_1_GENERIC,
  GFX1010,
  GFX1011,
  GFX1012,
  GFX1013,
  GFX10_3_GENERIC,
  GFX1030,
  GFX1031,
  GFX1032,
  GFX1033,
  GFX1034,
  GFX1035,
  GFX1036,
  GFX11_GENERIC,
  GFX1100,
  GFX1101,
  GFX1102,
  GFX1103,
  GFX1150,
  GFX1151,
  GFX1152,
  GFX12_GENERIC,
  GFX1200,
  GFX1201,
  AMDGCNSPIRV,
  Generic, // A processor model named 'generic' if the target backend defines a
           // public one.
  LAST,

  CudaDefault = OffloadArch::SM_52,
  HIPDefault = OffloadArch::GFX906,
};

enum class CUDAFunctionTarget {
  Device,
  Global,
  Host,
  HostDevice,
  InvalidTarget
};

static inline bool IsNVIDIAOffloadArch(OffloadArch A) {
  return A >= OffloadArch::SM_20 && A < OffloadArch::GFX600;
}

static inline bool IsAMDOffloadArch(OffloadArch A) {
  // Generic processor model is for testing only.
  return A >= OffloadArch::GFX600 && A < OffloadArch::Generic;
}

const char *OffloadArchToString(OffloadArch A);
const char *OffloadArchToVirtualArchString(OffloadArch A);

// The input should have the form "sm_20".
OffloadArch StringToOffloadArch(llvm::StringRef S);

/// Get the earliest CudaVersion that supports the given OffloadArch.
CudaVersion MinVersionForOffloadArch(OffloadArch A);

/// Get the latest CudaVersion that supports the given OffloadArch.
CudaVersion MaxVersionForOffloadArch(OffloadArch A);

//  Various SDK-dependent features that affect CUDA compilation
enum class CudaFeature {
  // CUDA-9.2+ uses a new API for launching kernels.
  CUDA_USES_NEW_LAUNCH,
  // CUDA-10.1+ needs explicit end of GPU binary registration.
  CUDA_USES_FATBIN_REGISTER_END,
};

CudaVersion ToCudaVersion(llvm::VersionTuple);
bool CudaFeatureEnabled(llvm::VersionTuple, CudaFeature);
bool CudaFeatureEnabled(CudaVersion, CudaFeature);

} // namespace clang

#endif
