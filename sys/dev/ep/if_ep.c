/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1994 Herb Peyerl <hpeyerl@novatel.ca>
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Herb Peyerl.
 * 4. The name of Herb Peyerl may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 *	Modified from the FreeBSD 1.1.5.1 version by:
 *		 	Andres Vega Garcia
 *			INRIA - Sophia Antipolis, France
 *			avega@sophia.inria.fr
 */

/*
 *  Promiscuous mode added and interrupt logic slightly changed
 *  to reduce the number of adapter failures. Transceiver select
 *  logic changed to use value from EEPROM. Autoconfiguration
 *  features added.
 *  Done by:
 *          Serge Babkin
 *          Chelindbank (Chelyabinsk, Russia)
 *          babkin@hq.icb.chel.su
 */

/*
 * Pccard support for 3C589 by:
 *		HAMADA Naoki
 *		nao@tom-yam.or.jp
 */

/*
 * MAINTAINER: Matthew N. Dodd <winter@jurai.net>
 *                             <mdodd@FreeBSD.org>
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/ethernet.h>
#include <net/bpf.h>

#include <dev/ep/if_epreg.h>
#include <dev/ep/if_epvar.h>

/* Exported variables */
devclass_t ep_devclass;

static int ep_media2if_media[] =
{IFM_10_T, IFM_10_5, IFM_NONE, IFM_10_2, IFM_NONE};

/* if functions */
static void epinit(void *);
static int epioctl(struct ifnet *, u_long, caddr_t);
static void epstart(struct ifnet *);

static void ep_intr_locked(struct ep_softc *);
static void epstart_locked(struct ifnet *);
static void epinit_locked(struct ep_softc *);
static void eptick(void *);
static void epwatchdog(struct ep_softc *);

/* if_media functions */
static int ep_ifmedia_upd(struct ifnet *);
static void ep_ifmedia_sts(struct ifnet *, struct ifmediareq *);

static void epstop(struct ep_softc *);
static void epread(struct ep_softc *);
static int eeprom_rdy(struct ep_softc *);

#define EP_FTST(sc, f)	(sc->stat &   (f))
#define EP_FSET(sc, f)	(sc->stat |=  (f))
#define EP_FRST(sc, f)	(sc->stat &= ~(f))

static int
eeprom_rdy(struct ep_softc *sc)
{
	int i;

	for (i = 0; is_eeprom_busy(sc) && i < MAX_EEPROMBUSY; i++)
		DELAY(100);

	if (i >= MAX_EEPROMBUSY) {
		device_printf(sc->dev, "eeprom failed to come ready.\n");
 		return (ENXIO);
	}

	return (0);
}

/*
 * get_e: gets a 16 bits word from the EEPROM. we must have set the window
 * before
 */
int
ep_get_e(struct ep_softc *sc, uint16_t offset, uint16_t *result)
{

	if (eeprom_rdy(sc))
		return (ENXIO);

	CSR_WRITE_2(sc, EP_W0_EEPROM_COMMAND,
	    (EEPROM_CMD_RD << sc->epb.cmd_off) | offset);

	if (eeprom_rdy(sc))
		return (ENXIO);

	(*result) = CSR_READ_2(sc, EP_W0_EEPROM_DATA);

	return (0);
}

static int
ep_get_macaddr(struct ep_softc *sc, u_char *addr)
{
	int i;
	uint16_t result;
	int error;
	uint16_t *macaddr;

	macaddr = (uint16_t *) addr;

	GO_WINDOW(sc, 0);
	for (i = EEPROM_NODE_ADDR_0; i <= EEPROM_NODE_ADDR_2; i++) {
		error = ep_get_e(sc, i, &result);
		if (error)
			return (error);
		macaddr[i] = htons(result);
	}
	return (0);
}

int
ep_alloc(device_t dev)
{
	struct ep_softc *sc = device_get_softc(dev);
	int rid;
	int error = 0;
	uint16_t result;

	rid = 0;
	sc->iobase = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &rid,
	    RF_ACTIVE);
	if (!sc->iobase) {
		device_printf(dev, "No I/O space?!\n");
		error = ENXIO;
		goto bad;
	}
	rid = 0;
	sc->irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE);
	if (!sc->irq) {
		device_printf(dev, "No irq?!\n");
		error = ENXIO;
		goto bad;
	}
	sc->dev = dev;
	sc->stat = 0;		/* 16 bit access */

	sc->bst = rman_get_bustag(sc->iobase);
	sc->bsh = rman_get_bushandle(sc->iobase);

	sc->ep_connectors = 0;
	sc->ep_connector = 0;

	GO_WINDOW(sc, 0);

	error = ep_get_e(sc, EEPROM_PROD_ID, &result);
	if (error)
		goto bad;
	sc->epb.prod_id = result;

	error = ep_get_e(sc, EEPROM_RESOURCE_CFG, &result);
	if (error)
		goto bad;
	sc->epb.res_cfg = result;

bad:
	if (error != 0)
		ep_free(dev);
	return (error);
}

void
ep_get_media(struct ep_softc *sc)
{
	uint16_t config;

	GO_WINDOW(sc, 0);
	config = CSR_READ_2(sc, EP_W0_CONFIG_CTRL);
	if (config & IS_AUI)
		sc->ep_connectors |= AUI;
	if (config & IS_BNC)
		sc->ep_connectors |= BNC;
	if (config & IS_UTP)
		sc->ep_connectors |= UTP;

	if (!(sc->ep_connectors & 7))
		if (bootverbose)
			device_printf(sc->dev, "no connectors!\n");

	/*
	 * This works for most of the cards so we'll do it here.
	 * The cards that require something different can override
	 * this later on.
	 */
	sc->ep_connector = CSR_READ_2(sc, EP_W0_ADDRESS_CFG) >> ACF_CONNECTOR_BITS;
}

void
ep_free(device_t dev)
{
	struct ep_softc *sc = device_get_softc(dev);

	if (sc->ep_intrhand)
		bus_teardown_intr(dev, sc->irq, sc->ep_intrhand);
	if (sc->iobase)
		bus_release_resource(dev, SYS_RES_IOPORT, 0, sc->iobase);
	if (sc->irq)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->irq);
	sc->ep_intrhand = 0;
	sc->iobase = 0;
	sc->irq = 0;
}

static void
ep_setup_station(struct ep_softc *sc, u_char *enaddr)
{
	int i;
	
	/*
	 * Setup the station address
	 */
	GO_WINDOW(sc, 2);
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		CSR_WRITE_1(sc, EP_W2_ADDR_0 + i, enaddr[i]);
}

int
ep_attach(struct ep_softc *sc)
{
	struct ifnet *ifp = NULL;
	struct ifmedia *ifm = NULL;
	int error;

	sc->gone = 0;
	EP_LOCK_INIT(sc);
	if (! (sc->stat & F_ENADDR_SKIP)) {
		error = ep_get_macaddr(sc, sc->eaddr);
		if (error) {
			device_printf(sc->dev, "Unable to get MAC address!\n");
			EP_LOCK_DESTROY(sc);
			return (ENXIO);
		}
	}
	ep_setup_station(sc, sc->eaddr);
	ifp = sc->ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(sc->dev, "if_alloc() failed\n");
		EP_LOCK_DESTROY(sc);
		return (ENOSPC);
	}

	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(sc->dev), device_get_unit(sc->dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_start = epstart;
	ifp->if_ioctl = epioctl;
	ifp->if_init = epinit;
	IFQ_SET_MAXLEN(&ifp->if_snd, ifqmaxlen);
	ifp->if_snd.ifq_drv_maxlen = ifqmaxlen;
	IFQ_SET_READY(&ifp->if_snd);

	callout_init_mtx(&sc->watchdog_timer, &sc->sc_mtx, 0);
	if (!sc->epb.mii_trans) {
		ifmedia_init(&sc->ifmedia, 0, ep_ifmedia_upd, ep_ifmedia_sts);

		if (sc->ep_connectors & AUI)
			ifmedia_add(&sc->ifmedia,
			    IFM_ETHER | IFM_10_5, 0, NULL);
		if (sc->ep_connectors & UTP)
			ifmedia_add(&sc->ifmedia,
			    IFM_ETHER | IFM_10_T, 0, NULL);
		if (sc->ep_connectors & BNC)
			ifmedia_add(&sc->ifmedia,
			    IFM_ETHER | IFM_10_2, 0, NULL);
		if (!sc->ep_connectors)
			ifmedia_add(&sc->ifmedia,
			    IFM_ETHER | IFM_NONE, 0, NULL);

		ifmedia_set(&sc->ifmedia,
		    IFM_ETHER | ep_media2if_media[sc->ep_connector]);

		ifm = &sc->ifmedia;
		ifm->ifm_media = ifm->ifm_cur->ifm_media;
		ep_ifmedia_upd(ifp);
	}
	ether_ifattach(ifp, sc->eaddr);

#ifdef EP_LOCAL_STATS
	sc->rx_no_first = sc->rx_no_mbuf = sc->rx_bpf_disc =
	    sc->rx_overrunf = sc->rx_overrunl = sc->tx_underrun = 0;
#endif
	EP_FSET(sc, F_RX_FIRST);
	sc->top = sc->mcur = 0;

	EP_LOCK(sc);
	epstop(sc);
	EP_UNLOCK(sc);

	gone_by_fcp101_dev(sc->dev);

	return (0);
}

int
ep_detach(device_t dev)
{
	struct ep_softc *sc;
	struct ifnet *ifp;

	sc = device_get_softc(dev);
	ifp = sc->ifp;
	EP_ASSERT_UNLOCKED(sc);
	EP_LOCK(sc);
	if (bus_child_present(dev))
		epstop(sc);
	sc->gone = 1;
	ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	EP_UNLOCK(sc);
	ether_ifdetach(ifp);
	callout_drain(&sc->watchdog_timer);
	ep_free(dev);

	if_free(ifp);
	EP_LOCK_DESTROY(sc);

	return (0);
}

static void
epinit(void *xsc)
{
	struct ep_softc *sc = xsc;
	EP_LOCK(sc);
	epinit_locked(sc);
	EP_UNLOCK(sc);
}

/*
 * The order in here seems important. Otherwise we may not receive
 * interrupts. ?!
 */
static void
epinit_locked(struct ep_softc *sc)
{
	struct ifnet *ifp = sc->ifp;
	int i;

	if (sc->gone)
		return;

	EP_ASSERT_LOCKED(sc);
	EP_BUSY_WAIT(sc);

	GO_WINDOW(sc, 0);
	CSR_WRITE_2(sc, EP_COMMAND, STOP_TRANSCEIVER);
	GO_WINDOW(sc, 4);
	CSR_WRITE_2(sc, EP_W4_MEDIA_TYPE, DISABLE_UTP);
	GO_WINDOW(sc, 0);

	/* Disable the card */
	CSR_WRITE_2(sc, EP_W0_CONFIG_CTRL, 0);

	/* Enable the card */
	CSR_WRITE_2(sc, EP_W0_CONFIG_CTRL, ENABLE_DRQ_IRQ);

	GO_WINDOW(sc, 2);
	/* Reload the ether_addr. */
	ep_setup_station(sc, IF_LLADDR(sc->ifp));

	CSR_WRITE_2(sc, EP_COMMAND, RX_RESET);
	CSR_WRITE_2(sc, EP_COMMAND, TX_RESET);
	EP_BUSY_WAIT(sc);

	/* Window 1 is operating window */
	GO_WINDOW(sc, 1);
	for (i = 0; i < 31; i++)
		CSR_READ_1(sc, EP_W1_TX_STATUS);

	/* get rid of stray intr's */
	CSR_WRITE_2(sc, EP_COMMAND, ACK_INTR | 0xff);

	CSR_WRITE_2(sc, EP_COMMAND, SET_RD_0_MASK | S_5_INTS);
	CSR_WRITE_2(sc, EP_COMMAND, SET_INTR_MASK | S_5_INTS);

	if (ifp->if_flags & IFF_PROMISC)
		CSR_WRITE_2(sc, EP_COMMAND, SET_RX_FILTER | FIL_INDIVIDUAL |
		    FIL_MULTICAST | FIL_BRDCST | FIL_PROMISC);
	else
		CSR_WRITE_2(sc, EP_COMMAND, SET_RX_FILTER | FIL_INDIVIDUAL |
		    FIL_MULTICAST | FIL_BRDCST);

	if (!sc->epb.mii_trans)
		ep_ifmedia_upd(ifp);

	if (sc->stat & F_HAS_TX_PLL)
		CSR_WRITE_2(sc, EP_COMMAND, TX_PLL_ENABLE);
	CSR_WRITE_2(sc, EP_COMMAND, RX_ENABLE);
	CSR_WRITE_2(sc, EP_COMMAND, TX_ENABLE);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;	/* just in case */

#ifdef EP_LOCAL_STATS
	sc->rx_no_first = sc->rx_no_mbuf =
	    sc->rx_overrunf = sc->rx_overrunl = sc->tx_underrun = 0;
#endif
	EP_FSET(sc, F_RX_FIRST);
	if (sc->top) {
		m_freem(sc->top);
		sc->top = sc->mcur = 0;
	}
	CSR_WRITE_2(sc, EP_COMMAND, SET_RX_EARLY_THRESH | RX_INIT_EARLY_THRESH);
	CSR_WRITE_2(sc, EP_COMMAND, SET_TX_START_THRESH | 16);

	GO_WINDOW(sc, 1);
	epstart_locked(ifp);
	callout_reset(&sc->watchdog_timer, hz, eptick, sc);
}

static void
epstart(struct ifnet *ifp)
{
	struct ep_softc *sc;
	sc = ifp->if_softc;
	EP_LOCK(sc);
	epstart_locked(ifp);
	EP_UNLOCK(sc);
}
	
static void
epstart_locked(struct ifnet *ifp)
{
	struct ep_softc *sc;
	u_int len;
	struct mbuf *m, *m0;
	int pad, started;

	sc = ifp->if_softc;
	if (sc->gone)
		return;
	EP_ASSERT_LOCKED(sc);
	EP_BUSY_WAIT(sc);
	if (ifp->if_drv_flags & IFF_DRV_OACTIVE)
		return;
	started = 0;
startagain:
	/* Sneak a peek at the next packet */
	IFQ_DRV_DEQUEUE(&ifp->if_snd, m0);
	if (m0 == NULL)
		return;
	if (!started && (sc->stat & F_HAS_TX_PLL))
		CSR_WRITE_2(sc, EP_COMMAND, TX_PLL_ENABLE);
	started++;
	for (len = 0, m = m0; m != NULL; m = m->m_next)
		len += m->m_len;

	pad = (4 - len) & 3;

	/*
	 * The 3c509 automatically pads short packets to minimum
	 * ethernet length, but we drop packets that are too large.
	 * Perhaps we should truncate them instead?
	 */
	if (len + pad > ETHER_MAX_LEN) {
		/* packet is obviously too large: toss it */
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		m_freem(m0);
		goto readcheck;
	}
	if (CSR_READ_2(sc, EP_W1_FREE_TX) < len + pad + 4) {
		/* no room in FIFO */
		CSR_WRITE_2(sc, EP_COMMAND, SET_TX_AVAIL_THRESH | (len + pad + 4));
		/* make sure */
		if (CSR_READ_2(sc, EP_W1_FREE_TX) < len + pad + 4) {
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			IFQ_DRV_PREPEND(&ifp->if_snd, m0);
			goto done;
		}
	} else
		CSR_WRITE_2(sc, EP_COMMAND,
		    SET_TX_AVAIL_THRESH | EP_THRESH_DISABLE);

	CSR_WRITE_2(sc, EP_W1_TX_PIO_WR_1, len);
	/* Second dword meaningless */
	CSR_WRITE_2(sc, EP_W1_TX_PIO_WR_1, 0x0);

	for (m = m0; m != NULL; m = m->m_next) {
		if (m->m_len > 1)
			CSR_WRITE_MULTI_2(sc, EP_W1_TX_PIO_WR_1,
			    mtod(m, uint16_t *), m->m_len / 2);
		if (m->m_len & 1)
			CSR_WRITE_1(sc, EP_W1_TX_PIO_WR_1,
			    *(mtod(m, uint8_t *)+m->m_len - 1));
	}

	while (pad--)
		CSR_WRITE_1(sc, EP_W1_TX_PIO_WR_1, 0);	/* Padding */

	/* XXX and drop splhigh here */

	BPF_MTAP(ifp, m0);

	sc->tx_timer = 2;
	if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
	m_freem(m0);

	/*
	 * Is another packet coming in? We don't want to overflow
	 * the tiny RX fifo.
	 */
readcheck:
	if (CSR_READ_2(sc, EP_W1_RX_STATUS) & RX_BYTES_MASK) {
		/*
		 * we check if we have packets left, in that case
		 * we prepare to come back later
		 */
		if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
			CSR_WRITE_2(sc, EP_COMMAND, SET_TX_AVAIL_THRESH | 8);
		goto done;
	}
	goto startagain;
done:;
	return;
}

void
ep_intr(void *arg)
{
	struct ep_softc *sc;

	sc = (struct ep_softc *) arg;
	EP_LOCK(sc);
	ep_intr_locked(sc);
	EP_UNLOCK(sc);
}

static void
ep_intr_locked(struct ep_softc *sc)
{
	int status;
	struct ifnet *ifp;

	/* XXX 4.x splbio'd here to reduce interruptability */

	/*
	 * quick fix: Try to detect an interrupt when the card goes away.
	 */
	if (sc->gone || CSR_READ_2(sc, EP_STATUS) == 0xffff)
		return;
	ifp = sc->ifp;

	CSR_WRITE_2(sc, EP_COMMAND, SET_INTR_MASK);	/* disable all Ints */

rescan:

	while ((status = CSR_READ_2(sc, EP_STATUS)) & S_5_INTS) {

		/* first acknowledge all interrupt sources */
		CSR_WRITE_2(sc, EP_COMMAND, ACK_INTR | (status & S_MASK));

		if (status & (S_RX_COMPLETE | S_RX_EARLY))
			epread(sc);
		if (status & S_TX_AVAIL) {
			/* we need ACK */
			sc->tx_timer = 0;
			ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
			GO_WINDOW(sc, 1);
			CSR_READ_2(sc, EP_W1_FREE_TX);
			epstart_locked(ifp);
		}
		if (status & S_CARD_FAILURE) {
			sc->tx_timer = 0;
#ifdef EP_LOCAL_STATS
			device_printf(sc->dev, "\n\tStatus: %x\n", status);
			GO_WINDOW(sc, 4);
			printf("\tFIFO Diagnostic: %x\n",
			    CSR_READ_2(sc, EP_W4_FIFO_DIAG));
			printf("\tStat: %x\n", sc->stat);
			printf("\tIpackets=%d, Opackets=%d\n",
			    ifp->if_get_counter(ifp, IFCOUNTER_IPACKETS),
			    ifp->if_get_counter(ifp, IFCOUNTER_OPACKETS));
			printf("\tNOF=%d, NOMB=%d, RXOF=%d, RXOL=%d, TXU=%d\n",
			    sc->rx_no_first, sc->rx_no_mbuf, sc->rx_overrunf,
			    sc->rx_overrunl, sc->tx_underrun);
#else

#ifdef DIAGNOSTIC
			device_printf(sc->dev,
			    "Status: %x (input buffer overflow)\n", status);
#else
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
#endif

#endif
			epinit_locked(sc);
			return;
		}
		if (status & S_TX_COMPLETE) {
			sc->tx_timer = 0;
			/*
			 * We need ACK. We do it at the end.
			 *
		         * We need to read TX_STATUS until we get a
			 * 0 status in order to turn off the interrupt flag.
		         */
			while ((status = CSR_READ_1(sc, EP_W1_TX_STATUS)) &
			    TXS_COMPLETE) {
				if (status & TXS_SUCCES_INTR_REQ)
					;	/* nothing */
				else if (status &
				    (TXS_UNDERRUN | TXS_JABBER |
				    TXS_MAX_COLLISION)) {
					CSR_WRITE_2(sc, EP_COMMAND, TX_RESET);
					if (status & TXS_UNDERRUN) {
#ifdef EP_LOCAL_STATS
						sc->tx_underrun++;
#endif
					}
					if (status & TXS_MAX_COLLISION) {
						/*
						 * TXS_MAX_COLLISION we
						 * shouldn't get here
						 */
						if_inc_counter(ifp, IFCOUNTER_COLLISIONS, 1);
					}
					if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
					CSR_WRITE_2(sc, EP_COMMAND, TX_ENABLE);
					/*
				         * To have a tx_avail_int but giving
					 * the chance to the Reception
				         */
					if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
						CSR_WRITE_2(sc, EP_COMMAND,
						    SET_TX_AVAIL_THRESH | 8);
				}
				/* pops up the next status */
				CSR_WRITE_1(sc, EP_W1_TX_STATUS, 0x0);
			}	/* while */
			ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
			GO_WINDOW(sc, 1);
			CSR_READ_2(sc, EP_W1_FREE_TX);
			epstart_locked(ifp);
		}	/* end TX_COMPLETE */
	}

	CSR_WRITE_2(sc, EP_COMMAND, C_INTR_LATCH);	/* ACK int Latch */

	if ((status = CSR_READ_2(sc, EP_STATUS)) & S_5_INTS)
		goto rescan;

	/* re-enable Ints */
	CSR_WRITE_2(sc, EP_COMMAND, SET_INTR_MASK | S_5_INTS);
}

static void
epread(struct ep_softc *sc)
{
	struct mbuf *top, *mcur, *m;
	struct ifnet *ifp;
	int lenthisone;
	short rx_fifo2, status;
	short rx_fifo;

/* XXX Must be called with sc locked */

	ifp = sc->ifp;
	status = CSR_READ_2(sc, EP_W1_RX_STATUS);

read_again:

	if (status & ERR_RX) {
		if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		if (status & ERR_RX_OVERRUN) {
			/*
		         * We can think the rx latency is actually
			 * greather than we expect
		         */
#ifdef EP_LOCAL_STATS
			if (EP_FTST(sc, F_RX_FIRST))
				sc->rx_overrunf++;
			else
				sc->rx_overrunl++;
#endif
		}
		goto out;
	}
	rx_fifo = rx_fifo2 = status & RX_BYTES_MASK;

	if (EP_FTST(sc, F_RX_FIRST)) {
		MGETHDR(m, M_NOWAIT, MT_DATA);
		if (!m)
			goto out;
		if (rx_fifo >= MINCLSIZE)
			MCLGET(m, M_NOWAIT);
		sc->top = sc->mcur = top = m;
#define EROUND  ((sizeof(struct ether_header) + 3) & ~3)
#define EOFF    (EROUND - sizeof(struct ether_header))
		top->m_data += EOFF;

		/* Read what should be the header. */
		CSR_READ_MULTI_2(sc, EP_W1_RX_PIO_RD_1,
		    mtod(top, uint16_t *), sizeof(struct ether_header) / 2);
		top->m_len = sizeof(struct ether_header);
		rx_fifo -= sizeof(struct ether_header);
		sc->cur_len = rx_fifo2;
	} else {
		/* come here if we didn't have a complete packet last time */
		top = sc->top;
		m = sc->mcur;
		sc->cur_len += rx_fifo2;
	}

	/* Reads what is left in the RX FIFO */
	while (rx_fifo > 0) {
		lenthisone = min(rx_fifo, M_TRAILINGSPACE(m));
		if (lenthisone == 0) {	/* no room in this one */
			mcur = m;
			MGET(m, M_NOWAIT, MT_DATA);
			if (!m)
				goto out;
			if (rx_fifo >= MINCLSIZE)
				MCLGET(m, M_NOWAIT);
			m->m_len = 0;
			mcur->m_next = m;
			lenthisone = min(rx_fifo, M_TRAILINGSPACE(m));
		}
		CSR_READ_MULTI_2(sc, EP_W1_RX_PIO_RD_1,
		    (uint16_t *)(mtod(m, caddr_t)+m->m_len),
		    lenthisone / 2);
		m->m_len += lenthisone;
		if (lenthisone & 1)
			*(mtod(m, caddr_t)+m->m_len - 1) =
			    CSR_READ_1(sc, EP_W1_RX_PIO_RD_1);
		rx_fifo -= lenthisone;
	}

	if (status & ERR_RX_INCOMPLETE) {
		/* we haven't received the complete packet */
		sc->mcur = m;
#ifdef EP_LOCAL_STATS
		/* to know how often we come here */
		sc->rx_no_first++;
#endif
		EP_FRST(sc, F_RX_FIRST);
		status = CSR_READ_2(sc, EP_W1_RX_STATUS);
		if (!(status & ERR_RX_INCOMPLETE)) {
			/*
			 * We see if by now, the packet has completly
			 * arrived
			 */
			goto read_again;
		}
		CSR_WRITE_2(sc, EP_COMMAND,
		    SET_RX_EARLY_THRESH | RX_NEXT_EARLY_THRESH);
		return;
	}
	CSR_WRITE_2(sc, EP_COMMAND, RX_DISCARD_TOP_PACK);
	if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
	EP_FSET(sc, F_RX_FIRST);
	top->m_pkthdr.rcvif = sc->ifp;
	top->m_pkthdr.len = sc->cur_len;

	/*
	 * Drop locks before calling if_input() since it may re-enter
	 * ep_start() in the netisr case.  This would result in a
	 * lock reversal.  Better performance might be obtained by
	 * chaining all packets received, dropping the lock, and then
	 * calling if_input() on each one.
	 */
	EP_UNLOCK(sc);
	(*ifp->if_input) (ifp, top);
	EP_LOCK(sc);
	sc->top = 0;
	EP_BUSY_WAIT(sc);
	CSR_WRITE_2(sc, EP_COMMAND, SET_RX_EARLY_THRESH | RX_INIT_EARLY_THRESH);
	return;

out:
	CSR_WRITE_2(sc, EP_COMMAND, RX_DISCARD_TOP_PACK);
	if (sc->top) {
		m_freem(sc->top);
		sc->top = 0;
#ifdef EP_LOCAL_STATS
		sc->rx_no_mbuf++;
#endif
	}
	EP_FSET(sc, F_RX_FIRST);
	EP_BUSY_WAIT(sc);
	CSR_WRITE_2(sc, EP_COMMAND, SET_RX_EARLY_THRESH | RX_INIT_EARLY_THRESH);
}

static int
ep_ifmedia_upd(struct ifnet *ifp)
{
	struct ep_softc *sc = ifp->if_softc;
	int i = 0, j;

	GO_WINDOW(sc, 0);
	CSR_WRITE_2(sc, EP_COMMAND, STOP_TRANSCEIVER);
	GO_WINDOW(sc, 4);
	CSR_WRITE_2(sc, EP_W4_MEDIA_TYPE, DISABLE_UTP);
	GO_WINDOW(sc, 0);

	switch (IFM_SUBTYPE(sc->ifmedia.ifm_media)) {
	case IFM_10_T:
		if (sc->ep_connectors & UTP) {
			i = ACF_CONNECTOR_UTP;
			GO_WINDOW(sc, 4);
			CSR_WRITE_2(sc, EP_W4_MEDIA_TYPE, ENABLE_UTP);
		}
		break;
	case IFM_10_2:
		if (sc->ep_connectors & BNC) {
			i = ACF_CONNECTOR_BNC;
			CSR_WRITE_2(sc, EP_COMMAND, START_TRANSCEIVER);
			DELAY(DELAY_MULTIPLE * 1000);
		}
		break;
	case IFM_10_5:
		if (sc->ep_connectors & AUI)
			i = ACF_CONNECTOR_AUI;
		break;
	default:
		i = sc->ep_connector;
		device_printf(sc->dev,
		    "strange connector type in EEPROM: assuming AUI\n");
	}

	GO_WINDOW(sc, 0);
	j = CSR_READ_2(sc, EP_W0_ADDRESS_CFG) & 0x3fff;
	CSR_WRITE_2(sc, EP_W0_ADDRESS_CFG, j | (i << ACF_CONNECTOR_BITS));

	return (0);
}

static void
ep_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct ep_softc *sc = ifp->if_softc;
	uint16_t ms;

	switch (IFM_SUBTYPE(sc->ifmedia.ifm_media)) {
	case IFM_10_T:
		GO_WINDOW(sc, 4);
		ms = CSR_READ_2(sc, EP_W4_MEDIA_TYPE);
		GO_WINDOW(sc, 0);
		ifmr->ifm_status = IFM_AVALID;
		if (ms & MT_LB) {
			ifmr->ifm_status |= IFM_ACTIVE;
			ifmr->ifm_active = IFM_ETHER | IFM_10_T;
		} else {
			ifmr->ifm_active = IFM_ETHER | IFM_NONE;
		}
		break;
	default:
		ifmr->ifm_active = sc->ifmedia.ifm_media;
		break;
	}
}

static int
epioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct ep_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *) data;
	int error = 0;

	switch (cmd) {
	case SIOCSIFFLAGS:
		EP_LOCK(sc);
		if (((ifp->if_flags & IFF_UP) == 0) &&
		    (ifp->if_drv_flags & IFF_DRV_RUNNING)) {
			epstop(sc);
		} else
			/* reinitialize card on any parameter change */
			epinit_locked(sc);
		EP_UNLOCK(sc);
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		/*
		 * The Etherlink III has no programmable multicast
		 * filter.  We always initialize the card to be
		 * promiscuous to multicast, since we're always a
		 * member of the ALL-SYSTEMS group, so there's no
		 * need to process SIOC*MULTI requests.
		 */
		error = 0;
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		if (!sc->epb.mii_trans)
			error = ifmedia_ioctl(ifp, ifr, &sc->ifmedia, cmd);
		else
			error = EINVAL;
		break;
	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}
	return (error);
}

static void
eptick(void *arg)
{
	struct ep_softc *sc;

	sc = arg;
	if (sc->tx_timer != 0 && --sc->tx_timer == 0)
		epwatchdog(sc);
	callout_reset(&sc->watchdog_timer, hz, eptick, sc);
}

static void
epwatchdog(struct ep_softc *sc)
{
	struct ifnet *ifp;

	ifp = sc->ifp;
	if (sc->gone)
		return;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	epstart_locked(ifp);
	ep_intr_locked(sc);
}

static void
epstop(struct ep_softc *sc)
{

	EP_ASSERT_LOCKED(sc);

	CSR_WRITE_2(sc, EP_COMMAND, RX_DISABLE);
	CSR_WRITE_2(sc, EP_COMMAND, RX_DISCARD_TOP_PACK);
	EP_BUSY_WAIT(sc);

	CSR_WRITE_2(sc, EP_COMMAND, TX_DISABLE);
	CSR_WRITE_2(sc, EP_COMMAND, STOP_TRANSCEIVER);
	DELAY(800);

	CSR_WRITE_2(sc, EP_COMMAND, RX_RESET);
	EP_BUSY_WAIT(sc);
	CSR_WRITE_2(sc, EP_COMMAND, TX_RESET);
	EP_BUSY_WAIT(sc);

	CSR_WRITE_2(sc, EP_COMMAND, C_INTR_LATCH);
	CSR_WRITE_2(sc, EP_COMMAND, SET_RD_0_MASK);
	CSR_WRITE_2(sc, EP_COMMAND, SET_INTR_MASK);
	CSR_WRITE_2(sc, EP_COMMAND, SET_RX_FILTER);

	sc->ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	callout_stop(&sc->watchdog_timer);
}
