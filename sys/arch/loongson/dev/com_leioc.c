/*	$OpenBSD: com_leioc.c,v 1.2 2017/02/19 10:18:41 visa Exp $	*/

/*
 * Copyright (c) 2016 Visa Hankala
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
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/termios.h>
#include <sys/tty.h>

#include <machine/autoconf.h>
#include <machine/bus.h>
#include <machine/loongson3.h>

#include <dev/ic/comreg.h>
#include <dev/ic/comvar.h>

#include <loongson/dev/leiocreg.h>
#include <loongson/dev/leiocvar.h>

#define UART_FREQ	33000000
#define UART_RATE	115200

int	com_leioc_match(struct device *, void *, void *);
void	com_leioc_attach(struct device *, struct device *, void *);
int	com_leioc_intr(void *);

const struct cfattach com_leioc_ca = {
	sizeof(struct com_softc), com_leioc_match, com_leioc_attach
};

extern struct cfdriver com_cd;

int
com_leioc_match(struct device *parent, void *match, void *aux)
{
	struct leioc_attach_args *laa = aux;

	if (strcmp(laa->laa_name, com_cd.cd_name) == 0)
		return 1;

	return 0;
}

void
com_leioc_attach(struct device *parent, struct device *self, void *aux)
{
	struct leioc_attach_args *laa = aux;
	struct com_softc *sc = (void *)self;
	int console = 0;

	if (comconsiot == &leioc_io_space_tag && comconsaddr == laa->laa_base)
		console = 1;

	sc->sc_hwflags = 0;
	sc->sc_swflags = 0;
	sc->sc_frequency = UART_FREQ;

	if (!console || comconsattached) {
		sc->sc_iot = &leioc_io_space_tag;
		sc->sc_iobase = laa->laa_base;
		if (bus_space_map(sc->sc_iot, sc->sc_iobase, COM_NPORTS, 0,
		    &sc->sc_ioh)) {
			printf(": could not map UART registers\n");
			return;
		}
	} else {
		/* Reuse the early console settings. */
		sc->sc_iot = comconsiot;
		sc->sc_iobase = comconsaddr;
		if (comcnattach(sc->sc_iot, sc->sc_iobase, comconsrate,
		    sc->sc_frequency, comconscflag))
			panic("could not set up serial console");
		sc->sc_ioh = comconsioh;
	}

	com_attach_subr(sc);

	loongson3_intr_establish(LS3_IRQ_LPC, IPL_TTY, com_leioc_intr, sc,
	    sc->sc_dev.dv_xname);
}

int
com_leioc_intr(void *arg)
{
	comintr(arg);

	/*
	 * Always return non-zero to prevent console clutter about spurious
	 * interrupts. comstart() enables the transmitter holding register
	 * empty interrupt before adding data to the FIFO, which can trigger
	 * a premature interrupt on the primary CPU in a multiprocessor system.
	 */
	return 1;
}

void
leioc_cons_setup(void)
{
	comconsiot = &leioc_io_space_tag;
	comconsaddr = LEIOC_UART0_BASE;
	comconsfreq = UART_FREQ;
	comconsrate = UART_RATE;
	comconscflag = (TTYDEF_CFLAG & ~(CSIZE | CSTOPB | PARENB)) |
	    CS8 | CLOCAL;
}
