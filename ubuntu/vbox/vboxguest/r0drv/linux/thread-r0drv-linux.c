/* $Id: thread-r0drv-linux.c $ */
/** @file
 * IPRT - Threads, Ring-0 Driver, Linux.
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
#include <iprt/thread.h>

#include <iprt/asm.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 5, 28) || defined(CONFIG_X86_SMAP)
# include <iprt/asm-amd64-x86.h>
#endif
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/mp.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifndef CONFIG_PREEMPT
/** Per-cpu preemption counters. */
static int32_t volatile g_acPreemptDisabled[NR_CPUS];
#endif


RTDECL(RTNATIVETHREAD) RTThreadNativeSelf(void)
{
    return (RTNATIVETHREAD)current;
}
RT_EXPORT_SYMBOL(RTThreadNativeSelf);


static int rtR0ThreadLnxSleepCommon(RTMSINTERVAL cMillies)
{
    IPRT_LINUX_SAVE_EFL_AC();
    long cJiffies = msecs_to_jiffies(cMillies);
    set_current_state(TASK_INTERRUPTIBLE);
    cJiffies = schedule_timeout(cJiffies);
    IPRT_LINUX_RESTORE_EFL_AC();
    if (!cJiffies)
        return VINF_SUCCESS;
    return VERR_INTERRUPTED;
}


RTDECL(int) RTThreadSleep(RTMSINTERVAL cMillies)
{
    return rtR0ThreadLnxSleepCommon(cMillies);
}
RT_EXPORT_SYMBOL(RTThreadSleep);


RTDECL(int) RTThreadSleepNoLog(RTMSINTERVAL cMillies)
{
    return rtR0ThreadLnxSleepCommon(cMillies);
}
RT_EXPORT_SYMBOL(RTThreadSleepNoLog);


RTDECL(bool) RTThreadYield(void)
{
    IPRT_LINUX_SAVE_EFL_AC();
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 20)
    yield();
#else
    /** @todo r=ramshankar: Can we use cond_resched() instead?  */
    set_current_state(TASK_RUNNING);
    sys_sched_yield();
    schedule();
#endif
    IPRT_LINUX_RESTORE_EFL_AC();
    return true;
}
RT_EXPORT_SYMBOL(RTThreadYield);


RTDECL(bool) RTThreadPreemptIsEnabled(RTTHREAD hThread)
{
#ifdef CONFIG_PREEMPT
    Assert(hThread == NIL_RTTHREAD); RT_NOREF_PV(hThread);
# ifdef preemptible
    return preemptible();
# else
    return preempt_count() == 0 && !in_atomic() && !irqs_disabled();
# endif
#else
    int32_t c;

    Assert(hThread == NIL_RTTHREAD);
    c = g_acPreemptDisabled[smp_processor_id()];
    AssertMsg(c >= 0 && c < 32, ("%d\n", c));
    if (c != 0)
        return false;
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 32)
    if (in_atomic())
        return false;
# endif
# if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 28)
    if (irqs_disabled())
        return false;
# else
    if (!ASMIntAreEnabled())
        return false;
# endif
    return true;
#endif
}
RT_EXPORT_SYMBOL(RTThreadPreemptIsEnabled);


RTDECL(bool) RTThreadPreemptIsPending(RTTHREAD hThread)
{
    Assert(hThread == NIL_RTTHREAD); RT_NOREF_PV(hThread);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 4)
    return !!test_tsk_thread_flag(current, TIF_NEED_RESCHED);

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 20)
    return !!need_resched();

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 1, 110)
    return current->need_resched != 0;

#else
    return need_resched != 0;
#endif
}
RT_EXPORT_SYMBOL(RTThreadPreemptIsPending);


RTDECL(bool) RTThreadPreemptIsPendingTrusty(void)
{
    /* yes, RTThreadPreemptIsPending is reliable. */
    return true;
}
RT_EXPORT_SYMBOL(RTThreadPreemptIsPendingTrusty);


RTDECL(bool) RTThreadPreemptIsPossible(void)
{
    /** @todo r=ramshankar: What about CONFIG_PREEMPT_VOLUNTARY? That can preempt
     *        too but does so in voluntarily in explicit preemption points. */
#ifdef CONFIG_PREEMPT
    return true;    /* yes, kernel preemption is possible. */
#else
    return false;   /* no kernel preemption */
#endif
}
RT_EXPORT_SYMBOL(RTThreadPreemptIsPossible);


RTDECL(void) RTThreadPreemptDisable(PRTTHREADPREEMPTSTATE pState)
{
#ifdef CONFIG_PREEMPT
    AssertPtr(pState);
    Assert(pState->u32Reserved == 0);
    pState->u32Reserved = 42;
    /* This ASSUMES that CONFIG_PREEMPT_COUNT is always defined with CONFIG_PREEMPT. */
    preempt_disable();
    RT_ASSERT_PREEMPT_CPUID_DISABLE(pState);

#else /* !CONFIG_PREEMPT */
    int32_t c;
    AssertPtr(pState);
    Assert(pState->u32Reserved == 0);

    /* Do our own accounting. */
    c = ASMAtomicIncS32(&g_acPreemptDisabled[smp_processor_id()]);
    AssertMsg(c > 0 && c < 32, ("%d\n", c));
    pState->u32Reserved = c;
    RT_ASSERT_PREEMPT_CPUID_DISABLE(pState);
#endif
}
RT_EXPORT_SYMBOL(RTThreadPreemptDisable);


RTDECL(void) RTThreadPreemptRestore(PRTTHREADPREEMPTSTATE pState)
{
#ifdef CONFIG_PREEMPT
    IPRT_LINUX_SAVE_EFL_AC(); /* paranoia */
    AssertPtr(pState);
    Assert(pState->u32Reserved == 42);
    RT_ASSERT_PREEMPT_CPUID_RESTORE(pState);
    preempt_enable();
    IPRT_LINUX_RESTORE_EFL_ONLY_AC();  /* paranoia */

#else
    int32_t volatile *pc;
    AssertPtr(pState);
    AssertMsg(pState->u32Reserved > 0 && pState->u32Reserved < 32, ("%d\n", pState->u32Reserved));
    RT_ASSERT_PREEMPT_CPUID_RESTORE(pState);

    /* Do our own accounting. */
    pc = &g_acPreemptDisabled[smp_processor_id()];
    AssertMsg(pState->u32Reserved == (uint32_t)*pc, ("u32Reserved=%d *pc=%d \n", pState->u32Reserved, *pc));
    ASMAtomicUoWriteS32(pc, pState->u32Reserved - 1);
#endif
    pState->u32Reserved = 0;
}
RT_EXPORT_SYMBOL(RTThreadPreemptRestore);


RTDECL(bool) RTThreadIsInInterrupt(RTTHREAD hThread)
{
    Assert(hThread == NIL_RTTHREAD); NOREF(hThread);

    return in_interrupt() != 0;
}
RT_EXPORT_SYMBOL(RTThreadIsInInterrupt);

