/** @file
 * VBoxGuest - VirtualBox Guest Additions Driver Interface. (ADD,DEV)
 *
 * @remarks This is in the process of being split up and usage cleaned up.
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

#ifndef ___VBox_VBoxGuest_h
#define ___VBox_VBoxGuest_h

#include <VBox/cdefs.h>
#include <VBox/types.h>
#include <iprt/assert.h>
#include <VBox/VMMDev2.h>
#include <VBox/VBoxGuest2.h>


/** @defgroup grp_vboxguest  VirtualBox Guest Additions Device Driver
 *
 * Also know as VBoxGuest.
 *
 * @{
 */

/** @defgroup grp_vboxguest_ioc  VirtualBox Guest Additions Driver Interface
 * @{
 */

/** @todo It would be nice if we could have two defines without paths. */

/** @def VBOXGUEST_DEVICE_NAME
 * The support device name. */
/** @def VBOXGUEST_USER_DEVICE_NAME
 * The support device name of the user accessible device node. */

#if defined(RT_OS_OS2)
# define VBOXGUEST_DEVICE_NAME          "\\Dev\\VBoxGst$"

#elif defined(RT_OS_WINDOWS)
# define VBOXGUEST_DEVICE_NAME          "\\\\.\\VBoxGuest"

/** The support service name. */
# define VBOXGUEST_SERVICE_NAME         "VBoxGuest"
/** Global name for Win2k+ */
# define VBOXGUEST_DEVICE_NAME_GLOBAL   "\\\\.\\Global\\VBoxGuest"
/** Win32 driver name */
# define VBOXGUEST_DEVICE_NAME_NT       L"\\Device\\VBoxGuest"
/** Device name. */
# define VBOXGUEST_DEVICE_NAME_DOS      L"\\DosDevices\\VBoxGuest"

#elif defined(RT_OS_HAIKU)
# define VBOXGUEST_DEVICE_NAME          "/dev/misc/vboxguest"

#else /* (PORTME) */
# define VBOXGUEST_DEVICE_NAME          "/dev/vboxguest"
# if defined(RT_OS_LINUX)
#  define VBOXGUEST_USER_DEVICE_NAME    "/dev/vboxuser"
# endif
#endif

#ifndef VBOXGUEST_USER_DEVICE_NAME
# define VBOXGUEST_USER_DEVICE_NAME     VBOXGUEST_DEVICE_NAME
#endif

/** Fictive start address of the hypervisor physical memory for MmMapIoSpace. */
#define VBOXGUEST_HYPERVISOR_PHYSICAL_START     UINT32_C(0xf8000000)

#ifdef RT_OS_DARWIN
/** Cookie used to fend off some unwanted clients to the IOService. */
# define VBOXGUEST_DARWIN_IOSERVICE_COOKIE      UINT32_C(0x56426f78) /* 'VBox' */
#endif

#if !defined(IN_RC) && !defined(IN_RING0_AGNOSTIC) && !defined(IPRT_NO_CRT)

/** @name VBoxGuest IOCTL codes and structures.
 *
 * The range 0..15 is for basic driver communication.
 * The range 16..31 is for HGCM communication.
 * The range 32..47 is reserved for future use.
 * The range 48..63 is for OS specific communication.
 * The 7th bit is reserved for future hacks.
 * The 8th bit is reserved for distinguishing between 32-bit and 64-bit
 * processes in future 64-bit guest additions.
 *
 * @remarks When creating new IOCtl interfaces keep in mind that not all OSes supports
 *          reporting back the output size. (This got messed up a little bit in VBoxDrv.)
 *
 *          The request size is also a little bit tricky as it's passed as part of the
 *          request code on unix. The size field is 14 bits on Linux, 12 bits on *BSD,
 *          13 bits Darwin, and 8-bits on Solaris. All the BSDs and Darwin kernels
 *          will make use of the size field, while Linux and Solaris will not. We're of
 *          course using the size to validate and/or map/lock the request, so it has
 *          to be valid.
 *
 *          For Solaris we will have to do something special though, 255 isn't
 *          sufficient for all we need. A 4KB restriction (BSD) is probably not
 *          too problematic (yet) as a general one.
 *
 *          More info can be found in SUPDRVIOC.h and related sources.
 *
 * @remarks If adding interfaces that only has input or only has output, some new macros
 *          needs to be created so the most efficient IOCtl data buffering method can be
 *          used.
 * @{
 */
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_SPARC64)
# define VBOXGUEST_IOCTL_FLAG     128
#elif defined(RT_ARCH_X86) || defined(RT_ARCH_SPARC)
# define VBOXGUEST_IOCTL_FLAG     0
#else
# error "dunno which arch this is!"
#endif
/** @} */

/** Ring-3 request wrapper for big requests.
 *
 * This is necessary because the ioctl number scheme on many Unixy OSes (esp. Solaris)
 * only allows a relatively small size to be encoded into the request. So, for big
 * request this generic form is used instead. */
typedef struct VBGLBIGREQ
{
    /** Magic value (VBGLBIGREQ_MAGIC). */
    uint32_t    u32Magic;
    /** The size of the data buffer. */
    uint32_t    cbData;
    /** The user address of the data buffer. */
    RTR3PTR     pvDataR3;
#if HC_ARCH_BITS == 32
    uint32_t    u32Padding;
#endif
/** @todo r=bird: We need a 'rc' field for passing VBox status codes. Reused
 *        some input field as rc on output. */
} VBGLBIGREQ;
/** Pointer to a request wrapper for solaris guests. */
typedef VBGLBIGREQ *PVBGLBIGREQ;
/** Pointer to a const request wrapper for solaris guests. */
typedef const VBGLBIGREQ *PCVBGLBIGREQ;

/** The VBGLBIGREQ::u32Magic value (Ryuu Murakami). */
#define VBGLBIGREQ_MAGIC                            0x19520219


#if defined(RT_OS_WINDOWS)
/** @todo Remove IOCTL_CODE later! Integrate it in VBOXGUEST_IOCTL_CODE below. */
/** @todo r=bird: IOCTL_CODE is supposedly defined in some header included by Windows.h or ntddk.h, which is why it wasn't in the #if 0 earlier. See HostDrivers/Support/SUPDrvIOC.h... */
# define IOCTL_CODE(DeviceType, Function, Method, Access, DataSize_ignored) \
  ( ((DeviceType) << 16) | ((Access) << 14) | ((Function) << 2) | (Method))
# define VBOXGUEST_IOCTL_CODE_(Function, Size)      IOCTL_CODE(FILE_DEVICE_UNKNOWN, 2048 + (Function), METHOD_BUFFERED, FILE_WRITE_ACCESS, 0)
# define VBOXGUEST_IOCTL_STRIP_SIZE(Code)           (Code)

#elif defined(RT_OS_OS2)
  /* No automatic buffering, size not encoded. */
# define VBOXGUEST_IOCTL_CATEGORY                   0xc2
# define VBOXGUEST_IOCTL_CODE_(Function, Size)      ((unsigned char)(Function))
# define VBOXGUEST_IOCTL_CATEGORY_FAST              0xc3 /**< Also defined in VBoxGuestA-os2.asm. */
# define VBOXGUEST_IOCTL_CODE_FAST_(Function)       ((unsigned char)(Function))
# define VBOXGUEST_IOCTL_STRIP_SIZE(Code)           (Code)

#elif defined(RT_OS_SOLARIS)
  /* No automatic buffering, size limited to 255 bytes => use VBGLBIGREQ for everything. */
# include <sys/ioccom.h>
# define VBOXGUEST_IOCTL_CODE_(Function, Size)      _IOWRN('V', (Function), sizeof(VBGLBIGREQ))
# define VBOXGUEST_IOCTL_CODE_FAST_(Function)       _IO(  'V', (Function))
# define VBOXGUEST_IOCTL_STRIP_SIZE(Code)           (Code)

#elif defined(RT_OS_LINUX)
  /* No automatic buffering, size limited to 16KB. */
# include <linux/ioctl.h>
# define VBOXGUEST_IOCTL_CODE_(Function, Size)      _IOC(_IOC_READ|_IOC_WRITE, 'V', (Function), (Size))
# define VBOXGUEST_IOCTL_CODE_FAST_(Function)       _IO(  'V', (Function))
# define VBOXGUEST_IOCTL_STRIP_SIZE(Code)           VBOXGUEST_IOCTL_CODE_(_IOC_NR((Code)), 0)

#elif defined(RT_OS_HAIKU)
  /* No automatic buffering, size not encoded. */
  /** @todo do something better */
# define VBOXGUEST_IOCTL_CODE_(Function, Size)      (0x56420000 | (Function))
# define VBOXGUEST_IOCTL_CODE_FAST_(Function)       (0x56420000 | (Function))
# define VBOXGUEST_IOCTL_STRIP_SIZE(Code)           (Code)

#elif defined(RT_OS_FREEBSD) || defined(RT_OS_NETBSD) /** @todo r=bird: Please do it like SUPDRVIOC to keep it as similar as possible. */
# include <sys/ioccom.h>

# define VBOXGUEST_IOCTL_CODE_(Function, Size)      _IOWR('V', (Function), VBGLBIGREQ)
# define VBOXGUEST_IOCTL_CODE_FAST_(Function)       _IO(  'V', (Function))
# define VBOXGUEST_IOCTL_STRIP_SIZE(Code)           IOCBASECMD(Code)

#else /* BSD Like */
  /* Automatic buffering, size limited to 4KB on *BSD and 8KB on Darwin - commands the limit, 4KB. */
# include <sys/ioccom.h>
# define VBOXGUEST_IOCTL_CODE_(Function, Size)      _IOC(IOC_INOUT, 'V', (Function), (Size))
# define VBOXGUEST_IOCTL_CODE_FAST_(Function)       _IO('V', (Function))
# define VBOXGUEST_IOCTL_STRIP_SIZE(uIOCtl)         ( (uIOCtl) & ~_IOC(0,0,0,IOCPARM_MASK) )
#endif

#define VBOXGUEST_IOCTL_CODE(Function, Size)        VBOXGUEST_IOCTL_CODE_((Function) | VBOXGUEST_IOCTL_FLAG, Size)
#define VBOXGUEST_IOCTL_CODE_FAST(Function)         VBOXGUEST_IOCTL_CODE_FAST_((Function) | VBOXGUEST_IOCTL_FLAG)

/* Define 32 bit codes to support 32 bit applications requests in the 64 bit guest driver. */
#ifdef RT_ARCH_AMD64
# define VBOXGUEST_IOCTL_CODE_32(Function, Size)    VBOXGUEST_IOCTL_CODE_(Function, Size)
# define VBOXGUEST_IOCTL_CODE_FAST_32(Function)     VBOXGUEST_IOCTL_CODE_FAST_(Function)
#endif /* RT_ARCH_AMD64 */



/** IOCTL to VBoxGuest to query the VMMDev IO port region start.
 * @remarks Ring-0 only. */
#define VBOXGUEST_IOCTL_GETVMMDEVPORT               VBOXGUEST_IOCTL_CODE(1, sizeof(VBoxGuestPortInfo))

#pragma pack(4)
typedef struct VBoxGuestPortInfo
{
    uint32_t portAddress;
    struct VMMDevMemory *pVMMDevMemory;
} VBoxGuestPortInfo;


/** IOCTL to VBoxGuest to wait for a VMMDev host notification */
#define VBOXGUEST_IOCTL_WAITEVENT                   VBOXGUEST_IOCTL_CODE_(2, sizeof(VBoxGuestWaitEventInfo))

/** @name Result codes for VBoxGuestWaitEventInfo::u32Result
 * @{
 */
/** Successful completion, an event occurred. */
#define VBOXGUEST_WAITEVENT_OK          (0)
/** Successful completion, timed out. */
#define VBOXGUEST_WAITEVENT_TIMEOUT     (1)
/** Wait was interrupted. */
#define VBOXGUEST_WAITEVENT_INTERRUPTED (2)
/** An error occurred while processing the request. */
#define VBOXGUEST_WAITEVENT_ERROR       (3)
/** @} */

/** Input and output buffers layout of the IOCTL_VBOXGUEST_WAITEVENT */
typedef struct VBoxGuestWaitEventInfo
{
    /** timeout in milliseconds */
    uint32_t u32TimeoutIn;
    /** events to wait for */
    uint32_t u32EventMaskIn;
    /** result code */
    uint32_t u32Result;
    /** events occurred */
    uint32_t u32EventFlagsOut;
} VBoxGuestWaitEventInfo;
AssertCompileSize(VBoxGuestWaitEventInfo, 16);


/** IOCTL to VBoxGuest to perform a VMM request
 * @remark  The data buffer for this IOCtl has an variable size, keep this in mind
 *          on systems where this matters. */
#define VBOXGUEST_IOCTL_VMMREQUEST(Size)            VBOXGUEST_IOCTL_CODE_(3, (Size))


/** IOCTL to VBoxGuest to control event filter mask. */
#define VBOXGUEST_IOCTL_CTL_FILTER_MASK             VBOXGUEST_IOCTL_CODE_(4, sizeof(VBoxGuestFilterMaskInfo))

/** Input and output buffer layout of the IOCTL_VBOXGUEST_CTL_FILTER_MASK. */
typedef struct VBoxGuestFilterMaskInfo
{
    uint32_t u32OrMask;
    uint32_t u32NotMask;
} VBoxGuestFilterMaskInfo;
AssertCompileSize(VBoxGuestFilterMaskInfo, 8);
#pragma pack()

/** IOCTL to VBoxGuest to interrupt (cancel) any pending WAITEVENTs and return.
 * Handled inside the guest additions and not seen by the host at all.
 * @see VBOXGUEST_IOCTL_WAITEVENT */
#define VBOXGUEST_IOCTL_CANCEL_ALL_WAITEVENTS       VBOXGUEST_IOCTL_CODE_(5, 0)

/** IOCTL to VBoxGuest to perform backdoor logging.
 * The argument is a string buffer of the specified size. */
#define VBOXGUEST_IOCTL_LOG(Size)                   VBOXGUEST_IOCTL_CODE_(6, (Size))

/** IOCTL to VBoxGuest to check memory ballooning.
 * The guest kernel module / device driver will ask the host for the current size of
 * the balloon and adjust the size. Or it will set fHandledInR0 = false and R3 is
 * responsible for allocating memory and calling R0 (VBOXGUEST_IOCTL_CHANGE_BALLOON). */
#define VBOXGUEST_IOCTL_CHECK_BALLOON               VBOXGUEST_IOCTL_CODE_(7, sizeof(VBoxGuestCheckBalloonInfo))

/** Output buffer layout of the VBOXGUEST_IOCTL_CHECK_BALLOON. */
typedef struct VBoxGuestCheckBalloonInfo
{
    /** The size of the balloon in chunks of 1MB. */
    uint32_t cBalloonChunks;
    /** false = handled in R0, no further action required.
     *   true = allocate balloon memory in R3. */
    uint32_t fHandleInR3;
} VBoxGuestCheckBalloonInfo;
AssertCompileSize(VBoxGuestCheckBalloonInfo, 8);


/** IOCTL to VBoxGuest to supply or revoke one chunk for ballooning.
 * The guest kernel module / device driver will lock down supplied memory or
 * unlock reclaimed memory and then forward the physical addresses of the
 * changed balloon chunk to the host. */
#define VBOXGUEST_IOCTL_CHANGE_BALLOON              VBOXGUEST_IOCTL_CODE_(8, sizeof(VBoxGuestChangeBalloonInfo))

/** Input buffer layout of the VBOXGUEST_IOCTL_CHANGE_BALLOON request.
 * Information about a memory chunk used to inflate or deflate the balloon. */
typedef struct VBoxGuestChangeBalloonInfo
{
    /** Address of the chunk. */
    uint64_t u64ChunkAddr;
    /** true = inflate, false = deflate. */
    uint32_t fInflate;
    /** Alignment padding. */
    uint32_t u32Align;
} VBoxGuestChangeBalloonInfo;
AssertCompileSize(VBoxGuestChangeBalloonInfo, 16);

/** IOCTL to VBoxGuest to write guest core. */
#define VBOXGUEST_IOCTL_WRITE_CORE_DUMP             VBOXGUEST_IOCTL_CODE(9, sizeof(VBoxGuestWriteCoreDump))

/** Input and output buffer layout of the VBOXGUEST_IOCTL_WRITE_CORE
 *  request. */
typedef struct VBoxGuestWriteCoreDump
{
    /** Flags (reserved, MBZ). */
    uint32_t fFlags;
} VBoxGuestWriteCoreDump;
AssertCompileSize(VBoxGuestWriteCoreDump, 4);

/** IOCTL to VBoxGuest to update the mouse status features. */
# define VBOXGUEST_IOCTL_SET_MOUSE_STATUS           VBOXGUEST_IOCTL_CODE_(10, sizeof(uint32_t))

#ifdef VBOX_WITH_HGCM
/** IOCTL to VBoxGuest to connect to a HGCM service. */
# define VBOXGUEST_IOCTL_HGCM_CONNECT               VBOXGUEST_IOCTL_CODE(16, sizeof(VBoxGuestHGCMConnectInfo))

/** IOCTL to VBoxGuest to disconnect from a HGCM service. */
# define VBOXGUEST_IOCTL_HGCM_DISCONNECT            VBOXGUEST_IOCTL_CODE(17, sizeof(VBoxGuestHGCMDisconnectInfo))

/** IOCTL to VBoxGuest to make a call to a HGCM service.
 * @see VBoxGuestHGCMCallInfo */
# define VBOXGUEST_IOCTL_HGCM_CALL(Size)            VBOXGUEST_IOCTL_CODE(18, (Size))

/** IOCTL to VBoxGuest to make a timed call to a HGCM service. */
# define VBOXGUEST_IOCTL_HGCM_CALL_TIMED(Size)      VBOXGUEST_IOCTL_CODE(20, (Size))

/** IOCTL to VBoxGuest passed from the Kernel Mode driver, but containing a user mode data in VBoxGuestHGCMCallInfo
 * the driver received from the UM. Called in the context of the process passing the data.
 * @see VBoxGuestHGCMCallInfo */
# define VBOXGUEST_IOCTL_HGCM_CALL_USERDATA(Size)   VBOXGUEST_IOCTL_CODE(21, (Size))

# ifdef RT_ARCH_AMD64
/** @name IOCTL numbers that 32-bit clients, like the Windows OpenGL guest
 *        driver, will use when taking to a 64-bit driver.
 * @remarks These are only used by the driver implementation!
 * @{*/
#  define VBOXGUEST_IOCTL_HGCM_CONNECT_32           VBOXGUEST_IOCTL_CODE_32(16, sizeof(VBoxGuestHGCMConnectInfo))
#  define VBOXGUEST_IOCTL_HGCM_DISCONNECT_32        VBOXGUEST_IOCTL_CODE_32(17, sizeof(VBoxGuestHGCMDisconnectInfo))
#  define VBOXGUEST_IOCTL_HGCM_CALL_32(Size)        VBOXGUEST_IOCTL_CODE_32(18, (Size))
#  define VBOXGUEST_IOCTL_HGCM_CALL_TIMED_32(Size)  VBOXGUEST_IOCTL_CODE_32(20, (Size))
/** @} */
# endif /* RT_ARCH_AMD64 */

/** Get the pointer to the first HGCM parameter.  */
# define VBOXGUEST_HGCM_CALL_PARMS(a)             ( (HGCMFunctionParameter   *)((uint8_t *)(a) + sizeof(VBoxGuestHGCMCallInfo)) )
/** Get the pointer to the first HGCM parameter in a 32-bit request.  */
# define VBOXGUEST_HGCM_CALL_PARMS32(a)           ( (HGCMFunctionParameter32 *)((uint8_t *)(a) + sizeof(VBoxGuestHGCMCallInfo)) )

#endif /* VBOX_WITH_HGCM */

#ifdef VBOX_WITH_DPC_LATENCY_CHECKER
/** IOCTL to VBoxGuest to perform DPC latency tests, printing the result in
 * the release log on the host.  Takes no data, returns no data. */
# define VBOXGUEST_IOCTL_DPC_LATENCY_CHECKER        VBOXGUEST_IOCTL_CODE_(30, 0)
#endif

/** IOCTL to for setting the mouse driver callback. (kernel only) */
/** @note The callback will be called in interrupt context with the VBoxGuest
 * device event spinlock held. */
#define VBOXGUEST_IOCTL_SET_MOUSE_NOTIFY_CALLBACK   VBOXGUEST_IOCTL_CODE(31, sizeof(VBoxGuestMouseSetNotifyCallback))

typedef DECLCALLBACK(void) FNVBOXGUESTMOUSENOTIFY(void *pfnUser);
typedef FNVBOXGUESTMOUSENOTIFY *PFNVBOXGUESTMOUSENOTIFY;

/** Input buffer for VBOXGUEST_IOCTL_INTERNAL_SET_MOUSE_NOTIFY_CALLBACK. */
typedef struct VBoxGuestMouseSetNotifyCallback
{
    /**
     * Mouse notification callback.
     *
     * @param   pvUser      The callback argument.
     */
    PFNVBOXGUESTMOUSENOTIFY      pfnNotify;
    /** The callback argument*/
    void                       *pvUser;
} VBoxGuestMouseSetNotifyCallback;


typedef enum VBOXGUESTCAPSACQUIRE_FLAGS
{
    VBOXGUESTCAPSACQUIRE_FLAGS_NONE = 0,
    /* configures VBoxGuest to use the specified caps in Acquire mode, w/o making any caps acquisition/release.
     * so far it is only possible to set acquire mode for caps, but not clear it,
     * so u32NotMask is ignored for this request */
    VBOXGUESTCAPSACQUIRE_FLAGS_CONFIG_ACQUIRE_MODE,
    /* to ensure enum is 32bit*/
    VBOXGUESTCAPSACQUIRE_FLAGS_32bit = 0x7fffffff
} VBOXGUESTCAPSACQUIRE_FLAGS;

typedef struct VBoxGuestCapsAquire
{
    /* result status
     * VINF_SUCCESS - on success
     * VERR_RESOURCE_BUSY    - some caps in the u32OrMask are acquired by some other VBoxGuest connection.
     *                         NOTE: no u32NotMask caps are cleaned in this case, i.e. no modifications are done on failure
     * VER_INVALID_PARAMETER - invalid Caps are specified with either u32OrMask or u32NotMask. No modifications are done on failure.
     */
    int32_t rc;
    /* Acquire command */
    VBOXGUESTCAPSACQUIRE_FLAGS enmFlags;
    /* caps to acquire, OR-ed VMMDEV_GUEST_SUPPORTS_XXX flags */
    uint32_t u32OrMask;
    /* caps to release, OR-ed VMMDEV_GUEST_SUPPORTS_XXX flags */
    uint32_t u32NotMask;
} VBoxGuestCapsAquire;

/** IOCTL to for Acquiring/Releasing Guest Caps
 * This is used for multiple purposes:
 * 1. By doing Acquire r3 client application (e.g. VBoxTray) claims it will use
 *    the given connection for performing operations like Seamles or Auto-resize,
 *    thus, if the application terminates, the driver will automatically cleanup the caps reported to host,
 *    so that host knows guest does not support them anymore
 * 2. In a multy-user environment this will not allow r3 applications (like VBoxTray)
 *    running in different user sessions simultaneously to interfere with each other.
 *    An r3 client application (like VBoxTray) is responsible for Acquiring/Releasing caps properly as needed.
 **/
#define VBOXGUEST_IOCTL_GUEST_CAPS_ACQUIRE          VBOXGUEST_IOCTL_CODE(32, sizeof(VBoxGuestCapsAquire))

/** IOCTL to VBoxGuest to set guest capabilities. */
#define VBOXGUEST_IOCTL_SET_GUEST_CAPABILITIES      VBOXGUEST_IOCTL_CODE_(33, sizeof(VBoxGuestSetCapabilitiesInfo))

/** Input and output buffer layout of the VBOXGUEST_IOCTL_SET_GUEST_CAPABILITIES
 *  IOCtl. */
typedef struct VBoxGuestSetCapabilitiesInfo
{
    uint32_t u32OrMask;
    uint32_t u32NotMask;
} VBoxGuestSetCapabilitiesInfo;
AssertCompileSize(VBoxGuestSetCapabilitiesInfo, 8);


#ifdef RT_OS_OS2

/**
 * The data buffer layout for the IDC entry point (AttachDD).
 *
 * @remark  This is defined in multiple 16-bit headers / sources.
 *          Some places it's called VBGOS2IDC to short things a bit.
 */
typedef struct VBOXGUESTOS2IDCCONNECT
{
    /** VMMDEV_VERSION. */
    uint32_t u32Version;
    /** Opaque session handle. */
    uint32_t u32Session;

    /**
     * The 32-bit service entry point.
     *
     * @returns VBox status code.
     * @param   u32Session          The above session handle.
     * @param   iFunction           The requested function.
     * @param   pvData              The input/output data buffer. The caller ensures that this
     *                              cannot be swapped out, or that it's acceptable to take a
     *                              page in fault in the current context. If the request doesn't
     *                              take input or produces output, apssing NULL is okay.
     * @param   cbData              The size of the data buffer.
     * @param   pcbDataReturned     Where to store the amount of data that's returned.
     *                              This can be NULL if pvData is NULL.
     */
    DECLCALLBACKMEMBER(int, pfnServiceEP)(uint32_t u32Session, unsigned iFunction, void *pvData, size_t cbData, size_t *pcbDataReturned);

    /** The 16-bit service entry point for C code (cdecl).
     *
     * It's the same as the 32-bit entry point, but the types has
     * changed to 16-bit equivalents.
     *
     * @code
     * int far cdecl
     * VBoxGuestOs2IDCService16(uint32_t u32Session, uint16_t iFunction,
     *                          void far *fpvData, uint16_t cbData, uint16_t far *pcbDataReturned);
     * @endcode
     */
    RTFAR16 fpfnServiceEP;

    /** The 16-bit service entry point for Assembly code (register).
     *
     * This is just a wrapper around fpfnServiceEP to simplify calls
     * from 16-bit assembly code.
     *
     * @returns (e)ax: VBox status code; cx: The amount of data returned.
     *
     * @param   u32Session          eax   - The above session handle.
     * @param   iFunction           dl    - The requested function.
     * @param   pvData              es:bx - The input/output data buffer.
     * @param   cbData              cx    - The size of the data buffer.
     */
    RTFAR16 fpfnServiceAsmEP;
} VBOXGUESTOS2IDCCONNECT;
/** Pointer to VBOXGUESTOS2IDCCONNECT buffer. */
typedef VBOXGUESTOS2IDCCONNECT *PVBOXGUESTOS2IDCCONNECT;

/** OS/2 specific: IDC client disconnect request.
 *
 * This takes no input and it doesn't return anything. Obviously this
 * is only recognized if it arrives thru the IDC service EP.
 */
# define VBOXGUEST_IOCTL_OS2_IDC_DISCONNECT     VBOXGUEST_IOCTL_CODE(48, sizeof(uint32_t))

#endif /* RT_OS_OS2 */

#if defined(RT_OS_LINUX) || defined(RT_OS_SOLARIS) || defined(RT_OS_FREEBSD)

/* Private IOCtls between user space and the kernel video driver.  DRM private
 * IOCtls always have the type 'd' and a number between 0x40 and 0x99 (0x9F?) */

# define VBOX_DRM_IOCTL(a) (0x40 + DRM_VBOX_ ## a)

/** Stop using HGSMI in the kernel driver until it is re-enabled, so that a
 *  user-space driver can use it.  It must be re-enabled before the kernel
 *  driver can be used again in a sensible way. */
/** @note These IOCtls was removed from the code, but are left here as
 * templates as we may need similar ones in future. */
# define DRM_VBOX_DISABLE_HGSMI    0
# define DRM_IOCTL_VBOX_DISABLE_HGSMI    VBOX_DRM_IOCTL(DISABLE_HGSMI)
# define VBOXVIDEO_IOCTL_DISABLE_HGSMI   _IO('d', DRM_IOCTL_VBOX_DISABLE_HGSMI)
/** Enable HGSMI in the kernel driver after it was previously disabled. */
# define DRM_VBOX_ENABLE_HGSMI     1
# define DRM_IOCTL_VBOX_ENABLE_HGSMI     VBOX_DRM_IOCTL(ENABLE_HGSMI)
# define VBOXVIDEO_IOCTL_ENABLE_HGSMI    _IO('d', DRM_IOCTL_VBOX_ENABLE_HGSMI)

#endif /* RT_OS_LINUX || RT_OS_SOLARIS || RT_OS_FREEBSD */

#endif /* !defined(IN_RC) && !defined(IN_RING0_AGNOSTIC) && !defined(IPRT_NO_CRT) */

/** @} */

/** @} */
#endif

