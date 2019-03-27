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
 @File          fm_port_im.c

 @Description   FM Port Independent-Mode ...
*//***************************************************************************/
#include "std_ext.h"
#include "string_ext.h"
#include "error_ext.h"
#include "memcpy_ext.h"
#include "fm_muram_ext.h"

#include "fm_port.h"


#define TX_CONF_STATUS_UNSENT 0x1


typedef enum e_TxConfType
{
     e_TX_CONF_TYPE_CHECK      = 0  /**< check if all the buffers were touched by the muxator, no confirmation callback */
    ,e_TX_CONF_TYPE_CALLBACK   = 1  /**< confirm to user all the available sent buffers */
    ,e_TX_CONF_TYPE_FLUSH      = 3  /**< confirm all buffers plus the unsent one with an appropriate status */
} e_TxConfType;


static void ImException(t_Handle h_FmPort, uint32_t event)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    ASSERT_COND(((event & (IM_EV_RX | IM_EV_BSY)) && FmIsMaster(p_FmPort->h_Fm)) ||
                !FmIsMaster(p_FmPort->h_Fm));

    if (event & IM_EV_RX)
        FmPortImRx(p_FmPort);
    if ((event & IM_EV_BSY) && p_FmPort->f_Exception)
        p_FmPort->f_Exception(p_FmPort->h_App, e_FM_PORT_EXCEPTION_IM_BUSY);
}


static t_Error TxConf(t_FmPort *p_FmPort, e_TxConfType confType)
{
    t_Error             retVal = E_BUSY;
    uint32_t            bdStatus;
    uint16_t            savedStartBdId, confBdId;

    ASSERT_COND(p_FmPort);

    /*
    if (confType==e_TX_CONF_TYPE_CHECK)
        return (WfqEntryIsQueueEmpty(p_FmPort->im.h_WfqEntry) ? E_OK : E_BUSY);
    */

    confBdId = savedStartBdId = p_FmPort->im.currBdId;
    bdStatus = BD_STATUS_AND_LENGTH(BD_GET(confBdId));

    /* If R bit is set, we don't enter, or we break.
       we run till we get to R, or complete the loop */
    while ((!(bdStatus & BD_R_E) || (confType == e_TX_CONF_TYPE_FLUSH)) && (retVal != E_OK))
    {
        if (confType & e_TX_CONF_TYPE_CALLBACK) /* if it is confirmation with user callbacks */
            BD_STATUS_AND_LENGTH_SET(BD_GET(confBdId), 0);

        /* case 1: R bit is 0 and Length is set -> confirm! */
        if ((confType & e_TX_CONF_TYPE_CALLBACK) && (bdStatus & BD_LENGTH_MASK))
        {
            if (p_FmPort->im.f_TxConf)
            {
                if ((confType == e_TX_CONF_TYPE_FLUSH) && (bdStatus & BD_R_E))
                    p_FmPort->im.f_TxConf(p_FmPort->h_App,
                                          BdBufferGet(XX_PhysToVirt, BD_GET(confBdId)),
                                          TX_CONF_STATUS_UNSENT,
                                          p_FmPort->im.p_BdShadow[confBdId]);
                else
                    p_FmPort->im.f_TxConf(p_FmPort->h_App,
                                          BdBufferGet(XX_PhysToVirt, BD_GET(confBdId)),
                                          0,
                                          p_FmPort->im.p_BdShadow[confBdId]);
            }
        }
        /* case 2: R bit is 0 and Length is 0 -> not used yet, nop! */

        confBdId = GetNextBdId(p_FmPort, confBdId);
        if (confBdId == savedStartBdId)
            retVal = E_OK;
        bdStatus = BD_STATUS_AND_LENGTH(BD_GET(confBdId));
    }

    return retVal;
}

t_Error FmPortImEnable(t_FmPort *p_FmPort)
{
    uint32_t    tmpReg = GET_UINT32(p_FmPort->im.p_FmPortImPram->mode);
    WRITE_UINT32(p_FmPort->im.p_FmPortImPram->mode, (uint32_t)(tmpReg & ~IM_MODE_GRC_STP));
    return E_OK;
}

t_Error FmPortImDisable(t_FmPort *p_FmPort)
{
    uint32_t    tmpReg = GET_UINT32(p_FmPort->im.p_FmPortImPram->mode);
    WRITE_UINT32(p_FmPort->im.p_FmPortImPram->mode, (uint32_t)(tmpReg | IM_MODE_GRC_STP));
    return E_OK;
}

t_Error FmPortImRx(t_FmPort *p_FmPort)
{
    t_Handle                h_CurrUserPriv, h_NewUserPriv;
    uint32_t                bdStatus;
    volatile uint8_t        buffPos;
    uint16_t                length;
    uint16_t                errors;
    uint8_t                 *p_CurData, *p_Data;
    uint32_t                flags;

    ASSERT_COND(p_FmPort);

    flags = XX_LockIntrSpinlock(p_FmPort->h_Spinlock);
    if (p_FmPort->lock)
    {
        XX_UnlockIntrSpinlock(p_FmPort->h_Spinlock, flags);
        return E_OK;
    }
    p_FmPort->lock = TRUE;
    XX_UnlockIntrSpinlock(p_FmPort->h_Spinlock, flags);

    bdStatus = BD_STATUS_AND_LENGTH(BD_GET(p_FmPort->im.currBdId));

    while (!(bdStatus & BD_R_E)) /* while there is data in the Rx BD */
    {
        if ((p_Data = p_FmPort->im.rxPool.f_GetBuf(p_FmPort->im.rxPool.h_BufferPool, &h_NewUserPriv)) == NULL)
        {
            p_FmPort->lock = FALSE;
            RETURN_ERROR(MAJOR, E_NOT_AVAILABLE, ("Data buffer"));
        }

        if (p_FmPort->im.firstBdOfFrameId == IM_ILEGAL_BD_ID)
            p_FmPort->im.firstBdOfFrameId = p_FmPort->im.currBdId;

        p_CurData = BdBufferGet(p_FmPort->im.rxPool.f_PhysToVirt, BD_GET(p_FmPort->im.currBdId));
        h_CurrUserPriv = p_FmPort->im.p_BdShadow[p_FmPort->im.currBdId];
        length = (uint16_t)((bdStatus & BD_L) ?
                            ((bdStatus & BD_LENGTH_MASK) - p_FmPort->im.rxFrameAccumLength):
                            (bdStatus & BD_LENGTH_MASK));
        p_FmPort->im.rxFrameAccumLength += length;

        /* determine whether buffer is first, last, first and last (single  */
        /* buffer frame) or middle (not first and not last)                 */
        buffPos = (uint8_t)((p_FmPort->im.currBdId == p_FmPort->im.firstBdOfFrameId) ?
                            ((bdStatus & BD_L) ? SINGLE_BUF : FIRST_BUF) :
                            ((bdStatus & BD_L) ? LAST_BUF : MIDDLE_BUF));

        if (bdStatus & BD_L)
        {
            p_FmPort->im.rxFrameAccumLength = 0;
            p_FmPort->im.firstBdOfFrameId = IM_ILEGAL_BD_ID;
        }

        BdBufferSet(p_FmPort->im.rxPool.f_VirtToPhys, BD_GET(p_FmPort->im.currBdId), p_Data);

        BD_STATUS_AND_LENGTH_SET(BD_GET(p_FmPort->im.currBdId), BD_R_E);

        errors = (uint16_t)((bdStatus & BD_RX_ERRORS) >> 16);
        p_FmPort->im.p_BdShadow[p_FmPort->im.currBdId] = h_NewUserPriv;

        p_FmPort->im.currBdId = GetNextBdId(p_FmPort, p_FmPort->im.currBdId);
        WRITE_UINT16(p_FmPort->im.p_FmPortImPram->rxQd.offsetOut, (uint16_t)(p_FmPort->im.currBdId<<4));
        /* Pass the buffer if one of the conditions is true:
        - There are no errors
        - This is a part of a larger frame ( the application has already received some buffers ) */
        if ((buffPos != SINGLE_BUF) || !errors)
        {
            if (p_FmPort->im.f_RxStore(p_FmPort->h_App,
                                       p_CurData,
                                       length,
                                       errors,
                                       buffPos,
                                       h_CurrUserPriv) == e_RX_STORE_RESPONSE_PAUSE)
                break;
        }
        else if (p_FmPort->im.rxPool.f_PutBuf(p_FmPort->im.rxPool.h_BufferPool,
                                              p_CurData,
                                              h_CurrUserPriv))
        {
            p_FmPort->lock = FALSE;
            RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Failed freeing data buffer"));
        }

        bdStatus = BD_STATUS_AND_LENGTH(BD_GET(p_FmPort->im.currBdId));
    }
    p_FmPort->lock = FALSE;
    return E_OK;
}

void FmPortConfigIM (t_FmPort *p_FmPort, t_FmPortParams *p_FmPortParams)
{
    ASSERT_COND(p_FmPort);

    SANITY_CHECK_RETURN(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    p_FmPort->im.h_FmMuram                      = p_FmPortParams->specificParams.imRxTxParams.h_FmMuram;
    p_FmPort->p_FmPortDriverParam->liodnOffset  = p_FmPortParams->specificParams.imRxTxParams.liodnOffset;
    p_FmPort->im.dataMemId                      = p_FmPortParams->specificParams.imRxTxParams.dataMemId;
    p_FmPort->im.dataMemAttributes              = p_FmPortParams->specificParams.imRxTxParams.dataMemAttributes;

    p_FmPort->im.fwExtStructsMemId              = DEFAULT_PORT_ImfwExtStructsMemId;
    p_FmPort->im.fwExtStructsMemAttr            = DEFAULT_PORT_ImfwExtStructsMemAttr;

    if ((p_FmPort->portType == e_FM_PORT_TYPE_RX) ||
        (p_FmPort->portType == e_FM_PORT_TYPE_RX_10G))
    {
        p_FmPort->im.rxPool.h_BufferPool    = p_FmPortParams->specificParams.imRxTxParams.rxPoolParams.h_BufferPool;
        p_FmPort->im.rxPool.f_GetBuf        = p_FmPortParams->specificParams.imRxTxParams.rxPoolParams.f_GetBuf;
        p_FmPort->im.rxPool.f_PutBuf        = p_FmPortParams->specificParams.imRxTxParams.rxPoolParams.f_PutBuf;
        p_FmPort->im.rxPool.bufferSize      = p_FmPortParams->specificParams.imRxTxParams.rxPoolParams.bufferSize;
        p_FmPort->im.rxPool.f_PhysToVirt    = p_FmPortParams->specificParams.imRxTxParams.rxPoolParams.f_PhysToVirt;
        if (!p_FmPort->im.rxPool.f_PhysToVirt)
            p_FmPort->im.rxPool.f_PhysToVirt = XX_PhysToVirt;
        p_FmPort->im.rxPool.f_VirtToPhys    = p_FmPortParams->specificParams.imRxTxParams.rxPoolParams.f_VirtToPhys;
        if (!p_FmPort->im.rxPool.f_VirtToPhys)
            p_FmPort->im.rxPool.f_VirtToPhys = XX_VirtToPhys;
        p_FmPort->im.f_RxStore              = p_FmPortParams->specificParams.imRxTxParams.f_RxStore;

        p_FmPort->im.mrblr                  = 0x8000;
        while (p_FmPort->im.mrblr)
        {
            if (p_FmPort->im.rxPool.bufferSize & p_FmPort->im.mrblr)
                break;
            p_FmPort->im.mrblr >>= 1;
        }
        if (p_FmPort->im.mrblr != p_FmPort->im.rxPool.bufferSize)
            DBG(WARNING, ("Max-Rx-Buffer-Length set to %d", p_FmPort->im.mrblr));
        p_FmPort->im.bdRingSize             = DEFAULT_PORT_rxBdRingLength;
        p_FmPort->exceptions                = DEFAULT_PORT_exception;
        if (FmIsMaster(p_FmPort->h_Fm))
            p_FmPort->polling               = FALSE;
        else
            p_FmPort->polling               = TRUE;
        p_FmPort->fmanCtrlEventId           = (uint8_t)NO_IRQ;
    }
    else
    {
        p_FmPort->im.f_TxConf               = p_FmPortParams->specificParams.imRxTxParams.f_TxConf;

        p_FmPort->im.bdRingSize             = DEFAULT_PORT_txBdRingLength;
    }
}

t_Error FmPortImCheckInitParameters(t_FmPort *p_FmPort)
{
    if ((p_FmPort->portType != e_FM_PORT_TYPE_RX) &&
        (p_FmPort->portType != e_FM_PORT_TYPE_RX_10G) &&
        (p_FmPort->portType != e_FM_PORT_TYPE_TX) &&
        (p_FmPort->portType != e_FM_PORT_TYPE_TX_10G))
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, NO_MSG);

    if ((p_FmPort->portType == e_FM_PORT_TYPE_RX) ||
        (p_FmPort->portType == e_FM_PORT_TYPE_RX_10G))
    {
        if (!POWER_OF_2(p_FmPort->im.mrblr))
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("max Rx buffer length must be power of 2!!!"));
        if (p_FmPort->im.mrblr < 256)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("max Rx buffer length must at least 256!!!"));
        if (p_FmPort->p_FmPortDriverParam->liodnOffset & ~FM_LIODN_OFFSET_MASK)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("liodnOffset is larger than %d", FM_LIODN_OFFSET_MASK+1));
    }

    return E_OK;
}

t_Error FmPortImInit(t_FmPort *p_FmPort)
{
    t_FmImBd    *p_Bd=NULL;
    t_Handle    h_BufContext;
    uint64_t    tmpPhysBase;
    uint16_t    log2Num;
    uint8_t     *p_Data/*, *p_Tmp*/;
    int         i;
    t_Error     err;
    uint16_t    tmpReg16;
    uint32_t    tmpReg32;

    ASSERT_COND(p_FmPort);

    p_FmPort->im.p_FmPortImPram =
        (t_FmPortImPram *)FM_MURAM_AllocMem(p_FmPort->im.h_FmMuram, sizeof(t_FmPortImPram), IM_PRAM_ALIGN);
    if (!p_FmPort->im.p_FmPortImPram)
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("Independent-Mode Parameter-RAM!!!"));
    WRITE_BLOCK(p_FmPort->im.p_FmPortImPram, 0, sizeof(t_FmPortImPram));

    if ((p_FmPort->portType == e_FM_PORT_TYPE_RX) ||
        (p_FmPort->portType == e_FM_PORT_TYPE_RX_10G))
    {
        p_FmPort->im.p_BdRing =
            (t_FmImBd *)XX_MallocSmart((uint32_t)(sizeof(t_FmImBd)*p_FmPort->im.bdRingSize),
                                       p_FmPort->im.fwExtStructsMemId,
                                       4);
        if (!p_FmPort->im.p_BdRing)
            RETURN_ERROR(MAJOR, E_NO_MEMORY, ("Independent-Mode Rx BD ring!!!"));
        IOMemSet32(p_FmPort->im.p_BdRing, 0, (uint32_t)(sizeof(t_FmImBd)*p_FmPort->im.bdRingSize));

        p_FmPort->im.p_BdShadow = (t_Handle *)XX_Malloc((uint32_t)(sizeof(t_Handle)*p_FmPort->im.bdRingSize));
        if (!p_FmPort->im.p_BdShadow)
            RETURN_ERROR(MAJOR, E_NO_MEMORY, ("Independent-Mode Rx BD shadow!!!"));
        memset(p_FmPort->im.p_BdShadow, 0, (uint32_t)(sizeof(t_Handle)*p_FmPort->im.bdRingSize));

        /* Initialize the Rx-BD ring */
        for (i=0; i<p_FmPort->im.bdRingSize; i++)
        {
            p_Bd = BD_GET(i);
            BD_STATUS_AND_LENGTH_SET (p_Bd, BD_R_E);

            if ((p_Data = p_FmPort->im.rxPool.f_GetBuf(p_FmPort->im.rxPool.h_BufferPool, &h_BufContext)) == NULL)
                RETURN_ERROR(MAJOR, E_NOT_AVAILABLE, ("Data buffer"));
            BdBufferSet(p_FmPort->im.rxPool.f_VirtToPhys, p_Bd, p_Data);
            p_FmPort->im.p_BdShadow[i] = h_BufContext;
        }

        if ((p_FmPort->im.dataMemAttributes & MEMORY_ATTR_CACHEABLE) ||
            (p_FmPort->im.fwExtStructsMemAttr & MEMORY_ATTR_CACHEABLE))
            WRITE_UINT32(p_FmPort->im.p_FmPortImPram->mode, IM_MODE_GBL | IM_MODE_SET_BO(2));
        else
            WRITE_UINT32(p_FmPort->im.p_FmPortImPram->mode, IM_MODE_SET_BO(2));

        WRITE_UINT32(p_FmPort->im.p_FmPortImPram->rxQdPtr,
                     (uint32_t)((uint64_t)(XX_VirtToPhys(p_FmPort->im.p_FmPortImPram)) -
                                p_FmPort->fmMuramPhysBaseAddr + 0x20));

        LOG2((uint64_t)p_FmPort->im.mrblr, log2Num);
        WRITE_UINT16(p_FmPort->im.p_FmPortImPram->mrblr, log2Num);

        /* Initialize Rx QD */
        tmpPhysBase = (uint64_t)(XX_VirtToPhys(p_FmPort->im.p_BdRing));
        SET_ADDR(&p_FmPort->im.p_FmPortImPram->rxQd.bdRingBase, tmpPhysBase);
        WRITE_UINT16(p_FmPort->im.p_FmPortImPram->rxQd.bdRingSize, (uint16_t)(sizeof(t_FmImBd)*p_FmPort->im.bdRingSize));

        /* Update the IM PRAM address in the BMI */
        WRITE_UINT32(p_FmPort->p_FmPortBmiRegs->rxPortBmiRegs.fmbm_rfqid,
                     (uint32_t)((uint64_t)(XX_VirtToPhys(p_FmPort->im.p_FmPortImPram)) -
                                p_FmPort->fmMuramPhysBaseAddr));
        if (!p_FmPort->polling || p_FmPort->exceptions)
        {
            /* Allocate, configure and register interrupts */
            err = FmAllocFmanCtrlEventReg(p_FmPort->h_Fm, &p_FmPort->fmanCtrlEventId);
            if (err)
                RETURN_ERROR(MAJOR, err, NO_MSG);

            ASSERT_COND(!(p_FmPort->fmanCtrlEventId & ~IM_RXQD_FPMEVT_SEL_MASK));
            tmpReg16 = (uint16_t)(p_FmPort->fmanCtrlEventId & IM_RXQD_FPMEVT_SEL_MASK);
            tmpReg32 = 0;

            if (p_FmPort->exceptions & IM_EV_BSY)
            {
                tmpReg16 |= IM_RXQD_BSYINTM;
                tmpReg32 |= IM_EV_BSY;
            }
            if (!p_FmPort->polling)
            {
                tmpReg16 |= IM_RXQD_RXFINTM;
                tmpReg32 |= IM_EV_RX;
            }
            WRITE_UINT16(p_FmPort->im.p_FmPortImPram->rxQd.gen, tmpReg16);

            FmRegisterFmanCtrlIntr(p_FmPort->h_Fm, p_FmPort->fmanCtrlEventId, ImException , (t_Handle)p_FmPort);

            FmSetFmanCtrlIntr(p_FmPort->h_Fm, p_FmPort->fmanCtrlEventId, tmpReg32);
        }
        else
            p_FmPort->fmanCtrlEventId = (uint8_t)NO_IRQ;
    }
    else
    {
        p_FmPort->im.p_BdRing = (t_FmImBd *)XX_MallocSmart((uint32_t)(sizeof(t_FmImBd)*p_FmPort->im.bdRingSize), p_FmPort->im.fwExtStructsMemId, 4);
        if (!p_FmPort->im.p_BdRing)
            RETURN_ERROR(MAJOR, E_NO_MEMORY, ("Independent-Mode Tx BD ring!!!"));
        IOMemSet32(p_FmPort->im.p_BdRing, 0, (uint32_t)(sizeof(t_FmImBd)*p_FmPort->im.bdRingSize));

        p_FmPort->im.p_BdShadow = (t_Handle *)XX_Malloc((uint32_t)(sizeof(t_Handle)*p_FmPort->im.bdRingSize));
        if (!p_FmPort->im.p_BdShadow)
            RETURN_ERROR(MAJOR, E_NO_MEMORY, ("Independent-Mode Rx BD shadow!!!"));
        memset(p_FmPort->im.p_BdShadow, 0, (uint32_t)(sizeof(t_Handle)*p_FmPort->im.bdRingSize));
        p_FmPort->im.firstBdOfFrameId = IM_ILEGAL_BD_ID;

        if ((p_FmPort->im.dataMemAttributes & MEMORY_ATTR_CACHEABLE) ||
            (p_FmPort->im.fwExtStructsMemAttr & MEMORY_ATTR_CACHEABLE))
            WRITE_UINT32(p_FmPort->im.p_FmPortImPram->mode, IM_MODE_GBL | IM_MODE_SET_BO(2));
        else
            WRITE_UINT32(p_FmPort->im.p_FmPortImPram->mode, IM_MODE_SET_BO(2));

        WRITE_UINT32(p_FmPort->im.p_FmPortImPram->txQdPtr,
                     (uint32_t)((uint64_t)(XX_VirtToPhys(p_FmPort->im.p_FmPortImPram)) -
                                p_FmPort->fmMuramPhysBaseAddr + 0x40));

        /* Initialize Tx QD */
        tmpPhysBase = (uint64_t)(XX_VirtToPhys(p_FmPort->im.p_BdRing));
        SET_ADDR(&p_FmPort->im.p_FmPortImPram->txQd.bdRingBase, tmpPhysBase);
        WRITE_UINT16(p_FmPort->im.p_FmPortImPram->txQd.bdRingSize, (uint16_t)(sizeof(t_FmImBd)*p_FmPort->im.bdRingSize));

        /* Update the IM PRAM address in the BMI */
        WRITE_UINT32(p_FmPort->p_FmPortBmiRegs->txPortBmiRegs.fmbm_tcfqid,
                     (uint32_t)((uint64_t)(XX_VirtToPhys(p_FmPort->im.p_FmPortImPram)) -
                                p_FmPort->fmMuramPhysBaseAddr));
    }


    return E_OK;
}

void FmPortImFree(t_FmPort *p_FmPort)
{
    uint32_t    bdStatus;
    uint8_t     *p_CurData;

    ASSERT_COND(p_FmPort);
    ASSERT_COND(p_FmPort->im.p_FmPortImPram);

    if ((p_FmPort->portType == e_FM_PORT_TYPE_RX) ||
        (p_FmPort->portType == e_FM_PORT_TYPE_RX_10G))
    {
        if (!p_FmPort->polling || p_FmPort->exceptions)
        {
            /* Deallocate and unregister interrupts */
            FmSetFmanCtrlIntr(p_FmPort->h_Fm, p_FmPort->fmanCtrlEventId, 0);

            FmFreeFmanCtrlEventReg(p_FmPort->h_Fm, p_FmPort->fmanCtrlEventId);

            WRITE_UINT16(p_FmPort->im.p_FmPortImPram->rxQd.gen, 0);

            FmUnregisterFmanCtrlIntr(p_FmPort->h_Fm, p_FmPort->fmanCtrlEventId);
        }
        /* Try first clean what has received */
        FmPortImRx(p_FmPort);

        /* Now, get rid of the the empty buffer! */
        bdStatus = BD_STATUS_AND_LENGTH(BD_GET(p_FmPort->im.currBdId));

        while (bdStatus & BD_R_E) /* while there is data in the Rx BD */
        {
            p_CurData = BdBufferGet(p_FmPort->im.rxPool.f_PhysToVirt, BD_GET(p_FmPort->im.currBdId));

            BdBufferSet(p_FmPort->im.rxPool.f_VirtToPhys, BD_GET(p_FmPort->im.currBdId), NULL);
            BD_STATUS_AND_LENGTH_SET(BD_GET(p_FmPort->im.currBdId), 0);

            p_FmPort->im.rxPool.f_PutBuf(p_FmPort->im.rxPool.h_BufferPool,
                                         p_CurData,
                                         p_FmPort->im.p_BdShadow[p_FmPort->im.currBdId]);

            p_FmPort->im.currBdId = GetNextBdId(p_FmPort, p_FmPort->im.currBdId);
            bdStatus = BD_STATUS_AND_LENGTH(BD_GET(p_FmPort->im.currBdId));
        }
    }
    else
        TxConf(p_FmPort, e_TX_CONF_TYPE_FLUSH);

    FM_MURAM_FreeMem(p_FmPort->im.h_FmMuram, p_FmPort->im.p_FmPortImPram);

    if (p_FmPort->im.p_BdShadow)
        XX_Free(p_FmPort->im.p_BdShadow);

    if (p_FmPort->im.p_BdRing)
        XX_FreeSmart(p_FmPort->im.p_BdRing);
}


t_Error FM_PORT_ConfigIMMaxRxBufLength(t_Handle h_FmPort, uint16_t newVal)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->imEn, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    p_FmPort->im.mrblr = newVal;

    return E_OK;
}

t_Error FM_PORT_ConfigIMRxBdRingLength(t_Handle h_FmPort, uint16_t newVal)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->imEn, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    p_FmPort->im.bdRingSize = newVal;

    return E_OK;
}

t_Error FM_PORT_ConfigIMTxBdRingLength(t_Handle h_FmPort, uint16_t newVal)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->imEn, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    p_FmPort->im.bdRingSize = newVal;

    return E_OK;
}

t_Error  FM_PORT_ConfigIMFmanCtrlExternalStructsMemory(t_Handle h_FmPort,
                                                       uint8_t  memId,
                                                       uint32_t memAttributes)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->imEn, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    p_FmPort->im.fwExtStructsMemId              = memId;
    p_FmPort->im.fwExtStructsMemAttr            = memAttributes;

    return E_OK;
}

t_Error FM_PORT_ConfigIMPolling(t_Handle h_FmPort)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->imEn, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    if ((p_FmPort->portType != e_FM_PORT_TYPE_RX_10G) && (p_FmPort->portType != e_FM_PORT_TYPE_RX))
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION, ("Available for Rx ports only"));

    if (!FmIsMaster(p_FmPort->h_Fm))
        RETURN_ERROR(MAJOR, E_INVALID_OPERATION, ("Available on master-partition only;"
                                                  "in guest-partitions, IM is always in polling!"));

    p_FmPort->polling = TRUE;

    return E_OK;
}

t_Error FM_PORT_SetIMExceptions(t_Handle h_FmPort, e_FmPortExceptions exception, bool enable)
{
    t_FmPort    *p_FmPort = (t_FmPort*)h_FmPort;
    t_Error     err;
    uint16_t    tmpReg16;
    uint32_t    tmpReg32;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->imEn, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    if (exception == e_FM_PORT_EXCEPTION_IM_BUSY)
    {
        if (enable)
        {
            p_FmPort->exceptions |= IM_EV_BSY;
            if (p_FmPort->fmanCtrlEventId == (uint8_t)NO_IRQ)
            {
                /* Allocate, configure and register interrupts */
                err = FmAllocFmanCtrlEventReg(p_FmPort->h_Fm, &p_FmPort->fmanCtrlEventId);
                if (err)
                    RETURN_ERROR(MAJOR, err, NO_MSG);
                ASSERT_COND(!(p_FmPort->fmanCtrlEventId & ~IM_RXQD_FPMEVT_SEL_MASK));

                FmRegisterFmanCtrlIntr(p_FmPort->h_Fm, p_FmPort->fmanCtrlEventId, ImException, (t_Handle)p_FmPort);
                tmpReg16 = (uint16_t)((p_FmPort->fmanCtrlEventId & IM_RXQD_FPMEVT_SEL_MASK) | IM_RXQD_BSYINTM);
                tmpReg32 = IM_EV_BSY;
            }
            else
            {
                tmpReg16 = (uint16_t)(GET_UINT16(p_FmPort->im.p_FmPortImPram->rxQd.gen) | IM_RXQD_BSYINTM);
                tmpReg32 = FmGetFmanCtrlIntr(p_FmPort->h_Fm, p_FmPort->fmanCtrlEventId) | IM_EV_BSY;
            }

            WRITE_UINT16(p_FmPort->im.p_FmPortImPram->rxQd.gen, tmpReg16);
            FmSetFmanCtrlIntr(p_FmPort->h_Fm, p_FmPort->fmanCtrlEventId, tmpReg32);
        }
        else
        {
            p_FmPort->exceptions &= ~IM_EV_BSY;
            if (!p_FmPort->exceptions && p_FmPort->polling)
            {
                FmFreeFmanCtrlEventReg(p_FmPort->h_Fm, p_FmPort->fmanCtrlEventId);
                FmUnregisterFmanCtrlIntr(p_FmPort->h_Fm, p_FmPort->fmanCtrlEventId);
                FmSetFmanCtrlIntr(p_FmPort->h_Fm, p_FmPort->fmanCtrlEventId, 0);
                WRITE_UINT16(p_FmPort->im.p_FmPortImPram->rxQd.gen, 0);
                p_FmPort->fmanCtrlEventId = (uint8_t)NO_IRQ;
            }
            else
            {
                tmpReg16 = (uint16_t)(GET_UINT16(p_FmPort->im.p_FmPortImPram->rxQd.gen) & ~IM_RXQD_BSYINTM);
                WRITE_UINT16(p_FmPort->im.p_FmPortImPram->rxQd.gen, tmpReg16);
                tmpReg32 = FmGetFmanCtrlIntr(p_FmPort->h_Fm, p_FmPort->fmanCtrlEventId) & ~IM_EV_BSY;
                FmSetFmanCtrlIntr(p_FmPort->h_Fm, p_FmPort->fmanCtrlEventId, tmpReg32);
            }
        }
    }
    else
        RETURN_ERROR(MINOR, E_INVALID_SELECTION, ("Invalid exception."));

    return E_OK;
}

t_Error  FM_PORT_ImTx( t_Handle               h_FmPort,
                       uint8_t                *p_Data,
                       uint16_t               length,
                       bool                   lastBuffer,
                       t_Handle               h_BufContext)
{
    t_FmPort            *p_FmPort = (t_FmPort*)h_FmPort;
    uint16_t            nextBdId;
    uint32_t            bdStatus, nextBdStatus;
    bool                firstBuffer;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->imEn, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    bdStatus = BD_STATUS_AND_LENGTH(BD_GET(p_FmPort->im.currBdId));
    nextBdId = GetNextBdId(p_FmPort, p_FmPort->im.currBdId);
    nextBdStatus = BD_STATUS_AND_LENGTH(BD_GET(nextBdId));

    if (!(bdStatus & BD_R_E) && !(nextBdStatus & BD_R_E))
    {
        /* Confirm the current BD - BD is available */
        if ((bdStatus & BD_LENGTH_MASK) && (p_FmPort->im.f_TxConf))
            p_FmPort->im.f_TxConf (p_FmPort->h_App,
                                   BdBufferGet(XX_PhysToVirt, BD_GET(p_FmPort->im.currBdId)),
                                   0,
                                   p_FmPort->im.p_BdShadow[p_FmPort->im.currBdId]);

        bdStatus = length;

        /* if this is the first BD of a frame */
        if (p_FmPort->im.firstBdOfFrameId == IM_ILEGAL_BD_ID)
        {
            firstBuffer = TRUE;
            p_FmPort->im.txFirstBdStatus = (bdStatus | BD_R_E);

            if (!lastBuffer)
                p_FmPort->im.firstBdOfFrameId = p_FmPort->im.currBdId;
        }
        else
            firstBuffer = FALSE;

        BdBufferSet(XX_VirtToPhys, BD_GET(p_FmPort->im.currBdId), p_Data);
        p_FmPort->im.p_BdShadow[p_FmPort->im.currBdId] = h_BufContext;

        /* deal with last */
        if (lastBuffer)
        {
            /* if single buffer frame */
            if (firstBuffer)
                BD_STATUS_AND_LENGTH_SET(BD_GET(p_FmPort->im.currBdId), p_FmPort->im.txFirstBdStatus | BD_L);
            else
            {
                /* Set the last BD of the frame */
                BD_STATUS_AND_LENGTH_SET (BD_GET(p_FmPort->im.currBdId), (bdStatus | BD_R_E | BD_L));
                /* Set the first BD of the frame */
                BD_STATUS_AND_LENGTH_SET(BD_GET(p_FmPort->im.firstBdOfFrameId), p_FmPort->im.txFirstBdStatus);
                p_FmPort->im.firstBdOfFrameId = IM_ILEGAL_BD_ID;
            }
            WRITE_UINT16(p_FmPort->im.p_FmPortImPram->txQd.offsetIn, (uint16_t)(GetNextBdId(p_FmPort, p_FmPort->im.currBdId)<<4));
        }
        else if (!firstBuffer) /* mid frame buffer */
            BD_STATUS_AND_LENGTH_SET (BD_GET(p_FmPort->im.currBdId), bdStatus | BD_R_E);

        p_FmPort->im.currBdId = GetNextBdId(p_FmPort, p_FmPort->im.currBdId);
    }
    else
    {
        /* Discard current frame. Return error.   */
        if (p_FmPort->im.firstBdOfFrameId != IM_ILEGAL_BD_ID)
        {
            /* Error:    No free BD */
            /* Response: Discard current frame. Return error.   */
            uint16_t   cleanBdId = p_FmPort->im.firstBdOfFrameId;

            ASSERT_COND(p_FmPort->im.firstBdOfFrameId != p_FmPort->im.currBdId);

            /* Since firstInFrame is not NULL, one buffer at least has already been
               inserted into the BD ring. Using do-while covers the situation of a
               frame spanned throughout the whole Tx BD ring (p_CleanBd is incremented
               prior to testing whether or not it's equal to TxBd). */
            do
            {
                BD_STATUS_AND_LENGTH_SET(BD_GET(cleanBdId), 0);
                /* Advance BD pointer */
                cleanBdId = GetNextBdId(p_FmPort, cleanBdId);
            } while (cleanBdId != p_FmPort->im.currBdId);

            p_FmPort->im.currBdId = cleanBdId;
            p_FmPort->im.firstBdOfFrameId = IM_ILEGAL_BD_ID;
        }

        return ERROR_CODE(E_FULL);
    }

    return E_OK;
}

void FM_PORT_ImTxConf(t_Handle h_FmPort)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN(p_FmPort->imEn, E_INVALID_STATE);
    SANITY_CHECK_RETURN(!p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    TxConf(p_FmPort, e_TX_CONF_TYPE_CALLBACK);
}

t_Error  FM_PORT_ImRx(t_Handle h_FmPort)
{
    t_FmPort *p_FmPort = (t_FmPort*)h_FmPort;

    SANITY_CHECK_RETURN_ERROR(p_FmPort, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmPort->imEn, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(!p_FmPort->p_FmPortDriverParam, E_INVALID_HANDLE);

    return FmPortImRx(p_FmPort);
}
