/*
 * TC956x XPCS Header
 *
 * tc956x_xpcs.h
 *
 * Copyright (C) 2021 Toshiba Electronic Devices & Storage Corporation
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*! History:
 *  05 Nov 2020 : Initial version
 *  VERSION     : 00-01
 *
 *  15 Mar 2021 : Base lined
 *  VERSION     : 01-00
 *
 *  05 Jul 2021 : 1. Used Systick handler instead of Driver kernel timer to process transmitted Tx descriptors.
 *                2. XFI interface support and module parameters for selection of Port0 and Port1 interface
 *  VERSION     : 01-00-01
 *  15 Jul 2021 : 1. USXGMII/XFI/SGMII/RGMII interface supported without module parameter
 *  VERSION     : 01-00-02
 */

#ifndef __TC956X_XPCS_H__
#define __TC956X_XPCS_H__
#include "common.h"

#ifdef TC956X
#define XPCS_XGMAC_OFFSET	0x3A00
#endif

/*XPCS registers*/
#define XGMAC_SR_MII_CTRL				0x7C0000
#define XGMAC_VR_MII_AN_CTRL			0x7e0004
#define XGMAC_VR_MII_DIG_CTRL1			0x7e0000
#define XGMAC_SR_XS_PCS_CTRL2			0xC001C
#define XGMAC_VR_XS_PCS_DIG_CTRL1		0xe0000
#define XGMAC_VR_XS_PCS_KR_CTRL			0xe001c
#define XGMAC_VR_MII_AN_INTR_STS		0x7e0008


/*XPCS Register value*/
#define XGMAC_PCS_MODE_MASK				0xFFFFFFF9
#define XGMAC_SGMII_MODE				0x00000004
#define XGMAC_TX_CFIG_INTR_EN_MASK		0xFFFFFFF6/*Mask TX_CONFIG & MII_AN_INTR_EN*/
#define XGMAC_MII_AN_INTR_EN			0x00000001/*MII_AN_INTR_EN*/
#define XGMAC_MAC_AUTO_SW_EN			0x00000200/*MAC_AUTO_SW*/
#define XGMAC_AN_37_ENABLE				0x00001000/*AN_EN*/
#define XGMAC_PCS_TYPE_SEL				0xFFFFFFF0/*PCS_TYPE_SEL: 0x0000*/
#define XGMAC_USXG_EN					0x00000200/*USXG_EN enable*/
#define XGMAC_USXG_MODE					0x00001c00/*USXG_MODE: 0x000*/
#define XGMAC_VR_RST					0x00008000/*set VR_RST*/
#define XGMAC_USXG_AN_STS_SPEED_MASK	0x00001c00/*USXGMII autonegotiated speed*/
#define XGMAC_USXG_AN_STS_DUPLEX_MASK	0x00002000/*USXGMII autonegtiated duplex*/
#define XGMAC_USXG_AN_STS_LINK_MASK		0x00004000/*USXGMII link status*/
#define XGMAC_SGM_STS_LINK_MASK			0x00000010/*SGMII link status*/
#define XGMAC_SGM_STS_DUPLEX			0x00000002/*SGMII autonegotiated duplex*/
#define XGMAC_SGM_STS_SPEED_MASK		0x0000000c/*SGMII autonegotiated speed*/
#define XGMAC_SOFT_RST					0x00008000/*SOFT RST*/
#define XGMAC_C37_AN_COMPL				0x00000001/*C37 Autoneg complete*/
#define XGMAC_SR_MII_CTRL_SPEED			0x00002060/* SR_MII_CTRL Reg SPEED SS13, SS6, SS5 */
#define XGMAC_SR_MII_CTRL_SPEED_10G		0x00002040/* SR_MII_CTRL SPEED: 10G */
#define XGMAC_SR_MII_CTRL_SPEED_5G		0x00002020/* SR_MII_CTRL SPEED: 5G */
#define XGMAC_SR_MII_CTRL_SPEED_2_5G	0x00000020/* SR_MII_CTRL SPEED: 5G */
#define XGMAC_USRA_RST					0x400/* USRA_RST */



#define XPCS_REG_BASE_ADDR				10
#define XPCS_REG_OFFSET					0x0003FF
#define XPCS_IND_ACCESS					0x3FC
#define XPCS_SS_SGMII_1G				0x40
#define XPCS_SS_SGMII_100M				0x2000
#define XPCS_SS_SGMII_10M				0x0

u32 tc956x_xpcs_read(void __iomem *xpcsaddr, u32 pcs_reg_num);
u32 tc956x_xpcs_write(void __iomem *xpcsaddr, u32 pcs_reg_num, u32 value);
void tc956x_xpcs_ctrl_ane(struct tc956xmac_priv *priv, bool ane);
int tc956x_xpcs_init(struct tc956xmac_priv *priv, void __iomem *xpcsaddr);

#endif /* __TC956X_XPCS_H__ */
