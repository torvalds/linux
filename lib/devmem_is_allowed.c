// SPDX-License-Identifier: GPL-2.0-only
/*
 * A generic version of devmem_is_allowed.
 *
 * Based on arch/arm64/mm/mmap.c
 *
 * Copyright (C) 2020 Google, Inc.
 * Copyright (C) 2012 ARM Ltd.
 */

#include <linux/mm.h>
#include <linux/ioport.h>

/*
 * devmem_is_allowed() checks to see if /dev/mem access to a certain address
 * is valid. The argument is a physical page number.  We mimic x86 here by
 * disallowing access to system RAM as well as device-exclusive MMIO regions.
 * This effectively disable read()/write() on /dev/mem.
 */
int devmem_is_allowed(unsigned long pfn)
{
	if (iomem_is_exclusive(PFN_PHYS(pfn)))
		return 0;
	if (!page_is_ram(pfn))
		return 1;
	return 0;
}
