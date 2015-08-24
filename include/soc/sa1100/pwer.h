#ifndef SOC_SA1100_PWER_H
#define SOC_SA1100_PWER_H

/*
 * Copyright (C) 2015, Dmitry Eremin-Solenikov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

int sa11x0_gpio_set_wake(unsigned int gpio, unsigned int on);
int sa11x0_sc_set_wake(unsigned int irq, unsigned int on);

#endif
