/* $Id: HGSMICommon.cpp $ */
/** @file
 * VBox Host Guest Shared Memory Interface (HGSMI) - Functions common to both host and guest.
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

#define LOG_DISABLED /* Maybe we can enabled it all the time now? */
#define LOG_GROUP LOG_GROUP_HGSMI
#include <iprt/heap.h>
#include <iprt/string.h>

#include <VBox/HGSMI/HGSMI.h>
#include <VBox/log.h>


/* Channel flags. */
#define HGSMI_CH_F_REGISTERED 0x01

/* Assertions for situations which could happen and normally must be processed properly
 * but must be investigated during development: guest misbehaving, etc.
 */
#ifdef HGSMI_STRICT
#define HGSMI_STRICT_ASSERT_FAILED() AssertFailed()
#define HGSMI_STRICT_ASSERT(expr) Assert(expr)
#else
#define HGSMI_STRICT_ASSERT_FAILED() do {} while (0)
#define HGSMI_STRICT_ASSERT(expr) do {} while (0)
#endif /* !HGSMI_STRICT */

/* One-at-a-Time Hash from
 * http://www.burtleburtle.net/bob/hash/doobs.html
 *
 * ub4 one_at_a_time(char *key, ub4 len)
 * {
 *   ub4   hash, i;
 *   for (hash=0, i=0; i<len; ++i)
 *   {
 *     hash += key[i];
 *     hash += (hash << 10);
 *     hash ^= (hash >> 6);
 *   }
 *   hash += (hash << 3);
 *   hash ^= (hash >> 11);
 *   hash += (hash << 15);
 *   return hash;
 * }
 */

static uint32_t hgsmiHashBegin(void)
{
    return 0;
}

static uint32_t hgsmiHashProcess(uint32_t hash,
                                 const void *pvData,
                                 size_t cbData)
{
    const uint8_t *pu8Data = (const uint8_t *)pvData;

    while (cbData--)
    {
        hash += *pu8Data++;
        hash += (hash << 10);
        hash ^= (hash >> 6);
    }

    return hash;
}

static uint32_t hgsmiHashEnd(uint32_t hash)
{
    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);

    return hash;
}

uint32_t HGSMIChecksum(HGSMIOFFSET offBuffer,
                       const HGSMIBUFFERHEADER *pHeader,
                       const HGSMIBUFFERTAIL *pTail)
{
    uint32_t u32Checksum = hgsmiHashBegin();

    u32Checksum = hgsmiHashProcess(u32Checksum, &offBuffer, sizeof(offBuffer));
    u32Checksum = hgsmiHashProcess(u32Checksum, pHeader, sizeof(HGSMIBUFFERHEADER));
    u32Checksum = hgsmiHashProcess(u32Checksum, pTail, RT_OFFSETOF(HGSMIBUFFERTAIL, u32Checksum));

    return hgsmiHashEnd(u32Checksum);
}

int HGSMIAreaInitialize(HGSMIAREA *pArea,
                        void *pvBase,
                        HGSMISIZE cbArea,
                        HGSMIOFFSET offBase)
{
    uint8_t *pu8Base = (uint8_t *)pvBase;

    if (  !pArea                                   /* Check that the area: */
        || cbArea < HGSMIBufferMinimumSize()       /* large enough; */
        || pu8Base + cbArea < pu8Base              /* no address space wrap; */
        || offBase > UINT32_C(0xFFFFFFFF) - cbArea /* area within the 32 bit space: offBase + cbMem <= 0xFFFFFFFF. */
       )
    {
        return VERR_INVALID_PARAMETER;
    }

    pArea->pu8Base = pu8Base;
    pArea->offBase = offBase;
    pArea->offLast = cbArea - HGSMIBufferMinimumSize() + offBase;
    pArea->cbArea = cbArea;

    return VINF_SUCCESS;
}

void HGSMIAreaClear(HGSMIAREA *pArea)
{
    if (pArea)
    {
        RT_ZERO(*pArea);
    }
}

/* Initialize the memory buffer including its checksum.
 * No changes alloed to the header and the tail after that.
 */
HGSMIOFFSET HGSMIBufferInitializeSingle(const HGSMIAREA *pArea,
                                        HGSMIBUFFERHEADER *pHeader,
                                        HGSMISIZE cbBuffer,
                                        uint8_t u8Channel,
                                        uint16_t u16ChannelInfo)
{
    if (   !pArea
        || !pHeader
        || cbBuffer < HGSMIBufferMinimumSize())
    {
        return HGSMIOFFSET_VOID;
    }

    /* Buffer must be within the area:
     *   * header data size do not exceed the maximum data size;
     *   * buffer address is greater than the area base address;
     *   * buffer address is lower than the maximum allowed for the given data size.
     */
    HGSMISIZE cbMaximumDataSize = pArea->offLast - pArea->offBase;
    uint32_t u32DataSize = cbBuffer - HGSMIBufferMinimumSize();

    if (   u32DataSize > cbMaximumDataSize
        || (uint8_t *)pHeader < pArea->pu8Base
        || (uint8_t *)pHeader > pArea->pu8Base + cbMaximumDataSize - u32DataSize)
    {
        return HGSMIOFFSET_VOID;
    }

    HGSMIOFFSET offBuffer = HGSMIPointerToOffset(pArea, pHeader);

    pHeader->u8Flags        = HGSMI_BUFFER_HEADER_F_SEQ_SINGLE;
    pHeader->u32DataSize    = u32DataSize;
    pHeader->u8Channel      = u8Channel;
    pHeader->u16ChannelInfo = u16ChannelInfo;
    RT_ZERO(pHeader->u.au8Union);

    HGSMIBUFFERTAIL *pTail = HGSMIBufferTailFromPtr(pHeader, u32DataSize);
    pTail->u32Reserved = 0;
    pTail->u32Checksum = HGSMIChecksum(offBuffer, pHeader, pTail);

    return offBuffer;
}

int HGSMIHeapSetup(HGSMIHEAP *pHeap,
                   void *pvBase,
                   HGSMISIZE cbArea,
                   HGSMIOFFSET offBase,
                   const HGSMIENV *pEnv)
{
    AssertPtrReturn(pHeap, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pvBase, VERR_INVALID_PARAMETER);

    int rc = HGSMIAreaInitialize(&pHeap->area, pvBase, cbArea, offBase);
    if (RT_SUCCESS(rc))
    {
        rc = HGSMIMAInit(&pHeap->ma, &pHeap->area, NULL, 0, 0, pEnv);
        if (RT_FAILURE(rc))
        {
            HGSMIAreaClear(&pHeap->area);
        }
    }

    return rc;
}

void HGSMIHeapDestroy(HGSMIHEAP *pHeap)
{
    if (pHeap)
    {
        HGSMIMAUninit(&pHeap->ma);
        RT_ZERO(*pHeap);
    }
}

void *HGSMIHeapAlloc(HGSMIHEAP *pHeap,
                     HGSMISIZE cbData,
                     uint8_t u8Channel,
                     uint16_t u16ChannelInfo)
{
    HGSMISIZE cbAlloc = HGSMIBufferRequiredSize(cbData);
    HGSMIBUFFERHEADER *pHeader = (HGSMIBUFFERHEADER *)HGSMIHeapBufferAlloc(pHeap, cbAlloc);
    if (pHeader)
    {
        HGSMIOFFSET offBuffer = HGSMIBufferInitializeSingle(HGSMIHeapArea(pHeap), pHeader,
                                                            cbAlloc, u8Channel, u16ChannelInfo);
        if (offBuffer == HGSMIOFFSET_VOID)
        {
            HGSMIHeapBufferFree(pHeap, pHeader);
            pHeader = NULL;
        }
    }

    return pHeader? HGSMIBufferDataFromPtr(pHeader): NULL;
}

void HGSMIHeapFree(HGSMIHEAP *pHeap,
                   void *pvData)
{
    if (pvData)
    {
        HGSMIBUFFERHEADER *pHeader = HGSMIBufferHeaderFromData(pvData);
        HGSMIHeapBufferFree(pHeap, pHeader);
    }
}

void *HGSMIHeapBufferAlloc(HGSMIHEAP *pHeap,
                           HGSMISIZE cbBuffer)
{
    void *pvBuf = HGSMIMAAlloc(&pHeap->ma, cbBuffer);
    return pvBuf;
}

void HGSMIHeapBufferFree(HGSMIHEAP *pHeap,
                         void *pvBuf)
{
    HGSMIMAFree(&pHeap->ma, pvBuf);
}

typedef struct HGSMIBUFFERCONTEXT
{
    const HGSMIBUFFERHEADER *pHeader; /* The original buffer header. */
    void *pvData;                     /* Payload data in the buffer./ */
    uint32_t cbData;                  /* Size of data  */
} HGSMIBUFFERCONTEXT;

/** Verify that the given offBuffer points to a valid buffer, which is within the area.
 *
 * @returns VBox status and the buffer information in pBufferContext.
 * @param pArea          Area which supposed to contain the buffer.
 * @param offBuffer      The buffer location in the area.
 * @param pBufferContext Where to write information about the buffer.
 */
static int hgsmiVerifyBuffer(const HGSMIAREA *pArea,
                             HGSMIOFFSET offBuffer,
                             HGSMIBUFFERCONTEXT *pBufferContext)
{
    LogFlowFunc(("buffer 0x%x, area %p %x [0x%x;0x%x]\n",
                 offBuffer, pArea->pu8Base, pArea->cbArea, pArea->offBase, pArea->offLast));

    int rc = VINF_SUCCESS;

    if (   offBuffer < pArea->offBase
        || offBuffer > pArea->offLast)
    {
        LogFunc(("offset 0x%x is outside the area [0x%x;0x%x]!!!\n",
                 offBuffer, pArea->offBase, pArea->offLast));
        rc = VERR_INVALID_PARAMETER;
        HGSMI_STRICT_ASSERT_FAILED();
    }
    else
    {
        void *pvBuffer = HGSMIOffsetToPointer(pArea, offBuffer);
        HGSMIBUFFERHEADER header = *HGSMIBufferHeaderFromPtr(pvBuffer);

        /* Quick check of the data size, it should be less than the maximum
         * data size for the buffer at this offset.
         */
        LogFlowFunc(("datasize check: header.u32DataSize = 0x%x pArea->offLast - offBuffer = 0x%x\n",
                     header.u32DataSize, pArea->offLast - offBuffer));

        if (header.u32DataSize <= pArea->offLast - offBuffer)
        {
            HGSMIBUFFERTAIL tail = *HGSMIBufferTailFromPtr(pvBuffer, header.u32DataSize);

            /* At least both header and tail structures are in the area. Check the checksum. */
            uint32_t u32Checksum = HGSMIChecksum(offBuffer, &header, &tail);
            LogFlowFunc(("checksum check: u32Checksum = 0x%x pTail->u32Checksum = 0x%x\n",
                         u32Checksum, tail.u32Checksum));
            if (u32Checksum == tail.u32Checksum)
            {
                /* Success. */
                pBufferContext->pHeader = HGSMIBufferHeaderFromPtr(pvBuffer);
                pBufferContext->pvData = HGSMIBufferDataFromPtr(pvBuffer);
                pBufferContext->cbData = header.u32DataSize;
            }
            else
            {
                LogFunc(("invalid checksum 0x%x, expected 0x%x!!!\n",
                         u32Checksum, tail.u32Checksum));
                rc = VERR_INVALID_STATE;
                HGSMI_STRICT_ASSERT_FAILED();
            }
        }
        else
        {
            LogFunc(("invalid data size 0x%x, maximum is 0x%x!!!\n",
                     header.u32DataSize, pArea->offLast - offBuffer));
            rc = VERR_TOO_MUCH_DATA;
            HGSMI_STRICT_ASSERT_FAILED();
        }
    }

    return rc;
}

/** Helper to convert HGSMI channel index to the channel structure pointer.
 *
 * @returns Pointer to the channel data.
 * @param pChannelInfo The channel pool.
 * @param u8Channel    The channel index.
 */
HGSMICHANNEL *HGSMIChannelFindById(HGSMICHANNELINFO *pChannelInfo,
                                   uint8_t u8Channel)
{
    AssertCompile(RT_ELEMENTS(pChannelInfo->Channels) >= 0x100);
    HGSMICHANNEL *pChannel = &pChannelInfo->Channels[u8Channel];

    if (pChannel->u8Flags & HGSMI_CH_F_REGISTERED)
    {
        return pChannel;
    }

    return NULL;
}

/** Process a guest buffer.
 *
 * @returns VBox status code.
 * @param pArea        Area which supposed to contain the buffer.
 * @param pChannelInfo The channel pool.
 * @param offBuffer    The buffer location in the area.
 */
int HGSMIBufferProcess(const HGSMIAREA *pArea,
                       HGSMICHANNELINFO *pChannelInfo,
                       HGSMIOFFSET offBuffer)
{
    LogFlowFunc(("pArea %p, offBuffer 0x%x\n", pArea, offBuffer));

    AssertPtrReturn(pArea, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pChannelInfo, VERR_INVALID_PARAMETER);

    /* Guest has prepared a command description at 'offBuffer'. */
    HGSMIBUFFERCONTEXT bufferContext = { NULL, NULL, 0 }; /* Makes old GCC happier. */
    int rc = hgsmiVerifyBuffer(pArea, offBuffer, &bufferContext);
    if (RT_SUCCESS(rc))
    {
        /* Pass the command to the appropriate handler registered with this instance.
         * Start with the handler list head, which is the preallocated HGSMI setup channel.
         */
        const HGSMICHANNEL *pChannel = HGSMIChannelFindById(pChannelInfo, bufferContext.pHeader->u8Channel);
        if (pChannel)
        {
            const HGSMICHANNELHANDLER *pHandler = &pChannel->handler;
            if (pHandler->pfnHandler)
            {
                pHandler->pfnHandler(pHandler->pvHandler, bufferContext.pHeader->u16ChannelInfo,
                                     bufferContext.pvData, bufferContext.cbData);
            }
            HGSMI_STRICT_ASSERT(RT_SUCCESS(hgsmiVerifyBuffer(pArea, offBuffer, &bufferContext)));
        }
        else
        {
            rc = VERR_INVALID_FUNCTION;
            HGSMI_STRICT_ASSERT_FAILED();
        }
    }

    return rc;
}

/** Register a new HGSMI channel by index.
 *
 * @returns VBox status code.
 * @param pChannelInfo      The channel pool managed by the caller.
 * @param u8Channel         Index of the channel.
 * @param pszName           Name of the channel (optional, allocated by the caller).
 * @param pfnChannelHandler The channel callback.
 * @param pvChannelHandler  The callback pointer.
 */
int HGSMIChannelRegister(HGSMICHANNELINFO *pChannelInfo,
                         uint8_t u8Channel,
                         const char *pszName,
                         PFNHGSMICHANNELHANDLER pfnChannelHandler,
                         void *pvChannelHandler)
{
    /* Check whether the channel is already registered. */
    HGSMICHANNEL *pChannel = HGSMIChannelFindById(pChannelInfo, u8Channel);
    if (pChannel)
    {
        HGSMI_STRICT_ASSERT_FAILED();
        return VERR_ALREADY_EXISTS;
    }

    /* Channel is not yet registered. */
    pChannel = &pChannelInfo->Channels[u8Channel];

    pChannel->u8Flags = HGSMI_CH_F_REGISTERED;
    pChannel->u8Channel = u8Channel;

    pChannel->handler.pfnHandler = pfnChannelHandler;
    pChannel->handler.pvHandler = pvChannelHandler;

    pChannel->pszName = pszName;

    return VINF_SUCCESS;
}
