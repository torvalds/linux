/*
 * Platform data for ST STA32x ASoC codec driver.
 *
 * Copyright: 2011 Raumfeld GmbH
 * Author: Johannes Stezenbach <js@sig21.net>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#ifndef __LINUX_SND__STA32X_H
#define __LINUX_SND__STA32X_H

#define STA32X_OCFG_2CH		0
#define STA32X_OCFG_2_1CH	1
#define STA32X_OCFG_1CH		3

#define STA32X_OM_CH1		0
#define STA32X_OM_CH2		1
#define STA32X_OM_CH3		2

#define STA32X_THERMAL_ADJUSTMENT_ENABLE	1
#define STA32X_THERMAL_RECOVERY_ENABLE		2

struct sta32x_platform_data {
	u8 output_conf;
	u8 ch1_output_mapping;
	u8 ch2_output_mapping;
	u8 ch3_output_mapping;
	int needs_esd_watchdog;
	u8 drop_compensation_ns;
	unsigned int thermal_warning_recovery:1;
	unsigned int thermal_warning_adjustment:1;
	unsigned int fault_detect_recovery:1;
	unsigned int max_power_use_mpcc:1;
	unsigned int max_power_correction:1;
	unsigned int am_reduction_mode:1;
	unsigned int odd_pwm_speed_mode:1;
	unsigned int invalid_input_detect_mute:1;
};

#endif /* __LINUX_SND__STA32X_H */
