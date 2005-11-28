/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "xfs.h"
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_trans.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_alloc.h"
#include "xfs_btree.h"
#include "xfs_error.h"
#include "xfs_rw.h"
#include "xfs_iomap.h"
#include <linux/mpage.h>
#include <linux/writeback.h>

STATIC void xfs_count_page_state(struct page *, int *, int *, int *);
STATIC void xfs_convert_page(struct inode *, struct page *, xfs_iomap_t *,
		struct writeback_control *wbc, void *, int, int);

#if defined(XFS_RW_TRACE)
void
xfs_page_trace(
	int		tag,
	struct inode	*inode,
	struct page	*page,
	int		mask)
{
	xfs_inode_t	*ip;
	bhv_desc_t	*bdp;
	vnode_t		*vp = LINVFS_GET_VP(inode);
	loff_t		isize = i_size_read(inode);
	loff_t		offset = (loff_t)page->index << PAGE_CACHE_SHIFT;
	int		delalloc = -1, unmapped = -1, unwritten = -1;

	if (page_has_buffers(page))
		xfs_count_page_state(page, &delalloc, &unmapped, &unwritten);

	bdp = vn_bhv_lookup(VN_BHV_HEAD(vp), &xfs_vnodeops);
	ip = XFS_BHVTOI(bdp);
	if (!ip->i_rwtrace)
		return;

	ktrace_enter(ip->i_rwtrace,
		(void *)((unsigned long)tag),
		(void *)ip,
		(void *)inode,
		(void *)page,
		(void *)((unsigned long)mask),
		(void *)((unsigned long)((ip->i_d.di_size >> 32) & 0xffffffff)),
		(void *)((unsigned long)(ip->i_d.di_size & 0xffffffff)),
		(void *)((unsigned long)((isize >> 32) & 0xffffffff)),
		(void *)((unsigned long)(isize & 0xffffffff)),
		(void *)((unsigned long)((offset >> 32) & 0xffffffff)),
		(void *)((unsigned long)(offset & 0xffffffff)),
		(void *)((unsigned long)delalloc),
		(void *)((unsigned long)unmapped),
		(void *)((unsigned long)unwritten),
		(void *)NULL,
		(void *)NULL);
}
#else
#define xfs_page_trace(tag, inode, page, mask)
#endif

/*
 * Schedule IO completion handling on a xfsdatad if this was
 * the final hold on this ioend.
 */
STATIC void
xfs_finish_ioend(
	xfs_ioend_t		*ioend)
{
	if (atomic_dec_and_test(&ioend->io_remaining))
		queue_work(xfsdatad_workqueue, &ioend->io_work);
}

STATIC void
xfs_destroy_ioend(
	xfs_ioend_t		*ioend)
{
	vn_iowake(ioend->io_vnode);
	mempool_free(ioend, xfs_ioend_pool);
}

/*
 * Issue transactions to convert a buffer range from unwritten
 * to written extents.
 */
STATIC void
xfs_end_bio_unwritten(
	void			*data)
{
	xfs_ioend_t		*ioend = data;
	vnode_t			*vp = ioend->io_vnode;
	xfs_off_t		offset = ioend->io_offset;
	size_t			size = ioend->io_size;
	struct buffer_head	*bh, *next;
	int			error;

	if (ioend->io_uptodate)
		VOP_BMAP(vp, offset, size, BMAPI_UNWRITTEN, NULL, NULL, error);

	/* ioend->io_buffer_head is only non-NULL for buffered I/O */
	for (bh = ioend->io_buffer_head; bh; bh = next) {
		next = bh->b_private;

		bh->b_end_io = NULL;
		clear_buffer_unwritten(bh);
		end_buffer_async_write(bh, ioend->io_uptodate);
	}

	xfs_destroy_ioend(ioend);
}

/*
 * Allocate and initialise an IO completion structure.
 * We need to track unwritten extent write completion here initially.
 * We'll need to extend this for updating the ondisk inode size later
 * (vs. incore size).
 */
STATIC xfs_ioend_t *
xfs_alloc_ioend(
	struct inode		*inode)
{
	xfs_ioend_t		*ioend;

	ioend = mempool_alloc(xfs_ioend_pool, GFP_NOFS);

	/*
	 * Set the count to 1 initially, which will prevent an I/O
	 * completion callback from happening before we have started
	 * all the I/O from calling the completion routine too early.
	 */
	atomic_set(&ioend->io_remaining, 1);
	ioend->io_uptodate = 1; /* cleared if any I/O fails */
	ioend->io_vnode = LINVFS_GET_VP(inode);
	ioend->io_buffer_head = NULL;
	atomic_inc(&ioend->io_vnode->v_iocount);
	ioend->io_offset = 0;
	ioend->io_size = 0;

	INIT_WORK(&ioend->io_work, xfs_end_bio_unwritten, ioend);

	return ioend;
}

void
linvfs_unwritten_done(
	struct buffer_head	*bh,
	int			uptodate)
{
	xfs_ioend_t		*ioend = bh->b_private;
	static spinlock_t	unwritten_done_lock = SPIN_LOCK_UNLOCKED;
	unsigned long		flags;

	ASSERT(buffer_unwritten(bh));
	bh->b_end_io = NULL;

	if (!uptodate)
		ioend->io_uptodate = 0;

	/*
	 * Deep magic here.  We reuse b_private in the buffer_heads to build
	 * a chain for completing the I/O from user context after we've issued
	 * a transaction to convert the unwritten extent.
	 */
	spin_lock_irqsave(&unwritten_done_lock, flags);
	bh->b_private = ioend->io_buffer_head;
	ioend->io_buffer_head = bh;
	spin_unlock_irqrestore(&unwritten_done_lock, flags);

	xfs_finish_ioend(ioend);
}

STATIC int
xfs_map_blocks(
	struct inode		*inode,
	loff_t			offset,
	ssize_t			count,
	xfs_iomap_t		*mapp,
	int			flags)
{
	vnode_t			*vp = LINVFS_GET_VP(inode);
	int			error, nmaps = 1;

	VOP_BMAP(vp, offset, count, flags, mapp, &nmaps, error);
	if (!error && (flags & (BMAPI_WRITE|BMAPI_ALLOCATE)))
		VMODIFY(vp);
	return -error;
}

/*
 * Finds the corresponding mapping in block @map array of the
 * given @offset within a @page.
 */
STATIC xfs_iomap_t *
xfs_offset_to_map(
	struct page		*page,
	xfs_iomap_t		*iomapp,
	unsigned long		offset)
{
	loff_t			full_offset;	/* offset from start of file */

	ASSERT(offset < PAGE_CACHE_SIZE);

	full_offset = page->index;		/* NB: using 64bit number */
	full_offset <<= PAGE_CACHE_SHIFT;	/* offset from file start */
	full_offset += offset;			/* offset from page start */

	if (full_offset < iomapp->iomap_offset)
		return NULL;
	if (iomapp->iomap_offset + (iomapp->iomap_bsize -1) >= full_offset)
		return iomapp;
	return NULL;
}

STATIC void
xfs_map_at_offset(
	struct page		*page,
	struct buffer_head	*bh,
	unsigned long		offset,
	int			block_bits,
	xfs_iomap_t		*iomapp)
{
	xfs_daddr_t		bn;
	loff_t			delta;
	int			sector_shift;

	ASSERT(!(iomapp->iomap_flags & IOMAP_HOLE));
	ASSERT(!(iomapp->iomap_flags & IOMAP_DELAY));
	ASSERT(iomapp->iomap_bn != IOMAP_DADDR_NULL);

	delta = page->index;
	delta <<= PAGE_CACHE_SHIFT;
	delta += offset;
	delta -= iomapp->iomap_offset;
	delta >>= block_bits;

	sector_shift = block_bits - BBSHIFT;
	bn = iomapp->iomap_bn >> sector_shift;
	bn += delta;
	BUG_ON(!bn && !(iomapp->iomap_flags & IOMAP_REALTIME));
	ASSERT((bn << sector_shift) >= iomapp->iomap_bn);

	lock_buffer(bh);
	bh->b_blocknr = bn;
	bh->b_bdev = iomapp->iomap_target->pbr_bdev;
	set_buffer_mapped(bh);
	clear_buffer_delay(bh);
}

/*
 * Look for a page at index which is unlocked and contains our
 * unwritten extent flagged buffers at its head.  Returns page
 * locked and with an extra reference count, and length of the
 * unwritten extent component on this page that we can write,
 * in units of filesystem blocks.
 */
STATIC struct page *
xfs_probe_unwritten_page(
	struct address_space	*mapping,
	pgoff_t			index,
	xfs_iomap_t		*iomapp,
	xfs_ioend_t		*ioend,
	unsigned long		max_offset,
	unsigned long		*fsbs,
	unsigned int            bbits)
{
	struct page		*page;

	page = find_trylock_page(mapping, index);
	if (!page)
		return NULL;
	if (PageWriteback(page))
		goto out;

	if (page->mapping && page_has_buffers(page)) {
		struct buffer_head	*bh, *head;
		unsigned long		p_offset = 0;

		*fsbs = 0;
		bh = head = page_buffers(page);
		do {
			if (!buffer_unwritten(bh) || !buffer_uptodate(bh))
				break;
			if (!xfs_offset_to_map(page, iomapp, p_offset))
				break;
			if (p_offset >= max_offset)
				break;
			xfs_map_at_offset(page, bh, p_offset, bbits, iomapp);
			set_buffer_unwritten_io(bh);
			bh->b_private = ioend;
			p_offset += bh->b_size;
			(*fsbs)++;
		} while ((bh = bh->b_this_page) != head);

		if (p_offset)
			return page;
	}

out:
	unlock_page(page);
	return NULL;
}

/*
 * Look for a page at index which is unlocked and not mapped
 * yet - clustering for mmap write case.
 */
STATIC unsigned int
xfs_probe_unmapped_page(
	struct address_space	*mapping,
	pgoff_t			index,
	unsigned int		pg_offset)
{
	struct page		*page;
	int			ret = 0;

	page = find_trylock_page(mapping, index);
	if (!page)
		return 0;
	if (PageWriteback(page))
		goto out;

	if (page->mapping && PageDirty(page)) {
		if (page_has_buffers(page)) {
			struct buffer_head	*bh, *head;

			bh = head = page_buffers(page);
			do {
				if (buffer_mapped(bh) || !buffer_uptodate(bh))
					break;
				ret += bh->b_size;
				if (ret >= pg_offset)
					break;
			} while ((bh = bh->b_this_page) != head);
		} else
			ret = PAGE_CACHE_SIZE;
	}

out:
	unlock_page(page);
	return ret;
}

STATIC unsigned int
xfs_probe_unmapped_cluster(
	struct inode		*inode,
	struct page		*startpage,
	struct buffer_head	*bh,
	struct buffer_head	*head)
{
	pgoff_t			tindex, tlast, tloff;
	unsigned int		pg_offset, len, total = 0;
	struct address_space	*mapping = inode->i_mapping;

	/* First sum forwards in this page */
	do {
		if (buffer_mapped(bh))
			break;
		total += bh->b_size;
	} while ((bh = bh->b_this_page) != head);

	/* If we reached the end of the page, sum forwards in
	 * following pages.
	 */
	if (bh == head) {
		tlast = i_size_read(inode) >> PAGE_CACHE_SHIFT;
		/* Prune this back to avoid pathological behavior */
		tloff = min(tlast, startpage->index + 64);
		for (tindex = startpage->index + 1; tindex < tloff; tindex++) {
			len = xfs_probe_unmapped_page(mapping, tindex,
							PAGE_CACHE_SIZE);
			if (!len)
				return total;
			total += len;
		}
		if (tindex == tlast &&
		    (pg_offset = i_size_read(inode) & (PAGE_CACHE_SIZE - 1))) {
			total += xfs_probe_unmapped_page(mapping,
							tindex, pg_offset);
		}
	}
	return total;
}

/*
 * Probe for a given page (index) in the inode and test if it is delayed
 * and without unwritten buffers.  Returns page locked and with an extra
 * reference count.
 */
STATIC struct page *
xfs_probe_delalloc_page(
	struct inode		*inode,
	pgoff_t			index)
{
	struct page		*page;

	page = find_trylock_page(inode->i_mapping, index);
	if (!page)
		return NULL;
	if (PageWriteback(page))
		goto out;

	if (page->mapping && page_has_buffers(page)) {
		struct buffer_head	*bh, *head;
		int			acceptable = 0;

		bh = head = page_buffers(page);
		do {
			if (buffer_unwritten(bh)) {
				acceptable = 0;
				break;
			} else if (buffer_delay(bh)) {
				acceptable = 1;
			}
		} while ((bh = bh->b_this_page) != head);

		if (acceptable)
			return page;
	}

out:
	unlock_page(page);
	return NULL;
}

STATIC int
xfs_map_unwritten(
	struct inode		*inode,
	struct page		*start_page,
	struct buffer_head	*head,
	struct buffer_head	*curr,
	unsigned long		p_offset,
	int			block_bits,
	xfs_iomap_t		*iomapp,
	struct writeback_control *wbc,
	int			startio,
	int			all_bh)
{
	struct buffer_head	*bh = curr;
	xfs_iomap_t		*tmp;
	xfs_ioend_t		*ioend;
	loff_t			offset;
	unsigned long		nblocks = 0;

	offset = start_page->index;
	offset <<= PAGE_CACHE_SHIFT;
	offset += p_offset;

	ioend = xfs_alloc_ioend(inode);

	/* First map forwards in the page consecutive buffers
	 * covering this unwritten extent
	 */
	do {
		if (!buffer_unwritten(bh))
			break;
		tmp = xfs_offset_to_map(start_page, iomapp, p_offset);
		if (!tmp)
			break;
		xfs_map_at_offset(start_page, bh, p_offset, block_bits, iomapp);
		set_buffer_unwritten_io(bh);
		bh->b_private = ioend;
		p_offset += bh->b_size;
		nblocks++;
	} while ((bh = bh->b_this_page) != head);

	atomic_add(nblocks, &ioend->io_remaining);

	/* If we reached the end of the page, map forwards in any
	 * following pages which are also covered by this extent.
	 */
	if (bh == head) {
		struct address_space	*mapping = inode->i_mapping;
		pgoff_t			tindex, tloff, tlast;
		unsigned long		bs;
		unsigned int		pg_offset, bbits = inode->i_blkbits;
		struct page		*page;

		tlast = i_size_read(inode) >> PAGE_CACHE_SHIFT;
		tloff = (iomapp->iomap_offset + iomapp->iomap_bsize) >> PAGE_CACHE_SHIFT;
		tloff = min(tlast, tloff);
		for (tindex = start_page->index + 1; tindex < tloff; tindex++) {
			page = xfs_probe_unwritten_page(mapping,
						tindex, iomapp, ioend,
						PAGE_CACHE_SIZE, &bs, bbits);
			if (!page)
				break;
			nblocks += bs;
			atomic_add(bs, &ioend->io_remaining);
			xfs_convert_page(inode, page, iomapp, wbc, ioend,
							startio, all_bh);
			/* stop if converting the next page might add
			 * enough blocks that the corresponding byte
			 * count won't fit in our ulong page buf length */
			if (nblocks >= ((ULONG_MAX - PAGE_SIZE) >> block_bits))
				goto enough;
		}

		if (tindex == tlast &&
		    (pg_offset = (i_size_read(inode) & (PAGE_CACHE_SIZE - 1)))) {
			page = xfs_probe_unwritten_page(mapping,
							tindex, iomapp, ioend,
							pg_offset, &bs, bbits);
			if (page) {
				nblocks += bs;
				atomic_add(bs, &ioend->io_remaining);
				xfs_convert_page(inode, page, iomapp, wbc, ioend,
							startio, all_bh);
				if (nblocks >= ((ULONG_MAX - PAGE_SIZE) >> block_bits))
					goto enough;
			}
		}
	}

enough:
	ioend->io_size = (xfs_off_t)nblocks << block_bits;
	ioend->io_offset = offset;
	xfs_finish_ioend(ioend);
	return 0;
}

STATIC void
xfs_submit_page(
	struct page		*page,
	struct writeback_control *wbc,
	struct buffer_head	*bh_arr[],
	int			bh_count,
	int			probed_page,
	int			clear_dirty)
{
	struct buffer_head	*bh;
	int			i;

	BUG_ON(PageWriteback(page));
	if (bh_count)
		set_page_writeback(page);
	if (clear_dirty)
		clear_page_dirty(page);
	unlock_page(page);

	if (bh_count) {
		for (i = 0; i < bh_count; i++) {
			bh = bh_arr[i];
			mark_buffer_async_write(bh);
			if (buffer_unwritten(bh))
				set_buffer_unwritten_io(bh);
			set_buffer_uptodate(bh);
			clear_buffer_dirty(bh);
		}

		for (i = 0; i < bh_count; i++)
			submit_bh(WRITE, bh_arr[i]);

		if (probed_page && clear_dirty)
			wbc->nr_to_write--;	/* Wrote an "extra" page */
	}
}

/*
 * Allocate & map buffers for page given the extent map. Write it out.
 * except for the original page of a writepage, this is called on
 * delalloc/unwritten pages only, for the original page it is possible
 * that the page has no mapping at all.
 */
STATIC void
xfs_convert_page(
	struct inode		*inode,
	struct page		*page,
	xfs_iomap_t		*iomapp,
	struct writeback_control *wbc,
	void			*private,
	int			startio,
	int			all_bh)
{
	struct buffer_head	*bh_arr[MAX_BUF_PER_PAGE], *bh, *head;
	xfs_iomap_t		*mp = iomapp, *tmp;
	unsigned long		offset, end_offset;
	int			index = 0;
	int			bbits = inode->i_blkbits;
	int			len, page_dirty;

	end_offset = (i_size_read(inode) & (PAGE_CACHE_SIZE - 1));

	/*
	 * page_dirty is initially a count of buffers on the page before
	 * EOF and is decrememted as we move each into a cleanable state.
	 */
	len = 1 << inode->i_blkbits;
	end_offset = max(end_offset, PAGE_CACHE_SIZE);
	end_offset = roundup(end_offset, len);
	page_dirty = end_offset / len;

	offset = 0;
	bh = head = page_buffers(page);
	do {
		if (offset >= end_offset)
			break;
		if (!(PageUptodate(page) || buffer_uptodate(bh)))
			continue;
		if (buffer_mapped(bh) && all_bh &&
		    !(buffer_unwritten(bh) || buffer_delay(bh))) {
			if (startio) {
				lock_buffer(bh);
				bh_arr[index++] = bh;
				page_dirty--;
			}
			continue;
		}
		tmp = xfs_offset_to_map(page, mp, offset);
		if (!tmp)
			continue;
		ASSERT(!(tmp->iomap_flags & IOMAP_HOLE));
		ASSERT(!(tmp->iomap_flags & IOMAP_DELAY));

		/* If this is a new unwritten extent buffer (i.e. one
		 * that we haven't passed in private data for, we must
		 * now map this buffer too.
		 */
		if (buffer_unwritten(bh) && !bh->b_end_io) {
			ASSERT(tmp->iomap_flags & IOMAP_UNWRITTEN);
			xfs_map_unwritten(inode, page, head, bh, offset,
					bbits, tmp, wbc, startio, all_bh);
		} else if (! (buffer_unwritten(bh) && buffer_locked(bh))) {
			xfs_map_at_offset(page, bh, offset, bbits, tmp);
			if (buffer_unwritten(bh)) {
				set_buffer_unwritten_io(bh);
				bh->b_private = private;
				ASSERT(private);
			}
		}
		if (startio) {
			bh_arr[index++] = bh;
		} else {
			set_buffer_dirty(bh);
			unlock_buffer(bh);
			mark_buffer_dirty(bh);
		}
		page_dirty--;
	} while (offset += len, (bh = bh->b_this_page) != head);

	if (startio && index) {
		xfs_submit_page(page, wbc, bh_arr, index, 1, !page_dirty);
	} else {
		unlock_page(page);
	}
}

/*
 * Convert & write out a cluster of pages in the same extent as defined
 * by mp and following the start page.
 */
STATIC void
xfs_cluster_write(
	struct inode		*inode,
	pgoff_t			tindex,
	xfs_iomap_t		*iomapp,
	struct writeback_control *wbc,
	int			startio,
	int			all_bh,
	pgoff_t			tlast)
{
	struct page		*page;

	for (; tindex <= tlast; tindex++) {
		page = xfs_probe_delalloc_page(inode, tindex);
		if (!page)
			break;
		xfs_convert_page(inode, page, iomapp, wbc, NULL,
				startio, all_bh);
	}
}

/*
 * Calling this without startio set means we are being asked to make a dirty
 * page ready for freeing it's buffers.  When called with startio set then
 * we are coming from writepage.
 *
 * When called with startio set it is important that we write the WHOLE
 * page if possible.
 * The bh->b_state's cannot know if any of the blocks or which block for
 * that matter are dirty due to mmap writes, and therefore bh uptodate is
 * only vaild if the page itself isn't completely uptodate.  Some layers
 * may clear the page dirty flag prior to calling write page, under the
 * assumption the entire page will be written out; by not writing out the
 * whole page the page can be reused before all valid dirty data is
 * written out.  Note: in the case of a page that has been dirty'd by
 * mapwrite and but partially setup by block_prepare_write the
 * bh->b_states's will not agree and only ones setup by BPW/BCW will have
 * valid state, thus the whole page must be written out thing.
 */

STATIC int
xfs_page_state_convert(
	struct inode	*inode,
	struct page	*page,
	struct writeback_control *wbc,
	int		startio,
	int		unmapped) /* also implies page uptodate */
{
	struct buffer_head	*bh_arr[MAX_BUF_PER_PAGE], *bh, *head;
	xfs_iomap_t		*iomp, iomap;
	loff_t			offset;
	unsigned long           p_offset = 0;
	__uint64_t              end_offset;
	pgoff_t                 end_index, last_index, tlast;
	int			len, err, i, cnt = 0, uptodate = 1;
	int			flags;
	int			page_dirty;

	/* wait for other IO threads? */
	flags = (startio && wbc->sync_mode != WB_SYNC_NONE) ? 0 : BMAPI_TRYLOCK;

	/* Is this page beyond the end of the file? */
	offset = i_size_read(inode);
	end_index = offset >> PAGE_CACHE_SHIFT;
	last_index = (offset - 1) >> PAGE_CACHE_SHIFT;
	if (page->index >= end_index) {
		if ((page->index >= end_index + 1) ||
		    !(i_size_read(inode) & (PAGE_CACHE_SIZE - 1))) {
			if (startio)
				unlock_page(page);
			return 0;
		}
	}

	end_offset = min_t(unsigned long long,
			(loff_t)(page->index + 1) << PAGE_CACHE_SHIFT, offset);
	offset = (loff_t)page->index << PAGE_CACHE_SHIFT;

	/*
	 * page_dirty is initially a count of buffers on the page before
	 * EOF and is decrememted as we move each into a cleanable state.
	 */
	len = 1 << inode->i_blkbits;
	p_offset = max(p_offset, PAGE_CACHE_SIZE);
	p_offset = roundup(p_offset, len);
	page_dirty = p_offset / len;

	iomp = NULL;
	p_offset = 0;
	bh = head = page_buffers(page);

	do {
		if (offset >= end_offset)
			break;
		if (!buffer_uptodate(bh))
			uptodate = 0;
		if (!(PageUptodate(page) || buffer_uptodate(bh)) && !startio)
			continue;

		if (iomp) {
			iomp = xfs_offset_to_map(page, &iomap, p_offset);
		}

		/*
		 * First case, map an unwritten extent and prepare for
		 * extent state conversion transaction on completion.
		 */
		if (buffer_unwritten(bh)) {
			if (!startio)
				continue;
			if (!iomp) {
				err = xfs_map_blocks(inode, offset, len, &iomap,
						BMAPI_WRITE|BMAPI_IGNSTATE);
				if (err) {
					goto error;
				}
				iomp = xfs_offset_to_map(page, &iomap,
								p_offset);
			}
			if (iomp) {
				if (!bh->b_end_io) {
					err = xfs_map_unwritten(inode, page,
							head, bh, p_offset,
							inode->i_blkbits, iomp,
							wbc, startio, unmapped);
					if (err) {
						goto error;
					}
				} else {
					set_bit(BH_Lock, &bh->b_state);
				}
				BUG_ON(!buffer_locked(bh));
				bh_arr[cnt++] = bh;
				page_dirty--;
			}
		/*
		 * Second case, allocate space for a delalloc buffer.
		 * We can return EAGAIN here in the release page case.
		 */
		} else if (buffer_delay(bh)) {
			if (!iomp) {
				err = xfs_map_blocks(inode, offset, len, &iomap,
						BMAPI_ALLOCATE | flags);
				if (err) {
					goto error;
				}
				iomp = xfs_offset_to_map(page, &iomap,
								p_offset);
			}
			if (iomp) {
				xfs_map_at_offset(page, bh, p_offset,
						inode->i_blkbits, iomp);
				if (startio) {
					bh_arr[cnt++] = bh;
				} else {
					set_buffer_dirty(bh);
					unlock_buffer(bh);
					mark_buffer_dirty(bh);
				}
				page_dirty--;
			}
		} else if ((buffer_uptodate(bh) || PageUptodate(page)) &&
			   (unmapped || startio)) {

			if (!buffer_mapped(bh)) {
				int	size;

				/*
				 * Getting here implies an unmapped buffer
				 * was found, and we are in a path where we
				 * need to write the whole page out.
				 */
				if (!iomp) {
					size = xfs_probe_unmapped_cluster(
							inode, page, bh, head);
					err = xfs_map_blocks(inode, offset,
							size, &iomap,
							BMAPI_WRITE|BMAPI_MMAP);
					if (err) {
						goto error;
					}
					iomp = xfs_offset_to_map(page, &iomap,
								     p_offset);
				}
				if (iomp) {
					xfs_map_at_offset(page,
							bh, p_offset,
							inode->i_blkbits, iomp);
					if (startio) {
						bh_arr[cnt++] = bh;
					} else {
						set_buffer_dirty(bh);
						unlock_buffer(bh);
						mark_buffer_dirty(bh);
					}
					page_dirty--;
				}
			} else if (startio) {
				if (buffer_uptodate(bh) &&
				    !test_and_set_bit(BH_Lock, &bh->b_state)) {
					bh_arr[cnt++] = bh;
					page_dirty--;
				}
			}
		}
	} while (offset += len, p_offset += len,
		((bh = bh->b_this_page) != head));

	if (uptodate && bh == head)
		SetPageUptodate(page);

	if (startio) {
		xfs_submit_page(page, wbc, bh_arr, cnt, 0, !page_dirty);
	}

	if (iomp) {
		offset = (iomp->iomap_offset + iomp->iomap_bsize - 1) >>
					PAGE_CACHE_SHIFT;
		tlast = min_t(pgoff_t, offset, last_index);
		xfs_cluster_write(inode, page->index + 1, iomp, wbc,
					startio, unmapped, tlast);
	}

	return page_dirty;

error:
	for (i = 0; i < cnt; i++) {
		unlock_buffer(bh_arr[i]);
	}

	/*
	 * If it's delalloc and we have nowhere to put it,
	 * throw it away, unless the lower layers told
	 * us to try again.
	 */
	if (err != -EAGAIN) {
		if (!unmapped) {
			block_invalidatepage(page, 0);
		}
		ClearPageUptodate(page);
	}
	return err;
}

STATIC int
__linvfs_get_block(
	struct inode		*inode,
	sector_t		iblock,
	unsigned long		blocks,
	struct buffer_head	*bh_result,
	int			create,
	int			direct,
	bmapi_flags_t		flags)
{
	vnode_t			*vp = LINVFS_GET_VP(inode);
	xfs_iomap_t		iomap;
	xfs_off_t		offset;
	ssize_t			size;
	int			retpbbm = 1;
	int			error;

	offset = (xfs_off_t)iblock << inode->i_blkbits;
	if (blocks)
		size = (ssize_t) min_t(xfs_off_t, LONG_MAX,
					(xfs_off_t)blocks << inode->i_blkbits);
	else
		size = 1 << inode->i_blkbits;

	VOP_BMAP(vp, offset, size,
		create ? flags : BMAPI_READ, &iomap, &retpbbm, error);
	if (error)
		return -error;

	if (retpbbm == 0)
		return 0;

	if (iomap.iomap_bn != IOMAP_DADDR_NULL) {
		xfs_daddr_t	bn;
		xfs_off_t	delta;

		/* For unwritten extents do not report a disk address on
		 * the read case (treat as if we're reading into a hole).
		 */
		if (create || !(iomap.iomap_flags & IOMAP_UNWRITTEN)) {
			delta = offset - iomap.iomap_offset;
			delta >>= inode->i_blkbits;

			bn = iomap.iomap_bn >> (inode->i_blkbits - BBSHIFT);
			bn += delta;
			BUG_ON(!bn && !(iomap.iomap_flags & IOMAP_REALTIME));
			bh_result->b_blocknr = bn;
			set_buffer_mapped(bh_result);
		}
		if (create && (iomap.iomap_flags & IOMAP_UNWRITTEN)) {
			if (direct)
				bh_result->b_private = inode;
			set_buffer_unwritten(bh_result);
			set_buffer_delay(bh_result);
		}
	}

	/* If this is a realtime file, data might be on a new device */
	bh_result->b_bdev = iomap.iomap_target->pbr_bdev;

	/* If we previously allocated a block out beyond eof and
	 * we are now coming back to use it then we will need to
	 * flag it as new even if it has a disk address.
	 */
	if (create &&
	    ((!buffer_mapped(bh_result) && !buffer_uptodate(bh_result)) ||
	     (offset >= i_size_read(inode)) || (iomap.iomap_flags & IOMAP_NEW)))
		set_buffer_new(bh_result);

	if (iomap.iomap_flags & IOMAP_DELAY) {
		BUG_ON(direct);
		if (create) {
			set_buffer_uptodate(bh_result);
			set_buffer_mapped(bh_result);
			set_buffer_delay(bh_result);
		}
	}

	if (blocks) {
		ASSERT(iomap.iomap_bsize - iomap.iomap_delta > 0);
		offset = min_t(xfs_off_t,
				iomap.iomap_bsize - iomap.iomap_delta,
				(xfs_off_t)blocks << inode->i_blkbits);
		bh_result->b_size = (u32) min_t(xfs_off_t, UINT_MAX, offset);
	}

	return 0;
}

int
linvfs_get_block(
	struct inode		*inode,
	sector_t		iblock,
	struct buffer_head	*bh_result,
	int			create)
{
	return __linvfs_get_block(inode, iblock, 0, bh_result,
					create, 0, BMAPI_WRITE);
}

STATIC int
linvfs_get_blocks_direct(
	struct inode		*inode,
	sector_t		iblock,
	unsigned long		max_blocks,
	struct buffer_head	*bh_result,
	int			create)
{
	return __linvfs_get_block(inode, iblock, max_blocks, bh_result,
					create, 1, BMAPI_WRITE|BMAPI_DIRECT);
}

STATIC void
linvfs_end_io_direct(
	struct kiocb	*iocb,
	loff_t		offset,
	ssize_t		size,
	void		*private)
{
	xfs_ioend_t	*ioend = iocb->private;

	/*
	 * Non-NULL private data means we need to issue a transaction to
	 * convert a range from unwritten to written extents.  This needs
	 * to happen from process contect but aio+dio I/O completion
	 * happens from irq context so we need to defer it to a workqueue.
	 * This is not nessecary for synchronous direct I/O, but we do
	 * it anyway to keep the code uniform and simpler.
	 *
	 * The core direct I/O code might be changed to always call the
	 * completion handler in the future, in which case all this can
	 * go away.
	 */
	if (private && size > 0) {
		ioend->io_offset = offset;
		ioend->io_size = size;
		xfs_finish_ioend(ioend);
	} else {
		ASSERT(size >= 0);
		xfs_destroy_ioend(ioend);
	}

	/*
	 * blockdev_direct_IO can return an error even afer the I/O
	 * completion handler was called.  Thus we need to protect
	 * against double-freeing.
	 */
	iocb->private = NULL;
}

STATIC ssize_t
linvfs_direct_IO(
	int			rw,
	struct kiocb		*iocb,
	const struct iovec	*iov,
	loff_t			offset,
	unsigned long		nr_segs)
{
	struct file	*file = iocb->ki_filp;
	struct inode	*inode = file->f_mapping->host;
	vnode_t		*vp = LINVFS_GET_VP(inode);
	xfs_iomap_t	iomap;
	int		maps = 1;
	int		error;
	ssize_t		ret;

	VOP_BMAP(vp, offset, 0, BMAPI_DEVICE, &iomap, &maps, error);
	if (error)
		return -error;

	iocb->private = xfs_alloc_ioend(inode);

	ret = blockdev_direct_IO_own_locking(rw, iocb, inode,
		iomap.iomap_target->pbr_bdev,
		iov, offset, nr_segs,
		linvfs_get_blocks_direct,
		linvfs_end_io_direct);

	if (unlikely(ret <= 0 && iocb->private))
		xfs_destroy_ioend(iocb->private);
	return ret;
}


STATIC sector_t
linvfs_bmap(
	struct address_space	*mapping,
	sector_t		block)
{
	struct inode		*inode = (struct inode *)mapping->host;
	vnode_t			*vp = LINVFS_GET_VP(inode);
	int			error;

	vn_trace_entry(vp, "linvfs_bmap", (inst_t *)__return_address);

	VOP_RWLOCK(vp, VRWLOCK_READ);
	VOP_FLUSH_PAGES(vp, (xfs_off_t)0, -1, 0, FI_REMAPF, error);
	VOP_RWUNLOCK(vp, VRWLOCK_READ);
	return generic_block_bmap(mapping, block, linvfs_get_block);
}

STATIC int
linvfs_readpage(
	struct file		*unused,
	struct page		*page)
{
	return mpage_readpage(page, linvfs_get_block);
}

STATIC int
linvfs_readpages(
	struct file		*unused,
	struct address_space	*mapping,
	struct list_head	*pages,
	unsigned		nr_pages)
{
	return mpage_readpages(mapping, pages, nr_pages, linvfs_get_block);
}

STATIC void
xfs_count_page_state(
	struct page		*page,
	int			*delalloc,
	int			*unmapped,
	int			*unwritten)
{
	struct buffer_head	*bh, *head;

	*delalloc = *unmapped = *unwritten = 0;

	bh = head = page_buffers(page);
	do {
		if (buffer_uptodate(bh) && !buffer_mapped(bh))
			(*unmapped) = 1;
		else if (buffer_unwritten(bh) && !buffer_delay(bh))
			clear_buffer_unwritten(bh);
		else if (buffer_unwritten(bh))
			(*unwritten) = 1;
		else if (buffer_delay(bh))
			(*delalloc) = 1;
	} while ((bh = bh->b_this_page) != head);
}


/*
 * writepage: Called from one of two places:
 *
 * 1. we are flushing a delalloc buffer head.
 *
 * 2. we are writing out a dirty page. Typically the page dirty
 *    state is cleared before we get here. In this case is it
 *    conceivable we have no buffer heads.
 *
 * For delalloc space on the page we need to allocate space and
 * flush it. For unmapped buffer heads on the page we should
 * allocate space if the page is uptodate. For any other dirty
 * buffer heads on the page we should flush them.
 *
 * If we detect that a transaction would be required to flush
 * the page, we have to check the process flags first, if we
 * are already in a transaction or disk I/O during allocations
 * is off, we need to fail the writepage and redirty the page.
 */

STATIC int
linvfs_writepage(
	struct page		*page,
	struct writeback_control *wbc)
{
	int			error;
	int			need_trans;
	int			delalloc, unmapped, unwritten;
	struct inode		*inode = page->mapping->host;

	xfs_page_trace(XFS_WRITEPAGE_ENTER, inode, page, 0);

	/*
	 * We need a transaction if:
	 *  1. There are delalloc buffers on the page
	 *  2. The page is uptodate and we have unmapped buffers
	 *  3. The page is uptodate and we have no buffers
	 *  4. There are unwritten buffers on the page
	 */

	if (!page_has_buffers(page)) {
		unmapped = 1;
		need_trans = 1;
	} else {
		xfs_count_page_state(page, &delalloc, &unmapped, &unwritten);
		if (!PageUptodate(page))
			unmapped = 0;
		need_trans = delalloc + unmapped + unwritten;
	}

	/*
	 * If we need a transaction and the process flags say
	 * we are already in a transaction, or no IO is allowed
	 * then mark the page dirty again and leave the page
	 * as is.
	 */
	if (PFLAGS_TEST_FSTRANS() && need_trans)
		goto out_fail;

	/*
	 * Delay hooking up buffer heads until we have
	 * made our go/no-go decision.
	 */
	if (!page_has_buffers(page))
		create_empty_buffers(page, 1 << inode->i_blkbits, 0);

	/*
	 * Convert delayed allocate, unwritten or unmapped space
	 * to real space and flush out to disk.
	 */
	error = xfs_page_state_convert(inode, page, wbc, 1, unmapped);
	if (error == -EAGAIN)
		goto out_fail;
	if (unlikely(error < 0))
		goto out_unlock;

	return 0;

out_fail:
	redirty_page_for_writepage(wbc, page);
	unlock_page(page);
	return 0;
out_unlock:
	unlock_page(page);
	return error;
}

STATIC int
linvfs_invalidate_page(
	struct page		*page,
	unsigned long		offset)
{
	xfs_page_trace(XFS_INVALIDPAGE_ENTER,
			page->mapping->host, page, offset);
	return block_invalidatepage(page, offset);
}

/*
 * Called to move a page into cleanable state - and from there
 * to be released. Possibly the page is already clean. We always
 * have buffer heads in this call.
 *
 * Returns 0 if the page is ok to release, 1 otherwise.
 *
 * Possible scenarios are:
 *
 * 1. We are being called to release a page which has been written
 *    to via regular I/O. buffer heads will be dirty and possibly
 *    delalloc. If no delalloc buffer heads in this case then we
 *    can just return zero.
 *
 * 2. We are called to release a page which has been written via
 *    mmap, all we need to do is ensure there is no delalloc
 *    state in the buffer heads, if not we can let the caller
 *    free them and we should come back later via writepage.
 */
STATIC int
linvfs_release_page(
	struct page		*page,
	gfp_t			gfp_mask)
{
	struct inode		*inode = page->mapping->host;
	int			dirty, delalloc, unmapped, unwritten;
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_ALL,
		.nr_to_write = 1,
	};

	xfs_page_trace(XFS_RELEASEPAGE_ENTER, inode, page, gfp_mask);

	xfs_count_page_state(page, &delalloc, &unmapped, &unwritten);
	if (!delalloc && !unwritten)
		goto free_buffers;

	if (!(gfp_mask & __GFP_FS))
		return 0;

	/* If we are already inside a transaction or the thread cannot
	 * do I/O, we cannot release this page.
	 */
	if (PFLAGS_TEST_FSTRANS())
		return 0;

	/*
	 * Convert delalloc space to real space, do not flush the
	 * data out to disk, that will be done by the caller.
	 * Never need to allocate space here - we will always
	 * come back to writepage in that case.
	 */
	dirty = xfs_page_state_convert(inode, page, &wbc, 0, 0);
	if (dirty == 0 && !unwritten)
		goto free_buffers;
	return 0;

free_buffers:
	return try_to_free_buffers(page);
}

STATIC int
linvfs_prepare_write(
	struct file		*file,
	struct page		*page,
	unsigned int		from,
	unsigned int		to)
{
	return block_prepare_write(page, from, to, linvfs_get_block);
}

struct address_space_operations linvfs_aops = {
	.readpage		= linvfs_readpage,
	.readpages		= linvfs_readpages,
	.writepage		= linvfs_writepage,
	.sync_page		= block_sync_page,
	.releasepage		= linvfs_release_page,
	.invalidatepage		= linvfs_invalidate_page,
	.prepare_write		= linvfs_prepare_write,
	.commit_write		= generic_commit_write,
	.bmap			= linvfs_bmap,
	.direct_IO		= linvfs_direct_IO,
};
