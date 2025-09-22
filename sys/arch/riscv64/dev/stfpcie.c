/*	$OpenBSD: stfpcie.c,v 1.4 2024/10/17 01:57:18 jsg Exp $	*/
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
#include <sys/evcount.h>
#include <sys/extent.h>
#include <sys/malloc.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/fdt.h>

/*
 * This driver is based on preliminary device tree bindings and will
 * almost certainly need changes once the official bindings land in
 * mainline Linux.  Support for these preliminary bindings will be
 * dropped as soon as official bindings are available.
 */

#define GEN_SETTINGS			0x80
#define  PORT_TYPE_RP			(1 << 0)
#define PCI_IDS_DW1			0x9c
#define PCIE_PCI_IOV_DW0		0xb4
#define  PHY_FUNCTION_DIS		(1 << 15)
#define PCIE_BAR_WIN			0xfc
#define  PFETCH_MEMWIN_64BADDR		(1 << 3)
#define IMASK_LOCAL			0x180
#define  IMASK_INT_INTA			(1 << 24)
#define  IMASK_INT_INTB			(1 << 25)
#define  IMASK_INT_INTC			(1 << 26)
#define  IMASK_INT_INTD			(1 << 27)
#define  IMASK_INT_INTX			(0xf << 24)
#define  IMASK_INT_MSI			(1 << 28)
#define ISTATUS_LOCAL			0x184
#define  PM_MSI_INT_INTA		(1 << 24)
#define  PM_MSI_INT_INTB		(1 << 25)
#define  PM_MSI_INT_INTC		(1 << 26)
#define  PM_MSI_INT_INTD		(1 << 27)
#define  PM_MSI_INT_INTX		(0xf << 24)
#define  PM_MSI_INT_MSI			(1 << 28)
#define IMSI_ADDR			0x190
#define ISTATUS_MSI			0x194
#define PMSG_SUPPORT_RX			0x3f0
#define  PMSG_LTR_SUPPORT		(1 << 2)
#define ATR_AXI4_SLV0_SRCADDR_PARAM(n)	(0x800 + (n) * 0x20)
#define  ATR_IMPL			(1 << 0)
#define  ATR_SIZE_SHIFT			1
#define ATR_AXI4_SLV0_SRC_ADDR(n)	(0x804 + (n) * 0x20)
#define ATR_AXI4_SLV0_TRSL_ADDR_LSB(n)	(0x808 + (n) * 0x20)
#define ATR_AXI4_SLV0_TRSL_ADDR_UDW(n)	(0x80c + (n) * 0x20)
#define ATR_AXI4_SLV0_TRSL_PARAM(n)	(0x810 + (n) * 0x20)
#define  TRSL_ID_PCIE_RX_TX		0
#define  TRSL_ID_PCIE_CONFIG		1

#define STG_PCIE0_BASE			0x048
#define STG_PCIE1_BASE			0x1f8

#define STG_ARFUN			0x078
#define  STG_ARFUN_AXI4_SLVL_MASK	(0x7ffff << 8)
#define  STG_ARFUN_AXI4_SLVL_SHIFT	8
#define  STG_PHY_FUNC_SHIFT		9
#define STG_AWFUN			0x07c
#define  STG_AWFUN_AXI4_SLVL_MASK	(0x7ffff << 0)
#define  STG_AWFUN_AXI4_SLVL_SHIFT	0
#define  STG_AWFUN_CKREF_SRC_MASK	(0x3 << 18)
#define  STG_AWFUN_CKREF_SRC_SHIFT	18
#define  STG_AWFUN_CLKREQ		(1 << 22)
#define STG_RP_NEP			0x0e8
#define  STG_K_RP_NEP			(1 << 8)
#define STG_LNKSTA			0x170
#define  STG_DATA_LINK_ACTIVE		(1 << 5)

#define HREAD4(sc, reg)							\
    (bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
    bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct stfpcie_range {
	uint32_t		flags;
	uint64_t		pci_base;
	uint64_t		phys_base;
	uint64_t		size;
};

struct stfpcie_intx {
	int			(*si_func)(void *);
	void			*si_arg;
	int			si_ipl;
	int			si_flags;
	int			si_pin;
	struct evcount		si_count;
	char			*si_name;
	struct stfpcie_softc	*si_sc;
	TAILQ_ENTRY(stfpcie_intx) si_next;
};

#define STFPCIE_NUM_MSI		32

struct stfpcie_msi {
	int			(*sm_func)(void *);
	void			*sm_arg;
	int			sm_ipl;
	int			sm_flags;
	int			sm_vec;
	struct evcount		sm_count;
	char			*sm_name;
};

struct stfpcie_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_space_handle_t	sc_cfg_ioh;
	bus_dma_tag_t		sc_dmat;

	int			sc_node;
	int			sc_acells;
	int			sc_scells;
	int			sc_pacells;
	int			sc_pscells;
	struct stfpcie_range	*sc_ranges;
	int			sc_nranges;

	struct bus_space	sc_bus_iot;
	struct bus_space	sc_bus_memt;
	
	struct machine_pci_chipset sc_pc;
	struct extent		*sc_busex;
	struct extent		*sc_memex;
	struct extent		*sc_pmemex;
	struct extent		*sc_ioex;
	int			sc_bus;

	void			*sc_ih;
	struct interrupt_controller sc_ic;
	TAILQ_HEAD(,stfpcie_intx) sc_intx[4];

	uint32_t		sc_msi_addr;
	struct stfpcie_msi	sc_msi[STFPCIE_NUM_MSI];
};

struct stfpcie_intr_handle {
	struct machine_intr_handle pih_ih;
	struct stfpcie_softc	*pih_sc;
	struct stfpcie_msi	*pih_sm;
};

int	stfpcie_match(struct device *, void *, void *);
void	stfpcie_attach(struct device *, struct device *, void *);

const struct cfattach stfpcie_ca = {
	sizeof (struct stfpcie_softc), stfpcie_match, stfpcie_attach
};

struct cfdriver stfpcie_cd = {
	NULL, "stfpcie", DV_DULL
};

int
stfpcie_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "starfive,jh7110-pcie");
}

int	stfpcie_intr(void *);
void	*stfpcie_intx_intr_establish(void *, int *, int,
 	    struct cpu_info *, int (*)(void *), void *, char *);
void	stfpcie_intx_intr_disestablish(void *);
void	stfpcie_intx_intr_barrier(void *);

void	stfpcie_attach_hook(struct device *, struct device *,
	    struct pcibus_attach_args *);
int	stfpcie_bus_maxdevs(void *, int);
pcitag_t stfpcie_make_tag(void *, int, int, int);
void	stfpcie_decompose_tag(void *, pcitag_t, int *, int *, int *);
int	stfpcie_conf_size(void *, pcitag_t);
pcireg_t stfpcie_conf_read(void *, pcitag_t, int);
void	stfpcie_conf_write(void *, pcitag_t, int, pcireg_t);
int	stfpcie_probe_device_hook(void *, struct pci_attach_args *);

int	stfpcie_intr_map(struct pci_attach_args *, pci_intr_handle_t *);
const char *stfpcie_intr_string(void *, pci_intr_handle_t);
void	*stfpcie_intr_establish(void *, pci_intr_handle_t, int,
	    struct cpu_info *, int (*)(void *), void *, char *);
void	stfpcie_intr_disestablish(void *, void *);

int	stfpcie_bs_iomap(bus_space_tag_t, bus_addr_t, bus_size_t, int,
	    bus_space_handle_t *);
int	stfpcie_bs_memmap(bus_space_tag_t, bus_addr_t, bus_size_t, int,
	    bus_space_handle_t *);

struct interrupt_controller stfpcie_ic = {
	.ic_barrier = intr_barrier
};

void
stfpcie_attach(struct device *parent, struct device *self, void *aux)
{
	struct stfpcie_softc *sc = (struct stfpcie_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct pcibus_attach_args pba;
	struct regmap *rm;
	uint32_t *ranges;
	int i, j, nranges, rangeslen;
	uint32_t bus_range[2];
	bus_addr_t cfg_base;
	bus_size_t cfg_size;
	bus_size_t stg_base;
	uint32_t *perst_gpio;
	int perst_gpiolen;
	uint32_t reg, stg;
	int idx, node, timo;

	sc->sc_iot = faa->fa_iot;

	idx = OF_getindex(faa->fa_node, "apb", "reg-names");
	/* XXX Preliminary bindings used a different name. */
	if (idx < 0)
		idx = OF_getindex(faa->fa_node, "reg", "reg-names");
	if (idx < 0 || idx >= faa->fa_nreg ||
	    bus_space_map(sc->sc_iot, faa->fa_reg[idx].addr,
	    faa->fa_reg[idx].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	idx = OF_getindex(faa->fa_node, "cfg", "reg-names");
	/* XXX Preliminary bindings used a different name. */
	if (idx < 0)
		idx = OF_getindex(faa->fa_node, "config", "reg-names");
	if (idx < 0 || idx >= faa->fa_nreg ||
	    bus_space_map(sc->sc_iot, faa->fa_reg[idx].addr,
	    faa->fa_reg[idx].size, 0, &sc->sc_cfg_ioh)) {
		printf(": can't map registers\n");
		return;
	}
	cfg_base = faa->fa_reg[idx].addr;
	cfg_size = faa->fa_reg[idx].size;

	sc->sc_dmat = faa->fa_dmat;
	sc->sc_node = faa->fa_node;

	switch (cfg_base) {
	case 0x940000000:
		stg_base = STG_PCIE0_BASE;
		break;
	case 0x9c0000000:
		stg_base = STG_PCIE1_BASE;
		break;
	default:
		printf(": unknown controller at 0x%lx\n", cfg_base);
		return;
	}

	/*
	 * XXX This was an array in the preliminary bindings; simplify
	 * when we drop support for those.
	 */
	if (OF_getpropintarray(sc->sc_node, "starfive,stg-syscon", &stg,
	    sizeof(stg)) < sizeof(stg)) {
		printf(": failed to get starfive,stg-syscon\n");
		return;
	}

	rm = regmap_byphandle(stg);
	if (rm == NULL) {
		printf(": can't get regmap\n");
		return;
	}

	pinctrl_byname(sc->sc_node, "default");

	reg = regmap_read_4(rm, stg_base + STG_RP_NEP);
	reg |= STG_K_RP_NEP;
	regmap_write_4(rm, stg_base + STG_RP_NEP, reg);

	reg = regmap_read_4(rm, stg_base + STG_AWFUN);
	reg &= ~STG_AWFUN_CKREF_SRC_MASK;
	reg |= (2 << STG_AWFUN_CKREF_SRC_SHIFT);
	regmap_write_4(rm, stg_base + STG_AWFUN, reg);

	reg = regmap_read_4(rm, stg_base + STG_AWFUN);
	reg |= STG_AWFUN_CLKREQ;
	regmap_write_4(rm, stg_base + STG_AWFUN, reg);

	clock_enable_all(sc->sc_node);
	reset_deassert_all(sc->sc_node);

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
	    sizeof(struct stfpcie_range), M_TEMP, M_WAITOK);
	sc->sc_nranges = nranges;

	for (i = 0, j = 0; i < sc->sc_nranges; i++) {
		sc->sc_ranges[i].flags = ranges[j++];
		sc->sc_ranges[i].pci_base = ranges[j++];
		if (sc->sc_acells - 1 == 2) {
			sc->sc_ranges[i].pci_base <<= 32;
			sc->sc_ranges[i].pci_base |= ranges[j++];
		}
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

	/* Mask and acknowledge all interrupts. */
	HWRITE4(sc, IMASK_LOCAL, 0);
	HWRITE4(sc, ISTATUS_LOCAL, 0xffffffff);

	sc->sc_ih = fdt_intr_establish(sc->sc_node, IPL_BIO | IPL_MPSAFE,
	    stfpcie_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		return;
	}

	printf("\n");

	perst_gpiolen = OF_getproplen(sc->sc_node, "perst-gpios");
	/* XXX Preliminary bindings used a different name. */
	if (perst_gpiolen <= 0)
		perst_gpiolen = OF_getproplen(sc->sc_node, "reset-gpios");
	if (perst_gpiolen <= 0)
		return;

	/* Assert PERST#. */
	perst_gpio = malloc(perst_gpiolen, M_TEMP, M_WAITOK);
	if (OF_getpropintarray(sc->sc_node, "perst-gpios",
	    perst_gpio, perst_gpiolen) != perst_gpiolen) {
		OF_getpropintarray(sc->sc_node, "reset-gpios",
		    perst_gpio, perst_gpiolen);
	}
	gpio_controller_config_pin(perst_gpio, GPIO_CONFIG_OUTPUT);
	gpio_controller_set_pin(perst_gpio, 1);

	/* Disable additional functions. */
	for (i = 1; i < 4; i++) {
		reg = regmap_read_4(rm, stg_base + STG_ARFUN);
		reg &= ~STG_ARFUN_AXI4_SLVL_MASK;
		reg |= (i << STG_PHY_FUNC_SHIFT) << STG_ARFUN_AXI4_SLVL_SHIFT;
		regmap_write_4(rm, stg_base + STG_ARFUN, reg);
		reg = regmap_read_4(rm, stg_base + STG_AWFUN);
		reg &= ~STG_AWFUN_AXI4_SLVL_MASK;
		reg |= (i << STG_PHY_FUNC_SHIFT) << STG_AWFUN_AXI4_SLVL_SHIFT;
		regmap_write_4(rm, stg_base + STG_AWFUN, reg);

		reg = HREAD4(sc, PCIE_PCI_IOV_DW0);
		reg |= PHY_FUNCTION_DIS;
		HWRITE4(sc, PCIE_PCI_IOV_DW0, reg);
	}
	reg = regmap_read_4(rm, stg_base + STG_ARFUN);
	reg &= ~STG_ARFUN_AXI4_SLVL_MASK;
	regmap_write_4(rm, stg_base + STG_ARFUN, reg);
	reg = regmap_read_4(rm, stg_base + STG_AWFUN);
	reg &= ~STG_AWFUN_AXI4_SLVL_MASK;
	regmap_write_4(rm, stg_base + STG_AWFUN, reg);

	/* Configure controller as root port. */
	reg = HREAD4(sc, GEN_SETTINGS);
	reg |= PORT_TYPE_RP;
	HWRITE4(sc, GEN_SETTINGS, reg);

	/* Configure as PCI bridge. */
	HWRITE4(sc, PCI_IDS_DW1,
	    PCI_CLASS_BRIDGE << PCI_CLASS_SHIFT |
	    PCI_SUBCLASS_BRIDGE_PCI << PCI_SUBCLASS_SHIFT);

	/* Enable prefetchable memory windows. */
	reg = HREAD4(sc, PCIE_BAR_WIN);
	reg |= PFETCH_MEMWIN_64BADDR;
	HWRITE4(sc, PCIE_BAR_WIN, reg);

	/* Disable LTR message forwarding. */
	reg = HREAD4(sc, PMSG_SUPPORT_RX);
	reg &= ~PMSG_LTR_SUPPORT;
	HWRITE4(sc, PMSG_SUPPORT_RX, reg);

	/* Configure config space address translation. */
	HWRITE4(sc, ATR_AXI4_SLV0_SRCADDR_PARAM(0),
	    cfg_base | ATR_IMPL | (flsl(cfg_size) - 1) << ATR_SIZE_SHIFT);
	HWRITE4(sc, ATR_AXI4_SLV0_SRC_ADDR(0), cfg_base >> 32);
	HWRITE4(sc, ATR_AXI4_SLV0_TRSL_ADDR_LSB(0), 0);
	HWRITE4(sc, ATR_AXI4_SLV0_TRSL_ADDR_UDW(0), 0);
	HWRITE4(sc, ATR_AXI4_SLV0_TRSL_PARAM(0), TRSL_ID_PCIE_CONFIG);

	/* Configure mmio space address translation. */
	for (i = 0; i < sc->sc_nranges; i++) {
		HWRITE4(sc, ATR_AXI4_SLV0_SRCADDR_PARAM(i + 1),
		    sc->sc_ranges[0].phys_base | ATR_IMPL |
		    (flsl(sc->sc_ranges[0].size) - 1) << ATR_SIZE_SHIFT);
		HWRITE4(sc, ATR_AXI4_SLV0_SRC_ADDR(i + 1),
		    sc->sc_ranges[0].phys_base >> 32);
		HWRITE4(sc, ATR_AXI4_SLV0_TRSL_ADDR_LSB(i + 1),
		    sc->sc_ranges[0].pci_base);
		HWRITE4(sc, ATR_AXI4_SLV0_TRSL_ADDR_UDW(i + 1),
		    sc->sc_ranges[0].pci_base >> 32);
		HWRITE4(sc, ATR_AXI4_SLV0_TRSL_PARAM(i + 1),
		    TRSL_ID_PCIE_RX_TX);
	}

	/*
	 * PERST# must remain asserted for at least 100us after the
	 * reference clock becomes stable.  But also has to remain
	 * active at least 100ms after power up.  Since we may have
	 * just powered on the device, play it safe and use 100ms.
	 */
	delay(100000);

	/* Deassert PERST#. */
	gpio_controller_set_pin(perst_gpio, 0);
	free(perst_gpio, M_TEMP, perst_gpiolen);

	/* Wait for link to come up. */
	for (timo = 100; timo > 0; timo--) {
		reg = regmap_read_4(rm, stg_base + STG_LNKSTA);
		if (reg & STG_DATA_LINK_ACTIVE)
			break;
		delay(1000);
	}

	/* INTx handling. */
	node = OF_getnodebyname(sc->sc_node, "interrupt-controller");
	if (node) {
		int pin;

		for (pin = 0; pin < nitems(sc->sc_intx); pin++)
			TAILQ_INIT(&sc->sc_intx[pin]);
		sc->sc_ic.ic_node = node;
		sc->sc_ic.ic_cookie = sc;
		sc->sc_ic.ic_establish = stfpcie_intx_intr_establish;
		sc->sc_ic.ic_disestablish = stfpcie_intx_intr_disestablish;
		sc->sc_ic.ic_barrier = stfpcie_intx_intr_barrier;
		fdt_intr_register(&sc->sc_ic);
	}

	/* MSI handling. */
	sc->sc_msi_addr = HREAD4(sc, IMSI_ADDR);

	/* Unmask interrupts. */
	HWRITE4(sc, IMASK_LOCAL, IMASK_INT_MSI);

	/* Create extents for our address spaces. */
	sc->sc_busex = extent_create("pcibus", 0, 255,
	    M_DEVBUF, NULL, 0, EX_WAITOK | EX_FILLED);
	sc->sc_memex = extent_create("pcimem", 0, (u_long)-1,
	    M_DEVBUF, NULL, 0, EX_WAITOK | EX_FILLED);
	sc->sc_pmemex = extent_create("pcipmem", 0, (u_long)-1,
	    M_DEVBUF, NULL, 0, EX_WAITOK | EX_FILLED);
	sc->sc_ioex = extent_create("pciio", 0, 0xffffffff,
	    M_DEVBUF, NULL, 0, EX_WAITOK | EX_FILLED);
	for (i = 0; i < sc->sc_nranges; i++) {
		if ((sc->sc_ranges[i].flags & 0x03000000) == 0x01000000) {
			extent_free(sc->sc_ioex, sc->sc_ranges[i].pci_base,
			    sc->sc_ranges[i].size, EX_WAITOK);
		}
		if ((sc->sc_ranges[i].flags & 0x03000000) == 0x02000000) {
			extent_free(sc->sc_memex, sc->sc_ranges[i].pci_base,
			    sc->sc_ranges[i].size, EX_WAITOK);
		}
		if ((sc->sc_ranges[i].flags & 0x03000000) == 0x03000000) {
			extent_free(sc->sc_pmemex, sc->sc_ranges[i].pci_base,
			    sc->sc_ranges[i].size, EX_WAITOK);
		}
	}

	/* Set up bus range. */
	if (OF_getpropintarray(sc->sc_node, "bus-range", bus_range,
	    sizeof(bus_range)) != sizeof(bus_range) ||
	    bus_range[0] >= 32 || bus_range[1] >= 32) {
		bus_range[0] = 0;
		bus_range[1] = 31;
	}
	sc->sc_bus = bus_range[0];
	extent_free(sc->sc_busex, bus_range[0],
	    bus_range[1] - bus_range[0] + 1, EX_WAITOK);

	memcpy(&sc->sc_bus_iot, sc->sc_iot, sizeof(sc->sc_bus_iot));
	sc->sc_bus_iot.bus_private = sc;
	sc->sc_bus_iot._space_map = stfpcie_bs_iomap;
	memcpy(&sc->sc_bus_memt, sc->sc_iot, sizeof(sc->sc_bus_memt));
	sc->sc_bus_memt.bus_private = sc;
	sc->sc_bus_memt._space_map = stfpcie_bs_memmap;

	sc->sc_pc.pc_conf_v = sc;
	sc->sc_pc.pc_attach_hook = stfpcie_attach_hook;
	sc->sc_pc.pc_bus_maxdevs = stfpcie_bus_maxdevs;
	sc->sc_pc.pc_make_tag = stfpcie_make_tag;
	sc->sc_pc.pc_decompose_tag = stfpcie_decompose_tag;
	sc->sc_pc.pc_conf_size = stfpcie_conf_size;
	sc->sc_pc.pc_conf_read = stfpcie_conf_read;
	sc->sc_pc.pc_conf_write = stfpcie_conf_write;
	sc->sc_pc.pc_probe_device_hook = stfpcie_probe_device_hook;

	sc->sc_pc.pc_intr_v = sc;
	sc->sc_pc.pc_intr_map = stfpcie_intr_map;
	sc->sc_pc.pc_intr_map_msi = _pci_intr_map_msi;
	sc->sc_pc.pc_intr_map_msix = _pci_intr_map_msix;
	sc->sc_pc.pc_intr_string = stfpcie_intr_string;
	sc->sc_pc.pc_intr_establish = stfpcie_intr_establish;
	sc->sc_pc.pc_intr_disestablish = stfpcie_intr_disestablish;

	memset(&pba, 0, sizeof(pba));
	pba.pba_busname = "pci";
	pba.pba_iot = &sc->sc_bus_iot;
	pba.pba_memt = &sc->sc_bus_memt;
	pba.pba_dmat = sc->sc_dmat;
	pba.pba_pc = &sc->sc_pc;
	pba.pba_busex = sc->sc_busex;
	pba.pba_memex = sc->sc_memex;
	pba.pba_pmemex = sc->sc_pmemex;
	pba.pba_ioex = sc->sc_ioex;
	pba.pba_domain = pci_ndomains++;
	pba.pba_bus = sc->sc_bus;
#ifdef notyet
	pba.pba_flags |= PCI_FLAGS_MSI_ENABLED;
#endif

	config_found(self, &pba, NULL);
}

void *
stfpcie_intx_intr_establish(void *cookie, int *cell, int level,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct stfpcie_softc *sc = (struct stfpcie_softc *)cookie;
	struct stfpcie_intx *si;
	int pin = cell[0] - 1;
	uint32_t mask;

	if (ci != NULL && !CPU_IS_PRIMARY(ci))
		return NULL;

	if (pin < 0 || pin >= nitems(sc->sc_intx))
		return NULL;

	/* Mask the interrupt. */
	mask = HREAD4(sc, IMASK_LOCAL);
	mask &= ~(IMASK_INT_INTA << pin);
	HWRITE4(sc, IMASK_LOCAL, mask);
	intr_barrier(sc->sc_ih);

	si = malloc(sizeof(*si), M_DEVBUF, M_WAITOK | M_ZERO);
	si->si_func = func;
	si->si_arg = arg;
	si->si_ipl = level & IPL_IRQMASK;
	si->si_flags = level & IPL_FLAGMASK;
	si->si_pin = pin;
	si->si_name = name;
	if (name != NULL)
		evcount_attach(&si->si_count, name, &si->si_pin);
	si->si_sc = sc;
	TAILQ_INSERT_TAIL(&sc->sc_intx[pin], si, si_next);

	/* Unmask the interrupt. */
	mask = HREAD4(sc, IMASK_LOCAL);
	mask |= (IMASK_INT_INTA << pin);
	HWRITE4(sc, IMASK_LOCAL, mask);

	return si;
}

void
stfpcie_intx_intr_disestablish(void *cookie)
{
	struct stfpcie_intx *si = cookie;
	struct stfpcie_softc *sc = si->si_sc;
	uint32_t mask;

	/* Mask the interrupt. */
	mask = HREAD4(sc, IMASK_LOCAL);
	mask &= ~(IMASK_INT_INTA << si->si_pin);
	HWRITE4(sc, IMASK_LOCAL, mask);
	intr_barrier(sc->sc_ih);

	if (si->si_name)
		evcount_detach(&si->si_count);

	TAILQ_REMOVE(&sc->sc_intx[si->si_pin], si, si_next);

	if (!TAILQ_EMPTY(&sc->sc_intx[si->si_pin])) {
		/* Unmask the interrupt. */
		mask = HREAD4(sc, IMASK_LOCAL);
		mask |= (IMASK_INT_INTA << si->si_pin);
		HWRITE4(sc, IMASK_LOCAL, mask);
	}

	free(si, M_DEVBUF, sizeof(*si));
}

void
stfpcie_intx_intr_barrier(void *cookie)
{
	struct stfpcie_intx *si = cookie;
	struct stfpcie_softc *sc = si->si_sc;

	intr_barrier(sc->sc_ih);
}

struct stfpcie_msi *
stfpcie_msi_establish(struct stfpcie_softc *sc, int level,
    int (*func)(void *), void *arg, char *name)
{
	struct stfpcie_msi *sm;
	int vec;

	for (vec = 0; vec < STFPCIE_NUM_MSI; vec++) {
		sm = &sc->sc_msi[vec];
		if (sm->sm_func == NULL)
			break;
	}
	if (vec == STFPCIE_NUM_MSI)
		return NULL;

	sm->sm_func = func;
	sm->sm_arg = arg;
	sm->sm_ipl = level & IPL_IRQMASK;
	sm->sm_flags = level & IPL_FLAGMASK;
	sm->sm_vec = vec;
	sm->sm_name = name;
	if (name != NULL)
		evcount_attach(&sm->sm_count, name, &sm->sm_vec);

	return sm;
	
}

void
stfpcie_msi_disestablish(struct stfpcie_softc *sc, struct stfpcie_msi *sm)
{
	if (sm->sm_name)
		evcount_detach(&sm->sm_count);
	sm->sm_func = NULL;
}

void
stfpcie_intx_intr(struct stfpcie_softc *sc, uint32_t status)
{
	struct stfpcie_intx *si;
	int pin, s;

	for (pin = 0; pin < nitems(sc->sc_intx); pin++) {
		if ((status & (PM_MSI_INT_INTA << pin)) == 0)
			continue;

		TAILQ_FOREACH(si, &sc->sc_intx[pin], si_next) {
			if ((si->si_flags & IPL_MPSAFE) == 0)
				KERNEL_LOCK();
			s = splraise(si->si_ipl);
			if (si->si_func(si->si_arg))
				si->si_count.ec_count++;
			splx(s);
			if ((si->si_flags & IPL_MPSAFE) == 0)
				KERNEL_UNLOCK();
		}
	}
}

void
stfpcie_msi_intr(struct stfpcie_softc *sc)
{
	struct stfpcie_msi *sm;
	uint32_t status;
	int vec, s;

	status = HREAD4(sc, ISTATUS_MSI);
	if (status == 0)
		return;
	HWRITE4(sc, ISTATUS_MSI, status);

	while (status) {
		vec = ffs(status) - 1;
		status &= ~(1U << vec);

		sm = &sc->sc_msi[vec];
		if (sm->sm_func == NULL)
			continue;

		if ((sm->sm_flags & IPL_MPSAFE) == 0)
			KERNEL_LOCK();
		s = splraise(sm->sm_ipl);
		if (sm->sm_func(sm->sm_arg))
			sm->sm_count.ec_count++;
		splx(s);
		if ((sm->sm_flags & IPL_MPSAFE) == 0)
			KERNEL_UNLOCK();
	}
}

int
stfpcie_intr(void *arg)
{
	struct stfpcie_softc *sc = arg;
	uint32_t status;

	status = HREAD4(sc, ISTATUS_LOCAL);
	if (status == 0)
		return 0;

	if (status & PM_MSI_INT_INTX)
		stfpcie_intx_intr(sc, status);

	/*
	 * Ack INTx late as they are level-triggered.  Ack MSI early
	 * as they are edge-triggered.
	 */
	HWRITE4(sc, ISTATUS_LOCAL, status);

	if (status & PM_MSI_INT_MSI)
		stfpcie_msi_intr(sc);

	return 1;
}

void
stfpcie_attach_hook(struct device *parent, struct device *self,
    struct pcibus_attach_args *pba)
{
}

int
stfpcie_bus_maxdevs(void *v, int bus)
{
	struct stfpcie_softc *sc = v;

	if (bus == sc->sc_bus || bus == sc->sc_bus + 1)
		return 1;
	return 32;
}

int
stfpcie_find_node(int node, int bus, int device, int function)
{
	uint32_t reg[5];
	uint32_t phys_hi;
	int child;

	phys_hi = ((bus << 16) | (device << 11) | (function << 8));

	for (child = OF_child(node); child; child = OF_peer(child)) {
		if (OF_getpropintarray(child, "reg",
		    reg, sizeof(reg)) != sizeof(reg))
			continue;

		if (reg[0] == phys_hi)
			return child;

		node = stfpcie_find_node(child, bus, device, function);
		if (node)
			return node;
	}

	return 0;
}

pcitag_t
stfpcie_make_tag(void *v, int bus, int device, int function)
{
	struct stfpcie_softc *sc = v;
	int node;

	node = stfpcie_find_node(sc->sc_node, bus, device, function);
	return (((pcitag_t)node << 32) |
	    (bus << 20) | (device << 15) | (function << 12));
}

void
stfpcie_decompose_tag(void *v, pcitag_t tag, int *bp, int *dp, int *fp)
{
	if (bp != NULL)
		*bp = (tag >> 20) & 0xff;
	if (dp != NULL)
		*dp = (tag >> 15) & 0x1f;
	if (fp != NULL)
		*fp = (tag >> 12) & 0x7;
}

int
stfpcie_conf_size(void *v, pcitag_t tag)
{
	return PCIE_CONFIG_SPACE_SIZE;
}

pcireg_t
stfpcie_conf_read(void *v, pcitag_t tag, int reg)
{
	struct stfpcie_softc *sc = v;

	tag = PCITAG_OFFSET(tag);
	return bus_space_read_4(sc->sc_iot, sc->sc_cfg_ioh, tag | reg);
}

void
stfpcie_conf_write(void *v, pcitag_t tag, int reg, pcireg_t data)
{
	struct stfpcie_softc *sc = v;

	tag = PCITAG_OFFSET(tag);
	bus_space_write_4(sc->sc_iot, sc->sc_cfg_ioh, tag | reg, data);
}

int
stfpcie_probe_device_hook(void *v, struct pci_attach_args *pa)
{
	struct stfpcie_softc *sc = v;
	uint16_t rid;

	rid = pci_requester_id(pa->pa_pc, pa->pa_tag);
	pa->pa_dmat = iommu_device_map_pci(sc->sc_node, rid, pa->pa_dmat);

	return 0;
}

int
stfpcie_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	int pin = pa->pa_rawintrpin;

	if (pin == 0 || pin > PCI_INTERRUPT_PIN_MAX)
		return -1;

	if (pa->pa_tag == 0)
		return -1;

	ihp->ih_pc = pa->pa_pc;
	ihp->ih_tag = pa->pa_intrtag;
	ihp->ih_intrpin = pa->pa_intrpin;
	ihp->ih_type = PCI_INTX;

	return 0;
}

const char *
stfpcie_intr_string(void *v, pci_intr_handle_t ih)
{
	switch (ih.ih_type) {
	case PCI_MSI:
		return "msi";
	case PCI_MSIX:
		return "msix";
	}

	return "intx";
}

void *
stfpcie_intr_establish(void *v, pci_intr_handle_t ih, int level,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct stfpcie_softc *sc = v;
	struct stfpcie_intr_handle *pih;
	void *cookie;

	KASSERT(ih.ih_type != PCI_NONE);

	if (ih.ih_type != PCI_INTX) {
		struct stfpcie_msi *sm;
		uint64_t addr, data;

		sm = stfpcie_msi_establish(sc, level, func, arg, name);
		if (sm == NULL)
			return NULL;
		addr = sc->sc_msi_addr;
		data = sm->sm_vec;

		pih = malloc(sizeof(*pih), M_DEVBUF, M_WAITOK | M_ZERO);
		pih->pih_ih.ih_ic = &stfpcie_ic;
		pih->pih_sc = sc;
		pih->pih_sm = sm;

		if (ih.ih_type == PCI_MSIX) {
			pci_msix_enable(ih.ih_pc, ih.ih_tag,
			    &sc->sc_bus_memt, ih.ih_intrpin, addr, data);
		} else
			pci_msi_enable(ih.ih_pc, ih.ih_tag, addr, data);
	} else {
		int bus, dev, fn;
		uint32_t reg[4];

		stfpcie_decompose_tag(sc, ih.ih_tag, &bus, &dev, &fn);

		reg[0] = bus << 16 | dev << 11 | fn << 8;
		reg[1] = reg[2] = 0;
		reg[3] = ih.ih_intrpin;

		cookie = fdt_intr_establish_imap_cpu(sc->sc_node, reg,
		    sizeof(reg), level, ci, func, arg, name);
		if (cookie == NULL)
			return NULL;

		pih = malloc(sizeof(*pih), M_DEVBUF, M_WAITOK | M_ZERO);
		pih->pih_ih.ih_ic = &stfpcie_ic;
		pih->pih_ih.ih_ih = cookie;
	}

	return pih;
}

void
stfpcie_intr_disestablish(void *v, void *cookie)
{
	struct stfpcie_intr_handle *pih = cookie;

	if (pih->pih_sm)
		stfpcie_msi_disestablish(pih->pih_sc, pih->pih_sm);
	else
		fdt_intr_disestablish(pih->pih_ih.ih_ih);

	free(pih, M_DEVBUF, sizeof(*pih));
}

int
stfpcie_bs_iomap(bus_space_tag_t t, bus_addr_t addr, bus_size_t size,
    int flags, bus_space_handle_t *bshp)
{
	struct stfpcie_softc *sc = t->bus_private;
	int i;

	for (i = 0; i < sc->sc_nranges; i++) {
		uint64_t pci_start = sc->sc_ranges[i].pci_base;
		uint64_t pci_end = pci_start + sc->sc_ranges[i].size;
		uint64_t phys_start = sc->sc_ranges[i].phys_base;

		if ((sc->sc_ranges[i].flags & 0x03000000) == 0x01000000 &&
		    addr >= pci_start && addr + size <= pci_end) {
			return bus_space_map(sc->sc_iot,
			    addr - pci_start + phys_start, size, flags, bshp);
		}
	}
	
	return ENXIO;
}

int
stfpcie_bs_memmap(bus_space_tag_t t, bus_addr_t addr, bus_size_t size,
    int flags, bus_space_handle_t *bshp)
{
	struct stfpcie_softc *sc = t->bus_private;
	int i;

	for (i = 0; i < sc->sc_nranges; i++) {
		uint64_t pci_start = sc->sc_ranges[i].pci_base;
		uint64_t pci_end = pci_start + sc->sc_ranges[i].size;
		uint64_t phys_start = sc->sc_ranges[i].phys_base;

		if ((sc->sc_ranges[i].flags & 0x02000000) == 0x02000000 &&
		    addr >= pci_start && addr + size <= pci_end) {
			return bus_space_map(sc->sc_iot,
			    addr - pci_start + phys_start, size, flags, bshp);
		}
	}
	
	return ENXIO;
}
