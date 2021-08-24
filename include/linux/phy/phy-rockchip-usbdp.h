/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Rockchip USBDP Combo PHY with Samsung IP block driver
 *
 * Copyright (C) 2021 Rockchip Electronics Co., Ltd
 */

#ifndef __PHY_ROCKCHIP_USBDP_H_
#define __PHY_ROCKCHIP_USBDP_H_

#include <linux/bits.h>

/* RK3588 USBDP PHY Register Definitions */

#define UDPHY_PCS				0x4000
#define UDPHY_PMA				0x8000

/* VO0 GRF Registers */
#define RK3588_GRF_VO0_CON0			0x0000
#define RK3588_GRF_VO0_CON2			0x0008
#define DP_SINK_HPD_CFG				BIT(11)
#define DP_SINK_HPD_SEL				BIT(10)
#define DP_AUX_DIN_SEL				BIT(9)
#define DP_AUX_DOUT_SEL				BIT(8)
#define DP_LANE_SEL_N(n)			GENMASK(2 * (n) + 1, 2 * (n))
#define DP_LANE_SEL_ALL				GENMASK(7, 0)
#define PHY_AUX_DP_DATA_POL_NORMAL		0
#define PHY_AUX_DP_DATA_POL_INVERT		1

/* PMA CMN Registers */
#define CMN_LANE_MUX_AND_EN_OFFSET		0x0288	/* cmn_reg00A2 */
#define CMN_DP_LANE_MUX_N(n)			BIT((n) + 4)
#define CMN_DP_LANE_EN_N(n)			BIT(n)
#define CMN_DP_LANE_MUX_ALL			GENMASK(7, 4)
#define CMN_DP_LANE_EN_ALL			GENMASK(3, 0)
#define PHY_LANE_MUX_USB			0
#define PHY_LANE_MUX_DP				1

#define CMN_DP_LINK_OFFSET			0x28c	/*cmn_reg00A3 */
#define CMN_DP_TX_LINK_BW			GENMASK(6, 5)
#define CMN_DP_TX_LANE_SWAP_EN			BIT(2)

#define CMN_SSC_EN_OFFSET			0x2d0	/* cmn_reg00B4 */
#define CMN_ROPLL_SSC_EN			BIT(1)
#define CMN_LCPLL_SSC_EN			BIT(0)

#define CMN_ANA_LCPLL_DONE_OFFSET		0x0350	/* cmn_reg00D4 */
#define CMN_ANA_LCPLL_LOCK_DONE			BIT(7)
#define CMN_ANA_LCPLL_AFC_DONE			BIT(6)

#define CMN_ANA_ROPLL_DONE_OFFSET		0x0354	/* cmn_reg00D5 */
#define CMN_ANA_ROPLL_LOCK_DONE			BIT(1)
#define CMN_ANA_ROPLL_AFC_DONE			BIT(0)

#define CMN_DP_RSTN_OFFSET			0x038c	/* cmn_reg00E3 */
#define CMN_DP_INIT_RSTN			BIT(3)
#define CMN_DP_CMN_RSTN				BIT(2)
#define CMN_CDR_WTCHDG_EN			BIT(1)
#define CMN_CDR_WTCHDG_MSK_CDR_EN		BIT(0)

#define TRSV_ANA_TX_CLK_OFFSET_N(n)		(0x854 + (n) * 0x800)	/* trsv_reg0215 */
#define LN_ANA_TX_SER_TXCLK_INV			BIT(1)

#define TRSV_LN0_MON_RX_CDR_DONE_OFFSET		0x0b84	/* trsv_reg02E1 */
#define TRSV_LN0_MON_RX_CDR_LOCK_DONE		BIT(0)

#define TRSV_LN2_MON_RX_CDR_DONE_OFFSET		0x1b84	/* trsv_reg06E1 */
#define TRSV_LN2_MON_RX_CDR_LOCK_DONE		BIT(0)

#endif
