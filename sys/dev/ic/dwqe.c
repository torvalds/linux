/*	$OpenBSD: dwqe.c,v 1.22 2024/06/05 10:19:55 stsp Exp $	*/
/*
 * Copyright (c) 2008, 2019 Mark Kettenis <kettenis@openbsd.org>
 * Copyright (c) 2017, 2022 Patrick Wildt <patrick@blueri.se>
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
#include "vlan.h"

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

#include <net/if.h>
#include <net/if_media.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <dev/ic/dwqevar.h>
#include <dev/ic/dwqereg.h>

struct cfdriver dwqe_cd = {
	NULL, "dwqe", DV_IFNET
};

uint32_t dwqe_read(struct dwqe_softc *, bus_addr_t);
void	dwqe_write(struct dwqe_softc *, bus_addr_t, uint32_t);

int	dwqe_ioctl(struct ifnet *, u_long, caddr_t);
void	dwqe_start(struct ifqueue *);
void	dwqe_watchdog(struct ifnet *);

int	dwqe_media_change(struct ifnet *);
void	dwqe_media_status(struct ifnet *, struct ifmediareq *);

void	dwqe_mii_attach(struct dwqe_softc *);
int	dwqe_mii_readreg(struct device *, int, int);
void	dwqe_mii_writereg(struct device *, int, int, int);
void	dwqe_mii_statchg(struct device *);

void	dwqe_lladdr_read(struct dwqe_softc *, uint8_t *);
void	dwqe_lladdr_write(struct dwqe_softc *);

void	dwqe_tick(void *);
void	dwqe_rxtick(void *);

int	dwqe_intr(void *);
void	dwqe_tx_proc(struct dwqe_softc *);
void	dwqe_rx_proc(struct dwqe_softc *);

void	dwqe_up(struct dwqe_softc *);
void	dwqe_down(struct dwqe_softc *);
void	dwqe_iff(struct dwqe_softc *);
int	dwqe_encap(struct dwqe_softc *, struct mbuf *, int *, int *);

void	dwqe_reset(struct dwqe_softc *);

struct dwqe_dmamem *
	dwqe_dmamem_alloc(struct dwqe_softc *, bus_size_t, bus_size_t);
void	dwqe_dmamem_free(struct dwqe_softc *, struct dwqe_dmamem *);
struct mbuf *dwqe_alloc_mbuf(struct dwqe_softc *, bus_dmamap_t);
void	dwqe_fill_rx_ring(struct dwqe_softc *);

int
dwqe_have_tx_csum_offload(struct dwqe_softc *sc)
{
	return (sc->sc_hw_feature[0] & GMAC_MAC_HW_FEATURE0_TXCOESEL);
}

int
dwqe_have_tx_vlan_offload(struct dwqe_softc *sc)
{
#if NVLAN > 0
	return (sc->sc_hw_feature[0] & GMAC_MAC_HW_FEATURE0_SAVLANINS);
#else
	return 0;
#endif
}

void
dwqe_set_vlan_rx_mode(struct dwqe_softc *sc)
{
#if NVLAN > 0
	uint32_t reg;

	/* Enable outer VLAN tag stripping on Rx. */
	reg = dwqe_read(sc, GMAC_VLAN_TAG_CTRL);
	reg |= GMAC_VLAN_TAG_CTRL_EVLRXS | GMAC_VLAN_TAG_CTRL_STRIP_ALWAYS;
	dwqe_write(sc, GMAC_VLAN_TAG_CTRL, reg);
#endif
}

void
dwqe_set_vlan_tx_mode(struct dwqe_softc *sc)
{
#if NVLAN > 0
	uint32_t reg;

	reg = dwqe_read(sc, GMAC_VLAN_TAG_INCL);

	/* Enable insertion of outer VLAN tag. */
	reg |= GMAC_VLAN_TAG_INCL_INSERT;

	/*
	 * Generate C-VLAN tags (type 0x8100, 802.1Q). Setting this
	 * bit would result in S-VLAN tags (type 0x88A8, 802.1ad).
	 */
	reg &= ~GMAC_VLAN_TAG_INCL_CSVL;

	/* Use VLAN tags provided in Tx context descriptors. */
	reg |= GMAC_VLAN_TAG_INCL_VLTI;

	dwqe_write(sc, GMAC_VLAN_TAG_INCL, reg);
#endif
}

int
dwqe_attach(struct dwqe_softc *sc)
{
	struct ifnet *ifp;
	uint32_t version, mode;
	int i;

	version = dwqe_read(sc, GMAC_VERSION);
	printf(": rev 0x%02x, address %s\n", version & GMAC_VERSION_SNPS_MASK,
	    ether_sprintf(sc->sc_lladdr));

	for (i = 0; i < 4; i++)
		sc->sc_hw_feature[i] = dwqe_read(sc, GMAC_MAC_HW_FEATURE(i));

	timeout_set(&sc->sc_phy_tick, dwqe_tick, sc);
	timeout_set(&sc->sc_rxto, dwqe_rxtick, sc);

	ifp = &sc->sc_ac.ac_if;
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_xflags = IFXF_MPSAFE;
	ifp->if_ioctl = dwqe_ioctl;
	ifp->if_qstart = dwqe_start;
	ifp->if_watchdog = dwqe_watchdog;
	ifq_init_maxlen(&ifp->if_snd, DWQE_NTXDESC - 1);
	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);

	ifp->if_capabilities = IFCAP_VLAN_MTU;
	if (dwqe_have_tx_vlan_offload(sc))
		ifp->if_capabilities |= IFCAP_VLAN_HWTAGGING;
	if (dwqe_have_tx_csum_offload(sc)) {
		ifp->if_capabilities |= (IFCAP_CSUM_IPv4 |
		    IFCAP_CSUM_TCPv4 | IFCAP_CSUM_UDPv4 |
		    IFCAP_CSUM_TCPv6 | IFCAP_CSUM_UDPv6);
	}

	sc->sc_mii.mii_ifp = ifp;
	sc->sc_mii.mii_readreg = dwqe_mii_readreg;
	sc->sc_mii.mii_writereg = dwqe_mii_writereg;
	sc->sc_mii.mii_statchg = dwqe_mii_statchg;

	ifmedia_init(&sc->sc_media, 0, dwqe_media_change, dwqe_media_status);

	dwqe_reset(sc);

	/* Configure DMA engine. */
	mode = dwqe_read(sc, GMAC_SYS_BUS_MODE);
	if (sc->sc_fixed_burst)
		mode |= GMAC_SYS_BUS_MODE_FB;
	if (sc->sc_mixed_burst)
		mode |= GMAC_SYS_BUS_MODE_MB;
	if (sc->sc_aal)
		mode |= GMAC_SYS_BUS_MODE_AAL;
	dwqe_write(sc, GMAC_SYS_BUS_MODE, mode);

	/* Configure channel 0. */
	mode = dwqe_read(sc, GMAC_CHAN_CONTROL(0));
	if (sc->sc_8xpbl)
		mode |= GMAC_CHAN_CONTROL_8XPBL;
	dwqe_write(sc, GMAC_CHAN_CONTROL(0), mode);

	mode = dwqe_read(sc, GMAC_CHAN_TX_CONTROL(0));
	mode &= ~GMAC_CHAN_TX_CONTROL_PBL_MASK;
	mode |= sc->sc_txpbl << GMAC_CHAN_TX_CONTROL_PBL_SHIFT;
	mode |= GMAC_CHAN_TX_CONTROL_OSP;
	dwqe_write(sc, GMAC_CHAN_TX_CONTROL(0), mode);
	mode = dwqe_read(sc, GMAC_CHAN_RX_CONTROL(0));
	mode &= ~GMAC_CHAN_RX_CONTROL_RPBL_MASK;
	mode |= sc->sc_rxpbl << GMAC_CHAN_RX_CONTROL_RPBL_SHIFT;
	dwqe_write(sc, GMAC_CHAN_RX_CONTROL(0), mode);

	/* Configure AXI master. */
	if (sc->sc_axi_config) {
		int i;

		mode = dwqe_read(sc, GMAC_SYS_BUS_MODE);

		mode &= ~GMAC_SYS_BUS_MODE_EN_LPI;
		if (sc->sc_lpi_en)
			mode |= GMAC_SYS_BUS_MODE_EN_LPI;
		mode &= ~GMAC_SYS_BUS_MODE_LPI_XIT_FRM;
		if (sc->sc_xit_frm)
			mode |= GMAC_SYS_BUS_MODE_LPI_XIT_FRM;

		mode &= ~GMAC_SYS_BUS_MODE_WR_OSR_LMT_MASK;
		mode |= (sc->sc_wr_osr_lmt << GMAC_SYS_BUS_MODE_WR_OSR_LMT_SHIFT);
		mode &= ~GMAC_SYS_BUS_MODE_RD_OSR_LMT_MASK;
		mode |= (sc->sc_rd_osr_lmt << GMAC_SYS_BUS_MODE_RD_OSR_LMT_SHIFT);

		for (i = 0; i < nitems(sc->sc_blen); i++) {
			switch (sc->sc_blen[i]) {
			case 256:
				mode |= GMAC_SYS_BUS_MODE_BLEN_256;
				break;
			case 128:
				mode |= GMAC_SYS_BUS_MODE_BLEN_128;
				break;
			case 64:
				mode |= GMAC_SYS_BUS_MODE_BLEN_64;
				break;
			case 32:
				mode |= GMAC_SYS_BUS_MODE_BLEN_32;
				break;
			case 16:
				mode |= GMAC_SYS_BUS_MODE_BLEN_16;
				break;
			case 8:
				mode |= GMAC_SYS_BUS_MODE_BLEN_8;
				break;
			case 4:
				mode |= GMAC_SYS_BUS_MODE_BLEN_4;
				break;
			}
		}

		dwqe_write(sc, GMAC_SYS_BUS_MODE, mode);
	}

	if (!sc->sc_fixed_link)
		dwqe_mii_attach(sc);

	/*
	 * All devices support VLAN tag stripping on Rx but inserting
	 * VLAN tags during Tx is an optional feature.
	 */
	dwqe_set_vlan_rx_mode(sc);
	if (ifp->if_capabilities & IFCAP_VLAN_HWTAGGING)
		dwqe_set_vlan_tx_mode(sc);

	if_attach(ifp);
	ether_ifattach(ifp);

	/* Disable interrupts. */
	dwqe_write(sc, GMAC_INT_EN, 0);
	dwqe_write(sc, GMAC_CHAN_INTR_ENA(0), 0);
	dwqe_write(sc, GMAC_MMC_RX_INT_MASK, 0xffffffff); 
	dwqe_write(sc, GMAC_MMC_TX_INT_MASK, 0xffffffff); 

	return 0;
}

void
dwqe_mii_attach(struct dwqe_softc *sc)
{
	int mii_flags = 0;

	switch (sc->sc_phy_mode) {
	case DWQE_PHY_MODE_RGMII:
		mii_flags |= MIIF_SETDELAY;
		break;
	case DWQE_PHY_MODE_RGMII_ID:
		mii_flags |= MIIF_SETDELAY | MIIF_RXID | MIIF_TXID;
		break;
	case DWQE_PHY_MODE_RGMII_RXID:
		mii_flags |= MIIF_SETDELAY | MIIF_RXID;
		break;
	case DWQE_PHY_MODE_RGMII_TXID:
		mii_flags |= MIIF_SETDELAY | MIIF_TXID;
		break;
	default:
		break;
	}

	mii_attach(&sc->sc_dev, &sc->sc_mii, 0xffffffff, sc->sc_phyloc,
	    (sc->sc_phyloc == MII_PHY_ANY) ? 0 : MII_OFFSET_ANY, mii_flags);
	if (LIST_FIRST(&sc->sc_mii.mii_phys) == NULL) {
		printf("%s: no PHY found!\n", sc->sc_dev.dv_xname);
		ifmedia_add(&sc->sc_media, IFM_ETHER|IFM_MANUAL, 0, NULL);
		ifmedia_set(&sc->sc_media, IFM_ETHER|IFM_MANUAL);
	} else
		ifmedia_set(&sc->sc_media, IFM_ETHER|IFM_AUTO);
}

uint32_t
dwqe_read(struct dwqe_softc *sc, bus_addr_t addr)
{
	return bus_space_read_4(sc->sc_iot, sc->sc_ioh, addr);
}

void
dwqe_write(struct dwqe_softc *sc, bus_addr_t addr, uint32_t data)
{
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, addr, data);
}

void
dwqe_lladdr_read(struct dwqe_softc *sc, uint8_t *lladdr)
{
	uint32_t machi, maclo;

	machi = dwqe_read(sc, GMAC_MAC_ADDR0_HI);
	maclo = dwqe_read(sc, GMAC_MAC_ADDR0_LO);

	if (machi || maclo) {
		lladdr[0] = (maclo >> 0) & 0xff;
		lladdr[1] = (maclo >> 8) & 0xff;
		lladdr[2] = (maclo >> 16) & 0xff;
		lladdr[3] = (maclo >> 24) & 0xff;
		lladdr[4] = (machi >> 0) & 0xff;
		lladdr[5] = (machi >> 8) & 0xff;
	} else {
		ether_fakeaddr(&sc->sc_ac.ac_if);
	}
}

void
dwqe_lladdr_write(struct dwqe_softc *sc)
{
	dwqe_write(sc, GMAC_MAC_ADDR0_HI,
	    sc->sc_lladdr[5] << 8 | sc->sc_lladdr[4] << 0);
	dwqe_write(sc, GMAC_MAC_ADDR0_LO,
	    sc->sc_lladdr[3] << 24 | sc->sc_lladdr[2] << 16 |
	    sc->sc_lladdr[1] << 8 | sc->sc_lladdr[0] << 0);
}

void
dwqe_start(struct ifqueue *ifq)
{
	struct ifnet *ifp = ifq->ifq_if;
	struct dwqe_softc *sc = ifp->if_softc;
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
		left += DWQE_NTXDESC;
	left -= idx;
	used = 0;

	for (;;) {
		/* VLAN tags require an extra Tx context descriptor. */
		if (used + DWQE_NTXSEGS + 2 > left) {
			ifq_set_oactive(ifq);
			break;
		}

		m = ifq_dequeue(ifq);
		if (m == NULL)
			break;

		error = dwqe_encap(sc, m, &idx, &used);
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

	if (used > 0) {
		sc->sc_tx_prod = idx;

		/* Set a timeout in case the chip goes out to lunch. */
		ifp->if_timer = 5;

		/*
		 * Start the transmit process after the last in-use Tx
		 * descriptor's OWN bit has been updated.
		 */
		dwqe_write(sc, GMAC_CHAN_TX_END_ADDR(0), DWQE_DMA_DVA(sc->sc_txring) +
		    idx * sizeof(struct dwqe_desc));
	}
}

int
dwqe_ioctl(struct ifnet *ifp, u_long cmd, caddr_t addr)
{
	struct dwqe_softc *sc = ifp->if_softc;
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
				dwqe_up(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				dwqe_down(sc);
		}
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		if (sc->sc_fixed_link)
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
			dwqe_iff(sc);
		error = 0;
	}

	splx(s);
	return (error);
}

void
dwqe_watchdog(struct ifnet *ifp)
{
	printf("%s\n", __func__);
}

int
dwqe_media_change(struct ifnet *ifp)
{
	struct dwqe_softc *sc = ifp->if_softc;

	if (LIST_FIRST(&sc->sc_mii.mii_phys))
		mii_mediachg(&sc->sc_mii);

	return (0);
}

void
dwqe_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct dwqe_softc *sc = ifp->if_softc;

	if (LIST_FIRST(&sc->sc_mii.mii_phys)) {
		mii_pollstat(&sc->sc_mii);
		ifmr->ifm_active = sc->sc_mii.mii_media_active;
		ifmr->ifm_status = sc->sc_mii.mii_media_status;
	}
}

int
dwqe_mii_readreg(struct device *self, int phy, int reg)
{
	struct dwqe_softc *sc = (void *)self;
	int n;

	dwqe_write(sc, GMAC_MAC_MDIO_ADDR,
	    (sc->sc_clk << GMAC_MAC_MDIO_ADDR_CR_SHIFT) |
	    (phy << GMAC_MAC_MDIO_ADDR_PA_SHIFT) |
	    (reg << GMAC_MAC_MDIO_ADDR_RDA_SHIFT) |
	    GMAC_MAC_MDIO_ADDR_GOC_READ |
	    GMAC_MAC_MDIO_ADDR_GB);

	for (n = 0; n < 2000; n++) {
		delay(10);
		if ((dwqe_read(sc, GMAC_MAC_MDIO_ADDR) & GMAC_MAC_MDIO_ADDR_GB) == 0)
			return dwqe_read(sc, GMAC_MAC_MDIO_DATA);
	}

	printf("%s: mii_read timeout\n", sc->sc_dev.dv_xname);
	return (0);
}

void
dwqe_mii_writereg(struct device *self, int phy, int reg, int val)
{
	struct dwqe_softc *sc = (void *)self;
	int n;

	dwqe_write(sc, GMAC_MAC_MDIO_DATA, val);
	dwqe_write(sc, GMAC_MAC_MDIO_ADDR,
	    (sc->sc_clk << GMAC_MAC_MDIO_ADDR_CR_SHIFT) |
	    (phy << GMAC_MAC_MDIO_ADDR_PA_SHIFT) |
	    (reg << GMAC_MAC_MDIO_ADDR_RDA_SHIFT) |
	    GMAC_MAC_MDIO_ADDR_GOC_WRITE |
	    GMAC_MAC_MDIO_ADDR_GB);

	for (n = 0; n < 2000; n++) {
		delay(10);
		if ((dwqe_read(sc, GMAC_MAC_MDIO_ADDR) & GMAC_MAC_MDIO_ADDR_GB) == 0)
			return;
	}

	printf("%s: mii_write timeout\n", sc->sc_dev.dv_xname);
}

void
dwqe_mii_statchg(struct device *self)
{
	struct dwqe_softc *sc = (void *)self;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	uint32_t conf;

	conf = dwqe_read(sc, GMAC_MAC_CONF);
	conf &= ~(GMAC_MAC_CONF_PS | GMAC_MAC_CONF_FES);

	switch (ifp->if_baudrate) {
	case IF_Mbps(1000):
		sc->sc_link = 1;
		break;
	case IF_Mbps(100):
		conf |= GMAC_MAC_CONF_PS | GMAC_MAC_CONF_FES;
		sc->sc_link = 1;
		break;
	case IF_Mbps(10):
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
	if (ifp->if_link_state == LINK_STATE_FULL_DUPLEX)
		conf |= GMAC_MAC_CONF_DM;

	dwqe_write(sc, GMAC_MAC_CONF, conf);
}

void
dwqe_tick(void *arg)
{
	struct dwqe_softc *sc = arg;
	int s;

	s = splnet();
	mii_tick(&sc->sc_mii);
	splx(s);

	timeout_add_sec(&sc->sc_phy_tick, 1);
}

void
dwqe_rxtick(void *arg)
{
	struct dwqe_softc *sc = arg;
	int s;

	s = splnet();

	/* TODO: disable RXQ? */
	printf("%s:%d\n", __func__, __LINE__);

	bus_dmamap_sync(sc->sc_dmat, DWQE_DMA_MAP(sc->sc_rxring),
	    0, DWQE_DMA_LEN(sc->sc_rxring),
	    BUS_DMASYNC_POSTREAD|BUS_DMASYNC_POSTWRITE);

	dwqe_write(sc, GMAC_CHAN_RX_BASE_ADDR_HI(0), 0);
	dwqe_write(sc, GMAC_CHAN_RX_BASE_ADDR(0), 0);

	sc->sc_rx_prod = sc->sc_rx_cons = 0;
	dwqe_fill_rx_ring(sc);

	bus_dmamap_sync(sc->sc_dmat, DWQE_DMA_MAP(sc->sc_rxring),
	    0, DWQE_DMA_LEN(sc->sc_rxring),
	    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

	dwqe_write(sc, GMAC_CHAN_RX_BASE_ADDR_HI(0), DWQE_DMA_DVA(sc->sc_rxring) >> 32);
	dwqe_write(sc, GMAC_CHAN_RX_BASE_ADDR(0), DWQE_DMA_DVA(sc->sc_rxring));

	/* TODO: re-enable RXQ? */

	splx(s);
}

int
dwqe_intr(void *arg)
{
	struct dwqe_softc *sc = arg;
	uint32_t reg;

	reg = dwqe_read(sc, GMAC_INT_STATUS);
	dwqe_write(sc, GMAC_INT_STATUS, reg);

	reg = dwqe_read(sc, GMAC_CHAN_STATUS(0));
	dwqe_write(sc, GMAC_CHAN_STATUS(0), reg);

	if (reg & GMAC_CHAN_STATUS_RI)
		dwqe_rx_proc(sc);

	if (reg & GMAC_CHAN_STATUS_TI)
		dwqe_tx_proc(sc);

	return (1);
}

void
dwqe_tx_proc(struct dwqe_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct dwqe_desc *txd;
	struct dwqe_buf *txb;
	int idx, txfree;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	bus_dmamap_sync(sc->sc_dmat, DWQE_DMA_MAP(sc->sc_txring), 0,
	    DWQE_DMA_LEN(sc->sc_txring),
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	txfree = 0;
	while (sc->sc_tx_cons != sc->sc_tx_prod) {
		idx = sc->sc_tx_cons;
		KASSERT(idx < DWQE_NTXDESC);

		txd = &sc->sc_txdesc[idx];
		if (txd->sd_tdes3 & TDES3_OWN)
			break;

		if (txd->sd_tdes3 & TDES3_ES)
			ifp->if_oerrors++;

		txb = &sc->sc_txbuf[idx];
		if (txb->tb_m) {
			bus_dmamap_sync(sc->sc_dmat, txb->tb_map, 0,
			    txb->tb_map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, txb->tb_map);

			m_freem(txb->tb_m);
			txb->tb_m = NULL;
		}

		txfree++;

		if (sc->sc_tx_cons == (DWQE_NTXDESC - 1))
			sc->sc_tx_cons = 0;
		else
			sc->sc_tx_cons++;

		txd->sd_tdes3 = 0;
	}

	if (sc->sc_tx_cons == sc->sc_tx_prod)
		ifp->if_timer = 0;

	if (txfree) {
		if (ifq_is_oactive(&ifp->if_snd))
			ifq_restart(&ifp->if_snd);
	}
}

int
dwqe_have_rx_csum_offload(struct dwqe_softc *sc)
{
	return (sc->sc_hw_feature[0] & GMAC_MAC_HW_FEATURE0_RXCOESEL);
}

void
dwqe_rx_csum(struct dwqe_softc *sc, struct mbuf *m, struct dwqe_desc *rxd)
{
	uint16_t csum_flags = 0;

	/*
	 * Checksum offload must be supported, the Last-Descriptor bit
	 * must be set, RDES1 must be valid, and checksumming must not
	 * have been bypassed (happens for unknown packet types), and
	 * an IP header must have been detected.
	 */
	if (!dwqe_have_rx_csum_offload(sc) ||
	    (rxd->sd_tdes3 & RDES3_LD) == 0 ||
	    (rxd->sd_tdes3 & RDES3_RDES1_VALID) == 0 ||
	    (rxd->sd_tdes1 & RDES1_IP_CSUM_BYPASS) ||
	    (rxd->sd_tdes1 & (RDES1_IPV4_HDR | RDES1_IPV6_HDR)) == 0)
		return;

	/* If the IP header checksum is invalid then the payload is ignored. */
	if (rxd->sd_tdes1 & RDES1_IP_HDR_ERROR) {
		if (rxd->sd_tdes1 & RDES1_IPV4_HDR)
			csum_flags |= M_IPV4_CSUM_IN_BAD;
	} else {
		if (rxd->sd_tdes1 & RDES1_IPV4_HDR)
			csum_flags |= M_IPV4_CSUM_IN_OK;

		/* Detect payload type and corresponding checksum errors. */
		switch (rxd->sd_tdes1 & RDES1_IP_PAYLOAD_TYPE) {
		case RDES1_IP_PAYLOAD_UDP:
			if (rxd->sd_tdes1 & RDES1_IP_PAYLOAD_ERROR)
				csum_flags |= M_UDP_CSUM_IN_BAD;
			else
				csum_flags |= M_UDP_CSUM_IN_OK;
			break;
		case RDES1_IP_PAYLOAD_TCP:
			if (rxd->sd_tdes1 & RDES1_IP_PAYLOAD_ERROR)
				csum_flags |= M_TCP_CSUM_IN_BAD;
			else
				csum_flags |= M_TCP_CSUM_IN_OK;
			break;
		case RDES1_IP_PAYLOAD_ICMP:
			if (rxd->sd_tdes1 & RDES1_IP_PAYLOAD_ERROR)
				csum_flags |= M_ICMP_CSUM_IN_BAD;
			else
				csum_flags |= M_ICMP_CSUM_IN_OK;
			break;
		default:
			break;
		}
	}

	m->m_pkthdr.csum_flags |= csum_flags;
}

void
dwqe_vlan_strip(struct dwqe_softc *sc, struct mbuf *m, struct dwqe_desc *rxd)
{
#if NVLAN > 0
	uint16_t tag;

	if ((rxd->sd_tdes3 & RDES3_RDES0_VALID) &&
	    (rxd->sd_tdes3 & RDES3_LD)) {
		tag = rxd->sd_tdes0 & RDES0_OVT;
		m->m_pkthdr.ether_vtag = le16toh(tag);
		m->m_flags |= M_VLANTAG;
	}
#endif
}

void
dwqe_rx_proc(struct dwqe_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct dwqe_desc *rxd;
	struct dwqe_buf *rxb;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *m;
	int idx, len, cnt, put;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	bus_dmamap_sync(sc->sc_dmat, DWQE_DMA_MAP(sc->sc_rxring), 0,
	    DWQE_DMA_LEN(sc->sc_rxring),
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

	cnt = if_rxr_inuse(&sc->sc_rx_ring);
	put = 0;
	while (put < cnt) {
		idx = sc->sc_rx_cons;
		KASSERT(idx < DWQE_NRXDESC);

		rxd = &sc->sc_rxdesc[idx];
		if (rxd->sd_tdes3 & RDES3_OWN)
			break;

		len = rxd->sd_tdes3 & RDES3_LENGTH;
		rxb = &sc->sc_rxbuf[idx];
		KASSERT(rxb->tb_m);

		bus_dmamap_sync(sc->sc_dmat, rxb->tb_map, 0,
		    len, BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_dmat, rxb->tb_map);

		m = rxb->tb_m;
		rxb->tb_m = NULL;

		if (rxd->sd_tdes3 & RDES3_ES) {
			ifp->if_ierrors++;
			m_freem(m);
		} else {
			/* Strip off CRC. */
			len -= ETHER_CRC_LEN;
			KASSERT(len > 0);

			m->m_pkthdr.len = m->m_len = len;

			dwqe_rx_csum(sc, m, rxd);
			dwqe_vlan_strip(sc, m, rxd);
			ml_enqueue(&ml, m);
		}

		put++;
		if (sc->sc_rx_cons == (DWQE_NRXDESC - 1))
			sc->sc_rx_cons = 0;
		else
			sc->sc_rx_cons++;
	}

	if_rxr_put(&sc->sc_rx_ring, put);
	if (ifiq_input(&ifp->if_rcv, &ml))
		if_rxr_livelocked(&sc->sc_rx_ring);

	dwqe_fill_rx_ring(sc);

	bus_dmamap_sync(sc->sc_dmat, DWQE_DMA_MAP(sc->sc_rxring), 0,
	    DWQE_DMA_LEN(sc->sc_rxring),
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
}

void
dwqe_up(struct dwqe_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct dwqe_buf *txb, *rxb;
	uint32_t mode, reg, fifosz, tqs, rqs;
	int i;

	/* Allocate Tx descriptor ring. */
	sc->sc_txring = dwqe_dmamem_alloc(sc,
	    DWQE_NTXDESC * sizeof(struct dwqe_desc), 8);
	sc->sc_txdesc = DWQE_DMA_KVA(sc->sc_txring);

	sc->sc_txbuf = malloc(sizeof(struct dwqe_buf) * DWQE_NTXDESC,
	    M_DEVBUF, M_WAITOK);
	for (i = 0; i < DWQE_NTXDESC; i++) {
		txb = &sc->sc_txbuf[i];
		bus_dmamap_create(sc->sc_dmat, MCLBYTES, DWQE_NTXSEGS,
		    MCLBYTES, 0, BUS_DMA_WAITOK, &txb->tb_map);
		txb->tb_m = NULL;
	}

	bus_dmamap_sync(sc->sc_dmat, DWQE_DMA_MAP(sc->sc_txring),
	    0, DWQE_DMA_LEN(sc->sc_txring), BUS_DMASYNC_PREWRITE);

	sc->sc_tx_prod = sc->sc_tx_cons = 0;

	dwqe_write(sc, GMAC_CHAN_TX_BASE_ADDR_HI(0), DWQE_DMA_DVA(sc->sc_txring) >> 32);
	dwqe_write(sc, GMAC_CHAN_TX_BASE_ADDR(0), DWQE_DMA_DVA(sc->sc_txring));
	dwqe_write(sc, GMAC_CHAN_TX_RING_LEN(0), DWQE_NTXDESC - 1);
	dwqe_write(sc, GMAC_CHAN_TX_END_ADDR(0), DWQE_DMA_DVA(sc->sc_txring));

	/* Allocate  descriptor ring. */
	sc->sc_rxring = dwqe_dmamem_alloc(sc,
	    DWQE_NRXDESC * sizeof(struct dwqe_desc), 8);
	sc->sc_rxdesc = DWQE_DMA_KVA(sc->sc_rxring);

	sc->sc_rxbuf = malloc(sizeof(struct dwqe_buf) * DWQE_NRXDESC,
	    M_DEVBUF, M_WAITOK);

	for (i = 0; i < DWQE_NRXDESC; i++) {
		rxb = &sc->sc_rxbuf[i];
		bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1,
		    MCLBYTES, 0, BUS_DMA_WAITOK, &rxb->tb_map);
		rxb->tb_m = NULL;
	}

	if_rxr_init(&sc->sc_rx_ring, 2, DWQE_NRXDESC - 1);

	dwqe_write(sc, GMAC_CHAN_RX_BASE_ADDR_HI(0), DWQE_DMA_DVA(sc->sc_rxring) >> 32);
	dwqe_write(sc, GMAC_CHAN_RX_BASE_ADDR(0), DWQE_DMA_DVA(sc->sc_rxring));
	dwqe_write(sc, GMAC_CHAN_RX_RING_LEN(0), DWQE_NRXDESC - 1);

	sc->sc_rx_prod = sc->sc_rx_cons = 0;
	dwqe_fill_rx_ring(sc);

	bus_dmamap_sync(sc->sc_dmat, DWQE_DMA_MAP(sc->sc_rxring),
	    0, DWQE_DMA_LEN(sc->sc_rxring),
	    BUS_DMASYNC_PREREAD|BUS_DMASYNC_PREWRITE);

	dwqe_lladdr_write(sc);

	/* Configure media. */
	if (LIST_FIRST(&sc->sc_mii.mii_phys))
		mii_mediachg(&sc->sc_mii);

	/* Program promiscuous mode and multicast filters. */
	dwqe_iff(sc);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	dwqe_write(sc, GMAC_MAC_1US_TIC_CTR, (sc->sc_clkrate / 1000000) - 1);

	/* Start receive DMA */
	reg = dwqe_read(sc, GMAC_CHAN_RX_CONTROL(0));
	reg |= GMAC_CHAN_RX_CONTROL_SR;
	dwqe_write(sc, GMAC_CHAN_RX_CONTROL(0), reg);

	/* Start transmit DMA */
	reg = dwqe_read(sc, GMAC_CHAN_TX_CONTROL(0));
	reg |= GMAC_CHAN_TX_CONTROL_ST;
	dwqe_write(sc, GMAC_CHAN_TX_CONTROL(0), reg);

	mode = dwqe_read(sc, GMAC_MTL_CHAN_RX_OP_MODE(0));
	if (sc->sc_force_thresh_dma_mode) {
		mode &= ~GMAC_MTL_CHAN_RX_OP_MODE_RSF;
		mode &= ~GMAC_MTL_CHAN_RX_OP_MODE_RTC_MASK;
		mode |= GMAC_MTL_CHAN_RX_OP_MODE_RTC_128;
	} else {
		mode |= GMAC_MTL_CHAN_RX_OP_MODE_RSF;
	}
	mode &= ~GMAC_MTL_CHAN_RX_OP_MODE_RQS_MASK;
	if (sc->sc_rxfifo_size)
		fifosz = sc->sc_rxfifo_size;
	else
		fifosz = (128 <<
		    GMAC_MAC_HW_FEATURE1_RXFIFOSIZE(sc->sc_hw_feature[1]));
	rqs = fifosz / 256 - 1;
	mode |= (rqs << GMAC_MTL_CHAN_RX_OP_MODE_RQS_SHIFT) &
	   GMAC_MTL_CHAN_RX_OP_MODE_RQS_MASK;
	if (fifosz >= 4096) {
		mode |= GMAC_MTL_CHAN_RX_OP_MODE_EHFC; 
		mode &= ~GMAC_MTL_CHAN_RX_OP_MODE_RFD_MASK;
		mode |= 0x3 << GMAC_MTL_CHAN_RX_OP_MODE_RFD_SHIFT;
		mode &= ~GMAC_MTL_CHAN_RX_OP_MODE_RFA_MASK;
		mode |= 0x1 << GMAC_MTL_CHAN_RX_OP_MODE_RFA_SHIFT;
	}
	dwqe_write(sc, GMAC_MTL_CHAN_RX_OP_MODE(0), mode);

	mode = dwqe_read(sc, GMAC_MTL_CHAN_TX_OP_MODE(0));
	if (sc->sc_force_thresh_dma_mode) {
		mode &= ~GMAC_MTL_CHAN_TX_OP_MODE_TSF;
		mode &= ~GMAC_MTL_CHAN_TX_OP_MODE_TTC_MASK;
		mode |= GMAC_MTL_CHAN_TX_OP_MODE_TTC_512;
	} else {
		mode |= GMAC_MTL_CHAN_TX_OP_MODE_TSF;
	}
	mode &= ~GMAC_MTL_CHAN_TX_OP_MODE_TXQEN_MASK;
	mode |= GMAC_MTL_CHAN_TX_OP_MODE_TXQEN;
	mode &= ~GMAC_MTL_CHAN_TX_OP_MODE_TQS_MASK;
	if (sc->sc_txfifo_size)
		fifosz = sc->sc_txfifo_size;
	else
		fifosz = (128 <<
		    GMAC_MAC_HW_FEATURE1_TXFIFOSIZE(sc->sc_hw_feature[1]));
	tqs = (fifosz / 256) - 1;
	mode |= (tqs << GMAC_MTL_CHAN_TX_OP_MODE_TQS_SHIFT) &
	    GMAC_MTL_CHAN_TX_OP_MODE_TQS_MASK;
	dwqe_write(sc, GMAC_MTL_CHAN_TX_OP_MODE(0), mode);

	reg = dwqe_read(sc, GMAC_QX_TX_FLOW_CTRL(0));
	reg |= 0xffffU << GMAC_QX_TX_FLOW_CTRL_PT_SHIFT;
	reg |= GMAC_QX_TX_FLOW_CTRL_TFE;
	dwqe_write(sc, GMAC_QX_TX_FLOW_CTRL(0), reg);
	reg = dwqe_read(sc, GMAC_RX_FLOW_CTRL);
	reg |= GMAC_RX_FLOW_CTRL_RFE;
	dwqe_write(sc, GMAC_RX_FLOW_CTRL, reg);

	dwqe_write(sc, GMAC_RXQ_CTRL0, GMAC_RXQ_CTRL0_DCB_QUEUE_EN(0));

	dwqe_write(sc, GMAC_MAC_CONF, dwqe_read(sc, GMAC_MAC_CONF) |
	    GMAC_MAC_CONF_BE | GMAC_MAC_CONF_JD | GMAC_MAC_CONF_JE |
	    GMAC_MAC_CONF_DCRS | GMAC_MAC_CONF_TE | GMAC_MAC_CONF_RE);

	dwqe_write(sc, GMAC_CHAN_INTR_ENA(0),
	    GMAC_CHAN_INTR_ENA_NIE |
	    GMAC_CHAN_INTR_ENA_AIE |
	    GMAC_CHAN_INTR_ENA_FBE |
	    GMAC_CHAN_INTR_ENA_RIE |
	    GMAC_CHAN_INTR_ENA_TIE);

	if (!sc->sc_fixed_link)
		timeout_add_sec(&sc->sc_phy_tick, 1);

	if (dwqe_have_rx_csum_offload(sc)) {
		reg = dwqe_read(sc, GMAC_MAC_CONF);
		reg |= GMAC_MAC_CONF_IPC;
		dwqe_write(sc, GMAC_MAC_CONF, reg);
	}
}

void
dwqe_down(struct dwqe_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct dwqe_buf *txb, *rxb;
	uint32_t reg;
	int i;

	timeout_del(&sc->sc_rxto);
	if (!sc->sc_fixed_link)
		timeout_del(&sc->sc_phy_tick);

	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;

	/* Disable receiver */
	reg = dwqe_read(sc, GMAC_MAC_CONF);
	reg &= ~GMAC_MAC_CONF_RE;
	dwqe_write(sc, GMAC_MAC_CONF, reg);

	/* Stop receive DMA */
	reg = dwqe_read(sc, GMAC_CHAN_RX_CONTROL(0));
	reg &= ~GMAC_CHAN_RX_CONTROL_SR;
	dwqe_write(sc, GMAC_CHAN_RX_CONTROL(0), reg);

	/* Stop transmit DMA */
	reg = dwqe_read(sc, GMAC_CHAN_TX_CONTROL(0));
	reg &= ~GMAC_CHAN_TX_CONTROL_ST;
	dwqe_write(sc, GMAC_CHAN_TX_CONTROL(0), reg);

	/* Flush data in the TX FIFO */
	reg = dwqe_read(sc, GMAC_MTL_CHAN_TX_OP_MODE(0));
	reg |= GMAC_MTL_CHAN_TX_OP_MODE_FTQ;
	dwqe_write(sc, GMAC_MTL_CHAN_TX_OP_MODE(0), reg);
	/* Wait for flush to complete */
	for (i = 10000; i > 0; i--) {
		reg = dwqe_read(sc, GMAC_MTL_CHAN_TX_OP_MODE(0));
		if ((reg & GMAC_MTL_CHAN_TX_OP_MODE_FTQ) == 0)
			break;
		delay(1);
	}
	if (i == 0) {
		printf("%s: timeout flushing TX queue\n",
		    sc->sc_dev.dv_xname);
	}

	/* Disable transmitter */
	reg = dwqe_read(sc, GMAC_MAC_CONF);
	reg &= ~GMAC_MAC_CONF_TE;
	dwqe_write(sc, GMAC_MAC_CONF, reg);

	dwqe_write(sc, GMAC_CHAN_INTR_ENA(0), 0);

	intr_barrier(sc->sc_ih);
	ifq_barrier(&ifp->if_snd);

	for (i = 0; i < DWQE_NTXDESC; i++) {
		txb = &sc->sc_txbuf[i];
		if (txb->tb_m) {
			bus_dmamap_sync(sc->sc_dmat, txb->tb_map, 0,
			    txb->tb_map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, txb->tb_map);
			m_freem(txb->tb_m);
		}
		bus_dmamap_destroy(sc->sc_dmat, txb->tb_map);
	}

	dwqe_dmamem_free(sc, sc->sc_txring);
	free(sc->sc_txbuf, M_DEVBUF, 0);

	for (i = 0; i < DWQE_NRXDESC; i++) {
		rxb = &sc->sc_rxbuf[i];
		if (rxb->tb_m) {
			bus_dmamap_sync(sc->sc_dmat, rxb->tb_map, 0,
			    rxb->tb_map->dm_mapsize, BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->sc_dmat, rxb->tb_map);
			m_freem(rxb->tb_m);
		}
		bus_dmamap_destroy(sc->sc_dmat, rxb->tb_map);
	}

	dwqe_dmamem_free(sc, sc->sc_rxring);
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
dwqe_iff(struct dwqe_softc *sc)
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
		reg |= GMAC_MAC_PACKET_FILTER_PM;
		if (ifp->if_flags & IFF_PROMISC)
			reg |= GMAC_MAC_PACKET_FILTER_PR |
			    GMAC_MAC_PACKET_FILTER_PCF_ALL;
	} else {
		reg |= GMAC_MAC_PACKET_FILTER_HMC;
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

	dwqe_lladdr_write(sc);

	dwqe_write(sc, GMAC_MAC_HASH_TAB_REG0, hash[0]);
	dwqe_write(sc, GMAC_MAC_HASH_TAB_REG1, hash[1]);

	dwqe_write(sc, GMAC_MAC_PACKET_FILTER, reg);
}

void
dwqe_tx_csum(struct dwqe_softc *sc, struct mbuf *m, struct dwqe_desc *txd)
{
	if (!dwqe_have_tx_csum_offload(sc))
		return;

	/* Checksum flags are valid only on first descriptor. */
	if ((txd->sd_tdes3 & TDES3_FS) == 0)
		return;

	/* TSO and Tx checksum offloading are incompatible. */
	if (txd->sd_tdes3 & TDES3_TSO_EN)
		return;

	if (m->m_pkthdr.csum_flags & (M_IPV4_CSUM_OUT |
	    M_TCP_CSUM_OUT | M_UDP_CSUM_OUT))
		txd->sd_tdes3 |= TDES3_CSUM_IPHDR_PAYLOAD_PSEUDOHDR;
}

uint16_t
dwqe_set_tx_context_desc(struct dwqe_softc *sc, struct mbuf *m, int idx)
{
	uint16_t tag = 0;
#if NVLAN > 0
	struct dwqe_desc *ctxt_txd;

	if ((m->m_flags & M_VLANTAG) == 0)
		return 0;

	tag = m->m_pkthdr.ether_vtag;
	if (tag) {
		ctxt_txd = &sc->sc_txdesc[idx];
		ctxt_txd->sd_tdes3 |= (htole16(tag) & TDES3_VLAN_TAG);
		ctxt_txd->sd_tdes3 |= TDES3_VLAN_TAG_VALID;
		ctxt_txd->sd_tdes3 |= (TDES3_CTXT | TDES3_OWN);
	}
#endif
	return tag;
}

int
dwqe_encap(struct dwqe_softc *sc, struct mbuf *m, int *idx, int *used)
{
	struct dwqe_desc *txd, *txd_start;
	bus_dmamap_t map;
	int cur, frag, i;
	uint16_t vlan_tag = 0;

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

	if (dwqe_have_tx_vlan_offload(sc)) {
		vlan_tag = dwqe_set_tx_context_desc(sc, m, frag);
		if (vlan_tag) {
			(*used)++;
			if (frag == (DWQE_NTXDESC - 1))
				frag = 0;
			else
				frag++;
		}
	}

	txd = txd_start = &sc->sc_txdesc[frag];
	for (i = 0; i < map->dm_nsegs; i++) {
		/* TODO: check for 32-bit vs 64-bit support */
		KASSERT((map->dm_segs[i].ds_addr >> 32) == 0);

		txd->sd_tdes0 = (uint32_t)map->dm_segs[i].ds_addr;
		txd->sd_tdes1 = (uint32_t)(map->dm_segs[i].ds_addr >> 32);
		txd->sd_tdes2 = map->dm_segs[i].ds_len;
		txd->sd_tdes3 = m->m_pkthdr.len;
		if (i == 0) {
			txd->sd_tdes3 |= TDES3_FS;
			dwqe_tx_csum(sc, m, txd);
			if (vlan_tag)
				txd->sd_tdes2 |= TDES2_VLAN_TAG_INSERT;
		}
		if (i == (map->dm_nsegs - 1)) {
			txd->sd_tdes2 |= TDES2_IC;
			txd->sd_tdes3 |= TDES3_LS;
		}
		if (i != 0)
			txd->sd_tdes3 |= TDES3_OWN;

		bus_dmamap_sync(sc->sc_dmat, DWQE_DMA_MAP(sc->sc_txring),
		    frag * sizeof(*txd), sizeof(*txd), BUS_DMASYNC_PREWRITE);

		cur = frag;
		if (frag == (DWQE_NTXDESC - 1)) {
			txd = &sc->sc_txdesc[0];
			frag = 0;
		} else {
			txd++;
			frag++;
		}
		KASSERT(frag != sc->sc_tx_cons);
	}

	txd_start->sd_tdes3 |= TDES3_OWN;
	bus_dmamap_sync(sc->sc_dmat, DWQE_DMA_MAP(sc->sc_txring),
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
dwqe_reset(struct dwqe_softc *sc)
{
	int n;

	dwqe_write(sc, GMAC_BUS_MODE, dwqe_read(sc, GMAC_BUS_MODE) |
	    GMAC_BUS_MODE_SWR);

	for (n = 0; n < 30000; n++) {
		if ((dwqe_read(sc, GMAC_BUS_MODE) &
		    GMAC_BUS_MODE_SWR) == 0)
			return;
		delay(10);
	}

	printf("%s: reset timeout\n", sc->sc_dev.dv_xname);
}

struct dwqe_dmamem *
dwqe_dmamem_alloc(struct dwqe_softc *sc, bus_size_t size, bus_size_t align)
{
	struct dwqe_dmamem *tdm;
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
dwqe_dmamem_free(struct dwqe_softc *sc, struct dwqe_dmamem *tdm)
{
	bus_dmamem_unmap(sc->sc_dmat, tdm->tdm_kva, tdm->tdm_size);
	bus_dmamem_free(sc->sc_dmat, &tdm->tdm_seg, 1);
	bus_dmamap_destroy(sc->sc_dmat, tdm->tdm_map);
	free(tdm, M_DEVBUF, 0);
}

struct mbuf *
dwqe_alloc_mbuf(struct dwqe_softc *sc, bus_dmamap_t map)
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
dwqe_fill_rx_ring(struct dwqe_softc *sc)
{
	struct dwqe_desc *rxd;
	struct dwqe_buf *rxb;
	u_int slots;

	for (slots = if_rxr_get(&sc->sc_rx_ring, DWQE_NRXDESC);
	    slots > 0; slots--) {
		rxb = &sc->sc_rxbuf[sc->sc_rx_prod];
		rxb->tb_m = dwqe_alloc_mbuf(sc, rxb->tb_map);
		if (rxb->tb_m == NULL)
			break;

		/* TODO: check for 32-bit vs 64-bit support */
		KASSERT((rxb->tb_map->dm_segs[0].ds_addr >> 32) == 0);

		rxd = &sc->sc_rxdesc[sc->sc_rx_prod];
		rxd->sd_tdes0 = (uint32_t)rxb->tb_map->dm_segs[0].ds_addr;
		rxd->sd_tdes1 = (uint32_t)(rxb->tb_map->dm_segs[0].ds_addr >> 32);
		rxd->sd_tdes2 = 0;
		rxd->sd_tdes3 = RDES3_OWN | RDES3_IC | RDES3_BUF1V;

		if (sc->sc_rx_prod == (DWQE_NRXDESC - 1))
			sc->sc_rx_prod = 0;
		else
			sc->sc_rx_prod++;
	}
	if_rxr_put(&sc->sc_rx_ring, slots);

	dwqe_write(sc, GMAC_CHAN_RX_END_ADDR(0), DWQE_DMA_DVA(sc->sc_rxring) +
	    sc->sc_rx_prod * sizeof(*rxd));

	if (if_rxr_inuse(&sc->sc_rx_ring) == 0)
		timeout_add(&sc->sc_rxto, 1);
}
