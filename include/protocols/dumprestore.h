/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *	@(#)dumprestore.h	8.2 (Berkeley) 1/21/94
 *
 * $FreeBSD$
 */

#ifndef _PROTOCOLS_DUMPRESTORE_H_
#define _PROTOCOLS_DUMPRESTORE_H_

/*
 * TP_BSIZE is the size of file blocks on the dump tapes.
 * Note that TP_BSIZE must be a multiple of DEV_BSIZE.
 *
 * NTREC is the number of TP_BSIZE blocks that are written
 * in each tape record. HIGHDENSITYTREC is the number of
 * TP_BSIZE blocks that are written in each tape record on
 * 6250 BPI or higher density tapes.
 *
 * TP_NINDIR is the number of indirect pointers in a TS_INODE
 * or TS_ADDR record. Note that it must be a power of two.
 */
#define TP_BSIZE	1024
#define NTREC   	10
#define HIGHDENSITYTREC	32
#define TP_NINDIR	(TP_BSIZE/2)
#define LBLSIZE		16
#define NAMELEN		64

#define OFS_MAGIC   	(int)60011
#define NFS_MAGIC   	(int)60012
#ifndef FS_UFS2_MAGIC
#define FS_UFS2_MAGIC  	(int)0x19540119
#endif
#define CHECKSUM	(int)84446

/*
 * Since ino_t size is changing to 64-bits, yet we desire this structure to
 * remain compatible with exiting dump formats, we do NOT use ino_t here,
 * but rather define a 32-bit type in its place.  At some point, it may be
 * necessary to use some of the c_spare[] in order to fully support 64-bit
 * inode numbers.
 */
typedef uint32_t dump_ino_t;

union u_spcl {
	char dummy[TP_BSIZE];
	struct	s_spcl {
		int32_t	c_type;		    /* record type (see below) */
		int32_t	c_old_date;	    /* date of this dump */
		int32_t	c_old_ddate;	    /* date of previous dump */
		int32_t	c_volume;	    /* dump volume number */
		int32_t	c_old_tapea;	    /* logical block of this record */
		dump_ino_t c_inumber;	    /* number of inode */
		int32_t	c_magic;	    /* magic number (see above) */
		int32_t	c_checksum;	    /* record checksum */
		/*
		 * Start old dinode structure, expanded for binary
		 * compatibility with UFS1.
		 */
		u_int16_t c_mode;	    /* file mode */
		int16_t	c_spare1[3];	    /* old nlink, ids */
		u_int64_t c_size;	    /* file byte count */
		int32_t	c_old_atime;	    /* old last access time, seconds */
		int32_t	c_atimensec;	    /* last access time, nanoseconds */
		int32_t	c_old_mtime;	    /* old last modified time, secs */
		int32_t	c_mtimensec;	    /* last modified time, nanosecs */
		int32_t	c_spare2[2];	    /* old ctime */
		int32_t	c_rdev;		    /* for devices, device number */
		int32_t	c_birthtimensec;    /* creation time, nanosecs */
		int64_t	c_birthtime;	    /* creation time, seconds */
		int64_t	c_atime;	    /* last access time, seconds */
		int64_t	c_mtime;	    /* last modified time, seconds */
		int32_t	c_extsize;	    /* external attribute size */
		int32_t	c_spare4[6];	    /* old block pointers */
		u_int32_t c_file_flags;	    /* status flags (chflags) */
		int32_t	c_spare5[2];	    /* old blocks, generation number */
		u_int32_t c_uid;	    /* file owner */
		u_int32_t c_gid;	    /* file group */
		int32_t	c_spare6[2];	    /* previously unused spares */
		/*
		 * End old dinode structure.
		 */
		int32_t	c_count;	    /* number of valid c_addr entries */
		char	c_addr[TP_NINDIR];  /* 1 => data; 0 => hole in inode */
		char	c_label[LBLSIZE];   /* dump label */
		int32_t	c_level;	    /* level of this dump */
		char	c_filesys[NAMELEN]; /* name of dumpped file system */
		char	c_dev[NAMELEN];	    /* name of dumpped device */
		char	c_host[NAMELEN];    /* name of dumpped host */
		int32_t	c_flags;	    /* additional information */
		int32_t	c_old_firstrec;	    /* first record on volume */
		int64_t	c_date;		    /* date of this dump */
		int64_t	c_ddate;	    /* date of previous dump */
		int64_t	c_tapea;	    /* logical block of this record */
		int64_t	c_firstrec;	    /* first record on volume */
		int32_t	c_spare[24];	    /* reserved for future uses */
	} s_spcl;
} u_spcl;
#define spcl u_spcl.s_spcl
/*
 * special record types
 */
#define TS_TAPE 	1	/* dump tape header */
#define TS_INODE	2	/* beginning of file record */
#define TS_ADDR 	4	/* continuation of file record */
#define TS_BITS 	3	/* map of inodes on tape */
#define TS_CLRI 	6	/* map of inodes deleted since last dump */
#define TS_END  	5	/* end of volume marker */

#endif /* !_DUMPRESTORE_H_ */
