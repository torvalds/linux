/** @file
 * VBox Host Guest Shared Memory Interface (HGSMI).
 * OS-independent guest structures.
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


#ifndef ___VBox_VBoxVideoGuest_h___
#define ___VBox_VBoxVideoGuest_h___

#include <VBox/HGSMI/HGSMI.h>
#include <VBox/HGSMI/HGSMIChSetup.h>
#include <VBox/VBoxVideo.h>

#ifdef VBOX_XPDM_MINIPORT
# include <iprt/nt/miniport.h>
# include <ntddvdeo.h> /* sdk, clean */
# include <iprt/nt/Video.h>
#elif defined VBOX_GUESTR3XORGMOD
# include <compiler.h>
#else
# include <iprt/asm-amd64-x86.h>
#endif

#ifdef VBOX_WDDM_MINIPORT
# include "wddm/VBoxMPShgsmi.h"
 typedef VBOXSHGSMI HGSMIGUESTCMDHEAP;
# define HGSMIGUESTCMDHEAP_GET(_p) (&(_p)->Heap)
#else
 typedef HGSMIHEAP HGSMIGUESTCMDHEAP;
# define HGSMIGUESTCMDHEAP_GET(_p) (_p)
#endif

RT_C_DECLS_BEGIN

/**
 * Structure grouping the context needed for submitting commands to the host
 * via HGSMI
 */
typedef struct HGSMIGUESTCOMMANDCONTEXT
{
    /** Information about the memory heap located in VRAM from which data
     * structures to be sent to the host are allocated. */
    HGSMIGUESTCMDHEAP heapCtx;
    /** The I/O port used for submitting commands to the host by writing their
     * offsets into the heap. */
    RTIOPORT port;
} HGSMIGUESTCOMMANDCONTEXT, *PHGSMIGUESTCOMMANDCONTEXT;


/**
 * Structure grouping the context needed for receiving commands from the host
 * via HGSMI
 */
typedef struct HGSMIHOSTCOMMANDCONTEXT
{
    /** Information about the memory area located in VRAM in which the host
     * places data structures to be read by the guest. */
    HGSMIAREA areaCtx;
    /** Convenience structure used for matching host commands to handlers. */
    /** @todo handlers are registered individually in code rather than just
     * passing a static structure in order to gain extra flexibility.  There is
     * currently no expected usage case for this though.  Is the additional
     * complexity really justified? */
    HGSMICHANNELINFO channels;
    /** Flag to indicate that one thread is currently processing the command
     * queue. */
    volatile bool fHostCmdProcessing;
    /* Pointer to the VRAM location where the HGSMI host flags are kept. */
    volatile HGSMIHOSTFLAGS *pfHostFlags;
    /** The I/O port used for receiving commands from the host as offsets into
     * the memory area and sending back confirmations (command completion,
     * IRQ acknowlegement). */
    RTIOPORT port;
} HGSMIHOSTCOMMANDCONTEXT, *PHGSMIHOSTCOMMANDCONTEXT;


/**
 * Structure grouping the context needed for sending graphics acceleration
 * information to the host via VBVA.  Each screen has its own VBVA buffer.
 */
typedef struct VBVABUFFERCONTEXT
{
    /** Offset of the buffer in the VRAM section for the screen */
    uint32_t    offVRAMBuffer;
    /** Length of the buffer in bytes */
    uint32_t    cbBuffer;
    /** This flag is set if we wrote to the buffer faster than the host could
     * read it. */
    bool        fHwBufferOverflow;
    /** The VBVA record that we are currently preparing for the host, NULL if
     * none. */
    struct VBVARECORD *pRecord;
    /** Pointer to the VBVA buffer mapped into the current address space.  Will
     * be NULL if VBVA is not enabled. */
    struct VBVABUFFER *pVBVA;
} VBVABUFFERCONTEXT, *PVBVABUFFERCONTEXT;

/** @name Helper functions
 * @{ */
/** Write an 8-bit value to an I/O port. */
DECLINLINE(void) VBoxVideoCmnPortWriteUchar(RTIOPORT Port, uint8_t Value)
{
#ifdef VBOX_XPDM_MINIPORT
    VideoPortWritePortUchar((PUCHAR)Port, Value);
#elif defined VBOX_GUESTR3XORGMOD
    outb(Port, Value);
#else  /** @todo make these explicit */
    ASMOutU8(Port, Value);
#endif
}

/** Write a 16-bit value to an I/O port. */
DECLINLINE(void) VBoxVideoCmnPortWriteUshort(RTIOPORT Port, uint16_t Value)
{
#ifdef VBOX_XPDM_MINIPORT
    VideoPortWritePortUshort((PUSHORT)Port,Value);
#elif defined VBOX_GUESTR3XORGMOD
    outw(Port, Value);
#else
    ASMOutU16(Port, Value);
#endif
}

/** Write a 32-bit value to an I/O port. */
DECLINLINE(void) VBoxVideoCmnPortWriteUlong(RTIOPORT Port, uint32_t Value)
{
#ifdef VBOX_XPDM_MINIPORT
    VideoPortWritePortUlong((PULONG)Port,Value);
#elif defined VBOX_GUESTR3XORGMOD
    outl(Port, Value);
#else
    ASMOutU32(Port, Value);
#endif
}

/** Read an 8-bit value from an I/O port. */
DECLINLINE(uint8_t) VBoxVideoCmnPortReadUchar(RTIOPORT Port)
{
#ifdef VBOX_XPDM_MINIPORT
    return VideoPortReadPortUchar((PUCHAR)Port);
#elif defined VBOX_GUESTR3XORGMOD
    return inb(Port);
#else
    return ASMInU8(Port);
#endif
}

/** Read a 16-bit value from an I/O port. */
DECLINLINE(uint16_t) VBoxVideoCmnPortReadUshort(RTIOPORT Port)
{
#ifdef VBOX_XPDM_MINIPORT
    return VideoPortReadPortUshort((PUSHORT)Port);
#elif defined VBOX_GUESTR3XORGMOD
    return inw(Port);
#else
    return ASMInU16(Port);
#endif
}

/** Read a 32-bit value from an I/O port. */
DECLINLINE(uint32_t) VBoxVideoCmnPortReadUlong(RTIOPORT Port)
{
#ifdef VBOX_XPDM_MINIPORT
    return VideoPortReadPortUlong((PULONG)Port);
#elif defined VBOX_GUESTR3XORGMOD
    return inl(Port);
#else
    return ASMInU32(Port);
#endif
}

/** @}  */

/** @name Base HGSMI APIs
 * @{ */

/** Acknowlege an IRQ. */
DECLINLINE(void) VBoxHGSMIClearIrq(PHGSMIHOSTCOMMANDCONTEXT pCtx)
{
    VBoxVideoCmnPortWriteUlong(pCtx->port, HGSMIOFFSET_VOID);
}

DECLHIDDEN(void)     VBoxHGSMIHostCmdComplete(PHGSMIHOSTCOMMANDCONTEXT pCtx,
                                              void *pvMem);
DECLHIDDEN(void)     VBoxHGSMIProcessHostQueue(PHGSMIHOSTCOMMANDCONTEXT pCtx);
DECLHIDDEN(bool)     VBoxHGSMIIsSupported(void);
DECLHIDDEN(void *)   VBoxHGSMIBufferAlloc(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                          HGSMISIZE cbData,
                                          uint8_t u8Ch,
                                          uint16_t u16Op);
DECLHIDDEN(void)     VBoxHGSMIBufferFree(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                         void *pvBuffer);
DECLHIDDEN(int)      VBoxHGSMIBufferSubmit(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                           void *pvBuffer);
DECLHIDDEN(void)     VBoxHGSMIGetBaseMappingInfo(uint32_t cbVRAM,
                                                 uint32_t *poffVRAMBaseMapping,
                                                 uint32_t *pcbMapping,
                                                 uint32_t *poffGuestHeapMemory,
                                                 uint32_t *pcbGuestHeapMemory,
                                                 uint32_t *poffHostFlags);
DECLHIDDEN(int)      VBoxHGSMIReportFlagsLocation(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                                  HGSMIOFFSET offLocation);
DECLHIDDEN(int)      VBoxHGSMISendCapsInfo(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                           uint32_t fCaps);
/** @todo we should provide a cleanup function too as part of the API */
DECLHIDDEN(int)      VBoxHGSMISetupGuestContext(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                                void *pvGuestHeapMemory,
                                                uint32_t cbGuestHeapMemory,
                                                uint32_t offVRAMGuestHeapMemory,
                                                const HGSMIENV *pEnv);
DECLHIDDEN(void)     VBoxHGSMIGetHostAreaMapping(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                                 uint32_t cbVRAM,
                                                 uint32_t offVRAMBaseMapping,
                                                 uint32_t *poffVRAMHostArea,
                                                 uint32_t *pcbHostArea);
DECLHIDDEN(void)     VBoxHGSMISetupHostContext(PHGSMIHOSTCOMMANDCONTEXT pCtx,
                                               void *pvBaseMapping,
                                               uint32_t offHostFlags,
                                               void *pvHostAreaMapping,
                                               uint32_t offVRAMHostArea,
                                               uint32_t cbHostArea);
DECLHIDDEN(int)      VBoxHGSMISendHostCtxInfo(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                              HGSMIOFFSET offVRAMFlagsLocation,
                                              uint32_t fCaps,
                                              uint32_t offVRAMHostArea,
                                              uint32_t cbHostArea);
DECLHIDDEN(int)      VBoxQueryConfHGSMI(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                    uint32_t u32Index, uint32_t *pulValue);
DECLHIDDEN(int)      VBoxQueryConfHGSMIDef(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                           uint32_t u32Index, uint32_t u32DefValue, uint32_t *pulValue);
DECLHIDDEN(int)      VBoxHGSMIUpdatePointerShape(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                                 uint32_t fFlags,
                                                 uint32_t cHotX,
                                                 uint32_t cHotY,
                                                 uint32_t cWidth,
                                                 uint32_t cHeight,
                                                 uint8_t *pPixels,
                                                 uint32_t cbLength);
DECLHIDDEN(int)      VBoxHGSMICursorPosition(PHGSMIGUESTCOMMANDCONTEXT pCtx, bool fReportPosition, uint32_t x, uint32_t y,
                                             uint32_t *pxHost, uint32_t *pyHost);

/** @}  */

/** @name VBVA APIs
 * @{ */
DECLHIDDEN(bool) VBoxVBVAEnable(PVBVABUFFERCONTEXT pCtx,
                                PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx,
                                struct VBVABUFFER *pVBVA, int32_t cScreen);
DECLHIDDEN(void) VBoxVBVADisable(PVBVABUFFERCONTEXT pCtx,
                                 PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx,
                                 int32_t cScreen);
DECLHIDDEN(bool) VBoxVBVABufferBeginUpdate(PVBVABUFFERCONTEXT pCtx,
                                           PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx);
DECLHIDDEN(void) VBoxVBVABufferEndUpdate(PVBVABUFFERCONTEXT pCtx);
DECLHIDDEN(bool) VBoxVBVAWrite(PVBVABUFFERCONTEXT pCtx,
                               PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx,
                               const void *pv, uint32_t cb);
DECLHIDDEN(bool) VBoxVBVAOrderSupported(PVBVABUFFERCONTEXT pCtx, unsigned code);
DECLHIDDEN(void) VBoxVBVASetupBufferContext(PVBVABUFFERCONTEXT pCtx,
                                            uint32_t offVRAMBuffer,
                                            uint32_t cbBuffer);

/** @}  */

/** @name Modesetting APIs
 * @{ */

DECLHIDDEN(uint32_t) VBoxHGSMIGetMonitorCount(PHGSMIGUESTCOMMANDCONTEXT pCtx);
DECLHIDDEN(uint32_t) VBoxVideoGetVRAMSize(void);
DECLHIDDEN(bool)     VBoxVideoAnyWidthAllowed(void);
DECLHIDDEN(uint16_t) VBoxHGSMIGetScreenFlags(PHGSMIGUESTCOMMANDCONTEXT pCtx);

struct VBVAINFOVIEW;
/**
 * Callback funtion called from @a VBoxHGSMISendViewInfo to initialise
 * the @a VBVAINFOVIEW structure for each screen.
 *
 * @returns  iprt status code
 * @param  pvData  context data for the callback, passed to @a
 *                 VBoxHGSMISendViewInfo along with the callback
 * @param  pInfo   array of @a VBVAINFOVIEW structures to be filled in
 * @todo  explicitly pass the array size
 */
typedef DECLCALLBACK(int) FNHGSMIFILLVIEWINFO(void *pvData,
                                              struct VBVAINFOVIEW *pInfo,
                                              uint32_t cViews);
/** Pointer to a FNHGSMIFILLVIEWINFO callback */
typedef FNHGSMIFILLVIEWINFO *PFNHGSMIFILLVIEWINFO;

DECLHIDDEN(int)      VBoxHGSMISendViewInfo(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                           uint32_t u32Count,
                                           PFNHGSMIFILLVIEWINFO pfnFill,
                                           void *pvData);
DECLHIDDEN(void)     VBoxVideoSetModeRegisters(uint16_t cWidth, uint16_t cHeight,
                                               uint16_t cVirtWidth, uint16_t cBPP,
                                               uint16_t fFlags,
                                               uint16_t cx, uint16_t cy);
DECLHIDDEN(bool)     VBoxVideoGetModeRegisters(uint16_t *pcWidth,
                                               uint16_t *pcHeight,
                                               uint16_t *pcVirtWidth,
                                               uint16_t *pcBPP,
                                               uint16_t *pfFlags);
DECLHIDDEN(void)     VBoxVideoDisableVBE(void);
DECLHIDDEN(void)     VBoxHGSMIProcessDisplayInfo(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                                 uint32_t cDisplay,
                                                 int32_t  cOriginX,
                                                 int32_t  cOriginY,
                                                 uint32_t offStart,
                                                 uint32_t cbPitch,
                                                 uint32_t cWidth,
                                                 uint32_t cHeight,
                                                 uint16_t cBPP,
                                                 uint16_t fFlags);
DECLHIDDEN(int)      VBoxHGSMIUpdateInputMapping(PHGSMIGUESTCOMMANDCONTEXT pCtx, int32_t  cOriginX, int32_t  cOriginY,
                                                 uint32_t cWidth, uint32_t cHeight);
DECLHIDDEN(int) VBoxHGSMIGetModeHints(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                      unsigned cScreens, VBVAMODEHINT *paHints);

/** @}  */

RT_C_DECLS_END

#endif

