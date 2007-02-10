/*
 * (c) Copyright 2006 Benjamin Herrenschmidt, IBM Corp.
 *                    <benh@kernel.crashing.org>
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY;  without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 *   the GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program;  if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef _ASM_POWERPC_DCR_H
#define _ASM_POWERPC_DCR_H
#ifdef __KERNEL__
#ifdef CONFIG_PPC_DCR

#ifdef CONFIG_PPC_DCR_NATIVE
#include <asm/dcr-native.h>
#else
#include <asm/dcr-mmio.h>
#endif

/*
 * On CONFIG_PPC_MERGE, we have additional helpers to read the DCR
 * base from the device-tree
 */
#ifdef CONFIG_PPC_MERGE
struct device_node;
extern unsigned int dcr_resource_start(struct device_node *np,
				       unsigned int index);
extern unsigned int dcr_resource_len(struct device_node *np,
				     unsigned int index);
#endif /* CONFIG_PPC_MERGE */

#endif /* CONFIG_PPC_DCR */
#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_DCR_H */
