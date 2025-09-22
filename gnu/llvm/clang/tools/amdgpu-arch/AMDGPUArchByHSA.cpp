//===- AMDGPUArchByHSA.cpp - list AMDGPU installed ------*- C++ -*---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a tool for detecting name of AMDGPU installed in system
// using HSA on Linux. This tool is used by AMDGPU OpenMP and HIP driver.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/Version.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <string>
#include <vector>

using namespace llvm;

typedef enum {
  HSA_STATUS_SUCCESS = 0x0,
} hsa_status_t;

typedef enum {
  HSA_DEVICE_TYPE_CPU = 0,
  HSA_DEVICE_TYPE_GPU = 1,
} hsa_device_type_t;

typedef enum {
  HSA_AGENT_INFO_NAME = 0,
  HSA_AGENT_INFO_DEVICE = 17,
} hsa_agent_info_t;

typedef struct hsa_agent_s {
  uint64_t handle;
} hsa_agent_t;

hsa_status_t (*hsa_init)();
hsa_status_t (*hsa_shut_down)();
hsa_status_t (*hsa_agent_get_info)(hsa_agent_t, hsa_agent_info_t, void *);
hsa_status_t (*hsa_iterate_agents)(hsa_status_t (*)(hsa_agent_t, void *),
                                   void *);

constexpr const char *DynamicHSAPath = "libhsa-runtime64.so";

llvm::Error loadHSA() {
  std::string ErrMsg;
  auto DynlibHandle = std::make_unique<llvm::sys::DynamicLibrary>(
      llvm::sys::DynamicLibrary::getPermanentLibrary(DynamicHSAPath, &ErrMsg));
  if (!DynlibHandle->isValid()) {
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "Failed to 'dlopen' %s", DynamicHSAPath);
  }
#define DYNAMIC_INIT(SYMBOL)                                                   \
  {                                                                            \
    void *SymbolPtr = DynlibHandle->getAddressOfSymbol(#SYMBOL);               \
    if (!SymbolPtr)                                                            \
      return llvm::createStringError(llvm::inconvertibleErrorCode(),           \
                                     "Failed to 'dlsym' " #SYMBOL);            \
    SYMBOL = reinterpret_cast<decltype(SYMBOL)>(SymbolPtr);                    \
  }
  DYNAMIC_INIT(hsa_init);
  DYNAMIC_INIT(hsa_shut_down);
  DYNAMIC_INIT(hsa_agent_get_info);
  DYNAMIC_INIT(hsa_iterate_agents);
#undef DYNAMIC_INIT
  return llvm::Error::success();
}

static hsa_status_t iterateAgentsCallback(hsa_agent_t Agent, void *Data) {
  hsa_device_type_t DeviceType;
  hsa_status_t Status =
      hsa_agent_get_info(Agent, HSA_AGENT_INFO_DEVICE, &DeviceType);

  // continue only if device type if GPU
  if (Status != HSA_STATUS_SUCCESS || DeviceType != HSA_DEVICE_TYPE_GPU) {
    return Status;
  }

  std::vector<std::string> *GPUs =
      static_cast<std::vector<std::string> *>(Data);
  char GPUName[64];
  Status = hsa_agent_get_info(Agent, HSA_AGENT_INFO_NAME, GPUName);
  if (Status != HSA_STATUS_SUCCESS) {
    return Status;
  }
  GPUs->push_back(GPUName);
  return HSA_STATUS_SUCCESS;
}

int printGPUsByHSA() {
  // Attempt to load the HSA runtime.
  if (llvm::Error Err = loadHSA()) {
    logAllUnhandledErrors(std::move(Err), llvm::errs());
    return 1;
  }

  hsa_status_t Status = hsa_init();
  if (Status != HSA_STATUS_SUCCESS) {
    return 1;
  }

  std::vector<std::string> GPUs;
  Status = hsa_iterate_agents(iterateAgentsCallback, &GPUs);
  if (Status != HSA_STATUS_SUCCESS) {
    return 1;
  }

  for (const auto &GPU : GPUs)
    llvm::outs() << GPU << '\n';

  if (GPUs.size() < 1)
    return 1;

  hsa_shut_down();
  return 0;
}
