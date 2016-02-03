/*
    tvaudio.h - definition for tvaudio inputs

    Copyright (C) 2006 Hans Verkuil (hverkuil@xs4all.nl)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef _TVAUDIO_H
#define _TVAUDIO_H

#include <media/i2c-addr.h>

/* The tvaudio module accepts the following inputs: */
#define TVAUDIO_INPUT_TUNER  0
#define TVAUDIO_INPUT_RADIO  1
#define TVAUDIO_INPUT_EXTERN 2
#define TVAUDIO_INPUT_INTERN 3

static inline const unsigned short *tvaudio_addrs(void)
{
	static const unsigned short addrs[] = {
		I2C_ADDR_TDA8425   >> 1,
		I2C_ADDR_TEA6300   >> 1,
		I2C_ADDR_TEA6420   >> 1,
		I2C_ADDR_TDA9840   >> 1,
		I2C_ADDR_TDA985x_L >> 1,
		I2C_ADDR_TDA985x_H >> 1,
		I2C_ADDR_TDA9874   >> 1,
		I2C_ADDR_PIC16C54  >> 1,
		I2C_CLIENT_END
	};

	return addrs;
}

#endif
