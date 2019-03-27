/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012-2014 Thomas Skibo <thomasskibo@yahoo.com>
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * A network interface driver for Cadence GEM Gigabit Ethernet
 * interface such as the one used in Xilinx Zynq-7000 SoC.
 *
 * Reference: Zynq-7000 All Programmable SoC Technical Reference Manual.
 * (v1.4) November 16, 2012.  Xilinx doc UG585.  GEM is covered in Ch. 16
 * and register definitions are in appendix B.18.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>

#include <machine/bus.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_mib.h>
#include <net/if_types.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#endif

#include <net/bpf.h>
#include <net/bpfdesc.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/cadence/if_cgem_hw.h>

#include "miibus_if.h"

#define IF_CGEM_NAME "cgem"

#define CGEM_NUM_RX_DESCS	512	/* size of receive descriptor ring */
#define CGEM_NUM_TX_DESCS	512	/* size of transmit descriptor ring */

#define MAX_DESC_RING_SIZE (MAX(CGEM_NUM_RX_DESCS*sizeof(struct cgem_rx_desc),\
				CGEM_NUM_TX_DESCS*sizeof(struct cgem_tx_desc)))


/* Default for sysctl rxbufs.  Must be < CGEM_NUM_RX_DESCS of course. */
#define DEFAULT_NUM_RX_BUFS	256	/* number of receive bufs to queue. */

#define TX_MAX_DMA_SEGS		8	/* maximum segs in a tx mbuf dma */

#define CGEM_CKSUM_ASSIST	(CSUM_IP | CSUM_TCP | CSUM_UDP | \
				 CSUM_TCP_IPV6 | CSUM_UDP_IPV6)

struct cgem_softc {
	if_t			ifp;
	struct mtx		sc_mtx;
	device_t		dev;
	device_t		miibus;
	u_int			mii_media_active;	/* last active media */
	int			if_old_flags;
	struct resource 	*mem_res;
	struct resource 	*irq_res;
	void			*intrhand;
	struct callout		tick_ch;
	uint32_t		net_ctl_shadow;
	int			ref_clk_num;
	u_char			eaddr[6];

	bus_dma_tag_t		desc_dma_tag;
	bus_dma_tag_t		mbuf_dma_tag;

	/* receive descriptor ring */
	struct cgem_rx_desc	*rxring;
	bus_addr_t		rxring_physaddr;
	struct mbuf		*rxring_m[CGEM_NUM_RX_DESCS];
	bus_dmamap_t		rxring_m_dmamap[CGEM_NUM_RX_DESCS];
	int			rxring_hd_ptr;	/* where to put rcv bufs */
	int			rxring_tl_ptr;	/* where to get receives */
	int			rxring_queued;	/* how many rcv bufs queued */
 	bus_dmamap_t		rxring_dma_map;
	int			rxbufs;		/* tunable number rcv bufs */
	int			rxhangwar;	/* rx hang work-around */
	u_int			rxoverruns;	/* rx overruns */
	u_int			rxnobufs;	/* rx buf ring empty events */
	u_int			rxdmamapfails;	/* rx dmamap failures */
	uint32_t		rx_frames_prev;

	/* transmit descriptor ring */
	struct cgem_tx_desc	*txring;
	bus_addr_t		txring_physaddr;
	struct mbuf		*txring_m[CGEM_NUM_TX_DESCS];
	bus_dmamap_t		txring_m_dmamap[CGEM_NUM_TX_DESCS];
	int			txring_hd_ptr;	/* where to put next xmits */
	int			txring_tl_ptr;	/* next xmit mbuf to free */
	int			txring_queued;	/* num xmits segs queued */
	bus_dmamap_t		txring_dma_map;
	u_int			txfull;		/* tx ring full events */
	u_int			txdefrags;	/* tx calls to m_defrag() */
	u_int			txdefragfails;	/* tx m_defrag() failures */
	u_int			txdmamapfails;	/* tx dmamap failures */

	/* hardware provided statistics */
	struct cgem_hw_stats {
		uint64_t		tx_bytes;
		uint32_t		tx_frames;
		uint32_t		tx_frames_bcast;
		uint32_t		tx_frames_multi;
		uint32_t		tx_frames_pause;
		uint32_t		tx_frames_64b;
		uint32_t		tx_frames_65to127b;
		uint32_t		tx_frames_128to255b;
		uint32_t		tx_frames_256to511b;
		uint32_t		tx_frames_512to1023b;
		uint32_t		tx_frames_1024to1536b;
		uint32_t		tx_under_runs;
		uint32_t		tx_single_collisn;
		uint32_t		tx_multi_collisn;
		uint32_t		tx_excsv_collisn;
		uint32_t		tx_late_collisn;
		uint32_t		tx_deferred_frames;
		uint32_t		tx_carrier_sense_errs;

		uint64_t		rx_bytes;
		uint32_t		rx_frames;
		uint32_t		rx_frames_bcast;
		uint32_t		rx_frames_multi;
		uint32_t		rx_frames_pause;
		uint32_t		rx_frames_64b;
		uint32_t		rx_frames_65to127b;
		uint32_t		rx_frames_128to255b;
		uint32_t		rx_frames_256to511b;
		uint32_t		rx_frames_512to1023b;
		uint32_t		rx_frames_1024to1536b;
		uint32_t		rx_frames_undersize;
		uint32_t		rx_frames_oversize;
		uint32_t		rx_frames_jabber;
		uint32_t		rx_frames_fcs_errs;
		uint32_t		rx_frames_length_errs;
		uint32_t		rx_symbol_errs;
		uint32_t		rx_align_errs;
		uint32_t		rx_resource_errs;
		uint32_t		rx_overrun_errs;
		uint32_t		rx_ip_hdr_csum_errs;
		uint32_t		rx_tcp_csum_errs;
		uint32_t		rx_udp_csum_errs;
	} stats;
};

#define RD4(sc, off) 		(bus_read_4((sc)->mem_res, (off)))
#define WR4(sc, off, val) 	(bus_write_4((sc)->mem_res, (off), (val)))
#define BARRIER(sc, off, len, flags) \
	(bus_barrier((sc)->mem_res, (off), (len), (flags))

#define CGEM_LOCK(sc)		mtx_lock(&(sc)->sc_mtx)
#define CGEM_UNLOCK(sc)	mtx_unlock(&(sc)->sc_mtx)
#define CGEM_LOCK_INIT(sc)	\
	mtx_init(&(sc)->sc_mtx, device_get_nameunit((sc)->dev), \
		 MTX_NETWORK_LOCK, MTX_DEF)
#define CGEM_LOCK_DESTROY(sc)	mtx_destroy(&(sc)->sc_mtx)
#define CGEM_ASSERT_LOCKED(sc)	mtx_assert(&(sc)->sc_mtx, MA_OWNED)

/* Allow platforms to optionally provide a way to set the reference clock. */
int cgem_set_ref_clk(int unit, int frequency);

static devclass_t cgem_devclass;

static int cgem_probe(device_t dev);
static int cgem_attach(device_t dev);
static int cgem_detach(device_t dev);
static void cgem_tick(void *);
static void cgem_intr(void *);

static void cgem_mediachange(struct cgem_softc *, struct mii_data *);

static void
cgem_get_mac(struct cgem_softc *sc, u_char eaddr[])
{
	int i;
	uint32_t rnd;

	/* See if boot loader gave us a MAC address already. */
	for (i = 0; i < 4; i++) {
		uint32_t low = RD4(sc, CGEM_SPEC_ADDR_LOW(i));
		uint32_t high = RD4(sc, CGEM_SPEC_ADDR_HI(i)) & 0xffff;
		if (low != 0 || high != 0) {
			eaddr[0] = low & 0xff;
			eaddr[1] = (low >> 8) & 0xff;
			eaddr[2] = (low >> 16) & 0xff;
			eaddr[3] = (low >> 24) & 0xff;
			eaddr[4] = high & 0xff;
			eaddr[5] = (high >> 8) & 0xff;
			break;
		}
	}

	/* No MAC from boot loader?  Assign a random one. */
	if (i == 4) {
		rnd = arc4random();

		eaddr[0] = 'b';
		eaddr[1] = 's';
		eaddr[2] = 'd';
		eaddr[3] = (rnd >> 16) & 0xff;
		eaddr[4] = (rnd >> 8) & 0xff;
		eaddr[5] = rnd & 0xff;

		device_printf(sc->dev, "no mac address found, assigning "
			      "random: %02x:%02x:%02x:%02x:%02x:%02x\n",
			      eaddr[0], eaddr[1], eaddr[2],
			      eaddr[3], eaddr[4], eaddr[5]);
	}

	/* Move address to first slot and zero out the rest. */
	WR4(sc, CGEM_SPEC_ADDR_LOW(0), (eaddr[3] << 24) |
	    (eaddr[2] << 16) | (eaddr[1] << 8) | eaddr[0]);
	WR4(sc, CGEM_SPEC_ADDR_HI(0), (eaddr[5] << 8) | eaddr[4]);

	for (i = 1; i < 4; i++) {
		WR4(sc, CGEM_SPEC_ADDR_LOW(i), 0);
		WR4(sc, CGEM_SPEC_ADDR_HI(i), 0);
	}
}

/* cgem_mac_hash():  map 48-bit address to a 6-bit hash.
 * The 6-bit hash corresponds to a bit in a 64-bit hash
 * register.  Setting that bit in the hash register enables
 * reception of all frames with a destination address that hashes
 * to that 6-bit value.
 *
 * The hash function is described in sec. 16.2.3 in the Zynq-7000 Tech
 * Reference Manual.  Bits 0-5 in the hash are the exclusive-or of
 * every sixth bit in the destination address.
 */
static int
cgem_mac_hash(u_char eaddr[])
{
	int hash;
	int i, j;

	hash = 0;
	for (i = 0; i < 6; i++)
		for (j = i; j < 48; j += 6)
			if ((eaddr[j >> 3] & (1 << (j & 7))) != 0)
				hash ^= (1 << i);

	return hash;
}

/* After any change in rx flags or multi-cast addresses, set up
 * hash registers and net config register bits.
 */
static void
cgem_rx_filter(struct cgem_softc *sc)
{
	if_t ifp = sc->ifp;
	u_char *mta;

	int index, i, mcnt;
	uint32_t hash_hi, hash_lo;
	uint32_t net_cfg;

	hash_hi = 0;
	hash_lo = 0;

	net_cfg = RD4(sc, CGEM_NET_CFG);

	net_cfg &= ~(CGEM_NET_CFG_MULTI_HASH_EN |
		     CGEM_NET_CFG_NO_BCAST | 
		     CGEM_NET_CFG_COPY_ALL);

	if ((if_getflags(ifp) & IFF_PROMISC) != 0)
		net_cfg |= CGEM_NET_CFG_COPY_ALL;
	else {
		if ((if_getflags(ifp) & IFF_BROADCAST) == 0)
			net_cfg |= CGEM_NET_CFG_NO_BCAST;
		if ((if_getflags(ifp) & IFF_ALLMULTI) != 0) {
			hash_hi = 0xffffffff;
			hash_lo = 0xffffffff;
		} else {
			mcnt = if_multiaddr_count(ifp, -1);
			mta = malloc(ETHER_ADDR_LEN * mcnt, M_DEVBUF,
				     M_NOWAIT);
			if (mta == NULL) {
				device_printf(sc->dev,
				      "failed to allocate temp mcast list\n");
				return;
			}
			if_multiaddr_array(ifp, mta, &mcnt, mcnt);
			for (i = 0; i < mcnt; i++) {
				index = cgem_mac_hash(
					LLADDR((struct sockaddr_dl *)
					       (mta + (i * ETHER_ADDR_LEN))));
				if (index > 31)
					hash_hi |= (1 << (index - 32));
				else
					hash_lo |= (1 << index);
			}
			free(mta, M_DEVBUF);
		}

		if (hash_hi != 0 || hash_lo != 0)
			net_cfg |= CGEM_NET_CFG_MULTI_HASH_EN;
	}

	WR4(sc, CGEM_HASH_TOP, hash_hi);
	WR4(sc, CGEM_HASH_BOT, hash_lo);
	WR4(sc, CGEM_NET_CFG, net_cfg);
}

/* For bus_dmamap_load() callback. */
static void
cgem_getaddr(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{

	if (nsegs != 1 || error != 0)
		return;
	*(bus_addr_t *)arg = segs[0].ds_addr;
}

/* Create DMA'able descriptor rings. */
static int
cgem_setup_descs(struct cgem_softc *sc)
{
	int i, err;

	sc->txring = NULL;
	sc->rxring = NULL;

	/* Allocate non-cached DMA space for RX and TX descriptors.
	 */
	err = bus_dma_tag_create(bus_get_dma_tag(sc->dev), 1, 0,
				 BUS_SPACE_MAXADDR_32BIT,
				 BUS_SPACE_MAXADDR,
				 NULL, NULL,
				 MAX_DESC_RING_SIZE,
				 1,
				 MAX_DESC_RING_SIZE,
				 0,
				 busdma_lock_mutex,
				 &sc->sc_mtx,
				 &sc->desc_dma_tag);
	if (err)
		return (err);

	/* Set up a bus_dma_tag for mbufs. */
	err = bus_dma_tag_create(bus_get_dma_tag(sc->dev), 1, 0,
				 BUS_SPACE_MAXADDR_32BIT,
				 BUS_SPACE_MAXADDR,
				 NULL, NULL,
				 MCLBYTES,
				 TX_MAX_DMA_SEGS,
				 MCLBYTES,
				 0,
				 busdma_lock_mutex,
				 &sc->sc_mtx,
				 &sc->mbuf_dma_tag);
	if (err)
		return (err);

	/* Allocate DMA memory in non-cacheable space. */
	err = bus_dmamem_alloc(sc->desc_dma_tag,
			       (void **)&sc->rxring,
			       BUS_DMA_NOWAIT | BUS_DMA_COHERENT,
			       &sc->rxring_dma_map);
	if (err)
		return (err);

	/* Load descriptor DMA memory. */
	err = bus_dmamap_load(sc->desc_dma_tag, sc->rxring_dma_map,
			      (void *)sc->rxring,
			      CGEM_NUM_RX_DESCS*sizeof(struct cgem_rx_desc),
			      cgem_getaddr, &sc->rxring_physaddr,
			      BUS_DMA_NOWAIT);
	if (err)
		return (err);

	/* Initialize RX descriptors. */
	for (i = 0; i < CGEM_NUM_RX_DESCS; i++) {
		sc->rxring[i].addr = CGEM_RXDESC_OWN;
		sc->rxring[i].ctl = 0;
		sc->rxring_m[i] = NULL;
		sc->rxring_m_dmamap[i] = NULL;
	}
	sc->rxring[CGEM_NUM_RX_DESCS - 1].addr |= CGEM_RXDESC_WRAP;

	sc->rxring_hd_ptr = 0;
	sc->rxring_tl_ptr = 0;
	sc->rxring_queued = 0;

	/* Allocate DMA memory for TX descriptors in non-cacheable space. */
	err = bus_dmamem_alloc(sc->desc_dma_tag,
			       (void **)&sc->txring,
			       BUS_DMA_NOWAIT | BUS_DMA_COHERENT,
			       &sc->txring_dma_map);
	if (err)
		return (err);

	/* Load TX descriptor DMA memory. */
	err = bus_dmamap_load(sc->desc_dma_tag, sc->txring_dma_map,
			      (void *)sc->txring,
			      CGEM_NUM_TX_DESCS*sizeof(struct cgem_tx_desc),
			      cgem_getaddr, &sc->txring_physaddr, 
			      BUS_DMA_NOWAIT);
	if (err)
		return (err);

	/* Initialize TX descriptor ring. */
	for (i = 0; i < CGEM_NUM_TX_DESCS; i++) {
		sc->txring[i].addr = 0;
		sc->txring[i].ctl = CGEM_TXDESC_USED;
		sc->txring_m[i] = NULL;
		sc->txring_m_dmamap[i] = NULL;
	}
	sc->txring[CGEM_NUM_TX_DESCS - 1].ctl |= CGEM_TXDESC_WRAP;

	sc->txring_hd_ptr = 0;
	sc->txring_tl_ptr = 0;
	sc->txring_queued = 0;

	return (0);
}

/* Fill receive descriptor ring with mbufs. */
static void
cgem_fill_rqueue(struct cgem_softc *sc)
{
	struct mbuf *m = NULL;
	bus_dma_segment_t segs[TX_MAX_DMA_SEGS];
	int nsegs;

	CGEM_ASSERT_LOCKED(sc);

	while (sc->rxring_queued < sc->rxbufs) {
		/* Get a cluster mbuf. */
		m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
		if (m == NULL)
			break;

		m->m_len = MCLBYTES;
		m->m_pkthdr.len = MCLBYTES;
		m->m_pkthdr.rcvif = sc->ifp;

		/* Load map and plug in physical address. */
		if (bus_dmamap_create(sc->mbuf_dma_tag, 0,
			      &sc->rxring_m_dmamap[sc->rxring_hd_ptr])) {
			sc->rxdmamapfails++;
			m_free(m);
			break;
		}
		if (bus_dmamap_load_mbuf_sg(sc->mbuf_dma_tag, 
			      sc->rxring_m_dmamap[sc->rxring_hd_ptr], m,
			      segs, &nsegs, BUS_DMA_NOWAIT)) {
			sc->rxdmamapfails++;
			bus_dmamap_destroy(sc->mbuf_dma_tag,
				   sc->rxring_m_dmamap[sc->rxring_hd_ptr]);
			sc->rxring_m_dmamap[sc->rxring_hd_ptr] = NULL;
			m_free(m);
			break;
		}
		sc->rxring_m[sc->rxring_hd_ptr] = m;

		/* Sync cache with receive buffer. */
		bus_dmamap_sync(sc->mbuf_dma_tag,
				sc->rxring_m_dmamap[sc->rxring_hd_ptr],
				BUS_DMASYNC_PREREAD);

		/* Write rx descriptor and increment head pointer. */
		sc->rxring[sc->rxring_hd_ptr].ctl = 0;
		if (sc->rxring_hd_ptr == CGEM_NUM_RX_DESCS - 1) {
			sc->rxring[sc->rxring_hd_ptr].addr = segs[0].ds_addr |
				CGEM_RXDESC_WRAP;
			sc->rxring_hd_ptr = 0;
		} else
			sc->rxring[sc->rxring_hd_ptr++].addr = segs[0].ds_addr;
			
		sc->rxring_queued++;
	}
}

/* Pull received packets off of receive descriptor ring. */
static void
cgem_recv(struct cgem_softc *sc)
{
	if_t ifp = sc->ifp;
	struct mbuf *m, *m_hd, **m_tl;
	uint32_t ctl;

	CGEM_ASSERT_LOCKED(sc);

	/* Pick up all packets in which the OWN bit is set. */
	m_hd = NULL;
	m_tl = &m_hd;
	while (sc->rxring_queued > 0 &&
	       (sc->rxring[sc->rxring_tl_ptr].addr & CGEM_RXDESC_OWN) != 0) {

		ctl = sc->rxring[sc->rxring_tl_ptr].ctl;

		/* Grab filled mbuf. */
		m = sc->rxring_m[sc->rxring_tl_ptr];
		sc->rxring_m[sc->rxring_tl_ptr] = NULL;

		/* Sync cache with receive buffer. */
		bus_dmamap_sync(sc->mbuf_dma_tag,
				sc->rxring_m_dmamap[sc->rxring_tl_ptr],
				BUS_DMASYNC_POSTREAD);

		/* Unload and destroy dmamap. */
		bus_dmamap_unload(sc->mbuf_dma_tag,
		  	sc->rxring_m_dmamap[sc->rxring_tl_ptr]);
		bus_dmamap_destroy(sc->mbuf_dma_tag,
				   sc->rxring_m_dmamap[sc->rxring_tl_ptr]);
		sc->rxring_m_dmamap[sc->rxring_tl_ptr] = NULL;

		/* Increment tail pointer. */
		if (++sc->rxring_tl_ptr == CGEM_NUM_RX_DESCS)
			sc->rxring_tl_ptr = 0;
		sc->rxring_queued--;

		/* Check FCS and make sure entire packet landed in one mbuf
		 * cluster (which is much bigger than the largest ethernet
		 * packet).
		 */
		if ((ctl & CGEM_RXDESC_BAD_FCS) != 0 ||
		    (ctl & (CGEM_RXDESC_SOF | CGEM_RXDESC_EOF)) !=
		           (CGEM_RXDESC_SOF | CGEM_RXDESC_EOF)) {
			/* discard. */
			m_free(m);
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			continue;
		}

		/* Ready it to hand off to upper layers. */
		m->m_data += ETHER_ALIGN;
		m->m_len = (ctl & CGEM_RXDESC_LENGTH_MASK);
		m->m_pkthdr.rcvif = ifp;
		m->m_pkthdr.len = m->m_len;

		/* Are we using hardware checksumming?  Check the
		 * status in the receive descriptor.
		 */
		if ((if_getcapenable(ifp) & IFCAP_RXCSUM) != 0) {
			/* TCP or UDP checks out, IP checks out too. */
			if ((ctl & CGEM_RXDESC_CKSUM_STAT_MASK) ==
			    CGEM_RXDESC_CKSUM_STAT_TCP_GOOD ||
			    (ctl & CGEM_RXDESC_CKSUM_STAT_MASK) ==
			    CGEM_RXDESC_CKSUM_STAT_UDP_GOOD) {
				m->m_pkthdr.csum_flags |=
					CSUM_IP_CHECKED | CSUM_IP_VALID |
					CSUM_DATA_VALID | CSUM_PSEUDO_HDR;
				m->m_pkthdr.csum_data = 0xffff;
			} else if ((ctl & CGEM_RXDESC_CKSUM_STAT_MASK) ==
				   CGEM_RXDESC_CKSUM_STAT_IP_GOOD) {
				/* Only IP checks out. */
				m->m_pkthdr.csum_flags |=
					CSUM_IP_CHECKED | CSUM_IP_VALID;
				m->m_pkthdr.csum_data = 0xffff;
			}
		}

		/* Queue it up for delivery below. */
		*m_tl = m;
		m_tl = &m->m_next;
	}

	/* Replenish receive buffers. */
	cgem_fill_rqueue(sc);

	/* Unlock and send up packets. */
	CGEM_UNLOCK(sc);
	while (m_hd != NULL) {
		m = m_hd;
		m_hd = m_hd->m_next;
		m->m_next = NULL;
		if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
		if_input(ifp, m);
	}
	CGEM_LOCK(sc);
}

/* Find completed transmits and free their mbufs. */
static void
cgem_clean_tx(struct cgem_softc *sc)
{
	struct mbuf *m;
	uint32_t ctl;

	CGEM_ASSERT_LOCKED(sc);

	/* free up finished transmits. */
	while (sc->txring_queued > 0 &&
	       ((ctl = sc->txring[sc->txring_tl_ptr].ctl) &
		CGEM_TXDESC_USED) != 0) {

		/* Sync cache. */
		bus_dmamap_sync(sc->mbuf_dma_tag,
				sc->txring_m_dmamap[sc->txring_tl_ptr],
				BUS_DMASYNC_POSTWRITE);

		/* Unload and destroy DMA map. */
		bus_dmamap_unload(sc->mbuf_dma_tag,
				  sc->txring_m_dmamap[sc->txring_tl_ptr]);
		bus_dmamap_destroy(sc->mbuf_dma_tag,
				   sc->txring_m_dmamap[sc->txring_tl_ptr]);
		sc->txring_m_dmamap[sc->txring_tl_ptr] = NULL;

		/* Free up the mbuf. */
		m = sc->txring_m[sc->txring_tl_ptr];
		sc->txring_m[sc->txring_tl_ptr] = NULL;
		m_freem(m);

		/* Check the status. */
		if ((ctl & CGEM_TXDESC_AHB_ERR) != 0) {
			/* Serious bus error. log to console. */
			device_printf(sc->dev, "cgem_clean_tx: Whoa! "
				   "AHB error, addr=0x%x\n",
				   sc->txring[sc->txring_tl_ptr].addr);
		} else if ((ctl & (CGEM_TXDESC_RETRY_ERR |
				   CGEM_TXDESC_LATE_COLL)) != 0) {
			if_inc_counter(sc->ifp, IFCOUNTER_OERRORS, 1);
		} else
			if_inc_counter(sc->ifp, IFCOUNTER_OPACKETS, 1);

		/* If the packet spanned more than one tx descriptor,
		 * skip descriptors until we find the end so that only
		 * start-of-frame descriptors are processed.
		 */
		while ((ctl & CGEM_TXDESC_LAST_BUF) == 0) {
			if ((ctl & CGEM_TXDESC_WRAP) != 0)
				sc->txring_tl_ptr = 0;
			else
				sc->txring_tl_ptr++;
			sc->txring_queued--;

			ctl = sc->txring[sc->txring_tl_ptr].ctl;

			sc->txring[sc->txring_tl_ptr].ctl =
				ctl | CGEM_TXDESC_USED;
		}

		/* Next descriptor. */
		if ((ctl & CGEM_TXDESC_WRAP) != 0)
			sc->txring_tl_ptr = 0;
		else
			sc->txring_tl_ptr++;
		sc->txring_queued--;

		if_setdrvflagbits(sc->ifp, 0, IFF_DRV_OACTIVE);
	}
}

/* Start transmits. */
static void
cgem_start_locked(if_t ifp)
{
	struct cgem_softc *sc = (struct cgem_softc *) if_getsoftc(ifp);
	struct mbuf *m;
	bus_dma_segment_t segs[TX_MAX_DMA_SEGS];
	uint32_t ctl;
	int i, nsegs, wrap, err;

	CGEM_ASSERT_LOCKED(sc);

	if ((if_getdrvflags(ifp) & IFF_DRV_OACTIVE) != 0)
		return;

	for (;;) {
		/* Check that there is room in the descriptor ring. */
		if (sc->txring_queued >=
		    CGEM_NUM_TX_DESCS - TX_MAX_DMA_SEGS * 2) {

			/* Try to make room. */
			cgem_clean_tx(sc);

			/* Still no room? */
			if (sc->txring_queued >=
			    CGEM_NUM_TX_DESCS - TX_MAX_DMA_SEGS * 2) {
				if_setdrvflagbits(ifp, IFF_DRV_OACTIVE, 0);
				sc->txfull++;
				break;
			}
		}

		/* Grab next transmit packet. */
		m = if_dequeue(ifp);
		if (m == NULL)
			break;

		/* Create and load DMA map. */
		if (bus_dmamap_create(sc->mbuf_dma_tag, 0,
			      &sc->txring_m_dmamap[sc->txring_hd_ptr])) {
			m_freem(m);
			sc->txdmamapfails++;
			continue;
		}
		err = bus_dmamap_load_mbuf_sg(sc->mbuf_dma_tag,
				      sc->txring_m_dmamap[sc->txring_hd_ptr],
				      m, segs, &nsegs, BUS_DMA_NOWAIT);
		if (err == EFBIG) {
			/* Too many segments!  defrag and try again. */
			struct mbuf *m2 = m_defrag(m, M_NOWAIT);

			if (m2 == NULL) {
				sc->txdefragfails++;
				m_freem(m);
				bus_dmamap_destroy(sc->mbuf_dma_tag,
				   sc->txring_m_dmamap[sc->txring_hd_ptr]);
				sc->txring_m_dmamap[sc->txring_hd_ptr] = NULL;
				continue;
			}
			m = m2;
			err = bus_dmamap_load_mbuf_sg(sc->mbuf_dma_tag,
				      sc->txring_m_dmamap[sc->txring_hd_ptr],
				      m, segs, &nsegs, BUS_DMA_NOWAIT);
			sc->txdefrags++;
		}
		if (err) {
			/* Give up. */
			m_freem(m);
			bus_dmamap_destroy(sc->mbuf_dma_tag,
				   sc->txring_m_dmamap[sc->txring_hd_ptr]);
			sc->txring_m_dmamap[sc->txring_hd_ptr] = NULL;
			sc->txdmamapfails++;
			continue;
		}
		sc->txring_m[sc->txring_hd_ptr] = m;

		/* Sync tx buffer with cache. */
		bus_dmamap_sync(sc->mbuf_dma_tag,
				sc->txring_m_dmamap[sc->txring_hd_ptr],
				BUS_DMASYNC_PREWRITE);

		/* Set wrap flag if next packet might run off end of ring. */
		wrap = sc->txring_hd_ptr + nsegs + TX_MAX_DMA_SEGS >=
			CGEM_NUM_TX_DESCS;

		/* Fill in the TX descriptors back to front so that USED
		 * bit in first descriptor is cleared last.
		 */
		for (i = nsegs - 1; i >= 0; i--) {
			/* Descriptor address. */
			sc->txring[sc->txring_hd_ptr + i].addr =
				segs[i].ds_addr;

			/* Descriptor control word. */
			ctl = segs[i].ds_len;
			if (i == nsegs - 1) {
				ctl |= CGEM_TXDESC_LAST_BUF;
				if (wrap)
					ctl |= CGEM_TXDESC_WRAP;
			}
			sc->txring[sc->txring_hd_ptr + i].ctl = ctl;

			if (i != 0)
				sc->txring_m[sc->txring_hd_ptr + i] = NULL;
		}

		if (wrap)
			sc->txring_hd_ptr = 0;
		else
			sc->txring_hd_ptr += nsegs;
		sc->txring_queued += nsegs;

		/* Kick the transmitter. */
		WR4(sc, CGEM_NET_CTRL, sc->net_ctl_shadow |
		    CGEM_NET_CTRL_START_TX);

		/* If there is a BPF listener, bounce a copy to him. */
		ETHER_BPF_MTAP(ifp, m);
	}
}

static void
cgem_start(if_t ifp)
{
	struct cgem_softc *sc = (struct cgem_softc *) if_getsoftc(ifp);

	CGEM_LOCK(sc);
	cgem_start_locked(ifp);
	CGEM_UNLOCK(sc);
}

static void
cgem_poll_hw_stats(struct cgem_softc *sc)
{
	uint32_t n;

	CGEM_ASSERT_LOCKED(sc);

	sc->stats.tx_bytes += RD4(sc, CGEM_OCTETS_TX_BOT);
	sc->stats.tx_bytes += (uint64_t)RD4(sc, CGEM_OCTETS_TX_TOP) << 32;

	sc->stats.tx_frames += RD4(sc, CGEM_FRAMES_TX);
	sc->stats.tx_frames_bcast += RD4(sc, CGEM_BCAST_FRAMES_TX);
	sc->stats.tx_frames_multi += RD4(sc, CGEM_MULTI_FRAMES_TX);
	sc->stats.tx_frames_pause += RD4(sc, CGEM_PAUSE_FRAMES_TX);
	sc->stats.tx_frames_64b += RD4(sc, CGEM_FRAMES_64B_TX);
	sc->stats.tx_frames_65to127b += RD4(sc, CGEM_FRAMES_65_127B_TX);
	sc->stats.tx_frames_128to255b += RD4(sc, CGEM_FRAMES_128_255B_TX);
	sc->stats.tx_frames_256to511b += RD4(sc, CGEM_FRAMES_256_511B_TX);
	sc->stats.tx_frames_512to1023b += RD4(sc, CGEM_FRAMES_512_1023B_TX);
	sc->stats.tx_frames_1024to1536b += RD4(sc, CGEM_FRAMES_1024_1518B_TX);
	sc->stats.tx_under_runs += RD4(sc, CGEM_TX_UNDERRUNS);

	n = RD4(sc, CGEM_SINGLE_COLL_FRAMES);
	sc->stats.tx_single_collisn += n;
	if_inc_counter(sc->ifp, IFCOUNTER_COLLISIONS, n);
	n = RD4(sc, CGEM_MULTI_COLL_FRAMES);
	sc->stats.tx_multi_collisn += n;
	if_inc_counter(sc->ifp, IFCOUNTER_COLLISIONS, n);
	n = RD4(sc, CGEM_EXCESSIVE_COLL_FRAMES);
	sc->stats.tx_excsv_collisn += n;
	if_inc_counter(sc->ifp, IFCOUNTER_COLLISIONS, n);
	n = RD4(sc, CGEM_LATE_COLL);
	sc->stats.tx_late_collisn += n;
	if_inc_counter(sc->ifp, IFCOUNTER_COLLISIONS, n);

	sc->stats.tx_deferred_frames += RD4(sc, CGEM_DEFERRED_TX_FRAMES);
	sc->stats.tx_carrier_sense_errs += RD4(sc, CGEM_CARRIER_SENSE_ERRS);

	sc->stats.rx_bytes += RD4(sc, CGEM_OCTETS_RX_BOT);
	sc->stats.rx_bytes += (uint64_t)RD4(sc, CGEM_OCTETS_RX_TOP) << 32;

	sc->stats.rx_frames += RD4(sc, CGEM_FRAMES_RX);
	sc->stats.rx_frames_bcast += RD4(sc, CGEM_BCAST_FRAMES_RX);
	sc->stats.rx_frames_multi += RD4(sc, CGEM_MULTI_FRAMES_RX);
	sc->stats.rx_frames_pause += RD4(sc, CGEM_PAUSE_FRAMES_RX);
	sc->stats.rx_frames_64b += RD4(sc, CGEM_FRAMES_64B_RX);
	sc->stats.rx_frames_65to127b += RD4(sc, CGEM_FRAMES_65_127B_RX);
	sc->stats.rx_frames_128to255b += RD4(sc, CGEM_FRAMES_128_255B_RX);
	sc->stats.rx_frames_256to511b += RD4(sc, CGEM_FRAMES_256_511B_RX);
	sc->stats.rx_frames_512to1023b += RD4(sc, CGEM_FRAMES_512_1023B_RX);
	sc->stats.rx_frames_1024to1536b += RD4(sc, CGEM_FRAMES_1024_1518B_RX);
	sc->stats.rx_frames_undersize += RD4(sc, CGEM_UNDERSZ_RX);
	sc->stats.rx_frames_oversize += RD4(sc, CGEM_OVERSZ_RX);
	sc->stats.rx_frames_jabber += RD4(sc, CGEM_JABBERS_RX);
	sc->stats.rx_frames_fcs_errs += RD4(sc, CGEM_FCS_ERRS);
	sc->stats.rx_frames_length_errs += RD4(sc, CGEM_LENGTH_FIELD_ERRS);
	sc->stats.rx_symbol_errs += RD4(sc, CGEM_RX_SYMBOL_ERRS);
	sc->stats.rx_align_errs += RD4(sc, CGEM_ALIGN_ERRS);
	sc->stats.rx_resource_errs += RD4(sc, CGEM_RX_RESOURCE_ERRS);
	sc->stats.rx_overrun_errs += RD4(sc, CGEM_RX_OVERRUN_ERRS);
	sc->stats.rx_ip_hdr_csum_errs += RD4(sc, CGEM_IP_HDR_CKSUM_ERRS);
	sc->stats.rx_tcp_csum_errs += RD4(sc, CGEM_TCP_CKSUM_ERRS);
	sc->stats.rx_udp_csum_errs += RD4(sc, CGEM_UDP_CKSUM_ERRS);
}

static void
cgem_tick(void *arg)
{
	struct cgem_softc *sc = (struct cgem_softc *)arg;
	struct mii_data *mii;

	CGEM_ASSERT_LOCKED(sc);

	/* Poll the phy. */
	if (sc->miibus != NULL) {
		mii = device_get_softc(sc->miibus);
		mii_tick(mii);
	}

	/* Poll statistics registers. */
	cgem_poll_hw_stats(sc);

	/* Check for receiver hang. */
	if (sc->rxhangwar && sc->rx_frames_prev == sc->stats.rx_frames) {
		/*
		 * Reset receiver logic by toggling RX_EN bit.  1usec
		 * delay is necessary especially when operating at 100mbps
		 * and 10mbps speeds.
		 */
		WR4(sc, CGEM_NET_CTRL, sc->net_ctl_shadow &
		    ~CGEM_NET_CTRL_RX_EN);
		DELAY(1);
		WR4(sc, CGEM_NET_CTRL, sc->net_ctl_shadow);
	}
	sc->rx_frames_prev = sc->stats.rx_frames;

	/* Next callout in one second. */
	callout_reset(&sc->tick_ch, hz, cgem_tick, sc);
}

/* Interrupt handler. */
static void
cgem_intr(void *arg)
{
	struct cgem_softc *sc = (struct cgem_softc *)arg;
	if_t ifp = sc->ifp;
	uint32_t istatus;

	CGEM_LOCK(sc);

	if ((if_getdrvflags(ifp) & IFF_DRV_RUNNING) == 0) {
		CGEM_UNLOCK(sc);
		return;
	}

	/* Read interrupt status and immediately clear the bits. */
	istatus = RD4(sc, CGEM_INTR_STAT);
	WR4(sc, CGEM_INTR_STAT, istatus);

	/* Packets received. */
	if ((istatus & CGEM_INTR_RX_COMPLETE) != 0)
		cgem_recv(sc);

	/* Free up any completed transmit buffers. */
	cgem_clean_tx(sc);

	/* Hresp not ok.  Something is very bad with DMA.  Try to clear. */
	if ((istatus & CGEM_INTR_HRESP_NOT_OK) != 0) {
		device_printf(sc->dev, "cgem_intr: hresp not okay! "
			      "rx_status=0x%x\n", RD4(sc, CGEM_RX_STAT));
		WR4(sc, CGEM_RX_STAT, CGEM_RX_STAT_HRESP_NOT_OK);
	}

	/* Receiver overrun. */
	if ((istatus & CGEM_INTR_RX_OVERRUN) != 0) {
		/* Clear status bit. */
		WR4(sc, CGEM_RX_STAT, CGEM_RX_STAT_OVERRUN);
		sc->rxoverruns++;
	}

	/* Receiver ran out of bufs. */
	if ((istatus & CGEM_INTR_RX_USED_READ) != 0) {
		WR4(sc, CGEM_NET_CTRL, sc->net_ctl_shadow |
		    CGEM_NET_CTRL_FLUSH_DPRAM_PKT);
		cgem_fill_rqueue(sc);
		sc->rxnobufs++;
	}

	/* Restart transmitter if needed. */
	if (!if_sendq_empty(ifp))
		cgem_start_locked(ifp);

	CGEM_UNLOCK(sc);
}

/* Reset hardware. */
static void
cgem_reset(struct cgem_softc *sc)
{

	CGEM_ASSERT_LOCKED(sc);

	WR4(sc, CGEM_NET_CTRL, 0);
	WR4(sc, CGEM_NET_CFG, 0);
	WR4(sc, CGEM_NET_CTRL, CGEM_NET_CTRL_CLR_STAT_REGS);
	WR4(sc, CGEM_TX_STAT, CGEM_TX_STAT_ALL);
	WR4(sc, CGEM_RX_STAT, CGEM_RX_STAT_ALL);
	WR4(sc, CGEM_INTR_DIS, CGEM_INTR_ALL);
	WR4(sc, CGEM_HASH_BOT, 0);
	WR4(sc, CGEM_HASH_TOP, 0);
	WR4(sc, CGEM_TX_QBAR, 0);	/* manual says do this. */
	WR4(sc, CGEM_RX_QBAR, 0);

	/* Get management port running even if interface is down. */
	WR4(sc, CGEM_NET_CFG,
	    CGEM_NET_CFG_DBUS_WIDTH_32 |
	    CGEM_NET_CFG_MDC_CLK_DIV_64);

	sc->net_ctl_shadow = CGEM_NET_CTRL_MGMT_PORT_EN;
	WR4(sc, CGEM_NET_CTRL, sc->net_ctl_shadow);
}

/* Bring up the hardware. */
static void
cgem_config(struct cgem_softc *sc)
{
	if_t ifp = sc->ifp;
	uint32_t net_cfg;
	uint32_t dma_cfg;
	u_char *eaddr = if_getlladdr(ifp);

	CGEM_ASSERT_LOCKED(sc);

	/* Program Net Config Register. */
	net_cfg = CGEM_NET_CFG_DBUS_WIDTH_32 |
		CGEM_NET_CFG_MDC_CLK_DIV_64 |
		CGEM_NET_CFG_FCS_REMOVE |
		CGEM_NET_CFG_RX_BUF_OFFSET(ETHER_ALIGN) |
		CGEM_NET_CFG_GIGE_EN |
		CGEM_NET_CFG_1536RXEN |
		CGEM_NET_CFG_FULL_DUPLEX |
		CGEM_NET_CFG_SPEED100;

	/* Enable receive checksum offloading? */
	if ((if_getcapenable(ifp) & IFCAP_RXCSUM) != 0)
		net_cfg |=  CGEM_NET_CFG_RX_CHKSUM_OFFLD_EN;

	WR4(sc, CGEM_NET_CFG, net_cfg);

	/* Program DMA Config Register. */
	dma_cfg = CGEM_DMA_CFG_RX_BUF_SIZE(MCLBYTES) |
		CGEM_DMA_CFG_RX_PKTBUF_MEMSZ_SEL_8K |
		CGEM_DMA_CFG_TX_PKTBUF_MEMSZ_SEL |
		CGEM_DMA_CFG_AHB_FIXED_BURST_LEN_16 |
		CGEM_DMA_CFG_DISC_WHEN_NO_AHB;

	/* Enable transmit checksum offloading? */
	if ((if_getcapenable(ifp) & IFCAP_TXCSUM) != 0)
		dma_cfg |= CGEM_DMA_CFG_CHKSUM_GEN_OFFLOAD_EN;

	WR4(sc, CGEM_DMA_CFG, dma_cfg);

	/* Write the rx and tx descriptor ring addresses to the QBAR regs. */
	WR4(sc, CGEM_RX_QBAR, (uint32_t) sc->rxring_physaddr);
	WR4(sc, CGEM_TX_QBAR, (uint32_t) sc->txring_physaddr);
	
	/* Enable rx and tx. */
	sc->net_ctl_shadow |= (CGEM_NET_CTRL_TX_EN | CGEM_NET_CTRL_RX_EN);
	WR4(sc, CGEM_NET_CTRL, sc->net_ctl_shadow);

	/* Set receive address in case it changed. */
	WR4(sc, CGEM_SPEC_ADDR_LOW(0), (eaddr[3] << 24) |
	    (eaddr[2] << 16) | (eaddr[1] << 8) | eaddr[0]);
	WR4(sc, CGEM_SPEC_ADDR_HI(0), (eaddr[5] << 8) | eaddr[4]);

	/* Set up interrupts. */
	WR4(sc, CGEM_INTR_EN,
	    CGEM_INTR_RX_COMPLETE | CGEM_INTR_RX_OVERRUN |
	    CGEM_INTR_TX_USED_READ | CGEM_INTR_RX_USED_READ |
	    CGEM_INTR_HRESP_NOT_OK);
}

/* Turn on interface and load up receive ring with buffers. */
static void
cgem_init_locked(struct cgem_softc *sc)
{
	struct mii_data *mii;

	CGEM_ASSERT_LOCKED(sc);

	if ((if_getdrvflags(sc->ifp) & IFF_DRV_RUNNING) != 0)
		return;

	cgem_config(sc);
	cgem_fill_rqueue(sc);

	if_setdrvflagbits(sc->ifp, IFF_DRV_RUNNING, IFF_DRV_OACTIVE);

	mii = device_get_softc(sc->miibus);
	mii_mediachg(mii);

	callout_reset(&sc->tick_ch, hz, cgem_tick, sc);
}

static void
cgem_init(void *arg)
{
	struct cgem_softc *sc = (struct cgem_softc *)arg;

	CGEM_LOCK(sc);
	cgem_init_locked(sc);
	CGEM_UNLOCK(sc);
}

/* Turn off interface.  Free up any buffers in transmit or receive queues. */
static void
cgem_stop(struct cgem_softc *sc)
{
	int i;

	CGEM_ASSERT_LOCKED(sc);

	callout_stop(&sc->tick_ch);

	/* Shut down hardware. */
	cgem_reset(sc);

	/* Clear out transmit queue. */
	for (i = 0; i < CGEM_NUM_TX_DESCS; i++) {
		sc->txring[i].ctl = CGEM_TXDESC_USED;
		sc->txring[i].addr = 0;
		if (sc->txring_m[i]) {
			/* Unload and destroy dmamap. */
			bus_dmamap_unload(sc->mbuf_dma_tag,
					  sc->txring_m_dmamap[i]);
			bus_dmamap_destroy(sc->mbuf_dma_tag,
					   sc->txring_m_dmamap[i]);
			sc->txring_m_dmamap[i] = NULL;
			m_freem(sc->txring_m[i]);
			sc->txring_m[i] = NULL;
		}
	}
	sc->txring[CGEM_NUM_TX_DESCS - 1].ctl |= CGEM_TXDESC_WRAP;

	sc->txring_hd_ptr = 0;
	sc->txring_tl_ptr = 0;
	sc->txring_queued = 0;

	/* Clear out receive queue. */
	for (i = 0; i < CGEM_NUM_RX_DESCS; i++) {
		sc->rxring[i].addr = CGEM_RXDESC_OWN;
		sc->rxring[i].ctl = 0;
		if (sc->rxring_m[i]) {
			/* Unload and destroy dmamap. */
			bus_dmamap_unload(sc->mbuf_dma_tag,
				  sc->rxring_m_dmamap[i]);
			bus_dmamap_destroy(sc->mbuf_dma_tag,
				   sc->rxring_m_dmamap[i]);
			sc->rxring_m_dmamap[i] = NULL;

			m_freem(sc->rxring_m[i]);
			sc->rxring_m[i] = NULL;
		}
	}
	sc->rxring[CGEM_NUM_RX_DESCS - 1].addr |= CGEM_RXDESC_WRAP;

	sc->rxring_hd_ptr = 0;
	sc->rxring_tl_ptr = 0;
	sc->rxring_queued = 0;

	/* Force next statchg or linkchg to program net config register. */
	sc->mii_media_active = 0;
}


static int
cgem_ioctl(if_t ifp, u_long cmd, caddr_t data)
{
	struct cgem_softc *sc = if_getsoftc(ifp);
	struct ifreq *ifr = (struct ifreq *)data;
	struct mii_data *mii;
	int error = 0, mask;

	switch (cmd) {
	case SIOCSIFFLAGS:
		CGEM_LOCK(sc);
		if ((if_getflags(ifp) & IFF_UP) != 0) {
			if ((if_getdrvflags(ifp) & IFF_DRV_RUNNING) != 0) {
				if (((if_getflags(ifp) ^ sc->if_old_flags) &
				     (IFF_PROMISC | IFF_ALLMULTI)) != 0) {
					cgem_rx_filter(sc);
				}
			} else {
				cgem_init_locked(sc);
			}
		} else if ((if_getdrvflags(ifp) & IFF_DRV_RUNNING) != 0) {
			if_setdrvflagbits(ifp, 0, IFF_DRV_RUNNING);
			cgem_stop(sc);
		}
		sc->if_old_flags = if_getflags(ifp);
		CGEM_UNLOCK(sc);
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		/* Set up multi-cast filters. */
		if ((if_getdrvflags(ifp) & IFF_DRV_RUNNING) != 0) {
			CGEM_LOCK(sc);
			cgem_rx_filter(sc);
			CGEM_UNLOCK(sc);
		}
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		mii = device_get_softc(sc->miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, cmd);
		break;

	case SIOCSIFCAP:
		CGEM_LOCK(sc);
		mask = if_getcapenable(ifp) ^ ifr->ifr_reqcap;

		if ((mask & IFCAP_TXCSUM) != 0) {
			if ((ifr->ifr_reqcap & IFCAP_TXCSUM) != 0) {
				/* Turn on TX checksumming. */
				if_setcapenablebit(ifp, IFCAP_TXCSUM |
						   IFCAP_TXCSUM_IPV6, 0);
				if_sethwassistbits(ifp, CGEM_CKSUM_ASSIST, 0);

				WR4(sc, CGEM_DMA_CFG,
				    RD4(sc, CGEM_DMA_CFG) |
				     CGEM_DMA_CFG_CHKSUM_GEN_OFFLOAD_EN);
			} else {
				/* Turn off TX checksumming. */
				if_setcapenablebit(ifp, 0, IFCAP_TXCSUM |
						   IFCAP_TXCSUM_IPV6);
				if_sethwassistbits(ifp, 0, CGEM_CKSUM_ASSIST);

				WR4(sc, CGEM_DMA_CFG,
				    RD4(sc, CGEM_DMA_CFG) &
				     ~CGEM_DMA_CFG_CHKSUM_GEN_OFFLOAD_EN);
			}
		}
		if ((mask & IFCAP_RXCSUM) != 0) {
			if ((ifr->ifr_reqcap & IFCAP_RXCSUM) != 0) {
				/* Turn on RX checksumming. */
				if_setcapenablebit(ifp, IFCAP_RXCSUM |
						   IFCAP_RXCSUM_IPV6, 0);
				WR4(sc, CGEM_NET_CFG,
				    RD4(sc, CGEM_NET_CFG) |
				     CGEM_NET_CFG_RX_CHKSUM_OFFLD_EN);
			} else {
				/* Turn off RX checksumming. */
				if_setcapenablebit(ifp, 0, IFCAP_RXCSUM |
						   IFCAP_RXCSUM_IPV6);
				WR4(sc, CGEM_NET_CFG,
				    RD4(sc, CGEM_NET_CFG) &
				     ~CGEM_NET_CFG_RX_CHKSUM_OFFLD_EN);
			}
		}
		if ((if_getcapenable(ifp) & (IFCAP_RXCSUM | IFCAP_TXCSUM)) == 
		    (IFCAP_RXCSUM | IFCAP_TXCSUM))
			if_setcapenablebit(ifp, IFCAP_VLAN_HWCSUM, 0);
		else
			if_setcapenablebit(ifp, 0, IFCAP_VLAN_HWCSUM);

		CGEM_UNLOCK(sc);
		break;
	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}

	return (error);
}

/* MII bus support routines.
 */
static void
cgem_child_detached(device_t dev, device_t child)
{
	struct cgem_softc *sc = device_get_softc(dev);

	if (child == sc->miibus)
		sc->miibus = NULL;
}

static int
cgem_ifmedia_upd(if_t ifp)
{
	struct cgem_softc *sc = (struct cgem_softc *) if_getsoftc(ifp);
	struct mii_data *mii;
	struct mii_softc *miisc;
	int error = 0;

	mii = device_get_softc(sc->miibus);
	CGEM_LOCK(sc);
	if ((if_getflags(ifp) & IFF_UP) != 0) {
		LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
			PHY_RESET(miisc);
		error = mii_mediachg(mii);
	}
	CGEM_UNLOCK(sc);

	return (error);
}

static void
cgem_ifmedia_sts(if_t ifp, struct ifmediareq *ifmr)
{
	struct cgem_softc *sc = (struct cgem_softc *) if_getsoftc(ifp);
	struct mii_data *mii;

	mii = device_get_softc(sc->miibus);
	CGEM_LOCK(sc);
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
	CGEM_UNLOCK(sc);
}

static int
cgem_miibus_readreg(device_t dev, int phy, int reg)
{
	struct cgem_softc *sc = device_get_softc(dev);
	int tries, val;

	WR4(sc, CGEM_PHY_MAINT,
	    CGEM_PHY_MAINT_CLAUSE_22 | CGEM_PHY_MAINT_MUST_10 |
	    CGEM_PHY_MAINT_OP_READ |
	    (phy << CGEM_PHY_MAINT_PHY_ADDR_SHIFT) |
	    (reg << CGEM_PHY_MAINT_REG_ADDR_SHIFT));

	/* Wait for completion. */
	tries=0;
	while ((RD4(sc, CGEM_NET_STAT) & CGEM_NET_STAT_PHY_MGMT_IDLE) == 0) {
		DELAY(5);
		if (++tries > 200) {
			device_printf(dev, "phy read timeout: %d\n", reg);
			return (-1);
		}
	}

	val = RD4(sc, CGEM_PHY_MAINT) & CGEM_PHY_MAINT_DATA_MASK;

	if (reg == MII_EXTSR)
		/*
		 * MAC does not support half-duplex at gig speeds.
		 * Let mii(4) exclude the capability.
		 */
		val &= ~(EXTSR_1000XHDX | EXTSR_1000THDX);

	return (val);
}

static int
cgem_miibus_writereg(device_t dev, int phy, int reg, int data)
{
	struct cgem_softc *sc = device_get_softc(dev);
	int tries;
	
	WR4(sc, CGEM_PHY_MAINT,
	    CGEM_PHY_MAINT_CLAUSE_22 | CGEM_PHY_MAINT_MUST_10 |
	    CGEM_PHY_MAINT_OP_WRITE |
	    (phy << CGEM_PHY_MAINT_PHY_ADDR_SHIFT) |
	    (reg << CGEM_PHY_MAINT_REG_ADDR_SHIFT) |
	    (data & CGEM_PHY_MAINT_DATA_MASK));

	/* Wait for completion. */
	tries = 0;
	while ((RD4(sc, CGEM_NET_STAT) & CGEM_NET_STAT_PHY_MGMT_IDLE) == 0) {
		DELAY(5);
		if (++tries > 200) {
			device_printf(dev, "phy write timeout: %d\n", reg);
			return (-1);
		}
	}

	return (0);
}

static void
cgem_miibus_statchg(device_t dev)
{
	struct cgem_softc *sc  = device_get_softc(dev);
	struct mii_data *mii = device_get_softc(sc->miibus);

	CGEM_ASSERT_LOCKED(sc);

	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID) &&
	    sc->mii_media_active != mii->mii_media_active)
		cgem_mediachange(sc, mii);
}

static void
cgem_miibus_linkchg(device_t dev)
{
	struct cgem_softc *sc  = device_get_softc(dev);
	struct mii_data *mii = device_get_softc(sc->miibus);

	CGEM_ASSERT_LOCKED(sc);

	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID) &&
	    sc->mii_media_active != mii->mii_media_active)
		cgem_mediachange(sc, mii);
}

/*
 * Overridable weak symbol cgem_set_ref_clk().  This allows platforms to
 * provide a function to set the cgem's reference clock.
 */
static int __used
cgem_default_set_ref_clk(int unit, int frequency)
{

	return 0;
}
__weak_reference(cgem_default_set_ref_clk, cgem_set_ref_clk);

/* Call to set reference clock and network config bits according to media. */
static void
cgem_mediachange(struct cgem_softc *sc,	struct mii_data *mii)
{
	uint32_t net_cfg;
	int ref_clk_freq;

	CGEM_ASSERT_LOCKED(sc);

	/* Update hardware to reflect media. */
	net_cfg = RD4(sc, CGEM_NET_CFG);
	net_cfg &= ~(CGEM_NET_CFG_SPEED100 | CGEM_NET_CFG_GIGE_EN |
		     CGEM_NET_CFG_FULL_DUPLEX);

	switch (IFM_SUBTYPE(mii->mii_media_active)) {
	case IFM_1000_T:
		net_cfg |= (CGEM_NET_CFG_SPEED100 |
			    CGEM_NET_CFG_GIGE_EN);
		ref_clk_freq = 125000000;
		break;
	case IFM_100_TX:
		net_cfg |= CGEM_NET_CFG_SPEED100;
		ref_clk_freq = 25000000;
		break;
	default:
		ref_clk_freq = 2500000;
	}

	if ((mii->mii_media_active & IFM_FDX) != 0)
		net_cfg |= CGEM_NET_CFG_FULL_DUPLEX;

	WR4(sc, CGEM_NET_CFG, net_cfg);

	/* Set the reference clock if necessary. */
	if (cgem_set_ref_clk(sc->ref_clk_num, ref_clk_freq))
		device_printf(sc->dev, "cgem_mediachange: "
			      "could not set ref clk%d to %d.\n",
			      sc->ref_clk_num, ref_clk_freq);

	sc->mii_media_active = mii->mii_media_active;
}

static void
cgem_add_sysctls(device_t dev)
{
	struct cgem_softc *sc = device_get_softc(dev);
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid_list *child;
	struct sysctl_oid *tree;

	ctx = device_get_sysctl_ctx(dev);
	child = SYSCTL_CHILDREN(device_get_sysctl_tree(dev));

	SYSCTL_ADD_INT(ctx, child, OID_AUTO, "rxbufs", CTLFLAG_RW,
		       &sc->rxbufs, 0,
		       "Number receive buffers to provide");

	SYSCTL_ADD_INT(ctx, child, OID_AUTO, "rxhangwar", CTLFLAG_RW,
		       &sc->rxhangwar, 0,
		       "Enable receive hang work-around");

	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "_rxoverruns", CTLFLAG_RD,
			&sc->rxoverruns, 0,
			"Receive overrun events");

	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "_rxnobufs", CTLFLAG_RD,
			&sc->rxnobufs, 0,
			"Receive buf queue empty events");

	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "_rxdmamapfails", CTLFLAG_RD,
			&sc->rxdmamapfails, 0,
			"Receive DMA map failures");

	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "_txfull", CTLFLAG_RD,
			&sc->txfull, 0,
			"Transmit ring full events");

	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "_txdmamapfails", CTLFLAG_RD,
			&sc->txdmamapfails, 0,
			"Transmit DMA map failures");

	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "_txdefrags", CTLFLAG_RD,
			&sc->txdefrags, 0,
			"Transmit m_defrag() calls");

	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "_txdefragfails", CTLFLAG_RD,
			&sc->txdefragfails, 0,
			"Transmit m_defrag() failures");

	tree = SYSCTL_ADD_NODE(ctx, child, OID_AUTO, "stats", CTLFLAG_RD,
			       NULL, "GEM statistics");
	child = SYSCTL_CHILDREN(tree);

	SYSCTL_ADD_UQUAD(ctx, child, OID_AUTO, "tx_bytes", CTLFLAG_RD,
			 &sc->stats.tx_bytes, "Total bytes transmitted");

	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "tx_frames", CTLFLAG_RD,
			&sc->stats.tx_frames, 0, "Total frames transmitted");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "tx_frames_bcast", CTLFLAG_RD,
			&sc->stats.tx_frames_bcast, 0,
			"Number broadcast frames transmitted");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "tx_frames_multi", CTLFLAG_RD,
			&sc->stats.tx_frames_multi, 0,
			"Number multicast frames transmitted");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "tx_frames_pause",
			CTLFLAG_RD, &sc->stats.tx_frames_pause, 0,
			"Number pause frames transmitted");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "tx_frames_64b", CTLFLAG_RD,
			&sc->stats.tx_frames_64b, 0,
			"Number frames transmitted of size 64 bytes or less");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "tx_frames_65to127b", CTLFLAG_RD,
			&sc->stats.tx_frames_65to127b, 0,
			"Number frames transmitted of size 65-127 bytes");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "tx_frames_128to255b",
			CTLFLAG_RD, &sc->stats.tx_frames_128to255b, 0,
			"Number frames transmitted of size 128-255 bytes");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "tx_frames_256to511b",
			CTLFLAG_RD, &sc->stats.tx_frames_256to511b, 0,
			"Number frames transmitted of size 256-511 bytes");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "tx_frames_512to1023b",
			CTLFLAG_RD, &sc->stats.tx_frames_512to1023b, 0,
			"Number frames transmitted of size 512-1023 bytes");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "tx_frames_1024to1536b",
			CTLFLAG_RD, &sc->stats.tx_frames_1024to1536b, 0,
			"Number frames transmitted of size 1024-1536 bytes");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "tx_under_runs",
			CTLFLAG_RD, &sc->stats.tx_under_runs, 0,
			"Number transmit under-run events");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "tx_single_collisn",
			CTLFLAG_RD, &sc->stats.tx_single_collisn, 0,
			"Number single-collision transmit frames");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "tx_multi_collisn",
			CTLFLAG_RD, &sc->stats.tx_multi_collisn, 0,
			"Number multi-collision transmit frames");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "tx_excsv_collisn",
			CTLFLAG_RD, &sc->stats.tx_excsv_collisn, 0,
			"Number excessive collision transmit frames");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "tx_late_collisn",
			CTLFLAG_RD, &sc->stats.tx_late_collisn, 0,
			"Number late-collision transmit frames");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "tx_deferred_frames",
			CTLFLAG_RD, &sc->stats.tx_deferred_frames, 0,
			"Number deferred transmit frames");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "tx_carrier_sense_errs",
			CTLFLAG_RD, &sc->stats.tx_carrier_sense_errs, 0,
			"Number carrier sense errors on transmit");

	SYSCTL_ADD_UQUAD(ctx, child, OID_AUTO, "rx_bytes", CTLFLAG_RD,
			 &sc->stats.rx_bytes, "Total bytes received");

	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "rx_frames", CTLFLAG_RD,
			&sc->stats.rx_frames, 0, "Total frames received");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "rx_frames_bcast",
			CTLFLAG_RD, &sc->stats.rx_frames_bcast, 0,
			"Number broadcast frames received");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "rx_frames_multi",
			CTLFLAG_RD, &sc->stats.rx_frames_multi, 0,
			"Number multicast frames received");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "rx_frames_pause",
			CTLFLAG_RD, &sc->stats.rx_frames_pause, 0,
			"Number pause frames received");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "rx_frames_64b",
			CTLFLAG_RD, &sc->stats.rx_frames_64b, 0,
			"Number frames received of size 64 bytes or less");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "rx_frames_65to127b",
			CTLFLAG_RD, &sc->stats.rx_frames_65to127b, 0,
			"Number frames received of size 65-127 bytes");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "rx_frames_128to255b",
			CTLFLAG_RD, &sc->stats.rx_frames_128to255b, 0,
			"Number frames received of size 128-255 bytes");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "rx_frames_256to511b",
			CTLFLAG_RD, &sc->stats.rx_frames_256to511b, 0,
			"Number frames received of size 256-511 bytes");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "rx_frames_512to1023b",
			CTLFLAG_RD, &sc->stats.rx_frames_512to1023b, 0,
			"Number frames received of size 512-1023 bytes");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "rx_frames_1024to1536b",
			CTLFLAG_RD, &sc->stats.rx_frames_1024to1536b, 0,
			"Number frames received of size 1024-1536 bytes");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "rx_frames_undersize",
			CTLFLAG_RD, &sc->stats.rx_frames_undersize, 0,
			"Number undersize frames received");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "rx_frames_oversize",
			CTLFLAG_RD, &sc->stats.rx_frames_oversize, 0,
			"Number oversize frames received");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "rx_frames_jabber",
			CTLFLAG_RD, &sc->stats.rx_frames_jabber, 0,
			"Number jabber frames received");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "rx_frames_fcs_errs",
			CTLFLAG_RD, &sc->stats.rx_frames_fcs_errs, 0,
			"Number frames received with FCS errors");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "rx_frames_length_errs",
			CTLFLAG_RD, &sc->stats.rx_frames_length_errs, 0,
			"Number frames received with length errors");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "rx_symbol_errs",
			CTLFLAG_RD, &sc->stats.rx_symbol_errs, 0,
			"Number receive symbol errors");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "rx_align_errs",
			CTLFLAG_RD, &sc->stats.rx_align_errs, 0,
			"Number receive alignment errors");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "rx_resource_errs",
			CTLFLAG_RD, &sc->stats.rx_resource_errs, 0,
			"Number frames received when no rx buffer available");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "rx_overrun_errs",
			CTLFLAG_RD, &sc->stats.rx_overrun_errs, 0,
			"Number frames received but not copied due to "
			"receive overrun");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "rx_frames_ip_hdr_csum_errs",
			CTLFLAG_RD, &sc->stats.rx_ip_hdr_csum_errs, 0,
			"Number frames received with IP header checksum "
			"errors");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "rx_frames_tcp_csum_errs",
			CTLFLAG_RD, &sc->stats.rx_tcp_csum_errs, 0,
			"Number frames received with TCP checksum errors");
	SYSCTL_ADD_UINT(ctx, child, OID_AUTO, "rx_frames_udp_csum_errs",
			CTLFLAG_RD, &sc->stats.rx_udp_csum_errs, 0,
			"Number frames received with UDP checksum errors");
}


static int
cgem_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "cadence,gem"))
		return (ENXIO);

	device_set_desc(dev, "Cadence CGEM Gigabit Ethernet Interface");
	return (0);
}

static int
cgem_attach(device_t dev)
{
	struct cgem_softc *sc = device_get_softc(dev);
	if_t ifp = NULL;
	phandle_t node;
	pcell_t cell;
	int rid, err;
	u_char eaddr[ETHER_ADDR_LEN];

	sc->dev = dev;
	CGEM_LOCK_INIT(sc);

	/* Get reference clock number and base divider from fdt. */
	node = ofw_bus_get_node(dev);
	sc->ref_clk_num = 0;
	if (OF_getprop(node, "ref-clock-num", &cell, sizeof(cell)) > 0)
		sc->ref_clk_num = fdt32_to_cpu(cell);

	/* Get memory resource. */
	rid = 0;
	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
					     RF_ACTIVE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "could not allocate memory resources.\n");
		return (ENOMEM);
	}

	/* Get IRQ resource. */
	rid = 0;
	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
					     RF_ACTIVE);
	if (sc->irq_res == NULL) {
		device_printf(dev, "could not allocate interrupt resource.\n");
		cgem_detach(dev);
		return (ENOMEM);
	}

	/* Set up ifnet structure. */
	ifp = sc->ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "could not allocate ifnet structure\n");
		cgem_detach(dev);
		return (ENOMEM);
	}
	if_setsoftc(ifp, sc);
	if_initname(ifp, IF_CGEM_NAME, device_get_unit(dev));
	if_setflags(ifp, IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST);
	if_setinitfn(ifp, cgem_init);
	if_setioctlfn(ifp, cgem_ioctl);
	if_setstartfn(ifp, cgem_start);
	if_setcapabilitiesbit(ifp, IFCAP_HWCSUM | IFCAP_HWCSUM_IPV6 |
			      IFCAP_VLAN_MTU | IFCAP_VLAN_HWCSUM, 0);
	if_setsendqlen(ifp, CGEM_NUM_TX_DESCS);
	if_setsendqready(ifp);

	/* Disable hardware checksumming by default. */
	if_sethwassist(ifp, 0);
	if_setcapenable(ifp, if_getcapabilities(ifp) &
		~(IFCAP_HWCSUM | IFCAP_HWCSUM_IPV6 | IFCAP_VLAN_HWCSUM));

	sc->if_old_flags = if_getflags(ifp);
	sc->rxbufs = DEFAULT_NUM_RX_BUFS;
	sc->rxhangwar = 1;

	/* Reset hardware. */
	CGEM_LOCK(sc);
	cgem_reset(sc);
	CGEM_UNLOCK(sc);

	/* Attach phy to mii bus. */
	err = mii_attach(dev, &sc->miibus, ifp,
			 cgem_ifmedia_upd, cgem_ifmedia_sts,
			 BMSR_DEFCAPMASK, MII_PHY_ANY, MII_OFFSET_ANY, 0);
	if (err) {
		device_printf(dev, "attaching PHYs failed\n");
		cgem_detach(dev);
		return (err);
	}

	/* Set up TX and RX descriptor area. */
	err = cgem_setup_descs(sc);
	if (err) {
		device_printf(dev, "could not set up dma mem for descs.\n");
		cgem_detach(dev);
		return (ENOMEM);
	}

	/* Get a MAC address. */
	cgem_get_mac(sc, eaddr);

	/* Start ticks. */
	callout_init_mtx(&sc->tick_ch, &sc->sc_mtx, 0);

	ether_ifattach(ifp, eaddr);

	err = bus_setup_intr(dev, sc->irq_res, INTR_TYPE_NET | INTR_MPSAFE |
			     INTR_EXCL, NULL, cgem_intr, sc, &sc->intrhand);
	if (err) {
		device_printf(dev, "could not set interrupt handler.\n");
		ether_ifdetach(ifp);
		cgem_detach(dev);
		return (err);
	}

	cgem_add_sysctls(dev);

	return (0);
}

static int
cgem_detach(device_t dev)
{
	struct cgem_softc *sc = device_get_softc(dev);
	int i;

	if (sc == NULL)
		return (ENODEV);

	if (device_is_attached(dev)) {
		CGEM_LOCK(sc);
		cgem_stop(sc);
		CGEM_UNLOCK(sc);
		callout_drain(&sc->tick_ch);
		if_setflagbits(sc->ifp, 0, IFF_UP);
		ether_ifdetach(sc->ifp);
	}

	if (sc->miibus != NULL) {
		device_delete_child(dev, sc->miibus);
		sc->miibus = NULL;
	}

	/* Release resources. */
	if (sc->mem_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY,
				     rman_get_rid(sc->mem_res), sc->mem_res);
		sc->mem_res = NULL;
	}
	if (sc->irq_res != NULL) {
		if (sc->intrhand)
			bus_teardown_intr(dev, sc->irq_res, sc->intrhand);
		bus_release_resource(dev, SYS_RES_IRQ,
				     rman_get_rid(sc->irq_res), sc->irq_res);
		sc->irq_res = NULL;
	}

	/* Release DMA resources. */
	if (sc->rxring != NULL) {
		if (sc->rxring_physaddr != 0) {
			bus_dmamap_unload(sc->desc_dma_tag,
					  sc->rxring_dma_map);
			sc->rxring_physaddr = 0;
		}
		bus_dmamem_free(sc->desc_dma_tag, sc->rxring,
				sc->rxring_dma_map);
		sc->rxring = NULL;
		for (i = 0; i < CGEM_NUM_RX_DESCS; i++)
			if (sc->rxring_m_dmamap[i] != NULL) {
				bus_dmamap_destroy(sc->mbuf_dma_tag,
						   sc->rxring_m_dmamap[i]);
				sc->rxring_m_dmamap[i] = NULL;
			}
	}
	if (sc->txring != NULL) {
		if (sc->txring_physaddr != 0) {
			bus_dmamap_unload(sc->desc_dma_tag,
					  sc->txring_dma_map);
			sc->txring_physaddr = 0;
		}
		bus_dmamem_free(sc->desc_dma_tag, sc->txring,
				sc->txring_dma_map);
		sc->txring = NULL;
		for (i = 0; i < CGEM_NUM_TX_DESCS; i++)
			if (sc->txring_m_dmamap[i] != NULL) {
				bus_dmamap_destroy(sc->mbuf_dma_tag,
						   sc->txring_m_dmamap[i]);
				sc->txring_m_dmamap[i] = NULL;
			}
	}
	if (sc->desc_dma_tag != NULL) {
		bus_dma_tag_destroy(sc->desc_dma_tag);
		sc->desc_dma_tag = NULL;
	}
	if (sc->mbuf_dma_tag != NULL) {
		bus_dma_tag_destroy(sc->mbuf_dma_tag);
		sc->mbuf_dma_tag = NULL;
	}

	bus_generic_detach(dev);

	CGEM_LOCK_DESTROY(sc);

	return (0);
}

static device_method_t cgem_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		cgem_probe),
	DEVMETHOD(device_attach,	cgem_attach),
	DEVMETHOD(device_detach,	cgem_detach),

	/* Bus interface */
	DEVMETHOD(bus_child_detached,	cgem_child_detached),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	cgem_miibus_readreg),
	DEVMETHOD(miibus_writereg,	cgem_miibus_writereg),
	DEVMETHOD(miibus_statchg,	cgem_miibus_statchg),
	DEVMETHOD(miibus_linkchg,	cgem_miibus_linkchg),

	DEVMETHOD_END
};

static driver_t cgem_driver = {
	"cgem",
	cgem_methods,
	sizeof(struct cgem_softc),
};

DRIVER_MODULE(cgem, simplebus, cgem_driver, cgem_devclass, NULL, NULL);
DRIVER_MODULE(miibus, cgem, miibus_driver, miibus_devclass, NULL, NULL);
MODULE_DEPEND(cgem, miibus, 1, 1, 1);
MODULE_DEPEND(cgem, ether, 1, 1, 1);
