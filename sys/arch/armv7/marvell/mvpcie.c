/*	$OpenBSD: mvpcie.c,v 1.6 2022/02/13 16:44:50 tobhe Exp $	*/
/*
 * Copyright (c) 2018 Patrick Wildt <patrick@blueri.se>
 * Copyright (c) 2018 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/extent.h>
#include <sys/malloc.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <armv7/marvell/mvmbusvar.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/ppbreg.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_gpio.h>

/* Registers */
#define PCIE_DEV_ID			0x0000
#define PCIE_CMD			0x0004
#define PCIE_DEV_REV			0x0008
#define PCIE_BAR_LO(n)			(0x0010 + ((n) << 3))
#define PCIE_BAR_HI(n)			(0x0014 + ((n) << 3))
#define PCIE_BAR_CTRL(n)		(0x1804 + (((n) - 1) * 4))
#define PCIE_WIN04_CTRL(n)		(0x1820 + ((n) << 4))
#define PCIE_WIN04_BASE(n)		(0x1824 + ((n) << 4))
#define PCIE_WIN04_REMAP(n)		(0x182c + ((n) << 4))
#define PCIE_WIN5_CTRL			0x1880
#define PCIE_WIN5_BASE			0x1884
#define PCIE_WIN5_REMAP			0x188c
#define PCIE_CONF_ADDR			0x18f8
#define  PCIE_CONF_ADDR_EN			0x80000000
#define  PCIE_CONF_REG(r)			((((r) & 0xf00) << 16) | ((r) & 0xfc))
#define  PCIE_CONF_BUS(b)			(((b) & 0xff) << 16)
#define  PCIE_CONF_DEV(d)			(((d) & 0x1f) << 11)
#define  PCIE_CONF_FUNC(f)			(((f) & 0x7) << 8)
#define PCIE_CONF_DATA			0x18fc
#define PCIE_MASK			0x1910
#define  PCIE_MASK_ENABLE_INTS			(0xf << 24)
#define PCIE_STAT			0x1a04
#define  PCIE_STAT_LINK_DOWN			(1 << 0)
#define  PCIE_STAT_BUS_SHIFT			8
#define  PCIE_STAT_BUS_MASK			0xff
#define  PCIE_STAT_DEV_SHIFT			16
#define  PCIE_STAT_DEV_MASK			0x1f

#define PCIE_SWCAP			0x0040

#define PCIE_TARGET(target)		(((target) & 0xf) << 4)
#define PCIE_ATTR(attr)			(((attr) & 0xff) << 8)
#define PCIE_BASEADDR(base)		((base) & 0xffff0000)
#define PCIE_SIZE(size)			(((size) - 1) & 0xffff0000)
#define PCIE_WINEN			(1 << 0)

#define HREAD4(po, reg)							\
	(bus_space_read_4((po)->po_iot, (po)->po_ioh, (reg)))
#define HWRITE4(po, reg, val)						\
	bus_space_write_4((po)->po_iot, (po)->po_ioh, (reg), (val))
#define HSET4(po, reg, bits)						\
	HWRITE4((po), (reg), HREAD4((po), (reg)) | (bits))
#define HCLR4(po, reg, bits)						\
	HWRITE4((po), (reg), HREAD4((po), (reg)) & ~(bits))

struct mvpcie_softc;

struct mvpcie_range {
	uint32_t		 flags;
	uint32_t		 slot;
	uint32_t		 pci_base;
	uint64_t		 phys_base;
	uint64_t		 size;
};

struct mvpcie_port {
	struct mvpcie_softc	*po_sc;
	bus_space_tag_t		 po_iot;
	bus_space_handle_t	 po_ioh;
	bus_dma_tag_t		 po_dmat;
	int			 po_node;

	int			 po_port;
	int			 po_lane;
	int			 po_dev;
	int			 po_fn;

	uint32_t		*po_gpio;
	int			 po_gpiolen;

	struct arm32_pci_chipset po_pc;
	int			 po_bus;

	uint32_t		 po_bridge_command;
	uint32_t		 po_bridge_bar0;
	uint32_t		 po_bridge_bar1;
	uint32_t		 po_bridge_businfo;
	uint32_t		 po_bridge_iobase;
	uint32_t		 po_bridge_iobaseupper;
	uint32_t		 po_bridge_iolimit;
	uint32_t		 po_bridge_iolimitupper;
	uint32_t		 po_bridge_membase;
	uint32_t		 po_bridge_memlimit;

	uint32_t		 po_win_iotarget;
	uint32_t		 po_win_ioattr;
	uint32_t		 po_win_memtarget;
	uint32_t		 po_win_memattr;

	uint32_t		 po_win_iobase;
	uint32_t		 po_win_iosize;
	uint32_t		 po_win_ioremap;
	uint32_t		 po_win_membase;
	uint32_t		 po_win_memsize;
};

struct mvpcie_softc {
	struct device		 sc_dev;
	bus_space_tag_t		 sc_iot;
	bus_dma_tag_t		 sc_dmat;

	int			 sc_node;
	int			 sc_acells;
	int			 sc_scells;
	int			 sc_pacells;
	int			 sc_pscells;
	struct mvpcie_range	*sc_ranges;
	int			 sc_rangeslen;

	struct extent		*sc_busex;
	struct extent		*sc_memex;
	struct extent		*sc_ioex;
	char			 sc_busex_name[32];
	char			 sc_ioex_name[32];
	char			 sc_memex_name[32];

	int			 sc_nports;
	struct mvpcie_port	*sc_ports;
};

int mvpcie_match(struct device *, void *, void *);
void mvpcie_attach(struct device *, struct device *, void *);

const struct cfattach	mvpcie_ca = {
	sizeof (struct mvpcie_softc), mvpcie_match, mvpcie_attach
};

struct cfdriver mvpcie_cd = {
	NULL, "mvpcie", DV_DULL
};

int
mvpcie_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "marvell,armada-370-pcie");
}

void	 mvpcie_port_attach(struct mvpcie_softc *, struct mvpcie_port *, int);
void	 mvpcie_wininit(struct mvpcie_port *);
void	 mvpcie_add_windows(paddr_t, size_t, paddr_t, uint8_t, uint8_t);
void	 mvpcie_del_windows(paddr_t, size_t);
void	 mvpcie_io_change(struct mvpcie_port *);
void	 mvpcie_mem_change(struct mvpcie_port *);
int	 mvpcie_link_up(struct mvpcie_port *);

void	 mvpcie_attach_hook(struct device *, struct device *,
	    struct pcibus_attach_args *);
int	 mvpcie_bus_maxdevs(void *, int);
pcitag_t mvpcie_make_tag(void *, int, int, int);
void	 mvpcie_decompose_tag(void *, pcitag_t, int *, int *, int *);
int	 mvpcie_conf_size(void *, pcitag_t);
pcireg_t mvpcie_conf_read(void *, pcitag_t, int);
void	 mvpcie_conf_write(void *, pcitag_t, int, pcireg_t);
int	 mvpcie_probe_device_hook(void *, struct pci_attach_args *);

int	 mvpcie_intr_map(struct pci_attach_args *, pci_intr_handle_t *);
int	 mvpcie_intr_map_msi(struct pci_attach_args *, pci_intr_handle_t *);
int	 mvpcie_intr_map_msix(struct pci_attach_args *, int,
	    pci_intr_handle_t *);
const char *mvpcie_intr_string(void *, pci_intr_handle_t);
void	*mvpcie_intr_establish(void *, pci_intr_handle_t, int,
	    struct cpu_info *ci, int (*)(void *), void *, char *);
void	 mvpcie_intr_disestablish(void *, void *);

void
mvpcie_attach(struct device *parent, struct device *self, void *aux)
{
	struct mvpcie_softc *sc = (struct mvpcie_softc *)self;
	struct fdt_attach_args *faa = aux;
	uint32_t bus_range[2];
	uint32_t *ranges;
	int i, j, nranges, rangeslen;
	char buf[32];
	int node;

	sc->sc_iot = faa->fa_iot;
	sc->sc_dmat = faa->fa_dmat;
	sc->sc_node = faa->fa_node;

	sc->sc_acells = OF_getpropint(sc->sc_node, "#address-cells",
	    faa->fa_acells);
	sc->sc_scells = OF_getpropint(sc->sc_node, "#size-cells",
	    faa->fa_scells);
	sc->sc_pacells = faa->fa_acells;
	sc->sc_pscells = faa->fa_scells;

	rangeslen = OF_getproplen(sc->sc_node, "ranges");
	if (rangeslen <= 0 || (rangeslen % sizeof(uint32_t)) ||
	     (rangeslen / sizeof(uint32_t)) % (sc->sc_acells +
	     sc->sc_pacells + sc->sc_scells)) {
		printf(": invalid ranges property\n");
		return;
	}

	ranges = malloc(rangeslen, M_TEMP, M_WAITOK);
	OF_getpropintarray(sc->sc_node, "ranges", ranges,
	    rangeslen);

	nranges = (rangeslen / sizeof(uint32_t)) /
	    (sc->sc_acells + sc->sc_pacells + sc->sc_scells);
	sc->sc_ranges = mallocarray(nranges,
	    sizeof(struct mvpcie_range), M_TEMP, M_WAITOK);
	sc->sc_rangeslen = nranges;

	for (i = 0, j = 0; i < nranges; i++) {
		sc->sc_ranges[i].flags = ranges[j++];
		sc->sc_ranges[i].slot = ranges[j++];
		sc->sc_ranges[i].pci_base = ranges[j++];
		sc->sc_ranges[i].phys_base = ranges[j++];
		if (sc->sc_pacells == 2) {
			sc->sc_ranges[i].phys_base <<= 32;
			sc->sc_ranges[i].phys_base |= ranges[j++];
		}
		sc->sc_ranges[i].size = ranges[j++];
		if (sc->sc_scells == 2) {
			sc->sc_ranges[i].size <<= 32;
			sc->sc_ranges[i].size |= ranges[j++];
		}
	}

	free(ranges, M_TEMP, rangeslen);

	printf("\n");

	/* Create extents for our address spaces. */
	snprintf(sc->sc_busex_name, sizeof(sc->sc_busex_name),
	    "%s pcibus", sc->sc_dev.dv_xname);
	snprintf(sc->sc_ioex_name, sizeof(sc->sc_ioex_name),
	    "%s pciio", sc->sc_dev.dv_xname);
	snprintf(sc->sc_memex_name, sizeof(sc->sc_memex_name),
	    "%s pcimem", sc->sc_dev.dv_xname);
	sc->sc_busex = extent_create(sc->sc_busex_name, 0, 255,
	    M_DEVBUF, NULL, 0, EX_WAITOK | EX_FILLED);
	sc->sc_ioex = extent_create(sc->sc_ioex_name, 0, 0xffffffff,
	    M_DEVBUF, NULL, 0, EX_WAITOK | EX_FILLED);
	sc->sc_memex = extent_create(sc->sc_memex_name, 0, (u_long)-1,
	    M_DEVBUF, NULL, 0, EX_WAITOK | EX_FILLED);

	/* Set up bus range. */
	if (OF_getpropintarray(sc->sc_node, "bus-range", bus_range,
	    sizeof(bus_range)) != sizeof(bus_range) ||
	    bus_range[0] >= 32 || bus_range[1] >= 32) {
		bus_range[0] = 0;
		bus_range[1] = 31;
	}
	extent_free(sc->sc_busex, bus_range[0],
	    bus_range[1] - bus_range[0] + 1, EX_WAITOK);

	/* Set up memory mapped I/O range. */
	extent_free(sc->sc_memex, mvmbus_pcie_mem_aperture[0],
	    mvmbus_pcie_mem_aperture[1], EX_NOWAIT);
	extent_free(sc->sc_ioex, mvmbus_pcie_io_aperture[0],
	    mvmbus_pcie_io_aperture[1], EX_NOWAIT);

	for (node = OF_child(sc->sc_node); node != 0; node = OF_peer(node)) {
		if (OF_getproplen(node, "status") <= 0)
			continue;
		OF_getprop(node, "status", buf, sizeof(buf));
		if (strcmp(buf, "disabled") == 0)
			continue;
		sc->sc_nports++;
	}

	if (!sc->sc_nports)
		return;

	sc->sc_ports = mallocarray(sc->sc_nports,
	    sizeof(struct mvpcie_port), M_DEVBUF, M_WAITOK);

	i = 0;
	for (node = OF_child(sc->sc_node); node != 0; node = OF_peer(node)) {
		if (OF_getproplen(node, "status") <= 0)
			continue;
		OF_getprop(node, "status", buf, sizeof(buf));
		if (strcmp(buf, "disabled") == 0)
			continue;
		mvpcie_port_attach(sc, &sc->sc_ports[i++], node);
	}
}

void
mvpcie_port_attach(struct mvpcie_softc *sc, struct mvpcie_port *po, int node)
{
	struct pcibus_attach_args pba;
	uint32_t assigned[5];
	uint32_t reg[5];
	int i;

	po->po_bus = 0;
	po->po_sc = sc;
	po->po_iot = sc->sc_iot;
	po->po_dmat = sc->sc_dmat;
	po->po_node = node;

	clock_enable_all(po->po_node);

	if (OF_getpropintarray(po->po_node, "reg", reg,
	    sizeof(reg)) != sizeof(reg))
		return;

	if (OF_getpropintarray(po->po_node, "assigned-addresses", assigned,
	    sizeof(assigned)) != sizeof(assigned))
		return;

	po->po_port = OF_getpropint(po->po_node, "marvell,pcie-port", 0);
	po->po_port = OF_getpropint(po->po_node, "marvell,pcie-lane", 0);
	po->po_dev = (reg[0] >> 11) & 0x1f;
	po->po_fn = (reg[0] >> 8) & 0x7;

	po->po_bridge_iobase = 1;
	po->po_bridge_iolimit = 1;

	po->po_gpiolen = OF_getproplen(po->po_node, "reset-gpios");
	if (po->po_gpiolen > 0) {
		po->po_gpio = malloc(po->po_gpiolen, M_DEVBUF, M_WAITOK);
		OF_getpropintarray(po->po_node, "reset-gpios",
		    po->po_gpio, po->po_gpiolen);
		gpio_controller_config_pin(po->po_gpio, GPIO_CONFIG_OUTPUT);
		gpio_controller_set_pin(po->po_gpio, 1);
		delay(100 * 1000);
		gpio_controller_set_pin(po->po_gpio, 0);
		delay(100 * 1000);
	}

	/* Look for IO and MEM mbus window info. */
	for (i = 0; i < sc->sc_rangeslen; i++) {
		if (sc->sc_ranges[i].slot != po->po_dev)
			continue;
		if (((sc->sc_ranges[i].flags >> 24) & 0x3) == 0x1) {
			po->po_win_iotarget = ((sc->sc_ranges[i].phys_base) >>
			    56) & 0xff;
			po->po_win_ioattr = ((sc->sc_ranges[i].phys_base) >>
			    48) & 0xff;
		}
		if (((sc->sc_ranges[i].flags >> 24) & 0x3) == 0x2) {
			po->po_win_memtarget = ((sc->sc_ranges[i].phys_base) >>
			    56) & 0xff;
			po->po_win_memattr = ((sc->sc_ranges[i].phys_base) >>
			    48) & 0xff;
		}
	}

	/* Map space. */
	for (i = 0; i < sc->sc_rangeslen; i++) {
		if (sc->sc_ranges[i].pci_base == assigned[2])
			break;
	}
	if (i == sc->sc_rangeslen)
		return;

	if (bus_space_map(po->po_iot, sc->sc_ranges[i].phys_base,
	    sc->sc_ranges[i].size, 0, &po->po_ioh)) {
		printf("%s: can't map ctrl registers\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	/* Set local dev number to 1. */
	HWRITE4(po, PCIE_STAT, (HREAD4(po, PCIE_STAT) &
	    ~(PCIE_STAT_DEV_MASK << PCIE_STAT_DEV_SHIFT)) |
	    1 << PCIE_STAT_DEV_SHIFT);

	mvpcie_wininit(po);

	HWRITE4(po, PCIE_CMD, HREAD4(po, PCIE_CMD) |
	    PCI_COMMAND_IO_ENABLE | PCI_COMMAND_MEM_ENABLE |
	    PCI_COMMAND_MASTER_ENABLE);
	HWRITE4(po, PCIE_MASK, HREAD4(po, PCIE_MASK) |
	    PCIE_MASK_ENABLE_INTS);

	if (!mvpcie_link_up(po))
		return;

	po->po_pc.pc_conf_v = po;
	po->po_pc.pc_attach_hook = mvpcie_attach_hook;
	po->po_pc.pc_bus_maxdevs = mvpcie_bus_maxdevs;
	po->po_pc.pc_make_tag = mvpcie_make_tag;
	po->po_pc.pc_decompose_tag = mvpcie_decompose_tag;
	po->po_pc.pc_conf_size = mvpcie_conf_size;
	po->po_pc.pc_conf_read = mvpcie_conf_read;
	po->po_pc.pc_conf_write = mvpcie_conf_write;
	po->po_pc.pc_probe_device_hook = mvpcie_probe_device_hook;

	po->po_pc.pc_intr_v = po;
	po->po_pc.pc_intr_map = mvpcie_intr_map;
	po->po_pc.pc_intr_map_msi = mvpcie_intr_map_msi;
	po->po_pc.pc_intr_map_msix = mvpcie_intr_map_msix;
	po->po_pc.pc_intr_string = mvpcie_intr_string;
	po->po_pc.pc_intr_establish = mvpcie_intr_establish;
	po->po_pc.pc_intr_disestablish = mvpcie_intr_disestablish;

	/* Skip address translation. */
	extern struct bus_space armv7_bs_tag;
	memset(&pba, 0, sizeof(pba));
	pba.pba_busname = "pci";
	pba.pba_iot = &armv7_bs_tag;
	pba.pba_memt = &armv7_bs_tag;
	pba.pba_dmat = po->po_dmat;
	pba.pba_pc = &po->po_pc;
	pba.pba_busex = sc->sc_busex;
	pba.pba_memex = sc->sc_memex;
	pba.pba_ioex = sc->sc_ioex;
	pba.pba_domain = pci_ndomains++;
	pba.pba_bus = po->po_bus;

	config_found(&sc->sc_dev, &pba, NULL);
}

void
mvpcie_wininit(struct mvpcie_port *po)
{
	size_t size;
	int i;

	if (mvmbus_dram_info == NULL)
		panic("%s: mbus dram information not set up", __func__);

	for (i = 0; i < 3; i++) {
		HWRITE4(po, PCIE_BAR_CTRL(i), 0);
		HWRITE4(po, PCIE_BAR_LO(i), 0);
		HWRITE4(po, PCIE_BAR_HI(i), 0);
	}

	for (i = 0; i < 5; i++) {
		HWRITE4(po, PCIE_WIN04_CTRL(i), 0);
		HWRITE4(po, PCIE_WIN04_BASE(i), 0);
		HWRITE4(po, PCIE_WIN04_REMAP(i), 0);
	}

	HWRITE4(po, PCIE_WIN5_CTRL, 0);
	HWRITE4(po, PCIE_WIN5_BASE, 0);
	HWRITE4(po, PCIE_WIN5_REMAP, 0);

	size = 0;
	for (i = 0; i < mvmbus_dram_info->numcs; i++) {
		struct mbus_dram_window *win = &mvmbus_dram_info->cs[i];

		HWRITE4(po, PCIE_WIN04_BASE(i), PCIE_BASEADDR(win->base));
		HWRITE4(po, PCIE_WIN04_REMAP(i), 0);
		HWRITE4(po, PCIE_WIN04_CTRL(i),
		    PCIE_WINEN |
		    PCIE_TARGET(mvmbus_dram_info->targetid) |
		    PCIE_ATTR(win->attr) |
		    PCIE_SIZE(win->size));

		size += win->size;
	}

	if ((size & (size - 1)) != 0)
		size = 1 << fls(size);

	HWRITE4(po, PCIE_BAR_LO(1), mvmbus_dram_info->cs[0].base);
	HWRITE4(po, PCIE_BAR_HI(1), 0);
	HWRITE4(po, PCIE_BAR_CTRL(1), PCIE_WINEN | PCIE_SIZE(size));
}

void
mvpcie_add_windows(paddr_t base, size_t size, paddr_t remap,
    uint8_t target, uint8_t attr)
{
	while (size) {
		size_t sz = 1 << (fls(size) - 1);

		mvmbus_add_window(base, size, remap, target, attr);
		base += sz;
		size -= sz;
		if (remap != MVMBUS_NO_REMAP)
			remap += sz;
	}
}

void
mvpcie_del_windows(paddr_t base, size_t size)
{
	while (size) {
		size_t sz = 1 << (fls(size) - 1);

		mvmbus_del_window(base, sz);
		base += sz;
		size -= sz;
	}
}

void
mvpcie_io_change(struct mvpcie_port *po)
{
	paddr_t base, remap;
	size_t size;

	/* If the limits are bogus or IO is disabled ... */
	if ((po->po_bridge_iolimit < po->po_bridge_iobase ||
	    po->po_bridge_iolimitupper < po->po_bridge_iobaseupper ||
	    (po->po_bridge_command & PCI_COMMAND_IO_ENABLE) == 0)) {
		/* ... delete the window if enabled. */
		if (po->po_win_iosize) {
			mvpcie_del_windows(po->po_win_iobase, po->po_win_iosize);
			po->po_win_iosize = 0;
		}
	}

	remap = po->po_bridge_iobaseupper << 16 |
	    (po->po_bridge_iobase & 0xf0) << 8;
	base = mvmbus_pcie_io_aperture[0] + remap;
	size = (po->po_bridge_iobaseupper << 16 |
	    (po->po_bridge_iobase & 0xf0) << 8 |
	    0xfff) - remap + 1;

	if (po->po_win_iobase == base && po->po_win_iosize == size &&
	    po->po_win_ioremap == remap)
		return;

	if (po->po_win_iosize)
		mvpcie_del_windows(po->po_win_iobase, po->po_win_iosize);

	if (size == 0)
		return;

	po->po_win_iobase = base;
	po->po_win_iosize = size;
	po->po_win_ioremap = remap;

	mvpcie_add_windows(po->po_win_iobase, po->po_win_iosize,
	    po->po_win_ioremap, po->po_win_iotarget, po->po_win_ioattr);
}

void
mvpcie_mem_change(struct mvpcie_port *po)
{
	paddr_t base;
	size_t size;

	/* If the limits are bogus or MEM is disabled ... */
	if ((po->po_bridge_memlimit < po->po_bridge_membase ||
	    (po->po_bridge_command & PCI_COMMAND_MEM_ENABLE) == 0)) {
		/* ... delete the window if enabled. */
		if (po->po_win_memsize) {
			mvpcie_del_windows(po->po_win_membase, po->po_win_memsize);
			po->po_win_memsize = 0;
		}
	}

	base = (po->po_bridge_membase & 0xfff0) << 16;
	size = (((po->po_bridge_memlimit & 0xfff0) << 16) |
	    0xfffff) - base + 1;

	if (po->po_win_membase == base && po->po_win_memsize == size)
		return;

	if (po->po_win_memsize)
		mvpcie_del_windows(po->po_win_membase, po->po_win_memsize);

	if (size == 0)
		return;

	po->po_win_membase = base;
	po->po_win_memsize = size;

	mvpcie_add_windows(po->po_win_membase, po->po_win_memsize,
	    MVMBUS_NO_REMAP, po->po_win_memtarget, po->po_win_memattr);
}

int
mvpcie_link_up(struct mvpcie_port *po)
{
	return !(HREAD4(po, PCIE_STAT) & PCIE_STAT_LINK_DOWN);
}

void
mvpcie_attach_hook(struct device *parent, struct device *self,
    struct pcibus_attach_args *pba)
{
}

int
mvpcie_bus_maxdevs(void *v, int bus)
{
	return 1;
}

pcitag_t
mvpcie_make_tag(void *v, int bus, int device, int function)
{
	return ((bus << 24) | (device << 19) | (function << 16));
}

void
mvpcie_decompose_tag(void *v, pcitag_t tag, int *bp, int *dp, int *fp)
{
	if (bp != NULL)
		*bp = (tag >> 24) & 0xff;
	if (dp != NULL)
		*dp = (tag >> 19) & 0x1f;
	if (fp != NULL)
		*fp = (tag >> 16) & 0x7;
}

int
mvpcie_conf_size(void *v, pcitag_t tag)
{
	return PCIE_CONFIG_SPACE_SIZE;
}

pcireg_t
mvpcie_conf_read_bridge(struct mvpcie_port *po, int reg)
{
	switch (reg) {
	case PCI_ID_REG:
		return PCI_VENDOR_MARVELL |
		    (HREAD4(po, PCIE_DEV_ID) & 0xffff0000);
	case PCI_COMMAND_STATUS_REG:
		return po->po_bridge_command;
	case PCI_CLASS_REG:
		return PCI_CLASS_BRIDGE << PCI_CLASS_SHIFT |
		    PCI_SUBCLASS_BRIDGE_PCI << PCI_SUBCLASS_SHIFT |
		    (HREAD4(po, PCIE_DEV_REV) & 0xff);
	case PCI_BHLC_REG:
		return 1 << PCI_HDRTYPE_SHIFT |
		    0x10 << PCI_CACHELINE_SHIFT;
	case PPB_REG_BASE0:
		return po->po_bridge_bar0;
	case PPB_REG_BASE1:
		return po->po_bridge_bar1;
	case PPB_REG_BUSINFO:
		return po->po_bridge_businfo;
	case PPB_REG_IOSTATUS:
		return po->po_bridge_iolimit << 8 |
		    po->po_bridge_iobase;
	case PPB_REG_MEM:
		return po->po_bridge_memlimit << 16 |
		    po->po_bridge_membase;
	case PPB_REG_IO_HI:
		return po->po_bridge_iolimitupper << 16 |
		    po->po_bridge_iobaseupper;
	case PPB_REG_PREFMEM:
	case PPB_REG_PREFBASE_HI32:
	case PPB_REG_PREFLIM_HI32:
	case PPB_REG_BRIDGECONTROL:
		return 0;
	default:
		printf("%s: reg %x\n", __func__, reg);
		break;
	}
	return 0;
}

void
mvpcie_conf_write_bridge(struct mvpcie_port *po, int reg, pcireg_t data)
{
	switch (reg) {
	case PCI_COMMAND_STATUS_REG: {
		uint32_t old = po->po_bridge_command;
		po->po_bridge_command = data & 0xffffff;
		if ((old ^ po->po_bridge_command) & PCI_COMMAND_IO_ENABLE)
			mvpcie_io_change(po);
		if ((old ^ po->po_bridge_command) & PCI_COMMAND_MEM_ENABLE)
			mvpcie_mem_change(po);
		break;
	}
	case PPB_REG_BASE0:
		po->po_bridge_bar0 = data;
		break;
	case PPB_REG_BASE1:
		po->po_bridge_bar1 = data;
		break;
	case PPB_REG_BUSINFO:
		po->po_bridge_businfo = data;
		HWRITE4(po, PCIE_STAT, (HREAD4(po, PCIE_STAT) &
		    ~(PCIE_STAT_BUS_MASK << PCIE_STAT_BUS_SHIFT)) |
		    ((data >> 8) & 0xff) << PCIE_STAT_BUS_SHIFT);
		break;
	case PPB_REG_IOSTATUS:
		po->po_bridge_iobase = (data & 0xff) | 1;
		po->po_bridge_iolimit = ((data >> 8) & 0xff) | 1;
		mvpcie_io_change(po);
		break;
	case PPB_REG_MEM:
		po->po_bridge_membase = data & 0xffff;
		po->po_bridge_memlimit = data >> 16;
		mvpcie_mem_change(po);
		break;
	case PPB_REG_IO_HI:
		po->po_bridge_iobaseupper = data & 0xffff;
		po->po_bridge_iolimitupper = data >> 16;
		mvpcie_io_change(po);
		break;
	case PPB_REG_PREFMEM:
	case PPB_REG_PREFBASE_HI32:
	case PPB_REG_PREFLIM_HI32:
		break;
	default:
		printf("%s: reg %x data %x\n", __func__, reg, data);
		break;
	}
}

pcireg_t
mvpcie_conf_read(void *v, pcitag_t tag, int reg)
{
	struct mvpcie_port *po = v;
	int bus, dev, fn;

	mvpcie_decompose_tag(NULL, tag, &bus, &dev, &fn);
	if (bus == po->po_bus) {
		KASSERT(dev == 0);
		return mvpcie_conf_read_bridge(po, reg);
	}

	if (!mvpcie_link_up(po))
		return 0;

	HWRITE4(po, PCIE_CONF_ADDR, PCIE_CONF_BUS(bus) | PCIE_CONF_DEV(dev) |
	     PCIE_CONF_FUNC(fn) | PCIE_CONF_REG(reg) | PCIE_CONF_ADDR_EN);
	return HREAD4(po, PCIE_CONF_DATA);
}

void
mvpcie_conf_write(void *v, pcitag_t tag, int reg, pcireg_t data)
{
	struct mvpcie_port *po = v;
	int bus, dev, fn;

	mvpcie_decompose_tag(NULL, tag, &bus, &dev, &fn);
	if (bus == po->po_bus) {
		KASSERT(dev == 0);
		mvpcie_conf_write_bridge(po, reg, data);
		return;
	}

	if (!mvpcie_link_up(po))
		return;

	HWRITE4(po, PCIE_CONF_ADDR, PCIE_CONF_BUS(bus) | PCIE_CONF_DEV(dev) |
	     PCIE_CONF_FUNC(fn) | PCIE_CONF_REG(reg) | PCIE_CONF_ADDR_EN);
	HWRITE4(po, PCIE_CONF_DATA, data);
}

int
mvpcie_probe_device_hook(void *v, struct pci_attach_args *pa)
{
	return 0;
}

struct mvpcie_intr_handle {
	pci_chipset_tag_t	ih_pc;
	pcitag_t		ih_tag;
	int			ih_intrpin;
};

int
mvpcie_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	struct mvpcie_intr_handle *ih;
	int pin = pa->pa_rawintrpin;

	if (pin == 0 || pin > PCI_INTERRUPT_PIN_MAX)
		return -1;

	if (pa->pa_tag == 0)
		return -1;

	ih = malloc(sizeof(struct mvpcie_intr_handle), M_DEVBUF, M_WAITOK);
	ih->ih_pc = pa->pa_pc;
	ih->ih_tag = pa->pa_intrtag;
	ih->ih_intrpin = pa->pa_intrpin;
	*ihp = (pci_intr_handle_t)ih;

	return 0;
}

int
mvpcie_intr_map_msi(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	return -1;
}

int
mvpcie_intr_map_msix(struct pci_attach_args *pa, int vec,
    pci_intr_handle_t *ihp)
{
	return -1;
}

const char *
mvpcie_intr_string(void *v, pci_intr_handle_t ihp)
{
	return "intx";
}

void *
mvpcie_intr_establish(void *v, pci_intr_handle_t ihp, int level,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct mvpcie_port *po = v;
	struct mvpcie_intr_handle *ih = (struct mvpcie_intr_handle *)ihp;
	int bus, dev, fn;
	uint32_t reg[4];
	void *cookie;

	mvpcie_decompose_tag(NULL, ih->ih_tag, &bus, &dev, &fn);

	reg[0] = bus << 16 | dev << 11 | fn << 8;
	reg[1] = reg[2] = 0;
	reg[3] = ih->ih_intrpin;

	cookie = arm_intr_establish_fdt_imap_cpu(po->po_node, reg,
	    sizeof(reg), level, ci, func, arg, name);

	free(ih, M_DEVBUF, sizeof(struct mvpcie_intr_handle));
	return cookie;
}

void
mvpcie_intr_disestablish(void *v, void *cookie)
{
	panic("%s", __func__);
}
