/*	$OpenBSD: octpcie.c,v 1.2 2019/09/22 04:43:24 visa Exp $	*/

/*
 * Copyright (c) 2019 Visa Hankala
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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

/*
 * Driver for OCTEON II and OCTEON III PCIe controller.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/extent.h>

#include <machine/autoconf.h>
#include <machine/octeonreg.h>
#include <machine/octeonvar.h>
#include <machine/octeon_model.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/ppbreg.h>

#include <octeon/dev/iobusvar.h>

#define PEM_BASE(port)		(0x11800c0000000ULL + (port) * PEM_SIZE)
#define PEM_SIZE		0x01000000

#define PEM_BAR0_SIZE		(1ULL << 14)
#define PEM_BAR2_SIZE		(1ULL << 41)

#define PEM_CTL_STATUS		0x00000000
#define   PEM_CTL_STATUS_LNK_ENB	0x0000000000000010ULL
#define PEM_CFG_WR		0x00000028
#define PEM_CFG_RD		0x00000030
#define PEM_P2P_BAR_START(i)	(0x00000040 + (i) * 16)
#define PEM_P2P_BAR_END(i)	(0x00000048 + (i) * 16)
#define PEM_P2N_BAR_START(i)	(0x00000080 + (i) * 8)
#define PEM_BAR_CTL(cfg)	((cfg)->cfg_bar_ctl_reg)
#define   PEM_BAR_CTL_BAR1_SIZ_M	0x0000000000000070ULL
#define   PEM_BAR_CTL_BAR1_SIZ_256M	0x0000000000000030ULL
#define   PEM_BAR_CTL_BAR2_ENB		0x0000000000000008ULL
#define   PEM_BAR_CTL_BAR2_ESX_M	0x0000000000000006ULL
#define   PEM_BAR_CTL_BAR2_ESX_S	1
#define   PEM_BAR_CTL_BAR2_CAX		0x0000000000000001ULL
#define PEM_BAR1_INDEX(cfg, i)	((cfg)->cfg_bar1_index_reg + (i) * 8)
#define PEM_STRAP		0x00000408
#define   PEM_STRAP_PIMODE_M		0x0000000000000003ULL
#define   PEM_STRAP_PIMODE_RC		0x0000000000000003ULL

#define PCIERC_CFG001		0x00000004
#define   PCIERC_CFG001_I_DIS		0x00000400U
#define   PCIERC_CFG001_SEE		0x00000100U
#define   PCIERC_CFG001_ME		0x00000004U
#define   PCIERC_CFG001_MSAE		0x00000002U
#define PCIERC_CFG006		0x00000018
#define PCIERC_CFG008		0x00000020
#define   PCIERC_CFG008_MLADDR_M	0xfff00000U
#define   PCIERC_CFG008_MLADDR_S	20
#define   PCIERC_CFG008_MBADDR_M	0x0000fff0U
#define   PCIERC_CFG008_MBADDR_S	4
#define PCIERC_CFG009		0x00000024
#define   PCIERC_CFG009_LMEM_LIMIT_M	0xfff00000U
#define   PCIERC_CFG009_LMEM_LIMIT_S	20
#define   PCIERC_CFG009_LMEM_BASE_M	0x0000fff0U
#define   PCIERC_CFG009_LMEM_BASE_S	4
#define PCIERC_CFG010		0x00000028
#define   PCIERC_CFG010_UMEM_BASE_M	0xffffffffU
#define   PCIERC_CFG010_UMEM_BASE_S	0
#define PCIERC_CFG011		0x0000002c
#define   PCIERC_CFG011_UMEM_LIMIT_M	0xffffffffU
#define   PCIERC_CFG011_UMEM_LIMIT_S	0
#define PCIERC_CFG030		0x00000078
#define   PCIERC_CFG030_MRRS_M		0x00007000U
#define   PCIERC_CFG030_MRRS_S		12
#define   PCIERC_CFG030_NS_EN		0x00000800U
#define   PCIERC_CFG030_AP_EN		0x00000400U
#define   PCIERC_CFG030_PF_EN		0x00000200U
#define   PCIERC_CFG030_ETF_EN		0x00000100U
#define   PCIERC_CFG030_MPS_M		0x000000e0U
#define   PCIERC_CFG030_MPS_S		5
#define   PCIERC_CFG030_RO_EN		0x00000010U
#define   PCIERC_CFG030_UR_EN		0x00000008U
#define   PCIERC_CFG030_FE_EN		0x00000004U
#define   PCIERC_CFG030_NFE_EN		0x00000002U
#define   PCIERC_CFG030_CE_EN		0x00000001U
#define PCIERC_CFG032		0x00000080
#define   PCIERC_CFG032_DLLA		0x20000000U
#define   PCIERC_CFG032_LT		0x08000000U
#define   PCIERC_CFG032_ASLPC_M		0x00000003U
#define   PCIERC_CFG032_ASLPC_S		0
#define PCIERC_CFG034		0x00000088
#define   PCIERC_CFG034_DLLS_EN		0x00001000U
#define   PCIERC_CFG034_CCINT_EN	0x00000010U
#define PCIERC_CFG035		0x0000008c
#define   PCIERC_CFG035_PMEIE		0x00000008U
#define   PCIERC_CFG035_SEFEE		0x00000004U
#define   PCIERC_CFG035_SENFEE		0x00000002U
#define   PCIERC_CFG035_SECEE		0x00000001U
#define PCIERC_CFG066		0x00000108
#define PCIERC_CFG069		0x00000114
#define PCIERC_CFG070		0x00000118
#define   PCIERC_CFG070_CE		0x00000100U
#define   PCIERC_CFG070_GE		0x00000040U
#define PCIERC_CFG075		0x0000012c
#define   PCIERC_CFG075_FERE		0x00000004U
#define   PCIERC_CFG075_NFERE		0x00000002U
#define   PCIERC_CFG075_CERE		0x00000001U
#define PCIERC_CFG515		0x0000080c
#define   PCIERC_CFG515_DSC		0x00000200U

#define DPI_BASE		0x1df0000000040ULL
#define DPI_SIZE		0x00001000
#define DPI_SLI_PRT_CFG(port)	(0x00000900 + (port) * 8)
#define   DPI_SLI_PRT_CFG_MOLR_M	0x0000000000003f00ULL
#define   DPI_SLI_PRT_CFG_MOLR_S	8
#define   DPI_SLI_PRT_CFG_MPS_M		0x0000000000000010ULL
#define   DPI_SLI_PRT_CFG_MPS_S		4
#define   DPI_SLI_PRT_CFG_MRRS_M	0x0000000000000003ULL
#define   DPI_SLI_PRT_CFG_MRRS_S	0

#define SLI_BASE		0x11f0000000000ULL
#define SLI_SIZE		0x00020000
#define SLI_S2M_PORT_CTL(port)	(0x00013d80 + (port) * 16)
#define   SLI_S2M_PORT_CTL_MRRS_M	0x0000000000000007ULL
#define   SLI_S2M_PORT_CTL_MRRS_S	0
#define SLI_MEM_ACCESS_CTL		0x000102f0U
#define   SLI_MEM_ACCESS_CTL_MAXW_M	0x0000000000003c00ULL
#define   SLI_MEM_ACCESS_CTL_MAXW_S	10
#define   SLI_MEM_ACCESS_CTL_TIMER_M	0x00000000000003ffULL
#define   SLI_MEM_ACCESS_CTL_TIMER_S	0
#define SLI_MEM_ACCESS_SUBID(i)	(0x000000e0 + (i) * 16)
#define   SLI_MEM_ACCESS_SUBID_PORT_M	0x0000038000000000ULL
#define   SLI_MEM_ACCESS_SUBID_PORT_S	39
#define   SLI_MEM_ACCESS_SUBID_NMERGE	0x0000004000000000ULL
#define   SLI_MEM_ACCESS_SUBID_ESR_M	0x0000003000000000ULL
#define   SLI_MEM_ACCESS_SUBID_ESR_S	36
#define   SLI_MEM_ACCESS_SUBID_ESW_M	0x0000000c00000000ULL
#define   SLI_MEM_ACCESS_SUBID_ESW_S	34
#define   SLI_MEM_ACCESS_SUBID_WTYPE_M	0x0000000300000000ULL
#define   SLI_MEM_ACCESS_SUBID_WTYPE_S	32
#define   SLI_MEM_ACCESS_SUBID_RTYPE_M	0x00000000c0000000ULL
#define   SLI_MEM_ACCESS_SUBID_RTYPE_S	30
#define   SLI_MEM_ACCESS_SUBID_BA_M	0x000000003fffffffULL
#define   SLI_MEM_ACCESS_SUBID_BA_S	0

#define SLI_PCIECFG_BASE(port)	(0x1190c00000000ULL + (port) * SLI_PCIECFG_SIZE)
#define SLI_PCIECFG_SIZE	(1ULL << 32)
#define SLI_PCIEIO_BASE(port)	(0x11a0400000000ULL + (port) * SLI_PCIEIO_SIZE)
#define SLI_PCIEIO_SIZE		(1ULL << 32)
#define SLI_PCIEMEM_BASE(port)	(0x11b0000000000ULL + (port) * SLI_PCIEMEM_SIZE)
#define SLI_PCIEMEM_SIZE	(1ULL << 40)

#define CIU_SOFT_PRST_ADDR	0x1070000000748ULL
#define CIU_SOFT_PRST1_ADDR	0x1070000000758ULL
#define RST_SOFT_PRST_ADDR(port) (0x11800060016c0ULL + (port) * 8)
#define   PRST_SOFT_PRST		0x0000000000000001ULL

#define CIU3_PEM_INTSN_INTA(i)	(((0xc0 + i) << 12) + 60)

struct octpcie_softc;

struct octpcie_config {
	int			 cfg_nports;
	uint32_t		 cfg_bar_ctl_reg;
	uint32_t		 cfg_bar1_index_reg;
	char			 cfg_has_ciu3;
};

struct octpcie_port {
	struct mips_pci_chipset	 port_pc;
	struct octpcie_softc	*port_sc;
	bus_space_tag_t		 port_iot;
	bus_space_handle_t	 port_pem_ioh;
	bus_space_handle_t	 port_pciecfg_ioh;
	struct extent		*port_ioex;
	struct extent		*port_memex;
	char			 port_ioex_name[32];
	char			 port_memex_name[32];
	int			 port_index;
	struct mips_bus_space	 port_bus_iot;
	struct mips_bus_space	 port_bus_memt;
};

struct octpcie_softc {
	struct device		 sc_dev;
	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_dpi_ioh;
	bus_space_handle_t	 sc_sli_ioh;
	struct machine_bus_dma_tag *sc_dmat;
	const struct octpcie_config *sc_cfg;
	struct octpcie_port	*sc_ports;
};

int	 octpcie_match(struct device *, void *, void *);
void	 octpcie_attach(struct device *, struct device *, void *);
int	 octpcie_print(void *, const char *);

void	 octpcie_attach_hook(struct device *, struct device *,
	    struct pcibus_attach_args *pba);
int	 octpcie_bus_maxdevs(void *, int);
pcitag_t octpcie_make_tag(void *, int, int, int);
void	 octpcie_decompose_tag(void *, pcitag_t, int *, int *, int *);
int	 octpcie_conf_size(void *, pcitag_t);
pcireg_t octpcie_conf_read(void *, pcitag_t, int);
void	 octpcie_conf_write(void *, pcitag_t, int, pcireg_t);
int	 octpcie_pci_intr_map(struct pci_attach_args *, pci_intr_handle_t *);
const char *
	 octpcie_pci_intr_string(void *, pci_intr_handle_t);
void	*octpcie_pci_intr_establish(void *, pci_intr_handle_t, int,
	    int (*)(void *), void *, char *);
void	 octpcie_pci_intr_disestablish(void *, void *);

int	 octpcie_io_map(bus_space_tag_t, bus_addr_t, bus_size_t, int,
	    bus_space_handle_t *);
int	 octpcie_mem_map(bus_space_tag_t, bus_addr_t, bus_size_t, int,
	    bus_space_handle_t *);

void	 octpcie_port_attach(struct octpcie_port *);
int	 octpcie_port_is_host(struct octpcie_port *);
int	 octpcie_port_reset(struct octpcie_port *);

uint32_t octpcie_cfgreg_read(struct octpcie_port *, uint32_t);
void	 octpcie_cfgreg_write(struct octpcie_port *, uint32_t, uint32_t);

const struct cfattach octpcie_ca = {
	sizeof(struct octpcie_softc), octpcie_match, octpcie_attach
};

struct cfdriver octpcie_cd = {
	NULL, "octpcie", DV_DULL
};

const struct octpcie_config cn61xx_config = {
	.cfg_nports		= 2,
	.cfg_bar_ctl_reg	= 0x00000128,
	.cfg_bar1_index_reg	= 0x000000a8,
};
const struct octpcie_config cn71xx_config = {
	.cfg_nports		= 3,
	.cfg_bar_ctl_reg	= 0x000000a8,
	.cfg_bar1_index_reg	= 0x00000100,
};
const struct octpcie_config cn78xx_config = {
	.cfg_nports		= 4,
	.cfg_bar_ctl_reg	= 0x000000a8,
	.cfg_bar1_index_reg	= 0x00000100,
	.cfg_has_ciu3		= 1,
};

int
octpcie_match(struct device *parent, void *match, void *aux)
{
	struct iobus_attach_args *aa = aux;

	if (strcmp(aa->aa_name, octpcie_cd.cd_name) != 0)
		return 0;

	return 1;
}

void
octpcie_attach(struct device *parent, struct device *self, void *aux)
{
	struct iobus_attach_args *aa = aux;
	const struct octpcie_config *cfg;
	struct octpcie_port *port;
	struct octpcie_softc *sc = (struct octpcie_softc *)self;
	uint64_t val;
	uint32_t chipid;
	int pi;

	sc->sc_iot = aa->aa_bust;
	sc->sc_dmat = aa->aa_dmat;

	chipid = octeon_get_chipid();
	switch (octeon_model_family(chipid)) {
	case OCTEON_MODEL_FAMILY_CN61XX:
	case OCTEON_MODEL_FAMILY_CN63XX:
		cfg = &cn61xx_config;
		break;
	case OCTEON_MODEL_FAMILY_CN71XX:
		cfg = &cn71xx_config;
		break;
	case OCTEON_MODEL_FAMILY_CN73XX:
	case OCTEON_MODEL_FAMILY_CN78XX:
		cfg = &cn78xx_config;
		break;
	default:
		printf(": unhandled chipid 0x%x\n", chipid);
		return;
	}
	sc->sc_cfg = cfg;

	if (bus_space_map(sc->sc_iot, DPI_BASE, DPI_SIZE, 0,
	    &sc->sc_dpi_ioh) != 0) {
		printf(": can't map DPI registers\n");
		return;
	}

	if (bus_space_map(sc->sc_iot, SLI_BASE, SLI_SIZE, 0,
	    &sc->sc_sli_ioh) != 0) {
		printf(": can't map SLI registers\n");
		goto error;
	}

	val = bus_space_read_8(sc->sc_iot, sc->sc_sli_ioh, SLI_MEM_ACCESS_CTL);
	val &= ~SLI_MEM_ACCESS_CTL_MAXW_M;
	val &= ~SLI_MEM_ACCESS_CTL_TIMER_M;
	val |= 127 << SLI_MEM_ACCESS_CTL_TIMER_S;
	bus_space_write_8(sc->sc_iot, sc->sc_sli_ioh, SLI_MEM_ACCESS_CTL, val);

	printf(": %d ports\n", cfg->cfg_nports);

	sc->sc_ports = mallocarray(cfg->cfg_nports, sizeof(*sc->sc_ports),
	    M_DEVBUF, M_WAITOK | M_ZERO);
	for (pi = 0; pi < cfg->cfg_nports; pi++) {
		port = &sc->sc_ports[pi];
		port->port_sc = sc;
		port->port_index = pi;
		port->port_iot = sc->sc_iot;
		octpcie_port_attach(port);
	}
	return;

error:
	bus_space_unmap(sc->sc_iot, sc->sc_dpi_ioh, DPI_SIZE);
}

void
octpcie_port_attach(struct octpcie_port *port)
{
	struct pcibus_attach_args pba;
	struct octpcie_softc *sc = port->port_sc;
	pci_chipset_tag_t pc = &port->port_pc;

	if (bus_space_map(port->port_iot, PEM_BASE(port->port_index),
	    PEM_SIZE, 0, &port->port_pem_ioh) != 0) {
		printf("%s port %d: can't map PEM registers\n",
		    sc->sc_dev.dv_xname, port->port_index);
		return;
	}

	if (bus_space_map(port->port_iot, SLI_PCIECFG_BASE(port->port_index),
	    SLI_PCIECFG_SIZE, 0, &port->port_pciecfg_ioh) != 0) {
		printf("%s port %d: can't map PCIECFG registers\n",
		    sc->sc_dev.dv_xname, port->port_index);
		goto error_pem;
	}

	if (octpcie_port_is_host(port) == 0)
		goto error_pciecfg;

	if (octpcie_port_reset(port) != 0) {
		/* Error has been printed already. */
		goto error_pciecfg;
	}

	snprintf(port->port_ioex_name, sizeof(port->port_ioex_name),
	    "%s port %d pciio", sc->sc_dev.dv_xname, port->port_index);
	snprintf(port->port_memex_name, sizeof(port->port_memex_name),
	    "%s port %d pcimem", sc->sc_dev.dv_xname, port->port_index);
	port->port_ioex = extent_create(port->port_ioex_name, 0, 0xffffffff,
	    M_DEVBUF, NULL, 0, EX_WAITOK | EX_FILLED);
	port->port_memex = extent_create(port->port_memex_name, 0, (u_long)-1,
	    M_DEVBUF, NULL, 0, EX_WAITOK | EX_FILLED);

	extent_free(port->port_ioex, 0, 0xffffffff, EX_WAITOK);
	extent_free(port->port_memex, PEM_BAR0_SIZE, PEM_BAR2_SIZE - 1,
	    EX_WAITOK);

	port->port_bus_iot = *port->port_iot;
	port->port_bus_iot.bus_private = port;
	port->port_bus_iot._space_map = octpcie_io_map;

	port->port_bus_memt = *port->port_iot;
	port->port_bus_memt.bus_private = port;
	port->port_bus_memt._space_map = octpcie_mem_map;

	pc->pc_conf_v = port;
	pc->pc_attach_hook = octpcie_attach_hook;
	pc->pc_bus_maxdevs = octpcie_bus_maxdevs;
	pc->pc_make_tag = octpcie_make_tag;
	pc->pc_decompose_tag = octpcie_decompose_tag;
	pc->pc_conf_size = octpcie_conf_size;
	pc->pc_conf_read = octpcie_conf_read;
	pc->pc_conf_write = octpcie_conf_write;

	pc->pc_intr_v = port;
	pc->pc_intr_map = octpcie_pci_intr_map;
	pc->pc_intr_string = octpcie_pci_intr_string;
	pc->pc_intr_establish = octpcie_pci_intr_establish;
	pc->pc_intr_disestablish = octpcie_pci_intr_disestablish;

	memset(&pba, 0, sizeof(pba));
	pba.pba_busname = "pci";
	pba.pba_iot = &port->port_bus_iot;
	pba.pba_memt = &port->port_bus_memt;
	pba.pba_dmat = sc->sc_dmat;
	pba.pba_pc = pc;
	pba.pba_domain = pci_ndomains++;
	pba.pba_bus = 0;
	pba.pba_ioex = port->port_ioex;
	pba.pba_memex = port->port_memex;
	config_found(&sc->sc_dev, &pba, octpcie_print);
	return;

error_pciecfg:
	bus_space_unmap(port->port_iot, port->port_pciecfg_ioh,
	    SLI_PCIECFG_SIZE);
error_pem:
	bus_space_unmap(port->port_iot, port->port_pem_ioh, PEM_SIZE);
}

int
octpcie_port_reset(struct octpcie_port *port)
{
	struct octpcie_softc *sc = port->port_sc;
	const struct octpcie_config *cfg = sc->sc_cfg;
	paddr_t ctl_reg, rst_reg;
	uint64_t val;
	uint32_t chipid, cr;
	int i, timeout;

	chipid = octeon_get_chipid();
	switch (octeon_model_family(chipid)) {
	case OCTEON_MODEL_FAMILY_CN61XX:
	case OCTEON_MODEL_FAMILY_CN63XX:
	case OCTEON_MODEL_FAMILY_CN66XX:
	case OCTEON_MODEL_FAMILY_CN68XX:
		ctl_reg = MIO_RST_CTL(port->port_index);
		if (port->port_index == 0)
			rst_reg = CIU_SOFT_PRST_ADDR;
		else
			rst_reg = CIU_SOFT_PRST1_ADDR;
		break;
	case OCTEON_MODEL_FAMILY_CN71XX:
	case OCTEON_MODEL_FAMILY_CN73XX:
	case OCTEON_MODEL_FAMILY_CN78XX:
		ctl_reg = RST_CTL(port->port_index);
		rst_reg = RST_SOFT_PRST_ADDR(port->port_index);
		break;
	default:
		printf("%s port %d: unhandled chipid 0x%x\n",
		    sc->sc_dev.dv_xname, port->port_index, chipid);
		return -1;
	}

	/* Put the hardware in reset. */
	val = octeon_xkphys_read_8(rst_reg);
	octeon_xkphys_write_8(rst_reg, val);
	(void)octeon_xkphys_read_8(rst_reg);
	delay(2000);

	/* Take the hardware out of reset. */
	val &= ~PRST_SOFT_PRST;
	octeon_xkphys_write_8(rst_reg, val);
	(void)octeon_xkphys_read_8(rst_reg);
	delay(1000);

	/* Wait until the reset has completed. */
	for (timeout = 100000; timeout > 0; timeout--) {
		val = octeon_xkphys_read_8(ctl_reg);
		if (val & RST_CTL_RST_DONE)
			break;
	}
	if (timeout == 0) {
		printf("%s port %d: reset timeout\n",
		    sc->sc_dev.dv_xname, port->port_index);
		return -1;
	}

	/*
	 * Initialize the configuration registers of the root complex.
	 */

	cr = octpcie_cfgreg_read(port, PCIERC_CFG030);
	cr &= ~PCIERC_CFG030_MPS_M;
	cr &= ~PCIERC_CFG030_MRRS_M;
	cr |= 0 << PCIERC_CFG030_MPS_S;
	cr |= 3 << PCIERC_CFG030_MRRS_S;
	cr |= PCIERC_CFG030_NS_EN;
	cr |= PCIERC_CFG030_RO_EN;
	cr |= PCIERC_CFG030_UR_EN;
	cr |= PCIERC_CFG030_FE_EN;
	cr |= PCIERC_CFG030_NFE_EN;
	cr |= PCIERC_CFG030_CE_EN;
	octpcie_cfgreg_write(port, PCIERC_CFG030, cr);

	val = bus_space_read_8(sc->sc_iot, sc->sc_dpi_ioh,
	    DPI_SLI_PRT_CFG(port->port_index));
	val &= ~DPI_SLI_PRT_CFG_MPS_M;
	val &= ~DPI_SLI_PRT_CFG_MRRS_M;
	val |= 0 << DPI_SLI_PRT_CFG_MPS_S;
	val |= 3 << DPI_SLI_PRT_CFG_MRRS_S;
	val &= ~DPI_SLI_PRT_CFG_MOLR_M;
	val |= 32 << DPI_SLI_PRT_CFG_MOLR_S;
	bus_space_write_8(sc->sc_iot, sc->sc_dpi_ioh,
	    DPI_SLI_PRT_CFG(port->port_index), val);
	(void)bus_space_read_8(sc->sc_iot, sc->sc_dpi_ioh,
	    DPI_SLI_PRT_CFG(port->port_index));

	val = bus_space_read_8(sc->sc_iot, sc->sc_sli_ioh,
	    SLI_S2M_PORT_CTL(port->port_index));
	val &= ~SLI_S2M_PORT_CTL_MRRS_M;
	val |= 32 << SLI_S2M_PORT_CTL_MRRS_S;
	bus_space_write_8(sc->sc_iot, sc->sc_sli_ioh,
	    SLI_S2M_PORT_CTL(port->port_index), val);
	(void)bus_space_read_8(sc->sc_iot, sc->sc_sli_ioh,
	    SLI_S2M_PORT_CTL(port->port_index));

	cr = octpcie_cfgreg_read(port, PCIERC_CFG070);
	cr |= PCIERC_CFG070_CE;
	cr |= PCIERC_CFG070_GE;
	octpcie_cfgreg_write(port, PCIERC_CFG070, cr);

	cr = octpcie_cfgreg_read(port, PCIERC_CFG001);
	cr |= PCIERC_CFG001_I_DIS;
	cr |= PCIERC_CFG001_SEE;
	cr |= PCIERC_CFG001_ME;
	cr |= PCIERC_CFG001_MSAE;
	octpcie_cfgreg_write(port, PCIERC_CFG001, cr);

	octpcie_cfgreg_write(port, PCIERC_CFG066, 0);
	octpcie_cfgreg_write(port, PCIERC_CFG069, 0);

	cr = octpcie_cfgreg_read(port, PCIERC_CFG032);
	cr &= ~PCIERC_CFG032_ASLPC_M;
	octpcie_cfgreg_write(port, PCIERC_CFG032, cr);

	octpcie_cfgreg_write(port, PCIERC_CFG006, 0);

	cr = 0x100 << PCIERC_CFG008_MBADDR_S;
	cr |= 0 << PCIERC_CFG008_MLADDR_S;
	octpcie_cfgreg_write(port, PCIERC_CFG008, cr);

	cr = octpcie_cfgreg_read(port, PCIERC_CFG009);
	cr &= ~PCIERC_CFG009_LMEM_BASE_M;
	cr |= 0x100 << PCIERC_CFG009_LMEM_BASE_S;
	cr &= ~PCIERC_CFG009_LMEM_LIMIT_M;
	octpcie_cfgreg_write(port, PCIERC_CFG009, cr);

	cr = octpcie_cfgreg_read(port, PCIERC_CFG010);
	cr &= ~PCIERC_CFG010_UMEM_BASE_M;
	cr |= 0x100 << PCIERC_CFG010_UMEM_BASE_S;
	octpcie_cfgreg_write(port, PCIERC_CFG010, cr);

	cr = octpcie_cfgreg_read(port, PCIERC_CFG011);
	cr &= ~PCIERC_CFG011_UMEM_LIMIT_M;
	octpcie_cfgreg_write(port, PCIERC_CFG011, cr);

	cr = octpcie_cfgreg_read(port, PCIERC_CFG035);
	cr |= PCIERC_CFG035_SECEE;
	cr |= PCIERC_CFG035_SEFEE;
	cr |= PCIERC_CFG035_SENFEE;
	cr |= PCIERC_CFG035_PMEIE;
	octpcie_cfgreg_write(port, PCIERC_CFG035, cr);

	cr = octpcie_cfgreg_read(port, PCIERC_CFG075);
	cr |= PCIERC_CFG075_CERE;
	cr |= PCIERC_CFG075_NFERE;
	cr |= PCIERC_CFG075_FERE;
	octpcie_cfgreg_write(port, PCIERC_CFG075, cr);

	cr = octpcie_cfgreg_read(port, PCIERC_CFG034);
	cr |= PCIERC_CFG034_DLLS_EN;
	cr |= PCIERC_CFG034_CCINT_EN;
	octpcie_cfgreg_write(port, PCIERC_CFG034, cr);

	cr = octpcie_cfgreg_read(port, PCIERC_CFG515);
	cr |= PCIERC_CFG515_DSC;
	octpcie_cfgreg_write(port, PCIERC_CFG515, cr);

	/* Enable the link. */
	val = bus_space_read_8(port->port_iot, port->port_pem_ioh,
	    PEM_CTL_STATUS);
	val |= PEM_CTL_STATUS_LNK_ENB;
	bus_space_write_8(port->port_iot, port->port_pem_ioh,
	    PEM_CTL_STATUS, val);

	/*
	 * Wait until link training finishes and
	 * data link layer activity begins.
	 */
	for (timeout = 1000; timeout > 0; timeout--) {
		cr = octpcie_cfgreg_read(port, PCIERC_CFG032);
		if ((cr & PCIERC_CFG032_DLLA) != 0 &&
		    (cr & PCIERC_CFG032_LT) == 0)
			break;
		delay(1000);
	}
	if (timeout == 0) {
		printf("%s port %d: link timeout\n",
		    sc->sc_dev.dv_xname, port->port_index);
		return -1;
	}

	val = (uint64_t)port->port_index << SLI_MEM_ACCESS_SUBID_PORT_S;
	val |= 3ULL << SLI_MEM_ACCESS_SUBID_ESR_S;
	val |= 3ULL << SLI_MEM_ACCESS_SUBID_ESW_S;
	for (i = 0; i < 4; i++) {
		bus_space_write_8(sc->sc_iot, sc->sc_sli_ioh,
		    SLI_MEM_ACCESS_SUBID(i + port->port_index * 4), val);
		val += 1ULL << SLI_MEM_ACCESS_SUBID_BA_S;
	}

	/* Disable forwarding between ports. */
	for (i = 0; i < 4; i++) {
		bus_space_write_8(port->port_iot, port->port_pem_ioh,
		    PEM_P2P_BAR_START(i), ~0ULL);
		bus_space_write_8(port->port_iot, port->port_pem_ioh,
		    PEM_P2P_BAR_END(i), ~0ULL);
	}

	/*
	 * Set up forwarding of requests from PCI memory space.
	 *
	 * BAR0 (size 2^14) forwards to internal CSRs.
	 * BAR1 (size configurable) and BAR2 (size 2^41) forward to DRAM.
	 *
	 * This code relies on BAR2 to forward DRAM requests.
	 * Forwarding through BAR1 is disabled.
	 */

	bus_space_write_8(port->port_iot, port->port_pem_ioh,
	    PEM_P2N_BAR_START(0), 0);
	bus_space_write_8(port->port_iot, port->port_pem_ioh,
	    PEM_P2N_BAR_START(1), PEM_BAR2_SIZE);
	bus_space_write_8(port->port_iot, port->port_pem_ioh,
	    PEM_P2N_BAR_START(2), 0);

	/* Set BAR1 size to 256 MiB, enable BAR2. */
	val = bus_space_read_8(port->port_iot, port->port_pem_ioh,
	    PEM_BAR_CTL(cfg));
	val &= ~PEM_BAR_CTL_BAR1_SIZ_M;
	val |= PEM_BAR_CTL_BAR1_SIZ_256M;
	val |= PEM_BAR_CTL_BAR2_ENB;
	val &= ~PEM_BAR_CTL_BAR2_ESX_M;
	val |= 1UL << PEM_BAR_CTL_BAR2_ESX_S;
	val |= PEM_BAR_CTL_BAR2_CAX;
	bus_space_write_8(port->port_iot, port->port_pem_ioh,
	    PEM_BAR_CTL(cfg), val);

	/* Disable BAR1 mappings. */
	for (i = 0; i < 16; i++) {
		bus_space_write_8(port->port_iot, port->port_pem_ioh,
		    PEM_BAR1_INDEX(cfg, i), 0);
	}

	return 0;
}

int
octpcie_port_is_host(struct octpcie_port *port)
{
	uint32_t chipid;
	int host = 0;

	chipid = octeon_get_chipid();
	switch (octeon_model_family(chipid)) {
	case OCTEON_MODEL_FAMILY_CN61XX:
	case OCTEON_MODEL_FAMILY_CN63XX:
	case OCTEON_MODEL_FAMILY_CN66XX:
	case OCTEON_MODEL_FAMILY_CN68XX:
		if ((octeon_xkphys_read_8(MIO_RST_CTL(port->port_index)) &
		    MIO_RST_CTL_PRTMODE) != 0)
			host = 1;
		break;
	case OCTEON_MODEL_FAMILY_CN71XX:
		if ((octeon_xkphys_read_8(RST_CTL(port->port_index)) &
		    RST_CTL_HOST_MODE) != 0)
			host = 1;
		break;
	case OCTEON_MODEL_FAMILY_CN73XX:
	case OCTEON_MODEL_FAMILY_CN78XX:
		if ((bus_space_read_8(port->port_iot, port->port_pem_ioh,
		    PEM_STRAP) & PEM_STRAP_PIMODE_M) == PEM_STRAP_PIMODE_RC)
			host = 1;
	default:
		break;
	}
	return host;
}

int
octpcie_print(void *aux, const char *pnp)
{
	struct pcibus_attach_args *pba = aux;

	if (pnp)
		printf("%s at %s", pba->pba_busname, pnp);
	printf(" bus %d", pba->pba_bus);

	return UNCONF;
}

void
octpcie_attach_hook(struct device *parent, struct device *self,
    struct pcibus_attach_args *pba)
{
}

int
octpcie_bus_maxdevs(void *v, int busno)
{
	return 1;
}

pcitag_t
octpcie_make_tag(void *unused, int b, int d, int f)
{
	return (b << 16) | (d << 11) | (f << 8);
}

void
octpcie_decompose_tag(void *unused, pcitag_t tag, int *bp, int *dp, int *fp)
{
	if (bp != NULL)
		*bp = (tag >> 16) & 0xff;
	if (dp != NULL)
		*dp = (tag >> 11) & 0x1f;
	if (fp != NULL)
		*fp = (tag >> 8) & 0x7;
}

int
octpcie_conf_size(void *v, pcitag_t tag)
{
	return PCIE_CONFIG_SPACE_SIZE;
}

pcireg_t
octpcie_conf_read(void *v, pcitag_t tag, int offset)
{
	struct octpcie_port * port = v;
	int bus;

	octpcie_decompose_tag(NULL, tag, &bus, NULL, NULL);
	if (bus == 0) {
		return bus_space_read_4(port->port_iot, port->port_pciecfg_ioh,
		    tag | (offset & 0xfffc));
	}
	return 0xffffffff;
}

void
octpcie_conf_write(void *v, pcitag_t tag, int offset, pcireg_t data)
{
	struct octpcie_port * port = v;
	int bus;

	octpcie_decompose_tag(NULL, tag, &bus, NULL, NULL);
	if (bus == 0) {
		bus_space_write_4(port->port_iot, port->port_pciecfg_ioh,
		    tag | (offset & 0xfffc), data);
	}
}

int
octpcie_pci_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	struct octpcie_port *port = pa->pa_pc->pc_conf_v;
	struct octpcie_softc *sc = port->port_sc;
	int dev, pin;

	if (pa->pa_intrpin == 0 || pa->pa_intrpin > PCI_INTERRUPT_PIN_MAX)
		return -1;

	if (pa->pa_bridgetag != NULL) {
		pci_decompose_tag(pa->pa_pc, pa->pa_tag, NULL, &dev, NULL);
		pin = PPB_INTERRUPT_SWIZZLE(pa->pa_rawintrpin, dev);
		if (pa->pa_bridgeih[pin - 1] == (pci_intr_handle_t)-1)
			return -1;
		*ihp = pa->pa_bridgeih[pin - 1];
		return 0;
	}

	if (sc->sc_cfg->cfg_has_ciu3)
		*ihp = pa->pa_intrpin - 1 +
		    CIU3_PEM_INTSN_INTA(port->port_index);
	else
		*ihp = pa->pa_intrpin - 1 + CIU_INT_PCI_INTA;
	return 0;
}

const char *
octpcie_pci_intr_string(void *cookie, pci_intr_handle_t ih)
{
	static char irqstr[16];

	snprintf(irqstr, sizeof(irqstr), "irq %lu", ih);
	return irqstr;
}

void *
octpcie_pci_intr_establish(void *cookie, pci_intr_handle_t ih, int level,
    int (*cb)(void *), void *cbarg, char *name)
{
	return octeon_intr_establish(ih, level, cb, cbarg, name);
}

void
octpcie_pci_intr_disestablish(void *cookie, void *ihp)
{
	octeon_intr_disestablish(ihp);
}

int
octpcie_io_map(bus_space_tag_t t, bus_addr_t offs, bus_size_t size, int flags,
    bus_space_handle_t *bshp)
{
	struct octpcie_port *port = t->bus_private;

	if (offs + size > SLI_PCIEIO_SIZE)
		return EINVAL;

	return bus_space_map(port->port_iot,
	    SLI_PCIEIO_BASE(port->port_index) + offs, size, flags, bshp);
}

int
octpcie_mem_map(bus_space_tag_t t, bus_addr_t offs, bus_size_t size, int flags,
    bus_space_handle_t *bshp)
{
	struct octpcie_port *port = t->bus_private;

	if (offs + size > SLI_PCIEMEM_SIZE)
		return EINVAL;

	return bus_space_map(port->port_iot,
	    SLI_PCIEMEM_BASE(port->port_index) + offs, size, flags, bshp);
}

uint32_t
octpcie_cfgreg_read(struct octpcie_port *port, uint32_t off)
{
	uint64_t val;

	bus_space_write_8(port->port_iot, port->port_pem_ioh, PEM_CFG_RD, off);
	val = bus_space_read_8(port->port_iot, port->port_pem_ioh, PEM_CFG_RD);

	return (uint32_t)(val >> 32);
}

void
octpcie_cfgreg_write(struct octpcie_port *port, uint32_t off, uint32_t val)
{
	bus_space_write_4(port->port_iot, port->port_pem_ioh, PEM_CFG_WR,
	    ((uint64_t)val << 32) | off);
	(void)bus_space_read_4(port->port_iot, port->port_pem_ioh, PEM_CFG_WR);
}
