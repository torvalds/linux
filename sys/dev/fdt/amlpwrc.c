/*	$OpenBSD: amlpwrc.c,v 1.4 2022/04/06 18:59:28 naddy Exp $	*/
/*
 * Copyright (c) 2020 Mark Kettenis <kettenis@openbsd.org>
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
#include <dev/ofw/ofw_power.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/fdt.h>

/* Power domain IDs */
#define PWRC_G12A_ETH_ID	1
#define PWRC_SM1_USB_ID		2
#define PWRC_SM1_PCIE_ID	3
#define PWRC_SM1_ETH_ID		6

/* Registers */
#define AO_RTI_GEN_PWR_SLEEP0		0x3a
#define AO_RTI_GEN_PWR_ISO0		0x3b
#define  AO_RTI_GEN_PWR_PCIE_MASK	(1 << 18)
#define  AO_RTI_GEN_PWR_USB_MASK	(1 << 17)
#define HHI_MEM_PD_REG0			0x40
#define  HHI_MEM_PD_USB_MASK		(0x3 << 30)
#define  HHI_MEM_PD_PCIE_MASK		(0xf << 26)
#define  HHI_MEM_PD_ETH_MASK		(0x3 << 2)

#define HREAD4(sc, reg)							\
	(regmap_read_4((sc)->sc_rm, (reg) << 2))
#define HWRITE4(sc, reg, val)						\
	regmap_write_4((sc)->sc_rm, (reg) << 2, (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct amlpwrc_softc {
	struct device		sc_dev;
	struct regmap		*sc_rm_hhi;
	struct regmap		*sc_rm_ao;
	uint32_t		sc_ao;
	int			sc_node;

	struct power_domain_device sc_pd;
};

int amlpwrc_match(struct device *, void *, void *);
void amlpwrc_attach(struct device *, struct device *, void *);

const struct cfattach amlpwrc_ca = {
	sizeof (struct amlpwrc_softc), amlpwrc_match, amlpwrc_attach
};

struct cfdriver amlpwrc_cd = {
	NULL, "amlpwrc", DV_DULL
};

void	amlpwrc_g12a_enable(void *, uint32_t *, int);
void	amlpwrc_sm1_enable(void *, uint32_t *, int);

int
amlpwrc_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "amlogic,meson-g12a-pwrc") ||
	    OF_is_compatible(faa->fa_node, "amlogic,meson-sm1-pwrc"));
}

void
amlpwrc_attach(struct device *parent, struct device *self, void *aux)
{
	struct amlpwrc_softc *sc = (struct amlpwrc_softc *)self;
	struct fdt_attach_args *faa = aux;

	/*
	 * We can't lookup the AO regmap at this point since the
	 * syscon(4) instance that provides it attaches after us.
	 */
	sc->sc_rm_hhi = regmap_bynode(OF_parent(faa->fa_node));
	sc->sc_ao = OF_getpropint(faa->fa_node, "amlogic,ao-sysctrl", 0);
	if (sc->sc_rm_hhi == NULL || sc->sc_ao == 0) {
		printf(": no registers\n");
		return;
	}

	sc->sc_node = faa->fa_node;
	printf("\n");

	sc->sc_pd.pd_node = faa->fa_node;
	sc->sc_pd.pd_cookie = sc;
	if (OF_is_compatible(faa->fa_node, "amlogic,meson-g12a-pwrc"))
		sc->sc_pd.pd_enable = amlpwrc_g12a_enable;
	else if (OF_is_compatible(faa->fa_node, "amlogic,meson-sm1-pwrc"))
		sc->sc_pd.pd_enable = amlpwrc_sm1_enable;
	power_domain_register(&sc->sc_pd);
}

static inline void
amlpwrc_toggle(struct regmap *rm, bus_size_t reg, uint32_t mask, int on)
{
	uint32_t val;
	
	val = regmap_read_4(rm, reg << 2);
	if (on)
		val &= ~mask;
	else
		val |= mask;
	regmap_write_4(rm, reg << 2, val);
}

void
amlpwrc_g12a_enable(void *cookie, uint32_t *cells, int on)
{
	struct amlpwrc_softc *sc = cookie;
	uint32_t idx = cells[0];

	sc->sc_rm_ao = regmap_byphandle(sc->sc_ao);
	KASSERT(sc->sc_rm_ao);

	switch (idx) {
	case PWRC_G12A_ETH_ID:
		amlpwrc_toggle(sc->sc_rm_hhi, HHI_MEM_PD_REG0,
		    HHI_MEM_PD_ETH_MASK, on);
		delay(20);
		return;
	}

	printf("%s: 0x%08x\n", __func__, idx);
}

void
amlpwrc_sm1_enable(void *cookie, uint32_t *cells, int on)
{
	struct amlpwrc_softc *sc = cookie;
	uint32_t idx = cells[0];

	sc->sc_rm_ao = regmap_byphandle(sc->sc_ao);
	KASSERT(sc->sc_rm_ao);

	switch (idx) {
	case PWRC_SM1_USB_ID:
		amlpwrc_toggle(sc->sc_rm_ao, AO_RTI_GEN_PWR_SLEEP0,
		    AO_RTI_GEN_PWR_USB_MASK, on);
		delay(20);
		amlpwrc_toggle(sc->sc_rm_hhi, HHI_MEM_PD_REG0,
		    HHI_MEM_PD_USB_MASK, on);
		delay(20);
		amlpwrc_toggle(sc->sc_rm_ao, AO_RTI_GEN_PWR_ISO0,
		    AO_RTI_GEN_PWR_USB_MASK, on);
		return;
	case PWRC_SM1_PCIE_ID:
		amlpwrc_toggle(sc->sc_rm_ao, AO_RTI_GEN_PWR_SLEEP0,
		    AO_RTI_GEN_PWR_PCIE_MASK, on);
		delay(20);
		amlpwrc_toggle(sc->sc_rm_hhi, HHI_MEM_PD_REG0,
		    HHI_MEM_PD_PCIE_MASK, on);
		delay(20);
		amlpwrc_toggle(sc->sc_rm_ao, AO_RTI_GEN_PWR_ISO0,
		    AO_RTI_GEN_PWR_PCIE_MASK, on);
		return;
	case PWRC_SM1_ETH_ID:
		amlpwrc_toggle(sc->sc_rm_hhi, HHI_MEM_PD_REG0,
		    HHI_MEM_PD_ETH_MASK, on);
		delay(20);
		return;
	}

	printf("%s: 0x%08x\n", __func__, idx);
}
