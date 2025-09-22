/*	$OpenBSD: beep.c,v 1.12 2024/11/02 10:36:47 jsg Exp $	*/

/*
 * Copyright (c) 2006 Jason L. Wright (jason@thought.net)
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Driver for beeper device on BBC machines (Blade 1k, 2k, etc)
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/timeout.h>

#include <machine/bus.h>
#include <machine/autoconf.h>
#include <machine/openfirm.h>

#include <sparc64/dev/ebusreg.h>
#include <sparc64/dev/ebusvar.h>

#include "hidkbd.h"
#if NHIDKBD > 0
#include <dev/hid/hidkbdvar.h>
#endif

#define	BEEP_CTRL		0
#define	BEEP_CNT_0		2
#define	BEEP_CNT_1		3
#define	BEEP_CNT_2		4
#define	BEEP_CNT_3		5

#define	BEEP_CTRL_ON		0x01
#define	BEEP_CTRL_OFF		0x00

struct beep_freq {
	int freq;
	u_int32_t reg;
};

struct beep_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	int			sc_clk;
	struct beep_freq	sc_freqs[9];
	struct timeout		sc_to;
	int			sc_belltimeout;
	int			sc_bellactive;
};

int beep_match(struct device *, void *, void *);
void beep_attach(struct device *, struct device *, void *);
void beep_setfreq(struct beep_softc *, int);

const struct cfattach beep_ca = {
	sizeof(struct beep_softc), beep_match, beep_attach
};

struct cfdriver beep_cd = {
	NULL, "beep", DV_DULL
};

#if NHIDKBD > 0
void beep_stop(void *);
void beep_bell(void *, u_int, u_int, u_int, int);
#endif

int
beep_match(struct device *parent, void *match, void *aux)
{
	struct ebus_attach_args *ea = aux;

	if (strcmp(ea->ea_name, "beep") == 0)
		return (1);
	return (0);
}

void
beep_attach(struct device *parent, struct device *self, void *aux)
{
	struct beep_softc *sc = (void *)self;
	struct ebus_attach_args *ea = aux;
	int i;

	sc->sc_iot = ea->ea_memtag;

	/* Use prom address if available, otherwise map it. */
	if (ea->ea_nvaddrs) {
		if (bus_space_map(sc->sc_iot, ea->ea_vaddrs[0], 0,
		    BUS_SPACE_MAP_PROMADDRESS, &sc->sc_ioh)) {
			printf(": can't map PROM register space\n");
			return;
		}
	} else if (ebus_bus_map(sc->sc_iot, 0,
	    EBUS_PADDR_FROM_REG(&ea->ea_regs[0]), ea->ea_regs[0].size, 0, 0,
	    &sc->sc_ioh) != 0) {
		printf(": can't map register space\n");
                return;
	}

	/* The bbc,beep is clocked at half the BBC frequency */
	sc->sc_clk = getpropint(findroot(), "clock-frequency", 0);
	sc->sc_clk /= 2;

	/*
	 * Compute the frequence table based on the scalar and base
	 * board clock speed.
	 */
	for (i = 0; i < 9; i++) {
		sc->sc_freqs[i].reg = 1 << (18 - i);
		sc->sc_freqs[i].freq = sc->sc_clk / sc->sc_freqs[i].reg;
	}

	/* set beep at around 1200hz */
	beep_setfreq(sc, 1200);

#if 0
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, BEEP_CTRL,
	    BEEP_CTRL_ON);
	for (i = 0; i < 1000; i++)
		DELAY(1000);
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, BEEP_CTRL,
	    BEEP_CTRL_OFF);
#endif

	printf(": clock %sMHz\n", clockfreq(sc->sc_clk));

#if NHIDKBD > 0
	timeout_set(&sc->sc_to, beep_stop, sc);
	hidkbd_hookup_bell(beep_bell, sc);
#endif
}

void
beep_setfreq(struct beep_softc *sc, int freq)
{
	int i, n, selected = -1;

	n = sizeof(sc->sc_freqs)/sizeof(sc->sc_freqs[0]);

	if (freq < sc->sc_freqs[0].freq)
		selected = 0;
	if (freq > sc->sc_freqs[n - 1].freq)
		selected = n - 1;

	for (i = 1; selected == -1 && i < n; i++) {
		if (sc->sc_freqs[i].freq == freq)
			selected = i;
		else if (sc->sc_freqs[i].freq > freq) {
			int diff1, diff2;

			diff1 = freq - sc->sc_freqs[i - 1].freq;
			diff2 = sc->sc_freqs[i].freq - freq;
			if (diff1 < diff2)
				selected = i - 1;
			else
				selected = i;
		}
	}

	if (selected == -1)
		selected = 0;

	bus_space_write_1(sc->sc_iot, sc->sc_ioh, BEEP_CNT_0,
	    (sc->sc_freqs[selected].reg >> 24) & 0xff);
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, BEEP_CNT_1,
	    (sc->sc_freqs[selected].reg >> 16) & 0xff);
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, BEEP_CNT_2,
	    (sc->sc_freqs[selected].reg >>  8) & 0xff);
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, BEEP_CNT_3,
	    (sc->sc_freqs[selected].reg >>  0) & 0xff);
}

#if NHIDKBD > 0
void
beep_stop(void *vsc)
{
	struct beep_softc *sc = vsc;
	int s;

	s = spltty();
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, BEEP_CTRL,
	    BEEP_CTRL_OFF);
	sc->sc_bellactive = 0;
	sc->sc_belltimeout = 0;
	splx(s);
}

void
beep_bell(void *vsc, u_int pitch, u_int period, u_int volume, int poll)
{
	struct beep_softc *sc = vsc;
	int s;

	s = spltty();
	if (sc->sc_bellactive) {
		if (sc->sc_belltimeout == 0)
			timeout_del(&sc->sc_to);
	}
	if (pitch == 0 || period == 0 || volume == 0) {
		beep_stop(sc);
		splx(s);
		return;
	}
	if (!sc->sc_bellactive) {
		sc->sc_bellactive = 1;
		sc->sc_belltimeout = 1;
		bus_space_write_1(sc->sc_iot, sc->sc_ioh, BEEP_CTRL,
	    	    BEEP_CTRL_ON);
		timeout_add_msec(&sc->sc_to, period);
	}
	splx(s);
}
#endif /* NHIDKBD > 0 */
