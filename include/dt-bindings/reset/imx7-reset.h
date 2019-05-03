/*
 * Copyright (C) 2017 Impinj, Inc.
 *
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef DT_BINDING_RESET_IMX7_H
#define DT_BINDING_RESET_IMX7_H

#define IMX7_RESET_A7_CORE_POR_RESET0	0
#define IMX7_RESET_A7_CORE_POR_RESET1	1
#define IMX7_RESET_A7_CORE_RESET0	2
#define IMX7_RESET_A7_CORE_RESET1	3
#define IMX7_RESET_A7_DBG_RESET0	4
#define IMX7_RESET_A7_DBG_RESET1	5
#define IMX7_RESET_A7_ETM_RESET0	6
#define IMX7_RESET_A7_ETM_RESET1	7
#define IMX7_RESET_A7_SOC_DBG_RESET	8
#define IMX7_RESET_A7_L2RESET		9
#define IMX7_RESET_SW_M4C_RST		10
#define IMX7_RESET_SW_M4P_RST		11
#define IMX7_RESET_EIM_RST		12
#define IMX7_RESET_HSICPHY_PORT_RST	13
#define IMX7_RESET_USBPHY1_POR		14
#define IMX7_RESET_USBPHY1_PORT_RST	15
#define IMX7_RESET_USBPHY2_POR		16
#define IMX7_RESET_USBPHY2_PORT_RST	17
#define IMX7_RESET_MIPI_PHY_MRST	18
#define IMX7_RESET_MIPI_PHY_SRST	19

/*
 * IMX7_RESET_PCIEPHY is a logical reset line combining PCIEPHY_BTN
 * and PCIEPHY_G_RST
 */
#define IMX7_RESET_PCIEPHY		20
#define IMX7_RESET_PCIEPHY_PERST	21

/*
 * IMX7_RESET_PCIE_CTRL_APPS_EN is not strictly a reset line, but it
 * can be used to inhibit PCIe LTTSM, so, in a way, it can be thoguht
 * of as one
 */
#define IMX7_RESET_PCIE_CTRL_APPS_EN	22
#define IMX7_RESET_DDRC_PRST		23
#define IMX7_RESET_DDRC_CORE_RST	24

#define IMX7_RESET_PCIE_CTRL_APPS_TURNOFF 25

#define IMX7_RESET_NUM			26

#endif

