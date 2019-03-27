/*	$NetBSD: ufs_bmap.c,v 1.14 2004/06/20 22:20:18 jmc Exp $	*/
/* From: NetBSD: ufs_bmap.c,v 1.14 2001/11/08 05:00:51 chs Exp */

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1991, 1993
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
 *	@(#)ufs_bmap.c	8.8 (Berkeley) 8/11/95
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/time.h>

#include <assert.h>
#include <errno.h>
#include <strings.h>

#include "makefs.h"

#include <ufs/ufs/dinode.h>
#include <ufs/ffs/fs.h>

#include "ffs/ufs_bswap.h"
#include "ffs/ufs_inode.h"
#include "ffs/ffs_extern.h"

/*
 * Create an array of logical block number/offset pairs which represent the
 * path of indirect blocks required to access a data block.  The first "pair"
 * contains the logical block number of the appropriate single, double or
 * triple indirect block and the offset into the inode indirect block array.
 * Note, the logical block number of the inode single/double/triple indirect
 * block appears twice in the array, once with the offset into the i_ffs_ib and
 * once with the offset into the page itself.
 */
int
ufs_getlbns(struct inode *ip, daddr_t bn, struct indir *ap, int *nump)
{
	daddr_t metalbn, realbn;
	int64_t blockcnt;
	int lbc;
	int i, numlevels, off;
	u_long lognindir;

	lognindir = ffs(NINDIR(ip->i_fs)) - 1;
	if (nump)
		*nump = 0;
	numlevels = 0;
	realbn = bn;
	if ((long)bn < 0)
		bn = -(long)bn;

	assert (bn >= UFS_NDADDR);

	/* 
	 * Determine the number of levels of indirection.  After this loop
	 * is done, blockcnt indicates the number of data blocks possible
	 * at the given level of indirection, and UFS_NIADDR - i is the number
	 * of levels of indirection needed to locate the requested block.
	 */

	bn -= UFS_NDADDR;
	for (lbc = 0, i = UFS_NIADDR;; i--, bn -= blockcnt) {
		if (i == 0)
			return (EFBIG);

		lbc += lognindir;
		blockcnt = (int64_t)1 << lbc;

		if (bn < blockcnt)
			break;
	}

	/* Calculate the address of the first meta-block. */
	if (realbn >= 0)
		metalbn = -(realbn - bn + UFS_NIADDR - i);
	else
		metalbn = -(-realbn - bn + UFS_NIADDR - i);

	/* 
	 * At each iteration, off is the offset into the bap array which is
	 * an array of disk addresses at the current level of indirection.
	 * The logical block number and the offset in that block are stored
	 * into the argument array.
	 */
	ap->in_lbn = metalbn;
	ap->in_off = off = UFS_NIADDR - i;
	ap++;
	for (++numlevels; i <= UFS_NIADDR; i++) {
		/* If searching for a meta-data block, quit when found. */
		if (metalbn == realbn)
			break;

		lbc -= lognindir;
		blockcnt = (int64_t)1 << lbc;
		off = (bn >> lbc) & (NINDIR(ip->i_fs) - 1);

		++numlevels;
		ap->in_lbn = metalbn;
		ap->in_off = off;
		++ap;

		metalbn -= -1 + (off << lbc);
	}
	if (nump)
		*nump = numlevels;
	return (0);
}
