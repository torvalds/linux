/* $OpenBSD: bcmgenet.c,v 1.8 2024/11/05 18:58:59 miod Exp $ */
/* $NetBSD: bcmgenet.c,v 1.3 2020/02/27 17:30:07 jmcneill Exp $ */

/*-
 * Copyright (c) 2020 Jared McNeill <jmcneill@invisible.ca>
 * Copyright (c) 2020 Mark Kettenis <kettenis@openbsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Broadcom GENETv5
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/timeout.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/bpf.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/mii/miivar.h>

#include <dev/ic/bcmgenetreg.h>
#include <dev/ic/bcmgenetvar.h>

CTASSERT(MCLBYTES == 2048);

#ifdef GENET_DEBUG
#define	DPRINTF(...)	printf(##__VA_ARGS__)
#else
#define	DPRINTF(...)	((void)0)
#endif

#define	TX_SKIP(n, o)		(((n) + (o)) & (GENET_DMA_DESC_COUNT - 1))
#define	TX_NEXT(n)		TX_SKIP(n, 1)
#define	RX_NEXT(n)		(((n) + 1) & (GENET_DMA_DESC_COUNT - 1))

#define	TX_MAX_SEGS		128
#define	TX_DESC_COUNT		GENET_DMA_DESC_COUNT
#define	RX_DESC_COUNT		GENET_DMA_DESC_COUNT
#define	MII_BUSY_RETRY		1000
#define	GENET_MAX_MDF_FILTER	17

#define	RD4(sc, reg)			\
	bus_space_read_4((sc)->sc_bst, (sc)->sc_bsh, (reg))
#define	WR4(sc, reg, val)		\
	bus_space_write_4((sc)->sc_bst, (sc)->sc_bsh, (reg), (val))

struct cfdriver bse_cd = {
	NULL, "bse", DV_IFNET
};

int
genet_media_change(struct ifnet *ifp)
{
	struct genet_softc *sc = ifp->if_softc;

	if (LIST_FIRST(&sc->sc_mii.mii_phys))
		mii_mediachg(&sc->sc_mii);

	return (0);
}

void
genet_media_status(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct genet_softc *sc = ifp->if_softc;

	if (LIST_FIRST(&sc->sc_mii.mii_phys)) {
		mii_pollstat(&sc->sc_mii);
		ifmr->ifm_active = sc->sc_mii.mii_media_active;
		ifmr->ifm_status = sc->sc_mii.mii_media_status;
	}
}

int
genet_mii_readreg(struct device *dev, int phy, int reg)
{
	struct genet_softc *sc = (struct genet_softc *)dev;
	int retry;

	WR4(sc, GENET_MDIO_CMD,
	    GENET_MDIO_READ | GENET_MDIO_START_BUSY |
	    __SHIFTIN(phy, GENET_MDIO_PMD) |
	    __SHIFTIN(reg, GENET_MDIO_REG));
	for (retry = MII_BUSY_RETRY; retry > 0; retry--) {
		if ((RD4(sc, GENET_MDIO_CMD) & GENET_MDIO_START_BUSY) == 0)
			return RD4(sc, GENET_MDIO_CMD) & 0xffff;
		delay(10);
	}

	printf("%s: phy read timeout, phy=%d reg=%d\n",
	    sc->sc_dev.dv_xname, phy, reg);
	return 0;
}

void
genet_mii_writereg(struct device *dev, int phy, int reg, int val)
{
	struct genet_softc *sc = (struct genet_softc *)dev;
	int retry;

	WR4(sc, GENET_MDIO_CMD,
	    val | GENET_MDIO_WRITE | GENET_MDIO_START_BUSY |
	    __SHIFTIN(phy, GENET_MDIO_PMD) |
	    __SHIFTIN(reg, GENET_MDIO_REG));
	for (retry = MII_BUSY_RETRY; retry > 0; retry--) {
		if ((RD4(sc, GENET_MDIO_CMD) & GENET_MDIO_START_BUSY) == 0)
			return;
		delay(10);
	}

	printf("%s: phy write timeout, phy=%d reg=%d\n",
	    sc->sc_dev.dv_xname, phy, reg);
}

void
genet_update_link(struct genet_softc *sc)
{
	struct mii_data *mii = &sc->sc_mii;
	uint32_t val;
	u_int speed;

	if (IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_T ||
	    IFM_SUBTYPE(mii->mii_media_active) == IFM_1000_SX)
		speed = GENET_UMAC_CMD_SPEED_1000;
	else if (IFM_SUBTYPE(mii->mii_media_active) == IFM_100_TX)
		speed = GENET_UMAC_CMD_SPEED_100;
	else
		speed = GENET_UMAC_CMD_SPEED_10;

	val = RD4(sc, GENET_EXT_RGMII_OOB_CTRL);
	val &= ~GENET_EXT_RGMII_OOB_OOB_DISABLE;
	val |= GENET_EXT_RGMII_OOB_RGMII_LINK;
	val |= GENET_EXT_RGMII_OOB_RGMII_MODE_EN;
	if (sc->sc_phy_mode == GENET_PHY_MODE_RGMII)
		val |= GENET_EXT_RGMII_OOB_ID_MODE_DISABLE;
	else
		val &= ~GENET_EXT_RGMII_OOB_ID_MODE_DISABLE;
	WR4(sc, GENET_EXT_RGMII_OOB_CTRL, val);

	val = RD4(sc, GENET_UMAC_CMD);
	val &= ~GENET_UMAC_CMD_SPEED;
	val |= __SHIFTIN(speed, GENET_UMAC_CMD_SPEED);
	WR4(sc, GENET_UMAC_CMD, val);
}

void
genet_mii_statchg(struct device *self)
{
	struct genet_softc *sc = (struct genet_softc *)self;

	genet_update_link(sc);
}

void
genet_setup_txdesc(struct genet_softc *sc, int index, int flags,
    bus_addr_t paddr, u_int len)
{
	uint32_t status;

	status = flags | __SHIFTIN(len, GENET_TX_DESC_STATUS_BUFLEN);
	++sc->sc_tx.queued;

	WR4(sc, GENET_TX_DESC_ADDRESS_LO(index), (uint32_t)paddr);
	WR4(sc, GENET_TX_DESC_ADDRESS_HI(index), (uint32_t)(paddr >> 32));
	WR4(sc, GENET_TX_DESC_STATUS(index), status);
}

int
genet_setup_txbuf(struct genet_softc *sc, int index, struct mbuf *m)
{
	bus_dma_segment_t *segs;
	int error, nsegs, cur, i;
	uint32_t flags;

	/*
	 * XXX Hardware doesn't seem to like small fragments.  For now
	 * just look at the first fragment and defrag if it is smaller
	 * than the minimum Ethernet packet size.
	 */
	if (m->m_len < ETHER_MIN_LEN - ETHER_CRC_LEN) {
		if (m_defrag(m, M_DONTWAIT))
			return 0;
	}

	error = bus_dmamap_load_mbuf(sc->sc_tx.buf_tag,
	    sc->sc_tx.buf_map[index].map, m, BUS_DMA_WRITE | BUS_DMA_NOWAIT);
	if (error == EFBIG) {
		if (m_defrag(m, M_DONTWAIT))
			return 0;
		error = bus_dmamap_load_mbuf(sc->sc_tx.buf_tag,
		    sc->sc_tx.buf_map[index].map, m,
		    BUS_DMA_WRITE | BUS_DMA_NOWAIT);
	}
	if (error != 0)
		return 0;

	segs = sc->sc_tx.buf_map[index].map->dm_segs;
	nsegs = sc->sc_tx.buf_map[index].map->dm_nsegs;

	if (sc->sc_tx.queued >= GENET_DMA_DESC_COUNT - nsegs) {
		bus_dmamap_unload(sc->sc_tx.buf_tag,
		    sc->sc_tx.buf_map[index].map);
		return -1;
	}

	flags = GENET_TX_DESC_STATUS_SOP |
		GENET_TX_DESC_STATUS_CRC |
		GENET_TX_DESC_STATUS_QTAG;

	for (cur = index, i = 0; i < nsegs; i++) {
		sc->sc_tx.buf_map[cur].mbuf = (i == 0 ? m : NULL);
		if (i == nsegs - 1)
			flags |= GENET_TX_DESC_STATUS_EOP;

		genet_setup_txdesc(sc, cur, flags, segs[i].ds_addr,
		    segs[i].ds_len);

		if (i == 0) {
			flags &= ~GENET_TX_DESC_STATUS_SOP;
			flags &= ~GENET_TX_DESC_STATUS_CRC;
		}
		cur = TX_NEXT(cur);
	}

	bus_dmamap_sync(sc->sc_tx.buf_tag, sc->sc_tx.buf_map[index].map,
	    0, sc->sc_tx.buf_map[index].map->dm_mapsize, BUS_DMASYNC_PREWRITE);

	return nsegs;
}

void
genet_setup_rxdesc(struct genet_softc *sc, int index,
    bus_addr_t paddr, bus_size_t len)
{
	WR4(sc, GENET_RX_DESC_ADDRESS_LO(index), (uint32_t)paddr);
	WR4(sc, GENET_RX_DESC_ADDRESS_HI(index), (uint32_t)(paddr >> 32));
}

int
genet_setup_rxbuf(struct genet_softc *sc, int index, struct mbuf *m)
{
	int error;

	error = bus_dmamap_load_mbuf(sc->sc_rx.buf_tag,
	    sc->sc_rx.buf_map[index].map, m, BUS_DMA_READ | BUS_DMA_NOWAIT);
	if (error != 0)
		return error;

	bus_dmamap_sync(sc->sc_rx.buf_tag, sc->sc_rx.buf_map[index].map,
	    0, sc->sc_rx.buf_map[index].map->dm_mapsize,
	    BUS_DMASYNC_PREREAD);

	sc->sc_rx.buf_map[index].mbuf = m;
	genet_setup_rxdesc(sc, index,
	    sc->sc_rx.buf_map[index].map->dm_segs[0].ds_addr,
	    sc->sc_rx.buf_map[index].map->dm_segs[0].ds_len);

	return 0;
}

struct mbuf *
genet_alloc_mbufcl(struct genet_softc *sc)
{
	struct mbuf *m;

	m = MCLGETL(NULL, M_DONTWAIT, MCLBYTES);
	if (m != NULL)
		m->m_pkthdr.len = m->m_len = m->m_ext.ext_size;

	return m;
}

void
genet_fill_rx_ring(struct genet_softc *sc, int qid)
{
	struct mbuf *m;
	uint32_t cidx, index, total;
	u_int slots;
	int error;

	cidx = sc->sc_rx.cidx;
	total = (sc->sc_rx.pidx - cidx) & 0xffff;
	KASSERT(total <= RX_DESC_COUNT);

	index = sc->sc_rx.cidx & (RX_DESC_COUNT - 1);
	for (slots = if_rxr_get(&sc->sc_rx_ring, total);
	     slots > 0; slots--) {
		if ((m = genet_alloc_mbufcl(sc)) == NULL) {
			printf("%s: cannot allocate RX mbuf\n",
			    sc->sc_dev.dv_xname);
			break;
		}
		error = genet_setup_rxbuf(sc, index, m);
		if (error != 0) {
			printf("%s: cannot create RX buffer\n",
			    sc->sc_dev.dv_xname);
			m_freem(m);
			break;
		}

		cidx = (cidx + 1) & 0xffff;
		index = RX_NEXT(index);
	}
	if_rxr_put(&sc->sc_rx_ring, slots);

	if (sc->sc_rx.cidx != cidx) {
		sc->sc_rx.cidx = cidx;
		WR4(sc, GENET_RX_DMA_CONS_INDEX(qid), sc->sc_rx.cidx);
	}

	if (if_rxr_inuse(&sc->sc_rx_ring) == 0)
		timeout_add(&sc->sc_rxto, 1);
}

void
genet_rxtick(void *arg)
{
	genet_fill_rx_ring(arg, GENET_DMA_DEFAULT_QUEUE);
}

void
genet_enable_intr(struct genet_softc *sc)
{
	WR4(sc, GENET_INTRL2_CPU_CLEAR_MASK,
	    GENET_IRQ_TXDMA_DONE | GENET_IRQ_RXDMA_DONE);
}

void
genet_disable_intr(struct genet_softc *sc)
{
	/* Disable interrupts */
	WR4(sc, GENET_INTRL2_CPU_SET_MASK, 0xffffffff);
	WR4(sc, GENET_INTRL2_CPU_CLEAR, 0xffffffff);
}

void
genet_tick(void *softc)
{
	struct genet_softc *sc = softc;
	struct mii_data *mii = &sc->sc_mii;
	int s = splnet();

	mii_tick(mii);
	timeout_add_sec(&sc->sc_stat_ch, 1);

	splx(s);
}

void
genet_setup_rxfilter_mdf(struct genet_softc *sc, u_int n, const uint8_t *ea)
{
	uint32_t addr0 = (ea[0] << 8) | ea[1];
	uint32_t addr1 = (ea[2] << 24) | (ea[3] << 16) | (ea[4] << 8) | ea[5];

	WR4(sc, GENET_UMAC_MDF_ADDR0(n), addr0);
	WR4(sc, GENET_UMAC_MDF_ADDR1(n), addr1);
}

void
genet_setup_rxfilter(struct genet_softc *sc)
{
	struct arpcom *ac = &sc->sc_ac;
	struct ifnet *ifp = &ac->ac_if;
	struct ether_multistep step;
	struct ether_multi *enm;
	uint32_t cmd, mdf_ctrl;
	u_int n;

	cmd = RD4(sc, GENET_UMAC_CMD);

	/*
	 * Count the required number of hardware filters. We need one
	 * for each multicast address, plus one for our own address and
	 * the broadcast address.
	 */
	ETHER_FIRST_MULTI(step, ac, enm);
	for (n = 2; enm != NULL; n++)
		ETHER_NEXT_MULTI(step, enm);

	if (n > GENET_MAX_MDF_FILTER || ac->ac_multirangecnt > 0)
		ifp->if_flags |= IFF_ALLMULTI;
	else
		ifp->if_flags &= ~IFF_ALLMULTI;

	if ((ifp->if_flags & (IFF_PROMISC|IFF_ALLMULTI)) != 0) {
		cmd |= GENET_UMAC_CMD_PROMISC;
		mdf_ctrl = 0;
	} else {
		cmd &= ~GENET_UMAC_CMD_PROMISC;
		genet_setup_rxfilter_mdf(sc, 0, etherbroadcastaddr);
		genet_setup_rxfilter_mdf(sc, 1, LLADDR(ifp->if_sadl));
		ETHER_FIRST_MULTI(step, ac, enm);
		for (n = 2; enm != NULL; n++) {
			genet_setup_rxfilter_mdf(sc, n, enm->enm_addrlo);
			ETHER_NEXT_MULTI(step, enm);
		}
		mdf_ctrl = __BITS(GENET_MAX_MDF_FILTER - 1,
				  GENET_MAX_MDF_FILTER - n);
	}

	WR4(sc, GENET_UMAC_CMD, cmd);
	WR4(sc, GENET_UMAC_MDF_CTRL, mdf_ctrl);
}

void
genet_disable_dma(struct genet_softc *sc)
{
	uint32_t val;

	/* Disable receiver */
	val = RD4(sc, GENET_UMAC_CMD);
	val &= ~GENET_UMAC_CMD_RXEN;
	WR4(sc, GENET_UMAC_CMD, val);

	/* Stop receive DMA */
	val = RD4(sc, GENET_RX_DMA_CTRL);
	val &= ~GENET_RX_DMA_CTRL_EN;
	val &= ~GENET_RX_DMA_CTRL_RBUF_EN(GENET_DMA_DEFAULT_QUEUE);
	WR4(sc, GENET_RX_DMA_CTRL, val);

	/* Stop transmit DMA */
	val = RD4(sc, GENET_TX_DMA_CTRL);
	val &= ~GENET_TX_DMA_CTRL_EN;
	val &= ~GENET_TX_DMA_CTRL_RBUF_EN(GENET_DMA_DEFAULT_QUEUE);
	WR4(sc, GENET_TX_DMA_CTRL, val);

	/* Flush data in the TX FIFO */
	WR4(sc, GENET_UMAC_TX_FLUSH, 1);
	delay(10);
	WR4(sc, GENET_UMAC_TX_FLUSH, 0);

	/* Disable transmitter */
	val = RD4(sc, GENET_UMAC_CMD);
	val &= ~GENET_UMAC_CMD_TXEN;
	WR4(sc, GENET_UMAC_CMD, val);
}

int
genet_reset(struct genet_softc *sc)
{
	uint32_t val;

	genet_disable_dma(sc);

	val = RD4(sc, GENET_SYS_RBUF_FLUSH_CTRL);
	val |= GENET_SYS_RBUF_FLUSH_RESET;
	WR4(sc, GENET_SYS_RBUF_FLUSH_CTRL, val);
	delay(10);

	val &= ~GENET_SYS_RBUF_FLUSH_RESET;
	WR4(sc, GENET_SYS_RBUF_FLUSH_CTRL, val);
	delay(10);

	WR4(sc, GENET_SYS_RBUF_FLUSH_CTRL, 0);
	delay(10);

	WR4(sc, GENET_UMAC_CMD, 0);
	WR4(sc, GENET_UMAC_CMD,
	    GENET_UMAC_CMD_LCL_LOOP_EN | GENET_UMAC_CMD_SW_RESET);
	delay(10);
	WR4(sc, GENET_UMAC_CMD, 0);

	WR4(sc, GENET_UMAC_MIB_CTRL, GENET_UMAC_MIB_RESET_RUNT |
	    GENET_UMAC_MIB_RESET_RX | GENET_UMAC_MIB_RESET_TX);
	WR4(sc, GENET_UMAC_MIB_CTRL, 0);

	WR4(sc, GENET_UMAC_MAX_FRAME_LEN, 1536);

	val = RD4(sc, GENET_RBUF_CTRL);
	val |= GENET_RBUF_ALIGN_2B;
	WR4(sc, GENET_RBUF_CTRL, val);

	WR4(sc, GENET_RBUF_TBUF_SIZE_CTRL, 1);

	return 0;
}

void
genet_init_rings(struct genet_softc *sc, int qid)
{
	uint32_t val;

	/* TX ring */

	sc->sc_tx.next = 0;
	sc->sc_tx.queued = 0;
	sc->sc_tx.cidx = sc->sc_tx.pidx = 0;

	WR4(sc, GENET_TX_SCB_BURST_SIZE, 0x08);

	WR4(sc, GENET_TX_DMA_READ_PTR_LO(qid), 0);
	WR4(sc, GENET_TX_DMA_READ_PTR_HI(qid), 0);
	WR4(sc, GENET_TX_DMA_CONS_INDEX(qid), sc->sc_tx.cidx);
	WR4(sc, GENET_TX_DMA_PROD_INDEX(qid), sc->sc_tx.pidx);
	WR4(sc, GENET_TX_DMA_RING_BUF_SIZE(qid),
	    __SHIFTIN(TX_DESC_COUNT, GENET_TX_DMA_RING_BUF_SIZE_DESC_COUNT) |
	    __SHIFTIN(MCLBYTES, GENET_TX_DMA_RING_BUF_SIZE_BUF_LENGTH));
	WR4(sc, GENET_TX_DMA_START_ADDR_LO(qid), 0);
	WR4(sc, GENET_TX_DMA_START_ADDR_HI(qid), 0);
	WR4(sc, GENET_TX_DMA_END_ADDR_LO(qid),
	    TX_DESC_COUNT * GENET_DMA_DESC_SIZE / 4 - 1);
	WR4(sc, GENET_TX_DMA_END_ADDR_HI(qid), 0);
	WR4(sc, GENET_TX_DMA_MBUF_DONE_THRES(qid), 1);
	WR4(sc, GENET_TX_DMA_FLOW_PERIOD(qid), 0);
	WR4(sc, GENET_TX_DMA_WRITE_PTR_LO(qid), 0);
	WR4(sc, GENET_TX_DMA_WRITE_PTR_HI(qid), 0);

	WR4(sc, GENET_TX_DMA_RING_CFG, __BIT(qid));	/* enable */

	/* Enable transmit DMA */
	val = RD4(sc, GENET_TX_DMA_CTRL);
	val |= GENET_TX_DMA_CTRL_EN;
	val |= GENET_TX_DMA_CTRL_RBUF_EN(qid);
	WR4(sc, GENET_TX_DMA_CTRL, val);

	/* RX ring */

	sc->sc_rx.next = 0;
	sc->sc_rx.cidx = 0;
	sc->sc_rx.pidx = RX_DESC_COUNT;

	WR4(sc, GENET_RX_SCB_BURST_SIZE, 0x08);

	WR4(sc, GENET_RX_DMA_WRITE_PTR_LO(qid), 0);
	WR4(sc, GENET_RX_DMA_WRITE_PTR_HI(qid), 0);
	WR4(sc, GENET_RX_DMA_PROD_INDEX(qid), sc->sc_rx.pidx);
	WR4(sc, GENET_RX_DMA_CONS_INDEX(qid), sc->sc_rx.cidx);
	WR4(sc, GENET_RX_DMA_RING_BUF_SIZE(qid),
	    __SHIFTIN(RX_DESC_COUNT, GENET_RX_DMA_RING_BUF_SIZE_DESC_COUNT) |
	    __SHIFTIN(MCLBYTES, GENET_RX_DMA_RING_BUF_SIZE_BUF_LENGTH));
	WR4(sc, GENET_RX_DMA_START_ADDR_LO(qid), 0);
	WR4(sc, GENET_RX_DMA_START_ADDR_HI(qid), 0);
	WR4(sc, GENET_RX_DMA_END_ADDR_LO(qid),
	    RX_DESC_COUNT * GENET_DMA_DESC_SIZE / 4 - 1);
	WR4(sc, GENET_RX_DMA_END_ADDR_HI(qid), 0);
	WR4(sc, GENET_RX_DMA_XON_XOFF_THRES(qid),
	    __SHIFTIN(5, GENET_RX_DMA_XON_XOFF_THRES_LO) |
	    __SHIFTIN(RX_DESC_COUNT >> 4, GENET_RX_DMA_XON_XOFF_THRES_HI));
	WR4(sc, GENET_RX_DMA_READ_PTR_LO(qid), 0);
	WR4(sc, GENET_RX_DMA_READ_PTR_HI(qid), 0);

	WR4(sc, GENET_RX_DMA_RING_CFG, __BIT(qid));	/* enable */

	if_rxr_init(&sc->sc_rx_ring, 2, RX_DESC_COUNT);
	genet_fill_rx_ring(sc, qid);

	/* Enable receive DMA */
	val = RD4(sc, GENET_RX_DMA_CTRL);
	val |= GENET_RX_DMA_CTRL_EN;
	val |= GENET_RX_DMA_CTRL_RBUF_EN(qid);
	WR4(sc, GENET_RX_DMA_CTRL, val);
}

int
genet_init(struct genet_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct mii_data *mii = &sc->sc_mii;
	uint32_t val;
	uint8_t *enaddr = LLADDR(ifp->if_sadl);

	if (ifp->if_flags & IFF_RUNNING)
		return 0;

	if (sc->sc_phy_mode == GENET_PHY_MODE_RGMII ||
	    sc->sc_phy_mode == GENET_PHY_MODE_RGMII_ID ||
	    sc->sc_phy_mode == GENET_PHY_MODE_RGMII_RXID ||
	    sc->sc_phy_mode == GENET_PHY_MODE_RGMII_TXID)
		WR4(sc, GENET_SYS_PORT_CTRL,
		    GENET_SYS_PORT_MODE_EXT_GPHY);

	/* Write hardware address */
	val = enaddr[3] | (enaddr[2] << 8) | (enaddr[1] << 16) |
	    (enaddr[0] << 24);
	WR4(sc, GENET_UMAC_MAC0, val);
	val = enaddr[5] | (enaddr[4] << 8);
	WR4(sc, GENET_UMAC_MAC1, val);

	/* Setup RX filter */
	genet_setup_rxfilter(sc);

	/* Setup TX/RX rings */
	genet_init_rings(sc, GENET_DMA_DEFAULT_QUEUE);

	/* Enable transmitter and receiver */
	val = RD4(sc, GENET_UMAC_CMD);
	val |= GENET_UMAC_CMD_TXEN;
	val |= GENET_UMAC_CMD_RXEN;
	WR4(sc, GENET_UMAC_CMD, val);

	/* Enable interrupts */
	genet_enable_intr(sc);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	mii_mediachg(mii);
	timeout_add_sec(&sc->sc_stat_ch, 1);

	return 0;
}

void
genet_stop(struct genet_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct genet_bufmap *bmap;
	int i;

	timeout_del(&sc->sc_rxto);
	timeout_del(&sc->sc_stat_ch);

	mii_down(&sc->sc_mii);

	genet_disable_dma(sc);

	/* Disable interrupts */
	genet_disable_intr(sc);

	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	ifp->if_timer = 0;

	intr_barrier(sc->sc_ih);

	/* Clean RX ring. */
	for (i = 0; i < RX_DESC_COUNT; i++) {
		bmap = &sc->sc_rx.buf_map[i];
		if (bmap->mbuf) {
			bus_dmamap_sync(sc->sc_dmat, bmap->map, 0,
			    bmap->map->dm_mapsize, BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->sc_dmat, bmap->map);
			m_freem(bmap->mbuf);
			bmap->mbuf = NULL;
		}
	}

	/* Clean TX ring. */
	for (i = 0; i < TX_DESC_COUNT; i++) {
		bmap = &sc->sc_tx.buf_map[i];
		if (bmap->mbuf) {
			bus_dmamap_sync(sc->sc_dmat, bmap->map, 0,
			    bmap->map->dm_mapsize, BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, bmap->map);
			m_freem(bmap->mbuf);
			bmap->mbuf = NULL;
		}
	}
}

void
genet_rxintr(struct genet_softc *sc, int qid)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *m;
	int index, len, n;
	uint32_t status, pidx, total;

	pidx = RD4(sc, GENET_RX_DMA_PROD_INDEX(qid)) & 0xffff;
	total = (pidx - sc->sc_rx.pidx) & 0xffff;

	DPRINTF("RX pidx=%08x total=%d\n", pidx, total);

	index = sc->sc_rx.next;
	for (n = 0; n < total; n++) {
		status = RD4(sc, GENET_RX_DESC_STATUS(index));
		len = __SHIFTOUT(status, GENET_RX_DESC_STATUS_BUFLEN);

		/* XXX check for errors */

		bus_dmamap_sync(sc->sc_rx.buf_tag, sc->sc_rx.buf_map[index].map,
		    0, sc->sc_rx.buf_map[index].map->dm_mapsize,
		    BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(sc->sc_rx.buf_tag, sc->sc_rx.buf_map[index].map);

		DPRINTF("RX [#%d] index=%02x status=%08x len=%d adj_len=%d\n",
		    n, index, status, len, len - ETHER_ALIGN);

		m = sc->sc_rx.buf_map[index].mbuf;
		sc->sc_rx.buf_map[index].mbuf = NULL;

		if (len > ETHER_ALIGN) {
			m_adj(m, ETHER_ALIGN);

			m->m_len = m->m_pkthdr.len = len - ETHER_ALIGN;
			m->m_nextpkt = NULL;

			ml_enqueue(&ml, m);
		} else {
			ifp->if_ierrors++;
			m_freem(m);
		}

		if_rxr_put(&sc->sc_rx_ring, 1);

		index = RX_NEXT(index);
	}

	if (sc->sc_rx.pidx != pidx) {
		sc->sc_rx.next = index;
		sc->sc_rx.pidx = pidx;

		if (ifiq_input(&ifp->if_rcv, &ml))
			if_rxr_livelocked(&sc->sc_rx_ring);

		genet_fill_rx_ring(sc, qid);
	}
}

void
genet_txintr(struct genet_softc *sc, int qid)
{
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	struct genet_bufmap *bmap;
	uint32_t cidx, total;
	int i;

	cidx = RD4(sc, GENET_TX_DMA_CONS_INDEX(qid)) & 0xffff;
	total = (cidx - sc->sc_tx.cidx) & 0xffff;

	for (i = sc->sc_tx.next; sc->sc_tx.queued > 0 && total > 0;
	     i = TX_NEXT(i), total--) {
		/* XXX check for errors */

		bmap = &sc->sc_tx.buf_map[i];
		if (bmap->mbuf != NULL) {
			bus_dmamap_sync(sc->sc_tx.buf_tag, bmap->map,
			    0, bmap->map->dm_mapsize,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_tx.buf_tag, bmap->map);
			m_freem(bmap->mbuf);
			bmap->mbuf = NULL;
		}

		--sc->sc_tx.queued;
	}

	if (sc->sc_tx.queued == 0)
		ifp->if_timer = 0;

	if (sc->sc_tx.cidx != cidx) {
		sc->sc_tx.next = i;
		sc->sc_tx.cidx = cidx;

		if (ifq_is_oactive(&ifp->if_snd))
			ifq_restart(&ifp->if_snd);
	}
}

void
genet_start(struct ifnet *ifp)
{
	struct genet_softc *sc = ifp->if_softc;
	struct mbuf *m;
	const int qid = GENET_DMA_DEFAULT_QUEUE;
	int nsegs, index, cnt;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;
	if (ifq_is_oactive(&ifp->if_snd))
		return;

	index = sc->sc_tx.pidx & (TX_DESC_COUNT - 1);
	cnt = 0;

	for (;;) {
		m = ifq_deq_begin(&ifp->if_snd);
		if (m == NULL)
			break;

		nsegs = genet_setup_txbuf(sc, index, m);
		if (nsegs == -1) {
			ifq_deq_rollback(&ifp->if_snd, m);
			ifq_set_oactive(&ifp->if_snd);
			break;
		}
		if (nsegs == 0) {
			ifq_deq_commit(&ifp->if_snd, m);
			m_freem(m);
			ifp->if_oerrors++;
			continue;
		}
		ifq_deq_commit(&ifp->if_snd, m);
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m, BPF_DIRECTION_OUT);

		index = TX_SKIP(index, nsegs);

		sc->sc_tx.pidx = (sc->sc_tx.pidx + nsegs) & 0xffff;
		cnt++;
	}

	if (cnt != 0) {
		WR4(sc, GENET_TX_DMA_PROD_INDEX(qid), sc->sc_tx.pidx);
		ifp->if_timer = 5;
	}
}

int
genet_intr(void *arg)
{
	struct genet_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	uint32_t val;

	val = RD4(sc, GENET_INTRL2_CPU_STAT);
	val &= ~RD4(sc, GENET_INTRL2_CPU_STAT_MASK);
	WR4(sc, GENET_INTRL2_CPU_CLEAR, val);

	if (val & GENET_IRQ_RXDMA_DONE)
		genet_rxintr(sc, GENET_DMA_DEFAULT_QUEUE);

	if (val & GENET_IRQ_TXDMA_DONE) {
		genet_txintr(sc, GENET_DMA_DEFAULT_QUEUE);
		if (ifq_is_oactive(&ifp->if_snd))
			ifq_restart(&ifp->if_snd);
	}

	return 1;
}

int
genet_ioctl(struct ifnet *ifp, u_long cmd, caddr_t addr)
{
	struct genet_softc *sc = ifp->if_softc;
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
				genet_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				genet_stop(sc);
		}
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_mii.mii_media, cmd);
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
			genet_setup_rxfilter(sc);
		error = 0;
	}

	splx(s);
	return error;
}

int
genet_setup_dma(struct genet_softc *sc, int qid)
{
	int error, i;

	/* Setup TX ring */
	sc->sc_tx.buf_tag = sc->sc_dmat;
	for (i = 0; i < TX_DESC_COUNT; i++) {
		error = bus_dmamap_create(sc->sc_tx.buf_tag, MCLBYTES,
		    TX_MAX_SEGS, MCLBYTES, 0, BUS_DMA_WAITOK,
		    &sc->sc_tx.buf_map[i].map);
		if (error != 0) {
			printf("%s: cannot create TX buffer map\n",
			    sc->sc_dev.dv_xname);
			return error;
		}
	}

	/* Setup RX ring */
	sc->sc_rx.buf_tag = sc->sc_dmat;
	for (i = 0; i < RX_DESC_COUNT; i++) {
		error = bus_dmamap_create(sc->sc_rx.buf_tag, MCLBYTES,
		    1, MCLBYTES, 0, BUS_DMA_WAITOK,
		    &sc->sc_rx.buf_map[i].map);
		if (error != 0) {
			printf("%s: cannot create RX buffer map\n",
			    sc->sc_dev.dv_xname);
			return error;
		}
	}

	return 0;
}

int
genet_attach(struct genet_softc *sc)
{
	struct mii_data *mii = &sc->sc_mii;
	struct ifnet *ifp = &sc->sc_ac.ac_if;
	int mii_flags = 0;

	switch (sc->sc_phy_mode) {
	case GENET_PHY_MODE_RGMII_ID:
		mii_flags |= MIIF_RXID | MIIF_TXID;
		break;
	case GENET_PHY_MODE_RGMII_RXID:
		mii_flags |= MIIF_RXID;
		break;
	case GENET_PHY_MODE_RGMII_TXID:
		mii_flags |= MIIF_TXID;
		break;
	case GENET_PHY_MODE_RGMII:
	default:
		break;
	}

	printf(": address %s\n", ether_sprintf(sc->sc_lladdr));

	/* Soft reset EMAC core */
	genet_reset(sc);

	/* Setup DMA descriptors */
	if (genet_setup_dma(sc, GENET_DMA_DEFAULT_QUEUE) != 0) {
		printf("%s: failed to setup DMA descriptors\n",
		    sc->sc_dev.dv_xname);
		return EINVAL;
	}

	timeout_set(&sc->sc_stat_ch, genet_tick, sc);
	timeout_set(&sc->sc_rxto, genet_rxtick, sc);

	/* Setup ethernet interface */
	ifp->if_softc = sc;
	snprintf(ifp->if_xname, IFNAMSIZ, "%s", sc->sc_dev.dv_xname);
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_start = genet_start;
	ifp->if_ioctl = genet_ioctl;
	ifq_init_maxlen(&ifp->if_snd, IFQ_MAXLEN);

	/* 802.1Q VLAN-sized frames are supported */
	ifp->if_capabilities = IFCAP_VLAN_MTU;

	/* Attach MII driver */
	ifmedia_init(&mii->mii_media, 0, genet_media_change, genet_media_status);
	mii->mii_ifp = ifp;
	mii->mii_readreg = genet_mii_readreg;
	mii->mii_writereg = genet_mii_writereg;
	mii->mii_statchg = genet_mii_statchg;
	mii_attach(&sc->sc_dev, mii, 0xffffffff, sc->sc_phy_id,
	    MII_OFFSET_ANY, mii_flags);

	if (LIST_EMPTY(&mii->mii_phys)) {
		printf("%s: no PHY found!\n", sc->sc_dev.dv_xname);
		ifmedia_add(&mii->mii_media, IFM_ETHER|IFM_MANUAL, 0, NULL);
		ifmedia_set(&mii->mii_media, IFM_ETHER|IFM_MANUAL);
	}
	ifmedia_set(&mii->mii_media, IFM_ETHER | IFM_AUTO);

	/* Attach interface */
	if_attach(ifp);

	/* Attach ethernet interface */
	ether_ifattach(ifp);

	return 0;
}

void
genet_lladdr_read(struct genet_softc *sc, uint8_t *lladdr)
{
	uint32_t maclo, machi;

	maclo = RD4(sc, GENET_UMAC_MAC0);
	machi = RD4(sc, GENET_UMAC_MAC1);

	lladdr[0] = (maclo >> 24) & 0xff;
	lladdr[1] = (maclo >> 16) & 0xff;
	lladdr[2] = (maclo >> 8) & 0xff;
	lladdr[3] = (maclo >> 0) & 0xff;
	lladdr[4] = (machi >> 8) & 0xff;
	lladdr[5] = (machi >> 0) & 0xff;
}
