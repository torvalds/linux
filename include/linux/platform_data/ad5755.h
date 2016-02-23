/*
 * Copyright 2012 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */
#ifndef __LINUX_PLATFORM_DATA_AD5755_H__
#define __LINUX_PLATFORM_DATA_AD5755_H__

enum ad5755_mode {
	AD5755_MODE_VOLTAGE_0V_5V		= 0,
	AD5755_MODE_VOLTAGE_0V_10V		= 1,
	AD5755_MODE_VOLTAGE_PLUSMINUS_5V	= 2,
	AD5755_MODE_VOLTAGE_PLUSMINUS_10V	= 3,
	AD5755_MODE_CURRENT_4mA_20mA		= 4,
	AD5755_MODE_CURRENT_0mA_20mA		= 5,
	AD5755_MODE_CURRENT_0mA_24mA		= 6,
};

enum ad5755_dc_dc_phase {
	AD5755_DC_DC_PHASE_ALL_SAME_EDGE		= 0,
	AD5755_DC_DC_PHASE_A_B_SAME_EDGE_C_D_OPP_EDGE	= 1,
	AD5755_DC_DC_PHASE_A_C_SAME_EDGE_B_D_OPP_EDGE	= 2,
	AD5755_DC_DC_PHASE_90_DEGREE			= 3,
};

enum ad5755_dc_dc_freq {
	AD5755_DC_DC_FREQ_250kHZ = 0,
	AD5755_DC_DC_FREQ_410kHZ = 1,
	AD5755_DC_DC_FREQ_650kHZ = 2,
};

enum ad5755_dc_dc_maxv {
	AD5755_DC_DC_MAXV_23V	= 0,
	AD5755_DC_DC_MAXV_24V5	= 1,
	AD5755_DC_DC_MAXV_27V	= 2,
	AD5755_DC_DC_MAXV_29V5	= 3,
};

enum ad5755_slew_rate {
	AD5755_SLEW_RATE_64k	= 0,
	AD5755_SLEW_RATE_32k	= 1,
	AD5755_SLEW_RATE_16k	= 2,
	AD5755_SLEW_RATE_8k	= 3,
	AD5755_SLEW_RATE_4k	= 4,
	AD5755_SLEW_RATE_2k	= 5,
	AD5755_SLEW_RATE_1k	= 6,
	AD5755_SLEW_RATE_500	= 7,
	AD5755_SLEW_RATE_250	= 8,
	AD5755_SLEW_RATE_125	= 9,
	AD5755_SLEW_RATE_64	= 10,
	AD5755_SLEW_RATE_32	= 11,
	AD5755_SLEW_RATE_16	= 12,
	AD5755_SLEW_RATE_8	= 13,
	AD5755_SLEW_RATE_4	= 14,
	AD5755_SLEW_RATE_0_5	= 15,
};

enum ad5755_slew_step_size {
	AD5755_SLEW_STEP_SIZE_1 = 0,
	AD5755_SLEW_STEP_SIZE_2 = 1,
	AD5755_SLEW_STEP_SIZE_4 = 2,
	AD5755_SLEW_STEP_SIZE_8 = 3,
	AD5755_SLEW_STEP_SIZE_16 = 4,
	AD5755_SLEW_STEP_SIZE_32 = 5,
	AD5755_SLEW_STEP_SIZE_64 = 6,
	AD5755_SLEW_STEP_SIZE_128 = 7,
	AD5755_SLEW_STEP_SIZE_256 = 8,
};

/**
 * struct ad5755_platform_data - AD5755 DAC driver platform data
 * @ext_dc_dc_compenstation_resistor: Whether an external DC-DC converter
 * compensation register is used.
 * @dc_dc_phase: DC-DC converter phase.
 * @dc_dc_freq: DC-DC converter frequency.
 * @dc_dc_maxv: DC-DC maximum allowed boost voltage.
 * @dac.mode: The mode to be used for the DAC output.
 * @dac.ext_current_sense_resistor: Whether an external current sense resistor
 * is used.
 * @dac.enable_voltage_overrange: Whether to enable 20% voltage output overrange.
 * @dac.slew.enable: Whether to enable digital slew.
 * @dac.slew.rate: Slew rate of the digital slew.
 * @dac.slew.step_size: Slew step size of the digital slew.
 **/
struct ad5755_platform_data {
	bool ext_dc_dc_compenstation_resistor;
	enum ad5755_dc_dc_phase dc_dc_phase;
	enum ad5755_dc_dc_freq dc_dc_freq;
	enum ad5755_dc_dc_maxv dc_dc_maxv;

	struct {
		enum ad5755_mode mode;
		bool ext_current_sense_resistor;
		bool enable_voltage_overrange;
		struct {
			bool enable;
			enum ad5755_slew_rate rate;
			enum ad5755_slew_step_size step_size;
		} slew;
	} dac[4];
};

#endif
