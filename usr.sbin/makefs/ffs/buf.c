/*	$OpenBSD: buf.c,v 1.7 2021/10/06 00:40:41 deraadt Exp $	*/
/*	$NetBSD: buf.c,v 1.24 2016/06/24 19:24:11 christos Exp $	*/

/*
 * Copyright (c) 2001 Wasabi Systems, Inc.
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

#include <sys/time.h>

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <util.h>

#include "makefs.h"
#include "buf.h"

TAILQ_HEAD(buftailhead,mkfsbuf) buftail;

int
bread(struct mkfsvnode *vp, daddr_t blkno, int size, int u2 __unused,
	struct mkfsbuf **bpp)
{
	off_t	offset;
	ssize_t	rv;
	fsinfo_t *fs = vp->fs;

	assert (bpp != NULL);

	*bpp = getblk(vp, blkno, size, 0, 0);
	offset = (*bpp)->b_blkno * fs->sectorsize + fs->offset;
	if (lseek((*bpp)->b_fs->fd, offset, SEEK_SET) == -1)
		err(1, "%s: lseek %lld (%lld)", __func__,
		    (long long)(*bpp)->b_blkno, (long long)offset);
	rv = read((*bpp)->b_fs->fd, (*bpp)->b_data, (size_t)(*bpp)->b_bcount);
	if (rv == -1)				/* read error */
		err(1, "%s: read %ld (%lld) returned %zd", __func__,
		    (*bpp)->b_bcount, (long long)offset, rv);
	else if (rv != (*bpp)->b_bcount)	/* short read */
		errx(1, "%s: read %ld (%lld) returned %zd", __func__,
		    (*bpp)->b_bcount, (long long)offset, rv);
	else
		return (0);
}

void
brelse(struct mkfsbuf *bp, int u1 __unused)
{

	assert (bp != NULL);
	assert (bp->b_data != NULL);

	if (bp->b_lblkno < 0) {
		/*
		 * XXX	don't remove any buffers with negative logical block
		 *	numbers (lblkno), so that we retain the mapping
		 *	of negative lblkno -> real blkno that ffs_balloc()
		 *	sets up.
		 *
		 *	if we instead released these buffers, and implemented
		 *	ufs_strategy() (and ufs_bmaparray()) and called those
		 *	from bread() and bwrite() to convert the lblkno to
		 *	a real blkno, we'd add a lot more code & complexity
		 *	and reading off disk, for little gain, because this
		 *	simple hack works for our purpose.
		 */
		bp->b_bcount = 0;
		return;
	}

	TAILQ_REMOVE(&buftail, bp, b_tailq);
	free(bp->b_data);
	free(bp);
}

int
bwrite(struct mkfsbuf *bp)
{
	off_t	offset;
	ssize_t	rv;
	size_t	bytes;
	fsinfo_t *fs = bp->b_fs;

	assert (bp != NULL);
	offset = bp->b_blkno * fs->sectorsize + fs->offset;
	bytes  = (size_t)bp->b_bcount;
	if (lseek(bp->b_fs->fd, offset, SEEK_SET) == -1)
		return (errno);
	rv = write(bp->b_fs->fd, bp->b_data, bytes);
	brelse(bp, 0);
	if (rv == (ssize_t)bytes)
		return (0);
	else if (rv == -1)		/* write error */
		return (errno);
	else				/* short write ? */
		return (EAGAIN);
}

void
bcleanup(void)
{
#if DEBUG_BUFFERS
	struct mkfsbuf *bp;

	/*
	 * XXX	this really shouldn't be necessary, but i'm curious to
	 *	know why there's still some buffers lying around that
	 *	aren't brelse()d
	 */

	if (TAILQ_EMPTY(&buftail))
		return;

	printf("bcleanup: unflushed buffers:\n");
	TAILQ_FOREACH(bp, &buftail, b_tailq) {
		printf("\tlblkno %10lld  blkno %10lld  count %6ld  bufsize %6ld\n",
		    (long long)bp->b_lblkno, (long long)bp->b_blkno,
		    bp->b_bcount, bp->b_bufsize);
	}
	printf("bcleanup: done\n");
#endif
}

struct mkfsbuf *
getblk(struct mkfsvnode *vp, daddr_t blkno, int size, int u1 __unused,
    int u2 __unused)
{
	static int buftailinitted;
	struct mkfsbuf *bp;
	void *n;

	bp = NULL;
	if (!buftailinitted) {
		TAILQ_INIT(&buftail);
		buftailinitted = 1;
	} else {
		TAILQ_FOREACH(bp, &buftail, b_tailq) {
			if (bp->b_lblkno != blkno)
				continue;
			break;
		}
	}
	if (bp == NULL) {
		bp = ecalloc(1, sizeof(*bp));
		bp->b_bufsize = 0;
		bp->b_blkno = bp->b_lblkno = blkno;
		bp->b_fs = vp->fs;
		bp->b_data = NULL;
		TAILQ_INSERT_HEAD(&buftail, bp, b_tailq);
	}
	bp->b_bcount = size;
	if (bp->b_data == NULL || bp->b_bcount > bp->b_bufsize) {
		n = erealloc(bp->b_data, (size_t)size);
		memset(n, 0, (size_t)size);
		bp->b_data = n;
		bp->b_bufsize = size;
	}

	return (bp);
}
