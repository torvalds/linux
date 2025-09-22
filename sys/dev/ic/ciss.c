/*	$OpenBSD: ciss.c,v 1.92 2024/04/14 03:26:25 jsg Exp $	*/

/*
 * Copyright (c) 2005,2006 Michael Shalayeff
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "bio.h"

/* #define CISS_DEBUG */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/ioctl.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/malloc.h>

#include <machine/bus.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/ic/cissreg.h>
#include <dev/ic/cissvar.h>

#if NBIO > 0
#include <dev/biovar.h>
#endif
#include <sys/sensors.h>

#ifdef CISS_DEBUG
#define	CISS_DPRINTF(m,a)	if (ciss_debug & (m)) printf a
#define	CISS_D_CMD	0x0001
#define	CISS_D_INTR	0x0002
#define	CISS_D_MISC	0x0004
#define	CISS_D_DMA	0x0008
#define	CISS_D_IOCTL	0x0010
#define	CISS_D_ERR	0x0020
int ciss_debug = 0
/*	| CISS_D_CMD */
/*	| CISS_D_INTR */
/*	| CISS_D_MISC */
/*	| CISS_D_DMA */
/*	| CISS_D_IOCTL */
/*	| CISS_D_ERR */
	;
#else
#define	CISS_DPRINTF(m,a)	/* m, a */
#endif

struct cfdriver ciss_cd = {
	NULL, "ciss", DV_DULL
};

void	ciss_scsi_cmd(struct scsi_xfer *xs);
int	ciss_scsi_ioctl(struct scsi_link *, u_long, caddr_t, int);

const struct scsi_adapter ciss_switch = {
	ciss_scsi_cmd, NULL, NULL, NULL, ciss_scsi_ioctl
};

#if NBIO > 0
int	ciss_ioctl(struct device *, u_long, caddr_t);
#endif
int	ciss_sync(struct ciss_softc *sc);
void	ciss_heartbeat(void *v);
#ifndef SMALL_KERNEL
void	ciss_sensors(void *);
#endif

void *	ciss_get_ccb(void *);
void	ciss_put_ccb(void *, void *);
int	ciss_cmd(struct ciss_ccb *ccb, int flags, int wait);
int	ciss_done(struct ciss_ccb *ccb);
int	ciss_error(struct ciss_ccb *ccb);

struct ciss_ld *ciss_pdscan(struct ciss_softc *sc, int ld);
int	ciss_inq(struct ciss_softc *sc, struct ciss_inquiry *inq);
int	ciss_ldmap(struct ciss_softc *sc);
int	ciss_ldid(struct ciss_softc *, int, struct ciss_ldid *);
int	ciss_ldstat(struct ciss_softc *, int, struct ciss_ldstat *);
int	ciss_pdid(struct ciss_softc *, u_int8_t, struct ciss_pdid *, int);
int	ciss_blink(struct ciss_softc *, int, int, int, struct ciss_blink *);

void *
ciss_get_ccb(void *xsc)
{
	struct ciss_softc *sc = xsc;
	struct ciss_ccb *ccb;

	mtx_enter(&sc->sc_free_ccb_mtx);
	ccb = SLIST_FIRST(&sc->sc_free_ccb);
	if (ccb != NULL) {
		SLIST_REMOVE_HEAD(&sc->sc_free_ccb, ccb_link);
		ccb->ccb_state = CISS_CCB_READY;
		ccb->ccb_xs = NULL;
	}
	mtx_leave(&sc->sc_free_ccb_mtx);

	return (ccb);
}

void
ciss_put_ccb(void *xsc, void *xccb)
{
	struct ciss_softc *sc = xsc;
	struct ciss_ccb *ccb = xccb;

	ccb->ccb_state = CISS_CCB_FREE;
	ccb->ccb_xs = NULL;
	ccb->ccb_data = NULL;

	mtx_enter(&sc->sc_free_ccb_mtx);
	SLIST_INSERT_HEAD(&sc->sc_free_ccb, ccb, ccb_link);
	mtx_leave(&sc->sc_free_ccb_mtx);
}

int
ciss_attach(struct ciss_softc *sc)
{
	struct scsibus_attach_args saa;
	struct scsibus_softc *scsibus;
	struct ciss_ccb *ccb;
	struct ciss_cmd *cmd;
	struct ciss_inquiry *inq;
	bus_dma_segment_t seg[1];
	int error, i, total, rseg, maxfer;
	ciss_lock_t lock;
	paddr_t pa;

	bus_space_read_region_4(sc->iot, sc->cfg_ioh, sc->cfgoff,
	    (u_int32_t *)&sc->cfg, sizeof(sc->cfg) / 4);

	if (sc->cfg.signature != CISS_SIGNATURE) {
		printf(": bad sign 0x%08x\n", sc->cfg.signature);
		return -1;
	}

	if (!(sc->cfg.methods & CISS_METH_SIMPL)) {
		printf(": not simple 0x%08x\n", sc->cfg.methods);
		return -1;
	}

	sc->cfg.rmethod = CISS_METH_SIMPL;
	sc->cfg.paddr_lim = 0;			/* 32bit addrs */
	sc->cfg.int_delay = 0;			/* disable coalescing */
	sc->cfg.int_count = 0;
	strlcpy(sc->cfg.hostname, "HUMPPA", sizeof(sc->cfg.hostname));
	sc->cfg.driverf |= CISS_DRV_PRF;	/* enable prefetch */
	if (!sc->cfg.maxsg)
		sc->cfg.maxsg = MAXPHYS / PAGE_SIZE;

	bus_space_write_region_4(sc->iot, sc->cfg_ioh, sc->cfgoff,
	    (u_int32_t *)&sc->cfg, sizeof(sc->cfg) / 4);
	bus_space_barrier(sc->iot, sc->cfg_ioh, sc->cfgoff, sizeof(sc->cfg),
	    BUS_SPACE_BARRIER_READ|BUS_SPACE_BARRIER_WRITE);

	bus_space_write_4(sc->iot, sc->ioh, CISS_IDB, CISS_IDB_CFG);
	bus_space_barrier(sc->iot, sc->ioh, CISS_IDB, 4,
	    BUS_SPACE_BARRIER_WRITE);
	for (i = 1000; i--; DELAY(1000)) {
		/* XXX maybe IDB is really 64bit? - hp dl380 needs this */
		(void)bus_space_read_4(sc->iot, sc->ioh, CISS_IDB + 4);
		if (!(bus_space_read_4(sc->iot, sc->ioh, CISS_IDB) & CISS_IDB_CFG))
			break;
		bus_space_barrier(sc->iot, sc->ioh, CISS_IDB, 4,
		    BUS_SPACE_BARRIER_READ);
	}

	if (bus_space_read_4(sc->iot, sc->ioh, CISS_IDB) & CISS_IDB_CFG) {
		printf(": cannot set config\n");
		return -1;
	}

	bus_space_read_region_4(sc->iot, sc->cfg_ioh, sc->cfgoff,
	    (u_int32_t *)&sc->cfg, sizeof(sc->cfg) / 4);

	if (!(sc->cfg.amethod & CISS_METH_SIMPL)) {
		printf(": cannot simplify 0x%08x\n", sc->cfg.amethod);
		return -1;
	}

	/* i'm ready for you and i hope you're ready for me */
	for (i = 30000; i--; DELAY(1000)) {
		if (bus_space_read_4(sc->iot, sc->cfg_ioh, sc->cfgoff +
		    offsetof(struct ciss_config, amethod)) & CISS_METH_READY)
			break;
		bus_space_barrier(sc->iot, sc->cfg_ioh, sc->cfgoff +
		    offsetof(struct ciss_config, amethod), 4,
		    BUS_SPACE_BARRIER_READ);
	}

	if (!(bus_space_read_4(sc->iot, sc->cfg_ioh, sc->cfgoff +
	    offsetof(struct ciss_config, amethod)) & CISS_METH_READY)) {
		printf(": she never came ready for me 0x%08x\n",
		    sc->cfg.amethod);
		return -1;
	}

	sc->maxcmd = sc->cfg.maxcmd;
	sc->maxsg = sc->cfg.maxsg;
	if (sc->maxsg > MAXPHYS / PAGE_SIZE)
		sc->maxsg = MAXPHYS / PAGE_SIZE;
	i = sizeof(struct ciss_ccb) +
	    sizeof(ccb->ccb_cmd.sgl[0]) * (sc->maxsg - 1);
	for (sc->ccblen = 0x10; sc->ccblen < i; sc->ccblen <<= 1)
		;

	total = sc->ccblen * sc->maxcmd;
	if ((error = bus_dmamem_alloc(sc->dmat, total, PAGE_SIZE, 0,
	    sc->cmdseg, 1, &rseg, BUS_DMA_NOWAIT | BUS_DMA_ZERO))) {
		printf(": cannot allocate CCBs (%d)\n", error);
		return -1;
	}

	if ((error = bus_dmamem_map(sc->dmat, sc->cmdseg, rseg, total,
	    (caddr_t *)&sc->ccbs, BUS_DMA_NOWAIT))) {
		printf(": cannot map CCBs (%d)\n", error);
		return -1;
	}

	if ((error = bus_dmamap_create(sc->dmat, total, 1,
	    total, 0, BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &sc->cmdmap))) {
		printf(": cannot create CCBs dmamap (%d)\n", error);
		bus_dmamem_free(sc->dmat, sc->cmdseg, 1);
		return -1;
	}

	if ((error = bus_dmamap_load(sc->dmat, sc->cmdmap, sc->ccbs, total,
	    NULL, BUS_DMA_NOWAIT))) {
		printf(": cannot load CCBs dmamap (%d)\n", error);
		bus_dmamem_free(sc->dmat, sc->cmdseg, 1);
		bus_dmamap_destroy(sc->dmat, sc->cmdmap);
		return -1;
	}

	SLIST_INIT(&sc->sc_free_ccb);
	mtx_init(&sc->sc_free_ccb_mtx, IPL_BIO);

	maxfer = sc->maxsg * PAGE_SIZE;
	for (i = 0; total; i++, total -= sc->ccblen) {
		ccb = sc->ccbs + i * sc->ccblen;
		cmd = &ccb->ccb_cmd;
		pa = sc->cmdseg[0].ds_addr + i * sc->ccblen;

		ccb->ccb_sc = sc;
		ccb->ccb_cmdpa = pa + offsetof(struct ciss_ccb, ccb_cmd);
		ccb->ccb_state = CISS_CCB_FREE;

		cmd->id = htole32(i << 2);
		cmd->id_hi = htole32(0);
		cmd->sgin = sc->maxsg;
		cmd->sglen = htole16((u_int16_t)cmd->sgin);
		cmd->err_len = htole32(sizeof(ccb->ccb_err));
		pa += offsetof(struct ciss_ccb, ccb_err);
		cmd->err_pa = htole64((u_int64_t)pa);

		if ((error = bus_dmamap_create(sc->dmat, maxfer, sc->maxsg,
		    maxfer, 0, BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW,
		    &ccb->ccb_dmamap)))
			break;

		SLIST_INSERT_HEAD(&sc->sc_free_ccb, ccb, ccb_link);
	}

	scsi_iopool_init(&sc->sc_iopool, sc, ciss_get_ccb, ciss_put_ccb);

	if (i < sc->maxcmd) {
		printf(": cannot create ccb#%d dmamap (%d)\n", i, error);
		if (i == 0) {
			/* TODO leaking cmd's dmamaps and shitz */
			bus_dmamem_free(sc->dmat, sc->cmdseg, 1);
			bus_dmamap_destroy(sc->dmat, sc->cmdmap);
			return -1;
		}
	}

	if ((error = bus_dmamem_alloc(sc->dmat, PAGE_SIZE, PAGE_SIZE, 0,
	    seg, 1, &rseg, BUS_DMA_NOWAIT | BUS_DMA_ZERO))) {
		printf(": cannot allocate scratch buffer (%d)\n", error);
		return -1;
	}

	if ((error = bus_dmamem_map(sc->dmat, seg, rseg, PAGE_SIZE,
	    (caddr_t *)&sc->scratch, BUS_DMA_NOWAIT))) {
		printf(": cannot map scratch buffer (%d)\n", error);
		return -1;
	}

	lock = CISS_LOCK_SCRATCH(sc);
	inq = sc->scratch;
	if (ciss_inq(sc, inq)) {
		printf(": adapter inquiry failed\n");
		CISS_UNLOCK_SCRATCH(sc, lock);
		bus_dmamem_free(sc->dmat, sc->cmdseg, 1);
		bus_dmamap_destroy(sc->dmat, sc->cmdmap);
		return -1;
	}

	if (!(inq->flags & CISS_INQ_BIGMAP)) {
		printf(": big map is not supported, flags=%b\n",
		    inq->flags, CISS_INQ_BITS);
		CISS_UNLOCK_SCRATCH(sc, lock);
		bus_dmamem_free(sc->dmat, sc->cmdseg, 1);
		bus_dmamap_destroy(sc->dmat, sc->cmdmap);
		return -1;
	}

	sc->maxunits = inq->numld;
	sc->nbus = inq->nscsi_bus;
	sc->ndrives = inq->buswidth? inq->buswidth : 256;
	printf(": %d LD%s, HW rev %d, FW %4.4s/%4.4s",
	    inq->numld, inq->numld == 1? "" : "s",
	    inq->hw_rev, inq->fw_running, inq->fw_stored);
	if (sc->cfg.methods & CISS_METH_FIFO64)
		printf(", 64bit fifo");
	else if (sc->cfg.methods & CISS_METH_FIFO64_RRO)
		printf(", 64bit fifo rro");
	printf("\n");

	CISS_UNLOCK_SCRATCH(sc, lock);

	timeout_set(&sc->sc_hb, ciss_heartbeat, sc);
	timeout_add_sec(&sc->sc_hb, 3);

	/* map LDs */
	if (ciss_ldmap(sc)) {
		printf("%s: adapter LD map failed\n", sc->sc_dev.dv_xname);
		bus_dmamem_free(sc->dmat, sc->cmdseg, 1);
		bus_dmamap_destroy(sc->dmat, sc->cmdmap);
		return -1;
	}

	if (!(sc->sc_lds = mallocarray(sc->maxunits, sizeof(*sc->sc_lds),
	    M_DEVBUF, M_NOWAIT | M_ZERO))) {
		bus_dmamem_free(sc->dmat, sc->cmdseg, 1);
		bus_dmamap_destroy(sc->dmat, sc->cmdmap);
		return -1;
	}

	sc->sc_flush = CISS_FLUSH_ENABLE;

	saa.saa_adapter_softc = sc;
	saa.saa_adapter = &ciss_switch;
	saa.saa_luns = 1;
	saa.saa_adapter_target = SDEV_NO_ADAPTER_TARGET;
	saa.saa_adapter_buswidth = sc->maxunits;
	saa.saa_openings = sc->maxcmd;
	saa.saa_pool = &sc->sc_iopool;
	saa.saa_quirks = saa.saa_flags = 0;
	saa.saa_wwpn = saa.saa_wwnn = 0;

	scsibus = (struct scsibus_softc *)config_found_sm(&sc->sc_dev,
	    &saa, scsiprint, NULL);

#if NBIO > 0
	/* XXX for now we can only deal w/ one volume. */
	if (!scsibus || sc->maxunits > 1)
		return 0;

	/* now map all the physdevs into their lds */
	/* XXX currently we assign all pf 'em into ld#0 */
	for (i = 0; i < sc->maxunits; i++)
		if (!(sc->sc_lds[i] = ciss_pdscan(sc, i)))
			return 0;

	if (bio_register(&sc->sc_dev, ciss_ioctl) != 0)
		printf("%s: controller registration failed",
		    sc->sc_dev.dv_xname);

	sc->sc_flags |= CISS_BIO;
#ifndef SMALL_KERNEL
	sc->sensors = mallocarray(sc->maxunits, sizeof(struct ksensor),
	    M_DEVBUF, M_NOWAIT | M_ZERO);
	if (sc->sensors) {
		struct device *dev;

		strlcpy(sc->sensordev.xname, sc->sc_dev.dv_xname,
		    sizeof(sc->sensordev.xname));
		for (i = 0; i < sc->maxunits; i++) {
			sc->sensors[i].type = SENSOR_DRIVE;
			sc->sensors[i].status = SENSOR_S_UNKNOWN;
			dev = scsi_get_link(scsibus, i, 0)->device_softc;
			strlcpy(sc->sensors[i].desc, dev->dv_xname,
			    sizeof(sc->sensors[i].desc));
			strlcpy(sc->sc_lds[i]->xname, dev->dv_xname,
			    sizeof(sc->sc_lds[i]->xname));
			sensor_attach(&sc->sensordev, &sc->sensors[i]);
		}
		if (sensor_task_register(sc, ciss_sensors, 10) == NULL)
			free(sc->sensors, M_DEVBUF,
			    sc->maxunits * sizeof(struct ksensor));
		else
			sensordev_install(&sc->sensordev);
	}
#endif /* SMALL_KERNEL */
#endif /* BIO > 0 */

	return 0;
}

void
ciss_shutdown(void *v)
{
	struct ciss_softc *sc = v;

	sc->sc_flush = CISS_FLUSH_DISABLE;
	timeout_del(&sc->sc_hb);
	ciss_sync(sc);
}

/*
 * submit a command and optionally wait for completion.
 * wait arg abuses SCSI_POLL|SCSI_NOSLEEP flags to request
 * to wait (SCSI_POLL) and to allow tsleep() (!SCSI_NOSLEEP)
 * instead of busy loop waiting
 */
int
ciss_cmd(struct ciss_ccb *ccb, int flags, int wait)
{
	struct timespec end, now, ts;
	struct ciss_softc *sc = ccb->ccb_sc;
	struct ciss_cmd *cmd = &ccb->ccb_cmd;
	struct ciss_ccb *ccb1;
	bus_dmamap_t dmap = ccb->ccb_dmamap;
	u_int64_t addr;
	uint64_t nsecs;
	u_int32_t id;
	int i, error = 0, ret;

	splassert(IPL_BIO);

	if (ccb->ccb_state != CISS_CCB_READY) {
		printf("%s: ccb %d not ready state=%b\n", sc->sc_dev.dv_xname,
		    cmd->id, ccb->ccb_state, CISS_CCB_BITS);
		return (EINVAL);
	}

	if (ccb->ccb_data) {
		bus_dma_segment_t *sgd;

		if ((error = bus_dmamap_load(sc->dmat, dmap, ccb->ccb_data,
		    ccb->ccb_len, NULL, flags))) {
			if (error == EFBIG)
				printf("more than %d dma segs\n", sc->maxsg);
			else
				printf("error %d loading dma map\n", error);
			if (ccb->ccb_xs) {
				ccb->ccb_xs->error = XS_DRIVER_STUFFUP;
				scsi_done(ccb->ccb_xs);
				ccb->ccb_xs = NULL;
			}
			return (error);
		}
		cmd->sgin = dmap->dm_nsegs;

		sgd = dmap->dm_segs;
		CISS_DPRINTF(CISS_D_DMA, ("data=%p/%zu<0x%lx/%lu",
		    ccb->ccb_data, ccb->ccb_len, sgd->ds_addr, sgd->ds_len));

		for (i = 0; i < dmap->dm_nsegs; sgd++, i++) {
			cmd->sgl[i].addr_lo = htole32(sgd->ds_addr);
			cmd->sgl[i].addr_hi =
			    htole32((u_int64_t)sgd->ds_addr >> 32);
			cmd->sgl[i].len = htole32(sgd->ds_len);
			cmd->sgl[i].flags = htole32(0);
			if (i)
				CISS_DPRINTF(CISS_D_DMA,
				    (",0x%lx/%lu", sgd->ds_addr, sgd->ds_len));
		}

		CISS_DPRINTF(CISS_D_DMA, ("> "));

		bus_dmamap_sync(sc->dmat, dmap, 0, dmap->dm_mapsize,
		    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);
	} else
		cmd->sgin = 0;
	cmd->sglen = htole16((u_int16_t)cmd->sgin);
	bzero(&ccb->ccb_err, sizeof(ccb->ccb_err));

	bus_dmamap_sync(sc->dmat, sc->cmdmap, 0, sc->cmdmap->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	ccb->ccb_state = CISS_CCB_ONQ;
	CISS_DPRINTF(CISS_D_CMD, ("submit=0x%x ", cmd->id));
	if (sc->cfg.methods & (CISS_METH_FIFO64|CISS_METH_FIFO64_RRO)) {
		/*
		 * Write the upper 32bits immediately before the lower
		 * 32bits and set bit 63 to indicate 64bit FIFO mode.
		 */
		addr = (u_int64_t)ccb->ccb_cmdpa;
		bus_space_write_4(sc->iot, sc->ioh, CISS_INQ64_HI,
		    (addr >> 32) | 0x80000000);
		bus_space_write_4(sc->iot, sc->ioh, CISS_INQ64_LO,
		    addr & 0x00000000ffffffffULL);
	} else
		bus_space_write_4(sc->iot, sc->ioh, CISS_INQ, ccb->ccb_cmdpa);

	/* If we're not waiting for completion we're done. */
	if (!(wait & SCSI_POLL))
		return (error);

	CISS_DPRINTF(CISS_D_CMD, ("waiting "));

	i = ccb->ccb_xs? ccb->ccb_xs->timeout : 60000;

	if (!(wait & SCSI_NOSLEEP)) {
		NSEC_TO_TIMESPEC(MSEC_TO_NSEC(i), &ts);
		nanouptime(&now);
		timespecadd(&now, &ts, &end);

		for (;;) {
			ccb->ccb_state = CISS_CCB_POLL;
			nsecs = TIMESPEC_TO_NSEC(&ts);
			CISS_DPRINTF(CISS_D_CMD, ("tsleep_nsec(%llu) ", nsecs));
			ret = tsleep_nsec(ccb, PRIBIO + 1, "ciss_cmd", nsecs);
			if (ret == EWOULDBLOCK)
				break;
			if (ccb->ccb_state != CISS_CCB_ONQ) {
				nanouptime(&now);
				if (timespeccmp(&end, &now, <=))
					break;
				timespecsub(&end, &now, &ts);
				CISS_DPRINTF(CISS_D_CMD, ("T"));
				continue;
			}
			ccb1 = ccb;

			error = ciss_done(ccb1);
			if (ccb1 == ccb)
				return (error);
		}
	} else {
		for (i *= 100; i--;) {
			DELAY(10);

			if (!(bus_space_read_4(sc->iot, sc->ioh,
			    CISS_ISR) & sc->iem)) {
				CISS_DPRINTF(CISS_D_CMD, ("N"));
				continue;
			}

			if (sc->cfg.methods & CISS_METH_FIFO64) {
				if (bus_space_read_4(sc->iot, sc->ioh,
				    CISS_OUTQ64_HI) == 0xffffffff) {
					CISS_DPRINTF(CISS_D_CMD, ("Q"));
					continue;
				}
				id = bus_space_read_4(sc->iot, sc->ioh,
				    CISS_OUTQ64_LO);
			} else if (sc->cfg.methods &
			    CISS_METH_FIFO64_RRO) {
				id = bus_space_read_4(sc->iot, sc->ioh,
				    CISS_OUTQ64_LO);
				if (id == 0xffffffff) {
					CISS_DPRINTF(CISS_D_CMD, ("Q"));
					continue;
				}
				(void)bus_space_read_4(sc->iot,
				    sc->ioh, CISS_OUTQ64_HI);
			} else {
				id = bus_space_read_4(sc->iot, sc->ioh,
				    CISS_OUTQ);
				if (id == 0xffffffff) {
					CISS_DPRINTF(CISS_D_CMD, ("Q"));
					continue;
				}
			}

			CISS_DPRINTF(CISS_D_CMD, ("got=0x%x ", id));
			ccb1 = sc->ccbs + (id >> 2) * sc->ccblen;
			ccb1->ccb_cmd.id = htole32(id);
			ccb1->ccb_cmd.id_hi = htole32(0);

			error = ciss_done(ccb1);
			if (ccb1 == ccb)
				return (error);
		}
	}

	/* if never got a chance to be done above... */
	ccb->ccb_err.cmd_stat = CISS_ERR_TMO;
	error = ciss_done(ccb);

	CISS_DPRINTF(CISS_D_CMD, ("done %d:%d",
	    ccb->ccb_err.cmd_stat, ccb->ccb_err.scsi_stat));

	return (error);
}

int
ciss_done(struct ciss_ccb *ccb)
{
	struct ciss_softc *sc = ccb->ccb_sc;
	struct scsi_xfer *xs = ccb->ccb_xs;
	struct ciss_cmd *cmd = &ccb->ccb_cmd;
	ciss_lock_t lock;
	int error = 0;

	CISS_DPRINTF(CISS_D_CMD, ("ciss_done(%p) ", ccb));

	if (ccb->ccb_state != CISS_CCB_ONQ) {
		printf("%s: unqueued ccb %p ready, state=%b\n",
		    sc->sc_dev.dv_xname, ccb, ccb->ccb_state, CISS_CCB_BITS);
		return 1;
	}
	lock = CISS_LOCK(sc);
	ccb->ccb_state = CISS_CCB_READY;

	if (ccb->ccb_cmd.id & CISS_CMD_ERR)
		error = ciss_error(ccb);

	if (ccb->ccb_data) {
		bus_dmamap_sync(sc->dmat, ccb->ccb_dmamap, 0,
		    ccb->ccb_dmamap->dm_mapsize, (cmd->flags & CISS_CDB_IN) ?
		    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->dmat, ccb->ccb_dmamap);
	}

	if (xs) {
		xs->resid = 0;
		scsi_done(xs);
	}

	CISS_UNLOCK(sc, lock);

	return error;
}

int
ciss_error(struct ciss_ccb *ccb)
{
	struct ciss_softc *sc = ccb->ccb_sc;
	struct ciss_error *err = &ccb->ccb_err;
	struct scsi_xfer *xs = ccb->ccb_xs;
	int rv;

	switch ((rv = letoh16(err->cmd_stat))) {
	case CISS_ERR_OK:
		rv = 0;
		break;

	case CISS_ERR_INVCMD:
		printf("%s: invalid cmd 0x%x: 0x%x is not valid @ 0x%x[%d]\n",
		    sc->sc_dev.dv_xname, ccb->ccb_cmd.id,
		    err->err_info, err->err_type[3], err->err_type[2]);
		if (xs) {
			bzero(&xs->sense, sizeof(xs->sense));
			xs->sense.error_code = SSD_ERRCODE_VALID |
			    SSD_ERRCODE_CURRENT;
			xs->sense.flags = SKEY_ILLEGAL_REQUEST;
			xs->sense.add_sense_code = 0x24; /* ill field */
			xs->error = XS_SENSE;
		}
		rv = EIO;
		break;

	case CISS_ERR_TMO:
		xs->error = XS_TIMEOUT;
		rv = ETIMEDOUT;
		break;

	default:
		if (xs) {
			switch (err->scsi_stat) {
			case SCSI_CHECK:
				xs->error = XS_SENSE;
				bcopy(&err->sense[0], &xs->sense,
				    sizeof(xs->sense));
				rv = EIO;
				break;

			case SCSI_BUSY:
				xs->error = XS_BUSY;
				rv = EBUSY;
				break;

			default:
				CISS_DPRINTF(CISS_D_ERR, ("%s: "
				    "cmd_stat %x scsi_stat 0x%x\n",
				    sc->sc_dev.dv_xname, rv, err->scsi_stat));
				xs->error = XS_DRIVER_STUFFUP;
				rv = EIO;
				break;
			}
			xs->resid = letoh32(err->resid);
		} else
			rv = EIO;
	}
	ccb->ccb_cmd.id &= htole32(~3);

	return rv;
}

int
ciss_inq(struct ciss_softc *sc, struct ciss_inquiry *inq)
{
	struct ciss_ccb *ccb;
	struct ciss_cmd *cmd;
	int rv;
	int s;

	ccb = scsi_io_get(&sc->sc_iopool, SCSI_POLL|SCSI_NOSLEEP);
	if (ccb == NULL)
		return ENOMEM;

	ccb->ccb_len = sizeof(*inq);
	ccb->ccb_data = inq;
	cmd = &ccb->ccb_cmd;
	cmd->tgt = htole32(CISS_CMD_MODE_PERIPH);
	cmd->tgt2 = 0;
	cmd->cdblen = 10;
	cmd->flags = CISS_CDB_CMD | CISS_CDB_SIMPL | CISS_CDB_IN;
	cmd->tmo = htole16(0);
	bzero(&cmd->cdb[0], sizeof(cmd->cdb));
	cmd->cdb[0] = CISS_CMD_CTRL_GET;
	cmd->cdb[6] = CISS_CMS_CTRL_CTRL;
	cmd->cdb[7] = sizeof(*inq) >> 8;	/* biiiig endian */
	cmd->cdb[8] = sizeof(*inq) & 0xff;

	s = splbio();
	rv = ciss_cmd(ccb, BUS_DMA_NOWAIT, SCSI_POLL|SCSI_NOSLEEP);
	splx(s);

	scsi_io_put(&sc->sc_iopool, ccb);

	return (rv);
}

int
ciss_ldmap(struct ciss_softc *sc)
{
	struct ciss_ccb *ccb;
	struct ciss_cmd *cmd;
	struct ciss_ldmap *lmap;
	ciss_lock_t lock;
	int total, rv;

	lock = CISS_LOCK_SCRATCH(sc);
	lmap = sc->scratch;
	lmap->size = htobe32(sc->maxunits * sizeof(lmap->map));
	total = sizeof(*lmap) + (sc->maxunits - 1) * sizeof(lmap->map);

	ccb = scsi_io_get(&sc->sc_iopool, SCSI_POLL|SCSI_NOSLEEP);
	if (ccb == NULL) {
		CISS_UNLOCK_SCRATCH(sc, lock);
		return ENOMEM;
	}

	ccb->ccb_len = total;
	ccb->ccb_data = lmap;
	cmd = &ccb->ccb_cmd;
	cmd->tgt = CISS_CMD_MODE_PERIPH;
	cmd->tgt2 = 0;
	cmd->cdblen = 12;
	cmd->flags = CISS_CDB_CMD | CISS_CDB_SIMPL | CISS_CDB_IN;
	cmd->tmo = htole16(30);
	bzero(&cmd->cdb[0], sizeof(cmd->cdb));
	cmd->cdb[0] = CISS_CMD_LDMAP;
	cmd->cdb[8] = total >> 8;	/* biiiig endian */
	cmd->cdb[9] = total & 0xff;

	rv = ciss_cmd(ccb, BUS_DMA_NOWAIT, SCSI_POLL|SCSI_NOSLEEP);
	scsi_io_put(&sc->sc_iopool, ccb);
	CISS_UNLOCK_SCRATCH(sc, lock);

	if (rv)
		return rv;

	CISS_DPRINTF(CISS_D_MISC, ("lmap %x:%x\n",
	    lmap->map[0].tgt, lmap->map[0].tgt2));

	return 0;
}

int
ciss_sync(struct ciss_softc *sc)
{
	struct ciss_ccb *ccb;
	struct ciss_cmd *cmd;
	struct ciss_flush *flush;
	ciss_lock_t lock;
	int rv;

	lock = CISS_LOCK_SCRATCH(sc);
	flush = sc->scratch;
	bzero(flush, sizeof(*flush));
	flush->flush = sc->sc_flush;

	ccb = scsi_io_get(&sc->sc_iopool, SCSI_POLL|SCSI_NOSLEEP);
	if (ccb == NULL) {
		CISS_UNLOCK_SCRATCH(sc, lock);
		return ENOMEM;
	}

	ccb->ccb_len = sizeof(*flush);
	ccb->ccb_data = flush;
	cmd = &ccb->ccb_cmd;
	cmd->tgt = CISS_CMD_MODE_PERIPH;
	cmd->tgt2 = 0;
	cmd->cdblen = 10;
	cmd->flags = CISS_CDB_CMD | CISS_CDB_SIMPL | CISS_CDB_OUT;
	cmd->tmo = htole16(0);
	bzero(&cmd->cdb[0], sizeof(cmd->cdb));
	cmd->cdb[0] = CISS_CMD_CTRL_SET;
	cmd->cdb[6] = CISS_CMS_CTRL_FLUSH;
	cmd->cdb[7] = sizeof(*flush) >> 8;	/* biiiig endian */
	cmd->cdb[8] = sizeof(*flush) & 0xff;

	rv = ciss_cmd(ccb, BUS_DMA_NOWAIT, SCSI_POLL|SCSI_NOSLEEP);
	scsi_io_put(&sc->sc_iopool, ccb);
	CISS_UNLOCK_SCRATCH(sc, lock);

	return rv;
}

void
ciss_scsi_cmd(struct scsi_xfer *xs)
{
	struct scsi_link *link = xs->sc_link;
	u_int8_t target = link->target;
	struct ciss_ccb *ccb;
	struct ciss_cmd *cmd;
	ciss_lock_t lock;

	CISS_DPRINTF(CISS_D_CMD, ("ciss_scsi_cmd "));

	if (xs->cmdlen > CISS_MAX_CDB) {
		CISS_DPRINTF(CISS_D_CMD, ("CDB too big %p ", xs));
		bzero(&xs->sense, sizeof(xs->sense));
		xs->sense.error_code = SSD_ERRCODE_VALID | SSD_ERRCODE_CURRENT;
		xs->sense.flags = SKEY_ILLEGAL_REQUEST;
		xs->sense.add_sense_code = 0x20; /* illcmd, 0x24 illfield */
		xs->error = XS_SENSE;
		scsi_done(xs);
		return;
	}

	xs->error = XS_NOERROR;

	/* XXX emulate SYNCHRONIZE_CACHE ??? */

	ccb = xs->io;

	cmd = &ccb->ccb_cmd;
	ccb->ccb_len = xs->datalen;
	ccb->ccb_data = xs->data;
	ccb->ccb_xs = xs;
	cmd->tgt = CISS_CMD_MODE_LD | target;
	cmd->tgt2 = 0;
	cmd->cdblen = xs->cmdlen;
	cmd->flags = CISS_CDB_CMD | CISS_CDB_SIMPL;
	if (xs->flags & SCSI_DATA_IN)
		cmd->flags |= CISS_CDB_IN;
	else if (xs->flags & SCSI_DATA_OUT)
		cmd->flags |= CISS_CDB_OUT;
	cmd->tmo = htole16(xs->timeout < 1000? 1 : xs->timeout / 1000);
	bzero(&cmd->cdb[0], sizeof(cmd->cdb));
	bcopy(&xs->cmd, &cmd->cdb[0], CISS_MAX_CDB);

	lock = CISS_LOCK(sc);
	ciss_cmd(ccb, BUS_DMA_WAITOK, xs->flags & (SCSI_POLL|SCSI_NOSLEEP));
	CISS_UNLOCK(sc, lock);
}

int
ciss_intr(void *v)
{
	struct ciss_softc *sc = v;
	struct ciss_ccb *ccb;
	bus_size_t reg;
	u_int32_t id;
	int hit = 0;

	CISS_DPRINTF(CISS_D_INTR, ("intr "));

	if (!(bus_space_read_4(sc->iot, sc->ioh, CISS_ISR) & sc->iem))
		return 0;

	if (sc->cfg.methods & CISS_METH_FIFO64)
		reg = CISS_OUTQ64_HI;
	else if (sc->cfg.methods & CISS_METH_FIFO64_RRO)
		reg = CISS_OUTQ64_LO;
	else
		reg = CISS_OUTQ;
	while ((id = bus_space_read_4(sc->iot, sc->ioh, reg)) != 0xffffffff) {
		if (reg == CISS_OUTQ64_HI)
			id = bus_space_read_4(sc->iot, sc->ioh,
			    CISS_OUTQ64_LO);
		else if (reg == CISS_OUTQ64_LO)
			(void)bus_space_read_4(sc->iot, sc->ioh,
			    CISS_OUTQ64_HI);
		ccb = sc->ccbs + (id >> 2) * sc->ccblen;
		ccb->ccb_cmd.id = htole32(id);
		ccb->ccb_cmd.id_hi = htole32(0); /* ignore the upper 32bits */
		if (ccb->ccb_state == CISS_CCB_POLL) {
			ccb->ccb_state = CISS_CCB_ONQ;
			wakeup(ccb);
		} else
			ciss_done(ccb);

		hit = 1;
	}
	CISS_DPRINTF(CISS_D_INTR, ("exit "));
	return hit;
}

void
ciss_heartbeat(void *v)
{
	struct ciss_softc *sc = v;
	u_int32_t hb;

	hb = bus_space_read_4(sc->iot, sc->cfg_ioh,
	    sc->cfgoff + offsetof(struct ciss_config, heartbeat));
	if (hb == sc->heartbeat) {
		sc->fibrillation++;
		CISS_DPRINTF(CISS_D_ERR, ("%s: fibrillation #%d (value=%d)\n",
		    sc->sc_dev.dv_xname, sc->fibrillation, hb));
		if (sc->fibrillation >= 11) {
			/* No heartbeat for 33 seconds */
			panic("%s: dead", sc->sc_dev.dv_xname);	/* XXX reset! */
		}
	} else {
		sc->heartbeat = hb;
		if (sc->fibrillation) {
			CISS_DPRINTF(CISS_D_ERR, ("%s: "
			    "fibrillation ended (value=%d)\n",
			    sc->sc_dev.dv_xname, hb));
		}
		sc->fibrillation = 0;
	}

	timeout_add_sec(&sc->sc_hb, 3);
}

int
ciss_scsi_ioctl(struct scsi_link *link, u_long cmd, caddr_t addr, int flag)
{
#if NBIO > 0
	return ciss_ioctl(link->bus->sb_adapter_softc, cmd, addr);
#else
	return ENOTTY;
#endif
}

#if NBIO > 0
const int ciss_level[] = { 0, 4, 1, 5, 51, 7 };
const int ciss_stat[] = { BIOC_SVONLINE, BIOC_SVOFFLINE, BIOC_SVOFFLINE,
    BIOC_SVDEGRADED, BIOC_SVREBUILD, BIOC_SVREBUILD, BIOC_SVDEGRADED,
    BIOC_SVDEGRADED, BIOC_SVINVALID, BIOC_SVINVALID, BIOC_SVBUILDING,
    BIOC_SVOFFLINE, BIOC_SVBUILDING };

int
ciss_ioctl(struct device *dev, u_long cmd, caddr_t addr)
{
	struct ciss_softc *sc = (struct ciss_softc *)dev;
	struct bioc_inq *bi;
	struct bioc_vol *bv;
	struct bioc_disk *bd;
	struct bioc_blink *bb;
	/* struct bioc_alarm *ba; */
	/* struct bioc_setstate *bss; */
	struct ciss_ldid *ldid;
	struct ciss_ldstat *ldstat;
	struct ciss_pdid *pdid;
	struct ciss_blink *blink;
	struct ciss_ld *ldp;
	ciss_lock_t lock;
	u_int8_t drv;
	int ld, pd, error = 0;
	u_int blks;

	if (!(sc->sc_flags & CISS_BIO))
		return ENOTTY;

	lock = CISS_LOCK(sc);
	switch (cmd) {
	case BIOCINQ:
		bi = (struct bioc_inq *)addr;
		strlcpy(bi->bi_dev, sc->sc_dev.dv_xname, sizeof(bi->bi_dev));
		bi->bi_novol = sc->maxunits;
		bi->bi_nodisk = sc->ndrives;
		break;

	case BIOCVOL:
		bv = (struct bioc_vol *)addr;
		if (bv->bv_volid > sc->maxunits) {
			error = EINVAL;
			break;
		}
		ldp = sc->sc_lds[bv->bv_volid];
		if (!ldp) {
			error = EINVAL;
			break;
		}
		ldid = sc->scratch;
		if ((error = ciss_ldid(sc, bv->bv_volid, ldid)))
			break;
		/* params 30:88:ff:00:00:00:00:00:00:00:00:00:00:00:20:00 */
		bv->bv_status = BIOC_SVINVALID;
		blks = (u_int)letoh16(ldid->nblocks[1]) << 16 |
		    letoh16(ldid->nblocks[0]);
		bv->bv_size = blks * (uint64_t)letoh16(ldid->blksize);
		bv->bv_level = ciss_level[ldid->type];
		bv->bv_nodisk = ldp->ndrives;
		strlcpy(bv->bv_dev, ldp->xname, sizeof(bv->bv_dev));
		strlcpy(bv->bv_vendor, "CISS", sizeof(bv->bv_vendor));
		ldstat = sc->scratch;
		bzero(ldstat, sizeof(*ldstat));
		if ((error = ciss_ldstat(sc, bv->bv_volid, ldstat)))
			break;
		bv->bv_percent = -1;
		bv->bv_seconds = 0;
		if (ldstat->stat < nitems(ciss_stat))
			bv->bv_status = ciss_stat[ldstat->stat];
		if (bv->bv_status == BIOC_SVREBUILD ||
		    bv->bv_status == BIOC_SVBUILDING)
			bv->bv_percent = (blks -
			    (((u_int)ldstat->prog[3] << 24) |
			    ((u_int)ldstat->prog[2] << 16) |
			    ((u_int)ldstat->prog[1] << 8) |
			    (u_int)ldstat->prog[0])) * 100ULL / blks;
		break;

	case BIOCDISK:
		bd = (struct bioc_disk *)addr;
		if (bd->bd_volid > sc->maxunits) {
			error = EINVAL;
			break;
		}
		ldp = sc->sc_lds[bd->bd_volid];
		if (!ldp || (pd = bd->bd_diskid) > ldp->ndrives) {
			error = EINVAL;
			break;
		}
		ldstat = sc->scratch;
		if ((error = ciss_ldstat(sc, bd->bd_volid, ldstat)))
			break;
		bd->bd_status = -1;
		if (ldstat->stat == CISS_LD_REBLD &&
		    ldstat->bigrebuild == ldp->tgts[pd])
			bd->bd_status = BIOC_SDREBUILD;
		if (ciss_bitset(ldp->tgts[pd] & (~CISS_BIGBIT),
		    ldstat->bigfailed)) {
			bd->bd_status = BIOC_SDFAILED;
			bd->bd_size = 0;
			bd->bd_channel = (ldp->tgts[pd] & (~CISS_BIGBIT)) /
			    sc->ndrives;
			bd->bd_target = ldp->tgts[pd] % sc->ndrives;
			bd->bd_lun = 0;
			bd->bd_vendor[0] = '\0';
			bd->bd_serial[0] = '\0';
			bd->bd_procdev[0] = '\0';
		} else {
			pdid = sc->scratch;
			if ((error = ciss_pdid(sc, ldp->tgts[pd], pdid,
			    SCSI_POLL)))
				break;
			if (bd->bd_status < 0) {
				if (pdid->config & CISS_PD_SPARE)
					bd->bd_status = BIOC_SDHOTSPARE;
				else if (pdid->present & CISS_PD_PRESENT)
					bd->bd_status = BIOC_SDONLINE;
				else
					bd->bd_status = BIOC_SDINVALID;
			}
			bd->bd_size = (u_int64_t)letoh32(pdid->nblocks) *
			    letoh16(pdid->blksz);
			bd->bd_channel = pdid->bus;
			bd->bd_target = pdid->target;
			bd->bd_lun = 0;
			strlcpy(bd->bd_vendor, pdid->model,
			    sizeof(bd->bd_vendor));
			strlcpy(bd->bd_serial, pdid->serial,
			    sizeof(bd->bd_serial));
			bd->bd_procdev[0] = '\0';
		}
		break;

	case BIOCBLINK:
		bb = (struct bioc_blink *)addr;
		blink = sc->scratch;
		error = EINVAL;
		/* XXX workaround completely dumb scsi addressing */
		for (ld = 0; ld < sc->maxunits; ld++) {
			ldp = sc->sc_lds[ld];
			if (!ldp)
				continue;
			if (sc->ndrives == 256)
				drv = bb->bb_target;
			else
				drv = CISS_BIGBIT +
				    bb->bb_channel * sc->ndrives +
				    bb->bb_target;
			for (pd = 0; pd < ldp->ndrives; pd++)
				if (ldp->tgts[pd] == drv)
					error = ciss_blink(sc, ld, pd,
					    bb->bb_status, blink);
		}
		break;

	case BIOCALARM:
	case BIOCSETSTATE:
	default:
		CISS_DPRINTF(CISS_D_IOCTL, ("%s: invalid ioctl\n",
		    sc->sc_dev.dv_xname));
		error = ENOTTY;
	}
	CISS_UNLOCK(sc, lock);

	return error;
}

#ifndef SMALL_KERNEL
void
ciss_sensors(void *v)
{
	struct ciss_softc *sc = v;
	struct ciss_ldstat *ldstat;
	int i, error;

	for (i = 0; i < sc->maxunits; i++) {
		ldstat = sc->scratch;
		if ((error = ciss_ldstat(sc, i, ldstat))) {
			sc->sensors[i].value = 0;
			sc->sensors[i].status = SENSOR_S_UNKNOWN;
			continue;
		}

		switch (ldstat->stat) {
		case CISS_LD_OK:
			sc->sensors[i].value = SENSOR_DRIVE_ONLINE;
			sc->sensors[i].status = SENSOR_S_OK;
			break;

		case CISS_LD_DEGRAD:
			sc->sensors[i].value = SENSOR_DRIVE_PFAIL;
			sc->sensors[i].status = SENSOR_S_WARN;
			break;

		case CISS_LD_EXPND:
		case CISS_LD_QEXPND:
		case CISS_LD_RBLDRD:
		case CISS_LD_REBLD:
			sc->sensors[i].value = SENSOR_DRIVE_REBUILD;
			sc->sensors[i].status = SENSOR_S_WARN;
			break;

		case CISS_LD_NORDY:
		case CISS_LD_PDINV:
		case CISS_LD_PDUNC:
		case CISS_LD_FAILED:
		case CISS_LD_UNCONF:
			sc->sensors[i].value = SENSOR_DRIVE_FAIL;
			sc->sensors[i].status = SENSOR_S_CRIT;
			break;

		default:
			sc->sensors[i].value = 0;
			sc->sensors[i].status = SENSOR_S_UNKNOWN;
		}
	}
}
#endif /* SMALL_KERNEL */

int
ciss_ldid(struct ciss_softc *sc, int target, struct ciss_ldid *id)
{
	struct ciss_ccb *ccb;
	struct ciss_cmd *cmd;
	int rv;
	int s;

	ccb = scsi_io_get(&sc->sc_iopool, SCSI_POLL);
	if (ccb == NULL)
		return ENOMEM;

	ccb->ccb_len = sizeof(*id);
	ccb->ccb_data = id;
	cmd = &ccb->ccb_cmd;
	cmd->tgt = htole32(CISS_CMD_MODE_PERIPH);
	cmd->tgt2 = 0;
	cmd->cdblen = 10;
	cmd->flags = CISS_CDB_CMD | CISS_CDB_SIMPL | CISS_CDB_IN;
	cmd->tmo = htole16(0);
	bzero(&cmd->cdb[0], sizeof(cmd->cdb));
	cmd->cdb[0] = CISS_CMD_CTRL_GET;
	cmd->cdb[5] = target;
	cmd->cdb[6] = CISS_CMS_CTRL_LDIDEXT;
	cmd->cdb[7] = sizeof(*id) >> 8;	/* biiiig endian */
	cmd->cdb[8] = sizeof(*id) & 0xff;

	s = splbio();
	rv = ciss_cmd(ccb, BUS_DMA_NOWAIT, SCSI_POLL);
	splx(s);

	scsi_io_put(&sc->sc_iopool, ccb);

	return (rv);
}

int
ciss_ldstat(struct ciss_softc *sc, int target, struct ciss_ldstat *stat)
{
	struct ciss_ccb *ccb;
	struct ciss_cmd *cmd;
	int rv;
	int s;

	ccb = scsi_io_get(&sc->sc_iopool, SCSI_POLL);
	if (ccb == NULL)
		return ENOMEM;

	ccb->ccb_len = sizeof(*stat);
	ccb->ccb_data = stat;
	cmd = &ccb->ccb_cmd;
	cmd->tgt = htole32(CISS_CMD_MODE_PERIPH);
	cmd->tgt2 = 0;
	cmd->cdblen = 10;
	cmd->flags = CISS_CDB_CMD | CISS_CDB_SIMPL | CISS_CDB_IN;
	cmd->tmo = htole16(0);
	bzero(&cmd->cdb[0], sizeof(cmd->cdb));
	cmd->cdb[0] = CISS_CMD_CTRL_GET;
	cmd->cdb[5] = target;
	cmd->cdb[6] = CISS_CMS_CTRL_LDSTAT;
	cmd->cdb[7] = sizeof(*stat) >> 8;	/* biiiig endian */
	cmd->cdb[8] = sizeof(*stat) & 0xff;

	s = splbio();
	rv = ciss_cmd(ccb, BUS_DMA_NOWAIT, SCSI_POLL);
	splx(s);

	scsi_io_put(&sc->sc_iopool, ccb);

	return (rv);
}

int
ciss_pdid(struct ciss_softc *sc, u_int8_t drv, struct ciss_pdid *id, int wait)
{
	struct ciss_ccb *ccb;
	struct ciss_cmd *cmd;
	int rv;
	int s;

	ccb = scsi_io_get(&sc->sc_iopool, SCSI_POLL);
	if (ccb == NULL)
		return ENOMEM;

	ccb->ccb_len = sizeof(*id);
	ccb->ccb_data = id;
	cmd = &ccb->ccb_cmd;
	cmd->tgt = htole32(CISS_CMD_MODE_PERIPH);
	cmd->tgt2 = 0;
	cmd->cdblen = 10;
	cmd->flags = CISS_CDB_CMD | CISS_CDB_SIMPL | CISS_CDB_IN;
	cmd->tmo = htole16(0);
	bzero(&cmd->cdb[0], sizeof(cmd->cdb));
	cmd->cdb[0] = CISS_CMD_CTRL_GET;
	cmd->cdb[2] = drv;
	cmd->cdb[6] = CISS_CMS_CTRL_PDID;
	cmd->cdb[7] = sizeof(*id) >> 8;	/* biiiig endian */
	cmd->cdb[8] = sizeof(*id) & 0xff;

	s = splbio();
	rv = ciss_cmd(ccb, BUS_DMA_NOWAIT, wait);
	splx(s);

	scsi_io_put(&sc->sc_iopool, ccb);

	return (rv);
}


struct ciss_ld *
ciss_pdscan(struct ciss_softc *sc, int ld)
{
	struct ciss_pdid *pdid;
	struct ciss_ld *ldp;
	u_int8_t drv, buf[128];
	int i, j, k = 0;

	pdid = sc->scratch;
	if (sc->ndrives == 256) {
		for (i = 0; i < CISS_BIGBIT; i++)
			if (!ciss_pdid(sc, i, pdid, SCSI_NOSLEEP|SCSI_POLL) &&
			    (pdid->present & CISS_PD_PRESENT))
				buf[k++] = i;
	} else
		for (i = 0; i < sc->nbus; i++)
			for (j = 0; j < sc->ndrives; j++) {
				drv = CISS_BIGBIT + i * sc->ndrives + j;
				if (!ciss_pdid(sc, drv, pdid,
				    SCSI_NOSLEEP|SCSI_POLL))
					buf[k++] = drv;
			}

	if (!k)
		return NULL;

	ldp = malloc(sizeof(*ldp) + (k-1), M_DEVBUF, M_NOWAIT);
	if (!ldp)
		return NULL;

	bzero(&ldp->bling, sizeof(ldp->bling));
	ldp->ndrives = k;
	bcopy(buf, ldp->tgts, k);
	return ldp;
}

int
ciss_blink(struct ciss_softc *sc, int ld, int pd, int stat,
    struct ciss_blink *blink)
{
	struct ciss_ccb *ccb;
	struct ciss_cmd *cmd;
	struct ciss_ld *ldp;
	int rv;
	int s;

	if (ld > sc->maxunits)
		return EINVAL;

	ldp = sc->sc_lds[ld];
	if (!ldp || pd > ldp->ndrives)
		return EINVAL;

	ldp->bling.pdtab[ldp->tgts[pd]] = stat == BIOC_SBUNBLINK? 0 :
	    CISS_BLINK_ALL;
	bcopy(&ldp->bling, blink, sizeof(*blink));

	ccb = scsi_io_get(&sc->sc_iopool, SCSI_POLL);
	if (ccb == NULL)
		return ENOMEM;

	ccb->ccb_len = sizeof(*blink);
	ccb->ccb_data = blink;
	cmd = &ccb->ccb_cmd;
	cmd->tgt = htole32(CISS_CMD_MODE_PERIPH);
	cmd->tgt2 = 0;
	cmd->cdblen = 10;
	cmd->flags = CISS_CDB_CMD | CISS_CDB_SIMPL | CISS_CDB_OUT;
	cmd->tmo = htole16(0);
	bzero(&cmd->cdb[0], sizeof(cmd->cdb));
	cmd->cdb[0] = CISS_CMD_CTRL_SET;
	cmd->cdb[6] = CISS_CMS_CTRL_PDBLINK;
	cmd->cdb[7] = sizeof(*blink) >> 8;	/* biiiig endian */
	cmd->cdb[8] = sizeof(*blink) & 0xff;

	s = splbio();
	rv = ciss_cmd(ccb, BUS_DMA_NOWAIT, SCSI_POLL);
	splx(s);

	scsi_io_put(&sc->sc_iopool, ccb);

	return (rv);
}
#endif
