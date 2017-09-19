/* $Id: VBGLInternal.h $ */
/** @file
 * VBoxGuestLibR0 - Internal header.
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

#ifndef ___VBoxGuestLib_VBGLInternal_h
#define ___VBoxGuestLib_VBGLInternal_h

#include <VBox/VMMDev.h>
#include <VBox/VBoxGuest.h>
#include <VBox/VBoxGuestLib.h>

#include <VBox/log.h>


#ifdef RT_OS_WINDOWS /** @todo dprintf() -> Log() */
# if (defined(DEBUG) && !defined(NO_LOGGING)) || defined(LOG_ENABLED)
#  define dprintf(a) RTLogBackdoorPrintf a
# else
#  define dprintf(a) do {} while (0)
# endif
#else
# define dprintf(a) Log(a)
#endif

#include "SysHlp.h"

struct _VBGLPHYSHEAPBLOCK;
typedef struct _VBGLPHYSHEAPBLOCK VBGLPHYSHEAPBLOCK;
struct _VBGLPHYSHEAPCHUNK;
typedef struct _VBGLPHYSHEAPCHUNK VBGLPHYSHEAPCHUNK;

#ifndef VBGL_VBOXGUEST
struct VBGLHGCMHANDLEDATA
{
    uint32_t fAllocated;
    VBGLDRIVER driver;
};
#endif

enum VbglLibStatus
{
    VbglStatusNotInitialized = 0,
    VbglStatusInitializing,
    VbglStatusReady
};

/**
 * Global VBGL ring-0 data.
 * Lives in VbglR0Init.cpp.
 */
typedef struct VBGLDATA
{
    enum VbglLibStatus status;

    RTIOPORT portVMMDev;

    VMMDevMemory *pVMMDevMemory;

    /**
     * Physical memory heap data.
     * @{
     */

    VBGLPHYSHEAPBLOCK *pFreeBlocksHead;
    VBGLPHYSHEAPBLOCK *pAllocBlocksHead;
    VBGLPHYSHEAPCHUNK *pChunkHead;

    RTSEMFASTMUTEX mutexHeap;
    /** @} */

    /**
     * The host version data.
     */
    VMMDevReqHostVersion hostVersion;


#ifndef VBGL_VBOXGUEST
    /**
     * Handle for the main driver instance.
     * @{
     */

    RTSEMMUTEX mutexDriverInit;

    VBGLDRIVER driver;

    /** @} */

    /**
     * Fast heap for HGCM handles data.
     * @{
     */

    RTSEMFASTMUTEX mutexHGCMHandle;

    struct VBGLHGCMHANDLEDATA aHGCMHandleData[64];

    /** @} */
#endif
} VBGLDATA;


#ifndef VBGL_DECL_DATA
extern VBGLDATA g_vbgldata;
#endif

/**
 * Internal macro for checking whether we can pass physical page lists to the
 * host.
 *
 * ASSUMES that vbglR0Enter has been called already.
 *
 * @param   a_fLocked       For the windows shared folders workarounds.
 *
 * @remarks Disabled the PageList feature for locked memory on Windows,
 *          because a new MDL is created by VBGL to get the page addresses
 *          and the pages from the MDL are marked as dirty when they should not.
 */
#if defined(RT_OS_WINDOWS)
# define VBGLR0_CAN_USE_PHYS_PAGE_LIST(a_fLocked) \
    ( !(a_fLocked) && (g_vbgldata.hostVersion.features & VMMDEV_HVF_HGCM_PHYS_PAGE_LIST) )
#else
# define VBGLR0_CAN_USE_PHYS_PAGE_LIST(a_fLocked) \
    ( !!(g_vbgldata.hostVersion.features & VMMDEV_HVF_HGCM_PHYS_PAGE_LIST) )
#endif

int vbglR0Enter (void);

#ifdef VBOX_WITH_HGCM
# ifndef VBGL_VBOXGUEST
int                         vbglR0HGCMInit(void);
int                         vbglR0HGCMTerminate(void);
# endif
struct VBGLHGCMHANDLEDATA  *vbglHGCMHandleAlloc(void);
void                        vbglHGCMHandleFree(struct VBGLHGCMHANDLEDATA *pHandle);
#endif /* VBOX_WITH_HGCM */

#ifndef VBGL_VBOXGUEST
/**
 * Get a handle to the main VBoxGuest driver.
 * @returns VERR_TRY_AGAIN if the main driver has not yet been loaded.
 */
int vbglGetDriver(VBGLDRIVER **ppDriver);
#endif

#endif /* !___VBoxGuestLib_VBGLInternal_h */

