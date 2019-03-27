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
 @File          memac.h

 @Description   FM Multirate Ethernet MAC (mEMAC)
*//***************************************************************************/
#ifndef __MEMAC_H
#define __MEMAC_H

#include "std_ext.h"
#include "error_ext.h"
#include "list_ext.h"

#include "fsl_fman_memac_mii_acc.h"
#include "fm_mac.h"
#include "fsl_fman_memac.h"


#define MEMAC_default_exceptions    \
        ((uint32_t)(MEMAC_IMASK_TSECC_ER | MEMAC_IMASK_TECC_ER | MEMAC_IMASK_RECC_ER | MEMAC_IMASK_MGI))

#define GET_EXCEPTION_FLAG(bitMask, exception)       switch (exception){    \
    case e_FM_MAC_EX_10G_1TX_ECC_ER:                                        \
        bitMask = MEMAC_IMASK_TECC_ER; break;                               \
    case e_FM_MAC_EX_10G_RX_ECC_ER:                                         \
        bitMask = MEMAC_IMASK_RECC_ER; break;                               \
    case e_FM_MAC_EX_TS_FIFO_ECC_ERR:                                       \
        bitMask = MEMAC_IMASK_TSECC_ER; break;                              \
    case e_FM_MAC_EX_MAGIC_PACKET_INDICATION:                               \
        bitMask = MEMAC_IMASK_MGI; break;                                   \
    default: bitMask = 0;break;}


typedef struct
{
    t_FmMacControllerDriver     fmMacControllerDriver;               /**< Upper Mac control block */
    t_Handle                    h_App;                               /**< Handle to the upper layer application  */
    struct memac_regs           *p_MemMap;                           /**< Pointer to MAC memory mapped registers */
    struct memac_mii_access_mem_map *p_MiiMemMap;                        /**< Pointer to MII memory mapped registers */
    uint64_t                    addr;                                /**< MAC address of device */
    e_EnetMode                  enetMode;                            /**< Ethernet physical interface  */
    t_FmMacExceptionCallback    *f_Exception;
    int                         mdioIrq;
    t_FmMacExceptionCallback    *f_Event;
    bool                        indAddrRegUsed[MEMAC_NUM_OF_PADDRS]; /**< Whether a particular individual address recognition register is being used */
    uint64_t                    paddr[MEMAC_NUM_OF_PADDRS];          /**< MAC address for particular individual address recognition register */
    uint8_t                     numOfIndAddrInRegs;                  /**< Number of individual addresses in registers for this station. */
    t_EthHash                   *p_MulticastAddrHash;                /**< Pointer to driver's global address hash table  */
    t_EthHash                   *p_UnicastAddrHash;                  /**< Pointer to driver's individual address hash table  */
    bool                        debugMode;
    uint8_t                     macId;
    uint32_t                    exceptions;
    struct memac_cfg            *p_MemacDriverParam;
} t_Memac;


/* Internal PHY access */
#define PHY_MDIO_ADDR               0

/* Internal PHY Registers - SGMII */
#define PHY_SGMII_CR_PHY_RESET          0x8000
#define PHY_SGMII_CR_RESET_AN           0x0200
#define PHY_SGMII_CR_DEF_VAL            0x1140
#define PHY_SGMII_DEV_ABILITY_SGMII     0x4001
#define PHY_SGMII_DEV_ABILITY_1000X     0x01A0
#define PHY_SGMII_IF_SPEED_GIGABIT	0x0008
#define PHY_SGMII_IF_MODE_AN            0x0002
#define PHY_SGMII_IF_MODE_SGMII         0x0001
#define PHY_SGMII_IF_MODE_1000X         0x0000


#define MEMAC_TO_MII_OFFSET         0x030       /* Offset from the MEM map to the MDIO mem map */

t_Error MEMAC_MII_WritePhyReg(t_Handle h_Memac, uint8_t phyAddr, uint8_t reg, uint16_t data);
t_Error MEMAC_MII_ReadPhyReg(t_Handle h_Memac,  uint8_t phyAddr, uint8_t reg, uint16_t *p_Data);


#endif /* __MEMAC_H */
