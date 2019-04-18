/* $Id: VBoxGuestR0LibGenericRequest.cpp $ */
/** @file
 * VBoxGuestLibR0 - Generic VMMDev request management.
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
#include <iprt/asm.h>
#include <iprt/asm-amd64-x86.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <VBox/err.h>


DECLR0VBGL(int) VbglGR0Verify(const VMMDevRequestHeader *pReq, size_t cbReq)
{
    size_t cbReqExpected;

    if (RT_UNLIKELY(!pReq || cbReq < sizeof(VMMDevRequestHeader)))
    {
        dprintf(("VbglGR0Verify: Invalid parameter: pReq = %p, cbReq = %zu\n", pReq, cbReq));
        return VERR_INVALID_PARAMETER;
    }

    if (RT_UNLIKELY(pReq->size > cbReq))
    {
        dprintf(("VbglGR0Verify: request size %u > buffer size %zu\n", pReq->size, cbReq));
        return VERR_INVALID_PARAMETER;
    }

    /* The request size must correspond to the request type. */
    cbReqExpected = vmmdevGetRequestSize(pReq->requestType);
    if (RT_UNLIKELY(cbReq < cbReqExpected))
    {
        dprintf(("VbglGR0Verify: buffer size %zu < expected size %zu\n", cbReq, cbReqExpected));
        return VERR_INVALID_PARAMETER;
    }

    if (cbReqExpected == cbReq)
    {
        /*
         * This is most likely a fixed size request, and in this case the
         * request size must be also equal to the expected size.
         */
        if (RT_UNLIKELY(pReq->size != cbReqExpected))
        {
            dprintf(("VbglGR0Verify: request size %u != expected size %zu\n", pReq->size, cbReqExpected));
            return VERR_INVALID_PARAMETER;
        }

        return VINF_SUCCESS;
    }

    /*
     * This can be a variable size request. Check the request type and limit the size
     * to VMMDEV_MAX_VMMDEVREQ_SIZE, which is max size supported by the host.
     *
     * Note: Keep this list sorted for easier human lookup!
     */
    if (   pReq->requestType == VMMDevReq_ChangeMemBalloon
        || pReq->requestType == VMMDevReq_GetDisplayChangeRequestMulti
#ifdef VBOX_WITH_64_BITS_GUESTS
        || pReq->requestType == VMMDevReq_HGCMCall64
#endif
        || pReq->requestType == VMMDevReq_HGCMCall32
        || pReq->requestType == VMMDevReq_RegisterSharedModule
        || pReq->requestType == VMMDevReq_ReportGuestUserState
        || pReq->requestType == VMMDevReq_LogString
        || pReq->requestType == VMMDevReq_SetPointerShape
        || pReq->requestType == VMMDevReq_VideoSetVisibleRegion)
    {
        if (RT_UNLIKELY(cbReq > VMMDEV_MAX_VMMDEVREQ_SIZE))
        {
            dprintf(("VbglGR0Verify: VMMDevReq_LogString: buffer size %zu too big\n", cbReq));
            return VERR_BUFFER_OVERFLOW; /** @todo is this error code ok? */
        }
    }
    else
    {
        dprintf(("VbglGR0Verify: request size %u > buffer size %zu\n", pReq->size, cbReq));
        return VERR_IO_BAD_LENGTH; /** @todo is this error code ok? */
    }

    return VINF_SUCCESS;
}

DECLR0VBGL(int) VbglR0GRAlloc(VMMDevRequestHeader **ppReq, size_t cbReq, VMMDevRequestType enmReqType)
{
    int rc = vbglR0Enter();
    if (RT_SUCCESS(rc))
    {
        if (   ppReq
            && cbReq >= sizeof(VMMDevRequestHeader)
            && cbReq == (uint32_t)cbReq)
        {
            VMMDevRequestHeader *pReq = (VMMDevRequestHeader *)VbglR0PhysHeapAlloc((uint32_t)cbReq);
            AssertMsgReturn(pReq, ("VbglR0GRAlloc: no memory (cbReq=%u)\n", cbReq), VERR_NO_MEMORY);
            memset(pReq, 0xAA, cbReq);

            pReq->size        = (uint32_t)cbReq;
            pReq->version     = VMMDEV_REQUEST_HEADER_VERSION;
            pReq->requestType = enmReqType;
            pReq->rc          = VERR_GENERAL_FAILURE;
            pReq->reserved1   = 0;
#ifdef VBGL_VBOXGUEST
            pReq->fRequestor  = VMMDEV_REQUESTOR_KERNEL        | VMMDEV_REQUESTOR_USR_DRV
#else
            pReq->fRequestor  = VMMDEV_REQUESTOR_KERNEL        | VMMDEV_REQUESTOR_USR_DRV_OTHER
#endif

                              | VMMDEV_REQUESTOR_CON_DONT_KNOW | VMMDEV_REQUESTOR_TRUST_NOT_GIVEN;
            *ppReq = pReq;
            rc = VINF_SUCCESS;
        }
        else
        {
            dprintf(("VbglR0GRAlloc: Invalid parameter: ppReq=%p cbReq=%u\n", ppReq, cbReq));
            rc = VERR_INVALID_PARAMETER;
        }
    }
    return rc;
}

DECLR0VBGL(int) VbglR0GRPerform(VMMDevRequestHeader *pReq)
{
    int rc = vbglR0Enter();
    if (RT_SUCCESS(rc))
    {
        if (pReq)
        {
            RTCCPHYS PhysAddr = VbglR0PhysHeapGetPhysAddr(pReq);
            if (   PhysAddr != 0
                && PhysAddr < _4G) /* Port IO is 32 bit. */
            {
                ASMOutU32(g_vbgldata.portVMMDev + VMMDEV_PORT_OFF_REQUEST, (uint32_t)PhysAddr);
                /* Make the compiler aware that the host has changed memory. */
                ASMCompilerBarrier();
                rc = pReq->rc;
            }
            else
                rc = VERR_VBGL_INVALID_ADDR;
        }
        else
            rc = VERR_INVALID_PARAMETER;
    }
    return rc;
}

DECLR0VBGL(void) VbglR0GRFree(VMMDevRequestHeader *pReq)
{
    int rc = vbglR0Enter();
    if (RT_SUCCESS(rc))
        VbglR0PhysHeapFree(pReq);
}

