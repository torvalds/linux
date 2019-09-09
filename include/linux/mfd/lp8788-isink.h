/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * TI LP8788 MFD - common definitions for current sinks
 *
 * Copyright 2012 Texas Instruments
 *
 * Author: Milo(Woogyom) Kim <milo.kim@ti.com>
 */

#ifndef __ISINK_LP8788_H__
#define __ISINK_LP8788_H__

/* register address */
#define LP8788_ISINK_CTRL		0x99
#define LP8788_ISINK12_IOUT		0x9A
#define LP8788_ISINK3_IOUT		0x9B
#define LP8788_ISINK1_PWM		0x9C
#define LP8788_ISINK2_PWM		0x9D
#define LP8788_ISINK3_PWM		0x9E

/* mask bits */
#define LP8788_ISINK1_IOUT_M		0x0F	/* Addr 9Ah */
#define LP8788_ISINK2_IOUT_M		0xF0
#define LP8788_ISINK3_IOUT_M		0x0F	/* Addr 9Bh */

/* 6 bits used for PWM code : Addr 9C ~ 9Eh */
#define LP8788_ISINK_MAX_PWM		63
#define LP8788_ISINK_SCALE_OFFSET	3

static const u8 lp8788_iout_addr[] = {
	LP8788_ISINK12_IOUT,
	LP8788_ISINK12_IOUT,
	LP8788_ISINK3_IOUT,
};

static const u8 lp8788_iout_mask[] = {
	LP8788_ISINK1_IOUT_M,
	LP8788_ISINK2_IOUT_M,
	LP8788_ISINK3_IOUT_M,
};

static const u8 lp8788_pwm_addr[] = {
	LP8788_ISINK1_PWM,
	LP8788_ISINK2_PWM,
	LP8788_ISINK3_PWM,
};

#endif
