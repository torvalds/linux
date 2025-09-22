/*	$OpenBSD: sdmmc_scsi.c,v 1.63 2023/04/19 01:46:10 dlg Exp $	*/

/*
 * Copyright (c) 2006 Uwe Stuehler <uwe@openbsd.org>
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

/* A SCSI adapter emulation to access SD/MMC memory cards */

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/systm.h>

#include <scsi/scsi_all.h>
#include <scsi/scsi_disk.h>
#include <scsi/scsiconf.h>

#include <dev/sdmmc/sdmmc_scsi.h>
#include <dev/sdmmc/sdmmcvar.h>

#ifdef HIBERNATE
#include <sys/hibernate.h>
#include <sys/disk.h>
#include <sys/disklabel.h>
#include <sys/rwlock.h>
#endif

#define SDMMC_SCSIID_HOST	0x00
#define SDMMC_SCSIID_MAX	0x0f

#define SDMMC_SCSI_MAXCMDS	8

struct sdmmc_scsi_target {
	struct sdmmc_function *card;
};

struct sdmmc_ccb {
	struct sdmmc_scsi_softc *ccb_scbus;
	struct scsi_xfer *ccb_xs;
	int ccb_flags;
#define SDMMC_CCB_F_ERR		0x0001
	u_int32_t ccb_blockno;
	u_int32_t ccb_blockcnt;
	volatile enum {
		SDMMC_CCB_FREE,
		SDMMC_CCB_READY,
		SDMMC_CCB_QUEUED
	} ccb_state;
	struct sdmmc_command ccb_cmd;
	struct sdmmc_task ccb_task;
	TAILQ_ENTRY(sdmmc_ccb) ccb_link;
};

TAILQ_HEAD(sdmmc_ccb_list, sdmmc_ccb);

struct sdmmc_scsi_softc {
	struct device *sc_child;
	struct sdmmc_scsi_target *sc_tgt;
	int sc_ntargets;
	struct sdmmc_ccb *sc_ccbs;		/* allocated ccbs */
	int		sc_nccbs;
	struct sdmmc_ccb_list sc_ccb_freeq;	/* free ccbs */
	struct sdmmc_ccb_list sc_ccb_runq;	/* queued ccbs */
	struct mutex sc_ccb_mtx;
	struct scsi_iopool sc_iopool;
};

int	sdmmc_alloc_ccbs(struct sdmmc_scsi_softc *, int);
void	sdmmc_free_ccbs(struct sdmmc_scsi_softc *);
void	*sdmmc_ccb_alloc(void *);
void	sdmmc_ccb_free(void *, void *);

void	sdmmc_scsi_cmd(struct scsi_xfer *);
void	sdmmc_inquiry(struct scsi_xfer *);
void	sdmmc_start_xs(struct sdmmc_softc *, struct sdmmc_ccb *);
void	sdmmc_complete_xs(void *);
void	sdmmc_done_xs(struct sdmmc_ccb *);
void	sdmmc_stimeout(void *);
void	sdmmc_minphys(struct buf *, struct scsi_link *);

const struct scsi_adapter sdmmc_switch = {
	sdmmc_scsi_cmd, sdmmc_minphys, NULL, NULL, NULL
};

#ifdef SDMMC_DEBUG
#define DPRINTF(s)	printf s
#else
#define DPRINTF(s)	/**/
#endif

void
sdmmc_scsi_attach(struct sdmmc_softc *sc)
{
	struct sdmmc_attach_args saa;
	struct sdmmc_scsi_softc *scbus;
	struct sdmmc_function *sf;

	rw_assert_wrlock(&sc->sc_lock);

	scbus = malloc(sizeof *scbus, M_DEVBUF, M_WAITOK | M_ZERO);

	scbus->sc_tgt = mallocarray(sizeof(*scbus->sc_tgt),
	    (SDMMC_SCSIID_MAX+1), M_DEVBUF, M_WAITOK | M_ZERO);

	/*
	 * Each card that sent us a CID in the identification stage
	 * gets a SCSI ID > 0, whether it is a memory card or not.
	 */
	scbus->sc_ntargets = 1;
	SIMPLEQ_FOREACH(sf, &sc->sf_head, sf_list) {
		if (scbus->sc_ntargets >= SDMMC_SCSIID_MAX+1)
			break;
		scbus->sc_tgt[scbus->sc_ntargets].card = sf;
		scbus->sc_ntargets++;
	}

	/* Preallocate some CCBs and initialize the CCB lists. */
	if (sdmmc_alloc_ccbs(scbus, SDMMC_SCSI_MAXCMDS) != 0) {
		printf("%s: can't allocate ccbs\n", sc->sc_dev.dv_xname);
		goto free_sctgt;
	}

	sc->sc_scsibus = scbus;

	saa.sf = NULL;
	saa.saa.saa_adapter_target = SDMMC_SCSIID_HOST;
	saa.saa.saa_adapter_buswidth = scbus->sc_ntargets;
	saa.saa.saa_adapter_softc = sc;
	saa.saa.saa_luns = 1;
	saa.saa.saa_adapter = &sdmmc_switch;
	saa.saa.saa_openings = 1;
	saa.saa.saa_pool = &scbus->sc_iopool;
	saa.saa.saa_quirks = saa.saa.saa_flags = 0;
	saa.saa.saa_wwpn = saa.saa.saa_wwnn = 0;

	scbus->sc_child = config_found(&sc->sc_dev, &saa, scsiprint);
	if (scbus->sc_child == NULL) {
		printf("%s: can't attach scsibus\n", sc->sc_dev.dv_xname);
		goto free_ccbs;
	}
	return;

 free_ccbs:
	sc->sc_scsibus = NULL;
	sdmmc_free_ccbs(scbus);
 free_sctgt:
	free(scbus->sc_tgt, M_DEVBUF,
	    sizeof(*scbus->sc_tgt) * (SDMMC_SCSIID_MAX+1));
	free(scbus, M_DEVBUF, sizeof *scbus);
}

void
sdmmc_scsi_detach(struct sdmmc_softc *sc)
{
	struct sdmmc_scsi_softc *scbus;
	struct sdmmc_ccb *ccb;
	int s;

	rw_assert_wrlock(&sc->sc_lock);

	scbus = sc->sc_scsibus;
	if (scbus == NULL)
		return;

	/* Complete all open scsi xfers. */
	s = splbio();
	for (ccb = TAILQ_FIRST(&scbus->sc_ccb_runq); ccb != NULL;
	     ccb = TAILQ_FIRST(&scbus->sc_ccb_runq))
		sdmmc_stimeout(ccb);
	splx(s);

	if (scbus->sc_child != NULL)
		config_detach(scbus->sc_child, DETACH_FORCE);

	if (scbus->sc_tgt != NULL)
		free(scbus->sc_tgt, M_DEVBUF,
		    sizeof(*scbus->sc_tgt) * (SDMMC_SCSIID_MAX+1));

	sdmmc_free_ccbs(scbus);
	free(scbus, M_DEVBUF, sizeof *scbus);
	sc->sc_scsibus = NULL;
}

/*
 * CCB management
 */

int
sdmmc_alloc_ccbs(struct sdmmc_scsi_softc *scbus, int nccbs)
{
	struct sdmmc_ccb *ccb;
	int i;

	scbus->sc_ccbs = mallocarray(nccbs, sizeof(struct sdmmc_ccb),
	    M_DEVBUF, M_NOWAIT);
	if (scbus->sc_ccbs == NULL)
		return 1;
	scbus->sc_nccbs = nccbs;

	TAILQ_INIT(&scbus->sc_ccb_freeq);
	TAILQ_INIT(&scbus->sc_ccb_runq);
	mtx_init(&scbus->sc_ccb_mtx, IPL_BIO);
	scsi_iopool_init(&scbus->sc_iopool, scbus, sdmmc_ccb_alloc,
	    sdmmc_ccb_free);

	for (i = 0; i < nccbs; i++) {
		ccb = &scbus->sc_ccbs[i];
		ccb->ccb_scbus = scbus;
		ccb->ccb_state = SDMMC_CCB_FREE;
		ccb->ccb_flags = 0;
		ccb->ccb_xs = NULL;

		TAILQ_INSERT_TAIL(&scbus->sc_ccb_freeq, ccb, ccb_link);
	}
	return 0;
}

void
sdmmc_free_ccbs(struct sdmmc_scsi_softc *scbus)
{
	if (scbus->sc_ccbs != NULL) {
		free(scbus->sc_ccbs, M_DEVBUF,
		    scbus->sc_nccbs * sizeof(struct sdmmc_ccb));
		scbus->sc_ccbs = NULL;
	}
}

void *
sdmmc_ccb_alloc(void *xscbus)
{
	struct sdmmc_scsi_softc *scbus = xscbus;
	struct sdmmc_ccb *ccb;

	mtx_enter(&scbus->sc_ccb_mtx);
	ccb = TAILQ_FIRST(&scbus->sc_ccb_freeq);
	if (ccb != NULL) {
		TAILQ_REMOVE(&scbus->sc_ccb_freeq, ccb, ccb_link);
		ccb->ccb_state = SDMMC_CCB_READY;
	}
	mtx_leave(&scbus->sc_ccb_mtx);

	return ccb;
}

void
sdmmc_ccb_free(void *xscbus, void *xccb)
{
	struct sdmmc_scsi_softc *scbus = xscbus;
	struct sdmmc_ccb *ccb = xccb;
	int s;

	s = splbio();
	if (ccb->ccb_state == SDMMC_CCB_QUEUED)
		TAILQ_REMOVE(&scbus->sc_ccb_runq, ccb, ccb_link);
	splx(s);

	ccb->ccb_state = SDMMC_CCB_FREE;
	ccb->ccb_flags = 0;
	ccb->ccb_xs = NULL;

	mtx_enter(&scbus->sc_ccb_mtx);
	TAILQ_INSERT_TAIL(&scbus->sc_ccb_freeq, ccb, ccb_link);
	mtx_leave(&scbus->sc_ccb_mtx);
}

/*
 * SCSI command emulation
 */

/* XXX move to some sort of "scsi emulation layer". */
static void
sdmmc_scsi_decode_rw(struct scsi_xfer *xs, u_int32_t *blocknop,
    u_int32_t *blockcntp)
{
	struct scsi_rw *rw;
	struct scsi_rw_10 *rw10;

	if (xs->cmdlen == 6) {
		rw = (struct scsi_rw *)&xs->cmd;
		*blocknop = _3btol(rw->addr) & (SRW_TOPADDR << 16 | 0xffff);
		*blockcntp = rw->length ? rw->length : 0x100;
	} else {
		rw10 = (struct scsi_rw_10 *)&xs->cmd;
		*blocknop = _4btol(rw10->addr);
		*blockcntp = _2btol(rw10->length);
	}
}

void
sdmmc_scsi_cmd(struct scsi_xfer *xs)
{
	struct scsi_link *link = xs->sc_link;
	struct sdmmc_softc *sc = link->bus->sb_adapter_softc;
	struct sdmmc_scsi_softc *scbus = sc->sc_scsibus;
	struct sdmmc_scsi_target *tgt = &scbus->sc_tgt[link->target];
	struct scsi_read_cap_data rcd;
	u_int32_t blockno;
	u_int32_t blockcnt;
	struct sdmmc_ccb *ccb;

	if (link->target >= scbus->sc_ntargets || tgt->card == NULL ||
	    link->lun != 0) {
		DPRINTF(("%s: sdmmc_scsi_cmd: no target %d\n",
		    DEVNAME(sc), link->target));
		/* XXX should be XS_SENSE and sense filled out */
		xs->error = XS_DRIVER_STUFFUP;
		scsi_done(xs);
		return;
	}

	DPRINTF(("%s: scsi cmd target=%d opcode=%#x proc=\"%s\" (poll=%#x)\n",
	    DEVNAME(sc), link->target, xs->cmd.opcode, curproc ?
	    curproc->p_p->ps_comm : "", xs->flags & SCSI_POLL));

	xs->error = XS_NOERROR;

	switch (xs->cmd.opcode) {
	case READ_COMMAND:
	case READ_10:
	case WRITE_COMMAND:
	case WRITE_10:
		/* Deal with I/O outside the switch. */
		break;

	case INQUIRY:
		sdmmc_inquiry(xs);
		return;

	case TEST_UNIT_READY:
	case START_STOP:
	case SYNCHRONIZE_CACHE:
		scsi_done(xs);
		return;

	case READ_CAPACITY:
		bzero(&rcd, sizeof rcd);
		_lto4b(tgt->card->csd.capacity - 1, rcd.addr);
		_lto4b(tgt->card->csd.sector_size, rcd.length);
		bcopy(&rcd, xs->data, MIN(xs->datalen, sizeof rcd));
		scsi_done(xs);
		return;

	default:
		DPRINTF(("%s: unsupported scsi command %#x\n",
		    DEVNAME(sc), xs->cmd.opcode));
		xs->error = XS_DRIVER_STUFFUP;
		scsi_done(xs);
		return;
	}

	/* A read or write operation. */
	sdmmc_scsi_decode_rw(xs, &blockno, &blockcnt);

	if (blockno >= tgt->card->csd.capacity ||
	    blockno + blockcnt > tgt->card->csd.capacity) {
		DPRINTF(("%s: out of bounds %u-%u >= %u\n", DEVNAME(sc),
		    blockno, blockcnt, tgt->card->csd.capacity));
		xs->error = XS_DRIVER_STUFFUP;
		scsi_done(xs);
		return;
	}

	ccb = xs->io;

	ccb->ccb_xs = xs;
	ccb->ccb_blockcnt = blockcnt;
	ccb->ccb_blockno = blockno;

	sdmmc_start_xs(sc, ccb);
}

void
sdmmc_inquiry(struct scsi_xfer *xs)
{
	struct scsi_link *link = xs->sc_link;
	struct sdmmc_softc *sc = link->bus->sb_adapter_softc;
	struct sdmmc_scsi_softc *scbus = sc->sc_scsibus;
	struct sdmmc_scsi_target *tgt = &scbus->sc_tgt[link->target];
	struct scsi_inquiry_data inq;
	struct scsi_inquiry *cdb = (struct scsi_inquiry *)&xs->cmd;
	char vendor[sizeof(inq.vendor) + 1];
	char product[sizeof(inq.product) + 1];
	char revision[sizeof(inq.revision) + 1];

        if (xs->cmdlen != sizeof(*cdb)) {
		xs->error = XS_DRIVER_STUFFUP;
		goto done;
	}

	if (ISSET(cdb->flags, SI_EVPD)) {
		xs->error = XS_DRIVER_STUFFUP;
		goto done;
	}

	memset(vendor, 0, sizeof(vendor));
	memset(product, 0, sizeof(product));
	memset(revision, 0, sizeof(revision));
	switch (tgt->card->cid.mid) {
	case 0x02:
	case 0x03:
	case 0x45:
		strlcpy(vendor, "Sandisk", sizeof(vendor));
		break;
	case 0x11:
		strlcpy(vendor, "Toshiba", sizeof(vendor));
		break;
	case 0x13:
		strlcpy(vendor, "Micron", sizeof(vendor));
		break;
	case 0x15:
		strlcpy(vendor, "Samsung", sizeof(vendor));
		break;
	case 0x27:
		strlcpy(vendor, "Apacer", sizeof(vendor));
		break;
	case 0x70:
		strlcpy(vendor, "Kingston", sizeof(vendor));
		break;
	case 0x90:
		strlcpy(vendor, "Hynix", sizeof(vendor));
		break;
	default:
		strlcpy(vendor, "SD/MMC", sizeof(vendor));
		break;
	}
	strlcpy(product, tgt->card->cid.pnm, sizeof(product));
	snprintf(revision, sizeof(revision), "%04X", tgt->card->cid.rev);

	memset(&inq, 0, sizeof inq);
	inq.device = T_DIRECT;
	if (!ISSET(sc->sc_caps, SMC_CAPS_NONREMOVABLE))
		inq.dev_qual2 = SID_REMOVABLE;
	inq.version = SCSI_REV_2;
	inq.response_format = SID_SCSI2_RESPONSE;
	inq.additional_length = SID_SCSI2_ALEN;
	memcpy(inq.vendor, vendor, sizeof(inq.vendor));
	memcpy(inq.product, product, sizeof(inq.product));
	memcpy(inq.revision, revision, sizeof(inq.revision));

	scsi_copy_internal_data(xs, &inq, sizeof(inq));

done:
	scsi_done(xs);
}

void
sdmmc_start_xs(struct sdmmc_softc *sc, struct sdmmc_ccb *ccb)
{
	struct sdmmc_scsi_softc *scbus = sc->sc_scsibus;
	struct scsi_xfer *xs = ccb->ccb_xs;
	int s;

	timeout_set(&xs->stimeout, sdmmc_stimeout, ccb);
	sdmmc_init_task(&ccb->ccb_task, sdmmc_complete_xs, ccb);

	s = splbio();
	TAILQ_INSERT_TAIL(&scbus->sc_ccb_runq, ccb, ccb_link);
	ccb->ccb_state = SDMMC_CCB_QUEUED;
	splx(s);

	if (ISSET(xs->flags, SCSI_POLL)) {
		sdmmc_complete_xs(ccb);
		return;
	}

	timeout_add_msec(&xs->stimeout, xs->timeout);
	sdmmc_add_task(sc, &ccb->ccb_task);
}

void
sdmmc_complete_xs(void *arg)
{
	struct sdmmc_ccb *ccb = arg;
	struct scsi_xfer *xs = ccb->ccb_xs;
	struct scsi_link *link = xs->sc_link;
	struct sdmmc_softc *sc = link->bus->sb_adapter_softc;
	struct sdmmc_scsi_softc *scbus = sc->sc_scsibus;
	struct sdmmc_scsi_target *tgt = &scbus->sc_tgt[link->target];
	int error;
	int s;

	DPRINTF(("%s: scsi cmd target=%d opcode=%#x proc=\"%s\" (poll=%#x)"
	    " complete\n", DEVNAME(sc), link->target, xs->cmd.opcode,
	    curproc ? curproc->p_p->ps_comm : "", xs->flags & SCSI_POLL));

	s = splbio();

	if (ISSET(xs->flags, SCSI_DATA_IN))
		error = sdmmc_mem_read_block(tgt->card, ccb->ccb_blockno,
		    xs->data, ccb->ccb_blockcnt * DEV_BSIZE);
	else
		error = sdmmc_mem_write_block(tgt->card, ccb->ccb_blockno,
		    xs->data, ccb->ccb_blockcnt * DEV_BSIZE);

	if (error != 0)
		xs->error = XS_DRIVER_STUFFUP;

	sdmmc_done_xs(ccb);
	splx(s);
}

void
sdmmc_done_xs(struct sdmmc_ccb *ccb)
{
	struct scsi_xfer *xs = ccb->ccb_xs;
#ifdef SDMMC_DEBUG
	struct scsi_link *link = xs->sc_link;
	struct sdmmc_softc *sc = link->bus->sb_adapter_softc;
#endif

	timeout_del(&xs->stimeout);

	DPRINTF(("%s: scsi cmd target=%d opcode=%#x proc=\"%s\" (error=%#x)"
	    " done\n", DEVNAME(sc), link->target, xs->cmd.opcode,
	    curproc ? curproc->p_p->ps_comm : "", xs->error));

	xs->resid = 0;

	if (ISSET(ccb->ccb_flags, SDMMC_CCB_F_ERR))
		xs->error = XS_DRIVER_STUFFUP;

	scsi_done(xs);
}

void
sdmmc_stimeout(void *arg)
{
	struct sdmmc_ccb *ccb = arg;
	int s;

	s = splbio();
	ccb->ccb_flags |= SDMMC_CCB_F_ERR;
	if (sdmmc_task_pending(&ccb->ccb_task)) {
		sdmmc_del_task(&ccb->ccb_task);
		sdmmc_done_xs(ccb);
	}
	splx(s);
}

void
sdmmc_minphys(struct buf *bp, struct scsi_link *sl)
{
	struct sdmmc_softc *sc = sl->bus->sb_adapter_softc;
	struct sdmmc_scsi_softc *scbus = sc->sc_scsibus;
	struct sdmmc_scsi_target *tgt = &scbus->sc_tgt[sl->target];
	struct sdmmc_function *sf = tgt->card;

	/* limit to max. transfer size supported by card/host */
	if (sc->sc_max_xfer != 0 &&
	    bp->b_bcount > sf->csd.sector_size * sc->sc_max_xfer)
		bp->b_bcount = sf->csd.sector_size * sc->sc_max_xfer;
	else
		minphys(bp);
}

#ifdef HIBERNATE
int
sdmmc_scsi_hibernate_io(dev_t dev, daddr_t blkno, vaddr_t addr, size_t size,
    int op, void *page)
{
	struct {
		struct sdmmc_softc sdmmc_sc;
		struct sdmmc_function sdmmc_sf;
		daddr_t poffset;
		size_t psize;
		struct sdmmc_function *orig_sf;
		char chipset_softc[0];	/* size depends on the chipset layer */
	} *state = page;
	extern struct cfdriver sd_cd;
	struct device *disk, *scsibus, *chip, *sdmmc;
	struct scsibus_softc *bus_sc;
	struct sdmmc_scsi_softc *scsi_sc;
	struct scsi_link *link;
	struct sdmmc_function *sf;
	struct sdmmc_softc *sc;
	int error;

	switch (op) {
	case HIB_INIT:
		/* find device (sdmmc_softc, sdmmc_function) */
		disk = disk_lookup(&sd_cd, DISKUNIT(dev));
		if (disk == NULL)
			return (ENOTTY);

		scsibus = disk->dv_parent;
		sdmmc = scsibus->dv_parent;
		chip = sdmmc->dv_parent;

		bus_sc = (struct scsibus_softc *)scsibus;
		scsi_sc = (struct sdmmc_scsi_softc *)scsibus;
		sc = NULL;
		SLIST_FOREACH(link, &bus_sc->sc_link_list, bus_list) {
			if (link->device_softc == disk) {
				sc = link->bus->sb_adapter_softc;
				scsi_sc = sc->sc_scsibus;
				sf = scsi_sc->sc_tgt[link->target].card;
			}
		}
		if (sc == NULL || sf == NULL)
			return (ENOTTY);

		/* if the chipset doesn't do hibernate, bail out now */
		sc = (struct sdmmc_softc *)sdmmc;
		if (sc->sct->hibernate_init == NULL)
			return (ENOTTY);

		state->sdmmc_sc = *sc;
		state->sdmmc_sf = *sf;
		state->sdmmc_sf.sc = &state->sdmmc_sc;

		/* pretend we own the lock */
		state->sdmmc_sc.sc_lock.rwl_owner =
		    (((long)curproc) & ~RWLOCK_MASK) | RWLOCK_WRLOCK;

		/* build chip layer fake softc */
		error = state->sdmmc_sc.sct->hibernate_init(state->sdmmc_sc.sch,
		    &state->chipset_softc);
		if (error)
			return (error);
		state->sdmmc_sc.sch = state->chipset_softc;

		/* make sure we're talking to the right target */
		state->orig_sf = sc->sc_card;
		error = sdmmc_select_card(&state->sdmmc_sc, &state->sdmmc_sf);
		if (error)
			return (error);

		state->poffset = blkno;
		state->psize = size;
		return (0);

	case HIB_W:
		if (blkno > state->psize)
			return (E2BIG);
		return (sdmmc_mem_hibernate_write(&state->sdmmc_sf,
		    blkno + state->poffset, (u_char *)addr, size));

	case HIB_DONE:
		/*
		 * bring the hardware state back into line with the real
		 * softc by operating on the fake one
		 */
		return (sdmmc_select_card(&state->sdmmc_sc, state->orig_sf));
	}

	return (EINVAL);
}

#endif
