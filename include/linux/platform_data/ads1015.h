/*
 * Platform Data for ADS1015 12-bit 4-input ADC
 * (C) Copyright 2010
 * Dirk Eibach, Guntermann & Drunck GmbH <eibach@gdsys.de>
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
