/** @file
 * VBoxGuest - VirtualBox Guest Additions, Core Types.
 *
 * This contains types that are used both in the VBoxGuest I/O control interface
 * and the VBoxGuestLib.  The goal is to avoid having to include VBoxGuest.h
 * everwhere VBoxGuestLib.h is used.
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


#ifndef ___VBoxGuestCoreTypes_h
#define ___VBoxGuestCoreTypes_h

#include <iprt/types.h>
#include <iprt/assertcompile.h>

/** @addtogroup grp_vboxguest
 * @{ */

/**
 * Common in/out header.
 *
 * This is a copy/mirror of VMMDevRequestHeader to prevent duplicating data and
 * needing to verify things multiple times.  For that reason this differs a bit
 * from SUPREQHDR.
 *
 * @sa VMMDevRequestHeader
 */
typedef struct VBGLREQHDR
{
    /** IN: The request input size, and output size if cbOut is zero.
     * @sa VMMDevRequestHeader::size  */
    uint32_t        cbIn;
    /** IN: Structure version (VBGLREQHDR_VERSION)
     * @sa VMMDevRequestHeader::version */
    uint32_t        uVersion;
    /** IN: The VMMDev request type, set to VBGLREQHDR_TYPE_DEFAULT unless this is a
     * kind of VMMDev request.
     * @sa VMMDevRequestType, VMMDevRequestHeader::requestType */
    uint32_t        uType;
    /** OUT: The VBox status code of the operation, out direction only. */
    int32_t         rc;
    /** IN: The output size.  This is optional - set to zero to use cbIn as the
     * output size. */
    uint32_t        cbOut;
    /** Reserved, MBZ. */
    uint32_t        uReserved;
} VBGLREQHDR;
AssertCompileSize(VBGLREQHDR, 24);
/** Pointer to a IOC header. */
typedef VBGLREQHDR RT_FAR *PVBGLREQHDR;

/** Version of VMMDevRequestHeader structure. */
#define VBGLREQHDR_VERSION          UINT32_C(0x10001)
/** Default request type.  Use this for non-VMMDev requests. */
#define VBGLREQHDR_TYPE_DEFAULT     UINT32_C(0)

/** Initialize a VBGLREQHDR structure for a fixed size I/O control call.
 * @param   a_pHdr      Pointer to the header to initialize.
 * @param   a_IOCtl     The base I/O control name, no VBGL_IOCTL_ prefix.  We
 *                      have to skip the prefix to avoid it getting expanded
 *                      before we append _SIZE_IN and _SIZE_OUT to it.
 */
#define VBGLREQHDR_INIT(a_pHdr, a_IOCtl) \
    VBGLREQHDR_INIT_EX(a_pHdr, RT_CONCAT3(VBGL_IOCTL_,a_IOCtl,_SIZE_IN), RT_CONCAT3(VBGL_IOCTL_,a_IOCtl,_SIZE_OUT))
/** Initialize a VBGLREQHDR structure, extended version. */
#define VBGLREQHDR_INIT_EX(a_pHdr, a_cbIn, a_cbOut) \
    do { \
        (a_pHdr)->cbIn      = (uint32_t)(a_cbIn); \
        (a_pHdr)->uVersion  = VBGLREQHDR_VERSION; \
        (a_pHdr)->uType     = VBGLREQHDR_TYPE_DEFAULT; \
        (a_pHdr)->rc        = VERR_INTERNAL_ERROR; \
        (a_pHdr)->cbOut     = (uint32_t)(a_cbOut); \
        (a_pHdr)->uReserved = 0; \
    } while (0)
/** Initialize a VBGLREQHDR structure for a VMMDev request.
 * Same as VMMDEV_REQ_HDR_INIT().  */
#define VBGLREQHDR_INIT_VMMDEV(a_pHdr, a_cb, a_enmType) \
    do { \
        (a_pHdr)->cbIn      = (a_cb); \
        (a_pHdr)->uVersion  = VBGLREQHDR_VERSION; \
        (a_pHdr)->uType     = (a_enmType); \
        (a_pHdr)->rc        = VERR_INTERNAL_ERROR; \
        (a_pHdr)->cbOut     = 0; \
        (a_pHdr)->uReserved = 0; \
    } while (0)


/**
 * For VBGL_IOCTL_HGCM_CALL and VBGL_IOCTL_HGCM_CALL_WITH_USER_DATA.
 *
 * @note This is used by alot of HGCM call structures.
 */
typedef struct VBGLIOCHGCMCALL
{
    /** Common header. */
    VBGLREQHDR  Hdr;
    /** Input: The id of the caller. */
    uint32_t    u32ClientID;
    /** Input: Function number. */
    uint32_t    u32Function;
    /** Input: How long to wait (milliseconds) for completion before cancelling the
     * call.  This is ignored if not a VBGL_IOCTL_HGCM_CALL_TIMED or
     * VBGL_IOCTL_HGCM_CALL_TIMED_32 request. */
    uint32_t    cMsTimeout;
    /** Input: Whether a timed call is interruptible (ring-0 only).  This is ignored
     * if not a VBGL_IOCTL_HGCM_CALL_TIMED or VBGL_IOCTL_HGCM_CALL_TIMED_32
     * request, or if made from user land. */
    bool        fInterruptible;
    /** Explicit padding, MBZ. */
    uint8_t     bReserved;
    /** Input: How many parameters following this structure.
     *
     * The parameters are either HGCMFunctionParameter64 or HGCMFunctionParameter32,
     * depending on whether we're receiving a 64-bit or 32-bit request.
     *
     * The current maximum is 61 parameters (given a 1KB max request size,
     * and a 64-bit parameter size of 16 bytes).
     *
     * @note This information is duplicated by Hdr.cbIn, but it's currently too much
     *       work to eliminate this. */
    uint16_t    cParms;
    /* Parameters follow in form HGCMFunctionParameter aParms[cParms] */
} VBGLIOCHGCMCALL, RT_FAR *PVBGLIOCHGCMCALL;
AssertCompileSize(VBGLIOCHGCMCALL, 24 + 16);
typedef VBGLIOCHGCMCALL const RT_FAR *PCVBGLIOCHGCMCALL;

/**
 * Initialize a HGCM header (VBGLIOCHGCMCALL) for a non-timed call.
 *
 * @param   a_pHdr          The header to initalize.
 * @param   a_idClient      The client connection ID to call thru.
 * @param   a_idFunction    The function we're calling
 * @param   a_cParameters   Number of parameters.
 */
# define VBGL_HGCM_HDR_INIT(a_pHdr, a_idClient, a_idFunction, a_cParameters) \
    do { \
        VBGLREQHDR_INIT_EX(&(a_pHdr)->Hdr, \
                           sizeof(VBGLIOCHGCMCALL) + (a_cParameters) * sizeof(HGCMFunctionParameter), \
                           sizeof(VBGLIOCHGCMCALL) + (a_cParameters) * sizeof(HGCMFunctionParameter)); \
        (a_pHdr)->u32ClientID    = (a_idClient); \
        (a_pHdr)->u32Function    = (a_idFunction); \
        (a_pHdr)->cMsTimeout     = RT_INDEFINITE_WAIT; \
        (a_pHdr)->fInterruptible = true; \
        (a_pHdr)->bReserved      = 0; \
        (a_pHdr)->cParms         = (a_cParameters); \
    } while (0)

/**
 * Initialize a HGCM header (VBGLIOCHGCMCALL) for a non-timed call, custom size.
 *
 * This is usually only needed when appending page lists to the call.
 *
 * @param   a_pHdr          The header to initalize.
 * @param   a_idClient      The client connection ID to call thru.
 * @param   a_idFunction    The function we're calling
 * @param   a_cParameters   Number of parameters.
 * @param   a_cbReq         The request size.
 */
# define VBGL_HGCM_HDR_INIT_EX(a_pHdr, a_idClient, a_idFunction, a_cParameters, a_cbReq) \
    do { \
        Assert((a_cbReq) >= sizeof(VBGLIOCHGCMCALL) + (a_cParameters) * sizeof(HGCMFunctionParameter)); \
        VBGLREQHDR_INIT_EX(&(a_pHdr)->Hdr, (a_cbReq), (a_cbReq)); \
        (a_pHdr)->u32ClientID    = (a_idClient); \
        (a_pHdr)->u32Function    = (a_idFunction); \
        (a_pHdr)->cMsTimeout     = RT_INDEFINITE_WAIT; \
        (a_pHdr)->fInterruptible = true; \
        (a_pHdr)->bReserved      = 0; \
        (a_pHdr)->cParms         = (a_cParameters); \
    } while (0)

/**
 * Initialize a HGCM header (VBGLIOCHGCMCALL), with timeout (interruptible).
 *
 * @param   a_pHdr          The header to initalize.
 * @param   a_idClient      The client connection ID to call thru.
 * @param   a_idFunction    The function we're calling
 * @param   a_cParameters   Number of parameters.
 * @param   a_cMsTimeout    The timeout in milliseconds.
 */
# define VBGL_HGCM_HDR_INIT_TIMED(a_pHdr, a_idClient, a_idFunction, a_cParameters, a_cMsTimeout) \
    do { \
        (a_pHdr)->u32ClientID    = (a_idClient); \
        (a_pHdr)->u32Function    = (a_idFunction); \
        (a_pHdr)->cMsTimeout     = (a_cMsTimeout); \
        (a_pHdr)->fInterruptible = true; \
        (a_pHdr)->bReserved      = 0; \
        (a_pHdr)->cParms         = (a_cParameters); \
    } while (0)

/** Get the pointer to the first HGCM parameter.  */
# define VBGL_HGCM_GET_CALL_PARMS(a_pInfo)   ( (HGCMFunctionParameter   *)((uint8_t *)(a_pInfo) + sizeof(VBGLIOCHGCMCALL)) )
/** Get the pointer to the first HGCM parameter in a 32-bit request.  */
# define VBGL_HGCM_GET_CALL_PARMS32(a_pInfo) ( (HGCMFunctionParameter32 *)((uint8_t *)(a_pInfo) + sizeof(VBGLIOCHGCMCALL)) )


/**
 * Mouse event noticification callback function.
 * @param   pvUser      Argument given when setting the callback.
 */
typedef DECLCALLBACK(void) FNVBOXGUESTMOUSENOTIFY(void *pvUser);
/** Pointer to a mouse event notification callback function. */
typedef FNVBOXGUESTMOUSENOTIFY *PFNVBOXGUESTMOUSENOTIFY; /**< @todo fix type prefix */

/** @} */

#endif

