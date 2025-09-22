/*	$OpenBSD: bcm2711_pcie.c,v 1.18 2025/08/29 11:50:43 kettenis Exp $	*/
/*
 * Copyright (c) 2020, 2025 Mark Kettenis <kettenis@openbsd.org>
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
#include <machine/simplebusvar.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/ppbreg.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/fdt.h>

#include <dev/ic/bcm2835_mbox.h>
#include <dev/ic/bcm2835_vcprop.h>

#define PCIE_RC_CFG_VENDOR_VENDOR_SPECIFIC_REG1		0x0188

#define PCIE_RC_CFG_PRIV1_ID_VAL3			0x043c
#define  PCIE_RC_CFG_PRIV1_ID_VAL3_CLASS_MASK		(0xffffff << 0)
#define PCIE_RC_CFG_PRIV1_LINK_CAP			0x04dc
#define  PCIE_RC_CFG_PRIV1_LINK_CAP_MAX_LINK_WIDTH_MASK	(0x1f << 4)
#define  PCIE_RC_CFG_PRIV1_LINK_CAP_ASPM_SUPPORT_MASK	(0x3 << 10)
#define PCIE_RC_CFG_PRIV1_ROOT_CAP			0x04f8
#define  PCIE_RC_CFG_PRIV1_ROOT_CAP_L1SS_MODE_MASK	(0x1f << 3)
#define  PCIE_RC_CFG_PRIV1_ROOT_CAP_L1SS_MODE_SHIFT	3

#define PCIE_RC_TL_VDM_CTL1				0x0a0c
#define PCIE_RC_TL_VDM_CTL0				0x0a20
#define  PCIE_RC_TL_VDM_CTL0_VDM_ENABLED		(1 << 16)
#define  PCIE_RC_TL_VDM_CTL0_VDM_IGNORETAG		(1 << 17)
#define  PCIE_RC_TL_VDM_CTL0_VDM_IGNOREVNDRID		(1 << 18)

#define PCIE_RC_DL_MDIO_ADDR				0x1100
#define  PCIE_RC_DL_MDIO_PORT_MASK			(0xf << 16)
#define  PCIE_RC_DL_MDIO_PORT_SHIFT			16
#define  PCIE_RC_DL_MDIO_REGAD_MASK			(0xffff << 0)
#define  PCIE_RC_DL_MDIO_REGAD_SHIFT			0
#define  PCIE_RC_DL_MDIO_CMD_READ			(1 << 20)
#define  PCIE_RC_DL_MDIO_CMD_WRITE			(0 << 20)
#define PCIE_RC_DL_MDIO_WR_DATA				0x1104
#define PCIE_RC_DL_MDIO_RD_DATA				0x1108
#define  PCIE_RC_DL_MDIO_DATA_DONE			(1U << 31)
#define  PCIE_RC_DL_MDIO_DATA_MASK			(0x7fffffff << 0)

#define PCIE_RC_PL_PHY_CTL_15			0x184c
#define  PCIE_RC_PL_PHY_CTL_15_PM_CLK_PERIOD_MASK	(0xff << 0)
#define  PCIE_RC_PL_PHY_CTL_15_PM_CLK_PERIOD_SHIFT	0

#define PCIE_MISC_MISC_CTRL				0x4008
#define  PCIE_MISC_MISC_CTRL_PCIE_RCB_64B_MODE		(1 << 7)
#define  PCIE_MISC_MISC_CTRL_PCIE_RCB_MPS_MODE		(1 << 10)
#define  PCIE_MISC_MISC_CTRL_SCB_ACCESS_EN		(1 << 12)
#define  PCIE_MISC_MISC_CTRL_CFG_READ_UR_MODE		(1 << 13)
#define  PCIE_MISC_MISC_CTRL_MAX_BURST_SIZE_MASK	(0x3 << 20)
#define  PCIE_MISC_MISC_CTRL_MAX_BURST_SIZE_128		(0x0 << 20)
#define  PCIE_MISC_MISC_CTRL_MAX_BURST_SIZE_256		(0x1 << 20)
#define  PCIE_MISC_MISC_CTRL_MAX_BURST_SIZE_512		(0x2 << 20)
#define  PCIE_MISC_MISC_CTRL_SCB0_SIZE_MASK		(0x1fU << 27)
#define  PCIE_MISC_MISC_CTRL_SCB0_SIZE_SHIFT		27
#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LO		0x400c
#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_HI		0x4010
#define PCIE_MISC_RC_BAR1_CONFIG_LO			0x402c
#define  PCIE_MISC_RC_BAR1_CONFIG_SIZE_MASK		(0x1f << 0)
#define PCIE_MISC_RC_BAR1_CONFIG_HI			0x4030
#define PCIE_MISC_MSI_BAR_CONFIG_LO			0x4044
#define  PCIE_MISC_MSI_BAR_CONFIG_LO_EN			(1 << 0)
#define PCIE_MISC_MSI_BAR_CONFIG_HI			0x4048
#define PCIE_MISC_MSI_DATA_CONFIG			0x404c
#define  PCIE_MISC_MSI_DATA_CONFIG_8			0xffe06540
#define  PCIE_MISC_MSI_DATA_CONFIG_32			0xfff86540
#define PCIE_MISC_RC_CONFIG_RETRY_TIMEOUT		0x405c
#define PCIE_MISC_PCIE_CTRL				0x4064
#define  PCIE_MISC_PCIE_CTRL_PCIE_PERSTB		(1 << 2)
#define PCIE_MISC_PCIE_STATUS				0x4068
#define  PCIE_MISC_PCIE_STATUS_PCIE_PHYLINKUP		(1 << 4)
#define  PCIE_MISC_PCIE_STATUS_PCIE_DL_ACTIVE		(1 << 5)
#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT	0x4070
#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_HI		0x4080
#define PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LIMIT_HI		0x4084
#define PCIE_MISC_CTRL_1				0x40a0
#define  PCIE_MISC_CTRL_1_EN_VDM_QOS_CONTROL		(1 << 5)
#define PCIE_MISC_UBUS_CTRL				0x40a4
#define  PCIE_MISC_UBUS_CTRL_UBUS_PCIE_REPLY_ERR_DIS    (1 << 13)
#define  PCIE_MISC_UBUS_CTRL_UBUS_PCIE_REPLY_DECERR_DIS (1 << 19)
#define PCIE_MISC_UBUS_TIMEOUT				0x40a8
#define PCIE_MISC_UBUS_BAR1_CONFIG_REMAP_LO		0x40ac
#define  PCIE_MISC_UBUS_BAR1_CONFIG_REMAP_EN		(1 << 0)
#define PCIE_MISC_UBUS_BAR1_CONFIG_REMAP_HI		0x40b0
#define PCIE_MISC_VDM_PRIORITY_TO_QOS_MAP_HI		0x4164
#define PCIE_MISC_VDM_PRIORITY_TO_QOS_MAP_LO		0x4168
#define PCIE_MISC_AXI_INTF_CTRL				0x416c
#define  PCIE_MISC_AXI_REQFIFO_EN_QOS_PROPAGATION	(1 << 17)
#define  PCIE_MISC_AXI_EN_RCLK_QOS_ARRAY_FIX		(1 << 13)
#define  PCIE_MISC_AXI_EN_QOS_UPDATE_TIMING_FIX		(1 << 12)
#define  PCIE_MISC_AXI_DIS_QOS_GATING_IN_MASTER		(1 << 11)
#define  PCIE_MISC_AXI_MASTER_MAX_OUTSTANDING_REQUESTS_MASK (0x3f << 0)
#define  PCIE_MISC_AXI_MASTER_MAX_OUTSTANDING_REQUESTS_SHIFT 0
#define PCIE_MISC_AXI_READ_ERROR_DATA			0x4170
#define PCIE_HARD_DEBUG					0x4204
#define PCIE_HARD_DEBUG_7712				0x4304
#define  PCIE_HARD_DEBUG_CLKREQ_DEBUG_ENABLE		(1 << 1)
#define  PCIE_HARD_DEBUG_REFCLK_OVRD_ENABLE		(1 << 16)
#define  PCIE_HARD_DEBUG_REFCLK_OVRD_OUT		(1 << 20)
#define  PCIE_HARD_DEBUG_L1SS_ENABLE			(1 << 21)
#define  PCIE_HARD_DEBUG_SERDES_IDDQ			(1 << 27)
#define  PCIE_HARD_DEBUG_CLKREQ_MASK \
	    (PCIE_HARD_DEBUG_CLKREQ_DEBUG_ENABLE |	\
	     PCIE_HARD_DEBUG_REFCLK_OVRD_ENABLE	|	\
	     PCIE_HARD_DEBUG_REFCLK_OVRD_OUT |		\
	     PCIE_HARD_DEBUG_L1SS_ENABLE)
#define PCIE_MSI_INTR2_INT_STATUS			0x4500
#define PCIE_MSI_INTR2_INT_CLR				0x4508
#define PCIE_MSI_INTR2_INT_MASK_SET			0x4510
#define PCIE_MSI_INTR2_INT_MASK_CLR			0x4514
#define PCIE_EXT_CFG_DATA				0x8000
#define PCIE_EXT_CFG_INDEX				0x9000
#define PCIE_RGR1_SW_INIT_1				0x9210
#define  PCIE_RGR1_SW_INIT_1_PERST			(1 << 0)
#define  PCIE_RGR1_SW_INIT_1_INIT			(1 << 1)

#define MDIO_SET_ADDR			0x1f
#define  MDIO_SSC_REGS_ADDR		0x1100

#define MDIO_SSC_STATUS			0x01
#define  MDIO_SSC_STATUS_SSC		(1 << 10)
#define  MDIO_SSC_STATUS_PLL_LOCK	(1 << 11)
#define MDIO_SSC_CNTL			0x02
#define  MDIO_SSC_CNTL_OVRD_EN		(1 << 15)
#define  MDIO_SSC_CNTL_OVRD_VAL		(1 << 14)

#define HREAD4(sc, reg)						\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct bcmpcie_range {
	uint32_t		flags;
	uint64_t		pci_base;
	uint64_t		phys_base;
	uint64_t		size;
};

struct bcmpcie_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_dma_tag_t		sc_dmat;

	bus_addr_t		sc_pcie_hard_debug;

	int			sc_node;
	int			sc_acells;
	int			sc_scells;
	int			sc_pacells;
	int			sc_pscells;
	struct bcmpcie_range	*sc_ranges;
	int			sc_nranges;
	struct bcmpcie_range	*sc_dmaranges;
	int			sc_ndmaranges;

	struct bus_space	sc_bus_iot;
	struct bus_space	sc_bus_memt;

	struct machine_bus_dma_tag sc_dma;

	struct machine_pci_chipset sc_pc;
	struct extent		*sc_busex;
	struct extent		*sc_memex;
	int			sc_bus;

	int			sc_vl805_fwload;
};

int bcmpcie_match(struct device *, void *, void *);
void bcmpcie_attach(struct device *, struct device *, void *);

const struct cfattach bcmpcie_ca = {
	sizeof (struct bcmpcie_softc), bcmpcie_match, bcmpcie_attach
};

struct cfdriver bcmpcie_cd = {
	NULL, "bcmpcie", DV_DULL
};

int
bcmpcie_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "brcm,bcm2711-pcie") ||
	    OF_is_compatible(faa->fa_node, "brcm,bcm2712-pcie");
}

void	bcmpcie_perst(struct bcmpcie_softc *, int);
void	bcmpcie_reset_bridge(struct bcmpcie_softc *, int);
int	bcmpcie_setup_bcm2712(struct bcmpcie_softc *);
void	bcmpcie_setup_clkreq(struct bcmpcie_softc *);
int	bcmpcie_setup_ssc(struct bcmpcie_softc *);
void	bcmpcie_setup_outbound(struct bcmpcie_softc *);
void	bcmpcie_setup_inbound(struct bcmpcie_softc *);
int	bcmpcie_link_up(struct bcmpcie_softc *);
int	bcmpcie_mdio_read(struct bcmpcie_softc *sc, uint8_t, uint16_t,
	    uint32_t *);
int	bcmpcie_mdio_write(struct bcmpcie_softc *sc, uint8_t, uint16_t,
	    uint32_t);

void	bcmpcie_attach_hook(struct device *, struct device *,
	    struct pcibus_attach_args *);
int	bcmpcie_bus_maxdevs(void *, int);
pcitag_t bcmpcie_make_tag(void *, int, int, int);
void	bcmpcie_decompose_tag(void *, pcitag_t, int *, int *, int *);
int	bcmpcie_conf_size(void *, pcitag_t);
pcireg_t bcmpcie_conf_read(void *, pcitag_t, int);
void	bcmpcie_conf_write(void *, pcitag_t, int, pcireg_t);
int	bcmpcie_probe_device_hook(void *, struct pci_attach_args *);

int	bcmpcie_intr_map(struct pci_attach_args *, pci_intr_handle_t *);
const char *bcmpcie_intr_string(void *, pci_intr_handle_t);
void	*bcmpcie_intr_establish(void *, pci_intr_handle_t, int,
	    struct cpu_info *, int (*)(void *), void *, char *);
void	bcmpcie_intr_disestablish(void *, void *);

int	bcmpcie_bs_iomap(bus_space_tag_t, bus_addr_t, bus_size_t, int,
	    bus_space_handle_t *);
int	bcmpcie_bs_memmap(bus_space_tag_t, bus_addr_t, bus_size_t, int,
	    bus_space_handle_t *);
int	bcmpcie_dmamap_load_buffer(bus_dma_tag_t, bus_dmamap_t, void *,
	    bus_size_t, struct proc *, int, paddr_t *, int *, int);
int	bcmpcie_dmamap_load_raw(bus_dma_tag_t, bus_dmamap_t,
	    bus_dma_segment_t *, int, bus_size_t, int);

void
bcmpcie_attach(struct device *parent, struct device *self, void *aux)
{
	struct bcmpcie_softc *sc = (struct bcmpcie_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct pcibus_attach_args pba;
	uint32_t bus_range[2];
	uint32_t *ranges;
	int i, j, nranges, rangeslen;
	uint32_t msi_parent, phandle;
	uint32_t reg;
	int timo;

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

	sc->sc_node = faa->fa_node;
	sc->sc_dmat = faa->fa_dmat;

	if (OF_is_compatible(faa->fa_node, "brcm,bcm2712-pcie"))
		sc->sc_pcie_hard_debug = PCIE_HARD_DEBUG_7712;
	else
		sc->sc_pcie_hard_debug = PCIE_HARD_DEBUG;

	sc->sc_acells = OF_getpropint(sc->sc_node, "#address-cells",
	    faa->fa_acells);
	sc->sc_scells = OF_getpropint(sc->sc_node, "#size-cells",
	    faa->fa_scells);
	sc->sc_pacells = faa->fa_acells;
	sc->sc_pscells = faa->fa_scells;

	/* Memory and IO space translations. */
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
	    sizeof(struct bcmpcie_range), M_DEVBUF, M_WAITOK);
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

	/* DMA translations */
	rangeslen = OF_getproplen(sc->sc_node, "dma-ranges");
	if (rangeslen > 0) {
		if ((rangeslen % sizeof(uint32_t)) ||
		     (rangeslen / sizeof(uint32_t)) % (sc->sc_acells +
		     sc->sc_pacells + sc->sc_scells)) {
			printf(": invalid dma-ranges property\n");
			free(sc->sc_ranges, M_DEVBUF,
			    sc->sc_nranges * sizeof(struct bcmpcie_range));
			return;
		}

		ranges = malloc(rangeslen, M_TEMP, M_WAITOK);
		OF_getpropintarray(sc->sc_node, "dma-ranges", ranges,
		    rangeslen);

		nranges = (rangeslen / sizeof(uint32_t)) /
		    (sc->sc_acells + sc->sc_pacells + sc->sc_scells);
		sc->sc_dmaranges = mallocarray(nranges,
		    sizeof(struct bcmpcie_range), M_DEVBUF, M_WAITOK);
		sc->sc_ndmaranges = nranges;

		for (i = 0, j = 0; i < sc->sc_ndmaranges; i++) {
			sc->sc_dmaranges[i].flags = ranges[j++];
			sc->sc_dmaranges[i].pci_base = ranges[j++];
			if (sc->sc_acells - 1 == 2) {
				sc->sc_dmaranges[i].pci_base <<= 32;
				sc->sc_dmaranges[i].pci_base |= ranges[j++];
			}
			sc->sc_dmaranges[i].phys_base = ranges[j++];
			if (sc->sc_pacells == 2) {
				sc->sc_dmaranges[i].phys_base <<= 32;
				sc->sc_dmaranges[i].phys_base |= ranges[j++];
			}
			sc->sc_dmaranges[i].size = ranges[j++];
			if (sc->sc_scells == 2) {
				sc->sc_dmaranges[i].size <<= 32;
				sc->sc_dmaranges[i].size |= ranges[j++];
			}
		}

		free(ranges, M_TEMP, rangeslen);
	}

	printf("\n");

	reset_assert(faa->fa_node, "rescal");
	reset_deassert(faa->fa_node, "rescal");

	/* Assert PERST#. */
	bcmpcie_perst(sc, 1);

	bcmpcie_reset_bridge(sc, 1);
	delay(200);
	bcmpcie_reset_bridge(sc, 0);

	reg = HREAD4(sc, sc->sc_pcie_hard_debug);
	reg &= ~PCIE_HARD_DEBUG_SERDES_IDDQ;
	HWRITE4(sc, sc->sc_pcie_hard_debug, reg);
	delay(200);

	reg = HREAD4(sc, PCIE_MISC_MISC_CTRL);
	reg &= ~PCIE_MISC_MISC_CTRL_MAX_BURST_SIZE_MASK;
	if (OF_is_compatible(faa->fa_node, "brcm,bcm2711-pcie"))
		reg |= PCIE_MISC_MISC_CTRL_MAX_BURST_SIZE_128;
	else
		reg |= PCIE_MISC_MISC_CTRL_MAX_BURST_SIZE_512;
	reg |= PCIE_MISC_MISC_CTRL_SCB_ACCESS_EN;
	reg |= PCIE_MISC_MISC_CTRL_CFG_READ_UR_MODE;
	reg |= PCIE_MISC_MISC_CTRL_PCIE_RCB_64B_MODE;
	reg |= PCIE_MISC_MISC_CTRL_PCIE_RCB_MPS_MODE;
	HWRITE4(sc, PCIE_MISC_MISC_CTRL, reg);

	/* Self-identify as a PCI bridge. */
	reg = HREAD4(sc, PCIE_RC_CFG_PRIV1_ID_VAL3);
	reg &= ~PCIE_RC_CFG_PRIV1_ID_VAL3_CLASS_MASK;
	reg |= (PCI_CLASS_BRIDGE << 16) | (PCI_SUBCLASS_BRIDGE_PCI << 8);
	HWRITE4(sc, PCIE_RC_CFG_PRIV1_ID_VAL3, reg);

	if (OF_is_compatible(sc->sc_node, "brcm,bcm2712-pcie")) {
		if (bcmpcie_setup_bcm2712(sc))
			printf("%s: can't set refclk\n", sc->sc_dev.dv_xname);
	}

	/* Deassert PERST#. */
	bcmpcie_perst(sc, 0);

	/* Wait for the link to come up. */
	for (timo = 100; timo > 0; timo--) {
		if (bcmpcie_link_up(sc))
			break;
		delay(1000);
	}
	if (timo == 0)
		return;

	bcmpcie_setup_clkreq(sc);

	if (OF_getpropbool(sc->sc_node, "brcm,enable-ssc")) {
		if (bcmpcie_setup_ssc(sc))
			printf("%s: can't enable SSC\n", sc->sc_dev.dv_xname);
	}

	/* Create extents for our address spaces. */
	sc->sc_busex = extent_create("pcibus", 0, 255,
	    M_DEVBUF, NULL, 0, EX_WAITOK | EX_FILLED);
	sc->sc_memex = extent_create("pcimem", 0, (u_long)-1,
	    M_DEVBUF, NULL, 0, EX_WAITOK | EX_FILLED);

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

	bcmpcie_setup_outbound(sc);
	bcmpcie_setup_inbound(sc);

	memcpy(&sc->sc_bus_iot, sc->sc_iot, sizeof(sc->sc_bus_iot));
	sc->sc_bus_iot.bus_private = sc;
	sc->sc_bus_iot._space_map = bcmpcie_bs_iomap;
	memcpy(&sc->sc_bus_memt, sc->sc_iot, sizeof(sc->sc_bus_memt));
	sc->sc_bus_memt.bus_private = sc;
	sc->sc_bus_memt._space_map = bcmpcie_bs_memmap;

	memcpy(&sc->sc_dma, sc->sc_dmat, sizeof(sc->sc_dma));
	sc->sc_dma._dmamap_load_buffer = bcmpcie_dmamap_load_buffer;
	sc->sc_dma._dmamap_load_raw = bcmpcie_dmamap_load_raw;
	sc->sc_dma._cookie = sc;

	sc->sc_pc.pc_conf_v = sc;
	sc->sc_pc.pc_attach_hook = bcmpcie_attach_hook;
	sc->sc_pc.pc_bus_maxdevs = bcmpcie_bus_maxdevs;
	sc->sc_pc.pc_make_tag = bcmpcie_make_tag;
	sc->sc_pc.pc_decompose_tag = bcmpcie_decompose_tag;
	sc->sc_pc.pc_conf_size = bcmpcie_conf_size;
	sc->sc_pc.pc_conf_read = bcmpcie_conf_read;
	sc->sc_pc.pc_conf_write = bcmpcie_conf_write;
	sc->sc_pc.pc_probe_device_hook = bcmpcie_probe_device_hook;

	sc->sc_pc.pc_intr_v = sc;
	sc->sc_pc.pc_intr_map = bcmpcie_intr_map;
	sc->sc_pc.pc_intr_map_msi = _pci_intr_map_msi;
	sc->sc_pc.pc_intr_map_msivec = _pci_intr_map_msivec;
	sc->sc_pc.pc_intr_map_msix = _pci_intr_map_msix;
	sc->sc_pc.pc_intr_string = bcmpcie_intr_string;
	sc->sc_pc.pc_intr_establish = bcmpcie_intr_establish;
	sc->sc_pc.pc_intr_disestablish = bcmpcie_intr_disestablish;

	memset(&pba, 0, sizeof(pba));
	pba.pba_busname = "pci";
	pba.pba_iot = &sc->sc_bus_iot;
	pba.pba_memt = &sc->sc_bus_memt;
	pba.pba_dmat = &sc->sc_dma;
	pba.pba_pc = &sc->sc_pc;
	pba.pba_busex = sc->sc_busex;
	pba.pba_memex = sc->sc_memex;
	pba.pba_domain = pci_ndomains++;
	pba.pba_bus = 0;

	/* Enable MSI support if we have an external MSI controller. */
	phandle = OF_getpropint(sc->sc_node, "phandle", 0);
	msi_parent = OF_getpropint(sc->sc_node, "msi-parent", phandle);
	if (msi_parent != phandle)
		pba.pba_flags |= PCI_FLAGS_MSI_ENABLED;

	config_found(self, &pba, NULL);
}

void
bcmpcie_perst(struct bcmpcie_softc *sc, int assert)
{
	uint32_t reg;

	if (OF_is_compatible(sc->sc_node, "brcm,bcm2711-pcie")) {
		reg = HREAD4(sc, PCIE_RGR1_SW_INIT_1);
		if (assert)
			reg |= PCIE_RGR1_SW_INIT_1_PERST;
		else
			reg &= ~PCIE_RGR1_SW_INIT_1_PERST;
		HWRITE4(sc, PCIE_RGR1_SW_INIT_1, reg);
	} else {
		reg = HREAD4(sc, PCIE_MISC_PCIE_CTRL);
		if (assert)
			reg &= ~PCIE_MISC_PCIE_CTRL_PCIE_PERSTB;
		else
			reg |= PCIE_MISC_PCIE_CTRL_PCIE_PERSTB;
		HWRITE4(sc, PCIE_MISC_PCIE_CTRL, reg);
	}
}

void
bcmpcie_reset_bridge(struct bcmpcie_softc *sc, int assert)
{
	if (OF_getindex(sc->sc_node, "reset-names", "bridge") >= 0) {
		if (assert)
			reset_assert(sc->sc_node, "bridge");
		else
			reset_deassert(sc->sc_node, "bridge");
		return;
	}

	if (assert)
		HSET4(sc, PCIE_RGR1_SW_INIT_1, PCIE_RGR1_SW_INIT_1_INIT);
	else
		HCLR4(sc, PCIE_RGR1_SW_INIT_1, PCIE_RGR1_SW_INIT_1_INIT);

	sc->sc_vl805_fwload = 1;
}

int
bcmpcie_setup_bcm2712(struct bcmpcie_softc *sc)
{
	struct {
		uint16_t addr;
		uint16_t data;
	} regs[] = {
		{ 0x16, 0x50b9 },
		{ 0x17, 0xbda1 },
		{ 0x18, 0x0094 },
		{ 0x19, 0x97b4 },
		{ 0x1b, 0x5030 },
		{ 0x1c, 0x5030 },
		{ 0x1e, 0x0007 },
	};
	uint32_t reg;
	int error, i;

	/* Make sure read errors return 0xffffffff instead of 0xdeaddead. */
	HWRITE4(sc, PCIE_MISC_AXI_READ_ERROR_DATA, 0xffffffff);

	/* Magic to select a 54MHz refclk soure? */
	error = bcmpcie_mdio_write(sc, 0, MDIO_SET_ADDR, 0x1600);
	if (error)
		return error;
	for (i = 0; i < nitems(regs); i++) {
		error = bcmpcie_mdio_write(sc, 0, regs[i].addr, regs[i].data);
		if (error)
			return error;
	}

	delay(100);

	/* Adjust L1SS sub-state timers. */
	reg = HREAD4(sc, PCIE_RC_PL_PHY_CTL_15);
	reg &= ~PCIE_RC_PL_PHY_CTL_15_PM_CLK_PERIOD_MASK;
	reg |= 18 << PCIE_RC_PL_PHY_CTL_15_PM_CLK_PERIOD_SHIFT;
	HWRITE4(sc, PCIE_RC_PL_PHY_CTL_15, reg);

	return 0;
}

void
bcmpcie_setup_clkreq(struct bcmpcie_softc *sc)
{
	char mode[8] = "default";
	uint32_t reg;

	/* Start out in safe mode. */
	reg = HREAD4(sc, sc->sc_pcie_hard_debug);
	reg &= ~PCIE_HARD_DEBUG_CLKREQ_MASK;

	OF_getprop(sc->sc_node, "brcm,clkreq-mode", mode, sizeof(&mode));
	if (strcmp(mode, "no-l1ss") == 0) {
		reg |= PCIE_HARD_DEBUG_CLKREQ_DEBUG_ENABLE;
	} else if (strcmp(mode, "default") == 0) {
		reg |= PCIE_HARD_DEBUG_L1SS_ENABLE;
	}

	HWRITE4(sc, sc->sc_pcie_hard_debug, reg);

	/* Unadvertise L1SS if appropriate. */
	if (strcmp(mode, "no-l1ss") == 0) {
		reg = HREAD4(sc, PCIE_RC_CFG_PRIV1_ROOT_CAP);
		reg &= ~PCIE_RC_CFG_PRIV1_ROOT_CAP_L1SS_MODE_MASK;
		reg |= (1 << PCIE_RC_CFG_PRIV1_ROOT_CAP_L1SS_MODE_SHIFT);
		HWRITE4(sc, PCIE_RC_CFG_PRIV1_ROOT_CAP, reg);
	}
}

int
bcmpcie_setup_ssc(struct bcmpcie_softc *sc)
{
	uint32_t reg;
	int error;

	error = bcmpcie_mdio_write(sc, 0, MDIO_SET_ADDR, MDIO_SSC_REGS_ADDR);
	if (error)
		return error;

	error = bcmpcie_mdio_read(sc, 0, MDIO_SSC_CNTL, &reg);
	if (error)
		return error;
	reg |= MDIO_SSC_CNTL_OVRD_VAL | MDIO_SSC_CNTL_OVRD_EN;
	error = bcmpcie_mdio_write(sc, 0, MDIO_SSC_CNTL, reg);
	if (error)
		return error;
	delay(1000);

	error = bcmpcie_mdio_read(sc, 0, MDIO_SSC_STATUS, &reg);
	if (error)
		return error;

	if ((reg & MDIO_SSC_STATUS_SSC) && (reg & MDIO_SSC_STATUS_PLL_LOCK))
		return 0;

	return EIO;
}

void
bcmpcie_setup_outbound(struct bcmpcie_softc *sc)
{
	int i;

	for (i = 0; i < sc->sc_nranges; i++) {
		if ((sc->sc_ranges[i].flags & 0x03000000) == 0x02000000) {
			uint64_t cpu_base = sc->sc_ranges[i].phys_base;
			uint64_t cpu_limit = sc->sc_ranges[i].phys_base +
			    sc->sc_ranges[i].size - 1;

			HWRITE4(sc, PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LO,
			    sc->sc_ranges[i].pci_base);
			HWRITE4(sc, PCIE_MISC_CPU_2_PCIE_MEM_WIN0_HI,
			    sc->sc_ranges[i].pci_base >> 32);
			HWRITE4(sc, PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_LIMIT,
			    (cpu_base & PPB_MEM_MASK) >> PPB_MEM_SHIFT |
			    (cpu_limit & PPB_MEM_MASK));
			HWRITE4(sc, PCIE_MISC_CPU_2_PCIE_MEM_WIN0_BASE_HI,
			    cpu_base >> 32);
			HWRITE4(sc, PCIE_MISC_CPU_2_PCIE_MEM_WIN0_LIMIT_HI,
			    cpu_limit >> 32);

			extent_free(sc->sc_memex, sc->sc_ranges[i].pci_base,
			    sc->sc_ranges[i].size, EX_WAITOK);
		}
	}
}

void
bcmpcie_setup_inbound(struct bcmpcie_softc *sc)
{
	struct bcmpcie_range ranges[3];
	uint64_t pci_base, cpu_base, size;
	int nranges = 0;
	int i;

	if (sc->sc_ndmaranges == 0)
		return;

	if (OF_is_compatible(sc->sc_node, "brcm,bcm2711-pcie")) {
		uint64_t start = sc->sc_dmaranges[0].pci_base;
		uint64_t end = start + sc->sc_dmaranges[0].size;
		
		for (i = 1; i < sc->sc_ndmaranges; i++) {
			pci_base = sc->sc_dmaranges[i].pci_base;
			size = sc->sc_dmaranges[i].size;

			start = MIN(start, pci_base);
			end = MAX(end, pci_base + size);
		}

		/*
		 * BAR1 and BAR3 need to be disabled, BAR2 should
		 * cover all inbound traffic.
		 */
		ranges[0].pci_base = 0;
		ranges[0].phys_base = 0;
		ranges[0].size = 0;
		ranges[1].pci_base = start;
		ranges[1].phys_base = 0;
		ranges[1].size = end - start;
		ranges[2].pci_base = 0;
		ranges[2].phys_base = 0;
		ranges[2].size = 0;
		nranges = 3;
	} else {
		for (i = 0; i < sc->sc_ndmaranges; i++) {
			if (nranges == nitems(ranges)) {
				printf("%s: too many dma-ranges\n",
				    sc->sc_dev.dv_xname);
				break;
			}
			ranges[i].pci_base = sc->sc_dmaranges[i].pci_base;
			ranges[i].phys_base = sc->sc_dmaranges[i].phys_base;
			ranges[i].size = sc->sc_dmaranges[i].size;
			nranges++;
		}
	}

	for (i = 0; i < nranges; i++) {
		u_int shift;

		pci_base = ranges[i].pci_base;
		cpu_base = ranges[i].phys_base;
		size = ranges[i].size;

		shift = 0;
		while ((1ULL << shift) < size)
			shift++;
		if (shift >= 12 && shift <= 15)
			size = 0x1c + (shift - 12);
		else if (shift >= 16 && shift <= 36)
			size = (shift - 15);
		else
			size = 0;

		HWRITE4(sc, PCIE_MISC_RC_BAR1_CONFIG_LO + i * 8,
		    (pci_base & ~PCIE_MISC_RC_BAR1_CONFIG_SIZE_MASK) | size);
		HWRITE4(sc, PCIE_MISC_RC_BAR1_CONFIG_HI + i * 8,
		    pci_base >> 32);

		/* BCM2711 doesn't have UBUS. */
		if (OF_is_compatible(sc->sc_node, "brcm,bcm2711-pcie"))
			continue;

		HWRITE4(sc, PCIE_MISC_UBUS_BAR1_CONFIG_REMAP_LO + i * 8,
		    cpu_base | PCIE_MISC_UBUS_BAR1_CONFIG_REMAP_EN);
		HWRITE4(sc, PCIE_MISC_UBUS_BAR1_CONFIG_REMAP_HI + i * 8,
		    cpu_base >> 32);
	}

	if (OF_is_compatible(sc->sc_node, "brcm,bcm2711-pcie")) {
		uint32_t reg;
		u_int shift;

		shift = 0;
		size = ranges[1].size;
		while ((1ULL << shift) < size)
			shift++;
		size = size ? (shift - 15) : 0xf;

		reg = HREAD4(sc, PCIE_MISC_MISC_CTRL);
		reg &= ~PCIE_MISC_MISC_CTRL_SCB0_SIZE_MASK;
		reg |= (size << PCIE_MISC_MISC_CTRL_SCB0_SIZE_SHIFT);
		HWRITE4(sc, PCIE_MISC_MISC_CTRL, reg);
	}
}

int
bcmpcie_link_up(struct bcmpcie_softc *sc)
{
	uint32_t reg;

	reg = HREAD4(sc, PCIE_MISC_PCIE_STATUS);
	if ((reg & PCIE_MISC_PCIE_STATUS_PCIE_DL_ACTIVE) &&
	    (reg & PCIE_MISC_PCIE_STATUS_PCIE_PHYLINKUP))
		return 1;
	return 0;
}

int
bcmpcie_mdio_read(struct bcmpcie_softc *sc, uint8_t port, uint16_t addr,
     uint32_t *data)
{
	uint32_t reg;
	int timo;

	KASSERT(port < 16);
	reg = PCIE_RC_DL_MDIO_CMD_READ;
	reg |= ((uint32_t)port << PCIE_RC_DL_MDIO_PORT_SHIFT);
	reg |= ((uint32_t)addr << PCIE_RC_DL_MDIO_REGAD_SHIFT);
	HWRITE4(sc, PCIE_RC_DL_MDIO_ADDR, reg);
	HREAD4(sc, PCIE_RC_DL_MDIO_ADDR);

	for (timo = 10; timo > 0; timo--) {
		reg = HREAD4(sc, PCIE_RC_DL_MDIO_RD_DATA);
		if (reg & PCIE_RC_DL_MDIO_DATA_DONE)
			break;
		delay(10);
	}
	if (timo == 0) {
		printf("%s: timeout\n", __func__);
		return EIO;
	}

	*data = reg & PCIE_RC_DL_MDIO_DATA_MASK;
	return 0;
}

int
bcmpcie_mdio_write(struct bcmpcie_softc *sc, uint8_t port, uint16_t addr,
     uint32_t data)
{
	uint32_t reg;
	int timo;

	KASSERT(port < 16);
	reg = PCIE_RC_DL_MDIO_CMD_WRITE;
	reg |= ((uint32_t)port << PCIE_RC_DL_MDIO_PORT_SHIFT);
	reg |= ((uint32_t)addr << PCIE_RC_DL_MDIO_REGAD_SHIFT);
	HWRITE4(sc, PCIE_RC_DL_MDIO_ADDR, reg);
	HREAD4(sc, PCIE_RC_DL_MDIO_ADDR);

	HWRITE4(sc, PCIE_RC_DL_MDIO_WR_DATA, data | PCIE_RC_DL_MDIO_DATA_DONE);
	for (timo = 10; timo > 0; timo--) {
		reg = HREAD4(sc, PCIE_RC_DL_MDIO_WR_DATA);
		if ((reg & PCIE_RC_DL_MDIO_DATA_DONE) == 0)
			break;
		delay(10);
	}
	if (timo == 0) {
		printf("%s: timeout\n", __func__);
		return EIO;
	}

	return 0;
}

void
bcmpcie_attach_hook(struct device *parent, struct device *self,
    struct pcibus_attach_args *pba)
{
}

int
bcmpcie_bus_maxdevs(void *v, int bus)
{
	struct bcmpcie_softc *sc = v;

	if (bus == sc->sc_bus || bus == sc->sc_bus + 1)
		return 1;
	return 32;
}

pcitag_t
bcmpcie_make_tag(void *v, int bus, int device, int function)
{
	/* Return ECAM address. */
	return ((bus << 20) | (device << 15) | (function << 12));
}

void
bcmpcie_decompose_tag(void *v, pcitag_t tag, int *bp, int *dp, int *fp)
{
	if (bp != NULL)
		*bp = (tag >> 20) & 0xff;
	if (dp != NULL)
		*dp = (tag >> 15) & 0x1f;
	if (fp != NULL)
		*fp = (tag >> 12) & 0x7;
}

int
bcmpcie_conf_size(void *v, pcitag_t tag)
{
	return PCIE_CONFIG_SPACE_SIZE;
}

pcireg_t
bcmpcie_conf_read(void *v, pcitag_t tag, int reg)
{
	struct bcmpcie_softc *sc = v;
	int bus, dev, fn;

	bcmpcie_decompose_tag(sc, tag, &bus, &dev, &fn);
	if (bus == 0) {
		KASSERT(dev == 0);
		return HREAD4(sc, tag | reg);
	}

	HWRITE4(sc, PCIE_EXT_CFG_INDEX, tag);
	return HREAD4(sc, PCIE_EXT_CFG_DATA + reg);
}

void
bcmpcie_conf_write(void *v, pcitag_t tag, int reg, pcireg_t data)
{
	struct bcmpcie_softc *sc = v;
	int bus, dev, fn;

	bcmpcie_decompose_tag(sc, tag, &bus, &dev, &fn);
	if (bus == 0) {
		KASSERT(dev == 0);
		HWRITE4(sc, tag | reg, data);
		return;
	}

	HWRITE4(sc, PCIE_EXT_CFG_INDEX, tag);
	HWRITE4(sc, PCIE_EXT_CFG_DATA + reg, data);
}

void
bcmpcie_vl805_fwload(struct bcmpcie_softc *sc, pcitag_t tag)
{
	struct request {
		struct vcprop_buffer_hdr vb_hdr;
		struct vcprop_tag_notifyxhcireset vbt_nhr;
		struct vcprop_tag end;
	} __packed;

	uint32_t result;
	struct request req = {
		.vb_hdr = {
			.vpb_len = sizeof(req),
			.vpb_rcode = VCPROP_PROCESS_REQUEST,
		},
		.vbt_nhr = {
			.tag = {
				.vpt_tag = VCPROPTAG_NOTIFY_XHCI_RESET,
				.vpt_len = VCPROPTAG_LEN(req.vbt_nhr),
				.vpt_rcode = VCPROPTAG_REQUEST,
			},
		},
		.end = {
			.vpt_tag = VCPROPTAG_NULL,
		}
	};

	/* Avoid loading the firmware multiple times. */
	if (!sc->sc_vl805_fwload)
		return;
	sc->sc_vl805_fwload = 0;

	req.vbt_nhr.deviceaddress = tag;
	bcmmbox_post(BCMMBOX_CHANARM2VC, &req, sizeof(req), &result);

	if (vcprop_tag_success_p(&req.vbt_nhr.tag)) {
		/* Wait for the device to start. */
		delay(200);
		return;
	}

	printf("%s: vcprop result %x:%x\n", __func__, req.vb_hdr.vpb_rcode,
	    req.vbt_nhr.tag.vpt_rcode);
}

int
bcmpcie_probe_device_hook(void *v, struct pci_attach_args *pa)
{
	struct bcmpcie_softc *sc = v;
	int node;

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_RPI &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_RPI_RP1) {
		node = OF_getnodebyname(sc->sc_node, "rp1");
		pa->pa_tag |= ((pcitag_t)node << 32);
	}

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_VIATECH &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_VIATECH_VL805_XHCI)
		bcmpcie_vl805_fwload(sc, pa->pa_tag);

	return 0;
}

int
bcmpcie_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
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
bcmpcie_intr_string(void *v, pci_intr_handle_t ih)
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
bcmpcie_intr_establish(void *v, pci_intr_handle_t ih, int level,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct bcmpcie_softc *sc = v;
	void *cookie;

	if (ih.ih_type != PCI_INTX) {
		uint64_t addr = 0, data;

		/* Assume hardware passes Requester ID as sideband data. */
		data = pci_requester_id(ih.ih_pc, ih.ih_tag);
		cookie = fdt_intr_establish_msi_cpu(sc->sc_node, &addr,
		    &data, level, ci, func, arg, name);
		if (cookie == NULL)
			return NULL;

		/* TODO: translate address to the PCI device's view */

		if (ih.ih_type == PCI_MSIX) {
			pci_msix_enable(ih.ih_pc, ih.ih_tag,
			    &sc->sc_bus_memt, ih.ih_intrpin, addr, data);
		} else
			pci_msi_enable(ih.ih_pc, ih.ih_tag, addr, data);
	} else {
		int bus, dev, fn;
		uint32_t reg[4];

		bcmpcie_decompose_tag(sc, ih.ih_tag, &bus, &dev, &fn);

		reg[0] = bus << 16 | dev << 11 | fn << 8;
		reg[1] = reg[2] = 0;
		reg[3] = ih.ih_intrpin;

		cookie = fdt_intr_establish_imap_cpu(sc->sc_node,
		    reg, sizeof(reg), level, ci, func, arg, name);
	}

	return cookie;
}

void
bcmpcie_intr_disestablish(void *v, void *cookie)
{
}

int
bcmpcie_bs_iomap(bus_space_tag_t t, bus_addr_t addr, bus_size_t size,
    int flags, bus_space_handle_t *bshp)
{
	struct bcmpcie_softc *sc = t->bus_private;
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
bcmpcie_bs_memmap(bus_space_tag_t t, bus_addr_t addr, bus_size_t size,
    int flags, bus_space_handle_t *bshp)
{
	struct bcmpcie_softc *sc = t->bus_private;
	int i;

	for (i = 0; i < sc->sc_nranges; i++) {
		uint64_t pci_start = sc->sc_ranges[i].pci_base;
		uint64_t pci_end = pci_start + sc->sc_ranges[i].size;
		uint64_t phys_start = sc->sc_ranges[i].phys_base;

		if ((sc->sc_ranges[i].flags & 0x03000000) == 0x02000000 &&
		    addr >= pci_start && addr + size <= pci_end) {
			return bus_space_map(sc->sc_iot,
			    addr - pci_start + phys_start, size, flags, bshp);
		}
	}
	
	return ENXIO;
}

int
bcmpcie_dmamap_load_buffer(bus_dma_tag_t t, bus_dmamap_t map, void *buf,
    bus_size_t buflen, struct proc *p, int flags, paddr_t *lastaddrp,
    int *segp, int first)
{
	struct bcmpcie_softc *sc = t->_cookie;
	paddr_t lastaddr = *lastaddrp;
	bus_size_t lastlen;
	int seg, firstseg = *segp;
	int error;

	lastlen = map->dm_segs[firstseg].ds_len;
	error = sc->sc_dmat->_dmamap_load_buffer(sc->sc_dmat, map, buf, buflen,
	    p, flags, lastaddrp, segp, first);
	if (error)
		return error;

	if (sc->sc_dmaranges == NULL)
		return 0;

	/* If we already translated the first segment, don't do it again! */
	if (!first && lastaddr == map->dm_segs[firstseg]._ds_paddr + lastlen)
		firstseg++;

	/* For each segment. */
	for (seg = firstseg; seg <= *segp; seg++) {
		uint64_t addr = map->dm_segs[seg].ds_addr;
		uint64_t size = map->dm_segs[seg].ds_len;
		int i;

		/* For each range. */
		for (i = 0; i < sc->sc_ndmaranges; i++) {
			uint64_t pci_start = sc->sc_dmaranges[i].pci_base;
			uint64_t phys_start = sc->sc_dmaranges[i].phys_base;
			uint64_t phys_end = phys_start +
			    sc->sc_dmaranges[i].size;

			if (addr >= phys_start && addr + size <= phys_end) {
				map->dm_segs[seg].ds_addr -= phys_start;
				map->dm_segs[seg].ds_addr += pci_start;
				break;
			}
		}

		if (i == sc->sc_ndmaranges)
			return EINVAL;
	}

	return 0;
}

int
bcmpcie_dmamap_load_raw(bus_dma_tag_t t, bus_dmamap_t map,
    bus_dma_segment_t *segs, int nsegs, bus_size_t size, int flags)
{
	struct bcmpcie_softc *sc = t->_cookie;
	int seg, error;

	error = sc->sc_dmat->_dmamap_load_raw(sc->sc_dmat, map,
	     segs, nsegs, size, flags);
	if (error)
		return error;

	if (sc->sc_dmaranges == NULL)
		return 0;

	/* For each segment. */
	for (seg = 0; seg < map->dm_nsegs; seg++) {
		uint64_t addr = map->dm_segs[seg].ds_addr;
		uint64_t size = map->dm_segs[seg].ds_len;
		int i;

		/* For each range. */
		for (i = 0; i < sc->sc_ndmaranges; i++) {
			uint64_t pci_start = sc->sc_dmaranges[i].pci_base;
			uint64_t phys_start = sc->sc_dmaranges[i].phys_base;
			uint64_t phys_end = phys_start +
			    sc->sc_dmaranges[i].size;

			if (addr >= phys_start && addr + size <= phys_end) {
				map->dm_segs[seg].ds_addr -= phys_start;
				map->dm_segs[seg].ds_addr += pci_start;
				break;
			}
		}

		if (i == sc->sc_ndmaranges)
			return EINVAL;
	}

	return 0;
}
