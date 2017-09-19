/** @file
 * Virtual Device for Guest <-> VMM/Host communication, Core Types. (ADD,DEV)
 *
 * These types are needed by several headers VBoxGuestLib.h and are kept
 * separate to avoid having to include the whole VMMDev.h fun.
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

#ifndef ___VBox_VMMDevCoreTypes_h
#define ___VBox_VMMDevCoreTypes_h

#include <iprt/assertcompile.h>
#include <iprt/types.h>
#ifdef __cplusplus
# include <iprt/err.h>
#endif


/** @addtogroup grp_vmmdev
 * @{
 */

/* Helpful forward declarations: */
struct VMMDevRequestHeader;
struct VMMDevReqMousePointer;
struct VMMDevMemory;


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


/** @name The ballooning chunk size which VMMDev works at.
 * @{ */
#define VMMDEV_MEMORY_BALLOON_CHUNK_PAGES            (_1M/4096)
#define VMMDEV_MEMORY_BALLOON_CHUNK_SIZE             (VMMDEV_MEMORY_BALLOON_CHUNK_PAGES*4096)
/** @} */


/**
 * Seamless mode.
 *
 * Used by VbglR3SeamlessWaitEvent
 *
 * @ingroup grp_vmmdev_req
 */
typedef enum
{
    VMMDev_Seamless_Disabled         = 0,     /**< normal mode; entire guest desktop displayed. */
    VMMDev_Seamless_Visible_Region   = 1,     /**< visible region mode; only top-level guest windows displayed. */
    VMMDev_Seamless_Host_Window      = 2,     /**< windowed mode; each top-level guest window is represented in a host window. */
    VMMDev_Seamless_SizeHack         = 0x7fffffff
} VMMDevSeamlessMode;
AssertCompileSize(VMMDevSeamlessMode, 4);


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
AssertCompileSize(VMMDevCpuEventType, 4);


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
    VBoxGuestFacilityType_MonitorAttach   = 1101,
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
#  pragma pack(4) /* We force structure dword packing here for hysterical raisins.  Saves us 4 bytes, at the cost of
                     misaligning the value64 member of every other parameter structure. */
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

    int GetUInt32(uint32_t RT_FAR *pu32)
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

    int GetUInt64(uint64_t RT_FAR *pu64)
    {
        if (type == VMMDevHGCMParmType_64bit)
        {
            *pu64 = u.value64;
            return VINF_SUCCESS;
        }
        return VERR_INVALID_PARAMETER;
    }

    void SetPtr(void RT_FAR *pv, uint32_t cb)
    {
        type                    = VMMDevHGCMParmType_LinAddr;
        u.Pointer.size          = cb;
        u.Pointer.u.linearAddr  = (RTGCPTR32)(uintptr_t)pv;
    }
#  endif /* __cplusplus */
} HGCMFunctionParameter32;
#  pragma pack()
AssertCompileSize(HGCMFunctionParameter32, 4+8);

/**
 * HGCM function parameter, 64-bit client.
 */
#  pragma pack(4)/* We force structure dword packing here for hysterical raisins.  Saves us 4 bytes, at the cost of
                    misaligning the value64, physAddr and linearAddr members of every other parameter structure. */
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

    int GetUInt32(uint32_t RT_FAR *pu32)
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

    int GetUInt64(uint64_t RT_FAR *pu64)
    {
        if (type == VMMDevHGCMParmType_64bit)
        {
            *pu64 = u.value64;
            return VINF_SUCCESS;
        }
        return VERR_INVALID_PARAMETER;
    }

    void SetPtr(void RT_FAR *pv, uint32_t cb)
    {
        type                    = VMMDevHGCMParmType_LinAddr;
        u.Pointer.size          = cb;
        u.Pointer.u.linearAddr  = (uintptr_t)pv;
    }
#  endif /** __cplusplus */
} HGCMFunctionParameter64;
#  pragma pack()
AssertCompileSize(HGCMFunctionParameter64, 4+12);

/* Redefine the structure type for the guest code. */
#  ifndef VBOX_HGCM_HOST_CODE
#   if ARCH_BITS == 64
#     define HGCMFunctionParameter  HGCMFunctionParameter64
#   elif ARCH_BITS == 32 || ARCH_BITS == 16
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
#  pragma pack(4) /* We force structure dword packing here for hysterical raisins.  Saves us 4 bytes, at the cost of
                     misaligning the value64 member of every other parameter structure. */
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
#  pragma pack()
AssertCompileSize(HGCMFunctionParameter, 4+8);
# endif /* !VBOX_WITH_64_BITS_GUESTS */

/** @} */

#endif

