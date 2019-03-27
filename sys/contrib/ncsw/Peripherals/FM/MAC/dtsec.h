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
 @File          dtsec.h

 @Description   FM dTSEC ...
*//***************************************************************************/
#ifndef __DTSEC_H
#define __DTSEC_H

#include "std_ext.h"
#include "error_ext.h"
#include "list_ext.h"
#include "enet_ext.h"

#include "dtsec_mii_acc.h"
#include "fm_mac.h"


#define DEFAULT_exceptions            \
    ((uint32_t)(DTSEC_IMASK_BREN    | \
                DTSEC_IMASK_RXCEN   | \
                DTSEC_IMASK_BTEN    | \
                DTSEC_IMASK_TXCEN   | \
                DTSEC_IMASK_TXEEN   | \
                DTSEC_IMASK_ABRTEN  | \
                DTSEC_IMASK_LCEN    | \
                DTSEC_IMASK_CRLEN   | \
                DTSEC_IMASK_XFUNEN  | \
                DTSEC_IMASK_IFERREN | \
                DTSEC_IMASK_MAGEN   | \
                DTSEC_IMASK_TDPEEN  | \
                DTSEC_IMASK_RDPEEN))

#define GET_EXCEPTION_FLAG(bitMask, exception)  switch (exception){ \
    case e_FM_MAC_EX_1G_BAB_RX:                                     \
        bitMask = DTSEC_IMASK_BREN; break;                          \
    case e_FM_MAC_EX_1G_RX_CTL:                                     \
        bitMask = DTSEC_IMASK_RXCEN; break;                         \
    case e_FM_MAC_EX_1G_GRATEFUL_TX_STP_COMPLET:                    \
        bitMask = DTSEC_IMASK_GTSCEN ; break;                       \
    case e_FM_MAC_EX_1G_BAB_TX:                                     \
        bitMask = DTSEC_IMASK_BTEN   ; break;                       \
    case e_FM_MAC_EX_1G_TX_CTL:                                     \
        bitMask = DTSEC_IMASK_TXCEN  ; break;                       \
    case e_FM_MAC_EX_1G_TX_ERR:                                     \
        bitMask = DTSEC_IMASK_TXEEN  ; break;                       \
    case e_FM_MAC_EX_1G_LATE_COL:                                   \
        bitMask = DTSEC_IMASK_LCEN   ; break;                       \
    case e_FM_MAC_EX_1G_COL_RET_LMT:                                \
        bitMask = DTSEC_IMASK_CRLEN  ; break;                       \
    case e_FM_MAC_EX_1G_TX_FIFO_UNDRN:                              \
        bitMask = DTSEC_IMASK_XFUNEN ; break;                       \
    case e_FM_MAC_EX_1G_MAG_PCKT:                                   \
        bitMask = DTSEC_IMASK_MAGEN ; break;                        \
    case e_FM_MAC_EX_1G_MII_MNG_RD_COMPLET:                         \
        bitMask = DTSEC_IMASK_MMRDEN; break;                        \
    case e_FM_MAC_EX_1G_MII_MNG_WR_COMPLET:                         \
        bitMask = DTSEC_IMASK_MMWREN  ; break;                      \
    case e_FM_MAC_EX_1G_GRATEFUL_RX_STP_COMPLET:                    \
        bitMask = DTSEC_IMASK_GRSCEN; break;                        \
    case e_FM_MAC_EX_1G_TX_DATA_ERR:                                \
        bitMask = DTSEC_IMASK_TDPEEN; break;                        \
    case e_FM_MAC_EX_1G_RX_MIB_CNT_OVFL:                            \
        bitMask = DTSEC_IMASK_MSROEN ; break;                       \
    default: bitMask = 0;break;}


#define MAX_PACKET_ALIGNMENT        31
#define MAX_INTER_PACKET_GAP        0x7f
#define MAX_INTER_PALTERNATE_BEB    0x0f
#define MAX_RETRANSMISSION          0x0f
#define MAX_COLLISION_WINDOW        0x03ff


/********************* From mac ext ******************************************/
typedef  uint32_t t_ErrorDisable;

#define ERROR_DISABLE_TRANSMIT              0x00400000
#define ERROR_DISABLE_LATE_COLLISION        0x00040000
#define ERROR_DISABLE_COLLISION_RETRY_LIMIT 0x00020000
#define ERROR_DISABLE_TxFIFO_UNDERRUN       0x00010000
#define ERROR_DISABLE_TxABORT               0x00008000
#define ERROR_DISABLE_INTERFACE             0x00004000
#define ERROR_DISABLE_TxDATA_PARITY         0x00000002
#define ERROR_DISABLE_RxDATA_PARITY         0x00000001

/*****************************************************************************/
#define DTSEC_NUM_OF_PADDRS             15  /* number of pattern match registers (entries) */

#define GROUP_ADDRESS                   0x0000010000000000LL /* Group address bit indication */

#define HASH_TABLE_SIZE                 256 /* Hash table size (= 32 bits * 8 regs) */

#define HASH_TABLE_SIZE                 256 /* Hash table size (32 bits * 8 regs) */
#define EXTENDED_HASH_TABLE_SIZE        512 /* Extended Hash table size (32 bits * 16 regs) */

#define DTSEC_TO_MII_OFFSET             0x1000  /* number of pattern match registers (entries) */

#define MAX_PHYS                    32 /* maximum number of phys */

#define     VAL32BIT    0x100000000LL
#define     VAL22BIT    0x00400000
#define     VAL16BIT    0x00010000
#define     VAL12BIT    0x00001000

/* CAR1/2 bits */
#define CAR1_TR64   0x80000000
#define CAR1_TR127  0x40000000
#define CAR1_TR255  0x20000000
#define CAR1_TR511  0x10000000
#define CAR1_TRK1   0x08000000
#define CAR1_TRMAX  0x04000000
#define CAR1_TRMGV  0x02000000

#define CAR1_RBYT   0x00010000
#define CAR1_RPKT   0x00008000
#define CAR1_RMCA   0x00002000
#define CAR1_RBCA   0x00001000
#define CAR1_RXPF   0x00000400
#define CAR1_RALN   0x00000100
#define CAR1_RFLR   0x00000080
#define CAR1_RCDE   0x00000040
#define CAR1_RCSE   0x00000020
#define CAR1_RUND   0x00000010
#define CAR1_ROVR   0x00000008
#define CAR1_RFRG   0x00000004
#define CAR1_RJBR   0x00000002
#define CAR1_RDRP   0x00000001

#define CAR2_TFCS   0x00040000
#define CAR2_TBYT   0x00002000
#define CAR2_TPKT   0x00001000
#define CAR2_TMCA   0x00000800
#define CAR2_TBCA   0x00000400
#define CAR2_TXPF   0x00000200
#define CAR2_TDRP   0x00000001

typedef struct t_InternalStatistics
{
    uint64_t    tr64;
    uint64_t    tr127;
    uint64_t    tr255;
    uint64_t    tr511;
    uint64_t    tr1k;
    uint64_t    trmax;
    uint64_t    trmgv;
    uint64_t    rfrg;
    uint64_t    rjbr;
    uint64_t    rdrp;
    uint64_t    raln;
    uint64_t    rund;
    uint64_t    rovr;
    uint64_t    rxpf;
    uint64_t    txpf;
    uint64_t    rbyt;
    uint64_t    rpkt;
    uint64_t    rmca;
    uint64_t    rbca;
    uint64_t    rflr;
    uint64_t    rcde;
    uint64_t    rcse;
    uint64_t    tbyt;
    uint64_t    tpkt;
    uint64_t    tmca;
    uint64_t    tbca;
    uint64_t    tdrp;
    uint64_t    tfcs;
} t_InternalStatistics;

typedef struct {
    t_FmMacControllerDriver     fmMacControllerDriver;
    t_Handle                    h_App;            /**< Handle to the upper layer application              */
    struct dtsec_regs           *p_MemMap;        /**< pointer to dTSEC memory mapped registers.          */
    struct dtsec_mii_reg        *p_MiiMemMap;     /**< pointer to dTSEC MII memory mapped registers.          */
    uint64_t                    addr;             /**< MAC address of device;                             */
    e_EnetMode                  enetMode;         /**< Ethernet physical interface  */
    t_FmMacExceptionCallback    *f_Exception;
    int                         mdioIrq;
    t_FmMacExceptionCallback    *f_Event;
    bool                        indAddrRegUsed[DTSEC_NUM_OF_PADDRS]; /**< Whether a particular individual address recognition register is being used */
    uint64_t                    paddr[DTSEC_NUM_OF_PADDRS]; /**< MAC address for particular individual address recognition register */
    uint8_t                     numOfIndAddrInRegs; /**< Number of individual addresses in registers for this station. */
    bool                        halfDuplex;
    t_InternalStatistics        internalStatistics;
    t_EthHash                   *p_MulticastAddrHash;      /* pointer to driver's global address hash table  */
    t_EthHash                   *p_UnicastAddrHash;    /* pointer to driver's individual address hash table  */
    uint8_t                     macId;
    uint8_t                     tbi_phy_addr;
    uint32_t                    exceptions;
    bool                        ptpTsuEnabled;
    bool                        enTsuErrExeption;
    e_FmMacStatisticsLevel      statisticsLevel;
    struct dtsec_cfg            *p_DtsecDriverParam;
} t_Dtsec;


#endif /* __DTSEC_H */
