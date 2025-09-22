/*	$OpenBSD: rkiovd.c,v 1.1 2023/04/01 08:39:05 kettenis Exp $	*/
/*
 * Copyright (c) 2023 Mark Kettenis <kettenis@openbsd.org>
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

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_regulator.h>
#include <dev/ofw/fdt.h>

#define RK3568_PMU_GRF_IO_VSEL0		0x0140
#define RK3568_PMU_GRF_IO_VSEL1		0x0144
#define RK3568_PMU_GRF_IO_VSEL2		0x0148

#define RKIOVD_MAX_DOMAINS	9

const char *rkiovd_rk3568_domains[] = {
	"pmuio1-supply",
	"pmuio2-supply",
	"vccio1-supply",
	"vccio2-supply",
	"vccio3-supply",
	"vccio4-supply",
	"vccio5-supply",
	"vccio6-supply",
	"vccio7-supply",
	NULL
};

struct rkiovd_domain {
	struct rkiovd_softc	*rd_sc;
	int			rd_idx;
	struct regulator_notifier rd_rn;
};

struct rkiovd_softc {
	struct device		sc_dv;
	struct regmap		*sc_rm;
	struct rkiovd_domain	sc_rd[RKIOVD_MAX_DOMAINS];
};

int	rkiovd_match(struct device *, void *, void *);
void	rkiovd_attach(struct device *, struct device *, void *);

const struct cfattach rkiovd_ca = {
	sizeof (struct rkiovd_softc), rkiovd_match, rkiovd_attach
};

struct cfdriver rkiovd_cd = {
	NULL, "rkiovd", DV_DULL
};

void	rkiovd_rk3568_notify(void *, uint32_t);

int
rkiovd_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node,
	    "rockchip,rk3568-pmu-io-voltage-domain");
}

void
rkiovd_attach(struct device *parent, struct device *self, void *aux)
{
	struct rkiovd_softc *sc = (struct rkiovd_softc *)self;
	struct fdt_attach_args *faa = aux;
	int idx;

	printf("\n");

	sc->sc_rm = regmap_bynode(OF_parent(faa->fa_node));
	if (sc->sc_rm == NULL)
		return;

	for (idx = 0; rkiovd_rk3568_domains[idx]; idx++) {
		struct rkiovd_domain *rd = &sc->sc_rd[idx];
		char *name = (char *)rkiovd_rk3568_domains[idx];
		uint32_t phandle;

		phandle = OF_getpropint(faa->fa_node, name, 0);
		if (phandle == 0)
			continue;

		rd->rd_sc = sc;
		rd->rd_idx = idx;
		rd->rd_rn.rn_phandle = phandle;
		rd->rd_rn.rn_cookie = rd;
		rd->rd_rn.rn_notify = rkiovd_rk3568_notify;
		regulator_notify(&rd->rd_rn);
	}
}

void
rkiovd_rk3568_notify(void *cookie, uint32_t voltage)
{
	struct rkiovd_domain *rd = cookie;
	struct rkiovd_softc *sc = rd->rd_sc;
	uint32_t current_voltage;
	uint32_t vsel0, vsel1;
	int bit;

	/*
	 * If the new voltage is higher than the current voltage we
	 * need to configure the I/O voltage domain before changing
	 * the voltage.  If the new voltage is lower we need to
	 * configure the domain after changing the voltage.  Using the
	 * maximum of the new and current voltage makes sure this is
	 * always the case.
	 */
	current_voltage = regulator_get_voltage(rd->rd_rn.rn_phandle);
	if (voltage < current_voltage)
		voltage = current_voltage;

	switch (rd->rd_idx) {
	case 1:			/* PMUIO2 */
		if (voltage > 1980000) {
			vsel0 = (1 << 1) << 16 | (0 << 1);
			vsel1 = (1 << 5) << 16 | (1 << 5);
		} else {
			vsel0 = (1 << 1) << 16 | (1 << 1);
			vsel1 = (1 << 5) << 16 | (0 << 5);
		}
		regmap_write_4(sc->sc_rm, RK3568_PMU_GRF_IO_VSEL2, vsel0);
		regmap_write_4(sc->sc_rm, RK3568_PMU_GRF_IO_VSEL2, vsel1);
		break;
	case 2:			/* VCCIO1 */
	case 4:			/* VCCIO3 */
	case 5:			/* VCCIO4 */
	case 6:			/* VCCIO5 */
	case 7:			/* VCCIO6 */
	case 8:			/* VCCIO7 */
		bit = rd->rd_idx - 1;
		if (voltage > 1980000) {
			vsel0 = (1 << bit) << 16 | (0 << bit);
			vsel1 = (1 << bit) << 16 | (1 << bit);
		} else {
			vsel0 = (1 << bit) << 16 | (1 << bit);
			vsel1 = (1 << bit) << 16 | (0 << bit);
		}
		regmap_write_4(sc->sc_rm, RK3568_PMU_GRF_IO_VSEL0, vsel0);
		regmap_write_4(sc->sc_rm, RK3568_PMU_GRF_IO_VSEL1, vsel1);
		break;
	case 0:			/* PMUIO1 */
	case 3:			/* VCCIO2 */
		/* Ignore */
		break;
	}
}
