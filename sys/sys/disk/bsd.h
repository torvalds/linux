/*-
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

#ifndef _SYS_DISK_BSD_H_
#define	_SYS_DISK_BSD_H_

/* The disk magic number */
#define BSD_MAGIC		0x82564557U

#define	BSD_NPARTS_MIN		8
#define	BSD_NPARTS_MAX		20

/* Size of bootblock area in sector-size neutral bytes */
#define BSD_BOOTBLOCK_SIZE	8192

/* partition containing whole disk */
#define	BSD_PART_RAW		2

/* partition normally containing swap */
#define	BSD_PART_SWAP		1

/* Drive-type specific data size (in number of 32-bit inegrals) */
#define	BSD_NDRIVEDATA		5

/* Number of spare 32-bit integrals following drive-type data */
#define	BSD_NSPARE		5

struct disklabel {
	uint32_t d_magic;		/* the magic number */
	uint16_t d_type;		/* drive type */
	uint16_t d_subtype;		/* controller/d_type specific */
	char	 d_typename[16];	/* type name, e.g. "eagle" */

	char	 d_packname[16];	/* pack identifier */

			/* disk geometry: */
	uint32_t d_secsize;		/* # of bytes per sector */
	uint32_t d_nsectors;		/* # of data sectors per track */
	uint32_t d_ntracks;		/* # of tracks per cylinder */
	uint32_t d_ncylinders;		/* # of data cylinders per unit */
	uint32_t d_secpercyl;		/* # of data sectors per cylinder */
	uint32_t d_secperunit;		/* # of data sectors per unit */

	/*
	 * Spares (bad sector replacements) below are not counted in
	 * d_nsectors or d_secpercyl.  Spare sectors are assumed to
	 * be physical sectors which occupy space at the end of each
	 * track and/or cylinder.
	 */
	uint16_t d_sparespertrack;	/* # of spare sectors per track */
	uint16_t d_sparespercyl;	/* # of spare sectors per cylinder */
	/*
	 * Alternate cylinders include maintenance, replacement, configuration
	 * description areas, etc.
	 */
	uint32_t d_acylinders;		/* # of alt. cylinders per unit */

			/* hardware characteristics: */
	/*
	 * d_interleave, d_trackskew and d_cylskew describe perturbations
	 * in the media format used to compensate for a slow controller.
	 * Interleave is physical sector interleave, set up by the
	 * formatter or controller when formatting.  When interleaving is
	 * in use, logically adjacent sectors are not physically
	 * contiguous, but instead are separated by some number of
	 * sectors.  It is specified as the ratio of physical sectors
	 * traversed per logical sector.  Thus an interleave of 1:1
	 * implies contiguous layout, while 2:1 implies that logical
	 * sector 0 is separated by one sector from logical sector 1.
	 * d_trackskew is the offset of sector 0 on track N relative to
	 * sector 0 on track N-1 on the same cylinder.  Finally, d_cylskew
	 * is the offset of sector 0 on cylinder N relative to sector 0
	 * on cylinder N-1.
	 */
	uint16_t d_rpm;			/* rotational speed */
	uint16_t d_interleave;		/* hardware sector interleave */
	uint16_t d_trackskew;		/* sector 0 skew, per track */
	uint16_t d_cylskew;		/* sector 0 skew, per cylinder */
	uint32_t d_headswitch;		/* head switch time, usec */
	uint32_t d_trkseek;		/* track-to-track seek, usec */
	uint32_t d_flags;		/* generic flags */
	uint32_t d_drivedata[BSD_NDRIVEDATA];	/* drive-type specific data */
	uint32_t d_spare[BSD_NSPARE];	/* reserved for future use */
	uint32_t d_magic2;		/* the magic number (again) */
	uint16_t d_checksum;		/* xor of data incl. partitions */

			/* filesystem and partition information: */
	uint16_t d_npartitions;	/* number of partitions in following */
	uint32_t d_bbsize;		/* size of boot area at sn0, bytes */
	uint32_t d_sbsize;		/* max size of fs superblock, bytes */
	struct partition {		/* the partition table */
		uint32_t p_size;	/* number of sectors in partition */
		uint32_t p_offset;	/* starting sector */
		uint32_t p_fsize;	/* filesystem basic fragment size */
		uint8_t  p_fstype;	/* filesystem type, see below */
		uint8_t  p_frag;	/* filesystem fragments per block */
		uint16_t p_cpg;		/* filesystem cylinders per group */
	} d_partitions[BSD_NPARTS_MIN];	/* actually may be more */
};
#ifdef CTASSERT
CTASSERT(sizeof(struct disklabel) == 148 + BSD_NPARTS_MIN * 16);
#endif

/* d_type values: */
#define	DTYPE_SMD		1		/* SMD, XSMD; VAX hp/up */
#define	DTYPE_MSCP		2		/* MSCP */
#define	DTYPE_DEC		3		/* other DEC (rk, rl) */
#define	DTYPE_SCSI		4		/* SCSI */
#define	DTYPE_ESDI		5		/* ESDI interface */
#define	DTYPE_ST506		6		/* ST506 etc. */
#define	DTYPE_HPIB		7		/* CS/80 on HP-IB */
#define	DTYPE_HPFL		8		/* HP Fiber-link */
#define	DTYPE_FLOPPY		10		/* floppy */
#define	DTYPE_CCD		11		/* concatenated disk */
#define	DTYPE_VINUM		12		/* vinum volume */
#define	DTYPE_DOC2K		13		/* Msys DiskOnChip */
#define	DTYPE_RAID		14		/* CMU RAIDFrame */
#define	DTYPE_JFS2		16		/* IBM JFS 2 */

/*
 * Filesystem type and version.
 * Used to interpret other filesystem-specific
 * per-partition information.
 */
#define	FS_UNUSED	0		/* unused */
#define	FS_SWAP		1		/* swap */
#define	FS_V6		2		/* Sixth Edition */
#define	FS_V7		3		/* Seventh Edition */
#define	FS_SYSV		4		/* System V */
#define	FS_V71K		5		/* V7 with 1K blocks (4.1, 2.9) */
#define	FS_V8		6		/* Eighth Edition, 4K blocks */
#define	FS_BSDFFS	7		/* 4.2BSD fast filesystem */
#define	FS_MSDOS	8		/* MSDOS filesystem */
#define	FS_BSDLFS	9		/* 4.4BSD log-structured filesystem */
#define	FS_OTHER	10		/* in use, but unknown/unsupported */
#define	FS_HPFS		11		/* OS/2 high-performance filesystem */
#define	FS_ISO9660	12		/* ISO 9660, normally CD-ROM */
#define	FS_BOOT		13		/* partition contains bootstrap */
#define	FS_VINUM	14		/* Vinum drive */
#define	FS_RAID		15		/* RAIDFrame drive */
#define	FS_FILECORE	16		/* Acorn Filecore Filing System */
#define	FS_EXT2FS	17		/* ext2fs */
#define	FS_NTFS		18		/* Windows/NT file system */
#define	FS_CCD		20		/* concatenated disk component */
#define	FS_JFS2		21		/* IBM JFS2 */
#define	FS_HAMMER	22		/* DragonFlyBSD Hammer FS */
#define	FS_HAMMER2	23		/* DragonFlyBSD Hammer2 FS */
#define	FS_UDF		24		/* UDF */
#define	FS_EFS		26		/* SGI's Extent File system */
#define	FS_ZFS		27		/* Sun's ZFS */
#define	FS_NANDFS	30		/* FreeBSD nandfs (NiLFS derived) */

/*
 * flags shared by various drives:
 */
#define	D_REMOVABLE	0x01		/* removable media */
#define	D_ECC		0x02		/* supports ECC */
#define	D_BADSECT	0x04		/* supports bad sector forw. */
#define	D_RAMDISK	0x08		/* disk emulator */
#define	D_CHAIN		0x10		/* can do back-back transfers */

#endif /* !_SYS_DISK_BSD_H_ */
