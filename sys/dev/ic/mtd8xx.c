/*	$OpenBSD: mtd8xx.c,v 1.36 2024/11/05 18:58:59 miod Exp $	*/

/*
 * Copyright (c) 2003 Oleg Safiullin <form@pdp11.org.ru>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <net/if.h>
#include <net/if_media.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <machine/bus.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/pci/pcidevs.h>

#include <dev/ic/mtd8xxreg.h>
#include <dev/ic/mtd8xxvar.h>


static int mtd_ifmedia_upd(struct ifnet *);
static void mtd_ifmedia_sts(struct ifnet *, struct ifmediareq *);

static u_int32_t mtd_mii_command(struct mtd_softc *, int, int, int);
static int mtd_miibus_readreg(struct device *, int, int);
static void mtd_miibus_writereg(struct device *, int, int, int);
static void mtd_miibus_statchg(struct device *);
static void mtd_setmulti(struct mtd_softc *);

static int mtd_encap(struct mtd_softc *, struct mbuf *, u_int32_t *);
static int mtd_list_rx_init(struct mtd_softc *);
static void mtd_list_tx_init(struct mtd_softc *);
static int mtd_newbuf(struct mtd_softc *, int, struct mbuf *);

static void mtd_reset(struct mtd_softc *sc);
static int mtd_ioctl(struct ifnet *, u_long, caddr_t);
static void mtd_init(struct ifnet *);
static void mtd_start(struct ifnet *);
static void mtd_stop(struct ifnet *);
static void mtd_watchdog(struct ifnet *);

static int mtd_rxeof(struct mtd_softc *);
static int mtd_rx_resync(struct mtd_softc *);
static void mtd_txeof(struct mtd_softc *);


void
mtd_attach(struct mtd_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	u_int32_t enaddr[2];
	int i;

	/* Reset the adapter. */
	mtd_reset(sc);

	if (bus_dmamem_alloc(sc->sc_dmat, sizeof(struct mtd_list_data),
	    PAGE_SIZE, 0, sc->sc_listseg, 1, &sc->sc_listnseg,
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO) != 0) {
		printf(": can't alloc list mem\n");
		return;
	}
	if (bus_dmamem_map(sc->sc_dmat, sc->sc_listseg, sc->sc_listnseg,
	    sizeof(struct mtd_list_data), &sc->sc_listkva,
	    BUS_DMA_NOWAIT) != 0) {
		printf(": can't map list mem\n");
		return;
	}
	if (bus_dmamap_create(sc->sc_dmat, sizeof(struct mtd_list_data), 1,
	    sizeof(struct mtd_list_data), 0, BUS_DMA_NOWAIT,
	    &sc->sc_listmap) != 0) {
		printf(": can't alloc list map\n");
		return;
	}
	if (bus_dmamap_load(sc->sc_dmat, sc->sc_listmap, sc->sc_listkva,
	    sizeof(struct mtd_list_data), NULL, BUS_DMA_NOWAIT) != 0) {
		printf(": can't load list map\n");
		return;
	}
	sc->mtd_ldata = (struct mtd_list_data *)sc->sc_listkva;

	for (i = 0; i < MTD_RX_LIST_CNT; i++) {
		if (bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1, MCLBYTES,
		    0, BUS_DMA_NOWAIT,
		    &sc->mtd_cdata.mtd_rx_chain[i].sd_map) != 0) {
			printf(": can't create rx map\n");
			return;
		}
	}
	if (bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1, MCLBYTES, 0,
	    BUS_DMA_NOWAIT, &sc->sc_rx_sparemap) != 0) {
		printf(": can't create rx spare map\n");
		return;
	}

	for (i = 0; i < MTD_TX_LIST_CNT; i++) {
		if (bus_dmamap_create(sc->sc_dmat, MCLBYTES,
		    MTD_TX_LIST_CNT - 5, MCLBYTES, 0, BUS_DMA_NOWAIT,
		    &sc->mtd_cdata.mtd_tx_chain[i].sd_map) != 0) {
			printf(": can't create tx map\n");
			return;
		}
	}
	if (bus_dmamap_create(sc->sc_dmat, MCLBYTES, MTD_TX_LIST_CNT - 5,
	    MCLBYTES, 0, BUS_DMA_NOWAIT, &sc->sc_tx_sparemap) != 0) {
		printf(": can't create tx spare map\n");
		return;
	}


	/* Get station address. */
	enaddr[0] = letoh32(CSR_READ_4(MTD_PAR0));
	enaddr[1] = letoh32(CSR_READ_4(MTD_PAR4));
	bcopy(enaddr, sc->sc_arpcom.ac_enaddr, ETHER_ADDR_LEN);
	printf(" address %s\n", ether_sprintf(sc->sc_arpcom.ac_enaddr));

	/* Initialize interface */
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = mtd_ioctl;
	ifp->if_start = mtd_start;
	ifp->if_watchdog = mtd_watchdog;
	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

	/*
	 * Initialize our media structures and probe the MII.
	 */
	sc->sc_mii.mii_ifp = ifp;
	sc->sc_mii.mii_readreg = mtd_miibus_readreg;
	sc->sc_mii.mii_writereg = mtd_miibus_writereg;
	sc->sc_mii.mii_statchg = mtd_miibus_statchg;
	ifmedia_init(&sc->sc_mii.mii_media, 0, mtd_ifmedia_upd,
	    mtd_ifmedia_sts);
	mii_attach(&sc->sc_dev, &sc->sc_mii, 0xffffffff, MII_PHY_ANY,
	    MII_OFFSET_ANY, 0);
	if (LIST_FIRST(&sc->sc_mii.mii_phys) == NULL) {
		ifmedia_add(&sc->sc_mii.mii_media, IFM_ETHER | IFM_NONE, 0,
		    NULL);
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER | IFM_NONE);
	} else
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER | IFM_AUTO);

	/*
	 * Attach us everywhere
	 */
	if_attach(ifp);
	ether_ifattach(ifp);
}


static int
mtd_ifmedia_upd(struct ifnet *ifp)
{
	struct mtd_softc *sc = ifp->if_softc;

	return (mii_mediachg(&sc->sc_mii));
}


static void
mtd_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct mtd_softc *sc = ifp->if_softc;

	mii_pollstat(&sc->sc_mii);
	ifmr->ifm_active = sc->sc_mii.mii_media_active;
	ifmr->ifm_status = sc->sc_mii.mii_media_status;
}


static u_int32_t
mtd_mii_command(struct mtd_softc *sc, int opcode, int phy, int reg)
{
	u_int32_t miir, mask, data;
	int i;

	miir = (CSR_READ_4(MTD_MIIMGT) & ~MIIMGT_MASK) | MIIMGT_WRITE |
	    MIIMGT_MDO;

	for (i = 0; i < 32; i++) {
		miir &= ~MIIMGT_MDC;
		CSR_WRITE_4(MTD_MIIMGT, miir);
		miir |= MIIMGT_MDC;
		CSR_WRITE_4(MTD_MIIMGT, miir);
	}

	data = opcode | (phy << 7) | (reg << 2);

	for (mask = 0; mask; mask >>= 1) {
		miir &= ~(MIIMGT_MDC | MIIMGT_MDO);
		if (mask & data)
			miir |= MIIMGT_MDO;
		CSR_WRITE_4(MTD_MIIMGT, miir);
		miir |= MIIMGT_MDC;
		CSR_WRITE_4(MTD_MIIMGT, miir);
		DELAY(30);

		if (mask == 0x4 && opcode == MII_OPCODE_RD)
			miir &= ~MIIMGT_WRITE;
	}
	return (miir);
}



static int
mtd_miibus_readreg(struct device *self, int phy, int reg)
{
	struct mtd_softc *sc = (void *)self;

	if (sc->sc_devid == PCI_PRODUCT_MYSON_MTD803)
		return (phy ? 0 : (int)CSR_READ_2(MTD_PHYCSR + (reg << 1)));
	else {
		u_int32_t miir, mask, data;

		miir = mtd_mii_command(sc, MII_OPCODE_RD, phy, reg);
		for (mask = 0x8000, data = 0; mask; mask >>= 1) {
			miir &= ~MIIMGT_MDC;
			CSR_WRITE_4(MTD_MIIMGT, miir);
			miir = CSR_READ_4(MTD_MIIMGT);
			if (miir & MIIMGT_MDI)
				data |= mask;
			miir |= MIIMGT_MDC;
			CSR_WRITE_4(MTD_MIIMGT, miir);
			DELAY(30);
		}
		miir &= ~MIIMGT_MDC;
		CSR_WRITE_4(MTD_MIIMGT, miir);

		return ((int)data);
	}
}


static void
mtd_miibus_writereg(struct device *self, int phy, int reg, int val)
{
	struct mtd_softc *sc = (void *)self;

	if (sc->sc_devid == PCI_PRODUCT_MYSON_MTD803) {
		if (!phy)
			CSR_WRITE_2(MTD_PHYCSR + (reg << 1), val);
	} else {
		u_int32_t miir, mask;

		miir = mtd_mii_command(sc, MII_OPCODE_WR, phy, reg);
		for (mask = 0x8000; mask; mask >>= 1) {
			miir &= ~(MIIMGT_MDC | MIIMGT_MDO);
			if (mask & (u_int32_t)val)
				miir |= MIIMGT_MDO;
			CSR_WRITE_4(MTD_MIIMGT, miir);
			miir |= MIIMGT_MDC;
			CSR_WRITE_4(MTD_MIIMGT, miir);
			DELAY(1);
		}
		miir &= ~MIIMGT_MDC;
		CSR_WRITE_4(MTD_MIIMGT, miir);
	}
}


static void
mtd_miibus_statchg(struct device *self)
{
	/* NOTHING */
}


void
mtd_setmulti(struct mtd_softc *sc)
{
	struct arpcom *ac = &sc->sc_arpcom;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	u_int32_t rxfilt, crc, hash[2] = { 0, 0 };
	struct ether_multistep step;
	struct ether_multi *enm;
	int mcnt = 0;

	if (ac->ac_multirangecnt > 0)
		ifp->if_flags |= IFF_ALLMULTI;

	rxfilt = CSR_READ_4(MTD_TCRRCR) & ~RCR_AM;
	if (ifp->if_flags & (IFF_ALLMULTI | IFF_PROMISC)) {
		rxfilt |= RCR_AM;
		CSR_WRITE_4(MTD_TCRRCR, rxfilt);
		CSR_WRITE_4(MTD_MAR0, 0xffffffff);
		CSR_WRITE_4(MTD_MAR4, 0xffffffff);
		return;
	}

	/* First, zot all the existing hash bits. */
	CSR_WRITE_4(MTD_MAR0, 0);
	CSR_WRITE_4(MTD_MAR4, 0);

	/* Now program new ones. */
	ETHER_FIRST_MULTI(step, ac, enm);
	while (enm != NULL) {
		crc = ether_crc32_be(enm->enm_addrlo, ETHER_ADDR_LEN) >> 26;
		hash[crc >> 5] |= 1 << (crc & 0xf);
		++mcnt;
		ETHER_NEXT_MULTI(step, enm);
	}

	if (mcnt)
		rxfilt |= RCR_AM;
	CSR_WRITE_4(MTD_MAR0, hash[0]);
	CSR_WRITE_4(MTD_MAR4, hash[1]);
	CSR_WRITE_4(MTD_TCRRCR, rxfilt);
}


/*
 * Encapsulate an mbuf chain in a descriptor by coupling the mbuf data
 * pointers to the fragment pointers.
 */
int
mtd_encap(struct mtd_softc *sc, struct mbuf *m_head, u_int32_t *txidx)
{
	struct mtd_tx_desc *f = NULL;
	int frag, cur, cnt = 0, i, total_len = 0;
	bus_dmamap_t map;

	/*
 	 * Start packing the mbufs in this chain into
	 * the fragment pointers. Stop when we run out
 	 * of fragments or hit the end of the mbuf chain.
	 */
	map = sc->sc_tx_sparemap;

	if (bus_dmamap_load_mbuf(sc->sc_dmat, map,
	    m_head, BUS_DMA_NOWAIT) != 0)
		return (1);

	cur = frag = *txidx;

	for (i = 0; i < map->dm_nsegs; i++) {
		if ((MTD_TX_LIST_CNT -
		    (sc->mtd_cdata.mtd_tx_cnt + cnt)) < 5) {
			bus_dmamap_unload(sc->sc_dmat, map);
			return (1);
		}

		f = &sc->mtd_ldata->mtd_tx_list[frag];
		f->td_tcw = htole32(map->dm_segs[i].ds_len);
		total_len += map->dm_segs[i].ds_len;
		if (cnt == 0) {
			f->td_tsw = 0;
			f->td_tcw |= htole32(TCW_FD | TCW_CRC | TCW_PAD);
		} else
			f->td_tsw = htole32(TSW_OWN);
		f->td_buf = htole32(map->dm_segs[i].ds_addr);
		cur = frag;
		frag = (frag + 1) % MTD_TX_LIST_CNT;
		cnt++;
	}

	sc->mtd_cdata.mtd_tx_cnt += cnt;
	sc->mtd_cdata.mtd_tx_chain[cur].sd_mbuf = m_head;
	sc->sc_tx_sparemap = sc->mtd_cdata.mtd_tx_chain[cur].sd_map;
	sc->mtd_cdata.mtd_tx_chain[cur].sd_map = map;
	sc->mtd_ldata->mtd_tx_list[cur].td_tcw |= htole32(TCW_LD | TCW_IC);
	if (sc->sc_devid == PCI_PRODUCT_MYSON_MTD891)
		sc->mtd_ldata->mtd_tx_list[cur].td_tcw |=
		    htole32(TCW_EIC | TCW_RTLC);

	bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	sc->mtd_ldata->mtd_tx_list[*txidx].td_tsw = htole32(TSW_OWN);
	sc->mtd_ldata->mtd_tx_list[*txidx].td_tcw |=
	    htole32(total_len << TCW_PKTS_SHIFT);

	bus_dmamap_sync(sc->sc_dmat, sc->sc_listmap,
	    offsetof(struct mtd_list_data, mtd_tx_list[0]),
	    sizeof(struct mtd_tx_desc) * MTD_TX_LIST_CNT,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	*txidx = frag;

	return (0);
}


/*
 * Initialize the transmit descriptors.
 */
static void
mtd_list_tx_init(struct mtd_softc *sc)
{
	struct mtd_chain_data *cd;
	struct mtd_list_data *ld;
	int i;

	cd = &sc->mtd_cdata;
	ld = sc->mtd_ldata;
	for (i = 0; i < MTD_TX_LIST_CNT; i++) {
		cd->mtd_tx_chain[i].sd_mbuf = NULL;
		ld->mtd_tx_list[i].td_tsw = 0;
		ld->mtd_tx_list[i].td_tcw = 0;
		ld->mtd_tx_list[i].td_buf = 0;
		ld->mtd_tx_list[i].td_next = htole32(
		    sc->sc_listmap->dm_segs[0].ds_addr +
		    offsetof(struct mtd_list_data,
		    mtd_tx_list[(i + 1) % MTD_TX_LIST_CNT]));
	}

	cd->mtd_tx_prod = cd->mtd_tx_cons = cd->mtd_tx_cnt = 0;
}


/*
 * Initialize the RX descriptors and allocate mbufs for them. Note that
 * we arrange the descriptors in a closed ring, so that the last descriptor
 * points back to the first.
 */
static int
mtd_list_rx_init(struct mtd_softc *sc)
{
	struct mtd_list_data *ld;
	int i;

	ld = sc->mtd_ldata;

	for (i = 0; i < MTD_RX_LIST_CNT; i++) {
		if (mtd_newbuf(sc, i, NULL))
			return (1);
		ld->mtd_rx_list[i].rd_next = htole32(
		    sc->sc_listmap->dm_segs[0].ds_addr +
		    offsetof(struct mtd_list_data,
		    mtd_rx_list[(i + 1) % MTD_RX_LIST_CNT])
		);
	}

	sc->mtd_cdata.mtd_rx_prod = 0;

	return (0);
}


/*
 * Initialize an RX descriptor and attach an MBUF cluster.
 */
static int
mtd_newbuf(struct mtd_softc *sc, int i, struct mbuf *m)
{
	struct mbuf *m_new = NULL;
	struct mtd_rx_desc *c;
	bus_dmamap_t map;

	c = &sc->mtd_ldata->mtd_rx_list[i];

	if (m == NULL) {
		MGETHDR(m_new, M_DONTWAIT, MT_DATA);
		if (m_new == NULL)
			return (1);

		MCLGET(m_new, M_DONTWAIT);
		if (!(m_new->m_flags & M_EXT)) {
			m_freem(m_new);
			return (1);
		}
		m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;
		if (bus_dmamap_load(sc->sc_dmat, sc->sc_rx_sparemap,
		    mtod(m_new, caddr_t), MCLBYTES, NULL,
		    BUS_DMA_NOWAIT) != 0) {
			m_freem(m_new);
			return (1);
		}
		map = sc->mtd_cdata.mtd_rx_chain[i].sd_map;
		sc->mtd_cdata.mtd_rx_chain[i].sd_map = sc->sc_rx_sparemap;
		sc->sc_rx_sparemap = map;
	} else {
		m_new = m;
		m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;
		m_new->m_data = m_new->m_ext.ext_buf;
	}

	m_adj(m_new, sizeof(u_int64_t));

	bus_dmamap_sync(sc->sc_dmat, sc->mtd_cdata.mtd_rx_chain[i].sd_map, 0,
	    sc->mtd_cdata.mtd_rx_chain[i].sd_map->dm_mapsize,
	    BUS_DMASYNC_PREREAD);

	sc->mtd_cdata.mtd_rx_chain[i].sd_mbuf = m_new;
	c->rd_buf = htole32(
	    sc->mtd_cdata.mtd_rx_chain[i].sd_map->dm_segs[0].ds_addr +
	    sizeof(u_int64_t));
	c->rd_rcw = htole32(ETHER_MAX_DIX_LEN);
	c->rd_rsr = htole32(RSR_OWN);

	bus_dmamap_sync(sc->sc_dmat, sc->sc_listmap,
	    offsetof(struct mtd_list_data, mtd_rx_list[i]),
	    sizeof(struct mtd_rx_desc),
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);
}


static void
mtd_reset(struct mtd_softc *sc)
{
	int i;

	/* Set software reset bit */
	CSR_WRITE_4(MTD_BCR, BCR_SWR);

	/*
	 * Wait until software reset completed.
	 */
	for (i = 0; i < MTD_TIMEOUT; ++i) {
		DELAY(10);
		if (!(CSR_READ_4(MTD_BCR) & BCR_SWR)) {
			/*
			 * Wait a little while for the chip to get
			 * its brains in order.
			 */
			DELAY(1000);
			return;
		}
	}

	/* Reset timed out. */
	printf("%s: reset never completed!\n", sc->sc_dev.dv_xname);
}


static int
mtd_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct mtd_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch (command) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		mtd_init(ifp);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP)
			mtd_init(ifp);
		else {
			if (ifp->if_flags & IFF_RUNNING)
				mtd_stop(ifp);
		}
		error = 0;
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_mii.mii_media, command);
		break;
	default:
		error = ether_ioctl(ifp, &sc->sc_arpcom, command, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			mtd_setmulti(sc);
		error = 0;
	}

	splx(s);
	return (error);
}


static void
mtd_init(struct ifnet *ifp)
{
	struct mtd_softc *sc = ifp->if_softc;
	int s;

	s = splnet();

	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */
	mtd_stop(ifp);

	/*
	 * Reset the chip to a known state.
	 */
	mtd_reset(sc);

	/*
	 * Set cache alignment and burst length.
	 */
	CSR_WRITE_4(MTD_BCR, BCR_PBL8);
	CSR_WRITE_4(MTD_TCRRCR, TCR_TFTSF | RCR_RBLEN | RCR_RPBL512);
	if (sc->sc_devid == PCI_PRODUCT_MYSON_MTD891) {
		CSR_SETBIT(MTD_BCR, BCR_PROG);
		CSR_SETBIT(MTD_TCRRCR, TCR_ENHANCED);
	}

	if (ifp->if_flags & IFF_PROMISC)
		CSR_SETBIT(MTD_TCRRCR, RCR_PROM);
	else
		CSR_CLRBIT(MTD_TCRRCR, RCR_PROM);

	if (ifp->if_flags & IFF_BROADCAST)
		CSR_SETBIT(MTD_TCRRCR, RCR_AB);
	else
		CSR_CLRBIT(MTD_TCRRCR, RCR_AB);

	mtd_setmulti(sc);

	if (mtd_list_rx_init(sc)) {
		printf("%s: can't allocate memory for rx buffers\n",
		    sc->sc_dev.dv_xname);
		splx(s);
		return;
	}
	mtd_list_tx_init(sc);

	CSR_WRITE_4(MTD_RXLBA, sc->sc_listmap->dm_segs[0].ds_addr +
	    offsetof(struct mtd_list_data, mtd_rx_list[0]));
	CSR_WRITE_4(MTD_TXLBA, sc->sc_listmap->dm_segs[0].ds_addr +
	    offsetof(struct mtd_list_data, mtd_tx_list[0]));

	/*
	 * Enable interrupts.
	 */
	CSR_WRITE_4(MTD_IMR, IMR_INTRS);
	CSR_WRITE_4(MTD_ISR, 0xffffffff);

	/* Enable receiver and transmitter */
	CSR_SETBIT(MTD_TCRRCR, TCR_TE | RCR_RE);
	CSR_WRITE_4(MTD_RXPDR, 0xffffffff);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	splx(s);
}


/*
 * Main transmit routine. To avoid having to do mbuf copies, we put pointers
 * to the mbuf data regions directly in the transmit lists. We also save a
 * copy of the pointers since the transmit list fragment pointers are
 * physical addresses.
 */
static void
mtd_start(struct ifnet *ifp)
{
	struct mtd_softc *sc = ifp->if_softc;
	struct mbuf *m_head = NULL;
	int idx;

	if (sc->mtd_cdata.mtd_tx_cnt) {
		ifq_set_oactive(&ifp->if_snd);
		return;
	}

	idx = sc->mtd_cdata.mtd_tx_prod;
	while (sc->mtd_cdata.mtd_tx_chain[idx].sd_mbuf == NULL) {
		m_head = ifq_dequeue(&ifp->if_snd);
		if (m_head == NULL)
			break;

		if (mtd_encap(sc, m_head, &idx)) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
#if NBPFILTER > 0
		if (ifp->if_bpf != NULL)
			bpf_mtap(ifp->if_bpf, m_head, BPF_DIRECTION_OUT);
#endif
	}

	if (idx == sc->mtd_cdata.mtd_tx_prod)
		return;

	/* Transmit */
	sc->mtd_cdata.mtd_tx_prod = idx;
	CSR_WRITE_4(MTD_TXPDR, 0xffffffff);

	/*
	 * Set a timeout in case the chip goes out to lunch.
	 */
	ifp->if_timer = 5;
}


static void
mtd_stop(struct ifnet *ifp)
{
	struct mtd_softc *sc = ifp->if_softc;
	int i;

	ifp->if_timer = 0;
	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	CSR_CLRBIT(MTD_TCRRCR, (RCR_RE | TCR_TE));
	CSR_WRITE_4(MTD_IMR, 0);
	CSR_WRITE_4(MTD_TXLBA, 0);
	CSR_WRITE_4(MTD_RXLBA, 0);

	/*
	 * Free data in the RX lists.
	 */
	for (i = 0; i < MTD_RX_LIST_CNT; i++) {
		if (sc->mtd_cdata.mtd_rx_chain[i].sd_map->dm_nsegs != 0) {
			bus_dmamap_t map = sc->mtd_cdata.mtd_rx_chain[i].sd_map;

			bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
			    BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->sc_dmat, map);
		}
		if (sc->mtd_cdata.mtd_rx_chain[i].sd_mbuf != NULL) {
			m_freem(sc->mtd_cdata.mtd_rx_chain[i].sd_mbuf);
			sc->mtd_cdata.mtd_rx_chain[i].sd_mbuf = NULL;
		}
	}
	bzero(&sc->mtd_ldata->mtd_rx_list, sizeof(sc->mtd_ldata->mtd_rx_list));

	/*
	 * Free the TX list buffers.
	 */
	for (i = 0; i < MTD_TX_LIST_CNT; i++) {
		if (sc->mtd_cdata.mtd_tx_chain[i].sd_map->dm_nsegs != 0) {
			bus_dmamap_t map = sc->mtd_cdata.mtd_tx_chain[i].sd_map;

			bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, map);
		}
		if (sc->mtd_cdata.mtd_tx_chain[i].sd_mbuf != NULL) {
			m_freem(sc->mtd_cdata.mtd_tx_chain[i].sd_mbuf);
			sc->mtd_cdata.mtd_tx_chain[i].sd_mbuf = NULL;
		}
	}

	bzero(&sc->mtd_ldata->mtd_tx_list, sizeof(sc->mtd_ldata->mtd_tx_list));

}


static void
mtd_watchdog(struct ifnet *ifp)
{
	struct mtd_softc *sc = ifp->if_softc;

	ifp->if_oerrors++;
	printf("%s: watchdog timeout\n", sc->sc_dev.dv_xname);

	mtd_init(ifp);

	if (!ifq_empty(&ifp->if_snd))
		mtd_start(ifp);
}


int
mtd_intr(void *xsc)
{
	struct mtd_softc *sc = xsc;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	u_int32_t status;
	int claimed = 0;

	/* Suppress unwanted interrupts */
	if (!(ifp->if_flags & IFF_RUNNING)) {
		if (CSR_READ_4(MTD_ISR) & ISR_INTRS)
			mtd_stop(ifp);
		return (claimed);
	}

	/* Disable interrupts. */
	CSR_WRITE_4(MTD_IMR, 0);

	while((status = CSR_READ_4(MTD_ISR)) & ISR_INTRS) {
		claimed = 1;

		CSR_WRITE_4(MTD_ISR, status);

		/* RX interrupt. */
		if (status & ISR_RI) {
			if (mtd_rxeof(sc) == 0)
				while(mtd_rx_resync(sc))
					mtd_rxeof(sc);
		}

		/* RX error interrupt. */
		if (status & (ISR_RXERI | ISR_RBU))
			ifp->if_ierrors++;

		/* TX interrupt. */
		if (status & (ISR_TI | ISR_ETI | ISR_TBU))
			mtd_txeof(sc);

		/* Fatal bus error interrupt. */
		if (status & ISR_FBE) {
			mtd_reset(sc);
			mtd_start(ifp);
		}
	}

	/* Re-enable interrupts. */
	CSR_WRITE_4(MTD_IMR, IMR_INTRS);

	if (!ifq_empty(&ifp->if_snd))
		mtd_start(ifp);

	return (claimed);
}


/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 */
static int
mtd_rxeof(struct mtd_softc *sc)
{
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *m;
	struct ifnet *ifp;
	struct mtd_rx_desc *cur_rx;
	int i, total_len = 0, consumed = 0;
	u_int32_t rxstat;

	ifp = &sc->sc_arpcom.ac_if;
	i = sc->mtd_cdata.mtd_rx_prod;

	while(!(sc->mtd_ldata->mtd_rx_list[i].rd_rsr & htole32(RSR_OWN))) {
		struct mbuf *m0 = NULL;

		bus_dmamap_sync(sc->sc_dmat, sc->sc_listmap,
		    offsetof(struct mtd_list_data, mtd_rx_list[i]),
		    sizeof(struct mtd_rx_desc),
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

		cur_rx = &sc->mtd_ldata->mtd_rx_list[i];
		rxstat = letoh32(cur_rx->rd_rsr);
		m = sc->mtd_cdata.mtd_rx_chain[i].sd_mbuf;
		total_len = RSR_FLNG_GET(rxstat);

		sc->mtd_cdata.mtd_rx_chain[i].sd_mbuf = NULL;

		/*
		 * If an error occurs, update stats, clear the
		 * status word and leave the mbuf cluster in place:
		 * it should simply get re-used next time this descriptor
	 	 * comes up in the ring.
		 */
		if (rxstat & RSR_RXER) {
			ifp->if_ierrors++;
			mtd_newbuf(sc, i, m);
			if (rxstat & RSR_CRC) {
				i = (i + 1) % MTD_RX_LIST_CNT;
				continue;
			} else {
				mtd_init(ifp);
				break;
			}
		}

		/* No errors; receive the packet. */	
		total_len -= ETHER_CRC_LEN;

		bus_dmamap_sync(sc->sc_dmat, sc->mtd_cdata.mtd_rx_chain[i].sd_map,
		    0, sc->mtd_cdata.mtd_rx_chain[i].sd_map->dm_mapsize,
		    BUS_DMASYNC_POSTREAD);

		m0 = m_devget(mtod(m, char *), total_len,  ETHER_ALIGN);
		mtd_newbuf(sc, i, m);
		i = (i + 1) % MTD_RX_LIST_CNT;
		if (m0 == NULL) {
			ifp->if_ierrors++;
			continue;
		}
		m = m0;

		consumed++;
		ml_enqueue(&ml, m);
	}

	if_input(ifp, &ml);

	sc->mtd_cdata.mtd_rx_prod = i;

	return (consumed);
}


/*
 * This routine searches the RX ring for dirty descriptors in the
 * event that the rxeof routine falls out of sync with the chip's
 * current descriptor pointer. This may happen sometimes as a result
 * of a "no RX buffer available" condition that happens when the chip
 * consumes all of the RX buffers before the driver has a chance to
 * process the RX ring. This routine may need to be called more than
 * once to bring the driver back in sync with the chip, however we
 * should still be getting RX DONE interrupts to drive the search
 * for new packets in the RX ring, so we should catch up eventually.
 */
static int
mtd_rx_resync(struct mtd_softc *sc)
{
	int i, pos;
	struct mtd_rx_desc *cur_rx;

	pos = sc->mtd_cdata.mtd_rx_prod;

	for (i = 0; i < MTD_RX_LIST_CNT; i++) {
		bus_dmamap_sync(sc->sc_dmat, sc->sc_listmap,
		    offsetof(struct mtd_list_data, mtd_rx_list[pos]),
		    sizeof(struct mtd_rx_desc),
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

		cur_rx = &sc->mtd_ldata->mtd_rx_list[pos];
		if (!(cur_rx->rd_rsr & htole32(RSR_OWN)))
			break;
		pos = (pos + 1) % MTD_RX_LIST_CNT;
	}

	/* If the ring really is empty, then just return. */
	if (i == MTD_RX_LIST_CNT)
		return (0);

	/* We've fallen behind the chip: catch it. */
	sc->mtd_cdata.mtd_rx_prod = pos;

	return (EAGAIN);
}


/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */
static void
mtd_txeof(struct mtd_softc *sc)
{
	struct mtd_tx_desc *cur_tx = NULL;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	int idx;

	/* Clear the timeout timer. */
	ifp->if_timer = 0;

	/*
	 * Go through our tx list and free mbufs for those
	 * frames that have been transmitted.
	 */
	idx = sc->mtd_cdata.mtd_tx_cons;
	while(idx != sc->mtd_cdata.mtd_tx_prod) {
		u_int32_t txstat;

		bus_dmamap_sync(sc->sc_dmat, sc->sc_listmap,
		    offsetof(struct mtd_list_data, mtd_tx_list[idx]),
		    sizeof(struct mtd_tx_desc),
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

		cur_tx = &sc->mtd_ldata->mtd_tx_list[idx];
		txstat = letoh32(cur_tx->td_tsw);

		if (txstat & TSW_OWN || txstat == TSW_UNSENT)
			break;

		if (!(cur_tx->td_tcw & htole32(TCW_LD))) {
			sc->mtd_cdata.mtd_tx_cnt--;
			idx = (idx + 1) % MTD_TX_LIST_CNT;
			continue;
		}

		if (CSR_READ_4(MTD_TCRRCR) & TCR_ENHANCED)
			ifp->if_collisions += TSR_NCR_GET(CSR_READ_4(MTD_TSR));
		else {
			if (txstat & TSW_TXERR) {
				ifp->if_oerrors++;
				if (txstat & TSW_EC)
					ifp->if_collisions++;
				if (txstat & TSW_LC)
					ifp->if_collisions++;
			}
			ifp->if_collisions += TSW_NCR_GET(txstat);
		}

		if (sc->mtd_cdata.mtd_tx_chain[idx].sd_map->dm_nsegs != 0) {
			bus_dmamap_t map =
			    sc->mtd_cdata.mtd_tx_chain[idx].sd_map;
			bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, map);
		}
		if (sc->mtd_cdata.mtd_tx_chain[idx].sd_mbuf != NULL) {
			m_freem(sc->mtd_cdata.mtd_tx_chain[idx].sd_mbuf);
			sc->mtd_cdata.mtd_tx_chain[idx].sd_mbuf = NULL;
		}
		sc->mtd_cdata.mtd_tx_cnt--;
		idx = (idx + 1) % MTD_TX_LIST_CNT;
	}

	if (cur_tx != NULL) {
		ifq_clr_oactive(&ifp->if_snd);
		sc->mtd_cdata.mtd_tx_cons = idx;
	} else
		if (sc->mtd_ldata->mtd_tx_list[idx].td_tsw ==
		    htole32(TSW_UNSENT)) {
			sc->mtd_ldata->mtd_tx_list[idx].td_tsw =
			    htole32(TSW_OWN);
			ifp->if_timer = 5;
			CSR_WRITE_4(MTD_TXPDR, 0xffffffff);
		}
}

struct cfdriver mtd_cd = {
	NULL, "mtd", DV_IFNET
};
