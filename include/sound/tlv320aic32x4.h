/*
 * tlv320aic32x4.h  --  TLV320AIC32X4 Soc Audio driver platform data
 *
 * Copyright 2011 Vista Silicon S.L.
 *
 * Author: Javier Martin <javier.martin@vista-silicon.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _AIC32X4_PDATA_H
#define _AIC32X4_PDATA_H

#define AIC32X4_PWR_MICBIAS_2075_LDOIN		0x00000001
#define AIC32X4_PWR_AVDD_DVDD_WEAK_DISABLE	0x00000002
#define AIC32X4_PWR_AIC32X4_LDO_ENABLE		0x00000004
#define AIC32X4_PWR_CMMODE_LDOIN_RANGE_18_36	0x00000008
#define AIC32X4_PWR_CMMODE_HP_LDOIN_POWERED	0x00000010

#define AIC32X4_MICPGA_ROUTE_LMIC_IN2R_10K	0x00000001
#define AIC32X4_MICPGA_ROUTE_RMIC_IN1L_10K	0x00000002

struct aic32x4_pdata {
	u32 power_cfg;
	u32 micpga_routing;
	bool swapdacs;
	int rstn_gpio;
};

#endif
