/*	$OpenBSD: dumprestore.h,v 1.11 2021/01/21 00:16:36 mortimer Exp $	*/
/*	$NetBSD: dumprestore.h,v 1.14 2005/12/26 19:01:47 perry Exp $	*/

/*
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
#define FS_UFS2_MAGIC   (int)0x19540119
#endif
#define CHECKSUM	(int)84446

extern union u_spcl {
	char dummy[TP_BSIZE];
	struct	s_spcl {
		int32_t	c_type;		    /* record type (see below) */
		int32_t	c_old_date;	    /* date of this dump */
		int32_t	c_old_ddate;	    /* date of previous dump */
		int32_t	c_volume;	    /* dump volume number */
		int32_t	c_old_tapea;	    /* logical block of this record */
		uint32_t c_inumber;	    /* number of inode */
		int32_t	c_magic;	    /* magic number (see above) */
		int32_t	c_checksum;	    /* record checksum */
		union {
			struct ufs1_dinode __uc_dinode;
			struct {
				uint16_t __uc_mode;
				int16_t __uc_spare1[3];
				uint64_t __uc_size;
				int32_t __uc_old_atime;
				int32_t __uc_atimensec;
				int32_t __uc_old_mtime;
				int32_t __uc_mtimensec;
				int32_t __uc_spare2[2];
				int32_t __uc_rdev;
				int32_t __uc_birthtimensec;
				int64_t __uc_birthtime;
				int64_t __uc_atime;
				int64_t __uc_mtime;
				int32_t __uc_spare4[7];
				uint32_t __uc_file_flags;
				int32_t __uc_spare5[2];
				uint32_t __uc_uid;
				uint32_t __uc_gid;
				int32_t __uc_spare6[2];
			} __uc_ino;
		} __c_ino;
		int32_t	c_count;	    /* number of valid c_addr entries */
		char	c_addr[TP_NINDIR];  /* 1 => data; 0 => hole in inode */
		char	c_label[LBLSIZE];   /* dump label */
		int32_t	c_level;	    /* level of this dump */
		char	c_filesys[NAMELEN]; /* name of dumped file system */
		char	c_dev[NAMELEN];	    /* name of dumped device */
		char	c_host[NAMELEN];    /* name of dumped host */
		int32_t	c_flags;	    /* additional information */
		int32_t	c_old_firstrec;	    /* first record on volume */
		int64_t c_date;		    /* date of this dump */
		int64_t c_ddate;	    /* date of previous dump */
		int64_t c_tapea;	    /* logical block of this record */
		int64_t c_firstrec;	    /* first record on volume */
		int32_t	c_spare[24];	    /* reserved for future uses */
	} s_spcl;
} u_spcl;
#define spcl u_spcl.s_spcl

#define c_dinode	__c_ino.__uc_dinode
#define c_mode		__c_ino.__uc_ino.__uc_mode
#define c_spare1	__c_ino.__uc_ino.__uc_spare1
#define c_size		__c_ino.__uc_ino.__uc_size
#define c_old_atime	__c_ino.__uc_ino.__uc_old_atime
#define c_atime		__c_ino.__uc_ino.__uc_atime
#define c_atimensec	__c_ino.__uc_ino.__uc_atimensec
#define c_mtime		__c_ino.__uc_ino.__uc_mtime
#define c_mtimensec	__c_ino.__uc_ino.__uc_mtimensec
#define c_birthtime	__c_ino.__uc_ino.__uc_birthtime
#define c_birthtimensec	__c_ino.__uc_ino.__uc_birthtimensec
#define c_old_mtime	__c_ino.__uc_ino.__uc_old_mtime
#define c_rdev		__c_ino.__uc_ino.__uc_rdev
#define c_file_flags	__c_ino.__uc_ino.__uc_file_flags
#define c_uid		__c_ino.__uc_ino.__uc_uid
#define c_gid		__c_ino.__uc_ino.__uc_gid

/*
 * special record types
 */
#define TS_TAPE 	1	/* dump tape header */
#define TS_INODE	2	/* beginning of file record */
#define TS_ADDR 	4	/* continuation of file record */
#define TS_BITS 	3	/* map of inodes on tape */
#define TS_CLRI 	6	/* map of inodes deleted since last dump */
#define TS_END  	5	/* end of volume marker */

/*
 * flag values
 */
#define DR_NEWHEADER	0x0001	/* new format tape header */
#define DR_NEWINODEFMT	0x0002	/* new format inodes on tape */

#define	DUMPOUTFMT	"%-18s %c %s"		/* for printf */
						/* name, level, ctime(date) */
#define	DUMPINFMT	"%18s %c %[^\n]\n"	/* inverse for scanf */

#endif /* !_PROTOCOLS_DUMPRESTORE_H_ */
