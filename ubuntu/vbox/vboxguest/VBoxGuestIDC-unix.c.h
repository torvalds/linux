/* $Rev: 109079 $ */
/** @file
 * VBoxGuest - Inter Driver Communication, unix implementation.
 *
 * This file is included by the platform specific source file.
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


/** @todo Use some header that we have in common with VBoxGuestLib.h... */
/** @todo fix DECLVBGL usage. */
RT_C_DECLS_BEGIN
DECLEXPORT(void *) VBOXCALL VBoxGuestIDCOpen(uint32_t *pu32Version);
DECLEXPORT(int) VBOXCALL VBoxGuestIDCClose(void *pvSession);
DECLEXPORT(int) VBOXCALL VBoxGuestIDCCall(void *pvSession, unsigned iCmd, void *pvData, size_t cbData, size_t *pcbDataReturned);
RT_C_DECLS_END


/**
 * Open a new IDC connection.
 *
 * @returns Opaque pointer to session object.
 * @param   pu32Version         Where to store VMMDev version.
 */
DECLEXPORT(void *) VBOXCALL VBoxGuestIDCOpen(uint32_t *pu32Version)
{
    PVBOXGUESTSESSION   pSession;
    int                 rc;
    LogFlow(("VBoxGuestIDCOpen: Version=%#x\n", pu32Version ? *pu32Version : 0));

    AssertPtrReturn(pu32Version, NULL);

#ifdef RT_OS_SOLARIS
    mutex_enter(&g_LdiMtx);
    if (!g_LdiHandle)
    {
        ldi_ident_t DevIdent = ldi_ident_from_anon();
        rc = ldi_open_by_name(VBOXGUEST_DEVICE_NAME, FREAD, kcred, &g_LdiHandle, DevIdent);
        ldi_ident_release(DevIdent);
        if (rc)
        {
            LogRel(("VBoxGuestIDCOpen: ldi_open_by_name failed. rc=%d\n", rc));
            mutex_exit(&g_LdiMtx);
            return NULL;
        }
    }
    ++g_cLdiOpens;
    mutex_exit(&g_LdiMtx);
#endif

    rc = VGDrvCommonCreateKernelSession(&g_DevExt, &pSession);
    if (RT_SUCCESS(rc))
    {
        *pu32Version = VMMDEV_VERSION;
        return pSession;
    }

#ifdef RT_OS_SOLARIS
    mutex_enter(&g_LdiMtx);
    if (g_cLdiOpens > 0)
        --g_cLdiOpens;
    if (   g_cLdiOpens == 0
        && g_LdiHandle)
    {
        ldi_close(g_LdiHandle, FREAD, kcred);
        g_LdiHandle = NULL;
    }
    mutex_exit(&g_LdiMtx);
#endif

    LogRel(("VBoxGuestIDCOpen: VGDrvCommonCreateKernelSession failed. rc=%d\n", rc));
    return NULL;
}


/**
 * Close an IDC connection.
 *
 * @returns VBox error code.
 * @param   pvSession           Opaque pointer to the session object.
 */
DECLEXPORT(int) VBOXCALL VBoxGuestIDCClose(void *pvSession)
{
    PVBOXGUESTSESSION pSession = (PVBOXGUESTSESSION)pvSession;
    LogFlow(("VBoxGuestIDCClose: pvSession=%p\n", pvSession));

    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    VGDrvCommonCloseSession(&g_DevExt, pSession);

#ifdef RT_OS_SOLARIS
    mutex_enter(&g_LdiMtx);
    if (g_cLdiOpens > 0)
        --g_cLdiOpens;
    if (   g_cLdiOpens == 0
        && g_LdiHandle)
    {
        ldi_close(g_LdiHandle, FREAD, kcred);
        g_LdiHandle = NULL;
    }
    mutex_exit(&g_LdiMtx);
#endif

    return VINF_SUCCESS;
}


/**
 * Perform an IDC call.
 *
 * @returns VBox error code.
 * @param   pvSession           Opaque pointer to the session.
 * @param   iCmd                Requested function.
 * @param   pvData              IO data buffer.
 * @param   cbData              Size of the data buffer.
 * @param   pcbDataReturned     Where to store the amount of returned data.
 */
DECLEXPORT(int) VBOXCALL VBoxGuestIDCCall(void *pvSession, unsigned iCmd, void *pvData, size_t cbData, size_t *pcbDataReturned)
{
    PVBOXGUESTSESSION pSession = (PVBOXGUESTSESSION)pvSession;
    LogFlow(("VBoxGuestIDCCall: %pvSession=%p Cmd=%u pvData=%p cbData=%d\n", pvSession, iCmd, pvData, cbData));

    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertMsgReturn(pSession->pDevExt == &g_DevExt, ("SC: %p != %p\n", pSession->pDevExt, &g_DevExt), VERR_INVALID_HANDLE);

    return VGDrvCommonIoCtl(iCmd, &g_DevExt, pSession, pvData, cbData, pcbDataReturned);
}

