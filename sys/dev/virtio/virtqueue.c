/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011, Bryan Venteicher <bryanv@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Implements the virtqueue interface as basically described
 * in the original VirtIO paper.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/sglist.h>
#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/cpu.h>
#include <machine/bus.h>
#include <machine/atomic.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <dev/virtio/virtio.h>
#include <dev/virtio/virtqueue.h>
#include <dev/virtio/virtio_ring.h>

#include "virtio_bus_if.h"

struct virtqueue {
	device_t		 vq_dev;
	char			 vq_name[VIRTQUEUE_MAX_NAME_SZ];
	uint16_t		 vq_queue_index;
	uint16_t		 vq_nentries;
	uint32_t		 vq_flags;
#define	VIRTQUEUE_FLAG_INDIRECT	 0x0001
#define	VIRTQUEUE_FLAG_EVENT_IDX 0x0002

	int			 vq_alignment;
	int			 vq_ring_size;
	void			*vq_ring_mem;
	int			 vq_max_indirect_size;
	int			 vq_indirect_mem_size;
	virtqueue_intr_t	*vq_intrhand;
	void			*vq_intrhand_arg;

	struct vring		 vq_ring;
	uint16_t		 vq_free_cnt;
	uint16_t		 vq_queued_cnt;
	/*
	 * Head of the free chain in the descriptor table. If
	 * there are no free descriptors, this will be set to
	 * VQ_RING_DESC_CHAIN_END.
	 */
	uint16_t		 vq_desc_head_idx;
	/*
	 * Last consumed descriptor in the used table,
	 * trails vq_ring.used->idx.
	 */
	uint16_t		 vq_used_cons_idx;

	struct vq_desc_extra {
		void		  *cookie;
		struct vring_desc *indirect;
		vm_paddr_t	   indirect_paddr;
		uint16_t	   ndescs;
	} vq_descx[0];
};

/*
 * The maximum virtqueue size is 2^15. Use that value as the end of
 * descriptor chain terminator since it will never be a valid index
 * in the descriptor table. This is used to verify we are correctly
 * handling vq_free_cnt.
 */
#define VQ_RING_DESC_CHAIN_END 32768

#define VQASSERT(_vq, _exp, _msg, ...)				\
    KASSERT((_exp),("%s: %s - "_msg, __func__, (_vq)->vq_name,	\
	##__VA_ARGS__))

#define VQ_RING_ASSERT_VALID_IDX(_vq, _idx)			\
    VQASSERT((_vq), (_idx) < (_vq)->vq_nentries,		\
	"invalid ring index: %d, max: %d", (_idx),		\
	(_vq)->vq_nentries)

#define VQ_RING_ASSERT_CHAIN_TERM(_vq)				\
    VQASSERT((_vq), (_vq)->vq_desc_head_idx ==			\
	VQ_RING_DESC_CHAIN_END,	"full ring terminated "		\
	"incorrectly: head idx: %d", (_vq)->vq_desc_head_idx)

static int	virtqueue_init_indirect(struct virtqueue *vq, int);
static void	virtqueue_free_indirect(struct virtqueue *vq);
static void	virtqueue_init_indirect_list(struct virtqueue *,
		    struct vring_desc *);

static void	vq_ring_init(struct virtqueue *);
static void	vq_ring_update_avail(struct virtqueue *, uint16_t);
static uint16_t	vq_ring_enqueue_segments(struct virtqueue *,
		    struct vring_desc *, uint16_t, struct sglist *, int, int);
static int	vq_ring_use_indirect(struct virtqueue *, int);
static void	vq_ring_enqueue_indirect(struct virtqueue *, void *,
		    struct sglist *, int, int);
static int	vq_ring_enable_interrupt(struct virtqueue *, uint16_t);
static int	vq_ring_must_notify_host(struct virtqueue *);
static void	vq_ring_notify_host(struct virtqueue *);
static void	vq_ring_free_chain(struct virtqueue *, uint16_t);

uint64_t
virtqueue_filter_features(uint64_t features)
{
	uint64_t mask;

	mask = (1 << VIRTIO_TRANSPORT_F_START) - 1;
	mask |= VIRTIO_RING_F_INDIRECT_DESC;
	mask |= VIRTIO_RING_F_EVENT_IDX;

	return (features & mask);
}

int
virtqueue_alloc(device_t dev, uint16_t queue, uint16_t size, int align,
    vm_paddr_t highaddr, struct vq_alloc_info *info, struct virtqueue **vqp)
{
	struct virtqueue *vq;
	int error;

	*vqp = NULL;
	error = 0;

	if (size == 0) {
		device_printf(dev,
		    "virtqueue %d (%s) does not exist (size is zero)\n",
		    queue, info->vqai_name);
		return (ENODEV);
	} else if (!powerof2(size)) {
		device_printf(dev,
		    "virtqueue %d (%s) size is not a power of 2: %d\n",
		    queue, info->vqai_name, size);
		return (ENXIO);
	} else if (info->vqai_maxindirsz > VIRTIO_MAX_INDIRECT) {
		device_printf(dev, "virtqueue %d (%s) requested too many "
		    "indirect descriptors: %d, max %d\n",
		    queue, info->vqai_name, info->vqai_maxindirsz,
		    VIRTIO_MAX_INDIRECT);
		return (EINVAL);
	}

	vq = malloc(sizeof(struct virtqueue) +
	    size * sizeof(struct vq_desc_extra), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (vq == NULL) {
		device_printf(dev, "cannot allocate virtqueue\n");
		return (ENOMEM);
	}

	vq->vq_dev = dev;
	strlcpy(vq->vq_name, info->vqai_name, sizeof(vq->vq_name));
	vq->vq_queue_index = queue;
	vq->vq_alignment = align;
	vq->vq_nentries = size;
	vq->vq_free_cnt = size;
	vq->vq_intrhand = info->vqai_intr;
	vq->vq_intrhand_arg = info->vqai_intr_arg;

	if (VIRTIO_BUS_WITH_FEATURE(dev, VIRTIO_RING_F_EVENT_IDX) != 0)
		vq->vq_flags |= VIRTQUEUE_FLAG_EVENT_IDX;

	if (info->vqai_maxindirsz > 1) {
		error = virtqueue_init_indirect(vq, info->vqai_maxindirsz);
		if (error)
			goto fail;
	}

	vq->vq_ring_size = round_page(vring_size(size, align));
	vq->vq_ring_mem = contigmalloc(vq->vq_ring_size, M_DEVBUF,
	    M_NOWAIT | M_ZERO, 0, highaddr, PAGE_SIZE, 0);
	if (vq->vq_ring_mem == NULL) {
		device_printf(dev,
		    "cannot allocate memory for virtqueue ring\n");
		error = ENOMEM;
		goto fail;
	}

	vq_ring_init(vq);
	virtqueue_disable_intr(vq);

	*vqp = vq;

fail:
	if (error)
		virtqueue_free(vq);

	return (error);
}

static int
virtqueue_init_indirect(struct virtqueue *vq, int indirect_size)
{
	device_t dev;
	struct vq_desc_extra *dxp;
	int i, size;

	dev = vq->vq_dev;

	if (VIRTIO_BUS_WITH_FEATURE(dev, VIRTIO_RING_F_INDIRECT_DESC) == 0) {
		/*
		 * Indirect descriptors requested by the driver but not
		 * negotiated. Return zero to keep the initialization
		 * going: we'll run fine without.
		 */
		if (bootverbose)
			device_printf(dev, "virtqueue %d (%s) requested "
			    "indirect descriptors but not negotiated\n",
			    vq->vq_queue_index, vq->vq_name);
		return (0);
	}

	size = indirect_size * sizeof(struct vring_desc);
	vq->vq_max_indirect_size = indirect_size;
	vq->vq_indirect_mem_size = size;
	vq->vq_flags |= VIRTQUEUE_FLAG_INDIRECT;

	for (i = 0; i < vq->vq_nentries; i++) {
		dxp = &vq->vq_descx[i];

		dxp->indirect = malloc(size, M_DEVBUF, M_NOWAIT);
		if (dxp->indirect == NULL) {
			device_printf(dev, "cannot allocate indirect list\n");
			return (ENOMEM);
		}

		dxp->indirect_paddr = vtophys(dxp->indirect);
		virtqueue_init_indirect_list(vq, dxp->indirect);
	}

	return (0);
}

static void
virtqueue_free_indirect(struct virtqueue *vq)
{
	struct vq_desc_extra *dxp;
	int i;

	for (i = 0; i < vq->vq_nentries; i++) {
		dxp = &vq->vq_descx[i];

		if (dxp->indirect == NULL)
			break;

		free(dxp->indirect, M_DEVBUF);
		dxp->indirect = NULL;
		dxp->indirect_paddr = 0;
	}

	vq->vq_flags &= ~VIRTQUEUE_FLAG_INDIRECT;
	vq->vq_indirect_mem_size = 0;
}

static void
virtqueue_init_indirect_list(struct virtqueue *vq,
    struct vring_desc *indirect)
{
	int i;

	bzero(indirect, vq->vq_indirect_mem_size);

	for (i = 0; i < vq->vq_max_indirect_size - 1; i++)
		indirect[i].next = i + 1;
	indirect[i].next = VQ_RING_DESC_CHAIN_END;
}

int
virtqueue_reinit(struct virtqueue *vq, uint16_t size)
{
	struct vq_desc_extra *dxp;
	int i;

	if (vq->vq_nentries != size) {
		device_printf(vq->vq_dev,
		    "%s: '%s' changed size; old=%hu, new=%hu\n",
		    __func__, vq->vq_name, vq->vq_nentries, size);
		return (EINVAL);
	}

	/* Warn if the virtqueue was not properly cleaned up. */
	if (vq->vq_free_cnt != vq->vq_nentries) {
		device_printf(vq->vq_dev,
		    "%s: warning '%s' virtqueue not empty, "
		    "leaking %d entries\n", __func__, vq->vq_name,
		    vq->vq_nentries - vq->vq_free_cnt);
	}

	vq->vq_desc_head_idx = 0;
	vq->vq_used_cons_idx = 0;
	vq->vq_queued_cnt = 0;
	vq->vq_free_cnt = vq->vq_nentries;

	/* To be safe, reset all our allocated memory. */
	bzero(vq->vq_ring_mem, vq->vq_ring_size);
	for (i = 0; i < vq->vq_nentries; i++) {
		dxp = &vq->vq_descx[i];
		dxp->cookie = NULL;
		dxp->ndescs = 0;
		if (vq->vq_flags & VIRTQUEUE_FLAG_INDIRECT)
			virtqueue_init_indirect_list(vq, dxp->indirect);
	}

	vq_ring_init(vq);
	virtqueue_disable_intr(vq);

	return (0);
}

void
virtqueue_free(struct virtqueue *vq)
{

	if (vq->vq_free_cnt != vq->vq_nentries) {
		device_printf(vq->vq_dev, "%s: freeing non-empty virtqueue, "
		    "leaking %d entries\n", vq->vq_name,
		    vq->vq_nentries - vq->vq_free_cnt);
	}

	if (vq->vq_flags & VIRTQUEUE_FLAG_INDIRECT)
		virtqueue_free_indirect(vq);

	if (vq->vq_ring_mem != NULL) {
		contigfree(vq->vq_ring_mem, vq->vq_ring_size, M_DEVBUF);
		vq->vq_ring_size = 0;
		vq->vq_ring_mem = NULL;
	}

	free(vq, M_DEVBUF);
}

vm_paddr_t
virtqueue_paddr(struct virtqueue *vq)
{

	return (vtophys(vq->vq_ring_mem));
}

vm_paddr_t
virtqueue_desc_paddr(struct virtqueue *vq)
{

	return (vtophys(vq->vq_ring.desc));
}

vm_paddr_t
virtqueue_avail_paddr(struct virtqueue *vq)
{

	return (vtophys(vq->vq_ring.avail));
}

vm_paddr_t
virtqueue_used_paddr(struct virtqueue *vq)
{

	return (vtophys(vq->vq_ring.used));
}

uint16_t
virtqueue_index(struct virtqueue *vq)
{
	return (vq->vq_queue_index);
}

int
virtqueue_size(struct virtqueue *vq)
{

	return (vq->vq_nentries);
}

int
virtqueue_nfree(struct virtqueue *vq)
{

	return (vq->vq_free_cnt);
}

int
virtqueue_empty(struct virtqueue *vq)
{

	return (vq->vq_nentries == vq->vq_free_cnt);
}

int
virtqueue_full(struct virtqueue *vq)
{

	return (vq->vq_free_cnt == 0);
}

void
virtqueue_notify(struct virtqueue *vq)
{

	/* Ensure updated avail->idx is visible to host. */
	mb();

	if (vq_ring_must_notify_host(vq))
		vq_ring_notify_host(vq);
	vq->vq_queued_cnt = 0;
}

int
virtqueue_nused(struct virtqueue *vq)
{
	uint16_t used_idx, nused;

	used_idx = vq->vq_ring.used->idx;

	nused = (uint16_t)(used_idx - vq->vq_used_cons_idx);
	VQASSERT(vq, nused <= vq->vq_nentries, "used more than available");

	return (nused);
}

int
virtqueue_intr_filter(struct virtqueue *vq)
{

	if (vq->vq_used_cons_idx == vq->vq_ring.used->idx)
		return (0);

	virtqueue_disable_intr(vq);

	return (1);
}

void
virtqueue_intr(struct virtqueue *vq)
{

	vq->vq_intrhand(vq->vq_intrhand_arg);
}

int
virtqueue_enable_intr(struct virtqueue *vq)
{

	return (vq_ring_enable_interrupt(vq, 0));
}

int
virtqueue_postpone_intr(struct virtqueue *vq, vq_postpone_t hint)
{
	uint16_t ndesc, avail_idx;

	avail_idx = vq->vq_ring.avail->idx;
	ndesc = (uint16_t)(avail_idx - vq->vq_used_cons_idx);

	switch (hint) {
	case VQ_POSTPONE_SHORT:
		ndesc = ndesc / 4;
		break;
	case VQ_POSTPONE_LONG:
		ndesc = (ndesc * 3) / 4;
		break;
	case VQ_POSTPONE_EMPTIED:
		break;
	}

	return (vq_ring_enable_interrupt(vq, ndesc));
}

/*
 * Note this is only considered a hint to the host.
 */
void
virtqueue_disable_intr(struct virtqueue *vq)
{

	if (vq->vq_flags & VIRTQUEUE_FLAG_EVENT_IDX) {
		vring_used_event(&vq->vq_ring) = vq->vq_used_cons_idx -
		    vq->vq_nentries - 1;
	} else
		vq->vq_ring.avail->flags |= VRING_AVAIL_F_NO_INTERRUPT;
}

int
virtqueue_enqueue(struct virtqueue *vq, void *cookie, struct sglist *sg,
    int readable, int writable)
{
	struct vq_desc_extra *dxp;
	int needed;
	uint16_t head_idx, idx;

	needed = readable + writable;

	VQASSERT(vq, cookie != NULL, "enqueuing with no cookie");
	VQASSERT(vq, needed == sg->sg_nseg,
	    "segment count mismatch, %d, %d", needed, sg->sg_nseg);
	VQASSERT(vq,
	    needed <= vq->vq_nentries || needed <= vq->vq_max_indirect_size,
	    "too many segments to enqueue: %d, %d/%d", needed,
	    vq->vq_nentries, vq->vq_max_indirect_size);

	if (needed < 1)
		return (EINVAL);
	if (vq->vq_free_cnt == 0)
		return (ENOSPC);

	if (vq_ring_use_indirect(vq, needed)) {
		vq_ring_enqueue_indirect(vq, cookie, sg, readable, writable);
		return (0);
	} else if (vq->vq_free_cnt < needed)
		return (EMSGSIZE);

	head_idx = vq->vq_desc_head_idx;
	VQ_RING_ASSERT_VALID_IDX(vq, head_idx);
	dxp = &vq->vq_descx[head_idx];

	VQASSERT(vq, dxp->cookie == NULL,
	    "cookie already exists for index %d", head_idx);
	dxp->cookie = cookie;
	dxp->ndescs = needed;

	idx = vq_ring_enqueue_segments(vq, vq->vq_ring.desc, head_idx,
	    sg, readable, writable);

	vq->vq_desc_head_idx = idx;
	vq->vq_free_cnt -= needed;
	if (vq->vq_free_cnt == 0)
		VQ_RING_ASSERT_CHAIN_TERM(vq);
	else
		VQ_RING_ASSERT_VALID_IDX(vq, idx);

	vq_ring_update_avail(vq, head_idx);

	return (0);
}

void *
virtqueue_dequeue(struct virtqueue *vq, uint32_t *len)
{
	struct vring_used_elem *uep;
	void *cookie;
	uint16_t used_idx, desc_idx;

	if (vq->vq_used_cons_idx == vq->vq_ring.used->idx)
		return (NULL);

	used_idx = vq->vq_used_cons_idx++ & (vq->vq_nentries - 1);
	uep = &vq->vq_ring.used->ring[used_idx];

	rmb();
	desc_idx = (uint16_t) uep->id;
	if (len != NULL)
		*len = uep->len;

	vq_ring_free_chain(vq, desc_idx);

	cookie = vq->vq_descx[desc_idx].cookie;
	VQASSERT(vq, cookie != NULL, "no cookie for index %d", desc_idx);
	vq->vq_descx[desc_idx].cookie = NULL;

	return (cookie);
}

void *
virtqueue_poll(struct virtqueue *vq, uint32_t *len)
{
	void *cookie;

	VIRTIO_BUS_POLL(vq->vq_dev);
	while ((cookie = virtqueue_dequeue(vq, len)) == NULL) {
		cpu_spinwait();
		VIRTIO_BUS_POLL(vq->vq_dev);
	}

	return (cookie);
}

void *
virtqueue_drain(struct virtqueue *vq, int *last)
{
	void *cookie;
	int idx;

	cookie = NULL;
	idx = *last;

	while (idx < vq->vq_nentries && cookie == NULL) {
		if ((cookie = vq->vq_descx[idx].cookie) != NULL) {
			vq->vq_descx[idx].cookie = NULL;
			/* Free chain to keep free count consistent. */
			vq_ring_free_chain(vq, idx);
		}
		idx++;
	}

	*last = idx;

	return (cookie);
}

void
virtqueue_dump(struct virtqueue *vq)
{

	if (vq == NULL)
		return;

	printf("VQ: %s - size=%d; free=%d; used=%d; queued=%d; "
	    "desc_head_idx=%d; avail.idx=%d; used_cons_idx=%d; "
	    "used.idx=%d; used_event_idx=%d; avail.flags=0x%x; used.flags=0x%x\n",
	    vq->vq_name, vq->vq_nentries, vq->vq_free_cnt,
	    virtqueue_nused(vq), vq->vq_queued_cnt, vq->vq_desc_head_idx,
	    vq->vq_ring.avail->idx, vq->vq_used_cons_idx,
	    vq->vq_ring.used->idx,
		vring_used_event(&vq->vq_ring),
	    vq->vq_ring.avail->flags,
	    vq->vq_ring.used->flags);
}

static void
vq_ring_init(struct virtqueue *vq)
{
	struct vring *vr;
	char *ring_mem;
	int i, size;

	ring_mem = vq->vq_ring_mem;
	size = vq->vq_nentries;
	vr = &vq->vq_ring;

	vring_init(vr, size, ring_mem, vq->vq_alignment);

	for (i = 0; i < size - 1; i++)
		vr->desc[i].next = i + 1;
	vr->desc[i].next = VQ_RING_DESC_CHAIN_END;
}

static void
vq_ring_update_avail(struct virtqueue *vq, uint16_t desc_idx)
{
	uint16_t avail_idx;

	/*
	 * Place the head of the descriptor chain into the next slot and make
	 * it usable to the host. The chain is made available now rather than
	 * deferring to virtqueue_notify() in the hopes that if the host is
	 * currently running on another CPU, we can keep it processing the new
	 * descriptor.
	 */
	avail_idx = vq->vq_ring.avail->idx & (vq->vq_nentries - 1);
	vq->vq_ring.avail->ring[avail_idx] = desc_idx;

	wmb();
	vq->vq_ring.avail->idx++;

	/* Keep pending count until virtqueue_notify(). */
	vq->vq_queued_cnt++;
}

static uint16_t
vq_ring_enqueue_segments(struct virtqueue *vq, struct vring_desc *desc,
    uint16_t head_idx, struct sglist *sg, int readable, int writable)
{
	struct sglist_seg *seg;
	struct vring_desc *dp;
	int i, needed;
	uint16_t idx;

	needed = readable + writable;

	for (i = 0, idx = head_idx, seg = sg->sg_segs;
	     i < needed;
	     i++, idx = dp->next, seg++) {
		VQASSERT(vq, idx != VQ_RING_DESC_CHAIN_END,
		    "premature end of free desc chain");

		dp = &desc[idx];
		dp->addr = seg->ss_paddr;
		dp->len = seg->ss_len;
		dp->flags = 0;

		if (i < needed - 1)
			dp->flags |= VRING_DESC_F_NEXT;
		if (i >= readable)
			dp->flags |= VRING_DESC_F_WRITE;
	}

	return (idx);
}

static int
vq_ring_use_indirect(struct virtqueue *vq, int needed)
{

	if ((vq->vq_flags & VIRTQUEUE_FLAG_INDIRECT) == 0)
		return (0);

	if (vq->vq_max_indirect_size < needed)
		return (0);

	if (needed < 2)
		return (0);

	return (1);
}

static void
vq_ring_enqueue_indirect(struct virtqueue *vq, void *cookie,
    struct sglist *sg, int readable, int writable)
{
	struct vring_desc *dp;
	struct vq_desc_extra *dxp;
	int needed;
	uint16_t head_idx;

	needed = readable + writable;
	VQASSERT(vq, needed <= vq->vq_max_indirect_size,
	    "enqueuing too many indirect descriptors");

	head_idx = vq->vq_desc_head_idx;
	VQ_RING_ASSERT_VALID_IDX(vq, head_idx);
	dp = &vq->vq_ring.desc[head_idx];
	dxp = &vq->vq_descx[head_idx];

	VQASSERT(vq, dxp->cookie == NULL,
	    "cookie already exists for index %d", head_idx);
	dxp->cookie = cookie;
	dxp->ndescs = 1;

	dp->addr = dxp->indirect_paddr;
	dp->len = needed * sizeof(struct vring_desc);
	dp->flags = VRING_DESC_F_INDIRECT;

	vq_ring_enqueue_segments(vq, dxp->indirect, 0,
	    sg, readable, writable);

	vq->vq_desc_head_idx = dp->next;
	vq->vq_free_cnt--;
	if (vq->vq_free_cnt == 0)
		VQ_RING_ASSERT_CHAIN_TERM(vq);
	else
		VQ_RING_ASSERT_VALID_IDX(vq, vq->vq_desc_head_idx);

	vq_ring_update_avail(vq, head_idx);
}

static int
vq_ring_enable_interrupt(struct virtqueue *vq, uint16_t ndesc)
{

	/*
	 * Enable interrupts, making sure we get the latest index of
	 * what's already been consumed.
	 */
	if (vq->vq_flags & VIRTQUEUE_FLAG_EVENT_IDX)
		vring_used_event(&vq->vq_ring) = vq->vq_used_cons_idx + ndesc;
	else
		vq->vq_ring.avail->flags &= ~VRING_AVAIL_F_NO_INTERRUPT;

	mb();

	/*
	 * Enough items may have already been consumed to meet our threshold
	 * since we last checked. Let our caller know so it processes the new
	 * entries.
	 */
	if (virtqueue_nused(vq) > ndesc)
		return (1);

	return (0);
}

static int
vq_ring_must_notify_host(struct virtqueue *vq)
{
	uint16_t new_idx, prev_idx, event_idx;

	if (vq->vq_flags & VIRTQUEUE_FLAG_EVENT_IDX) {
		new_idx = vq->vq_ring.avail->idx;
		prev_idx = new_idx - vq->vq_queued_cnt;
		event_idx = vring_avail_event(&vq->vq_ring);

		return (vring_need_event(event_idx, new_idx, prev_idx) != 0);
	}

	return ((vq->vq_ring.used->flags & VRING_USED_F_NO_NOTIFY) == 0);
}

static void
vq_ring_notify_host(struct virtqueue *vq)
{

	VIRTIO_BUS_NOTIFY_VQ(vq->vq_dev, vq->vq_queue_index);
}

static void
vq_ring_free_chain(struct virtqueue *vq, uint16_t desc_idx)
{
	struct vring_desc *dp;
	struct vq_desc_extra *dxp;

	VQ_RING_ASSERT_VALID_IDX(vq, desc_idx);
	dp = &vq->vq_ring.desc[desc_idx];
	dxp = &vq->vq_descx[desc_idx];

	if (vq->vq_free_cnt == 0)
		VQ_RING_ASSERT_CHAIN_TERM(vq);

	vq->vq_free_cnt += dxp->ndescs;
	dxp->ndescs--;

	if ((dp->flags & VRING_DESC_F_INDIRECT) == 0) {
		while (dp->flags & VRING_DESC_F_NEXT) {
			VQ_RING_ASSERT_VALID_IDX(vq, dp->next);
			dp = &vq->vq_ring.desc[dp->next];
			dxp->ndescs--;
		}
	}

	VQASSERT(vq, dxp->ndescs == 0,
	    "failed to free entire desc chain, remaining: %d", dxp->ndescs);

	/*
	 * We must append the existing free chain, if any, to the end of
	 * newly freed chain. If the virtqueue was completely used, then
	 * head would be VQ_RING_DESC_CHAIN_END (ASSERTed above).
	 */
	dp->next = vq->vq_desc_head_idx;
	vq->vq_desc_head_idx = desc_idx;
}
