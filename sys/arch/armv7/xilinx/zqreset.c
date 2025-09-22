/*	$OpenBSD: zqreset.c,v 1.1 2021/04/30 13:20:14 visa Exp $	*/

/*
 * Copyright (c) 2021 Visa Hankala
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
 * Driver for Xilinx Zynq-7000 reset controller.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/mutex.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/fdt.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_misc.h>

#include <armv7/xilinx/slcreg.h>

extern void (*cpuresetfn)(void);

struct zqreset_softc {
	struct device		sc_dev;
	struct regmap		*sc_rm;
};

int	zqreset_match(struct device *, void *, void *);
void	zqreset_attach(struct device *, struct device *, void *);

void	zqreset_cpureset(void);

const struct cfattach zqreset_ca = {
	sizeof(struct zqreset_softc), zqreset_match, zqreset_attach
};

struct cfdriver zqreset_cd = {
	NULL, "zqreset", DV_DULL
};

struct zqreset_softc	*zqreset_sc;

struct mutex	zynq_slcr_lock = MUTEX_INITIALIZER(IPL_HIGH);

int
zqreset_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "xlnx,zynq-reset");
}

void
zqreset_attach(struct device *parent, struct device *self, void *aux)
{
	struct fdt_attach_args *faa = aux;
	struct zqreset_softc *sc = (struct zqreset_softc *)self;

	sc->sc_rm = regmap_bynode(OF_parent(faa->fa_node));
	if (sc->sc_rm == NULL) {
		printf(": can't get regmap\n");
		return;
	}

	printf("\n");

	zqreset_sc = sc;
	cpuresetfn = zqreset_cpureset;
}

void
zqreset_cpureset(void)
{
	struct zqreset_softc *sc = zqreset_sc;

	mtx_enter(&zynq_slcr_lock);
	zynq_slcr_write(sc->sc_rm, SLCR_PSS_RST_CTRL,
	    SLCR_PSS_RST_CTRL_SOFT_RST);
	mtx_leave(&zynq_slcr_lock);
}

uint32_t
zynq_slcr_read(struct regmap *rm, uint32_t reg)
{
	return regmap_read_4(rm, reg);
}

void
zynq_slcr_write(struct regmap *rm, uint32_t reg, uint32_t val)
{
	MUTEX_ASSERT_LOCKED(&zynq_slcr_lock);

	regmap_write_4(rm, SLCR_UNLOCK, SLCR_UNLOCK_KEY);
	regmap_write_4(rm, reg, val);
	regmap_write_4(rm, SLCR_LOCK, SLCR_LOCK_KEY);
}
