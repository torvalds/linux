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
 * @dac: array to configure power on/off DAC's 1..6
 *
 * Power mode register (Register 0x0), for more info refer REGISTER MAP ACCESS
 * section of datasheet[1], table 17 page no 30.
 *
 * [1] http://www.analog.com/static/imported-files/data_sheets/ADV7342_7343.pdf
 */
struct adv7343_power_mode {
	bool sleep_mode;
	bool pll_control;
	u32 dac[6];
};

/**
 * struct adv7343_sd_config - SD Only Output Configuration.
 * @sd_dac_out: array configuring SD DAC Outputs 1 and 2
 */
struct adv7343_sd_config {
	/* SD only Output Configuration */
	u32 sd_dac_out[2];
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
