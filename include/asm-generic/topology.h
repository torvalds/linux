/*
 * linux/include/asm-generic/topology.h
 *
 * Written by: Matthew Dobson, IBM Corporation
 *
 * Copyright (C) 2002, IBM Corp.
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to <colpatch@us.ibm.com>
 */
#ifndef _ASM_GENERIC_TOPOLOGY_H
#define _ASM_GENERIC_TOPOLOGY_H

#ifndef	CONFIG_NUMA

/* Other architectures wishing to use this simple topology API should fill
   in the below functions as appropriate in their own <asm/topology.h> file. */
#ifndef cpu_to_node
#define cpu_to_node(cpu)	((void)(cpu),0)
#endif
#ifndef set_numa_node
#define set_numa_node(node)
#endif
#ifndef set_cpu_numa_node
#define set_cpu_numa_node(cpu, node)
#endif
#ifndef cpu_to_mem
#define cpu_to_mem(cpu)		((void)(cpu),0)
#endif

#ifndef cpumask_of_node
  #ifdef CONFIG_NUMA
    #define cpumask_of_node(node)	((node) == 0 ? cpu_online_mask : cpu_none_mask)
  #else
    #define cpumask_of_node(node)	((void)(node), cpu_online_mask)
  #endif
#endif
#ifndef pcibus_to_node
#define pcibus_to_node(bus)	((void)(bus), -1)
#endif

#ifndef cpumask_of_pcibus
#define cpumask_of_pcibus(bus)	(pcibus_to_node(bus) == -1 ?		\
				 cpu_all_mask :				\
				 cpumask_of_node(pcibus_to_node(bus)))
#endif

#endif	/* CONFIG_NUMA */

#if !defined(CONFIG_NUMA) || !defined(CONFIG_HAVE_MEMORYLESS_NODES)

#ifndef set_numa_mem
#define set_numa_mem(node)
#endif
#ifndef set_cpu_numa_mem
#define set_cpu_numa_mem(cpu, node)
#endif

#endif	/* !CONFIG_NUMA || !CONFIG_HAVE_MEMORYLESS_NODES */

#endif /* _ASM_GENERIC_TOPOLOGY_H */
