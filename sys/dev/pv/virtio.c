/*	$OpenBSD: virtio.c,v 1.39 2025/09/16 12:18:10 hshoexer Exp $	*/
/*	$NetBSD: virtio.c,v 1.3 2011/11/02 23:05:52 njoly Exp $	*/

/*
 * Copyright (c) 2012 Stefan Fritsch, Alexander Fiveg.
 * Copyright (c) 2010 Minoura Makoto.
 * All rights reserved.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/atomic.h>
#include <sys/malloc.h>

#include <dev/pv/virtioreg.h>
#include <dev/pv/virtiovar.h>

#if VIRTIO_DEBUG
#define VIRTIO_ASSERT(x)	KASSERT(x)
#else
#define VIRTIO_ASSERT(x)
#endif

void		 virtio_init_vq(struct virtio_softc *,
				struct virtqueue *);
void		 vq_free_entry(struct virtqueue *, struct vq_entry *);
struct vq_entry	*vq_alloc_entry(struct virtqueue *);

struct cfdriver virtio_cd = {
	NULL, "virtio", DV_DULL, CD_COCOVM
};

static const char * const virtio_device_name[] = {
	"Unknown (0)",		/* 0 */
	"Network",		/* 1 */
	"Block",		/* 2 */
	"Console",		/* 3 */
	"Entropy",		/* 4 */
	"Memory Balloon",	/* 5 */
	"IO Memory",		/* 6 */
	"Rpmsg",		/* 7 */
	"SCSI host",		/* 8 */
	"9P Transport",		/* 9 */
	"mac80211 wlan",	/* 10 */
	NULL,			/* 11 */
	NULL,			/* 12 */
	NULL,			/* 13 */
	NULL,			/* 14 */
	NULL,			/* 15 */
	"GPU",			/* 16 */
};
#define NDEVNAMES	(sizeof(virtio_device_name)/sizeof(char*))

const char *
virtio_device_string(int id)
{
	return id < NDEVNAMES ? virtio_device_name[id] : "Unknown";
}

#if VIRTIO_DEBUG
static const struct virtio_feature_name transport_feature_names[] = {
	{ VIRTIO_F_NOTIFY_ON_EMPTY,	"NotifyOnEmpty"},
	{ VIRTIO_F_ANY_LAYOUT,		"AnyLayout"},
	{ VIRTIO_F_RING_INDIRECT_DESC,	"RingIndirectDesc"},
	{ VIRTIO_F_RING_EVENT_IDX,	"RingEventIdx"},
	{ VIRTIO_F_BAD_FEATURE,		"BadFeature"},
	{ VIRTIO_F_VERSION_1,		"Version1"},
	{ VIRTIO_F_ACCESS_PLATFORM,	"AccessPlatf"},
	{ VIRTIO_F_RING_PACKED,		"RingPacked"},
	{ VIRTIO_F_IN_ORDER,		"InOrder"},
	{ VIRTIO_F_ORDER_PLATFORM,	"OrderPlatf"},
	{ VIRTIO_F_SR_IOV,		"SrIov"},
	{ VIRTIO_F_NOTIFICATION_DATA,	"NotifData"},
	{ VIRTIO_F_NOTIF_CONFIG_DATA,	"NotifConfData"},
	{ VIRTIO_F_RING_RESET,		"RingReset"},
	{ 0,				NULL}
};

void
virtio_log_features(uint64_t host, uint64_t neg,
    const struct virtio_feature_name *guest_feature_names)
{
	const struct virtio_feature_name *namep;
	int i;
	char c;
	uint64_t bit;

	for (i = 0; i < 64; i++) {
		if (i == 30) {
			/*
			 * VIRTIO_F_BAD_FEATURE is only used for
			 * checking correct negotiation
			 */
			continue;
		}
		bit = 1ULL << i;
		if ((host&bit) == 0)
			continue;
		namep = guest_feature_names;
		while (namep->bit && namep->bit != bit)
			namep++;
		if (namep->name == NULL) {
			namep = transport_feature_names;
			while (namep->bit && namep->bit != bit)
				namep++;
		}
		c = (neg&bit) ? '+' : '-';
		if (namep->name)
			printf(" %c%s", c, namep->name);
		else
			printf(" %cUnknown(%d)", c, i);
	}
}
#endif

/*
 * Reset the device.
 */
/*
 * To reset the device to a known state, do following:
 *	virtio_reset(sc);	     // this will stop the device activity
 *	<dequeue finished requests>; // virtio_dequeue() still can be called
 *	<revoke pending requests in the vqs if any>;
 *	virtio_reinit_start(sc);     // dequeue prohibited
 *	<some other initialization>;
 *	virtio_reinit_end(sc);	     // device activated; enqueue allowed
 * Once attached, features are assumed to not change again.
 */
void
virtio_reset(struct virtio_softc *sc)
{
	virtio_device_reset(sc);
	sc->sc_active_features = 0;
}

int
virtio_attach_finish(struct virtio_softc *sc, struct virtio_attach_args *va)
{
	int i, ret;

	ret = sc->sc_ops->attach_finish(sc, va);
	if (ret != 0)
		return ret;

	sc->sc_ops->setup_intrs(sc);
	for (i = 0; i < sc->sc_nvqs; i++) {
		struct virtqueue *vq = &sc->sc_vqs[i];

		if (vq->vq_num == 0)
			continue;
		virtio_setup_queue(sc, vq, vq->vq_dmamap->dm_segs[0].ds_addr);
	}
	virtio_set_status(sc, VIRTIO_CONFIG_DEVICE_STATUS_DRIVER_OK);
	return 0;
}

void
virtio_reinit_start(struct virtio_softc *sc)
{
	int i;

	virtio_set_status(sc, VIRTIO_CONFIG_DEVICE_STATUS_ACK);
	virtio_set_status(sc, VIRTIO_CONFIG_DEVICE_STATUS_DRIVER);
	virtio_negotiate_features(sc, NULL);
	sc->sc_ops->setup_intrs(sc);
	for (i = 0; i < sc->sc_nvqs; i++) {
		int n;
		struct virtqueue *vq = &sc->sc_vqs[i];
		if (vq->vq_num == 0)	/* not used */
			continue;
		n = virtio_read_queue_size(sc, vq->vq_index);
		if (n != vq->vq_num) {
			panic("%s: virtqueue size changed, vq index %d",
			    sc->sc_dev.dv_xname, vq->vq_index);
		}
		virtio_init_vq(sc, vq);
		virtio_setup_queue(sc, vq, vq->vq_dmamap->dm_segs[0].ds_addr);
	}
}

void
virtio_reinit_end(struct virtio_softc *sc)
{
	virtio_set_status(sc, VIRTIO_CONFIG_DEVICE_STATUS_DRIVER_OK);
}

/*
 * dmamap sync operations for a virtqueue.
 *
 * XXX These should be more fine grained. Syncing the whole ring if we
 * XXX only need a few bytes is inefficient if we use bounce buffers.
 */
static inline void
vq_sync_descs(struct virtio_softc *sc, struct virtqueue *vq, int ops)
{
	/* availoffset == sizeof(vring_desc)*vq_num */
	bus_dmamap_sync(sc->sc_dmat, vq->vq_dmamap, 0, vq->vq_availoffset,
	    ops);
}

static inline void
vq_sync_aring(struct virtio_softc *sc, struct virtqueue *vq, int ops)
{
	bus_dmamap_sync(sc->sc_dmat, vq->vq_dmamap, vq->vq_availoffset,
	    offsetof(struct vring_avail, ring) + vq->vq_num * sizeof(uint16_t),
	    ops);
}

static inline void
vq_sync_aring_used_event(struct virtio_softc *sc, struct virtqueue *vq, int ops)
{
	bus_dmamap_sync(sc->sc_dmat, vq->vq_dmamap, vq->vq_availoffset +
	    offsetof(struct vring_avail, ring) + vq->vq_num * sizeof(uint16_t),
	    sizeof(uint16_t), ops);
}


static inline void
vq_sync_uring(struct virtio_softc *sc, struct virtqueue *vq, int ops)
{
	bus_dmamap_sync(sc->sc_dmat, vq->vq_dmamap, vq->vq_usedoffset,
	    offsetof(struct vring_used, ring) + vq->vq_num *
	    sizeof(struct vring_used_elem), ops);
}

static inline void
vq_sync_uring_avail_event(struct virtio_softc *sc, struct virtqueue *vq, int ops)
{
	bus_dmamap_sync(sc->sc_dmat, vq->vq_dmamap,
	    vq->vq_usedoffset + offsetof(struct vring_used, ring) +
	    vq->vq_num * sizeof(struct vring_used_elem), sizeof(uint16_t),
	    ops);
}


static inline void
vq_sync_indirect(struct virtio_softc *sc, struct virtqueue *vq, int slot,
    int ops)
{
	int offset = vq->vq_indirectoffset +
	    sizeof(struct vring_desc) * vq->vq_maxnsegs * slot;

	bus_dmamap_sync(sc->sc_dmat, vq->vq_dmamap, offset,
	    sizeof(struct vring_desc) * vq->vq_maxnsegs, ops);
}

/*
 * Scan vq, bus_dmamap_sync for the vqs (not for the payload),
 * and calls (*vq_done)() if some entries are consumed.
 * For use in transport specific irq handlers.
 */
int
virtio_check_vqs(struct virtio_softc *sc)
{
	int i, r = 0;

	/* going backwards is better for if_vio */
	for (i = sc->sc_nvqs - 1; i >= 0; i--) {
		if (sc->sc_vqs[i].vq_num == 0)	/* not used */
			continue;
		r |= virtio_check_vq(sc, &sc->sc_vqs[i]);
	}

	return r;
}

int
virtio_check_vq(struct virtio_softc *sc, struct virtqueue *vq)
{
	if (vq->vq_queued) {
		vq->vq_queued = 0;
		vq_sync_aring(sc, vq, BUS_DMASYNC_POSTWRITE);
	}
	vq_sync_uring(sc, vq, BUS_DMASYNC_POSTREAD);
	if (vq->vq_used_idx != vq->vq_used->idx) {
		if (vq->vq_done)
			return (vq->vq_done)(vq);
	}

	return 0;
}

/*
 * Initialize vq structure.
 */
void
virtio_init_vq(struct virtio_softc *sc, struct virtqueue *vq)
{
	int i, j;
	int vq_size = vq->vq_num;

	VIRTIO_ASSERT(vq_size > 0);
	memset(vq->vq_vaddr, 0, vq->vq_bytesize);

	/* build the indirect descriptor chain */
	if (vq->vq_indirect != NULL) {
		struct vring_desc *vd;

		for (i = 0; i < vq_size; i++) {
			vd = vq->vq_indirect;
			vd += vq->vq_maxnsegs * i;
			for (j = 0; j < vq->vq_maxnsegs-1; j++)
				vd[j].next = j + 1;
		}
	}

	/* free slot management */
	SLIST_INIT(&vq->vq_freelist);
	/*
	 * virtio_enqueue_trim needs monotonely raising entries, therefore
	 * initialize in reverse order
	 */
	for (i = vq_size - 1; i >= 0; i--) {
		SLIST_INSERT_HEAD(&vq->vq_freelist, &vq->vq_entries[i],
		    qe_list);
		vq->vq_entries[i].qe_index = i;
	}

	bus_dmamap_sync(sc->sc_dmat, vq->vq_dmamap, 0, vq->vq_bytesize,
	    BUS_DMASYNC_PREWRITE);
	/* enqueue/dequeue status */
	vq->vq_avail_idx = 0;
	vq->vq_used_idx = 0;
	vq_sync_uring(sc, vq, BUS_DMASYNC_PREREAD);
	vq->vq_queued = 1;
}

/*
 * Allocate/free a vq.
 *
 * maxnsegs denotes how much space should be allocated for indirect
 * descriptors. maxnsegs == 1 can be used to disable use indirect
 * descriptors for this queue.
 */
int
virtio_alloc_vq(struct virtio_softc *sc, struct virtqueue *vq, int index,
    int maxnsegs, const char *name)
{
	int vq_size, allocsize1, allocsize2, allocsize3, allocsize = 0;
	int rsegs, r, hdrlen;
#define VIRTQUEUE_ALIGN(n)	(((n)+(VIRTIO_PAGE_SIZE-1))&	\
				 ~(VIRTIO_PAGE_SIZE-1))

	memset(vq, 0, sizeof(*vq));

	vq_size = virtio_read_queue_size(sc, index);
	if (vq_size == 0) {
		printf("virtqueue not exist, index %d for %s\n", index, name);
		goto err;
	}
	if (((vq_size - 1) & vq_size) != 0)
		panic("vq_size not power of two: %d", vq_size);

	hdrlen = virtio_has_feature(sc, VIRTIO_F_RING_EVENT_IDX) ? 3 : 2;

	/* allocsize1: descriptor table + avail ring + pad */
	allocsize1 = VIRTQUEUE_ALIGN(sizeof(struct vring_desc) * vq_size
	    + sizeof(uint16_t) * (hdrlen + vq_size));
	/* allocsize2: used ring + pad */
	allocsize2 = VIRTQUEUE_ALIGN(sizeof(uint16_t) * hdrlen
	    + sizeof(struct vring_used_elem) * vq_size);
	/* allocsize3: indirect table */
	if (sc->sc_indirect && maxnsegs > 1)
		allocsize3 = sizeof(struct vring_desc) * maxnsegs * vq_size;
	else
		allocsize3 = 0;
	allocsize = allocsize1 + allocsize2 + allocsize3;

	/*
	 * alloc and map the memory
	 *
	 * With virtio 0.9, the ring memory must be in the lowest 2^32
	 * pages. For simplicity, we use this limit even for virtio 1.0.
	 */
	r = bus_dmamem_alloc_range(sc->sc_dmat, allocsize, VIRTIO_PAGE_SIZE, 0,
	    &vq->vq_segs[0], 1, &rsegs, BUS_DMA_NOWAIT, 0,
	    ((uint64_t)VIRTIO_PAGE_SIZE << 32) - 1);
	if (r != 0) {
		printf("virtqueue %d for %s allocation failed, error %d\n",
		       index, name, r);
		goto err;
	}
	r = bus_dmamem_map(sc->sc_dmat, &vq->vq_segs[0], 1, allocsize,
	    (caddr_t*)&vq->vq_vaddr, BUS_DMA_NOWAIT);
	if (r != 0) {
		printf("virtqueue %d for %s map failed, error %d\n", index,
		    name, r);
		goto err;
	}
	r = bus_dmamap_create(sc->sc_dmat, allocsize, 1, allocsize, 0,
	    BUS_DMA_NOWAIT | BUS_DMA_64BIT, &vq->vq_dmamap);
	if (r != 0) {
		printf("virtqueue %d for %s dmamap creation failed, "
		    "error %d\n", index, name, r);
		goto err;
	}
	r = bus_dmamap_load(sc->sc_dmat, vq->vq_dmamap, vq->vq_vaddr,
	    allocsize, NULL, BUS_DMA_NOWAIT);
	if (r != 0) {
		printf("virtqueue %d for %s dmamap load failed, error %d\n",
		    index, name, r);
		goto err;
	}

	/* remember addresses and offsets for later use */
	vq->vq_owner = sc;
	vq->vq_num = vq_size;
	vq->vq_mask = vq_size - 1;
	vq->vq_index = index;
	vq->vq_desc = vq->vq_vaddr;
	vq->vq_availoffset = sizeof(struct vring_desc)*vq_size;
	vq->vq_avail = (struct vring_avail*)(((char*)vq->vq_desc) +
	    vq->vq_availoffset);
	vq->vq_usedoffset = allocsize1;
	vq->vq_used = (struct vring_used*)(((char*)vq->vq_desc) +
	    vq->vq_usedoffset);
	if (allocsize3 > 0) {
		vq->vq_indirectoffset = allocsize1 + allocsize2;
		vq->vq_indirect = (void*)(((char*)vq->vq_desc)
		    + vq->vq_indirectoffset);
	}
	vq->vq_bytesize = allocsize;
	vq->vq_maxnsegs = maxnsegs;

	/* free slot management */
	vq->vq_entries = mallocarray(vq_size, sizeof(struct vq_entry),
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (vq->vq_entries == NULL) {
		r = ENOMEM;
		goto err;
	}

	virtio_init_vq(sc, vq);

#if VIRTIO_DEBUG
	printf("\nallocated %u byte for virtqueue %d for %s, size %d\n",
	    allocsize, index, name, vq_size);
	if (allocsize3 > 0)
		printf("using %d byte (%d entries) indirect descriptors\n",
		    allocsize3, maxnsegs * vq_size);
#endif
	return 0;

err:
	if (vq->vq_dmamap)
		bus_dmamap_destroy(sc->sc_dmat, vq->vq_dmamap);
	if (vq->vq_vaddr)
		bus_dmamem_unmap(sc->sc_dmat, vq->vq_vaddr, allocsize);
	if (vq->vq_segs[0].ds_addr)
		bus_dmamem_free(sc->sc_dmat, &vq->vq_segs[0], 1);
	memset(vq, 0, sizeof(*vq));

	return -1;
}

int
virtio_free_vq(struct virtio_softc *sc, struct virtqueue *vq)
{
	struct vq_entry *qe;
	int i = 0;

	if (vq->vq_num == 0) {
		/* virtio_alloc_vq() was never called */
		return 0;
	}

	/* device must be already deactivated */
	/* confirm the vq is empty */
	SLIST_FOREACH(qe, &vq->vq_freelist, qe_list) {
		i++;
	}
	if (i != vq->vq_num) {
		printf("%s: freeing non-empty vq, index %d\n",
		    sc->sc_dev.dv_xname, vq->vq_index);
		return EBUSY;
	}

	/* tell device that there's no virtqueue any longer */
	virtio_setup_queue(sc, vq, 0);

	free(vq->vq_entries, M_DEVBUF, 0);
	bus_dmamap_unload(sc->sc_dmat, vq->vq_dmamap);
	bus_dmamap_destroy(sc->sc_dmat, vq->vq_dmamap);
	bus_dmamem_unmap(sc->sc_dmat, vq->vq_vaddr, vq->vq_bytesize);
	bus_dmamem_free(sc->sc_dmat, &vq->vq_segs[0], 1);
	memset(vq, 0, sizeof(*vq));

	return 0;
}

/*
 * Free descriptor management.
 */
struct vq_entry *
vq_alloc_entry(struct virtqueue *vq)
{
	struct vq_entry *qe;

	if (SLIST_EMPTY(&vq->vq_freelist))
		return NULL;
	qe = SLIST_FIRST(&vq->vq_freelist);
	SLIST_REMOVE_HEAD(&vq->vq_freelist, qe_list);

	return qe;
}

void
vq_free_entry(struct virtqueue *vq, struct vq_entry *qe)
{
	SLIST_INSERT_HEAD(&vq->vq_freelist, qe, qe_list);
}

/*
 * Enqueue several dmamaps as a single request.
 */
/*
 * Typical usage:
 *  <queue size> number of followings are stored in arrays
 *  - command blocks (in dmamem) should be pre-allocated and mapped
 *  - dmamaps for command blocks should be pre-allocated and loaded
 *  - dmamaps for payload should be pre-allocated
 *	r = virtio_enqueue_prep(sc, vq, &slot);		// allocate a slot
 *	if (r)		// currently 0 or EAGAIN
 *	  return r;
 *	r = bus_dmamap_load(dmat, dmamap_payload[slot], data, count, ..);
 *	if (r) {
 *	  virtio_enqueue_abort(sc, vq, slot);
 *	  bus_dmamap_unload(dmat, dmamap_payload[slot]);
 *	  return r;
 *	}
 *	r = virtio_enqueue_reserve(sc, vq, slot,
 *				   dmamap_payload[slot]->dm_nsegs+1);
 *							// ^ +1 for command
 *	if (r) {	// currently 0 or EAGAIN
 *	  bus_dmamap_unload(dmat, dmamap_payload[slot]);
 *	  return r;					// do not call abort()
 *	}
 *	<setup and prepare commands>
 *	bus_dmamap_sync(dmat, dmamap_cmd[slot],... BUS_DMASYNC_PREWRITE);
 *	bus_dmamap_sync(dmat, dmamap_payload[slot],...);
 *	virtio_enqueue(sc, vq, slot, dmamap_cmd[slot], 0);
 *	virtio_enqueue(sc, vq, slot, dmamap_payload[slot], iswrite);
 *	virtio_enqueue_commit(sc, vq, slot, 1);
 *
 * Alternative usage with statically allocated slots:
 *	<during initialization>
 *	// while not out of slots, do
 *	virtio_enqueue_prep(sc, vq, &slot);		// allocate a slot
 *	virtio_enqueue_reserve(sc, vq, slot, max_segs);	// reserve all slots
 *						that may ever be needed
 *
 *	<when enqueuing a request>
 *	// Don't call virtio_enqueue_prep()
 *	bus_dmamap_load(dmat, dmamap_payload[slot], data, count, ..);
 *	bus_dmamap_sync(dmat, dmamap_cmd[slot],... BUS_DMASYNC_PREWRITE);
 *	bus_dmamap_sync(dmat, dmamap_payload[slot],...);
 *	virtio_enqueue_trim(sc, vq, slot, num_segs_needed);
 *	virtio_enqueue(sc, vq, slot, dmamap_cmd[slot], 0);
 *	virtio_enqueue(sc, vq, slot, dmamap_payload[slot], iswrite);
 *	virtio_enqueue_commit(sc, vq, slot, 1);
 *
 *	<when dequeuing>
 *	// don't call virtio_dequeue_commit()
 */

/*
 * enqueue_prep: allocate a slot number
 */
int
virtio_enqueue_prep(struct virtqueue *vq, int *slotp)
{
	struct vq_entry *qe1;

	VIRTIO_ASSERT(slotp != NULL);

	qe1 = vq_alloc_entry(vq);
	if (qe1 == NULL)
		return EAGAIN;
	/* next slot is not allocated yet */
	qe1->qe_next = -1;
	*slotp = qe1->qe_index;

	return 0;
}

/*
 * enqueue_reserve: allocate remaining slots and build the descriptor chain.
 * Calls virtio_enqueue_abort() on failure.
 */
int
virtio_enqueue_reserve(struct virtqueue *vq, int slot, int nsegs)
{
	struct vq_entry *qe1 = &vq->vq_entries[slot];

	VIRTIO_ASSERT(qe1->qe_next == -1);
	VIRTIO_ASSERT(1 <= nsegs && nsegs <= vq->vq_num);

	if (vq->vq_indirect != NULL && nsegs > 1 && nsegs <= vq->vq_maxnsegs) {
		struct vring_desc *vd;
		int i;

		qe1->qe_indirect = 1;

		vd = &vq->vq_desc[qe1->qe_index];
		vd->addr = vq->vq_dmamap->dm_segs[0].ds_addr +
		    vq->vq_indirectoffset;
		vd->addr += sizeof(struct vring_desc) * vq->vq_maxnsegs *
		    qe1->qe_index;
		vd->len = sizeof(struct vring_desc) * nsegs;
		vd->flags = VRING_DESC_F_INDIRECT;

		vd = vq->vq_indirect;
		vd += vq->vq_maxnsegs * qe1->qe_index;
		qe1->qe_desc_base = vd;

		for (i = 0; i < nsegs-1; i++)
			vd[i].flags = VRING_DESC_F_NEXT;
		vd[i].flags = 0;
		qe1->qe_next = 0;

		return 0;
	} else {
		struct vring_desc *vd;
		struct vq_entry *qe;
		int i, s;

		qe1->qe_indirect = 0;

		vd = &vq->vq_desc[0];
		qe1->qe_desc_base = vd;
		qe1->qe_next = qe1->qe_index;
		s = slot;
		for (i = 0; i < nsegs - 1; i++) {
			qe = vq_alloc_entry(vq);
			if (qe == NULL) {
				vd[s].flags = 0;
				virtio_enqueue_abort(vq, slot);
				return EAGAIN;
			}
			vd[s].flags = VRING_DESC_F_NEXT;
			vd[s].next = qe->qe_index;
			s = qe->qe_index;
		}
		vd[s].flags = 0;

		return 0;
	}
}

/*
 * enqueue: enqueue a single dmamap.
 */
int
virtio_enqueue(struct virtqueue *vq, int slot, bus_dmamap_t dmamap, int write)
{
	struct vq_entry *qe1 = &vq->vq_entries[slot];
	struct vring_desc *vd = qe1->qe_desc_base;
	int i;
	int s = qe1->qe_next;

	VIRTIO_ASSERT(s >= 0);
	VIRTIO_ASSERT(dmamap->dm_nsegs > 0);
	if (dmamap->dm_nsegs > vq->vq_maxnsegs) {
#if VIRTIO_DEBUG
		for (i = 0; i < dmamap->dm_nsegs; i++) {
			printf(" %d (%d): %p %lx \n", i, write,
			    (void *)dmamap->dm_segs[i].ds_addr,
			    dmamap->dm_segs[i].ds_len);
		}
#endif
		panic("dmamap->dm_nseg %d > vq->vq_maxnsegs %d",
		    dmamap->dm_nsegs, vq->vq_maxnsegs);
	}

	for (i = 0; i < dmamap->dm_nsegs; i++) {
		vd[s].addr = dmamap->dm_segs[i].ds_addr;
		vd[s].len = dmamap->dm_segs[i].ds_len;
		if (!write)
			vd[s].flags |= VRING_DESC_F_WRITE;
		s = vd[s].next;
	}
	qe1->qe_next = s;

	return 0;
}

int
virtio_enqueue_p(struct virtqueue *vq, int slot, bus_dmamap_t dmamap,
    bus_addr_t start, bus_size_t len, int write)
{
	struct vq_entry *qe1 = &vq->vq_entries[slot];
	struct vring_desc *vd = qe1->qe_desc_base;
	int s = qe1->qe_next;

	VIRTIO_ASSERT(s >= 0);
	/* XXX todo: handle more segments */
	VIRTIO_ASSERT(dmamap->dm_nsegs == 1);
	VIRTIO_ASSERT((dmamap->dm_segs[0].ds_len > start) &&
	    (dmamap->dm_segs[0].ds_len >= start + len));

	vd[s].addr = dmamap->dm_segs[0].ds_addr + start;
	vd[s].len = len;
	if (!write)
		vd[s].flags |= VRING_DESC_F_WRITE;
	qe1->qe_next = vd[s].next;

	return 0;
}

static void
publish_avail_idx(struct virtio_softc *sc, struct virtqueue *vq)
{
	/* first make sure the avail ring entries are visible to the device */
	vq_sync_aring(sc, vq, BUS_DMASYNC_PREWRITE);

	virtio_membar_producer();
	vq->vq_avail->idx = vq->vq_avail_idx;
	/* make the avail idx visible to the device */
	vq_sync_aring(sc, vq, BUS_DMASYNC_PREWRITE);
	vq->vq_queued = 1;
}

/*
 * enqueue_commit: add it to the aring.
 */
void
virtio_enqueue_commit(struct virtio_softc *sc, struct virtqueue *vq, int slot,
    int notifynow)
{
	struct vq_entry *qe1;

	if (slot < 0)
		goto notify;
	vq_sync_descs(sc, vq, BUS_DMASYNC_PREWRITE);
	qe1 = &vq->vq_entries[slot];
	if (qe1->qe_indirect)
		vq_sync_indirect(sc, vq, slot, BUS_DMASYNC_PREWRITE);
	vq->vq_avail->ring[(vq->vq_avail_idx++) & vq->vq_mask] = slot;

notify:
	if (notifynow) {
		if (virtio_has_feature(vq->vq_owner, VIRTIO_F_RING_EVENT_IDX)) {
			uint16_t o = vq->vq_avail->idx;
			uint16_t n = vq->vq_avail_idx;
			uint16_t t;
			publish_avail_idx(sc, vq);

			virtio_membar_sync();
			vq_sync_uring_avail_event(sc, vq, BUS_DMASYNC_POSTREAD);
			t = VQ_AVAIL_EVENT(vq) + 1;
			if ((uint16_t)(n - t) < (uint16_t)(n - o))
				sc->sc_ops->kick(sc, vq->vq_index);
		} else {
			publish_avail_idx(sc, vq);

			virtio_membar_sync();
			vq_sync_uring(sc, vq, BUS_DMASYNC_POSTREAD);
			if (!(vq->vq_used->flags & VRING_USED_F_NO_NOTIFY))
				sc->sc_ops->kick(sc, vq->vq_index);
		}
	}
}

/*
 * enqueue_abort: rollback.
 */
int
virtio_enqueue_abort(struct virtqueue *vq, int slot)
{
	struct vq_entry *qe = &vq->vq_entries[slot];
	struct vring_desc *vd;
	int s;

	if (qe->qe_next < 0) {
		vq_free_entry(vq, qe);
		return 0;
	}

	s = slot;
	vd = &vq->vq_desc[0];
	while (vd[s].flags & VRING_DESC_F_NEXT) {
		s = vd[s].next;
		vq_free_entry(vq, qe);
		qe = &vq->vq_entries[s];
	}
	vq_free_entry(vq, qe);
	return 0;
}

/*
 * enqueue_trim: adjust buffer size to given # of segments, a.k.a.
 * descriptors.
 */
void
virtio_enqueue_trim(struct virtqueue *vq, int slot, int nsegs)
{
	struct vq_entry *qe1 = &vq->vq_entries[slot];
	struct vring_desc *vd = &vq->vq_desc[0];
	int i;

	if ((vd[slot].flags & VRING_DESC_F_INDIRECT) == 0) {
		qe1->qe_next = qe1->qe_index;
		/*
		 * N.B.: the vq_entries are ASSUMED to be a contiguous
		 *       block with slot being the index to the first one.
		 */
	} else {
		qe1->qe_next = 0;
		vd = &vq->vq_desc[qe1->qe_index];
		vd->len = sizeof(struct vring_desc) * nsegs;
		vd = qe1->qe_desc_base;
		slot = 0;
	}

	for (i = 0; i < nsegs -1 ; i++) {
		vd[slot].flags = VRING_DESC_F_NEXT;
		slot++;
	}
	vd[slot].flags = 0;
}

/*
 * Dequeue a request.
 */
/*
 * dequeue: dequeue a request from uring; bus_dmamap_sync for uring must
 * 	    already have been done, usually by virtio_check_vq()
 * 	    in the interrupt handler. This means that polling virtio_dequeue()
 * 	    repeatedly until it returns 0 does not work.
 */
int
virtio_dequeue(struct virtio_softc *sc, struct virtqueue *vq,
    int *slotp, int *lenp)
{
	uint16_t slot, usedidx;
	struct vq_entry *qe;

	if (vq->vq_used_idx == vq->vq_used->idx)
		return ENOENT;
	usedidx = vq->vq_used_idx++;
	usedidx &= vq->vq_mask;

	virtio_membar_consumer();
	vq_sync_uring(sc, vq, BUS_DMASYNC_POSTREAD);
	slot = vq->vq_used->ring[usedidx].id;
	qe = &vq->vq_entries[slot];

	if (qe->qe_indirect)
		vq_sync_indirect(sc, vq, slot, BUS_DMASYNC_POSTWRITE);

	if (slotp)
		*slotp = slot;
	if (lenp)
		*lenp = vq->vq_used->ring[usedidx].len;

	return 0;
}

/*
 * dequeue_commit: complete dequeue; the slot is recycled for future use.
 *                 if you forget to call this the slot will be leaked.
 *
 *                 Don't call this if you use statically allocated slots
 *                 and virtio_enqueue_trim().
 *
 *                 returns the number of freed slots.
 */
int
virtio_dequeue_commit(struct virtqueue *vq, int slot)
{
	struct vq_entry *qe = &vq->vq_entries[slot];
	struct vring_desc *vd = &vq->vq_desc[0];
	int s = slot, r = 1;

	while (vd[s].flags & VRING_DESC_F_NEXT) {
		s = vd[s].next;
		vq_free_entry(vq, qe);
		qe = &vq->vq_entries[s];
		r++;
	}
	vq_free_entry(vq, qe);

	return r;
}

/*
 * Increase the event index in order to delay interrupts.
 * Returns 0 on success; returns 1 if the used ring has already advanced
 * too far, and the caller must process the queue again (otherwise, no
 * more interrupts will happen).
 */
int
virtio_postpone_intr(struct virtqueue *vq, uint16_t nslots)
{
	uint16_t	idx;

	idx = vq->vq_used_idx + nslots;

	/* set the new event index: avail_ring->used_event = idx */
	VQ_USED_EVENT(vq) = idx;
	virtio_membar_sync();

	vq_sync_aring_used_event(vq->vq_owner, vq, BUS_DMASYNC_PREWRITE);
	vq->vq_queued++;

	if (nslots < virtio_nused(vq))
		return 1;

	return 0;
}

/*
 * Postpone interrupt until 3/4 of the available descriptors have been
 * consumed.
 */
int
virtio_postpone_intr_smart(struct virtqueue *vq)
{
	uint16_t	nslots;

	nslots = (uint16_t)(vq->vq_avail->idx - vq->vq_used_idx) * 3 / 4;

	return virtio_postpone_intr(vq, nslots);
}

/*
 * Postpone interrupt until all of the available descriptors have been
 * consumed.
 */
int
virtio_postpone_intr_far(struct virtqueue *vq)
{
	uint16_t	nslots;

	nslots = (uint16_t)(vq->vq_avail->idx - vq->vq_used_idx);

	return virtio_postpone_intr(vq, nslots);
}


/*
 * Start/stop vq interrupt.  No guarantee.
 */
void
virtio_stop_vq_intr(struct virtio_softc *sc, struct virtqueue *vq)
{
	if (virtio_has_feature(sc, VIRTIO_F_RING_EVENT_IDX)) {
		/*
		 * No way to disable the interrupt completely with
		 * RingEventIdx. Instead advance used_event by half
		 * the possible value. This won't happen soon and
		 * is far enough in the past to not trigger a spurious
		 * interrupt.
		 */
		VQ_USED_EVENT(vq) = vq->vq_used_idx + 0x8000;
		vq_sync_aring_used_event(sc, vq, BUS_DMASYNC_PREWRITE);
	} else {
		vq->vq_avail->flags |= VRING_AVAIL_F_NO_INTERRUPT;
	}
	vq_sync_aring(sc, vq, BUS_DMASYNC_PREWRITE);
	vq->vq_queued++;
}

int
virtio_start_vq_intr(struct virtio_softc *sc, struct virtqueue *vq)
{
	/*
	 * If event index feature is negotiated, enabling
	 * interrupts is done through setting the latest
	 * consumed index in the used_event field
	 */
	if (virtio_has_feature(sc, VIRTIO_F_RING_EVENT_IDX)) {
		VQ_USED_EVENT(vq) = vq->vq_used_idx;
		vq_sync_aring_used_event(sc, vq, BUS_DMASYNC_PREWRITE);
	} else {
		vq->vq_avail->flags &= ~VRING_AVAIL_F_NO_INTERRUPT;
		vq_sync_aring(sc, vq, BUS_DMASYNC_PREWRITE);
	}

	virtio_membar_sync();

	vq->vq_queued++;

	vq_sync_uring(sc, vq, BUS_DMASYNC_POSTREAD);
	if (vq->vq_used_idx != vq->vq_used->idx)
		return 1;

	return 0;
}

/*
 * Returns a number of slots in the used ring available to
 * be supplied to the avail ring.
 */
int
virtio_nused(struct virtqueue *vq)
{
	uint16_t	n;

	vq_sync_uring(vq->vq_owner, vq, BUS_DMASYNC_POSTREAD);
	n = (uint16_t)(vq->vq_used->idx - vq->vq_used_idx);
	VIRTIO_ASSERT(n <= vq->vq_num);

	return n;
}

#if VIRTIO_DEBUG
void
virtio_vq_dump(struct virtqueue *vq)
{
#if VIRTIO_DEBUG >= 2
	int i;
#endif
	/* Common fields */
	printf(" + addr: %p\n", vq);
	if (vq->vq_num == 0) {
		printf(" + vq is unused\n");
		return;
	}
	printf(" + vq num: %d\n", vq->vq_num);
	printf(" + vq mask: 0x%X\n", vq->vq_mask);
	printf(" + vq index: %d\n", vq->vq_index);
	printf(" + vq used idx: %d\n", vq->vq_used_idx);
	printf(" + vq avail idx: %d\n", vq->vq_avail_idx);
	printf(" + vq queued: %d\n",vq->vq_queued);
#if VIRTIO_DEBUG >= 2
	for (i = 0; i < vq->vq_num; i++) {
		struct vring_desc *desc = &vq->vq_desc[i];
		printf("  D%-3d len:%d flags:%d next:%d\n", i, desc->len,
		    desc->flags, desc->next);
	}
#endif
	/* Avail ring fields */
	printf(" + avail flags: 0x%X\n", vq->vq_avail->flags);
	printf(" + avail idx: %d\n", vq->vq_avail->idx);
	printf(" + avail event: %d\n", VQ_AVAIL_EVENT(vq));
#if VIRTIO_DEBUG >= 2
	for (i = 0; i < vq->vq_num; i++)
		printf("  A%-3d idx:%d\n", i, vq->vq_avail->ring[i]);
#endif
	/* Used ring fields */
	printf(" + used flags: 0x%X\n",vq->vq_used->flags);
	printf(" + used idx: %d\n",vq->vq_used->idx);
	printf(" + used event: %d\n", VQ_USED_EVENT(vq));
#if VIRTIO_DEBUG >= 2
	for (i = 0; i < vq->vq_num; i++) {
		printf("  U%-3d id:%d len:%d\n", i,
				vq->vq_used->ring[i].id,
				vq->vq_used->ring[i].len);
	}
#endif
	printf(" +++++++++++++++++++++++++++\n");
}
#endif
