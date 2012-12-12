/*
 * Copyright 2012 Maxime Ripard
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __LINUX_IRQCHIP_SUNXI_H
#define __LINUX_IRQCHIP_SUNXI_H

#include <asm/exception.h>

extern void sunxi_init_irq(void);

extern asmlinkage void __exception_irq_entry sunxi_handle_irq(
	struct pt_regs *regs);

#endif
