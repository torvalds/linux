/*
 * include/linux/irqchip/arm-gic-common.h
 *
 * Copyright (C) 2016 ARM Limited, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __LINUX_IRQCHIP_ARM_GIC_COMMON_H
#define __LINUX_IRQCHIP_ARM_GIC_COMMON_H

#include <linux/types.h>
#include <linux/ioport.h>

enum gic_type {
	GIC_V2,
	GIC_V3,
};

struct gic_kvm_info {
	/* GIC type */
	enum gic_type	type;
	/* Virtual CPU interface */
	struct resource vcpu;
	/* Interrupt number */
	unsigned int	maint_irq;
	/* Virtual control interface */
	struct resource vctrl;
};

const struct gic_kvm_info *gic_get_kvm_info(void);

#endif /* __LINUX_IRQCHIP_ARM_GIC_COMMON_H */
