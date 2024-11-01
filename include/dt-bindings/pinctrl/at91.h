/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This header provides constants for most at91 pinctrl bindings.
 *
 * Copyright (C) 2013 Jean-Christophe PLAGNIOL-VILLARD <plagnioj@jcrosoft.com>
 */

#ifndef __DT_BINDINGS_AT91_PINCTRL_H__
#define __DT_BINDINGS_AT91_PINCTRL_H__

#define AT91_PINCTRL_NONE		(0 << 0)
#define AT91_PINCTRL_PULL_UP		(1 << 0)
#define AT91_PINCTRL_MULTI_DRIVE	(1 << 1)
#define AT91_PINCTRL_DEGLITCH		(1 << 2)
#define AT91_PINCTRL_PULL_DOWN		(1 << 3)
#define AT91_PINCTRL_DIS_SCHMIT		(1 << 4)
#define AT91_PINCTRL_OUTPUT		(1 << 7)
#define AT91_PINCTRL_OUTPUT_VAL(x)	((x & 0x1) << 8)
#define AT91_PINCTRL_SLEWRATE		(1 << 9)
#define AT91_PINCTRL_DEBOUNCE		(1 << 16)
#define AT91_PINCTRL_DEBOUNCE_VAL(x)	(x << 17)

#define AT91_PINCTRL_PULL_UP_DEGLITCH	(AT91_PINCTRL_PULL_UP | AT91_PINCTRL_DEGLITCH)

#define AT91_PINCTRL_DRIVE_STRENGTH_DEFAULT		(0x0 << 5)
#define AT91_PINCTRL_DRIVE_STRENGTH_LOW			(0x1 << 5)
#define AT91_PINCTRL_DRIVE_STRENGTH_MED			(0x2 << 5)
#define AT91_PINCTRL_DRIVE_STRENGTH_HI			(0x3 << 5)

#define AT91_PINCTRL_SLEWRATE_ENA	(0x0 << 9)
#define AT91_PINCTRL_SLEWRATE_DIS	(0x1 << 9)

#define AT91_PIOA	0
#define AT91_PIOB	1
#define AT91_PIOC	2
#define AT91_PIOD	3
#define AT91_PIOE	4

#define AT91_PERIPH_GPIO	0
#define AT91_PERIPH_A		1
#define AT91_PERIPH_B		2
#define AT91_PERIPH_C		3
#define AT91_PERIPH_D		4

#define ATMEL_PIO_DRVSTR_LO	1
#define ATMEL_PIO_DRVSTR_ME	2
#define ATMEL_PIO_DRVSTR_HI	3

#endif /* __DT_BINDINGS_AT91_PINCTRL_H__ */
