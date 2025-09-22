/*	$OpenBSD: aplspi.c,v 1.6 2024/05/13 01:15:50 jsg Exp $	*/
/*
 * Copyright (c) 2021 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/malloc.h>
#include <sys/mutex.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/spi/spivar.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/ofw_power.h>
#include <dev/ofw/fdt.h>

#define SPI_CLKCFG		0x00
#define  SPI_CLKCFG_EN		0xd
#define SPI_CONFIG		0x04
#define  SPI_CONFIG_EN		(1 << 18)
#define  SPI_CONFIG_PIOEN	(1 << 5)
#define SPI_STATUS		0x08
#define SPI_PIN			0x0c
#define  SPI_PIN_CS		(1 << 1)
#define SPI_TXDATA		0x10
#define SPI_RXDATA		0x20
#define SPI_CLKDIV		0x30
#define  SPI_CLKDIV_MIN		2
#define  SPI_CLKDIV_MAX		2047
#define SPI_RXCNT		0x34
#define SPI_CLKIDLE		0x38
#define SPI_TXCNT		0x4c
#define SPI_AVAIL		0x10c
#define  SPI_AVAIL_TX(avail)	((avail >> 8) & 0xff)
#define  SPI_AVAIL_RX(avail)	((avail >> 24) & 0xff)
#define SPI_SHIFTCFG		0x150
#define  SPI_SHIFTCFG_OVERRIDE_CS	(1 << 24)
#define SPI_PINCFG		0x154
#define  SPI_PINCFG_KEEP_CS	(1 << 1)
#define  SPI_PINCFG_CS_IDLE_VAL	(1 << 9)

#define SPI_FIFO_SIZE		16

#define DEVNAME(sc)	((sc)->sc_dev.dv_xname)

struct aplspi_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	int			sc_node;

	uint32_t		sc_pfreq;

	struct spi_controller	sc_tag;
	struct mutex		sc_mtx;

	int			sc_cs;
	uint32_t		*sc_csgpio;
	int			sc_csgpiolen;
	u_int			sc_cs_delay;
};

int	 aplspi_match(struct device *, void *, void *);
void	 aplspi_attach(struct device *, struct device *, void *);

void	 aplspi_config(void *, struct spi_config *);
uint32_t aplspi_clkdiv(struct aplspi_softc *, uint32_t);
int	 aplspi_transfer(void *, char *, char *, int, int);
int	 aplspi_acquire_bus(void *, int);
void	 aplspi_release_bus(void *, int);

void	 aplspi_set_cs(struct aplspi_softc *, int, int);

void	 aplspi_scan(struct aplspi_softc *);

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

const struct cfattach aplspi_ca = {
	sizeof(struct aplspi_softc), aplspi_match, aplspi_attach
};

struct cfdriver aplspi_cd = {
	NULL, "aplspi", DV_DULL
};

int
aplspi_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "apple,spi");
}

void
aplspi_attach(struct device *parent, struct device *self, void *aux)
{
	struct aplspi_softc *sc = (struct aplspi_softc *)self;
	struct fdt_attach_args *faa = aux;

	if (faa->fa_nreg < 1)
		return;

	sc->sc_iot = faa->fa_iot;
	sc->sc_node = faa->fa_node;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	sc->sc_csgpiolen = OF_getproplen(faa->fa_node, "cs-gpios");
	if (sc->sc_csgpiolen > 0) {
		sc->sc_csgpio = malloc(sc->sc_csgpiolen, M_DEVBUF, M_WAITOK);
		OF_getpropintarray(faa->fa_node, "cs-gpios",
		    sc->sc_csgpio, sc->sc_csgpiolen);
		gpio_controller_config_pin(sc->sc_csgpio, GPIO_CONFIG_OUTPUT);
		gpio_controller_set_pin(sc->sc_csgpio, 0);
	}

	printf("\n");

	sc->sc_pfreq = clock_get_frequency(sc->sc_node, NULL);

	power_domain_enable(sc->sc_node);
	pinctrl_byname(sc->sc_node, "default");

	/* Configure CS# pin for manual control. */
	HWRITE4(sc, SPI_PIN, SPI_PIN_CS);
	HCLR4(sc, SPI_SHIFTCFG, SPI_SHIFTCFG_OVERRIDE_CS);
	HCLR4(sc, SPI_PINCFG, SPI_PINCFG_CS_IDLE_VAL);
	HSET4(sc, SPI_PINCFG, SPI_PINCFG_KEEP_CS);

	sc->sc_tag.sc_cookie = sc;
	sc->sc_tag.sc_config = aplspi_config;
	sc->sc_tag.sc_transfer = aplspi_transfer;
	sc->sc_tag.sc_acquire_bus = aplspi_acquire_bus;
	sc->sc_tag.sc_release_bus = aplspi_release_bus;

	mtx_init(&sc->sc_mtx, IPL_TTY);

	aplspi_scan(sc);
}

void
aplspi_config(void *cookie, struct spi_config *conf)
{
	struct aplspi_softc *sc = cookie;
	int cs;

	cs = conf->sc_cs;
	if (cs > 4) {
		printf("%s: invalid chip-select (%d)\n", DEVNAME(sc), cs);
		return;
	}
	sc->sc_cs = cs;
	sc->sc_cs_delay = conf->sc_cs_delay;

	HWRITE4(sc, SPI_CLKCFG, 0);

	HWRITE4(sc, SPI_CLKDIV, aplspi_clkdiv(sc, conf->sc_freq));
	HWRITE4(sc, SPI_CLKIDLE, 0);

	HWRITE4(sc, SPI_CONFIG, SPI_CONFIG_EN);
	HWRITE4(sc, SPI_CLKCFG, SPI_CLKCFG_EN);
	HREAD4(sc, SPI_CONFIG);
}

uint32_t
aplspi_clkdiv(struct aplspi_softc *sc, uint32_t freq)
{
	uint32_t div = 0;

	while ((freq * div) < sc->sc_pfreq)
		div++;
	if (div < SPI_CLKDIV_MIN)
		div = SPI_CLKDIV_MIN;
	if (div > SPI_CLKDIV_MAX)
		div = SPI_CLKDIV_MAX;

	return div << 1;
}

void
aplspi_set_cs(struct aplspi_softc *sc, int cs, int on)
{
	if (cs == 0) {
		if (sc->sc_csgpio)
			gpio_controller_set_pin(sc->sc_csgpio, on);
		else
			HWRITE4(sc, SPI_PIN, on ? 0 : SPI_PIN_CS);
	}
}

int
aplspi_transfer(void *cookie, char *out, char *in, int len, int flags)
{
	struct aplspi_softc *sc = cookie;
	uint32_t avail, data, status;
	int rsplen;
	int count;

	aplspi_set_cs(sc, sc->sc_cs, 1);
	delay(sc->sc_cs_delay);

	HWRITE4(sc, SPI_TXCNT, len);
	HWRITE4(sc, SPI_RXCNT, len);
	HWRITE4(sc, SPI_CONFIG, SPI_CONFIG_EN | SPI_CONFIG_PIOEN);

	rsplen = len;
	while (len > 0 || rsplen > 0) {
		avail = HREAD4(sc, SPI_AVAIL);
		count = SPI_AVAIL_RX(avail);
		while (rsplen > 0 && count > 0) {
			data = HREAD4(sc, SPI_RXDATA);
			if (in)
				*in++ = data;
			rsplen--;

			avail = HREAD4(sc, SPI_AVAIL);
			count = SPI_AVAIL_RX(avail);
		}

		count = SPI_FIFO_SIZE - SPI_AVAIL_TX(avail);
		while (len > 0 && count > 0) {
			data = out ? *out++ : 0;
			HWRITE4(sc, SPI_TXDATA, data);
			len--;
			count--;
		}
	}

	HWRITE4(sc, SPI_CONFIG, SPI_CONFIG_EN);
	status = HREAD4(sc, SPI_STATUS);
	HWRITE4(sc, SPI_STATUS, status);

	if (!ISSET(flags, SPI_KEEP_CS))
		aplspi_set_cs(sc, sc->sc_cs, 0);

	return 0;
}

int
aplspi_acquire_bus(void *cookie, int flags)
{
	struct aplspi_softc *sc = cookie;

	mtx_enter(&sc->sc_mtx);
	return 0;
}

void
aplspi_release_bus(void *cookie, int flags)
{
	struct aplspi_softc *sc = cookie;

	mtx_leave(&sc->sc_mtx);
}

void
aplspi_scan(struct aplspi_softc *sc)
{
	struct spi_attach_args sa;
	uint32_t reg[1];
	char name[32];
	int node;

	for (node = OF_child(sc->sc_node); node; node = OF_peer(node)) {
		memset(name, 0, sizeof(name));
		memset(reg, 0, sizeof(reg));

		if (OF_getprop(node, "compatible", name, sizeof(name)) == -1)
			continue;
		if (name[0] == '\0')
			continue;

		if (OF_getprop(node, "reg", &reg, sizeof(reg)) != sizeof(reg))
			continue;

		memset(&sa, 0, sizeof(sa));
		sa.sa_tag = &sc->sc_tag;
		sa.sa_name = name;
		sa.sa_cookie = &node;

		config_found(&sc->sc_dev, &sa, NULL);
	}
}
