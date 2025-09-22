/*	$OpenBSD: power.c,v 1.9 2022/04/06 18:59:26 naddy Exp $	*/

/*
 * Copyright (c) 2007 Martin Reindl.
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
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/proc.h>
#include <sys/signalvar.h>

#include <machine/bus.h>
#include <machine/autoconf.h>

#include <sh/include/devreg.h>

#include <landisk/landisk/landiskreg.h>
#include <landisk/dev/obiovar.h>

struct power_softc {
	struct device		sc_dev;
	void			*sc_ih;
};

int	power_match(struct device *, void *, void *);
void	power_attach(struct device *, struct device *, void *);
int	power_intr(void *aux);

const struct cfattach power_ca = {
	sizeof(struct power_softc),
	power_match,
	power_attach
};

struct cfdriver power_cd = {
	NULL, "power", DV_DULL
};

struct power_softc *power_softc;

int
power_match(struct device *parent, void *match, void *aux)
{
	struct obio_attach_args *oa = aux;
	static struct obio_irq power_match_irq;

	oa->oa_nio = 0;
	oa->oa_niomem = 0;
	if (oa->oa_nirq == 0)
		oa->oa_irq = &power_match_irq;
	oa->oa_nirq = 1;
	oa->oa_irq[0].or_irq = LANDISK_INTR_PWRSW;

	return (1);
}

void
power_attach(struct device *parent, struct device *self, void *aux)
{
	struct power_softc *sc = (void *)self;

	power_softc = sc;

	sc->sc_ih = extintr_establish(LANDISK_INTR_PWRSW, IPL_TTY,
	    power_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": couldn't map interrupt\n");
		return;
	}

	printf("\n");
}

int
power_intr(void *arg)
{
	extern int allowpowerdown;
	int status;

	status = (int8_t)_reg_read_1(LANDISK_BTNSTAT);
	if (status == -1) {
		return (0);
	}

	status = ~status;
	if (status & BTN_POWER_BIT) {
#ifdef DEBUG
		printf("%s switched\n", sc->sc_dev.dv_xname);
		db_enter();
#endif
		_reg_write_1(LANDISK_PWRSW_INTCLR, 1);
		if (allowpowerdown == 1) {
			allowpowerdown = 0;
			prsignal(initprocess, SIGUSR1);
		}
		return (1);
	}
	return (0);
}
