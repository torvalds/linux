/*
 *  linux/include/asm-arm/numnodes.h
 *
 *  Copyright (C) 2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* This declaration for the size of the NUMA (CONFIG_DISCONTIGMEM)
 * memory node table is the default.
 *
 * A good place to override this value is include/asm/arch/memory.h.
 */

#ifndef __ASM_ARM_NUMNODES_H
#define __ASM_ARM_NUMNODES_H

#ifndef NODES_SHIFT
# define NODES_SHIFT	2	/* Normally, Max 4 Nodes */
#endif

#endif
