/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012, 2013 Bjoern A. Zeeb
 * Copyright (c) 2014 Robert N. M. Watson
 * Copyright (c) 2016-2017 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-11-C-0249)
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
 */
/*
 * Altera Triple-Speed Ethernet MegaCore, Function User Guide
 * UG-01008-3.0, Software Version: 12.0, June 2012.
 * Available at the time of writing at:
 * http://www.altera.com/literature/ug/ug_ethernet.pdf
 *
 * We are using an Marvell E1111 (Alaska) PHY on the DE4.  See mii/e1000phy.c.
 */
/*
 * XXX-BZ NOTES:
 * - ifOutBroadcastPkts are only counted if both ether dst and src are all-1s;
 *   seems an IP core bug, they count ether broadcasts as multicast.  Is this
 *   still the case?
 * - figure out why the TX FIFO fill status and intr did not work as expected.
 * - test 100Mbit/s and 10Mbit/s
 * - blacklist the one special factory programmed ethernet address (for now
 *   hardcoded, later from loader?)
 * - resolve all XXX, left as reminders to shake out details later
 * - Jumbo frame support
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_device_polling.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/jail.h>
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

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/altera/atse/if_atsereg.h>
#include <dev/xdma/xdma.h>

#define	RX_QUEUE_SIZE		4096
#define	TX_QUEUE_SIZE		4096
#define	NUM_RX_MBUF		512
#define	BUFRING_SIZE		8192

#include <machine/cache.h>

/* XXX once we'd do parallel attach, we need a global lock for this. */
#define	ATSE_ETHERNET_OPTION_BITS_UNDEF	0
#define	ATSE_ETHERNET_OPTION_BITS_READ	1
static int atse_ethernet_option_bits_flag = ATSE_ETHERNET_OPTION_BITS_UNDEF;
static uint8_t atse_ethernet_option_bits[ALTERA_ETHERNET_OPTION_BITS_LEN];

/*
 * Softc and critical resource locking.
 */
#define	ATSE_LOCK(_sc)		mtx_lock(&(_sc)->atse_mtx)
#define	ATSE_UNLOCK(_sc)	mtx_unlock(&(_sc)->atse_mtx)
#define	ATSE_LOCK_ASSERT(_sc)	mtx_assert(&(_sc)->atse_mtx, MA_OWNED)

#define ATSE_DEBUG
#undef ATSE_DEBUG

#ifdef ATSE_DEBUG
#define	DPRINTF(format, ...)	printf(format, __VA_ARGS__)
#else
#define	DPRINTF(format, ...)
#endif

/*
 * Register space access macros.
 */
static inline void
csr_write_4(struct atse_softc *sc, uint32_t reg, uint32_t val4,
    const char *f, const int l)
{

	val4 = htole32(val4);
	DPRINTF("[%s:%d] CSR W %s 0x%08x (0x%08x) = 0x%08x\n", f, l,
	    "atse_mem_res", reg, reg * 4, val4);
	bus_write_4(sc->atse_mem_res, reg * 4, val4);
}

static inline uint32_t
csr_read_4(struct atse_softc *sc, uint32_t reg, const char *f, const int l)
{
	uint32_t val4;

	val4 = le32toh(bus_read_4(sc->atse_mem_res, reg * 4));
	DPRINTF("[%s:%d] CSR R %s 0x%08x (0x%08x) = 0x%08x\n", f, l, 
	    "atse_mem_res", reg, reg * 4, val4);

	return (val4);
}

/*
 * See page 5-2 that it's all dword offsets and the MS 16 bits need to be zero
 * on write and ignored on read.
 */
static inline void
pxx_write_2(struct atse_softc *sc, bus_addr_t bmcr, uint32_t reg, uint16_t val,
    const char *f, const int l, const char *s)
{
	uint32_t val4;

	val4 = htole32(val & 0x0000ffff);
	DPRINTF("[%s:%d] %s W %s 0x%08x (0x%08jx) = 0x%08x\n", f, l, s,
	    "atse_mem_res", reg, (bmcr + reg) * 4, val4);
	bus_write_4(sc->atse_mem_res, (bmcr + reg) * 4, val4);
}

static inline uint16_t
pxx_read_2(struct atse_softc *sc, bus_addr_t bmcr, uint32_t reg, const char *f,
    const int l, const char *s)
{
	uint32_t val4;
	uint16_t val;

	val4 = bus_read_4(sc->atse_mem_res, (bmcr + reg) * 4);
	val = le32toh(val4) & 0x0000ffff;
	DPRINTF("[%s:%d] %s R %s 0x%08x (0x%08jx) = 0x%04x\n", f, l, s,
	    "atse_mem_res", reg, (bmcr + reg) * 4, val);

	return (val);
}

#define	CSR_WRITE_4(sc, reg, val)	\
	csr_write_4((sc), (reg), (val), __func__, __LINE__)
#define	CSR_READ_4(sc, reg)		\
	csr_read_4((sc), (reg), __func__, __LINE__)
#define	PCS_WRITE_2(sc, reg, val)	\
	pxx_write_2((sc), sc->atse_bmcr0, (reg), (val), __func__, __LINE__, \
	    "PCS")
#define	PCS_READ_2(sc, reg)		\
	pxx_read_2((sc), sc->atse_bmcr0, (reg), __func__, __LINE__, "PCS")
#define	PHY_WRITE_2(sc, reg, val)	\
	pxx_write_2((sc), sc->atse_bmcr1, (reg), (val), __func__, __LINE__, \
	    "PHY")
#define	PHY_READ_2(sc, reg)		\
	pxx_read_2((sc), sc->atse_bmcr1, (reg), __func__, __LINE__, "PHY")

static void atse_tick(void *);
static int atse_detach(device_t);

devclass_t atse_devclass;

static int
atse_rx_enqueue(struct atse_softc *sc, uint32_t n)
{
	struct mbuf *m;
	int i;

	for (i = 0; i < n; i++) {
		m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
		if (m == NULL) {
			device_printf(sc->dev,
			    "%s: Can't alloc rx mbuf\n", __func__);
			return (-1);
		}

		m->m_pkthdr.len = m->m_len = m->m_ext.ext_size;
		xdma_enqueue_mbuf(sc->xchan_rx, &m, 0, 4, 4, XDMA_DEV_TO_MEM);
	}

	return (0);
}

static int
atse_xdma_tx_intr(void *arg, xdma_transfer_status_t *status)
{
	xdma_transfer_status_t st;
	struct atse_softc *sc;
	struct ifnet *ifp;
	struct mbuf *m;
	int err;

	sc = arg;

	ATSE_LOCK(sc);

	ifp = sc->atse_ifp;

	for (;;) {
		err = xdma_dequeue_mbuf(sc->xchan_tx, &m, &st);
		if (err != 0) {
			break;
		}

		if (st.error != 0) {
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		}

		m_freem(m);
		sc->txcount--;
	}

	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	ATSE_UNLOCK(sc);

	return (0);
}

static int
atse_xdma_rx_intr(void *arg, xdma_transfer_status_t *status)
{
	xdma_transfer_status_t st;
	struct atse_softc *sc;
	struct ifnet *ifp;
	struct mbuf *m;
	int err;
	uint32_t cnt_processed;

	sc = arg;

	ATSE_LOCK(sc);

	ifp = sc->atse_ifp;

	cnt_processed = 0;
	for (;;) {
		err = xdma_dequeue_mbuf(sc->xchan_rx, &m, &st);
		if (err != 0) {
			break;
		}
		cnt_processed++;

		if (st.error != 0) {
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			m_freem(m);
			continue;
		}

		m->m_pkthdr.len = m->m_len = st.transferred;
		m->m_pkthdr.rcvif = ifp;
		m_adj(m, ETHER_ALIGN);
		ATSE_UNLOCK(sc);
		(*ifp->if_input)(ifp, m);
		ATSE_LOCK(sc);
	}

	atse_rx_enqueue(sc, cnt_processed);

	ATSE_UNLOCK(sc);

	return (0);
}

static int
atse_transmit_locked(struct ifnet *ifp)
{
	struct atse_softc *sc;
	struct mbuf *m;
	struct buf_ring *br;
	int error;
	int enq;

	sc = ifp->if_softc;
	br = sc->br;

	enq = 0;

	while ((m = drbr_peek(ifp, br)) != NULL) {
		error = xdma_enqueue_mbuf(sc->xchan_tx, &m, 0, 4, 4, XDMA_MEM_TO_DEV);
		if (error != 0) {
			/* No space in request queue available yet. */
			drbr_putback(ifp, br, m);
			break;
		}

		drbr_advance(ifp, br);

		sc->txcount++;
		enq++;

		/* If anyone is interested give them a copy. */
		ETHER_BPF_MTAP(ifp, m);
        }

	if (enq > 0)
		xdma_queue_submit(sc->xchan_tx);

	return (0);
}

static int
atse_transmit(struct ifnet *ifp, struct mbuf *m)
{
	struct atse_softc *sc;
	struct buf_ring *br;
	int error;

	sc = ifp->if_softc;
	br = sc->br;

	ATSE_LOCK(sc);

	mtx_lock(&sc->br_mtx);

	if ((ifp->if_drv_flags & (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) != IFF_DRV_RUNNING) {
		error = drbr_enqueue(ifp, sc->br, m);
		mtx_unlock(&sc->br_mtx);
		ATSE_UNLOCK(sc);
		return (error);
	}

	if ((sc->atse_flags & ATSE_FLAGS_LINK) == 0) {
		error = drbr_enqueue(ifp, sc->br, m);
		mtx_unlock(&sc->br_mtx);
		ATSE_UNLOCK(sc);
		return (error);
	}

	error = drbr_enqueue(ifp, br, m);
	if (error) {
		mtx_unlock(&sc->br_mtx);
		ATSE_UNLOCK(sc);
		return (error);
	}
	error = atse_transmit_locked(ifp);

	mtx_unlock(&sc->br_mtx);
	ATSE_UNLOCK(sc);

	return (error);
}

static void
atse_qflush(struct ifnet *ifp)
{
	struct atse_softc *sc;

	sc = ifp->if_softc;

	printf("%s\n", __func__);
}

static int
atse_stop_locked(struct atse_softc *sc)
{
	uint32_t mask, val4;
	struct ifnet *ifp;
	int i;

	ATSE_LOCK_ASSERT(sc);

	callout_stop(&sc->atse_tick);

	ifp = sc->atse_ifp;
	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);

	/* Disable MAC transmit and receive datapath. */
	mask = BASE_CFG_COMMAND_CONFIG_TX_ENA|BASE_CFG_COMMAND_CONFIG_RX_ENA;
	val4 = CSR_READ_4(sc, BASE_CFG_COMMAND_CONFIG);
	val4 &= ~mask;
	CSR_WRITE_4(sc, BASE_CFG_COMMAND_CONFIG, val4);

	/* Wait for bits to be cleared; i=100 is excessive. */
	for (i = 0; i < 100; i++) {
		val4 = CSR_READ_4(sc, BASE_CFG_COMMAND_CONFIG);
		if ((val4 & mask) == 0) {
			break;
		}
		DELAY(10);
	}

	if ((val4 & mask) != 0) {
		device_printf(sc->atse_dev, "Disabling MAC TX/RX timed out.\n");
		/* Punt. */
	}

	sc->atse_flags &= ~ATSE_FLAGS_LINK;

	return (0);
}

static uint8_t
atse_mchash(struct atse_softc *sc __unused, const uint8_t *addr)
{
	uint8_t x, y;
	int i, j;

	x = 0;
	for (i = 0; i < ETHER_ADDR_LEN; i++) {
		y = addr[i] & 0x01;
		for (j = 1; j < 8; j++)
			y ^= (addr[i] >> j) & 0x01;
		x |= (y << i);
	}

	return (x);
}

static int
atse_rxfilter_locked(struct atse_softc *sc)
{
	struct ifmultiaddr *ifma;
	struct ifnet *ifp;
	uint32_t val4;
	int i;

	/* XXX-BZ can we find out if we have the MHASH synthesized? */
	val4 = CSR_READ_4(sc, BASE_CFG_COMMAND_CONFIG);
	/* For simplicity always hash full 48 bits of addresses. */
	if ((val4 & BASE_CFG_COMMAND_CONFIG_MHASH_SEL) != 0)
		val4 &= ~BASE_CFG_COMMAND_CONFIG_MHASH_SEL;

	ifp = sc->atse_ifp;
	if (ifp->if_flags & IFF_PROMISC) {
		val4 |= BASE_CFG_COMMAND_CONFIG_PROMIS_EN;
	} else {
		val4 &= ~BASE_CFG_COMMAND_CONFIG_PROMIS_EN;
	}

	CSR_WRITE_4(sc, BASE_CFG_COMMAND_CONFIG, val4);

	if (ifp->if_flags & IFF_ALLMULTI) {
		/* Accept all multicast addresses. */
		for (i = 0; i <= MHASH_LEN; i++)
			CSR_WRITE_4(sc, MHASH_START + i, 0x1);
	} else {
		/*
		 * Can hold MHASH_LEN entries.
		 * XXX-BZ bitstring.h would be more general.
		 */
		uint64_t h;

		h = 0;
		/*
		 * Re-build and re-program hash table.  First build the
		 * bit-field "yes" or "no" for each slot per address, then
		 * do all the programming afterwards.
		 */
		if_maddr_rlock(ifp);
		CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
			if (ifma->ifma_addr->sa_family != AF_LINK) {
				continue;
			}

			h |= (1 << atse_mchash(sc,
			    LLADDR((struct sockaddr_dl *)ifma->ifma_addr)));
		}
		if_maddr_runlock(ifp);
		for (i = 0; i <= MHASH_LEN; i++) {
			CSR_WRITE_4(sc, MHASH_START + i,
			    (h & (1 << i)) ? 0x01 : 0x00);
		}
	}

	return (0);
}

static int
atse_ethernet_option_bits_read_fdt(device_t dev)
{
	struct resource *res;
	device_t fdev;
	int i, rid;

	if (atse_ethernet_option_bits_flag & ATSE_ETHERNET_OPTION_BITS_READ) {
		return (0);
	}

	fdev = device_find_child(device_get_parent(dev), "cfi", 0);
	if (fdev == NULL) {
		return (ENOENT);
	}

	rid = 0;
	res = bus_alloc_resource_any(fdev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE | RF_SHAREABLE);
	if (res == NULL) {
		return (ENXIO);
	}

	for (i = 0; i < ALTERA_ETHERNET_OPTION_BITS_LEN; i++) {
		atse_ethernet_option_bits[i] = bus_read_1(res,
		    ALTERA_ETHERNET_OPTION_BITS_OFF + i);
	}

	bus_release_resource(fdev, SYS_RES_MEMORY, rid, res);
	atse_ethernet_option_bits_flag |= ATSE_ETHERNET_OPTION_BITS_READ;

	return (0);
}

static int
atse_ethernet_option_bits_read(device_t dev)
{
	int error;

	error = atse_ethernet_option_bits_read_fdt(dev);
	if (error == 0)
		return (0);

	device_printf(dev, "Cannot read Ethernet addresses from flash.\n");

	return (error);
}

static int
atse_get_eth_address(struct atse_softc *sc)
{
	unsigned long hostid;
	uint32_t val4;
	int unit;

	/*
	 * Make sure to only ever do this once.  Otherwise a reset would
	 * possibly change our ethernet address, which is not good at all.
	 */
	if (sc->atse_eth_addr[0] != 0x00 || sc->atse_eth_addr[1] != 0x00 ||
	    sc->atse_eth_addr[2] != 0x00) {
		return (0);
	}

	if ((atse_ethernet_option_bits_flag &
	    ATSE_ETHERNET_OPTION_BITS_READ) == 0) {
		goto get_random;
	}

	val4 = atse_ethernet_option_bits[0] << 24;
	val4 |= atse_ethernet_option_bits[1] << 16;
	val4 |= atse_ethernet_option_bits[2] << 8;
	val4 |= atse_ethernet_option_bits[3];
	/* They chose "safe". */
	if (val4 != le32toh(0x00005afe)) {
		device_printf(sc->atse_dev, "Magic '5afe' is not safe: 0x%08x. "
		    "Falling back to random numbers for hardware address.\n",
		     val4);
		goto get_random;
	}

	sc->atse_eth_addr[0] = atse_ethernet_option_bits[4];
	sc->atse_eth_addr[1] = atse_ethernet_option_bits[5];
	sc->atse_eth_addr[2] = atse_ethernet_option_bits[6];
	sc->atse_eth_addr[3] = atse_ethernet_option_bits[7];
	sc->atse_eth_addr[4] = atse_ethernet_option_bits[8];
	sc->atse_eth_addr[5] = atse_ethernet_option_bits[9];

	/* Handle factory default ethernet addresss: 00:07:ed:ff:ed:15 */
	if (sc->atse_eth_addr[0] == 0x00 && sc->atse_eth_addr[1] == 0x07 &&
	    sc->atse_eth_addr[2] == 0xed && sc->atse_eth_addr[3] == 0xff &&
	    sc->atse_eth_addr[4] == 0xed && sc->atse_eth_addr[5] == 0x15) {

		device_printf(sc->atse_dev, "Factory programmed Ethernet "
		    "hardware address blacklisted.  Falling back to random "
		    "address to avoid collisions.\n");
		device_printf(sc->atse_dev, "Please re-program your flash.\n");
		goto get_random;
	}

	if (sc->atse_eth_addr[0] == 0x00 && sc->atse_eth_addr[1] == 0x00 &&
	    sc->atse_eth_addr[2] == 0x00 && sc->atse_eth_addr[3] == 0x00 &&
	    sc->atse_eth_addr[4] == 0x00 && sc->atse_eth_addr[5] == 0x00) {
		device_printf(sc->atse_dev, "All zero's Ethernet hardware "
		    "address blacklisted.  Falling back to random address.\n");
		device_printf(sc->atse_dev, "Please re-program your flash.\n");
		goto get_random;
	}

	if (ETHER_IS_MULTICAST(sc->atse_eth_addr)) {
		device_printf(sc->atse_dev, "Multicast Ethernet hardware "
		    "address blacklisted.  Falling back to random address.\n");
		device_printf(sc->atse_dev, "Please re-program your flash.\n");
		goto get_random;
	}

	/*
	 * If we find an Altera prefixed address with a 0x0 ending
	 * adjust by device unit.  If not and this is not the first
	 * Ethernet, go to random.
	 */
	unit = device_get_unit(sc->atse_dev);
	if (unit == 0x00) {
		return (0);
	}

	if (unit > 0x0f) {
		device_printf(sc->atse_dev, "We do not support Ethernet "
		    "addresses for more than 16 MACs. Falling back to "
		    "random hadware address.\n");
		goto get_random;
	}
	if ((sc->atse_eth_addr[0] & ~0x2) != 0 ||
	    sc->atse_eth_addr[1] != 0x07 || sc->atse_eth_addr[2] != 0xed ||
	    (sc->atse_eth_addr[5] & 0x0f) != 0x0) {
		device_printf(sc->atse_dev, "Ethernet address not meeting our "
		    "multi-MAC standards. Falling back to random hadware "
		    "address.\n");
		goto get_random;
	}
	sc->atse_eth_addr[5] |= (unit & 0x0f);

	return (0);

get_random:
	/*
	 * Fall back to random code we also use on bridge(4).
	 */
	getcredhostid(curthread->td_ucred, &hostid);
	if (hostid == 0) {
		arc4rand(sc->atse_eth_addr, ETHER_ADDR_LEN, 1);
		sc->atse_eth_addr[0] &= ~1;/* clear multicast bit */
		sc->atse_eth_addr[0] |= 2; /* set the LAA bit */
	} else {
		sc->atse_eth_addr[0] = 0x2;
		sc->atse_eth_addr[1] = (hostid >> 24)	& 0xff;
		sc->atse_eth_addr[2] = (hostid >> 16)	& 0xff;
		sc->atse_eth_addr[3] = (hostid >> 8 )	& 0xff;
		sc->atse_eth_addr[4] = hostid		& 0xff;
		sc->atse_eth_addr[5] = sc->atse_unit	& 0xff;
	}

	return (0);
}

static int
atse_set_eth_address(struct atse_softc *sc, int n)
{
	uint32_t v0, v1;

	v0 = (sc->atse_eth_addr[3] << 24) | (sc->atse_eth_addr[2] << 16) |
	    (sc->atse_eth_addr[1] << 8) | sc->atse_eth_addr[0];
	v1 = (sc->atse_eth_addr[5] << 8) | sc->atse_eth_addr[4];

	if (n & ATSE_ETH_ADDR_DEF) {
		CSR_WRITE_4(sc, BASE_CFG_MAC_0, v0);
		CSR_WRITE_4(sc, BASE_CFG_MAC_1, v1);
	}
	if (n & ATSE_ETH_ADDR_SUPP1) {
		CSR_WRITE_4(sc, SUPPL_ADDR_SMAC_0_0, v0);
		CSR_WRITE_4(sc, SUPPL_ADDR_SMAC_0_1, v1);
	}
	if (n & ATSE_ETH_ADDR_SUPP2) {
		CSR_WRITE_4(sc, SUPPL_ADDR_SMAC_1_0, v0);
		CSR_WRITE_4(sc, SUPPL_ADDR_SMAC_1_1, v1);
	}
	if (n & ATSE_ETH_ADDR_SUPP3) {
		CSR_WRITE_4(sc, SUPPL_ADDR_SMAC_2_0, v0);
		CSR_WRITE_4(sc, SUPPL_ADDR_SMAC_2_1, v1);
	}
	if (n & ATSE_ETH_ADDR_SUPP4) {
		CSR_WRITE_4(sc, SUPPL_ADDR_SMAC_3_0, v0);
		CSR_WRITE_4(sc, SUPPL_ADDR_SMAC_3_1, v1);
	}

	return (0);
}

static int
atse_reset(struct atse_softc *sc)
{
	uint32_t val4, mask;
	uint16_t val;
	int i;

	/* 1. External PHY Initialization using MDIO. */
	/*
	 * We select the right MDIO space in atse_attach() and let MII do
	 * anything else.
	 */

	/* 2. PCS Configuration Register Initialization. */
	/* a. Set auto negotiation link timer to 1.6ms for SGMII. */
	PCS_WRITE_2(sc, PCS_EXT_LINK_TIMER_0, 0x0D40);
	PCS_WRITE_2(sc, PCS_EXT_LINK_TIMER_1, 0x0003);

	/* b. Configure SGMII. */
	val = PCS_EXT_IF_MODE_SGMII_ENA|PCS_EXT_IF_MODE_USE_SGMII_AN;
	PCS_WRITE_2(sc, PCS_EXT_IF_MODE, val);

	/* c. Enable auto negotiation. */
	/* Ignore Bits 6,8,13; should be set,set,unset. */
	val = PCS_READ_2(sc, PCS_CONTROL);
	val &= ~(PCS_CONTROL_ISOLATE|PCS_CONTROL_POWERDOWN);
	val &= ~PCS_CONTROL_LOOPBACK;		/* Make this a -link1 option? */
	val |= PCS_CONTROL_AUTO_NEGOTIATION_ENABLE;
	PCS_WRITE_2(sc, PCS_CONTROL, val);

	/* d. PCS reset. */
	val = PCS_READ_2(sc, PCS_CONTROL);
	val |= PCS_CONTROL_RESET;
	PCS_WRITE_2(sc, PCS_CONTROL, val);

	/* Wait for reset bit to clear; i=100 is excessive. */
	for (i = 0; i < 100; i++) {
		val = PCS_READ_2(sc, PCS_CONTROL);
		if ((val & PCS_CONTROL_RESET) == 0) {
			break;
		}
		DELAY(10);
	}

	if ((val & PCS_CONTROL_RESET) != 0) {
		device_printf(sc->atse_dev, "PCS reset timed out.\n");
		return (ENXIO);
	}

	/* 3. MAC Configuration Register Initialization. */
	/* a. Disable MAC transmit and receive datapath. */
	mask = BASE_CFG_COMMAND_CONFIG_TX_ENA|BASE_CFG_COMMAND_CONFIG_RX_ENA;
	val4 = CSR_READ_4(sc, BASE_CFG_COMMAND_CONFIG);
	val4 &= ~mask;
	/* Samples in the manual do have the SW_RESET bit set here, why? */
	CSR_WRITE_4(sc, BASE_CFG_COMMAND_CONFIG, val4);
	/* Wait for bits to be cleared; i=100 is excessive. */
	for (i = 0; i < 100; i++) {
		val4 = CSR_READ_4(sc, BASE_CFG_COMMAND_CONFIG);
		if ((val4 & mask) == 0) {
			break;
		}
		DELAY(10);
	}
	if ((val4 & mask) != 0) {
		device_printf(sc->atse_dev, "Disabling MAC TX/RX timed out.\n");
		return (ENXIO);
	}
	/* b. MAC FIFO configuration. */
	CSR_WRITE_4(sc, BASE_CFG_TX_SECTION_EMPTY, FIFO_DEPTH_TX - 16);
	CSR_WRITE_4(sc, BASE_CFG_TX_ALMOST_FULL, 3);
	CSR_WRITE_4(sc, BASE_CFG_TX_ALMOST_EMPTY, 8);
	CSR_WRITE_4(sc, BASE_CFG_RX_SECTION_EMPTY, FIFO_DEPTH_RX - 16);
	CSR_WRITE_4(sc, BASE_CFG_RX_ALMOST_FULL, 8);
	CSR_WRITE_4(sc, BASE_CFG_RX_ALMOST_EMPTY, 8);
#if 0
	CSR_WRITE_4(sc, BASE_CFG_TX_SECTION_FULL, 16);
	CSR_WRITE_4(sc, BASE_CFG_RX_SECTION_FULL, 16);
#else
	/* For store-and-forward mode, set this threshold to 0. */
	CSR_WRITE_4(sc, BASE_CFG_TX_SECTION_FULL, 0);
	CSR_WRITE_4(sc, BASE_CFG_RX_SECTION_FULL, 0);
#endif
	/* c. MAC address configuration. */
	/* Also intialize supplementary addresses to our primary one. */
	/* XXX-BZ FreeBSD really needs to grow and API for using these. */
	atse_get_eth_address(sc);
	atse_set_eth_address(sc, ATSE_ETH_ADDR_ALL);

	/* d. MAC function configuration. */
	CSR_WRITE_4(sc, BASE_CFG_FRM_LENGTH, 1518);	/* Default. */
	CSR_WRITE_4(sc, BASE_CFG_TX_IPG_LENGTH, 12);
	CSR_WRITE_4(sc, BASE_CFG_PAUSE_QUANT, 0xFFFF);

	val4 = CSR_READ_4(sc, BASE_CFG_COMMAND_CONFIG);
	/*
	 * If 1000BASE-X/SGMII PCS is initialized, set the ETH_SPEED (bit 3)
	 * and ENA_10 (bit 25) in command_config register to 0.  If half duplex
	 * is reported in the PHY/PCS status register, set the HD_ENA (bit 10)
	 * to 1 in command_config register.
	 * BZ: We shoot for 1000 instead.
	 */
#if 0
	val4 |= BASE_CFG_COMMAND_CONFIG_ETH_SPEED;
#else
	val4 &= ~BASE_CFG_COMMAND_CONFIG_ETH_SPEED;
#endif
	val4 &= ~BASE_CFG_COMMAND_CONFIG_ENA_10;
#if 0
	/*
	 * We do not want to set this, otherwise, we could not even send
	 * random raw ethernet frames for various other research.  By default
	 * FreeBSD will use the right ether source address.
	 */
	val4 |= BASE_CFG_COMMAND_CONFIG_TX_ADDR_INS;
#endif
	val4 |= BASE_CFG_COMMAND_CONFIG_PAD_EN;
	val4 &= ~BASE_CFG_COMMAND_CONFIG_CRC_FWD;
#if 0
	val4 |= BASE_CFG_COMMAND_CONFIG_CNTL_FRM_ENA;
#endif
#if 1
	val4 |= BASE_CFG_COMMAND_CONFIG_RX_ERR_DISC;
#endif
	val &= ~BASE_CFG_COMMAND_CONFIG_LOOP_ENA;		/* link0? */
	CSR_WRITE_4(sc, BASE_CFG_COMMAND_CONFIG, val4);

	/*
	 * Make sure we do not enable 32bit alignment;  FreeBSD cannot
	 * cope with the additional padding (though we should!?).
	 * Also make sure we get the CRC appended.
	 */
	val4 = CSR_READ_4(sc, TX_CMD_STAT);
	val4 &= ~(TX_CMD_STAT_OMIT_CRC|TX_CMD_STAT_TX_SHIFT16);
	CSR_WRITE_4(sc, TX_CMD_STAT, val4);

	val4 = CSR_READ_4(sc, RX_CMD_STAT);
	val4 &= ~RX_CMD_STAT_RX_SHIFT16;
	val4 |= RX_CMD_STAT_RX_SHIFT16;
	CSR_WRITE_4(sc, RX_CMD_STAT, val4);

	/* e. Reset MAC. */
	val4 = CSR_READ_4(sc, BASE_CFG_COMMAND_CONFIG);
	val4 |= BASE_CFG_COMMAND_CONFIG_SW_RESET;
	CSR_WRITE_4(sc, BASE_CFG_COMMAND_CONFIG, val4);
	/* Wait for bits to be cleared; i=100 is excessive. */
	for (i = 0; i < 100; i++) {
		val4 = CSR_READ_4(sc, BASE_CFG_COMMAND_CONFIG);
		if ((val4 & BASE_CFG_COMMAND_CONFIG_SW_RESET) == 0) {
			break;
		}
		DELAY(10);
	}
	if ((val4 & BASE_CFG_COMMAND_CONFIG_SW_RESET) != 0) {
		device_printf(sc->atse_dev, "MAC reset timed out.\n");
		return (ENXIO);
	}

	/* f. Enable MAC transmit and receive datapath. */
	mask = BASE_CFG_COMMAND_CONFIG_TX_ENA|BASE_CFG_COMMAND_CONFIG_RX_ENA;
	val4 = CSR_READ_4(sc, BASE_CFG_COMMAND_CONFIG);
	val4 |= mask;
	CSR_WRITE_4(sc, BASE_CFG_COMMAND_CONFIG, val4);
	/* Wait for bits to be cleared; i=100 is excessive. */
	for (i = 0; i < 100; i++) {
		val4 = CSR_READ_4(sc, BASE_CFG_COMMAND_CONFIG);
		if ((val4 & mask) == mask) {
			break;
		}
		DELAY(10);
	}
	if ((val4 & mask) != mask) {
		device_printf(sc->atse_dev, "Enabling MAC TX/RX timed out.\n");
		return (ENXIO);
	}

	return (0);
}

static void
atse_init_locked(struct atse_softc *sc)
{
	struct ifnet *ifp;
	struct mii_data *mii;
	uint8_t *eaddr;

	ATSE_LOCK_ASSERT(sc);
	ifp = sc->atse_ifp;

	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
		return;
	}

	/*
	 * Must update the ether address if changed.  Given we do not handle
	 * in atse_ioctl() but it's in the general framework, just always
	 * do it here before atse_reset().
	 */
	eaddr = IF_LLADDR(sc->atse_ifp);
	bcopy(eaddr, &sc->atse_eth_addr, ETHER_ADDR_LEN);

	/* Make things frind to halt, cleanup, ... */
	atse_stop_locked(sc);

	atse_reset(sc);

	/* ... and fire up the engine again. */
	atse_rxfilter_locked(sc);

	sc->atse_flags &= ATSE_FLAGS_LINK;	/* Preserve. */

	mii = device_get_softc(sc->atse_miibus);

	sc->atse_flags &= ~ATSE_FLAGS_LINK;
	mii_mediachg(mii);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	callout_reset(&sc->atse_tick, hz, atse_tick, sc);
}

static void
atse_init(void *xsc)
{
	struct atse_softc *sc;

	/*
	 * XXXRW: There is some argument that we should immediately do RX
	 * processing after enabling interrupts, or one may not fire if there
	 * are buffered packets.
	 */
	sc = (struct atse_softc *)xsc;
	ATSE_LOCK(sc);
	atse_init_locked(sc);
	ATSE_UNLOCK(sc);
}

static int
atse_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct atse_softc *sc;
	struct ifreq *ifr;
	int error, mask;

	error = 0;
	sc = ifp->if_softc;
	ifr = (struct ifreq *)data;

	switch (command) {
	case SIOCSIFFLAGS:
		ATSE_LOCK(sc);
		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0 &&
			    ((ifp->if_flags ^ sc->atse_if_flags) &
			    (IFF_PROMISC | IFF_ALLMULTI)) != 0)
				atse_rxfilter_locked(sc);
			else
				atse_init_locked(sc);
		} else if (ifp->if_drv_flags & IFF_DRV_RUNNING)
			atse_stop_locked(sc);
		sc->atse_if_flags = ifp->if_flags;
		ATSE_UNLOCK(sc);
		break;
	case SIOCSIFCAP:
		ATSE_LOCK(sc);
		mask = ifr->ifr_reqcap ^ ifp->if_capenable;
		ATSE_UNLOCK(sc);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		ATSE_LOCK(sc);
		atse_rxfilter_locked(sc);
		ATSE_UNLOCK(sc);
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
	{
		struct mii_data *mii;
		struct ifreq *ifr;

		mii = device_get_softc(sc->atse_miibus);
		ifr = (struct ifreq *)data;
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		break;
	}
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}

	return (error);
}

static void
atse_tick(void *xsc)
{
	struct atse_softc *sc;
	struct mii_data *mii;
	struct ifnet *ifp;

	sc = (struct atse_softc *)xsc;
	ATSE_LOCK_ASSERT(sc);
	ifp = sc->atse_ifp;

	mii = device_get_softc(sc->atse_miibus);
	mii_tick(mii);
	if ((sc->atse_flags & ATSE_FLAGS_LINK) == 0) {
		atse_miibus_statchg(sc->atse_dev);
	}

	callout_reset(&sc->atse_tick, hz, atse_tick, sc);
}

/*
 * Set media options.
 */
static int
atse_ifmedia_upd(struct ifnet *ifp)
{
	struct atse_softc *sc;
	struct mii_data *mii;
	struct mii_softc *miisc;
	int error;

	sc = ifp->if_softc;

	ATSE_LOCK(sc);
	mii = device_get_softc(sc->atse_miibus);
	LIST_FOREACH(miisc, &mii->mii_phys, mii_list) {
		PHY_RESET(miisc);
	}
	error = mii_mediachg(mii);
	ATSE_UNLOCK(sc);

	return (error);
}

/*
 * Report current media status.
 */
static void
atse_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct atse_softc *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;

	ATSE_LOCK(sc);
	mii = device_get_softc(sc->atse_miibus);
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
	ATSE_UNLOCK(sc);
}

static struct atse_mac_stats_regs {
	const char *name;
	const char *descr;	/* Mostly copied from Altera datasheet. */
} atse_mac_stats_regs[] = {
	[0x1a] =
	{ "aFramesTransmittedOK",
	    "The number of frames that are successfully transmitted including "
	    "the pause frames." },
	{ "aFramesReceivedOK",
	    "The number of frames that are successfully received including the "
	    "pause frames." },
	{ "aFrameCheckSequenceErrors",
	    "The number of receive frames with CRC error." },
	{ "aAlignmentErrors",
	    "The number of receive frames with alignment error." },
	{ "aOctetsTransmittedOK",
	    "The lower 32 bits of the number of data and padding octets that "
	    "are successfully transmitted." },
	{ "aOctetsReceivedOK",
	    "The lower 32 bits of the number of data and padding octets that "
	    " are successfully received." },
	{ "aTxPAUSEMACCtrlFrames",
	    "The number of pause frames transmitted." },
	{ "aRxPAUSEMACCtrlFrames",
	    "The number received pause frames received." },
	{ "ifInErrors",
	    "The number of errored frames received." },
	{ "ifOutErrors",
	    "The number of transmit frames with either a FIFO overflow error, "
	    "a FIFO underflow error, or a error defined by the user "
	    "application." },
	{ "ifInUcastPkts",
	    "The number of valid unicast frames received." },
	{ "ifInMulticastPkts",
	    "The number of valid multicast frames received. The count does "
	    "not include pause frames." },
	{ "ifInBroadcastPkts",
	    "The number of valid broadcast frames received." },
	{ "ifOutDiscards",
	    "This statistics counter is not in use.  The MAC function does not "
	    "discard frames that are written to the FIFO buffer by the user "
	    "application." },
	{ "ifOutUcastPkts",
	    "The number of valid unicast frames transmitted." },
	{ "ifOutMulticastPkts",
	    "The number of valid multicast frames transmitted, excluding pause "
	    "frames." },
	{ "ifOutBroadcastPkts",
	    "The number of valid broadcast frames transmitted." },
	{ "etherStatsDropEvents",
	    "The number of frames that are dropped due to MAC internal errors "
	    "when FIFO buffer overflow persists." },
	{ "etherStatsOctets",
	    "The lower 32 bits of the total number of octets received. This "
	    "count includes both good and errored frames." },
	{ "etherStatsPkts",
	    "The total number of good and errored frames received." },
	{ "etherStatsUndersizePkts",
	    "The number of frames received with length less than 64 bytes. "
	    "This count does not include errored frames." },
	{ "etherStatsOversizePkts",
	    "The number of frames received that are longer than the value "
	    "configured in the frm_length register. This count does not "
	    "include errored frames." },
	{ "etherStatsPkts64Octets",
	    "The number of 64-byte frames received. This count includes good "
	    "and errored frames." },
	{ "etherStatsPkts65to127Octets",
	    "The number of received good and errored frames between the length "
	    "of 65 and 127 bytes." },
	{ "etherStatsPkts128to255Octets",
	    "The number of received good and errored frames between the length "
	    "of 128 and 255 bytes." },
	{ "etherStatsPkts256to511Octets",
	    "The number of received good and errored frames between the length "
	    "of 256 and 511 bytes." },
	{ "etherStatsPkts512to1023Octets",
	    "The number of received good and errored frames between the length "
	    "of 512 and 1023 bytes." },
	{ "etherStatsPkts1024to1518Octets",
	    "The number of received good and errored frames between the length "
	    "of 1024 and 1518 bytes." },
	{ "etherStatsPkts1519toXOctets",
	    "The number of received good and errored frames between the length "
	    "of 1519 and the maximum frame length configured in the frm_length "
	    "register." },
	{ "etherStatsJabbers",
	    "Too long frames with CRC error." },
	{ "etherStatsFragments",
	    "Too short frames with CRC error." },
	/* 0x39 unused, 0x3a/b non-stats. */
	[0x3c] =
	/* Extended Statistics Counters */
	{ "msb_aOctetsTransmittedOK",
	    "Upper 32 bits of the number of data and padding octets that are "
	    "successfully transmitted." },
	{ "msb_aOctetsReceivedOK",
	    "Upper 32 bits of the number of data and padding octets that are "
	    "successfully received." },
	{ "msb_etherStatsOctets",
	    "Upper 32 bits of the total number of octets received. This count "
	    "includes both good and errored frames." }
};

static int
sysctl_atse_mac_stats_proc(SYSCTL_HANDLER_ARGS)
{
	struct atse_softc *sc;
	int error, offset, s;

	sc = arg1;
	offset = arg2;

	s = CSR_READ_4(sc, offset);
	error = sysctl_handle_int(oidp, &s, 0, req);
	if (error || !req->newptr) {
		return (error);
	}

	return (0);
}

static struct atse_rx_err_stats_regs {
	const char *name;
	const char *descr;
} atse_rx_err_stats_regs[] = {

#define	ATSE_RX_ERR_FIFO_THRES_EOP	0 /* FIFO threshold reached, on EOP. */
#define	ATSE_RX_ERR_ELEN		1 /* Frame/payload length not valid. */
#define	ATSE_RX_ERR_CRC32		2 /* CRC-32 error. */
#define	ATSE_RX_ERR_FIFO_THRES_TRUNC	3 /* FIFO thresh., truncated frame. */
#define	ATSE_RX_ERR_4			4 /* ? */
#define	ATSE_RX_ERR_5			5 /* / */

	{ "rx_err_fifo_thres_eop",
	    "FIFO threshold reached, reported on EOP." },
	{ "rx_err_fifo_elen",
	    "Frame or payload length not valid." },
	{ "rx_err_fifo_crc32",
	    "CRC-32 error." },
	{ "rx_err_fifo_thres_trunc",
	    "FIFO threshold reached, truncated frame" },
	{ "rx_err_4",
	    "?" },
	{ "rx_err_5",
	    "?" },
};

static int
sysctl_atse_rx_err_stats_proc(SYSCTL_HANDLER_ARGS)
{
	struct atse_softc *sc;
	int error, offset, s;

	sc = arg1;
	offset = arg2;

	s = sc->atse_rx_err[offset];
	error = sysctl_handle_int(oidp, &s, 0, req);
	if (error || !req->newptr) {
		return (error);
	}

	return (0);
}

static void
atse_sysctl_stats_attach(device_t dev)
{
	struct sysctl_ctx_list *sctx;
	struct sysctl_oid *soid;
	struct atse_softc *sc;
	int i;

	sc = device_get_softc(dev);
	sctx = device_get_sysctl_ctx(dev);
	soid = device_get_sysctl_tree(dev);

	/* MAC statistics. */
	for (i = 0; i < nitems(atse_mac_stats_regs); i++) {
		if (atse_mac_stats_regs[i].name == NULL ||
		    atse_mac_stats_regs[i].descr == NULL) {
			continue;
		}

		SYSCTL_ADD_PROC(sctx, SYSCTL_CHILDREN(soid), OID_AUTO,
		    atse_mac_stats_regs[i].name, CTLTYPE_UINT|CTLFLAG_RD,
		    sc, i, sysctl_atse_mac_stats_proc, "IU",
		    atse_mac_stats_regs[i].descr);
	}

	/* rx_err[]. */
	for (i = 0; i < ATSE_RX_ERR_MAX; i++) {
		if (atse_rx_err_stats_regs[i].name == NULL ||
		    atse_rx_err_stats_regs[i].descr == NULL) {
			continue;
		}

		SYSCTL_ADD_PROC(sctx, SYSCTL_CHILDREN(soid), OID_AUTO,
		    atse_rx_err_stats_regs[i].name, CTLTYPE_UINT|CTLFLAG_RD,
		    sc, i, sysctl_atse_rx_err_stats_proc, "IU",
		    atse_rx_err_stats_regs[i].descr);
	}
}

/*
 * Generic device handling routines.
 */
int
atse_attach(device_t dev)
{
	struct atse_softc *sc;
	struct ifnet *ifp;
	uint32_t caps;
	int error;

	sc = device_get_softc(dev);
	sc->dev = dev;

	/* Get xDMA controller */
	sc->xdma_tx = xdma_ofw_get(sc->dev, "tx");
	if (sc->xdma_tx == NULL) {
		device_printf(dev, "Can't find DMA controller.\n");
		return (ENXIO);
	}

	/*
	 * Only final (EOP) write can be less than "symbols per beat" value
	 * so we have to defrag mbuf chain.
	 * Chapter 15. On-Chip FIFO Memory Core.
	 * Embedded Peripherals IP User Guide.
	 */
	caps = XCHAN_CAP_BUSDMA_NOSEG;

	/* Alloc xDMA virtual channel. */
	sc->xchan_tx = xdma_channel_alloc(sc->xdma_tx, caps);
	if (sc->xchan_tx == NULL) {
		device_printf(dev, "Can't alloc virtual DMA channel.\n");
		return (ENXIO);
	}

	/* Setup interrupt handler. */
	error = xdma_setup_intr(sc->xchan_tx, atse_xdma_tx_intr, sc, &sc->ih_tx);
	if (error) {
		device_printf(sc->dev,
		    "Can't setup xDMA interrupt handler.\n");
		return (ENXIO);
	}

	xdma_prep_sg(sc->xchan_tx,
	    TX_QUEUE_SIZE,	/* xchan requests queue size */
	    MCLBYTES,	/* maxsegsize */
	    8,		/* maxnsegs */
	    16,		/* alignment */
	    0,		/* boundary */
	    BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR);

	/* Get RX xDMA controller */
	sc->xdma_rx = xdma_ofw_get(sc->dev, "rx");
	if (sc->xdma_rx == NULL) {
		device_printf(dev, "Can't find DMA controller.\n");
		return (ENXIO);
	}

	/* Alloc xDMA virtual channel. */
	sc->xchan_rx = xdma_channel_alloc(sc->xdma_rx, caps);
	if (sc->xchan_rx == NULL) {
		device_printf(dev, "Can't alloc virtual DMA channel.\n");
		return (ENXIO);
	}

	/* Setup interrupt handler. */
	error = xdma_setup_intr(sc->xchan_rx, atse_xdma_rx_intr, sc, &sc->ih_rx);
	if (error) {
		device_printf(sc->dev,
		    "Can't setup xDMA interrupt handler.\n");
		return (ENXIO);
	}

	xdma_prep_sg(sc->xchan_rx,
	    RX_QUEUE_SIZE,	/* xchan requests queue size */
	    MCLBYTES,		/* maxsegsize */
	    1,			/* maxnsegs */
	    16,			/* alignment */
	    0,			/* boundary */
	    BUS_SPACE_MAXADDR_32BIT,
	    BUS_SPACE_MAXADDR);

	mtx_init(&sc->br_mtx, "buf ring mtx", NULL, MTX_DEF);
	sc->br = buf_ring_alloc(BUFRING_SIZE, M_DEVBUF,
	    M_NOWAIT, &sc->br_mtx);
	if (sc->br == NULL) {
		return (ENOMEM);
	}

	atse_ethernet_option_bits_read(dev);

	mtx_init(&sc->atse_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);

	callout_init_mtx(&sc->atse_tick, &sc->atse_mtx, 0);

	/*
	 * We are only doing single-PHY with this driver currently.  The
	 * defaults would be right so that BASE_CFG_MDIO_ADDR0 points to the
	 * 1st PHY address (0) apart from the fact that BMCR0 is always
	 * the PCS mapping, so we always use BMCR1. See Table 5-1 0xA0-0xBF.
	 */
#if 0	/* Always PCS. */
	sc->atse_bmcr0 = MDIO_0_START;
	CSR_WRITE_4(sc, BASE_CFG_MDIO_ADDR0, 0x00);
#endif
	/* Always use matching PHY for atse[0..]. */
	sc->atse_phy_addr = device_get_unit(dev);
	sc->atse_bmcr1 = MDIO_1_START;
	CSR_WRITE_4(sc, BASE_CFG_MDIO_ADDR1, sc->atse_phy_addr);

	/* Reset the adapter. */
	atse_reset(sc);

	/* Setup interface. */
	ifp = sc->atse_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "if_alloc() failed\n");
		error = ENOSPC;
		goto err;
	}
	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = atse_ioctl;
	ifp->if_transmit = atse_transmit;
	ifp->if_qflush = atse_qflush;
	ifp->if_init = atse_init;
	IFQ_SET_MAXLEN(&ifp->if_snd, ATSE_TX_LIST_CNT - 1);
	ifp->if_snd.ifq_drv_maxlen = ATSE_TX_LIST_CNT - 1;
	IFQ_SET_READY(&ifp->if_snd);

	/* MII setup. */
	error = mii_attach(dev, &sc->atse_miibus, ifp, atse_ifmedia_upd,
	    atse_ifmedia_sts, BMSR_DEFCAPMASK, MII_PHY_ANY, MII_OFFSET_ANY, 0);
	if (error != 0) {
		device_printf(dev, "attaching PHY failed: %d\n", error);
		goto err;
	}

	/* Call media-indepedent attach routine. */
	ether_ifattach(ifp, sc->atse_eth_addr);

	/* Tell the upper layer(s) about vlan mtu support. */
	ifp->if_hdrlen = sizeof(struct ether_vlan_header);
	ifp->if_capabilities |= IFCAP_VLAN_MTU;
	ifp->if_capenable = ifp->if_capabilities;

err:
	if (error != 0) {
		atse_detach(dev);
	}

	if (error == 0) {
		atse_sysctl_stats_attach(dev);
	}

	atse_rx_enqueue(sc, NUM_RX_MBUF);
	xdma_queue_submit(sc->xchan_rx);

	return (error);
}

static int
atse_detach(device_t dev)
{
	struct atse_softc *sc;
	struct ifnet *ifp;

	sc = device_get_softc(dev);
	KASSERT(mtx_initialized(&sc->atse_mtx), ("%s: mutex not initialized",
	    device_get_nameunit(dev)));
	ifp = sc->atse_ifp;

	/* Only cleanup if attach succeeded. */
	if (device_is_attached(dev)) {
		ATSE_LOCK(sc);
		atse_stop_locked(sc);
		ATSE_UNLOCK(sc);
		callout_drain(&sc->atse_tick);
		ether_ifdetach(ifp);
	}
	if (sc->atse_miibus != NULL) {
		device_delete_child(dev, sc->atse_miibus);
	}

	if (ifp != NULL) {
		if_free(ifp);
	}

	mtx_destroy(&sc->atse_mtx);

	return (0);
}

/* Shared between nexus and fdt implementation. */
void
atse_detach_resources(device_t dev)
{
	struct atse_softc *sc;

	sc = device_get_softc(dev);

	if (sc->atse_mem_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY, sc->atse_mem_rid,
		    sc->atse_mem_res);
		sc->atse_mem_res = NULL;
	}
}

int
atse_detach_dev(device_t dev)
{
	int error;

	error = atse_detach(dev);
	if (error) {
		/* We are basically in undefined state now. */
		device_printf(dev, "atse_detach() failed: %d\n", error);
		return (error);
	}

	atse_detach_resources(dev);

	return (0);
}

int
atse_miibus_readreg(device_t dev, int phy, int reg)
{
	struct atse_softc *sc;
	int val;

	sc = device_get_softc(dev);

	/*
	 * We currently do not support re-mapping of MDIO space on-the-fly
	 * but de-facto hard-code the phy#.
	 */
	if (phy != sc->atse_phy_addr) {
		return (0);
	}

	val = PHY_READ_2(sc, reg);

	return (val);
}

int
atse_miibus_writereg(device_t dev, int phy, int reg, int data)
{
	struct atse_softc *sc;

	sc = device_get_softc(dev);

	/*
	 * We currently do not support re-mapping of MDIO space on-the-fly
	 * but de-facto hard-code the phy#.
	 */
	if (phy != sc->atse_phy_addr) {
		return (0);
	}

	PHY_WRITE_2(sc, reg, data);
	return (0);
}

void
atse_miibus_statchg(device_t dev)
{
	struct atse_softc *sc;
	struct mii_data *mii;
	struct ifnet *ifp;
	uint32_t val4;

	sc = device_get_softc(dev);
	ATSE_LOCK_ASSERT(sc);

	mii = device_get_softc(sc->atse_miibus);
	ifp = sc->atse_ifp;
	if (mii == NULL || ifp == NULL ||
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
		return;
	}

	val4 = CSR_READ_4(sc, BASE_CFG_COMMAND_CONFIG);

	/* Assume no link. */
	sc->atse_flags &= ~ATSE_FLAGS_LINK;

	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID)) {

		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
			val4 |= BASE_CFG_COMMAND_CONFIG_ENA_10;
			val4 &= ~BASE_CFG_COMMAND_CONFIG_ETH_SPEED;
			sc->atse_flags |= ATSE_FLAGS_LINK;
			break;
		case IFM_100_TX:
			val4 &= ~BASE_CFG_COMMAND_CONFIG_ENA_10;
			val4 &= ~BASE_CFG_COMMAND_CONFIG_ETH_SPEED;
			sc->atse_flags |= ATSE_FLAGS_LINK;
			break;
		case IFM_1000_T:
			val4 &= ~BASE_CFG_COMMAND_CONFIG_ENA_10;
			val4 |= BASE_CFG_COMMAND_CONFIG_ETH_SPEED;
			sc->atse_flags |= ATSE_FLAGS_LINK;
			break;
		default:
			break;
		}
	}

	if ((sc->atse_flags & ATSE_FLAGS_LINK) == 0) {
		/* Need to stop the MAC? */
		return;
	}

	if (IFM_OPTIONS(mii->mii_media_active & IFM_FDX) != 0) {
		val4 &= ~BASE_CFG_COMMAND_CONFIG_HD_ENA;
	} else {
		val4 |= BASE_CFG_COMMAND_CONFIG_HD_ENA;
	}

	/* flow control? */

	/* Make sure the MAC is activated. */
	val4 |= BASE_CFG_COMMAND_CONFIG_TX_ENA;
	val4 |= BASE_CFG_COMMAND_CONFIG_RX_ENA;

	CSR_WRITE_4(sc, BASE_CFG_COMMAND_CONFIG, val4);
}

MODULE_DEPEND(atse, ether, 1, 1, 1);
MODULE_DEPEND(atse, miibus, 1, 1, 1);
