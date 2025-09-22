/* $OpenBSD: tcds.c,v 1.12 2025/06/29 15:55:22 miod Exp $ */
/* $NetBSD: tcds.c,v 1.3 2001/11/13 06:26:10 lukem Exp $ */

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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

/*
 * Copyright (c) 1994, 1995, 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Keith Bostic, Chris G. Demetriou
 * 
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" 
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND 
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#ifdef __alpha__
#include <machine/rpb.h>
#endif /* __alpha__ */

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/ic/ncr53c9xvar.h>

#include <machine/bus.h>

#include <dev/tc/tcvar.h>
#include <dev/tc/tcdsreg.h>
#include <dev/tc/tcdsvar.h>

struct tcds_softc {
	struct	device sc_dv;
	bus_space_tag_t sc_bst;
	bus_space_handle_t sc_bsh;
	bus_dma_tag_t sc_dmat;
	void	*sc_cookie;
	int	sc_flags;
	struct tcds_slotconfig sc_slots[2];
};

/* sc_flags */
#define	TCDSF_BASEBOARD		0x01	/* baseboard on DEC 3000 */
#define	TCDSF_FASTSCSI		0x02	/* supports Fast SCSI */

/* Definition of the driver for autoconfig. */
int	tcdsmatch(struct device *, void *, void *);
void	tcdsattach(struct device *, struct device *, void *);
int     tcdsprint(void *, const char *);
int	tcdssubmatch(struct device *, void *, void *);

const struct cfattach tcds_ca = {
	sizeof(struct tcds_softc), tcdsmatch, tcdsattach,
};

struct cfdriver tcds_cd = {
	NULL, "tcds", DV_DULL,
};

/*static*/ int	tcds_intr(void *);
/*static*/ int	tcds_intrnull(void *);

struct tcds_device {
	const char *td_name;
	int td_flags;
} tcds_devices[] = {
#ifdef __alpha__
	{ "PMAZ-DS ",	TCDSF_BASEBOARD },
	{ "PMAZ-FS ",	TCDSF_BASEBOARD|TCDSF_FASTSCSI },
#endif /* __alpha__ */
	{ "PMAZB-AA",	0 },
	{ "PMAZC-AA",	TCDSF_FASTSCSI },
	{ NULL,		0 },
};

struct tcds_device *tcds_lookup(const char *);
void	tcds_params(struct tcds_softc *, int, int *, int *);

struct tcds_device *
tcds_lookup(const char *modname)
{
	struct tcds_device *td;

	for (td = tcds_devices; td->td_name != NULL; td++)
		if (strncmp(td->td_name, modname, TC_ROM_LLEN) == 0)
			return (td);

	return (NULL);
}

int
tcdsmatch(struct device *parent, void *cfdata, void *aux)
{
	struct tc_attach_args *ta = aux;

	return (tcds_lookup(ta->ta_modname) != NULL);
}

void
tcdsattach(struct device *parent, struct device *self, void *aux)
{
	struct tcds_softc *sc = (struct tcds_softc *)self;
	struct tc_attach_args *ta = aux;
	struct tcdsdev_attach_args tcdsdev;
	struct tcds_slotconfig *slotc;
	struct tcds_device *td;
	bus_space_handle_t sbsh[2];
	int i, gpi2;

	td = tcds_lookup(ta->ta_modname);
	if (td == NULL)
		panic("tcdsattach: impossible");

	printf(": TurboChannel Dual SCSI");
	if (td->td_flags & TCDSF_BASEBOARD)
		printf(" (baseboard)");
	printf("\n");

	sc->sc_flags = td->td_flags;

	sc->sc_bst = ta->ta_memt;
	sc->sc_dmat = ta->ta_dmat;

	/*
	 * Map the device.
	 */
	if (bus_space_map(sc->sc_bst, ta->ta_addr,
	    (TCDS_SCSI1_OFFSET + 0x100), 0, &sc->sc_bsh)) {
		printf("%s: unable to map device\n", sc->sc_dv.dv_xname);
		return;
	}

	/*
	 * Now, slice off two subregions for the individual NCR SCSI chips.
	 */
	if (bus_space_subregion(sc->sc_bst, sc->sc_bsh, TCDS_SCSI0_OFFSET,
	    0x100, &sbsh[0]) ||
	    bus_space_subregion(sc->sc_bst, sc->sc_bsh, TCDS_SCSI1_OFFSET,
	    0x100, &sbsh[1])) {
		printf("%s: unable to subregion SCSI chip space\n",
		    sc->sc_dv.dv_xname);
		return;
	}

	sc->sc_cookie = ta->ta_cookie;

	tc_intr_establish(parent, sc->sc_cookie, IPL_BIO, tcds_intr, sc,
	    self->dv_xname);

	/*
	 * XXX
	 * IMER apparently has some random (or, not so random, but still
	 * not useful) bits set in it when the system boots.  Clear it.
	 */
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, TCDS_IMER, 0);

	/* XXX Initial contents of CIR? */

	/*
	 * Remember if GPI2 is set in the CIR; we'll need it later.
	 */
	gpi2 = (bus_space_read_4(sc->sc_bst, sc->sc_bsh, TCDS_CIR) &
	    TCDS_CIR_GPI_2) != 0;

	/*
	 * Set up the per-slot definitions for later use.
	 */

	/* fill in common information first */
	for (i = 0; i < 2; i++) {
		slotc = &sc->sc_slots[i];
		bzero(slotc, sizeof *slotc);	/* clear everything */

		slotc->sc_slot = i;
		slotc->sc_bst = sc->sc_bst;
		slotc->sc_bsh = sc->sc_bsh;
		slotc->sc_intrhand = tcds_intrnull;
		slotc->sc_intrarg = (void *)(long)i;
	}

	/* information for slot 0 */
	slotc = &sc->sc_slots[0];
	slotc->sc_resetbits = TCDS_CIR_SCSI0_RESET;
	slotc->sc_intrmaskbits =
	    TCDS_IMER_SCSI0_MASK | TCDS_IMER_SCSI0_ENB;
	slotc->sc_intrbits = TCDS_CIR_SCSI0_INT;
	slotc->sc_dmabits = TCDS_CIR_SCSI0_DMAENA;
	slotc->sc_errorbits = 0;				/* XXX */
	slotc->sc_sda = TCDS_SCSI0_DMA_ADDR;
	slotc->sc_dic = TCDS_SCSI0_DMA_INTR;
	slotc->sc_dud0 = TCDS_SCSI0_DMA_DUD0;
	slotc->sc_dud1 = TCDS_SCSI0_DMA_DUD1;

	/* information for slot 1 */
	slotc = &sc->sc_slots[1];
	slotc->sc_resetbits = TCDS_CIR_SCSI1_RESET;
	slotc->sc_intrmaskbits =
	    TCDS_IMER_SCSI1_MASK | TCDS_IMER_SCSI1_ENB;
	slotc->sc_intrbits = TCDS_CIR_SCSI1_INT;
	slotc->sc_dmabits = TCDS_CIR_SCSI1_DMAENA;
	slotc->sc_errorbits = 0;				/* XXX */
	slotc->sc_sda = TCDS_SCSI1_DMA_ADDR;
	slotc->sc_dic = TCDS_SCSI1_DMA_INTR;
	slotc->sc_dud0 = TCDS_SCSI1_DMA_DUD0;
	slotc->sc_dud1 = TCDS_SCSI1_DMA_DUD1;

	/* find the hardware attached to the TCDS ASIC */
	for (i = 0; i < 2; i++) {
		tcds_params(sc, i, &tcdsdev.tcdsda_id,
		    &tcdsdev.tcdsda_fast);

		tcdsdev.tcdsda_bst = sc->sc_bst;
		tcdsdev.tcdsda_bsh = sbsh[i];
		tcdsdev.tcdsda_dmat = sc->sc_dmat;
		tcdsdev.tcdsda_chip = i;
		tcdsdev.tcdsda_sc = &sc->sc_slots[i];
		/*
		 * Determine the chip frequency.  TCDSF_FASTSCSI will be set
		 * for TC option cards.  For baseboard chips, GPI2 is set, for a
		 * 25MHz clock, else a 40MHz clock.
		 */
		if ((sc->sc_flags & TCDSF_BASEBOARD && gpi2 == 0) ||
		    sc->sc_flags & TCDSF_FASTSCSI) {
			tcdsdev.tcdsda_freq = 40000000;
			tcdsdev.tcdsda_period = tcdsdev.tcdsda_fast ? 4 : 8;
		} else {
			tcdsdev.tcdsda_freq = 25000000;
			tcdsdev.tcdsda_period = 5;
		}
		if (sc->sc_flags & TCDSF_BASEBOARD)
			tcdsdev.tcdsda_variant = NCR_VARIANT_NCR53C94;
		else
			tcdsdev.tcdsda_variant = NCR_VARIANT_NCR53C96;

		tcds_scsi_reset(tcdsdev.tcdsda_sc);

		config_found_sm(self, &tcdsdev, tcdsprint, tcdssubmatch);
#ifdef __alpha__
		/*
		 * The second SCSI chip isn't present on the baseboard TCDS
		 * on the DEC Alpha 3000/300 series.
		 */
		if (sc->sc_flags & TCDSF_BASEBOARD &&
		    cputype == ST_DEC_3000_300)
			break;
#endif /* __alpha__ */
	}
}

int
tcdssubmatch(struct device *parent, void *vcf, void *aux)
{
	struct tcdsdev_attach_args *tcdsdev = aux;
	struct cfdata *cf = vcf;

	if (cf->cf_loc[0] != -1 &&
	    cf->cf_loc[0] != tcdsdev->tcdsda_chip)
		return (0);

	return ((*cf->cf_attach->ca_match)(parent, vcf, aux));
}

int
tcdsprint(void *aux, const char *pnp)
{
	struct tcdsdev_attach_args *tcdsdev = aux;

	/* Only ASCs can attach to TCDSs; easy. */
	if (pnp)
		printf("asc at %s", pnp);

	printf(" chip %d", tcdsdev->tcdsda_chip);

	return (UNCONF);
}

void
tcds_intr_establish(struct device *tcds, int slot, int (*func)(void *),
    void *arg, const char *name)
{
	struct tcds_softc *sc = (struct tcds_softc *)tcds;

	if (sc->sc_slots[slot].sc_intrhand != tcds_intrnull)
		panic("tcds_intr_establish: chip %d twice", slot);

	sc->sc_slots[slot].sc_intrhand = func;
	sc->sc_slots[slot].sc_intrarg = arg;
	evcount_attach(&sc->sc_slots[slot].sc_count, name, NULL);

	tcds_scsi_reset(&sc->sc_slots[slot]);
}

void
tcds_intr_disestablish(struct device *tcds, int slot)
{
	struct tcds_softc *sc = (struct tcds_softc *)tcds;

	if (sc->sc_slots[slot].sc_intrhand == tcds_intrnull)
		panic("tcds_intr_disestablish: chip %d missing intr",
		    slot);

	sc->sc_slots[slot].sc_intrhand = tcds_intrnull;
	sc->sc_slots[slot].sc_intrarg = (void *)(u_long)slot;
	evcount_detach(&sc->sc_slots[slot].sc_count);

	tcds_dma_enable(&sc->sc_slots[slot], 0);
	tcds_scsi_enable(&sc->sc_slots[slot], 0);
}

int
tcds_intrnull(void *val)
{

	panic("tcds_intrnull: uncaught TCDS intr for chip %lu",
	    (u_long)val);
}

void
tcds_scsi_reset(struct tcds_slotconfig *sc)
{
	u_int32_t cir;

	tcds_dma_enable(sc, 0);
	tcds_scsi_enable(sc, 0);

	cir = bus_space_read_4(sc->sc_bst, sc->sc_bsh, TCDS_CIR);
	TCDS_CIR_CLR(cir, sc->sc_resetbits);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, TCDS_CIR, cir);

	DELAY(1);

	cir = bus_space_read_4(sc->sc_bst, sc->sc_bsh, TCDS_CIR);
	TCDS_CIR_SET(cir, sc->sc_resetbits);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, TCDS_CIR, cir);

	tcds_scsi_enable(sc, 1);
	tcds_dma_enable(sc, 1);
}

void
tcds_scsi_enable(struct tcds_slotconfig *sc, int on)
{
	u_int32_t imer;

	imer = bus_space_read_4(sc->sc_bst, sc->sc_bsh, TCDS_IMER);

	if (on)
		imer |= sc->sc_intrmaskbits;
	else
		imer &= ~sc->sc_intrmaskbits;

	bus_space_write_4(sc->sc_bst, sc->sc_bsh, TCDS_IMER, imer);
}

void
tcds_dma_enable(struct tcds_slotconfig *sc, int on)
{
	u_int32_t cir;

	cir = bus_space_read_4(sc->sc_bst, sc->sc_bsh, TCDS_CIR);

	/* XXX Clear/set IOSLOT/PBS bits. */
	if (on)
		TCDS_CIR_SET(cir, sc->sc_dmabits);
	else
		TCDS_CIR_CLR(cir, sc->sc_dmabits);

	bus_space_write_4(sc->sc_bst, sc->sc_bsh, TCDS_CIR, cir);
}

int
tcds_scsi_isintr(struct tcds_slotconfig *sc, int clear)
{
	u_int32_t cir;

	cir = bus_space_read_4(sc->sc_bst, sc->sc_bsh, TCDS_CIR);

	if ((cir & sc->sc_intrbits) != 0) {
		if (clear) {
			TCDS_CIR_CLR(cir, sc->sc_intrbits);
			bus_space_write_4(sc->sc_bst, sc->sc_bsh, TCDS_CIR,
			    cir);
		}
		return (1);
	} else
		return (0);
}

int
tcds_scsi_iserr(struct tcds_slotconfig *sc)
{
	u_int32_t cir;

	cir = bus_space_read_4(sc->sc_bst, sc->sc_bsh, TCDS_CIR);
	return ((cir & sc->sc_errorbits) != 0);
}

int
tcds_intr(void *arg)
{
	struct tcds_softc *sc = arg;
	u_int32_t ir, ir0;

	/*
	 * XXX
	 * Copy and clear (gag!) the interrupts.
	 */
	ir = ir0 = bus_space_read_4(sc->sc_bst, sc->sc_bsh, TCDS_CIR);
	TCDS_CIR_CLR(ir0, TCDS_CIR_ALLINTR);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, TCDS_CIR, ir0);
	tc_syncbus();

#define	CHECKINTR(slot)							\
	if (ir & sc->sc_slots[slot].sc_intrbits) {			\
		sc->sc_slots[slot].sc_count.ec_count++;			\
		(void)(*sc->sc_slots[slot].sc_intrhand)			\
		    (sc->sc_slots[slot].sc_intrarg);			\
	}
	CHECKINTR(0);
	CHECKINTR(1);
#undef CHECKINTR

#ifdef DIAGNOSTIC
	/*
	 * Interrupts not currently handled, but would like to know if they
	 * occur.
	 *
	 * XXX
	 * Don't know if we have to set the interrupt mask and enable bits
	 * in the IMER to allow some of them to happen?
	 */
#define	PRINTINTR(msg, bits)						\
	if (ir & bits)							\
		printf("%s: %s", sc->sc_dv.dv_xname, msg);
	PRINTINTR("SCSI0 DREQ interrupt.\n", TCDS_CIR_SCSI0_DREQ);
	PRINTINTR("SCSI1 DREQ interrupt.\n", TCDS_CIR_SCSI1_DREQ);
	PRINTINTR("SCSI0 prefetch interrupt.\n", TCDS_CIR_SCSI0_PREFETCH);
	PRINTINTR("SCSI1 prefetch interrupt.\n", TCDS_CIR_SCSI1_PREFETCH);
	PRINTINTR("SCSI0 DMA error.\n", TCDS_CIR_SCSI0_DMA);
	PRINTINTR("SCSI1 DMA error.\n", TCDS_CIR_SCSI1_DMA);
	PRINTINTR("SCSI0 DB parity error.\n", TCDS_CIR_SCSI0_DB);
	PRINTINTR("SCSI1 DB parity error.\n", TCDS_CIR_SCSI1_DB);
	PRINTINTR("SCSI0 DMA buffer parity error.\n", TCDS_CIR_SCSI0_DMAB_PAR);
	PRINTINTR("SCSI1 DMA buffer parity error.\n", TCDS_CIR_SCSI1_DMAB_PAR);
	PRINTINTR("SCSI0 DMA read parity error.\n", TCDS_CIR_SCSI0_DMAR_PAR);
	PRINTINTR("SCSI1 DMA read parity error.\n", TCDS_CIR_SCSI1_DMAR_PAR);
	PRINTINTR("TC write parity error.\n", TCDS_CIR_TCIOW_PAR);
	PRINTINTR("TC I/O address parity error.\n", TCDS_CIR_TCIOA_PAR);
#undef PRINTINTR
#endif

	tc_mb();

	return (1);
}

void
tcds_params(struct tcds_softc *sc, int chip, int *idp, int *fastp)
{
	int id, fast;
	u_int32_t ids;

#ifdef __alpha__
	if (sc->sc_flags & TCDSF_BASEBOARD) {
		extern u_int8_t dec_3000_scsiid[], dec_3000_scsifast[];

		id = dec_3000_scsiid[chip];
		fast = dec_3000_scsifast[chip];
	} else
#endif /* __alpha__ */
	{
		/*
		 * SCSI IDs are stored in the EEPROM, along with whether or
		 * not the device is "fast".  Chip 0 is the high nibble,
		 * chip 1 the low nibble.
		 */
		ids = bus_space_read_4(sc->sc_bst, sc->sc_bsh, TCDS_EEPROM_IDS);
		if (chip == 0)
			ids >>= 4;

		id = ids & 0x7;
		fast = ids & 0x8;
	}

	if (id < 0 || id > 7) {
		printf("%s: WARNING: bad SCSI ID %d for chip %d, using 7\n",
		    sc->sc_dv.dv_xname, id, chip);
		id = 7;
	}

	if (fast)
		printf("%s: fast mode set for chip %d\n",
		    sc->sc_dv.dv_xname, chip);

	*idp = id;
	*fastp = fast;
}
