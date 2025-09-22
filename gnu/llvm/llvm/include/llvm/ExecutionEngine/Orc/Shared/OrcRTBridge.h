//===---- OrcRTBridge.h -- Utils for interacting with orc-rt ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares types and symbol names provided by the ORC runtime.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_SHARED_ORCRTBRIDGE_H
#define LLVM_EXECUTIONENGINE_ORC_SHARED_ORCRTBRIDGE_H

#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorSymbolDef.h"
#include "llvm/ExecutionEngine/Orc/Shared/SimpleRemoteEPCUtils.h"
#include "llvm/ExecutionEngine/Orc/Shared/TargetProcessControlTypes.h"

namespace llvm {
namespace orc {
namespace rt {

extern const char *SimpleExecutorDylibManagerInstanceName;
extern const char *SimpleExecutorDylibManagerOpenWrapperName;
extern const char *SimpleExecutorDylibManagerLookupWrapperName;

extern const char *SimpleExecutorMemoryManagerInstanceName;
extern const char *SimpleExecutorMemoryManagerReserveWrapperName;
extern const char *SimpleExecutorMemoryManagerFinalizeWrapperName;
extern const char *SimpleExecutorMemoryManagerDeallocateWrapperName;

extern const char *ExecutorSharedMemoryMapperServiceInstanceName;
extern const char *ExecutorSharedMemoryMapperServiceReserveWrapperName;
extern const char *ExecutorSharedMemoryMapperServiceInitializeWrapperName;
extern const char *ExecutorSharedMemoryMapperServiceDeinitializeWrapperName;
extern const char *ExecutorSharedMemoryMapperServiceReleaseWrapperName;

extern const char *MemoryWriteUInt8sWrapperName;
extern const char *MemoryWriteUInt16sWrapperName;
extern const char *MemoryWriteUInt32sWrapperName;
extern const char *MemoryWriteUInt64sWrapperName;
extern const char *MemoryWriteBuffersWrapperName;

extern const char *RegisterEHFrameSectionWrapperName;
extern const char *DeregisterEHFrameSectionWrapperName;

extern const char *RunAsMainWrapperName;
extern const char *RunAsVoidFunctionWrapperName;
extern const char *RunAsIntFunctionWrapperName;

using SPSSimpleExecutorDylibManagerOpenSignature =
    shared::SPSExpected<shared::SPSExecutorAddr>(shared::SPSExecutorAddr,
                                                 shared::SPSString, uint64_t);

using SPSSimpleExecutorDylibManagerLookupSignature =
    shared::SPSExpected<shared::SPSSequence<shared::SPSExecutorSymbolDef>>(
        shared::SPSExecutorAddr, shared::SPSExecutorAddr,
        shared::SPSRemoteSymbolLookupSet);

using SPSSimpleExecutorMemoryManagerReserveSignature =
    shared::SPSExpected<shared::SPSExecutorAddr>(shared::SPSExecutorAddr,
                                                 uint64_t);
using SPSSimpleExecutorMemoryManagerFinalizeSignature =
    shared::SPSError(shared::SPSExecutorAddr, shared::SPSFinalizeRequest);
using SPSSimpleExecutorMemoryManagerDeallocateSignature = shared::SPSError(
    shared::SPSExecutorAddr, shared::SPSSequence<shared::SPSExecutorAddr>);

// ExecutorSharedMemoryMapperService
using SPSExecutorSharedMemoryMapperServiceReserveSignature =
    shared::SPSExpected<
        shared::SPSTuple<shared::SPSExecutorAddr, shared::SPSString>>(
        shared::SPSExecutorAddr, uint64_t);
using SPSExecutorSharedMemoryMapperServiceInitializeSignature =
    shared::SPSExpected<shared::SPSExecutorAddr>(
        shared::SPSExecutorAddr, shared::SPSExecutorAddr,
        shared::SPSSharedMemoryFinalizeRequest);
using SPSExecutorSharedMemoryMapperServiceDeinitializeSignature =
    shared::SPSError(shared::SPSExecutorAddr,
                     shared::SPSSequence<shared::SPSExecutorAddr>);
using SPSExecutorSharedMemoryMapperServiceReleaseSignature = shared::SPSError(
    shared::SPSExecutorAddr, shared::SPSSequence<shared::SPSExecutorAddr>);

using SPSRunAsMainSignature = int64_t(shared::SPSExecutorAddr,
                                      shared::SPSSequence<shared::SPSString>);
using SPSRunAsVoidFunctionSignature = int32_t(shared::SPSExecutorAddr);
using SPSRunAsIntFunctionSignature = int32_t(shared::SPSExecutorAddr, int32_t);
} // end namespace rt
} // end namespace orc
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_SHARED_ORCRTBRIDGE_H
