/** @file
 * Virtual Device for Guest <-> VMM/Host communication, Mixed Up Mess. (ADD,DEV)
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

#ifndef ___VBox_VMMDev2_h
#define ___VBox_VMMDev2_h

#include <iprt/assert.h>


/** @addtogroup grp_vmmdev
 * @{
 */


/**
 * Seamless mode.
 *
 * Used by VbglR3SeamlessWaitEvent
 *
 * @ingroup grp_vmmdev_req
 *
 * @todo DARN! DARN! DARN! Who forgot to do the 32-bit hack here???
 *       FIXME! XXX!
 *
 *       We will now have to carefully check how our compilers have treated this
 *       flag. If any are compressing it into a byte type, we'll have to check
 *       how the request memory is initialized. If we are 104% sure it's ok to
 *       expand it, we'll expand it. If not, we must redefine the field to a
 *       uint8_t and a 3 byte padding.
 */
typedef enum
{
    VMMDev_Seamless_Disabled         = 0,     /**< normal mode; entire guest desktop displayed. */
    VMMDev_Seamless_Visible_Region   = 1,     /**< visible region mode; only top-level guest windows displayed. */
    VMMDev_Seamless_Host_Window      = 2      /**< windowed mode; each top-level guest window is represented in a host window. */
} VMMDevSeamlessMode;

/**
 * CPU event types.
 *
 * Used by VbglR3CpuHotplugWaitForEvent
 *
 * @ingroup grp_vmmdev_req
 */
typedef enum
{
    VMMDevCpuEventType_Invalid  = 0,
    VMMDevCpuEventType_None     = 1,
    VMMDevCpuEventType_Plug     = 2,
    VMMDevCpuEventType_Unplug   = 3,
    VMMDevCpuEventType_SizeHack = 0x7fffffff
} VMMDevCpuEventType;

/**
 * HGCM service location types.
 * @ingroup grp_vmmdev_req
 */
typedef enum
{
    VMMDevHGCMLoc_Invalid    = 0,
    VMMDevHGCMLoc_LocalHost  = 1,
    VMMDevHGCMLoc_LocalHost_Existing = 2,
    VMMDevHGCMLoc_SizeHack   = 0x7fffffff
} HGCMServiceLocationType;
AssertCompileSize(HGCMServiceLocationType, 4);

/**
 * HGCM host service location.
 * @ingroup grp_vmmdev_req
 */
typedef struct
{
    char achName[128]; /**< This is really szName. */
} HGCMServiceLocationHost;
AssertCompileSize(HGCMServiceLocationHost, 128);

/**
 * HGCM service location.
 * @ingroup grp_vmmdev_req
 */
typedef struct HGCMSERVICELOCATION
{
    /** Type of the location. */
    HGCMServiceLocationType type;

    union
    {
        HGCMServiceLocationHost host;
    } u;
} HGCMServiceLocation;
AssertCompileSize(HGCMServiceLocation, 128+4);

/* forward declarations: */
struct VMMDevReqMousePointer;
struct VMMDevMemory;

/** @} */

#endif

