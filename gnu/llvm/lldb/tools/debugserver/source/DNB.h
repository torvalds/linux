//===-- DNB.h ---------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Created by Greg Clayton on 3/23/07.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_DEBUGSERVER_SOURCE_DNB_H
#define LLDB_TOOLS_DEBUGSERVER_SOURCE_DNB_H

#include "DNBDefs.h"
#include "JSONGenerator.h"
#include "MacOSX/Genealogy.h"
#include "MacOSX/ThreadInfo.h"
#include "RNBContext.h"
#include <Availability.h>
#include <mach/machine.h>
#include <mach/thread_info.h>
#include <optional>
#include <string>

#define DNB_EXPORT __attribute__((visibility("default")))

#ifndef CPU_TYPE_ARM64
#define CPU_TYPE_ARM64 ((cpu_type_t)12 | 0x01000000)
#endif

#ifndef CPU_TYPE_ARM64_32
#define CPU_TYPE_ARM64_32 ((cpu_type_t)12 | 0x02000000)
#endif

typedef bool (*DNBShouldCancelCallback)(void *);

void DNBInitialize();
void DNBTerminate();

nub_bool_t DNBSetArchitecture(const char *arch);

// Process control
nub_process_t DNBProcessLaunch(
    RNBContext *ctx, const char *path, char const *argv[], const char *envp[],
    const char *working_directory, // NULL => don't change, non-NULL => set
                                   // working directory for inferior to this
    const char *stdin_path, const char *stdout_path, const char *stderr_path,
    bool no_stdio, int disable_aslr, const char *event_data, char *err_str,
    size_t err_len);

nub_process_t DNBProcessGetPIDByName(const char *name);
nub_process_t DNBProcessAttach(nub_process_t pid, struct timespec *timeout,
                               const RNBContext::IgnoredExceptions 
                                   &ignored_exceptions, 
                               char *err_str,
                               size_t err_len);
nub_process_t DNBProcessAttachByName(const char *name, struct timespec *timeout,
                                     const RNBContext::IgnoredExceptions 
                                         &ignored_exceptions, 
                                     char *err_str,
                                     size_t err_len);
nub_process_t DNBProcessAttachWait(RNBContext *ctx, const char *wait_name,
                                   bool ignore_existing,
                                   struct timespec *timeout,
                                   useconds_t interval, char *err_str,
                                   size_t err_len,
                                   DNBShouldCancelCallback should_cancel = NULL,
                                   void *callback_data = NULL);
// Resume a process with exact instructions on what to do with each thread:
// - If no thread actions are supplied (actions is NULL or num_actions is zero),
//   then all threads are continued.
// - If any thread actions are supplied, then each thread will do as it is told
//   by the action. A default actions for any threads that don't have an
//   explicit thread action can be made by making a thread action with a tid of
//   INVALID_NUB_THREAD. If there is no default action, those threads will
//   remain stopped.
nub_bool_t DNBProcessResume(nub_process_t pid,
                            const DNBThreadResumeAction *actions,
                            size_t num_actions) DNB_EXPORT;
nub_bool_t DNBProcessHalt(nub_process_t pid) DNB_EXPORT;
nub_bool_t DNBProcessDetach(nub_process_t pid) DNB_EXPORT;
nub_bool_t DNBProcessSignal(nub_process_t pid, int signal) DNB_EXPORT;
nub_bool_t DNBProcessInterrupt(nub_process_t pid) DNB_EXPORT;
nub_bool_t DNBProcessKill(nub_process_t pid) DNB_EXPORT;
nub_bool_t DNBProcessSendEvent(nub_process_t pid, const char *event) DNB_EXPORT;
nub_size_t DNBProcessMemoryRead(nub_process_t pid, nub_addr_t addr,
                                nub_size_t size, void *buf) DNB_EXPORT;
uint64_t DNBProcessMemoryReadInteger(nub_process_t pid, nub_addr_t addr,
                                     nub_size_t integer_size,
                                     uint64_t fail_value) DNB_EXPORT;
nub_addr_t DNBProcessMemoryReadPointer(nub_process_t pid,
                                       nub_addr_t addr) DNB_EXPORT;
std::string DNBProcessMemoryReadCString(nub_process_t pid,
                                        nub_addr_t addr) DNB_EXPORT;
std::string
DNBProcessMemoryReadCStringFixed(nub_process_t pid, nub_addr_t addr,
                                 nub_size_t fixed_length) DNB_EXPORT;
nub_size_t DNBProcessMemoryWrite(nub_process_t pid, nub_addr_t addr,
                                 nub_size_t size, const void *buf) DNB_EXPORT;
nub_addr_t DNBProcessMemoryAllocate(nub_process_t pid, nub_size_t size,
                                    uint32_t permissions) DNB_EXPORT;
nub_bool_t DNBProcessMemoryDeallocate(nub_process_t pid,
                                      nub_addr_t addr) DNB_EXPORT;
int DNBProcessMemoryRegionInfo(nub_process_t pid, nub_addr_t addr,
                               DNBRegionInfo *region_info) DNB_EXPORT;
std::string
DNBProcessGetProfileData(nub_process_t pid,
                         DNBProfileDataScanType scanType) DNB_EXPORT;
nub_bool_t
DNBProcessSetEnableAsyncProfiling(nub_process_t pid, nub_bool_t enable,
                                  uint64_t interval_usec,
                                  DNBProfileDataScanType scan_type) DNB_EXPORT;

// Process status
nub_bool_t DNBProcessIsAlive(nub_process_t pid) DNB_EXPORT;
nub_state_t DNBProcessGetState(nub_process_t pid) DNB_EXPORT;
nub_bool_t DNBProcessGetExitStatus(nub_process_t pid, int *status) DNB_EXPORT;
nub_bool_t DNBProcessSetExitStatus(nub_process_t pid, int status) DNB_EXPORT;
const char *DNBProcessGetExitInfo(nub_process_t pid) DNB_EXPORT;
nub_bool_t DNBProcessSetExitInfo(nub_process_t pid,
                                 const char *info) DNB_EXPORT;
nub_size_t DNBProcessGetNumThreads(nub_process_t pid) DNB_EXPORT;
nub_thread_t DNBProcessGetCurrentThread(nub_process_t pid) DNB_EXPORT;
nub_thread_t DNBProcessGetCurrentThreadMachPort(nub_process_t pid) DNB_EXPORT;
nub_thread_t DNBProcessSetCurrentThread(nub_process_t pid,
                                        nub_thread_t tid) DNB_EXPORT;
nub_thread_t DNBProcessGetThreadAtIndex(nub_process_t pid,
                                        nub_size_t thread_idx) DNB_EXPORT;
nub_bool_t DNBProcessSyncThreadState(nub_process_t pid,
                                     nub_thread_t tid) DNB_EXPORT;
nub_addr_t DNBProcessGetSharedLibraryInfoAddress(nub_process_t pid) DNB_EXPORT;
nub_bool_t DNBProcessSharedLibrariesUpdated(nub_process_t pid) DNB_EXPORT;
nub_size_t
DNBProcessGetSharedLibraryInfo(nub_process_t pid, nub_bool_t only_changed,
                               DNBExecutableImageInfo **image_infos) DNB_EXPORT;
std::optional<std::string>
DNBGetDeploymentInfo(nub_process_t pid, bool is_executable,
                     const struct load_command &lc,
                     uint64_t load_command_address, uint32_t &major_version,
                     uint32_t &minor_version, uint32_t &patch_version);
nub_bool_t DNBProcessSetNameToAddressCallback(nub_process_t pid,
                                              DNBCallbackNameToAddress callback,
                                              void *baton) DNB_EXPORT;
nub_bool_t DNBProcessSetSharedLibraryInfoCallback(
    nub_process_t pid, DNBCallbackCopyExecutableImageInfos callback,
    void *baton) DNB_EXPORT;
nub_addr_t DNBProcessLookupAddress(nub_process_t pid, const char *name,
                                   const char *shlib) DNB_EXPORT;
nub_size_t DNBProcessGetAvailableSTDOUT(nub_process_t pid, char *buf,
                                        nub_size_t buf_size) DNB_EXPORT;
nub_size_t DNBProcessGetAvailableSTDERR(nub_process_t pid, char *buf,
                                        nub_size_t buf_size) DNB_EXPORT;
nub_size_t DNBProcessGetAvailableProfileData(nub_process_t pid, char *buf,
                                             nub_size_t buf_size) DNB_EXPORT;
nub_size_t DNBProcessGetStopCount(nub_process_t pid) DNB_EXPORT;
uint32_t DNBProcessGetCPUType(nub_process_t pid) DNB_EXPORT;
size_t DNBGetAllInfos(std::vector<struct kinfo_proc> &proc_infos);
JSONGenerator::ObjectSP DNBGetDyldProcessState(nub_process_t pid);

// Process executable and arguments
const char *DNBProcessGetExecutablePath(nub_process_t pid);
const char *DNBProcessGetArgumentAtIndex(nub_process_t pid, nub_size_t idx);
nub_size_t DNBProcessGetArgumentCount(nub_process_t pid);

// Process events
nub_event_t DNBProcessWaitForEvents(nub_process_t pid, nub_event_t event_mask,
                                    bool wait_for_set,
                                    struct timespec *timeout);
void DNBProcessResetEvents(nub_process_t pid, nub_event_t event_mask);

// Thread functions
const char *DNBThreadGetName(nub_process_t pid, nub_thread_t tid);
nub_bool_t
DNBThreadGetIdentifierInfo(nub_process_t pid, nub_thread_t tid,
                           thread_identifier_info_data_t *ident_info);
nub_state_t DNBThreadGetState(nub_process_t pid, nub_thread_t tid);
nub_bool_t DNBThreadGetRegisterValueByID(nub_process_t pid, nub_thread_t tid,
                                         uint32_t set, uint32_t reg,
                                         DNBRegisterValue *value);
nub_bool_t DNBThreadSetRegisterValueByID(nub_process_t pid, nub_thread_t tid,
                                         uint32_t set, uint32_t reg,
                                         const DNBRegisterValue *value);
nub_size_t DNBThreadGetRegisterContext(nub_process_t pid, nub_thread_t tid,
                                       void *buf, size_t buf_len);
nub_size_t DNBThreadSetRegisterContext(nub_process_t pid, nub_thread_t tid,
                                       const void *buf, size_t buf_len);
uint32_t DNBThreadSaveRegisterState(nub_process_t pid, nub_thread_t tid);
nub_bool_t DNBThreadRestoreRegisterState(nub_process_t pid, nub_thread_t tid,
                                         uint32_t save_id);
nub_bool_t DNBThreadGetRegisterValueByName(nub_process_t pid, nub_thread_t tid,
                                           uint32_t set, const char *name,
                                           DNBRegisterValue *value);
nub_bool_t DNBThreadGetStopReason(nub_process_t pid, nub_thread_t tid,
                                  DNBThreadStopInfo *stop_info);
const char *DNBThreadGetInfo(nub_process_t pid, nub_thread_t tid);
Genealogy::ThreadActivitySP DNBGetGenealogyInfoForThread(nub_process_t pid,
                                                         nub_thread_t tid,
                                                         bool &timed_out);
Genealogy::ProcessExecutableInfoSP DNBGetGenealogyImageInfo(nub_process_t pid,
                                                            size_t idx);
ThreadInfo::QoS DNBGetRequestedQoSForThread(nub_process_t pid, nub_thread_t tid,
                                            nub_addr_t tsd,
                                            uint64_t dti_qos_class_index);
nub_addr_t DNBGetPThreadT(nub_process_t pid, nub_thread_t tid);
nub_addr_t DNBGetDispatchQueueT(nub_process_t pid, nub_thread_t tid);
nub_addr_t
DNBGetTSDAddressForThread(nub_process_t pid, nub_thread_t tid,
                          uint64_t plo_pthread_tsd_base_address_offset,
                          uint64_t plo_pthread_tsd_base_offset,
                          uint64_t plo_pthread_tsd_entry_size);
std::optional<std::pair<cpu_type_t, cpu_subtype_t>>
DNBGetMainBinaryCPUTypes(nub_process_t pid);
JSONGenerator::ObjectSP
DNBGetAllLoadedLibrariesInfos(nub_process_t pid, bool report_load_commands);
JSONGenerator::ObjectSP
DNBGetLibrariesInfoForAddresses(nub_process_t pid,
                                std::vector<uint64_t> &macho_addresses);
JSONGenerator::ObjectSP DNBGetSharedCacheInfo(nub_process_t pid);

//
// Breakpoint functions
nub_bool_t DNBBreakpointSet(nub_process_t pid, nub_addr_t addr, nub_size_t size,
                            nub_bool_t hardware);
nub_bool_t DNBBreakpointClear(nub_process_t pid, nub_addr_t addr);

// Watchpoint functions
nub_bool_t DNBWatchpointSet(nub_process_t pid, nub_addr_t addr, nub_size_t size,
                            uint32_t watch_flags, nub_bool_t hardware);
nub_bool_t DNBWatchpointClear(nub_process_t pid, nub_addr_t addr);
uint32_t DNBWatchpointGetNumSupportedHWP(nub_process_t pid);

uint32_t DNBGetRegisterCPUType();
const DNBRegisterSetInfo *DNBGetRegisterSetInfo(nub_size_t *num_reg_sets);
nub_bool_t DNBGetRegisterInfoByName(const char *reg_name,
                                    DNBRegisterInfo *info);

// Other static nub information calls.
const char *DNBStateAsString(nub_state_t state);
nub_bool_t DNBResolveExecutablePath(const char *path, char *resolved_path,
                                    size_t resolved_path_size);
bool DNBGetOSVersionNumbers(uint64_t *major, uint64_t *minor, uint64_t *patch);
/// \return the iOSSupportVersion of the host OS.
std::string DNBGetMacCatalystVersionString();

/// \return true if debugserver is running in translation
/// (is an x86_64 process on arm64)
bool DNBDebugserverIsTranslated();

bool DNBGetAddressingBits(uint32_t &addressing_bits);

nub_process_t DNBGetParentProcessID(nub_process_t child_pid);

bool DNBProcessIsBeingDebugged(nub_process_t pid);

#endif
