/*
 * Copyright (C) 2003-2004 Intel
 * Copyright (C) Tom Long Nguyen (tom.l.nguyen@intel.com)
 */

#ifndef ASM_MSI_H
#define ASM_MSI_H

#include <asm/desc.h>
#include <mach_apic.h>

extern struct msi_ops arch_msi_ops;

static inline int msi_arch_init(void)
{
	msi_register(&arch_msi_ops);
	return 0;
}

#endif /* ASM_MSI_H */
