/*	$OpenBSD: dwpcie.c,v 1.57 2024/09/01 03:08:56 jsg Exp $	*/
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
#include <sys/evcount.h>
#include <sys/extent.h>
#include <sys/malloc.h>

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
#include <dev/ofw/ofw_regulator.h>
#include <dev/ofw/fdt.h>

/* Registers */
#define PCIE_PORT_LINK_CTRL		0x710
#define  PCIE_PORT_LINK_CTRL_LANES_MASK			(0x3f << 16)
#define  PCIE_PORT_LINK_CTRL_LANES_1			(0x1 << 16)
#define  PCIE_PORT_LINK_CTRL_LANES_2			(0x3 << 16)
#define  PCIE_PORT_LINK_CTRL_LANES_4			(0x7 << 16)
#define  PCIE_PORT_LINK_CTRL_LANES_8			(0xf << 16)
#define PCIE_PHY_DEBUG_R1		0x72c
#define  PCIE_PHY_DEBUG_R1_XMLH_LINK_IN_TRAINING	(1 << 29)
#define  PCIE_PHY_DEBUG_R1_XMLH_LINK_UP			(1 << 4)
#define PCIE_LINK_WIDTH_SPEED_CTRL	0x80c
#define  PCIE_LINK_WIDTH_SPEED_CTRL_LANES_MASK		(0x1f << 8)
#define  PCIE_LINK_WIDTH_SPEED_CTRL_LANES_1		(0x1 << 8)
#define  PCIE_LINK_WIDTH_SPEED_CTRL_LANES_2		(0x2 << 8)
#define  PCIE_LINK_WIDTH_SPEED_CTRL_LANES_4		(0x4 << 8)
#define  PCIE_LINK_WIDTH_SPEED_CTRL_LANES_8		(0x8 << 8)
#define  PCIE_LINK_WIDTH_SPEED_CTRL_CHANGE		(1 << 17)

#define PCIE_MSI_ADDR_LO	0x820
#define PCIE_MSI_ADDR_HI	0x824
#define PCIE_MSI_INTR_ENABLE(x)	(0x828 + (x) * 12)
#define PCIE_MSI_INTR_MASK(x)	(0x82c + (x) * 12)
#define PCIE_MSI_INTR_STATUS(x)	(0x830 + (x) * 12)

#define MISC_CONTROL_1		0x8bc
#define  MISC_CONTROL_1_DBI_RO_WR_EN	(1 << 0)
#define IATU_VIEWPORT		0x900
#define  IATU_VIEWPORT_INDEX0		0
#define  IATU_VIEWPORT_INDEX1		1
#define  IATU_VIEWPORT_INDEX2		2
#define  IATU_VIEWPORT_INDEX3		3
#define IATU_OFFSET_VIEWPORT	0x904
#define IATU_OFFSET_UNROLL(x)	(0x200 * (x))
#define IATU_REGION_CTRL_1	0x000
#define  IATU_REGION_CTRL_1_TYPE_MEM	0
#define  IATU_REGION_CTRL_1_TYPE_IO	2
#define  IATU_REGION_CTRL_1_TYPE_CFG0	4
#define  IATU_REGION_CTRL_1_TYPE_CFG1	5
#define IATU_REGION_CTRL_2	0x004
#define  IATU_REGION_CTRL_2_REGION_EN	(1U << 31)
#define IATU_LWR_BASE_ADDR	0x08
#define IATU_UPPER_BASE_ADDR	0x0c
#define IATU_LIMIT_ADDR		0x10
#define IATU_LWR_TARGET_ADDR	0x14
#define IATU_UPPER_TARGET_ADDR	0x18

/* Marvell ARMADA 8k registers */
#define PCIE_GLOBAL_CTRL	0x8000
#define  PCIE_GLOBAL_CTRL_APP_LTSSM_EN		(1 << 2)
#define  PCIE_GLOBAL_CTRL_DEVICE_TYPE_MASK	(0xf << 4)
#define  PCIE_GLOBAL_CTRL_DEVICE_TYPE_RC	(0x4 << 4)
#define PCIE_GLOBAL_STATUS	0x8008
#define  PCIE_GLOBAL_STATUS_RDLH_LINK_UP	(1 << 1)
#define  PCIE_GLOBAL_STATUS_PHY_LINK_UP		(1 << 9)
#define PCIE_PM_STATUS		0x8014
#define PCIE_GLOBAL_INT_CAUSE	0x801c
#define PCIE_GLOBAL_INT_MASK	0x8020
#define  PCIE_GLOBAL_INT_MASK_INT_A		(1 << 9)
#define  PCIE_GLOBAL_INT_MASK_INT_B		(1 << 10)
#define  PCIE_GLOBAL_INT_MASK_INT_C		(1 << 11)
#define  PCIE_GLOBAL_INT_MASK_INT_D		(1 << 12)
#define PCIE_ARCACHE_TRC	0x8050
#define  PCIE_ARCACHE_TRC_DEFAULT		0x3511
#define PCIE_AWCACHE_TRC	0x8054
#define  PCIE_AWCACHE_TRC_DEFAULT		0x5311
#define PCIE_ARUSER		0x805c
#define PCIE_AWUSER		0x8060
#define  PCIE_AXUSER_DOMAIN_MASK		(0x3 << 4)
#define  PCIE_AXUSER_DOMAIN_INNER_SHARABLE	(0x1 << 4)
#define  PCIE_AXUSER_DOMAIN_OUTER_SHARABLE	(0x2 << 4)
#define PCIE_STREAMID		0x8064
#define  PCIE_STREAMID_FUNC_BITS(x)		((x) << 0)
#define  PCIE_STREAMID_DEV_BITS(x)		((x) << 4)
#define  PCIE_STREAMID_BUS_BITS(x)		((x) << 8)
#define  PCIE_STREAMID_ROOTPORT(x)		((x) << 12)
#define  PCIE_STREAMID_8040			\
    (PCIE_STREAMID_ROOTPORT(0x80) | PCIE_STREAMID_BUS_BITS(2) | \
     PCIE_STREAMID_DEV_BITS(2) | PCIE_STREAMID_FUNC_BITS(3))

/* Amlogic G12A registers */
#define PCIE_CFG0		0x0000
#define  PCIE_CFG0_APP_LTSSM_EN			(1 << 7)
#define PCIE_STATUS12		0x0030
#define  PCIE_STATUS12_RDLH_LINK_UP		(1 << 16)
#define  PCIE_STATUS12_LTSSM_MASK		(0x1f << 10)
#define  PCIE_STATUS12_LTSSM_UP			(0x11 << 10)
#define  PCIE_STATUS12_SMLH_LINK_UP		(1 << 6)

/* NXP i.MX8MQ registers */
#define PCIE_RC_LCR				0x7c
#define  PCIE_RC_LCR_MAX_LINK_SPEEDS_GEN1		0x1
#define  PCIE_RC_LCR_MAX_LINK_SPEEDS_GEN2		0x2
#define  PCIE_RC_LCR_MAX_LINK_SPEEDS_MASK		0xf
#define  PCIE_RC_LCR_L1EL_MASK				(0x7 << 15)
#define  PCIE_RC_LCR_L1EL_64US				(0x6 << 15)

#define IOMUXC_GPR12				0x30
#define  IMX8MQ_GPR_PCIE2_DEVICE_TYPE_MASK		(0xf << 8)
#define  IMX8MQ_GPR_PCIE2_DEVICE_TYPE_RC		(0x4 << 8)
#define  IMX8MQ_GPR_PCIE1_DEVICE_TYPE_MASK		(0xf << 12)
#define  IMX8MQ_GPR_PCIE1_DEVICE_TYPE_RC		(0x4 << 12)
#define IOMUXC_GPR14				0x38
#define IOMUXC_GPR16				0x40
#define  IMX8MQ_GPR_PCIE_REF_USE_PAD			(1 << 9)
#define  IMX8MQ_GPR_PCIE_CLK_REQ_OVERRIDE_EN		(1 << 10)
#define  IMX8MQ_GPR_PCIE_CLK_REQ_OVERRIDE		(1 << 11)
#define  IMX8MM_GPR_PCIE_SSC_EN				(1 << 16)
#define  IMX8MM_GPR_PCIE_POWER_OFF			(1 << 17)
#define  IMX8MM_GPR_PCIE_CMN_RST			(1 << 18)
#define  IMX8MM_GPR_PCIE_AUX_EN				(1 << 19)
#define  IMX8MM_GPR_PCIE_REF_CLK_MASK			(0x3 << 24)
#define  IMX8MM_GPR_PCIE_REF_CLK_PLL			(0x3 << 24)
#define  IMX8MM_GPR_PCIE_REF_CLK_EXT			(0x2 << 24)

#define IMX8MM_PCIE_PHY_CMN_REG62			0x188
#define  IMX8MM_PCIE_PHY_CMN_REG62_PLL_CLK_OUT			0x08
#define IMX8MM_PCIE_PHY_CMN_REG64			0x190
#define  IMX8MM_PCIE_PHY_CMN_REG64_AUX_RX_TX_TERM		0x8c
#define IMX8MM_PCIE_PHY_CMN_REG75			0x1d4
#define  IMX8MM_PCIE_PHY_CMN_REG75_PLL_DONE			0x3
#define IMX8MM_PCIE_PHY_TRSV_REG5			0x414
#define  IMX8MM_PCIE_PHY_TRSV_REG5_GEN1_DEEMP			0x2d
#define IMX8MM_PCIE_PHY_TRSV_REG6			0x418
#define  IMX8MM_PCIE_PHY_TRSV_REG6_GEN2_DEEMP			0xf

#define ANATOP_PLLOUT_CTL			0x74
#define  ANATOP_PLLOUT_CTL_CKE				(1 << 4)
#define  ANATOP_PLLOUT_CTL_SEL_SYSPLL1			0xb
#define  ANATOP_PLLOUT_CTL_SEL_MASK			0xf
#define ANATOP_PLLOUT_DIV			0x7c
#define  ANATOP_PLLOUT_DIV_SYSPLL1			0x7

/* Rockchip RK3568/RK3588 registers */
#define PCIE_CLIENT_GENERAL_CON			0x0000
#define  PCIE_CLIENT_DEV_TYPE_RC		((0xf << 4) << 16 | (0x4 << 4))
#define  PCIE_CLIENT_LINK_REQ_RST_GRT		((1 << 3) << 16 | (1 << 3))
#define  PCIE_CLIENT_APP_LTSSM_ENABLE		((1 << 2) << 16 | (1 << 2))
#define PCIE_CLIENT_INTR_STATUS_LEGACY		0x0008
#define PCIE_CLIENT_INTR_MASK_LEGACY		0x001c
#define PCIE_CLIENT_HOT_RESET_CTRL		0x0180
#define  PCIE_CLIENT_APP_LTSSM_ENABLE_ENHANCE	((1 << 4) << 16 | (1 << 4))
#define PCIE_CLIENT_LTSSM_STATUS		0x0300
#define  PCIE_CLIENT_RDLH_LINK_UP		(1 << 17)
#define  PCIE_CLIENT_SMLH_LINK_UP		(1 << 16)
#define  PCIE_CLIENT_LTSSM_MASK			(0x1f << 0)
#define  PCIE_CLIENT_LTSSM_UP			(0x11 << 0)

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct dwpcie_range {
	uint32_t		flags;
	uint64_t		pci_base;
	uint64_t		phys_base;
	uint64_t		size;
};

struct dwpcie_intx {
	int			(*di_func)(void *);
	void			*di_arg;
	int			di_ipl;
	int			di_flags;
	int			di_pin;
	struct evcount		di_count;
	char			*di_name;
	struct dwpcie_softc	*di_sc;
	TAILQ_ENTRY(dwpcie_intx) di_next;
};

#define DWPCIE_MAX_MSI		64

struct dwpcie_msi {
	int			(*dm_func)(void *);
	void			*dm_arg;
	int			dm_ipl;
	int			dm_flags;
	int			dm_vec;
	int			dm_nvec;
	struct evcount		dm_count;
	char			*dm_name;
};

struct dwpcie_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_dma_tag_t		sc_dmat;

	bus_addr_t		sc_ctrl_base;
	bus_size_t		sc_ctrl_size;

	bus_addr_t		sc_conf_base;
	bus_size_t		sc_conf_size;
	bus_space_handle_t	sc_conf_ioh;

	bus_addr_t		sc_glue_base;
	bus_size_t		sc_glue_size;
	bus_space_handle_t	sc_glue_ioh;

	bus_addr_t		sc_atu_base;
	bus_size_t		sc_atu_size;
	bus_space_handle_t	sc_atu_ioh;

	bus_addr_t		sc_io_base;
	bus_addr_t		sc_io_bus_addr;
	bus_size_t		sc_io_size;
	bus_addr_t		sc_mem_base;
	bus_addr_t		sc_mem_bus_addr;
	bus_size_t		sc_mem_size;
	bus_addr_t		sc_pmem_base;
	bus_addr_t		sc_pmem_bus_addr;
	bus_size_t		sc_pmem_size;

	int			sc_node;
	int			sc_acells;
	int			sc_scells;
	int			sc_pacells;
	int			sc_pscells;
	struct dwpcie_range	*sc_ranges;
	int			sc_nranges;

	struct bus_space	sc_bus_iot;
	struct bus_space	sc_bus_memt;

	struct machine_pci_chipset sc_pc;
	int			sc_bus;

	int			sc_num_viewport;
	int			sc_atu_unroll;
	int			sc_atu_viewport;

	void			*sc_ih;
	struct interrupt_controller sc_ic;
	TAILQ_HEAD(,dwpcie_intx) sc_intx[4];

	void			*sc_msi_ih[2];
	uint64_t		sc_msi_addr;
	uint64_t		sc_msi_mask;
	struct dwpcie_msi	sc_msi[DWPCIE_MAX_MSI];
	int			sc_num_msi;
};

struct dwpcie_intr_handle {
	struct machine_intr_handle pih_ih;
	struct dwpcie_softc	*pih_sc;
	struct dwpcie_msi	*pih_dm;
	bus_dma_tag_t		pih_dmat;
	bus_dmamap_t		pih_map;
};

int dwpcie_match(struct device *, void *, void *);
void dwpcie_attach(struct device *, struct device *, void *);

const struct cfattach	dwpcie_ca = {
	sizeof (struct dwpcie_softc), dwpcie_match, dwpcie_attach
};

struct cfdriver dwpcie_cd = {
	NULL, "dwpcie", DV_DULL
};

int
dwpcie_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "amlogic,g12a-pcie") ||
	    OF_is_compatible(faa->fa_node, "baikal,bm1000-pcie") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx8mm-pcie") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx8mq-pcie") ||
	    OF_is_compatible(faa->fa_node, "marvell,armada8k-pcie") ||
	    OF_is_compatible(faa->fa_node, "qcom,pcie-sc8280xp") ||
	    OF_is_compatible(faa->fa_node, "qcom,pcie-x1e80100") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3568-pcie") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3588-pcie") ||
	    OF_is_compatible(faa->fa_node, "sifive,fu740-pcie"));
}

void	dwpcie_attach_deferred(struct device *);

void	dwpcie_atu_disable(struct dwpcie_softc *, int);
void	dwpcie_atu_config(struct dwpcie_softc *, int, int,
	    uint64_t, uint64_t, uint64_t);
void	dwpcie_link_config(struct dwpcie_softc *);
int	dwpcie_link_up(struct dwpcie_softc *);

int	dwpcie_armada8k_init(struct dwpcie_softc *);
int	dwpcie_armada8k_link_up(struct dwpcie_softc *);
int	dwpcie_armada8k_intr(void *);

int	dwpcie_g12a_init(struct dwpcie_softc *);
int	dwpcie_g12a_link_up(struct dwpcie_softc *);

int	dwpcie_imx8mq_init(struct dwpcie_softc *);
int	dwpcie_imx8mq_intr(void *);

int	dwpcie_fu740_init(struct dwpcie_softc *);

int	dwpcie_rk3568_init(struct dwpcie_softc *);
int	dwpcie_rk3568_intr(void *);
void	*dwpcie_rk3568_intr_establish(void *, int *, int,
 	    struct cpu_info *, int (*)(void *), void *, char *);
void	dwpcie_rk3568_intr_disestablish(void *);
void	dwpcie_rk3568_intr_barrier(void *);

int	dwpcie_sc8280xp_init(struct dwpcie_softc *);

void	dwpcie_attach_hook(struct device *, struct device *,
	    struct pcibus_attach_args *);
int	dwpcie_bus_maxdevs(void *, int);
pcitag_t dwpcie_make_tag(void *, int, int, int);
void	dwpcie_decompose_tag(void *, pcitag_t, int *, int *, int *);
int	dwpcie_conf_size(void *, pcitag_t);
pcireg_t dwpcie_conf_read(void *, pcitag_t, int);
void	dwpcie_conf_write(void *, pcitag_t, int, pcireg_t);
int	dwpcie_probe_device_hook(void *, struct pci_attach_args *);

int	dwpcie_intr_map(struct pci_attach_args *, pci_intr_handle_t *);
const char *dwpcie_intr_string(void *, pci_intr_handle_t);
void	*dwpcie_intr_establish(void *, pci_intr_handle_t, int,
	    struct cpu_info *, int (*)(void *), void *, char *);
void	dwpcie_intr_disestablish(void *, void *);

int	dwpcie_bs_iomap(bus_space_tag_t, bus_addr_t, bus_size_t, int,
	    bus_space_handle_t *);
int	dwpcie_bs_memmap(bus_space_tag_t, bus_addr_t, bus_size_t, int,
	    bus_space_handle_t *);

struct interrupt_controller dwpcie_ic = {
	.ic_barrier = intr_barrier
};

void
dwpcie_attach(struct device *parent, struct device *self, void *aux)
{
	struct dwpcie_softc *sc = (struct dwpcie_softc *)self;
	struct fdt_attach_args *faa = aux;
	uint32_t *ranges;
	int i, j, nranges, rangeslen;
	int atu, config, ctrl, glue;

	if (faa->fa_nreg < 2) {
		printf(": no registers\n");
		return;
	}

	sc->sc_ctrl_base = faa->fa_reg[0].addr;
	sc->sc_ctrl_size = faa->fa_reg[0].size;

	ctrl = OF_getindex(faa->fa_node, "dbi", "reg-names");
	if (ctrl >= 0 && ctrl < faa->fa_nreg) {
		sc->sc_ctrl_base = faa->fa_reg[ctrl].addr;
		sc->sc_ctrl_size = faa->fa_reg[ctrl].size;
	}

	config = OF_getindex(faa->fa_node, "config", "reg-names");
	if (config < 0 || config >= faa->fa_nreg) {
		printf(": no config registers\n");
		return;
	}

	sc->sc_conf_base = faa->fa_reg[config].addr;
	sc->sc_conf_size = faa->fa_reg[config].size;

	sc->sc_atu_base = sc->sc_ctrl_base + 0x300000;
	sc->sc_atu_size = sc->sc_ctrl_size - 0x300000;

	atu = OF_getindex(faa->fa_node, "atu", "reg-names");
	if (atu >= 0 && atu < faa->fa_nreg) {
		sc->sc_atu_base = faa->fa_reg[atu].addr;
		sc->sc_atu_size = faa->fa_reg[atu].size;
	}

	if (OF_is_compatible(faa->fa_node, "amlogic,g12a-pcie")) {
		glue = OF_getindex(faa->fa_node, "cfg", "reg-names");
		if (glue < 0 || glue >= faa->fa_nreg) {
			printf(": no glue registers\n");
			return;
		}

		sc->sc_glue_base = faa->fa_reg[glue].addr;
		sc->sc_glue_size = faa->fa_reg[glue].size;
	}

	if (OF_is_compatible(faa->fa_node, "rockchip,rk3568-pcie") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3588-pcie")) {
		glue = OF_getindex(faa->fa_node, "apb", "reg-names");
		if (glue < 0 || glue >= faa->fa_nreg) {
			printf(": no glue registers\n");
			return;
		}

		sc->sc_glue_base = faa->fa_reg[glue].addr;
		sc->sc_glue_size = faa->fa_reg[glue].size;
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

	ranges = malloc(rangeslen, M_TEMP, M_WAITOK);
	OF_getpropintarray(sc->sc_node, "ranges", ranges,
	    rangeslen);

	nranges = (rangeslen / sizeof(uint32_t)) /
	    (sc->sc_acells + sc->sc_pacells + sc->sc_scells);
	sc->sc_ranges = mallocarray(nranges,
	    sizeof(struct dwpcie_range), M_TEMP, M_WAITOK);
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

	if (bus_space_map(sc->sc_iot, sc->sc_ctrl_base,
	    sc->sc_ctrl_size, 0, &sc->sc_ioh)) {
		free(sc->sc_ranges, M_TEMP, sc->sc_nranges *
		    sizeof(struct dwpcie_range));
		printf(": can't map ctrl registers\n");
		return;
	}

	if (bus_space_map(sc->sc_iot, sc->sc_conf_base,
	    sc->sc_conf_size, 0, &sc->sc_conf_ioh)) {
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ctrl_size);
		free(sc->sc_ranges, M_TEMP, sc->sc_nranges *
		    sizeof(struct dwpcie_range));
		printf(": can't map config registers\n");
		return;
	}

	sc->sc_num_viewport = OF_getpropint(sc->sc_node, "num-viewport", 2);

	printf("\n");

	pinctrl_byname(sc->sc_node, "default");
	clock_set_assigned(sc->sc_node);

	config_defer(self, dwpcie_attach_deferred);
}

void
dwpcie_attach_deferred(struct device *self)
{
	struct dwpcie_softc *sc = (struct dwpcie_softc *)self;
	struct pcibus_attach_args pba;
	bus_addr_t iobase, iolimit;
	bus_addr_t membase, memlimit;
	bus_addr_t pmembase, pmemlimit;
	uint32_t bus_range[2];
	pcireg_t bir, blr, csr;
	int i, error = 0;

	if (OF_is_compatible(sc->sc_node, "marvell,armada8k-pcie"))
		error = dwpcie_armada8k_init(sc);
	if (OF_is_compatible(sc->sc_node, "amlogic,g12a-pcie"))
		error = dwpcie_g12a_init(sc);
	if (OF_is_compatible(sc->sc_node, "fsl,imx8mm-pcie") ||
	    OF_is_compatible(sc->sc_node, "fsl,imx8mq-pcie"))
		error = dwpcie_imx8mq_init(sc);
	if (OF_is_compatible(sc->sc_node, "qcom,pcie-sc8280xp") ||
	    OF_is_compatible(sc->sc_node, "qcom,pcie-x1e80100"))
		error = dwpcie_sc8280xp_init(sc);
	if (OF_is_compatible(sc->sc_node, "rockchip,rk3568-pcie") ||
	    OF_is_compatible(sc->sc_node, "rockchip,rk3588-pcie"))
		error = dwpcie_rk3568_init(sc);
	if (OF_is_compatible(sc->sc_node, "sifive,fu740-pcie"))
		error = dwpcie_fu740_init(sc);
	if (error != 0) {
		bus_space_unmap(sc->sc_iot, sc->sc_conf_ioh, sc->sc_conf_size);
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_ctrl_size);
		free(sc->sc_ranges, M_TEMP, sc->sc_nranges *
		    sizeof(struct dwpcie_range));
		printf("%s: can't initialize hardware\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	sc->sc_atu_viewport = -1;
	if (HREAD4(sc, IATU_VIEWPORT) == 0xffffffff) {
		sc->sc_atu_unroll = 1;
		if (bus_space_map(sc->sc_iot, sc->sc_atu_base,
		    sc->sc_atu_size, 0, &sc->sc_atu_ioh)) {
			bus_space_unmap(sc->sc_iot, sc->sc_conf_ioh,
			    sc->sc_conf_size);
			bus_space_unmap(sc->sc_iot, sc->sc_ioh,
			    sc->sc_ctrl_size);
			free(sc->sc_ranges, M_TEMP, sc->sc_nranges *
			    sizeof(struct dwpcie_range));
			printf("%s: can't map atu registers\n",
			    sc->sc_dev.dv_xname);
			return;
		}
	}

	/* Set up address translation for I/O space. */
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
		if ((sc->sc_ranges[i].flags & 0x03000000) == 0x03000000 &&
		    sc->sc_ranges[i].size > 0) {
			sc->sc_pmem_base = sc->sc_ranges[i].phys_base;
			sc->sc_pmem_bus_addr = sc->sc_ranges[i].pci_base;
			sc->sc_pmem_size = sc->sc_ranges[i].size;
		}
	}
	if (sc->sc_mem_size == 0) {
		printf("%s: no memory mapped I/O window\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	/*
	 * Disable prefetchable memory mapped I/O window if we don't
	 * have enough viewports to enable it.
	 */
	if (sc->sc_num_viewport < 4)
		sc->sc_pmem_size = 0;

	for (i = 0; i < sc->sc_num_viewport; i++)
		dwpcie_atu_disable(sc, i);

	dwpcie_atu_config(sc, IATU_VIEWPORT_INDEX0,
	    IATU_REGION_CTRL_1_TYPE_MEM, sc->sc_mem_base,
	    sc->sc_mem_bus_addr, sc->sc_mem_size);
	if (sc->sc_num_viewport > 2 && sc->sc_io_size > 0)
		dwpcie_atu_config(sc, IATU_VIEWPORT_INDEX2,
		    IATU_REGION_CTRL_1_TYPE_IO, sc->sc_io_base,
		    sc->sc_io_bus_addr, sc->sc_io_size);
	if (sc->sc_num_viewport > 3 && sc->sc_pmem_size > 0)
		dwpcie_atu_config(sc, IATU_VIEWPORT_INDEX3,
		    IATU_REGION_CTRL_1_TYPE_MEM, sc->sc_pmem_base,
		    sc->sc_pmem_bus_addr, sc->sc_pmem_size);

	/* Enable modification of read-only bits. */
	HSET4(sc, MISC_CONTROL_1, MISC_CONTROL_1_DBI_RO_WR_EN);

	/* A Root Port is a PCI-PCI Bridge. */
	HWRITE4(sc, PCI_CLASS_REG,
	    PCI_CLASS_BRIDGE << PCI_CLASS_SHIFT |
	    PCI_SUBCLASS_BRIDGE_PCI << PCI_SUBCLASS_SHIFT);

	/* Clear BAR as U-Boot seems to leave garbage in it. */
	HWRITE4(sc, PCI_MAPREG_START, PCI_MAPREG_MEM_TYPE_64BIT);
	HWRITE4(sc, PCI_MAPREG_START + 4, 0);

	/* Enable 32-bit I/O addressing. */
	HSET4(sc, PPB_REG_IOSTATUS,
	    PPB_IO_32BIT | (PPB_IO_32BIT << PPB_IOLIMIT_SHIFT));

	/* Make sure read-only bits are write-protected. */
	HCLR4(sc, MISC_CONTROL_1, MISC_CONTROL_1_DBI_RO_WR_EN);

	/* Set up bus range. */
	if (OF_getpropintarray(sc->sc_node, "bus-range", bus_range,
	    sizeof(bus_range)) != sizeof(bus_range)) {
		bus_range[0] = 0;
		bus_range[1] = 31;
	}
	sc->sc_bus = bus_range[0];

	/* Initialize bus range. */
	bir = bus_range[0];
	bir |= ((bus_range[0] + 1) << 8);
	bir |= (bus_range[1] << 16);
	HWRITE4(sc, PPB_REG_BUSINFO, bir);

	/* Initialize memory mapped I/O window. */
	membase = sc->sc_mem_bus_addr;
	memlimit = membase + sc->sc_mem_size - 1;
	blr = memlimit & PPB_MEM_MASK;
	blr |= (membase >> PPB_MEM_SHIFT);
	HWRITE4(sc, PPB_REG_MEM, blr);

	/* Initialize I/O window. */
	if (sc->sc_io_size > 0) {
		iobase = sc->sc_io_bus_addr;
		iolimit = iobase + sc->sc_io_size - 1;
		blr = iolimit & PPB_IO_MASK;
		blr |= (iobase >> PPB_IO_SHIFT);
		HWRITE4(sc, PPB_REG_IOSTATUS, blr);
		blr = (iobase & 0xffff0000) >> 16;
		blr |= iolimit & 0xffff0000;
		HWRITE4(sc, PPB_REG_IO_HI, blr);
	} else {
		HWRITE4(sc, PPB_REG_IOSTATUS, 0x000000ff);
		HWRITE4(sc, PPB_REG_IO_HI, 0x0000ffff);
	}

	/* Initialize prefetchable memory mapped I/O window. */
	if (sc->sc_pmem_size > 0) {
		pmembase = sc->sc_pmem_bus_addr;
		pmemlimit = pmembase + sc->sc_pmem_size - 1;
		blr = pmemlimit & PPB_MEM_MASK;
		blr |= ((pmembase & PPB_MEM_MASK) >> PPB_MEM_SHIFT);
		HWRITE4(sc, PPB_REG_PREFMEM, blr);
		HWRITE4(sc, PPB_REG_PREFBASE_HI32, pmembase >> 32);
		HWRITE4(sc, PPB_REG_PREFLIM_HI32, pmemlimit >> 32);
	} else {
		HWRITE4(sc, PPB_REG_PREFMEM, 0x0000ffff);
		HWRITE4(sc, PPB_REG_PREFBASE_HI32, 0);
		HWRITE4(sc, PPB_REG_PREFLIM_HI32, 0);
	}

	csr = PCI_COMMAND_MASTER_ENABLE | PCI_COMMAND_MEM_ENABLE;
	if (sc->sc_io_size > 0)
		csr |= PCI_COMMAND_IO_ENABLE;
	HWRITE4(sc, PCI_COMMAND_STATUS_REG, csr);

	memcpy(&sc->sc_bus_iot, sc->sc_iot, sizeof(sc->sc_bus_iot));
	sc->sc_bus_iot.bus_private = sc;
	sc->sc_bus_iot._space_map = dwpcie_bs_iomap;
	memcpy(&sc->sc_bus_memt, sc->sc_iot, sizeof(sc->sc_bus_memt));
	sc->sc_bus_memt.bus_private = sc;
	sc->sc_bus_memt._space_map = dwpcie_bs_memmap;

	sc->sc_pc.pc_conf_v = sc;
	sc->sc_pc.pc_attach_hook = dwpcie_attach_hook;
	sc->sc_pc.pc_bus_maxdevs = dwpcie_bus_maxdevs;
	sc->sc_pc.pc_make_tag = dwpcie_make_tag;
	sc->sc_pc.pc_decompose_tag = dwpcie_decompose_tag;
	sc->sc_pc.pc_conf_size = dwpcie_conf_size;
	sc->sc_pc.pc_conf_read = dwpcie_conf_read;
	sc->sc_pc.pc_conf_write = dwpcie_conf_write;
	sc->sc_pc.pc_probe_device_hook = dwpcie_probe_device_hook;

	sc->sc_pc.pc_intr_v = sc;
	sc->sc_pc.pc_intr_map = dwpcie_intr_map;
	sc->sc_pc.pc_intr_map_msi = _pci_intr_map_msi;
	sc->sc_pc.pc_intr_map_msivec = _pci_intr_map_msivec;
	sc->sc_pc.pc_intr_map_msix = _pci_intr_map_msix;
	sc->sc_pc.pc_intr_string = dwpcie_intr_string;
	sc->sc_pc.pc_intr_establish = dwpcie_intr_establish;
	sc->sc_pc.pc_intr_disestablish = dwpcie_intr_disestablish;

	memset(&pba, 0, sizeof(pba));
	pba.pba_busname = "pci";
	pba.pba_iot = &sc->sc_bus_iot;
	pba.pba_memt = &sc->sc_bus_memt;
	pba.pba_dmat = sc->sc_dmat;
	pba.pba_pc = &sc->sc_pc;
	pba.pba_domain = pci_ndomains++;
	pba.pba_bus = sc->sc_bus;

	if (OF_is_compatible(sc->sc_node, "baikal,bm1000-pcie") ||
	    OF_is_compatible(sc->sc_node, "marvell,armada8k-pcie") ||
	    OF_getproplen(sc->sc_node, "msi-map") > 0 ||
	    sc->sc_msi_addr)
		pba.pba_flags |= PCI_FLAGS_MSI_ENABLED;

	/*
	 * Only support multiple MSI vectors if we have enough MSI
	 * interrupts (or are using an external interrupt controller
	 * that hopefully supports plenty of MSI interrupts).
	 */
	if (OF_getproplen(sc->sc_node, "msi-map") > 0 ||
	    sc->sc_num_msi > 32)
		pba.pba_flags |= PCI_FLAGS_MSIVEC_ENABLED;

	pci_dopm = 1;

	config_found(self, &pba, NULL);
}

void
dwpcie_link_config(struct dwpcie_softc *sc)
{
	uint32_t mode, width, reg;
	int lanes;

	lanes = OF_getpropint(sc->sc_node, "num-lanes", 0);

	switch (lanes) {
	case 1:
		mode = PCIE_PORT_LINK_CTRL_LANES_1;
		width = PCIE_LINK_WIDTH_SPEED_CTRL_LANES_1;
		break;
	case 2:
		mode = PCIE_PORT_LINK_CTRL_LANES_2;
		width = PCIE_LINK_WIDTH_SPEED_CTRL_LANES_2;
		break;
	case 4:
		mode = PCIE_PORT_LINK_CTRL_LANES_4;
		width = PCIE_LINK_WIDTH_SPEED_CTRL_LANES_4;
		break;
	case 8:
		mode = PCIE_PORT_LINK_CTRL_LANES_8;
		width = PCIE_LINK_WIDTH_SPEED_CTRL_LANES_8;
		break;
	default:
		printf("%s: %d lanes not supported\n", __func__, lanes);
		return;
	}

	reg = HREAD4(sc, PCIE_PORT_LINK_CTRL);
	reg &= ~PCIE_PORT_LINK_CTRL_LANES_MASK;
	reg |= mode;
	HWRITE4(sc, PCIE_PORT_LINK_CTRL, reg);

	reg = HREAD4(sc, PCIE_LINK_WIDTH_SPEED_CTRL);
	reg &= ~PCIE_LINK_WIDTH_SPEED_CTRL_LANES_MASK;
	reg |= width;
	HWRITE4(sc, PCIE_LINK_WIDTH_SPEED_CTRL, reg);

	reg = HREAD4(sc, PCIE_LINK_WIDTH_SPEED_CTRL);
	reg |= PCIE_LINK_WIDTH_SPEED_CTRL_CHANGE;
	HWRITE4(sc, PCIE_LINK_WIDTH_SPEED_CTRL, reg);
}

int
dwpcie_msi_intr(struct dwpcie_softc *sc, int idx)
{
	struct dwpcie_msi *dm;
	uint32_t status;
	int vec, s;

	status = HREAD4(sc, PCIE_MSI_INTR_STATUS(idx));
	if (status == 0)
		return 0;

	HWRITE4(sc, PCIE_MSI_INTR_STATUS(idx), status);
	while (status) {
		vec = ffs(status) - 1;
		status &= ~(1U << vec);

		dm = &sc->sc_msi[idx * 32 + vec];
		if (dm->dm_func == NULL)
			continue;

		if ((dm->dm_flags & IPL_MPSAFE) == 0)
			KERNEL_LOCK();
		s = splraise(dm->dm_ipl);
		if (dm->dm_func(dm->dm_arg))
			dm->dm_count.ec_count++;
		splx(s);
		if ((dm->dm_flags & IPL_MPSAFE) == 0)
			KERNEL_UNLOCK();
	}

	return 1;
}

int
dwpcie_msi0_intr(void *arg)
{
	return dwpcie_msi_intr(arg, 0);
}

int
dwpcie_msi1_intr(void *arg)
{
	return dwpcie_msi_intr(arg, 1);
}

int
dwpcie_msi_init(struct dwpcie_softc *sc)
{
	bus_dma_segment_t seg;
	bus_dmamap_t map;
	uint64_t addr;
	int error, rseg;
	int idx;

	/*
	 * Allocate some DMA memory such that we have a "safe" target
	 * address for MSIs.
	 */
	error = bus_dmamem_alloc(sc->sc_dmat, sizeof(uint32_t),
	    sizeof(uint32_t), 0, &seg, 1, &rseg, BUS_DMA_WAITOK);
	if (error)
		return error;

	/*
	 * Translate the CPU address into a bus address that we can
	 * program into the hardware.
	 */
	error = bus_dmamap_create(sc->sc_dmat, sizeof(uint32_t), 1,
	    sizeof(uint32_t), 0, BUS_DMA_WAITOK, &map);
	if (error) {
		bus_dmamem_free(sc->sc_dmat, &seg, 1);
		return error;
	}
	error = bus_dmamap_load_raw(sc->sc_dmat, map, &seg, 1,
	    sizeof(uint32_t), BUS_DMA_WAITOK);
	if (error) {
		bus_dmamap_destroy(sc->sc_dmat, map);
		bus_dmamem_free(sc->sc_dmat, &seg, 1);
		return error;
	}

	addr = map->dm_segs[0].ds_addr;
	HWRITE4(sc, PCIE_MSI_ADDR_LO, addr);
	HWRITE4(sc, PCIE_MSI_ADDR_HI, addr >> 32);

	bus_dmamap_unload(sc->sc_dmat, map);
	bus_dmamap_destroy(sc->sc_dmat, map);

	/*
	 * See if the device tree indicates that the hardware supports
	 * more than 32 vectors.  Some hardware supports more than 64,
	 * but 64 is good enough for now.
	 */
	idx = OF_getindex(sc->sc_node, "msi1", "interrupt-names");
	if (idx == -1)
		sc->sc_num_msi = 32;
	else
		sc->sc_num_msi = 64;
	KASSERT(sc->sc_num_msi <= DWPCIE_MAX_MSI);

	/* Enable, mask and clear all MSIs. */
	for (idx = 0; idx < sc->sc_num_msi / 32; idx++) {
		HWRITE4(sc, PCIE_MSI_INTR_ENABLE(idx), 0xffffffff);
		HWRITE4(sc, PCIE_MSI_INTR_MASK(idx), 0xffffffff);
		HWRITE4(sc, PCIE_MSI_INTR_STATUS(idx), 0xffffffff);
	}

	idx = OF_getindex(sc->sc_node, "msi0", "interrupt-names");
	if (idx == -1)
		idx = 0;

	sc->sc_msi_ih[0] = fdt_intr_establish_idx(sc->sc_node, idx,
	    IPL_BIO | IPL_MPSAFE, dwpcie_msi0_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_msi_ih[0] == NULL) {
		bus_dmamem_free(sc->sc_dmat, &seg, 1);
		return EINVAL;
	}

	idx = OF_getindex(sc->sc_node, "msi1", "interrupt-names");
	if (idx == -1)
		goto finish;

	sc->sc_msi_ih[1] = fdt_intr_establish_idx(sc->sc_node, idx,
	    IPL_BIO | IPL_MPSAFE, dwpcie_msi1_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_msi_ih[1] == NULL)
		sc->sc_num_msi = 32;

finish:
	/*
	 * Hold on to the DMA memory such that nobody can use it to
	 * actually do DMA transfers.
	 */

	sc->sc_msi_addr = addr;
	return 0;
}

int
dwpcie_armada8k_init(struct dwpcie_softc *sc)
{
	uint32_t reg;
	int timo;

	clock_enable_all(sc->sc_node);

	dwpcie_link_config(sc);

	if (!dwpcie_armada8k_link_up(sc)) {
		reg = HREAD4(sc, PCIE_GLOBAL_CTRL);
		reg &= ~PCIE_GLOBAL_CTRL_APP_LTSSM_EN;
		HWRITE4(sc, PCIE_GLOBAL_CTRL, reg);
	}

	/*
	 * Setup Requester-ID to Stream-ID mapping
	 * XXX: TF-A is supposed to set this up, but doesn't!
	 */
	HWRITE4(sc, PCIE_STREAMID, PCIE_STREAMID_8040);

	/* Enable Root Complex mode. */
	reg = HREAD4(sc, PCIE_GLOBAL_CTRL);
	reg &= ~PCIE_GLOBAL_CTRL_DEVICE_TYPE_MASK;
	reg |= PCIE_GLOBAL_CTRL_DEVICE_TYPE_RC;
	HWRITE4(sc, PCIE_GLOBAL_CTRL, reg);

	HWRITE4(sc, PCIE_ARCACHE_TRC, PCIE_ARCACHE_TRC_DEFAULT);
	HWRITE4(sc, PCIE_AWCACHE_TRC, PCIE_AWCACHE_TRC_DEFAULT);
	reg = HREAD4(sc, PCIE_ARUSER);
	reg &= ~PCIE_AXUSER_DOMAIN_MASK;
	reg |= PCIE_AXUSER_DOMAIN_OUTER_SHARABLE;
	HWRITE4(sc, PCIE_ARUSER, reg);
	reg = HREAD4(sc, PCIE_AWUSER);
	reg &= ~PCIE_AXUSER_DOMAIN_MASK;
	reg |= PCIE_AXUSER_DOMAIN_OUTER_SHARABLE;
	HWRITE4(sc, PCIE_AWUSER, reg);

	if (!dwpcie_armada8k_link_up(sc)) {
		reg = HREAD4(sc, PCIE_GLOBAL_CTRL);
		reg |= PCIE_GLOBAL_CTRL_APP_LTSSM_EN;
		HWRITE4(sc, PCIE_GLOBAL_CTRL, reg);
	}

	for (timo = 40; timo > 0; timo--) {
		if (dwpcie_armada8k_link_up(sc))
			break;
		delay(1000);
	}
	if (timo == 0)
		return ETIMEDOUT;

	sc->sc_ih = fdt_intr_establish(sc->sc_node, IPL_AUDIO | IPL_MPSAFE,
	    dwpcie_armada8k_intr, sc, sc->sc_dev.dv_xname);

	/* Unmask INTx interrupts. */
	HWRITE4(sc, PCIE_GLOBAL_INT_MASK,
	    PCIE_GLOBAL_INT_MASK_INT_A | PCIE_GLOBAL_INT_MASK_INT_B |
	    PCIE_GLOBAL_INT_MASK_INT_C | PCIE_GLOBAL_INT_MASK_INT_D);

	return 0;
}

int
dwpcie_armada8k_link_up(struct dwpcie_softc *sc)
{
	uint32_t reg, mask;

	mask = PCIE_GLOBAL_STATUS_RDLH_LINK_UP;
	mask |= PCIE_GLOBAL_STATUS_PHY_LINK_UP;
	reg = HREAD4(sc, PCIE_GLOBAL_STATUS);
	return ((reg & mask) == mask);
}

int
dwpcie_armada8k_intr(void *arg)
{
	struct dwpcie_softc *sc = arg;
	uint32_t cause;

	/* Acknowledge interrupts. */
	cause = HREAD4(sc, PCIE_GLOBAL_INT_CAUSE);
	HWRITE4(sc, PCIE_GLOBAL_INT_CAUSE, cause);

	/* INTx interrupt, so not really ours. */
	return 0;
}

int
dwpcie_g12a_init(struct dwpcie_softc *sc)
{
	uint32_t *reset_gpio;
	ssize_t reset_gpiolen;
	uint32_t reg;
	int error, timo;

	reset_gpiolen = OF_getproplen(sc->sc_node, "reset-gpios");
	if (reset_gpiolen <= 0)
		return ENXIO;

	if (bus_space_map(sc->sc_iot, sc->sc_glue_base,
	    sc->sc_glue_size, 0, &sc->sc_glue_ioh))
		return ENOMEM;

	power_domain_enable(sc->sc_node);

	phy_enable(sc->sc_node, "pcie");

	reset_assert_all(sc->sc_node);
	delay(500);
	reset_deassert_all(sc->sc_node);
	delay(500);

	clock_set_frequency(sc->sc_node, "port", 100000000UL);
	clock_enable_all(sc->sc_node);

	reset_gpio = malloc(reset_gpiolen, M_TEMP, M_WAITOK);
	OF_getpropintarray(sc->sc_node, "reset-gpios", reset_gpio,
	    reset_gpiolen);
	gpio_controller_config_pin(reset_gpio, GPIO_CONFIG_OUTPUT);
	gpio_controller_set_pin(reset_gpio, 1);

	dwpcie_link_config(sc);

	reg = bus_space_read_4(sc->sc_iot, sc->sc_glue_ioh, PCIE_CFG0);
	reg |= PCIE_CFG0_APP_LTSSM_EN;
	bus_space_write_4(sc->sc_iot, sc->sc_glue_ioh, PCIE_CFG0, reg);

	gpio_controller_set_pin(reset_gpio, 1);
	delay(500);
	gpio_controller_set_pin(reset_gpio, 0);

	free(reset_gpio, M_TEMP, reset_gpiolen);

	for (timo = 40; timo > 0; timo--) {
		if (dwpcie_g12a_link_up(sc))
			break;
		delay(1000);
	}
	if (timo == 0)
		return ETIMEDOUT;

	error = dwpcie_msi_init(sc);
	if (error)
		return error;

	return 0;
}

int
dwpcie_g12a_link_up(struct dwpcie_softc *sc)
{
	uint32_t reg;

	reg = bus_space_read_4(sc->sc_iot, sc->sc_glue_ioh, PCIE_STATUS12);
	if ((reg & PCIE_STATUS12_SMLH_LINK_UP) &&
	    (reg & PCIE_STATUS12_RDLH_LINK_UP) &&
	    (reg & PCIE_STATUS12_LTSSM_MASK) == PCIE_STATUS12_LTSSM_UP)
		return 1;
	return 0;
}

int
dwpcie_imx8mq_init(struct dwpcie_softc *sc)
{
	uint32_t *clkreq_gpio, *disable_gpio, *reset_gpio;
	ssize_t clkreq_gpiolen, disable_gpiolen, reset_gpiolen;
	struct regmap *anatop, *gpr, *phy;
	uint32_t off, reg;
	int error, timo;

	if (OF_is_compatible(sc->sc_node, "fsl,imx8mm-pcie")) {
		anatop = regmap_bycompatible("fsl,imx8mm-anatop");
		gpr = regmap_bycompatible("fsl,imx8mm-iomuxc-gpr");
		phy = regmap_bycompatible("fsl,imx7d-pcie-phy");
		KASSERT(phy != NULL);
	} else {
		anatop = regmap_bycompatible("fsl,imx8mq-anatop");
		gpr = regmap_bycompatible("fsl,imx8mq-iomuxc-gpr");
	}
	KASSERT(anatop != NULL);
	KASSERT(gpr != NULL);

	clkreq_gpiolen = OF_getproplen(sc->sc_node, "clkreq-gpio");
	disable_gpiolen = OF_getproplen(sc->sc_node, "disable-gpio");
	reset_gpiolen = OF_getproplen(sc->sc_node, "reset-gpio");

	if (clkreq_gpiolen > 0) {
		clkreq_gpio = malloc(clkreq_gpiolen, M_TEMP, M_WAITOK);
		OF_getpropintarray(sc->sc_node, "clkreq-gpio", clkreq_gpio,
		    clkreq_gpiolen);
		gpio_controller_config_pin(clkreq_gpio, GPIO_CONFIG_OUTPUT);
		gpio_controller_set_pin(clkreq_gpio, 1);
	}

	if (disable_gpiolen > 0) {
		disable_gpio = malloc(disable_gpiolen, M_TEMP, M_WAITOK);
		OF_getpropintarray(sc->sc_node, "disable-gpio", disable_gpio,
		    disable_gpiolen);
		gpio_controller_config_pin(disable_gpio, GPIO_CONFIG_OUTPUT);
		gpio_controller_set_pin(disable_gpio, 0);
	}

	if (reset_gpiolen > 0) {
		reset_gpio = malloc(reset_gpiolen, M_TEMP, M_WAITOK);
		OF_getpropintarray(sc->sc_node, "reset-gpio", reset_gpio,
		    reset_gpiolen);
		gpio_controller_config_pin(reset_gpio, GPIO_CONFIG_OUTPUT);
		gpio_controller_set_pin(reset_gpio, 1);
	}

	power_domain_enable(sc->sc_node);
	reset_assert(sc->sc_node, "pciephy");
	reset_assert(sc->sc_node, "apps");

	reg = regmap_read_4(gpr, IOMUXC_GPR12);
	if (OF_getpropint(sc->sc_node, "ctrl-id", 0) == 0) {
		off = IOMUXC_GPR14;
		reg &= ~IMX8MQ_GPR_PCIE1_DEVICE_TYPE_MASK;
		reg |= IMX8MQ_GPR_PCIE1_DEVICE_TYPE_RC;
	} else {
		off = IOMUXC_GPR16;
		reg &= ~IMX8MQ_GPR_PCIE2_DEVICE_TYPE_MASK;
		reg |= IMX8MQ_GPR_PCIE2_DEVICE_TYPE_RC;
	}
	regmap_write_4(gpr, IOMUXC_GPR12, reg);

	if (OF_is_compatible(sc->sc_node, "fsl,imx8mm-pcie")) {
		if (OF_getproplen(sc->sc_node, "ext_osc") == 0 ||
		    OF_getpropint(sc->sc_node, "ext_osc", 0)) {
			reg = regmap_read_4(gpr, off);
			reg &= ~(IMX8MQ_GPR_PCIE_REF_USE_PAD |
			    IMX8MM_GPR_PCIE_SSC_EN |
			    IMX8MM_GPR_PCIE_POWER_OFF |
			    IMX8MM_GPR_PCIE_REF_CLK_MASK);
			reg |= (IMX8MM_GPR_PCIE_AUX_EN |
			    IMX8MM_GPR_PCIE_REF_CLK_EXT);
			regmap_write_4(gpr, off, reg);
			delay(100);
			reg = regmap_read_4(gpr, off);
			reg |= IMX8MM_GPR_PCIE_CMN_RST;
			regmap_write_4(gpr, off, reg);
			delay(200);
		} else {
			reg = regmap_read_4(gpr, off);
			reg &= ~(IMX8MQ_GPR_PCIE_REF_USE_PAD |
			    IMX8MM_GPR_PCIE_SSC_EN |
			    IMX8MM_GPR_PCIE_POWER_OFF |
			    IMX8MM_GPR_PCIE_REF_CLK_MASK);
			reg |= (IMX8MM_GPR_PCIE_AUX_EN |
			    IMX8MM_GPR_PCIE_REF_CLK_PLL);
			regmap_write_4(gpr, off, reg);
			delay(100);
			regmap_write_4(phy, IMX8MM_PCIE_PHY_CMN_REG62,
			    IMX8MM_PCIE_PHY_CMN_REG62_PLL_CLK_OUT);
			regmap_write_4(phy, IMX8MM_PCIE_PHY_CMN_REG64,
			    IMX8MM_PCIE_PHY_CMN_REG64_AUX_RX_TX_TERM);
			reg = regmap_read_4(gpr, off);
			reg |= IMX8MM_GPR_PCIE_CMN_RST;
			regmap_write_4(gpr, off, reg);
			delay(200);
			regmap_write_4(phy, IMX8MM_PCIE_PHY_TRSV_REG5,
			    IMX8MM_PCIE_PHY_TRSV_REG5_GEN1_DEEMP);
			regmap_write_4(phy, IMX8MM_PCIE_PHY_TRSV_REG6,
			    IMX8MM_PCIE_PHY_TRSV_REG6_GEN2_DEEMP);
		}
	} else {
		if (OF_getproplen(sc->sc_node, "ext_osc") == 0 ||
		    OF_getpropint(sc->sc_node, "ext_osc", 0)) {
			reg = regmap_read_4(gpr, off);
			reg |= IMX8MQ_GPR_PCIE_REF_USE_PAD;
			regmap_write_4(gpr, off, reg);
		} else {
			reg = regmap_read_4(gpr, off);
			reg &= ~IMX8MQ_GPR_PCIE_REF_USE_PAD;
			regmap_write_4(gpr, off, reg);

			regmap_write_4(anatop, ANATOP_PLLOUT_CTL,
			    ANATOP_PLLOUT_CTL_CKE |
			    ANATOP_PLLOUT_CTL_SEL_SYSPLL1);
			regmap_write_4(anatop, ANATOP_PLLOUT_DIV,
			    ANATOP_PLLOUT_DIV_SYSPLL1);
		}
	}

	clock_enable(sc->sc_node, "pcie_phy");
	clock_enable(sc->sc_node, "pcie_bus");
	clock_enable(sc->sc_node, "pcie");
	clock_enable(sc->sc_node, "pcie_aux");

	/* Allow clocks to stabilize. */
	delay(200);

	if (reset_gpiolen > 0) {
		gpio_controller_set_pin(reset_gpio, 1);
		delay(100000);
		gpio_controller_set_pin(reset_gpio, 0);
	}

	reset_deassert(sc->sc_node, "pciephy");

	if (OF_is_compatible(sc->sc_node, "fsl,imx8mm-pcie")) {
		for (timo = 2000; timo > 0; timo--) {
			if (regmap_read_4(phy, IMX8MM_PCIE_PHY_CMN_REG75) ==
			    IMX8MM_PCIE_PHY_CMN_REG75_PLL_DONE)
				break;
			delay(10);
		}
		if (timo == 0) {
			error = ETIMEDOUT;
			goto err;
		}
	}

	reg = HREAD4(sc, 0x100000 + PCIE_RC_LCR);
	reg &= ~PCIE_RC_LCR_L1EL_MASK;
	reg |= PCIE_RC_LCR_L1EL_64US;
	HWRITE4(sc, 0x100000 + PCIE_RC_LCR, reg);

	dwpcie_link_config(sc);

	reg = HREAD4(sc, PCIE_RC_LCR);
	reg &= ~PCIE_RC_LCR_MAX_LINK_SPEEDS_MASK;
	reg |= PCIE_RC_LCR_MAX_LINK_SPEEDS_GEN1;
	HWRITE4(sc, PCIE_RC_LCR, reg);

	reset_deassert(sc->sc_node, "apps");

	for (timo = 20000; timo > 0; timo--) {
		if (dwpcie_link_up(sc))
			break;
		delay(10);
	}
	if (timo == 0) {
		error = ETIMEDOUT;
		goto err;
	}

	if (OF_getpropint(sc->sc_node, "fsl,max-link-speed", 1) >= 2) {
		reg = HREAD4(sc, PCIE_RC_LCR);
		reg &= ~PCIE_RC_LCR_MAX_LINK_SPEEDS_MASK;
		reg |= PCIE_RC_LCR_MAX_LINK_SPEEDS_GEN2;
		HWRITE4(sc, PCIE_RC_LCR, reg);

		reg = HREAD4(sc, PCIE_LINK_WIDTH_SPEED_CTRL);
		reg |= PCIE_LINK_WIDTH_SPEED_CTRL_CHANGE;
		HWRITE4(sc, PCIE_LINK_WIDTH_SPEED_CTRL, reg);

		for (timo = 20000; timo > 0; timo--) {
			if (dwpcie_link_up(sc))
				break;
			delay(10);
		}
		if (timo == 0) {
			error = ETIMEDOUT;
			goto err;
		}
	}

	sc->sc_ih = fdt_intr_establish(sc->sc_node, IPL_AUDIO | IPL_MPSAFE,
	    dwpcie_imx8mq_intr, sc, sc->sc_dev.dv_xname);

	/* Unmask INTx interrupts. */
	HWRITE4(sc, PCIE_GLOBAL_INT_MASK,
	    PCIE_GLOBAL_INT_MASK_INT_A | PCIE_GLOBAL_INT_MASK_INT_B |
	    PCIE_GLOBAL_INT_MASK_INT_C | PCIE_GLOBAL_INT_MASK_INT_D);

	error = 0;
err:
	if (clkreq_gpiolen > 0)
		free(clkreq_gpio, M_TEMP, clkreq_gpiolen);
	if (disable_gpiolen > 0)
		free(disable_gpio, M_TEMP, disable_gpiolen);
	if (reset_gpiolen > 0)
		free(reset_gpio, M_TEMP, reset_gpiolen);
	return error;
}

int
dwpcie_imx8mq_intr(void *arg)
{
	struct dwpcie_softc *sc = arg;
	uint32_t cause;

	/* Acknowledge interrupts. */
	cause = HREAD4(sc, PCIE_GLOBAL_INT_CAUSE);
	HWRITE4(sc, PCIE_GLOBAL_INT_CAUSE, cause);

	/* INTx interrupt, so not really ours. */
	return 0;
}

int
dwpcie_fu740_init(struct dwpcie_softc *sc)
{
	sc->sc_num_viewport = 8;

	return 0;
}

int
dwpcie_rk3568_link_up(struct dwpcie_softc *sc)
{
	uint32_t reg;

	reg = bus_space_read_4(sc->sc_iot, sc->sc_glue_ioh,
	    PCIE_CLIENT_LTSSM_STATUS);
	if ((reg & PCIE_CLIENT_SMLH_LINK_UP) &&
	    (reg & PCIE_CLIENT_RDLH_LINK_UP) &&
	    (reg & PCIE_CLIENT_LTSSM_MASK) == PCIE_CLIENT_LTSSM_UP)
		return 1;
	return 0;
}

int
dwpcie_rk3568_init(struct dwpcie_softc *sc)
{
	uint32_t *reset_gpio;
	ssize_t reset_gpiolen;
	int error, idx, node;
	int pin, timo;

	sc->sc_num_viewport = 8;

	if (bus_space_map(sc->sc_iot, sc->sc_glue_base,
	    sc->sc_glue_size, 0, &sc->sc_glue_ioh))
		return ENOMEM;

	reset_assert_all(sc->sc_node);
	/* Power must be enabled before initializing the PHY. */
	regulator_enable(OF_getpropint(sc->sc_node, "vpcie3v3-supply", 0));
	phy_enable(sc->sc_node, "pcie-phy");
	reset_deassert_all(sc->sc_node);

	clock_enable_all(sc->sc_node);

	if (dwpcie_rk3568_link_up(sc))
		return 0;

	reset_gpiolen = OF_getproplen(sc->sc_node, "reset-gpios");
	if (reset_gpiolen > 0) {
		reset_gpio = malloc(reset_gpiolen, M_TEMP, M_WAITOK);
		OF_getpropintarray(sc->sc_node, "reset-gpios", reset_gpio,
		    reset_gpiolen);
		gpio_controller_config_pin(reset_gpio, GPIO_CONFIG_OUTPUT);
		gpio_controller_set_pin(reset_gpio, 1);
	}

	bus_space_write_4(sc->sc_iot, sc->sc_glue_ioh,
	    PCIE_CLIENT_HOT_RESET_CTRL, PCIE_CLIENT_APP_LTSSM_ENABLE_ENHANCE);
	bus_space_write_4(sc->sc_iot, sc->sc_glue_ioh,
	    PCIE_CLIENT_GENERAL_CON, PCIE_CLIENT_DEV_TYPE_RC);

	/* Assert PERST#. */
	if (reset_gpiolen > 0)
		gpio_controller_set_pin(reset_gpio, 0);

	dwpcie_link_config(sc);

	/* Enable LTSSM. */
	bus_space_write_4(sc->sc_iot, sc->sc_glue_ioh, PCIE_CLIENT_GENERAL_CON,
	    PCIE_CLIENT_LINK_REQ_RST_GRT | PCIE_CLIENT_APP_LTSSM_ENABLE);

	/*
	 * PERST# must remain asserted for at least 100us after the
	 * reference clock becomes stable.  But also has to remain
	 * active at least 100ms after power up.  Since we may have
	 * just powered on the device, play it safe and use 100ms.
	 */
	delay(100000);

	/* Deassert PERST#. */
	if (reset_gpiolen > 0)
		gpio_controller_set_pin(reset_gpio, 1);

	/* Wait for the link to come up. */
	for (timo = 100; timo > 0; timo--) {
		if (dwpcie_rk3568_link_up(sc))
			break;
		delay(10000);
	}
	if (timo == 0) {
		error = ETIMEDOUT;
		goto err;
	}

	node = OF_getnodebyname(sc->sc_node, "legacy-interrupt-controller");
	idx = OF_getindex(sc->sc_node, "legacy", "interrupt-names");
	if (node && idx != -1) {
		sc->sc_ih = fdt_intr_establish_idx(sc->sc_node, idx,
		    IPL_BIO | IPL_MPSAFE, dwpcie_rk3568_intr, sc,
		    sc->sc_dev.dv_xname);
	}

	if (sc->sc_ih) {
		for (pin = 0; pin < nitems(sc->sc_intx); pin++)
			TAILQ_INIT(&sc->sc_intx[pin]);
		sc->sc_ic.ic_node = node;
		sc->sc_ic.ic_cookie = sc;
		sc->sc_ic.ic_establish = dwpcie_rk3568_intr_establish;
		sc->sc_ic.ic_disestablish = dwpcie_rk3568_intr_disestablish;
		sc->sc_ic.ic_barrier = dwpcie_rk3568_intr_barrier;
		fdt_intr_register(&sc->sc_ic);
	}

	error = 0;
err:
	if (reset_gpiolen > 0)
		free(reset_gpio, M_TEMP, reset_gpiolen);
	
	return error;
}

int
dwpcie_rk3568_intr(void *arg)
{
	struct dwpcie_softc *sc = arg;
	struct dwpcie_intx *di;
	uint32_t status;
	int pin, s;

	status = bus_space_read_4(sc->sc_iot, sc->sc_glue_ioh,
	    PCIE_CLIENT_INTR_STATUS_LEGACY);
	for (pin = 0; pin < nitems(sc->sc_intx); pin++) {
		if ((status & (1 << pin)) == 0)
			continue;

		TAILQ_FOREACH(di, &sc->sc_intx[pin], di_next) {
			if ((di->di_flags & IPL_MPSAFE) == 0)
				KERNEL_LOCK();
			s = splraise(di->di_ipl);
			if (di->di_func(di->di_arg))
				di->di_count.ec_count++;
			splx(s);
			if ((di->di_flags & IPL_MPSAFE) == 0)
				KERNEL_UNLOCK();
		}
	}

	return 1;
}

void *
dwpcie_rk3568_intr_establish(void *cookie, int *cell, int level,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct dwpcie_softc *sc = (struct dwpcie_softc *)cookie;
	struct dwpcie_intx *di;
	int pin = cell[0];
	uint32_t mask = (1U << pin);

	if (ci != NULL && !CPU_IS_PRIMARY(ci))
		return NULL;

	if (pin < 0 || pin >= nitems(sc->sc_intx))
		return NULL;

	/* Mask the interrupt. */
	bus_space_write_4(sc->sc_iot, sc->sc_glue_ioh,
	    PCIE_CLIENT_INTR_MASK_LEGACY, (mask << 16) | mask);
	intr_barrier(sc->sc_ih);

	di = malloc(sizeof(*di), M_DEVBUF, M_WAITOK | M_ZERO);
	di->di_func = func;
	di->di_arg = arg;
	di->di_ipl = level & IPL_IRQMASK;
	di->di_flags = level & IPL_FLAGMASK;
	di->di_pin = pin;
	di->di_name = name;
	if (name != NULL)
		evcount_attach(&di->di_count, name, &di->di_pin);
	di->di_sc = sc;
	TAILQ_INSERT_TAIL(&sc->sc_intx[pin], di, di_next);

	/* Unmask the interrupt. */
	bus_space_write_4(sc->sc_iot, sc->sc_glue_ioh,
	    PCIE_CLIENT_INTR_MASK_LEGACY, mask << 16);

	return di;
}

void
dwpcie_rk3568_intr_disestablish(void *cookie)
{
	struct dwpcie_intx *di = cookie;
	struct dwpcie_softc *sc = di->di_sc;
	uint32_t mask = (1U << di->di_pin);

	/* Mask the interrupt. */
	bus_space_write_4(sc->sc_iot, sc->sc_glue_ioh,
	    PCIE_CLIENT_INTR_MASK_LEGACY, (mask << 16) | mask);
	intr_barrier(sc->sc_ih);

	if (di->di_name)
		evcount_detach(&di->di_count);

	TAILQ_REMOVE(&sc->sc_intx[di->di_pin], di, di_next);

	if (!TAILQ_EMPTY(&sc->sc_intx[di->di_pin])) {
		/* Unmask the interrupt. */
		bus_space_write_4(sc->sc_iot, sc->sc_glue_ioh,
		    PCIE_CLIENT_INTR_MASK_LEGACY, mask << 16);
	}

	free(di, M_DEVBUF, sizeof(*di));
}

void
dwpcie_rk3568_intr_barrier(void *cookie)
{
	struct dwpcie_intx *di = cookie;
	struct dwpcie_softc *sc = di->di_sc;

	intr_barrier(sc->sc_ih);
}

int
dwpcie_sc8280xp_init(struct dwpcie_softc *sc)
{
	sc->sc_num_viewport = 8;

	if (OF_getproplen(sc->sc_node, "msi-map") <= 0)
		return dwpcie_msi_init(sc);

	return 0;
}

void
dwpcie_atu_write(struct dwpcie_softc *sc, int index, off_t reg,
    uint32_t val)
{
	if (sc->sc_atu_unroll) {
		bus_space_write_4(sc->sc_iot, sc->sc_atu_ioh,
		    IATU_OFFSET_UNROLL(index) + reg, val);
		return;
	}

	if (sc->sc_atu_viewport != index) {
		HWRITE4(sc, IATU_VIEWPORT, index);
		sc->sc_atu_viewport = index;
	}

	HWRITE4(sc, IATU_OFFSET_VIEWPORT + reg, val);
}

uint32_t
dwpcie_atu_read(struct dwpcie_softc *sc, int index, off_t reg)
{
	if (sc->sc_atu_unroll) {
		return bus_space_read_4(sc->sc_iot, sc->sc_atu_ioh,
		    IATU_OFFSET_UNROLL(index) + reg);
	}

	if (sc->sc_atu_viewport != index) {
		HWRITE4(sc, IATU_VIEWPORT, index);
		sc->sc_atu_viewport = index;
	}

	return HREAD4(sc, IATU_OFFSET_VIEWPORT + reg);
}

void
dwpcie_atu_disable(struct dwpcie_softc *sc, int index)
{
	dwpcie_atu_write(sc, index, IATU_REGION_CTRL_2, 0);
}

void
dwpcie_atu_config(struct dwpcie_softc *sc, int index, int type,
    uint64_t cpu_addr, uint64_t pci_addr, uint64_t size)
{
	uint32_t reg;
	int timo;

	dwpcie_atu_write(sc, index, IATU_LWR_BASE_ADDR, cpu_addr);
	dwpcie_atu_write(sc, index, IATU_UPPER_BASE_ADDR, cpu_addr >> 32);
	dwpcie_atu_write(sc, index, IATU_LIMIT_ADDR, cpu_addr + size - 1);
	dwpcie_atu_write(sc, index, IATU_LWR_TARGET_ADDR, pci_addr);
	dwpcie_atu_write(sc, index, IATU_UPPER_TARGET_ADDR, pci_addr >> 32);
	dwpcie_atu_write(sc, index, IATU_REGION_CTRL_1, type);
	dwpcie_atu_write(sc, index, IATU_REGION_CTRL_2,
	    IATU_REGION_CTRL_2_REGION_EN);

	for (timo = 5; timo > 0; timo--) {
		reg = dwpcie_atu_read(sc, index, IATU_REGION_CTRL_2);
		if (reg & IATU_REGION_CTRL_2_REGION_EN)
			break;
		delay(9000);
	}
	if (timo == 0)
		printf("%s:%d: timeout\n", __func__, __LINE__);
}

int
dwpcie_link_up(struct dwpcie_softc *sc)
{
	uint32_t reg;

	reg = HREAD4(sc, PCIE_PHY_DEBUG_R1);
	if ((reg & PCIE_PHY_DEBUG_R1_XMLH_LINK_UP) != 0 &&
	    (reg & PCIE_PHY_DEBUG_R1_XMLH_LINK_IN_TRAINING) == 0)
		return 1;
	return 0;
}

void
dwpcie_attach_hook(struct device *parent, struct device *self,
    struct pcibus_attach_args *pba)
{
}

int
dwpcie_bus_maxdevs(void *v, int bus)
{
	struct dwpcie_softc *sc = v;

	if (bus == sc->sc_bus || bus == sc->sc_bus + 1)
		return 1;
	return 32;
}

int
dwpcie_find_node(int node, int bus, int device, int function)
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

		node = dwpcie_find_node(child, bus, device, function);
		if (node)
			return node;
	}

	return 0;
}

pcitag_t
dwpcie_make_tag(void *v, int bus, int device, int function)
{
	struct dwpcie_softc *sc = v;
	int node;

	node = dwpcie_find_node(sc->sc_node, bus, device, function);
	return (((pcitag_t)node << 32) |
	    (bus << 24) | (device << 19) | (function << 16));
}

void
dwpcie_decompose_tag(void *v, pcitag_t tag, int *bp, int *dp, int *fp)
{
	if (bp != NULL)
		*bp = (tag >> 24) & 0xff;
	if (dp != NULL)
		*dp = (tag >> 19) & 0x1f;
	if (fp != NULL)
		*fp = (tag >> 16) & 0x7;
}

int
dwpcie_conf_size(void *v, pcitag_t tag)
{
	return PCIE_CONFIG_SPACE_SIZE;
}

pcireg_t
dwpcie_conf_read(void *v, pcitag_t tag, int reg)
{
	struct dwpcie_softc *sc = v;
	int bus, dev, fn;
	uint32_t ret;

	dwpcie_decompose_tag(sc, tag, &bus, &dev, &fn);
	if (bus == sc->sc_bus) {
		KASSERT(dev == 0);
		tag = dwpcie_make_tag(sc, 0, dev, fn);
		return HREAD4(sc, PCITAG_OFFSET(tag) | reg);
	}

	if (bus == sc->sc_bus + 1) {
		dwpcie_atu_config(sc, IATU_VIEWPORT_INDEX1,
		    IATU_REGION_CTRL_1_TYPE_CFG0,
		    sc->sc_conf_base, PCITAG_OFFSET(tag),
		    sc->sc_conf_size);
	} else {
		dwpcie_atu_config(sc, IATU_VIEWPORT_INDEX1,
		    IATU_REGION_CTRL_1_TYPE_CFG1,
		    sc->sc_conf_base, PCITAG_OFFSET(tag),
		    sc->sc_conf_size);
	}

	ret = bus_space_read_4(sc->sc_iot, sc->sc_conf_ioh, reg);

	if (sc->sc_num_viewport <= 2 && sc->sc_io_size > 0) {
		dwpcie_atu_config(sc, IATU_VIEWPORT_INDEX1,
		    IATU_REGION_CTRL_1_TYPE_IO, sc->sc_io_base,
		    sc->sc_io_bus_addr, sc->sc_io_size);
	}

	return ret;
}

void
dwpcie_conf_write(void *v, pcitag_t tag, int reg, pcireg_t data)
{
	struct dwpcie_softc *sc = v;
	int bus, dev, fn;

	dwpcie_decompose_tag(sc, tag, &bus, &dev, &fn);
	if (bus == sc->sc_bus) {
		KASSERT(dev == 0);
		tag = dwpcie_make_tag(sc, 0, dev, fn);
		HWRITE4(sc, PCITAG_OFFSET(tag) | reg, data);
		return;
	}

	if (bus == sc->sc_bus + 1) {
		dwpcie_atu_config(sc, IATU_VIEWPORT_INDEX1,
		    IATU_REGION_CTRL_1_TYPE_CFG0,
		    sc->sc_conf_base, PCITAG_OFFSET(tag),
		    sc->sc_conf_size);
	} else {
		dwpcie_atu_config(sc, IATU_VIEWPORT_INDEX1,
		    IATU_REGION_CTRL_1_TYPE_CFG1,
		    sc->sc_conf_base, PCITAG_OFFSET(tag),
		    sc->sc_conf_size);
	}

	bus_space_write_4(sc->sc_iot, sc->sc_conf_ioh, reg, data);

	if (sc->sc_num_viewport <= 2 && sc->sc_io_size > 0) {
		dwpcie_atu_config(sc, IATU_VIEWPORT_INDEX1,
		    IATU_REGION_CTRL_1_TYPE_IO, sc->sc_io_base,
		    sc->sc_io_bus_addr, sc->sc_io_size);
	}
}

int
dwpcie_probe_device_hook(void *v, struct pci_attach_args *pa)
{
	struct dwpcie_softc *sc = v;
	uint16_t rid;
	int i;

	rid = pci_requester_id(pa->pa_pc, pa->pa_tag);
	pa->pa_dmat = iommu_device_map_pci(sc->sc_node, rid, pa->pa_dmat);

	for (i = 0; i < sc->sc_nranges; i++) {
		iommu_reserve_region_pci(sc->sc_node, rid,
		    sc->sc_ranges[i].pci_base, sc->sc_ranges[i].size);
	}

	return 0;
}

int
dwpcie_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
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
dwpcie_intr_string(void *v, pci_intr_handle_t ih)
{
	switch (ih.ih_type) {
	case PCI_MSI:
		return "msi";
	case PCI_MSIX:
		return "msix";
	}

	return "intx";
}

struct dwpcie_msi *
dwpcie_msi_establish(struct dwpcie_softc *sc, pci_intr_handle_t *ihp,
    int level, int (*func)(void *), void *arg, char *name)
{
	pci_chipset_tag_t pc = ihp->ih_pc;
	pcitag_t tag = ihp->ih_tag;
	struct dwpcie_msi *dm;
	uint64_t msi_mask;
	int vec = ihp->ih_intrpin;
	int base, mme, nvec, off;
	pcireg_t reg;

	if (ihp->ih_type == PCI_MSI) {
		if (pci_get_capability(pc, tag, PCI_CAP_MSI, &off, &reg) == 0)
			panic("%s: no msi capability", __func__);

		reg = pci_conf_read(ihp->ih_pc, ihp->ih_tag, off);
		mme = ((reg & PCI_MSI_MC_MME_MASK) >> PCI_MSI_MC_MME_SHIFT);
		if (vec >= (1 << mme))
			return NULL;
		if (reg & PCI_MSI_MC_C64)
			base = pci_conf_read(pc, tag, off + PCI_MSI_MD64);
		else
			base = pci_conf_read(pc, tag, off + PCI_MSI_MD32);
	} else {
		mme = 0;
		base = 0;
	}

	if (vec == 0) {
		/*
		 * Pre-allocate all the requested vectors.  Remember
		 * the number of requested vectors such that we can
		 * deallocate them in one go.
		 */
		msi_mask = (1ULL << (1 << mme)) - 1;
		while (vec <= sc->sc_num_msi - (1 << mme)) {
			if ((sc->sc_msi_mask & (msi_mask << vec)) == 0) {
				sc->sc_msi_mask |= (msi_mask << vec);
				break;
			}
			vec += (1 << mme);
		}
		base = vec;
		nvec = (1 << mme);
	} else {
		KASSERT(ihp->ih_type == PCI_MSI);
		vec += base;
		nvec = 0;
	}

	if (vec >= sc->sc_num_msi)
		return NULL;

	if (ihp->ih_type == PCI_MSI) {
		if (reg & PCI_MSI_MC_C64)
			pci_conf_write(pc, tag, off + PCI_MSI_MD64, base);
		else
			pci_conf_write(pc, tag, off + PCI_MSI_MD32, base);
	}

	dm = &sc->sc_msi[vec];
	KASSERT(dm->dm_func == NULL);

	dm->dm_func = func;
	dm->dm_arg = arg;
	dm->dm_ipl = level & IPL_IRQMASK;
	dm->dm_flags = level & IPL_FLAGMASK;
	dm->dm_vec = vec;
	dm->dm_nvec = nvec;
	dm->dm_name = name;
	if (name != NULL)
		evcount_attach(&dm->dm_count, name, &dm->dm_vec);

	/* Unmask the MSI. */
	HCLR4(sc, PCIE_MSI_INTR_MASK(vec / 32), (1U << (vec % 32)));

	return dm;
}

void
dwpcie_msi_disestablish(struct dwpcie_softc *sc, struct dwpcie_msi *dm)
{
	uint64_t msi_mask = (1ULL << dm->dm_nvec) - 1;

	/* Mask the MSI. */
	HSET4(sc, PCIE_MSI_INTR_MASK(dm->dm_vec / 32),
	    (1U << (dm->dm_vec % 32)));

	if (dm->dm_name)
		evcount_detach(&dm->dm_count);
	dm->dm_func = NULL;

	/*
	 * Unallocate all allocated vetcors if this is the first
	 * vector for the device.
	 */
	sc->sc_msi_mask &= ~(msi_mask << dm->dm_vec);
}

void *
dwpcie_intr_establish(void *v, pci_intr_handle_t ih, int level,
    struct cpu_info *ci, int (*func)(void *), void *arg, char *name)
{
	struct dwpcie_softc *sc = v;
	struct dwpcie_intr_handle *pih;
	void *cookie = NULL;

	KASSERT(ih.ih_type != PCI_NONE);

	if (ih.ih_type != PCI_INTX) {
		struct dwpcie_msi *dm = NULL;
		bus_dma_tag_t dmat = ih.ih_dmat;
		bus_dma_segment_t seg;
		bus_dmamap_t map;
		uint64_t addr, data;

		if (sc->sc_msi_addr) {
			dm = dwpcie_msi_establish(sc, &ih, level, func, arg, name);
			if (dm == NULL)
				return NULL;
			addr = sc->sc_msi_addr;
			data = dm->dm_vec;
		} else {
			/*
			 * Assume hardware passes Requester ID as
			 * sideband data.
			 */
			addr = ih.ih_intrpin;
			data = pci_requester_id(ih.ih_pc, ih.ih_tag);
			cookie = fdt_intr_establish_msi_cpu(sc->sc_node, &addr,
			    &data, level, ci, func, arg, (void *)name);
			if (cookie == NULL)
				return NULL;
		}

		pih = malloc(sizeof(*pih), M_DEVBUF, M_WAITOK | M_ZERO);
		pih->pih_ih.ih_ic = &dwpcie_ic;
		pih->pih_ih.ih_ih = cookie;
		pih->pih_sc = sc;
		pih->pih_dm = dm;

		if (sc->sc_msi_addr == 0) {
			if (bus_dmamap_create(dmat, sizeof(uint32_t), 1,
			    sizeof(uint32_t), 0, BUS_DMA_WAITOK, &map)) {
				free(pih, M_DEVBUF, sizeof(*pih));
				fdt_intr_disestablish(cookie);
				return NULL;
			}

			memset(&seg, 0, sizeof(seg));
			seg.ds_addr = addr;
			seg.ds_len = sizeof(uint32_t);

			if (bus_dmamap_load_raw(dmat, map, &seg, 1,
			    sizeof(uint32_t), BUS_DMA_WAITOK)) {
				bus_dmamap_destroy(dmat, map);
				free(pih, M_DEVBUF, sizeof(*pih));
				fdt_intr_disestablish(cookie);
				return NULL;
			}

			addr = map->dm_segs[0].ds_addr;
			pih->pih_dmat = dmat;
			pih->pih_map = map;
		}

		if (ih.ih_type == PCI_MSIX) {
			pci_msix_enable(ih.ih_pc, ih.ih_tag,
			    &sc->sc_bus_memt, ih.ih_intrpin, addr, data);
		} else
			pci_msi_enable(ih.ih_pc, ih.ih_tag, addr, data);
	} else {
		int bus, dev, fn;
		uint32_t reg[4];

		dwpcie_decompose_tag(sc, ih.ih_tag, &bus, &dev, &fn);

		reg[0] = bus << 16 | dev << 11 | fn << 8;
		reg[1] = reg[2] = 0;
		reg[3] = ih.ih_intrpin;

		cookie = fdt_intr_establish_imap_cpu(sc->sc_node, reg,
		    sizeof(reg), level, ci, func, arg, name);
		if (cookie == NULL)
			return NULL;

		pih = malloc(sizeof(*pih), M_DEVBUF, M_WAITOK | M_ZERO);
		pih->pih_ih.ih_ic = &dwpcie_ic;
		pih->pih_ih.ih_ih = cookie;
	}

	return pih;
}

void
dwpcie_intr_disestablish(void *v, void *cookie)
{
	struct dwpcie_intr_handle *pih = cookie;

	if (pih->pih_dm)
		dwpcie_msi_disestablish(pih->pih_sc, pih->pih_dm);
	else
		fdt_intr_disestablish(pih->pih_ih.ih_ih);

	if (pih->pih_dmat) {
		bus_dmamap_unload(pih->pih_dmat, pih->pih_map);
		bus_dmamap_destroy(pih->pih_dmat, pih->pih_map);
	}

	free(pih, M_DEVBUF, sizeof(*pih));
}

int
dwpcie_bs_iomap(bus_space_tag_t t, bus_addr_t addr, bus_size_t size,
    int flags, bus_space_handle_t *bshp)
{
	struct dwpcie_softc *sc = t->bus_private;
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
dwpcie_bs_memmap(bus_space_tag_t t, bus_addr_t addr, bus_size_t size,
    int flags, bus_space_handle_t *bshp)
{
	struct dwpcie_softc *sc = t->bus_private;
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
