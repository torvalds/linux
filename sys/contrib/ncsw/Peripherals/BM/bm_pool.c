/******************************************************************************

 © 1995-2003, 2004, 2005-2011 Freescale Semiconductor, Inc.
 All rights reserved.

 This is proprietary source code of Freescale Semiconductor Inc.,
 and its use is subject to the NetComm Device Drivers EULA.
 The copyright notice above does not evidence any actual or intended
 publication of such source code.

 ALTERNATIVELY, redistribution and use in source and binary forms, with
 or without modification, are permitted provided that the following
 conditions are met:
     * Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
     * Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.
     * Neither the name of Freescale Semiconductor nor the
       names of its contributors may be used to endorse or promote products
       derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *

 **************************************************************************/
/******************************************************************************
 @File          bm.c

 @Description   BM
*//***************************************************************************/
#include "error_ext.h"
#include "std_ext.h"
#include "string_ext.h"
#include "mem_ext.h"
#include "core_ext.h"

#include "bm.h"


#define __ERR_MODULE__  MODULE_BM


/****************************************/
/*       static functions               */
/****************************************/

/* (De)Registration of depletion notification callbacks */
static void depletion_link(t_BmPool *p_BmPool)
{
    t_BmPortal *p_Portal = (t_BmPortal *)p_BmPool->h_BmPortal;

    NCSW_PLOCK(p_Portal);
    p_Portal->depletionPoolsTable[p_BmPool->bpid] = p_BmPool;
    bm_isr_bscn_mask(p_Portal->p_BmPortalLow, (uint8_t)p_BmPool->bpid, 1);
    PUNLOCK(p_Portal);
}

static void depletion_unlink(t_BmPool *p_BmPool)
{
    t_BmPortal *p_Portal = (t_BmPortal *)p_BmPool->h_BmPortal;

    NCSW_PLOCK(p_Portal);
    p_Portal->depletionPoolsTable[p_BmPool->bpid] = NULL;
    bm_isr_bscn_mask(p_Portal->p_BmPortalLow, (uint8_t)p_BmPool->bpid, 0);
    PUNLOCK(p_Portal);
}

static t_Error BmPoolRelease(t_BmPool *p_BmPool,
                             t_Handle h_BmPortal,
                             struct bm_buffer *bufs,
                             uint8_t num,
                             uint32_t flags)
{
    ASSERT_COND(num && (num <= 8));
    if (p_BmPool->flags & BMAN_POOL_FLAG_NO_RELEASE)
        return ERROR_CODE(E_INVALID_VALUE);

    /* Without stockpile, this API is a pass-through to the h/w operation */
    if (!(p_BmPool->flags & BMAN_POOL_FLAG_STOCKPILE))
        return BmPortalRelease(h_BmPortal, p_BmPool->bpid, bufs, num, flags);

    /* This needs some explanation. Adding the given buffers may take the
     * stockpile over the threshold, but in fact the stockpile may already
     * *be* over the threshold if a previous release-to-hw attempt had
     * failed. So we have 3 cases to cover;
     *   1. we add to the stockpile and don't hit the threshold,
     *   2. we add to the stockpile, hit the threshold and release-to-hw,
     *   3. we have to release-to-hw before adding to the stockpile
     *      (not enough room in the stockpile for case 2).
     * Our constraints on thresholds guarantee that in case 3, there must be
     * at least 8 bufs already in the stockpile, so all release-to-hw ops
     * are for 8 bufs. Despite all this, the API must indicate whether the
     * given buffers were taken off the caller's hands, irrespective of
     * whether a release-to-hw was attempted. */
    while (num)
    {
        /* Add buffers to stockpile if they fit */
        if ((p_BmPool->spFill + num) <= p_BmPool->spMaxBufs)
        {
            memcpy(PTR_MOVE(p_BmPool->sp, sizeof(struct bm_buffer) * (p_BmPool->spFill)),
                   bufs,
                   sizeof(struct bm_buffer) * num);
            p_BmPool->spFill += num;
            num = 0; /* --> will return success no matter what */
        }
        else
        /* Do hw op if hitting the high-water threshold */
        {
            t_Error ret = BmPortalRelease(h_BmPortal,
                                          p_BmPool->bpid,
                                          (struct bm_buffer *)PTR_MOVE(p_BmPool->sp, sizeof(struct bm_buffer) * (p_BmPool->spFill - p_BmPool->spBufsCmd)),
                                          p_BmPool->spBufsCmd,
                                          flags);
            if (ret)
                return (num ? ret : E_OK);
            p_BmPool->spFill -= p_BmPool->spBufsCmd;
        }
    }

    return E_OK;
}

static int BmPoolAcquire(t_BmPool *p_BmPool,t_Handle h_BmPortal,
            struct bm_buffer *bufs, uint8_t num, uint32_t flags)
{
    ASSERT_COND(IN_RANGE(1, num, 8));
    if (p_BmPool->flags & BMAN_POOL_FLAG_ONLY_RELEASE)
        return 0;

    /* Without stockpile, this API is a pass-through to the h/w operation */
    if (!(p_BmPool->flags & BMAN_POOL_FLAG_STOCKPILE))
        return BmPortalAcquire(h_BmPortal, p_BmPool->bpid, bufs, num);
    /* Only need a h/w op if we'll hit the low-water thresh */
    if (!(flags & BMAN_ACQUIRE_FLAG_STOCKPILE) &&
            ((p_BmPool->spFill - num) < p_BmPool->spMinBufs))
    {
            p_BmPool->spFill += BmPortalAcquire(h_BmPortal,
                                               p_BmPool->bpid,
                                               (struct bm_buffer *)PTR_MOVE(p_BmPool->sp, sizeof(struct bm_buffer) * (p_BmPool->spFill)),
                                               p_BmPool->spBufsCmd);
    }
    else if (p_BmPool->spFill < num)
        return 0;
    if (!p_BmPool->spFill)
        return 0;
    memcpy(bufs,
           PTR_MOVE(p_BmPool->sp, sizeof(struct bm_buffer) * (p_BmPool->spFill - num)),
           sizeof(struct bm_buffer) * num);
    p_BmPool->spFill -= num;
    return num;
}

static t_Error BmPoolFree(t_BmPool *p_BmPool, bool discardBuffers)
{
    t_Handle    h_BufContext;
    void        *p_Data;

    ASSERT_COND(p_BmPool);

    if (!p_BmPool->shadowMode)
    {
        if (p_BmPool->flags & BMAN_POOL_FLAG_DEPLETION)
        {
            depletion_unlink(p_BmPool);
            BmUnSetPoolThresholds(p_BmPool->h_Bm, p_BmPool->bpid);
        }
        while (TRUE)
        {
            p_Data = BM_POOL_GetBuf(p_BmPool, p_BmPool->h_BmPortal);
            if (!p_Data)
                break;
            h_BufContext = BM_POOL_GetBufferContext(p_BmPool, p_Data);
            if (!discardBuffers)
                p_BmPool->bufferPoolInfo.f_PutBuf(p_BmPool->bufferPoolInfo.h_BufferPool, p_Data, h_BufContext);
        }
        BmBpidPut(p_BmPool->h_Bm, p_BmPool->bpid);
    }

    if (p_BmPool->sp)
        XX_Free(p_BmPool->sp);

    XX_Free(p_BmPool);

    return E_OK;
}

/****************************************/
/*       API Init unit functions        */
/****************************************/

t_Handle BM_POOL_Config(t_BmPoolParam *p_BmPoolParam)
{
    t_BmPool        *p_BmPool;

    SANITY_CHECK_RETURN_VALUE(p_BmPoolParam, E_INVALID_HANDLE, NULL);
    SANITY_CHECK_RETURN_VALUE(p_BmPoolParam->h_Bm, E_INVALID_HANDLE, NULL);
    SANITY_CHECK_RETURN_VALUE((p_BmPoolParam->shadowMode ||
                               (p_BmPoolParam->bufferPoolInfo.h_BufferPool &&
                               p_BmPoolParam->bufferPoolInfo.f_GetBuf &&
                               p_BmPoolParam->bufferPoolInfo.f_PutBuf &&
                               p_BmPoolParam->bufferPoolInfo.bufferSize)), E_INVALID_STATE, NULL);

    p_BmPool = (t_BmPool*)XX_Malloc(sizeof(t_BmPool));
    if (!p_BmPool)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("BM Pool obj!!!"));
        return NULL;
    }
    memset(p_BmPool, 0, sizeof(t_BmPool));

    p_BmPool->p_BmPoolDriverParams = (t_BmPoolDriverParams *)XX_Malloc(sizeof(t_BmPoolDriverParams));
    if (!p_BmPool->p_BmPoolDriverParams)
    {
        XX_Free(p_BmPool);
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("Bm Pool driver parameters"));
        return NULL;
    }
    memset(p_BmPool->p_BmPoolDriverParams, 0, sizeof(t_BmPoolDriverParams));

    p_BmPool->h_Bm          = p_BmPoolParam->h_Bm;
    p_BmPool->h_BmPortal    = p_BmPoolParam->h_BmPortal;
    p_BmPool->h_App         = p_BmPoolParam->h_App;
    p_BmPool->numOfBuffers  = p_BmPoolParam->numOfBuffers;
    p_BmPool->shadowMode    = p_BmPoolParam->shadowMode;

    if (!p_BmPool->h_BmPortal)
    {
        p_BmPool->h_BmPortal = BmGetPortalHandle(p_BmPool->h_Bm);
        SANITY_CHECK_RETURN_VALUE(p_BmPool->h_BmPortal, E_INVALID_HANDLE, NULL);
    }

    memcpy(&p_BmPool->bufferPoolInfo, &p_BmPoolParam->bufferPoolInfo, sizeof(t_BufferPoolInfo));
    if (!p_BmPool->bufferPoolInfo.f_PhysToVirt)
        p_BmPool->bufferPoolInfo.f_PhysToVirt = XX_PhysToVirt;
    if (!p_BmPool->bufferPoolInfo.f_VirtToPhys)
        p_BmPool->bufferPoolInfo.f_VirtToPhys = XX_VirtToPhys;

    p_BmPool->p_BmPoolDriverParams->dynamicBpid     = DEFAULT_dynamicBpid;
    p_BmPool->p_BmPoolDriverParams->useDepletion    = DEFAULT_useDepletion;
    p_BmPool->p_BmPoolDriverParams->useStockpile    = DEFAULT_useStockpile;

    if (p_BmPool->shadowMode)
    {
        p_BmPool->numOfBuffers = 0;
        BM_POOL_ConfigBpid(p_BmPool, p_BmPoolParam->bpid);
    }

    return p_BmPool;
}

t_Error BM_POOL_Init(t_Handle h_BmPool)
{
    t_BmPool        *p_BmPool = (t_BmPool *)h_BmPool;
    t_Error         err;

    SANITY_CHECK_RETURN_ERROR(p_BmPool, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_BmPool->p_BmPoolDriverParams, E_INVALID_HANDLE);

    p_BmPool->flags |= (p_BmPool->p_BmPoolDriverParams->dynamicBpid)?BMAN_POOL_FLAG_DYNAMIC_BPID:0;
    p_BmPool->flags |= (p_BmPool->p_BmPoolDriverParams->useStockpile)?BMAN_POOL_FLAG_STOCKPILE:0;
    p_BmPool->flags |= ((!p_BmPool->shadowMode) &&
                        (p_BmPool->p_BmPoolDriverParams->useDepletion))?BMAN_POOL_FLAG_DEPLETION:0;

    if (p_BmPool->flags & BMAN_POOL_FLAG_DYNAMIC_BPID)
    {
        if((p_BmPool->bpid = (uint8_t)BmBpidGet(p_BmPool->h_Bm, FALSE, (uint32_t)0)) == (uint8_t)ILLEGAL_BASE)
        {
            BM_POOL_Free(p_BmPool);
            RETURN_ERROR(CRITICAL, E_INVALID_STATE, ("can't allocate new dynamic pool id"));
        }
    }
    else
    {
        if (BmBpidGet(p_BmPool->h_Bm, TRUE, (uint32_t)p_BmPool->bpid) == (uint32_t)ILLEGAL_BASE)
        {
            BM_POOL_Free(p_BmPool);
            RETURN_ERROR(CRITICAL, E_INVALID_STATE, ("can't force pool id %d", p_BmPool->bpid));
        }
    }
    if (p_BmPool->flags & BMAN_POOL_FLAG_DEPLETION)
    {
        if(BmSetPoolThresholds(p_BmPool->h_Bm, p_BmPool->bpid, p_BmPool->p_BmPoolDriverParams->depletionThresholds))
        {
            BM_POOL_Free(p_BmPool);
            RETURN_ERROR(MAJOR, E_INVALID_STATE, ("can't set thresh for pool bpid %d",p_BmPool->bpid));
        }

        depletion_link(p_BmPool);
    }

    if (p_BmPool->flags & BMAN_POOL_FLAG_STOCKPILE)
    {
        p_BmPool->sp = (struct bm_buffer *)XX_Malloc(sizeof(struct bm_buffer) * p_BmPool->spMaxBufs);
        if (!p_BmPool->sp)
        {
            BM_POOL_Free(p_BmPool);
            RETURN_ERROR(MAJOR, E_NO_MEMORY, ("Bm Pool Stockpile"));
        }
        memset(p_BmPool->sp, 0, sizeof(struct bm_buffer) * p_BmPool->spMaxBufs);
    }

    XX_Free(p_BmPool->p_BmPoolDriverParams);
    p_BmPool->p_BmPoolDriverParams = NULL;

    /*******************/
    /* Create buffers  */
    /*******************/
    if ((err = BM_POOL_FillBufs (p_BmPool, p_BmPool->h_BmPortal, p_BmPool->numOfBuffers)) != E_OK)
    {
        BM_POOL_Free(p_BmPool);
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    return E_OK;
}

t_Error BM_POOL_Free(t_Handle h_BmPool)
{
    SANITY_CHECK_RETURN_ERROR(h_BmPool, E_INVALID_HANDLE);

    return BmPoolFree(h_BmPool, FALSE);
}

t_Error  BM_POOL_ConfigBpid(t_Handle h_BmPool, uint8_t bpid)
{
    t_BmPool            *p_BmPool   = (t_BmPool *)h_BmPool;

    SANITY_CHECK_RETURN_ERROR(p_BmPool, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_BmPool->p_BmPoolDriverParams, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(bpid < BM_MAX_NUM_OF_POOLS, E_INVALID_VALUE);

    p_BmPool->p_BmPoolDriverParams->dynamicBpid = FALSE;
    p_BmPool->bpid = bpid;

    return E_OK;
}


t_Error  BM_POOL_ConfigDepletion(t_Handle h_BmPool, t_BmDepletionCallback *f_Depletion, uint32_t *p_Thresholds)
{
    t_BmPool            *p_BmPool   = (t_BmPool *)h_BmPool;

    SANITY_CHECK_RETURN_ERROR(p_BmPool, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_BmPool->p_BmPoolDriverParams, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(f_Depletion, E_INVALID_HANDLE);

    p_BmPool->p_BmPoolDriverParams->useDepletion = TRUE;
    p_BmPool->f_Depletion = f_Depletion;
    memcpy(&p_BmPool->p_BmPoolDriverParams->depletionThresholds,
           p_Thresholds,
           sizeof(p_BmPool->p_BmPoolDriverParams->depletionThresholds));

    return E_OK;
}

t_Error  BM_POOL_ConfigStockpile(t_Handle h_BmPool, uint16_t maxBuffers, uint16_t minBuffers)
{
    t_BmPool            *p_BmPool   = (t_BmPool *)h_BmPool;

    SANITY_CHECK_RETURN_ERROR(p_BmPool, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_BmPool->p_BmPoolDriverParams, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(maxBuffers, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(maxBuffers >= minBuffers, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR((p_BmPool->shadowMode ||
                              ((maxBuffers * 2) <= p_BmPool->numOfBuffers)),
                              E_INVALID_STATE);

    p_BmPool->p_BmPoolDriverParams->useStockpile = TRUE;
    p_BmPool->spMaxBufs     = maxBuffers;
    p_BmPool->spMinBufs     = minBuffers;
    p_BmPool->spBufsCmd     = DEFAULT_numOfBufsPerCmd;

    SANITY_CHECK_RETURN_ERROR((p_BmPool->spMaxBufs >=
                               (p_BmPool->spMinBufs + p_BmPool->spBufsCmd)),
                              E_INVALID_STATE);

    return E_OK;
}

t_Error  BM_POOL_ConfigBuffContextMode(t_Handle h_BmPool, bool en)
{
    t_BmPool            *p_BmPool   = (t_BmPool *)h_BmPool;

    SANITY_CHECK_RETURN_ERROR(p_BmPool, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_BmPool->p_BmPoolDriverParams, E_INVALID_HANDLE);

    p_BmPool->noBuffCtxt = !en;

    return E_OK;
}

void * BM_POOL_GetBuf(t_Handle h_BmPool, t_Handle h_BmPortal)
{
    t_BmPool            *p_BmPool   = (t_BmPool *)h_BmPool;
    struct bm_buffer    bufs[1];
    uint8_t             retBufsNum;
    uint64_t            physAddr;
    uint32_t            flags = 0;

    SANITY_CHECK_RETURN_VALUE(p_BmPool, E_INVALID_HANDLE, NULL);
    SANITY_CHECK_RETURN_VALUE(!p_BmPool->p_BmPoolDriverParams, E_INVALID_HANDLE, NULL);
    SANITY_CHECK_RETURN_VALUE(p_BmPool->bufferPoolInfo.f_PhysToVirt, E_INVALID_STATE, NULL);

    if (!h_BmPortal)
    {
        if (p_BmPool->h_BmPortal)
            h_BmPortal = p_BmPool->h_BmPortal;
        else
        {
            SANITY_CHECK_RETURN_VALUE(p_BmPool->h_Bm, E_INVALID_HANDLE, NULL);
            h_BmPortal = BmGetPortalHandle(p_BmPool->h_Bm);
            SANITY_CHECK_RETURN_VALUE(h_BmPortal, E_INVALID_HANDLE, NULL);
        }
    }

    retBufsNum = (uint8_t)BmPoolAcquire(p_BmPool, h_BmPortal, bufs, 1, flags);
    if (!retBufsNum)
    {
        REPORT_ERROR(TRACE, E_NOT_AVAILABLE, ("buffer"));
        return NULL;
    }
    physAddr  = (uint64_t)bufs[0].lo;
    physAddr |= (uint64_t)(((uint64_t)bufs[0].hi << 32) & 0x000000ff00000000LL);
    DBG(TRACE,("Get Buffer : poolId %d, address 0x%016llx",
               p_BmPool->bpid,
               p_BmPool->bufferPoolInfo.f_PhysToVirt((physAddress_t)physAddr)));

    return p_BmPool->bufferPoolInfo.f_PhysToVirt((physAddress_t)physAddr);
}

t_Error BM_POOL_PutBuf(t_Handle h_BmPool, t_Handle h_BmPortal, void *p_Buff)
{
    t_BmPool            *p_BmPool   = (t_BmPool *)h_BmPool;
    uint64_t            physAddress;
    struct bm_buffer    bufs[1];

    SANITY_CHECK_RETURN_ERROR(p_BmPool, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_BmPool->p_BmPoolDriverParams, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(p_Buff, E_NULL_POINTER);

    physAddress = (uint64_t)(XX_VirtToPhys(p_Buff));

    bufs[0].bpid = p_BmPool->bpid;
    bufs[0].hi = (uint8_t)((physAddress & 0x000000ff00000000LL) >> 32);
    bufs[0].lo = (uint32_t)(physAddress & 0xffffffff);

    DBG(TRACE,("Put Buffer : poolId %d, address 0x%016llx, phys 0x%016llx",
               p_BmPool->bpid, (uint64_t)PTR_TO_UINT(p_Buff), physAddress));

    if (!h_BmPortal)
    {
        if (p_BmPool->h_BmPortal)
            h_BmPortal = p_BmPool->h_BmPortal;
        else
        {
            SANITY_CHECK_RETURN_ERROR(p_BmPool->h_Bm, E_INVALID_HANDLE);
            h_BmPortal = BmGetPortalHandle(p_BmPool->h_Bm);
            SANITY_CHECK_RETURN_ERROR(h_BmPortal, E_INVALID_HANDLE);
        }
    }

    return BmPoolRelease(p_BmPool, h_BmPortal, bufs, 1, BMAN_RELEASE_FLAG_WAIT);
}

t_Error BM_POOL_FillBufs(t_Handle h_BmPool, t_Handle h_BmPortal, uint32_t numBufs)
{
    t_BmPool    *p_BmPool   = (t_BmPool *)h_BmPool;

    SANITY_CHECK_RETURN_ERROR(p_BmPool, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_BmPool->p_BmPoolDriverParams, E_INVALID_STATE);

    if (!h_BmPortal)
    {
        if (p_BmPool->h_BmPortal)
            h_BmPortal = p_BmPool->h_BmPortal;
        else
        {
            SANITY_CHECK_RETURN_ERROR(p_BmPool->h_Bm, E_INVALID_HANDLE);
            h_BmPortal = BmGetPortalHandle(p_BmPool->h_Bm);
            SANITY_CHECK_RETURN_ERROR(h_BmPortal, E_INVALID_HANDLE);
        }
    }

    while (numBufs--)
    {
        uint8_t             *p_Data;
        t_Error             res;
        t_Handle            h_BufContext;

        p_Data = p_BmPool->bufferPoolInfo.f_GetBuf(p_BmPool->bufferPoolInfo.h_BufferPool, &h_BufContext);
        if(!p_Data)
            RETURN_ERROR(MAJOR, E_INVALID_STATE, ("run-out of buffers for bpid %d",p_BmPool->bpid));

        if (!p_BmPool->noBuffCtxt)
            *(t_Handle *)(p_Data - sizeof(t_Handle)) = h_BufContext;

        if ((res = BM_POOL_PutBuf(p_BmPool, h_BmPortal, p_Data)) != E_OK)
            RETURN_ERROR(CRITICAL, res, ("Seeding reserved buffer pool failed"));
    }

    return E_OK;
}

uint8_t BM_POOL_GetId(t_Handle h_BmPool)
{
    t_BmPool            *p_BmPool   = (t_BmPool *)h_BmPool;

    SANITY_CHECK_RETURN_VALUE(p_BmPool, E_INVALID_HANDLE, 0);
    SANITY_CHECK_RETURN_VALUE(!p_BmPool->p_BmPoolDriverParams, E_INVALID_HANDLE, 0);

    return p_BmPool->bpid;
}

uint16_t BM_POOL_GetBufferSize(t_Handle h_BmPool)
{
    t_BmPool            *p_BmPool   = (t_BmPool *)h_BmPool;

    SANITY_CHECK_RETURN_VALUE(p_BmPool, E_INVALID_HANDLE, 0);
    SANITY_CHECK_RETURN_VALUE(!p_BmPool->p_BmPoolDriverParams, E_INVALID_HANDLE, 0);

    return p_BmPool->bufferPoolInfo.bufferSize;
}

t_Handle BM_POOL_GetBufferContext(t_Handle h_BmPool, void *p_Buff)
{
    t_BmPool            *p_BmPool   = (t_BmPool *)h_BmPool;

    SANITY_CHECK_RETURN_VALUE(p_BmPool, E_INVALID_HANDLE, NULL);
    SANITY_CHECK_RETURN_VALUE(!p_BmPool->p_BmPoolDriverParams, E_INVALID_HANDLE, NULL);
    SANITY_CHECK_RETURN_VALUE(p_Buff, E_NULL_POINTER, NULL);

    if (p_BmPool->noBuffCtxt)
        return NULL;

    return *(t_Handle *)PTR_MOVE(p_Buff, -(sizeof(t_Handle)));
}

uint32_t BM_POOL_GetCounter(t_Handle h_BmPool, e_BmPoolCounters counter)
{
    t_BmPool            *p_BmPool   = (t_BmPool *)h_BmPool;

    SANITY_CHECK_RETURN_VALUE(p_BmPool, E_INVALID_HANDLE, 0);
    SANITY_CHECK_RETURN_VALUE(!p_BmPool->p_BmPoolDriverParams, E_INVALID_HANDLE, 0);

    switch(counter)
    {
        case(e_BM_POOL_COUNTERS_CONTENT):
            return BmGetCounter(p_BmPool->h_Bm, e_BM_IM_COUNTERS_POOL_CONTENT, p_BmPool->bpid);
        case(e_BM_POOL_COUNTERS_SW_DEPLETION):
            return (p_BmPool->swDepletionCount +=
                BmGetCounter(p_BmPool->h_Bm, e_BM_IM_COUNTERS_POOL_SW_DEPLETION, p_BmPool->bpid));
        case(e_BM_POOL_COUNTERS_HW_DEPLETION):
            return (p_BmPool->hwDepletionCount +=
                BmGetCounter(p_BmPool->h_Bm, e_BM_IM_COUNTERS_POOL_HW_DEPLETION, p_BmPool->bpid));
        default:
            break;
    }

    /* should never get here */
    ASSERT_COND(FALSE);

    return 0;
}
