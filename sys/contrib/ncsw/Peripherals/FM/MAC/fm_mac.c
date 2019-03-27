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
 @File          fm_mac.c

 @Description   FM MAC ...
*//***************************************************************************/
#include "std_ext.h"
#include "string_ext.h"
#include "sprint_ext.h"
#include "error_ext.h"
#include "fm_ext.h"

#include "fm_common.h"
#include "fm_mac.h"


/* ......................................................................... */

t_Handle FM_MAC_Config (t_FmMacParams *p_FmMacParam)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver;
    uint16_t                fmClkFreq;

    SANITY_CHECK_RETURN_VALUE(p_FmMacParam, E_INVALID_HANDLE, NULL);

    fmClkFreq = FmGetClockFreq(p_FmMacParam->h_Fm);
    if (fmClkFreq == 0)
    {
        REPORT_ERROR(MAJOR, E_INVALID_STATE, ("Can't get clock for MAC!"));
        return NULL;
    }

#if (DPAA_VERSION == 10)
    if (ENET_SPEED_FROM_MODE(p_FmMacParam->enetMode) < e_ENET_SPEED_10000)
        p_FmMacControllerDriver = (t_FmMacControllerDriver *)DTSEC_Config(p_FmMacParam);
    else
#if FM_MAX_NUM_OF_10G_MACS > 0
        p_FmMacControllerDriver = (t_FmMacControllerDriver *)TGEC_Config(p_FmMacParam);
#else
        p_FmMacControllerDriver = NULL;
#endif /* FM_MAX_NUM_OF_10G_MACS > 0 */
#else
    p_FmMacControllerDriver = (t_FmMacControllerDriver *)MEMAC_Config(p_FmMacParam);
#endif /* (DPAA_VERSION == 10) */

    if (!p_FmMacControllerDriver)
        return NULL;

    p_FmMacControllerDriver->h_Fm           = p_FmMacParam->h_Fm;
    p_FmMacControllerDriver->enetMode       = p_FmMacParam->enetMode;
    p_FmMacControllerDriver->macId          = p_FmMacParam->macId;
    p_FmMacControllerDriver->resetOnInit    = DEFAULT_resetOnInit;

    p_FmMacControllerDriver->clkFreq        = fmClkFreq;

    return (t_Handle)p_FmMacControllerDriver;
}

/* ......................................................................... */

t_Error FM_MAC_Init (t_Handle h_FmMac)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->resetOnInit &&
        !p_FmMacControllerDriver->f_FM_MAC_ConfigResetOnInit &&
        (FmResetMac(p_FmMacControllerDriver->h_Fm,
                    ((ENET_INTERFACE_FROM_MODE(p_FmMacControllerDriver->enetMode) == e_ENET_IF_XGMII) ?
                        e_FM_MAC_10G : e_FM_MAC_1G),
                    p_FmMacControllerDriver->macId) != E_OK))
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Can't reset MAC!"));

    if (p_FmMacControllerDriver->f_FM_MAC_Init)
        return p_FmMacControllerDriver->f_FM_MAC_Init(h_FmMac);
    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

/* ......................................................................... */

t_Error FM_MAC_Free (t_Handle h_FmMac)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_Free)
        return p_FmMacControllerDriver->f_FM_MAC_Free(h_FmMac);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

/* ......................................................................... */

t_Error FM_MAC_ConfigResetOnInit (t_Handle h_FmMac, bool enable)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_ConfigResetOnInit)
        return p_FmMacControllerDriver->f_FM_MAC_ConfigResetOnInit(h_FmMac, enable);

    p_FmMacControllerDriver->resetOnInit = enable;

    return E_OK;
}

/* ......................................................................... */

t_Error FM_MAC_ConfigLoopback (t_Handle h_FmMac, bool newVal)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_ConfigLoopback)
        return p_FmMacControllerDriver->f_FM_MAC_ConfigLoopback(h_FmMac, newVal);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

/* ......................................................................... */

t_Error FM_MAC_ConfigMaxFrameLength (t_Handle h_FmMac, uint16_t newVal)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_ConfigMaxFrameLength)
        return p_FmMacControllerDriver->f_FM_MAC_ConfigMaxFrameLength(h_FmMac, newVal);
    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

/* ......................................................................... */

t_Error FM_MAC_ConfigWan (t_Handle h_FmMac, bool flag)
{
   t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_ConfigWan)
        return p_FmMacControllerDriver->f_FM_MAC_ConfigWan(h_FmMac, flag);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

/* ......................................................................... */

t_Error FM_MAC_ConfigPadAndCrc (t_Handle h_FmMac, bool newVal)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_ConfigPadAndCrc)
        return p_FmMacControllerDriver->f_FM_MAC_ConfigPadAndCrc(h_FmMac, newVal);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

/* ......................................................................... */

t_Error FM_MAC_ConfigHalfDuplex (t_Handle h_FmMac, bool newVal)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_ConfigHalfDuplex)
        return p_FmMacControllerDriver->f_FM_MAC_ConfigHalfDuplex(h_FmMac,newVal);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

/* ......................................................................... */

t_Error FM_MAC_ConfigTbiPhyAddr (t_Handle h_FmMac, uint8_t newVal)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_ConfigTbiPhyAddr)
        return p_FmMacControllerDriver->f_FM_MAC_ConfigTbiPhyAddr(h_FmMac,newVal);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

/* ......................................................................... */

t_Error FM_MAC_ConfigLengthCheck (t_Handle h_FmMac, bool newVal)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_ConfigLengthCheck)
        return p_FmMacControllerDriver->f_FM_MAC_ConfigLengthCheck(h_FmMac,newVal);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

/* ......................................................................... */

t_Error FM_MAC_ConfigException (t_Handle h_FmMac, e_FmMacExceptions ex, bool enable)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_ConfigException)
        return p_FmMacControllerDriver->f_FM_MAC_ConfigException(h_FmMac, ex, enable);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

#ifdef FM_TX_ECC_FRMS_ERRATA_10GMAC_A004
/* ......................................................................... */

t_Error FM_MAC_ConfigSkipFman11Workaround (t_Handle h_FmMac)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_ConfigSkipFman11Workaround)
        return p_FmMacControllerDriver->f_FM_MAC_ConfigSkipFman11Workaround(h_FmMac);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}
#endif /* FM_TX_ECC_FRMS_ERRATA_10GMAC_A004 */


/*****************************************************************************/
/* Run Time Control                                                          */
/*****************************************************************************/

/* ......................................................................... */

t_Error FM_MAC_Enable  (t_Handle h_FmMac,  e_CommMode mode)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_Enable)
        return p_FmMacControllerDriver->f_FM_MAC_Enable(h_FmMac, mode);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

/* ......................................................................... */

t_Error FM_MAC_Disable (t_Handle h_FmMac, e_CommMode mode)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_Disable)
        return p_FmMacControllerDriver->f_FM_MAC_Disable(h_FmMac, mode);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

t_Error FM_MAC_Resume (t_Handle h_FmMac)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_Resume)
        return p_FmMacControllerDriver->f_FM_MAC_Resume(h_FmMac);

    return E_OK;
}

/* ......................................................................... */

t_Error FM_MAC_Enable1588TimeStamp (t_Handle h_FmMac)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_Enable1588TimeStamp)
        return p_FmMacControllerDriver->f_FM_MAC_Enable1588TimeStamp(h_FmMac);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

/* ......................................................................... */

t_Error FM_MAC_Disable1588TimeStamp (t_Handle h_FmMac)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_Disable1588TimeStamp)
        return p_FmMacControllerDriver->f_FM_MAC_Disable1588TimeStamp(h_FmMac);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

/* ......................................................................... */

t_Error FM_MAC_SetTxAutoPauseFrames(t_Handle h_FmMac,
                                    uint16_t pauseTime)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_SetTxAutoPauseFrames)
        return p_FmMacControllerDriver->f_FM_MAC_SetTxAutoPauseFrames(h_FmMac,
                                                                      pauseTime);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

/* ......................................................................... */

t_Error FM_MAC_SetTxPauseFrames(t_Handle h_FmMac,
                                uint8_t  priority,
                                uint16_t pauseTime,
                                uint16_t threshTime)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_SetTxPauseFrames)
        return p_FmMacControllerDriver->f_FM_MAC_SetTxPauseFrames(h_FmMac,
                                                                  priority,
                                                                  pauseTime,
                                                                  threshTime);

    RETURN_ERROR(MAJOR, E_NOT_SUPPORTED, NO_MSG);
}

/* ......................................................................... */

t_Error FM_MAC_SetRxIgnorePauseFrames (t_Handle h_FmMac, bool en)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_SetRxIgnorePauseFrames)
        return p_FmMacControllerDriver->f_FM_MAC_SetRxIgnorePauseFrames(h_FmMac, en);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

/* ......................................................................... */

t_Error FM_MAC_SetWakeOnLan (t_Handle h_FmMac, bool en)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_SetWakeOnLan)
        return p_FmMacControllerDriver->f_FM_MAC_SetWakeOnLan(h_FmMac, en);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

/* ......................................................................... */

t_Error FM_MAC_ResetCounters (t_Handle h_FmMac)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_ResetCounters)
        return p_FmMacControllerDriver->f_FM_MAC_ResetCounters(h_FmMac);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

/* ......................................................................... */

t_Error FM_MAC_SetException(t_Handle h_FmMac, e_FmMacExceptions ex, bool enable)
{
   t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_SetException)
        return p_FmMacControllerDriver->f_FM_MAC_SetException(h_FmMac, ex, enable);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

/* ......................................................................... */

t_Error FM_MAC_SetStatistics (t_Handle h_FmMac, e_FmMacStatisticsLevel statisticsLevel)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_SetStatistics)
        return p_FmMacControllerDriver->f_FM_MAC_SetStatistics(h_FmMac, statisticsLevel);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

/* ......................................................................... */

t_Error FM_MAC_GetStatistics (t_Handle h_FmMac, t_FmMacStatistics *p_Statistics)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_GetStatistics)
        return p_FmMacControllerDriver->f_FM_MAC_GetStatistics(h_FmMac, p_Statistics);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

/* ......................................................................... */

t_Error FM_MAC_ModifyMacAddr (t_Handle h_FmMac, t_EnetAddr *p_EnetAddr)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_ModifyMacAddr)
        return p_FmMacControllerDriver->f_FM_MAC_ModifyMacAddr(h_FmMac, p_EnetAddr);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

/* ......................................................................... */

t_Error FM_MAC_AddHashMacAddr (t_Handle h_FmMac, t_EnetAddr *p_EnetAddr)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_AddHashMacAddr)
        return p_FmMacControllerDriver->f_FM_MAC_AddHashMacAddr(h_FmMac, p_EnetAddr);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

/* ......................................................................... */

t_Error FM_MAC_RemoveHashMacAddr (t_Handle h_FmMac, t_EnetAddr *p_EnetAddr)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_RemoveHashMacAddr)
        return p_FmMacControllerDriver->f_FM_MAC_RemoveHashMacAddr(h_FmMac, p_EnetAddr);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

/* ......................................................................... */

t_Error FM_MAC_AddExactMatchMacAddr (t_Handle h_FmMac, t_EnetAddr *p_EnetAddr)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_AddExactMatchMacAddr)
        return p_FmMacControllerDriver->f_FM_MAC_AddExactMatchMacAddr(h_FmMac, p_EnetAddr);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

/* ......................................................................... */

t_Error FM_MAC_RemovelExactMatchMacAddr (t_Handle h_FmMac, t_EnetAddr *p_EnetAddr)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_RemovelExactMatchMacAddr)
        return p_FmMacControllerDriver->f_FM_MAC_RemovelExactMatchMacAddr(h_FmMac, p_EnetAddr);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

/* ......................................................................... */

t_Error FM_MAC_GetVesrion (t_Handle h_FmMac, uint32_t *macVresion)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_GetVersion)
        return p_FmMacControllerDriver->f_FM_MAC_GetVersion(h_FmMac, macVresion);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);

}

/* ......................................................................... */

t_Error FM_MAC_GetId (t_Handle h_FmMac, uint32_t *macId)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_GetId)
        return p_FmMacControllerDriver->f_FM_MAC_GetId(h_FmMac, macId);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

/* ......................................................................... */

t_Error FM_MAC_SetPromiscuous (t_Handle h_FmMac, bool newVal)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_SetPromiscuous)
        return p_FmMacControllerDriver->f_FM_MAC_SetPromiscuous(h_FmMac, newVal);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

/* ......................................................................... */

t_Error FM_MAC_AdjustLink(t_Handle h_FmMac, e_EnetSpeed speed, bool fullDuplex)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_AdjustLink)
        return p_FmMacControllerDriver->f_FM_MAC_AdjustLink(h_FmMac, speed, fullDuplex);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

/* ......................................................................... */

t_Error FM_MAC_RestartAutoneg(t_Handle h_FmMac)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_RestartAutoneg)
        return p_FmMacControllerDriver->f_FM_MAC_RestartAutoneg(h_FmMac);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

/* ......................................................................... */

t_Error FM_MAC_MII_WritePhyReg (t_Handle h_FmMac, uint8_t phyAddr, uint8_t reg, uint16_t data)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_MII_WritePhyReg)
        return p_FmMacControllerDriver->f_FM_MAC_MII_WritePhyReg(h_FmMac, phyAddr, reg, data);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

/* ......................................................................... */

t_Error FM_MAC_MII_ReadPhyReg(t_Handle h_FmMac,  uint8_t phyAddr, uint8_t reg, uint16_t *p_Data)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_MII_ReadPhyReg)
        return p_FmMacControllerDriver->f_FM_MAC_MII_ReadPhyReg(h_FmMac, phyAddr, reg, p_Data);

    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}

/* ......................................................................... */

uint16_t FM_MAC_GetMaxFrameLength(t_Handle h_FmMac)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_VALUE(p_FmMacControllerDriver, E_INVALID_HANDLE, 0);

    if (p_FmMacControllerDriver->f_FM_MAC_GetMaxFrameLength)
        return p_FmMacControllerDriver->f_FM_MAC_GetMaxFrameLength(h_FmMac);

    REPORT_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
    return 0;
}

#if (defined(DEBUG_ERRORS) && (DEBUG_ERRORS > 0))
/*****************************************************************************/
t_Error FM_MAC_DumpRegs(t_Handle h_FmMac)
{
    t_FmMacControllerDriver *p_FmMacControllerDriver = (t_FmMacControllerDriver *)h_FmMac;

    SANITY_CHECK_RETURN_ERROR(p_FmMacControllerDriver, E_INVALID_HANDLE);

    if (p_FmMacControllerDriver->f_FM_MAC_DumpRegs)
         return p_FmMacControllerDriver->f_FM_MAC_DumpRegs(h_FmMac);
    RETURN_ERROR(MINOR, E_NOT_SUPPORTED, NO_MSG);
}
#endif /* (defined(DEBUG_ERRORS) && ... */
