/*	$OpenBSD: berkwdt.c,v 1.12 2024/05/24 06:02:53 jsg Exp $ */

/*
 * Copyright (c) 2009 Wim Van Sebroeck <wim@iguana.be>
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

/*
 * Berkshire PCI-PC Watchdog Card Driver
 * http://www.pcwatchdog.com/
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <machine/bus.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>

struct berkwdt_softc {
	struct device	sc_dev;

	/* device access through bus space */
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	/* the timeout period */
	int		sc_period;
};

int berkwdt_match(struct device *, void *, void *);
void berkwdt_attach(struct device *, struct device *, void *);
int berkwdt_activate(struct device *, int);

void berkwdt_start(struct berkwdt_softc *sc);
void berkwdt_stop(struct berkwdt_softc *sc);
void berkwdt_reload(struct berkwdt_softc *sc);
int berkwdt_send_command(struct berkwdt_softc *sc, u_int8_t cmd, int *val);

int berkwdt_set_timeout(void *, int);

const struct cfattach berkwdt_ca = {
	sizeof(struct berkwdt_softc), berkwdt_match, berkwdt_attach,
	NULL, berkwdt_activate
};

struct cfdriver berkwdt_cd = {
	NULL, "berkwdt", DV_DULL
};

const struct pci_matchid berkwdt_devices[] = {
	{ PCI_VENDOR_PIJNENBURG, PCI_PRODUCT_PIJNENBURG_PCWD_PCI }
};

/* PCWD-PCI I/O Port definitions */
#define PCWD_PCI_RELOAD		0x00	/* Re-trigger */
#define PCWD_PCI_CS1		0x01	/* Control Status 1 */
#define PCWD_PCI_CS2		0x02	/* Control Status 2 */
#define PCWD_PCI_WDT_DIS	0x03	/* Watchdog Disable */
#define PCWD_PCI_LSB		0x04	/* Command / Response */
#define PCWD_PCI_MSB		0x05	/* Command/Response LSB */
#define PCWD_PCI_CMD		0x06	/* Command/Response MSB */

/* Port 1 : Control Status #1 */
#define WD_PCI_WTRP		0x01	/* Watchdog Trip status */
#define WD_PCI_TTRP		0x04	/* Temperature Trip status */
#define WD_PCI_R2DS		0x40	/* Relay 2 Disable Temp-trip reset */

/* Port 2 : Control Status #2 */
#define WD_PCI_WDIS		0x10	/* Watchdog Disable */
#define WD_PCI_WRSP		0x40	/* Watchdog wrote response */

/*
 * According to documentation max. time to process a command for the pci
 * watchdog card is 100 ms, so we give it 150 ms to do its job.
 */
#define PCI_CMD_TIMEOUT		150

/* Watchdog's internal commands */
#define CMD_WRITE_WD_TIMEOUT	0x19

int
berkwdt_send_command(struct berkwdt_softc *sc, u_int8_t cmd, int *val)
{
	u_int8_t msb;
	u_int8_t lsb;
	u_int8_t got_response;
	int count;

	msb = *val / 256;
	lsb = *val % 256;

	/* Send command with data (data first!) */
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, PCWD_PCI_LSB, lsb);
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, PCWD_PCI_MSB, msb);
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, PCWD_PCI_CMD, cmd);

	got_response = bus_space_read_1(sc->sc_iot, sc->sc_ioh, PCWD_PCI_CS2);
	got_response &= WD_PCI_WRSP;
	for (count = 0; count < PCI_CMD_TIMEOUT && !got_response; count++) {
		delay(1000);
		got_response = bus_space_read_1(sc->sc_iot, sc->sc_ioh, PCWD_PCI_CS2);
		got_response &= WD_PCI_WRSP;
	}

	if (got_response) {
		/* read back response */
		lsb = bus_space_read_1(sc->sc_iot, sc->sc_ioh, PCWD_PCI_LSB);
		msb = bus_space_read_1(sc->sc_iot, sc->sc_ioh, PCWD_PCI_MSB);
		*val = (msb << 8) + lsb;

		/* clear WRSP bit */
		bus_space_read_1(sc->sc_iot, sc->sc_ioh, PCWD_PCI_CMD);
		return 1;
	}

	return 0;
}

void
berkwdt_start(struct berkwdt_softc *sc)
{
	u_int8_t reg;

	bus_space_write_1(sc->sc_iot, sc->sc_ioh, PCWD_PCI_WDT_DIS, 0x00);
	delay(1000);

	reg = bus_space_read_1(sc->sc_iot, sc->sc_ioh, PCWD_PCI_CS2);
	if (reg & WD_PCI_WDIS) {
		printf("%s: unable to enable\n", sc->sc_dev.dv_xname);
	}
}

void
berkwdt_stop(struct berkwdt_softc *sc)
{
	u_int8_t reg;

	bus_space_write_1(sc->sc_iot, sc->sc_ioh, PCWD_PCI_WDT_DIS, 0xa5);
	delay(1000);
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, PCWD_PCI_WDT_DIS, 0xa5);
	delay(1000);

	reg = bus_space_read_1(sc->sc_iot, sc->sc_ioh, PCWD_PCI_CS2);
	if (!(reg & WD_PCI_WDIS)) {
		printf("%s: unable to disable\n", sc->sc_dev.dv_xname);
	}
}

void
berkwdt_reload(struct berkwdt_softc *sc)
{
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, PCWD_PCI_RELOAD, 0x42);
}

int
berkwdt_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, berkwdt_devices,
	    sizeof(berkwdt_devices) / sizeof(berkwdt_devices[0])));
}

void
berkwdt_attach(struct device *parent, struct device *self, void *aux)
{
	struct berkwdt_softc *sc = (struct berkwdt_softc *)self;
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;
	bus_size_t iosize;
	u_int8_t reg;

	/* retrieve the I/O region (BAR 0) */
	if (pci_mapreg_map(pa, 0x10, PCI_MAPREG_TYPE_IO, 0,
	    &sc->sc_iot, &sc->sc_ioh, NULL, &iosize, 0) != 0) {
		printf(": couldn't find PCI I/O region\n");
		return;
	}

	/* Check for reboot by the card */
	reg = bus_space_read_1(sc->sc_iot, sc->sc_ioh, PCWD_PCI_CS1);
	if (reg & WD_PCI_WTRP) {
		printf(", warning: watchdog triggered");

		if (reg & WD_PCI_TTRP)
			printf(", overheat detected");

		/* clear trip status & LED and keep mode of relay 2 */
		reg &= WD_PCI_R2DS;
		reg |= WD_PCI_WTRP;
		bus_space_write_1(sc->sc_iot, sc->sc_ioh, PCWD_PCI_CS1, reg);
	}

	printf("\n");

	/* ensure that the watchdog is disabled */
	berkwdt_stop(sc);
	sc->sc_period = 0;

	/* register with the watchdog framework */
	wdog_register(berkwdt_set_timeout, sc);
}

int
berkwdt_activate(struct device *self, int act)
{
	switch (act) {
	case DVACT_POWERDOWN:
		wdog_shutdown(self);
		break;
	}

	return (0);
}

int
berkwdt_set_timeout(void *self, int timeout)
{
	struct berkwdt_softc *sc = self;
	int new_timeout = timeout;

	if (timeout == 0) {
		/* Disable watchdog */
		berkwdt_stop(sc);
	} else {
		if (sc->sc_period != timeout) {
			/* Set new timeout */
			berkwdt_send_command(sc, CMD_WRITE_WD_TIMEOUT,
			    &new_timeout);
		}
		if (sc->sc_period == 0) {
			/* Enable watchdog */
			berkwdt_start(sc);
			berkwdt_reload(sc);
		} else {
			/* Reset timer */
			berkwdt_reload(sc);
		}
	}
	sc->sc_period = timeout;

	return (timeout);
}

