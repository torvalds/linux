/*
 * ADV7343 header file
 *
 * Copyright (C) 2009 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed .as is. WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef ADV7343_H
#define ADV7343_H

#define ADV7343_COMPOSITE_ID	(0)
#define ADV7343_COMPONENT_ID	(1)
#define ADV7343_SVIDEO_ID	(2)

/**
 * adv7343_power_mode - power mode configuration.
 * @sleep_mode: on enable the current consumption is reduced to micro ampere
 *		level. All DACs and the internal PLL circuit are disabled.
 *		Registers can be read from and written in sleep mode.
 * @pll_control: PLL and oversampling control. This control allows internal
 *		 PLL 1 circuit to be powered down and the oversampling to be
 *		 switched off.
 * @dac_1: power on/off DAC 1.
 * @dac_2: power on/off DAC 2.
 * @dac_3: power on/off DAC 3.
 * @dac_4: power on/off DAC 4.
 * @dac_5: power on/off DAC 5.
 * @dac_6: power on/off DAC 6.
 *
 * Power mode register (Register 0x0), for more info refer REGISTER MAP ACCESS
 * section of datasheet[1], table 17 page no 30.
 *
 * [1] http://www.analog.com/static/imported-files/data_sheets/ADV7342_7343.pdf
 */
struct adv7343_power_mode {
	bool sleep_mode;
	bool pll_control;
	bool dac_1;
	bool dac_2;
	bool dac_3;
	bool dac_4;
	bool dac_5;
	bool dac_6;
};

/**
 * struct adv7343_sd_config - SD Only Output Configuration.
 * @sd_dac_out1: Configure SD DAC Output 1.
 * @sd_dac_out2: Configure SD DAC Output 2.
 */
struct adv7343_sd_config {
	/* SD only Output Configuration */
	bool sd_dac_out1;
	bool sd_dac_out2;
};

/**
 * struct adv7343_platform_data - Platform data values and access functions.
 * @mode_config: Configuration for power mode.
 * @sd_config: SD Only Configuration.
 */
struct adv7343_platform_data {
	struct adv7343_power_mode mode_config;
	struct adv7343_sd_config sd_config;
};

#endif				/* End of #ifndef ADV7343_H */
