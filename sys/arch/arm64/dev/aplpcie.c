/*	$OpenBSD: aplpcie.c,v 1.19 2024/02/03 10:37:25 kettenis Exp $	*/
/*
 * Copyright (c) 2021 Mark Kettenis <kettenis@openbsd.org>
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

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/ofw_power.h>
#include <dev/ofw/fdt.h>

#define PCIE_RC_PHY_BASE(port)		(0x84000 + (port) * 0x4000)
#define PCIE_RC_PHY_SIZE		0x4000

#define PCIE_PHY_LANE_CONF		0x0000
#define  PCIE_PHY_LANE_CONF_REFCLK0REQ	(1 << 0)
#define  PCIE_PHY_LANE_CONF_REFCLK1REQ	(1 << 1)
#define  PCIE_PHY_LANE_CONF_REFCLK0ACK	(1 << 2)
#define  PCIE_PHY_LANE_CONF_REFCLK1ACK	(1 << 3)
#define  PCIE_PHY_LANE_CONF_REFCLK0EN	(1 << 9)
#define  PCIE_PHY_LANE_CONF_REFCLK1EN	(1 << 10)
#define  PCIE_PHY_LANE_CONF_REFCLK0CGEN	(1 << 30)
#define  PCIE_PHY_LANE_CONF_REFCLK1CGEN	(1U << 31)
#define PCIE_PHY_LANE_CTRL		0x0004
#define  PCIE_PHY_LANE_CTRL_CFGACC	(1 << 15)

#define PCIE_PORT_LTSSM_CTRL		0x0080
#define  PCIE_PORT_LTSSM_CTRL_START	(1 << 0)
#define PCIE_PORT_MSI_CTRL		0x0124
#define  PCIE_PORT_MSI_CTRL_ENABLE	(1 << 0)
#define  PCIE_PORT_MSI_CTRL_32		(5 << 4)
#define PCIE_PORT_MSI_REMAP		0x0128
#define PCIE_PORT_MSI_DOORBELL		0x0168
#define PCIE_PORT_LINK_STAT		0x0208
#define  PCIE_PORT_LINK_STAT_UP		(1 << 0)
#define PCIE_PORT_APPCLK		0x0800
#define  PCIE_PORT_APPCLK_EN		(1 << 0)
#define  PCIE_PORT_APPCLK_CGDIS		(1 << 8)
#define PCIE_PORT_STAT			0x0804
#define  PCIE_PORT_STAT_READY		(1 << 0)
#define PCIE_PORT_REFCLK		0x0810
#define  PCIE_PORT_REFCLK_EN		(1 << 0)
#define  PCIE_PORT_REFCLK_CGDIS		(1 << 8)
#define PCIE_PORT_PERST			0x0814
#define  PCIE_PORT_PERST_DIS		(1 << 0)
#define PCIE_PORT_RID2SID(idx)		(0x0828 + (idx) * 4)
#define  PCIE_PORT_RID2SID_VALID	(1U << 31)
#define  PCIE_PORT_RID2SID_SID_SHIFT	16
#define  PCIE_PORT_RID2SID_RID_MASK	0x0000ffff
#define  PCIE_PORT_MAX_RID2SID		64

#define PCIE_T6020_PORT_MSI_DOORBELL_LO	0x016c
#define PCIE_T6020_PORT_MSI_DOORBELL_HI	0x0170
#define PCIE_T6020_PORT_PERST		0x082c
#define PCIE_T6020_PORT_RID2SID(idx)	(0x3000 + (idx) * 4)
#define  PCIE_T6020_PORT_MAX_RID2SID	512
#define PCIE_T6020_PORT_MSI_MAP(idx)	(0x3800 + (idx) * 4)
#define  PCIE_T6020_PORT_MSI_MAP_ENABLE	(1U << 31)

#define HREAD4(sc, reg)							\
    (bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
    bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

#define RREAD4(sc, reg)						\
    (bus_space_read_4((sc)->sc_iot, (sc)->sc_rc_ioh, (reg)))
#define RWRITE4(sc, reg, val)					\
    bus_space_write_4((sc)->sc_iot, (sc)->sc_rc_ioh, (reg), (val))
#define RSET4(sc, reg, bits)				\
    RWRITE4((sc), (reg), RREAD4((sc), (reg)) | (bits))
#define RCLR4(sc, reg, bits)				\
    RWRITE4((sc), (reg), RREAD4((sc), (reg)) & ~(bits))

#define LREAD4(sc, port, reg)					\
    (bus_space_read_4((sc)->sc_iot, (sc)->sc_phy_ioh[port], (reg)))
#define LWRITE4(sc, port, reg, val)				\
    bus_space_write_4((sc)->sc_iot, (sc)->sc_phy_ioh[port], (reg), (val))
#define LSET4(sc, port, reg, bits)			\
    LWRITE4((sc), (port), (reg), LREAD4((sc), (port), (reg)) | (bits))
#define LCLR4(sc, port, reg, bits)			\
    LWRITE4((sc), (port), (reg), LREAD4((sc), (port), (reg)) & ~(bits))

#define PREAD4(sc, port, reg)						\
    (bus_space_read_4((sc)->sc_iot, (sc)->sc_port_ioh[(port)], (reg)))
#define PWRITE4(sc, port, reg, val)					\
    bus_space_write_4((sc)->sc_iot, (sc)->sc_port_ioh[(port)], (reg), (val))
#define PSET4(sc, port, reg, bits)				\
    PWRITE4((sc), (port), (reg), PREAD4((sc), (port), (reg)) | (bits))
#define PCLR4(sc, port, reg, bits)				\
    PWRITE4((sc), (port), (reg), PREAD4((sc), (port), (reg)) & ~(bits))

struct aplpcie_range {
	uint32_t		flags;
	uint64_t		pci_base;
	uint64_t		phys_base;
	uint64_t		size;
};

#define APLPCIE_MAX_PORTS	4

struct aplpcie_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_space_handle_t	sc_rc_ioh;
	bus_space_handle_t	sc_phy_ioh[APLPCIE_MAX_PORTS];
	bus_size_t		sc_phy_ios[APLPCIE_MAX_PORTS];
	bus_space_handle_t	sc_port_ioh[APLPCIE_MAX_PORTS];
	bus_size_t		sc_port_ios[APLPCIE_MAX_PORTS];
	bus_dma_tag_t		sc_dmat;

	int			sc_node;
	int			sc_acells;
	int			sc_scells;
	int			sc_pacells;
	int			sc_pscells;
	struct aplpcie_range	*sc_ranges;
	int			sc_nranges;

	struct bus_space	sc_bus_iot;
	struct bus_space	sc_bus_memt;
	
	struct machine_pci_chipset sc_pc;
	struct extent		*sc_busex;
	struct extent		*sc_memex;
	struct extent		*sc_pmemex;
	struct extent		*sc_ioex;
	int			sc_bus;

	int			sc_msi;
	bus_addr_t		sc_msi_doorbell;
	uint32_t		sc_msi_range[6];
	int			sc_msi_rangelen;
	struct interrupt_controller sc_msi_ic;
};

int	aplpcie_match(struct device *, void *, void *);
void	aplpcie_attach(struct device *, struct device *, void *);

const struct cfattach	aplpcie_ca = {
	sizeof (struct aplpcie_softc), aplpcie_match, aplpcie_attach
};

struct cfdriver aplpcie_cd = {
	NULL, "aplpcie", DV_DULL
};

int
aplpcie_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "apple,pcie") ||
	    OF_is_compatible(faa->fa_node, "apple,t6020-pcie");
}

void	aplpcie_init_port(struct aplpcie_softc *, int);
void	aplpcie_t6020_init_port(struct aplpcie_softc *, int);

void	aplpcie_attach_hook(struct device *, struct device *,
	    struct pcibus_attach_args *);
int	aplpcie_bus_maxdevs(void *, int);
pcitag_t aplpcie_make_tag(void *, int, int, int);
void	aplpcie_decompose_tag(void *, pcitag_t, int *, int *, int *);
int	aplpcie_conf_size(void *, pcitag_t);
pcireg_t aplpcie_conf_read(void *, pcitag_t, int);
void	aplpcie_conf_write(void *, pcitag_t, int, pcireg_t);
int	aplpcie_probe_device_hook(void *, struct pci_attach_args *);

int	aplpcie_intr_map(struct pci_attach_args *, pci_intr_handle_t *);
const char *aplpcie_intr_string(void *, pci_intr_handle_t);
void	*aplpcie_intr_establish(void *, pci_intr_handle_t, int,
	    struct cpu_info *, int (*)(void *), void *, char *);
void	aplpcie_intr_disestablish(void *, void *);

int	aplpcie_bs_iomap(bus_space_tag_t, bus_addr_t, bus_size_t, int,
	    bus_space_handle_t *);
int	aplpcie_bs_memmap(bus_space_tag_t, bus_addr_t, bus_size_t, int,
	    bus_space_handle_t *);

void	*aplpcie_intr_establish_msi(void *, uint64_t *, uint64_t *,
	    int, struct cpu_info *, int (*)(void *), void *, char *);
void	aplpcie_intr_disestablish_msi(void *);

void
aplpcie_attach(struct device *parent, struct device *self, void *aux)
{
	struct aplpcie_softc *sc = (struct aplpcie_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct pcibus_attach_args pba;
	uint32_t *ranges;
	int i, j, nranges, rangeslen;
	uint32_t bus_range[2];
	char name[32];
	int idx, node, port;

	sc->sc_iot = faa->fa_iot;

	idx = OF_getindex(faa->fa_node, "config", "reg-names");
	if (idx < 0 || idx >= faa->fa_nreg ||
	    bus_space_map(sc->sc_iot, faa->fa_reg[idx].addr,
	    faa->fa_reg[idx].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	idx = OF_getindex(faa->fa_node, "rc", "reg-names");
	if (idx < 0 || idx >= faa->fa_nreg ||
	    bus_space_map(sc->sc_iot, faa->fa_reg[idx].addr,
	    faa->fa_reg[idx].size, 0, &sc->sc_rc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	for (port = 0; port < APLPCIE_MAX_PORTS; port++) {
		snprintf(name, sizeof(name), "port%d", port);
		idx = OF_getindex(faa->fa_node, name, "reg-names");
		if (idx < 0)
			continue;
		if (idx > faa->fa_nreg ||
		    bus_space_map(sc->sc_iot, faa->fa_reg[idx].addr,
		    faa->fa_reg[idx].size, 0, &sc->sc_port_ioh[port])) {
			printf(": can't map registers\n");
			return;
		}
		sc->sc_port_ios[port] = faa->fa_reg[idx].size;

		snprintf(name, sizeof(name), "phy%d", port);
		idx = OF_getindex(faa->fa_node, name, "reg-names");
		if (idx < 0) {
			bus_space_subregion(sc->sc_iot, sc->sc_rc_ioh,
			    PCIE_RC_PHY_BASE(port), PCIE_RC_PHY_SIZE,
			    &sc->sc_phy_ioh[port]);
			continue;
		}
		if (idx > faa->fa_nreg ||
		    bus_space_map(sc->sc_iot, faa->fa_reg[idx].addr,
		    faa->fa_reg[idx].size, 0, &sc->sc_phy_ioh[port])) {
			printf(": can't map registers\n");
			return;
		}
		sc->sc_phy_ios[port] = faa->fa_reg[idx].size;
	}

	sc->sc_dmat = faa->fa_dmat;
	sc->sc_node = faa->fa_node;

	power_domain_enable(sc->sc_node);
	pinctrl_byname(sc->sc_node, "default");

	sc->sc_msi_doorbell =
	    OF_getpropint64(sc->sc_node, "msi-doorbell", 0xffff000ULL);
	sc->sc_msi_rangelen = OF_getpropintarray(sc->sc_node, "msi-ranges",
	    sc->sc_msi_range, sizeof(sc->sc_msi_range));
	if (sc->sc_msi_rangelen <= 0 ||
	    (sc->sc_msi_rangelen % sizeof(uint32_t)) ||
	    (sc->sc_msi_rangelen / sizeof(uint32_t)) < 5 ||
	    (sc->sc_msi_rangelen / sizeof(uint32_t) > 6)) {
		printf(": invalid msi-ranges property\n");
		return;
	}

	if (OF_is_compatible(sc->sc_node, "apple,t6020-pcie")) {
		for (node = OF_child(sc->sc_node); node; node = OF_peer(node))
			aplpcie_t6020_init_port(sc, node);
	} else {
		for (node = OF_child(sc->sc_node); node; node = OF_peer(node))
			aplpcie_init_port(sc, node);
	}

	/*
	 * Must wait at least 100ms after link training completes
	 * before sending a configuration request to a device
	 * immediately below a port.
	 */
	delay(100000);

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
	    sizeof(struct aplpcie_range), M_TEMP, M_WAITOK);
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

	printf("\n");

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
	sc->sc_bus_iot._space_map = aplpcie_bs_iomap;
	memcpy(&sc->sc_bus_memt, sc->sc_iot, sizeof(sc->sc_bus_memt));
	sc->sc_bus_memt.bus_private = sc;
	sc->sc_bus_memt._space_map = aplpcie_bs_memmap;

	sc->sc_pc.pc_conf_v = sc;
	sc->sc_pc.pc_attach_hook = aplpcie_attach_hook;
	sc->sc_pc.pc_bus_maxdevs = aplpcie_bus_maxdevs;
	sc->sc_pc.pc_make_tag = aplpcie_make_tag;
	sc->sc_pc.pc_decompose_tag = aplpcie_decompose_tag;
	sc->sc_pc.pc_conf_size = aplpcie_conf_size;
	sc->sc_pc.pc_conf_read = aplpcie_conf_read;
	sc->sc_pc.pc_conf_write = aplpcie_conf_write;
	sc->sc_pc.pc_probe_device_hook = aplpcie_probe_device_hook;

	sc->sc_pc.pc_intr_v = sc;
	sc->sc_pc.pc_intr_map = aplpcie_intr_map;
	sc->sc_pc.pc_intr_map_msi = _pci_intr_map_msi;
	sc->sc_pc.pc_intr_map_msivec = _pci_intr_map_msivec;
	sc->sc_pc.pc_intr_map_msix = _pci_intr_map_msix;
	sc->sc_pc.pc_intr_string = aplpcie_intr_string;
	sc->sc_pc.pc_intr_establish = aplpcie_intr_establish;
	sc->sc_pc.pc_intr_disestablish = aplpcie_intr_disestablish;

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
	pba.pba_flags |= PCI_FLAGS_MSI_ENABLED;

	sc->sc_msi_ic.ic_node = sc->sc_node;
	sc->sc_msi_ic.ic_cookie = sc;
	sc->sc_msi_ic.ic_establish_msi = aplpcie_intr_establish_msi;
	sc->sc_msi_ic.ic_disestablish = aplpcie_intr_disestablish_msi;
	sc->sc_msi_ic.ic_barrier = intr_barrier;
	fdt_intr_register(&sc->sc_msi_ic);

	pci_dopm = 1;

	config_found(self, &pba, NULL);
}

void
aplpcie_init_port(struct aplpcie_softc *sc, int node)
{
	char status[32];
	uint32_t reg[5];
	uint32_t *pwren_gpio;
	uint32_t *reset_gpio;
	int pwren_gpiolen, reset_gpiolen;
	uint32_t stat;
	int idx, port, timo;

	if (OF_getprop(node, "status", status, sizeof(status)) > 0 &&
	    strcmp(status, "disabled") == 0)
		return;

	if (OF_getpropintarray(node, "reg", reg, sizeof(reg)) != sizeof(reg))
		return;

	port = reg[0] >> 11;
	if (port >= APLPCIE_MAX_PORTS || sc->sc_port_ios[port] == 0)
		return;

	pwren_gpiolen = OF_getproplen(node, "pwren-gpios");
	reset_gpiolen = OF_getproplen(node, "reset-gpios");
	if (reset_gpiolen <= 0)
		return;

	/*
	 * Set things up such that we can share the 32 available MSIs
	 * across all ports.
	 */
	PWRITE4(sc, port, PCIE_PORT_MSI_CTRL,
	    PCIE_PORT_MSI_CTRL_32 | PCIE_PORT_MSI_CTRL_ENABLE);
	PWRITE4(sc, port, PCIE_PORT_MSI_REMAP, 0);
	PWRITE4(sc, port, PCIE_PORT_MSI_DOORBELL, sc->sc_msi_doorbell);

	/*
	 * Clear stream ID mappings.
	 */
	for (idx = 0; idx < PCIE_PORT_MAX_RID2SID; idx++)
		PWRITE4(sc, port, PCIE_PORT_RID2SID(idx), 0);

	/* Check if the link is already up. */
	stat = PREAD4(sc, port, PCIE_PORT_LINK_STAT);
	if (stat & PCIE_PORT_LINK_STAT_UP)
		return;

	PSET4(sc, port, PCIE_PORT_APPCLK, PCIE_PORT_APPCLK_EN);

	/* Assert PERST#. */
	reset_gpio = malloc(reset_gpiolen, M_TEMP, M_WAITOK);
	OF_getpropintarray(node, "reset-gpios", reset_gpio, reset_gpiolen);
	gpio_controller_config_pin(reset_gpio, GPIO_CONFIG_OUTPUT);
	gpio_controller_set_pin(reset_gpio, 1);

	/* Power up the device if necessary. */
	if (pwren_gpiolen > 0) {
		pwren_gpio = malloc(pwren_gpiolen, M_TEMP, M_WAITOK);
		OF_getpropintarray(node, "pwren-gpios",
		    pwren_gpio, pwren_gpiolen);
		gpio_controller_config_pin(pwren_gpio, GPIO_CONFIG_OUTPUT);
		gpio_controller_set_pin(pwren_gpio, 1);
		free(pwren_gpio, M_TEMP, pwren_gpiolen);
	}

	/* Setup Refclk. */
	LSET4(sc, port, PCIE_PHY_LANE_CTRL, PCIE_PHY_LANE_CTRL_CFGACC);
	LSET4(sc, port, PCIE_PHY_LANE_CONF, PCIE_PHY_LANE_CONF_REFCLK0REQ);
	for (timo = 500; timo > 0; timo--) {
		stat = LREAD4(sc, port, PCIE_PHY_LANE_CONF);
		if (stat & PCIE_PHY_LANE_CONF_REFCLK0ACK)
			break;
		delay(100);
	}
	LSET4(sc, port, PCIE_PHY_LANE_CONF, PCIE_PHY_LANE_CONF_REFCLK1REQ);
	for (timo = 500; timo > 0; timo--) {
		stat = LREAD4(sc, port, PCIE_PHY_LANE_CONF);
		if (stat & PCIE_PHY_LANE_CONF_REFCLK1ACK)
			break;
		delay(100);
	}
	LCLR4(sc, port, PCIE_PHY_LANE_CTRL, PCIE_PHY_LANE_CTRL_CFGACC);
	LSET4(sc, port, PCIE_PHY_LANE_CONF,
	    PCIE_PHY_LANE_CONF_REFCLK0EN | PCIE_PHY_LANE_CONF_REFCLK1EN);
	PSET4(sc, port, PCIE_PORT_REFCLK, PCIE_PORT_REFCLK_EN);

	/*
	 * PERST# must remain asserted for at least 100us after the
	 * reference clock becomes stable.  But also has to remain
	 * active at least 100ms after power up.
	 */
	if (pwren_gpiolen > 0)
		delay(100000);
	else
		delay(100);

	/* Deassert PERST#. */
	PSET4(sc, port, PCIE_PORT_PERST, PCIE_PORT_PERST_DIS);
	gpio_controller_set_pin(reset_gpio, 0);
	free(reset_gpio, M_TEMP, reset_gpiolen);

	for (timo = 2500; timo > 0; timo--) {
		stat = PREAD4(sc, port, PCIE_PORT_STAT);
		if (stat & PCIE_PORT_STAT_READY)
			break;
		delay(100);
	}
	if ((stat & PCIE_PORT_STAT_READY) == 0)
		return;

	PCLR4(sc, port, PCIE_PORT_REFCLK, PCIE_PORT_REFCLK_CGDIS);
	PCLR4(sc, port, PCIE_PORT_APPCLK, PCIE_PORT_APPCLK_CGDIS);

	/* Bring up the link. */
	PWRITE4(sc, port, PCIE_PORT_LTSSM_CTRL, PCIE_PORT_LTSSM_CTRL_START);
	for (timo = 1000; timo > 0; timo--) {
		stat = PREAD4(sc, port, PCIE_PORT_LINK_STAT);
		if (stat & PCIE_PORT_LINK_STAT_UP)
			break;
		delay(100);
	}
}

void
aplpcie_t6020_init_port(struct aplpcie_softc *sc, int node)
{
	char status[32];
	uint32_t reg[5];
	uint32_t *pwren_gpio;
	uint32_t *reset_gpio;
	int pwren_gpiolen, reset_gpiolen;
	uint32_t stat;
	int idx, msi, port, timo;

	if (OF_getprop(node, "status", status, sizeof(status)) > 0 &&
	    strcmp(status, "disabled") == 0)
		return;

	if (OF_getpropintarray(node, "reg", reg, sizeof(reg)) != sizeof(reg))
		return;

	port = reg[0] >> 11;
	if (port >= APLPCIE_MAX_PORTS || sc->sc_port_ios[port] == 0)
		return;

	pwren_gpiolen = OF_getproplen(node, "pwren-gpios");
	reset_gpiolen = OF_getproplen(node, "reset-gpios");
	if (reset_gpiolen <= 0)
		return;

	/*
	 * Set things up such that we can share the 32 available MSIs
	 * across all ports.
	 */
	PWRITE4(sc, port, PCIE_PORT_MSI_CTRL, PCIE_PORT_MSI_CTRL_ENABLE);
	for (msi = 0; msi < 32; msi++)
		PWRITE4(sc, port, PCIE_T6020_PORT_MSI_MAP(msi),
		    msi | PCIE_T6020_PORT_MSI_MAP_ENABLE);
	PWRITE4(sc, port, PCIE_T6020_PORT_MSI_DOORBELL_LO,
	    sc->sc_msi_doorbell);
	PWRITE4(sc, port, PCIE_T6020_PORT_MSI_DOORBELL_HI,
	    sc->sc_msi_doorbell >> 32);

	/*
	 * Clear stream ID mappings.
	 */
	for (idx = 0; idx < PCIE_T6020_PORT_MAX_RID2SID; idx++)
		PWRITE4(sc, port, PCIE_T6020_PORT_RID2SID(idx), 0);

	/* Check if the link is already up. */
	stat = PREAD4(sc, port, PCIE_PORT_LINK_STAT);
	if (stat & PCIE_PORT_LINK_STAT_UP)
		return;

	PSET4(sc, port, PCIE_PORT_APPCLK, PCIE_PORT_APPCLK_EN);

	/* Assert PERST#. */
	reset_gpio = malloc(reset_gpiolen, M_TEMP, M_WAITOK);
	OF_getpropintarray(node, "reset-gpios", reset_gpio, reset_gpiolen);
	gpio_controller_config_pin(reset_gpio, GPIO_CONFIG_OUTPUT);
	gpio_controller_set_pin(reset_gpio, 1);

	/* Power up the device if necessary. */
	if (pwren_gpiolen > 0) {
		pwren_gpio = malloc(pwren_gpiolen, M_TEMP, M_WAITOK);
		OF_getpropintarray(node, "pwren-gpios",
		    pwren_gpio, pwren_gpiolen);
		gpio_controller_config_pin(pwren_gpio, GPIO_CONFIG_OUTPUT);
		gpio_controller_set_pin(pwren_gpio, 1);
		free(pwren_gpio, M_TEMP, pwren_gpiolen);
	}

	/* Setup Refclk. */
	LSET4(sc, port, PCIE_PHY_LANE_CONF, PCIE_PHY_LANE_CONF_REFCLK0REQ);
	for (timo = 500; timo > 0; timo--) {
		stat = LREAD4(sc, port, PCIE_PHY_LANE_CONF);
		if (stat & PCIE_PHY_LANE_CONF_REFCLK0ACK)
			break;
		delay(100);
	}
	LSET4(sc, port, PCIE_PHY_LANE_CONF, PCIE_PHY_LANE_CONF_REFCLK1REQ);
	for (timo = 500; timo > 0; timo--) {
		stat = LREAD4(sc, port, PCIE_PHY_LANE_CONF);
		if (stat & PCIE_PHY_LANE_CONF_REFCLK1ACK)
			break;
		delay(100);
	}
	LSET4(sc, port, PCIE_PHY_LANE_CONF,
	    PCIE_PHY_LANE_CONF_REFCLK0EN | PCIE_PHY_LANE_CONF_REFCLK1EN);

	/*
	 * PERST# must remain asserted for at least 100us after the
	 * reference clock becomes stable.  But also has to remain
	 * active at least 100ms after power up.
	 */
	if (pwren_gpiolen > 0)
		delay(100000);
	else
		delay(100);

	/* Deassert PERST#. */
	PSET4(sc, port, PCIE_T6020_PORT_PERST, PCIE_PORT_PERST_DIS);
	gpio_controller_set_pin(reset_gpio, 0);
	free(reset_gpio, M_TEMP, reset_gpiolen);

	for (timo = 2500; timo > 0; timo--) {
		stat = PREAD4(sc, port, PCIE_PORT_STAT);
		if (stat & PCIE_PORT_STAT_READY)
			break;
		delay(100);
	}
	if ((stat & PCIE_PORT_STAT_READY) == 0)
		return;

	LSET4(sc, port, PCIE_PHY_LANE_CONF,
	    PCIE_PHY_LANE_CONF_REFCLK0CGEN | PCIE_PHY_LANE_CONF_REFCLK1CGEN);
	PCLR4(sc, port, PCIE_PORT_APPCLK, PCIE_PORT_APPCLK_CGDIS);

	/* Bring up the link. */
	PWRITE4(sc, port, PCIE_PORT_LTSSM_CTRL, PCIE_PORT_LTSSM_CTRL_START);
	for (timo = 1000; timo > 0; timo--) {
		stat = PREAD4(sc, port, PCIE_PORT_LINK_STAT);
		if (stat & PCIE_PORT_LINK_STAT_UP)
			break;
		delay(100);
	}
}

void
aplpcie_attach_hook(struct device *parent, struct device *self,
    struct pcibus_attach_args *pba)
{
}

int
aplpcie_bus_maxdevs(void *v, int bus)
{
	return 32;
}

int
aplpcie_find_node(int node, int bus, int device, int function)
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

		node = aplpcie_find_node(child, bus, device, function);
		if (node)
			return node;
	}

	return 0;
}

pcitag_t
aplpcie_make_tag(void *v, int bus, int device, int function)
{
	struct aplpcie_softc *sc = v;
	int node;

	node = aplpcie_find_node(sc->sc_node, bus, device, function);
	return (((pcitag_t)node << 32) |
	    (bus << 20) | (device << 15) | (function << 12));
}

void
aplpcie_decompose_tag(void *v, pcitag_t tag, int *bp, int *dp, int *fp)
{
	if (bp != NULL)
		*bp = (tag >> 20) & 0xff;
	if (dp != NULL)
		*dp = (tag >> 15) & 0x1f;
	if (fp != NULL)
		*fp = (tag >> 12) & 0x7;
}

int
aplpcie_conf_size(void *v, pcitag_t tag)
{
	return PCIE_CONFIG_SPACE_SIZE;
}

pcireg_t
aplpcie_conf_read(void *v, pcitag_t tag, int reg)
{
	struct aplpcie_softc *sc = v;

	tag = PCITAG_OFFSET(tag);
	return HREAD4(sc, tag | reg);
}

void
aplpcie_conf_write(void *v, pcitag_t tag, int reg, pcireg_t data)
{
	struct aplpcie_softc *sc = v;

	tag = PCITAG_OFFSET(tag);
	HWRITE4(sc, tag | reg, data);
}

int
aplpcie_find_port(struct aplpcie_softc *sc, int bus)
{
	uint32_t bus_range[2];
	uint32_t reg[5];
	int node;

	for (node = OF_child(sc->sc_node); node; node = OF_peer(node)) {
		/* Check if bus is in the range for this node. */
		if (OF_getpropintarray(node, "bus-range", bus_range,
		    sizeof(bus_range)) != sizeof(bus_range))
			continue;
		if (bus < bus_range[0] || bus > bus_range[1])
			continue;

		if (OF_getpropintarray(node, "reg", reg,
		    sizeof(reg)) != sizeof(reg))
			continue;
		return reg[0] >> 11;
	}

	return -1;
}

int
aplpcie_map_rid(struct aplpcie_softc *sc, int port, uint16_t rid, uint32_t sid)
{
	uint32_t reg;
	int idx;

	for (idx = 0; idx < PCIE_PORT_MAX_RID2SID; idx++) {
		reg = PREAD4(sc, port, PCIE_PORT_RID2SID(idx));

		/* If already mapped, we're done. */
		if ((reg & PCIE_PORT_RID2SID_RID_MASK) == rid)
			return 0;

		/* Is this an empty slot? */
		if (reg & PCIE_PORT_RID2SID_VALID)
			continue;

		/* Map using this slot. */
		reg = (sid << PCIE_PORT_RID2SID_SID_SHIFT) | rid |
		    PCIE_PORT_RID2SID_VALID;
		PWRITE4(sc, port, PCIE_PORT_RID2SID(idx), reg);

		/* Read back to check the slot is implemented. */
		if (PREAD4(sc, port, PCIE_PORT_RID2SID(idx)) != reg)
			return ENODEV;
		return 0;
	}

	return ENODEV;
}

int
aplpcie_t6020_map_rid(struct aplpcie_softc *sc, int port, uint16_t rid,
    uint32_t sid)
{
	uint32_t reg;
	int idx;

	for (idx = 0; idx < PCIE_T6020_PORT_MAX_RID2SID; idx++) {
		reg = PREAD4(sc, port, PCIE_T6020_PORT_RID2SID(idx));

		/* If already mapped, we're done. */
		if ((reg & PCIE_PORT_RID2SID_RID_MASK) == rid)
			return 0;

		/* Is this an empty slot? */
		if (reg & PCIE_PORT_RID2SID_VALID)
			continue;

		/* Map using this slot. */
		reg = (sid << PCIE_PORT_RID2SID_SID_SHIFT) | rid |
		    PCIE_PORT_RID2SID_VALID;
		PWRITE4(sc, port, PCIE_T6020_PORT_RID2SID(idx), reg);

		/* Read back to check the slot is implemented. */
		if (PREAD4(sc, port, PCIE_T6020_PORT_RID2SID(idx)) != reg)
			return ENODEV;
		return 0;
	}

	return ENODEV;
}

int
aplpcie_probe_device_hook(void *v, struct pci_attach_args *pa)
{
	struct aplpcie_softc *sc = v;
	uint32_t phandle, sid;
	uint16_t rid;
	int error, port;

	rid = pci_requester_id(pa->pa_pc, pa->pa_tag);
	pa->pa_dmat = iommu_device_map_pci(sc->sc_node, rid, pa->pa_dmat);
	if (iommu_device_lookup_pci(sc->sc_node, rid, &phandle, &sid))
		return 0;

	/*
	 * Create a stream ID mapping for this device.  The mappings
	 * are per-port so we first need to find the port for this
	 * device.  Then we find a free mapping slot to enter the
	 * mapping.  If we run out of mappings, we print a warning; as
	 * long as the device doesn't do DMA, it will still work.
	 */

	port = aplpcie_find_port(sc, pa->pa_bus);
	if (port == -1)
		return EINVAL;

	if (OF_is_compatible(sc->sc_node, "apple,t6020-pcie"))
		error = aplpcie_t6020_map_rid(sc, port, rid, sid);
	else
		error = aplpcie_map_rid(sc, port, rid, sid);
	if (error) {
		printf("%s: out of stream ID mapping slots\n",
		    sc->sc_dev.dv_xname);
	}

	/*
	 * Not all PCI devices do DMA, so don't return an error if we
	 * ran out of stream ID mapping slots.
	 */
	return 0;
}

int
aplpcie_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
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
aplpcie_intr_string(void *v, pci_intr_handle_t ih)
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
aplpcie_intr_establish(void *v, pci_intr_handle_t ih, int level,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct aplpcie_softc *sc = v;
	void *cookie;

	KASSERT(ih.ih_type != PCI_NONE);

	if (ih.ih_type != PCI_INTX) {
		uint64_t addr, data;

		addr = data = 0;
		cookie = fdt_intr_establish_msi_cpu(sc->sc_node, &addr,
		    &data, level, ci, func, arg, name);
		if (cookie == NULL)
			return NULL;

		if (ih.ih_type == PCI_MSIX) {
			pci_msix_enable(ih.ih_pc, ih.ih_tag,
			    &sc->sc_bus_memt, ih.ih_intrpin, addr, data);
		} else
			pci_msi_enable(ih.ih_pc, ih.ih_tag, addr, data);
	} else {
		int bus, dev, fn;
		uint32_t reg[4];

		aplpcie_decompose_tag(sc, ih.ih_tag, &bus, &dev, &fn);

		reg[0] = bus << 16 | dev << 11 | fn << 8;
		reg[1] = reg[2] = 0;
		reg[3] = ih.ih_intrpin;

		cookie = fdt_intr_establish_imap_cpu(sc->sc_node, reg,
		    sizeof(reg), level, ci, func, arg, name);
	}

	return cookie;
}

void
aplpcie_intr_disestablish(void *v, void *cookie)
{
}

void *
aplpcie_intr_establish_msi(void *cookie, uint64_t *addr, uint64_t *data,
    int level, struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct aplpcie_softc *sc = cookie;
	uint32_t cells[4];
	int ncells;

	ncells = sc->sc_msi_rangelen / sizeof(uint32_t);
	if (sc->sc_msi >= sc->sc_msi_range[ncells - 1])
		return NULL;

	*addr = sc->sc_msi_doorbell;
	*data = sc->sc_msi++;

	memcpy(cells, &sc->sc_msi_range[1], sizeof(cells));
	cells[ncells - 4] += *data;

	return fdt_intr_parent_establish(&sc->sc_msi_ic, cells,
	    level, ci, func, arg, name);
}

void
aplpcie_intr_disestablish_msi(void *cookie)
{
	fdt_intr_parent_disestablish(cookie);
}

int
aplpcie_bs_iomap(bus_space_tag_t t, bus_addr_t addr, bus_size_t size,
    int flags, bus_space_handle_t *bshp)
{
	struct aplpcie_softc *sc = t->bus_private;
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
aplpcie_bs_memmap(bus_space_tag_t t, bus_addr_t addr, bus_size_t size,
    int flags, bus_space_handle_t *bshp)
{
	struct aplpcie_softc *sc = t->bus_private;
	int i;

	flags |= BUS_SPACE_MAP_POSTED;

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
