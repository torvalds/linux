/*
 * Copied from the kernel sources to tools/:
 *
 * Memory barrier definitions.  This is based on information published
 * in the Processor Abstraction Layer and the System Abstraction Layer
 * manual.
 *
 * Copyright (C) 1998-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1999 Asit Mallick <asit.k.mallick@intel.com>
 * Copyright (C) 1999 Don Dugger <don.dugger@intel.com>
 */
#ifndef _TOOLS_LINUX_ASM_IA64_BARRIER_H
#define _TOOLS_LINUX_ASM_IA64_BARRIER_H

#include <linux/compiler.h>

/*
 * Macros to force memory ordering.  In these descriptions, "previous"
 * and "subsequent" refer to program order; "visible" means that all
 * architecturally visible effects of a memory access have occurred
 * (at a minimum, this means the memory has been read or written).
 *
 *   wmb():	Guarantees that all preceding stores to memory-
 *		like regions are visible before any subsequent
 *		stores and that all following stores will be
 *		visible only after all previous stores.
 *   rmb():	Like wmb(), but for reads.
 *   mb():	wmb()/rmb() combo, i.e., all previous memory
 *		accesses are visible before all subsequent
 *		accesses and vice versa.  This is also known as
 *		a "fence."
 *
 * Note: "mb()" and its variants cannot be used as a fence to order
 * accesses to memory mapped I/O registers.  For that, mf.a needs to
 * be used.  However, we don't want to always use mf.a because (a)
 * it's (presumably) much slower than mf and (b) mf.a is supported for
 * sequential memory pages only.
 */

/* XXX From arch/ia64/include/uapi/asm/gcc_intrin.h */
#define ia64_mf()       asm volatile ("mf" ::: "memory")

#define mb()		ia64_mf()
#define rmb()		mb()
#define wmb()		mb()

#endif /* _TOOLS_LINUX_ASM_IA64_BARRIER_H */
