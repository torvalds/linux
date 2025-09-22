/* $OpenBSD: mvspi.c,v 1.3 2021/10/31 15:12:00 kettenis Exp $ */
/*
 * Copyright (c) 2019 Patrick Wildt <patrick@blueri.se>
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
#include <sys/stdint.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/spi/spivar.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/fdt.h>

/* registers */
#define SPI_CTRL			0x00
#define  SPI_CTRL_XFER_READY			(1 << 1)
#define  SPI_CTRL_CS(x)				(1 << (16 + (x)))
#define SPI_CFG				0x04
#define  SPI_CFG_BYTE_LEN			(1 << 5)
#define  SPI_CFG_CPHA				(1 << 6)
#define  SPI_CFG_CPOL				(1 << 7)
#define  SPI_CFG_FIFO_FLUSH			(1 << 9)
#define  SPI_CFG_FIFO_ENABLE			(1 << 17)
#define  SPI_CFG_PRESCALE_MASK			0x1f
#define SPI_DOUT			0x08
#define SPI_DIN				0x0c

#define DEVNAME(sc)	((sc)->sc_dev.dv_xname)

struct mvspi_softc {
	struct device		 sc_dev;
	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_ioh;
	bus_size_t		 sc_ios;
	int			 sc_node;

	uint32_t		 sc_pfreq;

	struct rwlock		 sc_buslock;
	struct spi_controller	 sc_tag;

	int			 sc_cs;
	u_int			 sc_cs_delay;
};

int	 mvspi_match(struct device *, void *, void *);
void	 mvspi_attach(struct device *, struct device *, void *);
int	 mvspi_detach(struct device *, int);

void	 mvspi_config(void *, struct spi_config *);
uint32_t mvspi_clkdiv(struct mvspi_softc *, uint32_t);
int	 mvspi_transfer(void *, char *, char *, int, int);
int	 mvspi_acquire_bus(void *, int);
void	 mvspi_release_bus(void *, int);

void	 mvspi_set_cs(struct mvspi_softc *, int, int);
int	 mvspi_wait_state(struct mvspi_softc *, uint32_t, uint32_t);

void	 mvspi_scan(struct mvspi_softc *);

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

const struct cfattach mvspi_ca = {
	sizeof(struct mvspi_softc), mvspi_match, mvspi_attach, mvspi_detach
};

struct cfdriver mvspi_cd = {
	NULL, "mvspi", DV_DULL
};

int
mvspi_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "marvell,armada-3700-spi");
}

void
mvspi_attach(struct device *parent, struct device *self, void *aux)
{
	struct mvspi_softc *sc = (struct mvspi_softc *)self;
	struct fdt_attach_args *faa = aux;
	int timeout;

	if (faa->fa_nreg < 1)
		return;

	sc->sc_iot = faa->fa_iot;
	sc->sc_ios = faa->fa_reg[0].size;
	sc->sc_node = faa->fa_node;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	printf("\n");

	pinctrl_byname(sc->sc_node, "default");
	clock_enable(sc->sc_node, NULL);
	clock_set_assigned(sc->sc_node);

	sc->sc_pfreq = clock_get_frequency(sc->sc_node, NULL);

	/* drain input buffer */
	HSET4(sc, SPI_CFG, SPI_CFG_FIFO_FLUSH);
	for (timeout = 1000; timeout > 0; timeout--) {
		if ((HREAD4(sc, SPI_CFG) & SPI_CFG_FIFO_FLUSH) == 0)
			break;
		delay(10);
	}
	if (timeout == 0) {
		printf("%s: timeout", sc->sc_dev.dv_xname);
		return;
	}

	/* disable FIFO */
	HCLR4(sc, SPI_CFG, SPI_CFG_FIFO_ENABLE);
	HCLR4(sc, SPI_CFG, SPI_CFG_BYTE_LEN);

	rw_init(&sc->sc_buslock, sc->sc_dev.dv_xname);

	sc->sc_tag.sc_cookie = sc;
	sc->sc_tag.sc_config = mvspi_config;
	sc->sc_tag.sc_transfer = mvspi_transfer;
	sc->sc_tag.sc_acquire_bus = mvspi_acquire_bus;
	sc->sc_tag.sc_release_bus = mvspi_release_bus;

	mvspi_scan(sc);
}

int
mvspi_detach(struct device *self, int flags)
{
	struct mvspi_softc *sc = (struct mvspi_softc *)self;

	bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ios);
	return 0;
}

void
mvspi_config(void *cookie, struct spi_config *conf)
{
	struct mvspi_softc *sc = cookie;
	int cs;

	cs = conf->sc_cs;
	if (cs > 4) {
		printf("%s: invalid chip-select (%d)\n", DEVNAME(sc), cs);
		return;
	}
	sc->sc_cs = cs;
	sc->sc_cs_delay = conf->sc_cs_delay;

	HCLR4(sc, SPI_CFG, SPI_CFG_PRESCALE_MASK);
	HSET4(sc, SPI_CFG, mvspi_clkdiv(sc, conf->sc_freq));

	if (conf->sc_flags & SPI_CONFIG_CPHA)
		HSET4(sc, SPI_CFG, SPI_CFG_CPHA);
	if (conf->sc_flags & SPI_CONFIG_CPOL)
		HSET4(sc, SPI_CFG, SPI_CFG_CPOL);
}

uint32_t
mvspi_clkdiv(struct mvspi_softc *sc, uint32_t freq)
{
	uint32_t pre;

	pre = 0;
	while ((freq * pre) < sc->sc_pfreq)
		pre++;
	if (pre > 0x1f)
		pre = 0x1f;
	else if (pre > 0xf)
		pre = 0x10 + (pre + 1) / 2;

	return pre;
}

int
mvspi_wait_state(struct mvspi_softc *sc, uint32_t mask, uint32_t value)
{
	uint32_t state;
	int timeout;

	state = HREAD4(sc, SPI_CTRL);
	for (timeout = 1000; timeout > 0; timeout--) {
		if (((state = HREAD4(sc, SPI_CTRL)) & mask) == value)
			return 0;
		delay(10);
	}
	printf("%s: timeout mask %x value %x\n", __func__, mask, value);
	return ETIMEDOUT;
}

void
mvspi_set_cs(struct mvspi_softc *sc, int cs, int on)
{
	if (on)
		HSET4(sc, SPI_CTRL, SPI_CTRL_CS(cs));
	else
		HCLR4(sc, SPI_CTRL, SPI_CTRL_CS(cs));
}

int
mvspi_transfer(void *cookie, char *out, char *in, int len, int flags)
{
	struct mvspi_softc *sc = cookie;
	int i = 0;

	mvspi_set_cs(sc, sc->sc_cs, 1);
	delay(sc->sc_cs_delay);

	while (i < len) {
		if (mvspi_wait_state(sc, SPI_CTRL_XFER_READY,
		    SPI_CTRL_XFER_READY))
			goto err;
		if (out)
			HWRITE4(sc, SPI_DOUT, out[i]);
		else
			HWRITE4(sc, SPI_DOUT, 0x0);

		if (in) {
			if (mvspi_wait_state(sc, SPI_CTRL_XFER_READY,
			    SPI_CTRL_XFER_READY))
				goto err;
			in[i] = HREAD4(sc, SPI_DIN);
		}

		i++;
	}

	if (!ISSET(flags, SPI_KEEP_CS))
		mvspi_set_cs(sc, sc->sc_cs, 0);
	return 0;

err:
	mvspi_set_cs(sc, sc->sc_cs, 0);
	return ETIMEDOUT;
}

int
mvspi_acquire_bus(void *cookie, int flags)
{
	struct mvspi_softc *sc = cookie;

	rw_enter(&sc->sc_buslock, RW_WRITE);
	return 0;
}

void
mvspi_release_bus(void *cookie, int flags)
{
	struct mvspi_softc *sc = cookie;

	rw_exit(&sc->sc_buslock);
}

void
mvspi_scan(struct mvspi_softc *sc)
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
