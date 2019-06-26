/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TIMER_IXP4XX_H
#define __TIMER_IXP4XX_H

#include <linux/ioport.h>

void __init ixp4xx_timer_setup(resource_size_t timerbase,
			       int timer_irq,
			       unsigned int timer_freq);

#endif
