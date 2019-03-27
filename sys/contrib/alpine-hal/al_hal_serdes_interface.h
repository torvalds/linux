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
 * @defgroup group_serdes_api API
 * SerDes HAL driver API
 * @ingroup group_serdes SerDes
 * @{
 *
 * @file   al_hal_serdes_interface.h
 *
 * @brief Header file for the SerDes HAL driver
 *
 */

#ifndef __AL_HAL_SERDES_INTERFACE_H__
#define __AL_HAL_SERDES_INTERFACE_H__

#include "al_hal_common.h"

/* *INDENT-OFF* */
#ifdef __cplusplus
extern "C" {
#endif
/* *INDENT-ON* */

enum al_serdes_type {
	AL_SRDS_TYPE_HSSP,
	AL_SRDS_TYPE_25G,
};

enum al_serdes_reg_page {
	/* Relevant to Serdes hssp and 25g */
	AL_SRDS_REG_PAGE_0_LANE_0 = 0,
	AL_SRDS_REG_PAGE_1_LANE_1,
	/* Relevant to Serdes hssp only */
	AL_SRDS_REG_PAGE_2_LANE_2,
	AL_SRDS_REG_PAGE_3_LANE_3,
	/* Relevant to Serdes hssp and 25g */
	AL_SRDS_REG_PAGE_4_COMMON,
	/* Relevant to Serdes hssp only */
	AL_SRDS_REG_PAGE_0123_LANES_0123 = 7,
	/* Relevant to Serdes 25g only */
	AL_SRDS_REG_PAGE_TOP,
};

/* Relevant to Serdes hssp only */
enum al_serdes_reg_type {
	AL_SRDS_REG_TYPE_PMA = 0,
	AL_SRDS_REG_TYPE_PCS,
};

enum al_serdes_lane {
	AL_SRDS_LANE_0 = AL_SRDS_REG_PAGE_0_LANE_0,
	AL_SRDS_LANE_1 = AL_SRDS_REG_PAGE_1_LANE_1,
	AL_SRDS_LANE_2 = AL_SRDS_REG_PAGE_2_LANE_2,
	AL_SRDS_LANE_3 = AL_SRDS_REG_PAGE_3_LANE_3,

	AL_SRDS_NUM_LANES,
	AL_SRDS_LANES_0123 = AL_SRDS_REG_PAGE_0123_LANES_0123,
};

/** Serdes loopback mode */
enum al_serdes_lb_mode {
	/** No loopback */
	AL_SRDS_LB_MODE_OFF,

	/**
	 * Transmits the untimed, partial equalized RX signal out the transmit
	 * IO pins.
	 * No clock used (untimed)
	 */
	AL_SRDS_LB_MODE_PMA_IO_UN_TIMED_RX_TO_TX,

	/**
	 * Loops back the TX serializer output into the CDR.
	 * CDR recovered bit clock used (without attenuation)
	 */
	AL_SRDS_LB_MODE_PMA_INTERNALLY_BUFFERED_SERIAL_TX_TO_RX,

	/**
	 * Loops back the TX driver IO signal to the RX IO pins
	 * CDR recovered bit clock used (only through IO)
	 */
	AL_SRDS_LB_MODE_PMA_SERIAL_TX_IO_TO_RX_IO,

	/**
	 * Parallel loopback from the PMA receive lane data ports, to the
	 * transmit lane data ports
	 * CDR recovered bit clock used
	 */
	AL_SRDS_LB_MODE_PMA_PARALLEL_RX_TO_TX,

	/** Loops received data after elastic buffer to transmit path */
	AL_SRDS_LB_MODE_PCS_PIPE,

	/** Loops TX data (to PMA) to RX path (instead of PMA data) */
	AL_SRDS_LB_MODE_PCS_NEAR_END,

	/** Loops receive data prior to interface block to transmit path */
	AL_SRDS_LB_MODE_PCS_FAR_END,
};

enum al_serdes_clk_freq {
	AL_SRDS_CLK_FREQ_NA,
	AL_SRDS_CLK_FREQ_100_MHZ,
	AL_SRDS_CLK_FREQ_125_MHZ,
	AL_SRDS_CLK_FREQ_156_MHZ,
};

enum al_serdes_clk_src {
	AL_SRDS_CLK_SRC_LOGIC_0,
	AL_SRDS_CLK_SRC_REF_PINS,
	AL_SRDS_CLK_SRC_R2L,
	AL_SRDS_CLK_SRC_R2L_PLL,
	AL_SRDS_CLK_SRC_L2R,
};

/** Serdes BIST pattern */
enum al_serdes_bist_pattern {
	AL_SRDS_BIST_PATTERN_USER,
	AL_SRDS_BIST_PATTERN_PRBS7,
	AL_SRDS_BIST_PATTERN_PRBS23,
	AL_SRDS_BIST_PATTERN_PRBS31,
	AL_SRDS_BIST_PATTERN_CLK1010,
};

/** SerDes group rate */
enum al_serdes_rate {
	AL_SRDS_RATE_1_8,
	AL_SRDS_RATE_1_4,
	AL_SRDS_RATE_1_2,
	AL_SRDS_RATE_FULL,
};

/** SerDes power mode */
enum al_serdes_pm {
	AL_SRDS_PM_PD,
	AL_SRDS_PM_P2,
	AL_SRDS_PM_P1,
	AL_SRDS_PM_P0S,
	AL_SRDS_PM_P0,
};

/**
 * Tx de-emphasis parameters
 */
enum al_serdes_tx_deemph_param {
	AL_SERDES_TX_DEEMP_C_ZERO,	/*< c(0) */
	AL_SERDES_TX_DEEMP_C_PLUS,	/*< c(1) */
	AL_SERDES_TX_DEEMP_C_MINUS,	/*< c(-1) */
};

struct al_serdes_adv_tx_params {
	/*
	 * select the input values location.
	 * When set to true the values will be taken from the internal registers
	 * that will be override with the next following parameters.
	 * When set to false the values will be taken from external pins (the
	 * other parameters in this case is not needed)
	 */
	al_bool				override;
	/*
	 * Transmit Amplitude control signal. Used to define the full-scale
	 * maximum swing of the driver.
	 *	000 - Not Supported
	 *	001 - 952mVdiff-pkpk
	 *	010 - 1024mVdiff-pkpk
	 *	011 - 1094mVdiff-pkpk
	 *	100 - 1163mVdiff-pkpk
	 *	101 - 1227mVdiff-pkpk
	 *	110 - 1283mVdiff-pkpk
	 *	111 - 1331mVdiff-pkpk
	 */
	uint8_t				amp;
	/* Defines the total number of driver units allocated in the driver */
	uint8_t				total_driver_units;
	/* Defines the total number of driver units allocated to the
	 * first post-cursor (C+1) tap. */
	uint8_t				c_plus_1;
	/* Defines the total number of driver units allocated to the
	 * second post-cursor (C+2) tap. */
	uint8_t				c_plus_2;
	/* Defines the total number of driver units allocated to the
	 * first pre-cursor (C-1) tap. */
	uint8_t				c_minus_1;
	/* TX driver Slew Rate control:
	 *	00 - 31ps
	 *	01 - 33ps
	 *	10 - 68ps
	 *	11 - 170ps
	 */
	uint8_t				slew_rate;
};

struct al_serdes_adv_rx_params {
	/*
	 * select the input values location.
	 * When set to true the values will be taken from the internal registers
	 * that will be override with the next following parameters.
	 * When set to false the values will be taken based in the equalization
	 * results (the other parameters in this case is not needed)
	 */
	al_bool				override;
	/* RX agc high frequency dc gain:
	 *	-3'b000: -3dB
	 *	-3'b001: -2.5dB
	 *	-3'b010: -2dB
	 *	-3'b011: -1.5dB
	 *	-3'b100: -1dB
	 *	-3'b101: -0.5dB
	 *	-3'b110: -0dB
	 *	-3'b111: 0.5dB
	 */
	uint8_t				dcgain;
	/* DFE post-shaping tap 3dB frequency
	 *	-3'b000: 684MHz
	 *	-3'b001: 576MHz
	 *	-3'b010: 514MHz
	 *	-3'b011: 435MHz
	 *	-3'b100: 354MHz
	 *	-3'b101: 281MHz
	 *	-3'b110: 199MHz
	 *	-3'b111: 125MHz
	 */
	uint8_t				dfe_3db_freq;
	/* DFE post-shaping tap gain
	 *	0: no pulse shaping tap
	 *	1: -24mVpeak
	 *	2: -45mVpeak
	 *	3: -64mVpeak
	 *	4: -80mVpeak
	 *	5: -93mVpeak
	 *	6: -101mVpeak
	 *	7: -105mVpeak
	 */
	uint8_t				dfe_gain;
	/* DFE first tap gain control
	 *	-4'b0000: +1mVpeak
	 *	-4'b0001: +10mVpeak
	 *	....
	 *	-4'b0110: +55mVpeak
	 *	-4'b0111: +64mVpeak
	 *	-4'b1000: -1mVpeak
	 *	-4'b1001: -10mVpeak
	 *	....
	 *	-4'b1110: -55mVpeak
	 *	-4'b1111: -64mVpeak
	 */
	uint8_t				dfe_first_tap_ctrl;
	/* DFE second tap gain control
	 *	-4'b0000: +0mVpeak
	 *	-4'b0001: +9mVpeak
	 *	....
	 *	-4'b0110: +46mVpeak
	 *	-4'b0111: +53mVpeak
	 *	-4'b1000: -0mVpeak
	 *	-4'b1001: -9mVpeak
	 *	....
	 *	-4'b1110: -46mVpeak
	 *	-4'b1111: -53mVpeak
	 */
	uint8_t				dfe_secound_tap_ctrl;
	/* DFE third tap gain control
	 *	-4'b0000: +0mVpeak
	 *	-4'b0001: +7mVpeak
	 *	....
	 *	-4'b0110: +38mVpeak
	 *	-4'b0111: +44mVpeak
	 *	-4'b1000: -0mVpeak
	 *	-4'b1001: -7mVpeak
	 *	....
	 *	-4'b1110: -38mVpeak
	 *	-4'b1111: -44mVpeak
	 */
	uint8_t				dfe_third_tap_ctrl;
	/* DFE fourth tap gain control
	 *	-4'b0000: +0mVpeak
	 *	-4'b0001: +6mVpeak
	 *	....
	 *	-4'b0110: +29mVpeak
	 *	-4'b0111: +33mVpeak
	 *	-4'b1000: -0mVpeak
	 *	-4'b1001: -6mVpeak
	 *	....
	 *	-4'b1110: -29mVpeak
	 *	-4'b1111: -33mVpeak
	 */
	uint8_t				dfe_fourth_tap_ctrl;
	/* Low frequency agc gain (att) select
	 *	-3'b000: Disconnected
	 *	-3'b001: -18.5dB
	 *	-3'b010: -12.5dB
	 *	-3'b011: -9dB
	 *	-3'b100: -6.5dB
	 *	-3'b101: -4.5dB
	 *	-3'b110: -2.9dB
	 *	-3'b111: -1.6dB
	 */
	uint8_t				low_freq_agc_gain;
	/* Provides a RX Equalizer pre-hint, prior to beginning
	 * adaptive equalization */
	uint8_t				precal_code_sel;
	/* High frequency agc boost control
	 *	Min d0: Boost ~4dB
	 *	Max d31: Boost ~20dB
	 */
	uint8_t				high_freq_agc_boost;
};

struct al_serdes_25g_adv_rx_params {
	/* ATT (PLE Flat-Band Gain) */
	uint8_t				att;
	/* APG (CTLE's Flat-Band Gain) */
	uint8_t				apg;
	/* LFG (Low-Freq Gain) */
	uint8_t				lfg;
	/* HFG (High-Freq Gain) */
	uint8_t				hfg;
	/* MBG (MidBand-Freq-knob Gain) */
	uint8_t				mbg;
	/* MBF (MidBand-Freq-knob Frequency position Gain) */
	uint8_t				mbf;
	/* DFE Tap1 even#0 Value */
	int8_t				dfe_first_tap_even0_ctrl;
	/* DFE Tap1 even#1 Value */
	int8_t				dfe_first_tap_even1_ctrl;
	/* DFE Tap1 odd#0 Value */
	int8_t				dfe_first_tap_odd0_ctrl;
	/* DFE Tap1 odd#1 Value */
	int8_t				dfe_first_tap_odd1_ctrl;
	/* DFE Tap2 Value */
	int8_t				dfe_second_tap_ctrl;
	/* DFE Tap3 Value */
	int8_t				dfe_third_tap_ctrl;
	/* DFE Tap4 Value */
	int8_t				dfe_fourth_tap_ctrl;
	/* DFE Tap5 Value */
	int8_t				dfe_fifth_tap_ctrl;
};

struct al_serdes_25g_tx_diag_info {
	uint8_t regulated_supply;
	int8_t dcd_trim;
	uint8_t clk_delay;
	uint8_t calp_multiplied_by_2;
	uint8_t caln_multiplied_by_2;
};

struct al_serdes_25g_rx_diag_info {
	int8_t los_offset;
	int8_t agc_offset;
	int8_t leq_gainstage_offset;
	int8_t leq_eq1_offset;
	int8_t leq_eq2_offset;
	int8_t leq_eq3_offset;
	int8_t leq_eq4_offset;
	int8_t leq_eq5_offset;
	int8_t summer_even_offset;
	int8_t summer_odd_offset;
	int8_t vscan_even_offset;
	int8_t vscan_odd_offset;
	int8_t data_slicer_even0_offset;
	int8_t data_slicer_even1_offset;
	int8_t data_slicer_odd0_offset;
	int8_t data_slicer_odd1_offset;
	int8_t edge_slicer_even_offset;
	int8_t edge_slicer_odd_offset;
	int8_t eye_slicer_even_offset;
	int8_t eye_slicer_odd_offset;
	uint8_t cdr_clk_i;
	uint8_t cdr_clk_q;
	uint8_t cdr_dll;
	uint8_t cdr_vco_dosc;
	uint8_t cdr_vco_fr;
	uint16_t cdr_dlpf;
	uint8_t ple_resistance;
	uint8_t rx_term_mode;
	uint8_t rx_coupling;
	uint8_t rx_term_cal_code;
	uint8_t rx_sheet_res_cal_code;
};

/**
 * SRIS parameters
 */
struct al_serdes_sris_params {
	/* Controls the frequency accuracy threshold (ppm) for lock detection CDR */
	uint16_t	ppm_drift_count;
	/* Controls the frequency accuracy threshold (ppm) for lock detection in the CDR */
	uint16_t	ppm_drift_max;
	/* Controls the frequency accuracy threshold (ppm) for lock detection in PLL */
	uint16_t	synth_ppm_drift_max;
	/* Elastic buffer full threshold for PCIE modes: GEN1/GEN2 */
	uint8_t		full_d2r1;
	/* Elastic buffer full threshold for PCIE modes: GEN3 */
	uint8_t		full_pcie_g3;
	/* Elastic buffer midpoint threshold.
	 * Sets the depth of the buffer while in PCIE mode, GEN1/GEN2
	 */
	uint8_t		rd_threshold_d2r1;
	/* Elastic buffer midpoint threshold.
	 * Sets the depth of the buffer while in PCIE mode, GEN3
	 */
	uint8_t		rd_threshold_pcie_g3;
};

/** SerDes PCIe Rate - values are important for proper behavior */
enum al_serdes_pcie_rate {
	AL_SRDS_PCIE_RATE_GEN1 = 0,
	AL_SRDS_PCIE_RATE_GEN2,
	AL_SRDS_PCIE_RATE_GEN3,
};

struct al_serdes_grp_obj {
	void __iomem				*regs_base;

	/**
	 * get the type of the serdes.
	 * Must be implemented for all SerDes unit.
	 *
	 * @return the serdes type.
	 */
	enum al_serdes_type (*type_get)(void);

	/**
	 * Reads a SERDES internal register
	 *
	 * @param obj		The object context
	 * @param page		The SERDES register page within the group
	 * @param type		The SERDES register type (PMA /PCS)
	 * @param offset	The SERDES register offset (0 - 4095)
	 * @param data		The read data
	 *
	 * @return 0 if no error found.
	 */
	int (*reg_read)(struct al_serdes_grp_obj *, enum al_serdes_reg_page,
			enum al_serdes_reg_type, uint16_t, uint8_t *);

	/**
	 * Writes a SERDES internal register
	 *
	 * @param obj		The object context
	 * @param page		The SERDES register page within the group
	 * @param type		The SERDES register type (PMA /PCS)
	 * @param offset	The SERDES register offset (0 - 4095)
	 * @param data		The data to write
	 *
	 * @return 0 if no error found.
	 */
	int (*reg_write)(struct al_serdes_grp_obj *, enum al_serdes_reg_page,
			enum al_serdes_reg_type, uint16_t, uint8_t);

	/**
	 * Enable BIST required overrides
	 *
	 * @param obj		The object context
	 * @param grp		The SERDES group
	 * @param rate		The required speed rate
	 */
	void (*bist_overrides_enable)(struct al_serdes_grp_obj *, enum al_serdes_rate);
	/**
	 * Disable BIST required overrides
	 *
	 * @param obj		The object context
	 * @param grp		The SERDES group
	 * @param rate		The required speed rate
	 */
	void (*bist_overrides_disable)(struct al_serdes_grp_obj *);
	/**
	 * Rx rate change
	 *
	 * @param obj		The object context
	 * @param grp		The SERDES group
	 * @param rate		The Rx required rate
	 */
	void (*rx_rate_change)(struct al_serdes_grp_obj *, enum al_serdes_rate);
	/**
	 * SERDES lane Rx rate change software flow enable
	 *
	 * @param obj		The object context
	 * @param lane		The SERDES lane within the group
	 */
	void (*rx_rate_change_sw_flow_en)(struct al_serdes_grp_obj *, enum al_serdes_lane);
	/**
	 * SERDES lane Rx rate change software flow disable
	 *
	 * @param obj		The object context
	 * @param lane		The SERDES lane within the group
	 */
	void (*rx_rate_change_sw_flow_dis)(struct al_serdes_grp_obj *, enum al_serdes_lane);
	/**
	 * PCIe lane rate override check
	 *
	 * @param obj		The object context
	 * @param grp		The SERDES group
	 * @param lane		The SERDES lane within the group
	 *
	 * @returns	AL_TRUE if the override is enabled
	 */
	al_bool (*pcie_rate_override_is_enabled)(struct al_serdes_grp_obj *, enum al_serdes_lane);
	/**
	 * PCIe lane rate override control
	 *
	 * @param obj		The object context
	 * @param lane		The SERDES lane within the group
	 * @param en		Enable/disable
	 */
	void (*pcie_rate_override_enable_set)(struct al_serdes_grp_obj *, enum al_serdes_lane,
					      al_bool en);
	/**
	 * PCIe lane rate get
	 *
	 * @param obj		The object context
	 * @param lane		The SERDES lane within the group
	 */
	enum al_serdes_pcie_rate (*pcie_rate_get)(struct al_serdes_grp_obj *, enum al_serdes_lane);
	/**
	 * PCIe lane rate set
	 *
	 * @param obj		The object context
	 * @param lane		The SERDES lane within the group
	 * @param rate		The required rate
	 */
	void (*pcie_rate_set)(struct al_serdes_grp_obj *, enum al_serdes_lane,
			      enum al_serdes_pcie_rate rate);
	/**
	 * SERDES group power mode control
	 *
	 * @param obj		The object context
	 * @param grp		The SERDES group
	 * @param pm		The required power mode
	 */
	void (*group_pm_set)(struct al_serdes_grp_obj *, enum al_serdes_pm);
	/**
	 * SERDES lane power mode control
	 *
	 * @param obj		The object context
	 * @param grp		The SERDES group
	 * @param lane		The SERDES lane within the group
	 * @param rx_pm		The required RX power mode
	 * @param tx_pm		The required TX power mode
	 */
	void (*lane_pm_set)(struct al_serdes_grp_obj *, enum al_serdes_lane,
			    enum al_serdes_pm, enum al_serdes_pm);

	/**
	 * SERDES group PMA hard reset
	 * Controls Serdes group PMA hard reset
	 *
	 * @param obj		The object context
	 * @param grp		The SERDES group
	 * @param enable	Enable/disable hard reset
	 */
	void (*pma_hard_reset_group)(struct al_serdes_grp_obj *, al_bool);
	/**
	 * SERDES lane PMA hard reset
	 * Controls Serdes lane PMA hard reset
	 *
	 * @param obj		The object context
	 * @param lane		The SERDES lane within the group
	 * @param enable	Enable/disable hard reset
	 */
	void (*pma_hard_reset_lane)(struct al_serdes_grp_obj *, enum al_serdes_lane, al_bool);
	/**
	 * Configure SERDES loopback
	 * Controls the loopback
	 *
	 * @param obj		The object context
	 * @param lane		The SERDES lane within the group
	 * @param mode		The requested loopback mode
	 */
	void (*loopback_control)(struct al_serdes_grp_obj *, enum al_serdes_lane,
				 enum al_serdes_lb_mode);
	/**
	 * SERDES BIST pattern selection
	 * Selects the BIST pattern to be used
	 *
	 * @param obj		The object context
	 * @param pattern	The pattern to set
	 * @param user_data	The pattern user data (when pattern == AL_SRDS_BIST_PATTERN_USER)
	 *			80 bits (8 bytes array)
	 */
	void (*bist_pattern_select)(struct al_serdes_grp_obj *,
				    enum al_serdes_bist_pattern, uint8_t *);
	/**
	 * SERDES BIST TX Enable
	 * Enables/disables TX BIST per lane
	 *
	 * @param obj		The object context
	 * @param lane		The SERDES lane within the group
	 * @param enable	Enable or disable TX BIST
	 */
	void (*bist_tx_enable)(struct al_serdes_grp_obj *, enum al_serdes_lane, al_bool);
	/**
	 * SERDES BIST TX single bit error injection
	 * Injects single bit error during a TX BIST
	 *
	 * @param obj		The object context
	 */
	void (*bist_tx_err_inject)(struct al_serdes_grp_obj *);
	/**
	 * SERDES BIST RX Enable
	 * Enables/disables RX BIST per lane
	 *
	 * @param obj		The object context
	 * @param lane		The SERDES lane within the group
	 * @param enable	Enable or disable TX BIST
	 */
	void (*bist_rx_enable)(struct al_serdes_grp_obj *, enum al_serdes_lane, al_bool);
	/**
	 * SERDES BIST RX status
	 * Checks the RX BIST status for a specific SERDES lane
	 *
	 * @param obj		The object context
	 * @param lane		The SERDES lane within the group
	 * @param is_locked	An indication whether RX BIST is locked
	 * @param err_cnt_overflow	An indication whether error count overflow occured
	 * @param err_cnt	Current bit error count
	 */
	void (*bist_rx_status)(struct al_serdes_grp_obj *, enum al_serdes_lane, al_bool *,
			       al_bool *, uint32_t *);

	/**
	 * Set the tx de-emphasis to preset values
	 *
	 * @param obj		The object context
	 * @param lane		The SERDES lane within the group
	 *
	 */
	void (*tx_deemph_preset)(struct al_serdes_grp_obj *, enum al_serdes_lane);
	/**
	 * Increase tx de-emphasis param.
	 *
	 * @param obj		The object context
	 * @param lane		The SERDES lane within the group
	 * @param param		which tx de-emphasis to change
	 *
	 * @return false in case max is reached. true otherwise.
	 */
	al_bool (*tx_deemph_inc)(struct al_serdes_grp_obj *, enum al_serdes_lane,
				 enum al_serdes_tx_deemph_param);
	/**
	 * Decrease tx de-emphasis param.
	 *
	 * @param obj		The object context
	 * @param lane		The SERDES lane within the group
	 * @param param		which tx de-emphasis to change
	 *
	 * @return false in case min is reached. true otherwise.
	 */
	al_bool (*tx_deemph_dec)(struct al_serdes_grp_obj *, enum al_serdes_lane,
				 enum al_serdes_tx_deemph_param);
	/**
	 * run Rx eye measurement.
	 *
	 * @param obj		The object context
	 * @param lane		The SERDES lane within the group
	 * @param timeout	timeout in uSec
	 * @param value		Rx eye measurement value
	 *			(0 - completely closed eye, 0xffff - completely open eye).
	 *
	 * @return 0 if no error found.
	 */
	int (*eye_measure_run)(struct al_serdes_grp_obj *, enum al_serdes_lane,
			       uint32_t, unsigned int *);
	/**
	 * Eye diagram single sampling
	 *
	 * @param obj		The object context
	 * @param lane		The SERDES lane within the group
	 * @param x		Sampling X position (0 - 63 --> -1.00 UI ... 1.00 UI)
	 * @param y		Sampling Y position (0 - 62 --> 500mV ... -500mV)
	 * @param timeout	timeout in uSec
	 * @param value		Eye diagram sample value (BER - 0x0000 - 0xffff)
	 *
	 * @return 0 if no error found.
	 */
	int (*eye_diag_sample)(struct al_serdes_grp_obj *, enum al_serdes_lane,
			       unsigned int, int, unsigned int, unsigned int *);

	/**
	 * Eye diagram full run
	 *
	 * @param obj			The object context
	 * @param lane			The SERDES lane within the group
	 * @param x_start		Sampling from X position
	 * @param x_stop		Sampling to X position
	 * @param x_step		jump in x_step
	 * @param y_start		Sampling from Y position
	 * @param y_stop		Sampling to Y position
	 * @param y_step		jump in y_step
	 * @param num_bits_per_sample	How many bits to check
	 * @param buf			array of results
	 * @param buf_size		array size - must be equal to
	 *				(((y_stop - y_start) / y_step) + 1) *
	 *				(((x_stop - x_start) / x_step) + 1)
	 *
	 * @return 0 if no error found.
	 */
	int (*eye_diag_run)(struct al_serdes_grp_obj	*, enum al_serdes_lane,
			    int, int, unsigned int, int, int, unsigned int, uint64_t, uint64_t *,
			    uint32_t);
	/**
	 * Check if signal is detected
	 *
	 * @param obj		The object context
	 * @param lane		The SERDES lane within the group
	 *
	 * @return true if signal is detected. false otherwise.
	 */
	al_bool (*signal_is_detected)(struct al_serdes_grp_obj *, enum al_serdes_lane);

	/**
	 * Check if CDR is locked
	 *
	 * @param obj		The object context
	 * @param lane		The SERDES lane within the group
	 *
	 * @return true if cdr is locked. false otherwise.
	 */
	al_bool (*cdr_is_locked)(struct al_serdes_grp_obj *, enum al_serdes_lane);

	/**
	 * Check if rx is valid for this lane
	 *
	 * @param obj		The object context
	 * @param lane		The SERDES lane within the group
	 *
	 * @return true if rx is valid. false otherwise.
	 */
	al_bool (*rx_valid)(struct al_serdes_grp_obj *, enum al_serdes_lane);

	/**
	 * configure tx advanced parameters
	 *
	 * @param obj		The object context
	 * @param lane		The SERDES lane within the group
	 * @param params	pointer to the tx parameters
	 */
	void (*tx_advanced_params_set)(struct al_serdes_grp_obj *, enum al_serdes_lane, void *);
	/**
	 * read tx advanced parameters
	 *
	 * @param obj		The object context
	 * @param lane		The SERDES lane within the group
	 * @param params	pointer to the tx parameters
	 */
	void (*tx_advanced_params_get)(struct al_serdes_grp_obj *, enum al_serdes_lane, void *);
	/**
	 * configure rx advanced parameters
	 *
	 * @param obj		The object context
	 * @param lane		The SERDES lane within the group
	 * @param params	pointer to the rx parameters
	 */
	void (*rx_advanced_params_set)(struct al_serdes_grp_obj *, enum al_serdes_lane, void *);
	/**
	 * read rx advanced parameters
	 *
	 * @param obj		The object context
	 * @param lane		The SERDES lane within the group
	 * @param params	pointer to the rx parameters
	 */
	void (*rx_advanced_params_get)(struct al_serdes_grp_obj *, enum al_serdes_lane, void *);
	/**
	 *  Switch entire SerDes group to SGMII mode based on 156.25 Mhz reference clock
	 *
	 * @param obj		The object context
	 *
	 */
	void (*mode_set_sgmii)(struct al_serdes_grp_obj *);
	/**
	 *  Switch entire SerDes group to KR mode based on 156.25 Mhz reference clock
	 *
	 * @param obj		The object context
	 *
	 */
	void (*mode_set_kr)(struct al_serdes_grp_obj *);
	/**
	 * performs SerDes HW equalization test and update equalization parameters
	 *
	 * @param obj		the object context
	 * @param lane		The SERDES lane within the group
	 */
	int (*rx_equalization)(struct al_serdes_grp_obj *, enum al_serdes_lane);
	/**
	 * performs Rx equalization and compute the width and height of the eye
	 *
	 * @param obj		the object context
	 * @param lane		The SERDES lane within the group
	 * @param width		the output width of the eye
	 * @param height	the output height of the eye
	 */
	int (*calc_eye_size)(struct al_serdes_grp_obj *, enum al_serdes_lane, int *, int *);
	/**
	 * SRIS: Separate Refclk Independent SSC (Spread Spectrum Clocking)
	 * Currently available only for PCIe interfaces.
	 * When working with local Refclk, same SRIS configuration in both serdes sides
	 * (EP and RC in PCIe interface) is required.
	 *
	 * performs SRIS configuration according to params
	 *
	 * @param obj		the object context
	 * @param params	the SRIS parameters
	 */
	void (*sris_config)(struct al_serdes_grp_obj *, void *);
	/**
	 * set SERDES dcgain parameter
	 *
	 * @param obj		the object context
	 * @param dcgain	dcgain value to set
	 */
	void (*dcgain_set)(struct al_serdes_grp_obj *, uint8_t);
	/**
	 * read tx diagnostics info
	 *
	 * @param obj		The object context
	 * @param lane		The SERDES lane within the group
	 * @param params	pointer to the tx diagnostics info structure
	 */
	void (*tx_diag_info_get)(struct al_serdes_grp_obj *, enum al_serdes_lane, void*);
	/**
	 * read rx diagnostics info
	 *
	 * @param obj		The object context
	 * @param lane		The SERDES lane within the group
	 * @param params	pointer to the rx diagnostics info structure
	 */
	void (*rx_diag_info_get)(struct al_serdes_grp_obj *, enum al_serdes_lane, void*);
};


/* *INDENT-OFF* */
#ifdef __cplusplus
}
#endif

/* *INDENT-ON* */
#endif		/* __AL_HAL_SERDES_INTERFACE_H__ */

/** @} end of SERDES group */

