/*******************************************************************************
Copyright (C) 2013 Annapurna Labs Ltd.

This file may be licensed under the terms of the Annapurna Labs Commercial
License Agreement.

Alternatively, this file can be distributed under the terms of the GNU General
Public License V2 or V3 as published by the Free Software Foundation and can be
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
 * @file   al_hal_serdes_c_regs.h
 *
 * @brief ... registers
 *
 */

#ifndef __AL_HAL_serdes_c_REGS_H__
#define __AL_HAL_serdes_c_REGS_H__

#include "al_hal_plat_types.h"

#ifdef __cplusplus
extern "C" {
#endif
/*
* Unit Registers
*/

struct al_serdes_c_gen {
	/* [0x0] SERDES registers Version */
	uint32_t version;
	uint32_t rsrvd_0[3];
	/* [0x10] SERDES register file address */
	uint32_t reg_addr;
	/* [0x14] SERDES register file data */
	uint32_t reg_data;
	/* [0x18] SERDES control */
	uint32_t ctrl;
	/* [0x1c] SERDES cpu mem address */
	uint32_t cpu_prog_addr;
	/* [0x20] SERDES cpu mem data */
	uint32_t cpu_prog_data;
	/* [0x24] SERDES data mem address */
	uint32_t cpu_data_mem_addr;
	/* [0x28] SERDES data mem data */
	uint32_t cpu_data_mem_data;
	/* [0x2c] SERDES control */
	uint32_t rst;
	/* [0x30] SERDES control */
	uint32_t status;
	uint32_t rsrvd[51];
};
struct al_serdes_c_lane {
	uint32_t rsrvd_0[4];
	/* [0x10] Data configuration */
	uint32_t cfg;
	/* [0x14] Lane status */
	uint32_t stat;
	/* [0x18] SERDES control */
	uint32_t reserved;
	uint32_t rsrvd[25];
};

struct al_serdes_c_regs {
	uint32_t rsrvd_0[64];
	struct al_serdes_c_gen gen;                             /* [0x100] */
	struct al_serdes_c_lane lane[2];                        /* [0x200] */
};


/*
* Registers Fields
*/


/**** version register ****/
/*  Revision number (Minor) */
#define SERDES_C_GEN_VERSION_RELEASE_NUM_MINOR_MASK 0x000000FF
#define SERDES_C_GEN_VERSION_RELEASE_NUM_MINOR_SHIFT 0
/*  Revision number (Major) */
#define SERDES_C_GEN_VERSION_RELEASE_NUM_MAJOR_MASK 0x0000FF00
#define SERDES_C_GEN_VERSION_RELEASE_NUM_MAJOR_SHIFT 8
/*  date of release */
#define SERDES_C_GEN_VERSION_DATE_DAY_MASK 0x001F0000
#define SERDES_C_GEN_VERSION_DATE_DAY_SHIFT 16
/*  month of release */
#define SERDES_C_GEN_VERSION_DATA_MONTH_MASK 0x01E00000
#define SERDES_C_GEN_VERSION_DATA_MONTH_SHIFT 21
/*  year of release (starting from 2000) */
#define SERDES_C_GEN_VERSION_DATE_YEAR_MASK 0x3E000000
#define SERDES_C_GEN_VERSION_DATE_YEAR_SHIFT 25
/*  Reserved */
#define SERDES_C_GEN_VERSION_RESERVED_MASK 0xC0000000
#define SERDES_C_GEN_VERSION_RESERVED_SHIFT 30

/**** reg_addr register ****/
/* address value */
#define SERDES_C_GEN_REG_ADDR_VAL_MASK   0x00007FFF
#define SERDES_C_GEN_REG_ADDR_VAL_SHIFT  0

/**** reg_data register ****/
/* data value */
#define SERDES_C_GEN_REG_DATA_VAL_MASK   0x000000FF
#define SERDES_C_GEN_REG_DATA_VAL_SHIFT  0
/* Bit-wise write enable */
#define SERDES_C_GEN_REG_DATA_STRB_MASK  0x0000FF00
#define SERDES_C_GEN_REG_DATA_STRB_SHIFT 8

/**** ctrl register ****/
/*
 * 0x0 – Select reference clock from Bump
 * 0x1 – Select inter-macro reference clock from the left side
 * 0x2 – Same as 0x0
 * 0x3 – Select inter-macro reference clock from the right side
 */
#define SERDES_C_GEN_CTRL_REFCLK_INPUT_SEL_MASK 0x00000003
#define SERDES_C_GEN_CTRL_REFCLK_INPUT_SEL_SHIFT 0

#define SERDES_C_GEN_CTRL_REFCLK_INPUT_SEL_REF \
	(0 << (SERDES_C_GEN_CTRL_REFCLK_INPUT_SEL_SHIFT))
#define SERDES_C_GEN_CTRL_REFCLK_INPUT_SEL_L2R \
	(1 << (SERDES_C_GEN_CTRL_REFCLK_INPUT_SEL_SHIFT))
#define SERDES_C_GEN_CTRL_REFCLK_INPUT_SEL_R2L \
	(3 << (SERDES_C_GEN_CTRL_REFCLK_INPUT_SEL_SHIFT))

/*
 * 0x0 – Tied to 0 to save power
 * 0x1 – Select reference clock from Bump
 * 0x2 – Select inter-macro reference clock input from right side
 * 0x3 – Same as 0x2
 */
#define SERDES_C_GEN_CTRL_REFCLK_LEFT_SEL_MASK 0x00000030
#define SERDES_C_GEN_CTRL_REFCLK_LEFT_SEL_SHIFT 4

#define SERDES_C_GEN_CTRL_REFCLK_LEFT_SEL_0 \
	(0 << (SERDES_C_GEN_CTRL_REFCLK_LEFT_SEL_SHIFT))
#define SERDES_C_GEN_CTRL_REFCLK_LEFT_SEL_REF \
	(1 << (SERDES_C_GEN_CTRL_REFCLK_LEFT_SEL_SHIFT))
#define SERDES_C_GEN_CTRL_REFCLK_LEFT_SEL_R2L \
	(2 << (SERDES_C_GEN_CTRL_REFCLK_LEFT_SEL_SHIFT))

/*
 * 0x0 – Tied to 0 to save power
 * 0x1 – Select reference clock from Bump
 * 0x2 – Select inter-macro reference clock input from left side
 * 0x3 – Same as 0x2
 */
#define SERDES_C_GEN_CTRL_REFCLK_RIGHT_SEL_MASK 0x000000C0
#define SERDES_C_GEN_CTRL_REFCLK_RIGHT_SEL_SHIFT 6

#define SERDES_C_GEN_CTRL_REFCLK_RIGHT_SEL_0 \
	(0 << (SERDES_C_GEN_CTRL_REFCLK_RIGHT_SEL_SHIFT))
#define SERDES_C_GEN_CTRL_REFCLK_RIGHT_SEL_REF \
	(1 << (SERDES_C_GEN_CTRL_REFCLK_RIGHT_SEL_SHIFT))
#define SERDES_C_GEN_CTRL_REFCLK_RIGHT_SEL_L2R \
	(2 << (SERDES_C_GEN_CTRL_REFCLK_RIGHT_SEL_SHIFT))

/*
 * Program memory acknowledge -  Only when the access
 * to the program memory is not
 * ready for the microcontroller, it
 * is driven to 0
 */
#define SERDES_C_GEN_CTRL_CPU_MEMPSACK   (1 << 8)
/*
 * Data memory acknowledge -  Only when the access
 * to the program memory is not
 * ready for the microcontroller, it
 * is driven to 0
 */
#define SERDES_C_GEN_CTRL_CPU_MEMACK     (1 << 12)
/*
 * 0 - keep cpu clk as sb clk
 * 1 – cpu_clk is sb_clk divided by 2
 */
#define SERDES_C_GEN_CTRL_CPU_CLK_DIV    (1 << 16)
/*
 * 0x0 – OIF CEI-28G-SR
 * 0x1 – OIF CIE-25G-LR
 * 0x8 – XFI
 * Others – Reserved
 *
 * Note that phy_ctrl_cfg_i[3] is used to signify high-speed/low-speed
 */
#define SERDES_C_GEN_CTRL_PHY_CTRL_CFG_MASK 0x00F00000
#define SERDES_C_GEN_CTRL_PHY_CTRL_CFG_SHIFT 20
/*
 * 0 - Internal 8051 micro- controller is allowed to access the internal APB
 * CSR. Internal APB runs at cpu_clk_i, and the accesses from the external APB
 * in apb_clk_i domain to APB CSR are resynchronized to cpu_clk_i. 1 – Bypass
 * CPU. Internal 8051 micro-controller is blocked from accessing the internal
 * APB CSR. Internal APB runs at apb_clk_i.
 */
#define SERDES_C_GEN_CTRL_CPU_BYPASS     (1 << 24)

/**** cpu_prog_addr register ****/
/*
 * address value 32 bit,
 * The firmware data will be 1 byte with 64K rows
 */
#define SERDES_C_GEN_CPU_PROG_ADDR_VAL_MASK 0x00007FFF
#define SERDES_C_GEN_CPU_PROG_ADDR_VAL_SHIFT 0

/**** cpu_data_mem_addr register ****/
/* address value – 8K byte memory */
#define SERDES_C_GEN_CPU_DATA_MEM_ADDR_VAL_MASK 0x00001FFF
#define SERDES_C_GEN_CPU_DATA_MEM_ADDR_VAL_SHIFT 0

/**** cpu_data_mem_data register ****/
/* data value */
#define SERDES_C_GEN_CPU_DATA_MEM_DATA_VAL_MASK 0x000000FF
#define SERDES_C_GEN_CPU_DATA_MEM_DATA_VAL_SHIFT 0

/**** rst register ****/
/* Power on reset Signal  – active low */
#define SERDES_C_GEN_RST_POR_N           (1 << 0)
/* CMU reset   Active low */
#define SERDES_C_GEN_RST_CM0_RST_N       (1 << 1)
/*
 * 0x0 – Normal / Active
 * 0x1 – Partial power down
 * 0x2 – Near complete power down (only
 * refclk buffers and portions of analog bias
 * active)
 * 0x3 – complete power down (IDDQ mode)
 * Can be asserted when CMU is in normal
 * mode.  These modes provide an increased
 * power savings compared to reset mode.
 * Signal is overridden by por_n_i so has no
 * effect in power on reset state.
 */
#define SERDES_C_GEN_RST_CM0_PD_MASK     0x00000030
#define SERDES_C_GEN_RST_CM0_PD_SHIFT    4
/* Lane0 reset signal  active low */
#define SERDES_C_GEN_RST_LN0_RST_N       (1 << 6)
/* Lane1 reset signal  active low */
#define SERDES_C_GEN_RST_LN1_RST_N       (1 << 7)
/*
 * 0x0 – Normal / Active
 * 0x1 – Partial power down
 * 0x2 – Most blocks powered down (only LOS
 * active)
 * 0x3 – complete power down (IDDQ mode)
 * Can be asserted when Lane is in normal
 * mode.  These modes provide an increased
 * power savings compared to reset mode.
 * Signal is overridden by por_n_i so has no
 * affect in power on reset state
 */
#define SERDES_C_GEN_RST_LN0_PD_MASK     0x00000300
#define SERDES_C_GEN_RST_LN0_PD_SHIFT    8
/*
 * 0x0 – Normal / Active
 * 0x1 – Partial power down
 * 0x2 – Most blocks powered down (only LOS
 * active)
 * 0x3 – complete power down (IDDQ mode)
 * Can be asserted when Lane is in normal
 * mode.  These modes provide an increased
 * power savings compared to reset mode.
 * Signal is overridden by por_n_i so has no
 * affect in power on reset state
 */
#define SERDES_C_GEN_RST_LN1_PD_MASK     0x00000C00
#define SERDES_C_GEN_RST_LN1_PD_SHIFT    10

#define SERDES_C_GEN_RST_CPU_MEM_RESET   (1 << 12)

#define SERDES_C_GEN_RST_CPU_MEM_SHUTDOWN (1 << 13)

#define SERDES_C_GEN_RST_CAPRI_APB_RESET (1 << 14)

/**** status register ****/
/*
 * 0x0 – No error
 * 0x1 – PHY has an internal error
 */
#define SERDES_C_GEN_STATUS_ERR_O        (1 << 0)
/*
 * 0x0 – PHY is not ready to respond to
 * cm0_rst_n_i and cm0_pd_i[1:0]. The
 * signals should not be changed.
 * 0x1 - PHY is ready to respond to
 * cm0_rst_n_i and cm0_pd_i[1:0]
 */
#define SERDES_C_GEN_STATUS_CM0_RST_PD_READY (1 << 1)
/*
 * Indicates CMU PLL has locked to the
 * reference clock and all output clocks are at
 * the correct frequency
 */
#define SERDES_C_GEN_STATUS_CM0_OK_O     (1 << 2)
/*
 * 0x0 – PHY is not ready to respond to
 * ln0_rst_n and ln0_pd[1:0]. The signals
 * should not be changed.
 * 0x1 - PHY is ready to respond to lnX_rst_n_i
 * and lnX_pd_i[1:0]
 */
#define SERDES_C_GEN_STATUS_LN0_RST_PD_READY (1 << 3)
/*
 * 0x0 – PHY is not ready to respond to
 * ln1_rst_n_i and ln1_pd[1:0]. The signals
 * should not be changed.
 * 0x1 - PHY is ready to respond to lnX_rst_n_i
 * and lnX_pd_i[1:0]
 */
#define SERDES_C_GEN_STATUS_LN1_RST_PD_READY (1 << 4)
/*
 * Active low when the CPU performs a wait cycle (internally or externally
 * generated)
 */
#define SERDES_C_GEN_STATUS_CPU_WAITSTATE (1 << 5)

#define SERDES_C_GEN_STATUS_TBUS_MASK    0x000FFF00
#define SERDES_C_GEN_STATUS_TBUS_SHIFT   8

/**** cfg register ****/
/* 1- Swap 32 bit data on RX side */
#define SERDES_C_LANE_CFG_RX_LANE_SWAP   (1 << 0)
/* 1- Swap 32 bit data on TX side */
#define SERDES_C_LANE_CFG_TX_LANE_SWAP   (1 << 1)
/* 1 – invert rx data polarity */
#define SERDES_C_LANE_CFG_LN_CTRL_RXPOLARITY (1 << 2)
/* 1 – invert tx data polarity */
#define SERDES_C_LANE_CFG_TX_LANE_POLARITY (1 << 3)
/*
 * 0x0 –Data on lnX_txdata_o will not be
 * transmitted. Transmitter will be placed into
 * electrical idle.
 * 0x1 – Data on the active bits of
 * lnX_txdata_o will be transmitted
 */
#define SERDES_C_LANE_CFG_LN_CTRL_TX_EN  (1 << 4)
/*
 * Informs the PHY to bypass the output of the
 * analog LOS detector and instead rely upon
 * a protocol LOS mechanism in the SoC/ASIC
 * 0x0 – LOS operates as normal
 * 0x1 – Bypass analog LOS output and
 * instead rely upon protocol-level LOS
 * detection via input lnX_ctrl_los_eii_value
 */
#define SERDES_C_LANE_CFG_LN_CTRL_LOS_EII_EN (1 << 5)
/*
 * If lnX_ctrl_los_eii_en_i = 1 then Informs
 * the PHY that the received signal was lost
 */
#define SERDES_C_LANE_CFG_LN_CTRL_LOS_EII_VALUE (1 << 6)
/* One hot mux */
#define SERDES_C_LANE_CFG_TX_DATA_SRC_SELECT_MASK 0x00000F00
#define SERDES_C_LANE_CFG_TX_DATA_SRC_SELECT_SHIFT 8
/* 0x0 - 20-bit 0x1 – 40-bit */
#define SERDES_C_LANE_CFG_LN_CTRL_DATA_WIDTH (1 << 12)

/**** stat register ****/
/*
 * x0 – lane is not ready to send and receive data
 * 0x1 – lane is ready to send and receive data
 */
#define SERDES_C_LANE_STAT_LNX_STAT_OK   (1 << 0)
/*
 * 0x0 – received data run length has not
 * exceed the programmable run length
 * detector threshold
 * 0x1 – received data run length has
 * exceeded the programmable run length
 * detector threshold
 */
#define SERDES_C_LANE_STAT_LN_STAT_RUNLEN_ERR (1 << 1)
/*
 * 0x0 – data on lnX_rxdata_o are invalid
 * 0x1 – data on the active bits of
 * lnX_rxdata_o are valid
 */
#define SERDES_C_LANE_STAT_LN_STAT_RXVALID (1 << 2)
/*
 * Loss of Signal (LOS) indicator that includes
 * the combined functions of the digitally
 * assisted analog LOS, digital LOS, and
 * protocol LOS override features
 * 0x0 – Signal detected on  lnX_rxp_i /
 * lnX_rxm_i pins
 * 0x1 – No signal detected on lnX_rxp_i /
 * lnX_rxm_i pins
 */
#define SERDES_C_LANE_STAT_LN_STAT_LOS   (1 << 3)

#define SERDES_C_LANE_STAT_LN_STAT_LOS_DEGLITCH (1 << 4)

/**** reserved register ****/

#define SERDES_C_LANE_RESERVED_DEF_0_MASK 0x0000FFFF
#define SERDES_C_LANE_RESERVED_DEF_0_SHIFT 0

#define SERDES_C_LANE_RESERVED_DEF_1_MASK 0xFFFF0000
#define SERDES_C_LANE_RESERVED_DEF_1_SHIFT 16

#ifdef __cplusplus
}
#endif

#endif /* __AL_HAL_serdes_c_REGS_H__ */

/** @} end of ... group */


