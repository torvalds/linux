/*	$OpenBSD: mvkpcie.c,v 1.14 2024/02/03 10:37:26 kettenis Exp $	*/
/*
 * Copyright (c) 2018 Mark Kettenis <kettenis@openbsd.org>
 * Copyright (c) 2020 Patrick Wildt <patrick@blueri.se>
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
#include <sys/evcount.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/ppbreg.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/ofw_power.h>
#include <dev/ofw/fdt.h>

/* Registers */
#define PCIE_DEV_ID			0x0000
#define PCIE_CMD			0x0004
#define PCIE_DEV_REV			0x0008
#define PCIE_DEV_CTRL_STATS		0x00c8
#define  PCIE_DEV_CTRL_STATS_SNOOP		(1 << 1)
#define  PCIE_DEV_CTRL_STATS_RELAX_ORDER	(1 << 4)
#define  PCIE_DEV_CTRL_STATS_MAX_PAYLOAD_7	(0x7 << 5)
#define  PCIE_DEV_CTRL_STATS_MAX_RD_REQ_SZ	(0x2 << 12)
#define PCIE_LINK_CTRL_STAT		0x00d0
#define  PCIE_LINK_CTRL_STAT_LINK_L0S_ENTRY	(1 << 0)
#define  PCIE_LINK_CTRL_STAT_LINK_TRAINING	(1 << 5)
#define  PCIE_LINK_CTRL_STAT_LINK_WIDTH_1	(1 << 20)
#define PCIE_ERR_CAPCTL			0x0118
#define  PCIE_ERR_CAPCTL_ECRC_CHK_TX		(1 << 5)
#define  PCIE_ERR_CAPCTL_ECRC_CHK_TX_EN		(1 << 6)
#define  PCIE_ERR_CAPCTL_ECRC_CHCK		(1 << 7)
#define  PCIE_ERR_CAPCTL_ECRC_CHCK_RCV		(1 << 8)
#define PIO_CTRL			0x4000
#define  PIO_CTRL_TYPE_MASK			(0xf << 0)
#define  PIO_CTRL_TYPE_RD0			(0x8 << 0)
#define  PIO_CTRL_TYPE_RD1			(0x9 << 0)
#define  PIO_CTRL_TYPE_WR0			(0xa << 0)
#define  PIO_CTRL_TYPE_WR1			(0xb << 0)
#define  PIO_CTRL_ADDR_WIN_DISABLE		(1 << 24)
#define PIO_STAT			0x4004
#define  PIO_STAT_COMP_STATUS			(0x7 << 7)
#define PIO_ADDR_LS			0x4008
#define PIO_ADDR_MS			0x400c
#define PIO_WR_DATA			0x4010
#define PIO_WR_DATA_STRB		0x4014
#define  PIO_WR_DATA_STRB_VALUE			0xf
#define PIO_RD_DATA			0x4018
#define PIO_START			0x401c
#define  PIO_START_STOP				(0 << 0)
#define  PIO_START_START			(1 << 0)
#define PIO_ISR				0x4020
#define  PIO_ISR_CLEAR				(1 << 0)
#define PIO_ISRM			0x4024
#define PCIE_CORE_CTRL0			0x4800
#define  PCIE_CORE_CTRL0_GEN_1			(0 << 0)
#define  PCIE_CORE_CTRL0_GEN_2			(1 << 0)
#define  PCIE_CORE_CTRL0_GEN_3			(2 << 0)
#define  PCIE_CORE_CTRL0_GEN_MASK		(0x3 << 0)
#define  PCIE_CORE_CTRL0_IS_RC			(1 << 2)
#define  PCIE_CORE_CTRL0_LANE_1			(0 << 3)
#define  PCIE_CORE_CTRL0_LANE_2			(1 << 3)
#define  PCIE_CORE_CTRL0_LANE_4			(2 << 3)
#define  PCIE_CORE_CTRL0_LANE_8			(3 << 3)
#define  PCIE_CORE_CTRL0_LANE_MASK		(0x3 << 3)
#define  PCIE_CORE_CTRL0_LINK_TRAINING		(1 << 6)
#define PCIE_CORE_CTRL2			0x4808
#define  PCIE_CORE_CTRL2_RESERVED		(0x7 << 0)
#define  PCIE_CORE_CTRL2_TD_ENABLE		(1 << 4)
#define  PCIE_CORE_CTRL2_STRICT_ORDER_ENABLE	(1 << 5)
#define  PCIE_CORE_CTRL2_OB_WIN_ENABLE		(1 << 6)
#define  PCIE_CORE_CTRL2_MSI_ENABLE		(1 << 10)
#define PCIE_CORE_ISR0_STATUS		0x4840
#define PCIE_CORE_ISR0_MASK		0x4844
#define  PCIE_CORE_ISR0_MASK_MSI_INT		(1 << 24)
#define  PCIE_CORE_ISR0_MASK_ALL		0x07ffffff
#define PCIE_CORE_ISR1_STATUS		0x4848
#define PCIE_CORE_ISR1_MASK		0x484c
#define  PCIE_CORE_ISR1_MASK_ALL		0x00000ff0
#define  PCIE_CORE_ISR1_MASK_INTX(x)		(1 << (x + 8))
#define PCIE_CORE_MSI_ADDR_LOW		0x4850
#define PCIE_CORE_MSI_ADDR_HIGH		0x4854
#define PCIE_CORE_MSI_STATUS		0x4858
#define PCIE_CORE_MSI_MASK		0x485c
#define PCIE_CORE_MSI_PAYLOAD		0x489c
#define LMI_CFG				0x6000
#define  LMI_CFG_LTSSM_VAL(x)			(((x) >> 24) & 0x3f)
#define  LMI_CFG_LTSSM_L0			0x10
#define LMI_DEBUG_CTRL			0x6208
#define  LMI_DEBUG_CTRL_DIS_ORD_CHK		(1 << 30)
#define CTRL_CORE_CONFIG		0x18000
#define  CTRL_CORE_CONFIG_MODE_DIRECT		(0 << 0)
#define  CTRL_CORE_CONFIG_MODE_COMMAND		(1 << 0)
#define  CTRL_CORE_CONFIG_MODE_MASK		(1 << 0)
#define HOST_CTRL_INT_STATUS		0x1b000
#define HOST_CTRL_INT_MASK		0x1b004
#define  HOST_CTRL_INT_MASK_CORE_INT		(1 << 16)
#define  HOST_CTRL_INT_MASK_ALL			0xfff0fb

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct mvkpcie_dmamem {
	bus_dmamap_t		mdm_map;
	bus_dma_segment_t	mdm_seg;
	size_t			mdm_size;
	caddr_t			mdm_kva;
};

#define MVKPCIE_DMA_MAP(_mdm)	((_mdm)->mdm_map)
#define MVKPCIE_DMA_LEN(_mdm)	((_mdm)->mdm_size)
#define MVKPCIE_DMA_DVA(_mdm)	((uint64_t)(_mdm)->mdm_map->dm_segs[0].ds_addr)
#define MVKPCIE_DMA_KVA(_mdm)	((void *)(_mdm)->mdm_kva)

struct intrhand {
	int (*ih_func)(void *);		/* handler */
	void *ih_arg;			/* arg for handler */
	int ih_ipl;			/* IPL_* */
	int ih_irq;			/* IRQ number */
	struct evcount	ih_count;
	char *ih_name;
	void *ih_sc;
};

struct mvkpcie_range {
	uint32_t		flags;
	uint64_t		pci_base;
	uint64_t		phys_base;
	uint64_t		size;
};

struct mvkpcie_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_dma_tag_t		sc_dmat;

	bus_addr_t		sc_io_base;
	bus_addr_t		sc_io_bus_addr;
	bus_size_t		sc_io_size;
	bus_addr_t		sc_mem_base;
	bus_addr_t		sc_mem_bus_addr;
	bus_size_t		sc_mem_size;

	int			sc_node;
	int			sc_acells;
	int			sc_scells;
	int			sc_pacells;
	int			sc_pscells;
	struct mvkpcie_range	*sc_ranges;
	int			sc_nranges;

	struct bus_space	sc_bus_iot;
	struct bus_space	sc_bus_memt;

	struct machine_pci_chipset sc_pc;
	int			sc_bus;

	uint32_t		sc_bridge_command;
	uint32_t		sc_bridge_businfo;
	uint32_t		sc_bridge_iostatus;
	uint32_t		sc_bridge_io_hi;
	uint32_t		sc_bridge_mem;

	struct interrupt_controller sc_ic;
	struct intrhand		*sc_intx_handlers[4];
	struct interrupt_controller sc_msi_ic;
	struct intrhand		*sc_msi_handlers[32];
	struct mvkpcie_dmamem	*sc_msi_addr;
	void			*sc_ih;
	int			sc_ipl;
};

int mvkpcie_match(struct device *, void *, void *);
void mvkpcie_attach(struct device *, struct device *, void *);

const struct cfattach mvkpcie_ca = {
	sizeof (struct mvkpcie_softc), mvkpcie_match, mvkpcie_attach
};

struct cfdriver mvkpcie_cd = {
	NULL, "mvkpcie", DV_DULL
};

int
mvkpcie_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "marvell,armada-3700-pcie");
}

int	mvkpcie_link_up(struct mvkpcie_softc *);

void	mvkpcie_attach_hook(struct device *, struct device *,
	    struct pcibus_attach_args *);
int	mvkpcie_bus_maxdevs(void *, int);
pcitag_t mvkpcie_make_tag(void *, int, int, int);
void	mvkpcie_decompose_tag(void *, pcitag_t, int *, int *, int *);
int	mvkpcie_conf_size(void *, pcitag_t);
pcireg_t mvkpcie_conf_read(void *, pcitag_t, int);
void	mvkpcie_conf_write(void *, pcitag_t, int, pcireg_t);
int	mvkpcie_probe_device_hook(void *, struct pci_attach_args *);

int	mvkpcie_intr_map(struct pci_attach_args *, pci_intr_handle_t *);
const char *mvkpcie_intr_string(void *, pci_intr_handle_t);
void	*mvkpcie_intr_establish(void *, pci_intr_handle_t, int,
	    struct cpu_info *, int (*)(void *), void *, char *);
void	mvkpcie_intr_disestablish(void *, void *);

int	mvkpcie_bs_iomap(bus_space_tag_t, bus_addr_t, bus_size_t, int,
	    bus_space_handle_t *);
int	mvkpcie_bs_memmap(bus_space_tag_t, bus_addr_t, bus_size_t, int,
	    bus_space_handle_t *);

int	mvkpcie_intc_intr(void *);
void	*mvkpcie_intc_intr_establish(void *, int *, int, struct cpu_info *,
	    int (*)(void *), void *, char *);
void	mvkpcie_intc_intr_disestablish(void *);
void	*mvkpcie_intc_intr_establish_msi(void *, uint64_t *, uint64_t *,
	    int , struct cpu_info *, int (*)(void *), void *, char *);
void	mvkpcie_intc_intr_disestablish_msi(void *);
void	mvkpcie_intc_intr_barrier(void *);
void	mvkpcie_intc_recalc_ipl(struct mvkpcie_softc *);

struct mvkpcie_dmamem *mvkpcie_dmamem_alloc(struct mvkpcie_softc *, bus_size_t,
	    bus_size_t);
void	mvkpcie_dmamem_free(struct mvkpcie_softc *, struct mvkpcie_dmamem *);

void
mvkpcie_attach(struct device *parent, struct device *self, void *aux)
{
	struct mvkpcie_softc *sc = (struct mvkpcie_softc *)self;
	struct fdt_attach_args *faa = aux;
	struct pcibus_attach_args pba;
	uint32_t *reset_gpio;
	ssize_t reset_gpiolen;
	bus_addr_t iobase, iolimit;
	bus_addr_t membase, memlimit;
	uint32_t bus_range[2];
	uint32_t *ranges;
	int i, j, nranges, rangeslen;
	pcireg_t csr, bir, blr;
	uint32_t reg;
	int node;
	int timo;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

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

	sc->sc_msi_addr = mvkpcie_dmamem_alloc(sc, sizeof(uint16_t),
	    sizeof(uint64_t));
	if (sc->sc_msi_addr == NULL) {
		printf(": cannot allocate MSI address\n");
		return;
	}

	ranges = malloc(rangeslen, M_TEMP, M_WAITOK);
	OF_getpropintarray(sc->sc_node, "ranges", ranges,
	    rangeslen);

	nranges = (rangeslen / sizeof(uint32_t)) /
	    (sc->sc_acells + sc->sc_pacells + sc->sc_scells);
	sc->sc_ranges = mallocarray(nranges,
	    sizeof(struct mvkpcie_range), M_TEMP, M_WAITOK);
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

	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		free(sc->sc_ranges, M_TEMP, sc->sc_nranges *
		    sizeof(struct mvkpcie_range));
		printf(": can't map ctrl registers\n");
		return;
	}

	printf("\n");

	pinctrl_byname(sc->sc_node, "default");

	clock_set_assigned(sc->sc_node);
	clock_enable_all(sc->sc_node);

	reset_gpiolen = OF_getproplen(sc->sc_node, "reset-gpios");
	if (reset_gpiolen > 0) {
		/* Link training needs to be disabled during PCIe reset. */
		HCLR4(sc, PCIE_CORE_CTRL0, PCIE_CORE_CTRL0_LINK_TRAINING);

		reset_gpio = malloc(reset_gpiolen, M_TEMP, M_WAITOK);
		OF_getpropintarray(sc->sc_node, "reset-gpios", reset_gpio,
		    reset_gpiolen);

		/* Issue PCIe reset. */
		gpio_controller_config_pin(reset_gpio, GPIO_CONFIG_OUTPUT);
		gpio_controller_set_pin(reset_gpio, 1);
		delay(10000);
		gpio_controller_set_pin(reset_gpio, 0);

		free(reset_gpio, M_TEMP, reset_gpiolen);
	}

	reg = HREAD4(sc, CTRL_CORE_CONFIG);
	reg &= ~CTRL_CORE_CONFIG_MODE_MASK;
	reg |= CTRL_CORE_CONFIG_MODE_DIRECT;
	HWRITE4(sc, CTRL_CORE_CONFIG, reg);

	HSET4(sc, PCIE_CORE_CTRL0, PCIE_CORE_CTRL0_IS_RC);

	HWRITE4(sc, PCIE_ERR_CAPCTL,
	    PCIE_ERR_CAPCTL_ECRC_CHK_TX |
	    PCIE_ERR_CAPCTL_ECRC_CHK_TX_EN |
	    PCIE_ERR_CAPCTL_ECRC_CHCK |
	    PCIE_ERR_CAPCTL_ECRC_CHCK_RCV);

	HWRITE4(sc, PCIE_DEV_CTRL_STATS,
	    PCIE_DEV_CTRL_STATS_MAX_PAYLOAD_7 |
	    PCIE_DEV_CTRL_STATS_MAX_RD_REQ_SZ);

	HWRITE4(sc, PCIE_CORE_CTRL2,
	    PCIE_CORE_CTRL2_RESERVED |
	    PCIE_CORE_CTRL2_TD_ENABLE);

	reg = HREAD4(sc, LMI_DEBUG_CTRL);
	reg |= LMI_DEBUG_CTRL_DIS_ORD_CHK;
	HWRITE4(sc, LMI_DEBUG_CTRL, reg);

	reg = HREAD4(sc, PCIE_CORE_CTRL0);
	reg &= ~PCIE_CORE_CTRL0_GEN_MASK;
	reg |= PCIE_CORE_CTRL0_GEN_2;
	HWRITE4(sc, PCIE_CORE_CTRL0, reg);

	reg = HREAD4(sc, PCIE_CORE_CTRL0);
	reg &= ~PCIE_CORE_CTRL0_LANE_MASK;
	reg |= PCIE_CORE_CTRL0_LANE_1;
	HWRITE4(sc, PCIE_CORE_CTRL0, reg);

	HSET4(sc, PCIE_CORE_CTRL2, PCIE_CORE_CTRL2_MSI_ENABLE);

	HWRITE4(sc, PCIE_CORE_ISR0_STATUS, PCIE_CORE_ISR0_MASK_ALL);
	HWRITE4(sc, PCIE_CORE_ISR1_STATUS, PCIE_CORE_ISR1_MASK_ALL);
	HWRITE4(sc, HOST_CTRL_INT_STATUS, HOST_CTRL_INT_MASK_ALL);

	HWRITE4(sc, PCIE_CORE_ISR0_MASK, PCIE_CORE_ISR0_MASK_ALL &
	    ~PCIE_CORE_ISR0_MASK_MSI_INT);
	HWRITE4(sc, PCIE_CORE_ISR1_MASK, PCIE_CORE_ISR1_MASK_ALL);
	HWRITE4(sc, PCIE_CORE_MSI_MASK, 0);
	HWRITE4(sc, HOST_CTRL_INT_MASK, HOST_CTRL_INT_MASK_ALL &
	    ~HOST_CTRL_INT_MASK_CORE_INT);

	HSET4(sc, PCIE_CORE_CTRL2, PCIE_CORE_CTRL2_OB_WIN_ENABLE);
	HSET4(sc, PIO_CTRL, PIO_CTRL_ADDR_WIN_DISABLE);

	delay(100 * 1000);

	HSET4(sc, PCIE_CORE_CTRL0, PCIE_CORE_CTRL0_LINK_TRAINING);
	HSET4(sc, PCIE_LINK_CTRL_STAT, PCIE_LINK_CTRL_STAT_LINK_TRAINING);

	for (timo = 40; timo > 0; timo--) {
		if (mvkpcie_link_up(sc))
			break;
		delay(1000);
	}
	if (timo == 0) {
		printf("%s: timeout\n", sc->sc_dev.dv_xname);
		return;
	}

	HWRITE4(sc, PCIE_LINK_CTRL_STAT,
	    PCIE_LINK_CTRL_STAT_LINK_L0S_ENTRY |
	    PCIE_LINK_CTRL_STAT_LINK_WIDTH_1);

	HSET4(sc, PCIE_CMD, PCI_COMMAND_IO_ENABLE |
	    PCI_COMMAND_MEM_ENABLE | PCI_COMMAND_MASTER_ENABLE);

	HWRITE4(sc, PCIE_CORE_MSI_ADDR_LOW,
	    MVKPCIE_DMA_DVA(sc->sc_msi_addr) & 0xffffffff);
	HWRITE4(sc, PCIE_CORE_MSI_ADDR_HIGH,
	    MVKPCIE_DMA_DVA(sc->sc_msi_addr) >> 32);

	/* Set up address translation for I/O space. */
	sc->sc_io_bus_addr = sc->sc_mem_bus_addr = -1;
	for (i = 0; i < sc->sc_nranges; i++) {
		if ((sc->sc_ranges[i].flags & 0x03000000) == 0x01000000 &&
		    sc->sc_ranges[i].size > 0) {
			sc->sc_io_base = sc->sc_ranges[i].phys_base;
			sc->sc_io_bus_addr = sc->sc_ranges[i].pci_base;
			sc->sc_io_size = sc->sc_ranges[i].size;
		}
		if ((sc->sc_ranges[i].flags & 0x03000000) == 0x02000000 &&
		    sc->sc_ranges[i].size > 0) {
			sc->sc_mem_base = sc->sc_ranges[i].phys_base;
			sc->sc_mem_bus_addr = sc->sc_ranges[i].pci_base;
			sc->sc_mem_size = sc->sc_ranges[i].size;
		}
	}

	/* Set up bus range. */
	if (OF_getpropintarray(sc->sc_node, "bus-range", bus_range,
	    sizeof(bus_range)) != sizeof(bus_range) ||
	    bus_range[0] >= 256 || bus_range[1] >= 256) {
		bus_range[0] = 0;
		bus_range[1] = 255;
	}
	sc->sc_bus = bus_range[0];

	/* Initialize command/status. */
	csr = PCI_COMMAND_MASTER_ENABLE;
	if (sc->sc_io_size > 0)
		csr |= PCI_COMMAND_IO_ENABLE;
	if (sc->sc_mem_size > 0)
		csr |= PCI_COMMAND_MEM_ENABLE;
	sc->sc_bridge_command = csr;

	/* Initialize bus range. */
	bir = bus_range[0];
	bir |= ((bus_range[0] + 1) << 8);
	bir |= (bus_range[1] << 16);
	sc->sc_bridge_businfo = bir;

	/* Initialize I/O window. */
	iobase = sc->sc_io_bus_addr;
	iolimit = iobase + sc->sc_io_size - 1;
	blr = (iolimit & PPB_IO_MASK) | (PPB_IO_32BIT << PPB_IOLIMIT_SHIFT);
	blr |= ((iobase & PPB_IO_MASK) >> PPB_IO_SHIFT) | PPB_IO_32BIT;
	sc->sc_bridge_iostatus = blr;
	blr = (iobase & 0xffff0000) >> 16;
	blr |= iolimit & 0xffff0000;
	sc->sc_bridge_io_hi = blr;

	/* Initialize memory mapped I/O window. */
	membase = sc->sc_mem_bus_addr;
	memlimit = membase + sc->sc_mem_size - 1;
	blr = memlimit & PPB_MEM_MASK;
	blr |= (membase >> PPB_MEM_SHIFT);
	sc->sc_bridge_mem = blr;

	memcpy(&sc->sc_bus_iot, sc->sc_iot, sizeof(sc->sc_bus_iot));
	sc->sc_bus_iot.bus_private = sc;
	sc->sc_bus_iot._space_map = mvkpcie_bs_iomap;
	memcpy(&sc->sc_bus_memt, sc->sc_iot, sizeof(sc->sc_bus_memt));
	sc->sc_bus_memt.bus_private = sc;
	sc->sc_bus_memt._space_map = mvkpcie_bs_memmap;

	sc->sc_pc.pc_conf_v = sc;
	sc->sc_pc.pc_attach_hook = mvkpcie_attach_hook;
	sc->sc_pc.pc_bus_maxdevs = mvkpcie_bus_maxdevs;
	sc->sc_pc.pc_make_tag = mvkpcie_make_tag;
	sc->sc_pc.pc_decompose_tag = mvkpcie_decompose_tag;
	sc->sc_pc.pc_conf_size = mvkpcie_conf_size;
	sc->sc_pc.pc_conf_read = mvkpcie_conf_read;
	sc->sc_pc.pc_conf_write = mvkpcie_conf_write;
	sc->sc_pc.pc_probe_device_hook = mvkpcie_probe_device_hook;

	sc->sc_pc.pc_intr_v = sc;
	sc->sc_pc.pc_intr_map = mvkpcie_intr_map;
	sc->sc_pc.pc_intr_map_msi = _pci_intr_map_msi;
	sc->sc_pc.pc_intr_map_msivec = _pci_intr_map_msivec;
	sc->sc_pc.pc_intr_map_msix = _pci_intr_map_msix;
	sc->sc_pc.pc_intr_string = mvkpcie_intr_string;
	sc->sc_pc.pc_intr_establish = mvkpcie_intr_establish;
	sc->sc_pc.pc_intr_disestablish = mvkpcie_intr_disestablish;

	memset(&pba, 0, sizeof(pba));
	pba.pba_busname = "pci";
	pba.pba_iot = &sc->sc_bus_iot;
	pba.pba_memt = &sc->sc_bus_memt;
	pba.pba_dmat = faa->fa_dmat;
	pba.pba_pc = &sc->sc_pc;
	pba.pba_domain = pci_ndomains++;
	pba.pba_bus = sc->sc_bus;
	pba.pba_flags |= PCI_FLAGS_MSI_ENABLED;

	node = OF_getnodebyname(faa->fa_node, "interrupt-controller");
	if (node) {
		sc->sc_ic.ic_node = node;
		sc->sc_ic.ic_cookie = self;
		sc->sc_ic.ic_establish = mvkpcie_intc_intr_establish;
		sc->sc_ic.ic_disestablish = mvkpcie_intc_intr_disestablish;
		arm_intr_register_fdt(&sc->sc_ic);
	}

	sc->sc_msi_ic.ic_node = faa->fa_node;
	sc->sc_msi_ic.ic_cookie = self;
	sc->sc_msi_ic.ic_establish_msi = mvkpcie_intc_intr_establish_msi;
	sc->sc_msi_ic.ic_disestablish = mvkpcie_intc_intr_disestablish_msi;
	sc->sc_msi_ic.ic_barrier = mvkpcie_intc_intr_barrier;
	arm_intr_register_fdt(&sc->sc_msi_ic);

	config_found(self, &pba, NULL);
}

int
mvkpcie_link_up(struct mvkpcie_softc *sc)
{
	uint32_t reg;

	reg = HREAD4(sc, LMI_CFG);
	return LMI_CFG_LTSSM_VAL(reg) >= LMI_CFG_LTSSM_L0;
}

void
mvkpcie_attach_hook(struct device *parent, struct device *self,
    struct pcibus_attach_args *pba)
{
}

int
mvkpcie_bus_maxdevs(void *v, int bus)
{
	struct mvkpcie_softc *sc = v;

	if (bus == sc->sc_bus || bus == sc->sc_bus + 1)
		return 1;
	return 32;
}

pcitag_t
mvkpcie_make_tag(void *v, int bus, int device, int function)
{
	return ((bus << 20) | (device << 15) | (function << 12));
}

void
mvkpcie_decompose_tag(void *v, pcitag_t tag, int *bp, int *dp, int *fp)
{
	if (bp != NULL)
		*bp = (tag >> 20) & 0xff;
	if (dp != NULL)
		*dp = (tag >> 15) & 0x1f;
	if (fp != NULL)
		*fp = (tag >> 12) & 0x7;
}

int
mvkpcie_conf_size(void *v, pcitag_t tag)
{
	return PCIE_CONFIG_SPACE_SIZE;
}

pcireg_t
mvkpcie_conf_read_bridge(struct mvkpcie_softc *sc, int reg)
{
	switch (reg) {
	case PCI_ID_REG:
		return PCI_VENDOR_MARVELL |
		    (HREAD4(sc, PCIE_DEV_ID) & 0xffff0000);
	case PCI_COMMAND_STATUS_REG:
		return sc->sc_bridge_command;
	case PCI_CLASS_REG:
		return PCI_CLASS_BRIDGE << PCI_CLASS_SHIFT |
		    PCI_SUBCLASS_BRIDGE_PCI << PCI_SUBCLASS_SHIFT |
		    (HREAD4(sc, PCIE_DEV_REV) & 0xff);
	case PCI_BHLC_REG:
		return 1 << PCI_HDRTYPE_SHIFT |
		    0x10 << PCI_CACHELINE_SHIFT;
	case PPB_REG_BUSINFO:
		return sc->sc_bridge_businfo;
	case PPB_REG_IOSTATUS:
		return sc->sc_bridge_iostatus;
	case PPB_REG_MEM:
		return sc->sc_bridge_mem;
	case PPB_REG_IO_HI:
		return sc->sc_bridge_io_hi;
	case PPB_REG_PREFMEM:
	case PPB_REG_PREFBASE_HI32:
	case PPB_REG_PREFLIM_HI32:
	case PPB_REG_BRIDGECONTROL:
		return 0;
	default:
		break;
	}
	return 0;
}

void
mvkpcie_conf_write_bridge(struct mvkpcie_softc *sc, int reg, pcireg_t data)
{
	/* Treat emulated bridge registers as read-only. */
}

pcireg_t
mvkpcie_conf_read(void *v, pcitag_t tag, int off)
{
	struct mvkpcie_softc *sc = v;
	int bus, dev, fn;
	uint32_t reg;
	int i;

	mvkpcie_decompose_tag(sc, tag, &bus, &dev, &fn);
	if (bus == sc->sc_bus) {
		KASSERT(dev == 0);
		return mvkpcie_conf_read_bridge(sc, off);
	}

	HWRITE4(sc, PIO_START, PIO_START_STOP);
	HWRITE4(sc, PIO_ISR, PIO_ISR_CLEAR);
	reg = HREAD4(sc, PIO_CTRL);
	reg &= ~PIO_CTRL_TYPE_MASK;
	if (bus == sc->sc_bus + 1)
		reg |= PIO_CTRL_TYPE_RD0;
	else
		reg |= PIO_CTRL_TYPE_RD1;
	HWRITE4(sc, PIO_CTRL, reg);
	HWRITE4(sc, PIO_ADDR_LS, tag | off);
	HWRITE4(sc, PIO_ADDR_MS, 0);
	HWRITE4(sc, PIO_WR_DATA_STRB, PIO_WR_DATA_STRB_VALUE);
	HWRITE4(sc, PIO_START, PIO_START_START);

	for (i = 500; i > 0; i--) {
		if (HREAD4(sc, PIO_START) == 0 &&
		    HREAD4(sc, PIO_ISR) != 0)
			break;
		delay(2);
	}
	if (i == 0) {
		printf("%s: timeout\n", sc->sc_dev.dv_xname);
		return 0xffffffff;
	}

	return HREAD4(sc, PIO_RD_DATA);
}

void
mvkpcie_conf_write(void *v, pcitag_t tag, int off, pcireg_t data)
{
	struct mvkpcie_softc *sc = v;
	int bus, dev, fn;
	uint32_t reg;
	int i;

	mvkpcie_decompose_tag(sc, tag, &bus, &dev, &fn);
	if (bus == sc->sc_bus) {
		KASSERT(dev == 0);
		mvkpcie_conf_write_bridge(sc, off, data);
		return;
	}

	HWRITE4(sc, PIO_START, PIO_START_STOP);
	HWRITE4(sc, PIO_ISR, PIO_ISR_CLEAR);
	reg = HREAD4(sc, PIO_CTRL);
	reg &= ~PIO_CTRL_TYPE_MASK;
	if (bus == sc->sc_bus + 1)
		reg |= PIO_CTRL_TYPE_WR0;
	else
		reg |= PIO_CTRL_TYPE_WR1;
	HWRITE4(sc, PIO_CTRL, reg);
	HWRITE4(sc, PIO_ADDR_LS, tag | off);
	HWRITE4(sc, PIO_ADDR_MS, 0);
	HWRITE4(sc, PIO_WR_DATA, data);
	HWRITE4(sc, PIO_WR_DATA_STRB, PIO_WR_DATA_STRB_VALUE);
	HWRITE4(sc, PIO_START, PIO_START_START);

	for (i = 500; i > 0; i--) {
		if (HREAD4(sc, PIO_START) == 0 &&
		    HREAD4(sc, PIO_ISR) != 0)
			break;
		delay(2);
	}
	if (i == 0) {
		printf("%s: timeout\n", sc->sc_dev.dv_xname);
		return;
	}
}

int
mvkpcie_probe_device_hook(void *v, struct pci_attach_args *pa)
{
	return 0;
}

int
mvkpcie_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
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
mvkpcie_intr_string(void *v, pci_intr_handle_t ih)
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
mvkpcie_intr_establish(void *v, pci_intr_handle_t ih, int level,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct mvkpcie_softc *sc = v;
	void *cookie;

	KASSERT(ih.ih_type != PCI_NONE);

	if (ih.ih_type != PCI_INTX) {
		uint64_t addr = 0, data;

		/* Assume hardware passes Requester ID as sideband data. */
		data = pci_requester_id(ih.ih_pc, ih.ih_tag);
		cookie = fdt_intr_establish_msi_cpu(sc->sc_node, &addr,
		    &data, level, ci, func, arg, (void *)name);
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

		mvkpcie_decompose_tag(sc, ih.ih_tag, &bus, &dev, &fn);

		reg[0] = bus << 16 | dev << 11 | fn << 8;
		reg[1] = reg[2] = 0;
		reg[3] = ih.ih_intrpin;

		cookie = fdt_intr_establish_imap_cpu(sc->sc_node, reg,
		    sizeof(reg), level, ci, func, arg, name);
	}

	return cookie;
}

void
mvkpcie_intr_disestablish(void *v, void *cookie)
{
	panic("%s", __func__);
}

int
mvkpcie_bs_iomap(bus_space_tag_t t, bus_addr_t addr, bus_size_t size,
    int flags, bus_space_handle_t *bshp)
{
	struct mvkpcie_softc *sc = t->bus_private;
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
mvkpcie_bs_memmap(bus_space_tag_t t, bus_addr_t addr, bus_size_t size,
    int flags, bus_space_handle_t *bshp)
{
	struct mvkpcie_softc *sc = t->bus_private;
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
mvkpcie_intc_intr(void *cookie)
{
	struct mvkpcie_softc *sc = (struct mvkpcie_softc *)cookie;
	struct intrhand *ih;
	uint32_t pending;
	int i, s;

	if (!(HREAD4(sc, HOST_CTRL_INT_STATUS) & HOST_CTRL_INT_MASK_CORE_INT))
		return 0;

	if (HREAD4(sc, PCIE_CORE_ISR0_STATUS) & PCIE_CORE_ISR0_MASK_MSI_INT) {
		pending = HREAD4(sc, PCIE_CORE_MSI_STATUS);
		while (pending) {
			i = ffs(pending) - 1;
			HWRITE4(sc, PCIE_CORE_MSI_STATUS, (1 << i));
			pending &= ~(1 << i);

			i = HREAD4(sc, PCIE_CORE_MSI_PAYLOAD) & 0xff;
			if ((ih = sc->sc_msi_handlers[i]) != NULL) {
				s = splraise(ih->ih_ipl);
				if (ih->ih_func(ih->ih_arg))
					ih->ih_count.ec_count++;
				splx(s);
			}
		}
		HWRITE4(sc, PCIE_CORE_ISR0_STATUS, PCIE_CORE_ISR0_MASK_MSI_INT);
	}

	pending = HREAD4(sc, PCIE_CORE_ISR1_STATUS);
	for (i = 0; i < nitems(sc->sc_intx_handlers); i++) {
		if (pending & PCIE_CORE_ISR1_MASK_INTX(i)) {
			if ((ih = sc->sc_intx_handlers[i]) != NULL) {
				s = splraise(ih->ih_ipl);
				if (ih->ih_func(ih->ih_arg))
					ih->ih_count.ec_count++;
				splx(s);
			}
		}
	}
	HWRITE4(sc, PCIE_CORE_ISR1_STATUS, pending);

	HWRITE4(sc, HOST_CTRL_INT_STATUS, HOST_CTRL_INT_MASK_CORE_INT);
	return 1;
}

void *
mvkpcie_intc_intr_establish(void *cookie, int *cell, int level,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct mvkpcie_softc *sc = (struct mvkpcie_softc *)cookie;
	struct intrhand *ih;
	int irq = cell[0];
	int s;

	if (ci != NULL && !CPU_IS_PRIMARY(ci))
		return NULL;

	if (irq < 0 || irq >= nitems(sc->sc_intx_handlers))
		return NULL;

	/* Don't allow shared interrupts for now. */
	if (sc->sc_intx_handlers[irq])
		return NULL;

	ih = malloc(sizeof(*ih), M_DEVBUF, M_WAITOK);
	ih->ih_func = func;
	ih->ih_arg = arg;
	ih->ih_ipl = level & IPL_IRQMASK;
	ih->ih_irq = irq;
	ih->ih_name = name;
	ih->ih_sc = sc;

	s = splhigh();

	sc->sc_intx_handlers[irq] = ih;

	if (name != NULL)
		evcount_attach(&ih->ih_count, name, &ih->ih_irq);

	mvkpcie_intc_recalc_ipl(sc);

	splx(s);

	HCLR4(sc, PCIE_CORE_ISR1_MASK, PCIE_CORE_ISR1_MASK_INTX(irq));

	return (ih);
}

void
mvkpcie_intc_intr_disestablish(void *cookie)
{
	struct intrhand *ih = cookie;
	struct mvkpcie_softc *sc = ih->ih_sc;
	int s;

	HSET4(sc, PCIE_CORE_ISR1_MASK, PCIE_CORE_ISR1_MASK_INTX(ih->ih_irq));

	s = splhigh();

	sc->sc_intx_handlers[ih->ih_irq] = NULL;
	if (ih->ih_name != NULL)
		evcount_detach(&ih->ih_count);
	free(ih, M_DEVBUF, sizeof(*ih));

	mvkpcie_intc_recalc_ipl(sc);

	splx(s);
}

void *
mvkpcie_intc_intr_establish_msi(void *cookie, uint64_t *addr, uint64_t *data,
    int level, struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct mvkpcie_softc *sc = (struct mvkpcie_softc *)cookie;
	struct intrhand *ih;
	int i, s;

	if (ci != NULL && !CPU_IS_PRIMARY(ci))
		return NULL;

	for (i = 0; i < nitems(sc->sc_msi_handlers); i++) {
		if (sc->sc_msi_handlers[i] == NULL)
			break;
	}

	if (i == nitems(sc->sc_msi_handlers))
		return NULL;

	ih = malloc(sizeof(*ih), M_DEVBUF, M_WAITOK);
	ih->ih_func = func;
	ih->ih_arg = arg;
	ih->ih_ipl = level & IPL_IRQMASK;
	ih->ih_irq = i;
	ih->ih_name = name;
	ih->ih_sc = sc;

	s = splhigh();

	sc->sc_msi_handlers[i] = ih;

	if (name != NULL)
		evcount_attach(&ih->ih_count, name, &ih->ih_irq);

	mvkpcie_intc_recalc_ipl(sc);

	*addr = MVKPCIE_DMA_DVA(sc->sc_msi_addr);
	*data = i;

	splx(s);
	return (ih);
}

void
mvkpcie_intc_intr_disestablish_msi(void *cookie)
{
	struct intrhand *ih = cookie;
	struct mvkpcie_softc *sc = ih->ih_sc;
	int s;

	s = splhigh();

	sc->sc_msi_handlers[ih->ih_irq] = NULL;
	if (ih->ih_name != NULL)
		evcount_detach(&ih->ih_count);
	free(ih, M_DEVBUF, sizeof(*ih));

	mvkpcie_intc_recalc_ipl(sc);

	splx(s);
}

void
mvkpcie_intc_intr_barrier(void *cookie)
{
	struct intrhand *ih = cookie;
	struct mvkpcie_softc *sc = ih->ih_sc;

	intr_barrier(sc->sc_ih);
}

void
mvkpcie_intc_recalc_ipl(struct mvkpcie_softc *sc)
{
	struct intrhand *ih;
	int max = IPL_NONE;
	int min = IPL_HIGH;
	int irq;

	for (irq = 0; irq < nitems(sc->sc_intx_handlers); irq++) {
		ih = sc->sc_intx_handlers[irq];
		if (ih == NULL)
			continue;

		if (ih->ih_ipl > max)
			max = ih->ih_ipl;

		if (ih->ih_ipl < min)
			min = ih->ih_ipl;
	}

	for (irq = 0; irq < nitems(sc->sc_msi_handlers); irq++) {
		ih = sc->sc_msi_handlers[irq];
		if (ih == NULL)
			continue;

		if (ih->ih_ipl > max)
			max = ih->ih_ipl;

		if (ih->ih_ipl < min)
			min = ih->ih_ipl;
	}

	if (max == IPL_NONE)
		min = IPL_NONE;

	if (sc->sc_ipl != max) {
		sc->sc_ipl = max;

		if (sc->sc_ih != NULL)
			fdt_intr_disestablish(sc->sc_ih);

		if (sc->sc_ipl != IPL_NONE)
			sc->sc_ih = fdt_intr_establish(sc->sc_node, sc->sc_ipl,
			    mvkpcie_intc_intr, sc, sc->sc_dev.dv_xname);
	}
}

/* Only needed for the 16-bit MSI address */
struct mvkpcie_dmamem *
mvkpcie_dmamem_alloc(struct mvkpcie_softc *sc, bus_size_t size, bus_size_t align)
{
	struct mvkpcie_dmamem *mdm;
	int nsegs;

	mdm = malloc(sizeof(*mdm), M_DEVBUF, M_WAITOK | M_ZERO);
	mdm->mdm_size = size;

	if (bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW, &mdm->mdm_map) != 0)
		goto mdmfree;

	if (bus_dmamem_alloc(sc->sc_dmat, size, align, 0, &mdm->mdm_seg, 1,
	    &nsegs, BUS_DMA_WAITOK) != 0)
		goto destroy;

	if (bus_dmamem_map(sc->sc_dmat, &mdm->mdm_seg, nsegs, size,
	    &mdm->mdm_kva, BUS_DMA_WAITOK | BUS_DMA_COHERENT) != 0)
		goto free;

	if (bus_dmamap_load(sc->sc_dmat, mdm->mdm_map, mdm->mdm_kva, size,
	    NULL, BUS_DMA_WAITOK) != 0)
		goto unmap;

	bzero(mdm->mdm_kva, size);

	return (mdm);

unmap:
	bus_dmamem_unmap(sc->sc_dmat, mdm->mdm_kva, size);
free:
	bus_dmamem_free(sc->sc_dmat, &mdm->mdm_seg, 1);
destroy:
	bus_dmamap_destroy(sc->sc_dmat, mdm->mdm_map);
mdmfree:
	free(mdm, M_DEVBUF, sizeof(*mdm));

	return (NULL);
}

void
mvkpcie_dmamem_free(struct mvkpcie_softc *sc, struct mvkpcie_dmamem *mdm)
{
	bus_dmamem_unmap(sc->sc_dmat, mdm->mdm_kva, mdm->mdm_size);
	bus_dmamem_free(sc->sc_dmat, &mdm->mdm_seg, 1);
	bus_dmamap_destroy(sc->sc_dmat, mdm->mdm_map);
	free(mdm, M_DEVBUF, sizeof(*mdm));
}
