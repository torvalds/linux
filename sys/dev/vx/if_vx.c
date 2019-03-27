/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
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
 *
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Created from if_ep.c driver by Fred Gray (fgray@rice.edu) to support
 * the 3c590 family.
 */

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


#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_var.h>

#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_types.h>

#include <machine/bus.h>

#include <sys/bus.h>

#include <net/bpf.h>

#include <dev/vx/if_vxreg.h>
#include <dev/vx/if_vxvar.h>

#define ETHER_MAX_LEN	1518
#define ETHER_ADDR_LEN	6
#define ETHER_ALIGN 	2

static struct connector_entry {
	int bit;
	char *name;
} conn_tab[VX_CONNECTORS] = {

#define CONNECTOR_UTP	0
	{
		0x08, "utp"
	},
#define CONNECTOR_AUI	1
	{
		0x20, "aui"
	},
/* dummy */
	{
		0, "???"
	},
#define CONNECTOR_BNC	3
	{
		0x10, "bnc"
	},
#define CONNECTOR_TX	4
	{
		0x02, "tx"
	},
#define CONNECTOR_FX	5
	{
		0x04, "fx"
	},
#define CONNECTOR_MII	6
	{
		0x40, "mii"
	},
	{
		0, "???"
	}
};

static void vx_txstat(struct vx_softc *);
static int vx_status(struct vx_softc *);
static void vx_init(void *);
static void vx_init_locked(struct vx_softc *);
static int vx_ioctl(struct ifnet *, u_long, caddr_t);
static void vx_start(struct ifnet *);
static void vx_start_locked(struct ifnet *);
static void vx_watchdog(void *);
static void vx_reset(struct vx_softc *);
static void vx_read(struct vx_softc *);
static struct mbuf *vx_get(struct vx_softc *, u_int);
static void vx_mbuf_fill(void *);
static void vx_mbuf_empty(struct vx_softc *);
static void vx_setfilter(struct vx_softc *);
static void vx_getlink(struct vx_softc *);
static void vx_setlink(struct vx_softc *);

int
vx_attach(device_t dev)
{
	struct vx_softc *sc = device_get_softc(dev);
	struct ifnet *ifp;
	int i;
	u_char eaddr[6];

	ifp = sc->vx_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "can not if_alloc()\n");
		return 0;
	}
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));

	mtx_init(&sc->vx_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	callout_init_mtx(&sc->vx_callout, &sc->vx_mtx, 0);
	callout_init_mtx(&sc->vx_watchdog, &sc->vx_mtx, 0);
	GO_WINDOW(0);
	CSR_WRITE_2(sc, VX_COMMAND, GLOBAL_RESET);
	VX_BUSY_WAIT;

	vx_getlink(sc);

	/*
         * Read the station address from the eeprom
         */
	GO_WINDOW(0);
	for (i = 0; i < 3; i++) {
		int x;

		if (vx_busy_eeprom(sc)) {
			mtx_destroy(&sc->vx_mtx);
			if_free(ifp);
			return 0;
		}
		CSR_WRITE_2(sc, VX_W0_EEPROM_COMMAND, EEPROM_CMD_RD
		    | (EEPROM_OEM_ADDR0 + i));
		if (vx_busy_eeprom(sc)) {
			mtx_destroy(&sc->vx_mtx);
			if_free(ifp);
			return 0;
		}
		x = CSR_READ_2(sc, VX_W0_EEPROM_DATA);
		eaddr[(i << 1)] = x >> 8;
		eaddr[(i << 1) + 1] = x;
	}

	ifp->if_snd.ifq_maxlen = ifqmaxlen;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_start = vx_start;
	ifp->if_ioctl = vx_ioctl;
	ifp->if_init = vx_init;
	ifp->if_softc = sc;

	ether_ifattach(ifp, eaddr);

	sc->vx_tx_start_thresh = 20;	/* probably a good starting point. */

	VX_LOCK(sc);
	vx_stop(sc);
	VX_UNLOCK(sc);

	gone_by_fcp101_dev(dev);

	return 1;
}

/*
 * The order in here seems important. Otherwise we may not receive
 * interrupts. ?!
 */
static void
vx_init(void *xsc)
{
	struct vx_softc *sc = (struct vx_softc *)xsc;

	VX_LOCK(sc);
	vx_init_locked(sc);
	VX_UNLOCK(sc);
}

static void
vx_init_locked(struct vx_softc *sc)
{
	struct ifnet *ifp = sc->vx_ifp;
	int i;

	VX_LOCK_ASSERT(sc);

	VX_BUSY_WAIT;

	GO_WINDOW(2);

	for (i = 0; i < 6; i++)	/* Reload the ether_addr. */
		CSR_WRITE_1(sc, VX_W2_ADDR_0 + i, IF_LLADDR(sc->vx_ifp)[i]);

	CSR_WRITE_2(sc, VX_COMMAND, RX_RESET);
	VX_BUSY_WAIT;
	CSR_WRITE_2(sc, VX_COMMAND, TX_RESET);
	VX_BUSY_WAIT;

	GO_WINDOW(1);		/* Window 1 is operating window */
	for (i = 0; i < 31; i++)
		CSR_READ_1(sc, VX_W1_TX_STATUS);

	CSR_WRITE_2(sc, VX_COMMAND, SET_RD_0_MASK | S_CARD_FAILURE |
	    S_RX_COMPLETE | S_TX_COMPLETE | S_TX_AVAIL);
	CSR_WRITE_2(sc, VX_COMMAND, SET_INTR_MASK | S_CARD_FAILURE |
	    S_RX_COMPLETE | S_TX_COMPLETE | S_TX_AVAIL);

	/*
         * Attempt to get rid of any stray interrupts that occurred during
         * configuration.  On the i386 this isn't possible because one may
         * already be queued.  However, a single stray interrupt is
         * unimportant.
         */
	CSR_WRITE_2(sc, VX_COMMAND, ACK_INTR | 0xff);

	vx_setfilter(sc);
	vx_setlink(sc);

	CSR_WRITE_2(sc, VX_COMMAND, RX_ENABLE);
	CSR_WRITE_2(sc, VX_COMMAND, TX_ENABLE);

	vx_mbuf_fill(sc);

	/* Interface is now `running', with no output active. */
	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	callout_reset(&sc->vx_watchdog, hz, vx_watchdog, sc);

	/* Attempt to start output, if any. */
	vx_start_locked(ifp);
}

static void
vx_setfilter(struct vx_softc *sc)
{
	struct ifnet *ifp = sc->vx_ifp;

	VX_LOCK_ASSERT(sc);
	GO_WINDOW(1);		/* Window 1 is operating window */
	CSR_WRITE_2(sc, VX_COMMAND, SET_RX_FILTER |
	    FIL_INDIVIDUAL | FIL_BRDCST | FIL_MULTICAST |
	    ((ifp->if_flags & IFF_PROMISC) ? FIL_PROMISC : 0));
}

static void
vx_getlink(struct vx_softc *sc)
{
	int n, k;

	GO_WINDOW(3);
	sc->vx_connectors = CSR_READ_2(sc, VX_W3_RESET_OPT) & 0x7f;
	for (n = 0, k = 0; k < VX_CONNECTORS; k++) {
		if (sc->vx_connectors & conn_tab[k].bit) {
			if (n > 0)
				printf("/");
			printf("%s", conn_tab[k].name);
			n++;
		}
	}
	if (sc->vx_connectors == 0) {
		printf("no connectors!\n");
		return;
	}
	GO_WINDOW(3);
	sc->vx_connector =
	    (CSR_READ_4(sc, VX_W3_INTERNAL_CFG) & INTERNAL_CONNECTOR_MASK)
	    >> INTERNAL_CONNECTOR_BITS;
	if (sc->vx_connector & 0x10) {
		sc->vx_connector &= 0x0f;
		printf("[*%s*]", conn_tab[(int)sc->vx_connector].name);
		printf(": disable 'auto select' with DOS util!\n");
	} else {
		printf("[*%s*]\n", conn_tab[(int)sc->vx_connector].name);
	}
}

static void
vx_setlink(struct vx_softc *sc)
{
	struct ifnet *ifp = sc->vx_ifp;
	int i, j, k;
	char *reason, *warning;
	static int prev_flags;
	static signed char prev_conn = -1;

	VX_LOCK_ASSERT(sc);
	if (prev_conn == -1)
		prev_conn = sc->vx_connector;

	/*
         * S.B.
         *
         * Now behavior was slightly changed:
         *
         * if any of flags link[0-2] is used and its connector is
         * physically present the following connectors are used:
         *
         *   link0 - AUI * highest precedence
         *   link1 - BNC
         *   link2 - UTP * lowest precedence
         *
         * If none of them is specified then
         * connector specified in the EEPROM is used
         * (if present on card or UTP if not).
         */
	i = sc->vx_connector;	/* default in EEPROM */
	reason = "default";
	warning = NULL;

	if (ifp->if_flags & IFF_LINK0) {
		if (sc->vx_connectors & conn_tab[CONNECTOR_AUI].bit) {
			i = CONNECTOR_AUI;
			reason = "link0";
		} else {
			warning = "aui not present! (link0)";
		}
	} else if (ifp->if_flags & IFF_LINK1) {
		if (sc->vx_connectors & conn_tab[CONNECTOR_BNC].bit) {
			i = CONNECTOR_BNC;
			reason = "link1";
		} else {
			warning = "bnc not present! (link1)";
		}
	} else if (ifp->if_flags & IFF_LINK2) {
		if (sc->vx_connectors & conn_tab[CONNECTOR_UTP].bit) {
			i = CONNECTOR_UTP;
			reason = "link2";
		} else {
			warning = "utp not present! (link2)";
		}
	} else if ((sc->vx_connectors & conn_tab[(int)sc->vx_connector].bit) == 0) {
		warning = "strange connector type in EEPROM.";
		reason = "forced";
		i = CONNECTOR_UTP;
	}
	/* Avoid unnecessary message. */
	k = (prev_flags ^ ifp->if_flags) & (IFF_LINK0 | IFF_LINK1 | IFF_LINK2);
	if ((k != 0) || (prev_conn != i)) {
		if (warning != NULL)
			if_printf(ifp, "warning: %s\n", warning);
		if_printf(ifp, "selected %s. (%s)\n", conn_tab[i].name, reason);
	}
	/* Set the selected connector. */
	GO_WINDOW(3);
	j = CSR_READ_4(sc, VX_W3_INTERNAL_CFG) & ~INTERNAL_CONNECTOR_MASK;
	CSR_WRITE_4(sc, VX_W3_INTERNAL_CFG, j | (i << INTERNAL_CONNECTOR_BITS));

	/* First, disable all. */
	CSR_WRITE_2(sc, VX_COMMAND, STOP_TRANSCEIVER);
	DELAY(800);
	GO_WINDOW(4);
	CSR_WRITE_2(sc, VX_W4_MEDIA_TYPE, 0);

	/* Second, enable the selected one. */
	switch (i) {
	case CONNECTOR_UTP:
		GO_WINDOW(4);
		CSR_WRITE_2(sc, VX_W4_MEDIA_TYPE, ENABLE_UTP);
		break;
	case CONNECTOR_BNC:
		CSR_WRITE_2(sc, VX_COMMAND, START_TRANSCEIVER);
		DELAY(800);
		break;
	case CONNECTOR_TX:
	case CONNECTOR_FX:
		GO_WINDOW(4);
		CSR_WRITE_2(sc, VX_W4_MEDIA_TYPE, LINKBEAT_ENABLE);
		break;
	default:		/* AUI and MII fall here */
		break;
	}
	GO_WINDOW(1);

	prev_flags = ifp->if_flags;
	prev_conn = i;
}

static void
vx_start(struct ifnet *ifp)
{
	struct vx_softc *sc = ifp->if_softc;

	VX_LOCK(sc);
	vx_start_locked(ifp);
	VX_UNLOCK(sc);
}

static void
vx_start_locked(struct ifnet *ifp)
{
	struct vx_softc *sc = ifp->if_softc;
	struct mbuf *m;
	int len, pad;

	VX_LOCK_ASSERT(sc);

	/* Don't transmit if interface is busy or not running */
	if ((sc->vx_ifp->if_drv_flags &
	    (IFF_DRV_RUNNING | IFF_DRV_OACTIVE)) != IFF_DRV_RUNNING)
		return;

startagain:
	/* Sneak a peek at the next packet */
	m = ifp->if_snd.ifq_head;
	if (m == NULL) {
		return;
	}
	/* We need to use m->m_pkthdr.len, so require the header */
	M_ASSERTPKTHDR(m);
	len = m->m_pkthdr.len;

	pad = (4 - len) & 3;

	/*
         * The 3c509 automatically pads short packets to minimum ethernet
	 * length, but we drop packets that are too large. Perhaps we should
	 * truncate them instead?
         */
	if (len + pad > ETHER_MAX_LEN) {
		/* packet is obviously too large: toss it */
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		IF_DEQUEUE(&ifp->if_snd, m);
		m_freem(m);
		goto readcheck;
	}
	VX_BUSY_WAIT;
	if (CSR_READ_2(sc, VX_W1_FREE_TX) < len + pad + 4) {
		CSR_WRITE_2(sc, VX_COMMAND,
		    SET_TX_AVAIL_THRESH | ((len + pad + 4) >> 2));
		/* not enough room in FIFO - make sure */
		if (CSR_READ_2(sc, VX_W1_FREE_TX) < len + pad + 4) {
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			sc->vx_timer = 1;
			return;
		}
	}
	CSR_WRITE_2(sc, VX_COMMAND, SET_TX_AVAIL_THRESH | (8188 >> 2));
	IF_DEQUEUE(&ifp->if_snd, m);
	if (m == NULL)		/* not really needed */
		return;

	VX_BUSY_WAIT;
	CSR_WRITE_2(sc, VX_COMMAND, SET_TX_START_THRESH |
	    ((len / 4 + sc->vx_tx_start_thresh) >> 2));

	BPF_MTAP(sc->vx_ifp, m);

	/*
         * Do the output at splhigh() so that an interrupt from another device
         * won't cause a FIFO underrun.
	 *
	 * XXX: Can't enforce that anymore.
         */

	CSR_WRITE_4(sc, VX_W1_TX_PIO_WR_1, len | TX_INDICATE);

	while (m) {
		if (m->m_len > 3)
			bus_space_write_multi_4(sc->vx_bst, sc->vx_bsh,
			    VX_W1_TX_PIO_WR_1, (u_int32_t *)mtod(m, caddr_t),
			    m->m_len / 4);
		if (m->m_len & 3)
			bus_space_write_multi_1(sc->vx_bst, sc->vx_bsh,
			    VX_W1_TX_PIO_WR_1,
			    mtod(m, caddr_t) + (m->m_len & ~3), m->m_len & 3);
		m = m_free(m);
	}
	while (pad--)
		CSR_WRITE_1(sc, VX_W1_TX_PIO_WR_1, 0);	/* Padding */

	if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
	sc->vx_timer = 1;

readcheck:
	if ((CSR_READ_2(sc, VX_W1_RX_STATUS) & ERR_INCOMPLETE) == 0) {
		/* We received a complete packet. */

		if ((CSR_READ_2(sc, VX_STATUS) & S_INTR_LATCH) == 0) {
			/*
		         * No interrupt, read the packet and continue
		         * Is this supposed to happen?  Is my motherboard
		         * completely busted?
		         */
			vx_read(sc);
		} else
			/*
			 * Got an interrupt, return so that it gets
			 * serviced.
			 */
			return;
	} else {
		/* Check if we are stuck and reset [see XXX comment] */
		if (vx_status(sc)) {
			if (ifp->if_flags & IFF_DEBUG)
				if_printf(ifp, "adapter reset\n");
			vx_reset(sc);
		}
	}

	goto startagain;
}

/*
 * XXX: The 3c509 card can get in a mode where both the fifo status bit
 *      FIFOS_RX_OVERRUN and the status bit ERR_INCOMPLETE are set
 *      We detect this situation and we reset the adapter.
 *      It happens at times when there is a lot of broadcast traffic
 *      on the cable (once in a blue moon).
 */
static int
vx_status(struct vx_softc *sc)
{
	struct ifnet *ifp;
	int fifost;

	VX_LOCK_ASSERT(sc);

	/*
         * Check the FIFO status and act accordingly
         */
	GO_WINDOW(4);
	fifost = CSR_READ_2(sc, VX_W4_FIFO_DIAG);
	GO_WINDOW(1);

	ifp = sc->vx_ifp;
	if (fifost & FIFOS_RX_UNDERRUN) {
		if (ifp->if_flags & IFF_DEBUG)
			if_printf(ifp, "RX underrun\n");
		vx_reset(sc);
		return 0;
	}
	if (fifost & FIFOS_RX_STATUS_OVERRUN) {
		if (ifp->if_flags & IFF_DEBUG)
			if_printf(ifp, "RX Status overrun\n");
		return 1;
	}
	if (fifost & FIFOS_RX_OVERRUN) {
		if (ifp->if_flags & IFF_DEBUG)
			if_printf(ifp, "RX overrun\n");
		return 1;
	}
	if (fifost & FIFOS_TX_OVERRUN) {
		if (ifp->if_flags & IFF_DEBUG)
			if_printf(ifp, "TX overrun\n");
		vx_reset(sc);
		return 0;
	}
	return 0;
}

static void
vx_txstat(struct vx_softc *sc)
{
	struct ifnet *ifp;
	int i;

	VX_LOCK_ASSERT(sc);

	/*
        * We need to read+write TX_STATUS until we get a 0 status
        * in order to turn off the interrupt flag.
        */
	ifp = sc->vx_ifp;
	while ((i = CSR_READ_1(sc, VX_W1_TX_STATUS)) & TXS_COMPLETE) {
		CSR_WRITE_1(sc, VX_W1_TX_STATUS, 0x0);

		if (i & TXS_JABBER) {
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			if (ifp->if_flags & IFF_DEBUG)
				if_printf(ifp, "jabber (%x)\n", i);
			vx_reset(sc);
		} else if (i & TXS_UNDERRUN) {
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			if (ifp->if_flags & IFF_DEBUG)
				if_printf(ifp, "fifo underrun (%x) @%d\n", i,
				    sc->vx_tx_start_thresh);
			if (sc->vx_tx_succ_ok < 100)
				sc->vx_tx_start_thresh =
				    min(ETHER_MAX_LEN,
					sc->vx_tx_start_thresh + 20);
			sc->vx_tx_succ_ok = 0;
			vx_reset(sc);
		} else if (i & TXS_MAX_COLLISION) {
			if_inc_counter(ifp, IFCOUNTER_COLLISIONS, 1);
			CSR_WRITE_2(sc, VX_COMMAND, TX_ENABLE);
			ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		} else
			sc->vx_tx_succ_ok = (sc->vx_tx_succ_ok + 1) & 127;
	}
}

void
vx_intr(void *voidsc)
{
	short status;
	struct vx_softc *sc = voidsc;
	struct ifnet *ifp = sc->vx_ifp;

	VX_LOCK(sc);
	for (;;) {
		CSR_WRITE_2(sc, VX_COMMAND, C_INTR_LATCH);

		status = CSR_READ_2(sc, VX_STATUS);

		if ((status & (S_TX_COMPLETE | S_TX_AVAIL |
		    S_RX_COMPLETE | S_CARD_FAILURE)) == 0)
			break;

		/*
		 * Acknowledge any interrupts.  It's important that we do this
		 * first, since there would otherwise be a race condition.
		 * Due to the i386 interrupt queueing, we may get spurious
		 * interrupts occasionally.
		 */
		CSR_WRITE_2(sc, VX_COMMAND, ACK_INTR | status);

		if (status & S_RX_COMPLETE)
			vx_read(sc);
		if (status & S_TX_AVAIL) {
			sc->vx_timer = 0;
			sc->vx_ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
			vx_start_locked(sc->vx_ifp);
		}
		if (status & S_CARD_FAILURE) {
			if_printf(ifp, "adapter failure (%x)\n", status);
			sc->vx_timer = 0;
			vx_reset(sc);
			break;
		}
		if (status & S_TX_COMPLETE) {
			sc->vx_timer = 0;
			vx_txstat(sc);
			vx_start_locked(ifp);
		}
	}
	VX_UNLOCK(sc);

	/* no more interrupts */
	return;
}

static void
vx_read(struct vx_softc *sc)
{
	struct ifnet *ifp = sc->vx_ifp;
	struct mbuf *m;
	struct ether_header *eh;
	u_int len;

	VX_LOCK_ASSERT(sc);
	len = CSR_READ_2(sc, VX_W1_RX_STATUS);
again:

	if (ifp->if_flags & IFF_DEBUG) {
		int err = len & ERR_MASK;
		char *s = NULL;

		if (len & ERR_INCOMPLETE)
			s = "incomplete packet";
		else if (err == ERR_OVERRUN)
			s = "packet overrun";
		else if (err == ERR_RUNT)
			s = "runt packet";
		else if (err == ERR_ALIGNMENT)
			s = "bad alignment";
		else if (err == ERR_CRC)
			s = "bad crc";
		else if (err == ERR_OVERSIZE)
			s = "oversized packet";
		else if (err == ERR_DRIBBLE)
			s = "dribble bits";

		if (s)
			if_printf(ifp, "%s\n", s);
	}
	if (len & ERR_INCOMPLETE)
		return;

	if (len & ERR_RX) {
		if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		goto abort;
	}
	len &= RX_BYTES_MASK;	/* Lower 11 bits = RX bytes. */

	/* Pull packet off interface. */
	m = vx_get(sc, len);
	if (m == NULL) {
		if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		goto abort;
	}
	if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);

	{
		struct mbuf *m0;

		m0 = m_devget(mtod(m, char *), m->m_pkthdr.len, ETHER_ALIGN,
		    ifp, NULL);
		if (m0 == NULL) {
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			goto abort;
		}
		m_freem(m);
		m = m0;
	}

	/* We assume the header fit entirely in one mbuf. */
	eh = mtod(m, struct ether_header *);

	/*
         * XXX: Some cards seem to be in promiscuous mode all the time.
         * we need to make sure we only get our own stuff always.
         * bleah!
         */

	if (!(ifp->if_flags & IFF_PROMISC)
	    && (eh->ether_dhost[0] & 1) == 0	/* !mcast and !bcast */
	    && bcmp(eh->ether_dhost, IF_LLADDR(sc->vx_ifp),
	    ETHER_ADDR_LEN) != 0) {
		m_freem(m);
		return;
	}
	VX_UNLOCK(sc);
	(*ifp->if_input)(ifp, m);
	VX_LOCK(sc);

	/*
        * In periods of high traffic we can actually receive enough
        * packets so that the fifo overrun bit will be set at this point,
        * even though we just read a packet. In this case we
        * are not going to receive any more interrupts. We check for
        * this condition and read again until the fifo is not full.
        * We could simplify this test by not using vx_status(), but
        * rechecking the RX_STATUS register directly. This test could
        * result in unnecessary looping in cases where there is a new
        * packet but the fifo is not full, but it will not fix the
        * stuck behavior.
        *
        * Even with this improvement, we still get packet overrun errors
        * which are hurting performance. Maybe when I get some more time
        * I'll modify vx_read() so that it can handle RX_EARLY interrupts.
        */
	if (vx_status(sc)) {
		len = CSR_READ_2(sc, VX_W1_RX_STATUS);
		/* Check if we are stuck and reset [see XXX comment] */
		if (len & ERR_INCOMPLETE) {
			if (ifp->if_flags & IFF_DEBUG)
				if_printf(ifp, "adapter reset\n");
			vx_reset(sc);
			return;
		}
		goto again;
	}
	return;

abort:
	CSR_WRITE_2(sc, VX_COMMAND, RX_DISCARD_TOP_PACK);
}

static struct mbuf *
vx_get(struct vx_softc *sc, u_int totlen)
{
	struct ifnet *ifp = sc->vx_ifp;
	struct mbuf *top, **mp, *m;
	int len;

	VX_LOCK_ASSERT(sc);
	m = sc->vx_mb[sc->vx_next_mb];
	sc->vx_mb[sc->vx_next_mb] = NULL;
	if (m == NULL) {
		MGETHDR(m, M_NOWAIT, MT_DATA);
		if (m == NULL)
			return NULL;
	} else {
		/* If the queue is no longer full, refill. */
		if (sc->vx_last_mb == sc->vx_next_mb &&
		    sc->vx_buffill_pending == 0) {
			callout_reset(&sc->vx_callout, hz / 100, vx_mbuf_fill,
			    sc);
			sc->vx_buffill_pending = 1;
		}
		/* Convert one of our saved mbuf's. */
		sc->vx_next_mb = (sc->vx_next_mb + 1) % MAX_MBS;
		m->m_data = m->m_pktdat;
		m->m_flags = M_PKTHDR;
		bzero(&m->m_pkthdr, sizeof(m->m_pkthdr));
	}
	m->m_pkthdr.rcvif = ifp;
	m->m_pkthdr.len = totlen;
	len = MHLEN;
	top = NULL;
	mp = &top;

	/*
         * We read the packet at splhigh() so that an interrupt from another
         * device doesn't cause the card's buffer to overflow while we're
         * reading it.  We may still lose packets at other times.
	 *
	 * XXX: Can't enforce this anymore.
         */

	/*
         * Since we don't set allowLargePackets bit in MacControl register,
         * we can assume that totlen <= 1500bytes.
         * The while loop will be performed iff we have a packet with
         * MLEN < m_len < MINCLSIZE.
         */
	while (totlen > 0) {
		if (top) {
			m = sc->vx_mb[sc->vx_next_mb];
			sc->vx_mb[sc->vx_next_mb] = NULL;
			if (m == NULL) {
				MGET(m, M_NOWAIT, MT_DATA);
				if (m == NULL) {
					m_freem(top);
					return NULL;
				}
			} else {
				sc->vx_next_mb = (sc->vx_next_mb + 1) % MAX_MBS;
			}
			len = MLEN;
		}
		if (totlen >= MINCLSIZE) {
			if (MCLGET(m, M_NOWAIT))
				len = MCLBYTES;
		}
		len = min(totlen, len);
		if (len > 3)
			bus_space_read_multi_4(sc->vx_bst, sc->vx_bsh,
			    VX_W1_RX_PIO_RD_1, mtod(m, u_int32_t *), len / 4);
		if (len & 3) {
			bus_space_read_multi_1(sc->vx_bst, sc->vx_bsh,
			    VX_W1_RX_PIO_RD_1, mtod(m, u_int8_t *) + (len & ~3),
			    len & 3);
		}
		m->m_len = len;
		totlen -= len;
		*mp = m;
		mp = &m->m_next;
	}

	CSR_WRITE_2(sc, VX_COMMAND, RX_DISCARD_TOP_PACK);

	return top;
}


static int
vx_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct vx_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *) data;
	int error = 0;

	switch (cmd) {
	case SIOCSIFFLAGS:
		VX_LOCK(sc);
		if ((ifp->if_flags & IFF_UP) == 0 &&
		    (ifp->if_drv_flags & IFF_DRV_RUNNING) != 0) {
			/*
	                 * If interface is marked up and it is stopped, then
	                 * start it.
	                 */
			vx_stop(sc);
			ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		} else if ((ifp->if_flags & IFF_UP) != 0 &&
		    (ifp->if_drv_flags & IFF_DRV_RUNNING) == 0) {
			/*
	                 * If interface is marked up and it is stopped, then
	                 * start it.
	                 */
			vx_init_locked(sc);
		} else {
			/*
	                 * deal with flags changes:
	                 * IFF_MULTICAST, IFF_PROMISC,
	                 * IFF_LINK0, IFF_LINK1,
	                 */
			vx_setfilter(sc);
			vx_setlink(sc);
		}
		VX_UNLOCK(sc);
		break;

	case SIOCSIFMTU:
		/*
	         * Set the interface MTU.
	         */
		VX_LOCK(sc);
		if (ifr->ifr_mtu > ETHERMTU) {
			error = EINVAL;
		} else {
			ifp->if_mtu = ifr->ifr_mtu;
		}
		VX_UNLOCK(sc);
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		/*
		 * Multicast list has changed; set the hardware filter
		 * accordingly.
		 */
		VX_LOCK(sc);
		vx_reset(sc);
		VX_UNLOCK(sc);
		error = 0;
		break;


	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}

	return (error);
}

static void
vx_reset(struct vx_softc *sc)
{

	VX_LOCK_ASSERT(sc);
	vx_stop(sc);
	vx_init_locked(sc);
}

static void
vx_watchdog(void *arg)
{
	struct vx_softc *sc;
	struct ifnet *ifp;

	sc = arg;
	VX_LOCK_ASSERT(sc);
	callout_reset(&sc->vx_watchdog, hz, vx_watchdog, sc);
	if (sc->vx_timer == 0 || --sc->vx_timer > 0)
		return;

	ifp = sc->vx_ifp;
	if (ifp->if_flags & IFF_DEBUG)
		if_printf(ifp, "device timeout\n");
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	vx_start_locked(ifp);
	vx_intr(sc);
}

void
vx_stop(struct vx_softc *sc)
{

	VX_LOCK_ASSERT(sc);
	sc->vx_timer = 0;
	callout_stop(&sc->vx_watchdog);

	CSR_WRITE_2(sc, VX_COMMAND, RX_DISABLE);
	CSR_WRITE_2(sc, VX_COMMAND, RX_DISCARD_TOP_PACK);
	VX_BUSY_WAIT;
	CSR_WRITE_2(sc, VX_COMMAND, TX_DISABLE);
	CSR_WRITE_2(sc, VX_COMMAND, STOP_TRANSCEIVER);
	DELAY(800);
	CSR_WRITE_2(sc, VX_COMMAND, RX_RESET);
	VX_BUSY_WAIT;
	CSR_WRITE_2(sc, VX_COMMAND, TX_RESET);
	VX_BUSY_WAIT;
	CSR_WRITE_2(sc, VX_COMMAND, C_INTR_LATCH);
	CSR_WRITE_2(sc, VX_COMMAND, SET_RD_0_MASK);
	CSR_WRITE_2(sc, VX_COMMAND, SET_INTR_MASK);
	CSR_WRITE_2(sc, VX_COMMAND, SET_RX_FILTER);

	vx_mbuf_empty(sc);
}

int
vx_busy_eeprom(struct vx_softc *sc)
{
	int j, i = 100;

	while (i--) {
		j = CSR_READ_2(sc, VX_W0_EEPROM_COMMAND);
		if (j & EEPROM_BUSY)
			DELAY(100);
		else
			break;
	}
	if (!i) {
		if_printf(sc->vx_ifp, "eeprom failed to come ready\n");
		return (1);
	}
	return (0);
}

static void
vx_mbuf_fill(void *sp)
{
	struct vx_softc *sc = (struct vx_softc *)sp;
	int i;

	VX_LOCK_ASSERT(sc);
	i = sc->vx_last_mb;
	do {
		if (sc->vx_mb[i] == NULL)
			MGET(sc->vx_mb[i], M_NOWAIT, MT_DATA);
		if (sc->vx_mb[i] == NULL)
			break;
		i = (i + 1) % MAX_MBS;
	} while (i != sc->vx_next_mb);
	sc->vx_last_mb = i;
	/* If the queue was not filled, try again. */
	if (sc->vx_last_mb != sc->vx_next_mb) {
		callout_reset(&sc->vx_callout, hz / 100, vx_mbuf_fill, sc);
		sc->vx_buffill_pending = 1;
	} else {
		sc->vx_buffill_pending = 0;
	}
}

static void
vx_mbuf_empty(struct vx_softc *sc)
{
	int i;

	VX_LOCK_ASSERT(sc);
	for (i = 0; i < MAX_MBS; i++) {
		if (sc->vx_mb[i]) {
			m_freem(sc->vx_mb[i]);
			sc->vx_mb[i] = NULL;
		}
	}
	sc->vx_last_mb = sc->vx_next_mb = 0;
	if (sc->vx_buffill_pending != 0)
		callout_stop(&sc->vx_callout);
}
