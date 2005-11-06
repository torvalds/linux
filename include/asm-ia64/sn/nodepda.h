/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000-2005 Silicon Graphics, Inc. All rights reserved.
 */
#ifndef _ASM_IA64_SN_NODEPDA_H
#define _ASM_IA64_SN_NODEPDA_H


#include <asm/semaphore.h>
#include <asm/irq.h>
#include <asm/sn/arch.h>
#include <asm/sn/intr.h>
#include <asm/sn/bte.h>

/*
 * NUMA Node-Specific Data structures are defined in this file.
 * In particular, this is the location of the node PDA.
 * A pointer to the right node PDA is saved in each CPU PDA.
 */

/*
 * Node-specific data structure.
 *
 * One of these structures is allocated on each node of a NUMA system.
 *
 * This structure provides a convenient way of keeping together 
 * all per-node data structures. 
 */
struct phys_cpuid {
	short			nasid;
	char			subnode;
	char			slice;
};

struct nodepda_s {
	void 		*pdinfo;	/* Platform-dependent per-node info */

	/*
	 * The BTEs on this node are shared by the local cpus
	 */
	struct bteinfo_s	bte_if[MAX_BTES_PER_NODE];	/* Virtual Interface */
	struct timer_list	bte_recovery_timer;
	spinlock_t		bte_recovery_lock;

	/* 
	 * Array of pointers to the nodepdas for each node.
	 */
	struct nodepda_s	*pernode_pdaindr[MAX_COMPACT_NODES]; 

	/*
	 * Array of physical cpu identifiers. Indexed by cpuid.
	 */
	struct phys_cpuid	phys_cpuid[NR_CPUS];
	spinlock_t		ptc_lock ____cacheline_aligned_in_smp;
};

typedef struct nodepda_s nodepda_t;

/*
 * Access Functions for node PDA.
 * Since there is one nodepda for each node, we need a convenient mechanism
 * to access these nodepdas without cluttering code with #ifdefs.
 * The next set of definitions provides this.
 * Routines are expected to use 
 *
 *	sn_nodepda   - to access node PDA for the node on which code is running
 *	NODEPDA(cnodeid)   - to access node PDA for cnodeid
 */

DECLARE_PER_CPU(struct nodepda_s *, __sn_nodepda);
#define sn_nodepda		(__get_cpu_var(__sn_nodepda))
#define	NODEPDA(cnodeid)	(sn_nodepda->pernode_pdaindr[cnodeid])

/*
 * Check if given a compact node id the corresponding node has all the
 * cpus disabled. 
 */
#define is_headless_node(cnodeid)	(nr_cpus_node(cnodeid) == 0)

#endif /* _ASM_IA64_SN_NODEPDA_H */
