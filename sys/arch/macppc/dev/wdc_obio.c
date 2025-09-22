/*	$OpenBSD: wdc_obio.c,v 1.31 2022/03/13 12:33:01 mpi Exp $	*/
/*	$NetBSD: wdc_obio.c,v 1.15 2001/07/25 20:26:33 bouyer Exp $	*/

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum and by Onno van der Linden.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <uvm/uvm_extern.h>

#include <machine/bus.h>
#include <machine/autoconf.h>

#include <dev/ofw/openfirm.h>
#include <dev/ata/atavar.h>
#include <dev/ata/atareg.h>
#include <dev/ic/wdcvar.h>

#include <macppc/dev/dbdma.h>

#define WDC_REG_NPORTS		8
#define WDC_AUXREG_OFFSET	0x16
#define WDC_DEFAULT_PIO_IRQ	13	/* XXX */
#define WDC_DEFAULT_DMA_IRQ	2	/* XXX */

#define WDC_OPTIONS_DMA 0x01

#define	WDC_DMALIST_MAX	32

struct wdc_obio_softc {
	struct wdc_softc sc_wdcdev;
	struct channel_softc *wdc_chanptr;
	struct channel_softc wdc_channel;

	bus_dma_tag_t sc_dmat;
	bus_dmamap_t sc_dmamap;
	dbdma_regmap_t *sc_dmareg;
	dbdma_command_t	*sc_dmacmd;
	dbdma_t sc_dbdma;

	void *sc_ih;
	int sc_use_dma;
	bus_size_t sc_cmdsize;
	size_t sc_dmasize;
};

u_int8_t wdc_obio_read_reg(struct channel_softc *, enum wdc_regs);
void wdc_obio_write_reg(struct channel_softc *, enum wdc_regs, u_int8_t);

struct channel_softc_vtbl wdc_obio_vtbl = {
	wdc_obio_read_reg,
	wdc_obio_write_reg,
	wdc_default_lba48_write_reg,
	wdc_default_read_raw_multi_2,
	wdc_default_write_raw_multi_2,
	wdc_default_read_raw_multi_4,
	wdc_default_write_raw_multi_4
};

int	wdc_obio_probe(struct device *, void *, void *);
void	wdc_obio_attach(struct device *, struct device *, void *);
int	wdc_obio_detach(struct device *, int);

const struct cfattach wdc_obio_ca = {
	sizeof(struct wdc_obio_softc), wdc_obio_probe, wdc_obio_attach,
	wdc_obio_detach
};

int	wdc_obio_dma_init(void *, int, int, void *, size_t, int);
void	wdc_obio_dma_start(void *, int, int);
int	wdc_obio_dma_finish(void *, int, int, int);
void	wdc_obio_adjust_timing(struct channel_softc *);
void	wdc_obio_ata4_adjust_timing(struct channel_softc *);
void	wdc_obio_ata6_adjust_timing(struct channel_softc *);

int
wdc_obio_probe(struct device *parent, void *match, void *aux)
{
	struct confargs *ca = aux;
	char compat[32];

	if (ca->ca_nreg < 8)
		return 0;

	/* XXX should not use name */
	if (strcmp(ca->ca_name, "ATA") == 0 ||
	    strncmp(ca->ca_name, "ata", 3) == 0 ||
	    strcmp(ca->ca_name, "ide") == 0)
		return 1;

	bzero(compat, sizeof(compat));
	OF_getprop(ca->ca_node, "compatible", compat, sizeof(compat));
	if (strcmp(compat, "heathrow-ata") == 0 ||
	    strcmp(compat, "keylargo-ata") == 0)
		return 1;

	return 0;
}

void
wdc_obio_attach(struct device *parent, struct device *self, void *aux)
{
	struct wdc_obio_softc *sc = (void *)self;
	struct confargs *ca = aux;
	struct channel_softc *chp = &sc->wdc_channel;
	int intr, error;
	bus_addr_t cmdbase;

	sc->sc_use_dma = 0;
	if (ca->ca_nreg >= 16)
		sc->sc_use_dma = 1;	/* Enable dma */

	sc->sc_dmat = ca->ca_dmat;
	if ((error = bus_dmamap_create(sc->sc_dmat,
	    WDC_DMALIST_MAX * DBDMA_COUNT_MAX, WDC_DMALIST_MAX,
	    DBDMA_COUNT_MAX, NBPG, BUS_DMA_NOWAIT, &sc->sc_dmamap)) != 0) {
		printf(": cannot create dma map, error = %d\n", error);
		return;
	}

	if (ca->ca_nintr >= 4 && ca->ca_nreg >= 8) {
		intr = ca->ca_intr[0];
		printf(" irq %d", intr);
	} else if (ca->ca_nintr == -1) {
		intr = WDC_DEFAULT_PIO_IRQ;
		printf(" irq property not found; using %d", intr);
	} else {
		printf(": couldn't get irq property\n");
		return;
	}

	if (sc->sc_use_dma)
		printf(": DMA");

	printf("\n");

	chp->cmd_iot = chp->ctl_iot = ca->ca_iot;
	chp->_vtbl = &wdc_obio_vtbl;

	cmdbase = ca->ca_reg[0];
	sc->sc_cmdsize = ca->ca_reg[1];

	if (bus_space_map(chp->cmd_iot, cmdbase, sc->sc_cmdsize, 0,
	    &chp->cmd_ioh) || bus_space_subregion(chp->cmd_iot, chp->cmd_ioh,
	    /* WDC_AUXREG_OFFSET<<4 */ 0x160, 1, &chp->ctl_ioh)) {
		printf("%s: couldn't map registers\n",
			sc->sc_wdcdev.sc_dev.dv_xname);
		return;
	}
	chp->data32iot = chp->cmd_iot;
	chp->data32ioh = chp->cmd_ioh;

	sc->sc_ih = mac_intr_establish(parent, intr, IST_LEVEL, IPL_BIO,
	    wdcintr, chp, sc->sc_wdcdev.sc_dev.dv_xname);

	sc->sc_wdcdev.set_modes = wdc_obio_adjust_timing;
	if (sc->sc_use_dma) {
		sc->sc_dbdma = dbdma_alloc(sc->sc_dmat, WDC_DMALIST_MAX + 1);
		sc->sc_dmacmd = sc->sc_dbdma->d_addr;

		sc->sc_dmareg = mapiodev(ca->ca_baseaddr + ca->ca_reg[2],
		    sc->sc_dmasize = ca->ca_reg[3]);

		sc->sc_wdcdev.cap |= WDC_CAPABILITY_DMA;
		sc->sc_wdcdev.DMA_cap = 2;
		if (strcmp(ca->ca_name, "ata-4") == 0) {
			sc->sc_wdcdev.cap |= WDC_CAPABILITY_UDMA |
			    WDC_CAPABILITY_MODE;
			sc->sc_wdcdev.UDMA_cap = 4;
			sc->sc_wdcdev.set_modes = wdc_obio_ata4_adjust_timing;
		}
		if (strcmp(ca->ca_name, "ata-6") == 0) {
			sc->sc_wdcdev.cap |= WDC_CAPABILITY_UDMA |
			    WDC_CAPABILITY_MODE;
			sc->sc_wdcdev.UDMA_cap = 5;
			sc->sc_wdcdev.set_modes = wdc_obio_ata6_adjust_timing;
		}
	}
	sc->sc_wdcdev.cap |= WDC_CAPABILITY_DATA16;
	sc->sc_wdcdev.PIO_cap = 4;
	sc->wdc_chanptr = chp;
	sc->sc_wdcdev.channels = &sc->wdc_chanptr;
	sc->sc_wdcdev.nchannels = 1;
	sc->sc_wdcdev.dma_arg = sc;
	sc->sc_wdcdev.dma_init = wdc_obio_dma_init;
	sc->sc_wdcdev.dma_start = wdc_obio_dma_start;
	sc->sc_wdcdev.dma_finish = wdc_obio_dma_finish;
	chp->channel = 0;
	chp->wdc = &sc->sc_wdcdev;

	chp->ch_queue = wdc_alloc_queue();
	if (chp->ch_queue == NULL) {
		printf("%s: cannot allocate channel queue",
		sc->sc_wdcdev.sc_dev.dv_xname);
		return;
	}

	wdcattach(chp);
	sc->sc_wdcdev.set_modes(chp);
	wdc_print_current_modes(chp);
}

int
wdc_obio_detach(struct device *self, int flags)
{
	struct wdc_obio_softc *sc = (struct wdc_obio_softc *)self;
	struct channel_softc *chp = &sc->wdc_channel;
	int error;

	if ((error = wdcdetach(chp, flags)) != 0)
		return (error);

	wdc_free_queue(chp->ch_queue);

	if (sc->sc_use_dma) {
		unmapiodev((void *)sc->sc_dmareg, sc->sc_dmasize);
		dbdma_free(sc->sc_dbdma);
	}
	mac_intr_disestablish(NULL, sc->sc_ih);

	bus_space_unmap(chp->cmd_iot, chp->cmd_ioh, sc->sc_cmdsize);
	bus_dmamap_destroy(sc->sc_dmat, sc->sc_dmamap);

	return (0);
}

/* Multiword DMA transfer timings */
struct ide_timings {
	int cycle;	/* minimum cycle time [ns] */
	int active;	/* minimum command active time [ns] */
};

static const struct ide_timings pio_timing[] = {
	{ 600, 165 },    /* Mode 0 */
	{ 383, 125 },    /*      1 */
	{ 240, 100 },    /*      2 */
	{ 180,  80 },    /*      3 */
	{ 120,  70 }     /*      4 */
};

static const struct ide_timings dma_timing[] = {
	{ 480, 215 },	/* Mode 0 */
	{ 150,  80 },	/* Mode 1 */
	{ 120,  70 },	/* Mode 2 */
};

static const struct ide_timings udma_timing[] = {
	{114,   0},     /* Mode 0 */
	{ 75,   0},     /* Mode 1 */
	{ 55,   0},     /* Mode 2 */
	{ 45, 100},     /* Mode 3 */
	{ 25, 100}      /* Mode 4 */
};

/* these number _guessed_ from linux driver. */
static u_int32_t kauai_pio_timing[] = {
	/*600*/	0x08000a92,	/* Mode 0 */
	/*360*/	0x08000492,	/* Mode 1 */
	/*240*/	0x0800038b,	/* Mode 2 */
	/*180*/	0x05000249,	/* Mode 3 */
	/*120*/	0x04000148	/* Mode 4 */
		
};
static u_int32_t kauai_dma_timing[] = {
	/*480*/	0x00618000,	/* Mode 0 */
	/*360*/	0x00492000,	/* Mode 1 */
	/*240*/	0x00149000	/* Mode 2 */ /* fw value */
};
static u_int32_t kauai_udma_timing[] = {
	/*120*/	0x000070c0,	/* Mode 0 */
	/* 90*/	0x00005d80,	/* Mode 1 */
	/* 60*/	0x00004a60,	/* Mode 2 */
	/* 45*/	0x00003a50,	/* Mode 3 */
	/* 30*/	0x00002a30,	/* Mode 4 */
	/* 20*/	0x00002921	/* Mode 5 */
};

#define	TIME_TO_TICK(time)	howmany((time), 30)
#define	PIO_REC_OFFSET	4
#define	PIO_REC_MIN	1
#define	PIO_ACT_MIN	1
#define	DMA_REC_OFFSET	1
#define	DMA_REC_MIN	1
#define	DMA_ACT_MIN	1

#define	ATA4_TIME_TO_TICK(time)	howmany((time) * 1000, 7500)

#define CONFIG_REG (0x200)		/* IDE access timing register */
#define KAUAI_ULTRA_CONFIG (0x210)	/* secondary config register (kauai)*/

#define KAUAI_PIO_MASK		0xff000fff
#define KAUAI_DMA_MASK		0x00fff000
#define KAUAI_UDMA_MASK		0x0000ffff
#define KAUAI_UDMA_EN		0x00000001

void
wdc_obio_adjust_timing(struct channel_softc *chp)
{
	struct ata_drive_datas *drvp;
	u_int conf;
	int drive;
	int piomode = -1, dmamode = -1;
	int min_cycle, min_active;
	int cycle_tick, act_tick, inact_tick, half_tick;

	for (drive = 0; drive < 2; drive++) {
		drvp = &chp->ch_drive[drive];
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;
		if (piomode == -1 || piomode > drvp->PIO_mode)
			piomode = drvp->PIO_mode;
		if (drvp->drive_flags & DRIVE_DMA)
			if (dmamode == -1 || dmamode > drvp->DMA_mode)
				dmamode = drvp->DMA_mode;
	}
	if (piomode == -1)
		return; /* No drive */
	for (drive = 0; drive < 2; drive++) {
		drvp = &chp->ch_drive[drive];
		if (drvp->drive_flags & DRIVE) {
			drvp->PIO_mode = piomode;
			if (drvp->drive_flags & DRIVE_DMA)
				drvp->DMA_mode = dmamode;
		}
	}
	min_cycle = pio_timing[piomode].cycle;
	min_active = pio_timing[piomode].active;

	cycle_tick = TIME_TO_TICK(min_cycle);
	act_tick = TIME_TO_TICK(min_active);
	if (act_tick < PIO_ACT_MIN)
		act_tick = PIO_ACT_MIN;
	inact_tick = cycle_tick - act_tick - PIO_REC_OFFSET;
	if (inact_tick < PIO_REC_MIN)
		inact_tick = PIO_REC_MIN;
	/* mask: 0x000007ff */
	conf = (inact_tick << 5) | act_tick;
	if (dmamode != -1) {
		/* there are active DMA mode */

		min_cycle = dma_timing[dmamode].cycle;
		min_active = dma_timing[dmamode].active;
		cycle_tick = TIME_TO_TICK(min_cycle);
		act_tick = TIME_TO_TICK(min_active);
		inact_tick = cycle_tick - act_tick - DMA_REC_OFFSET;
		if (inact_tick < DMA_REC_MIN)
			inact_tick = DMA_REC_MIN;
		half_tick = 0;	/* XXX */
		/* mask: 0xfffff800 */
		conf |=
		    (half_tick << 21) |
		    (inact_tick << 16) | (act_tick << 11);
	}
	bus_space_write_4(chp->cmd_iot, chp->cmd_ioh, CONFIG_REG, conf);
#if 0
	printf("conf = 0x%x, cyc = %d (%d ns), act = %d (%d ns), inact = %d\n",
	    conf, cycle_tick, min_cycle, act_tick, min_active, inact_tick);
#endif
}

void
wdc_obio_ata4_adjust_timing(struct channel_softc *chp)
{
	struct ata_drive_datas *drvp;
	u_int conf;
	int drive;
	int piomode = -1, dmamode = -1;
	int min_cycle, min_active;
	int cycle_tick, act_tick, inact_tick;
	int udmamode = -1;

	for (drive = 0; drive < 2; drive++) {
		drvp = &chp->ch_drive[drive];
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;
		if (piomode == -1 || piomode > drvp->PIO_mode)
			piomode = drvp->PIO_mode;
		if (drvp->drive_flags & DRIVE_DMA)
			if (dmamode == -1 || dmamode > drvp->DMA_mode)
				dmamode = drvp->DMA_mode;
		if (drvp->drive_flags & DRIVE_UDMA) {
			if (udmamode == -1 || udmamode > drvp->UDMA_mode)
				udmamode = drvp->UDMA_mode;
		} else
			udmamode = -2;
	}
	if (piomode == -1)
		return; /* No drive */
	for (drive = 0; drive < 2; drive++) {
		drvp = &chp->ch_drive[drive];
		if (drvp->drive_flags & DRIVE) {
			drvp->PIO_mode = piomode;
			if (drvp->drive_flags & DRIVE_DMA)
				drvp->DMA_mode = dmamode;
			if (drvp->drive_flags & DRIVE_UDMA) {
				if (udmamode == -2)
					drvp->drive_flags &= ~DRIVE_UDMA;
				else
					drvp->UDMA_mode = udmamode;
			}
		}
	}

	if (udmamode == -2)
		udmamode = -1;

	min_cycle = pio_timing[piomode].cycle;
	min_active = pio_timing[piomode].active;

	cycle_tick = ATA4_TIME_TO_TICK(min_cycle);
	act_tick = ATA4_TIME_TO_TICK(min_active);
	inact_tick = cycle_tick - act_tick;
	/* mask: 0x000003ff */
	conf = (inact_tick << 5) | act_tick;
	if (dmamode != -1) {
		/* there are active  DMA mode */

		min_cycle = dma_timing[dmamode].cycle;
		min_active = dma_timing[dmamode].active;
		cycle_tick = ATA4_TIME_TO_TICK(min_cycle);
		act_tick = ATA4_TIME_TO_TICK(min_active);
		inact_tick = cycle_tick - act_tick;
		/* mask: 0x001ffc00 */
		conf |= (act_tick << 10) | (inact_tick << 15);
	}
	if (udmamode != -1) {
		min_cycle = udma_timing[udmamode].cycle;
		min_active = udma_timing[udmamode].active;
		act_tick = ATA4_TIME_TO_TICK(min_active);
		cycle_tick = ATA4_TIME_TO_TICK(min_cycle);
		/* mask: 0x1ff00000 */
		conf |= (cycle_tick << 21) | (act_tick << 25) | 0x100000;
	}

	bus_space_write_4(chp->cmd_iot, chp->cmd_ioh, CONFIG_REG, conf);
#if 0
	printf("ata4 conf = 0x%x, cyc = %d (%d ns), act = %d (%d ns), inact = %d\n",
	    conf, cycle_tick, min_cycle, act_tick, min_active, inact_tick);
#endif
}

void
wdc_obio_ata6_adjust_timing(struct channel_softc *chp)
{
	struct ata_drive_datas *drvp;
	u_int conf, conf1;
	int drive;
	int piomode = -1, dmamode = -1;
	int udmamode = -1;

	for (drive = 0; drive < 2; drive++) {
		drvp = &chp->ch_drive[drive];
		if ((drvp->drive_flags & DRIVE) == 0)
			continue;
		if (piomode == -1 || piomode > drvp->PIO_mode)
			piomode = drvp->PIO_mode;
		if (drvp->drive_flags & DRIVE_DMA) {
			if (dmamode == -1 || dmamode > drvp->DMA_mode)
				dmamode = drvp->DMA_mode;
		}
		if (drvp->drive_flags & DRIVE_UDMA) {
			if (udmamode == -1 || udmamode > drvp->UDMA_mode)
				udmamode = drvp->UDMA_mode;
		} else
			udmamode = -2;
	}
	if (piomode == -1)
		return; /* No drive */
	for (drive = 0; drive < 2; drive++) {
		drvp = &chp->ch_drive[drive];
		if (drvp->drive_flags & DRIVE) {
			drvp->PIO_mode = piomode;
			if (drvp->drive_flags & DRIVE_DMA)
				drvp->DMA_mode = dmamode;
			if (drvp->drive_flags & DRIVE_UDMA) {
				if (udmamode == -2)
					drvp->drive_flags &= ~DRIVE_UDMA;
				else
					drvp->UDMA_mode = udmamode;
			}
		}
	}

	if (udmamode == -2)
		udmamode = -1;

	conf = bus_space_read_4(chp->cmd_iot, chp->cmd_ioh, CONFIG_REG);
	conf1 = bus_space_read_4(chp->cmd_iot, chp->cmd_ioh,
	    KAUAI_ULTRA_CONFIG);

	conf = (conf & ~KAUAI_PIO_MASK) | kauai_pio_timing[piomode];

	if (dmamode != -1)
		conf = (conf & ~KAUAI_DMA_MASK) | kauai_dma_timing[dmamode];
	if (udmamode != -1)
		conf1 = (conf1 & ~KAUAI_UDMA_MASK) |
		    kauai_udma_timing[udmamode] | KAUAI_UDMA_EN;
	else 
		conf1 = conf1 & ~KAUAI_UDMA_EN;

	bus_space_write_4(chp->cmd_iot, chp->cmd_ioh, CONFIG_REG, conf);
	bus_space_write_4(chp->cmd_iot, chp->cmd_ioh, KAUAI_ULTRA_CONFIG,
	    conf1);
}

int
wdc_obio_dma_init(void *v, int channel, int drive, void *databuf,
    size_t datalen, int flags)
{
	struct wdc_obio_softc *sc = v;
	dbdma_command_t *cmdp;
	u_int cmd;
	int i, error;

	if ((error = bus_dmamap_load(sc->sc_dmat, sc->sc_dmamap, databuf,
	    datalen, NULL, BUS_DMA_NOWAIT)) != 0)
		return (error);

	cmdp = sc->sc_dmacmd;
	cmd = (flags & WDC_DMA_READ) ? DBDMA_CMD_IN_MORE : DBDMA_CMD_OUT_MORE;

	for (i = 0; i < sc->sc_dmamap->dm_nsegs; i++, cmdp++) {
		if (i + 1 == sc->sc_dmamap->dm_nsegs)
			cmd = (flags & WDC_DMA_READ) ? DBDMA_CMD_IN_LAST :
			    DBDMA_CMD_OUT_LAST;

		DBDMA_BUILD(cmdp, cmd, 0, sc->sc_dmamap->dm_segs[i].ds_len,
		    sc->sc_dmamap->dm_segs[i].ds_addr,
		    DBDMA_INT_NEVER, DBDMA_WAIT_NEVER, DBDMA_BRANCH_NEVER);
	}

	DBDMA_BUILD(cmdp, DBDMA_CMD_STOP, 0, 0, 0,
		DBDMA_INT_NEVER, DBDMA_WAIT_NEVER, DBDMA_BRANCH_NEVER);

	return 0;
}

void
wdc_obio_dma_start(void *v, int channel, int drive)
{
	struct wdc_obio_softc *sc = v;

	dbdma_start(sc->sc_dmareg, sc->sc_dbdma);
}

int
wdc_obio_dma_finish(void *v, int channel, int drive, int force)
{
	struct wdc_obio_softc *sc = v;

	dbdma_stop(sc->sc_dmareg);
	bus_dmamap_unload(sc->sc_dmat, sc->sc_dmamap);
	return 0;
}

/* read register code
 * this allows the registers to be spaced by 0x10, instead of 0x1.
 * mac hardware (obio) requires this.
 */

u_int8_t
wdc_obio_read_reg(struct channel_softc *chp, enum wdc_regs reg)
{
#ifdef DIAGNOSTIC
	if (reg & _WDC_WRONLY) {
		printf ("wdc_obio_read_reg: reading from a write-only register %d\n", reg);
	}
#endif

	if (reg & _WDC_AUX)
		return (bus_space_read_1(chp->ctl_iot, chp->ctl_ioh,
		    (reg & _WDC_REGMASK) << 4));
	else
		return (bus_space_read_1(chp->cmd_iot, chp->cmd_ioh,
		    (reg & _WDC_REGMASK) << 4));
}


void
wdc_obio_write_reg(struct channel_softc *chp, enum wdc_regs reg, u_int8_t val)
{
#ifdef DIAGNOSTIC
	if (reg & _WDC_RDONLY) {
		printf ("wdc_obio_write_reg: writing to a read-only register %d\n", reg);
	}
#endif

	if (reg & _WDC_AUX)
		bus_space_write_1(chp->ctl_iot, chp->ctl_ioh,
		    (reg & _WDC_REGMASK) << 4, val);
	else
		bus_space_write_1(chp->cmd_iot, chp->cmd_ioh,
		    (reg & _WDC_REGMASK) << 4, val);
}
