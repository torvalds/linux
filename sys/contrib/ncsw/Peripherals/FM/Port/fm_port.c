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
 @File          fm_port.c

 @Description   FM driver routines implementation.
 *//***************************************************************************/
#include "error_ext.h"
#include "std_ext.h"
#include "string_ext.h"
#include "sprint_ext.h"
#include "debug_ext.h"
#include "fm_muram_ext.h"

#include "fman_common.h"
#include "fm_port.h"
#include "fm_port_dsar.h"
#include "common/general.h"

/****************************************/
/*       static functions               */
/****************************************/
static t_Error FmPortConfigAutoResForDeepSleepSupport1(t_FmPort *p_FmPort);

static t_Error CheckInitParameters(t_FmPort *p_FmPort)
{
    t_FmPortDriverParam *p_Params = p_FmPort->p_FmPortDriverParam;
    struct fman_port_cfg *p_DfltConfig = &p_Params->dfltCfg;
    t_Error ans = E_OK;
    uint32_t unusedMask;

    if (p_FmPort->imEn)
    {
        if (p_FmPort->portType == e_FM_PORT_TYPE_RX_10G)
            if (p_FmPort->p_FmPortDriverParam->dfltCfg.tx_fifo_deq_pipeline_depth
                    > 2)
                RETURN_ERROR(
                        MAJOR,
                        E_INVALID_VALUE,
                        ("fifoDeqPipelineDepth for IM 10G can't be larger than 2"));

        if ((ans = FmPortImCheckInitParameters(p_FmPort)) != E_OK)
            return ERROR_CODE(ans);
    }
    else
    {
        /****************************************/
        /*   Rx only                            */
        /****************************************/
        if ((p_FmPort->portType == e_FM_PORT_TYPE_RX)
                || (p_FmPort->portType == e_FM_PORT_TYPE_RX_10G))
        {
            /* external buffer pools */
            if (!p_Params->extBufPools.numOfPoolsUsed)
                RETURN_ERROR(
                        MAJOR,
                        E_INVALID_VALUE,
                        ("extBufPools.numOfPoolsUsed=0. At least one buffer pool must be defined"));

            if (FmSpCheckBufPoolsParams(&p_Params->extBufPools,
                                        p_Params->p_BackupBmPools,
                                        &p_Params->bufPoolDepletion) != E_OK)
                RETURN_ERROR(MAJOR, E_INVALID_VALUE, NO_MSG);

            /* Check that part of IC that needs copying is small enough to enter start margin */
            if (p_Params->intContext.size
                    && (p_Params->intContext.size
                            + p_Params->intContext.extBufOffset
                            > p_Params->bufMargins.startMargins))
                RETURN_ERROR(MAJOR, E_INVALID_VALUE,
                             ("intContext.size is larger than start margins"));

            if ((p_Params->liodnOffset != (uint16_t)DPAA_LIODN_DONT_OVERRIDE)
                    && (p_Params->liodnOffset & ~FM_LIODN_OFFSET_MASK))
                RETURN_ERROR(
                        MAJOR,
                        E_INVALID_VALUE,
                        ("liodnOffset is larger than %d", FM_LIODN_OFFSET_MASK+1));

#ifdef FM_NO_BACKUP_POOLS
            if ((p_FmPort->fmRevInfo.majorRev != 4) && (p_FmPort->fmRevInfo.majorRev < 6))
            if (p_FmPort->p_FmPortDriverParam->p_BackupBmPools)
            RETURN_ERROR(MAJOR, E_NOT_SUPPORTED, ("BackupBmPools"));
#endif /* FM_NO_BACKUP_POOLS */
        }

        /****************************************/
        /*   Non Rx ports                       */
        /****************************************/
        else
        {
            if (p_Params->deqSubPortal >= FM_MAX_NUM_OF_SUB_PORTALS)
                RETURN_ERROR(
                        MAJOR,
                        E_INVALID_VALUE,
                        (" deqSubPortal has to be in the range of 0 - %d", FM_MAX_NUM_OF_SUB_PORTALS));

            /* to protect HW internal-context from overwrite */
            if ((p_Params->intContext.size)
                    && (p_Params->intContext.intContextOffset
                            < MIN_TX_INT_OFFSET))
                RETURN_ERROR(
                        MAJOR,
                        E_INVALID_VALUE,
                        ("non-Rx intContext.intContextOffset can't be smaller than %d", MIN_TX_INT_OFFSET));

            if ((p_FmPort->portType == e_FM_PORT_TYPE_TX)
                    || (p_FmPort->portType == e_FM_PORT_TYPE_TX_10G)
                    /* in O/H DEFAULT_notSupported indicates that it is not supported and should not be checked */
                    || (p_FmPort->p_FmPortDriverParam->dfltCfg.tx_fifo_deq_pipeline_depth
                            != DEFAULT_notSupported))
            {
                /* Check that not larger than 8 */
                if ((!p_FmPort->p_FmPortDriverParam->dfltCfg.tx_fifo_deq_pipeline_depth)
                        || (p_FmPort->p_FmPortDriverParam->dfltCfg.tx_fifo_deq_pipeline_depth
                                > MAX_FIFO_PIPELINE_DEPTH))
                    RETURN_ERROR(
                            MAJOR,
                            E_INVALID_VALUE,
                            ("fifoDeqPipelineDepth can't be larger than %d", MAX_FIFO_PIPELINE_DEPTH));
            }
        }

        /****************************************/
        /*   Rx Or Offline Parsing              */
        /****************************************/
        if ((p_FmPort->portType == e_FM_PORT_TYPE_RX)
                || (p_FmPort->portType == e_FM_PORT_TYPE_RX_10G)
                || (p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING))
        {
            if (!p_Params->dfltFqid)
                RETURN_ERROR(MAJOR, E_INVALID_VALUE,
                             ("dfltFqid must be between 1 and 2^24-1"));
#if defined(FM_CAPWAP_SUPPORT) && defined(FM_LOCKUP_ALIGNMENT_ERRATA_FMAN_SW004)
            if (p_FmPort->p_FmPortDriverParam->bufferPrefixContent.manipExtraSpace % 16)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("bufferPrefixContent.manipExtraSpace has to be devidable by 16"));
#endif /* defined(FM_CAPWAP_SUPPORT) && ... */
        }

        /****************************************/
        /*   All ports                          */
        /****************************************/
        /* common BMI registers values */
        /* Check that Queue Id is not larger than 2^24, and is not 0 */
        if ((p_Params->errFqid & ~0x00FFFFFF) || !p_Params->errFqid)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE,
                         ("errFqid must be between 1 and 2^24-1"));
        if (p_Params->dfltFqid & ~0x00FFFFFF)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE,
                         ("dfltFqid must be between 1 and 2^24-1"));
    }

    /****************************************/
    /*   Rx only                            */
    /****************************************/
    if ((p_FmPort->portType == e_FM_PORT_TYPE_RX)
            || (p_FmPort->portType == e_FM_PORT_TYPE_RX_10G))
    {
        if (p_DfltConfig->rx_pri_elevation % BMI_FIFO_UNITS)
            RETURN_ERROR(
                    MAJOR,
                    E_INVALID_VALUE,
                    ("rxFifoPriElevationLevel has to be divisible by %d", BMI_FIFO_UNITS));
        if ((p_DfltConfig->rx_pri_elevation < BMI_FIFO_UNITS)
                || (p_DfltConfig->rx_pri_elevation > MAX_PORT_FIFO_SIZE))
            RETURN_ERROR(
                    MAJOR,
                    E_INVALID_VALUE,
                    ("rxFifoPriElevationLevel has to be in the range of 256 - %d", MAX_PORT_FIFO_SIZE));
        if (p_DfltConfig->rx_fifo_thr % BMI_FIFO_UNITS)
            RETURN_ERROR(
                    MAJOR,
                    E_INVALID_VALUE,
                    ("rxFifoThreshold has to be divisible by %d", BMI_FIFO_UNITS));
        if ((p_DfltConfig->rx_fifo_thr < BMI_FIFO_UNITS)
                || (p_DfltConfig->rx_fifo_thr > MAX_PORT_FIFO_SIZE))
            RETURN_ERROR(
                    MAJOR,
                    E_INVALID_VALUE,
                    ("rxFifoThreshold has to be in the range of 256 - %d", MAX_PORT_FIFO_SIZE));

        /* Check that not larger than 16 */
        if (p_DfltConfig->rx_cut_end_bytes > FRAME_END_DATA_SIZE)
            RETURN_ERROR(
                    MAJOR,
                    E_INVALID_VALUE,
                    ("cutBytesFromEnd can't be larger than %d", FRAME_END_DATA_SIZE));

        if (FmSpCheckBufMargins(&p_Params->bufMargins) != E_OK)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, NO_MSG);

        /* extra FIFO size (allowed only to Rx ports) */
        if (p_Params->setSizeOfFifo
                && (p_FmPort->fifoBufs.extra % BMI_FIFO_UNITS))
            RETURN_ERROR(
                    MAJOR,
                    E_INVALID_VALUE,
                    ("fifoBufs.extra has to be divisible by %d", BMI_FIFO_UNITS));

        if (p_Params->bufPoolDepletion.poolsGrpModeEnable
                && !p_Params->bufPoolDepletion.numOfPools)
            RETURN_ERROR(
                    MAJOR,
                    E_INVALID_VALUE,
                    ("bufPoolDepletion.numOfPools can not be 0 when poolsGrpModeEnable=TRUE"));
#ifdef FM_CSI_CFED_LIMIT
        if (p_FmPort->fmRevInfo.majorRev == 4)
        {
            /* Check that not larger than 16 */
            if (p_DfltConfig->rx_cut_end_bytes + p_DfltConfig->checksum_bytes_ignore > FRAME_END_DATA_SIZE)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("cheksumLastBytesIgnore + cutBytesFromEnd can't be larger than %d", FRAME_END_DATA_SIZE));
        }
#endif /* FM_CSI_CFED_LIMIT */
    }

    /****************************************/
    /*   Non Rx ports                       */
    /****************************************/
    /* extra FIFO size (allowed only to Rx ports) */
    else
        if (p_FmPort->fifoBufs.extra)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE,
                         (" No fifoBufs.extra for non Rx ports"));

    /****************************************/
    /*   Tx only                            */
    /****************************************/
    if ((p_FmPort->portType == e_FM_PORT_TYPE_TX)
            || (p_FmPort->portType == e_FM_PORT_TYPE_TX_10G))
    {
        if (p_DfltConfig->tx_fifo_min_level % BMI_FIFO_UNITS)
            RETURN_ERROR(
                    MAJOR,
                    E_INVALID_VALUE,
                    ("txFifoMinFillLevel has to be divisible by %d", BMI_FIFO_UNITS));
        if (p_DfltConfig->tx_fifo_min_level > (MAX_PORT_FIFO_SIZE - 256))
            RETURN_ERROR(
                    MAJOR,
                    E_INVALID_VALUE,
                    ("txFifoMinFillLevel has to be in the range of 0 - %d", (MAX_PORT_FIFO_SIZE - 256)));
        if (p_DfltConfig->tx_fifo_low_comf_level % BMI_FIFO_UNITS)
            RETURN_ERROR(
                    MAJOR,
                    E_INVALID_VALUE,
                    ("txFifoLowComfLevel has to be divisible by %d", BMI_FIFO_UNITS));
        if ((p_DfltConfig->tx_fifo_low_comf_level < BMI_FIFO_UNITS)
                || (p_DfltConfig->tx_fifo_low_comf_level > MAX_PORT_FIFO_SIZE))
            RETURN_ERROR(
                    MAJOR,
                    E_INVALID_VALUE,
                    ("txFifoLowComfLevel has to be in the range of 256 - %d", MAX_PORT_FIFO_SIZE));

        if (p_FmPort->portType == e_FM_PORT_TYPE_TX)
            if (p_FmPort->p_FmPortDriverParam->dfltCfg.tx_fifo_deq_pipeline_depth
                    > 2)
                RETURN_ERROR(
                        MAJOR, E_INVALID_VALUE,
                        ("fifoDeqPipelineDepth for 1G can't be larger than 2"));
    }

    /****************************************/
    /*   Non Tx Ports                       */
    /****************************************/
    /* If discard override was selected , no frames may be discarded. */
    else
        if (p_DfltConfig->discard_override && p_Params->errorsToDiscard)
            RETURN_ERROR(
                    MAJOR,
                    E_CONFLICT,
                    ("errorsToDiscard is not empty, but frmDiscardOverride selected (all discarded frames to be enqueued to error queue)."));

    /****************************************/
    /*   Rx and Offline parsing             */
    /****************************************/
    if ((p_FmPort->portType == e_FM_PORT_TYPE_RX)
            || (p_FmPort->portType == e_FM_PORT_TYPE_RX_10G)
            || (p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING))
    {
        if (p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING)
            unusedMask = BMI_STATUS_OP_MASK_UNUSED;
        else
            unusedMask = BMI_STATUS_RX_MASK_UNUSED;

        /* Check that no common bits with BMI_STATUS_MASK_UNUSED */
        if (p_Params->errorsToDiscard & unusedMask)
            RETURN_ERROR(MAJOR, E_INVALID_SELECTION,
                         ("errorsToDiscard contains undefined bits"));
    }

    /****************************************/
    /*   Offline Ports                      */
    /****************************************/
#ifdef FM_OP_OPEN_DMA_MIN_LIMIT
    if ((p_FmPort->fmRevInfo.majorRev >= 6)
            && (p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING)
            && p_Params->setNumOfOpenDmas
            && (p_FmPort->openDmas.num < MIN_NUM_OF_OP_DMAS))
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("For Offline port, openDmas.num can't be smaller than %d", MIN_NUM_OF_OP_DMAS));
#endif /* FM_OP_OPEN_DMA_MIN_LIMIT */

    /****************************************/
    /*   Offline & HC Ports                 */
    /****************************************/
    if ((p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING)
            || (p_FmPort->portType == e_FM_PORT_TYPE_OH_HOST_COMMAND))
    {
#ifndef FM_FRAME_END_PARAMS_FOR_OP
        if ((p_FmPort->fmRevInfo.majorRev < 6) &&
                (p_FmPort->p_FmPortDriverParam->cheksumLastBytesIgnore != DEFAULT_notSupported))
        /* this is an indication that user called config for this mode which is not supported in this integration */
        RETURN_ERROR(MAJOR, E_NOT_SUPPORTED, ("cheksumLastBytesIgnore is available for Rx & Tx ports only"));
#endif /* !FM_FRAME_END_PARAMS_FOR_OP */

#ifndef FM_DEQ_PIPELINE_PARAMS_FOR_OP
        if ((!((p_FmPort->fmRevInfo.majorRev == 4) ||
                                (p_FmPort->fmRevInfo.majorRev >= 6))) &&
                (p_FmPort->p_FmPortDriverParam->dfltCfg.tx_fifo_deq_pipeline_depth != DEFAULT_notSupported))
        /* this is an indication that user called config for this mode which is not supported in this integration */
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION, ("fifoDeqPipelineDepth is available for Tx ports only"));
#endif /* !FM_DEQ_PIPELINE_PARAMS_FOR_OP */
    }

    /****************************************/
    /*   All ports                          */
    /****************************************/
    /* Check that not larger than 16 */
    if ((p_Params->cheksumLastBytesIgnore > FRAME_END_DATA_SIZE)
            && ((p_Params->cheksumLastBytesIgnore != DEFAULT_notSupported)))
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("cheksumLastBytesIgnore can't be larger than %d", FRAME_END_DATA_SIZE));

    if (FmSpCheckIntContextParams(&p_Params->intContext) != E_OK)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, NO_MSG);

    /* common BMI registers values */
    if (p_Params->setNumOfTasks
            && ((!p_FmPort->tasks.num)
                    || (p_FmPort->tasks.num > MAX_NUM_OF_TASKS)))
        RETURN_ERROR(MAJOR, E_INVALID_VALUE,
                     ("tasks.num can't be larger than %d", MAX_NUM_OF_TASKS));
    if (p_Params->setNumOfTasks
            && (p_FmPort->tasks.extra > MAX_NUM_OF_EXTRA_TASKS))
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("tasks.extra can't be larger than %d", MAX_NUM_OF_EXTRA_TASKS));
    if (p_Params->setNumOfOpenDmas
            && ((!p_FmPort->openDmas.num)
                    || (p_FmPort->openDmas.num > MAX_NUM_OF_DMAS)))
        RETURN_ERROR(MAJOR, E_INVALID_VALUE,
                     ("openDmas.num can't be larger than %d", MAX_NUM_OF_DMAS));
    if (p_Params->setNumOfOpenDmas
            && (p_FmPort->openDmas.extra > MAX_NUM_OF_EXTRA_DMAS))
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("openDmas.extra can't be larger than %d", MAX_NUM_OF_EXTRA_DMAS));
    if (p_Params->setSizeOfFifo
            && (!p_FmPort->fifoBufs.num
                    || (p_FmPort->fifoBufs.num > MAX_PORT_FIFO_SIZE)))
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("fifoBufs.num has to be in the range of 256 - %d", MAX_PORT_FIFO_SIZE));
    if (p_Params->setSizeOfFifo && (p_FmPort->fifoBufs.num % BMI_FIFO_UNITS))
        RETURN_ERROR(
                MAJOR, E_INVALID_VALUE,
                ("fifoBufs.num has to be divisible by %d", BMI_FIFO_UNITS));

#ifdef FM_QMI_NO_DEQ_OPTIONS_SUPPORT
    if (p_FmPort->fmRevInfo.majorRev == 4)
    if (p_FmPort->p_FmPortDriverParam->deqPrefetchOption != DEFAULT_notSupported)
    /* this is an indication that user called config for this mode which is not supported in this integration */
    RETURN_ERROR(MAJOR, E_INVALID_OPERATION, ("deqPrefetchOption"));
#endif /* FM_QMI_NO_DEQ_OPTIONS_SUPPORT */

    return E_OK;
}

static t_Error VerifySizeOfFifo(t_FmPort *p_FmPort)
{
    uint32_t minFifoSizeRequired = 0, optFifoSizeForB2B = 0;

    /*************************/
    /*    TX PORTS           */
    /*************************/
    if ((p_FmPort->portType == e_FM_PORT_TYPE_TX)
            || (p_FmPort->portType == e_FM_PORT_TYPE_TX_10G))
    {
        minFifoSizeRequired =
                (uint32_t)(ROUND_UP(p_FmPort->maxFrameLength, BMI_FIFO_UNITS)
                        + (3 * BMI_FIFO_UNITS));
        if (!p_FmPort->imEn)
            minFifoSizeRequired +=
                    p_FmPort->p_FmPortDriverParam->dfltCfg.tx_fifo_deq_pipeline_depth
                            * BMI_FIFO_UNITS;

        optFifoSizeForB2B = minFifoSizeRequired;

        /* Add some margin for back-to-back capability to improve performance,
         allows the hardware to pipeline new frame dma while the previous
         frame not yet transmitted. */
        if (p_FmPort->portType == e_FM_PORT_TYPE_TX_10G)
            optFifoSizeForB2B += 3 * BMI_FIFO_UNITS;
        else
            optFifoSizeForB2B += 2 * BMI_FIFO_UNITS;
    }

    /*************************/
    /*    RX IM PORTS        */
    /*************************/
    else
        if (((p_FmPort->portType == e_FM_PORT_TYPE_RX)
                || (p_FmPort->portType == e_FM_PORT_TYPE_RX_10G))
                && p_FmPort->imEn)
        {
            optFifoSizeForB2B =
                    minFifoSizeRequired =
                            (uint32_t)(ROUND_UP(p_FmPort->maxFrameLength, BMI_FIFO_UNITS)
                                    + (4 * BMI_FIFO_UNITS));
        }

        /*************************/
        /*    RX non-IM PORTS    */
        /*************************/
        else
            if (((p_FmPort->portType == e_FM_PORT_TYPE_RX)
                    || (p_FmPort->portType == e_FM_PORT_TYPE_RX_10G))
                    && !p_FmPort->imEn)
            {
                if (p_FmPort->fmRevInfo.majorRev == 4)
                {
                    if (p_FmPort->rxPoolsParams.numOfPools == 1)
                        minFifoSizeRequired = 8 * BMI_FIFO_UNITS;
                    else
                        minFifoSizeRequired =
                                (uint32_t)(ROUND_UP(p_FmPort->rxPoolsParams.secondLargestBufSize, BMI_FIFO_UNITS)
                                        + (7 * BMI_FIFO_UNITS));
                }
                else
                {
#if (DPAA_VERSION >= 11)
                    minFifoSizeRequired =
                            (uint32_t)(ROUND_UP(p_FmPort->maxFrameLength, BMI_FIFO_UNITS)
                                    + (5 * BMI_FIFO_UNITS));
                    /* 4 according to spec + 1 for FOF>0 */
#else
                    minFifoSizeRequired = (uint32_t)
                    (ROUND_UP(MIN(p_FmPort->maxFrameLength, p_FmPort->rxPoolsParams.largestBufSize), BMI_FIFO_UNITS)
                            + (7*BMI_FIFO_UNITS));
#endif /* (DPAA_VERSION >= 11) */
                }

                optFifoSizeForB2B = minFifoSizeRequired;

                /* Add some margin for back-to-back capability to improve performance,
                 allows the hardware to pipeline new frame dma while the previous
                 frame not yet transmitted. */
                if (p_FmPort->portType == e_FM_PORT_TYPE_RX_10G)
                    optFifoSizeForB2B += 8 * BMI_FIFO_UNITS;
                else
                    optFifoSizeForB2B += 3 * BMI_FIFO_UNITS;
            }

            /* For O/H ports, check fifo size and update if necessary */
            else
                if ((p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING)
                        || (p_FmPort->portType == e_FM_PORT_TYPE_OH_HOST_COMMAND))
                {
#if (DPAA_VERSION >= 11)
                    optFifoSizeForB2B =
                            minFifoSizeRequired =
                                    (uint32_t)(ROUND_UP(p_FmPort->maxFrameLength, BMI_FIFO_UNITS)
                                            + ((p_FmPort->p_FmPortDriverParam->dfltCfg.tx_fifo_deq_pipeline_depth
                                                    + 5) * BMI_FIFO_UNITS));
                    /* 4 according to spec + 1 for FOF>0 */
#else
                    optFifoSizeForB2B = minFifoSizeRequired = (uint32_t)((p_FmPort->tasks.num + 2) * BMI_FIFO_UNITS);
#endif /* (DPAA_VERSION >= 11) */
                }

    ASSERT_COND(minFifoSizeRequired > 0);
    ASSERT_COND(optFifoSizeForB2B >= minFifoSizeRequired);

    /* Verify the size  */
    if (p_FmPort->fifoBufs.num < minFifoSizeRequired)
        DBG(INFO,
           ("FIFO size is %d and should be enlarged to %d bytes",p_FmPort->fifoBufs.num, minFifoSizeRequired));
    else if (p_FmPort->fifoBufs.num < optFifoSizeForB2B)
        DBG(INFO,
	    ("For back-to-back frames processing, FIFO size is %d and needs to enlarge to %d bytes", p_FmPort->fifoBufs.num, optFifoSizeForB2B));

    return E_OK;
}

static void FmPortDriverParamFree(t_FmPort *p_FmPort)
{
    if (p_FmPort->p_FmPortDriverParam)
    {
        XX_Free(p_FmPort->p_FmPortDriverParam);
        p_FmPort->p_FmPortDriverParam = NULL;
    }
}

static t_Error SetExtBufferPools(t_FmPort *p_FmPort)
{
    t_FmExtPools *p_ExtBufPools = &p_FmPort->p_FmPortDriverParam->extBufPools;
    t_FmBufPoolDepletion *p_BufPoolDepletion =
            &p_FmPort->p_FmPortDriverParam->bufPoolDepletion;
    uint8_t orderedArray[FM_PORT_MAX_NUM_OF_EXT_POOLS];
    uint16_t sizesArray[BM_MAX_NUM_OF_POOLS];
    int i = 0, j = 0, err;
    struct fman_port_bpools bpools;

    memset(&orderedArray, 0, sizeof(uint8_t) * FM_PORT_MAX_NUM_OF_EXT_POOLS);
    memset(&sizesArray, 0, sizeof(uint16_t) * BM_MAX_NUM_OF_POOLS);
    memcpy(&p_FmPort->extBufPools, p_ExtBufPools, sizeof(t_FmExtPools));

    FmSpSetBufPoolsInAscOrderOfBufSizes(p_ExtBufPools, orderedArray,
                                        sizesArray);

    /* Prepare flibs bpools structure */
    memset(&bpools, 0, sizeof(struct fman_port_bpools));
    bpools.count = p_ExtBufPools->numOfPoolsUsed;
    bpools.counters_enable = TRUE;
    for (i = 0; i < p_ExtBufPools->numOfPoolsUsed; i++)
    {
        bpools.bpool[i].bpid = orderedArray[i];
        bpools.bpool[i].size = sizesArray[orderedArray[i]];
        /* functionality available only for some derivatives (limited by config) */
        if (p_FmPort->p_FmPortDriverParam->p_BackupBmPools)
            for (j = 0;
                    j
                            < p_FmPort->p_FmPortDriverParam->p_BackupBmPools->numOfBackupPools;
                    j++)
                if (orderedArray[i]
                        == p_FmPort->p_FmPortDriverParam->p_BackupBmPools->poolIds[j])
                {
                    bpools.bpool[i].is_backup = TRUE;
                    break;
                }
    }

    /* save pools parameters for later use */
    p_FmPort->rxPoolsParams.numOfPools = p_ExtBufPools->numOfPoolsUsed;
    p_FmPort->rxPoolsParams.largestBufSize =
            sizesArray[orderedArray[p_ExtBufPools->numOfPoolsUsed - 1]];
    p_FmPort->rxPoolsParams.secondLargestBufSize =
            sizesArray[orderedArray[p_ExtBufPools->numOfPoolsUsed - 2]];

    /* FMBM_RMPD reg. - pool depletion */
    if (p_BufPoolDepletion->poolsGrpModeEnable)
    {
        bpools.grp_bp_depleted_num = p_BufPoolDepletion->numOfPools;
        for (i = 0; i < BM_MAX_NUM_OF_POOLS; i++)
        {
            if (p_BufPoolDepletion->poolsToConsider[i])
            {
                for (j = 0; j < p_ExtBufPools->numOfPoolsUsed; j++)
                {
                    if (i == orderedArray[j])
                    {
                        bpools.bpool[j].grp_bp_depleted = TRUE;
                        break;
                    }
                }
            }
        }
    }

    if (p_BufPoolDepletion->singlePoolModeEnable)
    {
        for (i = 0; i < BM_MAX_NUM_OF_POOLS; i++)
        {
            if (p_BufPoolDepletion->poolsToConsiderForSingleMode[i])
            {
                for (j = 0; j < p_ExtBufPools->numOfPoolsUsed; j++)
                {
                    if (i == orderedArray[j])
                    {
                        bpools.bpool[j].single_bp_depleted = TRUE;
                        break;
                    }
                }
            }
        }
    }

#if (DPAA_VERSION >= 11)
    /* fill QbbPEV */
    if (p_BufPoolDepletion->poolsGrpModeEnable
            || p_BufPoolDepletion->singlePoolModeEnable)
    {
        for (i = 0; i < FM_MAX_NUM_OF_PFC_PRIORITIES; i++)
        {
            if (p_BufPoolDepletion->pfcPrioritiesEn[i] == TRUE)
            {
                bpools.bpool[i].pfc_priorities_en = TRUE;
            }
        }
    }
#endif /* (DPAA_VERSION >= 11) */

    /* Issue flibs function */
    err = fman_port_set_bpools(&p_FmPort->port, &bpools);
    if (err != 0)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("fman_port_set_bpools"));

    if (p_FmPort->p_FmPortDriverParam->p_BackupBmPools)
        XX_Free(p_FmPort->p_FmPortDriverParam->p_BackupBmPools);

    return E_OK;
}

static t_Error ClearPerfCnts(t_FmPort *p_FmPort)
{
    if (p_FmPort->portType != e_FM_PORT_TYPE_OH_OFFLINE_PARSING)
        FM_PORT_ModifyCounter(p_FmPort, e_FM_PORT_COUNTERS_QUEUE_UTIL, 0);
    FM_PORT_ModifyCounter(p_FmPort, e_FM_PORT_COUNTERS_TASK_UTIL, 0);
    FM_PORT_ModifyCounter(p_FmPort, e_FM_PORT_COUNTERS_DMA_UTIL, 0);
    FM_PORT_ModifyCounter(p_FmPort, e_FM_PORT_COUNTERS_FIFO_UTIL, 0);
    return E_OK;
}

static t_Error InitLowLevelDriver(t_FmPort *p_FmPort)
{
    t_FmPortDriverParam *p_DriverParams = p_FmPort->p_FmPortDriverParam;
    struct fman_port_params portParams;
    uint32_t tmpVal;
    t_Error err;

    /* Set up flibs parameters and issue init function */

    memset(&portParams, 0, sizeof(struct fman_port_params));
    portParams.discard_mask = p_DriverParams->errorsToDiscard;
    portParams.dflt_fqid = p_DriverParams->dfltFqid;
    portParams.err_fqid = p_DriverParams->errFqid;
    portParams.deq_sp = p_DriverParams->deqSubPortal;
    portParams.dont_release_buf = p_DriverParams->dontReleaseBuf;
    switch (p_FmPort->portType)
    {
        case (e_FM_PORT_TYPE_RX_10G):
        case (e_FM_PORT_TYPE_RX):
            portParams.err_mask = (RX_ERRS_TO_ENQ & ~portParams.discard_mask);
            if (!p_FmPort->imEn)
            {
                if (p_DriverParams->forwardReuseIntContext)
                    p_DriverParams->dfltCfg.rx_fd_bits =
                            (uint8_t)(BMI_PORT_RFNE_FRWD_RPD >> 24);
            }
            break;

        case (e_FM_PORT_TYPE_OH_OFFLINE_PARSING):
            portParams.err_mask = (OP_ERRS_TO_ENQ & ~portParams.discard_mask);
            break;
            break;

        default:
            break;
    }

    tmpVal =
            (uint32_t)(
                    (p_FmPort->internalBufferOffset % OFFSET_UNITS) ? (p_FmPort->internalBufferOffset
                            / OFFSET_UNITS + 1) :
                            (p_FmPort->internalBufferOffset / OFFSET_UNITS));
    p_FmPort->internalBufferOffset = (uint8_t)(tmpVal * OFFSET_UNITS);
    p_DriverParams->dfltCfg.int_buf_start_margin =
            p_FmPort->internalBufferOffset;

    p_DriverParams->dfltCfg.ext_buf_start_margin =
            p_DriverParams->bufMargins.startMargins;
    p_DriverParams->dfltCfg.ext_buf_end_margin =
            p_DriverParams->bufMargins.endMargins;

    p_DriverParams->dfltCfg.ic_ext_offset =
            p_DriverParams->intContext.extBufOffset;
    p_DriverParams->dfltCfg.ic_int_offset =
            p_DriverParams->intContext.intContextOffset;
    p_DriverParams->dfltCfg.ic_size = p_DriverParams->intContext.size;

    p_DriverParams->dfltCfg.stats_counters_enable = TRUE;
    p_DriverParams->dfltCfg.perf_counters_enable = TRUE;
    p_DriverParams->dfltCfg.queue_counters_enable = TRUE;

    p_DriverParams->dfltCfg.perf_cnt_params.task_val =
            (uint8_t)p_FmPort->tasks.num;
    if (p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING ||
    p_FmPort->portType == e_FM_PORT_TYPE_OH_HOST_COMMAND)p_DriverParams->dfltCfg.perf_cnt_params.queue_val = 0;
    else
    p_DriverParams->dfltCfg.perf_cnt_params.queue_val = 1;
    p_DriverParams->dfltCfg.perf_cnt_params.dma_val =
            (uint8_t)p_FmPort->openDmas.num;
    p_DriverParams->dfltCfg.perf_cnt_params.fifo_val = p_FmPort->fifoBufs.num;

    if (0
            != fman_port_init(&p_FmPort->port, &p_DriverParams->dfltCfg,
                              &portParams))
        RETURN_ERROR(MAJOR, E_NO_DEVICE, ("fman_port_init"));

    if (p_FmPort->imEn && ((err = FmPortImInit(p_FmPort)) != E_OK))
        RETURN_ERROR(MAJOR, err, NO_MSG);
    else
    {
        //  from QMIInit
        if ((p_FmPort->portType != e_FM_PORT_TYPE_RX_10G)
                && (p_FmPort->portType != e_FM_PORT_TYPE_RX))
        {
            if (p_DriverParams->deqPrefetchOption == e_FM_PORT_DEQ_NO_PREFETCH)
                FmSetPortPreFetchConfiguration(p_FmPort->h_Fm, p_FmPort->portId,
                                               FALSE);
            else
                FmSetPortPreFetchConfiguration(p_FmPort->h_Fm, p_FmPort->portId,
                                               TRUE);
        }
    }
    /* The code bellow is a trick so the FM will not release the buffer
     to BM nor will try to enqueue the frame to QM */
    if (((p_FmPort->portType == e_FM_PORT_TYPE_TX_10G)
            || (p_FmPort->portType == e_FM_PORT_TYPE_TX)) && (!p_FmPort->imEn))
    {
        if (!p_DriverParams->dfltFqid && p_DriverParams->dontReleaseBuf)
        {
            /* override fmbm_tcfqid 0 with a false non-0 value. This will force FM to
             * act according to tfene. Otherwise, if fmbm_tcfqid is 0 the FM will release
             * buffers to BM regardless of fmbm_tfene
             */
            WRITE_UINT32(p_FmPort->port.bmi_regs->tx.fmbm_tcfqid, 0xFFFFFF);
            WRITE_UINT32(p_FmPort->port.bmi_regs->tx.fmbm_tfene,
                         NIA_ENG_BMI | NIA_BMI_AC_TX_RELEASE);
        }
    }

    return E_OK;
}

static bool CheckRxBmiCounter(t_FmPort *p_FmPort, e_FmPortCounters counter)
{
    UNUSED(p_FmPort);

    switch (counter)
    {
        case (e_FM_PORT_COUNTERS_CYCLE):
        case (e_FM_PORT_COUNTERS_TASK_UTIL):
        case (e_FM_PORT_COUNTERS_QUEUE_UTIL):
        case (e_FM_PORT_COUNTERS_DMA_UTIL):
        case (e_FM_PORT_COUNTERS_FIFO_UTIL):
        case (e_FM_PORT_COUNTERS_RX_PAUSE_ACTIVATION):
        case (e_FM_PORT_COUNTERS_FRAME):
        case (e_FM_PORT_COUNTERS_DISCARD_FRAME):
        case (e_FM_PORT_COUNTERS_RX_BAD_FRAME):
        case (e_FM_PORT_COUNTERS_RX_LARGE_FRAME):
        case (e_FM_PORT_COUNTERS_RX_FILTER_FRAME):
        case (e_FM_PORT_COUNTERS_RX_LIST_DMA_ERR):
        case (e_FM_PORT_COUNTERS_RX_OUT_OF_BUFFERS_DISCARD):
        case (e_FM_PORT_COUNTERS_DEALLOC_BUF):
        case (e_FM_PORT_COUNTERS_PREPARE_TO_ENQUEUE_COUNTER):
            return TRUE;
        default:
            return FALSE;
    }
}

static bool CheckTxBmiCounter(t_FmPort *p_FmPort, e_FmPortCounters counter)
{
    UNUSED(p_FmPort);

    switch (counter)
    {
        case (e_FM_PORT_COUNTERS_CYCLE):
        case (e_FM_PORT_COUNTERS_TASK_UTIL):
        case (e_FM_PORT_COUNTERS_QUEUE_UTIL):
        case (e_FM_PORT_COUNTERS_DMA_UTIL):
        case (e_FM_PORT_COUNTERS_FIFO_UTIL):
        case (e_FM_PORT_COUNTERS_FRAME):
        case (e_FM_PORT_COUNTERS_DISCARD_FRAME):
        case (e_FM_PORT_COUNTERS_LENGTH_ERR):
        case (e_FM_PORT_COUNTERS_UNSUPPRTED_FORMAT):
        case (e_FM_PORT_COUNTERS_DEALLOC_BUF):
            return TRUE;
        default:
            return FALSE;
    }
}

static bool CheckOhBmiCounter(t_FmPort *p_FmPort, e_FmPortCounters counter)
{
    switch (counter)
    {
        case (e_FM_PORT_COUNTERS_CYCLE):
        case (e_FM_PORT_COUNTERS_TASK_UTIL):
        case (e_FM_PORT_COUNTERS_DMA_UTIL):
        case (e_FM_PORT_COUNTERS_FIFO_UTIL):
        case (e_FM_PORT_COUNTERS_FRAME):
        case (e_FM_PORT_COUNTERS_DISCARD_FRAME):
        case (e_FM_PORT_COUNTERS_RX_LIST_DMA_ERR):
        case (e_FM_PORT_COUNTERS_WRED_DISCARD):
        case (e_FM_PORT_COUNTERS_LENGTH_ERR):
        case (e_FM_PORT_COUNTERS_UNSUPPRTED_FORMAT):
        case (e_FM_PORT_COUNTERS_DEALLOC_BUF):
            return TRUE;
        case (e_FM_PORT_COUNTERS_RX_FILTER_FRAME):
            if (p_FmPort->portType == e_FM_PORT_TYPE_OH_HOST_COMMAND)
                return FALSE;
            else
                return TRUE;
        default:
            return FALSE;
    }
}

static t_Error BmiPortCheckAndGetCounterType(
        t_FmPort *p_FmPort, e_FmPortCounters counter,
        enum fman_port_stats_counters *p_StatsType,
        enum fman_port_perf_counters *p_PerfType, bool *p_IsStats)
{
    volatile uint32_t *p_Reg;
    bool isValid;

    switch (p_FmPort->portType)
    {
        case (e_FM_PORT_TYPE_RX_10G):
        case (e_FM_PORT_TYPE_RX):
            p_Reg = &p_FmPort->port.bmi_regs->rx.fmbm_rstc;
            isValid = CheckRxBmiCounter(p_FmPort, counter);
            break;
        case (e_FM_PORT_TYPE_TX_10G):
        case (e_FM_PORT_TYPE_TX):
            p_Reg = &p_FmPort->port.bmi_regs->tx.fmbm_tstc;
            isValid = CheckTxBmiCounter(p_FmPort, counter);
            break;
        case (e_FM_PORT_TYPE_OH_OFFLINE_PARSING):
        case (e_FM_PORT_TYPE_OH_HOST_COMMAND):
            p_Reg = &p_FmPort->port.bmi_regs->oh.fmbm_ostc;
            isValid = CheckOhBmiCounter(p_FmPort, counter);
            break;
        default:
            RETURN_ERROR(MINOR, E_INVALID_STATE, ("Unsupported port type"));
    }

    if (!isValid)
        RETURN_ERROR(MINOR, E_INVALID_STATE,
                     ("Requested counter is not available for this port type"));

    /* check that counters are enabled */
    switch (counter)
    {
        case (e_FM_PORT_COUNTERS_CYCLE):
        case (e_FM_PORT_COUNTERS_TASK_UTIL):
        case (e_FM_PORT_COUNTERS_QUEUE_UTIL):
        case (e_FM_PORT_COUNTERS_DMA_UTIL):
        case (e_FM_PORT_COUNTERS_FIFO_UTIL):
        case (e_FM_PORT_COUNTERS_RX_PAUSE_ACTIVATION):
            /* performance counters - may be read when disabled */
            *p_IsStats = FALSE;
            break;
        case (e_FM_PORT_COUNTERS_FRAME):
        case (e_FM_PORT_COUNTERS_DISCARD_FRAME):
        case (e_FM_PORT_COUNTERS_DEALLOC_BUF):
        case (e_FM_PORT_COUNTERS_RX_BAD_FRAME):
        case (e_FM_PORT_COUNTERS_RX_LARGE_FRAME):
        case (e_FM_PORT_COUNTERS_RX_FILTER_FRAME):
        case (e_FM_PORT_COUNTERS_RX_LIST_DMA_ERR):
        case (e_FM_PORT_COUNTERS_RX_OUT_OF_BUFFERS_DISCARD):
        case (e_FM_PORT_COUNTERS_LENGTH_ERR):
        case (e_FM_PORT_COUNTERS_UNSUPPRTED_FORMAT):
        case (e_FM_PORT_COUNTERS_WRED_DISCARD):
            *p_IsStats = TRUE;
            if (!(GET_UINT32(*p_Reg) & BMI_COUNTERS_EN))
                RETURN_ERROR(MINOR, E_INVALID_STATE,
                             ("Requested counter was not enabled"));
            break;
        default:
            break;
    }

    /* Set counter */
    switch (counter)
    {
        case (e_FM_PORT_COUNTERS_CYCLE):
            *p_PerfType = E_FMAN_PORT_PERF_CNT_CYCLE;
            break;
        case (e_FM_PORT_COUNTERS_TASK_UTIL):
            *p_PerfType = E_FMAN_PORT_PERF_CNT_TASK_UTIL;
            break;
        case (e_FM_PORT_COUNTERS_QUEUE_UTIL):
            *p_PerfType = E_FMAN_PORT_PERF_CNT_QUEUE_UTIL;
            break;
        case (e_FM_PORT_COUNTERS_DMA_UTIL):
            *p_PerfType = E_FMAN_PORT_PERF_CNT_DMA_UTIL;
            break;
        case (e_FM_PORT_COUNTERS_FIFO_UTIL):
            *p_PerfType = E_FMAN_PORT_PERF_CNT_FIFO_UTIL;
            break;
        case (e_FM_PORT_COUNTERS_RX_PAUSE_ACTIVATION):
            *p_PerfType = E_FMAN_PORT_PERF_CNT_RX_PAUSE;
            break;
        case (e_FM_PORT_COUNTERS_FRAME):
            *p_StatsType = E_FMAN_PORT_STATS_CNT_FRAME;
            break;
        case (e_FM_PORT_COUNTERS_DISCARD_FRAME):
            *p_StatsType = E_FMAN_PORT_STATS_CNT_DISCARD;
            break;
        case (e_FM_PORT_COUNTERS_DEALLOC_BUF):
            *p_StatsType = E_FMAN_PORT_STATS_CNT_DEALLOC_BUF;
            break;
        case (e_FM_PORT_COUNTERS_RX_BAD_FRAME):
            *p_StatsType = E_FMAN_PORT_STATS_CNT_RX_BAD_FRAME;
            break;
        case (e_FM_PORT_COUNTERS_RX_LARGE_FRAME):
            *p_StatsType = E_FMAN_PORT_STATS_CNT_RX_LARGE_FRAME;
            break;
        case (e_FM_PORT_COUNTERS_RX_OUT_OF_BUFFERS_DISCARD):
            *p_StatsType = E_FMAN_PORT_STATS_CNT_RX_OUT_OF_BUF;
            break;
        case (e_FM_PORT_COUNTERS_RX_FILTER_FRAME):
            *p_StatsType = E_FMAN_PORT_STATS_CNT_FILTERED_FRAME;
            break;
        case (e_FM_PORT_COUNTERS_RX_LIST_DMA_ERR):
            *p_StatsType = E_FMAN_PORT_STATS_CNT_DMA_ERR;
            break;
        case (e_FM_PORT_COUNTERS_WRED_DISCARD):
            *p_StatsType = E_FMAN_PORT_STATS_CNT_WRED_DISCARD;
            break;
        case (e_FM_PORT_COUNTERS_LENGTH_ERR):
            *p_StatsType = E_FMAN_PORT_STATS_CNT_LEN_ERR;
            break;
        case (e_FM_PORT_COUNTERS_UNSUPPRTED_FORMAT):
            *p_StatsType = E_FMAN_PORT_STATS_CNT_UNSUPPORTED_FORMAT;
            break;
        default:
            break;
    }

    return E_OK;
}

static t_Error AdditionalPrsParams(t_FmPort *p_FmPort,
                                   t_FmPcdPrsAdditionalHdrParams *p_HdrParams,
                                   uint32_t *p_SoftSeqAttachReg)
{
    uint8_t hdrNum, Ipv4HdrNum;
    u_FmPcdHdrPrsOpts *p_prsOpts;
    uint32_t tmpReg = *p_SoftSeqAttachReg, tmpPrsOffset;

    if (IS_PRIVATE_HEADER(p_HdrParams->hdr)
            || IS_SPECIAL_HEADER(p_HdrParams->hdr))
        RETURN_ERROR(
                MAJOR, E_NOT_SUPPORTED,
                ("No additional parameters for private or special headers."));

    if (p_HdrParams->errDisable)
        tmpReg |= PRS_HDR_ERROR_DIS;

    /* Set parser options */
    if (p_HdrParams->usePrsOpts)
    {
        p_prsOpts = &p_HdrParams->prsOpts;
        switch (p_HdrParams->hdr)
        {
            case (HEADER_TYPE_MPLS):
                if (p_prsOpts->mplsPrsOptions.labelInterpretationEnable)
                    tmpReg |= PRS_HDR_MPLS_LBL_INTER_EN;
                hdrNum = GetPrsHdrNum(p_prsOpts->mplsPrsOptions.nextParse);
                if (hdrNum == ILLEGAL_HDR_NUM)
                    RETURN_ERROR(MAJOR, E_INVALID_VALUE, NO_MSG);
                Ipv4HdrNum = GetPrsHdrNum(HEADER_TYPE_IPv4);
                if (hdrNum < Ipv4HdrNum)
                    RETURN_ERROR(MAJOR, E_INVALID_VALUE,
                                 ("Header must be equal or higher than IPv4"));
                tmpReg |= ((uint32_t)hdrNum * PRS_HDR_ENTRY_SIZE)
                        << PRS_HDR_MPLS_NEXT_HDR_SHIFT;
                break;
            case (HEADER_TYPE_PPPoE):
                if (p_prsOpts->pppoePrsOptions.enableMTUCheck)
                    tmpReg |= PRS_HDR_PPPOE_MTU_CHECK_EN;
                break;
            case (HEADER_TYPE_IPv6):
                if (p_prsOpts->ipv6PrsOptions.routingHdrEnable)
                    tmpReg |= PRS_HDR_IPV6_ROUTE_HDR_EN;
                break;
            case (HEADER_TYPE_TCP):
                if (p_prsOpts->tcpPrsOptions.padIgnoreChecksum)
                    tmpReg |= PRS_HDR_TCP_PAD_REMOVAL;
                else
                    tmpReg &= ~PRS_HDR_TCP_PAD_REMOVAL;
                break;
            case (HEADER_TYPE_UDP):
                if (p_prsOpts->udpPrsOptions.padIgnoreChecksum)
                    tmpReg |= PRS_HDR_UDP_PAD_REMOVAL;
                else
                    tmpReg &= ~PRS_HDR_UDP_PAD_REMOVAL;
                break;
            default:
                RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Invalid header"));
        }
    }

    /* set software parsing (address is divided in 2 since parser uses 2 byte access. */
    if (p_HdrParams->swPrsEnable)
    {
        tmpPrsOffset = FmPcdGetSwPrsOffset(p_FmPort->h_FmPcd, p_HdrParams->hdr,
                                           p_HdrParams->indexPerHdr);
        if (tmpPrsOffset == ILLEGAL_BASE)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, NO_MSG);
        tmpReg |= (PRS_HDR_SW_PRS_EN | tmpPrsOffset);
    }
    *p_SoftSeqAttachReg = tmpReg;

    return E_OK;
}

static uint32_t GetPortSchemeBindParams(
        t_Handle h_FmPort, t_FmPcdKgInterModuleBindPortToSchemes *p_SchemeBind)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    uint32_t walking1Mask = 0x80000000, tmp;
    uint8_t idx = 0;

    p_SchemeBind->netEnvId = p_FmPort->netEnvId;
    p_SchemeBind->hardwarePortId = p_FmPort->hardwarePortId;
    p_SchemeBind->useClsPlan = p_FmPort->useClsPlan;
    p_SchemeBind->numOfSchemes = 0;
    tmp = p_FmPort->schemesPerPortVector;
    if (tmp)
    {
        while (tmp)
        {
            if (tmp & walking1Mask)
            {
                p_SchemeBind->schemesIds[p_SchemeBind->numOfSchemes] = idx;
                p_SchemeBind->numOfSchemes++;
                tmp &= ~walking1Mask;
            }
            walking1Mask >>= 1;
            idx++;
        }
    }

    return tmp;
}

static void FmPortCheckNApplyMacsec(t_Handle h_FmPort)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    volatile uint32_t *p_BmiCfgReg = NULL;
    uint32_t macsecEn = BMI_PORT_CFG_EN_MACSEC;
    uint32_t lcv, walking1Mask = 0x80000000;
    uint8_t cnt = 0;

    ASSERT_COND(p_FmPort);
    ASSERT_COND(p_FmPort->h_FmPcd);
    ASSERT_COND(!p_FmPort->p_FmPortDriverParam);

    if ((p_FmPort->portType != e_FM_PORT_TYPE_RX_10G)
            && (p_FmPort->portType != e_FM_PORT_TYPE_RX))
        return;

    p_BmiCfgReg = &p_FmPort->port.bmi_regs->rx.fmbm_rcfg;
    /* get LCV for MACSEC */
    if ((lcv = FmPcdGetMacsecLcv(p_FmPort->h_FmPcd, p_FmPort->netEnvId))
                    != 0)
    {
        while (!(lcv & walking1Mask))
        {
            cnt++;
            walking1Mask >>= 1;
        }

        macsecEn |= (uint32_t)cnt << BMI_PORT_CFG_MS_SEL_SHIFT;
        WRITE_UINT32(*p_BmiCfgReg, GET_UINT32(*p_BmiCfgReg) | macsecEn);
    }
}

static t_Error SetPcd(t_FmPort *p_FmPort, t_FmPortPcdParams *p_PcdParams)
{
    t_Error err = E_OK;
    uint32_t tmpReg;
    volatile uint32_t *p_BmiNia = NULL;
    volatile uint32_t *p_BmiPrsNia = NULL;
    volatile uint32_t *p_BmiPrsStartOffset = NULL;
    volatile uint32_t *p_BmiInitPrsResult = NULL;
    volatile uint32_t *p_BmiCcBase = NULL;
    uint16_t hdrNum, L3HdrNum, greHdrNum;
    int i;
    bool isEmptyClsPlanGrp;
    uint32_t tmpHxs[FM_PCD_PRS_NUM_OF_HDRS];
    uint16_t absoluteProfileId;
    uint8_t physicalSchemeId;
    uint32_t ccTreePhysOffset;
    t_FmPcdKgInterModuleBindPortToSchemes schemeBind;
    uint32_t initialSwPrs = 0;

    ASSERT_COND(p_FmPort);
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->p_FmPortDriverParam, E_INVALID_STATE);

    if (p_FmPort->imEn)
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION,
                     ("available for non-independant mode ports only"));

    if ((p_FmPort->portType != e_FM_PORT_TYPE_RX_10G)
            && (p_FmPort->portType != e_FM_PORT_TYPE_RX)
            && (p_FmPort->portType != e_FM_PORT_TYPE_OH_OFFLINE_PARSING))
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION,
                     ("available for Rx and offline parsing ports only"));

    p_FmPort->netEnvId = FmPcdGetNetEnvId(p_PcdParams->h_NetEnv);

    p_FmPort->pcdEngines = 0;

    /* initialize p_FmPort->pcdEngines field in port's structure */
    switch (p_PcdParams->pcdSupport)
    {
        case (e_FM_PORT_PCD_SUPPORT_NONE):
            RETURN_ERROR(
                    MAJOR,
                    E_INVALID_STATE,
                    ("No PCD configuration required if e_FM_PORT_PCD_SUPPORT_NONE selected"));
        case (e_FM_PORT_PCD_SUPPORT_PRS_ONLY):
            p_FmPort->pcdEngines |= FM_PCD_PRS;
            break;
        case (e_FM_PORT_PCD_SUPPORT_PLCR_ONLY):
            p_FmPort->pcdEngines |= FM_PCD_PLCR;
            break;
        case (e_FM_PORT_PCD_SUPPORT_PRS_AND_PLCR):
            p_FmPort->pcdEngines |= FM_PCD_PRS;
            p_FmPort->pcdEngines |= FM_PCD_PLCR;
            break;
        case (e_FM_PORT_PCD_SUPPORT_PRS_AND_KG):
            p_FmPort->pcdEngines |= FM_PCD_PRS;
            p_FmPort->pcdEngines |= FM_PCD_KG;
            break;
        case (e_FM_PORT_PCD_SUPPORT_PRS_AND_KG_AND_CC):
            p_FmPort->pcdEngines |= FM_PCD_PRS;
            p_FmPort->pcdEngines |= FM_PCD_CC;
            p_FmPort->pcdEngines |= FM_PCD_KG;
            break;
        case (e_FM_PORT_PCD_SUPPORT_PRS_AND_KG_AND_CC_AND_PLCR):
            p_FmPort->pcdEngines |= FM_PCD_PRS;
            p_FmPort->pcdEngines |= FM_PCD_KG;
            p_FmPort->pcdEngines |= FM_PCD_CC;
            p_FmPort->pcdEngines |= FM_PCD_PLCR;
            break;
        case (e_FM_PORT_PCD_SUPPORT_PRS_AND_CC):
            p_FmPort->pcdEngines |= FM_PCD_PRS;
            p_FmPort->pcdEngines |= FM_PCD_CC;
            break;
        case (e_FM_PORT_PCD_SUPPORT_PRS_AND_CC_AND_PLCR):
            p_FmPort->pcdEngines |= FM_PCD_PRS;
            p_FmPort->pcdEngines |= FM_PCD_CC;
            p_FmPort->pcdEngines |= FM_PCD_PLCR;
            break;
        case (e_FM_PORT_PCD_SUPPORT_PRS_AND_KG_AND_PLCR):
            p_FmPort->pcdEngines |= FM_PCD_PRS;
            p_FmPort->pcdEngines |= FM_PCD_KG;
            p_FmPort->pcdEngines |= FM_PCD_PLCR;
            break;
        case (e_FM_PORT_PCD_SUPPORT_CC_ONLY):
            p_FmPort->pcdEngines |= FM_PCD_CC;
            break;
#ifdef FM_CAPWAP_SUPPORT
            case (e_FM_PORT_PCD_SUPPORT_CC_AND_KG):
            p_FmPort->pcdEngines |= FM_PCD_CC;
            p_FmPort->pcdEngines |= FM_PCD_KG;
            break;
            case (e_FM_PORT_PCD_SUPPORT_CC_AND_KG_AND_PLCR):
            p_FmPort->pcdEngines |= FM_PCD_CC;
            p_FmPort->pcdEngines |= FM_PCD_KG;
            p_FmPort->pcdEngines |= FM_PCD_PLCR;
            break;
#endif /* FM_CAPWAP_SUPPORT */

        default:
            RETURN_ERROR(MAJOR, E_INVALID_STATE, ("invalid pcdSupport"));
    }

    if ((p_FmPort->pcdEngines & FM_PCD_PRS)
            && (p_PcdParams->p_PrsParams->numOfHdrsWithAdditionalParams
                    > FM_PCD_PRS_NUM_OF_HDRS))
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("Port parser numOfHdrsWithAdditionalParams may not exceed %d", FM_PCD_PRS_NUM_OF_HDRS));

    /* check that parameters exist for each and only each defined engine */
    if ((!!(p_FmPort->pcdEngines & FM_PCD_PRS) != !!p_PcdParams->p_PrsParams)
            || (!!(p_FmPort->pcdEngines & FM_PCD_KG)
                    != !!p_PcdParams->p_KgParams)
            || (!!(p_FmPort->pcdEngines & FM_PCD_CC)
                    != !!p_PcdParams->p_CcParams))
        RETURN_ERROR(
                MAJOR,
                E_INVALID_STATE,
                ("PCD initialization structure is not consistent with pcdSupport"));

    /* get PCD registers pointers */
    switch (p_FmPort->portType)
    {
        case (e_FM_PORT_TYPE_RX_10G):
        case (e_FM_PORT_TYPE_RX):
            p_BmiNia = &p_FmPort->port.bmi_regs->rx.fmbm_rfne;
            p_BmiPrsNia = &p_FmPort->port.bmi_regs->rx.fmbm_rfpne;
            p_BmiPrsStartOffset = &p_FmPort->port.bmi_regs->rx.fmbm_rpso;
            p_BmiInitPrsResult = &p_FmPort->port.bmi_regs->rx.fmbm_rprai[0];
            p_BmiCcBase = &p_FmPort->port.bmi_regs->rx.fmbm_rccb;
            break;
        case (e_FM_PORT_TYPE_OH_OFFLINE_PARSING):
            p_BmiNia = &p_FmPort->port.bmi_regs->oh.fmbm_ofne;
            p_BmiPrsNia = &p_FmPort->port.bmi_regs->oh.fmbm_ofpne;
            p_BmiPrsStartOffset = &p_FmPort->port.bmi_regs->oh.fmbm_opso;
            p_BmiInitPrsResult = &p_FmPort->port.bmi_regs->oh.fmbm_oprai[0];
            p_BmiCcBase = &p_FmPort->port.bmi_regs->oh.fmbm_occb;
            break;
        default:
            RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Invalid port type"));
    }

    /* set PCD port parameter */
    if (p_FmPort->pcdEngines & FM_PCD_CC)
    {
        err = FmPcdCcBindTree(p_FmPort->h_FmPcd, p_PcdParams,
                              p_PcdParams->p_CcParams->h_CcTree,
                              &ccTreePhysOffset, p_FmPort);
        if (err)
            RETURN_ERROR(MAJOR, err, NO_MSG);

        WRITE_UINT32(*p_BmiCcBase, ccTreePhysOffset);
        p_FmPort->ccTreeId = p_PcdParams->p_CcParams->h_CcTree;
    }

    if (p_FmPort->pcdEngines & FM_PCD_KG)
    {
        if (p_PcdParams->p_KgParams->numOfSchemes == 0)
            RETURN_ERROR(
                    MAJOR,
                    E_INVALID_VALUE,
                    ("For ports using Keygen, at least one scheme must be bound. "));

        err = FmPcdKgSetOrBindToClsPlanGrp(p_FmPort->h_FmPcd,
                                           p_FmPort->hardwarePortId,
                                           p_FmPort->netEnvId,
                                           p_FmPort->optArray,
                                           &p_FmPort->clsPlanGrpId,
                                           &isEmptyClsPlanGrp);
        if (err)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE,
                         ("FmPcdKgSetOrBindToClsPlanGrp failed. "));

        p_FmPort->useClsPlan = !isEmptyClsPlanGrp;

        schemeBind.netEnvId = p_FmPort->netEnvId;
        schemeBind.hardwarePortId = p_FmPort->hardwarePortId;
        schemeBind.numOfSchemes = p_PcdParams->p_KgParams->numOfSchemes;
        schemeBind.useClsPlan = p_FmPort->useClsPlan;

        /* for each scheme */
        for (i = 0; i < p_PcdParams->p_KgParams->numOfSchemes; i++)
        {
            ASSERT_COND(p_PcdParams->p_KgParams->h_Schemes[i]);
            physicalSchemeId = FmPcdKgGetSchemeId(
                    p_PcdParams->p_KgParams->h_Schemes[i]);
            schemeBind.schemesIds[i] = physicalSchemeId;
            /* build vector */
            p_FmPort->schemesPerPortVector |= 1
                    << (31 - (uint32_t)physicalSchemeId);
#if (DPAA_VERSION >= 11)
            /*because of the state that VSPE is defined per port - all PCD path should be according to this requirement
             if !VSPE - in port, for relevant scheme VSPE can not be set*/
            if (!p_FmPort->vspe
                    && FmPcdKgGetVspe((p_PcdParams->p_KgParams->h_Schemes[i])))
                RETURN_ERROR(MAJOR, E_INVALID_STATE,
                             ("VSPE is not at port level"));
#endif /* (DPAA_VERSION >= 11) */
        }

        err = FmPcdKgBindPortToSchemes(p_FmPort->h_FmPcd, &schemeBind);
        if (err)
            RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    /***************************/
    /* configure NIA after BMI */
    /***************************/
    /* rfne may contain FDCS bits, so first we read them. */
    p_FmPort->savedBmiNia = GET_UINT32(*p_BmiNia) & BMI_RFNE_FDCS_MASK;

    /* If policer is used directly after BMI or PRS */
    if ((p_FmPort->pcdEngines & FM_PCD_PLCR)
            && ((p_PcdParams->pcdSupport == e_FM_PORT_PCD_SUPPORT_PLCR_ONLY)
                    || (p_PcdParams->pcdSupport
                            == e_FM_PORT_PCD_SUPPORT_PRS_AND_PLCR)))
    {
        if (!p_PcdParams->p_PlcrParams->h_Profile)
            RETURN_ERROR(MAJOR, E_INVALID_STATE,
                         ("Profile should be initialized"));

        absoluteProfileId = (uint16_t)FmPcdPlcrProfileGetAbsoluteId(
                p_PcdParams->p_PlcrParams->h_Profile);

        if (!FmPcdPlcrIsProfileValid(p_FmPort->h_FmPcd, absoluteProfileId))
            RETURN_ERROR(MAJOR, E_INVALID_STATE,
                         ("Private port profile not valid."));

        tmpReg = (uint32_t)(absoluteProfileId | NIA_PLCR_ABSOLUTE);

        if (p_FmPort->pcdEngines & FM_PCD_PRS) /* e_FM_PCD_SUPPORT_PRS_AND_PLCR */
            /* update BMI HPNIA */
            WRITE_UINT32(*p_BmiPrsNia, (uint32_t)(NIA_ENG_PLCR | tmpReg));
        else
            /* e_FM_PCD_SUPPORT_PLCR_ONLY */
            /* update BMI NIA */
            p_FmPort->savedBmiNia |= (uint32_t)(NIA_ENG_PLCR);
    }

    /* if CC is used directly after BMI */
    if ((p_PcdParams->pcdSupport == e_FM_PORT_PCD_SUPPORT_CC_ONLY)
#ifdef FM_CAPWAP_SUPPORT
    || (p_PcdParams->pcdSupport == e_FM_PORT_PCD_SUPPORT_CC_AND_KG)
    || (p_PcdParams->pcdSupport == e_FM_PORT_PCD_SUPPORT_CC_AND_KG_AND_PLCR)
#endif /* FM_CAPWAP_SUPPORT */
    )
    {
        if (p_FmPort->portType != e_FM_PORT_TYPE_OH_OFFLINE_PARSING)
            RETURN_ERROR(
                    MAJOR,
                    E_INVALID_OPERATION,
                    ("e_FM_PORT_PCD_SUPPORT_CC_xx available for offline parsing ports only"));
        p_FmPort->savedBmiNia |= (uint32_t)(NIA_ENG_FM_CTL | NIA_FM_CTL_AC_CC);
        /* check that prs start offset == RIM[FOF] */
    }

    if (p_FmPort->pcdEngines & FM_PCD_PRS)
    {
        ASSERT_COND(p_PcdParams->p_PrsParams);
#if (DPAA_VERSION >= 11)
        if (p_PcdParams->p_PrsParams->firstPrsHdr == HEADER_TYPE_CAPWAP)
            hdrNum = OFFLOAD_SW_PATCH_CAPWAP_LABEL;
        else
        {
#endif /* (DPAA_VERSION >= 11) */
            /* if PRS is used it is always first */
                hdrNum = GetPrsHdrNum(p_PcdParams->p_PrsParams->firstPrsHdr);
            if (hdrNum == ILLEGAL_HDR_NUM)
                RETURN_ERROR(MAJOR, E_NOT_SUPPORTED, ("Unsupported header."));
#if (DPAA_VERSION >= 11)
        }
#endif /* (DPAA_VERSION >= 11) */
        p_FmPort->savedBmiNia |= (uint32_t)(NIA_ENG_PRS | (uint32_t)(hdrNum));
        /* set after parser NIA */
        tmpReg = 0;
        switch (p_PcdParams->pcdSupport)
        {
            case (e_FM_PORT_PCD_SUPPORT_PRS_ONLY):
                WRITE_UINT32(*p_BmiPrsNia,
                             GET_NIA_BMI_AC_ENQ_FRAME(p_FmPort->h_FmPcd));
                break;
            case (e_FM_PORT_PCD_SUPPORT_PRS_AND_KG_AND_CC):
            case (e_FM_PORT_PCD_SUPPORT_PRS_AND_KG_AND_CC_AND_PLCR):
                tmpReg = NIA_KG_CC_EN;
            case (e_FM_PORT_PCD_SUPPORT_PRS_AND_KG):
            case (e_FM_PORT_PCD_SUPPORT_PRS_AND_KG_AND_PLCR):
                if (p_PcdParams->p_KgParams->directScheme)
                {
                    physicalSchemeId = FmPcdKgGetSchemeId(
                            p_PcdParams->p_KgParams->h_DirectScheme);
                    /* check that this scheme was bound to this port */
                    for (i = 0; i < p_PcdParams->p_KgParams->numOfSchemes; i++)
                        if (p_PcdParams->p_KgParams->h_DirectScheme
                                == p_PcdParams->p_KgParams->h_Schemes[i])
                            break;
                    if (i == p_PcdParams->p_KgParams->numOfSchemes)
                        RETURN_ERROR(
                                MAJOR,
                                E_INVALID_VALUE,
                                ("Direct scheme is not one of the port selected schemes."));
                    tmpReg |= (uint32_t)(NIA_KG_DIRECT | physicalSchemeId);
                }
                WRITE_UINT32(*p_BmiPrsNia, NIA_ENG_KG | tmpReg);
                break;
            case (e_FM_PORT_PCD_SUPPORT_PRS_AND_CC):
            case (e_FM_PORT_PCD_SUPPORT_PRS_AND_CC_AND_PLCR):
                WRITE_UINT32(*p_BmiPrsNia,
                             (uint32_t)(NIA_ENG_FM_CTL | NIA_FM_CTL_AC_CC));
                break;
            case (e_FM_PORT_PCD_SUPPORT_PRS_AND_PLCR):
                break;
            default:
                RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Invalid PCD support"));
        }

        /* set start parsing offset */
        WRITE_UINT32(*p_BmiPrsStartOffset,
                     p_PcdParams->p_PrsParams->parsingOffset);

        /************************************/
        /* Parser port parameters           */
        /************************************/
        /* stop before configuring */
        WRITE_UINT32(p_FmPort->p_FmPortPrsRegs->pcac, PRS_CAC_STOP);
        /* wait for parser to be in idle state */
        while (GET_UINT32(p_FmPort->p_FmPortPrsRegs->pcac) & PRS_CAC_ACTIVE)
            ;

        /* set soft seq attachment register */
        memset(tmpHxs, 0, FM_PCD_PRS_NUM_OF_HDRS * sizeof(uint32_t));

        /* set protocol options */
        for (i = 0; p_FmPort->optArray[i]; i++)
            switch (p_FmPort->optArray[i])
            {
                case (ETH_BROADCAST):
                    hdrNum = GetPrsHdrNum(HEADER_TYPE_ETH);
                    tmpHxs[hdrNum] |= (i + 1) << PRS_HDR_ETH_BC_SHIFT;
                    break;
                case (ETH_MULTICAST):
                    hdrNum = GetPrsHdrNum(HEADER_TYPE_ETH);
                    tmpHxs[hdrNum] |= (i + 1) << PRS_HDR_ETH_MC_SHIFT;
                    break;
                case (VLAN_STACKED):
                    hdrNum = GetPrsHdrNum(HEADER_TYPE_VLAN);
                    tmpHxs[hdrNum] |= (i + 1) << PRS_HDR_VLAN_STACKED_SHIFT;
                    break;
                case (MPLS_STACKED):
                    hdrNum = GetPrsHdrNum(HEADER_TYPE_MPLS);
                    tmpHxs[hdrNum] |= (i + 1) << PRS_HDR_MPLS_STACKED_SHIFT;
                    break;
                case (IPV4_BROADCAST_1):
                    hdrNum = GetPrsHdrNum(HEADER_TYPE_IPv4);
                    tmpHxs[hdrNum] |= (i + 1) << PRS_HDR_IPV4_1_BC_SHIFT;
                    break;
                case (IPV4_MULTICAST_1):
                    hdrNum = GetPrsHdrNum(HEADER_TYPE_IPv4);
                    tmpHxs[hdrNum] |= (i + 1) << PRS_HDR_IPV4_1_MC_SHIFT;
                    break;
                case (IPV4_UNICAST_2):
					hdrNum = GetPrsHdrNum(HEADER_TYPE_IPv4);
                    tmpHxs[hdrNum] |= (i + 1) << PRS_HDR_IPV4_2_UC_SHIFT;
                    break;
                case (IPV4_MULTICAST_BROADCAST_2):
					hdrNum = GetPrsHdrNum(HEADER_TYPE_IPv4);
                    tmpHxs[hdrNum] |= (i + 1) << PRS_HDR_IPV4_2_MC_BC_SHIFT;
                    break;
                case (IPV6_MULTICAST_1):
                    hdrNum = GetPrsHdrNum(HEADER_TYPE_IPv6);
                    tmpHxs[hdrNum] |= (i + 1) << PRS_HDR_IPV6_1_MC_SHIFT;
                    break;
                case (IPV6_UNICAST_2):
                    hdrNum = GetPrsHdrNum(HEADER_TYPE_IPv6);
                    tmpHxs[hdrNum] |= (i + 1) << PRS_HDR_IPV6_2_UC_SHIFT;
                    break;
                case (IPV6_MULTICAST_2):
                    hdrNum = GetPrsHdrNum(HEADER_TYPE_IPv6);
                    tmpHxs[hdrNum] |= (i + 1) << PRS_HDR_IPV6_2_MC_SHIFT;
                    break;
            }

        if (FmPcdNetEnvIsHdrExist(p_FmPort->h_FmPcd, p_FmPort->netEnvId,
                                  HEADER_TYPE_UDP_ENCAP_ESP))
        {
            if (p_PcdParams->p_PrsParams->numOfHdrsWithAdditionalParams == FM_PCD_PRS_NUM_OF_HDRS)
                RETURN_ERROR(
                         MINOR, E_INVALID_VALUE,
                         ("If HEADER_TYPE_UDP_ENCAP_ESP is used, numOfHdrsWithAdditionalParams may be up to FM_PCD_PRS_NUM_OF_HDRS - 1"));

            p_PcdParams->p_PrsParams->additionalParams[p_PcdParams->p_PrsParams->numOfHdrsWithAdditionalParams].hdr =
                    HEADER_TYPE_UDP;
            p_PcdParams->p_PrsParams->additionalParams[p_PcdParams->p_PrsParams->numOfHdrsWithAdditionalParams].swPrsEnable =
                    TRUE;
            p_PcdParams->p_PrsParams->numOfHdrsWithAdditionalParams++;
        }

        /* set MPLS default next header - HW reset workaround  */
        hdrNum = GetPrsHdrNum(HEADER_TYPE_MPLS);
        tmpHxs[hdrNum] |= PRS_HDR_MPLS_LBL_INTER_EN;
        L3HdrNum = GetPrsHdrNum(HEADER_TYPE_USER_DEFINED_L3);
        tmpHxs[hdrNum] |= (uint32_t)L3HdrNum << PRS_HDR_MPLS_NEXT_HDR_SHIFT;

        /* for GRE, disable errors */
        greHdrNum = GetPrsHdrNum(HEADER_TYPE_GRE);
        tmpHxs[greHdrNum] |= PRS_HDR_ERROR_DIS;

        /* For UDP remove PAD from L4 checksum calculation */
        hdrNum = GetPrsHdrNum(HEADER_TYPE_UDP);
        tmpHxs[hdrNum] |= PRS_HDR_UDP_PAD_REMOVAL;
        /* For TCP remove PAD from L4 checksum calculation */
        hdrNum = GetPrsHdrNum(HEADER_TYPE_TCP);
        tmpHxs[hdrNum] |= PRS_HDR_TCP_PAD_REMOVAL;

        /* config additional params for specific headers */
        for (i = 0; i < p_PcdParams->p_PrsParams->numOfHdrsWithAdditionalParams;
                i++)
        {
            /* case for using sw parser as the initial NIA address, before
               * HW parsing
               */
            if ((p_PcdParams->p_PrsParams->additionalParams[i].hdr == HEADER_TYPE_NONE) && 
                    p_PcdParams->p_PrsParams->additionalParams[i].swPrsEnable)
            {
                initialSwPrs = FmPcdGetSwPrsOffset(p_FmPort->h_FmPcd, HEADER_TYPE_NONE,
                               p_PcdParams->p_PrsParams->additionalParams[i].indexPerHdr);
                if (initialSwPrs == ILLEGAL_BASE)
                    RETURN_ERROR(MAJOR, E_INVALID_VALUE, NO_MSG);

                /* clear parser first HXS */
                p_FmPort->savedBmiNia &= ~BMI_RFNE_HXS_MASK; /* 0x000000FF */
                /* rewrite with soft parser start */
                p_FmPort->savedBmiNia |= initialSwPrs;
                continue;
            }

            hdrNum =
                GetPrsHdrNum(p_PcdParams->p_PrsParams->additionalParams[i].hdr);
            if (hdrNum == ILLEGAL_HDR_NUM)
                RETURN_ERROR(MAJOR, E_INVALID_VALUE, NO_MSG);
            if (hdrNum == NO_HDR_NUM)
                RETURN_ERROR(
                        MAJOR, E_INVALID_VALUE,
                        ("Private headers may not use additional parameters"));

            err = AdditionalPrsParams(
                    p_FmPort, &p_PcdParams->p_PrsParams->additionalParams[i],
                    &tmpHxs[hdrNum]);
            if (err)
                RETURN_ERROR(MAJOR, E_INVALID_VALUE, NO_MSG);
        }

        /* Check if ip-reassembly port - need to link sw-parser code */
        if (p_FmPort->h_IpReassemblyManip)
        {
           /* link to sw parser code for IP Frag - only if no other code is applied. */
            hdrNum = GetPrsHdrNum(HEADER_TYPE_IPv4);
            if (!(tmpHxs[hdrNum] & PRS_HDR_SW_PRS_EN))
                tmpHxs[hdrNum] |= (PRS_HDR_SW_PRS_EN | OFFLOAD_SW_PATCH_IPv4_IPR_LABEL);
            hdrNum = GetPrsHdrNum(HEADER_TYPE_IPv6);
            if (!(tmpHxs[hdrNum] & PRS_HDR_SW_PRS_EN))
                tmpHxs[hdrNum] |= (PRS_HDR_SW_PRS_EN | OFFLOAD_SW_PATCH_IPv6_IPR_LABEL);
        } else {
            if (FmPcdNetEnvIsHdrExist(p_FmPort->h_FmPcd, p_FmPort->netEnvId, HEADER_TYPE_UDP_LITE))
            {
                hdrNum = GetPrsHdrNum(HEADER_TYPE_IPv6);
                if (!(tmpHxs[hdrNum] & PRS_HDR_SW_PRS_EN))
                    tmpHxs[hdrNum] |= (PRS_HDR_SW_PRS_EN | OFFLOAD_SW_PATCH_IPv6_IPF_LABEL);
            } else if ((FmPcdIsAdvancedOffloadSupported(p_FmPort->h_FmPcd)
                       && (p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING)))
                {
                    hdrNum = GetPrsHdrNum(HEADER_TYPE_IPv6);
                    if (!(tmpHxs[hdrNum] & PRS_HDR_SW_PRS_EN))
                        tmpHxs[hdrNum] |= (PRS_HDR_SW_PRS_EN | OFFLOAD_SW_PATCH_IPv6_IPF_LABEL);
                }
            }

#if ((DPAA_VERSION == 10) && defined(FM_CAPWAP_SUPPORT))
        if (FmPcdNetEnvIsHdrExist(p_FmPort->h_FmPcd, p_FmPort->netEnvId,
                        HEADER_TYPE_UDP_LITE))
        {
            /* link to sw parser code for udp lite - only if no other code is applied. */
            hdrNum = GetPrsHdrNum(HEADER_TYPE_IPv6);
            if (!(tmpHxs[hdrNum] & PRS_HDR_SW_PRS_EN))
            tmpHxs[hdrNum] |= (PRS_HDR_SW_PRS_EN | UDP_LITE_SW_PATCH_LABEL);
        }
#endif /* ((DPAA_VERSION == 10) && defined(FM_CAPWAP_SUPPORT)) */
        for (i = 0; i < FM_PCD_PRS_NUM_OF_HDRS; i++)
        {
            /* For all header set LCV as taken from netEnv*/
            WRITE_UINT32(
                    p_FmPort->p_FmPortPrsRegs->hdrs[i].lcv,
                    FmPcdGetLcv(p_FmPort->h_FmPcd, p_FmPort->netEnvId, (uint8_t)i));
            /* set HXS register according to default+Additional params+protocol options */
            WRITE_UINT32(p_FmPort->p_FmPortPrsRegs->hdrs[i].softSeqAttach,
                         tmpHxs[i]);
        }

        /* set tpid. */
        tmpReg = PRS_TPID_DFLT;
        if (p_PcdParams->p_PrsParams->setVlanTpid1)
        {
            tmpReg &= PRS_TPID2_MASK;
            tmpReg |= (uint32_t)p_PcdParams->p_PrsParams->vlanTpid1
                    << PRS_PCTPID_SHIFT;
        }
        if (p_PcdParams->p_PrsParams->setVlanTpid2)
        {
            tmpReg &= PRS_TPID1_MASK;
            tmpReg |= (uint32_t)p_PcdParams->p_PrsParams->vlanTpid2;
        }WRITE_UINT32(p_FmPort->p_FmPortPrsRegs->pctpid, tmpReg);

        /* enable parser */
        WRITE_UINT32(p_FmPort->p_FmPortPrsRegs->pcac, 0);

        if (p_PcdParams->p_PrsParams->prsResultPrivateInfo)
            p_FmPort->privateInfo =
                    p_PcdParams->p_PrsParams->prsResultPrivateInfo;

    } /* end parser */
    else {
        if (FmPcdIsAdvancedOffloadSupported(p_FmPort->h_FmPcd)
            && (p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING))
        {
            hdrNum = GetPrsHdrNum(HEADER_TYPE_IPv6);
            WRITE_UINT32(p_FmPort->p_FmPortPrsRegs->hdrs[hdrNum].softSeqAttach,
                         (PRS_HDR_SW_PRS_EN | OFFLOAD_SW_PATCH_IPv6_IPF_LABEL));
        }

        WRITE_UINT32(*p_BmiPrsStartOffset, 0);

        p_FmPort->privateInfo = 0;
    }

    FmPortCheckNApplyMacsec(p_FmPort);

    WRITE_UINT32(
            *p_BmiPrsStartOffset,
            GET_UINT32(*p_BmiPrsStartOffset) + p_FmPort->internalBufferOffset);

    /* set initial parser result - used for all engines */
    for (i = 0; i < FM_PORT_PRS_RESULT_NUM_OF_WORDS; i++)
    {
        if (!i)
            WRITE_UINT32(
                    *(p_BmiInitPrsResult),
                    (uint32_t)(((uint32_t)p_FmPort->privateInfo << BMI_PR_PORTID_SHIFT) | BMI_PRS_RESULT_HIGH));
        else
        {
            if (i < FM_PORT_PRS_RESULT_NUM_OF_WORDS / 2)
                WRITE_UINT32(*(p_BmiInitPrsResult+i), BMI_PRS_RESULT_HIGH);
            else
                WRITE_UINT32(*(p_BmiInitPrsResult+i), BMI_PRS_RESULT_LOW);
        }
    }

    return E_OK;
}

static t_Error DeletePcd(t_FmPort *p_FmPort)
{
    t_Error err = E_OK;
    volatile uint32_t *p_BmiNia = NULL;
    volatile uint32_t *p_BmiPrsStartOffset = NULL;

    ASSERT_COND(p_FmPort);
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->p_FmPortDriverParam, E_INVALID_STATE);

    if (p_FmPort->imEn)
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION,
                     ("available for non-independant mode ports only"));

    if ((p_FmPort->portType != e_FM_PORT_TYPE_RX_10G)
            && (p_FmPort->portType != e_FM_PORT_TYPE_RX)
            && (p_FmPort->portType != e_FM_PORT_TYPE_OH_OFFLINE_PARSING))
        RETURN_ERROR( MAJOR, E_INVALID_OPERATION,
                     ("available for Rx and offline parsing ports only"));

    if (!p_FmPort->pcdEngines)
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION, ("called for non PCD port"));

    /* get PCD registers pointers */
    switch (p_FmPort->portType)
    {
        case (e_FM_PORT_TYPE_RX_10G):
        case (e_FM_PORT_TYPE_RX):
            p_BmiNia = &p_FmPort->port.bmi_regs->rx.fmbm_rfne;
            p_BmiPrsStartOffset = &p_FmPort->port.bmi_regs->rx.fmbm_rpso;
            break;
        case (e_FM_PORT_TYPE_OH_OFFLINE_PARSING):
            p_BmiNia = &p_FmPort->port.bmi_regs->oh.fmbm_ofne;
            p_BmiPrsStartOffset = &p_FmPort->port.bmi_regs->oh.fmbm_opso;
            break;
        default:
            RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Invalid port type"));
    }

    if ((GET_UINT32(*p_BmiNia) & GET_NO_PCD_NIA_BMI_AC_ENQ_FRAME())
            != GET_NO_PCD_NIA_BMI_AC_ENQ_FRAME())
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION,
                     ("port has to be detached previousely"));

    WRITE_UINT32(*p_BmiPrsStartOffset, 0);

    /* "cut" PCD out of the port's flow - go to BMI */
    /* WRITE_UINT32(*p_BmiNia, (p_FmPort->savedBmiNia & BMI_RFNE_FDCS_MASK) | (NIA_ENG_BMI | NIA_BMI_AC_ENQ_FRAME)); */

    if (p_FmPort->pcdEngines & FM_PCD_PRS)
    {
        /* stop parser */
        WRITE_UINT32(p_FmPort->p_FmPortPrsRegs->pcac, PRS_CAC_STOP);
        /* wait for parser to be in idle state */
        while (GET_UINT32(p_FmPort->p_FmPortPrsRegs->pcac) & PRS_CAC_ACTIVE)
            ;
    }

    if (p_FmPort->pcdEngines & FM_PCD_KG)
    {
        t_FmPcdKgInterModuleBindPortToSchemes schemeBind;

        /* unbind all schemes */
        p_FmPort->schemesPerPortVector = GetPortSchemeBindParams(p_FmPort,
                                                                 &schemeBind);

        err = FmPcdKgUnbindPortToSchemes(p_FmPort->h_FmPcd, &schemeBind);
        if (err)
            RETURN_ERROR(MAJOR, err, NO_MSG);

        err = FmPcdKgDeleteOrUnbindPortToClsPlanGrp(p_FmPort->h_FmPcd,
                                                    p_FmPort->hardwarePortId,
                                                    p_FmPort->clsPlanGrpId);
        if (err)
            RETURN_ERROR(MAJOR, err, NO_MSG);
        p_FmPort->useClsPlan = FALSE;
    }

    if (p_FmPort->pcdEngines & FM_PCD_CC)
    {
        /* unbind - we need to get the treeId too */
        err = FmPcdCcUnbindTree(p_FmPort->h_FmPcd, p_FmPort->ccTreeId);
        if (err)
            RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    p_FmPort->pcdEngines = 0;

    return E_OK;
}

static t_Error AttachPCD(t_FmPort *p_FmPort)
{
    volatile uint32_t *p_BmiNia = NULL;

    ASSERT_COND(p_FmPort);

    /* get PCD registers pointers */
    if (p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING)
        p_BmiNia = &p_FmPort->port.bmi_regs->oh.fmbm_ofne;
    else
        p_BmiNia = &p_FmPort->port.bmi_regs->rx.fmbm_rfne;

    /* check that current NIA is BMI to BMI */
    if ((GET_UINT32(*p_BmiNia) & ~BMI_RFNE_FDCS_MASK)
            != GET_NO_PCD_NIA_BMI_AC_ENQ_FRAME())
        RETURN_ERROR( MAJOR, E_INVALID_OPERATION,
                     ("may be called only for ports in BMI-to-BMI state."));

    if (p_FmPort->requiredAction & UPDATE_FMFP_PRC_WITH_ONE_RISC_ONLY)
        if (FmSetNumOfRiscsPerPort(p_FmPort->h_Fm, p_FmPort->hardwarePortId, 1,
                                   p_FmPort->orFmanCtrl) != E_OK)
            RETURN_ERROR(MAJOR, E_INVALID_STATE, NO_MSG);

    if (p_FmPort->requiredAction & UPDATE_NIA_CMNE)
    {
        if (p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING)
            WRITE_UINT32(p_FmPort->port.bmi_regs->oh.fmbm_ocmne,
                         p_FmPort->savedBmiCmne);
        else
            WRITE_UINT32(p_FmPort->port.bmi_regs->rx.fmbm_rcmne,
                         p_FmPort->savedBmiCmne);
    }

    if (p_FmPort->requiredAction & UPDATE_NIA_PNEN)
        WRITE_UINT32(p_FmPort->p_FmPortQmiRegs->fmqm_pnen,
                     p_FmPort->savedQmiPnen);

    if (p_FmPort->requiredAction & UPDATE_NIA_FENE)
    {
        if (p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING)
            WRITE_UINT32(p_FmPort->port.bmi_regs->oh.fmbm_ofene,
                         p_FmPort->savedBmiFene);
        else
            WRITE_UINT32(p_FmPort->port.bmi_regs->rx.fmbm_rfene,
                         p_FmPort->savedBmiFene);
    }

    if (p_FmPort->requiredAction & UPDATE_NIA_FPNE)
    {
        if (p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING)
            WRITE_UINT32(p_FmPort->port.bmi_regs->oh.fmbm_ofpne,
                         p_FmPort->savedBmiFpne);
        else
            WRITE_UINT32(p_FmPort->port.bmi_regs->rx.fmbm_rfpne,
                         p_FmPort->savedBmiFpne);
    }

    if (p_FmPort->requiredAction & UPDATE_OFP_DPTE)
    {
        ASSERT_COND(p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING);

        WRITE_UINT32(p_FmPort->port.bmi_regs->oh.fmbm_ofp,
                     p_FmPort->savedBmiOfp);
    }

    WRITE_UINT32(*p_BmiNia, p_FmPort->savedBmiNia);

    if (p_FmPort->requiredAction & UPDATE_NIA_PNDN)
    {
        p_FmPort->origNonRxQmiRegsPndn =
                GET_UINT32(p_FmPort->port.qmi_regs->fmqm_pndn);
        WRITE_UINT32(p_FmPort->port.qmi_regs->fmqm_pndn,
                     p_FmPort->savedNonRxQmiRegsPndn);
    }

    return E_OK;
}

static t_Error DetachPCD(t_FmPort *p_FmPort)
{
    volatile uint32_t *p_BmiNia = NULL;

    ASSERT_COND(p_FmPort);

    /* get PCD registers pointers */
    if (p_FmPort->requiredAction & UPDATE_NIA_PNDN)
        WRITE_UINT32(p_FmPort->port.qmi_regs->fmqm_pndn,
                     p_FmPort->origNonRxQmiRegsPndn);

    if (p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING)
        p_BmiNia = &p_FmPort->port.bmi_regs->oh.fmbm_ofne;
    else
        p_BmiNia = &p_FmPort->port.bmi_regs->rx.fmbm_rfne;

    WRITE_UINT32(
            *p_BmiNia,
            (p_FmPort->savedBmiNia & BMI_RFNE_FDCS_MASK) | GET_NO_PCD_NIA_BMI_AC_ENQ_FRAME());

    if (FmPcdGetHcHandle(p_FmPort->h_FmPcd))
        FmPcdHcSync(p_FmPort->h_FmPcd);

    if (p_FmPort->requiredAction & UPDATE_NIA_FENE)
    {
        if (p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING)
            WRITE_UINT32(p_FmPort->port.bmi_regs->oh.fmbm_ofene,
                         NIA_ENG_QMI_ENQ | NIA_ORDER_RESTOR);
        else
            WRITE_UINT32(p_FmPort->port.bmi_regs->rx.fmbm_rfene,
                         NIA_ENG_QMI_ENQ | NIA_ORDER_RESTOR);
    }

    if (p_FmPort->requiredAction & UPDATE_NIA_PNEN)
        WRITE_UINT32(p_FmPort->port.qmi_regs->fmqm_pnen,
                     NIA_ENG_BMI | NIA_BMI_AC_RELEASE);

    if (p_FmPort->requiredAction & UPDATE_FMFP_PRC_WITH_ONE_RISC_ONLY)
        if (FmSetNumOfRiscsPerPort(p_FmPort->h_Fm, p_FmPort->hardwarePortId, 2,
                                   p_FmPort->orFmanCtrl) != E_OK)
            RETURN_ERROR(MAJOR, E_INVALID_STATE, NO_MSG);

    p_FmPort->requiredAction = 0;

    return E_OK;
}

/*****************************************************************************/
/*              Inter-module API routines                                    */
/*****************************************************************************/
void FmPortSetMacsecCmd(t_Handle h_FmPort, uint8_t dfltSci)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    volatile uint32_t *p_BmiCfgReg = NULL;
    uint32_t tmpReg;

    SANITY_CHECK_RETURN(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN(p_FmPort->p_FmPortDriverParam, E_INVALID_STATE);

    if ((p_FmPort->portType != e_FM_PORT_TYPE_TX_10G)
            && (p_FmPort->portType != e_FM_PORT_TYPE_TX))
    {
        REPORT_ERROR(MAJOR, E_INVALID_OPERATION, ("The routine is relevant for Tx ports only"));
        return;
    }

    p_BmiCfgReg = &p_FmPort->port.bmi_regs->tx.fmbm_tfca;
    tmpReg = GET_UINT32(*p_BmiCfgReg) & ~BMI_CMD_ATTR_MACCMD_MASK;
    tmpReg |= BMI_CMD_ATTR_MACCMD_SECURED;
    tmpReg |= (((uint32_t)dfltSci << BMI_CMD_ATTR_MACCMD_SC_SHIFT)
            & BMI_CMD_ATTR_MACCMD_SC_MASK);

    WRITE_UINT32(*p_BmiCfgReg, tmpReg);
}

uint8_t FmPortGetNetEnvId(t_Handle h_FmPort)
{
    return ((t_FmPort*)h_FmPort)->netEnvId;
}

uint8_t FmPortGetHardwarePortId(t_Handle h_FmPort)
{
    return ((t_FmPort*)h_FmPort)->hardwarePortId;
}

uint32_t FmPortGetPcdEngines(t_Handle h_FmPort)
{
    return ((t_FmPort*)h_FmPort)->pcdEngines;
}

#if (DPAA_VERSION >= 11)
t_Error FmPortSetGprFunc(t_Handle h_FmPort, e_FmPortGprFuncType gprFunc,
                         void **p_Value)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    uint32_t muramPageOffset;

    ASSERT_COND(p_FmPort);
    ASSERT_COND(p_Value);

    if (p_FmPort->gprFunc != e_FM_PORT_GPR_EMPTY)
    {
        if (p_FmPort->gprFunc != gprFunc)
            RETURN_ERROR(MAJOR, E_INVALID_STATE,
                         ("gpr was assigned with different func"));
    }
    else
    {
        switch (gprFunc)
        {
            case (e_FM_PORT_GPR_MURAM_PAGE):
                p_FmPort->p_ParamsPage = FM_MURAM_AllocMem(p_FmPort->h_FmMuram,
                                                           256, 8);
                if (!p_FmPort->p_ParamsPage)
                    RETURN_ERROR(MAJOR, E_NO_MEMORY, ("MURAM alloc for page"));

                IOMemSet32(p_FmPort->p_ParamsPage, 0, 256);
                muramPageOffset =
                        (uint32_t)(XX_VirtToPhys(p_FmPort->p_ParamsPage)
                                - p_FmPort->fmMuramPhysBaseAddr);
                switch (p_FmPort->portType)
                {
                    case (e_FM_PORT_TYPE_RX_10G):
                    case (e_FM_PORT_TYPE_RX):
                        WRITE_UINT32(
                                p_FmPort->p_FmPortBmiRegs->rxPortBmiRegs.fmbm_rgpr,
                                muramPageOffset);
                        break;
                    case (e_FM_PORT_TYPE_OH_OFFLINE_PARSING):
                        WRITE_UINT32(
                                p_FmPort->p_FmPortBmiRegs->ohPortBmiRegs.fmbm_ogpr,
                                muramPageOffset);
                        break;
                    default:
                        RETURN_ERROR(MAJOR, E_INVALID_STATE,
                                     ("Invalid port type"));
                }
                break;
            default:
                RETURN_ERROR(MAJOR, E_INVALID_SELECTION, NO_MSG);
        }
        p_FmPort->gprFunc = gprFunc;
    }

    switch (p_FmPort->gprFunc)
    {
        case (e_FM_PORT_GPR_MURAM_PAGE):
            *p_Value = p_FmPort->p_ParamsPage;
            break;
        default:
            RETURN_ERROR(MAJOR, E_INVALID_SELECTION, NO_MSG);
    }

    return E_OK;
}
#endif /* (DPAA_VERSION >= 11) */

t_Error FmPortGetSetCcParams(t_Handle h_FmPort,
                             t_FmPortGetSetCcParams *p_CcParams)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    uint32_t tmpInt;
    volatile uint32_t *p_BmiPrsStartOffset = NULL;

    /* this function called from Cc for pass and receive parameters port params between CC and PORT*/

    if ((p_CcParams->getCcParams.type & OFFSET_OF_PR)
            && (p_FmPort->bufferOffsets.prsResultOffset != ILLEGAL_BASE))
    {
        p_CcParams->getCcParams.prOffset =
                (uint8_t)p_FmPort->bufferOffsets.prsResultOffset;
        p_CcParams->getCcParams.type &= ~OFFSET_OF_PR;
    }
    if (p_CcParams->getCcParams.type & HW_PORT_ID)
    {
        p_CcParams->getCcParams.hardwarePortId =
                (uint8_t)p_FmPort->hardwarePortId;
        p_CcParams->getCcParams.type &= ~HW_PORT_ID;
    }
    if ((p_CcParams->getCcParams.type & OFFSET_OF_DATA)
            && (p_FmPort->bufferOffsets.dataOffset != ILLEGAL_BASE))
    {
        p_CcParams->getCcParams.dataOffset =
                (uint16_t)p_FmPort->bufferOffsets.dataOffset;
        p_CcParams->getCcParams.type &= ~OFFSET_OF_DATA;
    }
    if (p_CcParams->getCcParams.type & NUM_OF_TASKS)
    {
        p_CcParams->getCcParams.numOfTasks = (uint8_t)p_FmPort->tasks.num;
        p_CcParams->getCcParams.type &= ~NUM_OF_TASKS;
    }
    if (p_CcParams->getCcParams.type & NUM_OF_EXTRA_TASKS)
    {
        p_CcParams->getCcParams.numOfExtraTasks =
                (uint8_t)p_FmPort->tasks.extra;
        p_CcParams->getCcParams.type &= ~NUM_OF_EXTRA_TASKS;
    }
    if (p_CcParams->getCcParams.type & FM_REV)
    {
        p_CcParams->getCcParams.revInfo.majorRev = p_FmPort->fmRevInfo.majorRev;
        p_CcParams->getCcParams.revInfo.minorRev = p_FmPort->fmRevInfo.minorRev;
        p_CcParams->getCcParams.type &= ~FM_REV;
    }
    if (p_CcParams->getCcParams.type & DISCARD_MASK)
    {
        if (p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING)
            p_CcParams->getCcParams.discardMask =
                    GET_UINT32(p_FmPort->p_FmPortBmiRegs->ohPortBmiRegs.fmbm_ofsdm);
        else
            p_CcParams->getCcParams.discardMask =
                    GET_UINT32(p_FmPort->p_FmPortBmiRegs->rxPortBmiRegs.fmbm_rfsdm);
        p_CcParams->getCcParams.type &= ~DISCARD_MASK;
    }
    if (p_CcParams->getCcParams.type & MANIP_EXTRA_SPACE)
    {
        p_CcParams->getCcParams.internalBufferOffset =
                p_FmPort->internalBufferOffset;
        p_CcParams->getCcParams.type &= ~MANIP_EXTRA_SPACE;
    }
    if (p_CcParams->getCcParams.type & GET_NIA_FPNE)
    {
        if (p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING)
            p_CcParams->getCcParams.nia =
                    GET_UINT32(p_FmPort->port.bmi_regs->oh.fmbm_ofpne);
        else
            p_CcParams->getCcParams.nia =
                    GET_UINT32(p_FmPort->port.bmi_regs->rx.fmbm_rfpne);
        p_CcParams->getCcParams.type &= ~GET_NIA_FPNE;
    }
    if (p_CcParams->getCcParams.type & GET_NIA_PNDN)
    {
        if (p_FmPort->portType != e_FM_PORT_TYPE_OH_OFFLINE_PARSING)
            RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Invalid port type"));
        p_CcParams->getCcParams.nia =
                GET_UINT32(p_FmPort->p_FmPortQmiRegs->nonRxQmiRegs.fmqm_pndn);
        p_CcParams->getCcParams.type &= ~GET_NIA_PNDN;
    }

    if ((p_CcParams->setCcParams.type & UPDATE_FMFP_PRC_WITH_ONE_RISC_ONLY)
            && !(p_FmPort->requiredAction & UPDATE_FMFP_PRC_WITH_ONE_RISC_ONLY))
    {
        p_FmPort->requiredAction |= UPDATE_FMFP_PRC_WITH_ONE_RISC_ONLY;
        p_FmPort->orFmanCtrl = p_CcParams->setCcParams.orFmanCtrl;
    }

    if ((p_CcParams->setCcParams.type & UPDATE_NIA_PNEN)
            && !(p_FmPort->requiredAction & UPDATE_NIA_PNEN))
    {
        p_FmPort->savedQmiPnen = p_CcParams->setCcParams.nia;
        p_FmPort->requiredAction |= UPDATE_NIA_PNEN;
    }
    else
        if (p_CcParams->setCcParams.type & UPDATE_NIA_PNEN)
        {
            if (p_FmPort->savedQmiPnen != p_CcParams->setCcParams.nia)
                RETURN_ERROR(MAJOR, E_INVALID_STATE,
                             ("PNEN was defined previously different"));
        }

    if ((p_CcParams->setCcParams.type & UPDATE_NIA_PNDN)
            && !(p_FmPort->requiredAction & UPDATE_NIA_PNDN))
    {
        p_FmPort->savedNonRxQmiRegsPndn = p_CcParams->setCcParams.nia;
        p_FmPort->requiredAction |= UPDATE_NIA_PNDN;
    }
    else
        if (p_CcParams->setCcParams.type & UPDATE_NIA_PNDN)
        {
            if (p_FmPort->savedNonRxQmiRegsPndn != p_CcParams->setCcParams.nia)
                RETURN_ERROR(MAJOR, E_INVALID_STATE,
                             ("PNDN was defined previously different"));
        }

    if ((p_CcParams->setCcParams.type & UPDATE_NIA_FENE)
            && (p_CcParams->setCcParams.overwrite
                    || !(p_FmPort->requiredAction & UPDATE_NIA_FENE)))
    {
        p_FmPort->savedBmiFene = p_CcParams->setCcParams.nia;
        p_FmPort->requiredAction |= UPDATE_NIA_FENE;
    }
    else
        if (p_CcParams->setCcParams.type & UPDATE_NIA_FENE)
        {
            if (p_FmPort->savedBmiFene != p_CcParams->setCcParams.nia)
                RETURN_ERROR( MAJOR, E_INVALID_STATE,
                             ("xFENE was defined previously different"));
        }

    if ((p_CcParams->setCcParams.type & UPDATE_NIA_FPNE)
            && !(p_FmPort->requiredAction & UPDATE_NIA_FPNE))
    {
        p_FmPort->savedBmiFpne = p_CcParams->setCcParams.nia;
        p_FmPort->requiredAction |= UPDATE_NIA_FPNE;
    }
    else
        if (p_CcParams->setCcParams.type & UPDATE_NIA_FPNE)
        {
            if (p_FmPort->savedBmiFpne != p_CcParams->setCcParams.nia)
                RETURN_ERROR( MAJOR, E_INVALID_STATE,
                             ("xFPNE was defined previously different"));
        }

    if ((p_CcParams->setCcParams.type & UPDATE_NIA_CMNE)
            && !(p_FmPort->requiredAction & UPDATE_NIA_CMNE))
    {
        p_FmPort->savedBmiCmne = p_CcParams->setCcParams.nia;
        p_FmPort->requiredAction |= UPDATE_NIA_CMNE;
    }
    else
        if (p_CcParams->setCcParams.type & UPDATE_NIA_CMNE)
        {
            if (p_FmPort->savedBmiCmne != p_CcParams->setCcParams.nia)
                RETURN_ERROR( MAJOR, E_INVALID_STATE,
                             ("xCMNE was defined previously different"));
        }

    if ((p_CcParams->setCcParams.type & UPDATE_PSO)
            && !(p_FmPort->requiredAction & UPDATE_PSO))
    {
        /* get PCD registers pointers */
        switch (p_FmPort->portType)
        {
            case (e_FM_PORT_TYPE_RX_10G):
            case (e_FM_PORT_TYPE_RX):
                p_BmiPrsStartOffset = &p_FmPort->port.bmi_regs->rx.fmbm_rpso;
                break;
            case (e_FM_PORT_TYPE_OH_OFFLINE_PARSING):
                p_BmiPrsStartOffset = &p_FmPort->port.bmi_regs->oh.fmbm_opso;
                break;
            default:
                RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Invalid port type"));
        }

        /* set start parsing offset */
        tmpInt = (int)GET_UINT32(*p_BmiPrsStartOffset)
                + p_CcParams->setCcParams.psoSize;
        if (tmpInt > 0)
            WRITE_UINT32(*p_BmiPrsStartOffset, (uint32_t)tmpInt);

        p_FmPort->requiredAction |= UPDATE_PSO;
        p_FmPort->savedPrsStartOffset = p_CcParams->setCcParams.psoSize;
    }
    else
        if (p_CcParams->setCcParams.type & UPDATE_PSO)
        {
            if (p_FmPort->savedPrsStartOffset
                    != p_CcParams->setCcParams.psoSize)
                RETURN_ERROR(
                        MAJOR,
                        E_INVALID_STATE,
                        ("parser start offset was defoned previousley different"));
        }

    if ((p_CcParams->setCcParams.type & UPDATE_OFP_DPTE)
            && !(p_FmPort->requiredAction & UPDATE_OFP_DPTE))
    {
        if (p_FmPort->portType != e_FM_PORT_TYPE_OH_OFFLINE_PARSING)
            RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Invalid port type"));
        p_FmPort->savedBmiOfp = GET_UINT32(p_FmPort->port.bmi_regs->oh.fmbm_ofp);
        p_FmPort->savedBmiOfp &= ~BMI_FIFO_PIPELINE_DEPTH_MASK;
        p_FmPort->savedBmiOfp |= p_CcParams->setCcParams.ofpDpde
                << BMI_FIFO_PIPELINE_DEPTH_SHIFT;
        p_FmPort->requiredAction |= UPDATE_OFP_DPTE;
    }

    return E_OK;
}
/*********************** End of inter-module routines ************************/

/****************************************/
/*       API Init unit functions        */
/****************************************/

t_Handle FM_PORT_Config(t_FmPortParams *p_FmPortParams)
{
    t_FmPort *p_FmPort;
    uintptr_t baseAddr = p_FmPortParams->baseAddr;
    uint32_t tmpReg;

    /* Allocate FM structure */
    p_FmPort = (t_FmPort *)XX_Malloc(sizeof(t_FmPort));
    if (!p_FmPort)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("FM Port driver structure"));
        return NULL;
    }
    memset(p_FmPort, 0, sizeof(t_FmPort));

    /* Allocate the FM driver's parameters structure */
    p_FmPort->p_FmPortDriverParam = (t_FmPortDriverParam *)XX_Malloc(
            sizeof(t_FmPortDriverParam));
    if (!p_FmPort->p_FmPortDriverParam)
    {
        XX_Free(p_FmPort);
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("FM Port driver parameters"));
        return NULL;
    }
    memset(p_FmPort->p_FmPortDriverParam, 0, sizeof(t_FmPortDriverParam));

    /* Initialize FM port parameters which will be kept by the driver */
    p_FmPort->portType = p_FmPortParams->portType;
    p_FmPort->portId = p_FmPortParams->portId;
    p_FmPort->pcdEngines = FM_PCD_NONE;
    p_FmPort->f_Exception = p_FmPortParams->f_Exception;
    p_FmPort->h_App = p_FmPortParams->h_App;
    p_FmPort->h_Fm = p_FmPortParams->h_Fm;

    /* get FM revision */
    FM_GetRevision(p_FmPort->h_Fm, &p_FmPort->fmRevInfo);

    /* calculate global portId number */
    p_FmPort->hardwarePortId = SwPortIdToHwPortId(p_FmPort->portType,
                                    p_FmPortParams->portId,
                                    p_FmPort->fmRevInfo.majorRev,
                                    p_FmPort->fmRevInfo.minorRev);

    if (p_FmPort->fmRevInfo.majorRev >= 6)
    {
        if ((p_FmPort->portType == e_FM_PORT_TYPE_OH_HOST_COMMAND)
                && (p_FmPortParams->portId != FM_OH_PORT_ID))
            DBG(WARNING,
                    ("Port ID %d is recommended for HC port. Overwriting HW defaults to be suitable for HC.",
                            FM_OH_PORT_ID));

        if ((p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING)
                && (p_FmPortParams->portId == FM_OH_PORT_ID))
            DBG(WARNING, ("Use non-zero portId for OP port due to insufficient resources on portId 0."));
    }

    /* Set up FM port parameters for initialization phase only */

    /* First, fill in flibs struct */
    fman_port_defconfig(&p_FmPort->p_FmPortDriverParam->dfltCfg,
                        (enum fman_port_type)p_FmPort->portType);
    /* Overwrite some integration specific parameters */
    p_FmPort->p_FmPortDriverParam->dfltCfg.rx_pri_elevation =
            DEFAULT_PORT_rxFifoPriElevationLevel;
    p_FmPort->p_FmPortDriverParam->dfltCfg.rx_fifo_thr =
            DEFAULT_PORT_rxFifoThreshold;

#if defined(FM_OP_NO_VSP_NO_RELEASE_ERRATA_FMAN_A006675) || defined(FM_ERROR_VSP_NO_MATCH_SW006)
    p_FmPort->p_FmPortDriverParam->dfltCfg.errata_A006675 = TRUE;
#else
    p_FmPort->p_FmPortDriverParam->dfltCfg.errata_A006675 = FALSE;
#endif
    if ((p_FmPort->fmRevInfo.majorRev == 6)
            && (p_FmPort->fmRevInfo.minorRev == 0))
        p_FmPort->p_FmPortDriverParam->dfltCfg.errata_A006320 = TRUE;
    else
        p_FmPort->p_FmPortDriverParam->dfltCfg.errata_A006320 = FALSE;

    /* Excessive Threshold register - exists for pre-FMv3 chips only */
    if (p_FmPort->fmRevInfo.majorRev < 6)
    {
#ifdef FM_NO_RESTRICT_ON_ACCESS_RSRC
        p_FmPort->p_FmPortDriverParam->dfltCfg.excessive_threshold_register =
                TRUE;
#endif
        p_FmPort->p_FmPortDriverParam->dfltCfg.fmbm_rebm_has_sgd = FALSE;
        p_FmPort->p_FmPortDriverParam->dfltCfg.fmbm_tfne_has_features = FALSE;
    }
    else
    {
        p_FmPort->p_FmPortDriverParam->dfltCfg.excessive_threshold_register =
                FALSE;
        p_FmPort->p_FmPortDriverParam->dfltCfg.fmbm_rebm_has_sgd = TRUE;
        p_FmPort->p_FmPortDriverParam->dfltCfg.fmbm_tfne_has_features = TRUE;
    }
    if (p_FmPort->fmRevInfo.majorRev == 4)
        p_FmPort->p_FmPortDriverParam->dfltCfg.qmi_deq_options_support = FALSE;
    else
        p_FmPort->p_FmPortDriverParam->dfltCfg.qmi_deq_options_support = TRUE;

    /* Continue with other parameters */
    p_FmPort->p_FmPortDriverParam->baseAddr = baseAddr;
    /* set memory map pointers */
    p_FmPort->p_FmPortQmiRegs =
            (t_FmPortQmiRegs *)UINT_TO_PTR(baseAddr + QMI_PORT_REGS_OFFSET);
    p_FmPort->p_FmPortBmiRegs =
            (u_FmPortBmiRegs *)UINT_TO_PTR(baseAddr + BMI_PORT_REGS_OFFSET);
    p_FmPort->p_FmPortPrsRegs =
            (t_FmPortPrsRegs *)UINT_TO_PTR(baseAddr + PRS_PORT_REGS_OFFSET);

    p_FmPort->p_FmPortDriverParam->bufferPrefixContent.privDataSize =
            DEFAULT_PORT_bufferPrefixContent_privDataSize;
    p_FmPort->p_FmPortDriverParam->bufferPrefixContent.passPrsResult =
            DEFAULT_PORT_bufferPrefixContent_passPrsResult;
    p_FmPort->p_FmPortDriverParam->bufferPrefixContent.passTimeStamp =
            DEFAULT_PORT_bufferPrefixContent_passTimeStamp;
    p_FmPort->p_FmPortDriverParam->bufferPrefixContent.passAllOtherPCDInfo =
            DEFAULT_PORT_bufferPrefixContent_passTimeStamp;
    p_FmPort->p_FmPortDriverParam->bufferPrefixContent.dataAlign =
            DEFAULT_PORT_bufferPrefixContent_dataAlign;
    /*    p_FmPort->p_FmPortDriverParam->dmaSwapData                      = (e_FmDmaSwapOption)DEFAULT_PORT_dmaSwapData;
     p_FmPort->p_FmPortDriverParam->dmaIntContextCacheAttr           = (e_FmDmaCacheOption)DEFAULT_PORT_dmaIntContextCacheAttr;
     p_FmPort->p_FmPortDriverParam->dmaHeaderCacheAttr               = (e_FmDmaCacheOption)DEFAULT_PORT_dmaHeaderCacheAttr;
     p_FmPort->p_FmPortDriverParam->dmaScatterGatherCacheAttr        = (e_FmDmaCacheOption)DEFAULT_PORT_dmaScatterGatherCacheAttr;
     p_FmPort->p_FmPortDriverParam->dmaWriteOptimize                 = DEFAULT_PORT_dmaWriteOptimize;
     */
    p_FmPort->p_FmPortDriverParam->liodnBase = p_FmPortParams->liodnBase;
    p_FmPort->p_FmPortDriverParam->cheksumLastBytesIgnore =
            DEFAULT_PORT_cheksumLastBytesIgnore;

    p_FmPort->maxFrameLength = DEFAULT_PORT_maxFrameLength;
    /* resource distribution. */
	p_FmPort->fifoBufs.num = DEFAULT_PORT_numOfFifoBufs(p_FmPort->portType)
			* BMI_FIFO_UNITS;
	p_FmPort->fifoBufs.extra = DEFAULT_PORT_extraNumOfFifoBufs
			* BMI_FIFO_UNITS;
	p_FmPort->openDmas.num = DEFAULT_PORT_numOfOpenDmas(p_FmPort->portType);
	p_FmPort->openDmas.extra =
			DEFAULT_PORT_extraNumOfOpenDmas(p_FmPort->portType);
	p_FmPort->tasks.num = DEFAULT_PORT_numOfTasks(p_FmPort->portType);
	p_FmPort->tasks.extra = DEFAULT_PORT_extraNumOfTasks(p_FmPort->portType);


#ifdef FM_HEAVY_TRAFFIC_SEQUENCER_HANG_ERRATA_FMAN_A006981
    if ((p_FmPort->fmRevInfo.majorRev == 6)
            && (p_FmPort->fmRevInfo.minorRev == 0)
            && ((p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING)
                    || (p_FmPort->portType == e_FM_PORT_TYPE_TX)))
    {
        p_FmPort->openDmas.num = 16;
        p_FmPort->openDmas.extra = 0;
    }
#endif /* FM_HEAVY_TRAFFIC_SEQUENCER_HANG_ERRATA_FMAN_A006981 */

    /* Port type specific initialization: */
    switch (p_FmPort->portType)
    {
        case (e_FM_PORT_TYPE_RX):
        case (e_FM_PORT_TYPE_RX_10G):
            /* Initialize FM port parameters for initialization phase only */
            p_FmPort->p_FmPortDriverParam->cutBytesFromEnd =
                    DEFAULT_PORT_cutBytesFromEnd;
            p_FmPort->p_FmPortDriverParam->enBufPoolDepletion = FALSE;
            p_FmPort->p_FmPortDriverParam->frmDiscardOverride =
                    DEFAULT_PORT_frmDiscardOverride;

                tmpReg =
                        GET_UINT32(p_FmPort->p_FmPortBmiRegs->rxPortBmiRegs.fmbm_rfp);
			p_FmPort->p_FmPortDriverParam->rxFifoPriElevationLevel =
                        (((tmpReg & BMI_RX_FIFO_PRI_ELEVATION_MASK)
                                >> BMI_RX_FIFO_PRI_ELEVATION_SHIFT) + 1)
                                * BMI_FIFO_UNITS;
                p_FmPort->p_FmPortDriverParam->rxFifoThreshold = (((tmpReg
                        & BMI_RX_FIFO_THRESHOLD_MASK)
                        >> BMI_RX_FIFO_THRESHOLD_SHIFT) + 1) * BMI_FIFO_UNITS;

            p_FmPort->p_FmPortDriverParam->bufMargins.endMargins =
                    DEFAULT_PORT_BufMargins_endMargins;
            p_FmPort->p_FmPortDriverParam->errorsToDiscard =
                    DEFAULT_PORT_errorsToDiscard;
            p_FmPort->p_FmPortDriverParam->forwardReuseIntContext =
                    DEFAULT_PORT_forwardIntContextReuse;
#if (DPAA_VERSION >= 11)
            p_FmPort->p_FmPortDriverParam->noScatherGather =
                    DEFAULT_PORT_noScatherGather;
#endif /* (DPAA_VERSION >= 11) */
            break;

        case (e_FM_PORT_TYPE_TX):
            p_FmPort->p_FmPortDriverParam->dontReleaseBuf = FALSE;
#ifdef FM_WRONG_RESET_VALUES_ERRATA_FMAN_A005127
            tmpReg = 0x00001013;
            WRITE_UINT32( p_FmPort->p_FmPortBmiRegs->txPortBmiRegs.fmbm_tfp,
                         tmpReg);
#endif /* FM_WRONG_RESET_VALUES_ERRATA_FMAN_A005127 */
        case (e_FM_PORT_TYPE_TX_10G):
                tmpReg =
                        GET_UINT32(p_FmPort->p_FmPortBmiRegs->txPortBmiRegs.fmbm_tfp);
                p_FmPort->p_FmPortDriverParam->txFifoMinFillLevel = ((tmpReg
                        & BMI_TX_FIFO_MIN_FILL_MASK)
                        >> BMI_TX_FIFO_MIN_FILL_SHIFT) * BMI_FIFO_UNITS;
			p_FmPort->p_FmPortDriverParam->dfltCfg.tx_fifo_deq_pipeline_depth =
                        (uint8_t)(((tmpReg & BMI_FIFO_PIPELINE_DEPTH_MASK)
                                >> BMI_FIFO_PIPELINE_DEPTH_SHIFT) + 1);
                p_FmPort->p_FmPortDriverParam->txFifoLowComfLevel = (((tmpReg
                        & BMI_TX_LOW_COMF_MASK) >> BMI_TX_LOW_COMF_SHIFT) + 1)
                        * BMI_FIFO_UNITS;

            p_FmPort->p_FmPortDriverParam->deqType = DEFAULT_PORT_deqType;
            p_FmPort->p_FmPortDriverParam->deqPrefetchOption =
                    DEFAULT_PORT_deqPrefetchOption;
            p_FmPort->p_FmPortDriverParam->deqHighPriority =
                    (bool)((p_FmPort->portType == e_FM_PORT_TYPE_TX) ? DEFAULT_PORT_deqHighPriority_1G :
                            DEFAULT_PORT_deqHighPriority_10G);
            p_FmPort->p_FmPortDriverParam->deqByteCnt =
                    (uint16_t)(
                            (p_FmPort->portType == e_FM_PORT_TYPE_TX) ? DEFAULT_PORT_deqByteCnt_1G :
                                    DEFAULT_PORT_deqByteCnt_10G);
            break;
        case (e_FM_PORT_TYPE_OH_OFFLINE_PARSING):
            p_FmPort->p_FmPortDriverParam->errorsToDiscard =
                    DEFAULT_PORT_errorsToDiscard;
#if (DPAA_VERSION >= 11)
            p_FmPort->p_FmPortDriverParam->noScatherGather =
                    DEFAULT_PORT_noScatherGather;
#endif /* (DPAA_VERSION >= 11) */
        case (e_FM_PORT_TYPE_OH_HOST_COMMAND):
            p_FmPort->p_FmPortDriverParam->deqPrefetchOption =
                    DEFAULT_PORT_deqPrefetchOption_HC;
            p_FmPort->p_FmPortDriverParam->deqHighPriority =
                    DEFAULT_PORT_deqHighPriority_1G;
            p_FmPort->p_FmPortDriverParam->deqType = DEFAULT_PORT_deqType;
            p_FmPort->p_FmPortDriverParam->deqByteCnt =
                    DEFAULT_PORT_deqByteCnt_1G;

                tmpReg =
                        GET_UINT32(p_FmPort->p_FmPortBmiRegs->ohPortBmiRegs.fmbm_ofp);
                p_FmPort->p_FmPortDriverParam->dfltCfg.tx_fifo_deq_pipeline_depth =
                        (uint8_t)(((tmpReg & BMI_FIFO_PIPELINE_DEPTH_MASK)
                                >> BMI_FIFO_PIPELINE_DEPTH_SHIFT) + 1);
                if ((p_FmPort->portType == e_FM_PORT_TYPE_OH_HOST_COMMAND)
                        && (p_FmPortParams->portId != FM_OH_PORT_ID))
                {
                    /* Overwrite HC defaults */
			p_FmPort->p_FmPortDriverParam->dfltCfg.tx_fifo_deq_pipeline_depth =
					DEFAULT_PORT_fifoDeqPipelineDepth_OH;
                }

#ifndef FM_FRAME_END_PARAMS_FOR_OP
            if (p_FmPort->fmRevInfo.majorRev < 6)
            p_FmPort->p_FmPortDriverParam->cheksumLastBytesIgnore = DEFAULT_notSupported;
#endif /* !FM_FRAME_END_PARAMS_FOR_OP */

#ifndef FM_DEQ_PIPELINE_PARAMS_FOR_OP
            if (!((p_FmPort->fmRevInfo.majorRev == 4) ||
                            (p_FmPort->fmRevInfo.majorRev >= 6)))
            p_FmPort->p_FmPortDriverParam->dfltCfg.tx_fifo_deq_pipeline_depth = DEFAULT_notSupported;
#endif /* !FM_DEQ_PIPELINE_PARAMS_FOR_OP */
            break;

        default:
            XX_Free(p_FmPort->p_FmPortDriverParam);
            XX_Free(p_FmPort);
            REPORT_ERROR(MAJOR, E_INVALID_STATE, ("Invalid port type"));
            return NULL;
    }
#ifdef FM_QMI_NO_DEQ_OPTIONS_SUPPORT
    if (p_FmPort->fmRevInfo.majorRev == 4)
    p_FmPort->p_FmPortDriverParam->deqPrefetchOption = (e_FmPortDeqPrefetchOption)DEFAULT_notSupported;
#endif /* FM_QMI_NO_DEQ_OPTIONS_SUPPORT */

    p_FmPort->imEn = p_FmPortParams->independentModeEnable;

    if (p_FmPort->imEn)
    {
        if ((p_FmPort->portType == e_FM_PORT_TYPE_TX)
                || (p_FmPort->portType == e_FM_PORT_TYPE_TX_10G))
            p_FmPort->p_FmPortDriverParam->dfltCfg.tx_fifo_deq_pipeline_depth =
                    DEFAULT_PORT_fifoDeqPipelineDepth_IM;
        FmPortConfigIM(p_FmPort, p_FmPortParams);
    }
    else
    {
        switch (p_FmPort->portType)
        {
            case (e_FM_PORT_TYPE_RX):
            case (e_FM_PORT_TYPE_RX_10G):
                /* Initialize FM port parameters for initialization phase only */
                memcpy(&p_FmPort->p_FmPortDriverParam->extBufPools,
                       &p_FmPortParams->specificParams.rxParams.extBufPools,
                       sizeof(t_FmExtPools));
                p_FmPort->p_FmPortDriverParam->errFqid =
                        p_FmPortParams->specificParams.rxParams.errFqid;
                p_FmPort->p_FmPortDriverParam->dfltFqid =
                        p_FmPortParams->specificParams.rxParams.dfltFqid;
                p_FmPort->p_FmPortDriverParam->liodnOffset =
                        p_FmPortParams->specificParams.rxParams.liodnOffset;
                break;
            case (e_FM_PORT_TYPE_OH_OFFLINE_PARSING):
            case (e_FM_PORT_TYPE_TX):
            case (e_FM_PORT_TYPE_TX_10G):
            case (e_FM_PORT_TYPE_OH_HOST_COMMAND):
                p_FmPort->p_FmPortDriverParam->errFqid =
                        p_FmPortParams->specificParams.nonRxParams.errFqid;
                p_FmPort->p_FmPortDriverParam->deqSubPortal =
                        (uint8_t)(p_FmPortParams->specificParams.nonRxParams.qmChannel
                                & QMI_DEQ_CFG_SUBPORTAL_MASK);
                p_FmPort->p_FmPortDriverParam->dfltFqid =
                        p_FmPortParams->specificParams.nonRxParams.dfltFqid;
                break;
            default:
                XX_Free(p_FmPort->p_FmPortDriverParam);
                XX_Free(p_FmPort);
                REPORT_ERROR(MAJOR, E_INVALID_STATE, ("Invalid port type"));
                return NULL;
        }
    }

    memset(p_FmPort->name, 0, (sizeof(char)) * MODULE_NAME_SIZE);
    if (Sprint(
            p_FmPort->name,
            "FM-%d-port-%s-%d",
            FmGetId(p_FmPort->h_Fm),
            ((p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING
                    || (p_FmPort->portType == e_FM_PORT_TYPE_OH_HOST_COMMAND)) ? "OH" :
                    (p_FmPort->portType == e_FM_PORT_TYPE_RX ? "1g-RX" :
                            (p_FmPort->portType == e_FM_PORT_TYPE_TX ? "1g-TX" :
                                    (p_FmPort->portType
                                            == e_FM_PORT_TYPE_RX_10G ? "10g-RX" :
                                            "10g-TX")))),
            p_FmPort->portId) == 0)
    {
        XX_Free(p_FmPort->p_FmPortDriverParam);
        XX_Free(p_FmPort);
        REPORT_ERROR(MAJOR, E_INVALID_STATE, ("Sprint failed"));
        return NULL;
    }

    p_FmPort->h_Spinlock = XX_InitSpinlock();
    if (!p_FmPort->h_Spinlock)
    {
        XX_Free(p_FmPort->p_FmPortDriverParam);
        XX_Free(p_FmPort);
        REPORT_ERROR(MAJOR, E_INVALID_STATE, ("Sprint failed"));
        return NULL;
    }

    return p_FmPort;
}

t_FmPort *rx_port = 0;
t_FmPort *tx_port = 0;

/**************************************************************************//**
 @Function      FM_PORT_Init

 @Description   Initializes the FM module

 @Param[in]     h_FmPort - FM module descriptor

 @Return        E_OK on success; Error code otherwise.
 *//***************************************************************************/
t_Error FM_PORT_Init(t_Handle h_FmPort)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    t_FmPortDriverParam *p_DriverParams;
    t_Error errCode;
    t_FmInterModulePortInitParams fmParams;
    t_FmRevisionInfo revInfo;

    SANITY_CHECK_RETURN_ERROR(h_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    errCode = FmSpBuildBufferStructure(
            &p_FmPort->p_FmPortDriverParam->intContext,
            &p_FmPort->p_FmPortDriverParam->bufferPrefixContent,
            &p_FmPort->p_FmPortDriverParam->bufMargins,
            &p_FmPort->bufferOffsets, &p_FmPort->internalBufferOffset);
    if (errCode != E_OK)
        RETURN_ERROR(MAJOR, errCode, NO_MSG);
#ifdef FM_HEAVY_TRAFFIC_HANG_ERRATA_FMAN_A005669
    if ((p_FmPort->p_FmPortDriverParam->bcbWorkaround) &&
            (p_FmPort->portType == e_FM_PORT_TYPE_RX))
    {
        p_FmPort->p_FmPortDriverParam->errorsToDiscard |= FM_PORT_FRM_ERR_PHYSICAL;
        if (!p_FmPort->fifoBufs.num)
        p_FmPort->fifoBufs.num = DEFAULT_PORT_numOfFifoBufs(p_FmPort->portType)*BMI_FIFO_UNITS;
        p_FmPort->fifoBufs.num += 4*KILOBYTE;
    }
#endif /* FM_HEAVY_TRAFFIC_HANG_ERRATA_FMAN_A005669 */

    CHECK_INIT_PARAMETERS(p_FmPort, CheckInitParameters);

    p_DriverParams = p_FmPort->p_FmPortDriverParam;

    /* Set up flibs port structure */
    memset(&p_FmPort->port, 0, sizeof(struct fman_port));
    p_FmPort->port.type = (enum fman_port_type)p_FmPort->portType;
    FM_GetRevision(p_FmPort->h_Fm, &revInfo);
    p_FmPort->port.fm_rev_maj = revInfo.majorRev;
    p_FmPort->port.fm_rev_min = revInfo.minorRev;
    p_FmPort->port.bmi_regs =
            (union fman_port_bmi_regs *)UINT_TO_PTR(p_DriverParams->baseAddr + BMI_PORT_REGS_OFFSET);
    p_FmPort->port.qmi_regs =
            (struct fman_port_qmi_regs *)UINT_TO_PTR(p_DriverParams->baseAddr + QMI_PORT_REGS_OFFSET);
    p_FmPort->port.ext_pools_num = (uint8_t)((revInfo.majorRev == 4) ? 4 : 8);
    p_FmPort->port.im_en = p_FmPort->imEn;
    p_FmPort->p_FmPortPrsRegs =
            (t_FmPortPrsRegs *)UINT_TO_PTR(p_DriverParams->baseAddr + PRS_PORT_REGS_OFFSET);

    if (((p_FmPort->portType == e_FM_PORT_TYPE_RX_10G)
            || (p_FmPort->portType == e_FM_PORT_TYPE_RX)) && !p_FmPort->imEn)
    {
        /* Call the external Buffer routine which also checks fifo
         size and updates it if necessary */
        /* define external buffer pools and pool depletion*/
        errCode = SetExtBufferPools(p_FmPort);
        if (errCode)
            RETURN_ERROR(MAJOR, errCode, NO_MSG);
        /* check if the largest external buffer pool is large enough */
        if (p_DriverParams->bufMargins.startMargins + MIN_EXT_BUF_SIZE
                + p_DriverParams->bufMargins.endMargins
                > p_FmPort->rxPoolsParams.largestBufSize)
            RETURN_ERROR(
                    MAJOR,
                    E_INVALID_VALUE,
                    ("bufMargins.startMargins (%d) + minimum buf size (64) + bufMargins.endMargins (%d) is larger than maximum external buffer size (%d)", p_DriverParams->bufMargins.startMargins, p_DriverParams->bufMargins.endMargins, p_FmPort->rxPoolsParams.largestBufSize));
    }
    if (p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING)
    {
        {
#ifdef FM_NO_OP_OBSERVED_POOLS
            t_FmRevisionInfo revInfo;

            FM_GetRevision(p_FmPort->h_Fm, &revInfo);
            if ((revInfo.majorRev == 4) && (p_DriverParams->enBufPoolDepletion))
#endif /* FM_NO_OP_OBSERVED_POOLS */
            {
                /* define external buffer pools */
                errCode = SetExtBufferPools(p_FmPort);
                if (errCode)
                    RETURN_ERROR(MAJOR, errCode, NO_MSG);
            }
        }
    }

    /************************************************************/
    /* Call FM module routine for communicating parameters      */
    /************************************************************/
    memset(&fmParams, 0, sizeof(fmParams));
    fmParams.hardwarePortId = p_FmPort->hardwarePortId;
    fmParams.portType = (e_FmPortType)p_FmPort->portType;
    fmParams.numOfTasks = (uint8_t)p_FmPort->tasks.num;
    fmParams.numOfExtraTasks = (uint8_t)p_FmPort->tasks.extra;
    fmParams.numOfOpenDmas = (uint8_t)p_FmPort->openDmas.num;
    fmParams.numOfExtraOpenDmas = (uint8_t)p_FmPort->openDmas.extra;

    if (p_FmPort->fifoBufs.num)
    {
        errCode = VerifySizeOfFifo(p_FmPort);
        if (errCode != E_OK)
            RETURN_ERROR(MAJOR, errCode, NO_MSG);
    }
    fmParams.sizeOfFifo = p_FmPort->fifoBufs.num;
    fmParams.extraSizeOfFifo = p_FmPort->fifoBufs.extra;
    fmParams.independentMode = p_FmPort->imEn;
    fmParams.liodnOffset = p_DriverParams->liodnOffset;
    fmParams.liodnBase = p_DriverParams->liodnBase;
    fmParams.deqPipelineDepth =
            p_FmPort->p_FmPortDriverParam->dfltCfg.tx_fifo_deq_pipeline_depth;
    fmParams.maxFrameLength = p_FmPort->maxFrameLength;
#ifndef FM_DEQ_PIPELINE_PARAMS_FOR_OP
    if ((p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING) ||
            (p_FmPort->portType == e_FM_PORT_TYPE_OH_HOST_COMMAND))
    {
        if (!((p_FmPort->fmRevInfo.majorRev == 4) ||
                        (p_FmPort->fmRevInfo.majorRev >= 6)))
        /* HC ports do not have fifoDeqPipelineDepth, but it is needed only
         * for deq threshold calculation.
         */
        fmParams.deqPipelineDepth = 2;
    }
#endif /* !FM_DEQ_PIPELINE_PARAMS_FOR_OP */

    errCode = FmGetSetPortParams(p_FmPort->h_Fm, &fmParams);
    if (errCode)
        RETURN_ERROR(MAJOR, errCode, NO_MSG);

    /* get params for use in init */
    p_FmPort->fmMuramPhysBaseAddr =
            (uint64_t)((uint64_t)(fmParams.fmMuramPhysBaseAddr.low)
                    | ((uint64_t)(fmParams.fmMuramPhysBaseAddr.high) << 32));
    p_FmPort->h_FmMuram = FmGetMuramHandle(p_FmPort->h_Fm);

    errCode = InitLowLevelDriver(p_FmPort);
    if (errCode != E_OK)
        RETURN_ERROR(MAJOR, errCode, NO_MSG);

    FmPortDriverParamFree(p_FmPort);

#if (DPAA_VERSION >= 11)
    if ((p_FmPort->portType == e_FM_PORT_TYPE_RX_10G)
            || (p_FmPort->portType == e_FM_PORT_TYPE_RX)
            || (p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING))
    {
        t_FmPcdCtrlParamsPage *p_ParamsPage;

        FmPortSetGprFunc(p_FmPort, e_FM_PORT_GPR_MURAM_PAGE,
                         (void**)&p_ParamsPage);
        ASSERT_COND(p_ParamsPage);

        WRITE_UINT32(p_ParamsPage->misc, FM_CTL_PARAMS_PAGE_ALWAYS_ON);
#ifdef FM_OP_NO_VSP_NO_RELEASE_ERRATA_FMAN_A006675
        if (p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING)
        {
            WRITE_UINT32(
                    p_ParamsPage->misc,
                    (GET_UINT32(p_ParamsPage->misc) | FM_CTL_PARAMS_PAGE_OP_FIX_EN));
            WRITE_UINT32(
                    p_ParamsPage->discardMask,
                    GET_UINT32(p_FmPort->p_FmPortBmiRegs->ohPortBmiRegs.fmbm_ofsdm));
        }
#endif /* FM_OP_NO_VSP_NO_RELEASE_ERRATA_FMAN_A006675 */
#ifdef FM_ERROR_VSP_NO_MATCH_SW006
        if (p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING)
            WRITE_UINT32(
                    p_ParamsPage->errorsDiscardMask,
                    (GET_UINT32(p_FmPort->p_FmPortBmiRegs->ohPortBmiRegs.fmbm_ofsdm) | GET_UINT32(p_FmPort->p_FmPortBmiRegs->ohPortBmiRegs.fmbm_ofsem)));
        else
            WRITE_UINT32(
                    p_ParamsPage->errorsDiscardMask,
                    (GET_UINT32(p_FmPort->p_FmPortBmiRegs->rxPortBmiRegs.fmbm_rfsdm) | GET_UINT32(p_FmPort->p_FmPortBmiRegs->rxPortBmiRegs.fmbm_rfsem)));
#endif /* FM_ERROR_VSP_NO_MATCH_SW006 */
    }
#endif /* (DPAA_VERSION >= 11) */

    if (p_FmPort->deepSleepVars.autoResMaxSizes)
        FmPortConfigAutoResForDeepSleepSupport1(p_FmPort);
    return E_OK;
}

/**************************************************************************//**
 @Function      FM_PORT_Free

 @Description   Frees all resources that were assigned to FM module.

 Calling this routine invalidates the descriptor.

 @Param[in]     h_FmPort - FM module descriptor

 @Return        E_OK on success; Error code otherwise.
 *//***************************************************************************/
t_Error FM_PORT_Free(t_Handle h_FmPort)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    t_FmInterModulePortFreeParams fmParams;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);

    if (p_FmPort->pcdEngines)
        RETURN_ERROR(
                MAJOR,
                E_INVALID_STATE,
                ("Trying to free a port with PCD. FM_PORT_DeletePCD must be called first."));

    if (p_FmPort->enabled)
    {
        if (FM_PORT_Disable(p_FmPort) != E_OK)
            RETURN_ERROR(MAJOR, E_INVALID_STATE, ("FM_PORT_Disable FAILED"));
    }

    if (p_FmPort->imEn)
        FmPortImFree(p_FmPort);

    FmPortDriverParamFree(p_FmPort);

    memset(&fmParams, 0, sizeof(fmParams));
    fmParams.hardwarePortId = p_FmPort->hardwarePortId;
    fmParams.portType = (e_FmPortType)p_FmPort->portType;
    fmParams.deqPipelineDepth =
            p_FmPort->p_FmPortDriverParam->dfltCfg.tx_fifo_deq_pipeline_depth;

    FmFreePortParams(p_FmPort->h_Fm, &fmParams);

#if (DPAA_VERSION >= 11)
    if (FmVSPFreeForPort(p_FmPort->h_Fm, p_FmPort->portType, p_FmPort->portId)
            != E_OK)
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("VSP free of port FAILED"));

    if (p_FmPort->p_ParamsPage)
        FM_MURAM_FreeMem(p_FmPort->h_FmMuram, p_FmPort->p_ParamsPage);
#endif /* (DPAA_VERSION >= 11) */

    if (p_FmPort->h_Spinlock)
        XX_FreeSpinlock(p_FmPort->h_Spinlock);

    XX_Free(p_FmPort);

    return E_OK;
}

/*************************************************/
/*       API Advanced Init unit functions        */
/*************************************************/

t_Error FM_PORT_ConfigNumOfOpenDmas(t_Handle h_FmPort, t_FmPortRsrc *p_OpenDmas)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    p_FmPort->p_FmPortDriverParam->setNumOfOpenDmas = TRUE;
    memcpy(&p_FmPort->openDmas, p_OpenDmas, sizeof(t_FmPortRsrc));

    return E_OK;
}

t_Error FM_PORT_ConfigNumOfTasks(t_Handle h_FmPort, t_FmPortRsrc *p_NumOfTasks)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    memcpy(&p_FmPort->tasks, p_NumOfTasks, sizeof(t_FmPortRsrc));
    p_FmPort->p_FmPortDriverParam->setNumOfTasks = TRUE;
    return E_OK;
}

t_Error FM_PORT_ConfigSizeOfFifo(t_Handle h_FmPort, t_FmPortRsrc *p_SizeOfFifo)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    p_FmPort->p_FmPortDriverParam->setSizeOfFifo = TRUE;
    memcpy(&p_FmPort->fifoBufs, p_SizeOfFifo, sizeof(t_FmPortRsrc));

    return E_OK;
}

t_Error FM_PORT_ConfigDeqHighPriority(t_Handle h_FmPort, bool highPri)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);
    if ((p_FmPort->portType == e_FM_PORT_TYPE_RX_10G)
            || (p_FmPort->portType == e_FM_PORT_TYPE_RX))
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("not available for Rx ports"));

    p_FmPort->p_FmPortDriverParam->dfltCfg.deq_high_pri = highPri;

    return E_OK;
}

t_Error FM_PORT_ConfigDeqType(t_Handle h_FmPort, e_FmPortDeqType deqType)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);
    if ((p_FmPort->portType == e_FM_PORT_TYPE_RX_10G)
            || (p_FmPort->portType == e_FM_PORT_TYPE_RX))
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION,
                     ("not available for Rx ports"));

    p_FmPort->p_FmPortDriverParam->dfltCfg.deq_type =
            (enum fman_port_deq_type)deqType;

    return E_OK;
}

t_Error FM_PORT_ConfigDeqPrefetchOption(
        t_Handle h_FmPort, e_FmPortDeqPrefetchOption deqPrefetchOption)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);
    if ((p_FmPort->portType == e_FM_PORT_TYPE_RX_10G)
            || (p_FmPort->portType == e_FM_PORT_TYPE_RX))
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION,
                     ("not available for Rx ports"));
    p_FmPort->p_FmPortDriverParam->dfltCfg.deq_prefetch_opt =
            (enum fman_port_deq_prefetch)deqPrefetchOption;

    return E_OK;
}

t_Error FM_PORT_ConfigBackupPools(t_Handle h_FmPort,
                                  t_FmBackupBmPools *p_BackupBmPools)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);
    if ((p_FmPort->portType != e_FM_PORT_TYPE_RX_10G)
            && (p_FmPort->portType != e_FM_PORT_TYPE_RX))
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION,
                     ("available for Rx ports only"));

    p_FmPort->p_FmPortDriverParam->p_BackupBmPools =
            (t_FmBackupBmPools *)XX_Malloc(sizeof(t_FmBackupBmPools));
    if (!p_FmPort->p_FmPortDriverParam->p_BackupBmPools)
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("p_BackupBmPools allocation failed"));
    memcpy(p_FmPort->p_FmPortDriverParam->p_BackupBmPools, p_BackupBmPools,
           sizeof(t_FmBackupBmPools));

    return E_OK;
}

t_Error FM_PORT_ConfigDeqByteCnt(t_Handle h_FmPort, uint16_t deqByteCnt)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);
    if ((p_FmPort->portType == e_FM_PORT_TYPE_RX_10G)
            || (p_FmPort->portType == e_FM_PORT_TYPE_RX))
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION,
                     ("not available for Rx ports"));

    p_FmPort->p_FmPortDriverParam->dfltCfg.deq_byte_cnt = deqByteCnt;

    return E_OK;
}

t_Error FM_PORT_ConfigBufferPrefixContent(
        t_Handle h_FmPort, t_FmBufferPrefixContent *p_FmBufferPrefixContent)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    memcpy(&p_FmPort->p_FmPortDriverParam->bufferPrefixContent,
           p_FmBufferPrefixContent, sizeof(t_FmBufferPrefixContent));
    /* if dataAlign was not initialized by user, we return to driver's default */
    if (!p_FmPort->p_FmPortDriverParam->bufferPrefixContent.dataAlign)
        p_FmPort->p_FmPortDriverParam->bufferPrefixContent.dataAlign =
                DEFAULT_PORT_bufferPrefixContent_dataAlign;

    return E_OK;
}

t_Error FM_PORT_ConfigCheksumLastBytesIgnore(t_Handle h_FmPort,
                                             uint8_t checksumLastBytesIgnore)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    p_FmPort->p_FmPortDriverParam->dfltCfg.checksum_bytes_ignore =
            checksumLastBytesIgnore;

    return E_OK;
}

t_Error FM_PORT_ConfigCutBytesFromEnd(t_Handle h_FmPort,
                                      uint8_t cutBytesFromEnd)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);
    if ((p_FmPort->portType != e_FM_PORT_TYPE_RX_10G)
            && (p_FmPort->portType != e_FM_PORT_TYPE_RX))
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION,
                     ("available for Rx ports only"));

    p_FmPort->p_FmPortDriverParam->dfltCfg.rx_cut_end_bytes = cutBytesFromEnd;

    return E_OK;
}

t_Error FM_PORT_ConfigPoolDepletion(t_Handle h_FmPort,
                                    t_FmBufPoolDepletion *p_BufPoolDepletion)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);
    if ((p_FmPort->portType != e_FM_PORT_TYPE_RX_10G)
            && (p_FmPort->portType != e_FM_PORT_TYPE_RX))
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION,
                     ("available for Rx ports only"));

    p_FmPort->p_FmPortDriverParam->enBufPoolDepletion = TRUE;
    memcpy(&p_FmPort->p_FmPortDriverParam->bufPoolDepletion, p_BufPoolDepletion,
           sizeof(t_FmBufPoolDepletion));

    return E_OK;
}

t_Error FM_PORT_ConfigObservedPoolDepletion(
        t_Handle h_FmPort,
        t_FmPortObservedBufPoolDepletion *p_FmPortObservedBufPoolDepletion)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);
    if (p_FmPort->portType != e_FM_PORT_TYPE_OH_OFFLINE_PARSING)
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION,
                     ("available for OP ports only"));

    p_FmPort->p_FmPortDriverParam->enBufPoolDepletion = TRUE;
    memcpy(&p_FmPort->p_FmPortDriverParam->bufPoolDepletion,
           &p_FmPortObservedBufPoolDepletion->poolDepletionParams,
           sizeof(t_FmBufPoolDepletion));
    memcpy(&p_FmPort->p_FmPortDriverParam->extBufPools,
           &p_FmPortObservedBufPoolDepletion->poolsParams,
           sizeof(t_FmExtPools));

    return E_OK;
}

t_Error FM_PORT_ConfigExtBufPools(t_Handle h_FmPort, t_FmExtPools *p_FmExtPools)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    if (p_FmPort->portType != e_FM_PORT_TYPE_OH_OFFLINE_PARSING)
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION,
                     ("available for OP ports only"));

    memcpy(&p_FmPort->p_FmPortDriverParam->extBufPools, p_FmExtPools,
           sizeof(t_FmExtPools));

    return E_OK;
}

t_Error FM_PORT_ConfigDontReleaseTxBufToBM(t_Handle h_FmPort)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);
    if ((p_FmPort->portType != e_FM_PORT_TYPE_TX_10G)
            && (p_FmPort->portType != e_FM_PORT_TYPE_TX))
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION,
                     ("available for Tx ports only"));

    p_FmPort->p_FmPortDriverParam->dontReleaseBuf = TRUE;

    return E_OK;
}

t_Error FM_PORT_ConfigDfltColor(t_Handle h_FmPort, e_FmPortColor color)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);
    p_FmPort->p_FmPortDriverParam->dfltCfg.color = (enum fman_port_color)color;

    return E_OK;
}

t_Error FM_PORT_ConfigSyncReq(t_Handle h_FmPort, bool syncReq)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    if ((p_FmPort->portType == e_FM_PORT_TYPE_TX_10G)
            || (p_FmPort->portType == e_FM_PORT_TYPE_TX))
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION,
                     ("Not available for Tx ports"));

    p_FmPort->p_FmPortDriverParam->dfltCfg.sync_req = syncReq;

    return E_OK;
}

t_Error FM_PORT_ConfigFrmDiscardOverride(t_Handle h_FmPort, bool override)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);
    if ((p_FmPort->portType == e_FM_PORT_TYPE_TX_10G)
            || (p_FmPort->portType == e_FM_PORT_TYPE_TX))
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION,
                     ("Not available for Tx ports"));

    p_FmPort->p_FmPortDriverParam->dfltCfg.discard_override = override;

    return E_OK;
}

t_Error FM_PORT_ConfigErrorsToDiscard(t_Handle h_FmPort,
                                      fmPortFrameErrSelect_t errs)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);
    if ((p_FmPort->portType != e_FM_PORT_TYPE_RX_10G)
            && (p_FmPort->portType != e_FM_PORT_TYPE_RX)
            && (p_FmPort->portType != e_FM_PORT_TYPE_OH_OFFLINE_PARSING))
        RETURN_ERROR( MAJOR, E_INVALID_OPERATION,
                     ("available for Rx and offline parsing ports only"));

    p_FmPort->p_FmPortDriverParam->errorsToDiscard = errs;

    return E_OK;
}

t_Error FM_PORT_ConfigDmaSwapData(t_Handle h_FmPort, e_FmDmaSwapOption swapData)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    p_FmPort->p_FmPortDriverParam->dfltCfg.dma_swap_data =
            (enum fman_port_dma_swap)swapData;

    return E_OK;
}

t_Error FM_PORT_ConfigDmaIcCacheAttr(t_Handle h_FmPort,
                                     e_FmDmaCacheOption intContextCacheAttr)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    p_FmPort->p_FmPortDriverParam->dfltCfg.dma_ic_stash_on =
            (bool)(intContextCacheAttr == e_FM_DMA_STASH);

    return E_OK;
}

t_Error FM_PORT_ConfigDmaHdrAttr(t_Handle h_FmPort,
                                 e_FmDmaCacheOption headerCacheAttr)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    p_FmPort->p_FmPortDriverParam->dfltCfg.dma_header_stash_on =
            (bool)(headerCacheAttr == e_FM_DMA_STASH);

    return E_OK;
}

t_Error FM_PORT_ConfigDmaScatterGatherAttr(
        t_Handle h_FmPort, e_FmDmaCacheOption scatterGatherCacheAttr)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    p_FmPort->p_FmPortDriverParam->dfltCfg.dma_sg_stash_on =
            (bool)(scatterGatherCacheAttr == e_FM_DMA_STASH);

    return E_OK;
}

t_Error FM_PORT_ConfigDmaWriteOptimize(t_Handle h_FmPort, bool optimize)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    if ((p_FmPort->portType == e_FM_PORT_TYPE_TX_10G)
            || (p_FmPort->portType == e_FM_PORT_TYPE_TX))
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION,
                     ("Not available for Tx ports"));

    p_FmPort->p_FmPortDriverParam->dfltCfg.dma_write_optimize = optimize;

    return E_OK;
}

#if (DPAA_VERSION >= 11)
t_Error FM_PORT_ConfigNoScatherGather(t_Handle h_FmPort, bool noScatherGather)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    UNUSED(noScatherGather);
    UNUSED(p_FmPort);

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    p_FmPort->p_FmPortDriverParam->noScatherGather = noScatherGather;

    return E_OK;
}
#endif /* (DPAA_VERSION >= 11) */

t_Error FM_PORT_ConfigForwardReuseIntContext(t_Handle h_FmPort,
                                             bool forwardReuse)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    if ((p_FmPort->portType != e_FM_PORT_TYPE_RX_10G)
            && (p_FmPort->portType != e_FM_PORT_TYPE_RX))
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION,
                     ("available for Rx ports only"));

    p_FmPort->p_FmPortDriverParam->forwardReuseIntContext = forwardReuse;

    return E_OK;
}

t_Error FM_PORT_ConfigMaxFrameLength(t_Handle h_FmPort, uint16_t length)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    p_FmPort->maxFrameLength = length;

    return E_OK;
}

#ifdef FM_HEAVY_TRAFFIC_HANG_ERRATA_FMAN_A005669
t_Error FM_PORT_ConfigBCBWorkaround(t_Handle h_FmPort)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    p_FmPort->p_FmPortDriverParam->bcbWorkaround = TRUE;

    return E_OK;
}
#endif /* FM_HEAVY_TRAFFIC_HANG_ERRATA_FMAN_A005669 */

/****************************************************/
/*       Hidden-DEBUG Only API                      */
/****************************************************/

t_Error FM_PORT_ConfigTxFifoMinFillLevel(t_Handle h_FmPort,
                                         uint32_t minFillLevel)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);
    if ((p_FmPort->portType != e_FM_PORT_TYPE_TX_10G)
            && (p_FmPort->portType != e_FM_PORT_TYPE_TX))
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION,
                     ("available for Tx ports only"));

    p_FmPort->p_FmPortDriverParam->dfltCfg.tx_fifo_min_level = minFillLevel;

    return E_OK;
}

t_Error FM_PORT_ConfigFifoDeqPipelineDepth(t_Handle h_FmPort,
                                           uint8_t deqPipelineDepth)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    if ((p_FmPort->portType == e_FM_PORT_TYPE_RX_10G)
            || (p_FmPort->portType == e_FM_PORT_TYPE_RX))
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION,
                     ("Not available for Rx ports"));

    if (p_FmPort->imEn)
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION,
                     ("Not available for IM ports!"));

    p_FmPort->p_FmPortDriverParam->dfltCfg.tx_fifo_deq_pipeline_depth =
            deqPipelineDepth;

    return E_OK;
}

t_Error FM_PORT_ConfigTxFifoLowComfLevel(t_Handle h_FmPort,
                                         uint32_t fifoLowComfLevel)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);
    if ((p_FmPort->portType != e_FM_PORT_TYPE_TX_10G)
            && (p_FmPort->portType != e_FM_PORT_TYPE_TX))
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION,
                     ("available for Tx ports only"));

    p_FmPort->p_FmPortDriverParam->dfltCfg.tx_fifo_low_comf_level =
            fifoLowComfLevel;

    return E_OK;
}

t_Error FM_PORT_ConfigRxFifoThreshold(t_Handle h_FmPort, uint32_t fifoThreshold)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);
    if ((p_FmPort->portType != e_FM_PORT_TYPE_RX_10G)
            && (p_FmPort->portType != e_FM_PORT_TYPE_RX))
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION,
                     ("available for Rx ports only"));

    p_FmPort->p_FmPortDriverParam->dfltCfg.rx_fifo_thr = fifoThreshold;

    return E_OK;
}

t_Error FM_PORT_ConfigRxFifoPriElevationLevel(t_Handle h_FmPort,
                                              uint32_t priElevationLevel)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);
    if ((p_FmPort->portType != e_FM_PORT_TYPE_RX_10G)
            && (p_FmPort->portType != e_FM_PORT_TYPE_RX))
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION,
                     ("available for Rx ports only"));

    p_FmPort->p_FmPortDriverParam->dfltCfg.rx_pri_elevation = priElevationLevel;

    return E_OK;
}
/****************************************************/
/*       API Run-time Control unit functions        */
/****************************************************/

t_Error FM_PORT_SetNumOfOpenDmas(t_Handle h_FmPort,
                                 t_FmPortRsrc *p_NumOfOpenDmas)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    t_Error err;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    if ((!p_NumOfOpenDmas->num) || (p_NumOfOpenDmas->num > MAX_NUM_OF_DMAS))
        RETURN_ERROR( MAJOR, E_INVALID_VALUE,
                     ("openDmas-num can't be larger than %d", MAX_NUM_OF_DMAS));
    if (p_NumOfOpenDmas->extra > MAX_NUM_OF_EXTRA_DMAS)
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("openDmas-extra can't be larger than %d", MAX_NUM_OF_EXTRA_DMAS));
    err = FmSetNumOfOpenDmas(p_FmPort->h_Fm, p_FmPort->hardwarePortId,
                             (uint8_t*)&p_NumOfOpenDmas->num,
                             (uint8_t*)&p_NumOfOpenDmas->extra, FALSE);
    if (err)
        RETURN_ERROR(MAJOR, err, NO_MSG);

    memcpy(&p_FmPort->openDmas, p_NumOfOpenDmas, sizeof(t_FmPortRsrc));

    return E_OK;
}

t_Error FM_PORT_SetNumOfTasks(t_Handle h_FmPort, t_FmPortRsrc *p_NumOfTasks)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    t_Error err;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    /* only driver uses host command port, so ASSERT rather than  RETURN_ERROR */
    ASSERT_COND(p_FmPort->portType != e_FM_PORT_TYPE_OH_HOST_COMMAND);

    if ((!p_NumOfTasks->num) || (p_NumOfTasks->num > MAX_NUM_OF_TASKS))
        RETURN_ERROR(
                MAJOR, E_INVALID_VALUE,
                ("NumOfTasks-num can't be larger than %d", MAX_NUM_OF_TASKS));
    if (p_NumOfTasks->extra > MAX_NUM_OF_EXTRA_TASKS)
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("NumOfTasks-extra can't be larger than %d", MAX_NUM_OF_EXTRA_TASKS));

    err = FmSetNumOfTasks(p_FmPort->h_Fm, p_FmPort->hardwarePortId,
                          (uint8_t*)&p_NumOfTasks->num,
                          (uint8_t*)&p_NumOfTasks->extra, FALSE);
    if (err)
        RETURN_ERROR(MAJOR, err, NO_MSG);

    /* update driver's struct */
    memcpy(&p_FmPort->tasks, p_NumOfTasks, sizeof(t_FmPortRsrc));
    return E_OK;
}

t_Error FM_PORT_SetSizeOfFifo(t_Handle h_FmPort, t_FmPortRsrc *p_SizeOfFifo)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    t_Error err;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    if (!p_SizeOfFifo->num || (p_SizeOfFifo->num > MAX_PORT_FIFO_SIZE))
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("SizeOfFifo-num has to be in the range of 256 - %d", MAX_PORT_FIFO_SIZE));
    if (p_SizeOfFifo->num % BMI_FIFO_UNITS)
        RETURN_ERROR(
                MAJOR, E_INVALID_VALUE,
                ("SizeOfFifo-num has to be divisible by %d", BMI_FIFO_UNITS));
    if ((p_FmPort->portType == e_FM_PORT_TYPE_RX)
            || (p_FmPort->portType == e_FM_PORT_TYPE_RX_10G))
    {
        /* extra FIFO size (allowed only to Rx ports) */
        if (p_SizeOfFifo->extra % BMI_FIFO_UNITS)
            RETURN_ERROR(
                    MAJOR,
                    E_INVALID_VALUE,
                    ("SizeOfFifo-extra has to be divisible by %d", BMI_FIFO_UNITS));
    }
    else
        if (p_SizeOfFifo->extra)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE,
                         (" No SizeOfFifo-extra for non Rx ports"));

    memcpy(&p_FmPort->fifoBufs, p_SizeOfFifo, sizeof(t_FmPortRsrc));

    /* we do not change user's parameter */
    err = VerifySizeOfFifo(p_FmPort);
    if (err)
        RETURN_ERROR(MAJOR, err, NO_MSG);

    err = FmSetSizeOfFifo(p_FmPort->h_Fm, p_FmPort->hardwarePortId,
                          &p_SizeOfFifo->num, &p_SizeOfFifo->extra, FALSE);
    if (err)
        RETURN_ERROR(MAJOR, err, NO_MSG);

    return E_OK;
}

uint32_t FM_PORT_GetBufferDataOffset(t_Handle h_FmPort)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_VALUE(p_FmPort, E_INVALID_HANDLE, 0);
    SANITY_CHECK_RETURN_VALUE(!p_FmPort->p_FmPortDriverParam, E_INVALID_STATE,
                              0);

    return p_FmPort->bufferOffsets.dataOffset;
}

uint8_t * FM_PORT_GetBufferICInfo(t_Handle h_FmPort, char *p_Data)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_VALUE(p_FmPort, E_INVALID_HANDLE, NULL);
    SANITY_CHECK_RETURN_VALUE(!p_FmPort->p_FmPortDriverParam, E_INVALID_STATE,
                              NULL);

    if (p_FmPort->bufferOffsets.pcdInfoOffset == ILLEGAL_BASE)
        return NULL;

    return (uint8_t *)PTR_MOVE(p_Data, p_FmPort->bufferOffsets.pcdInfoOffset);
}

t_FmPrsResult * FM_PORT_GetBufferPrsResult(t_Handle h_FmPort, char *p_Data)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_VALUE(p_FmPort, E_INVALID_HANDLE, NULL);
    SANITY_CHECK_RETURN_VALUE(!p_FmPort->p_FmPortDriverParam, E_INVALID_STATE,
                              NULL);

    if (p_FmPort->bufferOffsets.prsResultOffset == ILLEGAL_BASE)
        return NULL;

    return (t_FmPrsResult *)PTR_MOVE(p_Data, p_FmPort->bufferOffsets.prsResultOffset);
}

uint64_t * FM_PORT_GetBufferTimeStamp(t_Handle h_FmPort, char *p_Data)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_VALUE(p_FmPort, E_INVALID_HANDLE, NULL);
    SANITY_CHECK_RETURN_VALUE(!p_FmPort->p_FmPortDriverParam, E_INVALID_STATE,
                              NULL);

    if (p_FmPort->bufferOffsets.timeStampOffset == ILLEGAL_BASE)
        return NULL;

    return (uint64_t *)PTR_MOVE(p_Data, p_FmPort->bufferOffsets.timeStampOffset);
}

uint8_t * FM_PORT_GetBufferHashResult(t_Handle h_FmPort, char *p_Data)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_VALUE(p_FmPort, E_INVALID_HANDLE, NULL);
    SANITY_CHECK_RETURN_VALUE(!p_FmPort->p_FmPortDriverParam, E_INVALID_STATE,
                              NULL);

    if (p_FmPort->bufferOffsets.hashResultOffset == ILLEGAL_BASE)
        return NULL;

    return (uint8_t *)PTR_MOVE(p_Data, p_FmPort->bufferOffsets.hashResultOffset);
}

t_Error FM_PORT_Disable(t_Handle h_FmPort)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    int err;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->p_FmPortDriverParam, E_INVALID_STATE);

    if (p_FmPort->imEn)
        FmPortImDisable(p_FmPort);

    err = fman_port_disable(&p_FmPort->port);
    if (err == -EBUSY)
    {
        DBG(WARNING, ("%s: BMI or QMI is Busy. Port forced down",
               p_FmPort->name));
    }
    else
        if (err != 0)
        {
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("fman_port_disable"));
        }

    p_FmPort->enabled = FALSE;

    return E_OK;
}

t_Error FM_PORT_Enable(t_Handle h_FmPort)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    int err;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->p_FmPortDriverParam, E_INVALID_STATE);

    /* Used by FM_PORT_Free routine as indication
     if to disable port. Thus set it to TRUE prior
     to enabling itself. This way if part of enable
     process fails there will be still things
     to disable during Free. For example, if BMI
     enable succeeded but QMI failed, still  BMI
     needs to be disabled by Free. */
    p_FmPort->enabled = TRUE;

    if (p_FmPort->imEn)
        FmPortImEnable(p_FmPort);

    err = fman_port_enable(&p_FmPort->port);
    if (err != 0)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("fman_port_enable"));

    return E_OK;
}

t_Error FM_PORT_SetRateLimit(t_Handle h_FmPort, t_FmPortRateLimit *p_RateLimit)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    uint8_t factor, countUnitBit;
    uint16_t baseGran;
    struct fman_port_rate_limiter params;
    int err;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    switch (p_FmPort->portType)
    {
        case (e_FM_PORT_TYPE_TX_10G):
        case (e_FM_PORT_TYPE_TX):
            baseGran = BMI_RATE_LIMIT_GRAN_TX;
            break;
        case (e_FM_PORT_TYPE_OH_OFFLINE_PARSING):
            baseGran = BMI_RATE_LIMIT_GRAN_OP;
            break;
        default:
            RETURN_ERROR( MAJOR, E_INVALID_OPERATION,
                         ("available for Tx and Offline parsing ports only"));
    }

    countUnitBit = (uint8_t)FmGetTimeStampScale(p_FmPort->h_Fm); /* TimeStamp per nano seconds units */
    /* normally, we use 1 usec as the reference count */
    factor = 1;
    /* if ratelimit is too small for a 1usec factor, multiply the factor */
    while (p_RateLimit->rateLimit < baseGran / factor)
    {
        if (countUnitBit == 31)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Rate limit is too small"));

        countUnitBit++;
        factor <<= 1;
    }
    /* if ratelimit is too large for a 1usec factor, it is also larger than max rate*/
    if (p_RateLimit->rateLimit
            > ((uint32_t)baseGran * (1 << 10) * (uint32_t)factor))
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Rate limit is too large"));

    if (!p_RateLimit->maxBurstSize
            || (p_RateLimit->maxBurstSize > BMI_RATE_LIMIT_MAX_BURST_SIZE))
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("maxBurstSize must be between 1K and %dk", BMI_RATE_LIMIT_MAX_BURST_SIZE));

    params.count_1micro_bit = (uint8_t)FmGetTimeStampScale(p_FmPort->h_Fm);
    params.high_burst_size_gran = FALSE;
    params.burst_size = p_RateLimit->maxBurstSize;
    params.rate = p_RateLimit->rateLimit;
    params.rate_factor = E_FMAN_PORT_RATE_DOWN_NONE;

    if (p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING)
    {
#ifndef FM_NO_ADVANCED_RATE_LIMITER

        if ((p_FmPort->fmRevInfo.majorRev == 4)
                || (p_FmPort->fmRevInfo.majorRev >= 6))
        {
            params.high_burst_size_gran = TRUE;
        }
        else
#endif /* ! FM_NO_ADVANCED_RATE_LIMITER */
        {
            if (p_RateLimit->rateLimitDivider
                    != e_FM_PORT_DUAL_RATE_LIMITER_NONE)
                RETURN_ERROR(MAJOR, E_NOT_SUPPORTED,
                             ("FM_PORT_ConfigDualRateLimitScaleDown"));

            if (p_RateLimit->maxBurstSize % 1000)
            {
                p_RateLimit->maxBurstSize =
                        (uint16_t)((p_RateLimit->maxBurstSize / 1000) + 1);
                DBG(WARNING, ("rateLimit.maxBurstSize rounded up to %d", (p_RateLimit->maxBurstSize/1000+1)*1000));
            }
            else
                p_RateLimit->maxBurstSize = (uint16_t)(p_RateLimit->maxBurstSize
                        / 1000);
        }
        params.rate_factor =
                (enum fman_port_rate_limiter_scale_down)p_RateLimit->rateLimitDivider;
        params.burst_size = p_RateLimit->maxBurstSize;
    }

    err = fman_port_set_rate_limiter(&p_FmPort->port, &params);
    if (err != 0)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("fman_port_set_rate_limiter"));

    return E_OK;
}

t_Error FM_PORT_DeleteRateLimit(t_Handle h_FmPort)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    int err;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    if ((p_FmPort->portType == e_FM_PORT_TYPE_RX_10G)
            || (p_FmPort->portType == e_FM_PORT_TYPE_RX)
            || (p_FmPort->portType == e_FM_PORT_TYPE_OH_HOST_COMMAND))
        RETURN_ERROR( MAJOR, E_INVALID_OPERATION,
                     ("available for Tx and Offline parsing ports only"));

    err = fman_port_delete_rate_limiter(&p_FmPort->port);
    if (err != 0)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("fman_port_set_rate_limiter"));
    return E_OK;
}

t_Error FM_PORT_SetPfcPrioritiesMappingToQmanWQ(t_Handle h_FmPort, uint8_t prio,
                                                uint8_t wq)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    uint32_t tmpReg;
    uint32_t wqTmpReg;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->p_FmPortDriverParam, E_INVALID_STATE);

    if ((p_FmPort->portType != e_FM_PORT_TYPE_TX)
            && (p_FmPort->portType != e_FM_PORT_TYPE_TX_10G))
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION,
                     ("PFC mapping is available for Tx ports only"));

    if (prio > 7)
        RETURN_ERROR(MAJOR, E_NOT_IN_RANGE,
                     ("PFC priority (%d) is out of range (0-7)", prio));
    if (wq > 7)
        RETURN_ERROR(MAJOR, E_NOT_IN_RANGE,
                     ("WQ (%d) is out of range (0-7)", wq));

    tmpReg = GET_UINT32(p_FmPort->p_FmPortBmiRegs->txPortBmiRegs.fmbm_tpfcm[0]);
    tmpReg &= ~(0xf << ((7 - prio) * 4));
    wqTmpReg = ((uint32_t)wq << ((7 - prio) * 4));
    tmpReg |= wqTmpReg;

    WRITE_UINT32(p_FmPort->p_FmPortBmiRegs->txPortBmiRegs.fmbm_tpfcm[0],
                 tmpReg);

    return E_OK;
}

t_Error FM_PORT_SetFrameQueueCounters(t_Handle h_FmPort, bool enable)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->p_FmPortDriverParam, E_INVALID_STATE);

    fman_port_set_queue_cnt_mode(&p_FmPort->port, enable);

    return E_OK;
}

t_Error FM_PORT_SetPerformanceCounters(t_Handle h_FmPort, bool enable)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    int err;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->p_FmPortDriverParam, E_INVALID_STATE);

    err = fman_port_set_perf_cnt_mode(&p_FmPort->port, enable);
    if (err != 0)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("fman_port_set_perf_cnt_mode"));
    return E_OK;
}

t_Error FM_PORT_SetPerformanceCountersParams(
        t_Handle h_FmPort, t_FmPortPerformanceCnt *p_FmPortPerformanceCnt)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    struct fman_port_perf_cnt_params params;
    int err;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);

    /* check parameters */
    if (!p_FmPortPerformanceCnt->taskCompVal
            || (p_FmPortPerformanceCnt->taskCompVal > p_FmPort->tasks.num))
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("taskCompVal (%d) has to be in the range of 1 - %d (current value)!", p_FmPortPerformanceCnt->taskCompVal, p_FmPort->tasks.num));
    if (!p_FmPortPerformanceCnt->dmaCompVal
            || (p_FmPortPerformanceCnt->dmaCompVal > p_FmPort->openDmas.num))
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("dmaCompVal (%d) has to be in the range of 1 - %d (current value)!", p_FmPortPerformanceCnt->dmaCompVal, p_FmPort->openDmas.num));
    if (!p_FmPortPerformanceCnt->fifoCompVal
            || (p_FmPortPerformanceCnt->fifoCompVal > p_FmPort->fifoBufs.num))
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("fifoCompVal (%d) has to be in the range of 256 - %d (current value)!", p_FmPortPerformanceCnt->fifoCompVal, p_FmPort->fifoBufs.num));
    if (p_FmPortPerformanceCnt->fifoCompVal % BMI_FIFO_UNITS)
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("fifoCompVal (%d) has to be divisible by %d", p_FmPortPerformanceCnt->fifoCompVal, BMI_FIFO_UNITS));

    switch (p_FmPort->portType)
    {
        case (e_FM_PORT_TYPE_RX_10G):
        case (e_FM_PORT_TYPE_RX):
            if (!p_FmPortPerformanceCnt->queueCompVal
                    || (p_FmPortPerformanceCnt->queueCompVal
                            > MAX_PERFORMANCE_RX_QUEUE_COMP))
                RETURN_ERROR(
                        MAJOR,
                        E_INVALID_VALUE,
                        ("performanceCnt.queueCompVal for Rx has to be in the range of 1 - %d", MAX_PERFORMANCE_RX_QUEUE_COMP));
            break;
        case (e_FM_PORT_TYPE_TX_10G):
        case (e_FM_PORT_TYPE_TX):
            if (!p_FmPortPerformanceCnt->queueCompVal
                    || (p_FmPortPerformanceCnt->queueCompVal
                            > MAX_PERFORMANCE_TX_QUEUE_COMP))
                RETURN_ERROR(
                        MAJOR,
                        E_INVALID_VALUE,
                        ("performanceCnt.queueCompVal for Tx has to be in the range of 1 - %d", MAX_PERFORMANCE_TX_QUEUE_COMP));
            break;
        case (e_FM_PORT_TYPE_OH_OFFLINE_PARSING):
        case (e_FM_PORT_TYPE_OH_HOST_COMMAND):
            if (p_FmPortPerformanceCnt->queueCompVal)
                RETURN_ERROR(
                        MAJOR,
                        E_INVALID_VALUE,
                        ("performanceCnt.queueCompVal is not relevant for H/O ports."));
            break;
        default:
            RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Invalid port type"));
    }

    params.task_val = p_FmPortPerformanceCnt->taskCompVal;
    params.queue_val = p_FmPortPerformanceCnt->queueCompVal;
    params.dma_val = p_FmPortPerformanceCnt->dmaCompVal;
    params.fifo_val = p_FmPortPerformanceCnt->fifoCompVal;

    err = fman_port_set_perf_cnt_params(&p_FmPort->port, &params);
    if (err != 0)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("fman_port_set_perf_cnt_params"));

    return E_OK;
}

t_Error FM_PORT_AnalyzePerformanceParams(t_Handle h_FmPort)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    t_FmPortPerformanceCnt currParams, savedParams;
    t_Error err;
    bool underTest, failed = FALSE;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);

    XX_Print("Analyzing Performance parameters for port (type %d, id%d)\n",
             p_FmPort->portType, p_FmPort->portId);

    currParams.taskCompVal = (uint8_t)p_FmPort->tasks.num;
    if ((p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING)
            || (p_FmPort->portType == e_FM_PORT_TYPE_OH_HOST_COMMAND))
        currParams.queueCompVal = 0;
    else
        currParams.queueCompVal = 1;
    currParams.dmaCompVal = (uint8_t)p_FmPort->openDmas.num;
    currParams.fifoCompVal = p_FmPort->fifoBufs.num;

    FM_PORT_SetPerformanceCounters(p_FmPort, FALSE);
    ClearPerfCnts(p_FmPort);
    if ((err = FM_PORT_SetPerformanceCountersParams(p_FmPort, &currParams))
            != E_OK)
        RETURN_ERROR(MAJOR, err, NO_MSG);
    FM_PORT_SetPerformanceCounters(p_FmPort, TRUE);
    XX_UDelay(1000000);
    FM_PORT_SetPerformanceCounters(p_FmPort, FALSE);
    if (FM_PORT_GetCounter(p_FmPort, e_FM_PORT_COUNTERS_TASK_UTIL))
    {
        XX_Print(
                "Max num of defined port tasks (%d) utilized - Please enlarge\n",
                p_FmPort->tasks.num);
        failed = TRUE;
    }
    if (FM_PORT_GetCounter(p_FmPort, e_FM_PORT_COUNTERS_DMA_UTIL))
    {
        XX_Print(
                "Max num of defined port openDmas (%d) utilized - Please enlarge\n",
                p_FmPort->openDmas.num);
        failed = TRUE;
    }
    if (FM_PORT_GetCounter(p_FmPort, e_FM_PORT_COUNTERS_FIFO_UTIL))
    {
        XX_Print(
                "Max size of defined port fifo (%d) utilized - Please enlarge\n",
                p_FmPort->fifoBufs.num);
        failed = TRUE;
    }
    if (failed)
        RETURN_ERROR(MAJOR, E_INVALID_STATE, NO_MSG);

    memset(&savedParams, 0, sizeof(savedParams));
    while (TRUE)
    {
        underTest = FALSE;
        if ((currParams.taskCompVal != 1) && !savedParams.taskCompVal)
        {
            currParams.taskCompVal--;
            underTest = TRUE;
        }
        if ((currParams.dmaCompVal != 1) && !savedParams.dmaCompVal)
        {
            currParams.dmaCompVal--;
            underTest = TRUE;
        }
        if ((currParams.fifoCompVal != BMI_FIFO_UNITS)
                && !savedParams.fifoCompVal)
        {
            currParams.fifoCompVal -= BMI_FIFO_UNITS;
            underTest = TRUE;
        }
        if (!underTest)
            break;

        ClearPerfCnts(p_FmPort);
        if ((err = FM_PORT_SetPerformanceCountersParams(p_FmPort, &currParams))
                != E_OK)
            RETURN_ERROR(MAJOR, err, NO_MSG);
        FM_PORT_SetPerformanceCounters(p_FmPort, TRUE);
        XX_UDelay(1000000);
        FM_PORT_SetPerformanceCounters(p_FmPort, FALSE);

        if (!savedParams.taskCompVal
                && FM_PORT_GetCounter(p_FmPort, e_FM_PORT_COUNTERS_TASK_UTIL))
            savedParams.taskCompVal = (uint8_t)(currParams.taskCompVal + 2);
        if (!savedParams.dmaCompVal
                && FM_PORT_GetCounter(p_FmPort, e_FM_PORT_COUNTERS_DMA_UTIL))
            savedParams.dmaCompVal = (uint8_t)(currParams.dmaCompVal + 2);
        if (!savedParams.fifoCompVal
                && FM_PORT_GetCounter(p_FmPort, e_FM_PORT_COUNTERS_FIFO_UTIL))
            savedParams.fifoCompVal = currParams.fifoCompVal
                    + (2 * BMI_FIFO_UNITS);
    }

    XX_Print("best vals: tasks %d, dmas %d, fifos %d\n",
             savedParams.taskCompVal, savedParams.dmaCompVal,
             savedParams.fifoCompVal);
    return E_OK;
}

t_Error FM_PORT_SetStatisticsCounters(t_Handle h_FmPort, bool enable)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    int err;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->p_FmPortDriverParam, E_INVALID_STATE);

    err = fman_port_set_stats_cnt_mode(&p_FmPort->port, enable);
    if (err != 0)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("fman_port_set_stats_cnt_mode"));
    return E_OK;
}

t_Error FM_PORT_SetErrorsRoute(t_Handle h_FmPort, fmPortFrameErrSelect_t errs)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    volatile uint32_t *p_ErrDiscard = NULL;
    int err;

    UNUSED(p_ErrDiscard);
    err = fman_port_set_err_mask(&p_FmPort->port, (uint32_t)errs);
    if (err != 0)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("fman_port_set_err_mask"));

#ifdef FM_ERROR_VSP_NO_MATCH_SW006
    if (p_FmPort->fmRevInfo.majorRev >= 6)
    {
        t_FmPcdCtrlParamsPage *p_ParamsPage;

        FmPortSetGprFunc(p_FmPort, e_FM_PORT_GPR_MURAM_PAGE,
                         (void**)&p_ParamsPage);
        ASSERT_COND(p_ParamsPage);
        switch (p_FmPort->portType)
        {
            case (e_FM_PORT_TYPE_RX_10G):
            case (e_FM_PORT_TYPE_RX):
                p_ErrDiscard =
                        &p_FmPort->p_FmPortBmiRegs->rxPortBmiRegs.fmbm_rfsdm;
                break;
            case (e_FM_PORT_TYPE_OH_OFFLINE_PARSING):
                p_ErrDiscard =
                        &p_FmPort->p_FmPortBmiRegs->ohPortBmiRegs.fmbm_ofsdm;
                break;
            default:
                RETURN_ERROR(
                        MAJOR, E_INVALID_OPERATION,
                        ("available for Rx and offline parsing ports only"));
        }
        WRITE_UINT32(p_ParamsPage->errorsDiscardMask,
                     GET_UINT32(*p_ErrDiscard) | errs);
    }
#endif /* FM_ERROR_VSP_NO_MATCH_SW006 */

    return E_OK;
}

t_Error FM_PORT_SetAllocBufCounter(t_Handle h_FmPort, uint8_t poolId,
                                   bool enable)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    int err;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(poolId<BM_MAX_NUM_OF_POOLS, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->p_FmPortDriverParam, E_INVALID_STATE);

    if ((p_FmPort->portType != e_FM_PORT_TYPE_RX_10G)
            && (p_FmPort->portType != e_FM_PORT_TYPE_RX))
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION,
                     ("available for Rx ports only"));

    err = fman_port_set_bpool_cnt_mode(&p_FmPort->port, poolId, enable);
    if (err != 0)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("fman_port_set_bpool_cnt_mode"));
    return E_OK;
}

t_Error FM_PORT_GetBmiCounters(t_Handle h_FmPort, t_FmPortBmiStats *p_BmiStats)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    if ((p_FmPort->portType == e_FM_PORT_TYPE_RX)
            || (p_FmPort->portType == e_FM_PORT_TYPE_RX_10G)){
        p_BmiStats->cntCycle =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_CYCLE);
            /* fmbm_rccn */
        p_BmiStats->cntTaskUtil =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_TASK_UTIL);
            /* fmbm_rtuc */
        p_BmiStats->cntQueueUtil =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_QUEUE_UTIL);
            /* fmbm_rrquc */
        p_BmiStats->cntDmaUtil =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_DMA_UTIL);
            /* fmbm_rduc */
        p_BmiStats->cntFifoUtil =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_FIFO_UTIL);
            /* fmbm_rfuc */
        p_BmiStats->cntRxPauseActivation =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_RX_PAUSE_ACTIVATION);
            /* fmbm_rpac */
        p_BmiStats->cntFrame =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_FRAME);
            /* fmbm_rfrc */
        p_BmiStats->cntDiscardFrame =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_DISCARD_FRAME);
            /* fmbm_rfdc */
        p_BmiStats->cntDeallocBuf =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_DEALLOC_BUF);
            /* fmbm_rbdc */
        p_BmiStats->cntRxBadFrame =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_RX_BAD_FRAME);
            /* fmbm_rfbc */
        p_BmiStats->cntRxLargeFrame =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_RX_LARGE_FRAME);
            /* fmbm_rlfc */
        p_BmiStats->cntRxFilterFrame =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_RX_FILTER_FRAME);
            /* fmbm_rffc */
        p_BmiStats->cntRxListDmaErr =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_RX_LIST_DMA_ERR);
            /* fmbm_rfldec */
        p_BmiStats->cntRxOutOfBuffersDiscard =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_RX_OUT_OF_BUFFERS_DISCARD);
            /* fmbm_rodc */
        p_BmiStats->cntWredDiscard = 0;
        p_BmiStats->cntLengthErr = 0;
        p_BmiStats->cntUnsupportedFormat = 0;
    }
    else if ((p_FmPort->portType == e_FM_PORT_TYPE_TX)
                || (p_FmPort->portType == e_FM_PORT_TYPE_TX_10G)){
        p_BmiStats->cntCycle =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_CYCLE);
            /* fmbm_tccn */
        p_BmiStats->cntTaskUtil =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_TASK_UTIL);
            /* fmbm_ttuc */
        p_BmiStats->cntQueueUtil =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_QUEUE_UTIL);
            /* fmbm_ttcquc */
        p_BmiStats->cntDmaUtil =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_DMA_UTIL);
            /* fmbm_tduc */
        p_BmiStats->cntFifoUtil =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_FIFO_UTIL);
            /* fmbm_tfuc */
        p_BmiStats->cntRxPauseActivation = 0;
        p_BmiStats->cntFrame =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_FRAME);
            /* fmbm_tfrc */
        p_BmiStats->cntDiscardFrame =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_DISCARD_FRAME);
            /* fmbm_tfdc */
        p_BmiStats->cntDeallocBuf =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_DEALLOC_BUF);
            /* fmbm_tbdc */
        p_BmiStats->cntRxBadFrame = 0;
        p_BmiStats->cntRxLargeFrame = 0;
        p_BmiStats->cntRxFilterFrame = 0;
        p_BmiStats->cntRxListDmaErr = 0;
        p_BmiStats->cntRxOutOfBuffersDiscard = 0;
        p_BmiStats->cntWredDiscard = 0;
        p_BmiStats->cntLengthErr =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_LENGTH_ERR);
            /* fmbm_tfledc */
        p_BmiStats->cntUnsupportedFormat =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_UNSUPPRTED_FORMAT);
            /* fmbm_tfufdc */
    }
    else if (p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING) {
        p_BmiStats->cntCycle =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_CYCLE);
            /* fmbm_occn */
        p_BmiStats->cntTaskUtil =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_TASK_UTIL);
            /* fmbm_otuc */
        p_BmiStats->cntQueueUtil = 0;
        p_BmiStats->cntDmaUtil =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_DMA_UTIL);
            /* fmbm_oduc */
        p_BmiStats->cntFifoUtil =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_FIFO_UTIL);
            /* fmbm_ofuc*/
        p_BmiStats->cntRxPauseActivation = 0;
        p_BmiStats->cntFrame =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_FRAME);
            /* fmbm_ofrc */
        p_BmiStats->cntDiscardFrame =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_DISCARD_FRAME);
            /* fmbm_ofdc */
        p_BmiStats->cntDeallocBuf =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_DEALLOC_BUF);
            /* fmbm_obdc*/
        p_BmiStats->cntRxBadFrame = 0;
        p_BmiStats->cntRxLargeFrame = 0;
        p_BmiStats->cntRxFilterFrame =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_RX_FILTER_FRAME);
            /* fmbm_offc */
        p_BmiStats->cntRxListDmaErr =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_RX_LIST_DMA_ERR);
            /* fmbm_ofldec */
        p_BmiStats->cntRxOutOfBuffersDiscard =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_RX_OUT_OF_BUFFERS_DISCARD);
            /* fmbm_rodc */
        p_BmiStats->cntWredDiscard =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_WRED_DISCARD);
            /* fmbm_ofwdc */
        p_BmiStats->cntLengthErr =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_LENGTH_ERR);
            /* fmbm_ofledc */
        p_BmiStats->cntUnsupportedFormat =
            FM_PORT_GetCounter(h_FmPort, e_FM_PORT_COUNTERS_UNSUPPRTED_FORMAT);
            /* fmbm_ofufdc */
    }
    return E_OK;
}

uint32_t FM_PORT_GetCounter(t_Handle h_FmPort, e_FmPortCounters counter)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    bool bmiCounter = FALSE;
    enum fman_port_stats_counters statsType;
    enum fman_port_perf_counters perfType;
    enum fman_port_qmi_counters queueType;
    bool isStats;
    t_Error errCode;

    SANITY_CHECK_RETURN_VALUE(p_FmPort, E_INVALID_HANDLE, 0);
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->p_FmPortDriverParam, E_INVALID_STATE);

    switch (counter)
    {
        case (e_FM_PORT_COUNTERS_DEQ_TOTAL):
        case (e_FM_PORT_COUNTERS_DEQ_FROM_DEFAULT):
        case (e_FM_PORT_COUNTERS_DEQ_CONFIRM):
            /* check that counter is available for the port type */
            if ((p_FmPort->portType == e_FM_PORT_TYPE_RX)
                    || (p_FmPort->portType == e_FM_PORT_TYPE_RX_10G))
            {
                REPORT_ERROR(MINOR, E_INVALID_STATE,
                        ("Requested counter is not available for Rx ports"));
                return 0;
            }
            bmiCounter = FALSE;
            break;
        case (e_FM_PORT_COUNTERS_ENQ_TOTAL):
            bmiCounter = FALSE;
            break;
        default: /* BMI counters (or error - will be checked in BMI routine )*/
            bmiCounter = TRUE;
            break;
    }

    if (bmiCounter)
    {
        errCode = BmiPortCheckAndGetCounterType(p_FmPort, counter, &statsType,
                                                &perfType, &isStats);
        if (errCode != E_OK)
        {
            REPORT_ERROR(MINOR, errCode, NO_MSG);
            return 0;
        }
        if (isStats)
            return fman_port_get_stats_counter(&p_FmPort->port, statsType);
        else
            return fman_port_get_perf_counter(&p_FmPort->port, perfType);
    }
    else /* QMI counter */
    {
        /* check that counters are enabled */
        if (!(GET_UINT32(p_FmPort->port.qmi_regs->fmqm_pnc)
                & QMI_PORT_CFG_EN_COUNTERS))

        {
            REPORT_ERROR(MINOR, E_INVALID_STATE, ("Requested counter was not enabled"));
            return 0;
        }

        /* Set counter */
        switch (counter)
        {
            case (e_FM_PORT_COUNTERS_ENQ_TOTAL):
                queueType = E_FMAN_PORT_ENQ_TOTAL;
                break;
            case (e_FM_PORT_COUNTERS_DEQ_TOTAL):
                queueType = E_FMAN_PORT_DEQ_TOTAL;
                break;
            case (e_FM_PORT_COUNTERS_DEQ_FROM_DEFAULT):
                queueType = E_FMAN_PORT_DEQ_FROM_DFLT;
                break;
            case (e_FM_PORT_COUNTERS_DEQ_CONFIRM):
                queueType = E_FMAN_PORT_DEQ_CONFIRM;
                break;
            default:
                REPORT_ERROR(MINOR, E_INVALID_STATE, ("Requested counter is not available"));
                return 0;
        }

        return fman_port_get_qmi_counter(&p_FmPort->port, queueType);
    }

    return 0;
}

t_Error FM_PORT_ModifyCounter(t_Handle h_FmPort, e_FmPortCounters counter,
                              uint32_t value)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    bool bmiCounter = FALSE;
    enum fman_port_stats_counters statsType;
    enum fman_port_perf_counters perfType;
    enum fman_port_qmi_counters queueType;
    bool isStats;
    t_Error errCode;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->p_FmPortDriverParam, E_INVALID_STATE);

    switch (counter)
    {
        case (e_FM_PORT_COUNTERS_DEQ_TOTAL):
        case (e_FM_PORT_COUNTERS_DEQ_FROM_DEFAULT):
        case (e_FM_PORT_COUNTERS_DEQ_CONFIRM):
            /* check that counter is available for the port type */
            if ((p_FmPort->portType == e_FM_PORT_TYPE_RX)
                    || (p_FmPort->portType == e_FM_PORT_TYPE_RX_10G))
                RETURN_ERROR(
                        MINOR, E_INVALID_STATE,
                        ("Requested counter is not available for Rx ports"));
        case (e_FM_PORT_COUNTERS_ENQ_TOTAL):
            bmiCounter = FALSE;
            break;
        default: /* BMI counters (or error - will be checked in BMI routine )*/
            bmiCounter = TRUE;
            break;
    }

    if (bmiCounter)
    {
        errCode = BmiPortCheckAndGetCounterType(p_FmPort, counter, &statsType,
                                                &perfType, &isStats);
        if (errCode != E_OK)
        {
            RETURN_ERROR(MINOR, errCode, NO_MSG);
        }
        if (isStats)
            fman_port_set_stats_counter(&p_FmPort->port, statsType, value);
        else
            fman_port_set_perf_counter(&p_FmPort->port, perfType, value);
    }
    else /* QMI counter */
    {
        /* check that counters are enabled */
        if (!(GET_UINT32(p_FmPort->port.qmi_regs->fmqm_pnc)
                & QMI_PORT_CFG_EN_COUNTERS))
        {
            RETURN_ERROR(MINOR, E_INVALID_STATE,
                         ("Requested counter was not enabled"));
        }

        /* Set counter */
        switch (counter)
        {
            case (e_FM_PORT_COUNTERS_ENQ_TOTAL):
                queueType = E_FMAN_PORT_ENQ_TOTAL;
                break;
            case (e_FM_PORT_COUNTERS_DEQ_TOTAL):
                queueType = E_FMAN_PORT_DEQ_TOTAL;
                break;
            case (e_FM_PORT_COUNTERS_DEQ_FROM_DEFAULT):
                queueType = E_FMAN_PORT_DEQ_FROM_DFLT;
                break;
            case (e_FM_PORT_COUNTERS_DEQ_CONFIRM):
                queueType = E_FMAN_PORT_DEQ_CONFIRM;
                break;
            default:
                RETURN_ERROR(MAJOR, E_INVALID_STATE,
                             ("Requested counter is not available"));
        }

        fman_port_set_qmi_counter(&p_FmPort->port, queueType, value);
    }

    return E_OK;
}

uint32_t FM_PORT_GetAllocBufCounter(t_Handle h_FmPort, uint8_t poolId)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_VALUE(p_FmPort, E_INVALID_HANDLE, 0);
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->p_FmPortDriverParam, E_INVALID_STATE);

    if ((p_FmPort->portType != e_FM_PORT_TYPE_RX)
            && (p_FmPort->portType == e_FM_PORT_TYPE_RX_10G))
    {
        REPORT_ERROR(MINOR, E_INVALID_STATE, ("Requested counter is not available for non-Rx ports"));
        return 0;
    }
    return fman_port_get_bpool_counter(&p_FmPort->port, poolId);
}

t_Error FM_PORT_ModifyAllocBufCounter(t_Handle h_FmPort, uint8_t poolId,
                                      uint32_t value)
{
    t_FmPort *p_FmPort = (t_FmPort *)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->p_FmPortDriverParam, E_INVALID_STATE);

    if ((p_FmPort->portType != e_FM_PORT_TYPE_RX)
            && (p_FmPort->portType == e_FM_PORT_TYPE_RX_10G))
        RETURN_ERROR( MINOR, E_INVALID_STATE,
                     ("Requested counter is not available for non-Rx ports"));

    fman_port_set_bpool_counter(&p_FmPort->port, poolId, value);
    return E_OK;
}
bool FM_PORT_IsStalled(t_Handle h_FmPort)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    t_Error err;
    bool isStalled;

    SANITY_CHECK_RETURN_VALUE(p_FmPort, E_INVALID_HANDLE, FALSE);
    SANITY_CHECK_RETURN_VALUE(!p_FmPort->p_FmPortDriverParam, E_INVALID_STATE,
                              FALSE);

    err = FmIsPortStalled(p_FmPort->h_Fm, p_FmPort->hardwarePortId, &isStalled);
    if (err != E_OK)
    {
        REPORT_ERROR(MAJOR, err, NO_MSG);
        return TRUE;
    }
    return isStalled;
}

t_Error FM_PORT_ReleaseStalled(t_Handle h_FmPort)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->p_FmPortDriverParam, E_INVALID_STATE);

    return FmResumeStalledPort(p_FmPort->h_Fm, p_FmPort->hardwarePortId);
}

t_Error FM_PORT_SetRxL4ChecksumVerify(t_Handle h_FmPort, bool l4Checksum)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    int err;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->p_FmPortDriverParam, E_INVALID_STATE);

    if ((p_FmPort->portType != e_FM_PORT_TYPE_RX_10G)
            && (p_FmPort->portType != e_FM_PORT_TYPE_RX))
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION,
                     ("available for Rx ports only"));

    if (l4Checksum)
        err = fman_port_modify_rx_fd_bits(
                &p_FmPort->port, (uint8_t)(BMI_PORT_RFNE_FRWD_DCL4C >> 24),
                TRUE);
    else
        err = fman_port_modify_rx_fd_bits(
                &p_FmPort->port, (uint8_t)(BMI_PORT_RFNE_FRWD_DCL4C >> 24),
                FALSE);
    if (err != 0)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("fman_port_modify_rx_fd_bits"));

    return E_OK;
}

/*****************************************************************************/
/*       API Run-time PCD Control unit functions                             */
/*****************************************************************************/

#if (DPAA_VERSION >= 11)
t_Error FM_PORT_VSPAlloc(t_Handle h_FmPort, t_FmPortVSPAllocParams *p_VSPParams)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    t_Error err = E_OK;
    volatile uint32_t *p_BmiStorageProfileId = NULL, *p_BmiVspe = NULL;
    uint32_t tmpReg = 0, tmp = 0;
    uint16_t hwStoragePrflId;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->h_Fm, E_INVALID_HANDLE);
    /*for numOfProfiles = 0 don't call this function*/
    SANITY_CHECK_RETURN_ERROR(p_VSPParams->numOfProfiles, E_INVALID_VALUE);
    /*dfltRelativeId should be in the range of numOfProfiles*/
    SANITY_CHECK_RETURN_ERROR(
            p_VSPParams->dfltRelativeId < p_VSPParams->numOfProfiles,
            E_INVALID_VALUE);
    /*p_FmPort should be from Rx type or OP*/
    SANITY_CHECK_RETURN_ERROR(
            ((p_FmPort->portType == e_FM_PORT_TYPE_RX_10G) || (p_FmPort->portType == e_FM_PORT_TYPE_RX) || (p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING)),
            E_INVALID_VALUE);
    /*port should be disabled*/
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->enabled, E_INVALID_STATE);
    /*if its called for Rx port relevant Tx Port should be passed (initialized) too and it should be disabled*/
    SANITY_CHECK_RETURN_ERROR(
            ((p_VSPParams->h_FmTxPort && !((t_FmPort *)(p_VSPParams->h_FmTxPort))->enabled) || (p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING)),
            E_INVALID_VALUE);
    /*should be called before SetPCD - this port should be without PCD*/
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->pcdEngines, E_INVALID_STATE);

    /*alloc window of VSPs for this port*/
    err = FmVSPAllocForPort(p_FmPort->h_Fm, p_FmPort->portType,
                            p_FmPort->portId, p_VSPParams->numOfProfiles);
    if (err != E_OK)
        RETURN_ERROR(MAJOR, err, NO_MSG);

    /*get absolute VSP ID for dfltRelative*/
    err = FmVSPGetAbsoluteProfileId(p_FmPort->h_Fm, p_FmPort->portType,
                                    p_FmPort->portId,
                                    p_VSPParams->dfltRelativeId,
                                    &hwStoragePrflId);
    if (err != E_OK)
        RETURN_ERROR(MAJOR, err, NO_MSG);

    /*fill relevant registers for p_FmPort and relative TxPort in the case p_FmPort from Rx type*/
    switch (p_FmPort->portType)
    {
        case (e_FM_PORT_TYPE_RX_10G):
        case (e_FM_PORT_TYPE_RX):
            p_BmiStorageProfileId =
                    &(((t_FmPort *)(p_VSPParams->h_FmTxPort))->p_FmPortBmiRegs->txPortBmiRegs.fmbm_tcfqid);
            p_BmiVspe =
                    &(((t_FmPort *)(p_VSPParams->h_FmTxPort))->p_FmPortBmiRegs->txPortBmiRegs.fmbm_tfne);

            tmpReg = GET_UINT32(*p_BmiStorageProfileId) & ~BMI_SP_ID_MASK;
            tmpReg |= (uint32_t)hwStoragePrflId << BMI_SP_ID_SHIFT;
            WRITE_UINT32(*p_BmiStorageProfileId, tmpReg);

            tmpReg = GET_UINT32(*p_BmiVspe);
            WRITE_UINT32(*p_BmiVspe, tmpReg | BMI_SP_EN);

            p_BmiStorageProfileId =
                    &p_FmPort->p_FmPortBmiRegs->rxPortBmiRegs.fmbm_rfqid;
            p_BmiVspe = &p_FmPort->p_FmPortBmiRegs->rxPortBmiRegs.fmbm_rpp;
            hwStoragePrflId = p_VSPParams->dfltRelativeId;
            break;

        case (e_FM_PORT_TYPE_OH_OFFLINE_PARSING):
            tmpReg = NIA_ENG_BMI | NIA_BMI_AC_FETCH_ALL_FRAME;
            WRITE_UINT32( p_FmPort->p_FmPortQmiRegs->nonRxQmiRegs.fmqm_pndn,
                         tmpReg);

            p_BmiStorageProfileId =
                    &p_FmPort->p_FmPortBmiRegs->ohPortBmiRegs.fmbm_ofqid;
            p_BmiVspe = &p_FmPort->p_FmPortBmiRegs->ohPortBmiRegs.fmbm_opp;
            tmp |= BMI_EBD_EN;
            break;

        default:
            RETURN_ERROR( MAJOR, E_INVALID_OPERATION,
                         ("available for Rx and offline parsing ports only"));
    }

    p_FmPort->vspe = TRUE;
    p_FmPort->dfltRelativeId = p_VSPParams->dfltRelativeId;

    tmpReg = GET_UINT32(*p_BmiStorageProfileId) & ~BMI_SP_ID_MASK;
    tmpReg |= (uint32_t)hwStoragePrflId << BMI_SP_ID_SHIFT;
    WRITE_UINT32(*p_BmiStorageProfileId, tmpReg);

    tmpReg = GET_UINT32(*p_BmiVspe);
    WRITE_UINT32(*p_BmiVspe, tmpReg | BMI_SP_EN | tmp);
    return E_OK;
}
#endif /* (DPAA_VERSION >= 11) */

t_Error FM_PORT_PcdPlcrAllocProfiles(t_Handle h_FmPort, uint16_t numOfProfiles)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    t_Error err = E_OK;

    p_FmPort->h_FmPcd = FmGetPcdHandle(p_FmPort->h_Fm);
    ASSERT_COND(p_FmPort->h_FmPcd);

    if (!TRY_LOCK(p_FmPort->h_Spinlock, &p_FmPort->lock))
    {
        DBG(TRACE, ("FM Port Try Lock - BUSY"));
        return ERROR_CODE(E_BUSY);
    }

    if (numOfProfiles)
    {
        err = FmPcdPlcrAllocProfiles(p_FmPort->h_FmPcd,
                                     p_FmPort->hardwarePortId, numOfProfiles);
        if (err)
            RETURN_ERROR(MAJOR, err, NO_MSG);
    }
    /* set the port handle within the PCD policer, even if no profiles defined */
    FmPcdPortRegister(p_FmPort->h_FmPcd, h_FmPort, p_FmPort->hardwarePortId);

    RELEASE_LOCK(p_FmPort->lock);

    return E_OK;
}

t_Error FM_PORT_PcdPlcrFreeProfiles(t_Handle h_FmPort)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    t_Error err = E_OK;

    if (!TRY_LOCK(p_FmPort->h_Spinlock, &p_FmPort->lock))
    {
        DBG(TRACE, ("FM Port Try Lock - BUSY"));
        return ERROR_CODE(E_BUSY);
    }

    err = FmPcdPlcrFreeProfiles(p_FmPort->h_FmPcd, p_FmPort->hardwarePortId);

    RELEASE_LOCK(p_FmPort->lock);

    if (err)
        RETURN_ERROR(MAJOR, err, NO_MSG);

    return E_OK;
}

t_Error FM_PORT_PcdKgModifyInitialScheme(t_Handle h_FmPort,
                                         t_FmPcdKgSchemeSelect *p_FmPcdKgScheme)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    volatile uint32_t *p_BmiHpnia = NULL;
    uint32_t tmpReg;
    uint8_t relativeSchemeId;
    uint8_t physicalSchemeId;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->p_FmPortDriverParam, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->pcdEngines & FM_PCD_KG,
                              E_INVALID_STATE);

    tmpReg = (uint32_t)((p_FmPort->pcdEngines & FM_PCD_CC) ? NIA_KG_CC_EN : 0);
    switch (p_FmPort->portType)
    {
        case (e_FM_PORT_TYPE_RX_10G):
        case (e_FM_PORT_TYPE_RX):
            p_BmiHpnia = &p_FmPort->port.bmi_regs->rx.fmbm_rfpne;
            break;
        case (e_FM_PORT_TYPE_OH_OFFLINE_PARSING):
            p_BmiHpnia = &p_FmPort->port.bmi_regs->oh.fmbm_ofpne;
            break;
        default:
            RETURN_ERROR( MAJOR, E_INVALID_OPERATION,
                         ("available for Rx and offline parsing ports only"));
    }

    if (!TRY_LOCK(p_FmPort->h_Spinlock, &p_FmPort->lock))
    {
        DBG(TRACE, ("FM Port Try Lock - BUSY"));
        return ERROR_CODE(E_BUSY);
    }

    /* if we want to change to direct scheme, we need to check that this scheme is valid */
    if (p_FmPcdKgScheme->direct)
    {
        physicalSchemeId = FmPcdKgGetSchemeId(p_FmPcdKgScheme->h_DirectScheme);
        /* check that this scheme is bound to this port */
        if (!(p_FmPort->schemesPerPortVector
                & (uint32_t)(1 << (31 - (uint32_t)physicalSchemeId))))
        {
            RELEASE_LOCK(p_FmPort->lock);
            RETURN_ERROR(
                    MAJOR, E_INVALID_STATE,
                    ("called with a scheme that is not bound to this port"));
        }

        relativeSchemeId = FmPcdKgGetRelativeSchemeId(p_FmPort->h_FmPcd,
                                                      physicalSchemeId);
        if (relativeSchemeId >= FM_PCD_KG_NUM_OF_SCHEMES)
        {
            RELEASE_LOCK(p_FmPort->lock);
            RETURN_ERROR(MAJOR, E_NOT_IN_RANGE,
                         ("called with invalid Scheme "));
        }

        if (!FmPcdKgIsSchemeValidSw(p_FmPcdKgScheme->h_DirectScheme))
        {
            RELEASE_LOCK(p_FmPort->lock);
            RETURN_ERROR(MAJOR, E_INVALID_STATE,
                         ("called with uninitialized Scheme "));
        }

        WRITE_UINT32(
                *p_BmiHpnia,
                NIA_ENG_KG | tmpReg | NIA_KG_DIRECT | (uint32_t)physicalSchemeId);
    }
    else
        /* change to indirect scheme */
        WRITE_UINT32(*p_BmiHpnia, NIA_ENG_KG | tmpReg);
    RELEASE_LOCK(p_FmPort->lock);

    return E_OK;
}

t_Error FM_PORT_PcdPlcrModifyInitialProfile(t_Handle h_FmPort,
                                            t_Handle h_Profile)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    volatile uint32_t *p_BmiNia;
    volatile uint32_t *p_BmiHpnia;
    uint32_t tmpReg;
    uint16_t absoluteProfileId = FmPcdPlcrProfileGetAbsoluteId(h_Profile);

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->p_FmPortDriverParam, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->pcdEngines & FM_PCD_PLCR,
                              E_INVALID_STATE);

    /* check relevance of this routine  - only when policer is used
     directly after BMI or Parser */
    if ((p_FmPort->pcdEngines & FM_PCD_KG)
            || (p_FmPort->pcdEngines & FM_PCD_CC))
        RETURN_ERROR(
                MAJOR,
                E_INVALID_STATE,
                ("relevant only when PCD support mode is e_FM_PCD_SUPPORT_PLCR_ONLY or e_FM_PCD_SUPPORT_PRS_AND_PLCR"));

    switch (p_FmPort->portType)
    {
        case (e_FM_PORT_TYPE_RX_10G):
        case (e_FM_PORT_TYPE_RX):
            p_BmiNia = &p_FmPort->port.bmi_regs->rx.fmbm_rfne;
            p_BmiHpnia = &p_FmPort->port.bmi_regs->rx.fmbm_rfpne;
            tmpReg = GET_UINT32(*p_BmiNia) & BMI_RFNE_FDCS_MASK;
            break;
        case (e_FM_PORT_TYPE_OH_OFFLINE_PARSING):
            p_BmiNia = &p_FmPort->port.bmi_regs->oh.fmbm_ofne;
            p_BmiHpnia = &p_FmPort->port.bmi_regs->oh.fmbm_ofpne;
            tmpReg = 0;
            break;
        default:
            RETURN_ERROR( MAJOR, E_INVALID_OPERATION,
                         ("available for Rx and offline parsing ports only"));
    }

    if (!TRY_LOCK(p_FmPort->h_Spinlock, &p_FmPort->lock))
    {
        DBG(TRACE, ("FM Port Try Lock - BUSY"));
        return ERROR_CODE(E_BUSY);
    }

    if (!FmPcdPlcrIsProfileValid(p_FmPort->h_FmPcd, absoluteProfileId))
    {
        RELEASE_LOCK(p_FmPort->lock);
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION, ("Invalid profile"));
    }

    tmpReg |= (uint32_t)(NIA_ENG_PLCR | NIA_PLCR_ABSOLUTE | absoluteProfileId);

    if (p_FmPort->pcdEngines & FM_PCD_PRS) /* e_FM_PCD_SUPPORT_PRS_AND_PLCR */
    {
        /* update BMI HPNIA */
        WRITE_UINT32(*p_BmiHpnia, tmpReg);
    }
    else /* e_FM_PCD_SUPPORT_PLCR_ONLY */
    {
        /* rfne may contain FDCS bits, so first we read them. */
        tmpReg |= (GET_UINT32(*p_BmiNia) & BMI_RFNE_FDCS_MASK);
        /* update BMI NIA */
        WRITE_UINT32(*p_BmiNia, tmpReg);
    }RELEASE_LOCK(p_FmPort->lock);

    return E_OK;
}

t_Error FM_PORT_PcdCcModifyTree(t_Handle h_FmPort, t_Handle h_CcTree)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    t_Error err = E_OK;
    volatile uint32_t *p_BmiCcBase = NULL;
    volatile uint32_t *p_BmiNia = NULL;
    uint32_t ccTreePhysOffset;

    SANITY_CHECK_RETURN_ERROR(h_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(h_CcTree, E_INVALID_HANDLE);

    if (p_FmPort->imEn)
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION,
                     ("available for non-independent mode ports only"));

    /* get PCD registers pointers */
    switch (p_FmPort->portType)
    {
        case (e_FM_PORT_TYPE_RX_10G):
        case (e_FM_PORT_TYPE_RX):
            p_BmiNia = &p_FmPort->port.bmi_regs->rx.fmbm_rfne;
            break;
        case (e_FM_PORT_TYPE_OH_OFFLINE_PARSING):
            p_BmiNia = &p_FmPort->port.bmi_regs->oh.fmbm_ofne;
            break;
        default:
            RETURN_ERROR( MAJOR, E_INVALID_OPERATION,
                         ("available for Rx and offline parsing ports only"));
    }

    /* check that current NIA is BMI to BMI */
    if ((GET_UINT32(*p_BmiNia) & ~BMI_RFNE_FDCS_MASK)
            != GET_NIA_BMI_AC_ENQ_FRAME(p_FmPort->h_FmPcd))
        RETURN_ERROR( MAJOR, E_INVALID_OPERATION,
                     ("may be called only for ports in BMI-to-BMI state."));

    if (p_FmPort->pcdEngines & FM_PCD_CC)
    {
        if (p_FmPort->h_IpReassemblyManip)
        {
            err = FmPcdCcTreeAddIPR(p_FmPort->h_FmPcd, h_CcTree, NULL,
                                    p_FmPort->h_IpReassemblyManip, FALSE);
            if (err != E_OK)
            {
                RETURN_ERROR(MAJOR, err, NO_MSG);
            }
        }
        else
            if (p_FmPort->h_CapwapReassemblyManip)
            {
                err = FmPcdCcTreeAddCPR(p_FmPort->h_FmPcd, h_CcTree, NULL,
                                        p_FmPort->h_CapwapReassemblyManip,
                                        FALSE);
                if (err != E_OK)
                {
                    RETURN_ERROR(MAJOR, err, NO_MSG);
                }
            }
        switch (p_FmPort->portType)
        {
            case (e_FM_PORT_TYPE_RX_10G):
            case (e_FM_PORT_TYPE_RX):
                p_BmiCcBase = &p_FmPort->port.bmi_regs->rx.fmbm_rccb;
                break;
            case (e_FM_PORT_TYPE_OH_OFFLINE_PARSING):
                p_BmiCcBase = &p_FmPort->port.bmi_regs->oh.fmbm_occb;
                break;
            default:
                RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Invalid port type"));
        }

        if (!TRY_LOCK(p_FmPort->h_Spinlock, &p_FmPort->lock))
        {
            DBG(TRACE, ("FM Port Try Lock - BUSY"));
            return ERROR_CODE(E_BUSY);
        }
        err = FmPcdCcBindTree(p_FmPort->h_FmPcd, NULL, h_CcTree,
                              &ccTreePhysOffset, h_FmPort);
        if (err)
        {
            RELEASE_LOCK(p_FmPort->lock);
            RETURN_ERROR(MAJOR, err, NO_MSG);
        }WRITE_UINT32(*p_BmiCcBase, ccTreePhysOffset);

        p_FmPort->ccTreeId = h_CcTree;
        RELEASE_LOCK(p_FmPort->lock);
    }
    else
        RETURN_ERROR( MAJOR, E_INVALID_STATE,
                     ("Coarse Classification not defined for this port."));

    return E_OK;
}

t_Error FM_PORT_AttachPCD(t_Handle h_FmPort)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    t_Error err = E_OK;

    SANITY_CHECK_RETURN_ERROR(h_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->p_FmPortDriverParam, E_INVALID_STATE);

    if (p_FmPort->imEn)
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION,
                     ("available for non-independent mode ports only"));

    if ((p_FmPort->portType != e_FM_PORT_TYPE_RX_10G)
            && (p_FmPort->portType != e_FM_PORT_TYPE_RX)
            && (p_FmPort->portType != e_FM_PORT_TYPE_OH_OFFLINE_PARSING))
        RETURN_ERROR( MAJOR, E_INVALID_OPERATION,
                     ("available for Rx and offline parsing ports only"));

    if (!TRY_LOCK(p_FmPort->h_Spinlock, &p_FmPort->lock))
    {
        DBG(TRACE, ("FM Port Try Lock - BUSY"));
        return ERROR_CODE(E_BUSY);
    }

    if (p_FmPort->h_ReassemblyTree)
        p_FmPort->pcdEngines |= FM_PCD_CC;

    err = AttachPCD(h_FmPort);
    RELEASE_LOCK(p_FmPort->lock);

    return err;
}

t_Error FM_PORT_DetachPCD(t_Handle h_FmPort)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    t_Error err = E_OK;

    SANITY_CHECK_RETURN_ERROR(h_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->p_FmPortDriverParam, E_INVALID_STATE);

    if (p_FmPort->imEn)
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION,
                     ("available for non-independent mode ports only"));

    if ((p_FmPort->portType != e_FM_PORT_TYPE_RX_10G)
            && (p_FmPort->portType != e_FM_PORT_TYPE_RX)
            && (p_FmPort->portType != e_FM_PORT_TYPE_OH_OFFLINE_PARSING))
        RETURN_ERROR( MAJOR, E_INVALID_OPERATION,
                     ("available for Rx and offline parsing ports only"));

    if (!TRY_LOCK(p_FmPort->h_Spinlock, &p_FmPort->lock))
    {
        DBG(TRACE, ("FM Port Try Lock - BUSY"));
        return ERROR_CODE(E_BUSY);
    }

    err = DetachPCD(h_FmPort);
    if (err != E_OK)
    {
        RELEASE_LOCK(p_FmPort->lock);
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    if (p_FmPort->h_ReassemblyTree)
        p_FmPort->pcdEngines &= ~FM_PCD_CC;
    RELEASE_LOCK(p_FmPort->lock);

    return E_OK;
}

t_Error FM_PORT_SetPCD(t_Handle h_FmPort, t_FmPortPcdParams *p_PcdParam)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    t_Error err = E_OK;
    t_FmPortPcdParams modifiedPcdParams, *p_PcdParams;
    t_FmPcdCcTreeParams *p_FmPcdCcTreeParams;
    t_FmPortPcdCcParams fmPortPcdCcParams;
    t_FmPortGetSetCcParams fmPortGetSetCcParams;

    SANITY_CHECK_RETURN_ERROR(h_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_PcdParam, E_NULL_POINTER);
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->p_FmPortDriverParam, E_INVALID_STATE);

    if (p_FmPort->imEn)
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION,
                     ("available for non-independent mode ports only"));

    if ((p_FmPort->portType != e_FM_PORT_TYPE_RX_10G)
            && (p_FmPort->portType != e_FM_PORT_TYPE_RX)
            && (p_FmPort->portType != e_FM_PORT_TYPE_OH_OFFLINE_PARSING))
        RETURN_ERROR( MAJOR, E_INVALID_OPERATION,
                     ("available for Rx and offline parsing ports only"));

    if (!TRY_LOCK(p_FmPort->h_Spinlock, &p_FmPort->lock))
    {
        DBG(TRACE, ("FM Port Try Lock - BUSY"));
        return ERROR_CODE(E_BUSY);
    }

    p_FmPort->h_FmPcd = FmGetPcdHandle(p_FmPort->h_Fm);
    ASSERT_COND(p_FmPort->h_FmPcd);

    if (p_PcdParam->p_CcParams && !p_PcdParam->p_CcParams->h_CcTree)
        RETURN_ERROR(MAJOR, E_INVALID_HANDLE,
                     ("Tree handle must be given if CC is required"));

    memcpy(&modifiedPcdParams, p_PcdParam, sizeof(t_FmPortPcdParams));
    p_PcdParams = &modifiedPcdParams;
    if ((p_PcdParams->h_IpReassemblyManip)
#if (DPAA_VERSION >= 11)
            || (p_PcdParams->h_CapwapReassemblyManip)
#endif /* (DPAA_VERSION >= 11) */
            )
    {
        if ((p_PcdParams->pcdSupport != e_FM_PORT_PCD_SUPPORT_PRS_AND_KG)
                && (p_PcdParams->pcdSupport
                        != e_FM_PORT_PCD_SUPPORT_PRS_AND_KG_AND_CC)
                && (p_PcdParams->pcdSupport
                        != e_FM_PORT_PCD_SUPPORT_PRS_AND_KG_AND_CC_AND_PLCR)
                && (p_PcdParams->pcdSupport
                        != e_FM_PORT_PCD_SUPPORT_PRS_AND_KG_AND_PLCR))
        {
            RELEASE_LOCK(p_FmPort->lock);
            RETURN_ERROR( MAJOR, E_INVALID_STATE,
                         ("pcdSupport must have KG for supporting Reassembly"));
        }
        p_FmPort->h_IpReassemblyManip = p_PcdParams->h_IpReassemblyManip;
#if (DPAA_VERSION >= 11)
        if ((p_PcdParams->h_IpReassemblyManip)
                && (p_PcdParams->h_CapwapReassemblyManip))
            RETURN_ERROR(MAJOR, E_INVALID_STATE,
                         ("Either IP-R or CAPWAP-R is allowed"));
        if ((p_PcdParams->h_CapwapReassemblyManip)
                && (p_FmPort->portType != e_FM_PORT_TYPE_OH_OFFLINE_PARSING))
            RETURN_ERROR(MAJOR, E_INVALID_STATE,
                         ("CAPWAP-R is allowed only on offline-port"));
        if (p_PcdParams->h_CapwapReassemblyManip)
            p_FmPort->h_CapwapReassemblyManip =
                    p_PcdParams->h_CapwapReassemblyManip;
#endif /* (DPAA_VERSION >= 11) */

        if (!p_PcdParams->p_CcParams)
        {
            if (!((p_PcdParams->pcdSupport == e_FM_PORT_PCD_SUPPORT_PRS_AND_KG)
                    || (p_PcdParams->pcdSupport
                            == e_FM_PORT_PCD_SUPPORT_PRS_AND_KG_AND_PLCR)))
            {
                RELEASE_LOCK(p_FmPort->lock);
                RETURN_ERROR(
                        MAJOR,
                        E_INVALID_STATE,
                        ("PCD initialization structure is not consistent with pcdSupport"));
            }

            /* No user-tree, need to build internal tree */
            p_FmPcdCcTreeParams = (t_FmPcdCcTreeParams*)XX_Malloc(
                    sizeof(t_FmPcdCcTreeParams));
            if (!p_FmPcdCcTreeParams)
                RETURN_ERROR(MAJOR, E_NO_MEMORY, ("p_FmPcdCcTreeParams"));
            memset(p_FmPcdCcTreeParams, 0, sizeof(t_FmPcdCcTreeParams));
            p_FmPcdCcTreeParams->h_NetEnv = p_PcdParams->h_NetEnv;
            p_FmPort->h_ReassemblyTree = FM_PCD_CcRootBuild(
                    p_FmPort->h_FmPcd, p_FmPcdCcTreeParams);

            if (!p_FmPort->h_ReassemblyTree)
            {
                RELEASE_LOCK(p_FmPort->lock);
                XX_Free(p_FmPcdCcTreeParams);
                RETURN_ERROR( MAJOR, E_INVALID_HANDLE,
                             ("FM_PCD_CcBuildTree for Reassembly failed"));
            }
            if (p_PcdParams->pcdSupport == e_FM_PORT_PCD_SUPPORT_PRS_AND_KG)
                p_PcdParams->pcdSupport =
                        e_FM_PORT_PCD_SUPPORT_PRS_AND_KG_AND_CC;
            else
                p_PcdParams->pcdSupport =
                        e_FM_PORT_PCD_SUPPORT_PRS_AND_KG_AND_CC_AND_PLCR;

            memset(&fmPortPcdCcParams, 0, sizeof(t_FmPortPcdCcParams));
            fmPortPcdCcParams.h_CcTree = p_FmPort->h_ReassemblyTree;
            p_PcdParams->p_CcParams = &fmPortPcdCcParams;
            XX_Free(p_FmPcdCcTreeParams);
        }

        if (p_FmPort->h_IpReassemblyManip)
            err = FmPcdCcTreeAddIPR(p_FmPort->h_FmPcd,
                                    p_PcdParams->p_CcParams->h_CcTree,
                                    p_PcdParams->h_NetEnv,
                                    p_FmPort->h_IpReassemblyManip, TRUE);
#if (DPAA_VERSION >= 11)
        else
            if (p_FmPort->h_CapwapReassemblyManip)
                err = FmPcdCcTreeAddCPR(p_FmPort->h_FmPcd,
                                        p_PcdParams->p_CcParams->h_CcTree,
                                        p_PcdParams->h_NetEnv,
                                        p_FmPort->h_CapwapReassemblyManip,
                                        TRUE);
#endif /* (DPAA_VERSION >= 11) */

        if (err != E_OK)
        {
            if (p_FmPort->h_ReassemblyTree)
            {
                FM_PCD_CcRootDelete(p_FmPort->h_ReassemblyTree);
                p_FmPort->h_ReassemblyTree = NULL;
            }RELEASE_LOCK(p_FmPort->lock);
            RETURN_ERROR(MAJOR, err, NO_MSG);
        }
    }

    if (!FmPcdLockTryLockAll(p_FmPort->h_FmPcd))
    {
        if (p_FmPort->h_ReassemblyTree)
        {
            FM_PCD_CcRootDelete(p_FmPort->h_ReassemblyTree);
            p_FmPort->h_ReassemblyTree = NULL;
        }RELEASE_LOCK(p_FmPort->lock);
        DBG(TRACE, ("Try LockAll - BUSY"));
        return ERROR_CODE(E_BUSY);
    }

    err = SetPcd(h_FmPort, p_PcdParams);
    if (err)
    {
        if (p_FmPort->h_ReassemblyTree)
        {
            FM_PCD_CcRootDelete(p_FmPort->h_ReassemblyTree);
            p_FmPort->h_ReassemblyTree = NULL;
        }
        FmPcdLockUnlockAll(p_FmPort->h_FmPcd);
        RELEASE_LOCK(p_FmPort->lock);
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    if ((p_FmPort->pcdEngines & FM_PCD_PRS)
            && (p_PcdParams->p_PrsParams->includeInPrsStatistics))
    {
        err = FmPcdPrsIncludePortInStatistics(p_FmPort->h_FmPcd,
                                              p_FmPort->hardwarePortId, TRUE);
        if (err)
        {
            DeletePcd(p_FmPort);
            if (p_FmPort->h_ReassemblyTree)
            {
                FM_PCD_CcRootDelete(p_FmPort->h_ReassemblyTree);
                p_FmPort->h_ReassemblyTree = NULL;
            }
            FmPcdLockUnlockAll(p_FmPort->h_FmPcd);
            RELEASE_LOCK(p_FmPort->lock);
            RETURN_ERROR(MAJOR, err, NO_MSG);
        }
        p_FmPort->includeInPrsStatistics = TRUE;
    }

    FmPcdIncNetEnvOwners(p_FmPort->h_FmPcd, p_FmPort->netEnvId);

    if (FmPcdIsAdvancedOffloadSupported(p_FmPort->h_FmPcd))
    {
        memset(&fmPortGetSetCcParams, 0, sizeof(t_FmPortGetSetCcParams));

        if (p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING)
        {
#ifdef FM_KG_ERASE_FLOW_ID_ERRATA_FMAN_SW004
            if ((p_FmPort->fmRevInfo.majorRev < 6) &&
                    (p_FmPort->pcdEngines & FM_PCD_KG))
            {
                int i;
                for (i = 0; i<p_PcdParams->p_KgParams->numOfSchemes; i++)
                /* The following function must be locked */
                FmPcdKgCcGetSetParams(p_FmPort->h_FmPcd,
                        p_PcdParams->p_KgParams->h_Schemes[i],
                        UPDATE_KG_NIA_CC_WA,
                        0);
            }
#endif /* FM_KG_ERASE_FLOW_ID_ERRATA_FMAN_SW004 */

#if (DPAA_VERSION >= 11)
            {
                t_FmPcdCtrlParamsPage *p_ParamsPage;

                FmPortSetGprFunc(p_FmPort, e_FM_PORT_GPR_MURAM_PAGE,
                                 (void**)&p_ParamsPage);
                ASSERT_COND(p_ParamsPage);
                WRITE_UINT32(p_ParamsPage->postBmiFetchNia,
                             p_FmPort->savedBmiNia);
            }
#endif /* (DPAA_VERSION >= 11) */

            /* Set post-bmi-fetch nia */
            p_FmPort->savedBmiNia &= BMI_RFNE_FDCS_MASK;
            p_FmPort->savedBmiNia |= (NIA_FM_CTL_AC_POST_BMI_FETCH
                    | NIA_ENG_FM_CTL);

            /* Set pre-bmi-fetch nia */
            fmPortGetSetCcParams.setCcParams.type = UPDATE_NIA_PNDN;
#if (DPAA_VERSION >= 11)
            fmPortGetSetCcParams.setCcParams.nia =
                    (NIA_FM_CTL_AC_PRE_BMI_FETCH_FULL_FRAME | NIA_ENG_FM_CTL);
#else
            fmPortGetSetCcParams.setCcParams.nia = (NIA_FM_CTL_AC_PRE_BMI_FETCH_HEADER | NIA_ENG_FM_CTL);
#endif /* (DPAA_VERSION >= 11) */
            if ((err = FmPortGetSetCcParams(p_FmPort, &fmPortGetSetCcParams))
                    != E_OK)
            {
                DeletePcd(p_FmPort);
                if (p_FmPort->h_ReassemblyTree)
                {
                    FM_PCD_CcRootDelete(p_FmPort->h_ReassemblyTree);
                    p_FmPort->h_ReassemblyTree = NULL;
                }
                FmPcdLockUnlockAll(p_FmPort->h_FmPcd);
                RELEASE_LOCK(p_FmPort->lock);
                RETURN_ERROR(MAJOR, err, NO_MSG);
            }
        }

        FmPcdLockUnlockAll(p_FmPort->h_FmPcd);

        /* Set pop-to-next-step nia */
#if (DPAA_VERSION == 10)
        if (p_FmPort->fmRevInfo.majorRev < 6)
        {
            fmPortGetSetCcParams.setCcParams.type = UPDATE_NIA_PNEN;
            fmPortGetSetCcParams.setCcParams.nia = NIA_FM_CTL_AC_POP_TO_N_STEP | NIA_ENG_FM_CTL;
        }
        else
        {
#endif /* (DPAA_VERSION == 10) */
        fmPortGetSetCcParams.getCcParams.type = GET_NIA_FPNE;
#if (DPAA_VERSION == 10)
    }
#endif /* (DPAA_VERSION == 10) */
        if ((err = FmPortGetSetCcParams(h_FmPort, &fmPortGetSetCcParams))
                != E_OK)
        {
            DeletePcd(p_FmPort);
            if (p_FmPort->h_ReassemblyTree)
            {
                FM_PCD_CcRootDelete(p_FmPort->h_ReassemblyTree);
                p_FmPort->h_ReassemblyTree = NULL;
            }RELEASE_LOCK(p_FmPort->lock);
            RETURN_ERROR(MAJOR, err, NO_MSG);
        }

        /* Set post-bmi-prepare-to-enq nia */
        fmPortGetSetCcParams.setCcParams.type = UPDATE_NIA_FENE;
        fmPortGetSetCcParams.setCcParams.nia = (NIA_FM_CTL_AC_POST_BMI_ENQ
                | NIA_ENG_FM_CTL);
        if ((err = FmPortGetSetCcParams(h_FmPort, &fmPortGetSetCcParams))
                != E_OK)
        {
            DeletePcd(p_FmPort);
            if (p_FmPort->h_ReassemblyTree)
            {
                FM_PCD_CcRootDelete(p_FmPort->h_ReassemblyTree);
                p_FmPort->h_ReassemblyTree = NULL;
            }RELEASE_LOCK(p_FmPort->lock);
            RETURN_ERROR(MAJOR, err, NO_MSG);
        }

        if ((p_FmPort->h_IpReassemblyManip)
                || (p_FmPort->h_CapwapReassemblyManip))
        {
#if (DPAA_VERSION == 10)
            if (p_FmPort->fmRevInfo.majorRev < 6)
            {
                /* Overwrite post-bmi-prepare-to-enq nia */
                fmPortGetSetCcParams.setCcParams.type = UPDATE_NIA_FENE;
                fmPortGetSetCcParams.setCcParams.nia = (NIA_FM_CTL_AC_POST_BMI_ENQ_ORR | NIA_ENG_FM_CTL | NIA_ORDER_RESTOR);
                fmPortGetSetCcParams.setCcParams.overwrite = TRUE;
            }
            else
            {
#endif /* (DPAA_VERSION == 10) */
            /* Set the ORR bit (for order-restoration) */
            fmPortGetSetCcParams.setCcParams.type = UPDATE_NIA_FPNE;
            fmPortGetSetCcParams.setCcParams.nia =
                    fmPortGetSetCcParams.getCcParams.nia | NIA_ORDER_RESTOR;
#if (DPAA_VERSION == 10)
        }
#endif /* (DPAA_VERSION == 10) */
            if ((err = FmPortGetSetCcParams(h_FmPort, &fmPortGetSetCcParams))
                    != E_OK)
            {
                DeletePcd(p_FmPort);
                if (p_FmPort->h_ReassemblyTree)
                {
                    FM_PCD_CcRootDelete(p_FmPort->h_ReassemblyTree);
                    p_FmPort->h_ReassemblyTree = NULL;
                }RELEASE_LOCK(p_FmPort->lock);
                RETURN_ERROR(MAJOR, err, NO_MSG);
            }
        }
    }
    else
        FmPcdLockUnlockAll(p_FmPort->h_FmPcd);

#if (DPAA_VERSION >= 11)
    {
        t_FmPcdCtrlParamsPage *p_ParamsPage;

        memset(&fmPortGetSetCcParams, 0, sizeof(t_FmPortGetSetCcParams));

        fmPortGetSetCcParams.setCcParams.type = UPDATE_NIA_CMNE;
        if (FmPcdIsAdvancedOffloadSupported(p_FmPort->h_FmPcd))
            fmPortGetSetCcParams.setCcParams.nia = NIA_FM_CTL_AC_POP_TO_N_STEP
                    | NIA_ENG_FM_CTL;
        else
            fmPortGetSetCcParams.setCcParams.nia =
                    NIA_FM_CTL_AC_NO_IPACC_POP_TO_N_STEP | NIA_ENG_FM_CTL;
        if ((err = FmPortGetSetCcParams(h_FmPort, &fmPortGetSetCcParams))
                != E_OK)
        {
            DeletePcd(p_FmPort);
            if (p_FmPort->h_ReassemblyTree)
            {
                FM_PCD_CcRootDelete(p_FmPort->h_ReassemblyTree);
                p_FmPort->h_ReassemblyTree = NULL;
            }RELEASE_LOCK(p_FmPort->lock);
            RETURN_ERROR(MAJOR, err, NO_MSG);
        }

        FmPortSetGprFunc(p_FmPort, e_FM_PORT_GPR_MURAM_PAGE,
                         (void**)&p_ParamsPage);
        ASSERT_COND(p_ParamsPage);

        if (FmPcdIsAdvancedOffloadSupported(p_FmPort->h_FmPcd))
            WRITE_UINT32(
                    p_ParamsPage->misc,
                    GET_UINT32(p_ParamsPage->misc) | FM_CTL_PARAMS_PAGE_OFFLOAD_SUPPORT_EN);

        if ((p_FmPort->h_IpReassemblyManip)
                || (p_FmPort->h_CapwapReassemblyManip))
        {
            if (p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING)
                WRITE_UINT32(
                        p_ParamsPage->discardMask,
                        GET_UINT32(p_FmPort->p_FmPortBmiRegs->ohPortBmiRegs.fmbm_ofsdm));
            else
                WRITE_UINT32(
                        p_ParamsPage->discardMask,
                        GET_UINT32(p_FmPort->p_FmPortBmiRegs->rxPortBmiRegs.fmbm_rfsdm));
        }
#ifdef FM_ERROR_VSP_NO_MATCH_SW006
        if (p_FmPort->vspe)
            WRITE_UINT32(
                    p_ParamsPage->misc,
                    GET_UINT32(p_ParamsPage->misc) | (p_FmPort->dfltRelativeId & FM_CTL_PARAMS_PAGE_ERROR_VSP_MASK));
#endif /* FM_ERROR_VSP_NO_MATCH_SW006 */
    }
#endif /* (DPAA_VERSION >= 11) */

    err = AttachPCD(h_FmPort);
    if (err)
    {
        DeletePcd(p_FmPort);
        if (p_FmPort->h_ReassemblyTree)
        {
            FM_PCD_CcRootDelete(p_FmPort->h_ReassemblyTree);
            p_FmPort->h_ReassemblyTree = NULL;
        }RELEASE_LOCK(p_FmPort->lock);
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    RELEASE_LOCK(p_FmPort->lock);

    return err;
}

t_Error FM_PORT_DeletePCD(t_Handle h_FmPort)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    t_Error err = E_OK;

    SANITY_CHECK_RETURN_ERROR(h_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->p_FmPortDriverParam, E_INVALID_STATE);

    if (p_FmPort->imEn)
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION,
                     ("available for non-independant mode ports only"));

    if ((p_FmPort->portType != e_FM_PORT_TYPE_RX_10G)
            && (p_FmPort->portType != e_FM_PORT_TYPE_RX)
            && (p_FmPort->portType != e_FM_PORT_TYPE_OH_OFFLINE_PARSING))
        RETURN_ERROR( MAJOR, E_INVALID_OPERATION,
                     ("available for Rx and offline parsing ports only"));

    if (!TRY_LOCK(p_FmPort->h_Spinlock, &p_FmPort->lock))
    {
        DBG(TRACE, ("FM Port Try Lock - BUSY"));
        return ERROR_CODE(E_BUSY);
    }

    err = DetachPCD(h_FmPort);
    if (err)
    {
        RELEASE_LOCK(p_FmPort->lock);
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    FmPcdDecNetEnvOwners(p_FmPort->h_FmPcd, p_FmPort->netEnvId);

    /* we do it anyway, instead of checking if included */
    if ((p_FmPort->pcdEngines & FM_PCD_PRS) && p_FmPort->includeInPrsStatistics)
    {
        FmPcdPrsIncludePortInStatistics(p_FmPort->h_FmPcd,
                                        p_FmPort->hardwarePortId, FALSE);
        p_FmPort->includeInPrsStatistics = FALSE;
    }

    if (!FmPcdLockTryLockAll(p_FmPort->h_FmPcd))
    {
        RELEASE_LOCK(p_FmPort->lock);
        DBG(TRACE, ("Try LockAll - BUSY"));
        return ERROR_CODE(E_BUSY);
    }

    err = DeletePcd(h_FmPort);
    FmPcdLockUnlockAll(p_FmPort->h_FmPcd);
    if (err)
    {
        RELEASE_LOCK(p_FmPort->lock);
        RETURN_ERROR(MAJOR, err, NO_MSG);
    }

    if (p_FmPort->h_ReassemblyTree)
    {
        err = FM_PCD_CcRootDelete(p_FmPort->h_ReassemblyTree);
        if (err)
        {
            RELEASE_LOCK(p_FmPort->lock);
            RETURN_ERROR(MAJOR, err, NO_MSG);
        }
        p_FmPort->h_ReassemblyTree = NULL;
    }RELEASE_LOCK(p_FmPort->lock);

    return err;
}

t_Error FM_PORT_PcdKgBindSchemes(t_Handle h_FmPort,
                                 t_FmPcdPortSchemesParams *p_PortScheme)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    t_FmPcdKgInterModuleBindPortToSchemes schemeBind;
    t_Error err = E_OK;
    uint32_t tmpScmVec = 0;
    int i;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->p_FmPortDriverParam, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->pcdEngines & FM_PCD_KG,
                              E_INVALID_STATE);

    schemeBind.netEnvId = p_FmPort->netEnvId;
    schemeBind.hardwarePortId = p_FmPort->hardwarePortId;
    schemeBind.numOfSchemes = p_PortScheme->numOfSchemes;
    schemeBind.useClsPlan = p_FmPort->useClsPlan;
    for (i = 0; i < schemeBind.numOfSchemes; i++)
    {
        schemeBind.schemesIds[i] = FmPcdKgGetSchemeId(
                p_PortScheme->h_Schemes[i]);
        /* build vector */
        tmpScmVec |= 1 << (31 - (uint32_t)schemeBind.schemesIds[i]);
    }

    if (!TRY_LOCK(p_FmPort->h_Spinlock, &p_FmPort->lock))
    {
        DBG(TRACE, ("FM Port Try Lock - BUSY"));
        return ERROR_CODE(E_BUSY);
    }

    err = FmPcdKgBindPortToSchemes(p_FmPort->h_FmPcd, &schemeBind);
    if (err == E_OK)
        p_FmPort->schemesPerPortVector |= tmpScmVec;

#ifdef FM_KG_ERASE_FLOW_ID_ERRATA_FMAN_SW004
    if ((FmPcdIsAdvancedOffloadSupported(p_FmPort->h_FmPcd)) &&
            (p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING) &&
            (p_FmPort->fmRevInfo.majorRev < 6))
    {
        for (i=0; i<p_PortScheme->numOfSchemes; i++)
        FmPcdKgCcGetSetParams(p_FmPort->h_FmPcd, p_PortScheme->h_Schemes[i], UPDATE_KG_NIA_CC_WA, 0);
    }
#endif /* FM_KG_ERASE_FLOW_ID_ERRATA_FMAN_SW004 */

    RELEASE_LOCK(p_FmPort->lock);

    return err;
}

t_Error FM_PORT_PcdKgUnbindSchemes(t_Handle h_FmPort,
                                   t_FmPcdPortSchemesParams *p_PortScheme)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    t_FmPcdKgInterModuleBindPortToSchemes schemeBind;
    t_Error err = E_OK;
    uint32_t tmpScmVec = 0;
    int i;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->p_FmPortDriverParam, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->pcdEngines & FM_PCD_KG,
                              E_INVALID_STATE);

    schemeBind.netEnvId = p_FmPort->netEnvId;
    schemeBind.hardwarePortId = p_FmPort->hardwarePortId;
    schemeBind.numOfSchemes = p_PortScheme->numOfSchemes;
    for (i = 0; i < schemeBind.numOfSchemes; i++)
    {
        schemeBind.schemesIds[i] = FmPcdKgGetSchemeId(
                p_PortScheme->h_Schemes[i]);
        /* build vector */
        tmpScmVec |= 1 << (31 - (uint32_t)schemeBind.schemesIds[i]);
    }

    if (!TRY_LOCK(p_FmPort->h_Spinlock, &p_FmPort->lock))
    {
        DBG(TRACE, ("FM Port Try Lock - BUSY"));
        return ERROR_CODE(E_BUSY);
    }

    err = FmPcdKgUnbindPortToSchemes(p_FmPort->h_FmPcd, &schemeBind);
    if (err == E_OK)
        p_FmPort->schemesPerPortVector &= ~tmpScmVec;
    RELEASE_LOCK(p_FmPort->lock);

    return err;
}

t_Error FM_PORT_AddCongestionGrps(t_Handle h_FmPort,
                                  t_FmPortCongestionGrps *p_CongestionGrps)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    uint8_t priorityTmpArray[FM_PORT_NUM_OF_CONGESTION_GRPS];
    uint8_t mod, index;
    uint32_t i, grpsMap[FMAN_PORT_CG_MAP_NUM];
    int err;
#if (DPAA_VERSION >= 11)
    int j;
#endif /* (DPAA_VERSION >= 11) */

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);

    /* un-necessary check of the indexes; probably will be needed in the future when there
     will be more CGs available ....
     for (i=0; i<p_CongestionGrps->numOfCongestionGrpsToConsider; i++)
     if (p_CongestionGrps->congestionGrpsToConsider[i] >= FM_PORT_NUM_OF_CONGESTION_GRPS)
     RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("CG id!"));
     */

#ifdef FM_NO_OP_OBSERVED_CGS
    if ((p_FmPort->fmRevInfo.majorRev != 4) &&
            (p_FmPort->fmRevInfo.majorRev < 6))
    {
        if ((p_FmPort->portType != e_FM_PORT_TYPE_RX_10G) &&
                (p_FmPort->portType != e_FM_PORT_TYPE_RX))
        RETURN_ERROR(MAJOR, E_NOT_SUPPORTED, ("Available for Rx ports only"));
    }
    else
#endif /* FM_NO_OP_OBSERVED_CGS */
    if ((p_FmPort->portType != e_FM_PORT_TYPE_RX_10G)
            && (p_FmPort->portType != e_FM_PORT_TYPE_RX)
            && (p_FmPort->portType != e_FM_PORT_TYPE_OH_OFFLINE_PARSING))
        RETURN_ERROR(MAJOR, E_NOT_SUPPORTED,
                     ("Available for Rx & OP ports only"));

    /* Prepare groups map array */
    memset(grpsMap, 0, FMAN_PORT_CG_MAP_NUM * sizeof(uint32_t));
    for (i = 0; i < p_CongestionGrps->numOfCongestionGrpsToConsider; i++)
    {
        index = (uint8_t)(p_CongestionGrps->congestionGrpsToConsider[i] / 32);
        mod = (uint8_t)(p_CongestionGrps->congestionGrpsToConsider[i] % 32);
        if (p_FmPort->fmRevInfo.majorRev != 4)
            grpsMap[7 - index] |= (uint32_t)(1 << mod);
        else
            grpsMap[0] |= (uint32_t)(1 << mod);
    }

    memset(&priorityTmpArray, 0,
           FM_PORT_NUM_OF_CONGESTION_GRPS * sizeof(uint8_t));

    for (i = 0; i < p_CongestionGrps->numOfCongestionGrpsToConsider; i++)
    {
#if (DPAA_VERSION >= 11)
        for (j = 0; j < FM_MAX_NUM_OF_PFC_PRIORITIES; j++)
            if (p_CongestionGrps->pfcPrioritiesEn[i][j])
                priorityTmpArray[p_CongestionGrps->congestionGrpsToConsider[i]] |=
                        (0x01 << (FM_MAX_NUM_OF_PFC_PRIORITIES - j - 1));
#endif /* (DPAA_VERSION >= 11) */
    }

#if (DPAA_VERSION >= 11)
    for (i = 0; i < FM_PORT_NUM_OF_CONGESTION_GRPS; i++)
    {
        err = FmSetCongestionGroupPFCpriority(p_FmPort->h_Fm, i,
                                              priorityTmpArray[i]);
        if (err)
            return err;
    }
#endif /* (DPAA_VERSION >= 11) */

    err = fman_port_add_congestion_grps(&p_FmPort->port, grpsMap);
    if (err != 0)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("fman_port_add_congestion_grps"));

    return E_OK;
}

t_Error FM_PORT_RemoveCongestionGrps(t_Handle h_FmPort,
                                     t_FmPortCongestionGrps *p_CongestionGrps)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;
    uint8_t mod, index;
    uint32_t i, grpsMap[FMAN_PORT_CG_MAP_NUM];
    int err;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);

    {
#ifdef FM_NO_OP_OBSERVED_CGS
        t_FmRevisionInfo revInfo;

        FM_GetRevision(p_FmPort->h_Fm, &revInfo);
        if (revInfo.majorRev != 4)
        {
            if ((p_FmPort->portType != e_FM_PORT_TYPE_RX_10G) &&
                    (p_FmPort->portType != e_FM_PORT_TYPE_RX))
            RETURN_ERROR(MAJOR, E_NOT_SUPPORTED, ("Available for Rx ports only"));
        }
        else
#endif /* FM_NO_OP_OBSERVED_CGS */
        if ((p_FmPort->portType != e_FM_PORT_TYPE_RX_10G)
                && (p_FmPort->portType != e_FM_PORT_TYPE_RX)
                && (p_FmPort->portType != e_FM_PORT_TYPE_OH_OFFLINE_PARSING))
            RETURN_ERROR(MAJOR, E_NOT_SUPPORTED,
                         ("Available for Rx & OP ports only"));
    }

    /* Prepare groups map array */
    memset(grpsMap, 0, FMAN_PORT_CG_MAP_NUM * sizeof(uint32_t));
    for (i = 0; i < p_CongestionGrps->numOfCongestionGrpsToConsider; i++)
    {
        index = (uint8_t)(p_CongestionGrps->congestionGrpsToConsider[i] / 32);
        mod = (uint8_t)(p_CongestionGrps->congestionGrpsToConsider[i] % 32);
        if (p_FmPort->fmRevInfo.majorRev != 4)
            grpsMap[7 - index] |= (uint32_t)(1 << mod);
        else
            grpsMap[0] |= (uint32_t)(1 << mod);
    }

#if (DPAA_VERSION >= 11)
    for (i = 0; i < p_CongestionGrps->numOfCongestionGrpsToConsider; i++)
    {
        t_Error err = FmSetCongestionGroupPFCpriority(
                p_FmPort->h_Fm, p_CongestionGrps->congestionGrpsToConsider[i],
                0);
        if (err)
            return err;
    }
#endif /* (DPAA_VERSION >= 11) */

    err = fman_port_remove_congestion_grps(&p_FmPort->port, grpsMap);
    if (err != 0)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE,
                     ("fman_port_remove_congestion_grps"));
    return E_OK;
}

#if (DPAA_VERSION >= 11)
t_Error FM_PORT_GetIPv4OptionsCount(t_Handle h_FmPort,
                                    uint32_t *p_Ipv4OptionsCount)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(
            (p_FmPort->portType == e_FM_PORT_TYPE_OH_OFFLINE_PARSING),
            E_INVALID_VALUE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_ParamsPage, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(p_Ipv4OptionsCount, E_NULL_POINTER);

    *p_Ipv4OptionsCount = GET_UINT32(p_FmPort->p_ParamsPage->ipfOptionsCounter);

    return E_OK;
}
#endif /* (DPAA_VERSION >= 11) */

t_Error FM_PORT_ConfigDsarSupport(t_Handle h_FmPortRx,
                                  t_FmPortDsarTablesSizes *params)
{
    t_FmPort *p_FmPort = (t_FmPort *)h_FmPortRx;
    p_FmPort->deepSleepVars.autoResMaxSizes = XX_Malloc(
            sizeof(struct t_FmPortDsarTablesSizes));
    memcpy(p_FmPort->deepSleepVars.autoResMaxSizes, params,
           sizeof(struct t_FmPortDsarTablesSizes));
    return E_OK;
}

static t_Error FmPortConfigAutoResForDeepSleepSupport1(t_FmPort *p_FmPort)
{
    uint32_t *param_page;
    t_FmPortDsarTablesSizes *params = p_FmPort->deepSleepVars.autoResMaxSizes;
    t_ArCommonDesc *ArCommonDescPtr;
    uint32_t size = sizeof(t_ArCommonDesc);
    // ARP
    // should put here if (params->max_num_of_arp_entries)?
    size = ROUND_UP(size,4);
    size += sizeof(t_DsarArpDescriptor);
    size += sizeof(t_DsarArpBindingEntry) * params->maxNumOfArpEntries;
    size += sizeof(t_DsarArpStatistics);
    //ICMPV4
    size = ROUND_UP(size,4);
    size += sizeof(t_DsarIcmpV4Descriptor);
    size += sizeof(t_DsarIcmpV4BindingEntry) * params->maxNumOfEchoIpv4Entries;
    size += sizeof(t_DsarIcmpV4Statistics);
    //ICMPV6
    size = ROUND_UP(size,4);
    size += sizeof(t_DsarIcmpV6Descriptor);
    size += sizeof(t_DsarIcmpV6BindingEntry) * params->maxNumOfEchoIpv6Entries;
    size += sizeof(t_DsarIcmpV6Statistics);
    //ND
    size = ROUND_UP(size,4);
    size += sizeof(t_DsarNdDescriptor);
    size += sizeof(t_DsarIcmpV6BindingEntry) * params->maxNumOfNdpEntries;
    size += sizeof(t_DsarIcmpV6Statistics);
    //SNMP
    size = ROUND_UP(size,4);
    size += sizeof(t_DsarSnmpDescriptor);
    size += sizeof(t_DsarSnmpIpv4AddrTblEntry)
            * params->maxNumOfSnmpIPV4Entries;
    size += sizeof(t_DsarSnmpIpv6AddrTblEntry)
            * params->maxNumOfSnmpIPV6Entries;
    size += sizeof(t_OidsTblEntry) * params->maxNumOfSnmpOidEntries;
    size += params->maxNumOfSnmpOidChar;
    size += sizeof(t_DsarIcmpV6Statistics);
    //filters
    size = ROUND_UP(size,4);
    size += params->maxNumOfIpProtFiltering;
    size = ROUND_UP(size,4);
    size += params->maxNumOfUdpPortFiltering * sizeof(t_PortTblEntry);
    size = ROUND_UP(size,4);
    size += params->maxNumOfTcpPortFiltering * sizeof(t_PortTblEntry);

    // add here for more protocols

    // statistics
    size = ROUND_UP(size,4);
    size += sizeof(t_ArStatistics);

    ArCommonDescPtr = FM_MURAM_AllocMem(p_FmPort->h_FmMuram, size, 0x10);

    param_page =
            XX_PhysToVirt(
                    p_FmPort->fmMuramPhysBaseAddr
                            + GET_UINT32(p_FmPort->p_FmPortBmiRegs->rxPortBmiRegs.fmbm_rgpr));
    WRITE_UINT32(
            *param_page,
            (uint32_t)(XX_VirtToPhys(ArCommonDescPtr) - p_FmPort->fmMuramPhysBaseAddr));
    return E_OK;
}

t_FmPortDsarTablesSizes* FM_PORT_GetDsarTablesMaxSizes(t_Handle h_FmPortRx)
{
    t_FmPort *p_FmPort = (t_FmPort *)h_FmPortRx;
    return p_FmPort->deepSleepVars.autoResMaxSizes;
}

struct arOffsets
{
    uint32_t arp;
    uint32_t nd;
    uint32_t icmpv4;
    uint32_t icmpv6;
    uint32_t snmp;
    uint32_t stats;
    uint32_t filtIp;
    uint32_t filtUdp;
    uint32_t filtTcp;
};

static uint32_t AR_ComputeOffsets(struct arOffsets* of,
                                  struct t_FmPortDsarParams *params,
                                  t_FmPort *p_FmPort)
{
    uint32_t size = sizeof(t_ArCommonDesc);
    // ARP
    if (params->p_AutoResArpInfo)
    {
        size = ROUND_UP(size,4);
        of->arp = size;
        size += sizeof(t_DsarArpDescriptor);
        size += sizeof(t_DsarArpBindingEntry)
                * params->p_AutoResArpInfo->tableSize;
        size += sizeof(t_DsarArpStatistics);
    }
    // ICMPV4
    if (params->p_AutoResEchoIpv4Info)
    {
        size = ROUND_UP(size,4);
        of->icmpv4 = size;
        size += sizeof(t_DsarIcmpV4Descriptor);
        size += sizeof(t_DsarIcmpV4BindingEntry)
                * params->p_AutoResEchoIpv4Info->tableSize;
        size += sizeof(t_DsarIcmpV4Statistics);
    }
    // ICMPV6
    if (params->p_AutoResEchoIpv6Info)
    {
        size = ROUND_UP(size,4);
        of->icmpv6 = size;
        size += sizeof(t_DsarIcmpV6Descriptor);
        size += sizeof(t_DsarIcmpV6BindingEntry)
                * params->p_AutoResEchoIpv6Info->tableSize;
        size += sizeof(t_DsarIcmpV6Statistics);
    }
    // ND
    if (params->p_AutoResNdpInfo)
    {
        size = ROUND_UP(size,4);
        of->nd = size;
        size += sizeof(t_DsarNdDescriptor);
        size += sizeof(t_DsarIcmpV6BindingEntry)
                * (params->p_AutoResNdpInfo->tableSizeAssigned
                        + params->p_AutoResNdpInfo->tableSizeTmp);
        size += sizeof(t_DsarIcmpV6Statistics);
    }
    // SNMP
    if (params->p_AutoResSnmpInfo)
    {
        size = ROUND_UP(size,4);
        of->snmp = size;
        size += sizeof(t_DsarSnmpDescriptor);
        size += sizeof(t_DsarSnmpIpv4AddrTblEntry)
                * params->p_AutoResSnmpInfo->numOfIpv4Addresses;
        size += sizeof(t_DsarSnmpIpv6AddrTblEntry)
                * params->p_AutoResSnmpInfo->numOfIpv6Addresses;
        size += sizeof(t_OidsTblEntry) * params->p_AutoResSnmpInfo->oidsTblSize;
        size += p_FmPort->deepSleepVars.autoResMaxSizes->maxNumOfSnmpOidChar;
        size += sizeof(t_DsarIcmpV6Statistics);
    }
    //filters
    size = ROUND_UP(size,4);
    if (params->p_AutoResFilteringInfo)
    {
        of->filtIp = size;
        size += params->p_AutoResFilteringInfo->ipProtTableSize;
        size = ROUND_UP(size,4);
        of->filtUdp = size;
        size += params->p_AutoResFilteringInfo->udpPortsTableSize
                * sizeof(t_PortTblEntry);
        size = ROUND_UP(size,4);
        of->filtTcp = size;
        size += params->p_AutoResFilteringInfo->tcpPortsTableSize
                * sizeof(t_PortTblEntry);
    }
    // add here for more protocols
    // statistics
    size = ROUND_UP(size,4);
    of->stats = size;
    size += sizeof(t_ArStatistics);
    return size;
}

uint32_t* ARDesc;
void PrsEnable(t_Handle p_FmPcd);
void PrsDisable(t_Handle p_FmPcd);
int PrsIsEnabled(t_Handle p_FmPcd);
t_Handle FM_PCD_GetHcPort(t_Handle h_FmPcd);

static t_Error DsarCheckParams(t_FmPortDsarParams *params,
                               t_FmPortDsarTablesSizes *sizes)
{
    bool macInit = FALSE;
    uint8_t mac[6];
    int i = 0;

    // check table sizes
    if (params->p_AutoResArpInfo
            && sizes->maxNumOfArpEntries < params->p_AutoResArpInfo->tableSize)
        RETURN_ERROR(
                MAJOR, E_INVALID_VALUE,
                ("DSAR: Arp table size exceeds the configured maximum size."));
    if (params->p_AutoResEchoIpv4Info
            && sizes->maxNumOfEchoIpv4Entries
                    < params->p_AutoResEchoIpv4Info->tableSize)
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("DSAR: EchoIpv4 table size exceeds the configured maximum size."));
    if (params->p_AutoResNdpInfo
            && sizes->maxNumOfNdpEntries
                    < params->p_AutoResNdpInfo->tableSizeAssigned
                            + params->p_AutoResNdpInfo->tableSizeTmp)
        RETURN_ERROR(
                MAJOR, E_INVALID_VALUE,
                ("DSAR: NDP table size exceeds the configured maximum size."));
    if (params->p_AutoResEchoIpv6Info
            && sizes->maxNumOfEchoIpv6Entries
                    < params->p_AutoResEchoIpv6Info->tableSize)
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("DSAR: EchoIpv6 table size exceeds the configured maximum size."));
    if (params->p_AutoResSnmpInfo
            && sizes->maxNumOfSnmpOidEntries
                    < params->p_AutoResSnmpInfo->oidsTblSize)
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("DSAR: Snmp Oid table size exceeds the configured maximum size."));
    if (params->p_AutoResSnmpInfo
            && sizes->maxNumOfSnmpIPV4Entries
                    < params->p_AutoResSnmpInfo->numOfIpv4Addresses)
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("DSAR: Snmp ipv4 table size exceeds the configured maximum size."));
    if (params->p_AutoResSnmpInfo
            && sizes->maxNumOfSnmpIPV6Entries
                    < params->p_AutoResSnmpInfo->numOfIpv6Addresses)
        RETURN_ERROR(
                MAJOR,
                E_INVALID_VALUE,
                ("DSAR: Snmp ipv6 table size exceeds the configured maximum size."));
    if (params->p_AutoResFilteringInfo)
    {
        if (sizes->maxNumOfIpProtFiltering
                < params->p_AutoResFilteringInfo->ipProtTableSize)
            RETURN_ERROR(
                    MAJOR,
                    E_INVALID_VALUE,
                    ("DSAR: ip filter table size exceeds the configured maximum size."));
        if (sizes->maxNumOfTcpPortFiltering
                < params->p_AutoResFilteringInfo->udpPortsTableSize)
            RETURN_ERROR(
                    MAJOR,
                    E_INVALID_VALUE,
                    ("DSAR: udp filter table size exceeds the configured maximum size."));
        if (sizes->maxNumOfUdpPortFiltering
                < params->p_AutoResFilteringInfo->tcpPortsTableSize)
            RETURN_ERROR(
                    MAJOR,
                    E_INVALID_VALUE,
                    ("DSAR: tcp filter table size exceeds the configured maximum size."));
    }
    /* check only 1 MAC address is configured (this is what ucode currently supports) */
    if (params->p_AutoResArpInfo && params->p_AutoResArpInfo->tableSize)
    {
        memcpy(mac, params->p_AutoResArpInfo->p_AutoResTable[0].mac, 6);
        i = 1;
        macInit = TRUE;

        for (; i < params->p_AutoResArpInfo->tableSize; i++)
            if (memcmp(mac, params->p_AutoResArpInfo->p_AutoResTable[i].mac, 6))
                RETURN_ERROR(
                        MAJOR, E_INVALID_VALUE,
                        ("DSAR: Only 1 mac address is currently supported."));
    }
    if (params->p_AutoResEchoIpv4Info
            && params->p_AutoResEchoIpv4Info->tableSize)
    {
        i = 0;
        if (!macInit)
        {
            memcpy(mac, params->p_AutoResEchoIpv4Info->p_AutoResTable[0].mac,
                   6);
            i = 1;
            macInit = TRUE;
        }
        for (; i < params->p_AutoResEchoIpv4Info->tableSize; i++)
            if (memcmp(mac,
                       params->p_AutoResEchoIpv4Info->p_AutoResTable[i].mac, 6))
                RETURN_ERROR(
                        MAJOR, E_INVALID_VALUE,
                        ("DSAR: Only 1 mac address is currently supported."));
    }
    if (params->p_AutoResEchoIpv6Info
            && params->p_AutoResEchoIpv6Info->tableSize)
    {
        i = 0;
        if (!macInit)
        {
            memcpy(mac, params->p_AutoResEchoIpv6Info->p_AutoResTable[0].mac,
                   6);
            i = 1;
            macInit = TRUE;
        }
        for (; i < params->p_AutoResEchoIpv6Info->tableSize; i++)
            if (memcmp(mac,
                       params->p_AutoResEchoIpv6Info->p_AutoResTable[i].mac, 6))
                RETURN_ERROR(
                        MAJOR, E_INVALID_VALUE,
                        ("DSAR: Only 1 mac address is currently supported."));
    }
    if (params->p_AutoResNdpInfo && params->p_AutoResNdpInfo->tableSizeAssigned)
    {
        i = 0;
        if (!macInit)
        {
            memcpy(mac, params->p_AutoResNdpInfo->p_AutoResTableAssigned[0].mac,
                   6);
            i = 1;
            macInit = TRUE;
        }
        for (; i < params->p_AutoResNdpInfo->tableSizeAssigned; i++)
            if (memcmp(mac,
                       params->p_AutoResNdpInfo->p_AutoResTableAssigned[i].mac,
                       6))
                RETURN_ERROR(
                        MAJOR, E_INVALID_VALUE,
                        ("DSAR: Only 1 mac address is currently supported."));
    }
    if (params->p_AutoResNdpInfo && params->p_AutoResNdpInfo->tableSizeTmp)
    {
        i = 0;
        if (!macInit)
        {
            memcpy(mac, params->p_AutoResNdpInfo->p_AutoResTableTmp[0].mac, 6);
            i = 1;
        }
        for (; i < params->p_AutoResNdpInfo->tableSizeTmp; i++)
            if (memcmp(mac, params->p_AutoResNdpInfo->p_AutoResTableTmp[i].mac,
                       6))
                RETURN_ERROR(
                        MAJOR, E_INVALID_VALUE,
                        ("DSAR: Only 1 mac address is currently supported."));
    }
    return E_OK;
}

static int GetBERLen(uint8_t* buf)
{
    if (*buf & 0x80)
    {
        if ((*buf & 0x7F) == 1)
            return buf[1];
        else
            return *(uint16_t*)&buf[1]; // assuming max len is 2
    }
    else
        return buf[0];
}
#define TOTAL_BER_LEN(len) (len < 128) ? len + 2 : len + 3

#ifdef TODO_SOC_SUSPEND // XXX
#define SCFG_FMCLKDPSLPCR_ADDR 0xFFE0FC00C
#define SCFG_FMCLKDPSLPCR_DS_VAL 0x08402000
#define SCFG_FMCLKDPSLPCR_NORMAL_VAL 0x00402000
static int fm_soc_suspend(void)
{
	uint32_t *fmclk, tmp32;
	fmclk = ioremap(SCFG_FMCLKDPSLPCR_ADDR, 4);
	tmp32 = GET_UINT32(*fmclk);
	WRITE_UINT32(*fmclk, SCFG_FMCLKDPSLPCR_DS_VAL);
	tmp32 = GET_UINT32(*fmclk);
	iounmap(fmclk);
	return 0;
}

void fm_clk_down(void)
{
	uint32_t *fmclk, tmp32;
	fmclk = ioremap(SCFG_FMCLKDPSLPCR_ADDR, 4);
	tmp32 = GET_UINT32(*fmclk);
	WRITE_UINT32(*fmclk, SCFG_FMCLKDPSLPCR_DS_VAL | 0x40000000);
	tmp32 = GET_UINT32(*fmclk);
	iounmap(fmclk);
}
#endif 

#if 0
t_Error FM_PORT_EnterDsar(t_Handle h_FmPortRx, t_FmPortDsarParams *params)
{
    int i, j;
    t_Error err;
    uint32_t nia;
    t_FmPort *p_FmPort = (t_FmPort *)h_FmPortRx;
    t_FmPort *p_FmPortTx = (t_FmPort *)params->h_FmPortTx;
    t_DsarArpDescriptor *ArpDescriptor;
    t_DsarIcmpV4Descriptor* ICMPV4Descriptor;
    t_DsarIcmpV6Descriptor* ICMPV6Descriptor;
    t_DsarNdDescriptor* NDDescriptor;

    uint64_t fmMuramVirtBaseAddr = (uint64_t)PTR_TO_UINT(XX_PhysToVirt(p_FmPort->fmMuramPhysBaseAddr));
    uint32_t *param_page = XX_PhysToVirt(p_FmPort->fmMuramPhysBaseAddr + GET_UINT32(p_FmPort->p_FmPortBmiRegs->rxPortBmiRegs.fmbm_rgpr));
    t_ArCommonDesc *ArCommonDescPtr = (t_ArCommonDesc*)(XX_PhysToVirt(p_FmPort->fmMuramPhysBaseAddr + GET_UINT32(*param_page)));
    struct arOffsets* of;
    uint8_t tmp = 0;
    t_FmGetSetParams fmGetSetParams;
    memset(&fmGetSetParams, 0, sizeof (t_FmGetSetParams));
    fmGetSetParams.setParams.type = UPDATE_FPM_BRKC_SLP;
    fmGetSetParams.setParams.sleep = 1;    

    err = DsarCheckParams(params, p_FmPort->deepSleepVars.autoResMaxSizes);
    if (err != E_OK)
        return err;

    p_FmPort->deepSleepVars.autoResOffsets = XX_Malloc(sizeof(struct arOffsets));
    of = (struct arOffsets *)p_FmPort->deepSleepVars.autoResOffsets;
    IOMemSet32(ArCommonDescPtr, 0, AR_ComputeOffsets(of, params, p_FmPort));

    // common
    WRITE_UINT8(ArCommonDescPtr->arTxPort, p_FmPortTx->hardwarePortId);
    nia = GET_UINT32(p_FmPort->p_FmPortBmiRegs->rxPortBmiRegs.fmbm_rfne); // bmi nia
    if ((nia & 0x007C0000) == 0x00440000) // bmi nia is parser
        WRITE_UINT32(ArCommonDescPtr->activeHPNIA, GET_UINT32(p_FmPort->p_FmPortBmiRegs->rxPortBmiRegs.fmbm_rfpne));
    else
        WRITE_UINT32(ArCommonDescPtr->activeHPNIA, nia);
    WRITE_UINT16(ArCommonDescPtr->snmpPort, 161);

    // ARP
    if (params->p_AutoResArpInfo)
    {
        t_DsarArpBindingEntry* arp_bindings;
        ArpDescriptor = (t_DsarArpDescriptor*)(PTR_TO_UINT(ArCommonDescPtr) + of->arp);
        WRITE_UINT32(ArCommonDescPtr->p_ArpDescriptor, PTR_TO_UINT(ArpDescriptor) - fmMuramVirtBaseAddr);
        arp_bindings = (t_DsarArpBindingEntry*)(PTR_TO_UINT(ArpDescriptor) + sizeof(t_DsarArpDescriptor));
	if (params->p_AutoResArpInfo->enableConflictDetection)
	        WRITE_UINT16(ArpDescriptor->control, 1);
	else
        WRITE_UINT16(ArpDescriptor->control, 0);
        if (params->p_AutoResArpInfo->tableSize)
        {
            t_FmPortDsarArpEntry* arp_entry = params->p_AutoResArpInfo->p_AutoResTable;
            WRITE_UINT16(*(uint16_t*)&ArCommonDescPtr->macStationAddr[0], *(uint16_t*)&arp_entry[0].mac[0]);
            WRITE_UINT32(*(uint32_t*)&ArCommonDescPtr->macStationAddr[2], *(uint32_t*)&arp_entry[0].mac[2]);
            WRITE_UINT16(ArpDescriptor->numOfBindings, params->p_AutoResArpInfo->tableSize);

            for (i = 0; i < params->p_AutoResArpInfo->tableSize; i++)
            {
                WRITE_UINT32(arp_bindings[i].ipv4Addr, arp_entry[i].ipAddress);
                if (arp_entry[i].isVlan)
                    WRITE_UINT16(arp_bindings[i].vlanId, arp_entry[i].vid & 0xFFF);
            }
            WRITE_UINT32(ArpDescriptor->p_Bindings, PTR_TO_UINT(arp_bindings) - fmMuramVirtBaseAddr);
        }
        WRITE_UINT32(ArpDescriptor->p_Statistics, PTR_TO_UINT(arp_bindings) +
            sizeof(t_DsarArpBindingEntry) * params->p_AutoResArpInfo->tableSize - fmMuramVirtBaseAddr);
    }

    // ICMPV4
    if (params->p_AutoResEchoIpv4Info)
    {
        t_DsarIcmpV4BindingEntry* icmpv4_bindings;
        ICMPV4Descriptor = (t_DsarIcmpV4Descriptor*)(PTR_TO_UINT(ArCommonDescPtr) + of->icmpv4);
        WRITE_UINT32(ArCommonDescPtr->p_IcmpV4Descriptor, PTR_TO_UINT(ICMPV4Descriptor) - fmMuramVirtBaseAddr);
        icmpv4_bindings = (t_DsarIcmpV4BindingEntry*)(PTR_TO_UINT(ICMPV4Descriptor) + sizeof(t_DsarIcmpV4Descriptor));
        WRITE_UINT16(ICMPV4Descriptor->control, 0);
        if (params->p_AutoResEchoIpv4Info->tableSize)
        {
            t_FmPortDsarArpEntry* arp_entry = params->p_AutoResEchoIpv4Info->p_AutoResTable;
            WRITE_UINT16(*(uint16_t*)&ArCommonDescPtr->macStationAddr[0], *(uint16_t*)&arp_entry[0].mac[0]);
            WRITE_UINT32(*(uint32_t*)&ArCommonDescPtr->macStationAddr[2], *(uint32_t*)&arp_entry[0].mac[2]);
            WRITE_UINT16(ICMPV4Descriptor->numOfBindings, params->p_AutoResEchoIpv4Info->tableSize);

            for (i = 0; i < params->p_AutoResEchoIpv4Info->tableSize; i++)
            {
                WRITE_UINT32(icmpv4_bindings[i].ipv4Addr, arp_entry[i].ipAddress);
                if (arp_entry[i].isVlan)
                    WRITE_UINT16(icmpv4_bindings[i].vlanId, arp_entry[i].vid & 0xFFF);
            }
            WRITE_UINT32(ICMPV4Descriptor->p_Bindings, PTR_TO_UINT(icmpv4_bindings) - fmMuramVirtBaseAddr);
        }
        WRITE_UINT32(ICMPV4Descriptor->p_Statistics, PTR_TO_UINT(icmpv4_bindings) +
            sizeof(t_DsarIcmpV4BindingEntry) * params->p_AutoResEchoIpv4Info->tableSize - fmMuramVirtBaseAddr);
    }

    // ICMPV6
    if (params->p_AutoResEchoIpv6Info)
    {
        t_DsarIcmpV6BindingEntry* icmpv6_bindings;
        ICMPV6Descriptor = (t_DsarIcmpV6Descriptor*)(PTR_TO_UINT(ArCommonDescPtr) + of->icmpv6);
        WRITE_UINT32(ArCommonDescPtr->p_IcmpV6Descriptor, PTR_TO_UINT(ICMPV6Descriptor) - fmMuramVirtBaseAddr);
        icmpv6_bindings = (t_DsarIcmpV6BindingEntry*)(PTR_TO_UINT(ICMPV6Descriptor) + sizeof(t_DsarIcmpV6Descriptor));
        WRITE_UINT16(ICMPV6Descriptor->control, 0);
        if (params->p_AutoResEchoIpv6Info->tableSize)
        {
            t_FmPortDsarNdpEntry* ndp_entry = params->p_AutoResEchoIpv6Info->p_AutoResTable;
            WRITE_UINT16(*(uint16_t*)&ArCommonDescPtr->macStationAddr[0], *(uint16_t*)&ndp_entry[0].mac[0]);
            WRITE_UINT32(*(uint32_t*)&ArCommonDescPtr->macStationAddr[2], *(uint32_t*)&ndp_entry[0].mac[2]);
            WRITE_UINT16(ICMPV6Descriptor->numOfBindings, params->p_AutoResEchoIpv6Info->tableSize);

            for (i = 0; i < params->p_AutoResEchoIpv6Info->tableSize; i++)
            {
                for (j = 0; j < 4; j++)
                    WRITE_UINT32(icmpv6_bindings[i].ipv6Addr[j], ndp_entry[i].ipAddress[j]);
                if (ndp_entry[i].isVlan)
                    WRITE_UINT16(*(uint16_t*)&icmpv6_bindings[i].ipv6Addr[4], ndp_entry[i].vid & 0xFFF); // writing vlan
            }
            WRITE_UINT32(ICMPV6Descriptor->p_Bindings, PTR_TO_UINT(icmpv6_bindings) - fmMuramVirtBaseAddr);
        }
        WRITE_UINT32(ICMPV6Descriptor->p_Statistics, PTR_TO_UINT(icmpv6_bindings) +
            sizeof(t_DsarIcmpV6BindingEntry) * params->p_AutoResEchoIpv6Info->tableSize - fmMuramVirtBaseAddr);
    }

    // ND
    if (params->p_AutoResNdpInfo)
    {
        t_DsarIcmpV6BindingEntry* icmpv6_bindings;
        NDDescriptor = (t_DsarNdDescriptor*)(PTR_TO_UINT(ArCommonDescPtr) + of->nd);
        WRITE_UINT32(ArCommonDescPtr->p_NdDescriptor, PTR_TO_UINT(NDDescriptor) - fmMuramVirtBaseAddr);
        icmpv6_bindings = (t_DsarIcmpV6BindingEntry*)(PTR_TO_UINT(NDDescriptor) + sizeof(t_DsarNdDescriptor));
	if (params->p_AutoResNdpInfo->enableConflictDetection)
	        WRITE_UINT16(NDDescriptor->control, 1);
	else
        WRITE_UINT16(NDDescriptor->control, 0);
        if (params->p_AutoResNdpInfo->tableSizeAssigned + params->p_AutoResNdpInfo->tableSizeTmp)
        {
            t_FmPortDsarNdpEntry* ndp_entry = params->p_AutoResNdpInfo->p_AutoResTableAssigned;
            WRITE_UINT16(*(uint16_t*)&ArCommonDescPtr->macStationAddr[0], *(uint16_t*)&ndp_entry[0].mac[0]);
            WRITE_UINT32(*(uint32_t*)&ArCommonDescPtr->macStationAddr[2], *(uint32_t*)&ndp_entry[0].mac[2]);
            WRITE_UINT16(NDDescriptor->numOfBindings, params->p_AutoResNdpInfo->tableSizeAssigned
                + params->p_AutoResNdpInfo->tableSizeTmp);

            for (i = 0; i < params->p_AutoResNdpInfo->tableSizeAssigned; i++)
            {
                for (j = 0; j < 4; j++)
                    WRITE_UINT32(icmpv6_bindings[i].ipv6Addr[j], ndp_entry[i].ipAddress[j]);
                if (ndp_entry[i].isVlan)
                    WRITE_UINT16(*(uint16_t*)&icmpv6_bindings[i].ipv6Addr[4], ndp_entry[i].vid & 0xFFF); // writing vlan
            }
            ndp_entry = params->p_AutoResNdpInfo->p_AutoResTableTmp;
            for (i = 0; i < params->p_AutoResNdpInfo->tableSizeTmp; i++)
            {
                for (j = 0; j < 4; j++)
                    WRITE_UINT32(icmpv6_bindings[i + params->p_AutoResNdpInfo->tableSizeAssigned].ipv6Addr[j], ndp_entry[i].ipAddress[j]);
                if (ndp_entry[i].isVlan)
                    WRITE_UINT16(*(uint16_t*)&icmpv6_bindings[i + params->p_AutoResNdpInfo->tableSizeAssigned].ipv6Addr[4], ndp_entry[i].vid & 0xFFF); // writing vlan
            }
            WRITE_UINT32(NDDescriptor->p_Bindings, PTR_TO_UINT(icmpv6_bindings) - fmMuramVirtBaseAddr);
        }
        WRITE_UINT32(NDDescriptor->p_Statistics, PTR_TO_UINT(icmpv6_bindings) + sizeof(t_DsarIcmpV6BindingEntry)
            * (params->p_AutoResNdpInfo->tableSizeAssigned + params->p_AutoResNdpInfo->tableSizeTmp)
            - fmMuramVirtBaseAddr);
        WRITE_UINT32(NDDescriptor->solicitedAddr, 0xFFFFFFFF);
    }

    // SNMP
    if (params->p_AutoResSnmpInfo)
    {
        t_FmPortDsarSnmpInfo *snmpSrc = params->p_AutoResSnmpInfo;
        t_DsarSnmpIpv4AddrTblEntry* snmpIpv4Addr;
        t_DsarSnmpIpv6AddrTblEntry* snmpIpv6Addr;
        t_OidsTblEntry* snmpOid;
        uint8_t *charPointer;
        int len;
        t_DsarSnmpDescriptor* SnmpDescriptor = (t_DsarSnmpDescriptor*)(PTR_TO_UINT(ArCommonDescPtr) + of->snmp);
        WRITE_UINT32(ArCommonDescPtr->p_SnmpDescriptor, PTR_TO_UINT(SnmpDescriptor) - fmMuramVirtBaseAddr);
        WRITE_UINT16(SnmpDescriptor->control, snmpSrc->control);
        WRITE_UINT16(SnmpDescriptor->maxSnmpMsgLength, snmpSrc->maxSnmpMsgLength);
        snmpIpv4Addr = (t_DsarSnmpIpv4AddrTblEntry*)(PTR_TO_UINT(SnmpDescriptor) + sizeof(t_DsarSnmpDescriptor));
        if (snmpSrc->numOfIpv4Addresses)
        {
            t_FmPortDsarSnmpIpv4AddrTblEntry* snmpIpv4AddrSrc = snmpSrc->p_Ipv4AddrTbl;
            WRITE_UINT16(SnmpDescriptor->numOfIpv4Addresses, snmpSrc->numOfIpv4Addresses);
            for (i = 0; i < snmpSrc->numOfIpv4Addresses; i++)
            {
                WRITE_UINT32(snmpIpv4Addr[i].ipv4Addr, snmpIpv4AddrSrc[i].ipv4Addr);
                if (snmpIpv4AddrSrc[i].isVlan)
                    WRITE_UINT16(snmpIpv4Addr[i].vlanId, snmpIpv4AddrSrc[i].vid & 0xFFF);
            }
            WRITE_UINT32(SnmpDescriptor->p_Ipv4AddrTbl, PTR_TO_UINT(snmpIpv4Addr) - fmMuramVirtBaseAddr);
        }
        snmpIpv6Addr = (t_DsarSnmpIpv6AddrTblEntry*)(PTR_TO_UINT(snmpIpv4Addr)
                + sizeof(t_DsarSnmpIpv4AddrTblEntry) * snmpSrc->numOfIpv4Addresses);
        if (snmpSrc->numOfIpv6Addresses)
        {
            t_FmPortDsarSnmpIpv6AddrTblEntry* snmpIpv6AddrSrc = snmpSrc->p_Ipv6AddrTbl;
            WRITE_UINT16(SnmpDescriptor->numOfIpv6Addresses, snmpSrc->numOfIpv6Addresses);
            for (i = 0; i < snmpSrc->numOfIpv6Addresses; i++)
            {
                for (j = 0; j < 4; j++)
                    WRITE_UINT32(snmpIpv6Addr[i].ipv6Addr[j], snmpIpv6AddrSrc[i].ipv6Addr[j]);
                if (snmpIpv6AddrSrc[i].isVlan)
                    WRITE_UINT16(snmpIpv6Addr[i].vlanId, snmpIpv6AddrSrc[i].vid & 0xFFF);
            }
            WRITE_UINT32(SnmpDescriptor->p_Ipv6AddrTbl, PTR_TO_UINT(snmpIpv6Addr) - fmMuramVirtBaseAddr);
        }
        snmpOid = (t_OidsTblEntry*)(PTR_TO_UINT(snmpIpv6Addr)
                + sizeof(t_DsarSnmpIpv6AddrTblEntry) * snmpSrc->numOfIpv6Addresses);
        charPointer = (uint8_t*)(PTR_TO_UINT(snmpOid)
                + sizeof(t_OidsTblEntry) * snmpSrc->oidsTblSize);
        len = TOTAL_BER_LEN(GetBERLen(&snmpSrc->p_RdOnlyCommunityStr[1]));
        Mem2IOCpy32(charPointer, snmpSrc->p_RdOnlyCommunityStr, len);
        WRITE_UINT32(SnmpDescriptor->p_RdOnlyCommunityStr, PTR_TO_UINT(charPointer) - fmMuramVirtBaseAddr);
        charPointer += len;
        len = TOTAL_BER_LEN(GetBERLen(&snmpSrc->p_RdWrCommunityStr[1]));
        Mem2IOCpy32(charPointer, snmpSrc->p_RdWrCommunityStr, len);
        WRITE_UINT32(SnmpDescriptor->p_RdWrCommunityStr, PTR_TO_UINT(charPointer) - fmMuramVirtBaseAddr);
        charPointer += len;
        WRITE_UINT32(SnmpDescriptor->oidsTblSize, snmpSrc->oidsTblSize);
        WRITE_UINT32(SnmpDescriptor->p_OidsTbl, PTR_TO_UINT(snmpOid) - fmMuramVirtBaseAddr);
        for (i = 0; i < snmpSrc->oidsTblSize; i++)
        {
            WRITE_UINT16(snmpOid->oidSize, snmpSrc->p_OidsTbl[i].oidSize);
            WRITE_UINT16(snmpOid->resSize, snmpSrc->p_OidsTbl[i].resSize);
            Mem2IOCpy32(charPointer, snmpSrc->p_OidsTbl[i].oidVal, snmpSrc->p_OidsTbl[i].oidSize);
            WRITE_UINT32(snmpOid->p_Oid, PTR_TO_UINT(charPointer) - fmMuramVirtBaseAddr);
            charPointer += snmpSrc->p_OidsTbl[i].oidSize;
            if (snmpSrc->p_OidsTbl[i].resSize <= 4)
                WRITE_UINT32(snmpOid->resValOrPtr, *snmpSrc->p_OidsTbl[i].resVal);
            else
            {
                Mem2IOCpy32(charPointer, snmpSrc->p_OidsTbl[i].resVal, snmpSrc->p_OidsTbl[i].resSize);
                WRITE_UINT32(snmpOid->resValOrPtr, PTR_TO_UINT(charPointer) - fmMuramVirtBaseAddr);
                charPointer += snmpSrc->p_OidsTbl[i].resSize;
            }
            snmpOid++;
        }
        charPointer = UINT_TO_PTR(ROUND_UP(PTR_TO_UINT(charPointer),4));
        WRITE_UINT32(SnmpDescriptor->p_Statistics, PTR_TO_UINT(charPointer) - fmMuramVirtBaseAddr);
    }

    // filtering
    if (params->p_AutoResFilteringInfo)
    {
        if (params->p_AutoResFilteringInfo->ipProtPassOnHit)
            tmp |= IP_PROT_TBL_PASS_MASK;
        if (params->p_AutoResFilteringInfo->udpPortPassOnHit)
            tmp |= UDP_PORT_TBL_PASS_MASK;
        if (params->p_AutoResFilteringInfo->tcpPortPassOnHit)
            tmp |= TCP_PORT_TBL_PASS_MASK;
        WRITE_UINT8(ArCommonDescPtr->filterControl, tmp);
        WRITE_UINT16(ArCommonDescPtr->tcpControlPass, params->p_AutoResFilteringInfo->tcpFlagsMask);

        // ip filtering
        if (params->p_AutoResFilteringInfo->ipProtTableSize)
        {
            uint8_t* ip_tbl = (uint8_t*)(PTR_TO_UINT(ArCommonDescPtr) + of->filtIp);
            WRITE_UINT8(ArCommonDescPtr->ipProtocolTblSize, params->p_AutoResFilteringInfo->ipProtTableSize);
            for (i = 0; i < params->p_AutoResFilteringInfo->ipProtTableSize; i++)
                WRITE_UINT8(ip_tbl[i], params->p_AutoResFilteringInfo->p_IpProtTablePtr[i]);
            WRITE_UINT32(ArCommonDescPtr->p_IpProtocolFiltTbl, PTR_TO_UINT(ip_tbl) - fmMuramVirtBaseAddr);
        }

        // udp filtering
        if (params->p_AutoResFilteringInfo->udpPortsTableSize)
        {
            t_PortTblEntry* udp_tbl = (t_PortTblEntry*)(PTR_TO_UINT(ArCommonDescPtr) + of->filtUdp);
            WRITE_UINT8(ArCommonDescPtr->udpPortTblSize, params->p_AutoResFilteringInfo->udpPortsTableSize);
            for (i = 0; i < params->p_AutoResFilteringInfo->udpPortsTableSize; i++)
            {
                WRITE_UINT32(udp_tbl[i].Ports,
                    (params->p_AutoResFilteringInfo->p_UdpPortsTablePtr[i].srcPort << 16) +
                    params->p_AutoResFilteringInfo->p_UdpPortsTablePtr[i].dstPort);
                WRITE_UINT32(udp_tbl[i].PortsMask,
                    (params->p_AutoResFilteringInfo->p_UdpPortsTablePtr[i].srcPortMask << 16) +
                    params->p_AutoResFilteringInfo->p_UdpPortsTablePtr[i].dstPortMask);
            }
            WRITE_UINT32(ArCommonDescPtr->p_UdpPortFiltTbl, PTR_TO_UINT(udp_tbl) - fmMuramVirtBaseAddr);
        }

        // tcp filtering
        if (params->p_AutoResFilteringInfo->tcpPortsTableSize)
        {
            t_PortTblEntry* tcp_tbl = (t_PortTblEntry*)(PTR_TO_UINT(ArCommonDescPtr) + of->filtTcp);
            WRITE_UINT8(ArCommonDescPtr->tcpPortTblSize, params->p_AutoResFilteringInfo->tcpPortsTableSize);
            for (i = 0; i < params->p_AutoResFilteringInfo->tcpPortsTableSize; i++)
            {
                WRITE_UINT32(tcp_tbl[i].Ports,
                    (params->p_AutoResFilteringInfo->p_TcpPortsTablePtr[i].srcPort << 16) +
                    params->p_AutoResFilteringInfo->p_TcpPortsTablePtr[i].dstPort);
                WRITE_UINT32(tcp_tbl[i].PortsMask,
                    (params->p_AutoResFilteringInfo->p_TcpPortsTablePtr[i].srcPortMask << 16) +
                    params->p_AutoResFilteringInfo->p_TcpPortsTablePtr[i].dstPortMask);
            }
            WRITE_UINT32(ArCommonDescPtr->p_TcpPortFiltTbl, PTR_TO_UINT(tcp_tbl) - fmMuramVirtBaseAddr);
        }
    }
    // common stats
    WRITE_UINT32(ArCommonDescPtr->p_ArStats, PTR_TO_UINT(ArCommonDescPtr) + of->stats - fmMuramVirtBaseAddr);

    // get into Deep Sleep sequence:

	// Ensures that FMan do not enter the idle state. This is done by programing
	// FMDPSLPCR[FM_STOP] to one.
	fm_soc_suspend();

    ARDesc = UINT_TO_PTR(XX_VirtToPhys(ArCommonDescPtr));
    return E_OK;

}

void FM_ChangeClock(t_Handle h_Fm, int hardwarePortId);
t_Error FM_PORT_EnterDsarFinal(t_Handle h_DsarRxPort, t_Handle h_DsarTxPort)
{
	t_FmGetSetParams fmGetSetParams;
	t_FmPort *p_FmPort = (t_FmPort *)h_DsarRxPort;
	t_FmPort *p_FmPortTx = (t_FmPort *)h_DsarTxPort;
	t_Handle *h_FmPcd = FmGetPcd(p_FmPort->h_Fm);
	t_FmPort *p_FmPortHc = FM_PCD_GetHcPort(h_FmPcd);
	memset(&fmGetSetParams, 0, sizeof (t_FmGetSetParams));
        fmGetSetParams.setParams.type = UPDATE_FM_CLD;
        FmGetSetParams(p_FmPort->h_Fm, &fmGetSetParams);

	/* Issue graceful stop to HC port */
	FM_PORT_Disable(p_FmPortHc);

	// config tx port
    p_FmPort->deepSleepVars.fmbm_tcfg = GET_UINT32(p_FmPortTx->p_FmPortBmiRegs->txPortBmiRegs.fmbm_tcfg);
    WRITE_UINT32(p_FmPortTx->p_FmPortBmiRegs->txPortBmiRegs.fmbm_tcfg, GET_UINT32(p_FmPortTx->p_FmPortBmiRegs->txPortBmiRegs.fmbm_tcfg) | BMI_PORT_CFG_IM | BMI_PORT_CFG_EN);
    // ????
    p_FmPort->deepSleepVars.fmbm_tcmne = GET_UINT32(p_FmPortTx->p_FmPortBmiRegs->txPortBmiRegs.fmbm_tcmne);
    WRITE_UINT32(p_FmPortTx->p_FmPortBmiRegs->txPortBmiRegs.fmbm_tcmne, 0xE);
    // Stage 7:echo
    p_FmPort->deepSleepVars.fmbm_rfpne = GET_UINT32(p_FmPort->p_FmPortBmiRegs->rxPortBmiRegs.fmbm_rfpne);
    WRITE_UINT32(p_FmPort->p_FmPortBmiRegs->rxPortBmiRegs.fmbm_rfpne, 0x2E);
    if (!PrsIsEnabled(h_FmPcd))
    {
        p_FmPort->deepSleepVars.dsarEnabledParser = TRUE;
        PrsEnable(h_FmPcd);
    }
    else
        p_FmPort->deepSleepVars.dsarEnabledParser = FALSE;

    p_FmPort->deepSleepVars.fmbm_rfne = GET_UINT32(p_FmPort->p_FmPortBmiRegs->rxPortBmiRegs.fmbm_rfne);
    WRITE_UINT32(p_FmPort->p_FmPortBmiRegs->rxPortBmiRegs.fmbm_rfne, 0x440000);

    // save rcfg for restoring: accumulate mode is changed by ucode
    p_FmPort->deepSleepVars.fmbm_rcfg = GET_UINT32(p_FmPort->port.bmi_regs->rx.fmbm_rcfg);
    WRITE_UINT32(p_FmPort->port.bmi_regs->rx.fmbm_rcfg, p_FmPort->deepSleepVars.fmbm_rcfg | BMI_PORT_CFG_AM);
        memset(&fmGetSetParams, 0, sizeof (t_FmGetSetParams));
        fmGetSetParams.setParams.type = UPDATE_FPM_BRKC_SLP;
        fmGetSetParams.setParams.sleep = 1;
        FmGetSetParams(p_FmPort->h_Fm, &fmGetSetParams);

// ***** issue external request sync command
        memset(&fmGetSetParams, 0, sizeof (t_FmGetSetParams));
        fmGetSetParams.setParams.type = UPDATE_FPM_EXTC;
        FmGetSetParams(p_FmPort->h_Fm, &fmGetSetParams);
	// get
	memset(&fmGetSetParams, 0, sizeof (t_FmGetSetParams));
	fmGetSetParams.getParams.type = GET_FMFP_EXTC;
	FmGetSetParams(p_FmPort->h_Fm, &fmGetSetParams);
	if (fmGetSetParams.getParams.fmfp_extc != 0)
	{
		// clear
		memset(&fmGetSetParams, 0, sizeof (t_FmGetSetParams));
		fmGetSetParams.setParams.type = UPDATE_FPM_EXTC_CLEAR;
		FmGetSetParams(p_FmPort->h_Fm, &fmGetSetParams);
}

	memset(&fmGetSetParams, 0, sizeof (t_FmGetSetParams));
	fmGetSetParams.getParams.type = GET_FMFP_EXTC | GET_FM_NPI;
	do
	{
		FmGetSetParams(p_FmPort->h_Fm, &fmGetSetParams);
	} while (fmGetSetParams.getParams.fmfp_extc != 0 && fmGetSetParams.getParams.fm_npi == 0);
	if (fmGetSetParams.getParams.fm_npi != 0)
		XX_Print("FM: Sync did not finish\n");

        // check that all stoped
	memset(&fmGetSetParams, 0, sizeof (t_FmGetSetParams));
        fmGetSetParams.getParams.type = GET_FMQM_GS | GET_FM_NPI;
        FmGetSetParams(p_FmPort->h_Fm, &fmGetSetParams);
	while (fmGetSetParams.getParams.fmqm_gs & 0xF0000000)
	        FmGetSetParams(p_FmPort->h_Fm, &fmGetSetParams);
	if (fmGetSetParams.getParams.fmqm_gs == 0 && fmGetSetParams.getParams.fm_npi == 0)
		XX_Print("FM: Sleeping\n");
//	FM_ChangeClock(p_FmPort->h_Fm, p_FmPort->hardwarePortId);

    return E_OK;
}

void FM_PORT_Dsar_DumpRegs()
{
    uint32_t* hh = XX_PhysToVirt(PTR_TO_UINT(ARDesc));
    DUMP_MEMORY(hh, 0x220);
}

void FM_PORT_ExitDsar(t_Handle h_FmPortRx, t_Handle h_FmPortTx)
{
    t_FmPort *p_FmPort = (t_FmPort *)h_FmPortRx;
    t_FmPort *p_FmPortTx = (t_FmPort *)h_FmPortTx;
    t_Handle *h_FmPcd = FmGetPcd(p_FmPort->h_Fm);
    t_FmPort *p_FmPortHc = FM_PCD_GetHcPort(h_FmPcd);
    t_FmGetSetParams fmGetSetParams;
    memset(&fmGetSetParams, 0, sizeof (t_FmGetSetParams));
    fmGetSetParams.setParams.type = UPDATE_FPM_BRKC_SLP;
    fmGetSetParams.setParams.sleep = 0;
    if (p_FmPort->deepSleepVars.autoResOffsets)
    {
        XX_Free(p_FmPort->deepSleepVars.autoResOffsets);
        p_FmPort->deepSleepVars.autoResOffsets = 0;
    }

    if (p_FmPort->deepSleepVars.dsarEnabledParser)
        PrsDisable(FmGetPcd(p_FmPort->h_Fm));
    WRITE_UINT32(p_FmPort->p_FmPortBmiRegs->rxPortBmiRegs.fmbm_rfpne, p_FmPort->deepSleepVars.fmbm_rfpne);
    WRITE_UINT32(p_FmPort->p_FmPortBmiRegs->rxPortBmiRegs.fmbm_rfne, p_FmPort->deepSleepVars.fmbm_rfne);
    WRITE_UINT32(p_FmPort->p_FmPortBmiRegs->rxPortBmiRegs.fmbm_rcfg, p_FmPort->deepSleepVars.fmbm_rcfg);
    FmGetSetParams(p_FmPort->h_Fm, &fmGetSetParams);
    WRITE_UINT32(p_FmPortTx->p_FmPortBmiRegs->txPortBmiRegs.fmbm_tcmne, p_FmPort->deepSleepVars.fmbm_tcmne);
    WRITE_UINT32(p_FmPortTx->p_FmPortBmiRegs->txPortBmiRegs.fmbm_tcfg, p_FmPort->deepSleepVars.fmbm_tcfg);
    FM_PORT_Enable(p_FmPortHc);
}

bool FM_PORT_IsInDsar(t_Handle h_FmPort)
{
    t_FmPort *p_FmPort = (t_FmPort *)h_FmPort;
    return PTR_TO_UINT(p_FmPort->deepSleepVars.autoResOffsets);
}

t_Error FM_PORT_GetDsarStats(t_Handle h_FmPortRx, t_FmPortDsarStats *stats)
{
    t_FmPort *p_FmPort = (t_FmPort *)h_FmPortRx;
    struct arOffsets *of = (struct arOffsets*)p_FmPort->deepSleepVars.autoResOffsets;
    uint8_t* fmMuramVirtBaseAddr = XX_PhysToVirt(p_FmPort->fmMuramPhysBaseAddr);
    uint32_t *param_page = XX_PhysToVirt(p_FmPort->fmMuramPhysBaseAddr + GET_UINT32(p_FmPort->p_FmPortBmiRegs->rxPortBmiRegs.fmbm_rgpr));
    t_ArCommonDesc *ArCommonDescPtr = (t_ArCommonDesc*)(XX_PhysToVirt(p_FmPort->fmMuramPhysBaseAddr + GET_UINT32(*param_page)));
    t_DsarArpDescriptor *ArpDescriptor = (t_DsarArpDescriptor*)(PTR_TO_UINT(ArCommonDescPtr) + of->arp);
    t_DsarArpStatistics* arp_stats = (t_DsarArpStatistics*)(PTR_TO_UINT(ArpDescriptor->p_Statistics) + fmMuramVirtBaseAddr);
    t_DsarIcmpV4Descriptor* ICMPV4Descriptor = (t_DsarIcmpV4Descriptor*)(PTR_TO_UINT(ArCommonDescPtr) + of->icmpv4);
    t_DsarIcmpV4Statistics* icmpv4_stats = (t_DsarIcmpV4Statistics*)(PTR_TO_UINT(ICMPV4Descriptor->p_Statistics) + fmMuramVirtBaseAddr);
    t_DsarNdDescriptor* NDDescriptor = (t_DsarNdDescriptor*)(PTR_TO_UINT(ArCommonDescPtr) + of->nd);
    t_NdStatistics* nd_stats = (t_NdStatistics*)(PTR_TO_UINT(NDDescriptor->p_Statistics) + fmMuramVirtBaseAddr);
    t_DsarIcmpV6Descriptor* ICMPV6Descriptor = (t_DsarIcmpV6Descriptor*)(PTR_TO_UINT(ArCommonDescPtr) + of->icmpv6);
    t_DsarIcmpV6Statistics* icmpv6_stats = (t_DsarIcmpV6Statistics*)(PTR_TO_UINT(ICMPV6Descriptor->p_Statistics) + fmMuramVirtBaseAddr);
    t_DsarSnmpDescriptor* SnmpDescriptor = (t_DsarSnmpDescriptor*)(PTR_TO_UINT(ArCommonDescPtr) + of->snmp);
    t_DsarSnmpStatistics* snmp_stats = (t_DsarSnmpStatistics*)(PTR_TO_UINT(SnmpDescriptor->p_Statistics) + fmMuramVirtBaseAddr);
    stats->arpArCnt = arp_stats->arCnt;
    stats->echoIcmpv4ArCnt = icmpv4_stats->arCnt;
    stats->ndpArCnt = nd_stats->arCnt;
    stats->echoIcmpv6ArCnt = icmpv6_stats->arCnt;
    stats->snmpGetCnt = snmp_stats->snmpGetReqCnt;
    stats->snmpGetNextCnt = snmp_stats->snmpGetNextReqCnt;
    return E_OK;
}
#endif
