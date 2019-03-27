/*******************************************************************************
Copyright (C) 2015 Annapurna Labs Ltd.

This file may be licensed under the terms of the Annapurna Labs Commercial
License Agreement.

Alternatively, this file can be distributed under the terms of the GNU General
Public License V2 as published by the Free Software Foundation and can be
found at http://www.gnu.org/licenses/gpl-2.0.html

Alternatively, redistribution and use in source and binary forms, with or
without modification, are permitted provided that the following conditions are
met:

    *     Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.

    *     Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in
the documentation and/or other materials provided with the
distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*******************************************************************************/

/**
 *  @{
 * @file   al_hal_serdes_regs.h
 *
 * @brief ... registers
 *
 */

#ifndef __AL_HAL_SERDES_REGS_H__
#define __AL_HAL_SERDES_REGS_H__

#include "al_hal_plat_types.h"

#ifdef __cplusplus
extern "C" {
#endif
/*
* Unit Registers
*/

struct serdes_gen {
	/* [0x0] SerDes Registers Version */
	uint32_t version;
	uint32_t rsrvd_0[3];
	/* [0x10] SerDes register file address */
	uint32_t reg_addr;
	/* [0x14] SerDes register file data */
	uint32_t reg_data;
	uint32_t rsrvd_1[2];
	/* [0x20] SerDes control */
	uint32_t ictl_multi_bist;
	/* [0x24] SerDes control */
	uint32_t ictl_pcs;
	/* [0x28] SerDes control */
	uint32_t ictl_pma;
	uint32_t rsrvd_2;
	/* [0x30] SerDes control */
	uint32_t ipd_multi_synth;
	/* [0x34] SerDes control */
	uint32_t irst;
	/* [0x38] SerDes control */
	uint32_t octl_multi_synthready;
	/* [0x3c] SerDes control */
	uint32_t octl_multi_synthstatus;
	/* [0x40] SerDes control */
	uint32_t clk_out;
	uint32_t rsrvd[47];
};
struct serdes_lane {
	uint32_t rsrvd1[4];
	/* [0x10] SerDes status */
	uint32_t octl_pma;
	/* [0x14] SerDes control */
	uint32_t ictl_multi_andme;
	/* [0x18] SerDes control */
	uint32_t ictl_multi_lb;
	/* [0x1c] SerDes control */
	uint32_t ictl_multi_rxbist;
	/* [0x20] SerDes control */
	uint32_t ictl_multi_txbist;
	/* [0x24] SerDes control */
	uint32_t ictl_multi;
	/* [0x28] SerDes control */
	uint32_t ictl_multi_rxeq;
	/* [0x2c] SerDes control */
	uint32_t ictl_multi_rxeq_l_low;
	/* [0x30] SerDes control */
	uint32_t ictl_multi_rxeq_l_high;
	/* [0x34] SerDes control */
	uint32_t ictl_multi_rxeyediag;
	/* [0x38] SerDes control */
	uint32_t ictl_multi_txdeemph;
	/* [0x3c] SerDes control */
	uint32_t ictl_multi_txmargin;
	/* [0x40] SerDes control */
	uint32_t ictl_multi_txswing;
	/* [0x44] SerDes control */
	uint32_t idat_multi;
	/* [0x48] SerDes control */
	uint32_t ipd_multi;
	/* [0x4c] SerDes control */
	uint32_t octl_multi_rxbist;
	/* [0x50] SerDes control */
	uint32_t octl_multi;
	/* [0x54] SerDes control */
	uint32_t octl_multi_rxeyediag;
	/* [0x58] SerDes control */
	uint32_t odat_multi_rxbist;
	/* [0x5c] SerDes control */
	uint32_t odat_multi_rxeq;
	/* [0x60] SerDes control */
	uint32_t multi_rx_dvalid;
	/* [0x64] SerDes control */
	uint32_t reserved;
	uint32_t rsrvd[6];
};

struct al_serdes_regs {
	uint32_t rsrvd_0[64];
	struct serdes_gen gen;                               /* [0x100] */
	struct serdes_lane lane[4];                          /* [0x200] */
};


/*
* Registers Fields
*/


/**** version register ****/
/*  Revision number (Minor) */
#define SERDES_GEN_VERSION_RELEASE_NUM_MINOR_MASK 0x000000FF
#define SERDES_GEN_VERSION_RELEASE_NUM_MINOR_SHIFT 0
/*  Revision number (Major) */
#define SERDES_GEN_VERSION_RELEASE_NUM_MAJOR_MASK 0x0000FF00
#define SERDES_GEN_VERSION_RELEASE_NUM_MAJOR_SHIFT 8
/*  Date of release */
#define SERDES_GEN_VERSION_DATE_DAY_MASK 0x001F0000
#define SERDES_GEN_VERSION_DATE_DAY_SHIFT 16
/*  Month of release */
#define SERDES_GEN_VERSION_DATA_MONTH_MASK 0x01E00000
#define SERDES_GEN_VERSION_DATA_MONTH_SHIFT 21
/* Year of release (starting from 2000) */
#define SERDES_GEN_VERSION_DATE_YEAR_MASK 0x3E000000
#define SERDES_GEN_VERSION_DATE_YEAR_SHIFT 25
/*  Reserved */
#define SERDES_GEN_VERSION_RESERVED_MASK 0xC0000000
#define SERDES_GEN_VERSION_RESERVED_SHIFT 30

/**** reg_addr register ****/
/* Address value */
#define SERDES_GEN_REG_ADDR_VAL_MASK     0x0000FFFF
#define SERDES_GEN_REG_ADDR_VAL_SHIFT    0

/**** reg_data register ****/
/* Data value */
#define SERDES_GEN_REG_DATA_VAL_MASK     0x000000FF
#define SERDES_GEN_REG_DATA_VAL_SHIFT    0

/**** ICTL_MULTI_BIST register ****/

#define SERDES_GEN_ICTL_MULTI_BIST_MODESEL_NT_MASK 0x00000007
#define SERDES_GEN_ICTL_MULTI_BIST_MODESEL_NT_SHIFT 0

/**** ICTL_PCS register ****/

#define SERDES_GEN_ICTL_PCS_EN_NT        (1 << 0)

/**** ICTL_PMA register ****/

#define SERDES_GEN_ICTL_PMA_REF_SEL_NT_MASK 0x00000007
#define SERDES_GEN_ICTL_PMA_REF_SEL_NT_SHIFT 0

#define SERDES_GEN_ICTL_PMA_REF_SEL_NT_REF \
	(0 << (SERDES_GEN_ICTL_PMA_REF_SEL_NT_SHIFT))
#define SERDES_GEN_ICTL_PMA_REF_SEL_NT_R2L \
	(3 << (SERDES_GEN_ICTL_PMA_REF_SEL_NT_SHIFT))
#define SERDES_GEN_ICTL_PMA_REF_SEL_NT_L2R \
	(4 << (SERDES_GEN_ICTL_PMA_REF_SEL_NT_SHIFT))

#define SERDES_GEN_ICTL_PMA_REFBUSRIGHT2LEFT_MODE_NT_MASK 0x00000070
#define SERDES_GEN_ICTL_PMA_REFBUSRIGHT2LEFT_MODE_NT_SHIFT 4

#define SERDES_GEN_ICTL_PMA_REFBUSRIGHT2LEFT_MODE_NT_0 \
	(0 << (SERDES_GEN_ICTL_PMA_REFBUSRIGHT2LEFT_MODE_NT_SHIFT))
#define SERDES_GEN_ICTL_PMA_REFBUSRIGHT2LEFT_MODE_NT_REF \
	(2 << (SERDES_GEN_ICTL_PMA_REFBUSRIGHT2LEFT_MODE_NT_SHIFT))
#define SERDES_GEN_ICTL_PMA_REFBUSRIGHT2LEFT_MODE_NT_R2L \
	(3 << (SERDES_GEN_ICTL_PMA_REFBUSRIGHT2LEFT_MODE_NT_SHIFT))

#define SERDES_GEN_ICTL_PMA_REFBUSLEFT2RIGHT_MODE_NT_MASK 0x00000700
#define SERDES_GEN_ICTL_PMA_REFBUSLEFT2RIGHT_MODE_NT_SHIFT 8

#define SERDES_GEN_ICTL_PMA_REFBUSLEFT2RIGHT_MODE_NT_0 \
	(0 << (SERDES_GEN_ICTL_PMA_REFBUSLEFT2RIGHT_MODE_NT_SHIFT))
#define SERDES_GEN_ICTL_PMA_REFBUSLEFT2RIGHT_MODE_NT_REF \
	(2 << (SERDES_GEN_ICTL_PMA_REFBUSLEFT2RIGHT_MODE_NT_SHIFT))
#define SERDES_GEN_ICTL_PMA_REFBUSLEFT2RIGHT_MODE_NT_L2R \
	(3 << (SERDES_GEN_ICTL_PMA_REFBUSLEFT2RIGHT_MODE_NT_SHIFT))

#define SERDES_GEN_ICTL_PMA_TXENABLE_A_SRC		(1 << 11)
#define SERDES_GEN_ICTL_PMA_TXENABLE_A_SRC_THIS		(0 << 11)
#define SERDES_GEN_ICTL_PMA_TXENABLE_A_SRC_MASTER	(1 << 11)

#define SERDES_GEN_ICTL_PMA_TXENABLE_A   (1 << 12)

#define SERDES_GEN_ICTL_PMA_SYNTHCKBYPASSEN_NT (1 << 13)

/**** IPD_MULTI_SYNTH register ****/

#define SERDES_GEN_IPD_MULTI_SYNTH_B     (1 << 0)

/**** IRST register ****/

#define SERDES_GEN_IRST_PIPE_RST_L3_B_A  (1 << 0)

#define SERDES_GEN_IRST_PIPE_RST_L2_B_A  (1 << 1)

#define SERDES_GEN_IRST_PIPE_RST_L1_B_A  (1 << 2)

#define SERDES_GEN_IRST_PIPE_RST_L0_B_A  (1 << 3)

#define SERDES_GEN_IRST_MULTI_HARD_TXRX_L3_B_A (1 << 4)

#define SERDES_GEN_IRST_MULTI_HARD_TXRX_L2_B_A (1 << 5)

#define SERDES_GEN_IRST_MULTI_HARD_TXRX_L1_B_A (1 << 6)

#define SERDES_GEN_IRST_MULTI_HARD_TXRX_L0_B_A (1 << 7)

#define SERDES_GEN_IRST_MULTI_HARD_SYNTH_B_A (1 << 8)

#define SERDES_GEN_IRST_POR_B_A          (1 << 12)

#define SERDES_GEN_IRST_PIPE_RST_L3_B_A_SEL (1 << 16)

#define SERDES_GEN_IRST_PIPE_RST_L2_B_A_SEL (1 << 17)

#define SERDES_GEN_IRST_PIPE_RST_L1_B_A_SEL (1 << 18)

#define SERDES_GEN_IRST_PIPE_RST_L0_B_A_SEL (1 << 19)

#define SERDES_GEN_IRST_MULTI_HARD_TXRX_L3_B_A_SEL (1 << 20)

#define SERDES_GEN_IRST_MULTI_HARD_TXRX_L2_B_A_SEL (1 << 21)

#define SERDES_GEN_IRST_MULTI_HARD_TXRX_L1_B_A_SEL (1 << 22)

#define SERDES_GEN_IRST_MULTI_HARD_TXRX_L0_B_A_SEL (1 << 23)

/**** OCTL_MULTI_SYNTHREADY register ****/

#define SERDES_GEN_OCTL_MULTI_SYNTHREADY_A (1 << 0)

/**** OCTL_MULTI_SYNTHSTATUS register ****/

#define SERDES_GEN_OCTL_MULTI_SYNTHSTATUS_A (1 << 0)

/**** clk_out register ****/

#define SERDES_GEN_CLK_OUT_SEL_MASK      0x0000003F
#define SERDES_GEN_CLK_OUT_SEL_SHIFT     0

/**** OCTL_PMA register ****/

#define SERDES_LANE_OCTL_PMA_TXSTATUS_L_A (1 << 0)

/**** ICTL_MULTI_ANDME register ****/

#define SERDES_LANE_ICTL_MULTI_ANDME_EN_L_A (1 << 0)

#define SERDES_LANE_ICTL_MULTI_ANDME_EN_L_A_SEL (1 << 1)

/**** ICTL_MULTI_LB register ****/

#define SERDES_LANE_ICTL_MULTI_LB_TX2RXIOTIMEDEN_L_NT (1 << 0)

#define SERDES_LANE_ICTL_MULTI_LB_TX2RXBUFTIMEDEN_L_NT (1 << 1)

#define SERDES_LANE_ICTL_MULTI_LB_RX2TXUNTIMEDEN_L_NT (1 << 2)

#define SERDES_LANE_ICTL_MULTI_LB_PARRX2TXTIMEDEN_L_NT (1 << 3)

#define SERDES_LANE_ICTL_MULTI_LB_CDRCLK2TXEN_L_NT (1 << 4)

#define SERDES_LANE_ICTL_MULTI_LB_TX2RXBUFTIMEDEN_L_NT_SEL (1 << 8)

#define SERDES_LANE_ICTL_MULTI_LB_RX2TXUNTIMEDEN_L_NT_SEL (1 << 9)

/**** ICTL_MULTI_RXBIST register ****/

#define SERDES_LANE_ICTL_MULTI_RXBIST_EN_L_A (1 << 0)

/**** ICTL_MULTI_TXBIST register ****/

#define SERDES_LANE_ICTL_MULTI_TXBIST_EN_L_A (1 << 0)

/**** ICTL_MULTI register ****/

#define SERDES_LANE_ICTL_MULTI_PSTATE_L_MASK 0x00000003
#define SERDES_LANE_ICTL_MULTI_PSTATE_L_SHIFT 0

#define SERDES_LANE_ICTL_MULTI_PSTATE_L_SEL (1 << 2)

#define SERDES_LANE_ICTL_MULTI_RXDATAWIDTH_L_MASK 0x00000070
#define SERDES_LANE_ICTL_MULTI_RXDATAWIDTH_L_SHIFT 4

#define SERDES_LANE_ICTL_MULTI_RXOVRCDRLOCK2DATAEN_L_A (1 << 8)

#define SERDES_LANE_ICTL_MULTI_RXOVRCDRLOCK2DATA_L_A (1 << 9)

#define SERDES_LANE_ICTL_MULTI_TXBEACON_L_A (1 << 12)

#define SERDES_LANE_ICTL_MULTI_TXDETECTRXREQ_L_A (1 << 13)

#define SERDES_LANE_ICTL_MULTI_RXRATE_L_MASK 0x00070000
#define SERDES_LANE_ICTL_MULTI_RXRATE_L_SHIFT 16

#define SERDES_LANE_ICTL_MULTI_RXRATE_L_SEL (1 << 19)

#define SERDES_LANE_ICTL_MULTI_TXRATE_L_MASK 0x00700000
#define SERDES_LANE_ICTL_MULTI_TXRATE_L_SHIFT 20

#define SERDES_LANE_ICTL_MULTI_TXRATE_L_SEL (1 << 23)

#define SERDES_LANE_ICTL_MULTI_TXAMP_L_MASK 0x07000000
#define SERDES_LANE_ICTL_MULTI_TXAMP_L_SHIFT 24

#define SERDES_LANE_ICTL_MULTI_TXAMP_EN_L (1 << 27)

#define SERDES_LANE_ICTL_MULTI_TXDATAWIDTH_L_MASK 0x70000000
#define SERDES_LANE_ICTL_MULTI_TXDATAWIDTH_L_SHIFT 28

/**** ICTL_MULTI_RXEQ register ****/

#define SERDES_LANE_ICTL_MULTI_RXEQ_EN_L (1 << 0)

#define SERDES_LANE_ICTL_MULTI_RXEQ_START_L_A (1 << 1)

#define SERDES_LANE_ICTL_MULTI_RXEQ_PRECAL_CODE_SEL_MASK 0x00000070
#define SERDES_LANE_ICTL_MULTI_RXEQ_PRECAL_CODE_SEL_SHIFT 4

/**** ICTL_MULTI_RXEQ_L_high register ****/

#define SERDES_LANE_ICTL_MULTI_RXEQ_L_HIGH_VAL (1 << 0)

/**** ICTL_MULTI_RXEYEDIAG register ****/

#define SERDES_LANE_ICTL_MULTI_RXEYEDIAG_START_L_A (1 << 0)

/**** ICTL_MULTI_TXDEEMPH register ****/

#define SERDES_LANE_ICTL_MULTI_TXDEEMPH_L_MASK 0x0003FFFF
#define SERDES_LANE_ICTL_MULTI_TXDEEMPH_L_SHIFT 0

#define SERDES_LANE_ICTL_MULTI_TXDEEMPH_C_ZERO_MASK	0x7c0
#define SERDES_LANE_ICTL_MULTI_TXDEEMPH_C_ZERO_SHIFT	6
#define SERDES_LANE_ICTL_MULTI_TXDEEMPH_C_PLUS_MASK	0xf000
#define SERDES_LANE_ICTL_MULTI_TXDEEMPH_C_PLUS_SHIFT	12
#define SERDES_LANE_ICTL_MULTI_TXDEEMPH_C_MINUS_MASK	0x7
#define SERDES_LANE_ICTL_MULTI_TXDEEMPH_C_MINUS_SHIFT	0

/**** ICTL_MULTI_TXMARGIN register ****/

#define SERDES_LANE_ICTL_MULTI_TXMARGIN_L_MASK 0x00000007
#define SERDES_LANE_ICTL_MULTI_TXMARGIN_L_SHIFT 0

/**** ICTL_MULTI_TXSWING register ****/

#define SERDES_LANE_ICTL_MULTI_TXSWING_L (1 << 0)

/**** IDAT_MULTI register ****/

#define SERDES_LANE_IDAT_MULTI_TXELECIDLE_L_MASK 0x0000000F
#define SERDES_LANE_IDAT_MULTI_TXELECIDLE_L_SHIFT 0

#define SERDES_LANE_IDAT_MULTI_TXELECIDLE_L_SEL (1 << 4)

/**** IPD_MULTI register ****/

#define SERDES_LANE_IPD_MULTI_TX_L_B     (1 << 0)

#define SERDES_LANE_IPD_MULTI_RX_L_B     (1 << 1)

/**** OCTL_MULTI_RXBIST register ****/

#define SERDES_LANE_OCTL_MULTI_RXBIST_DONE_L_A (1 << 0)

#define SERDES_LANE_OCTL_MULTI_RXBIST_RXLOCKED_L_A (1 << 1)

/**** OCTL_MULTI register ****/

#define SERDES_LANE_OCTL_MULTI_RXCDRLOCK2DATA_L_A (1 << 0)

#define SERDES_LANE_OCTL_MULTI_RXEQ_DONE_L_A (1 << 1)

#define SERDES_LANE_OCTL_MULTI_RXREADY_L_A (1 << 2)

#define SERDES_LANE_OCTL_MULTI_RXSTATUS_L_A (1 << 3)

#define SERDES_LANE_OCTL_MULTI_TXREADY_L_A (1 << 4)

#define SERDES_LANE_OCTL_MULTI_TXDETECTRXSTAT_L_A (1 << 5)

#define SERDES_LANE_OCTL_MULTI_TXDETECTRXACK_L_A (1 << 6)

#define SERDES_LANE_OCTL_MULTI_RXSIGNALDETECT_L_A (1 << 7)

/**** OCTL_MULTI_RXEYEDIAG register ****/

#define SERDES_LANE_OCTL_MULTI_RXEYEDIAG_STAT_L_A_MASK 0x00003FFF
#define SERDES_LANE_OCTL_MULTI_RXEYEDIAG_STAT_L_A_SHIFT 0

#define SERDES_LANE_OCTL_MULTI_RXEYEDIAG_DONE_L_A (1 << 16)

#define SERDES_LANE_OCTL_MULTI_RXEYEDIAG_ERR_L_A (1 << 17)

/**** ODAT_MULTI_RXBIST register ****/

#define SERDES_LANE_ODAT_MULTI_RXBIST_ERRCOUNT_L_A_MASK 0x0000FFFF
#define SERDES_LANE_ODAT_MULTI_RXBIST_ERRCOUNT_L_A_SHIFT 0

#define SERDES_LANE_ODAT_MULTI_RXBIST_ERRCOUNT_OVERFLOW_L_A (1 << 16)

/**** ODAT_MULTI_RXEQ register ****/

#define SERDES_LANE_ODAT_MULTI_RXEQ_BEST_EYE_VAL_L_A_MASK 0x00003FFF
#define SERDES_LANE_ODAT_MULTI_RXEQ_BEST_EYE_VAL_L_A_SHIFT 0

/**** MULTI_RX_DVALID register ****/

#define SERDES_LANE_MULTI_RX_DVALID_MASK_CDR_LOCK (1 << 0)

#define SERDES_LANE_MULTI_RX_DVALID_MASK_SIGNALDETECT (1 << 1)

#define SERDES_LANE_MULTI_RX_DVALID_MASK_TX_READY (1 << 2)

#define SERDES_LANE_MULTI_RX_DVALID_MASK_RX_READY (1 << 3)

#define SERDES_LANE_MULTI_RX_DVALID_MASK_SYNT_READY (1 << 4)

#define SERDES_LANE_MULTI_RX_DVALID_MASK_RX_ELECIDLE (1 << 5)

#define SERDES_LANE_MULTI_RX_DVALID_MUX_SEL_MASK 0x00FF0000
#define SERDES_LANE_MULTI_RX_DVALID_MUX_SEL_SHIFT 16

#define SERDES_LANE_MULTI_RX_DVALID_PS_00_SEL (1 << 24)

#define SERDES_LANE_MULTI_RX_DVALID_PS_00_VAL (1 << 25)

#define SERDES_LANE_MULTI_RX_DVALID_PS_01_SEL (1 << 26)

#define SERDES_LANE_MULTI_RX_DVALID_PS_01_VAL (1 << 27)

#define SERDES_LANE_MULTI_RX_DVALID_PS_10_SEL (1 << 28)

#define SERDES_LANE_MULTI_RX_DVALID_PS_10_VAL (1 << 29)

#define SERDES_LANE_MULTI_RX_DVALID_PS_11_SEL (1 << 30)

#define SERDES_LANE_MULTI_RX_DVALID_PS_11_VAL (1 << 31)

/**** reserved register ****/

#define SERDES_LANE_RESERVED_OUT_MASK    0x000000FF
#define SERDES_LANE_RESERVED_OUT_SHIFT   0

#define SERDES_LANE_RESERVED_IN_MASK     0x00FF0000
#define SERDES_LANE_RESERVED_IN_SHIFT    16

#ifdef __cplusplus
}
#endif

#endif /* __AL_HAL_serdes_REGS_H__ */

/** @} end of ... group */


