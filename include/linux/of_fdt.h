/*
 * Definitions for working with the Flattened Device Tree data format
 *
 * Copyright 2009 Benjamin Herrenschmidt, IBM Corp
 * benh@kernel.crashing.org
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#ifndef _LINUX_OF_FDT_H
#define _LINUX_OF_FDT_H

/* Definitions used by the flattened device tree */
#define OF_DT_HEADER		0xd00dfeed	/* marker */
#define OF_DT_BEGIN_NODE	0x1		/* Start of node, full name */
#define OF_DT_END_NODE		0x2		/* End node */
#define OF_DT_PROP		0x3		/* Property: name off, size,
						 * content */
#define OF_DT_NOP		0x4		/* nop */
#define OF_DT_END		0x9

#define OF_DT_VERSION		0x10

#ifndef __ASSEMBLY__
/*
 * This is what gets passed to the kernel by prom_init or kexec
 *
 * The dt struct contains the device tree structure, full pathes and
 * property contents. The dt strings contain a separate block with just
 * the strings for the property names, and is fully page aligned and
 * self contained in a page, so that it can be kept around by the kernel,
 * each property name appears only once in this page (cheap compression)
 *
 * the mem_rsvmap contains a map of reserved ranges of physical memory,
 * passing it here instead of in the device-tree itself greatly simplifies
 * the job of everybody. It's just a list of u64 pairs (base/size) that
 * ends when size is 0
 */
struct boot_param_header {
	u32	magic;			/* magic word OF_DT_HEADER */
	u32	totalsize;		/* total size of DT block */
	u32	off_dt_struct;		/* offset to structure */
	u32	off_dt_strings;		/* offset to strings */
	u32	off_mem_rsvmap;		/* offset to memory reserve map */
	u32	version;		/* format version */
	u32	last_comp_version;	/* last compatible version */
	/* version 2 fields below */
	u32	boot_cpuid_phys;	/* Physical CPU id we're booting on */
	/* version 3 fields below */
	u32	dt_strings_size;	/* size of the DT strings block */
	/* version 17 fields below */
	u32	dt_struct_size;		/* size of the DT structure block */
};

#endif /* __ASSEMBLY__ */
#endif /* _LINUX_OF_FDT_H */
