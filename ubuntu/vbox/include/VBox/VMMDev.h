/** @file
 * Virtual Device for Guest <-> VMM/Host communication (ADD,DEV).
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

#ifndef ___VBox_VMMDev_h
#define ___VBox_VMMDev_h

#include <VBox/cdefs.h>
#include <VBox/param.h>                 /* for the PCI IDs. */
#include <VBox/types.h>
#include <VBox/err.h>
#include <VBox/ostypes.h>
#include <VBox/VMMDev2.h>
#include <iprt/assert.h>


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


/** @name VMMDev events.
 *
 * Used mainly by VMMDevReq_AcknowledgeEvents/VMMDevEvents and version 1.3 of
 * VMMDevMemory.
 *
 * @{
 */
/** Host mouse capabilities has been changed. */
#define VMMDEV_EVENT_MOUSE_CAPABILITIES_CHANGED             RT_BIT(0)
/** HGCM event. */
#define VMMDEV_EVENT_HGCM                                   RT_BIT(1)
/** A display change request has been issued. */
#define VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST                 RT_BIT(2)
/** Credentials are available for judgement. */
#define VMMDEV_EVENT_JUDGE_CREDENTIALS                      RT_BIT(3)
/** The guest has been restored. */
#define VMMDEV_EVENT_RESTORED                               RT_BIT(4)
/** Seamless mode state changed. */
#define VMMDEV_EVENT_SEAMLESS_MODE_CHANGE_REQUEST           RT_BIT(5)
/** Memory balloon size changed. */
#define VMMDEV_EVENT_BALLOON_CHANGE_REQUEST                 RT_BIT(6)
/** Statistics interval changed. */
#define VMMDEV_EVENT_STATISTICS_INTERVAL_CHANGE_REQUEST     RT_BIT(7)
/** VRDP status changed. */
#define VMMDEV_EVENT_VRDP                                   RT_BIT(8)
/** New mouse position data available. */
#define VMMDEV_EVENT_MOUSE_POSITION_CHANGED                 RT_BIT(9)
/** CPU hotplug event occurred. */
#define VMMDEV_EVENT_CPU_HOTPLUG                            RT_BIT(10)
/** The mask of valid events, for sanity checking. */
#define VMMDEV_EVENT_VALID_EVENT_MASK                       UINT32_C(0x000007ff)
/** @} */


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
/** Maximum number of HGCM parameters. */
#define VMMDEV_MAX_HGCM_PARMS               1024
/** Maximum total size of hgcm buffers in one call. */
#define VMMDEV_MAX_HGCM_DATA_SIZE           UINT32_C(0x7FFFFFFF)

/**
 * VMMDev request types.
 * @note when updating this, adjust vmmdevGetRequestSize() as well
 */
typedef enum
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
    VMMDevReq_RegisterPatchMemory        = 22, /* since version 3.0.6 */
    VMMDevReq_DeregisterPatchMemory      = 23, /* since version 3.0.6 */
    VMMDevReq_SetPowerStatus             = 30,
    VMMDevReq_AcknowledgeEvents          = 41,
    VMMDevReq_CtlGuestFilterMask         = 42,
    VMMDevReq_ReportGuestInfo            = 50,
    VMMDevReq_ReportGuestInfo2           = 58, /* since version 3.2.0 */
    VMMDevReq_ReportGuestStatus          = 59, /* since version 3.2.8 */
    VMMDevReq_ReportGuestUserState       = 74, /* since version 4.3 */
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
    VMMDevReq_VideoModeSupported2        = 57, /* since version 3.2.0 */
    VMMDevReq_GetDisplayChangeRequestEx  = 80, /* since version 4.2.4 */
#ifdef VBOX_WITH_HGCM
    VMMDevReq_HGCMConnect                = 60,
    VMMDevReq_HGCMDisconnect             = 61,
#ifdef VBOX_WITH_64_BITS_GUESTS
    VMMDevReq_HGCMCall32                 = 62,
    VMMDevReq_HGCMCall64                 = 63,
#else
    VMMDevReq_HGCMCall                   = 62,
#endif /* VBOX_WITH_64_BITS_GUESTS */
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
    VMMDevReq_GetSessionId               = 217, /* since version 3.2.8 */
    VMMDevReq_WriteCoreDump              = 218,
    VMMDevReq_GuestHeartbeat             = 219,
    VMMDevReq_HeartbeatConfigure         = 220,
    VMMDevReq_SizeHack                   = 0x7fffffff
} VMMDevRequestType;

#ifdef VBOX_WITH_64_BITS_GUESTS
/*
 * Constants and structures are redefined for the guest.
 *
 * Host code MUST always use either *32 or *64 variant explicitely.
 * Host source code will use VBOX_HGCM_HOST_CODE define to catch undefined
 * data types and constants.
 *
 * This redefinition means that the new additions builds will use
 * the *64 or *32 variants depending on the current architecture bit count (ARCH_BITS).
 */
# ifndef VBOX_HGCM_HOST_CODE
#  if ARCH_BITS == 64
#   define VMMDevReq_HGCMCall VMMDevReq_HGCMCall64
#  elif ARCH_BITS == 32
#   define VMMDevReq_HGCMCall VMMDevReq_HGCMCall32
#  else
#   error "Unsupported ARCH_BITS"
#  endif
# endif /* !VBOX_HGCM_HOST_CODE */
#endif /* VBOX_WITH_64_BITS_GUESTS */

/** Version of VMMDevRequestHeader structure. */
#define VMMDEV_REQUEST_HEADER_VERSION (0x10001)


/**
 * Generic VMMDev request header.
 */
typedef struct
{
    /** IN: Size of the structure in bytes (including body). */
    uint32_t size;
    /** IN: Version of the structure.  */
    uint32_t version;
    /** IN: Type of the request. */
    VMMDevRequestType requestType;
    /** OUT: Return code. */
    int32_t  rc;
    /** Reserved field no.1. MBZ. */
    uint32_t reserved1;
    /** Reserved field no.2. MBZ. */
    uint32_t reserved2;
} VMMDevRequestHeader;
AssertCompileSize(VMMDevRequestHeader, 24);


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
    /** VBOX_MOUSE_POINTER_* bit flags. */
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
    size_t cbBase = RT_OFFSETOF(VMMDevReqMousePointer, pointerData);
    size_t cbMask = (width + 7) / 8 * height;
    size_t cbArgb = width * height * 4;
    return RT_MAX(cbBase + ((cbMask + 3) & ~3) + cbArgb,
                  sizeof(VMMDevReqMousePointer));
}

/** @name VMMDevReqMousePointer::fFlags
 * @note The VBOX_MOUSE_POINTER_* flags are used in the guest video driver,
 *       values must be <= 0x8000 and must not be changed. (try make more sense
 *       of this, please).
 * @{
 */
/** pointer is visible */
#define VBOX_MOUSE_POINTER_VISIBLE (0x0001)
/** pointer has alpha channel */
#define VBOX_MOUSE_POINTER_ALPHA   (0x0002)
/** pointerData contains new pointer shape */
#define VBOX_MOUSE_POINTER_SHAPE   (0x0004)
/** @} */


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

/** @name VMMDevReqHostVersion::features
 * @{ */
/** Physical page lists are supported by HGCM. */
#define VMMDEV_HVF_HGCM_PHYS_PAGE_LIST  RT_BIT(0)
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

/** @name Guest capability bits.
 * Used by VMMDevReq_ReportGuestCapabilities and VMMDevReq_SetGuestCapabilities.
 * @{ */
/** The guest supports seamless display rendering. */
#define VMMDEV_GUEST_SUPPORTS_SEAMLESS                      RT_BIT_32(0)
/** The guest supports mapping guest to host windows. */
#define VMMDEV_GUEST_SUPPORTS_GUEST_HOST_WINDOW_MAPPING     RT_BIT_32(1)
/** The guest graphical additions are active.
 * Used for fast activation and deactivation of certain graphical operations
 * (e.g. resizing & seamless). The legacy VMMDevReq_ReportGuestCapabilities
 * request sets this automatically, but VMMDevReq_SetGuestCapabilities does
 * not. */
#define VMMDEV_GUEST_SUPPORTS_GRAPHICS                      RT_BIT_32(2)
/** The mask of valid events, for sanity checking. */
#define VMMDEV_GUEST_CAPABILITIES_MASK                      UINT32_C(0x00000007)
/** @} */


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
    /** Feature mask, currently unused. */
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
 * The guest facility.
 * This needs to be kept in sync with AdditionsFacilityType of the Main API!
 */
typedef enum
{
    VBoxGuestFacilityType_Unknown         = 0,
    VBoxGuestFacilityType_VBoxGuestDriver = 20,
    VBoxGuestFacilityType_AutoLogon       = 90,  /* VBoxGINA / VBoxCredProv / pam_vbox. */
    VBoxGuestFacilityType_VBoxService     = 100,
    VBoxGuestFacilityType_VBoxTrayClient  = 101, /* VBoxTray (Windows), VBoxClient (Linux, Unix). */
    VBoxGuestFacilityType_Seamless        = 1000,
    VBoxGuestFacilityType_Graphics        = 1100,
    VBoxGuestFacilityType_All             = 0x7ffffffe,
    VBoxGuestFacilityType_SizeHack        = 0x7fffffff
} VBoxGuestFacilityType;
AssertCompileSize(VBoxGuestFacilityType, 4);


/**
 * The current guest status of a facility.
 * This needs to be kept in sync with AdditionsFacilityStatus of the Main API!
 *
 * @remarks r=bird: Pretty please, for future types like this, simply do a
 *          linear allocation without any gaps.  This stuff is impossible work
 *          efficiently with, let alone validate.  Applies to the other facility
 *          enums too.
 */
typedef enum
{
    VBoxGuestFacilityStatus_Inactive    = 0,
    VBoxGuestFacilityStatus_Paused      = 1,
    VBoxGuestFacilityStatus_PreInit     = 20,
    VBoxGuestFacilityStatus_Init        = 30,
    VBoxGuestFacilityStatus_Active      = 50,
    VBoxGuestFacilityStatus_Terminating = 100,
    VBoxGuestFacilityStatus_Terminated  = 101,
    VBoxGuestFacilityStatus_Failed  =     800,
    VBoxGuestFacilityStatus_Unknown     = 999,
    VBoxGuestFacilityStatus_SizeHack    = 0x7fffffff
} VBoxGuestFacilityStatus;
AssertCompileSize(VBoxGuestFacilityStatus, 4);


/**
 * The facility class.
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
 * The current status of specific guest user.
 * This needs to be kept in sync with GuestUserState of the Main API!
 */
typedef enum VBoxGuestUserState
{
    VBoxGuestUserState_Unknown            = 0,
    VBoxGuestUserState_LoggedIn           = 1,
    VBoxGuestUserState_LoggedOut          = 2,
    VBoxGuestUserState_Locked             = 3,
    VBoxGuestUserState_Unlocked           = 4,
    VBoxGuestUserState_Disabled           = 5,
    VBoxGuestUserState_Idle               = 6,
    VBoxGuestUserState_InUse              = 7,
    VBoxGuestUserState_Created            = 8,
    VBoxGuestUserState_Deleted            = 9,
    VBoxGuestUserState_SessionChanged     = 10,
    VBoxGuestUserState_CredentialsChanged = 11,
    VBoxGuestUserState_RoleChanged        = 12,
    VBoxGuestUserState_GroupAdded         = 13,
    VBoxGuestUserState_GroupRemoved       = 14,
    VBoxGuestUserState_Elevated           = 15,
    VBoxGuestUserState_SizeHack           = 0x7fffffff
} VBoxGuestUserState;
AssertCompileSize(VBoxGuestUserState, 4);


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

/** @name The ballooning chunk size which VMMDev works at.
 * @{ */
#define VMMDEV_MEMORY_BALLOON_CHUNK_PAGES            (_1M/4096)
#define VMMDEV_MEMORY_BALLOON_CHUNK_SIZE             (VMMDEV_MEMORY_BALLOON_CHUNK_PAGES*4096)
/** @} */


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


/**
 * Shared region description
 */
typedef struct VMMDEVSHAREDREGIONDESC
{
    RTGCPTR64           GCRegionAddr;
    uint32_t            cbRegion;
    uint32_t            u32Alignment;
} VMMDEVSHAREDREGIONDESC;
AssertCompileSize(VMMDEVSHAREDREGIONDESC, 16);

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
    VMMDevRequestHeader header;
    /** OUT: unique session id; the id will be different after each start, reset or restore of the VM */
    uint64_t            idSession;
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
    VMMDevRequestHeader header;
    /** Flags (reserved, MBZ). */
    uint32_t            fFlags;
} VMMDevReqWriteCoreDump;
AssertCompileSize(VMMDevReqWriteCoreDump, 24+4);

/** Heart beat check state structure.
 *  Used by VMMDevReq_HeartbeatConfigure. */
typedef struct
{
    /** Header. */
    VMMDevRequestHeader header;
    /** OUT: Guest heartbeat interval in nanosec. */
    uint64_t    cNsInterval;
    /** Heartbeat check flag. */
    bool fEnabled;
} VMMDevReqHeartbeat;
AssertCompileSize(VMMDevReqHeartbeat, 24+12);



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
 * HGCM parameter type.
 */
typedef enum
{
    VMMDevHGCMParmType_Invalid            = 0,
    VMMDevHGCMParmType_32bit              = 1,
    VMMDevHGCMParmType_64bit              = 2,
    VMMDevHGCMParmType_PhysAddr           = 3,  /**< @deprecated Doesn't work, use PageList. */
    VMMDevHGCMParmType_LinAddr            = 4,  /**< In and Out */
    VMMDevHGCMParmType_LinAddr_In         = 5,  /**< In  (read;  host<-guest) */
    VMMDevHGCMParmType_LinAddr_Out        = 6,  /**< Out (write; host->guest) */
    VMMDevHGCMParmType_LinAddr_Locked     = 7,  /**< Locked In and Out */
    VMMDevHGCMParmType_LinAddr_Locked_In  = 8,  /**< Locked In  (read;  host<-guest) */
    VMMDevHGCMParmType_LinAddr_Locked_Out = 9,  /**< Locked Out (write; host->guest) */
    VMMDevHGCMParmType_PageList           = 10, /**< Physical addresses of locked pages for a buffer. */
    VMMDevHGCMParmType_SizeHack           = 0x7fffffff
} HGCMFunctionParameterType;
AssertCompileSize(HGCMFunctionParameterType, 4);

# ifdef VBOX_WITH_64_BITS_GUESTS
/**
 * HGCM function parameter, 32-bit client.
 */
typedef struct
{
    HGCMFunctionParameterType type;
    union
    {
        uint32_t   value32;
        uint64_t   value64;
        struct
        {
            uint32_t size;

            union
            {
                RTGCPHYS32 physAddr;
                RTGCPTR32  linearAddr;
            } u;
        } Pointer;
        struct
        {
            uint32_t size;   /**< Size of the buffer described by the page list. */
            uint32_t offset; /**< Relative to the request header, valid if size != 0. */
        } PageList;
    } u;
#  ifdef __cplusplus
    void SetUInt32(uint32_t u32)
    {
        type = VMMDevHGCMParmType_32bit;
        u.value64 = 0; /* init unused bits to 0 */
        u.value32 = u32;
    }

    int GetUInt32(uint32_t *pu32)
    {
        if (type == VMMDevHGCMParmType_32bit)
        {
            *pu32 = u.value32;
            return VINF_SUCCESS;
        }
        return VERR_INVALID_PARAMETER;
    }

    void SetUInt64(uint64_t u64)
    {
        type      = VMMDevHGCMParmType_64bit;
        u.value64 = u64;
    }

    int GetUInt64(uint64_t *pu64)
    {
        if (type == VMMDevHGCMParmType_64bit)
        {
            *pu64 = u.value64;
            return VINF_SUCCESS;
        }
        return VERR_INVALID_PARAMETER;
    }

    void SetPtr(void *pv, uint32_t cb)
    {
        type                    = VMMDevHGCMParmType_LinAddr;
        u.Pointer.size          = cb;
        u.Pointer.u.linearAddr  = (RTGCPTR32)(uintptr_t)pv;
    }
#  endif /* __cplusplus */
} HGCMFunctionParameter32;
AssertCompileSize(HGCMFunctionParameter32, 4+8);

/**
 * HGCM function parameter, 64-bit client.
 */
typedef struct
{
    HGCMFunctionParameterType type;
    union
    {
        uint32_t   value32;
        uint64_t   value64;
        struct
        {
            uint32_t size;

            union
            {
                RTGCPHYS64 physAddr;
                RTGCPTR64  linearAddr;
            } u;
        } Pointer;
        struct
        {
            uint32_t size;   /**< Size of the buffer described by the page list. */
            uint32_t offset; /**< Relative to the request header, valid if size != 0. */
        } PageList;
    } u;
#  ifdef __cplusplus
    void SetUInt32(uint32_t u32)
    {
        type = VMMDevHGCMParmType_32bit;
        u.value64 = 0; /* init unused bits to 0 */
        u.value32 = u32;
    }

    int GetUInt32(uint32_t *pu32)
    {
        if (type == VMMDevHGCMParmType_32bit)
        {
            *pu32 = u.value32;
            return VINF_SUCCESS;
        }
        return VERR_INVALID_PARAMETER;
    }

    void SetUInt64(uint64_t u64)
    {
        type      = VMMDevHGCMParmType_64bit;
        u.value64 = u64;
    }

    int GetUInt64(uint64_t *pu64)
    {
        if (type == VMMDevHGCMParmType_64bit)
        {
            *pu64 = u.value64;
            return VINF_SUCCESS;
        }
        return VERR_INVALID_PARAMETER;
    }

    void SetPtr(void *pv, uint32_t cb)
    {
        type                    = VMMDevHGCMParmType_LinAddr;
        u.Pointer.size          = cb;
        u.Pointer.u.linearAddr  = (uintptr_t)pv;
    }
#  endif /** __cplusplus */
} HGCMFunctionParameter64;
AssertCompileSize(HGCMFunctionParameter64, 4+12);

/* Redefine the structure type for the guest code. */
#  ifndef VBOX_HGCM_HOST_CODE
#   if ARCH_BITS == 64
#     define HGCMFunctionParameter  HGCMFunctionParameter64
#   elif ARCH_BITS == 32
#     define HGCMFunctionParameter  HGCMFunctionParameter32
#   else
#    error "Unsupported sizeof (void *)"
#   endif
#  endif /* !VBOX_HGCM_HOST_CODE */

# else /* !VBOX_WITH_64_BITS_GUESTS */

/**
 * HGCM function parameter, 32-bit client.
 *
 * @todo If this is the same as HGCMFunctionParameter32, why the duplication?
 */
typedef struct
{
    HGCMFunctionParameterType type;
    union
    {
        uint32_t   value32;
        uint64_t   value64;
        struct
        {
            uint32_t size;

            union
            {
                RTGCPHYS32 physAddr;
                RTGCPTR32  linearAddr;
            } u;
        } Pointer;
        struct
        {
            uint32_t size;   /**< Size of the buffer described by the page list. */
            uint32_t offset; /**< Relative to the request header, valid if size != 0. */
        } PageList;
    } u;
#  ifdef __cplusplus
    void SetUInt32(uint32_t u32)
    {
        type = VMMDevHGCMParmType_32bit;
        u.value64 = 0; /* init unused bits to 0 */
        u.value32 = u32;
    }

    int GetUInt32(uint32_t *pu32)
    {
        if (type == VMMDevHGCMParmType_32bit)
        {
            *pu32 = u.value32;
            return VINF_SUCCESS;
        }
        return VERR_INVALID_PARAMETER;
    }

    void SetUInt64(uint64_t u64)
    {
        type      = VMMDevHGCMParmType_64bit;
        u.value64 = u64;
    }

    int GetUInt64(uint64_t *pu64)
    {
        if (type == VMMDevHGCMParmType_64bit)
        {
            *pu64 = u.value64;
            return VINF_SUCCESS;
        }
        return VERR_INVALID_PARAMETER;
    }

    void SetPtr(void *pv, uint32_t cb)
    {
        type                    = VMMDevHGCMParmType_LinAddr;
        u.Pointer.size          = cb;
        u.Pointer.u.linearAddr  = (uintptr_t)pv;
    }
#  endif /* __cplusplus */
} HGCMFunctionParameter;
AssertCompileSize(HGCMFunctionParameter, 4+8);
# endif /* !VBOX_WITH_64_BITS_GUESTS */

/**
 * HGCM call request structure.
 *
 * Used by VMMDevReq_HGCMCall, VMMDevReq_HGCMCall32 and VMMDevReq_HGCMCall64.
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
/** Macro for validating that the specified flags are valid. */
#define VBOX_HGCM_F_PARM_ARE_VALID(fFlags) \
    (   (fFlags) > VBOX_HGCM_F_PARM_DIRECTION_NONE \
     && (fFlags) < VBOX_HGCM_F_PARM_DIRECTION_BOTH )
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
#ifdef VBOX_WITH_64_BITS_GUESTS
        case VMMDevReq_HGCMCall32:
            return sizeof(VMMDevHGCMCall);
        case VMMDevReq_HGCMCall64:
            return sizeof(VMMDevHGCMCall);
#else
        case VMMDevReq_HGCMCall:
            return sizeof(VMMDevHGCMCall);
#endif /* VBOX_WITH_64_BITS_GUESTS */
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
    req->reserved2   = 0;
    return VINF_SUCCESS;
}

/** @} */


/**
 * VBVA command header.
 *
 * @todo Where does this fit in?
 */
typedef struct VBVACMDHDR
{
   /** Coordinates of affected rectangle. */
   int16_t x;
   int16_t y;
   uint16_t w;
   uint16_t h;
} VBVACMDHDR;
AssertCompileSize(VBVACMDHDR, 8);

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
#define VBVA_RING_BUFFER_SIZE        (_4M - _1K)
#define VBVA_RING_BUFFER_THRESHOLD   (4 * _1K)

#define VBVA_MAX_RECORDS (64)

#define VBVA_F_MODE_ENABLED         UINT32_C(0x00000001)
#define VBVA_F_MODE_VRDP            UINT32_C(0x00000002)
#define VBVA_F_MODE_VRDP_RESET      UINT32_C(0x00000004)
#define VBVA_F_MODE_VRDP_ORDER_MASK UINT32_C(0x00000008)

#define VBVA_F_STATE_PROCESSING     UINT32_C(0x00010000)

#define VBVA_F_RECORD_PARTIAL       UINT32_C(0x80000000)
/** @} */

/**
 * VBVA record.
 */
typedef struct VBVARECORD
{
    /** The length of the record. Changed by guest. */
    uint32_t cbRecord;
} VBVARECORD;
AssertCompileSize(VBVARECORD, 4);


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
    uint8_t  au8RingBuffer[VBVA_RING_BUFFER_SIZE];

    /** The queue of record descriptions. */
    VBVARECORD aRecords[VBVA_MAX_RECORDS];
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
#define VMMDEV_MEMORY_VERSION   (1)

/** @} */

RT_C_DECLS_END
#pragma pack()

#endif

