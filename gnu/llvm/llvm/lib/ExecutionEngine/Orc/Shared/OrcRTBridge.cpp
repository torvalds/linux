//===------ OrcRTBridge.cpp - Executor functions for bootstrap -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/Shared/OrcRTBridge.h"

namespace llvm {
namespace orc {
namespace rt {

const char *SimpleExecutorDylibManagerInstanceName =
    "__llvm_orc_SimpleExecutorDylibManager_Instance";
const char *SimpleExecutorDylibManagerOpenWrapperName =
    "__llvm_orc_SimpleExecutorDylibManager_open_wrapper";
const char *SimpleExecutorDylibManagerLookupWrapperName =
    "__llvm_orc_SimpleExecutorDylibManager_lookup_wrapper";

const char *SimpleExecutorMemoryManagerInstanceName =
    "__llvm_orc_SimpleExecutorMemoryManager_Instance";
const char *SimpleExecutorMemoryManagerReserveWrapperName =
    "__llvm_orc_SimpleExecutorMemoryManager_reserve_wrapper";
const char *SimpleExecutorMemoryManagerFinalizeWrapperName =
    "__llvm_orc_SimpleExecutorMemoryManager_finalize_wrapper";
const char *SimpleExecutorMemoryManagerDeallocateWrapperName =
    "__llvm_orc_SimpleExecutorMemoryManager_deallocate_wrapper";

const char *ExecutorSharedMemoryMapperServiceInstanceName =
    "__llvm_orc_ExecutorSharedMemoryMapperService_Instance";
const char *ExecutorSharedMemoryMapperServiceReserveWrapperName =
    "__llvm_orc_ExecutorSharedMemoryMapperService_Reserve";
const char *ExecutorSharedMemoryMapperServiceInitializeWrapperName =
    "__llvm_orc_ExecutorSharedMemoryMapperService_Initialize";
const char *ExecutorSharedMemoryMapperServiceDeinitializeWrapperName =
    "__llvm_orc_ExecutorSharedMemoryMapperService_Deinitialize";
const char *ExecutorSharedMemoryMapperServiceReleaseWrapperName =
    "__llvm_orc_ExecutorSharedMemoryMapperService_Release";

const char *MemoryWriteUInt8sWrapperName =
    "__llvm_orc_bootstrap_mem_write_uint8s_wrapper";
const char *MemoryWriteUInt16sWrapperName =
    "__llvm_orc_bootstrap_mem_write_uint16s_wrapper";
const char *MemoryWriteUInt32sWrapperName =
    "__llvm_orc_bootstrap_mem_write_uint32s_wrapper";
const char *MemoryWriteUInt64sWrapperName =
    "__llvm_orc_bootstrap_mem_write_uint64s_wrapper";
const char *MemoryWriteBuffersWrapperName =
    "__llvm_orc_bootstrap_mem_write_buffers_wrapper";

const char *RegisterEHFrameSectionWrapperName =
    "llvm_orc_registerEHFrameSectionWrapper";
const char *DeregisterEHFrameSectionWrapperName =
    "llvm_orc_deregisterEHFrameSectionWrapper";

const char *RunAsMainWrapperName = "__llvm_orc_bootstrap_run_as_main_wrapper";
const char *RunAsVoidFunctionWrapperName =
    "__llvm_orc_bootstrap_run_as_void_function_wrapper";
const char *RunAsIntFunctionWrapperName =
    "__llvm_orc_bootstrap_run_as_int_function_wrapper";

} // end namespace rt
} // end namespace orc
} // end namespace llvm
