/* $Id: VBoxGuestR0LibInit.cpp $ */
/** @file
 * VBoxGuestLibR0 - Library initialization.
 */

/*
 * Copyright (C) 2006-2019 Oracle Corporation
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "VBoxGuestR0LibInternal.h"

#include <iprt/string.h>
#include <iprt/assert.h>
#include <iprt/semaphore.h>
#include <VBox/err.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The global VBGL instance data.  */
VBGLDATA g_vbgldata;


/**
 * Used by vbglR0QueryDriverInfo and VbglInit to try get the host feature mask
 * and version information (g_vbgldata::hostVersion).
 *
 * This was first implemented by the host in 3.1 and we quietly ignore failures
 * for that reason.
 */
static void vbglR0QueryHostVersion(void)
{
    VMMDevReqHostVersion *pReq;
    int rc = VbglR0GRAlloc((VMMDevRequestHeader **) &pReq, sizeof (*pReq), VMMDevReq_GetHostVersion);
    if (RT_SUCCESS(rc))
    {
        rc = VbglR0GRPerform(&pReq->header);
        if (RT_SUCCESS(rc))
        {
            g_vbgldata.hostVersion = *pReq;
            Log(("vbglR0QueryHostVersion: %u.%u.%ur%u %#x\n",
                 pReq->major, pReq->minor, pReq->build, pReq->revision, pReq->features));
        }

        VbglR0GRFree(&pReq->header);
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
 */
static int vbglR0QueryDriverInfo(void)
{
# ifdef VBGLDATA_USE_FAST_MUTEX
    int rc = RTSemFastMutexRequest(g_vbgldata.hMtxIdcSetup);
# else
    int rc = RTSemMutexRequest(g_vbgldata.hMtxIdcSetup, RT_INDEFINITE_WAIT);
# endif
    if (RT_SUCCESS(rc))
    {
        if (g_vbgldata.status == VbglStatusReady)
        { /* likely */ }
        else
        {
            rc = VbglR0IdcOpen(&g_vbgldata.IdcHandle,
                               VBGL_IOC_VERSION /*uReqVersion*/,
                               VBGL_IOC_VERSION & UINT32_C(0xffff0000) /*uMinVersion*/,
                               NULL /*puSessionVersion*/, NULL /*puDriverVersion*/, NULL /*puDriverRevision*/);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Try query the port info.
                 */
                VBGLIOCGETVMMDEVIOINFO PortInfo;
                RT_ZERO(PortInfo);
                VBGLREQHDR_INIT(&PortInfo.Hdr, GET_VMMDEV_IO_INFO);
                rc = VbglR0IdcCall(&g_vbgldata.IdcHandle, VBGL_IOCTL_GET_VMMDEV_IO_INFO, &PortInfo.Hdr, sizeof(PortInfo));
                if (RT_SUCCESS(rc))
                {
                    dprintf(("Port I/O = 0x%04x, MMIO = %p\n", PortInfo.u.Out.IoPort, PortInfo.u.Out.pvVmmDevMapping));

                    g_vbgldata.portVMMDev    = PortInfo.u.Out.IoPort;
                    g_vbgldata.pVMMDevMemory = (VMMDevMemory *)PortInfo.u.Out.pvVmmDevMapping;
                    g_vbgldata.status        = VbglStatusReady;

                    vbglR0QueryHostVersion();
                }
            }

            dprintf(("vbglQueryDriverInfo rc = %Rrc\n", rc));
        }

# ifdef VBGLDATA_USE_FAST_MUTEX
        RTSemFastMutexRelease(g_vbgldata.hMtxIdcSetup);
# else
        RTSemMutexRelease(g_vbgldata.hMtxIdcSetup);
# endif
    }
    return rc;
}
#endif /* !VBGL_VBOXGUEST */

/**
 * Checks if VBGL has been initialized.
 *
 * The client library, this will lazily complete the initialization.
 *
 * @return VINF_SUCCESS or VERR_VBGL_NOT_INITIALIZED.
 */
int vbglR0Enter(void)
{
    if (g_vbgldata.status == VbglStatusReady)
        return VINF_SUCCESS;

#ifndef VBGL_VBOXGUEST
    if (g_vbgldata.status == VbglStatusInitializing)
    {
        vbglR0QueryDriverInfo();
        if (g_vbgldata.status == VbglStatusReady)
            return VINF_SUCCESS;
    }
#endif
    return VERR_VBGL_NOT_INITIALIZED;
}


static int vbglR0InitCommon(void)
{
    int rc;

    RT_ZERO(g_vbgldata);
    g_vbgldata.status = VbglStatusInitializing;

    rc = VbglR0PhysHeapInit();
    if (RT_SUCCESS(rc))
    {
        dprintf(("vbglR0InitCommon: returns rc = %d\n", rc));
        return rc;
    }

    LogRel(("vbglR0InitCommon: VbglR0PhysHeapInit failed: rc=%Rrc\n", rc));
    g_vbgldata.status = VbglStatusNotInitialized;
    return rc;
}


static void vbglR0TerminateCommon(void)
{
    VbglR0PhysHeapTerminate();
    g_vbgldata.status = VbglStatusNotInitialized;
}

#ifdef VBGL_VBOXGUEST

DECLR0VBGL(int) VbglR0InitPrimary(RTIOPORT portVMMDev, VMMDevMemory *pVMMDevMemory, uint32_t *pfFeatures)
{
    int rc;

# ifdef RT_OS_WINDOWS /** @todo r=bird: this doesn't make sense. Is there something special going on on windows? */
    dprintf(("vbglInit: starts g_vbgldata.status %d\n", g_vbgldata.status));

    if (   g_vbgldata.status == VbglStatusInitializing
        || g_vbgldata.status == VbglStatusReady)
    {
        /* Initialization is already in process. */
        return VINF_SUCCESS;
    }
# else
    dprintf(("vbglInit: starts\n"));
# endif

    rc = vbglR0InitCommon();
    if (RT_SUCCESS(rc))
    {
        g_vbgldata.portVMMDev    = portVMMDev;
        g_vbgldata.pVMMDevMemory = pVMMDevMemory;
        g_vbgldata.status        = VbglStatusReady;

        vbglR0QueryHostVersion();
        *pfFeatures = g_vbgldata.hostVersion.features;
        return VINF_SUCCESS;
    }

    g_vbgldata.status = VbglStatusNotInitialized;
    return rc;
}

DECLR0VBGL(void) VbglR0TerminatePrimary(void)
{
    vbglR0TerminateCommon();
}


#else /* !VBGL_VBOXGUEST */

DECLR0VBGL(int) VbglR0InitClient(void)
{
    int rc;

    /** @todo r=bird: explain why we need to be doing this, please... */
    if (   g_vbgldata.status == VbglStatusInitializing
        || g_vbgldata.status == VbglStatusReady)
    {
        /* Initialization is already in process. */
        return VINF_SUCCESS;
    }

    rc = vbglR0InitCommon();
    if (RT_SUCCESS(rc))
    {
# ifdef VBGLDATA_USE_FAST_MUTEX
        rc = RTSemFastMutexCreate(&g_vbgldata.hMtxIdcSetup);
# else
        rc = RTSemMutexCreate(&g_vbgldata.hMtxIdcSetup);
# endif
        if (RT_SUCCESS(rc))
        {
            /* Try to obtain VMMDev port via IOCTL to VBoxGuest main driver. */
            vbglR0QueryDriverInfo();

# ifdef VBOX_WITH_HGCM
            rc = VbglR0HGCMInit();
# endif
            if (RT_SUCCESS(rc))
                return VINF_SUCCESS;

# ifdef VBGLDATA_USE_FAST_MUTEX
            RTSemFastMutexDestroy(g_vbgldata.hMtxIdcSetup);
            g_vbgldata.hMtxIdcSetup = NIL_RTSEMFASTMUTEX;
# else
            RTSemMutexDestroy(g_vbgldata.hMtxIdcSetup);
            g_vbgldata.hMtxIdcSetup = NIL_RTSEMMUTEX;
# endif
        }
        vbglR0TerminateCommon();
    }

    return rc;
}

DECLR0VBGL(void) VbglR0TerminateClient(void)
{
# ifdef VBOX_WITH_HGCM
    VbglR0HGCMTerminate();
# endif

    /* driver open could fail, which does not prevent VbglInit from succeeding,
     * close the driver only if it is opened */
    VbglR0IdcClose(&g_vbgldata.IdcHandle);
# ifdef VBGLDATA_USE_FAST_MUTEX
    RTSemFastMutexDestroy(g_vbgldata.hMtxIdcSetup);
    g_vbgldata.hMtxIdcSetup = NIL_RTSEMFASTMUTEX;
# else
    RTSemMutexDestroy(g_vbgldata.hMtxIdcSetup);
    g_vbgldata.hMtxIdcSetup = NIL_RTSEMMUTEX;
# endif

    /* note: do vbglR0TerminateCommon as a last step since it zeroez up the g_vbgldata
     * conceptually, doing vbglR0TerminateCommon last is correct
     * since this is the reverse order to how init is done */
    vbglR0TerminateCommon();
}


int VBOXCALL vbglR0QueryIdcHandle(PVBGLIDCHANDLE *ppIdcHandle)
{
    if (g_vbgldata.status == VbglStatusReady)
    { /* likely */ }
    else
    {
        vbglR0QueryDriverInfo();
        if (g_vbgldata.status != VbglStatusReady)
        {
            *ppIdcHandle = NULL;
            return VERR_TRY_AGAIN;
        }
    }

    *ppIdcHandle = &g_vbgldata.IdcHandle;
    return VINF_SUCCESS;
}


DECLR0VBGL(int) VbglR0QueryHostFeatures(uint32_t *pfHostFeatures)
{
    if (g_vbgldata.status == VbglStatusReady)
        *pfHostFeatures = g_vbgldata.hostVersion.features;
    else
    {
        int rc = vbglR0QueryDriverInfo();
        if (g_vbgldata.status != VbglStatusReady)
            return rc;
        *pfHostFeatures = g_vbgldata.hostVersion.features;
    }

    return VINF_SUCCESS;
}

#endif /* !VBGL_VBOXGUEST */

