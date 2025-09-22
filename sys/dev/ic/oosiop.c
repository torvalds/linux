/*	$OpenBSD: oosiop.c,v 1.37 2024/02/13 17:51:17 miod Exp $	*/
/*	$NetBSD: oosiop.c,v 1.4 2003/10/29 17:45:55 tsutsui Exp $	*/

/*
 * Copyright (c) 2001 Shuichiro URATA.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 * NCR53C700 SCSI I/O processor (OOSIOP) driver
 *
 * TODO:
 *   - Better error handling.
 *   - Implement tagged queuing.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/timeout.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/buf.h>
#include <sys/malloc.h>
#include <sys/queue.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>
#include <scsi/scsi_message.h>

#include <machine/cpu.h>
#include <machine/bus.h>

#include <dev/ic/oosiopreg.h>
#include <dev/ic/oosiopvar.h>

/* 53C700 script */
#include <dev/microcode/siop/oosiop.out>

int	oosiop_alloc_cb(struct oosiop_softc *, int);

static __inline void oosiop_relocate_io(struct oosiop_softc *, bus_addr_t);
static __inline void oosiop_relocate_tc(struct oosiop_softc *, bus_addr_t);
static __inline void oosiop_fixup_select(struct oosiop_softc *, bus_addr_t,
		         int);
static __inline void oosiop_fixup_jump(struct oosiop_softc *, bus_addr_t,
		         bus_addr_t);
static __inline void oosiop_fixup_move(struct oosiop_softc *, bus_addr_t,
		         bus_size_t, bus_addr_t);

void	oosiop_load_script(struct oosiop_softc *);
void	oosiop_setup_sgdma(struct oosiop_softc *, struct oosiop_cb *);
void	oosiop_setup_dma(struct oosiop_softc *);
void	oosiop_flush_fifo(struct oosiop_softc *);
void	oosiop_clear_fifo(struct oosiop_softc *);
void	oosiop_phasemismatch(struct oosiop_softc *);
void	oosiop_setup_syncxfer(struct oosiop_softc *);
void	oosiop_set_syncparam(struct oosiop_softc *, int, int, int);
void	oosiop_scsicmd(struct scsi_xfer *);
void	oosiop_done(struct oosiop_softc *, struct oosiop_cb *);
void	oosiop_timeout(void *);
void	oosiop_reset(struct oosiop_softc *, int);
void	oosiop_reset_bus(struct oosiop_softc *);
void	oosiop_scriptintr(struct oosiop_softc *);
void	oosiop_msgin(struct oosiop_softc *, struct oosiop_cb *);
void	oosiop_setup(struct oosiop_softc *, struct oosiop_cb *);
void	oosiop_poll(struct oosiop_softc *, struct oosiop_cb *);
void	oosiop_processintr(struct oosiop_softc *, u_int8_t);

void	*oosiop_cb_alloc(void *);
void	oosiop_cb_free(void *, void *);

/* Trap interrupt code for unexpected data I/O */
#define	DATAIN_TRAP	0xdead0001
#define	DATAOUT_TRAP	0xdead0002

/* Possible TP and SCF combination */
static const struct {
	u_int8_t	tp;
	u_int8_t	scf;
} synctbl[] = {
	{0, 1},		/* SCLK /  4.0 */
	{1, 1},		/* SCLK /  5.0 */
	{2, 1},		/* SCLK /  6.0 */
	{3, 1},		/* SCLK /  7.0 */
	{1, 2},		/* SCLK /  7.5 */
	{4, 1},		/* SCLK /  8.0 */
	{5, 1},		/* SCLK /  9.0 */
	{6, 1},		/* SCLK / 10.0 */
	{3, 2},		/* SCLK / 10.5 */
	{7, 1},		/* SCLK / 11.0 */
	{4, 2},		/* SCLK / 12.0 */
	{5, 2},		/* SCLK / 13.5 */
	{3, 3},		/* SCLK / 14.0 */
	{6, 2},		/* SCLK / 15.0 */
	{4, 3},		/* SCLK / 16.0 */
	{7, 2},		/* SCLK / 16.5 */
	{5, 3},		/* SCLK / 18.0 */
	{6, 3},		/* SCLK / 20.0 */
	{7, 3}		/* SCLK / 22.0 */
};
#define	NSYNCTBL	(sizeof(synctbl) / sizeof(synctbl[0]))

#define	oosiop_period(sc, tp, scf)					\
	    (((1000000000 / (sc)->sc_freq) * (tp) * (scf)) / 40)

struct cfdriver oosiop_cd = {
	NULL, "oosiop", DV_DULL
};

const struct scsi_adapter oosiop_switch = {
	oosiop_scsicmd, NULL, NULL, NULL, NULL
};

void *
oosiop_cb_alloc(void *xsc)
{
	struct oosiop_softc *sc = xsc;
	struct oosiop_cb *cb;

	mtx_enter(&sc->sc_cb_mtx);
	cb = TAILQ_FIRST(&sc->sc_free_cb);
	if (cb)
		TAILQ_REMOVE(&sc->sc_free_cb, cb, chain);
	mtx_leave(&sc->sc_cb_mtx);

	return (cb);
}

void
oosiop_cb_free(void *xsc, void *xcb)
{
	struct oosiop_softc *sc = xsc;
	struct oosiop_cb *cb = xcb;

	mtx_enter(&sc->sc_cb_mtx);
	TAILQ_INSERT_TAIL(&sc->sc_free_cb, cb, chain);
	mtx_leave(&sc->sc_cb_mtx);
}

void
oosiop_attach(struct oosiop_softc *sc)
{
	struct scsibus_attach_args saa;
	bus_size_t scrsize;
	bus_dma_segment_t seg;
	struct oosiop_cb *cb;
	int err, i, nseg;

	/*
	 * Allocate DMA-safe memory for the script and map it.
	 */
	scrsize = round_page(sizeof(oosiop_script));
	err = bus_dmamem_alloc(sc->sc_dmat, scrsize, PAGE_SIZE, 0, &seg, 1,
	    &nseg, BUS_DMA_NOWAIT | BUS_DMA_ZERO);
	if (err) {
		printf(": failed to allocate script memory, err=%d\n", err);
		return;
	}
	err = bus_dmamem_map(sc->sc_dmat, &seg, nseg, scrsize,
	    (caddr_t *)&sc->sc_scr, BUS_DMA_NOWAIT | BUS_DMA_COHERENT);
	if (err) {
		printf(": failed to map script memory, err=%d\n", err);
		return;
	}
	err = bus_dmamap_create(sc->sc_dmat, scrsize, 1, scrsize, 0,
	    BUS_DMA_NOWAIT, &sc->sc_scrdma);
	if (err) {
		printf(": failed to create script map, err=%d\n", err);
		return;
	}
	err = bus_dmamap_load_raw(sc->sc_dmat, sc->sc_scrdma,
	    &seg, nseg, scrsize, BUS_DMA_NOWAIT | BUS_DMA_WRITE);
	if (err) {
		printf(": failed to load script map, err=%d\n", err);
		return;
	}
	sc->sc_scrbase = sc->sc_scrdma->dm_segs[0].ds_addr;

	/* Initialize command block array */
	TAILQ_INIT(&sc->sc_free_cb);
	TAILQ_INIT(&sc->sc_cbq);
	if (oosiop_alloc_cb(sc, OOSIOP_NCB) != 0)
		return;

	/* Use first cb to reselection msgin buffer */
	cb = TAILQ_FIRST(&sc->sc_free_cb);
	sc->sc_reselbuf = cb->xferdma->dm_segs[0].ds_addr +
	    offsetof(struct oosiop_xfer, msgin[0]);

	for (i = 0; i < OOSIOP_NTGT; i++) {
		sc->sc_tgt[i].nexus = NULL;
		sc->sc_tgt[i].flags = 0;
	}

	/* Setup asynchronous clock divisor parameters */
	if (sc->sc_freq <= 25000000) {
		sc->sc_ccf = 10;
		sc->sc_dcntl = OOSIOP_DCNTL_CF_1;
	} else if (sc->sc_freq <= 37500000) {
		sc->sc_ccf = 15;
		sc->sc_dcntl = OOSIOP_DCNTL_CF_1_5;
	} else if (sc->sc_freq <= 50000000) {
		sc->sc_ccf = 20;
		sc->sc_dcntl = OOSIOP_DCNTL_CF_2;
	} else {
		sc->sc_ccf = 30;
		sc->sc_dcntl = OOSIOP_DCNTL_CF_3;
	}

	if (sc->sc_chip == OOSIOP_700)
		sc->sc_minperiod = oosiop_period(sc, 4, sc->sc_ccf);
	else
		sc->sc_minperiod = oosiop_period(sc, 4, 10);

	if (sc->sc_minperiod < 25)
		sc->sc_minperiod = 25;	/* limit to 10MB/s */

	mtx_init(&sc->sc_cb_mtx, IPL_BIO);
	scsi_iopool_init(&sc->sc_iopool, sc, oosiop_cb_alloc, oosiop_cb_free);

	printf(": NCR53C700%s rev %d, %dMHz\n",
	    sc->sc_chip == OOSIOP_700_66 ? "-66" : "",
	    oosiop_read_1(sc, OOSIOP_CTEST7) >> 4,
	    sc->sc_freq / 1000000);
	/*
	 * Reset all
	 */
	oosiop_reset(sc, TRUE);
	oosiop_reset_bus(sc);

	/*
	 * Start SCRIPTS processor
	 */
	oosiop_load_script(sc);
	sc->sc_active = 0;
	oosiop_write_4(sc, OOSIOP_DSP, sc->sc_scrbase + Ent_wait_reselect);

	saa.saa_adapter = &oosiop_switch;
	saa.saa_adapter_softc = sc;
	saa.saa_adapter_buswidth = OOSIOP_NTGT;
	saa.saa_adapter_target = sc->sc_id;
	saa.saa_luns = 8;
	saa.saa_openings = 1;	/* XXX */
	saa.saa_pool = &sc->sc_iopool;
	saa.saa_quirks = ADEV_NODOORLOCK;
	saa.saa_flags = 0;
	saa.saa_wwpn = saa.saa_wwnn = 0;

	config_found(&sc->sc_dev, &saa, scsiprint);
}

int
oosiop_alloc_cb(struct oosiop_softc *sc, int ncb)
{
	struct oosiop_cb *cb;
	struct oosiop_xfer *xfer;
	bus_size_t xfersize;
	bus_dma_segment_t seg;
	int i, s, err, nseg;

	/*
	 * Allocate oosiop_cb.
	 */
	cb = mallocarray(ncb, sizeof(*cb), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (cb == NULL) {
		printf(": failed to allocate cb memory\n");
		return (ENOMEM);
	}

	/*
	 * Allocate DMA-safe memory for the oosiop_xfer and map it.
	 */
	xfersize = sizeof(struct oosiop_xfer) * ncb;
	err = bus_dmamem_alloc(sc->sc_dmat, xfersize, PAGE_SIZE, 0, &seg, 1,
	    &nseg, BUS_DMA_NOWAIT);
	if (err) {
		printf(": failed to allocate xfer block memory, err=%d\n", err);
		return (err);
	}
	err = bus_dmamem_map(sc->sc_dmat, &seg, nseg, xfersize,
	    (caddr_t *)(void *)&xfer, BUS_DMA_NOWAIT | BUS_DMA_COHERENT);
	if (err) {
		printf(": failed to map xfer block memory, err=%d\n", err);
		return (err);
	}

	/* Initialize each command block */
	for (i = 0; i < ncb; i++) {
		err = bus_dmamap_create(sc->sc_dmat, PAGE_SIZE, 1, PAGE_SIZE,
		    0, BUS_DMA_NOWAIT, &cb->cmddma);
		if (err) {
			printf(": failed to create cmddma map, err=%d\n", err);
			return (err);
		}

		err = bus_dmamap_create(sc->sc_dmat, OOSIOP_MAX_XFER,
		    OOSIOP_NSG, OOSIOP_DBC_MAX, 0, BUS_DMA_NOWAIT,
		    &cb->datadma);
		if (err) {
			printf(": failed to create datadma map, err=%d\n", err);
			return (err);
		}

		err = bus_dmamap_create(sc->sc_dmat,
		    sizeof(struct oosiop_xfer), 1, sizeof(struct oosiop_xfer),
		    0, BUS_DMA_NOWAIT, &cb->xferdma);
		if (err) {
			printf(": failed to create xfer block map, err=%d\n",
			    err);
			return (err);
		}
		err = bus_dmamap_load(sc->sc_dmat, cb->xferdma, xfer,
		    sizeof(struct oosiop_xfer), NULL, BUS_DMA_NOWAIT);
		if (err) {
			printf(": failed to load xfer block, err=%d\n", err);
			return (err);
		}

		cb->xfer = xfer;

		s = splbio();
		TAILQ_INSERT_TAIL(&sc->sc_free_cb, cb, chain);
		splx(s);

		cb++;
		xfer++;
	}

	return (0);
}

static __inline void
oosiop_relocate_io(struct oosiop_softc *sc, bus_addr_t addr)
{
	u_int32_t dcmd;
	int32_t dsps;

	dcmd = letoh32(sc->sc_scr[addr / 4 + 0]);
	dsps = letoh32(sc->sc_scr[addr / 4 + 1]);

	/* convert relative to absolute */
	if (dcmd & 0x04000000) {
		dcmd &= ~0x04000000;
#if 0
		/*
		 * sign extension isn't needed here because
		 * ncr53cxxx.c generates 32 bit dsps.
		 */
		dsps <<= 8;
		dsps >>= 8;
#endif
		sc->sc_scr[addr / 4 + 0] = htole32(dcmd);
		dsps += addr + 8;
	}

	sc->sc_scr[addr / 4 + 1] = htole32(dsps + sc->sc_scrbase);
}

static __inline void
oosiop_relocate_tc(struct oosiop_softc *sc, bus_addr_t addr)
{
	u_int32_t dcmd;
	int32_t dsps;

	dcmd = letoh32(sc->sc_scr[addr / 4 + 0]);
	dsps = letoh32(sc->sc_scr[addr / 4 + 1]);

	/* convert relative to absolute */
	if (dcmd & 0x00800000) {
		dcmd &= ~0x00800000;
		sc->sc_scr[addr / 4] = htole32(dcmd);
#if 0
		/*
		 * sign extension isn't needed here because
		 * ncr53cxxx.c generates 32 bit dsps.
		 */
		dsps <<= 8;
		dsps >>= 8;
#endif
		dsps += addr + 8;
	}

	sc->sc_scr[addr / 4 + 1] = htole32(dsps + sc->sc_scrbase);
}

static __inline void
oosiop_fixup_select(struct oosiop_softc *sc, bus_addr_t addr, int id)
{
	u_int32_t dcmd;

	dcmd = letoh32(sc->sc_scr[addr / 4]);
	dcmd &= 0xff00ffff;
	dcmd |= 0x00010000 << id;
	sc->sc_scr[addr / 4] = htole32(dcmd);
}

static __inline void
oosiop_fixup_jump(struct oosiop_softc *sc, bus_addr_t addr, bus_addr_t dst)
{

	sc->sc_scr[addr / 4 + 1] = htole32(dst);
}

static __inline void
oosiop_fixup_move(struct oosiop_softc *sc, bus_addr_t addr, bus_size_t dbc,
    bus_addr_t dsps)
{
	u_int32_t dcmd;

	dcmd = letoh32(sc->sc_scr[addr / 4]);
	dcmd &= 0xff000000;
	dcmd |= dbc & 0x00ffffff;
	sc->sc_scr[addr / 4 + 0] = htole32(dcmd);
	sc->sc_scr[addr / 4 + 1] = htole32(dsps);
}

void
oosiop_load_script(struct oosiop_softc *sc)
{
	int i;

	/* load script */
	for (i = 0; i < sizeof(oosiop_script) / sizeof(oosiop_script[0]); i++)
		sc->sc_scr[i] = htole32(oosiop_script[i]);

	/* relocate script */
	for (i = 0; i < (sizeof(oosiop_script) / 8); i++) {
		switch (oosiop_script[i * 2] >> 27) {
		case 0x08:	/* select */
		case 0x0a:	/* wait reselect */
			oosiop_relocate_io(sc, i * 8);
			break;
		case 0x10:	/* jump */
		case 0x11:	/* call */
			oosiop_relocate_tc(sc, i * 8);
			break;
		}
	}

	oosiop_fixup_move(sc, Ent_p_resel_msgin_move, 1, sc->sc_reselbuf);
	OOSIOP_SCRIPT_SYNC(sc, BUS_DMASYNC_PREWRITE);
}

void
oosiop_setup_sgdma(struct oosiop_softc *sc, struct oosiop_cb *cb)
{
	struct oosiop_xfer *xfer = cb->xfer;
	struct scsi_xfer *xs = cb->xs;
	int i, n, off;

	OOSIOP_XFERSCR_SYNC(sc, cb,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	off = cb->curdp;

	if (xs->flags & (SCSI_DATA_IN | SCSI_DATA_OUT)) {
		/* Find start segment */
		for (i = 0; i < cb->datadma->dm_nsegs; i++) {
			if (off < cb->datadma->dm_segs[i].ds_len)
				break;
			off -= cb->datadma->dm_segs[i].ds_len;
		}

		/* build MOVE block */
		if (xs->flags & SCSI_DATA_IN) {
			n = 0;
			while (i < cb->datadma->dm_nsegs) {
				xfer->datain_scr[n * 2 + 0] =
				    htole32(0x09000000 |
				    (cb->datadma->dm_segs[i].ds_len - off));
				xfer->datain_scr[n * 2 + 1] =
				    htole32(cb->datadma->dm_segs[i].ds_addr +
				    off);
				n++;
				i++;
				off = 0;
			}
			xfer->datain_scr[n * 2 + 0] = htole32(0x80080000);
			xfer->datain_scr[n * 2 + 1] =
			    htole32(sc->sc_scrbase + Ent_phasedispatch);
		}
		if (xs->flags & SCSI_DATA_OUT) {
			n = 0;
			while (i < cb->datadma->dm_nsegs) {
				xfer->dataout_scr[n * 2 + 0] =
				    htole32(0x08000000 |
				    (cb->datadma->dm_segs[i].ds_len - off));
				xfer->dataout_scr[n * 2 + 1] =
				    htole32(cb->datadma->dm_segs[i].ds_addr +
				    off);
				n++;
				i++;
				off = 0;
			}
			xfer->dataout_scr[n * 2 + 0] = htole32(0x80080000);
			xfer->dataout_scr[n * 2 + 1] =
			    htole32(sc->sc_scrbase + Ent_phasedispatch);
		}
	}
	if ((xs->flags & SCSI_DATA_IN) == 0) {
		xfer->datain_scr[0] = htole32(0x98080000);
		xfer->datain_scr[1] = htole32(DATAIN_TRAP);
	}
	if ((xs->flags & SCSI_DATA_OUT) == 0) {
		xfer->dataout_scr[0] = htole32(0x98080000);
		xfer->dataout_scr[1] = htole32(DATAOUT_TRAP);
	}
	OOSIOP_XFERSCR_SYNC(sc, cb,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

/*
 * Setup DMA pointer into script.
 */
void
oosiop_setup_dma(struct oosiop_softc *sc)
{
	struct oosiop_cb *cb;
	bus_addr_t xferbase;

	cb = sc->sc_curcb;
	xferbase = cb->xferdma->dm_segs[0].ds_addr;

	OOSIOP_SCRIPT_SYNC(sc, BUS_DMASYNC_POSTWRITE);

	oosiop_fixup_select(sc, Ent_p_select, cb->id);
	oosiop_fixup_jump(sc, Ent_p_datain_jump, xferbase +
	    offsetof(struct oosiop_xfer, datain_scr[0]));
	oosiop_fixup_jump(sc, Ent_p_dataout_jump, xferbase +
	    offsetof(struct oosiop_xfer, dataout_scr[0]));
	oosiop_fixup_move(sc, Ent_p_msgin_move, 1, xferbase +
	    offsetof(struct oosiop_xfer, msgin[0]));
	oosiop_fixup_move(sc, Ent_p_extmsglen_move, 1, xferbase +
	    offsetof(struct oosiop_xfer, msgin[1]));
	oosiop_fixup_move(sc, Ent_p_msgout_move, cb->msgoutlen, xferbase +
	    offsetof(struct oosiop_xfer, msgout[0]));
	oosiop_fixup_move(sc, Ent_p_status_move, 1, xferbase +
	    offsetof(struct oosiop_xfer, status));
	oosiop_fixup_move(sc, Ent_p_cmdout_move, cb->cmdlen,
	    cb->cmddma->dm_segs[0].ds_addr);

	OOSIOP_SCRIPT_SYNC(sc, BUS_DMASYNC_PREWRITE);
}

void
oosiop_flush_fifo(struct oosiop_softc *sc)
{

	oosiop_write_1(sc, OOSIOP_DFIFO, oosiop_read_1(sc, OOSIOP_DFIFO) |
	    OOSIOP_DFIFO_FLF);
	while ((oosiop_read_1(sc, OOSIOP_CTEST1) & OOSIOP_CTEST1_FMT) !=
	    OOSIOP_CTEST1_FMT)
		;
	oosiop_write_1(sc, OOSIOP_DFIFO, oosiop_read_1(sc, OOSIOP_DFIFO) &
	    ~OOSIOP_DFIFO_FLF);
}

void
oosiop_clear_fifo(struct oosiop_softc *sc)
{

	oosiop_write_1(sc, OOSIOP_DFIFO, oosiop_read_1(sc, OOSIOP_DFIFO) |
	    OOSIOP_DFIFO_CLF);
	while ((oosiop_read_1(sc, OOSIOP_CTEST1) & OOSIOP_CTEST1_FMT) !=
	    OOSIOP_CTEST1_FMT)
		;
	oosiop_write_1(sc, OOSIOP_DFIFO, oosiop_read_1(sc, OOSIOP_DFIFO) &
	    ~OOSIOP_DFIFO_CLF);
}

void
oosiop_phasemismatch(struct oosiop_softc *sc)
{
	struct oosiop_cb *cb;
	u_int32_t dsp, dbc, n, i, len;
	u_int8_t dfifo, sstat1;

	cb = sc->sc_curcb;
	if (cb == NULL)
		return;

	dsp = oosiop_read_4(sc, OOSIOP_DSP);
	dbc = oosiop_read_4(sc, OOSIOP_DBC) & OOSIOP_DBC_MAX;
	len = 0;

	n = dsp - cb->xferdma->dm_segs[0].ds_addr - 8;
	if (n >= offsetof(struct oosiop_xfer, datain_scr[0]) &&
	    n < offsetof(struct oosiop_xfer, datain_scr[OOSIOP_NSG * 2])) {
		n -= offsetof(struct oosiop_xfer, datain_scr[0]);
		n >>= 3;
		OOSIOP_DINSCR_SYNC(sc, cb,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		for (i = 0; i <= n; i++)
			len += letoh32(cb->xfer->datain_scr[i * 2]) &
			    0x00ffffff;
		OOSIOP_DINSCR_SYNC(sc, cb,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
		/* All data in the chip are already flushed */
	} else if (n >= offsetof(struct oosiop_xfer, dataout_scr[0]) &&
	    n < offsetof(struct oosiop_xfer, dataout_scr[OOSIOP_NSG * 2])) {
		n -= offsetof(struct oosiop_xfer, dataout_scr[0]);
		n >>= 3;
		OOSIOP_DOUTSCR_SYNC(sc, cb,
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		for (i = 0; i <= n; i++)
			len += letoh32(cb->xfer->dataout_scr[i * 2]) &
			    0x00ffffff;
		OOSIOP_DOUTSCR_SYNC(sc, cb,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

		dfifo = oosiop_read_1(sc, OOSIOP_DFIFO);
		dbc += ((dfifo & OOSIOP_DFIFO_BO) - (dbc & OOSIOP_DFIFO_BO)) &
		    OOSIOP_DFIFO_BO;

		sstat1 = oosiop_read_1(sc, OOSIOP_SSTAT1);
		if (sstat1 & OOSIOP_SSTAT1_OLF)
			dbc++;
		if ((sc->sc_tgt[cb->id].sxfer != 0) &&
		    (sstat1 & OOSIOP_SSTAT1_ORF) != 0)
			dbc++;

		oosiop_clear_fifo(sc);
	} else {
		printf("%s: phase mismatch addr=%08x\n", sc->sc_dev.dv_xname,
		    oosiop_read_4(sc, OOSIOP_DSP) - 8);
		oosiop_clear_fifo(sc);
		return;
	}

	len -= dbc;
	if (len) {
		cb->curdp += len;
		oosiop_setup_sgdma(sc, cb);
	}
}

void
oosiop_setup_syncxfer(struct oosiop_softc *sc)
{
	int id;

	id = sc->sc_curcb->id;
	if (sc->sc_chip != OOSIOP_700)
		oosiop_write_1(sc, OOSIOP_SBCL, sc->sc_tgt[id].scf);

	oosiop_write_1(sc, OOSIOP_SXFER, sc->sc_tgt[id].sxfer);
}

void
oosiop_set_syncparam(struct oosiop_softc *sc, int id, int period, int offset)
{
	int i, p;

	printf("%s: target %d now using 8 bit ", sc->sc_dev.dv_xname, id);

	if (offset == 0) {
		/* Asynchronous */
		sc->sc_tgt[id].scf = 0;
		sc->sc_tgt[id].sxfer = 0;
		printf("asynchronous");
	} else {
		/* Synchronous */
		if (sc->sc_chip == OOSIOP_700) {
			for (i = 4; i < 12; i++) {
				p = oosiop_period(sc, i, sc->sc_ccf);
				if (p >= period)
					break;
			}
			if (i == 12) {
				printf("%s: target %d period too large\n",
				    sc->sc_dev.dv_xname, id);
				i = 11;	/* XXX */
			}
			sc->sc_tgt[id].scf = 0;
			sc->sc_tgt[id].sxfer = ((i - 4) << 4) | offset;
		} else {
			for (i = 0; i < NSYNCTBL; i++) {
				p = oosiop_period(sc, synctbl[i].tp + 4,
				    (synctbl[i].scf + 1) * 5);
				if (p >= period)
					break;
			}
			if (i == NSYNCTBL) {
				printf("%s: target %d period too large\n",
				    sc->sc_dev.dv_xname, id);
				i = NSYNCTBL - 1;	/* XXX */
			}
			sc->sc_tgt[id].scf = synctbl[i].scf;
			sc->sc_tgt[id].sxfer = (synctbl[i].tp << 4) | offset;
		}
		/* XXX print actual ns period... */
		printf("synchronous");
	}
	printf(" xfers\n");
}

void
oosiop_scsicmd(struct scsi_xfer *xs)
{
	struct oosiop_softc *sc;
	struct oosiop_cb *cb;
	struct oosiop_xfer *xfer;
	int s, err;
	int dopoll;

	sc = xs->sc_link->bus->sb_adapter_softc;

	cb = xs->io;

	cb->xs = xs;
	cb->xsflags = xs->flags;
	cb->cmdlen = xs->cmdlen;
	cb->datalen = 0;
	cb->flags = 0;
	cb->id = xs->sc_link->target;
	cb->lun = xs->sc_link->lun;
	xfer = cb->xfer;

	/* Setup SCSI command buffer DMA */
	err = bus_dmamap_load(sc->sc_dmat, cb->cmddma, &xs->cmd,
	    xs->cmdlen, NULL, ((xs->flags & SCSI_NOSLEEP) ?
	    BUS_DMA_NOWAIT : BUS_DMA_WAITOK) |
	    BUS_DMA_STREAMING | BUS_DMA_WRITE);
	if (err) {
		printf("%s: unable to load cmd DMA map: %d",
		    sc->sc_dev.dv_xname, err);
		xs->error = XS_DRIVER_STUFFUP;
		scsi_done(xs);
		return;
	}
	bus_dmamap_sync(sc->sc_dmat, cb->cmddma, 0, xs->cmdlen,
	    BUS_DMASYNC_PREWRITE);

	/* Setup data buffer DMA */
	if (xs->flags & (SCSI_DATA_IN | SCSI_DATA_OUT)) {
		cb->datalen = xs->datalen;
		err = bus_dmamap_load(sc->sc_dmat, cb->datadma,
		    xs->data, xs->datalen, NULL,
		    ((xs->flags & SCSI_NOSLEEP) ?
		    BUS_DMA_NOWAIT : BUS_DMA_WAITOK) |
		    BUS_DMA_STREAMING |
		    ((xs->flags & SCSI_DATA_IN) ? BUS_DMA_READ :
		    BUS_DMA_WRITE));
		if (err) {
			printf("%s: unable to load data DMA map: %d",
			    sc->sc_dev.dv_xname, err);
			bus_dmamap_unload(sc->sc_dmat, cb->cmddma);
			xs->error = XS_DRIVER_STUFFUP;
			scsi_done(xs);
			return;
		}
		bus_dmamap_sync(sc->sc_dmat, cb->datadma,
		    0, xs->datalen,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	}

	xfer->status = SCSI_OOSIOP_NOSTATUS;

	/*
	 * Always initialize timeout so it does not contain trash
	 * that could confuse timeout_del().
	 */
	timeout_set(&xs->stimeout, oosiop_timeout, cb);

	oosiop_setup(sc, cb);

	s = splbio();

	TAILQ_INSERT_TAIL(&sc->sc_cbq, cb, chain);

	if (xs->flags & SCSI_POLL)
		dopoll = 1;
	else {
		dopoll = 0;
		/* start expire timer */
		timeout_add_msec(&xs->stimeout, xs->timeout);
	}

	if (!sc->sc_active) {
		/* Abort script to start selection */
		oosiop_write_1(sc, OOSIOP_ISTAT, OOSIOP_ISTAT_ABRT);
	}

	splx(s);

	if (dopoll)
		oosiop_poll(sc, cb);
}

void
oosiop_poll(struct oosiop_softc *sc, struct oosiop_cb *cb)
{
	struct scsi_xfer *xs = cb->xs;
	int i, s, to;
	u_int8_t istat;

	s = splbio();
	to = xs->timeout / 1000;
	for (;;) {
		i = 1000;
		while (((istat = oosiop_read_1(sc, OOSIOP_ISTAT)) &
		    (OOSIOP_ISTAT_SIP | OOSIOP_ISTAT_DIP)) == 0) {
			if (i <= 0) {
				i = 1000;
				to--;
				if (to <= 0) {
					oosiop_reset(sc, TRUE);
					splx(s);
					return;
				}
			}
			delay(1000);
			i--;
		}
		oosiop_processintr(sc, istat);

		if (xs->flags & ITSDONE)
			break;
	}

	splx(s);
}

void
oosiop_setup(struct oosiop_softc *sc, struct oosiop_cb *cb)
{
	struct oosiop_xfer *xfer = cb->xfer;

	cb->curdp = 0;
	cb->savedp = 0;

	oosiop_setup_sgdma(sc, cb);

	/* Setup msgout buffer */
	OOSIOP_XFERMSG_SYNC(sc, cb,
	   BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	xfer->msgout[0] = MSG_IDENTIFY(cb->lun,
	    (cb->xs->cmd.opcode != REQUEST_SENSE));
	cb->msgoutlen = 1;

	if (sc->sc_tgt[cb->id].flags & TGTF_SYNCNEG) {
		sc->sc_tgt[cb->id].flags &= ~TGTF_SYNCNEG;
		/* Send SDTR */
		xfer->msgout[1] = MSG_EXTENDED;
		xfer->msgout[2] = MSG_EXT_SDTR_LEN;
		xfer->msgout[3] = MSG_EXT_SDTR;
		xfer->msgout[4] = sc->sc_minperiod;
		xfer->msgout[5] = OOSIOP_MAX_OFFSET;
		cb->msgoutlen = 6;
		sc->sc_tgt[cb->id].flags |= TGTF_WAITSDTR;
	}

	OOSIOP_XFERMSG_SYNC(sc, cb,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

void
oosiop_done(struct oosiop_softc *sc, struct oosiop_cb *cb)
{
	struct scsi_xfer *xs;
	struct scsi_link *periph;
	int autosense;

	xs = cb->xs;
	periph = xs->sc_link;

	/*
	 * Record if this is the completion of an auto sense
	 * scsi command, and then reset the flag so we don't loop
	 * when such a command fails or times out.
	 */
	autosense = cb->flags & CBF_AUTOSENSE;
	cb->flags &= ~CBF_AUTOSENSE;

	bus_dmamap_sync(sc->sc_dmat, cb->cmddma, 0, cb->cmdlen,
	    BUS_DMASYNC_POSTWRITE);
	bus_dmamap_unload(sc->sc_dmat, cb->cmddma);

	if (cb->xsflags & (SCSI_DATA_IN | SCSI_DATA_OUT)) {
		bus_dmamap_sync(sc->sc_dmat, cb->datadma, 0, cb->datalen,
		    (cb->xsflags & SCSI_DATA_IN) ?
		    BUS_DMASYNC_POSTREAD : BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, cb->datadma);
	}

	timeout_del(&xs->stimeout);

	xs->status = cb->xfer->status;

	if (cb->flags & CBF_SELTOUT)
		xs->error = XS_SELTIMEOUT;
	else if (cb->flags & CBF_TIMEOUT)
		xs->error = XS_TIMEOUT;
	else switch (xs->status) {
	case SCSI_OK:
		if (autosense == 0)
			xs->error = XS_NOERROR;
		else
			xs->error = XS_SENSE;
		break;

	case SCSI_BUSY:
		xs->error = XS_BUSY;
		break;
	case SCSI_CHECK:
#ifdef notyet
		if (autosense == 0)
			cb->flags |= CBF_AUTOSENSE;
		else
#endif
			xs->error = XS_DRIVER_STUFFUP;
		break;
	case SCSI_OOSIOP_NOSTATUS:
		/* the status byte was not updated, cmd was aborted. */
		xs->error = XS_SELTIMEOUT;
		break;

	default:
		xs->error = XS_RESET;
		break;
	}

	if ((cb->flags & CBF_AUTOSENSE) == 0) {
		/* Put it on the free list. */
FREE:
		xs->resid = 0;
		scsi_done(xs);

		if (cb == sc->sc_curcb)
			sc->sc_curcb = NULL;
		if (cb == sc->sc_lastcb)
			sc->sc_lastcb = NULL;
		sc->sc_tgt[cb->id].nexus = NULL;
	} else {
		/* Set up REQUEST_SENSE command */
		struct scsi_sense *cmd = (struct scsi_sense *)&xs->cmd;
		int err;

		bzero(cmd, sizeof(*cmd));
		cmd->opcode = REQUEST_SENSE;
		cmd->byte2 = xs->sc_link->lun << 5;
		cb->cmdlen = cmd->length = sizeof(xs->sense);

		cb->xsflags &= SCSI_POLL | SCSI_NOSLEEP;
		cb->xsflags |= SCSI_DATA_IN;
		cb->datalen = sizeof xs->sense;

		/* Setup SCSI command buffer DMA */
		err = bus_dmamap_load(sc->sc_dmat, cb->cmddma, cmd,
		    cb->cmdlen, NULL,
		    BUS_DMA_NOWAIT | BUS_DMA_STREAMING | BUS_DMA_WRITE);
		if (err) {
			printf("%s: unable to load REQUEST_SENSE cmd DMA map: %d",
			    sc->sc_dev.dv_xname, err);
			xs->error = XS_DRIVER_STUFFUP;
			goto FREE;
		}
		bus_dmamap_sync(sc->sc_dmat, cb->cmddma, 0, cb->cmdlen,
		    BUS_DMASYNC_PREWRITE);

		/* Setup data buffer DMA */
		err = bus_dmamap_load(sc->sc_dmat, cb->datadma,
		    &xs->sense, sizeof(xs->sense), NULL,
		    BUS_DMA_NOWAIT | BUS_DMA_STREAMING | BUS_DMA_READ);
		if (err) {
			printf("%s: unable to load REQUEST_SENSE data DMA map: %d",
			    sc->sc_dev.dv_xname, err);
			xs->error = XS_DRIVER_STUFFUP;
			bus_dmamap_unload(sc->sc_dmat, cb->cmddma);
			goto FREE;
		}
		bus_dmamap_sync(sc->sc_dmat, cb->datadma,
		    0, sizeof(xs->sense), BUS_DMASYNC_PREREAD);

		oosiop_setup(sc, cb);

		TAILQ_INSERT_HEAD(&sc->sc_cbq, cb, chain);
		if ((cb->xs->flags & SCSI_POLL) == 0) {
			/* start expire timer */
			timeout_add_msec(&xs->stimeout, xs->timeout);
		}
	}
}

void
oosiop_timeout(void *arg)
{
	struct oosiop_cb *cb = arg;
	struct scsi_xfer *xs = cb->xs;
	struct oosiop_softc *sc = xs->sc_link->bus->sb_adapter_softc;
	int s;

	sc_print_addr(xs->sc_link);
	printf("command 0x%02x timeout on xs %p\n", xs->cmd.opcode, xs);

	s = splbio();

	oosiop_reset_bus(sc);

	cb->flags |= CBF_TIMEOUT;
	oosiop_done(sc, cb);

	splx(s);
}

void
oosiop_reset(struct oosiop_softc *sc, int allflags)
{
	int i, s;

	s = splbio();

	/* Stop SCRIPTS processor */
	oosiop_write_1(sc, OOSIOP_ISTAT, OOSIOP_ISTAT_ABRT);
	delay(100);
	oosiop_write_1(sc, OOSIOP_ISTAT, 0);

	/* Reset the chip */
	oosiop_write_1(sc, OOSIOP_DCNTL, sc->sc_dcntl | OOSIOP_DCNTL_RST);
	delay(100);
	oosiop_write_1(sc, OOSIOP_DCNTL, sc->sc_dcntl);
	delay(10000);

	/* Set up various chip parameters */
	oosiop_write_1(sc, OOSIOP_SCNTL0, OOSIOP_ARB_FULL | sc->sc_scntl0);
	oosiop_write_1(sc, OOSIOP_SCNTL1, OOSIOP_SCNTL1_ESR);
	oosiop_write_1(sc, OOSIOP_DCNTL, sc->sc_dcntl);
	oosiop_write_1(sc, OOSIOP_DMODE, sc->sc_dmode);
	oosiop_write_1(sc, OOSIOP_SCID, OOSIOP_SCID_VALUE(sc->sc_id));
	oosiop_write_1(sc, OOSIOP_DWT, sc->sc_dwt);
	oosiop_write_1(sc, OOSIOP_CTEST7, sc->sc_ctest7);
	oosiop_write_1(sc, OOSIOP_SXFER, 0);

	/* Clear all interrupts */
	(void)oosiop_read_1(sc, OOSIOP_SSTAT0);
	(void)oosiop_read_1(sc, OOSIOP_SSTAT1);
	(void)oosiop_read_1(sc, OOSIOP_DSTAT);

	/* Enable interrupts */
	oosiop_write_1(sc, OOSIOP_SIEN,
	    OOSIOP_SIEN_M_A | OOSIOP_SIEN_STO | OOSIOP_SIEN_SGE |
	    OOSIOP_SIEN_UDC | OOSIOP_SIEN_RST | OOSIOP_SIEN_PAR);
	oosiop_write_1(sc, OOSIOP_DIEN,
	    OOSIOP_DIEN_ABRT | OOSIOP_DIEN_SSI | OOSIOP_DIEN_SIR |
	    OOSIOP_DIEN_WTD | OOSIOP_DIEN_IID);

	/* Set target state to asynchronous */
	for (i = 0; i < OOSIOP_NTGT; i++) {
		if (allflags)
			sc->sc_tgt[i].flags = 0;
		else
			sc->sc_tgt[i].flags |= TGTF_SYNCNEG;
		sc->sc_tgt[i].scf = 0;
		sc->sc_tgt[i].sxfer = 0;
	}

	splx(s);
}

void
oosiop_reset_bus(struct oosiop_softc *sc)
{
	int s, i;

	s = splbio();

	/* Assert SCSI RST */
	oosiop_write_1(sc, OOSIOP_SCNTL1, OOSIOP_SCNTL1_RST);
	delay(25);	/* Reset hold time (25us) */
	oosiop_write_1(sc, OOSIOP_SCNTL1, 0);

	/* Remove all nexuses */
	for (i = 0; i < OOSIOP_NTGT; i++) {
		if (sc->sc_tgt[i].nexus) {
			sc->sc_tgt[i].nexus->xfer->status =
			    SCSI_OOSIOP_NOSTATUS; /* XXX */
			oosiop_done(sc, sc->sc_tgt[i].nexus);
		}
	}

	sc->sc_curcb = NULL;

	delay(250000);	/* Reset to selection (250ms) */

	splx(s);
}

/*
 * interrupt handler
 */
int
oosiop_intr(struct oosiop_softc *sc)
{
	u_int8_t istat;

	istat = oosiop_read_1(sc, OOSIOP_ISTAT);

	if ((istat & (OOSIOP_ISTAT_SIP | OOSIOP_ISTAT_DIP)) == 0)
		return (0);

	oosiop_processintr(sc, istat);
	return (1);
}

void
oosiop_processintr(struct oosiop_softc *sc, u_int8_t istat)
{
	struct oosiop_cb *cb;
	u_int32_t dcmd;
	u_int8_t dstat, sstat0;

	sc->sc_nextdsp = Ent_wait_reselect;

	/* DMA interrupts */
	if (istat & OOSIOP_ISTAT_DIP) {
		oosiop_write_1(sc, OOSIOP_ISTAT, 0);

		dstat = oosiop_read_1(sc, OOSIOP_DSTAT);

		if (dstat & OOSIOP_DSTAT_ABRT) {
			sc->sc_nextdsp = oosiop_read_4(sc, OOSIOP_DSP) -
			    sc->sc_scrbase - 8;

			if (sc->sc_nextdsp == Ent_p_resel_msgin_move &&
			    (oosiop_read_1(sc, OOSIOP_SBCL) & OOSIOP_ACK)) {
				if ((dstat & OOSIOP_DSTAT_DFE) == 0)
					oosiop_flush_fifo(sc);
				sc->sc_nextdsp += 8;
			}
		}

		if (dstat & OOSIOP_DSTAT_SSI) {
			sc->sc_nextdsp = oosiop_read_4(sc, OOSIOP_DSP) -
			    sc->sc_scrbase;
			printf("%s: single step %08x\n", sc->sc_dev.dv_xname,
			    sc->sc_nextdsp);
		}

		if (dstat & OOSIOP_DSTAT_SIR) {
			if ((dstat & OOSIOP_DSTAT_DFE) == 0)
				oosiop_flush_fifo(sc);
			oosiop_scriptintr(sc);
		}

		if (dstat & OOSIOP_DSTAT_WTD) {
			printf("%s: DMA time out\n", sc->sc_dev.dv_xname);
			oosiop_reset(sc, TRUE);
		}

		if (dstat & OOSIOP_DSTAT_IID) {
			dcmd = oosiop_read_4(sc, OOSIOP_DBC);
			if ((dcmd & 0xf8000000) == 0x48000000) {
				printf("%s: REQ asserted on WAIT DISCONNECT\n",
				    sc->sc_dev.dv_xname);
				sc->sc_nextdsp = Ent_phasedispatch; /* XXX */
			} else {
				printf("%s: invalid SCRIPTS instruction "
				    "addr=%08x dcmd=%08x dsps=%08x\n",
				    sc->sc_dev.dv_xname,
				    oosiop_read_4(sc, OOSIOP_DSP) - 8, dcmd,
				    oosiop_read_4(sc, OOSIOP_DSPS));
				oosiop_reset(sc, TRUE);
				OOSIOP_SCRIPT_SYNC(sc, BUS_DMASYNC_POSTWRITE);
				oosiop_load_script(sc);
			}
		}

		if ((dstat & OOSIOP_DSTAT_DFE) == 0)
			oosiop_clear_fifo(sc);
	}

	/* SCSI interrupts */
	if (istat & OOSIOP_ISTAT_SIP) {
		if (istat & OOSIOP_ISTAT_DIP)
			delay(1);
		sstat0 = oosiop_read_1(sc, OOSIOP_SSTAT0);

		if (sstat0 & OOSIOP_SSTAT0_M_A) {
			/* SCSI phase mismatch during MOVE operation */
			oosiop_phasemismatch(sc);
			sc->sc_nextdsp = Ent_phasedispatch;
		}

		if (sstat0 & OOSIOP_SSTAT0_STO) {
			if (sc->sc_curcb) {
				sc->sc_curcb->flags |= CBF_SELTOUT;
				oosiop_done(sc, sc->sc_curcb);
			}
		}

		if (sstat0 & OOSIOP_SSTAT0_SGE) {
			printf("%s: SCSI gross error\n", sc->sc_dev.dv_xname);
			oosiop_reset(sc, TRUE);
		}

		if (sstat0 & OOSIOP_SSTAT0_UDC) {
			/* XXX */
			if (sc->sc_curcb) {
				printf("%s: unexpected disconnect\n",
				    sc->sc_dev.dv_xname);
				oosiop_done(sc, sc->sc_curcb);
			}
		}

		if (sstat0 & OOSIOP_SSTAT0_RST) {
			/*
			 * This may happen during sync request negotiation;
			 * be sure not to reset TGTF_WAITSDTR in that case.
			 */
			oosiop_reset(sc, FALSE);
		}

		if (sstat0 & OOSIOP_SSTAT0_PAR)
			printf("%s: parity error\n", sc->sc_dev.dv_xname);
	}

	/* Start next command if available */
	if (sc->sc_nextdsp == Ent_wait_reselect && TAILQ_FIRST(&sc->sc_cbq)) {
		cb = sc->sc_curcb = TAILQ_FIRST(&sc->sc_cbq);
		TAILQ_REMOVE(&sc->sc_cbq, cb, chain);
		sc->sc_tgt[cb->id].nexus = cb;

		oosiop_setup_dma(sc);
		oosiop_setup_syncxfer(sc);
		sc->sc_lastcb = cb;
		sc->sc_nextdsp = Ent_start_select;

		/* Schedule timeout */
		if ((cb->xs->flags & SCSI_POLL) == 0) {
			/* start expire timer */
			timeout_add_msec(&cb->xs->stimeout, cb->xs->timeout);
		}
	}

	sc->sc_active = (sc->sc_nextdsp != Ent_wait_reselect);

	/* Restart script */
	oosiop_write_4(sc, OOSIOP_DSP, sc->sc_nextdsp + sc->sc_scrbase);
}

void
oosiop_scriptintr(struct oosiop_softc *sc)
{
	struct oosiop_cb *cb;
	u_int32_t icode;
	u_int32_t dsp;
	int i;
	u_int8_t sfbr, resid, resmsg;

	cb = sc->sc_curcb;
	icode = oosiop_read_4(sc, OOSIOP_DSPS);

	switch (icode) {
	case A_int_done:
		if (cb)
			oosiop_done(sc, cb);
		break;

	case A_int_msgin:
		if (cb)
			oosiop_msgin(sc, cb);
		break;

	case A_int_extmsg:
		/* extended message in DMA setup request */
		sfbr = oosiop_read_1(sc, OOSIOP_SFBR);
		OOSIOP_SCRIPT_SYNC(sc, BUS_DMASYNC_POSTWRITE);
		oosiop_fixup_move(sc, Ent_p_extmsgin_move, sfbr,
		    cb->xferdma->dm_segs[0].ds_addr +
		    offsetof(struct oosiop_xfer, msgin[2]));
		OOSIOP_SCRIPT_SYNC(sc, BUS_DMASYNC_PREWRITE);
		sc->sc_nextdsp = Ent_rcv_extmsg;
		break;

	case A_int_resel:
		/* reselected */
		resid = oosiop_read_1(sc, OOSIOP_SFBR);
		for (i = 0; i < OOSIOP_NTGT; i++)
			if (resid & (1 << i))
				break;
		if (i == OOSIOP_NTGT) {
			printf("%s: missing reselection target id\n",
			    sc->sc_dev.dv_xname);
			break;
		}
		sc->sc_resid = i;
		sc->sc_nextdsp = Ent_wait_resel_identify;

		if (cb) {
			/* Current command was lost arbitration */
			sc->sc_tgt[cb->id].nexus = NULL;
			TAILQ_INSERT_HEAD(&sc->sc_cbq, cb, chain);
			sc->sc_curcb = NULL;
		}

		break;

	case A_int_res_id:
		cb = sc->sc_tgt[sc->sc_resid].nexus;
		resmsg = oosiop_read_1(sc, OOSIOP_SFBR);
		if (MSG_ISIDENTIFY(resmsg) && cb &&
		    (resmsg & MSG_IDENTIFY_LUNMASK) == cb->lun) {
			sc->sc_curcb = cb;
			if (cb != sc->sc_lastcb) {
				oosiop_setup_dma(sc);
				oosiop_setup_syncxfer(sc);
				sc->sc_lastcb = cb;
			}
			if (cb->curdp != cb->savedp) {
				cb->curdp = cb->savedp;
				oosiop_setup_sgdma(sc, cb);
			}
			sc->sc_nextdsp = Ent_ack_msgin;
		} else {
			/* Reselection from invalid target */
			oosiop_reset_bus(sc);
		}
		break;

	case A_int_resfail:
		/* reselect failed */
		break;

	case A_int_disc:
		/* disconnected */
		sc->sc_curcb = NULL;
		break;

	case A_int_err:
		/* generic error */
		dsp = oosiop_read_4(sc, OOSIOP_DSP);
		printf("%s: script error at 0x%08x\n", sc->sc_dev.dv_xname,
		    dsp - 8);
		sc->sc_curcb = NULL;
		break;

	case DATAIN_TRAP:
		printf("%s: unexpected datain\n", sc->sc_dev.dv_xname);
		/* XXX: need to reset? */
		break;

	case DATAOUT_TRAP:
		printf("%s: unexpected dataout\n", sc->sc_dev.dv_xname);
		/* XXX: need to reset? */
		break;

	default:
		printf("%s: unknown intr code %08x\n", sc->sc_dev.dv_xname,
		    icode);
		break;
	}
}

void
oosiop_msgin(struct oosiop_softc *sc, struct oosiop_cb *cb)
{
	struct oosiop_xfer *xfer;
	int msgout;

	xfer = cb->xfer;
	sc->sc_nextdsp = Ent_ack_msgin;
	msgout = 0;

	OOSIOP_XFERMSG_SYNC(sc, cb,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	switch (xfer->msgin[0]) {
	case MSG_EXTENDED:
		switch (xfer->msgin[2]) {
		case MSG_EXT_SDTR:
			if (sc->sc_tgt[cb->id].flags & TGTF_WAITSDTR) {
				/* Host initiated SDTR */
				sc->sc_tgt[cb->id].flags &= ~TGTF_WAITSDTR;
			} else {
				/* Target initiated SDTR */
				if (xfer->msgin[3] < sc->sc_minperiod)
					xfer->msgin[3] = sc->sc_minperiod;
				if (xfer->msgin[4] > OOSIOP_MAX_OFFSET)
					xfer->msgin[4] = OOSIOP_MAX_OFFSET;
				xfer->msgout[0] = MSG_EXTENDED;
				xfer->msgout[1] = MSG_EXT_SDTR_LEN;
				xfer->msgout[2] = MSG_EXT_SDTR;
				xfer->msgout[3] = xfer->msgin[3];
				xfer->msgout[4] = xfer->msgin[4];
				cb->msgoutlen = 5;
				msgout = 1;
			}
			oosiop_set_syncparam(sc, cb->id, (int)xfer->msgin[3],
			    (int)xfer->msgin[4]);
			oosiop_setup_syncxfer(sc);
			break;

		default:
			/* Reject message */
			xfer->msgout[0] = MSG_MESSAGE_REJECT;
			cb->msgoutlen = 1;
			msgout = 1;
			break;
		}
		break;

	case MSG_SAVEDATAPOINTER:
		cb->savedp = cb->curdp;
		break;

	case MSG_RESTOREPOINTERS:
		if (cb->curdp != cb->savedp) {
			cb->curdp = cb->savedp;
			oosiop_setup_sgdma(sc, cb);
		}
		break;

	case MSG_MESSAGE_REJECT:
		if (sc->sc_tgt[cb->id].flags & TGTF_WAITSDTR) {
			/* SDTR rejected */
			sc->sc_tgt[cb->id].flags &= ~TGTF_WAITSDTR;
			oosiop_set_syncparam(sc, cb->id, 0, 0);
			oosiop_setup_syncxfer(sc);
		}
		break;

	default:
		/* Reject message */
		xfer->msgout[0] = MSG_MESSAGE_REJECT;
		cb->msgoutlen = 1;
		msgout = 1;
	}

	OOSIOP_XFERMSG_SYNC(sc, cb,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	if (msgout) {
		OOSIOP_SCRIPT_SYNC(sc, BUS_DMASYNC_POSTWRITE);
		oosiop_fixup_move(sc, Ent_p_msgout_move, cb->msgoutlen,
		    cb->xferdma->dm_segs[0].ds_addr +
		    offsetof(struct oosiop_xfer, msgout[0]));
		OOSIOP_SCRIPT_SYNC(sc, BUS_DMASYNC_PREWRITE);
		sc->sc_nextdsp = Ent_sendmsg;
	}
}
