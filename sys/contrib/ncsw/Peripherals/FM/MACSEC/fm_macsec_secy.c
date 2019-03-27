/*
 * Copyright 2008-2015 Freescale Semiconductor Inc.
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
 @File          fm_macsec_secy.c

 @Description   FM MACSEC SECY driver routines implementation.
*//***************************************************************************/

#include "std_ext.h"
#include "error_ext.h"
#include "xx_ext.h"
#include "string_ext.h"
#include "sprint_ext.h"

#include "fm_macsec_secy.h"


/****************************************/
/*       static functions               */
/****************************************/
static void FmMacsecSecYExceptionsIsr(t_Handle h_FmMacsecSecY, uint32_t id)
{
    t_FmMacsecSecY *p_FmMacsecSecY = (t_FmMacsecSecY *)h_FmMacsecSecY;

    UNUSED(id);
    SANITY_CHECK_RETURN(p_FmMacsecSecY, E_INVALID_HANDLE);

    if (p_FmMacsecSecY->exceptions & FM_MACSEC_SECY_EX_FRAME_DISCARDED)
        p_FmMacsecSecY->f_Exception(p_FmMacsecSecY->h_App, e_FM_MACSEC_SECY_EX_FRAME_DISCARDED);
}

static void FmMacsecSecYEventsIsr(t_Handle h_FmMacsecSecY, uint32_t id)
{
    t_FmMacsecSecY *p_FmMacsecSecY = (t_FmMacsecSecY *)h_FmMacsecSecY;

    UNUSED(id);
    SANITY_CHECK_RETURN(p_FmMacsecSecY, E_INVALID_HANDLE);

    if (p_FmMacsecSecY->events & FM_MACSEC_SECY_EV_NEXT_PN)
        p_FmMacsecSecY->f_Event(p_FmMacsecSecY->h_App, e_FM_MACSEC_SECY_EV_NEXT_PN);
}

static t_Error CheckFmMacsecSecYParameters(t_FmMacsecSecY *p_FmMacsecSecY)
{
    if (!p_FmMacsecSecY->f_Exception)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Exceptions callback not provided"));

    if (!p_FmMacsecSecY->f_Event)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Events callback not provided"));

    if (!p_FmMacsecSecY->numOfRxSc)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Num of Rx Scs must be greater than '0'"));


    return E_OK;
}

static t_Handle FmMacsecSecYCreateSc(t_FmMacsecSecY             *p_FmMacsecSecY,
                                     macsecSCI_t                sci,
                                     e_FmMacsecSecYCipherSuite  cipherSuite,
                                     e_ScType                   type)
{
    t_SecYSc        *p_ScTable;
    void            *p_Params;
    uint32_t        numOfSc,i;
    t_Error         err = E_OK;
    t_RxScParams    rxScParams;
    t_TxScParams    txScParams;

    ASSERT_COND(p_FmMacsecSecY);
    ASSERT_COND(p_FmMacsecSecY->h_FmMacsec);

    if (type == e_SC_RX)
    {
        memset(&rxScParams, 0, sizeof(rxScParams));
        i                                   = (NUM_OF_RX_SC - 1);
        p_ScTable                           = p_FmMacsecSecY->p_RxSc;
        numOfSc                             = p_FmMacsecSecY->numOfRxSc;
        rxScParams.confidentialityOffset    = p_FmMacsecSecY->confidentialityOffset;
        rxScParams.replayProtect            = p_FmMacsecSecY->replayProtect;
        rxScParams.replayWindow             = p_FmMacsecSecY->replayWindow;
        rxScParams.validateFrames           = p_FmMacsecSecY->validateFrames;
        rxScParams.cipherSuite              = cipherSuite;
        p_Params = &rxScParams;
    }
    else
    {
        memset(&txScParams, 0, sizeof(txScParams));
        i                                   = (NUM_OF_TX_SC - 1);
        p_ScTable                           = p_FmMacsecSecY->p_TxSc;
        numOfSc                             = p_FmMacsecSecY->numOfTxSc;
        txScParams.sciInsertionMode         = p_FmMacsecSecY->sciInsertionMode;
        txScParams.protectFrames            = p_FmMacsecSecY->protectFrames;
        txScParams.confidentialityEnable    = p_FmMacsecSecY->confidentialityEnable;
        txScParams.confidentialityOffset    = p_FmMacsecSecY->confidentialityOffset;
        txScParams.cipherSuite              = cipherSuite;
        p_Params = &txScParams;
    }

    for (i=0;i<numOfSc;i++)
        if (!p_ScTable[i].inUse)
            break;
    if (i == numOfSc)
    {
        REPORT_ERROR(MAJOR, E_FULL, ("FM MACSEC SECY SC"));
        return NULL;
    }

    if (type == e_SC_RX)
    {
        ((t_RxScParams *)p_Params)->scId = p_ScTable[i].scId;
        ((t_RxScParams *)p_Params)->sci  = sci;
        if ((err = FmMacsecCreateRxSc(p_FmMacsecSecY->h_FmMacsec, (t_RxScParams *)p_Params)) != E_OK)
        {
            REPORT_ERROR(MAJOR, E_NO_MEMORY, ("FM MACSEC SECY RX SC"));
            return NULL;
        }
    }
    else
    {
        ((t_TxScParams *)p_Params)->scId = p_ScTable[i].scId;
        ((t_TxScParams *)p_Params)->sci  = sci;
        if ((err = FmMacsecCreateTxSc(p_FmMacsecSecY->h_FmMacsec, (t_TxScParams *)p_Params)) != E_OK)
        {
            REPORT_ERROR(MAJOR, E_NO_MEMORY, ("FM MACSEC SECY TX SC"));
            return NULL;
        }
    }

    p_ScTable[i].inUse = TRUE;
    return &p_ScTable[i];
}

static t_Error FmMacsecSecYDeleteSc(t_FmMacsecSecY *p_FmMacsecSecY, t_SecYSc *p_FmSecYSc, e_ScType type)
{
    t_Error         err = E_OK;

    ASSERT_COND(p_FmMacsecSecY);
    ASSERT_COND(p_FmMacsecSecY->h_FmMacsec);
    ASSERT_COND(p_FmSecYSc);

    if (type == e_SC_RX)
    {
        if ((err = FmMacsecDeleteRxSc(p_FmMacsecSecY->h_FmMacsec, p_FmSecYSc->scId)) != E_OK)
            RETURN_ERROR(MINOR, err, NO_MSG);
    }
    else
        if ((err = FmMacsecDeleteTxSc(p_FmMacsecSecY->h_FmMacsec, p_FmSecYSc->scId)) != E_OK)
            RETURN_ERROR(MINOR, err, NO_MSG);

    p_FmSecYSc->inUse = FALSE;

    return err;
}

/****************************************/
/*       API Init unit functions        */
/****************************************/
t_Handle FM_MACSEC_SECY_Config(t_FmMacsecSecYParams *p_FmMacsecSecYParam)
{
    t_FmMacsecSecY  *p_FmMacsecSecY;

    /* Allocate FM MACSEC structure */
    p_FmMacsecSecY = (t_FmMacsecSecY *) XX_Malloc(sizeof(t_FmMacsecSecY));
    if (!p_FmMacsecSecY)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("FM MACSEC SECY driver structure"));
        return NULL;
    }
    memset(p_FmMacsecSecY, 0, sizeof(t_FmMacsecSecY));

    /* Allocate the FM MACSEC driver's parameters structure */
    p_FmMacsecSecY->p_FmMacsecSecYDriverParam = (t_FmMacsecSecYDriverParam *)XX_Malloc(sizeof(t_FmMacsecSecYDriverParam));
    if (!p_FmMacsecSecY->p_FmMacsecSecYDriverParam)
    {
        XX_Free(p_FmMacsecSecY);
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("FM MACSEC SECY driver parameters"));
        return NULL;
    }
    memset(p_FmMacsecSecY->p_FmMacsecSecYDriverParam, 0, sizeof(t_FmMacsecSecYDriverParam));

    /* Initialize FM MACSEC SECY parameters which will be kept by the driver */
    p_FmMacsecSecY->h_FmMacsec              = p_FmMacsecSecYParam->h_FmMacsec;
    p_FmMacsecSecY->f_Event                 = p_FmMacsecSecYParam->f_Event;
    p_FmMacsecSecY->f_Exception             = p_FmMacsecSecYParam->f_Exception;
    p_FmMacsecSecY->h_App                   = p_FmMacsecSecYParam->h_App;
    p_FmMacsecSecY->confidentialityEnable   = DEFAULT_confidentialityEnable;
    p_FmMacsecSecY->confidentialityOffset   = DEFAULT_confidentialityOffset;
    p_FmMacsecSecY->validateFrames          = DEFAULT_validateFrames;
    p_FmMacsecSecY->replayProtect           = DEFAULT_replayEnable;
    p_FmMacsecSecY->replayWindow            = DEFAULT_replayWindow;
    p_FmMacsecSecY->protectFrames           = DEFAULT_protectFrames;
    p_FmMacsecSecY->sciInsertionMode        = DEFAULT_sciInsertionMode;
    p_FmMacsecSecY->isPointToPoint          = DEFAULT_ptp;
    p_FmMacsecSecY->numOfRxSc               = p_FmMacsecSecYParam->numReceiveChannels;
    p_FmMacsecSecY->numOfTxSc               = DEFAULT_numOfTxSc;
    p_FmMacsecSecY->exceptions              = DEFAULT_exceptions;
    p_FmMacsecSecY->events                  = DEFAULT_events;

    memcpy(&p_FmMacsecSecY->p_FmMacsecSecYDriverParam->txScParams,
           &p_FmMacsecSecYParam->txScParams,
           sizeof(t_FmMacsecSecYSCParams));
    return p_FmMacsecSecY;
}

t_Error FM_MACSEC_SECY_Init(t_Handle h_FmMacsecSecY)
{
    t_FmMacsecSecY              *p_FmMacsecSecY = (t_FmMacsecSecY *)h_FmMacsecSecY;
    t_FmMacsecSecYDriverParam   *p_FmMacsecSecYDriverParam = NULL;
    uint32_t                    rxScIds[NUM_OF_RX_SC], txScIds[NUM_OF_TX_SC], i, j;
    t_Error                     err;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY->p_FmMacsecSecYDriverParam, E_INVALID_HANDLE);

    CHECK_INIT_PARAMETERS(p_FmMacsecSecY, CheckFmMacsecSecYParameters);

    p_FmMacsecSecYDriverParam = p_FmMacsecSecY->p_FmMacsecSecYDriverParam;

    if ((p_FmMacsecSecY->isPointToPoint) &&
        ((err = FmMacsecSetPTP(p_FmMacsecSecY->h_FmMacsec, TRUE)) != E_OK))
        RETURN_ERROR(MAJOR, err, ("Can't set Poin-to-Point"));

    /* Rx Sc Allocation */
    p_FmMacsecSecY->p_RxSc = (t_SecYSc *)XX_Malloc(sizeof(t_SecYSc) * p_FmMacsecSecY->numOfRxSc);
    if (!p_FmMacsecSecY->p_RxSc)
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("FM MACSEC SECY RX SC"));
    memset(p_FmMacsecSecY->p_RxSc, 0, sizeof(t_SecYSc) * p_FmMacsecSecY->numOfRxSc);
    if ((err = FmMacsecAllocScs(p_FmMacsecSecY->h_FmMacsec, e_SC_RX, p_FmMacsecSecY->isPointToPoint, p_FmMacsecSecY->numOfRxSc, rxScIds)) != E_OK)
    {
        if (p_FmMacsecSecY->p_TxSc)
            XX_Free(p_FmMacsecSecY->p_TxSc);
        if (p_FmMacsecSecY->p_RxSc)
            XX_Free(p_FmMacsecSecY->p_RxSc);
        return ERROR_CODE(err);
    }
    for (i=0; i<p_FmMacsecSecY->numOfRxSc; i++)
    {
        p_FmMacsecSecY->p_RxSc[i].scId  = rxScIds[i];
        p_FmMacsecSecY->p_RxSc[i].type  = e_SC_RX;
        for (j=0; j<MAX_NUM_OF_SA_PER_SC;j++)
            p_FmMacsecSecY->p_RxSc[i].sa[j].saId = (e_ScSaId)SECY_AN_FREE_VALUE;
    }

    /* Tx Sc Allocation */
    p_FmMacsecSecY->p_TxSc = (t_SecYSc *)XX_Malloc(sizeof(t_SecYSc) * p_FmMacsecSecY->numOfTxSc);
    if (!p_FmMacsecSecY->p_TxSc)
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("FM MACSEC SECY TX SC"));
    memset(p_FmMacsecSecY->p_TxSc, 0, sizeof(t_SecYSc) * p_FmMacsecSecY->numOfTxSc);

    if ((err = FmMacsecAllocScs(p_FmMacsecSecY->h_FmMacsec, e_SC_TX, p_FmMacsecSecY->isPointToPoint, p_FmMacsecSecY->numOfTxSc, txScIds)) != E_OK)
    {
        if (p_FmMacsecSecY->p_TxSc)
            XX_Free(p_FmMacsecSecY->p_TxSc);
        if (p_FmMacsecSecY->p_RxSc)
            XX_Free(p_FmMacsecSecY->p_RxSc);
        return ERROR_CODE(err);
    }
    for (i=0; i<p_FmMacsecSecY->numOfTxSc; i++)
    {
        p_FmMacsecSecY->p_TxSc[i].scId  = txScIds[i];
        p_FmMacsecSecY->p_TxSc[i].type  = e_SC_TX;
        for (j=0; j<MAX_NUM_OF_SA_PER_SC;j++)
            p_FmMacsecSecY->p_TxSc[i].sa[j].saId = (e_ScSaId)SECY_AN_FREE_VALUE;
        FmMacsecRegisterIntr(p_FmMacsecSecY->h_FmMacsec,
                             e_FM_MACSEC_MOD_SC_TX,
                             (uint8_t)txScIds[i],
                             e_FM_INTR_TYPE_ERR,
                             FmMacsecSecYExceptionsIsr,
                             p_FmMacsecSecY);
        FmMacsecRegisterIntr(p_FmMacsecSecY->h_FmMacsec,
                             e_FM_MACSEC_MOD_SC_TX,
                             (uint8_t)txScIds[i],
                             e_FM_INTR_TYPE_NORMAL,
                             FmMacsecSecYEventsIsr,
                             p_FmMacsecSecY);

        if (p_FmMacsecSecY->exceptions & FM_MACSEC_SECY_EX_FRAME_DISCARDED)
            FmMacsecSetException(p_FmMacsecSecY->h_FmMacsec, e_FM_MACSEC_EX_TX_SC, txScIds[i], TRUE);
        if (p_FmMacsecSecY->events & FM_MACSEC_SECY_EV_NEXT_PN)
            FmMacsecSetEvent(p_FmMacsecSecY->h_FmMacsec, e_FM_MACSEC_EV_TX_SC_NEXT_PN, txScIds[i], TRUE);
    }

    FmMacsecSecYCreateSc(p_FmMacsecSecY,
                         p_FmMacsecSecYDriverParam->txScParams.sci,
                         p_FmMacsecSecYDriverParam->txScParams.cipherSuite,
                         e_SC_TX);
    XX_Free(p_FmMacsecSecYDriverParam);
    p_FmMacsecSecY->p_FmMacsecSecYDriverParam = NULL;

    return E_OK;
}

t_Error FM_MACSEC_SECY_Free(t_Handle h_FmMacsecSecY)
{
    t_FmMacsecSecY  *p_FmMacsecSecY = (t_FmMacsecSecY *)h_FmMacsecSecY;
    t_Error         err             = E_OK;
    uint32_t        rxScIds[NUM_OF_RX_SC], txScIds[NUM_OF_TX_SC], i;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmMacsecSecY->p_FmMacsecSecYDriverParam, E_INVALID_STATE);

    if (p_FmMacsecSecY->isPointToPoint)
        FmMacsecSetPTP(p_FmMacsecSecY->h_FmMacsec, FALSE);
    if (p_FmMacsecSecY->p_RxSc)
    {
        for (i=0; i<p_FmMacsecSecY->numOfRxSc; i++)
            rxScIds[i] = p_FmMacsecSecY->p_RxSc[i].scId;
        if ((err = FmMacsecFreeScs(p_FmMacsecSecY->h_FmMacsec, e_SC_RX, p_FmMacsecSecY->numOfRxSc, rxScIds)) != E_OK)
            return ERROR_CODE(err);
        XX_Free(p_FmMacsecSecY->p_RxSc);
    }
    if (p_FmMacsecSecY->p_TxSc)
    {
       FmMacsecSecYDeleteSc(p_FmMacsecSecY, &p_FmMacsecSecY->p_TxSc[0], e_SC_TX);

       for (i=0; i<p_FmMacsecSecY->numOfTxSc; i++) {
             txScIds[i] = p_FmMacsecSecY->p_TxSc[i].scId;
            FmMacsecUnregisterIntr(p_FmMacsecSecY->h_FmMacsec,
                                 e_FM_MACSEC_MOD_SC_TX,
                                 (uint8_t)txScIds[i],
                                 e_FM_INTR_TYPE_ERR);
            FmMacsecUnregisterIntr(p_FmMacsecSecY->h_FmMacsec,
                                 e_FM_MACSEC_MOD_SC_TX,
                                 (uint8_t)txScIds[i],
                                 e_FM_INTR_TYPE_NORMAL);

            if (p_FmMacsecSecY->exceptions & FM_MACSEC_SECY_EX_FRAME_DISCARDED)
                FmMacsecSetException(p_FmMacsecSecY->h_FmMacsec, e_FM_MACSEC_EX_TX_SC, txScIds[i], FALSE);
            if (p_FmMacsecSecY->events & FM_MACSEC_SECY_EV_NEXT_PN)
                FmMacsecSetEvent(p_FmMacsecSecY->h_FmMacsec, e_FM_MACSEC_EV_TX_SC_NEXT_PN, txScIds[i], FALSE);
       }

        if ((err = FmMacsecFreeScs(p_FmMacsecSecY->h_FmMacsec, e_SC_TX, p_FmMacsecSecY->numOfTxSc, txScIds)) != E_OK)
            return ERROR_CODE(err);
        XX_Free(p_FmMacsecSecY->p_TxSc);
    }

    XX_Free(p_FmMacsecSecY);

    return err;
}

t_Error FM_MACSEC_SECY_ConfigSciInsertionMode(t_Handle h_FmMacsecSecY, e_FmMacsecSciInsertionMode sciInsertionMode)
{
    t_FmMacsecSecY  *p_FmMacsecSecY = (t_FmMacsecSecY *)h_FmMacsecSecY;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY->p_FmMacsecSecYDriverParam, E_INVALID_STATE);

    p_FmMacsecSecY->sciInsertionMode = sciInsertionMode;

    return E_OK;
}

t_Error FM_MACSEC_SECY_ConfigProtectFrames(t_Handle h_FmMacsecSecY, bool protectFrames)
{
    t_FmMacsecSecY  *p_FmMacsecSecY = (t_FmMacsecSecY *)h_FmMacsecSecY;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY->p_FmMacsecSecYDriverParam, E_INVALID_STATE);

    p_FmMacsecSecY->protectFrames = protectFrames;

    return E_OK;
}

t_Error FM_MACSEC_SECY_ConfigReplayWindow(t_Handle h_FmMacsecSecY, bool replayProtect, uint32_t replayWindow)
{
    t_FmMacsecSecY  *p_FmMacsecSecY = (t_FmMacsecSecY *)h_FmMacsecSecY;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY->p_FmMacsecSecYDriverParam, E_INVALID_STATE);

    p_FmMacsecSecY->replayProtect   = replayProtect;
    p_FmMacsecSecY->replayWindow    = replayWindow;

    return E_OK;
}

t_Error FM_MACSEC_SECY_ConfigValidationMode(t_Handle h_FmMacsecSecY, e_FmMacsecValidFrameBehavior validateFrames)
{
    t_FmMacsecSecY  *p_FmMacsecSecY = (t_FmMacsecSecY *)h_FmMacsecSecY;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY->p_FmMacsecSecYDriverParam, E_INVALID_STATE);

    p_FmMacsecSecY->validateFrames = validateFrames;

    return E_OK;
}

t_Error FM_MACSEC_SECY_ConfigConfidentiality(t_Handle h_FmMacsecSecY, bool confidentialityEnable, uint16_t confidentialityOffset)
{
    t_FmMacsecSecY  *p_FmMacsecSecY = (t_FmMacsecSecY *)h_FmMacsecSecY;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY->p_FmMacsecSecYDriverParam, E_INVALID_STATE);

    p_FmMacsecSecY->confidentialityEnable = confidentialityEnable;
    p_FmMacsecSecY->confidentialityOffset = confidentialityOffset;

    return E_OK;
}

t_Error FM_MACSEC_SECY_ConfigPointToPoint(t_Handle h_FmMacsecSecY)
{
    t_FmMacsecSecY  *p_FmMacsecSecY = (t_FmMacsecSecY *)h_FmMacsecSecY;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY->p_FmMacsecSecYDriverParam, E_INVALID_STATE);

    p_FmMacsecSecY->numOfRxSc = 1;
    p_FmMacsecSecY->isPointToPoint = TRUE;
    p_FmMacsecSecY->sciInsertionMode = e_FM_MACSEC_SCI_INSERTION_MODE_IMPLICT_PTP;

    return E_OK;
}

t_Error FM_MACSEC_SECY_ConfigException(t_Handle h_FmMacsecSecY, e_FmMacsecSecYExceptions exception, bool enable)
{
    t_FmMacsecSecY  *p_FmMacsecSecY = (t_FmMacsecSecY *)h_FmMacsecSecY;
    uint32_t        bitMask         = 0;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY->p_FmMacsecSecYDriverParam, E_INVALID_STATE);

    GET_EXCEPTION_FLAG(bitMask, exception);
    if (bitMask)
    {
        if (enable)
            p_FmMacsecSecY->exceptions |= bitMask;
        else
            p_FmMacsecSecY->exceptions &= ~bitMask;
    }
    else
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Undefined exception"));

    return E_OK;
}

t_Error FM_MACSEC_SECY_ConfigEvent(t_Handle h_FmMacsecSecY, e_FmMacsecSecYEvents event, bool enable)
{
    t_FmMacsecSecY  *p_FmMacsecSecY = (t_FmMacsecSecY *)h_FmMacsecSecY;
    uint32_t        bitMask         = 0;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY->p_FmMacsecSecYDriverParam, E_INVALID_STATE);

    GET_EVENT_FLAG(bitMask, event);
    if (bitMask)
    {
        if (enable)
            p_FmMacsecSecY->events |= bitMask;
        else
            p_FmMacsecSecY->events &= ~bitMask;
    }
    else
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Undefined event"));

    return E_OK;
}

t_Handle FM_MACSEC_SECY_CreateRxSc(t_Handle h_FmMacsecSecY, t_FmMacsecSecYSCParams *p_ScParams)
{
    t_FmMacsecSecY  *p_FmMacsecSecY = (t_FmMacsecSecY *)h_FmMacsecSecY;

    SANITY_CHECK_RETURN_VALUE(p_FmMacsecSecY, E_INVALID_HANDLE, NULL);
    SANITY_CHECK_RETURN_VALUE(p_FmMacsecSecY->h_FmMacsec, E_INVALID_HANDLE, NULL);
    SANITY_CHECK_RETURN_VALUE(p_ScParams, E_NULL_POINTER, NULL);
    SANITY_CHECK_RETURN_VALUE(!p_FmMacsecSecY->p_FmMacsecSecYDriverParam, E_INVALID_STATE, NULL);

    return FmMacsecSecYCreateSc(p_FmMacsecSecY, p_ScParams->sci, p_ScParams->cipherSuite, e_SC_RX);
}

t_Error FM_MACSEC_SECY_DeleteRxSc(t_Handle h_FmMacsecSecY, t_Handle h_Sc)
{
    t_FmMacsecSecY  *p_FmMacsecSecY = (t_FmMacsecSecY *)h_FmMacsecSecY;
    t_SecYSc        *p_FmSecYSc     = (t_SecYSc *)h_Sc;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY->h_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmMacsecSecY->p_FmMacsecSecYDriverParam, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(p_FmSecYSc, E_INVALID_HANDLE);

    return FmMacsecSecYDeleteSc(p_FmMacsecSecY, p_FmSecYSc, e_SC_RX);
}

t_Error FM_MACSEC_SECY_CreateRxSa(t_Handle h_FmMacsecSecY, t_Handle h_Sc, macsecAN_t an, uint32_t lowestPn, macsecSAKey_t key)
{
    t_FmMacsecSecY  *p_FmMacsecSecY = (t_FmMacsecSecY *)h_FmMacsecSecY;
    t_SecYSc        *p_FmSecYSc     = (t_SecYSc *)h_Sc;
    t_Error         err = E_OK;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY->h_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmMacsecSecY->p_FmMacsecSecYDriverParam, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(p_FmSecYSc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(an < MAX_NUM_OF_SA_PER_SC, E_INVALID_STATE);

    if (p_FmSecYSc->sa[an].saId != SECY_AN_FREE_VALUE)
        RETURN_ERROR(MINOR, E_INVALID_STATE, ("An %d is already assigned",an));

    if ((err = FmMacsecCreateRxSa(p_FmMacsecSecY->h_FmMacsec, p_FmSecYSc->scId, (e_ScSaId)p_FmSecYSc->numOfSa, an, lowestPn, key)) != E_OK)
        RETURN_ERROR(MINOR, err, NO_MSG);

    p_FmSecYSc->sa[an].saId = (e_ScSaId)p_FmSecYSc->numOfSa++;
    return err;
}

t_Error FM_MACSEC_SECY_DeleteRxSa(t_Handle h_FmMacsecSecY, t_Handle h_Sc, macsecAN_t an)
{
    t_FmMacsecSecY  *p_FmMacsecSecY = (t_FmMacsecSecY *)h_FmMacsecSecY;
    t_SecYSc        *p_FmSecYSc     = (t_SecYSc *)h_Sc;
    t_Error         err             = E_OK;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY->h_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmMacsecSecY->p_FmMacsecSecYDriverParam, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(p_FmSecYSc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(an < MAX_NUM_OF_SA_PER_SC, E_INVALID_STATE);

    if (p_FmSecYSc->sa[an].saId == SECY_AN_FREE_VALUE)
        RETURN_ERROR(MINOR, E_INVALID_STATE, ("An %d is already deleted",an));

    if ((err = FmMacsecDeleteRxSa(p_FmMacsecSecY->h_FmMacsec, p_FmSecYSc->scId, p_FmSecYSc->sa[an].saId)) != E_OK)
        RETURN_ERROR(MINOR, err, NO_MSG);

    p_FmSecYSc->numOfSa--;
    p_FmSecYSc->sa[an].saId = (e_ScSaId)SECY_AN_FREE_VALUE;
    /* TODO - check if statistics need to be read*/
    return err;
}

t_Error FM_MACSEC_SECY_RxSaEnableReceive(t_Handle h_FmMacsecSecY, t_Handle h_Sc, macsecAN_t an)
{
    t_FmMacsecSecY  *p_FmMacsecSecY = (t_FmMacsecSecY *)h_FmMacsecSecY;
    t_SecYSc        *p_FmSecYSc     = (t_SecYSc *)h_Sc;
    t_Error         err = E_OK;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY->h_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmMacsecSecY->p_FmMacsecSecYDriverParam, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(p_FmSecYSc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(an < MAX_NUM_OF_SA_PER_SC, E_INVALID_STATE);

    if (p_FmSecYSc->sa[an].saId == SECY_AN_FREE_VALUE)
        RETURN_ERROR(MINOR, E_INVALID_STATE, ("An %d is not configured",an));

    if ((err = FmMacsecRxSaSetReceive(p_FmMacsecSecY->h_FmMacsec,p_FmSecYSc->scId, p_FmSecYSc->sa[an].saId, TRUE)) != E_OK)
        RETURN_ERROR(MINOR, err, NO_MSG);

    p_FmSecYSc->sa[an].active = TRUE;
    return err;
}

t_Error FM_MACSEC_SECY_RxSaDisableReceive(t_Handle h_FmMacsecSecY, t_Handle h_Sc, macsecAN_t an)
{
    t_FmMacsecSecY  *p_FmMacsecSecY = (t_FmMacsecSecY *)h_FmMacsecSecY;
    t_SecYSc        *p_FmSecYSc     = (t_SecYSc *)h_Sc;
    t_Error         err = E_OK;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY->h_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmMacsecSecY->p_FmMacsecSecYDriverParam, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(p_FmSecYSc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(an < MAX_NUM_OF_SA_PER_SC, E_INVALID_STATE);

    if (p_FmSecYSc->sa[an].saId == SECY_AN_FREE_VALUE)
        RETURN_ERROR(MINOR, E_INVALID_STATE, ("An %d is not configured",an));

    if ((err = FmMacsecRxSaSetReceive(p_FmMacsecSecY->h_FmMacsec,p_FmSecYSc->scId, p_FmSecYSc->sa[an].saId, FALSE)) != E_OK)
        RETURN_ERROR(MINOR, err, NO_MSG);

    p_FmSecYSc->sa[an].active = FALSE;
    return err;
}

t_Error FM_MACSEC_SECY_RxSaUpdateNextPn(t_Handle h_FmMacsecSecY, t_Handle h_Sc, macsecAN_t an, uint32_t updtNextPN)
{
    t_FmMacsecSecY  *p_FmMacsecSecY = (t_FmMacsecSecY *)h_FmMacsecSecY;
    t_SecYSc        *p_FmSecYSc     = (t_SecYSc *)h_Sc;
    t_Error         err = E_OK;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY->h_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmMacsecSecY->p_FmMacsecSecYDriverParam, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(p_FmSecYSc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(an < MAX_NUM_OF_SA_PER_SC, E_INVALID_STATE);

    if (p_FmSecYSc->sa[an].saId == SECY_AN_FREE_VALUE)
        RETURN_ERROR(MINOR, E_INVALID_STATE, ("An %d is not configured",an));

    if ((err = FmMacsecRxSaUpdateNextPn(p_FmMacsecSecY->h_FmMacsec,p_FmSecYSc->scId, p_FmSecYSc->sa[an].saId, updtNextPN)) != E_OK)
        RETURN_ERROR(MINOR, err, NO_MSG);

    return err;
}

t_Error FM_MACSEC_SECY_RxSaUpdateLowestPn(t_Handle h_FmMacsecSecY, t_Handle h_Sc, macsecAN_t an, uint32_t updtLowestPN)
{
    t_FmMacsecSecY  *p_FmMacsecSecY = (t_FmMacsecSecY *)h_FmMacsecSecY;
    t_SecYSc        *p_FmSecYSc     = (t_SecYSc *)h_Sc;
    t_Error         err = E_OK;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY->h_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmMacsecSecY->p_FmMacsecSecYDriverParam, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(p_FmSecYSc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(an < MAX_NUM_OF_SA_PER_SC, E_INVALID_STATE);

    if (p_FmSecYSc->sa[an].saId == SECY_AN_FREE_VALUE)
        RETURN_ERROR(MINOR, E_INVALID_STATE, ("An %d is not configured",an));

    if ((err = FmMacsecRxSaUpdateLowestPn(p_FmMacsecSecY->h_FmMacsec,p_FmSecYSc->scId, p_FmSecYSc->sa[an].saId, updtLowestPN)) != E_OK)
        RETURN_ERROR(MINOR, err, NO_MSG);

    return err;
}

t_Error FM_MACSEC_SECY_RxSaModifyKey(t_Handle h_FmMacsecSecY, t_Handle h_Sc, macsecAN_t an, macsecSAKey_t key)
{
    t_FmMacsecSecY  *p_FmMacsecSecY = (t_FmMacsecSecY *)h_FmMacsecSecY;
    t_SecYSc        *p_FmSecYSc     = (t_SecYSc *)h_Sc;
    t_Error         err = E_OK;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY->h_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmMacsecSecY->p_FmMacsecSecYDriverParam, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(p_FmSecYSc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(an < MAX_NUM_OF_SA_PER_SC, E_INVALID_STATE);

    if (p_FmSecYSc->sa[an].saId == SECY_AN_FREE_VALUE)
        RETURN_ERROR(MINOR, E_INVALID_STATE, ("An %d is not configured",an));

    if (p_FmSecYSc->sa[an].active)
        if ((err = FmMacsecRxSaSetReceive(p_FmMacsecSecY->h_FmMacsec, p_FmSecYSc->scId, p_FmSecYSc->sa[an].saId, FALSE)) != E_OK)
            RETURN_ERROR(MINOR, err, NO_MSG);

    /* TODO - statistics should be read */

    if ((err = FmMacsecCreateRxSa(p_FmMacsecSecY->h_FmMacsec, p_FmSecYSc->scId, p_FmSecYSc->sa[an].saId, an, 1, key)) != E_OK)
        RETURN_ERROR(MINOR, err, NO_MSG);

    if (p_FmSecYSc->sa[an].active)
        if ((err = FmMacsecRxSaSetReceive(p_FmMacsecSecY->h_FmMacsec, p_FmSecYSc->scId, p_FmSecYSc->sa[an].saId, TRUE)) != E_OK)
            RETURN_ERROR(MINOR, err, NO_MSG);
    return err;
}


t_Error FM_MACSEC_SECY_CreateTxSa(t_Handle h_FmMacsecSecY, macsecAN_t an, macsecSAKey_t key)
{
    t_FmMacsecSecY  *p_FmMacsecSecY = (t_FmMacsecSecY *)h_FmMacsecSecY;
    t_SecYSc        *p_FmSecYSc;
    t_Error         err = E_OK;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY->h_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmMacsecSecY->p_FmMacsecSecYDriverParam, E_INVALID_STATE);
    p_FmSecYSc = &p_FmMacsecSecY->p_TxSc[0];
    SANITY_CHECK_RETURN_ERROR(p_FmSecYSc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(an < MAX_NUM_OF_SA_PER_SC, E_INVALID_STATE);

    if (p_FmSecYSc->sa[an].saId != SECY_AN_FREE_VALUE)
        RETURN_ERROR(MINOR, err, ("An %d is already assigned",an));

    if ((err = FmMacsecCreateTxSa(p_FmMacsecSecY->h_FmMacsec,p_FmSecYSc->scId, (e_ScSaId)p_FmSecYSc->numOfSa, key)) != E_OK)
        RETURN_ERROR(MINOR, err, NO_MSG);

    p_FmSecYSc->sa[an].saId = (e_ScSaId)p_FmSecYSc->numOfSa++;
    return err;
}

t_Error FM_MACSEC_SECY_DeleteTxSa(t_Handle h_FmMacsecSecY, macsecAN_t an)
{
    t_FmMacsecSecY  *p_FmMacsecSecY = (t_FmMacsecSecY *)h_FmMacsecSecY;
    t_SecYSc        *p_FmSecYSc;
    t_Error         err = E_OK;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY->h_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmMacsecSecY->p_FmMacsecSecYDriverParam, E_INVALID_STATE);
    p_FmSecYSc = &p_FmMacsecSecY->p_TxSc[0];
    SANITY_CHECK_RETURN_ERROR(p_FmSecYSc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(an < MAX_NUM_OF_SA_PER_SC, E_INVALID_STATE);

    if (p_FmSecYSc->sa[an].saId == SECY_AN_FREE_VALUE)
        RETURN_ERROR(MINOR, E_INVALID_STATE, ("An %d is already deleted",an));

    if ((err = FmMacsecDeleteTxSa(p_FmMacsecSecY->h_FmMacsec, p_FmSecYSc->scId, p_FmSecYSc->sa[an].saId)) != E_OK)
        RETURN_ERROR(MINOR, err, NO_MSG);

    p_FmSecYSc->numOfSa--;
    p_FmSecYSc->sa[an].saId = (e_ScSaId)SECY_AN_FREE_VALUE;
    /* TODO - check if statistics need to be read*/
    return err;
}

t_Error FM_MACSEC_SECY_TxSaModifyKey(t_Handle h_FmMacsecSecY, macsecAN_t nextActiveAn, macsecSAKey_t key)
{
    t_FmMacsecSecY  *p_FmMacsecSecY = (t_FmMacsecSecY *)h_FmMacsecSecY;
    t_SecYSc        *p_FmSecYSc;
    macsecAN_t      currentAn;
    t_Error         err = E_OK;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY->h_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmMacsecSecY->p_FmMacsecSecYDriverParam, E_INVALID_STATE);
    p_FmSecYSc = &p_FmMacsecSecY->p_TxSc[0];
    SANITY_CHECK_RETURN_ERROR(p_FmSecYSc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(nextActiveAn < MAX_NUM_OF_SA_PER_SC, E_INVALID_STATE);

    if ((err = FmMacsecTxSaGetActive(p_FmMacsecSecY->h_FmMacsec,
                                     p_FmSecYSc->scId,
                                     &currentAn)) != E_OK)
        RETURN_ERROR(MINOR, err, NO_MSG);

    if ((err = FmMacsecTxSaSetActive(p_FmMacsecSecY->h_FmMacsec,
                                     p_FmSecYSc->scId,
                                     p_FmSecYSc->sa[nextActiveAn].saId,
                                     nextActiveAn)) != E_OK)
        RETURN_ERROR(MINOR, err, NO_MSG);

    /* TODO - statistics should be read */

    if ((err = FmMacsecCreateTxSa(p_FmMacsecSecY->h_FmMacsec, p_FmSecYSc->scId, p_FmSecYSc->sa[currentAn].saId, key)) != E_OK)
        RETURN_ERROR(MINOR, err, NO_MSG);

    return err;
}

t_Error FM_MACSEC_SECY_TxSaSetActive(t_Handle h_FmMacsecSecY, macsecAN_t an)
{
    t_FmMacsecSecY  *p_FmMacsecSecY = (t_FmMacsecSecY *)h_FmMacsecSecY;
    t_SecYSc        *p_FmSecYSc;
    t_Error         err = E_OK;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY->h_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmMacsecSecY->p_FmMacsecSecYDriverParam, E_INVALID_STATE);
    p_FmSecYSc = &p_FmMacsecSecY->p_TxSc[0];
    SANITY_CHECK_RETURN_ERROR(p_FmSecYSc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(an < MAX_NUM_OF_SA_PER_SC, E_INVALID_STATE);

    if (p_FmSecYSc->sa[an].saId == SECY_AN_FREE_VALUE)
        RETURN_ERROR(MINOR, E_INVALID_STATE, ("An %d is not configured",an));

    if ((err = FmMacsecTxSaSetActive(p_FmMacsecSecY->h_FmMacsec,
                                     p_FmSecYSc->scId,
                                     p_FmSecYSc->sa[an].saId,
                                     an)) != E_OK)
        RETURN_ERROR(MINOR, err, NO_MSG);

    return err;
}

t_Error FM_MACSEC_SECY_TxSaGetActive(t_Handle h_FmMacsecSecY, macsecAN_t *p_An)
{
    t_FmMacsecSecY  *p_FmMacsecSecY = (t_FmMacsecSecY *)h_FmMacsecSecY;
    t_SecYSc        *p_FmSecYSc;
    t_Error         err = E_OK;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY->h_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmMacsecSecY->p_FmMacsecSecYDriverParam, E_INVALID_STATE);
    p_FmSecYSc = &p_FmMacsecSecY->p_TxSc[0];
    SANITY_CHECK_RETURN_ERROR(p_FmSecYSc, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_An, E_INVALID_HANDLE);

    if ((err = FmMacsecTxSaGetActive(p_FmMacsecSecY->h_FmMacsec,
                                     p_FmSecYSc->scId,
                                     p_An)) != E_OK)
        RETURN_ERROR(MINOR, err, NO_MSG);

    return err;
}

t_Error FM_MACSEC_SECY_GetRxScPhysId(t_Handle h_FmMacsecSecY, t_Handle h_Sc, uint32_t *p_ScPhysId)
{
    t_SecYSc        *p_FmSecYSc = (t_SecYSc *)h_Sc;
    t_Error         err = E_OK;

    SANITY_CHECK_RETURN_ERROR(h_FmMacsecSecY, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(((t_FmMacsecSecY *)h_FmMacsecSecY)->h_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!((t_FmMacsecSecY *)h_FmMacsecSecY)->p_FmMacsecSecYDriverParam, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(p_FmSecYSc, E_INVALID_HANDLE);
#ifdef DISABLE_SANITY_CHECKS
    UNUSED(h_FmMacsecSecY);
#endif /* DISABLE_SANITY_CHECKS */

    *p_ScPhysId = p_FmSecYSc->scId;
    return err;
}

t_Error FM_MACSEC_SECY_GetTxScPhysId(t_Handle h_FmMacsecSecY, uint32_t *p_ScPhysId)
{
    t_FmMacsecSecY  *p_FmMacsecSecY = (t_FmMacsecSecY *)h_FmMacsecSecY;
    t_SecYSc        *p_FmSecYSc;
    t_Error         err = E_OK;

    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_FmMacsecSecY->h_FmMacsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_FmMacsecSecY->p_FmMacsecSecYDriverParam, E_INVALID_STATE);
    p_FmSecYSc = &p_FmMacsecSecY->p_TxSc[0];
    SANITY_CHECK_RETURN_ERROR(p_FmSecYSc, E_INVALID_HANDLE);

    *p_ScPhysId = p_FmSecYSc->scId;
    return err;
}

t_Error FM_MACSEC_SECY_SetException(t_Handle h_FmMacsecSecY, e_FmMacsecExceptions exception, bool enable)
{
   UNUSED(h_FmMacsecSecY);UNUSED(exception);UNUSED(enable);
   RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

t_Error FM_MACSEC_SECY_SetEvent(t_Handle h_FmMacsecSecY, e_FmMacsecSecYEvents event, bool enable)
{
    UNUSED(h_FmMacsecSecY);UNUSED(event);UNUSED(enable);
    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

t_Error FM_MACSEC_SECY_GetStatistics(t_Handle h_FmMacsecSecY, t_FmMacsecSecYStatistics *p_Statistics)
{
    UNUSED(h_FmMacsecSecY);UNUSED(p_Statistics);
    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

t_Error FM_MACSEC_SECY_RxScGetStatistics(t_Handle h_FmMacsecSecY, t_Handle h_Sc, t_FmMacsecSecYRxScStatistics *p_Statistics)
{
    UNUSED(h_FmMacsecSecY);UNUSED(h_Sc);UNUSED(p_Statistics);
    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

t_Error FM_MACSEC_SECY_RxSaGetStatistics(t_Handle h_FmMacsecSecY, t_Handle h_Sc, macsecAN_t an, t_FmMacsecSecYRxSaStatistics *p_Statistics)
{
    UNUSED(h_FmMacsecSecY);UNUSED(h_Sc);UNUSED(an);UNUSED(p_Statistics);
    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

t_Error FM_MACSEC_SECY_TxScGetStatistics(t_Handle h_FmMacsecSecY, t_FmMacsecSecYTxScStatistics *p_Statistics)
{
    UNUSED(h_FmMacsecSecY);UNUSED(p_Statistics);
    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

t_Error FM_MACSEC_SECY_TxSaGetStatistics(t_Handle h_FmMacsecSecY, macsecAN_t an, t_FmMacsecSecYTxSaStatistics *p_Statistics)
{
    UNUSED(h_FmMacsecSecY);UNUSED(an);UNUSED(p_Statistics);
    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

