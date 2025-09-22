/*	$OpenBSD: if_dwxe.c,v 1.24 2024/02/27 10:47:20 kettenis Exp $	*/
/*
 * Copyright (c) 2008 Mark Kettenis
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
 * Driver for the ethernet controller on the Allwinner H3/A64 SoCs.
 */

#include "bpfilter.h"

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
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/ofw_regulator.h>
#include <dev/ofw/fdt.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <netinet/in.h>
#include <netinet/if_ether.h>

/*
 * DWXE registers.
 */

#define DWXE_BASIC_CTL0		0x00
#define  DWXE_BASIC_CTL0_DUPLEX		(1 << 0)
#define  DWXE_BASIC_CTL0_LOOPBACK		(1 << 1)
#define  DWXE_BASIC_CTL0_SPEED_1000		(0 << 2)
#define  DWXE_BASIC_CTL0_SPEED_10		(2 << 2)
#define  DWXE_BASIC_CTL0_SPEED_100		(3 << 2)
#define  DWXE_BASIC_CTL0_SPEED_MASK		(3 << 2)
#define DWXE_BASIC_CTL1		0x04
#define  DWXE_BASIC_CTL1_SOFT_RST		(1 << 0)
#define  DWXE_BASIC_CTL1_RX_TX_PRI		(1 << 1)
#define  DWXE_BASIC_CTL1_BURST_LEN_MASK	(0x3f << 24)
#define  DWXE_BASIC_CTL1_BURST_LEN(x)		((x) << 24)
#define DWXE_INT_STA			0x08
#define  DWXE_INT_STA_TX_INT			(1 << 0)
#define  DWXE_INT_STA_TX_DMA_STOP_INT		(1 << 1)
#define  DWXE_INT_STA_TX_BUF_UA_INT		(1 << 2)
#define  DWXE_INT_STA_TX_TIMEOUT_INT		(1 << 3)
#define  DWXE_INT_STA_TX_UNDERFLOW_INT	(1 << 4)
#define  DWXE_INT_STA_TX_EARLY_INT		(1 << 5)
#define  DWXE_INT_STA_RX_INT			(1 << 8)
#define  DWXE_INT_STA_RX_BUF_UA_INT		(1 << 9)
#define  DWXE_INT_STA_RX_DMA_STOP_INT		(1 << 10)
#define  DWXE_INT_STA_RX_TIMEOUT_INT		(1 << 11)
#define  DWXE_INT_STA_RX_OVERFLOW_INT		(1 << 12)
#define  DWXE_INT_STA_RX_EARLY_INT		(1 << 13)
#define  DWXE_INT_STA_RGMII_STA_INT		(1 << 16)
#define DWXE_INT_EN			0x0C
#define  DWXE_INT_EN_TX_INT			(1 << 0)
#define  DWXE_INT_EN_TX_DMA_STOP_INT		(1 << 1)
#define  DWXE_INT_EN_TX_BUF_UA_INT		(1 << 2)
#define  DWXE_INT_EN_TX_TIMEOUT_INT		(1 << 3)
#define  DWXE_INT_EN_TX_UNDERFLOW_INT		(1 << 4)
#define  DWXE_INT_EN_TX_EARLY_INT		(1 << 5)
#define  DWXE_INT_EN_RX_INT			(1 << 8)
#define  DWXE_INT_EN_RX_BUF_UA_INT		(1 << 9)
#define  DWXE_INT_EN_RX_DMA_STOP_INT		(1 << 10)
#define  DWXE_INT_EN_RX_TIMEOUT_INT		(1 << 11)
#define  DWXE_INT_EN_RX_OVERFLOW_INT		(1 << 12)
#define  DWXE_INT_EN_RX_EARLY_INT		(1 << 13)
#define  DWXE_INT_EN_RGMII_EN_INT		(1 << 16)
#define DWXE_TX_CTL0			0x10
#define  DWXE_TX_CTL0_TX_TRANSMITTER_EN	(1U << 31)
#define DWXE_TX_CTL1			0x14
#define  DWXE_TX_CTL1_TX_FIFO_FLUSH		(1 << 0)
#define  DWXE_TX_CTL1_TX_MD			(1 << 1)
#define  DWXE_TX_CTL1_TX_NEXT_FRM		(1 << 2)
#define  DWXE_TX_CTL1_TX_TH_MASK		(0x3 << 8)
#define  DWXE_TX_CTL1_TX_TH_64		0
#define  DWXE_TX_CTL1_TX_TH_128		(0x1 << 8)
#define  DWXE_TX_CTL1_TX_TH_192		(0x2 << 8)
#define  DWXE_TX_CTL1_TX_TH_256		(0x3 << 8)
#define  DWXE_TX_CTL1_TX_DMA_EN		(1 << 30)
#define  DWXE_TX_CTL1_TX_DMA_START		(1U << 31)
#define DWXE_TX_FLOW_CTL		0x1C
#define  DWXE_TX_FLOW_CTL_EN			(1 << 0)
#define DWXE_TX_DESC_LIST		0x20
#define DWXE_RX_CTL0			0x24
#define  DWXE_RX_CTL0_RX_FLOW_CTL_EN		(1 << 16)
#define  DWXE_RX_CTL0_RX_DO_CRC		(1 << 27)
#define  DWXE_RX_CTL0_RX_RECEIVER_EN		(1U << 31)
#define DWXE_RX_CTL1			0x28
#define  DWXE_RX_CTL1_RX_MD			(1 << 1)
#define  DWXE_RX_CTL1_RX_TH_MASK		(0x3 << 4)
#define  DWXE_RX_CTL1_RX_TH_32		(0x0 << 4)
#define  DWXE_RX_CTL1_RX_TH_64		(0x1 << 4)
#define  DWXE_RX_CTL1_RX_TH_96		(0x2 << 4)
#define  DWXE_RX_CTL1_RX_TH_128		(0x3 << 4)
#define  DWXE_RX_CTL1_RX_DMA_EN		(1 << 30)
#define  DWXE_RX_CTL1_RX_DMA_START		(1U << 31)
#define DWXE_RX_DESC_LIST		0x34
#define DWXE_RX_FRM_FLT		0x38
#define DWXE_RX_FRM_FLT_RX_ALL		(1 << 0)
#define DWXE_RX_FRM_FLT_HASH_UNICAST		(1 << 8)
#define DWXE_RX_FRM_FLT_HASH_MULTICAST	(1 << 9)
#define DWXE_RX_FRM_FLT_CTL			(1 << 13)
#define DWXE_RX_FRM_FLT_RX_ALL_MULTICAST	(1 << 16)
#define DWXE_RX_FRM_FLT_DIS_BROADCAST		(1 << 17)
#define DWXE_RX_FRM_FLT_DIS_ADDR_FILTER	(1U << 31)
#define DWXE_RX_HASH0			0x40
#define DWXE_RX_HASH1			0x44
#define DWXE_MDIO_CMD			0x48
#define  DWXE_MDIO_CMD_MII_BUSY		(1 << 0)
#define  DWXE_MDIO_CMD_MII_WRITE		(1 << 1)
#define  DWXE_MDIO_CMD_PHY_REG_SHIFT		4
#define  DWXE_MDIO_CMD_PHY_ADDR_SHIFT		12
#define  DWXE_MDIO_CMD_MDC_DIV_RATIO_M_SHIFT	20
#define  DWXE_MDIO_CMD_MDC_DIV_RATIO_M_MASK	0x7
#define  DWXE_MDIO_CMD_MDC_DIV_RATIO_M_16	0
#define  DWXE_MDIO_CMD_MDC_DIV_RATIO_M_32	1
#define  DWXE_MDIO_CMD_MDC_DIV_RATIO_M_64	2
#define  DWXE_MDIO_CMD_MDC_DIV_RATIO_M_128	3
#define DWXE_MDIO_DATA		0x4C
#define DWXE_MACADDR_HI		0x50
#define DWXE_MACADDR_LO		0x54
#define DWXE_TX_DMA_STA		0xB0
#define DWXE_TX_CUR_DESC		0xB4
#define DWXE_TX_CUR_BUF		0xB8
#define DWXE_RX_DMA_STA		0xC0
#define DWXE_RX_CUR_DESC		0xC4
#define DWXE_RX_CUR_BUF		0xC8

/*
 * DWXE descriptors.
 */

struct dwxe_desc {
	uint32_t sd_status;
	uint32_t sd_len;
	uint32_t sd_addr;
	uint32_t sd_next;
};

/* Tx status bits. */
#define DWXE_TX_DEFER			(1 << 0)
#define DWXE_TX_UNDERFLOW_ERR		(1 << 1)
#define DWXE_TX_DEFER_ERR		(1 << 2)
#define DWXE_TX_COL_CNT_MASK		(0xf << 3)
#define DWXE_TX_COL_CNT_SHIFT		3
#define DWXE_TX_COL_ERR_1		(1 << 8)
#define DWXE_TX_COL_ERR_0		(1 << 9)
#define DWXE_TX_CRS_ERR		(1 << 10)
#define DWXE_TX_PAYLOAD_ERR		(1 << 12)
#define DWXE_TX_LENGTH_ERR		(1 << 14)
#define DWXE_TX_HEADER_ERR		(1 << 16)
#define DWXE_TX_DESC_CTL		(1U << 31)

/* Rx status bits */
#define DWXE_RX_PAYLOAD_ERR		(1 << 0)
#define DWXE_RX_CRC_ERR		(1 << 1)
#define DWXE_RX_PHY_ERR		(1 << 3)
#define DWXE_RX_LENGTH_ERR		(1 << 4)
#define DWXE_RX_FRM_TYPE		(1 << 5)
#define DWXE_RX_COL_ERR		(1 << 6)
#define DWXE_RX_HEADER_ERR		(1 << 7)
#define DWXE_RX_LAST_DESC		(1 << 8)
#define DWXE_RX_FIR_DESC		(1 << 9)
#define DWXE_RX_OVERFLOW_ERR		(1 << 11)
#define DWXE_RX_SAF_FAIL		(1 << 13)
#define DWXE_RX_NO_ENOUGH_BUF_ERR	(1 << 14)
#define DWXE_RX_FRM_LEN_MASK		0x3fff
#define DWXE_RX_FRM_LEN_SHIFT		16
#define DWXE_RX_DAF_FAIL		(1 << 30)
#define DWXE_RX_DESC_CTL		(1U << 31)

/* Tx size bits */
#define DWXE_TX_BUF_SIZE		(0xfff << 0)
#define DWXE_TX_CRC_CTL		(1 << 26)
#define DWXE_TX_CHECKSUM_CTL_MASK	(0x3 << 27)
#define DWXE_TX_CHECKSUM_CTL_IP	(1 << 27)
#define DWXE_TX_CHECKSUM_CTL_NO_PSE	(2 << 27)
#define DWXE_TX_CHECKSUM_CTL_FULL	(3 << 27)
#define DWXE_TX_FIR_DESC		(1 << 29)
#define DWXE_TX_LAST_DESC		(1 << 30)
#define DWXE_TX_INT_CTL		(1U << 31)

/* Rx size bits */
#define DWXE_RX_BUF_SIZE		(0xfff << 0)
#define DWXE_RX_INT_CTL		(1U << 31)

/* EMAC syscon bits */
#define SYSCON_EMAC			0x30
#define SYSCON_ETCS_MASK		(0x3 << 0)
#define SYSCON_ETCS_MII			(0 << 0)
#define SYSCON_ETCS_EXT_GMII		(1 << 0)
#define SYSCON_ETCS_INT_GMII		(2 << 0)
#define SYSCON_EPIT			(1 << 2) /* 1: RGMII, 0: MII */
#define SYSCON_ERXDC_MASK		(0xf << 5)
#define SYSCON_ERXDC_SHIFT		5
#define SYSCON_ETXDC_MASK		(0x7 << 10)
#define SYSCON_ETXDC_SHIFT		10
#define SYSCON_RMII_EN			(1 << 13) /* 1: enable RMII (overrides EPIT) */
#define SYSCON_H3_EPHY_SELECT		(1 << 15) /* 1: internal PHY, 0: external PHY */
#define SYSCON_H3_EPHY_SHUTDOWN		(1 << 16) /* 1: shutdown, 0: power up */
#define SYSCON_H3_EPHY_LED_POL		(1 << 17) /* 1: active low, 0: active high */
#define SYSCON_H3_EPHY_CLK_SEL		(1 << 18) /* 1: 24MHz, 0: 25MHz */
#define SYSCON_H3_EPHY_ADDR_MASK	(0x1f << 20)
#define SYSCON_H3_EPHY_ADDR_SHIFT	20

/* GMAC syscon bits (Allwinner R40) */
#define SYSCON_GMAC			0x00
#define SYSCON_GTCS_MASK		SYSCON_ETCS_MASK
#define SYSCON_GTCS_MII			SYSCON_ETCS_MII
#define SYSCON_GTCS_EXT_GMII		SYSCON_ETCS_EXT_GMII
#define SYSCON_GTCS_INT_GMII		SYSCON_ETCS_INT_GMII
#define SYSCON_GPIT			SYSCON_EPIT
#define SYSCON_GRXDC_MASK		(0x7 << 5)
#define SYSCON_GRXDC_SHIFT		5

struct dwxe_buf {
	bus_dmamap_t	tb_map;
	struct mbuf	*tb_m;
};

#define DWXE_NTXDESC	256
#define DWXE_NTXSEGS	16

#define DWXE_NRXDESC	256

struct dwxe_dmamem {
	bus_dmamap_t		tdm_map;
	bus_dma_segment_t	tdm_seg;
	size_t			tdm_size;
	caddr_t			tdm_kva;
};
#define DWXE_DMA_MAP(_tdm)	((_tdm)->tdm_map)
#define DWXE_DMA_LEN(_tdm)	((_tdm)->tdm_size)
#define DWXE_DMA_DVA(_tdm)	((_tdm)->tdm_map->dm_segs[0].ds_addr)
#define DWXE_DMA_KVA(_tdm)	((void *)(_tdm)->tdm_kva)

struct dwxe_softc {
	struct device		sc_dev;
	int			sc_node;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_dma_tag_t		sc_dmat;
	void			*sc_ih;

	struct arpcom		sc_ac;
#define sc_lladdr	sc_ac.ac_enaddr
	struct mii_data		sc_mii;
#define sc_media	sc_mii.mii_media
	int			sc_link;
	int			sc_phyloc;

	struct dwxe_dmamem	*sc_txring;
	struct dwxe_buf		*sc_txbuf;
	struct dwxe_desc	*sc_txdesc;
	int			sc_tx_prod;
	int			sc_tx_cons;

	struct dwxe_dmamem	*sc_rxring;
	struct dwxe_buf		*sc_rxbuf;
	struct dwxe_desc	*sc_rxdesc;
	int			sc_rx_prod;
	struct if_rxring	sc_rx_ring;
	int			sc_rx_cons;

	struct timeout		sc_tick;
	struct timeout		sc_rxto;

	uint32_t		sc_clk;
};

#define DEVNAME(_s)	((_s)->sc_dev.dv_xname)

int	dwxe_match(struct device *, void *, void *);
void	dwxe_attach(struct device *, struct device *, void *);
int	dwxe_activate(struct device *, int);
void	dwxe_init(struct dwxe_softc *sc);
void	dwxe_phy_setup_emac(struct dwxe_softc *);
void	dwxe_phy_setup_gmac(struct dwxe_softc *);

const struct cfattach dwxe_ca = {
	sizeof(struct dwxe_softc), dwxe_match, dwxe_attach,
	NULL, dwxe_activate
};

struct cfdriver dwxe_cd = {
	NULL, "dwxe", DV_IFNET
};

uint32_t dwxe_read(struct dwxe_softc *, bus_addr_t);
void	dwxe_write(struct dwxe_softc *, bus_addr_t, uint32_t);

int	dwxe_ioctl(struct ifnet *, u_long, caddr_t);
void	dwxe_start(struct ifqueue *);
void	dwxe_watchdog(struct ifnet *);

int	dwxe_media_change(struct ifnet *);
void	dwxe_media_status(struct ifnet *, struct ifmediareq *);

int	dwxe_mii_readreg(struct device *, int, int);
void	dwxe_mii_writereg(struct device *, int, int, int);
void	dwxe_mii_statchg(struct device *);

void	dwxe_lladdr_read(struct dwxe_softc *, uint8_t *);
void	dwxe_lladdr_write(struct dwxe_softc *);

void	dwxe_tick(void *);
void	dwxe_rxtick(void *);

int	dwxe_intr(void *);
void	dwxe_tx_proc(struct dwxe_softc *);
void	dwxe_rx_proc(struct dwxe_softc *);

void	dwxe_up(struct dwxe_softc *);
void	dwxe_down(struct dwxe_softc *);
void	dwxe_iff(struct dwxe_softc *);
int	dwxe_encap(struct dwxe_softc *, struct mbuf *, int *, int *);

void	dwxe_reset(struct dwxe_softc *);
void	dwxe_stop_dma(struct dwxe_softc *);

struct dwxe_dmamem *
	dwxe_dmamem_alloc(struct dwxe_softc *, bus_size_t, bus_size_t);
void	dwxe_dmamem_free(struct dwxe_softc *, struct dwxe_dmamem *);
struct mbuf *dwxe_alloc_mbuf(struct dwxe_softc *, bus_dmamap_t);
void	dwxe_fill_rx_ring(struct dwxe_softc *);

int
dwxe_match(struct device *parent, void *cfdata, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "allwinner,sun8i-h3-emac") ||
	    OF_is_compatible(faa->fa_node, "allwinner,sun8i-r40-gmac") ||
	    OF_is_compatible(faa->fa_node, "allwinner,sun50i-a64-emac");
}

void
dwxe_attach(struct device *parent, struct device *self, void *aux)
{
	struct dwxe_softc *sc = (void *)self;
	struct fdt_attach_args *faa = aux;
	char phy_mode[16] = { 0 };
	struct ifnet *ifp;
	uint32_t phy;
	int mii_flags = 0;
	int node;

	sc->sc_node = faa->fa_node;
	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf("%s: cannot map registers\n", self->dv_xname);
		return;
	}
	sc->sc_dmat = faa->fa_dmat;

	OF_getprop(faa->fa_node, "phy-mode", phy_mode, sizeof(phy_mode));
	if (strcmp(phy_mode, "rgmii") == 0)
		mii_flags |= MIIF_SETDELAY;
	else if (strcmp(phy_mode, "rgmii-rxid") == 0)
		mii_flags |= MIIF_SETDELAY | MIIF_RXID;
	else if (strcmp(phy_mode, "rgmii-txid") == 0)
		mii_flags |= MIIF_SETDELAY | MIIF_TXID;
	else if (strcmp(phy_mode, "rgmii-id") == 0)
		mii_flags |= MIIF_SETDELAY | MIIF_RXID | MIIF_TXID;

	/* Lookup PHY. */
	phy = OF_getpropint(faa->fa_node, "phy-handle", 0);
	node = OF_getnodebyphandle(phy);
	if (node)
		sc->sc_phyloc = OF_getpropint(node, "reg", MII_PHY_ANY);
	else
		sc->sc_phyloc = MII_PHY_ANY;
	sc->sc_mii.mii_node = node;

	sc->sc_clk = clock_get_frequency(faa->fa_node, "stmmaceth");
	if (sc->sc_clk > 160000000)
		sc->sc_clk = DWXE_MDIO_CMD_MDC_DIV_RATIO_M_128;
	else if (sc->sc_clk > 80000000)
		sc->sc_clk = DWXE_MDIO_CMD_MDC_DIV_RATIO_M_64;
	else if (sc->sc_clk > 40000000)
		sc->sc_clk = DWXE_MDIO_CMD_MDC_DIV_RATIO_M_32;
	else
		sc->sc_clk = DWXE_MDIO_CMD_MDC_DIV_RATIO_M_16;

	if (OF_getprop(faa->fa_node, "local-mac-address",
	    &sc->sc_lladdr, ETHER_ADDR_LEN) != ETHER_ADDR_LEN)
		dwxe_lladdr_read(sc, sc->sc_lladdr);
	printf(": address %s\n", ether_sprintf(sc->sc_lladdr));

	dwxe_init(sc);

	timeout_set(&sc->sc_tick, dwxe_tick, sc);
	timeout_set(&sc->sc_rxto, dwxe_rxtick, sc);

	ifp = &sc->sc_ac.ac_if;
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_xflags = IFXF_MPSAFE;
	ifp->if_ioctl = dwxe_ioctl;
	ifp->if_qstart = dwxe_start;
	ifp->if_watchdog = dwxe_watchdog;
	ifq_init_maxlen(&ifp->if_snd, DWXE_NTXDESC - 1);
	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

	sc->sc_mii.mii_ifp = ifp;
	sc->sc_mii.mii_readreg = dwxe_mii_readreg;
	sc->sc_mii.mii_writereg = dwxe_mii_writereg;
	sc->sc_mii.mii_statchg = dwxe_mii_statchg;

	ifmedia_init(&sc->sc_media, 0, dwxe_media_change, dwxe_media_status);

	mii_attach(self, &sc->sc_mii, 0xffffffff, sc->sc_phyloc,
	    MII_OFFSET_ANY, MIIF_NOISOLATE | mii_flags);
	if (LIST_FIRST(&sc->sc_mii.mii_phys) == NULL) {
		printf("%s: no PHY found!\n", sc->sc_dev.dv_xname);
		ifmedia_add(&sc->sc_media, IFM_ETHER|IFM_MANUAL, 0, NULL);
		ifmedia_set(&sc->sc_media, IFM_ETHER|IFM_MANUAL);
	} else
		ifmedia_set(&sc->sc_media, IFM_ETHER|IFM_AUTO);

	if_attach(ifp);
	ether_ifattach(ifp);

	sc->sc_ih = fdt_intr_establish(faa->fa_node, IPL_NET | IPL_MPSAFE,
	    dwxe_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL)
		printf("%s: can't establish interrupt\n", sc->sc_dev.dv_xname);
}

int
dwxe_activate(struct device *self, int act)
{
	struct dwxe_softc *sc = (struct dwxe_softc *)self;
	struct ifnet *ifp = &sc->sc_ac.ac_if;

	switch (act) {
	case DVACT_SUSPEND:
		if (ifp->if_flags & IFF_RUNNING)
			dwxe_down(sc);
		break;
	case DVACT_RESUME:
		dwxe_init(sc);
		if (ifp->if_flags & IFF_UP)
			dwxe_up(sc);
		break;
	}

	return 0;
}

void
dwxe_init(struct dwxe_softc *sc)
{
	uint32_t phy_supply;

	pinctrl_byname(sc->sc_node, "default");

	/* Enable clock. */
	clock_enable(sc->sc_node, "stmmaceth");
	reset_deassert(sc->sc_node, "stmmaceth");
	delay(5000);

	/* Power up PHY. */
	phy_supply = OF_getpropint(sc->sc_node, "phy-supply", 0);
	if (phy_supply)
		regulator_enable(phy_supply);

	/* Do hardware specific initializations. */
	if (OF_is_compatible(sc->sc_node, "allwinner,sun8i-r40-gmac"))
		dwxe_phy_setup_gmac(sc);
	else
		dwxe_phy_setup_emac(sc);

	dwxe_reset(sc);
}

void
dwxe_phy_setup_emac(struct dwxe_softc *sc)
{
	struct regmap *rm;
	uint32_t syscon;
	uint32_t tx_delay, rx_delay;
	char *phy_mode;
	int len;

	rm = regmap_byphandle(OF_getpropint(sc->sc_node, "syscon", 0));
	if (rm == NULL)
		return;

	syscon = regmap_read_4(rm, SYSCON_EMAC);
	syscon &= ~(SYSCON_ETCS_MASK|SYSCON_EPIT|SYSCON_RMII_EN);
	syscon &= ~(SYSCON_ETXDC_MASK | SYSCON_ERXDC_MASK);
	syscon &= ~SYSCON_H3_EPHY_SELECT;

	if ((len = OF_getproplen(sc->sc_node, "phy-mode")) <= 0)
		return;
	phy_mode = malloc(len, M_TEMP, M_WAITOK);
	OF_getprop(sc->sc_node, "phy-mode", phy_mode, len);
	if (!strncmp(phy_mode, "rgmii", strlen("rgmii")))
		syscon |= SYSCON_EPIT | SYSCON_ETCS_INT_GMII;
	else if (!strncmp(phy_mode, "rmii", strlen("rmii")))
		syscon |= SYSCON_EPIT | SYSCON_ETCS_EXT_GMII;
	else if (!strncmp(phy_mode, "mii", strlen("mii")) &&
	    OF_is_compatible(sc->sc_node, "allwinner,sun8i-h3-emac")) {
		syscon &= ~SYSCON_H3_EPHY_SHUTDOWN;
		syscon |= SYSCON_H3_EPHY_SELECT | SYSCON_H3_EPHY_CLK_SEL;
		if (OF_getproplen(sc->sc_node, "allwinner,leds-active-low") == 0)
			syscon |= SYSCON_H3_EPHY_LED_POL;
		else
			syscon &= ~SYSCON_H3_EPHY_LED_POL;
		syscon &= ~SYSCON_H3_EPHY_ADDR_MASK;
		syscon |= (sc->sc_phyloc << SYSCON_H3_EPHY_ADDR_SHIFT);
	}
	free(phy_mode, M_TEMP, len);

	tx_delay = OF_getpropint(sc->sc_node, "allwinner,tx-delay-ps", 0);
	rx_delay = OF_getpropint(sc->sc_node, "allwinner,rx-delay-ps", 0);
	syscon |= ((tx_delay / 100) << SYSCON_ETXDC_SHIFT) & SYSCON_ETXDC_MASK;
	syscon |= ((rx_delay / 100) << SYSCON_ERXDC_SHIFT) & SYSCON_ERXDC_MASK;

	regmap_write_4(rm, SYSCON_EMAC, syscon);
}

void
dwxe_phy_setup_gmac(struct dwxe_softc *sc)
{
	struct regmap *rm;
	uint32_t syscon;
	uint32_t rx_delay;
	char *phy_mode;
	int len;

	rm = regmap_byphandle(OF_getpropint(sc->sc_node, "syscon", 0));
	if (rm == NULL)
		return;

	syscon = regmap_read_4(rm, SYSCON_GMAC);
	syscon &= ~(SYSCON_GTCS_MASK|SYSCON_GPIT|SYSCON_ERXDC_MASK);

	if ((len = OF_getproplen(sc->sc_node, "phy-mode")) <= 0)
		return;
	phy_mode = malloc(len, M_TEMP, M_WAITOK);
	OF_getprop(sc->sc_node, "phy-mode", phy_mode, len);
	if (!strncmp(phy_mode, "rgmii", strlen("rgmii")))
		syscon |= SYSCON_GPIT | SYSCON_GTCS_INT_GMII;
	else if (!strncmp(phy_mode, "rmii", strlen("rmii")))
		syscon |= SYSCON_GPIT | SYSCON_GTCS_EXT_GMII;
	free(phy_mode, M_TEMP, len);

	rx_delay = OF_getpropint(sc->sc_node, "allwinner,rx-delay-ps", 0);
	syscon |= ((rx_delay / 100) << SYSCON_ERXDC_SHIFT) & SYSCON_ERXDC_MASK;

	regmap_write_4(rm, SYSCON_GMAC, syscon);
}

uint32_t
dwxe_read(struct dwxe_softc *sc, bus_addr_t addr)
{
	return bus_space_read_4(sc->sc_iot, sc->sc_ioh, addr);
}

void
dwxe_write(struct dwxe_softc *sc, bus_addr_t addr, uint32_t data)
{
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, addr, data);
}

void
dwxe_lladdr_read(struct dwxe_softc *sc, uint8_t *lladdr)
{
	uint32_t machi, maclo;

	machi = dwxe_read(sc, DWXE_MACADDR_HI);
	maclo = dwxe_read(sc, DWXE_MACADDR_LO);

	lladdr[0] = (maclo >> 0) & 0xff;
	lladdr[1] = (maclo >> 8) & 0xff;
	lladdr[2] = (maclo >> 16) & 0xff;
	lladdr[3] = (maclo >> 24) & 0xff;
	lladdr[4] = (machi >> 0) & 0xff;
	lladdr[5] = (machi >> 8) & 0xff;
}

void
dwxe_lladdr_write(struct dwxe_softc *sc)
{
	dwxe_write(sc, DWXE_MACADDR_HI,
	    sc->sc_lladdr[5] << 8 | sc->sc_lladdr[4] << 0);
	dwxe_write(sc, DWXE_MACADDR_LO,
	    sc->sc_lladdr[3] << 24 | sc->sc_lladdr[2] << 16 |
	    sc->sc_lladdr[1] << 8 | sc->sc_lladdr[0] << 0);
}

void
dwxe_start(struct ifqueue *ifq)
{
	struct ifnet *ifp = ifq->ifq_if;
	struct dwxe_softc *sc = ifp->if_softc;
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
		left += DWXE_NTXDESC;
	left -= idx;
	used = 0;

	for (;;) {
		if (used + DWXE_NTXSEGS + 1 > left) {
			ifq_set_oactive(ifq);
			break;
		}

		m = ifq_dequeue(ifq);
		if (m == NULL)
			break;

		error = dwxe_encap(sc, m, &idx, &used);
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

		dwxe_write(sc, DWXE_TX_CTL1, dwxe_read(sc,
		     DWXE_TX_CTL1) | DWXE_TX_CTL1_TX_DMA_START);
	}
}

int
dwxe_ioctl(struct ifnet *ifp, u_long cmd, caddr_t addr)
{
	struct dwxe_softc *sc = ifp->if_softc;
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
				dwxe_up(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				dwxe_down(sc);
		}
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
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
			dwxe_iff(sc);
		error = 0;
	}

	splx(s);
	return (error);
}

void
dwxe_watchdog(struct ifnet *ifp)
{
	printf("%s\n", __func__);
}

int
dwxe_media_change(struct ifnet *ifp)
{
	struct dwxe_softc *sc = ifp->if_softc;

	if (LIST_FIRST(&sc->sc_mii.mii_phys))
		mii_mediachg(&sc->sc_mii);

	return (0);
}

void
dwxe_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct dwxe_softc *sc = ifp->if_softc;

	if (LIST_FIRST(&sc->sc_mii.mii_phys)) {
		mii_pollstat(&sc->sc_mii);
		ifmr->ifm_active = sc->sc_mii.mii_media_active;
		ifmr->ifm_status = sc->sc_mii.mii_media_status;
	}
}

int
dwxe_mii_readreg(struct device *self, int phy, int reg)
{
	struct dwxe_softc *sc = (void *)self;
	int n;

	dwxe_write(sc, DWXE_MDIO_CMD,
	    sc->sc_clk << DWXE_MDIO_CMD_MDC_DIV_RATIO_M_SHIFT |
	    phy << DWXE_MDIO_CMD_PHY_ADDR_SHIFT |
	    reg << DWXE_MDIO_CMD_PHY_REG_SHIFT |
	    DWXE_MDIO_CMD_MII_BUSY);
	for (n = 0; n < 1000; n++) {
		if ((dwxe_read(sc, DWXE_MDIO_CMD) &
		    DWXE_MDIO_CMD_MII_BUSY) == 0)
			return dwxe_read(sc, DWXE_MDIO_DATA);
		delay(10);
	}

	printf("%s: mii_read timeout\n", sc->sc_dev.dv_xname);
	return (0);
}

void
dwxe_mii_writereg(struct device *self, int phy, int reg, int val)
{
	struct dwxe_softc *sc = (void *)self;
	int n;

	dwxe_write(sc, DWXE_MDIO_DATA, val);
	dwxe_write(sc, DWXE_MDIO_CMD,
	    sc->sc_clk << DWXE_MDIO_CMD_MDC_DIV_RATIO_M_SHIFT |
	    phy << DWXE_MDIO_CMD_PHY_ADDR_SHIFT |
	    reg << DWXE_MDIO_CMD_PHY_REG_SHIFT |
	    DWXE_MDIO_CMD_MII_WRITE |
	    DWXE_MDIO_CMD_MII_BUSY);
	for (n = 0; n < 1000; n++) {
		if ((dwxe_read(sc, DWXE_MDIO_CMD) &
		    DWXE_MDIO_CMD_MII_BUSY) == 0)
			return;
		delay(10);
	}

	printf("%s: mii_write timeout\n", sc->sc_dev.dv_xname);
}

void
dwxe_mii_statchg(struct device *self)
{
	struct dwxe_softc *sc = (void *)self;
	uint32_t basicctrl;

	basicctrl = dwxe_read(sc, DWXE_BASIC_CTL0);
	basicctrl &= ~DWXE_BASIC_CTL0_SPEED_MASK;

	switch (IFM_SUBTYPE(sc->sc_mii.mii_media_active)) {
	case IFM_1000_SX:
	case IFM_1000_LX:
	case IFM_1000_CX:
	case IFM_1000_T:
		basicctrl |= DWXE_BASIC_CTL0_SPEED_1000;
		sc->sc_link = 1;
		break;
	case IFM_100_TX:
		basicctrl |= DWXE_BASIC_CTL0_SPEED_100;
		sc->sc_link = 1;
		break;
	case IFM_10_T:
		basicctrl |= DWXE_BASIC_CTL0_SPEED_10;
		sc->sc_link = 1;
		break;
	default:
		sc->sc_link = 0;
		return;
	}

	if (sc->sc_link == 0)
		return;

	basicctrl &= ~DWXE_BASIC_CTL0_DUPLEX;
	if ((sc->sc_mii.mii_media_active & IFM_GMASK) == IFM_FDX)
		basicctrl |= DWXE_BASIC_CTL0_DUPLEX;

	/* XXX: RX/TX flow control? */

	dwxe_write(sc, DWXE_BASIC_CTL0, basicctrl);
}

void
dwxe_tick(void *arg)
{
	struct dwxe_softc *sc = arg;
	int s;

	s = splnet();
	mii_tick(&sc->sc_mii);
	splx(s);

	timeout_add_sec(&sc->sc_tick, 1);
}

void
dwxe_rxtick(void *arg)
{
	struct dwxe_softc *sc = arg;
	uint32_t ctl;
	int s;

	s = splnet();

	ctl = dwxe_read(sc, DWXE_RX_CTL1);
	dwxe_write(sc, DWXE_RX_CTL1, ctl & ~DWXE_RX_CTL1_RX_DMA_EN);

	bus_dmamap_sync(sc->sc_dmat, DWXE_DMA_MAP(sc->sc_rxring),
	    0, DWXE_DMA_LEN(sc->sc_rxring),
	    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);

	dwxe_write(sc, DWXE_RX_DESC_LIST, 0);

	sc->sc_rx_prod = sc->sc_rx_cons = 0;
	dwxe_fill_rx_ring(sc);

	bus_dmamap_sync(sc->sc_dmat, DWXE_DMA_MAP(sc->sc_rxring),
	    0, DWXE_DMA_LEN(sc->sc_rxring),
	    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

	dwxe_write(sc, DWXE_RX_DESC_LIST, DWXE_DMA_DVA(sc->sc_rxring));
	dwxe_write(sc, DWXE_RX_CTL1, ctl);

	splx(s);
}

int
dwxe_intr(void *arg)
{
	struct dwxe_softc *sc = arg;
	uint32_t reg;

	reg = dwxe_read(sc, DWXE_INT_STA);
	dwxe_write(sc, DWXE_INT_STA, reg);

	if (reg & DWXE_INT_STA_RX_INT)
		dwxe_rx_proc(sc);

	if (reg & DWXE_INT_STA_TX_INT ||
	    reg & DWXE_INT_STA_TX_BUF_UA_INT)
		dwxe_tx_proc(sc);

	return (1);
}

void
dwxe_tx_proc(struct dwxe_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct dwxe_desc *txd;
	struct dwxe_buf *txb;
	int idx, txfree;

	bus_dmamap_sync(sc->sc_dmat, DWXE_DMA_MAP(sc->sc_txring), 0,
	    DWXE_DMA_LEN(sc->sc_txring),
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	txfree = 0;
	while (sc->sc_tx_cons != sc->sc_tx_prod) {
		idx = sc->sc_tx_cons;
		KASSERT(idx < DWXE_NTXDESC);

		txd = &sc->sc_txdesc[idx];
		if (txd->sd_status & DWXE_TX_DESC_CTL)
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

		if (sc->sc_tx_cons == (DWXE_NTXDESC - 1))
			sc->sc_tx_cons = 0;
		else
			sc->sc_tx_cons++;

		txd->sd_status = 0;
	}

	if (sc->sc_tx_cons == sc->sc_tx_prod)
		ifp->if_timer = 0;

	if (txfree) {
		if (ifq_is_oactive(&ifp->if_snd))
			ifq_restart(&ifp->if_snd);
	}
}

void
dwxe_rx_proc(struct dwxe_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct dwxe_desc *rxd;
	struct dwxe_buf *rxb;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *m;
	int idx, len, cnt, put;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	bus_dmamap_sync(sc->sc_dmat, DWXE_DMA_MAP(sc->sc_rxring), 0,
	    DWXE_DMA_LEN(sc->sc_rxring),
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	cnt = if_rxr_inuse(&sc->sc_rx_ring);
	put = 0;
	while (put < cnt) {
		idx = sc->sc_rx_cons;
		KASSERT(idx < DWXE_NRXDESC);

		rxd = &sc->sc_rxdesc[idx];
		if (rxd->sd_status & DWXE_RX_DESC_CTL)
			break;

		len = (rxd->sd_status >> DWXE_RX_FRM_LEN_SHIFT)
		    & DWXE_RX_FRM_LEN_MASK;
		rxb = &sc->sc_rxbuf[idx];
		KASSERT(rxb->tb_m);

		bus_dmamap_sync(sc->sc_dmat, rxb->tb_map, 0,
		    len, BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmat, rxb->tb_map);

		/* Strip off CRC. */
		len -= ETHER_CRC_LEN;
		KASSERT(len > 0);

		m = rxb->tb_m;
		rxb->tb_m = NULL;
		m->m_pkthdr.len = m->m_len = len;

		ml_enqueue(&ml, m);

		put++;
		if (sc->sc_rx_cons == (DWXE_NRXDESC - 1))
			sc->sc_rx_cons = 0;
		else
			sc->sc_rx_cons++;
	}

	if_rxr_put(&sc->sc_rx_ring, put);
	if (ifiq_input(&ifp->if_rcv, &ml))
		if_rxr_livelocked(&sc->sc_rx_ring);

	dwxe_fill_rx_ring(sc);

	bus_dmamap_sync(sc->sc_dmat, DWXE_DMA_MAP(sc->sc_rxring), 0,
	    DWXE_DMA_LEN(sc->sc_rxring),
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

void
dwxe_up(struct dwxe_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct dwxe_buf *txb, *rxb;
	int i;

	/* Allocate Tx descriptor ring. */
	sc->sc_txring = dwxe_dmamem_alloc(sc,
	    DWXE_NTXDESC * sizeof(struct dwxe_desc), 8);
	sc->sc_txdesc = DWXE_DMA_KVA(sc->sc_txring);

	sc->sc_txbuf = malloc(sizeof(struct dwxe_buf) * DWXE_NTXDESC,
	    M_DEVBUF, M_WAITOK);
	for (i = 0; i < DWXE_NTXDESC; i++) {
		txb = &sc->sc_txbuf[i];
		bus_dmamap_create(sc->sc_dmat, MCLBYTES, DWXE_NTXSEGS,
		    MCLBYTES, 0, BUS_DMA_WAITOK, &txb->tb_map);
		txb->tb_m = NULL;

		sc->sc_txdesc[i].sd_next =
		    DWXE_DMA_DVA(sc->sc_txring) +
		    ((i+1) % DWXE_NTXDESC) * sizeof(struct dwxe_desc);
	}

	bus_dmamap_sync(sc->sc_dmat, DWXE_DMA_MAP(sc->sc_txring),
	    0, DWXE_DMA_LEN(sc->sc_txring), BUS_DMASYNC_PREWRITE);

	sc->sc_tx_prod = sc->sc_tx_cons = 0;

	dwxe_write(sc, DWXE_TX_DESC_LIST, DWXE_DMA_DVA(sc->sc_txring));

	/* Allocate  descriptor ring. */
	sc->sc_rxring = dwxe_dmamem_alloc(sc,
	    DWXE_NRXDESC * sizeof(struct dwxe_desc), 8);
	sc->sc_rxdesc = DWXE_DMA_KVA(sc->sc_rxring);

	sc->sc_rxbuf = malloc(sizeof(struct dwxe_buf) * DWXE_NRXDESC,
	    M_DEVBUF, M_WAITOK);

	for (i = 0; i < DWXE_NRXDESC; i++) {
		rxb = &sc->sc_rxbuf[i];
		bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1,
		    MCLBYTES, 0, BUS_DMA_WAITOK, &rxb->tb_map);
		rxb->tb_m = NULL;

		sc->sc_rxdesc[i].sd_next =
		    DWXE_DMA_DVA(sc->sc_rxring) +
		    ((i+1) % DWXE_NRXDESC) * sizeof(struct dwxe_desc);
	}

	if_rxr_init(&sc->sc_rx_ring, 2, DWXE_NRXDESC);

	sc->sc_rx_prod = sc->sc_rx_cons = 0;
	dwxe_fill_rx_ring(sc);

	bus_dmamap_sync(sc->sc_dmat, DWXE_DMA_MAP(sc->sc_rxring),
	    0, DWXE_DMA_LEN(sc->sc_rxring),
	    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

	dwxe_write(sc, DWXE_RX_DESC_LIST, DWXE_DMA_DVA(sc->sc_rxring));

	dwxe_lladdr_write(sc);

	//dwxe_write(sc, DWXE_BASIC_CTL1, DWXE_BASIC_CTL1_SOFT_RST);

	/* Configure media. */
	if (LIST_FIRST(&sc->sc_mii.mii_phys))
		mii_mediachg(&sc->sc_mii);

	/* Program promiscuous mode and multicast filters. */
	dwxe_iff(sc);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	dwxe_write(sc, DWXE_INT_EN, DWXE_INT_EN_RX_INT |
	    DWXE_INT_EN_TX_INT | DWXE_INT_EN_TX_BUF_UA_INT);

	dwxe_write(sc, DWXE_TX_CTL1, dwxe_read(sc, DWXE_TX_CTL1) |
	    DWXE_TX_CTL1_TX_MD | DWXE_TX_CTL1_TX_NEXT_FRM |
	    DWXE_TX_CTL1_TX_DMA_EN);
	dwxe_write(sc, DWXE_RX_CTL1, dwxe_read(sc, DWXE_RX_CTL1) |
	    DWXE_RX_CTL1_RX_MD | DWXE_RX_CTL1_RX_DMA_EN);

	dwxe_write(sc, DWXE_TX_CTL0, dwxe_read(sc, DWXE_TX_CTL0) |
	    DWXE_TX_CTL0_TX_TRANSMITTER_EN);
	dwxe_write(sc, DWXE_RX_CTL0, dwxe_read(sc, DWXE_RX_CTL0) |
	    DWXE_RX_CTL0_RX_RECEIVER_EN | DWXE_RX_CTL0_RX_DO_CRC);

	timeout_add_sec(&sc->sc_tick, 1);
}

void
dwxe_down(struct dwxe_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct dwxe_buf *txb, *rxb;
	uint32_t dmactrl;
	int i;

	timeout_del(&sc->sc_rxto);
	timeout_del(&sc->sc_tick);

	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;

	dwxe_stop_dma(sc);

	dwxe_write(sc, DWXE_TX_CTL0, dwxe_read(sc,
	    DWXE_TX_CTL0) & ~DWXE_TX_CTL0_TX_TRANSMITTER_EN);

	dwxe_write(sc, DWXE_RX_CTL0, dwxe_read(sc,
	    DWXE_RX_CTL0) & ~DWXE_RX_CTL0_RX_RECEIVER_EN);

	dmactrl = dwxe_read(sc, DWXE_TX_CTL1);
	dmactrl &= ~DWXE_TX_CTL1_TX_DMA_EN;
	dwxe_write(sc, DWXE_TX_CTL1, dmactrl);

	dmactrl = dwxe_read(sc, DWXE_RX_CTL1);
	dmactrl &= ~DWXE_RX_CTL1_RX_DMA_EN;
	dwxe_write(sc, DWXE_RX_CTL1, dmactrl);

	dwxe_write(sc, DWXE_INT_EN, 0);

	intr_barrier(sc->sc_ih);
	ifq_barrier(&ifp->if_snd);

	for (i = 0; i < DWXE_NTXDESC; i++) {
		txb = &sc->sc_txbuf[i];
		if (txb->tb_m) {
			bus_dmamap_sync(sc->sc_dmat, txb->tb_map, 0,
			    txb->tb_map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, txb->tb_map);
			m_freem(txb->tb_m);
		}
		bus_dmamap_destroy(sc->sc_dmat, txb->tb_map);
	}

	dwxe_dmamem_free(sc, sc->sc_txring);
	free(sc->sc_txbuf, M_DEVBUF, 0);

	for (i = 0; i < DWXE_NRXDESC; i++) {
		rxb = &sc->sc_rxbuf[i];
		if (rxb->tb_m) {
			bus_dmamap_sync(sc->sc_dmat, rxb->tb_map, 0,
			    rxb->tb_map->dm_mapsize, BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->sc_dmat, rxb->tb_map);
			m_freem(rxb->tb_m);
		}
		bus_dmamap_destroy(sc->sc_dmat, rxb->tb_map);
	}

	dwxe_dmamem_free(sc, sc->sc_rxring);
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
dwxe_iff(struct dwxe_softc *sc)
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
		reg |= DWXE_RX_FRM_FLT_RX_ALL_MULTICAST;
		if (ifp->if_flags & IFF_PROMISC)
			reg |= DWXE_RX_FRM_FLT_DIS_ADDR_FILTER;
	} else {
		reg |= DWXE_RX_FRM_FLT_HASH_MULTICAST;
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

	dwxe_lladdr_write(sc);

	dwxe_write(sc, DWXE_RX_HASH0, hash[1]);
	dwxe_write(sc, DWXE_RX_HASH1, hash[0]);

	dwxe_write(sc, DWXE_RX_FRM_FLT, reg);
}

int
dwxe_encap(struct dwxe_softc *sc, struct mbuf *m, int *idx, int *used)
{
	struct dwxe_desc *txd, *txd_start;
	bus_dmamap_t map;
	int cur, frag, i;

	cur = frag = *idx;
	map = sc->sc_txbuf[cur].tb_map;

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
		txd->sd_len = map->dm_segs[i].ds_len;
		if (i == 0)
			txd->sd_len |= DWXE_TX_FIR_DESC;
		if (i == (map->dm_nsegs - 1))
			txd->sd_len |= DWXE_TX_LAST_DESC | DWXE_TX_INT_CTL;
		if (i != 0)
			txd->sd_status = DWXE_TX_DESC_CTL;

		bus_dmamap_sync(sc->sc_dmat, DWXE_DMA_MAP(sc->sc_txring),
		    frag * sizeof(*txd), sizeof(*txd), BUS_DMASYNC_PREWRITE);

		cur = frag;
		if (frag == (DWXE_NTXDESC - 1)) {
			txd = &sc->sc_txdesc[0];
			frag = 0;
		} else {
			txd++;
			frag++;
		}
		KASSERT(frag != sc->sc_tx_cons);
	}

	txd_start->sd_status = DWXE_TX_DESC_CTL;
	bus_dmamap_sync(sc->sc_dmat, DWXE_DMA_MAP(sc->sc_txring),
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
dwxe_reset(struct dwxe_softc *sc)
{
	int n;

	dwxe_stop_dma(sc);

	dwxe_write(sc, DWXE_BASIC_CTL1, DWXE_BASIC_CTL1_SOFT_RST);

	for (n = 0; n < 1000; n++) {
		if ((dwxe_read(sc, DWXE_BASIC_CTL1) &
		    DWXE_BASIC_CTL1_SOFT_RST) == 0)
			return;
		delay(10);
	}

	printf("%s: reset timeout\n", sc->sc_dev.dv_xname);
}

void
dwxe_stop_dma(struct dwxe_softc *sc)
{
	uint32_t dmactrl;

	/* Stop DMA. */
	dmactrl = dwxe_read(sc, DWXE_TX_CTL1);
	dmactrl &= ~DWXE_TX_CTL1_TX_DMA_EN;
	dmactrl |= DWXE_TX_CTL1_TX_FIFO_FLUSH;
	dwxe_write(sc, DWXE_TX_CTL1, dmactrl);
}

struct dwxe_dmamem *
dwxe_dmamem_alloc(struct dwxe_softc *sc, bus_size_t size, bus_size_t align)
{
	struct dwxe_dmamem *tdm;
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
dwxe_dmamem_free(struct dwxe_softc *sc, struct dwxe_dmamem *tdm)
{
	bus_dmamem_unmap(sc->sc_dmat, tdm->tdm_kva, tdm->tdm_size);
	bus_dmamem_free(sc->sc_dmat, &tdm->tdm_seg, 1);
	bus_dmamap_destroy(sc->sc_dmat, tdm->tdm_map);
	free(tdm, M_DEVBUF, 0);
}

struct mbuf *
dwxe_alloc_mbuf(struct dwxe_softc *sc, bus_dmamap_t map)
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
dwxe_fill_rx_ring(struct dwxe_softc *sc)
{
	struct dwxe_desc *rxd;
	struct dwxe_buf *rxb;
	u_int slots;

	for (slots = if_rxr_get(&sc->sc_rx_ring, DWXE_NRXDESC);
	    slots > 0; slots--) {
		rxb = &sc->sc_rxbuf[sc->sc_rx_prod];
		rxb->tb_m = dwxe_alloc_mbuf(sc, rxb->tb_map);
		if (rxb->tb_m == NULL)
			break;

		rxd = &sc->sc_rxdesc[sc->sc_rx_prod];
		rxd->sd_len = rxb->tb_map->dm_segs[0].ds_len - 1;
		rxd->sd_addr = rxb->tb_map->dm_segs[0].ds_addr;
		rxd->sd_status = DWXE_RX_DESC_CTL;

		if (sc->sc_rx_prod == (DWXE_NRXDESC - 1))
			sc->sc_rx_prod = 0;
		else
			sc->sc_rx_prod++;
	}
	if_rxr_put(&sc->sc_rx_ring, slots);

	if (if_rxr_inuse(&sc->sc_rx_ring) == 0)
		timeout_add(&sc->sc_rxto, 1);
}
