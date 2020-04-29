/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ak4535.h  --  AK4535 Soc Audio driver
 *
 * Copyright 2005 Openedhand Ltd.
 *
 * Author: Richard Purdie <richard@openedhand.com>
 *
 * Based on wm8753.h
 */

#ifndef _AK4535_H
#define _AK4535_H

/* AK4535 register space */

#define AK4535_PM1		0x0
#define AK4535_PM2		0x1
#define AK4535_SIG1		0x2
#define AK4535_SIG2		0x3
#define AK4535_MODE1		0x4
#define AK4535_MODE2		0x5
#define AK4535_DAC		0x6
#define AK4535_MIC		0x7
#define AK4535_TIMER		0x8
#define AK4535_ALC1		0x9
#define AK4535_ALC2		0xa
#define AK4535_PGA		0xb
#define AK4535_LATT		0xc
#define AK4535_RATT		0xd
#define AK4535_VOL		0xe
#define AK4535_STATUS		0xf

#endif
