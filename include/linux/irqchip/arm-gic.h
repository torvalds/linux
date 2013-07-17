/*
 *  include/linux/irqchip/arm-gic.h
 *
 *  Copyright (C) 2002 ARM Limited, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __LINUX_IRQCHIP_ARM_GIC_H
#define __LINUX_IRQCHIP_ARM_GIC_H

#define GIC_CPU_CTRL			0x00
#define GIC_CPU_PRIMASK			0x04
#define GIC_CPU_BINPOINT		0x08
#define GIC_CPU_INTACK			0x0c
#define GIC_CPU_EOI			0x10
#define GIC_CPU_RUNNINGPRI		0x14
#define GIC_CPU_HIGHPRI			0x18

#define GIC_DIST_CTRL			0x000
#define GIC_DIST_CTR			0x004
#define GIC_DIST_IGROUP			0x080
#define GIC_DIST_ENABLE_SET		0x100
#define GIC_DIST_ENABLE_CLEAR		0x180
#define GIC_DIST_PENDING_SET		0x200
#define GIC_DIST_PENDING_CLEAR		0x280
#define GIC_DIST_ACTIVE_SET		0x300
#define GIC_DIST_ACTIVE_CLEAR		0x380
#define GIC_DIST_PRI			0x400
#define GIC_DIST_TARGET			0x800
#define GIC_DIST_CONFIG			0xc00
#define GIC_DIST_SOFTINT		0xf00
#define GIC_DIST_SGI_PENDING_CLEAR	0xf10
#define GIC_DIST_SGI_PENDING_SET	0xf20

#define GICH_HCR			0x0
#define GICH_VTR			0x4
#define GICH_VMCR			0x8
#define GICH_MISR			0x10
#define GICH_EISR0 			0x20
#define GICH_EISR1 			0x24
#define GICH_ELRSR0 			0x30
#define GICH_ELRSR1 			0x34
#define GICH_APR			0xf0
#define GICH_LR0			0x100

#define GICH_HCR_EN			(1 << 0)
#define GICH_HCR_UIE			(1 << 1)

#define GICH_LR_VIRTUALID		(0x3ff << 0)
#define GICH_LR_PHYSID_CPUID_SHIFT	(10)
#define GICH_LR_PHYSID_CPUID		(7 << GICH_LR_PHYSID_CPUID_SHIFT)
#define GICH_LR_STATE			(3 << 28)
#define GICH_LR_PENDING_BIT		(1 << 28)
#define GICH_LR_ACTIVE_BIT		(1 << 29)
#define GICH_LR_EOI			(1 << 19)

#define GICH_MISR_EOI			(1 << 0)
#define GICH_MISR_U			(1 << 1)

#ifndef __ASSEMBLY__

struct device_node;

extern struct irq_chip gic_arch_extn;

void gic_init_bases(unsigned int, int, void __iomem *, void __iomem *,
		    u32 offset, struct device_node *);
void gic_cascade_irq(unsigned int gic_nr, unsigned int irq);

static inline void gic_init(unsigned int nr, int start,
			    void __iomem *dist , void __iomem *cpu)
{
	gic_init_bases(nr, start, dist, cpu, 0, NULL);
}

void gic_send_sgi(unsigned int cpu_id, unsigned int irq);
int gic_get_cpu_id(unsigned int cpu);
void gic_migrate_target(unsigned int new_cpu_id);
unsigned long gic_get_sgir_physaddr(void);

#endif /* __ASSEMBLY */

#endif
