/* $Id: mp-r0drv-linux.c $ */
/** @file
 * IPRT - Multiprocessor, Ring-0 Driver, Linux.
 */

/*
 * Copyright (C) 2008-2017 Oracle Corporation
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "the-linux-kernel.h"
#include "internal/iprt.h"

#include <iprt/mp.h>
#include <iprt/cpuset.h>
#include <iprt/err.h>
#include <iprt/asm.h>
#include <iprt/thread.h>
#include "r0drv/mp-r0drv.h"

#ifdef nr_cpumask_bits
# define VBOX_NR_CPUMASK_BITS   nr_cpumask_bits
#else
# define VBOX_NR_CPUMASK_BITS   NR_CPUS
#endif

RTDECL(RTCPUID) RTMpCpuId(void)
{
    return smp_processor_id();
}
RT_EXPORT_SYMBOL(RTMpCpuId);


RTDECL(int) RTMpCurSetIndex(void)
{
    return smp_processor_id();
}
RT_EXPORT_SYMBOL(RTMpCurSetIndex);


RTDECL(int) RTMpCurSetIndexAndId(PRTCPUID pidCpu)
{
    return *pidCpu = smp_processor_id();
}
RT_EXPORT_SYMBOL(RTMpCurSetIndexAndId);


RTDECL(int) RTMpCpuIdToSetIndex(RTCPUID idCpu)
{
    return idCpu < RTCPUSET_MAX_CPUS && idCpu < VBOX_NR_CPUMASK_BITS ? (int)idCpu : -1;
}
RT_EXPORT_SYMBOL(RTMpCpuIdToSetIndex);


RTDECL(RTCPUID) RTMpCpuIdFromSetIndex(int iCpu)
{
    return iCpu < VBOX_NR_CPUMASK_BITS ? (RTCPUID)iCpu : NIL_RTCPUID;
}
RT_EXPORT_SYMBOL(RTMpCpuIdFromSetIndex);


RTDECL(RTCPUID) RTMpGetMaxCpuId(void)
{
    return VBOX_NR_CPUMASK_BITS - 1; //???
}
RT_EXPORT_SYMBOL(RTMpGetMaxCpuId);


RTDECL(bool) RTMpIsCpuPossible(RTCPUID idCpu)
{
#if defined(CONFIG_SMP)
    if (RT_UNLIKELY(idCpu >= VBOX_NR_CPUMASK_BITS))
        return false;

# if defined(cpu_possible)
    return cpu_possible(idCpu);
# else /* < 2.5.29 */
    return idCpu < (RTCPUID)smp_num_cpus;
# endif
#else
    return idCpu == RTMpCpuId();
#endif
}
RT_EXPORT_SYMBOL(RTMpIsCpuPossible);


RTDECL(PRTCPUSET) RTMpGetSet(PRTCPUSET pSet)
{
    RTCPUID idCpu;

    RTCpuSetEmpty(pSet);
    idCpu = RTMpGetMaxCpuId();
    do
    {
        if (RTMpIsCpuPossible(idCpu))
            RTCpuSetAdd(pSet, idCpu);
    } while (idCpu-- > 0);
    return pSet;
}
RT_EXPORT_SYMBOL(RTMpGetSet);


RTDECL(RTCPUID) RTMpGetCount(void)
{
#ifdef CONFIG_SMP
# if defined(CONFIG_HOTPLUG_CPU) /* introduced & uses cpu_present */
    return num_present_cpus();
# elif defined(num_possible_cpus)
    return num_possible_cpus();
# elif LINUX_VERSION_CODE < KERNEL_VERSION(2, 5, 0)
    return smp_num_cpus;
# else
    RTCPUSET Set;
    RTMpGetSet(&Set);
    return RTCpuSetCount(&Set);
# endif
#else
    return 1;
#endif
}
RT_EXPORT_SYMBOL(RTMpGetCount);


RTDECL(bool) RTMpIsCpuOnline(RTCPUID idCpu)
{
#ifdef CONFIG_SMP
    if (RT_UNLIKELY(idCpu >= VBOX_NR_CPUMASK_BITS))
        return false;
# ifdef cpu_online
    return cpu_online(idCpu);
# else /* 2.4: */
    return cpu_online_map & RT_BIT_64(idCpu);
# endif
#else
    return idCpu == RTMpCpuId();
#endif
}
RT_EXPORT_SYMBOL(RTMpIsCpuOnline);


RTDECL(PRTCPUSET) RTMpGetOnlineSet(PRTCPUSET pSet)
{
#ifdef CONFIG_SMP
    RTCPUID idCpu;

    RTCpuSetEmpty(pSet);
    idCpu = RTMpGetMaxCpuId();
    do
    {
        if (RTMpIsCpuOnline(idCpu))
            RTCpuSetAdd(pSet, idCpu);
    } while (idCpu-- > 0);
#else
    RTCpuSetEmpty(pSet);
    RTCpuSetAdd(pSet, RTMpCpuId());
#endif
    return pSet;
}
RT_EXPORT_SYMBOL(RTMpGetOnlineSet);


RTDECL(RTCPUID) RTMpGetOnlineCount(void)
{
#ifdef CONFIG_SMP
# if defined(num_online_cpus)
    return num_online_cpus();
# else
    RTCPUSET Set;
    RTMpGetOnlineSet(&Set);
    return RTCpuSetCount(&Set);
# endif
#else
    return 1;
#endif
}
RT_EXPORT_SYMBOL(RTMpGetOnlineCount);


RTDECL(bool) RTMpIsCpuWorkPending(void)
{
    /** @todo (not used on non-Windows platforms yet). */
    return false;
}
RT_EXPORT_SYMBOL(RTMpIsCpuWorkPending);


/**
 * Wrapper between the native linux per-cpu callbacks and PFNRTWORKER.
 *
 * @param   pvInfo      Pointer to the RTMPARGS package.
 */
static void rtmpLinuxWrapper(void *pvInfo)
{
    PRTMPARGS pArgs = (PRTMPARGS)pvInfo;
    ASMAtomicIncU32(&pArgs->cHits);
    pArgs->pfnWorker(RTMpCpuId(), pArgs->pvUser1, pArgs->pvUser2);
}


/**
 * Wrapper between the native linux per-cpu callbacks and PFNRTWORKER, does hit
 * increment after calling the worker.
 *
 * @param   pvInfo      Pointer to the RTMPARGS package.
 */
static void rtmpLinuxWrapperPostInc(void *pvInfo)
{
    PRTMPARGS pArgs = (PRTMPARGS)pvInfo;
    pArgs->pfnWorker(RTMpCpuId(), pArgs->pvUser1, pArgs->pvUser2);
    ASMAtomicIncU32(&pArgs->cHits);
}


/**
 * Wrapper between the native linux all-cpu callbacks and PFNRTWORKER.
 *
 * @param   pvInfo      Pointer to the RTMPARGS package.
 */
static void rtmpLinuxAllWrapper(void *pvInfo)
{
    PRTMPARGS  pArgs      = (PRTMPARGS)pvInfo;
    PRTCPUSET  pWorkerSet = pArgs->pWorkerSet;
    RTCPUID    idCpu      = RTMpCpuId();
    Assert(!RTThreadPreemptIsEnabled(NIL_RTTHREAD));

    if (RTCpuSetIsMember(pWorkerSet, idCpu))
    {
        pArgs->pfnWorker(idCpu, pArgs->pvUser1, pArgs->pvUser2);
        RTCpuSetDel(pWorkerSet, idCpu);
    }
}


RTDECL(int) RTMpOnAll(PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2)
{
    IPRT_LINUX_SAVE_EFL_AC();
    int rc;
    RTMPARGS Args;
    RTCPUSET OnlineSet;
    RTCPUID  idCpu;
    uint32_t cLoops;

    RTTHREADPREEMPTSTATE PreemptState = RTTHREADPREEMPTSTATE_INITIALIZER;

    Args.pfnWorker  = pfnWorker;
    Args.pvUser1    = pvUser1;
    Args.pvUser2    = pvUser2;
    Args.idCpu      = NIL_RTCPUID;
    Args.cHits      = 0;

    RTThreadPreemptDisable(&PreemptState);
    RTMpGetOnlineSet(&OnlineSet);
    Args.pWorkerSet = &OnlineSet;
    idCpu = RTMpCpuId();

    if (RTCpuSetCount(&OnlineSet) > 1)
    {
        /* Fire the function on all other CPUs without waiting for completion. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
        rc = smp_call_function(rtmpLinuxAllWrapper, &Args, 0 /* wait */);
#else
        rc = smp_call_function(rtmpLinuxAllWrapper, &Args, 0 /* retry */, 0 /* wait */);
#endif
        Assert(!rc); NOREF(rc);
    }

    /* Fire the function on this CPU. */
    Args.pfnWorker(idCpu, Args.pvUser1, Args.pvUser2);
    RTCpuSetDel(Args.pWorkerSet, idCpu);

    /* Wait for all of them finish. */
    cLoops = 64000;
    while (!RTCpuSetIsEmpty(Args.pWorkerSet))
    {
        /* Periodically check if any CPU in the wait set has gone offline, if so update the wait set. */
        if (!cLoops--)
        {
            RTCPUSET OnlineSetNow;
            RTMpGetOnlineSet(&OnlineSetNow);
            RTCpuSetAnd(Args.pWorkerSet, &OnlineSetNow);

            cLoops = 64000;
        }

        ASMNopPause();
    }

    RTThreadPreemptRestore(&PreemptState);
    IPRT_LINUX_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTMpOnAll);


RTDECL(int) RTMpOnOthers(PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2)
{
    IPRT_LINUX_SAVE_EFL_AC();
    int rc;
    RTMPARGS Args;

    RTTHREADPREEMPTSTATE PreemptState = RTTHREADPREEMPTSTATE_INITIALIZER;
    Args.pfnWorker = pfnWorker;
    Args.pvUser1 = pvUser1;
    Args.pvUser2 = pvUser2;
    Args.idCpu = NIL_RTCPUID;
    Args.cHits = 0;

    RTThreadPreemptDisable(&PreemptState);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
    rc = smp_call_function(rtmpLinuxWrapper, &Args, 1 /* wait */);
#else /* older kernels */
    rc = smp_call_function(rtmpLinuxWrapper, &Args, 0 /* retry */, 1 /* wait */);
#endif /* older kernels */
    RTThreadPreemptRestore(&PreemptState);

    Assert(rc == 0); NOREF(rc);
    IPRT_LINUX_RESTORE_EFL_AC();
    return VINF_SUCCESS;
}
RT_EXPORT_SYMBOL(RTMpOnOthers);


#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 27)
/**
 * Wrapper between the native linux per-cpu callbacks and PFNRTWORKER
 * employed by RTMpOnPair on older kernels that lacks smp_call_function_many.
 *
 * @param   pvInfo      Pointer to the RTMPARGS package.
 */
static void rtMpLinuxOnPairWrapper(void *pvInfo)
{
    PRTMPARGS pArgs = (PRTMPARGS)pvInfo;
    RTCPUID   idCpu = RTMpCpuId();

    if (   idCpu == pArgs->idCpu
        || idCpu == pArgs->idCpu2)
    {
        pArgs->pfnWorker(idCpu, pArgs->pvUser1, pArgs->pvUser2);
        ASMAtomicIncU32(&pArgs->cHits);
    }
}
#endif


RTDECL(int) RTMpOnPair(RTCPUID idCpu1, RTCPUID idCpu2, uint32_t fFlags, PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2)
{
    IPRT_LINUX_SAVE_EFL_AC();
    int rc;
    RTTHREADPREEMPTSTATE PreemptState = RTTHREADPREEMPTSTATE_INITIALIZER;

    AssertReturn(idCpu1 != idCpu2, VERR_INVALID_PARAMETER);
    AssertReturn(!(fFlags & RTMPON_F_VALID_MASK), VERR_INVALID_FLAGS);

    /*
     * Check that both CPUs are online before doing the broadcast call.
     */
    RTThreadPreemptDisable(&PreemptState);
    if (   RTMpIsCpuOnline(idCpu1)
        && RTMpIsCpuOnline(idCpu2))
    {
        /*
         * Use the smp_call_function variant taking a cpu mask where available,
         * falling back on broadcast with filter.  Slight snag if one of the
         * CPUs is the one we're running on, we must do the call and the post
         * call wait ourselves.
         */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28)
        /* 2.6.28 introduces CONFIG_CPUMASK_OFFSTACK */
        cpumask_var_t DstCpuMask;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
        cpumask_t   DstCpuMask;
#endif
        RTCPUID     idCpuSelf = RTMpCpuId();
        bool const  fCallSelf = idCpuSelf == idCpu1 || idCpuSelf == idCpu2;
        RTMPARGS    Args;
        Args.pfnWorker = pfnWorker;
        Args.pvUser1 = pvUser1;
        Args.pvUser2 = pvUser2;
        Args.idCpu   = idCpu1;
        Args.idCpu2  = idCpu2;
        Args.cHits   = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 30)
        if (!zalloc_cpumask_var(&DstCpuMask, GFP_KERNEL))
            return VERR_NO_MEMORY;
        cpumask_set_cpu(idCpu1, DstCpuMask);
        cpumask_set_cpu(idCpu2, DstCpuMask);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28)
        if (!alloc_cpumask_var(&DstCpuMask, GFP_KERNEL))
            return VERR_NO_MEMORY;
        cpumask_clear(DstCpuMask);
        cpumask_set_cpu(idCpu1, DstCpuMask);
        cpumask_set_cpu(idCpu2, DstCpuMask);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
        cpus_clear(DstCpuMask);
        cpu_set(idCpu1, DstCpuMask);
        cpu_set(idCpu2, DstCpuMask);
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28)
        smp_call_function_many(DstCpuMask, rtmpLinuxWrapperPostInc, &Args, !fCallSelf /* wait */);
        rc = 0;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
        rc = smp_call_function_mask(DstCpuMask, rtmpLinuxWrapperPostInc, &Args, !fCallSelf /* wait */);
#else /* older kernels */
        rc = smp_call_function(rtMpLinuxOnPairWrapper, &Args, 0 /* retry */, !fCallSelf /* wait */);
#endif /* older kernels */
        Assert(rc == 0);

        /* Call ourselves if necessary and wait for the other party to be done. */
        if (fCallSelf)
        {
            uint32_t cLoops = 0;
            rtmpLinuxWrapper(&Args);
            while (ASMAtomicReadU32(&Args.cHits) < 2)
            {
                if ((cLoops & 0x1ff) == 0 && !RTMpIsCpuOnline(idCpuSelf == idCpu1 ? idCpu2 : idCpu1))
                    break;
                cLoops++;
                ASMNopPause();
            }
        }

        Assert(Args.cHits <= 2);
        if (Args.cHits == 2)
            rc = VINF_SUCCESS;
        else if (Args.cHits == 1)
            rc = VERR_NOT_ALL_CPUS_SHOWED;
        else if (Args.cHits == 0)
            rc = VERR_CPU_OFFLINE;
        else
            rc = VERR_CPU_IPE_1;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28)
        free_cpumask_var(DstCpuMask);
#endif
    }
    /*
     * A CPU must be present to be considered just offline.
     */
    else if (   RTMpIsCpuPresent(idCpu1)
             && RTMpIsCpuPresent(idCpu2))
        rc = VERR_CPU_OFFLINE;
    else
        rc = VERR_CPU_NOT_FOUND;
    RTThreadPreemptRestore(&PreemptState);;
    IPRT_LINUX_RESTORE_EFL_AC();
    return rc;
}
RT_EXPORT_SYMBOL(RTMpOnPair);


RTDECL(bool) RTMpOnPairIsConcurrentExecSupported(void)
{
    return true;
}
RT_EXPORT_SYMBOL(RTMpOnPairIsConcurrentExecSupported);


#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 19)
/**
 * Wrapper between the native linux per-cpu callbacks and PFNRTWORKER
 * employed by RTMpOnSpecific on older kernels that lacks smp_call_function_single.
 *
 * @param   pvInfo      Pointer to the RTMPARGS package.
 */
static void rtmpOnSpecificLinuxWrapper(void *pvInfo)
{
    PRTMPARGS pArgs = (PRTMPARGS)pvInfo;
    RTCPUID idCpu = RTMpCpuId();

    if (idCpu == pArgs->idCpu)
    {
        pArgs->pfnWorker(idCpu, pArgs->pvUser1, pArgs->pvUser2);
        ASMAtomicIncU32(&pArgs->cHits);
    }
}
#endif


RTDECL(int) RTMpOnSpecific(RTCPUID idCpu, PFNRTMPWORKER pfnWorker, void *pvUser1, void *pvUser2)
{
    IPRT_LINUX_SAVE_EFL_AC();
    int rc;
    RTMPARGS Args;

    RTTHREADPREEMPTSTATE PreemptState = RTTHREADPREEMPTSTATE_INITIALIZER;
    Args.pfnWorker = pfnWorker;
    Args.pvUser1 = pvUser1;
    Args.pvUser2 = pvUser2;
    Args.idCpu = idCpu;
    Args.cHits = 0;

    if (!RTMpIsCpuPossible(idCpu))
        return VERR_CPU_NOT_FOUND;

    RTThreadPreemptDisable(&PreemptState);
    if (idCpu != RTMpCpuId())
    {
        if (RTMpIsCpuOnline(idCpu))
        {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
            rc = smp_call_function_single(idCpu, rtmpLinuxWrapper, &Args, 1 /* wait */);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
            rc = smp_call_function_single(idCpu, rtmpLinuxWrapper, &Args, 0 /* retry */, 1 /* wait */);
#else /* older kernels */
            rc = smp_call_function(rtmpOnSpecificLinuxWrapper, &Args, 0 /* retry */, 1 /* wait */);
#endif /* older kernels */
            Assert(rc == 0);
            rc = Args.cHits ? VINF_SUCCESS : VERR_CPU_OFFLINE;
        }
        else
            rc = VERR_CPU_OFFLINE;
    }
    else
    {
        rtmpLinuxWrapper(&Args);
        rc = VINF_SUCCESS;
    }
    RTThreadPreemptRestore(&PreemptState);;

    NOREF(rc);
    IPRT_LINUX_RESTORE_EFL_AC();
    return rc;
}
RT_EXPORT_SYMBOL(RTMpOnSpecific);


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
/**
 * Dummy callback used by RTMpPokeCpu.
 *
 * @param   pvInfo      Ignored.
 */
static void rtmpLinuxPokeCpuCallback(void *pvInfo)
{
    NOREF(pvInfo);
}
#endif


RTDECL(int) RTMpPokeCpu(RTCPUID idCpu)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
    int rc;
    IPRT_LINUX_SAVE_EFL_AC();

    if (!RTMpIsCpuPossible(idCpu))
        return VERR_CPU_NOT_FOUND;
    if (!RTMpIsCpuOnline(idCpu))
        return VERR_CPU_OFFLINE;

# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
    rc = smp_call_function_single(idCpu, rtmpLinuxPokeCpuCallback, NULL, 0 /* wait */);
# elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19)
    rc = smp_call_function_single(idCpu, rtmpLinuxPokeCpuCallback, NULL, 0 /* retry */, 0 /* wait */);
# else  /* older kernels */
#  error oops
# endif /* older kernels */
    NOREF(rc);
    Assert(rc == 0);
    IPRT_LINUX_RESTORE_EFL_AC();
    return VINF_SUCCESS;

#else  /* older kernels */
    /* no unicast here? */
    return VERR_NOT_SUPPORTED;
#endif /* older kernels */
}
RT_EXPORT_SYMBOL(RTMpPokeCpu);


RTDECL(bool) RTMpOnAllIsConcurrentSafe(void)
{
    return true;
}
RT_EXPORT_SYMBOL(RTMpOnAllIsConcurrentSafe);

