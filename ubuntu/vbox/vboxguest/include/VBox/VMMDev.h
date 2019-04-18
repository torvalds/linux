/** @file
 * Virtual Device for Guest <-> VMM/Host communication (ADD,DEV).
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

#ifndef VBOX_INCLUDED_VMMDev_h
#define VBOX_INCLUDED_VMMDev_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/cdefs.h>
#include <VBox/param.h>                 /* for the PCI IDs. */
#include <VBox/types.h>
#include <VBox/ostypes.h>
#include <VBox/VMMDevCoreTypes.h>
#include <iprt/assertcompile.h>
#include <iprt/errcore.h>


#pragma pack(4) /* force structure dword packing here. */
RT_C_DECLS_BEGIN


/** @defgroup grp_vmmdev    VMM Device
 *
 * @note This interface cannot be changed, it can only be extended!
 *
 * @{
 */


/** Size of VMMDev RAM region accessible by guest.
 * Must be big enough to contain VMMDevMemory structure (see further down).
 * For now: 4 megabyte.
 */
#define VMMDEV_RAM_SIZE                                     (4 * 256 * PAGE_SIZE)

/** Size of VMMDev heap region accessible by guest.
 *  (Must be a power of two (pci range).)
 */
#define VMMDEV_HEAP_SIZE                                    (4 * PAGE_SIZE)

/** Port for generic request interface (relative offset). */
#define VMMDEV_PORT_OFF_REQUEST                             0
/** Port for requests that can be handled w/o going to ring-3 (relative offset).
 * This works like VMMDevReq_AcknowledgeEvents when read.  */
#define VMMDEV_PORT_OFF_REQUEST_FAST                        8


/** @defgroup grp_vmmdev_req    VMMDev Generic Request Interface
 * @{
 */

/** @name Current version of the VMMDev interface.
 *
 * Additions are allowed to work only if
 * additions_major == vmmdev_current && additions_minor <= vmmdev_current.
 * Additions version is reported to host (VMMDev) by VMMDevReq_ReportGuestInfo.
 *
 * @remarks These defines also live in the 16-bit and assembly versions of this
 *          header.
 */
#define VMMDEV_VERSION                      0x00010004
#define VMMDEV_VERSION_MAJOR                (VMMDEV_VERSION >> 16)
#define VMMDEV_VERSION_MINOR                (VMMDEV_VERSION & 0xffff)
/** @} */

/** Maximum request packet size. */
#define VMMDEV_MAX_VMMDEVREQ_SIZE           _1M
/** Maximum number of HGCM parameters.
 * @note This used to be 1024, which is kind of insane.  Was changed to 32,
 *       given that (guest) user land can only pass 61 anyway.
 *       See comments on VBGLIOCHGCMCALL::cParms. */
#define VMMDEV_MAX_HGCM_PARMS               32
/** Maximum total size of hgcm buffers in one call.
 * @note Used to be 2G, since reduced to 128MB.  */
#define VMMDEV_MAX_HGCM_DATA_SIZE           _128M

/**
 * VMMDev request types.
 * @note when updating this, adjust vmmdevGetRequestSize() as well
 */
typedef enum VMMDevRequestType
{
    VMMDevReq_InvalidRequest             =  0,
    VMMDevReq_GetMouseStatus             =  1,
    VMMDevReq_SetMouseStatus             =  2,
    VMMDevReq_SetPointerShape            =  3,
    VMMDevReq_GetHostVersion             =  4,
    VMMDevReq_Idle                       =  5,
    VMMDevReq_GetHostTime                = 10,
    VMMDevReq_GetHypervisorInfo          = 20,
    VMMDevReq_SetHypervisorInfo          = 21,
    VMMDevReq_RegisterPatchMemory        = 22, /**< @since version 3.0.6 */
    VMMDevReq_DeregisterPatchMemory      = 23, /**< @since version 3.0.6 */
    VMMDevReq_SetPowerStatus             = 30,
    VMMDevReq_AcknowledgeEvents          = 41,
    VMMDevReq_CtlGuestFilterMask         = 42,
    VMMDevReq_ReportGuestInfo            = 50,
    VMMDevReq_ReportGuestInfo2           = 58, /**< @since version 3.2.0 */
    VMMDevReq_ReportGuestStatus          = 59, /**< @since version 3.2.8 */
    VMMDevReq_ReportGuestUserState       = 74, /**< @since version 4.3 */
    /**
     * Retrieve a display resize request sent by the host using
     * @a IDisplay:setVideoModeHint.  Deprecated.
     *
     * Similar to @a VMMDevReq_GetDisplayChangeRequest2, except that it only
     * considers host requests sent for the first virtual display.  This guest
     * request should not be used in new guest code, and the results are
     * undefined if a guest mixes calls to this and
     * @a VMMDevReq_GetDisplayChangeRequest2.
     */
    VMMDevReq_GetDisplayChangeRequest    = 51,
    VMMDevReq_VideoModeSupported         = 52,
    VMMDevReq_GetHeightReduction         = 53,
    /**
     * Retrieve a display resize request sent by the host using
     * @a IDisplay:setVideoModeHint.
     *
     * Queries a display resize request sent from the host.  If the
     * @a eventAck member is sent to true and there is an unqueried
     * request available for one of the virtual display then that request will
     * be returned.  If several displays have unqueried requests the lowest
     * numbered display will be chosen first.  Only the most recent unseen
     * request for each display is remembered.
     * If @a eventAck is set to false, the last host request queried with
     * @a eventAck set is resent, or failing that the most recent received from
     * the host.  If no host request was ever received then all zeros are
     * returned.
     */
    VMMDevReq_GetDisplayChangeRequest2   = 54,
    VMMDevReq_ReportGuestCapabilities    = 55,
    VMMDevReq_SetGuestCapabilities       = 56,
    VMMDevReq_VideoModeSupported2        = 57, /**< @since version 3.2.0 */
    VMMDevReq_GetDisplayChangeRequestEx  = 80, /**< @since version 4.2.4 */
    VMMDevReq_GetDisplayChangeRequestMulti = 81,
#ifdef VBOX_WITH_HGCM
    VMMDevReq_HGCMConnect                = 60,
    VMMDevReq_HGCMDisconnect             = 61,
    VMMDevReq_HGCMCall32                 = 62,
    VMMDevReq_HGCMCall64                 = 63,
# ifdef IN_GUEST
#  if   ARCH_BITS == 64
    VMMDevReq_HGCMCall                   = VMMDevReq_HGCMCall64,
#  elif ARCH_BITS == 32 || ARCH_BITS == 16
    VMMDevReq_HGCMCall                   = VMMDevReq_HGCMCall32,
#  else
#   error "Unsupported ARCH_BITS"
#  endif
# endif
    VMMDevReq_HGCMCancel                 = 64,
    VMMDevReq_HGCMCancel2                = 65,
#endif
    VMMDevReq_VideoAccelEnable           = 70,
    VMMDevReq_VideoAccelFlush            = 71,
    VMMDevReq_VideoSetVisibleRegion      = 72,
    VMMDevReq_GetSeamlessChangeRequest   = 73,
    VMMDevReq_QueryCredentials           = 100,
    VMMDevReq_ReportCredentialsJudgement = 101,
    VMMDevReq_ReportGuestStats           = 110,
    VMMDevReq_GetMemBalloonChangeRequest = 111,
    VMMDevReq_GetStatisticsChangeRequest = 112,
    VMMDevReq_ChangeMemBalloon           = 113,
    VMMDevReq_GetVRDPChangeRequest       = 150,
    VMMDevReq_LogString                  = 200,
    VMMDevReq_GetCpuHotPlugRequest       = 210,
    VMMDevReq_SetCpuHotPlugStatus        = 211,
    VMMDevReq_RegisterSharedModule       = 212,
    VMMDevReq_UnregisterSharedModule     = 213,
    VMMDevReq_CheckSharedModules         = 214,
    VMMDevReq_GetPageSharingStatus       = 215,
    VMMDevReq_DebugIsPageShared          = 216,
    VMMDevReq_GetSessionId               = 217, /**< @since version 3.2.8 */
    VMMDevReq_WriteCoreDump              = 218,
    VMMDevReq_GuestHeartbeat             = 219,
    VMMDevReq_HeartbeatConfigure         = 220,
    VMMDevReq_NtBugCheck                 = 221,
    VMMDevReq_SizeHack                   = 0x7fffffff
} VMMDevRequestType;

/** Version of VMMDevRequestHeader structure. */
#define VMMDEV_REQUEST_HEADER_VERSION (0x10001)


/**
 * Generic VMMDev request header.
 *
 * This structure is copied/mirrored by VBGLREQHDR in the VBoxGuest I/O control
 * interface.  Changes there needs to be mirrored in it.
 *
 * @sa VBGLREQHDR
 */
typedef struct VMMDevRequestHeader
{
    /** IN: Size of the structure in bytes (including body).
     * (VBGLREQHDR uses this for input size and output if reserved1 is zero). */
    uint32_t size;
    /** IN: Version of the structure.  */
    uint32_t version;
    /** IN: Type of the request.
     * @note VBGLREQHDR uses this for optional output size. */
    VMMDevRequestType requestType;
    /** OUT: VBox status code. */
    int32_t  rc;
    /** Reserved field no.1. MBZ.
     * @note VBGLREQHDR uses this for optional output size, however never for a
     *       real VMMDev request, only in the I/O control interface. */
    uint32_t reserved1;
    /** IN: Requestor information (VMMDEV_REQUESTOR_XXX) when
     * VBOXGSTINFO2_F_REQUESTOR_INFO is set, otherwise ignored by the host. */
    uint32_t fRequestor;
} VMMDevRequestHeader;
AssertCompileSize(VMMDevRequestHeader, 24);

/** @name VMMDEV_REQUESTOR_XXX - Requestor information.
 *
 * This is information provided to the host by the VBoxGuest device driver, so
 * the host can implemented fine grained access to functionality if it likes.
 * @bugref{9105}
 *
 * @{ */
/** Requestor user not given. */
#define VMMDEV_REQUESTOR_USR_NOT_GIVEN              UINT32_C(0x00000000)
/** The kernel driver (VBoxGuest) is the requestor. */
#define VMMDEV_REQUESTOR_USR_DRV                    UINT32_C(0x00000001)
/** Some other kernel driver is the requestor. */
#define VMMDEV_REQUESTOR_USR_DRV_OTHER              UINT32_C(0x00000002)
/** The root or a admin user is the requestor. */
#define VMMDEV_REQUESTOR_USR_ROOT                   UINT32_C(0x00000003)
/** Requestor is the windows system user (SID S-1-5-18). */
#define VMMDEV_REQUESTOR_USR_SYSTEM                 UINT32_C(0x00000004)
/** Reserved requestor user \#1, treat like VMMDEV_REQUESTOR_USR_USER. */
#define VMMDEV_REQUESTOR_USR_RESERVED1              UINT32_C(0x00000005)
/** Regular joe user is making the request. */
#define VMMDEV_REQUESTOR_USR_USER                   UINT32_C(0x00000006)
/** Requestor is a guest user (or in a guest user group). */
#define VMMDEV_REQUESTOR_USR_GUEST                  UINT32_C(0x00000007)
/** User classification mask. */
#define VMMDEV_REQUESTOR_USR_MASK                   UINT32_C(0x00000007)

/** Kernel mode request.
 * @note This is zero, so test for VMMDEV_REQUESTOR_USERMODE instead.  */
#define VMMDEV_REQUESTOR_KERNEL                     UINT32_C(0x00000000)
/** User mode request. */
#define VMMDEV_REQUESTOR_USERMODE                   UINT32_C(0x00000008)

/** Don't know the physical console association of the requestor. */
#define VMMDEV_REQUESTOR_CON_DONT_KNOW              UINT32_C(0x00000000)
/** The request originates with a process that is NOT associated with the
 * physical console. */
#define VMMDEV_REQUESTOR_CON_NO                     UINT32_C(0x00000010)
/** Requestor process DOES is associated with the physical console. */
#define VMMDEV_REQUESTOR_CON_YES                    UINT32_C(0x00000020)
/** Requestor process belongs to user on the physical console, but cannot
 * ascertain that it is associated with that login. */
#define VMMDEV_REQUESTOR_CON_USER                   UINT32_C(0x00000030)
/** Mask the physical console state of the request. */
#define VMMDEV_REQUESTOR_CON_MASK                   UINT32_C(0x00000030)

/** Requestor is member of special VirtualBox user group (not on windows). */
#define VMMDEV_REQUESTOR_GRP_VBOX                   UINT32_C(0x00000080)
/** Requestor is member of wheel / administrators group (SID S-1-5-32-544). */
#define VMMDEV_REQUESTOR_GRP_WHEEL                  UINT32_C(0x00000100)

/** Requestor trust level: Unspecified */
#define VMMDEV_REQUESTOR_TRUST_NOT_GIVEN            UINT32_C(0x00000000)
/** Requestor trust level: Untrusted (SID S-1-16-0) */
#define VMMDEV_REQUESTOR_TRUST_UNTRUSTED            UINT32_C(0x00001000)
/** Requestor trust level: Untrusted (SID S-1-16-4096) */
#define VMMDEV_REQUESTOR_TRUST_LOW                  UINT32_C(0x00002000)
/** Requestor trust level: Medium (SID S-1-16-8192) */
#define VMMDEV_REQUESTOR_TRUST_MEDIUM               UINT32_C(0x00003000)
/** Requestor trust level: Medium plus (SID S-1-16-8448) */
#define VMMDEV_REQUESTOR_TRUST_MEDIUM_PLUS          UINT32_C(0x00004000)
/** Requestor trust level: High (SID S-1-16-12288) */
#define VMMDEV_REQUESTOR_TRUST_HIGH                 UINT32_C(0x00005000)
/** Requestor trust level: System (SID S-1-16-16384) */
#define VMMDEV_REQUESTOR_TRUST_SYSTEM               UINT32_C(0x00006000)
/** Requestor trust level: Protected or higher (SID S-1-16-20480, S-1-16-28672)
 * @note To avoid wasting an unnecessary bit, we combine the two top most
 *       mandatory security labels on Windows (protected and secure). */
#define VMMDEV_REQUESTOR_TRUST_PROTECTED            UINT32_C(0x00007000)
/** Requestor trust level mask.
 * The higher the value, the more the guest trusts the process. */
#define VMMDEV_REQUESTOR_TRUST_MASK                 UINT32_C(0x00007000)

/** Requestor is using the less trusted user device node (/dev/vboxuser). */
#define VMMDEV_REQUESTOR_USER_DEVICE                UINT32_C(0x00008000)
/** There is no user device node (/dev/vboxuser). */
#define VMMDEV_REQUESTOR_NO_USER_DEVICE             UINT32_C(0x00010000)

/** Legacy value for when VBOXGSTINFO2_F_REQUESTOR_INFO is clear.
 * @internal Host only. */
#define VMMDEV_REQUESTOR_LEGACY                     UINT32_MAX
/** Lowest conceivable trust level, for error situations of getters.
 * @internal Host only. */
#define VMMDEV_REQUESTOR_LOWEST                     (  VMMDEV_REQUESTOR_TRUST_UNTRUSTED | VMMDEV_REQUESTOR_USER_DEVICE \
                                                     | VMMDEV_REQUESTOR_CON_NO | VMMDEV_REQUESTOR_USERMODE \
                                                     | VMMDEV_REQUESTOR_USR_GUEST)
/** Used on the host to check whether a requestor value is present or not. */
#define VMMDEV_REQUESTOR_IS_PRESENT(a_fRequestor)   ((a_fRequestor) != VMMDEV_REQUESTOR_LEGACY)
/** @} */

/** Initialize a VMMDevRequestHeader structure.
 * Same as VBGLREQHDR_INIT_VMMDEV(). */
#define VMMDEV_REQ_HDR_INIT(a_pHdr, a_cb, a_enmType) \
    do { \
        (a_pHdr)->size         = (a_cb); \
        (a_pHdr)->version      = VMMDEV_REQUEST_HEADER_VERSION; \
        (a_pHdr)->requestType  = (a_enmType); \
        (a_pHdr)->rc           = VERR_INTERNAL_ERROR; \
        (a_pHdr)->reserved1    = 0; \
        (a_pHdr)->fRequestor   = 0; \
    } while (0)


/**
 * Mouse status request structure.
 *
 * Used by VMMDevReq_GetMouseStatus and VMMDevReq_SetMouseStatus.
 */
typedef struct
{
    /** header */
    VMMDevRequestHeader header;
    /** Mouse feature mask. See VMMDEV_MOUSE_*. */
    uint32_t mouseFeatures;
    /** Mouse x position. */
    int32_t pointerXPos;
    /** Mouse y position. */
    int32_t pointerYPos;
} VMMDevReqMouseStatus;
AssertCompileSize(VMMDevReqMouseStatus, 24+12);

/** @name Mouse capability bits (VMMDevReqMouseStatus::mouseFeatures).
 * @{ */
/** The guest can (== wants to) handle absolute coordinates.  */
#define VMMDEV_MOUSE_GUEST_CAN_ABSOLUTE                     RT_BIT(0)
/** The host can (== wants to) send absolute coordinates.
 * (Input not captured.) */
#define VMMDEV_MOUSE_HOST_WANTS_ABSOLUTE                    RT_BIT(1)
/** The guest can *NOT* switch to software cursor and therefore depends on the
 * host cursor.
 *
 * When guest additions are installed and the host has promised to display the
 * cursor itself, the guest installs a hardware mouse driver. Don't ask the
 * guest to switch to a software cursor then. */
#define VMMDEV_MOUSE_GUEST_NEEDS_HOST_CURSOR                RT_BIT(2)
/** The host does NOT provide support for drawing the cursor itself. */
#define VMMDEV_MOUSE_HOST_CANNOT_HWPOINTER                  RT_BIT(3)
/** The guest can read VMMDev events to find out about pointer movement */
#define VMMDEV_MOUSE_NEW_PROTOCOL                           RT_BIT(4)
/** If the guest changes the status of the
 * VMMDEV_MOUSE_GUEST_NEEDS_HOST_CURSOR bit, the host will honour this */
#define VMMDEV_MOUSE_HOST_RECHECKS_NEEDS_HOST_CURSOR        RT_BIT(5)
/** The host supplies an absolute pointing device.  The Guest Additions may
 * wish to use this to decide whether to install their own driver */
#define VMMDEV_MOUSE_HOST_HAS_ABS_DEV                       RT_BIT(6)
/** The mask of all VMMDEV_MOUSE_* flags */
#define VMMDEV_MOUSE_MASK                                   UINT32_C(0x0000007f)
/** The mask of guest capability changes for which notification events should
 * be sent */
#define VMMDEV_MOUSE_NOTIFY_HOST_MASK \
      (VMMDEV_MOUSE_GUEST_CAN_ABSOLUTE | VMMDEV_MOUSE_GUEST_NEEDS_HOST_CURSOR)
/** The mask of all capabilities which the guest can legitimately change */
#define VMMDEV_MOUSE_GUEST_MASK \
      (VMMDEV_MOUSE_NOTIFY_HOST_MASK | VMMDEV_MOUSE_NEW_PROTOCOL)
/** The mask of host capability changes for which notification events should
 * be sent */
#define VMMDEV_MOUSE_NOTIFY_GUEST_MASK \
      VMMDEV_MOUSE_HOST_WANTS_ABSOLUTE
/** The mask of all capabilities which the host can legitimately change */
#define VMMDEV_MOUSE_HOST_MASK \
      (  VMMDEV_MOUSE_NOTIFY_GUEST_MASK \
       | VMMDEV_MOUSE_HOST_CANNOT_HWPOINTER \
       | VMMDEV_MOUSE_HOST_RECHECKS_NEEDS_HOST_CURSOR \
       | VMMDEV_MOUSE_HOST_HAS_ABS_DEV)
/** @} */

/** @name Absolute mouse reporting range
 * @{ */
/** @todo Should these be here?  They are needed by both host and guest. */
/** The minumum value our pointing device can return. */
#define VMMDEV_MOUSE_RANGE_MIN 0
/** The maximum value our pointing device can return. */
#define VMMDEV_MOUSE_RANGE_MAX 0xFFFF
/** The full range our pointing device can return. */
#define VMMDEV_MOUSE_RANGE (VMMDEV_MOUSE_RANGE_MAX - VMMDEV_MOUSE_RANGE_MIN)
/** @} */


/**
 * Mouse pointer shape/visibility change request.
 *
 * Used by VMMDevReq_SetPointerShape. The size is variable.
 */
typedef struct VMMDevReqMousePointer
{
    /** Header. */
    VMMDevRequestHeader header;
    /** VBOX_MOUSE_POINTER_* bit flags from VBox/Graphics/VBoxVideo.h. */
    uint32_t fFlags;
    /** x coordinate of hot spot. */
    uint32_t xHot;
    /** y coordinate of hot spot. */
    uint32_t yHot;
    /** Width of the pointer in pixels. */
    uint32_t width;
    /** Height of the pointer in scanlines. */
    uint32_t height;
    /** Pointer data.
     *
     ****
     * The data consists of 1 bpp AND mask followed by 32 bpp XOR (color) mask.
     *
     * For pointers without alpha channel the XOR mask pixels are 32 bit values: (lsb)BGR0(msb).
     * For pointers with alpha channel the XOR mask consists of (lsb)BGRA(msb) 32 bit values.
     *
     * Guest driver must create the AND mask for pointers with alpha channel, so if host does not
     * support alpha, the pointer could be displayed as a normal color pointer. The AND mask can
     * be constructed from alpha values. For example alpha value >= 0xf0 means bit 0 in the AND mask.
     *
     * The AND mask is 1 bpp bitmap with byte aligned scanlines. Size of AND mask,
     * therefore, is cbAnd = (width + 7) / 8 * height. The padding bits at the
     * end of any scanline are undefined.
     *
     * The XOR mask follows the AND mask on the next 4 bytes aligned offset:
     * uint8_t *pXor = pAnd + (cbAnd + 3) & ~3
     * Bytes in the gap between the AND and the XOR mask are undefined.
     * XOR mask scanlines have no gap between them and size of XOR mask is:
     * cXor = width * 4 * height.
     ****
     *
     * Preallocate 4 bytes for accessing actual data as p->pointerData.
     */
    char pointerData[4];
} VMMDevReqMousePointer;
AssertCompileSize(VMMDevReqMousePointer, 24+24);

/**
 * Get the size that a VMMDevReqMousePointer request should have for a given
 * size of cursor, including the trailing cursor image and mask data.
 * @note an "empty" request still has the four preallocated bytes of data
 *
 * @returns the size
 * @param  width   the cursor width
 * @param  height  the cursor height
 */
DECLINLINE(size_t) vmmdevGetMousePointerReqSize(uint32_t width, uint32_t height)
{
    size_t cbBase = RT_UOFFSETOF(VMMDevReqMousePointer, pointerData[0]);
    size_t cbMask = (width + 7) / 8 * height;
    size_t cbArgb = width * height * 4;
    return RT_MAX(cbBase + ((cbMask + 3) & ~3) + cbArgb,
                  sizeof(VMMDevReqMousePointer));
}


/**
 * String log request structure.
 *
 * Used by VMMDevReq_LogString.
 * @deprecated  Use the IPRT logger or VbglR3WriteLog instead.
 */
typedef struct
{
    /** header */
    VMMDevRequestHeader header;
    /** variable length string data */
    char szString[1];
} VMMDevReqLogString;
AssertCompileSize(VMMDevReqLogString, 24+4);


/**
 * VirtualBox host version request structure.
 *
 * Used by VMMDevReq_GetHostVersion.
 *
 * @remarks VBGL uses this to detect the precense of new features in the
 *          interface.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader header;
    /** Major version. */
    uint16_t major;
    /** Minor version. */
    uint16_t minor;
    /** Build number. */
    uint32_t build;
    /** SVN revision. */
    uint32_t revision;
    /** Feature mask. */
    uint32_t features;
} VMMDevReqHostVersion;
AssertCompileSize(VMMDevReqHostVersion, 24+16);

/** @name VMMDEV_HVF_XXX - VMMDevReqHostVersion::features
 * @{ */
/** Physical page lists are supported by HGCM. */
#define VMMDEV_HVF_HGCM_PHYS_PAGE_LIST          RT_BIT_32(0)
/** HGCM supports the embedded buffer parameter type. */
#define VMMDEV_HVF_HGCM_EMBEDDED_BUFFERS        RT_BIT_32(1)
/** HGCM supports the contiguous page list parameter type. */
#define VMMDEV_HVF_HGCM_CONTIGUOUS_PAGE_LIST    RT_BIT_32(2)
/** HGCM supports the no-bounce page list parameter type. */
#define VMMDEV_HVF_HGCM_NO_BOUNCE_PAGE_LIST     RT_BIT_32(3)
/** VMMDev supports fast IRQ acknowledgements. */
#define VMMDEV_HVF_FAST_IRQ_ACK                 RT_BIT_32(31)
/** @} */


/**
 * Guest capabilities structure.
 *
 * Used by VMMDevReq_ReportGuestCapabilities.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader header;
    /** Capabilities (VMMDEV_GUEST_*). */
    uint32_t caps;
} VMMDevReqGuestCapabilities;
AssertCompileSize(VMMDevReqGuestCapabilities, 24+4);


/**
 * Guest capabilities structure, version 2.
 *
 * Used by VMMDevReq_SetGuestCapabilities.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader header;
    /** Mask of capabilities to be added. */
    uint32_t u32OrMask;
    /** Mask of capabilities to be removed. */
    uint32_t u32NotMask;
} VMMDevReqGuestCapabilities2;
AssertCompileSize(VMMDevReqGuestCapabilities2, 24+8);


/**
 * Idle request structure.
 *
 * Used by VMMDevReq_Idle.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader header;
} VMMDevReqIdle;
AssertCompileSize(VMMDevReqIdle, 24);


/**
 * Host time request structure.
 *
 * Used by VMMDevReq_GetHostTime.
 */
typedef struct
{
    /** Header */
    VMMDevRequestHeader header;
    /** OUT: Time in milliseconds since unix epoch. */
    uint64_t time;
} VMMDevReqHostTime;
AssertCompileSize(VMMDevReqHostTime, 24+8);


/**
 * Hypervisor info structure.
 *
 * Used by VMMDevReq_GetHypervisorInfo and VMMDevReq_SetHypervisorInfo.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader header;
    /** Guest virtual address of proposed hypervisor start.
     * Not used by VMMDevReq_GetHypervisorInfo.
     * @todo Make this 64-bit compatible? */
    RTGCPTR32 hypervisorStart;
    /** Hypervisor size in bytes. */
    uint32_t hypervisorSize;
} VMMDevReqHypervisorInfo;
AssertCompileSize(VMMDevReqHypervisorInfo, 24+8);

/** @name Default patch memory size .
 * Used by VMMDevReq_RegisterPatchMemory and VMMDevReq_DeregisterPatchMemory.
 * @{ */
#define VMMDEV_GUEST_DEFAULT_PATCHMEM_SIZE          8192
/** @} */

/**
 * Patching memory structure. (locked executable & read-only page from the guest's perspective)
 *
 * Used by VMMDevReq_RegisterPatchMemory and VMMDevReq_DeregisterPatchMemory
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader header;
    /** Guest virtual address of the patching page(s). */
    RTGCPTR64           pPatchMem;
    /** Patch page size in bytes. */
    uint32_t            cbPatchMem;
} VMMDevReqPatchMemory;
AssertCompileSize(VMMDevReqPatchMemory, 24+12);


/**
 * Guest power requests.
 *
 * See VMMDevReq_SetPowerStatus and VMMDevPowerStateRequest.
 */
typedef enum
{
    VMMDevPowerState_Invalid   = 0,
    VMMDevPowerState_Pause     = 1,
    VMMDevPowerState_PowerOff  = 2,
    VMMDevPowerState_SaveState = 3,
    VMMDevPowerState_SizeHack = 0x7fffffff
} VMMDevPowerState;
AssertCompileSize(VMMDevPowerState, 4);

/**
 * VM power status structure.
 *
 * Used by VMMDevReq_SetPowerStatus.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader header;
    /** Power state request. */
    VMMDevPowerState powerState;
} VMMDevPowerStateRequest;
AssertCompileSize(VMMDevPowerStateRequest, 24+4);


/**
 * Pending events structure.
 *
 * Used by VMMDevReq_AcknowledgeEvents.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader header;
    /** OUT: Pending event mask. */
    uint32_t events;
} VMMDevEvents;
AssertCompileSize(VMMDevEvents, 24+4);


/**
 * Guest event filter mask control.
 *
 * Used by VMMDevReq_CtlGuestFilterMask.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader header;
    /** Mask of events to be added to the filter. */
    uint32_t u32OrMask;
    /** Mask of events to be removed from the filter. */
    uint32_t u32NotMask;
} VMMDevCtlGuestFilterMask;
AssertCompileSize(VMMDevCtlGuestFilterMask, 24+8);


/**
 * Guest information structure.
 *
 * Used by VMMDevReportGuestInfo and PDMIVMMDEVCONNECTOR::pfnUpdateGuestVersion.
 */
typedef struct VBoxGuestInfo
{
    /** The VMMDev interface version expected by additions.
      * *Deprecated*, do not use anymore! Will be removed. */
    uint32_t interfaceVersion;
    /** Guest OS type. */
    VBOXOSTYPE osType;
} VBoxGuestInfo;
AssertCompileSize(VBoxGuestInfo, 8);

/**
 * Guest information report.
 *
 * Used by VMMDevReq_ReportGuestInfo.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader header;
    /** Guest information. */
    VBoxGuestInfo guestInfo;
} VMMDevReportGuestInfo;
AssertCompileSize(VMMDevReportGuestInfo, 24+8);


/**
 * Guest information structure, version 2.
 *
 * Used by VMMDevReportGuestInfo2 and PDMIVMMDEVCONNECTOR::pfnUpdateGuestVersion2.
 */
typedef struct VBoxGuestInfo2
{
    /** Major version. */
    uint16_t additionsMajor;
    /** Minor version. */
    uint16_t additionsMinor;
    /** Build number. */
    uint32_t additionsBuild;
    /** SVN revision. */
    uint32_t additionsRevision;
    /** Feature mask, VBOXGSTINFO2_F_XXX. */
    uint32_t additionsFeatures;
    /** The intentional meaning of this field was:
     * Some additional information, for example 'Beta 1' or something like that.
     *
     * The way it was implemented was implemented: VBOX_VERSION_STRING.
     *
     * This means the first three members are duplicated in this field (if the guest
     * build config is sane). So, the user must check this and chop it off before
     * usage.  There is, because of the Main code's blind trust in the field's
     * content, no way back. */
    char     szName[128];
} VBoxGuestInfo2;
AssertCompileSize(VBoxGuestInfo2, 144);

/** @name VBOXGSTINFO2_F_XXX - Features
 * @{ */
/** Request header carries requestor information. */
#define VBOXGSTINFO2_F_REQUESTOR_INFO       RT_BIT_32(0)
/** @} */


/**
 * Guest information report, version 2.
 *
 * Used by VMMDevReq_ReportGuestInfo2.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader header;
    /** Guest information. */
    VBoxGuestInfo2 guestInfo;
} VMMDevReportGuestInfo2;
AssertCompileSize(VMMDevReportGuestInfo2, 24+144);


/**
 * The facility class.
 *
 * This needs to be kept in sync with AdditionsFacilityClass of the Main API!
 */
typedef enum
{
    VBoxGuestFacilityClass_None       = 0,
    VBoxGuestFacilityClass_Driver     = 10,
    VBoxGuestFacilityClass_Service    = 30,
    VBoxGuestFacilityClass_Program    = 50,
    VBoxGuestFacilityClass_Feature    = 100,
    VBoxGuestFacilityClass_ThirdParty = 999,
    VBoxGuestFacilityClass_All        = 0x7ffffffe,
    VBoxGuestFacilityClass_SizeHack   = 0x7fffffff
} VBoxGuestFacilityClass;
AssertCompileSize(VBoxGuestFacilityClass, 4);

/**
 * Guest status structure.
 *
 * Used by VMMDevReqGuestStatus.
 */
typedef struct VBoxGuestStatus
{
    /** Facility the status is indicated for. */
    VBoxGuestFacilityType facility;
    /** Current guest status. */
    VBoxGuestFacilityStatus status;
    /** Flags, not used at the moment. */
    uint32_t flags;
} VBoxGuestStatus;
AssertCompileSize(VBoxGuestStatus, 12);

/**
 * Guest Additions status structure.
 *
 * Used by VMMDevReq_ReportGuestStatus.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader header;
    /** Guest information. */
    VBoxGuestStatus guestStatus;
} VMMDevReportGuestStatus;
AssertCompileSize(VMMDevReportGuestStatus, 24+12);


/**
 * Guest user status updates.
 */
typedef struct VBoxGuestUserStatus
{
    /** The guest user state to send. */
    VBoxGuestUserState  state;
    /** Size (in bytes) of szUser. */
    uint32_t            cbUser;
    /** Size (in bytes) of szDomain. */
    uint32_t            cbDomain;
    /** Size (in bytes) of aDetails. */
    uint32_t            cbDetails;
    /** Note: Here begins the dynamically
     *        allocated region. */
    /** Guest user to report state for. */
    char                szUser[1];
    /** Domain the guest user is bound to. */
    char                szDomain[1];
    /** Optional details of the state. */
    uint8_t             aDetails[1];
} VBoxGuestUserStatus;
AssertCompileSize(VBoxGuestUserStatus, 20);


/**
 * Guest user status structure.
 *
 * Used by VMMDevReq_ReportGuestUserStatus.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader header;
    /** Guest user status. */
    VBoxGuestUserStatus status;
} VMMDevReportGuestUserState;
AssertCompileSize(VMMDevReportGuestUserState, 24+20);


/**
 * Guest statistics structure.
 *
 * Used by VMMDevReportGuestStats and PDMIVMMDEVCONNECTOR::pfnReportStatistics.
 */
typedef struct VBoxGuestStatistics
{
    /** Virtual CPU ID. */
    uint32_t        u32CpuId;
    /** Reported statistics. */
    uint32_t        u32StatCaps;
    /** Idle CPU load (0-100) for last interval. */
    uint32_t        u32CpuLoad_Idle;
    /** Kernel CPU load (0-100) for last interval. */
    uint32_t        u32CpuLoad_Kernel;
    /** User CPU load (0-100) for last interval. */
    uint32_t        u32CpuLoad_User;
    /** Nr of threads. */
    uint32_t        u32Threads;
    /** Nr of processes. */
    uint32_t        u32Processes;
    /** Nr of handles. */
    uint32_t        u32Handles;
    /** Memory load (0-100). */
    uint32_t        u32MemoryLoad;
    /** Page size of guest system. */
    uint32_t        u32PageSize;
    /** Total physical memory (in 4KB pages). */
    uint32_t        u32PhysMemTotal;
    /** Available physical memory (in 4KB pages). */
    uint32_t        u32PhysMemAvail;
    /** Ballooned physical memory (in 4KB pages). */
    uint32_t        u32PhysMemBalloon;
    /** Total number of committed memory (which is not necessarily in-use) (in 4KB pages). */
    uint32_t        u32MemCommitTotal;
    /** Total amount of memory used by the kernel (in 4KB pages). */
    uint32_t        u32MemKernelTotal;
    /** Total amount of paged memory used by the kernel (in 4KB pages). */
    uint32_t        u32MemKernelPaged;
    /** Total amount of nonpaged memory used by the kernel (in 4KB pages). */
    uint32_t        u32MemKernelNonPaged;
    /** Total amount of memory used for the system cache (in 4KB pages). */
    uint32_t        u32MemSystemCache;
    /** Pagefile size (in 4KB pages). */
    uint32_t        u32PageFileSize;
} VBoxGuestStatistics;
AssertCompileSize(VBoxGuestStatistics, 19*4);

/** @name Guest statistics values (VBoxGuestStatistics::u32StatCaps).
 * @{ */
#define VBOX_GUEST_STAT_CPU_LOAD_IDLE       RT_BIT(0)
#define VBOX_GUEST_STAT_CPU_LOAD_KERNEL     RT_BIT(1)
#define VBOX_GUEST_STAT_CPU_LOAD_USER       RT_BIT(2)
#define VBOX_GUEST_STAT_THREADS             RT_BIT(3)
#define VBOX_GUEST_STAT_PROCESSES           RT_BIT(4)
#define VBOX_GUEST_STAT_HANDLES             RT_BIT(5)
#define VBOX_GUEST_STAT_MEMORY_LOAD         RT_BIT(6)
#define VBOX_GUEST_STAT_PHYS_MEM_TOTAL      RT_BIT(7)
#define VBOX_GUEST_STAT_PHYS_MEM_AVAIL      RT_BIT(8)
#define VBOX_GUEST_STAT_PHYS_MEM_BALLOON    RT_BIT(9)
#define VBOX_GUEST_STAT_MEM_COMMIT_TOTAL    RT_BIT(10)
#define VBOX_GUEST_STAT_MEM_KERNEL_TOTAL    RT_BIT(11)
#define VBOX_GUEST_STAT_MEM_KERNEL_PAGED    RT_BIT(12)
#define VBOX_GUEST_STAT_MEM_KERNEL_NONPAGED RT_BIT(13)
#define VBOX_GUEST_STAT_MEM_SYSTEM_CACHE    RT_BIT(14)
#define VBOX_GUEST_STAT_PAGE_FILE_SIZE      RT_BIT(15)
/** @} */

/**
 * Guest statistics command structure.
 *
 * Used by VMMDevReq_ReportGuestStats.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader header;
    /** Guest information. */
    VBoxGuestStatistics guestStats;
} VMMDevReportGuestStats;
AssertCompileSize(VMMDevReportGuestStats, 24+19*4);


/** Memory balloon change request structure. */
#define VMMDEV_MAX_MEMORY_BALLOON(PhysMemTotal)     ( (9 * (PhysMemTotal)) / 10 )

/**
 * Poll for ballooning change request.
 *
 * Used by VMMDevReq_GetMemBalloonChangeRequest.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader header;
    /** Balloon size in megabytes. */
    uint32_t            cBalloonChunks;
    /** Guest ram size in megabytes. */
    uint32_t            cPhysMemChunks;
    /** Setting this to VMMDEV_EVENT_BALLOON_CHANGE_REQUEST indicates that the
     * request is a response to that event.
     * (Don't confuse this with VMMDevReq_AcknowledgeEvents.) */
    uint32_t            eventAck;
} VMMDevGetMemBalloonChangeRequest;
AssertCompileSize(VMMDevGetMemBalloonChangeRequest, 24+12);


/**
 * Change the size of the balloon.
 *
 * Used by VMMDevReq_ChangeMemBalloon.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader header;
    /** The number of pages in the array. */
    uint32_t            cPages;
    /** true = inflate, false = deflate.  */
    uint32_t            fInflate;
    /** Physical address (RTGCPHYS) of each page, variable size. */
    RTGCPHYS            aPhysPage[1];
} VMMDevChangeMemBalloon;
AssertCompileSize(VMMDevChangeMemBalloon, 24+16);


/**
 * Guest statistics interval change request structure.
 *
 * Used by VMMDevReq_GetStatisticsChangeRequest.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader header;
    /** The interval in seconds. */
    uint32_t            u32StatInterval;
    /** Setting this to VMMDEV_EVENT_STATISTICS_INTERVAL_CHANGE_REQUEST indicates
     * that the request is a response to that event.
     * (Don't confuse this with VMMDevReq_AcknowledgeEvents.) */
    uint32_t            eventAck;
} VMMDevGetStatisticsChangeRequest;
AssertCompileSize(VMMDevGetStatisticsChangeRequest, 24+8);


/** The size of a string field in the credentials request (including '\\0').
 * @see VMMDevCredentials  */
#define VMMDEV_CREDENTIALS_SZ_SIZE          128

/**
 * Credentials request structure.
 *
 * Used by VMMDevReq_QueryCredentials.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader header;
    /** IN/OUT: Request flags. */
    uint32_t u32Flags;
    /** OUT: User name (UTF-8). */
    char szUserName[VMMDEV_CREDENTIALS_SZ_SIZE];
    /** OUT: Password (UTF-8). */
    char szPassword[VMMDEV_CREDENTIALS_SZ_SIZE];
    /** OUT: Domain name (UTF-8). */
    char szDomain[VMMDEV_CREDENTIALS_SZ_SIZE];
} VMMDevCredentials;
AssertCompileSize(VMMDevCredentials, 24+4+3*128);

/** @name Credentials request flag (VMMDevCredentials::u32Flags)
 * @{ */
/** query from host whether credentials are present */
#define VMMDEV_CREDENTIALS_QUERYPRESENCE     RT_BIT(1)
/** read credentials from host (can be combined with clear) */
#define VMMDEV_CREDENTIALS_READ              RT_BIT(2)
/** clear credentials on host (can be combined with read) */
#define VMMDEV_CREDENTIALS_CLEAR             RT_BIT(3)
/** read credentials for judgement in the guest */
#define VMMDEV_CREDENTIALS_READJUDGE         RT_BIT(8)
/** clear credentials for judegement on the host */
#define VMMDEV_CREDENTIALS_CLEARJUDGE        RT_BIT(9)
/** report credentials acceptance by guest */
#define VMMDEV_CREDENTIALS_JUDGE_OK          RT_BIT(10)
/** report credentials denial by guest */
#define VMMDEV_CREDENTIALS_JUDGE_DENY        RT_BIT(11)
/** report that no judgement could be made by guest */
#define VMMDEV_CREDENTIALS_JUDGE_NOJUDGEMENT RT_BIT(12)

/** flag telling the guest that credentials are present */
#define VMMDEV_CREDENTIALS_PRESENT           RT_BIT(16)
/** flag telling guest that local logons should be prohibited */
#define VMMDEV_CREDENTIALS_NOLOCALLOGON      RT_BIT(17)
/** @} */


/**
 * Seamless mode change request structure.
 *
 * Used by VMMDevReq_GetSeamlessChangeRequest.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader header;

    /** New seamless mode. */
    VMMDevSeamlessMode mode;
    /** Setting this to VMMDEV_EVENT_SEAMLESS_MODE_CHANGE_REQUEST indicates
     * that the request is a response to that event.
     * (Don't confuse this with VMMDevReq_AcknowledgeEvents.) */
    uint32_t eventAck;
} VMMDevSeamlessChangeRequest;
AssertCompileSize(VMMDevSeamlessChangeRequest, 24+8);
AssertCompileMemberOffset(VMMDevSeamlessChangeRequest, eventAck, 24+4);


/**
 * Display change request structure.
 *
 * Used by VMMDevReq_GetDisplayChangeRequest.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader header;
    /** Horizontal pixel resolution (0 = do not change). */
    uint32_t xres;
    /** Vertical pixel resolution (0 = do not change). */
    uint32_t yres;
    /** Bits per pixel (0 = do not change). */
    uint32_t bpp;
    /** Setting this to VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST indicates
     * that the request is a response to that event.
     * (Don't confuse this with VMMDevReq_AcknowledgeEvents.) */
    uint32_t eventAck;
} VMMDevDisplayChangeRequest;
AssertCompileSize(VMMDevDisplayChangeRequest, 24+16);


/**
 * Display change request structure, version 2.
 *
 * Used by VMMDevReq_GetDisplayChangeRequest2.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader header;
    /** Horizontal pixel resolution (0 = do not change). */
    uint32_t xres;
    /** Vertical pixel resolution (0 = do not change). */
    uint32_t yres;
    /** Bits per pixel (0 = do not change). */
    uint32_t bpp;
    /** Setting this to VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST indicates
     * that the request is a response to that event.
     * (Don't confuse this with VMMDevReq_AcknowledgeEvents.) */
    uint32_t eventAck;
    /** 0 for primary display, 1 for the first secondary, etc. */
    uint32_t display;
} VMMDevDisplayChangeRequest2;
AssertCompileSize(VMMDevDisplayChangeRequest2, 24+20);


/**
 * Display change request structure, version Extended.
 *
 * Used by VMMDevReq_GetDisplayChangeRequestEx.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader header;
    /** Horizontal pixel resolution (0 = do not change). */
    uint32_t xres;
    /** Vertical pixel resolution (0 = do not change). */
    uint32_t yres;
    /** Bits per pixel (0 = do not change). */
    uint32_t bpp;
    /** Setting this to VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST indicates
     * that the request is a response to that event.
     * (Don't confuse this with VMMDevReq_AcknowledgeEvents.) */
    uint32_t eventAck;
    /** 0 for primary display, 1 for the first secondary, etc. */
    uint32_t display;
    /** New OriginX of secondary virtual screen */
    uint32_t cxOrigin;
    /** New OriginY of secondary virtual screen  */
    uint32_t cyOrigin;
    /** Change in origin of the secondary virtaul scree is
     *  required */
    bool fChangeOrigin;
    /** secondary virtual screen enabled or disabled */
    bool fEnabled;
} VMMDevDisplayChangeRequestEx;
AssertCompileSize(VMMDevDisplayChangeRequestEx, 24+32);


/** Flags for VMMDevDisplayDef::fDisplayFlags */
#define VMMDEV_DISPLAY_PRIMARY  UINT32_C(0x00000001) /**< Primary display. */
#define VMMDEV_DISPLAY_DISABLED UINT32_C(0x00000002) /**< Display is disabled. */
#define VMMDEV_DISPLAY_ORIGIN   UINT32_C(0x00000004) /**< Change position of the diplay. */
#define VMMDEV_DISPLAY_CX       UINT32_C(0x00000008) /**< Change the horizontal resolution of the display. */
#define VMMDEV_DISPLAY_CY       UINT32_C(0x00000010) /**< Change the vertical resolution of the display. */
#define VMMDEV_DISPLAY_BPP      UINT32_C(0x00000020) /**< Change the color depth of the display. */

/** Definition of one monitor. Used by VMMDevReq_GetDisplayChangeRequestMulti. */
typedef struct VMMDevDisplayDef
{
    uint32_t fDisplayFlags;             /**< VMMDEV_DISPLAY_* flags. */
    uint32_t idDisplay;                 /**< The display number. */
    int32_t  xOrigin;                   /**< New OriginX of the guest screen. */
    int32_t  yOrigin;                   /**< New OriginY of the guest screen.  */
    uint32_t cx;                        /**< Horizontal pixel resolution. */
    uint32_t cy;                        /**< Vertical pixel resolution. */
    uint32_t cBitsPerPixel;             /**< Bits per pixel. */
} VMMDevDisplayDef;
AssertCompileSize(VMMDevDisplayDef, 28);

/** Multimonitor display change request structure. Used by VMMDevReq_GetDisplayChangeRequestMulti. */
typedef struct VMMDevDisplayChangeRequestMulti
{
    VMMDevRequestHeader header;         /**< Header. */
    uint32_t eventAck;                  /**< Setting this to VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST indicates
                                         * that the request is a response to that event.
                                         * (Don't confuse this with VMMDevReq_AcknowledgeEvents.) */
    uint32_t cDisplays;                 /**< Number of monitors. In: how many the guest expects.
                                         *                      Out: how many the host provided. */
    VMMDevDisplayDef aDisplays[1];      /**< Layout of monitors. */
} VMMDevDisplayChangeRequestMulti;
AssertCompileSize(VMMDevDisplayChangeRequestMulti, 24+8+28);


/**
 * Video mode supported request structure.
 *
 * Used by VMMDevReq_VideoModeSupported.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader header;
    /** IN: Horizontal pixel resolution. */
    uint32_t width;
    /** IN: Vertical pixel resolution. */
    uint32_t height;
    /** IN: Bits per pixel. */
    uint32_t bpp;
    /** OUT: Support indicator. */
    bool fSupported;
} VMMDevVideoModeSupportedRequest;
AssertCompileSize(VMMDevVideoModeSupportedRequest, 24+16);

/**
 * Video mode supported request structure for a specific display.
 *
 * Used by VMMDevReq_VideoModeSupported2.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader header;
    /** IN: The guest display number. */
    uint32_t display;
    /** IN: Horizontal pixel resolution. */
    uint32_t width;
    /** IN: Vertical pixel resolution. */
    uint32_t height;
    /** IN: Bits per pixel. */
    uint32_t bpp;
    /** OUT: Support indicator. */
    bool fSupported;
} VMMDevVideoModeSupportedRequest2;
AssertCompileSize(VMMDevVideoModeSupportedRequest2, 24+20);

/**
 * Video modes height reduction request structure.
 *
 * Used by VMMDevReq_GetHeightReduction.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader header;
    /** OUT: Height reduction in pixels. */
    uint32_t heightReduction;
} VMMDevGetHeightReductionRequest;
AssertCompileSize(VMMDevGetHeightReductionRequest, 24+4);


/**
 * VRDP change request structure.
 *
 * Used by VMMDevReq_GetVRDPChangeRequest.
 */
typedef struct
{
    /** Header */
    VMMDevRequestHeader header;
    /** Whether VRDP is active or not. */
    uint8_t u8VRDPActive;
    /** The configured experience level for active VRDP. */
    uint32_t u32VRDPExperienceLevel;
} VMMDevVRDPChangeRequest;
AssertCompileSize(VMMDevVRDPChangeRequest, 24+8);
AssertCompileMemberOffset(VMMDevVRDPChangeRequest, u8VRDPActive, 24);
AssertCompileMemberOffset(VMMDevVRDPChangeRequest, u32VRDPExperienceLevel, 24+4);

/** @name VRDP Experience level (VMMDevVRDPChangeRequest::u32VRDPExperienceLevel)
 * @{ */
#define VRDP_EXPERIENCE_LEVEL_ZERO     0 /**< Theming disabled. */
#define VRDP_EXPERIENCE_LEVEL_LOW      1 /**< Full window dragging and desktop wallpaper disabled. */
#define VRDP_EXPERIENCE_LEVEL_MEDIUM   2 /**< Font smoothing, gradients. */
#define VRDP_EXPERIENCE_LEVEL_HIGH     3 /**< Animation effects disabled. */
#define VRDP_EXPERIENCE_LEVEL_FULL     4 /**< Everything enabled. */
/** @} */


/**
 * VBVA enable request structure.
 *
 * Used by VMMDevReq_VideoAccelEnable.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader header;
    /** 0 - disable, !0 - enable. */
    uint32_t u32Enable;
    /** The size of VBVAMEMORY::au8RingBuffer expected by driver.
     *  The host will refuse to enable VBVA if the size is not equal to
     *  VBVA_RING_BUFFER_SIZE.
     */
    uint32_t cbRingBuffer;
    /** Guest initializes the status to 0. Host sets appropriate VBVA_F_STATUS_ flags. */
    uint32_t fu32Status;
} VMMDevVideoAccelEnable;
AssertCompileSize(VMMDevVideoAccelEnable, 24+12);

/** @name VMMDevVideoAccelEnable::fu32Status.
 * @{ */
#define VBVA_F_STATUS_ACCEPTED (0x01)
#define VBVA_F_STATUS_ENABLED  (0x02)
/** @} */


/**
 * VBVA flush request structure.
 *
 * Used by VMMDevReq_VideoAccelFlush.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader header;
} VMMDevVideoAccelFlush;
AssertCompileSize(VMMDevVideoAccelFlush, 24);


/**
 * VBVA set visible region request structure.
 *
 * Used by VMMDevReq_VideoSetVisibleRegion.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader header;
    /** Number of rectangles */
    uint32_t cRect;
    /** Rectangle array.
     * @todo array is spelled aRects[1].  */
    RTRECT Rect;
} VMMDevVideoSetVisibleRegion;
AssertCompileSize(RTRECT, 16);
AssertCompileSize(VMMDevVideoSetVisibleRegion, 24+4+16);

/**
 * CPU event types.
 */
typedef enum
{
    VMMDevCpuStatusType_Invalid  = 0,
    VMMDevCpuStatusType_Disable  = 1,
    VMMDevCpuStatusType_Enable   = 2,
    VMMDevCpuStatusType_SizeHack = 0x7fffffff
} VMMDevCpuStatusType;

/**
 * CPU hotplug event status request.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader header;
    /** Status type */
    VMMDevCpuStatusType enmStatusType;
} VMMDevCpuHotPlugStatusRequest;
AssertCompileSize(VMMDevCpuHotPlugStatusRequest, 24+4);

/**
 * Get the ID of the changed CPU and event type.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader header;
    /** Event type */
    VMMDevCpuEventType  enmEventType;
    /** core id of the CPU changed */
    uint32_t            idCpuCore;
    /** package id of the CPU changed */
    uint32_t            idCpuPackage;
} VMMDevGetCpuHotPlugRequest;
AssertCompileSize(VMMDevGetCpuHotPlugRequest, 24+4+4+4);


AssertCompileSize(VMMDEVSHAREDREGIONDESC, 16); /* structure was promoted to VBox/types.h. */

#define VMMDEVSHAREDREGIONDESC_MAX          32

/**
 * Shared module registration
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader         header;
    /** Shared module size. */
    uint32_t                    cbModule;
    /** Number of included region descriptors */
    uint32_t                    cRegions;
    /** Base address of the shared module. */
    RTGCPTR64                   GCBaseAddr;
    /** Guest OS type. */
    VBOXOSFAMILY                enmGuestOS;
    /** Alignment. */
    uint32_t                    u32Align;
    /** Module name */
    char                        szName[128];
    /** Module version */
    char                        szVersion[16];
    /** Shared region descriptor(s). */
    VMMDEVSHAREDREGIONDESC      aRegions[1];
} VMMDevSharedModuleRegistrationRequest;
AssertCompileSize(VMMDevSharedModuleRegistrationRequest, 24+4+4+8+4+4+128+16+16);


/**
 * Shared module unregistration
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader         header;
    /** Shared module size. */
    uint32_t                    cbModule;
    /** Align at 8 byte boundary. */
    uint32_t                    u32Alignment;
    /** Base address of the shared module. */
    RTGCPTR64                   GCBaseAddr;
    /** Module name */
    char                        szName[128];
    /** Module version */
    char                        szVersion[16];
} VMMDevSharedModuleUnregistrationRequest;
AssertCompileSize(VMMDevSharedModuleUnregistrationRequest, 24+4+4+8+128+16);


/**
 * Shared module periodic check
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader         header;
} VMMDevSharedModuleCheckRequest;
AssertCompileSize(VMMDevSharedModuleCheckRequest, 24);

/**
 * Paging sharing enabled query
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader         header;
    /** Enabled flag (out) */
    bool                        fEnabled;
    /** Alignment */
    bool                        fAlignment[3];
} VMMDevPageSharingStatusRequest;
AssertCompileSize(VMMDevPageSharingStatusRequest, 24+4);


/**
 * Page sharing status query (debug build only)
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader         header;
    /** Page address. */
    RTGCPTR                     GCPtrPage;
    /** Page flags. */
    uint64_t                    uPageFlags;
    /** Shared flag (out) */
    bool                        fShared;
    /** Alignment */
    bool                        fAlignment[3];
} VMMDevPageIsSharedRequest;

/**
 * Session id request structure.
 *
 * Used by VMMDevReq_GetSessionId.
 */
typedef struct
{
    /** Header */
    VMMDevRequestHeader         header;
    /** OUT: unique session id; the id will be different after each start, reset or restore of the VM */
    uint64_t                    idSession;
} VMMDevReqSessionId;
AssertCompileSize(VMMDevReqSessionId, 24+8);


/**
 * Write Core Dump request.
 *
 * Used by VMMDevReq_WriteCoreDump.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader         header;
    /** Flags (reserved, MBZ). */
    uint32_t                    fFlags;
} VMMDevReqWriteCoreDump;
AssertCompileSize(VMMDevReqWriteCoreDump, 24+4);


/**
 * Heart beat check state structure.
 * Used by VMMDevReq_HeartbeatConfigure.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader         header;
    /** OUT: Guest heartbeat interval in nanosec. */
    uint64_t                    cNsInterval;
    /** Heartbeat check flag. */
    bool                        fEnabled;
} VMMDevReqHeartbeat;
AssertCompileSize(VMMDevReqHeartbeat, 24+12);


/**
 * NT bug check report.
 * Used by VMMDevReq_NtBugCheck.
 * @remarks  Can be issued with just the header if no more data is available.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader         header;
    /** The bug check number (P0). */
    uint64_t                    uBugCheck;
    /** The four bug check parameters. */
    uint64_t                    auParameters[4];
} VMMDevReqNtBugCheck;
AssertCompileSize(VMMDevReqNtBugCheck, 24+40);



#ifdef VBOX_WITH_HGCM

/** @name HGCM flags.
 * @{
 */
# define VBOX_HGCM_REQ_DONE      RT_BIT_32(VBOX_HGCM_REQ_DONE_BIT)
# define VBOX_HGCM_REQ_DONE_BIT  0
# define VBOX_HGCM_REQ_CANCELLED (0x2)
/** @} */

/**
 * HGCM request header.
 */
typedef struct VMMDevHGCMRequestHeader
{
    /** Request header. */
    VMMDevRequestHeader header;

    /** HGCM flags. */
    uint32_t fu32Flags;

    /** Result code. */
    int32_t result;
} VMMDevHGCMRequestHeader;
AssertCompileSize(VMMDevHGCMRequestHeader, 24+8);

/**
 * HGCM connect request structure.
 *
 * Used by VMMDevReq_HGCMConnect.
 */
typedef struct
{
    /** HGCM request header. */
    VMMDevHGCMRequestHeader header;

    /** IN: Description of service to connect to. */
    HGCMServiceLocation loc;

    /** OUT: Client identifier assigned by local instance of HGCM. */
    uint32_t u32ClientID;
} VMMDevHGCMConnect;
AssertCompileSize(VMMDevHGCMConnect, 32+132+4);


/**
 * HGCM disconnect request structure.
 *
 * Used by VMMDevReq_HGCMDisconnect.
 */
typedef struct
{
    /** HGCM request header. */
    VMMDevHGCMRequestHeader header;

    /** IN: Client identifier. */
    uint32_t u32ClientID;
} VMMDevHGCMDisconnect;
AssertCompileSize(VMMDevHGCMDisconnect, 32+4);

/**
 * HGCM call request structure.
 *
 * Used by VMMDevReq_HGCMCall32 and VMMDevReq_HGCMCall64.
 */
typedef struct
{
    /* request header */
    VMMDevHGCMRequestHeader header;

    /** IN: Client identifier. */
    uint32_t u32ClientID;
    /** IN: Service function number. */
    uint32_t u32Function;
    /** IN: Number of parameters. */
    uint32_t cParms;
    /** Parameters follow in form: HGCMFunctionParameter aParms[X]; */
} VMMDevHGCMCall;
AssertCompileSize(VMMDevHGCMCall, 32+12);

/** @name Direction of data transfer (HGCMPageListInfo::flags). Bit flags.
 * @{ */
#define VBOX_HGCM_F_PARM_DIRECTION_NONE      UINT32_C(0x00000000)
#define VBOX_HGCM_F_PARM_DIRECTION_TO_HOST   UINT32_C(0x00000001)
#define VBOX_HGCM_F_PARM_DIRECTION_FROM_HOST UINT32_C(0x00000002)
#define VBOX_HGCM_F_PARM_DIRECTION_BOTH      UINT32_C(0x00000003)
#define VBOX_HGCM_F_PARM_DIRECTION_MASK      UINT32_C(0x00000003)
/** Macro for validating that the specified flags are valid. */
#define VBOX_HGCM_F_PARM_ARE_VALID(fFlags) \
    (   ((fFlags) & VBOX_HGCM_F_PARM_DIRECTION_MASK) \
     && !((fFlags) & ~VBOX_HGCM_F_PARM_DIRECTION_MASK) )
/** @} */

/**
 * VMMDevHGCMParmType_PageList points to this structure to actually describe the
 * buffer.
 */
typedef struct
{
    uint32_t flags;        /**< VBOX_HGCM_F_PARM_*. */
    uint16_t offFirstPage; /**< Offset in the first page where data begins. */
    uint16_t cPages;       /**< Number of pages. */
    RTGCPHYS64 aPages[1];  /**< Page addresses. */
} HGCMPageListInfo;
AssertCompileSize(HGCMPageListInfo, 4+2+2+8);


/** Get the pointer to the first parmater of a HGCM call request.  */
# define VMMDEV_HGCM_CALL_PARMS(a)   ((HGCMFunctionParameter *)((uint8_t *)(a) + sizeof (VMMDevHGCMCall)))
/** Get the pointer to the first parmater of a 32-bit HGCM call request.  */
# define VMMDEV_HGCM_CALL_PARMS32(a) ((HGCMFunctionParameter32 *)((uint8_t *)(a) + sizeof (VMMDevHGCMCall)))

# ifdef VBOX_WITH_64_BITS_GUESTS
/* Explicit defines for the host code. */
#  ifdef VBOX_HGCM_HOST_CODE
#   define VMMDEV_HGCM_CALL_PARMS32(a) ((HGCMFunctionParameter32 *)((uint8_t *)(a) + sizeof (VMMDevHGCMCall)))
#   define VMMDEV_HGCM_CALL_PARMS64(a) ((HGCMFunctionParameter64 *)((uint8_t *)(a) + sizeof (VMMDevHGCMCall)))
#  endif /* VBOX_HGCM_HOST_CODE */
# endif /* VBOX_WITH_64_BITS_GUESTS */

# define VBOX_HGCM_MAX_PARMS 32

/**
 * HGCM cancel request structure.
 *
 * The Cancel request is issued using the same physical memory address as was
 * used for the corresponding initial HGCMCall.
 *
 * Used by VMMDevReq_HGCMCancel.
 */
typedef struct
{
    /** Header. */
    VMMDevHGCMRequestHeader header;
} VMMDevHGCMCancel;
AssertCompileSize(VMMDevHGCMCancel, 32);

/**
 * HGCM cancel request structure, version 2.
 *
 * Used by VMMDevReq_HGCMCancel2.
 *
 * VINF_SUCCESS when cancelled.
 * VERR_NOT_FOUND if the specified request cannot be found.
 * VERR_INVALID_PARAMETER if the address is invalid valid.
 */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader header;
    /** The physical address of the request to cancel. */
    RTGCPHYS32 physReqToCancel;
} VMMDevHGCMCancel2;
AssertCompileSize(VMMDevHGCMCancel2, 24+4);

#endif /* VBOX_WITH_HGCM */


/**
 * Inline helper to determine the request size for the given operation.
 * Returns 0 if the given operation is not handled and/or supported.
 *
 * @returns Size.
 * @param   requestType     The VMMDev request type.
 */
DECLINLINE(size_t) vmmdevGetRequestSize(VMMDevRequestType requestType)
{
    switch (requestType)
    {
        case VMMDevReq_GetMouseStatus:
        case VMMDevReq_SetMouseStatus:
            return sizeof(VMMDevReqMouseStatus);
        case VMMDevReq_SetPointerShape:
            return sizeof(VMMDevReqMousePointer);
        case VMMDevReq_GetHostVersion:
            return sizeof(VMMDevReqHostVersion);
        case VMMDevReq_Idle:
            return sizeof(VMMDevReqIdle);
        case VMMDevReq_GetHostTime:
            return sizeof(VMMDevReqHostTime);
        case VMMDevReq_GetHypervisorInfo:
        case VMMDevReq_SetHypervisorInfo:
            return sizeof(VMMDevReqHypervisorInfo);
        case VMMDevReq_RegisterPatchMemory:
        case VMMDevReq_DeregisterPatchMemory:
            return sizeof(VMMDevReqPatchMemory);
        case VMMDevReq_SetPowerStatus:
            return sizeof(VMMDevPowerStateRequest);
        case VMMDevReq_AcknowledgeEvents:
            return sizeof(VMMDevEvents);
        case VMMDevReq_ReportGuestInfo:
            return sizeof(VMMDevReportGuestInfo);
        case VMMDevReq_ReportGuestInfo2:
            return sizeof(VMMDevReportGuestInfo2);
        case VMMDevReq_ReportGuestStatus:
            return sizeof(VMMDevReportGuestStatus);
        case VMMDevReq_ReportGuestUserState:
            return sizeof(VMMDevReportGuestUserState);
        case VMMDevReq_GetDisplayChangeRequest:
            return sizeof(VMMDevDisplayChangeRequest);
        case VMMDevReq_GetDisplayChangeRequest2:
            return sizeof(VMMDevDisplayChangeRequest2);
        case VMMDevReq_GetDisplayChangeRequestEx:
            return sizeof(VMMDevDisplayChangeRequestEx);
        case VMMDevReq_GetDisplayChangeRequestMulti:
            return RT_UOFFSETOF(VMMDevDisplayChangeRequestMulti, aDisplays[0]);
        case VMMDevReq_VideoModeSupported:
            return sizeof(VMMDevVideoModeSupportedRequest);
        case VMMDevReq_GetHeightReduction:
            return sizeof(VMMDevGetHeightReductionRequest);
        case VMMDevReq_ReportGuestCapabilities:
            return sizeof(VMMDevReqGuestCapabilities);
        case VMMDevReq_SetGuestCapabilities:
            return sizeof(VMMDevReqGuestCapabilities2);
#ifdef VBOX_WITH_HGCM
        case VMMDevReq_HGCMConnect:
            return sizeof(VMMDevHGCMConnect);
        case VMMDevReq_HGCMDisconnect:
            return sizeof(VMMDevHGCMDisconnect);
        case VMMDevReq_HGCMCall32:
            return sizeof(VMMDevHGCMCall);
# ifdef VBOX_WITH_64_BITS_GUESTS
        case VMMDevReq_HGCMCall64:
            return sizeof(VMMDevHGCMCall);
# endif
        case VMMDevReq_HGCMCancel:
            return sizeof(VMMDevHGCMCancel);
#endif /* VBOX_WITH_HGCM */
        case VMMDevReq_VideoAccelEnable:
            return sizeof(VMMDevVideoAccelEnable);
        case VMMDevReq_VideoAccelFlush:
            return sizeof(VMMDevVideoAccelFlush);
        case VMMDevReq_VideoSetVisibleRegion:
            /* The original protocol didn't consider a guest with NO visible
             * windows */
            return sizeof(VMMDevVideoSetVisibleRegion) - sizeof(RTRECT);
        case VMMDevReq_GetSeamlessChangeRequest:
            return sizeof(VMMDevSeamlessChangeRequest);
        case VMMDevReq_QueryCredentials:
            return sizeof(VMMDevCredentials);
        case VMMDevReq_ReportGuestStats:
            return sizeof(VMMDevReportGuestStats);
        case VMMDevReq_GetMemBalloonChangeRequest:
            return sizeof(VMMDevGetMemBalloonChangeRequest);
        case VMMDevReq_GetStatisticsChangeRequest:
            return sizeof(VMMDevGetStatisticsChangeRequest);
        case VMMDevReq_ChangeMemBalloon:
            return sizeof(VMMDevChangeMemBalloon);
        case VMMDevReq_GetVRDPChangeRequest:
            return sizeof(VMMDevVRDPChangeRequest);
        case VMMDevReq_LogString:
            return sizeof(VMMDevReqLogString);
        case VMMDevReq_CtlGuestFilterMask:
            return sizeof(VMMDevCtlGuestFilterMask);
        case VMMDevReq_GetCpuHotPlugRequest:
            return sizeof(VMMDevGetCpuHotPlugRequest);
        case VMMDevReq_SetCpuHotPlugStatus:
            return sizeof(VMMDevCpuHotPlugStatusRequest);
        case VMMDevReq_RegisterSharedModule:
            return sizeof(VMMDevSharedModuleRegistrationRequest);
        case VMMDevReq_UnregisterSharedModule:
            return sizeof(VMMDevSharedModuleUnregistrationRequest);
        case VMMDevReq_CheckSharedModules:
            return sizeof(VMMDevSharedModuleCheckRequest);
        case VMMDevReq_GetPageSharingStatus:
            return sizeof(VMMDevPageSharingStatusRequest);
        case VMMDevReq_DebugIsPageShared:
            return sizeof(VMMDevPageIsSharedRequest);
        case VMMDevReq_GetSessionId:
            return sizeof(VMMDevReqSessionId);
        case VMMDevReq_HeartbeatConfigure:
            return sizeof(VMMDevReqHeartbeat);
        case VMMDevReq_GuestHeartbeat:
            return sizeof(VMMDevRequestHeader);
        default:
            break;
    }

    return 0;
}


/**
 * Initializes a request structure.
 *
 * @returns VBox status code.
 * @param   req             The request structure to initialize.
 * @param   type            The request type.
 */
DECLINLINE(int) vmmdevInitRequest(VMMDevRequestHeader *req, VMMDevRequestType type)
{
    uint32_t requestSize;
    if (!req)
        return VERR_INVALID_PARAMETER;
    requestSize = (uint32_t)vmmdevGetRequestSize(type);
    if (!requestSize)
        return VERR_INVALID_PARAMETER;
    req->size        = requestSize;
    req->version     = VMMDEV_REQUEST_HEADER_VERSION;
    req->requestType = type;
    req->rc          = VERR_GENERAL_FAILURE;
    req->reserved1   = 0;
    req->fRequestor  = 0;
    return VINF_SUCCESS;
}

/** @} */

/** @name VBVA ring defines.
 *
 * The VBVA ring buffer is suitable for transferring large (< 2GB) amount of
 * data. For example big bitmaps which do not fit to the buffer.
 *
 * Guest starts writing to the buffer by initializing a record entry in the
 * aRecords queue. VBVA_F_RECORD_PARTIAL indicates that the record is being
 * written. As data is written to the ring buffer, the guest increases off32End
 * for the record.
 *
 * The host reads the aRecords on flushes and processes all completed records.
 * When host encounters situation when only a partial record presents and
 * cbRecord & ~VBVA_F_RECORD_PARTIAL >= VBVA_RING_BUFFER_SIZE -
 * VBVA_RING_BUFFER_THRESHOLD, the host fetched all record data and updates
 * off32Head. After that on each flush the host continues fetching the data
 * until the record is completed.
 *
 */
#define VMMDEV_VBVA_RING_BUFFER_SIZE        (_4M - _1K)
#define VMMDEV_VBVA_RING_BUFFER_THRESHOLD   (4 * _1K)

#define VMMDEV_VBVA_MAX_RECORDS (64)
/** @} */

/**
 * VBVA record.
 */
typedef struct VMMDEVVBVARECORD
{
    /** The length of the record. Changed by guest. */
    uint32_t cbRecord;
} VMMDEVVBVARECORD;
AssertCompileSize(VMMDEVVBVARECORD, 4);

#if ARCH_BITS >= 32

/**
 * VBVA memory layout.
 *
 * This is a subsection of the VMMDevMemory structure.
 */
typedef struct VBVAMEMORY
{
    /** VBVA_F_MODE_*. */
    uint32_t fu32ModeFlags;

    /** The offset where the data start in the buffer. */
    uint32_t off32Data;
    /** The offset where next data must be placed in the buffer. */
    uint32_t off32Free;

    /** The ring buffer for data. */
    uint8_t  au8RingBuffer[VMMDEV_VBVA_RING_BUFFER_SIZE];

    /** The queue of record descriptions. */
    VMMDEVVBVARECORD aRecords[VMMDEV_VBVA_MAX_RECORDS];
    uint32_t indexRecordFirst;
    uint32_t indexRecordFree;

    /** RDP orders supported by the client. The guest reports only them
     * and falls back to DIRTY rects for not supported ones.
     *
     * (1 << VBVA_VRDP_*)
     */
    uint32_t fu32SupportedOrders;

} VBVAMEMORY;
AssertCompileSize(VBVAMEMORY, 12 + (_4M-_1K) + 4*64 + 12);


/**
 * The layout of VMMDEV RAM region that contains information for guest.
 */
typedef struct VMMDevMemory
{
    /** The size of this structure. */
    uint32_t u32Size;
    /** The structure version. (VMMDEV_MEMORY_VERSION) */
    uint32_t u32Version;

    union
    {
        struct
        {
            /** Flag telling that VMMDev set the IRQ and acknowlegment is required */
            bool fHaveEvents;
        } V1_04;

        struct
        {
            /** Pending events flags, set by host. */
            uint32_t u32HostEvents;
            /** Mask of events the guest wants to see, set by guest. */
            uint32_t u32GuestEventMask;
        } V1_03;
    } V;

    VBVAMEMORY vbvaMemory;

} VMMDevMemory;
AssertCompileSize(VMMDevMemory, 8+8 + (12 + (_4M-_1K) + 4*64 + 12) );
AssertCompileMemberOffset(VMMDevMemory, vbvaMemory, 16);

/** Version of VMMDevMemory structure (VMMDevMemory::u32Version). */
# define VMMDEV_MEMORY_VERSION   (1)

#endif /* ARCH_BITS >= 32 */

/** @} */

RT_C_DECLS_END
#pragma pack()

#endif /* !VBOX_INCLUDED_VMMDev_h */

