/*
 * AT32 System Manager interface.
 *
 * Copyright (C) 2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_AVR32_AT32_SM_H__
#define __ASM_AVR32_AT32_SM_H__

struct irq_chip;
struct platform_device;

struct at32_sm {
	spinlock_t lock;
	void __iomem *regs;
	struct irq_chip *eim_chip;
	unsigned int eim_first_irq;
	struct platform_device *pdev;
};

extern struct platform_device at32_sm_device;
extern struct at32_sm system_manager;

#endif /* __ASM_AVR32_AT32_SM_H__ */
