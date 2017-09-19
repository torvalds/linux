/* $Id: HGCM.cpp $ */
/** @file
 * VBoxGuestLib - Host-Guest Communication Manager.
 *
 * These public functions can be only used by other drivers. They all
 * do an IOCTL to VBoxGuest via IDC.
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

/* Entire file is ifdef'ed with !VBGL_VBOXGUEST */
#ifndef VBGL_VBOXGUEST

#include "VBGLInternal.h"

#include <iprt/assert.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>

#define VBGL_HGCM_ASSERT_MSG AssertReleaseMsg

/**
 * Initializes the HGCM VBGL bits.
 *
 * @return VBox status code.
 */
int vbglR0HGCMInit(void)
{
    return RTSemFastMutexCreate(&g_vbgldata.mutexHGCMHandle);
}

/**
 * Initializes the HGCM VBGL bits.
 *
 * @return VBox status code.
 */
int vbglR0HGCMTerminate(void)
{
    RTSemFastMutexDestroy(g_vbgldata.mutexHGCMHandle);
    g_vbgldata.mutexHGCMHandle = NIL_RTSEMFASTMUTEX;

    return VINF_SUCCESS;
}

DECLINLINE(int) vbglHandleHeapEnter(void)
{
    int rc = RTSemFastMutexRequest(g_vbgldata.mutexHGCMHandle);

    VBGL_HGCM_ASSERT_MSG(RT_SUCCESS(rc), ("Failed to request handle heap mutex, rc = %Rrc\n", rc));

    return rc;
}

DECLINLINE(void) vbglHandleHeapLeave(void)
{
    RTSemFastMutexRelease(g_vbgldata.mutexHGCMHandle);
}

struct VBGLHGCMHANDLEDATA *vbglHGCMHandleAlloc(void)
{
    struct VBGLHGCMHANDLEDATA *p = NULL;
    int rc = vbglHandleHeapEnter();
    if (RT_SUCCESS(rc))
    {
        uint32_t i;

        /* Simple linear search in array. This will be called not so often, only connect/disconnect. */
        /** @todo bitmap for faster search and other obvious optimizations. */
        for (i = 0; i < RT_ELEMENTS(g_vbgldata.aHGCMHandleData); i++)
        {
            if (!g_vbgldata.aHGCMHandleData[i].fAllocated)
            {
                p = &g_vbgldata.aHGCMHandleData[i];
                p->fAllocated = 1;
                break;
            }
        }

        vbglHandleHeapLeave();

        VBGL_HGCM_ASSERT_MSG(p != NULL, ("Not enough HGCM handles.\n"));
    }
    return p;
}

void vbglHGCMHandleFree(struct VBGLHGCMHANDLEDATA *pHandle)
{
    if (pHandle)
    {
        int rc = vbglHandleHeapEnter();
        if (RT_SUCCESS(rc))
        {
            VBGL_HGCM_ASSERT_MSG(pHandle->fAllocated, ("Freeing not allocated handle.\n"));

            RT_ZERO(*pHandle);
            vbglHandleHeapLeave();
        }
    }
}

DECLVBGL(int) VbglHGCMConnect(VBGLHGCMHANDLE *pHandle, VBoxGuestHGCMConnectInfo *pData)
{
    int rc;
    if (pHandle && pData)
    {
        struct VBGLHGCMHANDLEDATA *pHandleData = vbglHGCMHandleAlloc();
        if (pHandleData)
        {
            rc = vbglDriverOpen(&pHandleData->driver);
            if (RT_SUCCESS(rc))
            {
                rc = vbglDriverIOCtl(&pHandleData->driver, VBOXGUEST_IOCTL_HGCM_CONNECT, pData, sizeof(*pData));
                if (RT_SUCCESS(rc))
                    rc = pData->result;
                if (RT_SUCCESS(rc))
                {
                    *pHandle = pHandleData;
                    return rc;
                }

                vbglDriverClose(&pHandleData->driver);
            }

            vbglHGCMHandleFree(pHandleData);
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
        rc = VERR_INVALID_PARAMETER;
    return rc;
}

DECLVBGL(int) VbglHGCMDisconnect(VBGLHGCMHANDLE handle, VBoxGuestHGCMDisconnectInfo *pData)
{
    int rc = vbglDriverIOCtl(&handle->driver, VBOXGUEST_IOCTL_HGCM_DISCONNECT, pData, sizeof(*pData));

    vbglDriverClose(&handle->driver);

    vbglHGCMHandleFree(handle);

    return rc;
}

DECLVBGL(int) VbglHGCMCall(VBGLHGCMHANDLE handle, VBoxGuestHGCMCallInfo *pData, uint32_t cbData)
{
    VBGL_HGCM_ASSERT_MSG(cbData >= sizeof(VBoxGuestHGCMCallInfo) + pData->cParms * sizeof(HGCMFunctionParameter),
                         ("cbData = %d, cParms = %d (calculated size %d)\n", cbData, pData->cParms,
                          sizeof(VBoxGuestHGCMCallInfo) + pData->cParms * sizeof(VBoxGuestHGCMCallInfo)));

    return vbglDriverIOCtl(&handle->driver, VBOXGUEST_IOCTL_HGCM_CALL(cbData), pData, cbData);
}

DECLVBGL(int) VbglHGCMCallUserData (VBGLHGCMHANDLE handle, VBoxGuestHGCMCallInfo *pData, uint32_t cbData)
{
    VBGL_HGCM_ASSERT_MSG(cbData >= sizeof(VBoxGuestHGCMCallInfo) + pData->cParms * sizeof(HGCMFunctionParameter),
                         ("cbData = %d, cParms = %d (calculated size %d)\n", cbData, pData->cParms,
                          sizeof(VBoxGuestHGCMCallInfo) + pData->cParms * sizeof(VBoxGuestHGCMCallInfo)));

    return vbglDriverIOCtl(&handle->driver, VBOXGUEST_IOCTL_HGCM_CALL_USERDATA(cbData), pData, cbData);
}


DECLVBGL(int) VbglHGCMCallTimed(VBGLHGCMHANDLE handle, VBoxGuestHGCMCallInfoTimed *pData, uint32_t cbData)
{
    uint32_t cbExpected = sizeof(VBoxGuestHGCMCallInfoTimed)
                        + pData->info.cParms * sizeof(HGCMFunctionParameter);
    VBGL_HGCM_ASSERT_MSG(cbData >= cbExpected,
                         ("cbData = %d, cParms = %d (calculated size %d)\n", cbData, pData->info.cParms, cbExpected));
    NOREF(cbExpected);

    return vbglDriverIOCtl(&handle->driver, VBOXGUEST_IOCTL_HGCM_CALL_TIMED(cbData), pData, cbData);
}

#endif /* !VBGL_VBOXGUEST */

