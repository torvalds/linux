/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __LINUX_IRQCHIP_ARM_GIC_V3_PRIO_H
#define __LINUX_IRQCHIP_ARM_GIC_V3_PRIO_H

/*
 * GIC priorities from the view of the PMR/RPR.
 *
 * These values are chosen to be valid in either the absolute priority space or
 * the NS view of the priority space. The value programmed into the distributor
 * and ITS will be chosen at boot time such that these values appear in the
 * PMR/RPR.
 *
 * GICV3_PRIO_UNMASKED is the PMR view of the priority to use to permit both
 * IRQs and pseudo-NMIs.
 *
 * GICV3_PRIO_IRQ is the PMR view of the priority of regular interrupts. This
 * can be written to the PMR to mask regular IRQs.
 *
 * GICV3_PRIO_NMI is the PMR view of the priority of pseudo-NMIs. This can be
 * written to the PMR to mask pseudo-NMIs.
 *
 * On arm64 some code sections either automatically switch back to PSR.I or
 * explicitly require to not use priority masking. If bit GICV3_PRIO_PSR_I_SET
 * is included in the priority mask, it indicates that PSR.I should be set and
 * interrupt disabling temporarily does not rely on IRQ priorities.
 */
#define GICV3_PRIO_UNMASKED	0xe0
#define GICV3_PRIO_IRQ		0xc0
#define GICV3_PRIO_NMI		0x80

#define GICV3_PRIO_PSR_I_SET	(1 << 4)

#ifndef __ASSEMBLER__

#define __gicv3_prio_to_ns(p)	(0xff & ((p) << 1))
#define __gicv3_ns_to_prio(ns)	(0x80 | ((ns) >> 1))

#define __gicv3_prio_valid_ns(p) \
	(__gicv3_ns_to_prio(__gicv3_prio_to_ns(p)) == (p))

static_assert(__gicv3_prio_valid_ns(GICV3_PRIO_NMI));
static_assert(__gicv3_prio_valid_ns(GICV3_PRIO_IRQ));

static_assert(GICV3_PRIO_NMI < GICV3_PRIO_IRQ);
static_assert(GICV3_PRIO_IRQ < GICV3_PRIO_UNMASKED);

static_assert(GICV3_PRIO_IRQ < (GICV3_PRIO_IRQ | GICV3_PRIO_PSR_I_SET));

#endif /* __ASSEMBLER */

#endif /* __LINUX_IRQCHIP_ARM_GIC_V3_PRIO_H */
