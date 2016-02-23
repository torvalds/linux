/*
 * ak4641.h  --  AK4641 SoC Audio driver
 *
 * Copyright 2008 Harald Welte <laforge@gnufiish.org>
 *
 * Based on ak4535.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _AK4641_H
#define _AK4641_H

/* AK4641 register space */

#define AK4641_PM1		0x00
#define AK4641_PM2		0x01
#define AK4641_SIG1		0x02
#define AK4641_SIG2		0x03
#define AK4641_MODE1		0x04
#define AK4641_MODE2		0x05
#define AK4641_DAC		0x06
#define AK4641_MIC		0x07
#define AK4641_TIMER		0x08
#define AK4641_ALC1		0x09
#define AK4641_ALC2		0x0a
#define AK4641_PGA		0x0b
#define AK4641_LATT		0x0c
#define AK4641_RATT		0x0d
#define AK4641_VOL		0x0e
#define AK4641_STATUS		0x0f
#define AK4641_EQLO		0x10
#define AK4641_EQMID		0x11
#define AK4641_EQHI		0x12
#define AK4641_BTIF		0x13

#define AK4641_CACHEREGNUM	0x14



#define AK4641_DAI_HIFI		0
#define AK4641_DAI_VOICE	1


#endif
