/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2006 Simtec Electronics
 *	http://armlinux.simtec.co.uk/
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C24XX - LEDs GPIO connector
*/

#ifndef __LEDS_S3C24XX_H
#define __LEDS_S3C24XX_H

struct s3c24xx_led_platdata {
	char			*name;
	char			*def_trigger;
};

#endif /* __LEDS_S3C24XX_H */
