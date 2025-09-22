/*	$OpenBSD: dwiic_fdt.c,v 1.2 2024/03/29 22:08:09 kettenis Exp $	*/
/*
 * Copyright (c) 2023 Patrick Wildt <patrick@blueri.se>
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
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/fdt.h>

#include <dev/ic/dwiicvar.h>

static inline uint32_t
round_closest(uint64_t num, uint64_t den)
{
	return (num + (den / 2)) / den;
}

struct dwiic_fdt_softc {
	struct dwiic_softc	sc_sc;
	int			sc_node;
};

int	dwiic_fdt_match(struct device *, void *, void *);
void	dwiic_fdt_attach(struct device *, struct device *, void *);

const struct cfattach dwiic_fdt_ca = {
	sizeof(struct dwiic_fdt_softc), dwiic_fdt_match, dwiic_fdt_attach
};

void	dwiic_fdt_calc_timings(struct dwiic_fdt_softc *);
void	dwiic_fdt_bus_scan(struct device *, struct i2cbus_attach_args *,
	    void *);

int
dwiic_fdt_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "snps,designware-i2c");
}

void
dwiic_fdt_attach(struct device *parent, struct device *self, void *aux)
{
	struct dwiic_fdt_softc *fsc = (struct dwiic_fdt_softc *)self;
	struct dwiic_softc *sc = &fsc->sc_sc;
	struct fdt_attach_args *faa = aux;
	struct i2cbus_attach_args iba;

	if (faa->fa_nreg < 1)
		return;

	sc->sc_iot = faa->fa_iot;
	fsc->sc_node = faa->fa_node;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	pinctrl_byname(faa->fa_node, "default");
	reset_deassert_all(faa->fa_node);
	clock_enable(faa->fa_node, NULL);

	dwiic_fdt_calc_timings(fsc);

	if (dwiic_init(sc)) {
		printf(": can't initialize\n");
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, faa->fa_reg[0].size);
		return;
	}

	/* leave the controller disabled */
	dwiic_write(sc, DW_IC_INTR_MASK, 0);
	dwiic_enable(sc, 0);
	dwiic_read(sc, DW_IC_CLR_INTR);

	sc->sc_ih = fdt_intr_establish(faa->fa_node, IPL_BIO,
	    dwiic_intr, sc, sc->sc_dev.dv_xname);

	printf("\n");

	rw_init(&sc->sc_i2c_lock, "dwiic");

	sc->sc_i2c_tag.ic_cookie = sc;
	sc->sc_i2c_tag.ic_acquire_bus = dwiic_i2c_acquire_bus;
	sc->sc_i2c_tag.ic_release_bus = dwiic_i2c_release_bus;
	sc->sc_i2c_tag.ic_exec = dwiic_i2c_exec;

	bzero(&iba, sizeof iba);
	iba.iba_name = "iic";
	iba.iba_tag = &sc->sc_i2c_tag;
	iba.iba_bus_scan = dwiic_fdt_bus_scan;
	iba.iba_bus_scan_arg = &fsc->sc_node;
	config_found(&sc->sc_dev, &iba, iicbus_print);
}

void
dwiic_fdt_calc_timings(struct dwiic_fdt_softc *fsc)
{
	struct dwiic_softc *sc = &fsc->sc_sc;
	uint32_t sda_hold, sda_fall, scl_fall;
	uint64_t freq;

	freq = clock_get_frequency(fsc->sc_node, NULL);
	if (freq == 0)
		return;

	sda_hold = OF_getpropint(fsc->sc_node, "i2c-sda-hold-time-ns", 300);
	sda_fall = OF_getpropint(fsc->sc_node, "i2c-sda-falling-time-ns", 300);
	scl_fall = OF_getpropint(fsc->sc_node, "i2c-scl-falling-time-ns", 300);

	sc->sda_hold_time = round_closest(freq * sda_hold, 1000000000);

	/* Standard-mode: tHIGH = 4.0 us; tLOW = 4.7 us */
	sc->ss_hcnt = round_closest(freq * (4000 + sda_fall), 1000000000) - 3;
	sc->ss_lcnt = round_closest(freq * (4700 + scl_fall), 1000000000) - 1;
	/* Fast-mode: tHIGH = 0.6 us; tLOW = 1.3 us */
	sc->fs_hcnt = round_closest(freq * (600 + sda_fall), 1000000000) - 3;
	sc->fs_lcnt = round_closest(freq * (1300 + scl_fall), 1000000000) - 1;
}

void
dwiic_fdt_bus_scan(struct device *self, struct i2cbus_attach_args *iba,
    void *aux)
{
	int iba_node = *(int *)aux;
	extern int iic_print(void *, const char *);
	struct i2c_attach_args ia;
	char name[32], status[32];
	uint32_t reg[1];
	int node;

	for (node = OF_child(iba_node); node; node = OF_peer(node)) {
		memset(name, 0, sizeof(name));
		memset(status, 0, sizeof(status));
		memset(reg, 0, sizeof(reg));

		if (OF_getprop(node, "compatible", name, sizeof(name)) == -1)
			continue;
		if (name[0] == '\0')
			continue;

		if (OF_getprop(node, "status", status, sizeof(status)) > 0 &&
		    strcmp(status, "disabled") == 0)
			continue;

		if (OF_getprop(node, "reg", &reg, sizeof(reg)) != sizeof(reg))
			continue;

		memset(&ia, 0, sizeof(ia));
		ia.ia_tag = iba->iba_tag;
		ia.ia_addr = bemtoh32(&reg[0]);
		ia.ia_name = name;
		ia.ia_cookie = &node;

		config_found(self, &ia, iic_print);
	}
}
