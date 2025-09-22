/*	$OpenBSD: wdt.c,v 1.27 2024/05/24 06:02:58 jsg Exp $	*/

/*-
 * Copyright (c) 1998,1999 Alex Nash
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <machine/bus.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>

struct wdt_softc {
	/* wdt_dev must be the first item in the struct */
	struct device		sc_dev;

	/* feature set: 0 = none   1 = temp, buzzer, etc. */
	int			sc_features;

	/* device access through bus space */
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
};

int	wdt_probe(struct device *, void *, void *);
void	wdt_attach(struct device *, struct device *, void *);
int	wdt_activate(struct device *, int);

int	wdt_is501(struct wdt_softc *);
void	wdt_8254_count(struct wdt_softc *, int, u_int16_t);
void	wdt_8254_mode(struct wdt_softc *, int, int);
int	wdt_set_timeout(void *, int);
void	wdt_init_timer(struct wdt_softc *);
void	wdt_buzzer_off(struct wdt_softc *);
void	wdt_timer_disable(struct wdt_softc *);
void	wdt_buzzer_enable(struct wdt_softc *);

const struct cfattach wdt_ca = {
	sizeof(struct wdt_softc), wdt_probe, wdt_attach,
	NULL, wdt_activate
};

struct cfdriver wdt_cd = {
	NULL, "wdt", DV_DULL
};

const struct pci_matchid wdt_devices[] = {
	{ PCI_VENDOR_INDCOMPSRC, PCI_PRODUCT_INDCOMPSRC_WDT50X }
};

/*
 *	8254 counter mappings
 */
#define WDT_8254_TC_LO		0	/* low 16 bits of timeout counter  */
#define	WDT_8254_TC_HI		1	/* high 16 bits of timeout counter */
#define WDT_8254_BUZZER		2

/*
 *	WDT500/501 ports
 */
#define WDT_8254_BASE		0
#define WDT_8254_CTL		(WDT_8254_BASE + 3)
#define WDT_DISABLE_TIMER	7
#define WDT_ENABLE_TIMER	7

/*
 *	WDT501 specific ports
 */
#define WDT_STATUS_REG		4
#define WDT_START_BUZZER	4
#define WDT_TEMPERATURE		5
#define WDT_STOP_BUZZER		5

int
wdt_probe(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, wdt_devices,
	    nitems(wdt_devices)));
}

void
wdt_attach(struct device *parent, struct device *self, void *aux)
{
	struct wdt_softc *wdt = (struct wdt_softc *)self;
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;
	bus_size_t iosize;

	/* retrieve the I/O region (BAR2) */
	if (pci_mapreg_map(pa, 0x18, PCI_MAPREG_TYPE_IO, 0,
	    &wdt->sc_iot, &wdt->sc_ioh, NULL, &iosize, 0) != 0) {
		printf("%s: couldn't find PCI I/O region\n",
		    wdt->sc_dev.dv_xname);
		return;
	}

	/* sanity check I/O size */
	if (iosize != (bus_size_t)16) {
		printf("%s: invalid I/O region size\n",
		    wdt->sc_dev.dv_xname);
		return;
	}

	/* initialize the watchdog timer structure */

	/* check the feature set available */
	if (wdt_is501(wdt))
		wdt->sc_features = 1;
	else
		wdt->sc_features = 0;

	if (wdt->sc_features) {
		/*
		 * turn off the buzzer, it may have been activated
		 * by a previous timeout
		 */
		wdt_buzzer_off(wdt);

		wdt_buzzer_enable(wdt);
	}

	/* initialize the timer modes and the lower 16-bit counter */
	wdt_init_timer(wdt);

	/*
	 * ensure that the watchdog is disabled
	 */
	wdt_timer_disable(wdt);

	/*
	 * register with the watchdog framework
	 */
	wdog_register(wdt_set_timeout, wdt);
}

int
wdt_activate(struct device *self, int act)
{
	switch (act) {
	case DVACT_POWERDOWN:
		wdog_shutdown(self);
		break;
	}

	return (0);
}

/*
 *	wdt_is501
 *
 *	Returns non-zero if the card is a 501 model.
 */
int
wdt_is501(struct wdt_softc *wdt)
{
	/*
	 *	It makes too much sense to detect the card type
	 *	by the device ID, so we have to resort to testing
	 *	the presence of a register to determine the type.
	 */
	int v = bus_space_read_1(wdt->sc_iot, wdt->sc_ioh, WDT_TEMPERATURE);

	/* XXX may not be reliable */
	if (v == 0 || v == 0xFF)
		return(0);

	return(1);
}

/*
 *	wdt_8254_count
 *
 *	Loads the specified counter with the 16-bit value 'v'.
 */
void
wdt_8254_count(struct wdt_softc *wdt, int counter, u_int16_t v)
{
	bus_space_write_1(wdt->sc_iot, wdt->sc_ioh,
			WDT_8254_BASE + counter, v & 0xFF);
	bus_space_write_1(wdt->sc_iot, wdt->sc_ioh, WDT_8254_BASE + counter, v >> 8);
}

/*
 *	wdt_8254_mode
 *
 *	Sets the mode of the specified counter.
 */
void
wdt_8254_mode(struct wdt_softc *wdt, int counter, int mode)
{
	bus_space_write_1(wdt->sc_iot, wdt->sc_ioh, WDT_8254_CTL,
		(counter << 6) | 0x30 | (mode << 1));
}

/*
 *	wdt_set_timeout
 *
 *	Load the watchdog timer with the specified number of seconds.
 *	Clamp seconds to be in the interval [2; 1800].
 */
int
wdt_set_timeout(void *self, int seconds)
{
	struct wdt_softc *wdt = (struct wdt_softc *)self;

	u_int16_t v;
	int s;

	s = splclock();

	wdt_timer_disable(wdt);

	if (seconds == 0) {
		splx(s);
		return (0);
	} else if (seconds < 2)
		seconds = 2;
	else if (seconds > 1800)
		seconds = 1800;

	/* 8254 has been programmed with a 2ms period */
	v = (u_int16_t)seconds * 50;

	/* load the new timeout count */
	wdt_8254_count(wdt, WDT_8254_TC_HI, v);

	/* enable the timer */
	bus_space_write_1(wdt->sc_iot, wdt->sc_ioh, WDT_ENABLE_TIMER, 0);

	splx(s);

	return (seconds);
}

/*
 *	wdt_timer_disable
 *
 *	Disables the watchdog timer and cancels the scheduled (if any)
 *	kernel timeout.
 */
void
wdt_timer_disable(struct wdt_softc *wdt)
{
	(void)bus_space_read_1(wdt->sc_iot, wdt->sc_ioh, WDT_DISABLE_TIMER);
}

/*
 *	wdt_init_timer
 *
 *	Configure the modes for the watchdog counters and initialize
 *	the low 16-bits of the watchdog counter to have a period of
 *	approximately 1/50th of a second.
 */
void
wdt_init_timer(struct wdt_softc *wdt)
{
	wdt_8254_mode(wdt, WDT_8254_TC_LO, 3);
	wdt_8254_mode(wdt, WDT_8254_TC_HI, 2);
	wdt_8254_count(wdt, WDT_8254_TC_LO, 41666);
}

/*******************************************************************
 *	WDT501 specific functions
 *******************************************************************/

/*
 *	wdt_buzzer_off
 *
 *	Turns the buzzer off.
 */
void
wdt_buzzer_off(struct wdt_softc *wdt)
{
	bus_space_write_1(wdt->sc_iot, wdt->sc_ioh, WDT_STOP_BUZZER, 0);
}

#ifndef WDT_DISABLE_BUZZER
/*
 *	wdt_buzzer_enable
 *
 *	Enables the buzzer when the watchdog counter expires.
 */
void
wdt_buzzer_enable(struct wdt_softc *wdt)
{
	bus_space_write_1(wdt->sc_iot, wdt->sc_ioh, WDT_8254_BUZZER, 1);
	wdt_8254_mode(wdt, WDT_8254_BUZZER, 1);
}
#else
/*
 *	wdt_buzzer_disable
 *
 *	Disables the buzzer from sounding when the watchdog counter
 *	expires.
 */
void
wdt_buzzer_disable(struct wdt_softc *wdt)
{
	wdt_8254_mode(wdt, WDT_8254_BUZZER, 0);
}
#endif
