/*	$OpenBSD: lasi.c,v 1.24 2025/06/28 13:24:21 miod Exp $	*/

/*
 * Copyright (c) 1998-2003 Michael Shalayeff
 * All rights reserved.
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

#undef LASIDEBUG

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/reboot.h>

#include <machine/bus.h>
#include <machine/iomod.h>
#include <machine/reg.h>
#include <machine/autoconf.h>

#include <hppa/dev/cpudevs.h>

#include <hppa/gsc/gscbusvar.h>

#define	LASI_IOMASK	0xfff00000

struct lasi_hwr {
	u_int32_t lasi_power;
#define	LASI_BLINK	0x01
#define	LASI_OFF	0x02
	u_int32_t lasi_error;
	u_int32_t lasi_version;
	u_int32_t lasi_reset;
	u_int32_t lasi_arbmask;
};

struct lasi_softc {
	struct device sc_dev;

	struct lasi_hwr volatile *sc_hw;
	struct gsc_attach_args ga;	/* for deferred attach */
};

int	lasimatch(struct device *, void *, void *);
void	lasiattach(struct device *, struct device *, void *);

const struct cfattach lasi_ca = {
	sizeof(struct lasi_softc), lasimatch, lasiattach
};

struct cfdriver lasi_cd = {
	NULL, "lasi", DV_DULL
};

void lasi_cold_hook(int on);
void lasi_gsc_attach(struct device *self);

int
lasimatch(struct device *parent, void *cfdata, void *aux)
{
	struct confargs *ca = aux;
	/* struct cfdata *cf = cfdata; */

	if (ca->ca_type.iodc_type != HPPA_TYPE_BHA ||
	    ca->ca_type.iodc_sv_model != HPPA_BHA_LASI)
		return 0;

	return 1;
}

void
lasiattach(struct device *parent, struct device *self, void *aux)
{
	struct lasi_softc *sc = (struct lasi_softc *)self;
	struct confargs *ca = aux;
	struct gscbus_ic *ic;
	bus_space_handle_t ioh, ioh2;
	int s;

	if (bus_space_map(ca->ca_iot, ca->ca_hpa,
	    IOMOD_HPASIZE, 0, &ioh)) {
		printf(": can't map TRS space\n");
		return;
	}

	if (bus_space_map(ca->ca_iot, ca->ca_hpa + 0xc000,
	    IOMOD_HPASIZE, 0, &ioh2)) {
		bus_space_unmap(ca->ca_iot, ioh, IOMOD_HPASIZE);
		printf(": can't map IO space\n");
		return;
	}

	sc->sc_hw = (struct lasi_hwr *)(ca->ca_hpa + 0xc000);
	ic = (struct gscbus_ic *)ca->ca_hpa;

	/* XXX should we reset the chip here? */

	printf(": rev %d.%d\n", (sc->sc_hw->lasi_version & 0xf0) >> 4,
	    sc->sc_hw->lasi_version & 0xf);

	/* interrupts guts */
	s = splhigh();
	ic->iar = 0; /* will be set up by gsc when attaching */
	ic->icr = 0;
	ic->imr = ~0U;
	(void)ic->irr;
	ic->imr = 0;
	splx(s);

#ifdef USELEDS
	/* figure out the leds address */
	switch (cpu_hvers) {
	case HPPA_BOARD_HP712_60:
	case HPPA_BOARD_HP712_80:
	case HPPA_BOARD_HP712_100:
	case HPPA_BOARD_HP743I_64:
	case HPPA_BOARD_HP743I_100:
	case HPPA_BOARD_HP712_120:
		break;	/* only has one led. works different */

	case HPPA_BOARD_HP715_64:
	case HPPA_BOARD_HP715_80:
	case HPPA_BOARD_HP715_100:
	case HPPA_BOARD_HP715_100XC:
	case HPPA_BOARD_HP725_100:
	case HPPA_BOARD_HP725_120:
		if (bus_space_map(ca->ca_iot, ca->ca_hpa - 0x20000,
		    4, 0, (bus_space_handle_t *)&machine_ledaddr))
			machine_ledaddr = NULL;
		machine_ledword = 1;
		break;

	case HPPA_BOARD_HP800_A180C:
	case HPPA_BOARD_HP778_B132L:
	case HPPA_BOARD_HP778_B132LP:
	case HPPA_BOARD_HP778_B160L:
	case HPPA_BOARD_HP778_B180L:
	case HPPA_BOARD_HP780_C100:
	case HPPA_BOARD_HP780_C110:
	case HPPA_BOARD_HP779_C132L:
	case HPPA_BOARD_HP779_C160L:
	case HPPA_BOARD_HP779_C180L:
	case HPPA_BOARD_HP779_C160L1:
		if (bus_space_map(ca->ca_iot, 0xf0190000,
		    4, 0, (bus_space_handle_t *)&machine_ledaddr))
			machine_ledaddr = NULL;
		machine_ledword = 1;
		break;

	default:
		machine_ledaddr = (u_int8_t *)sc->sc_hw;
		machine_ledword = 1;
		break;
	}
#endif

	sc->ga.ga_ca = *ca;	/* clone from us */
	if (!strcmp(parent->dv_xname, "mainbus0")) {
		sc->ga.ga_dp.dp_bc[0] = sc->ga.ga_dp.dp_bc[1];
		sc->ga.ga_dp.dp_bc[1] = sc->ga.ga_dp.dp_bc[2];
		sc->ga.ga_dp.dp_bc[2] = sc->ga.ga_dp.dp_bc[3];
		sc->ga.ga_dp.dp_bc[3] = sc->ga.ga_dp.dp_bc[4];
		sc->ga.ga_dp.dp_bc[4] = sc->ga.ga_dp.dp_bc[5];
		sc->ga.ga_dp.dp_bc[5] = sc->ga.ga_dp.dp_mod;
		sc->ga.ga_dp.dp_mod = 0;
	}
	sc->ga.ga_name = "gsc";
	sc->ga.ga_hpamask = LASI_IOMASK;
	sc->ga.ga_parent = gsc_lasi;
	sc->ga.ga_ic = ic;
	if (sc->sc_dev.dv_unit)
		config_defer(self, lasi_gsc_attach);
	else {
		extern void (*cold_hook)(int);

		lasi_gsc_attach(self);
		/* could be already set by power(4) */
		if (!cold_hook)
			cold_hook = lasi_cold_hook;
	}
}

void
lasi_gsc_attach(struct device *self)
{
	struct lasi_softc *sc = (struct lasi_softc *)self;

	config_found(self, &sc->ga, gscprint);
}

void
lasi_cold_hook(int on)
{
	struct lasi_softc *sc = lasi_cd.cd_devs[0];

	if (!sc)
		return;

	switch (on) {
	case HPPA_COLD_COLD:
		sc->sc_hw->lasi_power = LASI_BLINK;
		break;
	case HPPA_COLD_HOT:
		sc->sc_hw->lasi_power = 0;
		break;
	case HPPA_COLD_OFF:
		sc->sc_hw->lasi_power = LASI_OFF;
		break;
	}
}
