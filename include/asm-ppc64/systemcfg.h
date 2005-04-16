#ifndef _SYSTEMCFG_H
#define _SYSTEMCFG_H

/* 
 * Copyright (C) 2002 Peter Bergner <bergner@vnet.ibm.com>, IBM
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/* Change Activity:
 * 2002/09/30 : bergner  : Created
 * End Change Activity 
 */

/*
 * If the major version changes we are incompatible.
 * Minor version changes are a hint.
 */
#define SYSTEMCFG_MAJOR 1
#define SYSTEMCFG_MINOR 1

#ifndef __ASSEMBLY__

#include <linux/unistd.h>

#define SYSCALL_MAP_SIZE      ((__NR_syscalls + 31) / 32)

struct systemcfg {
	__u8  eye_catcher[16];		/* Eyecatcher: SYSTEMCFG:PPC64	0x00 */
	struct {			/* Systemcfg version numbers	     */
		__u32 major;		/* Major number			0x10 */
		__u32 minor;		/* Minor number			0x14 */
	} version;

	__u32 platform;			/* Platform flags		0x18 */
	__u32 processor;		/* Processor type		0x1C */
	__u64 processorCount;		/* # of physical processors	0x20 */
	__u64 physicalMemorySize;	/* Size of real memory(B)	0x28 */
	__u64 tb_orig_stamp;		/* Timebase at boot		0x30 */
	__u64 tb_ticks_per_sec;		/* Timebase tics / sec		0x38 */
	__u64 tb_to_xs;			/* Inverse of TB to 2^20	0x40 */
	__u64 stamp_xsec;		/*				0x48 */
	__u64 tb_update_count;		/* Timebase atomicity ctr	0x50 */
	__u32 tz_minuteswest;		/* Minutes west of Greenwich	0x58 */
	__u32 tz_dsttime;		/* Type of dst correction	0x5C */
	/* next four are no longer used except to be exported to /proc */
	__u32 dcache_size;		/* L1 d-cache size		0x60 */
	__u32 dcache_line_size;		/* L1 d-cache line size		0x64 */
	__u32 icache_size;		/* L1 i-cache size		0x68 */
	__u32 icache_line_size;		/* L1 i-cache line size		0x6C */
   	__u32 syscall_map_64[SYSCALL_MAP_SIZE]; /* map of available syscalls 0x70 */
   	__u32 syscall_map_32[SYSCALL_MAP_SIZE]; /* map of available syscalls */
};

#ifdef __KERNEL__
extern struct systemcfg *systemcfg;
#endif

#endif /* __ASSEMBLY__ */

#endif /* _SYSTEMCFG_H */
