/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * SGI specific setup.
 *
 * Copyright (C) 1995 - 1997, 1999 Silcon Graphics, Inc.
 * Copyright (C) 1999 Ralf Baechle (ralf@gnu.org)
 */
#ifndef _ASM_SN_ARCH_H
#define _ASM_SN_ARCH_H

#include <linux/config.h>
#include <linux/types.h>
#include <asm/sn/types.h>
#ifdef CONFIG_SGI_IP27
#include <asm/sn/sn0/arch.h>
#endif

typedef u64	hubreg_t;
typedef u64	nic_t;

#define cputonasid(cpu)			(cpu_data[(cpu)].p_nasid)
#define cputoslice(cpu)			(cpu_data[(cpu)].p_slice)
#define makespnum(_nasid, _slice)					\
		(((_nasid) << CPUS_PER_NODE_SHFT) | (_slice))

#define INVALID_NASID		(nasid_t)-1
#define INVALID_CNODEID		(cnodeid_t)-1
#define INVALID_PNODEID		(pnodeid_t)-1
#define INVALID_MODULE		(moduleid_t)-1
#define	INVALID_PARTID		(partid_t)-1

extern nasid_t get_nasid(void);
extern cnodeid_t get_cpu_cnode(cpuid_t);
extern int get_cpu_slice(cpuid_t);

/*
 * NO ONE should access these arrays directly.  The only reason we refer to
 * them here is to avoid the procedure call that would be required in the
 * macros below.  (Really want private data members here :-)
 */
extern cnodeid_t nasid_to_compact_node[MAX_NASIDS];
extern nasid_t compact_to_nasid_node[MAX_COMPACT_NODES];

/*
 * These macros are used by various parts of the kernel to convert
 * between the three different kinds of node numbering.   At least some
 * of them may change to procedure calls in the future, but the macros
 * will continue to work.  Don't use the arrays above directly.
 */

#define	NASID_TO_REGION(nnode)	      	\
    ((nnode) >> \
     (is_fine_dirmode() ? NASID_TO_FINEREG_SHFT : NASID_TO_COARSEREG_SHFT))

extern cnodeid_t nasid_to_compact_node[MAX_NASIDS];
extern nasid_t compact_to_nasid_node[MAX_COMPACT_NODES];
extern cnodeid_t cpuid_to_compact_node[MAXCPUS];

#define NASID_TO_COMPACT_NODEID(nnode)	(nasid_to_compact_node[nnode])
#define COMPACT_TO_NASID_NODEID(cnode)	(compact_to_nasid_node[cnode])
#define CPUID_TO_COMPACT_NODEID(cpu)	(cpuid_to_compact_node[(cpu)])

#endif /* _ASM_SN_ARCH_H */
