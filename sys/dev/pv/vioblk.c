/*	$OpenBSD: vioblk.c,v 1.47 2025/09/16 12:18:10 hshoexer Exp $	*/

/*
 * Copyright (c) 2012 Stefan Fritsch.
 * Copyright (c) 2010 Minoura Makoto.
 * Copyright (c) 1998, 2001 Manuel Bouyer.
 * All rights reserved.
 *
 * This code is based in part on the NetBSD ld_virtio driver and the
 * OpenBSD vdsk driver.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
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
 * Copyright (c) 2009, 2011 Mark Kettenis
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
#include <machine/bus.h>

#include <sys/device.h>
#include <sys/mutex.h>
#include <dev/pv/virtioreg.h>
#include <dev/pv/virtiovar.h>
#include <dev/pv/vioblkreg.h>

#include <scsi/scsi_all.h>
#include <scsi/scsi_disk.h>
#include <scsi/scsiconf.h>

#define VIOBLK_DONE	-1

/* Number of DMA segments for buffers that the device must support */
#define SEG_MAX		(MAXPHYS/PAGE_SIZE + 1)
/* In the virtqueue, we need space for header and footer, too */
#define ALLOC_SEGS	(SEG_MAX + 2)

static const struct virtio_feature_name vioblk_feature_names[] = {
#if VIRTIO_DEBUG
	{ VIRTIO_BLK_F_BARRIER,		"Barrier" },
	{ VIRTIO_BLK_F_SIZE_MAX,	"SizeMax" },
	{ VIRTIO_BLK_F_SEG_MAX,		"SegMax" },
	{ VIRTIO_BLK_F_GEOMETRY,	"Geometry" },
	{ VIRTIO_BLK_F_RO,		"RO" },
	{ VIRTIO_BLK_F_BLK_SIZE,	"BlkSize" },
	{ VIRTIO_BLK_F_SCSI,		"SCSI" },
	{ VIRTIO_BLK_F_FLUSH,		"Flush" },
	{ VIRTIO_BLK_F_TOPOLOGY,	"Topology" },
	{ VIRTIO_BLK_F_CONFIG_WCE,	"ConfigWCE" },
	{ VIRTIO_BLK_F_MQ,		"MQ" },
	{ VIRTIO_BLK_F_DISCARD,		"Discard" },
	{ VIRTIO_BLK_F_WRITE_ZEROES,	"Write0s" },
	{ VIRTIO_BLK_F_LIFETIME,	"Lifetime" },
	{ VIRTIO_BLK_F_SECURE_ERASE,	"SecErase" },
#endif
	{ 0,				NULL }
};

struct virtio_blk_req {
	struct virtio_blk_req_hdr	 vr_hdr;
	uint8_t				 vr_status;
#define VR_DMA_END	offsetof(struct virtio_blk_req, vr_qe_index)
	int16_t				 vr_qe_index;
	int				 vr_len;
	struct scsi_xfer		*vr_xs;
	bus_dmamap_t			 vr_cmdsts;
	bus_dmamap_t			 vr_payload;
	SLIST_ENTRY(virtio_blk_req)	 vr_list;
};

struct vioblk_softc {
	struct device		 sc_dev;
	struct virtio_softc	*sc_virtio;

	struct virtqueue         sc_vq[1];
	struct virtio_blk_req   *sc_reqs;
	bus_dma_segment_t        sc_reqs_segs[1];
	int			 sc_nreqs;

	struct scsi_iopool	 sc_iopool;
	struct mutex		 sc_vr_mtx;
	SLIST_HEAD(, virtio_blk_req) sc_freelist;

	int			 sc_notify_on_empty;

	uint32_t		 sc_queued;

	uint64_t		 sc_capacity;
};

int	vioblk_match(struct device *, void *, void *);
void	vioblk_attach(struct device *, struct device *, void *);
int	vioblk_alloc_reqs(struct vioblk_softc *, int);
int	vioblk_vq_done(struct virtqueue *);
void	vioblk_vq_done1(struct vioblk_softc *, struct virtio_softc *,
			struct virtqueue *, int);
void	vioblk_reset(struct vioblk_softc *);

void	vioblk_scsi_cmd(struct scsi_xfer *);

void   *vioblk_req_get(void *);
void	vioblk_req_put(void *, void *);

void	vioblk_scsi_inq(struct scsi_xfer *);
void	vioblk_scsi_capacity(struct scsi_xfer *);
void	vioblk_scsi_capacity16(struct scsi_xfer *);
void	vioblk_scsi_done(struct scsi_xfer *, int);

const struct cfattach vioblk_ca = {
	sizeof(struct vioblk_softc),
	vioblk_match,
	vioblk_attach,
	NULL
};

struct cfdriver vioblk_cd = {
	NULL, "vioblk", DV_DULL, CD_COCOVM
};

const struct scsi_adapter vioblk_switch = {
	vioblk_scsi_cmd, NULL, NULL, NULL, NULL
};

int
vioblk_match(struct device *parent, void *match, void *aux)
{
	struct virtio_attach_args *va = aux;
	if (va->va_devid == PCI_PRODUCT_VIRTIO_BLOCK)
		return 1;
	return 0;
}

#define DNPRINTF(n,x...)				\
    do { if (VIRTIO_DEBUG >= n) printf(x); } while(0)

void
vioblk_attach(struct device *parent, struct device *self, void *aux)
{
	struct vioblk_softc *sc = (struct vioblk_softc *)self;
	struct virtio_softc *vsc = (struct virtio_softc *)parent;
	struct virtio_attach_args *va = aux;
	struct scsibus_attach_args saa;
	int qsize;

	vsc->sc_vqs = &sc->sc_vq[0];
	vsc->sc_nvqs = 1;
	if (vsc->sc_child)
		panic("already attached to something else");
	vsc->sc_child = self;
	vsc->sc_ipl = IPL_BIO;
	sc->sc_virtio = vsc;
	vsc->sc_driver_features = VIRTIO_BLK_F_RO | VIRTIO_F_NOTIFY_ON_EMPTY |
	     VIRTIO_BLK_F_SIZE_MAX | VIRTIO_BLK_F_SEG_MAX | VIRTIO_BLK_F_FLUSH;

        if (virtio_negotiate_features(vsc, vioblk_feature_names) != 0)
		goto err;

	if (virtio_has_feature(vsc, VIRTIO_BLK_F_SIZE_MAX)) {
		uint32_t size_max = virtio_read_device_config_4(vsc,
		    VIRTIO_BLK_CONFIG_SIZE_MAX);
		if (size_max < PAGE_SIZE) {
			printf("\nMax segment size %u too low\n", size_max);
			goto err;
		}
	}

	if (virtio_has_feature(vsc, VIRTIO_BLK_F_SEG_MAX)) {
		uint32_t seg_max = virtio_read_device_config_4(vsc,
		    VIRTIO_BLK_CONFIG_SEG_MAX);
		if (seg_max < SEG_MAX) {
			printf("\nMax number of segments %d too small\n",
			    seg_max);
			goto err;
		}
	}

	sc->sc_capacity = virtio_read_device_config_8(vsc,
	    VIRTIO_BLK_CONFIG_CAPACITY);

	if (virtio_alloc_vq(vsc, &sc->sc_vq[0], 0, ALLOC_SEGS, "I/O request")
	    != 0) {
		printf("\nCan't alloc virtqueue\n");
		goto err;
	}
	qsize = sc->sc_vq[0].vq_num;
	sc->sc_vq[0].vq_done = vioblk_vq_done;

	if (virtio_has_feature(vsc, VIRTIO_F_NOTIFY_ON_EMPTY)) {
		virtio_stop_vq_intr(vsc, &sc->sc_vq[0]);
		sc->sc_notify_on_empty = 1;
	}
	else {
		sc->sc_notify_on_empty = 0;
	}

	sc->sc_queued = 0;

	SLIST_INIT(&sc->sc_freelist);
	mtx_init(&sc->sc_vr_mtx, IPL_BIO);
	scsi_iopool_init(&sc->sc_iopool, sc, vioblk_req_get, vioblk_req_put);

	sc->sc_nreqs = vioblk_alloc_reqs(sc, qsize);
	if (sc->sc_nreqs == 0) {
		printf("\nCan't alloc reqs\n");
		goto err;
	}
	DNPRINTF(1, "%s: qsize: %d\n", __func__, qsize);
	printf("\n");

	saa.saa_adapter = &vioblk_switch;
	saa.saa_adapter_softc = self;
	saa.saa_adapter_buswidth = 1;
	saa.saa_luns = 1;
	saa.saa_adapter_target = SDEV_NO_ADAPTER_TARGET;
	saa.saa_openings = sc->sc_nreqs;
	saa.saa_pool = &sc->sc_iopool;
	if (virtio_has_feature(vsc, VIRTIO_BLK_F_RO))
		saa.saa_flags = SDEV_READONLY;
	else
		saa.saa_flags = 0;
	saa.saa_quirks = 0;
	saa.saa_wwpn = saa.saa_wwnn = 0;

	if (virtio_attach_finish(vsc, va) != 0)
		goto err;
	config_found(self, &saa, scsiprint);
	return;

err:
	vsc->sc_child = VIRTIO_CHILD_ERROR;
	return;
}

/*
 * vioblk_req_get() provides the SCSI layer with all the
 * resources necessary to start an I/O on the device.
 *
 * Since the size of the I/O is unknown at this time the
 * resources allocated (a.k.a. reserved) must be sufficient
 * to allow the maximum possible I/O size.
 *
 * When the I/O is actually attempted via vioblk_scsi_cmd()
 * excess resources will be returned via virtio_enqueue_trim().
 */
void *
vioblk_req_get(void *cookie)
{
	struct vioblk_softc *sc = cookie;
	struct virtio_blk_req *vr = NULL;

	mtx_enter(&sc->sc_vr_mtx);
	vr = SLIST_FIRST(&sc->sc_freelist);
	if (vr != NULL)
		SLIST_REMOVE_HEAD(&sc->sc_freelist, vr_list);
	mtx_leave(&sc->sc_vr_mtx);

	DNPRINTF(2, "%s: %p\n", __func__, vr);

	return vr;
}

void
vioblk_req_put(void *cookie, void *io)
{
	struct vioblk_softc *sc = cookie;
	struct virtio_blk_req *vr = io;

	DNPRINTF(2, "%s: %p\n", __func__, vr);

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
vioblk_vq_done(struct virtqueue *vq)
{
	struct virtio_softc *vsc = vq->vq_owner;
	struct vioblk_softc *sc = (struct vioblk_softc *)vsc->sc_child;
	struct vq_entry *qe;
	int slot;
	int ret = 0;

	if (!sc->sc_notify_on_empty)
		virtio_stop_vq_intr(vsc, vq);
	for (;;) {
		if (virtio_dequeue(vsc, vq, &slot, NULL) != 0) {
			if (sc->sc_notify_on_empty)
				break;
			virtio_start_vq_intr(vsc, vq);
			if (virtio_dequeue(vsc, vq, &slot, NULL) != 0)
				break;
		}
		qe = &vq->vq_entries[slot];
		vioblk_vq_done1(sc, vsc, vq, qe->qe_vr_index);
		ret = 1;
	}
	return ret;
}

void
vioblk_vq_done1(struct vioblk_softc *sc, struct virtio_softc *vsc,
    struct virtqueue *vq, int slot)
{
	struct virtio_blk_req *vr = &sc->sc_reqs[slot];
	struct scsi_xfer *xs = vr->vr_xs;
	KASSERT(vr->vr_len != VIOBLK_DONE);
	bus_dmamap_sync(vsc->sc_dmat, vr->vr_cmdsts, 0,
	    sizeof(struct virtio_blk_req_hdr), BUS_DMASYNC_POSTWRITE);
	if (vr->vr_hdr.type != VIRTIO_BLK_T_FLUSH) {
		bus_dmamap_sync(vsc->sc_dmat, vr->vr_payload, 0, vr->vr_len,
		    (vr->vr_hdr.type == VIRTIO_BLK_T_IN) ?
		    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(vsc->sc_dmat, vr->vr_payload);
	}
	bus_dmamap_sync(vsc->sc_dmat, vr->vr_cmdsts,
	    sizeof(struct virtio_blk_req_hdr), sizeof(uint8_t),
	    BUS_DMASYNC_POSTREAD);


	if (vr->vr_status != VIRTIO_BLK_S_OK) {
		DNPRINTF(1, "%s: EIO\n", __func__);
		xs->error = XS_DRIVER_STUFFUP;
		xs->resid = xs->datalen;
	} else {
		xs->error = XS_NOERROR;
		xs->resid = xs->datalen - vr->vr_len;
	}
	vr->vr_len = VIOBLK_DONE;
	scsi_done(xs);
}

void
vioblk_reset(struct vioblk_softc *sc)
{
	int i;

	/* reset device to stop DMA */
	virtio_reset(sc->sc_virtio);

	/* finish requests that have been completed */
	virtio_check_vq(sc->sc_virtio, &sc->sc_vq[0]);

	/* abort all remaining requests */
	for (i = 0; i < sc->sc_nreqs; i++) {
		struct virtio_blk_req *vr = &sc->sc_reqs[i];
		struct scsi_xfer *xs = vr->vr_xs;

		if (vr->vr_len == VIOBLK_DONE)
			continue;

		xs->error = XS_DRIVER_STUFFUP;
		xs->resid = xs->datalen;
		scsi_done(xs);
	}
}

void
vioblk_scsi_cmd(struct scsi_xfer *xs)
{
	struct vioblk_softc *sc = xs->sc_link->bus->sb_adapter_softc;
	struct virtqueue *vq = &sc->sc_vq[0];
	struct virtio_softc *vsc = sc->sc_virtio;
	struct virtio_blk_req *vr;
	int len, s, timeout, isread, slot, ret, nsegs;
	int error = XS_DRIVER_STUFFUP;
	struct scsi_rw *rw;
	struct scsi_rw_10 *rw10;
	struct scsi_rw_12 *rw12;
	struct scsi_rw_16 *rw16;
	u_int64_t lba = 0;
	u_int32_t sector_count = 0;
	uint8_t operation;

	switch (xs->cmd.opcode) {
	case READ_COMMAND:
	case READ_10:
	case READ_12:
	case READ_16:
		operation = VIRTIO_BLK_T_IN;
		isread = 1;
		break;
	case WRITE_COMMAND:
	case WRITE_10:
	case WRITE_12:
	case WRITE_16:
		operation = VIRTIO_BLK_T_OUT;
		isread = 0;
		break;

	case SYNCHRONIZE_CACHE:
		if (!virtio_has_feature(vsc, VIRTIO_BLK_F_FLUSH)) {
			vioblk_scsi_done(xs, XS_NOERROR);
			return;
		}
		operation = VIRTIO_BLK_T_FLUSH;
		break;

	case INQUIRY:
		vioblk_scsi_inq(xs);
		return;
	case READ_CAPACITY:
		vioblk_scsi_capacity(xs);
		return;
	case READ_CAPACITY_16:
		vioblk_scsi_capacity16(xs);
		return;

	case TEST_UNIT_READY:
	case START_STOP:
	case PREVENT_ALLOW:
		vioblk_scsi_done(xs, XS_NOERROR);
		return;

	default:
		printf("%s cmd 0x%02x\n", __func__, xs->cmd.opcode);
	case MODE_SENSE:
	case MODE_SENSE_BIG:
	case REPORT_LUNS:
		vioblk_scsi_done(xs, XS_DRIVER_STUFFUP);
		return;
	}

	/*
	 * READ/WRITE/SYNCHRONIZE commands. SYNCHRONIZE CACHE has same
	 * layout as 10-byte READ/WRITE commands.
	 */
	if (xs->cmdlen == 6) {
		rw = (struct scsi_rw *)&xs->cmd;
		lba = _3btol(rw->addr) & (SRW_TOPADDR << 16 | 0xffff);
		sector_count = rw->length ? rw->length : 0x100;
	} else if (xs->cmdlen == 10) {
		rw10 = (struct scsi_rw_10 *)&xs->cmd;
		lba = _4btol(rw10->addr);
		sector_count = _2btol(rw10->length);
	} else if (xs->cmdlen == 12) {
		rw12 = (struct scsi_rw_12 *)&xs->cmd;
		lba = _4btol(rw12->addr);
		sector_count = _4btol(rw12->length);
	} else if (xs->cmdlen == 16) {
		rw16 = (struct scsi_rw_16 *)&xs->cmd;
		lba = _8btol(rw16->addr);
		sector_count = _4btol(rw16->length);
	}

	s = splbio();
	vr = xs->io;
	slot = vr->vr_qe_index;
	if (operation != VIRTIO_BLK_T_FLUSH) {
		len = MIN(xs->datalen, sector_count * VIRTIO_BLK_SECTOR_SIZE);
		ret = bus_dmamap_load(vsc->sc_dmat, vr->vr_payload,
		    xs->data, len, NULL,
		    ((isread ? BUS_DMA_READ : BUS_DMA_WRITE) |
		     BUS_DMA_NOWAIT));
		if (ret) {
			printf("%s: bus_dmamap_load: %d", __func__, ret);
			error = XS_DRIVER_STUFFUP;
			goto out_done;
		}
		nsegs = vr->vr_payload->dm_nsegs + 2;
	} else {
		len = 0;
		nsegs = 2;
	}

	/*
	 * Adjust reservation to the number needed, or virtio gets upset. Note
	 * that it may trim UP if 'xs' is being recycled w/o getting a new
	 * reservation!
	 */
	virtio_enqueue_trim(vq, slot, nsegs);

	vr->vr_xs = xs;
	vr->vr_hdr.type = operation;
	vr->vr_hdr.ioprio = 0;
	vr->vr_hdr.sector = lba;
	vr->vr_len = len;

	bus_dmamap_sync(vsc->sc_dmat, vr->vr_cmdsts,
			0, sizeof(struct virtio_blk_req_hdr),
			BUS_DMASYNC_PREWRITE);
	if (operation != VIRTIO_BLK_T_FLUSH) {
		bus_dmamap_sync(vsc->sc_dmat, vr->vr_payload, 0, len,
		    isread ? BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE);
	}
	bus_dmamap_sync(vsc->sc_dmat, vr->vr_cmdsts,
	    offsetof(struct virtio_blk_req, vr_status), sizeof(uint8_t),
	    BUS_DMASYNC_PREREAD);

	virtio_enqueue_p(vq, slot, vr->vr_cmdsts, 0,
	    sizeof(struct virtio_blk_req_hdr), 1);
	if (operation != VIRTIO_BLK_T_FLUSH)
		virtio_enqueue(vq, slot, vr->vr_payload, !isread);
	virtio_enqueue_p(vq, slot, vr->vr_cmdsts,
	    offsetof(struct virtio_blk_req, vr_status), sizeof(uint8_t), 0);
	virtio_enqueue_commit(vsc, vq, slot, 1);
	sc->sc_queued++;

	if (!ISSET(xs->flags, SCSI_POLL)) {
		/* check if some xfers are done: */
		if (sc->sc_queued > 1)
			virtio_check_vq(sc->sc_virtio, vq);
		splx(s);
		return;
	}

	timeout = 15 * 1000;
	do {
		if (virtio_poll_intr(vsc) && vr->vr_len == VIOBLK_DONE)
			break;

		delay(1000);
	} while(--timeout > 0);
	if (timeout <= 0) {
		printf("%s: SCSI_POLL timed out\n", __func__);
		vioblk_reset(sc);
		virtio_reinit_start(vsc);
		virtio_reinit_end(vsc);
	}
	splx(s);
	return;

out_done:
	splx(s);
	vioblk_scsi_done(xs, error);
}

void
vioblk_scsi_inq(struct scsi_xfer *xs)
{
	struct scsi_inquiry *inq = (struct scsi_inquiry *)&xs->cmd;
	struct scsi_inquiry_data inqd;

	if (ISSET(inq->flags, SI_EVPD)) {
		vioblk_scsi_done(xs, XS_DRIVER_STUFFUP);
		return;
	}

	bzero(&inqd, sizeof(inqd));

	inqd.device = T_DIRECT;
	inqd.version = SCSI_REV_SPC3;
	inqd.response_format = SID_SCSI2_RESPONSE;
	inqd.additional_length = SID_SCSI2_ALEN;
	inqd.flags |= SID_CmdQue;
	bcopy("VirtIO  ", inqd.vendor, sizeof(inqd.vendor));
	bcopy("Block Device    ", inqd.product, sizeof(inqd.product));

	scsi_copy_internal_data(xs, &inqd, sizeof(inqd));

	vioblk_scsi_done(xs, XS_NOERROR);
}

void
vioblk_scsi_capacity(struct scsi_xfer *xs)
{
	struct vioblk_softc *sc = xs->sc_link->bus->sb_adapter_softc;
	struct scsi_read_cap_data rcd;
	uint64_t capacity;

	bzero(&rcd, sizeof(rcd));

	capacity = sc->sc_capacity - 1;
	if (capacity > 0xffffffff)
		capacity = 0xffffffff;

	_lto4b(capacity, rcd.addr);
	_lto4b(VIRTIO_BLK_SECTOR_SIZE, rcd.length);

	bcopy(&rcd, xs->data, MIN(sizeof(rcd), xs->datalen));
	vioblk_scsi_done(xs, XS_NOERROR);
}

void
vioblk_scsi_capacity16(struct scsi_xfer *xs)
{
	struct vioblk_softc *sc = xs->sc_link->bus->sb_adapter_softc;
	struct scsi_read_cap_data_16 rcd;

	bzero(&rcd, sizeof(rcd));

	_lto8b(sc->sc_capacity - 1, rcd.addr);
	_lto4b(VIRTIO_BLK_SECTOR_SIZE, rcd.length);

	bcopy(&rcd, xs->data, MIN(sizeof(rcd), xs->datalen));
	vioblk_scsi_done(xs, XS_NOERROR);
}

void
vioblk_scsi_done(struct scsi_xfer *xs, int error)
{
	xs->error = error;
	scsi_done(xs);
}

int
vioblk_alloc_reqs(struct vioblk_softc *sc, int qsize)
{
	struct virtqueue *vq = &sc->sc_vq[0];
	struct vring_desc *vd;
	int allocsize, nreqs, r, rsegs, slot, i;
	void *vaddr;

	if (vq->vq_indirect != NULL)
		nreqs = qsize;
	else
		nreqs = qsize / ALLOC_SEGS;

	allocsize = sizeof(struct virtio_blk_req) * nreqs;
	r = bus_dmamem_alloc(sc->sc_virtio->sc_dmat, allocsize, 0, 0,
	    &sc->sc_reqs_segs[0], 1, &rsegs, BUS_DMA_NOWAIT | BUS_DMA_64BIT);
	if (r != 0) {
		printf("DMA memory allocation failed, size %d, error %d\n",
		    allocsize, r);
		goto err_none;
	}
	r = bus_dmamem_map(sc->sc_virtio->sc_dmat, &sc->sc_reqs_segs[0], 1,
	    allocsize, (caddr_t *)&vaddr, BUS_DMA_NOWAIT);
	if (r != 0) {
		printf("DMA memory map failed, error %d\n", r);
		goto err_dmamem_alloc;
	}
	sc->sc_reqs = vaddr;
	memset(vaddr, 0, allocsize);
	for (i = 0; i < nreqs; i++) {
		/*
		 * Assign descriptors and create the DMA maps for each
		 * allocated request.
		 */
		struct virtio_blk_req *vr = &sc->sc_reqs[i];
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
				DNPRINTF(2, "%s: vd[%d].next = %d should be "
				    "%d\n", __func__, r, vd[r].next,
				    (slot + r + 1));
				if (vd[r].next != (slot + r + 1))
					return i;
			}
			if (r == (ALLOC_SEGS -1) && vd[r].next != 0)
				return i;
			DNPRINTF(2, "%s: reserved slots are contiguous "
			    "(good!)\n", __func__);
		}

		vr->vr_qe_index = slot;
		vq->vq_entries[slot].qe_vr_index = i;
		vr->vr_len = VIOBLK_DONE;

		r = bus_dmamap_create(sc->sc_virtio->sc_dmat,
		    VR_DMA_END, 1, VR_DMA_END, 0,
		    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW | BUS_DMA_64BIT,
		    &vr->vr_cmdsts);
		if (r != 0) {
			printf("cmd dmamap creation failed, err %d\n", r);
			nreqs = i;
			goto err_reqs;
		}
		r = bus_dmamap_load(sc->sc_virtio->sc_dmat, vr->vr_cmdsts,
		    &vr->vr_hdr, VR_DMA_END, NULL, BUS_DMA_NOWAIT);
		if (r != 0) {
			printf("command dmamap load failed, err %d\n", r);
			nreqs = i;
			goto err_reqs;
		}
		r = bus_dmamap_create(sc->sc_virtio->sc_dmat, MAXPHYS,
		    SEG_MAX, MAXPHYS, 0,
		    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW | BUS_DMA_64BIT,
		    &vr->vr_payload);
		if (r != 0) {
			printf("payload dmamap creation failed, err %d\n", r);
			nreqs = i;
			goto err_reqs;
		}
		SLIST_INSERT_HEAD(&sc->sc_freelist, vr, vr_list);
	}
	return nreqs;

err_reqs:
	for (i = 0; i < nreqs; i++) {
		struct virtio_blk_req *vr = &sc->sc_reqs[i];
		if (vr->vr_cmdsts) {
			bus_dmamap_destroy(sc->sc_virtio->sc_dmat,
			    vr->vr_cmdsts);
			vr->vr_cmdsts = 0;
		}
		if (vr->vr_payload) {
			bus_dmamap_destroy(sc->sc_virtio->sc_dmat,
			    vr->vr_payload);
			vr->vr_payload = 0;
		}
	}
	bus_dmamem_unmap(sc->sc_virtio->sc_dmat, (caddr_t)sc->sc_reqs,
	    allocsize);
err_dmamem_alloc:
	bus_dmamem_free(sc->sc_virtio->sc_dmat, &sc->sc_reqs_segs[0], 1);
err_none:
	return 0;
}
