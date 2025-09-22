/* $OpenBSD: imxspi.c,v 1.5 2024/05/13 01:15:50 jsg Exp $ */
/*
 * Copyright (c) 2018 Patrick Wildt <patrick@blueri.se>
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
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/fdt.h>

/* registers */
#define SPI_RXDATA			0x00
#define SPI_TXDATA			0x04
#define SPI_CONREG			0x08
#define  SPI_CONREG_EN				(1 << 0)
#define  SPI_CONREG_HT				(1 << 1)
#define  SPI_CONREG_XCH				(1 << 2)
#define  SPI_CONREG_SMC				(1 << 3)
#define  SPI_CONREG_CHANNEL_MASTER		(0xf << 4)
#define  SPI_CONREG_POST_DIVIDER_SHIFT		8
#define  SPI_CONREG_POST_DIVIDER_MASK		0xf
#define  SPI_CONREG_PRE_DIVIDER_SHIFT		12
#define  SPI_CONREG_PRE_DIVIDER_MASK		0xf
#define  SPI_CONREG_DRCTL_SHIFT			16
#define  SPI_CONREG_DRCTL_MASK			0x3
#define  SPI_CONREG_CHANNEL_SELECT(x)		((x) << 18)
#define  SPI_CONREG_BURST_LENGTH(x)		((x) << 20)
#define SPI_CONFIGREG			0x0c
#define  SPI_CONFIGREG_SCLK_PHA(x)		(1 << (0 + (x)))
#define  SPI_CONFIGREG_SCLK_POL(x)		(1 << (4 + (x)))
#define  SPI_CONFIGREG_SS_CTL(x)		(1 << (8 + (x)))
#define  SPI_CONFIGREG_SS_POL(x)		(1 << (12 + (x)))
#define  SPI_CONFIGREG_DATA_CTL(x)		(1 << (16 + (x)))
#define  SPI_CONFIGREG_SCLK_CTL(x)		(1 << (20 + (x)))
#define  SPI_CONFIGREG_HT_LENGTH(x)		(((x) & 0x1f) << 24)
#define SPI_INTREG			0x10
#define  SPI_INTREG_TEEN			(1 << 0)
#define  SPI_INTREG_TDREN			(1 << 1)
#define  SPI_INTREG_TFEN			(1 << 2)
#define  SPI_INTREG_RREN			(1 << 3)
#define  SPI_INTREG_RDREN			(1 << 4)
#define  SPI_INTREG_RFEN			(1 << 5)
#define  SPI_INTREG_ROEN			(1 << 6)
#define  SPI_INTREG_TCEN			(1 << 7)
#define SPI_DMAREG			0x14
#define SPI_STATREG			0x18
#define  SPI_STATREG_TE				(1 << 0)
#define  SPI_STATREG_TDR			(1 << 1)
#define  SPI_STATREG_TF				(1 << 2)
#define  SPI_STATREG_RR				(1 << 3)
#define  SPI_STATREG_RDR			(1 << 4)
#define  SPI_STATREG_RF				(1 << 5)
#define  SPI_STATREG_RO				(1 << 6)
#define  SPI_STATREG_TC				(1 << 7)
#define SPI_PERIODREG			0x1c
#define SPI_TESTREG			0x20
#define  SPI_TESTREG_LBC			(1U << 31)
#define SPI_MSGDATA			0x40

#define DEVNAME(sc)	((sc)->sc_dev.dv_xname)

struct imxspi_softc {
	struct device		 sc_dev;
	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_ioh;
	bus_size_t		 sc_ios;
	int			 sc_node;

	uint32_t		*sc_gpio;
	int			 sc_gpiolen;

	struct rwlock		 sc_buslock;
	struct spi_controller	 sc_tag;

	int			 sc_ridx;
	int			 sc_widx;
	int			 sc_cs;
	u_int			 sc_cs_delay;
};

int	 imxspi_match(struct device *, void *, void *);
void	 imxspi_attach(struct device *, struct device *, void *);
void	 imxspi_attachhook(struct device *);
int	 imxspi_detach(struct device *, int);

void	 imxspi_config(void *, struct spi_config *);
uint32_t imxspi_clkdiv(struct imxspi_softc *, uint32_t);
int	 imxspi_transfer(void *, char *, char *, int, int);
int	 imxspi_acquire_bus(void *, int);
void	 imxspi_release_bus(void *, int);

void	*imxspi_find_cs_gpio(struct imxspi_softc *, int);
int	 imxspi_wait_state(struct imxspi_softc *, uint32_t, uint32_t);

void	 imxspi_scan(struct imxspi_softc *);

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

const struct cfattach imxspi_ca = {
	sizeof(struct imxspi_softc), imxspi_match, imxspi_attach,
	imxspi_detach
};

struct cfdriver imxspi_cd = {
	NULL, "imxspi", DV_DULL
};

int
imxspi_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "fsl,imx51-ecspi");
}

void
imxspi_attach(struct device *parent, struct device *self, void *aux)
{
	struct imxspi_softc *sc = (struct imxspi_softc *)self;
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

	printf("\n");

	config_mountroot(self, imxspi_attachhook);
}

void
imxspi_attachhook(struct device *self)
{
	struct imxspi_softc *sc = (struct imxspi_softc *)self;
	uint32_t *gpio;
	int i;

	pinctrl_byname(sc->sc_node, "default");
	clock_enable(sc->sc_node, NULL);

	sc->sc_gpiolen = OF_getproplen(sc->sc_node, "cs-gpios");
	if (sc->sc_gpiolen > 0) {
		sc->sc_gpio = malloc(sc->sc_gpiolen, M_DEVBUF, M_WAITOK);
		OF_getpropintarray(sc->sc_node, "cs-gpios",
		    sc->sc_gpio, sc->sc_gpiolen);
		for (i = 0; i < 4; i++) {
			gpio = imxspi_find_cs_gpio(sc, i);
			if (gpio == NULL)
				break;
			gpio_controller_config_pin(gpio,
			    GPIO_CONFIG_OUTPUT);
			gpio_controller_set_pin(gpio, 1);
		}
	}

	/* disable interrupts */
	HWRITE4(sc, SPI_INTREG, 0);
	HWRITE4(sc, SPI_STATREG, SPI_STATREG_TC);

	/* drain input buffer */
	while (HREAD4(sc, SPI_STATREG) & SPI_STATREG_RR)
		HREAD4(sc, SPI_RXDATA);

	rw_init(&sc->sc_buslock, sc->sc_dev.dv_xname);

	sc->sc_tag.sc_cookie = sc;
	sc->sc_tag.sc_config = imxspi_config;
	sc->sc_tag.sc_transfer = imxspi_transfer;
	sc->sc_tag.sc_acquire_bus = imxspi_acquire_bus;
	sc->sc_tag.sc_release_bus = imxspi_release_bus;

	imxspi_scan(sc);
}

int
imxspi_detach(struct device *self, int flags)
{
	struct imxspi_softc *sc = (struct imxspi_softc *)self;

	HWRITE4(sc, SPI_CONREG, 0);
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ios);
	free(sc->sc_gpio, M_DEVBUF, sc->sc_gpiolen);
	return 0;
}

void
imxspi_config(void *cookie, struct spi_config *conf)
{
	struct imxspi_softc *sc = cookie;
	uint32_t conreg, configreg;
	int cs;

	cs = conf->sc_cs;
	if (cs > 4) {
		printf("%s: invalid chip-select (%d)\n", DEVNAME(sc), cs);
		return;
	}
	sc->sc_cs = cs;
	sc->sc_cs_delay = conf->sc_cs_delay;

	conreg = SPI_CONREG_EN;
	conreg |= SPI_CONREG_CHANNEL_MASTER;
	conreg |= imxspi_clkdiv(sc, conf->sc_freq);
	conreg |= SPI_CONREG_CHANNEL_SELECT(cs);
	conreg |= SPI_CONREG_BURST_LENGTH(conf->sc_bpw - 1);

	configreg = HREAD4(sc, SPI_CONFIGREG);
	configreg &= ~SPI_CONFIGREG_SCLK_PHA(cs);
	if (conf->sc_flags & SPI_CONFIG_CPHA)
		configreg |= SPI_CONFIGREG_SCLK_PHA(cs);
	configreg &= ~SPI_CONFIGREG_SCLK_POL(cs);
	configreg &= ~SPI_CONFIGREG_SCLK_CTL(cs);
	if (conf->sc_flags & SPI_CONFIG_CPOL) {
		configreg |= SPI_CONFIGREG_SCLK_POL(cs);
		configreg |= SPI_CONFIGREG_SCLK_CTL(cs);
	}
	configreg |= SPI_CONFIGREG_SS_CTL(cs);
	configreg &= ~SPI_CONFIGREG_SS_POL(cs);
	if (conf->sc_flags & SPI_CONFIG_CS_HIGH)
		configreg |= SPI_CONFIGREG_SS_POL(cs);

	HWRITE4(sc, SPI_CONREG, conreg);
	HWRITE4(sc, SPI_TESTREG, HREAD4(sc, SPI_TESTREG) &
	    ~SPI_TESTREG_LBC);
	HWRITE4(sc, SPI_CONFIGREG, configreg);
	delay(1000);
}

uint32_t
imxspi_clkdiv(struct imxspi_softc *sc, uint32_t freq)
{
	uint32_t pre, post;
	uint32_t pfreq;

	pfreq = clock_get_frequency(sc->sc_node, "per");

	pre = 0, post = 0;
	while ((freq * (1 << post) * 16) < pfreq)
		post++;
	while ((freq * (1 << post) * (pre + 1)) < pfreq)
		pre++;
	if (post >= 16 || pre >= 16) {
		printf("%s: clock frequency too high\n",
		    DEVNAME(sc));
		return 0;
	}

	return (pre << SPI_CONREG_PRE_DIVIDER_SHIFT |
	    post << SPI_CONREG_POST_DIVIDER_SHIFT);
}

int
imxspi_wait_state(struct imxspi_softc *sc, uint32_t mask, uint32_t value)
{
	uint32_t state;
	int timeout;
	state = HREAD4(sc, SPI_STATREG);
	for (timeout = 1000; timeout > 0; timeout--) {
		if (((state = HREAD4(sc, SPI_STATREG)) & mask) == value)
			return 0;
		delay(10);
	}
	printf("%s: timeout mask %x value %x\n", __func__, mask, value);
	return ETIMEDOUT;
}

void *
imxspi_find_cs_gpio(struct imxspi_softc *sc, int cs)
{
	uint32_t *gpio;

	if (sc->sc_gpio == NULL)
		return NULL;

	gpio = sc->sc_gpio;
	while (gpio < sc->sc_gpio + (sc->sc_gpiolen / 4)) {
		if (cs == 0)
			return gpio;
		gpio = gpio_controller_next_pin(gpio);
		cs--;
	}

	return NULL;
}

int
imxspi_transfer(void *cookie, char *out, char *in, int len, int flags)
{
	struct imxspi_softc *sc = cookie;
	uint32_t *gpio;
	int i;

	sc->sc_ridx = sc->sc_widx = 0;

	gpio = imxspi_find_cs_gpio(sc, sc->sc_cs);
	if (gpio) {
		gpio_controller_set_pin(gpio, 0);
		delay(1);
	}
	delay(sc->sc_cs_delay);

	/* drain input buffer */
	while (HREAD4(sc, SPI_STATREG) & SPI_STATREG_RR)
		HREAD4(sc, SPI_RXDATA);

	while (sc->sc_ridx < len || sc->sc_widx < len) {
		for (i = sc->sc_widx; i < len; i++) {
			if (imxspi_wait_state(sc, SPI_STATREG_TF, 0))
				goto err;
			if (out)
				HWRITE4(sc, SPI_TXDATA, out[i]);
			else
				HWRITE4(sc, SPI_TXDATA, 0xff);
			sc->sc_widx++;
			if (HREAD4(sc, SPI_STATREG) & SPI_STATREG_TF)
				break;
		}

		HSET4(sc, SPI_CONREG, SPI_CONREG_XCH);
		if (imxspi_wait_state(sc, SPI_STATREG_TC, SPI_STATREG_TC))
			goto err;

		for (i = sc->sc_ridx; i < sc->sc_widx; i++) {
			if (imxspi_wait_state(sc, SPI_STATREG_RR, SPI_STATREG_RR))
				goto err;
			if (in)
				in[i] = HREAD4(sc, SPI_RXDATA);
			else
				HREAD4(sc, SPI_RXDATA);
			sc->sc_ridx++;
		}

		HWRITE4(sc, SPI_STATREG, SPI_STATREG_TC);
	}

	if (!ISSET(flags, SPI_KEEP_CS)) {
		gpio = imxspi_find_cs_gpio(sc, sc->sc_cs);
		if (gpio) {
			gpio_controller_set_pin(gpio, 1);
			delay(1);
		}
	}

	return 0;
err:
	HWRITE4(sc, SPI_CONREG, 0);
	HWRITE4(sc, SPI_STATREG, SPI_STATREG_TC);
	return ETIMEDOUT;
}

int
imxspi_acquire_bus(void *cookie, int flags)
{
	struct imxspi_softc *sc = cookie;

	rw_enter(&sc->sc_buslock, RW_WRITE);
	return 0;
}

void
imxspi_release_bus(void *cookie, int flags)
{
	struct imxspi_softc *sc = cookie;

	rw_exit(&sc->sc_buslock);
}

void
imxspi_scan(struct imxspi_softc *sc)
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
