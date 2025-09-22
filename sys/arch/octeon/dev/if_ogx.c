/*	$OpenBSD: if_ogx.c,v 1.7 2024/05/20 23:13:33 jsg Exp $	*/

/*
 * Copyright (c) 2019-2020 Visa Hankala
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
 * Driver for OCTEON III network processor.
 */

#include "bpfilter.h"
#include "kstat.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/atomic.h>
#include <sys/mutex.h>
#include <sys/rwlock.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/kstat.h>
#include <sys/socket.h>
#include <sys/stdint.h>

#include <net/if.h>
#include <net/if_media.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#ifdef INET6
#include <netinet/ip6.h>
#endif

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/ofw/fdt.h>
#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/octeonvar.h>
#include <machine/octeon_model.h>

#include <octeon/dev/cn30xxsmivar.h>
#include <octeon/dev/ogxreg.h>
#include <octeon/dev/ogxvar.h>

struct ogx_link_ops;

struct ogx_softc {
	struct device		 sc_dev;
	struct arpcom		 sc_ac;
	unsigned int		 sc_bgxid;
	unsigned int		 sc_lmacid;
	unsigned int		 sc_ipdport;
	unsigned int		 sc_pkomac;
	unsigned int		 sc_rxused;
	unsigned int		 sc_txfree;

	struct ogx_node		*sc_node;
	unsigned int		 sc_unit;	/* logical unit within node */

	struct mii_data		 sc_mii;
#define sc_media	sc_mii.mii_media
	struct timeout		 sc_tick;
	struct cn30xxsmi_softc	*sc_smi;

	struct timeout		 sc_rxrefill;
	void			*sc_rx_ih;
	void			*sc_tx_ih;

	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_port_ioh;
	bus_space_handle_t	 sc_nexus_ioh;

	struct fpa3aura		 sc_pkt_aura;
	const struct ogx_link_ops *sc_link_ops;
	uint8_t			 sc_link_duplex;

	struct mutex		 sc_kstat_mtx;
	struct timeout		 sc_kstat_tmo;
	struct kstat		*sc_kstat;
	uint64_t		*sc_counter_vals;
	bus_space_handle_t	 sc_pki_stat_ioh;
};

#define DEVNAME(sc)		((sc)->sc_dev.dv_xname)

#define L1_QUEUE(sc)		((sc)->sc_unit)
#define L2_QUEUE(sc)		((sc)->sc_unit)
#define L3_QUEUE(sc)		((sc)->sc_unit)
#define L4_QUEUE(sc)		((sc)->sc_unit)
#define L5_QUEUE(sc)		((sc)->sc_unit)
#define DESC_QUEUE(sc)		((sc)->sc_unit)

#define PORT_FIFO(sc)		((sc)->sc_unit)		/* PKO FIFO */
#define PORT_GROUP_RX(sc)	((sc)->sc_unit * 2)	/* SSO group for Rx */
#define PORT_GROUP_TX(sc)	((sc)->sc_unit * 2 + 1)	/* SSO group for Tx */
#define PORT_MAC(sc)		((sc)->sc_pkomac)
#define PORT_PKIND(sc)		((sc)->sc_unit)
#define PORT_QPG(sc)		((sc)->sc_unit)
#define PORT_STYLE(sc)		((sc)->sc_unit)

struct ogx_link_ops {
	const char	*link_type;
	unsigned int	 link_fifo_speed;	/* in Mbps */
	/* Initialize link. */
	int		(*link_init)(struct ogx_softc *);
	/* Deinitialize link. */
	void		(*link_down)(struct ogx_softc *);
	/* Change link parameters. */
	void		(*link_change)(struct ogx_softc *);
	/* Query link status. Returns non-zero if status has changed. */
	int		(*link_status)(struct ogx_softc *);
};

struct ogx_fifo_group {
	unsigned int		fg_inited;
	unsigned int		fg_speed;
};

struct ogx_config {
	unsigned int		cfg_nclusters;	/* number of parsing clusters */
	unsigned int		cfg_nfifogrps;	/* number of FIFO groups */
	unsigned int		cfg_nmacs;	/* number of MACs */
	unsigned int		cfg_npqs;	/* number of port queues */
	unsigned int		cfg_npkolvl;	/* number of PKO Lx levels */
	unsigned int		cfg_nullmac;	/* index of NULL MAC */
};

struct ogx_node {
	bus_dma_tag_t		 node_dmat;
	bus_space_tag_t		 node_iot;
	bus_space_handle_t	 node_fpa3;
	bus_space_handle_t	 node_pki;
	bus_space_handle_t	 node_pko3;
	bus_space_handle_t	 node_sso;

	struct fpa3pool		 node_pko_pool;
	struct fpa3pool		 node_pkt_pool;
	struct fpa3pool		 node_sso_pool;
	struct fpa3aura		 node_pko_aura;
	struct fpa3aura		 node_sso_aura;

	uint64_t		 node_id;
	unsigned int		 node_nclusters;
	unsigned int		 node_nunits;
	struct ogx_fifo_group	 node_fifogrp[8];
	const struct ogx_config	*node_cfg;

	struct rwlock		 node_lock;
	unsigned int		 node_flags;
#define NODE_INITED			0x01	/* node initialized */
#define NODE_FWREADY			0x02	/* node firmware ready */
};

struct ogx_fwhdr {
	char		fw_version[8];
	uint64_t	fw_size;
};

#define BGX_PORT_SIZE	0x100000

#define PORT_RD_8(sc, reg) \
	bus_space_read_8((sc)->sc_iot, (sc)->sc_port_ioh, (reg))
#define PORT_WR_8(sc, reg, val) \
	bus_space_write_8((sc)->sc_iot, (sc)->sc_port_ioh, (reg), (val))

#define NEXUS_RD_8(sc, reg) \
	bus_space_read_8((sc)->sc_iot, (sc)->sc_nexus_ioh, (reg))
#define NEXUS_WR_8(sc, reg, val) \
	bus_space_write_8((sc)->sc_iot, (sc)->sc_nexus_ioh, (reg), (val))

#define FPA3_RD_8(node, reg) \
	bus_space_read_8((node)->node_iot, (node)->node_fpa3, (reg))
#define FPA3_WR_8(node, reg, val) \
	bus_space_write_8((node)->node_iot, (node)->node_fpa3, (reg), (val))
#define PKI_RD_8(node, reg) \
	bus_space_read_8((node)->node_iot, (node)->node_pki, (reg))
#define PKI_WR_8(node, reg, val) \
	bus_space_write_8((node)->node_iot, (node)->node_pki, (reg), (val))
#define PKO3_RD_8(node, reg) \
	bus_space_read_8((node)->node_iot, (node)->node_pko3, (reg))
#define PKO3_WR_8(node, reg, val) \
	bus_space_write_8((node)->node_iot, (node)->node_pko3, (reg), (val))
#define SSO_RD_8(node, reg) \
	bus_space_read_8((node)->node_iot, (node)->node_sso, (reg))
#define SSO_WR_8(node, reg, val) \
	bus_space_write_8((node)->node_iot, (node)->node_sso, (reg), (val))

int	ogx_match(struct device *, void *, void *);
void	ogx_attach(struct device *, struct device *, void *);
void	ogx_defer(struct device *);

int	ogx_ioctl(struct ifnet *, u_long, caddr_t);
void	ogx_start(struct ifqueue *);
int	ogx_send_mbuf(struct ogx_softc *, struct mbuf *);
u_int	ogx_load_mbufs(struct ogx_softc *, unsigned int);
u_int	ogx_unload_mbufs(struct ogx_softc *);

void	ogx_media_status(struct ifnet *, struct ifmediareq *);
int	ogx_media_change(struct ifnet *);
int	ogx_mii_readreg(struct device *, int, int);
void	ogx_mii_writereg(struct device *, int, int, int);
void	ogx_mii_statchg(struct device *);

int	ogx_init(struct ogx_softc *);
void	ogx_down(struct ogx_softc *);
void	ogx_iff(struct ogx_softc *);
void	ogx_rxrefill(void *);
int	ogx_rxintr(void *);
int	ogx_txintr(void *);
void	ogx_tick(void *);

#if NKSTAT > 0
#define OGX_KSTAT_TICK_SECS	600
void	ogx_kstat_attach(struct ogx_softc *);
int	ogx_kstat_read(struct kstat *);
void	ogx_kstat_start(struct ogx_softc *);
void	ogx_kstat_stop(struct ogx_softc *);
void	ogx_kstat_tick(void *);
#endif

int	ogx_node_init(struct ogx_node **, bus_dma_tag_t, bus_space_tag_t);
int	ogx_node_load_firmware(struct ogx_node *);
void	ogx_fpa3_aura_init(struct ogx_node *, struct fpa3aura *, uint32_t,
	    struct fpa3pool *);
void	ogx_fpa3_aura_load(struct ogx_node *, struct fpa3aura *, size_t,
	    size_t);
paddr_t	ogx_fpa3_alloc(struct fpa3aura *);
void	ogx_fpa3_free(struct fpa3aura *, paddr_t);
void	ogx_fpa3_pool_init(struct ogx_node *, struct fpa3pool *, uint32_t,
	    uint32_t);

int	ogx_sgmii_link_init(struct ogx_softc *);
void	ogx_sgmii_link_down(struct ogx_softc *);
void	ogx_sgmii_link_change(struct ogx_softc *);

static inline paddr_t
ogx_kvtophys(vaddr_t kva)
{
	KASSERT(IS_XKPHYS(kva));
	return XKPHYS_TO_PHYS(kva);
}
#define KVTOPHYS(addr)	ogx_kvtophys((vaddr_t)(addr))

const struct cfattach ogx_ca = {
	sizeof(struct ogx_softc), ogx_match, ogx_attach
};

struct cfdriver ogx_cd = {
	NULL, "ogx", DV_IFNET
};

const struct ogx_config ogx_cn73xx_config = {
	.cfg_nclusters		= 2,
	.cfg_nfifogrps		= 4,
	.cfg_nmacs		= 14,
	.cfg_npqs		= 16,
	.cfg_npkolvl		= 3,
	.cfg_nullmac		= 15,
};

const struct ogx_config ogx_cn78xx_config = {
	.cfg_nclusters		= 4,
	.cfg_nfifogrps		= 8,
	.cfg_nmacs		= 28,
	.cfg_npqs		= 32,
	.cfg_npkolvl		= 5,
	.cfg_nullmac		= 28,
};

const struct ogx_link_ops ogx_sgmii_link_ops = {
	.link_type		= "SGMII",
	.link_fifo_speed	= 1000,
	.link_init		= ogx_sgmii_link_init,
	.link_down		= ogx_sgmii_link_down,
	.link_change		= ogx_sgmii_link_change,
};

const struct ogx_link_ops ogx_xfi_link_ops = {
	.link_type		= "XFI",
	.link_fifo_speed	= 10000,
};

#define BELTYPE_NONE	0x00
#define BELTYPE_MISC	0x01
#define BELTYPE_IPv4	0x02
#define BELTYPE_IPv6	0x03
#define BELTYPE_TCP	0x04
#define BELTYPE_UDP	0x05

static const unsigned int ogx_ltypes[] = {
	BELTYPE_NONE,	/* 0x00 */
	BELTYPE_MISC,	/* 0x01 Ethernet */
	BELTYPE_MISC,	/* 0x02 VLAN */
	BELTYPE_NONE,	/* 0x03 */
	BELTYPE_NONE,	/* 0x04 */
	BELTYPE_MISC,	/* 0x05 SNAP */
	BELTYPE_MISC,	/* 0x06 ARP */
	BELTYPE_MISC,	/* 0x07 RARP */
	BELTYPE_IPv4,	/* 0x08 IPv4 */
	BELTYPE_IPv4,	/* 0x09 IPv4 options */
	BELTYPE_IPv6,	/* 0x0a IPv6 */
	BELTYPE_IPv6,	/* 0x0b IPv6 options */
	BELTYPE_MISC,	/* 0x0c ESP */
	BELTYPE_MISC,	/* 0x0d IP fragment */
	BELTYPE_MISC,	/* 0x0e IPcomp */
	BELTYPE_NONE,	/* 0x0f */
	BELTYPE_TCP,	/* 0x10 TCP */
	BELTYPE_UDP,	/* 0x11 UDP */
	BELTYPE_MISC,	/* 0x12 SCTP */
	BELTYPE_UDP,	/* 0x13 UDP VXLAN */
	BELTYPE_MISC,	/* 0x14 GRE */
	BELTYPE_MISC,	/* 0x15 NVGRE */
	BELTYPE_MISC,	/* 0x16 GTP */
	BELTYPE_UDP,	/* 0x17 UDP Geneve */
	BELTYPE_NONE,	/* 0x18 */
	BELTYPE_NONE,	/* 0x19 */
	BELTYPE_NONE,	/* 0x1a */
	BELTYPE_NONE,	/* 0x1b */
	BELTYPE_MISC,	/* 0x1c software */
	BELTYPE_MISC,	/* 0x1d software */
	BELTYPE_MISC,	/* 0x1e software */
	BELTYPE_MISC	/* 0x1f software */
};

#define OGX_POOL_SSO		0
#define OGX_POOL_PKO		1
#define OGX_POOL_PKT		2

#define OGX_AURA_SSO		0
#define OGX_AURA_PKO		1
#define OGX_AURA_PKT(sc)	((sc)->sc_unit + 2)

struct ogx_node	ogx_node;

int
ogx_match(struct device *parent, void *match, void *aux)
{
	return 1;
}

void
ogx_attach(struct device *parent, struct device *self, void *aux)
{
	const struct ogx_config *cfg;
	struct ogx_fifo_group *fifogrp;
	struct ogx_node *node;
	struct ogx_attach_args *oaa = aux;
	struct ogx_softc *sc = (struct ogx_softc *)self;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	uint64_t lmac_type, lut_index, val;
	uint32_t lmac;
	int fgindex = PORT_FIFO(sc) >> 2;
	int cl, phy_addr, phy_handle;

	if (ogx_node_init(&node, oaa->oaa_dmat, oaa->oaa_iot)) {
		printf(": node init failed\n");
		return;
	}
	cfg = node->node_cfg;

	sc->sc_node = node;
	sc->sc_unit = node->node_nunits++;

	phy_handle = OF_getpropint(oaa->oaa_node, "phy-handle", 0);
	if (phy_handle == 0) {
		printf(": no phy-handle\n");
		return;
	}
	if (cn30xxsmi_get_phy(phy_handle, 0, &sc->sc_smi, &phy_addr)) {
		printf(": no phy found\n");
		return;
	}

	lmac = OF_getpropint(oaa->oaa_node, "reg", UINT32_MAX);
	if (lmac == UINT32_MAX) {
		printf(": no reg property\n");
		return;
	}

	sc->sc_bgxid = oaa->oaa_bgxid;
	sc->sc_lmacid = lmac;
	sc->sc_ipdport = sc->sc_bgxid * 0x100 + lmac * 0x10 + 0x800;
	sc->sc_pkomac = sc->sc_bgxid * 4 + lmac + 2;

	if (OF_getproplen(oaa->oaa_node, "local-mac-address") !=
	    ETHER_ADDR_LEN) {
		printf(": no MAC address\n");
		return;
	}
	OF_getprop(oaa->oaa_node, "local-mac-address", sc->sc_ac.ac_enaddr,
	    ETHER_ADDR_LEN);

	sc->sc_iot = oaa->oaa_iot;
	sc->sc_nexus_ioh = oaa->oaa_ioh;
	if (bus_space_subregion(sc->sc_iot, oaa->oaa_ioh,
	    sc->sc_lmacid * BGX_PORT_SIZE, BGX_PORT_SIZE, &sc->sc_port_ioh)) {
		printf(": can't map IO subregion\n");
		return;
	}

	val = PORT_RD_8(sc, BGX_CMR_RX_ID_MAP);
	val &= ~BGX_CMR_RX_ID_MAP_RID_M;
	val &= ~BGX_CMR_RX_ID_MAP_PKND_M;
	val |= (uint64_t)(sc->sc_bgxid * 4 + 2 + sc->sc_lmacid) <<
	    BGX_CMR_RX_ID_MAP_RID_S;
	val |= (uint64_t)PORT_PKIND(sc) << BGX_CMR_RX_ID_MAP_PKND_S;
	PORT_WR_8(sc, BGX_CMR_RX_ID_MAP, val);

	val = PORT_RD_8(sc, BGX_CMR_CHAN_MSK_AND);
	val |= 0xffffULL << (sc->sc_lmacid * 16);
	PORT_WR_8(sc, BGX_CMR_CHAN_MSK_AND, val);

	val = PORT_RD_8(sc, BGX_CMR_CHAN_MSK_OR);
	val |= 0xffffULL << (sc->sc_lmacid * 16);
	PORT_WR_8(sc, BGX_CMR_CHAN_MSK_OR, val);

	sc->sc_rx_ih = octeon_intr_establish(0x61000 | PORT_GROUP_RX(sc),
	    IPL_NET | IPL_MPSAFE, ogx_rxintr, sc, DEVNAME(sc));
	if (sc->sc_rx_ih == NULL) {
		printf(": could not establish Rx interrupt\n");
		return;
	}
	sc->sc_tx_ih = octeon_intr_establish(0x61000 | PORT_GROUP_TX(sc),
	    IPL_NET | IPL_MPSAFE, ogx_txintr, sc, DEVNAME(sc));
	if (sc->sc_tx_ih == NULL) {
		printf(": could not establish Tx interrupt\n");
		return;
	}

	val = PORT_RD_8(sc, BGX_CMR_CONFIG);
	lmac_type = (val & BGX_CMR_CONFIG_LMAC_TYPE_M) >>
	    BGX_CMR_CONFIG_LMAC_TYPE_S;
	switch (lmac_type) {
	case 0:
		sc->sc_link_ops = &ogx_sgmii_link_ops;
		break;
	default:
		printf(": unhandled LMAC type %llu\n", lmac_type);
		return;
	}
	printf(": %s", sc->sc_link_ops->link_type);

	printf(", address %s", ether_sprintf(sc->sc_ac.ac_enaddr));

	ogx_fpa3_aura_init(node, &sc->sc_pkt_aura, OGX_AURA_PKT(sc),
	    &node->node_pkt_pool);

	sc->sc_rxused = 128;
	sc->sc_txfree = 128;

	timeout_set(&sc->sc_rxrefill, ogx_rxrefill, sc);
	timeout_set(&sc->sc_tick, ogx_tick, sc);

	printf("\n");

	strlcpy(ifp->if_xname, DEVNAME(sc), IFNAMSIZ);
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_MULTICAST;
	ifp->if_xflags |= IFXF_MPSAFE;
	ifp->if_ioctl = ogx_ioctl;
	ifp->if_qstart = ogx_start;
	ifp->if_capabilities = IFCAP_CSUM_IPv4 |
	    IFCAP_CSUM_TCPv4 | IFCAP_CSUM_UDPv4 |
	    IFCAP_CSUM_TCPv6 | IFCAP_CSUM_UDPv6;

	sc->sc_mii.mii_ifp = ifp;
	sc->sc_mii.mii_readreg = ogx_mii_readreg;
	sc->sc_mii.mii_writereg = ogx_mii_writereg;
	sc->sc_mii.mii_statchg = ogx_mii_statchg;
	ifmedia_init(&sc->sc_media, 0, ogx_media_change, ogx_media_status);

	mii_attach(&sc->sc_dev, &sc->sc_mii, 0xffffffff, phy_addr,
	    MII_OFFSET_ANY, MIIF_NOISOLATE);
	if (LIST_EMPTY(&sc->sc_mii.mii_phys)) {
		printf("%s: no PHY found\n", DEVNAME(sc));
		ifmedia_add(&sc->sc_media, IFM_ETHER | IFM_MANUAL, 0, NULL);
		ifmedia_set(&sc->sc_media, IFM_ETHER | IFM_MANUAL);
	} else {
		ifmedia_set(&sc->sc_media, IFM_ETHER | IFM_AUTO);

		timeout_add_sec(&sc->sc_tick, 1);
	}

	/*
	 * Set up the PKI for this port.
	 */

	val = (uint64_t)PORT_GROUP_RX(sc) << PKI_QPG_TBL_GRP_OK_S;
	val |= (uint64_t)PORT_GROUP_RX(sc) << PKI_QPG_TBL_GRP_BAD_S;
	val |= OGX_AURA_PKT(sc) << PKI_QPG_TBL_LAURA_S;
	PKI_WR_8(node, PKI_QPG_TBL(PORT_QPG(sc)), val);

	for (cl = 0; cl < cfg->cfg_nclusters; cl++) {
		val = (uint64_t)PORT_QPG(sc) << PKI_CL_STYLE_CFG_QPG_BASE_S;
		PKI_WR_8(node, PKI_CL_STYLE_CFG(cl, PORT_STYLE(sc)), val);
		PKI_WR_8(node, PKI_CL_STYLE_CFG2(cl, PORT_STYLE(sc)), 0);
		PKI_WR_8(node, PKI_CL_STYLE_ALG(cl, PORT_STYLE(sc)), 1u << 31);

		val = PKI_RD_8(node, PKI_CL_PKIND_STYLE(cl, PORT_PKIND(sc)));
		val &= ~PKI_CL_PKIND_STYLE_PM_M;
		val &= ~PKI_CL_PKIND_STYLE_STYLE_M;
		val |= PORT_STYLE(sc) << PKI_CL_PKIND_STYLE_STYLE_S;
		PKI_WR_8(node, PKI_CL_PKIND_STYLE(cl, PORT_PKIND(sc)), val);
	}

	val = 5ULL << PKI_STYLE_BUF_FIRST_SKIP_S;
	val |= ((MCLBYTES - CACHELINESIZE) / sizeof(uint64_t)) <<
	    PKI_STYLE_BUF_MB_SIZE_S;
	PKI_WR_8(node, PKI_STYLE_BUF(PORT_STYLE(sc)), val);

	/*
	 * Set up output queues from the descriptor queue to the port queue.
	 *
	 * The hardware implements a multilevel hierarchy of queues
	 * with configurable priorities.
	 * This driver uses a simple topology where there is one queue
	 * on each level.
	 *
	 * CN73xx: DQ ->             L3 -> L2 -> port
	 * CN78xx: DQ -> L5 -> L4 -> L3 -> L2 -> port
	 */

	/* Map channel to queue L2. */
	val = PKO3_RD_8(node, PKO3_L3_L2_SQ_CHANNEL(L2_QUEUE(sc)));
	val &= ~PKO3_L3_L2_SQ_CHANNEL_CC_ENABLE;
	val &= ~PKO3_L3_L2_SQ_CHANNEL_M;
	val |= (uint64_t)sc->sc_ipdport << PKO3_L3_L2_SQ_CHANNEL_S;
	PKO3_WR_8(node, PKO3_L3_L2_SQ_CHANNEL(L2_QUEUE(sc)), val);

	val = PKO3_RD_8(node, PKO3_MAC_CFG(PORT_MAC(sc)));
	val &= ~PKO3_MAC_CFG_MIN_PAD_ENA;
	val &= ~PKO3_MAC_CFG_FCS_ENA;
	val &= ~PKO3_MAC_CFG_FCS_SOP_OFF_M;
	val &= ~PKO3_MAC_CFG_FIFO_NUM_M;
	val |= PORT_FIFO(sc) << PKO3_MAC_CFG_FIFO_NUM_S;
	PKO3_WR_8(node, PKO3_MAC_CFG(PORT_MAC(sc)), val);

	val = PKO3_RD_8(node, PKO3_MAC_CFG(PORT_MAC(sc)));
	val &= ~PKO3_MAC_CFG_SKID_MAX_CNT_M;
	PKO3_WR_8(node, PKO3_MAC_CFG(PORT_MAC(sc)), val);

	PKO3_WR_8(node, PKO3_MCI0_MAX_CRED(PORT_MAC(sc)), 0);
	PKO3_WR_8(node, PKO3_MCI1_MAX_CRED(PORT_MAC(sc)), 2560 / 16);

	/* Map the port queue to the MAC. */

	val = (uint64_t)PORT_MAC(sc) << PKO3_L1_SQ_TOPOLOGY_LINK_S;
	PKO3_WR_8(node, PKO3_L1_SQ_TOPOLOGY(L1_QUEUE(sc)), val);

	val = (uint64_t)PORT_MAC(sc) << PKO3_L1_SQ_SHAPE_LINK_S;
	PKO3_WR_8(node, PKO3_L1_SQ_SHAPE(L1_QUEUE(sc)), val);

	val = (uint64_t)PORT_MAC(sc) << PKO3_L1_SQ_LINK_LINK_S;
	PKO3_WR_8(node, PKO3_L1_SQ_LINK(L1_QUEUE(sc)), val);

	/* L1 / port queue */

	val = (uint64_t)0x10 << PKO3_LX_SQ_SCHEDULE_RR_QUANTUM_S;
	PKO3_WR_8(node, PKO3_L1_SQ_SCHEDULE(L1_QUEUE(sc)), val);

	val = PKO3_RD_8(node, PKO3_L1_SQ_TOPOLOGY(L1_QUEUE(sc)));
	val &= ~PKO3_L1_SQ_TOPOLOGY_PRIO_ANCHOR_M;
	val &= ~PKO3_L1_SQ_TOPOLOGY_RR_PRIO_M;
	val |= (uint64_t)L2_QUEUE(sc) << PKO3_L1_SQ_TOPOLOGY_PRIO_ANCHOR_S;
	val |= (uint64_t)0xf << PKO3_L1_SQ_TOPOLOGY_RR_PRIO_S;
	PKO3_WR_8(node, PKO3_L1_SQ_TOPOLOGY(L1_QUEUE(sc)), val);

	/* L2 */

	val = (uint64_t)0x10 << PKO3_LX_SQ_SCHEDULE_RR_QUANTUM_S;
	PKO3_WR_8(node, PKO3_L2_SQ_SCHEDULE(L2_QUEUE(sc)), val);

	val = PKO3_RD_8(node, PKO3_L2_SQ_TOPOLOGY(L2_QUEUE(sc)));
	val &= ~PKO3_L2_SQ_TOPOLOGY_PRIO_ANCHOR_M;
	val &= ~PKO3_L2_SQ_TOPOLOGY_PARENT_M;
	val &= ~PKO3_L2_SQ_TOPOLOGY_RR_PRIO_M;
	val |= (uint64_t)L3_QUEUE(sc) << PKO3_L2_SQ_TOPOLOGY_PRIO_ANCHOR_S;
	val |= (uint64_t)L1_QUEUE(sc) << PKO3_L2_SQ_TOPOLOGY_PARENT_S;
	val |= (uint64_t)0xf << PKO3_L2_SQ_TOPOLOGY_RR_PRIO_S;
	PKO3_WR_8(node, PKO3_L2_SQ_TOPOLOGY(L2_QUEUE(sc)), val);

	switch (cfg->cfg_npkolvl) {
	case 3:
		/* L3 */

		val = (uint64_t)0x10 << PKO3_LX_SQ_SCHEDULE_RR_QUANTUM_S;
		PKO3_WR_8(node, PKO3_L3_SQ_SCHEDULE(L3_QUEUE(sc)), val);

		val = PKO3_RD_8(node, PKO3_L3_SQ_TOPOLOGY(L3_QUEUE(sc)));
		val &= ~PKO3_L3_SQ_TOPOLOGY_PRIO_ANCHOR_M;
		val &= ~PKO3_L3_SQ_TOPOLOGY_PARENT_M;
		val &= ~PKO3_L3_SQ_TOPOLOGY_RR_PRIO_M;
		val |= (uint64_t)DESC_QUEUE(sc) <<
		    PKO3_L3_SQ_TOPOLOGY_PRIO_ANCHOR_S;
		val |= (uint64_t)L2_QUEUE(sc) << PKO3_L3_SQ_TOPOLOGY_PARENT_S;
		val |= (uint64_t)0xf << PKO3_L3_SQ_TOPOLOGY_RR_PRIO_S;
		PKO3_WR_8(node, PKO3_L3_SQ_TOPOLOGY(L3_QUEUE(sc)), val);

		/* Descriptor queue */

		val = (uint64_t)0x10 << PKO3_LX_SQ_SCHEDULE_RR_QUANTUM_S;
		PKO3_WR_8(node, PKO3_DQ_SCHEDULE(DESC_QUEUE(sc)), val);

		val = (uint64_t)L3_QUEUE(sc) << PKO3_DQ_TOPOLOGY_PARENT_S;
		PKO3_WR_8(node, PKO3_DQ_TOPOLOGY(DESC_QUEUE(sc)), val);

		break;

	case 5:
		/* L3 */

		val = (uint64_t)0x10 << PKO3_LX_SQ_SCHEDULE_RR_QUANTUM_S;
		PKO3_WR_8(node, PKO3_L3_SQ_SCHEDULE(L3_QUEUE(sc)), val);

		val = PKO3_RD_8(node, PKO3_L3_SQ_TOPOLOGY(L3_QUEUE(sc)));
		val &= ~PKO3_L3_SQ_TOPOLOGY_PRIO_ANCHOR_M;
		val &= ~PKO3_L3_SQ_TOPOLOGY_PARENT_M;
		val &= ~PKO3_L3_SQ_TOPOLOGY_RR_PRIO_M;
		val |= (uint64_t)L4_QUEUE(sc) <<
		    PKO3_L3_SQ_TOPOLOGY_PRIO_ANCHOR_S;
		val |= (uint64_t)L2_QUEUE(sc) << PKO3_L3_SQ_TOPOLOGY_PARENT_S;
		val |= (uint64_t)0xf << PKO3_L3_SQ_TOPOLOGY_RR_PRIO_S;
		PKO3_WR_8(node, PKO3_L3_SQ_TOPOLOGY(L3_QUEUE(sc)), val);

		/* L4 */

		val = (uint64_t)0x10 << PKO3_LX_SQ_SCHEDULE_RR_QUANTUM_S;
		PKO3_WR_8(node, PKO3_L4_SQ_SCHEDULE(L4_QUEUE(sc)), val);

		val = PKO3_RD_8(node, PKO3_L4_SQ_TOPOLOGY(L4_QUEUE(sc)));
		val &= ~PKO3_L4_SQ_TOPOLOGY_PRIO_ANCHOR_M;
		val &= ~PKO3_L4_SQ_TOPOLOGY_PARENT_M;
		val &= ~PKO3_L4_SQ_TOPOLOGY_RR_PRIO_M;
		val |= (uint64_t)L5_QUEUE(sc) <<
		    PKO3_L4_SQ_TOPOLOGY_PRIO_ANCHOR_S;
		val |= (uint64_t)L3_QUEUE(sc) << PKO3_L4_SQ_TOPOLOGY_PARENT_S;
		val |= (uint64_t)0xf << PKO3_L4_SQ_TOPOLOGY_RR_PRIO_S;
		PKO3_WR_8(node, PKO3_L4_SQ_TOPOLOGY(L4_QUEUE(sc)), val);

		/* L5 */

		val = (uint64_t)0x10 << PKO3_LX_SQ_SCHEDULE_RR_QUANTUM_S;
		PKO3_WR_8(node, PKO3_L5_SQ_SCHEDULE(L5_QUEUE(sc)), val);

		val = PKO3_RD_8(node, PKO3_L5_SQ_TOPOLOGY(L5_QUEUE(sc)));
		val &= ~PKO3_L5_SQ_TOPOLOGY_PRIO_ANCHOR_M;
		val &= ~PKO3_L5_SQ_TOPOLOGY_PARENT_M;
		val &= ~PKO3_L5_SQ_TOPOLOGY_RR_PRIO_M;
		val |= (uint64_t)DESC_QUEUE(sc) <<
		    PKO3_L5_SQ_TOPOLOGY_PRIO_ANCHOR_S;
		val |= (uint64_t)L4_QUEUE(sc) << PKO3_L5_SQ_TOPOLOGY_PARENT_S;
		val |= (uint64_t)0xf << PKO3_L5_SQ_TOPOLOGY_RR_PRIO_S;
		PKO3_WR_8(node, PKO3_L5_SQ_TOPOLOGY(L5_QUEUE(sc)), val);

		/* Descriptor queue */

		val = (uint64_t)0x10 << PKO3_LX_SQ_SCHEDULE_RR_QUANTUM_S;
		PKO3_WR_8(node, PKO3_DQ_SCHEDULE(DESC_QUEUE(sc)), val);

		val = (uint64_t)L5_QUEUE(sc) << PKO3_DQ_TOPOLOGY_PARENT_S;
		PKO3_WR_8(node, PKO3_DQ_TOPOLOGY(DESC_QUEUE(sc)), val);

		break;

	default:
		printf(": unhandled number of PKO levels (%u)\n",
		    cfg->cfg_npkolvl);
		return;
	}

	/* Descriptor queue, common part */

	PKO3_WR_8(node, PKO3_DQ_WM_CTL(DESC_QUEUE(sc)), PKO3_DQ_WM_CTL_KIND);

	val = PKO3_RD_8(node, PKO3_PDM_DQ_MINPAD(DESC_QUEUE(sc)));
	val &= ~PKO3_PDM_DQ_MINPAD_MINPAD;
	PKO3_WR_8(node, PKO3_PDM_DQ_MINPAD(DESC_QUEUE(sc)), val);

	lut_index = sc->sc_bgxid * 0x40 + lmac * 0x10;
	val = PKO3_LUT_VALID | (L1_QUEUE(sc) << PKO3_LUT_PQ_IDX_S) |
	    (L2_QUEUE(sc) << PKO3_LUT_QUEUE_NUM_S);
	PKO3_WR_8(node, PKO3_LUT(lut_index), val);

#if NKSTAT > 0
	ogx_kstat_attach(sc);
#endif

	fifogrp = &node->node_fifogrp[fgindex];
	fifogrp->fg_speed += sc->sc_link_ops->link_fifo_speed;

	/*
	 * Defer the rest of the initialization so that FIFO groups
	 * can be configured properly.
	 */
	config_defer(&sc->sc_dev, ogx_defer);
}

void
ogx_defer(struct device *dev)
{
	struct ogx_fifo_group *fifogrp;
	struct ogx_softc *sc = (struct ogx_softc *)dev;
	struct ogx_node *node = sc->sc_node;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	uint64_t grprate, val;
	int fgindex = PORT_FIFO(sc) >> 2;

	fifogrp = &node->node_fifogrp[fgindex];
	if (fifogrp->fg_inited == 0) {
		/* Adjust the total rate of the fifo group. */
		grprate = 0;
		while (fifogrp->fg_speed > (6250 << grprate))
			grprate++;
		if (grprate > 5)
			grprate = 5;

		val = PKO3_RD_8(node, PKO3_PTGF_CFG(fgindex));
		val &= ~PKO3_PTGF_CFG_RATE_M;
		val |= grprate << PKO3_PTGF_CFG_RATE_S;
		PKO3_WR_8(node, PKO3_PTGF_CFG(fgindex), val);

		fifogrp->fg_inited = 1;
	}

	if_attach(ifp);
	ether_ifattach(ifp);
}

int
ogx_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct ogx_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int error = 0;
	int s;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		/* FALLTHROUGH */

	case SIOCSIFFLAGS:
		if (ISSET(ifp->if_flags, IFF_UP)) {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				error = ENETRESET;
			else
				error = ogx_init(sc);
		} else {
			if (ISSET(ifp->if_flags, IFF_RUNNING))
				ogx_down(sc);
		}
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_mii.mii_media, cmd);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_ac, cmd, data);
		break;
	}

	if (error == ENETRESET) {
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING))
			ogx_iff(sc);
		error = 0;
	}

	splx(s);

	return error;
}

int
ogx_init(struct ogx_softc *sc)
{
	struct ogx_node *node = sc->sc_node;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	uint64_t op;
	int error;

	error = ogx_node_load_firmware(node);
	if (error != 0)
		return error;

#if NKSTAT > 0
	ogx_kstat_start(sc);
#endif

	ogx_iff(sc);

	SSO_WR_8(sc->sc_node, SSO_GRP_INT_THR(PORT_GROUP_RX(sc)), 1);
	SSO_WR_8(sc->sc_node, SSO_GRP_INT_THR(PORT_GROUP_TX(sc)), 1);

	sc->sc_link_ops->link_init(sc);
	if (!LIST_EMPTY(&sc->sc_mii.mii_phys))
		mii_mediachg(&sc->sc_mii);

	/* Open the descriptor queue. */
	op = PKO3_LD_IO | PKO3_LD_DID;
	op |= node->node_id << PKO3_LD_NODE_S;
	op |= PKO3_DQOP_OPEN << PKO3_LD_OP_S;
	op |= DESC_QUEUE(sc) << PKO3_LD_DQ_S;
	(void)octeon_xkphys_read_8(op);

	ifp->if_flags |= IFF_RUNNING;
	ifq_restart(&ifp->if_snd);

	timeout_add(&sc->sc_rxrefill, 1);
	timeout_add_sec(&sc->sc_tick, 1);

	return 0;
}

void
ogx_down(struct ogx_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct ogx_node *node = sc->sc_node;
	uint64_t op, val;
	unsigned int nused;

	CLR(ifp->if_flags, IFF_RUNNING);

	/* Drain the descriptor queue. */
	val = PKO3_LX_SQ_SW_XOFF_DRAIN;
	val |= PKO3_LX_SQ_SW_XOFF_DRAIN_NULL_LINK;
	PKO3_WR_8(node, PKO3_DQ_SW_XOFF(DESC_QUEUE(sc)), val);
	(void)PKO3_RD_8(node, PKO3_DQ_SW_XOFF(DESC_QUEUE(sc)));

	delay(1000);

	/* Finish the drain operation. */
	PKO3_WR_8(node, PKO3_DQ_SW_XOFF(DESC_QUEUE(sc)), 0);
	(void)PKO3_RD_8(node, PKO3_DQ_SW_XOFF(DESC_QUEUE(sc)));

	/* Close the descriptor queue. */
	op = PKO3_LD_IO | PKO3_LD_DID;
	op |= node->node_id << PKO3_LD_NODE_S;
	op |= PKO3_DQOP_CLOSE << PKO3_LD_OP_S;
	op |= DESC_QUEUE(sc) << PKO3_LD_DQ_S;
	(void)octeon_xkphys_read_8(op);

	/* Disable data transfer. */
	val = PORT_RD_8(sc, BGX_CMR_CONFIG);
	val &= ~BGX_CMR_CONFIG_DATA_PKT_RX_EN;
	val &= ~BGX_CMR_CONFIG_DATA_PKT_TX_EN;
	PORT_WR_8(sc, BGX_CMR_CONFIG, val);
	(void)PORT_RD_8(sc, BGX_CMR_CONFIG);

	if (!LIST_EMPTY(&sc->sc_mii.mii_phys))
		mii_down(&sc->sc_mii);
	sc->sc_link_ops->link_down(sc);

	ifq_clr_oactive(&ifp->if_snd);
	ifq_barrier(&ifp->if_snd);

	timeout_del_barrier(&sc->sc_rxrefill);
	timeout_del_barrier(&sc->sc_tick);

#if NKSTAT > 0
	ogx_kstat_stop(sc);
#endif

	nused = ogx_unload_mbufs(sc);
	atomic_add_int(&sc->sc_rxused, nused);
}

void
ogx_iff(struct ogx_softc *sc)
{
	struct arpcom *ac = &sc->sc_ac;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct ether_multi *enm;
	struct ether_multistep step;
	uint64_t rx_adr_ctl;
	uint64_t val;
	int cidx, clast, i;

	rx_adr_ctl = PORT_RD_8(sc, BGX_CMR_RX_ADR_CTL);
	rx_adr_ctl |= BGX_CMR_RX_ADR_CTL_BCST_ACCEPT;
	rx_adr_ctl |= BGX_CMR_RX_ADR_CTL_CAM_ACCEPT;
	rx_adr_ctl &= ~BGX_CMR_RX_ADR_CTL_MCST_MODE_ALL;
	ifp->if_flags &= ~IFF_ALLMULTI;

	if (ISSET(ifp->if_flags, IFF_PROMISC)) {
		ifp->if_flags |= IFF_ALLMULTI;
		rx_adr_ctl &= ~BGX_CMR_RX_ADR_CTL_CAM_ACCEPT;
		rx_adr_ctl |= BGX_CMR_RX_ADR_CTL_MCST_MODE_ALL;
	} else if (ac->ac_multirangecnt > 0 || ac->ac_multicnt >= OGX_NCAM) {
		ifp->if_flags |= IFF_ALLMULTI;
		rx_adr_ctl |= BGX_CMR_RX_ADR_CTL_MCST_MODE_ALL;
	} else {
		rx_adr_ctl |= BGX_CMR_RX_ADR_CTL_MCST_MODE_CAM;
	}

	PORT_WR_8(sc, BGX_CMR_RX_ADR_CTL, rx_adr_ctl);

	cidx = sc->sc_lmacid * OGX_NCAM;
	clast = (sc->sc_lmacid + 1) * OGX_NCAM;

	if (!ISSET(ifp->if_flags, IFF_PROMISC)) {
		val = BGX_CMR_RX_ADR_CAM_EN | ((uint64_t)sc->sc_lmacid
		    << BGX_CMR_RX_ADR_CAM_ID_S);
		for (i = 0; i < ETHER_ADDR_LEN; i++) {
			val |= (uint64_t)ac->ac_enaddr[i] <<
			    ((ETHER_ADDR_LEN - 1 - i) * 8);
		}
		NEXUS_WR_8(sc, BGX_CMR_RX_ADR_CAM(cidx++), val);
	}

	if (!ISSET(ifp->if_flags, IFF_ALLMULTI)) {
		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			val = BGX_CMR_RX_ADR_CAM_EN | ((uint64_t)sc->sc_lmacid
			    << BGX_CMR_RX_ADR_CAM_ID_S);
			for (i = 0; i < ETHER_ADDR_LEN; i++)
				val |= (uint64_t)enm->enm_addrlo[i] << (i * 8);
			KASSERT(cidx < clast);
			NEXUS_WR_8(sc, BGX_CMR_RX_ADR_CAM(cidx++), val);

			ETHER_NEXT_MULTI(step, enm);
		}
	}

	/* Disable any remaining address CAM entries. */
	while (cidx < clast)
		NEXUS_WR_8(sc, BGX_CMR_RX_ADR_CAM(cidx++), 0);
}

static inline uint64_t *
ogx_get_work(struct ogx_node *node, uint32_t group)
{
	uint64_t op, resp;

	op = SSO_LD_IO | SSO_LD_DID;
	op |= node->node_id << SSO_LD_NODE_S;
	op |= SSO_LD_GROUPED | (group << SSO_LD_INDEX_S);
	resp = octeon_xkphys_read_8(op);

	if (resp & SSO_LD_RTN_NO_WORK)
		return NULL;

	return (uint64_t *)PHYS_TO_XKPHYS(resp & SSO_LD_RTN_ADDR_M, CCA_CACHED);
}

static inline struct mbuf *
ogx_extract_mbuf(struct ogx_softc *sc, paddr_t pktbuf)
{
	struct mbuf *m, **pm;

	pm = (struct mbuf **)PHYS_TO_XKPHYS(pktbuf, CCA_CACHED) - 1;
	m = *pm;
	*pm = NULL;
	KASSERTMSG((paddr_t)m->m_pkthdr.ph_cookie == pktbuf,
	    "%s: corrupt packet pool, mbuf cookie %p != pktbuf %p",
	    DEVNAME(sc), m->m_pkthdr.ph_cookie, (void *)pktbuf);
	m->m_pkthdr.ph_cookie = NULL;
	return m;
}

void
ogx_rxrefill(void *arg)
{
	struct ogx_softc *sc = arg;
	unsigned int to_alloc;

	if (sc->sc_rxused > 0) {
		to_alloc = atomic_swap_uint(&sc->sc_rxused, 0);
		to_alloc = ogx_load_mbufs(sc, to_alloc);
		if (to_alloc > 0) {
			atomic_add_int(&sc->sc_rxused, to_alloc);
			timeout_add(&sc->sc_rxrefill, 1);
		}
	}
}

void
ogx_tick(void *arg)
{
	struct ogx_softc *sc = arg;
	int s;

	s = splnet();
	if (!LIST_EMPTY(&sc->sc_mii.mii_phys)) {
		mii_tick(&sc->sc_mii);
	} else {
		if (sc->sc_link_ops->link_status(sc))
			sc->sc_link_ops->link_change(sc);
	}
	splx(s);

	timeout_add_sec(&sc->sc_tick, 1);
}

int
ogx_rxintr(void *arg)
{
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *m, *m0, *mprev;
	struct ogx_softc *sc = arg;
	struct ogx_node *node = sc->sc_node;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	paddr_t pktbuf, pktdata;
	uint64_t *work;
	uint64_t nsegs;
	unsigned int rxused = 0;

	/* Acknowledge the interrupt. */
	SSO_WR_8(node, SSO_GRP_INT(PORT_GROUP_RX(sc)), SSO_GRP_INT_EXE_INT);

	for (;;) {
		uint64_t errcode, errlevel;
		uint64_t word3;
		size_t pktlen, left;
#ifdef DIAGNOSTIC
		unsigned int pkind;
#endif

		work = ogx_get_work(sc->sc_node, PORT_GROUP_RX(sc));
		if (work == NULL)
			break;

#ifdef DIAGNOSTIC
		pkind = (work[0] & PKI_WORD0_PKIND_M) >> PKI_WORD0_PKIND_S;
		if (__predict_false(pkind != PORT_PKIND(sc))) {
			printf("%s: unexpected pkind %u, should be %u\n",
			    DEVNAME(sc), pkind, PORT_PKIND(sc));
			goto wqe_error;
		}
#endif

		nsegs = (work[0] & PKI_WORD0_BUFS_M) >> PKI_WORD0_BUFS_S;
		word3 = work[3];

		errlevel = (work[2] & PKI_WORD2_ERR_LEVEL_M) >>
		    PKI_WORD2_ERR_LEVEL_S;
		errcode = (work[2] & PKI_WORD2_ERR_CODE_M) >>
		    PKI_WORD2_ERR_CODE_S;
		if (__predict_false(errlevel <= 1 && errcode != 0)) {
			ifp->if_ierrors++;
			goto drop;
		}

		KASSERT(nsegs > 0);
		rxused += nsegs;

		pktlen = (work[1] & PKI_WORD1_LEN_M) >> PKI_WORD1_LEN_S;
		left = pktlen;

		m0 = NULL;
		mprev = NULL;
		while (nsegs-- > 0) {
			size_t size;

			pktdata = (word3 & PKI_WORD3_ADDR_M) >>
			    PKI_WORD3_ADDR_S;
			pktbuf = pktdata & ~(CACHELINESIZE - 1);
			size = (word3 & PKI_WORD3_SIZE_M) >> PKI_WORD3_SIZE_S;
			if (size > left)
				size = left;

			m = ogx_extract_mbuf(sc, pktbuf);
			m->m_data += (pktdata - pktbuf) & (CACHELINESIZE - 1);
			m->m_len = size;
			left -= size;

			/* pktdata can be unaligned. */
			memcpy(&word3, (void *)PHYS_TO_XKPHYS(pktdata -
			    sizeof(uint64_t), CCA_CACHED), sizeof(uint64_t));

			if (m0 == NULL) {
				m0 = m;
			} else {
				m->m_flags &= ~M_PKTHDR;
				mprev->m_next = m;
			}
			mprev = m;
		}

		m0->m_pkthdr.len = pktlen;
		ml_enqueue(&ml, m0);

		continue;

drop:
		/* Return the buffers back to the pool. */
		while (nsegs-- > 0) {
			pktdata = (word3 & PKI_WORD3_ADDR_M) >>
			    PKI_WORD3_ADDR_S;
			pktbuf = pktdata & ~(CACHELINESIZE - 1);
			/* pktdata can be unaligned. */
			memcpy(&word3, (void *)PHYS_TO_XKPHYS(pktdata -
			    sizeof(uint64_t), CCA_CACHED), sizeof(uint64_t));
			ogx_fpa3_free(&sc->sc_pkt_aura, pktbuf);
		}
	}

	if_input(ifp, &ml);

	rxused = ogx_load_mbufs(sc, rxused);
	if (rxused != 0) {
		atomic_add_int(&sc->sc_rxused, rxused);
		timeout_add(&sc->sc_rxrefill, 1);
	}

	return 1;

#ifdef DIAGNOSTIC
wqe_error:
	printf("work0: %016llx\n", work[0]);
	printf("work1: %016llx\n", work[1]);
	printf("work2: %016llx\n", work[2]);
	printf("work3: %016llx\n", work[3]);
	printf("work4: %016llx\n", work[4]);
	panic("%s: %s: wqe error", DEVNAME(sc), __func__);
#endif
}

int
ogx_txintr(void *arg)
{
	struct ogx_softc *sc = arg;
	struct ogx_node *node = sc->sc_node;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct mbuf *m;
	uint64_t *work;
	unsigned int nfreed = 0;

	/* Acknowledge the interrupt. */
	SSO_WR_8(node, SSO_GRP_INT(PORT_GROUP_TX(sc)), SSO_GRP_INT_EXE_INT);

	for (;;) {
		work = ogx_get_work(node, PORT_GROUP_TX(sc));
		if (work == NULL)
			break;

		/*
		 * work points to ph_cookie via the xkphys segment.
		 * ph_cookie contains the original mbuf pointer.
		 */
		m = *(struct mbuf **)work;
		KASSERT(m->m_pkthdr.ph_ifidx == (u_int)(uintptr_t)sc);
		m->m_pkthdr.ph_ifidx = 0;
		m_freem(m);
		nfreed++;
	}

	if (nfreed > 0 && atomic_add_int_nv(&sc->sc_txfree, nfreed) == nfreed)
		ifq_restart(&ifp->if_snd);

	return 1;
}

unsigned int
ogx_load_mbufs(struct ogx_softc *sc, unsigned int n)
{
	struct mbuf *m;
	paddr_t pktbuf;

	for ( ; n > 0; n--) {
		m = MCLGETL(NULL, M_NOWAIT, MCLBYTES);
		if (m == NULL)
			break;

		m->m_data = (void *)(((vaddr_t)m->m_data + CACHELINESIZE) &
		    ~(CACHELINESIZE - 1));
		((struct mbuf **)m->m_data)[-1] = m;

		pktbuf = KVTOPHYS(m->m_data);
		m->m_pkthdr.ph_cookie = (void *)pktbuf;
		ogx_fpa3_free(&sc->sc_pkt_aura, pktbuf);
	}
	return n;
}

unsigned int
ogx_unload_mbufs(struct ogx_softc *sc)
{
	struct mbuf *m;
	paddr_t pktbuf;
	unsigned int n = 0;

	for (;;) {
		pktbuf = ogx_fpa3_alloc(&sc->sc_pkt_aura);
		if (pktbuf == 0)
			break;
		m = ogx_extract_mbuf(sc, pktbuf);
		m_freem(m);
		n++;
	}
	return n;
}

void
ogx_start(struct ifqueue *ifq)
{
	struct ifnet *ifp = ifq->ifq_if;
	struct ogx_softc *sc = ifp->if_softc;
	struct mbuf *m;
	unsigned int txfree, txused;

	txfree = READ_ONCE(sc->sc_txfree);
	txused = 0;

	while (txused < txfree) {
		m = ifq_dequeue(ifq);
		if (m == NULL)
			break;

#if NBPFILTER > 0
		if (ifp->if_bpf != NULL)
			bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);
#endif

		if (ogx_send_mbuf(sc, m) != 0) {
			m_freem(m);
			ifp->if_oerrors++;
			continue;
		}
		txused++;
	}

	if (atomic_sub_int_nv(&sc->sc_txfree, txused) == 0)
		ifq_set_oactive(ifq);
}

int
ogx_send_mbuf(struct ogx_softc *sc, struct mbuf *m0)
{
	struct ether_header *eh;
	struct mbuf *m;
	uint64_t ehdrlen, hdr, scroff, word;
	unsigned int nfrags;

	/* Save original pointer for freeing after transmission. */
	m0->m_pkthdr.ph_cookie = m0;
	/* Add a tag for sanity checking. */
	m0->m_pkthdr.ph_ifidx = (u_int)(uintptr_t)sc;

	hdr = PKO3_SEND_HDR_DF;
	hdr |= m0->m_pkthdr.len << PKO3_SEND_HDR_TOTAL_S;

	if (m0->m_pkthdr.csum_flags &
	    (M_IPV4_CSUM_OUT | M_TCP_CSUM_OUT | M_UDP_CSUM_OUT)) {
		eh = mtod(m0, struct ether_header *);
		ehdrlen = ETHER_HDR_LEN;

		switch (ntohs(eh->ether_type)) {
		case ETHERTYPE_IP:
			hdr |= ehdrlen << PKO3_SEND_HDR_L3PTR_S;
			hdr |= (ehdrlen + sizeof(struct ip)) <<
			    PKO3_SEND_HDR_L4PTR_S;
			break;
		case ETHERTYPE_IPV6:
			hdr |= ehdrlen << PKO3_SEND_HDR_L3PTR_S;
			hdr |= (ehdrlen + sizeof(struct ip6_hdr)) <<
			    PKO3_SEND_HDR_L4PTR_S;
			break;
		default:
			break;
		}

		if (m0->m_pkthdr.csum_flags & M_IPV4_CSUM_OUT)
			hdr |= PKO3_SEND_HDR_CKL3;
		if (m0->m_pkthdr.csum_flags & M_TCP_CSUM_OUT)
			hdr |= PKO3_SEND_HDR_CKL4_TCP;
		if (m0->m_pkthdr.csum_flags & M_UDP_CSUM_OUT)
			hdr |= PKO3_SEND_HDR_CKL4_UDP;
	}

	/* Flush pending writes before packet submission. */
	octeon_syncw();

	/* Block until any previous LMTDMA request has been processed. */
	octeon_synciobdma();

	/* Get the LMTDMA region offset in the scratchpad. */
	scroff = 2 * 0x80;

	octeon_cvmseg_write_8(scroff, hdr);
	scroff += sizeof(hdr);

	for (m = m0, nfrags = 0; m != NULL && nfrags < 13;
	    m = m->m_next, nfrags++) {
		word = PKO3_SUBDC3_SEND_GATHER << PKO3_SUBC_BUF_PTR_SUBDC3_S;
		word |= KVTOPHYS(m->m_data) << PKO3_SUBC_BUF_PTR_ADDR_S;
		word |= (uint64_t)m->m_len << PKO3_SUBC_BUF_PTR_SIZE_S;
		octeon_cvmseg_write_8(scroff, word);
		scroff += sizeof(word);
	}

	if (m != NULL) {
		if (m_defrag(m0, M_DONTWAIT) != 0)
			return ENOMEM;

		/* Discard previously set fragments. */
		scroff -= sizeof(word) * nfrags;

		word = PKO3_SUBDC3_SEND_GATHER << PKO3_SUBC_BUF_PTR_SUBDC3_S;
		word |= KVTOPHYS(m0->m_data) << PKO3_SUBC_BUF_PTR_ADDR_S;
		word |= (uint64_t)m0->m_len << PKO3_SUBC_BUF_PTR_SIZE_S;
		octeon_cvmseg_write_8(scroff, word);
		scroff += sizeof(word);
	}

	/* Send work when ready to free the mbuf. */
	word = PKO3_SEND_WORK_CODE << PKO3_SEND_SUBDC4_CODE_S;
	word |= KVTOPHYS(&m0->m_pkthdr.ph_cookie) << PKO3_SEND_WORK_ADDR_S;
	word |= (uint64_t)PORT_GROUP_TX(sc) << PKO3_SEND_WORK_GRP_S;
	word |= 2ULL << PKO3_SEND_WORK_TT_S;
	octeon_cvmseg_write_8(scroff, word);
	scroff += sizeof(word);

	/* Submit the command. */
	word = PKO3_LMTDMA_DID;
	word |= ((2ULL * 0x80) >> 3) << PKO3_LMTDMA_SCRADDR_S;
	word |= 1ULL << PKO3_LMTDMA_RTNLEN_S;
	word |= DESC_QUEUE(sc) << PKO3_LMTDMA_DQ_S;
	octeon_lmtdma_write_8((scroff - 8) & 0x78, word);

	return 0;
}

int
ogx_media_change(struct ifnet *ifp)
{
	struct ogx_softc *sc = ifp->if_softc;

	if (!LIST_EMPTY(&sc->sc_mii.mii_phys))
		mii_mediachg(&sc->sc_mii);

	return 0;
}

void
ogx_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
	struct ogx_softc *sc = ifp->if_softc;

	if (!LIST_EMPTY(&sc->sc_mii.mii_phys)) {
		mii_pollstat(&sc->sc_mii);
		imr->ifm_status = sc->sc_mii.mii_media_status;
		imr->ifm_active = sc->sc_mii.mii_media_active;
	}
}

int
ogx_mii_readreg(struct device *self, int phy_no, int reg)
{
	struct ogx_softc *sc = (struct ogx_softc *)self;

	return cn30xxsmi_read(sc->sc_smi, phy_no, reg);
}

void
ogx_mii_writereg(struct device *self, int phy_no, int reg, int value)
{
	struct ogx_softc *sc = (struct ogx_softc *)self;

	cn30xxsmi_write(sc->sc_smi, phy_no, reg, value);
}

void
ogx_mii_statchg(struct device *self)
{
	struct ogx_softc *sc = (struct ogx_softc *)self;

	if (ISSET(sc->sc_mii.mii_media_active, IFM_FDX))
		sc->sc_link_duplex = 1;
	else
		sc->sc_link_duplex = 0;
	sc->sc_link_ops->link_change(sc);
}

int
ogx_sgmii_link_init(struct ogx_softc *sc)
{
	uint64_t cpu_freq = octeon_boot_info->eclock / 1000000;
	uint64_t val;
	int align = 1;

	val = PORT_RD_8(sc, BGX_GMP_GMI_TX_APPEND);
	val |= BGX_GMP_GMI_TX_APPEND_FCS;
	val |= BGX_GMP_GMI_TX_APPEND_PAD;
	if (ISSET(val, BGX_GMP_GMI_TX_APPEND_PREAMBLE))
		align = 0;
	PORT_WR_8(sc, BGX_GMP_GMI_TX_APPEND, val);
	PORT_WR_8(sc, BGX_GMP_GMI_TX_MIN_PKT, 59);
	PORT_WR_8(sc, BGX_GMP_GMI_TX_THRESH, 0x20);

	val = PORT_RD_8(sc, BGX_GMP_GMI_TX_SGMII_CTL);
	if (align)
		val |= BGX_GMP_GMI_TX_SGMII_CTL_ALIGN;
	else
		val &= ~BGX_GMP_GMI_TX_SGMII_CTL_ALIGN;
	PORT_WR_8(sc, BGX_GMP_GMI_TX_SGMII_CTL, val);

	/* Set timing for SGMII. */
	val = PORT_RD_8(sc, BGX_GMP_PCS_LINK_TIMER);
	val &= ~BGX_GMP_PCS_LINK_TIMER_COUNT_M;
	val |= (1600 * cpu_freq) >> 10;
	PORT_WR_8(sc, BGX_GMP_PCS_LINK_TIMER, val);

	return 0;
}

void
ogx_sgmii_link_down(struct ogx_softc *sc)
{
	uint64_t val;
	int timeout;

	/* Wait until the port is idle. */
	for (timeout = 1000; timeout > 0; timeout--) {
		const uint64_t idlemask = BGX_GMP_GMI_PRT_CFG_RX_IDLE |
		    BGX_GMP_GMI_PRT_CFG_TX_IDLE;
		val = PORT_RD_8(sc, BGX_GMP_GMI_PRT_CFG);
		if ((val & idlemask) == idlemask)
			break;
		delay(1000);
	}
	if (timeout == 0)
		printf("%s: port idle timeout\n", DEVNAME(sc));

	/* Disable autonegotiation and power down the link. */
	val = PORT_RD_8(sc, BGX_GMP_PCS_MR_CONTROL);
	val &= ~BGX_GMP_PCS_MR_CONTROL_AN_EN;
	val |= BGX_GMP_PCS_MR_CONTROL_PWR_DN;
	PORT_WR_8(sc, BGX_GMP_PCS_MR_CONTROL, val);
}

void
ogx_sgmii_link_change(struct ogx_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	uint64_t config;
	uint64_t misc_ctl;
	uint64_t prt_cfg = 0;
	uint64_t samp_pt;
	uint64_t tx_burst, tx_slot;
	uint64_t val;
	int timeout;

	if (!LINK_STATE_IS_UP(ifp->if_link_state)) {
		misc_ctl = PORT_RD_8(sc, BGX_GMP_PCS_MISC_CTL);
		misc_ctl |= BGX_GMP_PCS_MISC_CTL_GMXENO;
		PORT_WR_8(sc, BGX_GMP_PCS_MISC_CTL, misc_ctl);
		return;
	}

	val = PORT_RD_8(sc, BGX_CMR_CONFIG);
	val |= BGX_CMR_CONFIG_ENABLE;
	PORT_WR_8(sc, BGX_CMR_CONFIG, val);

	/* Reset the PCS. */
	val = PORT_RD_8(sc, BGX_GMP_PCS_MR_CONTROL);
	val |= BGX_GMP_PCS_MR_CONTROL_RESET;
	PORT_WR_8(sc, BGX_GMP_PCS_MR_CONTROL_RESET, val);

	/* Wait for the reset to complete. */
	timeout = 100000;
	while (timeout-- > 0) {
		val = PORT_RD_8(sc, BGX_GMP_PCS_MR_CONTROL);
		if (!ISSET(val, BGX_GMP_PCS_MR_CONTROL_RESET))
			break;
		delay(10);
	}
	if (timeout == 0)
		printf("%s: SGMII reset timeout\n", DEVNAME(sc));

	/* Use MAC mode. */
	val = PORT_RD_8(sc, BGX_GMP_PCS_MISC_CTL);
	val &= ~BGX_GMP_PCS_MISC_CTL_MAC_PHY;
	val &= ~BGX_GMP_PCS_MISC_CTL_MODE;
	PORT_WR_8(sc, BGX_GMP_PCS_MISC_CTL, val);

	/* Start autonegotiation between the SoC and the PHY. */
	val = PORT_RD_8(sc, BGX_GMP_PCS_MR_CONTROL);
	val |= BGX_GMP_PCS_MR_CONTROL_AN_EN;
	val |= BGX_GMP_PCS_MR_CONTROL_RST_AN;
	val &= ~BGX_GMP_PCS_MR_CONTROL_PWR_DN;
	PORT_WR_8(sc, BGX_GMP_PCS_MR_CONTROL, val);

	/* Wait for the autonegotiation to complete. */
	timeout = 100000;
	while (timeout-- > 0) {
		val = PORT_RD_8(sc, BGX_GMP_PCS_MR_STATUS);
		if (ISSET(val, BGX_GMP_PCS_MR_STATUS_AN_CPT))
			break;
		delay(10);
	}
	if (timeout == 0)
		printf("%s: SGMII autonegotiation timeout\n", DEVNAME(sc));

	/* Stop Rx and Tx engines. */
	config = PORT_RD_8(sc, BGX_CMR_CONFIG);
	config &= ~BGX_CMR_CONFIG_DATA_PKT_RX_EN;
	config &= ~BGX_CMR_CONFIG_DATA_PKT_TX_EN;
	PORT_WR_8(sc, BGX_CMR_CONFIG, config);
	(void)PORT_RD_8(sc, BGX_CMR_CONFIG);

	/* Wait until the engines are idle. */
	for (timeout = 1000000; timeout > 0; timeout--) {
		const uint64_t idlemask = BGX_GMP_GMI_PRT_CFG_RX_IDLE |
		    BGX_GMP_GMI_PRT_CFG_TX_IDLE;
		prt_cfg = PORT_RD_8(sc, BGX_GMP_GMI_PRT_CFG);
		if ((prt_cfg & idlemask) == idlemask)
			break;
		delay(1);
	}
	if (timeout == 0)
		printf("%s: port idle timeout\n", DEVNAME(sc));

	if (sc->sc_link_duplex)
		prt_cfg |= BGX_GMP_GMI_PRT_CFG_DUPLEX;
	else
		prt_cfg &= ~BGX_GMP_GMI_PRT_CFG_DUPLEX;

	switch (ifp->if_baudrate) {
	case IF_Mbps(10):
		prt_cfg &= ~BGX_GMP_GMI_PRT_CFG_SPEED;
		prt_cfg |= BGX_GMP_GMI_PRT_CFG_SPEED_MSB;
		prt_cfg &= ~BGX_GMP_GMI_PRT_CFG_SLOTTIME;
		samp_pt = 25;
		tx_slot = 0x40;
		tx_burst = 0;
		break;
	case IF_Mbps(100):
		prt_cfg &= ~BGX_GMP_GMI_PRT_CFG_SPEED;
		prt_cfg &= ~BGX_GMP_GMI_PRT_CFG_SPEED_MSB;
		prt_cfg &= ~BGX_GMP_GMI_PRT_CFG_SLOTTIME;
		samp_pt = 5;
		tx_slot = 0x40;
		tx_burst = 0;
		break;
	case IF_Gbps(1):
	default:
		prt_cfg |= BGX_GMP_GMI_PRT_CFG_SPEED;
		prt_cfg &= ~BGX_GMP_GMI_PRT_CFG_SPEED_MSB;
		prt_cfg |= BGX_GMP_GMI_PRT_CFG_SLOTTIME;
		samp_pt = 1;
		tx_slot = 0x200;
		if (sc->sc_link_duplex)
			tx_burst = 0;
		else
			tx_burst = 0x2000;
		break;
	}

	PORT_WR_8(sc, BGX_GMP_GMI_TX_SLOT, tx_slot);
	PORT_WR_8(sc, BGX_GMP_GMI_TX_BURST, tx_burst);

	misc_ctl = PORT_RD_8(sc, BGX_GMP_PCS_MISC_CTL);
	misc_ctl &= ~BGX_GMP_PCS_MISC_CTL_GMXENO;
	misc_ctl &= ~BGX_GMP_PCS_MISC_CTL_SAMP_PT_M;
	misc_ctl |= samp_pt << BGX_GMP_PCS_MISC_CTL_SAMP_PT_S;
	PORT_WR_8(sc, BGX_GMP_PCS_MISC_CTL, misc_ctl);
	(void)PORT_RD_8(sc, BGX_GMP_PCS_MISC_CTL);

	PORT_WR_8(sc, BGX_GMP_GMI_PRT_CFG, prt_cfg);
	(void)PORT_RD_8(sc, BGX_GMP_GMI_PRT_CFG);

	config = PORT_RD_8(sc, BGX_CMR_CONFIG);
	config |= BGX_CMR_CONFIG_ENABLE |
	    BGX_CMR_CONFIG_DATA_PKT_RX_EN |
	    BGX_CMR_CONFIG_DATA_PKT_TX_EN;
	PORT_WR_8(sc, BGX_CMR_CONFIG, config);
	(void)PORT_RD_8(sc, BGX_CMR_CONFIG);
}

#if NKSTAT > 0
enum ogx_stat {
	ogx_stat_rx_hmin,
	ogx_stat_rx_h64,
	ogx_stat_rx_h128,
	ogx_stat_rx_h256,
	ogx_stat_rx_h512,
	ogx_stat_rx_h1024,
	ogx_stat_rx_hmax,
	ogx_stat_rx_totp_pki,
	ogx_stat_rx_toto_pki,
	ogx_stat_rx_raw,
	ogx_stat_rx_drop,
	ogx_stat_rx_bcast,
	ogx_stat_rx_mcast,
	ogx_stat_rx_fcs_error,
	ogx_stat_rx_fcs_undersz,
	ogx_stat_rx_undersz,
	ogx_stat_rx_fcs_oversz,
	ogx_stat_rx_oversz,
	ogx_stat_rx_error,
	ogx_stat_rx_special,
	ogx_stat_rx_bdrop,
	ogx_stat_rx_mdrop,
	ogx_stat_rx_ipbdrop,
	ogx_stat_rx_ipmdrop,
	ogx_stat_rx_sdrop,
	ogx_stat_rx_totp_bgx,
	ogx_stat_rx_toto_bgx,
	ogx_stat_rx_pause,
	ogx_stat_rx_dmac,
	ogx_stat_rx_bgx_drop,
	ogx_stat_rx_bgx_error,
	ogx_stat_tx_hmin,
	ogx_stat_tx_h64,
	ogx_stat_tx_h65,
	ogx_stat_tx_h128,
	ogx_stat_tx_h256,
	ogx_stat_tx_h512,
	ogx_stat_tx_h1024,
	ogx_stat_tx_hmax,
	ogx_stat_tx_coll,
	ogx_stat_tx_defer,
	ogx_stat_tx_mcoll,
	ogx_stat_tx_scoll,
	ogx_stat_tx_toto_bgx,
	ogx_stat_tx_totp_bgx,
	ogx_stat_tx_bcast,
	ogx_stat_tx_mcast,
	ogx_stat_tx_uflow,
	ogx_stat_tx_control,
	ogx_stat_count
};

enum ogx_counter_type {
	C_NONE = 0,
	C_BGX,
	C_PKI,
};

struct ogx_counter {
	const char		*c_name;
	enum kstat_kv_unit	 c_unit;
	enum ogx_counter_type	 c_type;
	uint32_t		 c_reg;
};

static const struct ogx_counter ogx_counters[ogx_stat_count] = {
	[ogx_stat_rx_hmin] =
	    { "rx 1-63B",	KSTAT_KV_U_PACKETS, C_PKI, PKI_STAT_HIST0 },
	[ogx_stat_rx_h64] =
	    { "rx 64-127B",	KSTAT_KV_U_PACKETS, C_PKI, PKI_STAT_HIST1 },
	[ogx_stat_rx_h128] =
	    { "rx 128-255B",	KSTAT_KV_U_PACKETS, C_PKI, PKI_STAT_HIST2 },
	[ogx_stat_rx_h256] =
	    { "rx 256-511B",	KSTAT_KV_U_PACKETS, C_PKI, PKI_STAT_HIST3 },
	[ogx_stat_rx_h512] =
	    { "rx 512-1023B",	KSTAT_KV_U_PACKETS, C_PKI, PKI_STAT_HIST4 },
	[ogx_stat_rx_h1024] =
	    { "rx 1024-1518B",	KSTAT_KV_U_PACKETS, C_PKI, PKI_STAT_HIST5 },
	[ogx_stat_rx_hmax] =
	    { "rx 1519-maxB",	KSTAT_KV_U_PACKETS, C_PKI, PKI_STAT_HIST6 },
	[ogx_stat_rx_totp_pki] =
	    { "rx total pki",	KSTAT_KV_U_PACKETS, C_PKI, PKI_STAT_STAT0 },
	[ogx_stat_rx_toto_pki] =
	    { "rx total pki",	KSTAT_KV_U_BYTES, C_PKI, PKI_STAT_STAT1 },
	[ogx_stat_rx_raw] =
	    { "rx raw",		KSTAT_KV_U_PACKETS, C_PKI, PKI_STAT_STAT2 },
	[ogx_stat_rx_drop] =
	    { "rx drop",	KSTAT_KV_U_PACKETS, C_PKI, PKI_STAT_STAT3 },
	[ogx_stat_rx_bcast] =
	    { "rx bcast",	KSTAT_KV_U_PACKETS, C_PKI, PKI_STAT_STAT5 },
	[ogx_stat_rx_mcast] =
	    { "rx mcast",	KSTAT_KV_U_PACKETS, C_PKI, PKI_STAT_STAT6 },
	[ogx_stat_rx_fcs_error] =
	    { "rx fcs error",	KSTAT_KV_U_PACKETS, C_PKI, PKI_STAT_STAT7 },
	[ogx_stat_rx_fcs_undersz] =
	    { "rx fcs undersz",	KSTAT_KV_U_PACKETS, C_PKI, PKI_STAT_STAT8 },
	[ogx_stat_rx_undersz] =
	    { "rx undersz",	KSTAT_KV_U_PACKETS, C_PKI, PKI_STAT_STAT9 },
	[ogx_stat_rx_fcs_oversz] =
	    { "rx fcs oversz",	KSTAT_KV_U_PACKETS, C_PKI, PKI_STAT_STAT10 },
	[ogx_stat_rx_oversz] =
	    { "rx oversize",	KSTAT_KV_U_PACKETS, C_PKI, PKI_STAT_STAT11 },
	[ogx_stat_rx_error] =
	    { "rx error",	KSTAT_KV_U_PACKETS, C_PKI, PKI_STAT_STAT12 },
	[ogx_stat_rx_special] =
	    { "rx special",	KSTAT_KV_U_PACKETS, C_PKI, PKI_STAT_STAT13 },
	[ogx_stat_rx_bdrop] =
	    { "rx drop bcast",	KSTAT_KV_U_PACKETS, C_PKI, PKI_STAT_STAT14 },
	[ogx_stat_rx_mdrop] =
	    { "rx drop mcast",	KSTAT_KV_U_PACKETS, C_PKI, PKI_STAT_STAT15 },
	[ogx_stat_rx_ipbdrop] =
	    { "rx drop ipbcast",KSTAT_KV_U_PACKETS, C_PKI, PKI_STAT_STAT16 },
	[ogx_stat_rx_ipmdrop] =
	    { "rx drop ipmcast",KSTAT_KV_U_PACKETS, C_PKI, PKI_STAT_STAT17 },
	[ogx_stat_rx_sdrop] =
	    { "rx drop special",KSTAT_KV_U_PACKETS, C_PKI, PKI_STAT_STAT18 },
	[ogx_stat_rx_totp_bgx] =
	    { "rx total bgx",	KSTAT_KV_U_PACKETS, C_BGX, BGX_CMR_RX_STAT0 },
	[ogx_stat_rx_toto_bgx] =
	    { "rx total bgx",	KSTAT_KV_U_BYTES, C_BGX, BGX_CMR_RX_STAT1 },
	[ogx_stat_rx_pause] =
	    { "rx bgx pause",	KSTAT_KV_U_PACKETS, C_BGX, BGX_CMR_RX_STAT2 },
	[ogx_stat_rx_dmac] =
	    { "rx bgx dmac",	KSTAT_KV_U_PACKETS, C_BGX, BGX_CMR_RX_STAT4 },
	[ogx_stat_rx_bgx_drop] =
	    { "rx bgx drop",	KSTAT_KV_U_PACKETS, C_BGX, BGX_CMR_RX_STAT6 },
	[ogx_stat_rx_bgx_error] =
	    { "rx bgx error",	KSTAT_KV_U_PACKETS, C_BGX, BGX_CMR_RX_STAT8 },
	[ogx_stat_tx_hmin] =
	    { "tx 1-63B",	KSTAT_KV_U_PACKETS, C_BGX, BGX_CMR_TX_STAT6 },
	[ogx_stat_tx_h64] =
	    { "tx 64B",		KSTAT_KV_U_PACKETS, C_BGX, BGX_CMR_TX_STAT7 },
	[ogx_stat_tx_h65] =
	    { "tx 65-127B",	KSTAT_KV_U_PACKETS, C_BGX, BGX_CMR_TX_STAT8 },
	[ogx_stat_tx_h128] =
	    { "tx 128-255B",	KSTAT_KV_U_PACKETS, C_BGX, BGX_CMR_TX_STAT9 },
	[ogx_stat_tx_h256] =
	    { "tx 256-511B",	KSTAT_KV_U_PACKETS, C_BGX, BGX_CMR_TX_STAT10 },
	[ogx_stat_tx_h512] =
	    { "tx 512-1023B",	KSTAT_KV_U_PACKETS, C_BGX, BGX_CMR_TX_STAT11 },
	[ogx_stat_tx_h1024] =
	    { "tx 1024-1518B",	KSTAT_KV_U_PACKETS, C_BGX, BGX_CMR_TX_STAT12 },
	[ogx_stat_tx_hmax] =
	    { "tx 1519-maxB",	KSTAT_KV_U_PACKETS, C_BGX, BGX_CMR_TX_STAT13 },
	[ogx_stat_tx_coll] =
	    { "tx coll",	KSTAT_KV_U_PACKETS, C_BGX, BGX_CMR_TX_STAT0 },
	[ogx_stat_tx_defer] =
	    { "tx defer",	KSTAT_KV_U_PACKETS, C_BGX, BGX_CMR_TX_STAT1 },
	[ogx_stat_tx_mcoll] =
	    { "tx mcoll",	KSTAT_KV_U_PACKETS, C_BGX, BGX_CMR_TX_STAT2 },
	[ogx_stat_tx_scoll] =
	    { "tx scoll",	KSTAT_KV_U_PACKETS, C_BGX, BGX_CMR_TX_STAT3 },
	[ogx_stat_tx_toto_bgx] =
	    { "tx total bgx",	KSTAT_KV_U_BYTES, C_BGX, BGX_CMR_TX_STAT4 },
	[ogx_stat_tx_totp_bgx] =
	    { "tx total bgx",	KSTAT_KV_U_PACKETS, C_BGX, BGX_CMR_TX_STAT5 },
	[ogx_stat_tx_bcast] =
	    { "tx bcast",	KSTAT_KV_U_PACKETS, C_BGX, BGX_CMR_TX_STAT14 },
	[ogx_stat_tx_mcast] =
	    { "tx mcast",	KSTAT_KV_U_PACKETS, C_BGX, BGX_CMR_TX_STAT15 },
	[ogx_stat_tx_uflow] =
	    { "tx underflow",	KSTAT_KV_U_PACKETS, C_BGX, BGX_CMR_TX_STAT16 },
	[ogx_stat_tx_control] =
	    { "tx control",	KSTAT_KV_U_PACKETS, C_BGX, BGX_CMR_TX_STAT17 },
};

void
ogx_kstat_attach(struct ogx_softc *sc)
{
	const struct ogx_counter *c;
	struct kstat *ks;
	struct kstat_kv *kvs;
	struct ogx_node *node = sc->sc_node;
	uint64_t *vals;
	int i;

	mtx_init(&sc->sc_kstat_mtx, IPL_SOFTCLOCK);
	timeout_set(&sc->sc_kstat_tmo, ogx_kstat_tick, sc);

	if (bus_space_subregion(node->node_iot, node->node_pki,
	    PKI_STAT_BASE(PORT_PKIND(sc)), PKI_STAT_SIZE,
	    &sc->sc_pki_stat_ioh) != 0)
		return;

	ks = kstat_create(DEVNAME(sc), 0, "ogx-stats", 0, KSTAT_T_KV, 0);
	if (ks == NULL)
		return;

	vals = mallocarray(nitems(ogx_counters), sizeof(*vals),
	    M_DEVBUF, M_WAITOK | M_ZERO);
	sc->sc_counter_vals = vals;

	kvs = mallocarray(nitems(ogx_counters), sizeof(*kvs),
	    M_DEVBUF, M_WAITOK | M_ZERO);
	for (i = 0; i < nitems(ogx_counters); i++) {
		c = &ogx_counters[i];
		kstat_kv_unit_init(&kvs[i], c->c_name, KSTAT_KV_T_COUNTER64,
		    c->c_unit);
	}

	kstat_set_mutex(ks, &sc->sc_kstat_mtx);
	ks->ks_softc = sc;
	ks->ks_data = kvs;
	ks->ks_datalen = nitems(ogx_counters) * sizeof(*kvs);
	ks->ks_read = ogx_kstat_read;

	sc->sc_kstat = ks;
	kstat_install(ks);
}

int
ogx_kstat_read(struct kstat *ks)
{
	const struct ogx_counter *c;
	struct ogx_softc *sc = ks->ks_softc;
	struct kstat_kv *kvs = ks->ks_data;
	uint64_t *counter_vals = sc->sc_counter_vals;
	uint64_t delta, val;
	int i, timeout;

	for (i = 0; i < nitems(ogx_counters); i++) {
		c = &ogx_counters[i];
		switch (c->c_type) {
		case C_BGX:
			val = PORT_RD_8(sc, c->c_reg);
			delta = (val - counter_vals[i]) & BGX_CMR_STAT_MASK;
			counter_vals[i] = val;
			kstat_kv_u64(&kvs[i]) += delta;
			break;
		case C_PKI:
			/*
			 * Retry the read if the value is bogus.
			 * This can happen on some hardware when
			 * the hardware is updating the value.
			 */
			for (timeout = 100; timeout > 0; timeout--) {
				val = bus_space_read_8(sc->sc_iot,
				    sc->sc_pki_stat_ioh, c->c_reg);
				if (val != ~0ULL) {
					delta = (val - counter_vals[i]) &
					    PKI_STAT_MASK;
					counter_vals[i] = val;
					kstat_kv_u64(&kvs[i]) += delta;
					break;
				}
				CPU_BUSY_CYCLE();
			}
			break;
		case C_NONE:
			break;
		}
	}

	getnanouptime(&ks->ks_updated);

	return 0;
}

void
ogx_kstat_start(struct ogx_softc *sc)
{
	const struct ogx_counter *c;
	int i;

	/* Zero the counters. */
	for (i = 0; i < nitems(ogx_counters); i++) {
		c = &ogx_counters[i];
		switch (c->c_type) {
		case C_BGX:
			PORT_WR_8(sc, c->c_reg, 0);
			break;
		case C_PKI:
			bus_space_write_8(sc->sc_iot, sc->sc_pki_stat_ioh,
			    c->c_reg, 0);
			break;
		case C_NONE:
			break;
		}
	}
	memset(sc->sc_counter_vals, 0,
	    nitems(ogx_counters) * sizeof(*sc->sc_counter_vals));

	timeout_add_sec(&sc->sc_kstat_tmo, OGX_KSTAT_TICK_SECS);
}

void
ogx_kstat_stop(struct ogx_softc *sc)
{
	timeout_del_barrier(&sc->sc_kstat_tmo);

	mtx_enter(&sc->sc_kstat_mtx);
	ogx_kstat_read(sc->sc_kstat);
	mtx_leave(&sc->sc_kstat_mtx);
}

void
ogx_kstat_tick(void *arg)
{
	struct ogx_softc *sc = arg;

	timeout_add_sec(&sc->sc_kstat_tmo, OGX_KSTAT_TICK_SECS);

	if (mtx_enter_try(&sc->sc_kstat_mtx)) {
		ogx_kstat_read(sc->sc_kstat);
		mtx_leave(&sc->sc_kstat_mtx);
	}
}
#endif /* NKSTAT > 0 */

int
ogx_node_init(struct ogx_node **pnode, bus_dma_tag_t dmat, bus_space_tag_t iot)
{
	const struct ogx_config *cfg;
	struct ogx_node *node = &ogx_node;
	uint64_t val;
	uint32_t chipid;
	int cl, i, timeout;

	if (node->node_flags & NODE_INITED) {
		*pnode = node;
		return 0;
	}

	chipid = octeon_get_chipid();
	switch (octeon_model_family(chipid)) {
	case OCTEON_MODEL_FAMILY_CN73XX:
		node->node_cfg = cfg = &ogx_cn73xx_config;
		break;
	case OCTEON_MODEL_FAMILY_CN78XX:
		node->node_cfg = cfg = &ogx_cn78xx_config;
		break;
	default:
		printf(": unhandled chipid 0x%x\n", chipid);
		return -1;
	}

	rw_init(&node->node_lock, "ogxnlk");

	node->node_dmat = dmat;
	node->node_iot = iot;
	if (bus_space_map(node->node_iot, FPA3_BASE, FPA3_SIZE, 0,
	    &node->node_fpa3)) {
		printf(": can't map FPA3\n");
		goto error;
	}
	if (bus_space_map(node->node_iot, PKI_BASE, PKI_SIZE, 0,
	    &node->node_pki)) {
		printf(": can't map PKI\n");
		goto error;
	}
	if (bus_space_map(node->node_iot, PKO3_BASE, PKO3_SIZE, 0,
	    &node->node_pko3)) {
		printf(": can't map PKO3\n");
		goto error;
	}
	if (bus_space_map(node->node_iot, SSO_BASE, SSO_SIZE, 0,
	    &node->node_sso)) {
		printf(": can't map SSO\n");
		goto error;
	}

	/*
	 * The rest of this function handles errors by panicking.
	 */

	node->node_flags |= NODE_INITED;

	PKO3_WR_8(node, PKO3_CHANNEL_LEVEL, 0);

	ogx_fpa3_pool_init(node, &node->node_pkt_pool, OGX_POOL_PKT, 1024 * 32);
	ogx_fpa3_pool_init(node, &node->node_pko_pool, OGX_POOL_PKO, 1024 * 32);
	ogx_fpa3_pool_init(node, &node->node_sso_pool, OGX_POOL_SSO, 1024 * 32);

	ogx_fpa3_aura_init(node, &node->node_pko_aura, OGX_AURA_PKO,
	    &node->node_pko_pool);
	ogx_fpa3_aura_init(node, &node->node_sso_aura, OGX_AURA_SSO,
	    &node->node_sso_pool);

	ogx_fpa3_aura_load(node, &node->node_sso_aura, 1024, 4096);
	ogx_fpa3_aura_load(node, &node->node_pko_aura, 1024, 4096);

	/*
	 * Initialize the Schedule/Synchronization/Order (SSO) unit.
	 */

	val = SSO_AW_CFG_LDWB | SSO_AW_CFG_LDT | SSO_AW_CFG_STT;
	SSO_WR_8(node, SSO_AW_CFG, val);

	val = node->node_id << SSO_XAQ_AURA_NODE_S;
	val |= (uint64_t)OGX_AURA_SSO << SSO_XAQ_AURA_LAURA_S;
	SSO_WR_8(node, SSO_XAQ_AURA, val);

	SSO_WR_8(node, SSO_ERR0, 0);

	/* Initialize the hardware's linked lists. */
	for (i = 0; i < 64; i++) {
		paddr_t addr;

		addr = ogx_fpa3_alloc(&node->node_sso_aura);
		if (addr == 0)
			panic("%s: could not alloc initial XAQ block %d",
			    __func__, i);
		SSO_WR_8(node, SSO_XAQ_HEAD_PTR(i), addr);
		SSO_WR_8(node, SSO_XAQ_TAIL_PTR(i), addr);
		SSO_WR_8(node, SSO_XAQ_HEAD_NEXT(i), addr);
		SSO_WR_8(node, SSO_XAQ_TAIL_NEXT(i), addr);

		SSO_WR_8(node, SSO_GRP_PRI(i), SSO_GRP_PRI_WEIGHT_M);
	}

	val = SSO_RD_8(node, SSO_AW_CFG);
	val |= SSO_AW_CFG_RWEN;
	SSO_WR_8(node, SSO_AW_CFG, val);

	/*
	 * Initialize the Packet Input (PKI) unit.
	 */

	/* Clear any previous style configuration. */
	for (cl = 0; cl < cfg->cfg_nclusters; cl++) {
		int pkind;

		for (pkind = 0; pkind < 64; pkind++)
			PKI_WR_8(node, PKI_CL_PKIND_STYLE(cl, pkind), 0);
	}

	/* Invalidate all PCAM entries. */
	for (cl = 0; cl < cfg->cfg_nclusters; cl++) {
		int bank;

		for (bank = 0; bank < 2; bank++) {
			for (i = 0; i < 192; i++) {
				PKI_WR_8(node,
				    PKI_CL_PCAM_TERM(cl, bank, i), 0);
			}
		}
	}

	PKI_WR_8(node, PKI_STAT_CTL, 0);

	/* Enable input backpressure. */
	val = PKI_RD_8(node, PKI_BUF_CTL);
	val |= PKI_BUF_CTL_PBP_EN;
	PKI_WR_8(node, PKI_BUF_CTL, val);

	/* Disable the parsing clusters until the firmware has been loaded. */
	for (cl = 0; cl < cfg->cfg_nclusters; cl++) {
		val = PKI_RD_8(node, PKI_ICG_CFG(cl));
		val &= ~PKI_ICG_CFG_PENA;
		PKI_WR_8(node, PKI_ICG_CFG(cl), val);
	}

	val = PKI_RD_8(node, PKI_GBL_PEN);
	val &= ~PKI_GBL_PEN_M;
	val |= PKI_GBL_PEN_L3;
	val |= PKI_GBL_PEN_L4;
	PKI_WR_8(node, PKI_GBL_PEN, val);

	for (i = 0; i < nitems(ogx_ltypes); i++) {
		val = PKI_RD_8(node, PKI_LTYPE_MAP(i));
		val &= ~0x7;
		val |= ogx_ltypes[i];
		PKI_WR_8(node, PKI_LTYPE_MAP(i), val);
	}

	while (PKI_RD_8(node, PKI_SFT_RST) & PKI_SFT_RST_BUSY)
		delay(1);

	val = PKI_RD_8(node, PKI_BUF_CTL);
	val |= PKI_BUF_CTL_PKI_EN;
	PKI_WR_8(node, PKI_BUF_CTL, val);

	/*
	 * Initialize the Packet Output (PKO) unit.
	 */

	/* Detach MACs from FIFOs. */
	for (i = 0; i < cfg->cfg_nmacs; i++) {
		val = PKO3_RD_8(node, PKO3_MAC_CFG(i));
		val |= PKO3_MAC_CFG_FIFO_NUM_M;
		PKO3_WR_8(node, PKO3_MAC_CFG(i), val);
	}

	/* Attach port queues to the NULL FIFO. */
	for (i = 0; i < cfg->cfg_npqs; i++) {
		val = (uint64_t)cfg->cfg_nullmac << PKO3_L1_SQ_TOPOLOGY_LINK_S;
		PKO3_WR_8(node, PKO3_L1_SQ_TOPOLOGY(i), val);
		val = (uint64_t)cfg->cfg_nullmac << PKO3_L1_SQ_SHAPE_LINK_S;
		PKO3_WR_8(node, PKO3_L1_SQ_SHAPE(i), val);
		val = (uint64_t)cfg->cfg_nullmac << PKO3_L1_SQ_LINK_LINK_S;
		PKO3_WR_8(node, PKO3_L1_SQ_LINK(i), val);
	}

	/* Reset the FIFO groups to use 2.5 KB per each FIFO. */
	for (i = 0; i < cfg->cfg_nfifogrps; i++) {
		val = PKO3_RD_8(node, PKO3_PTGF_CFG(i));
		val &= ~PKO3_PTGF_CFG_SIZE_M;
		val &= ~PKO3_PTGF_CFG_RATE_M;
		val |= 2 << PKO3_PTGF_CFG_RATE_S;
		val |= PKO3_PTGF_CFG_RESET;
		PKO3_WR_8(node, PKO3_PTGF_CFG(i), val);

		val = PKO3_RD_8(node, PKO3_PTGF_CFG(i));
		val &= ~PKO3_PTGF_CFG_RESET;
		PKO3_WR_8(node, PKO3_PTGF_CFG(i), val);
	}

	PKO3_WR_8(node, PKO3_DPFI_FLUSH, 0);

	/* Set PKO aura. */
	val = ((uint64_t)node->node_id << PKO3_DPFI_FPA_AURA_NODE_S) |
	    (OGX_AURA_PKO << PKO3_DPFI_FPA_AURA_AURA_S);
	PKO3_WR_8(node, PKO3_DPFI_FPA_AURA, val);

	/* Allow PKO to use the FPA. */
	PKO3_WR_8(node, PKO3_DPFI_FPA_ENA, PKO3_DPFI_FPA_ENA_ENABLE);

	timeout = 1000;
	while (timeout-- > 0) {
		val = PKO3_RD_8(node, PKO3_STATUS);
		if (ISSET(val, PKO3_STATUS_PKO_RDY))
			break;
		delay(1000);
	}
	if (timeout == 0)
		panic("PKO timeout");

	val = 72 << PKO3_PTF_IOBP_CFG_MAX_RD_SZ_S;
	PKO3_WR_8(node, PKO3_PTF_IOBP_CFG, val);

	val = 60 << PKO3_PDM_CFG_MIN_PAD_LEN_S;
	PKO3_WR_8(node, PKO3_PDM_CFG, val);

	PKO3_WR_8(node, PKO3_ENABLE, PKO3_ENABLE_ENABLE);

	*pnode = node;
	return 0;

error:
	if (node->node_sso != 0)
		bus_space_unmap(node->node_iot, node->node_sso, SSO_SIZE);
	if (node->node_pko3 != 0)
		bus_space_unmap(node->node_iot, node->node_pko3, PKO3_SIZE);
	if (node->node_pki != 0)
		bus_space_unmap(node->node_iot, node->node_pki, PKI_SIZE);
	if (node->node_fpa3 != 0)
		bus_space_unmap(node->node_iot, node->node_fpa3, FPA3_SIZE);
	node->node_sso = 0;
	node->node_pko3 = 0;
	node->node_pki = 0;
	node->node_fpa3 = 0;
	return 1;
}

paddr_t
ogx_fpa3_alloc(struct fpa3aura *aura)
{
	uint64_t op;

	op = FPA3_LD_IO | FPA3_LD_DID;
	op |= (uint64_t)aura->nodeid << FPA3_LD_NODE_S;
	op |= (uint64_t)aura->auraid << FPA3_LD_AURA_S;
	return octeon_xkphys_read_8(op);
}

void
ogx_fpa3_free(struct fpa3aura *aura, paddr_t addr)
{
	uint64_t op;

	/* Flush pending writes before the block is freed. */
	octeon_syncw();

	op = FPA3_ST_IO | FPA3_ST_DID_FPA;
	op |= (uint64_t)aura->nodeid << FPA3_ST_NODE_S;
	op |= (uint64_t)aura->auraid << FPA3_ST_AURA_S;
	octeon_xkphys_write_8(op, addr);
}

void
ogx_fpa3_pool_init(struct ogx_node *node, struct fpa3pool *pool,
    uint32_t poolid, uint32_t nentries)
{
	size_t segsize;
	int rsegs;

	segsize = nentries * 16;

	pool->nodeid = node->node_id;
	pool->poolid = poolid;

	if (bus_dmamap_create(node->node_dmat, segsize, 1, segsize, 0,
	    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW, &pool->dmap))
		panic("%s: out of memory", __func__);
	if (bus_dmamem_alloc(node->node_dmat, segsize, CACHELINESIZE,
	    0, &pool->dmaseg, 1, &rsegs,
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO))
		panic("%s: out of memory", __func__);
	if (bus_dmamem_map(node->node_dmat, &pool->dmaseg, 1, segsize,
	    &pool->kva, BUS_DMA_NOWAIT | BUS_DMA_COHERENT))
		panic("%s: bus_dmamem_map", __func__);
	if (bus_dmamap_load(node->node_dmat, pool->dmap, pool->kva, segsize,
	    NULL, BUS_DMA_NOWAIT))
		panic("%s: bus_dmamap_load", __func__);

	/* Disable the pool before setup. */
	FPA3_WR_8(node, FPA3_POOL_CFG(poolid), 0);

	/* Set permitted address range of stored pointers. */
	FPA3_WR_8(node, FPA3_POOL_START_ADDR(poolid), CACHELINESIZE);
	FPA3_WR_8(node, FPA3_POOL_END_ADDR(poolid), UINT32_MAX);

	/* Set up the pointer stack. */
	FPA3_WR_8(node, FPA3_POOL_STACK_BASE(poolid), pool->dmaseg.ds_addr);
	FPA3_WR_8(node, FPA3_POOL_STACK_ADDR(poolid), pool->dmaseg.ds_addr);
	FPA3_WR_8(node, FPA3_POOL_STACK_END(poolid), pool->dmaseg.ds_addr +
	    pool->dmaseg.ds_len);

	/* Re-enable the pool. */
	FPA3_WR_8(node, FPA3_POOL_CFG(poolid), FPA3_POOL_CFG_ENA);
}

void
ogx_fpa3_aura_init(struct ogx_node *node, struct fpa3aura *aura,
    uint32_t auraid, struct fpa3pool *pool)
{
	KASSERT(node->node_id == pool->nodeid);

	aura->nodeid = pool->nodeid;
	aura->poolid = pool->poolid;
	aura->auraid = auraid;

	/* Enable pointer counting. */
	FPA3_WR_8(node, FPA3_AURA_CFG(aura->auraid), 0);
	FPA3_WR_8(node, FPA3_AURA_CNT(aura->auraid), 1024);
	FPA3_WR_8(node, FPA3_AURA_CNT_LIMIT(aura->auraid), 1024);

	/* Set the backend pool. */
	FPA3_WR_8(node, FPA3_AURA_POOL(aura->auraid), aura->poolid);
}

void
ogx_fpa3_aura_load(struct ogx_node *node, struct fpa3aura *aura, size_t nelem,
    size_t size)
{
	paddr_t addr;
	caddr_t kva;
	size_t i;
	size_t totsize;
	int rsegs;

	KASSERT(size % CACHELINESIZE == 0);

	if (nelem > SIZE_MAX / size)
		panic("%s: too large allocation", __func__);
	totsize = nelem * size;

	if (bus_dmamap_create(node->node_dmat, totsize, 1, totsize, 0,
	    BUS_DMA_WAITOK | BUS_DMA_ALLOCNOW, &aura->dmap))
		panic("%s: out of memory", __func__);
	if (bus_dmamem_alloc(node->node_dmat, totsize, CACHELINESIZE, 0,
	    &aura->dmaseg, 1, &rsegs, BUS_DMA_NOWAIT | BUS_DMA_ZERO))
		panic("%s: out of memory", __func__);
	if (bus_dmamem_map(node->node_dmat, &aura->dmaseg, rsegs, totsize,
	    &kva, BUS_DMA_NOWAIT | BUS_DMA_COHERENT))
		panic("%s: bus_dmamem_map failed", __func__);
	if (bus_dmamap_load(node->node_dmat, aura->dmap, kva, totsize, NULL,
	    BUS_DMA_NOWAIT))
		panic("%s: bus_dmamap_load failed", __func__);

	for (i = 0, addr = aura->dmaseg.ds_addr; i < nelem; i++, addr += size)
		ogx_fpa3_free(aura, addr);
}

int
ogx_node_load_firmware(struct ogx_node *node)
{
	struct ogx_fwhdr *fw;
	uint8_t *ucode = NULL;
	size_t size = 0;
	uint64_t *imem, val;
	int cl, error = 0, i;

	rw_enter_write(&node->node_lock);
	if (node->node_flags & NODE_FWREADY)
		goto out;

	error = loadfirmware("ogx-pki-cluster", &ucode, &size);
	if (error != 0) {
		printf("ogx node%llu: could not load firmware, error %d\n",
		    node->node_id, error);
		goto out;
	}

	fw = (struct ogx_fwhdr *)ucode;
	if (size < sizeof(*fw) || fw->fw_size != size - sizeof(*fw)) {
		printf("ogx node%llu: invalid firmware\n", node->node_id);
		error = EINVAL;
		goto out;
	}

	imem = (uint64_t *)(fw + 1);
	for (i = 0; i < fw->fw_size / sizeof(uint64_t); i++)
		PKI_WR_8(node, PKI_IMEM(i), imem[i]);

	/* Enable the parsing clusters. */
	for (cl = 0; cl < node->node_cfg->cfg_nclusters; cl++) {
		val = PKI_RD_8(node, PKI_ICG_CFG(cl));
		val |= PKI_ICG_CFG_PENA;
		PKI_WR_8(node, PKI_ICG_CFG(cl), val);
	}

	node->node_flags |= NODE_FWREADY;

out:
	free(ucode, M_DEVBUF, size);
	rw_exit_write(&node->node_lock);
	return error;
}
