/* $OpenBSD: imxsrc.c,v 1.6 2022/06/28 23:43:12 naddy Exp $ */
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

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/fdt.h>

#define IMX51_RESET_GPU				0
#define IMX51_RESET_VPU				1
#define IMX51_RESET_IPU1			2
#define IMX51_RESET_OPEN_VG			3
#define IMX51_RESET_IPU2			4

#define SRC_SCR					0x00
#define  SRC_SCR_SW_GPU_RST			(1 << 1)
#define  SRC_SCR_SW_VPU_RST			(1 << 2)
#define  SRC_SCR_SW_IPU1_RST			(1 << 3)
#define  SRC_SCR_SW_OPEN_VG_RST			(1 << 4)
#define  SRC_SCR_SW_IPU2_RST			(1 << 12)

#define IMX8M_RESET_PCIEPHY			26
#define IMX8M_RESET_PCIEPHY_PERST		27
#define IMX8M_RESET_PCIE_CTRL_APPS_EN		28
#define IMX8M_RESET_PCIE_CTRL_APPS_TURNOFF	29
#define IMX8M_RESET_PCIE2PHY			34
#define IMX8M_RESET_PCIE2PHY_PERST		35
#define IMX8M_RESET_PCIE2_CTRL_APPS_EN		36
#define IMX8M_RESET_PCIE2_CTRL_APPS_TURNOFF	37

#define SRC_PCIE1_RCR				0x2c
#define SRC_PCIE2_RCR				0x48
#define  SRC_PCIE_RCR_PCIEPHY_G_RST			(1 << 1)
#define  SRC_PCIE_RCR_PCIEPHY_BTN			(1 << 2)
#define  SRC_PCIE_RCR_PCIEPHY_PERST			(1 << 3)
#define  SRC_PCIE_RCR_PCIE_CTRL_APPS_EN			(1 << 6)
#define  SRC_PCIE_RCR_PCIE_CTRL_APPS_TURNOFF		(1 << 11)

struct imxsrc_reset {
	uint32_t	reg;
	uint32_t	bit;
};

const struct imxsrc_reset imx51_resets[] = {
	[IMX51_RESET_GPU] = { SRC_SCR, SRC_SCR_SW_GPU_RST },
	[IMX51_RESET_VPU] = { SRC_SCR, SRC_SCR_SW_VPU_RST },
	[IMX51_RESET_IPU1] = { SRC_SCR, SRC_SCR_SW_IPU1_RST },
	[IMX51_RESET_OPEN_VG] = { SRC_SCR, SRC_SCR_SW_OPEN_VG_RST },
	[IMX51_RESET_IPU2] = { SRC_SCR, SRC_SCR_SW_IPU2_RST },
};

const struct imxsrc_reset imx8m_resets[] = {
	[IMX8M_RESET_PCIEPHY] = { SRC_PCIE1_RCR,
	    SRC_PCIE_RCR_PCIEPHY_G_RST | SRC_PCIE_RCR_PCIEPHY_BTN },
	[IMX8M_RESET_PCIEPHY_PERST] = { SRC_PCIE1_RCR,
	    SRC_PCIE_RCR_PCIEPHY_PERST },
	[IMX8M_RESET_PCIE_CTRL_APPS_EN] = { SRC_PCIE1_RCR,
	    SRC_PCIE_RCR_PCIE_CTRL_APPS_EN },
	[IMX8M_RESET_PCIE_CTRL_APPS_TURNOFF] = { SRC_PCIE1_RCR,
	    SRC_PCIE_RCR_PCIE_CTRL_APPS_TURNOFF },
	[IMX8M_RESET_PCIE2PHY] = { SRC_PCIE2_RCR,
	    SRC_PCIE_RCR_PCIEPHY_G_RST | SRC_PCIE_RCR_PCIEPHY_BTN },
	[IMX8M_RESET_PCIE2PHY_PERST] = { SRC_PCIE2_RCR,
	    SRC_PCIE_RCR_PCIEPHY_PERST },
	[IMX8M_RESET_PCIE2_CTRL_APPS_EN] = { SRC_PCIE2_RCR,
	    SRC_PCIE_RCR_PCIE_CTRL_APPS_EN },
	[IMX8M_RESET_PCIE2_CTRL_APPS_TURNOFF] = { SRC_PCIE2_RCR,
	    SRC_PCIE_RCR_PCIE_CTRL_APPS_TURNOFF },
};

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct imxsrc_softc {
	struct device		 sc_dev;
	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_ioh;
	struct reset_device	 sc_rd;
	const struct imxsrc_reset *sc_resets;
	int			 sc_nresets;
};

int imxsrc_match(struct device *, void *, void *);
void imxsrc_attach(struct device *, struct device *, void *);
void imxsrc_reset(void *, uint32_t *, int);

const struct cfattach	imxsrc_ca = {
	sizeof (struct imxsrc_softc), imxsrc_match, imxsrc_attach
};

struct cfdriver imxsrc_cd = {
	NULL, "imxsrc", DV_DULL
};

int
imxsrc_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	if (OF_is_compatible(faa->fa_node, "fsl,imx51-src") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx8mq-src"))		
		return 10;	/* Must beat syscon(4). */

	return 0;
}

void
imxsrc_attach(struct device *parent, struct device *self, void *aux)
{
	struct imxsrc_softc *sc = (struct imxsrc_softc *)self;
	struct fdt_attach_args *faa = aux;

	KASSERT(faa->fa_nreg >= 1);

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("%s: bus_space_map failed!", __func__);

	if (OF_is_compatible(faa->fa_node, "fsl,imx51-src")) {
		sc->sc_resets = imx51_resets;
		sc->sc_nresets = nitems(imx51_resets);
	} else {
		sc->sc_resets = imx8m_resets;
		sc->sc_nresets = nitems(imx8m_resets);
	}

	sc->sc_rd.rd_node = faa->fa_node;
	sc->sc_rd.rd_cookie = sc;
	sc->sc_rd.rd_reset = imxsrc_reset;
	reset_register(&sc->sc_rd);

	printf("\n");
}

void
imxsrc_reset(void *cookie, uint32_t *cells, int assert)
{
	struct imxsrc_softc *sc = cookie;
	int idx = cells[0];
	uint32_t reg;

	if (idx >= sc->sc_nresets || sc->sc_resets[idx].bit == 0) {
		printf("%s: 0x%08x\n", __func__, idx);
		return;
	}

	switch (idx) {
	case IMX8M_RESET_PCIEPHY:
	case IMX8M_RESET_PCIE2PHY:
		if (!assert)
			delay(10);
		break;
	case IMX8M_RESET_PCIE_CTRL_APPS_EN:
	case IMX8M_RESET_PCIE2_CTRL_APPS_EN:
		assert = !assert;
		break;
	}

	reg = HREAD4(sc, sc->sc_resets[idx].reg);
	if (assert)
		reg |= sc->sc_resets[idx].bit;
	else
		reg &= ~sc->sc_resets[idx].bit;
	HWRITE4(sc, sc->sc_resets[idx].reg, reg);
}
