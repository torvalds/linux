/*-
 * Copyright (c) 2012-2014 Bjoern A. Zeeb
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-11-C-0249
 * ("MRC2"), as part of the DARPA MRC research programme.
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
 * This driver is modelled after atse(4).  We need to seriously reduce the
 * per-driver code we have to write^wcopy & paste.
 *
 * TODO:
 * - figure out on the HW side why some data is LE and some is BE.
 * - general set of improvements possible (e.g., reduce times of copying,
 *   do on-the-copy checksum calculations)
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_device_polling.h"
#include "opt_netfpga.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/types.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_vlan_var.h>

#include <net/bpf.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include "if_nf10bmacreg.h"

#ifndef	NF10BMAC_MAX_PKTS
/*
 * We have a 4k buffer in HW, so do not try to send more than 3 packets.
 * At the time of writing HW is orders of magnitude faster than we can
 * enqueue so it would not matter but need an escape.
 */
#define	NF10BMAC_MAX_PKTS		3
#endif

#ifndef NF10BMAC_WATCHDOG_TIME
#define	NF10BMAC_WATCHDOG_TIME		5	/* seconds */
#endif

#ifdef DEVICE_POLLING
static poll_handler_t nf10bmac_poll;
#endif

#define	NF10BMAC_LOCK(_sc)		mtx_lock(&(_sc)->nf10bmac_mtx)
#define	NF10BMAC_UNLOCK(_sc)		mtx_unlock(&(_sc)->nf10bmac_mtx)
#define	NF10BMAC_LOCK_ASSERT(_sc)	\
	mtx_assert(&(_sc)->nf10bmac_mtx, MA_OWNED)

#define	NF10BMAC_CTRL0			0x00
#define	NF10BMAC_TX_DATA		0x00
#define	NF10BMAC_TX_META		0x08
#define	NF10BMAC_TX_LEN			0x10
#define	NF10BMAC_RX_DATA		0x00
#define	NF10BMAC_RX_META		0x08
#define	NF10BMAC_RX_LEN			0x10
#define	NF10BMAC_INTR_CLEAR_DIS		0x00
#define	NF10BMAC_INTR_CTRL		0x08

#define NF10BMAC_TUSER_MAC0		(1 << 0)
#define NF10BMAC_TUSER_CPU0		(1 << 1)
#define NF10BMAC_TUSER_MAC1		(1 << 2)
#define NF10BMAC_TUSER_CPU1		(1 << 3)
#define NF10BMAC_TUSER_MAC2		(1 << 4)
#define NF10BMAC_TUSER_CPU2		(1 << 5)
#define NF10BMAC_TUSER_MAC3		(1 << 6)
#define NF10BMAC_TUSER_CPU3		(1 << 7)

#define	NF10BMAC_DATA_LEN_MASK		0x0000ffff
#define	NF10BMAC_DATA_DPORT_MASK	0xff000000
#define	NF10BMAC_DATA_DPORT_SHIFT	24
#define	NF10BMAC_DATA_SPORT_MASK	0x00ff0000
#define	NF10BMAC_DATA_SPORT_SHIFT	16
#define	NF10BMAC_DATA_LAST		0x00008000
#ifdef NF10BMAC_64BIT
#define	NF10BMAC_DATA_STRB		0x000000ff
#define	REGWTYPE			uint64_t
#else
#define	NF10BMAC_DATA_STRB		0x0000000f
#define	REGWTYPE			uint32_t
#endif


static inline void
nf10bmac_write(struct resource *res, REGWTYPE reg, REGWTYPE val,
    const char *f __unused, const int l __unused)
{

#ifdef NF10BMAC_64BIT
	bus_write_8(res, reg, htole64(val));
#else
	bus_write_4(res, reg, htole32(val));
#endif
}

static inline REGWTYPE
nf10bmac_read(struct resource *res, REGWTYPE reg,
    const char *f __unused, const int l __unused)
{

#ifdef NF10BMAC_64BIT
	return (le64toh(bus_read_8(res, reg)));
#else
	return (le32toh(bus_read_4(res, reg)));
#endif
}

static inline void
nf10bmac_write_be(struct resource *res, REGWTYPE reg, REGWTYPE val,
    const char *f __unused, const int l __unused)
{

#ifdef NF10BMAC_64BIT
	bus_write_8(res, reg, htobe64(val));
#else
	bus_write_4(res, reg, htobe32(val));
#endif
}


static inline REGWTYPE
nf10bmac_read_be(struct resource *res, REGWTYPE reg,
    const char *f __unused, const int l __unused)
{

#ifdef NF10BMAC_64BIT
	return (be64toh(bus_read_8(res, reg)));
#else
	return (be32toh(bus_read_4(res, reg)));
#endif
}

#define	NF10BMAC_WRITE_CTRL(sc, reg, val)				\
	nf10bmac_write((sc)->nf10bmac_ctrl_res, (reg), (val),		\
	    __func__, __LINE__)
#define	NF10BMAC_WRITE(sc, reg, val)					\
	nf10bmac_write((sc)->nf10bmac_tx_mem_res, (reg), (val),		\
	    __func__, __LINE__)
#define	NF10BMAC_READ(sc, reg)						\
	nf10bmac_read((sc)->nf10bmac_rx_mem_res, (reg),			\
	    __func__, __LINE__)
#define	NF10BMAC_WRITE_BE(sc, reg, val)					\
	nf10bmac_write_be((sc)->nf10bmac_tx_mem_res, (reg), (val),	\
	    __func__, __LINE__)
#define	NF10BMAC_READ_BE(sc, reg)					\
	nf10bmac_read_be((sc)->nf10bmac_rx_mem_res, (reg),		\
	    __func__, __LINE__)

#define	NF10BMAC_WRITE_INTR(sc, reg, val, _f, _l)			\
	nf10bmac_write((sc)->nf10bmac_intr_res, (reg), (val),		\
	    (_f), (_l))

#define	NF10BMAC_RX_INTR_CLEAR_DIS(sc)					\
	NF10BMAC_WRITE_INTR((sc), NF10BMAC_INTR_CLEAR_DIS, 1,		\
	__func__, __LINE__)
#define	NF10BMAC_RX_INTR_ENABLE(sc)					\
	NF10BMAC_WRITE_INTR((sc), NF10BMAC_INTR_CTRL, 1,		\
	__func__, __LINE__)
#define	NF10BMAC_RX_INTR_DISABLE(sc)					\
	NF10BMAC_WRITE_INTR((sc), NF10BMAC_INTR_CTRL, 0,		\
	__func__, __LINE__)


#ifdef ENABLE_WATCHDOG
static void nf10bmac_tick(void *);
#endif
static int nf10bmac_detach(device_t);

devclass_t nf10bmac_devclass;


static int
nf10bmac_tx_locked(struct nf10bmac_softc *sc, struct mbuf *m)
{
	int32_t len, l, ml;
	REGWTYPE md, val;

	NF10BMAC_LOCK_ASSERT(sc);

	KASSERT(m != NULL, ("%s: m is null: sc=%p", __func__, sc));
	KASSERT(m->m_flags & M_PKTHDR, ("%s: not a pkthdr: m=%p", __func__, m));
	/*
	 * Copy to buffer to minimize our pain as we can only store
	 * double words which, after the first mbuf gets out of alignment
	 * quite quickly.
	 */
	m_copydata(m, 0, m->m_pkthdr.len, sc->nf10bmac_tx_buf);
	len = m->m_pkthdr.len;

	/* Write the length at start of packet. */
	NF10BMAC_WRITE(sc, NF10BMAC_TX_LEN, len);

	/* Write the meta data and data. */
	ml = len / sizeof(val);
	len -= (ml * sizeof(val));
	for (l = 0; l <= ml; l++) {
		int32_t cl;

		cl = sizeof(val);
		md = (NF10BMAC_TUSER_CPU0 << NF10BMAC_DATA_SPORT_SHIFT);
		if (l == ml || (len == 0 && l == (ml - 1))) {
			if (l == ml && len == 0) {
				break;
			} else {
				uint8_t s;
				int sl;

				if (l == (ml - 1))
					len = sizeof(val);
				cl = len;

				for (s = 0, sl = len; sl > 0; sl--)
					s |= (1 << (sl - 1));
				md |= (s & NF10BMAC_DATA_STRB);
				md |= NF10BMAC_DATA_LAST;
			}
		} else {
			md |= NF10BMAC_DATA_STRB;
		}
		NF10BMAC_WRITE(sc, NF10BMAC_TX_META, md);
		bcopy(&sc->nf10bmac_tx_buf[l*sizeof(val)], &val, cl);
		NF10BMAC_WRITE_BE(sc, NF10BMAC_TX_DATA, val);	
	}

	/* If anyone is interested give them a copy. */
	BPF_MTAP(sc->nf10bmac_ifp, m);

	m_freem(m);

	return (0);
}

static void
nf10bmac_start_locked(struct ifnet *ifp)
{
	struct nf10bmac_softc *sc;
	int count, error;

	sc = ifp->if_softc;
	NF10BMAC_LOCK_ASSERT(sc);

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) !=
	    IFF_DRV_RUNNING || (sc->nf10bmac_flags & NF10BMAC_FLAGS_LINK) == 0)
		return;

#ifdef ENABLE_WATCHDOG
	/*
	 * Disable the watchdog while sending, we are batching packets.
	 * Though we should never reach 5 seconds, and are holding the lock,
	 * but who knows.
	 */
	sc->nf10bmac_watchdog_timer = 0;
#endif

	/* Send up to MAX_PKTS_PER_TX_LOOP packets. */
	for (count = 0; !IFQ_DRV_IS_EMPTY(&ifp->if_snd) &&
	    count < NF10BMAC_MAX_PKTS; count++) {
		struct mbuf *m;

		IFQ_DRV_DEQUEUE(&ifp->if_snd, m);
		if (m == NULL)
			break;
		error = nf10bmac_tx_locked(sc, m);
		if (error != 0)
			break;
	}

#ifdef ENABLE_WATCHDOG
done:
	/* If the IP core walks into Nekromanteion try to bail out. */
	/* XXX-BZ useless until we have direct FIFO fill status feedback. */
	if (count > 0)
		sc->nf10bmac_watchdog_timer = NF10BMAC_WATCHDOG_TIME;
#endif
}

static void
nf10bmac_start(struct ifnet *ifp)
{
	struct nf10bmac_softc *sc;

	sc = ifp->if_softc;
	NF10BMAC_LOCK(sc);
	nf10bmac_start_locked(ifp);
	NF10BMAC_UNLOCK(sc);
}

static void
nf10bmac_eat_packet_munch_munch(struct nf10bmac_softc *sc)
{
	REGWTYPE md, val;

	do {
		md = NF10BMAC_READ_BE(sc, NF10BMAC_RX_META);
		if ((md & NF10BMAC_DATA_STRB) != 0)
			val = NF10BMAC_READ_BE(sc, NF10BMAC_RX_DATA);
	} while ((md & NF10BMAC_DATA_STRB) != 0 &&
	    (md & NF10BMAC_DATA_LAST) == 0);
}

static int
nf10bmac_rx_locked(struct nf10bmac_softc *sc)
{
	struct ifnet *ifp;
	struct mbuf *m;
	REGWTYPE md, val;
	int32_t len, l;

	/*
	 * General problem here in case we need to sync ourselves to the
	 * beginning of a packet.  Length will only be set for the first
	 * read, and together with strb we can detect the beginning (or
	 * skip to tlast).
	 */

	len = NF10BMAC_READ(sc, NF10BMAC_RX_LEN) & NF10BMAC_DATA_LEN_MASK;
	if (len > (MCLBYTES - ETHER_ALIGN)) {
		nf10bmac_eat_packet_munch_munch(sc);
		return (0);
	}

	md = NF10BMAC_READ(sc, NF10BMAC_RX_META);
	if (len == 0 && (md & NF10BMAC_DATA_STRB) == 0) {
		/* No packet data available. */
		return (0);
	} else if (len == 0 && (md & NF10BMAC_DATA_STRB) != 0) {
		/* We are in the middle of a packet. */
		nf10bmac_eat_packet_munch_munch(sc);
		return (0);
	} else if ((md & NF10BMAC_DATA_STRB) == 0) {
		/* Invalid length "hint". */
		device_printf(sc->nf10bmac_dev,
		    "Unexpected length %d on zero strb\n", len);
		return (0);
	}

	/* Assume at this point that we have data and a full packet. */
	if ((len + ETHER_ALIGN) >= MINCLSIZE) {
		/* Get a cluster. */
		m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
		if (m == NULL)
			return (0);
		m->m_len = m->m_pkthdr.len = MCLBYTES;
	} else {
		/* Hey this still fits into the mbuf+pkthdr. */
		m = m_gethdr(M_NOWAIT, MT_DATA);
		if (m == NULL)
			return (0);
		m->m_len = m->m_pkthdr.len = MHLEN;
	}
	/* Make sure upper layers will be aligned. */
	m_adj(m, ETHER_ALIGN);

	ifp = sc->nf10bmac_ifp;
	l = 0;
/*
	while ((md & NF10BMAC_DATA_STRB) != 0 && l < len) {
*/
	while (l < len) {
		size_t cl;

		if ((md & NF10BMAC_DATA_LAST) == 0 &&
		    (len - l) < sizeof(val)) {
			/*
			 * Our length and LAST disagree. We have a valid STRB.
			 * We could continue until we fill the mbuf and just
			 * log the invlid length "hint".  For now drop the
			 * packet on the floor and count the error.
			 */
			nf10bmac_eat_packet_munch_munch(sc);		
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			m_freem(m);
			return (0);
		} else if ((len - l) <= sizeof(val)) {
			cl = len - l;
		} else {
			cl = sizeof(val);
		}

		/* Read the first bytes of data as well. */
		val = NF10BMAC_READ_BE(sc, NF10BMAC_RX_DATA);
		bcopy(&val, (uint8_t *)(m->m_data + l), cl);
		l += cl;

		if ((md & NF10BMAC_DATA_LAST) != 0 || l >= len)
			break;
		else {
			DELAY(50);
			md = NF10BMAC_READ(sc, NF10BMAC_RX_META);
		}

		cl = 10;
		while ((md & NF10BMAC_DATA_STRB) == 0 && cl-- > 0) {
			DELAY(10);
			md = NF10BMAC_READ(sc, NF10BMAC_RX_META);
		}
	}
	/* We should get out of this loop with tlast and tsrb. */
	if ((md & NF10BMAC_DATA_LAST) == 0 || (md & NF10BMAC_DATA_STRB) == 0) {
		device_printf(sc->nf10bmac_dev, "Unexpected rx loop end state: "
		    "md=0x%08jx len=%d l=%d\n", (uintmax_t)md, len, l);
		if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		m_freem(m);
		return (0);
	}

	m->m_pkthdr.len = m->m_len = len;
	m->m_pkthdr.rcvif = ifp;
	if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);

	NF10BMAC_UNLOCK(sc);
	(*ifp->if_input)(ifp, m);
	NF10BMAC_LOCK(sc);

	return (1);
}


static int
nf10bmac_stop_locked(struct nf10bmac_softc *sc)
{
	struct ifnet *ifp;

	NF10BMAC_LOCK_ASSERT(sc);

#ifdef ENABLE_WATCHDOG
	sc->nf10bmac_watchdog_timer = 0;
	callout_stop(&sc->nf10bmac_tick);
#endif

	ifp = sc->nf10bmac_ifp;
	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	NF10BMAC_RX_INTR_CLEAR_DIS(sc);

	sc->nf10bmac_flags &= ~NF10BMAC_FLAGS_LINK;
	if_link_state_change(ifp, LINK_STATE_DOWN);

	return (0);
}

static int
nf10bmac_reset(struct nf10bmac_softc *sc)
{

	/*
	 * If we do not have an ether address set, initialize to the same
	 * OUI as NetFPGA-10G Linux driver does (which luckily seems
	 * unallocated).  We just change the NIC specific part from
	 * the slightly long "\0NF10C0" to "\0NFBSD".
	 * Oh and we keep the way of setting it from a string as they do.
	 * It's an amazing way to hide it.
	 * XXX-BZ If NetFPGA gets their own OUI we should fix this.
	 */
	if (sc->nf10bmac_eth_addr[0] == 0x00 &&
	    sc->nf10bmac_eth_addr[1] == 0x00 &&
	    sc->nf10bmac_eth_addr[2] == 0x00 &&
	    sc->nf10bmac_eth_addr[3] == 0x00 &&
	    sc->nf10bmac_eth_addr[4] == 0x00 &&
	    sc->nf10bmac_eth_addr[5] == 0x00) {
		memcpy(&sc->nf10bmac_eth_addr, "\0NFBSD", ETHER_ADDR_LEN);
		sc->nf10bmac_eth_addr[5] += sc->nf10bmac_unit;
	}

	return (0);
}

static void
nf10bmac_init_locked(struct nf10bmac_softc *sc)
{
	struct ifnet *ifp;
	uint8_t *eaddr;

	NF10BMAC_LOCK_ASSERT(sc);
	ifp = sc->nf10bmac_ifp;

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		return;

	/*
	 * Must update the ether address if changed.  Given we do not handle
	 * in nf10bmac_ioctl() but it's in the general framework, just always
	 * do it here before nf10bmac_reset().
	 */
	eaddr = IF_LLADDR(sc->nf10bmac_ifp);
	bcopy(eaddr, &sc->nf10bmac_eth_addr, ETHER_ADDR_LEN);
	/* XXX-BZ we do not have any way to tell the NIC our ether address. */

	/* Make things frind to halt, cleanup, ... */
	nf10bmac_stop_locked(sc);
	/* ... reset, ... */
	nf10bmac_reset(sc);

	/* Memory rings?  DMA engine? MC filter?  MII? */
	/* Instead drain the FIFO; or at least a possible first packet.. */
	nf10bmac_eat_packet_munch_munch(sc);

#ifdef DEVICE_POLLING
	/* Only enable interrupts if we are not polling. */
	if (ifp->if_capenable & IFCAP_POLLING) {
		NF10BMAC_RX_INTR_CLEAR_DIS(sc);
	} else
#endif
	{
		NF10BMAC_RX_INTR_ENABLE(sc);
	}

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	/* We have no underlying media, fake link state. */
	sc->nf10bmac_flags = NF10BMAC_FLAGS_LINK;	/* Always up. */
	if_link_state_change(sc->nf10bmac_ifp, LINK_STATE_UP);

#ifdef ENABLE_WATCHDOG
	callout_reset(&sc->nf10bmac_tick, hz, nf10bmac_tick, sc);
#endif
}

static void
nf10bmac_init(void *xsc)
{
	struct nf10bmac_softc *sc;

	sc = (struct nf10bmac_softc *)xsc;
	NF10BMAC_LOCK(sc);
	nf10bmac_init_locked(sc);
	NF10BMAC_UNLOCK(sc);
}

#ifdef ENABLE_WATCHDOG
static void
nf10bmac_watchdog(struct nf10bmac_softc *sc)
{

	NF10BMAC_LOCK_ASSERT(sc);

	if (sc->nf10bmac_watchdog_timer == 0 || --sc->nf10bmac_watchdog_timer > 0)
		return;

	device_printf(sc->nf10bmac_dev, "watchdog timeout\n");
	sc->nf10if_inc_counter(bmac_ifp, IFCOUNTER_OERRORS, 1);

	sc->nf10bmac_ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	nf10bmac_init_locked(sc);

	if (!IFQ_DRV_IS_EMPTY(&sc->nf10bmac_ifp->if_snd))
		nf10bmac_start_locked(sc->nf10bmac_ifp);
}

static void
nf10bmac_tick(void *xsc)
{
	struct nf10bmac_softc *sc;
	struct ifnet *ifp;

	sc = (struct nf10bmac_softc *)xsc;
	NF10BMAC_LOCK_ASSERT(sc);
	ifp = sc->nf10bmac_ifp;

	nf10bmac_watchdog(sc);
	callout_reset(&sc->nf10bmac_tick, hz, nf10bmac_tick, sc);
}
#endif

static void
nf10bmac_intr(void *arg)
{
	struct nf10bmac_softc *sc;
	struct ifnet *ifp;
	int rx_npkts;

	sc = (struct nf10bmac_softc *)arg;
	ifp = sc->nf10bmac_ifp;

	NF10BMAC_LOCK(sc);
#ifdef DEVICE_POLLING
	if (ifp->if_capenable & IFCAP_POLLING) {
		NF10BMAC_UNLOCK(sc);
		return;
	} 
#endif

	/* NF10BMAC_RX_INTR_DISABLE(sc); */
	NF10BMAC_RX_INTR_CLEAR_DIS(sc);

	/* We only have an RX interrupt and no status information. */
	rx_npkts = 0;
	while (rx_npkts < NF10BMAC_MAX_PKTS) {
		int c;

		c = nf10bmac_rx_locked(sc);
		rx_npkts += c;
		if (c == 0)
			break;
	}

	if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
		/* Re-enable interrupts. */
		NF10BMAC_RX_INTR_ENABLE(sc);

		if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
			nf10bmac_start_locked(ifp);
	}
	NF10BMAC_UNLOCK(sc);
}


#ifdef DEVICE_POLLING
static int
nf10bmac_poll(struct ifnet *ifp, enum poll_cmd cmd, int count)
{
	struct nf10bmac_softc *sc;
	int rx_npkts = 0;

	sc = ifp->if_softc;
	NF10BMAC_LOCK(sc);
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
		NF10BMAC_UNLOCK(sc);
		return (rx_npkts);
	}

	while (rx_npkts < count) {
		int c;

		c = nf10bmac_rx_locked(sc);
		rx_npkts += c;
		if (c == 0)
			break;
	}
	nf10bmac_start_locked(ifp);

	if (rx_npkts > 0 || cmd == POLL_AND_CHECK_STATUS) {
		/* We currently cannot do much. */
		;
	}

        NF10BMAC_UNLOCK(sc);
        return (rx_npkts);
}
#else
#error We only support polling mode
#endif /* DEVICE_POLLING */

static int
nf10bmac_media_change(struct ifnet *ifp __unused)
{

	/* Do nothing. */
	return (0);
}

static void
nf10bmac_media_status(struct ifnet *ifp __unused, struct ifmediareq *imr)
{ 

	imr->ifm_status = IFM_AVALID | IFM_ACTIVE;
	imr->ifm_active = IFM_ETHER | IFM_10G_T | IFM_FDX;
}

static int
nf10bmac_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct nf10bmac_softc *sc;
	struct ifreq *ifr;
	int error, mask;

	error = 0;
	sc = ifp->if_softc;
	ifr = (struct ifreq *)data;

	switch (command) {
	case SIOCSIFFLAGS:
		NF10BMAC_LOCK(sc);
		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0 &&
			    ((ifp->if_flags ^ sc->nf10bmac_if_flags) &
			    (IFF_PROMISC | IFF_ALLMULTI)) != 0)
				/* Nothing we can do. */ ;
			else
				nf10bmac_init_locked(sc);
		} else if (ifp->if_drv_flags & IFF_DRV_RUNNING)
			nf10bmac_stop_locked(sc);  
		sc->nf10bmac_if_flags = ifp->if_flags;
		NF10BMAC_UNLOCK(sc);
		break;
	case SIOCSIFCAP:
		NF10BMAC_LOCK(sc);
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
#ifdef DEVICE_POLLING
		if ((mask & IFCAP_POLLING) != 0 &&
		    (IFCAP_POLLING & ifp->if_capabilities) != 0) {
			ifp->if_capenable ^= IFCAP_POLLING;
			if ((IFCAP_POLLING & ifp->if_capenable) != 0) {

				error = ether_poll_register(nf10bmac_poll, ifp);
				if (error != 0) {
					NF10BMAC_UNLOCK(sc);
					break;
				}

				NF10BMAC_RX_INTR_CLEAR_DIS(sc);

			/*
			 * Do not allow disabling of polling if we do
			 * not have interrupts.
			 */
			} else if (sc->nf10bmac_rx_irq_res != NULL) {
				error = ether_poll_deregister(ifp);
				/* Enable interrupts. */
				NF10BMAC_RX_INTR_ENABLE(sc);
			} else {
				ifp->if_capenable ^= IFCAP_POLLING;
				error = EINVAL;
			}
		}
#endif /* DEVICE_POLLING */
                NF10BMAC_UNLOCK(sc);
                break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
                error = ifmedia_ioctl(ifp, ifr, &sc->nf10bmac_media, command);
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}

	return (error);
}

/*
 * Generic device handling routines.
 */
int
nf10bmac_attach(device_t dev)
{
	struct nf10bmac_softc *sc;
	struct ifnet *ifp;
	int error;

	sc = device_get_softc(dev);

	mtx_init(&sc->nf10bmac_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);

#ifdef	ENABLE_WATCHDOG
	callout_init_mtx(&sc->nf10bmac_tick, &sc->nf10bmac_mtx, 0);
#endif

	sc->nf10bmac_tx_buf = malloc(ETHER_MAX_LEN_JUMBO, M_DEVBUF, M_WAITOK);

	/* Reset the adapter. */
	nf10bmac_reset(sc);

	/* Setup interface. */
	ifp = sc->nf10bmac_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {   
		device_printf(dev, "if_alloc() failed\n");
		error = ENOSPC;
		goto err;
	}
	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX; /* | IFF_MULTICAST; */
	ifp->if_ioctl = nf10bmac_ioctl;
	ifp->if_start = nf10bmac_start;
	ifp->if_init = nf10bmac_init;
	IFQ_SET_MAXLEN(&ifp->if_snd, NF10BMAC_MAX_PKTS - 1);
	ifp->if_snd.ifq_drv_maxlen = NF10BMAC_MAX_PKTS - 1;
	IFQ_SET_READY(&ifp->if_snd);

	/* Call media-indepedent attach routine. */
	ether_ifattach(ifp, sc->nf10bmac_eth_addr);

	/* Tell the upper layer(s) about vlan mtu support. */
	ifp->if_hdrlen = sizeof(struct ether_vlan_header);
	ifp->if_capabilities |= IFCAP_VLAN_MTU;
	ifp->if_capenable = ifp->if_capabilities;
#ifdef DEVICE_POLLING
	/* We will enable polling by default if no irqs available. See below. */
	ifp->if_capabilities |= IFCAP_POLLING;
#endif

	/* We need more media attention.  Fake it! */
        ifmedia_init(&sc->nf10bmac_media, 0, nf10bmac_media_change,
	    nf10bmac_media_status);
        ifmedia_add(&sc->nf10bmac_media, IFM_ETHER | IFM_10G_T, 0, NULL);
        ifmedia_set(&sc->nf10bmac_media, IFM_ETHER | IFM_10G_T);

	/* Initialise. */
	error = 0;

	/* Hook up interrupts. Well the one. */
	if (sc->nf10bmac_rx_irq_res != NULL) {
		error = bus_setup_intr(dev, sc->nf10bmac_rx_irq_res,
		    INTR_TYPE_NET | INTR_MPSAFE, NULL, nf10bmac_intr,
		    sc, &sc->nf10bmac_rx_intrhand);
		if (error != 0) {
			device_printf(dev, "enabling RX IRQ failed\n");
			ether_ifdetach(ifp);
			goto err;
		}
	}

	if ((ifp->if_capenable & IFCAP_POLLING) != 0 ||
	    sc->nf10bmac_rx_irq_res == NULL) {
#ifdef DEVICE_POLLING
		/* If not on and no IRQs force it on. */
		if (sc->nf10bmac_rx_irq_res == NULL) {
			ifp->if_capenable |= IFCAP_POLLING;
			device_printf(dev,
			    "forcing to polling due to no interrupts\n");
		}
		error = ether_poll_register(nf10bmac_poll, ifp);
		if (error != 0)
			goto err;
#else
		device_printf(dev, "no DEVICE_POLLING in kernel and no IRQs\n");
		error = ENXIO;
#endif
	} else {
		NF10BMAC_RX_INTR_ENABLE(sc);
	}

err:
	if (error != 0)
		nf10bmac_detach(dev);

	return (error);                                                                                                                                                                      
}

static int
nf10bmac_detach(device_t dev)
{
	struct nf10bmac_softc *sc;
	struct ifnet *ifp;

	sc = device_get_softc(dev);
	KASSERT(mtx_initialized(&sc->nf10bmac_mtx),
	    ("%s: mutex not initialized", device_get_nameunit(dev)));
	ifp = sc->nf10bmac_ifp;

#ifdef DEVICE_POLLING
	if (ifp->if_capenable & IFCAP_POLLING)
		ether_poll_deregister(ifp);
#endif

	/* Only cleanup if attach succeeded. */
	if (device_is_attached(dev)) {
		NF10BMAC_LOCK(sc);
		nf10bmac_stop_locked(sc);
		NF10BMAC_UNLOCK(sc);
#ifdef ENABLE_WATCHDOG
		callout_drain(&sc->nf10bmac_tick);
#endif
		ether_ifdetach(ifp);
	}

	if (sc->nf10bmac_rx_intrhand)
		bus_teardown_intr(dev, sc->nf10bmac_rx_irq_res,
		    sc->nf10bmac_rx_intrhand);

	if (ifp != NULL)
		if_free(ifp);
	ifmedia_removeall(&sc->nf10bmac_media);

	mtx_destroy(&sc->nf10bmac_mtx);

	return (0);
}

/* Shared with the attachment specific (e.g., fdt) implementation. */
void
nf10bmac_detach_resources(device_t dev)
{
	struct nf10bmac_softc *sc;

	sc = device_get_softc(dev);

	if (sc->nf10bmac_rx_irq_res != NULL) {
		bus_release_resource(dev, SYS_RES_IRQ, sc->nf10bmac_rx_irq_rid,
		    sc->nf10bmac_rx_irq_res);
		sc->nf10bmac_rx_irq_res = NULL;
	}
	if (sc->nf10bmac_intr_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY,
		    sc->nf10bmac_intr_rid, sc->nf10bmac_intr_res);
		sc->nf10bmac_intr_res = NULL;
	}
	if (sc->nf10bmac_rx_mem_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY,
		    sc->nf10bmac_rx_mem_rid, sc->nf10bmac_rx_mem_res);
		sc->nf10bmac_rx_mem_res = NULL;
	}
	if (sc->nf10bmac_tx_mem_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY,
		    sc->nf10bmac_tx_mem_rid, sc->nf10bmac_tx_mem_res);
		sc->nf10bmac_tx_mem_res = NULL;
	}
	if (sc->nf10bmac_ctrl_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY,
		    sc->nf10bmac_ctrl_rid, sc->nf10bmac_ctrl_res);
		sc->nf10bmac_ctrl_res = NULL;
	}
}

int
nf10bmac_detach_dev(device_t dev)
{
	int error;

	error = nf10bmac_detach(dev);
	if (error) {
		/* We are basically in undefined state now. */
		device_printf(dev, "nf10bmac_detach() failed: %d\n", error);
		return (error);
	}

	nf10bmac_detach_resources(dev);

	return (0);
}

/* end */
