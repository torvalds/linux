/* $FreeBSD$ */
/*	$NetBSD: direntry.h,v 1.14 1997/11/17 15:36:32 ws Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (C) 1994, 1995, 1997 Wolfgang Solfrank.
 * Copyright (C) 1994, 1995, 1997 TooLs GmbH.
 * All rights reserved.
 * Original code by Paul Popelka (paulp@uts.amdahl.com) (see below).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*-
 * Written by Paul Popelka (paulp@uts.amdahl.com)
 *
 * You can do anything you want with this software, just don't say you wrote
 * it, and don't remove this notice.
 *
 * This software is provided "as is".
 *
 * The author supplies this software to be publicly redistributed on the
 * understanding that the author is not responsible for the correct
 * functioning of this software in any circumstances and is not liable for
 * any damages caused by this software.
 *
 * October 1992
 */
#ifndef _FS_MSDOSFS_DIRENTRY_H_
#define	_FS_MSDOSFS_DIRENTRY_H_

/*
 * Structure of a dos directory entry.
 */
struct direntry {
	uint8_t		deName[11];	/* filename, blank filled */
#define	SLOT_EMPTY	0x00		/* slot has never been used */
#define	SLOT_E5		0x05		/* the real value is 0xe5 */
#define	SLOT_DELETED	0xe5		/* file in this slot deleted */
	uint8_t		deAttributes;	/* file attributes */
#define	ATTR_NORMAL	0x00		/* normal file */
#define	ATTR_READONLY	0x01		/* file is readonly */
#define	ATTR_HIDDEN	0x02		/* file is hidden */
#define	ATTR_SYSTEM	0x04		/* file is a system file */
#define	ATTR_VOLUME	0x08		/* entry is a volume label */
#define	ATTR_DIRECTORY	0x10		/* entry is a directory name */
#define	ATTR_ARCHIVE	0x20		/* file is new or modified */
	uint8_t		deLowerCase;	/* NT VFAT lower case flags */
#define	LCASE_BASE	0x08		/* filename base in lower case */
#define	LCASE_EXT	0x10		/* filename extension in lower case */
	uint8_t		deCHundredth;	/* hundredth of seconds in CTime */
	uint8_t		deCTime[2];	/* create time */
	uint8_t		deCDate[2];	/* create date */
	uint8_t		deADate[2];	/* access date */
	uint8_t		deHighClust[2];	/* high bytes of cluster number */
	uint8_t		deMTime[2];	/* last update time */
	uint8_t		deMDate[2];	/* last update date */
	uint8_t		deStartCluster[2]; /* starting cluster of file */
	uint8_t		deFileSize[4];	/* size of file in bytes */
};

/*
 * Structure of a Win95 long name directory entry
 */
struct winentry {
	uint8_t		weCnt;
#define	WIN_LAST	0x40
#define	WIN_CNT		0x3f
	uint8_t		wePart1[10];
	uint8_t		weAttributes;
#define	ATTR_WIN95	0x0f
	uint8_t		weReserved1;
	uint8_t		weChksum;
	uint8_t		wePart2[12];
	uint16_t	weReserved2;
	uint8_t		wePart3[4];
};
#define	WIN_CHARS	13	/* Number of chars per winentry */

/*
 * Maximum number of winentries for a filename.
 */
#define	WIN_MAXSUBENTRIES 20

/*
 * Maximum filename length in Win95
 * Note: Must be < sizeof(dirent.d_name)
 */
#define	WIN_MAXLEN	255

/*
 * This is the format of the contents of the deTime field in the direntry
 * structure.
 * We don't use bitfields because we don't know how compilers for
 * arbitrary machines will lay them out.
 */
#define DT_2SECONDS_MASK	0x1F	/* seconds divided by 2 */
#define DT_2SECONDS_SHIFT	0
#define DT_MINUTES_MASK		0x7E0	/* minutes */
#define DT_MINUTES_SHIFT	5
#define DT_HOURS_MASK		0xF800	/* hours */
#define DT_HOURS_SHIFT		11

/*
 * This is the format of the contents of the deDate field in the direntry
 * structure.
 */
#define DD_DAY_MASK		0x1F	/* day of month */
#define DD_DAY_SHIFT		0
#define DD_MONTH_MASK		0x1E0	/* month */
#define DD_MONTH_SHIFT		5
#define DD_YEAR_MASK		0xFE00	/* year - 1980 */
#define DD_YEAR_SHIFT		9

#ifdef _KERNEL
struct mbnambuf {
	size_t	nb_len;
	int	nb_last_id;
	char	nb_buf[WIN_MAXLEN + 1];
};

struct dirent;
struct msdosfsmount;

char	*mbnambuf_flush(struct mbnambuf *nbp, struct dirent *dp);
void	mbnambuf_init(struct mbnambuf *nbp);
int	mbnambuf_write(struct mbnambuf *nbp, char *name, int id);
int	dos2unixfn(u_char dn[11], u_char *un, int lower,
	    struct msdosfsmount *pmp);
int	unix2dosfn(const u_char *un, u_char dn[12], size_t unlen, u_int gen,
	    struct msdosfsmount *pmp);
int	unix2winfn(const u_char *un, size_t unlen, struct winentry *wep, int cnt,
	    int chksum, struct msdosfsmount *pmp);
int	winChkName(struct mbnambuf *nbp, const u_char *un, size_t unlen,
	    int chksum, struct msdosfsmount *pmp);
int	win2unixfn(struct mbnambuf *nbp, struct winentry *wep, int chksum,
	    struct msdosfsmount *pmp);
uint8_t winChksum(uint8_t *name);
int	winSlotCnt(const u_char *un, size_t unlen, struct msdosfsmount *pmp);
size_t	winLenFixup(const u_char *un, size_t unlen);
#endif	/* _KERNEL */
#endif	/* !_FS_MSDOSFS_DIRENTRY_H_ */
