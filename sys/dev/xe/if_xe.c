/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD AND BSD-3-Clause
 *
 * Copyright (c) 1998, 1999, 2003  Scott Mitchell
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
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*-
 * Portions of this software were derived from Werner Koch's xirc2ps driver
 * for Linux under the terms of the following license (from v1.30 of the
 * xirc2ps driver):
 *
 * Copyright (c) 1997 by Werner Koch (dd9jn)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, and the entire permission notice in its entirety,
 *    including the disclaimer of warranties.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.	IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*		
 * FreeBSD device driver for Xircom CreditCard PCMCIA Ethernet adapters.  The
 * following cards are currently known to work with the driver:
 *   Xircom CreditCard 10/100 (CE3)
 *   Xircom CreditCard Ethernet + Modem 28 (CEM28)
 *   Xircom CreditCard Ethernet 10/100 + Modem 56 (CEM56)
 *   Xircom RealPort Ethernet 10
 *   Xircom RealPort Ethernet 10/100
 *   Xircom RealPort Ethernet 10/100 + Modem 56 (REM56, REM56G)
 *   Intel EtherExpress Pro/100 PC Card Mobile Adapter 16 (Pro/100 M16A)
 *   Compaq Netelligent 10/100 PC Card (CPQ-10/100)
 *
 * Some other cards *should* work, but support for them is either broken or in 
 * an unknown state at the moment.  I'm always interested in hearing from
 * people who own any of these cards:
 *   Xircom CreditCard 10Base-T (PS-CE2-10)
 *   Xircom CreditCard Ethernet + ModemII (CEM2)
 *   Xircom CEM28 and CEM33 Ethernet/Modem cards (may be variants of CEM2?)
 *
 * Thanks to all who assisted with the development and testing of the driver,
 * especially: Werner Koch, Duke Kamstra, Duncan Barclay, Jason George, Dru
 * Nelson, Mike Kephart, Bill Rainey and Douglas Rand.  Apologies if I've left
 * out anyone who deserves a mention here.
 *
 * Special thanks to Ade Lovett for both hosting the mailing list and doing
 * the CEM56/REM56 support code; and the FreeBSD UK Users' Group for hosting
 * the web pages.
 *
 * Author email: <scott@uk.freebsd.org>
 * Driver web page: http://ukug.uk.freebsd.org/~scott/xe_drv/
 */


#include <sys/param.h>
#include <sys/cdefs.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/sysctl.h>

#include <sys/module.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>
 
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_mib.h>
#include <net/bpf.h>
#include <net/if_types.h>

#include <dev/xe/if_xereg.h>
#include <dev/xe/if_xevar.h>

/*
 * MII command structure
 */
struct xe_mii_frame {
	uint8_t		mii_stdelim;
	uint8_t		mii_opcode;
	uint8_t		mii_phyaddr;
	uint8_t		mii_regaddr;
	uint8_t		mii_turnaround;
	uint16_t	mii_data;
};

/*
 * Media autonegotiation progress constants
 */
#define	XE_AUTONEG_NONE		0	/* No autonegotiation in progress */
#define	XE_AUTONEG_WAITING	1	/* Waiting for transmitter to go idle */
#define	XE_AUTONEG_STARTED	2	/* Waiting for autonegotiation to complete */
#define	XE_AUTONEG_100TX	3	/* Trying to force 100baseTX link */
#define	XE_AUTONEG_FAIL		4	/* Autonegotiation failed */

/*
 * Prototypes start here
 */
static void	xe_init(void *xscp);
static void	xe_init_locked(struct xe_softc *scp);
static void	xe_start(struct ifnet *ifp);
static void	xe_start_locked(struct ifnet *ifp);
static int	xe_ioctl(struct ifnet *ifp, u_long command, caddr_t data);
static void	xe_watchdog(void *arg);
static void	xe_intr(void *xscp);
static void	xe_txintr(struct xe_softc *scp, uint8_t txst1);
static void	xe_macintr(struct xe_softc *scp, uint8_t rst0, uint8_t txst0,
		    uint8_t txst1);
static void	xe_rxintr(struct xe_softc *scp, uint8_t rst0);
static int	xe_media_change(struct ifnet *ifp);
static void	xe_media_status(struct ifnet *ifp, struct ifmediareq *mrp);
static void	xe_setmedia(void *arg);
static void	xe_reset(struct xe_softc *scp);
static void	xe_enable_intr(struct xe_softc *scp);
static void	xe_disable_intr(struct xe_softc *scp);
static void	xe_set_multicast(struct xe_softc *scp);
static void	xe_set_addr(struct xe_softc *scp, uint8_t* addr, unsigned idx);
static void	xe_mchash(struct xe_softc *scp, const uint8_t *addr);
static int	xe_pio_write_packet(struct xe_softc *scp, struct mbuf *mbp);

/*
 * MII functions
 */
static void	xe_mii_sync(struct xe_softc *scp);
static int	xe_mii_init(struct xe_softc *scp);
static void	xe_mii_send(struct xe_softc *scp, uint32_t bits, int cnt);
static int	xe_mii_readreg(struct xe_softc *scp,
		    struct xe_mii_frame *frame);
static int	xe_mii_writereg(struct xe_softc *scp,
		    struct xe_mii_frame *frame);
static uint16_t	xe_phy_readreg(struct xe_softc *scp, uint16_t reg);
static void	xe_phy_writereg(struct xe_softc *scp, uint16_t reg,
		    uint16_t data);

/*
 * Debugging functions
 */
static void	xe_mii_dump(struct xe_softc *scp);
#if 0
static void	xe_reg_dump(struct xe_softc *scp);
#endif

/*
 * Debug logging levels - set with hw.xe.debug sysctl
 * 0 = None
 * 1 = More hardware details, probe/attach progress
 * 2 = Most function calls, ioctls and media selection progress
 * 3 = Everything - interrupts, packets in/out and multicast address setup
 */
#define	XE_DEBUG
#ifdef XE_DEBUG

/* sysctl vars */
static SYSCTL_NODE(_hw, OID_AUTO, xe, CTLFLAG_RD, 0, "if_xe parameters");

int xe_debug = 0;
SYSCTL_INT(_hw_xe, OID_AUTO, debug, CTLFLAG_RW, &xe_debug, 0,
    "if_xe debug level");

#define	DEVPRINTF(level, arg)	if (xe_debug >= (level)) device_printf arg
#define	DPRINTF(level, arg)	if (xe_debug >= (level)) printf arg
#define	XE_MII_DUMP(scp)	if (xe_debug >= 3) xe_mii_dump(scp)
#if 0
#define	XE_REG_DUMP(scp)	if (xe_debug >= 3) xe_reg_dump(scp)
#endif
#else
#define	DEVPRINTF(level, arg)
#define	DPRINTF(level, arg)
#define	XE_MII_DUMP(scp)
#if 0
#define	XE_REG_DUMP(scp)
#endif
#endif

/*
 * Attach a device.
 */
int
xe_attach(device_t dev)
{
	struct xe_softc *scp = device_get_softc(dev);
	int err;

	DEVPRINTF(2, (dev, "attach\n"));

	/* Initialise stuff... */
	scp->dev = dev;
	scp->ifp = if_alloc(IFT_ETHER);
	if (scp->ifp == NULL)
		return (ENOSPC);
	scp->ifm = &scp->ifmedia;
	scp->autoneg_status = XE_AUTONEG_NONE;
	mtx_init(&scp->lock, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	callout_init_mtx(&scp->wdog_timer, &scp->lock, 0);

	/* Initialise the ifnet structure */
	scp->ifp->if_softc = scp;
	if_initname(scp->ifp, device_get_name(dev), device_get_unit(dev));
	scp->ifp->if_flags = (IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST);
	scp->ifp->if_linkmib = &scp->mibdata;
	scp->ifp->if_linkmiblen = sizeof(scp->mibdata);
	scp->ifp->if_start = xe_start;
	scp->ifp->if_ioctl = xe_ioctl;
	scp->ifp->if_init = xe_init;
	scp->ifp->if_baudrate = 100000000;
	IFQ_SET_MAXLEN(&scp->ifp->if_snd, ifqmaxlen);

	/* Initialise the ifmedia structure */
	ifmedia_init(scp->ifm, 0, xe_media_change, xe_media_status);
	callout_init_mtx(&scp->media_timer, &scp->lock, 0);

	/* Add supported media types */
	if (scp->mohawk) {
		ifmedia_add(scp->ifm, IFM_ETHER|IFM_100_TX, 0, NULL);
		ifmedia_add(scp->ifm, IFM_ETHER|IFM_10_T|IFM_FDX, 0, NULL);
		ifmedia_add(scp->ifm, IFM_ETHER|IFM_10_T|IFM_HDX, 0, NULL);
	}
	ifmedia_add(scp->ifm, IFM_ETHER|IFM_10_T, 0, NULL);
	if (scp->ce2)
		ifmedia_add(scp->ifm, IFM_ETHER|IFM_10_2, 0, NULL);
	ifmedia_add(scp->ifm, IFM_ETHER|IFM_AUTO, 0, NULL);

	/* Default is to autoselect best supported media type */
	ifmedia_set(scp->ifm, IFM_ETHER|IFM_AUTO);

	/* Get the hardware into a known state */
	XE_LOCK(scp);
	xe_reset(scp);
	XE_UNLOCK(scp);

	/* Get hardware version numbers */
	XE_SELECT_PAGE(4);
	scp->version = XE_INB(XE_BOV);
	if (scp->mohawk)
		scp->srev = (XE_INB(XE_BOV) & 0x70) >> 4;
	else
		scp->srev = (XE_INB(XE_BOV) & 0x30) >> 4;

	/* Print some useful information */
	device_printf(dev, "version 0x%02x/0x%02x%s%s\n", scp->version,
	    scp->srev, scp->mohawk ? ", 100Mbps capable" : "",
	    scp->modem ?  ", with modem" : "");
	if (scp->mohawk) {
		XE_SELECT_PAGE(0x10);
		DEVPRINTF(1, (dev,
		    "DingoID=0x%04x, RevisionID=0x%04x, VendorID=0x%04x\n",
		    XE_INW(XE_DINGOID), XE_INW(XE_RevID), XE_INW(XE_VendorID)));
	}
	if (scp->ce2) {
		XE_SELECT_PAGE(0x45);
		DEVPRINTF(1, (dev, "CE2 version = 0x%02x\n", XE_INB(XE_REV)));
	}

	/* Attach the interface */
	ether_ifattach(scp->ifp, scp->enaddr);

	err = bus_setup_intr(dev, scp->irq_res, INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, xe_intr, scp, &scp->intrhand);
	if (err) {
		ether_ifdetach(scp->ifp);
		mtx_destroy(&scp->lock);
		return (err);
	}

	gone_by_fcp101_dev(dev);

	/* Done */
	return (0);
}

/*
 * Complete hardware intitialisation and enable output.  Exits without doing
 * anything if there's no address assigned to the card, or if media selection
 * is in progress (the latter implies we've already run this function).
 */
static void
xe_init(void *xscp)
{
	struct xe_softc *scp = xscp;

	XE_LOCK(scp);
	xe_init_locked(scp);
	XE_UNLOCK(scp);
}

static void
xe_init_locked(struct xe_softc *scp)
{
	unsigned i;

	if (scp->autoneg_status != XE_AUTONEG_NONE)
		return;

	DEVPRINTF(2, (scp->dev, "init\n"));

	/* Reset transmitter flags */
	scp->tx_queued = 0;
	scp->tx_tpr = 0;
	scp->tx_timeouts = 0;
	scp->tx_thres = 64;
	scp->tx_min = ETHER_MIN_LEN - ETHER_CRC_LEN;
	scp->tx_timeout = 0;

	/* Soft reset the card */
	XE_SELECT_PAGE(0);
	XE_OUTB(XE_CR, XE_CR_SOFT_RESET);
	DELAY(40000);
	XE_OUTB(XE_CR, 0);
	DELAY(40000);

	if (scp->mohawk) {
		/*
		 * set GP1 and GP2 as outputs (bits 2 & 3)
		 * set GP1 low to power on the ML6692 (bit 0)
		 * set GP2 high to power on the 10Mhz chip (bit 1)
		 */
		XE_SELECT_PAGE(4);
		XE_OUTB(XE_GPR0, XE_GPR0_GP2_SELECT | XE_GPR0_GP1_SELECT |
		    XE_GPR0_GP2_OUT);
	}

	/* Shut off interrupts */
	xe_disable_intr(scp);

	/* Wait for everything to wake up */
	DELAY(500000);

	/* Check for PHY */
	if (scp->mohawk)
		scp->phy_ok = xe_mii_init(scp);

	/* Disable 'source insertion' (not sure what that means) */
	XE_SELECT_PAGE(0x42);
	XE_OUTB(XE_SWC0, XE_SWC0_NO_SRC_INSERT);

	/* Set 8K/24K Tx/Rx buffer split */
	if (scp->srev != 1) {
		XE_SELECT_PAGE(2);
		XE_OUTW(XE_RBS, 0x2000);
	}

	/* Enable early transmit mode on Mohawk/Dingo */
	if (scp->mohawk) {
		XE_SELECT_PAGE(0x03);
		XE_OUTW(XE_TPT, scp->tx_thres);
		XE_SELECT_PAGE(0x01);
		XE_OUTB(XE_ECR, XE_INB(XE_ECR) | XE_ECR_EARLY_TX);
	}

	/* Put MAC address in first 'individual address' register */
	XE_SELECT_PAGE(0x50);
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		XE_OUTB(0x08 + i, IF_LLADDR(scp->ifp)[scp->mohawk ? 5 - i : i]);

	/* Set up multicast addresses */
	xe_set_multicast(scp);

	/* Fix the receive data offset -- reset can leave it off-by-one */
	XE_SELECT_PAGE(0);
	XE_OUTW(XE_DO, 0x2000);

	/* Set interrupt masks */
	XE_SELECT_PAGE(1);
	XE_OUTB(XE_IMR0, XE_IMR0_TX_PACKET | XE_IMR0_MAC_INTR |
	    XE_IMR0_RX_PACKET);

	/* Set MAC interrupt masks */
	XE_SELECT_PAGE(0x40);
	XE_OUTB(XE_RX0Msk,
	    ~(XE_RX0M_RX_OVERRUN | XE_RX0M_CRC_ERROR | XE_RX0M_ALIGN_ERROR |
	    XE_RX0M_LONG_PACKET));
	XE_OUTB(XE_TX0Msk,
	    ~(XE_TX0M_SQE_FAIL | XE_TX0M_LATE_COLLISION | XE_TX0M_TX_UNDERRUN |
	    XE_TX0M_16_COLLISIONS | XE_TX0M_NO_CARRIER));

	/* Clear MAC status registers */
	XE_SELECT_PAGE(0x40);
	XE_OUTB(XE_RST0, 0x00);
	XE_OUTB(XE_TXST0, 0x00);

	/* Enable receiver and put MAC online */
	XE_SELECT_PAGE(0x40);
	XE_OUTB(XE_CMD0, XE_CMD0_RX_ENABLE|XE_CMD0_ONLINE);

	/* Set up IMR, enable interrupts */
	xe_enable_intr(scp);

	/* Start media selection */
	xe_setmedia(scp);

	/* Enable output */
	scp->ifp->if_drv_flags |= IFF_DRV_RUNNING;
	scp->ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	callout_reset(&scp->wdog_timer, hz, xe_watchdog, scp);
}

/*
 * Start output on interface.  Should be called at splimp() priority.  Check
 * that the output is idle (ie, IFF_DRV_OACTIVE is not set) before calling this
 * function.  If media selection is in progress we set IFF_DRV_OACTIVE ourselves
 * and return immediately.
 */
static void
xe_start(struct ifnet *ifp)
{
	struct xe_softc *scp = ifp->if_softc;

	XE_LOCK(scp);
	xe_start_locked(ifp);
	XE_UNLOCK(scp);
}

static void
xe_start_locked(struct ifnet *ifp)
{
	struct xe_softc *scp = ifp->if_softc;
	struct mbuf *mbp;

	if (scp->autoneg_status != XE_AUTONEG_NONE) {
		ifp->if_drv_flags |= IFF_DRV_OACTIVE;
		return;
	}

	DEVPRINTF(3, (scp->dev, "start\n"));

	/*
	 * Loop while there are packets to be sent, and space to send
	 * them.
	 */
	for (;;) {
		/* Suck a packet off the send queue */
		IF_DEQUEUE(&ifp->if_snd, mbp);

		if (mbp == NULL) {
			/*
			 * We are using the !OACTIVE flag to indicate
			 * to the outside world that we can accept an
			 * additional packet rather than that the
			 * transmitter is _actually_ active. Indeed,
			 * the transmitter may be active, but if we
			 * haven't filled all the buffers with data
			 * then we still want to accept more.
			 */
			ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
			return;
		}

		if (xe_pio_write_packet(scp, mbp) != 0) {
			/* Push the packet back onto the queue */
			IF_PREPEND(&ifp->if_snd, mbp);
			ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			return;
		}

		/* Tap off here if there is a bpf listener */
		BPF_MTAP(ifp, mbp);

		/* In case we don't hear from the card again... */
		scp->tx_timeout = 5;
		scp->tx_queued++;

		m_freem(mbp);
	}
}

/*
 * Process an ioctl request.  Adapted from the ed driver.
 */
static int
xe_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct xe_softc *scp;
	int error;

	scp = ifp->if_softc;
	error = 0;

	switch (command) {
	case SIOCSIFFLAGS:
		DEVPRINTF(2, (scp->dev, "ioctl: SIOCSIFFLAGS: 0x%04x\n",
			ifp->if_flags));
		/*
		 * If the interface is marked up and stopped, then
		 * start it.  If it is marked down and running, then
		 * stop it.
		 */
		XE_LOCK(scp);
		if (ifp->if_flags & IFF_UP) {
			if (!(ifp->if_drv_flags & IFF_DRV_RUNNING)) {
				xe_reset(scp);
				xe_init_locked(scp);
			}
		} else {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING)
				xe_stop(scp);
		}

		/* handle changes to PROMISC/ALLMULTI flags */
		xe_set_multicast(scp);
		XE_UNLOCK(scp);
		error = 0;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		DEVPRINTF(2, (scp->dev, "ioctl: SIOC{ADD,DEL}MULTI\n"));
		/*
		 * Multicast list has (maybe) changed; set the
		 * hardware filters accordingly.
		 */
		XE_LOCK(scp);
		xe_set_multicast(scp);
		XE_UNLOCK(scp);
		error = 0;
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		DEVPRINTF(3, (scp->dev, "ioctl: bounce to ifmedia_ioctl\n"));
		/*
		 * Someone wants to get/set media options.
		 */
		error = ifmedia_ioctl(ifp, (struct ifreq *)data, &scp->ifmedia,
		    command);
		break;
	default:
		DEVPRINTF(3, (scp->dev, "ioctl: bounce to ether_ioctl\n"));
		error = ether_ioctl(ifp, command, data);
	}

	return (error);
}

/*
 * Card interrupt handler.
 *
 * This function is probably more complicated than it needs to be, as it
 * attempts to deal with the case where multiple packets get sent between
 * interrupts.  This is especially annoying when working out the collision
 * stats.  Not sure whether this case ever really happens or not (maybe on a
 * slow/heavily loaded machine?) so it's probably best to leave this like it
 * is.
 *
 * Note that the crappy PIO used to get packets on and off the card means that 
 * you will spend a lot of time in this routine -- I can get my P150 to spend
 * 90% of its time servicing interrupts if I really hammer the network.  Could 
 * fix this, but then you'd start dropping/losing packets.  The moral of this
 * story?  If you want good network performance _and_ some cycles left over to 
 * get your work done, don't buy a Xircom card.  Or convince them to tell me
 * how to do memory-mapped I/O :)
 */
static void
xe_txintr(struct xe_softc *scp, uint8_t txst1)
{
	struct ifnet *ifp;
	uint8_t tpr, sent, coll;

	ifp = scp->ifp;

	/* Update packet count, accounting for rollover */
	tpr = XE_INB(XE_TPR);
	sent = -scp->tx_tpr + tpr;

	/* Update statistics if we actually sent anything */
	if (sent > 0) {
		coll = txst1 & XE_TXST1_RETRY_COUNT;
		scp->tx_tpr = tpr;
		scp->tx_queued -= sent;
		if_inc_counter(ifp, IFCOUNTER_OPACKETS, sent);
		if_inc_counter(ifp, IFCOUNTER_COLLISIONS, coll);

		/*
		 * According to the Xircom manual, Dingo will
		 * sometimes manage to transmit a packet with
		 * triggering an interrupt.  If this happens, we have
		 * sent > 1 and the collision count only reflects
		 * collisions on the last packet sent (the one that
		 * triggered the interrupt).  Collision stats might
		 * therefore be a bit low, but there doesn't seem to
		 * be anything we can do about that.
		 */
		switch (coll) {
		case 0:
			break;
		case 1:
			scp->mibdata.dot3StatsSingleCollisionFrames++;
			scp->mibdata.dot3StatsCollFrequencies[0]++;
			break;
		default:
			scp->mibdata.dot3StatsMultipleCollisionFrames++;
			scp->mibdata.dot3StatsCollFrequencies[coll-1]++;
		}
	}
	scp->tx_timeout = 0;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
}

/* Handle most MAC interrupts */
static void
xe_macintr(struct xe_softc *scp, uint8_t rst0, uint8_t txst0, uint8_t txst1)
{
	struct ifnet *ifp;

	ifp = scp->ifp;

#if 0
	/* Carrier sense lost -- only in 10Mbit HDX mode */
	if (txst0 & XE_TXST0_NO_CARRIER || !(txst1 & XE_TXST1_LINK_STATUS)) {
		/* XXX - Need to update media status here */
		device_printf(scp->dev, "no carrier\n");
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		scp->mibdata.dot3StatsCarrierSenseErrors++;
	}
#endif
	/* Excessive collisions -- try sending again */
	if (txst0 & XE_TXST0_16_COLLISIONS) {
		if_inc_counter(ifp, IFCOUNTER_COLLISIONS, 16);
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		scp->mibdata.dot3StatsExcessiveCollisions++;
		scp->mibdata.dot3StatsMultipleCollisionFrames++;
		scp->mibdata.dot3StatsCollFrequencies[15]++;
		XE_OUTB(XE_CR, XE_CR_RESTART_TX);
	}

	/* Transmit underrun -- increase early transmit threshold */
	if (txst0 & XE_TXST0_TX_UNDERRUN && scp->mohawk) {
		DEVPRINTF(1, (scp->dev, "transmit underrun"));
		if (scp->tx_thres < ETHER_MAX_LEN) {
			if ((scp->tx_thres += 64) > ETHER_MAX_LEN)
				scp->tx_thres = ETHER_MAX_LEN;
			DPRINTF(1, (": increasing transmit threshold to %u",
			    scp->tx_thres));
			XE_SELECT_PAGE(0x3);
			XE_OUTW(XE_TPT, scp->tx_thres);
			XE_SELECT_PAGE(0x0);
		}
		DPRINTF(1, ("\n"));
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		scp->mibdata.dot3StatsInternalMacTransmitErrors++;
	}

	/* Late collision -- just complain about it */
	if (txst0 & XE_TXST0_LATE_COLLISION) {
		device_printf(scp->dev, "late collision\n");
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		scp->mibdata.dot3StatsLateCollisions++;
	}

	/* SQE test failure -- just complain about it */
	if (txst0 & XE_TXST0_SQE_FAIL) {
		device_printf(scp->dev, "SQE test failure\n");
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		scp->mibdata.dot3StatsSQETestErrors++;
	}

	/* Packet too long -- what happens to these */
	if (rst0 & XE_RST0_LONG_PACKET) {
		device_printf(scp->dev, "received giant packet\n");
		if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		scp->mibdata.dot3StatsFrameTooLongs++;
	}

	/* CRC error -- packet dropped */
	if (rst0 & XE_RST0_CRC_ERROR) {
		device_printf(scp->dev, "CRC error\n");
		if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		scp->mibdata.dot3StatsFCSErrors++;
	}
}

static void
xe_rxintr(struct xe_softc *scp, uint8_t rst0)
{
	struct ifnet *ifp;
	uint8_t esr, rsr;

	ifp = scp->ifp;

	/* Handle received packet(s) */
	while ((esr = XE_INB(XE_ESR)) & XE_ESR_FULL_PACKET_RX) {
		rsr = XE_INB(XE_RSR);

		DEVPRINTF(3, (scp->dev, "intr: ESR=0x%02x, RSR=0x%02x\n", esr,
		    rsr));

		/* Make sure packet is a good one */
		if (rsr & XE_RSR_RX_OK) {
			struct ether_header *ehp;
			struct mbuf *mbp;
			uint16_t len;

			len = XE_INW(XE_RBC) - ETHER_CRC_LEN;

			DEVPRINTF(3, (scp->dev, "intr: receive length = %d\n",
			    len));

			if (len == 0) {
				if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
				continue;
			}

			/*
			 * Allocate mbuf to hold received packet.  If
			 * the mbuf header isn't big enough, we attach
			 * an mbuf cluster to hold the packet.  Note
			 * the +=2 to align the packet data on a
			 * 32-bit boundary, and the +3 to allow for
			 * the possibility of reading one more byte
			 * than the actual packet length (we always
			 * read 16-bit words).  XXX - Surely there's a
			 * better way to do this alignment?
			 */
			MGETHDR(mbp, M_NOWAIT, MT_DATA);
			if (mbp == NULL) {
				if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
				continue;
			}

			if (len + 3 > MHLEN) {
				if (!(MCLGET(mbp, M_NOWAIT))) {
					m_freem(mbp);
					if_inc_counter(ifp, IFCOUNTER_IQDROPS, 1);
					continue;
				}
			}

			mbp->m_data += 2;
			ehp = mtod(mbp, struct ether_header *);

			/*
			 * Now get the packet in PIO mode, including
			 * the Ethernet header but omitting the
			 * trailing CRC.
			 */

			/*
			 * Work around a bug in CE2 cards.  There
			 * seems to be a problem with duplicated and
			 * extraneous bytes in the receive buffer, but
			 * without any real documentation for the CE2
			 * it's hard to tell for sure.  XXX - Needs
			 * testing on CE2 hardware
			 */
			if (scp->srev == 0) {
				u_short rhs;

				XE_SELECT_PAGE(5);
				rhs = XE_INW(XE_RHSA);
				XE_SELECT_PAGE(0);

				rhs += 3;	 /* Skip control info */

				if (rhs >= 0x8000)
					rhs = 0;

				if (rhs + len > 0x8000) {
					int i;

					for (i = 0; i < len; i++, rhs++) {
						((char *)ehp)[i] =
						    XE_INB(XE_EDP);
						if (rhs == 0x8000) {
							rhs = 0;
							i--;
						}
					}
				} else
					bus_read_multi_2(scp->port_res, XE_EDP, 
					    (uint16_t *)ehp, (len + 1) >> 1);
			} else
				bus_read_multi_2(scp->port_res, XE_EDP, 
				    (uint16_t *)ehp, (len + 1) >> 1);

			/* Deliver packet to upper layers */
			mbp->m_pkthdr.rcvif = ifp;
			mbp->m_pkthdr.len = mbp->m_len = len;
			XE_UNLOCK(scp);
			(*ifp->if_input)(ifp, mbp);
			XE_LOCK(scp);
			if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);

		} else if (rsr & XE_RSR_ALIGN_ERROR) {
			/* Packet alignment error -- drop packet */
			device_printf(scp->dev, "alignment error\n");
			scp->mibdata.dot3StatsAlignmentErrors++;
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		}

		/* Skip to next packet, if there is one */
		XE_OUTW(XE_DO, 0x8000);
	}

	/* Clear receiver overruns now we have some free buffer space */
	if (rst0 & XE_RST0_RX_OVERRUN) {
		DEVPRINTF(1, (scp->dev, "receive overrun\n"));
		if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		scp->mibdata.dot3StatsInternalMacReceiveErrors++;
		XE_OUTB(XE_CR, XE_CR_CLEAR_OVERRUN);
	}
}

static void
xe_intr(void *xscp)
{
	struct xe_softc *scp = (struct xe_softc *) xscp;
	struct ifnet *ifp;
	uint8_t psr, isr, rst0, txst0, txst1;

	ifp = scp->ifp;
	XE_LOCK(scp);

	/* Disable interrupts */
	if (scp->mohawk)
		XE_OUTB(XE_CR, 0);

	/* Cache current register page */
	psr = XE_INB(XE_PR);

	/* Read ISR to see what caused this interrupt */
	while ((isr = XE_INB(XE_ISR)) != 0) {

		/* 0xff might mean the card is no longer around */
		if (isr == 0xff) {
			DEVPRINTF(3, (scp->dev,
			    "intr: interrupt received for missing card?\n"));
			break;
		}

		/* Read other status registers */
		XE_SELECT_PAGE(0x40);
		rst0 = XE_INB(XE_RST0);
		XE_OUTB(XE_RST0, 0);
		txst0 = XE_INB(XE_TXST0);
		txst1 = XE_INB(XE_TXST1);
		XE_OUTB(XE_TXST0, 0);
		XE_OUTB(XE_TXST1, 0);
		XE_SELECT_PAGE(0);

		DEVPRINTF(3, (scp->dev,
		    "intr: ISR=0x%02x, RST=0x%02x, TXT=0x%02x%02x\n", isr,
		    rst0, txst1, txst0));

		if (isr & XE_ISR_TX_PACKET)
			xe_txintr(scp, txst1);

		if (isr & XE_ISR_MAC_INTR)
			xe_macintr(scp, rst0, txst0, txst1);

		xe_rxintr(scp, rst0);
	}

	/* Restore saved page */
	XE_SELECT_PAGE(psr);

	/* Re-enable interrupts */
	XE_OUTB(XE_CR, XE_CR_ENABLE_INTR);

	XE_UNLOCK(scp);
}

/*
 * Device timeout/watchdog routine.  Called automatically if we queue a packet 
 * for transmission but don't get an interrupt within a specified timeout
 * (usually 5 seconds).  When this happens we assume the worst and reset the
 * card.
 */
static void
xe_watchdog(void *arg)
{
	struct xe_softc *scp = arg;

	XE_ASSERT_LOCKED(scp);

	if (scp->tx_timeout && --scp->tx_timeout == 0) {
   		device_printf(scp->dev, "watchdog timeout: resetting card\n");
		scp->tx_timeouts++;
		if_inc_counter(scp->ifp, IFCOUNTER_OERRORS, scp->tx_queued);
		xe_stop(scp);
		xe_reset(scp);
		xe_init_locked(scp);
	}
	callout_reset(&scp->wdog_timer, hz, xe_watchdog, scp);
}

/*
 * Change media selection.
 */
static int
xe_media_change(struct ifnet *ifp)
{
	struct xe_softc *scp = ifp->if_softc;

	DEVPRINTF(2, (scp->dev, "media_change\n"));

	XE_LOCK(scp);
	if (IFM_TYPE(scp->ifm->ifm_media) != IFM_ETHER) {
		XE_UNLOCK(scp);
		return(EINVAL);
	}

	/*
	 * Some card/media combos aren't always possible -- filter
	 * those out here.
	 */
	if ((IFM_SUBTYPE(scp->ifm->ifm_media) == IFM_AUTO ||
	    IFM_SUBTYPE(scp->ifm->ifm_media) == IFM_100_TX) && !scp->phy_ok) {
		XE_UNLOCK(scp);
		return (EINVAL);
	}

	xe_setmedia(scp);
	XE_UNLOCK(scp);

	return (0);
}

/*
 * Return current media selection.
 */
static void
xe_media_status(struct ifnet *ifp, struct ifmediareq *mrp)
{
	struct xe_softc *scp = ifp->if_softc;

	DEVPRINTF(3, (scp->dev, "media_status\n"));

	/* XXX - This is clearly wrong.  Will fix once I have CE2 working */
	XE_LOCK(scp);
	mrp->ifm_status = IFM_AVALID | IFM_ACTIVE;
	mrp->ifm_active = ((struct xe_softc *)ifp->if_softc)->media;
	XE_UNLOCK(scp);
}

/*
 * Select active media.
 */
static void
xe_setmedia(void *xscp)
{
	struct xe_softc *scp = xscp;
	uint16_t bmcr, bmsr, anar, lpar;

	DEVPRINTF(2, (scp->dev, "setmedia\n"));

	XE_ASSERT_LOCKED(scp);

	/* Cancel any pending timeout */
	callout_stop(&scp->media_timer);
	xe_disable_intr(scp);

	/* Select media */
	scp->media = IFM_ETHER;
	switch (IFM_SUBTYPE(scp->ifm->ifm_media)) {

	case IFM_AUTO:	/* Autoselect media */
		scp->media = IFM_ETHER|IFM_AUTO;

		/*
		 * Autoselection is really awful.  It goes something like this:
		 *
		 * Wait until the transmitter goes idle (2sec timeout).
		 * Reset card
		 *   IF a 100Mbit PHY exists
		 *     Start NWAY autonegotiation (3.5sec timeout)
		 *     IF that succeeds
		 *       Select 100baseTX or 10baseT, whichever was detected
		 *     ELSE
		 *       Reset card
		 *       IF a 100Mbit PHY exists
		 *         Try to force a 100baseTX link (3sec timeout)
		 *         IF that succeeds
		 *           Select 100baseTX
		 *         ELSE
		 *           Disable the PHY
		 *         ENDIF
		 *       ENDIF
		 *     ENDIF
		 *   ENDIF
		 * IF nothing selected so far
		 *   IF a 100Mbit PHY exists
		 *     Select 10baseT
		 *   ELSE
		 *     Select 10baseT or 10base2, whichever is connected
		 *   ENDIF
		 * ENDIF
		 */
		switch (scp->autoneg_status) {
		case XE_AUTONEG_NONE:
			DEVPRINTF(2, (scp->dev,
			    "Waiting for idle transmitter\n"));
			scp->ifp->if_drv_flags |= IFF_DRV_OACTIVE;
			scp->autoneg_status = XE_AUTONEG_WAITING;
			/* FALL THROUGH */
		case XE_AUTONEG_WAITING:
			if (scp->tx_queued != 0) {
				callout_reset(&scp->media_timer, hz / 2,
				    xe_setmedia, scp);
				return;
			}
			if (scp->phy_ok) {
				DEVPRINTF(2, (scp->dev,
					"Starting autonegotiation\n"));
				bmcr = xe_phy_readreg(scp, PHY_BMCR);
				bmcr &= ~(PHY_BMCR_AUTONEGENBL);
				xe_phy_writereg(scp, PHY_BMCR, bmcr);
				anar = xe_phy_readreg(scp, PHY_ANAR);
				anar &= ~(PHY_ANAR_100BT4 |
				    PHY_ANAR_100BTXFULL | PHY_ANAR_10BTFULL);
				anar |= PHY_ANAR_100BTXHALF | PHY_ANAR_10BTHALF;
				xe_phy_writereg(scp, PHY_ANAR, anar);
				bmcr |= PHY_BMCR_AUTONEGENBL |
				    PHY_BMCR_AUTONEGRSTR;
				xe_phy_writereg(scp, PHY_BMCR, bmcr);
				scp->autoneg_status = XE_AUTONEG_STARTED;
				callout_reset(&scp->media_timer, hz * 7/2,
				    xe_setmedia, scp);
				return;
			} else {
				scp->autoneg_status = XE_AUTONEG_FAIL;
			}
			break;
		case XE_AUTONEG_STARTED:
			bmsr = xe_phy_readreg(scp, PHY_BMSR);
			lpar = xe_phy_readreg(scp, PHY_LPAR);
			if (bmsr & (PHY_BMSR_AUTONEGCOMP | PHY_BMSR_LINKSTAT)) {
				DEVPRINTF(2, (scp->dev,
				    "Autonegotiation complete!\n"));

				/*
				 * XXX - Shouldn't have to do this,
				 * but (on my hub at least) the
				 * transmitter won't work after a
				 * successful autoneg.  So we see what
				 * the negotiation result was and
				 * force that mode.  I'm sure there is
				 * an easy fix for this.
				 */
				if (lpar & PHY_LPAR_100BTXHALF) {
					xe_phy_writereg(scp, PHY_BMCR,
					    PHY_BMCR_SPEEDSEL);
					XE_MII_DUMP(scp);
					XE_SELECT_PAGE(2);
					XE_OUTB(XE_MSR, XE_INB(XE_MSR) | 0x08);
					scp->media = IFM_ETHER | IFM_100_TX;
					scp->autoneg_status = XE_AUTONEG_NONE;
				} else {
					/*
					 * XXX - Bit of a hack going
					 * on in here.  This is
					 * derived from Ken Hughes
					 * patch to the Linux driver
					 * to make it work with 10Mbit
					 * _autonegotiated_ links on
					 * CE3B cards.  What's a CE3B
					 * and how's it differ from a
					 * plain CE3?  these are the
					 * things we need to find out.
					 */
					xe_phy_writereg(scp, PHY_BMCR, 0x0000);
					XE_SELECT_PAGE(2);
					/* BEGIN HACK */
					XE_OUTB(XE_MSR, XE_INB(XE_MSR) | 0x08);
					XE_SELECT_PAGE(0x42);
					XE_OUTB(XE_SWC1, 0x80);
					scp->media = IFM_ETHER | IFM_10_T;
					scp->autoneg_status = XE_AUTONEG_NONE;
					/* END HACK */
#if 0
					/* Display PHY? */
					XE_OUTB(XE_MSR, XE_INB(XE_MSR) & ~0x08);
					scp->autoneg_status = XE_AUTONEG_FAIL;
#endif
				}
			} else {
				DEVPRINTF(2, (scp->dev,
			    "Autonegotiation failed; trying 100baseTX\n"));
				XE_MII_DUMP(scp);
				if (scp->phy_ok) {
					xe_phy_writereg(scp, PHY_BMCR,
					    PHY_BMCR_SPEEDSEL);
					scp->autoneg_status = XE_AUTONEG_100TX;
					callout_reset(&scp->media_timer, hz * 3,
					    xe_setmedia, scp);
					return;
				} else {
					scp->autoneg_status = XE_AUTONEG_FAIL;
				}
			}
			break;
		case XE_AUTONEG_100TX:
			(void)xe_phy_readreg(scp, PHY_BMSR);
			bmsr = xe_phy_readreg(scp, PHY_BMSR);
			if (bmsr & PHY_BMSR_LINKSTAT) {
				DEVPRINTF(2, (scp->dev,
				    "Got 100baseTX link!\n"));
				XE_MII_DUMP(scp);
				XE_SELECT_PAGE(2);
				XE_OUTB(XE_MSR, XE_INB(XE_MSR) | 0x08);
				scp->media = IFM_ETHER | IFM_100_TX;
				scp->autoneg_status = XE_AUTONEG_NONE;
			} else {
				DEVPRINTF(2, (scp->dev,
				    "Autonegotiation failed; disabling PHY\n"));
				XE_MII_DUMP(scp);
				xe_phy_writereg(scp, PHY_BMCR, 0x0000);
				XE_SELECT_PAGE(2);

				/* Disable PHY? */
				XE_OUTB(XE_MSR, XE_INB(XE_MSR) & ~0x08);
				scp->autoneg_status = XE_AUTONEG_FAIL;
			}
			break;
		}

		/*
		 * If we got down here _and_ autoneg_status is
		 * XE_AUTONEG_FAIL, then either autonegotiation
		 * failed, or never got started to begin with.  In
		 * either case, select a suitable 10Mbit media and
		 * hope it works.  We don't need to reset the card
		 * again, since it will have been done already by the
		 * big switch above.
		 */
		if (scp->autoneg_status == XE_AUTONEG_FAIL) {
			DEVPRINTF(2, (scp->dev, "Selecting 10baseX\n"));
			if (scp->mohawk) {
				XE_SELECT_PAGE(0x42);
				XE_OUTB(XE_SWC1, 0x80);
				scp->media = IFM_ETHER | IFM_10_T;
				scp->autoneg_status = XE_AUTONEG_NONE;
			} else {
				XE_SELECT_PAGE(4);
				XE_OUTB(XE_GPR0, 4);
				DELAY(50000);
				XE_SELECT_PAGE(0x42);
				XE_OUTB(XE_SWC1,
				    (XE_INB(XE_ESR) & XE_ESR_MEDIA_SELECT) ?
				    0x80 : 0xc0);
				scp->media = IFM_ETHER | ((XE_INB(XE_ESR) &
				    XE_ESR_MEDIA_SELECT) ? IFM_10_T : IFM_10_2);
				scp->autoneg_status = XE_AUTONEG_NONE;
			}
		}
		break;

	/*
	 * If a specific media has been requested, we just reset the
	 * card and select it (one small exception -- if 100baseTX is
	 * requested but there is no PHY, we fall back to 10baseT
	 * operation).
	 */
	case IFM_100_TX:	/* Force 100baseTX */
		if (scp->phy_ok) {
			DEVPRINTF(2, (scp->dev, "Selecting 100baseTX\n"));
			XE_SELECT_PAGE(0x42);
			XE_OUTB(XE_SWC1, 0);
			xe_phy_writereg(scp, PHY_BMCR, PHY_BMCR_SPEEDSEL);
			XE_SELECT_PAGE(2);
			XE_OUTB(XE_MSR, XE_INB(XE_MSR) | 0x08);
			scp->media |= IFM_100_TX;
			break;
		}
		/* FALLTHROUGH */
	case IFM_10_T:		/* Force 10baseT */
		DEVPRINTF(2, (scp->dev, "Selecting 10baseT\n"));
		if (scp->phy_ok) {
			xe_phy_writereg(scp, PHY_BMCR, 0x0000);
			XE_SELECT_PAGE(2);

			/* Disable PHY */
			XE_OUTB(XE_MSR, XE_INB(XE_MSR) & ~0x08);
		}
		XE_SELECT_PAGE(0x42);
		XE_OUTB(XE_SWC1, 0x80);
		scp->media |= IFM_10_T;
		break;
	case IFM_10_2:
		DEVPRINTF(2, (scp->dev, "Selecting 10base2\n"));
		XE_SELECT_PAGE(0x42);
		XE_OUTB(XE_SWC1, 0xc0);
		scp->media |= IFM_10_2;
		break;
	}

	/*
	 * Finally, the LEDs are set to match whatever media was
	 * chosen and the transmitter is unblocked.
	 */
	DEVPRINTF(2, (scp->dev, "Setting LEDs\n"));
	XE_SELECT_PAGE(2);
	switch (IFM_SUBTYPE(scp->media)) {
	case IFM_100_TX:
	case IFM_10_T:
		XE_OUTB(XE_LED, 0x3b);
		if (scp->dingo)
			XE_OUTB(0x0b, 0x04);	/* 100Mbit LED */
		break;
	case IFM_10_2:
		XE_OUTB(XE_LED, 0x3a);
		break;
	}

	/* Restart output? */
	xe_enable_intr(scp);
	scp->ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	xe_start_locked(scp->ifp);
}

/*
 * Hard reset (power cycle) the card.
 */
static void
xe_reset(struct xe_softc *scp)
{

	DEVPRINTF(2, (scp->dev, "reset\n"));

	XE_ASSERT_LOCKED(scp);

	/* Power down */
	XE_SELECT_PAGE(4);
	XE_OUTB(XE_GPR1, 0);
	DELAY(40000);

	/* Power up again */
	if (scp->mohawk)
		XE_OUTB(XE_GPR1, XE_GPR1_POWER_DOWN);
	else
		XE_OUTB(XE_GPR1, XE_GPR1_POWER_DOWN | XE_GPR1_AIC);

	DELAY(40000);
	XE_SELECT_PAGE(0);
}

/*
 * Take interface offline.  This is done by powering down the device, which I
 * assume means just shutting down the transceiver and Ethernet logic.  This
 * requires a _hard_ reset to recover from, as we need to power up again.
 */
void
xe_stop(struct xe_softc *scp)
{

	DEVPRINTF(2, (scp->dev, "stop\n"));

	XE_ASSERT_LOCKED(scp);

	/*
	 * Shut off interrupts.
	 */
	xe_disable_intr(scp);

	/*
	 * Power down.
	 */
	XE_SELECT_PAGE(4);
	XE_OUTB(XE_GPR1, 0);
	XE_SELECT_PAGE(0);
	if (scp->mohawk) {
		/*
		 * set GP1 and GP2 as outputs (bits 2 & 3)
		 * set GP1 high to power on the ML6692 (bit 0)
		 * set GP2 low to power on the 10Mhz chip (bit 1)
		 */
		XE_SELECT_PAGE(4);
		XE_OUTB(XE_GPR0, XE_GPR0_GP2_SELECT | XE_GPR0_GP1_SELECT |
		    XE_GPR0_GP1_OUT);
	}

	/*
	 * ~IFF_DRV_RUNNING == interface down.
	 */
	scp->ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
	scp->ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	scp->tx_timeout = 0;
	callout_stop(&scp->wdog_timer);
	callout_stop(&scp->media_timer);
}

/*
 * Enable interrupts from the card.
 */
static void
xe_enable_intr(struct xe_softc *scp)
{

	DEVPRINTF(2, (scp->dev, "enable_intr\n"));

	XE_SELECT_PAGE(0);
	XE_OUTB(XE_CR, XE_CR_ENABLE_INTR);	/* Enable interrupts */
	if (scp->modem && !scp->dingo) {	/* This bit is just magic */
		if (!(XE_INB(0x10) & 0x01)) {
			XE_OUTB(0x10, 0x11);	/* Unmask master int enable */
		}
	}
}

/*
 * Disable interrupts from the card.
 */
static void
xe_disable_intr(struct xe_softc *scp)
{

	DEVPRINTF(2, (scp->dev, "disable_intr\n"));

	XE_SELECT_PAGE(0);
	XE_OUTB(XE_CR, 0);			/* Disable interrupts */
	if (scp->modem && !scp->dingo) {	/* More magic */
		XE_OUTB(0x10, 0x10);		/* Mask the master int enable */
	}
}

/*
 * Set up multicast filter and promiscuous modes.
 */
static void
xe_set_multicast(struct xe_softc *scp)
{
	struct ifnet *ifp;
	struct ifmultiaddr *maddr;
	unsigned count, i;

	DEVPRINTF(2, (scp->dev, "set_multicast\n"));

	ifp = scp->ifp;
	XE_SELECT_PAGE(0x42);

	/* Handle PROMISC flag */
	if (ifp->if_flags & IFF_PROMISC) {
		XE_OUTB(XE_SWC1, XE_INB(XE_SWC1) | XE_SWC1_PROMISCUOUS);
		return;
	} else
		XE_OUTB(XE_SWC1, XE_INB(XE_SWC1) & ~XE_SWC1_PROMISCUOUS);

	/* Handle ALLMULTI flag */
	if (ifp->if_flags & IFF_ALLMULTI) {
		XE_OUTB(XE_SWC1, XE_INB(XE_SWC1) | XE_SWC1_ALLMULTI);
		return;
	} else
		XE_OUTB(XE_SWC1, XE_INB(XE_SWC1) & ~XE_SWC1_ALLMULTI);

	/* Iterate over multicast address list */
	count = 0;
	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(maddr, &ifp->if_multiaddrs, ifma_link) {
		if (maddr->ifma_addr->sa_family != AF_LINK)
			continue;

		count++;

		if (count < 10)
			/*
			 * First 9 use Individual Addresses for exact
			 * matching.
			 */
			xe_set_addr(scp,
			    LLADDR((struct sockaddr_dl *)maddr->ifma_addr),
			    count);
		else if (scp->mohawk)
			/* Use hash filter on Mohawk and Dingo */
			xe_mchash(scp,
			    LLADDR((struct sockaddr_dl *)maddr->ifma_addr));
		else
			/* Nowhere else to put them on CE2 */
			break;
	}
	if_maddr_runlock(ifp);

	DEVPRINTF(2, (scp->dev, "set_multicast: count = %u\n", count));

	/* Now do some cleanup and enable multicast handling as needed */
	if (count == 0) {
		/* Disable all multicast handling */
		XE_SELECT_PAGE(0x42);
		XE_OUTB(XE_SWC1, XE_INB(XE_SWC1) &
		    ~(XE_SWC1_IA_ENABLE | XE_SWC1_ALLMULTI));
		if (scp->mohawk) {
			XE_SELECT_PAGE(0x02);
			XE_OUTB(XE_MSR, XE_INB(XE_MSR) & ~XE_MSR_HASH_TABLE);
		}
	} else if (count < 10) {
		/*
		 * Full in any unused Individual Addresses with our
		 * MAC address.
		 */
		for (i = count + 1; i < 10; i++)
			xe_set_addr(scp, IF_LLADDR(scp->ifp), i);

		/* Enable Individual Address matching only */
		XE_SELECT_PAGE(0x42);
		XE_OUTB(XE_SWC1, (XE_INB(XE_SWC1) & ~XE_SWC1_ALLMULTI) |
		    XE_SWC1_IA_ENABLE);
		if (scp->mohawk) {
			XE_SELECT_PAGE(0x02);
			XE_OUTB(XE_MSR, XE_INB(XE_MSR) & ~XE_MSR_HASH_TABLE);
		}
	} else if (scp->mohawk) {
		/* Check whether hash table is full */
		XE_SELECT_PAGE(0x58);
		for (i = 0x08; i < 0x10; i++)
			if (XE_INB(i) != 0xff)
				break;
		if (i == 0x10) {
			/*
			 * Hash table full - enable
			 * promiscuous multicast matching
			 */
			XE_SELECT_PAGE(0x42);
			XE_OUTB(XE_SWC1, (XE_INB(XE_SWC1) &
			    ~XE_SWC1_IA_ENABLE) | XE_SWC1_ALLMULTI);
			XE_SELECT_PAGE(0x02);
			XE_OUTB(XE_MSR, XE_INB(XE_MSR) & ~XE_MSR_HASH_TABLE);
		} else {
			/* Enable hash table and Individual Address matching */
			XE_SELECT_PAGE(0x42);
			XE_OUTB(XE_SWC1, (XE_INB(XE_SWC1) & ~XE_SWC1_ALLMULTI) |
			    XE_SWC1_IA_ENABLE);
			XE_SELECT_PAGE(0x02);
			XE_OUTB(XE_MSR, XE_INB(XE_MSR) | XE_MSR_HASH_TABLE);
		}
	} else {
		/* Enable promiscuous multicast matching */
		XE_SELECT_PAGE(0x42);
		XE_OUTB(XE_SWC1, (XE_INB(XE_SWC1) & ~XE_SWC1_IA_ENABLE) |
		    XE_SWC1_ALLMULTI);
	}

	XE_SELECT_PAGE(0);
}

/*
 * Copy the Ethernet multicast address in addr to the on-chip registers for
 * Individual Address idx.  Assumes that addr is really a multicast address
 * and that idx > 0 (slot 0 is always used for the card MAC address).
 */
static void
xe_set_addr(struct xe_softc *scp, uint8_t* addr, unsigned idx)
{
	uint8_t page, reg;
	unsigned i;

	/*
	 * Individual Addresses are stored in registers 8-F of pages
	 * 0x50-0x57.  IA1 therefore starts at register 0xE on page
	 * 0x50.  The expressions below compute the starting page and
	 * register for any IA index > 0.
	 */
	--idx;
	page = 0x50 + idx % 4 + idx / 4 * 3;
	reg = 0x0e - 2 * (idx % 4);

	DEVPRINTF(3, (scp->dev,
	    "set_addr: idx = %u, page = 0x%02x, reg = 0x%02x\n", idx + 1, page,
	    reg));

	/*
	 * Copy the IA bytes.  Note that the byte order is reversed
	 * for Mohawk and Dingo wrt. CE2 hardware.
	 */
	XE_SELECT_PAGE(page);
	for (i = 0; i < ETHER_ADDR_LEN; i++) {
		if (i > 0) {
			DPRINTF(3, (":%02x", addr[i]));
		} else {
			DEVPRINTF(3, (scp->dev, "set_addr: %02x", addr[0]));
		}
		XE_OUTB(reg, addr[scp->mohawk ? 5 - i : i]);
		if (++reg == 0x10) {
			reg = 0x08;
			XE_SELECT_PAGE(++page);
		}
	}
	DPRINTF(3, ("\n"));
}

/*
 * Set the appropriate bit in the multicast hash table for the supplied
 * Ethernet multicast address addr.  Assumes that addr is really a multicast
 * address.
 */
static void
xe_mchash(struct xe_softc* scp, const uint8_t *addr)
{
	int bit;
	uint8_t byte, hash;

	hash = ether_crc32_le(addr, ETHER_ADDR_LEN) & 0x3F;

	/*
	 * Top 3 bits of hash give register - 8, bottom 3 give bit
	 * within register.
	 */
	byte = hash >> 3 | 0x08;
	bit = 0x01 << (hash & 0x07);

	DEVPRINTF(3, (scp->dev,
	    "set_hash: hash = 0x%02x, byte = 0x%02x, bit = 0x%02x\n", hash,
	    byte, bit));

	XE_SELECT_PAGE(0x58);
	XE_OUTB(byte, XE_INB(byte) | bit);
}

/*
 * Write an outgoing packet to the card using programmed I/O.
 */
static int
xe_pio_write_packet(struct xe_softc *scp, struct mbuf *mbp)
{
	unsigned len, pad;
	unsigned char wantbyte;
	uint8_t *data;
	uint8_t savebyte[2];

	/* Get total packet length */
	if (mbp->m_flags & M_PKTHDR)
		len = mbp->m_pkthdr.len;
	else {
		struct mbuf* mbp2 = mbp;
		for (len = 0; mbp2 != NULL;
		     len += mbp2->m_len, mbp2 = mbp2->m_next);
	}

	DEVPRINTF(3, (scp->dev, "pio_write_packet: len = %u\n", len));

	/* Packets < minimum length may need to be padded out */
	pad = 0;
	if (len < scp->tx_min) {
		pad = scp->tx_min - len;
		len = scp->tx_min;
	}

	/* Check transmit buffer space */
	XE_SELECT_PAGE(0);
	XE_OUTW(XE_TRS, len + 2);	/* Only effective on rev. 1 CE2 cards */
	if ((XE_INW(XE_TSO) & 0x7fff) <= len + 2)
		return (1);

	/* Send packet length to card */
	XE_OUTW(XE_EDP, len);

	/*
	 * Write packet to card using PIO (code stolen from the ed driver)
	 */
	wantbyte = 0;
	while (mbp != NULL) {
		len = mbp->m_len;
		if (len > 0) {
			data = mtod(mbp, caddr_t);
			if (wantbyte) {		/* Finish the last word */
				savebyte[1] = *data;
				XE_OUTW(XE_EDP, *(u_short *)savebyte);
				data++;
				len--;
				wantbyte = 0;
			}
			if (len > 1) {		/* Output contiguous words */
				bus_write_multi_2(scp->port_res, XE_EDP,
				    (uint16_t *)data, len >> 1);
				data += len & ~1;
				len &= 1;
			}
			if (len == 1) {		/* Save last byte, if needed */
				savebyte[0] = *data;
				wantbyte = 1;
			}
		}
		mbp = mbp->m_next;
	}

	/*
	 * Send last byte of odd-length packets
	 */
	if (wantbyte)
		XE_OUTB(XE_EDP, savebyte[0]);

	/*
	 * Can just tell CE3 cards to send; short packets will be
	 * padded out with random cruft automatically.  For CE2,
	 * manually pad the packet with garbage; it will be sent when
	 * the required number of bytes have been delivered to the
	 * card.
	 */
	if (scp->mohawk)
		XE_OUTB(XE_CR, XE_CR_TX_PACKET | XE_CR_RESTART_TX |
		    XE_CR_ENABLE_INTR);
	else if (pad > 0) {
		if (pad & 0x01)
			XE_OUTB(XE_EDP, 0xaa);
		pad >>= 1;
		while (pad > 0) {
			XE_OUTW(XE_EDP, 0xdead);
			pad--;
		}
	}

	return (0);
}

/**************************************************************
 *                                                            *
 *                  M I I  F U N C T I O N S                  *
 *                                                            *
 **************************************************************/

/*
 * Alternative MII/PHY handling code adapted from the xl driver.  It doesn't
 * seem to work any better than the xirc2_ps stuff, but it's cleaner code.
 * XXX - this stuff shouldn't be here.  It should all be abstracted off to
 * XXX - some kind of common MII-handling code, shared by all drivers.  But
 * XXX - that's a whole other mission.
 */
#define	XE_MII_SET(x)	XE_OUTB(XE_GPR2, (XE_INB(XE_GPR2) | 0x04) | (x))
#define	XE_MII_CLR(x)	XE_OUTB(XE_GPR2, (XE_INB(XE_GPR2) | 0x04) & ~(x))

/*
 * Sync the PHYs by setting data bit and strobing the clock 32 times.
 */
static void
xe_mii_sync(struct xe_softc *scp)
{
	int i;

	XE_SELECT_PAGE(2);
	XE_MII_SET(XE_MII_DIR|XE_MII_WRD);

	for (i = 0; i < 32; i++) {
		XE_MII_SET(XE_MII_CLK);
		DELAY(1);
		XE_MII_CLR(XE_MII_CLK);
		DELAY(1);
	}
}

/*
 * Look for a MII-compliant PHY.  If we find one, reset it.
 */
static int
xe_mii_init(struct xe_softc *scp)
{
	uint16_t status;

	status = xe_phy_readreg(scp, PHY_BMSR);
	if ((status & 0xff00) != 0x7800) {
		DEVPRINTF(2, (scp->dev, "no PHY found, %0x\n", status));
		return (0);
	} else {
		DEVPRINTF(2, (scp->dev, "PHY OK!\n"));

		/* Reset the PHY */
		xe_phy_writereg(scp, PHY_BMCR, PHY_BMCR_RESET);
		DELAY(500);
		while(xe_phy_readreg(scp, PHY_BMCR) & PHY_BMCR_RESET)
			;	/* nothing */
		XE_MII_DUMP(scp);
		return (1);
	}
}

/*
 * Clock a series of bits through the MII.
 */
static void
xe_mii_send(struct xe_softc *scp, uint32_t bits, int cnt)
{
	int i;

	XE_SELECT_PAGE(2);
	XE_MII_CLR(XE_MII_CLK);
  
	for (i = (0x1 << (cnt - 1)); i; i >>= 1) {
		if (bits & i) {
			XE_MII_SET(XE_MII_WRD);
		} else {
			XE_MII_CLR(XE_MII_WRD);
		}
		DELAY(1);
		XE_MII_CLR(XE_MII_CLK);
		DELAY(1);
		XE_MII_SET(XE_MII_CLK);
	}
}

/*
 * Read an PHY register through the MII.
 */
static int
xe_mii_readreg(struct xe_softc *scp, struct xe_mii_frame *frame)
{
	int i, ack;

	XE_ASSERT_LOCKED(scp);

	/*
	 * Set up frame for RX.
	 */
	frame->mii_stdelim = XE_MII_STARTDELIM;
	frame->mii_opcode = XE_MII_READOP;
	frame->mii_turnaround = 0;
	frame->mii_data = 0;
	
	XE_SELECT_PAGE(2);
	XE_OUTB(XE_GPR2, 0);

	/*
	 * Turn on data xmit.
	 */
	XE_MII_SET(XE_MII_DIR);

	xe_mii_sync(scp);

	/*	
	 * Send command/address info.
	 */
	xe_mii_send(scp, frame->mii_stdelim, 2);
	xe_mii_send(scp, frame->mii_opcode, 2);
	xe_mii_send(scp, frame->mii_phyaddr, 5);
	xe_mii_send(scp, frame->mii_regaddr, 5);

	/* Idle bit */
	XE_MII_CLR((XE_MII_CLK|XE_MII_WRD));
	DELAY(1);
	XE_MII_SET(XE_MII_CLK);
	DELAY(1);

	/* Turn off xmit. */
	XE_MII_CLR(XE_MII_DIR);

	/* Check for ack */
	XE_MII_CLR(XE_MII_CLK);
	DELAY(1);
	ack = XE_INB(XE_GPR2) & XE_MII_RDD;
	XE_MII_SET(XE_MII_CLK);
	DELAY(1);

	/*
	 * Now try reading data bits. If the ack failed, we still
	 * need to clock through 16 cycles to keep the PHY(s) in sync.
	 */
	if (ack) {
		for(i = 0; i < 16; i++) {
			XE_MII_CLR(XE_MII_CLK);
			DELAY(1);
			XE_MII_SET(XE_MII_CLK);
			DELAY(1);
		}
		goto fail;
	}

	for (i = 0x8000; i; i >>= 1) {
		XE_MII_CLR(XE_MII_CLK);
		DELAY(1);
		if (!ack) {
			if (XE_INB(XE_GPR2) & XE_MII_RDD)
				frame->mii_data |= i;
			DELAY(1);
		}
		XE_MII_SET(XE_MII_CLK);
		DELAY(1);
	}

fail:
	XE_MII_CLR(XE_MII_CLK);
	DELAY(1);
	XE_MII_SET(XE_MII_CLK);
	DELAY(1);

	if (ack)
		return(1);
	return(0);
}

/*
 * Write to a PHY register through the MII.
 */
static int
xe_mii_writereg(struct xe_softc *scp, struct xe_mii_frame *frame)
{

	XE_ASSERT_LOCKED(scp);

	/*
	 * Set up frame for TX.
	 */
	frame->mii_stdelim = XE_MII_STARTDELIM;
	frame->mii_opcode = XE_MII_WRITEOP;
	frame->mii_turnaround = XE_MII_TURNAROUND;
	
	XE_SELECT_PAGE(2);

	/*		
	 * Turn on data output.
	 */
	XE_MII_SET(XE_MII_DIR);

	xe_mii_sync(scp);

	xe_mii_send(scp, frame->mii_stdelim, 2);
	xe_mii_send(scp, frame->mii_opcode, 2);
	xe_mii_send(scp, frame->mii_phyaddr, 5);
	xe_mii_send(scp, frame->mii_regaddr, 5);
	xe_mii_send(scp, frame->mii_turnaround, 2);
	xe_mii_send(scp, frame->mii_data, 16);

	/* Idle bit. */
	XE_MII_SET(XE_MII_CLK);
	DELAY(1);
	XE_MII_CLR(XE_MII_CLK);
	DELAY(1);

	/*
	 * Turn off xmit.
	 */
	XE_MII_CLR(XE_MII_DIR);

	return(0);
}

/*
 * Read a register from the PHY.
 */
static uint16_t
xe_phy_readreg(struct xe_softc *scp, uint16_t reg)
{
	struct xe_mii_frame frame;

	bzero((char *)&frame, sizeof(frame));

	frame.mii_phyaddr = 0;
	frame.mii_regaddr = reg;
	xe_mii_readreg(scp, &frame);

	return (frame.mii_data);
}

/*
 * Write to a PHY register.
 */
static void
xe_phy_writereg(struct xe_softc *scp, uint16_t reg, uint16_t data)
{
	struct xe_mii_frame frame;

	bzero((char *)&frame, sizeof(frame));

	frame.mii_phyaddr = 0;
	frame.mii_regaddr = reg;
	frame.mii_data = data;
	xe_mii_writereg(scp, &frame);
}

/*
 * A bit of debugging code.
 */
static void
xe_mii_dump(struct xe_softc *scp)
{
	int i;

	device_printf(scp->dev, "MII registers: ");
	for (i = 0; i < 2; i++) {
		printf(" %d:%04x", i, xe_phy_readreg(scp, i));
	}
	for (i = 4; i < 7; i++) {
		printf(" %d:%04x", i, xe_phy_readreg(scp, i));
	}
	printf("\n");
}

#if 0
void
xe_reg_dump(struct xe_softc *scp)
{
	int page, i;

	device_printf(scp->dev, "Common registers: ");
	for (i = 0; i < 8; i++) {
		printf(" %2.2x", XE_INB(i));
	}
	printf("\n");

	for (page = 0; page <= 8; page++) {
		device_printf(scp->dev, "Register page %2.2x: ", page);
		XE_SELECT_PAGE(page);
		for (i = 8; i < 16; i++) {
			printf(" %2.2x", XE_INB(i));
		}
		printf("\n");
	}

	for (page = 0x10; page < 0x5f; page++) {
		if ((page >= 0x11 && page <= 0x3f) ||
		    (page == 0x41) ||
		    (page >= 0x43 && page <= 0x4f) ||
		    (page >= 0x59))
			continue;
		device_printf(scp->dev, "Register page %2.2x: ", page);
		XE_SELECT_PAGE(page);
		for (i = 8; i < 16; i++) {
			printf(" %2.2x", XE_INB(i));
		}
		printf("\n");
	}
}
#endif

int
xe_activate(device_t dev)
{
	struct xe_softc *sc = device_get_softc(dev);
	int start, i;

	DEVPRINTF(2, (dev, "activate\n"));

	if (!sc->modem) {
		sc->port_rid = 0;	/* 0 is managed by pccard */
		sc->port_res = bus_alloc_resource_anywhere(dev, SYS_RES_IOPORT,
		    &sc->port_rid, 16, RF_ACTIVE);
	} else if (sc->dingo) {
		/*
		 * Find a 16 byte aligned ioport for the card.
		 */
		DEVPRINTF(1, (dev, "Finding an aligned port for RealPort\n"));
		sc->port_rid = 1;	/* 0 is managed by pccard */
		start = 0x100;
		do {
			sc->port_res = bus_alloc_resource(dev, SYS_RES_IOPORT,
			    &sc->port_rid, start, 0x3ff, 16, RF_ACTIVE);
			if (sc->port_res == NULL)
				break;
			if ((rman_get_start(sc->port_res) & 0xf) == 0)
				break;
			bus_release_resource(dev, SYS_RES_IOPORT, sc->port_rid, 
			    sc->port_res);
			start = (rman_get_start(sc->port_res) + 15) & ~0xf;
		} while (1);
		DEVPRINTF(1, (dev, "RealPort port 0x%0jx, size 0x%0jx\n",
		    bus_get_resource_start(dev, SYS_RES_IOPORT, sc->port_rid),
		    bus_get_resource_count(dev, SYS_RES_IOPORT, sc->port_rid)));
	} else if (sc->ce2) {
		/*
		 * Find contiguous I/O port for the Ethernet function
		 * on CEM2 and CEM3 cards.  We allocate window 0
		 * wherever pccard has decided it should be, then find
		 * an available window adjacent to it for the second
		 * function.  Not sure that both windows are actually
		 * needed.
		 */
		DEVPRINTF(1, (dev, "Finding I/O port for CEM2/CEM3\n"));
		sc->ce2_port_rid = 0;	/* 0 is managed by pccard */
		sc->ce2_port_res = bus_alloc_resource_anywhere(dev,
		    SYS_RES_IOPORT, &sc->ce2_port_rid, 8, RF_ACTIVE);
		if (sc->ce2_port_res == NULL) {
			DEVPRINTF(1, (dev,
			    "Cannot allocate I/O port for modem\n"));
			xe_deactivate(dev);
			return (ENOMEM);
		}

		sc->port_rid = 1;
		start = bus_get_resource_start(dev, SYS_RES_IOPORT,
		    sc->ce2_port_rid);
		for (i = 0; i < 2; i++) {
			start += (i == 0 ? 8 : -24);
			sc->port_res = bus_alloc_resource(dev, SYS_RES_IOPORT,
			    &sc->port_rid, start, start + 15, 16, RF_ACTIVE);
			if (sc->port_res == NULL)
				continue;
			if (bus_get_resource_start(dev, SYS_RES_IOPORT,
			    sc->port_rid) == start)
				break;

			bus_release_resource(dev, SYS_RES_IOPORT, sc->port_rid,
			    sc->port_res);
			sc->port_res = NULL;
		}
		DEVPRINTF(1, (dev, "CEM2/CEM3 port 0x%0jx, size 0x%0jx\n",
		    bus_get_resource_start(dev, SYS_RES_IOPORT, sc->port_rid),
		    bus_get_resource_count(dev, SYS_RES_IOPORT, sc->port_rid)));
	}

	if (!sc->port_res) {
		DEVPRINTF(1, (dev, "Cannot allocate ioport\n"));
		xe_deactivate(dev);
		return (ENOMEM);
	}

	sc->irq_rid = 0;
	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->irq_rid, 
	    RF_ACTIVE);
	if (sc->irq_res == NULL) {
		DEVPRINTF(1, (dev, "Cannot allocate irq\n"));
		xe_deactivate(dev);
		return (ENOMEM);
	}

	return (0);
}

void
xe_deactivate(device_t dev)
{
	struct xe_softc *sc = device_get_softc(dev);
	
	DEVPRINTF(2, (dev, "deactivate\n"));
	if (sc->intrhand)
		bus_teardown_intr(dev, sc->irq_res, sc->intrhand);
	sc->intrhand = NULL;
	if (sc->port_res)
		bus_release_resource(dev, SYS_RES_IOPORT, sc->port_rid, 
		    sc->port_res);
	sc->port_res = NULL;
	if (sc->ce2_port_res)
	    bus_release_resource(dev, SYS_RES_IOPORT, sc->ce2_port_rid,
		sc->ce2_port_res);
	sc->ce2_port_res = NULL;
	if (sc->irq_res)
		bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid, 
		    sc->irq_res);
	sc->irq_res = NULL;
	if (sc->ifp)
		if_free(sc->ifp);
	sc->ifp = NULL;
}
