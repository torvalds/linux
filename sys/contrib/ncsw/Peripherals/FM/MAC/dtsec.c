/*
 * Copyright 2008-2013 Freescale Semiconductor Inc.
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
 @File          dtsec.c

 @Description   FMan dTSEC driver
*//***************************************************************************/

#include "std_ext.h"
#include "error_ext.h"
#include "string_ext.h"
#include "xx_ext.h"
#include "endian_ext.h"
#include "debug_ext.h"
#include "crc_mac_addr_ext.h"

#include "fm_common.h"
#include "dtsec.h"
#include "fsl_fman_dtsec.h"
#include "fsl_fman_dtsec_mii_acc.h"

/*****************************************************************************/
/*                      Internal routines                                    */
/*****************************************************************************/

static t_Error CheckInitParameters(t_Dtsec *p_Dtsec)
{
    if (ENET_SPEED_FROM_MODE(p_Dtsec->enetMode) >= e_ENET_SPEED_10000)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Ethernet 1G MAC driver only supports 1G or lower speeds"));
    if (p_Dtsec->macId >= FM_MAX_NUM_OF_1G_MACS)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("macId can not be greater than the number of 1G MACs"));
    if (p_Dtsec->addr == 0)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Ethernet MAC Must have a valid MAC Address"));
    if ((ENET_SPEED_FROM_MODE(p_Dtsec->enetMode) >= e_ENET_SPEED_1000) &&
        p_Dtsec->p_DtsecDriverParam->halfdup_on)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Ethernet MAC 1G can't work in half duplex"));
    if (p_Dtsec->p_DtsecDriverParam->halfdup_on && (p_Dtsec->p_DtsecDriverParam)->loopback)
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("LoopBack is not supported in halfDuplex mode"));
#ifdef FM_RX_PREAM_4_ERRATA_DTSEC_A001
    if (p_Dtsec->fmMacControllerDriver.fmRevInfo.majorRev <= 6) /* fixed for rev3 */
        if (p_Dtsec->p_DtsecDriverParam->rx_preamble)
            RETURN_ERROR(MAJOR, E_NOT_SUPPORTED, ("preambleRxEn"));
#endif /* FM_RX_PREAM_4_ERRATA_DTSEC_A001 */
    if (((p_Dtsec->p_DtsecDriverParam)->tx_preamble || (p_Dtsec->p_DtsecDriverParam)->rx_preamble) &&( (p_Dtsec->p_DtsecDriverParam)->preamble_len != 0x7))
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Preamble length should be 0x7 bytes"));
    if ((p_Dtsec->p_DtsecDriverParam)->halfdup_on &&
       (p_Dtsec->p_DtsecDriverParam->tx_time_stamp_en || p_Dtsec->p_DtsecDriverParam->rx_time_stamp_en))
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("dTSEC in half duplex mode has to be with 1588 timeStamping diable"));
    if ((p_Dtsec->p_DtsecDriverParam)->rx_flow && (p_Dtsec->p_DtsecDriverParam)->rx_ctrl_acc )
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Receive control frame are not passed to the system memory so it can not be accept "));
    if ((p_Dtsec->p_DtsecDriverParam)->rx_prepend  > MAX_PACKET_ALIGNMENT)
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("packetAlignmentPadding can't be greater than %d ",MAX_PACKET_ALIGNMENT ));
    if (((p_Dtsec->p_DtsecDriverParam)->non_back_to_back_ipg1  > MAX_INTER_PACKET_GAP) ||
        ((p_Dtsec->p_DtsecDriverParam)->non_back_to_back_ipg2 > MAX_INTER_PACKET_GAP) ||
        ((p_Dtsec->p_DtsecDriverParam)->back_to_back_ipg > MAX_INTER_PACKET_GAP))
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Inter packet gap can't be greater than %d ",MAX_INTER_PACKET_GAP ));
    if ((p_Dtsec->p_DtsecDriverParam)->halfdup_alt_backoff_val > MAX_INTER_PALTERNATE_BEB)
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("alternateBackoffVal can't be greater than %d ",MAX_INTER_PALTERNATE_BEB ));
    if ((p_Dtsec->p_DtsecDriverParam)->halfdup_retransmit > MAX_RETRANSMISSION)
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("maxRetransmission can't be greater than %d ",MAX_RETRANSMISSION ));
    if ((p_Dtsec->p_DtsecDriverParam)->halfdup_coll_window > MAX_COLLISION_WINDOW)
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("collisionWindow can't be greater than %d ",MAX_COLLISION_WINDOW ));

    /*  If Auto negotiation process is disabled, need to */
    /*  Set up the PHY using the MII Management Interface */
    if (p_Dtsec->p_DtsecDriverParam->tbipa > MAX_PHYS)
        RETURN_ERROR(MAJOR, E_NOT_IN_RANGE, ("PHY address (should be 0-%d)", MAX_PHYS));
    if (!p_Dtsec->f_Exception)
        RETURN_ERROR(MAJOR, E_INVALID_HANDLE, ("uninitialized f_Exception"));
    if (!p_Dtsec->f_Event)
        RETURN_ERROR(MAJOR, E_INVALID_HANDLE, ("uninitialized f_Event"));

#ifdef FM_LEN_CHECK_ERRATA_FMAN_SW002
    if (p_Dtsec->p_DtsecDriverParam->rx_len_check)
       RETURN_ERROR(MINOR, E_NOT_SUPPORTED, ("LengthCheck!"));
#endif /* FM_LEN_CHECK_ERRATA_FMAN_SW002 */

    return E_OK;
}

/* ......................................................................... */

static uint32_t GetMacAddrHashCode(uint64_t ethAddr)
{
    uint32_t crc;

    /* CRC calculation */
    GET_MAC_ADDR_CRC(ethAddr, crc);

    crc = GetMirror32(crc);

    return crc;
}

/* ......................................................................... */

static void UpdateStatistics(t_Dtsec *p_Dtsec)
{
    uint32_t car1, car2;

    fman_dtsec_get_clear_carry_regs(p_Dtsec->p_MemMap, &car1, &car2);

    if (car1)
    {
        if (car1 & CAR1_TR64)
            p_Dtsec->internalStatistics.tr64 += VAL22BIT;
        if (car1 & CAR1_TR127)
            p_Dtsec->internalStatistics.tr127 += VAL22BIT;
        if (car1 & CAR1_TR255)
            p_Dtsec->internalStatistics.tr255 += VAL22BIT;
        if (car1 & CAR1_TR511)
            p_Dtsec->internalStatistics.tr511 += VAL22BIT;
        if (car1 & CAR1_TRK1)
            p_Dtsec->internalStatistics.tr1k += VAL22BIT;
        if (car1 & CAR1_TRMAX)
            p_Dtsec->internalStatistics.trmax += VAL22BIT;
        if (car1 & CAR1_TRMGV)
            p_Dtsec->internalStatistics.trmgv += VAL22BIT;
        if (car1 & CAR1_RBYT)
            p_Dtsec->internalStatistics.rbyt += (uint64_t)VAL32BIT;
        if (car1 & CAR1_RPKT)
            p_Dtsec->internalStatistics.rpkt += VAL22BIT;
        if (car1 & CAR1_RMCA)
            p_Dtsec->internalStatistics.rmca += VAL22BIT;
        if (car1 & CAR1_RBCA)
            p_Dtsec->internalStatistics.rbca += VAL22BIT;
        if (car1 & CAR1_RXPF)
            p_Dtsec->internalStatistics.rxpf += VAL16BIT;
        if (car1 & CAR1_RALN)
            p_Dtsec->internalStatistics.raln += VAL16BIT;
        if (car1 & CAR1_RFLR)
            p_Dtsec->internalStatistics.rflr += VAL16BIT;
        if (car1 & CAR1_RCDE)
            p_Dtsec->internalStatistics.rcde += VAL16BIT;
        if (car1 & CAR1_RCSE)
            p_Dtsec->internalStatistics.rcse += VAL16BIT;
        if (car1 & CAR1_RUND)
            p_Dtsec->internalStatistics.rund += VAL16BIT;
        if (car1 & CAR1_ROVR)
            p_Dtsec->internalStatistics.rovr += VAL16BIT;
        if (car1 & CAR1_RFRG)
            p_Dtsec->internalStatistics.rfrg += VAL16BIT;
        if (car1 & CAR1_RJBR)
            p_Dtsec->internalStatistics.rjbr += VAL16BIT;
        if (car1 & CAR1_RDRP)
            p_Dtsec->internalStatistics.rdrp += VAL16BIT;
    }
    if (car2)
    {
        if (car2  & CAR2_TFCS)
            p_Dtsec->internalStatistics.tfcs += VAL12BIT;
        if (car2  & CAR2_TBYT)
            p_Dtsec->internalStatistics.tbyt += (uint64_t)VAL32BIT;
        if (car2  & CAR2_TPKT)
            p_Dtsec->internalStatistics.tpkt += VAL22BIT;
        if (car2  & CAR2_TMCA)
            p_Dtsec->internalStatistics.tmca += VAL22BIT;
        if (car2  & CAR2_TBCA)
            p_Dtsec->internalStatistics.tbca += VAL22BIT;
        if (car2  & CAR2_TXPF)
            p_Dtsec->internalStatistics.txpf += VAL16BIT;
        if (car2  & CAR2_TDRP)
            p_Dtsec->internalStatistics.tdrp += VAL16BIT;
    }
}

/* .............................................................................. */

static uint16_t DtsecGetMaxFrameLength(t_Handle h_Dtsec)
{
    t_Dtsec     *p_Dtsec = (t_Dtsec *)h_Dtsec;

    SANITY_CHECK_RETURN_VALUE(p_Dtsec, E_INVALID_HANDLE, 0);
    SANITY_CHECK_RETURN_VALUE(!p_Dtsec->p_DtsecDriverParam, E_INVALID_STATE, 0);

    return fman_dtsec_get_max_frame_len(p_Dtsec->p_MemMap);
}

/* .............................................................................. */

static void DtsecIsr(t_Handle h_Dtsec)
{
    t_Dtsec             *p_Dtsec = (t_Dtsec *)h_Dtsec;
    uint32_t            event;
    struct dtsec_regs   *p_DtsecMemMap = p_Dtsec->p_MemMap;

    /* do not handle MDIO events */
    event = fman_dtsec_get_event(p_DtsecMemMap, (uint32_t)(~(DTSEC_IMASK_MMRDEN | DTSEC_IMASK_MMWREN)));

    event &= fman_dtsec_get_interrupt_mask(p_DtsecMemMap);

    fman_dtsec_ack_event(p_DtsecMemMap, event);

    if (event & DTSEC_IMASK_BREN)
        p_Dtsec->f_Exception(p_Dtsec->h_App, e_FM_MAC_EX_1G_BAB_RX);
    if (event & DTSEC_IMASK_RXCEN)
        p_Dtsec->f_Exception(p_Dtsec->h_App, e_FM_MAC_EX_1G_RX_CTL);
    if (event & DTSEC_IMASK_MSROEN)
        UpdateStatistics(p_Dtsec);
    if (event & DTSEC_IMASK_GTSCEN)
        p_Dtsec->f_Exception(p_Dtsec->h_App, e_FM_MAC_EX_1G_GRATEFUL_TX_STP_COMPLET);
    if (event & DTSEC_IMASK_BTEN)
        p_Dtsec->f_Exception(p_Dtsec->h_App, e_FM_MAC_EX_1G_BAB_TX);
    if (event & DTSEC_IMASK_TXCEN)
        p_Dtsec->f_Exception(p_Dtsec->h_App, e_FM_MAC_EX_1G_TX_CTL);
    if (event & DTSEC_IMASK_TXEEN)
        p_Dtsec->f_Exception(p_Dtsec->h_App, e_FM_MAC_EX_1G_TX_ERR);
    if (event & DTSEC_IMASK_LCEN)
        p_Dtsec->f_Exception(p_Dtsec->h_App, e_FM_MAC_EX_1G_LATE_COL);
    if (event & DTSEC_IMASK_CRLEN)
        p_Dtsec->f_Exception(p_Dtsec->h_App, e_FM_MAC_EX_1G_COL_RET_LMT);
    if (event & DTSEC_IMASK_XFUNEN)
    {
#ifdef FM_TX_LOCKUP_ERRATA_DTSEC6
        if (p_Dtsec->fmMacControllerDriver.fmRevInfo.majorRev == 2)
        {
            uint32_t  tpkt1, tmpReg1, tpkt2, tmpReg2, i;
            /* a. Write 0x00E0_0C00 to DTSEC_ID */
            /* This is a read only regidter */

            /* b. Read and save the value of TPKT */
            tpkt1 = GET_UINT32(p_DtsecMemMap->tpkt);

            /* c. Read the register at dTSEC address offset 0x32C */
            tmpReg1 =  GET_UINT32(*(uint32_t*)((uint8_t*)p_DtsecMemMap + 0x32c));

            /* d. Compare bits [9:15] to bits [25:31] of the register at address offset 0x32C. */
            if ((tmpReg1 & 0x007F0000) != (tmpReg1 & 0x0000007F))
            {
                /* If they are not equal, save the value of this register and wait for at least
                 * MAXFRM*16 ns */
                XX_UDelay((uint32_t)(MIN(DtsecGetMaxFrameLength(p_Dtsec)*16/1000, 1)));
            }

            /* e. Read and save TPKT again and read the register at dTSEC address offset
                0x32C again*/
            tpkt2 = GET_UINT32(p_DtsecMemMap->tpkt);
            tmpReg2 = GET_UINT32(*(uint32_t*)((uint8_t*)p_DtsecMemMap + 0x32c));

            /* f. Compare the value of TPKT saved in step b to value read in step e. Also
                compare bits [9:15] of the register at offset 0x32C saved in step d to the value
                of bits [9:15] saved in step e. If the two registers values are unchanged, then
                the transmit portion of the dTSEC controller is locked up and the user should
                proceed to the recover sequence. */
            if ((tpkt1 == tpkt2) && ((tmpReg1 & 0x007F0000) == (tmpReg2 & 0x007F0000)))
            {
                /* recover sequence */

                /* a.Write a 1 to RCTRL[GRS]*/

                WRITE_UINT32(p_DtsecMemMap->rctrl, GET_UINT32(p_DtsecMemMap->rctrl) | RCTRL_GRS);

                /* b.Wait until IEVENT[GRSC]=1, or at least 100 us has elapsed. */
                for (i = 0 ; i < 100 ; i++ )
                {
                    if (GET_UINT32(p_DtsecMemMap->ievent) & DTSEC_IMASK_GRSCEN)
                        break;
                    XX_UDelay(1);
                }
                if (GET_UINT32(p_DtsecMemMap->ievent) & DTSEC_IMASK_GRSCEN)
                    WRITE_UINT32(p_DtsecMemMap->ievent, DTSEC_IMASK_GRSCEN);
                else
                    DBG(INFO,("Rx lockup due to dTSEC Tx lockup"));

                /* c.Write a 1 to bit n of FM_RSTC (offset 0x0CC of FPM)*/
                FmResetMac(p_Dtsec->fmMacControllerDriver.h_Fm, e_FM_MAC_1G, p_Dtsec->fmMacControllerDriver.macId);

                /* d.Wait 4 Tx clocks (32 ns) */
                XX_UDelay(1);

                /* e.Write a 0 to bit n of FM_RSTC. */
                /* cleared by FMAN */
            }
        }
#endif /* FM_TX_LOCKUP_ERRATA_DTSEC6 */

        p_Dtsec->f_Exception(p_Dtsec->h_App, e_FM_MAC_EX_1G_TX_FIFO_UNDRN);
    }
    if (event & DTSEC_IMASK_MAGEN)
        p_Dtsec->f_Exception(p_Dtsec->h_App, e_FM_MAC_EX_1G_MAG_PCKT);
    if (event & DTSEC_IMASK_GRSCEN)
        p_Dtsec->f_Exception(p_Dtsec->h_App, e_FM_MAC_EX_1G_GRATEFUL_RX_STP_COMPLET);
    if (event & DTSEC_IMASK_TDPEEN)
        p_Dtsec->f_Exception(p_Dtsec->h_App, e_FM_MAC_EX_1G_TX_DATA_ERR);
    if (event & DTSEC_IMASK_RDPEEN)
        p_Dtsec->f_Exception(p_Dtsec->h_App, e_FM_MAC_EX_1G_RX_DATA_ERR);

    /*  - masked interrupts */
    ASSERT_COND(!(event & DTSEC_IMASK_ABRTEN));
    ASSERT_COND(!(event & DTSEC_IMASK_IFERREN));
}

static void DtsecMdioIsr(t_Handle h_Dtsec)
{
    t_Dtsec             *p_Dtsec = (t_Dtsec *)h_Dtsec;
    uint32_t            event;
    struct dtsec_regs   *p_DtsecMemMap = p_Dtsec->p_MemMap;

    event = GET_UINT32(p_DtsecMemMap->ievent);
    /* handle only MDIO events */
    event &= (DTSEC_IMASK_MMRDEN | DTSEC_IMASK_MMWREN);
    if (event)
    {
        event &= GET_UINT32(p_DtsecMemMap->imask);

        WRITE_UINT32(p_DtsecMemMap->ievent, event);

        if (event & DTSEC_IMASK_MMRDEN)
            p_Dtsec->f_Event(p_Dtsec->h_App, e_FM_MAC_EX_1G_MII_MNG_RD_COMPLET);
        if (event & DTSEC_IMASK_MMWREN)
            p_Dtsec->f_Event(p_Dtsec->h_App, e_FM_MAC_EX_1G_MII_MNG_WR_COMPLET);
    }
}

static void Dtsec1588Isr(t_Handle h_Dtsec)
{
    t_Dtsec             *p_Dtsec = (t_Dtsec *)h_Dtsec;
    uint32_t            event;
    struct dtsec_regs   *p_DtsecMemMap = p_Dtsec->p_MemMap;

    if (p_Dtsec->ptpTsuEnabled)
    {
        event = fman_dtsec_check_and_clear_tmr_event(p_DtsecMemMap);

        if (event)
        {
            ASSERT_COND(event & TMR_PEVENT_TSRE);
            p_Dtsec->f_Exception(p_Dtsec->h_App, e_FM_MAC_EX_1G_1588_TS_RX_ERR);
        }
    }
}

/* ........................................................................... */

static void FreeInitResources(t_Dtsec *p_Dtsec)
{
    if (p_Dtsec->mdioIrq != NO_IRQ)
    {
        XX_DisableIntr(p_Dtsec->mdioIrq);
        XX_FreeIntr(p_Dtsec->mdioIrq);
    }
    FmUnregisterIntr(p_Dtsec->fmMacControllerDriver.h_Fm, e_FM_MOD_1G_MAC, p_Dtsec->macId, e_FM_INTR_TYPE_ERR);
    FmUnregisterIntr(p_Dtsec->fmMacControllerDriver.h_Fm, e_FM_MOD_1G_MAC, p_Dtsec->macId, e_FM_INTR_TYPE_NORMAL);

    /* release the driver's group hash table */
    FreeHashTable(p_Dtsec->p_MulticastAddrHash);
    p_Dtsec->p_MulticastAddrHash =   NULL;

    /* release the driver's individual hash table */
    FreeHashTable(p_Dtsec->p_UnicastAddrHash);
    p_Dtsec->p_UnicastAddrHash =     NULL;
}

/* ........................................................................... */

static t_Error GracefulStop(t_Dtsec *p_Dtsec, e_CommMode mode)
{
    struct dtsec_regs *p_MemMap;

    ASSERT_COND(p_Dtsec);

    p_MemMap = p_Dtsec->p_MemMap;
    ASSERT_COND(p_MemMap);

    /* Assert the graceful transmit stop bit */
    if (mode & e_COMM_MODE_RX)
    {
        fman_dtsec_stop_rx(p_MemMap);

#ifdef FM_GRS_ERRATA_DTSEC_A002
        if (p_Dtsec->fmMacControllerDriver.fmRevInfo.majorRev == 2)
            XX_UDelay(100);
#else  /* FM_GRS_ERRATA_DTSEC_A002 */
#ifdef FM_GTS_AFTER_DROPPED_FRAME_ERRATA_DTSEC_A004839
        XX_UDelay(10);
#endif /* FM_GTS_AFTER_DROPPED_FRAME_ERRATA_DTSEC_A004839 */
#endif /* FM_GRS_ERRATA_DTSEC_A002 */
    }

    if (mode & e_COMM_MODE_TX)
#if defined(FM_GTS_ERRATA_DTSEC_A004) || defined(FM_GTS_AFTER_MAC_ABORTED_FRAME_ERRATA_DTSEC_A0012)
    if (p_Dtsec->fmMacControllerDriver.fmRevInfo.majorRev == 2)
        DBG(INFO, ("GTS not supported due to DTSEC_A004 errata."));
#else  /* not defined(FM_GTS_ERRATA_DTSEC_A004) ||... */
#ifdef FM_GTS_UNDERRUN_ERRATA_DTSEC_A0014
        DBG(INFO, ("GTS not supported due to DTSEC_A0014 errata."));
#else  /* FM_GTS_UNDERRUN_ERRATA_DTSEC_A0014 */
        fman_dtsec_stop_tx(p_MemMap);
#endif /* FM_GTS_UNDERRUN_ERRATA_DTSEC_A0014 */
#endif /* defined(FM_GTS_ERRATA_DTSEC_A004) ||...  */

    return E_OK;
}

/* .............................................................................. */

static t_Error GracefulRestart(t_Dtsec *p_Dtsec, e_CommMode mode)
{
    struct dtsec_regs *p_MemMap;

    ASSERT_COND(p_Dtsec);
    p_MemMap = p_Dtsec->p_MemMap;
    ASSERT_COND(p_MemMap);

    /* clear the graceful receive stop bit */
    if (mode & e_COMM_MODE_TX)
        fman_dtsec_start_tx(p_MemMap);

    if (mode & e_COMM_MODE_RX)
        fman_dtsec_start_rx(p_MemMap);

    return E_OK;
}


/*****************************************************************************/
/*                      dTSEC Configs modification functions                 */
/*****************************************************************************/

/* .............................................................................. */

static t_Error DtsecConfigLoopback(t_Handle h_Dtsec, bool newVal)
{

    t_Dtsec *p_Dtsec = (t_Dtsec *)h_Dtsec;

    SANITY_CHECK_RETURN_ERROR(p_Dtsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Dtsec->p_DtsecDriverParam, E_INVALID_STATE);

    p_Dtsec->p_DtsecDriverParam->loopback = newVal;

    return E_OK;
}

/* .............................................................................. */

static t_Error DtsecConfigMaxFrameLength(t_Handle h_Dtsec, uint16_t newVal)
{
    t_Dtsec *p_Dtsec = (t_Dtsec *)h_Dtsec;

    SANITY_CHECK_RETURN_ERROR(p_Dtsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Dtsec->p_DtsecDriverParam, E_INVALID_STATE);

    p_Dtsec->p_DtsecDriverParam->maximum_frame = newVal;

    return E_OK;
}

/* .............................................................................. */

static t_Error DtsecConfigPadAndCrc(t_Handle h_Dtsec, bool newVal)
{
    t_Dtsec *p_Dtsec = (t_Dtsec *)h_Dtsec;

    SANITY_CHECK_RETURN_ERROR(p_Dtsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Dtsec->p_DtsecDriverParam, E_INVALID_STATE);

    p_Dtsec->p_DtsecDriverParam->tx_pad_crc = newVal;

    return E_OK;
}

/* .............................................................................. */

static t_Error DtsecConfigHalfDuplex(t_Handle h_Dtsec, bool newVal)
{
    t_Dtsec *p_Dtsec = (t_Dtsec *)h_Dtsec;

    SANITY_CHECK_RETURN_ERROR(p_Dtsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Dtsec->p_DtsecDriverParam, E_INVALID_STATE);

    p_Dtsec->p_DtsecDriverParam->halfdup_on = newVal;

    return E_OK;
}

/* .............................................................................. */

static t_Error DtsecConfigTbiPhyAddr(t_Handle h_Dtsec, uint8_t newVal)
{
    t_Dtsec *p_Dtsec = (t_Dtsec *)h_Dtsec;

    SANITY_CHECK_RETURN_ERROR(p_Dtsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Dtsec->p_DtsecDriverParam, E_INVALID_STATE);

    p_Dtsec->p_DtsecDriverParam->tbi_phy_addr = newVal;

    return E_OK;
}

/* .............................................................................. */

static t_Error DtsecConfigLengthCheck(t_Handle h_Dtsec, bool newVal)
{
    t_Dtsec *p_Dtsec = (t_Dtsec *)h_Dtsec;

    SANITY_CHECK_RETURN_ERROR(p_Dtsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Dtsec->p_DtsecDriverParam, E_INVALID_STATE);

    p_Dtsec->p_DtsecDriverParam->rx_len_check = newVal;

    return E_OK;
}

/* .............................................................................. */

static t_Error DtsecConfigException(t_Handle h_Dtsec, e_FmMacExceptions exception, bool enable)
{
    t_Dtsec *p_Dtsec = (t_Dtsec *)h_Dtsec;
    uint32_t    bitMask = 0;

    SANITY_CHECK_RETURN_ERROR(p_Dtsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Dtsec->p_DtsecDriverParam, E_INVALID_STATE);

    if (exception != e_FM_MAC_EX_1G_1588_TS_RX_ERR)
    {
        GET_EXCEPTION_FLAG(bitMask, exception);
        if (bitMask)
        {
            if (enable)
                p_Dtsec->exceptions |= bitMask;
            else
                p_Dtsec->exceptions &= ~bitMask;
        }
        else
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Undefined exception"));
    }
    else
    {
        if (!p_Dtsec->ptpTsuEnabled)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Exception valid for 1588 only"));

        if (enable)
            p_Dtsec->enTsuErrExeption = TRUE;
        else
            p_Dtsec->enTsuErrExeption = FALSE;
    }

    return E_OK;
}


/*****************************************************************************/
/*                      dTSEC Run Time API functions                         */
/*****************************************************************************/

/* .............................................................................. */

static t_Error DtsecEnable(t_Handle h_Dtsec,  e_CommMode mode)
{
    t_Dtsec     *p_Dtsec = (t_Dtsec *)h_Dtsec;

    SANITY_CHECK_RETURN_ERROR(p_Dtsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Dtsec->p_DtsecDriverParam, E_INVALID_STATE);

    fman_dtsec_enable(p_Dtsec->p_MemMap,
                 (bool)!!(mode & e_COMM_MODE_RX),
                 (bool)!!(mode & e_COMM_MODE_TX));

    GracefulRestart(p_Dtsec, mode);

    return E_OK;
}

/* .............................................................................. */

static t_Error DtsecDisable (t_Handle h_Dtsec, e_CommMode mode)
{
    t_Dtsec     *p_Dtsec = (t_Dtsec *)h_Dtsec;

    SANITY_CHECK_RETURN_ERROR(p_Dtsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Dtsec->p_DtsecDriverParam, E_INVALID_STATE);

    GracefulStop(p_Dtsec, mode);

    fman_dtsec_disable(p_Dtsec->p_MemMap,
                  (bool)!!(mode & e_COMM_MODE_RX),
                  (bool)!!(mode & e_COMM_MODE_TX));

    return E_OK;
}

/* .............................................................................. */

static t_Error DtsecSetTxPauseFrames(t_Handle h_Dtsec,
                                     uint8_t  priority,
                                     uint16_t pauseTime,
                                     uint16_t threshTime)
{
    t_Dtsec     *p_Dtsec = (t_Dtsec *)h_Dtsec;

    UNUSED(priority);UNUSED(threshTime);

    SANITY_CHECK_RETURN_ERROR(p_Dtsec, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(!p_Dtsec->p_DtsecDriverParam, E_INVALID_STATE);

#ifdef FM_BAD_TX_TS_IN_B_2_B_ERRATA_DTSEC_A003
    if (p_Dtsec->fmMacControllerDriver.fmRevInfo.majorRev == 2)
        if (0 < pauseTime && pauseTime <= 320)
            RETURN_ERROR(MINOR, E_INVALID_VALUE,
                     ("This pause-time value of %d is illegal due to errata dTSEC-A003!"
                      " value should be greater than 320."));
#endif /* FM_BAD_TX_TS_IN_B_2_B_ERRATA_DTSEC_A003 */

    fman_dtsec_set_tx_pause_frames(p_Dtsec->p_MemMap, pauseTime);
    return E_OK;
}

/* .............................................................................. */
/* backward compatibility. will be removed in the future. */
static t_Error DtsecTxMacPause(t_Handle h_Dtsec, uint16_t pauseTime)
{
    return DtsecSetTxPauseFrames(h_Dtsec, 0, pauseTime, 0);
}

/* .............................................................................. */

static t_Error DtsecRxIgnoreMacPause(t_Handle h_Dtsec, bool en)
{
    t_Dtsec         *p_Dtsec = (t_Dtsec *)h_Dtsec;
    bool            accept_pause = !en;

    SANITY_CHECK_RETURN_ERROR(p_Dtsec, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(!p_Dtsec->p_DtsecDriverParam, E_INVALID_STATE);

    fman_dtsec_handle_rx_pause(p_Dtsec->p_MemMap, accept_pause);

    return E_OK;
}

/* .............................................................................. */

static t_Error DtsecEnable1588TimeStamp(t_Handle h_Dtsec)
{
    t_Dtsec     *p_Dtsec = (t_Dtsec *)h_Dtsec;

    SANITY_CHECK_RETURN_ERROR(p_Dtsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Dtsec->p_DtsecDriverParam, E_INVALID_STATE);

    p_Dtsec->ptpTsuEnabled = TRUE;
    fman_dtsec_set_ts(p_Dtsec->p_MemMap, TRUE);

    return E_OK;
}

/* .............................................................................. */

static t_Error DtsecDisable1588TimeStamp(t_Handle h_Dtsec)
{
    t_Dtsec     *p_Dtsec = (t_Dtsec *)h_Dtsec;

    SANITY_CHECK_RETURN_ERROR(p_Dtsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Dtsec->p_DtsecDriverParam, E_INVALID_STATE);

    p_Dtsec->ptpTsuEnabled = FALSE;
    fman_dtsec_set_ts(p_Dtsec->p_MemMap, FALSE);

    return E_OK;
}

/* .............................................................................. */

static t_Error DtsecGetStatistics(t_Handle h_Dtsec, t_FmMacStatistics *p_Statistics)
{
    t_Dtsec             *p_Dtsec = (t_Dtsec *)h_Dtsec;
    struct dtsec_regs   *p_DtsecMemMap;

    SANITY_CHECK_RETURN_ERROR(p_Dtsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Dtsec->p_DtsecDriverParam, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(p_Statistics, E_NULL_POINTER);

    p_DtsecMemMap = p_Dtsec->p_MemMap;

    if (p_Dtsec->statisticsLevel == e_FM_MAC_NONE_STATISTICS)
        RETURN_ERROR(MINOR, E_INVALID_STATE, ("Statistics disabled"));

    memset(p_Statistics, 0xff, sizeof(t_FmMacStatistics));

    if (p_Dtsec->statisticsLevel == e_FM_MAC_FULL_STATISTICS)
    {
        p_Statistics->eStatPkts64 = fman_dtsec_get_stat_counter(p_DtsecMemMap, E_DTSEC_STAT_TR64)
                + p_Dtsec->internalStatistics.tr64;
        p_Statistics->eStatPkts65to127 = fman_dtsec_get_stat_counter(p_DtsecMemMap, E_DTSEC_STAT_TR127)
                + p_Dtsec->internalStatistics.tr127;
        p_Statistics->eStatPkts128to255 = fman_dtsec_get_stat_counter(p_DtsecMemMap, E_DTSEC_STAT_TR255)
                + p_Dtsec->internalStatistics.tr255;
        p_Statistics->eStatPkts256to511 = fman_dtsec_get_stat_counter(p_DtsecMemMap, E_DTSEC_STAT_TR511)
                + p_Dtsec->internalStatistics.tr511;
        p_Statistics->eStatPkts512to1023 = fman_dtsec_get_stat_counter(p_DtsecMemMap, E_DTSEC_STAT_TR1K)
                + p_Dtsec->internalStatistics.tr1k;
        p_Statistics->eStatPkts1024to1518 = fman_dtsec_get_stat_counter(p_DtsecMemMap, E_DTSEC_STAT_TRMAX)
                + p_Dtsec->internalStatistics.trmax;
        p_Statistics->eStatPkts1519to1522 = fman_dtsec_get_stat_counter(p_DtsecMemMap, E_DTSEC_STAT_TRMGV)
                + p_Dtsec->internalStatistics.trmgv;

        /* MIB II */
        p_Statistics->ifInOctets = fman_dtsec_get_stat_counter(p_DtsecMemMap, E_DTSEC_STAT_RBYT)
                + p_Dtsec->internalStatistics.rbyt;
        p_Statistics->ifInPkts = fman_dtsec_get_stat_counter(p_DtsecMemMap, E_DTSEC_STAT_RPKT)
                + p_Dtsec->internalStatistics.rpkt;
        p_Statistics->ifInUcastPkts = 0;
        p_Statistics->ifInMcastPkts = fman_dtsec_get_stat_counter(p_DtsecMemMap, E_DTSEC_STAT_RMCA)
                + p_Dtsec->internalStatistics.rmca;
        p_Statistics->ifInBcastPkts = fman_dtsec_get_stat_counter(p_DtsecMemMap, E_DTSEC_STAT_RBCA)
                + p_Dtsec->internalStatistics.rbca;
        p_Statistics->ifOutOctets = fman_dtsec_get_stat_counter(p_DtsecMemMap, E_DTSEC_STAT_TBYT)
                + p_Dtsec->internalStatistics.tbyt;
        p_Statistics->ifOutPkts = fman_dtsec_get_stat_counter(p_DtsecMemMap, E_DTSEC_STAT_TPKT)
                + p_Dtsec->internalStatistics.tpkt;
        p_Statistics->ifOutUcastPkts = 0;
        p_Statistics->ifOutMcastPkts = fman_dtsec_get_stat_counter(p_DtsecMemMap, E_DTSEC_STAT_TMCA)
                + p_Dtsec->internalStatistics.tmca;
        p_Statistics->ifOutBcastPkts = fman_dtsec_get_stat_counter(p_DtsecMemMap, E_DTSEC_STAT_TBCA)
                + p_Dtsec->internalStatistics.tbca;
    }

    p_Statistics->eStatFragments = fman_dtsec_get_stat_counter(p_DtsecMemMap, E_DTSEC_STAT_RFRG)
            + p_Dtsec->internalStatistics.rfrg;
    p_Statistics->eStatJabbers = fman_dtsec_get_stat_counter(p_DtsecMemMap, E_DTSEC_STAT_RJBR)
            + p_Dtsec->internalStatistics.rjbr;
    p_Statistics->eStatsDropEvents = fman_dtsec_get_stat_counter(p_DtsecMemMap, E_DTSEC_STAT_RDRP)
            + p_Dtsec->internalStatistics.rdrp;
    p_Statistics->eStatCRCAlignErrors = fman_dtsec_get_stat_counter(p_DtsecMemMap, E_DTSEC_STAT_RALN)
            + p_Dtsec->internalStatistics.raln;
    p_Statistics->eStatUndersizePkts = fman_dtsec_get_stat_counter(p_DtsecMemMap, E_DTSEC_STAT_RUND)
            + p_Dtsec->internalStatistics.rund;
    p_Statistics->eStatOversizePkts = fman_dtsec_get_stat_counter(p_DtsecMemMap, E_DTSEC_STAT_ROVR)
            + p_Dtsec->internalStatistics.rovr;
    p_Statistics->reStatPause = fman_dtsec_get_stat_counter(p_DtsecMemMap, E_DTSEC_STAT_RXPF)
            + p_Dtsec->internalStatistics.rxpf;
    p_Statistics->teStatPause = fman_dtsec_get_stat_counter(p_DtsecMemMap, E_DTSEC_STAT_TXPF)
            + p_Dtsec->internalStatistics.txpf;
    p_Statistics->ifInDiscards = p_Statistics->eStatsDropEvents;
    p_Statistics->ifInErrors = p_Statistics->eStatsDropEvents + p_Statistics->eStatCRCAlignErrors
            + fman_dtsec_get_stat_counter(p_DtsecMemMap,E_DTSEC_STAT_RFLR) + p_Dtsec->internalStatistics.rflr
            + fman_dtsec_get_stat_counter(p_DtsecMemMap,E_DTSEC_STAT_RCDE) + p_Dtsec->internalStatistics.rcde
            + fman_dtsec_get_stat_counter(p_DtsecMemMap,E_DTSEC_STAT_RCSE) + p_Dtsec->internalStatistics.rcse;

    p_Statistics->ifOutDiscards = fman_dtsec_get_stat_counter(p_DtsecMemMap, E_DTSEC_STAT_TDRP)
            + p_Dtsec->internalStatistics.tdrp;
    p_Statistics->ifOutErrors = p_Statistics->ifOutDiscards                                           /**< Number of frames transmitted with error: */
            + fman_dtsec_get_stat_counter(p_DtsecMemMap,E_DTSEC_STAT_TFCS)
            + p_Dtsec->internalStatistics.tfcs;

    return E_OK;
}

/* .............................................................................. */

static t_Error DtsecModifyMacAddress (t_Handle h_Dtsec, t_EnetAddr *p_EnetAddr)
{
    t_Dtsec     *p_Dtsec = (t_Dtsec *)h_Dtsec;

    SANITY_CHECK_RETURN_ERROR(p_Dtsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Dtsec->p_DtsecDriverParam, E_INVALID_STATE);

    /* Initialize MAC Station Address registers (1 & 2)    */
    /* Station address have to be swapped (big endian to little endian */
    p_Dtsec->addr = ENET_ADDR_TO_UINT64(*p_EnetAddr);
    fman_dtsec_set_mac_address(p_Dtsec->p_MemMap, (uint8_t *)(*p_EnetAddr));

    return E_OK;
}

/* .............................................................................. */

static t_Error DtsecResetCounters (t_Handle h_Dtsec)
{
    t_Dtsec     *p_Dtsec = (t_Dtsec *)h_Dtsec;

    SANITY_CHECK_RETURN_ERROR(p_Dtsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Dtsec->p_DtsecDriverParam, E_INVALID_STATE);

    /* clear HW counters */
    fman_dtsec_reset_stat(p_Dtsec->p_MemMap);

    /* clear SW counters holding carries */
    memset(&p_Dtsec->internalStatistics, 0, sizeof(t_InternalStatistics));

    return E_OK;
}

/* .............................................................................. */

static t_Error DtsecAddExactMatchMacAddress(t_Handle h_Dtsec, t_EnetAddr *p_EthAddr)
{
    t_Dtsec   *p_Dtsec = (t_Dtsec *) h_Dtsec;
    uint64_t  ethAddr;
    uint8_t   paddrNum;

    SANITY_CHECK_RETURN_ERROR(p_Dtsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Dtsec->p_DtsecDriverParam, E_INVALID_STATE);

    ethAddr = ENET_ADDR_TO_UINT64(*p_EthAddr);

    if (ethAddr & GROUP_ADDRESS)
        /* Multicast address has no effect in PADDR */
        RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Multicast address"));

    /* Make sure no PADDR contains this address */
    for (paddrNum = 0; paddrNum < DTSEC_NUM_OF_PADDRS; paddrNum++)
        if (p_Dtsec->indAddrRegUsed[paddrNum])
            if (p_Dtsec->paddr[paddrNum] == ethAddr)
                RETURN_ERROR(MAJOR, E_ALREADY_EXISTS, NO_MSG);

    /* Find first unused PADDR */
    for (paddrNum = 0; paddrNum < DTSEC_NUM_OF_PADDRS; paddrNum++)
        if (!(p_Dtsec->indAddrRegUsed[paddrNum]))
        {
            /* mark this PADDR as used */
            p_Dtsec->indAddrRegUsed[paddrNum] = TRUE;
            /* store address */
            p_Dtsec->paddr[paddrNum] = ethAddr;

            /* put in hardware */
            fman_dtsec_add_addr_in_paddr(p_Dtsec->p_MemMap, (uint64_t)PTR_TO_UINT(&ethAddr), paddrNum);
            p_Dtsec->numOfIndAddrInRegs++;

            return E_OK;
        }

    /* No free PADDR */
    RETURN_ERROR(MAJOR, E_FULL, NO_MSG);
}

/* .............................................................................. */

static t_Error DtsecDelExactMatchMacAddress(t_Handle h_Dtsec, t_EnetAddr *p_EthAddr)
{
    t_Dtsec   *p_Dtsec = (t_Dtsec *) h_Dtsec;
    uint64_t  ethAddr;
    uint8_t   paddrNum;

    SANITY_CHECK_RETURN_ERROR(p_Dtsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Dtsec->p_DtsecDriverParam, E_INVALID_STATE);

    ethAddr = ENET_ADDR_TO_UINT64(*p_EthAddr);

    /* Find used PADDR containing this address */
    for (paddrNum = 0; paddrNum < DTSEC_NUM_OF_PADDRS; paddrNum++)
    {
        if ((p_Dtsec->indAddrRegUsed[paddrNum]) &&
            (p_Dtsec->paddr[paddrNum] == ethAddr))
        {
            /* mark this PADDR as not used */
            p_Dtsec->indAddrRegUsed[paddrNum] = FALSE;
            /* clear in hardware */
            fman_dtsec_clear_addr_in_paddr(p_Dtsec->p_MemMap, paddrNum);
            p_Dtsec->numOfIndAddrInRegs--;

            return E_OK;
        }
    }

    RETURN_ERROR(MAJOR, E_NOT_FOUND, NO_MSG);
}

/* .............................................................................. */

static t_Error DtsecAddHashMacAddress(t_Handle h_Dtsec, t_EnetAddr *p_EthAddr)
{
    t_Dtsec         *p_Dtsec = (t_Dtsec *)h_Dtsec;
    t_EthHashEntry  *p_HashEntry;
    uint64_t        ethAddr;
    int32_t         bucket;
    uint32_t        crc;
    bool            mcast, ghtx;

    SANITY_CHECK_RETURN_ERROR(p_Dtsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Dtsec->p_DtsecDriverParam, E_INVALID_STATE);

    ethAddr = ENET_ADDR_TO_UINT64(*p_EthAddr);

    ghtx = (bool)((fman_dtsec_get_rctrl(p_Dtsec->p_MemMap) & RCTRL_GHTX) ? TRUE : FALSE);
    mcast = (bool)((ethAddr & MAC_GROUP_ADDRESS) ? TRUE : FALSE);

    if (ghtx && !mcast) /* Cannot handle unicast mac addr when GHTX is on */
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Could not compute hash bucket"));

    crc = GetMacAddrHashCode(ethAddr);

    /* considering the 9 highest order bits in crc H[8:0]:
     * if ghtx = 0 H[8:6] (highest order 3 bits) identify the hash register
     * and H[5:1] (next 5 bits) identify the hash bit
     * if ghts = 1 H[8:5] (highest order 4 bits) identify the hash register
     * and H[4:0] (next 5 bits) identify the hash bit.
     *
     * In bucket index output the low 5 bits identify the hash register bit,
     * while the higher 4 bits identify the hash register
     */

    if (ghtx)
        bucket = (int32_t)((crc >> 23) & 0x1ff);
    else {
        bucket = (int32_t)((crc >> 24) & 0xff);
        /* if !ghtx and mcast the bit must be set in gaddr instead of igaddr. */
        if (mcast)
            bucket += 0x100;
    }

    fman_dtsec_set_bucket(p_Dtsec->p_MemMap, bucket, TRUE);

    /* Create element to be added to the driver hash table */
    p_HashEntry = (t_EthHashEntry *)XX_Malloc(sizeof(t_EthHashEntry));
    p_HashEntry->addr = ethAddr;
    INIT_LIST(&p_HashEntry->node);

    if (ethAddr & MAC_GROUP_ADDRESS)
        /* Group Address */
        NCSW_LIST_AddToTail(&(p_HashEntry->node), &(p_Dtsec->p_MulticastAddrHash->p_Lsts[bucket]));
    else
        NCSW_LIST_AddToTail(&(p_HashEntry->node), &(p_Dtsec->p_UnicastAddrHash->p_Lsts[bucket]));

    return E_OK;
}

/* .............................................................................. */

static t_Error DtsecDelHashMacAddress(t_Handle h_Dtsec, t_EnetAddr *p_EthAddr)
{
    t_Dtsec         *p_Dtsec = (t_Dtsec *)h_Dtsec;
    t_List          *p_Pos;
    t_EthHashEntry  *p_HashEntry = NULL;
    uint64_t        ethAddr;
    int32_t         bucket;
    uint32_t        crc;
    bool            mcast, ghtx;

    SANITY_CHECK_RETURN_ERROR(p_Dtsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Dtsec->p_DtsecDriverParam, E_INVALID_STATE);

    ethAddr = ENET_ADDR_TO_UINT64(*p_EthAddr);

    ghtx = (bool)((fman_dtsec_get_rctrl(p_Dtsec->p_MemMap) & RCTRL_GHTX) ? TRUE : FALSE);
    mcast = (bool)((ethAddr & MAC_GROUP_ADDRESS) ? TRUE : FALSE);

    if (ghtx && !mcast) /* Cannot handle unicast mac addr when GHTX is on */
        RETURN_ERROR(MAJOR, E_INVALID_STATE, ("Could not compute hash bucket"));

    crc = GetMacAddrHashCode(ethAddr);

    if (ghtx)
        bucket = (int32_t)((crc >> 23) & 0x1ff);
    else {
        bucket = (int32_t)((crc >> 24) & 0xff);
        /* if !ghtx and mcast the bit must be set in gaddr instead of igaddr. */
        if (mcast)
            bucket += 0x100;
    }

    if (ethAddr & MAC_GROUP_ADDRESS)
    {
        /* Group Address */
        NCSW_LIST_FOR_EACH(p_Pos, &(p_Dtsec->p_MulticastAddrHash->p_Lsts[bucket]))
        {
            p_HashEntry = ETH_HASH_ENTRY_OBJ(p_Pos);
            if (p_HashEntry->addr == ethAddr)
            {
                NCSW_LIST_DelAndInit(&p_HashEntry->node);
                XX_Free(p_HashEntry);
                break;
            }
        }
        if (NCSW_LIST_IsEmpty(&p_Dtsec->p_MulticastAddrHash->p_Lsts[bucket]))
            fman_dtsec_set_bucket(p_Dtsec->p_MemMap, bucket, FALSE);
    }
    else
    {
        /* Individual Address */
        NCSW_LIST_FOR_EACH(p_Pos, &(p_Dtsec->p_UnicastAddrHash->p_Lsts[bucket]))
        {
            p_HashEntry = ETH_HASH_ENTRY_OBJ(p_Pos);
            if (p_HashEntry->addr == ethAddr)
            {
                NCSW_LIST_DelAndInit(&p_HashEntry->node);
                XX_Free(p_HashEntry);
                break;
            }
        }
        if (NCSW_LIST_IsEmpty(&p_Dtsec->p_UnicastAddrHash->p_Lsts[bucket]))
            fman_dtsec_set_bucket(p_Dtsec->p_MemMap, bucket, FALSE);
    }

    /* address does not exist */
    ASSERT_COND(p_HashEntry != NULL);

    return E_OK;
}

/* .............................................................................. */

static t_Error DtsecSetPromiscuous(t_Handle h_Dtsec, bool newVal)
{
    t_Dtsec     *p_Dtsec = (t_Dtsec *)h_Dtsec;

    SANITY_CHECK_RETURN_ERROR(p_Dtsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Dtsec->p_DtsecDriverParam, E_INVALID_STATE);

    fman_dtsec_set_uc_promisc(p_Dtsec->p_MemMap, newVal);
    fman_dtsec_set_mc_promisc(p_Dtsec->p_MemMap, newVal);

    return E_OK;
}

/* .............................................................................. */

static t_Error DtsecSetStatistics(t_Handle h_Dtsec, e_FmMacStatisticsLevel statisticsLevel)
{
    t_Dtsec     *p_Dtsec = (t_Dtsec *)h_Dtsec;
    t_Error     err;

    SANITY_CHECK_RETURN_ERROR(p_Dtsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Dtsec->p_DtsecDriverParam, E_INVALID_STATE);

    p_Dtsec->statisticsLevel = statisticsLevel;

    err = (t_Error)fman_dtsec_set_stat_level(p_Dtsec->p_MemMap,
                                        (enum dtsec_stat_level)statisticsLevel);
    if (err != E_OK)
        return err;

    switch (statisticsLevel)
    {
    case (e_FM_MAC_NONE_STATISTICS):
            p_Dtsec->exceptions &= ~DTSEC_IMASK_MSROEN;
            break;
    case (e_FM_MAC_PARTIAL_STATISTICS):
            p_Dtsec->exceptions |= DTSEC_IMASK_MSROEN;
            break;
    case (e_FM_MAC_FULL_STATISTICS):
            p_Dtsec->exceptions |= DTSEC_IMASK_MSROEN;
            break;
        default:
            RETURN_ERROR(MINOR, E_INVALID_SELECTION, NO_MSG);
    }

    return E_OK;
}

/* .............................................................................. */

static t_Error DtsecSetWakeOnLan(t_Handle h_Dtsec, bool en)
{
    t_Dtsec         *p_Dtsec = (t_Dtsec *)h_Dtsec;

    SANITY_CHECK_RETURN_ERROR(p_Dtsec, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(!p_Dtsec->p_DtsecDriverParam, E_INVALID_STATE);

    fman_dtsec_set_wol(p_Dtsec->p_MemMap, en);

    return E_OK;
}

/* .............................................................................. */

static t_Error DtsecAdjustLink(t_Handle h_Dtsec, e_EnetSpeed speed, bool fullDuplex)
{
    t_Dtsec             *p_Dtsec = (t_Dtsec *)h_Dtsec;
    int                 err;
    enum enet_interface enet_interface;
    enum enet_speed     enet_speed;

    SANITY_CHECK_RETURN_ERROR(p_Dtsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Dtsec->p_DtsecDriverParam, E_INVALID_STATE);

    p_Dtsec->enetMode = MAKE_ENET_MODE(ENET_INTERFACE_FROM_MODE(p_Dtsec->enetMode), speed);
    enet_interface = (enum enet_interface) ENET_INTERFACE_FROM_MODE(p_Dtsec->enetMode);
    enet_speed = (enum enet_speed) ENET_SPEED_FROM_MODE(p_Dtsec->enetMode);
    p_Dtsec->halfDuplex = !fullDuplex;

    err = fman_dtsec_adjust_link(p_Dtsec->p_MemMap, enet_interface, enet_speed, fullDuplex);

    if (err == -EINVAL)
        RETURN_ERROR(MAJOR, E_CONFLICT, ("Ethernet interface does not support Half Duplex mode"));

    return (t_Error)err;
}

/* .............................................................................. */

static t_Error DtsecRestartAutoneg(t_Handle h_Dtsec)
{
    t_Dtsec      *p_Dtsec = (t_Dtsec *)h_Dtsec;
    uint16_t     tmpReg16;

    SANITY_CHECK_RETURN_ERROR(p_Dtsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Dtsec->p_DtsecDriverParam, E_INVALID_STATE);

    DTSEC_MII_ReadPhyReg(p_Dtsec, p_Dtsec->tbi_phy_addr, 0, &tmpReg16);

    tmpReg16 &= ~( PHY_CR_SPEED0 | PHY_CR_SPEED1 );
    tmpReg16 |= (PHY_CR_ANE | PHY_CR_RESET_AN | PHY_CR_FULLDUPLEX | PHY_CR_SPEED1);

    DTSEC_MII_WritePhyReg(p_Dtsec, p_Dtsec->tbi_phy_addr, 0, tmpReg16);

    return E_OK;
}

/* .............................................................................. */

static t_Error DtsecGetId(t_Handle h_Dtsec, uint32_t *macId)
{
    t_Dtsec     *p_Dtsec = (t_Dtsec *)h_Dtsec;

    SANITY_CHECK_RETURN_ERROR(p_Dtsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Dtsec->p_DtsecDriverParam, E_INVALID_STATE);

    *macId = p_Dtsec->macId;

    return E_OK;
}

/* .............................................................................. */

static t_Error DtsecGetVersion(t_Handle h_Dtsec, uint32_t *macVersion)
{
    t_Dtsec     *p_Dtsec = (t_Dtsec *)h_Dtsec;

    SANITY_CHECK_RETURN_ERROR(p_Dtsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Dtsec->p_DtsecDriverParam, E_INVALID_STATE);

    *macVersion = fman_dtsec_get_revision(p_Dtsec->p_MemMap);

    return E_OK;
}

/* .............................................................................. */

static t_Error DtsecSetException(t_Handle h_Dtsec, e_FmMacExceptions exception, bool enable)
{
    t_Dtsec     *p_Dtsec = (t_Dtsec *)h_Dtsec;
    uint32_t    bitMask = 0;

    SANITY_CHECK_RETURN_ERROR(p_Dtsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(!p_Dtsec->p_DtsecDriverParam, E_INVALID_STATE);

    if (exception != e_FM_MAC_EX_1G_1588_TS_RX_ERR)
    {
        GET_EXCEPTION_FLAG(bitMask, exception);
        if (bitMask)
        {
            if (enable)
                p_Dtsec->exceptions |= bitMask;
            else
                p_Dtsec->exceptions &= ~bitMask;
        }
        else
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Undefined exception"));

        if (enable)
            fman_dtsec_enable_interrupt(p_Dtsec->p_MemMap, bitMask);
        else
            fman_dtsec_disable_interrupt(p_Dtsec->p_MemMap, bitMask);
    }
    else
    {
        if (!p_Dtsec->ptpTsuEnabled)
            RETURN_ERROR(MAJOR, E_INVALID_VALUE, ("Exception valid for 1588 only"));

        if (enable)
        {
            p_Dtsec->enTsuErrExeption = TRUE;
            fman_dtsec_enable_tmr_interrupt(p_Dtsec->p_MemMap);
        }
        else
        {
            p_Dtsec->enTsuErrExeption = FALSE;
            fman_dtsec_disable_tmr_interrupt(p_Dtsec->p_MemMap);
        }
    }

    return E_OK;
}


/*****************************************************************************/
/*                      dTSEC Init & Free API                                   */
/*****************************************************************************/

/* .............................................................................. */

static t_Error DtsecInit(t_Handle h_Dtsec)
{
    t_Dtsec             *p_Dtsec = (t_Dtsec *)h_Dtsec;
    struct dtsec_cfg    *p_DtsecDriverParam;
    t_Error             err;
    uint16_t            maxFrmLn;
    enum enet_interface enet_interface;
    enum enet_speed     enet_speed;
    t_EnetAddr          ethAddr;

    SANITY_CHECK_RETURN_ERROR(p_Dtsec, E_INVALID_HANDLE);
    SANITY_CHECK_RETURN_ERROR(p_Dtsec->p_DtsecDriverParam, E_INVALID_STATE);
    SANITY_CHECK_RETURN_ERROR(p_Dtsec->fmMacControllerDriver.h_Fm, E_INVALID_HANDLE);

    FM_GetRevision(p_Dtsec->fmMacControllerDriver.h_Fm, &p_Dtsec->fmMacControllerDriver.fmRevInfo);
    CHECK_INIT_PARAMETERS(p_Dtsec, CheckInitParameters);

    p_DtsecDriverParam  = p_Dtsec->p_DtsecDriverParam;
    p_Dtsec->halfDuplex = p_DtsecDriverParam->halfdup_on;

    enet_interface = (enum enet_interface)ENET_INTERFACE_FROM_MODE(p_Dtsec->enetMode);
    enet_speed = (enum enet_speed)ENET_SPEED_FROM_MODE(p_Dtsec->enetMode);
    MAKE_ENET_ADDR_FROM_UINT64(p_Dtsec->addr, ethAddr);

    err = (t_Error)fman_dtsec_init(p_Dtsec->p_MemMap,
                              p_DtsecDriverParam,
                              enet_interface,
                              enet_speed,
                              (uint8_t*)ethAddr,
                              p_Dtsec->fmMacControllerDriver.fmRevInfo.majorRev,
                              p_Dtsec->fmMacControllerDriver.fmRevInfo.minorRev,
                              p_Dtsec->exceptions);
    if (err)
    {
        FreeInitResources(p_Dtsec);
        RETURN_ERROR(MAJOR, err, ("This DTSEC version does not support the required i/f mode"));
    }

    if (ENET_INTERFACE_FROM_MODE(p_Dtsec->enetMode) == e_ENET_IF_SGMII)
    {
        uint16_t            tmpReg16;

        /* Configure the TBI PHY Control Register */
        tmpReg16 = PHY_TBICON_CLK_SEL | PHY_TBICON_SRESET;
        DTSEC_MII_WritePhyReg(p_Dtsec, (uint8_t)p_DtsecDriverParam->tbipa, 17, tmpReg16);

        tmpReg16 = PHY_TBICON_CLK_SEL;
        DTSEC_MII_WritePhyReg(p_Dtsec, (uint8_t)p_DtsecDriverParam->tbipa, 17, tmpReg16);

        tmpReg16 = (PHY_CR_PHY_RESET | PHY_CR_ANE | PHY_CR_FULLDUPLEX | PHY_CR_SPEED1);
        DTSEC_MII_WritePhyReg(p_Dtsec, (uint8_t)p_DtsecDriverParam->tbipa, 0, tmpReg16);

        if (p_Dtsec->enetMode & ENET_IF_SGMII_BASEX)
            tmpReg16 = PHY_TBIANA_1000X;
        else
            tmpReg16 = PHY_TBIANA_SGMII;
        DTSEC_MII_WritePhyReg(p_Dtsec, (uint8_t)p_DtsecDriverParam->tbipa, 4, tmpReg16);

        tmpReg16 = (PHY_CR_ANE | PHY_CR_RESET_AN | PHY_CR_FULLDUPLEX | PHY_CR_SPEED1);

        DTSEC_MII_WritePhyReg(p_Dtsec, (uint8_t)p_DtsecDriverParam->tbipa, 0, tmpReg16);
    }

    /* Max Frame Length */
    maxFrmLn = fman_dtsec_get_max_frame_len(p_Dtsec->p_MemMap);
    err = FmSetMacMaxFrame(p_Dtsec->fmMacControllerDriver.h_Fm, e_FM_MAC_1G,
            p_Dtsec->fmMacControllerDriver.macId, maxFrmLn);
    if (err)
        RETURN_ERROR(MINOR,err, NO_MSG);

    p_Dtsec->p_MulticastAddrHash = AllocHashTable(EXTENDED_HASH_TABLE_SIZE);
    if (!p_Dtsec->p_MulticastAddrHash) {
        FreeInitResources(p_Dtsec);
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("MC hash table is FAILED"));
    }

    p_Dtsec->p_UnicastAddrHash = AllocHashTable(HASH_TABLE_SIZE);
    if (!p_Dtsec->p_UnicastAddrHash)
    {
        FreeInitResources(p_Dtsec);
        RETURN_ERROR(MAJOR, E_NO_MEMORY, ("UC hash table is FAILED"));
    }

    /* register err intr handler for dtsec to FPM (err)*/
    FmRegisterIntr(p_Dtsec->fmMacControllerDriver.h_Fm,
                   e_FM_MOD_1G_MAC,
                   p_Dtsec->macId,
                   e_FM_INTR_TYPE_ERR,
                   DtsecIsr,
                   p_Dtsec);
    /* register 1588 intr handler for TMR to FPM (normal)*/
    FmRegisterIntr(p_Dtsec->fmMacControllerDriver.h_Fm,
                   e_FM_MOD_1G_MAC,
                   p_Dtsec->macId,
                   e_FM_INTR_TYPE_NORMAL,
                   Dtsec1588Isr,
                   p_Dtsec);
    /* register normal intr handler for dtsec to main interrupt controller. */
    if (p_Dtsec->mdioIrq != NO_IRQ)
    {
        XX_SetIntr(p_Dtsec->mdioIrq, DtsecMdioIsr, p_Dtsec);
        XX_EnableIntr(p_Dtsec->mdioIrq);
    }

    XX_Free(p_DtsecDriverParam);
    p_Dtsec->p_DtsecDriverParam = NULL;

    err = DtsecSetStatistics(h_Dtsec, e_FM_MAC_FULL_STATISTICS);
    if (err)
    {
        FreeInitResources(p_Dtsec);
        RETURN_ERROR(MAJOR, err, ("Undefined statistics level"));
    }

    return E_OK;
}

/* ........................................................................... */

static t_Error DtsecFree(t_Handle h_Dtsec)
{
    t_Dtsec      *p_Dtsec = (t_Dtsec *)h_Dtsec;

    SANITY_CHECK_RETURN_ERROR(p_Dtsec, E_INVALID_HANDLE);

    if (p_Dtsec->p_DtsecDriverParam)
    {
        /* Called after config */
        XX_Free(p_Dtsec->p_DtsecDriverParam);
        p_Dtsec->p_DtsecDriverParam = NULL;
    }
    else
        /* Called after init */
        FreeInitResources(p_Dtsec);

    XX_Free(p_Dtsec);

    return E_OK;
}

/* .............................................................................. */

static void InitFmMacControllerDriver(t_FmMacControllerDriver *p_FmMacControllerDriver)
{
    p_FmMacControllerDriver->f_FM_MAC_Init                      = DtsecInit;
    p_FmMacControllerDriver->f_FM_MAC_Free                      = DtsecFree;

    p_FmMacControllerDriver->f_FM_MAC_SetStatistics             = DtsecSetStatistics;
    p_FmMacControllerDriver->f_FM_MAC_ConfigLoopback            = DtsecConfigLoopback;
    p_FmMacControllerDriver->f_FM_MAC_ConfigMaxFrameLength      = DtsecConfigMaxFrameLength;

    p_FmMacControllerDriver->f_FM_MAC_ConfigWan                 = NULL; /* Not supported on dTSEC */

    p_FmMacControllerDriver->f_FM_MAC_ConfigPadAndCrc           = DtsecConfigPadAndCrc;
    p_FmMacControllerDriver->f_FM_MAC_ConfigHalfDuplex          = DtsecConfigHalfDuplex;
    p_FmMacControllerDriver->f_FM_MAC_ConfigLengthCheck         = DtsecConfigLengthCheck;
    p_FmMacControllerDriver->f_FM_MAC_ConfigTbiPhyAddr          = DtsecConfigTbiPhyAddr;
    p_FmMacControllerDriver->f_FM_MAC_ConfigException           = DtsecConfigException;
    p_FmMacControllerDriver->f_FM_MAC_ConfigResetOnInit         = NULL;

    p_FmMacControllerDriver->f_FM_MAC_Enable                    = DtsecEnable;
    p_FmMacControllerDriver->f_FM_MAC_Disable                   = DtsecDisable;
    p_FmMacControllerDriver->f_FM_MAC_Resume                    = NULL;

    p_FmMacControllerDriver->f_FM_MAC_SetException              = DtsecSetException;

    p_FmMacControllerDriver->f_FM_MAC_SetPromiscuous            = DtsecSetPromiscuous;
    p_FmMacControllerDriver->f_FM_MAC_AdjustLink                = DtsecAdjustLink;
    p_FmMacControllerDriver->f_FM_MAC_SetWakeOnLan              = DtsecSetWakeOnLan;
    p_FmMacControllerDriver->f_FM_MAC_RestartAutoneg            = DtsecRestartAutoneg;

    p_FmMacControllerDriver->f_FM_MAC_Enable1588TimeStamp       = DtsecEnable1588TimeStamp;
    p_FmMacControllerDriver->f_FM_MAC_Disable1588TimeStamp      = DtsecDisable1588TimeStamp;

    p_FmMacControllerDriver->f_FM_MAC_SetTxAutoPauseFrames      = DtsecTxMacPause;
    p_FmMacControllerDriver->f_FM_MAC_SetTxPauseFrames          = DtsecSetTxPauseFrames;
    p_FmMacControllerDriver->f_FM_MAC_SetRxIgnorePauseFrames    = DtsecRxIgnoreMacPause;

    p_FmMacControllerDriver->f_FM_MAC_ResetCounters             = DtsecResetCounters;
    p_FmMacControllerDriver->f_FM_MAC_GetStatistics             = DtsecGetStatistics;

    p_FmMacControllerDriver->f_FM_MAC_ModifyMacAddr             = DtsecModifyMacAddress;
    p_FmMacControllerDriver->f_FM_MAC_AddHashMacAddr            = DtsecAddHashMacAddress;
    p_FmMacControllerDriver->f_FM_MAC_RemoveHashMacAddr         = DtsecDelHashMacAddress;
    p_FmMacControllerDriver->f_FM_MAC_AddExactMatchMacAddr      = DtsecAddExactMatchMacAddress;
    p_FmMacControllerDriver->f_FM_MAC_RemovelExactMatchMacAddr  = DtsecDelExactMatchMacAddress;
    p_FmMacControllerDriver->f_FM_MAC_GetId                     = DtsecGetId;
    p_FmMacControllerDriver->f_FM_MAC_GetVersion                = DtsecGetVersion;
    p_FmMacControllerDriver->f_FM_MAC_GetMaxFrameLength         = DtsecGetMaxFrameLength;

    p_FmMacControllerDriver->f_FM_MAC_MII_WritePhyReg           = DTSEC_MII_WritePhyReg;
    p_FmMacControllerDriver->f_FM_MAC_MII_ReadPhyReg            = DTSEC_MII_ReadPhyReg;

}


/*****************************************************************************/
/*                      dTSEC Config Main Entry                             */
/*****************************************************************************/

/* .............................................................................. */

t_Handle  DTSEC_Config(t_FmMacParams *p_FmMacParam)
{
    t_Dtsec             *p_Dtsec;
    struct dtsec_cfg    *p_DtsecDriverParam;
    uintptr_t           baseAddr;

    SANITY_CHECK_RETURN_VALUE(p_FmMacParam, E_NULL_POINTER, NULL);

    baseAddr = p_FmMacParam->baseAddr;

    /* allocate memory for the UCC GETH data structure. */
    p_Dtsec = (t_Dtsec *)XX_Malloc(sizeof(t_Dtsec));
    if (!p_Dtsec)
    {
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("dTSEC driver structure"));
        return NULL;
    }
    memset(p_Dtsec, 0, sizeof(t_Dtsec));
    InitFmMacControllerDriver(&p_Dtsec->fmMacControllerDriver);

    /* allocate memory for the dTSEC driver parameters data structure. */
    p_DtsecDriverParam = (struct dtsec_cfg *) XX_Malloc(sizeof(struct dtsec_cfg));
    if (!p_DtsecDriverParam)
    {
        XX_Free(p_Dtsec);
        REPORT_ERROR(MAJOR, E_NO_MEMORY, ("dTSEC driver parameters"));
        return NULL;
    }
    memset(p_DtsecDriverParam, 0, sizeof(struct dtsec_cfg));

    /* Plant parameter structure pointer */
    p_Dtsec->p_DtsecDriverParam = p_DtsecDriverParam;

    fman_dtsec_defconfig(p_DtsecDriverParam);

    p_Dtsec->p_MemMap           = (struct dtsec_regs *)UINT_TO_PTR(baseAddr);
    p_Dtsec->p_MiiMemMap        = (struct dtsec_mii_reg *)UINT_TO_PTR(baseAddr + DTSEC_TO_MII_OFFSET);
    p_Dtsec->addr               = ENET_ADDR_TO_UINT64(p_FmMacParam->addr);
    p_Dtsec->enetMode           = p_FmMacParam->enetMode;
    p_Dtsec->macId              = p_FmMacParam->macId;
    p_Dtsec->exceptions         = DEFAULT_exceptions;
    p_Dtsec->mdioIrq            = p_FmMacParam->mdioIrq;
    p_Dtsec->f_Exception        = p_FmMacParam->f_Exception;
    p_Dtsec->f_Event            = p_FmMacParam->f_Event;
    p_Dtsec->h_App              = p_FmMacParam->h_App;
    p_Dtsec->ptpTsuEnabled      = p_Dtsec->p_DtsecDriverParam->ptp_tsu_en;
    p_Dtsec->enTsuErrExeption   = p_Dtsec->p_DtsecDriverParam->ptp_exception_en;
    p_Dtsec->tbi_phy_addr       = p_Dtsec->p_DtsecDriverParam->tbi_phy_addr;

    return p_Dtsec;
}
