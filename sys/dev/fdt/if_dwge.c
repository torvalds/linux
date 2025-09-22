/*	$OpenBSD: if_dwge.c,v 1.22 2024/02/08 20:50:34 kettenis Exp $	*/
/*
 * Copyright (c) 2008, 2019 Mark Kettenis <kettenis@openbsd.org>
 * Copyright (c) 2017 Patrick Wildt <patrick@blueri.se>
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

/*
 * Driver for the Synopsys Designware ethernet controller.
 */

#include "bpfilter.h"
#include "kstat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/timeout.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <net/if.h>
#include <net/if_media.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_gpio.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/ofw_regulator.h>
#include <dev/ofw/fdt.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#if NKSTAT > 0
#include <sys/kstat.h>
#endif

#include <netinet/in.h>
#include <netinet/if_ether.h>

/* Registers */

#define GMAC_MAC_CONF		0x0000
#define  GMAC_MAC_CONF_JD		(1 << 22)
#define  GMAC_MAC_CONF_BE		(1 << 21)
#define  GMAC_MAC_CONF_DCRS		(1 << 16)
#define  GMAC_MAC_CONF_PS		(1 << 15)
#define  GMAC_MAC_CONF_FES		(1 << 14)
#define  GMAC_MAC_CONF_LM		(1 << 12)
#define  GMAC_MAC_CONF_DM		(1 << 11)
#define  GMAC_MAC_CONF_TE		(1 << 3)
#define  GMAC_MAC_CONF_RE		(1 << 2)
#define GMAC_MAC_FRM_FILT	0x0004
#define  GMAC_MAC_FRM_FILT_PM		(1 << 4)
#define  GMAC_MAC_FRM_FILT_HMC		(1 << 2)
#define  GMAC_MAC_FRM_FILT_PR		(1 << 0)
#define GMAC_HASH_TAB_HI	0x0008
#define GMAC_HASH_TAB_LO	0x000c
#define GMAC_GMII_ADDR		0x0010
#define  GMAC_GMII_ADDR_PA_SHIFT	11
#define  GMAC_GMII_ADDR_GR_SHIFT	6
#define  GMAC_GMII_ADDR_CR_SHIFT	2
#define  GMAC_GMII_ADDR_CR_MASK		0xf
#define  GMAC_GMII_ADDR_CR_DIV_42	0
#define  GMAC_GMII_ADDR_CR_DIV_62	1
#define  GMAC_GMII_ADDR_CR_DIV_16	2
#define  GMAC_GMII_ADDR_CR_DIV_26	3
#define  GMAC_GMII_ADDR_CR_DIV_102	4
#define  GMAC_GMII_ADDR_CR_DIV_124	5
#define  GMAC_GMII_ADDR_GW		(1 << 1)
#define  GMAC_GMII_ADDR_GB		(1 << 0)
#define GMAC_GMII_DATA		0x0014
#define GMAC_VERSION		0x0020
#define  GMAC_VERSION_SNPS_MASK		0xff
#define GMAC_INT_MASK		0x003c
#define  GMAC_INT_MASK_LPIIM		(1 << 10)
#define  GMAC_INT_MASK_PIM		(1 << 3)
#define  GMAC_INT_MASK_RIM		(1 << 0)
#define GMAC_MAC_ADDR0_HI	0x0040
#define GMAC_MAC_ADDR0_LO	0x0044
#define GMAC_MAC_MMC_CTRL	0x0100
#define  GMAC_MAC_MMC_CTRL_ROR	(1 << 2)
#define  GMAC_MAC_MMC_CTRL_CR	(1 << 0)
#define GMAC_MMC_RX_INT_MSK	0x010c
#define GMAC_MMC_TX_INT_MSK	0x0110
#define GMAC_MMC_TXOCTETCNT_GB	0x0114
#define GMAC_MMC_TXFRMCNT_GB	0x0118
#define GMAC_MMC_TXUNDFLWERR	0x0148
#define GMAC_MMC_TXCARERR	0x0160
#define GMAC_MMC_TXOCTETCNT_G	0x0164
#define GMAC_MMC_TXFRMCNT_G	0x0168
#define GMAC_MMC_RXFRMCNT_GB	0x0180
#define GMAC_MMC_RXOCTETCNT_GB	0x0184
#define GMAC_MMC_RXOCTETCNT_G	0x0188
#define GMAC_MMC_RXMCFRMCNT_G	0x0190
#define GMAC_MMC_RXCRCERR	0x0194
#define GMAC_MMC_RXLENERR	0x01c8
#define GMAC_MMC_RXFIFOOVRFLW	0x01d4
#define GMAC_MMC_IPC_INT_MSK	0x0200
#define GMAC_BUS_MODE		0x1000
#define  GMAC_BUS_MODE_8XPBL		(1 << 24)
#define  GMAC_BUS_MODE_USP		(1 << 23)
#define  GMAC_BUS_MODE_RPBL_MASK	(0x3f << 17)
#define  GMAC_BUS_MODE_RPBL_SHIFT	17
#define  GMAC_BUS_MODE_FB		(1 << 16)
#define  GMAC_BUS_MODE_PBL_MASK		(0x3f << 8)
#define  GMAC_BUS_MODE_PBL_SHIFT	8
#define  GMAC_BUS_MODE_SWR		(1 << 0)
#define GMAC_TX_POLL_DEMAND	0x1004
#define GMAC_RX_DESC_LIST_ADDR	0x100c
#define GMAC_TX_DESC_LIST_ADDR	0x1010
#define GMAC_STATUS		0x1014
#define  GMAC_STATUS_MMC		(1 << 27)
#define  GMAC_STATUS_RI			(1 << 6)
#define  GMAC_STATUS_TU			(1 << 2)
#define  GMAC_STATUS_TI			(1 << 0)
#define GMAC_OP_MODE		0x1018
#define  GMAC_OP_MODE_RSF		(1 << 25)
#define  GMAC_OP_MODE_TSF		(1 << 21)
#define  GMAC_OP_MODE_FTF		(1 << 20)
#define  GMAC_OP_MODE_TTC_MASK		(0x7 << 14)
#define  GMAC_OP_MODE_TTC_64		(0x0 << 14)
#define  GMAC_OP_MODE_TTC_128		(0x1 << 14)
#define  GMAC_OP_MODE_ST		(1 << 13)
#define  GMAC_OP_MODE_RTC_MASK		(0x3 << 3)
#define  GMAC_OP_MODE_RTC_64		(0x0 << 3)
#define  GMAC_OP_MODE_RTC_128		(0x3 << 3)
#define  GMAC_OP_MODE_OSF		(1 << 2)
#define  GMAC_OP_MODE_SR		(1 << 1)
#define GMAC_INT_ENA		0x101c
#define  GMAC_INT_ENA_NIE		(1 << 16)
#define  GMAC_INT_ENA_RIE		(1 << 6)
#define  GMAC_INT_ENA_TUE		(1 << 2)
#define  GMAC_INT_ENA_TIE		(1 << 0)
#define GMAC_AXI_BUS_MODE	0x1028
#define  GMAC_AXI_BUS_MODE_WR_OSR_LMT_MASK	(0xf << 20)
#define  GMAC_AXI_BUS_MODE_WR_OSR_LMT_SHIFT	20
#define  GMAC_AXI_BUS_MODE_RD_OSR_LMT_MASK	(0xf << 16)
#define  GMAC_AXI_BUS_MODE_RD_OSR_LMT_SHIFT	16
#define  GMAC_AXI_BUS_MODE_BLEN_256		(1 << 7)
#define  GMAC_AXI_BUS_MODE_BLEN_128		(1 << 6)
#define  GMAC_AXI_BUS_MODE_BLEN_64		(1 << 5)
#define  GMAC_AXI_BUS_MODE_BLEN_32		(1 << 4)
#define  GMAC_AXI_BUS_MODE_BLEN_16		(1 << 3)
#define  GMAC_AXI_BUS_MODE_BLEN_8		(1 << 2)
#define  GMAC_AXI_BUS_MODE_BLEN_4		(1 << 1)
#define GMAC_HW_FEATURE		0x1058
#define  GMAC_HW_FEATURE_ENHDESSEL	(1 << 24)

/*
 * DWGE descriptors.
 */

struct dwge_desc {
	uint32_t sd_status;
	uint32_t sd_len;
	uint32_t sd_addr;
	uint32_t sd_next;
};

/* Tx status bits. */
#define TDES0_DB		(1 << 0)
#define TDES0_UF		(1 << 1)
#define TDES0_ED		(1 << 2)
#define TDES0_CC_MASK		(0xf << 3)
#define TDES0_CC_SHIFT		3
#define TDES0_EC		(1 << 8)
#define TDES0_LC		(1 << 9)
#define TDES0_NC		(1 << 10)
#define TDES0_PCE		(1 << 12)
#define TDES0_JT		(1 << 14)
#define TDES0_IHE		(1 << 16)
#define TDES0_OWN		(1U << 31)

#define ETDES0_TCH		(1 << 20)
#define ETDES0_FS		(1 << 28)
#define ETDES0_LS		(1 << 29)
#define ETDES0_IC		(1 << 30)

/* Rx status bits */
#define RDES0_PE		(1 << 0)
#define RDES0_CE		(1 << 1)
#define RDES0_RE		(1 << 3)
#define RDES0_RWT		(1 << 4)
#define RDES0_FT		(1 << 5)
#define RDES0_LC		(1 << 6)
#define RDES0_IPC		(1 << 7)
#define RDES0_LS		(1 << 8)
#define RDES0_FS		(1 << 9)
#define RDES0_OE		(1 << 11)
#define RDES0_SAF		(1 << 13)
#define RDES0_DE		(1 << 14)
#define RDES0_ES		(1 << 15)
#define RDES0_FL_MASK		0x3fff
#define RDES0_FL_SHIFT		16
#define RDES0_AFM		(1 << 30)
#define RDES0_OWN		(1U << 31)

/* Tx size bits */
#define TDES1_TBS1		(0xfff << 0)
#define TDES1_TCH		(1 << 24)
#define TDES1_DC		(1 << 26)
#define TDES1_CIC_MASK		(0x3 << 27)
#define TDES1_CIC_IP		(1 << 27)
#define TDES1_CIC_NO_PSE	(2 << 27)
#define TDES1_CIC_FULL		(3 << 27)
#define TDES1_FS		(1 << 29)
#define TDES1_LS		(1 << 30)
#define TDES1_IC		(1U << 31)

/* Rx size bits */
#define RDES1_RBS1		(0xfff << 0)
#define RDES1_RCH		(1 << 24)
#define RDES1_DIC		(1U << 31)

#define ERDES1_RCH		(1 << 14)

struct dwge_buf {
	bus_dmamap_t	tb_map;
	struct mbuf	*tb_m;
};

#define DWGE_NTXDESC	512
#define DWGE_NTXSEGS	16

#define DWGE_NRXDESC	512

struct dwge_dmamem {
	bus_dmamap_t		tdm_map;
	bus_dma_segment_t	tdm_seg;
	size_t			tdm_size;
	caddr_t			tdm_kva;
};
#define DWGE_DMA_MAP(_tdm)	((_tdm)->tdm_map)
#define DWGE_DMA_LEN(_tdm)	((_tdm)->tdm_size)
#define DWGE_DMA_DVA(_tdm)	((_tdm)->tdm_map->dm_segs[0].ds_addr)
#define DWGE_DMA_KVA(_tdm)	((void *)(_tdm)->tdm_kva)

struct dwge_softc {
	struct device		sc_dev;
	int			sc_node;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_dma_tag_t		sc_dmat;
	void			*sc_ih;

	struct if_device	sc_ifd;

	struct arpcom		sc_ac;
#define sc_lladdr	sc_ac.ac_enaddr
	struct mii_data		sc_mii;
#define sc_media	sc_mii.mii_media
	uint64_t		sc_fixed_media;
	int			sc_link;
	int			sc_phyloc;
	int			sc_force_thresh_dma_mode;
	int			sc_enh_desc;
	int			sc_defrag;

	struct dwge_dmamem	*sc_txring;
	struct dwge_buf		*sc_txbuf;
	struct dwge_desc	*sc_txdesc;
	int			sc_tx_prod;
	int			sc_tx_cons;

	struct dwge_dmamem	*sc_rxring;
	struct dwge_buf		*sc_rxbuf;
	struct dwge_desc	*sc_rxdesc;
	int			sc_rx_prod;
	struct if_rxring	sc_rx_ring;
	int			sc_rx_cons;

	struct timeout		sc_tick;
	struct timeout		sc_rxto;

	uint32_t		sc_clk;

	bus_size_t		sc_clk_sel;
	uint32_t		sc_clk_sel_125;
	uint32_t		sc_clk_sel_25;
	uint32_t		sc_clk_sel_2_5;

#if NKSTAT > 0
	struct mutex		sc_kstat_mtx;
	struct kstat		*sc_kstat;
#endif
};

#define DEVNAME(_s)	((_s)->sc_dev.dv_xname)

int	dwge_match(struct device *, void *, void *);
void	dwge_attach(struct device *, struct device *, void *);
void	dwge_setup_allwinner(struct dwge_softc *);
void	dwge_setup_rockchip(struct dwge_softc *);

const struct cfattach dwge_ca = {
	sizeof(struct dwge_softc), dwge_match, dwge_attach
};

struct cfdriver dwge_cd = {
	NULL, "dwge", DV_IFNET
};

void	dwge_reset_phy(struct dwge_softc *);

uint32_t dwge_read(struct dwge_softc *, bus_addr_t);
void	dwge_write(struct dwge_softc *, bus_addr_t, uint32_t);

int	dwge_ioctl(struct ifnet *, u_long, caddr_t);
void	dwge_start(struct ifqueue *);
void	dwge_watchdog(struct ifnet *);

int	dwge_media_change(struct ifnet *);
void	dwge_media_status(struct ifnet *, struct ifmediareq *);

int	dwge_mii_readreg(struct device *, int, int);
void	dwge_mii_writereg(struct device *, int, int, int);
void	dwge_mii_statchg(struct device *);

void	dwge_lladdr_read(struct dwge_softc *, uint8_t *);
void	dwge_lladdr_write(struct dwge_softc *);

void	dwge_tick(void *);
void	dwge_rxtick(void *);

int	dwge_intr(void *);
void	dwge_tx_proc(struct dwge_softc *);
void	dwge_rx_proc(struct dwge_softc *);

void	dwge_up(struct dwge_softc *);
void	dwge_down(struct dwge_softc *);
void	dwge_iff(struct dwge_softc *);
int	dwge_encap(struct dwge_softc *, struct mbuf *, int *, int *);

void	dwge_reset(struct dwge_softc *);
void	dwge_stop_dma(struct dwge_softc *);

struct dwge_dmamem *
	dwge_dmamem_alloc(struct dwge_softc *, bus_size_t, bus_size_t);
void	dwge_dmamem_free(struct dwge_softc *, struct dwge_dmamem *);
struct mbuf *dwge_alloc_mbuf(struct dwge_softc *, bus_dmamap_t);
void	dwge_fill_rx_ring(struct dwge_softc *);

#if NKSTAT > 0
int	dwge_kstat_read(struct kstat *);
void	dwge_kstat_attach(struct dwge_softc *);
#endif

int
dwge_match(struct device *parent, void *cfdata, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "allwinner,sun7i-a20-gmac") ||
	    OF_is_compatible(faa->fa_node, "amlogic,meson-axg-dwmac") ||
	    OF_is_compatible(faa->fa_node, "amlogic,meson-g12a-dwmac") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3288-gmac") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3308-mac") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3328-gmac") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3399-gmac") ||
	    OF_is_compatible(faa->fa_node, "snps,dwmac"));
}

void
dwge_attach(struct device *parent, struct device *self, void *aux)
{
	struct dwge_softc *sc = (void *)self;
	struct fdt_attach_args *faa = aux;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	uint32_t phy, phy_supply;
	uint32_t axi_config;
	uint32_t mode, pbl;
	uint32_t version;
	uint32_t feature;
	int node;

	sc->sc_node = faa->fa_node;
	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf("%s: cannot map registers\n", self->dv_xname);
		return;
	}
	sc->sc_dmat = faa->fa_dmat;

	/* Lookup PHY. */
	phy = OF_getpropint(faa->fa_node, "phy", 0);
	if (phy == 0)
		phy = OF_getpropint(faa->fa_node, "phy-handle", 0);
	node = OF_getnodebyphandle(phy);
	if (node)
		sc->sc_phyloc = OF_getpropint(node, "reg", MII_PHY_ANY);
	else
		sc->sc_phyloc = MII_PHY_ANY;

	pinctrl_byname(faa->fa_node, "default");

	/* Enable clocks. */
	clock_set_assigned(faa->fa_node);
	clock_enable(faa->fa_node, "stmmaceth");
	reset_deassert(faa->fa_node, "stmmaceth");
	if (OF_is_compatible(faa->fa_node, "rockchip,rk3288-gmac") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3308-mac") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3328-gmac") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3399-gmac")) {
		clock_enable(faa->fa_node, "mac_clk_rx");
		clock_enable(faa->fa_node, "mac_clk_tx");
		clock_enable(faa->fa_node, "aclk_mac");
		clock_enable(faa->fa_node, "pclk_mac");
	}
	delay(5000);

	version = dwge_read(sc, GMAC_VERSION);
	printf(": rev 0x%02x", version & GMAC_VERSION_SNPS_MASK);

	if ((version & GMAC_VERSION_SNPS_MASK) > 0x35) {
		feature = dwge_read(sc, GMAC_HW_FEATURE);
		if (feature & GMAC_HW_FEATURE_ENHDESSEL)
			sc->sc_enh_desc = 1;
	}

	/*
	 * The GMAC on the StarFive JH7100 (core version 3.70)
	 * sometimes transmits corrupted packets.  The exact
	 * conditions under which this happens are unclear, but
	 * defragmenting mbufs before transmitting them fixes the
	 * issue.
	 */
	/* XXX drop "starfive,jh7100-gmac" in the future */
	if (OF_is_compatible(faa->fa_node, "starfive,jh7100-gmac") ||
	    OF_is_compatible(faa->fa_node, "starfive,jh7100-dwmac"))
		sc->sc_defrag = 1;

	/* Power up PHY. */
	phy_supply = OF_getpropint(faa->fa_node, "phy-supply", 0);
	if (phy_supply)
		regulator_enable(phy_supply);

	/* Reset PHY */
	dwge_reset_phy(sc);

	node = OF_getnodebyname(faa->fa_node, "fixed-link");
	if (node) {
		ifp->if_baudrate = IF_Mbps(OF_getpropint(node, "speed", 0));

		switch (OF_getpropint(node, "speed", 0)) {
		case 1000:
			sc->sc_fixed_media = IFM_ETHER | IFM_1000_T;
			break;
		case 100:
			sc->sc_fixed_media = IFM_ETHER | IFM_100_TX;
			break;
		default:
			sc->sc_fixed_media = IFM_ETHER | IFM_AUTO;
			break;
		}
		
		if (OF_getpropbool(node, "full-duplex")) {
			ifp->if_link_state = LINK_STATE_FULL_DUPLEX;
			sc->sc_fixed_media |= IFM_FDX;
		} else {
			ifp->if_link_state = LINK_STATE_UP;
		}
	}

	sc->sc_clk = clock_get_frequency(faa->fa_node, "stmmaceth");
	if (sc->sc_clk > 250000000)
		sc->sc_clk = GMAC_GMII_ADDR_CR_DIV_124;
	else if (sc->sc_clk > 150000000)
		sc->sc_clk = GMAC_GMII_ADDR_CR_DIV_102;
	else if (sc->sc_clk > 100000000)
		sc->sc_clk = GMAC_GMII_ADDR_CR_DIV_62;
	else if (sc->sc_clk > 60000000)
		sc->sc_clk = GMAC_GMII_ADDR_CR_DIV_42;
	else if (sc->sc_clk > 35000000)
		sc->sc_clk = GMAC_GMII_ADDR_CR_DIV_26;
	else
		sc->sc_clk = GMAC_GMII_ADDR_CR_DIV_16;

	if (OF_getprop(faa->fa_node, "local-mac-address",
	    &sc->sc_lladdr, ETHER_ADDR_LEN) != ETHER_ADDR_LEN)
		dwge_lladdr_read(sc, sc->sc_lladdr);
	printf(", address %s\n", ether_sprintf(sc->sc_lladdr));

	timeout_set(&sc->sc_tick, dwge_tick, sc);
	timeout_set(&sc->sc_rxto, dwge_rxtick, sc);

	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_xflags = IFXF_MPSAFE;
	ifp->if_ioctl = dwge_ioctl;
	ifp->if_qstart = dwge_start;
	ifp->if_watchdog = dwge_watchdog;
	ifq_init_maxlen(&ifp->if_snd, DWGE_NTXDESC - 1);
	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

	sc->sc_mii.mii_ifp = ifp;
	sc->sc_mii.mii_readreg = dwge_mii_readreg;
	sc->sc_mii.mii_writereg = dwge_mii_writereg;
	sc->sc_mii.mii_statchg = dwge_mii_statchg;

	ifmedia_init(&sc->sc_media, 0, dwge_media_change, dwge_media_status);

	/* Do hardware specific initializations. */
	if (OF_is_compatible(faa->fa_node, "allwinner,sun7i-a20-gmac"))
		dwge_setup_allwinner(sc);
	if (OF_is_compatible(faa->fa_node, "rockchip,rk3288-gmac") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3308-mac") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3328-gmac") ||
	    OF_is_compatible(faa->fa_node, "rockchip,rk3399-gmac"))
		dwge_setup_rockchip(sc);

	if (OF_getpropbool(faa->fa_node, "snps,force_thresh_dma_mode"))
		sc->sc_force_thresh_dma_mode = 1;

	dwge_reset(sc);

	/* Configure MAC. */
	dwge_write(sc, GMAC_MAC_CONF, dwge_read(sc, GMAC_MAC_CONF) |
	    GMAC_MAC_CONF_JD | GMAC_MAC_CONF_BE | GMAC_MAC_CONF_DCRS);

	/* Configure DMA engine. */
	mode = dwge_read(sc, GMAC_BUS_MODE);
	mode |= GMAC_BUS_MODE_USP;
	if (!OF_getpropbool(faa->fa_node, "snps,no-pbl-x8"))
		mode |= GMAC_BUS_MODE_8XPBL;
	mode &= ~(GMAC_BUS_MODE_RPBL_MASK | GMAC_BUS_MODE_PBL_MASK);
	pbl = OF_getpropint(faa->fa_node, "snps,pbl", 8);
	mode |= pbl << GMAC_BUS_MODE_RPBL_SHIFT;
	mode |= pbl << GMAC_BUS_MODE_PBL_SHIFT;
	if (OF_getpropbool(faa->fa_node, "snps,fixed-burst"))
		mode |= GMAC_BUS_MODE_FB;
	dwge_write(sc, GMAC_BUS_MODE, mode);

	/* Configure AXI master. */
	axi_config = OF_getpropint(faa->fa_node, "snps,axi-config", 0);
	node = OF_getnodebyphandle(axi_config);
	if (node) {
		uint32_t blen[7] = { 0 };
		uint32_t osr_lmt;
		int i;

		mode = dwge_read(sc, GMAC_AXI_BUS_MODE);

		osr_lmt = OF_getpropint(node, "snps,wr_osr_lmt", 1);
		mode &= ~GMAC_AXI_BUS_MODE_WR_OSR_LMT_MASK;
		mode |= (osr_lmt << GMAC_AXI_BUS_MODE_WR_OSR_LMT_SHIFT);
		osr_lmt = OF_getpropint(node, "snps,rd_osr_lmt", 1);
		mode &= ~GMAC_AXI_BUS_MODE_RD_OSR_LMT_MASK;
		mode |= (osr_lmt << GMAC_AXI_BUS_MODE_RD_OSR_LMT_SHIFT);

		OF_getpropintarray(node, "snps,blen", blen, sizeof(blen));
		for (i = 0; i < nitems(blen); i++) {
			switch (blen[i]) {
			case 256:
				mode |= GMAC_AXI_BUS_MODE_BLEN_256;
				break;
			case 128:
				mode |= GMAC_AXI_BUS_MODE_BLEN_128;
				break;
			case 64:
				mode |= GMAC_AXI_BUS_MODE_BLEN_64;
				break;
			case 32:
				mode |= GMAC_AXI_BUS_MODE_BLEN_32;
				break;
			case 16:
				mode |= GMAC_AXI_BUS_MODE_BLEN_16;
				break;
			case 8:
				mode |= GMAC_AXI_BUS_MODE_BLEN_8;
				break;
			case 4:
				mode |= GMAC_AXI_BUS_MODE_BLEN_4;
				break;
			}
		}

		dwge_write(sc, GMAC_AXI_BUS_MODE, mode);
	}

	if (sc->sc_fixed_media == 0) {
		mii_attach(self, &sc->sc_mii, 0xffffffff, sc->sc_phyloc,
		    (sc->sc_phyloc == MII_PHY_ANY) ? 0 : MII_OFFSET_ANY, 0);
		if (LIST_FIRST(&sc->sc_mii.mii_phys) == NULL) {
			printf("%s: no PHY found!\n", sc->sc_dev.dv_xname);
			ifmedia_add(&sc->sc_media, IFM_ETHER|IFM_MANUAL, 0,
			    NULL);
			ifmedia_set(&sc->sc_media, IFM_ETHER|IFM_MANUAL);
		} else
			ifmedia_set(&sc->sc_media, IFM_ETHER|IFM_AUTO);
	} else {
		ifmedia_add(&sc->sc_media, sc->sc_fixed_media, 0, NULL);
		ifmedia_set(&sc->sc_media, sc->sc_fixed_media);

		/* force a configuration of the clocks/mac */
		sc->sc_mii.mii_statchg(self);
	}

	if_attach(ifp);
	ether_ifattach(ifp);
#if NKSTAT > 0
	dwge_kstat_attach(sc);
#endif

	/* Disable interrupts. */
	dwge_write(sc, GMAC_INT_ENA, 0);
	dwge_write(sc, GMAC_INT_MASK,
	    GMAC_INT_MASK_LPIIM | GMAC_INT_MASK_PIM | GMAC_INT_MASK_RIM);
	dwge_write(sc, GMAC_MMC_IPC_INT_MSK, 0xffffffff);

	sc->sc_ih = fdt_intr_establish(faa->fa_node, IPL_NET | IPL_MPSAFE,
	    dwge_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL)
		printf("%s: can't establish interrupt\n", sc->sc_dev.dv_xname);

	sc->sc_ifd.if_node = faa->fa_node;
	sc->sc_ifd.if_ifp = ifp;
	if_register(&sc->sc_ifd);
}

void
dwge_reset_phy(struct dwge_softc *sc)
{
	uint32_t *gpio;
	uint32_t delays[3];
	int active = 1;
	int len;

	len = OF_getproplen(sc->sc_node, "snps,reset-gpio");
	if (len <= 0)
		return;

	gpio = malloc(len, M_TEMP, M_WAITOK);

	/* Gather information. */
	OF_getpropintarray(sc->sc_node, "snps,reset-gpio", gpio, len);
	if (OF_getpropbool(sc->sc_node, "snps-reset-active-low"))
		active = 0;
	delays[0] = delays[1] = delays[2] = 0;
	OF_getpropintarray(sc->sc_node, "snps,reset-delays-us", delays,
	    sizeof(delays));

	/* Perform reset sequence. */
	gpio_controller_config_pin(gpio, GPIO_CONFIG_OUTPUT);
	gpio_controller_set_pin(gpio, !active);
	delay(delays[0]);
	gpio_controller_set_pin(gpio, active);
	delay(delays[1]);
	gpio_controller_set_pin(gpio, !active);
	delay(delays[2]);

	free(gpio, M_TEMP, len);
}

uint32_t
dwge_read(struct dwge_softc *sc, bus_addr_t addr)
{
	return bus_space_read_4(sc->sc_iot, sc->sc_ioh, addr);
}

void
dwge_write(struct dwge_softc *sc, bus_addr_t addr, uint32_t data)
{
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, addr, data);
}

void
dwge_lladdr_read(struct dwge_softc *sc, uint8_t *lladdr)
{
	uint32_t machi, maclo;

	machi = dwge_read(sc, GMAC_MAC_ADDR0_HI);
	maclo = dwge_read(sc, GMAC_MAC_ADDR0_LO);

	lladdr[0] = (maclo >> 0) & 0xff;
	lladdr[1] = (maclo >> 8) & 0xff;
	lladdr[2] = (maclo >> 16) & 0xff;
	lladdr[3] = (maclo >> 24) & 0xff;
	lladdr[4] = (machi >> 0) & 0xff;
	lladdr[5] = (machi >> 8) & 0xff;
}

void
dwge_lladdr_write(struct dwge_softc *sc)
{
	dwge_write(sc, GMAC_MAC_ADDR0_HI,
	    sc->sc_lladdr[5] << 8 | sc->sc_lladdr[4] << 0);
	dwge_write(sc, GMAC_MAC_ADDR0_LO,
	    sc->sc_lladdr[3] << 24 | sc->sc_lladdr[2] << 16 |
	    sc->sc_lladdr[1] << 8 | sc->sc_lladdr[0] << 0);
}

void
dwge_start(struct ifqueue *ifq)
{
	struct ifnet *ifp = ifq->ifq_if;
	struct dwge_softc *sc = ifp->if_softc;
	struct mbuf *m;
	int error, idx, left, used;

	if (!(ifp->if_flags & IFF_RUNNING))
		return;
	if (ifq_is_oactive(&ifp->if_snd))
		return;
	if (ifq_empty(&ifp->if_snd))
		return;
	if (!sc->sc_link)
		return;

	idx = sc->sc_tx_prod;
	left = sc->sc_tx_cons;
	if (left <= idx)
		left += DWGE_NTXDESC;
	left -= idx;
	used = 0;

	for (;;) {
		if (used + DWGE_NTXSEGS + 1 > left) {
			ifq_set_oactive(ifq);
			break;
		}

		m = ifq_dequeue(ifq);
		if (m == NULL)
			break;

		error = dwge_encap(sc, m, &idx, &used);
		if (error == EFBIG) {
			m_freem(m); /* give up: drop it */
			ifp->if_oerrors++;
			continue;
		}

#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif
	}

	if (sc->sc_tx_prod != idx) {
		sc->sc_tx_prod = idx;

		/* Set a timeout in case the chip goes out to lunch. */
		ifp->if_timer = 5;

		dwge_write(sc, GMAC_TX_POLL_DEMAND, 0xffffffff);
	}
}

int
dwge_ioctl(struct ifnet *ifp, u_long cmd, caddr_t addr)
{
	struct dwge_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)addr;
	int error = 0, s;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				dwge_up(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				dwge_down(sc);
		}
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		if (sc->sc_fixed_media != 0)
			error = ENOTTY;
		else
			error = ifmedia_ioctl(ifp, ifr, &sc->sc_media, cmd);
		break;

	case SIOCGIFRXR:
		error = if_rxr_ioctl((struct if_rxrinfo *)ifr->ifr_data,
		    NULL, MCLBYTES, &sc->sc_rx_ring);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_ac, cmd, addr);
		break;
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			dwge_iff(sc);
		error = 0;
	}

	splx(s);
	return (error);
}

void
dwge_watchdog(struct ifnet *ifp)
{
	printf("%s\n", __func__);
}

int
dwge_media_change(struct ifnet *ifp)
{
	struct dwge_softc *sc = ifp->if_softc;

	if (LIST_FIRST(&sc->sc_mii.mii_phys))
		mii_mediachg(&sc->sc_mii);

	return (0);
}

void
dwge_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct dwge_softc *sc = ifp->if_softc;

	if (LIST_FIRST(&sc->sc_mii.mii_phys)) {
		mii_pollstat(&sc->sc_mii);
		ifmr->ifm_active = sc->sc_mii.mii_media_active;
		ifmr->ifm_status = sc->sc_mii.mii_media_status;
	}
}

int
dwge_mii_readreg(struct device *self, int phy, int reg)
{
	struct dwge_softc *sc = (void *)self;
	int n;

	dwge_write(sc, GMAC_GMII_ADDR,
	    sc->sc_clk << GMAC_GMII_ADDR_CR_SHIFT |
	    phy << GMAC_GMII_ADDR_PA_SHIFT |
	    reg << GMAC_GMII_ADDR_GR_SHIFT |
	    GMAC_GMII_ADDR_GB);
	for (n = 0; n < 1000; n++) {
		if ((dwge_read(sc, GMAC_GMII_ADDR) & GMAC_GMII_ADDR_GB) == 0)
			return dwge_read(sc, GMAC_GMII_DATA);
		delay(10);
	}

	printf("%s: mii_read timeout\n", sc->sc_dev.dv_xname);
	return (0);
}

void
dwge_mii_writereg(struct device *self, int phy, int reg, int val)
{
	struct dwge_softc *sc = (void *)self;
	int n;

	dwge_write(sc, GMAC_GMII_DATA, val);
	dwge_write(sc, GMAC_GMII_ADDR,
	    sc->sc_clk << GMAC_GMII_ADDR_CR_SHIFT |
	    phy << GMAC_GMII_ADDR_PA_SHIFT |
	    reg << GMAC_GMII_ADDR_GR_SHIFT |
	    GMAC_GMII_ADDR_GW | GMAC_GMII_ADDR_GB);
	for (n = 0; n < 1000; n++) {
		if ((dwge_read(sc, GMAC_GMII_ADDR) & GMAC_GMII_ADDR_GB) == 0)
			return;
		delay(10);
	}

	printf("%s: mii_write timeout\n", sc->sc_dev.dv_xname);
}

void
dwge_mii_statchg(struct device *self)
{
	struct dwge_softc *sc = (void *)self;
	uint32_t conf;
	uint64_t media_active;

	conf = dwge_read(sc, GMAC_MAC_CONF);
	conf &= ~(GMAC_MAC_CONF_PS | GMAC_MAC_CONF_FES);

	media_active = sc->sc_fixed_media;
	if (media_active == 0)
		media_active = sc->sc_mii.mii_media_active;

	switch (IFM_SUBTYPE(media_active)) {
	case IFM_1000_SX:
	case IFM_1000_LX:
	case IFM_1000_CX:
	case IFM_1000_T:
		sc->sc_link = 1;
		break;
	case IFM_100_TX:
		conf |= GMAC_MAC_CONF_PS | GMAC_MAC_CONF_FES;
		sc->sc_link = 1;
		break;
	case IFM_10_T:
		conf |= GMAC_MAC_CONF_PS;
		sc->sc_link = 1;
		break;
	default:
		sc->sc_link = 0;
		return;
	}

	if (sc->sc_link == 0)
		return;

	conf &= ~GMAC_MAC_CONF_DM;
	if ((media_active & IFM_GMASK) == IFM_FDX)
		conf |= GMAC_MAC_CONF_DM;

	/* XXX: RX/TX flow control? */

	dwge_write(sc, GMAC_MAC_CONF, conf);
}

void
dwge_tick(void *arg)
{
	struct dwge_softc *sc = arg;
	int s;

	s = splnet();
	mii_tick(&sc->sc_mii);
	splx(s);

	timeout_add_sec(&sc->sc_tick, 1);
}

void
dwge_rxtick(void *arg)
{
	struct dwge_softc *sc = arg;
	uint32_t mode;
	int s;

	s = splnet();

	mode = dwge_read(sc, GMAC_OP_MODE);
	dwge_write(sc, GMAC_OP_MODE, mode & ~GMAC_OP_MODE_SR);

	bus_dmamap_sync(sc->sc_dmat, DWGE_DMA_MAP(sc->sc_rxring),
	    0, DWGE_DMA_LEN(sc->sc_rxring),
	    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);

	dwge_write(sc, GMAC_RX_DESC_LIST_ADDR, 0);

	sc->sc_rx_prod = sc->sc_rx_cons = 0;
	dwge_fill_rx_ring(sc);

	bus_dmamap_sync(sc->sc_dmat, DWGE_DMA_MAP(sc->sc_rxring),
	    0, DWGE_DMA_LEN(sc->sc_rxring),
	    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

	dwge_write(sc, GMAC_RX_DESC_LIST_ADDR, DWGE_DMA_DVA(sc->sc_rxring));
	dwge_write(sc, GMAC_OP_MODE, mode);

	splx(s);
}

int
dwge_intr(void *arg)
{
	struct dwge_softc *sc = arg;
	uint32_t reg;

	reg = dwge_read(sc, GMAC_STATUS);
	dwge_write(sc, GMAC_STATUS, reg);

	if (reg & GMAC_STATUS_RI)
		dwge_rx_proc(sc);

	if (reg & GMAC_STATUS_TI ||
	    reg & GMAC_STATUS_TU)
		dwge_tx_proc(sc);

#if NKSTAT > 0
	if (reg & GMAC_STATUS_MMC) {
		mtx_enter(&sc->sc_kstat_mtx);
		dwge_kstat_read(sc->sc_kstat);
		mtx_leave(&sc->sc_kstat_mtx);
	}
#endif

	return (1);
}

void
dwge_tx_proc(struct dwge_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct dwge_desc *txd;
	struct dwge_buf *txb;
	int idx, txfree;

	bus_dmamap_sync(sc->sc_dmat, DWGE_DMA_MAP(sc->sc_txring), 0,
	    DWGE_DMA_LEN(sc->sc_txring),
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	txfree = 0;
	while (sc->sc_tx_cons != sc->sc_tx_prod) {
		idx = sc->sc_tx_cons;
		KASSERT(idx < DWGE_NTXDESC);

		txd = &sc->sc_txdesc[idx];
		if (txd->sd_status & TDES0_OWN)
			break;

		txb = &sc->sc_txbuf[idx];
		if (txb->tb_m) {
			bus_dmamap_sync(sc->sc_dmat, txb->tb_map, 0,
			    txb->tb_map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, txb->tb_map);

			m_freem(txb->tb_m);
			txb->tb_m = NULL;
		}

		txfree++;

		if (sc->sc_tx_cons == (DWGE_NTXDESC - 1))
			sc->sc_tx_cons = 0;
		else
			sc->sc_tx_cons++;

		txd->sd_status = sc->sc_enh_desc ? ETDES0_TCH : 0;
	}

	if (sc->sc_tx_cons == sc->sc_tx_prod)
		ifp->if_timer = 0;

	if (txfree) {
		if (ifq_is_oactive(&ifp->if_snd))
			ifq_restart(&ifp->if_snd);
	}
}

void
dwge_rx_proc(struct dwge_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct dwge_desc *rxd;
	struct dwge_buf *rxb;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *m;
	int idx, len, cnt, put;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	bus_dmamap_sync(sc->sc_dmat, DWGE_DMA_MAP(sc->sc_rxring), 0,
	    DWGE_DMA_LEN(sc->sc_rxring),
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	cnt = if_rxr_inuse(&sc->sc_rx_ring);
	put = 0;
	while (put < cnt) {
		idx = sc->sc_rx_cons;
		KASSERT(idx < DWGE_NRXDESC);

		rxd = &sc->sc_rxdesc[idx];
		if (rxd->sd_status & RDES0_OWN)
			break;

		len = (rxd->sd_status >> RDES0_FL_SHIFT) & RDES0_FL_MASK;
		rxb = &sc->sc_rxbuf[idx];
		KASSERT(rxb->tb_m);

		bus_dmamap_sync(sc->sc_dmat, rxb->tb_map, 0,
		    len, BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmat, rxb->tb_map);

		m = rxb->tb_m;
		rxb->tb_m = NULL;
		if (rxd->sd_status & RDES0_ES) {
			ifp->if_ierrors++;
			m_freem(m);
		} else {
			/* Strip off CRC. */
			len -= ETHER_CRC_LEN;
			KASSERT(len > 0);

			m->m_pkthdr.len = m->m_len = len;

			ml_enqueue(&ml, m);
		}

		put++;
		if (sc->sc_rx_cons == (DWGE_NRXDESC - 1))
			sc->sc_rx_cons = 0;
		else
			sc->sc_rx_cons++;
	}

	if_rxr_put(&sc->sc_rx_ring, put);
	if (ifiq_input(&ifp->if_rcv, &ml))
		if_rxr_livelocked(&sc->sc_rx_ring);

	dwge_fill_rx_ring(sc);

	bus_dmamap_sync(sc->sc_dmat, DWGE_DMA_MAP(sc->sc_rxring), 0,
	    DWGE_DMA_LEN(sc->sc_rxring),
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

}

void
dwge_up(struct dwge_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct dwge_buf *txb, *rxb;
	uint32_t mode;
	int i;

	/* Allocate Tx descriptor ring. */
	sc->sc_txring = dwge_dmamem_alloc(sc,
	    DWGE_NTXDESC * sizeof(struct dwge_desc), 8);
	sc->sc_txdesc = DWGE_DMA_KVA(sc->sc_txring);

	sc->sc_txbuf = malloc(sizeof(struct dwge_buf) * DWGE_NTXDESC,
	    M_DEVBUF, M_WAITOK);
	for (i = 0; i < DWGE_NTXDESC; i++) {
		txb = &sc->sc_txbuf[i];
		bus_dmamap_create(sc->sc_dmat, MCLBYTES, DWGE_NTXSEGS,
		    MCLBYTES, 0, BUS_DMA_WAITOK, &txb->tb_map);
		txb->tb_m = NULL;

		sc->sc_txdesc[i].sd_next =
		    DWGE_DMA_DVA(sc->sc_txring) +
		    ((i+1) % DWGE_NTXDESC) * sizeof(struct dwge_desc);
		if (sc->sc_enh_desc)
			sc->sc_txdesc[i].sd_status = ETDES0_TCH;
		else
			sc->sc_txdesc[i].sd_len = TDES1_TCH;
	}

	bus_dmamap_sync(sc->sc_dmat, DWGE_DMA_MAP(sc->sc_txring),
	    0, DWGE_DMA_LEN(sc->sc_txring), BUS_DMASYNC_PREWRITE);

	sc->sc_tx_prod = sc->sc_tx_cons = 0;

	dwge_write(sc, GMAC_TX_DESC_LIST_ADDR, DWGE_DMA_DVA(sc->sc_txring));

	/* Allocate  descriptor ring. */
	sc->sc_rxring = dwge_dmamem_alloc(sc,
	    DWGE_NRXDESC * sizeof(struct dwge_desc), 8);
	sc->sc_rxdesc = DWGE_DMA_KVA(sc->sc_rxring);

	sc->sc_rxbuf = malloc(sizeof(struct dwge_buf) * DWGE_NRXDESC,
	    M_DEVBUF, M_WAITOK);

	for (i = 0; i < DWGE_NRXDESC; i++) {
		rxb = &sc->sc_rxbuf[i];
		bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1,
		    MCLBYTES, 0, BUS_DMA_WAITOK, &rxb->tb_map);
		rxb->tb_m = NULL;

		sc->sc_rxdesc[i].sd_next =
		    DWGE_DMA_DVA(sc->sc_rxring) +
		    ((i+1) % DWGE_NRXDESC) * sizeof(struct dwge_desc);
		sc->sc_rxdesc[i].sd_len =
		    sc->sc_enh_desc ? ERDES1_RCH : RDES1_RCH;
	}

	if_rxr_init(&sc->sc_rx_ring, 2, DWGE_NRXDESC);

	sc->sc_rx_prod = sc->sc_rx_cons = 0;
	dwge_fill_rx_ring(sc);

	bus_dmamap_sync(sc->sc_dmat, DWGE_DMA_MAP(sc->sc_rxring),
	    0, DWGE_DMA_LEN(sc->sc_rxring),
	    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

	dwge_write(sc, GMAC_RX_DESC_LIST_ADDR, DWGE_DMA_DVA(sc->sc_rxring));

	dwge_lladdr_write(sc);

	/* Configure media. */
	if (LIST_FIRST(&sc->sc_mii.mii_phys))
		mii_mediachg(&sc->sc_mii);

	/* Program promiscuous mode and multicast filters. */
	dwge_iff(sc);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	dwge_write(sc, GMAC_INT_ENA, GMAC_INT_ENA_NIE |
	    GMAC_INT_ENA_RIE | GMAC_INT_ENA_TIE | GMAC_INT_ENA_TUE);

	mode = dwge_read(sc, GMAC_OP_MODE);
	if (sc->sc_force_thresh_dma_mode) {
		mode &= ~(GMAC_OP_MODE_TSF | GMAC_OP_MODE_TTC_MASK);
		mode |= GMAC_OP_MODE_TTC_128;
		mode &= ~(GMAC_OP_MODE_RSF | GMAC_OP_MODE_RTC_MASK);
		mode |= GMAC_OP_MODE_RTC_128;
	} else {
		mode |= GMAC_OP_MODE_TSF | GMAC_OP_MODE_OSF;
		mode |= GMAC_OP_MODE_RSF;
	}
	dwge_write(sc, GMAC_OP_MODE, mode | GMAC_OP_MODE_ST | GMAC_OP_MODE_SR);

	dwge_write(sc, GMAC_MAC_CONF, dwge_read(sc, GMAC_MAC_CONF) |
	    GMAC_MAC_CONF_TE | GMAC_MAC_CONF_RE);

	if (sc->sc_fixed_media == 0)
		timeout_add_sec(&sc->sc_tick, 1);
}

void
dwge_down(struct dwge_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct dwge_buf *txb, *rxb;
	uint32_t dmactrl;
	int i;

	timeout_del(&sc->sc_rxto);
	if (sc->sc_fixed_media == 0)
		timeout_del(&sc->sc_tick);

	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;

	dwge_stop_dma(sc);

	dwge_write(sc, GMAC_MAC_CONF, dwge_read(sc,
	    GMAC_MAC_CONF) & ~(GMAC_MAC_CONF_TE | GMAC_MAC_CONF_RE));

	dmactrl = dwge_read(sc, GMAC_OP_MODE);
	dmactrl &= ~(GMAC_OP_MODE_ST | GMAC_OP_MODE_SR);
	dwge_write(sc, GMAC_OP_MODE, dmactrl);

	dwge_write(sc, GMAC_INT_ENA, 0);

	intr_barrier(sc->sc_ih);
	ifq_barrier(&ifp->if_snd);

	for (i = 0; i < DWGE_NTXDESC; i++) {
		txb = &sc->sc_txbuf[i];
		if (txb->tb_m) {
			bus_dmamap_sync(sc->sc_dmat, txb->tb_map, 0,
			    txb->tb_map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, txb->tb_map);
			m_freem(txb->tb_m);
		}
		bus_dmamap_destroy(sc->sc_dmat, txb->tb_map);
	}

	dwge_dmamem_free(sc, sc->sc_txring);
	free(sc->sc_txbuf, M_DEVBUF, 0);

	for (i = 0; i < DWGE_NRXDESC; i++) {
		rxb = &sc->sc_rxbuf[i];
		if (rxb->tb_m) {
			bus_dmamap_sync(sc->sc_dmat, rxb->tb_map, 0,
			    rxb->tb_map->dm_mapsize, BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->sc_dmat, rxb->tb_map);
			m_freem(rxb->tb_m);
		}
		bus_dmamap_destroy(sc->sc_dmat, rxb->tb_map);
	}

	dwge_dmamem_free(sc, sc->sc_rxring);
	free(sc->sc_rxbuf, M_DEVBUF, 0);
}

/* Bit Reversal - http://aggregate.org/MAGIC/#Bit%20Reversal */
static uint32_t
bitrev32(uint32_t x)
{
	x = (((x & 0xaaaaaaaa) >> 1) | ((x & 0x55555555) << 1));
	x = (((x & 0xcccccccc) >> 2) | ((x & 0x33333333) << 2));
	x = (((x & 0xf0f0f0f0) >> 4) | ((x & 0x0f0f0f0f) << 4));
	x = (((x & 0xff00ff00) >> 8) | ((x & 0x00ff00ff) << 8));

	return (x >> 16) | (x << 16);
}

void
dwge_iff(struct dwge_softc *sc)
{
	struct arpcom *ac = &sc->sc_ac;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct ether_multi *enm;
	struct ether_multistep step;
	uint32_t crc, hash[2], hashbit, hashreg;
	uint32_t reg;

	reg = 0;

	ifp->if_flags &= ~IFF_ALLMULTI;
	bzero(hash, sizeof(hash));
	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		reg |= GMAC_MAC_FRM_FILT_PM;
		if (ifp->if_flags & IFF_PROMISC)
			reg |= GMAC_MAC_FRM_FILT_PR;
	} else {
		reg |= GMAC_MAC_FRM_FILT_HMC;
		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			crc = ether_crc32_le(enm->enm_addrlo,
			    ETHER_ADDR_LEN) & 0x7f;

			crc = bitrev32(~crc) >> 26;
			hashreg = (crc >> 5);
			hashbit = (crc & 0x1f);
			hash[hashreg] |= (1 << hashbit);

			ETHER_NEXT_MULTI(step, enm);
		}
	}

	dwge_lladdr_write(sc);

	dwge_write(sc, GMAC_HASH_TAB_HI, hash[1]);
	dwge_write(sc, GMAC_HASH_TAB_LO, hash[0]);

	dwge_write(sc, GMAC_MAC_FRM_FILT, reg);
}

int
dwge_encap(struct dwge_softc *sc, struct mbuf *m, int *idx, int *used)
{
	struct dwge_desc *txd, *txd_start;
	bus_dmamap_t map;
	int cur, frag, i;

	cur = frag = *idx;
	map = sc->sc_txbuf[cur].tb_map;

	if (sc->sc_defrag) {
		if (m_defrag(m, M_DONTWAIT))
			return (ENOBUFS);
	}

	if (bus_dmamap_load_mbuf(sc->sc_dmat, map, m, BUS_DMA_NOWAIT)) {
		if (m_defrag(m, M_DONTWAIT))
			return (EFBIG);
		if (bus_dmamap_load_mbuf(sc->sc_dmat, map, m, BUS_DMA_NOWAIT))
			return (EFBIG);
	}

	/* Sync the DMA map. */
	bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	txd = txd_start = &sc->sc_txdesc[frag];
	for (i = 0; i < map->dm_nsegs; i++) {
		txd->sd_addr = map->dm_segs[i].ds_addr;
		if (sc->sc_enh_desc) {
			txd->sd_status = ETDES0_TCH;
			txd->sd_len = map->dm_segs[i].ds_len;
			if (i == 0)
				txd->sd_status |= ETDES0_FS;
			if (i == (map->dm_nsegs - 1))
				txd->sd_status |= ETDES0_LS | ETDES0_IC;
		} else {
			txd->sd_status = 0;
			txd->sd_len = map->dm_segs[i].ds_len | TDES1_TCH;
			if (i == 0)
				txd->sd_len |= TDES1_FS;
			if (i == (map->dm_nsegs - 1))
				txd->sd_len |= TDES1_LS | TDES1_IC;
		}
		if (i != 0)
			txd->sd_status |= TDES0_OWN;

		bus_dmamap_sync(sc->sc_dmat, DWGE_DMA_MAP(sc->sc_txring),
		    frag * sizeof(*txd), sizeof(*txd), BUS_DMASYNC_PREWRITE);

		cur = frag;
		if (frag == (DWGE_NTXDESC - 1)) {
			txd = &sc->sc_txdesc[0];
			frag = 0;
		} else {
			txd++;
			frag++;
		}
		KASSERT(frag != sc->sc_tx_cons);
	}

	txd_start->sd_status |= TDES0_OWN;
	bus_dmamap_sync(sc->sc_dmat, DWGE_DMA_MAP(sc->sc_txring),
	    *idx * sizeof(*txd), sizeof(*txd), BUS_DMASYNC_PREWRITE);

	KASSERT(sc->sc_txbuf[cur].tb_m == NULL);
	sc->sc_txbuf[*idx].tb_map = sc->sc_txbuf[cur].tb_map;
	sc->sc_txbuf[cur].tb_map = map;
	sc->sc_txbuf[cur].tb_m = m;

	*idx = frag;
	*used += map->dm_nsegs;

	return (0);
}

void
dwge_reset(struct dwge_softc *sc)
{
	int n;

	dwge_stop_dma(sc);

	dwge_write(sc, GMAC_BUS_MODE, dwge_read(sc, GMAC_BUS_MODE) |
	    GMAC_BUS_MODE_SWR);

	for (n = 0; n < 30000; n++) {
		if ((dwge_read(sc, GMAC_BUS_MODE) &
		    GMAC_BUS_MODE_SWR) == 0)
			return;
		delay(10);
	}

	printf("%s: reset timeout\n", sc->sc_dev.dv_xname);
}

void
dwge_stop_dma(struct dwge_softc *sc)
{
	uint32_t dmactrl;

	/* Stop DMA. */
	dmactrl = dwge_read(sc, GMAC_OP_MODE);
	dmactrl &= ~GMAC_OP_MODE_ST;
	dmactrl |= GMAC_OP_MODE_FTF;
	dwge_write(sc, GMAC_OP_MODE, dmactrl);
}

struct dwge_dmamem *
dwge_dmamem_alloc(struct dwge_softc *sc, bus_size_t size, bus_size_t align)
{
	struct dwge_dmamem *tdm;
	int nsegs;

	tdm = malloc(sizeof(*tdm), M_DEVBUF, M_WAITOK | M_ZERO);
	tdm->tdm_size = size;

	if (bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW, &tdm->tdm_map) != 0)
		goto tdmfree;

	if (bus_dmamem_alloc(sc->sc_dmat, size, align, 0, &tdm->tdm_seg, 1,
	    &nsegs, BUS_DMA_WAITOK) != 0)
		goto destroy;

	if (bus_dmamem_map(sc->sc_dmat, &tdm->tdm_seg, nsegs, size,
	    &tdm->tdm_kva, BUS_DMA_WAITOK | BUS_DMA_COHERENT) != 0)
		goto free;

	if (bus_dmamap_load(sc->sc_dmat, tdm->tdm_map, tdm->tdm_kva, size,
	    NULL, BUS_DMA_WAITOK) != 0)
		goto unmap;

	bzero(tdm->tdm_kva, size);

	return (tdm);

unmap:
	bus_dmamem_unmap(sc->sc_dmat, tdm->tdm_kva, size);
free:
	bus_dmamem_free(sc->sc_dmat, &tdm->tdm_seg, 1);
destroy:
	bus_dmamap_destroy(sc->sc_dmat, tdm->tdm_map);
tdmfree:
	free(tdm, M_DEVBUF, 0);

	return (NULL);
}

void
dwge_dmamem_free(struct dwge_softc *sc, struct dwge_dmamem *tdm)
{
	bus_dmamem_unmap(sc->sc_dmat, tdm->tdm_kva, tdm->tdm_size);
	bus_dmamem_free(sc->sc_dmat, &tdm->tdm_seg, 1);
	bus_dmamap_destroy(sc->sc_dmat, tdm->tdm_map);
	free(tdm, M_DEVBUF, 0);
}

struct mbuf *
dwge_alloc_mbuf(struct dwge_softc *sc, bus_dmamap_t map)
{
	struct mbuf *m = NULL;

	m = MCLGETL(NULL, M_DONTWAIT, MCLBYTES);
	if (!m)
		return (NULL);
	m->m_len = m->m_pkthdr.len = MCLBYTES;
	m_adj(m, ETHER_ALIGN);

	if (bus_dmamap_load_mbuf(sc->sc_dmat, map, m, BUS_DMA_NOWAIT) != 0) {
		printf("%s: could not load mbuf DMA map", DEVNAME(sc));
		m_freem(m);
		return (NULL);
	}

	bus_dmamap_sync(sc->sc_dmat, map, 0,
	    m->m_pkthdr.len, BUS_DMASYNC_PREREAD);

	return (m);
}

void
dwge_fill_rx_ring(struct dwge_softc *sc)
{
	struct dwge_desc *rxd;
	struct dwge_buf *rxb;
	u_int slots;

	for (slots = if_rxr_get(&sc->sc_rx_ring, DWGE_NRXDESC);
	    slots > 0; slots--) {
		rxb = &sc->sc_rxbuf[sc->sc_rx_prod];
		rxb->tb_m = dwge_alloc_mbuf(sc, rxb->tb_map);
		if (rxb->tb_m == NULL)
			break;

		rxd = &sc->sc_rxdesc[sc->sc_rx_prod];
		rxd->sd_len = rxb->tb_map->dm_segs[0].ds_len;
		rxd->sd_len |= sc->sc_enh_desc ? ERDES1_RCH : RDES1_RCH;
		rxd->sd_addr = rxb->tb_map->dm_segs[0].ds_addr;
		rxd->sd_status = RDES0_OWN;

		if (sc->sc_rx_prod == (DWGE_NRXDESC - 1))
			sc->sc_rx_prod = 0;
		else
			sc->sc_rx_prod++;
	}
	if_rxr_put(&sc->sc_rx_ring, slots);

	if (if_rxr_inuse(&sc->sc_rx_ring) == 0)
		timeout_add(&sc->sc_rxto, 1);
}

/*
 * Allwinner A20/A31.
 */

void
dwge_setup_allwinner(struct dwge_softc *sc)
{
	char phy_mode[8];
	uint32_t freq;

	/* default to RGMII */
	OF_getprop(sc->sc_node, "phy-mode", phy_mode, sizeof(phy_mode));
	if (strcmp(phy_mode, "mii") == 0)
		freq = 25000000;
	else
		freq = 125000000;
	clock_set_frequency(sc->sc_node, "allwinner_gmac_tx", freq);
}

/*
 * Rockchip RK3288/RK3399.
 */

/* RK3308 registers */
#define RK3308_GRF_MAC_CON0	0x04a0
#define RK3308_MAC_SPEED_100M	((0x1 << 0) << 16 | (0x1 << 0))
#define RK3308_MAC_SPEED_10M	((0x1 << 0) << 16 | (0x0 << 0))
#define RK3308_INTF_SEL_RMII	((0x1 << 4) << 16 | (0x1 << 4))

/* RK3288 registers */
#define RK3288_GRF_SOC_CON1	0x0248
#define  RK3288_GMAC_PHY_INTF_SEL_RGMII	((0x7 << 6) << 16 | (0x1 << 6))
#define  RK3288_GMAC_PHY_INTF_SEL_RMII	((0x7 << 6) << 16 | (0x4 << 6))
#define  RK3288_RMII_MODE_RMII		((1 << 14) << 16 | (1 << 14))
#define  RK3288_RMII_MODE_MII		((1 << 14) << 16 | (0 << 14))
#define  RK3288_GMAC_CLK_SEL_125	((0x3 << 12) << 16 | (0x0 << 12))
#define  RK3288_GMAC_CLK_SEL_25		((0x3 << 12) << 16 | (0x3 << 12))
#define  RK3288_GMAC_CLK_SEL_2_5	((0x3 << 12) << 16 | (0x2 << 12))

#define RK3288_GRF_SOC_CON3	0x0250
#define  RK3288_GMAC_RXCLK_DLY_ENA	((1 << 15) << 16 | (1 << 15))
#define  RK3288_GMAC_CLK_RX_DL_CFG(val) ((0x7f << 7) << 16 | ((val) << 7))
#define  RK3288_GMAC_TXCLK_DLY_ENA	((1 << 14) << 16 | (1 << 14))
#define  RK3288_GMAC_CLK_TX_DL_CFG(val) ((0x7f << 0) << 16 | ((val) << 0))

/* RK3328 registers */
#define RK3328_GRF_MAC_CON0	0x0900
#define  RK3328_GMAC_CLK_RX_DL_CFG(val) ((0x7f << 7) << 16 | ((val) << 7))
#define  RK3328_GMAC_CLK_TX_DL_CFG(val) ((0x7f << 0) << 16 | ((val) << 0))

#define RK3328_GRF_MAC_CON1	0x0904
#define  RK3328_GMAC_PHY_INTF_SEL_RGMII	((0x7 << 4) << 16 | (0x1 << 4))
#define  RK3328_GMAC_PHY_INTF_SEL_RMII	((0x7 << 4) << 16 | (0x4 << 4))
#define  RK3328_RMII_MODE_RMII		((1 << 9) << 16 | (1 << 9))
#define  RK3328_RMII_MODE_MII		((1 << 9) << 16 | (0 << 9))
#define  RK3328_GMAC_CLK_SEL_125	((0x3 << 11) << 16 | (0x0 << 11))
#define  RK3328_GMAC_CLK_SEL_25		((0x3 << 11) << 16 | (0x3 << 11))
#define  RK3328_GMAC_CLK_SEL_2_5	((0x3 << 11) << 16 | (0x2 << 11))
#define  RK3328_GMAC_RXCLK_DLY_ENA	((1 << 1) << 16 | (1 << 1))
#define  RK3328_GMAC_TXCLK_DLY_ENA	((1 << 0) << 16 | (1 << 0))

/* RK3399 registers */
#define RK3399_GRF_SOC_CON5	0xc214
#define  RK3399_GMAC_PHY_INTF_SEL_RGMII	((0x7 << 9) << 16 | (0x1 << 9))
#define  RK3399_GMAC_PHY_INTF_SEL_RMII	((0x7 << 9) << 16 | (0x4 << 9))
#define  RK3399_RMII_MODE_RMII		((1 << 6) << 16 | (1 << 6))
#define  RK3399_RMII_MODE_MII		((1 << 6) << 16 | (0 << 6))
#define  RK3399_GMAC_CLK_SEL_125	((0x3 << 4) << 16 | (0x0 << 4))
#define  RK3399_GMAC_CLK_SEL_25		((0x3 << 4) << 16 | (0x3 << 4))
#define  RK3399_GMAC_CLK_SEL_2_5	((0x3 << 4) << 16 | (0x2 << 4))
#define RK3399_GRF_SOC_CON6	0xc218
#define  RK3399_GMAC_RXCLK_DLY_ENA	((1 << 15) << 16 | (1 << 15))
#define  RK3399_GMAC_CLK_RX_DL_CFG(val) ((0x7f << 8) << 16 | ((val) << 8))
#define  RK3399_GMAC_TXCLK_DLY_ENA	((1 << 7) << 16 | (1 << 7))
#define  RK3399_GMAC_CLK_TX_DL_CFG(val) ((0x7f << 0) << 16 | ((val) << 0))

void	dwge_mii_statchg_rockchip(struct device *);

void
dwge_setup_rockchip(struct dwge_softc *sc)
{
	struct regmap *rm;
	uint32_t grf;
	int tx_delay, rx_delay;
	char clock_mode[8];

	grf = OF_getpropint(sc->sc_node, "rockchip,grf", 0);
	rm = regmap_byphandle(grf);
	if (rm == NULL)
		return;

	tx_delay = OF_getpropint(sc->sc_node, "tx_delay", 0x30);
	rx_delay = OF_getpropint(sc->sc_node, "rx_delay", 0x10);

	if (OF_is_compatible(sc->sc_node, "rockchip,rk3288-gmac")) {
		/* Use RGMII interface. */
		regmap_write_4(rm, RK3288_GRF_SOC_CON1,
		    RK3288_GMAC_PHY_INTF_SEL_RGMII | RK3288_RMII_MODE_MII);

		/* Program clock delay lines. */
		regmap_write_4(rm, RK3288_GRF_SOC_CON3,
		    RK3288_GMAC_TXCLK_DLY_ENA | RK3288_GMAC_RXCLK_DLY_ENA |
		    RK3288_GMAC_CLK_TX_DL_CFG(tx_delay) |
		    RK3288_GMAC_CLK_RX_DL_CFG(rx_delay));

		/* Clock speed bits. */
		sc->sc_clk_sel = RK3288_GRF_SOC_CON1;
		sc->sc_clk_sel_2_5 = RK3288_GMAC_CLK_SEL_2_5;
		sc->sc_clk_sel_25 = RK3288_GMAC_CLK_SEL_25;
		sc->sc_clk_sel_125 = RK3288_GMAC_CLK_SEL_125;
	} else if (OF_is_compatible(sc->sc_node, "rockchip,rk3308-mac")) {
		/* Use RMII interface. */
		regmap_write_4(rm, RK3308_GRF_MAC_CON0,
		    RK3308_INTF_SEL_RMII | RK3308_MAC_SPEED_100M);

		/* Adjust MAC clock if necessary. */
		OF_getprop(sc->sc_node, "clock_in_out", clock_mode,
		    sizeof(clock_mode));
		if (strcmp(clock_mode, "output") == 0) {
			clock_set_frequency(sc->sc_node, "stmmaceth",
			    50000000);
			sc->sc_clk = GMAC_GMII_ADDR_CR_DIV_26;
		}

		/* Clock speed bits. */
		sc->sc_clk_sel = RK3308_GRF_MAC_CON0;
		sc->sc_clk_sel_2_5 = RK3308_MAC_SPEED_10M;
		sc->sc_clk_sel_25 = RK3308_MAC_SPEED_100M;
	} else if (OF_is_compatible(sc->sc_node, "rockchip,rk3328-gmac")) {
		/* Use RGMII interface. */
		regmap_write_4(rm, RK3328_GRF_MAC_CON1,
		    RK3328_GMAC_PHY_INTF_SEL_RGMII | RK3328_RMII_MODE_MII);

		/* Program clock delay lines. */
		regmap_write_4(rm, RK3328_GRF_MAC_CON0,
		    RK3328_GMAC_CLK_TX_DL_CFG(tx_delay) |
		    RK3328_GMAC_CLK_RX_DL_CFG(rx_delay));
		regmap_write_4(rm, RK3328_GRF_MAC_CON1,
		    RK3328_GMAC_TXCLK_DLY_ENA | RK3328_GMAC_RXCLK_DLY_ENA);

		/* Clock speed bits. */
		sc->sc_clk_sel = RK3328_GRF_MAC_CON1;
		sc->sc_clk_sel_2_5 = RK3328_GMAC_CLK_SEL_2_5;
		sc->sc_clk_sel_25 = RK3328_GMAC_CLK_SEL_25;
		sc->sc_clk_sel_125 = RK3328_GMAC_CLK_SEL_125;
	} else {
		/* Use RGMII interface. */
		regmap_write_4(rm, RK3399_GRF_SOC_CON5,
		    RK3399_GMAC_PHY_INTF_SEL_RGMII | RK3399_RMII_MODE_MII);

		/* Program clock delay lines. */
		regmap_write_4(rm, RK3399_GRF_SOC_CON6,
		    RK3399_GMAC_TXCLK_DLY_ENA | RK3399_GMAC_RXCLK_DLY_ENA |
		    RK3399_GMAC_CLK_TX_DL_CFG(tx_delay) |
		    RK3399_GMAC_CLK_RX_DL_CFG(rx_delay));

		/* Clock speed bits. */
		sc->sc_clk_sel = RK3399_GRF_SOC_CON5;
		sc->sc_clk_sel_2_5 = RK3399_GMAC_CLK_SEL_2_5;
		sc->sc_clk_sel_25 = RK3399_GMAC_CLK_SEL_25;
		sc->sc_clk_sel_125 = RK3399_GMAC_CLK_SEL_125;
	}

	sc->sc_mii.mii_statchg = dwge_mii_statchg_rockchip;
}

void
dwge_mii_statchg_rockchip(struct device *self)
{
	struct dwge_softc *sc = (void *)self;
	struct regmap *rm;
	uint32_t grf;
	uint32_t gmac_clk_sel = 0;
	uint64_t media_active;

	dwge_mii_statchg(self);

	grf = OF_getpropint(sc->sc_node, "rockchip,grf", 0);
	rm = regmap_byphandle(grf);
	if (rm == NULL)
		return;

	media_active = sc->sc_fixed_media;
	if (media_active == 0)
		media_active = sc->sc_mii.mii_media_active;

	switch (IFM_SUBTYPE(media_active)) {
	case IFM_10_T:
		gmac_clk_sel = sc->sc_clk_sel_2_5;
		break;
	case IFM_100_TX:
		gmac_clk_sel = sc->sc_clk_sel_25;
		break;
	case IFM_1000_T:
		gmac_clk_sel = sc->sc_clk_sel_125;
		break;
	}

	regmap_write_4(rm, sc->sc_clk_sel, gmac_clk_sel);
}

#if NKSTAT > 0

struct dwge_counter {
	const char		*c_name;
	enum kstat_kv_unit	c_unit;
	uint32_t		c_reg;
};

const struct dwge_counter dwge_counters[] = {
	{ "tx octets total", KSTAT_KV_U_BYTES, GMAC_MMC_TXOCTETCNT_GB },
	{ "tx frames total", KSTAT_KV_U_PACKETS, GMAC_MMC_TXFRMCNT_GB },
	{ "tx underflow", KSTAT_KV_U_PACKETS, GMAC_MMC_TXUNDFLWERR },
	{ "tx carrier err", KSTAT_KV_U_PACKETS, GMAC_MMC_TXCARERR },
	{ "tx good octets", KSTAT_KV_U_BYTES, GMAC_MMC_TXOCTETCNT_G },
	{ "tx good frames", KSTAT_KV_U_PACKETS, GMAC_MMC_TXFRMCNT_G },
	{ "rx frames total", KSTAT_KV_U_PACKETS, GMAC_MMC_RXFRMCNT_GB },
	{ "rx octets total", KSTAT_KV_U_BYTES, GMAC_MMC_RXOCTETCNT_GB },
	{ "rx good octets", KSTAT_KV_U_BYTES, GMAC_MMC_RXOCTETCNT_G },
	{ "rx good mcast", KSTAT_KV_U_PACKETS, GMAC_MMC_RXMCFRMCNT_G },
	{ "rx crc errors", KSTAT_KV_U_PACKETS, GMAC_MMC_RXCRCERR },
	{ "rx len errors", KSTAT_KV_U_PACKETS, GMAC_MMC_RXLENERR },
	{ "rx fifo err", KSTAT_KV_U_PACKETS, GMAC_MMC_RXFIFOOVRFLW },
};

void
dwge_kstat_attach(struct dwge_softc *sc)
{
	struct kstat *ks;
	struct kstat_kv *kvs;
	int i;

	mtx_init(&sc->sc_kstat_mtx, IPL_NET);

	/* clear counters, enable reset-on-read */
	dwge_write(sc, GMAC_MAC_MMC_CTRL, GMAC_MAC_MMC_CTRL_ROR |
	    GMAC_MAC_MMC_CTRL_CR);

	ks = kstat_create(DEVNAME(sc), 0, "dwge-stats", 0,
	    KSTAT_T_KV, 0);
	if (ks == NULL)
		return;

	kvs = mallocarray(nitems(dwge_counters), sizeof(*kvs), M_DEVBUF,
	    M_WAITOK | M_ZERO);
	for (i = 0; i < nitems(dwge_counters); i++) {
		kstat_kv_unit_init(&kvs[i], dwge_counters[i].c_name,
		    KSTAT_KV_T_COUNTER64, dwge_counters[i].c_unit);
	}

	kstat_set_mutex(ks, &sc->sc_kstat_mtx);
	ks->ks_softc = sc;
	ks->ks_data = kvs;
	ks->ks_datalen = nitems(dwge_counters) * sizeof(*kvs);
	ks->ks_read = dwge_kstat_read;
	sc->sc_kstat = ks;
	kstat_install(ks);
}

int
dwge_kstat_read(struct kstat *ks)
{
	struct kstat_kv *kvs = ks->ks_data;
	struct dwge_softc *sc = ks->ks_softc;
	int i;

	for (i = 0; i < nitems(dwge_counters); i++)
		kstat_kv_u64(&kvs[i]) += dwge_read(sc, dwge_counters[i].c_reg);

	getnanouptime(&ks->ks_updated);
	return 0;
}

#endif
