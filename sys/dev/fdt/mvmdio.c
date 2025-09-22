/*	$OpenBSD: mvmdio.c,v 1.5 2023/09/22 01:10:44 jsg Exp $	*/
/*	$NetBSD: if_mvneta.c,v 1.41 2015/04/15 10:15:40 hsuenaga Exp $	*/
/*
 * Copyright (c) 2007, 2008, 2013 KIYOHARA Takashi
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/mutex.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/simplebusvar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

#include <dev/fdt/if_mvnetareg.h>

#include <net/if.h>

#define MVNETA_READ(sc, reg) \
	bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg))
#define MVNETA_WRITE(sc, reg, val) \
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct mvmdio_softc {
	struct simplebus_softc sc_sbus;

	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_ioh;

	struct mutex sc_mtx;
	struct mii_bus sc_mii;
};

static int mvmdio_match(struct device *, void *, void *);
static void mvmdio_attach(struct device *, struct device *, void *);

int mvmdio_smi_readreg(struct device *, int, int);
void mvmdio_smi_writereg(struct device *, int, int, int);

struct cfdriver mvmdio_cd = {
	NULL, "mvmdio", DV_DULL
};

const struct cfattach mvmdio_ca = {
	sizeof (struct mvmdio_softc), mvmdio_match, mvmdio_attach,
};

static int
mvmdio_match(struct device *parent, void *cfdata, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "marvell,orion-mdio");
}

static void
mvmdio_attach(struct device *parent, struct device *self, void *aux)
{
	struct mvmdio_softc *sc = (struct mvmdio_softc *) self;
	struct fdt_attach_args *faa = aux;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	pinctrl_byname(faa->fa_node, "default");
	clock_enable_all(faa->fa_node);

	mtx_init(&sc->sc_mtx, IPL_NET);

	sc->sc_mii.md_node = faa->fa_node;
	sc->sc_mii.md_cookie = sc;
	sc->sc_mii.md_readreg = mvmdio_smi_readreg;
	sc->sc_mii.md_writereg = mvmdio_smi_writereg;
	mii_register(&sc->sc_mii);

	simplebus_attach(parent, &sc->sc_sbus.sc_dev, faa);
}

int
mvmdio_smi_readreg(struct device *dev, int phy, int reg)
{
	struct mvmdio_softc *sc = (struct mvmdio_softc *) dev;
	uint32_t smi, val;
	int i;

	mtx_enter(&sc->sc_mtx);

	for (i = 0; i < MVNETA_PHY_TIMEOUT; i++) {
		DELAY(1);
		if (!(MVNETA_READ(sc, 0) & MVNETA_SMI_BUSY))
			break;
	}
	if (i == MVNETA_PHY_TIMEOUT) {
		printf("%s: SMI busy timeout\n", sc->sc_sbus.sc_dev.dv_xname);
		mtx_leave(&sc->sc_mtx);
		return -1;
	}

	smi = MVNETA_SMI_PHYAD(phy) | MVNETA_SMI_REGAD(reg)
	    | MVNETA_SMI_OPCODE_READ;
	MVNETA_WRITE(sc, 0, smi);

	for (i = 0; i < MVNETA_PHY_TIMEOUT; i++) {
		DELAY(1);
		smi = MVNETA_READ(sc, 0);
		if (smi & MVNETA_SMI_READVALID)
			break;
	}

	mtx_leave(&sc->sc_mtx);

	val = smi & MVNETA_SMI_DATA_MASK;

	return val;
}

void
mvmdio_smi_writereg(struct device *dev, int phy, int reg, int val)
{
	struct mvmdio_softc *sc = (struct mvmdio_softc *) dev;
	uint32_t smi;
	int i;

	mtx_enter(&sc->sc_mtx);

	for (i = 0; i < MVNETA_PHY_TIMEOUT; i++) {
		DELAY(1);
		if (!(MVNETA_READ(sc, 0) & MVNETA_SMI_BUSY))
			break;
	}
	if (i == MVNETA_PHY_TIMEOUT) {
		printf("%s: SMI busy timeout\n", sc->sc_sbus.sc_dev.dv_xname);
		mtx_leave(&sc->sc_mtx);
		return;
	}

	smi = MVNETA_SMI_PHYAD(phy) | MVNETA_SMI_REGAD(reg) |
	    MVNETA_SMI_OPCODE_WRITE | (val & MVNETA_SMI_DATA_MASK);
	MVNETA_WRITE(sc, 0, smi);

	for (i = 0; i < MVNETA_PHY_TIMEOUT; i++) {
		DELAY(1);
		if (!(MVNETA_READ(sc, 0) & MVNETA_SMI_BUSY))
			break;
	}

	mtx_leave(&sc->sc_mtx);

	if (i == MVNETA_PHY_TIMEOUT)
		printf("%s: phy write timed out\n", sc->sc_sbus.sc_dev.dv_xname);
}
