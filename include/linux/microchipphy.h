/*
 * Copyright (C) 2015 Microchip Technology
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _MICROCHIPPHY_H
#define _MICROCHIPPHY_H

#define LAN88XX_INT_MASK			(0x19)
#define LAN88XX_INT_MASK_MDINTPIN_EN_		(0x8000)
#define LAN88XX_INT_MASK_SPEED_CHANGE_		(0x4000)
#define LAN88XX_INT_MASK_LINK_CHANGE_		(0x2000)
#define LAN88XX_INT_MASK_FDX_CHANGE_		(0x1000)
#define LAN88XX_INT_MASK_AUTONEG_ERR_		(0x0800)
#define LAN88XX_INT_MASK_AUTONEG_DONE_		(0x0400)
#define LAN88XX_INT_MASK_POE_DETECT_		(0x0200)
#define LAN88XX_INT_MASK_SYMBOL_ERR_		(0x0100)
#define LAN88XX_INT_MASK_FAST_LINK_FAIL_	(0x0080)
#define LAN88XX_INT_MASK_WOL_EVENT_		(0x0040)
#define LAN88XX_INT_MASK_EXTENDED_INT_		(0x0020)
#define LAN88XX_INT_MASK_RESERVED_		(0x0010)
#define LAN88XX_INT_MASK_FALSE_CARRIER_		(0x0008)
#define LAN88XX_INT_MASK_LINK_SPEED_DS_		(0x0004)
#define LAN88XX_INT_MASK_MASTER_SLAVE_DONE_	(0x0002)
#define LAN88XX_INT_MASK_RX__ER_		(0x0001)

#define LAN88XX_INT_STS				(0x1A)
#define LAN88XX_INT_STS_INT_ACTIVE_		(0x8000)
#define LAN88XX_INT_STS_SPEED_CHANGE_		(0x4000)
#define LAN88XX_INT_STS_LINK_CHANGE_		(0x2000)
#define LAN88XX_INT_STS_FDX_CHANGE_		(0x1000)
#define LAN88XX_INT_STS_AUTONEG_ERR_		(0x0800)
#define LAN88XX_INT_STS_AUTONEG_DONE_		(0x0400)
#define LAN88XX_INT_STS_POE_DETECT_		(0x0200)
#define LAN88XX_INT_STS_SYMBOL_ERR_		(0x0100)
#define LAN88XX_INT_STS_FAST_LINK_FAIL_		(0x0080)
#define LAN88XX_INT_STS_WOL_EVENT_		(0x0040)
#define LAN88XX_INT_STS_EXTENDED_INT_		(0x0020)
#define LAN88XX_INT_STS_RESERVED_		(0x0010)
#define LAN88XX_INT_STS_FALSE_CARRIER_		(0x0008)
#define LAN88XX_INT_STS_LINK_SPEED_DS_		(0x0004)
#define LAN88XX_INT_STS_MASTER_SLAVE_DONE_	(0x0002)
#define LAN88XX_INT_STS_RX_ER_			(0x0001)

#define LAN88XX_EXT_PAGE_ACCESS			(0x1F)
#define LAN88XX_EXT_PAGE_SPACE_0		(0x0000)
#define LAN88XX_EXT_PAGE_SPACE_1		(0x0001)
#define LAN88XX_EXT_PAGE_SPACE_2		(0x0002)

/* Extended Register Page 1 space */
#define LAN88XX_EXT_MODE_CTRL			(0x13)
#define LAN88XX_EXT_MODE_CTRL_MDIX_MASK_	(0x000C)
#define LAN88XX_EXT_MODE_CTRL_AUTO_MDIX_	(0x0000)
#define LAN88XX_EXT_MODE_CTRL_MDI_		(0x0008)
#define LAN88XX_EXT_MODE_CTRL_MDI_X_		(0x000C)

/* MMD 3 Registers */
#define	LAN88XX_MMD3_CHIP_ID			(32877)
#define	LAN88XX_MMD3_CHIP_REV			(32878)

/* DSP registers */
#define PHY_ARDENNES_MMD_DEV_3_PHY_CFG		(0x806A)
#define PHY_ARDENNES_MMD_DEV_3_PHY_CFG_ZD_DLY_EN_	(0x2000)
#define LAN88XX_EXT_PAGE_ACCESS_TR		(0x52B5)
#define LAN88XX_EXT_PAGE_TR_CR			16
#define LAN88XX_EXT_PAGE_TR_LOW_DATA		17
#define LAN88XX_EXT_PAGE_TR_HIGH_DATA		18

#endif /* _MICROCHIPPHY_H */
