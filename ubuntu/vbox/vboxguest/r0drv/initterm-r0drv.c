/* $Id: initterm-r0drv.cpp $ */
/** @file
 * IPRT - Initialization & Termination, R0 Driver, Common.
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
#include <iprt/initterm.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/mp.h>
#include <iprt/thread.h>
#ifndef IN_GUEST /* play safe for now */
# include "r0drv/mp-r0drv.h"
# include "r0drv/power-r0drv.h"
#endif

#include "internal/initterm.h"
#include "internal/mem.h"
#include "internal/thread.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Count of current IPRT users.
 * In ring-0 several drivers / kmods / kexts / wossnames may share the
 * same runtime code. So, we need to keep count in order not to terminate
 * it prematurely. */
static int32_t volatile g_crtR0Users = 0;


/**
 * Initializes the ring-0 driver runtime library.
 *
 * @returns iprt status code.
 * @param   fReserved       Flags reserved for the future.
 */
RTR0DECL(int) RTR0Init(unsigned fReserved)
{
    int rc;
    uint32_t cNewUsers;
    Assert(fReserved == 0); RT_NOREF_PV(fReserved);
#ifndef RT_OS_SOLARIS       /* On Solaris our thread preemption information is only obtained in rtR0InitNative().*/
    RT_ASSERT_PREEMPTIBLE();
#endif

    /*
     * The first user initializes it.
     * We rely on the module loader to ensure that there are no
     * initialization races should two modules share the IPRT.
     */
    cNewUsers = ASMAtomicIncS32(&g_crtR0Users);
    if (cNewUsers != 1)
    {
        if (cNewUsers > 1)
            return VINF_SUCCESS;
        ASMAtomicDecS32(&g_crtR0Users);
        return VERR_INTERNAL_ERROR_3;
    }

    rc = rtR0InitNative();
    if (RT_SUCCESS(rc))
    {
#ifdef RTR0MEM_WITH_EF_APIS
        rtR0MemEfInit();
#endif
        rc = rtThreadInit();
        if (RT_SUCCESS(rc))
        {
#ifndef IN_GUEST /* play safe for now */
            rc = rtR0MpNotificationInit();
            if (RT_SUCCESS(rc))
            {
                rc = rtR0PowerNotificationInit();
                if (RT_SUCCESS(rc))
                    return rc;
                rtR0MpNotificationTerm();
            }
#else
            if (RT_SUCCESS(rc))
                return rc;
#endif
            rtThreadTerm();
        }
#ifdef RTR0MEM_WITH_EF_APIS
        rtR0MemEfTerm();
#endif
        rtR0TermNative();
    }
    return rc;
}
RT_EXPORT_SYMBOL(RTR0Init);


static void rtR0Term(void)
{
    rtThreadTerm();
#ifndef IN_GUEST /* play safe for now */
    rtR0PowerNotificationTerm();
    rtR0MpNotificationTerm();
#endif
#ifdef RTR0MEM_WITH_EF_APIS
    rtR0MemEfTerm();
#endif
    rtR0TermNative();
}


/**
 * Terminates the ring-0 driver runtime library.
 */
RTR0DECL(void) RTR0Term(void)
{
    int32_t cNewUsers;
    RT_ASSERT_PREEMPTIBLE();

    cNewUsers = ASMAtomicDecS32(&g_crtR0Users);
    Assert(cNewUsers >= 0);
    if (cNewUsers == 0)
        rtR0Term();
    else if (cNewUsers < 0)
        ASMAtomicIncS32(&g_crtR0Users);
}
RT_EXPORT_SYMBOL(RTR0Term);


/* Note! Should *not* be exported since it's only for static linking. */
RTR0DECL(void) RTR0TermForced(void)
{
    RT_ASSERT_PREEMPTIBLE();

    AssertMsg(g_crtR0Users == 1, ("%d\n", g_crtR0Users));
    ASMAtomicWriteS32(&g_crtR0Users, 0);

    rtR0Term();
}

