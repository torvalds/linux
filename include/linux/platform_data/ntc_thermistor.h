/*
 * ntc_thermistor.h - NTC Thermistors
 *
 *  Copyright (C) 2010 Samsung Electronics
 *  MyungJoo Ham <myungjoo.ham@samsung.com>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef _LINUX_NTC_H
#define _LINUX_NTC_H

struct iio_channel;

enum ntc_thermistor_type {
	TYPE_NCPXXWB473,
	TYPE_NCPXXWL333,
	TYPE_B57330V2103,
	TYPE_NCPXXWF104,
	TYPE_NCPXXXH103,
};

struct ntc_thermistor_platform_data {
	/*
	 * One (not both) of read_uV and read_ohm should be provided and only
	 * one of the two should be provided.
	 * Both functions should return negative value for an error case.
	 *
	 * pullup_uV, pullup_ohm, pulldown_ohm, and connect are required to use
	 * read_uV()
	 *
	 * How to setup pullup_ohm, pulldown_ohm, and connect is
	 * described at Documentation/hwmon/ntc_thermistor
	 *
	 * pullup/down_ohm: 0 for infinite / not-connected
	 *
	 * chan: iio_channel pointer to communicate with the ADC which the
	 * thermistor is using for conversion of the analog values.
	 */
	int (*read_uv)(struct ntc_thermistor_platform_data *);
	unsigned int pullup_uv;

	unsigned int pullup_ohm;
	unsigned int pulldown_ohm;
	enum { NTC_CONNECTED_POSITIVE, NTC_CONNECTED_GROUND } connect;
	struct iio_channel *chan;

	int (*read_ohm)(void);
};

#endif /* _LINUX_NTC_H */
