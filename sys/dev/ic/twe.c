/*	$OpenBSD: twe.c,v 1.67 2022/04/16 19:19:59 naddy Exp $	*/

/*
 * Copyright (c) 2000-2002 Michael Shalayeff.  All rights reserved.
 *
 * The SCSI emulation layer is derived from gdt(4) driver,
 * Copyright (c) 1999, 2000 Niklas Hallqvist. All rights reserved.
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
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/* #define	TWE_DEBUG */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/kthread.h>

#include <machine/bus.h>

#include <scsi/scsi_all.h>
#include <scsi/scsi_disk.h>
#include <scsi/scsiconf.h>

#include <dev/ic/twereg.h>
#include <dev/ic/twevar.h>

#ifdef TWE_DEBUG
#define	TWE_DPRINTF(m,a)	if (twe_debug & (m)) printf a
#define	TWE_D_CMD	0x0001
#define	TWE_D_INTR	0x0002
#define	TWE_D_MISC	0x0004
#define	TWE_D_DMA	0x0008
#define	TWE_D_AEN	0x0010
int twe_debug = 0;
#else
#define	TWE_DPRINTF(m,a)	/* m, a */
#endif

struct cfdriver twe_cd = {
	NULL, "twe", DV_DULL
};

void	twe_scsi_cmd(struct scsi_xfer *);

const struct scsi_adapter twe_switch = {
	twe_scsi_cmd, NULL, NULL, NULL, NULL
};

void *twe_get_ccb(void *);
void twe_put_ccb(void *, void *);
void twe_dispose(struct twe_softc *sc);
int  twe_cmd(struct twe_ccb *ccb, int flags, int wait);
int  twe_start(struct twe_ccb *ccb, int wait);
int  twe_complete(struct twe_ccb *ccb);
int  twe_done(struct twe_softc *sc, struct twe_ccb *ccb);
void twe_thread_create(void *v);
void twe_thread(void *v);
void twe_aen(void *, void *);

void *
twe_get_ccb(void *xsc)
{
	struct twe_softc *sc = xsc;
	struct twe_ccb *ccb;

	mtx_enter(&sc->sc_ccb_mtx);
	ccb = TAILQ_LAST(&sc->sc_free_ccb, twe_queue_head);
	if (ccb != NULL)
		TAILQ_REMOVE(&sc->sc_free_ccb, ccb, ccb_link);
	mtx_leave(&sc->sc_ccb_mtx);

	return (ccb);
}

void
twe_put_ccb(void *xsc, void *xccb)
{
	struct twe_softc *sc = xsc;
	struct twe_ccb *ccb = xccb;

	ccb->ccb_state = TWE_CCB_FREE;
	mtx_enter(&sc->sc_ccb_mtx);
	TAILQ_INSERT_TAIL(&sc->sc_free_ccb, ccb, ccb_link);
	mtx_leave(&sc->sc_ccb_mtx);
}

void
twe_dispose(struct twe_softc *sc)
{
	register struct twe_ccb *ccb;
	if (sc->sc_cmdmap != NULL) {
		bus_dmamap_destroy(sc->dmat, sc->sc_cmdmap);
		/* traverse the ccbs and destroy the maps */
		for (ccb = &sc->sc_ccbs[TWE_MAXCMDS - 1]; ccb >= sc->sc_ccbs; ccb--)
			if (ccb->ccb_dmamap)
				bus_dmamap_destroy(sc->dmat, ccb->ccb_dmamap);
	}
	bus_dmamem_unmap(sc->dmat, sc->sc_cmds,
	    sizeof(struct twe_cmd) * TWE_MAXCMDS);
	bus_dmamem_free(sc->dmat, sc->sc_cmdseg, 1);
}

int
twe_attach(struct twe_softc *sc)
{
	struct scsibus_attach_args saa;
	/* this includes a buffer for drive config req, and a capacity req */
	u_int8_t	param_buf[2 * TWE_SECTOR_SIZE + TWE_ALIGN - 1];
	struct twe_param *pb = (void *)
	    (((u_long)param_buf + TWE_ALIGN - 1) & ~(TWE_ALIGN - 1));
	struct twe_param *cap = (void *)((u_int8_t *)pb + TWE_SECTOR_SIZE);
	struct twe_ccb	*ccb;
	struct twe_cmd	*cmd;
	u_int32_t	status;
	int		error, i, retry, nunits, nseg;
	const char	*errstr;
	twe_lock_t	lock;
	paddr_t		pa;

	error = bus_dmamem_alloc(sc->dmat, sizeof(struct twe_cmd) * TWE_MAXCMDS,
	    PAGE_SIZE, 0, sc->sc_cmdseg, 1, &nseg, BUS_DMA_NOWAIT);
	if (error) {
		printf(": cannot allocate commands (%d)\n", error);
		return (1);
	}

	error = bus_dmamem_map(sc->dmat, sc->sc_cmdseg, nseg,
	    sizeof(struct twe_cmd) * TWE_MAXCMDS,
	    (caddr_t *)&sc->sc_cmds, BUS_DMA_NOWAIT);
	if (error) {
		printf(": cannot map commands (%d)\n", error);
		bus_dmamem_free(sc->dmat, sc->sc_cmdseg, 1);
		return (1);
	}

	error = bus_dmamap_create(sc->dmat,
	    sizeof(struct twe_cmd) * TWE_MAXCMDS, TWE_MAXCMDS,
	    sizeof(struct twe_cmd) * TWE_MAXCMDS, 0,
	    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &sc->sc_cmdmap);
	if (error) {
		printf(": cannot create ccb cmd dmamap (%d)\n", error);
		twe_dispose(sc);
		return (1);
	}
	error = bus_dmamap_load(sc->dmat, sc->sc_cmdmap, sc->sc_cmds,
	    sizeof(struct twe_cmd) * TWE_MAXCMDS, NULL, BUS_DMA_NOWAIT);
	if (error) {
		printf(": cannot load command dma map (%d)\n", error);
		twe_dispose(sc);
		return (1);
	}

	TAILQ_INIT(&sc->sc_ccb2q);
	TAILQ_INIT(&sc->sc_ccbq);
	TAILQ_INIT(&sc->sc_free_ccb);
	TAILQ_INIT(&sc->sc_done_ccb);
	mtx_init(&sc->sc_ccb_mtx, IPL_BIO);
	scsi_iopool_init(&sc->sc_iopool, sc, twe_get_ccb, twe_put_ccb);

	scsi_ioh_set(&sc->sc_aen, &sc->sc_iopool, twe_aen, sc);

	pa = sc->sc_cmdmap->dm_segs[0].ds_addr +
	    sizeof(struct twe_cmd) * (TWE_MAXCMDS - 1);
	for (cmd = (struct twe_cmd *)sc->sc_cmds + TWE_MAXCMDS - 1;
	     cmd >= (struct twe_cmd *)sc->sc_cmds; cmd--, pa -= sizeof(*cmd)) {

		cmd->cmd_index = cmd - (struct twe_cmd *)sc->sc_cmds;
		ccb = &sc->sc_ccbs[cmd->cmd_index];
		error = bus_dmamap_create(sc->dmat,
		    TWE_MAXFER, TWE_MAXOFFSETS, TWE_MAXFER, 0,
		    BUS_DMA_NOWAIT | BUS_DMA_ALLOCNOW, &ccb->ccb_dmamap);
		if (error) {
			printf(": cannot create ccb dmamap (%d)\n", error);
			twe_dispose(sc);
			return (1);
		}
		ccb->ccb_sc = sc;
		ccb->ccb_cmd = cmd;
		ccb->ccb_cmdpa = pa;
		ccb->ccb_state = TWE_CCB_FREE;
		TAILQ_INSERT_TAIL(&sc->sc_free_ccb, ccb, ccb_link);
	}

	for (errstr = NULL, retry = 3; retry--; ) {
		int		veseen_srst;
		u_int16_t	aen;

		if (errstr)
			TWE_DPRINTF(TWE_D_MISC, ("%s ", errstr));

		for (i = 350000; i--; DELAY(100)) {
			status = bus_space_read_4(sc->iot, sc->ioh, TWE_STATUS);
			if (status & TWE_STAT_CPURDY)
				break;
		}

		if (!(status & TWE_STAT_CPURDY)) {
			errstr = ": card CPU is not ready\n";
			continue;
		}

		/* soft reset, disable ints */
		bus_space_write_4(sc->iot, sc->ioh, TWE_CONTROL,
		    TWE_CTRL_SRST |
		    TWE_CTRL_CHOSTI | TWE_CTRL_CATTNI | TWE_CTRL_CERR |
		    TWE_CTRL_MCMDI | TWE_CTRL_MRDYI |
		    TWE_CTRL_MINT);

		for (i = 350000; i--; DELAY(100)) {
			status = bus_space_read_4(sc->iot, sc->ioh, TWE_STATUS);
			if (status & TWE_STAT_ATTNI)
				break;
		}

		if (!(status & TWE_STAT_ATTNI)) {
			errstr = ": cannot get card's attention\n";
			continue;
		}

		/* drain aen queue */
		for (veseen_srst = 0, aen = -1; aen != TWE_AEN_QEMPTY; ) {

			ccb = scsi_io_get(&sc->sc_iopool, 0);
			if (ccb == NULL) {
				errstr = ": out of ccbs\n";
				break;
			}

			ccb->ccb_xs = NULL;
			ccb->ccb_data = pb;
			ccb->ccb_length = TWE_SECTOR_SIZE;
			ccb->ccb_state = TWE_CCB_READY;
			cmd = ccb->ccb_cmd;
			cmd->cmd_unit_host = TWE_UNITHOST(0, 0);
			cmd->cmd_op = TWE_CMD_GPARAM;
			cmd->cmd_param.count = 1;

			pb->table_id = TWE_PARAM_AEN;
			pb->param_id = 2;
			pb->param_size = 2;

			error = twe_cmd(ccb, BUS_DMA_NOWAIT, 1);
			scsi_io_put(&sc->sc_iopool, ccb);
			if (error) {
				errstr = ": error draining attention queue\n";
				break;
			}

			aen = *(u_int16_t *)pb->data;
			TWE_DPRINTF(TWE_D_AEN, ("aen=%x ", aen));
			if (aen == TWE_AEN_SRST)
				veseen_srst++;
		}

		if (!veseen_srst) {
			errstr = ": we don't get it\n";
			continue;
		}

		if (status & TWE_STAT_CPUERR) {
			errstr = ": card CPU error detected\n";
			continue;
		}

		if (status & TWE_STAT_PCIPAR) {
			errstr = ": PCI parity error detected\n";
			continue;
		}

		if (status & TWE_STAT_QUEUEE ) {
			errstr = ": queuing error detected\n";
			continue;
		}

		if (status & TWE_STAT_PCIABR) {
			errstr = ": PCI abort\n";
			continue;
		}

		while (!(status & TWE_STAT_RQE)) {
			bus_space_read_4(sc->iot, sc->ioh, TWE_READYQUEUE);
			status = bus_space_read_4(sc->iot, sc->ioh, TWE_STATUS);
		}

		break;
	}

	if (retry < 0) {
		printf("%s", errstr);
		twe_dispose(sc);
		return 1;
	}

	ccb = scsi_io_get(&sc->sc_iopool, 0);
	if (ccb == NULL) {
		printf(": out of ccbs\n");
		twe_dispose(sc);
		return 1;
	}

	ccb->ccb_xs = NULL;
	ccb->ccb_data = pb;
	ccb->ccb_length = TWE_SECTOR_SIZE;
	ccb->ccb_state = TWE_CCB_READY;
	cmd = ccb->ccb_cmd;
	cmd->cmd_unit_host = TWE_UNITHOST(0, 0);
	cmd->cmd_op = TWE_CMD_GPARAM;
	cmd->cmd_param.count = 1;

	pb->table_id = TWE_PARAM_UC;
	pb->param_id = TWE_PARAM_UC;
	pb->param_size = TWE_MAX_UNITS;

	error = twe_cmd(ccb, BUS_DMA_NOWAIT, 1);
	scsi_io_put(&sc->sc_iopool, ccb);
	if (error) {
		printf(": failed to fetch unit parameters\n");
		twe_dispose(sc);
		return 1;
	}

	/* we are assuming last read status was good */
	printf(": Escalade V%d.%d\n", TWE_MAJV(status), TWE_MINV(status));

	for (nunits = i = 0; i < TWE_MAX_UNITS; i++) {
		if (pb->data[i] == 0)
			continue;

		ccb = scsi_io_get(&sc->sc_iopool, 0);
		if (ccb == NULL) {
			printf(": out of ccbs\n");
			twe_dispose(sc);
			return 1;
		}

		ccb->ccb_xs = NULL;
		ccb->ccb_data = cap;
		ccb->ccb_length = TWE_SECTOR_SIZE;
		ccb->ccb_state = TWE_CCB_READY;
		cmd = ccb->ccb_cmd;
		cmd->cmd_unit_host = TWE_UNITHOST(0, 0);
		cmd->cmd_op = TWE_CMD_GPARAM;
		cmd->cmd_param.count = 1;

		cap->table_id = TWE_PARAM_UI + i;
		cap->param_id = 4;
		cap->param_size = 4;	/* 4 bytes */

		lock = TWE_LOCK(sc);
		error = twe_cmd(ccb, BUS_DMA_NOWAIT, 1);
		TWE_UNLOCK(sc, lock);
		scsi_io_put(&sc->sc_iopool, ccb);
		if (error) {
			printf("%s: error fetching capacity for unit %d\n",
			    sc->sc_dev.dv_xname, i);
			continue;
		}

		nunits++;
		sc->sc_hdr[i].hd_present = 1;
		sc->sc_hdr[i].hd_devtype = 0;
		sc->sc_hdr[i].hd_size = letoh32(*(u_int32_t *)cap->data);
		TWE_DPRINTF(TWE_D_MISC, ("twed%d: size=%d\n",
		    i, sc->sc_hdr[i].hd_size));
	}

	if (!nunits)
		nunits++;

	/* TODO: fetch & print cache params? */

	saa.saa_adapter_softc = sc;
	saa.saa_adapter = &twe_switch;
	saa.saa_adapter_target = SDEV_NO_ADAPTER_TARGET;
	saa.saa_adapter_buswidth = TWE_MAX_UNITS;
	saa.saa_luns = 8;
	saa.saa_openings = TWE_MAXCMDS / nunits;
	saa.saa_pool = &sc->sc_iopool;
	saa.saa_quirks = saa.saa_flags = 0;
	saa.saa_wwpn = saa.saa_wwnn = 0;

	config_found(&sc->sc_dev, &saa, scsiprint);

	kthread_create_deferred(twe_thread_create, sc);

	return (0);
}

void
twe_thread_create(void *v)
{
	struct twe_softc *sc = v;

	if (kthread_create(twe_thread, sc, &sc->sc_thread,
	    sc->sc_dev.dv_xname)) {
		/* TODO disable twe */
		printf("%s: failed to create kernel thread, disabled\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	TWE_DPRINTF(TWE_D_CMD, ("stat=%b ",
	    bus_space_read_4(sc->iot, sc->ioh, TWE_STATUS), TWE_STAT_BITS));
	/*
	 * ack all before enable, cannot be done in one
	 * operation as it seems clear is not processed
	 * if enable is specified.
	 */
	bus_space_write_4(sc->iot, sc->ioh, TWE_CONTROL,
	    TWE_CTRL_CHOSTI | TWE_CTRL_CATTNI | TWE_CTRL_CERR);
	TWE_DPRINTF(TWE_D_CMD, ("stat=%b ",
	    bus_space_read_4(sc->iot, sc->ioh, TWE_STATUS), TWE_STAT_BITS));
	/* enable interrupts */
	bus_space_write_4(sc->iot, sc->ioh, TWE_CONTROL,
	    TWE_CTRL_EINT | TWE_CTRL_ERDYI |
	    /*TWE_CTRL_HOSTI |*/ TWE_CTRL_MCMDI);
}

void
twe_thread(void *v)
{
	struct twe_softc *sc = v;
	struct twe_ccb *ccb;
	twe_lock_t lock;
	u_int32_t status;
	int err;

	for (;;) {
		lock = TWE_LOCK(sc);

		while (!TAILQ_EMPTY(&sc->sc_done_ccb)) {
			ccb = TAILQ_FIRST(&sc->sc_done_ccb);
			TAILQ_REMOVE(&sc->sc_done_ccb, ccb, ccb_link);
			if ((err = twe_done(sc, ccb)))
				printf("%s: done failed (%d)\n",
				    sc->sc_dev.dv_xname, err);
		}

		status = bus_space_read_4(sc->iot, sc->ioh, TWE_STATUS);
		TWE_DPRINTF(TWE_D_INTR, ("twe_thread stat=%b ",
		    status & TWE_STAT_FLAGS, TWE_STAT_BITS));
		while (!(status & TWE_STAT_CQF) &&
		    !TAILQ_EMPTY(&sc->sc_ccb2q)) {

			ccb = TAILQ_LAST(&sc->sc_ccb2q, twe_queue_head);
			TAILQ_REMOVE(&sc->sc_ccb2q, ccb, ccb_link);

			ccb->ccb_state = TWE_CCB_QUEUED;
			TAILQ_INSERT_TAIL(&sc->sc_ccbq, ccb, ccb_link);
			bus_space_write_4(sc->iot, sc->ioh, TWE_COMMANDQUEUE,
			    ccb->ccb_cmdpa);

			status = bus_space_read_4(sc->iot, sc->ioh, TWE_STATUS);
			TWE_DPRINTF(TWE_D_INTR, ("twe_thread stat=%b ",
			    status & TWE_STAT_FLAGS, TWE_STAT_BITS));
		}

		if (!TAILQ_EMPTY(&sc->sc_ccb2q))
			bus_space_write_4(sc->iot, sc->ioh, TWE_CONTROL,
			    TWE_CTRL_ECMDI);

		TWE_UNLOCK(sc, lock);
		sc->sc_thread_on = 1;
		tsleep_nsec(sc, PWAIT, "twespank", INFSLP);
	}
}

int
twe_cmd(struct twe_ccb *ccb, int flags, int wait)
{
	struct twe_softc *sc = ccb->ccb_sc;
	bus_dmamap_t dmap;
	struct twe_cmd *cmd;
	struct twe_segs *sgp;
	int error, i;

	if (ccb->ccb_data && ((u_long)ccb->ccb_data & (TWE_ALIGN - 1))) {
		TWE_DPRINTF(TWE_D_DMA, ("data=%p is unaligned ",ccb->ccb_data));
		ccb->ccb_realdata = ccb->ccb_data;

		error = bus_dmamem_alloc(sc->dmat, ccb->ccb_length, PAGE_SIZE,
		    0, ccb->ccb_2bseg, TWE_MAXOFFSETS, &ccb->ccb_2nseg,
		    BUS_DMA_NOWAIT);
		if (error) {
			TWE_DPRINTF(TWE_D_DMA, ("2buf alloc failed(%d) ", error));
			return (ENOMEM);
		}

		error = bus_dmamem_map(sc->dmat, ccb->ccb_2bseg, ccb->ccb_2nseg,
		    ccb->ccb_length, (caddr_t *)&ccb->ccb_data, BUS_DMA_NOWAIT);
		if (error) {
			TWE_DPRINTF(TWE_D_DMA, ("2buf map failed(%d) ", error));
			bus_dmamem_free(sc->dmat, ccb->ccb_2bseg, ccb->ccb_2nseg);
			return (ENOMEM);
		}
		bcopy(ccb->ccb_realdata, ccb->ccb_data, ccb->ccb_length);
	} else
		ccb->ccb_realdata = NULL;

	dmap = ccb->ccb_dmamap;
	cmd = ccb->ccb_cmd;
	cmd->cmd_status = 0;

	if (ccb->ccb_data) {
		error = bus_dmamap_load(sc->dmat, dmap, ccb->ccb_data,
		    ccb->ccb_length, NULL, flags);
		if (error) {
			if (error == EFBIG)
				printf("more than %d dma segs\n", TWE_MAXOFFSETS);
			else
				printf("error %d loading dma map\n", error);

			if (ccb->ccb_realdata) {
				bus_dmamem_unmap(sc->dmat, ccb->ccb_data,
				    ccb->ccb_length);
				bus_dmamem_free(sc->dmat, ccb->ccb_2bseg,
				    ccb->ccb_2nseg);
			}
			return error;
		}
		/* load addresses into command */
		switch (cmd->cmd_op) {
		case TWE_CMD_GPARAM:
		case TWE_CMD_SPARAM:
			sgp = cmd->cmd_param.segs;
			break;
		case TWE_CMD_READ:
		case TWE_CMD_WRITE:
			sgp = cmd->cmd_io.segs;
			break;
		default:
			/* no data transfer */
			TWE_DPRINTF(TWE_D_DMA, ("twe_cmd: unknown sgp op=%x\n",
			    cmd->cmd_op));
			sgp = NULL;
			break;
		}
		TWE_DPRINTF(TWE_D_DMA, ("data=%p<", ccb->ccb_data));
		if (sgp) {
			/*
			 * we know that size is in the upper byte,
			 * and we do not worry about overflow
			 */
			cmd->cmd_op += (2 * dmap->dm_nsegs) << 8;
			bzero (sgp, TWE_MAXOFFSETS * sizeof(*sgp));
			for (i = 0; i < dmap->dm_nsegs; i++, sgp++) {
				sgp->twes_addr = htole32(dmap->dm_segs[i].ds_addr);
				sgp->twes_len  = htole32(dmap->dm_segs[i].ds_len);
				TWE_DPRINTF(TWE_D_DMA, ("%lx[%lx] ",
				    dmap->dm_segs[i].ds_addr,
				    dmap->dm_segs[i].ds_len));
			}
		}
		TWE_DPRINTF(TWE_D_DMA, ("> "));
		bus_dmamap_sync(sc->dmat, dmap, 0, dmap->dm_mapsize,
		    BUS_DMASYNC_PREWRITE);
	}
	bus_dmamap_sync(sc->dmat, sc->sc_cmdmap, 0, sc->sc_cmdmap->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	if ((error = twe_start(ccb, wait))) {
		bus_dmamap_unload(sc->dmat, dmap);
		if (ccb->ccb_realdata) {
			bus_dmamem_unmap(sc->dmat, ccb->ccb_data,
			    ccb->ccb_length);
			bus_dmamem_free(sc->dmat, ccb->ccb_2bseg,
			    ccb->ccb_2nseg);
		}
		return (error);
	}

	return wait? twe_complete(ccb) : 0;
}

int
twe_start(struct twe_ccb *ccb, int wait)
{
	struct twe_softc*sc = ccb->ccb_sc;
	struct twe_cmd	*cmd = ccb->ccb_cmd;
	u_int32_t	status;
	int i;

	cmd->cmd_op = htole16(cmd->cmd_op);

	if (!wait) {

		TWE_DPRINTF(TWE_D_CMD, ("prequeue(%d) ", cmd->cmd_index));
		ccb->ccb_state = TWE_CCB_PREQUEUED;
		TAILQ_INSERT_TAIL(&sc->sc_ccb2q, ccb, ccb_link);
		wakeup(sc);
		return 0;
	}

	for (i = 1000; i--; DELAY(10)) {

		status = bus_space_read_4(sc->iot, sc->ioh, TWE_STATUS);
		if (!(status & TWE_STAT_CQF))
			break;
		TWE_DPRINTF(TWE_D_CMD,  ("twe_start stat=%b ",
		    status & TWE_STAT_FLAGS, TWE_STAT_BITS));
	}

	if (!(status & TWE_STAT_CQF)) {
		bus_space_write_4(sc->iot, sc->ioh, TWE_COMMANDQUEUE,
		    ccb->ccb_cmdpa);

		TWE_DPRINTF(TWE_D_CMD, ("queue(%d) ", cmd->cmd_index));
		ccb->ccb_state = TWE_CCB_QUEUED;
		TAILQ_INSERT_TAIL(&sc->sc_ccbq, ccb, ccb_link);
		return 0;

	} else {

		printf("%s: twe_start(%d) timed out\n",
		    sc->sc_dev.dv_xname, cmd->cmd_index);

		return EPERM;
	}
}

int
twe_complete(struct twe_ccb *ccb)
{
	struct twe_softc *sc = ccb->ccb_sc;
	struct scsi_xfer *xs = ccb->ccb_xs;
	int i;

	for (i = 100 * (xs? xs->timeout : 35000); i--; DELAY(10)) {
		u_int32_t status = bus_space_read_4(sc->iot, sc->ioh, TWE_STATUS);

		/* TWE_DPRINTF(TWE_D_CMD,  ("twe_intr stat=%b ",
		    status & TWE_STAT_FLAGS, TWE_STAT_BITS)); */

		while (!(status & TWE_STAT_RQE)) {
			struct twe_ccb *ccb1;
			u_int32_t ready;

			ready = bus_space_read_4(sc->iot, sc->ioh,
			    TWE_READYQUEUE);

			TWE_DPRINTF(TWE_D_CMD, ("ready=%x ", ready));

			ccb1 = &sc->sc_ccbs[TWE_READYID(ready)];
			TAILQ_REMOVE(&sc->sc_ccbq, ccb1, ccb_link);
			ccb1->ccb_state = TWE_CCB_DONE;
			if (!twe_done(sc, ccb1) && ccb1 == ccb) {
				TWE_DPRINTF(TWE_D_CMD, ("complete\n"));
				return 0;
			}

			status = bus_space_read_4(sc->iot, sc->ioh, TWE_STATUS);
			/* TWE_DPRINTF(TWE_D_CMD,  ("twe_intr stat=%b ",
			    status & TWE_STAT_FLAGS, TWE_STAT_BITS)); */
		}
	}

	return 1;
}

int
twe_done(struct twe_softc *sc, struct twe_ccb *ccb)
{
	struct twe_cmd *cmd = ccb->ccb_cmd;
	struct scsi_xfer *xs = ccb->ccb_xs;
	bus_dmamap_t	dmap;
	twe_lock_t	lock;

	TWE_DPRINTF(TWE_D_CMD, ("done(%d) ", cmd->cmd_index));

	if (ccb->ccb_state != TWE_CCB_DONE) {
		printf("%s: undone ccb %d ready\n",
		     sc->sc_dev.dv_xname, cmd->cmd_index);
		return 1;
	}

	dmap = ccb->ccb_dmamap;
	if (xs) {
		if (xs->cmd.opcode != PREVENT_ALLOW &&
		    xs->cmd.opcode != SYNCHRONIZE_CACHE) {
			bus_dmamap_sync(sc->dmat, dmap, 0,
			    dmap->dm_mapsize, (xs->flags & SCSI_DATA_IN) ?
			    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->dmat, dmap);
		}
	} else {
		switch (letoh16(cmd->cmd_op)) {
		case TWE_CMD_GPARAM:
		case TWE_CMD_READ:
			bus_dmamap_sync(sc->dmat, dmap, 0,
			    dmap->dm_mapsize, BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->dmat, dmap);
			break;
		case TWE_CMD_SPARAM:
		case TWE_CMD_WRITE:
			bus_dmamap_sync(sc->dmat, dmap, 0,
			    dmap->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->dmat, dmap);
			break;
		default:
			/* no data */
			break;
		}
	}

	if (ccb->ccb_realdata) {
		bcopy(ccb->ccb_data, ccb->ccb_realdata, ccb->ccb_length);
		bus_dmamem_unmap(sc->dmat, ccb->ccb_data, ccb->ccb_length);
		bus_dmamem_free(sc->dmat, ccb->ccb_2bseg, ccb->ccb_2nseg);
	}

	lock = TWE_LOCK(sc);

	if (xs) {
		xs->resid = 0;
		scsi_done(xs);
	}
	TWE_UNLOCK(sc, lock);

	return 0;
}

void
twe_scsi_cmd(struct scsi_xfer *xs)
{
	struct scsi_link *link = xs->sc_link;
	struct twe_softc *sc = link->bus->sb_adapter_softc;
	struct twe_ccb *ccb = xs->io;
	struct twe_cmd *cmd;
	struct scsi_inquiry_data inq;
	struct scsi_sense_data sd;
	struct scsi_read_cap_data rcd;
	u_int8_t target = link->target;
	u_int32_t blockno, blockcnt;
	struct scsi_rw *rw;
	struct scsi_rw_10 *rw10;
	int error, op, flags, wait;
	twe_lock_t lock;


	if (target >= TWE_MAX_UNITS || !sc->sc_hdr[target].hd_present ||
	    link->lun != 0) {
		xs->error = XS_DRIVER_STUFFUP;
		scsi_done(xs);
		return;
	}

	TWE_DPRINTF(TWE_D_CMD, ("twe_scsi_cmd "));

	xs->error = XS_NOERROR;

	switch (xs->cmd.opcode) {
	case TEST_UNIT_READY:
	case START_STOP:
#if 0
	case VERIFY:
#endif
		TWE_DPRINTF(TWE_D_CMD, ("opc %d tgt %d ", xs->cmd.opcode,
		    target));
		break;

	case REQUEST_SENSE:
		TWE_DPRINTF(TWE_D_CMD, ("REQUEST SENSE tgt %d ", target));
		bzero(&sd, sizeof sd);
		sd.error_code = SSD_ERRCODE_CURRENT;
		sd.segment = 0;
		sd.flags = SKEY_NO_SENSE;
		*(u_int32_t*)sd.info = htole32(0);
		sd.extra_len = 0;
		scsi_copy_internal_data(xs, &sd, sizeof(sd));
		break;

	case INQUIRY:
		TWE_DPRINTF(TWE_D_CMD, ("INQUIRY tgt %d devtype %x ", target,
		    sc->sc_hdr[target].hd_devtype));
		bzero(&inq, sizeof inq);
		inq.device =
		    (sc->sc_hdr[target].hd_devtype & 4) ? T_CDROM : T_DIRECT;
		inq.dev_qual2 =
		    (sc->sc_hdr[target].hd_devtype & 1) ? SID_REMOVABLE : 0;
		inq.version = SCSI_REV_2;
		inq.response_format = SID_SCSI2_RESPONSE;
		inq.additional_length = SID_SCSI2_ALEN;
		strlcpy(inq.vendor, "3WARE  ", sizeof inq.vendor);
		snprintf(inq.product, sizeof inq.product, "Host drive  #%02d",
		    target);
		strlcpy(inq.revision, "   ", sizeof inq.revision);
		scsi_copy_internal_data(xs, &inq, sizeof(inq));
		break;

	case READ_CAPACITY:
		TWE_DPRINTF(TWE_D_CMD, ("READ CAPACITY tgt %d ", target));
		bzero(&rcd, sizeof rcd);
		_lto4b(sc->sc_hdr[target].hd_size - 1, rcd.addr);
		_lto4b(TWE_SECTOR_SIZE, rcd.length);
		scsi_copy_internal_data(xs, &rcd, sizeof(rcd));
		break;

	case PREVENT_ALLOW:
		TWE_DPRINTF(TWE_D_CMD, ("PREVENT/ALLOW "));
		scsi_done(xs);
		return;

	case READ_COMMAND:
	case READ_10:
	case WRITE_COMMAND:
	case WRITE_10:
	case SYNCHRONIZE_CACHE:
		lock = TWE_LOCK(sc);

		flags = 0;
		if (xs->cmd.opcode == SYNCHRONIZE_CACHE) {
			blockno = blockcnt = 0;
		} else {
			/* A read or write operation. */
			if (xs->cmdlen == 6) {
				rw = (struct scsi_rw *)&xs->cmd;
				blockno = _3btol(rw->addr) &
				    (SRW_TOPADDR << 16 | 0xffff);
				blockcnt = rw->length ? rw->length : 0x100;
			} else {
				rw10 = (struct scsi_rw_10 *)&xs->cmd;
				blockno = _4btol(rw10->addr);
				blockcnt = _2btol(rw10->length);
				/* reflect DPO & FUA flags */
				if (xs->cmd.opcode == WRITE_10 &&
				    rw10->byte2 & 0x18)
					flags = TWE_FLAGS_CACHEDISABLE;
			}
			if (blockno >= sc->sc_hdr[target].hd_size ||
			    blockno + blockcnt > sc->sc_hdr[target].hd_size) {
				printf("%s: out of bounds %u-%u >= %u\n",
				    sc->sc_dev.dv_xname, blockno, blockcnt,
				    sc->sc_hdr[target].hd_size);
				xs->error = XS_DRIVER_STUFFUP;
				scsi_done(xs);
				TWE_UNLOCK(sc, lock);
				return;
			}
		}

		switch (xs->cmd.opcode) {
		case READ_COMMAND:	op = TWE_CMD_READ;	break;
		case READ_10:		op = TWE_CMD_READ;	break;
		case WRITE_COMMAND:	op = TWE_CMD_WRITE;	break;
		case WRITE_10:		op = TWE_CMD_WRITE;	break;
		default:		op = TWE_CMD_NOP;	break;
		}

		ccb->ccb_xs = xs;
		ccb->ccb_data = xs->data;
		ccb->ccb_length = xs->datalen;
		ccb->ccb_state = TWE_CCB_READY;
		cmd = ccb->ccb_cmd;
		cmd->cmd_unit_host = TWE_UNITHOST(target, 0); /* XXX why 0? */
		cmd->cmd_op = op;
		cmd->cmd_flags = flags;
		cmd->cmd_io.count = htole16(blockcnt);
		cmd->cmd_io.lba = htole32(blockno);
		wait = xs->flags & SCSI_POLL;
		if (!sc->sc_thread_on)
			wait |= SCSI_POLL;

		if ((error = twe_cmd(ccb, ((xs->flags & SCSI_NOSLEEP)?
		    BUS_DMA_NOWAIT : BUS_DMA_WAITOK), wait))) {

			TWE_DPRINTF(TWE_D_CMD, ("failed %p ", xs));
			xs->error = XS_DRIVER_STUFFUP;
			scsi_done(xs);
		}

		TWE_UNLOCK(sc, lock);
		return;

	default:
		TWE_DPRINTF(TWE_D_CMD, ("unsupported scsi command %#x tgt %d ",
		    xs->cmd.opcode, target));
		xs->error = XS_DRIVER_STUFFUP;
	}

	scsi_done(xs);
}

int
twe_intr(void *v)
{
	struct twe_softc *sc = v;
	struct twe_ccb	*ccb;
	u_int32_t	status;
	int		rv = 0;

	status = bus_space_read_4(sc->iot, sc->ioh, TWE_STATUS);
	TWE_DPRINTF(TWE_D_INTR,  ("twe_intr stat=%b ",
	    status & TWE_STAT_FLAGS, TWE_STAT_BITS));
#if 0
	if (status & TWE_STAT_HOSTI) {

		bus_space_write_4(sc->iot, sc->ioh, TWE_CONTROL,
		    TWE_CTRL_CHOSTI);
	}
#endif

	if (status & TWE_STAT_RDYI) {

		while (!(status & TWE_STAT_RQE)) {

			u_int32_t ready;

			/*
			 * it seems that reading ready queue
			 * we get all the status bits in each ready word.
			 * i wonder if it's legal to use those for
			 * status and avoid extra read below
			 */
			ready = bus_space_read_4(sc->iot, sc->ioh,
			    TWE_READYQUEUE);

			ccb = &sc->sc_ccbs[TWE_READYID(ready)];
			TAILQ_REMOVE(&sc->sc_ccbq, ccb, ccb_link);
			ccb->ccb_state = TWE_CCB_DONE;
			TAILQ_INSERT_TAIL(&sc->sc_done_ccb, ccb, ccb_link);
			rv++;

			status = bus_space_read_4(sc->iot, sc->ioh, TWE_STATUS);
			TWE_DPRINTF(TWE_D_INTR, ("twe_intr stat=%b ",
			    status & TWE_STAT_FLAGS, TWE_STAT_BITS));
		}
	}

	if (status & TWE_STAT_CMDI) {
		rv++;
		bus_space_write_4(sc->iot, sc->ioh, TWE_CONTROL,
		    TWE_CTRL_MCMDI);
	}

	if (rv)
		wakeup(sc);

	if (status & TWE_STAT_ATTNI) {
		/*
		 * we know no attentions of interest right now.
		 * one of those would be mirror degradation i think.
		 * or, what else exists in there?
		 * maybe 3ware can answer that?
		 */
		bus_space_write_4(sc->iot, sc->ioh, TWE_CONTROL,
		    TWE_CTRL_CATTNI);

		scsi_ioh_add(&sc->sc_aen);
	}

	return rv;
}

void
twe_aen(void *cookie, void *io)
{
	struct twe_softc *sc = cookie;
	struct twe_ccb *ccb = io;
	struct twe_cmd *cmd = ccb->ccb_cmd;

	u_int8_t param_buf[2 * TWE_SECTOR_SIZE + TWE_ALIGN - 1];
	struct twe_param *pb = (void *) (((u_long)param_buf +
	    TWE_ALIGN - 1) & ~(TWE_ALIGN - 1));
	u_int16_t aen;

	twe_lock_t lock;
	int error;

	ccb->ccb_xs = NULL;
	ccb->ccb_data = pb;
	ccb->ccb_length = TWE_SECTOR_SIZE;
	ccb->ccb_state = TWE_CCB_READY;
	cmd->cmd_unit_host = TWE_UNITHOST(0, 0);
	cmd->cmd_op = TWE_CMD_GPARAM;
	cmd->cmd_flags = 0;
	cmd->cmd_param.count = 1;

	pb->table_id = TWE_PARAM_AEN;
	pb->param_id = 2;
	pb->param_size = 2;

	lock = TWE_LOCK(sc);
	error = twe_cmd(ccb, BUS_DMA_NOWAIT, 1);
	TWE_UNLOCK(sc, lock);
	scsi_io_put(&sc->sc_iopool, ccb);

	if (error) {
		printf("%s: error draining attention queue\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	aen = *(u_int16_t *)pb->data;
	if (aen != TWE_AEN_QEMPTY)
		scsi_ioh_add(&sc->sc_aen);
}
