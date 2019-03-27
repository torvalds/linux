/* $FreeBSD$ */
/*	$NetBSD: fat.h,v 1.12 1997/11/17 15:36:36 ws Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (C) 1994, 1997 Wolfgang Solfrank.
 * Copyright (C) 1994, 1997 TooLs GmbH.
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

#ifndef _FS_MSDOSFS_FAT_H_
#define	_FS_MSDOSFS_FAT_H_
/*
 * Some useful cluster numbers.
 */
#define	MSDOSFSROOT	0		/* cluster 0 means the root dir */
#define	CLUST_FREE	0		/* cluster 0 also means a free cluster */
#define	MSDOSFSFREE	CLUST_FREE
#define	CLUST_FIRST	2		/* first legal cluster number */
#define	CLUST_RSRVD	0xfffffff6	/* reserved cluster range */
#define	CLUST_BAD	0xfffffff7	/* a cluster with a defect */
#define	CLUST_EOFS	0xfffffff8	/* start of eof cluster range */
#define	CLUST_EOFE	0xffffffff	/* end of eof cluster range */

#define	FAT12_MASK	0x00000fff	/* mask for 12 bit cluster numbers */
#define	FAT16_MASK	0x0000ffff	/* mask for 16 bit cluster numbers */
#define	FAT32_MASK	0x0fffffff	/* mask for FAT32 cluster numbers */

/*
 * MSDOSFS:
 * Return true if filesystem uses 12 bit FATs. Microsoft Programmer's
 * Reference says if the maximum cluster number in a filesystem is greater
 * than 4084 ((CLUST_RSRVD - CLUST_FIRST) & FAT12_MASK) then we've got a
 * 16 bit FAT filesystem. While mounting, the result of this test is stored
 * in pm_fatentrysize.
 */
#define	FAT12(pmp)	(pmp->pm_fatmask == FAT12_MASK)
#define	FAT16(pmp)	(pmp->pm_fatmask == FAT16_MASK)
#define	FAT32(pmp)	(pmp->pm_fatmask == FAT32_MASK)

#define	MSDOSFSEOF(pmp, cn)	((((cn) | ~(pmp)->pm_fatmask) & CLUST_EOFS) == CLUST_EOFS)

#ifdef _KERNEL
/*
 * These are the values for the function argument to the function
 * fatentry().
 */
#define	FAT_GET		0x0001	/* get a FAT entry */
#define	FAT_SET		0x0002	/* set a FAT entry */
#define	FAT_GET_AND_SET	(FAT_GET | FAT_SET)

/*
 * Flags to extendfile:
 */
#define	DE_CLEAR	1	/* Zero out the blocks allocated */

int pcbmap(struct denode *dep, u_long findcn, daddr_t *bnp, u_long *cnp, int* sp);
int clusterfree(struct msdosfsmount *pmp, u_long cn, u_long *oldcnp);
int clusteralloc(struct msdosfsmount *pmp, u_long start, u_long count, u_long fillwith, u_long *retcluster, u_long *got);
int fatentry(int function, struct msdosfsmount *pmp, u_long cluster, u_long *oldcontents, u_long newcontents);
int freeclusterchain(struct msdosfsmount *pmp, u_long startchain);
int extendfile(struct denode *dep, u_long count, struct buf **bpp, u_long *ncp, int flags);
void fc_purge(struct denode *dep, u_int frcn);
int markvoldirty(struct msdosfsmount *pmp, int dirty);

#endif	/* _KERNEL */
#endif	/* !_FS_MSDOSFS_FAT_H_ */
