/** @file
 * IPRT - Multiprocessor.
 */

/*
 * Copyright (C) 2008-2019 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */

#ifndef IPRT_INCLUDED_mp_h
#define IPRT_INCLUDED_mp_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <iprt/types.h>


RT_C_DECLS_BEGIN

/** @defgroup grp_rt_mp RTMp - Multiprocessor
 * @ingroup grp_rt
 * @{
 */

/**
 * Gets the identifier of the CPU executing the call.
 *
 * When called from a system mode where scheduling is active, like ring-3 or
 * kernel mode with interrupts enabled on some systems, no assumptions should
 * be made about the current CPU when the call returns.
 *
 * @returns CPU Id.
 */
RTDECL(RTCPUID) RTMpCpuId(void);

/**
 * Get the CPU set index of the CPU executing the call.
 *
 * Same scheduling warnings as for RTMpCpuId().
 *
 * @returns CPU set index.
 */
RTDECL(int) RTMpCurSetIndex(void);

/**
 * Get the CPU set index and identifier of the CPU executing the call.
 *
 * Same scheduling warnings as for RTMpCpuId().
 *
 * @returns CPU set index.
 * @param   pidCpu      Where to return the CPU identifier. (not optional)
 */
RTDECL(int) RTMpCurSetIndexAndId(PRTCPUID pidCpu);

/**
 * Converts a CPU identifier to a CPU set index.
 *
 * This may or may not validate the presence of the CPU.
 *
 * @returns The CPU set index on success, -1 on failure.
 * @param   idCpu       The identifier of the CPU.
 */
RTDECL(int) RTMpCpuIdToSetIndex(RTCPUID idCpu);

/**
 * Converts a CPU set index to a a CPU identifier.
 *
 * This may or may not validate the presence of the CPU, so, use
 * RTMpIsCpuPossible for that.
 *
 * @returns The corresponding CPU identifier, NIL_RTCPUID on failure.
 * @param   iCpu    The CPU set index.
 */
RTDECL(RTCPUID) RTMpCpuIdFromSetIndex(int iCpu);

/**
 * Translates an NT process group member to a CPU set index.
 *
 * @returns CPU set index, -1 if not valid.
 * @param   idxGroup        The CPU group.
 * @param   idxMember       The CPU group member number.
 *
 * @remarks Only available on Windows.
 */
RTDECL(int) RTMpSetIndexFromCpuGroupMember(uint32_t idxGroup, uint32_t idxMember);

/**
 * Gets the member numbers for a CPU group.
 *
 * @returns Maximum number of group members.
 * @param   idxGroup        The CPU group.
 * @param   pcActive        Where to return the number of active members.
 *
 * @remarks Only available on Windows.
 */
RTDECL(uint32_t) RTMpGetCpuGroupCounts(uint32_t idxGroup, uint32_t *pcActive);

/**
 * Get the maximum number of CPU groups.
 *
 * @returns Maximum number of CPU groups.
 *
 * @remarks Only available on Windows.
 */
RTDECL(uint32_t) RTMpGetMaxCpuGroupCount(void);

/**
 * Gets the max CPU identifier (inclusive).
 *
 * Intended for brute force enumerations, but use with
 * care as it may be expensive.
 *
 * @returns The current higest CPU identifier value.
 */
RTDECL(RTCPUID) RTMpGetMaxCpuId(void);

/**
 * Gets the size of a CPU array that is indexed by CPU set index.
 *
 * This takes both online, offline and hot-plugged cpus into account.
 *
 * @returns Number of elements.
 *
 * @remarks Use RTMpCpuIdToSetIndex to convert a RTCPUID into an array index.
 */
RTDECL(uint32_t) RTMpGetArraySize(void);

/**
 * Checks if a CPU exists in the system or may possibly be hotplugged later.
 *
 * @returns true/false accordingly.
 * @param   idCpu       The identifier of the CPU.
 */
RTDECL(bool) RTMpIsCpuPossible(RTCPUID idCpu);

/**
 * Gets set of the CPUs present in the system plus any that may
 * possibly be hotplugged later.
 *
 * @returns pSet.
 * @param   pSet    Where to put the set.
 */
RTDECL(PRTCPUSET) RTMpGetSet(PRTCPUSET pSet);

/**
 * Get the count of CPUs present in the system plus any that may
 * possibly be hotplugged later.
 *
 * @returns The count.
 * @remarks Don't use this for CPU array sizing, use RTMpGetArraySize instead.
 */
RTDECL(RTCPUID) RTMpGetCount(void);

/**
 * Get the count of physical CPU cores present in the system plus any that may
 * possibly be hotplugged later.
 *
 * @returns The number of cores.
 */
RTDECL(RTCPUID) RTMpGetCoreCount(void);

/**
 * Gets set of the CPUs present that are currently online.
 *
 * @returns pSet.
 * @param   pSet    Where to put the set.
 */
RTDECL(PRTCPUSET) RTMpGetOnlineSet(PRTCPUSET pSet);

/**
 * Get the count of CPUs that are currently online.
 *
 * @return The count.
 */
RTDECL(RTCPUID) RTMpGetOnlineCount(void);

/**
 * Get the count of physical CPU cores in the system with one or more online
 * threads.
 *
 * @returns The number of online cores.
 */
RTDECL(RTCPUID) RTMpGetOnlineCoreCount(void);

/**
 * Checks if a CPU is online or not.
 *
 * @returns true/false accordingly.
 * @param   idCpu       The identifier of the CPU.
 */
RTDECL(bool) RTMpIsCpuOnline(RTCPUID idCpu);


/**
 * Gets set of the CPUs present in the system.
 *
 * @returns pSet.
 * @param   pSet    Where to put the set.
 */
RTDECL(PRTCPUSET) RTMpGetPresentSet(PRTCPUSET pSet);

/**
 * Get the count of CPUs that are present in the system.
 *
 * @return The count.
 */
RTDECL(RTCPUID) RTMpGetPresentCount(void);

/**
 * Get the count of physical CPU cores present in the system.
 *
 * @returns The number of cores.
 */
RTDECL(RTCPUID) RTMpGetPresentCoreCount(void);

/**
 * Checks if a CPU is present in the system.
 *
 * @returns true/false accordingly.
 * @param   idCpu       The identifier of the CPU.
 */
RTDECL(bool) RTMpIsCpuPresent(RTCPUID idCpu);


/**
 * Get the current frequency of a CPU.
 *
 * The CPU must be online.
 *
 * @returns The frequency as MHz. 0 if the CPU is offline
 *          or the information is not available.
 * @param   idCpu       The identifier of the CPU.
 */
RTDECL(uint32_t) RTMpGetCurFrequency(RTCPUID idCpu);

/**
 * Get the maximum frequency of a CPU.
 *
 * The CPU must be online.
 *
 * @returns The frequency as MHz. 0 if the CPU is offline
 *          or the information is not available.
 * @param   idCpu       The identifier of the CPU.
 */
RTDECL(uint32_t) RTMpGetMaxFrequency(RTCPUID idCpu);

/**
 * Get the CPU description string.
 *
 * The CPU must be online.
 *
 * @returns IPRT status code.
 * @param   idCpu       The identifier of the CPU.  NIL_RTCPUID can be used to
 *                      indicate the current CPU.
 * @param   pszBuf      The output buffer.
 * @param   cbBuf       The size of the output buffer.
 */
RTDECL(int) RTMpGetDescription(RTCPUID idCpu, char *pszBuf, size_t cbBuf);


#ifdef IN_RING0

/**
 * Check if there's work (DPCs on Windows) pending on the current CPU.
 *
 * @return true if there's pending work on the current CPU, false otherwise.
 */
RTDECL(bool) RTMpIsCpuWorkPending(void);


/**
 * Worker function passed to RTMpOnAll, RTMpOnOthers and RTMpOnSpecific that
 * is to be called on the target cpus.
 *
 * @param   idCpu       The identifier for the CPU the function is called on.
 * @param   pvUser1     The 1st user argument.
 * @param   pvUser2     The 2nd user argument.
 */
typedef DECLCALLBACK(void) FNRTMPWORKER(RTCPUID idCpu, void *pvUser1, void *pvUser2);
/** Pointer to a FNRTMPWORKER. */
typedef FNRTMPWORKER *PFNRTMPWORKER;

/** @name RTMPON_F_XXX - RTMpOn flags.
 * @{ */
/** Caller doesn't care if pfnWorker is executed at the same time on the
 *  specified CPUs or not, as long as it gets executed. */
#define RTMPON_F_WHATEVER_EXEC      0
/** The caller insists on pfnWorker being executed more or less concurrently
 * on the specified CPUs. */
#define RTMPON_F_CONCURRENT_EXEC    RT_BIT_32(1)
/** Mask of valid bits. */
#define RTMPON_F_VALID_MASK         UINT32_C(0x00000001)
/** @}*/

/**
 * Checks if the RTMpOnAll() is safe with regards to all threads executing
 * concurrently.
 *
 * If for instance, the RTMpOnAll() is implemented in a way where the threads
 * might cause a classic deadlock, it is considered -not- concurrent safe.
 * Windows currently is one such platform where it isn't safe.
 *
 * @returns true if RTMpOnAll() is concurrent safe, false otherwise.
 */
RTDECL(bool) RTMpOnAllIsConcurrentSafe(void);

/**
 * Executes a function on each (online) CPU in the system.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_NOT_SUPPORTED if this kind of operation isn't supported by the system.
 *
 * @param   pfnWorker       The worker function.
 * @param   pvUser1         The first user argument for the worker.
 * @param   pvUser2         The second user argument for the worker.
 *
 * @remarks The execution isn't in any way guaranteed to be simultaneous,
 *          it might even be serial (cpu by cpu).
 */
RTDECL(int) RTMpOnAll(PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2);

/**
 * Executes a function on all other (online) CPUs in the system.
 *
 * The caller must disable preemption prior to calling this API if the outcome
 * is to make any sense. But do *not* disable interrupts.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_NOT_SUPPORTED if this kind of operation isn't supported by the system.
 *
 * @param   pfnWorker       The worker function.
 * @param   pvUser1         The first user argument for the worker.
 * @param   pvUser2         The second user argument for the worker.
 *
 * @remarks The execution isn't in any way guaranteed to be simultaneous,
 *          it might even be serial (cpu by cpu).
 */
RTDECL(int) RTMpOnOthers(PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2);

/**
 * Executes a function on a specific CPU in the system.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_NOT_SUPPORTED if this kind of operation isn't supported by the system.
 * @retval  VERR_CPU_OFFLINE if the CPU is offline.
 * @retval  VERR_CPU_NOT_FOUND if the CPU wasn't found.
 *
 * @param   idCpu           The id of the CPU.
 * @param   pfnWorker       The worker function.
 * @param   pvUser1         The first user argument for the worker.
 * @param   pvUser2         The second user argument for the worker.
 */
RTDECL(int) RTMpOnSpecific(RTCPUID idCpu, PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2);

/**
 * Executes a function on two specific CPUs in the system.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_NOT_SUPPORTED if this kind of operation isn't supported by the
 *          system or if the specified modifier flag isn't supported.
 * @retval  VERR_CPU_OFFLINE if one or more of the CPUs are offline (see
 *          remarks).
 * @retval  VERR_CPU_NOT_FOUND if on or both of the CPUs weren't found.
 * @retval  VERR_NOT_ALL_CPUS_SHOWED if one of the CPUs didn't show.
 *
 * @param   idCpu1          The id of the first CPU.
 * @param   idCpu2          The id of the second CPU.
 * @param   fFlags          Combination of RTMPON_F_XXX flags.
 * @param   pfnWorker       The worker function.
 * @param   pvUser1         The first user argument for the worker.
 * @param   pvUser2         The second user argument for the worker.
 *
 * @remarks There is a possible race between one (or both) of the CPUs going
 *          offline while setting up the call.  The worker function must take
 *          this into account.
 */
RTDECL(int) RTMpOnPair(RTCPUID idCpu1, RTCPUID idCpu2, uint32_t fFlags, PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2);

/**
 * Indicates whether RTMpOnPair supports running the pfnWorker concurrently on
 * both CPUs using RTMPON_F_CONCURRENT_EXEC.
 *
 * @returns true if supported, false if not.
 */
RTDECL(bool) RTMpOnPairIsConcurrentExecSupported(void);


/**
 * Pokes the specified CPU.
 *
 * This should cause the execution on the CPU to be interrupted and forcing it
 * to enter kernel context. It is optimized version of a RTMpOnSpecific call
 * with a worker which returns immediately.
 *
 * @returns IPRT status code.
 * @retval  VERR_NOT_SUPPORTED if this kind of operation isn't supported by the
 *          system. The caller must not automatically assume that this API works
 *          when any of the RTMpOn* APIs works. This is because not all systems
 *          supports unicast MP events and this API will not be implemented as a
 *          broadcast.
 * @retval  VERR_CPU_OFFLINE if the CPU is offline.
 * @retval  VERR_CPU_NOT_FOUND if the CPU wasn't found.
 *
 * @param   idCpu           The id of the CPU to poke.
 */
RTDECL(int) RTMpPokeCpu(RTCPUID idCpu);


/**
 * MP event, see FNRTMPNOTIFICATION.
 */
typedef enum RTMPEVENT
{
    /** The CPU goes online. */
    RTMPEVENT_ONLINE = 1,
    /** The CPU goes offline. */
    RTMPEVENT_OFFLINE
} RTMPEVENT;

/**
 * Notification callback.
 *
 * The context this is called in differs a bit from platform to platform, so be
 * careful while in here.
 *
 * On Windows we're running with IRQL=PASSIVE_LEVEL (reschedulable) according to
 * the KeRegisterProcessorChangeCallback documentation - unrestricted API
 * access. Probably not being called on the onlined/offlined CPU...
 *
 * On Solaris we're holding the cpu_lock, IPL/SPL/PIL is not yet known, however
 * we will most likely -not- be firing on the CPU going offline/online.
 *
 * On Linux it looks like we're called with preemption enabled on any CPU and
 * not necessarily on the CPU going offline/online.
 *
 * There is no callbacks for darwin at the moment, due to lack of suitable KPI.
 *
 * @param   idCpu       The CPU this applies to.
 * @param   enmEvent    The event.
 * @param   pvUser      The user argument.
 */
typedef DECLCALLBACK(void) FNRTMPNOTIFICATION(RTMPEVENT enmEvent, RTCPUID idCpu, void *pvUser);
/** Pointer to a FNRTMPNOTIFICATION(). */
typedef FNRTMPNOTIFICATION *PFNRTMPNOTIFICATION;

/**
 * Registers a notification callback for cpu events.
 *
 * On platforms which doesn't do cpu offline/online events this API
 * will just be a no-op that pretends to work.
 *
 * @todo We'll be adding a flag to this soon to indicate whether the callback should be called on all
 *       CPUs that are currently online while it's being registered. This is to help avoid some race
 *       conditions (we'll hopefully be able to implement this on linux, solaris/win is no issue).
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_NO_MEMORY if a registration record cannot be allocated.
 * @retval  VERR_ALREADY_EXISTS if the pfnCallback and pvUser already exist
 *          in the callback list.
 *
 * @param   pfnCallback     The callback.
 * @param   pvUser          The user argument to the callback function.
 */
RTDECL(int) RTMpNotificationRegister(PFNRTMPNOTIFICATION pfnCallback, void *pvUser);

/**
 * This deregisters a notification callback registered via RTMpNotificationRegister().
 *
 * The pfnCallback and pvUser arguments must be identical to the registration call
 * of we won't find the right entry.
 *
 * @returns IPRT status code.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_NOT_FOUND if no matching entry was found.
 *
 * @param   pfnCallback     The callback.
 * @param   pvUser          The user argument to the callback function.
 */
RTDECL(int) RTMpNotificationDeregister(PFNRTMPNOTIFICATION pfnCallback, void *pvUser);

#endif /* IN_RING0 */

/** @} */

RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_mp_h */

