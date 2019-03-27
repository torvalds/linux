/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Ganbold Tsagaankhuu <ganbold@freebsd.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/* A10/A20 EMAC driver */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mbuf.h>
#include <sys/mutex.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/gpio.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/intr.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_mib.h>
#include <net/ethernet.h>
#include <net/if_vlan_var.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#endif

#include <net/bpf.h>
#include <net/bpfdesc.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <arm/allwinner/if_emacreg.h>
#include <arm/allwinner/aw_sid.h>

#include <dev/extres/clk/clk.h>

#include "miibus_if.h"

#include "gpio_if.h"

#include "a10_sramc.h"

struct emac_softc {
	struct ifnet		*emac_ifp;
	device_t		emac_dev;
	device_t		emac_miibus;
	bus_space_handle_t	emac_handle;
	bus_space_tag_t		emac_tag;
	struct resource		*emac_res;
	struct resource		*emac_irq;
	void			*emac_intrhand;
	clk_t			emac_clk;
	int			emac_if_flags;
	struct mtx		emac_mtx;
	struct callout		emac_tick_ch;
	int			emac_watchdog_timer;
	int			emac_rx_process_limit;
	int			emac_link;
	uint32_t		emac_fifo_mask;
};

static int	emac_probe(device_t);
static int	emac_attach(device_t);
static int	emac_detach(device_t);
static int	emac_shutdown(device_t);
static int	emac_suspend(device_t);
static int	emac_resume(device_t);

static int	emac_sys_setup(struct emac_softc *);
static void	emac_reset(struct emac_softc *);

static void	emac_init_locked(struct emac_softc *);
static void	emac_start_locked(struct ifnet *);
static void	emac_init(void *);
static void	emac_stop_locked(struct emac_softc *);
static void	emac_intr(void *);
static int	emac_ioctl(struct ifnet *, u_long, caddr_t);

static void	emac_rxeof(struct emac_softc *, int);
static void	emac_txeof(struct emac_softc *, uint32_t);

static int	emac_miibus_readreg(device_t, int, int);
static int	emac_miibus_writereg(device_t, int, int, int);
static void	emac_miibus_statchg(device_t);

static int	emac_ifmedia_upd(struct ifnet *);
static void	emac_ifmedia_sts(struct ifnet *, struct ifmediareq *);

static int	sysctl_int_range(SYSCTL_HANDLER_ARGS, int, int);
static int	sysctl_hw_emac_proc_limit(SYSCTL_HANDLER_ARGS);

#define	EMAC_READ_REG(sc, reg)		\
    bus_space_read_4(sc->emac_tag, sc->emac_handle, reg)
#define	EMAC_WRITE_REG(sc, reg, val)	\
    bus_space_write_4(sc->emac_tag, sc->emac_handle, reg, val)

static int
emac_sys_setup(struct emac_softc *sc)
{
	int error;

	/* Activate EMAC clock. */
	error = clk_get_by_ofw_index(sc->emac_dev, 0, 0, &sc->emac_clk);
	if (error != 0) {
		device_printf(sc->emac_dev, "cannot get clock\n");
		return (error);
	}
	error = clk_enable(sc->emac_clk);
	if (error != 0) {
		device_printf(sc->emac_dev, "cannot enable clock\n");
		return (error);
	}

	/* Map sram. */
	a10_map_to_emac();

	return (0);
}

static void
emac_get_hwaddr(struct emac_softc *sc, uint8_t *hwaddr)
{
	uint32_t val0, val1, rnd;
	u_char rootkey[16];
	size_t rootkey_size;

	/*
	 * Try to get MAC address from running hardware.
	 * If there is something non-zero there just use it.
	 *
	 * Otherwise set the address to a convenient locally assigned address,
	 * using the SID rootkey.
	 * This is was uboot does so we end up with the same mac as if uboot
	 * did set it.
	 * If we can't get the root key, generate a random one,
	 * 'bsd' + random 24 low-order bits. 'b' is 0x62, which has the locally
	 * assigned bit set, and the broadcast/multicast bit clear.
	 */
	val0 = EMAC_READ_REG(sc, EMAC_MAC_A0);
	val1 = EMAC_READ_REG(sc, EMAC_MAC_A1);
	if ((val0 | val1) != 0 && (val0 | val1) != 0xffffff) {
		hwaddr[0] = (val1 >> 16) & 0xff;
		hwaddr[1] = (val1 >> 8) & 0xff;
		hwaddr[2] = (val1 >> 0) & 0xff;
		hwaddr[3] = (val0 >> 16) & 0xff;
		hwaddr[4] = (val0 >> 8) & 0xff;
		hwaddr[5] = (val0 >> 0) & 0xff;
	} else {
		rootkey_size = sizeof(rootkey);
		if (aw_sid_get_fuse(AW_SID_FUSE_ROOTKEY, rootkey,
		    &rootkey_size) == 0) {
			hwaddr[0] = 0x2;
			hwaddr[1] = rootkey[3];
			hwaddr[2] = rootkey[12];
			hwaddr[3] = rootkey[13];
			hwaddr[4] = rootkey[14];
			hwaddr[5] = rootkey[15];
		}
		else {
			rnd = arc4random() & 0x00ffffff;
			hwaddr[0] = 'b';
			hwaddr[1] = 's';
			hwaddr[2] = 'd';
			hwaddr[3] = (rnd >> 16) & 0xff;
			hwaddr[4] = (rnd >> 8) & 0xff;
			hwaddr[5] = (rnd >> 0) & 0xff;
		}
	}
	if (bootverbose)
		printf("MAC address: %s\n", ether_sprintf(hwaddr));
}

static void
emac_set_rx_mode(struct emac_softc *sc)
{
	struct ifnet *ifp;
	struct ifmultiaddr *ifma;
	uint32_t h, hashes[2];
	uint32_t rcr = 0;

	EMAC_ASSERT_LOCKED(sc);

	ifp = sc->emac_ifp;

	rcr = EMAC_READ_REG(sc, EMAC_RX_CTL);

	/* Unicast packet and DA filtering */
	rcr |= EMAC_RX_UCAD;
	rcr |= EMAC_RX_DAF;

	hashes[0] = 0;
	hashes[1] = 0;
	if (ifp->if_flags & IFF_ALLMULTI) {
		hashes[0] = 0xffffffff;
		hashes[1] = 0xffffffff;
	} else {
		if_maddr_rlock(ifp);
		CK_STAILQ_FOREACH(ifma, &sc->emac_ifp->if_multiaddrs, ifma_link) {
			if (ifma->ifma_addr->sa_family != AF_LINK)
				continue;
			h = ether_crc32_be(LLADDR((struct sockaddr_dl *)
			    ifma->ifma_addr), ETHER_ADDR_LEN) >> 26;
			hashes[h >> 5] |= 1 << (h & 0x1f);
		}
		if_maddr_runlock(ifp);
	}
	rcr |= EMAC_RX_MCO;
	rcr |= EMAC_RX_MHF;
	EMAC_WRITE_REG(sc, EMAC_RX_HASH0, hashes[0]);
	EMAC_WRITE_REG(sc, EMAC_RX_HASH1, hashes[1]);

	if (ifp->if_flags & IFF_BROADCAST) {
		rcr |= EMAC_RX_BCO;
		rcr |= EMAC_RX_MCO;
	}

	if (ifp->if_flags & IFF_PROMISC)
		rcr |= EMAC_RX_PA;
	else
		rcr |= EMAC_RX_UCAD;

	EMAC_WRITE_REG(sc, EMAC_RX_CTL, rcr);
}

static void
emac_reset(struct emac_softc *sc)
{

	EMAC_WRITE_REG(sc, EMAC_CTL, 0);
	DELAY(200);
	EMAC_WRITE_REG(sc, EMAC_CTL, 1);
	DELAY(200);
}

static void
emac_drain_rxfifo(struct emac_softc *sc)
{
	uint32_t data;

	while (EMAC_READ_REG(sc, EMAC_RX_FBC) > 0)
		data = EMAC_READ_REG(sc, EMAC_RX_IO_DATA);
}

static void
emac_txeof(struct emac_softc *sc, uint32_t status)
{
	struct ifnet *ifp;

	EMAC_ASSERT_LOCKED(sc);

	ifp = sc->emac_ifp;
	status &= (EMAC_TX_FIFO0 | EMAC_TX_FIFO1);
	sc->emac_fifo_mask &= ~status;
	if (status == (EMAC_TX_FIFO0 | EMAC_TX_FIFO1))
		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 2);
	else
		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	/* Unarm watchdog timer if no TX */
	sc->emac_watchdog_timer = 0;
}

static void
emac_rxeof(struct emac_softc *sc, int count)
{
	struct ifnet *ifp;
	struct mbuf *m, *m0;
	uint32_t reg_val, rxcount;
	int16_t len;
	uint16_t status;
	int i;

	ifp = sc->emac_ifp;
	for (; count > 0 &&
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) != 0; count--) {
		/*
		 * Race warning: The first packet might arrive with
		 * the interrupts disabled, but the second will fix
		 */
		rxcount = EMAC_READ_REG(sc, EMAC_RX_FBC);
		if (!rxcount) {
			/* Had one stuck? */
			rxcount = EMAC_READ_REG(sc, EMAC_RX_FBC);
			if (!rxcount)
				return;
		}
		/* Check packet header */
		reg_val = EMAC_READ_REG(sc, EMAC_RX_IO_DATA);
		if (reg_val != EMAC_PACKET_HEADER) {
			/* Packet header is wrong */
			if (bootverbose)
				if_printf(ifp, "wrong packet header\n");
			/* Disable RX */
			reg_val = EMAC_READ_REG(sc, EMAC_CTL);
			reg_val &= ~EMAC_CTL_RX_EN;
			EMAC_WRITE_REG(sc, EMAC_CTL, reg_val);

			/* Flush RX FIFO */
			reg_val = EMAC_READ_REG(sc, EMAC_RX_CTL);
			reg_val |= EMAC_RX_FLUSH_FIFO;
			EMAC_WRITE_REG(sc, EMAC_RX_CTL, reg_val);
			for (i = 100; i > 0; i--) {
				DELAY(100);
				if ((EMAC_READ_REG(sc, EMAC_RX_CTL) &
				    EMAC_RX_FLUSH_FIFO) == 0)
					break;
			}
			if (i == 0) {
				device_printf(sc->emac_dev,
				    "flush FIFO timeout\n");
				/* Reinitialize controller */
				emac_init_locked(sc);
				return;
			}
			/* Enable RX */
			reg_val = EMAC_READ_REG(sc, EMAC_CTL);
			reg_val |= EMAC_CTL_RX_EN;
			EMAC_WRITE_REG(sc, EMAC_CTL, reg_val);

			return;
		}

		/* Get packet size and status */
		reg_val = EMAC_READ_REG(sc, EMAC_RX_IO_DATA);
		len = reg_val & 0xffff;
		status = (reg_val >> 16) & 0xffff;

		if (len < 64 || (status & EMAC_PKT_OK) == 0) {
			if (bootverbose)
				if_printf(ifp,
				    "bad packet: len = %i status = %i\n",
				    len, status);
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			emac_drain_rxfifo(sc);
			continue;
		}
#if 0
		if (status & (EMAC_CRCERR | EMAC_LENERR)) {
			good_packet = 0;
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			if (status & EMAC_CRCERR)
				if_printf(ifp, "crc error\n");
			if (status & EMAC_LENERR)
				if_printf(ifp, "length error\n");
		}
#endif
		m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
		if (m == NULL) {
			emac_drain_rxfifo(sc);
			return;
		}
		m->m_len = m->m_pkthdr.len = MCLBYTES;

		/* Copy entire frame to mbuf first. */
		bus_space_read_multi_4(sc->emac_tag, sc->emac_handle,
		    EMAC_RX_IO_DATA, mtod(m, uint32_t *), roundup2(len, 4) / 4);

		m->m_pkthdr.rcvif = ifp;
		m->m_len = m->m_pkthdr.len = len - ETHER_CRC_LEN;

		/*
		 * Emac controller needs strict aligment, so to avoid
		 * copying over an entire frame to align, we allocate
		 * a new mbuf and copy ethernet header + IP header to
		 * the new mbuf. The new mbuf is prepended into the
		 * existing mbuf chain.
		 */
		if (m->m_len <= (MHLEN - ETHER_HDR_LEN)) {
			bcopy(m->m_data, m->m_data + ETHER_HDR_LEN, m->m_len);
			m->m_data += ETHER_HDR_LEN;
		} else if (m->m_len <= (MCLBYTES - ETHER_HDR_LEN) &&
		    m->m_len > (MHLEN - ETHER_HDR_LEN)) {
			MGETHDR(m0, M_NOWAIT, MT_DATA);
			if (m0 != NULL) {
				len = ETHER_HDR_LEN + m->m_pkthdr.l2hlen;
				bcopy(m->m_data, m0->m_data, len);
				m->m_data += len;
				m->m_len -= len;
				m0->m_len = len;
				M_MOVE_PKTHDR(m0, m);
				m0->m_next = m;
				m = m0;
			} else {
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
				m_freem(m);
				m = NULL;
				continue;
			}
		} else if (m->m_len > EMAC_MAC_MAXF) {
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			m_freem(m);
			m = NULL;
			continue;
		}
		if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
		EMAC_UNLOCK(sc);
		(*ifp->if_input)(ifp, m);
		EMAC_LOCK(sc);
	}
}

static void
emac_watchdog(struct emac_softc *sc)
{
	struct ifnet *ifp;

	EMAC_ASSERT_LOCKED(sc);

	if (sc->emac_watchdog_timer == 0 || --sc->emac_watchdog_timer)
		return;

	ifp = sc->emac_ifp;

	if (sc->emac_link == 0) {
		if (bootverbose)
			if_printf(sc->emac_ifp, "watchdog timeout "
			    "(missed link)\n");
	} else
		if_printf(sc->emac_ifp, "watchdog timeout -- resetting\n");
	
	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	emac_init_locked(sc);
	if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
		emac_start_locked(ifp);
}

static void
emac_tick(void *arg)
{
	struct emac_softc *sc;
	struct mii_data *mii;

	sc = (struct emac_softc *)arg;
	mii = device_get_softc(sc->emac_miibus);
	mii_tick(mii);

	emac_watchdog(sc);
	callout_reset(&sc->emac_tick_ch, hz, emac_tick, sc);
}

static void
emac_init(void *xcs)
{
	struct emac_softc *sc;

	sc = (struct emac_softc *)xcs;
	EMAC_LOCK(sc);
	emac_init_locked(sc);
	EMAC_UNLOCK(sc);
}

static void
emac_init_locked(struct emac_softc *sc)
{
	struct ifnet *ifp;
	struct mii_data *mii;
	uint32_t reg_val;
	uint8_t *eaddr;

	EMAC_ASSERT_LOCKED(sc);

	ifp = sc->emac_ifp;
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		return;

	/* Flush RX FIFO */
	reg_val = EMAC_READ_REG(sc, EMAC_RX_CTL);
	reg_val |= EMAC_RX_FLUSH_FIFO;
	EMAC_WRITE_REG(sc, EMAC_RX_CTL, reg_val);
	DELAY(1);

	/* Soft reset MAC */
	reg_val = EMAC_READ_REG(sc, EMAC_MAC_CTL0);
	reg_val &= (~EMAC_MAC_CTL0_SOFT_RST);
	EMAC_WRITE_REG(sc, EMAC_MAC_CTL0, reg_val);

	/* Set MII clock */
	reg_val = EMAC_READ_REG(sc, EMAC_MAC_MCFG);
	reg_val &= (~(0xf << 2));
	reg_val |= (0xd << 2);
	EMAC_WRITE_REG(sc, EMAC_MAC_MCFG, reg_val);

	/* Clear RX counter */
	EMAC_WRITE_REG(sc, EMAC_RX_FBC, 0);

	/* Disable all interrupt and clear interrupt status */
	EMAC_WRITE_REG(sc, EMAC_INT_CTL, 0);
	reg_val = EMAC_READ_REG(sc, EMAC_INT_STA);
	EMAC_WRITE_REG(sc, EMAC_INT_STA, reg_val);
	DELAY(1);

	/* Set up TX */
	reg_val = EMAC_READ_REG(sc, EMAC_TX_MODE);
	reg_val |= EMAC_TX_AB_M;
	reg_val &= EMAC_TX_TM;
	EMAC_WRITE_REG(sc, EMAC_TX_MODE, reg_val);

	/* Set up RX */
	reg_val = EMAC_READ_REG(sc, EMAC_RX_CTL);
	reg_val |= EMAC_RX_SETUP;
	reg_val &= EMAC_RX_TM;
	EMAC_WRITE_REG(sc, EMAC_RX_CTL, reg_val);

	/* Set up MAC CTL0. */
	reg_val = EMAC_READ_REG(sc, EMAC_MAC_CTL0);
	reg_val |= EMAC_MAC_CTL0_SETUP;
	EMAC_WRITE_REG(sc, EMAC_MAC_CTL0, reg_val);

	/* Set up MAC CTL1. */
	reg_val = EMAC_READ_REG(sc, EMAC_MAC_CTL1);
	reg_val |= EMAC_MAC_CTL1_SETUP;
	EMAC_WRITE_REG(sc, EMAC_MAC_CTL1, reg_val);

	/* Set up IPGT */
	EMAC_WRITE_REG(sc, EMAC_MAC_IPGT, EMAC_MAC_IPGT_FD);

	/* Set up IPGR */
	EMAC_WRITE_REG(sc, EMAC_MAC_IPGR, EMAC_MAC_NBTB_IPG2 |
	    (EMAC_MAC_NBTB_IPG1 << 8));

	/* Set up Collison window */
	EMAC_WRITE_REG(sc, EMAC_MAC_CLRT, EMAC_MAC_RM | (EMAC_MAC_CW << 8));

	/* Set up Max Frame Length */
	EMAC_WRITE_REG(sc, EMAC_MAC_MAXF, EMAC_MAC_MFL);

	/* Setup ethernet address */
	eaddr = IF_LLADDR(ifp);
	EMAC_WRITE_REG(sc, EMAC_MAC_A1, eaddr[0] << 16 |
	    eaddr[1] << 8 | eaddr[2]);
	EMAC_WRITE_REG(sc, EMAC_MAC_A0, eaddr[3] << 16 |
	    eaddr[4] << 8 | eaddr[5]);

	/* Setup rx filter */
	emac_set_rx_mode(sc);

	/* Enable RX/TX0/RX Hlevel interrupt */
	reg_val = EMAC_READ_REG(sc, EMAC_INT_CTL);
	reg_val |= EMAC_INT_EN;
	EMAC_WRITE_REG(sc, EMAC_INT_CTL, reg_val);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	sc->emac_link = 0;

	/* Switch to the current media. */
	mii = device_get_softc(sc->emac_miibus);
	mii_mediachg(mii);

	callout_reset(&sc->emac_tick_ch, hz, emac_tick, sc);
}


static void
emac_start(struct ifnet *ifp)
{
	struct emac_softc *sc;

	sc = ifp->if_softc;
	EMAC_LOCK(sc);
	emac_start_locked(ifp);
	EMAC_UNLOCK(sc);
}

static void
emac_start_locked(struct ifnet *ifp)
{
	struct emac_softc *sc;
	struct mbuf *m, *m0;
	uint32_t fifo, reg;

	sc = ifp->if_softc;
	if (ifp->if_drv_flags & IFF_DRV_OACTIVE)
		return;
	if (sc->emac_fifo_mask == (EMAC_TX_FIFO0 | EMAC_TX_FIFO1))
		return;
	if (sc->emac_link == 0)
		return;
	IFQ_DRV_DEQUEUE(&ifp->if_snd, m);
	if (m == NULL)
		return;

	/* Select channel */
	if (sc->emac_fifo_mask & EMAC_TX_FIFO0)
		fifo = 1;
	else
		fifo = 0;
	sc->emac_fifo_mask |= (1 << fifo);
	if (sc->emac_fifo_mask == (EMAC_TX_FIFO0 | EMAC_TX_FIFO1))
		ifp->if_drv_flags |= IFF_DRV_OACTIVE;
	EMAC_WRITE_REG(sc, EMAC_TX_INS, fifo);

	/*
	 * Emac controller wants 4 byte aligned TX buffers.
	 * We have to copy pretty much all the time.
	 */
	if (m->m_next != NULL || (mtod(m, uintptr_t) & 3) != 0) {
		m0 = m_defrag(m, M_NOWAIT);
		if (m0 == NULL) {
			m_freem(m);
			m = NULL;
			return;
		}
		m = m0;
	}
	/* Write data */
	bus_space_write_multi_4(sc->emac_tag, sc->emac_handle,
	    EMAC_TX_IO_DATA, mtod(m, uint32_t *),
	    roundup2(m->m_len, 4) / 4);

	/* Send the data lengh. */
	reg = (fifo == 0) ? EMAC_TX_PL0 : EMAC_TX_PL1;
	EMAC_WRITE_REG(sc, reg, m->m_len);

	/* Start translate from fifo to phy. */
	reg = (fifo == 0) ? EMAC_TX_CTL0 : EMAC_TX_CTL1;
	EMAC_WRITE_REG(sc, reg, EMAC_READ_REG(sc, reg) | 1);

	/* Set timeout */
	sc->emac_watchdog_timer = 5;

	/* Data have been sent to hardware, it is okay to free the mbuf now. */
	BPF_MTAP(ifp, m);
	m_freem(m);
}

static void
emac_stop_locked(struct emac_softc *sc)
{
	struct ifnet *ifp;
	uint32_t reg_val;

	EMAC_ASSERT_LOCKED(sc);

	ifp = sc->emac_ifp;
	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	sc->emac_link = 0;

	/* Disable all interrupt and clear interrupt status */
	EMAC_WRITE_REG(sc, EMAC_INT_CTL, 0);
	reg_val = EMAC_READ_REG(sc, EMAC_INT_STA);
	EMAC_WRITE_REG(sc, EMAC_INT_STA, reg_val);

	/* Disable RX/TX */
	reg_val = EMAC_READ_REG(sc, EMAC_CTL);
	reg_val &= ~(EMAC_CTL_RST | EMAC_CTL_TX_EN | EMAC_CTL_RX_EN);
	EMAC_WRITE_REG(sc, EMAC_CTL, reg_val);

	callout_stop(&sc->emac_tick_ch);
}

static void
emac_intr(void *arg)
{
	struct emac_softc *sc;
	struct ifnet *ifp;
	uint32_t reg_val;

	sc = (struct emac_softc *)arg;
	EMAC_LOCK(sc);

	/* Disable all interrupts */
	EMAC_WRITE_REG(sc, EMAC_INT_CTL, 0);
	/* Get EMAC interrupt status */
	reg_val = EMAC_READ_REG(sc, EMAC_INT_STA);
	/* Clear ISR status */
	EMAC_WRITE_REG(sc, EMAC_INT_STA, reg_val);

	/* Received incoming packet */
	if (reg_val & EMAC_INT_STA_RX)
		emac_rxeof(sc, sc->emac_rx_process_limit);

	/* Transmit Interrupt check */
	if (reg_val & EMAC_INT_STA_TX) {
		emac_txeof(sc, reg_val);
		ifp = sc->emac_ifp;
		if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
			emac_start_locked(ifp);
	}

	/* Re-enable interrupt mask */
	reg_val = EMAC_READ_REG(sc, EMAC_INT_CTL);
	reg_val |= EMAC_INT_EN;
	EMAC_WRITE_REG(sc, EMAC_INT_CTL, reg_val);
	EMAC_UNLOCK(sc);
}

static int
emac_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct emac_softc *sc;
	struct mii_data *mii;
	struct ifreq *ifr;
	int error = 0;

	sc = ifp->if_softc;
	ifr = (struct ifreq *)data;

	switch (command) {
	case SIOCSIFFLAGS:
		EMAC_LOCK(sc);
		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
				if ((ifp->if_flags ^ sc->emac_if_flags) &
				    (IFF_PROMISC | IFF_ALLMULTI))
					emac_set_rx_mode(sc);
			} else
				emac_init_locked(sc);
		} else {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
				emac_stop_locked(sc);
		}
		sc->emac_if_flags = ifp->if_flags;
		EMAC_UNLOCK(sc);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		EMAC_LOCK(sc);
		if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
			emac_set_rx_mode(sc);
		}
		EMAC_UNLOCK(sc);
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		mii = device_get_softc(sc->emac_miibus);
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}
	return (error);
}

static int
emac_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "allwinner,sun4i-a10-emac"))
		return (ENXIO);

	device_set_desc(dev, "A10/A20 EMAC ethernet controller");
	return (BUS_PROBE_DEFAULT);
}

static int
emac_detach(device_t dev)
{
	struct emac_softc *sc;

	sc = device_get_softc(dev);
	sc->emac_ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	if (device_is_attached(dev)) {
		ether_ifdetach(sc->emac_ifp);
		EMAC_LOCK(sc);
		emac_stop_locked(sc);
		EMAC_UNLOCK(sc);
		callout_drain(&sc->emac_tick_ch);
	}

	if (sc->emac_intrhand != NULL)
		bus_teardown_intr(sc->emac_dev, sc->emac_irq,
		    sc->emac_intrhand);

	if (sc->emac_miibus != NULL) {
		device_delete_child(sc->emac_dev, sc->emac_miibus);
		bus_generic_detach(sc->emac_dev);
	}

	if (sc->emac_clk != NULL)
		clk_disable(sc->emac_clk);

	if (sc->emac_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->emac_res);

	if (sc->emac_irq != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->emac_irq);

	if (sc->emac_ifp != NULL)
		if_free(sc->emac_ifp);

	if (mtx_initialized(&sc->emac_mtx))
		mtx_destroy(&sc->emac_mtx);

	return (0);
}

static int
emac_shutdown(device_t dev)
{

	return (emac_suspend(dev));
}

static int
emac_suspend(device_t dev)
{
	struct emac_softc *sc;
	struct ifnet *ifp;

	sc = device_get_softc(dev);

	EMAC_LOCK(sc);
	ifp = sc->emac_ifp;
	if ((ifp->if_drv_flags & IFF_DRV_RUNNING) != 0)
		emac_stop_locked(sc);
	EMAC_UNLOCK(sc);

	return (0);
}

static int
emac_resume(device_t dev)
{
	struct emac_softc *sc;
	struct ifnet *ifp;

	sc = device_get_softc(dev);

	EMAC_LOCK(sc);
	ifp = sc->emac_ifp;
	if ((ifp->if_flags & IFF_UP) != 0) {
		ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		emac_init_locked(sc);
	}
	EMAC_UNLOCK(sc);

	return (0);
}

static int
emac_attach(device_t dev)
{
	struct emac_softc *sc;
	struct ifnet *ifp;
	int error, rid;
	uint8_t eaddr[ETHER_ADDR_LEN];

	sc = device_get_softc(dev);
	sc->emac_dev = dev;

	error = 0;
	mtx_init(&sc->emac_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	callout_init_mtx(&sc->emac_tick_ch, &sc->emac_mtx, 0);

	rid = 0;
	sc->emac_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->emac_res == NULL) {
		device_printf(dev, "unable to map memory\n");
		error = ENXIO;
		goto fail;
	}

	sc->emac_tag = rman_get_bustag(sc->emac_res);
	sc->emac_handle = rman_get_bushandle(sc->emac_res);

	rid = 0;
	sc->emac_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE | RF_ACTIVE);
	if (sc->emac_irq == NULL) {
		device_printf(dev, "cannot allocate IRQ resources.\n");
		error = ENXIO;
		goto fail;
	}
	/* Create device sysctl node. */
	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "process_limit", CTLTYPE_INT | CTLFLAG_RW,
	    &sc->emac_rx_process_limit, 0, sysctl_hw_emac_proc_limit, "I",
	    "max number of Rx events to process");

	sc->emac_rx_process_limit = EMAC_PROC_DEFAULT;
	error = resource_int_value(device_get_name(dev), device_get_unit(dev),
	    "process_limit", &sc->emac_rx_process_limit);
	if (error == 0) {
		if (sc->emac_rx_process_limit < EMAC_PROC_MIN ||
		    sc->emac_rx_process_limit > EMAC_PROC_MAX) {
			device_printf(dev, "process_limit value out of range; "
			    "using default: %d\n", EMAC_PROC_DEFAULT);
			sc->emac_rx_process_limit = EMAC_PROC_DEFAULT;
		}
	}
	/* Setup EMAC */
	error = emac_sys_setup(sc);
	if (error != 0)
		goto fail;

	emac_reset(sc);

	ifp = sc->emac_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "unable to allocate ifp\n");
		error = ENOSPC;
		goto fail;
	}
	ifp->if_softc = sc;

	/* Setup MII */
	error = mii_attach(dev, &sc->emac_miibus, ifp, emac_ifmedia_upd,
	    emac_ifmedia_sts, BMSR_DEFCAPMASK, MII_PHY_ANY, MII_OFFSET_ANY, 0);
	if (error != 0) {
		device_printf(dev, "PHY probe failed\n");
		goto fail;
	}

	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_start = emac_start;
	ifp->if_ioctl = emac_ioctl;
	ifp->if_init = emac_init;
	IFQ_SET_MAXLEN(&ifp->if_snd, IFQ_MAXLEN);

	/* Get MAC address */
	emac_get_hwaddr(sc, eaddr);
	ether_ifattach(ifp, eaddr);

	/* VLAN capability setup. */
	ifp->if_capabilities |= IFCAP_VLAN_MTU;
	ifp->if_capenable = ifp->if_capabilities;
	/* Tell the upper layer we support VLAN over-sized frames. */
	ifp->if_hdrlen = sizeof(struct ether_vlan_header);

	error = bus_setup_intr(dev, sc->emac_irq, INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, emac_intr, sc, &sc->emac_intrhand);
	if (error != 0) {
		device_printf(dev, "could not set up interrupt handler.\n");
		ether_ifdetach(ifp);
		goto fail;
	}

fail:
	if (error != 0)
		emac_detach(dev);
	return (error);
}

static boolean_t
emac_miibus_iowait(struct emac_softc *sc)
{
	uint32_t timeout;

	for (timeout = 100; timeout != 0; --timeout) {
		DELAY(100);
		if ((EMAC_READ_REG(sc, EMAC_MAC_MIND) & 0x1) == 0)
			return (true);
	}

	return (false);
}

/*
 * The MII bus interface
 */
static int
emac_miibus_readreg(device_t dev, int phy, int reg)
{
	struct emac_softc *sc;
	int rval;

	sc = device_get_softc(dev);

	/* Issue phy address and reg */
	EMAC_WRITE_REG(sc, EMAC_MAC_MADR, (phy << 8) | reg);
	/* Pull up the phy io line */
	EMAC_WRITE_REG(sc, EMAC_MAC_MCMD, 0x1);
	if (!emac_miibus_iowait(sc)) {
		device_printf(dev, "timeout waiting for mii read\n");
		return (0);
	}
	/* Push down the phy io line */
	EMAC_WRITE_REG(sc, EMAC_MAC_MCMD, 0x0);
	/* Read data */
	rval = EMAC_READ_REG(sc, EMAC_MAC_MRDD);

	return (rval);
}

static int
emac_miibus_writereg(device_t dev, int phy, int reg, int data)
{
	struct emac_softc *sc;

	sc = device_get_softc(dev);

	/* Issue phy address and reg */
	EMAC_WRITE_REG(sc, EMAC_MAC_MADR, (phy << 8) | reg);
	/* Write data */
	EMAC_WRITE_REG(sc, EMAC_MAC_MWTD, data);
	/* Pull up the phy io line */
	EMAC_WRITE_REG(sc, EMAC_MAC_MCMD, 0x1);
	if (!emac_miibus_iowait(sc)) {
		device_printf(dev, "timeout waiting for mii write\n");
		return (0);
	}
	/* Push down the phy io line */
	EMAC_WRITE_REG(sc, EMAC_MAC_MCMD, 0x0);

	return (0);
}

static void
emac_miibus_statchg(device_t dev)
{
	struct emac_softc *sc;
	struct mii_data *mii;
	struct ifnet *ifp;
	uint32_t reg_val;

	sc = device_get_softc(dev);

	mii = device_get_softc(sc->emac_miibus);
	ifp = sc->emac_ifp;
	if (mii == NULL || ifp == NULL ||
	    (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
		return;

	sc->emac_link = 0;
	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID)) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
		case IFM_100_TX:
			sc->emac_link = 1;
			break;
		default:
			break;
		}
	}
	/* Program MACs with resolved speed/duplex. */
	if (sc->emac_link != 0) {
		reg_val = EMAC_READ_REG(sc, EMAC_MAC_IPGT);
		if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) != 0) {
			reg_val &= ~EMAC_MAC_IPGT_HD;
			reg_val |= EMAC_MAC_IPGT_FD;
		} else {
			reg_val &= ~EMAC_MAC_IPGT_FD;
			reg_val |= EMAC_MAC_IPGT_HD;
		}
		EMAC_WRITE_REG(sc, EMAC_MAC_IPGT, reg_val);
		/* Enable RX/TX */
		reg_val = EMAC_READ_REG(sc, EMAC_CTL);
		reg_val |= EMAC_CTL_RST | EMAC_CTL_TX_EN | EMAC_CTL_RX_EN;
		EMAC_WRITE_REG(sc, EMAC_CTL, reg_val);
	} else {
		/* Disable RX/TX */
		reg_val = EMAC_READ_REG(sc, EMAC_CTL);
		reg_val &= ~(EMAC_CTL_RST | EMAC_CTL_TX_EN | EMAC_CTL_RX_EN);
		EMAC_WRITE_REG(sc, EMAC_CTL, reg_val);
	}
}

static int
emac_ifmedia_upd(struct ifnet *ifp)
{
	struct emac_softc *sc;
	struct mii_data *mii;
	struct mii_softc *miisc;
	int error;

	sc = ifp->if_softc;
	mii = device_get_softc(sc->emac_miibus);
	EMAC_LOCK(sc);
	LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
		PHY_RESET(miisc);
	error = mii_mediachg(mii);
	EMAC_UNLOCK(sc);

	return (error);
}

static void
emac_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct emac_softc *sc;
	struct mii_data *mii;

	sc = ifp->if_softc;
	mii = device_get_softc(sc->emac_miibus);

	EMAC_LOCK(sc);
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
	EMAC_UNLOCK(sc);
}

static device_method_t emac_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		emac_probe),
	DEVMETHOD(device_attach,	emac_attach),
	DEVMETHOD(device_detach,	emac_detach),
	DEVMETHOD(device_shutdown,	emac_shutdown),
	DEVMETHOD(device_suspend,	emac_suspend),
	DEVMETHOD(device_resume,	emac_resume),

	/* bus interface, for miibus */
	DEVMETHOD(bus_print_child,	bus_generic_print_child),
	DEVMETHOD(bus_driver_added,	bus_generic_driver_added),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	emac_miibus_readreg),
	DEVMETHOD(miibus_writereg,	emac_miibus_writereg),
	DEVMETHOD(miibus_statchg,	emac_miibus_statchg),

	DEVMETHOD_END
};

static driver_t emac_driver = {
	"emac",
	emac_methods,
	sizeof(struct emac_softc)
};

static devclass_t emac_devclass;

DRIVER_MODULE(emac, simplebus, emac_driver, emac_devclass, 0, 0);
DRIVER_MODULE(miibus, emac, miibus_driver, miibus_devclass, 0, 0);
MODULE_DEPEND(emac, miibus, 1, 1, 1);
MODULE_DEPEND(emac, ether, 1, 1, 1);

static int
sysctl_int_range(SYSCTL_HANDLER_ARGS, int low, int high)
{
	int error, value;

	if (arg1 == NULL)
		return (EINVAL);
	value = *(int *)arg1;
	error = sysctl_handle_int(oidp, &value, 0, req);
	if (error || req->newptr == NULL)
		return (error);
	if (value < low || value > high)
		return (EINVAL);
	*(int *)arg1 = value;

	return (0);
}

static int
sysctl_hw_emac_proc_limit(SYSCTL_HANDLER_ARGS)
{

	return (sysctl_int_range(oidp, arg1, arg2, req,
	    EMAC_PROC_MIN, EMAC_PROC_MAX));
}
