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
#define cpu_mask_to_apicid(mask) cpu_physical_id(first_cpu(mask))
#define MSI_DEST_MODE		MSI_PHYSICAL_MODE
#define MSI_TARGET_CPU	((ia64_getreg(_IA64_REG_CR_LID) >> 16) & 0xffff)
#define MSI_TARGET_CPU_SHIFT	4

#endif /* ASM_MSI_H */
