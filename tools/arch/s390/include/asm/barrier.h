/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copied from the kernel sources:
 *
 * Copyright IBM Corp. 1999, 2009
 *
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef __TOOLS_LINUX_ASM_BARRIER_H
#define __TOOLS_LINUX_ASM_BARRIER_H

/*
 * Force strict CPU ordering.
 * And yes, this is required on UP too when we're talking
 * to devices.
 */

#ifdef CONFIG_HAVE_MARCH_Z196_FEATURES
/* Fast-BCR without checkpoint synchronization */
#define __ASM_BARRIER "bcr 14,0\n"
#else
#define __ASM_BARRIER "bcr 15,0\n"
#endif

#define mb() do {  asm volatile(__ASM_BARRIER : : : "memory"); } while (0)

#define rmb()				mb()
#define wmb()				mb()

#endif /* __TOOLS_LIB_ASM_BARRIER_H */
