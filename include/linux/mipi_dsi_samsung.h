/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __INCLUDE_MIPI_DSI_SAMSUNG_H
#define __INCLUDE_MIPI_DSI_SAMSUNG_H

#define MIPI_DSI_VERSION		(0x000)
#define MIPI_DSI_STATUS			(0x004)
#define MIPI_DSI_RGB_STATUS		(0x008)
#define MIPI_DSI_SWRST			(0x00c)
#define MIPI_DSI_CLKCTRL		(0x010)
#define MIPI_DSI_TIMEOUT		(0x014)
#define MIPI_DSI_CONFIG			(0x018)
#define MIPI_DSI_ESCMODE		(0x01c)
#define MIPI_DSI_MDRESOL		(0x020)
#define MIPI_DSI_MVPORCH		(0x024)
#define MIPI_DSI_MHPORCH		(0x028)
#define MIPI_DSI_MSYNC			(0x02c)
#define MIPI_DSI_SDRESOL		(0x030)
#define MIPI_DSI_INTSRC			(0x034)
#define MIPI_DSI_INTMSK			(0x038)
#define MIPI_DSI_PKTHDR			(0x03c)
#define MIPI_DSI_PAYLOAD		(0x040)
#define MIPI_DSI_RXFIFO			(0x044)
#define MIPI_DSI_FIFOTHLD		(0x048)
#define MIPI_DSI_FIFOCTRL		(0x04c)
#define MIPI_DSI_MEMACCHR		(0x050)
#define MIPI_DSI_MULTI_PKT		(0x078)
#define MIPI_DSI_PLLCTRL_1G		(0x090)
#define MIPI_DSI_PLLCTRL		(0x094)
#define MIPI_DSI_PLLCTRL1		(0x098)
#define MIPI_DSI_PLLCTRL2		(0x09c)
#define MIPI_DSI_PLLTMR			(0x0a0)
#define MIPI_DSI_PHYCTRL_B1		(0x0a4)
#define MIPI_DSI_PHYCTRL_B2		(0x0a8)
#define MIPI_DSI_PHYCTRL_M1		(0x0a8)
#define MIPI_DSI_PHYCTRL_M2		(0x0ac)
#define MIPI_DSI_PHYTIMING		(0x0b4)
#define MIPI_DSI_PHYTIMING1		(0x0b8)
#define MIPI_DSI_PHYTIMING2		(0x0bc)

#define MIPI_DSI_SWRST_SWRST		(0x1 << 0)
#define MIPI_DSI_SWRST_FUNCRST		(0x1 << 16)
#define MIPI_DSI_MAIN_HRESOL(x)		(((x) & 0x7ff) << 0)
#define MIPI_DSI_MAIN_VRESOL(x)		(((x) & 0x7ff) << 16)
#define MIPI_DSI_MAIN_STANDBY(x)	(((x) & 0x1) << 31)
#define MIPI_DSI_MAIN_VBP(x)		(((x) & 0x7ff) << 0)
#define MIPI_DSI_STABLE_VFP(x)		(((x) & 0x7ff) << 16)
#define MIPI_DSI_CMDALLOW(x)		(((x) & 0xf) << 28)
#define MIPI_DSI_MAIN_HBP(x)		(((x) & 0xffff) << 0)
#define MIPI_DSI_MAIN_HFP(x)		(((x) & 0xffff) << 16)
#define MIPI_DSI_MAIN_VSA(x)		(((x) & 0x3ff) << 22)
#define MIPI_DSI_MAIN_HSA(x)		(((x) & 0xffff) << 0)

#define MIPI_DSI_LANE_EN(x)		(((x) & 0x1f) << 0)
#define MIPI_DSI_NUM_OF_DATALANE(x)	(((x) & 0x3) << 5)
#define MIPI_DSI_SUB_PIX_FORMAT(x)	(((x) & 0x7) << 8)
#define MIPI_DSI_MAIN_PIX_FORMAT(x)	(((x) & 0x7) << 12)
#define MIPI_DSI_SUB_VC(x)		(((x) & 0x3) << 16)
#define MIPI_DSI_MAIN_VC(x)		(((x) & 0x3) << 18)
#define MIPI_DSI_HSA_DISABLE_MODE(x)	(((x) & 0x1) << 20)
#define MIPI_DSI_HBP_DISABLE_MODE(x)	(((x) & 0x1) << 21)
#define MIPI_DSI_HFP_DISABLE_MODE(x)	(((x) & 0x1) << 22)
#define MIPI_DSI_HSE_DISABLE_MODE(x)	(((x) & 0x1) << 23)
#define MIPI_DSI_AUTO_MODE(x)		(((x) & 0x1) << 24)
#define MIPI_DSI_VIDEO_MODE(x)		(((x) & 0x1) << 25)
#define MIPI_DSI_BURST_MODE(x)		(((x) & 0x1) << 26)
#define MIPI_DSI_SYNC_IN_FORM(x)	(((x) & 0x1) << 27)
#define MIPI_DSI_EOT_R03(x)		(((x) & 0x1) << 28)
#define MIPI_DSI_MFLUSH_VS(x)		(((x) & 0x1) << 29)

#define MIPI_DSI_DP_DN_SWAP_DATA	(0x1 << 24)
#define MIPI_DSI_PLL_EN(x)		(((x) & 0x1) << 23)
#define MIPI_DSI_PMS(x)			(((x) & 0x7ffff) << 1)

#define MIPI_DSI_TX_REQUEST_HSCLK(x)	(((x) & 0x1) << 31)
#define MIPI_DSI_DPHY_SEL(x)		(((x) & 0x1) << 29)
#define MIPI_DSI_ESC_CLK_EN(x)		(((x) & 0x1) << 28)
#define MIPI_DSI_PLL_BYPASS(x)		(((x) & 0x1) << 27)
#define MIPI_DSI_BYTE_CLK_SRC(x)	(((x) & 0x3) << 25)
#define MIPI_DSI_BYTE_CLK_EN(x)		(((x) & 0x1) << 24)
#define MIPI_DSI_LANE_ESC_CLK_EN(x)	(((x) & 0x1f) << 19)

#define MIPI_DSI_FORCE_STOP_STATE(x)	(((x) & 0x1) << 20)

#define MIPI_DSI_M_TLPXCTL(x)		(((x) & 0xff) << 8)
#define MIPI_DSI_M_THSEXITCTL(x)	(((x) & 0xff) << 0)

#define MIPI_DSI_M_TCLKPRPRCTL(x)	(((x) & 0xff) << 24)
#define MIPI_DSI_M_TCLKZEROCTL(x)	(((x) & 0xff) << 16)
#define MIPI_DSI_M_TCLKPOSTCTL(x)	(((x) & 0xff) << 8)
#define MIPI_DSI_M_TCLKTRAILCTL(x)	(((x) & 0xff) << 0)

#define MIPI_DSI_M_THSPRPRCTL(x)	(((x) & 0xff) << 16)
#define MIPI_DSI_M_THSZEROCTL(x)	(((x) & 0xff) << 8)
#define MIPI_DSI_M_THSTRAILCTL(x)	(((x) & 0xff) << 0)

#define MIPI_DSI_PLL_STABLE(x)		(((x) & 0x1) << 31)
#define MIPI_DSI_TX_READY_HS_CLK(x)	(((x) & 0x1) << 10)
#define MIPI_DSI_ULPS_CLK(x)		(((x) & 0x1) << 9)
#define MIPI_DSI_STOP_STATE_CLK(x)	(((x) & 0x1) << 8)
#define MIPI_DSI_ULPS_DAT(x)		(((x) & 0xf) << 4)
#define MIPI_DSI_STOP_STATE_DAT(x)	(((x) & 0xf) << 0)

#define INTSRC_SFR_PL_FIFO_EMPTY	(0x1 << 29)
#define INTSRC_SFR_PH_FIFO_EMPTY	(0x1 << 28)
#define INTSRC_RX_DATA_DONE		(0x1 << 18)
#define INTMSK_SFR_PL_FIFO_EMPTY	(0x1 << 29)
#define INTMSK_SFR_PH_FIFO_EMPTY	(0x1 << 28)
#define INTMSK_RX_DATA_DONE		(0x1 << 18)

#define MIPI_DSI_STOP_STATE_CNT(x)	(((x) & 0x7ff) << 21)
#define MIPI_DSI_CMD_LPDT		(0x1 << 7)
#define MIPI_DSI_TX_LPDT		(0x1 << 6)

#endif
