/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2008 Yahoo!, Inc.
 * All rights reserved.
 * Written by: John Baldwin <jhb@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/bio.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/proc.h>
#include <sys/sglist.h>
#include <sys/uio.h>

#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>

#include <sys/ktr.h>

static MALLOC_DEFINE(M_SGLIST, "sglist", "scatter/gather lists");

/*
 * Convenience macros to save the state of an sglist so it can be restored
 * if an append attempt fails.  Since sglist's only grow we only need to
 * save the current count of segments and the length of the ending segment.
 * Earlier segments will not be changed by an append, and the only change
 * that can occur to the ending segment is that it can be extended.
 */
struct sgsave {
	u_short sg_nseg;
	size_t ss_len;
};

#define	SGLIST_SAVE(sg, sgsave) do {					\
	(sgsave).sg_nseg = (sg)->sg_nseg;				\
	if ((sgsave).sg_nseg > 0)					\
		(sgsave).ss_len = (sg)->sg_segs[(sgsave).sg_nseg - 1].ss_len; \
	else								\
		(sgsave).ss_len = 0;					\
} while (0)

#define	SGLIST_RESTORE(sg, sgsave) do {					\
	(sg)->sg_nseg = (sgsave).sg_nseg;				\
	if ((sgsave).sg_nseg > 0)					\
		(sg)->sg_segs[(sgsave).sg_nseg - 1].ss_len = (sgsave).ss_len; \
} while (0)

/*
 * Append a single (paddr, len) to a sglist.  sg is the list and ss is
 * the current segment in the list.  If we run out of segments then
 * EFBIG will be returned.
 */
static __inline int
_sglist_append_range(struct sglist *sg, struct sglist_seg **ssp,
    vm_paddr_t paddr, size_t len)
{
	struct sglist_seg *ss;

	ss = *ssp;
	if (ss->ss_paddr + ss->ss_len == paddr)
		ss->ss_len += len;
	else {
		if (sg->sg_nseg == sg->sg_maxseg)
			return (EFBIG);
		ss++;
		ss->ss_paddr = paddr;
		ss->ss_len = len;
		sg->sg_nseg++;
		*ssp = ss;
	}
	return (0);
}

/*
 * Worker routine to append a virtual address range (either kernel or
 * user) to a scatter/gather list.
 */
static __inline int
_sglist_append_buf(struct sglist *sg, void *buf, size_t len, pmap_t pmap,
    size_t *donep)
{
	struct sglist_seg *ss;
	vm_offset_t vaddr, offset;
	vm_paddr_t paddr;
	size_t seglen;
	int error;

	if (donep)
		*donep = 0;
	if (len == 0)
		return (0);

	/* Do the first page.  It may have an offset. */
	vaddr = (vm_offset_t)buf;
	offset = vaddr & PAGE_MASK;
	if (pmap != NULL)
		paddr = pmap_extract(pmap, vaddr);
	else
		paddr = pmap_kextract(vaddr);
	seglen = MIN(len, PAGE_SIZE - offset);
	if (sg->sg_nseg == 0) {
		ss = sg->sg_segs;
		ss->ss_paddr = paddr;
		ss->ss_len = seglen;
		sg->sg_nseg = 1;
	} else {
		ss = &sg->sg_segs[sg->sg_nseg - 1];
		error = _sglist_append_range(sg, &ss, paddr, seglen);
		if (error)
			return (error);
	}
	vaddr += seglen;
	len -= seglen;
	if (donep)
		*donep += seglen;

	while (len > 0) {
		seglen = MIN(len, PAGE_SIZE);
		if (pmap != NULL)
			paddr = pmap_extract(pmap, vaddr);
		else
			paddr = pmap_kextract(vaddr);
		error = _sglist_append_range(sg, &ss, paddr, seglen);
		if (error)
			return (error);
		vaddr += seglen;
		len -= seglen;
		if (donep)
			*donep += seglen;
	}

	return (0);
}

/*
 * Determine the number of scatter/gather list elements needed to
 * describe a kernel virtual address range.
 */
int
sglist_count(void *buf, size_t len)
{
	vm_offset_t vaddr, vendaddr;
	vm_paddr_t lastaddr, paddr;
	int nsegs;

	if (len == 0)
		return (0);

	vaddr = trunc_page((vm_offset_t)buf);
	vendaddr = (vm_offset_t)buf + len;
	nsegs = 1;
	lastaddr = pmap_kextract(vaddr);
	vaddr += PAGE_SIZE;
	while (vaddr < vendaddr) {
		paddr = pmap_kextract(vaddr);
		if (lastaddr + PAGE_SIZE != paddr)
			nsegs++;
		lastaddr = paddr;
		vaddr += PAGE_SIZE;
	}
	return (nsegs);
}

/*
 * Determine the number of scatter/gather list elements needed to
 * describe a buffer backed by an array of VM pages.
 */
int
sglist_count_vmpages(vm_page_t *m, size_t pgoff, size_t len)
{
	vm_paddr_t lastaddr, paddr;
	int i, nsegs;

	if (len == 0)
		return (0);

	len += pgoff;
	nsegs = 1;
	lastaddr = VM_PAGE_TO_PHYS(m[0]);
	for (i = 1; len > PAGE_SIZE; len -= PAGE_SIZE, i++) {
		paddr = VM_PAGE_TO_PHYS(m[i]);
		if (lastaddr + PAGE_SIZE != paddr)
			nsegs++;
		lastaddr = paddr;
	}
	return (nsegs);
}

/*
 * Allocate a scatter/gather list along with 'nsegs' segments.  The
 * 'mflags' parameters are the same as passed to malloc(9).  The caller
 * should use sglist_free() to free this list.
 */
struct sglist *
sglist_alloc(int nsegs, int mflags)
{
	struct sglist *sg;

	sg = malloc(sizeof(struct sglist) + nsegs * sizeof(struct sglist_seg),
	    M_SGLIST, mflags);
	if (sg == NULL)
		return (NULL);
	sglist_init(sg, nsegs, (struct sglist_seg *)(sg + 1));
	return (sg);
}

/*
 * Free a scatter/gather list allocated via sglist_allc().
 */
void
sglist_free(struct sglist *sg)
{

	if (sg == NULL)
		return;

	if (refcount_release(&sg->sg_refs))
		free(sg, M_SGLIST);
}

/*
 * Append the segments to describe a single kernel virtual address
 * range to a scatter/gather list.  If there are insufficient
 * segments, then this fails with EFBIG.
 */
int
sglist_append(struct sglist *sg, void *buf, size_t len)
{
	struct sgsave save;
	int error;

	if (sg->sg_maxseg == 0)
		return (EINVAL);
	SGLIST_SAVE(sg, save);
	error = _sglist_append_buf(sg, buf, len, NULL, NULL);
	if (error)
		SGLIST_RESTORE(sg, save);
	return (error);
}

/*
 * Append the segments to describe a bio's data to a scatter/gather list.
 * If there are insufficient segments, then this fails with EFBIG.
 *
 * NOTE: This function expects bio_bcount to be initialized.
 */
int
sglist_append_bio(struct sglist *sg, struct bio *bp)
{
	int error;

	if ((bp->bio_flags & BIO_UNMAPPED) == 0)
		error = sglist_append(sg, bp->bio_data, bp->bio_bcount);
	else
		error = sglist_append_vmpages(sg, bp->bio_ma,
		    bp->bio_ma_offset, bp->bio_bcount);
	return (error);
}

/*
 * Append a single physical address range to a scatter/gather list.
 * If there are insufficient segments, then this fails with EFBIG.
 */
int
sglist_append_phys(struct sglist *sg, vm_paddr_t paddr, size_t len)
{
	struct sglist_seg *ss;
	struct sgsave save;
	int error;

	if (sg->sg_maxseg == 0)
		return (EINVAL);
	if (len == 0)
		return (0);

	if (sg->sg_nseg == 0) {
		sg->sg_segs[0].ss_paddr = paddr;
		sg->sg_segs[0].ss_len = len;
		sg->sg_nseg = 1;
		return (0);
	}
	ss = &sg->sg_segs[sg->sg_nseg - 1];
	SGLIST_SAVE(sg, save);
	error = _sglist_append_range(sg, &ss, paddr, len);
	if (error)
		SGLIST_RESTORE(sg, save);
	return (error);
}

/*
 * Append the segments that describe a single mbuf chain to a
 * scatter/gather list.  If there are insufficient segments, then this
 * fails with EFBIG.
 */
int
sglist_append_mbuf(struct sglist *sg, struct mbuf *m0)
{
	struct sgsave save;
	struct mbuf *m;
	int error;

	if (sg->sg_maxseg == 0)
		return (EINVAL);

	error = 0;
	SGLIST_SAVE(sg, save);
	for (m = m0; m != NULL; m = m->m_next) {
		if (m->m_len > 0) {
			error = sglist_append(sg, m->m_data, m->m_len);
			if (error) {
				SGLIST_RESTORE(sg, save);
				return (error);
			}
		}
	}
	return (0);
}

/*
 * Append the segments that describe a buffer spanning an array of VM
 * pages.  The buffer begins at an offset of 'pgoff' in the first
 * page.
 */
int
sglist_append_vmpages(struct sglist *sg, vm_page_t *m, size_t pgoff,
    size_t len)
{
	struct sgsave save;
	struct sglist_seg *ss;
	vm_paddr_t paddr;
	size_t seglen;
	int error, i;

	if (sg->sg_maxseg == 0)
		return (EINVAL);
	if (len == 0)
		return (0);

	SGLIST_SAVE(sg, save);
	i = 0;
	if (sg->sg_nseg == 0) {
		seglen = min(PAGE_SIZE - pgoff, len);
		sg->sg_segs[0].ss_paddr = VM_PAGE_TO_PHYS(m[0]) + pgoff;
		sg->sg_segs[0].ss_len = seglen;
		sg->sg_nseg = 1;
		pgoff = 0;
		len -= seglen;
		i++;
	}
	ss = &sg->sg_segs[sg->sg_nseg - 1];
	for (; len > 0; i++, len -= seglen) {
		seglen = min(PAGE_SIZE - pgoff, len);
		paddr = VM_PAGE_TO_PHYS(m[i]) + pgoff;
		error = _sglist_append_range(sg, &ss, paddr, seglen);
		if (error) {
			SGLIST_RESTORE(sg, save);
			return (error);
		}
		pgoff = 0;
	}
	return (0);
}

/*
 * Append the segments that describe a single user address range to a
 * scatter/gather list.  If there are insufficient segments, then this
 * fails with EFBIG.
 */
int
sglist_append_user(struct sglist *sg, void *buf, size_t len, struct thread *td)
{
	struct sgsave save;
	int error;

	if (sg->sg_maxseg == 0)
		return (EINVAL);
	SGLIST_SAVE(sg, save);
	error = _sglist_append_buf(sg, buf, len,
	    vmspace_pmap(td->td_proc->p_vmspace), NULL);
	if (error)
		SGLIST_RESTORE(sg, save);
	return (error);
}

/*
 * Append a subset of an existing scatter/gather list 'source' to a
 * the scatter/gather list 'sg'.  If there are insufficient segments,
 * then this fails with EFBIG.
 */
int
sglist_append_sglist(struct sglist *sg, struct sglist *source, size_t offset,
    size_t length)
{
	struct sgsave save;
	struct sglist_seg *ss;
	size_t seglen;
	int error, i;

	if (sg->sg_maxseg == 0 || length == 0)
		return (EINVAL);
	SGLIST_SAVE(sg, save);
	error = EINVAL;
	ss = &sg->sg_segs[sg->sg_nseg - 1];
	for (i = 0; i < source->sg_nseg; i++) {
		if (offset >= source->sg_segs[i].ss_len) {
			offset -= source->sg_segs[i].ss_len;
			continue;
		}
		seglen = source->sg_segs[i].ss_len - offset;
		if (seglen > length)
			seglen = length;
		error = _sglist_append_range(sg, &ss,
		    source->sg_segs[i].ss_paddr + offset, seglen);
		if (error)
			break;
		offset = 0;
		length -= seglen;
		if (length == 0)
			break;
	}
	if (length != 0)
		error = EINVAL;
	if (error)
		SGLIST_RESTORE(sg, save);
	return (error);
}

/*
 * Append the segments that describe a single uio to a scatter/gather
 * list.  If there are insufficient segments, then this fails with
 * EFBIG.
 */
int
sglist_append_uio(struct sglist *sg, struct uio *uio)
{
	struct iovec *iov;
	struct sgsave save;
	size_t resid, minlen;
	pmap_t pmap;
	int error, i;

	if (sg->sg_maxseg == 0)
		return (EINVAL);

	resid = uio->uio_resid;
	iov = uio->uio_iov;

	if (uio->uio_segflg == UIO_USERSPACE) {
		KASSERT(uio->uio_td != NULL,
		    ("sglist_append_uio: USERSPACE but no thread"));
		pmap = vmspace_pmap(uio->uio_td->td_proc->p_vmspace);
	} else
		pmap = NULL;

	error = 0;
	SGLIST_SAVE(sg, save);
	for (i = 0; i < uio->uio_iovcnt && resid != 0; i++) {
		/*
		 * Now at the first iovec to load.  Load each iovec
		 * until we have exhausted the residual count.
		 */
		minlen = MIN(resid, iov[i].iov_len);
		if (minlen > 0) {
			error = _sglist_append_buf(sg, iov[i].iov_base, minlen,
			    pmap, NULL);
			if (error) {
				SGLIST_RESTORE(sg, save);
				return (error);
			}
			resid -= minlen;
		}
	}
	return (0);
}

/*
 * Append the segments that describe at most 'resid' bytes from a
 * single uio to a scatter/gather list.  If there are insufficient
 * segments, then only the amount that fits is appended.
 */
int
sglist_consume_uio(struct sglist *sg, struct uio *uio, size_t resid)
{
	struct iovec *iov;
	size_t done;
	pmap_t pmap;
	int error, len;

	if (sg->sg_maxseg == 0)
		return (EINVAL);

	if (uio->uio_segflg == UIO_USERSPACE) {
		KASSERT(uio->uio_td != NULL,
		    ("sglist_consume_uio: USERSPACE but no thread"));
		pmap = vmspace_pmap(uio->uio_td->td_proc->p_vmspace);
	} else
		pmap = NULL;

	error = 0;
	while (resid > 0 && uio->uio_resid) {
		iov = uio->uio_iov;
		len = iov->iov_len;
		if (len == 0) {
			uio->uio_iov++;
			uio->uio_iovcnt--;
			continue;
		}
		if (len > resid)
			len = resid;

		/*
		 * Try to append this iovec.  If we run out of room,
		 * then break out of the loop.
		 */
		error = _sglist_append_buf(sg, iov->iov_base, len, pmap, &done);
		iov->iov_base = (char *)iov->iov_base + done;
		iov->iov_len -= done;
		uio->uio_resid -= done;
		uio->uio_offset += done;
		resid -= done;
		if (error)
			break;
	}
	return (0);
}

/*
 * Allocate and populate a scatter/gather list to describe a single
 * kernel virtual address range.
 */
struct sglist *
sglist_build(void *buf, size_t len, int mflags)
{
	struct sglist *sg;
	int nsegs;

	if (len == 0)
		return (NULL);

	nsegs = sglist_count(buf, len);
	sg = sglist_alloc(nsegs, mflags);
	if (sg == NULL)
		return (NULL);
	if (sglist_append(sg, buf, len) != 0) {
		sglist_free(sg);
		return (NULL);
	}
	return (sg);
}

/*
 * Clone a new copy of a scatter/gather list.
 */
struct sglist *
sglist_clone(struct sglist *sg, int mflags)
{
	struct sglist *new;

	if (sg == NULL)
		return (NULL);
	new = sglist_alloc(sg->sg_maxseg, mflags);
	if (new == NULL)
		return (NULL);
	new->sg_nseg = sg->sg_nseg;
	bcopy(sg->sg_segs, new->sg_segs, sizeof(struct sglist_seg) *
	    sg->sg_nseg);
	return (new);
}

/*
 * Calculate the total length of the segments described in a
 * scatter/gather list.
 */
size_t
sglist_length(struct sglist *sg)
{
	size_t space;
	int i;

	space = 0;
	for (i = 0; i < sg->sg_nseg; i++)
		space += sg->sg_segs[i].ss_len;
	return (space);
}

/*
 * Split a scatter/gather list into two lists.  The scatter/gather
 * entries for the first 'length' bytes of the 'original' list are
 * stored in the '*head' list and are removed from 'original'.
 *
 * If '*head' is NULL, then a new list will be allocated using
 * 'mflags'.  If M_NOWAIT is specified and the allocation fails,
 * ENOMEM will be returned.
 *
 * If '*head' is not NULL, it should point to an empty sglist.  If it
 * does not have enough room for the remaining space, then EFBIG will
 * be returned.  If '*head' is not empty, then EINVAL will be
 * returned.
 *
 * If 'original' is shared (refcount > 1), then EDOOFUS will be
 * returned.
 */
int
sglist_split(struct sglist *original, struct sglist **head, size_t length,
    int mflags)
{
	struct sglist *sg;
	size_t space, split;
	int count, i;

	if (original->sg_refs > 1)
		return (EDOOFUS);

	/* Figure out how big of a sglist '*head' has to hold. */
	count = 0;
	space = 0;
	split = 0;
	for (i = 0; i < original->sg_nseg; i++) {
		space += original->sg_segs[i].ss_len;
		count++;
		if (space >= length) {
			/*
			 * If 'length' falls in the middle of a
			 * scatter/gather list entry, then 'split'
			 * holds how much of that entry will remain in
			 * 'original'.
			 */
			split = space - length;
			break;
		}
	}

	/* Nothing to do, so leave head empty. */
	if (count == 0)
		return (0);

	if (*head == NULL) {
		sg = sglist_alloc(count, mflags);
		if (sg == NULL)
			return (ENOMEM);
		*head = sg;
	} else {
		sg = *head;
		if (sg->sg_maxseg < count)
			return (EFBIG);
		if (sg->sg_nseg != 0)
			return (EINVAL);
	}

	/* Copy 'count' entries to 'sg' from 'original'. */
	bcopy(original->sg_segs, sg->sg_segs, count *
	    sizeof(struct sglist_seg));
	sg->sg_nseg = count;

	/*
	 * If we had to split a list entry, fixup the last entry in
	 * 'sg' and the new first entry in 'original'.  We also
	 * decrement 'count' by 1 since we will only be removing
	 * 'count - 1' segments from 'original' now.
	 */
	if (split != 0) {
		count--;
		sg->sg_segs[count].ss_len -= split;
		original->sg_segs[count].ss_paddr =
		    sg->sg_segs[count].ss_paddr + split;
		original->sg_segs[count].ss_len = split;
	}

	/* Trim 'count' entries from the front of 'original'. */
	original->sg_nseg -= count;
	bcopy(original->sg_segs + count, original->sg_segs, count *
	    sizeof(struct sglist_seg));
	return (0);
}

/*
 * Append the scatter/gather list elements in 'second' to the
 * scatter/gather list 'first'.  If there is not enough space in
 * 'first', EFBIG is returned.
 */
int
sglist_join(struct sglist *first, struct sglist *second)
{
	struct sglist_seg *flast, *sfirst;
	int append;

	/* If 'second' is empty, there is nothing to do. */
	if (second->sg_nseg == 0)
		return (0);

	/*
	 * If the first entry in 'second' can be appended to the last entry
	 * in 'first' then set append to '1'.
	 */
	append = 0;
	flast = &first->sg_segs[first->sg_nseg - 1];
	sfirst = &second->sg_segs[0];
	if (first->sg_nseg != 0 &&
	    flast->ss_paddr + flast->ss_len == sfirst->ss_paddr)
		append = 1;

	/* Make sure 'first' has enough room. */
	if (first->sg_nseg + second->sg_nseg - append > first->sg_maxseg)
		return (EFBIG);

	/* Merge last in 'first' and first in 'second' if needed. */
	if (append)
		flast->ss_len += sfirst->ss_len;

	/* Append new segments from 'second' to 'first'. */
	bcopy(first->sg_segs + first->sg_nseg, second->sg_segs + append,
	    (second->sg_nseg - append) * sizeof(struct sglist_seg));
	first->sg_nseg += second->sg_nseg - append;
	sglist_reset(second);
	return (0);
}

/*
 * Generate a new scatter/gather list from a range of an existing
 * scatter/gather list.  The 'offset' and 'length' parameters specify
 * the logical range of the 'original' list to extract.  If that range
 * is not a subset of the length of 'original', then EINVAL is
 * returned.  The new scatter/gather list is stored in '*slice'.
 *
 * If '*slice' is NULL, then a new list will be allocated using
 * 'mflags'.  If M_NOWAIT is specified and the allocation fails,
 * ENOMEM will be returned.
 *
 * If '*slice' is not NULL, it should point to an empty sglist.  If it
 * does not have enough room for the remaining space, then EFBIG will
 * be returned.  If '*slice' is not empty, then EINVAL will be
 * returned.
 */
int
sglist_slice(struct sglist *original, struct sglist **slice, size_t offset,
    size_t length, int mflags)
{
	struct sglist *sg;
	size_t space, end, foffs, loffs;
	int count, i, fseg;

	/* Nothing to do. */
	if (length == 0)
		return (0);

	/* Figure out how many segments '*slice' needs to have. */
	end = offset + length;
	space = 0;
	count = 0;
	fseg = 0;
	foffs = loffs = 0;
	for (i = 0; i < original->sg_nseg; i++) {
		space += original->sg_segs[i].ss_len;
		if (space > offset) {
			/*
			 * When we hit the first segment, store its index
			 * in 'fseg' and the offset into the first segment
			 * of 'offset' in 'foffs'.
			 */
			if (count == 0) {
				fseg = i;
				foffs = offset - (space -
				    original->sg_segs[i].ss_len);
				CTR1(KTR_DEV, "sglist_slice: foffs = %08lx",
				    foffs);
			}
			count++;

			/*
			 * When we hit the last segment, break out of
			 * the loop.  Store the amount of extra space
			 * at the end of this segment in 'loffs'.
			 */
			if (space >= end) {
				loffs = space - end;
				CTR1(KTR_DEV, "sglist_slice: loffs = %08lx",
				    loffs);
				break;
			}
		}
	}

	/* If we never hit 'end', then 'length' ran off the end, so fail. */
	if (space < end)
		return (EINVAL);

	if (*slice == NULL) {
		sg = sglist_alloc(count, mflags);
		if (sg == NULL)
			return (ENOMEM);
		*slice = sg;
	} else {
		sg = *slice;
		if (sg->sg_maxseg < count)
			return (EFBIG);
		if (sg->sg_nseg != 0)
			return (EINVAL);
	}

	/*
	 * Copy over 'count' segments from 'original' starting at
	 * 'fseg' to 'sg'.
	 */
	bcopy(original->sg_segs + fseg, sg->sg_segs,
	    count * sizeof(struct sglist_seg));
	sg->sg_nseg = count;

	/* Fixup first and last segments if needed. */
	if (foffs != 0) {
		sg->sg_segs[0].ss_paddr += foffs;
		sg->sg_segs[0].ss_len -= foffs;
		CTR2(KTR_DEV, "sglist_slice seg[0]: %08lx:%08lx",
		    (long)sg->sg_segs[0].ss_paddr, sg->sg_segs[0].ss_len);
	}
	if (loffs != 0) {
		sg->sg_segs[count - 1].ss_len -= loffs;
		CTR2(KTR_DEV, "sglist_slice seg[%d]: len %08x", count - 1,
		    sg->sg_segs[count - 1].ss_len);
	}
	return (0);
}
