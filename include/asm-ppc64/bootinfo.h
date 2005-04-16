/*
 * Non-machine dependent bootinfo structure.  Basic idea
 * borrowed from the m68k.
 *
 * Copyright (C) 1999 Cort Dougan <cort@ppc.kernel.org>
 * Copyright (c) 2001 PPC64 Team, IBM Corp 
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */


#ifndef _PPC64_BOOTINFO_H
#define _PPC64_BOOTINFO_H

#include <asm/types.h>

/* We use a u32 for the type of the fields since they're written by
 * the bootloader which is a 32-bit process and read by the kernel
 * which is a 64-bit process.  This way they can both agree on the
 * size of the type.
 */
typedef u32 bi_rec_field;

struct bi_record {
	bi_rec_field tag;	/* tag ID */
	bi_rec_field size;	/* size of record (in bytes) */
	bi_rec_field data[0];	/* data */
};

#define BI_FIRST		0x1010  /* first record - marker */
#define BI_LAST			0x1011	/* last record - marker */
#define BI_CMD_LINE		0x1012
#define BI_BOOTLOADER_ID	0x1013
#define BI_INITRD		0x1014
#define BI_SYSMAP		0x1015
#define BI_MACHTYPE		0x1016

static __inline__ struct bi_record * bi_rec_init(unsigned long addr)
{
	struct bi_record *bi_recs;
	bi_recs = (struct bi_record *)_ALIGN(addr, PAGE_SIZE);
	bi_recs->size = 0;
	return bi_recs;
}

static __inline__ struct bi_record * bi_rec_alloc(struct bi_record *rec,
						  unsigned long args)
{
	rec = (struct bi_record *)((unsigned long)rec + rec->size);
	rec->size = sizeof(struct bi_record) + args*sizeof(bi_rec_field);
	return rec;
}

static __inline__ struct bi_record * bi_rec_alloc_bytes(struct bi_record *rec,
							unsigned long bytes)
{
	rec = (struct bi_record *)((unsigned long)rec + rec->size);
	rec->size = sizeof(struct bi_record) + bytes;
	return rec;
}

static __inline__ struct bi_record * bi_rec_next(struct bi_record *rec)
{
	return (struct bi_record *)((unsigned long)rec + rec->size);
}

#endif /* _PPC64_BOOTINFO_H */
