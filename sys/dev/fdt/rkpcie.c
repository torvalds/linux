/*	$OpenBSD: rkpcie.c,v 1.18 2024/02/03 10:37:26 kettenis Exp $	*/
/*
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

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_regulator.h>
#include <dev/ofw/fdt.h>

#define PCIE_CLIENT_BASIC_STRAP_CONF	0x0000
#define  PCIE_CLIENT_PCIE_GEN_SEL_1	(((1 << 7) << 16) | (0 << 7))
#define  PCIE_CLIENT_PCIE_GEN_SEL_2	(((1 << 7) << 16) | (1 << 7))
#define  PCIE_CLIENT_MODE_SELECT_RC	(((1 << 6) << 16) | (1 << 6))
#define  PCIE_CLIENT_LINK_TRAIN_EN	(((1 << 1) << 16) | (1 << 1))
#define  PCIE_CLIENT_CONF_EN		(((1 << 0) << 16) | (1 << 0))
#define PCIE_CLIENT_DEBUG_OUT_0		0x003c
#define  PCIE_CLIENT_DEBUG_LTSSM_MASK	0x0000001f
#define  PCIE_CLIENT_DEBUG_LTSSM_L0	0x00000010
#define PCIE_CLIENT_BASIC_STATUS1	0x0048
#define  PCIE_CLIENT_LINK_ST		(0x3 << 20)
#define  PCIE_CLIENT_LINK_ST_UP		(0x3 << 20)
#define PCIE_CLIENT_INT_MASK		0x004c
#define  PCIE_CLIENT_INTD_MASK		(((1 << 8) << 16) | (1 << 8))
#define  PCIE_CLIENT_INTD_UNMASK	(((1 << 8) << 16) | (0 << 8))
#define  PCIE_CLIENT_INTC_MASK		(((1 << 7) << 16) | (1 << 7))
#define  PCIE_CLIENT_INTC_UNMASK	(((1 << 7) << 16) | (0 << 7))
#define  PCIE_CLIENT_INTB_MASK		(((1 << 6) << 16) | (1 << 6))
#define  PCIE_CLIENT_INTB_UNMASK	(((1 << 6) << 16) | (0 << 6))
#define  PCIE_CLIENT_INTA_MASK		(((1 << 5) << 16) | (1 << 5))
#define  PCIE_CLIENT_INTA_UNMASK	(((1 << 5) << 16) | (0 << 5))

#define PCIE_RC_NORMAL_BASE		0x800000

#define PCIE_LM_BASE			0x900000
#define PCIE_LM_VENDOR_ID		(PCIE_LM_BASE + 0x44)
#define PCIE_LM_RCBAR			(PCIE_LM_BASE + 0x300)
#define  PCIE_LM_RCBARPIE		(1 << 19)
#define  PCIE_LM_RCBARPIS		(1 << 20)

#define PCIE_RC_BASE			0xa00000
#define PCIE_RC_PCIE_LCAP		(PCIE_RC_BASE + 0x0cc)
#define  PCIE_RC_PCIE_LCAP_APMS_L0S	(1 << 10)
#define PCIE_RC_LCSR			(PCIE_RC_BASE + 0x0d0)
#define PCIE_RC_LCSR2			(PCIE_RC_BASE + 0x0f0)

#define PCIE_ATR_BASE			0xc00000
#define PCIE_ATR_OB_ADDR0(i)		(PCIE_ATR_BASE + 0x000 + (i) * 0x20)
#define PCIE_ATR_OB_ADDR1(i)		(PCIE_ATR_BASE + 0x004 + (i) * 0x20)
#define PCIE_ATR_OB_DESC0(i)		(PCIE_ATR_BASE + 0x008 + (i) * 0x20)
#define PCIE_ATR_OB_DESC1(i)		(PCIE_ATR_BASE + 0x00c + (i) * 0x20)
#define PCIE_ATR_IB_ADDR0(i)		(PCIE_ATR_BASE + 0x800 + (i) * 0x8)
#define PCIE_ATR_IB_ADDR1(i)		(PCIE_ATR_BASE + 0x804 + (i) * 0x8)
#define  PCIE_ATR_HDR_MEM		0x2
#define  PCIE_ATR_HDR_IO		0x6
#define  PCIE_ATR_HDR_CFG_TYPE0		0xa
#define  PCIE_ATR_HDR_CFG_TYPE1		0xb
#define  PCIE_ATR_HDR_RID		(1 << 23)

#define PCIE_ATR_OB_REGION0_SIZE	(32 * 1024 * 1024)
#define PCIE_ATR_OB_REGION_SIZE		(1 * 1024 * 1024)

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct rkpcie_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_space_handle_t	sc_axi_ioh;
	bus_addr_t		sc_axi_addr;
	bus_addr_t		sc_apb_addr;
	int			sc_node;
	int			sc_phy_node;

	struct machine_pci_chipset sc_pc;
	struct extent		*sc_busex;
	struct extent		*sc_memex;
	struct extent		*sc_ioex;
	int			sc_bus;
};

int rkpcie_match(struct device *, void *, void *);
void rkpcie_attach(struct device *, struct device *, void *);

const struct cfattach	rkpcie_ca = {
	sizeof (struct rkpcie_softc), rkpcie_match, rkpcie_attach
};

struct cfdriver rkpcie_cd = {
	NULL, "rkpcie", DV_DULL
};

int
rkpcie_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "rockchip,rk3399-pcie");
}

void	rkpcie_atr_init(struct rkpcie_softc *);
void	rkpcie_phy_init(struct rkpcie_softc *);
void	rkpcie_phy_poweron(struct rkpcie_softc *);

void	rkpcie_attach_hook(struct device *, struct device *,
	    struct pcibus_attach_args *);
int	rkpcie_bus_maxdevs(void *, int);
pcitag_t rkpcie_make_tag(void *, int, int, int);
void	rkpcie_decompose_tag(void *, pcitag_t, int *, int *, int *);
int	rkpcie_conf_size(void *, pcitag_t);
pcireg_t rkpcie_conf_read(void *, pcitag_t, int);
void	rkpcie_conf_write(void *, pcitag_t, int, pcireg_t);
int	rkpcie_probe_device_hook(void *, struct pci_attach_args *);

int	rkpcie_intr_map(struct pci_attach_args *, pci_intr_handle_t *);
const char *rkpcie_intr_string(void *, pci_intr_handle_t);
void	*rkpcie_intr_establish(void *, pci_intr_handle_t, int,
	    struct cpu_info *, int (*)(void *), void *, char *);
void	rkpcie_intr_disestablish(void *, void *);

/*
 * When link training, the LTSSM configuration state exits to L0 state upon
 * success. Wait for L0 state before proceeding after link training has been
 * initiated either by PCIE_CLIENT_LINK_TRAIN_EN or when triggered via
 * LCSR Retrain Link bit. See PCIE 2.0 Base Specification, 4.2.6.3.6 
 * Configuration.Idle.
 *
 * Checking link up alone is not sufficient for checking for L0 state. LTSSM
 * state L0 can be detected when link up is set and link training is cleared.
 * See PCIE 2.0 Base Specification, 4.2.6 Link Training and Status State Rules,
 * Table 4-8 Link Status Mapped to the LTSSM.
 *
 * However, RC doesn't set the link training bit when initially training via
 * PCIE_CLIENT_LINK_TRAIN_EN. Fortunately, RC has provided a debug register
 * that has the LTSSM state which can be checked instead.
 *
 * It is important to have reached L0 state before beginning Gen 2 training,
 * as it is documented that setting the Retrain Link bit while currently
 * in Recovery or Configuration states is a race condition that may result
 * in missing the retraining. See PCIE 2.0 Base Specification, 7.8.7
 * Link Control Register implementation notes on Retrain Link bit.
 */

static int
rkpcie_link_training_wait(struct rkpcie_softc *sc)
{
	uint32_t status;
	int timo;
	for (timo = 500; timo > 0; timo--) {
		status = HREAD4(sc, PCIE_CLIENT_DEBUG_OUT_0);
		if ((status & PCIE_CLIENT_DEBUG_LTSSM_MASK) ==
		    PCIE_CLIENT_DEBUG_LTSSM_L0)
			break;
		delay(1000);
	}
	return timo == 0;
}

void
rkpcie_attach(struct device *parent, struct device *self, void *aux)
{
	struct rkpcie_softc *sc = (struct rkpcie_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct pcibus_attach_args pba;
	uint32_t *ep_gpio = NULL;
	uint32_t bus_range[2];
	uint32_t status;
	uint32_t max_link_speed;
	int len;

	if (faa->fa_nreg < 2) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;

	if (bus_space_map(sc->sc_iot, faa->fa_reg[1].addr,
	    faa->fa_reg[1].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_axi_ioh)) {
		printf(": can't map AXI registers\n");
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, faa->fa_reg[1].size);
		return;
	}

	sc->sc_axi_addr = faa->fa_reg[0].addr;
	sc->sc_apb_addr = faa->fa_reg[1].addr;
	sc->sc_node = faa->fa_node;
	printf("\n");

	len = OF_getproplen(sc->sc_node, "ep-gpios");
	if (len > 0) {
		ep_gpio = malloc(len, M_TEMP, M_WAITOK);
		OF_getpropintarray(sc->sc_node, "ep-gpios", ep_gpio, len);
	}

	max_link_speed = OF_getpropint(sc->sc_node, "max-link-speed", 1);

	clock_enable_all(sc->sc_node);

	regulator_enable(OF_getpropint(sc->sc_node, "vpcie12v-supply", 0));
	regulator_enable(OF_getpropint(sc->sc_node, "vpcie3v3-supply", 0));
	regulator_enable(OF_getpropint(sc->sc_node, "vpcie1v8-supply", 0));
	regulator_enable(OF_getpropint(sc->sc_node, "vpcie0v9-supply", 0));

	if (ep_gpio) {
		gpio_controller_config_pin(ep_gpio, GPIO_CONFIG_OUTPUT);
		gpio_controller_set_pin(ep_gpio, 0);
	}

	reset_assert(sc->sc_node, "aclk");
	reset_assert(sc->sc_node, "pclk");
	reset_assert(sc->sc_node, "pm");

	rkpcie_phy_init(sc);

	reset_assert(sc->sc_node, "core");
	reset_assert(sc->sc_node, "mgmt");
	reset_assert(sc->sc_node, "mgmt-sticky");
	reset_assert(sc->sc_node, "pipe");

	delay(10);
	
	reset_deassert(sc->sc_node, "pm");
	reset_deassert(sc->sc_node, "aclk");
	reset_deassert(sc->sc_node, "pclk");

	if (max_link_speed > 1)
		status = PCIE_CLIENT_PCIE_GEN_SEL_2;
	else
		status = PCIE_CLIENT_PCIE_GEN_SEL_1;

	/* Switch into Root Complex mode. */
	HWRITE4(sc, PCIE_CLIENT_BASIC_STRAP_CONF, PCIE_CLIENT_MODE_SELECT_RC
	    | PCIE_CLIENT_CONF_EN | status);

	rkpcie_phy_poweron(sc);

	reset_deassert(sc->sc_node, "core");
	reset_deassert(sc->sc_node, "mgmt");
	reset_deassert(sc->sc_node, "mgmt-sticky");
	reset_deassert(sc->sc_node, "pipe");

	/*
	 * Workaround RC bug where Target Link Speed is not set by GEN_SEL_2
	 */
	if (max_link_speed > 1) {
		status = HREAD4(sc, PCIE_RC_LCSR2);
		status &= ~PCI_PCIE_LCSR2_TLS;
		status |= PCI_PCIE_LCSR2_TLS_5;
		HWRITE4(sc, PCIE_RC_LCSR2, status);
	}

	/* Start link training. */
	HWRITE4(sc, PCIE_CLIENT_BASIC_STRAP_CONF, PCIE_CLIENT_LINK_TRAIN_EN);

	/* XXX Advertise power limits? */

	if (ep_gpio) {
		gpio_controller_set_pin(ep_gpio, 1);
		free(ep_gpio, M_TEMP, len);
	}

	if (rkpcie_link_training_wait(sc)) {
		printf("%s: link training timeout\n", sc->sc_dev.dv_xname);
		return;
	}

	if (max_link_speed > 1) {
		status = HREAD4(sc, PCIE_RC_LCSR);
		if ((status & PCI_PCIE_LCSR_CLS) == PCI_PCIE_LCSR_CLS_2_5) {
			HWRITE4(sc, PCIE_RC_LCSR, HREAD4(sc, PCIE_RC_LCSR) | 
			    PCI_PCIE_LCSR_RL);

			if (rkpcie_link_training_wait(sc)) {
				/* didn't make it back to L0 state */
				printf("%s: gen2 link training timeout\n",
				    sc->sc_dev.dv_xname);
				return;
			}
		}
	}

	/*
	 * XXX On at least the RockPro64, many cards will panic when first
	 * accessing PCIe config space during bus scanning. A delay after
	 * link training allows some of these cards to function.
	 */
	delay(2000000);

	/* Initialize Root Complex registers. */
	HWRITE4(sc, PCIE_LM_VENDOR_ID, PCI_VENDOR_ROCKCHIP);
	HWRITE4(sc, PCIE_RC_BASE + PCI_CLASS_REG,
	    PCI_CLASS_BRIDGE << PCI_CLASS_SHIFT |
	    PCI_SUBCLASS_BRIDGE_PCI << PCI_SUBCLASS_SHIFT);
	HWRITE4(sc, PCIE_LM_RCBAR, PCIE_LM_RCBARPIE | PCIE_LM_RCBARPIS);

	if (OF_getproplen(sc->sc_node, "aspm-no-l0s") == 0) {
		status = HREAD4(sc, PCIE_RC_PCIE_LCAP);
		status &= ~PCIE_RC_PCIE_LCAP_APMS_L0S;
		HWRITE4(sc, PCIE_RC_PCIE_LCAP, status);
	}

	/* Create extents for our address spaces. */
	sc->sc_busex = extent_create("pcibus", 0, 255,
	    M_DEVBUF, NULL, 0, EX_WAITOK | EX_FILLED);
	sc->sc_memex = extent_create("pcimem", 0, (u_long)-1,
	    M_DEVBUF, NULL, 0, EX_WAITOK | EX_FILLED);
	sc->sc_ioex = extent_create("pciio", 0, 0xffffffff,
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

	/* Configure Address Translation. */
	rkpcie_atr_init(sc);

	sc->sc_pc.pc_conf_v = sc;
	sc->sc_pc.pc_attach_hook = rkpcie_attach_hook;
	sc->sc_pc.pc_bus_maxdevs = rkpcie_bus_maxdevs;
	sc->sc_pc.pc_make_tag = rkpcie_make_tag;
	sc->sc_pc.pc_decompose_tag = rkpcie_decompose_tag;
	sc->sc_pc.pc_conf_size = rkpcie_conf_size;
	sc->sc_pc.pc_conf_read = rkpcie_conf_read;
	sc->sc_pc.pc_conf_write = rkpcie_conf_write;
	sc->sc_pc.pc_probe_device_hook = rkpcie_probe_device_hook;

	sc->sc_pc.pc_intr_v = sc;
	sc->sc_pc.pc_intr_map = rkpcie_intr_map;
	sc->sc_pc.pc_intr_map_msi = _pci_intr_map_msi;
	sc->sc_pc.pc_intr_map_msivec = _pci_intr_map_msivec;
	sc->sc_pc.pc_intr_map_msix = _pci_intr_map_msix;
	sc->sc_pc.pc_intr_string = rkpcie_intr_string;
	sc->sc_pc.pc_intr_establish = rkpcie_intr_establish;
	sc->sc_pc.pc_intr_disestablish = rkpcie_intr_disestablish;

	memset(&pba, 0, sizeof(pba));
	pba.pba_busname = "pci";
	pba.pba_iot = faa->fa_iot;
	pba.pba_memt = faa->fa_iot;
	pba.pba_dmat = faa->fa_dmat;
	pba.pba_pc = &sc->sc_pc;
	pba.pba_busex = sc->sc_busex;
	pba.pba_memex = sc->sc_memex;
	pba.pba_ioex = sc->sc_ioex;
	pba.pba_domain = pci_ndomains++;
	pba.pba_bus = sc->sc_bus;
	pba.pba_flags |= PCI_FLAGS_MSI_ENABLED;

	config_found(self, &pba, NULL);
}

void
rkpcie_atr_init(struct rkpcie_softc *sc)
{
	uint32_t *ranges = NULL;
	struct extent *ex;
	bus_addr_t addr;
	bus_size_t size, offset;
	uint32_t type;
	int len, region;
	int i;

	/* Use region 0 to map PCI configuration space. */
	HWRITE4(sc, PCIE_ATR_OB_ADDR0(0), 25 - 1);
	HWRITE4(sc, PCIE_ATR_OB_ADDR1(0), 0);
	HWRITE4(sc, PCIE_ATR_OB_DESC0(0),
	    PCIE_ATR_HDR_CFG_TYPE0 | PCIE_ATR_HDR_RID);
	HWRITE4(sc, PCIE_ATR_OB_DESC1(0), 0);

	len = OF_getproplen(sc->sc_node, "ranges");
	if (len <= 0 || (len % (7 * sizeof(uint32_t))) != 0)
		goto fail;
	ranges = malloc(len, M_TEMP, M_WAITOK);
	OF_getpropintarray(sc->sc_node, "ranges", ranges, len);

	for (i = 0; i < len / sizeof(uint32_t); i += 7) {
		/* Handle IO and MMIO. */
		switch (ranges[i] & 0x03000000) {
		case 0x01000000:
			type = PCIE_ATR_HDR_IO;
			ex = sc->sc_ioex;
			break;
		case 0x02000000:
		case 0x03000000:
			type = PCIE_ATR_HDR_MEM;
			ex = sc->sc_memex;
			break;
		default:
			continue;
		}

		/* Only support identity mappings. */
		if (ranges[i + 1] != ranges[i + 3] ||
		    ranges[i + 2] != ranges[i + 4])
			goto fail;

		/* Only support mappings aligned on a region boundary. */
		addr = ((uint64_t)ranges[i + 1] << 32) + ranges[i + 2];
		if (addr & (PCIE_ATR_OB_REGION_SIZE - 1))
			goto fail;

		/* Mappings should lie between AXI and APB regions. */
		size = ranges[i + 6];
		if (addr < sc->sc_axi_addr + PCIE_ATR_OB_REGION0_SIZE)
			goto fail;
		if (addr + size > sc->sc_apb_addr)
			goto fail;

		offset = addr - sc->sc_axi_addr - PCIE_ATR_OB_REGION0_SIZE;
		region = 1 + (offset / PCIE_ATR_OB_REGION_SIZE);
		while (size > 0) {
			HWRITE4(sc, PCIE_ATR_OB_ADDR0(region), 32 - 1);
			HWRITE4(sc, PCIE_ATR_OB_ADDR1(region), 0);
			HWRITE4(sc, PCIE_ATR_OB_DESC0(region),
			    type | PCIE_ATR_HDR_RID);
			HWRITE4(sc, PCIE_ATR_OB_DESC1(region), 0);

			extent_free(ex, addr, PCIE_ATR_OB_REGION_SIZE,
			    EX_WAITOK);
			addr += PCIE_ATR_OB_REGION_SIZE;
			size -= PCIE_ATR_OB_REGION_SIZE;
			region++;
		}
	}

	/* Passthrough inbound translations unmodified. */
	HWRITE4(sc, PCIE_ATR_IB_ADDR0(2), 32 - 1);
	HWRITE4(sc, PCIE_ATR_IB_ADDR1(2), 0);

	free(ranges, M_TEMP, len);
	return;

fail:
	printf("%s: can't map ranges\n", sc->sc_dev.dv_xname);
	free(ranges, M_TEMP, len);
}

void
rkpcie_attach_hook(struct device *parent, struct device *self,
    struct pcibus_attach_args *pba)
{
}

int
rkpcie_bus_maxdevs(void *v, int bus)
{
	struct rkpcie_softc *sc = v;

	if (bus == sc->sc_bus || bus == sc->sc_bus + 1)
		return 1;
	return 32;
}

pcitag_t
rkpcie_make_tag(void *v, int bus, int device, int function)
{
	/* Return ECAM address. */
	return ((bus << 20) | (device << 15) | (function << 12));
}

void
rkpcie_decompose_tag(void *v, pcitag_t tag, int *bp, int *dp, int *fp)
{
	if (bp != NULL)
		*bp = (tag >> 20) & 0xff;
	if (dp != NULL)
		*dp = (tag >> 15) & 0x1f;
	if (fp != NULL)
		*fp = (tag >> 12) & 0x7;
}

int
rkpcie_conf_size(void *v, pcitag_t tag)
{
	return PCIE_CONFIG_SPACE_SIZE;
}

pcireg_t
rkpcie_conf_read(void *v, pcitag_t tag, int reg)
{
	struct rkpcie_softc *sc = v;
	int bus, dev, fn;

	rkpcie_decompose_tag(sc, tag, &bus, &dev, &fn);
	if (bus == sc->sc_bus) {
		KASSERT(dev == 0);
		return HREAD4(sc, PCIE_RC_NORMAL_BASE + tag | reg);
	}
	if (bus == sc->sc_bus + 1) {
		KASSERT(dev == 0);
		return bus_space_read_4(sc->sc_iot, sc->sc_axi_ioh, tag | reg);
	}
	
	return 0xffffffff;
}

void
rkpcie_conf_write(void *v, pcitag_t tag, int reg, pcireg_t data)
{
	struct rkpcie_softc *sc = v;
	int bus, dev, fn;

	rkpcie_decompose_tag(sc, tag, &bus, &dev, &fn);
	if (bus == sc->sc_bus) {
		KASSERT(dev == 0);
		HWRITE4(sc, PCIE_RC_NORMAL_BASE + tag | reg, data);
		return;
	}
	if (bus == sc->sc_bus + 1) {
		KASSERT(dev == 0);
		bus_space_write_4(sc->sc_iot, sc->sc_axi_ioh, tag | reg, data);
		return;
	}
}

int
rkpcie_probe_device_hook(void *v, struct pci_attach_args *pa)
{
	return 0;
}

int
rkpcie_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
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
rkpcie_intr_string(void *v, pci_intr_handle_t ih)
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
rkpcie_intr_establish(void *v, pci_intr_handle_t ih, int level,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct rkpcie_softc *sc = v;
	void *cookie;

	KASSERT(ih.ih_type != PCI_NONE);

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
			    sc->sc_iot, ih.ih_intrpin, addr, data);
		} else
			pci_msi_enable(ih.ih_pc, ih.ih_tag, addr, data);
	} else {
		/* Unmask legacy interrupts. */
		HWRITE4(sc, PCIE_CLIENT_INT_MASK,
		    PCIE_CLIENT_INTA_UNMASK | PCIE_CLIENT_INTB_UNMASK |
		    PCIE_CLIENT_INTC_UNMASK | PCIE_CLIENT_INTD_UNMASK);

		cookie = fdt_intr_establish_idx_cpu(sc->sc_node, 1, level,
		    ci, func, arg, name);
	}

	return cookie;
}

void
rkpcie_intr_disestablish(void *v, void *cookie)
{
}

/*
 * PHY Support.
 */

#define RK3399_GRF_SOC_CON5_PCIE	0xe214
#define  RK3399_TX_ELEC_IDLE_OFF_MASK	((1 << 3) << 16)
#define  RK3399_TX_ELEC_IDLE_OFF	(1 << 3)
#define RK3399_GRF_SOC_CON8		0xe220
#define  RK3399_PCIE_TEST_DATA_MASK	((0xf << 7) << 16)
#define  RK3399_PCIE_TEST_DATA_SHIFT	7
#define  RK3399_PCIE_TEST_ADDR_MASK	((0x3f << 1) << 16)
#define  RK3399_PCIE_TEST_ADDR_SHIFT	1
#define  RK3399_PCIE_TEST_WRITE_ENABLE	(((1 << 0) << 16) | (1 << 0))
#define  RK3399_PCIE_TEST_WRITE_DISABLE	(((1 << 0) << 16) | (0 << 0))
#define RK3399_GRF_SOC_STATUS1		0xe2a4
#define  RK3399_PCIE_PHY_PLL_LOCKED	(1 << 9)
#define  RK3399_PCIE_PHY_PLL_OUTPUT	(1 << 10)

#define RK3399_PCIE_PHY_CFG_PLL_LOCK	0x10
#define RK3399_PCIE_PHY_CFG_CLK_TEST	0x10
#define  RK3399_PCIE_PHY_CFG_SEPE_RATE	(1 << 3)
#define RK3399_PCIE_PHY_CFG_CLK_SCC	0x12
#define  RK3399_PCIE_PHY_CFG_PLL_100M	(1 << 3)

void
rkpcie_phy_init(struct rkpcie_softc *sc)
{
	uint32_t phys[8];
	int len;

	len = OF_getpropintarray(sc->sc_node, "phys", phys, sizeof(phys));
	if (len < sizeof(phys[0]))
		return;

	sc->sc_phy_node = OF_getnodebyphandle(phys[0]);
	if (sc->sc_phy_node == 0)
		return;

	clock_set_assigned(sc->sc_phy_node);
	clock_enable(sc->sc_phy_node, "refclk");
	reset_assert(sc->sc_phy_node, "phy");
}

void
rkpcie_phy_write_conf(struct regmap *rm, uint8_t addr, uint8_t data)
{
	regmap_write_4(rm, RK3399_GRF_SOC_CON8,
	    RK3399_PCIE_TEST_ADDR_MASK |
	    (addr << RK3399_PCIE_TEST_ADDR_SHIFT) |
	    RK3399_PCIE_TEST_DATA_MASK |
	    (data << RK3399_PCIE_TEST_DATA_SHIFT) |
	    RK3399_PCIE_TEST_WRITE_DISABLE);
	delay(1);
	regmap_write_4(rm, RK3399_GRF_SOC_CON8,
	    RK3399_PCIE_TEST_WRITE_ENABLE);
	delay(1);
	regmap_write_4(rm, RK3399_GRF_SOC_CON8,
	    RK3399_PCIE_TEST_WRITE_DISABLE);
}

void
rkpcie_phy_poweron(struct rkpcie_softc *sc)
{
	struct regmap *rm;
	uint32_t status;
	int lane = 0;
	int timo;

	reset_deassert(sc->sc_phy_node, "phy");

	rm = regmap_bynode(OF_parent(sc->sc_phy_node));
	if (rm == NULL)
		return;

	regmap_write_4(rm, RK3399_GRF_SOC_CON8,
	    RK3399_PCIE_TEST_ADDR_MASK |
	    RK3399_PCIE_PHY_CFG_PLL_LOCK << RK3399_PCIE_TEST_ADDR_SHIFT);
	regmap_write_4(rm, RK3399_GRF_SOC_CON5_PCIE,
	    RK3399_TX_ELEC_IDLE_OFF_MASK << lane | 0);

	for (timo = 50; timo > 0; timo--) {
		status = regmap_read_4(rm, RK3399_GRF_SOC_STATUS1);
		if (status & RK3399_PCIE_PHY_PLL_LOCKED)
			break;
		delay(20000);
	}
	if (timo == 0) {
		printf("%s: PHY PLL lock timeout\n", sc->sc_dev.dv_xname);
		return;
	}

	rkpcie_phy_write_conf(rm, RK3399_PCIE_PHY_CFG_CLK_TEST,
	    RK3399_PCIE_PHY_CFG_SEPE_RATE);
	rkpcie_phy_write_conf(rm, RK3399_PCIE_PHY_CFG_CLK_SCC,
	    RK3399_PCIE_PHY_CFG_PLL_100M);

	for (timo = 50; timo > 0; timo--) {
		status = regmap_read_4(rm, RK3399_GRF_SOC_STATUS1);
		if ((status & RK3399_PCIE_PHY_PLL_OUTPUT) == 0)
			break;
		delay(20000);
	}
	if (timo == 0) {
		printf("%s: PHY PLL output enable timeout\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	regmap_write_4(rm, RK3399_GRF_SOC_CON8,
	    RK3399_PCIE_TEST_ADDR_MASK |
	    RK3399_PCIE_PHY_CFG_PLL_LOCK << RK3399_PCIE_TEST_ADDR_SHIFT);
		
	for (timo = 50; timo > 0; timo--) {
		status = regmap_read_4(rm, RK3399_GRF_SOC_STATUS1);
		if (status & RK3399_PCIE_PHY_PLL_LOCKED)
			break;
		delay(20000);
	}
	if (timo == 0) {
		printf("%s: PHY PLL relock timeout\n", sc->sc_dev.dv_xname);
		return;
	}
}
