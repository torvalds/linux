/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2007 Aurelien Jarno <aurelien@aurel32.net>
 */

#ifndef __BCM47XX_GPIO_H
#define __BCM47XX_GPIO_H

#define BCM47XX_EXTIF_GPIO_LINES	5
#define BCM47XX_CHIPCO_GPIO_LINES	16

extern int bcm47xx_gpio_to_irq(unsigned gpio);
extern int bcm47xx_gpio_get_value(unsigned gpio);
extern void bcm47xx_gpio_set_value(unsigned gpio, int value);
extern int bcm47xx_gpio_direction_input(unsigned gpio);
extern int bcm47xx_gpio_direction_output(unsigned gpio, int value);

static inline int gpio_request(unsigned gpio, const char *label)
{
       return 0;
}

static inline void gpio_free(unsigned gpio)
{
}

static inline int gpio_to_irq(unsigned gpio)
{
	return bcm47xx_gpio_to_irq(gpio);
}

static inline int gpio_get_value(unsigned gpio)
{
	return bcm47xx_gpio_get_value(gpio);
}

static inline void gpio_set_value(unsigned gpio, int value)
{
	bcm47xx_gpio_set_value(gpio, value);
}

static inline int gpio_direction_input(unsigned gpio)
{
	return bcm47xx_gpio_direction_input(gpio);
}

static inline int gpio_direction_output(unsigned gpio, int value)
{
	return bcm47xx_gpio_direction_output(gpio, value);
}


/* cansleep wrappers */
#include <asm-generic/gpio.h>

#endif /* __BCM47XX_GPIO_H */
