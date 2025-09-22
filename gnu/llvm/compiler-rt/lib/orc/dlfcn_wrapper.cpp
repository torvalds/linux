//===- dlfcn_wrapper.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of the ORC runtime support library.
//
//===----------------------------------------------------------------------===//

#include "adt.h"
#include "common.h"
#include "wrapper_function_utils.h"

#include <vector>

using namespace __orc_rt;

extern "C" const char *__orc_rt_jit_dlerror();
extern "C" void *__orc_rt_jit_dlopen(const char *path, int mode);
extern "C" int __orc_rt_jit_dlclose(void *dso_handle);

ORC_RT_INTERFACE orc_rt_CWrapperFunctionResult
__orc_rt_jit_dlerror_wrapper(const char *ArgData, size_t ArgSize) {
  return WrapperFunction<SPSString()>::handle(
             ArgData, ArgSize,
             []() { return std::string(__orc_rt_jit_dlerror()); })
      .release();
}

ORC_RT_INTERFACE orc_rt_CWrapperFunctionResult
__orc_rt_jit_dlopen_wrapper(const char *ArgData, size_t ArgSize) {
  return WrapperFunction<SPSExecutorAddr(SPSString, int32_t)>::handle(
             ArgData, ArgSize,
             [](const std::string &Path, int32_t mode) {
               return ExecutorAddr::fromPtr(
                   __orc_rt_jit_dlopen(Path.c_str(), mode));
             })
      .release();
}

ORC_RT_INTERFACE orc_rt_CWrapperFunctionResult
__orc_rt_jit_dlclose_wrapper(const char *ArgData, size_t ArgSize) {
  return WrapperFunction<int32_t(SPSExecutorAddr)>::handle(
             ArgData, ArgSize,
             [](ExecutorAddr &DSOHandle) {
               return __orc_rt_jit_dlclose(DSOHandle.toPtr<void *>());
             })
      .release();
}
