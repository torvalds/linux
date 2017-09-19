/* $Id: Init.cpp $ */
/** @file
 * VBoxGuestLibR0 - Library initialization.
 */

/*
 * Copyright (C) 2006-2016 Oracle Corporation
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
#define VBGL_DECL_DATA
#include "VBGLInternal.h"

#include <iprt/string.h>
#include <iprt/assert.h>
#include <iprt/semaphore.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The global VBGL instance data.  */
VBGLDATA g_vbgldata;

/**
 * Used by vbglQueryDriverInfo and VbglInit to try get the host feature mask and
 * version information (g_vbgldata::hostVersion).
 *
 * This was first implemented by the host in 3.1 and we quietly ignore failures
 * for that reason.
 */
static void vbglR0QueryHostVersion (void)
{
    VMMDevReqHostVersion *pReq;

    int rc = VbglGRAlloc ((VMMDevRequestHeader **) &pReq, sizeof (*pReq), VMMDevReq_GetHostVersion);

    if (RT_SUCCESS (rc))
    {
        rc = VbglGRPerform (&pReq->header);

        if (RT_SUCCESS (rc))
        {
            g_vbgldata.hostVersion = *pReq;
            Log (("vbglR0QueryHostVersion: %u.%u.%ur%u %#x\n",
                  pReq->major, pReq->minor, pReq->build, pReq->revision, pReq->features));
        }

        VbglGRFree (&pReq->header);
    }
}

#ifndef VBGL_VBOXGUEST
/**
 * The guest library uses lazy initialization for VMMDev port and memory,
 * because these values are provided by the VBoxGuest driver and it might
 * be loaded later than other drivers.
 *
 * The VbglEnter checks the current library status, tries to retrieve these
 * values and fails if they are unavailable.
 *
 */
static void vbglQueryDriverInfo (void)
{
    int rc = VINF_SUCCESS;

    rc = RTSemMutexRequest(g_vbgldata.mutexDriverInit, RT_INDEFINITE_WAIT);

    if (RT_FAILURE(rc))
        return;

    if (g_vbgldata.status == VbglStatusReady)
    {
        RTSemMutexRelease(g_vbgldata.mutexDriverInit);
        return;
    }

    rc = vbglDriverOpen(&g_vbgldata.driver);

    if (RT_SUCCESS(rc))
    {
        /*
         * Try query the port info.
         */
        VBoxGuestPortInfo port;

        rc = vbglDriverIOCtl (&g_vbgldata.driver,
                              VBOXGUEST_IOCTL_GETVMMDEVPORT, &port,
                              sizeof (port));

        if (RT_SUCCESS (rc))
        {
            dprintf (("port = 0x%04X, mem = %p\n", port.portAddress, port.pVMMDevMemory));

            g_vbgldata.portVMMDev = (RTIOPORT)port.portAddress;
            g_vbgldata.pVMMDevMemory = port.pVMMDevMemory;

            g_vbgldata.status = VbglStatusReady;

            vbglR0QueryHostVersion();
        }
    }
    RTSemMutexRelease(g_vbgldata.mutexDriverInit);
    dprintf (("vbglQueryDriverInfo rc = %d\n", rc));
}
#endif /* !VBGL_VBOXGUEST */

/**
 * Checks if VBGL has been initialized.
 *
 * The client library, this will lazily complete the initialization.
 *
 * @return VINF_SUCCESS or VERR_VBGL_NOT_INITIALIZED.
 */
int vbglR0Enter (void)
{
    int rc;

#ifndef VBGL_VBOXGUEST
    if (g_vbgldata.status == VbglStatusInitializing)
    {
        vbglQueryDriverInfo ();
    }
#endif

    rc = g_vbgldata.status == VbglStatusReady? VINF_SUCCESS: VERR_VBGL_NOT_INITIALIZED;

    // dprintf(("VbglEnter: rc = %d\n", rc));

    return rc;
}

int vbglInitCommon (void)
{
    int rc = VINF_SUCCESS;

    RT_ZERO(g_vbgldata);

    g_vbgldata.status = VbglStatusInitializing;

    rc = VbglPhysHeapInit ();

    if (RT_SUCCESS(rc))
    {
        /* other subsystems, none yet */
        ;
    }
    else
    {
        LogRel(("vbglInitCommon: VbglPhysHeapInit failed. rc=%Rrc\n", rc));
        g_vbgldata.status = VbglStatusNotInitialized;
    }

    dprintf(("vbglInitCommon: rc = %d\n", rc));

    return rc;
}

DECLVBGL(void) vbglTerminateCommon (void)
{
    VbglPhysHeapTerminate ();
    g_vbgldata.status = VbglStatusNotInitialized;

    return;
}

#ifdef VBGL_VBOXGUEST

DECLVBGL(int) VbglInitPrimary(RTIOPORT portVMMDev, VMMDevMemory *pVMMDevMemory)
{
    int rc = VINF_SUCCESS;

# ifdef RT_OS_WINDOWS /** @todo r=bird: this doesn't make sense. Is there something special going on on windows? */
    dprintf(("vbglInit: starts g_vbgldata.status %d\n", g_vbgldata.status));

    if (   g_vbgldata.status == VbglStatusInitializing
        || g_vbgldata.status == VbglStatusReady)
    {
        /* Initialization is already in process. */
        return rc;
    }
# else
    dprintf(("vbglInit: starts\n"));
# endif

    rc = vbglInitCommon ();

    if (RT_SUCCESS(rc))
    {
        g_vbgldata.portVMMDev = portVMMDev;
        g_vbgldata.pVMMDevMemory = pVMMDevMemory;

        g_vbgldata.status = VbglStatusReady;

        vbglR0QueryHostVersion();
    }
    else
    {
        g_vbgldata.status = VbglStatusNotInitialized;
    }

    return rc;
}

DECLVBGL(void) VbglTerminate (void)
{
    vbglTerminateCommon ();

    return;
}


#else /* !VBGL_VBOXGUEST */

DECLVBGL(int) VbglInitClient(void)
{
    int rc = VINF_SUCCESS;

    if (   g_vbgldata.status == VbglStatusInitializing
        || g_vbgldata.status == VbglStatusReady)
    {
        /* Initialization is already in process. */
        return rc;
    }

    rc = vbglInitCommon ();

    if (RT_SUCCESS(rc))
    {
        rc = RTSemMutexCreate(&g_vbgldata.mutexDriverInit);
        if (RT_SUCCESS(rc))
        {
            /* Try to obtain VMMDev port via IOCTL to VBoxGuest main driver. */
            vbglQueryDriverInfo ();

# ifdef VBOX_WITH_HGCM
            rc = vbglR0HGCMInit ();
# endif /* VBOX_WITH_HGCM */

            if (RT_FAILURE(rc))
            {
                RTSemMutexDestroy(g_vbgldata.mutexDriverInit);
                g_vbgldata.mutexDriverInit = NIL_RTSEMMUTEX;
            }
        }

        if (RT_FAILURE(rc))
        {
            vbglTerminateCommon ();
        }

    }

    return rc;
}

DECLVBGL(void) VbglTerminate (void)
{
# ifdef VBOX_WITH_HGCM
    vbglR0HGCMTerminate ();
# endif

    /* driver open could fail, which does not prevent VbglInit from succeeding,
     * close the driver only if it is opened */
    if (vbglDriverIsOpened(&g_vbgldata.driver))
        vbglDriverClose(&g_vbgldata.driver);
    RTSemMutexDestroy(g_vbgldata.mutexDriverInit);
    g_vbgldata.mutexDriverInit = NIL_RTSEMMUTEX;

    /* note: do vbglTerminateCommon as a last step since it zeroez up the g_vbgldata
     * conceptually, doing vbglTerminateCommon last is correct
     * since this is the reverse order to how init is done */
    vbglTerminateCommon ();

    return;
}

int vbglGetDriver(VBGLDRIVER **ppDriver)
{
    if (g_vbgldata.status != VbglStatusReady)
    {
        vbglQueryDriverInfo();
        if (g_vbgldata.status != VbglStatusReady)
            return VERR_TRY_AGAIN;
    }
    *ppDriver = &g_vbgldata.driver;
    return VINF_SUCCESS;
}

#endif /* !VBGL_VBOXGUEST */
