/* $Id: SysHlp.h $ */
/** @file
 * VBoxGuestLibR0 - System dependent helpers internal header.
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

#ifndef ___VBoxGuestLib_SysHlp_h
#define ___VBoxGuestLib_SysHlp_h

#include <iprt/types.h>

#ifdef RT_OS_WINDOWS
# undef PAGE_SIZE
# undef PAGE_SHIFT
# include <iprt/nt/ntddk.h>
/* XP DDK #defines ExFreePool to ExFreePoolWithTag. The latter does not exist on NT4, so...
 * The same for ExAllocatePool.
 */
# undef ExAllocatePool
# undef ExFreePool
#endif

typedef struct _VBGLDRIVER
{
#ifdef RT_OS_WINDOWS
    PDEVICE_OBJECT pDeviceObject;
    PFILE_OBJECT pFileObject;
#elif defined (RT_OS_OS2)
    uint32_t u32Session; /**< just for sanity checking. */
#else /* PORTME */
    void *pvOpaque;
#endif
} VBGLDRIVER;

int  vbglLockLinear(void **ppvCtx, void *pv, uint32_t cb, bool fWriteAccess, uint32_t fFlags);
void vbglUnlockLinear(void *pvCtx, void *pv, uint32_t cb);


#ifndef VBGL_VBOXGUEST

/**
 * Open VBoxGuest driver.
 *
 * @param pDriver      Pointer to the driver structure.
 *
 * @return VBox status code
 */
int vbglDriverOpen(VBGLDRIVER *pDriver);

/**
 * Answers whether the VBoxGuest driver is opened
 *
 * @param pDriver      Pointer to the driver structure.
 *
 * @return true - if opened, false - otherwise
 */
bool vbglDriverIsOpened(VBGLDRIVER *pDriver);

/**
 * Call VBoxGuest driver.
 *
 * @param pDriver      Pointer to the driver structure.
 * @param u32Function  Function code.
 * @param pvData       Pointer to supplied in/out data buffer.
 * @param cbData       Size of data buffer.
 *
 * @returns VBox status code
 */
int vbglDriverIOCtl(VBGLDRIVER *pDriver, uint32_t u32Function, void *pvData, uint32_t cbData);

/**
 * Close VBoxGuest driver.
 *
 * @param pDriver      Pointer to the driver structure.
 *
 * @returns VBox status code
 */
void vbglDriverClose(VBGLDRIVER *pDriver);

#endif

#endif

