/* $Id: VBoxGuest.cpp $ */
/** @file
 * VBoxGuest - Guest Additions Driver, Common Code.
 */

/*
 * Copyright (C) 2007-2017 Oracle Corporation
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

/** @page pg_vbdrv VBoxGuest
 *
 * VBoxGuest is the device driver for VMMDev.
 *
 * The device driver is shipped as part of the guest additions.  It has roots in
 * the host VMM support driver (usually known as VBoxDrv), so fixes in platform
 * specific code may apply to both drivers.
 *
 * The common code lives in VBoxGuest.cpp and is compiled both as C++ and C.
 * The VBoxGuest.cpp source file shall not contain platform specific code,
 * though it must occationally do a few \#ifdef RT_OS_XXX tests to cater for
 * platform differences.  Though, in those cases, it is common that more than
 * one platform needs special handling.
 *
 * On most platforms the device driver should create two device nodes, one for
 * full (unrestricted) access to the feature set, and one which only provides a
 * restrict set of functions.  These are generally referred to as 'vboxguest'
 * and 'vboxuser' respectively.  Currently, this two device approach is only
 * implemented on Linux!
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP   LOG_GROUP_DEFAULT
#include "VBoxGuestInternal.h"
#include <VBox/VMMDev.h> /* for VMMDEV_RAM_SIZE */
#include <VBox/log.h>
#include <iprt/mem.h>
#include <iprt/time.h>
#include <iprt/memobj.h>
#include <iprt/asm.h>
#include <iprt/asm-amd64-x86.h>
#include <iprt/string.h>
#include <iprt/process.h>
#include <iprt/assert.h>
#include <iprt/param.h>
#include <iprt/timer.h>
#ifdef VBOX_WITH_HGCM
# include <iprt/thread.h>
#endif
#include "version-generated.h"
#if defined(RT_OS_LINUX) || defined(RT_OS_FREEBSD)
# include "revision-generated.h"
#endif
#if defined(RT_OS_SOLARIS) || defined(RT_OS_DARWIN)
# include <iprt/rand.h>
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define VBOXGUEST_ACQUIRE_STYLE_EVENTS (VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST | VMMDEV_EVENT_SEAMLESS_MODE_CHANGE_REQUEST)


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
#ifdef VBOX_WITH_HGCM
static DECLCALLBACK(int) vgdrvHgcmAsyncWaitCallback(VMMDevHGCMRequestHeader *pHdrNonVolatile, void *pvUser, uint32_t u32User);
#endif
static int      vgdrvIoCtl_CancelAllWaitEvents(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession);
static void     vgdrvBitUsageTrackerClear(PVBOXGUESTBITUSAGETRACER pTracker);
static uint32_t vgdrvGetAllowedEventMaskForSession(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession);
static int      vgdrvResetEventFilterOnHost(PVBOXGUESTDEVEXT pDevExt, uint32_t fFixedEvents);
static int      vgdrvResetMouseStatusOnHost(PVBOXGUESTDEVEXT pDevExt);
static int      vgdrvResetCapabilitiesOnHost(PVBOXGUESTDEVEXT pDevExt);
static int      vgdrvSetSessionEventFilter(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession,
                                           uint32_t fOrMask, uint32_t fNotMask, bool fSessionTermination);
static int      vgdrvSetSessionMouseStatus(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession,
                                           uint32_t fOrMask, uint32_t fNotMask, bool fSessionTermination);
static int      vgdrvSetSessionCapabilities(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession,
                                            uint32_t fOrMask, uint32_t fNoMask,
                                            uint32_t *pfSessionCaps, uint32_t *pfGlobalCaps, bool fSessionTermination);
static int      vgdrvAcquireSessionCapabilities(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession,
                                                uint32_t fOrMask, uint32_t fNotMask, uint32_t fFlags, bool fSessionTermination);
static int      vgdrvDispatchEventsLocked(PVBOXGUESTDEVEXT pDevExt, uint32_t fEvents);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static const uint32_t g_cbChangeMemBalloonReq = RT_OFFSETOF(VMMDevChangeMemBalloon, aPhysPage[VMMDEV_MEMORY_BALLOON_CHUNK_PAGES]);

#if defined(RT_OS_DARWIN) || defined(RT_OS_SOLARIS)
/**
 * Drag in the rest of IRPT since we share it with the
 * rest of the kernel modules on Solaris.
 */
PFNRT g_apfnVBoxGuestIPRTDeps[] =
{
    /* VirtioNet */
    (PFNRT)RTRandBytes,
    /* RTSemMutex* */
    (PFNRT)RTSemMutexCreate,
    (PFNRT)RTSemMutexDestroy,
    (PFNRT)RTSemMutexRequest,
    (PFNRT)RTSemMutexRequestNoResume,
    (PFNRT)RTSemMutexRequestDebug,
    (PFNRT)RTSemMutexRequestNoResumeDebug,
    (PFNRT)RTSemMutexRelease,
    (PFNRT)RTSemMutexIsOwned,
    NULL
};
#endif  /* RT_OS_DARWIN || RT_OS_SOLARIS  */


/**
 * Reserves memory in which the VMM can relocate any guest mappings
 * that are floating around.
 *
 * This operation is a little bit tricky since the VMM might not accept
 * just any address because of address clashes between the three contexts
 * it operates in, so use a small stack to perform this operation.
 *
 * @returns VBox status code (ignored).
 * @param   pDevExt     The device extension.
 */
static int vgdrvInitFixateGuestMappings(PVBOXGUESTDEVEXT pDevExt)
{
    /*
     * Query the required space.
     */
    VMMDevReqHypervisorInfo *pReq;
    int rc = VbglR0GRAlloc((VMMDevRequestHeader **)&pReq, sizeof(VMMDevReqHypervisorInfo), VMMDevReq_GetHypervisorInfo);
    if (RT_FAILURE(rc))
        return rc;
    pReq->hypervisorStart = 0;
    pReq->hypervisorSize  = 0;
    rc = VbglR0GRPerform(&pReq->header);
    if (RT_FAILURE(rc)) /* this shouldn't happen! */
    {
        VbglR0GRFree(&pReq->header);
        return rc;
    }

    /*
     * The VMM will report back if there is nothing it wants to map, like for
     * instance in VT-x and AMD-V mode.
     */
    if (pReq->hypervisorSize == 0)
        Log(("vgdrvInitFixateGuestMappings: nothing to do\n"));
    else
    {
        /*
         * We have to try several times since the host can be picky
         * about certain addresses.
         */
        RTR0MEMOBJ  hFictive     = NIL_RTR0MEMOBJ;
        uint32_t    cbHypervisor = pReq->hypervisorSize;
        RTR0MEMOBJ  ahTries[5];
        uint32_t    iTry;
        bool        fBitched = false;
        Log(("vgdrvInitFixateGuestMappings: cbHypervisor=%#x\n", cbHypervisor));
        for (iTry = 0; iTry < RT_ELEMENTS(ahTries); iTry++)
        {
            /*
             * Reserve space, or if that isn't supported, create a object for
             * some fictive physical memory and map that in to kernel space.
             *
             * To make the code a bit uglier, most systems cannot help with
             * 4MB alignment, so we have to deal with that in addition to
             * having two ways of getting the memory.
             */
            uint32_t    uAlignment = _4M;
            RTR0MEMOBJ  hObj;
            rc = RTR0MemObjReserveKernel(&hObj, (void *)-1, RT_ALIGN_32(cbHypervisor, _4M), uAlignment);
            if (rc == VERR_NOT_SUPPORTED)
            {
                uAlignment = PAGE_SIZE;
                rc = RTR0MemObjReserveKernel(&hObj, (void *)-1, RT_ALIGN_32(cbHypervisor, _4M) + _4M, uAlignment);
            }
            /*
             * If both RTR0MemObjReserveKernel calls above failed because either not supported or
             * not implemented at all at the current platform, try to map the memory object into the
             * virtual kernel space.
             */
            if (rc == VERR_NOT_SUPPORTED)
            {
                if (hFictive == NIL_RTR0MEMOBJ)
                {
                    rc = RTR0MemObjEnterPhys(&hObj, VBOXGUEST_HYPERVISOR_PHYSICAL_START, cbHypervisor + _4M, RTMEM_CACHE_POLICY_DONT_CARE);
                    if (RT_FAILURE(rc))
                        break;
                    hFictive = hObj;
                }
                uAlignment = _4M;
                rc = RTR0MemObjMapKernel(&hObj, hFictive, (void *)-1, uAlignment, RTMEM_PROT_READ | RTMEM_PROT_WRITE);
                if (rc == VERR_NOT_SUPPORTED)
                {
                    uAlignment = PAGE_SIZE;
                    rc = RTR0MemObjMapKernel(&hObj, hFictive, (void *)-1, uAlignment, RTMEM_PROT_READ | RTMEM_PROT_WRITE);
                }
            }
            if (RT_FAILURE(rc))
            {
                LogRel(("VBoxGuest: Failed to reserve memory for the hypervisor: rc=%Rrc (cbHypervisor=%#x uAlignment=%#x iTry=%u)\n",
                        rc, cbHypervisor, uAlignment, iTry));
                fBitched = true;
                break;
            }

            /*
             * Try set it.
             */
            pReq->header.requestType = VMMDevReq_SetHypervisorInfo;
            pReq->header.rc          = VERR_INTERNAL_ERROR;
            pReq->hypervisorSize     = cbHypervisor;
            pReq->hypervisorStart    = (RTGCPTR32)(uintptr_t)RTR0MemObjAddress(hObj);
            if (    uAlignment == PAGE_SIZE
                &&  pReq->hypervisorStart & (_4M - 1))
                pReq->hypervisorStart = RT_ALIGN_32(pReq->hypervisorStart, _4M);
            AssertMsg(RT_ALIGN_32(pReq->hypervisorStart, _4M) == pReq->hypervisorStart, ("%#x\n", pReq->hypervisorStart));

            rc = VbglR0GRPerform(&pReq->header);
            if (RT_SUCCESS(rc))
            {
                pDevExt->hGuestMappings = hFictive != NIL_RTR0MEMOBJ ? hFictive : hObj;
                Log(("VBoxGuest: %p LB %#x; uAlignment=%#x iTry=%u hGuestMappings=%p (%s)\n",
                     RTR0MemObjAddress(pDevExt->hGuestMappings),
                     RTR0MemObjSize(pDevExt->hGuestMappings),
                     uAlignment, iTry, pDevExt->hGuestMappings, hFictive != NIL_RTR0PTR ? "fictive" : "reservation"));
                break;
            }
            ahTries[iTry] = hObj;
        }

        /*
         * Cleanup failed attempts.
         */
        while (iTry-- > 0)
            RTR0MemObjFree(ahTries[iTry], false /* fFreeMappings */);
        if (    RT_FAILURE(rc)
            &&  hFictive != NIL_RTR0PTR)
            RTR0MemObjFree(hFictive, false /* fFreeMappings */);
        if (RT_FAILURE(rc) && !fBitched)
            LogRel(("VBoxGuest: Warning: failed to reserve %#d of memory for guest mappings.\n", cbHypervisor));
    }
    VbglR0GRFree(&pReq->header);

    /*
     * We ignore failed attempts for now.
     */
    return VINF_SUCCESS;
}


/**
 * Undo what vgdrvInitFixateGuestMappings did.
 *
 * @param   pDevExt     The device extension.
 */
static void vgdrvTermUnfixGuestMappings(PVBOXGUESTDEVEXT pDevExt)
{
    if (pDevExt->hGuestMappings != NIL_RTR0PTR)
    {
        /*
         * Tell the host that we're going to free the memory we reserved for
         * it, the free it up. (Leak the memory if anything goes wrong here.)
         */
        VMMDevReqHypervisorInfo *pReq;
        int rc = VbglR0GRAlloc((VMMDevRequestHeader **)&pReq, sizeof(VMMDevReqHypervisorInfo), VMMDevReq_SetHypervisorInfo);
        if (RT_SUCCESS(rc))
        {
            pReq->hypervisorStart = 0;
            pReq->hypervisorSize  = 0;
            rc = VbglR0GRPerform(&pReq->header);
            VbglR0GRFree(&pReq->header);
        }
        if (RT_SUCCESS(rc))
        {
            rc = RTR0MemObjFree(pDevExt->hGuestMappings, true /* fFreeMappings */);
            AssertRC(rc);
        }
        else
            LogRel(("vgdrvTermUnfixGuestMappings: Failed to unfix the guest mappings! rc=%Rrc\n", rc));

        pDevExt->hGuestMappings = NIL_RTR0MEMOBJ;
    }
}



/**
 * Report the guest information to the host.
 *
 * @returns IPRT status code.
 * @param   enmOSType       The OS type to report.
 */
static int vgdrvReportGuestInfo(VBOXOSTYPE enmOSType)
{
    /*
     * Allocate and fill in the two guest info reports.
     */
    VMMDevReportGuestInfo2 *pReqInfo2 = NULL;
    VMMDevReportGuestInfo  *pReqInfo1 = NULL;
    int rc = VbglR0GRAlloc((VMMDevRequestHeader **)&pReqInfo2, sizeof (VMMDevReportGuestInfo2), VMMDevReq_ReportGuestInfo2);
    Log(("vgdrvReportGuestInfo: VbglR0GRAlloc VMMDevReportGuestInfo2 completed with rc=%Rrc\n", rc));
    if (RT_SUCCESS(rc))
    {
        pReqInfo2->guestInfo.additionsMajor    = VBOX_VERSION_MAJOR;
        pReqInfo2->guestInfo.additionsMinor    = VBOX_VERSION_MINOR;
        pReqInfo2->guestInfo.additionsBuild    = VBOX_VERSION_BUILD;
        pReqInfo2->guestInfo.additionsRevision = VBOX_SVN_REV;
        pReqInfo2->guestInfo.additionsFeatures = 0; /* (no features defined yet) */
        RTStrCopy(pReqInfo2->guestInfo.szName, sizeof(pReqInfo2->guestInfo.szName), VBOX_VERSION_STRING);

        rc = VbglR0GRAlloc((VMMDevRequestHeader **)&pReqInfo1, sizeof (VMMDevReportGuestInfo), VMMDevReq_ReportGuestInfo);
        Log(("vgdrvReportGuestInfo: VbglR0GRAlloc VMMDevReportGuestInfo completed with rc=%Rrc\n", rc));
        if (RT_SUCCESS(rc))
        {
            pReqInfo1->guestInfo.interfaceVersion = VMMDEV_VERSION;
            pReqInfo1->guestInfo.osType           = enmOSType;

            /*
             * There are two protocols here:
             *      1. Info2 + Info1. Supported by >=3.2.51.
             *      2. Info1 and optionally Info2. The old protocol.
             *
             * We try protocol 1 first.  It will fail with VERR_NOT_SUPPORTED
             * if not supported by the VMMDev (message ordering requirement).
             */
            rc = VbglR0GRPerform(&pReqInfo2->header);
            Log(("vgdrvReportGuestInfo: VbglR0GRPerform VMMDevReportGuestInfo2 completed with rc=%Rrc\n", rc));
            if (RT_SUCCESS(rc))
            {
                rc = VbglR0GRPerform(&pReqInfo1->header);
                Log(("vgdrvReportGuestInfo: VbglR0GRPerform VMMDevReportGuestInfo completed with rc=%Rrc\n", rc));
            }
            else if (   rc == VERR_NOT_SUPPORTED
                     || rc == VERR_NOT_IMPLEMENTED)
            {
                rc = VbglR0GRPerform(&pReqInfo1->header);
                Log(("vgdrvReportGuestInfo: VbglR0GRPerform VMMDevReportGuestInfo completed with rc=%Rrc\n", rc));
                if (RT_SUCCESS(rc))
                {
                    rc = VbglR0GRPerform(&pReqInfo2->header);
                    Log(("vgdrvReportGuestInfo: VbglR0GRPerform VMMDevReportGuestInfo2 completed with rc=%Rrc\n", rc));
                    if (rc == VERR_NOT_IMPLEMENTED)
                        rc = VINF_SUCCESS;
                }
            }
            VbglR0GRFree(&pReqInfo1->header);
        }
        VbglR0GRFree(&pReqInfo2->header);
    }

    return rc;
}


/**
 * Report the guest driver status to the host.
 *
 * @returns IPRT status code.
 * @param   fActive         Flag whether the driver is now active or not.
 */
static int vgdrvReportDriverStatus(bool fActive)
{
    /*
     * Report guest status of the VBox driver to the host.
     */
    VMMDevReportGuestStatus *pReq2 = NULL;
    int rc = VbglR0GRAlloc((VMMDevRequestHeader **)&pReq2, sizeof(*pReq2), VMMDevReq_ReportGuestStatus);
    Log(("vgdrvReportDriverStatus: VbglR0GRAlloc VMMDevReportGuestStatus completed with rc=%Rrc\n", rc));
    if (RT_SUCCESS(rc))
    {
        pReq2->guestStatus.facility = VBoxGuestFacilityType_VBoxGuestDriver;
        pReq2->guestStatus.status = fActive ?
                                    VBoxGuestFacilityStatus_Active
                                  : VBoxGuestFacilityStatus_Inactive;
        pReq2->guestStatus.flags = 0;
        rc = VbglR0GRPerform(&pReq2->header);
        Log(("vgdrvReportDriverStatus: VbglR0GRPerform VMMDevReportGuestStatus completed with fActive=%d, rc=%Rrc\n",
             fActive ? 1 : 0, rc));
        if (rc == VERR_NOT_IMPLEMENTED) /* Compatibility with older hosts. */
            rc = VINF_SUCCESS;
        VbglR0GRFree(&pReq2->header);
    }

    return rc;
}


/** @name Memory Ballooning
 * @{
 */

/**
 * Inflate the balloon by one chunk represented by an R0 memory object.
 *
 * The caller owns the balloon mutex.
 *
 * @returns IPRT status code.
 * @param   pMemObj     Pointer to the R0 memory object.
 * @param   pReq        The pre-allocated request for performing the VMMDev call.
 */
static int vgdrvBalloonInflate(PRTR0MEMOBJ pMemObj, VMMDevChangeMemBalloon *pReq)
{
    uint32_t iPage;
    int rc;

    for (iPage = 0; iPage < VMMDEV_MEMORY_BALLOON_CHUNK_PAGES; iPage++)
    {
        RTHCPHYS phys = RTR0MemObjGetPagePhysAddr(*pMemObj, iPage);
        pReq->aPhysPage[iPage] = phys;
    }

    pReq->fInflate = true;
    pReq->header.size = g_cbChangeMemBalloonReq;
    pReq->cPages = VMMDEV_MEMORY_BALLOON_CHUNK_PAGES;

    rc = VbglR0GRPerform(&pReq->header);
    if (RT_FAILURE(rc))
        LogRel(("vgdrvBalloonInflate: VbglR0GRPerform failed. rc=%Rrc\n", rc));
    return rc;
}


/**
 * Deflate the balloon by one chunk - info the host and free the memory object.
 *
 * The caller owns the balloon mutex.
 *
 * @returns IPRT status code.
 * @param   pMemObj     Pointer to the R0 memory object.
 *                      The memory object will be freed afterwards.
 * @param   pReq        The pre-allocated request for performing the VMMDev call.
 */
static int vgdrvBalloonDeflate(PRTR0MEMOBJ pMemObj, VMMDevChangeMemBalloon *pReq)
{
    uint32_t iPage;
    int rc;

    for (iPage = 0; iPage < VMMDEV_MEMORY_BALLOON_CHUNK_PAGES; iPage++)
    {
        RTHCPHYS phys = RTR0MemObjGetPagePhysAddr(*pMemObj, iPage);
        pReq->aPhysPage[iPage] = phys;
    }

    pReq->fInflate = false;
    pReq->header.size = g_cbChangeMemBalloonReq;
    pReq->cPages = VMMDEV_MEMORY_BALLOON_CHUNK_PAGES;

    rc = VbglR0GRPerform(&pReq->header);
    if (RT_FAILURE(rc))
    {
        LogRel(("vgdrvBalloonDeflate: VbglR0GRPerform failed. rc=%Rrc\n", rc));
        return rc;
    }

    rc = RTR0MemObjFree(*pMemObj, true);
    if (RT_FAILURE(rc))
    {
        LogRel(("vgdrvBalloonDeflate: RTR0MemObjFree(%p,true) -> %Rrc; this is *BAD*!\n", *pMemObj, rc));
        return rc;
    }

    *pMemObj = NIL_RTR0MEMOBJ;
    return VINF_SUCCESS;
}


/**
 * Inflate/deflate the memory balloon and notify the host.
 *
 * This is a worker used by vgdrvIoCtl_CheckMemoryBalloon - it takes the mutex.
 *
 * @returns VBox status code.
 * @param   pDevExt         The device extension.
 * @param   cBalloonChunks  The new size of the balloon in chunks of 1MB.
 * @param   pfHandleInR3    Where to return the handle-in-ring3 indicator
 *                          (VINF_SUCCESS if set).
 */
static int vgdrvSetBalloonSizeKernel(PVBOXGUESTDEVEXT pDevExt, uint32_t cBalloonChunks, bool *pfHandleInR3)
{
    int rc = VINF_SUCCESS;

    if (pDevExt->MemBalloon.fUseKernelAPI)
    {
        VMMDevChangeMemBalloon *pReq;
        uint32_t i;

        if (cBalloonChunks > pDevExt->MemBalloon.cMaxChunks)
        {
            LogRel(("vgdrvSetBalloonSizeKernel: illegal balloon size %u (max=%u)\n",
                    cBalloonChunks, pDevExt->MemBalloon.cMaxChunks));
            return VERR_INVALID_PARAMETER;
        }

        if (cBalloonChunks == pDevExt->MemBalloon.cMaxChunks)
            return VINF_SUCCESS;   /* nothing to do */

        if (   cBalloonChunks > pDevExt->MemBalloon.cChunks
            && !pDevExt->MemBalloon.paMemObj)
        {
            pDevExt->MemBalloon.paMemObj = (PRTR0MEMOBJ)RTMemAllocZ(sizeof(RTR0MEMOBJ) * pDevExt->MemBalloon.cMaxChunks);
            if (!pDevExt->MemBalloon.paMemObj)
            {
                LogRel(("vgdrvSetBalloonSizeKernel: no memory for paMemObj!\n"));
                return VERR_NO_MEMORY;
            }
        }

        rc = VbglR0GRAlloc((VMMDevRequestHeader **)&pReq, g_cbChangeMemBalloonReq, VMMDevReq_ChangeMemBalloon);
        if (RT_FAILURE(rc))
            return rc;

        if (cBalloonChunks > pDevExt->MemBalloon.cChunks)
        {
            /* inflate */
            for (i = pDevExt->MemBalloon.cChunks; i < cBalloonChunks; i++)
            {
                rc = RTR0MemObjAllocPhysNC(&pDevExt->MemBalloon.paMemObj[i],
                                           VMMDEV_MEMORY_BALLOON_CHUNK_SIZE, NIL_RTHCPHYS);
                if (RT_FAILURE(rc))
                {
                    if (rc == VERR_NOT_SUPPORTED)
                    {
                        /* not supported -- fall back to the R3-allocated memory. */
                        rc = VINF_SUCCESS;
                        pDevExt->MemBalloon.fUseKernelAPI = false;
                        Assert(pDevExt->MemBalloon.cChunks == 0);
                        Log(("VBoxGuestSetBalloonSizeKernel: PhysNC allocs not supported, falling back to R3 allocs.\n"));
                    }
                    /* else if (rc == VERR_NO_MEMORY || rc == VERR_NO_PHYS_MEMORY):
                     *      cannot allocate more memory => don't try further, just stop here */
                    /* else: XXX what else can fail?  VERR_MEMOBJ_INIT_FAILED for instance. just stop. */
                    break;
                }

                rc = vgdrvBalloonInflate(&pDevExt->MemBalloon.paMemObj[i], pReq);
                if (RT_FAILURE(rc))
                {
                    Log(("vboxGuestSetBalloonSize(inflate): failed, rc=%Rrc!\n", rc));
                    RTR0MemObjFree(pDevExt->MemBalloon.paMemObj[i], true);
                    pDevExt->MemBalloon.paMemObj[i] = NIL_RTR0MEMOBJ;
                    break;
                }
                pDevExt->MemBalloon.cChunks++;
            }
        }
        else
        {
            /* deflate */
            for (i = pDevExt->MemBalloon.cChunks; i-- > cBalloonChunks;)
            {
                rc = vgdrvBalloonDeflate(&pDevExt->MemBalloon.paMemObj[i], pReq);
                if (RT_FAILURE(rc))
                {
                    Log(("vboxGuestSetBalloonSize(deflate): failed, rc=%Rrc!\n", rc));
                    break;
                }
                pDevExt->MemBalloon.cChunks--;
            }
        }

        VbglR0GRFree(&pReq->header);
    }

    /*
     * Set the handle-in-ring3 indicator.  When set Ring-3 will have to work
     * the balloon changes via the other API.
     */
    *pfHandleInR3 = pDevExt->MemBalloon.fUseKernelAPI ? false : true;

    return rc;
}


/**
 * Inflate/deflate the balloon by one chunk.
 *
 * Worker for vgdrvIoCtl_ChangeMemoryBalloon - it takes the mutex.
 *
 * @returns VBox status code.
 * @param   pDevExt         The device extension.
 * @param   pSession        The session.
 * @param   pvChunk         The address of the chunk to add to / remove from the
 *                          balloon. (user space address)
 * @param   fInflate        Inflate if true, deflate if false.
 */
static int vgdrvSetBalloonSizeFromUser(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession, RTR3PTR pvChunk, bool fInflate)
{
    VMMDevChangeMemBalloon *pReq;
    PRTR0MEMOBJ pMemObj = NULL;
    int rc = VINF_SUCCESS;
    uint32_t i;
    RT_NOREF1(pSession);

    if (fInflate)
    {
        if (   pDevExt->MemBalloon.cChunks > pDevExt->MemBalloon.cMaxChunks - 1
            || pDevExt->MemBalloon.cMaxChunks == 0 /* If called without first querying. */)
        {
            LogRel(("vgdrvSetBalloonSizeFromUser: cannot inflate balloon, already have %u chunks (max=%u)\n",
                    pDevExt->MemBalloon.cChunks, pDevExt->MemBalloon.cMaxChunks));
            return VERR_INVALID_PARAMETER;
        }

        if (!pDevExt->MemBalloon.paMemObj)
        {
            pDevExt->MemBalloon.paMemObj = (PRTR0MEMOBJ)RTMemAlloc(sizeof(RTR0MEMOBJ) * pDevExt->MemBalloon.cMaxChunks);
            if (!pDevExt->MemBalloon.paMemObj)
            {
                LogRel(("vgdrvSetBalloonSizeFromUser: no memory for paMemObj!\n"));
                return VERR_NO_MEMORY;
            }
            for (i = 0; i < pDevExt->MemBalloon.cMaxChunks; i++)
                pDevExt->MemBalloon.paMemObj[i] = NIL_RTR0MEMOBJ;
        }
    }
    else
    {
        if (pDevExt->MemBalloon.cChunks == 0)
        {
            AssertMsgFailed(("vgdrvSetBalloonSizeFromUser: cannot decrease balloon, already at size 0\n"));
            return VERR_INVALID_PARAMETER;
        }
    }

    /*
     * Enumerate all memory objects and check if the object is already registered.
     */
    for (i = 0; i < pDevExt->MemBalloon.cMaxChunks; i++)
    {
        if (   fInflate
            && !pMemObj
            && pDevExt->MemBalloon.paMemObj[i] == NIL_RTR0MEMOBJ)
            pMemObj = &pDevExt->MemBalloon.paMemObj[i]; /* found free object pointer */
        if (RTR0MemObjAddressR3(pDevExt->MemBalloon.paMemObj[i]) == pvChunk)
        {
            if (fInflate)
                return VERR_ALREADY_EXISTS; /* don't provide the same memory twice */
            pMemObj = &pDevExt->MemBalloon.paMemObj[i];
            break;
        }
    }
    if (!pMemObj)
    {
        if (fInflate)
        {
            /* no free object pointer found -- should not happen */
            return VERR_NO_MEMORY;
        }

        /* cannot free this memory as it wasn't provided before */
        return VERR_NOT_FOUND;
    }

    /*
     * Try inflate / default the balloon as requested.
     */
    rc = VbglR0GRAlloc((VMMDevRequestHeader **)&pReq, g_cbChangeMemBalloonReq, VMMDevReq_ChangeMemBalloon);
    if (RT_FAILURE(rc))
        return rc;

    if (fInflate)
    {
        rc = RTR0MemObjLockUser(pMemObj, pvChunk, VMMDEV_MEMORY_BALLOON_CHUNK_SIZE,
                                RTMEM_PROT_READ | RTMEM_PROT_WRITE, NIL_RTR0PROCESS);
        if (RT_SUCCESS(rc))
        {
            rc = vgdrvBalloonInflate(pMemObj, pReq);
            if (RT_SUCCESS(rc))
                pDevExt->MemBalloon.cChunks++;
            else
            {
                Log(("vgdrvSetBalloonSizeFromUser(inflate): failed, rc=%Rrc!\n", rc));
                RTR0MemObjFree(*pMemObj, true);
                *pMemObj = NIL_RTR0MEMOBJ;
            }
        }
    }
    else
    {
        rc = vgdrvBalloonDeflate(pMemObj, pReq);
        if (RT_SUCCESS(rc))
            pDevExt->MemBalloon.cChunks--;
        else
            Log(("vgdrvSetBalloonSizeFromUser(deflate): failed, rc=%Rrc!\n", rc));
    }

    VbglR0GRFree(&pReq->header);
    return rc;
}


/**
 * Cleanup the memory balloon of a session.
 *
 * Will request the balloon mutex, so it must be valid and the caller must not
 * own it already.
 *
 * @param   pDevExt     The device extension.
 * @param   pSession    The session.  Can be NULL at unload.
 */
static void vgdrvCloseMemBalloon(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession)
{
    RTSemFastMutexRequest(pDevExt->MemBalloon.hMtx);
    if (    pDevExt->MemBalloon.pOwner == pSession
        ||  pSession == NULL /*unload*/)
    {
        if (pDevExt->MemBalloon.paMemObj)
        {
            VMMDevChangeMemBalloon *pReq;
            int rc = VbglR0GRAlloc((VMMDevRequestHeader **)&pReq, g_cbChangeMemBalloonReq, VMMDevReq_ChangeMemBalloon);
            if (RT_SUCCESS(rc))
            {
                uint32_t i;
                for (i = pDevExt->MemBalloon.cChunks; i-- > 0;)
                {
                    rc = vgdrvBalloonDeflate(&pDevExt->MemBalloon.paMemObj[i], pReq);
                    if (RT_FAILURE(rc))
                    {
                        LogRel(("vgdrvCloseMemBalloon: Deflate failed with rc=%Rrc.  Will leak %u chunks.\n",
                                rc, pDevExt->MemBalloon.cChunks));
                        break;
                    }
                    pDevExt->MemBalloon.paMemObj[i] = NIL_RTR0MEMOBJ;
                    pDevExt->MemBalloon.cChunks--;
                }
                VbglR0GRFree(&pReq->header);
            }
            else
                LogRel(("vgdrvCloseMemBalloon: Failed to allocate VMMDev request buffer (rc=%Rrc).  Will leak %u chunks.\n",
                        rc, pDevExt->MemBalloon.cChunks));
            RTMemFree(pDevExt->MemBalloon.paMemObj);
            pDevExt->MemBalloon.paMemObj = NULL;
        }

        pDevExt->MemBalloon.pOwner = NULL;
    }
    RTSemFastMutexRelease(pDevExt->MemBalloon.hMtx);
}

/** @} */



/** @name Heartbeat
 * @{
 */

/**
 * Sends heartbeat to host.
 *
 * @returns VBox status code.
 */
static int vgdrvHeartbeatSend(PVBOXGUESTDEVEXT pDevExt)
{
    int rc;
    if (pDevExt->pReqGuestHeartbeat)
    {
        rc = VbglR0GRPerform(pDevExt->pReqGuestHeartbeat);
        Log3(("vgdrvHeartbeatSend: VbglR0GRPerform vgdrvHeartbeatSend completed with rc=%Rrc\n", rc));
    }
    else
        rc = VERR_INVALID_STATE;
    return rc;
}


/**
 * Callback for heartbeat timer.
 */
static DECLCALLBACK(void) vgdrvHeartbeatTimerHandler(PRTTIMER hTimer, void *pvUser, uint64_t iTick)
{
    PVBOXGUESTDEVEXT pDevExt = (PVBOXGUESTDEVEXT)pvUser;
    int rc;
    AssertReturnVoid(pDevExt);

    rc = vgdrvHeartbeatSend(pDevExt);
    if (RT_FAILURE(rc))
        Log(("HB Timer: vgdrvHeartbeatSend failed: rc=%Rrc\n", rc));

    NOREF(hTimer); NOREF(iTick);
}


/**
 * Configure the host to check guest's heartbeat
 * and get heartbeat interval from the host.
 *
 * @returns VBox status code.
 * @param   pDevExt         The device extension.
 * @param   fEnabled        Set true to enable guest heartbeat checks on host.
 */
static int vgdrvHeartbeatHostConfigure(PVBOXGUESTDEVEXT pDevExt, bool fEnabled)
{
    VMMDevReqHeartbeat *pReq;
    int rc = VbglR0GRAlloc((VMMDevRequestHeader **)&pReq, sizeof(*pReq), VMMDevReq_HeartbeatConfigure);
    Log(("vgdrvHeartbeatHostConfigure: VbglR0GRAlloc vgdrvHeartbeatHostConfigure completed with rc=%Rrc\n", rc));
    if (RT_SUCCESS(rc))
    {
        pReq->fEnabled = fEnabled;
        pReq->cNsInterval = 0;
        rc = VbglR0GRPerform(&pReq->header);
        Log(("vgdrvHeartbeatHostConfigure: VbglR0GRPerform vgdrvHeartbeatHostConfigure completed with rc=%Rrc\n", rc));
        pDevExt->cNsHeartbeatInterval = pReq->cNsInterval;
        VbglR0GRFree(&pReq->header);
    }
    return rc;
}


/**
 * Initializes the heartbeat timer.
 *
 * This feature may be disabled by the host.
 *
 * @returns VBox status (ignored).
 * @param   pDevExt             The device extension.
 */
static int vgdrvHeartbeatInit(PVBOXGUESTDEVEXT pDevExt)
{
    /*
     * Make sure that heartbeat checking is disabled.
     */
    int rc = vgdrvHeartbeatHostConfigure(pDevExt, false);
    if (RT_SUCCESS(rc))
    {
        rc = vgdrvHeartbeatHostConfigure(pDevExt, true);
        if (RT_SUCCESS(rc))
        {
            /*
             * Preallocate the request to use it from the timer callback because:
             *    1) on Windows VbglR0GRAlloc must be called at IRQL <= APC_LEVEL
             *       and the timer callback runs at DISPATCH_LEVEL;
             *    2) avoid repeated allocations.
             */
            rc = VbglR0GRAlloc(&pDevExt->pReqGuestHeartbeat, sizeof(*pDevExt->pReqGuestHeartbeat), VMMDevReq_GuestHeartbeat);
            if (RT_SUCCESS(rc))
            {
                LogRel(("vgdrvHeartbeatInit: Setting up heartbeat to trigger every %RU64 milliseconds\n",
                        pDevExt->cNsHeartbeatInterval / RT_NS_1MS));
                rc = RTTimerCreateEx(&pDevExt->pHeartbeatTimer, pDevExt->cNsHeartbeatInterval, 0 /*fFlags*/,
                                     (PFNRTTIMER)vgdrvHeartbeatTimerHandler, pDevExt);
                if (RT_SUCCESS(rc))
                {
                    rc = RTTimerStart(pDevExt->pHeartbeatTimer, 0);
                    if (RT_SUCCESS(rc))
                        return VINF_SUCCESS;

                    LogRel(("vgdrvHeartbeatInit: Heartbeat timer failed to start, rc=%Rrc\n", rc));
                }
                else
                    LogRel(("vgdrvHeartbeatInit: Failed to create heartbeat timer: %Rrc\n", rc));

                VbglR0GRFree(pDevExt->pReqGuestHeartbeat);
                pDevExt->pReqGuestHeartbeat = NULL;
            }
            else
                LogRel(("vgdrvHeartbeatInit: VbglR0GRAlloc(VMMDevReq_GuestHeartbeat): %Rrc\n", rc));

            LogRel(("vgdrvHeartbeatInit: Failed to set up the timer, guest heartbeat is disabled\n"));
            vgdrvHeartbeatHostConfigure(pDevExt, false);
        }
        else
            LogRel(("vgdrvHeartbeatInit: Failed to configure host for heartbeat checking: rc=%Rrc\n", rc));
    }
    return rc;
}

/** @} */


/**
 * Helper to reinit the VMMDev communication after hibernation.
 *
 * @returns VBox status code.
 * @param   pDevExt         The device extension.
 * @param   enmOSType       The OS type.
 *
 * @todo Call this on all platforms, not just windows.
 */
int VGDrvCommonReinitDevExtAfterHibernation(PVBOXGUESTDEVEXT pDevExt, VBOXOSTYPE enmOSType)
{
    int rc = vgdrvReportGuestInfo(enmOSType);
    if (RT_SUCCESS(rc))
    {
        rc = vgdrvReportDriverStatus(true /* Driver is active */);
        if (RT_FAILURE(rc))
            Log(("VGDrvCommonReinitDevExtAfterHibernation: could not report guest driver status, rc=%Rrc\n", rc));
    }
    else
        Log(("VGDrvCommonReinitDevExtAfterHibernation: could not report guest information to host, rc=%Rrc\n", rc));
    LogFlow(("VGDrvCommonReinitDevExtAfterHibernation: returned with rc=%Rrc\n", rc));
    RT_NOREF1(pDevExt);
    return rc;
}


/**
 * Initializes the VBoxGuest device extension when the
 * device driver is loaded.
 *
 * The native code locates the VMMDev on the PCI bus and retrieve
 * the MMIO and I/O port ranges, this function will take care of
 * mapping the MMIO memory (if present). Upon successful return
 * the native code should set up the interrupt handler.
 *
 * @returns VBox status code.
 *
 * @param   pDevExt         The device extension. Allocated by the native code.
 * @param   IOPortBase      The base of the I/O port range.
 * @param   pvMMIOBase      The base of the MMIO memory mapping.
 *                          This is optional, pass NULL if not present.
 * @param   cbMMIO          The size of the MMIO memory mapping.
 *                          This is optional, pass 0 if not present.
 * @param   enmOSType       The guest OS type to report to the VMMDev.
 * @param   fFixedEvents    Events that will be enabled upon init and no client
 *                          will ever be allowed to mask.
 */
int VGDrvCommonInitDevExt(PVBOXGUESTDEVEXT pDevExt, uint16_t IOPortBase,
                          void *pvMMIOBase, uint32_t cbMMIO, VBOXOSTYPE enmOSType, uint32_t fFixedEvents)
{
    int rc, rc2;

#ifdef VBOX_GUESTDRV_WITH_RELEASE_LOGGER
    /*
     * Create the release log.
     */
    static const char * const s_apszGroups[] = VBOX_LOGGROUP_NAMES;
    PRTLOGGER pRelLogger;
    rc = RTLogCreate(&pRelLogger, 0 /*fFlags*/, "all", "VBOXGUEST_RELEASE_LOG", RT_ELEMENTS(s_apszGroups), s_apszGroups,
                     RTLOGDEST_STDOUT | RTLOGDEST_DEBUGGER, NULL);
    if (RT_SUCCESS(rc))
        RTLogRelSetDefaultInstance(pRelLogger);
    /** @todo Add native hook for getting logger config parameters and setting
     *        them.  On linux we should use the module parameter stuff... */
#endif

    /*
     * Adjust fFixedEvents.
     */
#ifdef VBOX_WITH_HGCM
    fFixedEvents |= VMMDEV_EVENT_HGCM;
#endif

    /*
     * Initialize the data.
     */
    pDevExt->IOPortBase = IOPortBase;
    pDevExt->pVMMDevMemory = NULL;
    pDevExt->hGuestMappings = NIL_RTR0MEMOBJ;
    pDevExt->EventSpinlock = NIL_RTSPINLOCK;
    pDevExt->pIrqAckEvents = NULL;
    pDevExt->PhysIrqAckEvents = NIL_RTCCPHYS;
    RTListInit(&pDevExt->WaitList);
#ifdef VBOX_WITH_HGCM
    RTListInit(&pDevExt->HGCMWaitList);
#endif
#ifdef VBOXGUEST_USE_DEFERRED_WAKE_UP
    RTListInit(&pDevExt->WakeUpList);
#endif
    RTListInit(&pDevExt->WokenUpList);
    RTListInit(&pDevExt->FreeList);
    RTListInit(&pDevExt->SessionList);
    pDevExt->cSessions = 0;
    pDevExt->fLoggingEnabled = false;
    pDevExt->f32PendingEvents = 0;
    pDevExt->u32MousePosChangedSeq = 0;
    pDevExt->SessionSpinlock = NIL_RTSPINLOCK;
    pDevExt->MemBalloon.hMtx = NIL_RTSEMFASTMUTEX;
    pDevExt->MemBalloon.cChunks = 0;
    pDevExt->MemBalloon.cMaxChunks = 0;
    pDevExt->MemBalloon.fUseKernelAPI = true;
    pDevExt->MemBalloon.paMemObj = NULL;
    pDevExt->MemBalloon.pOwner = NULL;
    pDevExt->pfnMouseNotifyCallback = NULL;
    pDevExt->pvMouseNotifyCallbackArg = NULL;
    pDevExt->pReqGuestHeartbeat = NULL;

    pDevExt->fFixedEvents = fFixedEvents;
    vgdrvBitUsageTrackerClear(&pDevExt->EventFilterTracker);
    pDevExt->fEventFilterHost = UINT32_MAX;  /* forces a report */

    vgdrvBitUsageTrackerClear(&pDevExt->MouseStatusTracker);
    pDevExt->fMouseStatusHost = UINT32_MAX;  /* forces a report */

    pDevExt->fAcquireModeGuestCaps = 0;
    pDevExt->fSetModeGuestCaps = 0;
    pDevExt->fAcquiredGuestCaps = 0;
    vgdrvBitUsageTrackerClear(&pDevExt->SetGuestCapsTracker);
    pDevExt->fGuestCapsHost = UINT32_MAX; /* forces a report */

    /*
     * If there is an MMIO region validate the version and size.
     */
    if (pvMMIOBase)
    {
        VMMDevMemory *pVMMDev = (VMMDevMemory *)pvMMIOBase;
        Assert(cbMMIO);
        if (    pVMMDev->u32Version == VMMDEV_MEMORY_VERSION
            &&  pVMMDev->u32Size >= 32
            &&  pVMMDev->u32Size <= cbMMIO)
        {
            pDevExt->pVMMDevMemory = pVMMDev;
            Log(("VGDrvCommonInitDevExt: VMMDevMemory: mapping=%p size=%#RX32 (%#RX32) version=%#RX32\n",
                 pVMMDev, pVMMDev->u32Size, cbMMIO, pVMMDev->u32Version));
        }
        else /* try live without it. */
            LogRel(("VGDrvCommonInitDevExt: Bogus VMMDev memory; u32Version=%RX32 (expected %RX32) u32Size=%RX32 (expected <= %RX32)\n",
                    pVMMDev->u32Version, VMMDEV_MEMORY_VERSION, pVMMDev->u32Size, cbMMIO));
    }

    /*
     * Create the wait and session spinlocks as well as the ballooning mutex.
     */
    rc = RTSpinlockCreate(&pDevExt->EventSpinlock, RTSPINLOCK_FLAGS_INTERRUPT_SAFE, "VBoxGuestEvent");
    if (RT_SUCCESS(rc))
        rc = RTSpinlockCreate(&pDevExt->SessionSpinlock, RTSPINLOCK_FLAGS_INTERRUPT_SAFE, "VBoxGuestSession");
    if (RT_FAILURE(rc))
    {
        LogRel(("VGDrvCommonInitDevExt: failed to create spinlock, rc=%Rrc!\n", rc));
        if (pDevExt->EventSpinlock != NIL_RTSPINLOCK)
            RTSpinlockDestroy(pDevExt->EventSpinlock);
        return rc;
    }

    rc = RTSemFastMutexCreate(&pDevExt->MemBalloon.hMtx);
    if (RT_FAILURE(rc))
    {
        LogRel(("VGDrvCommonInitDevExt: failed to create mutex, rc=%Rrc!\n", rc));
        RTSpinlockDestroy(pDevExt->SessionSpinlock);
        RTSpinlockDestroy(pDevExt->EventSpinlock);
        return rc;
    }

    /*
     * Initialize the guest library and report the guest info back to VMMDev,
     * set the interrupt control filter mask, and fixate the guest mappings
     * made by the VMM.
     */
    rc = VbglR0InitPrimary(pDevExt->IOPortBase, (VMMDevMemory *)pDevExt->pVMMDevMemory);
    if (RT_SUCCESS(rc))
    {
        rc = VbglR0GRAlloc((VMMDevRequestHeader **)&pDevExt->pIrqAckEvents, sizeof(VMMDevEvents), VMMDevReq_AcknowledgeEvents);
        if (RT_SUCCESS(rc))
        {
            pDevExt->PhysIrqAckEvents = VbglR0PhysHeapGetPhysAddr(pDevExt->pIrqAckEvents);
            Assert(pDevExt->PhysIrqAckEvents != 0);

            rc = vgdrvReportGuestInfo(enmOSType);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Set the fixed event and make sure the host doesn't have any lingering
                 * the guest capabilities or mouse status bits set.
                 */
                rc = vgdrvResetEventFilterOnHost(pDevExt, pDevExt->fFixedEvents);
                if (RT_SUCCESS(rc))
                {
                    rc = vgdrvResetCapabilitiesOnHost(pDevExt);
                    if (RT_SUCCESS(rc))
                    {
                        rc = vgdrvResetMouseStatusOnHost(pDevExt);
                        if (RT_SUCCESS(rc))
                        {
                            /*
                             * Initialize stuff which may fail without requiring the driver init to fail.
                             */
                            vgdrvInitFixateGuestMappings(pDevExt);
                            vgdrvHeartbeatInit(pDevExt);

                            /*
                             * Done!
                             */
                            rc = vgdrvReportDriverStatus(true /* Driver is active */);
                            if (RT_FAILURE(rc))
                                LogRel(("VGDrvCommonInitDevExt: VBoxReportGuestDriverStatus failed, rc=%Rrc\n", rc));

                            LogFlowFunc(("VGDrvCommonInitDevExt: returns success\n"));
                            return VINF_SUCCESS;
                        }
                        LogRel(("VGDrvCommonInitDevExt: failed to clear mouse status: rc=%Rrc\n", rc));
                    }
                    else
                        LogRel(("VGDrvCommonInitDevExt: failed to clear guest capabilities: rc=%Rrc\n", rc));
                }
                else
                    LogRel(("VGDrvCommonInitDevExt: failed to set fixed event filter: rc=%Rrc\n", rc));
            }
            else
                LogRel(("VGDrvCommonInitDevExt: vgdrvReportGuestInfo failed: rc=%Rrc\n", rc));
            VbglR0GRFree((VMMDevRequestHeader *)pDevExt->pIrqAckEvents);
        }
        else
            LogRel(("VGDrvCommonInitDevExt: VbglR0GRAlloc failed: rc=%Rrc\n", rc));

        VbglR0TerminatePrimary();
    }
    else
        LogRel(("VGDrvCommonInitDevExt: VbglR0InitPrimary failed: rc=%Rrc\n", rc));

    rc2 = RTSemFastMutexDestroy(pDevExt->MemBalloon.hMtx); AssertRC(rc2);
    rc2 = RTSpinlockDestroy(pDevExt->EventSpinlock); AssertRC(rc2);
    rc2 = RTSpinlockDestroy(pDevExt->SessionSpinlock); AssertRC(rc2);

#ifdef VBOX_GUESTDRV_WITH_RELEASE_LOGGER
    RTLogDestroy(RTLogRelSetDefaultInstance(NULL));
    RTLogDestroy(RTLogSetDefaultInstance(NULL));
#endif
    return rc; /* (failed) */
}


/**
 * Deletes all the items in a wait chain.
 * @param   pList       The head of the chain.
 */
static void vgdrvDeleteWaitList(PRTLISTNODE pList)
{
    while (!RTListIsEmpty(pList))
    {
        int             rc2;
        PVBOXGUESTWAIT  pWait = RTListGetFirst(pList, VBOXGUESTWAIT, ListNode);
        RTListNodeRemove(&pWait->ListNode);

        rc2 = RTSemEventMultiDestroy(pWait->Event); AssertRC(rc2);
        pWait->Event = NIL_RTSEMEVENTMULTI;
        pWait->pSession = NULL;
        RTMemFree(pWait);
    }
}


/**
 * Destroys the VBoxGuest device extension.
 *
 * The native code should call this before the driver is loaded,
 * but don't call this on shutdown.
 *
 * @param   pDevExt         The device extension.
 */
void VGDrvCommonDeleteDevExt(PVBOXGUESTDEVEXT pDevExt)
{
    int rc2;
    Log(("VGDrvCommonDeleteDevExt:\n"));
    Log(("VBoxGuest: The additions driver is terminating.\n"));

    /*
     * Stop and destroy HB timer and
     * disable host heartbeat checking.
     */
    if (pDevExt->pHeartbeatTimer)
    {
        RTTimerDestroy(pDevExt->pHeartbeatTimer);
        vgdrvHeartbeatHostConfigure(pDevExt, false);
    }

    VbglR0GRFree(pDevExt->pReqGuestHeartbeat);
    pDevExt->pReqGuestHeartbeat = NULL;

    /*
     * Clean up the bits that involves the host first.
     */
    vgdrvTermUnfixGuestMappings(pDevExt);
    if (!RTListIsEmpty(&pDevExt->SessionList))
    {
        LogRelFunc(("session list not empty!\n"));
        RTListInit(&pDevExt->SessionList);
    }
    /* Update the host flags (mouse status etc) not to reflect this session. */
    pDevExt->fFixedEvents = 0;
    vgdrvResetEventFilterOnHost(pDevExt, 0 /*fFixedEvents*/);
    vgdrvResetCapabilitiesOnHost(pDevExt);
    vgdrvResetMouseStatusOnHost(pDevExt);

    vgdrvCloseMemBalloon(pDevExt, (PVBOXGUESTSESSION)NULL);

    /*
     * Cleanup all the other resources.
     */
    rc2 = RTSpinlockDestroy(pDevExt->EventSpinlock); AssertRC(rc2);
    rc2 = RTSpinlockDestroy(pDevExt->SessionSpinlock); AssertRC(rc2);
    rc2 = RTSemFastMutexDestroy(pDevExt->MemBalloon.hMtx); AssertRC(rc2);

    vgdrvDeleteWaitList(&pDevExt->WaitList);
#ifdef VBOX_WITH_HGCM
    vgdrvDeleteWaitList(&pDevExt->HGCMWaitList);
#endif
#ifdef VBOXGUEST_USE_DEFERRED_WAKE_UP
    vgdrvDeleteWaitList(&pDevExt->WakeUpList);
#endif
    vgdrvDeleteWaitList(&pDevExt->WokenUpList);
    vgdrvDeleteWaitList(&pDevExt->FreeList);

    VbglR0TerminatePrimary();

    pDevExt->pVMMDevMemory = NULL;

    pDevExt->IOPortBase = 0;
    pDevExt->pIrqAckEvents = NULL;

#ifdef VBOX_GUESTDRV_WITH_RELEASE_LOGGER
    RTLogDestroy(RTLogRelSetDefaultInstance(NULL));
    RTLogDestroy(RTLogSetDefaultInstance(NULL));
#endif

}


/**
 * Creates a VBoxGuest user session.
 *
 * The native code calls this when a ring-3 client opens the device.
 * Use VGDrvCommonCreateKernelSession when a ring-0 client connects.
 *
 * @returns VBox status code.
 * @param   pDevExt         The device extension.
 * @param   ppSession       Where to store the session on success.
 */
int VGDrvCommonCreateUserSession(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION *ppSession)
{
    PVBOXGUESTSESSION pSession = (PVBOXGUESTSESSION)RTMemAllocZ(sizeof(*pSession));
    if (RT_UNLIKELY(!pSession))
    {
        LogRel(("VGDrvCommonCreateUserSession: no memory!\n"));
        return VERR_NO_MEMORY;
    }

    pSession->Process = RTProcSelf();
    pSession->R0Process = RTR0ProcHandleSelf();
    pSession->pDevExt = pDevExt;
    RTSpinlockAcquire(pDevExt->SessionSpinlock);
    RTListAppend(&pDevExt->SessionList, &pSession->ListNode);
    pDevExt->cSessions++;
    RTSpinlockRelease(pDevExt->SessionSpinlock);

    *ppSession = pSession;
    LogFlow(("VGDrvCommonCreateUserSession: pSession=%p proc=%RTproc (%d) r0proc=%p\n",
             pSession, pSession->Process, (int)pSession->Process, (uintptr_t)pSession->R0Process)); /** @todo %RTr0proc */
    return VINF_SUCCESS;
}


/**
 * Creates a VBoxGuest kernel session.
 *
 * The native code calls this when a ring-0 client connects to the device.
 * Use VGDrvCommonCreateUserSession when a ring-3 client opens the device.
 *
 * @returns VBox status code.
 * @param   pDevExt         The device extension.
 * @param   ppSession       Where to store the session on success.
 */
int VGDrvCommonCreateKernelSession(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION *ppSession)
{
    PVBOXGUESTSESSION pSession = (PVBOXGUESTSESSION)RTMemAllocZ(sizeof(*pSession));
    if (RT_UNLIKELY(!pSession))
    {
        LogRel(("VGDrvCommonCreateKernelSession: no memory!\n"));
        return VERR_NO_MEMORY;
    }

    pSession->Process = NIL_RTPROCESS;
    pSession->R0Process = NIL_RTR0PROCESS;
    pSession->pDevExt = pDevExt;
    RTSpinlockAcquire(pDevExt->SessionSpinlock);
    RTListAppend(&pDevExt->SessionList, &pSession->ListNode);
    pDevExt->cSessions++;
    RTSpinlockRelease(pDevExt->SessionSpinlock);

    *ppSession = pSession;
    LogFlow(("VGDrvCommonCreateKernelSession: pSession=%p proc=%RTproc (%d) r0proc=%p\n",
             pSession, pSession->Process, (int)pSession->Process, (uintptr_t)pSession->R0Process)); /** @todo %RTr0proc */
    return VINF_SUCCESS;
}


/**
 * Closes a VBoxGuest session.
 *
 * @param   pDevExt         The device extension.
 * @param   pSession        The session to close (and free).
 */
void VGDrvCommonCloseSession(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession)
{
#ifdef VBOX_WITH_HGCM
    unsigned i;
#endif
    LogFlow(("VGDrvCommonCloseSession: pSession=%p proc=%RTproc (%d) r0proc=%p\n",
             pSession, pSession->Process, (int)pSession->Process, (uintptr_t)pSession->R0Process)); /** @todo %RTr0proc */

    RTSpinlockAcquire(pDevExt->SessionSpinlock);
    RTListNodeRemove(&pSession->ListNode);
    pDevExt->cSessions--;
    RTSpinlockRelease(pDevExt->SessionSpinlock);
    vgdrvAcquireSessionCapabilities(pDevExt, pSession, 0, UINT32_MAX, VBGL_IOC_AGC_FLAGS_DEFAULT, true /*fSessionTermination*/);
    vgdrvSetSessionCapabilities(pDevExt, pSession, 0 /*fOrMask*/, UINT32_MAX /*fNotMask*/,
                                NULL /*pfSessionCaps*/, NULL /*pfGlobalCaps*/, true /*fSessionTermination*/);
    vgdrvSetSessionEventFilter(pDevExt, pSession, 0 /*fOrMask*/, UINT32_MAX /*fNotMask*/, true /*fSessionTermination*/);
    vgdrvSetSessionMouseStatus(pDevExt, pSession, 0 /*fOrMask*/, UINT32_MAX /*fNotMask*/, true /*fSessionTermination*/);

    vgdrvIoCtl_CancelAllWaitEvents(pDevExt, pSession);

#ifdef VBOX_WITH_HGCM
    for (i = 0; i < RT_ELEMENTS(pSession->aHGCMClientIds); i++)
        if (pSession->aHGCMClientIds[i])
        {
            uint32_t idClient = pSession->aHGCMClientIds[i];
            pSession->aHGCMClientIds[i] = 0;
            Log(("VGDrvCommonCloseSession: disconnecting client id %#RX32\n", idClient));
            VbglR0HGCMInternalDisconnect(idClient, vgdrvHgcmAsyncWaitCallback, pDevExt, RT_INDEFINITE_WAIT);
        }
#endif

    pSession->pDevExt = NULL;
    pSession->Process = NIL_RTPROCESS;
    pSession->R0Process = NIL_RTR0PROCESS;
    vgdrvCloseMemBalloon(pDevExt, pSession);
    RTMemFree(pSession);
}


/**
 * Allocates a wait-for-event entry.
 *
 * @returns The wait-for-event entry.
 * @param   pDevExt         The device extension.
 * @param   pSession        The session that's allocating this. Can be NULL.
 */
static PVBOXGUESTWAIT vgdrvWaitAlloc(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession)
{
    /*
     * Allocate it one way or the other.
     */
    PVBOXGUESTWAIT pWait = RTListGetFirst(&pDevExt->FreeList, VBOXGUESTWAIT, ListNode);
    if (pWait)
    {
        RTSpinlockAcquire(pDevExt->EventSpinlock);

        pWait = RTListGetFirst(&pDevExt->FreeList, VBOXGUESTWAIT, ListNode);
        if (pWait)
            RTListNodeRemove(&pWait->ListNode);

        RTSpinlockRelease(pDevExt->EventSpinlock);
    }
    if (!pWait)
    {
        int rc;

        pWait = (PVBOXGUESTWAIT)RTMemAlloc(sizeof(*pWait));
        if (!pWait)
        {
            LogRelMax(32, ("vgdrvWaitAlloc: out-of-memory!\n"));
            return NULL;
        }

        rc = RTSemEventMultiCreate(&pWait->Event);
        if (RT_FAILURE(rc))
        {
            LogRelMax(32, ("vgdrvWaitAlloc: RTSemEventMultiCreate failed with rc=%Rrc!\n", rc));
            RTMemFree(pWait);
            return NULL;
        }

        pWait->ListNode.pNext = NULL;
        pWait->ListNode.pPrev = NULL;
    }

    /*
     * Zero members just as an precaution.
     */
    pWait->fReqEvents = 0;
    pWait->fResEvents = 0;
#ifdef VBOXGUEST_USE_DEFERRED_WAKE_UP
    pWait->fPendingWakeUp = false;
    pWait->fFreeMe = false;
#endif
    pWait->pSession = pSession;
#ifdef VBOX_WITH_HGCM
    pWait->pHGCMReq = NULL;
#endif
    RTSemEventMultiReset(pWait->Event);
    return pWait;
}


/**
 * Frees the wait-for-event entry.
 *
 * The caller must own the wait spinlock !
 * The entry must be in a list!
 *
 * @param   pDevExt         The device extension.
 * @param   pWait           The wait-for-event entry to free.
 */
static void vgdrvWaitFreeLocked(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTWAIT pWait)
{
    pWait->fReqEvents = 0;
    pWait->fResEvents = 0;
#ifdef VBOX_WITH_HGCM
    pWait->pHGCMReq = NULL;
#endif
#ifdef VBOXGUEST_USE_DEFERRED_WAKE_UP
    Assert(!pWait->fFreeMe);
    if (pWait->fPendingWakeUp)
        pWait->fFreeMe = true;
    else
#endif
    {
        RTListNodeRemove(&pWait->ListNode);
        RTListAppend(&pDevExt->FreeList, &pWait->ListNode);
    }
}


/**
 * Frees the wait-for-event entry.
 *
 * @param   pDevExt         The device extension.
 * @param   pWait           The wait-for-event entry to free.
 */
static void vgdrvWaitFreeUnlocked(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTWAIT pWait)
{
    RTSpinlockAcquire(pDevExt->EventSpinlock);
    vgdrvWaitFreeLocked(pDevExt, pWait);
    RTSpinlockRelease(pDevExt->EventSpinlock);
}


#ifdef VBOXGUEST_USE_DEFERRED_WAKE_UP
/**
 * Processes the wake-up list.
 *
 * All entries in the wake-up list gets signalled and moved to the woken-up
 * list.
 * At least on Windows this function can be invoked concurrently from
 * different VCPUs. So, be thread-safe.
 *
 * @param   pDevExt         The device extension.
 */
void VGDrvCommonWaitDoWakeUps(PVBOXGUESTDEVEXT pDevExt)
{
    if (!RTListIsEmpty(&pDevExt->WakeUpList))
    {
        RTSpinlockAcquire(pDevExt->EventSpinlock);
        for (;;)
        {
            int            rc;
            PVBOXGUESTWAIT pWait = RTListGetFirst(&pDevExt->WakeUpList, VBOXGUESTWAIT, ListNode);
            if (!pWait)
                break;
            /* Prevent other threads from accessing pWait when spinlock is released. */
            RTListNodeRemove(&pWait->ListNode);

            pWait->fPendingWakeUp = true;
            RTSpinlockRelease(pDevExt->EventSpinlock);

            rc = RTSemEventMultiSignal(pWait->Event);
            AssertRC(rc);

            RTSpinlockAcquire(pDevExt->EventSpinlock);
            Assert(pWait->ListNode.pNext == NULL && pWait->ListNode.pPrev == NULL);
            RTListAppend(&pDevExt->WokenUpList, &pWait->ListNode);
            pWait->fPendingWakeUp = false;
            if (RT_LIKELY(!pWait->fFreeMe))
            { /* likely */ }
            else
            {
                pWait->fFreeMe = false;
                vgdrvWaitFreeLocked(pDevExt, pWait);
            }
        }
        RTSpinlockRelease(pDevExt->EventSpinlock);
    }
}
#endif /* VBOXGUEST_USE_DEFERRED_WAKE_UP */


/**
 * Implements the fast (no input or output) type of IOCtls.
 *
 * This is currently just a placeholder stub inherited from the support driver code.
 *
 * @returns VBox status code.
 * @param   iFunction   The IOCtl function number.
 * @param   pDevExt     The device extension.
 * @param   pSession    The session.
 */
int VGDrvCommonIoCtlFast(uintptr_t iFunction, PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession)
{
    LogFlow(("VGDrvCommonIoCtlFast: iFunction=%#x pDevExt=%p pSession=%p\n", iFunction, pDevExt, pSession));

    NOREF(iFunction);
    NOREF(pDevExt);
    NOREF(pSession);
    return VERR_NOT_SUPPORTED;
}


/**
 * Gets the driver I/O control interface version, maybe adjusting it for
 * backwards compatibility.
 *
 * The adjusting is currently not implemented as we only have one major I/O
 * control interface version out there to support.  This is something we will
 * implement as needed.
 *
 * returns IPRT status code.
 * @param   pDevExt         The device extension.
 * @param   pSession        The session.
 * @param   pReq            The request info.
 */
static int vgdrvIoCtl_DriverVersionInfo(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession, PVBGLIOCDRIVERVERSIONINFO pReq)
{
    int rc;
    LogFlow(("VBGL_IOCTL_DRIVER_VERSION_INFO: uReqVersion=%#x uMinVersion=%#x uReserved1=%#x uReserved2=%#x\n",
             pReq->u.In.uReqVersion, pReq->u.In.uMinVersion, pReq->u.In.uReserved1, pReq->u.In.uReserved2));
    RT_NOREF2(pDevExt, pSession);

    /*
     * Input validation.
     */
    if (   pReq->u.In.uMinVersion <= pReq->u.In.uReqVersion
        && RT_HI_U16(pReq->u.In.uMinVersion) == RT_HI_U16(pReq->u.In.uReqVersion))
    {
        /*
         * Match the version.
         * The current logic is very simple, match the major interface version.
         */
        if (   pReq->u.In.uMinVersion <= VBGL_IOC_VERSION
            && RT_HI_U16(pReq->u.In.uMinVersion) == RT_HI_U16(VBGL_IOC_VERSION))
            rc = VINF_SUCCESS;
        else
        {
            LogRel(("VBGL_IOCTL_DRIVER_VERSION_INFO: Version mismatch. Requested: %#x  Min: %#x  Current: %#x\n",
                    pReq->u.In.uReqVersion, pReq->u.In.uMinVersion, VBGL_IOC_VERSION));
            rc = VERR_VERSION_MISMATCH;
        }
    }
    else
    {
        LogRel(("VBGL_IOCTL_DRIVER_VERSION_INFO: uMinVersion=%#x uMaxVersion=%#x doesn't match!\n",
                pReq->u.In.uMinVersion, pReq->u.In.uReqVersion));
        rc = VERR_INVALID_PARAMETER;
    }

    pReq->u.Out.uSessionVersion = RT_SUCCESS(rc) ? VBGL_IOC_VERSION : UINT32_MAX;
    pReq->u.Out.uDriverVersion  = VBGL_IOC_VERSION;
    pReq->u.Out.uDriverRevision = VBOX_SVN_REV;
    pReq->u.Out.uReserved1      = 0;
    pReq->u.Out.uReserved2      = 0;
    return rc;
}


/**
 * Similar to vgdrvIoCtl_DriverVersionInfo, except its for IDC.
 *
 * returns IPRT status code.
 * @param   pDevExt         The device extension.
 * @param   pSession        The session.
 * @param   pReq            The request info.
 */
static int vgdrvIoCtl_IdcConnect(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession, PVBGLIOCIDCCONNECT pReq)
{
    int rc;
    LogFlow(("VBGL_IOCTL_IDC_CONNECT: u32MagicCookie=%#x uReqVersion=%#x uMinVersion=%#x uReserved=%#x\n",
             pReq->u.In.u32MagicCookie, pReq->u.In.uReqVersion, pReq->u.In.uMinVersion, pReq->u.In.uReserved));
    Assert(pSession != NULL);
    RT_NOREF(pDevExt);

    /*
     * Input validation.
     */
    if (pReq->u.In.u32MagicCookie == VBGL_IOCTL_IDC_CONNECT_MAGIC_COOKIE)
    {
        if (   pReq->u.In.uMinVersion <= pReq->u.In.uReqVersion
            && RT_HI_U16(pReq->u.In.uMinVersion) == RT_HI_U16(pReq->u.In.uReqVersion))
        {
            /*
             * Match the version.
             * The current logic is very simple, match the major interface version.
             */
            if (   pReq->u.In.uMinVersion <= VBGL_IOC_VERSION
                && RT_HI_U16(pReq->u.In.uMinVersion) == RT_HI_U16(VBGL_IOC_VERSION))
            {
                pReq->u.Out.pvSession       = pSession;
                pReq->u.Out.uSessionVersion = VBGL_IOC_VERSION;
                pReq->u.Out.uDriverVersion  = VBGL_IOC_VERSION;
                pReq->u.Out.uDriverRevision = VBOX_SVN_REV;
                pReq->u.Out.uReserved1      = 0;
                pReq->u.Out.pvReserved2     = NULL;
                return VINF_SUCCESS;

            }
            LogRel(("VBGL_IOCTL_IDC_CONNECT: Version mismatch. Requested: %#x  Min: %#x  Current: %#x\n",
                    pReq->u.In.uReqVersion, pReq->u.In.uMinVersion, VBGL_IOC_VERSION));
            rc = VERR_VERSION_MISMATCH;
        }
        else
        {
            LogRel(("VBGL_IOCTL_IDC_CONNECT: uMinVersion=%#x uMaxVersion=%#x doesn't match!\n",
                    pReq->u.In.uMinVersion, pReq->u.In.uReqVersion));
            rc = VERR_INVALID_PARAMETER;
        }

        pReq->u.Out.pvSession       = NULL;
        pReq->u.Out.uSessionVersion = UINT32_MAX;
        pReq->u.Out.uDriverVersion  = VBGL_IOC_VERSION;
        pReq->u.Out.uDriverRevision = VBOX_SVN_REV;
        pReq->u.Out.uReserved1      = 0;
        pReq->u.Out.pvReserved2     = NULL;
    }
    else
    {
        LogRel(("VBGL_IOCTL_IDC_CONNECT: u32MagicCookie=%#x expected %#x!\n",
                pReq->u.In.u32MagicCookie, VBGL_IOCTL_IDC_CONNECT_MAGIC_COOKIE));
        rc = VERR_INVALID_PARAMETER;
    }
    return rc;
}


/**
 * Counterpart to vgdrvIoCtl_IdcConnect, destroys the session.
 *
 * returns IPRT status code.
 * @param   pDevExt         The device extension.
 * @param   pSession        The session.
 * @param   pReq            The request info.
 */
static int vgdrvIoCtl_IdcDisconnect(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession, PVBGLIOCIDCDISCONNECT pReq)
{
    LogFlow(("VBGL_IOCTL_IDC_DISCONNECT: pvSession=%p vs pSession=%p\n", pReq->u.In.pvSession, pSession));
    RT_NOREF(pDevExt);
    Assert(pSession != NULL);

    if (pReq->u.In.pvSession == pSession)
    {
        VGDrvCommonCloseSession(pDevExt, pSession);
        return VINF_SUCCESS;
    }
    LogRel(("VBGL_IOCTL_IDC_DISCONNECT: In.pvSession=%p is not equal to pSession=%p!\n", pReq->u.In.pvSession, pSession));
    return VERR_INVALID_PARAMETER;
}


/**
 * Return the VMM device I/O info.
 *
 * returns IPRT status code.
 * @param   pDevExt         The device extension.
 * @param   pInfo           The request info.
 * @note    Ring-0 only, caller checked.
 */
static int vgdrvIoCtl_GetVMMDevIoInfo(PVBOXGUESTDEVEXT pDevExt, PVBGLIOCGETVMMDEVIOINFO pInfo)
{
    LogFlow(("VBGL_IOCTL_GET_VMMDEV_IO_INFO\n"));

    pInfo->u.Out.IoPort          = pDevExt->IOPortBase;
    pInfo->u.Out.pvVmmDevMapping = pDevExt->pVMMDevMemory;
    pInfo->u.Out.auPadding[0]    = 0;
#if HC_ARCH_BITS != 32
    pInfo->u.Out.auPadding[1]    = 0;
    pInfo->u.Out.auPadding[2]    = 0;
#endif
    return VINF_SUCCESS;
}


/**
 * Set the callback for the kernel mouse handler.
 *
 * returns IPRT status code.
 * @param   pDevExt         The device extension.
 * @param   pNotify         The new callback information.
 */
int vgdrvIoCtl_SetMouseNotifyCallback(PVBOXGUESTDEVEXT pDevExt, PVBGLIOCSETMOUSENOTIFYCALLBACK pNotify)
{
    LogFlow(("VBOXGUEST_IOCTL_SET_MOUSE_NOTIFY_CALLBACK: pfnNotify=%p pvUser=%p\n", pNotify->u.In.pfnNotify, pNotify->u.In.pvUser));

#ifdef VBOXGUEST_MOUSE_NOTIFY_CAN_PREEMPT
    VGDrvNativeSetMouseNotifyCallback(pDevExt, pNotify);
#else
    RTSpinlockAcquire(pDevExt->EventSpinlock);
    pDevExt->pfnMouseNotifyCallback   = pNotify->u.In.pfnNotify;
    pDevExt->pvMouseNotifyCallbackArg = pNotify->u.In.pvUser;
    RTSpinlockRelease(pDevExt->EventSpinlock);
#endif
    return VINF_SUCCESS;
}


/**
 * Worker vgdrvIoCtl_WaitEvent.
 *
 * The caller enters the spinlock, we leave it.
 *
 * @returns VINF_SUCCESS if we've left the spinlock and can return immediately.
 */
DECLINLINE(int) vbdgCheckWaitEventCondition(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession,
                                            PVBGLIOCWAITFOREVENTS pInfo, int iEvent, const uint32_t fReqEvents)
{
    uint32_t fMatches = pDevExt->f32PendingEvents & fReqEvents;
    if (fMatches & VBOXGUEST_ACQUIRE_STYLE_EVENTS)
        fMatches &= vgdrvGetAllowedEventMaskForSession(pDevExt, pSession);
    if (fMatches || pSession->fPendingCancelWaitEvents)
    {
        ASMAtomicAndU32(&pDevExt->f32PendingEvents, ~fMatches);
        RTSpinlockRelease(pDevExt->EventSpinlock);

        pInfo->u.Out.fEvents = fMatches;
        if (fReqEvents & ~((uint32_t)1 << iEvent))
            LogFlow(("VBOXGUEST_IOCTL_WAITEVENT: returns %#x\n", pInfo->u.Out.fEvents));
        else
            LogFlow(("VBOXGUEST_IOCTL_WAITEVENT: returns %#x/%d\n", pInfo->u.Out.fEvents, iEvent));
        pSession->fPendingCancelWaitEvents = false;
        return VINF_SUCCESS;
    }

    RTSpinlockRelease(pDevExt->EventSpinlock);
    return VERR_TIMEOUT;
}


static int vgdrvIoCtl_WaitForEvents(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession,
                                    PVBGLIOCWAITFOREVENTS pInfo, bool fInterruptible)
{
    uint32_t const  cMsTimeout = pInfo->u.In.cMsTimeOut;
    const uint32_t  fReqEvents = pInfo->u.In.fEvents;
    uint32_t        fResEvents;
    int             iEvent;
    PVBOXGUESTWAIT  pWait;
    int             rc;

    pInfo->u.Out.fEvents = 0;           /* Note! This overwrites pInfo->u.In.* fields!  */

    /*
     * Copy and verify the input mask.
     */
    iEvent = ASMBitFirstSetU32(fReqEvents) - 1;
    if (RT_UNLIKELY(iEvent < 0))
    {
        LogRel(("VBOXGUEST_IOCTL_WAITEVENT: Invalid input mask %#x!!\n", fReqEvents));
        return VERR_INVALID_PARAMETER;
    }

    /*
     * Check the condition up front, before doing the wait-for-event allocations.
     */
    RTSpinlockAcquire(pDevExt->EventSpinlock);
    rc = vbdgCheckWaitEventCondition(pDevExt, pSession, pInfo, iEvent, fReqEvents);
    if (rc == VINF_SUCCESS)
        return rc;

    if (!cMsTimeout)
    {
        LogFlow(("VBOXGUEST_IOCTL_WAITEVENT: returns VERR_TIMEOUT\n"));
        return VERR_TIMEOUT;
    }

    pWait = vgdrvWaitAlloc(pDevExt, pSession);
    if (!pWait)
        return VERR_NO_MEMORY;
    pWait->fReqEvents = fReqEvents;

    /*
     * We've got the wait entry now, re-enter the spinlock and check for the condition.
     * If the wait condition is met, return.
     * Otherwise enter into the list and go to sleep waiting for the ISR to signal us.
     */
    RTSpinlockAcquire(pDevExt->EventSpinlock);
    RTListAppend(&pDevExt->WaitList, &pWait->ListNode);
    rc = vbdgCheckWaitEventCondition(pDevExt, pSession, pInfo, iEvent, fReqEvents);
    if (rc == VINF_SUCCESS)
    {
        vgdrvWaitFreeUnlocked(pDevExt, pWait);
        return rc;
    }

    if (fInterruptible)
        rc = RTSemEventMultiWaitNoResume(pWait->Event, cMsTimeout == UINT32_MAX ? RT_INDEFINITE_WAIT : cMsTimeout);
    else
        rc = RTSemEventMultiWait(pWait->Event, cMsTimeout == UINT32_MAX ? RT_INDEFINITE_WAIT : cMsTimeout);

    /*
     * There is one special case here and that's when the semaphore is
     * destroyed upon device driver unload. This shouldn't happen of course,
     * but in case it does, just get out of here ASAP.
     */
    if (rc == VERR_SEM_DESTROYED)
        return rc;

    /*
     * Unlink the wait item and dispose of it.
     */
    RTSpinlockAcquire(pDevExt->EventSpinlock);
    fResEvents = pWait->fResEvents;
    vgdrvWaitFreeLocked(pDevExt, pWait);
    RTSpinlockRelease(pDevExt->EventSpinlock);

    /*
     * Now deal with the return code.
     */
    if (   fResEvents
        && fResEvents != UINT32_MAX)
    {
        pInfo->u.Out.fEvents = fResEvents;
        if (fReqEvents & ~((uint32_t)1 << iEvent))
            LogFlow(("VBOXGUEST_IOCTL_WAITEVENT: returns %#x\n", pInfo->u.Out.fEvents));
        else
            LogFlow(("VBOXGUEST_IOCTL_WAITEVENT: returns %#x/%d\n", pInfo->u.Out.fEvents, iEvent));
        rc = VINF_SUCCESS;
    }
    else if (   fResEvents == UINT32_MAX
             || rc == VERR_INTERRUPTED)
    {
        rc = VERR_INTERRUPTED;
        LogFlow(("VBOXGUEST_IOCTL_WAITEVENT: returns VERR_INTERRUPTED\n"));
    }
    else if (rc == VERR_TIMEOUT)
        LogFlow(("VBOXGUEST_IOCTL_WAITEVENT: returns VERR_TIMEOUT (2)\n"));
    else
    {
        if (RT_SUCCESS(rc))
        {
            LogRelMax(32, ("VBOXGUEST_IOCTL_WAITEVENT: returns %Rrc but no events!\n", rc));
            rc = VERR_INTERNAL_ERROR;
        }
        LogFlow(("VBOXGUEST_IOCTL_WAITEVENT: returns %Rrc\n", rc));
    }

    return rc;
}


/** @todo the semantics of this IoCtl have been tightened, so that no calls to
 *  VBOXGUEST_IOCTL_WAITEVENT are allowed in a session after it has been
 *  called.  Change the code to make calls to VBOXGUEST_IOCTL_WAITEVENT made
 *  after that to return VERR_INTERRUPTED or something appropriate. */
static int vgdrvIoCtl_CancelAllWaitEvents(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession)
{
    PVBOXGUESTWAIT          pWait;
    PVBOXGUESTWAIT          pSafe;
    int                     rc = 0;
    /* Was as least one WAITEVENT in process for this session?  If not we
     * set a flag that the next call should be interrupted immediately.  This
     * is needed so that a user thread can reliably interrupt another one in a
     * WAITEVENT loop. */
    bool                    fCancelledOne = false;

    LogFlow(("VBOXGUEST_IOCTL_CANCEL_ALL_WAITEVENTS\n"));

    /*
     * Walk the event list and wake up anyone with a matching session.
     */
    RTSpinlockAcquire(pDevExt->EventSpinlock);
    RTListForEachSafe(&pDevExt->WaitList, pWait, pSafe, VBOXGUESTWAIT, ListNode)
    {
        if (pWait->pSession == pSession)
        {
            fCancelledOne = true;
            pWait->fResEvents = UINT32_MAX;
            RTListNodeRemove(&pWait->ListNode);
#ifdef VBOXGUEST_USE_DEFERRED_WAKE_UP
            RTListAppend(&pDevExt->WakeUpList, &pWait->ListNode);
#else
            rc |= RTSemEventMultiSignal(pWait->Event);
            RTListAppend(&pDevExt->WokenUpList, &pWait->ListNode);
#endif
        }
    }
    if (!fCancelledOne)
        pSession->fPendingCancelWaitEvents = true;
    RTSpinlockRelease(pDevExt->EventSpinlock);
    Assert(rc == 0);
    NOREF(rc);

#ifdef VBOXGUEST_USE_DEFERRED_WAKE_UP
    VGDrvCommonWaitDoWakeUps(pDevExt);
#endif

    return VINF_SUCCESS;
}


/**
 * Checks if the VMM request is allowed in the context of the given session.
 *
 * @returns VINF_SUCCESS or VERR_PERMISSION_DENIED.
 * @param   pDevExt             The device extension.
 * @param   pSession            The calling session.
 * @param   enmType             The request type.
 * @param   pReqHdr             The request.
 */
static int vgdrvCheckIfVmmReqIsAllowed(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession, VMMDevRequestType enmType,
                                       VMMDevRequestHeader const *pReqHdr)
{
    /*
     * Categorize the request being made.
     */
    /** @todo This need quite some more work! */
    enum
    {
        kLevel_Invalid, kLevel_NoOne, kLevel_OnlyVBoxGuest, kLevel_OnlyKernel, kLevel_TrustedUsers, kLevel_AllUsers
    } enmRequired;
    RT_NOREF1(pDevExt);

    switch (enmType)
    {
        /*
         * Deny access to anything we don't know or provide specialized I/O controls for.
         */
#ifdef VBOX_WITH_HGCM
        case VMMDevReq_HGCMConnect:
        case VMMDevReq_HGCMDisconnect:
# ifdef VBOX_WITH_64_BITS_GUESTS
        case VMMDevReq_HGCMCall32:
        case VMMDevReq_HGCMCall64:
# else
        case VMMDevReq_HGCMCall:
# endif /* VBOX_WITH_64_BITS_GUESTS */
        case VMMDevReq_HGCMCancel:
        case VMMDevReq_HGCMCancel2:
#endif /* VBOX_WITH_HGCM */
        case VMMDevReq_SetGuestCapabilities:
        default:
            enmRequired = kLevel_NoOne;
            break;

        /*
         * There are a few things only this driver can do (and it doesn't use
         * the VMMRequst I/O control route anyway, but whatever).
         */
        case VMMDevReq_ReportGuestInfo:
        case VMMDevReq_ReportGuestInfo2:
        case VMMDevReq_GetHypervisorInfo:
        case VMMDevReq_SetHypervisorInfo:
        case VMMDevReq_RegisterPatchMemory:
        case VMMDevReq_DeregisterPatchMemory:
        case VMMDevReq_GetMemBalloonChangeRequest:
            enmRequired = kLevel_OnlyVBoxGuest;
            break;

        /*
         * Trusted users apps only.
         */
        case VMMDevReq_QueryCredentials:
        case VMMDevReq_ReportCredentialsJudgement:
        case VMMDevReq_RegisterSharedModule:
        case VMMDevReq_UnregisterSharedModule:
        case VMMDevReq_WriteCoreDump:
        case VMMDevReq_GetCpuHotPlugRequest:
        case VMMDevReq_SetCpuHotPlugStatus:
        case VMMDevReq_CheckSharedModules:
        case VMMDevReq_GetPageSharingStatus:
        case VMMDevReq_DebugIsPageShared:
        case VMMDevReq_ReportGuestStats:
        case VMMDevReq_ReportGuestUserState:
        case VMMDevReq_GetStatisticsChangeRequest:
        case VMMDevReq_ChangeMemBalloon:
            enmRequired = kLevel_TrustedUsers;
            break;

        /*
         * Anyone.
         */
        case VMMDevReq_GetMouseStatus:
        case VMMDevReq_SetMouseStatus:
        case VMMDevReq_SetPointerShape:
        case VMMDevReq_GetHostVersion:
        case VMMDevReq_Idle:
        case VMMDevReq_GetHostTime:
        case VMMDevReq_SetPowerStatus:
        case VMMDevReq_AcknowledgeEvents:
        case VMMDevReq_CtlGuestFilterMask:
        case VMMDevReq_ReportGuestStatus:
        case VMMDevReq_GetDisplayChangeRequest:
        case VMMDevReq_VideoModeSupported:
        case VMMDevReq_GetHeightReduction:
        case VMMDevReq_GetDisplayChangeRequest2:
        case VMMDevReq_VideoModeSupported2:
        case VMMDevReq_VideoAccelEnable:
        case VMMDevReq_VideoAccelFlush:
        case VMMDevReq_VideoSetVisibleRegion:
        case VMMDevReq_GetDisplayChangeRequestEx:
        case VMMDevReq_GetSeamlessChangeRequest:
        case VMMDevReq_GetVRDPChangeRequest:
        case VMMDevReq_LogString:
        case VMMDevReq_GetSessionId:
            enmRequired = kLevel_AllUsers;
            break;

        /*
         * Depends on the request parameters...
         */
        /** @todo this have to be changed into an I/O control and the facilities
         *        tracked in the session so they can automatically be failed when the
         *        session terminates without reporting the new status.
         *
         *  The information presented by IGuest is not reliable without this! */
        case VMMDevReq_ReportGuestCapabilities:
            switch (((VMMDevReportGuestStatus const *)pReqHdr)->guestStatus.facility)
            {
                case VBoxGuestFacilityType_All:
                case VBoxGuestFacilityType_VBoxGuestDriver:
                    enmRequired = kLevel_OnlyVBoxGuest;
                    break;
                case VBoxGuestFacilityType_VBoxService:
                    enmRequired = kLevel_TrustedUsers;
                    break;
                case VBoxGuestFacilityType_VBoxTrayClient:
                case VBoxGuestFacilityType_Seamless:
                case VBoxGuestFacilityType_Graphics:
                default:
                    enmRequired = kLevel_AllUsers;
                    break;
            }
            break;
    }

    /*
     * Check against the session.
     */
    switch (enmRequired)
    {
        default:
        case kLevel_NoOne:
            break;
        case kLevel_OnlyVBoxGuest:
        case kLevel_OnlyKernel:
            if (pSession->R0Process == NIL_RTR0PROCESS)
                return VINF_SUCCESS;
            break;
        case kLevel_TrustedUsers:
            if (pSession->fUserSession)
                break;
        case kLevel_AllUsers:
            return VINF_SUCCESS;
    }

    return VERR_PERMISSION_DENIED;
}

static int vgdrvIoCtl_VMMDevRequest(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession,
                                    VMMDevRequestHeader *pReqHdr, size_t cbData)
{
    int                     rc;
    VMMDevRequestHeader    *pReqCopy;

    /*
     * Validate the header and request size.
     */
    const VMMDevRequestType enmType   = pReqHdr->requestType;
    const uint32_t          cbReq     = pReqHdr->size;
    const uint32_t          cbMinSize = (uint32_t)vmmdevGetRequestSize(enmType);

    LogFlow(("VBOXGUEST_IOCTL_VMMREQUEST: type %d\n", pReqHdr->requestType));

    if (cbReq < cbMinSize)
    {
        LogRel(("VBOXGUEST_IOCTL_VMMREQUEST: invalid hdr size %#x, expected >= %#x; type=%#x!!\n",
                cbReq, cbMinSize, enmType));
        return VERR_INVALID_PARAMETER;
    }
    if (cbReq > cbData)
    {
        LogRel(("VBOXGUEST_IOCTL_VMMREQUEST: invalid size %#x, expected >= %#x (hdr); type=%#x!!\n",
                cbData, cbReq, enmType));
        return VERR_INVALID_PARAMETER;
    }
    rc = VbglGR0Verify(pReqHdr, cbData);
    if (RT_FAILURE(rc))
    {
        Log(("VBOXGUEST_IOCTL_VMMREQUEST: invalid header: size %#x, expected >= %#x (hdr); type=%#x; rc=%Rrc!!\n",
             cbData, cbReq, enmType, rc));
        return rc;
    }

    rc = vgdrvCheckIfVmmReqIsAllowed(pDevExt, pSession, enmType, pReqHdr);
    if (RT_FAILURE(rc))
    {
        Log(("VBOXGUEST_IOCTL_VMMREQUEST: Operation not allowed! type=%#x rc=%Rrc\n", enmType, rc));
        return rc;
    }

    /*
     * Make a copy of the request in the physical memory heap so
     * the VBoxGuestLibrary can more easily deal with the request.
     * (This is really a waste of time since the OS or the OS specific
     * code has already buffered or locked the input/output buffer, but
     * it does makes things a bit simpler wrt to phys address.)
     */
    rc = VbglR0GRAlloc(&pReqCopy, cbReq, enmType);
    if (RT_FAILURE(rc))
    {
        Log(("VBOXGUEST_IOCTL_VMMREQUEST: failed to allocate %u (%#x) bytes to cache the request. rc=%Rrc!!\n",
             cbReq, cbReq, rc));
        return rc;
    }
    memcpy(pReqCopy, pReqHdr, cbReq);
    Assert(pReqCopy->reserved1 == cbReq);
    pReqCopy->reserved1 = 0;            /* VGDrvCommonIoCtl or caller sets cbOut, so clear it. */

    if (enmType == VMMDevReq_GetMouseStatus) /* clear poll condition. */
        pSession->u32MousePosChangedSeq = ASMAtomicUoReadU32(&pDevExt->u32MousePosChangedSeq);

    rc = VbglR0GRPerform(pReqCopy);
    if (   RT_SUCCESS(rc)
        && RT_SUCCESS(pReqCopy->rc))
    {
        Assert(rc != VINF_HGCM_ASYNC_EXECUTE);
        Assert(pReqCopy->rc != VINF_HGCM_ASYNC_EXECUTE);

        memcpy(pReqHdr, pReqCopy, cbReq);
        pReqHdr->reserved1 = cbReq; /* preserve cbOut */
    }
    else if (RT_FAILURE(rc))
        Log(("VBOXGUEST_IOCTL_VMMREQUEST: VbglR0GRPerform - rc=%Rrc!\n", rc));
    else
    {
        Log(("VBOXGUEST_IOCTL_VMMREQUEST: request execution failed; VMMDev rc=%Rrc!\n", pReqCopy->rc));
        rc = pReqCopy->rc;
    }

    VbglR0GRFree(pReqCopy);
    return rc;
}


#ifdef VBOX_WITH_HGCM

AssertCompile(RT_INDEFINITE_WAIT == (uint32_t)RT_INDEFINITE_WAIT); /* assumed by code below */

/** Worker for vgdrvHgcmAsyncWaitCallback*. */
static int vgdrvHgcmAsyncWaitCallbackWorker(VMMDevHGCMRequestHeader volatile *pHdr, PVBOXGUESTDEVEXT pDevExt,
                                            bool fInterruptible, uint32_t cMillies)
{
    int rc;

    /*
     * Check to see if the condition was met by the time we got here.
     *
     * We create a simple poll loop here for dealing with out-of-memory
     * conditions since the caller isn't necessarily able to deal with
     * us returning too early.
     */
    PVBOXGUESTWAIT pWait;
    for (;;)
    {
        RTSpinlockAcquire(pDevExt->EventSpinlock);
        if ((pHdr->fu32Flags & VBOX_HGCM_REQ_DONE) != 0)
        {
            RTSpinlockRelease(pDevExt->EventSpinlock);
            return VINF_SUCCESS;
        }
        RTSpinlockRelease(pDevExt->EventSpinlock);

        pWait = vgdrvWaitAlloc(pDevExt, NULL);
        if (pWait)
            break;
        if (fInterruptible)
            return VERR_INTERRUPTED;
        RTThreadSleep(1);
    }
    pWait->fReqEvents = VMMDEV_EVENT_HGCM;
    pWait->pHGCMReq = pHdr;

    /*
     * Re-enter the spinlock and re-check for the condition.
     * If the condition is met, return.
     * Otherwise link us into the HGCM wait list and go to sleep.
     */
    RTSpinlockAcquire(pDevExt->EventSpinlock);
    RTListAppend(&pDevExt->HGCMWaitList, &pWait->ListNode);
    if ((pHdr->fu32Flags & VBOX_HGCM_REQ_DONE) != 0)
    {
        vgdrvWaitFreeLocked(pDevExt, pWait);
        RTSpinlockRelease(pDevExt->EventSpinlock);
        return VINF_SUCCESS;
    }
    RTSpinlockRelease(pDevExt->EventSpinlock);

    if (fInterruptible)
        rc = RTSemEventMultiWaitNoResume(pWait->Event, cMillies);
    else
        rc = RTSemEventMultiWait(pWait->Event, cMillies);
    if (rc == VERR_SEM_DESTROYED)
        return rc;

    /*
     * Unlink, free and return.
     */
    if (   RT_FAILURE(rc)
        && rc != VERR_TIMEOUT
        && (   !fInterruptible
            || rc != VERR_INTERRUPTED))
        LogRel(("vgdrvHgcmAsyncWaitCallback: wait failed! %Rrc\n", rc));

    vgdrvWaitFreeUnlocked(pDevExt, pWait);
    return rc;
}


/**
 * This is a callback for dealing with async waits.
 *
 * It operates in a manner similar to vgdrvIoCtl_WaitEvent.
 */
static DECLCALLBACK(int) vgdrvHgcmAsyncWaitCallback(VMMDevHGCMRequestHeader *pHdr, void *pvUser, uint32_t u32User)
{
    PVBOXGUESTDEVEXT pDevExt = (PVBOXGUESTDEVEXT)pvUser;
    LogFlow(("vgdrvHgcmAsyncWaitCallback: requestType=%d\n", pHdr->header.requestType));
    return vgdrvHgcmAsyncWaitCallbackWorker((VMMDevHGCMRequestHeader volatile *)pHdr, pDevExt,
                                            false /* fInterruptible */, u32User  /* cMillies */);
}


/**
 * This is a callback for dealing with async waits with a timeout.
 *
 * It operates in a manner similar to vgdrvIoCtl_WaitEvent.
 */
static DECLCALLBACK(int) vgdrvHgcmAsyncWaitCallbackInterruptible(VMMDevHGCMRequestHeader *pHdr, void *pvUser, uint32_t u32User)
{
    PVBOXGUESTDEVEXT pDevExt = (PVBOXGUESTDEVEXT)pvUser;
    LogFlow(("vgdrvHgcmAsyncWaitCallbackInterruptible: requestType=%d\n", pHdr->header.requestType));
    return vgdrvHgcmAsyncWaitCallbackWorker((VMMDevHGCMRequestHeader volatile *)pHdr, pDevExt,
                                            true /* fInterruptible */, u32User /* cMillies */);
}


static int vgdrvIoCtl_HGCMConnect(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession, PVBGLIOCHGCMCONNECT pInfo)
{
    int rc;
    HGCMCLIENTID idClient = 0;

    /*
     * The VbglHGCMConnect call will invoke the callback if the HGCM
     * call is performed in an ASYNC fashion. The function is not able
     * to deal with cancelled requests.
     */
    Log(("VBOXGUEST_IOCTL_HGCM_CONNECT: %.128s\n",
         pInfo->u.In.Loc.type == VMMDevHGCMLoc_LocalHost || pInfo->u.In.Loc.type == VMMDevHGCMLoc_LocalHost_Existing
         ? pInfo->u.In.Loc.u.host.achName : "<not local host>"));

    rc = VbglR0HGCMInternalConnect(&pInfo->u.In.Loc, &idClient, vgdrvHgcmAsyncWaitCallback, pDevExt, RT_INDEFINITE_WAIT);
    Log(("VBOXGUEST_IOCTL_HGCM_CONNECT: idClient=%RX32 (rc=%Rrc)\n", idClient, rc));
    if (RT_SUCCESS(rc))
    {
        /*
         * Append the client id to the client id table.
         * If the table has somehow become filled up, we'll disconnect the session.
         */
        unsigned i;
        RTSpinlockAcquire(pDevExt->SessionSpinlock);
        for (i = 0; i < RT_ELEMENTS(pSession->aHGCMClientIds); i++)
            if (!pSession->aHGCMClientIds[i])
            {
                pSession->aHGCMClientIds[i] = idClient;
                break;
            }
        RTSpinlockRelease(pDevExt->SessionSpinlock);
        if (i >= RT_ELEMENTS(pSession->aHGCMClientIds))
        {
            LogRelMax(32, ("VBOXGUEST_IOCTL_HGCM_CONNECT: too many HGCMConnect calls for one session!\n"));
            VbglR0HGCMInternalDisconnect(idClient, vgdrvHgcmAsyncWaitCallback, pDevExt, RT_INDEFINITE_WAIT);

            pInfo->u.Out.idClient = 0;
            return VERR_TOO_MANY_OPEN_FILES;
        }
    }
    pInfo->u.Out.idClient = idClient;
    return rc;
}


static int vgdrvIoCtl_HGCMDisconnect(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession, PVBGLIOCHGCMDISCONNECT pInfo)
{
    /*
     * Validate the client id and invalidate its entry while we're in the call.
     */
    int             rc;
    const uint32_t  idClient = pInfo->u.In.idClient;
    unsigned        i;
    RTSpinlockAcquire(pDevExt->SessionSpinlock);
    for (i = 0; i < RT_ELEMENTS(pSession->aHGCMClientIds); i++)
        if (pSession->aHGCMClientIds[i] == idClient)
        {
            pSession->aHGCMClientIds[i] = UINT32_MAX;
            break;
        }
    RTSpinlockRelease(pDevExt->SessionSpinlock);
    if (i >= RT_ELEMENTS(pSession->aHGCMClientIds))
    {
        LogRelMax(32, ("VBOXGUEST_IOCTL_HGCM_DISCONNECT: idClient=%RX32\n", idClient));
        return VERR_INVALID_HANDLE;
    }

    /*
     * The VbglHGCMConnect call will invoke the callback if the HGCM
     * call is performed in an ASYNC fashion. The function is not able
     * to deal with cancelled requests.
     */
    Log(("VBOXGUEST_IOCTL_HGCM_DISCONNECT: idClient=%RX32\n", idClient));
    rc = VbglR0HGCMInternalDisconnect(idClient, vgdrvHgcmAsyncWaitCallback, pDevExt, RT_INDEFINITE_WAIT);
    LogFlow(("VBOXGUEST_IOCTL_HGCM_DISCONNECT: rc=%Rrc\n", rc));

    /* Update the client id array according to the result. */
    RTSpinlockAcquire(pDevExt->SessionSpinlock);
    if (pSession->aHGCMClientIds[i] == UINT32_MAX)
        pSession->aHGCMClientIds[i] = RT_SUCCESS(rc) ? 0 : idClient;
    RTSpinlockRelease(pDevExt->SessionSpinlock);

    return rc;
}


static int vgdrvIoCtl_HGCMCallInner(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession, PVBGLIOCHGCMCALL pInfo,
                                    uint32_t cMillies, bool fInterruptible, bool f32bit, bool fUserData,
                                    size_t cbExtra, size_t cbData)
{
    const uint32_t  u32ClientId = pInfo->u32ClientID;
    uint32_t        fFlags;
    size_t          cbActual;
    unsigned        i;
    int             rc;

    /*
     * Some more validations.
     */
    if (pInfo->cParms > 4096) /* (Just make sure it doesn't overflow the next check.) */
    {
        LogRel(("VBOXGUEST_IOCTL_HGCM_CALL: cParm=%RX32 is not sane\n", pInfo->cParms));
        return VERR_INVALID_PARAMETER;
    }

    cbActual = cbExtra + sizeof(*pInfo);
#ifdef RT_ARCH_AMD64
    if (f32bit)
        cbActual += pInfo->cParms * sizeof(HGCMFunctionParameter32);
    else
#endif
        cbActual += pInfo->cParms * sizeof(HGCMFunctionParameter);
    if (cbData < cbActual)
    {
        LogRel(("VBOXGUEST_IOCTL_HGCM_CALL: cbData=%#zx (%zu) required size is %#zx (%zu)\n",
               cbData, cbData, cbActual, cbActual));
        return VERR_INVALID_PARAMETER;
    }
    pInfo->Hdr.cbOut = (uint32_t)cbActual;

    /*
     * Validate the client id.
     */
    RTSpinlockAcquire(pDevExt->SessionSpinlock);
    for (i = 0; i < RT_ELEMENTS(pSession->aHGCMClientIds); i++)
        if (pSession->aHGCMClientIds[i] == u32ClientId)
            break;
    RTSpinlockRelease(pDevExt->SessionSpinlock);
    if (RT_UNLIKELY(i >= RT_ELEMENTS(pSession->aHGCMClientIds)))
    {
        LogRelMax(32, ("VBOXGUEST_IOCTL_HGCM_CALL: Invalid handle. u32Client=%RX32\n", u32ClientId));
        return VERR_INVALID_HANDLE;
    }

    /*
     * The VbglHGCMCall call will invoke the callback if the HGCM
     * call is performed in an ASYNC fashion. This function can
     * deal with cancelled requests, so we let user more requests
     * be interruptible (should add a flag for this later I guess).
     */
    LogFlow(("VBOXGUEST_IOCTL_HGCM_CALL: u32Client=%RX32\n", pInfo->u32ClientID));
    fFlags = !fUserData && pSession->R0Process == NIL_RTR0PROCESS ? VBGLR0_HGCMCALL_F_KERNEL : VBGLR0_HGCMCALL_F_USER;
    uint32_t cbInfo = (uint32_t)(cbData - cbExtra);
#ifdef RT_ARCH_AMD64
    if (f32bit)
    {
        if (fInterruptible)
            rc = VbglR0HGCMInternalCall32(pInfo, cbInfo, fFlags, vgdrvHgcmAsyncWaitCallbackInterruptible, pDevExt, cMillies);
        else
            rc = VbglR0HGCMInternalCall32(pInfo, cbInfo, fFlags, vgdrvHgcmAsyncWaitCallback, pDevExt, cMillies);
    }
    else
#endif
    {
        if (fInterruptible)
            rc = VbglR0HGCMInternalCall(pInfo, cbInfo, fFlags, vgdrvHgcmAsyncWaitCallbackInterruptible, pDevExt, cMillies);
        else
            rc = VbglR0HGCMInternalCall(pInfo, cbInfo, fFlags, vgdrvHgcmAsyncWaitCallback, pDevExt, cMillies);
    }
    if (RT_SUCCESS(rc))
    {
        rc = pInfo->Hdr.rc;
        LogFlow(("VBOXGUEST_IOCTL_HGCM_CALL: result=%Rrc\n", rc));
    }
    else
    {
        if (   rc != VERR_INTERRUPTED
            && rc != VERR_TIMEOUT)
            LogRelMax(32, ("VBOXGUEST_IOCTL_HGCM_CALL: %s Failed. rc=%Rrc (Hdr.rc=%Rrc).\n", f32bit ? "32" : "64", rc, pInfo->Hdr.rc));
        else
            Log(("VBOXGUEST_IOCTL_HGCM_CALL: %s Failed. rc=%Rrc (Hdr.rc=%Rrc).\n", f32bit ? "32" : "64", rc, pInfo->Hdr.rc));
    }
    return rc;
}


static int vgdrvIoCtl_HGCMCallWrapper(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession, PVBGLIOCHGCMCALL pInfo,
                                      bool f32bit, bool fUserData, size_t cbData)
{
    return vgdrvIoCtl_HGCMCallInner(pDevExt, pSession, pInfo, pInfo->cMsTimeout,
                                    pInfo->fInterruptible || pSession->R0Process != NIL_RTR0PROCESS,
                                    f32bit, fUserData, 0 /*cbExtra*/, cbData);
}


#endif /* VBOX_WITH_HGCM */

/**
 * Handle VBGL_IOCTL_CHECK_BALLOON from R3.
 *
 * Ask the host for the size of the balloon and try to set it accordingly.  If
 * this approach fails because it's not supported, return with fHandleInR3 set
 * and let the user land supply memory we can lock via the other ioctl.
 *
 * @returns VBox status code.
 *
 * @param   pDevExt             The device extension.
 * @param   pSession            The session.
 * @param   pInfo               The output buffer.
 */
static int vgdrvIoCtl_CheckMemoryBalloon(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession, PVBGLIOCCHECKBALLOON pInfo)
{
    VMMDevGetMemBalloonChangeRequest *pReq;
    int rc;

    LogFlow(("VBGL_IOCTL_CHECK_BALLOON:\n"));
    rc = RTSemFastMutexRequest(pDevExt->MemBalloon.hMtx);
    AssertRCReturn(rc, rc);

    /*
     * The first user trying to query/change the balloon becomes the
     * owner and owns it until the session is closed (vgdrvCloseMemBalloon).
     */
    if (   pDevExt->MemBalloon.pOwner != pSession
        && pDevExt->MemBalloon.pOwner == NULL)
        pDevExt->MemBalloon.pOwner = pSession;

    if (pDevExt->MemBalloon.pOwner == pSession)
    {
        rc = VbglR0GRAlloc((VMMDevRequestHeader **)&pReq, sizeof(VMMDevGetMemBalloonChangeRequest), VMMDevReq_GetMemBalloonChangeRequest);
        if (RT_SUCCESS(rc))
        {
            /*
             * This is a response to that event. Setting this bit means that
             * we request the value from the host and change the guest memory
             * balloon according to this value.
             */
            pReq->eventAck = VMMDEV_EVENT_BALLOON_CHANGE_REQUEST;
            rc = VbglR0GRPerform(&pReq->header);
            if (RT_SUCCESS(rc))
            {
                Assert(pDevExt->MemBalloon.cMaxChunks == pReq->cPhysMemChunks || pDevExt->MemBalloon.cMaxChunks == 0);
                pDevExt->MemBalloon.cMaxChunks = pReq->cPhysMemChunks;

                pInfo->u.Out.cBalloonChunks = pReq->cBalloonChunks;
                pInfo->u.Out.fHandleInR3    = false;
                pInfo->u.Out.afPadding[0]   = false;
                pInfo->u.Out.afPadding[1]   = false;
                pInfo->u.Out.afPadding[2]   = false;

                rc = vgdrvSetBalloonSizeKernel(pDevExt, pReq->cBalloonChunks, &pInfo->u.Out.fHandleInR3);
                /* Ignore various out of memory failures. */
                if (   rc == VERR_NO_MEMORY
                    || rc == VERR_NO_PHYS_MEMORY
                    || rc == VERR_NO_CONT_MEMORY)
                    rc = VINF_SUCCESS;
            }
            else
                LogRel(("VBGL_IOCTL_CHECK_BALLOON: VbglR0GRPerform failed. rc=%Rrc\n", rc));
            VbglR0GRFree(&pReq->header);
        }
    }
    else
        rc = VERR_PERMISSION_DENIED;

    RTSemFastMutexRelease(pDevExt->MemBalloon.hMtx);
    LogFlow(("VBGL_IOCTL_CHECK_BALLOON returns %Rrc\n", rc));
    return rc;
}


/**
 * Handle a request for changing the memory balloon.
 *
 * @returns VBox status code.
 *
 * @param   pDevExt             The device extention.
 * @param   pSession            The session.
 * @param   pInfo               The change request structure (input).
 */
static int vgdrvIoCtl_ChangeMemoryBalloon(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession, PVBGLIOCCHANGEBALLOON pInfo)
{
    int rc;
    LogFlow(("VBGL_IOCTL_CHANGE_BALLOON: fInflate=%RTbool u64ChunkAddr=%p\n", pInfo->u.In.fInflate, pInfo->u.In.pvChunk));
    if (   pInfo->u.In.abPadding[0]
        || pInfo->u.In.abPadding[1]
        || pInfo->u.In.abPadding[2]
        || pInfo->u.In.abPadding[3]
        || pInfo->u.In.abPadding[4]
        || pInfo->u.In.abPadding[5]
        || pInfo->u.In.abPadding[6]
#if ARCH_BITS == 32
        || pInfo->u.In.abPadding[7]
        || pInfo->u.In.abPadding[8]
        || pInfo->u.In.abPadding[9]
#endif
       )
    {
        Log(("VBGL_IOCTL_CHANGE_BALLOON: Padding isn't all zero: %.*Rhxs\n", sizeof(pInfo->u.In.abPadding), pInfo->u.In.abPadding));
        return VERR_INVALID_PARAMETER;
    }

    rc = RTSemFastMutexRequest(pDevExt->MemBalloon.hMtx);
    AssertRCReturn(rc, rc);

    if (!pDevExt->MemBalloon.fUseKernelAPI)
    {
        /*
         * The first user trying to query/change the balloon becomes the
         * owner and owns it until the session is closed (vgdrvCloseMemBalloon).
         */
        if (   pDevExt->MemBalloon.pOwner != pSession
            && pDevExt->MemBalloon.pOwner == NULL)
            pDevExt->MemBalloon.pOwner = pSession;

        if (pDevExt->MemBalloon.pOwner == pSession)
            rc = vgdrvSetBalloonSizeFromUser(pDevExt, pSession, pInfo->u.In.pvChunk, pInfo->u.In.fInflate != false);
        else
            rc = VERR_PERMISSION_DENIED;
    }
    else
        rc = VERR_PERMISSION_DENIED;

    RTSemFastMutexRelease(pDevExt->MemBalloon.hMtx);
    return rc;
}


/**
 * Handle a request for writing a core dump of the guest on the host.
 *
 * @returns VBox status code.
 *
 * @param pDevExt               The device extension.
 * @param pInfo                 The output buffer.
 */
static int vgdrvIoCtl_WriteCoreDump(PVBOXGUESTDEVEXT pDevExt, PVBGLIOCWRITECOREDUMP pInfo)
{
    VMMDevReqWriteCoreDump *pReq = NULL;
    int rc;
    LogFlow(("VBOXGUEST_IOCTL_WRITE_CORE_DUMP\n"));
    RT_NOREF1(pDevExt);

    rc = VbglR0GRAlloc((VMMDevRequestHeader **)&pReq, sizeof(*pReq), VMMDevReq_WriteCoreDump);
    if (RT_SUCCESS(rc))
    {
        pReq->fFlags = pInfo->u.In.fFlags;
        rc = VbglR0GRPerform(&pReq->header);
        if (RT_FAILURE(rc))
            Log(("VBOXGUEST_IOCTL_WRITE_CORE_DUMP: VbglR0GRPerform failed, rc=%Rrc!\n", rc));

        VbglR0GRFree(&pReq->header);
    }
    else
        Log(("VBOXGUEST_IOCTL_WRITE_CORE_DUMP: failed to allocate %u (%#x) bytes to cache the request. rc=%Rrc!!\n",
             sizeof(*pReq), sizeof(*pReq), rc));
    return rc;
}


/**
 * Guest backdoor logging.
 *
 * @returns VBox status code.
 *
 * @param   pDevExt             The device extension.
 * @param   pch                 The log message (need not be NULL terminated).
 * @param   cbData              Size of the buffer.
 * @param   fUserSession        Copy of VBOXGUESTSESSION::fUserSession for the
 *                              call.  True normal user, false root user.
 */
static int vgdrvIoCtl_Log(PVBOXGUESTDEVEXT pDevExt, const char *pch, size_t cbData, bool fUserSession)
{
    if (pDevExt->fLoggingEnabled)
        RTLogBackdoorPrintf("%.*s", cbData, pch);
    else if (!fUserSession)
        LogRel(("%.*s", cbData, pch));
    else
        Log(("%.*s", cbData, pch));
    return VINF_SUCCESS;
}


/** @name Guest Capabilities, Mouse Status and Event Filter
 * @{
 */

/**
 * Clears a bit usage tracker (init time).
 *
 * @param   pTracker            The tracker to clear.
 */
static void vgdrvBitUsageTrackerClear(PVBOXGUESTBITUSAGETRACER pTracker)
{
    uint32_t iBit;
    AssertCompile(sizeof(pTracker->acPerBitUsage) == 32 * sizeof(uint32_t));

    for (iBit = 0; iBit < 32; iBit++)
        pTracker->acPerBitUsage[iBit] = 0;
    pTracker->fMask = 0;
}


#ifdef VBOX_STRICT
/**
 * Checks that pTracker->fMask is correct and that the usage values are within
 * the valid range.
 *
 * @param   pTracker            The tracker.
 * @param   cMax                Max valid usage value.
 * @param   pszWhat             Identifies the tracker in assertions.
 */
static void vgdrvBitUsageTrackerCheckMask(PCVBOXGUESTBITUSAGETRACER pTracker, uint32_t cMax, const char *pszWhat)
{
    uint32_t fMask = 0;
    uint32_t iBit;
    AssertCompile(sizeof(pTracker->acPerBitUsage) == 32 * sizeof(uint32_t));

    for (iBit = 0; iBit < 32; iBit++)
        if (pTracker->acPerBitUsage[iBit])
        {
            fMask |= RT_BIT_32(iBit);
            AssertMsg(pTracker->acPerBitUsage[iBit] <= cMax,
                      ("%s: acPerBitUsage[%u]=%#x cMax=%#x\n", pszWhat, iBit, pTracker->acPerBitUsage[iBit], cMax));
        }

    AssertMsg(fMask == pTracker->fMask, ("%s: %#x vs %#x\n", pszWhat, fMask, pTracker->fMask));
}
#endif


/**
 * Applies a change to the bit usage tracker.
 *
 *
 * @returns true if the mask changed, false if not.
 * @param   pTracker            The bit usage tracker.
 * @param   fChanged            The bits to change.
 * @param   fPrevious           The previous value of the bits.
 * @param   cMax                The max valid usage value for assertions.
 * @param   pszWhat             Identifies the tracker in assertions.
 */
static bool vgdrvBitUsageTrackerChange(PVBOXGUESTBITUSAGETRACER pTracker, uint32_t fChanged, uint32_t fPrevious,
                                       uint32_t cMax, const char *pszWhat)
{
    bool fGlobalChange = false;
    AssertCompile(sizeof(pTracker->acPerBitUsage) == 32 * sizeof(uint32_t));

    while (fChanged)
    {
        uint32_t const iBit     = ASMBitFirstSetU32(fChanged) - 1;
        uint32_t const fBitMask = RT_BIT_32(iBit);
        Assert(iBit < 32); Assert(fBitMask & fChanged);

        if (fBitMask & fPrevious)
        {
            pTracker->acPerBitUsage[iBit] -= 1;
            AssertMsg(pTracker->acPerBitUsage[iBit] <= cMax,
                      ("%s: acPerBitUsage[%u]=%#x cMax=%#x\n", pszWhat, iBit, pTracker->acPerBitUsage[iBit], cMax));
            if (pTracker->acPerBitUsage[iBit] == 0)
            {
                fGlobalChange = true;
                pTracker->fMask &= ~fBitMask;
            }
        }
        else
        {
            pTracker->acPerBitUsage[iBit] += 1;
            AssertMsg(pTracker->acPerBitUsage[iBit] > 0 && pTracker->acPerBitUsage[iBit] <= cMax,
                      ("pTracker->acPerBitUsage[%u]=%#x cMax=%#x\n", pszWhat, iBit, pTracker->acPerBitUsage[iBit], cMax));
            if (pTracker->acPerBitUsage[iBit] == 1)
            {
                fGlobalChange = true;
                pTracker->fMask |= fBitMask;
            }
        }

        fChanged &= ~fBitMask;
    }

#ifdef VBOX_STRICT
    vgdrvBitUsageTrackerCheckMask(pTracker, cMax, pszWhat);
#endif
    NOREF(pszWhat); NOREF(cMax);
    return fGlobalChange;
}


/**
 * Init and termination worker for resetting the (host) event filter on the host
 *
 * @returns VBox status code.
 * @param   pDevExt         The device extension.
 * @param   fFixedEvents    Fixed events (init time).
 */
static int vgdrvResetEventFilterOnHost(PVBOXGUESTDEVEXT pDevExt, uint32_t fFixedEvents)
{
    VMMDevCtlGuestFilterMask *pReq;
    int rc = VbglR0GRAlloc((VMMDevRequestHeader **)&pReq, sizeof(*pReq), VMMDevReq_CtlGuestFilterMask);
    if (RT_SUCCESS(rc))
    {
        pReq->u32NotMask = UINT32_MAX & ~fFixedEvents;
        pReq->u32OrMask  = fFixedEvents;
        rc = VbglR0GRPerform(&pReq->header);
        if (RT_FAILURE(rc))
            LogRelFunc(("failed with rc=%Rrc\n", rc));
        VbglR0GRFree(&pReq->header);
    }
    RT_NOREF1(pDevExt);
    return rc;
}


/**
 * Changes the event filter mask for the given session.
 *
 * This is called in response to VBGL_IOCTL_CHANGE_FILTER_MASK as well as to do
 * session cleanup.
 *
 * @returns VBox status code.
 * @param   pDevExt             The device extension.
 * @param   pSession            The session.
 * @param   fOrMask             The events to add.
 * @param   fNotMask            The events to remove.
 * @param   fSessionTermination Set if we're called by the session cleanup code.
 *                              This tweaks the error handling so we perform
 *                              proper session cleanup even if the host
 *                              misbehaves.
 *
 * @remarks Takes the session spinlock.
 */
static int vgdrvSetSessionEventFilter(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession,
                                      uint32_t fOrMask, uint32_t fNotMask, bool fSessionTermination)
{
    VMMDevCtlGuestFilterMask   *pReq;
    uint32_t                    fChanged;
    uint32_t                    fPrevious;
    int                         rc;

    /*
     * Preallocate a request buffer so we can do all in one go without leaving the spinlock.
     */
    rc = VbglR0GRAlloc((VMMDevRequestHeader **)&pReq, sizeof(*pReq), VMMDevReq_CtlGuestFilterMask);
    if (RT_SUCCESS(rc))
    { /* nothing */ }
    else if (!fSessionTermination)
    {
        LogRel(("vgdrvSetSessionFilterMask: VbglR0GRAlloc failure: %Rrc\n", rc));
        return rc;
    }
    else
        pReq = NULL; /* Ignore failure, we must do session cleanup. */


    RTSpinlockAcquire(pDevExt->SessionSpinlock);

    /*
     * Apply the changes to the session mask.
     */
    fPrevious = pSession->fEventFilter;
    pSession->fEventFilter |= fOrMask;
    pSession->fEventFilter &= ~fNotMask;

    /*
     * If anything actually changed, update the global usage counters.
     */
    fChanged = fPrevious ^ pSession->fEventFilter;
    LogFlow(("vgdrvSetSessionEventFilter: Session->fEventFilter: %#x -> %#x (changed %#x)\n",
             fPrevious, pSession->fEventFilter, fChanged));
    if (fChanged)
    {
        bool fGlobalChange = vgdrvBitUsageTrackerChange(&pDevExt->EventFilterTracker, fChanged, fPrevious,
                                                        pDevExt->cSessions, "EventFilterTracker");

        /*
         * If there are global changes, update the event filter on the host.
         */
        if (fGlobalChange || pDevExt->fEventFilterHost == UINT32_MAX)
        {
            Assert(pReq || fSessionTermination);
            if (pReq)
            {
                pReq->u32OrMask = pDevExt->fFixedEvents | pDevExt->EventFilterTracker.fMask;
                if (pReq->u32OrMask == pDevExt->fEventFilterHost)
                    rc = VINF_SUCCESS;
                else
                {
                    pDevExt->fEventFilterHost = pReq->u32OrMask;
                    pReq->u32NotMask = ~pReq->u32OrMask;
                    rc = VbglR0GRPerform(&pReq->header);
                    if (RT_FAILURE(rc))
                    {
                        /*
                         * Failed, roll back (unless it's session termination time).
                         */
                        pDevExt->fEventFilterHost = UINT32_MAX;
                        if (!fSessionTermination)
                        {
                            vgdrvBitUsageTrackerChange(&pDevExt->EventFilterTracker, fChanged, pSession->fEventFilter,
                                                       pDevExt->cSessions, "EventFilterTracker");
                            pSession->fEventFilter = fPrevious;
                        }
                    }
                }
            }
            else
                rc = VINF_SUCCESS;
        }
    }

    RTSpinlockRelease(pDevExt->SessionSpinlock);
    if (pReq)
        VbglR0GRFree(&pReq->header);
    return rc;
}


/**
 * Handle VBGL_IOCTL_CHANGE_FILTER_MASK.
 *
 * @returns VBox status code.
 *
 * @param   pDevExt             The device extension.
 * @param   pSession            The session.
 * @param   pInfo               The request.
 */
static int vgdrvIoCtl_ChangeFilterMask(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession, PVBGLIOCCHANGEFILTERMASK pInfo)
{
    LogFlow(("VBGL_IOCTL_CHANGE_FILTER_MASK: or=%#x not=%#x\n", pInfo->u.In.fOrMask, pInfo->u.In.fNotMask));

    if ((pInfo->u.In.fOrMask | pInfo->u.In.fNotMask) & ~VMMDEV_EVENT_VALID_EVENT_MASK)
    {
        Log(("VBGL_IOCTL_CHANGE_FILTER_MASK: or=%#x not=%#x: Invalid masks!\n", pInfo->u.In.fOrMask, pInfo->u.In.fNotMask));
        return VERR_INVALID_PARAMETER;
    }

    return vgdrvSetSessionEventFilter(pDevExt, pSession, pInfo->u.In.fOrMask, pInfo->u.In.fNotMask, false /*fSessionTermination*/);
}


/**
 * Init and termination worker for set mouse feature status to zero on the host.
 *
 * @returns VBox status code.
 * @param   pDevExt         The device extension.
 */
static int vgdrvResetMouseStatusOnHost(PVBOXGUESTDEVEXT pDevExt)
{
    VMMDevReqMouseStatus *pReq;
    int rc = VbglR0GRAlloc((VMMDevRequestHeader **)&pReq, sizeof(*pReq), VMMDevReq_SetMouseStatus);
    if (RT_SUCCESS(rc))
    {
        pReq->mouseFeatures = 0;
        pReq->pointerXPos   = 0;
        pReq->pointerYPos   = 0;
        rc = VbglR0GRPerform(&pReq->header);
        if (RT_FAILURE(rc))
            LogRelFunc(("failed with rc=%Rrc\n", rc));
        VbglR0GRFree(&pReq->header);
    }
    RT_NOREF1(pDevExt);
    return rc;
}


/**
 * Changes the mouse status mask for the given session.
 *
 * This is called in response to VBOXGUEST_IOCTL_SET_MOUSE_STATUS as well as to
 * do session cleanup.
 *
 * @returns VBox status code.
 * @param   pDevExt             The device extension.
 * @param   pSession            The session.
 * @param   fOrMask             The status flags to add.
 * @param   fNotMask            The status flags to remove.
 * @param   fSessionTermination Set if we're called by the session cleanup code.
 *                              This tweaks the error handling so we perform
 *                              proper session cleanup even if the host
 *                              misbehaves.
 *
 * @remarks Takes the session spinlock.
 */
static int vgdrvSetSessionMouseStatus(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession,
                                      uint32_t fOrMask, uint32_t fNotMask, bool fSessionTermination)
{
    VMMDevReqMouseStatus   *pReq;
    uint32_t                fChanged;
    uint32_t                fPrevious;
    int                     rc;

    /*
     * Preallocate a request buffer so we can do all in one go without leaving the spinlock.
     */
    rc = VbglR0GRAlloc((VMMDevRequestHeader **)&pReq, sizeof(*pReq), VMMDevReq_SetMouseStatus);
    if (RT_SUCCESS(rc))
    { /* nothing */ }
    else if (!fSessionTermination)
    {
        LogRel(("vgdrvSetSessionMouseStatus: VbglR0GRAlloc failure: %Rrc\n", rc));
        return rc;
    }
    else
        pReq = NULL; /* Ignore failure, we must do session cleanup. */


    RTSpinlockAcquire(pDevExt->SessionSpinlock);

    /*
     * Apply the changes to the session mask.
     */
    fPrevious = pSession->fMouseStatus;
    pSession->fMouseStatus |= fOrMask;
    pSession->fMouseStatus &= ~fNotMask;

    /*
     * If anything actually changed, update the global usage counters.
     */
    fChanged = fPrevious ^ pSession->fMouseStatus;
    if (fChanged)
    {
        bool fGlobalChange = vgdrvBitUsageTrackerChange(&pDevExt->MouseStatusTracker, fChanged, fPrevious,
                                                        pDevExt->cSessions, "MouseStatusTracker");

        /*
         * If there are global changes, update the event filter on the host.
         */
        if (fGlobalChange || pDevExt->fMouseStatusHost == UINT32_MAX)
        {
            Assert(pReq || fSessionTermination);
            if (pReq)
            {
                pReq->mouseFeatures = pDevExt->MouseStatusTracker.fMask;
                if (pReq->mouseFeatures == pDevExt->fMouseStatusHost)
                    rc = VINF_SUCCESS;
                else
                {
                    pDevExt->fMouseStatusHost = pReq->mouseFeatures;
                    pReq->pointerXPos = 0;
                    pReq->pointerYPos = 0;
                    rc = VbglR0GRPerform(&pReq->header);
                    if (RT_FAILURE(rc))
                    {
                        /*
                         * Failed, roll back (unless it's session termination time).
                         */
                        pDevExt->fMouseStatusHost = UINT32_MAX;
                        if (!fSessionTermination)
                        {
                            vgdrvBitUsageTrackerChange(&pDevExt->MouseStatusTracker, fChanged, pSession->fMouseStatus,
                                                       pDevExt->cSessions, "MouseStatusTracker");
                            pSession->fMouseStatus = fPrevious;
                        }
                    }
                }
            }
            else
                rc = VINF_SUCCESS;
        }
    }

    RTSpinlockRelease(pDevExt->SessionSpinlock);
    if (pReq)
        VbglR0GRFree(&pReq->header);
    return rc;
}


/**
 * Sets the mouse status features for this session and updates them globally.
 *
 * @returns VBox status code.
 *
 * @param   pDevExt             The device extention.
 * @param   pSession            The session.
 * @param   fFeatures           New bitmap of enabled features.
 */
static int vgdrvIoCtl_SetMouseStatus(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession, uint32_t fFeatures)
{
    LogFlow(("VBGL_IOCTL_SET_MOUSE_STATUS: features=%#x\n", fFeatures));

    if (fFeatures & ~VMMDEV_MOUSE_GUEST_MASK)
        return VERR_INVALID_PARAMETER;

    return vgdrvSetSessionMouseStatus(pDevExt, pSession, fFeatures, ~fFeatures, false /*fSessionTermination*/);
}


/**
 * Return the mask of VMM device events that this session is allowed to see (wrt
 * to "acquire" mode guest capabilities).
 *
 * The events associated with guest capabilities in "acquire" mode will be
 * restricted to sessions which has acquired the respective capabilities.
 * If someone else tries to wait for acquired events, they won't be woken up
 * when the event becomes pending.  Should some other thread in the session
 * acquire the capability while the corresponding event is pending, the waiting
 * thread will woken up.
 *
 * @returns Mask of events valid for the given session.
 * @param   pDevExt             The device extension.
 * @param   pSession            The session.
 *
 * @remarks Needs only be called when dispatching events in the
 *          VBOXGUEST_ACQUIRE_STYLE_EVENTS mask.
 */
static uint32_t vgdrvGetAllowedEventMaskForSession(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession)
{
    uint32_t fAcquireModeGuestCaps;
    uint32_t fAcquiredGuestCaps;
    uint32_t fAllowedEvents;

    /*
     * Note! Reads pSession->fAcquiredGuestCaps and pDevExt->fAcquireModeGuestCaps
     *       WITHOUT holding VBOXGUESTDEVEXT::SessionSpinlock.
     */
    fAcquireModeGuestCaps = ASMAtomicUoReadU32(&pDevExt->fAcquireModeGuestCaps);
    if (fAcquireModeGuestCaps == 0)
        return VMMDEV_EVENT_VALID_EVENT_MASK;
    fAcquiredGuestCaps = ASMAtomicUoReadU32(&pSession->fAcquiredGuestCaps);

    /*
     * Calculate which events to allow according to the cap config and caps
     * acquired by the session.
     */
    fAllowedEvents = VMMDEV_EVENT_VALID_EVENT_MASK;
    if (   !(fAcquiredGuestCaps   & VMMDEV_GUEST_SUPPORTS_GRAPHICS)
        && (fAcquireModeGuestCaps & VMMDEV_GUEST_SUPPORTS_GRAPHICS))
        fAllowedEvents &= ~VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST;

    if (   !(fAcquiredGuestCaps   & VMMDEV_GUEST_SUPPORTS_SEAMLESS)
        && (fAcquireModeGuestCaps & VMMDEV_GUEST_SUPPORTS_SEAMLESS))
        fAllowedEvents &= ~VMMDEV_EVENT_SEAMLESS_MODE_CHANGE_REQUEST;

    return fAllowedEvents;
}


/**
 * Init and termination worker for set guest capabilities to zero on the host.
 *
 * @returns VBox status code.
 * @param   pDevExt         The device extension.
 */
static int vgdrvResetCapabilitiesOnHost(PVBOXGUESTDEVEXT pDevExt)
{
    VMMDevReqGuestCapabilities2 *pReq;
    int rc = VbglR0GRAlloc((VMMDevRequestHeader **)&pReq, sizeof(*pReq), VMMDevReq_SetGuestCapabilities);
    if (RT_SUCCESS(rc))
    {
        pReq->u32NotMask = UINT32_MAX;
        pReq->u32OrMask  = 0;
        rc = VbglR0GRPerform(&pReq->header);

        if (RT_FAILURE(rc))
            LogRelFunc(("failed with rc=%Rrc\n", rc));
        VbglR0GRFree(&pReq->header);
    }
    RT_NOREF1(pDevExt);
    return rc;
}


/**
 * Sets the guest capabilities to the host while holding the lock.
 *
 * This will ASSUME that we're the ones in charge of the mask, so
 * we'll simply clear all bits we don't set.
 *
 * @returns VBox status code.
 * @param   pDevExt             The device extension.
 * @param   pReq                The request.
 */
static int vgdrvUpdateCapabilitiesOnHostWithReqAndLock(PVBOXGUESTDEVEXT pDevExt, VMMDevReqGuestCapabilities2 *pReq)
{
    int rc;

    pReq->u32OrMask = pDevExt->fAcquiredGuestCaps | pDevExt->SetGuestCapsTracker.fMask;
    if (pReq->u32OrMask == pDevExt->fGuestCapsHost)
        rc = VINF_SUCCESS;
    else
    {
        pDevExt->fGuestCapsHost = pReq->u32OrMask;
        pReq->u32NotMask = ~pReq->u32OrMask;
        rc = VbglR0GRPerform(&pReq->header);
        if (RT_FAILURE(rc))
            pDevExt->fGuestCapsHost = UINT32_MAX;
    }

    return rc;
}


/**
 * Switch a set of capabilities into "acquire" mode and (maybe) acquire them for
 * the given session.
 *
 * This is called in response to VBOXGUEST_IOCTL_GUEST_CAPS_ACQUIRE as well as
 * to do session cleanup.
 *
 * @returns VBox status code.
 * @param   pDevExt             The device extension.
 * @param   pSession            The session.
 * @param   fOrMask             The capabilities to add .
 * @param   fNotMask            The capabilities to remove.  Ignored in
 *                              VBOXGUESTCAPSACQUIRE_FLAGS_CONFIG_ACQUIRE_MODE.
 * @param   fFlags              Confusing operation modifier.
 *                              VBOXGUESTCAPSACQUIRE_FLAGS_NONE means to both
 *                              configure and acquire/release the capabilities.
 *                              VBOXGUESTCAPSACQUIRE_FLAGS_CONFIG_ACQUIRE_MODE
 *                              means only configure capabilities in the
 *                              @a fOrMask capabilities for "acquire" mode.
 * @param   fSessionTermination Set if we're called by the session cleanup code.
 *                              This tweaks the error handling so we perform
 *                              proper session cleanup even if the host
 *                              misbehaves.
 *
 * @remarks Takes both the session and event spinlocks.
 */
static int vgdrvAcquireSessionCapabilities(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession,
                                           uint32_t fOrMask, uint32_t fNotMask, uint32_t fFlags,
                                           bool fSessionTermination)
{
    uint32_t fCurrentOwnedCaps;
    uint32_t fSessionRemovedCaps;
    uint32_t fSessionAddedCaps;
    uint32_t fOtherConflictingCaps;
    VMMDevReqGuestCapabilities2 *pReq = NULL;
    int rc;


    /*
     * Validate and adjust input.
     */
    if (fOrMask & ~(  VMMDEV_GUEST_SUPPORTS_SEAMLESS
                    | VMMDEV_GUEST_SUPPORTS_GUEST_HOST_WINDOW_MAPPING
                    | VMMDEV_GUEST_SUPPORTS_GRAPHICS ) )
    {
        LogRel(("vgdrvAcquireSessionCapabilities: invalid fOrMask=%#x (pSession=%p fNotMask=%#x fFlags=%#x)\n",
                fOrMask, pSession, fNotMask, fFlags));
        return VERR_INVALID_PARAMETER;
    }

    if ((fFlags & ~VBGL_IOC_AGC_FLAGS_VALID_MASK) != 0)
    {
        LogRel(("vgdrvAcquireSessionCapabilities: invalid fFlags=%#x (pSession=%p fOrMask=%#x fNotMask=%#x)\n",
                fFlags, pSession, fOrMask, fNotMask));
        return VERR_INVALID_PARAMETER;
    }
    Assert(!fOrMask || !fSessionTermination);

    /* The fNotMask no need to have all values valid, invalid ones will simply be ignored. */
    fNotMask &= ~fOrMask;

    /*
     * Preallocate a update request if we're about to do more than just configure
     * the capability mode.
     */
    if (!(fFlags & VBGL_IOC_AGC_FLAGS_CONFIG_ACQUIRE_MODE))
    {
        rc = VbglR0GRAlloc((VMMDevRequestHeader **)&pReq, sizeof(*pReq), VMMDevReq_SetGuestCapabilities);
        if (RT_SUCCESS(rc))
        { /* do nothing */ }
        else if (!fSessionTermination)
        {
            LogRel(("vgdrvAcquireSessionCapabilities: pSession=%p fOrMask=%#x fNotMask=%#x fFlags=%#x: VbglR0GRAlloc failure: %Rrc\n",
                    pSession, fOrMask, fNotMask, fFlags, rc));
            return rc;
        }
        else
            pReq = NULL; /* Ignore failure, we must do session cleanup. */
    }

    /*
     * Try switch the capabilities in the OR mask into "acquire" mode.
     *
     * Note! We currently ignore anyone which may already have "set" the capabilities
     *       in fOrMask.  Perhaps not the best way to handle it, but it's simple...
     */
    RTSpinlockAcquire(pDevExt->EventSpinlock);

    if (!(pDevExt->fSetModeGuestCaps & fOrMask))
        pDevExt->fAcquireModeGuestCaps |= fOrMask;
    else
    {
        RTSpinlockRelease(pDevExt->EventSpinlock);

        if (pReq)
            VbglR0GRFree(&pReq->header);
        AssertMsgFailed(("Trying to change caps mode: %#x\n", fOrMask));
        LogRel(("vgdrvAcquireSessionCapabilities: pSession=%p fOrMask=%#x fNotMask=%#x fFlags=%#x: calling caps acquire for set caps\n",
                pSession, fOrMask, fNotMask, fFlags));
        return VERR_INVALID_STATE;
    }

    /*
     * If we only wanted to switch the capabilities into "acquire" mode, we're done now.
     */
    if (fFlags & VBGL_IOC_AGC_FLAGS_CONFIG_ACQUIRE_MODE)
    {
        RTSpinlockRelease(pDevExt->EventSpinlock);

        Assert(!pReq);
        Log(("vgdrvAcquireSessionCapabilities: pSession=%p fOrMask=%#x fNotMask=%#x fFlags=%#x: configured acquire caps: 0x%x\n",
             pSession, fOrMask, fNotMask, fFlags));
        return VINF_SUCCESS;
    }
    Assert(pReq || fSessionTermination);

    /*
     * Caller wants to acquire/release the capabilities too.
     *
     * Note! The mode change of the capabilities above won't be reverted on
     *       failure, this is intentional.
     */
    fCurrentOwnedCaps      = pSession->fAcquiredGuestCaps;
    fSessionRemovedCaps    = fCurrentOwnedCaps & fNotMask;
    fSessionAddedCaps      = fOrMask & ~fCurrentOwnedCaps;
    fOtherConflictingCaps  = pDevExt->fAcquiredGuestCaps & ~fCurrentOwnedCaps;
    fOtherConflictingCaps &= fSessionAddedCaps;

    if (!fOtherConflictingCaps)
    {
        if (fSessionAddedCaps)
        {
            pSession->fAcquiredGuestCaps |= fSessionAddedCaps;
            pDevExt->fAcquiredGuestCaps  |= fSessionAddedCaps;
        }

        if (fSessionRemovedCaps)
        {
            pSession->fAcquiredGuestCaps &= ~fSessionRemovedCaps;
            pDevExt->fAcquiredGuestCaps  &= ~fSessionRemovedCaps;
        }

        /*
         * If something changes (which is very likely), tell the host.
         */
        if (fSessionAddedCaps || fSessionRemovedCaps || pDevExt->fGuestCapsHost == UINT32_MAX)
        {
            Assert(pReq || fSessionTermination);
            if (pReq)
            {
                rc = vgdrvUpdateCapabilitiesOnHostWithReqAndLock(pDevExt, pReq);
                if (RT_FAILURE(rc) && !fSessionTermination)
                {
                    /* Failed, roll back. */
                    if (fSessionAddedCaps)
                    {
                        pSession->fAcquiredGuestCaps &= ~fSessionAddedCaps;
                        pDevExt->fAcquiredGuestCaps  &= ~fSessionAddedCaps;
                    }
                    if (fSessionRemovedCaps)
                    {
                        pSession->fAcquiredGuestCaps |= fSessionRemovedCaps;
                        pDevExt->fAcquiredGuestCaps  |= fSessionRemovedCaps;
                    }

                    RTSpinlockRelease(pDevExt->EventSpinlock);
                    LogRel(("vgdrvAcquireSessionCapabilities: vgdrvUpdateCapabilitiesOnHostWithReqAndLock failed: rc=%Rrc\n", rc));
                    VbglR0GRFree(&pReq->header);
                    return rc;
                }
            }
        }
    }
    else
    {
        RTSpinlockRelease(pDevExt->EventSpinlock);

        Log(("vgdrvAcquireSessionCapabilities: Caps %#x were busy\n", fOtherConflictingCaps));
        VbglR0GRFree(&pReq->header);
        return VERR_RESOURCE_BUSY;
    }

    RTSpinlockRelease(pDevExt->EventSpinlock);
    if (pReq)
        VbglR0GRFree(&pReq->header);

    /*
     * If we added a capability, check if that means some other thread in our
     * session should be unblocked because there are events pending.
     *
     * HACK ALERT! When the seamless support capability is added we generate a
     *             seamless change event so that the ring-3 client can sync with
     *             the seamless state. Although this introduces a spurious
     *             wakeups of the ring-3 client, it solves the problem of client
     *             state inconsistency in multiuser environment (on Windows).
     */
    if (fSessionAddedCaps)
    {
        uint32_t fGenFakeEvents = 0;
        if (fSessionAddedCaps & VMMDEV_GUEST_SUPPORTS_SEAMLESS)
            fGenFakeEvents |= VMMDEV_EVENT_SEAMLESS_MODE_CHANGE_REQUEST;

        RTSpinlockAcquire(pDevExt->EventSpinlock);
        if (fGenFakeEvents || pDevExt->f32PendingEvents)
            vgdrvDispatchEventsLocked(pDevExt, fGenFakeEvents);
        RTSpinlockRelease(pDevExt->EventSpinlock);

#ifdef VBOXGUEST_USE_DEFERRED_WAKE_UP
        VGDrvCommonWaitDoWakeUps(pDevExt);
#endif
    }

    return VINF_SUCCESS;
}


/**
 * Handle VBGL_IOCTL_ACQUIRE_GUEST_CAPABILITIES.
 *
 * @returns VBox status code.
 *
 * @param   pDevExt             The device extension.
 * @param   pSession            The session.
 * @param   pAcquire            The request.
 */
static int vgdrvIoCtl_GuestCapsAcquire(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession, PVBGLIOCACQUIREGUESTCAPS pAcquire)
{
    int rc;
    LogFlow(("VBGL_IOCTL_ACQUIRE_GUEST_CAPABILITIES: or=%#x not=%#x flags=%#x\n",
             pAcquire->u.In.fOrMask, pAcquire->u.In.fNotMask, pAcquire->u.In.fFlags));

    rc = vgdrvAcquireSessionCapabilities(pDevExt, pSession, pAcquire->u.In.fOrMask, pAcquire->u.In.fNotMask,
                                         pAcquire->u.In.fFlags, false /*fSessionTermination*/);
    if (RT_FAILURE(rc))
        LogRel(("VBGL_IOCTL_ACQUIRE_GUEST_CAPABILITIES failed rc=%Rrc\n", rc));
    return rc;
}


/**
 * Sets the guest capabilities for a session.
 *
 * @returns VBox status code.
 * @param   pDevExt             The device extension.
 * @param   pSession            The session.
 * @param   fOrMask             The capabilities to add.
 * @param   fNotMask            The capabilities to remove.
 * @param   pfSessionCaps       Where to return the guest capabilities reported
 *                              for this session.  Optional.
 * @param   pfGlobalCaps        Where to return the guest capabilities reported
 *                              for all the sessions.  Optional.
 *
 * @param   fSessionTermination Set if we're called by the session cleanup code.
 *                              This tweaks the error handling so we perform
 *                              proper session cleanup even if the host
 *                              misbehaves.
 *
 * @remarks Takes the session spinlock.
 */
static int vgdrvSetSessionCapabilities(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession,
                                       uint32_t fOrMask, uint32_t fNotMask, uint32_t *pfSessionCaps, uint32_t *pfGlobalCaps,
                                       bool fSessionTermination)
{
    /*
     * Preallocate a request buffer so we can do all in one go without leaving the spinlock.
     */
    VMMDevReqGuestCapabilities2 *pReq;
    int rc = VbglR0GRAlloc((VMMDevRequestHeader **)&pReq, sizeof(*pReq), VMMDevReq_SetGuestCapabilities);
    if (RT_SUCCESS(rc))
    { /* nothing */ }
    else if (!fSessionTermination)
    {
        if (pfSessionCaps)
            *pfSessionCaps = UINT32_MAX;
        if (pfGlobalCaps)
            *pfGlobalCaps  = UINT32_MAX;
        LogRel(("vgdrvSetSessionCapabilities: VbglR0GRAlloc failure: %Rrc\n", rc));
        return rc;
    }
    else
        pReq = NULL; /* Ignore failure, we must do session cleanup. */


    RTSpinlockAcquire(pDevExt->SessionSpinlock);

#ifndef VBOXGUEST_DISREGARD_ACQUIRE_MODE_GUEST_CAPS
    /*
     * Capabilities in "acquire" mode cannot be set via this API.
     * (Acquire mode is only used on windows at the time of writing.)
     */
    if (!(fOrMask & pDevExt->fAcquireModeGuestCaps))
#endif
    {
        /*
         * Apply the changes to the session mask.
         */
        uint32_t fChanged;
        uint32_t fPrevious = pSession->fCapabilities;
        pSession->fCapabilities |= fOrMask;
        pSession->fCapabilities &= ~fNotMask;

        /*
         * If anything actually changed, update the global usage counters.
         */
        fChanged = fPrevious ^ pSession->fCapabilities;
        if (fChanged)
        {
            bool fGlobalChange = vgdrvBitUsageTrackerChange(&pDevExt->SetGuestCapsTracker, fChanged, fPrevious,
                                                            pDevExt->cSessions, "SetGuestCapsTracker");

            /*
             * If there are global changes, update the capabilities on the host.
             */
            if (fGlobalChange || pDevExt->fGuestCapsHost == UINT32_MAX)
            {
                Assert(pReq || fSessionTermination);
                if (pReq)
                {
                    rc = vgdrvUpdateCapabilitiesOnHostWithReqAndLock(pDevExt, pReq);

                    /* On failure, roll back (unless it's session termination time). */
                    if (RT_FAILURE(rc) && !fSessionTermination)
                    {
                        vgdrvBitUsageTrackerChange(&pDevExt->SetGuestCapsTracker, fChanged, pSession->fCapabilities,
                                                   pDevExt->cSessions, "SetGuestCapsTracker");
                        pSession->fCapabilities = fPrevious;
                    }
                }
            }
        }
    }
#ifndef VBOXGUEST_DISREGARD_ACQUIRE_MODE_GUEST_CAPS
    else
        rc = VERR_RESOURCE_BUSY;
#endif

    if (pfSessionCaps)
        *pfSessionCaps = pSession->fCapabilities;
    if (pfGlobalCaps)
        *pfGlobalCaps  = pDevExt->fAcquiredGuestCaps | pDevExt->SetGuestCapsTracker.fMask;

    RTSpinlockRelease(pDevExt->SessionSpinlock);
    if (pReq)
        VbglR0GRFree(&pReq->header);
    return rc;
}


/**
 * Handle VBGL_IOCTL_CHANGE_GUEST_CAPABILITIES.
 *
 * @returns VBox status code.
 *
 * @param   pDevExt             The device extension.
 * @param   pSession            The session.
 * @param   pInfo               The request.
 */
static int vgdrvIoCtl_SetCapabilities(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession, PVBGLIOCSETGUESTCAPS pInfo)
{
    int rc;
    LogFlow(("VBGL_IOCTL_CHANGE_GUEST_CAPABILITIES: or=%#x not=%#x\n", pInfo->u.In.fOrMask, pInfo->u.In.fNotMask));

    if (!((pInfo->u.In.fOrMask | pInfo->u.In.fNotMask) & ~VMMDEV_GUEST_CAPABILITIES_MASK))
        rc = vgdrvSetSessionCapabilities(pDevExt, pSession, pInfo->u.In.fOrMask, pInfo->u.In.fNotMask,
                                         &pInfo->u.Out.fSessionCaps, &pInfo->u.Out.fGlobalCaps, false /*fSessionTermination*/);
    else
        rc = VERR_INVALID_PARAMETER;

    return rc;
}

/** @} */


/**
 * Common IOCtl for user to kernel and kernel to kernel communication.
 *
 * This function only does the basic validation and then invokes
 * worker functions that takes care of each specific function.
 *
 * @returns VBox status code.
 *
 * @param   iFunction           The requested function.
 * @param   pDevExt             The device extension.
 * @param   pSession            The client session.
 * @param   pReqHdr             Pointer to the request.  This always starts with
 *                              a request common header.
 * @param   cbReq               The max size of the request buffer.
 */
int VGDrvCommonIoCtl(uintptr_t iFunction, PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession, PVBGLREQHDR pReqHdr, size_t cbReq)
{
    uintptr_t const iFunctionStripped = VBGL_IOCTL_CODE_STRIPPED(iFunction);
    int rc;

    LogFlow(("VGDrvCommonIoCtl: iFunction=%#x pDevExt=%p pSession=%p pReqHdr=%p cbReq=%zu\n",
             iFunction, pDevExt, pSession, pReqHdr, cbReq));

    /*
     * Define some helper macros to simplify validation.
     */
#define REQ_CHECK_SIZES_EX(Name, cbInExpect, cbOutExpect) \
    do { \
        if (RT_LIKELY(   pReqHdr->cbIn == (cbInExpect) \
                      && (   pReqHdr->cbOut == (cbOutExpect) \
                          || ((cbInExpect) == (cbOutExpect) && pReqHdr->cbOut == 0) ) )) \
        { /* likely */ } \
        else \
        { \
            Log(( #Name ": Invalid input/output sizes. cbIn=%ld expected %ld. cbOut=%ld expected %ld.\n", \
                 (long)pReqHdr->cbIn, (long)(cbInExpect), (long)pReqHdr->cbOut, (long)(cbOutExpect))); \
            return pReqHdr->rc = VERR_INVALID_PARAMETER; \
        } \
    } while (0)

#define REQ_CHECK_SIZES(Name) REQ_CHECK_SIZES_EX(Name, Name ## _SIZE_IN, Name ## _SIZE_OUT)

#define REQ_CHECK_SIZE_IN(Name, cbInExpect) \
    do { \
        if (RT_LIKELY(pReqHdr->cbIn == (cbInExpect))) \
        { /* likely */ } \
        else \
        { \
            Log(( #Name ": Invalid input/output sizes. cbIn=%ld expected %ld.\n", \
                 (long)pReqHdr->cbIn, (long)(cbInExpect))); \
            return pReqHdr->rc = VERR_INVALID_PARAMETER; \
        } \
    } while (0)

#define REQ_CHECK_SIZE_OUT(Name, cbOutExpect) \
    do { \
        if (RT_LIKELY(   pReqHdr->cbOut == (cbOutExpect) \
                      || (pReqHdr->cbOut == 0 && pReqHdr->cbIn == (cbOutExpect)))) \
        { /* likely */ } \
        else \
        { \
            Log(( #Name ": Invalid input/output sizes. cbOut=%ld (%ld) expected %ld.\n", \
                 (long)pReqHdr->cbOut, (long)pReqHdr->cbIn, (long)(cbOutExpect))); \
            return pReqHdr->rc = VERR_INVALID_PARAMETER; \
        } \
    } while (0)

#define REQ_CHECK_EXPR(Name, expr) \
    do { \
        if (RT_LIKELY(!!(expr))) \
        { /* likely */ } \
        else \
        { \
            Log(( #Name ": %s\n", #expr)); \
            return pReqHdr->rc = VERR_INVALID_PARAMETER; \
        } \
    } while (0)

#define REQ_CHECK_EXPR_FMT(expr, fmt) \
    do { \
        if (RT_LIKELY(!!(expr))) \
        { /* likely */ } \
        else \
        { \
            Log( fmt ); \
            return pReqHdr->rc = VERR_INVALID_PARAMETER; \
        } \
    } while (0)

#define REQ_CHECK_RING0(mnemonic) \
    do { \
        if (pSession->R0Process != NIL_RTR0PROCESS) \
        { \
            LogFunc((mnemonic ": Ring-0 only, caller is %RTproc/%p\n", \
                     pSession->Process, (uintptr_t)pSession->R0Process)); \
            return pReqHdr->rc = VERR_PERMISSION_DENIED; \
        } \
    } while (0)


    /*
     * Validate the request.
     */
    if (RT_LIKELY(cbReq >= sizeof(*pReqHdr)))
    { /* likely */ }
    else
    {
        Log(("VGDrvCommonIoCtl: Bad ioctl request size; cbReq=%#lx\n", (long)cbReq));
        return VERR_INVALID_PARAMETER;
    }

    if (pReqHdr->cbOut == 0)
        pReqHdr->cbOut = pReqHdr->cbIn;

    if (RT_LIKELY(   pReqHdr->uVersion == VBGLREQHDR_VERSION
                  && pReqHdr->cbIn  >= sizeof(*pReqHdr)
                  && pReqHdr->cbIn  <= cbReq
                  && pReqHdr->cbOut >= sizeof(*pReqHdr)
                  && pReqHdr->cbOut <= cbReq))
    { /* likely */ }
    else
    {
        Log(("VGDrvCommonIoCtl: Bad ioctl request header; cbIn=%#lx cbOut=%#lx version=%#lx\n",
             (long)pReqHdr->cbIn, (long)pReqHdr->cbOut, (long)pReqHdr->uVersion));
        return VERR_INVALID_PARAMETER;
    }

    if (RT_LIKELY(RT_VALID_PTR(pSession)))
    { /* likely */ }
    else
    {
        Log(("VGDrvCommonIoCtl: Invalid pSession value %p (ioctl=%#x)\n", pSession, iFunction));
        return VERR_INVALID_PARAMETER;
    }


    /*
     * Deal with variably sized requests first.
     */
    rc = VINF_SUCCESS;
    if (   iFunctionStripped == VBGL_IOCTL_CODE_STRIPPED(VBGL_IOCTL_VMMDEV_REQUEST(0))
        || iFunctionStripped == VBGL_IOCTL_CODE_STRIPPED(VBGL_IOCTL_VMMDEV_REQUEST_BIG) )
    {
        REQ_CHECK_EXPR(VBGL_IOCTL_VMMDEV_REQUEST, pReqHdr->uType != VBGLREQHDR_TYPE_DEFAULT);
        REQ_CHECK_EXPR_FMT(pReqHdr->cbIn == pReqHdr->cbOut,
                           ("VBGL_IOCTL_VMMDEV_REQUEST: cbIn=%ld != cbOut=%ld\n", (long)pReqHdr->cbIn, (long)pReqHdr->cbOut));
        pReqHdr->rc = vgdrvIoCtl_VMMDevRequest(pDevExt, pSession, (VMMDevRequestHeader *)pReqHdr, cbReq);
    }
    else if (RT_LIKELY(pReqHdr->uType == VBGLREQHDR_TYPE_DEFAULT))
    {
        if (iFunctionStripped == VBGL_IOCTL_CODE_STRIPPED(VBGL_IOCTL_LOG(0)))
        {
            REQ_CHECK_SIZE_OUT(VBGL_IOCTL_LOG, VBGL_IOCTL_LOG_SIZE_OUT);
            pReqHdr->rc = vgdrvIoCtl_Log(pDevExt, &((PVBGLIOCLOG)pReqHdr)->u.In.szMsg[0], pReqHdr->cbIn - sizeof(VBGLREQHDR),
                                         pSession->fUserSession);
        }
#ifdef VBOX_WITH_HGCM
        else if (   iFunctionStripped == VBGL_IOCTL_CODE_STRIPPED(VBGL_IOCTL_HGCM_CALL(0))
# if ARCH_BITS == 64
                 || iFunctionStripped == VBGL_IOCTL_CODE_STRIPPED(VBGL_IOCTL_HGCM_CALL_32(0))
# endif
                )
        {
            REQ_CHECK_EXPR(VBGL_IOCTL_HGCM_CALL, pReqHdr->cbIn >= sizeof(VBGLIOCHGCMCALL));
            REQ_CHECK_EXPR(VBGL_IOCTL_HGCM_CALL, pReqHdr->cbIn == pReqHdr->cbOut);
            pReqHdr->rc = vgdrvIoCtl_HGCMCallWrapper(pDevExt, pSession, (PVBGLIOCHGCMCALL)pReqHdr,
                                                     iFunctionStripped == VBGL_IOCTL_CODE_STRIPPED(VBGL_IOCTL_HGCM_CALL_32(0)),
                                                     false /*fUserData*/, cbReq);
        }
        else if (iFunctionStripped == VBGL_IOCTL_CODE_STRIPPED(VBGL_IOCTL_HGCM_CALL_WITH_USER_DATA(0)))
        {
            REQ_CHECK_RING0("VBGL_IOCTL_HGCM_CALL_WITH_USER_DATA");
            REQ_CHECK_EXPR(VBGL_IOCTL_HGCM_CALL, pReqHdr->cbIn >= sizeof(VBGLIOCHGCMCALL));
            REQ_CHECK_EXPR(VBGL_IOCTL_HGCM_CALL, pReqHdr->cbIn == pReqHdr->cbOut);
            pReqHdr->rc = vgdrvIoCtl_HGCMCallWrapper(pDevExt, pSession, (PVBGLIOCHGCMCALL)pReqHdr,
                                                     ARCH_BITS == 32, true /*fUserData*/, cbReq);
        }
#endif /* VBOX_WITH_HGCM */
        else
        {
            switch (iFunction)
            {
                /*
                 * Ring-0 only:
                 */
                case VBGL_IOCTL_IDC_CONNECT:
                    REQ_CHECK_RING0("VBGL_IOCL_IDC_CONNECT");
                    REQ_CHECK_SIZES(VBGL_IOCTL_IDC_CONNECT);
                    pReqHdr->rc = vgdrvIoCtl_IdcConnect(pDevExt, pSession, (PVBGLIOCIDCCONNECT)pReqHdr);
                    break;

                case VBGL_IOCTL_IDC_DISCONNECT:
                    REQ_CHECK_RING0("VBGL_IOCTL_IDC_DISCONNECT");
                    REQ_CHECK_SIZES(VBGL_IOCTL_IDC_DISCONNECT);
                    pReqHdr->rc = vgdrvIoCtl_IdcDisconnect(pDevExt, pSession, (PVBGLIOCIDCDISCONNECT)pReqHdr);
                    break;

                case VBGL_IOCTL_GET_VMMDEV_IO_INFO:
                    REQ_CHECK_RING0("GET_VMMDEV_IO_INFO");
                    REQ_CHECK_SIZES(VBGL_IOCTL_GET_VMMDEV_IO_INFO);
                    pReqHdr->rc = vgdrvIoCtl_GetVMMDevIoInfo(pDevExt, (PVBGLIOCGETVMMDEVIOINFO)pReqHdr);
                    break;

                case VBGL_IOCTL_SET_MOUSE_NOTIFY_CALLBACK:
                    REQ_CHECK_RING0("SET_MOUSE_NOTIFY_CALLBACK");
                    REQ_CHECK_SIZES(VBGL_IOCTL_SET_MOUSE_NOTIFY_CALLBACK);
                    pReqHdr->rc = vgdrvIoCtl_SetMouseNotifyCallback(pDevExt, (PVBGLIOCSETMOUSENOTIFYCALLBACK)pReqHdr);
                    break;

                /*
                 * Ring-3 only:
                 */
                case VBGL_IOCTL_DRIVER_VERSION_INFO:
                    REQ_CHECK_SIZES(VBGL_IOCTL_DRIVER_VERSION_INFO);
                    pReqHdr->rc = vgdrvIoCtl_DriverVersionInfo(pDevExt, pSession, (PVBGLIOCDRIVERVERSIONINFO)pReqHdr);
                    break;

                /*
                 * Both ring-3 and ring-0:
                 */
                case VBGL_IOCTL_WAIT_FOR_EVENTS:
                    REQ_CHECK_SIZES(VBGL_IOCTL_WAIT_FOR_EVENTS);
                    pReqHdr->rc = vgdrvIoCtl_WaitForEvents(pDevExt, pSession, (VBGLIOCWAITFOREVENTS *)pReqHdr,
                                                           pSession->R0Process != NIL_RTR0PROCESS);
                    break;

                case VBGL_IOCTL_INTERRUPT_ALL_WAIT_FOR_EVENTS:
                    REQ_CHECK_SIZES(VBGL_IOCTL_INTERRUPT_ALL_WAIT_FOR_EVENTS);
                    pReqHdr->rc = vgdrvIoCtl_CancelAllWaitEvents(pDevExt, pSession);
                    break;

                case VBGL_IOCTL_CHANGE_FILTER_MASK:
                    REQ_CHECK_SIZES(VBGL_IOCTL_CHANGE_FILTER_MASK);
                    pReqHdr->rc = vgdrvIoCtl_ChangeFilterMask(pDevExt, pSession, (PVBGLIOCCHANGEFILTERMASK)pReqHdr);
                    break;

#ifdef VBOX_WITH_HGCM
                case VBGL_IOCTL_HGCM_CONNECT:
                    REQ_CHECK_SIZES(VBGL_IOCTL_HGCM_CONNECT);
                    pReqHdr->rc = vgdrvIoCtl_HGCMConnect(pDevExt, pSession, (PVBGLIOCHGCMCONNECT)pReqHdr);
                    break;

                case VBGL_IOCTL_HGCM_DISCONNECT:
                    REQ_CHECK_SIZES(VBGL_IOCTL_HGCM_DISCONNECT);
                    pReqHdr->rc = vgdrvIoCtl_HGCMDisconnect(pDevExt, pSession, (PVBGLIOCHGCMDISCONNECT)pReqHdr);
                    break;
#endif

                case VBGL_IOCTL_CHECK_BALLOON:
                    REQ_CHECK_SIZES(VBGL_IOCTL_CHECK_BALLOON);
                    pReqHdr->rc = vgdrvIoCtl_CheckMemoryBalloon(pDevExt, pSession, (PVBGLIOCCHECKBALLOON)pReqHdr);
                    break;

                case VBGL_IOCTL_CHANGE_BALLOON:
                    REQ_CHECK_SIZES(VBGL_IOCTL_CHANGE_BALLOON);
                    pReqHdr->rc = vgdrvIoCtl_ChangeMemoryBalloon(pDevExt, pSession, (PVBGLIOCCHANGEBALLOON)pReqHdr);
                    break;

                case VBGL_IOCTL_WRITE_CORE_DUMP:
                    REQ_CHECK_SIZES(VBGL_IOCTL_WRITE_CORE_DUMP);
                    pReqHdr->rc = vgdrvIoCtl_WriteCoreDump(pDevExt, (PVBGLIOCWRITECOREDUMP)pReqHdr);
                    break;

                case VBGL_IOCTL_SET_MOUSE_STATUS:
                    REQ_CHECK_SIZES(VBGL_IOCTL_SET_MOUSE_STATUS);
                    pReqHdr->rc = vgdrvIoCtl_SetMouseStatus(pDevExt, pSession, ((PVBGLIOCSETMOUSESTATUS)pReqHdr)->u.In.fStatus);
                    break;

                case VBGL_IOCTL_ACQUIRE_GUEST_CAPABILITIES:
                    REQ_CHECK_SIZES(VBGL_IOCTL_ACQUIRE_GUEST_CAPABILITIES);
                    pReqHdr->rc = vgdrvIoCtl_GuestCapsAcquire(pDevExt, pSession, (PVBGLIOCACQUIREGUESTCAPS)pReqHdr);
                    break;

                case VBGL_IOCTL_CHANGE_GUEST_CAPABILITIES:
                    REQ_CHECK_SIZES(VBGL_IOCTL_CHANGE_GUEST_CAPABILITIES);
                    pReqHdr->rc = vgdrvIoCtl_SetCapabilities(pDevExt, pSession, (PVBGLIOCSETGUESTCAPS)pReqHdr);
                    break;

#ifdef VBOX_WITH_DPC_LATENCY_CHECKER
                case VBGL_IOCTL_DPC_LATENCY_CHECKER:
                    REQ_CHECK_SIZES(VBGL_IOCTL_DPC_LATENCY_CHECKER);
                    pReqHdr->rc = VGDrvNtIOCtl_DpcLatencyChecker();
                    break;
#endif

                default:
                {
                    LogRel(("VGDrvCommonIoCtl: Unknown request iFunction=%#x (stripped %#x) cbReq=%#x\n",
                            iFunction, iFunctionStripped, cbReq));
                    pReqHdr->rc = rc = VERR_NOT_SUPPORTED;
                    break;
                }
            }
        }
    }
    else
    {
        Log(("VGDrvCommonIoCtl: uType=%#x, expected default (ioctl=%#x)\n", pReqHdr->uType, iFunction));
        return VERR_INVALID_PARAMETER;
    }

    LogFlow(("VGDrvCommonIoCtl: returns %Rrc (req: rc=%Rrc cbOut=%#x)\n", rc, pReqHdr->rc, pReqHdr->cbOut));
    return rc;
}


/**
 * Used by VGDrvCommonISR as well as the acquire guest capability code.
 *
 * @returns VINF_SUCCESS on success. On failure, ORed together
 *          RTSemEventMultiSignal errors (completes processing despite errors).
 * @param   pDevExt             The VBoxGuest device extension.
 * @param   fEvents             The events to dispatch.
 */
static int vgdrvDispatchEventsLocked(PVBOXGUESTDEVEXT pDevExt, uint32_t fEvents)
{
    PVBOXGUESTWAIT  pWait;
    PVBOXGUESTWAIT  pSafe;
    int             rc = VINF_SUCCESS;

    fEvents |= pDevExt->f32PendingEvents;

    RTListForEachSafe(&pDevExt->WaitList, pWait, pSafe, VBOXGUESTWAIT, ListNode)
    {
        uint32_t fHandledEvents = pWait->fReqEvents & fEvents;
        if (    fHandledEvents != 0
            &&  !pWait->fResEvents)
        {
            /* Does this one wait on any of the events we're dispatching?  We do a quick
               check first, then deal with VBOXGUEST_ACQUIRE_STYLE_EVENTS as applicable. */
            if (fHandledEvents & VBOXGUEST_ACQUIRE_STYLE_EVENTS)
                fHandledEvents &= vgdrvGetAllowedEventMaskForSession(pDevExt, pWait->pSession);
            if (fHandledEvents)
            {
                pWait->fResEvents = pWait->fReqEvents & fEvents & fHandledEvents;
                fEvents &= ~pWait->fResEvents;
                RTListNodeRemove(&pWait->ListNode);
#ifdef VBOXGUEST_USE_DEFERRED_WAKE_UP
                RTListAppend(&pDevExt->WakeUpList, &pWait->ListNode);
#else
                RTListAppend(&pDevExt->WokenUpList, &pWait->ListNode);
                rc |= RTSemEventMultiSignal(pWait->Event);
#endif
                if (!fEvents)
                    break;
            }
        }
    }

    ASMAtomicWriteU32(&pDevExt->f32PendingEvents, fEvents);
    return rc;
}


/**
 * Simply checks whether the IRQ is ours or not, does not do any interrupt
 * procesing.
 *
 * @returns true if it was our interrupt, false if it wasn't.
 * @param   pDevExt     The VBoxGuest device extension.
 */
bool VGDrvCommonIsOurIRQ(PVBOXGUESTDEVEXT pDevExt)
{
    RTSpinlockAcquire(pDevExt->EventSpinlock);
    bool const fOurIrq = pDevExt->pVMMDevMemory->V.V1_04.fHaveEvents;
    RTSpinlockRelease(pDevExt->EventSpinlock);

    return fOurIrq;
}


/**
 * Common interrupt service routine.
 *
 * This deals with events and with waking up thread waiting for those events.
 *
 * @returns true if it was our interrupt, false if it wasn't.
 * @param   pDevExt     The VBoxGuest device extension.
 */
bool VGDrvCommonISR(PVBOXGUESTDEVEXT pDevExt)
{
    VMMDevEvents volatile  *pReq                  = pDevExt->pIrqAckEvents;
    bool                    fMousePositionChanged = false;
    int                     rc                    = 0;
    bool                    fOurIrq;

    /*
     * Make sure we've initialized the device extension.
     */
    if (RT_UNLIKELY(!pReq))
        return false;

    /*
     * Enter the spinlock and check if it's our IRQ or not.
     */
    RTSpinlockAcquire(pDevExt->EventSpinlock);
    fOurIrq = pDevExt->pVMMDevMemory->V.V1_04.fHaveEvents;
    if (fOurIrq)
    {
        /*
         * Acknowlegde events.
         * We don't use VbglR0GRPerform here as it may take another spinlocks.
         */
        pReq->header.rc = VERR_INTERNAL_ERROR;
        pReq->events    = 0;
        ASMCompilerBarrier();
        ASMOutU32(pDevExt->IOPortBase + VMMDEV_PORT_OFF_REQUEST, (uint32_t)pDevExt->PhysIrqAckEvents);
        ASMCompilerBarrier();   /* paranoia */
        if (RT_SUCCESS(pReq->header.rc))
        {
            uint32_t        fEvents = pReq->events;

            Log3(("VGDrvCommonISR: acknowledge events succeeded %#RX32\n", fEvents));

            /*
             * VMMDEV_EVENT_MOUSE_POSITION_CHANGED can only be polled for.
             */
            if (fEvents & VMMDEV_EVENT_MOUSE_POSITION_CHANGED)
            {
                fMousePositionChanged = true;
                fEvents &= ~VMMDEV_EVENT_MOUSE_POSITION_CHANGED;
#if !defined(VBOXGUEST_MOUSE_NOTIFY_CAN_PREEMPT)
                if (pDevExt->pfnMouseNotifyCallback)
                    pDevExt->pfnMouseNotifyCallback(pDevExt->pvMouseNotifyCallbackArg);
#endif
            }

#ifdef VBOX_WITH_HGCM
            /*
             * The HGCM event/list is kind of different in that we evaluate all entries.
             */
            if (fEvents & VMMDEV_EVENT_HGCM)
            {
                PVBOXGUESTWAIT pWait;
                PVBOXGUESTWAIT pSafe;
                RTListForEachSafe(&pDevExt->HGCMWaitList, pWait, pSafe, VBOXGUESTWAIT, ListNode)
                {
                    if (pWait->pHGCMReq->fu32Flags & VBOX_HGCM_REQ_DONE)
                    {
                        pWait->fResEvents = VMMDEV_EVENT_HGCM;
                        RTListNodeRemove(&pWait->ListNode);
# ifdef VBOXGUEST_USE_DEFERRED_WAKE_UP
                        RTListAppend(&pDevExt->WakeUpList, &pWait->ListNode);
# else
                        RTListAppend(&pDevExt->WokenUpList, &pWait->ListNode);
                        rc |= RTSemEventMultiSignal(pWait->Event);
# endif
                    }
                }
                fEvents &= ~VMMDEV_EVENT_HGCM;
            }
#endif

            /*
             * Normal FIFO waiter evaluation.
             */
            rc |= vgdrvDispatchEventsLocked(pDevExt, fEvents);
        }
        else /* something is serious wrong... */
            Log(("VGDrvCommonISR: acknowledge events failed rc=%Rrc (events=%#x)!!\n",
                 pReq->header.rc, pReq->events));
    }
    else
        Log3(("VGDrvCommonISR: not ours\n"));

    RTSpinlockRelease(pDevExt->EventSpinlock);

    /*
     * Execute the mouse notification callback here if it cannot be executed while
     * holding the interrupt safe spinlock, see @bugref{8639}.
     */
#if defined(VBOXGUEST_MOUSE_NOTIFY_CAN_PREEMPT) && !defined(RT_OS_WINDOWS) /* (Windows does this in the Dpc callback) */
    if (   fMousePositionChanged
        && pDevExt->pfnMouseNotifyCallback)
        pDevExt->pfnMouseNotifyCallback(pDevExt->pvMouseNotifyCallbackArg);
#endif

#if defined(VBOXGUEST_USE_DEFERRED_WAKE_UP) && !defined(RT_OS_DARWIN) && !defined(RT_OS_WINDOWS)
    /*
     * Do wake-ups.
     * Note. On Windows this isn't possible at this IRQL, so a DPC will take
     *       care of it.  Same on darwin, doing it in the work loop callback.
     */
    VGDrvCommonWaitDoWakeUps(pDevExt);
#endif

    /*
     * Work the poll and async notification queues on OSes that implements that.
     * (Do this outside the spinlock to prevent some recursive spinlocking.)
     */
    if (fMousePositionChanged)
    {
        ASMAtomicIncU32(&pDevExt->u32MousePosChangedSeq);
        VGDrvNativeISRMousePollEvent(pDevExt);
    }

    Assert(rc == 0);
    NOREF(rc);
    return fOurIrq;
}

