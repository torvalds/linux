/* $Id: thread2-r0drv-linux.c $ */
/** @file
 * IPRT - Threads (Part 2), Ring-0 Driver, Linux.
 */

/*
 * Copyright (C) 2006-2017 Oracle Corporation
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

#include <iprt/assert.h>
#include <iprt/thread.h>
#include <iprt/err.h>
#include "internal/thread.h"


RTDECL(RTTHREAD) RTThreadSelf(void)
{
    return rtThreadGetByNative((RTNATIVETHREAD)current);
}


DECLHIDDEN(int) rtThreadNativeInit(void)
{
    return VINF_SUCCESS;
}


DECLHIDDEN(int) rtThreadNativeSetPriority(PRTTHREADINT pThread, RTTHREADTYPE enmType)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 11)
    /* See comment near MAX_RT_PRIO in linux/sched.h for details on
       sched_priority. */
    int                 iSchedClass = SCHED_NORMAL;
    struct sched_param  Param       = { .sched_priority = MAX_PRIO - 1 };
    switch (enmType)
    {
        case RTTHREADTYPE_INFREQUENT_POLLER:
            Param.sched_priority = MAX_RT_PRIO + 5;
            break;

        case RTTHREADTYPE_EMULATION:
            Param.sched_priority = MAX_RT_PRIO + 4;
            break;

        case RTTHREADTYPE_DEFAULT:
            Param.sched_priority = MAX_RT_PRIO + 3;
            break;

        case RTTHREADTYPE_MSG_PUMP:
            Param.sched_priority = MAX_RT_PRIO + 2;
            break;

        case RTTHREADTYPE_IO:
            iSchedClass = SCHED_FIFO;
            Param.sched_priority = MAX_RT_PRIO - 1;
            break;

        case RTTHREADTYPE_TIMER:
            iSchedClass = SCHED_FIFO;
            Param.sched_priority = 1; /* not 0 just in case */
            break;

        default:
            AssertMsgFailed(("enmType=%d\n", enmType));
            return VERR_INVALID_PARAMETER;
    }

    sched_setscheduler(current, iSchedClass, &Param);
#else
    RT_NOREF_PV(enmType);
#endif
    RT_NOREF_PV(pThread);

    return VINF_SUCCESS;
}


DECLHIDDEN(int) rtThreadNativeAdopt(PRTTHREADINT pThread)
{
    RT_NOREF_PV(pThread);
    return VERR_NOT_IMPLEMENTED;
}


DECLHIDDEN(void) rtThreadNativeWaitKludge(PRTTHREADINT pThread)
{
    /** @todo fix RTThreadWait/RTR0Term race on linux. */
    RTThreadSleep(1); NOREF(pThread);
}


DECLHIDDEN(void) rtThreadNativeDestroy(PRTTHREADINT pThread)
{
    NOREF(pThread);
}


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 4)
/**
 * Native kernel thread wrapper function.
 *
 * This will forward to rtThreadMain and do termination upon return.
 *
 * @param pvArg         Pointer to the argument package.
 */
static int rtThreadNativeMain(void *pvArg)
{
    PRTTHREADINT pThread = (PRTTHREADINT)pvArg;

    rtThreadMain(pThread, (RTNATIVETHREAD)current, &pThread->szName[0]);
    return 0;
}
#endif


DECLHIDDEN(int) rtThreadNativeCreate(PRTTHREADINT pThreadInt, PRTNATIVETHREAD pNativeThread)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 4)
    struct task_struct *NativeThread;
    IPRT_LINUX_SAVE_EFL_AC();

    RT_ASSERT_PREEMPTIBLE();

    NativeThread = kthread_run(rtThreadNativeMain, pThreadInt, "iprt-%s", pThreadInt->szName);

    if (!IS_ERR(NativeThread))
    {
        *pNativeThread = (RTNATIVETHREAD)NativeThread;
        IPRT_LINUX_RESTORE_EFL_AC();
        return VINF_SUCCESS;
    }
    IPRT_LINUX_RESTORE_EFL_AC();
    return VERR_GENERAL_FAILURE;
#else
    return VERR_NOT_IMPLEMENTED;
#endif
}

