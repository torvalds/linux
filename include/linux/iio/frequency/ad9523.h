/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AD9523 SPI Low Jitter Clock Generator
 *
 * Copyright 2012 Analog Devices Inc.
 */

#ifndef IIO_FREQUENCY_AD9523_H_
#define IIO_FREQUENCY_AD9523_H_

enum outp_drv_mode {
	TRISTATE,
	LVPECL_8mA,
	LVDS_4mA,
	LVDS_7mA,
	HSTL0_16mA,
	HSTL1_8mA,
	CMOS_CONF1,
	CMOS_CONF2,
	CMOS_CONF3,
	CMOS_CONF4,
	CMOS_CONF5,
	CMOS_CONF6,
	CMOS_CONF7,
	CMOS_CONF8,
	CMOS_CONF9
};

enum ref_sel_mode {
	NONEREVERTIVE_STAY_ON_REFB,
	REVERT_TO_REFA,
	SELECT_REFA,
	SELECT_REFB,
	EXT_REF_SEL
};

/**
 * struct ad9523_channel_spec - Output channel configuration
 *
 * @channel_num: Output channel number.
 * @divider_output_invert_en: Invert the polarity of the output clock.
 * @sync_ignore_en: Ignore chip-level SYNC signal.
 * @low_power_mode_en: Reduce power used in the differential output modes.
 * @use_alt_clock_src: Channel divider uses alternative clk source.
 * @output_dis: Disables, powers down the entire channel.
 * @driver_mode: Output driver mode (logic level family).
 * @divider_phase: Divider initial phase after a SYNC. Range 0..63
		   LSB = 1/2 of a period of the divider input clock.
 * @channel_divider: 10-bit channel divider.
 * @extended_name: Optional descriptive channel name.
 */

struct ad9523_channel_spec {
	unsigned		channel_num;
	bool			divider_output_invert_en;
	bool			sync_ignore_en;
	bool			low_power_mode_en;
				 /* CH0..CH3 VCXO, CH4..CH9 VCO2 */
	bool			use_alt_clock_src;
	bool			output_dis;
	enum outp_drv_mode	driver_mode;
	unsigned char		divider_phase;
	unsigned short		channel_divider;
	char			extended_name[16];
};

enum pll1_rzero_resistor {
	RZERO_883_OHM,
	RZERO_677_OHM,
	RZERO_341_OHM,
	RZERO_135_OHM,
	RZERO_10_OHM,
	RZERO_USE_EXT_RES = 8,
};

enum rpole2_resistor {
	RPOLE2_900_OHM,
	RPOLE2_450_OHM,
	RPOLE2_300_OHM,
	RPOLE2_225_OHM,
};

enum rzero_resistor {
	RZERO_3250_OHM,
	RZERO_2750_OHM,
	RZERO_2250_OHM,
	RZERO_2100_OHM,
	RZERO_3000_OHM,
	RZERO_2500_OHM,
	RZERO_2000_OHM,
	RZERO_1850_OHM,
};

enum cpole1_capacitor {
	CPOLE1_0_PF,
	CPOLE1_8_PF,
	CPOLE1_16_PF,
	CPOLE1_24_PF,
	_CPOLE1_24_PF, /* place holder */
	CPOLE1_32_PF,
	CPOLE1_40_PF,
	CPOLE1_48_PF,
};

/**
 * struct ad9523_platform_data - platform specific information
 *
 * @vcxo_freq: External VCXO frequency in Hz
 * @refa_diff_rcv_en: REFA differential/single-ended input selection.
 * @refb_diff_rcv_en: REFB differential/single-ended input selection.
 * @zd_in_diff_en: Zero Delay differential/single-ended input selection.
 * @osc_in_diff_en: OSC differential/ single-ended input selection.
 * @refa_cmos_neg_inp_en: REFA single-ended neg./pos. input enable.
 * @refb_cmos_neg_inp_en: REFB single-ended neg./pos. input enable.
 * @zd_in_cmos_neg_inp_en: Zero Delay single-ended neg./pos. input enable.
 * @osc_in_cmos_neg_inp_en: OSC single-ended neg./pos. input enable.
 * @refa_r_div: PLL1 10-bit REFA R divider.
 * @refb_r_div: PLL1 10-bit REFB R divider.
 * @pll1_feedback_div: PLL1 10-bit Feedback N divider.
 * @pll1_charge_pump_current_nA: Magnitude of PLL1 charge pump current (nA).
 * @zero_delay_mode_internal_en: Internal, external Zero Delay mode selection.
 * @osc_in_feedback_en: PLL1 feedback path, local feedback from
 *			the OSC_IN receiver or zero delay mode
 * @pll1_loop_filter_rzero: PLL1 Loop Filter Zero Resistor selection.
 * @ref_mode: Reference selection mode.
 * @pll2_charge_pump_current_nA: Magnitude of PLL2 charge pump current (nA).
 * @pll2_ndiv_a_cnt: PLL2 Feedback N-divider, A Counter, range 0..4.
 * @pll2_ndiv_b_cnt: PLL2 Feedback N-divider, B Counter, range 0..63.
 * @pll2_freq_doubler_en: PLL2 frequency doubler enable.
 * @pll2_r2_div: PLL2 R2 divider, range 0..31.
 * @pll2_vco_div_m1: VCO1 divider, range 3..5.
 * @pll2_vco_div_m2: VCO2 divider, range 3..5.
 * @rpole2: PLL2 loop filter Rpole resistor value.
 * @rzero: PLL2 loop filter Rzero resistor value.
 * @cpole1: PLL2 loop filter Cpole capacitor value.
 * @rzero_bypass_en: PLL2 loop filter Rzero bypass enable.
 * @num_channels: Array size of struct ad9523_channel_spec.
 * @channels: Pointer to channel array.
 * @name: Optional alternative iio device name.
 */

struct ad9523_platform_data {
	unsigned long vcxo_freq;

	/* Differential/ Single-Ended Input Configuration */
	bool				refa_diff_rcv_en;
	bool				refb_diff_rcv_en;
	bool				zd_in_diff_en;
	bool				osc_in_diff_en;

	/*
	 * Valid if differential input disabled
	 * if false defaults to pos input
	 */
	bool				refa_cmos_neg_inp_en;
	bool				refb_cmos_neg_inp_en;
	bool				zd_in_cmos_neg_inp_en;
	bool				osc_in_cmos_neg_inp_en;

	/* PLL1 Setting */
	unsigned short			refa_r_div;
	unsigned short			refb_r_div;
	unsigned short			pll1_feedback_div;
	unsigned short			pll1_charge_pump_current_nA;
	bool				zero_delay_mode_internal_en;
	bool				osc_in_feedback_en;
	enum pll1_rzero_resistor	pll1_loop_filter_rzero;

	/* Reference */
	enum ref_sel_mode		ref_mode;

	/* PLL2 Setting */
	unsigned int			pll2_charge_pump_current_nA;
	unsigned char			pll2_ndiv_a_cnt;
	unsigned char			pll2_ndiv_b_cnt;
	bool				pll2_freq_doubler_en;
	unsigned char			pll2_r2_div;
	unsigned char			pll2_vco_div_m1; /* 3..5 */
	unsigned char			pll2_vco_div_m2; /* 3..5 */

	/* Loop Filter PLL2 */
	enum rpole2_resistor		rpole2;
	enum rzero_resistor		rzero;
	enum cpole1_capacitor		cpole1;
	bool				rzero_bypass_en;

	/* Output Channel Configuration */
	int				num_channels;
	struct ad9523_channel_spec	*channels;

	char				name[SPI_NAME_SIZE];
};

#endif /* IIO_FREQUENCY_AD9523_H_ */
