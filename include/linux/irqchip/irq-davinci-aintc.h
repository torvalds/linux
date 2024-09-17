/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2019 Texas Instruments
 */

#ifndef _LINUX_IRQ_DAVINCI_AINTC_
#define _LINUX_IRQ_DAVINCI_AINTC_

#include <linux/ioport.h>

/**
 * struct davinci_aintc_config - configuration data for davinci-aintc driver.
 *
 * @reg: register range to map
 * @num_irqs: number of HW interrupts supported by the controller
 * @prios: an array of size num_irqs containing priority settings for
 *         each interrupt
 */
struct davinci_aintc_config {
	struct resource reg;
	unsigned int num_irqs;
	u8 *prios;
};

void davinci_aintc_init(const struct davinci_aintc_config *config);

#endif /* _LINUX_IRQ_DAVINCI_AINTC_ */
