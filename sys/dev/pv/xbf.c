/*	$OpenBSD: xbf.c,v 1.54 2024/05/24 10:05:55 jsg Exp $	*/

/*
 * Copyright (c) 2016, 2017 Mike Belopuhov
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
#include <sys/atomic.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/task.h>

#include <machine/bus.h>

#include <dev/pv/xenreg.h>
#include <dev/pv/xenvar.h>

#include <scsi/scsi_all.h>
#include <scsi/cd.h>
#include <scsi/scsi_disk.h>
#include <scsi/scsiconf.h>

/* #define XBF_DEBUG */

#ifdef XBF_DEBUG
#define DPRINTF(x...)		printf(x)
#else
#define DPRINTF(x...)
#endif

#define XBF_OP_READ		0
#define XBF_OP_WRITE		1
#define XBF_OP_BARRIER		2 /* feature-barrier */
#define XBF_OP_FLUSH		3 /* feature-flush-cache */
#define XBF_OP_DISCARD		5 /* feature-discard */
#define XBF_OP_INDIRECT		6 /* feature-max-indirect-segments */

#define XBF_MAX_SGE		11
#define XBF_MAX_ISGE		8

#define XBF_SEC_SHIFT		9
#define XBF_SEC_SIZE		(1 << XBF_SEC_SHIFT)

#define XBF_CDROM		1
#define XBF_REMOVABLE		2
#define XBF_READONLY		4

#define XBF_OK			0
#define XBF_EIO			-1 /* generic failure */
#define XBF_EOPNOTSUPP		-2 /* only for XBF_OP_BARRIER */

struct xbf_sge {
	uint32_t		 sge_ref;
	uint8_t			 sge_first;
	uint8_t			 sge_last;
	uint16_t		 sge_pad;
} __packed;

/* Generic I/O request */
struct xbf_req {
	uint8_t			 req_op;
	uint8_t			 req_nsegs;
	uint16_t		 req_unit;
#ifdef __amd64__
	uint32_t		 req_pad;
#endif
	uint64_t		 req_id;
	uint64_t		 req_sector;
	struct xbf_sge		 req_sgl[XBF_MAX_SGE];
} __packed;

/* Indirect I/O request */
struct xbf_ireq {
	uint8_t			 req_op;
	uint8_t			 req_iop;
	uint16_t		 req_nsegs;
#ifdef __amd64__
	uint32_t		 req_pad;
#endif
	uint64_t		 req_id;
	uint64_t		 req_sector;
	uint16_t		 req_unit;
	uint32_t		 req_gref[XBF_MAX_ISGE];
#ifdef __i386__
	uint64_t		 req_pad;
#endif
} __packed;

struct xbf_rsp {
	uint64_t		 rsp_id;
	uint8_t			 rsp_op;
	uint8_t			 rsp_pad1;
	int16_t			 rsp_status;
#ifdef __amd64__
	uint32_t		 rsp_pad2;
#endif
} __packed;

union xbf_ring_desc {
	struct xbf_req	 	 xrd_req;
	struct xbf_ireq		 xrd_ireq;
	struct xbf_rsp	 	 xrd_rsp;
} __packed;

#define XBF_MIN_RING_SIZE	1
#define XBF_MAX_RING_SIZE	8
#define XBF_MAX_REQS		256 /* must be a power of 2 */

struct xbf_ring {
	volatile uint32_t	 xr_prod;
	volatile uint32_t	 xr_prod_event;
	volatile uint32_t	 xr_cons;
	volatile uint32_t	 xr_cons_event;
	uint32_t		 xr_reserved[12];
	union xbf_ring_desc	 xr_desc[0];
} __packed;

struct xbf_dma_mem {
	bus_size_t		 dma_size;
	bus_dma_tag_t		 dma_tag;
	bus_dmamap_t		 dma_map;
	bus_dma_segment_t	*dma_seg;
	int			 dma_nsegs; /* total amount */
	int			 dma_rsegs; /* used amount */
	caddr_t			 dma_vaddr;
};

struct xbf_ccb {
	struct scsi_xfer	*ccb_xfer;  /* associated transfer */
	bus_dmamap_t		 ccb_dmap;  /* transfer map */
	struct xbf_dma_mem	 ccb_bbuf;  /* bounce buffer */
	uint32_t		 ccb_first; /* first descriptor */
	uint32_t		 ccb_last;  /* last descriptor */
	uint16_t		 ccb_want;  /* expected chunks */
	uint16_t		 ccb_seen;  /* completed chunks */
	TAILQ_ENTRY(xbf_ccb)	 ccb_link;
};
TAILQ_HEAD(xbf_ccb_queue, xbf_ccb);

struct xbf_softc {
	struct device		 sc_dev;
	struct device		*sc_parent;
	char			 sc_node[XEN_MAX_NODE_LEN];
	char			 sc_backend[XEN_MAX_BACKEND_LEN];
	bus_dma_tag_t		 sc_dmat;
	int			 sc_domid;

	xen_intr_handle_t	 sc_xih;

	int			 sc_state;
#define  XBF_CONNECTED		  4
#define  XBF_CLOSING		  5

	int			 sc_caps;
#define  XBF_CAP_BARRIER	  0x0001
#define  XBF_CAP_FLUSH		  0x0002

	uint32_t		 sc_type;
	uint32_t		 sc_unit;
	char			 sc_dtype[16];
	char			 sc_prod[16];

	uint64_t		 sc_disk_size;
	uint32_t		 sc_block_size;

	/* Ring */
	struct xbf_ring		*sc_xr;
	uint32_t		 sc_xr_cons;
	uint32_t		 sc_xr_prod;
	uint32_t		 sc_xr_size; /* in pages */
	struct xbf_dma_mem	 sc_xr_dma;
	uint32_t		 sc_xr_ref[XBF_MAX_RING_SIZE];
	int			 sc_xr_ndesc;

	/* Maximum number of blocks that one descriptor may refer to */
	int			 sc_xrd_nblk;

	/* CCBs */
	int			 sc_nccb;
	struct xbf_ccb		*sc_ccbs;
	struct xbf_ccb_queue	 sc_ccb_fq; /* free queue */
	struct xbf_ccb_queue	 sc_ccb_sq; /* pending requests */
	struct mutex		 sc_ccb_fqlck;
	struct mutex		 sc_ccb_sqlck;

	struct scsi_iopool	 sc_iopool;
	struct device		*sc_scsibus;
};

int	xbf_match(struct device *, void *, void *);
void	xbf_attach(struct device *, struct device *, void *);
int	xbf_detach(struct device *, int);

struct cfdriver xbf_cd = {
	NULL, "xbf", DV_DULL
};

const struct cfattach xbf_ca = {
	sizeof(struct xbf_softc), xbf_match, xbf_attach, xbf_detach
};

void	xbf_intr(void *);

int	xbf_load_cmd(struct scsi_xfer *);
int	xbf_bounce_cmd(struct scsi_xfer *);
void	xbf_reclaim_cmd(struct scsi_xfer *);

void	xbf_scsi_cmd(struct scsi_xfer *);
int	xbf_submit_cmd(struct scsi_xfer *);
int	xbf_poll_cmd(struct scsi_xfer *);
void	xbf_complete_cmd(struct xbf_softc *, struct xbf_ccb_queue *, int);

const struct scsi_adapter xbf_switch = {
	xbf_scsi_cmd, NULL, NULL, NULL, NULL
};

void	xbf_scsi_inq(struct scsi_xfer *);
void	xbf_scsi_inquiry(struct scsi_xfer *);
void	xbf_scsi_capacity(struct scsi_xfer *);
void	xbf_scsi_capacity16(struct scsi_xfer *);
void	xbf_scsi_done(struct scsi_xfer *, int);

int	xbf_dma_alloc(struct xbf_softc *, struct xbf_dma_mem *,
	    bus_size_t, int, int);
void	xbf_dma_free(struct xbf_softc *, struct xbf_dma_mem *);

int	xbf_get_type(struct xbf_softc *);
int	xbf_init(struct xbf_softc *);
int	xbf_ring_create(struct xbf_softc *);
void	xbf_ring_destroy(struct xbf_softc *);
void	xbf_stop(struct xbf_softc *);

int	xbf_alloc_ccbs(struct xbf_softc *);
void	xbf_free_ccbs(struct xbf_softc *);
void	*xbf_get_ccb(void *);
void	xbf_put_ccb(void *, void *);

int
xbf_match(struct device *parent, void *match, void *aux)
{
	struct xen_attach_args *xa = aux;

	if (strcmp("vbd", xa->xa_name))
		return (0);

	return (1);
}

void
xbf_attach(struct device *parent, struct device *self, void *aux)
{
	struct xen_attach_args *xa = aux;
	struct xbf_softc *sc = (struct xbf_softc *)self;
	struct scsibus_attach_args saa;

	sc->sc_parent = parent;
	sc->sc_dmat = xa->xa_dmat;
	sc->sc_domid = xa->xa_domid;

	memcpy(sc->sc_node, xa->xa_node, XEN_MAX_NODE_LEN);
	memcpy(sc->sc_backend, xa->xa_backend, XEN_MAX_BACKEND_LEN);

	if (xbf_get_type(sc))
		return;

	if (xen_intr_establish(0, &sc->sc_xih, sc->sc_domid, xbf_intr, sc,
	    sc->sc_dev.dv_xname)) {
		printf(": failed to establish an interrupt\n");
		return;
	}
	xen_intr_mask(sc->sc_xih);

	printf(" backend %d channel %u: %s\n", sc->sc_domid, sc->sc_xih,
	    sc->sc_dtype);

	if (xbf_init(sc))
		goto error;

	if (xen_intr_unmask(sc->sc_xih)) {
		printf("%s: failed to enable interrupts\n",
		    sc->sc_dev.dv_xname);
		goto error;
	}

	saa.saa_adapter = &xbf_switch;
	saa.saa_adapter_softc = self;
	saa.saa_adapter_buswidth = 1;
	saa.saa_luns = 1;
	saa.saa_adapter_target = SDEV_NO_ADAPTER_TARGET;
	saa.saa_openings = sc->sc_nccb;
	saa.saa_pool = &sc->sc_iopool;
	saa.saa_quirks = saa.saa_flags = 0;
	saa.saa_wwpn = saa.saa_wwnn = 0;

	sc->sc_scsibus = config_found(self, &saa, scsiprint);

	xen_unplug_emulated(parent, XEN_UNPLUG_IDE | XEN_UNPLUG_IDESEC);

	return;

 error:
	xen_intr_disestablish(sc->sc_xih);
}

int
xbf_detach(struct device *self, int flags)
{
	struct xbf_softc *sc = (struct xbf_softc *)self;
	int ostate = sc->sc_state;

	sc->sc_state = XBF_CLOSING;

	xen_intr_mask(sc->sc_xih);
	xen_intr_barrier(sc->sc_xih);

	if (ostate == XBF_CONNECTED) {
		xen_intr_disestablish(sc->sc_xih);
		xbf_stop(sc);
	}

	if (sc->sc_scsibus)
		return (config_detach(sc->sc_scsibus, flags | DETACH_FORCE));

	return (0);
}

void
xbf_intr(void *xsc)
{
	struct xbf_softc *sc = xsc;
	struct xbf_ring *xr = sc->sc_xr;
	struct xbf_dma_mem *dma = &sc->sc_xr_dma;
	struct xbf_ccb_queue cq;
	struct xbf_ccb *ccb, *nccb;
	uint32_t cons;
	int desc, s;

	TAILQ_INIT(&cq);

	for (;;) {
		bus_dmamap_sync(dma->dma_tag, dma->dma_map, 0, dma->dma_size,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

		for (cons = sc->sc_xr_cons; cons != xr->xr_cons; cons++) {
			desc = cons & (sc->sc_xr_ndesc - 1);
			xbf_complete_cmd(sc, &cq, desc);
		}

		sc->sc_xr_cons = cons;

		if (TAILQ_EMPTY(&cq))
			break;

		s = splbio();
		KERNEL_LOCK();
		TAILQ_FOREACH_SAFE(ccb, &cq, ccb_link, nccb) {
			TAILQ_REMOVE(&cq, ccb, ccb_link);
			xbf_reclaim_cmd(ccb->ccb_xfer);
			scsi_done(ccb->ccb_xfer);
		}
		KERNEL_UNLOCK();
		splx(s);
	}
}

void
xbf_scsi_cmd(struct scsi_xfer *xs)
{
	struct xbf_softc *sc = xs->sc_link->bus->sb_adapter_softc;

	switch (xs->cmd.opcode) {
	case READ_COMMAND:
	case READ_10:
	case READ_12:
	case READ_16:
	case WRITE_COMMAND:
	case WRITE_10:
	case WRITE_12:
	case WRITE_16:
		if (sc->sc_state != XBF_CONNECTED) {
			xbf_scsi_done(xs, XS_SELTIMEOUT);
			return;
		}
		break;
	case SYNCHRONIZE_CACHE:
		if (!(sc->sc_caps & (XBF_CAP_BARRIER|XBF_CAP_FLUSH))) {
			xbf_scsi_done(xs, XS_NOERROR);
			return;
		}
		break;
	case INQUIRY:
		xbf_scsi_inq(xs);
		return;
	case READ_CAPACITY:
		xbf_scsi_capacity(xs);
		return;
	case READ_CAPACITY_16:
		xbf_scsi_capacity16(xs);
		return;
	case TEST_UNIT_READY:
	case START_STOP:
	case PREVENT_ALLOW:
		xbf_scsi_done(xs, XS_NOERROR);
		return;
	default:
		printf("%s cmd 0x%02x\n", __func__, xs->cmd.opcode);
	case MODE_SENSE:
	case MODE_SENSE_BIG:
	case REPORT_LUNS:
	case READ_TOC:
		xbf_scsi_done(xs, XS_DRIVER_STUFFUP);
		return;
	}

	if (xbf_submit_cmd(xs)) {
		xbf_scsi_done(xs, XS_DRIVER_STUFFUP);
		return;
	}

	if (ISSET(xs->flags, SCSI_POLL) && xbf_poll_cmd(xs)) {
		printf("%s: op %#x timed out\n", sc->sc_dev.dv_xname,
		    xs->cmd.opcode);
		if (sc->sc_state == XBF_CONNECTED) {
			xbf_reclaim_cmd(xs);
			xbf_scsi_done(xs, XS_TIMEOUT);
		}
		return;
	}
}

int
xbf_load_cmd(struct scsi_xfer *xs)
{
	struct xbf_softc *sc = xs->sc_link->bus->sb_adapter_softc;
	struct xbf_ccb *ccb = xs->io;
	struct xbf_sge *sge;
	union xbf_ring_desc *xrd;
	bus_dmamap_t map;
	int error, mapflags, nsg, seg;
	int desc, ndesc = 0;

	map = ccb->ccb_dmap;

	mapflags = (sc->sc_domid << 16);
	if (ISSET(xs->flags, SCSI_NOSLEEP))
		mapflags |= BUS_DMA_NOWAIT;
	else
		mapflags |= BUS_DMA_WAITOK;
	if (ISSET(xs->flags, SCSI_DATA_IN))
		mapflags |= BUS_DMA_READ;
	else
		mapflags |= BUS_DMA_WRITE;

	error = bus_dmamap_load(sc->sc_dmat, map, xs->data, xs->datalen,
	    NULL, mapflags);
	if (error) {
		printf("%s: failed to load %d bytes of data\n",
		    sc->sc_dev.dv_xname, xs->datalen);
		return (error);
	}

	xrd = &sc->sc_xr->xr_desc[ccb->ccb_first];
	/* seg is the segment map iterator, nsg is the s-g list iterator */
	for (seg = 0, nsg = 0; seg < map->dm_nsegs; seg++, nsg++) {
		if (nsg == XBF_MAX_SGE) {
			/* Number of segments so far */
			xrd->xrd_req.req_nsegs = nsg;
			/* Pick next descriptor */
			ndesc++;
			desc = (sc->sc_xr_prod + ndesc) & (sc->sc_xr_ndesc - 1);
			xrd = &sc->sc_xr->xr_desc[desc];
			nsg = 0;
		}
		sge = &xrd->xrd_req.req_sgl[nsg];
		sge->sge_ref = map->dm_segs[seg].ds_addr;
		sge->sge_first = nsg > 0 ? 0 :
		    (((vaddr_t)xs->data + ndesc * sc->sc_xrd_nblk *
			(1 << XBF_SEC_SHIFT)) & PAGE_MASK) >> XBF_SEC_SHIFT;
		sge->sge_last = sge->sge_first +
		    (map->dm_segs[seg].ds_len >> XBF_SEC_SHIFT) - 1;

		DPRINTF("%s:   seg %d/%d ref %lu len %lu first %u last %u\n",
		    sc->sc_dev.dv_xname, nsg + 1, map->dm_nsegs,
		    map->dm_segs[seg].ds_addr, map->dm_segs[seg].ds_len,
		    sge->sge_first, sge->sge_last);

		KASSERT(sge->sge_last <= 7);
	}

	xrd->xrd_req.req_nsegs = nsg;

	return (0);
}

int
xbf_bounce_cmd(struct scsi_xfer *xs)
{
	struct xbf_softc *sc = xs->sc_link->bus->sb_adapter_softc;
	struct xbf_ccb *ccb = xs->io;
	struct xbf_sge *sge;
	struct xbf_dma_mem *dma;
	union xbf_ring_desc *xrd;
	bus_dmamap_t map;
	bus_size_t size;
	int error, mapflags, nsg, seg;
	int desc, ndesc = 0;

	size = roundup(xs->datalen, PAGE_SIZE);
	if (size > MAXPHYS)
		return (EFBIG);

	mapflags = (sc->sc_domid << 16);
	if (ISSET(xs->flags, SCSI_NOSLEEP))
		mapflags |= BUS_DMA_NOWAIT;
	else
		mapflags |= BUS_DMA_WAITOK;
	if (ISSET(xs->flags, SCSI_DATA_IN))
		mapflags |= BUS_DMA_READ;
	else
		mapflags |= BUS_DMA_WRITE;

	dma = &ccb->ccb_bbuf;
	error = xbf_dma_alloc(sc, dma, size, size / PAGE_SIZE, mapflags);
	if (error) {
		DPRINTF("%s: failed to allocate a %lu byte bounce buffer\n",
		    sc->sc_dev.dv_xname, size);
		return (error);
	}

	map = dma->dma_map;

	DPRINTF("%s: bouncing %d bytes via %lu size map with %d segments\n",
	    sc->sc_dev.dv_xname, xs->datalen, size, map->dm_nsegs);

	if (ISSET(xs->flags, SCSI_DATA_OUT))
		memcpy(dma->dma_vaddr, xs->data, xs->datalen);

	xrd = &sc->sc_xr->xr_desc[ccb->ccb_first];
	/* seg is the map segment iterator, nsg is the s-g element iterator */
	for (seg = 0, nsg = 0; seg < map->dm_nsegs; seg++, nsg++) {
		if (nsg == XBF_MAX_SGE) {
			/* Number of segments so far */
			xrd->xrd_req.req_nsegs = nsg;
			/* Pick next descriptor */
			ndesc++;
			desc = (sc->sc_xr_prod + ndesc) & (sc->sc_xr_ndesc - 1);
			xrd = &sc->sc_xr->xr_desc[desc];
			nsg = 0;
		}
		sge = &xrd->xrd_req.req_sgl[nsg];
		sge->sge_ref = map->dm_segs[seg].ds_addr;
		sge->sge_first = nsg > 0 ? 0 :
		    (((vaddr_t)dma->dma_vaddr + ndesc * sc->sc_xrd_nblk *
			(1 << XBF_SEC_SHIFT)) & PAGE_MASK) >> XBF_SEC_SHIFT;
		sge->sge_last = sge->sge_first +
		    (map->dm_segs[seg].ds_len >> XBF_SEC_SHIFT) - 1;

		DPRINTF("%s:   seg %d/%d ref %lu len %lu first %u last %u\n",
		    sc->sc_dev.dv_xname, nsg + 1, map->dm_nsegs,
		    map->dm_segs[seg].ds_addr, map->dm_segs[seg].ds_len,
		    sge->sge_first, sge->sge_last);

		KASSERT(sge->sge_last <= 7);
	}

	xrd->xrd_req.req_nsegs = nsg;

	return (0);
}

void
xbf_reclaim_cmd(struct scsi_xfer *xs)
{
	struct xbf_softc *sc = xs->sc_link->bus->sb_adapter_softc;
	struct xbf_ccb *ccb = xs->io;
	struct xbf_dma_mem *dma = &ccb->ccb_bbuf;

	if (dma->dma_size == 0)
		return;

	if (ISSET(xs->flags, SCSI_DATA_IN))
		memcpy(xs->data, (caddr_t)dma->dma_vaddr, xs->datalen);

	xbf_dma_free(sc, &ccb->ccb_bbuf);
}

int
xbf_submit_cmd(struct scsi_xfer *xs)
{
	struct xbf_softc *sc = xs->sc_link->bus->sb_adapter_softc;
	struct xbf_ccb *ccb = xs->io;
	union xbf_ring_desc *xrd;
	struct scsi_rw *rw;
	struct scsi_rw_10 *rw10;
	struct scsi_rw_12 *rw12;
	struct scsi_rw_16 *rw16;
	uint64_t lba = 0;
	uint32_t nblk = 0;
	uint8_t operation = 0;
	unsigned int ndesc = 0;
	int desc, error;

	switch (xs->cmd.opcode) {
	case READ_COMMAND:
	case READ_10:
	case READ_12:
	case READ_16:
		operation = XBF_OP_READ;
		break;

	case WRITE_COMMAND:
	case WRITE_10:
	case WRITE_12:
	case WRITE_16:
		operation = XBF_OP_WRITE;
		break;

	case SYNCHRONIZE_CACHE:
		if (sc->sc_caps & XBF_CAP_FLUSH)
			operation = XBF_OP_FLUSH;
		else if (sc->sc_caps & XBF_CAP_BARRIER)
			operation = XBF_OP_BARRIER;
		break;
	}

	/*
	 * READ/WRITE/SYNCHRONIZE commands. SYNCHRONIZE CACHE
	 * has the same layout as 10-byte READ/WRITE commands.
	 */
	if (xs->cmdlen == 6) {
		rw = (struct scsi_rw *)&xs->cmd;
		lba = _3btol(rw->addr) & (SRW_TOPADDR << 16 | 0xffff);
		nblk = rw->length ? rw->length : 0x100;
	} else if (xs->cmdlen == 10) {
		rw10 = (struct scsi_rw_10 *)&xs->cmd;
		lba = _4btol(rw10->addr);
		nblk = _2btol(rw10->length);
	} else if (xs->cmdlen == 12) {
		rw12 = (struct scsi_rw_12 *)&xs->cmd;
		lba = _4btol(rw12->addr);
		nblk = _4btol(rw12->length);
	} else if (xs->cmdlen == 16) {
		rw16 = (struct scsi_rw_16 *)&xs->cmd;
		lba = _8btol(rw16->addr);
		nblk = _4btol(rw16->length);
	}

	/* SCSI lba/nblk are sc_block_size. ccb's need XBF_SEC_SIZE. */
	lba *= sc->sc_block_size / XBF_SEC_SIZE;
	nblk *= sc->sc_block_size / XBF_SEC_SIZE;

	ccb->ccb_want = ccb->ccb_seen = 0;

	do {
		desc = (sc->sc_xr_prod + ndesc) & (sc->sc_xr_ndesc - 1);
		if (ndesc == 0)
			ccb->ccb_first = desc;

		xrd = &sc->sc_xr->xr_desc[desc];
		xrd->xrd_req.req_op = operation;
		xrd->xrd_req.req_unit = (uint16_t)sc->sc_unit;
		xrd->xrd_req.req_sector = lba + ndesc * sc->sc_xrd_nblk;

		ccb->ccb_want |= 1 << ndesc;
		ndesc++;
	} while (ndesc * sc->sc_xrd_nblk < nblk);

	ccb->ccb_last = desc;

	if (operation == XBF_OP_READ || operation == XBF_OP_WRITE) {
		DPRINTF("%s: desc %u,%u %s%s lba %llu nsec %u "
		    "len %d\n", sc->sc_dev.dv_xname, ccb->ccb_first,
		    ccb->ccb_last, operation == XBF_OP_READ ? "read" :
		    "write", ISSET(xs->flags, SCSI_POLL) ? "-poll" : "",
		    lba, nblk, xs->datalen);

		if (((vaddr_t)xs->data & ((1 << XBF_SEC_SHIFT) - 1)) == 0)
			error = xbf_load_cmd(xs);
		else
			error = xbf_bounce_cmd(xs);
		if (error)
			return (-1);
	} else {
		DPRINTF("%s: desc %u %s%s lba %llu\n", sc->sc_dev.dv_xname,
		    ccb->ccb_first, operation == XBF_OP_FLUSH ? "flush" :
		    "barrier", ISSET(xs->flags, SCSI_POLL) ? "-poll" : "",
		    lba);
		xrd->xrd_req.req_nsegs = 0;
	}

	ccb->ccb_xfer = xs;

	bus_dmamap_sync(sc->sc_dmat, ccb->ccb_dmap, 0,
	    ccb->ccb_dmap->dm_mapsize, BUS_DMASYNC_PREREAD |
	    BUS_DMASYNC_PREWRITE);

	mtx_enter(&sc->sc_ccb_sqlck);
	TAILQ_INSERT_TAIL(&sc->sc_ccb_sq, ccb, ccb_link);
	mtx_leave(&sc->sc_ccb_sqlck);

	sc->sc_xr_prod += ndesc;
	sc->sc_xr->xr_prod = sc->sc_xr_prod;
	sc->sc_xr->xr_cons_event = sc->sc_xr_prod;

	bus_dmamap_sync(sc->sc_dmat, sc->sc_xr_dma.dma_map, 0,
	    sc->sc_xr_dma.dma_map->dm_mapsize, BUS_DMASYNC_PREREAD |
	    BUS_DMASYNC_PREWRITE);

	xen_intr_signal(sc->sc_xih);

	return (0);
}

int
xbf_poll_cmd(struct scsi_xfer *xs)
{
	int timo = 1000;

	do {
		if (ISSET(xs->flags, ITSDONE))
			break;
		if (ISSET(xs->flags, SCSI_NOSLEEP))
			delay(10);
		else
			tsleep_nsec(xs, PRIBIO, "xbfpoll", USEC_TO_NSEC(10));
		xbf_intr(xs->sc_link->bus->sb_adapter_softc);
	} while(--timo > 0);

	return (0);
}

void
xbf_complete_cmd(struct xbf_softc *sc, struct xbf_ccb_queue *cq, int desc)
{
	struct xbf_ccb *ccb;
	union xbf_ring_desc *xrd;
	bus_dmamap_t map;
	uint32_t id, chunk;
	int error;

	xrd = &sc->sc_xr->xr_desc[desc];
	error = xrd->xrd_rsp.rsp_status == XBF_OK ? XS_NOERROR :
	    XS_DRIVER_STUFFUP;

	mtx_enter(&sc->sc_ccb_sqlck);

	/*
	 * To find a CCB for id equal to x within an interval [a, b] we must
	 * locate a CCB such that (x - a) mod N <= (b - a) mod N, where a is
	 * the first descriptor, b is the last one and N is the ring size.
	 */
	id = (uint32_t)xrd->xrd_rsp.rsp_id;
	TAILQ_FOREACH(ccb, &sc->sc_ccb_sq, ccb_link) {
		if (((id - ccb->ccb_first) & (sc->sc_xr_ndesc - 1)) <=
		    ((ccb->ccb_last - ccb->ccb_first) & (sc->sc_xr_ndesc - 1)))
			break;
	}
	KASSERT(ccb != NULL);

	/* Assert that this chunk belongs to this CCB */
	chunk = 1 << ((id - ccb->ccb_first) & (sc->sc_xr_ndesc - 1));
	KASSERT((ccb->ccb_want & chunk) != 0);
	KASSERT((ccb->ccb_seen & chunk) == 0);

	/* When all chunks are collected remove the CCB from the queue */
	ccb->ccb_seen |= chunk;
	if (ccb->ccb_seen == ccb->ccb_want)
		TAILQ_REMOVE(&sc->sc_ccb_sq, ccb, ccb_link);

	mtx_leave(&sc->sc_ccb_sqlck);

	DPRINTF("%s: completing desc %d(%llu) op %u with error %d\n",
	    sc->sc_dev.dv_xname, desc, xrd->xrd_rsp.rsp_id,
	    xrd->xrd_rsp.rsp_op, xrd->xrd_rsp.rsp_status);

	memset(xrd, 0, sizeof(*xrd));
	xrd->xrd_req.req_id = desc;

	if (ccb->ccb_seen != ccb->ccb_want)
		return;

	if (ccb->ccb_bbuf.dma_size > 0)
		map = ccb->ccb_bbuf.dma_map;
	else
		map = ccb->ccb_dmap;

	bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(sc->sc_dmat, map);

	ccb->ccb_xfer->resid = 0;
	ccb->ccb_xfer->error = error;
	TAILQ_INSERT_TAIL(cq, ccb, ccb_link);
}

void
xbf_scsi_inq(struct scsi_xfer *xs)
{
	struct scsi_inquiry *inq = (struct scsi_inquiry *)&xs->cmd;

	if (ISSET(inq->flags, SI_EVPD))
		xbf_scsi_done(xs, XS_DRIVER_STUFFUP);
	else
		xbf_scsi_inquiry(xs);
}

void
xbf_scsi_inquiry(struct scsi_xfer *xs)
{
	struct xbf_softc *sc = xs->sc_link->bus->sb_adapter_softc;
	struct scsi_inquiry_data inq;

	bzero(&inq, sizeof(inq));

	switch (sc->sc_type) {
	case XBF_CDROM:
		inq.device = T_CDROM;
		break;
	default:
		inq.device = T_DIRECT;
		break;
	}

	inq.version = SCSI_REV_SPC3;
	inq.response_format = SID_SCSI2_RESPONSE;
	inq.additional_length = SID_SCSI2_ALEN;
	inq.flags |= SID_CmdQue;
	bcopy("Xen     ", inq.vendor, sizeof(inq.vendor));
	bcopy(sc->sc_prod, inq.product, sizeof(inq.product));
	bcopy("0000", inq.revision, sizeof(inq.revision));

	scsi_copy_internal_data(xs, &inq, sizeof(inq));

	xbf_scsi_done(xs, XS_NOERROR);
}

void
xbf_scsi_capacity(struct scsi_xfer *xs)
{
	struct xbf_softc *sc = xs->sc_link->bus->sb_adapter_softc;
	struct scsi_read_cap_data rcd;
	uint64_t capacity;

	bzero(&rcd, sizeof(rcd));

	/* [addr|length] are sc_block_size. sc->sc_disk_size is XBF_SEC_SIZE. */
	capacity = (sc->sc_disk_size * XBF_SEC_SIZE) / sc->sc_block_size - 1;
	if (capacity > 0xffffffff)
		capacity = 0xffffffff;

	_lto4b(capacity, rcd.addr);
	_lto4b(sc->sc_block_size, rcd.length);

	bcopy(&rcd, xs->data, MIN(sizeof(rcd), xs->datalen));

	xbf_scsi_done(xs, XS_NOERROR);
}

void
xbf_scsi_capacity16(struct scsi_xfer *xs)
{
	struct xbf_softc *sc = xs->sc_link->bus->sb_adapter_softc;
	struct scsi_read_cap_data_16 rcd;
	uint64_t capacity;

	bzero(&rcd, sizeof(rcd));

	/* [addr|length] are sc_block_size. sc->sc_disk_size is XBF_SEC_SIZE. */
	capacity = (sc->sc_disk_size * XBF_SEC_SIZE) / sc->sc_block_size - 1;
	_lto8b(capacity, rcd.addr);
	_lto4b(sc->sc_block_size, rcd.length);

	bcopy(&rcd, xs->data, MIN(sizeof(rcd), xs->datalen));

	xbf_scsi_done(xs, XS_NOERROR);
}

void
xbf_scsi_done(struct scsi_xfer *xs, int error)
{
	int s;

	xs->error = error;

	s = splbio();
	scsi_done(xs);
	splx(s);
}

int
xbf_get_type(struct xbf_softc *sc)
{
	unsigned long long res;
	const char *prop;
	char val[32];
	int error;

	prop = "type";
	if ((error = xs_getprop(sc->sc_parent, sc->sc_backend, prop, val,
	    sizeof(val))) != 0)
		goto errout;
	snprintf(sc->sc_prod, sizeof(sc->sc_prod), "%s", val);

	prop = "dev";
	if ((error = xs_getprop(sc->sc_parent, sc->sc_backend, prop, val,
	    sizeof(val))) != 0)
		goto errout;
	snprintf(sc->sc_prod, sizeof(sc->sc_prod), "%s %s", sc->sc_prod, val);

	prop = "virtual-device";
	if ((error = xs_getnum(sc->sc_parent, sc->sc_node, prop, &res)) != 0)
		goto errout;
	sc->sc_unit = (uint32_t)res;
	snprintf(sc->sc_prod, sizeof(sc->sc_prod), "%s %llu", sc->sc_prod, res);

	prop = "device-type";
	if ((error = xs_getprop(sc->sc_parent, sc->sc_node, prop,
	    sc->sc_dtype, sizeof(sc->sc_dtype))) != 0)
		goto errout;
	if (!strcmp(sc->sc_dtype, "cdrom"))
		sc->sc_type = XBF_CDROM;

	return (0);

 errout:
	printf("%s: failed to read \"%s\" property\n", sc->sc_dev.dv_xname,
	    prop);
	return (-1);
}

int
xbf_init(struct xbf_softc *sc)
{
	unsigned long long res;
	const char *action, *prop;
	char pbuf[sizeof("ring-refXX")];
	unsigned int i;
	int error;

	prop = "max-ring-page-order";
	error = xs_getnum(sc->sc_parent, sc->sc_backend, prop, &res);
	if (error == 0)
		sc->sc_xr_size = 1 << res;
	if (error == ENOENT) {
		prop = "max-ring-pages";
		error = xs_getnum(sc->sc_parent, sc->sc_backend, prop, &res);
		if (error == 0)
			sc->sc_xr_size = res;
	}
	/* Fallback to the known minimum */
	if (error)
		sc->sc_xr_size = XBF_MIN_RING_SIZE;

	if (sc->sc_xr_size < XBF_MIN_RING_SIZE)
		sc->sc_xr_size = XBF_MIN_RING_SIZE;
	if (sc->sc_xr_size > XBF_MAX_RING_SIZE)
		sc->sc_xr_size = XBF_MAX_RING_SIZE;
	if (!powerof2(sc->sc_xr_size))
		sc->sc_xr_size = 1 << (fls(sc->sc_xr_size) - 1);

	sc->sc_xr_ndesc = ((sc->sc_xr_size * PAGE_SIZE) -
	    sizeof(struct xbf_ring)) / sizeof(union xbf_ring_desc);
	if (!powerof2(sc->sc_xr_ndesc))
		sc->sc_xr_ndesc = 1 << (fls(sc->sc_xr_ndesc) - 1);
	if (sc->sc_xr_ndesc > XBF_MAX_REQS)
		sc->sc_xr_ndesc = XBF_MAX_REQS;

	DPRINTF("%s: %u ring pages, %d requests\n",
	    sc->sc_dev.dv_xname, sc->sc_xr_size, sc->sc_xr_ndesc);

	if (xbf_ring_create(sc))
		return (-1);

	action = "set";

	for (i = 0; i < sc->sc_xr_size; i++) {
		if (i == 0 && sc->sc_xr_size == 1)
			snprintf(pbuf, sizeof(pbuf), "ring-ref");
		else
			snprintf(pbuf, sizeof(pbuf), "ring-ref%d", i);
		prop = pbuf;
		if (xs_setnum(sc->sc_parent, sc->sc_node, prop,
		    sc->sc_xr_ref[i]))
			goto errout;
	}

	if (sc->sc_xr_size > 1) {
		prop = "num-ring-pages";
		if (xs_setnum(sc->sc_parent, sc->sc_node, prop,
		    sc->sc_xr_size))
			goto errout;
		prop = "ring-page-order";
		if (xs_setnum(sc->sc_parent, sc->sc_node, prop,
		    fls(sc->sc_xr_size) - 1))
			goto errout;
	}

	prop = "event-channel";
	if (xs_setnum(sc->sc_parent, sc->sc_node, prop, sc->sc_xih))
		goto errout;

	prop = "protocol";
#ifdef __amd64__
	if (xs_setprop(sc->sc_parent, sc->sc_node, prop, "x86_64-abi",
	    strlen("x86_64-abi")))
		goto errout;
#else
	if (xs_setprop(sc->sc_parent, sc->sc_node, prop, "x86_32-abi",
	    strlen("x86_32-abi")))
		goto errout;
#endif

	if (xs_setprop(sc->sc_parent, sc->sc_node, "state",
	    XEN_STATE_INITIALIZED, strlen(XEN_STATE_INITIALIZED))) {
		printf("%s: failed to set state to INITIALIZED\n",
		    sc->sc_dev.dv_xname);
		xbf_ring_destroy(sc);
		return (-1);
	}

	if (xs_await_transition(sc->sc_parent, sc->sc_backend, "state",
	    XEN_STATE_CONNECTED, 10000)) {
		printf("%s: timed out waiting for backend to connect\n",
		    sc->sc_dev.dv_xname);
		xbf_ring_destroy(sc);
		return (-1);
	}

	action = "read";

	prop = "sectors";
	if ((error = xs_getnum(sc->sc_parent, sc->sc_backend, prop, &res)) != 0)
		goto errout;
	sc->sc_disk_size = res;

	prop = "sector-size";
	if ((error = xs_getnum(sc->sc_parent, sc->sc_backend, prop, &res)) != 0)
		goto errout;
	sc->sc_block_size = res;

	prop = "feature-barrier";
	if ((error = xs_getnum(sc->sc_parent, sc->sc_backend, prop, &res)) != 0
	    && error != ENOENT)
		goto errout;
	if (error == 0 && res == 1)
		sc->sc_caps |= XBF_CAP_BARRIER;

	prop = "feature-flush-cache";
	if ((error = xs_getnum(sc->sc_parent, sc->sc_backend, prop, &res)) != 0
	    && error != ENOENT)
		goto errout;
	if (error == 0 && res == 1)
		sc->sc_caps |= XBF_CAP_FLUSH;

#ifdef XBF_DEBUG
	if (sc->sc_caps) {
		printf("%s: features:", sc->sc_dev.dv_xname);
		if (sc->sc_caps & XBF_CAP_BARRIER)
			printf(" BARRIER");
		if (sc->sc_caps & XBF_CAP_FLUSH)
			printf(" FLUSH");
		printf("\n");
	}
#endif

	if (xs_setprop(sc->sc_parent, sc->sc_node, "state",
	    XEN_STATE_CONNECTED, strlen(XEN_STATE_CONNECTED))) {
		printf("%s: failed to set state to CONNECTED\n",
		    sc->sc_dev.dv_xname);
		return (-1);
	}

	sc->sc_state = XBF_CONNECTED;

	return (0);

 errout:
	printf("%s: failed to %s \"%s\" property (%d)\n", sc->sc_dev.dv_xname,
	    action, prop, error);
	xbf_ring_destroy(sc);
	return (-1);
}

int
xbf_dma_alloc(struct xbf_softc *sc, struct xbf_dma_mem *dma,
    bus_size_t size, int nsegs, int mapflags)
{
	int error;

	dma->dma_tag = sc->sc_dmat;

	dma->dma_seg = mallocarray(nsegs, sizeof(bus_dma_segment_t), M_DEVBUF,
	    M_ZERO | M_NOWAIT);
	if (dma->dma_seg == NULL) {
		printf("%s: failed to allocate a segment array\n",
		    sc->sc_dev.dv_xname);
		return (ENOMEM);
	}

	error = bus_dmamap_create(dma->dma_tag, size, nsegs, PAGE_SIZE, 0,
	    BUS_DMA_NOWAIT, &dma->dma_map);
	if (error) {
		printf("%s: failed to create a memory map (%d)\n",
		    sc->sc_dev.dv_xname, error);
		goto errout;
	}

	error = bus_dmamem_alloc(dma->dma_tag, size, PAGE_SIZE, 0,
	    dma->dma_seg, nsegs, &dma->dma_rsegs, BUS_DMA_ZERO |
	    BUS_DMA_NOWAIT);
	if (error) {
		printf("%s: failed to allocate DMA memory (%d)\n",
		    sc->sc_dev.dv_xname, error);
		goto destroy;
	}

	error = bus_dmamem_map(dma->dma_tag, dma->dma_seg, dma->dma_rsegs,
	    size, &dma->dma_vaddr, BUS_DMA_NOWAIT);
	if (error) {
		printf("%s: failed to map DMA memory (%d)\n",
		    sc->sc_dev.dv_xname, error);
		goto free;
	}

	error = bus_dmamap_load(dma->dma_tag, dma->dma_map, dma->dma_vaddr,
	    size, NULL, mapflags | BUS_DMA_NOWAIT);
	if (error) {
		printf("%s: failed to load DMA memory (%d)\n",
		    sc->sc_dev.dv_xname, error);
		goto unmap;
	}

	dma->dma_size = size;
	dma->dma_nsegs = nsegs;
	return (0);

 unmap:
	bus_dmamem_unmap(dma->dma_tag, dma->dma_vaddr, size);
 free:
	bus_dmamem_free(dma->dma_tag, dma->dma_seg, dma->dma_rsegs);
 destroy:
	bus_dmamap_destroy(dma->dma_tag, dma->dma_map);
 errout:
	free(dma->dma_seg, M_DEVBUF, nsegs * sizeof(bus_dma_segment_t));
	dma->dma_map = NULL;
	dma->dma_tag = NULL;
	return (error);
}

void
xbf_dma_free(struct xbf_softc *sc, struct xbf_dma_mem *dma)
{
	if (dma->dma_tag == NULL || dma->dma_map == NULL)
		return;
	bus_dmamap_sync(dma->dma_tag, dma->dma_map, 0, dma->dma_size,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(dma->dma_tag, dma->dma_map);
	bus_dmamem_unmap(dma->dma_tag, dma->dma_vaddr, dma->dma_size);
	bus_dmamem_free(dma->dma_tag, dma->dma_seg, dma->dma_rsegs);
	bus_dmamap_destroy(dma->dma_tag, dma->dma_map);
	free(dma->dma_seg, M_DEVBUF, dma->dma_nsegs * sizeof(bus_dma_segment_t));
	dma->dma_seg = NULL;
	dma->dma_map = NULL;
	dma->dma_size = 0;
}

int
xbf_ring_create(struct xbf_softc *sc)
{
	int i;

	if (xbf_dma_alloc(sc, &sc->sc_xr_dma, sc->sc_xr_size * PAGE_SIZE,
	    sc->sc_xr_size, sc->sc_domid << 16))
		return (-1);
	for (i = 0; i < sc->sc_xr_dma.dma_map->dm_nsegs; i++)
		sc->sc_xr_ref[i] = sc->sc_xr_dma.dma_map->dm_segs[i].ds_addr;

	sc->sc_xr = (struct xbf_ring *)sc->sc_xr_dma.dma_vaddr;

	sc->sc_xr->xr_prod_event = sc->sc_xr->xr_cons_event = 1;

	for (i = 0; i < sc->sc_xr_ndesc; i++)
		sc->sc_xr->xr_desc[i].xrd_req.req_id = i;

	/* The number of contiguous blocks addressable by one descriptor */
	sc->sc_xrd_nblk = (PAGE_SIZE * XBF_MAX_SGE) / (1 << XBF_SEC_SHIFT);

	if (xbf_alloc_ccbs(sc)) {
		xbf_ring_destroy(sc);
		return (-1);
	}

	return (0);
}

void
xbf_ring_destroy(struct xbf_softc *sc)
{
	xbf_free_ccbs(sc);
	xbf_dma_free(sc, &sc->sc_xr_dma);
	sc->sc_xr = NULL;
}

void
xbf_stop(struct xbf_softc *sc)
{
	struct xbf_ccb *ccb, *nccb;
	bus_dmamap_t map;

	bus_dmamap_sync(sc->sc_dmat, sc->sc_xr_dma.dma_map, 0,
	    sc->sc_xr_dma.dma_map->dm_mapsize, BUS_DMASYNC_POSTREAD |
	    BUS_DMASYNC_POSTWRITE);

	TAILQ_FOREACH_SAFE(ccb, &sc->sc_ccb_sq, ccb_link, nccb) {
		TAILQ_REMOVE(&sc->sc_ccb_sq, ccb, ccb_link);

		if (ccb->ccb_bbuf.dma_size > 0)
			map = ccb->ccb_bbuf.dma_map;
		else
			map = ccb->ccb_dmap;
		bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, map);

		xbf_reclaim_cmd(ccb->ccb_xfer);
		xbf_scsi_done(ccb->ccb_xfer, XS_SELTIMEOUT);
	}

	xbf_ring_destroy(sc);
}

int
xbf_alloc_ccbs(struct xbf_softc *sc)
{
	int i, error;

	TAILQ_INIT(&sc->sc_ccb_fq);
	TAILQ_INIT(&sc->sc_ccb_sq);
	mtx_init(&sc->sc_ccb_fqlck, IPL_BIO);
	mtx_init(&sc->sc_ccb_sqlck, IPL_BIO);

	sc->sc_nccb = sc->sc_xr_ndesc / 2;

	sc->sc_ccbs = mallocarray(sc->sc_nccb, sizeof(struct xbf_ccb),
	    M_DEVBUF, M_ZERO | M_NOWAIT);
	if (sc->sc_ccbs == NULL) {
		printf("%s: failed to allocate CCBs\n", sc->sc_dev.dv_xname);
		return (-1);
	}

	for (i = 0; i < sc->sc_nccb; i++) {
		/*
		 * Each CCB is set up to use up to 2 descriptors and
		 * each descriptor can transfer XBF_MAX_SGE number of
		 * pages.
		 */
		error = bus_dmamap_create(sc->sc_dmat, MAXPHYS, 2 *
		    XBF_MAX_SGE, PAGE_SIZE, PAGE_SIZE, BUS_DMA_NOWAIT,
		    &sc->sc_ccbs[i].ccb_dmap);
		if (error) {
			printf("%s: failed to create a memory map for "
			    "the xfer %d (%d)\n", sc->sc_dev.dv_xname, i,
			    error);
			goto errout;
		}

		xbf_put_ccb(sc, &sc->sc_ccbs[i]);
	}

	scsi_iopool_init(&sc->sc_iopool, sc, xbf_get_ccb, xbf_put_ccb);

	return (0);

 errout:
	xbf_free_ccbs(sc);
	return (-1);
}

void
xbf_free_ccbs(struct xbf_softc *sc)
{
	struct xbf_ccb *ccb;
	int i;

	for (i = 0; i < sc->sc_nccb; i++) {
		ccb = &sc->sc_ccbs[i];
		if (ccb->ccb_dmap == NULL)
			continue;
		bus_dmamap_sync(sc->sc_dmat, ccb->ccb_dmap, 0, 0,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, ccb->ccb_dmap);
		bus_dmamap_destroy(sc->sc_dmat, ccb->ccb_dmap);
	}

	free(sc->sc_ccbs, M_DEVBUF, sc->sc_nccb * sizeof(struct xbf_ccb));
	sc->sc_ccbs = NULL;
	sc->sc_nccb = 0;
}

void *
xbf_get_ccb(void *xsc)
{
	struct xbf_softc *sc = xsc;
	struct xbf_ccb *ccb;

	if (sc->sc_state != XBF_CONNECTED &&
	    sc->sc_state != XBF_CLOSING)
		return (NULL);

	mtx_enter(&sc->sc_ccb_fqlck);
	ccb = TAILQ_FIRST(&sc->sc_ccb_fq);
	if (ccb != NULL)
		TAILQ_REMOVE(&sc->sc_ccb_fq, ccb, ccb_link);
	mtx_leave(&sc->sc_ccb_fqlck);

	return (ccb);
}

void
xbf_put_ccb(void *xsc, void *io)
{
	struct xbf_softc *sc = xsc;
	struct xbf_ccb *ccb = io;

	ccb->ccb_xfer = NULL;

	mtx_enter(&sc->sc_ccb_fqlck);
	TAILQ_INSERT_HEAD(&sc->sc_ccb_fq, ccb, ccb_link);
	mtx_leave(&sc->sc_ccb_fqlck);
}
