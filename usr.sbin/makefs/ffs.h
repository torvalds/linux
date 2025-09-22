/*	$OpenBSD: ffs.h,v 1.10 2023/04/25 08:57:11 krw Exp $	*/
/*	$NetBSD: ffs.h,v 1.2 2011/10/09 21:33:43 christos Exp $	*/

/*
 * Copyright (c) 2001-2003 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Luke Mewburn for Wasabi Systems, Inc.
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
 *      This product includes software developed for the NetBSD Project by
 *      Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _FFS_H
#define _FFS_H

typedef struct {
	char	label[MAXVOLLEN];	/* volume name/label */
	int	bsize;		/* block size */
	int	fsize;		/* fragment size */
	int	density;	/* bytes per inode */
	int	minfree;	/* free space threshold */
	int	optimization;	/* optimization (space or time) */
	int	maxbpg;		/* maximum blocks per file in a cyl group */
	int	avgfilesize;	/* expected average file size */
	int	avgfpdir;	/* expected # of files per directory */
	int	version;	/* filesystem version (1 = FFS, 2 = UFS2) */
	int	maxbsize;	/* maximum extent size */
	int	maxblkspercg;	/* max # of blocks per cylinder group */
	int	rdroot;		/* create rdroot disklabel */

	struct disklabel *lp;	/* disk label */
	struct partition *pp;	/* matching FFS partition in disklabel */
} ffs_opt_t;

#endif /* _FFS_H */
