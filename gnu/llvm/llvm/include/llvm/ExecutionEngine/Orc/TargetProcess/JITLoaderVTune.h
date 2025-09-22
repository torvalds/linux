
//===------ JITLoaderVTune.h --- Register profiler objects ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Register objects for access by profilers via the perf JIT interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_JITLOADERVTUNE_H
#define LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_JITLOADERVTUNE_H

#include "llvm/ExecutionEngine/Orc/Shared/WrapperFunctionUtils.h"
#include <cstdint>

extern "C" llvm::orc::shared::CWrapperFunctionResult
llvm_orc_registerVTuneImpl(const char *Data, uint64_t Size);

extern "C" llvm::orc::shared::CWrapperFunctionResult
llvm_orc_unregisterVTuneImpl(const char *Data, uint64_t Size);

extern "C" llvm::orc::shared::CWrapperFunctionResult
llvm_orc_test_registerVTuneImpl(const char *Data, uint64_t Size);

#endif // LLVM_EXECUTIONENGINE_ORC_TARGETPROCESS_JITLOADERVTUNE_H


