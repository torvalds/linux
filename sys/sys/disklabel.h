/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1987, 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)disklabel.h	8.2 (Berkeley) 7/10/94
 * $FreeBSD$
 */

#ifndef _SYS_DISKLABEL_H_
#define	_SYS_DISKLABEL_H_

#ifndef _KERNEL
#include <sys/types.h>
#endif
#include <sys/ioccom.h>

#include <sys/disk/bsd.h>

/* Disk description table, see disktab(5) */
#define	_PATH_DISKTAB	"/etc/disktab"

/*
 * The label is in block 0 or 1, possibly offset from the beginning
 * to leave room for a bootstrap, etc.
 * XXX these should be defined per controller (or drive) elsewhere, not here!
 * XXX in actuality it can't even be per controller or drive. It should be
 * constant/fixed across storage hardware and CPU architectures. Disks can
 * travel from one machine to another and a label created on one machine
 * should be detectable and understood by the other.
 */
#define LABELSECTOR	1			/* sector containing label */
#define LABELOFFSET	0			/* offset of label in sector */

#define DISKMAGIC	BSD_MAGIC		/* The disk magic number */

#ifndef MAXPARTITIONS
#define	MAXPARTITIONS	BSD_NPARTS_MIN
#endif

/* Size of bootblock area in sector-size neutral bytes */
#define BBSIZE		BSD_BOOTBLOCK_SIZE

#define	LABEL_PART	BSD_PART_RAW
#define	RAW_PART	BSD_PART_RAW
#define	SWAP_PART	BSD_PART_SWAP

#define NDDATA		BSD_NDRIVEDATA
#define NSPARE		BSD_NSPARE

static __inline u_int16_t dkcksum(struct disklabel *lp);
static __inline u_int16_t
dkcksum(struct disklabel *lp)
{
	u_int16_t *start, *end;
	u_int16_t sum = 0;

	start = (u_int16_t *)lp;
	end = (u_int16_t *)&lp->d_partitions[lp->d_npartitions];
	while (start < end)
		sum ^= *start++;
	return (sum);
}

#ifdef DKTYPENAMES
static const char *dktypenames[] = {
	"unknown",
	"SMD",
	"MSCP",
	"old DEC",
	"SCSI",
	"ESDI",
	"ST506",
	"HP-IB",
	"HP-FL",
	"type 9",
	"floppy",
	"CCD",
	"Vinum",
	"DOC2K",
	"Raid",
	"?",
	"jfs",
	NULL
};
#define DKMAXTYPES	(sizeof(dktypenames) / sizeof(dktypenames[0]) - 1)
#endif

#ifdef	FSTYPENAMES
static const char *fstypenames[] = {
	"unused",
	"swap",
	"Version 6",
	"Version 7",
	"System V",
	"4.1BSD",
	"Eighth Edition",
	"4.2BSD",
	"MSDOS",
	"4.4LFS",
	"unknown",
	"HPFS",
	"ISO9660",
	"boot",
	"vinum",
	"raid",
	"Filecore",
	"EXT2FS",
	"NTFS",
	"?",
	"ccd",
	"jfs",
	"HAMMER",
	"HAMMER2",
	"UDF",
	"?",
	"EFS",
	"ZFS",
	"?",
	"?",
	"nandfs",
	NULL
};
#define FSMAXTYPES	(sizeof(fstypenames) / sizeof(fstypenames[0]) - 1)
#endif

/*
 * NB: <sys/disk.h> defines ioctls from 'd'/128 and up.
 */

/*
 * Functions for proper encoding/decoding of struct disklabel into/from
 * bytestring.
 */
void bsd_partition_le_dec(u_char *ptr, struct partition *d);
int bsd_disklabel_le_dec(u_char *ptr, struct disklabel *d, int maxpart);
void bsd_partition_le_enc(u_char *ptr, struct partition *d);
void bsd_disklabel_le_enc(u_char *ptr, struct disklabel *d);

#ifndef _KERNEL
__BEGIN_DECLS
struct disklabel *getdiskbyname(const char *);
__END_DECLS
#endif

#endif /* !_SYS_DISKLABEL_H_ */
