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
 * This is the maximum number of NUMALINK nodes that can be part of a single
 * SSI kernel. This number includes C-brick, M-bricks, and TIOs. Nodes in
 * remote partitions are NOT included in this number.
 * The number of compact nodes cannot exceed size of a coherency domain.
 * The purpose of this define is to specify a node count that includes
 * all C/M/TIO nodes in an SSI system.
 *
 * SGI system can currently support up to 256 C/M nodes plus additional TIO nodes.
 *
 * 	Note: ACPI20 has an architectural limit of 256 nodes. When we upgrade
 * 	to ACPI3.0, this limit will be removed. The notion of "compact nodes"
 * 	should be deleted and TIOs should be included in MAX_NUMNODES.
 */
#define MAX_COMPACT_NODES	512

/*
 * Maximum number of nodes in all partitions and in all coherency domains.
 * This is the total number of nodes accessible in the numalink fabric. It
 * includes all C & M bricks, plus all TIOs.
 *
 * This value is also the value of the maximum number of NASIDs in the numalink
 * fabric.
 */
#define MAX_NUMALINK_NODES	16384

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
 * Compact node ID to nasid mappings kept in the per-cpu data areas of each
 * cpu.
 */
DECLARE_PER_CPU(short, __sn_cnodeid_to_nasid[MAX_COMPACT_NODES]);
#define sn_cnodeid_to_nasid	(&__get_cpu_var(__sn_cnodeid_to_nasid[0]))


extern u8 sn_partition_id;
extern u8 sn_system_size;
extern u8 sn_sharing_domain_size;
extern u8 sn_region_size;

extern void sn_flush_all_caches(long addr, long bytes);

#endif /* _ASM_IA64_SN_ARCH_H */
