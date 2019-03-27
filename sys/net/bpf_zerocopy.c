/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 Seccuris Inc.
 * All rights reserved.
 *
 * This software was developed by Robert N. M. Watson under contract to
 * Seccuris Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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

#include "opt_bpf.h"

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/sf_buf.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <machine/atomic.h>

#include <net/if.h>
#include <net/bpf.h>
#include <net/bpf_zerocopy.h>
#include <net/bpfdesc.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>
#include <vm/vm_map.h>
#include <vm/vm_page.h>

/*
 * Zero-copy buffer scheme for BPF: user space "donates" two buffers, which
 * are mapped into the kernel address space using sf_bufs and used directly
 * by BPF.  Memory is wired since page faults cannot be tolerated in the
 * contexts where the buffers are copied to (locks held, interrupt context,
 * etc).  Access to shared memory buffers is synchronized using a header on
 * each buffer, allowing the number of system calls to go to zero as BPF
 * reaches saturation (buffers filled as fast as they can be drained by the
 * user process).  Full details of the protocol for communicating between the
 * user process and BPF may be found in bpf(4).
 */

/*
 * Maximum number of pages per buffer.  Since all BPF devices use two, the
 * maximum per device is 2*BPF_MAX_PAGES.  Resource limits on the number of
 * sf_bufs may be an issue, so do not set this too high.  On older systems,
 * kernel address space limits may also be an issue.
 */
#define	BPF_MAX_PAGES	512

/*
 * struct zbuf describes a memory buffer loaned by a user process to the
 * kernel.  We represent this as a series of pages managed using an array of
 * sf_bufs.  Even though the memory is contiguous in user space, it may not
 * be mapped contiguously in the kernel (i.e., a set of physically
 * non-contiguous pages in the direct map region) so we must implement
 * scatter-gather copying.  One significant mitigating factor is that on
 * systems with a direct memory map, we can avoid TLB misses.
 *
 * At the front of the shared memory region is a bpf_zbuf_header, which
 * contains shared control data to allow user space and the kernel to
 * synchronize; this is included in zb_size, but not bpf_bufsize, so that BPF
 * knows that the space is not available.
 */
struct zbuf {
	vm_offset_t	 zb_uaddr;	/* User address at time of setup. */
	size_t		 zb_size;	/* Size of buffer, incl. header. */
	u_int		 zb_numpages;	/* Number of pages. */
	int		 zb_flags;	/* Flags on zbuf. */
	struct sf_buf	**zb_pages;	/* Pages themselves. */
	struct bpf_zbuf_header	*zb_header;	/* Shared header. */
};

/*
 * When a buffer has been assigned to userspace, flag it as such, as the
 * buffer may remain in the store position as a result of the user process
 * not yet having acknowledged the buffer in the hold position yet.
 */
#define	ZBUF_FLAG_ASSIGNED	0x00000001	/* Set when owned by user. */

/*
 * Release a page we've previously wired.
 */
static void
zbuf_page_free(vm_page_t pp)
{

	vm_page_lock(pp);
	if (vm_page_unwire(pp, PQ_INACTIVE) && pp->object == NULL)
		vm_page_free(pp);
	vm_page_unlock(pp);
}

/*
 * Free an sf_buf with attached page.
 */
static void
zbuf_sfbuf_free(struct sf_buf *sf)
{
	vm_page_t pp;

	pp = sf_buf_page(sf);
	sf_buf_free(sf);
	zbuf_page_free(pp);
}

/*
 * Free a zbuf, including its page array, sbufs, and pages.  Allow partially
 * allocated zbufs to be freed so that it may be used even during a zbuf
 * setup.
 */
static void
zbuf_free(struct zbuf *zb)
{
	int i;

	for (i = 0; i < zb->zb_numpages; i++) {
		if (zb->zb_pages[i] != NULL)
			zbuf_sfbuf_free(zb->zb_pages[i]);
	}
	free(zb->zb_pages, M_BPF);
	free(zb, M_BPF);
}

/*
 * Given a user pointer to a page of user memory, return an sf_buf for the
 * page.  Because we may be requesting quite a few sf_bufs, prefer failure to
 * deadlock and use SFB_NOWAIT.
 */
static struct sf_buf *
zbuf_sfbuf_get(struct vm_map *map, vm_offset_t uaddr)
{
	struct sf_buf *sf;
	vm_page_t pp;

	if (vm_fault_quick_hold_pages(map, uaddr, PAGE_SIZE, VM_PROT_READ |
	    VM_PROT_WRITE, &pp, 1) < 0)
		return (NULL);
	vm_page_lock(pp);
	vm_page_wire(pp);
	vm_page_unhold(pp);
	vm_page_unlock(pp);
	sf = sf_buf_alloc(pp, SFB_NOWAIT);
	if (sf == NULL) {
		zbuf_page_free(pp);
		return (NULL);
	}
	return (sf);
}

/*
 * Create a zbuf describing a range of user address space memory.  Validate
 * page alignment, size requirements, etc.
 */
static int
zbuf_setup(struct thread *td, vm_offset_t uaddr, size_t len,
    struct zbuf **zbp)
{
	struct zbuf *zb;
	struct vm_map *map;
	int error, i;

	*zbp = NULL;

	/*
	 * User address must be page-aligned.
	 */
	if (uaddr & PAGE_MASK)
		return (EINVAL);

	/*
	 * Length must be an integer number of full pages.
	 */
	if (len & PAGE_MASK)
		return (EINVAL);

	/*
	 * Length must not exceed per-buffer resource limit.
	 */
	if ((len / PAGE_SIZE) > BPF_MAX_PAGES)
		return (EINVAL);

	/*
	 * Allocate the buffer and set up each page with is own sf_buf.
	 */
	error = 0;
	zb = malloc(sizeof(*zb), M_BPF, M_ZERO | M_WAITOK);
	zb->zb_uaddr = uaddr;
	zb->zb_size = len;
	zb->zb_numpages = len / PAGE_SIZE;
	zb->zb_pages = malloc(sizeof(struct sf_buf *) *
	    zb->zb_numpages, M_BPF, M_ZERO | M_WAITOK);
	map = &td->td_proc->p_vmspace->vm_map;
	for (i = 0; i < zb->zb_numpages; i++) {
		zb->zb_pages[i] = zbuf_sfbuf_get(map,
		    uaddr + (i * PAGE_SIZE));
		if (zb->zb_pages[i] == NULL) {
			error = EFAULT;
			goto error;
		}
	}
	zb->zb_header =
	    (struct bpf_zbuf_header *)sf_buf_kva(zb->zb_pages[0]);
	bzero(zb->zb_header, sizeof(*zb->zb_header));
	*zbp = zb;
	return (0);

error:
	zbuf_free(zb);
	return (error);
}

/*
 * Copy bytes from a source into the specified zbuf.  The caller is
 * responsible for performing bounds checking, etc.
 */
void
bpf_zerocopy_append_bytes(struct bpf_d *d, caddr_t buf, u_int offset,
    void *src, u_int len)
{
	u_int count, page, poffset;
	u_char *src_bytes;
	struct zbuf *zb;

	KASSERT(d->bd_bufmode == BPF_BUFMODE_ZBUF,
	    ("bpf_zerocopy_append_bytes: not in zbuf mode"));
	KASSERT(buf != NULL, ("bpf_zerocopy_append_bytes: NULL buf"));

	src_bytes = (u_char *)src;
	zb = (struct zbuf *)buf;

	KASSERT((zb->zb_flags & ZBUF_FLAG_ASSIGNED) == 0,
	    ("bpf_zerocopy_append_bytes: ZBUF_FLAG_ASSIGNED"));

	/*
	 * Scatter-gather copy to user pages mapped into kernel address space
	 * using sf_bufs: copy up to a page at a time.
	 */
	offset += sizeof(struct bpf_zbuf_header);
	page = offset / PAGE_SIZE;
	poffset = offset % PAGE_SIZE;
	while (len > 0) {
		KASSERT(page < zb->zb_numpages, ("bpf_zerocopy_append_bytes:"
		   " page overflow (%d p %d np)\n", page, zb->zb_numpages));

		count = min(len, PAGE_SIZE - poffset);
		bcopy(src_bytes, ((u_char *)sf_buf_kva(zb->zb_pages[page])) +
		    poffset, count);
		poffset += count;
		if (poffset == PAGE_SIZE) {
			poffset = 0;
			page++;
		}
		KASSERT(poffset < PAGE_SIZE,
		    ("bpf_zerocopy_append_bytes: page offset overflow (%d)",
		    poffset));
		len -= count;
		src_bytes += count;
	}
}

/*
 * Copy bytes from an mbuf chain to the specified zbuf: copying will be
 * scatter-gather both from mbufs, which may be fragmented over memory, and
 * to pages, which may not be contiguously mapped in kernel address space.
 * As with bpf_zerocopy_append_bytes(), the caller is responsible for
 * checking that this will not exceed the buffer limit.
 */
void
bpf_zerocopy_append_mbuf(struct bpf_d *d, caddr_t buf, u_int offset,
    void *src, u_int len)
{
	u_int count, moffset, page, poffset;
	const struct mbuf *m;
	struct zbuf *zb;

	KASSERT(d->bd_bufmode == BPF_BUFMODE_ZBUF,
	    ("bpf_zerocopy_append_mbuf not in zbuf mode"));
	KASSERT(buf != NULL, ("bpf_zerocopy_append_mbuf: NULL buf"));

	m = (struct mbuf *)src;
	zb = (struct zbuf *)buf;

	KASSERT((zb->zb_flags & ZBUF_FLAG_ASSIGNED) == 0,
	    ("bpf_zerocopy_append_mbuf: ZBUF_FLAG_ASSIGNED"));

	/*
	 * Scatter gather both from an mbuf chain and to a user page set
	 * mapped into kernel address space using sf_bufs.  If we're lucky,
	 * each mbuf requires one copy operation, but if page alignment and
	 * mbuf alignment work out less well, we'll be doing two copies per
	 * mbuf.
	 */
	offset += sizeof(struct bpf_zbuf_header);
	page = offset / PAGE_SIZE;
	poffset = offset % PAGE_SIZE;
	moffset = 0;
	while (len > 0) {
		KASSERT(page < zb->zb_numpages,
		    ("bpf_zerocopy_append_mbuf: page overflow (%d p %d "
		    "np)\n", page, zb->zb_numpages));
		KASSERT(m != NULL,
		    ("bpf_zerocopy_append_mbuf: end of mbuf chain"));

		count = min(m->m_len - moffset, len);
		count = min(count, PAGE_SIZE - poffset);
		bcopy(mtod(m, u_char *) + moffset,
		    ((u_char *)sf_buf_kva(zb->zb_pages[page])) + poffset,
		    count);
		poffset += count;
		if (poffset == PAGE_SIZE) {
			poffset = 0;
			page++;
		}
		KASSERT(poffset < PAGE_SIZE,
		    ("bpf_zerocopy_append_mbuf: page offset overflow (%d)",
		    poffset));
		moffset += count;
		if (moffset == m->m_len) {
			m = m->m_next;
			moffset = 0;
		}
		len -= count;
	}
}

/*
 * Notification from the BPF framework that a buffer in the store position is
 * rejecting packets and may be considered full.  We mark the buffer as
 * immutable and assign to userspace so that it is immediately available for
 * the user process to access.
 */
void
bpf_zerocopy_buffull(struct bpf_d *d)
{
	struct zbuf *zb;

	KASSERT(d->bd_bufmode == BPF_BUFMODE_ZBUF,
	    ("bpf_zerocopy_buffull: not in zbuf mode"));

	zb = (struct zbuf *)d->bd_sbuf;
	KASSERT(zb != NULL, ("bpf_zerocopy_buffull: zb == NULL"));

	if ((zb->zb_flags & ZBUF_FLAG_ASSIGNED) == 0) {
		zb->zb_flags |= ZBUF_FLAG_ASSIGNED;
		zb->zb_header->bzh_kernel_len = d->bd_slen;
		atomic_add_rel_int(&zb->zb_header->bzh_kernel_gen, 1);
	}
}

/*
 * Notification from the BPF framework that a buffer has moved into the held
 * slot on a descriptor.  Zero-copy BPF will update the shared page to let
 * the user process know and flag the buffer as assigned if it hasn't already
 * been marked assigned due to filling while it was in the store position.
 *
 * Note: identical logic as in bpf_zerocopy_buffull(), except that we operate
 * on bd_hbuf and bd_hlen.
 */
void
bpf_zerocopy_bufheld(struct bpf_d *d)
{
	struct zbuf *zb;

	KASSERT(d->bd_bufmode == BPF_BUFMODE_ZBUF,
	    ("bpf_zerocopy_bufheld: not in zbuf mode"));

	zb = (struct zbuf *)d->bd_hbuf;
	KASSERT(zb != NULL, ("bpf_zerocopy_bufheld: zb == NULL"));

	if ((zb->zb_flags & ZBUF_FLAG_ASSIGNED) == 0) {
		zb->zb_flags |= ZBUF_FLAG_ASSIGNED;
		zb->zb_header->bzh_kernel_len = d->bd_hlen;
		atomic_add_rel_int(&zb->zb_header->bzh_kernel_gen, 1);
	}
}

/*
 * Notification from the BPF framework that the free buffer has been been
 * rotated out of the held position to the free position.  This happens when
 * the user acknowledges the held buffer.
 */
void
bpf_zerocopy_buf_reclaimed(struct bpf_d *d)
{
	struct zbuf *zb;

	KASSERT(d->bd_bufmode == BPF_BUFMODE_ZBUF,
	    ("bpf_zerocopy_reclaim_buf: not in zbuf mode"));

	KASSERT(d->bd_fbuf != NULL,
	    ("bpf_zerocopy_buf_reclaimed: NULL free buf"));
	zb = (struct zbuf *)d->bd_fbuf;
	zb->zb_flags &= ~ZBUF_FLAG_ASSIGNED;
}

/*
 * Query from the BPF framework regarding whether the buffer currently in the
 * held position can be moved to the free position, which can be indicated by
 * the user process making their generation number equal to the kernel
 * generation number.
 */
int
bpf_zerocopy_canfreebuf(struct bpf_d *d)
{
	struct zbuf *zb;

	KASSERT(d->bd_bufmode == BPF_BUFMODE_ZBUF,
	    ("bpf_zerocopy_canfreebuf: not in zbuf mode"));

	zb = (struct zbuf *)d->bd_hbuf;
	if (zb == NULL)
		return (0);
	if (zb->zb_header->bzh_kernel_gen ==
	    atomic_load_acq_int(&zb->zb_header->bzh_user_gen))
		return (1);
	return (0);
}

/*
 * Query from the BPF framework as to whether or not the buffer current in
 * the store position can actually be written to.  This may return false if
 * the store buffer is assigned to userspace before the hold buffer is
 * acknowledged.
 */
int
bpf_zerocopy_canwritebuf(struct bpf_d *d)
{
	struct zbuf *zb;

	KASSERT(d->bd_bufmode == BPF_BUFMODE_ZBUF,
	    ("bpf_zerocopy_canwritebuf: not in zbuf mode"));

	zb = (struct zbuf *)d->bd_sbuf;
	KASSERT(zb != NULL, ("bpf_zerocopy_canwritebuf: bd_sbuf NULL"));

	if (zb->zb_flags & ZBUF_FLAG_ASSIGNED)
		return (0);
	return (1);
}

/*
 * Free zero copy buffers at request of descriptor.
 */
void
bpf_zerocopy_free(struct bpf_d *d)
{
	struct zbuf *zb;

	KASSERT(d->bd_bufmode == BPF_BUFMODE_ZBUF,
	    ("bpf_zerocopy_free: not in zbuf mode"));

	zb = (struct zbuf *)d->bd_sbuf;
	if (zb != NULL)
		zbuf_free(zb);
	zb = (struct zbuf *)d->bd_hbuf;
	if (zb != NULL)
		zbuf_free(zb);
	zb = (struct zbuf *)d->bd_fbuf;
	if (zb != NULL)
		zbuf_free(zb);
}

/*
 * Ioctl to return the maximum buffer size.
 */
int
bpf_zerocopy_ioctl_getzmax(struct thread *td, struct bpf_d *d, size_t *i)
{

	KASSERT(d->bd_bufmode == BPF_BUFMODE_ZBUF,
	    ("bpf_zerocopy_ioctl_getzmax: not in zbuf mode"));

	*i = BPF_MAX_PAGES * PAGE_SIZE;
	return (0);
}

/*
 * Ioctl to force rotation of the two buffers, if there's any data available.
 * This can be used by user space to implement timeouts when waiting for a
 * buffer to fill.
 */
int
bpf_zerocopy_ioctl_rotzbuf(struct thread *td, struct bpf_d *d,
    struct bpf_zbuf *bz)
{
	struct zbuf *bzh;

	bzero(bz, sizeof(*bz));
	BPFD_LOCK(d);
	if (d->bd_hbuf == NULL && d->bd_slen != 0) {
		ROTATE_BUFFERS(d);
		bzh = (struct zbuf *)d->bd_hbuf;
		bz->bz_bufa = (void *)bzh->zb_uaddr;
		bz->bz_buflen = d->bd_hlen;
	}
	BPFD_UNLOCK(d);
	return (0);
}

/*
 * Ioctl to configure zero-copy buffers -- may be done only once.
 */
int
bpf_zerocopy_ioctl_setzbuf(struct thread *td, struct bpf_d *d,
    struct bpf_zbuf *bz)
{
	struct zbuf *zba, *zbb;
	int error;

	KASSERT(d->bd_bufmode == BPF_BUFMODE_ZBUF,
	    ("bpf_zerocopy_ioctl_setzbuf: not in zbuf mode"));

	/*
	 * Must set both buffers.  Cannot clear them.
	 */
	if (bz->bz_bufa == NULL || bz->bz_bufb == NULL)
		return (EINVAL);

	/*
	 * Buffers must have a size greater than 0.  Alignment and other size
	 * validity checking is done in zbuf_setup().
	 */
	if (bz->bz_buflen == 0)
		return (EINVAL);

	/*
	 * Allocate new buffers.
	 */
	error = zbuf_setup(td, (vm_offset_t)bz->bz_bufa, bz->bz_buflen,
	    &zba);
	if (error)
		return (error);
	error = zbuf_setup(td, (vm_offset_t)bz->bz_bufb, bz->bz_buflen,
	    &zbb);
	if (error) {
		zbuf_free(zba);
		return (error);
	}

	/*
	 * We only allow buffers to be installed once, so atomically check
	 * that no buffers are currently installed and install new buffers.
	 */
	BPFD_LOCK(d);
	if (d->bd_hbuf != NULL || d->bd_sbuf != NULL || d->bd_fbuf != NULL ||
	    d->bd_bif != NULL) {
		BPFD_UNLOCK(d);
		zbuf_free(zba);
		zbuf_free(zbb);
		return (EINVAL);
	}

	/*
	 * Point BPF descriptor at buffers; initialize sbuf as zba so that
	 * it is always filled first in the sequence, per bpf(4).
	 */
	d->bd_fbuf = (caddr_t)zbb;
	d->bd_sbuf = (caddr_t)zba;
	d->bd_slen = 0;
	d->bd_hlen = 0;

	/*
	 * We expose only the space left in the buffer after the size of the
	 * shared management region.
	 */
	d->bd_bufsize = bz->bz_buflen - sizeof(struct bpf_zbuf_header);
	BPFD_UNLOCK(d);
	return (0);
}
