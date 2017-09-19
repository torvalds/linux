/* $Id: SysHlp.cpp $ */
/** @file
 * VBoxGuestLibR0 - IDC with VBoxGuest and HGCM helpers.
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

#define LOG_GROUP LOG_GROUP_HGCM
#include <VBox/log.h>

#include <VBox/VBoxGuestLib.h>
#include "SysHlp.h"

#include <iprt/assert.h>

#ifdef VBGL_VBOXGUEST

#if !defined (RT_OS_WINDOWS)
# include <iprt/memobj.h>
# include <iprt/mem.h>
#endif


/**
 * Internal worker for locking a range of linear addresses.
 *
 * @returns VBox status code.
 * @param   ppvCtx          Where to store context data.
 * @param   pv              The start of the range.
 * @param   u32Size         The size of the range.
 * @param   fWriteAccess    Lock for read-write (true) or readonly (false).
 * @param   fFlags          HGCM call flags, VBGLR0_HGCM_F_XXX.
 */
int vbglLockLinear(void **ppvCtx, void *pv, uint32_t u32Size, bool fWriteAccess, uint32_t fFlags)
{
    int         rc      = VINF_SUCCESS;
#ifndef RT_OS_WINDOWS
    RTR0MEMOBJ  MemObj  = NIL_RTR0MEMOBJ;
    uint32_t    fAccess = RTMEM_PROT_READ | (fWriteAccess ? RTMEM_PROT_WRITE : 0);
#endif

    /* Zero size buffers shouldn't be locked. */
    if (u32Size == 0)
    {
        Assert(pv == NULL);
#ifdef RT_OS_WINDOWS
        *ppvCtx = NULL;
#else
        *ppvCtx = NIL_RTR0MEMOBJ;
#endif
        return VINF_SUCCESS;
    }

    /** @todo just use IPRT here. the extra allocation shouldn't matter much...
     *        Then we can move all this up one level even. */
#ifdef RT_OS_WINDOWS
    PMDL pMdl = IoAllocateMdl(pv, u32Size, FALSE, FALSE, NULL);

    if (pMdl == NULL)
    {
        rc = VERR_NOT_SUPPORTED;
        AssertMsgFailed(("IoAllocateMdl %p %x failed!!\n", pv, u32Size));
    }
    else
    {
        __try {
            /* Calls to MmProbeAndLockPages must be enclosed in a try/except block. */
            RT_NOREF1(fFlags);  /** @todo fFlags on windows */
            MmProbeAndLockPages(pMdl,
                                /** @todo (fFlags & VBGLR0_HGCMCALL_F_MODE_MASK) == VBGLR0_HGCMCALL_F_USER? UserMode: KernelMode */
                                KernelMode,
                                (fWriteAccess) ? IoModifyAccess : IoReadAccess);

            *ppvCtx = pMdl;

        } __except(EXCEPTION_EXECUTE_HANDLER) {

            IoFreeMdl(pMdl);
            /** @todo  */
            rc = VERR_INVALID_PARAMETER;
            AssertMsgFailed(("MmProbeAndLockPages %p %x failed!!\n", pv, u32Size));
        }
    }

#else
    /*
     * Lock depending on context.
     *
     * Note: We will later use the memory object here to convert the HGCM
     *       linear buffer parameter into a physical page list. This is why
     *       we lock both kernel pages on all systems, even those where we
     *       know they aren't pageable.
     */
    if ((fFlags & VBGLR0_HGCMCALL_F_MODE_MASK) == VBGLR0_HGCMCALL_F_USER)
        rc = RTR0MemObjLockUser(&MemObj, (RTR3PTR)pv, u32Size, fAccess, NIL_RTR0PROCESS);
    else
        rc = RTR0MemObjLockKernel(&MemObj, pv, u32Size, fAccess);
    if (RT_SUCCESS(rc))
        *ppvCtx = MemObj;
    else
        *ppvCtx = NIL_RTR0MEMOBJ;

#endif

    return rc;
}

void vbglUnlockLinear(void *pvCtx, void *pv, uint32_t u32Size)
{
#ifdef RT_OS_WINDOWS
    PMDL pMdl = (PMDL)pvCtx;

    Assert(pMdl);
    if (pMdl != NULL)
    {
        MmUnlockPages(pMdl);
        IoFreeMdl(pMdl);
    }

#else
    RTR0MEMOBJ MemObj = (RTR0MEMOBJ)pvCtx;
    int rc = RTR0MemObjFree(MemObj, false);
    AssertRC(rc);

#endif

    NOREF(pv);
    NOREF(u32Size);
}

#else  /* !VBGL_VBOXGUEST */

# ifdef RT_OS_OS2
#  include <VBox/VBoxGuest.h> /* for VBOXGUESTOS2IDCCONNECT */
RT_C_DECLS_BEGIN
/*
 * On OS/2 we'll do the connecting in the assembly code of the
 * client driver, exporting a g_VBoxGuestIDC symbol containing
 * the connection information obtained from the 16-bit IDC.
 */
extern VBOXGUESTOS2IDCCONNECT g_VBoxGuestIDC;
RT_C_DECLS_END
# endif

# if !defined(RT_OS_OS2) \
  && !defined(RT_OS_WINDOWS)
RT_C_DECLS_BEGIN
extern DECLVBGL(void *) VBoxGuestIDCOpen(uint32_t *pu32Version);
extern DECLVBGL(void)   VBoxGuestIDCClose(void *pvOpaque);
extern DECLVBGL(int)    VBoxGuestIDCCall(void *pvOpaque, unsigned int iCmd, void *pvData, size_t cbSize, size_t *pcbReturn);
RT_C_DECLS_END
# endif

bool vbglDriverIsOpened(VBGLDRIVER *pDriver)
{
# ifdef RT_OS_WINDOWS
    return pDriver->pFileObject != NULL;
# elif defined (RT_OS_OS2)
    return pDriver->u32Session != UINT32_MAX && pDriver->u32Session != 0;
# else
    return pDriver->pvOpaque != NULL;
# endif
}

int vbglDriverOpen(VBGLDRIVER *pDriver)
{
# ifdef RT_OS_WINDOWS
    UNICODE_STRING uszDeviceName;
    RtlInitUnicodeString(&uszDeviceName, L"\\Device\\VBoxGuest");

    PDEVICE_OBJECT pDeviceObject = NULL;
    PFILE_OBJECT pFileObject = NULL;

    NTSTATUS rc = IoGetDeviceObjectPointer(&uszDeviceName, FILE_ALL_ACCESS, &pFileObject, &pDeviceObject);
    if (NT_SUCCESS(rc))
    {
        Log(("vbglDriverOpen VBoxGuest successful pDeviceObject=%x\n", pDeviceObject));
        pDriver->pDeviceObject = pDeviceObject;
        pDriver->pFileObject = pFileObject;
        return VINF_SUCCESS;
    }
    /** @todo return RTErrConvertFromNtStatus(rc)! */
    Log(("vbglDriverOpen VBoxGuest failed with ntstatus=%x\n", rc));
    return rc;

# elif defined (RT_OS_OS2)
    /*
     * Just check whether the connection was made or not.
     */
    if (   g_VBoxGuestIDC.u32Version == VMMDEV_VERSION
        && RT_VALID_PTR(g_VBoxGuestIDC.u32Session)
        && RT_VALID_PTR(g_VBoxGuestIDC.pfnServiceEP))
    {
        pDriver->u32Session = g_VBoxGuestIDC.u32Session;
        return VINF_SUCCESS;
    }
    pDriver->u32Session = UINT32_MAX;
    Log(("vbglDriverOpen: failed\n"));
    return VERR_FILE_NOT_FOUND;

# else
    uint32_t u32VMMDevVersion;
    pDriver->pvOpaque = VBoxGuestIDCOpen(&u32VMMDevVersion);
    if (   pDriver->pvOpaque
        && u32VMMDevVersion == VMMDEV_VERSION)
        return VINF_SUCCESS;

    Log(("vbglDriverOpen: failed\n"));
    return VERR_FILE_NOT_FOUND;
# endif
}

# ifdef RT_OS_WINDOWS
static NTSTATUS vbglDriverIOCtlCompletion(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp, IN PVOID Context)
{
    RT_NOREF2(DeviceObject, Irp);
    Log(("VBGL completion %x\n", Irp));

    KEVENT *pEvent = (KEVENT *)Context;
    KeSetEvent(pEvent, IO_NO_INCREMENT, FALSE);

    return STATUS_MORE_PROCESSING_REQUIRED;
}
# endif

int vbglDriverIOCtl(VBGLDRIVER *pDriver, uint32_t u32Function, void *pvData, uint32_t cbData)
{
    Log(("vbglDriverIOCtl: pDriver: %p, Func: %x, pvData: %p, cbData: %d\n", pDriver, u32Function, pvData, cbData));

# ifdef RT_OS_WINDOWS
    KEVENT Event;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    /* Have to use the IoAllocateIRP method because this code is generic and
     * must work in any thread context.
     * The IoBuildDeviceIoControlRequest, which was used here, does not work
     * when APCs are disabled, for example.
     */
    PIRP irp = IoAllocateIrp(pDriver->pDeviceObject->StackSize, FALSE);

    Log(("vbglDriverIOCtl: irp %p, IRQL = %d\n", irp, KeGetCurrentIrql()));

    if (irp == NULL)
    {
        Log(("vbglDriverIOCtl: IRP allocation failed!\n"));
        return VERR_NO_MEMORY;
    }

    /*
     * Setup the IRP_MJ_DEVICE_CONTROL IRP.
     */

    PIO_STACK_LOCATION nextStack = IoGetNextIrpStackLocation(irp);

    nextStack->MajorFunction = IRP_MJ_DEVICE_CONTROL;
    nextStack->MinorFunction = 0;
    nextStack->DeviceObject = pDriver->pDeviceObject;
    nextStack->Parameters.DeviceIoControl.OutputBufferLength = cbData;
    nextStack->Parameters.DeviceIoControl.InputBufferLength = cbData;
    nextStack->Parameters.DeviceIoControl.IoControlCode = u32Function;
    nextStack->Parameters.DeviceIoControl.Type3InputBuffer = pvData;

    irp->AssociatedIrp.SystemBuffer = pvData; /* Output buffer. */
    irp->MdlAddress = NULL;

    /* A completion routine is required to signal the Event. */
    IoSetCompletionRoutine(irp, vbglDriverIOCtlCompletion, &Event, TRUE, TRUE, TRUE);

    NTSTATUS rc = IoCallDriver(pDriver->pDeviceObject, irp);

    if (NT_SUCCESS (rc))
    {
        /* Wait the event to be signalled by the completion routine. */
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);

        rc = irp->IoStatus.Status;

        Log(("vbglDriverIOCtl: wait completed IRQL = %d\n", KeGetCurrentIrql()));
    }

    IoFreeIrp(irp);

    if (rc != STATUS_SUCCESS)
        Log(("vbglDriverIOCtl: ntstatus=%x\n", rc));

    if (NT_SUCCESS(rc))
        return VINF_SUCCESS;
    if (rc == STATUS_INVALID_PARAMETER)
        return VERR_INVALID_PARAMETER;
    if (rc == STATUS_INVALID_BUFFER_SIZE)
        return VERR_OUT_OF_RANGE;
    return VERR_VBGL_IOCTL_FAILED;

# elif defined (RT_OS_OS2)
    if (    pDriver->u32Session
        &&  pDriver->u32Session == g_VBoxGuestIDC.u32Session)
        return g_VBoxGuestIDC.pfnServiceEP(pDriver->u32Session, u32Function, pvData, cbData, NULL);

    Log(("vbglDriverIOCtl: No connection\n"));
    return VERR_WRONG_ORDER;

# else
    return VBoxGuestIDCCall(pDriver->pvOpaque, u32Function, pvData, cbData, NULL);
# endif
}

void vbglDriverClose(VBGLDRIVER *pDriver)
{
# ifdef RT_OS_WINDOWS
    Log(("vbglDriverClose pDeviceObject=%x\n", pDriver->pDeviceObject));
    ObDereferenceObject(pDriver->pFileObject);
    pDriver->pFileObject = NULL;
    pDriver->pDeviceObject = NULL;

# elif defined (RT_OS_OS2)
    pDriver->u32Session = 0;

# else
    VBoxGuestIDCClose(pDriver->pvOpaque);
    pDriver->pvOpaque = NULL;
# endif
}

#endif /* !VBGL_VBOXGUEST */

