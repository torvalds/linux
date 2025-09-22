//===- AMDGPUArchByHIP.cpp - list AMDGPU installed ----------*- C++ -*-----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a tool for detecting name of AMDGPU installed in system
// using HIP runtime. This tool is used by AMDGPU OpenMP and HIP driver.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

typedef struct {
  char padding[396];
  char gcnArchName[256];
  char padding2[1024];
} hipDeviceProp_t;

typedef enum {
  hipSuccess = 0,
} hipError_t;

typedef hipError_t (*hipGetDeviceCount_t)(int *);
typedef hipError_t (*hipDeviceGet_t)(int *, int);
typedef hipError_t (*hipGetDeviceProperties_t)(hipDeviceProp_t *, int);

int printGPUsByHIP() {
#ifdef _WIN32
  constexpr const char *DynamicHIPPath = "amdhip64.dll";
#else
  constexpr const char *DynamicHIPPath = "libamdhip64.so";
#endif

  std::string ErrMsg;
  auto DynlibHandle = std::make_unique<llvm::sys::DynamicLibrary>(
      llvm::sys::DynamicLibrary::getPermanentLibrary(DynamicHIPPath, &ErrMsg));
  if (!DynlibHandle->isValid()) {
    llvm::errs() << "Failed to load " << DynamicHIPPath << ": " << ErrMsg
                 << '\n';
    return 1;
  }

#define DYNAMIC_INIT_HIP(SYMBOL)                                               \
  {                                                                            \
    void *SymbolPtr = DynlibHandle->getAddressOfSymbol(#SYMBOL);               \
    if (!SymbolPtr) {                                                          \
      llvm::errs() << "Failed to find symbol " << #SYMBOL << '\n';             \
      return 1;                                                                \
    }                                                                          \
    SYMBOL = reinterpret_cast<decltype(SYMBOL)>(SymbolPtr);                    \
  }

  hipGetDeviceCount_t hipGetDeviceCount;
  hipDeviceGet_t hipDeviceGet;
  hipGetDeviceProperties_t hipGetDeviceProperties;

  DYNAMIC_INIT_HIP(hipGetDeviceCount);
  DYNAMIC_INIT_HIP(hipDeviceGet);
  DYNAMIC_INIT_HIP(hipGetDeviceProperties);

#undef DYNAMIC_INIT_HIP

  int deviceCount;
  hipError_t err = hipGetDeviceCount(&deviceCount);
  if (err != hipSuccess) {
    llvm::errs() << "Failed to get device count\n";
    return 1;
  }

  for (int i = 0; i < deviceCount; ++i) {
    int deviceId;
    err = hipDeviceGet(&deviceId, i);
    if (err != hipSuccess) {
      llvm::errs() << "Failed to get device id for ordinal " << i << '\n';
      return 1;
    }

    hipDeviceProp_t prop;
    err = hipGetDeviceProperties(&prop, deviceId);
    if (err != hipSuccess) {
      llvm::errs() << "Failed to get device properties for device " << deviceId
                   << '\n';
      return 1;
    }
    llvm::outs() << prop.gcnArchName << '\n';
  }

  return 0;
}
