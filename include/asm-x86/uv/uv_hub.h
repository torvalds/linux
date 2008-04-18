/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * SGI UV architectural definitions
 *
 * Copyright (C) 2007 Silicon Graphics, Inc. All rights reserved.
 */

#ifndef __ASM_X86_UV_HUB_H__
#define __ASM_X86_UV_HUB_H__

#include <linux/numa.h>
#include <linux/percpu.h>
#include <asm/types.h>
#include <asm/percpu.h>


/*
 * Addressing Terminology
 *
 * 	NASID - network ID of a router, Mbrick or Cbrick. Nasid values of
 * 		routers always have low bit of 1, C/MBricks have low bit
 * 		equal to 0. Most addressing macros that target UV hub chips
 * 		right shift the NASID by 1 to exclude the always-zero bit.
 *
 *	SNASID - NASID right shifted by 1 bit.
 *
 *
 *  Memory/UV-HUB Processor Socket Address Format:
 *  +--------+---------------+---------------------+
 *  |00..0000|    SNASID     |      NodeOffset     |
 *  +--------+---------------+---------------------+
 *           <--- N bits --->|<--------M bits ----->
 *
 *	M number of node offset bits (35 .. 40)
 *	N number of SNASID bits (0 .. 10)
 *
 *		Note: M + N cannot currently exceed 44 (x86_64) or 46 (IA64).
 *		The actual values are configuration dependent and are set at
 *		boot time
 *
 * APICID format
 * 	NOTE!!!!!! This is the current format of the APICID. However, code
 * 	should assume that this will change in the future. Use functions
 * 	in this file for all APICID bit manipulations and conversion.
 *
 * 		1111110000000000
 * 		5432109876543210
 *		nnnnnnnnnnlc0cch
 *		sssssssssss
 *
 *			n  = snasid bits
 *			l =  socket number on board
 *			c  = core
 *			h  = hyperthread
 *			s  = bits that are in the socket CSR
 *
 *	Note: Processor only supports 12 bits in the APICID register. The ACPI
 *	      tables hold all 16 bits. Software needs to be aware of this.
 *
 * 	      Unless otherwise specified, all references to APICID refer to
 * 	      the FULL value contained in ACPI tables, not the subset in the
 * 	      processor APICID register.
 */


/*
 * Maximum number of bricks in all partitions and in all coherency domains.
 * This is the total number of bricks accessible in the numalink fabric. It
 * includes all C & M bricks. Routers are NOT included.
 *
 * This value is also the value of the maximum number of non-router NASIDs
 * in the numalink fabric.
 *
 * NOTE: a brick may be 1 or 2 OS nodes. Don't get these confused.
 */
#define UV_MAX_NUMALINK_BLADES	16384

/*
 * Maximum number of C/Mbricks within a software SSI (hardware may support
 * more).
 */
#define UV_MAX_SSI_BLADES	256

/*
 * The largest possible NASID of a C or M brick (+ 2)
 */
#define UV_MAX_NASID_VALUE	(UV_MAX_NUMALINK_NODES * 2)

/*
 * The following defines attributes of the HUB chip. These attributes are
 * frequently referenced and are kept in the per-cpu data areas of each cpu.
 * They are kept together in a struct to minimize cache misses.
 */
struct uv_hub_info_s {
	unsigned long	global_mmr_base;
	unsigned short	local_nasid;
	unsigned short	gnode_upper;
	unsigned short	coherency_domain_number;
	unsigned short	numa_blade_id;
	unsigned char	blade_processor_id;
	unsigned char	m_val;
	unsigned char	n_val;
};
DECLARE_PER_CPU(struct uv_hub_info_s, __uv_hub_info);
#define uv_hub_info 		(&__get_cpu_var(__uv_hub_info))
#define uv_cpu_hub_info(cpu)	(&per_cpu(__uv_hub_info, cpu))

/*
 * Local & Global MMR space macros.
 * 	Note: macros are intended to be used ONLY by inline functions
 * 	in this file - not by other kernel code.
 */
#define UV_SNASID(n)			((n) >> 1)
#define UV_NASID(n)			((n) << 1)

#define UV_LOCAL_MMR_BASE		0xf4000000UL
#define UV_GLOBAL_MMR32_BASE		0xf8000000UL
#define UV_GLOBAL_MMR64_BASE		(uv_hub_info->global_mmr_base)

#define UV_GLOBAL_MMR32_SNASID_MASK	0x3ff
#define UV_GLOBAL_MMR32_SNASID_SHIFT	15
#define UV_GLOBAL_MMR64_SNASID_SHIFT	26

#define UV_GLOBAL_MMR32_NASID_BITS(n)					\
		(((UV_SNASID(n) & UV_GLOBAL_MMR32_SNASID_MASK)) <<	\
		(UV_GLOBAL_MMR32_SNASID_SHIFT))

#define UV_GLOBAL_MMR64_NASID_BITS(n)					\
	((unsigned long)UV_SNASID(n) << UV_GLOBAL_MMR64_SNASID_SHIFT)

#define UV_APIC_NASID_SHIFT	6

/*
 * Extract a NASID from an APICID (full apicid, not processor subset)
 */
static inline int uv_apicid_to_nasid(int apicid)
{
	return (UV_NASID(apicid >> UV_APIC_NASID_SHIFT));
}

/*
 * Access global MMRs using the low memory MMR32 space. This region supports
 * faster MMR access but not all MMRs are accessible in this space.
 */
static inline unsigned long *uv_global_mmr32_address(int nasid,
				unsigned long offset)
{
	return __va(UV_GLOBAL_MMR32_BASE |
		       UV_GLOBAL_MMR32_NASID_BITS(nasid) | offset);
}

static inline void uv_write_global_mmr32(int nasid, unsigned long offset,
				 unsigned long val)
{
	*uv_global_mmr32_address(nasid, offset) = val;
}

static inline unsigned long uv_read_global_mmr32(int nasid,
						 unsigned long offset)
{
	return *uv_global_mmr32_address(nasid, offset);
}

/*
 * Access Global MMR space using the MMR space located at the top of physical
 * memory.
 */
static inline unsigned long *uv_global_mmr64_address(int nasid,
				unsigned long offset)
{
	return __va(UV_GLOBAL_MMR64_BASE |
		       UV_GLOBAL_MMR64_NASID_BITS(nasid) | offset);
}

static inline void uv_write_global_mmr64(int nasid, unsigned long offset,
				unsigned long val)
{
	*uv_global_mmr64_address(nasid, offset) = val;
}

static inline unsigned long uv_read_global_mmr64(int nasid,
						 unsigned long offset)
{
	return *uv_global_mmr64_address(nasid, offset);
}

/*
 * Access node local MMRs. Faster than using global space but only local MMRs
 * are accessible.
 */
static inline unsigned long *uv_local_mmr_address(unsigned long offset)
{
	return __va(UV_LOCAL_MMR_BASE | offset);
}

static inline unsigned long uv_read_local_mmr(unsigned long offset)
{
	return *uv_local_mmr_address(offset);
}

static inline void uv_write_local_mmr(unsigned long offset, unsigned long val)
{
	*uv_local_mmr_address(offset) = val;
}

/*
 * Structures and definitions for converting between cpu, node, and blade
 * numbers.
 */
struct uv_blade_info {
	unsigned short	nr_posible_cpus;
	unsigned short	nr_online_cpus;
	unsigned short	nasid;
};
struct uv_blade_info *uv_blade_info;
extern short *uv_node_to_blade;
extern short *uv_cpu_to_blade;
extern short uv_possible_blades;

/* Blade-local cpu number of current cpu. Numbered 0 .. <# cpus on the blade> */
static inline int uv_blade_processor_id(void)
{
	return uv_hub_info->blade_processor_id;
}

/* Blade number of current cpu. Numnbered 0 .. <#blades -1> */
static inline int uv_numa_blade_id(void)
{
	return uv_hub_info->numa_blade_id;
}

/* Convert a cpu number to the the UV blade number */
static inline int uv_cpu_to_blade_id(int cpu)
{
	return uv_cpu_to_blade[cpu];
}

/* Convert linux node number to the UV blade number */
static inline int uv_node_to_blade_id(int nid)
{
	return uv_node_to_blade[nid];
}

/* Convert a blade id to the NASID of the blade */
static inline int uv_blade_to_nasid(int bid)
{
	return uv_blade_info[bid].nasid;
}

/* Determine the number of possible cpus on a blade */
static inline int uv_blade_nr_possible_cpus(int bid)
{
	return uv_blade_info[bid].nr_posible_cpus;
}

/* Determine the number of online cpus on a blade */
static inline int uv_blade_nr_online_cpus(int bid)
{
	return uv_blade_info[bid].nr_online_cpus;
}

/* Convert a cpu id to the NASID of the blade containing the cpu */
static inline int uv_cpu_to_nasid(int cpu)
{
	return uv_blade_info[uv_cpu_to_blade_id(cpu)].nasid;
}

/* Convert a node number to the NASID of the blade */
static inline int uv_node_to_nasid(int nid)
{
	return uv_blade_info[uv_node_to_blade_id(nid)].nasid;
}

/* Maximum possible number of blades */
static inline int uv_num_possible_blades(void)
{
	return uv_possible_blades;
}

#endif /* __ASM_X86_UV_HUB__ */

