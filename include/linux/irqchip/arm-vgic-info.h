/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * include/linux/irqchip/arm-vgic-info.h
 *
 * Copyright (C) 2016 ARM Limited, All Rights Reserved.
 */
#ifndef __LINUX_IRQCHIP_ARM_VGIC_INFO_H
#define __LINUX_IRQCHIP_ARM_VGIC_INFO_H

#include <linux/types.h>
#include <linux/ioport.h>

enum gic_type {
	/* Full GICv2 */
	GIC_V2,
	/* Full GICv3, optionally with v2 compat */
	GIC_V3,
};

struct gic_kvm_info {
	/* GIC type */
	enum gic_type	type;
	/* Virtual CPU interface */
	struct resource vcpu;
	/* Interrupt number */
	unsigned int	maint_irq;
	/* No interrupt mask, no need to use the above field */
	bool		no_maint_irq_mask;
	/* Virtual control interface */
	struct resource vctrl;
	/* vlpi support */
	bool		has_v4;
	/* rvpeid support */
	bool		has_v4_1;
	/* Deactivation impared, subpar stuff */
	bool		no_hw_deactivation;
};

#ifdef CONFIG_KVM
void vgic_set_kvm_info(const struct gic_kvm_info *info);
#else
static inline void vgic_set_kvm_info(const struct gic_kvm_info *info) {}
#endif

#endif
