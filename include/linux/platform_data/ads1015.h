/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Platform Data for ADS1015 12-bit 4-input ADC
 * (C) Copyright 2010
 * Dirk Eibach, Guntermann & Drunck GmbH <eibach@gdsys.de>
 */

#ifndef LINUX_ADS1015_H
#define LINUX_ADS1015_H

#define ADS1015_CHANNELS 8

struct ads1015_channel_data {
	bool enabled;
	unsigned int pga;
	unsigned int data_rate;
};

struct ads1015_platform_data {
	struct ads1015_channel_data channel_data[ADS1015_CHANNELS];
};

#endif /* LINUX_ADS1015_H */
