/*
 * Copyright (C) 2003-2004 Intel
 * Copyright (C) Tom Long Nguyen (tom.l.nguyen@intel.com)
 */

#ifndef ASM_MSI_H
#define ASM_MSI_H

#define NR_VECTORS		NR_IRQS
#define FIRST_DEVICE_VECTOR 	IA64_FIRST_DEVICE_VECTOR
#define LAST_DEVICE_VECTOR	IA64_LAST_DEVICE_VECTOR
static inline void set_intr_gate (int nr, void *func) {}
#define IO_APIC_VECTOR(irq)	(irq)
#define ack_APIC_irq		ia64_eoi
#define MSI_TARGET_CPU_SHIFT	4

#endif /* ASM_MSI_H */
