/** @file
 * VBoxGuestLib - VirtualBox Guest Additions Library.
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

#ifndef ___VBox_VBoxGuestLib_h
#define ___VBox_VBoxGuestLib_h

#include <VBox/types.h>
#include <VBox/VMMDev2.h>
#include <VBox/VMMDev.h>     /* grumble */
#ifdef IN_RING0
# include <VBox/VBoxGuest.h>
# include <VBox/VBoxGuest2.h>
#endif


/** @defgroup grp_vboxguest_lib     VirtualBox Guest Additions Library
 * @ingroup grp_vboxguest
 * @{
 */

/** @page pg_guest_lib  VirtualBox Guest Library
 *
 * This is a library for abstracting the additions driver interface. There are
 * multiple versions of the library depending on the context. The main
 * distinction is between kernel and user mode where the interfaces are very
 * different.
 *
 *
 * @section sec_guest_lib_ring0     Ring-0
 *
 * In ring-0 there are two version:
 *  - VBOX_LIB_VBGL_R0_BASE / VBoxGuestR0LibBase for the VBoxGuest main driver,
 *    who is responsible for managing the VMMDev virtual hardware.
 *  - VBOX_LIB_VBGL_R0 / VBoxGuestR0Lib for other (client) guest drivers.
 *
 *
 * The library source code and the header have a define VBGL_VBOXGUEST, which is
 * defined for VBoxGuest and undefined for other drivers. Drivers must choose
 * right library in their makefiles and set VBGL_VBOXGUEST accordingly.
 *
 * The libraries consists of:
 *  - common code to be used by both VBoxGuest and other drivers;
 *  - VBoxGuest specific code;
 *  - code for other drivers which communicate with VBoxGuest via an IOCTL.
 *
 *
 * @section sec_guest_lib_ring3     Ring-3
 *
 * There are more variants of the library here:
 *  - VBOX_LIB_VBGL_R3 / VBoxGuestR3Lib for programs.
 *  - VBOX_LIB_VBGL_R3_XFREE86 / VBoxGuestR3LibXFree86 for old style XFree
 *    drivers which uses special loader and or symbol resolving strategy.
 *  - VBOX_LIB_VBGL_R3_SHARED / VBoxGuestR3LibShared for shared objects / DLLs /
 *    Dylibs.
 *
 */

RT_C_DECLS_BEGIN

/** HGCM client ID.
 * @todo Promote to VBox/types.h  */
typedef uint32_t HGCMCLIENTID;


/** @defgroup grp_vboxguest_lib_r0     Ring-0 interface.
 * @{
 */
#if defined(IN_RING0) && !defined(IN_RING0_AGNOSTIC)
/** @def DECLR0VBGL
 * Declare a VBGL ring-0 API with the right calling convention and visibilitiy.
 * @param type      Return type.  */
# ifdef RT_OS_DARWIN /** @todo probably apply to all, but don't want a forest fire on our hands right now. */
#  define DECLR0VBGL(type) DECLHIDDEN(type) VBOXCALL
# else
#  define DECLR0VBGL(type) type VBOXCALL
# endif
# define DECLVBGL(type) DECLR0VBGL(type)


# ifdef VBGL_VBOXGUEST

/**
 * The library initialization function to be used by the main
 * VBoxGuest system driver.
 *
 * @return VBox status code.
 */
DECLVBGL(int) VbglInitPrimary(RTIOPORT portVMMDev, struct VMMDevMemory *pVMMDevMemory);

# else

/**
 * The library initialization function to be used by all drivers
 * other than the main VBoxGuest system driver.
 *
 * @return VBox status code.
 */
DECLVBGL(int) VbglInitClient(void);

# endif

/**
 * The library termination function.
 */
DECLVBGL(void) VbglTerminate (void);


/** @name Generic request functions.
 * @{
 */

/**
 * Allocate memory for generic request and initialize the request header.
 *
 * @returns VBox status code.
 * @param   ppReq       Where to return the pointer to the allocated memory.
 * @param   cbReq       Size of memory block required for the request.
 * @param   enmReqType  the generic request type.
 */
DECLVBGL(int) VbglGRAlloc(VMMDevRequestHeader **ppReq, size_t cbReq, VMMDevRequestType enmReqType);

/**
 * Perform the generic request.
 *
 * @param pReq     pointer the request structure.
 *
 * @return VBox status code.
 */
DECLVBGL(int) VbglGRPerform (VMMDevRequestHeader *pReq);

/**
 * Free the generic request memory.
 *
 * @param pReq     pointer the request structure.
 *
 * @return VBox status code.
 */
DECLVBGL(void) VbglGRFree (VMMDevRequestHeader *pReq);

/**
 * Verify the generic request header.
 *
 * @param pReq     pointer the request header structure.
 * @param cbReq    size of the request memory block. It should be equal to the request size
 *                 for fixed size requests. It can be greater than the request size for
 *                 variable size requests.
 *
 * @return VBox status code.
 */
DECLVBGL(int) VbglGRVerify (const VMMDevRequestHeader *pReq, size_t cbReq);
/** @} */

# ifdef VBOX_WITH_HGCM

#  ifdef VBGL_VBOXGUEST

/**
 * Callback function called from HGCM helpers when a wait for request
 * completion IRQ is required.
 *
 * @returns VINF_SUCCESS, VERR_INTERRUPT or VERR_TIMEOUT.
 * @param   pvData      VBoxGuest pointer to be passed to callback.
 * @param   u32Data     VBoxGuest 32 bit value to be passed to callback.
 */
typedef DECLCALLBACK(int) FNVBGLHGCMCALLBACK(VMMDevHGCMRequestHeader *pHeader, void *pvData, uint32_t u32Data);
/** Pointer to a FNVBGLHGCMCALLBACK. */
typedef FNVBGLHGCMCALLBACK *PFNVBGLHGCMCALLBACK;

/**
 * Perform a connect request. That is locate required service and
 * obtain a client identifier for future access.
 *
 * @note This function can NOT handle cancelled requests!
 *
 * @param   pConnectInfo        The request data.
 * @param   pfnAsyncCallback    Required pointer to function that is calledwhen
 *                              host returns VINF_HGCM_ASYNC_EXECUTE. VBoxGuest
 *                              implements waiting for an IRQ in this function.
 * @param   pvAsyncData         An arbitrary VBoxGuest pointer to be passed to callback.
 * @param   u32AsyncData        An arbitrary VBoxGuest 32 bit value to be passed to callback.
 *
 * @return  VBox status code.
 */

DECLR0VBGL(int) VbglR0HGCMInternalConnect (VBoxGuestHGCMConnectInfo *pConnectInfo,
                                           PFNVBGLHGCMCALLBACK pfnAsyncCallback, void *pvAsyncData, uint32_t u32AsyncData);


/**
 * Perform a disconnect request. That is tell the host that
 * the client will not call the service anymore.
 *
 * @note This function can NOT handle cancelled requests!
 *
 * @param   pDisconnectInfo     The request data.
 * @param   pfnAsyncCallback    Required pointer to function that is called when
 *                              host returns VINF_HGCM_ASYNC_EXECUTE. VBoxGuest
 *                              implements waiting for an IRQ in this function.
 * @param   pvAsyncData         An arbitrary VBoxGuest pointer to be passed to callback.
 * @param   u32AsyncData        An arbitrary VBoxGuest 32 bit value to be passed to
 *                              callback.
 *
 * @return  VBox status code.
 */

DECLR0VBGL(int) VbglR0HGCMInternalDisconnect (VBoxGuestHGCMDisconnectInfo *pDisconnectInfo,
                                              PFNVBGLHGCMCALLBACK pfnAsyncCallback, void *pvAsyncData, uint32_t u32AsyncData);

/** Call a HGCM service.
 *
 * @note This function can deal with cancelled requests.
 *
 * @param   pCallInfo           The request data.
 * @param   fFlags              Flags, see VBGLR0_HGCMCALL_F_XXX.
 * @param   pfnAsyncCallback    Required pointer to function that is called when
 *                              host returns VINF_HGCM_ASYNC_EXECUTE. VBoxGuest
 *                              implements waiting for an IRQ in this function.
 * @param   pvAsyncData         An arbitrary VBoxGuest pointer to be passed to callback.
 * @param   u32AsyncData        An arbitrary VBoxGuest 32 bit value to be passed to callback.
 *
 * @return VBox status code.
 */
DECLR0VBGL(int) VbglR0HGCMInternalCall (VBoxGuestHGCMCallInfo *pCallInfo, uint32_t cbCallInfo, uint32_t fFlags,
                                        PFNVBGLHGCMCALLBACK pfnAsyncCallback, void *pvAsyncData, uint32_t u32AsyncData);

/** Call a HGCM service. (32 bits packet structure in a 64 bits guest)
 *
 * @note This function can deal with cancelled requests.
 *
 * @param   pCallInfo           The request data.
 * @param   fFlags              Flags, see VBGLR0_HGCMCALL_F_XXX.
 * @param   pfnAsyncCallback    Required pointer to function that is called when
 *                              host returns VINF_HGCM_ASYNC_EXECUTE. VBoxGuest
 *                              implements waiting for an IRQ in this function.
 * @param   pvAsyncData         An arbitrary VBoxGuest pointer to be passed to callback.
 * @param   u32AsyncData        An arbitrary VBoxGuest 32 bit value to be passed to callback.
 *
 * @return  VBox status code.
 */
DECLR0VBGL(int) VbglR0HGCMInternalCall32 (VBoxGuestHGCMCallInfo *pCallInfo, uint32_t cbCallInfo, uint32_t fFlags,
                                          PFNVBGLHGCMCALLBACK pfnAsyncCallback, void *pvAsyncData, uint32_t u32AsyncData);

/** @name VbglR0HGCMInternalCall flags
 * @{ */
/** User mode request.
 * Indicates that only user mode addresses are permitted as parameters. */
#define VBGLR0_HGCMCALL_F_USER          UINT32_C(0)
/** Kernel mode request.
 * Indicates that kernel mode addresses are permitted as parameters. Whether or
 * not user mode addresses are permitted is, unfortunately, OS specific. The
 * following OSes allows user mode addresses: Windows, TODO.
 */
#define VBGLR0_HGCMCALL_F_KERNEL        UINT32_C(1)
/** Mode mask. */
#define VBGLR0_HGCMCALL_F_MODE_MASK     UINT32_C(1)
/** @} */

#  else  /* !VBGL_VBOXGUEST */

struct VBGLHGCMHANDLEDATA;
typedef struct VBGLHGCMHANDLEDATA *VBGLHGCMHANDLE;

/** @name HGCM functions
 * @{
 */

/**
 * Connect to a service.
 *
 * @param pHandle     Pointer to variable that will hold a handle to be used
 *                    further in VbglHGCMCall and VbglHGCMClose.
 * @param pData       Connection information structure.
 *
 * @return VBox status code.
 */
DECLVBGL(int) VbglHGCMConnect (VBGLHGCMHANDLE *pHandle, VBoxGuestHGCMConnectInfo *pData);

/**
 * Connect to a service.
 *
 * @param handle      Handle of the connection.
 * @param pData       Disconnect request information structure.
 *
 * @return VBox status code.
 */
DECLVBGL(int) VbglHGCMDisconnect (VBGLHGCMHANDLE handle, VBoxGuestHGCMDisconnectInfo *pData);

/**
 * Call to a service.
 *
 * @param handle      Handle of the connection.
 * @param pData       Call request information structure, including function parameters.
 * @param cbData      Length in bytes of data.
 *
 * @return VBox status code.
 */
DECLVBGL(int) VbglHGCMCall (VBGLHGCMHANDLE handle, VBoxGuestHGCMCallInfo *pData, uint32_t cbData);

/**
 * Call to a service with user-mode data received by the calling driver from the User-Mode process.
 * The call must be done in the context of a calling process.
 *
 * @param handle      Handle of the connection.
 * @param pData       Call request information structure, including function parameters.
 * @param cbData      Length in bytes of data.
 *
 * @return VBox status code.
 */
DECLVBGL(int) VbglHGCMCallUserData (VBGLHGCMHANDLE handle, VBoxGuestHGCMCallInfo *pData, uint32_t cbData);

/**
 * Call to a service with timeout.
 *
 * @param handle      Handle of the connection.
 * @param pData       Call request information structure, including function parameters.
 * @param cbData      Length in bytes of data.
 * @param cMillies    Timeout in milliseconds.  Use RT_INDEFINITE_WAIT to wait forever.
 *
 * @return VBox status code.
 */
DECLVBGL(int) VbglHGCMCallTimed(VBGLHGCMHANDLE handle, VBoxGuestHGCMCallInfoTimed *pData, uint32_t cbData);
/** @} */

/** @name Undocumented helpers for talking to the Chromium OpenGL Host Service
 * @{ */
typedef VBGLHGCMHANDLE VBGLCRCTLHANDLE;
DECLVBGL(int) VbglR0CrCtlCreate(VBGLCRCTLHANDLE *phCtl);
DECLVBGL(int) VbglR0CrCtlDestroy(VBGLCRCTLHANDLE hCtl);
DECLVBGL(int) VbglR0CrCtlConConnect(VBGLCRCTLHANDLE hCtl, HGCMCLIENTID *pidClient);
DECLVBGL(int) VbglR0CrCtlConDisconnect(VBGLCRCTLHANDLE hCtl, HGCMCLIENTID idClient);
DECLVBGL(int) VbglR0CrCtlConCall(VBGLCRCTLHANDLE hCtl, struct VBoxGuestHGCMCallInfo *pCallInfo, int cbCallInfo);
DECLVBGL(int) VbglR0CrCtlConCallUserData(VBGLCRCTLHANDLE hCtl, struct VBoxGuestHGCMCallInfo *pCallInfo, int cbCallInfo);
/** @} */

#  endif /* !VBGL_VBOXGUEST */

# endif /* VBOX_WITH_HGCM */


/**
 * Initialize the heap.
 *
 * @returns VBox status code.
 */
DECLVBGL(int) VbglPhysHeapInit (void);

/**
 * Shutdown the heap.
 */
DECLVBGL(void) VbglPhysHeapTerminate (void);

/**
 * Allocate a memory block.
 *
 * @returns Virtual address of the allocated memory block.
 * @param cbSize    Size of block to be allocated.
 */
DECLVBGL(void *) VbglPhysHeapAlloc (uint32_t cbSize);

/**
 * Get physical address of memory block pointed by the virtual address.
 *
 * @note WARNING!
 *       The function does not acquire the Heap mutex!
 *       When calling the function make sure that the pointer is a valid one and
 *       is not being deallocated.  This function can NOT be used for verifying
 *       if the given pointer is a valid one allocated from the heap.
 *
 * @param   pv      Virtual address of memory block.
 * @returns Physical address of the memory block.
 */
DECLVBGL(uint32_t)  VbglPhysHeapGetPhysAddr(void *pv);

/**
 * Free a memory block.
 *
 * @param   pv    Virtual address of memory block.
 */
DECLVBGL(void)      VbglPhysHeapFree(void *pv);

DECLVBGL(int) VbglQueryVMMDevMemory (VMMDevMemory **ppVMMDevMemory);
DECLR0VBGL(bool) VbglR0CanUsePhysPageList(void);

# ifndef VBOX_GUEST
/** @name Mouse
 * @{ */
DECLVBGL(int)     VbglSetMouseNotifyCallback(PFNVBOXGUESTMOUSENOTIFY pfnNotify, void *pvUser);
DECLVBGL(int)     VbglGetMouseStatus(uint32_t *pfFeatures, uint32_t *px, uint32_t *py);
DECLVBGL(int)     VbglSetMouseStatus(uint32_t fFeatures);
/** @}  */
# endif /* VBOX_GUEST */

#endif /* IN_RING0 && !IN_RING0_AGNOSTIC */

/** @} */


/** @defgroup grp_vboxguest_lib_r3      Ring-3 interface.
 * @{
 */
#ifdef IN_RING3

/** @def VBGLR3DECL
 * Ring 3 VBGL declaration.
 * @param   type    The return type of the function declaration.
 */
# define VBGLR3DECL(type) DECLHIDDEN(type) VBOXCALL

/** @name General-purpose functions
 * @{ */
VBGLR3DECL(int)     VbglR3Init(void);
VBGLR3DECL(int)     VbglR3InitUser(void);
VBGLR3DECL(void)    VbglR3Term(void);
# ifdef ___iprt_time_h
VBGLR3DECL(int)     VbglR3GetHostTime(PRTTIMESPEC pTime);
# endif
VBGLR3DECL(int)     VbglR3InterruptEventWaits(void);
VBGLR3DECL(int)     VbglR3WriteLog(const char *pch, size_t cch);
VBGLR3DECL(int)     VbglR3CtlFilterMask(uint32_t fOr, uint32_t fNot);
VBGLR3DECL(int)     VbglR3Daemonize(bool fNoChDir, bool fNoClose, bool fRespawn, unsigned *pcRespawn);
VBGLR3DECL(int)     VbglR3PidFile(const char *pszPath, PRTFILE phFile);
VBGLR3DECL(void)    VbglR3ClosePidFile(const char *pszPath, RTFILE hFile);
VBGLR3DECL(int)     VbglR3SetGuestCaps(uint32_t fOr, uint32_t fNot);
VBGLR3DECL(int)     VbglR3WaitEvent(uint32_t fMask, uint32_t cMillies, uint32_t *pfEvents);

VBGLR3DECL(int)     VbglR3ReportAdditionsStatus(VBoxGuestFacilityType Facility, VBoxGuestFacilityStatus StatusCurrent,
                                                uint32_t fFlags);
VBGLR3DECL(int)     VbglR3GetAdditionsVersion(char **ppszVer, char **ppszVerEx, char **ppszRev);
VBGLR3DECL(int)     VbglR3GetAdditionsInstallationPath(char **ppszPath);
VBGLR3DECL(int)     VbglR3GetSessionId(uint64_t *pu64IdSession);

/** @} */

/** @name Shared clipboard
 * @{ */
VBGLR3DECL(int)     VbglR3ClipboardConnect(HGCMCLIENTID *pidClient);
VBGLR3DECL(int)     VbglR3ClipboardDisconnect(HGCMCLIENTID idClient);
VBGLR3DECL(int)     VbglR3ClipboardGetHostMsg(HGCMCLIENTID idClient, uint32_t *pMsg, uint32_t *pfFormats);
VBGLR3DECL(int)     VbglR3ClipboardReadData(HGCMCLIENTID idClient, uint32_t fFormat, void *pv, uint32_t cb, uint32_t *pcb);
VBGLR3DECL(int)     VbglR3ClipboardReportFormats(HGCMCLIENTID idClient, uint32_t fFormats);
VBGLR3DECL(int)     VbglR3ClipboardWriteData(HGCMCLIENTID idClient, uint32_t fFormat, void *pv, uint32_t cb);
/** @} */

/** @name Seamless mode
 * @{ */
VBGLR3DECL(int)     VbglR3SeamlessSetCap(bool fState);
VBGLR3DECL(int)     VbglR3SeamlessWaitEvent(VMMDevSeamlessMode *pMode);
VBGLR3DECL(int)     VbglR3SeamlessSendRects(uint32_t cRects, PRTRECT pRects);
VBGLR3DECL(int)     VbglR3SeamlessGetLastEvent(VMMDevSeamlessMode *pMode);

/** @}  */

/** @name Mouse
 * @{ */
VBGLR3DECL(int)     VbglR3GetMouseStatus(uint32_t *pfFeatures, uint32_t *px, uint32_t *py);
VBGLR3DECL(int)     VbglR3SetMouseStatus(uint32_t fFeatures);
/** @}  */

/** @name Video
 * @{ */
VBGLR3DECL(int)     VbglR3VideoAccelEnable(bool fEnable);
VBGLR3DECL(int)     VbglR3VideoAccelFlush(void);
VBGLR3DECL(int)     VbglR3SetPointerShape(uint32_t fFlags, uint32_t xHot, uint32_t yHot, uint32_t cx, uint32_t cy,
                                          const void *pvImg, size_t cbImg);
VBGLR3DECL(int)     VbglR3SetPointerShapeReq(struct VMMDevReqMousePointer *pReq);
/** @}  */

/** @name Display
 * @{ */
/** The folder for the video mode hint unix domain socket on Unix-like guests.
 * @note This can be safely changed as all users are rebuilt in lock-step. */
#define VBGLR3HOSTDISPSOCKETPATH "/tmp/.VBoxService"
/** The path to the video mode hint unix domain socket on Unix-like guests. */
#define VBGLR3HOSTDISPSOCKET        VBGLR3VIDEOMODEHINTSOCKETPATH "/VideoModeHint"

/** The folder for saving video mode hints to between sessions. */
#define VBGLR3HOSTDISPSAVEDMODEPATH "/var/lib/VBoxGuestAdditions"
/** The path to the file for saving video mode hints to between sessions. */
#define VBGLR3HOSTDISPSAVEDMODE     VBGLR3HOSTDISPSAVEDMODEPATH "/SavedVideoModes"

VBGLR3DECL(int)     VbglR3GetDisplayChangeRequest(uint32_t *pcx, uint32_t *pcy, uint32_t *pcBits, uint32_t *piDisplay,
                                                  uint32_t *pdx, uint32_t *pdy, bool *pfEnabled, bool *pfChangeOrigin, bool fAck);
VBGLR3DECL(bool)    VbglR3HostLikesVideoMode(uint32_t cx, uint32_t cy, uint32_t cBits);
VBGLR3DECL(int)     VbglR3VideoModeGetHighestSavedScreen(unsigned *pcScreen);
VBGLR3DECL(int)     VbglR3SaveVideoMode(unsigned cScreen, unsigned cx, unsigned cy, unsigned cBits,
                                        unsigned x, unsigned y, bool fEnabled);
VBGLR3DECL(int)     VbglR3RetrieveVideoMode(unsigned cScreen, unsigned *pcx, unsigned *pcy, unsigned *pcBits,
                                            unsigned *px, unsigned *py, bool *pfEnabled);
/** @}  */

/** @name VM Statistics
 * @{ */
VBGLR3DECL(int)     VbglR3StatQueryInterval(uint32_t *pu32Interval);
VBGLR3DECL(int)     VbglR3StatReport(VMMDevReportGuestStats *pReq);
/** @}  */

/** @name Memory ballooning
 * @{ */
VBGLR3DECL(int)     VbglR3MemBalloonRefresh(uint32_t *pcChunks, bool *pfHandleInR3);
VBGLR3DECL(int)     VbglR3MemBalloonChange(void *pv, bool fInflate);
/** @}  */

/** @name Core Dump
 * @{ */
VBGLR3DECL(int)     VbglR3WriteCoreDump(void);

/** @}  */

# ifdef VBOX_WITH_GUEST_PROPS
/** @name Guest properties
 * @{ */
/** @todo Docs. */
typedef struct VBGLR3GUESTPROPENUM VBGLR3GUESTPROPENUM;
/** @todo Docs. */
typedef VBGLR3GUESTPROPENUM *PVBGLR3GUESTPROPENUM;
VBGLR3DECL(int)     VbglR3GuestPropConnect(uint32_t *pidClient);
VBGLR3DECL(int)     VbglR3GuestPropDisconnect(HGCMCLIENTID idClient);
VBGLR3DECL(int)     VbglR3GuestPropWrite(HGCMCLIENTID idClient, const char *pszName, const char *pszValue, const char *pszFlags);
VBGLR3DECL(int)     VbglR3GuestPropWriteValue(HGCMCLIENTID idClient, const char *pszName, const char *pszValue);
VBGLR3DECL(int)     VbglR3GuestPropWriteValueV(HGCMCLIENTID idClient, const char *pszName,
                                               const char *pszValueFormat, va_list va) RT_IPRT_FORMAT_ATTR(3, 0);
VBGLR3DECL(int)     VbglR3GuestPropWriteValueF(HGCMCLIENTID idClient, const char *pszName,
                                               const char *pszValueFormat, ...) RT_IPRT_FORMAT_ATTR(3, 4);
VBGLR3DECL(int)     VbglR3GuestPropRead(HGCMCLIENTID idClient, const char *pszName, void *pvBuf, uint32_t cbBuf, char **ppszValue,
                                        uint64_t *pu64Timestamp, char **ppszFlags, uint32_t *pcbBufActual);
VBGLR3DECL(int)     VbglR3GuestPropReadValue(uint32_t ClientId, const char *pszName, char *pszValue, uint32_t cchValue,
                                             uint32_t *pcchValueActual);
VBGLR3DECL(int)     VbglR3GuestPropReadValueAlloc(HGCMCLIENTID idClient, const char *pszName, char **ppszValue);
VBGLR3DECL(void)    VbglR3GuestPropReadValueFree(char *pszValue);
VBGLR3DECL(int)     VbglR3GuestPropEnumRaw(HGCMCLIENTID idClient, const char *paszPatterns, char *pcBuf, uint32_t cbBuf,
                                           uint32_t *pcbBufActual);
VBGLR3DECL(int)     VbglR3GuestPropEnum(HGCMCLIENTID idClient, char const * const *ppaszPatterns, uint32_t cPatterns,
                                        PVBGLR3GUESTPROPENUM *ppHandle, char const **ppszName, char const **ppszValue,
                                        uint64_t *pu64Timestamp, char const **ppszFlags);
VBGLR3DECL(int)     VbglR3GuestPropEnumNext(PVBGLR3GUESTPROPENUM pHandle, char const **ppszName, char const **ppszValue,
                                            uint64_t *pu64Timestamp, char const **ppszFlags);
VBGLR3DECL(void)    VbglR3GuestPropEnumFree(PVBGLR3GUESTPROPENUM pHandle);
VBGLR3DECL(int)     VbglR3GuestPropDelete(HGCMCLIENTID idClient, const char *pszName);
VBGLR3DECL(int)     VbglR3GuestPropDelSet(HGCMCLIENTID idClient, char const * const *papszPatterns, uint32_t cPatterns);
VBGLR3DECL(int)     VbglR3GuestPropWait(HGCMCLIENTID idClient, const char *pszPatterns, void *pvBuf, uint32_t cbBuf,
                                        uint64_t u64Timestamp, uint32_t cMillies, char ** ppszName, char **ppszValue,
                                        uint64_t *pu64Timestamp, char **ppszFlags, uint32_t *pcbBufActual);
/** @}  */

/** @name Guest user handling / reporting.
 * @{ */
VBGLR3DECL(int)     VbglR3GuestUserReportState(const char *pszUser, const char *pszDomain, VBoxGuestUserState enmState,
                                               uint8_t *pbDetails, uint32_t cbDetails);
/** @}  */

/** @name Host version handling
 * @{ */
VBGLR3DECL(int)     VbglR3HostVersionCheckForUpdate(HGCMCLIENTID idClient, bool *pfUpdate, char **ppszHostVersion,
                                                    char **ppszGuestVersion);
VBGLR3DECL(int)     VbglR3HostVersionLastCheckedLoad(HGCMCLIENTID idClient, char **ppszVer);
VBGLR3DECL(int)     VbglR3HostVersionLastCheckedStore(HGCMCLIENTID idClient, const char *pszVer);
/** @}  */
# endif /* VBOX_WITH_GUEST_PROPS defined */

# ifdef VBOX_WITH_SHARED_FOLDERS
/** @name Shared folders
 * @{ */
/**
 * Structure containing mapping information for a shared folder.
 */
typedef struct VBGLR3SHAREDFOLDERMAPPING
{
    /** Mapping status. */
    uint32_t u32Status;
    /** Root handle. */
    uint32_t u32Root;
} VBGLR3SHAREDFOLDERMAPPING;
/** Pointer to a shared folder mapping information structure. */
typedef VBGLR3SHAREDFOLDERMAPPING *PVBGLR3SHAREDFOLDERMAPPING;
/** Pointer to a const shared folder mapping information structure. */
typedef VBGLR3SHAREDFOLDERMAPPING const *PCVBGLR3SHAREDFOLDERMAPPING;

VBGLR3DECL(int)     VbglR3SharedFolderConnect(uint32_t *pidClient);
VBGLR3DECL(int)     VbglR3SharedFolderDisconnect(HGCMCLIENTID idClient);
VBGLR3DECL(bool)    VbglR3SharedFolderExists(HGCMCLIENTID idClient, const char *pszShareName);
VBGLR3DECL(int)     VbglR3SharedFolderGetMappings(HGCMCLIENTID idClient, bool fAutoMountOnly,
                                                  PVBGLR3SHAREDFOLDERMAPPING *ppaMappings, uint32_t *pcMappings);
VBGLR3DECL(void)    VbglR3SharedFolderFreeMappings(PVBGLR3SHAREDFOLDERMAPPING paMappings);
VBGLR3DECL(int)     VbglR3SharedFolderGetName(HGCMCLIENTID  idClient,uint32_t u32Root, char **ppszName);
VBGLR3DECL(int)     VbglR3SharedFolderGetMountPrefix(char **ppszPrefix);
VBGLR3DECL(int)     VbglR3SharedFolderGetMountDir(char **ppszDir);
/** @}  */
# endif /* VBOX_WITH_SHARED_FOLDERS defined */

# ifdef VBOX_WITH_GUEST_CONTROL
/** @name Guest control
 * @{ */

/**
 * Structure containing the context required for
 * either retrieving or sending a HGCM guest control
 * commands from or to the host.
 *
 * Note: Do not change parameter order without also
 *       adapting all structure initializers.
 */
typedef struct VBGLR3GUESTCTRLCMDCTX
{
    /** @todo This struct could be handy if we want to implement
     *        a second communication channel, e.g. via TCP/IP.
     *        Use a union for the HGCM stuff then. */

    /** IN: HGCM client ID to use for
     *      communication. */
    uint32_t uClientID;
    /** IN/OUT: Context ID to retrieve
     *          or to use. */
    uint32_t uContextID;
    /** IN: Protocol version to use. */
    uint32_t uProtocol;
    /** OUT: Number of parameters retrieved. */
    uint32_t uNumParms;
} VBGLR3GUESTCTRLCMDCTX, *PVBGLR3GUESTCTRLCMDCTX;

/* General message handling on the guest. */
VBGLR3DECL(int) VbglR3GuestCtrlConnect(uint32_t *pidClient);
VBGLR3DECL(int) VbglR3GuestCtrlDisconnect(uint32_t uClientId);
VBGLR3DECL(int) VbglR3GuestCtrlMsgFilterSet(uint32_t uClientId, uint32_t uValue, uint32_t uMaskAdd, uint32_t uMaskRemove);
VBGLR3DECL(int) VbglR3GuestCtrlMsgFilterUnset(uint32_t uClientId);
VBGLR3DECL(int) VbglR3GuestCtrlMsgReply(PVBGLR3GUESTCTRLCMDCTX pCtx, int rc);
VBGLR3DECL(int) VbglR3GuestCtrlMsgReplyEx(PVBGLR3GUESTCTRLCMDCTX pCtx, int rc, uint32_t uType,
                                          void *pvPayload, uint32_t cbPayload);
VBGLR3DECL(int) VbglR3GuestCtrlMsgSkip(uint32_t uClientId);
VBGLR3DECL(int) VbglR3GuestCtrlMsgWaitFor(uint32_t uClientId, uint32_t *puMsg, uint32_t *puNumParms);
VBGLR3DECL(int) VbglR3GuestCtrlCancelPendingWaits(HGCMCLIENTID idClient);
/* Guest session handling. */
VBGLR3DECL(int) VbglR3GuestCtrlSessionClose(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t fFlags);
VBGLR3DECL(int) VbglR3GuestCtrlSessionNotify(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t uType, uint32_t uResult);
VBGLR3DECL(int) VbglR3GuestCtrlSessionGetOpen(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t *puProtocol, char *pszUser, uint32_t cbUser,
                                              char *pszPassword, uint32_t  cbPassword, char *pszDomain, uint32_t cbDomain,
                                              uint32_t *pfFlags, uint32_t *pidSession);
VBGLR3DECL(int) VbglR3GuestCtrlSessionGetClose(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t *pfFlags, uint32_t *pidSession);
/* Guest path handling. */
VBGLR3DECL(int) VbglR3GuestCtrlPathGetRename(PVBGLR3GUESTCTRLCMDCTX pCtx, char *pszSource, uint32_t cbSource, char *pszDest,
                                             uint32_t cbDest, uint32_t *pfFlags);
/* Guest process execution. */
VBGLR3DECL(int) VbglR3GuestCtrlProcGetStart(PVBGLR3GUESTCTRLCMDCTX pCtx, char *pszCmd, uint32_t cbCmd, uint32_t *pfFlags,
                                            char *pszArgs, uint32_t cbArgs, uint32_t *puNumArgs, char *pszEnv, uint32_t *pcbEnv,
                                            uint32_t *puNumEnvVars, char *pszUser, uint32_t cbUser, char *pszPassword,
                                            uint32_t cbPassword, uint32_t *puTimeoutMS, uint32_t *puPriority,
                                            uint64_t *puAffinity, uint32_t cbAffinity, uint32_t *pcAffinity);
VBGLR3DECL(int) VbglR3GuestCtrlProcGetTerminate(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t *puPID);
VBGLR3DECL(int) VbglR3GuestCtrlProcGetInput(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t *puPID, uint32_t *pfFlags, void *pvData,
                                            uint32_t cbData, uint32_t *pcbSize);
VBGLR3DECL(int) VbglR3GuestCtrlProcGetOutput(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t *puPID, uint32_t *puHandle, uint32_t *pfFlags);
VBGLR3DECL(int) VbglR3GuestCtrlProcGetWaitFor(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t *puPID, uint32_t *puWaitFlags,
                                              uint32_t *puTimeoutMS);
/* Guest native directory handling. */
VBGLR3DECL(int) VbglR3GuestCtrlDirGetRemove(PVBGLR3GUESTCTRLCMDCTX pCtx, char *pszPath, uint32_t cbPath, uint32_t *pfFlags);
/* Guest native file handling. */
VBGLR3DECL(int) VbglR3GuestCtrlFileGetOpen(PVBGLR3GUESTCTRLCMDCTX pCtx, char *pszFileName, uint32_t cbFileName, char *pszOpenMode,
                                           uint32_t cbOpenMode, char *pszDisposition, uint32_t cbDisposition, char *pszSharing,
                                           uint32_t cbSharing, uint32_t *puCreationMode, uint64_t *puOffset);
VBGLR3DECL(int) VbglR3GuestCtrlFileGetClose(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t *puHandle);
VBGLR3DECL(int) VbglR3GuestCtrlFileGetRead(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t *puHandle, uint32_t *puToRead);
VBGLR3DECL(int) VbglR3GuestCtrlFileGetReadAt(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t *puHandle,
                                             uint32_t *puToRead, uint64_t *poffRead);
VBGLR3DECL(int) VbglR3GuestCtrlFileGetWrite(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t *puHandle,
                                            void *pvData, uint32_t cbData, uint32_t *pcbActual);
VBGLR3DECL(int) VbglR3GuestCtrlFileGetWriteAt(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t *puHandle, void *pvData, uint32_t cbData,
                                              uint32_t *pcbActual, uint64_t *poffWrite);
VBGLR3DECL(int) VbglR3GuestCtrlFileGetSeek(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t *puHandle,
                                           uint32_t *puSeekMethod, uint64_t *poffSeek);
VBGLR3DECL(int) VbglR3GuestCtrlFileGetTell(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t *puHandle);
/* Guest -> Host. */
VBGLR3DECL(int) VbglR3GuestCtrlFileCbOpen(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t uRc, uint32_t uFileHandle);
VBGLR3DECL(int) VbglR3GuestCtrlFileCbClose(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t uRc);
VBGLR3DECL(int) VbglR3GuestCtrlFileCbError(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t uRc);
VBGLR3DECL(int) VbglR3GuestCtrlFileCbRead(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t uRc, void *pvData, uint32_t cbData);
VBGLR3DECL(int) VbglR3GuestCtrlFileCbWrite(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t uRc, uint32_t uWritten);
VBGLR3DECL(int) VbglR3GuestCtrlFileCbSeek(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t uRc, uint64_t uOffActual);
VBGLR3DECL(int) VbglR3GuestCtrlFileCbTell(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t uRc, uint64_t uOffActual);
VBGLR3DECL(int) VbglR3GuestCtrlProcCbStatus(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t uPID, uint32_t uStatus, uint32_t fFlags,
                                            void *pvData, uint32_t cbData);
VBGLR3DECL(int) VbglR3GuestCtrlProcCbOutput(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t uPID, uint32_t uHandle, uint32_t fFlags,
                                            void *pvData, uint32_t cbData);
VBGLR3DECL(int) VbglR3GuestCtrlProcCbStatusInput(PVBGLR3GUESTCTRLCMDCTX pCtx, uint32_t u32PID, uint32_t uStatus,
                                                 uint32_t fFlags, uint32_t cbWritten);

/** @}  */
# endif /* VBOX_WITH_GUEST_CONTROL defined */

/** @name Auto-logon handling
 * @{ */
VBGLR3DECL(int)     VbglR3AutoLogonReportStatus(VBoxGuestFacilityStatus enmStatus);
VBGLR3DECL(bool)    VbglR3AutoLogonIsRemoteSession(void);
/** @}  */

/** @name User credentials handling
 * @{ */
VBGLR3DECL(int)     VbglR3CredentialsQueryAvailability(void);
VBGLR3DECL(int)     VbglR3CredentialsRetrieve(char **ppszUser, char **ppszPassword, char **ppszDomain);
VBGLR3DECL(int)     VbglR3CredentialsRetrieveUtf16(PRTUTF16 *ppwszUser, PRTUTF16 *ppwszPassword, PRTUTF16 *ppwszDomain);
VBGLR3DECL(void)    VbglR3CredentialsDestroy(char *pszUser, char *pszPassword, char *pszDomain, uint32_t cPasses);
VBGLR3DECL(void)    VbglR3CredentialsDestroyUtf16(PRTUTF16 pwszUser, PRTUTF16 pwszPassword, PRTUTF16 pwszDomain,
                                                  uint32_t cPasses);
/** @}  */

/** @name CPU hotplug monitor
 * @{ */
VBGLR3DECL(int)     VbglR3CpuHotPlugInit(void);
VBGLR3DECL(int)     VbglR3CpuHotPlugTerm(void);
VBGLR3DECL(int)     VbglR3CpuHotPlugWaitForEvent(VMMDevCpuEventType *penmEventType, uint32_t *pidCpuCore, uint32_t *pidCpuPackage);
/** @} */

/** @name Page sharing
 * @{ */
VBGLR3DECL(int)     VbglR3RegisterSharedModule(char *pszModuleName, char *pszVersion, RTGCPTR64  GCBaseAddr, uint32_t cbModule,
                                               unsigned cRegions, VMMDEVSHAREDREGIONDESC *pRegions);
VBGLR3DECL(int)     VbglR3UnregisterSharedModule(char *pszModuleName, char *pszVersion, RTGCPTR64  GCBaseAddr, uint32_t cbModule);
VBGLR3DECL(int)     VbglR3CheckSharedModules(void);
VBGLR3DECL(bool)    VbglR3PageSharingIsEnabled(void);
VBGLR3DECL(int)     VbglR3PageIsShared(RTGCPTR pPage, bool *pfShared, uint64_t *puPageFlags);
/** @} */

# ifdef VBOX_WITH_DRAG_AND_DROP
/** @name Drag and Drop
 * @{ */
/**
 * Structure containing the context required for
 * either retrieving or sending a HGCM guest drag'n drop
 * commands from or to the host.
 *
 * Note: Do not change parameter order without also
 *       adapting all structure initializers.
 */
typedef struct VBGLR3GUESTDNDCMDCTX
{
    /** @todo This struct could be handy if we want to implement
     *        a second communication channel, e.g. via TCP/IP.
     *        Use a union for the HGCM stuff then. */

    /** HGCM client ID to use for communication. */
    uint32_t uClientID;
    /** The VM's current session ID. */
    uint64_t uSessionID;
    /** Protocol version to use. */
    uint32_t uProtocol;
    /** Number of parameters retrieved for the current command. */
    uint32_t uNumParms;
    /** Max chunk size (in bytes) for data transfers. */
    uint32_t cbMaxChunkSize;
} VBGLR3GUESTDNDCMDCTX, *PVBGLR3GUESTDNDCMDCTX;

typedef struct VBGLR3DNDHGCMEVENT
{
    uint32_t uType;               /** The event type this struct contains. */
    uint32_t uScreenId;           /** Screen ID this request belongs to. */
    char    *pszFormats;          /** Format list (\r\n separated). */
    uint32_t cbFormats;           /** Size (in bytes) of pszFormats (\0 included). */
    union
    {
        struct
        {
            uint32_t uXpos;       /** X position of guest screen. */
            uint32_t uYpos;       /** Y position of guest screen. */
            uint32_t uDefAction;  /** Proposed DnD action. */
            uint32_t uAllActions; /** Allowed DnD actions. */
        } a; /** Values used in init, move and drop event type. */
        struct
        {
            void    *pvData;      /** Data request. */
            uint32_t cbData;      /** Size (in bytes) of pvData. */
        } b; /** Values used in drop data event type. */
    } u;
} VBGLR3DNDHGCMEVENT;
typedef VBGLR3DNDHGCMEVENT *PVBGLR3DNDHGCMEVENT;
typedef const PVBGLR3DNDHGCMEVENT CPVBGLR3DNDHGCMEVENT;
VBGLR3DECL(int)     VbglR3DnDConnect(PVBGLR3GUESTDNDCMDCTX pCtx);
VBGLR3DECL(int)     VbglR3DnDDisconnect(PVBGLR3GUESTDNDCMDCTX pCtx);

VBGLR3DECL(int)     VbglR3DnDRecvNextMsg(PVBGLR3GUESTDNDCMDCTX pCtx, CPVBGLR3DNDHGCMEVENT pEvent);

VBGLR3DECL(int)     VbglR3DnDHGSendAckOp(PVBGLR3GUESTDNDCMDCTX pCtx, uint32_t uAction);
VBGLR3DECL(int)     VbglR3DnDHGSendReqData(PVBGLR3GUESTDNDCMDCTX pCtx, const char *pcszFormat);
VBGLR3DECL(int)     VbglR3DnDHGSendProgress(PVBGLR3GUESTDNDCMDCTX pCtx, uint32_t uStatus, uint8_t uPercent, int rcErr);
#  ifdef VBOX_WITH_DRAG_AND_DROP_GH
VBGLR3DECL(int)     VbglR3DnDGHSendAckPending(PVBGLR3GUESTDNDCMDCTX pCtx, uint32_t uDefAction, uint32_t uAllActions, const char* pcszFormats, uint32_t cbFormats);
VBGLR3DECL(int)     VbglR3DnDGHSendData(PVBGLR3GUESTDNDCMDCTX pCtx, const char *pszFormat, void *pvData, uint32_t cbData);
VBGLR3DECL(int)     VbglR3DnDGHSendError(PVBGLR3GUESTDNDCMDCTX pCtx, int rcOp);
#  endif /* VBOX_WITH_DRAG_AND_DROP_GH */
/** @} */
# endif /* VBOX_WITH_DRAG_AND_DROP */

/* Generic Host Channel Service. */
VBGLR3DECL(int)  VbglR3HostChannelInit(uint32_t *pu32HGCMClientId);
VBGLR3DECL(void) VbglR3HostChannelTerm(uint32_t u32HGCMClientId);
VBGLR3DECL(int)  VbglR3HostChannelAttach(uint32_t *pu32ChannelHandle, uint32_t u32HGCMClientId,
                                         const char *pszName, uint32_t u32Flags);
VBGLR3DECL(void) VbglR3HostChannelDetach(uint32_t u32ChannelHandle, uint32_t u32HGCMClientId);
VBGLR3DECL(int)  VbglR3HostChannelSend(uint32_t u32ChannelHandle, uint32_t u32HGCMClientId,
                                       void *pvData, uint32_t cbData);
VBGLR3DECL(int)  VbglR3HostChannelRecv(uint32_t u32ChannelHandle, uint32_t u32HGCMClientId,
                                       void *pvData, uint32_t cbData,
                                       uint32_t *pu32SizeReceived, uint32_t *pu32SizeRemaining);
VBGLR3DECL(int)  VbglR3HostChannelControl(uint32_t u32ChannelHandle, uint32_t u32HGCMClientId,
                                         uint32_t u32Code, void *pvParm, uint32_t cbParm,
                                         void *pvData, uint32_t cbData, uint32_t *pu32SizeDataReturned);
VBGLR3DECL(int)  VbglR3HostChannelEventWait(uint32_t *pu32ChannelHandle, uint32_t u32HGCMClientId,
                                            uint32_t *pu32EventId, void *pvParm, uint32_t cbParm,
                                            uint32_t *pu32SizeReturned);
VBGLR3DECL(int)  VbglR3HostChannelEventCancel(uint32_t u32ChannelHandle, uint32_t u32HGCMClientId);
VBGLR3DECL(int)  VbglR3HostChannelQuery(const char *pszName, uint32_t u32HGCMClientId, uint32_t u32Code,
                                        void *pvParm, uint32_t cbParm, void *pvData, uint32_t cbData,
                                        uint32_t *pu32SizeDataReturned);

/** @name Mode hint storage
 * @{ */
VBGLR3DECL(int) VbglR3ReadVideoMode(unsigned cDisplay, unsigned *cx,
                                    unsigned *cy, unsigned *cBPP, unsigned *x,
                                    unsigned *y, unsigned *fEnabled);
VBGLR3DECL(int) VbglR3WriteVideoMode(unsigned cDisplay, unsigned cx,
                                     unsigned cy, unsigned cBPP, unsigned x,
                                     unsigned y, unsigned fEnabled);
/** @} */

/** @name Generic HGCM
 * @{ */
VBGLR3DECL(int)     VbglR3HGCMConnect(const char *pszServiceName, HGCMCLIENTID *pidClient);
VBGLR3DECL(int)     VbglR3HGCMDisconnect(HGCMCLIENTID idClient);
/** @} */

#endif /* IN_RING3 */
/** @} */

RT_C_DECLS_END

/** @} */

#endif

