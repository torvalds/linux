/* $Id: mpnotification-r0drv-linux.c $ */
/** @file
 * IPRT - Multiprocessor Event Notifications, Ring-0 Driver, Linux.
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

#include <iprt/asm-amd64-x86.h>
#include <iprt/err.h>
#include <iprt/cpuset.h>
#include <iprt/thread.h>
#include "r0drv/mp-r0drv.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)

static enum cpuhp_state g_rtR0MpOnline;

/*
 * Linux 4.10 completely removed CPU notifiers. So let's switch to CPU hotplug
 * notification.
 */

static int rtR0MpNotificationLinuxOnline(unsigned int cpu)
{
    RTCPUID idCpu = RTMpCpuIdFromSetIndex(cpu);
    rtMpNotificationDoCallbacks(RTMPEVENT_ONLINE, idCpu);
    return 0;
}

static int rtR0MpNotificationLinuxOffline(unsigned int cpu)
{
    RTCPUID idCpu = RTMpCpuIdFromSetIndex(cpu);
    rtMpNotificationDoCallbacks(RTMPEVENT_OFFLINE, idCpu);
    return 0;
}

DECLHIDDEN(int) rtR0MpNotificationNativeInit(void)
{
    int rc;
    IPRT_LINUX_SAVE_EFL_AC();
    rc = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN, "vboxdrv:online",
                                   rtR0MpNotificationLinuxOnline, rtR0MpNotificationLinuxOffline);
    IPRT_LINUX_RESTORE_EFL_AC();
    /*
     * cpuhp_setup_state_nocalls() returns a positive state number for
     * CPUHP_AP_ONLINE_DYN or -ENOSPC if there is no free slot available
     * (see cpuhp_reserve_state / definition of CPUHP_AP_ONLINE_DYN).
     */
    AssertMsgReturn(rc > 0, ("%d\n", rc), RTErrConvertFromErrno(rc));
    g_rtR0MpOnline = rc;
    return VINF_SUCCESS;
}


DECLHIDDEN(void) rtR0MpNotificationNativeTerm(void)
{
    IPRT_LINUX_SAVE_EFL_AC();
    cpuhp_remove_state_nocalls(g_rtR0MpOnline);
    IPRT_LINUX_RESTORE_EFL_AC();
}

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 71) && defined(CONFIG_SMP)

static int rtMpNotificationLinuxCallback(struct notifier_block *pNotifierBlock, unsigned long ulNativeEvent, void *pvCpu);

/**
 * The notifier block we use for registering the callback.
 */
static struct notifier_block g_NotifierBlock =
{
    .notifier_call = rtMpNotificationLinuxCallback,
    .next = NULL,
    .priority = 0
};

# ifdef CPU_DOWN_FAILED
/**
 * The set of CPUs we've seen going offline recently.
 */
static RTCPUSET g_MpPendingOfflineSet;
# endif


/**
 * The native callback.
 *
 * @returns NOTIFY_DONE.
 * @param   pNotifierBlock  Pointer to g_NotifierBlock.
 * @param   ulNativeEvent   The native event.
 * @param   pvCpu           The cpu id cast into a pointer value.
 *
 * @remarks This can fire with preemption enabled and on any CPU.
 */
static int rtMpNotificationLinuxCallback(struct notifier_block *pNotifierBlock, unsigned long ulNativeEvent, void *pvCpu)
{
    bool fProcessEvent = false;
    RTCPUID idCpu      = (uintptr_t)pvCpu;
    NOREF(pNotifierBlock);

    /*
     * Note that redhat/CentOS ported _some_ of the FROZEN macros
     * back to their 2.6.18-92.1.10.el5 kernel but actually don't
     * use them. Thus we have to test for both CPU_TASKS_FROZEN and
     * the individual event variants.
     */
    switch (ulNativeEvent)
    {
        /*
         * Pick up online events or failures to go offline.
         * Ignore failure events for CPUs we didn't see go offline.
         */
# ifdef CPU_DOWN_FAILED
        case CPU_DOWN_FAILED:
#  if defined(CPU_TASKS_FROZEN) && defined(CPU_DOWN_FAILED_FROZEN)
        case CPU_DOWN_FAILED_FROZEN:
#  endif
            if (!RTCpuSetIsMember(&g_MpPendingOfflineSet, idCpu))
                break;      /* fProcessEvents = false */
        /* fall thru */
# endif
        case CPU_ONLINE:
# if defined(CPU_TASKS_FROZEN) && defined(CPU_ONLINE_FROZEN)
        case CPU_ONLINE_FROZEN:
# endif
# ifdef CPU_DOWN_FAILED
            RTCpuSetDel(&g_MpPendingOfflineSet, idCpu);
# endif
            fProcessEvent = true;
            break;

        /*
         * Pick the earliest possible offline event.
         * The only important thing here is that we get the event and that
         * it's exactly one.
         */
# ifdef CPU_DOWN_PREPARE
        case CPU_DOWN_PREPARE:
#  if defined(CPU_TASKS_FROZEN) && defined(CPU_DOWN_PREPARE_FROZEN)
        case CPU_DOWN_PREPARE_FROZEN:
#  endif
            fProcessEvent = true;
# else
        case CPU_DEAD:
#  if defined(CPU_TASKS_FROZEN) && defined(CPU_DEAD_FROZEN)
        case CPU_DEAD_FROZEN:
#  endif
            /* Don't process CPU_DEAD notifications. */
# endif
# ifdef CPU_DOWN_FAILED
            RTCpuSetAdd(&g_MpPendingOfflineSet, idCpu);
# endif
            break;
    }

    if (!fProcessEvent)
        return NOTIFY_DONE;

    switch (ulNativeEvent)
    {
# ifdef CPU_DOWN_FAILED
        case CPU_DOWN_FAILED:
#  if defined(CPU_TASKS_FROZEN) && defined(CPU_DOWN_FAILED_FROZEN)
        case CPU_DOWN_FAILED_FROZEN:
#  endif
# endif
        case CPU_ONLINE:
# if defined(CPU_TASKS_FROZEN) && defined(CPU_ONLINE_FROZEN)
        case CPU_ONLINE_FROZEN:
# endif
            rtMpNotificationDoCallbacks(RTMPEVENT_ONLINE, idCpu);
            break;

# ifdef CPU_DOWN_PREPARE
        case CPU_DOWN_PREPARE:
#  if defined(CPU_TASKS_FROZEN) && defined(CPU_DOWN_PREPARE_FROZEN)
        case CPU_DOWN_PREPARE_FROZEN:
#  endif
            rtMpNotificationDoCallbacks(RTMPEVENT_OFFLINE, idCpu);
            break;
# endif
    }

    return NOTIFY_DONE;
}


DECLHIDDEN(int) rtR0MpNotificationNativeInit(void)
{
    int rc;
    IPRT_LINUX_SAVE_EFL_AC();

# ifdef CPU_DOWN_FAILED
    RTCpuSetEmpty(&g_MpPendingOfflineSet);
# endif

    rc = register_cpu_notifier(&g_NotifierBlock);
    IPRT_LINUX_RESTORE_EFL_AC();
    AssertMsgReturn(!rc, ("%d\n", rc), RTErrConvertFromErrno(rc));
    return VINF_SUCCESS;
}


DECLHIDDEN(void) rtR0MpNotificationNativeTerm(void)
{
    IPRT_LINUX_SAVE_EFL_AC();
    unregister_cpu_notifier(&g_NotifierBlock);
    IPRT_LINUX_RESTORE_EFL_AC();
}

#else   /* Not supported / Not needed */

DECLHIDDEN(int) rtR0MpNotificationNativeInit(void)
{
    return VINF_SUCCESS;
}

DECLHIDDEN(void) rtR0MpNotificationNativeTerm(void)
{
}

#endif  /* Not supported / Not needed */

