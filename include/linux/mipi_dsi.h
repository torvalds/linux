/*
 * Copyright (C) 2011-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#ifndef __INCLUDE_MIPI_DSI_H
#define __INCLUDE_MIPI_DSI_H

#define     MIPI_DSI_VERSION		(0x000)
#define     MIPI_DSI_PWR_UP		(0x004)
#define     MIPI_DSI_CLKMGR_CFG		(0x008)
#define     MIPI_DSI_DPI_CFG		(0x00c)
#define     MIPI_DSI_DBI_CFG		(0x010)
#define     MIPI_DSI_DBIS_CMDSIZE	(0x014)
#define     MIPI_DSI_PCKHDL_CFG		(0x018)
#define     MIPI_DSI_VID_MODE_CFG	(0x01c)
#define     MIPI_DSI_VID_PKT_CFG	(0x020)
#define     MIPI_DSI_CMD_MODE_CFG	(0x024)
#define     MIPI_DSI_TMR_LINE_CFG	(0x028)
#define     MIPI_DSI_VTIMING_CFG	(0x02c)
#define     MIPI_DSI_PHY_TMR_CFG	(0x030)
#define     MIPI_DSI_GEN_HDR		(0x034)
#define     MIPI_DSI_GEN_PLD_DATA	(0x038)
#define     MIPI_DSI_CMD_PKT_STATUS	(0x03c)
#define     MIPI_DSI_TO_CNT_CFG		(0x040)
#define     MIPI_DSI_ERROR_ST0		(0x044)
#define     MIPI_DSI_ERROR_ST1		(0x048)
#define     MIPI_DSI_ERROR_MSK0		(0x04c)
#define     MIPI_DSI_ERROR_MSK1		(0x050)
#define     MIPI_DSI_PHY_RSTZ		(0x054)
#define     MIPI_DSI_PHY_IF_CFG		(0x058)
#define     MIPI_DSI_PHY_IF_CTRL	(0x05c)
#define     MIPI_DSI_PHY_STATUS		(0x060)
#define     MIPI_DSI_PHY_TST_CTRL0	(0x064)
#define     MIPI_DSI_PHY_TST_CTRL1	(0x068)

#define		DSI_PWRUP_RESET					(0x0 << 0)
#define		DSI_PWRUP_POWERUP				(0x1 << 0)

#define		DSI_DPI_CFG_VID_SHIFT				(0)
#define		DSI_DPI_CFG_VID_MASK				(0x3)
#define		DSI_DPI_CFG_COLORCODE_SHIFT			(2)
#define		DSI_DPI_CFG_COLORCODE_MASK			(0x7)
#define		DSI_DPI_CFG_DATAEN_ACT_LOW			(0x1 << 5)
#define		DSI_DPI_CFG_DATAEN_ACT_HIGH			(0x0 << 5)
#define		DSI_DPI_CFG_VSYNC_ACT_LOW			(0x1 << 6)
#define		DSI_DPI_CFG_VSYNC_ACT_HIGH			(0x0 << 6)
#define		DSI_DPI_CFG_HSYNC_ACT_LOW			(0x1 << 7)
#define		DSI_DPI_CFG_HSYNC_ACT_HIGH			(0x0 << 7)
#define		DSI_DPI_CFG_SHUTD_ACT_LOW			(0x1 << 8)
#define		DSI_DPI_CFG_SHUTD_ACT_HIGH			(0x0 << 8)
#define		DSI_DPI_CFG_COLORMODE_ACT_LOW			(0x1 << 9)
#define		DSI_DPI_CFG_COLORMODE_ACT_HIGH			(0x0 << 9)
#define		DSI_DPI_CFG_EN18LOOSELY				(0x1 << 10)

#define		DSI_PCKHDL_CFG_EN_EOTP_TX			(0x1 << 0)
#define		DSI_PCKHDL_CFG_EN_EOTP_RX			(0x1 << 1)
#define		DSI_PCKHDL_CFG_EN_BTA				(0x1 << 2)
#define		DSI_PCKHDL_CFG_EN_ECC_RX			(0x1 << 3)
#define		DSI_PCKHDL_CFG_EN_CRC_RX			(0x1 << 4)
#define		DSI_PCKHDL_CFG_GEN_VID_RX_MASK			(0x3)
#define		DSI_PCKHDL_CFG_GEN_VID_RX_SHIFT			(5)

#define		DSI_VID_MODE_CFG_EN				(0x1 << 0)
#define		DSI_VID_MODE_CFG_EN_BURSTMODE			(0x3 << 1)
#define		DSI_VID_MODE_CFG_TYPE_MASK			(0x3)
#define		DSI_VID_MODE_CFG_TYPE_SHIFT			(1)
#define		DSI_VID_MODE_CFG_EN_LP_VSA			(0x1 << 3)
#define		DSI_VID_MODE_CFG_EN_LP_VBP			(0x1 << 4)
#define		DSI_VID_MODE_CFG_EN_LP_VFP			(0x1 << 5)
#define		DSI_VID_MODE_CFG_EN_LP_VACT			(0x1 << 6)
#define		DSI_VID_MODE_CFG_EN_LP_HBP			(0x1 << 7)
#define		DSI_VID_MODE_CFG_EN_LP_HFP			(0x1 << 8)
#define		DSI_VID_MODE_CFG_EN_MULTI_PKT			(0x1 << 9)
#define		DSI_VID_MODE_CFG_EN_NULL_PKT			(0x1 << 10)
#define		DSI_VID_MODE_CFG_EN_FRAME_ACK			(0x1 << 11)
#define		DSI_VID_MODE_CFG_EN_LP_MODE (DSI_VID_MODE_CFG_EN_LP_VSA | \
						 DSI_VID_MODE_CFG_EN_LP_VBP | \
						 DSI_VID_MODE_CFG_EN_LP_VFP | \
						 DSI_VID_MODE_CFG_EN_LP_HFP | \
						 DSI_VID_MODE_CFG_EN_LP_HBP | \
						 DSI_VID_MODE_CFG_EN_LP_VACT)



#define		DSI_VID_PKT_CFG_VID_PKT_SZ_MASK			(0x7ff)
#define		DSI_VID_PKT_CFG_VID_PKT_SZ_SHIFT		(0)
#define		DSI_VID_PKT_CFG_NUM_CHUNKS_MASK			(0x3ff)
#define		DSI_VID_PKT_CFG_NUM_CHUNKS_SHIFT		(11)
#define		DSI_VID_PKT_CFG_NULL_PKT_SZ_MASK		(0x3ff)
#define		DSI_VID_PKT_CFG_NULL_PKT_SZ_SHIFT		(21)

#define		MIPI_DSI_CMD_MODE_CFG_EN_LOWPOWER		(0x1FFF)
#define		MIPI_DSI_CMD_MODE_CFG_EN_CMD_MODE		(0x1 << 0)

#define		DSI_TME_LINE_CFG_HSA_TIME_MASK			(0x1ff)
#define		DSI_TME_LINE_CFG_HSA_TIME_SHIFT			(0)
#define		DSI_TME_LINE_CFG_HBP_TIME_MASK			(0x1ff)
#define		DSI_TME_LINE_CFG_HBP_TIME_SHIFT			(9)
#define		DSI_TME_LINE_CFG_HLINE_TIME_MASK		(0x3fff)
#define		DSI_TME_LINE_CFG_HLINE_TIME_SHIFT		(18)

#define		DSI_VTIMING_CFG_VSA_LINES_MASK			(0xf)
#define		DSI_VTIMING_CFG_VSA_LINES_SHIFT			(0)
#define		DSI_VTIMING_CFG_VBP_LINES_MASK			(0x3f)
#define		DSI_VTIMING_CFG_VBP_LINES_SHIFT			(4)
#define		DSI_VTIMING_CFG_VFP_LINES_MASK			(0x3f)
#define		DSI_VTIMING_CFG_VFP_LINES_SHIFT			(10)
#define		DSI_VTIMING_CFG_V_ACT_LINES_MASK		(0x7ff)
#define		DSI_VTIMING_CFG_V_ACT_LINES_SHIFT		(16)

#define		DSI_PHY_TMR_CFG_BTA_TIME_MASK			(0xfff)
#define		DSI_PHY_TMR_CFG_BTA_TIME_SHIFT			(0)
#define		DSI_PHY_TMR_CFG_LP2HS_TIME_MASK			(0xff)
#define		DSI_PHY_TMR_CFG_LP2HS_TIME_SHIFT		(12)
#define		DSI_PHY_TMR_CFG_HS2LP_TIME_MASK			(0xff)
#define		DSI_PHY_TMR_CFG_HS2LP_TIME_SHIFT		(20)

#define		DSI_PHY_IF_CFG_N_LANES_MASK			(0x3)
#define		DSI_PHY_IF_CFG_N_LANES_SHIFT			(0)
#define		DSI_PHY_IF_CFG_WAIT_TIME_MASK			(0xff)
#define		DSI_PHY_IF_CFG_WAIT_TIME_SHIFT			(2)

#define		DSI_PHY_RSTZ_EN_CLK				(0x1 << 2)
#define		DSI_PHY_RSTZ_DISABLE_RST			(0x1 << 1)
#define		DSI_PHY_RSTZ_DISABLE_SHUTDOWN			(0x1 << 0)
#define		DSI_PHY_RSTZ_RST				(0x0)

#define		DSI_PHY_STATUS_LOCK				(0x1 << 0)
#define		DSI_PHY_STATUS_STOPSTATE_CLK_LANE		(0x1 << 2)

#define		DSI_GEN_HDR_TYPE_MASK				(0xff)
#define		DSI_GEN_HDR_TYPE_SHIFT				(0)
#define		DSI_GEN_HDR_DATA_MASK				(0xffff)
#define		DSI_GEN_HDR_DATA_SHIFT				(8)

#define		DSI_CMD_PKT_STATUS_GEN_CMD_EMPTY		(0x1 << 0)
#define		DSI_CMD_PKT_STATUS_GEN_CMD_FULL			(0x1 << 1)
#define		DSI_CMD_PKT_STATUS_GEN_PLD_W_EMPTY		(0x1 << 2)
#define		DSI_CMD_PKT_STATUS_GEN_PLD_W_FULL		(0x1 << 3)
#define		DSI_CMD_PKT_STATUS_GEN_PLD_R_EMPTY		(0x1 << 4)
#define		DSI_CMD_PKT_STATUS_GEN_RD_CMD_BUSY		(0x1 << 6)

#define		DSI_ERROR_MSK0_ALL_MASK				(0x1fffff)
#define		DSI_ERROR_MSK1_ALL_MASK				(0x3ffff)

#define		DSI_PHY_IF_CTRL_RESET				(0x0)
#define		DSI_PHY_IF_CTRL_TX_REQ_CLK_HS			(0x1 << 0)
#define		DSI_PHY_IF_CTRL_TX_REQ_CLK_ULPS			(0x1 << 1)
#define		DSI_PHY_IF_CTRL_TX_EXIT_CLK_ULPS		(0x1 << 2)
#define		DSI_PHY_IF_CTRL_TX_REQ_DATA_ULPS		(0x1 << 3)
#define		DSI_PHY_IF_CTRL_TX_EXIT_DATA_ULPS		(0x1 << 4)
#define		DSI_PHY_IF_CTRL_TX_TRIG_MASK			(0xF)
#define		DSI_PHY_IF_CTRL_TX_TRIG_SHIFT			(5)

#define		DSI_PHY_CLK_INIT_COMMAND			(0x44)
#define		DSI_GEN_PLD_DATA_BUF_SIZE			(0x4)
#endif
