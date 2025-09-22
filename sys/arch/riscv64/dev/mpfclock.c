/*	$OpenBSD: mpfclock.c,v 1.1 2022/01/05 03:32:44 visa Exp $	*/

/*
 * Copyright (c) 2022 Visa Hankala
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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
 * Driver for PolarFire SoC MSS clock controller.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/mutex.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/fdt.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>

extern void (*cpuresetfn)(void);

#define CLOCK_CONFIG_CR			0x0008
#define  CLOCK_CONFIG_CR_AHB_DIV_SHIFT		4
#define  CLOCK_CONFIG_CR_AXI_DIV_SHIFT		2
#define  CLOCK_CONFIG_CR_CPU_DIV_SHIFT		0
#define  CLOCK_CONFIG_CR_DIV_MASK		0x3
#define MSS_RESET_CR			0x0018
#define SUBBLK_CLOCK_CR			0x0084
#define SUBBLK_RESET_CR			0x0088

#define CLK_CPU				0
#define CLK_AXI				1
#define CLK_AHB				2
#define CLK_ENVM			3
#define CLK_MAC0			4
#define CLK_MAC1			5
#define CLK_MMC				6
#define CLK_TIMER			7
#define CLK_MMUART0			8
#define CLK_MMUART1			9
#define CLK_MMUART2			10
#define CLK_MMUART3			11
#define CLK_MMUART4			12
#define CLK_SPI0			13
#define CLK_SPI1			14
#define CLK_I2C0			15
#define CLK_I2C1			16
#define CLK_CAN0			17
#define CLK_CAN1			18
#define CLK_USB				19
#define CLK_RESERVED			20	/* FPGA in SUBBLK_RESET_CR */
#define CLK_RTC				21
#define CLK_QSPI			22
#define CLK_GPIO0			23
#define CLK_GPIO1			24
#define CLK_GPIO2			25
#define CLK_DDRC			26
#define CLK_FIC0			27
#define CLK_FIC1			28
#define CLK_FIC2			29
#define CLK_FIC3			30
#define CLK_ATHENA			31
#define CLK_CFM				32

struct mpfclock_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	uint32_t		sc_clkcfg;
	uint32_t		sc_refclk;

	struct clock_device	sc_cd;
};

#define HREAD4(sc, reg) \
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val) \
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

int	mpfclock_match(struct device *, void *, void *);
void	mpfclock_attach(struct device *, struct device *, void *);

void	mpfclock_enable(void *, uint32_t *, int);
uint32_t mpfclock_get_frequency(void *, uint32_t *);
int	mpfclock_set_frequency(void *, uint32_t *, uint32_t);

void	mpfclock_cpureset(void);

const struct cfattach mpfclock_ca = {
	sizeof(struct mpfclock_softc), mpfclock_match, mpfclock_attach
};

struct cfdriver mpfclock_cd = {
	NULL, "mpfclock", DV_DULL
};

struct mutex		mpfclock_mtx = MUTEX_INITIALIZER(IPL_HIGH);
struct mpfclock_softc	*mpfclock_sc;

int
mpfclock_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	if (faa->fa_nreg < 1)
		return 0;
	return OF_is_compatible(faa->fa_node, "microchip,mpfs-clkcfg");
}

void
mpfclock_attach(struct device *parent, struct device *self, void *aux)
{
	struct fdt_attach_args *faa = aux;
	struct mpfclock_softc *sc = (struct mpfclock_softc *)self;

	sc->sc_refclk = clock_get_frequency_idx(faa->fa_node, 0);
	if (sc->sc_refclk == 0) {
		printf(": can't get refclk frequency\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh) != 0) {
		printf(": can't map registers\n");
		return;
	}

	sc->sc_clkcfg = HREAD4(sc, CLOCK_CONFIG_CR);

	printf(": %u MHz ref clock\n", (sc->sc_refclk + 500000) / 1000000);

	sc->sc_cd.cd_node = faa->fa_node;
	sc->sc_cd.cd_cookie = sc;
	sc->sc_cd.cd_enable = mpfclock_enable;
	sc->sc_cd.cd_get_frequency = mpfclock_get_frequency;
	sc->sc_cd.cd_set_frequency = mpfclock_set_frequency;
	clock_register(&sc->sc_cd);

	mpfclock_sc = sc;
	cpuresetfn = mpfclock_cpureset;
}

uint32_t
mpfclock_get_frequency(void *cookie, uint32_t *cells)
{
	struct mpfclock_softc *sc = cookie;
	uint32_t div, shift;
	uint32_t idx = cells[0];

	if (idx == CLK_MMC)
		return 200000000;

	if (idx > CLK_AHB)
		idx = CLK_AHB;

	switch (idx) {
	case CLK_CPU:
		shift = CLOCK_CONFIG_CR_CPU_DIV_SHIFT;
		break;
	case CLK_AXI:
		shift = CLOCK_CONFIG_CR_AXI_DIV_SHIFT;
		break;
	case CLK_AHB:
		shift = CLOCK_CONFIG_CR_AHB_DIV_SHIFT;
		break;
	default:
		panic("%s: invalid idx %u\n", __func__, idx);
	}

	div = 1U << ((sc->sc_clkcfg >> shift) & CLOCK_CONFIG_CR_DIV_MASK);

	return sc->sc_refclk / div;
}

int
mpfclock_set_frequency(void *cookie, uint32_t *cells, uint32_t freq)
{
	return -1;
}

void
mpfclock_enable(void *cookie, uint32_t *cells, int on)
{
	struct mpfclock_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t bit, val;

	if (idx < CLK_ENVM || idx - CLK_ENVM > 31)
		return;
	bit = 1U << (idx - CLK_ENVM);

	mtx_enter(&mpfclock_mtx);
	if (on) {
		val = HREAD4(sc, SUBBLK_CLOCK_CR);
		val |= bit;
		HWRITE4(sc, SUBBLK_CLOCK_CR, val);

		val = HREAD4(sc, SUBBLK_RESET_CR);
		val &= ~bit;
		HWRITE4(sc, SUBBLK_RESET_CR, val);
	} else {
		val = HREAD4(sc, SUBBLK_RESET_CR);
		val |= bit;
		HWRITE4(sc, SUBBLK_RESET_CR, val);

		val = HREAD4(sc, SUBBLK_CLOCK_CR);
		val &= ~bit;
		HWRITE4(sc, SUBBLK_CLOCK_CR, val);
	}
	mtx_leave(&mpfclock_mtx);
}

void
mpfclock_cpureset(void)
{
	struct mpfclock_softc *sc = mpfclock_sc;

	HWRITE4(sc, MSS_RESET_CR, 0xdead);
}
