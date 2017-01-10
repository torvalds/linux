/*
 * Header providing constants for bcm2835 pinctrl bindings.
 *
 * Copyright (C) 2015 Stefan Wahren <stefan.wahren@i2se.com>
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#ifndef __DT_BINDINGS_PINCTRL_BCM2835_H__
#define __DT_BINDINGS_PINCTRL_BCM2835_H__

/* brcm,function property */
#define BCM2835_FSEL_GPIO_IN	0
#define BCM2835_FSEL_GPIO_OUT	1
#define BCM2835_FSEL_ALT5	2
#define BCM2835_FSEL_ALT4	3
#define BCM2835_FSEL_ALT0	4
#define BCM2835_FSEL_ALT1	5
#define BCM2835_FSEL_ALT2	6
#define BCM2835_FSEL_ALT3	7

/* brcm,pull property */
#define BCM2835_PUD_OFF		0
#define BCM2835_PUD_DOWN	1
#define BCM2835_PUD_UP		2

#endif /* __DT_BINDINGS_PINCTRL_BCM2835_H__ */
