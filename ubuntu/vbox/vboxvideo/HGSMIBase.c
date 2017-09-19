/* $Id: HGSMIBase.cpp $ */
/** @file
 * VirtualBox Video driver, common code - HGSMI initialisation and helper
 * functions.
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
 */

#include <VBox/VBoxVideoGuest.h>
#include <VBox/VBoxVideo.h>
#include <VBox/VBoxGuest.h>
#include <VBox/Hardware/VBoxVideoVBE.h>
#include <VBox/VMMDev.h>

#include <iprt/asm.h>
#include <iprt/log.h>
#include <iprt/string.h>

/** Send completion notification to the host for the command located at offset
 * @a offt into the host command buffer. */
static void HGSMINotifyHostCmdComplete(PHGSMIHOSTCOMMANDCONTEXT pCtx, HGSMIOFFSET offt)
{
    VBoxVideoCmnPortWriteUlong(pCtx->port, offt);
}


/**
 * Inform the host that a command has been handled.
 *
 * @param  pCtx   the context containing the heap to be used
 * @param  pvMem  pointer into the heap as mapped in @a pCtx to the command to
 *                be completed
 */
DECLHIDDEN(void) VBoxHGSMIHostCmdComplete(PHGSMIHOSTCOMMANDCONTEXT pCtx,
                                          void *pvMem)
{
    HGSMIBUFFERHEADER *pHdr = HGSMIBufferHeaderFromData(pvMem);
    HGSMIOFFSET offMem = HGSMIPointerToOffset(&pCtx->areaCtx, pHdr);
    Assert(offMem != HGSMIOFFSET_VOID);
    if(offMem != HGSMIOFFSET_VOID)
    {
        HGSMINotifyHostCmdComplete(pCtx, offMem);
    }
}


/** Submit an incoming host command to the appropriate handler. */
static void hgsmiHostCmdProcess(PHGSMIHOSTCOMMANDCONTEXT pCtx,
                                HGSMIOFFSET offBuffer)
{
    int rc = HGSMIBufferProcess(&pCtx->areaCtx, &pCtx->channels, offBuffer);
    Assert(!RT_FAILURE(rc));
    if(RT_FAILURE(rc))
    {
        /* failure means the command was not submitted to the handler for some reason
         * it's our responsibility to notify its completion in this case */
        HGSMINotifyHostCmdComplete(pCtx, offBuffer);
    }
    /* if the cmd succeeded it's responsibility of the callback to complete it */
}

/** Get the next command from the host. */
static HGSMIOFFSET hgsmiGetHostBuffer(PHGSMIHOSTCOMMANDCONTEXT pCtx)
{
    return VBoxVideoCmnPortReadUlong(pCtx->port);
}


/** Get and handle the next command from the host. */
static void hgsmiHostCommandQueryProcess(PHGSMIHOSTCOMMANDCONTEXT pCtx)
{
    HGSMIOFFSET offset = hgsmiGetHostBuffer(pCtx);
    AssertReturnVoid(offset != HGSMIOFFSET_VOID);
    hgsmiHostCmdProcess(pCtx, offset);
}


/** Drain the host command queue. */
DECLHIDDEN(void) VBoxHGSMIProcessHostQueue(PHGSMIHOSTCOMMANDCONTEXT pCtx)
{
    while (pCtx->pfHostFlags->u32HostFlags & HGSMIHOSTFLAGS_COMMANDS_PENDING)
    {
        if (!ASMAtomicCmpXchgBool(&pCtx->fHostCmdProcessing, true, false))
            return;
        hgsmiHostCommandQueryProcess(pCtx);
        ASMAtomicWriteBool(&pCtx->fHostCmdProcessing, false);
    }
}


/** Detect whether HGSMI is supported by the host. */
DECLHIDDEN(bool) VBoxHGSMIIsSupported(void)
{
    uint16_t DispiId;

    VBoxVideoCmnPortWriteUshort(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_ID);
    VBoxVideoCmnPortWriteUshort(VBE_DISPI_IOPORT_DATA, VBE_DISPI_ID_HGSMI);

    DispiId = VBoxVideoCmnPortReadUshort(VBE_DISPI_IOPORT_DATA);

    return (DispiId == VBE_DISPI_ID_HGSMI);
}


/**
 * Allocate and initialise a command descriptor in the guest heap for a
 * guest-to-host command.
 *
 * @returns  pointer to the descriptor's command data buffer
 * @param  pCtx     the context containing the heap to be used
 * @param  cbData   the size of the command data to go into the descriptor
 * @param  u8Ch     the HGSMI channel to be used, set to the descriptor
 * @param  u16Op    the HGSMI command to be sent, set to the descriptor
 */
DECLHIDDEN(void *) VBoxHGSMIBufferAlloc(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                        HGSMISIZE cbData,
                                        uint8_t u8Ch,
                                        uint16_t u16Op)
{
#ifdef VBOX_WDDM_MINIPORT
    return VBoxSHGSMIHeapAlloc (&pCtx->heapCtx, cbData, u8Ch, u16Op);
#else
    return HGSMIHeapAlloc (&pCtx->heapCtx, cbData, u8Ch, u16Op);
#endif
}


/**
 * Free a descriptor allocated by @a VBoxHGSMIBufferAlloc.
 *
 * @param  pCtx      the context containing the heap used
 * @param  pvBuffer  the pointer returned by @a VBoxHGSMIBufferAlloc
 */
DECLHIDDEN(void) VBoxHGSMIBufferFree(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                     void *pvBuffer)
{
#ifdef VBOX_WDDM_MINIPORT
    VBoxSHGSMIHeapFree (&pCtx->heapCtx, pvBuffer);
#else
    HGSMIHeapFree (&pCtx->heapCtx, pvBuffer);
#endif
}


/**
 * Submit a command descriptor allocated by @a VBoxHGSMIBufferAlloc.
 *
 * @param  pCtx      the context containing the heap used
 * @param  pvBuffer  the pointer returned by @a VBoxHGSMIBufferAlloc
 */
DECLHIDDEN(int) VBoxHGSMIBufferSubmit(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                      void *pvBuffer)
{
    /* Initialize the buffer and get the offset for port IO. */
    HGSMIOFFSET offBuffer = HGSMIHeapBufferOffset (HGSMIGUESTCMDHEAP_GET(&pCtx->heapCtx), pvBuffer);

    Assert(offBuffer != HGSMIOFFSET_VOID);
    if (offBuffer != HGSMIOFFSET_VOID)
    {
        /* Submit the buffer to the host. */
        VBoxVideoCmnPortWriteUlong(pCtx->port, offBuffer);
        /* Make the compiler aware that the host has changed memory. */
        ASMCompilerBarrier();
        return VINF_SUCCESS;
    }

    return VERR_INVALID_PARAMETER;
}


/** Inform the host of the location of the host flags in VRAM via an HGSMI
 * command. */
static int vboxHGSMIReportFlagsLocation(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                        HGSMIOFFSET offLocation)
{
    HGSMIBUFFERLOCATION *p;
    int rc = VINF_SUCCESS;

    /* Allocate the IO buffer. */
    p = (HGSMIBUFFERLOCATION *)VBoxHGSMIBufferAlloc(pCtx,
                                              sizeof(HGSMIBUFFERLOCATION),
                                              HGSMI_CH_HGSMI,
                                              HGSMI_CC_HOST_FLAGS_LOCATION);
    if (p)
    {
        /* Prepare data to be sent to the host. */
        p->offLocation = offLocation;
        p->cbLocation  = sizeof(HGSMIHOSTFLAGS);
        rc = VBoxHGSMIBufferSubmit(pCtx, p);
        /* Free the IO buffer. */
        VBoxHGSMIBufferFree(pCtx, p);
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}


/**
 * Inform the host of the location of the host flags in VRAM via an HGSMI
 * command.
 * @returns  IPRT status value.
 * @returns  VERR_NOT_IMPLEMENTED  if the host does not support the command.
 * @returns  VERR_NO_MEMORY        if a heap allocation fails.
 * @param    pCtx                  the context of the guest heap to use.
 * @param    offLocation           the offset chosen for the flags withing guest
 *                                 VRAM.
 */
DECLHIDDEN(int) VBoxHGSMIReportFlagsLocation(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                             HGSMIOFFSET offLocation)
{
    return vboxHGSMIReportFlagsLocation(pCtx, offLocation);
}


/** Notify the host of HGSMI-related guest capabilities via an HGSMI command.
 */
static int vboxHGSMISendCapsInfo(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                 uint32_t fCaps)
{
    VBVACAPS *pCaps;
    int rc = VINF_SUCCESS;

    /* Allocate the IO buffer. */
    pCaps = (VBVACAPS *)VBoxHGSMIBufferAlloc(pCtx,
                                       sizeof(VBVACAPS), HGSMI_CH_VBVA,
                                       VBVA_INFO_CAPS);

    if (pCaps)
    {
        /* Prepare data to be sent to the host. */
        pCaps->rc    = VERR_NOT_IMPLEMENTED;
        pCaps->fCaps = fCaps;
        rc = VBoxHGSMIBufferSubmit(pCtx, pCaps);
        if (RT_SUCCESS(rc))
        {
            AssertRC(pCaps->rc);
            rc = pCaps->rc;
        }
        /* Free the IO buffer. */
        VBoxHGSMIBufferFree(pCtx, pCaps);
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}


/**
 * Notify the host of HGSMI-related guest capabilities via an HGSMI command.
 * @returns  IPRT status value.
 * @returns  VERR_NOT_IMPLEMENTED  if the host does not support the command.
 * @returns  VERR_NO_MEMORY        if a heap allocation fails.
 * @param    pCtx                  the context of the guest heap to use.
 * @param    fCaps                 the capabilities to report, see VBVACAPS.
 */
DECLHIDDEN(int) VBoxHGSMISendCapsInfo(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                      uint32_t fCaps)
{
    return vboxHGSMISendCapsInfo(pCtx, fCaps);
}


/** Tell the host about the location of the area of VRAM set aside for the host
 * heap. */
static int vboxHGSMIReportHostArea(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                   uint32_t u32AreaOffset, uint32_t u32AreaSize)
{
    VBVAINFOHEAP *p;
    int rc = VINF_SUCCESS;

    /* Allocate the IO buffer. */
    p = (VBVAINFOHEAP *)VBoxHGSMIBufferAlloc(pCtx,
                                       sizeof (VBVAINFOHEAP), HGSMI_CH_VBVA,
                                       VBVA_INFO_HEAP);
    if (p)
    {
        /* Prepare data to be sent to the host. */
        p->u32HeapOffset = u32AreaOffset;
        p->u32HeapSize   = u32AreaSize;
        rc = VBoxHGSMIBufferSubmit(pCtx, p);
        /* Free the IO buffer. */
        VBoxHGSMIBufferFree(pCtx, p);
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}


/**
 * Get the information needed to map the basic communication structures in
 * device memory into our address space.  All pointer parameters are optional.
 *
 * @param  cbVRAM               how much video RAM is allocated to the device
 * @param  poffVRAMBaseMapping  where to save the offset from the start of the
 *                              device VRAM of the whole area to map
 * @param  pcbMapping           where to save the mapping size
 * @param  poffGuestHeapMemory  where to save the offset into the mapped area
 *                              of the guest heap backing memory
 * @param  pcbGuestHeapMemory   where to save the size of the guest heap
 *                              backing memory
 * @param  poffHostFlags        where to save the offset into the mapped area
 *                              of the host flags
 */
DECLHIDDEN(void) VBoxHGSMIGetBaseMappingInfo(uint32_t cbVRAM,
                                             uint32_t *poffVRAMBaseMapping,
                                             uint32_t *pcbMapping,
                                             uint32_t *poffGuestHeapMemory,
                                             uint32_t *pcbGuestHeapMemory,
                                             uint32_t *poffHostFlags)
{
    AssertPtrNullReturnVoid(poffVRAMBaseMapping);
    AssertPtrNullReturnVoid(pcbMapping);
    AssertPtrNullReturnVoid(poffGuestHeapMemory);
    AssertPtrNullReturnVoid(pcbGuestHeapMemory);
    AssertPtrNullReturnVoid(poffHostFlags);
    if (poffVRAMBaseMapping)
        *poffVRAMBaseMapping = cbVRAM - VBVA_ADAPTER_INFORMATION_SIZE;
    if (pcbMapping)
        *pcbMapping = VBVA_ADAPTER_INFORMATION_SIZE;
    if (poffGuestHeapMemory)
        *poffGuestHeapMemory = 0;
    if (pcbGuestHeapMemory)
        *pcbGuestHeapMemory =   VBVA_ADAPTER_INFORMATION_SIZE
                              - sizeof(HGSMIHOSTFLAGS);
    if (poffHostFlags)
        *poffHostFlags =   VBVA_ADAPTER_INFORMATION_SIZE
                         - sizeof(HGSMIHOSTFLAGS);
}


/**
 * Set up the HGSMI guest-to-host command context.
 * @returns iprt status value
 * @param  pCtx                    the context to set up
 * @param  pvGuestHeapMemory       a pointer to the mapped backing memory for
 *                                 the guest heap
 * @param  cbGuestHeapMemory       the size of the backing memory area
 * @param  offVRAMGuestHeapMemory  the offset of the memory pointed to by
 *                                 @a pvGuestHeapMemory within the video RAM
 */
DECLHIDDEN(int) VBoxHGSMISetupGuestContext(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                           void *pvGuestHeapMemory,
                                           uint32_t cbGuestHeapMemory,
                                           uint32_t offVRAMGuestHeapMemory,
                                           const HGSMIENV *pEnv)
{
    /** @todo should we be using a fixed ISA port value here? */
    pCtx->port = (RTIOPORT)VGA_PORT_HGSMI_GUEST;
#ifdef VBOX_WDDM_MINIPORT
    return VBoxSHGSMIInit(&pCtx->heapCtx, pvGuestHeapMemory,
                          cbGuestHeapMemory, offVRAMGuestHeapMemory, pEnv);
#else
    return HGSMIHeapSetup(&pCtx->heapCtx, pvGuestHeapMemory,
                          cbGuestHeapMemory, offVRAMGuestHeapMemory, pEnv);
#endif
}


/**
 * Get the information needed to map the area used by the host to send back
 * requests.
 *
 * @param  pCtx                the context containing the heap to use
 * @param  cbVRAM              how much video RAM is allocated to the device
 * @param  offVRAMBaseMapping  the offset of the basic communication structures
 *                             into the guest's VRAM
 * @param  poffVRAMHostArea    where to store the offset into VRAM of the host
 *                             heap area
 * @param  pcbHostArea         where to store the size of the host heap area
 */
DECLHIDDEN(void) VBoxHGSMIGetHostAreaMapping(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                             uint32_t cbVRAM,
                                             uint32_t offVRAMBaseMapping,
                                             uint32_t *poffVRAMHostArea,
                                             uint32_t *pcbHostArea)
{
    uint32_t offVRAMHostArea = offVRAMBaseMapping, cbHostArea = 0;

    AssertPtrReturnVoid(poffVRAMHostArea);
    AssertPtrReturnVoid(pcbHostArea);
    VBoxQueryConfHGSMI(pCtx, VBOX_VBVA_CONF32_HOST_HEAP_SIZE, &cbHostArea);
    if (cbHostArea != 0)
    {
        uint32_t cbHostAreaMaxSize = cbVRAM / 4;
        /** @todo what is the idea of this? */
        if (cbHostAreaMaxSize >= VBVA_ADAPTER_INFORMATION_SIZE)
        {
            cbHostAreaMaxSize -= VBVA_ADAPTER_INFORMATION_SIZE;
        }
        if (cbHostArea > cbHostAreaMaxSize)
        {
            cbHostArea = cbHostAreaMaxSize;
        }
        /* Round up to 4096 bytes. */
        cbHostArea = (cbHostArea + 0xFFF) & ~0xFFF;
        offVRAMHostArea = offVRAMBaseMapping - cbHostArea;
    }

    *pcbHostArea = cbHostArea;
    *poffVRAMHostArea = offVRAMHostArea;
    LogFunc(("offVRAMHostArea = 0x%08X, cbHostArea = 0x%08X\n",
             offVRAMHostArea, cbHostArea));
}


/**
 * Initialise the host context structure.
 *
 * @param  pCtx               the context structure to initialise
 * @param  pvBaseMapping      where the basic HGSMI structures are mapped at
 * @param  offHostFlags       the offset of the host flags into the basic HGSMI
 *                            structures
 * @param  pvHostAreaMapping  where the area for the host heap is mapped at
 * @param  offVRAMHostArea    offset of the host heap area into VRAM
 * @param  cbHostArea         size in bytes of the host heap area
 */
DECLHIDDEN(void) VBoxHGSMISetupHostContext(PHGSMIHOSTCOMMANDCONTEXT pCtx,
                                           void *pvBaseMapping,
                                           uint32_t offHostFlags,
                                           void *pvHostAreaMapping,
                                           uint32_t offVRAMHostArea,
                                           uint32_t cbHostArea)
{
    uint8_t *pu8HostFlags = ((uint8_t *)pvBaseMapping) + offHostFlags;
    pCtx->pfHostFlags = (HGSMIHOSTFLAGS *)pu8HostFlags;
    /** @todo should we really be using a fixed ISA port value here? */
    pCtx->port        = (RTIOPORT)VGA_PORT_HGSMI_HOST;
    HGSMIAreaInitialize(&pCtx->areaCtx, pvHostAreaMapping, cbHostArea,
                         offVRAMHostArea);
}


/**
 * Tell the host about the ways it can use to communicate back to us via an
 * HGSMI command
 *
 * @returns  iprt status value
 * @param  pCtx                  the context containing the heap to use
 * @param  offVRAMFlagsLocation  where we wish the host to place its flags
 *                               relative to the start of the VRAM
 * @param  fCaps                 additions HGSMI capabilities the guest
 *                               supports
 * @param  offVRAMHostArea       offset into VRAM of the host heap area
 * @param  cbHostArea            size in bytes of the host heap area
 */
DECLHIDDEN(int) VBoxHGSMISendHostCtxInfo(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                         HGSMIOFFSET offVRAMFlagsLocation,
                                         uint32_t fCaps,
                                         uint32_t offVRAMHostArea,
                                         uint32_t cbHostArea)
{
    Log(("VBoxVideo::vboxSetupAdapterInfo\n"));

    /* setup the flags first to ensure they are initialized by the time the
     * host heap is ready */
    int rc = vboxHGSMIReportFlagsLocation(pCtx, offVRAMFlagsLocation);
    AssertRC(rc);
    if (RT_SUCCESS(rc) && fCaps)
    {
        /* Inform about caps */
        rc = vboxHGSMISendCapsInfo(pCtx, fCaps);
        AssertRC(rc);
    }
    if (RT_SUCCESS (rc))
    {
        /* Report the host heap location. */
        rc = vboxHGSMIReportHostArea(pCtx, offVRAMHostArea, cbHostArea);
        AssertRC(rc);
    }
    Log(("VBoxVideo::vboxSetupAdapterInfo finished rc = %d\n", rc));
    return rc;
}


/** Sanity test on first call.  We do not worry about concurrency issues. */
static int testQueryConf(PHGSMIGUESTCOMMANDCONTEXT pCtx)
{
    static bool cOnce = false;
    uint32_t ulValue = 0;
    int rc;

    if (cOnce)
        return VINF_SUCCESS;
    cOnce = true;
    rc = VBoxQueryConfHGSMI(pCtx, UINT32_MAX, &ulValue);
    if (RT_SUCCESS(rc) && ulValue == UINT32_MAX)
        return VINF_SUCCESS;
    cOnce = false;
    if (RT_FAILURE(rc))
        return rc;
    return VERR_INTERNAL_ERROR;
}


/**
 * Query the host for an HGSMI configuration parameter via an HGSMI command.
 * @returns iprt status value
 * @param  pCtx      the context containing the heap used
 * @param  u32Index  the index of the parameter to query,
 *                   @see VBVACONF32::u32Index
 * @param  u32DefValue defaut value
 * @param  pulValue  where to store the value of the parameter on success
 */
DECLHIDDEN(int) VBoxQueryConfHGSMIDef(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                      uint32_t u32Index, uint32_t u32DefValue, uint32_t *pulValue)
{
    int rc = VINF_SUCCESS;
    VBVACONF32 *p;
    LogFunc(("u32Index = %d\n", u32Index));

    rc = testQueryConf(pCtx);
    if (RT_FAILURE(rc))
        return rc;
    /* Allocate the IO buffer. */
    p = (VBVACONF32 *)VBoxHGSMIBufferAlloc(pCtx,
                                     sizeof(VBVACONF32), HGSMI_CH_VBVA,
                                     VBVA_QUERY_CONF32);
    if (p)
    {
        /* Prepare data to be sent to the host. */
        p->u32Index = u32Index;
        p->u32Value = u32DefValue;
        rc = VBoxHGSMIBufferSubmit(pCtx, p);
        if (RT_SUCCESS(rc))
        {
            *pulValue = p->u32Value;
            LogFunc(("u32Value = %d\n", p->u32Value));
        }
        /* Free the IO buffer. */
        VBoxHGSMIBufferFree(pCtx, p);
    }
    else
        rc = VERR_NO_MEMORY;
    LogFunc(("rc = %d\n", rc));
    return rc;
}

DECLHIDDEN(int) VBoxQueryConfHGSMI(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                   uint32_t u32Index, uint32_t *pulValue)
{
    return VBoxQueryConfHGSMIDef(pCtx, u32Index, UINT32_MAX, pulValue);
}

/**
 * Pass the host a new mouse pointer shape via an HGSMI command.
 *
 * @returns  success or failure
 * @param  fFlags    cursor flags, @see VMMDevReqMousePointer::fFlags
 * @param  cHotX     horizontal position of the hot spot
 * @param  cHotY     vertical position of the hot spot
 * @param  cWidth    width in pixels of the cursor
 * @param  cHeight   height in pixels of the cursor
 * @param  pPixels   pixel data, @see VMMDevReqMousePointer for the format
 * @param  cbLength  size in bytes of the pixel data
 */
DECLHIDDEN(int)  VBoxHGSMIUpdatePointerShape(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                             uint32_t fFlags,
                                             uint32_t cHotX,
                                             uint32_t cHotY,
                                             uint32_t cWidth,
                                             uint32_t cHeight,
                                             uint8_t *pPixels,
                                             uint32_t cbLength)
{
    VBVAMOUSEPOINTERSHAPE *p;
    uint32_t cbData = 0;
    int rc = VINF_SUCCESS;

    if (fFlags & VBOX_MOUSE_POINTER_SHAPE)
    {
        /* Size of the pointer data: sizeof (AND mask) + sizeof (XOR_MASK) */
        cbData = ((((cWidth + 7) / 8) * cHeight + 3) & ~3)
                 + cWidth * 4 * cHeight;
        /* If shape is supplied, then always create the pointer visible.
         * See comments in 'vboxUpdatePointerShape'
         */
        fFlags |= VBOX_MOUSE_POINTER_VISIBLE;
    }
    LogFlowFunc(("cbData %d, %dx%d\n", cbData, cWidth, cHeight));
    if (cbData > cbLength)
    {
        LogFunc(("calculated pointer data size is too big (%d bytes, limit %d)\n",
                 cbData, cbLength));
        return VERR_INVALID_PARAMETER;
    }
    /* Allocate the IO buffer. */
    p = (VBVAMOUSEPOINTERSHAPE *)VBoxHGSMIBufferAlloc(pCtx,
                                                  sizeof(VBVAMOUSEPOINTERSHAPE)
                                                + cbData,
                                                HGSMI_CH_VBVA,
                                                VBVA_MOUSE_POINTER_SHAPE);
    if (p)
    {
        /* Prepare data to be sent to the host. */
        /* Will be updated by the host. */
        p->i32Result = VINF_SUCCESS;
        /* We have our custom flags in the field */
        p->fu32Flags = fFlags;
        p->u32HotX   = cHotX;
        p->u32HotY   = cHotY;
        p->u32Width  = cWidth;
        p->u32Height = cHeight;
        if (p->fu32Flags & VBOX_MOUSE_POINTER_SHAPE)
            /* Copy the actual pointer data. */
            memcpy (p->au8Data, pPixels, cbData);
        rc = VBoxHGSMIBufferSubmit(pCtx, p);
        if (RT_SUCCESS(rc))
            rc = p->i32Result;
        /* Free the IO buffer. */
        VBoxHGSMIBufferFree(pCtx, p);
    }
    else
        rc = VERR_NO_MEMORY;
    LogFlowFunc(("rc %d\n", rc));
    return rc;
}


/**
 * Report the guest cursor position.  The host may wish to use this information
 * to re-position its own cursor (though this is currently unlikely).  The
 * current host cursor position is returned.
 * @param  pCtx             The context containing the heap used.
 * @param  fReportPosition  Are we reporting a position?
 * @param  x                Guest cursor X position.
 * @param  y                Guest cursor Y position.
 * @param  pxHost           Host cursor X position is stored here.  Optional.
 * @param  pyHost           Host cursor Y position is stored here.  Optional.
 * @returns  iprt status code.
 * @returns  VERR_NO_MEMORY      HGSMI heap allocation failed.
 */
DECLHIDDEN(int) VBoxHGSMICursorPosition(PHGSMIGUESTCOMMANDCONTEXT pCtx, bool fReportPosition, uint32_t x, uint32_t y,
                                        uint32_t *pxHost, uint32_t *pyHost)
{
    int rc = VINF_SUCCESS;
    VBVACURSORPOSITION *p;
    Log(("%s: x=%u, y=%u\n", __PRETTY_FUNCTION__, (unsigned)x, (unsigned)y));

    /* Allocate the IO buffer. */
    p = (VBVACURSORPOSITION *)VBoxHGSMIBufferAlloc(pCtx, sizeof(VBVACURSORPOSITION), HGSMI_CH_VBVA, VBVA_CURSOR_POSITION);
    if (p)
    {
        /* Prepare data to be sent to the host. */
        p->fReportPosition = fReportPosition ? 1 : 0;
        p->x = x;
        p->y = y;
        rc = VBoxHGSMIBufferSubmit(pCtx, p);
        if (RT_SUCCESS(rc))
        {
            if (pxHost)
                *pxHost = p->x;
            if (pyHost)
                *pyHost = p->y;
            Log(("%s: return: x=%u, y=%u\n", __PRETTY_FUNCTION__, (unsigned)p->x, (unsigned)p->y));
        }
        /* Free the IO buffer. */
        VBoxHGSMIBufferFree(pCtx, p);
    }
    else
        rc = VERR_NO_MEMORY;
    LogFunc(("rc = %d\n", rc));
    return rc;
}


/** @todo Mouse pointer position to be read from VMMDev memory, address of the memory region
 * can be queried from VMMDev via an IOCTL. This VMMDev memory region will contain
 * host information which is needed by the guest.
 *
 * Reading will not cause a switch to the host.
 *
 * Have to take into account:
 *  * synchronization: host must write to the memory only from EMT,
 *    large structures must be read under flag, which tells the host
 *    that the guest is currently reading the memory (OWNER flag?).
 *  * guest writes: may be allocate a page for the host info and make
 *    the page readonly for the guest.
 *  * the information should be available only for additions drivers.
 *  * VMMDev additions driver will inform the host which version of the info it expects,
 *    host must support all versions.
 *
 */
