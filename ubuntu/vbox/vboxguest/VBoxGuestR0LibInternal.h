/* $Id: VBoxGuestR0LibInternal.h $ */
/** @file
 * VBoxGuestLibR0 - Internal header.
 */

/*
 * Copyright (C) 2006-2017 Oracle Corporation
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

#ifndef ___VBoxGuestLib_VBoxGuestR0LibInternal_h
#define ___VBoxGuestLib_VBoxGuestR0LibInternal_h

/*
 * Define the private IDC handle structure before we include the VBoxGuestLib.h header.
 */
#include <iprt/types.h>
#include <iprt/assert.h>
RT_C_DECLS_BEGIN

# ifndef VBGL_VBOXGUEST
/**
 * The hidden part of VBGLIDCHANDLE.
 */
struct VBGLIDCHANDLEPRIVATE
{
    /** Pointer to the session handle. */
    void           *pvSession;
# if defined(RT_OS_WINDOWS) && (defined(___iprt_nt_ntddk_h___) || defined(___iprt_nt_nt_h___))
    /** Pointer to the NT device object. */
    PDEVICE_OBJECT  pDeviceObject;
    /** Pointer to the NT file object. */
    PFILE_OBJECT    pFileObject;
# elif defined(RT_OS_SOLARIS) && defined(_SYS_SUNLDI_H)
    /** LDI device handle to keep the device attached. */
    ldi_handle_t    hDev;
# endif
};
/** Indicate that the VBGLIDCHANDLEPRIVATE structure is present. */
# define VBGLIDCHANDLEPRIVATE_DECLARED 1
#endif

#include <VBox/VMMDev.h>
#include <VBox/VBoxGuest.h>
#include <VBox/VBoxGuestLib.h>

#ifdef VBGLIDCHANDLEPRIVATE_DECLARED
AssertCompile(RT_SIZEOFMEMB(VBGLIDCHANDLE, apvPadding) >= sizeof(struct VBGLIDCHANDLEPRIVATE));
#endif


/*
 * Native IDC functions.
 */
int VBOXCALL vbglR0IdcNativeOpen(PVBGLIDCHANDLE pHandle, PVBGLIOCIDCCONNECT pReq);
int VBOXCALL vbglR0IdcNativeClose(PVBGLIDCHANDLE pHandle, PVBGLIOCIDCDISCONNECT pReq);


/*
 * Deprecated logging macro
 */
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

/*
 * Lazy bird: OS/2 doesn't currently implement the RTSemMutex API in ring-0, so
 *  use a fast mutex instead.   Unlike Windows, the OS/2 implementation
 *  doesn't have any nasty side effects on IRQL-like context properties, so the
 *  fast mutexes on OS/2 are identical to normal mutexes except for the missing
 *  timeout aspec.  Fortunately we don't need timeouts here.
 */
#ifdef RT_OS_OS2
# define VBGLDATA_USE_FAST_MUTEX
#endif

struct _VBGLPHYSHEAPBLOCK;
typedef struct _VBGLPHYSHEAPBLOCK VBGLPHYSHEAPBLOCK;
struct _VBGLPHYSHEAPCHUNK;
typedef struct _VBGLPHYSHEAPCHUNK VBGLPHYSHEAPCHUNK;

#ifndef VBGL_VBOXGUEST
struct VBGLHGCMHANDLEDATA
{
    uint32_t fAllocated;
    VBGLIDCHANDLE IdcHandle;
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
    /** The IDC handle.  This is used for talking to the main driver. */
    VBGLIDCHANDLE IdcHandle;
    /** Mutex used to serialize IDC setup.   */
# ifdef VBGLDATA_USE_FAST_MUTEX
    RTSEMFASTMUTEX hMtxIdcSetup;
# else
    RTSEMMUTEX hMtxIdcSetup;
# endif
#endif
} VBGLDATA;


extern VBGLDATA g_vbgldata;

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
struct VBGLHGCMHANDLEDATA  *vbglR0HGCMHandleAlloc(void);
void                        vbglR0HGCMHandleFree(struct VBGLHGCMHANDLEDATA *pHandle);
#endif /* VBOX_WITH_HGCM */

#ifndef VBGL_VBOXGUEST
/**
 * Get the IDC handle to the main VBoxGuest driver.
 * @returns VERR_TRY_AGAIN if the main driver has not yet been loaded.
 */
int VBOXCALL vbglR0QueryIdcHandle(PVBGLIDCHANDLE *ppIdcHandle);
#endif

RT_C_DECLS_END

#endif /* !___VBoxGuestLib_VBoxGuestR0LibInternal_h */

