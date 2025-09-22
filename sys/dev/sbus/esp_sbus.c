/*	$OpenBSD: esp_sbus.c,v 1.27 2024/05/17 20:03:13 miod Exp $	*/
/*	$NetBSD: esp_sbus.c,v 1.14 2001/04/25 17:53:37 bouyer Exp $	*/

/*-
 * Copyright (c) 1997, 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum; Jason R. Thorpe of the Numerical Aerospace
 * Simulation Facility, NASA Ames Research Center; Paul Kranenburg.
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
#include <sys/buf.h>
#include <sys/malloc.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>
#include <scsi/scsi_message.h>

#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/autoconf.h>

#include <dev/ic/lsi64854reg.h>
#include <dev/ic/lsi64854var.h>

#include <dev/ic/ncr53c9xreg.h>
#include <dev/ic/ncr53c9xvar.h>

#include <dev/sbus/sbusvar.h>

/* #define ESP_SBUS_DEBUG */

static int esp_unit_offset;

struct esp_softc {
	struct ncr53c9x_softc sc_ncr53c9x;	/* glue to MI code */

	bus_space_tag_t	sc_bustag;
	bus_dma_tag_t	sc_dmatag;

	bus_space_handle_t sc_reg;		/* the registers */
	struct lsi64854_softc *sc_dma;		/* pointer to my dma */

	int	sc_pri;				/* SBUS priority */
};

void	espattach_sbus(struct device *, struct device *, void *);
void	espattach_dma(struct device *, struct device *, void *);
int	espmatch_sbus(struct device *, void *, void *);


/* Linkup to the rest of the kernel */
const struct cfattach esp_sbus_ca = {
	sizeof(struct esp_softc), espmatch_sbus, espattach_sbus
};
const struct cfattach esp_dma_ca = {
	sizeof(struct esp_softc), espmatch_sbus, espattach_dma
};

/*
 * Functions and the switch for the MI code.
 */
static u_char	esp_read_reg(struct ncr53c9x_softc *, int);
static void	esp_write_reg(struct ncr53c9x_softc *, int, u_char);
static u_char	esp_rdreg1(struct ncr53c9x_softc *, int);
static void	esp_wrreg1(struct ncr53c9x_softc *, int, u_char);
static int	esp_dma_isintr(struct ncr53c9x_softc *);
static void	esp_dma_reset(struct ncr53c9x_softc *);
static int	esp_dma_intr(struct ncr53c9x_softc *);
static int	esp_dma_setup(struct ncr53c9x_softc *, caddr_t *,
				    size_t *, int, size_t *);
static void	esp_dma_go(struct ncr53c9x_softc *);
static void	esp_dma_stop(struct ncr53c9x_softc *);
static int	esp_dma_isactive(struct ncr53c9x_softc *);

static struct ncr53c9x_glue esp_sbus_glue = {
	esp_read_reg,
	esp_write_reg,
	esp_dma_isintr,
	esp_dma_reset,
	esp_dma_intr,
	esp_dma_setup,
	esp_dma_go,
	esp_dma_stop,
	esp_dma_isactive,
	NULL,			/* gl_clear_latched_intr */
};

static struct ncr53c9x_glue esp_sbus_glue1 = {
	esp_rdreg1,
	esp_wrreg1,
	esp_dma_isintr,
	esp_dma_reset,
	esp_dma_intr,
	esp_dma_setup,
	esp_dma_go,
	esp_dma_stop,
	esp_dma_isactive,
	NULL,			/* gl_clear_latched_intr */
};

static void	espattach(struct esp_softc *, struct ncr53c9x_glue *);

int
espmatch_sbus(struct device *parent, void *vcf, void *aux)
{
	struct cfdata *cf = vcf;
	int rv;
	struct sbus_attach_args *sa = aux;

	if (strcmp("SUNW,fas", sa->sa_name) == 0)
	        return 1;

	rv = (strcmp(cf->cf_driver->cd_name, sa->sa_name) == 0 ||
	    strcmp("ptscII", sa->sa_name) == 0);
	return (rv);
}

void
espattach_sbus(struct device *parent, struct device *self, void *aux)
{
	struct esp_softc *esc = (void *)self;
	struct ncr53c9x_softc *sc = &esc->sc_ncr53c9x;
	struct sbus_attach_args *sa = aux;
	struct lsi64854_softc *lsc;
	int burst, sbusburst;

	esc->sc_bustag = sa->sa_bustag;
	esc->sc_dmatag = sa->sa_dmatag;

	sc->sc_id = getpropint(sa->sa_node, "initiator-id", 7);
	sc->sc_freq = getpropint(sa->sa_node, "clock-frequency", -1);
	if (sc->sc_freq < 0)
		sc->sc_freq = sa->sa_frequency;

#ifdef ESP_SBUS_DEBUG
	printf("%s: espattach_sbus: sc_id %d, freq %d\n",
	       self->dv_xname, sc->sc_id, sc->sc_freq);
#endif

	if (strcmp("SUNW,fas", sa->sa_name) == 0) {
		/*
		 * offset searches for other esp/dma devices.
		 */
		esp_unit_offset++;

		/*
		 * fas has 2 register spaces: dma(lsi64854) and SCSI core (ncr53c9x)
		 */
		if (sa->sa_nreg != 2) {
			printf("%s: %d register spaces\n", self->dv_xname, sa->sa_nreg);
			return;
		}

		/*
		 * allocate space for dma, in SUNW,fas there are no separate
		 * dma device
		 */
		lsc = malloc(sizeof (struct lsi64854_softc), M_DEVBUF, M_NOWAIT);

		if (lsc == NULL) {
			printf("%s: out of memory (lsi64854_softc)\n",
			       self->dv_xname);
			return;
		}
		esc->sc_dma = lsc;

		lsc->sc_bustag = sa->sa_bustag;
		lsc->sc_dmatag = sa->sa_dmatag;

		bcopy(sc->sc_dev.dv_xname, lsc->sc_dev.dv_xname,
		      sizeof (lsc->sc_dev.dv_xname));

		/* Map dma registers */
		if (sbus_bus_map(sa->sa_bustag, sa->sa_reg[0].sbr_slot,
		    sa->sa_reg[0].sbr_offset, sa->sa_reg[0].sbr_size,
		    0, 0, &lsc->sc_regs) != 0) {
			printf("%s: cannot map dma registers\n", self->dv_xname);
			return;
		}

		/*
		 * XXX is this common(from bpp.c), the same in dma_sbus...etc.
		 *
		 * Get transfer burst size from PROM and plug it into the
		 * controller registers. This is needed on the Sun4m; do
		 * others need it too?
		 */
		sbusburst = ((struct sbus_softc *)parent)->sc_burst;
		if (sbusburst == 0)
			sbusburst = SBUS_BURST_32 - 1; /* 1->16 */

		burst = getpropint(sa->sa_node, "burst-sizes", -1);

#ifdef ESP_SBUS_DEBUG
		printf("espattach_sbus: burst 0x%x, sbus 0x%x\n",
		    burst, sbusburst);
#endif

		if (burst == -1)
			/* take SBus burst sizes */
			burst = sbusburst;

		/* Clamp at parent's burst sizes */
		burst &= sbusburst;
		lsc->sc_burst = (burst & SBUS_BURST_32) ? 32 :
		    (burst & SBUS_BURST_16) ? 16 : 0;

		lsc->sc_channel = L64854_CHANNEL_SCSI;
		lsc->sc_client = sc;

		lsi64854_attach(lsc);

		/*
		 * map SCSI core registers
		 */
		if (sbus_bus_map(sa->sa_bustag, sa->sa_reg[1].sbr_slot,
		    sa->sa_reg[1].sbr_offset, sa->sa_reg[1].sbr_size,
		    0, 0, &esc->sc_reg) != 0) {
			printf("%s: cannot map scsi core registers\n",
			       self->dv_xname);
			return;
		}

		if (sa->sa_nintr == 0) {
			printf("%s: no interrupt property\n", self->dv_xname);
			return;
		}

		esc->sc_pri = sa->sa_pri;

		printf("%s", self->dv_xname);
		espattach(esc, &esp_sbus_glue);

		return;
	}

	/*
	 * Find the DMA by poking around the dma device structures
	 * What happens here is that if the dma driver has not been
	 * configured, then this returns a NULL pointer.
	 */
	esc->sc_dma = (struct lsi64854_softc *)
	    getdevunit("dma", sc->sc_dev.dv_unit - esp_unit_offset);

	/*
	 * add a back pointer to us, for DMA
	 */
	if (esc->sc_dma)
		esc->sc_dma->sc_client = sc;
	else {
		printf("\n");
		panic("espattach: no dma found");
	}

	/*
	 * The `ESC' DMA chip must be reset before we can access
	 * the esp registers.
	 */
	if (esc->sc_dma->sc_rev == DMAREV_ESC)
		DMA_RESET(esc->sc_dma);

	/*
	 * Map my registers in, if they aren't already in virtual
	 * address space.
	 */
	if (sa->sa_npromvaddrs) {
		if (bus_space_map(sa->sa_bustag, sa->sa_promvaddrs[0],
		    sa->sa_size, BUS_SPACE_MAP_PROMADDRESS,
		    &esc->sc_reg) != 0) {
			printf("%s @ sbus: cannot map registers\n",
				self->dv_xname);
			return;
		}
	} else {
		if (sbus_bus_map(sa->sa_bustag, sa->sa_slot,
		    sa->sa_offset, sa->sa_size, 0, 0, &esc->sc_reg) != 0) {
			printf("%s @ sbus: cannot map registers\n",
				self->dv_xname);
			return;
		}
	}

	if (sa->sa_nintr == 0) {
		/*
		 * No interrupt properties: we quit; this might
		 * happen on e.g. a Sparc X terminal.
		 */
		printf("\n%s: no interrupt property\n", self->dv_xname);
		return;
	}

	esc->sc_pri = sa->sa_pri;

	if (strcmp("ptscII", sa->sa_name) == 0) {
		espattach(esc, &esp_sbus_glue1);
	} else {
		espattach(esc, &esp_sbus_glue);
	}
}

void
espattach_dma(struct device *parent, struct device *self, void *aux)
{
	struct esp_softc *esc = (void *)self;
	struct ncr53c9x_softc *sc = &esc->sc_ncr53c9x;
	struct sbus_attach_args *sa = aux;

	if (strcmp("ptscII", sa->sa_name) == 0) {
		return;
	}

	esc->sc_bustag = sa->sa_bustag;
	esc->sc_dmatag = sa->sa_dmatag;

	sc->sc_id = getpropint(sa->sa_node, "initiator-id", 7);
	sc->sc_freq = getpropint(sa->sa_node, "clock-frequency", -1);

	esc->sc_dma = (struct lsi64854_softc *)parent;
	esc->sc_dma->sc_client = sc;

	/*
	 * Map my registers in, if they aren't already in virtual
	 * address space.
	 */
	if (sa->sa_npromvaddrs) {
		if (bus_space_map(sa->sa_bustag, sa->sa_promvaddrs[0],
		    sa->sa_size /* ??? */, BUS_SPACE_MAP_PROMADDRESS,
		    &esc->sc_reg) != 0) {
			printf("%s @ dma: cannot map registers\n",
				self->dv_xname);
			return;
		}
	} else {
		if (sbus_bus_map(sa->sa_bustag, sa->sa_slot, sa->sa_offset,
		    sa->sa_size, 0, 0, &esc->sc_reg) != 0) {
			printf("%s @ dma: cannot map registers\n",
				self->dv_xname);
			return;
		}
	}

	if (sa->sa_nintr == 0) {
		/*
		 * No interrupt properties: we quit; this might
		 * happen on e.g. a Sparc X terminal.
		 */
		printf("\n%s: no interrupt property\n", self->dv_xname);
		return;
	}

	esc->sc_pri = sa->sa_pri;

	espattach(esc, &esp_sbus_glue);
}


/*
 * Attach this instance, and then all the sub-devices
 */
void
espattach(struct esp_softc *esc, struct ncr53c9x_glue *gluep)
{
	struct ncr53c9x_softc *sc = &esc->sc_ncr53c9x;
	void *icookie;
	unsigned int uid = 0;

	/*
	 * Set up glue for MI code early; we use some of it here.
	 */
	sc->sc_glue = gluep;

	/* gimme MHz */
	sc->sc_freq /= 1000000;

	/*
	 * XXX More of this should be in ncr53c9x_attach(), but
	 * XXX should we really poke around the chip that much in
	 * XXX the MI code?  Think about this more...
	 */

	/*
	 * It is necessary to try to load the 2nd config register here,
	 * to find out what rev the esp chip is, else the ncr53c9x_reset
	 * will not set up the defaults correctly.
	 */
	sc->sc_cfg1 = sc->sc_id | NCRCFG1_PARENB;
	sc->sc_cfg2 = NCRCFG2_SCSI2 | NCRCFG2_RPE;
	sc->sc_cfg3 = NCRCFG3_CDB;
	NCR_WRITE_REG(sc, NCR_CFG2, sc->sc_cfg2);

	if ((NCR_READ_REG(sc, NCR_CFG2) & ~NCRCFG2_RSVD) !=
	    (NCRCFG2_SCSI2 | NCRCFG2_RPE)) {
		sc->sc_rev = NCR_VARIANT_ESP100;
	} else {
		sc->sc_cfg2 = NCRCFG2_SCSI2;
		NCR_WRITE_REG(sc, NCR_CFG2, sc->sc_cfg2);
		sc->sc_cfg3 = 0;
		NCR_WRITE_REG(sc, NCR_CFG3, sc->sc_cfg3);
		sc->sc_cfg3 = (NCRCFG3_CDB | NCRCFG3_FCLK);
		NCR_WRITE_REG(sc, NCR_CFG3, sc->sc_cfg3);
		if (NCR_READ_REG(sc, NCR_CFG3) !=
		    (NCRCFG3_CDB | NCRCFG3_FCLK)) {
			sc->sc_rev = NCR_VARIANT_ESP100A;
		} else {
			/* NCRCFG2_FE enables > 64K transfers */
			sc->sc_cfg2 |= NCRCFG2_FE;
			sc->sc_cfg3 = 0;
			NCR_WRITE_REG(sc, NCR_CFG3, sc->sc_cfg3);
			sc->sc_rev = NCR_VARIANT_ESP200;

			/* XXX spec says it's valid after power up or chip reset */
			uid = NCR_READ_REG(sc, NCR_UID);
			if (((uid & 0xf8) >> 3) == 0x0a) /* XXX */
				sc->sc_rev = NCR_VARIANT_FAS366;
		}
	}

#ifdef ESP_SBUS_DEBUG
	printf("espattach: revision %d, uid 0x%x\n", sc->sc_rev, uid);
#endif

	/*
	 * XXX minsync and maxxfer _should_ be set up in MI code,
	 * XXX but it appears to have some dependency on what sort
	 * XXX of DMA we're hooked up to, etc.
	 */

	/*
	 * This is the value used to start sync negotiations
	 * Note that the NCR register "SYNCTP" is programmed
	 * in "clocks per byte", and has a minimum value of 4.
	 * The SCSI period used in negotiation is one-fourth
	 * of the time (in nanoseconds) needed to transfer one byte.
	 * Since the chip's clock is given in MHz, we have the following
	 * formula: 4 * period = (1000 / freq) * 4
	 */
	sc->sc_minsync = 1000 / sc->sc_freq;

	/*
	 * Alas, we must now modify the value a bit, because it's
	 * only valid when can switch on FASTCLK and FASTSCSI bits  
	 * in config register 3... 
	 */
	switch (sc->sc_rev) {
	case NCR_VARIANT_ESP100:
		sc->sc_maxxfer = 64 * 1024;
		sc->sc_minsync = 0;	/* No synch on old chip? */
		break;

	case NCR_VARIANT_ESP100A:
		sc->sc_maxxfer = 64 * 1024;
		/* Min clocks/byte is 5 */
		sc->sc_minsync = ncr53c9x_cpb2stp(sc, 5);
		break;

	case NCR_VARIANT_ESP200:
	case NCR_VARIANT_FAS366:
		sc->sc_maxxfer = 16 * 1024 * 1024;
		/* XXX - do actually set FAST* bits */
		break;
	}

	/* Establish interrupt channel */
	icookie = bus_intr_establish(esc->sc_bustag, esc->sc_pri, IPL_BIO, 0,
				     ncr53c9x_intr, sc, sc->sc_dev.dv_xname);

	/* Turn on target selection using the `dma' method */
	if (sc->sc_rev != NCR_VARIANT_FAS366)
		sc->sc_features |= NCR_F_DMASELECT;

	/* Do the common parts of attachment. */
	ncr53c9x_attach(sc);
}

/*
 * Glue functions.
 */

#ifdef ESP_SBUS_DEBUG
int esp_sbus_debug = 0;

static struct {
	char *r_name;
	int   r_flag; 
} esp__read_regnames [] = {
	{ "TCL", 0},			/* 0/00 */
	{ "TCM", 0},			/* 1/04 */
	{ "FIFO", 0},			/* 2/08 */
	{ "CMD", 0},			/* 3/0c */
	{ "STAT", 0},			/* 4/10 */
	{ "INTR", 0},			/* 5/14 */
	{ "STEP", 0},			/* 6/18 */
	{ "FFLAGS", 1},			/* 7/1c */
	{ "CFG1", 1},			/* 8/20 */
	{ "STAT2", 0},			/* 9/24 */
	{ "CFG4", 1},			/* a/28 */
	{ "CFG2", 1},			/* b/2c */
	{ "CFG3", 1},			/* c/30 */
	{ "-none", 1},			/* d/34 */
	{ "TCH", 1},			/* e/38 */
	{ "TCX", 1},			/* f/3c */
};

static struct {
	char *r_name;
	int   r_flag;
} esp__write_regnames[] = {
	{ "TCL", 1},			/* 0/00 */
	{ "TCM", 1},			/* 1/04 */
	{ "FIFO", 0},			/* 2/08 */
	{ "CMD", 0},			/* 3/0c */
	{ "SELID", 1},			/* 4/10 */
	{ "TIMEOUT", 1},		/* 5/14 */
	{ "SYNCTP", 1},			/* 6/18 */
	{ "SYNCOFF", 1},		/* 7/1c */
	{ "CFG1", 1},			/* 8/20 */
	{ "CCF", 1},			/* 9/24 */
	{ "TEST", 1},			/* a/28 */
	{ "CFG2", 1},			/* b/2c */
	{ "CFG3", 1},			/* c/30 */
	{ "-none", 1},			/* d/34 */
	{ "TCH", 1},			/* e/38 */
	{ "TCX", 1},			/* f/3c */
};
#endif

u_char
esp_read_reg(struct ncr53c9x_softc *sc, int reg)
{
	struct esp_softc *esc = (struct esp_softc *)sc;
	u_char v;

	v = bus_space_read_1(esc->sc_bustag, esc->sc_reg, reg * 4);
#ifdef ESP_SBUS_DEBUG
	if (esp_sbus_debug && (reg < 0x10) && esp__read_regnames[reg].r_flag)
		printf("RD:%x <%s> %x\n", reg * 4,
		    ((unsigned)reg < 0x10) ? esp__read_regnames[reg].r_name : "<***>", v);
#endif
	return v;
}

void
esp_write_reg(struct ncr53c9x_softc *sc, int reg, u_char v)
{
	struct esp_softc *esc = (struct esp_softc *)sc;

#ifdef ESP_SBUS_DEBUG
	if (esp_sbus_debug && (reg < 0x10) && esp__write_regnames[reg].r_flag)
		printf("WR:%x <%s> %x\n", reg * 4,
		    ((unsigned)reg < 0x10) ? esp__write_regnames[reg].r_name : "<***>", v);
#endif
	bus_space_write_1(esc->sc_bustag, esc->sc_reg, reg * 4, v);
}

u_char
esp_rdreg1(struct ncr53c9x_softc *sc, int reg)
{
	struct esp_softc *esc = (struct esp_softc *)sc;

	return (bus_space_read_1(esc->sc_bustag, esc->sc_reg, reg));
}

void
esp_wrreg1(struct ncr53c9x_softc *sc, int reg, u_char v)
{
	struct esp_softc *esc = (struct esp_softc *)sc;

	bus_space_write_1(esc->sc_bustag, esc->sc_reg, reg, v);
}

int
esp_dma_isintr(struct ncr53c9x_softc *sc)
{
	struct esp_softc *esc = (struct esp_softc *)sc;

	return (DMA_ISINTR(esc->sc_dma));
}

void
esp_dma_reset(struct ncr53c9x_softc *sc)
{
	struct esp_softc *esc = (struct esp_softc *)sc;

	DMA_RESET(esc->sc_dma);
}

int
esp_dma_intr(struct ncr53c9x_softc *sc)
{
	struct esp_softc *esc = (struct esp_softc *)sc;

	return (DMA_INTR(esc->sc_dma));
}

int
esp_dma_setup(struct ncr53c9x_softc *sc, caddr_t *addr, size_t *len,
    int datain, size_t *dmasize)
{
	struct esp_softc *esc = (struct esp_softc *)sc;

	return (DMA_SETUP(esc->sc_dma, addr, len, datain, dmasize));
}

void
esp_dma_go(struct ncr53c9x_softc *sc)
{
	struct esp_softc *esc = (struct esp_softc *)sc;

	DMA_GO(esc->sc_dma);
}

void
esp_dma_stop(struct ncr53c9x_softc *sc)
{
	struct esp_softc *esc = (struct esp_softc *)sc;
	u_int32_t csr;

	csr = L64854_GCSR(esc->sc_dma);
	csr &= ~D_EN_DMA;
	L64854_SCSR(esc->sc_dma, csr);
}

int
esp_dma_isactive(struct ncr53c9x_softc *sc)
{
	struct esp_softc *esc = (struct esp_softc *)sc;

	return (DMA_ISACTIVE(esc->sc_dma));
}

#if defined(DDB) && defined(notyet)
#include <machine/db_machdep.h>
#include <ddb/db_output.h>

void db_esp(db_expr_t, int, db_expr_t, char *);

void
db_esp(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	struct ncr53c9x_softc *sc;
	struct ncr53c9x_ecb *ecb;
	struct ncr53c9x_linfo *li;
	int u, t, i;

	for (u=0; u<10; u++) {
		sc = (struct ncr53c9x_softc *)
			getdevunit("esp", u);
		if (!sc) continue;

		db_printf("esp%d: nexus %p phase %x prev %x dp %p dleft %lx ify %x\n",
			  u, sc->sc_nexus, sc->sc_phase, sc->sc_prevphase, 
			  sc->sc_dp, sc->sc_dleft, sc->sc_msgify);
		db_printf("\tmsgout %x msgpriq %x msgin %x:%x:%x:%x:%x\n",
			  sc->sc_msgout, sc->sc_msgpriq, sc->sc_imess[0],
			  sc->sc_imess[1], sc->sc_imess[2], sc->sc_imess[3],
			  sc->sc_imess[0]);
		db_printf("ready: ");
		TAILQ_FOREACH(ecb, &sc->ready_list, chain) {
			db_printf("ecb %p ", ecb);
			if (ecb == TAILQ_NEXT(ecb, chain)) {
				db_printf("\nWARNING: tailq loop on ecb %p", ecb);
				break;
			}
		}
		db_printf("\n");
		
		for (t=0; t<NCR_NTARG; t++) {
			LIST_FOREACH(li, &sc->sc_tinfo[t].luns, link) {
				db_printf("t%d lun %d untagged %p busy %d used %x\n",
					  t, (int)li->lun, li->untagged, li->busy,
					  li->used);
				for (i=0; i<256; i++)
					if ((ecb = li->queued[i])) {
						db_printf("ecb %p tag %x\n", ecb, i);
					}
			}
		}
	}
}
#endif

