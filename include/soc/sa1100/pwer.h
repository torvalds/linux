/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SOC_SA1100_PWER_H
#define SOC_SA1100_PWER_H

/*
 * Copyright (C) 2015, Dmitry Eremin-Solenikov
 */

int sa11x0_gpio_set_wake(unsigned int gpio, unsigned int on);
int sa11x0_sc_set_wake(unsigned int irq, unsigned int on);

#endif
