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
 @File          tgec.h

 @Description   FM 10G MAC ...
*//***************************************************************************/
#ifndef __TGEC_H
#define __TGEC_H

#include "std_ext.h"
#include "error_ext.h"
#include "list_ext.h"
#include "enet_ext.h"

#include "tgec_mii_acc.h"
#include "fm_mac.h"


#define DEFAULT_exceptions                        \
    ((uint32_t)(TGEC_IMASK_MDIO_SCAN_EVENT     |  \
                TGEC_IMASK_REM_FAULT           |  \
                TGEC_IMASK_LOC_FAULT           |  \
                TGEC_IMASK_TX_ECC_ER           |  \
                TGEC_IMASK_TX_FIFO_UNFL        |  \
                TGEC_IMASK_TX_FIFO_OVFL        |  \
                TGEC_IMASK_TX_ER               |  \
                TGEC_IMASK_RX_FIFO_OVFL        |  \
                TGEC_IMASK_RX_ECC_ER           |  \
                TGEC_IMASK_RX_JAB_FRM          |  \
                TGEC_IMASK_RX_OVRSZ_FRM        |  \
                TGEC_IMASK_RX_RUNT_FRM         |  \
                TGEC_IMASK_RX_FRAG_FRM         |  \
                TGEC_IMASK_RX_CRC_ER           |  \
                TGEC_IMASK_RX_ALIGN_ER))

#define GET_EXCEPTION_FLAG(bitMask, exception)      switch (exception){ \
    case e_FM_MAC_EX_10G_MDIO_SCAN_EVENTMDIO:                           \
        bitMask = TGEC_IMASK_MDIO_SCAN_EVENT    ; break;                \
    case e_FM_MAC_EX_10G_MDIO_CMD_CMPL:                                 \
        bitMask = TGEC_IMASK_MDIO_CMD_CMPL      ; break;                \
    case e_FM_MAC_EX_10G_REM_FAULT:                                     \
        bitMask = TGEC_IMASK_REM_FAULT          ; break;                \
    case e_FM_MAC_EX_10G_LOC_FAULT:                                     \
        bitMask = TGEC_IMASK_LOC_FAULT          ; break;                \
    case e_FM_MAC_EX_10G_1TX_ECC_ER:                                    \
        bitMask = TGEC_IMASK_TX_ECC_ER         ; break;                 \
    case e_FM_MAC_EX_10G_TX_FIFO_UNFL:                                  \
        bitMask = TGEC_IMASK_TX_FIFO_UNFL       ; break;                \
    case e_FM_MAC_EX_10G_TX_FIFO_OVFL:                                  \
        bitMask = TGEC_IMASK_TX_FIFO_OVFL       ; break;                \
    case e_FM_MAC_EX_10G_TX_ER:                                         \
        bitMask = TGEC_IMASK_TX_ER              ; break;                \
    case e_FM_MAC_EX_10G_RX_FIFO_OVFL:                                  \
        bitMask = TGEC_IMASK_RX_FIFO_OVFL       ; break;                \
    case e_FM_MAC_EX_10G_RX_ECC_ER:                                     \
        bitMask = TGEC_IMASK_RX_ECC_ER          ; break;                \
    case e_FM_MAC_EX_10G_RX_JAB_FRM:                                    \
        bitMask = TGEC_IMASK_RX_JAB_FRM         ; break;                \
    case e_FM_MAC_EX_10G_RX_OVRSZ_FRM:                                  \
        bitMask = TGEC_IMASK_RX_OVRSZ_FRM       ; break;                \
    case e_FM_MAC_EX_10G_RX_RUNT_FRM:                                   \
        bitMask = TGEC_IMASK_RX_RUNT_FRM        ; break;                \
    case e_FM_MAC_EX_10G_RX_FRAG_FRM:                                   \
        bitMask = TGEC_IMASK_RX_FRAG_FRM        ; break;                \
    case e_FM_MAC_EX_10G_RX_LEN_ER:                                     \
        bitMask = TGEC_IMASK_RX_LEN_ER          ; break;                \
    case e_FM_MAC_EX_10G_RX_CRC_ER:                                     \
        bitMask = TGEC_IMASK_RX_CRC_ER          ; break;                \
    case e_FM_MAC_EX_10G_RX_ALIGN_ER:                                   \
        bitMask = TGEC_IMASK_RX_ALIGN_ER        ; break;                \
    default: bitMask = 0;break;}

#define MAX_PACKET_ALIGNMENT        31
#define MAX_INTER_PACKET_GAP        0x7f
#define MAX_INTER_PALTERNATE_BEB    0x0f
#define MAX_RETRANSMISSION          0x0f
#define MAX_COLLISION_WINDOW        0x03ff

#define TGEC_NUM_OF_PADDRS          1                   /* number of pattern match registers (entries) */

#define GROUP_ADDRESS               0x0000010000000000LL /* Group address bit indication */

#define HASH_TABLE_SIZE             512                 /* Hash table size (= 32 bits * 8 regs) */

#define TGEC_TO_MII_OFFSET          0x1030              /* Offset from the MEM map to the MDIO mem map */

/* 10-gigabit Ethernet MAC Controller ID (10GEC_ID) */
#define TGEC_ID_ID                  0xffff0000
#define TGEC_ID_MAC_VERSION         0x0000FF00
#define TGEC_ID_MAC_REV             0x000000ff


typedef struct {
    t_FmMacControllerDriver     fmMacControllerDriver;              /**< Upper Mac control block */
    t_Handle                    h_App;                              /**< Handle to the upper layer application  */
    struct tgec_regs            *p_MemMap;                          /**< pointer to 10G memory mapped registers. */
    t_TgecMiiAccessMemMap       *p_MiiMemMap;                       /**< pointer to MII memory mapped registers.          */
    uint64_t                    addr;                               /**< MAC address of device; */
    e_EnetMode                  enetMode;                           /**< Ethernet physical interface  */
    t_FmMacExceptionCallback    *f_Exception;
    int                         mdioIrq;
    t_FmMacExceptionCallback    *f_Event;
    bool                        indAddrRegUsed[TGEC_NUM_OF_PADDRS]; /**< Whether a particular individual address recognition register is being used */
    uint64_t                    paddr[TGEC_NUM_OF_PADDRS];          /**< MAC address for particular individual address recognition register */
    uint8_t                     numOfIndAddrInRegs;                 /**< Number of individual addresses in registers for this station. */
    t_EthHash                   *p_MulticastAddrHash;               /**< pointer to driver's global address hash table  */
    t_EthHash                   *p_UnicastAddrHash;                 /**< pointer to driver's individual address hash table  */
    bool                        debugMode;
    uint8_t                     macId;
    uint32_t                    exceptions;
    struct tgec_cfg             *p_TgecDriverParam;
} t_Tgec;


t_Error TGEC_MII_WritePhyReg(t_Handle h_Tgec, uint8_t phyAddr, uint8_t reg, uint16_t data);
t_Error TGEC_MII_ReadPhyReg(t_Handle h_Tgec,  uint8_t phyAddr, uint8_t reg, uint16_t *p_Data);


#endif /* __TGEC_H */
