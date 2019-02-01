/* $Id: VBoxGuestR0LibIdc.cpp $ */
/** @file
 * VBoxGuestLib - Ring-0 Support Library for VBoxGuest, IDC.
 */

/*
 * Copyright (C) 2008-2019 Oracle Corporation
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
#include <iprt/errcore.h>
#include <VBox/VBoxGuest.h>
/*#include <iprt/asm.h>*/

#ifdef VBGL_VBOXGUEST
# error "This file shouldn't be part of the VBoxGuestR0LibBase library that is linked into VBoxGuest.  It's client code."
#endif



/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/*static PVBGLIDCHANDLE volatile g_pMainHandle = NULL;*/


/**
 * Opens the IDC interface of the support driver.
 *
 * This will perform basic version negotiations and fail if the
 * minimum requirements aren't met.
 *
 * @returns VBox status code.
 * @param   pHandle             The handle structure (output).
 * @param   uReqVersion         The requested version. Pass 0 for default.
 * @param   uMinVersion         The minimum required version. Pass 0 for default.
 * @param   puSessionVersion    Where to store the session version. Optional.
 * @param   puDriverVersion     Where to store the session version. Optional.
 * @param   puDriverRevision    Where to store the SVN revision of the driver. Optional.
 */
DECLR0VBGL(int) VbglR0IdcOpen(PVBGLIDCHANDLE pHandle, uint32_t uReqVersion, uint32_t uMinVersion,
                              uint32_t *puSessionVersion, uint32_t *puDriverVersion, uint32_t *puDriverRevision)
{
    unsigned uDefaultMinVersion;
    VBGLIOCIDCCONNECT Req;
    int rc;

    /*
     * Validate and set failure return values.
     */
    AssertPtrReturn(pHandle, VERR_INVALID_POINTER);
    pHandle->s.pvSession = NULL;

    AssertPtrNullReturn(puSessionVersion, VERR_INVALID_POINTER);
    if (puSessionVersion)
        *puSessionVersion = 0;

    AssertPtrNullReturn(puDriverVersion, VERR_INVALID_POINTER);
    if (puDriverVersion)
        *puDriverVersion = 0;

    AssertPtrNullReturn(puDriverRevision, VERR_INVALID_POINTER);
    if (puDriverRevision)
        *puDriverRevision = 0;

    AssertReturn(!uMinVersion || (uMinVersion & UINT32_C(0xffff0000)) == (VBGL_IOC_VERSION & UINT32_C(0xffff0000)), VERR_INVALID_PARAMETER);
    AssertReturn(!uReqVersion || (uReqVersion & UINT32_C(0xffff0000)) == (VBGL_IOC_VERSION & UINT32_C(0xffff0000)), VERR_INVALID_PARAMETER);

    /*
     * Handle default version input and enforce minimum requirements made
     * by this library.
     *
     * The clients will pass defaults (0), and only in the case that some
     * special API feature was just added will they set an actual version.
     * So, this is the place where can easily enforce a minimum IDC version
     * on bugs and similar. It corresponds a bit to what SUPR3Init is
     * responsible for.
     */
    uDefaultMinVersion = VBGL_IOC_VERSION & UINT32_C(0xffff0000);
    if (!uMinVersion || uMinVersion < uDefaultMinVersion)
        uMinVersion = uDefaultMinVersion;
    if (!uReqVersion || uReqVersion < uDefaultMinVersion)
        uReqVersion = uDefaultMinVersion;

    /*
     * Setup the connect request packet and call the OS specific function.
     */
    VBGLREQHDR_INIT(&Req.Hdr, IDC_CONNECT);
    Req.u.In.u32MagicCookie = VBGL_IOCTL_IDC_CONNECT_MAGIC_COOKIE;
    Req.u.In.uMinVersion = uMinVersion;
    Req.u.In.uReqVersion = uReqVersion;
    Req.u.In.uReserved = 0;
    rc = vbglR0IdcNativeOpen(pHandle, &Req);
    if (RT_SUCCESS(rc))
        rc = Req.Hdr.rc;
    if (RT_SUCCESS(rc))
    {
        pHandle->s.pvSession = Req.u.Out.pvSession;
        if (puSessionVersion)
            *puSessionVersion = Req.u.Out.uSessionVersion;
        if (puDriverVersion)
            *puDriverVersion = Req.u.Out.uDriverVersion;
        if (puDriverRevision)
            *puDriverRevision = Req.u.Out.uDriverRevision;

        /*
         * We don't really trust anyone, make sure the returned
         * session and version values actually makes sense.
         */
        if (    RT_VALID_PTR(Req.u.Out.pvSession)
            &&  Req.u.Out.uSessionVersion >= uMinVersion
            &&  (Req.u.Out.uSessionVersion & UINT32_C(0xffff0000)) == (VBGL_IOC_VERSION & UINT32_C(0xffff0000)))
        {
            /*ASMAtomicCmpXchgPtr(&g_pMainHandle, pHandle, NULL);*/
            return rc;
        }

        AssertMsgFailed(("pSession=%p uSessionVersion=0x%x (r%u)\n", Req.u.Out.pvSession, Req.u.Out.uSessionVersion, Req.u.Out.uDriverRevision));
        rc = VERR_VERSION_MISMATCH;
        VbglR0IdcClose(pHandle);
    }

    return rc;
}


/**
 * Closes a IDC connection established by VbglR0IdcOpen.
 *
 * @returns VBox status code.
 * @param   pHandle     The IDC handle.
 */
DECLR0VBGL(int) VbglR0IdcClose(PVBGLIDCHANDLE pHandle)
{
    VBGLIOCIDCDISCONNECT Req;
    int rc;

    /*
     * Catch closed handles and check that the session is valid.
     */
    AssertPtrReturn(pHandle, VERR_INVALID_POINTER);
    if (!pHandle->s.pvSession)
        return VERR_INVALID_HANDLE;
    AssertPtrReturn(pHandle->s.pvSession, VERR_INVALID_HANDLE);

    /*
     * Create the request and hand it to the OS specific code.
     */
    VBGLREQHDR_INIT(&Req.Hdr, IDC_DISCONNECT);
    Req.u.In.pvSession = pHandle->s.pvSession;
    rc = vbglR0IdcNativeClose(pHandle, &Req);
    if (RT_SUCCESS(rc))
        rc = Req.Hdr.rc;
    if (RT_SUCCESS(rc))
    {
        pHandle->s.pvSession = NULL;
        /*ASMAtomicCmpXchgPtr(&g_pMainHandle, NULL, pHandle);*/
    }
    return rc;
}


/**
 * Makes an IDC call, returning the request status.
 *
 * @returns VBox status code.  Request status if the I/O control succeeds,
 *          otherwise the I/O control failure status.
 * @param   pHandle             The IDC handle.
 * @param   uReq                The request number.
 * @param   pReqHdr             The request header.
 * @param   cbReq               The request size.
 */
DECLR0VBGL(int) VbglR0IdcCall(PVBGLIDCHANDLE pHandle, uintptr_t uReq, PVBGLREQHDR pReqHdr, uint32_t cbReq)
{
    int rc = VbglR0IdcCallRaw(pHandle, uReq, pReqHdr, cbReq);
    if (RT_SUCCESS(rc))
        rc = pReqHdr->rc;
    return rc;
}

