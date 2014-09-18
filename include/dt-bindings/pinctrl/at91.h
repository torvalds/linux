/*
 * This header provides constants for most at91 pinctrl bindings.
 *
 * Copyright (C) 2013 Jean-Christophe PLAGNIOL-VILLARD <plagnioj@jcrosoft.com>
 *
 * GPLv2 only
 */

#ifndef __DT_BINDINGS_AT91_PINCTRL_H__
#define __DT_BINDINGS_AT91_PINCTRL_H__

#define AT91_PINCTRL_NONE		(0 << 0)
#define AT91_PINCTRL_PULL_UP		(1 << 0)
#define AT91_PINCTRL_MULTI_DRIVE	(1 << 1)
#define AT91_PINCTRL_DEGLITCH		(1 << 2)
#define AT91_PINCTRL_PULL_DOWN		(1 << 3)
#define AT91_PINCTRL_DIS_SCHMIT		(1 << 4)
#define AT91_PINCTRL_DEBOUNCE		(1 << 16)
#define AT91_PINCTRL_DEBOUNCE_VAL(x)	(x << 17)

#define AT91_PINCTRL_PULL_UP_DEGLITCH	(AT91_PINCTRL_PULL_UP | AT91_PINCTRL_DEGLITCH)

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

#endif /* __DT_BINDINGS_AT91_PINCTRL_H__ */
