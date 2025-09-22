/* $OpenBSD: mvmbus.c,v 1.5 2023/09/22 01:10:43 jsg Exp $ */
/*
 * Copyright (c) 2016 Patrick Wildt <patrick@blueri.se>
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
#include <sys/malloc.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <machine/simplebusvar.h>
#include <armv7/marvell/mvmbusvar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

#define MVMBUS_XP_WINDOW(i)	(((i) < 8) ? i << 4 : 0x90 + (((i) - 8) << 3))

#define MVMBUS_WINDOW_CTRL		0x0000
#define  MVMBUS_WINDOW_CTRL_ENABLE		(1 << 0)
#define  MVMBUS_WINDOW_CTRL_TARGET_MASK		0xf0
#define  MVMBUS_WINDOW_CTRL_TARGET_SHIFT	4
#define  MVMBUS_WINDOW_CTRL_ATTR_MASK		0xff00
#define  MVMBUS_WINDOW_CTRL_ATTR_SHIFT		8
#define  MVMBUS_WINDOW_CTRL_SIZE_MASK		0xffff0000
#define  MVMBUS_WINDOW_CTRL_SIZE_SHIFT		16
#define MVMBUS_WINDOW_BASE		0x0004
#define  MVMBUS_WINDOW_BASE_LOW			0xffff0000
#define  MVMBUS_WINDOW_BASE_HIGH		0xf
#define MVMBUS_WINDOW_REMAP_LO		0x0008
#define  MVMBUS_WINDOW_REMAP_LO_LOW		0xffff0000
#define MVMBUS_WINDOW_REMAP_HI		0x000c

#define MVMBUS_SDRAM_BASE(i)		(0x0000 + ((i) << 3))
#define  MVMBUS_SDRAM_BASE_HIGH_MASK		0xf
#define  MVMBUS_SDRAM_BASE_LOW_MASK		0xff000000
#define MVMBUS_SDRAM_SIZE(i)		(0x0004 + ((i) << 3))
#define  MVMBUS_SDRAM_SIZE_ENABLED		(1 << 0)
#define  MVMBUS_SDRAM_SIZE_MASK			0xff000000

struct mvmbus_softc {
	struct simplebus_softc	 sc_sbus;
	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_mbus_ioh;
	bus_space_handle_t	 sc_sdram_ioh;
	bus_space_handle_t	 sc_bridge_ioh;

	int			 sc_num_wins;
	int			 sc_num_remappable_wins;

	struct mbus_dram_info	 sc_dram_info;
	struct mbus_window {
		int		 enabled;
		paddr_t		 base;
		size_t		 size;
		uint8_t		 target;
		uint8_t		 attr;
	}			*sc_windows;
};

int	 mvmbus_match(struct device *, void *, void *);
void	 mvmbus_attach(struct device *, struct device *, void *);

void	 mvmbus_parse_ranges(struct mvmbus_softc *, struct fdt_attach_args *);
void	 mvmbus_alloc_window(struct mvmbus_softc *, paddr_t, size_t, paddr_t,
	    uint8_t, uint8_t);
void	 mvmbus_setup_window(struct mvmbus_softc *, int, paddr_t, size_t,
	    paddr_t, uint8_t, uint8_t);
void	 mvmbus_disable_window(struct mvmbus_softc *, int);
int	 mvmbus_window_conflicts(struct mvmbus_softc *, paddr_t, size_t);
int	 mvmbus_window_is_free(struct mvmbus_softc *, int);
int	 mvmbus_find_window(struct mvmbus_softc *, paddr_t, size_t);
void	 mvmbus_parse_dram_info(struct mvmbus_softc *);

struct mvmbus_softc *mvmbus_sc;
struct mbus_dram_info *mvmbus_dram_info;
uint32_t mvmbus_pcie_mem_aperture[2];
uint32_t mvmbus_pcie_io_aperture[2];

const struct cfattach mvmbus_ca = {
	sizeof (struct mvmbus_softc), mvmbus_match, mvmbus_attach
};

struct cfdriver mvmbus_cd = {
	NULL, "mvmbus", DV_DULL
};

int
mvmbus_match(struct device *parent, void *cfdata, void *aux)
{
	struct fdt_attach_args *faa = aux;

	if (OF_is_compatible(faa->fa_node, "marvell,armada370-mbus") ||
	    OF_is_compatible(faa->fa_node, "marvell,armada380-mbus") ||
	    OF_is_compatible(faa->fa_node, "marvell,armadaxp-mbus"))
		return 10;

	return 0;
}

void
mvmbus_attach(struct device *parent, struct device *self, void *args)
{
	struct mvmbus_softc *sc = (struct mvmbus_softc *)self;
	struct fdt_attach_args *faa = args;
	struct fdt_reg reg;
	void *mbusc;
	int i;

	mvmbus_sc = sc;
	sc->sc_iot = faa->fa_iot;

	/* The registers are in the controller itself, find it. */
	mbusc = fdt_find_phandle(OF_getpropint(faa->fa_node, "controller", 0));
	if (mbusc == NULL)
		panic("%s: cannot find mbus controller", __func__);

	if (fdt_get_reg(mbusc, 0, &reg))
		panic("%s: could not extract memory data from FDT",
		    __func__);

	if (bus_space_map(sc->sc_iot, reg.addr, reg.size, 0, &sc->sc_mbus_ioh))
		panic("%s: bus_space_map failed!", __func__);

	if (fdt_get_reg(mbusc, 1, &reg))
		panic("%s: could not extract memory data from FDT",
		    __func__);

	if (bus_space_map(sc->sc_iot, reg.addr, reg.size, 0, &sc->sc_sdram_ioh))
		panic("%s: bus_space_map failed!", __func__);

	/* Bridge mapping is optional. */
	if (fdt_get_reg(mbusc, 2, &reg) == 0)
		if (bus_space_map(sc->sc_iot, reg.addr, reg.size, 0,
		    &sc->sc_bridge_ioh))
			sc->sc_bridge_ioh = 0;

	OF_getpropintarray(faa->fa_node, "pcie-mem-aperture",
	    mvmbus_pcie_mem_aperture, sizeof(mvmbus_pcie_mem_aperture));
	OF_getpropintarray(faa->fa_node, "pcie-io-aperture",
	    mvmbus_pcie_io_aperture, sizeof(mvmbus_pcie_io_aperture));

	/* TODO: support more than the armada 370/380/xp */
	sc->sc_num_wins = 20;
	sc->sc_num_remappable_wins = 8;

	sc->sc_windows = mallocarray(sc->sc_num_wins, sizeof(*sc->sc_windows),
	    M_DEVBUF, M_ZERO | M_WAITOK);

	for (i = 0; i < sc->sc_num_wins; i++)
		mvmbus_disable_window(sc, i);

	mvmbus_parse_dram_info(sc);
	mvmbus_dram_info = &sc->sc_dram_info;

	mvmbus_parse_ranges(sc, faa);

	/* We are simple-bus compatible, so just attach it. */
	simplebus_attach(parent, &sc->sc_sbus.sc_dev, faa);
}

void
mvmbus_parse_ranges(struct mvmbus_softc *sc, struct fdt_attach_args *faa)
{
	int pac, psc, cac, csc, rlen, rone, *range;
	uint32_t *ranges;
	int rangeslen;

	rangeslen = OF_getproplen(faa->fa_node, "ranges");
	if (rangeslen <= 0 || (rangeslen % sizeof(uint32_t)))
		panic("%s: unsupported ranges format",
		    sc->sc_sbus.sc_dev.dv_xname);

	ranges = malloc(rangeslen, M_TEMP, M_WAITOK);
	OF_getpropintarray(faa->fa_node, "ranges", ranges, rangeslen);

	cac = OF_getpropint(faa->fa_node, "#address-cells",
	    faa->fa_acells);
	csc = OF_getpropint(faa->fa_node, "#size-cells",
	    faa->fa_scells);
	pac = faa->fa_acells;
	psc = faa->fa_scells;

	/* Must have at least one range. */
	rone = pac + cac + csc;
	rlen = rangeslen / sizeof(uint32_t);
	if (rlen < rone)
		return;

	/* For each range. */
	for (range = ranges; rlen >= rone; rlen -= rone, range += rone) {
		uint32_t window = range[0];
		if (window & 0xf0000000)
			continue;

		uint32_t size = range[cac + pac];
		if (csc == 2)
			size = range[cac + pac + 1];

		/* All good, extract to address and translate. */
		uint32_t base = range[cac];
		if (pac == 2)
			base = range[cac + 1];

		uint8_t target = (window & 0x0f000000) >> 24;
		uint8_t attr = (window & 0x00ff0000) >> 16;

		mvmbus_alloc_window(sc, base, size, MVMBUS_NO_REMAP,
		    target, attr);
	}
}

int
mvmbus_window_is_free(struct mvmbus_softc *sc, int window)
{
	/*
	 * On Armada XP systems, window 13 is a remappable window.  For this
	 * window to work we need to configure the remap register.  Since we
	 * don't do this yet, make sure we don't use this window.
	 */
	if (window == 13)
		return 0;
	return !sc->sc_windows[window].enabled;
}

void
mvmbus_alloc_window(struct mvmbus_softc *sc, paddr_t base, size_t size,
    paddr_t remap, uint8_t target, uint8_t attr)
{
	int win;

	if (mvmbus_window_conflicts(sc, base, size)) {
		printf("%s: window conflicts with another window\n",
		    sc->sc_sbus.sc_dev.dv_xname);
		return;
	}

	/* If no remap is wanted, use unremappable windows. */
	if (remap == MVMBUS_NO_REMAP)
		for (win = sc->sc_num_remappable_wins;
		     win < sc->sc_num_wins; win++)
			if (mvmbus_window_is_free(sc, win))
				return mvmbus_setup_window(sc, win, base, size,
				    remap, target, attr);

	for (win = 0; win < sc->sc_num_wins; win++)
		if (mvmbus_window_is_free(sc, win))
			return mvmbus_setup_window(sc, win, base, size,
				    remap, target, attr);

	printf("%s: no free window available\n",
	    sc->sc_sbus.sc_dev.dv_xname);
}

void
mvmbus_setup_window(struct mvmbus_softc *sc, int window, paddr_t base,
    size_t size, paddr_t remap, uint8_t target, uint8_t attr)
{
	bus_space_write_4(sc->sc_iot, sc->sc_mbus_ioh,
	    MVMBUS_XP_WINDOW(window) + MVMBUS_WINDOW_BASE,
	    base & MVMBUS_WINDOW_BASE_LOW);
	bus_space_write_4(sc->sc_iot, sc->sc_mbus_ioh,
	    MVMBUS_XP_WINDOW(window) + MVMBUS_WINDOW_CTRL,
	    ((size - 1) & MVMBUS_WINDOW_CTRL_SIZE_MASK) |
	    attr << MVMBUS_WINDOW_CTRL_ATTR_SHIFT |
	    target << MVMBUS_WINDOW_CTRL_TARGET_SHIFT |
	    MVMBUS_WINDOW_CTRL_ENABLE);

	if (window < sc->sc_num_remappable_wins) {
		if (remap == MVMBUS_NO_REMAP)
			remap = base;
		bus_space_write_4(sc->sc_iot, sc->sc_mbus_ioh,
		    MVMBUS_XP_WINDOW(window) + MVMBUS_WINDOW_REMAP_LO,
		    remap & MVMBUS_WINDOW_REMAP_LO_LOW);
		bus_space_write_4(sc->sc_iot, sc->sc_mbus_ioh,
		    MVMBUS_XP_WINDOW(window) + MVMBUS_WINDOW_REMAP_HI, 0);
	}

	sc->sc_windows[window].enabled = 1;
	sc->sc_windows[window].base = base;
	sc->sc_windows[window].size = size;
	sc->sc_windows[window].target = target;
	sc->sc_windows[window].attr = attr;
}

void
mvmbus_disable_window(struct mvmbus_softc *sc, int window)
{
	bus_space_write_4(sc->sc_iot, sc->sc_mbus_ioh,
	    MVMBUS_XP_WINDOW(window) + MVMBUS_WINDOW_BASE, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_mbus_ioh,
	    MVMBUS_XP_WINDOW(window) + MVMBUS_WINDOW_CTRL, 0);

	if (window < sc->sc_num_remappable_wins) {
		bus_space_write_4(sc->sc_iot, sc->sc_mbus_ioh,
		    MVMBUS_XP_WINDOW(window) + MVMBUS_WINDOW_REMAP_LO, 0);
		bus_space_write_4(sc->sc_iot, sc->sc_mbus_ioh,
		    MVMBUS_XP_WINDOW(window) + MVMBUS_WINDOW_REMAP_HI, 0);
	}

	sc->sc_windows[window].enabled = 0;
}

int
mvmbus_window_conflicts(struct mvmbus_softc *sc, paddr_t base, size_t size)
{
	int i;

	for (i = 0; i < sc->sc_num_wins; i++) {
		if (!sc->sc_windows[i].enabled)
			continue;

		if (base < (sc->sc_windows[i].base + sc->sc_windows[i].size) &&
		    (base + size) > sc->sc_windows[i].base)
			return 1;
	}

	return 0;
}

int
mvmbus_find_window(struct mvmbus_softc *sc, paddr_t base, size_t size)
{
	int win;

	for (win = 0; win < sc->sc_num_wins; win++) {
		if (!sc->sc_windows[win].enabled)
			continue;
		if (sc->sc_windows[win].base != base)
			continue;
		if (sc->sc_windows[win].size != size)
			continue;
		break;
	}

	if (win == sc->sc_num_wins)
		return -1;
	return win;
}

void
mvmbus_add_window(paddr_t base, size_t size, paddr_t remap,
    uint8_t target, uint8_t attr)
{
	struct mvmbus_softc *sc = mvmbus_sc;

	KASSERT(sc != NULL);
	mvmbus_alloc_window(sc, base, size, remap, target, attr);
}

void
mvmbus_del_window(paddr_t base, size_t size)
{
	struct mvmbus_softc *sc = mvmbus_sc;
	int win;

	KASSERT(sc != NULL);
	win = mvmbus_find_window(sc, base, size);
	if (win >= 0)
		mvmbus_disable_window(sc, win);
}

void
mvmbus_parse_dram_info(struct mvmbus_softc *sc)
{
	int i, cs = 0;

	sc->sc_dram_info.targetid = 0; /* DDR */

	for (i = 0; i < 4; i++) {
		uint32_t base = bus_space_read_4(sc->sc_iot, sc->sc_sdram_ioh,
		    MVMBUS_SDRAM_BASE(i));
		uint32_t size = bus_space_read_4(sc->sc_iot, sc->sc_sdram_ioh,
		    MVMBUS_SDRAM_SIZE(i));

		if (!(size & MVMBUS_SDRAM_SIZE_ENABLED))
			continue;

		if (base & MVMBUS_SDRAM_BASE_HIGH_MASK)
			continue;

		struct mbus_dram_window *win = &sc->sc_dram_info.cs[cs++];
		win->index = i;
		win->attr = 0xf & ~(1 << i); /* XXX: coherency? */
		win->base = base & MVMBUS_SDRAM_BASE_LOW_MASK;
		win->size = (size | MVMBUS_SDRAM_SIZE_MASK) + 1;
	}

	sc->sc_dram_info.numcs = cs;
}
