/* $Id: VBoxGuestR0LibHGCM.cpp $ */
/** @file
 * VBoxGuestLib - Host-Guest Communication Manager, ring-0 client drivers.
 *
 * These public functions can be only used by other drivers. They all
 * do an IOCTL to VBoxGuest via IDC.
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

#include <iprt/assert.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <VBox/err.h>

#ifdef VBGL_VBOXGUEST
# error "This file shouldn't be part of the VBoxGuestR0LibBase library that is linked into VBoxGuest.  It's client code."
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define VBGL_HGCM_ASSERT_MSG AssertReleaseMsg


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * Fast heap for HGCM handles data.
 * @{
 */
static RTSEMFASTMUTEX            g_hMtxHGCMHandleData;
static struct VBGLHGCMHANDLEDATA g_aHGCMHandleData[64];
/** @} */


/**
 * Initializes the HGCM VBGL bits.
 *
 * @return VBox status code.
 */
DECLR0VBGL(int) VbglR0HGCMInit(void)
{
    AssertReturn(g_hMtxHGCMHandleData == NIL_RTSEMFASTMUTEX, VINF_ALREADY_INITIALIZED);
    return RTSemFastMutexCreate(&g_hMtxHGCMHandleData);
}

/**
 * Initializes the HGCM VBGL bits.
 *
 * @return VBox status code.
 */
DECLR0VBGL(int) VbglR0HGCMTerminate(void)
{
    RTSemFastMutexDestroy(g_hMtxHGCMHandleData);
    g_hMtxHGCMHandleData = NIL_RTSEMFASTMUTEX;

    return VINF_SUCCESS;
}

DECLINLINE(int) vbglR0HandleHeapEnter(void)
{
    int rc = RTSemFastMutexRequest(g_hMtxHGCMHandleData);

    VBGL_HGCM_ASSERT_MSG(RT_SUCCESS(rc), ("Failed to request handle heap mutex, rc = %Rrc\n", rc));

    return rc;
}

DECLINLINE(void) vbglR0HandleHeapLeave(void)
{
    RTSemFastMutexRelease(g_hMtxHGCMHandleData);
}

struct VBGLHGCMHANDLEDATA *vbglR0HGCMHandleAlloc(void)
{
    struct VBGLHGCMHANDLEDATA *p = NULL;
    int rc = vbglR0HandleHeapEnter();
    if (RT_SUCCESS(rc))
    {
        uint32_t i;

        /* Simple linear search in array. This will be called not so often, only connect/disconnect. */
        /** @todo bitmap for faster search and other obvious optimizations. */
        for (i = 0; i < RT_ELEMENTS(g_aHGCMHandleData); i++)
        {
            if (!g_aHGCMHandleData[i].fAllocated)
            {
                p = &g_aHGCMHandleData[i];
                p->fAllocated = 1;
                break;
            }
        }

        vbglR0HandleHeapLeave();

        VBGL_HGCM_ASSERT_MSG(p != NULL, ("Not enough HGCM handles.\n"));
    }
    return p;
}

void vbglR0HGCMHandleFree(struct VBGLHGCMHANDLEDATA *pHandle)
{
    if (pHandle)
    {
        int rc = vbglR0HandleHeapEnter();
        if (RT_SUCCESS(rc))
        {
            VBGL_HGCM_ASSERT_MSG(pHandle->fAllocated, ("Freeing not allocated handle.\n"));

            RT_ZERO(*pHandle);
            vbglR0HandleHeapLeave();
        }
    }
}

DECLR0VBGL(int) VbglR0HGCMConnect(VBGLHGCMHANDLE *pHandle, const char *pszServiceName, HGCMCLIENTID *pidClient)
{
    int rc;
    if (pHandle && pszServiceName && pidClient)
    {
        struct VBGLHGCMHANDLEDATA *pHandleData = vbglR0HGCMHandleAlloc();
        if (pHandleData)
        {
            rc = VbglR0IdcOpen(&pHandleData->IdcHandle,
                               VBGL_IOC_VERSION /*uReqVersion*/,
                               VBGL_IOC_VERSION & UINT32_C(0xffff0000) /*uMinVersion*/,
                               NULL /*puSessionVersion*/, NULL /*puDriverVersion*/, NULL /*uDriverRevision*/);
            if (RT_SUCCESS(rc))
            {
                VBGLIOCHGCMCONNECT Info;
                RT_ZERO(Info);
                VBGLREQHDR_INIT(&Info.Hdr, HGCM_CONNECT);
                Info.u.In.Loc.type     = VMMDevHGCMLoc_LocalHost_Existing;
                rc = RTStrCopy(Info.u.In.Loc.u.host.achName, sizeof(Info.u.In.Loc.u.host.achName), pszServiceName);
                if (RT_SUCCESS(rc))
                {
                    rc = VbglR0IdcCall(&pHandleData->IdcHandle, VBGL_IOCTL_HGCM_CONNECT, &Info.Hdr, sizeof(Info));
                    if (RT_SUCCESS(rc))
                    {
                        *pidClient = Info.u.Out.idClient;
                        *pHandle   = pHandleData;
                        return rc;
                    }
                }

                VbglR0IdcClose(&pHandleData->IdcHandle);
            }

            vbglR0HGCMHandleFree(pHandleData);
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
        rc = VERR_INVALID_PARAMETER;
    return rc;
}

DECLR0VBGL(int) VbglR0HGCMDisconnect(VBGLHGCMHANDLE handle, HGCMCLIENTID idClient)
{
    int rc;
    VBGLIOCHGCMDISCONNECT Info;

    RT_ZERO(Info);
    VBGLREQHDR_INIT(&Info.Hdr, HGCM_DISCONNECT);
    Info.u.In.idClient = idClient;
    rc = VbglR0IdcCall(&handle->IdcHandle, VBGL_IOCTL_HGCM_DISCONNECT, &Info.Hdr, sizeof(Info));

    VbglR0IdcClose(&handle->IdcHandle);

    vbglR0HGCMHandleFree(handle);

    return rc;
}

DECLR0VBGL(int) VbglR0HGCMCallRaw(VBGLHGCMHANDLE handle, PVBGLIOCHGCMCALL pData, uint32_t cbData)
{
    VBGL_HGCM_ASSERT_MSG(cbData >= sizeof(VBGLIOCHGCMCALL) + pData->cParms * sizeof(HGCMFunctionParameter),
                         ("cbData = %d, cParms = %d (calculated size %d)\n", cbData, pData->cParms,
                          sizeof(VBGLIOCHGCMCALL) + pData->cParms * sizeof(VBGLIOCHGCMCALL)));

    return VbglR0IdcCallRaw(&handle->IdcHandle, VBGL_IOCTL_HGCM_CALL(cbData), &pData->Hdr, cbData);
}

DECLR0VBGL(int) VbglR0HGCMCall(VBGLHGCMHANDLE handle, PVBGLIOCHGCMCALL pData, uint32_t cbData)
{
    int rc = VbglR0HGCMCallRaw(handle, pData, cbData);
    if (RT_SUCCESS(rc))
        rc = pData->Hdr.rc;
    return rc;
}

DECLR0VBGL(int) VbglR0HGCMCallUserDataRaw(VBGLHGCMHANDLE handle, PVBGLIOCHGCMCALL pData, uint32_t cbData)
{
    VBGL_HGCM_ASSERT_MSG(cbData >= sizeof(VBGLIOCHGCMCALL) + pData->cParms * sizeof(HGCMFunctionParameter),
                         ("cbData = %d, cParms = %d (calculated size %d)\n", cbData, pData->cParms,
                          sizeof(VBGLIOCHGCMCALL) + pData->cParms * sizeof(VBGLIOCHGCMCALL)));

    return VbglR0IdcCallRaw(&handle->IdcHandle, VBGL_IOCTL_HGCM_CALL_WITH_USER_DATA(cbData), &pData->Hdr, cbData);
}


DECLR0VBGL(int) VbglR0HGCMFastCall(VBGLHGCMHANDLE hHandle, PVBGLIOCIDCHGCMFASTCALL pCallReq, uint32_t cbCallReq)
{
    /* pCallReq->Hdr.rc and pCallReq->HgcmCallReq.header.header.rc; are not used by this IDC. */
    return VbglR0IdcCallRaw(&hHandle->IdcHandle, VBGL_IOCTL_IDC_HGCM_FAST_CALL, &pCallReq->Hdr, cbCallReq);
}

