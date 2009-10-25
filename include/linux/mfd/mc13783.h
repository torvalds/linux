/*
 * Copyright 2009 Pengutronix, Sascha Hauer <s.hauer@pengutronix.de>
 *
 * Initial development of this code was funded by
 * Phytec Messtechnik GmbH, http://www.phytec.de
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __INCLUDE_LINUX_MFD_MC13783_H
#define __INCLUDE_LINUX_MFD_MC13783_H

struct mc13783;
struct regulator_init_data;

struct mc13783_regulator_init_data {
	int id;
	struct regulator_init_data *init_data;
};

struct mc13783_platform_data {
	struct mc13783_regulator_init_data *regulators;
	int num_regulators;
	unsigned int flags;
};

/* mc13783_platform_data flags */
#define MC13783_USE_TOUCHSCREEN (1 << 0)
#define MC13783_USE_CODEC	(1 << 1)
#define MC13783_USE_ADC		(1 << 2)
#define MC13783_USE_RTC		(1 << 3)
#define MC13783_USE_REGULATOR	(1 << 4)

int mc13783_adc_do_conversion(struct mc13783 *mc13783, unsigned int mode,
		unsigned int channel, unsigned int *sample);

void mc13783_adc_set_ts_status(struct mc13783 *mc13783, unsigned int status);

#define	MC13783_SW_SW1A		0
#define	MC13783_SW_SW1B		1
#define	MC13783_SW_SW2A		2
#define	MC13783_SW_SW2B		3
#define	MC13783_SW_SW3		4
#define	MC13783_SW_PLL		5
#define	MC13783_REGU_VAUDIO	6
#define	MC13783_REGU_VIOHI	7
#define	MC13783_REGU_VIOLO	8
#define	MC13783_REGU_VDIG	9
#define	MC13783_REGU_VGEN	10
#define	MC13783_REGU_VRFDIG	11
#define	MC13783_REGU_VRFREF	12
#define	MC13783_REGU_VRFCP	13
#define	MC13783_REGU_VSIM	14
#define	MC13783_REGU_VESIM	15
#define	MC13783_REGU_VCAM	16
#define	MC13783_REGU_VRFBG	17
#define	MC13783_REGU_VVIB	18
#define	MC13783_REGU_VRF1	19
#define	MC13783_REGU_VRF2	20
#define	MC13783_REGU_VMMC1	21
#define	MC13783_REGU_VMMC2	22
#define	MC13783_REGU_GPO1	23
#define	MC13783_REGU_GPO2	24
#define	MC13783_REGU_GPO3	25
#define	MC13783_REGU_GPO4	26
#define	MC13783_REGU_V1		27
#define	MC13783_REGU_V2		28
#define	MC13783_REGU_V3		29
#define	MC13783_REGU_V4		30

#endif /* __INCLUDE_LINUX_MFD_MC13783_H */

