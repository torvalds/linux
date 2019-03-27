/*
 * Copyright 2008-2012 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


/******************************************************************************
 @File          fm_sp.c

 @Description   FM PCD Storage profile  ...
*//***************************************************************************/

#include "std_ext.h"
#include "error_ext.h"
#include "string_ext.h"
#include "debug_ext.h"
#include "net_ext.h"

#include "fm_vsp_ext.h"
#include "fm_sp.h"
#include "fm_common.h"
#include "fsl_fman_sp.h"


#if (DPAA_VERSION >= 11)
static t_Error CheckParamsGeneratedInternally(t_FmVspEntry *p_FmVspEntry)
{
    t_Error err = E_OK;

    if ((err = FmSpCheckIntContextParams(&p_FmVspEntry->intContext))!= E_OK)
        RETURN_ERROR(MAJOR, err, NO_MSG);
    if ((err =  FmSpCheckBufMargins(&p_FmVspEntry->bufMargins)) != E_OK)
        RETURN_ERROR(MAJOR, err, NO_MSG);
    return err;

}

static t_Error CheckParams(t_FmVspEntry *p_FmVspEntry)
{
    t_Error err = E_OK;

    SANITY_CHECK_RETURN_ERROR(p_FmVspEntry, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmVspEntry->p_FmVspEntryDriverParams, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmVspEntry->h_Fm, E_INVALID_HANDLE);

    if ((err = FmSpCheckBufPoolsParams(&p_FmVspEntry->p_FmVspEntryDriverParams->extBufPools,
                                        p_FmVspEntry->p_FmVspEntryDriverParams->p_BackupBmPools,
                                        p_FmVspEntry->p_FmVspEntryDriverParams->p_BufPoolDepletion)) != E_OK)

        RETURN_ERROR(MAJOR, err, NO_MSG);

    if (p_FmVspEntry->p_FmVspEntryDriverParams->liodnOffset & ~FM_LIODN_OFFSET_MASK)
         RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("liodnOffset is larger than %d", FM_LIODN_OFFSET_MASK+1));

    err = FmVSPCheckRelativeProfile(p_FmVspEntry->h_Fm,
                                    p_FmVspEntry->portType,
                                    p_FmVspEntry->portId,
                                    p_FmVspEntry->relativeProfileId);

    return err;
}
#endif /* (DPAA_VERSION >= 11) */


/*****************************************************************************/
/*              Inter-module API routines                                    */
/*****************************************************************************/
void FmSpSetBufPoolsInAscOrderOfBufSizes(t_FmExtPools   *p_FmExtPools,
                                         uint8_t        *orderedArray,
                                         uint16_t       *sizesArray)
{
    uint16_t                    bufSize = 0;
    int                         i=0, j=0, k=0;

    /* First we copy the external buffers pools information to an ordered local array */
    for (i=0;i<p_FmExtPools->numOfPoolsUsed;i++)
    {
        /* get pool size */
        bufSize = p_FmExtPools->extBufPool[i].size;

        /* keep sizes in an array according to poolId for direct access */
        sizesArray[p_FmExtPools->extBufPool[i].id] =  bufSize;

        /* save poolId in an ordered array according to size */
        for (j=0;j<=i;j++)
        {
            /* this is the next free place in the array */
            if (j==i)
                orderedArray[i] = p_FmExtPools->extBufPool[i].id;
            else
            {
                /* find the right place for this poolId */
                if (bufSize < sizesArray[orderedArray[j]])
                {
                    /* move the poolIds one place ahead to make room for this poolId */
                    for (k=i;k>j;k--)
                       orderedArray[k] = orderedArray[k-1];

                    /* now k==j, this is the place for the new size */
                    orderedArray[k] = p_FmExtPools->extBufPool[i].id;
                    break;
                }
            }
        }
    }
}

t_Error FmSpCheckBufPoolsParams(t_FmExtPools            *p_FmExtPools,
                                t_FmBackupBmPools       *p_FmBackupBmPools,
                                t_FmBufPoolDepletion    *p_FmBufPoolDepletion)
{

    int         i = 0, j = 0;
    bool        found;
    uint8_t     count = 0;

    if (p_FmExtPools)
    {
        if (p_FmExtPools->numOfPoolsUsed > FM_PORT_MAX_NUM_OF_EXT_POOLS)
                RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("numOfPoolsUsed can't be larger than %d", FM_PORT_MAX_NUM_OF_EXT_POOLS));

        for (i=0;i<p_FmExtPools->numOfPoolsUsed;i++)
        {
            if (p_FmExtPools->extBufPool[i].id >= BM_MAX_NUM_OF_POOLS)
                RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("extBufPools.extBufPool[%d].id can't be larger than %d", i, BM_MAX_NUM_OF_POOLS));
            if (!p_FmExtPools->extBufPool[i].size)
                RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("extBufPools.extBufPool[%d].size is 0", i));
        }
    }
    if (!p_FmExtPools && (p_FmBackupBmPools || p_FmBufPoolDepletion))
          RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("backupBmPools ot bufPoolDepletion can not be defined without external pools"));

    /* backup BM pools indication is valid only for some chip derivatives
       (limited by the config routine) */
    if (p_FmBackupBmPools)
    {
        if (p_FmBackupBmPools->numOfBackupPools >= p_FmExtPools->numOfPoolsUsed)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("p_BackupBmPools must be smaller than extBufPools.numOfPoolsUsed"));
        found = FALSE;
        for (i = 0;i<p_FmBackupBmPools->numOfBackupPools;i++)
        {

            for (j=0;j<p_FmExtPools->numOfPoolsUsed;j++)
            {
                if (p_FmBackupBmPools->poolIds[i] == p_FmExtPools->extBufPool[j].id)
                {
                    found = TRUE;
                    break;
                }
            }
            if (!found)
                RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("All p_BackupBmPools.poolIds must be included in extBufPools.extBufPool[n].id"));
            else
                found = FALSE;
        }
    }

    /* up to extBufPools.numOfPoolsUsed pools may be defined */
    if (p_FmBufPoolDepletion && p_FmBufPoolDepletion->poolsGrpModeEnable)
    {
        if ((p_FmBufPoolDepletion->numOfPools > p_FmExtPools->numOfPoolsUsed))
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("bufPoolDepletion.numOfPools can't be larger than %d and can't be larger than numOfPoolsUsed", FM_PORT_MAX_NUM_OF_EXT_POOLS));

        if (!p_FmBufPoolDepletion->numOfPools)
          RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("bufPoolDepletion.numOfPoolsToConsider can not be 0 when poolsGrpModeEnable=TRUE"));

        found = FALSE;
        count = 0;
        /* for each pool that is in poolsToConsider, check if it is defined
           in extBufPool */
        for (i=0;i<BM_MAX_NUM_OF_POOLS;i++)
        {
            if (p_FmBufPoolDepletion->poolsToConsider[i])
            {
                for (j=0;j<p_FmExtPools->numOfPoolsUsed;j++)
                 {
                    if (i == p_FmExtPools->extBufPool[j].id)
                    {
                        found = TRUE;
                        count++;
                        break;
                    }
                 }
                if (!found)
                    RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Pools selected for depletion are not used."));
                else
                    found = FALSE;
            }
        }
        /* check that the number of pools that we have checked is equal to the number announced by the user */
        if (count != p_FmBufPoolDepletion->numOfPools)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("bufPoolDepletion.numOfPools is larger than the number of pools defined."));
    }

    if (p_FmBufPoolDepletion && p_FmBufPoolDepletion->singlePoolModeEnable)
    {
        /* calculate vector for number of pools depletion */
        found = FALSE;
        count = 0;
        for (i=0;i<BM_MAX_NUM_OF_POOLS;i++)
        {
            if (p_FmBufPoolDepletion->poolsToConsiderForSingleMode[i])
            {
                for (j=0;j<p_FmExtPools->numOfPoolsUsed;j++)
                {
                    if (i == p_FmExtPools->extBufPool[j].id)
                    {
                        found = TRUE;
                        count++;
                        break;
                    }
                }
                if (!found)
                    RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Pools selected for depletion are not used."));
                else
                    found = FALSE;
            }
        }
        if (!count)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("No pools defined for single buffer mode pool depletion."));
    }

    return E_OK;
}

t_Error FmSpCheckIntContextParams(t_FmSpIntContextDataCopy *p_FmSpIntContextDataCopy)
{
    /* Check that divisible by 16 and not larger than 240 */
    if (p_FmSpIntContextDataCopy->intContextOffset >MAX_INT_OFFSET)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("intContext.intContextOffset can't be larger than %d", MAX_INT_OFFSET));
    if (p_FmSpIntContextDataCopy->intContextOffset % OFFSET_UNITS)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("intContext.intContextOffset has to be divisible by %d", OFFSET_UNITS));

    /* check that ic size+ic internal offset, does not exceed ic block size */
    if (p_FmSpIntContextDataCopy->size + p_FmSpIntContextDataCopy->intContextOffset > MAX_IC_SIZE)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("intContext.size + intContext.intContextOffset has to be smaller than %d", MAX_IC_SIZE));
    /* Check that divisible by 16 and not larger than 256 */
    if (p_FmSpIntContextDataCopy->size % OFFSET_UNITS)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("intContext.size  has to be divisible by %d", OFFSET_UNITS));

    /* Check that divisible by 16 and not larger than 4K */
    if (p_FmSpIntContextDataCopy->extBufOffset > MAX_EXT_OFFSET)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("intContext.extBufOffset can't be larger than %d", MAX_EXT_OFFSET));
    if (p_FmSpIntContextDataCopy->extBufOffset % OFFSET_UNITS)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("intContext.extBufOffset  has to be divisible by %d", OFFSET_UNITS));

    return E_OK;
}

t_Error FmSpCheckBufMargins(t_FmSpBufMargins *p_FmSpBufMargins)
{
    /* Check the margin definition */
    if (p_FmSpBufMargins->startMargins > MAX_EXT_BUFFER_OFFSET)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("bufMargins.startMargins can't be larger than %d", MAX_EXT_BUFFER_OFFSET));
    if (p_FmSpBufMargins->endMargins > MAX_EXT_BUFFER_OFFSET)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("bufMargins.endMargins can't be larger than %d", MAX_EXT_BUFFER_OFFSET));

    return E_OK;
}

t_Error FmSpBuildBufferStructure(t_FmSpIntContextDataCopy   *p_FmSpIntContextDataCopy,
                                 t_FmBufferPrefixContent     *p_BufferPrefixContent,
                                 t_FmSpBufMargins            *p_FmSpBufMargins,
                                 t_FmSpBufferOffsets         *p_FmSpBufferOffsets,
                                 uint8_t                     *internalBufferOffset)
{
    uint32_t                        tmp;

    SANITY_CHECK_RETURN_ERROR(p_FmSpIntContextDataCopy,  E_INVALID_VALUE);
    ASSERT_COND(p_FmSpIntContextDataCopy);
    ASSERT_COND(p_BufferPrefixContent);
    ASSERT_COND(p_FmSpBufMargins);
    ASSERT_COND(p_FmSpBufferOffsets);

    /* Align start of internal context data to 16 byte */
    p_FmSpIntContextDataCopy->extBufOffset =
        (uint16_t)((p_BufferPrefixContent->privDataSize & (OFFSET_UNITS-1)) ?
            ((p_BufferPrefixContent->privDataSize + OFFSET_UNITS) & ~(uint16_t)(OFFSET_UNITS-1)) :
             p_BufferPrefixContent->privDataSize);

    /* Translate margin and intContext params to FM parameters */
    /* Initialize with illegal value. Later we'll set legal values. */
    p_FmSpBufferOffsets->prsResultOffset = (uint32_t)ILLEGAL_BASE;
    p_FmSpBufferOffsets->timeStampOffset = (uint32_t)ILLEGAL_BASE;
    p_FmSpBufferOffsets->hashResultOffset= (uint32_t)ILLEGAL_BASE;
    p_FmSpBufferOffsets->pcdInfoOffset   = (uint32_t)ILLEGAL_BASE;

    /* Internally the driver supports 4 options
       1. prsResult/timestamp/hashResult selection (in fact 8 options, but for simplicity we'll
          relate to it as 1).
       2. All IC context (from AD) not including debug.*/

    /* This 'if' covers option 2. We copy from beginning of context. */
    if (p_BufferPrefixContent->passAllOtherPCDInfo)
    {
        p_FmSpIntContextDataCopy->size = 128; /* must be aligned to 16 */
        /* Start copying data after 16 bytes (FD) from the beginning of the internal context */
        p_FmSpIntContextDataCopy->intContextOffset = 16;

        if (p_BufferPrefixContent->passAllOtherPCDInfo)
            p_FmSpBufferOffsets->pcdInfoOffset = p_FmSpIntContextDataCopy->extBufOffset;
        if (p_BufferPrefixContent->passPrsResult)
            p_FmSpBufferOffsets->prsResultOffset =
                (uint32_t)(p_FmSpIntContextDataCopy->extBufOffset + 16);
        if (p_BufferPrefixContent->passTimeStamp)
            p_FmSpBufferOffsets->timeStampOffset =
                (uint32_t)(p_FmSpIntContextDataCopy->extBufOffset + 48);
        if (p_BufferPrefixContent->passHashResult)
            p_FmSpBufferOffsets->hashResultOffset =
                (uint32_t)(p_FmSpIntContextDataCopy->extBufOffset + 56);
    }
    else
    {
        /* This case covers the options under 1 */
        /* Copy size must be in 16-byte granularity. */
        p_FmSpIntContextDataCopy->size =
            (uint16_t)((p_BufferPrefixContent->passPrsResult ? 32 : 0) +
                      ((p_BufferPrefixContent->passTimeStamp ||
                      p_BufferPrefixContent->passHashResult) ? 16 : 0));

        /* Align start of internal context data to 16 byte */
        p_FmSpIntContextDataCopy->intContextOffset =
            (uint8_t)(p_BufferPrefixContent->passPrsResult ? 32 :
                      ((p_BufferPrefixContent->passTimeStamp  ||
                       p_BufferPrefixContent->passHashResult) ? 64 : 0));

        if (p_BufferPrefixContent->passPrsResult)
            p_FmSpBufferOffsets->prsResultOffset = p_FmSpIntContextDataCopy->extBufOffset;
        if (p_BufferPrefixContent->passTimeStamp)
            p_FmSpBufferOffsets->timeStampOffset =  p_BufferPrefixContent->passPrsResult ?
                                        (p_FmSpIntContextDataCopy->extBufOffset + sizeof(t_FmPrsResult)) :
                                        p_FmSpIntContextDataCopy->extBufOffset;
        if (p_BufferPrefixContent->passHashResult)
            /* If PR is not requested, whether TS is requested or not, IC will be copied from TS */
            p_FmSpBufferOffsets->hashResultOffset = p_BufferPrefixContent->passPrsResult ?
                                          (p_FmSpIntContextDataCopy->extBufOffset + sizeof(t_FmPrsResult) + 8) :
                                          p_FmSpIntContextDataCopy->extBufOffset + 8;
    }

    if (p_FmSpIntContextDataCopy->size)
        p_FmSpBufMargins->startMargins =
            (uint16_t)(p_FmSpIntContextDataCopy->extBufOffset +
                       p_FmSpIntContextDataCopy->size);
    else
        /* No Internal Context passing, STartMargin is immediately after privateInfo */
        p_FmSpBufMargins->startMargins = p_BufferPrefixContent->privDataSize;

    /* save extra space for manip in both external and internal buffers */
    if (p_BufferPrefixContent->manipExtraSpace)
    {
        uint8_t extraSpace;
#ifdef FM_CAPWAP_SUPPORT
        if ((p_BufferPrefixContent->manipExtraSpace + CAPWAP_FRAG_EXTRA_SPACE) >= 256)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE,
                         ("p_BufferPrefixContent->manipExtraSpace should be less than %d",
                          256-CAPWAP_FRAG_EXTRA_SPACE));
        extraSpace = (uint8_t)(p_BufferPrefixContent->manipExtraSpace + CAPWAP_FRAG_EXTRA_SPACE);
#else
        extraSpace = p_BufferPrefixContent->manipExtraSpace;
#endif /* FM_CAPWAP_SUPPORT */
        p_FmSpBufferOffsets->manipOffset = p_FmSpBufMargins->startMargins;
        p_FmSpBufMargins->startMargins += extraSpace;
        *internalBufferOffset = extraSpace;
    }

    /* align data start */
    tmp = (uint32_t)(p_FmSpBufMargins->startMargins % p_BufferPrefixContent->dataAlign);
    if (tmp)
        p_FmSpBufMargins->startMargins += (p_BufferPrefixContent->dataAlign-tmp);
    p_FmSpBufferOffsets->dataOffset = p_FmSpBufMargins->startMargins;

    return E_OK;
}
/*********************** End of inter-module routines ************************/


#if (DPAA_VERSION >= 11)
/*****************************************************************************/
/*              API routines                                                 */
/*****************************************************************************/
t_Handle FM_VSP_Config(t_FmVspParams *p_FmVspParams)
{
    t_FmVspEntry          *p_FmVspEntry = NULL;
    struct fm_storage_profile_params   fm_vsp_params;

    p_FmVspEntry = (t_FmVspEntry *)XX_Malloc(sizeof(t_FmVspEntry));
    if (!p_FmVspEntry)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("p_StorageProfile allocation failed"));
        return NULL;
    }
    memset(p_FmVspEntry, 0, sizeof(t_FmVspEntry));

    p_FmVspEntry->p_FmVspEntryDriverParams = (t_FmVspEntryDriverParams *)XX_Malloc(sizeof(t_FmVspEntryDriverParams));
    if (!p_FmVspEntry->p_FmVspEntryDriverParams)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("p_StorageProfile allocation failed"));
        XX_Free(p_FmVspEntry);
        return NULL;
    }
    memset(p_FmVspEntry->p_FmVspEntryDriverParams, 0, sizeof(t_FmVspEntryDriverParams));
    fman_vsp_defconfig(&fm_vsp_params);
    p_FmVspEntry->p_FmVspEntryDriverParams->dmaHeaderCacheAttr = fm_vsp_params.header_cache_attr;
    p_FmVspEntry->p_FmVspEntryDriverParams->dmaIntContextCacheAttr = fm_vsp_params.int_context_cache_attr;
    p_FmVspEntry->p_FmVspEntryDriverParams->dmaScatterGatherCacheAttr = fm_vsp_params.scatter_gather_cache_attr;
    p_FmVspEntry->p_FmVspEntryDriverParams->dmaSwapData = fm_vsp_params.dma_swap_data;
    p_FmVspEntry->p_FmVspEntryDriverParams->dmaWriteOptimize = fm_vsp_params.dma_write_optimize;
    p_FmVspEntry->p_FmVspEntryDriverParams->noScatherGather = fm_vsp_params.no_scather_gather;
    p_FmVspEntry->p_FmVspEntryDriverParams->bufferPrefixContent.privDataSize = DEFAULT_FM_SP_bufferPrefixContent_privDataSize;
    p_FmVspEntry->p_FmVspEntryDriverParams->bufferPrefixContent.passPrsResult= DEFAULT_FM_SP_bufferPrefixContent_passPrsResult;
    p_FmVspEntry->p_FmVspEntryDriverParams->bufferPrefixContent.passTimeStamp= DEFAULT_FM_SP_bufferPrefixContent_passTimeStamp;
    p_FmVspEntry->p_FmVspEntryDriverParams->bufferPrefixContent.passAllOtherPCDInfo
                                                                    = DEFAULT_FM_SP_bufferPrefixContent_passTimeStamp;
    p_FmVspEntry->p_FmVspEntryDriverParams->bufferPrefixContent.dataAlign    = DEFAULT_FM_SP_bufferPrefixContent_dataAlign;
    p_FmVspEntry->p_FmVspEntryDriverParams->liodnOffset                      = p_FmVspParams->liodnOffset;

    memcpy(&p_FmVspEntry->p_FmVspEntryDriverParams->extBufPools, &p_FmVspParams->extBufPools, sizeof(t_FmExtPools));
    p_FmVspEntry->h_Fm                                                       = p_FmVspParams->h_Fm;
    p_FmVspEntry->portType                                                   = p_FmVspParams->portParams.portType;
    p_FmVspEntry->portId                                                     = p_FmVspParams->portParams.portId;

    p_FmVspEntry->relativeProfileId                                          = p_FmVspParams->relativeProfileId;

   return p_FmVspEntry;
}

t_Error FM_VSP_Init(t_Handle h_FmVsp)
{

    t_FmVspEntry                *p_FmVspEntry = (t_FmVspEntry *)h_FmVsp;
    struct fm_storage_profile_params   fm_vsp_params;
    uint8_t                     orderedArray[FM_PORT_MAX_NUM_OF_EXT_POOLS];
    uint16_t                    sizesArray[BM_MAX_NUM_OF_POOLS];
    t_Error                     err;
    uint16_t                    absoluteProfileId = 0;
    int                         i = 0;

    SANITY_CHECK_RETURN_ERROR(p_FmVspEntry, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmVspEntry->p_FmVspEntryDriverParams,E_INVALID_HANDLE);

    CHECK_INIT_PARAMETERS(p_FmVspEntry, CheckParams);

    memset(&orderedArray, 0, sizeof(uint8_t) * FM_PORT_MAX_NUM_OF_EXT_POOLS);
    memset(&sizesArray, 0, sizeof(uint16_t) * BM_MAX_NUM_OF_POOLS);

    err = FmSpBuildBufferStructure(&p_FmVspEntry->intContext,
                                   &p_FmVspEntry->p_FmVspEntryDriverParams->bufferPrefixContent,
                                   &p_FmVspEntry->bufMargins,
                                   &p_FmVspEntry->bufferOffsets,
                                   &p_FmVspEntry->internalBufferOffset);
    if (err != E_OK)
        RETURN_ERROR(MAJOR, err, NO_MSG);


    err = CheckParamsGeneratedInternally(p_FmVspEntry);
    if (err != E_OK)
        RETURN_ERROR(MAJOR, err, NO_MSG);


     p_FmVspEntry->p_FmSpRegsBase =
        (struct fm_pcd_storage_profile_regs *)FmGetVSPBaseAddr(p_FmVspEntry->h_Fm);
    if (!p_FmVspEntry->p_FmSpRegsBase)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("impossible to initialize SpRegsBase"));

    /* order external buffer pools in ascending order of buffer pools sizes */
    FmSpSetBufPoolsInAscOrderOfBufSizes(&(p_FmVspEntry->p_FmVspEntryDriverParams)->extBufPools,
                                        orderedArray,
                                        sizesArray);

    p_FmVspEntry->extBufPools.numOfPoolsUsed =
        p_FmVspEntry->p_FmVspEntryDriverParams->extBufPools.numOfPoolsUsed;
    for (i = 0; i < p_FmVspEntry->extBufPools.numOfPoolsUsed; i++)
    {
       p_FmVspEntry->extBufPools.extBufPool[i].id = orderedArray[i];
       p_FmVspEntry->extBufPools.extBufPool[i].size = sizesArray[orderedArray[i]];
    }

    /* on user responsibility to fill it according requirement */
    memset(&fm_vsp_params, 0, sizeof(struct fm_storage_profile_params));
    fm_vsp_params.dma_swap_data              = p_FmVspEntry->p_FmVspEntryDriverParams->dmaSwapData;
    fm_vsp_params.int_context_cache_attr     = p_FmVspEntry->p_FmVspEntryDriverParams->dmaIntContextCacheAttr;
    fm_vsp_params.header_cache_attr          = p_FmVspEntry->p_FmVspEntryDriverParams->dmaHeaderCacheAttr;
    fm_vsp_params.scatter_gather_cache_attr  = p_FmVspEntry->p_FmVspEntryDriverParams->dmaScatterGatherCacheAttr;
    fm_vsp_params.dma_write_optimize         = p_FmVspEntry->p_FmVspEntryDriverParams->dmaWriteOptimize;
    fm_vsp_params.liodn_offset               = p_FmVspEntry->p_FmVspEntryDriverParams->liodnOffset;
    fm_vsp_params.no_scather_gather          = p_FmVspEntry->p_FmVspEntryDriverParams->noScatherGather;

    if (p_FmVspEntry->p_FmVspEntryDriverParams->p_BufPoolDepletion)
    {
        fm_vsp_params.buf_pool_depletion.buf_pool_depletion_enabled = TRUE;
        fm_vsp_params.buf_pool_depletion.pools_grp_mode_enable = p_FmVspEntry->p_FmVspEntryDriverParams->p_BufPoolDepletion->poolsGrpModeEnable;
        fm_vsp_params.buf_pool_depletion.num_pools = p_FmVspEntry->p_FmVspEntryDriverParams->p_BufPoolDepletion->numOfPools;
        fm_vsp_params.buf_pool_depletion.pools_to_consider = p_FmVspEntry->p_FmVspEntryDriverParams->p_BufPoolDepletion->poolsToConsider;
        fm_vsp_params.buf_pool_depletion.single_pool_mode_enable = p_FmVspEntry->p_FmVspEntryDriverParams->p_BufPoolDepletion->singlePoolModeEnable;
        fm_vsp_params.buf_pool_depletion.pools_to_consider_for_single_mode = p_FmVspEntry->p_FmVspEntryDriverParams->p_BufPoolDepletion->poolsToConsiderForSingleMode;
        fm_vsp_params.buf_pool_depletion.has_pfc_priorities = TRUE;
        fm_vsp_params.buf_pool_depletion.pfc_priorities_en = p_FmVspEntry->p_FmVspEntryDriverParams->p_BufPoolDepletion->pfcPrioritiesEn;
    }
    else
        fm_vsp_params.buf_pool_depletion.buf_pool_depletion_enabled = FALSE;
 
    if (p_FmVspEntry->p_FmVspEntryDriverParams->p_BackupBmPools)
    {
        fm_vsp_params.backup_pools.num_backup_pools = p_FmVspEntry->p_FmVspEntryDriverParams->p_BackupBmPools->numOfBackupPools;
        fm_vsp_params.backup_pools.pool_ids = p_FmVspEntry->p_FmVspEntryDriverParams->p_BackupBmPools->poolIds;
    }
    else
        fm_vsp_params.backup_pools.num_backup_pools = 0;

    fm_vsp_params.fm_ext_pools.num_pools_used = p_FmVspEntry->extBufPools.numOfPoolsUsed;
    fm_vsp_params.fm_ext_pools.ext_buf_pool = (struct fman_ext_pool_params*)&p_FmVspEntry->extBufPools.extBufPool;
    fm_vsp_params.buf_margins = (struct fman_sp_buf_margins*)&p_FmVspEntry->bufMargins;
    fm_vsp_params.int_context = (struct fman_sp_int_context_data_copy*)&p_FmVspEntry->intContext;

    /* no check on err - it was checked earlier */
    FmVSPGetAbsoluteProfileId(p_FmVspEntry->h_Fm,
                              p_FmVspEntry->portType,
                              p_FmVspEntry->portId,
                              p_FmVspEntry->relativeProfileId,
                              &absoluteProfileId);

    ASSERT_COND(p_FmVspEntry->p_FmSpRegsBase);
    ASSERT_COND(fm_vsp_params.int_context);
    ASSERT_COND(fm_vsp_params.buf_margins);
    ASSERT_COND((absoluteProfileId <= FM_VSP_MAX_NUM_OF_ENTRIES));

    /* Set all registers related to VSP */
    fman_vsp_init(p_FmVspEntry->p_FmSpRegsBase, absoluteProfileId, &fm_vsp_params,FM_PORT_MAX_NUM_OF_EXT_POOLS, BM_MAX_NUM_OF_POOLS, FM_MAX_NUM_OF_PFC_PRIORITIES);

    p_FmVspEntry->absoluteSpId = absoluteProfileId;

    if (p_FmVspEntry->p_FmVspEntryDriverParams)
        XX_Free(p_FmVspEntry->p_FmVspEntryDriverParams);
    p_FmVspEntry->p_FmVspEntryDriverParams = NULL;

    return E_OK;
}

t_Error FM_VSP_Free(t_Handle h_FmVsp)
{
    t_FmVspEntry   *p_FmVspEntry = (t_FmVspEntry *)h_FmVsp;
    SANITY_CHECK_RETURN_ERROR(h_FmVsp, E_INVALID_HANDLE);
    XX_Free(p_FmVspEntry);
    return E_OK;
}

t_Error FM_VSP_ConfigBufferPrefixContent(t_Handle h_FmVsp, t_FmBufferPrefixContent *p_FmBufferPrefixContent)
{
    t_FmVspEntry *p_FmVspEntry = (t_FmVspEntry*)h_FmVsp;

    SANITY_CHECK_RETURN_ERROR(p_FmVspEntry, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmVspEntry->p_FmVspEntryDriverParams, E_INVALID_HANDLE);

    memcpy(&p_FmVspEntry->p_FmVspEntryDriverParams->bufferPrefixContent, p_FmBufferPrefixContent, sizeof(t_FmBufferPrefixContent));
    /* if dataAlign was not initialized by user, we return to driver's default */
    if (!p_FmVspEntry->p_FmVspEntryDriverParams->bufferPrefixContent.dataAlign)
        p_FmVspEntry->p_FmVspEntryDriverParams->bufferPrefixContent.dataAlign = DEFAULT_FM_SP_bufferPrefixContent_dataAlign;

    return E_OK;
}

t_Error FM_VSP_ConfigDmaSwapData(t_Handle h_FmVsp, e_FmDmaSwapOption swapData)
{
    t_FmVspEntry *p_FmVspEntry = (t_FmVspEntry*)h_FmVsp;

    SANITY_CHECK_RETURN_ERROR(p_FmVspEntry, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmVspEntry->p_FmVspEntryDriverParams, E_INVALID_HANDLE);

    p_FmVspEntry->p_FmVspEntryDriverParams->dmaSwapData = swapData;

    return E_OK;
}

t_Error FM_VSP_ConfigDmaIcCacheAttr(t_Handle h_FmVsp, e_FmDmaCacheOption intContextCacheAttr)
{
    t_FmVspEntry *p_FmVspEntry = (t_FmVspEntry*)h_FmVsp;

    SANITY_CHECK_RETURN_ERROR(p_FmVspEntry, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmVspEntry->p_FmVspEntryDriverParams, E_INVALID_HANDLE);

    p_FmVspEntry->p_FmVspEntryDriverParams->dmaIntContextCacheAttr = intContextCacheAttr;

    return E_OK;
}

t_Error FM_VSP_ConfigDmaHdrAttr(t_Handle h_FmVsp, e_FmDmaCacheOption headerCacheAttr)
{
    t_FmVspEntry *p_FmVspEntry = (t_FmVspEntry*)h_FmVsp;

    SANITY_CHECK_RETURN_ERROR(p_FmVspEntry, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmVspEntry->p_FmVspEntryDriverParams, E_INVALID_HANDLE);

    p_FmVspEntry->p_FmVspEntryDriverParams->dmaHeaderCacheAttr = headerCacheAttr;

    return E_OK;
}

t_Error FM_VSP_ConfigDmaScatterGatherAttr(t_Handle h_FmVsp, e_FmDmaCacheOption scatterGatherCacheAttr)
{
    t_FmVspEntry *p_FmVspEntry = (t_FmVspEntry*)h_FmVsp;

    SANITY_CHECK_RETURN_ERROR(p_FmVspEntry, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmVspEntry->p_FmVspEntryDriverParams, E_INVALID_HANDLE);

     p_FmVspEntry->p_FmVspEntryDriverParams->dmaScatterGatherCacheAttr = scatterGatherCacheAttr;

    return E_OK;
}

t_Error FM_VSP_ConfigDmaWriteOptimize(t_Handle h_FmVsp, bool optimize)
{
    t_FmVspEntry *p_FmVspEntry = (t_FmVspEntry*)h_FmVsp;

    SANITY_CHECK_RETURN_ERROR(p_FmVspEntry, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmVspEntry->p_FmVspEntryDriverParams, E_INVALID_HANDLE);


    p_FmVspEntry->p_FmVspEntryDriverParams->dmaWriteOptimize = optimize;

    return E_OK;
}

t_Error FM_VSP_ConfigNoScatherGather(t_Handle h_FmVsp, bool noScatherGather)
{
    t_FmVspEntry *p_FmVspEntry = (t_FmVspEntry*)h_FmVsp;

    SANITY_CHECK_RETURN_ERROR(p_FmVspEntry, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmVspEntry->p_FmVspEntryDriverParams, E_INVALID_HANDLE);


    p_FmVspEntry->p_FmVspEntryDriverParams->noScatherGather = noScatherGather;

    return E_OK;
}

t_Error FM_VSP_ConfigPoolDepletion(t_Handle h_FmVsp, t_FmBufPoolDepletion *p_BufPoolDepletion)
{
    t_FmVspEntry *p_FmVspEntry = (t_FmVspEntry*)h_FmVsp;

    SANITY_CHECK_RETURN_ERROR(h_FmVsp, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmVspEntry->p_FmVspEntryDriverParams, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_BufPoolDepletion, E_INVALID_HANDLE);

    p_FmVspEntry->p_FmVspEntryDriverParams->p_BufPoolDepletion = (t_FmBufPoolDepletion *)XX_Malloc(sizeof(t_FmBufPoolDepletion));
    if (!p_FmVspEntry->p_FmVspEntryDriverParams->p_BufPoolDepletion)
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("p_BufPoolDepletion allocation failed"));
    memcpy(p_FmVspEntry->p_FmVspEntryDriverParams->p_BufPoolDepletion, p_BufPoolDepletion, sizeof(t_FmBufPoolDepletion));

    return E_OK;
}

t_Error FM_VSP_ConfigBackupPools(t_Handle h_FmVsp, t_FmBackupBmPools *p_BackupBmPools)
{
    t_FmVspEntry *p_FmVspEntry = (t_FmVspEntry*)h_FmVsp;

    SANITY_CHECK_RETURN_ERROR(h_FmVsp, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmVspEntry->p_FmVspEntryDriverParams, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_BackupBmPools, E_INVALID_HANDLE);

    p_FmVspEntry->p_FmVspEntryDriverParams->p_BackupBmPools = (t_FmBackupBmPools *)XX_Malloc(sizeof(t_FmBackupBmPools));
    if (!p_FmVspEntry->p_FmVspEntryDriverParams->p_BackupBmPools)
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("p_BackupBmPools allocation failed"));
    memcpy(p_FmVspEntry->p_FmVspEntryDriverParams->p_BackupBmPools, p_BackupBmPools, sizeof(t_FmBackupBmPools));

    return E_OK;
}

uint32_t FM_VSP_GetBufferDataOffset(t_Handle h_FmVsp)
{
    t_FmVspEntry *p_FmVspEntry = (t_FmVspEntry*)h_FmVsp;

    SANITY_CHECK_RETURN_VALUE(p_FmVspEntry, E_INVALID_HANDLE, 0);
    SANITY_CHECK_RETURN_VALUE(!p_FmVspEntry->p_FmVspEntryDriverParams, E_INVALID_STATE, 0);

    return p_FmVspEntry->bufferOffsets.dataOffset;
}

uint8_t * FM_VSP_GetBufferICInfo(t_Handle h_FmVsp, char *p_Data)
{
    t_FmVspEntry *p_FmVspEntry = (t_FmVspEntry*)h_FmVsp;

    SANITY_CHECK_RETURN_VALUE(p_FmVspEntry, E_INVALID_HANDLE, NULL);
    SANITY_CHECK_RETURN_VALUE(!p_FmVspEntry->p_FmVspEntryDriverParams, E_INVALID_STATE, NULL);

    if (p_FmVspEntry->bufferOffsets.pcdInfoOffset == ILLEGAL_BASE)
        return NULL;

    return (uint8_t *)PTR_MOVE(p_Data, p_FmVspEntry->bufferOffsets.pcdInfoOffset);
}

t_FmPrsResult * FM_VSP_GetBufferPrsResult(t_Handle h_FmVsp, char *p_Data)
{
    t_FmVspEntry *p_FmVspEntry = (t_FmVspEntry*)h_FmVsp;

    SANITY_CHECK_RETURN_VALUE(p_FmVspEntry, E_INVALID_HANDLE, NULL);
    SANITY_CHECK_RETURN_VALUE(!p_FmVspEntry->p_FmVspEntryDriverParams, E_INVALID_STATE, NULL);

    if (p_FmVspEntry->bufferOffsets.prsResultOffset == ILLEGAL_BASE)
        return NULL;

    return (t_FmPrsResult *)PTR_MOVE(p_Data, p_FmVspEntry->bufferOffsets.prsResultOffset);
}

uint64_t * FM_VSP_GetBufferTimeStamp(t_Handle h_FmVsp, char *p_Data)
{
    t_FmVspEntry *p_FmVspEntry = (t_FmVspEntry*)h_FmVsp;

    SANITY_CHECK_RETURN_VALUE(p_FmVspEntry, E_INVALID_HANDLE, NULL);
    SANITY_CHECK_RETURN_VALUE(!p_FmVspEntry->p_FmVspEntryDriverParams, E_INVALID_STATE, NULL);

    if (p_FmVspEntry->bufferOffsets.timeStampOffset == ILLEGAL_BASE)
        return NULL;

    return (uint64_t *)PTR_MOVE(p_Data, p_FmVspEntry->bufferOffsets.timeStampOffset);
}

uint8_t * FM_VSP_GetBufferHashResult(t_Handle h_FmVsp, char *p_Data)
{
    t_FmVspEntry *p_FmVspEntry = (t_FmVspEntry*)h_FmVsp;

    SANITY_CHECK_RETURN_VALUE(p_FmVspEntry, E_INVALID_HANDLE, NULL);
    SANITY_CHECK_RETURN_VALUE(!p_FmVspEntry->p_FmVspEntryDriverParams, E_INVALID_STATE, NULL);

    if (p_FmVspEntry->bufferOffsets.hashResultOffset == ILLEGAL_BASE)
        return NULL;

    return (uint8_t *)PTR_MOVE(p_Data, p_FmVspEntry->bufferOffsets.hashResultOffset);
}

#endif /* (DPAA_VERSION >= 11) */
