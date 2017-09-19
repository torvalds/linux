/* $Id: time-r0drv-linux.c $ */
/** @file
 * IPRT - Time, Ring-0 Driver, Linux.
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
#define LOG_GROUP RTLOGGROUP_TIME
#include "the-linux-kernel.h"
#include "internal/iprt.h"
#include <iprt/time.h>
#include <iprt/asm.h>



DECLINLINE(uint64_t) rtTimeGetSystemNanoTS(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 16) /* This must match timer-r0drv-linux.c! */
    /*
     * Use ktime_get_ts, this is also what clock_gettime(CLOCK_MONOTONIC,) is using.
     */
    uint64_t u64;
    struct timespec Ts;
    ktime_get_ts(&Ts);
    u64 = Ts.tv_sec * RT_NS_1SEC_64 + Ts.tv_nsec;
    return u64;

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 60)
    /*
     * Seems there is no way of getting to the exact source of
     * sys_clock_gettime(CLOCK_MONOTONIC, &ts) here, I think. But
     * 64-bit jiffies adjusted for the initial value should be pretty
     * much the same I hope.
     */
    uint64_t u64 = get_jiffies_64();
# ifdef INITIAL_JIFFIES
    u64 += INITIAL_JIFFIES;
# endif
    u64 *= TICK_NSEC;
    return u64;

#else   /* < 2.5.60 */
# if BITS_PER_LONG >= 64
    /*
     * This is the same as above, except that there is no get_jiffies_64()
     * here and we rely on long, and therefor jiffies, being 64-bit instead.
     */
    uint64_t u64 = jiffies;
# ifdef INITIAL_JIFFIES
    u64 += INITIAL_JIFFIES;
# endif
    u64 *= TICK_NSEC;
    return u64;

# else /* 32 bit jiffies */
    /*
     * We'll have to try track jiffy rollovers here or we'll be
     * in trouble every time it flips.
     *
     * The high dword of the s_u64Last is the rollover count, the
     * low dword is the previous jiffies.  Updating is done by
     * atomic compare & exchange of course.
     */
    static uint64_t volatile s_u64Last = 0;
    uint64_t u64;

    for (;;)
    {
        uint64_t u64NewLast;
        int32_t iDelta;
        uint32_t cRollovers;
        uint32_t u32LastJiffies;

        /* sample the values */
        unsigned long ulNow = jiffies;
        uint64_t u64Last = s_u64Last;
        if (ulNow != jiffies)
            continue; /* try again */
#  ifdef INITIAL_JIFFIES
        ulNow += INITIAL_JIFFIES;
#  endif

        u32LastJiffies = (uint32_t)u64Last;
        cRollovers = u64Last >> 32;

        /*
         * Check for rollover and update the static last value.
         *
         * We have to make sure we update it successfully to rule out
         * an underrun because of racing someone.
         */
        iDelta = ulNow - u32LastJiffies;
        if (iDelta < 0)
        {
            cRollovers++;
            u64NewLast = RT_MAKE_U64(ulNow, cRollovers);
            if (!ASMAtomicCmpXchgU64(&s_u64Last, u64NewLast, u64Last))
                continue; /* race, try again */
        }
        else
        {
            u64NewLast = RT_MAKE_U64(ulNow, cRollovers);
            ASMAtomicCmpXchgU64(&s_u64Last, u64NewLast, u64Last);
        }

        /* calculate the return value */
        u64 = ulNow;
        u64 *= TICK_NSEC;
        u64 += cRollovers * (_4G * TICK_NSEC);
        break;
    }

    return u64;
# endif /* 32 bit jiffies */
#endif  /* < 2.5.60 */
}


RTDECL(uint64_t) RTTimeNanoTS(void)
{
    return rtTimeGetSystemNanoTS();
}
RT_EXPORT_SYMBOL(RTTimeNanoTS);


RTDECL(uint64_t) RTTimeMilliTS(void)
{
    return rtTimeGetSystemNanoTS() / RT_NS_1MS;
}
RT_EXPORT_SYMBOL(RTTimeMilliTS);


RTDECL(uint64_t) RTTimeSystemNanoTS(void)
{
    return rtTimeGetSystemNanoTS();
}
RT_EXPORT_SYMBOL(RTTimeSystemNanoTS);


RTDECL(uint64_t) RTTimeSystemMilliTS(void)
{
    return rtTimeGetSystemNanoTS() / RT_NS_1MS;
}
RT_EXPORT_SYMBOL(RTTimeSystemMilliTS);


RTDECL(PRTTIMESPEC) RTTimeNow(PRTTIMESPEC pTime)
{
    IPRT_LINUX_SAVE_EFL_AC();
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 16)
    struct timespec Ts;
    ktime_get_real_ts(&Ts);
    IPRT_LINUX_RESTORE_EFL_AC();
    return RTTimeSpecSetTimespec(pTime, &Ts);

#else   /* < 2.6.16 */
    struct timeval Tv;
    do_gettimeofday(&Tv);
    IPRT_LINUX_RESTORE_EFL_AC();
    return RTTimeSpecSetTimeval(pTime, &Tv);
#endif
}
RT_EXPORT_SYMBOL(RTTimeNow);

