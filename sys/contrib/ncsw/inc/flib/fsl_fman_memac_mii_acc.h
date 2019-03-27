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

#ifndef __FSL_FMAN_MEMAC_MII_ACC_H
#define __FSL_FMAN_MEMAC_MII_ACC_H

#include "common/general.h"
#include "fsl_enet.h"
/* MII Management Registers */
#define MDIO_CFG_CLK_DIV_MASK       0x0080ff80
#define MDIO_CFG_CLK_DIV_SHIFT      7
#define MDIO_CFG_HOLD_MASK          0x0000001c
#define MDIO_CFG_ENC45              0x00000040
#define MDIO_CFG_READ_ERR           0x00000002
#define MDIO_CFG_BSY                0x00000001

#define MDIO_CTL_PHY_ADDR_SHIFT     5
#define MDIO_CTL_READ               0x00008000

#define MDIO_DATA_BSY               0x80000000

/*MEMAC Internal PHY Registers - SGMII */
#define PHY_SGMII_CR_PHY_RESET          0x8000
#define PHY_SGMII_CR_RESET_AN           0x0200
#define PHY_SGMII_CR_DEF_VAL            0x1140
#define PHY_SGMII_DEV_ABILITY_SGMII     0x4001
#define PHY_SGMII_DEV_ABILITY_1000X     0x01A0
#define PHY_SGMII_IF_MODE_AN            0x0002
#define PHY_SGMII_IF_MODE_SGMII         0x0001
#define PHY_SGMII_IF_MODE_1000X         0x0000

/*----------------------------------------------------*/
/* MII Configuration Control Memory Map Registers     */
/*----------------------------------------------------*/
struct memac_mii_access_mem_map {
	uint32_t   mdio_cfg;       /* 0x030  */
	uint32_t   mdio_ctrl;      /* 0x034  */
	uint32_t   mdio_data;      /* 0x038  */
	uint32_t   mdio_addr;      /* 0x03c  */
};

int fman_memac_mii_read_phy_reg(struct memac_mii_access_mem_map *mii_regs,
	uint8_t phy_addr, uint8_t reg, uint16_t *data,
	enum enet_speed enet_speed);
int fman_memac_mii_write_phy_reg(struct memac_mii_access_mem_map *mii_regs,
	uint8_t phy_addr, uint8_t reg, uint16_t data,
	enum enet_speed enet_speed);

#endif /* __MAC_API_MEMAC_MII_ACC_H */
