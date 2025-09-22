/*	$OpenBSD: vfs_bio.c,v 1.216 2025/06/08 06:23:43 rsadowski Exp $	*/
/*	$NetBSD: vfs_bio.c,v 1.44 1996/06/11 11:15:36 pk Exp $	*/

/*
 * Copyright (c) 1994 Christopher G. Demetriou
 * Copyright (c) 1982, 1986, 1989, 1993
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
 *	@(#)vfs_bio.c	8.6 (Berkeley) 1/11/94
 */

/*
 * Some references:
 *	Bach: The Design of the UNIX Operating System (Prentice Hall, 1986)
 *	Leffler, et al.: The Design and Implementation of the 4.3BSD
 *		UNIX Operating System (Addison Welley, 1989)
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/malloc.h>
#include <sys/pool.h>
#include <sys/specdev.h>
#include <sys/tracepoint.h>
#include <uvm/uvm_extern.h>

struct uvm_constraint_range high_constraint;
int fliphigh;

int nobuffers;
int needbuffer;

/* private bufcache functions */
void bufcache_init(void);
void bufcache_adjust(void);
struct buf *bufcache_gethighcleanbuf(void);

/*
 * Buffer pool for I/O buffers.
 */
struct pool bufpool;
struct bufhead bufhead = LIST_HEAD_INITIALIZER(bufhead);
void buf_put(struct buf *);

struct buf *bio_doread(struct vnode *, daddr_t, int, int);
struct buf *buf_get(struct vnode *, daddr_t, size_t);
void bread_cluster_callback(struct buf *);
int64_t bufcache_recover_dmapages(int discard, int64_t howmany);
static struct buf *incore_locked(struct vnode *vp, daddr_t blkno);

struct bcachestats bcstats;  /* counters */
long lodirtypages;      /* dirty page count low water mark */
long hidirtypages;      /* dirty page count high water mark */
long targetpages;   	/* target number of pages for cache size */
long buflowpages;	/* smallest size cache allowed */
long bufhighpages; 	/* largest size cache allowed */
long bufbackpages; 	/* minimum number of pages we shrink when asked to */

vsize_t bufkvm;

struct proc *cleanerproc;
int bd_req;			/* Sleep point for cleaner daemon. */

#define NUM_CACHES 2
#define DMA_CACHE 0
struct bufcache cleancache[NUM_CACHES];
struct bufqueue dirtyqueue;

void
buf_put(struct buf *bp)
{
	splassert(IPL_BIO);

#ifdef DIAGNOSTIC
	if (bp->b_pobj != NULL)
		KASSERT(bp->b_bufsize > 0);
	if (ISSET(bp->b_flags, B_DELWRI))
		panic("buf_put: releasing dirty buffer");
	if (bp->b_freelist.tqe_next != NOLIST &&
	    bp->b_freelist.tqe_next != (void *)-1)
		panic("buf_put: still on the free list");
	if (bp->b_vnbufs.le_next != NOLIST &&
	    bp->b_vnbufs.le_next != (void *)-1)
		panic("buf_put: still on the vnode list");
#endif

	LIST_REMOVE(bp, b_list);
	bcstats.numbufs--;

	if (buf_dealloc_mem(bp) != 0)
		return;
	pool_put(&bufpool, bp);
}

/*
 * Initialize buffers and hash links for buffers.
 */
void
bufinit(void)
{
	u_int64_t dmapages;
	u_int64_t highpages;

	dmapages = uvm_pagecount(&dma_constraint);
	/* take away a guess at how much of this the kernel will consume */
	dmapages -= (atop(physmem) - atop(uvmexp.free));

	/* See if we have memory above the dma accessible region. */
	high_constraint.ucr_low = dma_constraint.ucr_high;
	high_constraint.ucr_high = no_constraint.ucr_high;
	if (high_constraint.ucr_low != high_constraint.ucr_high)
		high_constraint.ucr_low++;
	highpages = uvm_pagecount(&high_constraint);

	/*
	 * Do we have any significant amount of high memory above
	 * the DMA region? if so enable moving buffers there, if not,
	 * don't bother.
	 */
	if (highpages > dmapages / 4)
		fliphigh = 1;
	else
		fliphigh = 0;

	/*
	 * If MD code doesn't say otherwise, use up to 10% of DMA'able
	 * memory for buffers.
	 */
	if (bufcachepercent == 0)
		bufcachepercent = 10;

	/*
	 * XXX these values and their same use in kern_sysctl
	 * need to move into buf.h
	 */
	KASSERT(bufcachepercent <= 90);
	KASSERT(bufcachepercent >= 5);
	if (bufpages == 0)
		bufpages = dmapages * bufcachepercent / 100;
	if (bufpages < BCACHE_MIN)
		bufpages = BCACHE_MIN;
	KASSERT(bufpages < dmapages);

	bufhighpages = bufpages;

	/*
	 * Set the base backoff level for the buffer cache.  We will
	 * not allow uvm to steal back more than this number of pages.
	 */
	buflowpages = dmapages * 5 / 100;
	if (buflowpages < BCACHE_MIN)
		buflowpages = BCACHE_MIN;

	/*
	 * set bufbackpages to 100 pages, or 10 percent of the low water mark
	 * if we don't have that many pages.
	 */

	bufbackpages = buflowpages * 10 / 100;
	if (bufbackpages > 100)
		bufbackpages = 100;

	/*
	 * If the MD code does not say otherwise, reserve 10% of kva
	 * space for mapping buffers.
	 */
	if (bufkvm == 0)
		bufkvm = VM_KERNEL_SPACE_SIZE / 10;

	/*
	 * Don't use more than twice the amount of bufpages for mappings.
	 * It's twice since we map things sparsely.
	 */
	if (bufkvm > bufpages * PAGE_SIZE)
		bufkvm = bufpages * PAGE_SIZE;
	/*
	 * Round bufkvm to MAXPHYS because we allocate chunks of va space
	 * in MAXPHYS chunks.
	 */
	bufkvm &= ~(MAXPHYS - 1);

	pool_init(&bufpool, sizeof(struct buf), 0, IPL_BIO, 0, "bufpl", NULL);

	bufcache_init();

	/*
	 * hmm - bufkvm is an argument because it's static, while
	 * bufpages is global because it can change while running.
 	 */
	buf_mem_init(bufkvm);

	/*
	 * Set the dirty page high water mark to be less than the low
	 * water mark for pages in the buffer cache. This ensures we
	 * can always back off by throwing away clean pages, and give
	 * ourselves a chance to write out the dirty pages eventually.
	 */
	hidirtypages = (buflowpages / 4) * 3;
	lodirtypages = buflowpages / 2;

	/*
	 * We are allowed to use up to the reserve.
	 */
	targetpages = bufpages - RESERVE_PAGES;
}

/*
 * Change cachepct
 */
void
bufadjust(int newbufpages)
{
	int s;
	int64_t npages;

	if (newbufpages < buflowpages)
		newbufpages = buflowpages;

	s = splbio();
	bufpages = newbufpages;

	/*
	 * We are allowed to use up to the reserve
	 */
	targetpages = bufpages - RESERVE_PAGES;

	npages = bcstats.dmapages - targetpages;

	/*
	 * Shrinking the cache happens here only if someone has manually
	 * adjusted bufcachepercent - or the pagedaemon has told us
	 * to give back memory *now* - so we give it all back.
	 */
	if (bcstats.dmapages > targetpages)
		(void) bufcache_recover_dmapages(0, bcstats.dmapages - targetpages);
	bufcache_adjust();

	/*
	 * Wake up the cleaner if we have lots of dirty pages,
	 * or if we are getting low on buffer cache kva.
	 */
	if ((UNCLEAN_PAGES >= hidirtypages) ||
	    bcstats.kvaslots_avail <= 2 * RESERVE_SLOTS)
		wakeup(&bd_req);

	splx(s);
}

/*
 * Back off "size" buffer cache pages. Called by the page
 * daemon to consume buffer cache pages rather than scanning.
 *
 * It returns the number of freed pages.
 */
unsigned long
bufbackoff(struct uvm_constraint_range *range, long size)
{
	long pdelta, oldbufpages;
	int64_t dmarecovered, recovered = 0;

	/*
	 * If we will accept high memory for this backoff
	 * try to steal it from the high memory buffer cache.
	 */
	if (range != NULL && range->ucr_high > dma_constraint.ucr_high) {
		struct buf *bp;
		int64_t start;
		int s;

		start = bcstats.numbufpages;

		s = splbio();
		while (recovered < size && (bp = bufcache_gethighcleanbuf())) {
			bufcache_take(bp);
			if (bp->b_vp) {
				RBT_REMOVE(buf_rb_bufs,
				    &bp->b_vp->v_bufs_tree, bp);
				brelvp(bp);
			}
			buf_put(bp);
			recovered = start - bcstats.numbufpages;
		}
		bufcache_adjust();
		splx(s);

		/* We got enough. */
		if (recovered >= size)
			return recovered;

		/* If we needed only memory above DMA, we're done. */
		if (range->ucr_low > dma_constraint.ucr_high)
			return recovered;

		/* Otherwise get the rest from DMA */
		size -= recovered;
	}

	/*
	 * XXX Otherwise do the dma memory cache dance. this needs
	 * refactoring later to get rid of 'bufpages'
	 */

	/*
	 * Back off by at least bufbackpages. If the page daemon gave us
	 * a larger size, back off by that much.
	 */
	pdelta = (size > bufbackpages) ? size : bufbackpages;

	if (bufpages <= buflowpages)
		return recovered;
	if (bufpages - pdelta < buflowpages)
		pdelta = bufpages - buflowpages;
	oldbufpages = bufpages;
	bufadjust(bufpages - pdelta);
	dmarecovered = oldbufpages - bufpages;

	return recovered + dmarecovered;
}


/*
 * Opportunistically flip a buffer into high memory. Will move the buffer
 * if memory is available without sleeping, and return 0, otherwise will
 * fail and return -1 with the buffer unchanged.
 */

int
buf_flip_high(struct buf *bp)
{
	int ret = -1;

	KASSERT(ISSET(bp->b_flags, B_BC));
	KASSERT(ISSET(bp->b_flags, B_DMA));
	KASSERT(bp->cache == DMA_CACHE);
	KASSERT(fliphigh);

	splassert(IPL_BIO);

	/* Attempt to move the buffer to high memory if we can */
	if (buf_realloc_pages(bp, &high_constraint, UVM_PLA_NOWAIT) == 0) {
		KASSERT(!ISSET(bp->b_flags, B_DMA));
		bcstats.highflips++;
		ret = 0;
	} else
		bcstats.highflops++;

	return ret;
}

/*
 * Flip a buffer to dma reachable memory, when we need it there for
 * I/O. This can sleep since it will wait for memory allocation in the
 * DMA reachable area since we have to have the buffer there to proceed.
 */
void
buf_flip_dma(struct buf *bp)
{
	KASSERT(ISSET(bp->b_flags, B_BC));
	KASSERT(ISSET(bp->b_flags, B_BUSY));
	KASSERT(bp->cache < NUM_CACHES);

	splassert(IPL_BIO);

	if (!ISSET(bp->b_flags, B_DMA)) {

		/* move buf to dma reachable memory */
		(void) buf_realloc_pages(bp, &dma_constraint, UVM_PLA_WAITOK);
		KASSERT(ISSET(bp->b_flags, B_DMA));
		bcstats.dmaflips++;
	}

	if (bp->cache > DMA_CACHE) {
		CLR(bp->b_flags, B_COLD);
		CLR(bp->b_flags, B_WARM);
		bp->cache = DMA_CACHE;
	}
}

struct buf *
bio_doread(struct vnode *vp, daddr_t blkno, int size, int async)
{
	struct buf *bp;
	struct mount *mp;

	bp = getblk(vp, blkno, size, 0, INFSLP);

	/*
	 * If buffer does not have valid data, start a read.
	 * Note that if buffer is B_INVAL, getblk() won't return it.
	 * Therefore, it's valid if its I/O has completed or been delayed.
	 */
	if (!ISSET(bp->b_flags, (B_DONE | B_DELWRI))) {
		SET(bp->b_flags, B_READ | async);
		bcstats.pendingreads++;
		bcstats.numreads++;
		VOP_STRATEGY(bp->b_vp, bp);
		/* Pay for the read. */
		curproc->p_ru.ru_inblock++;			/* XXX */
	} else if (async) {
		brelse(bp);
	}

	mp = vp->v_type == VBLK ? vp->v_specmountpoint : vp->v_mount;

	/*
	 * Collect statistics on synchronous and asynchronous reads.
	 * Reads from block devices are charged to their associated
	 * filesystem (if any).
	 */
	if (mp != NULL) {
		if (async == 0)
			mp->mnt_stat.f_syncreads++;
		else
			mp->mnt_stat.f_asyncreads++;
	}

	return (bp);
}

/*
 * Read a disk block.
 * This algorithm described in Bach (p.54).
 */
int
bread(struct vnode *vp, daddr_t blkno, int size, struct buf **bpp)
{
	struct buf *bp;

	/* Get buffer for block. */
	bp = *bpp = bio_doread(vp, blkno, size, 0);

	/* Wait for the read to complete, and return result. */
	return (biowait(bp));
}

/*
 * Read-ahead multiple disk blocks. The first is sync, the rest async.
 * Trivial modification to the breada algorithm presented in Bach (p.55).
 */
int
breadn(struct vnode *vp, daddr_t blkno, int size, daddr_t rablks[],
    int rasizes[], int nrablks, struct buf **bpp)
{
	struct buf *bp;
	int i;

	bp = *bpp = bio_doread(vp, blkno, size, 0);

	/*
	 * For each of the read-ahead blocks, start a read, if necessary.
	 */
	for (i = 0; i < nrablks; i++) {
		/* If it's in the cache, just go on to next one. */
		if (incore(vp, rablks[i]))
			continue;

		/* Get a buffer for the read-ahead block */
		(void) bio_doread(vp, rablks[i], rasizes[i], B_ASYNC);
	}

	/* Otherwise, we had to start a read for it; wait until it's valid. */
	return (biowait(bp));
}

/*
 * Called from interrupt context.
 */
void
bread_cluster_callback(struct buf *bp)
{
	struct buf **xbpp = bp->b_saveaddr;
	int i;

	if (xbpp[1] != NULL) {
		size_t newsize = xbpp[1]->b_bufsize;

		/*
		 * Shrink this buffer's mapping to only cover its part of
		 * the total I/O.
		 */
		buf_fix_mapping(bp, newsize);
		bp->b_bcount = newsize;
	}

	/* Invalidate read-ahead buffers if read short */
	if (bp->b_resid > 0) {
		for (i = 1; xbpp[i] != NULL; i++)
			continue;
		for (i = i - 1; i != 0; i--) {
			if (xbpp[i]->b_bufsize <= bp->b_resid) {
				bp->b_resid -= xbpp[i]->b_bufsize;
				SET(xbpp[i]->b_flags, B_INVAL);
			} else if (bp->b_resid > 0) {
				bp->b_resid = 0;
				SET(xbpp[i]->b_flags, B_INVAL);
			} else
				break;
		}
	}

	for (i = 1; xbpp[i] != NULL; i++) {
		if (ISSET(bp->b_flags, B_ERROR))
			SET(xbpp[i]->b_flags, B_INVAL | B_ERROR);
		/*
		 * Move the pages from the master buffer's uvm object
		 * into the individual buffer's uvm objects.
		 */
		struct uvm_object *newobj = &xbpp[i]->b_uobj;
		struct uvm_object *oldobj = &bp->b_uobj;
		int page;

		uvm_obj_init(newobj, &bufcache_pager, 1);
		for (page = 0; page < atop(xbpp[i]->b_bufsize); page++) {
			struct vm_page *pg = uvm_pagelookup(oldobj,
			    xbpp[i]->b_poffs + ptoa(page));
			KASSERT(pg != NULL);
			KASSERT(pg->wire_count == 1);
			uvm_pagerealloc(pg, newobj, xbpp[i]->b_poffs + ptoa(page));
		}
		xbpp[i]->b_pobj = newobj;

		biodone(xbpp[i]);
	}

	free(xbpp, M_TEMP, (i + 1) * sizeof(*xbpp));

	if (ISSET(bp->b_flags, B_ASYNC)) {
		brelse(bp);
	} else {
		CLR(bp->b_flags, B_WANTED);
		wakeup(bp);
	}
}

/*
 * Read-ahead multiple disk blocks, but make sure only one (big) I/O
 * request is sent to the disk.
 * XXX This should probably be dropped and breadn should instead be optimized
 * XXX to do fewer I/O requests.
 */
int
bread_cluster(struct vnode *vp, daddr_t blkno, int size, struct buf **rbpp)
{
	struct buf *bp, **xbpp;
	int howmany, maxra, i, inc;
	daddr_t sblkno;

	*rbpp = bio_doread(vp, blkno, size, 0);

	/*
	 * If the buffer is in the cache skip any I/O operation.
	 */
	if (ISSET((*rbpp)->b_flags, B_CACHE))
		goto out;

	if (size != round_page(size))
		goto out;

	if (VOP_BMAP(vp, blkno + 1, NULL, &sblkno, &maxra))
		goto out;

	maxra++;
	if (sblkno == -1 || maxra < 2)
		goto out;

	howmany = MAXPHYS / size;
	if (howmany > maxra)
		howmany = maxra;

	xbpp = mallocarray(howmany + 1, sizeof(*xbpp), M_TEMP, M_NOWAIT);
	if (xbpp == NULL)
		goto out;

	for (i = howmany - 1; i >= 0; i--) {
		size_t sz;

		/*
		 * First buffer allocates big enough size to cover what
		 * all the other buffers need.
		 */
		sz = i == 0 ? howmany * size : 0;

		xbpp[i] = buf_get(vp, blkno + i + 1, sz);
		if (xbpp[i] == NULL) {
			for (++i; i < howmany; i++) {
				SET(xbpp[i]->b_flags, B_INVAL);
				brelse(xbpp[i]);
			}
			free(xbpp, M_TEMP, (howmany + 1) * sizeof(*xbpp));
			goto out;
		}
	}

	bp = xbpp[0];

	xbpp[howmany] = NULL;

	inc = btodb(size);

	for (i = 1; i < howmany; i++) {
		bcstats.pendingreads++;
		bcstats.numreads++;
                /*
                * We set B_DMA here because bp above will be B_DMA,
                * and we are playing buffer slice-n-dice games from
                * the memory allocated in bp.
                */
		SET(xbpp[i]->b_flags, B_DMA | B_READ | B_ASYNC);
		xbpp[i]->b_blkno = sblkno + (i * inc);
		xbpp[i]->b_bufsize = xbpp[i]->b_bcount = size;
		xbpp[i]->b_data = NULL;
		xbpp[i]->b_pobj = bp->b_pobj;
		xbpp[i]->b_poffs = bp->b_poffs + (i * size);
	}

	KASSERT(bp->b_lblkno == blkno + 1);
	KASSERT(bp->b_vp == vp);

	bp->b_blkno = sblkno;
	SET(bp->b_flags, B_READ | B_ASYNC | B_CALL);

	bp->b_saveaddr = (void *)xbpp;
	bp->b_iodone = bread_cluster_callback;

	bcstats.pendingreads++;
	bcstats.numreads++;
	VOP_STRATEGY(bp->b_vp, bp);
	curproc->p_ru.ru_inblock++;

out:
	return (biowait(*rbpp));
}

/*
 * Block write.  Described in Bach (p.56)
 */
int
bwrite(struct buf *bp)
{
	int rv, async, wasdelayed, s;
	struct vnode *vp;
	struct mount *mp;

	vp = bp->b_vp;
	if (vp != NULL)
		mp = vp->v_type == VBLK? vp->v_specmountpoint : vp->v_mount;
	else
		mp = NULL;

	/*
	 * Remember buffer type, to switch on it later.  If the write was
	 * synchronous, but the file system was mounted with MNT_ASYNC,
	 * convert it to a delayed write.
	 * XXX note that this relies on delayed tape writes being converted
	 * to async, not sync writes (which is safe, but ugly).
	 */
	async = ISSET(bp->b_flags, B_ASYNC);
	if (!async && mp && ISSET(mp->mnt_flag, MNT_ASYNC)) {
		/*
		 * Don't convert writes from VND on async filesystems
		 * that already have delayed writes in the upper layer.
		 */
		if (!ISSET(bp->b_flags, B_NOCACHE)) {
			bdwrite(bp);
			return (0);
		}
	}

	/*
	 * Collect statistics on synchronous and asynchronous writes.
	 * Writes to block devices are charged to their associated
	 * filesystem (if any).
	 */
	if (mp != NULL) {
		if (async)
			mp->mnt_stat.f_asyncwrites++;
		else
			mp->mnt_stat.f_syncwrites++;
	}
	bcstats.pendingwrites++;
	bcstats.numwrites++;

	wasdelayed = ISSET(bp->b_flags, B_DELWRI);
	CLR(bp->b_flags, (B_READ | B_DONE | B_ERROR | B_DELWRI));

	s = splbio();

	/*
	 * If not synchronous, pay for the I/O operation and make
	 * sure the buf is on the correct vnode queue.  We have
	 * to do this now, because if we don't, the vnode may not
	 * be properly notified that its I/O has completed.
	 */
	if (wasdelayed) {
		reassignbuf(bp);
	} else
		curproc->p_ru.ru_oublock++;


	/* Initiate disk write.  Make sure the appropriate party is charged. */
	bp->b_vp->v_numoutput++;
	buf_flip_dma(bp);
	SET(bp->b_flags, B_WRITEINPROG);
	splx(s);
	VOP_STRATEGY(bp->b_vp, bp);

	/*
	 * If the queue is above the high water mark, wait till
	 * the number of outstanding write bufs drops below the low
	 * water mark.
	 */
	if (bp->b_bq)
		bufq_wait(bp->b_bq);

	if (async)
		return (0);

	/*
	 * If I/O was synchronous, wait for it to complete.
	 */
	rv = biowait(bp);

	/* Release the buffer. */
	brelse(bp);

	return (rv);
}


/*
 * Delayed write.
 *
 * The buffer is marked dirty, but is not queued for I/O.
 * This routine should be used when the buffer is expected
 * to be modified again soon, typically a small write that
 * partially fills a buffer.
 *
 * NB: magnetic tapes cannot be delayed; they must be
 * written in the order that the writes are requested.
 *
 * Described in Leffler, et al. (pp. 208-213).
 */
void
bdwrite(struct buf *bp)
{
	int s;

	/*
	 * If the block hasn't been seen before:
	 *	(1) Mark it as having been seen,
	 *	(2) Charge for the write.
	 *	(3) Make sure it's on its vnode's correct block list,
	 *	(4) If a buffer is rewritten, move it to end of dirty list
	 */
	if (!ISSET(bp->b_flags, B_DELWRI)) {
		SET(bp->b_flags, B_DELWRI);
		s = splbio();
		buf_flip_dma(bp);
		reassignbuf(bp);
		splx(s);
		curproc->p_ru.ru_oublock++;		/* XXX */
	}

	/* The "write" is done, so mark and release the buffer. */
	CLR(bp->b_flags, B_NEEDCOMMIT);
	CLR(bp->b_flags, B_NOCACHE); /* Must cache delayed writes */
	SET(bp->b_flags, B_DONE);
	brelse(bp);
}

/*
 * Asynchronous block write; just an asynchronous bwrite().
 */
void
bawrite(struct buf *bp)
{

	SET(bp->b_flags, B_ASYNC);
	VOP_BWRITE(bp);
}

/*
 * Must be called at splbio()
 */
void
buf_dirty(struct buf *bp)
{
	splassert(IPL_BIO);

#ifdef DIAGNOSTIC
	if (!ISSET(bp->b_flags, B_BUSY))
		panic("Trying to dirty buffer on freelist!");
#endif

	if (ISSET(bp->b_flags, B_DELWRI) == 0) {
		SET(bp->b_flags, B_DELWRI);
		buf_flip_dma(bp);
		reassignbuf(bp);
	}
}

/*
 * Must be called at splbio()
 */
void
buf_undirty(struct buf *bp)
{
	splassert(IPL_BIO);

#ifdef DIAGNOSTIC
	if (!ISSET(bp->b_flags, B_BUSY))
		panic("Trying to undirty buffer on freelist!");
#endif
	if (ISSET(bp->b_flags, B_DELWRI)) {
		CLR(bp->b_flags, B_DELWRI);
		reassignbuf(bp);
	}
}

/*
 * Release a buffer on to the free lists.
 * Described in Bach (p. 46).
 */
void
brelse(struct buf *bp)
{
	int s;

	s = splbio();

	if (bp->b_data != NULL)
		KASSERT(bp->b_bufsize > 0);

	/*
	 * Determine which queue the buffer should be on, then put it there.
	 */

	/* If it's not cacheable, or an error, mark it invalid. */
	if (ISSET(bp->b_flags, (B_NOCACHE|B_ERROR)))
		SET(bp->b_flags, B_INVAL);
	/* If it's a write error, also mark the vnode as damaged. */
	if (ISSET(bp->b_flags, B_ERROR) && !ISSET(bp->b_flags, B_READ)) {
		if (bp->b_vp && bp->b_vp->v_type == VREG)
			SET(bp->b_vp->v_bioflag, VBIOERROR);
	}

	if (ISSET(bp->b_flags, B_INVAL)) {
		/*
		 * If the buffer is invalid, free it now rather than leaving
		 * it in a queue and wasting memory.
		 */
		if (ISSET(bp->b_flags, B_DELWRI)) {
			CLR(bp->b_flags, B_DELWRI);
		}

		if (bp->b_vp) {
			RBT_REMOVE(buf_rb_bufs, &bp->b_vp->v_bufs_tree, bp);
			brelvp(bp);
		}
		bp->b_vp = NULL;

		/*
		 * Wake up any processes waiting for _this_ buffer to
		 * become free. They are not allowed to grab it
		 * since it will be freed. But the only sleeper is
		 * getblk and it will restart the operation after
		 * sleep.
		 */
		if (ISSET(bp->b_flags, B_WANTED)) {
			CLR(bp->b_flags, B_WANTED);
			wakeup(bp);
		}
		buf_put(bp);
	} else {
		/*
		 * It has valid data.  Put it on the end of the appropriate
		 * queue, so that it'll stick around for as long as possible.
		 */
		bufcache_release(bp);

		/* Unlock the buffer. */
		CLR(bp->b_flags, (B_AGE | B_ASYNC | B_NOCACHE | B_DEFERRED));
		buf_release(bp);

		/* Wake up any processes waiting for _this_ buffer to
		 * become free. */
		if (ISSET(bp->b_flags, B_WANTED)) {
			CLR(bp->b_flags, B_WANTED);
			wakeup(bp);
		}

		if (bcstats.dmapages > targetpages)
			(void) bufcache_recover_dmapages(0,
			    bcstats.dmapages - targetpages);
		bufcache_adjust();
	}

	/* Wake up syncer and cleaner processes waiting for buffers. */
	if (nobuffers) {
		nobuffers = 0;
		wakeup(&nobuffers);
	}

	/* Wake up any processes waiting for any buffer to become free. */
	if (needbuffer && bcstats.dmapages < targetpages &&
	    bcstats.kvaslots_avail > RESERVE_SLOTS) {
		needbuffer = 0;
		wakeup(&needbuffer);
	}

	splx(s);
}

/*
 * Determine if a block is in the cache. Just look on what would be its hash
 * chain. If it's there, return a pointer to it, unless it's marked invalid.
 */
static struct buf *
incore_locked(struct vnode *vp, daddr_t blkno)
{
	struct buf *bp;
	struct buf b;

	splassert(IPL_BIO);

	/* Search buf lookup tree */
	b.b_lblkno = blkno;
	bp = RBT_FIND(buf_rb_bufs, &vp->v_bufs_tree, &b);
	if (bp != NULL && ISSET(bp->b_flags, B_INVAL))
		bp = NULL;

	return (bp);
}
struct buf *
incore(struct vnode *vp, daddr_t blkno)
{
	struct buf *bp;
	int s;

	s = splbio();
	bp = incore_locked(vp, blkno);
	splx(s);

	return (bp);
}

/*
 * Get a block of requested size that is associated with
 * a given vnode and block offset. If it is found in the
 * block cache, mark it as having been found, make it busy
 * and return it. Otherwise, return an empty block of the
 * correct size. It is up to the caller to ensure that the
 * cached blocks be of the correct size.
 */
struct buf *
getblk(struct vnode *vp, daddr_t blkno, int size, int slpflag,
    uint64_t slptimeo)
{
	struct buf *bp;
	struct buf b;
	int s, error;

	/*
	 * XXX
	 * The following is an inlined version of 'incore()', but with
	 * the 'invalid' test moved to after the 'busy' test.  It's
	 * necessary because there are some cases in which the NFS
	 * code sets B_INVAL prior to writing data to the server, but
	 * in which the buffers actually contain valid data.  In this
	 * case, we can't allow the system to allocate a new buffer for
	 * the block until the write is finished.
	 */
start:
	s = splbio();
	b.b_lblkno = blkno;
	bp = RBT_FIND(buf_rb_bufs, &vp->v_bufs_tree, &b);
	if (bp != NULL) {
		if (ISSET(bp->b_flags, B_BUSY)) {
			SET(bp->b_flags, B_WANTED);
			error = tsleep_nsec(bp, slpflag | (PRIBIO + 1),
			    "getblk", slptimeo);
			splx(s);
			if (error)
				return (NULL);
			goto start;
		}

		if (!ISSET(bp->b_flags, B_INVAL)) {
			bcstats.cachehits++;
			SET(bp->b_flags, B_CACHE);
			bufcache_take(bp);
			buf_acquire(bp);
			splx(s);
			return (bp);
		}
	}
	splx(s);

	if ((bp = buf_get(vp, blkno, size)) == NULL)
		goto start;

	return (bp);
}

/*
 * Get an empty, disassociated buffer of given size.
 */
struct buf *
geteblk(size_t size)
{
	struct buf *bp;

	while ((bp = buf_get(NULL, 0, size)) == NULL)
		continue;

	return (bp);
}

/*
 * Allocate a buffer.
 * If vp is given, put it into the buffer cache for that vnode.
 * If size != 0, allocate memory and call buf_map().
 * If there is already a buffer for the given vnode/blkno, return NULL.
 */
struct buf *
buf_get(struct vnode *vp, daddr_t blkno, size_t size)
{
	struct buf *bp;
	int poolwait = size == 0 ? PR_NOWAIT : PR_WAITOK;
	int npages;
	int s;

	s = splbio();
	if (size) {
		/*
		 * Wake up the cleaner if we have lots of dirty pages,
		 * or if we are getting low on buffer cache kva.
		 */
		if (UNCLEAN_PAGES >= hidirtypages ||
			bcstats.kvaslots_avail <= 2 * RESERVE_SLOTS)
			wakeup(&bd_req);

		npages = atop(round_page(size));

		/*
		 * if our cache has been previously shrunk,
		 * allow it to grow again with use up to
		 * bufhighpages (cachepercent)
		 */
		if (bufpages < bufhighpages)
			bufadjust(bufhighpages);

		/*
		 * If we would go over the page target with our
		 * new allocation, free enough buffers first
		 * to stay at the target with our new allocation.
		 */
		if (bcstats.dmapages + npages > targetpages) {
			(void) bufcache_recover_dmapages(0, npages);
			bufcache_adjust();
		}

		/*
		 * If we get here, we tried to free the world down
		 * above, and couldn't get down - Wake the cleaner
		 * and wait for it to push some buffers out.
		 */
		if ((bcstats.dmapages + npages > targetpages ||
		    bcstats.kvaslots_avail <= RESERVE_SLOTS) &&
		    curproc != syncerproc && curproc != cleanerproc) {
			wakeup(&bd_req);
			needbuffer++;
			tsleep_nsec(&needbuffer, PRIBIO, "needbuffer", INFSLP);
			splx(s);
			return (NULL);
		}
		if (bcstats.dmapages + npages > bufpages) {
			/* cleaner or syncer */
			nobuffers = 1;
			tsleep_nsec(&nobuffers, PRIBIO, "nobuffers", INFSLP);
			splx(s);
			return (NULL);
		}
	}

	bp = pool_get(&bufpool, poolwait|PR_ZERO);

	if (bp == NULL) {
		splx(s);
		return (NULL);
	}

	bp->b_freelist.tqe_next = NOLIST;
	bp->b_dev = NODEV;
	bp->b_bcount = size;

	buf_acquire_nomap(bp);

	if (vp != NULL) {
		/*
		 * We insert the buffer into the hash with B_BUSY set
		 * while we allocate pages for it. This way any getblk
		 * that happens while we allocate pages will wait for
		 * this buffer instead of starting its own buf_get.
		 *
		 * But first, we check if someone beat us to it.
		 */
		if (incore_locked(vp, blkno)) {
			pool_put(&bufpool, bp);
			splx(s);
			return (NULL);
		}

		bp->b_blkno = bp->b_lblkno = blkno;
		bgetvp(vp, bp);
		if (RBT_INSERT(buf_rb_bufs, &vp->v_bufs_tree, bp))
			panic("buf_get: dup lblk vp %p bp %p", vp, bp);
	} else {
		bp->b_vnbufs.le_next = NOLIST;
		SET(bp->b_flags, B_INVAL);
		bp->b_vp = NULL;
	}

	LIST_INSERT_HEAD(&bufhead, bp, b_list);
	bcstats.numbufs++;

	if (size) {
		buf_alloc_pages(bp, round_page(size));
		KASSERT(ISSET(bp->b_flags, B_DMA));
		buf_map(bp);
	}

	SET(bp->b_flags, B_BC);
	splx(s);

	return (bp);
}

/*
 * Buffer cleaning daemon.
 */
void
buf_daemon(void *arg)
{
	struct buf *bp = NULL;
	int s, pushed = 0;

	s = splbio();
	for (;;) {
		if (bp == NULL || (pushed >= 16 &&
		    UNCLEAN_PAGES < hidirtypages &&
		    bcstats.kvaslots_avail > 2 * RESERVE_SLOTS)){
			pushed = 0;
			/*
			 * Wake up anyone who was waiting for buffers
			 * to be released.
			 */
			if (needbuffer) {
				needbuffer = 0;
				wakeup(&needbuffer);
			}
			tsleep_nsec(&bd_req, PRIBIO - 7, "cleaner", INFSLP);
		}

		while ((bp = bufcache_getdirtybuf())) {
			TRACEPOINT(vfs, cleaner, bp->b_flags, pushed,
			    lodirtypages, hidirtypages);

			if (UNCLEAN_PAGES < lodirtypages &&
			    bcstats.kvaslots_avail > 2 * RESERVE_SLOTS &&
			    pushed >= 16)
				break;

			bufcache_take(bp);
			buf_acquire(bp);
			splx(s);

			if (ISSET(bp->b_flags, B_INVAL)) {
				brelse(bp);
				s = splbio();
				continue;
			}
#ifdef DIAGNOSTIC
			if (!ISSET(bp->b_flags, B_DELWRI))
				panic("Clean buffer on dirty queue");
#endif
			bawrite(bp);
			pushed++;

			sched_pause(yield);

			s = splbio();
		}
	}
}

/*
 * Wait for operations on the buffer to complete.
 * When they do, extract and return the I/O's error value.
 */
int
biowait(struct buf *bp)
{
	int s;

	KASSERT(!(bp->b_flags & B_ASYNC));

	s = splbio();
	while (!ISSET(bp->b_flags, B_DONE))
		tsleep_nsec(bp, PRIBIO + 1, "biowait", INFSLP);
	splx(s);

	/* check for interruption of I/O (e.g. via NFS), then errors. */
	if (ISSET(bp->b_flags, B_EINTR)) {
		CLR(bp->b_flags, B_EINTR);
		return (EINTR);
	}

	if (ISSET(bp->b_flags, B_ERROR))
		return (bp->b_error ? bp->b_error : EIO);
	else
		return (0);
}

/*
 * Mark I/O complete on a buffer.
 *
 * If a callback has been requested, e.g. the pageout
 * daemon, do so. Otherwise, awaken waiting processes.
 *
 * [ Leffler, et al., says on p.247:
 *	"This routine wakes up the blocked process, frees the buffer
 *	for an asynchronous write, or, for a request by the pagedaemon
 *	process, invokes a procedure specified in the buffer structure" ]
 *
 * In real life, the pagedaemon (or other system processes) wants
 * to do async stuff to, and doesn't want the buffer brelse()'d.
 * (for swap pager, that puts swap buffers on the free lists (!!!),
 * for the vn device, that puts malloc'd buffers on the free lists!)
 *
 * Must be called at splbio().
 */
void
biodone(struct buf *bp)
{
	splassert(IPL_BIO);

	if (ISSET(bp->b_flags, B_DONE))
		panic("biodone already");
	SET(bp->b_flags, B_DONE);		/* note that it's done */

	if (bp->b_bq)
		bufq_done(bp->b_bq, bp);

	if (!ISSET(bp->b_flags, B_READ)) {
		CLR(bp->b_flags, B_WRITEINPROG);
		vwakeup(bp->b_vp);
	}
	if (bcstats.numbufs &&
	    (!(ISSET(bp->b_flags, B_RAW) || ISSET(bp->b_flags, B_PHYS)))) {
		if (!ISSET(bp->b_flags, B_READ)) {
			bcstats.pendingwrites--;
		} else
			bcstats.pendingreads--;
	}
	if (ISSET(bp->b_flags, B_CALL)) {	/* if necessary, call out */
		CLR(bp->b_flags, B_CALL);	/* but note callout done */
		(*bp->b_iodone)(bp);
	} else {
		if (ISSET(bp->b_flags, B_ASYNC)) {/* if async, release it */
			brelse(bp);
		} else {			/* or just wakeup the buffer */
			CLR(bp->b_flags, B_WANTED);
			wakeup(bp);
		}
	}
}

#ifdef DDB
void	bcstats_print(int (*)(const char *, ...)
    __attribute__((__format__(__kprintf__,1,2))));
/*
 * bcstats_print: ddb hook to print interesting buffer cache counters
 */
void
bcstats_print(
    int (*pr)(const char *, ...) __attribute__((__format__(__kprintf__,1,2))))
{
	(*pr)("Current Buffer Cache status:\n");
	(*pr)("numbufs %lld busymapped %lld, delwri %lld\n",
	    bcstats.numbufs, bcstats.busymapped, bcstats.delwribufs);
	(*pr)("kvaslots %lld avail kva slots %lld\n",
	    bcstats.kvaslots, bcstats.kvaslots_avail);
    	(*pr)("bufpages %lld, dmapages %lld, dirtypages %lld\n",
	    bcstats.numbufpages, bcstats.dmapages, bcstats.numdirtypages);
	(*pr)("pendingreads %lld, pendingwrites %lld\n",
	    bcstats.pendingreads, bcstats.pendingwrites);
	(*pr)("highflips %lld, highflops %lld, dmaflips %lld\n",
	    bcstats.highflips, bcstats.highflops, bcstats.dmaflips);
}
#endif

void
buf_adjcnt(struct buf *bp, long ncount)
{
	KASSERT(ncount <= bp->b_bufsize);
	bp->b_bcount = ncount;
}

/* bufcache freelist code below */
/*
 * Copyright (c) 2014 Ted Unangst <tedu@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * The code below implements a variant of the 2Q buffer cache algorithm by
 * Johnson and Shasha.
 *
 * General Outline
 * We divide the buffer cache into three working sets: current, previous,
 * and long term. Each list is itself LRU and buffers get promoted and moved
 * around between them. A buffer starts its life in the current working set.
 * As time passes and newer buffers push it out, it will turn into the previous
 * working set and is subject to recycling. But if it's accessed again from
 * the previous working set, that's an indication that it's actually in the
 * long term working set, so we promote it there. The separation of current
 * and previous working sets prevents us from promoting a buffer that's only
 * temporarily hot to the long term cache.
 *
 * The objective is to provide scan resistance by making the long term
 * working set ineligible for immediate recycling, even as the current
 * working set is rapidly turned over.
 *
 * Implementation
 * The code below identifies the current, previous, and long term sets as
 * hotqueue, coldqueue, and warmqueue. The hot and warm queues are capped at
 * 1/3 of the total clean pages, after which point they start pushing their
 * oldest buffers into coldqueue.
 * A buf always starts out with neither WARM or COLD flags set (implying HOT).
 * When released, it will be returned to the tail of the hotqueue list.
 * When the hotqueue gets too large, the oldest hot buf will be moved to the
 * coldqueue, with the B_COLD flag set. When a cold buf is released, we set
 * the B_WARM flag and put it onto the warmqueue. Warm bufs are also
 * directly returned to the end of the warmqueue. As with the hotqueue, when
 * the warmqueue grows too large, B_WARM bufs are moved onto the coldqueue.
 *
 * Note that this design does still support large working sets, greater
 * than the cap of hotqueue or warmqueue would imply. The coldqueue is still
 * cached and has no maximum length. The hot and warm queues form a Y feeding
 * into the coldqueue. Moving bufs between queues is constant time, so this
 * design decays to one long warm->cold queue.
 *
 * In the 2Q paper, hotqueue and coldqueue are A1in and A1out. The warmqueue
 * is Am. We always cache pages, as opposed to pointers to pages for A1.
 *
 * This implementation adds support for multiple 2q caches.
 *
 * If we have more than one 2q cache, as bufs fall off the cold queue
 * for recycling, bufs that have been warm before (which retain the
 * B_WARM flag in addition to B_COLD) can be put into the hot queue of
 * a second level 2Q cache. buffers which are only B_COLD are
 * recycled. Bufs falling off the last cache's cold queue are always
 * recycled.
 *
 */

/*
 * this function is called when a hot or warm queue may have exceeded its
 * size limit. it will move a buf to the coldqueue.
 */
int chillbufs(struct
    bufcache *cache, struct bufqueue *queue, int64_t *queuepages);

void
bufcache_init(void)
{
	int i;

	for (i = 0; i < NUM_CACHES; i++) {
		TAILQ_INIT(&cleancache[i].hotqueue);
		TAILQ_INIT(&cleancache[i].coldqueue);
		TAILQ_INIT(&cleancache[i].warmqueue);
	}
	TAILQ_INIT(&dirtyqueue);
}

/*
 * if the buffer caches have shrunk, we may need to rebalance our queues.
 */
void
bufcache_adjust(void)
{
	int i;

	for (i = 0; i < NUM_CACHES; i++) {
		while (chillbufs(&cleancache[i], &cleancache[i].warmqueue,
		    &cleancache[i].warmbufpages) ||
		    chillbufs(&cleancache[i], &cleancache[i].hotqueue,
		    &cleancache[i].hotbufpages))
			continue;
	}
}

/*
 * Get a clean buffer from the cache. if "discard" is set do not promote
 * previously warm buffers as normal, because we are tossing everything
 * away such as in a hibernation
 */
struct buf *
bufcache_getcleanbuf(int cachenum, int discard)
{
	struct buf *bp = NULL;
	struct bufcache *cache = &cleancache[cachenum];
	struct bufqueue * queue;

	splassert(IPL_BIO);

	/* try cold queue */
	while ((bp = TAILQ_FIRST(&cache->coldqueue)) ||
	    (bp = TAILQ_FIRST(&cache->warmqueue)) ||
	    (bp = TAILQ_FIRST(&cache->hotqueue))) {
		int64_t pages = atop(bp->b_bufsize);
		struct bufcache *newcache;

		if (discard || cachenum >= NUM_CACHES - 1) {
			/* Victim selected, give it up */
			return bp;
		}
		KASSERT(bp->cache == cachenum);

		/*
		 * If this buffer was warm before, move it to
		 * the hot queue in the next cache
		 */

		if (fliphigh) {
			/*
			 * If we are in the DMA cache, try to flip the
			 * buffer up high to move it on to the other
			 * caches. if we can't move the buffer to high
			 * memory without sleeping, we give it up and
			 * return it rather than fight for more memory
			 * against non buffer cache competitors.
			 */
			SET(bp->b_flags, B_BUSY);
			if (bp->cache == 0 && buf_flip_high(bp) == -1) {
				CLR(bp->b_flags, B_BUSY);
				return bp;
			}
			CLR(bp->b_flags, B_BUSY);
		}

		/* Move the buffer to the hot queue in the next cache */
		if (ISSET(bp->b_flags, B_COLD)) {
			queue = &cache->coldqueue;
		} else if (ISSET(bp->b_flags, B_WARM)) {
			queue = &cache->warmqueue;
			cache->warmbufpages -= pages;
		} else {
			queue = &cache->hotqueue;
			cache->hotbufpages -= pages;
		}
		TAILQ_REMOVE(queue, bp, b_freelist);
		cache->cachepages -= pages;
		CLR(bp->b_flags, B_WARM);
		CLR(bp->b_flags, B_COLD);
		bp->cache++;
		newcache= &cleancache[bp->cache];
		newcache->cachepages += pages;
		newcache->hotbufpages += pages;
		chillbufs(newcache, &newcache->hotqueue,
		    &newcache->hotbufpages);
		TAILQ_INSERT_TAIL(&newcache->hotqueue, bp, b_freelist);
	}
	return bp;
}


void
discard_buffer(struct buf *bp)
{
	splassert(IPL_BIO);

	bufcache_take(bp);
	if (bp->b_vp) {
		RBT_REMOVE(buf_rb_bufs,
		    &bp->b_vp->v_bufs_tree, bp);
		brelvp(bp);
	}
	buf_put(bp);
}

int64_t
bufcache_recover_dmapages(int discard, int64_t howmany)
{
	struct buf *bp = NULL;
	struct bufcache *cache = &cleancache[DMA_CACHE];
	struct bufqueue * queue;
	int64_t recovered = 0;

	splassert(IPL_BIO);

	while ((recovered < howmany) &&
	    ((bp = TAILQ_FIRST(&cache->coldqueue)) ||
	    (bp = TAILQ_FIRST(&cache->warmqueue)) ||
	    (bp = TAILQ_FIRST(&cache->hotqueue)))) {
		int64_t pages = atop(bp->b_bufsize);
		struct bufcache *newcache;

		if (discard || DMA_CACHE >= NUM_CACHES - 1) {
			discard_buffer(bp);
			continue;
		}
		KASSERT(bp->cache == DMA_CACHE);

		/*
		 * If this buffer was warm before, move it to
		 * the hot queue in the next cache
		 */

		/*
		 * One way or another, the pages for this
		 * buffer are leaving DMA memory
		 */
		recovered += pages;

		if (!fliphigh) {
			discard_buffer(bp);
			continue;
		}

		/*
		 * If we are in the DMA cache, try to flip the
		 * buffer up high to move it on to the other
		 * caches. if we can't move the buffer to high
		 * memory without sleeping, we give it up
		 * now rather than fight for more memory
		 * against non buffer cache competitors.
		 */
		SET(bp->b_flags, B_BUSY);
		if (bp->cache == 0 && buf_flip_high(bp) == -1) {
			CLR(bp->b_flags, B_BUSY);
			discard_buffer(bp);
			continue;
		}
		CLR(bp->b_flags, B_BUSY);

		/*
		 * Move the buffer to the hot queue in the next cache
		 */
		if (ISSET(bp->b_flags, B_COLD)) {
			queue = &cache->coldqueue;
		} else if (ISSET(bp->b_flags, B_WARM)) {
			queue = &cache->warmqueue;
			cache->warmbufpages -= pages;
		} else {
			queue = &cache->hotqueue;
			cache->hotbufpages -= pages;
		}
		TAILQ_REMOVE(queue, bp, b_freelist);
		cache->cachepages -= pages;
		CLR(bp->b_flags, B_WARM);
		CLR(bp->b_flags, B_COLD);
		bp->cache++;
		newcache= &cleancache[bp->cache];
		newcache->cachepages += pages;
		newcache->hotbufpages += pages;
		chillbufs(newcache, &newcache->hotqueue,
		    &newcache->hotbufpages);
		TAILQ_INSERT_TAIL(&newcache->hotqueue, bp, b_freelist);
	}
	return recovered;
}

struct buf *
bufcache_getcleanbuf_range(int start, int end, int discard)
{
	int i, j = start, q = end;
	struct buf *bp = NULL;

	/*
	 * XXX in theory we could promote warm buffers into a previous queue
	 * so in the pathological case of where we go through all the caches
	 * without getting a buffer we have to start at the beginning again.
	 */
	while (j <= q)	{
		for (i = q; i >= j; i--)
			if ((bp = bufcache_getcleanbuf(i, discard)))
				return (bp);
		j++;
	}
	return bp;
}

struct buf *
bufcache_gethighcleanbuf(void)
{
	if (!fliphigh)
		return NULL;
	return bufcache_getcleanbuf_range(DMA_CACHE + 1, NUM_CACHES - 1, 0);
}

struct buf *
bufcache_getdirtybuf(void)
{
	return TAILQ_FIRST(&dirtyqueue);
}

void
bufcache_take(struct buf *bp)
{
	struct bufqueue *queue;
	int64_t pages;

	splassert(IPL_BIO);
	KASSERT(ISSET(bp->b_flags, B_BC));
	KASSERT(bp->cache >= DMA_CACHE);
	KASSERT((bp->cache < NUM_CACHES));

	pages = atop(bp->b_bufsize);

	TRACEPOINT(vfs, bufcache_take, bp->b_flags, bp->cache, pages);

	struct bufcache *cache = &cleancache[bp->cache];
	if (!ISSET(bp->b_flags, B_DELWRI)) {
                if (ISSET(bp->b_flags, B_COLD)) {
			queue = &cache->coldqueue;
		} else if (ISSET(bp->b_flags, B_WARM)) {
			queue = &cache->warmqueue;
			cache->warmbufpages -= pages;
		} else {
			queue = &cache->hotqueue;
			cache->hotbufpages -= pages;
		}
		bcstats.numcleanpages -= pages;
		cache->cachepages -= pages;
	} else {
		queue = &dirtyqueue;
		bcstats.numdirtypages -= pages;
		bcstats.delwribufs--;
	}
	TAILQ_REMOVE(queue, bp, b_freelist);
}

/* move buffers from a hot or warm queue to a cold queue in a cache */
int
chillbufs(struct bufcache *cache, struct bufqueue *queue, int64_t *queuepages)
{
	struct buf *bp;
	int64_t limit, pages;

	/*
	 * We limit the hot queue to be small, with a max of 4096 pages.
	 * We limit the warm queue to half the cache size.
	 *
	 * We impose a minimum size of 96 to prevent too much "wobbling".
	 */
	if (queue == &cache->hotqueue)
		limit = min(cache->cachepages / 20, 4096);
	else if (queue == &cache->warmqueue)
		limit = (cache->cachepages / 2);
	else
		panic("chillbufs: invalid queue");

	if (*queuepages > 96 && *queuepages > limit) {
		bp = TAILQ_FIRST(queue);
		if (!bp)
			panic("inconsistent bufpage counts");
		pages = atop(bp->b_bufsize);
		*queuepages -= pages;
		TAILQ_REMOVE(queue, bp, b_freelist);
		/* we do not clear B_WARM */
		SET(bp->b_flags, B_COLD);
		TAILQ_INSERT_TAIL(&cache->coldqueue, bp, b_freelist);
		return 1;
	}
	return 0;
}

void
bufcache_release(struct buf *bp)
{
	struct bufqueue *queue;
	int64_t pages;
	struct bufcache *cache = &cleancache[bp->cache];

	KASSERT(ISSET(bp->b_flags, B_BC));
	pages = atop(bp->b_bufsize);

	TRACEPOINT(vfs, bufcache_rel, bp->b_flags, bp->cache, pages);

	if (fliphigh) {
		if (ISSET(bp->b_flags, B_DMA) && bp->cache > 0)
			panic("B_DMA buffer release from cache %d",
			    bp->cache);
		else if ((!ISSET(bp->b_flags, B_DMA)) && bp->cache == 0)
			panic("Non B_DMA buffer release from cache %d",
			    bp->cache);
	}

	if (!ISSET(bp->b_flags, B_DELWRI)) {
		int64_t *queuepages;
		if (ISSET(bp->b_flags, B_WARM | B_COLD)) {
			SET(bp->b_flags, B_WARM);
			CLR(bp->b_flags, B_COLD);
			queue = &cache->warmqueue;
			queuepages = &cache->warmbufpages;
		} else {
			queue = &cache->hotqueue;
			queuepages = &cache->hotbufpages;
		}
		*queuepages += pages;
		bcstats.numcleanpages += pages;
		cache->cachepages += pages;
		chillbufs(cache, queue, queuepages);
	} else {
		queue = &dirtyqueue;
		bcstats.numdirtypages += pages;
		bcstats.delwribufs++;
	}
	TAILQ_INSERT_TAIL(queue, bp, b_freelist);
}

#ifdef HIBERNATE
/*
 * Nuke the buffer cache from orbit when hibernating. We do not want to save
 * any clean cache pages to swap and read them back. the original disk files
 * are just as good.
 */
void
hibernate_suspend_bufcache(void)
{
	struct buf *bp;
	int s;

	s = splbio();
	/* Chuck away all the cache pages.. discard bufs, do not promote */
	while ((bp = bufcache_getcleanbuf_range(DMA_CACHE, NUM_CACHES - 1, 1))) {
		bufcache_take(bp);
		if (bp->b_vp) {
			RBT_REMOVE(buf_rb_bufs, &bp->b_vp->v_bufs_tree, bp);
			brelvp(bp);
		}
		buf_put(bp);
	}
	splx(s);
}

void
hibernate_resume_bufcache(void)
{
	/* XXX Nothing needed here for now */
}
#endif /* HIBERNATE */
