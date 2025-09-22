/*	$OpenBSD: sxirsb.c,v 1.6 2022/07/09 20:52:46 kettenis Exp $	*/
/*
 * Copyright (c) 2017 Mark Kettenis <kettenis@openbsd.org>
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

#include <dev/fdt/rsbvar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/fdt.h>

#define RSB_CTRL			0x0000
#define  RSB_CTRL_START_TRANS		(1 << 7)
#define  RSB_CTRL_ABORT_TRANS		(1 << 6)
#define  RSB_CTRL_GLOBAL_INT_ENB	(1 << 1)
#define  RSB_CTRL_SOFT_RESET		(1 << 0)
#define RSB_CCR				0x0004
#define  RSB_CCR_CD_ODLY_SHIFT		8
#define  RSB_CCR_CD_ODLY_MAX		0x7
#define  RSB_CCR_CK_DIV_SHIFT		0
#define  RSB_CCR_CK_DIV_MAX		0xff
#define RSB_STAT			0x000c
#define  RSB_STAT_TRANS_OVER		(1 << 0)
#define RSB_AR				0x0010
#define RSB_DATA			0x001c
#define RSB_DMCR			0x0028
#define  RSB_DMCR_DEVICE_MODE_START	(1U << 31)
#define  RSB_DMCR_DEVICE_MODE_DATA	0x7c3e00
#define RSB_CMD				0x002c
#define RSB_DAR				0x0030

#define SRTA	0xe8
#define RD8	0x8b
#define RD16	0x9c
#define RD32	0xa6
#define WR8	0x4e
#define WR16	0x59
#define WR32	0x63

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct sxirsb_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	int			sc_node;
	int			sc_addr;
};

int	sxirsb_match(struct device *, void *, void *);
void	sxirsb_attach(struct device *, struct device *, void *);
int	sxirsb_activate(struct device *, int);

const struct cfattach sxirsb_ca = {
	sizeof(struct sxirsb_softc), sxirsb_match, sxirsb_attach,
	NULL, sxirsb_activate
};

struct cfdriver sxirsb_cd = {
	NULL, "sxirsb", DV_DULL
};

int	sxirsb_init(struct sxirsb_softc *);
uint8_t	sxirsb_rta(uint16_t);

int
sxirsb_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "allwinner,sun8i-a23-rsb");
}

void
sxirsb_attach(struct device *parent, struct device *self, void *aux)
{
	struct sxirsb_softc *sc = (struct sxirsb_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct rsb_attach_args ra;
	char name[32];
	uint32_t reg;
	int node;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_node = faa->fa_node;
	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	printf("\n");

	if (sxirsb_init(sc))
		return;

	for (node = OF_child(faa->fa_node); node; node = OF_peer(node)) {
		reg = OF_getpropint(node, "reg", 0);
		if (reg == 0)
			continue;

		memset(name, 0, sizeof(name));
		if (OF_getprop(node, "compatible", name, sizeof(name)) == -1)
			continue;
		if (name[0] == '\0')
			continue;

		memset(&ra, 0, sizeof(ra));
		ra.ra_cookie = sc;
		ra.ra_da = reg;
		ra.ra_rta = sxirsb_rta(reg);
		ra.ra_name = name;
		ra.ra_node = node;
		config_found(self, &ra, rsb_print);
	}
}

int
sxirsb_activate(struct device *self, int act)
{
	struct sxirsb_softc *sc = (struct sxirsb_softc *)self;
	int error = 0;

	switch (act) {
	case DVACT_RESUME:
		error = sxirsb_init(sc);
		if (error)
			return error;
		error = config_activate_children(self, act);
		break;
	default:
		error = config_activate_children(self, act);
		break;
	}

	return error;
}

int
sxirsb_init(struct sxirsb_softc *sc)
{
	uint32_t freq, parent_freq, div, odly;
	uint32_t reg;
	uint8_t rta;
	int node;
	int timo;

	pinctrl_byname(sc->sc_node, "default");

	clock_enable_all(sc->sc_node);
	reset_deassert_all(sc->sc_node);

	HWRITE4(sc, RSB_CTRL, RSB_CTRL_SOFT_RESET);
	for (timo = 1000; timo > 0; timo--) {
		if ((HREAD4(sc, RSB_CTRL) & RSB_CTRL_SOFT_RESET) == 0)
			break;
		delay(100);
	}
	if (timo == 0) {
		printf("%s: reset failed\n", sc->sc_dev.dv_xname);
		return EIO;
	}

	freq = OF_getpropint(sc->sc_node, "clock-frequency", 3000000);
	parent_freq = clock_get_frequency_idx(sc->sc_node, 0);
	div = parent_freq / freq / 2;
	if (div == 0)
		div = 1;
	if (div > (RSB_CCR_CK_DIV_MAX + 1))
		div = (RSB_CCR_CK_DIV_MAX + 1);
	odly = div >> 1;
	if (odly == 0)
		odly = 1;
	if (odly > RSB_CCR_CD_ODLY_MAX)
		odly = RSB_CCR_CD_ODLY_MAX;
	HWRITE4(sc, RSB_CCR, (odly << RSB_CCR_CD_ODLY_SHIFT) |
	    ((div - 1) << RSB_CCR_CK_DIV_SHIFT));

	HWRITE4(sc, RSB_DMCR, RSB_DMCR_DEVICE_MODE_START |
	    RSB_DMCR_DEVICE_MODE_DATA);
	for (timo = 1000; timo > 0; timo--) {
		if ((HREAD4(sc, RSB_DMCR) & RSB_DMCR_DEVICE_MODE_START) == 0)
			break;
		delay(100);
	}
	if (timo == 0) {
		printf("%s: mode switch failed\n", sc->sc_dev.dv_xname);
		return EIO;
	}

	for (node = OF_child(sc->sc_node); node; node = OF_peer(node)) {
		reg = OF_getpropint(node, "reg", 0);
		if (reg == 0)
			continue;

		rta = sxirsb_rta(reg);
		HWRITE4(sc, RSB_CMD, SRTA);
		HWRITE4(sc, RSB_DAR, (rta << 16 | reg));

		HSET4(sc, RSB_CTRL, RSB_CTRL_START_TRANS);
		for (timo = 1000; timo > 0; timo--) {
			if ((HREAD4(sc, RSB_CTRL) & RSB_CTRL_START_TRANS) == 0)
				break;
			delay(10);
		}
		if (timo == 0) {
			printf("%s: SRTA failed for device 0x%03x\n",
			    sc->sc_dev.dv_xname, reg);
			return EIO;
		}
	}

	return 0;
}

/*
 * Use a fixed device address to run-time address mapping.  This keeps
 * things simple and matches what Linux does.
 */

struct rsb_addr_map {
	uint16_t	da;
	uint8_t		rta;
};

struct rsb_addr_map rsb_addr_map[] = {
	{ 0x3a3, 0x2d },
	{ 0x745, 0x3a },
	{ 0xe89, 0x4e }
};

uint8_t
sxirsb_rta(uint16_t da)
{
	int i;

	for (i = 0; i < nitems(rsb_addr_map); i++) {
		if (rsb_addr_map[i].da == da)
			return rsb_addr_map[i].rta;
	}

	return 0;
}

int
sxirsb_do_trans(struct sxirsb_softc *sc)
{
	uint16_t stat;
	int timo;

	HSET4(sc, RSB_CTRL, RSB_CTRL_START_TRANS);
	for (timo = 1000; timo > 0; timo--) {
		if ((HREAD4(sc, RSB_CTRL) & RSB_CTRL_START_TRANS) == 0)
			break;
		delay(10);
	}
	stat = HREAD4(sc, RSB_STAT);
	HWRITE4(sc, RSB_STAT, stat);
	if (stat != RSB_STAT_TRANS_OVER)
		return EIO;
	if (timo == 0)
		return ETIMEDOUT;
	return 0;
}

uint8_t
rsb_read_1(void *cookie, uint8_t rta, uint8_t addr)
{
	struct sxirsb_softc *sc = cookie;

	HWRITE4(sc, RSB_CMD, RD8);
	HWRITE4(sc, RSB_DAR, rta << 16);
	HWRITE4(sc, RSB_AR, addr);

	if (sxirsb_do_trans(sc)) {
		printf("%s: RD8 failed for run-time address 0x%02x\n",
		    sc->sc_dev.dv_xname, rta);
		return 0xff;
	}

	return HREAD4(sc, RSB_DATA);
}

uint16_t
rsb_read_2(void *cookie, uint8_t rta, uint8_t addr)
{
	struct sxirsb_softc *sc = cookie;

	HWRITE4(sc, RSB_CMD, RD16);
	HWRITE4(sc, RSB_DAR, rta << 16);
	HWRITE4(sc, RSB_AR, addr);

	if (sxirsb_do_trans(sc)) {
		printf("%s: RD16 failed for run-time address 0x%02x\n",
		    sc->sc_dev.dv_xname, rta);
		return 0xff;
	}

	return HREAD4(sc, RSB_DATA);
}

void
rsb_write_1(void *cookie, uint8_t rta, uint8_t addr, uint8_t data)
{
	struct sxirsb_softc *sc = cookie;

	HWRITE4(sc, RSB_CMD, WR8);
	HWRITE4(sc, RSB_DAR, rta << 16);
	HWRITE4(sc, RSB_AR, addr);
	HWRITE4(sc, RSB_DATA, data);

	if (sxirsb_do_trans(sc)) {
		printf("%s: WR8 failed for run-time address 0x%02x\n",
		    sc->sc_dev.dv_xname, rta);
		return;
	}
}

void
rsb_write_2(void *cookie, uint8_t rta, uint8_t addr, uint16_t data)
{
	struct sxirsb_softc *sc = cookie;

	HWRITE4(sc, RSB_CMD, WR16);
	HWRITE4(sc, RSB_DAR, rta << 16);
	HWRITE4(sc, RSB_AR, addr);
	HWRITE4(sc, RSB_DATA, data);

	if (sxirsb_do_trans(sc)) {
		printf("%s: WR16 failed for run-time address 0x%02x\n",
		    sc->sc_dev.dv_xname, rta);
		return;
	}
}

int
rsb_print(void *aux, const char *pnp)
{
	struct rsb_attach_args *ra = aux;

	if (pnp != NULL)
		printf("\"%s\" at %s", ra->ra_name, pnp);
	printf(" addr 0x%x", ra->ra_da);

	return (UNCONF);
}
