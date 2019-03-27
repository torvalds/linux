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
 @File          memac.c

 @Description   FM mEMAC driver
*//***************************************************************************/

#include "std_ext.h"
#include "string_ext.h"
#include "error_ext.h"
#include "xx_ext.h"
#include "endian_ext.h"
#include "debug_ext.h"

#include "fm_common.h"
#include "memac.h"


/*****************************************************************************/
/*                      Internal routines                                    */
/*****************************************************************************/

/* ......................................................................... */

static uint32_t GetMacAddrHashCode(uint64_t ethAddr)
{
    uint64_t    mask1, mask2;
    uint32_t    xorVal = 0;
    uint8_t     i, j;

    for (i=0; i<6; i++)
    {
        mask1 = ethAddr & (uint64_t)0x01;
        ethAddr >>= 1;

        for (j=0; j<7; j++)
        {
            mask2 = ethAddr & (uint64_t)0x01;
            mask1 ^= mask2;
            ethAddr >>= 1;
        }

        xorVal |= (mask1 << (5-i));
    }

    return xorVal;
}

/* ......................................................................... */

static void SetupSgmiiInternalPhy(t_Memac *p_Memac, uint8_t phyAddr)
{
    uint16_t    tmpReg16;
    e_EnetMode  enetMode;

     /* In case the higher MACs are used (i.e. the MACs that should support 10G),
        speed=10000 is provided for SGMII ports. Temporary modify enet mode
        to 1G one, so MII functions can work correctly. */
    enetMode = p_Memac->enetMode;

    /* SGMII mode + AN enable */
    tmpReg16 = PHY_SGMII_IF_MODE_AN | PHY_SGMII_IF_MODE_SGMII;
    if ((p_Memac->enetMode) == e_ENET_MODE_SGMII_2500)
        tmpReg16 = PHY_SGMII_CR_PHY_RESET | PHY_SGMII_IF_SPEED_GIGABIT | PHY_SGMII_IF_MODE_SGMII;

    p_Memac->enetMode = MAKE_ENET_MODE(ENET_INTERFACE_FROM_MODE(p_Memac->enetMode), e_ENET_SPEED_1000);
    MEMAC_MII_WritePhyReg(p_Memac, phyAddr, 0x14, tmpReg16);

    /* Device ability according to SGMII specification */
    tmpReg16 = PHY_SGMII_DEV_ABILITY_SGMII;
    MEMAC_MII_WritePhyReg(p_Memac, phyAddr, 0x4, tmpReg16);

    /* Adjust link timer for SGMII  -
       According to Cisco SGMII specification the timer should be 1.6 ms.
       The link_timer register is configured in units of the clock.
       - When running as 1G SGMII, Serdes clock is 125 MHz, so
         unit = 1 / (125*10^6 Hz) = 8 ns.
         1.6 ms in units of 8 ns = 1.6ms / 8ns = 2 * 10^5 = 0x30d40
       - When running as 2.5G SGMII, Serdes clock is 312.5 MHz, so
         unit = 1 / (312.5*10^6 Hz) = 3.2 ns.
         1.6 ms in units of 3.2 ns = 1.6ms / 3.2ns = 5 * 10^5 = 0x7a120.
       Since link_timer value of 1G SGMII will be too short for 2.5 SGMII,
       we always set up here a value of 2.5 SGMII. */
    MEMAC_MII_WritePhyReg(p_Memac, phyAddr, 0x13, 0x0007);
    MEMAC_MII_WritePhyReg(p_Memac, phyAddr, 0x12, 0xa120);

    /* Restart AN */
    tmpReg16 = PHY_SGMII_CR_DEF_VAL | PHY_SGMII_CR_RESET_AN;
    MEMAC_MII_WritePhyReg(p_Memac, phyAddr, 0x0, tmpReg16);

    /* Restore original enet mode */
    p_Memac->enetMode = enetMode;
}

/* ......................................................................... */

static void SetupSgmiiInternalPhyBaseX(t_Memac *p_Memac, uint8_t phyAddr)
{
    uint16_t    tmpReg16;
    e_EnetMode  enetMode;

     /* In case the higher MACs are used (i.e. the MACs that should support 10G),
        speed=10000 is provided for SGMII ports. Temporary modify enet mode
        to 1G one, so MII functions can work correctly. */
    enetMode = p_Memac->enetMode;
    p_Memac->enetMode = MAKE_ENET_MODE(ENET_INTERFACE_FROM_MODE(p_Memac->enetMode), e_ENET_SPEED_1000);

    /* 1000BaseX mode */
    tmpReg16 = PHY_SGMII_IF_MODE_1000X;
    MEMAC_MII_WritePhyReg(p_Memac, phyAddr, 0x14, tmpReg16);

    /* AN Device capability  */
    tmpReg16 = PHY_SGMII_DEV_ABILITY_1000X;
    MEMAC_MII_WritePhyReg(p_Memac, phyAddr, 0x4, tmpReg16);

    /* Adjust link timer for SGMII  -
       For Serdes 1000BaseX auto-negotiation the timer should be 10 ms.
       The link_timer register is configured in units of the clock.
       - When running as 1G SGMII, Serdes clock is 125 MHz, so
         unit = 1 / (125*10^6 Hz) = 8 ns.
         10 ms in units of 8 ns = 10ms / 8ns = 1250000 = 0x1312d0
       - When running as 2.5G SGMII, Serdes clock is 312.5 MHz, so
         unit = 1 / (312.5*10^6 Hz) = 3.2 ns.
         10 ms in units of 3.2 ns = 10ms / 3.2ns = 3125000 = 0x2faf08.
       Since link_timer value of 1G SGMII will be too short for 2.5 SGMII,
       we always set up here a value of 2.5 SGMII. */
    MEMAC_MII_WritePhyReg(p_Memac, phyAddr, 0x13, 0x002f);
    MEMAC_MII_WritePhyReg(p_Memac, phyAddr, 0x12, 0xaf08);

    /* Restart AN */
    tmpReg16 = PHY_SGMII_CR_DEF_VAL | PHY_SGMII_CR_RESET_AN;
    MEMAC_MII_WritePhyReg(p_Memac, phyAddr, 0x0, tmpReg16);

    /* Restore original enet mode */
    p_Memac->enetMode = enetMode;
}

/* ......................................................................... */

static t_Error CheckInitParameters(t_Memac *p_Memac)
{
    e_FmMacType portType;

    portType = ((ENET_SPEED_FROM_MODE(p_Memac->enetMode) < e_ENET_SPEED_10000) ? e_FM_MAC_1G : e_FM_MAC_10G);

#if (FM_MAX_NUM_OF_10G_MACS > 0)
    if ((portType == e_FM_MAC_10G) && (p_Memac->macId >= FM_MAX_NUM_OF_10G_MACS))
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("10G MAC ID must be less than %d", FM_MAX_NUM_OF_10G_MACS));
#endif /* (FM_MAX_NUM_OF_10G_MACS > 0) */

    if ((portType == e_FM_MAC_1G) && (p_Memac->macId >= FM_MAX_NUM_OF_1G_MACS))
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("1G MAC ID must be less than %d", FM_MAX_NUM_OF_1G_MACS));
    if (p_Memac->addr == 0)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Ethernet MAC must have a valid MAC address"));
    if (!p_Memac->f_Exception)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Uninitialized f_Exception"));
    if (!p_Memac->f_Event)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Uninitialized f_Event"));
#ifdef FM_LEN_CHECK_ERRATA_FMAN_SW002
    if (!p_Memac->p_MemacDriverParam->no_length_check_enable)
       RETURN_ERROR(MINOR, E_NOT_SUPPORTED, ("LengthCheck!"));
#endif /* FM_LEN_CHECK_ERRATA_FMAN_SW002 */

    return E_OK;
}

/* ........................................................................... */

static void MemacErrException(t_Handle h_Memac)
{
    t_Memac     *p_Memac = (t_Memac *)h_Memac;
    uint32_t    event, imask;

    event = fman_memac_get_event(p_Memac->p_MemMap, 0xffffffff);
    imask = fman_memac_get_interrupt_mask(p_Memac->p_MemMap);

    /* Imask include both error and notification/event bits.
       Leaving only error bits enabled by imask.
       The imask error bits are shifted by 16 bits offset from
       their corresponding location in the ievent - hence the >> 16 */
    event &= ((imask & MEMAC_ALL_ERRS_IMASK) >> 16);

    fman_memac_ack_event(p_Memac->p_MemMap, event);

    if (event & MEMAC_IEVNT_TS_ECC_ER)
        p_Memac->f_Exception(p_Memac->h_App, e_FM_MAC_EX_TS_FIFO_ECC_ERR);
    if (event & MEMAC_IEVNT_TX_ECC_ER)
        p_Memac->f_Exception(p_Memac->h_App, e_FM_MAC_EX_10G_1TX_ECC_ER);
    if (event & MEMAC_IEVNT_RX_ECC_ER)
        p_Memac->f_Exception(p_Memac->h_App, e_FM_MAC_EX_10G_RX_ECC_ER);
}

static void MemacException(t_Handle h_Memac)
{
    t_Memac     *p_Memac = (t_Memac *)h_Memac;
    uint32_t    event, imask;

    event = fman_memac_get_event(p_Memac->p_MemMap, 0xffffffff);
    imask = fman_memac_get_interrupt_mask(p_Memac->p_MemMap);

    /* Imask include both error and notification/event bits.
       Leaving only error bits enabled by imask.
       The imask error bits are shifted by 16 bits offset from
       their corresponding location in the ievent - hence the >> 16 */
    event &= ((imask & MEMAC_ALL_ERRS_IMASK) >> 16);

    fman_memac_ack_event(p_Memac->p_MemMap, event);

    if (event & MEMAC_IEVNT_MGI)
        p_Memac->f_Exception(p_Memac->h_App, e_FM_MAC_EX_MAGIC_PACKET_INDICATION);
}

/* ......................................................................... */

static void FreeInitResources(t_Memac *p_Memac)
{
    e_FmMacType portType;

    portType =
        ((ENET_SPEED_FROM_MODE(p_Memac->enetMode) < e_ENET_SPEED_10000) ? e_FM_MAC_1G : e_FM_MAC_10G);

    if (portType == e_FM_MAC_10G)
        FmUnregisterIntr(p_Memac->fmMacControllerDriver.h_Fm, e_FM_MOD_10G_MAC, p_Memac->macId, e_FM_INTR_TYPE_ERR);
    else
        FmUnregisterIntr(p_Memac->fmMacControllerDriver.h_Fm, e_FM_MOD_1G_MAC, p_Memac->macId, e_FM_INTR_TYPE_ERR);

    /* release the driver's group hash table */
    FreeHashTable(p_Memac->p_MulticastAddrHash);
    p_Memac->p_MulticastAddrHash =   NULL;

    /* release the driver's individual hash table */
    FreeHashTable(p_Memac->p_UnicastAddrHash);
    p_Memac->p_UnicastAddrHash =     NULL;
}


/*****************************************************************************/
/*                     mEMAC API routines                                    */
/*****************************************************************************/

/* ......................................................................... */

static t_Error MemacEnable(t_Handle h_Memac,  e_CommMode mode)
{
    t_Memac     *p_Memac = (t_Memac *)h_Memac;

    SANITY_CHECK_RETURN_ERROR(p_Memac, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Memac->p_MemacDriverParam, E_INVALID_STATE);

    fman_memac_enable(p_Memac->p_MemMap, (mode & e_COMM_MODE_RX), (mode & e_COMM_MODE_TX));

    return E_OK;
}

/* ......................................................................... */

static t_Error MemacDisable (t_Handle h_Memac, e_CommMode mode)
{
    t_Memac     *p_Memac = (t_Memac *)h_Memac;

    SANITY_CHECK_RETURN_ERROR(p_Memac, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Memac->p_MemacDriverParam, E_INVALID_STATE);

    fman_memac_disable(p_Memac->p_MemMap, (mode & e_COMM_MODE_RX), (mode & e_COMM_MODE_TX));

    return E_OK;
}

/* ......................................................................... */

static t_Error MemacSetPromiscuous(t_Handle h_Memac, bool newVal)
{
    t_Memac     *p_Memac = (t_Memac *)h_Memac;

    SANITY_CHECK_RETURN_ERROR(p_Memac, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Memac->p_MemacDriverParam, E_INVALID_STATE);

    fman_memac_set_promiscuous(p_Memac->p_MemMap, newVal);

    return E_OK;
}

/* .............................................................................. */

static t_Error MemacAdjustLink(t_Handle h_Memac, e_EnetSpeed speed, bool fullDuplex)
{
    t_Memac     *p_Memac = (t_Memac *)h_Memac;

    SANITY_CHECK_RETURN_ERROR(p_Memac, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Memac->p_MemacDriverParam, E_INVALID_STATE);

    if ((speed >= e_ENET_SPEED_1000) && (!fullDuplex))
        RETURN_ERROR(MAJOR, E_CONFLICT,
                     ("Ethernet MAC 1G or 10G does not support half-duplex"));

    fman_memac_adjust_link(p_Memac->p_MemMap,
                           (enum enet_interface)ENET_INTERFACE_FROM_MODE(p_Memac->enetMode),
                           (enum enet_speed)speed,
                           fullDuplex);
    return E_OK;
}


/*****************************************************************************/
/*                      Memac Configs modification functions                 */
/*****************************************************************************/

/* ......................................................................... */

static t_Error MemacConfigLoopback(t_Handle h_Memac, bool newVal)
{
    t_Memac     *p_Memac = (t_Memac *)h_Memac;

    SANITY_CHECK_RETURN_ERROR(p_Memac, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Memac->p_MemacDriverParam, E_INVALID_STATE);

    p_Memac->p_MemacDriverParam->loopback_enable = newVal;

    return E_OK;
}

/* ......................................................................... */

static t_Error MemacConfigWan(t_Handle h_Memac, bool newVal)
{
    t_Memac     *p_Memac = (t_Memac *)h_Memac;

    SANITY_CHECK_RETURN_ERROR(p_Memac, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Memac->p_MemacDriverParam, E_INVALID_STATE);

    p_Memac->p_MemacDriverParam->wan_mode_enable = newVal;

    return E_OK;
}

/* ......................................................................... */

static t_Error MemacConfigMaxFrameLength(t_Handle h_Memac, uint16_t newVal)
{
    t_Memac     *p_Memac = (t_Memac *)h_Memac;

    SANITY_CHECK_RETURN_ERROR(p_Memac, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Memac->p_MemacDriverParam, E_INVALID_STATE);

    p_Memac->p_MemacDriverParam->max_frame_length = newVal;

    return E_OK;
}

/* ......................................................................... */

static t_Error MemacConfigPad(t_Handle h_Memac, bool newVal)
{
    t_Memac     *p_Memac = (t_Memac *)h_Memac;

    SANITY_CHECK_RETURN_ERROR(p_Memac, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Memac->p_MemacDriverParam, E_INVALID_STATE);

    p_Memac->p_MemacDriverParam->pad_enable = newVal;

    return E_OK;
}

/* ......................................................................... */

static t_Error MemacConfigLengthCheck(t_Handle h_Memac, bool newVal)
{
    t_Memac     *p_Memac = (t_Memac *)h_Memac;

    SANITY_CHECK_RETURN_ERROR(p_Memac, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Memac->p_MemacDriverParam, E_INVALID_STATE);

    p_Memac->p_MemacDriverParam->no_length_check_enable = !newVal;

    return E_OK;
}

/* ......................................................................... */

static t_Error MemacConfigException(t_Handle h_Memac, e_FmMacExceptions exception, bool enable)
{
    t_Memac     *p_Memac = (t_Memac *)h_Memac;
    uint32_t    bitMask = 0;

    SANITY_CHECK_RETURN_ERROR(p_Memac, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Memac->p_MemacDriverParam, E_INVALID_STATE);

    GET_EXCEPTION_FLAG(bitMask, exception);
    if (bitMask)
    {
        if (enable)
            p_Memac->exceptions |= bitMask;
        else
            p_Memac->exceptions &= ~bitMask;
    }
    else
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Undefined exception"));

    return E_OK;
}

/* ......................................................................... */

static t_Error MemacConfigResetOnInit(t_Handle h_Memac, bool enable)
{
    t_Memac     *p_Memac = (t_Memac *)h_Memac;

    SANITY_CHECK_RETURN_ERROR(p_Memac, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Memac->p_MemacDriverParam, E_INVALID_STATE);

    p_Memac->p_MemacDriverParam->reset_on_init = enable;

    return E_OK;
}


/*****************************************************************************/
/*                      Memac Run Time API functions                         */
/*****************************************************************************/

/* ......................................................................... */

static t_Error MemacSetTxPauseFrames(t_Handle h_Memac,
                                     uint8_t  priority,
                                     uint16_t pauseTime,
                                     uint16_t threshTime)
{
    t_Memac     *p_Memac = (t_Memac *)h_Memac;

    SANITY_CHECK_RETURN_ERROR(p_Memac, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(!p_Memac->p_MemacDriverParam, E_INVALID_STATE);

    if (priority != 0xFF)
    {
        bool   PortConfigured, PreFetchEnabled;

        if (FmGetTnumAgingPeriod(p_Memac->fmMacControllerDriver.h_Fm) == 0)
            RETURN_ERROR(MAJOR, E_CONFLICT, ("For PFC operation, TNUM aging must be enabled"));

        FmGetPortPreFetchConfiguration(p_Memac->fmMacControllerDriver.h_Fm,
                                       p_Memac->fmMacControllerDriver.macId,
                                       &PortConfigured,
                                       &PreFetchEnabled);

        if ((ENET_SPEED_FROM_MODE(p_Memac->fmMacControllerDriver.enetMode) == e_ENET_SPEED_1000) && !PortConfigured)
            DBG(INFO, ("For PFC correct operation, prefetch must be configured on the FM Tx PORT"));

        if ((ENET_SPEED_FROM_MODE(p_Memac->fmMacControllerDriver.enetMode) == e_ENET_SPEED_1000) && PortConfigured && !PreFetchEnabled)
            DBG(WARNING, ("For PFC correct operation, prefetch must be configured on the FM Tx PORT"));
    }

    fman_memac_set_tx_pause_frames(p_Memac->p_MemMap, priority, pauseTime, threshTime);

    return E_OK;
}

/* ......................................................................... */

static t_Error MemacSetTxAutoPauseFrames(t_Handle h_Memac,
                                         uint16_t pauseTime)
{
    return MemacSetTxPauseFrames(h_Memac, FM_MAC_NO_PFC, pauseTime, 0);
}

/* ......................................................................... */

static t_Error MemacSetRxIgnorePauseFrames(t_Handle h_Memac, bool en)
{
    t_Memac     *p_Memac = (t_Memac *)h_Memac;

    SANITY_CHECK_RETURN_ERROR(p_Memac, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(!p_Memac->p_MemacDriverParam, E_INVALID_STATE);

    fman_memac_set_rx_ignore_pause_frames(p_Memac->p_MemMap, en);

    return E_OK;
}

/* ......................................................................... */

static t_Error MemacSetWakeOnLan(t_Handle h_Memac, bool en)
{
    t_Memac     *p_Memac = (t_Memac *)h_Memac;

    SANITY_CHECK_RETURN_ERROR(p_Memac, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(!p_Memac->p_MemacDriverParam, E_INVALID_STATE);

    fman_memac_set_wol(p_Memac->p_MemMap, en);

    return E_OK;
}

/* .............................................................................. */

static t_Error MemacEnable1588TimeStamp(t_Handle h_Memac)
{
    t_Memac     *p_Memac = (t_Memac *)h_Memac;

    SANITY_CHECK_RETURN_ERROR(p_Memac, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Memac->p_MemacDriverParam, E_INVALID_STATE);
UNUSED(p_Memac);
DBG(WARNING, ("mEMAC has 1588 always enabled!"));

    return E_OK;
}

/* Counters handling */
/* ......................................................................... */

static t_Error MemacGetStatistics(t_Handle h_Memac, t_FmMacStatistics *p_Statistics)
{
    t_Memac     *p_Memac = (t_Memac *)h_Memac;

    SANITY_CHECK_RETURN_ERROR(p_Memac, E_NULL_POINTER);
    SANITY_CHECK_RETURN_ERROR(!p_Memac->p_MemacDriverParam, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(p_Statistics, E_NULL_POINTER);

    p_Statistics->eStatPkts64           = fman_memac_get_counter(p_Memac->p_MemMap, E_MEMAC_COUNTER_R64);
    p_Statistics->eStatPkts65to127      = fman_memac_get_counter(p_Memac->p_MemMap, E_MEMAC_COUNTER_R127);
    p_Statistics->eStatPkts128to255     = fman_memac_get_counter(p_Memac->p_MemMap, E_MEMAC_COUNTER_R255);
    p_Statistics->eStatPkts256to511     = fman_memac_get_counter(p_Memac->p_MemMap, E_MEMAC_COUNTER_R511);
    p_Statistics->eStatPkts512to1023    = fman_memac_get_counter(p_Memac->p_MemMap, E_MEMAC_COUNTER_R1023);
    p_Statistics->eStatPkts1024to1518   = fman_memac_get_counter(p_Memac->p_MemMap, E_MEMAC_COUNTER_R1518);
    p_Statistics->eStatPkts1519to1522   = fman_memac_get_counter(p_Memac->p_MemMap, E_MEMAC_COUNTER_R1519X);
/* */
    p_Statistics->eStatFragments        = fman_memac_get_counter(p_Memac->p_MemMap, E_MEMAC_COUNTER_RFRG);
    p_Statistics->eStatJabbers          = fman_memac_get_counter(p_Memac->p_MemMap, E_MEMAC_COUNTER_RJBR);

    p_Statistics->eStatsDropEvents      = fman_memac_get_counter(p_Memac->p_MemMap, E_MEMAC_COUNTER_RDRP);
    p_Statistics->eStatCRCAlignErrors   = fman_memac_get_counter(p_Memac->p_MemMap, E_MEMAC_COUNTER_RALN);

    p_Statistics->eStatUndersizePkts    = fman_memac_get_counter(p_Memac->p_MemMap, E_MEMAC_COUNTER_TUND);
    p_Statistics->eStatOversizePkts     = fman_memac_get_counter(p_Memac->p_MemMap, E_MEMAC_COUNTER_ROVR);
/* Pause */
    p_Statistics->reStatPause           = fman_memac_get_counter(p_Memac->p_MemMap, E_MEMAC_COUNTER_RXPF);
    p_Statistics->teStatPause           = fman_memac_get_counter(p_Memac->p_MemMap, E_MEMAC_COUNTER_TXPF);

/* MIB II */
    p_Statistics->ifInOctets            = fman_memac_get_counter(p_Memac->p_MemMap, E_MEMAC_COUNTER_ROCT);
    p_Statistics->ifInUcastPkts         = fman_memac_get_counter(p_Memac->p_MemMap, E_MEMAC_COUNTER_RUCA);
    p_Statistics->ifInMcastPkts         = fman_memac_get_counter(p_Memac->p_MemMap, E_MEMAC_COUNTER_RMCA);
    p_Statistics->ifInBcastPkts         = fman_memac_get_counter(p_Memac->p_MemMap, E_MEMAC_COUNTER_RBCA);
    p_Statistics->ifInPkts              = p_Statistics->ifInUcastPkts
                                        + p_Statistics->ifInMcastPkts
                                        + p_Statistics->ifInBcastPkts;
    p_Statistics->ifInDiscards          = 0;
    p_Statistics->ifInErrors            = fman_memac_get_counter(p_Memac->p_MemMap, E_MEMAC_COUNTER_RERR);

    p_Statistics->ifOutOctets           = fman_memac_get_counter(p_Memac->p_MemMap, E_MEMAC_COUNTER_TOCT);
    p_Statistics->ifOutUcastPkts        = fman_memac_get_counter(p_Memac->p_MemMap, E_MEMAC_COUNTER_TUCA);
    p_Statistics->ifOutMcastPkts        = fman_memac_get_counter(p_Memac->p_MemMap, E_MEMAC_COUNTER_TMCA);
    p_Statistics->ifOutBcastPkts        = fman_memac_get_counter(p_Memac->p_MemMap, E_MEMAC_COUNTER_TBCA);
    p_Statistics->ifOutPkts             = p_Statistics->ifOutUcastPkts
                                        + p_Statistics->ifOutMcastPkts
                                        + p_Statistics->ifOutBcastPkts;
    p_Statistics->ifOutDiscards         = 0;
    p_Statistics->ifOutErrors           = fman_memac_get_counter(p_Memac->p_MemMap,  E_MEMAC_COUNTER_TERR);

    return E_OK;
}

/* ......................................................................... */

static t_Error MemacModifyMacAddress (t_Handle h_Memac, t_EnetAddr *p_EnetAddr)
{
    t_Memac     *p_Memac = (t_Memac *)h_Memac;

    SANITY_CHECK_RETURN_ERROR(p_Memac, E_NULL_POINTER);
    SANITY_CHECK_RETURN_ERROR(!p_Memac->p_MemacDriverParam, E_INVALID_STATE);

    fman_memac_add_addr_in_paddr(p_Memac->p_MemMap, (uint8_t *)(*p_EnetAddr), 0);

    return E_OK;
}

/* ......................................................................... */

static t_Error MemacResetCounters (t_Handle h_Memac)
{
    t_Memac     *p_Memac = (t_Memac *)h_Memac;

    SANITY_CHECK_RETURN_ERROR(p_Memac, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Memac->p_MemacDriverParam, E_INVALID_STATE);

    fman_memac_reset_stat(p_Memac->p_MemMap);

    return E_OK;
}

/* ......................................................................... */

static t_Error MemacAddExactMatchMacAddress(t_Handle h_Memac, t_EnetAddr *p_EthAddr)
{
    t_Memac     *p_Memac = (t_Memac *) h_Memac;
    uint64_t    ethAddr;
    uint8_t     paddrNum;

    SANITY_CHECK_RETURN_ERROR(p_Memac, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Memac->p_MemacDriverParam, E_INVALID_STATE);

    ethAddr = ENET_ADDR_TO_UINT64(*p_EthAddr);

    if (ethAddr & GROUP_ADDRESS)
        /* Multicast address has no effect in PADDR */
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Multicast address"));

    /* Make sure no PADDR contains this address */
    for (paddrNum = 0; paddrNum < MEMAC_NUM_OF_PADDRS; paddrNum++)
        if (p_Memac->indAddrRegUsed[paddrNum])
            if (p_Memac->paddr[paddrNum] == ethAddr)
                RETURN_ERROR(MAJOR, E_ALREADY_EXISTS, NO_MSG);

    /* Find first unused PADDR */
    for (paddrNum = 0; paddrNum < MEMAC_NUM_OF_PADDRS; paddrNum++)
        if (!(p_Memac->indAddrRegUsed[paddrNum]))
        {
            /* mark this PADDR as used */
            p_Memac->indAddrRegUsed[paddrNum] = TRUE;
            /* store address */
            p_Memac->paddr[paddrNum] = ethAddr;

            /* put in hardware */
            fman_memac_add_addr_in_paddr(p_Memac->p_MemMap, (uint8_t*)(*p_EthAddr), paddrNum);
            p_Memac->numOfIndAddrInRegs++;

            return E_OK;
        }

    /* No free PADDR */
    RETURN_ERROR(MAJOR, E_FULL, NO_MSG);
}

/* ......................................................................... */

static t_Error MemacDelExactMatchMacAddress(t_Handle h_Memac, t_EnetAddr *p_EthAddr)
{
    t_Memac     *p_Memac = (t_Memac *) h_Memac;
    uint64_t    ethAddr;
    uint8_t     paddrNum;

    SANITY_CHECK_RETURN_ERROR(p_Memac, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Memac->p_MemacDriverParam, E_INVALID_STATE);

    ethAddr = ENET_ADDR_TO_UINT64(*p_EthAddr);

    /* Find used PADDR containing this address */
    for (paddrNum = 0; paddrNum < MEMAC_NUM_OF_PADDRS; paddrNum++)
    {
        if ((p_Memac->indAddrRegUsed[paddrNum]) &&
            (p_Memac->paddr[paddrNum] == ethAddr))
        {
            /* mark this PADDR as not used */
            p_Memac->indAddrRegUsed[paddrNum] = FALSE;
            /* clear in hardware */
            fman_memac_clear_addr_in_paddr(p_Memac->p_MemMap, paddrNum);
            p_Memac->numOfIndAddrInRegs--;

            return E_OK;
        }
    }

    RETURN_ERROR(MAJOR, E_NOT_FOUND, NO_MSG);
}

/* ......................................................................... */

static t_Error MemacGetId(t_Handle h_Memac, uint32_t *macId)
{
    t_Memac     *p_Memac = (t_Memac *)h_Memac;

    SANITY_CHECK_RETURN_ERROR(p_Memac, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Memac->p_MemacDriverParam, E_INVALID_STATE);

    *macId = p_Memac->macId;

    return E_OK;
}

/* ......................................................................... */


static t_Error MemacAddHashMacAddress(t_Handle h_Memac, t_EnetAddr *p_EthAddr)
{
    t_Memac             *p_Memac = (t_Memac *)h_Memac;
    t_EthHashEntry      *p_HashEntry;
    uint32_t            hash;
    uint64_t            ethAddr;

    SANITY_CHECK_RETURN_ERROR(p_Memac, E_NULL_POINTER);
    SANITY_CHECK_RETURN_ERROR(!p_Memac->p_MemacDriverParam, E_INVALID_STATE);

    ethAddr = ENET_ADDR_TO_UINT64(*p_EthAddr);

    if (!(ethAddr & GROUP_ADDRESS))
        /* Unicast addresses not supported in hash */
        RETURN_ERROR(MAJOR, E_NOT_SUPPORTED, ("Unicast Address"));

    hash = GetMacAddrHashCode(ethAddr) & HASH_CTRL_ADDR_MASK;

    /* Create element to be added to the driver hash table */
    p_HashEntry = (t_EthHashEntry *)XX_Malloc(sizeof(t_EthHashEntry));
    p_HashEntry->addr = ethAddr;
    INIT_LIST(&p_HashEntry->node);

    LIST_AddToTail(&(p_HashEntry->node), &(p_Memac->p_MulticastAddrHash->p_Lsts[hash]));
    fman_memac_set_hash_table(p_Memac->p_MemMap, (hash | HASH_CTRL_MCAST_EN));

    return E_OK;
}

/* ......................................................................... */

static t_Error MemacDelHashMacAddress(t_Handle h_Memac, t_EnetAddr *p_EthAddr)
{
    t_Memac             *p_Memac = (t_Memac *)h_Memac;
    t_EthHashEntry      *p_HashEntry = NULL;
    t_List              *p_Pos;
    uint32_t            hash;
    uint64_t            ethAddr;

    SANITY_CHECK_RETURN_ERROR(p_Memac, E_NULL_POINTER);
    SANITY_CHECK_RETURN_ERROR(!p_Memac->p_MemacDriverParam, E_INVALID_STATE);

    ethAddr = ENET_ADDR_TO_UINT64(*p_EthAddr);

    hash = GetMacAddrHashCode(ethAddr) & HASH_CTRL_ADDR_MASK;

    LIST_FOR_EACH(p_Pos, &(p_Memac->p_MulticastAddrHash->p_Lsts[hash]))
    {
        p_HashEntry = ETH_HASH_ENTRY_OBJ(p_Pos);
        if (p_HashEntry->addr == ethAddr)
        {
            LIST_DelAndInit(&p_HashEntry->node);
            XX_Free(p_HashEntry);
            break;
        }
    }
    if (LIST_IsEmpty(&p_Memac->p_MulticastAddrHash->p_Lsts[hash]))
        fman_memac_set_hash_table(p_Memac->p_MemMap, (hash & ~HASH_CTRL_MCAST_EN));

    return E_OK;
}


/* ......................................................................... */

static t_Error MemacSetException(t_Handle h_Memac, e_FmMacExceptions exception, bool enable)
{
    t_Memac     *p_Memac = (t_Memac *)h_Memac;
    uint32_t    bitMask = 0;

    SANITY_CHECK_RETURN_ERROR(p_Memac, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Memac->p_MemacDriverParam, E_INVALID_STATE);

    GET_EXCEPTION_FLAG(bitMask, exception);
    if (bitMask)
    {
        if (enable)
            p_Memac->exceptions |= bitMask;
        else
            p_Memac->exceptions &= ~bitMask;
    }
    else
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Undefined exception"));

    fman_memac_set_exception(p_Memac->p_MemMap, bitMask, enable);

    return E_OK;
}

/* ......................................................................... */

static uint16_t MemacGetMaxFrameLength(t_Handle h_Memac)
{
    t_Memac     *p_Memac = (t_Memac *)h_Memac;

    SANITY_CHECK_RETURN_VALUE(p_Memac, E_INVALID_HANDLE, 0);
    SANITY_CHECK_RETURN_VALUE(!p_Memac->p_MemacDriverParam, E_INVALID_STATE, 0);

    return fman_memac_get_max_frame_len(p_Memac->p_MemMap);
}

static t_Error MemacInitInternalPhy(t_Handle h_Memac)
{
    t_Memac *p_Memac = (t_Memac *)h_Memac;
    uint8_t i, phyAddr;

    if (ENET_INTERFACE_FROM_MODE(p_Memac->enetMode) == e_ENET_IF_SGMII)
    {
        /* Configure internal SGMII PHY */
        if (p_Memac->enetMode & ENET_IF_SGMII_BASEX)
            SetupSgmiiInternalPhyBaseX(p_Memac, PHY_MDIO_ADDR);
        else
            SetupSgmiiInternalPhy(p_Memac, PHY_MDIO_ADDR);
    }
    else if (ENET_INTERFACE_FROM_MODE(p_Memac->enetMode) == e_ENET_IF_QSGMII)
    {
        /* Configure 4 internal SGMII PHYs */
        for (i = 0; i < 4; i++)
        {
            /* QSGMII PHY address occupies 3 upper bits of 5-bit
               phyAddress; the lower 2 bits are used to extend
               register address space and access each one of 4
               ports inside QSGMII. */
            phyAddr = (uint8_t)((PHY_MDIO_ADDR << 2) | i);
            if (p_Memac->enetMode & ENET_IF_SGMII_BASEX)
                SetupSgmiiInternalPhyBaseX(p_Memac, phyAddr);
            else
                SetupSgmiiInternalPhy(p_Memac, phyAddr);
        }
    }
    return E_OK;
}

/*****************************************************************************/
/*                      mEMAC Init & Free API                                   */
/*****************************************************************************/

/* ......................................................................... */
void *g_MemacRegs;
static t_Error MemacInit(t_Handle h_Memac)
{
    t_Memac                 *p_Memac = (t_Memac *)h_Memac;
    struct memac_cfg        *p_MemacDriverParam;
    enum enet_interface     enet_interface;
    enum enet_speed         enet_speed;
    t_EnetAddr              ethAddr;
    e_FmMacType             portType;
    t_Error                 err;
    bool                    slow_10g_if = FALSE;
    if (p_Memac->macId == 3) /* This is a quick WA */
		g_MemacRegs = p_Memac->p_MemMap;

    SANITY_CHECK_RETURN_ERROR(p_Memac, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Memac->p_MemacDriverParam, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(p_Memac->fmMacControllerDriver.h_Fm, E_INVALID_HANDLE);

    FM_GetRevision(p_Memac->fmMacControllerDriver.h_Fm, &p_Memac->fmMacControllerDriver.fmRevInfo);
    if (p_Memac->fmMacControllerDriver.fmRevInfo.majorRev == 6 &&
        p_Memac->fmMacControllerDriver.fmRevInfo.minorRev == 4)
        slow_10g_if = TRUE;

    CHECK_INIT_PARAMETERS(p_Memac, CheckInitParameters);

    p_MemacDriverParam = p_Memac->p_MemacDriverParam;

    portType =
        ((ENET_SPEED_FROM_MODE(p_Memac->enetMode) < e_ENET_SPEED_10000) ? e_FM_MAC_1G : e_FM_MAC_10G);

    /* First, reset the MAC if desired. */
    if (p_MemacDriverParam->reset_on_init)
        fman_memac_reset(p_Memac->p_MemMap);

    /* MAC Address */
    MAKE_ENET_ADDR_FROM_UINT64(p_Memac->addr, ethAddr);
    fman_memac_add_addr_in_paddr(p_Memac->p_MemMap, (uint8_t*)ethAddr, 0);

    enet_interface = (enum enet_interface) ENET_INTERFACE_FROM_MODE(p_Memac->enetMode);
    enet_speed = (enum enet_speed) ENET_SPEED_FROM_MODE(p_Memac->enetMode);

    fman_memac_init(p_Memac->p_MemMap,
               p_Memac->p_MemacDriverParam,
               enet_interface,
               enet_speed,
               slow_10g_if,
               p_Memac->exceptions);

#ifdef FM_RX_FIFO_CORRUPT_ERRATA_10GMAC_A006320
    {
    	uint32_t tmpReg = 0;

    	FM_GetRevision(p_Memac->fmMacControllerDriver.h_Fm, &p_Memac->fmMacControllerDriver.fmRevInfo);
        /* check the FMAN version - the bug exists only in rev1 */
        if ((p_Memac->fmMacControllerDriver.fmRevInfo.majorRev == 6) &&
        	(p_Memac->fmMacControllerDriver.fmRevInfo.minorRev == 0))
        {
        	/* MAC strips CRC from received frames - this workaround should
        	   decrease the likelihood of bug appearance
            */
			tmpReg = GET_UINT32(p_Memac->p_MemMap->command_config);
			tmpReg &= ~CMD_CFG_CRC_FWD;
			WRITE_UINT32(p_Memac->p_MemMap->command_config, tmpReg);
			/* DBG(WARNING, ("mEMAC strips CRC from received frames as part of A006320 errata workaround"));*/
        }
    }
#endif /* FM_RX_FIFO_CORRUPT_ERRATA_10GMAC_A006320 */

    MemacInitInternalPhy(h_Memac);

    /* Max Frame Length */
    err = FmSetMacMaxFrame(p_Memac->fmMacControllerDriver.h_Fm,
                           portType,
                           p_Memac->fmMacControllerDriver.macId,
                           p_MemacDriverParam->max_frame_length);
    if (err)
        RETURN_ERROR(MAJOR, err, ("settings Mac max frame length is FAILED"));

    p_Memac->p_MulticastAddrHash = AllocHashTable(HASH_TABLE_SIZE);
    if (!p_Memac->p_MulticastAddrHash)
    {
        FreeInitResources(p_Memac);
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("allocation hash table is FAILED"));
    }

    p_Memac->p_UnicastAddrHash = AllocHashTable(HASH_TABLE_SIZE);
    if (!p_Memac->p_UnicastAddrHash)
    {
        FreeInitResources(p_Memac);
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("allocation hash table is FAILED"));
    }

    FmRegisterIntr(p_Memac->fmMacControllerDriver.h_Fm,
                   (portType == e_FM_MAC_10G) ? e_FM_MOD_10G_MAC : e_FM_MOD_1G_MAC,
                   p_Memac->macId,
                   e_FM_INTR_TYPE_ERR,
                   MemacErrException,
                   p_Memac);

    FmRegisterIntr(p_Memac->fmMacControllerDriver.h_Fm,
                   (portType == e_FM_MAC_10G) ? e_FM_MOD_10G_MAC : e_FM_MOD_1G_MAC,
                   p_Memac->macId,
                   e_FM_INTR_TYPE_NORMAL,
                   MemacException,
                   p_Memac);

    XX_Free(p_MemacDriverParam);
    p_Memac->p_MemacDriverParam = NULL;

    return E_OK;
}

/* ......................................................................... */

static t_Error MemacFree(t_Handle h_Memac)
{
    t_Memac     *p_Memac = (t_Memac *)h_Memac;

    SANITY_CHECK_RETURN_ERROR(p_Memac, E_INVALID_HANDLE);

    if (p_Memac->p_MemacDriverParam)
    {
        /* Called after config */
        XX_Free(p_Memac->p_MemacDriverParam);
        p_Memac->p_MemacDriverParam = NULL;
    }
    else
        /* Called after init */
        FreeInitResources(p_Memac);

    XX_Free(p_Memac);

    return E_OK;
}

/* ......................................................................... */

static void InitFmMacControllerDriver(t_FmMacControllerDriver *p_FmMacControllerDriver)
{
    p_FmMacControllerDriver->f_FM_MAC_Init                      = MemacInit;
    p_FmMacControllerDriver->f_FM_MAC_Free                      = MemacFree;

    p_FmMacControllerDriver->f_FM_MAC_SetStatistics             = NULL;
    p_FmMacControllerDriver->f_FM_MAC_ConfigLoopback            = MemacConfigLoopback;
    p_FmMacControllerDriver->f_FM_MAC_ConfigMaxFrameLength      = MemacConfigMaxFrameLength;

    p_FmMacControllerDriver->f_FM_MAC_ConfigWan                 = MemacConfigWan;

    p_FmMacControllerDriver->f_FM_MAC_ConfigPadAndCrc           = MemacConfigPad;
    p_FmMacControllerDriver->f_FM_MAC_ConfigHalfDuplex          = NULL; /* half-duplex is detected automatically */
    p_FmMacControllerDriver->f_FM_MAC_ConfigLengthCheck         = MemacConfigLengthCheck;

    p_FmMacControllerDriver->f_FM_MAC_ConfigException           = MemacConfigException;
    p_FmMacControllerDriver->f_FM_MAC_ConfigResetOnInit         = MemacConfigResetOnInit;

    p_FmMacControllerDriver->f_FM_MAC_SetException              = MemacSetException;

    p_FmMacControllerDriver->f_FM_MAC_Enable1588TimeStamp       = MemacEnable1588TimeStamp; /* always enabled */
    p_FmMacControllerDriver->f_FM_MAC_Disable1588TimeStamp      = NULL;

    p_FmMacControllerDriver->f_FM_MAC_SetPromiscuous            = MemacSetPromiscuous;
    p_FmMacControllerDriver->f_FM_MAC_AdjustLink                = MemacAdjustLink;
    p_FmMacControllerDriver->f_FM_MAC_RestartAutoneg            = NULL;

    p_FmMacControllerDriver->f_FM_MAC_Enable                    = MemacEnable;
    p_FmMacControllerDriver->f_FM_MAC_Disable                   = MemacDisable;
    p_FmMacControllerDriver->f_FM_MAC_Resume                    = MemacInitInternalPhy;

    p_FmMacControllerDriver->f_FM_MAC_SetTxAutoPauseFrames      = MemacSetTxAutoPauseFrames;
    p_FmMacControllerDriver->f_FM_MAC_SetTxPauseFrames          = MemacSetTxPauseFrames;
    p_FmMacControllerDriver->f_FM_MAC_SetRxIgnorePauseFrames    = MemacSetRxIgnorePauseFrames;

    p_FmMacControllerDriver->f_FM_MAC_SetWakeOnLan              = MemacSetWakeOnLan;

    p_FmMacControllerDriver->f_FM_MAC_ResetCounters             = MemacResetCounters;
    p_FmMacControllerDriver->f_FM_MAC_GetStatistics             = MemacGetStatistics;

    p_FmMacControllerDriver->f_FM_MAC_ModifyMacAddr             = MemacModifyMacAddress;
    p_FmMacControllerDriver->f_FM_MAC_AddHashMacAddr            = MemacAddHashMacAddress;
    p_FmMacControllerDriver->f_FM_MAC_RemoveHashMacAddr         = MemacDelHashMacAddress;
    p_FmMacControllerDriver->f_FM_MAC_AddExactMatchMacAddr      = MemacAddExactMatchMacAddress;
    p_FmMacControllerDriver->f_FM_MAC_RemovelExactMatchMacAddr  = MemacDelExactMatchMacAddress;
    p_FmMacControllerDriver->f_FM_MAC_GetId                     = MemacGetId;
    p_FmMacControllerDriver->f_FM_MAC_GetVersion                = NULL;
    p_FmMacControllerDriver->f_FM_MAC_GetMaxFrameLength         = MemacGetMaxFrameLength;

    p_FmMacControllerDriver->f_FM_MAC_MII_WritePhyReg           = MEMAC_MII_WritePhyReg;
    p_FmMacControllerDriver->f_FM_MAC_MII_ReadPhyReg            = MEMAC_MII_ReadPhyReg;
}


/*****************************************************************************/
/*                      mEMAC Config Main Entry                             */
/*****************************************************************************/

/* ......................................................................... */

t_Handle MEMAC_Config(t_FmMacParams *p_FmMacParam)
{
    t_Memac             *p_Memac;
    struct memac_cfg    *p_MemacDriverParam;
    uintptr_t           baseAddr;

    SANITY_CHECK_RETURN_VALUE(p_FmMacParam, E_NULL_POINTER, NULL);

    baseAddr = p_FmMacParam->baseAddr;
    /* Allocate memory for the mEMAC data structure */
    p_Memac = (t_Memac *)XX_Malloc(sizeof(t_Memac));
    if (!p_Memac)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("mEMAC driver structure"));
        return NULL;
    }
    memset(p_Memac, 0, sizeof(t_Memac));
    InitFmMacControllerDriver(&p_Memac->fmMacControllerDriver);

    /* Allocate memory for the mEMAC driver parameters data structure */
    p_MemacDriverParam = (struct memac_cfg *)XX_Malloc(sizeof(struct memac_cfg));
    if (!p_MemacDriverParam)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("mEMAC driver parameters"));
        XX_Free(p_Memac);
        return NULL;
    }
    memset(p_MemacDriverParam, 0, sizeof(struct memac_cfg));

    /* Plant parameter structure pointer */
    p_Memac->p_MemacDriverParam = p_MemacDriverParam;

    fman_memac_defconfig(p_MemacDriverParam);

    p_Memac->addr           = ENET_ADDR_TO_UINT64(p_FmMacParam->addr);

    p_Memac->p_MemMap       = (struct memac_regs *)UINT_TO_PTR(baseAddr);
    p_Memac->p_MiiMemMap    = (struct memac_mii_access_mem_map*)UINT_TO_PTR(baseAddr + MEMAC_TO_MII_OFFSET);

    p_Memac->enetMode       = p_FmMacParam->enetMode;
    p_Memac->macId          = p_FmMacParam->macId;
    p_Memac->exceptions     = MEMAC_default_exceptions;
    p_Memac->f_Exception    = p_FmMacParam->f_Exception;
    p_Memac->f_Event        = p_FmMacParam->f_Event;
    p_Memac->h_App          = p_FmMacParam->h_App;

    return p_Memac;
}
