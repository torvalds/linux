/*	$OpenBSD: direntry.h,v 1.8 2021/12/23 02:12:52 jsg Exp $	*/
/*	$NetBSD: direntry.h,v 1.13 1997/10/17 11:23:45 ws Exp $	*/

/*-
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
/*
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

/*
 * Structure of a dos directory entry.
 */
struct direntry {
	u_int8_t	deName[8];	/* filename, blank filled */
#define	SLOT_EMPTY	0x00		/* slot has never been used */
#define	SLOT_E5		0x05		/* the real value is 0xe5 */
#define	SLOT_DELETED	0xe5		/* file in this slot deleted */
	u_int8_t	deExtension[3];	/* extension, blank filled */
	u_int8_t	deAttributes;	/* file attributes */
#define	ATTR_NORMAL	0x00		/* normal file */
#define	ATTR_READONLY	0x01		/* file is readonly */
#define	ATTR_HIDDEN	0x02		/* file is hidden */
#define	ATTR_SYSTEM	0x04		/* file is a system file */
#define	ATTR_VOLUME	0x08		/* entry is a volume label */
#define	ATTR_DIRECTORY	0x10		/* entry is a directory name */
#define	ATTR_ARCHIVE	0x20		/* file is new or modified */
	u_int8_t	deLowerCase;	/* case for base and extension */
#define	CASE_LOWER_BASE	0x08		/* base is lower case */
#define	CASE_LOWER_EXT	0x10		/* extension is lower case */
	u_int8_t	deCTimeHundredth; /* create time, 1/100th of a sec */
	u_int8_t	deCTime[2];	/* create time */
	u_int8_t	deCDate[2];	/* create date */
	u_int8_t	deADate[2];	/* access date */
	u_int8_t	deHighClust[2];	/* high byte of cluster number */
	u_int8_t	deMTime[2];	/* last update time */
	u_int8_t	deMDate[2];	/* last update date */
	u_int8_t	deStartCluster[2]; /* starting cluster of file */
	u_int8_t	deFileSize[4];	/* size of file in bytes */
};

/*
 * Structure of a Win95 long name directory entry
 */
struct winentry {
	u_int8_t	weCnt;
#define	WIN_LAST	0x40
#define	WIN_CNT		0x3f
	u_int8_t	wePart1[10];
	u_int8_t	weAttributes;
#define	ATTR_WIN95	0x0f
	u_int8_t	weReserved1;
	u_int8_t	weChksum;
	u_int8_t	wePart2[12];
	u_int16_t	weReserved2;
	u_int8_t	wePart3[4];
};
#define	WIN_CHARS	13	/* Number of chars per winentry */

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
void unix2dostime(struct timespec *tsp, u_int16_t *ddp, u_int16_t *dtp, u_int8_t *dhp);
void dos2unixtime(u_int dd, u_int dt, u_int dh, struct timespec *tsp);
int dos2unixfn(u_char dn[11], u_char *un, int lower);
int unix2dosfn(u_char *un, u_char dn[11], int unlen, u_int gen);
int unix2winfn(u_char *un, int unlen, struct winentry *wep, int cnt, int chksum);
int winChkName(u_char *un, int unlen, struct winentry *wep, int chksum);
int win2unixfn(struct winentry *wep, struct dirent *dp, int chksum);
u_int8_t winChksum(u_int8_t *name);
int winSlotCnt(u_char *un, int unlen);
#endif	/* _KERNEL */
