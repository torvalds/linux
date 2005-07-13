/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * SGI specific setup.
 *
 * Copyright (C) 1995-1997,1999,2001-2005 Silicon Graphics, Inc.  All rights reserved.
 * Copyright (C) 1999 Ralf Baechle (ralf@gnu.org)
 */
#ifndef _ASM_IA64_SN_ARCH_H
#define _ASM_IA64_SN_ARCH_H

#include <linux/numa.h>
#include <asm/types.h>
#include <asm/percpu.h>
#include <asm/sn/types.h>
#include <asm/sn/sn_cpuid.h>

/*
 * The following defines attributes of the HUB chip. These attributes are
 * frequently referenced. They are kept in the per-cpu data areas of each cpu.
 * They are kept together in a struct to minimize cache misses.
 */
struct sn_hub_info_s {
	u8 shub2;
	u8 nasid_shift;
	u8 as_shift;
	u8 shub_1_1_found;
	u16 nasid_bitmask;
};
DECLARE_PER_CPU(struct sn_hub_info_s, __sn_hub_info);
#define sn_hub_info 	(&__get_cpu_var(__sn_hub_info))
#define is_shub2()	(sn_hub_info->shub2)
#define is_shub1()	(sn_hub_info->shub2 == 0)

/*
 * Use this macro to test if shub 1.1 wars should be enabled
 */
#define enable_shub_wars_1_1()	(sn_hub_info->shub_1_1_found)


/*
 * This is the maximum number of nodes that can be part of a kernel.
 * Effectively, it's the maximum number of compact node ids (cnodeid_t).
 * This is not necessarily the same as MAX_NASIDS.
 */
#define MAX_COMPACT_NODES	2048
#define CPUS_PER_NODE		4


/*
 * Compact node ID to nasid mappings kept in the per-cpu data areas of each
 * cpu.
 */
DECLARE_PER_CPU(short, __sn_cnodeid_to_nasid[MAX_NUMNODES]);
#define sn_cnodeid_to_nasid	(&__get_cpu_var(__sn_cnodeid_to_nasid[0]))



extern u8 sn_partition_id;
extern u8 sn_system_size;
extern u8 sn_sharing_domain_size;
extern u8 sn_region_size;

extern void sn_flush_all_caches(long addr, long bytes);

#endif /* _ASM_IA64_SN_ARCH_H */
