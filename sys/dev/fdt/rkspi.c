/* $OpenBSD: rkspi.c,v 1.2 2024/05/13 01:15:50 jsg Exp $ */
/*
 * Copyright (c) 2018,2023 Patrick Wildt <patrick@blueri.se>
 * Copyright (c) 2024 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/device.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/fdt.h>
#include <dev/spi/spivar.h>

/* registers */
#define SPI_CTRLR0			0x0000
#define  SPI_CTRLR0_DFS_4BIT			(0x0 << 0)
#define  SPI_CTRLR0_DFS_8BIT			(0x1 << 0)
#define  SPI_CTRLR0_DFS_16BIT			(0x2 << 0)
#define  SPI_CTRLR0_SCPH			(0x1 << 6)
#define  SPI_CTRLR0_SCPOL			(0x1 << 7)
#define  SPI_CTRLR0_CSM_KEEP			(0x0 << 8)
#define  SPI_CTRLR0_CSM_HALF			(0x1 << 8)
#define  SPI_CTRLR0_CSM_ONE			(0x2 << 8)
#define  SPI_CTRLR0_SSD_HALF			(0x0 << 10)
#define  SPI_CTRLR0_SSD_ONE			(0x1 << 10)
#define  SPI_CTRLR0_EM_LITTLE			(0x0 << 11)
#define  SPI_CTRLR0_EM_BIG			(0x1 << 11)
#define  SPI_CTRLR0_FBM_MSB			(0x0 << 12)
#define  SPI_CTRLR0_FBM_LSB			(0x1 << 12)
#define  SPI_CTRLR0_BHT_16BIT			(0x0 << 13)
#define  SPI_CTRLR0_BHT_8BIT			(0x1 << 13)
#define  SPI_CTRLR0_RSD(x)			((x) << 14)
#define  SPI_CTRLR0_FRF_SPI			(0x0 << 16)
#define  SPI_CTRLR0_FRF_SSP			(0x1 << 16)
#define  SPI_CTRLR0_FRF_MICROWIRE		(0x2 << 16)
#define  SPI_CTRLR0_XFM_TR			(0x0 << 18)
#define  SPI_CTRLR0_XFM_TO			(0x1 << 18)
#define  SPI_CTRLR0_XFM_RO			(0x2 << 18)
#define  SPI_CTRLR0_SOI(x)			((1 << (x)) << 23)
#define SPI_CTRLR1			0x0004
#define SPI_ENR				0x0008
#define SPI_SER				0x000c
#define  SPI_SER_CS(x)				((1 << (x)) << 0)
#define SPI_BAUDR			0x0010
#define SPI_TXFTLR			0x0014
#define SPI_RXFTLR			0x0018
#define SPI_TXFLR			0x001c
#define SPI_RXFLR			0x0020
#define SPI_SR				0x0024
#define  SPI_SR_BSF				(1 << 0)
#define  SPI_SR_TFF				(1 << 1)
#define  SPI_SR_TFE				(1 << 2)
#define  SPI_SR_RFE				(1 << 3)
#define  SPI_SR_RFF				(1 << 4)
#define SPI_IPR				0x0028
#define SPI_IMR				0x002c
#define SPI_ISR				0x0030
#define SPI_RISR			0x0034
#define SPI_ICR				0x0038
#define  SPI_ICR_MASK				(0x7f << 0)
#define SPI_DMACR			0x003c
#define SPI_DMATDLR			0x0040
#define SPI_DMARDLR			0x0044
#define SPI_VERSION			0x0048
#define SPI_TXDR			0x0400
#define SPI_RXDR			0x0800

#define DEVNAME(sc)	((sc)->sc_dev.dv_xname)

struct rkspi_softc {
	struct device		 sc_dev;
	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_ioh;
	bus_size_t		 sc_ios;
	int			 sc_node;

	struct rwlock		 sc_buslock;
	struct spi_controller	 sc_tag;

	int			 sc_ridx;
	int			 sc_widx;
	int			 sc_cs;
	u_int			 sc_cs_delay;
	u_int			 sc_spi_freq;
};

int	 rkspi_match(struct device *, void *, void *);
void	 rkspi_attach(struct device *, struct device *, void *);
int	 rkspi_detach(struct device *, int);

void	 rkspi_config(void *, struct spi_config *);
int	 rkspi_transfer(void *, char *, char *, int, int);
int	 rkspi_acquire_bus(void *, int);
void	 rkspi_release_bus(void *, int);

int	 rkspi_wait_state(struct rkspi_softc *, uint32_t, uint32_t);

void	 rkspi_scan(struct rkspi_softc *);

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

const struct cfattach rkspi_ca = {
	sizeof(struct rkspi_softc), rkspi_match, rkspi_attach,
	rkspi_detach
};

struct cfdriver rkspi_cd = {
	NULL, "rkspi", DV_DULL
};

int
rkspi_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "rockchip,rk3066-spi");
}

void
rkspi_attach(struct device *parent, struct device *self, void *aux)
{
	struct rkspi_softc *sc = (struct rkspi_softc *)self;
	struct fdt_attach_args *faa = aux;

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

	pinctrl_byname(sc->sc_node, "default");
	clock_set_assigned(sc->sc_node);
	clock_enable(sc->sc_node, "apb_pclk");
	clock_enable(sc->sc_node, "spiclk");

	sc->sc_spi_freq = clock_get_frequency(sc->sc_node, "spiclk");

	printf("\n");

	HWRITE4(sc, SPI_ENR, 0);
	HWRITE4(sc, SPI_DMACR, 0);
	HWRITE4(sc, SPI_DMATDLR, 0);
	HWRITE4(sc, SPI_DMARDLR, 0);
	HWRITE4(sc, SPI_IPR, 0);
	HWRITE4(sc, SPI_IMR, 0);
	HWRITE4(sc, SPI_ICR, SPI_ICR_MASK);
	
	rw_init(&sc->sc_buslock, sc->sc_dev.dv_xname);

	sc->sc_tag.sc_cookie = sc;
	sc->sc_tag.sc_config = rkspi_config;
	sc->sc_tag.sc_transfer = rkspi_transfer;
	sc->sc_tag.sc_acquire_bus = rkspi_acquire_bus;
	sc->sc_tag.sc_release_bus = rkspi_release_bus;

	rkspi_scan(sc);
}

int
rkspi_detach(struct device *self, int flags)
{
	struct rkspi_softc *sc = (struct rkspi_softc *)self;

	HWRITE4(sc, SPI_ENR, 0);
	HWRITE4(sc, SPI_IMR, 0);
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ios);
	return 0;
}

void
rkspi_config(void *cookie, struct spi_config *conf)
{
	struct rkspi_softc *sc = cookie;
	uint32_t ctrlr0;
	uint16_t div;
	int cs;

	div = 2;
	while ((sc->sc_spi_freq / div) > conf->sc_freq)
		div++;
	/* Clock divider needs to be even. */
	if (div & 1)
		div++;

	cs = conf->sc_cs;
	if (cs >= 2) {
		printf("%s: invalid chip-select (%d)\n", DEVNAME(sc), cs);
		return;
	}
	sc->sc_cs = cs;
	sc->sc_cs_delay = conf->sc_cs_delay;

	ctrlr0 = SPI_CTRLR0_BHT_8BIT | SPI_CTRLR0_SSD_ONE | SPI_CTRLR0_EM_BIG;
	if (conf->sc_flags & SPI_CONFIG_CPHA)
		ctrlr0 |= SPI_CTRLR0_SCPH;
	if (conf->sc_flags & SPI_CONFIG_CPOL)
		ctrlr0 |= SPI_CTRLR0_SCPOL;
	switch (conf->sc_bpw) {
	case 4:
		ctrlr0 |= SPI_CTRLR0_DFS_4BIT;
		break;
	case 8:
		ctrlr0 |= SPI_CTRLR0_DFS_8BIT;
		break;
	case 16:
		ctrlr0 |= SPI_CTRLR0_DFS_16BIT;
		break;
	default:
		printf("%s: invalid bits-per-word (%d)\n", DEVNAME(sc),
		    conf->sc_bpw);
		return;
	}

	HWRITE4(sc, SPI_ENR, 0);
	HWRITE4(sc, SPI_SER, 0);
	HWRITE4(sc, SPI_CTRLR0, ctrlr0);
	HWRITE4(sc, SPI_BAUDR, div);
}

int
rkspi_wait_state(struct rkspi_softc *sc, uint32_t mask, uint32_t value)
{
	int timeout;

	for (timeout = 1000; timeout > 0; timeout--) {
		if ((HREAD4(sc, SPI_SR) & mask) == value)
			return 0;
		delay(10);
	}

	return ETIMEDOUT;
}

int
rkspi_transfer(void *cookie, char *out, char *in, int len, int flags)
{
	struct rkspi_softc *sc = cookie;
	int i;

	sc->sc_ridx = sc->sc_widx = 0;

	/* drain input buffer */
	while (!(HREAD4(sc, SPI_SR) & SPI_SR_RFE))
		HREAD4(sc, SPI_RXDR);

	if (out)
		HCLR4(sc, SPI_CTRLR0, SPI_CTRLR0_XFM_RO);
	else
		HSET4(sc, SPI_CTRLR0, SPI_CTRLR0_XFM_RO);
	HWRITE4(sc, SPI_CTRLR1, len - 1);

	HSET4(sc, SPI_SER, SPI_SER_CS(sc->sc_cs));
	delay(sc->sc_cs_delay);

	HWRITE4(sc, SPI_ENR, 1);

	while (sc->sc_ridx < len || sc->sc_widx < len) {
		for (i = sc->sc_widx; i < len; i++) {
			if (rkspi_wait_state(sc, SPI_SR_TFF, 0))
				goto err;
			if (out)
				HWRITE4(sc, SPI_TXDR, out[i]);
			sc->sc_widx++;
		}

		for (i = sc->sc_ridx; i < sc->sc_widx; i++) {
			if (rkspi_wait_state(sc, SPI_SR_RFE, 0))
				goto err;
			if (in)
				in[i] = HREAD4(sc, SPI_RXDR);
			else
				HREAD4(sc, SPI_RXDR);
			sc->sc_ridx++;
		}

		if (rkspi_wait_state(sc, SPI_SR_BSF, 0))
			goto err;
	}

	HWRITE4(sc, SPI_ENR, 0);

	if (!ISSET(flags, SPI_KEEP_CS))
		HCLR4(sc, SPI_SER, SPI_SER_CS(sc->sc_cs));
	return 0;

err:
	HWRITE4(sc, SPI_ENR, 0);

	HCLR4(sc, SPI_SER, SPI_SER_CS(sc->sc_cs));
	return ETIMEDOUT;
}

int
rkspi_acquire_bus(void *cookie, int flags)
{
	struct rkspi_softc *sc = cookie;

	rw_enter(&sc->sc_buslock, RW_WRITE);
	return 0;
}

void
rkspi_release_bus(void *cookie, int flags)
{
	struct rkspi_softc *sc = cookie;

	rw_exit(&sc->sc_buslock);
}

void
rkspi_scan(struct rkspi_softc *sc)
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
