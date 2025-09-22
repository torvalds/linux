/*	$OpenBSD: vioscsi.c,v 1.37 2025/08/01 14:41:03 sf Exp $	*/
/*
 * Copyright (c) 2013 Google Inc.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/mutex.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/pv/vioscsireg.h>
#include <dev/pv/virtiovar.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

enum { vioscsi_debug = 0 };
#define DPRINTF(f...) do { if (vioscsi_debug) printf(f); } while (0)

/* Number of DMA segments for buffers that the device must support */
#define SEG_MAX		(MAXPHYS/PAGE_SIZE + 1)
/* In the virtqueue, we need space for header and footer, too */
#define ALLOC_SEGS	(SEG_MAX + 2)

struct vioscsi_req {
	struct virtio_scsi_req_hdr	 vr_req;
	struct virtio_scsi_res_hdr	 vr_res;
	struct scsi_xfer		*vr_xs;
	bus_dmamap_t			 vr_control;
	bus_dmamap_t			 vr_data;
	SLIST_ENTRY(vioscsi_req)	 vr_list;
	int				 vr_qe_index;
};

struct vioscsi_softc {
	struct device		 sc_dev;
	struct scsi_iopool	 sc_iopool;
	struct mutex		 sc_vr_mtx;

	struct virtqueue	 sc_vqs[3];
	struct vioscsi_req	*sc_reqs;
	bus_dma_segment_t        sc_reqs_segs[1];
	SLIST_HEAD(, vioscsi_req) sc_freelist;
};

int		 vioscsi_match(struct device *, void *, void *);
void		 vioscsi_attach(struct device *, struct device *, void *);

int		 vioscsi_alloc_reqs(struct vioscsi_softc *,
		    struct virtio_softc *, int);
void		 vioscsi_scsi_cmd(struct scsi_xfer *);
int		 vioscsi_vq_done(struct virtqueue *);
void		 vioscsi_req_done(struct vioscsi_softc *, struct virtio_softc *,
		    struct vioscsi_req *);
void		*vioscsi_req_get(void *);
void		 vioscsi_req_put(void *, void *);

const struct cfattach vioscsi_ca = {
	sizeof(struct vioscsi_softc),
	vioscsi_match,
	vioscsi_attach,
};

struct cfdriver vioscsi_cd = {
	NULL, "vioscsi", DV_DULL,
};

const struct scsi_adapter vioscsi_switch = {
	vioscsi_scsi_cmd, NULL, NULL, NULL, NULL
};

const char *const vioscsi_vq_names[] = {
	"control",
	"event",
	"request",
};

int
vioscsi_match(struct device *parent, void *self, void *aux)
{
	struct virtio_attach_args *va = aux;

	if (va->va_devid == PCI_PRODUCT_VIRTIO_SCSI)
		return (1);
	return (0);
}

void
vioscsi_attach(struct device *parent, struct device *self, void *aux)
{
	struct virtio_softc *vsc = (struct virtio_softc *)parent;
	struct vioscsi_softc *sc = (struct vioscsi_softc *)self;
	struct virtio_attach_args *va = aux;
	struct scsibus_attach_args saa;
	int i, rv;

	if (vsc->sc_child != NULL) {
		printf(": parent already has a child\n");
		return;
	}
	vsc->sc_child = &sc->sc_dev;
	vsc->sc_ipl = IPL_BIO;

	// TODO(matthew): Negotiate hotplug.

	vsc->sc_vqs = sc->sc_vqs;
	vsc->sc_nvqs = nitems(sc->sc_vqs);

	if (virtio_negotiate_features(vsc, NULL) != 0)
		goto err;
	uint32_t cmd_per_lun = virtio_read_device_config_4(vsc,
	    VIRTIO_SCSI_CONFIG_CMD_PER_LUN);
	uint32_t seg_max = virtio_read_device_config_4(vsc,
	    VIRTIO_SCSI_CONFIG_SEG_MAX);
	uint16_t max_target = virtio_read_device_config_2(vsc,
	    VIRTIO_SCSI_CONFIG_MAX_TARGET);
	uint32_t max_lun = virtio_read_device_config_4(vsc,
	    VIRTIO_SCSI_CONFIG_MAX_LUN);

	if (seg_max < SEG_MAX) {
		printf("\nMax number of segments %d too small\n", seg_max);
		goto err;
	}

	for (i = 0; i < nitems(sc->sc_vqs); i++) {
		rv = virtio_alloc_vq(vsc, &sc->sc_vqs[i], i, ALLOC_SEGS,
		    vioscsi_vq_names[i]);
		if (rv) {
			printf(": failed to allocate virtqueue %d\n", i);
			goto err;
		}
		sc->sc_vqs[i].vq_done = vioscsi_vq_done;
	}

	int qsize = sc->sc_vqs[2].vq_num;
	printf(": qsize %d\n", qsize);

	SLIST_INIT(&sc->sc_freelist);
	mtx_init(&sc->sc_vr_mtx, IPL_BIO);
	scsi_iopool_init(&sc->sc_iopool, sc, vioscsi_req_get, vioscsi_req_put);

	int nreqs = vioscsi_alloc_reqs(sc, vsc, qsize);
	if (nreqs == 0) {
		printf("\nCan't alloc reqs\n");
		goto err;
	}

	saa.saa_adapter = &vioscsi_switch;
	saa.saa_adapter_softc = sc;
	saa.saa_adapter_target = SDEV_NO_ADAPTER_TARGET;
	saa.saa_adapter_buswidth = max_target;
	saa.saa_luns = MIN(UINT8_MAX, max_lun + 1);
	saa.saa_openings = (nreqs > cmd_per_lun) ? cmd_per_lun : nreqs;
	saa.saa_pool = &sc->sc_iopool;
	saa.saa_quirks = saa.saa_flags = 0;
	saa.saa_wwpn = saa.saa_wwnn = 0;

	if (virtio_attach_finish(vsc, va) != 0)
		goto err;
	config_found(self, &saa, scsiprint);
	return;

err:
	vsc->sc_child = VIRTIO_CHILD_ERROR;
	return;
}

void
vioscsi_scsi_cmd(struct scsi_xfer *xs)
{
	struct vioscsi_softc *sc = xs->sc_link->bus->sb_adapter_softc;
	struct virtio_softc *vsc = (struct virtio_softc *)sc->sc_dev.dv_parent;
	struct vioscsi_req *vr = xs->io;
	struct virtio_scsi_req_hdr *req = &vr->vr_req;
	struct virtqueue *vq = &sc->sc_vqs[2];
	int slot = vr->vr_qe_index;

	DPRINTF("vioscsi_scsi_cmd: enter\n");

	// TODO(matthew): Support bidirectional SCSI commands?
	if ((xs->flags & (SCSI_DATA_IN | SCSI_DATA_OUT))
	    == (SCSI_DATA_IN | SCSI_DATA_OUT)) {
		goto stuffup;
	}

	vr->vr_xs = xs;

	/*
	 * "The only supported format for the LUN field is: first byte set to
	 * 1, second byte set to target, third and fourth byte representing a
	 * single level LUN structure, followed by four zero bytes."
	 */
	if (xs->sc_link->target >= 256 || xs->sc_link->lun >= 16384)
		goto stuffup;
	req->lun[0] = 1;
	req->lun[1] = xs->sc_link->target;
	req->lun[2] = 0x40 | (xs->sc_link->lun >> 8);
	req->lun[3] = xs->sc_link->lun;
	memset(req->lun + 4, 0, 4);

	if ((size_t)xs->cmdlen > sizeof(req->cdb))
		goto stuffup;
	memset(req->cdb, 0, sizeof(req->cdb));
	memcpy(req->cdb, &xs->cmd, xs->cmdlen);

	int isread = !!(xs->flags & SCSI_DATA_IN);

	int nsegs = 2;
	if (xs->flags & (SCSI_DATA_IN | SCSI_DATA_OUT)) {
		if (bus_dmamap_load(vsc->sc_dmat, vr->vr_data,
		    xs->data, xs->datalen, NULL,
		    ((isread ? BUS_DMA_READ : BUS_DMA_WRITE) |
		     BUS_DMA_NOWAIT)))
			goto stuffup;
		nsegs += vr->vr_data->dm_nsegs;
	}

	/*
	 * Adjust reservation to the number needed, or virtio gets upset. Note
	 * that it may trim UP if 'xs' is being recycled w/o getting a new
	 * reservation!
	 */
	int s = splbio();
	virtio_enqueue_trim(vq, slot, nsegs);
	splx(s);

	bus_dmamap_sync(vsc->sc_dmat, vr->vr_control,
	    offsetof(struct vioscsi_req, vr_req),
	    sizeof(struct virtio_scsi_req_hdr),
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(vsc->sc_dmat, vr->vr_control,
	    offsetof(struct vioscsi_req, vr_res),
            sizeof(struct virtio_scsi_res_hdr),
	    BUS_DMASYNC_PREREAD);
	if (xs->flags & (SCSI_DATA_IN | SCSI_DATA_OUT))
		bus_dmamap_sync(vsc->sc_dmat, vr->vr_data, 0, xs->datalen,
		    isread ? BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE);

	s = splbio();
	virtio_enqueue_p(vq, slot, vr->vr_control,
	    offsetof(struct vioscsi_req, vr_req),
            sizeof(struct virtio_scsi_req_hdr),
	    1);
	if (xs->flags & SCSI_DATA_OUT)
		virtio_enqueue(vq, slot, vr->vr_data, 1);
	virtio_enqueue_p(vq, slot, vr->vr_control,
	    offsetof(struct vioscsi_req, vr_res),
            sizeof(struct virtio_scsi_res_hdr),
	    0);
	if (xs->flags & SCSI_DATA_IN)
		virtio_enqueue(vq, slot, vr->vr_data, 0);

	virtio_enqueue_commit(vsc, vq, slot, 1);

	if (ISSET(xs->flags, SCSI_POLL)) {
		DPRINTF("vioscsi_scsi_cmd: polling...\n");
		int timeout = 1000;
		do {
			virtio_poll_intr(vsc);
			if (vr->vr_xs != xs)
				break;
			delay(1000);
		} while (--timeout > 0);
		if (vr->vr_xs == xs) {
			// TODO(matthew): Abort the request.
			xs->error = XS_TIMEOUT;
			xs->resid = xs->datalen;
			DPRINTF("vioscsi_scsi_cmd: polling timeout\n");
			scsi_done(xs);
		}
		DPRINTF("vioscsi_scsi_cmd: done (timeout=%d)\n", timeout);
	}
	splx(s);
	return;

stuffup:
	xs->error = XS_DRIVER_STUFFUP;
	xs->resid = xs->datalen;
	DPRINTF("vioscsi_scsi_cmd: stuffup\n");
	scsi_done(xs);
}

void
vioscsi_req_done(struct vioscsi_softc *sc, struct virtio_softc *vsc,
    struct vioscsi_req *vr)
{
	struct scsi_xfer *xs = vr->vr_xs;
	DPRINTF("vioscsi_req_done: enter vr: %p xs: %p\n", vr, xs);

	int isread = !!(xs->flags & SCSI_DATA_IN);
	bus_dmamap_sync(vsc->sc_dmat, vr->vr_control,
	    offsetof(struct vioscsi_req, vr_req),
	    sizeof(struct virtio_scsi_req_hdr),
	    BUS_DMASYNC_POSTWRITE);
	bus_dmamap_sync(vsc->sc_dmat, vr->vr_control,
	    offsetof(struct vioscsi_req, vr_res),
	    sizeof(struct virtio_scsi_res_hdr),
	    BUS_DMASYNC_POSTREAD);
	if (xs->flags & (SCSI_DATA_IN | SCSI_DATA_OUT)) {
		bus_dmamap_sync(vsc->sc_dmat, vr->vr_data, 0, xs->datalen,
		    isread ? BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(vsc->sc_dmat, vr->vr_data);
	}

	if (vr->vr_res.response != VIRTIO_SCSI_S_OK) {
		xs->error = XS_DRIVER_STUFFUP;
		xs->resid = xs->datalen;
		DPRINTF("vioscsi_req_done: stuffup: %d\n", vr->vr_res.response);
		goto done;
	}

	size_t sense_len = MIN(sizeof(xs->sense), vr->vr_res.sense_len);
	memcpy(&xs->sense, vr->vr_res.sense, sense_len);
	xs->error = (sense_len == 0) ? XS_NOERROR : XS_SENSE;

	xs->status = vr->vr_res.status;
	xs->resid = vr->vr_res.residual;

	DPRINTF("vioscsi_req_done: done %d, %d, %zd\n",
	    xs->error, xs->status, xs->resid);

done:
	vr->vr_xs = NULL;
	scsi_done(xs);
}

int
vioscsi_vq_done(struct virtqueue *vq)
{
	struct virtio_softc *vsc = vq->vq_owner;
	struct vioscsi_softc *sc = (struct vioscsi_softc *)vsc->sc_child;
	struct vq_entry *qe;
	struct vioscsi_req *vr;
	int ret = 0;

	DPRINTF("vioscsi_vq_done: enter\n");

	for (;;) {
		int r, s, slot;
		s = splbio();
		r = virtio_dequeue(vsc, vq, &slot, NULL);
		splx(s);
		if (r != 0)
			break;

		DPRINTF("vioscsi_vq_done: slot=%d\n", slot);
		qe = &vq->vq_entries[slot];
		vr = &sc->sc_reqs[qe->qe_vr_index];
		vioscsi_req_done(sc, vsc, vr);
		ret = 1;
	}

	DPRINTF("vioscsi_vq_done: exit %d\n", ret);

	return (ret);
}

/*
 * vioscso_req_get() provides the SCSI layer with all the
 * resources necessary to start an I/O on the device.
 *
 * Since the size of the I/O is unknown at this time the
 * resources allocated (a.k.a. reserved) must be sufficient
 * to allow the maximum possible I/O size.
 *
 * When the I/O is actually attempted via vioscsi_scsi_cmd()
 * excess resources will be returned via virtio_enqueue_trim().
 */
void *
vioscsi_req_get(void *cookie)
{
	struct vioscsi_softc *sc = cookie;
	struct vioscsi_req *vr = NULL;

	mtx_enter(&sc->sc_vr_mtx);
	vr = SLIST_FIRST(&sc->sc_freelist);
	if (vr != NULL)
		SLIST_REMOVE_HEAD(&sc->sc_freelist, vr_list);
	mtx_leave(&sc->sc_vr_mtx);

	DPRINTF("vioscsi_req_get: %p\n", vr);

	return (vr);
}

void
vioscsi_req_put(void *cookie, void *io)
{
	struct vioscsi_softc *sc = cookie;
	struct vioscsi_req *vr = io;

	DPRINTF("vioscsi_req_put: %p\n", vr);

	mtx_enter(&sc->sc_vr_mtx);
	/*
	 * Do *NOT* call virtio_dequeue_commit()!
	 *
	 * Descriptors are permanently associated with the vioscsi_req and
	 * should not be placed on the free list!
	 */
	SLIST_INSERT_HEAD(&sc->sc_freelist, vr, vr_list);
	mtx_leave(&sc->sc_vr_mtx);
}

int
vioscsi_alloc_reqs(struct vioscsi_softc *sc, struct virtio_softc *vsc,
    int qsize)
{
	struct virtqueue *vq = &sc->sc_vqs[2];
	struct vioscsi_req *vr;
	struct vring_desc *vd;
	size_t allocsize;
	int i, r, nreqs, rsegs, slot;
	void *vaddr;

	if (vq->vq_indirect != NULL)
		nreqs = qsize;
	else
		nreqs = qsize / ALLOC_SEGS;

	allocsize = nreqs * sizeof(struct vioscsi_req);
	r = bus_dmamem_alloc(vsc->sc_dmat, allocsize, 0, 0,
	    &sc->sc_reqs_segs[0], 1, &rsegs, BUS_DMA_NOWAIT | BUS_DMA_64BIT);
	if (r != 0) {
		printf("bus_dmamem_alloc, size %zd, error %d\n",
		    allocsize, r);
		return 0;
	}
	r = bus_dmamem_map(vsc->sc_dmat, &sc->sc_reqs_segs[0], 1,
	    allocsize, (caddr_t *)&vaddr, BUS_DMA_NOWAIT);
	if (r != 0) {
		printf("bus_dmamem_map failed, error %d\n", r);
		bus_dmamem_free(vsc->sc_dmat, &sc->sc_reqs_segs[0], 1);
		return 0;
	}
	sc->sc_reqs = vaddr;
	memset(vaddr, 0, allocsize);

	for (i = 0; i < nreqs; i++) {
		/*
		 * Assign descriptors and create the DMA maps for each
		 * allocated request.
		 */
		vr = &sc->sc_reqs[i];
		r = virtio_enqueue_prep(vq, &slot);
		if (r == 0)
			r = virtio_enqueue_reserve(vq, slot, ALLOC_SEGS);
		if (r != 0)
			return i;

		if (vq->vq_indirect == NULL) {
			/*
			 * The reserved slots must be a contiguous block
			 * starting at vq_desc[slot].
			 */
			vd = &vq->vq_desc[slot];
			for (r = 0; r < ALLOC_SEGS - 1; r++) {
				DPRINTF("vd[%d].next = %d should be %d\n",
				    r, vd[r].next, (slot + r + 1));
				if (vd[r].next != (slot + r + 1))
					return i;
			}
			if (r == (ALLOC_SEGS -1) && vd[r].next != 0)
				return i;
			DPRINTF("Reserved slots are contiguous as required!\n");
		}

		vr->vr_qe_index = slot;
		vr->vr_req.id = slot;
		vr->vr_req.task_attr = VIRTIO_SCSI_S_SIMPLE;
		vq->vq_entries[slot].qe_vr_index = i;

		r = bus_dmamap_create(vsc->sc_dmat,
		    offsetof(struct vioscsi_req, vr_xs), 1,
		    offsetof(struct vioscsi_req, vr_xs), 0,
		    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW | BUS_DMA_64BIT,
		    &vr->vr_control);
		if (r != 0) {
			printf("bus_dmamap_create vr_control failed, error  %d\n", r);
			return i;
		}
		r = bus_dmamap_create(vsc->sc_dmat, MAXPHYS, SEG_MAX, MAXPHYS,
		    0, BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW | BUS_DMA_64BIT,
		    &vr->vr_data);
		if (r != 0) {
			printf("bus_dmamap_create vr_data failed, error %d\n", r );
			return i;
		}
		r = bus_dmamap_load(vsc->sc_dmat, vr->vr_control,
		    vr, offsetof(struct vioscsi_req, vr_xs), NULL,
		    BUS_DMA_NOWAIT);
		if (r != 0) {
			printf("bus_dmamap_load vr_control failed, error %d\n", r );
			return i;
		}

		SLIST_INSERT_HEAD(&sc->sc_freelist, vr, vr_list);
	}

	return nreqs;
}
