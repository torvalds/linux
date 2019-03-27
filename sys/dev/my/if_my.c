/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Written by: yen_cw@myson.com.tw
 * Copyright (c) 2002 Myson Technology Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Myson fast ethernet PCI NIC driver, available at: http://www.myson.com.tw/
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/module.h>
#include <sys/lock.h>
#include <sys/mutex.h>

#define NBPFILTER	1

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_media.h>
#include <net/if_types.h>
#include <net/if_dl.h>
#include <net/bpf.h>

#include <vm/vm.h>		/* for vtophys */
#include <vm/pmap.h>		/* for vtophys */
#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

/*
 * #define MY_USEIOSPACE
 */

static int      MY_USEIOSPACE = 1;

#ifdef MY_USEIOSPACE
#define MY_RES                  SYS_RES_IOPORT
#define MY_RID                  MY_PCI_LOIO
#else
#define MY_RES                  SYS_RES_MEMORY
#define MY_RID                  MY_PCI_LOMEM
#endif


#include <dev/my/if_myreg.h>

/*
 * Various supported device vendors/types and their names.
 */
struct my_type *my_info_tmp;
static struct my_type my_devs[] = {
	{MYSONVENDORID, MTD800ID, "Myson MTD80X Based Fast Ethernet Card"},
	{MYSONVENDORID, MTD803ID, "Myson MTD80X Based Fast Ethernet Card"},
	{MYSONVENDORID, MTD891ID, "Myson MTD89X Based Giga Ethernet Card"},
	{0, 0, NULL}
};

/*
 * Various supported PHY vendors/types and their names. Note that this driver
 * will work with pretty much any MII-compliant PHY, so failure to positively
 * identify the chip is not a fatal error.
 */
static struct my_type my_phys[] = {
	{MysonPHYID0, MysonPHYID0, "<MYSON MTD981>"},
	{SeeqPHYID0, SeeqPHYID0, "<SEEQ 80225>"},
	{AhdocPHYID0, AhdocPHYID0, "<AHDOC 101>"},
	{MarvellPHYID0, MarvellPHYID0, "<MARVELL 88E1000>"},
	{LevelOnePHYID0, LevelOnePHYID0, "<LevelOne LXT1000>"},
	{0, 0, "<MII-compliant physical interface>"}
};

static int      my_probe(device_t);
static int      my_attach(device_t);
static int      my_detach(device_t);
static int      my_newbuf(struct my_softc *, struct my_chain_onefrag *);
static int      my_encap(struct my_softc *, struct my_chain *, struct mbuf *);
static void     my_rxeof(struct my_softc *);
static void     my_txeof(struct my_softc *);
static void     my_txeoc(struct my_softc *);
static void     my_intr(void *);
static void     my_start(struct ifnet *);
static void     my_start_locked(struct ifnet *);
static int      my_ioctl(struct ifnet *, u_long, caddr_t);
static void     my_init(void *);
static void     my_init_locked(struct my_softc *);
static void     my_stop(struct my_softc *);
static void     my_autoneg_timeout(void *);
static void     my_watchdog(void *);
static int      my_shutdown(device_t);
static int      my_ifmedia_upd(struct ifnet *);
static void     my_ifmedia_sts(struct ifnet *, struct ifmediareq *);
static u_int16_t my_phy_readreg(struct my_softc *, int);
static void     my_phy_writereg(struct my_softc *, int, int);
static void     my_autoneg_xmit(struct my_softc *);
static void     my_autoneg_mii(struct my_softc *, int, int);
static void     my_setmode_mii(struct my_softc *, int);
static void     my_getmode_mii(struct my_softc *);
static void     my_setcfg(struct my_softc *, int);
static void     my_setmulti(struct my_softc *);
static void     my_reset(struct my_softc *);
static int      my_list_rx_init(struct my_softc *);
static int      my_list_tx_init(struct my_softc *);
static long     my_send_cmd_to_phy(struct my_softc *, int, int);

#define MY_SETBIT(sc, reg, x) CSR_WRITE_4(sc, reg, CSR_READ_4(sc, reg) | (x))
#define MY_CLRBIT(sc, reg, x) CSR_WRITE_4(sc, reg, CSR_READ_4(sc, reg) & ~(x))

static device_method_t my_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, my_probe),
	DEVMETHOD(device_attach, my_attach),
	DEVMETHOD(device_detach, my_detach),
	DEVMETHOD(device_shutdown, my_shutdown),

	DEVMETHOD_END
};

static driver_t my_driver = {
	"my",
	my_methods,
	sizeof(struct my_softc)
};

static devclass_t my_devclass;

DRIVER_MODULE(my, pci, my_driver, my_devclass, 0, 0);
MODULE_PNP_INFO("U16:vendor;U16:device;D:#", pci, my, my_devs,
    nitems(my_devs) - 1);
MODULE_DEPEND(my, pci, 1, 1, 1);
MODULE_DEPEND(my, ether, 1, 1, 1);

static long
my_send_cmd_to_phy(struct my_softc * sc, int opcode, int regad)
{
	long            miir;
	int             i;
	int             mask, data;

	MY_LOCK_ASSERT(sc);

	/* enable MII output */
	miir = CSR_READ_4(sc, MY_MANAGEMENT);
	miir &= 0xfffffff0;

	miir |= MY_MASK_MIIR_MII_WRITE + MY_MASK_MIIR_MII_MDO;

	/* send 32 1's preamble */
	for (i = 0; i < 32; i++) {
		/* low MDC; MDO is already high (miir) */
		miir &= ~MY_MASK_MIIR_MII_MDC;
		CSR_WRITE_4(sc, MY_MANAGEMENT, miir);

		/* high MDC */
		miir |= MY_MASK_MIIR_MII_MDC;
		CSR_WRITE_4(sc, MY_MANAGEMENT, miir);
	}

	/* calculate ST+OP+PHYAD+REGAD+TA */
	data = opcode | (sc->my_phy_addr << 7) | (regad << 2);

	/* sent out */
	mask = 0x8000;
	while (mask) {
		/* low MDC, prepare MDO */
		miir &= ~(MY_MASK_MIIR_MII_MDC + MY_MASK_MIIR_MII_MDO);
		if (mask & data)
			miir |= MY_MASK_MIIR_MII_MDO;

		CSR_WRITE_4(sc, MY_MANAGEMENT, miir);
		/* high MDC */
		miir |= MY_MASK_MIIR_MII_MDC;
		CSR_WRITE_4(sc, MY_MANAGEMENT, miir);
		DELAY(30);

		/* next */
		mask >>= 1;
		if (mask == 0x2 && opcode == MY_OP_READ)
			miir &= ~MY_MASK_MIIR_MII_WRITE;
	}

	return miir;
}


static u_int16_t
my_phy_readreg(struct my_softc * sc, int reg)
{
	long            miir;
	int             mask, data;

	MY_LOCK_ASSERT(sc);

	if (sc->my_info->my_did == MTD803ID)
		data = CSR_READ_2(sc, MY_PHYBASE + reg * 2);
	else {
		miir = my_send_cmd_to_phy(sc, MY_OP_READ, reg);

		/* read data */
		mask = 0x8000;
		data = 0;
		while (mask) {
			/* low MDC */
			miir &= ~MY_MASK_MIIR_MII_MDC;
			CSR_WRITE_4(sc, MY_MANAGEMENT, miir);

			/* read MDI */
			miir = CSR_READ_4(sc, MY_MANAGEMENT);
			if (miir & MY_MASK_MIIR_MII_MDI)
				data |= mask;

			/* high MDC, and wait */
			miir |= MY_MASK_MIIR_MII_MDC;
			CSR_WRITE_4(sc, MY_MANAGEMENT, miir);
			DELAY(30);

			/* next */
			mask >>= 1;
		}

		/* low MDC */
		miir &= ~MY_MASK_MIIR_MII_MDC;
		CSR_WRITE_4(sc, MY_MANAGEMENT, miir);
	}

	return (u_int16_t) data;
}


static void
my_phy_writereg(struct my_softc * sc, int reg, int data)
{
	long            miir;
	int             mask;

	MY_LOCK_ASSERT(sc);

	if (sc->my_info->my_did == MTD803ID)
		CSR_WRITE_2(sc, MY_PHYBASE + reg * 2, data);
	else {
		miir = my_send_cmd_to_phy(sc, MY_OP_WRITE, reg);

		/* write data */
		mask = 0x8000;
		while (mask) {
			/* low MDC, prepare MDO */
			miir &= ~(MY_MASK_MIIR_MII_MDC + MY_MASK_MIIR_MII_MDO);
			if (mask & data)
				miir |= MY_MASK_MIIR_MII_MDO;
			CSR_WRITE_4(sc, MY_MANAGEMENT, miir);
			DELAY(1);

			/* high MDC */
			miir |= MY_MASK_MIIR_MII_MDC;
			CSR_WRITE_4(sc, MY_MANAGEMENT, miir);
			DELAY(1);

			/* next */
			mask >>= 1;
		}

		/* low MDC */
		miir &= ~MY_MASK_MIIR_MII_MDC;
		CSR_WRITE_4(sc, MY_MANAGEMENT, miir);
	}
	return;
}


/*
 * Program the 64-bit multicast hash filter.
 */
static void
my_setmulti(struct my_softc * sc)
{
	struct ifnet   *ifp;
	int             h = 0;
	u_int32_t       hashes[2] = {0, 0};
	struct ifmultiaddr *ifma;
	u_int32_t       rxfilt;
	int             mcnt = 0;

	MY_LOCK_ASSERT(sc);

	ifp = sc->my_ifp;

	rxfilt = CSR_READ_4(sc, MY_TCRRCR);

	if (ifp->if_flags & IFF_ALLMULTI || ifp->if_flags & IFF_PROMISC) {
		rxfilt |= MY_AM;
		CSR_WRITE_4(sc, MY_TCRRCR, rxfilt);
		CSR_WRITE_4(sc, MY_MAR0, 0xFFFFFFFF);
		CSR_WRITE_4(sc, MY_MAR1, 0xFFFFFFFF);

		return;
	}
	/* first, zot all the existing hash bits */
	CSR_WRITE_4(sc, MY_MAR0, 0);
	CSR_WRITE_4(sc, MY_MAR1, 0);

	/* now program new ones */
	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		h = ~ether_crc32_be(LLADDR((struct sockaddr_dl *)
		    ifma->ifma_addr), ETHER_ADDR_LEN) >> 26;
		if (h < 32)
			hashes[0] |= (1 << h);
		else
			hashes[1] |= (1 << (h - 32));
		mcnt++;
	}
	if_maddr_runlock(ifp);

	if (mcnt)
		rxfilt |= MY_AM;
	else
		rxfilt &= ~MY_AM;
	CSR_WRITE_4(sc, MY_MAR0, hashes[0]);
	CSR_WRITE_4(sc, MY_MAR1, hashes[1]);
	CSR_WRITE_4(sc, MY_TCRRCR, rxfilt);
	return;
}

/*
 * Initiate an autonegotiation session.
 */
static void
my_autoneg_xmit(struct my_softc * sc)
{
	u_int16_t       phy_sts = 0;

	MY_LOCK_ASSERT(sc);

	my_phy_writereg(sc, PHY_BMCR, PHY_BMCR_RESET);
	DELAY(500);
	while (my_phy_readreg(sc, PHY_BMCR) & PHY_BMCR_RESET);

	phy_sts = my_phy_readreg(sc, PHY_BMCR);
	phy_sts |= PHY_BMCR_AUTONEGENBL | PHY_BMCR_AUTONEGRSTR;
	my_phy_writereg(sc, PHY_BMCR, phy_sts);

	return;
}

static void
my_autoneg_timeout(void *arg)
{
	struct my_softc *sc;

	sc = arg;
	MY_LOCK_ASSERT(sc);
	my_autoneg_mii(sc, MY_FLAG_DELAYTIMEO, 1);
}

/*
 * Invoke autonegotiation on a PHY.
 */
static void
my_autoneg_mii(struct my_softc * sc, int flag, int verbose)
{
	u_int16_t       phy_sts = 0, media, advert, ability;
	u_int16_t       ability2 = 0;
	struct ifnet   *ifp;
	struct ifmedia *ifm;

	MY_LOCK_ASSERT(sc);

	ifm = &sc->ifmedia;
	ifp = sc->my_ifp;

	ifm->ifm_media = IFM_ETHER | IFM_AUTO;

#ifndef FORCE_AUTONEG_TFOUR
	/*
	 * First, see if autoneg is supported. If not, there's no point in
	 * continuing.
	 */
	phy_sts = my_phy_readreg(sc, PHY_BMSR);
	if (!(phy_sts & PHY_BMSR_CANAUTONEG)) {
		if (verbose)
			device_printf(sc->my_dev,
			    "autonegotiation not supported\n");
		ifm->ifm_media = IFM_ETHER | IFM_10_T | IFM_HDX;
		return;
	}
#endif
	switch (flag) {
	case MY_FLAG_FORCEDELAY:
		/*
		 * XXX Never use this option anywhere but in the probe
		 * routine: making the kernel stop dead in its tracks for
		 * three whole seconds after we've gone multi-user is really
		 * bad manners.
		 */
		my_autoneg_xmit(sc);
		DELAY(5000000);
		break;
	case MY_FLAG_SCHEDDELAY:
		/*
		 * Wait for the transmitter to go idle before starting an
		 * autoneg session, otherwise my_start() may clobber our
		 * timeout, and we don't want to allow transmission during an
		 * autoneg session since that can screw it up.
		 */
		if (sc->my_cdata.my_tx_head != NULL) {
			sc->my_want_auto = 1;
			MY_UNLOCK(sc);
			return;
		}
		my_autoneg_xmit(sc);
		callout_reset(&sc->my_autoneg_timer, hz * 5, my_autoneg_timeout,
		    sc);
		sc->my_autoneg = 1;
		sc->my_want_auto = 0;
		return;
	case MY_FLAG_DELAYTIMEO:
		callout_stop(&sc->my_autoneg_timer);
		sc->my_autoneg = 0;
		break;
	default:
		device_printf(sc->my_dev, "invalid autoneg flag: %d\n", flag);
		return;
	}

	if (my_phy_readreg(sc, PHY_BMSR) & PHY_BMSR_AUTONEGCOMP) {
		if (verbose)
			device_printf(sc->my_dev, "autoneg complete, ");
		phy_sts = my_phy_readreg(sc, PHY_BMSR);
	} else {
		if (verbose)
			device_printf(sc->my_dev, "autoneg not complete, ");
	}

	media = my_phy_readreg(sc, PHY_BMCR);

	/* Link is good. Report modes and set duplex mode. */
	if (my_phy_readreg(sc, PHY_BMSR) & PHY_BMSR_LINKSTAT) {
		if (verbose)
			device_printf(sc->my_dev, "link status good. ");
		advert = my_phy_readreg(sc, PHY_ANAR);
		ability = my_phy_readreg(sc, PHY_LPAR);
		if ((sc->my_pinfo->my_vid == MarvellPHYID0) ||
		    (sc->my_pinfo->my_vid == LevelOnePHYID0)) {
			ability2 = my_phy_readreg(sc, PHY_1000SR);
			if (ability2 & PHY_1000SR_1000BTXFULL) {
				advert = 0;
				ability = 0;
				/*
				 * this version did not support 1000M,
				 * ifm->ifm_media =
				 * IFM_ETHER|IFM_1000_T|IFM_FDX;
				 */
				ifm->ifm_media =
				    IFM_ETHER | IFM_100_TX | IFM_FDX;
				media &= ~PHY_BMCR_SPEEDSEL;
				media |= PHY_BMCR_1000;
				media |= PHY_BMCR_DUPLEX;
				printf("(full-duplex, 1000Mbps)\n");
			} else if (ability2 & PHY_1000SR_1000BTXHALF) {
				advert = 0;
				ability = 0;
				/*
				 * this version did not support 1000M,
				 * ifm->ifm_media = IFM_ETHER|IFM_1000_T;
				 */
				ifm->ifm_media = IFM_ETHER | IFM_100_TX;
				media &= ~PHY_BMCR_SPEEDSEL;
				media &= ~PHY_BMCR_DUPLEX;
				media |= PHY_BMCR_1000;
				printf("(half-duplex, 1000Mbps)\n");
			}
		}
		if (advert & PHY_ANAR_100BT4 && ability & PHY_ANAR_100BT4) {
			ifm->ifm_media = IFM_ETHER | IFM_100_T4;
			media |= PHY_BMCR_SPEEDSEL;
			media &= ~PHY_BMCR_DUPLEX;
			printf("(100baseT4)\n");
		} else if (advert & PHY_ANAR_100BTXFULL &&
			   ability & PHY_ANAR_100BTXFULL) {
			ifm->ifm_media = IFM_ETHER | IFM_100_TX | IFM_FDX;
			media |= PHY_BMCR_SPEEDSEL;
			media |= PHY_BMCR_DUPLEX;
			printf("(full-duplex, 100Mbps)\n");
		} else if (advert & PHY_ANAR_100BTXHALF &&
			   ability & PHY_ANAR_100BTXHALF) {
			ifm->ifm_media = IFM_ETHER | IFM_100_TX | IFM_HDX;
			media |= PHY_BMCR_SPEEDSEL;
			media &= ~PHY_BMCR_DUPLEX;
			printf("(half-duplex, 100Mbps)\n");
		} else if (advert & PHY_ANAR_10BTFULL &&
			   ability & PHY_ANAR_10BTFULL) {
			ifm->ifm_media = IFM_ETHER | IFM_10_T | IFM_FDX;
			media &= ~PHY_BMCR_SPEEDSEL;
			media |= PHY_BMCR_DUPLEX;
			printf("(full-duplex, 10Mbps)\n");
		} else if (advert) {
			ifm->ifm_media = IFM_ETHER | IFM_10_T | IFM_HDX;
			media &= ~PHY_BMCR_SPEEDSEL;
			media &= ~PHY_BMCR_DUPLEX;
			printf("(half-duplex, 10Mbps)\n");
		}
		media &= ~PHY_BMCR_AUTONEGENBL;

		/* Set ASIC's duplex mode to match the PHY. */
		my_phy_writereg(sc, PHY_BMCR, media);
		my_setcfg(sc, media);
	} else {
		if (verbose)
			device_printf(sc->my_dev, "no carrier\n");
	}

	my_init_locked(sc);
	if (sc->my_tx_pend) {
		sc->my_autoneg = 0;
		sc->my_tx_pend = 0;
		my_start_locked(ifp);
	}
	return;
}

/*
 * To get PHY ability.
 */
static void
my_getmode_mii(struct my_softc * sc)
{
	u_int16_t       bmsr;
	struct ifnet   *ifp;

	MY_LOCK_ASSERT(sc);
	ifp = sc->my_ifp;
	bmsr = my_phy_readreg(sc, PHY_BMSR);
	if (bootverbose)
		device_printf(sc->my_dev, "PHY status word: %x\n", bmsr);

	/* fallback */
	sc->ifmedia.ifm_media = IFM_ETHER | IFM_10_T | IFM_HDX;

	if (bmsr & PHY_BMSR_10BTHALF) {
		if (bootverbose)
			device_printf(sc->my_dev,
			    "10Mbps half-duplex mode supported\n");
		ifmedia_add(&sc->ifmedia, IFM_ETHER | IFM_10_T | IFM_HDX,
		    0, NULL);
		ifmedia_add(&sc->ifmedia, IFM_ETHER | IFM_10_T, 0, NULL);
	}
	if (bmsr & PHY_BMSR_10BTFULL) {
		if (bootverbose)
			device_printf(sc->my_dev,
			    "10Mbps full-duplex mode supported\n");

		ifmedia_add(&sc->ifmedia, IFM_ETHER | IFM_10_T | IFM_FDX,
		    0, NULL);
		sc->ifmedia.ifm_media = IFM_ETHER | IFM_10_T | IFM_FDX;
	}
	if (bmsr & PHY_BMSR_100BTXHALF) {
		if (bootverbose)
			device_printf(sc->my_dev,
			    "100Mbps half-duplex mode supported\n");
		ifp->if_baudrate = 100000000;
		ifmedia_add(&sc->ifmedia, IFM_ETHER | IFM_100_TX, 0, NULL);
		ifmedia_add(&sc->ifmedia, IFM_ETHER | IFM_100_TX | IFM_HDX,
			    0, NULL);
		sc->ifmedia.ifm_media = IFM_ETHER | IFM_100_TX | IFM_HDX;
	}
	if (bmsr & PHY_BMSR_100BTXFULL) {
		if (bootverbose)
			device_printf(sc->my_dev,
			    "100Mbps full-duplex mode supported\n");
		ifp->if_baudrate = 100000000;
		ifmedia_add(&sc->ifmedia, IFM_ETHER | IFM_100_TX | IFM_FDX,
		    0, NULL);
		sc->ifmedia.ifm_media = IFM_ETHER | IFM_100_TX | IFM_FDX;
	}
	/* Some also support 100BaseT4. */
	if (bmsr & PHY_BMSR_100BT4) {
		if (bootverbose)
			device_printf(sc->my_dev, "100baseT4 mode supported\n");
		ifp->if_baudrate = 100000000;
		ifmedia_add(&sc->ifmedia, IFM_ETHER | IFM_100_T4, 0, NULL);
		sc->ifmedia.ifm_media = IFM_ETHER | IFM_100_T4;
#ifdef FORCE_AUTONEG_TFOUR
		if (bootverbose)
			device_printf(sc->my_dev,
			    "forcing on autoneg support for BT4\n");
		ifmedia_add(&sc->ifmedia, IFM_ETHER | IFM_AUTO, 0 NULL):
		sc->ifmedia.ifm_media = IFM_ETHER | IFM_AUTO;
#endif
	}
#if 0				/* this version did not support 1000M, */
	if (sc->my_pinfo->my_vid == MarvellPHYID0) {
		if (bootverbose)
			device_printf(sc->my_dev,
			    "1000Mbps half-duplex mode supported\n");

		ifp->if_baudrate = 1000000000;
		ifmedia_add(&sc->ifmedia, IFM_ETHER | IFM_1000_T, 0, NULL);
		ifmedia_add(&sc->ifmedia, IFM_ETHER | IFM_1000_T | IFM_HDX,
		    0, NULL);
		if (bootverbose)
			device_printf(sc->my_dev,
			    "1000Mbps full-duplex mode supported\n");
		ifp->if_baudrate = 1000000000;
		ifmedia_add(&sc->ifmedia, IFM_ETHER | IFM_1000_T | IFM_FDX,
		    0, NULL);
		sc->ifmedia.ifm_media = IFM_ETHER | IFM_1000_T | IFM_FDX;
	}
#endif
	if (bmsr & PHY_BMSR_CANAUTONEG) {
		if (bootverbose)
			device_printf(sc->my_dev, "autoneg supported\n");
		ifmedia_add(&sc->ifmedia, IFM_ETHER | IFM_AUTO, 0, NULL);
		sc->ifmedia.ifm_media = IFM_ETHER | IFM_AUTO;
	}
	return;
}

/*
 * Set speed and duplex mode.
 */
static void
my_setmode_mii(struct my_softc * sc, int media)
{
	u_int16_t       bmcr;

	MY_LOCK_ASSERT(sc);
	/*
	 * If an autoneg session is in progress, stop it.
	 */
	if (sc->my_autoneg) {
		device_printf(sc->my_dev, "canceling autoneg session\n");
		callout_stop(&sc->my_autoneg_timer);
		sc->my_autoneg = sc->my_want_auto = 0;
		bmcr = my_phy_readreg(sc, PHY_BMCR);
		bmcr &= ~PHY_BMCR_AUTONEGENBL;
		my_phy_writereg(sc, PHY_BMCR, bmcr);
	}
	device_printf(sc->my_dev, "selecting MII, ");
	bmcr = my_phy_readreg(sc, PHY_BMCR);
	bmcr &= ~(PHY_BMCR_AUTONEGENBL | PHY_BMCR_SPEEDSEL | PHY_BMCR_1000 |
		  PHY_BMCR_DUPLEX | PHY_BMCR_LOOPBK);

#if 0				/* this version did not support 1000M, */
	if (IFM_SUBTYPE(media) == IFM_1000_T) {
		printf("1000Mbps/T4, half-duplex\n");
		bmcr &= ~PHY_BMCR_SPEEDSEL;
		bmcr &= ~PHY_BMCR_DUPLEX;
		bmcr |= PHY_BMCR_1000;
	}
#endif
	if (IFM_SUBTYPE(media) == IFM_100_T4) {
		printf("100Mbps/T4, half-duplex\n");
		bmcr |= PHY_BMCR_SPEEDSEL;
		bmcr &= ~PHY_BMCR_DUPLEX;
	}
	if (IFM_SUBTYPE(media) == IFM_100_TX) {
		printf("100Mbps, ");
		bmcr |= PHY_BMCR_SPEEDSEL;
	}
	if (IFM_SUBTYPE(media) == IFM_10_T) {
		printf("10Mbps, ");
		bmcr &= ~PHY_BMCR_SPEEDSEL;
	}
	if ((media & IFM_GMASK) == IFM_FDX) {
		printf("full duplex\n");
		bmcr |= PHY_BMCR_DUPLEX;
	} else {
		printf("half duplex\n");
		bmcr &= ~PHY_BMCR_DUPLEX;
	}
	my_phy_writereg(sc, PHY_BMCR, bmcr);
	my_setcfg(sc, bmcr);
	return;
}

/*
 * The Myson manual states that in order to fiddle with the 'full-duplex' and
 * '100Mbps' bits in the netconfig register, we first have to put the
 * transmit and/or receive logic in the idle state.
 */
static void
my_setcfg(struct my_softc * sc, int bmcr)
{
	int             i, restart = 0;

	MY_LOCK_ASSERT(sc);
	if (CSR_READ_4(sc, MY_TCRRCR) & (MY_TE | MY_RE)) {
		restart = 1;
		MY_CLRBIT(sc, MY_TCRRCR, (MY_TE | MY_RE));
		for (i = 0; i < MY_TIMEOUT; i++) {
			DELAY(10);
			if (!(CSR_READ_4(sc, MY_TCRRCR) &
			    (MY_TXRUN | MY_RXRUN)))
				break;
		}
		if (i == MY_TIMEOUT)
			device_printf(sc->my_dev,
			    "failed to force tx and rx to idle \n");
	}
	MY_CLRBIT(sc, MY_TCRRCR, MY_PS1000);
	MY_CLRBIT(sc, MY_TCRRCR, MY_PS10);
	if (bmcr & PHY_BMCR_1000)
		MY_SETBIT(sc, MY_TCRRCR, MY_PS1000);
	else if (!(bmcr & PHY_BMCR_SPEEDSEL))
		MY_SETBIT(sc, MY_TCRRCR, MY_PS10);
	if (bmcr & PHY_BMCR_DUPLEX)
		MY_SETBIT(sc, MY_TCRRCR, MY_FD);
	else
		MY_CLRBIT(sc, MY_TCRRCR, MY_FD);
	if (restart)
		MY_SETBIT(sc, MY_TCRRCR, MY_TE | MY_RE);
	return;
}

static void
my_reset(struct my_softc * sc)
{
	int    i;

	MY_LOCK_ASSERT(sc);
	MY_SETBIT(sc, MY_BCR, MY_SWR);
	for (i = 0; i < MY_TIMEOUT; i++) {
		DELAY(10);
		if (!(CSR_READ_4(sc, MY_BCR) & MY_SWR))
			break;
	}
	if (i == MY_TIMEOUT)
		device_printf(sc->my_dev, "reset never completed!\n");

	/* Wait a little while for the chip to get its brains in order. */
	DELAY(1000);
	return;
}

/*
 * Probe for a Myson chip. Check the PCI vendor and device IDs against our
 * list and return a device name if we find a match.
 */
static int
my_probe(device_t dev)
{
	struct my_type *t;

	t = my_devs;
	while (t->my_name != NULL) {
		if ((pci_get_vendor(dev) == t->my_vid) &&
		    (pci_get_device(dev) == t->my_did)) {
			device_set_desc(dev, t->my_name);
			my_info_tmp = t;
			return (BUS_PROBE_DEFAULT);
		}
		t++;
	}
	return (ENXIO);
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia setup and
 * ethernet/BPF attach.
 */
static int
my_attach(device_t dev)
{
	int             i;
	u_char          eaddr[ETHER_ADDR_LEN];
	u_int32_t       iobase;
	struct my_softc *sc;
	struct ifnet   *ifp;
	int             media = IFM_ETHER | IFM_100_TX | IFM_FDX;
	unsigned int    round;
	caddr_t         roundptr;
	struct my_type *p;
	u_int16_t       phy_vid, phy_did, phy_sts = 0;
	int             rid, error = 0;

	sc = device_get_softc(dev);
	sc->my_dev = dev;
	mtx_init(&sc->my_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);
	callout_init_mtx(&sc->my_autoneg_timer, &sc->my_mtx, 0);
	callout_init_mtx(&sc->my_watchdog, &sc->my_mtx, 0);

	/*
	 * Map control/status registers.
	 */
	pci_enable_busmaster(dev);

	if (my_info_tmp->my_did == MTD800ID) {
		iobase = pci_read_config(dev, MY_PCI_LOIO, 4);
		if (iobase & 0x300)
			MY_USEIOSPACE = 0;
	}

	rid = MY_RID;
	sc->my_res = bus_alloc_resource_any(dev, MY_RES, &rid, RF_ACTIVE);

	if (sc->my_res == NULL) {
		device_printf(dev, "couldn't map ports/memory\n");
		error = ENXIO;
		goto destroy_mutex;
	}
	sc->my_btag = rman_get_bustag(sc->my_res);
	sc->my_bhandle = rman_get_bushandle(sc->my_res);

	rid = 0;
	sc->my_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
					    RF_SHAREABLE | RF_ACTIVE);

	if (sc->my_irq == NULL) {
		device_printf(dev, "couldn't map interrupt\n");
		error = ENXIO;
		goto release_io;
	}

	sc->my_info = my_info_tmp;

	/* Reset the adapter. */
	MY_LOCK(sc);
	my_reset(sc);
	MY_UNLOCK(sc);

	/*
	 * Get station address
	 */
	for (i = 0; i < ETHER_ADDR_LEN; ++i)
		eaddr[i] = CSR_READ_1(sc, MY_PAR0 + i);

	sc->my_ldata_ptr = malloc(sizeof(struct my_list_data) + 8,
				  M_DEVBUF, M_NOWAIT);
	if (sc->my_ldata_ptr == NULL) {
		device_printf(dev, "no memory for list buffers!\n");
		error = ENXIO;
		goto release_irq;
	}
	sc->my_ldata = (struct my_list_data *) sc->my_ldata_ptr;
	round = (uintptr_t)sc->my_ldata_ptr & 0xF;
	roundptr = sc->my_ldata_ptr;
	for (i = 0; i < 8; i++) {
		if (round % 8) {
			round++;
			roundptr++;
		} else
			break;
	}
	sc->my_ldata = (struct my_list_data *) roundptr;
	bzero(sc->my_ldata, sizeof(struct my_list_data));

	ifp = sc->my_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "can not if_alloc()\n");
		error = ENOSPC;
		goto free_ldata;
	}
	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = my_ioctl;
	ifp->if_start = my_start;
	ifp->if_init = my_init;
	ifp->if_baudrate = 10000000;
	IFQ_SET_MAXLEN(&ifp->if_snd, ifqmaxlen);
	ifp->if_snd.ifq_drv_maxlen = ifqmaxlen;
	IFQ_SET_READY(&ifp->if_snd);

	if (sc->my_info->my_did == MTD803ID)
		sc->my_pinfo = my_phys;
	else {
		if (bootverbose)
			device_printf(dev, "probing for a PHY\n");
		MY_LOCK(sc);
		for (i = MY_PHYADDR_MIN; i < MY_PHYADDR_MAX + 1; i++) {
			if (bootverbose)
				device_printf(dev, "checking address: %d\n", i);
			sc->my_phy_addr = i;
			phy_sts = my_phy_readreg(sc, PHY_BMSR);
			if ((phy_sts != 0) && (phy_sts != 0xffff))
				break;
			else
				phy_sts = 0;
		}
		if (phy_sts) {
			phy_vid = my_phy_readreg(sc, PHY_VENID);
			phy_did = my_phy_readreg(sc, PHY_DEVID);
			if (bootverbose) {
				device_printf(dev, "found PHY at address %d, ",
				    sc->my_phy_addr);
				printf("vendor id: %x device id: %x\n",
				    phy_vid, phy_did);
			}
			p = my_phys;
			while (p->my_vid) {
				if (phy_vid == p->my_vid) {
					sc->my_pinfo = p;
					break;
				}
				p++;
			}
			if (sc->my_pinfo == NULL)
				sc->my_pinfo = &my_phys[PHY_UNKNOWN];
			if (bootverbose)
				device_printf(dev, "PHY type: %s\n",
				       sc->my_pinfo->my_name);
		} else {
			MY_UNLOCK(sc);
			device_printf(dev, "MII without any phy!\n");
			error = ENXIO;
			goto free_if;
		}
		MY_UNLOCK(sc);
	}

	/* Do ifmedia setup. */
	ifmedia_init(&sc->ifmedia, 0, my_ifmedia_upd, my_ifmedia_sts);
	MY_LOCK(sc);
	my_getmode_mii(sc);
	my_autoneg_mii(sc, MY_FLAG_FORCEDELAY, 1);
	media = sc->ifmedia.ifm_media;
	my_stop(sc);
	MY_UNLOCK(sc);
	ifmedia_set(&sc->ifmedia, media);

	ether_ifattach(ifp, eaddr);

	error = bus_setup_intr(dev, sc->my_irq, INTR_TYPE_NET | INTR_MPSAFE,
			       NULL, my_intr, sc, &sc->my_intrhand);

	if (error) {
		device_printf(dev, "couldn't set up irq\n");
		goto detach_if;
	}
	 
	return (0);

detach_if:
	ether_ifdetach(ifp);
free_if:
	if_free(ifp);
free_ldata:
	free(sc->my_ldata_ptr, M_DEVBUF);
release_irq:
	bus_release_resource(dev, SYS_RES_IRQ, 0, sc->my_irq);
release_io:
	bus_release_resource(dev, MY_RES, MY_RID, sc->my_res);
destroy_mutex:
	mtx_destroy(&sc->my_mtx);
	return (error);
}

static int
my_detach(device_t dev)
{
	struct my_softc *sc;
	struct ifnet   *ifp;

	sc = device_get_softc(dev);
	ifp = sc->my_ifp;
	ether_ifdetach(ifp);
	MY_LOCK(sc);
	my_stop(sc);
	MY_UNLOCK(sc);
	bus_teardown_intr(dev, sc->my_irq, sc->my_intrhand);
	callout_drain(&sc->my_watchdog);
	callout_drain(&sc->my_autoneg_timer);

	if_free(ifp);
	free(sc->my_ldata_ptr, M_DEVBUF);

	bus_release_resource(dev, SYS_RES_IRQ, 0, sc->my_irq);
	bus_release_resource(dev, MY_RES, MY_RID, sc->my_res);
	mtx_destroy(&sc->my_mtx);
	return (0);
}


/*
 * Initialize the transmit descriptors.
 */
static int
my_list_tx_init(struct my_softc * sc)
{
	struct my_chain_data *cd;
	struct my_list_data *ld;
	int             i;

	MY_LOCK_ASSERT(sc);
	cd = &sc->my_cdata;
	ld = sc->my_ldata;
	for (i = 0; i < MY_TX_LIST_CNT; i++) {
		cd->my_tx_chain[i].my_ptr = &ld->my_tx_list[i];
		if (i == (MY_TX_LIST_CNT - 1))
			cd->my_tx_chain[i].my_nextdesc = &cd->my_tx_chain[0];
		else
			cd->my_tx_chain[i].my_nextdesc =
			    &cd->my_tx_chain[i + 1];
	}
	cd->my_tx_free = &cd->my_tx_chain[0];
	cd->my_tx_tail = cd->my_tx_head = NULL;
	return (0);
}

/*
 * Initialize the RX descriptors and allocate mbufs for them. Note that we
 * arrange the descriptors in a closed ring, so that the last descriptor
 * points back to the first.
 */
static int
my_list_rx_init(struct my_softc * sc)
{
	struct my_chain_data *cd;
	struct my_list_data *ld;
	int             i;

	MY_LOCK_ASSERT(sc);
	cd = &sc->my_cdata;
	ld = sc->my_ldata;
	for (i = 0; i < MY_RX_LIST_CNT; i++) {
		cd->my_rx_chain[i].my_ptr =
		    (struct my_desc *) & ld->my_rx_list[i];
		if (my_newbuf(sc, &cd->my_rx_chain[i]) == ENOBUFS) {
			MY_UNLOCK(sc);
			return (ENOBUFS);
		}
		if (i == (MY_RX_LIST_CNT - 1)) {
			cd->my_rx_chain[i].my_nextdesc = &cd->my_rx_chain[0];
			ld->my_rx_list[i].my_next = vtophys(&ld->my_rx_list[0]);
		} else {
			cd->my_rx_chain[i].my_nextdesc =
			    &cd->my_rx_chain[i + 1];
			ld->my_rx_list[i].my_next =
			    vtophys(&ld->my_rx_list[i + 1]);
		}
	}
	cd->my_rx_head = &cd->my_rx_chain[0];
	return (0);
}

/*
 * Initialize an RX descriptor and attach an MBUF cluster.
 */
static int
my_newbuf(struct my_softc * sc, struct my_chain_onefrag * c)
{
	struct mbuf    *m_new = NULL;

	MY_LOCK_ASSERT(sc);
	MGETHDR(m_new, M_NOWAIT, MT_DATA);
	if (m_new == NULL) {
		device_printf(sc->my_dev,
		    "no memory for rx list -- packet dropped!\n");
		return (ENOBUFS);
	}
	if (!(MCLGET(m_new, M_NOWAIT))) {
		device_printf(sc->my_dev,
		    "no memory for rx list -- packet dropped!\n");
		m_freem(m_new);
		return (ENOBUFS);
	}
	c->my_mbuf = m_new;
	c->my_ptr->my_data = vtophys(mtod(m_new, caddr_t));
	c->my_ptr->my_ctl = (MCLBYTES - 1) << MY_RBSShift;
	c->my_ptr->my_status = MY_OWNByNIC;
	return (0);
}

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to the higher
 * level protocols.
 */
static void
my_rxeof(struct my_softc * sc)
{
	struct ether_header *eh;
	struct mbuf    *m;
	struct ifnet   *ifp;
	struct my_chain_onefrag *cur_rx;
	int             total_len = 0;
	u_int32_t       rxstat;

	MY_LOCK_ASSERT(sc);
	ifp = sc->my_ifp;
	while (!((rxstat = sc->my_cdata.my_rx_head->my_ptr->my_status)
	    & MY_OWNByNIC)) {
		cur_rx = sc->my_cdata.my_rx_head;
		sc->my_cdata.my_rx_head = cur_rx->my_nextdesc;

		if (rxstat & MY_ES) {	/* error summary: give up this rx pkt */
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			cur_rx->my_ptr->my_status = MY_OWNByNIC;
			continue;
		}
		/* No errors; receive the packet. */
		total_len = (rxstat & MY_FLNGMASK) >> MY_FLNGShift;
		total_len -= ETHER_CRC_LEN;

		if (total_len < MINCLSIZE) {
			m = m_devget(mtod(cur_rx->my_mbuf, char *),
			    total_len, 0, ifp, NULL);
			cur_rx->my_ptr->my_status = MY_OWNByNIC;
			if (m == NULL) {
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
				continue;
			}
		} else {
			m = cur_rx->my_mbuf;
			/*
			 * Try to conjure up a new mbuf cluster. If that
			 * fails, it means we have an out of memory condition
			 * and should leave the buffer in place and continue.
			 * This will result in a lost packet, but there's
			 * little else we can do in this situation.
			 */
			if (my_newbuf(sc, cur_rx) == ENOBUFS) {
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
				cur_rx->my_ptr->my_status = MY_OWNByNIC;
				continue;
			}
			m->m_pkthdr.rcvif = ifp;
			m->m_pkthdr.len = m->m_len = total_len;
		}
		if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
		eh = mtod(m, struct ether_header *);
#if NBPFILTER > 0
		/*
		 * Handle BPF listeners. Let the BPF user see the packet, but
		 * don't pass it up to the ether_input() layer unless it's a
		 * broadcast packet, multicast packet, matches our ethernet
		 * address or the interface is in promiscuous mode.
		 */
		if (bpf_peers_present(ifp->if_bpf)) {
			bpf_mtap(ifp->if_bpf, m);
			if (ifp->if_flags & IFF_PROMISC &&
			    (bcmp(eh->ether_dhost, IF_LLADDR(sc->my_ifp),
				ETHER_ADDR_LEN) &&
			     (eh->ether_dhost[0] & 1) == 0)) {
				m_freem(m);
				continue;
			}
		}
#endif
		MY_UNLOCK(sc);
		(*ifp->if_input)(ifp, m);
		MY_LOCK(sc);
	}
	return;
}


/*
 * A frame was downloaded to the chip. It's safe for us to clean up the list
 * buffers.
 */
static void
my_txeof(struct my_softc * sc)
{
	struct my_chain *cur_tx;
	struct ifnet   *ifp;

	MY_LOCK_ASSERT(sc);
	ifp = sc->my_ifp;
	/* Clear the timeout timer. */
	sc->my_timer = 0;
	if (sc->my_cdata.my_tx_head == NULL) {
		return;
	}
	/*
	 * Go through our tx list and free mbufs for those frames that have
	 * been transmitted.
	 */
	while (sc->my_cdata.my_tx_head->my_mbuf != NULL) {
		u_int32_t       txstat;

		cur_tx = sc->my_cdata.my_tx_head;
		txstat = MY_TXSTATUS(cur_tx);
		if ((txstat & MY_OWNByNIC) || txstat == MY_UNSENT)
			break;
		if (!(CSR_READ_4(sc, MY_TCRRCR) & MY_Enhanced)) {
			if (txstat & MY_TXERR) {
				if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
				if (txstat & MY_EC) /* excessive collision */
					if_inc_counter(ifp, IFCOUNTER_COLLISIONS, 1);
				if (txstat & MY_LC)	/* late collision */
					if_inc_counter(ifp, IFCOUNTER_COLLISIONS, 1);
			}
			if_inc_counter(ifp, IFCOUNTER_COLLISIONS,
			    (txstat & MY_NCRMASK) >> MY_NCRShift);
		}
		if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
		m_freem(cur_tx->my_mbuf);
		cur_tx->my_mbuf = NULL;
		if (sc->my_cdata.my_tx_head == sc->my_cdata.my_tx_tail) {
			sc->my_cdata.my_tx_head = NULL;
			sc->my_cdata.my_tx_tail = NULL;
			break;
		}
		sc->my_cdata.my_tx_head = cur_tx->my_nextdesc;
	}
	if (CSR_READ_4(sc, MY_TCRRCR) & MY_Enhanced) {
		if_inc_counter(ifp, IFCOUNTER_COLLISIONS, (CSR_READ_4(sc, MY_TSR) & MY_NCRMask));
	}
	return;
}

/*
 * TX 'end of channel' interrupt handler.
 */
static void
my_txeoc(struct my_softc * sc)
{
	struct ifnet   *ifp;

	MY_LOCK_ASSERT(sc);
	ifp = sc->my_ifp;
	sc->my_timer = 0;
	if (sc->my_cdata.my_tx_head == NULL) {
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		sc->my_cdata.my_tx_tail = NULL;
		if (sc->my_want_auto)
			my_autoneg_mii(sc, MY_FLAG_SCHEDDELAY, 1);
	} else {
		if (MY_TXOWN(sc->my_cdata.my_tx_head) == MY_UNSENT) {
			MY_TXOWN(sc->my_cdata.my_tx_head) = MY_OWNByNIC;
			sc->my_timer = 5;
			CSR_WRITE_4(sc, MY_TXPDR, 0xFFFFFFFF);
		}
	}
	return;
}

static void
my_intr(void *arg)
{
	struct my_softc *sc;
	struct ifnet   *ifp;
	u_int32_t       status;

	sc = arg;
	MY_LOCK(sc);
	ifp = sc->my_ifp;
	if (!(ifp->if_flags & IFF_UP)) {
		MY_UNLOCK(sc);
		return;
	}
	/* Disable interrupts. */
	CSR_WRITE_4(sc, MY_IMR, 0x00000000);

	for (;;) {
		status = CSR_READ_4(sc, MY_ISR);
		status &= MY_INTRS;
		if (status)
			CSR_WRITE_4(sc, MY_ISR, status);
		else
			break;

		if (status & MY_RI)	/* receive interrupt */
			my_rxeof(sc);

		if ((status & MY_RBU) || (status & MY_RxErr)) {
			/* rx buffer unavailable or rx error */
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
#ifdef foo
			my_stop(sc);
			my_reset(sc);
			my_init_locked(sc);
#endif
		}
		if (status & MY_TI)	/* tx interrupt */
			my_txeof(sc);
		if (status & MY_ETI)	/* tx early interrupt */
			my_txeof(sc);
		if (status & MY_TBU)	/* tx buffer unavailable */
			my_txeoc(sc);

#if 0				/* 90/1/18 delete */
		if (status & MY_FBE) {
			my_reset(sc);
			my_init_locked(sc);
		}
#endif

	}

	/* Re-enable interrupts. */
	CSR_WRITE_4(sc, MY_IMR, MY_INTRS);
	if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
		my_start_locked(ifp);
	MY_UNLOCK(sc);
	return;
}

/*
 * Encapsulate an mbuf chain in a descriptor by coupling the mbuf data
 * pointers to the fragment pointers.
 */
static int
my_encap(struct my_softc * sc, struct my_chain * c, struct mbuf * m_head)
{
	struct my_desc *f = NULL;
	int             total_len;
	struct mbuf    *m, *m_new = NULL;

	MY_LOCK_ASSERT(sc);
	/* calculate the total tx pkt length */
	total_len = 0;
	for (m = m_head; m != NULL; m = m->m_next)
		total_len += m->m_len;
	/*
	 * Start packing the mbufs in this chain into the fragment pointers.
	 * Stop when we run out of fragments or hit the end of the mbuf
	 * chain.
	 */
	m = m_head;
	MGETHDR(m_new, M_NOWAIT, MT_DATA);
	if (m_new == NULL) {
		device_printf(sc->my_dev, "no memory for tx list");
		return (1);
	}
	if (m_head->m_pkthdr.len > MHLEN) {
		if (!(MCLGET(m_new, M_NOWAIT))) {
			m_freem(m_new);
			device_printf(sc->my_dev, "no memory for tx list");
			return (1);
		}
	}
	m_copydata(m_head, 0, m_head->m_pkthdr.len, mtod(m_new, caddr_t));
	m_new->m_pkthdr.len = m_new->m_len = m_head->m_pkthdr.len;
	m_freem(m_head);
	m_head = m_new;
	f = &c->my_ptr->my_frag[0];
	f->my_status = 0;
	f->my_data = vtophys(mtod(m_new, caddr_t));
	total_len = m_new->m_len;
	f->my_ctl = MY_TXFD | MY_TXLD | MY_CRCEnable | MY_PADEnable;
	f->my_ctl |= total_len << MY_PKTShift;	/* pkt size */
	f->my_ctl |= total_len;	/* buffer size */
	/* 89/12/29 add, for mtd891 *//* [ 89? ] */
	if (sc->my_info->my_did == MTD891ID)
		f->my_ctl |= MY_ETIControl | MY_RetryTxLC;
	c->my_mbuf = m_head;
	c->my_lastdesc = 0;
	MY_TXNEXT(c) = vtophys(&c->my_nextdesc->my_ptr->my_frag[0]);
	return (0);
}

/*
 * Main transmit routine. To avoid having to do mbuf copies, we put pointers
 * to the mbuf data regions directly in the transmit lists. We also save a
 * copy of the pointers since the transmit list fragment pointers are
 * physical addresses.
 */
static void
my_start(struct ifnet * ifp)
{
	struct my_softc *sc;

	sc = ifp->if_softc;
	MY_LOCK(sc);
	my_start_locked(ifp);
	MY_UNLOCK(sc);
}

static void
my_start_locked(struct ifnet * ifp)
{
	struct my_softc *sc;
	struct mbuf    *m_head = NULL;
	struct my_chain *cur_tx = NULL, *start_tx;

	sc = ifp->if_softc;
	MY_LOCK_ASSERT(sc);
	if (sc->my_autoneg) {
		sc->my_tx_pend = 1;
		return;
	}
	/*
	 * Check for an available queue slot. If there are none, punt.
	 */
	if (sc->my_cdata.my_tx_free->my_mbuf != NULL) {
		ifp->if_drv_flags |= IFF_DRV_OACTIVE;
		return;
	}
	start_tx = sc->my_cdata.my_tx_free;
	while (sc->my_cdata.my_tx_free->my_mbuf == NULL) {
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;

		/* Pick a descriptor off the free list. */
		cur_tx = sc->my_cdata.my_tx_free;
		sc->my_cdata.my_tx_free = cur_tx->my_nextdesc;

		/* Pack the data into the descriptor. */
		my_encap(sc, cur_tx, m_head);

		if (cur_tx != start_tx)
			MY_TXOWN(cur_tx) = MY_OWNByNIC;
#if NBPFILTER > 0
		/*
		 * If there's a BPF listener, bounce a copy of this frame to
		 * him.
		 */
		BPF_MTAP(ifp, cur_tx->my_mbuf);
#endif
	}
	/*
	 * If there are no packets queued, bail.
	 */
	if (cur_tx == NULL) {
		return;
	}
	/*
	 * Place the request for the upload interrupt in the last descriptor
	 * in the chain. This way, if we're chaining several packets at once,
	 * we'll only get an interrupt once for the whole chain rather than
	 * once for each packet.
	 */
	MY_TXCTL(cur_tx) |= MY_TXIC;
	cur_tx->my_ptr->my_frag[0].my_ctl |= MY_TXIC;
	sc->my_cdata.my_tx_tail = cur_tx;
	if (sc->my_cdata.my_tx_head == NULL)
		sc->my_cdata.my_tx_head = start_tx;
	MY_TXOWN(start_tx) = MY_OWNByNIC;
	CSR_WRITE_4(sc, MY_TXPDR, 0xFFFFFFFF);	/* tx polling demand */

	/*
	 * Set a timeout in case the chip goes out to lunch.
	 */
	sc->my_timer = 5;
	return;
}

static void
my_init(void *xsc)
{
	struct my_softc *sc = xsc;

	MY_LOCK(sc);
	my_init_locked(sc);
	MY_UNLOCK(sc);
}

static void
my_init_locked(struct my_softc *sc)
{
	struct ifnet   *ifp = sc->my_ifp;
	u_int16_t       phy_bmcr = 0;

	MY_LOCK_ASSERT(sc);
	if (sc->my_autoneg) {
		return;
	}
	if (sc->my_pinfo != NULL)
		phy_bmcr = my_phy_readreg(sc, PHY_BMCR);
	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */
	my_stop(sc);
	my_reset(sc);

	/*
	 * Set cache alignment and burst length.
	 */
#if 0				/* 89/9/1 modify,  */
	CSR_WRITE_4(sc, MY_BCR, MY_RPBLE512);
	CSR_WRITE_4(sc, MY_TCRRCR, MY_TFTSF);
#endif
	CSR_WRITE_4(sc, MY_BCR, MY_PBL8);
	CSR_WRITE_4(sc, MY_TCRRCR, MY_TFTSF | MY_RBLEN | MY_RPBLE512);
	/*
	 * 89/12/29 add, for mtd891,
	 */
	if (sc->my_info->my_did == MTD891ID) {
		MY_SETBIT(sc, MY_BCR, MY_PROG);
		MY_SETBIT(sc, MY_TCRRCR, MY_Enhanced);
	}
	my_setcfg(sc, phy_bmcr);
	/* Init circular RX list. */
	if (my_list_rx_init(sc) == ENOBUFS) {
		device_printf(sc->my_dev, "init failed: no memory for rx buffers\n");
		my_stop(sc);
		return;
	}
	/* Init TX descriptors. */
	my_list_tx_init(sc);

	/* If we want promiscuous mode, set the allframes bit. */
	if (ifp->if_flags & IFF_PROMISC)
		MY_SETBIT(sc, MY_TCRRCR, MY_PROM);
	else
		MY_CLRBIT(sc, MY_TCRRCR, MY_PROM);

	/*
	 * Set capture broadcast bit to capture broadcast frames.
	 */
	if (ifp->if_flags & IFF_BROADCAST)
		MY_SETBIT(sc, MY_TCRRCR, MY_AB);
	else
		MY_CLRBIT(sc, MY_TCRRCR, MY_AB);

	/*
	 * Program the multicast filter, if necessary.
	 */
	my_setmulti(sc);

	/*
	 * Load the address of the RX list.
	 */
	MY_CLRBIT(sc, MY_TCRRCR, MY_RE);
	CSR_WRITE_4(sc, MY_RXLBA, vtophys(&sc->my_ldata->my_rx_list[0]));

	/*
	 * Enable interrupts.
	 */
	CSR_WRITE_4(sc, MY_IMR, MY_INTRS);
	CSR_WRITE_4(sc, MY_ISR, 0xFFFFFFFF);

	/* Enable receiver and transmitter. */
	MY_SETBIT(sc, MY_TCRRCR, MY_RE);
	MY_CLRBIT(sc, MY_TCRRCR, MY_TE);
	CSR_WRITE_4(sc, MY_TXLBA, vtophys(&sc->my_ldata->my_tx_list[0]));
	MY_SETBIT(sc, MY_TCRRCR, MY_TE);

	/* Restore state of BMCR */
	if (sc->my_pinfo != NULL)
		my_phy_writereg(sc, PHY_BMCR, phy_bmcr);
	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	callout_reset(&sc->my_watchdog, hz, my_watchdog, sc);
	return;
}

/*
 * Set media options.
 */

static int
my_ifmedia_upd(struct ifnet * ifp)
{
	struct my_softc *sc;
	struct ifmedia *ifm;

	sc = ifp->if_softc;
	MY_LOCK(sc);
	ifm = &sc->ifmedia;
	if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER) {
		MY_UNLOCK(sc);
		return (EINVAL);
	}
	if (IFM_SUBTYPE(ifm->ifm_media) == IFM_AUTO)
		my_autoneg_mii(sc, MY_FLAG_SCHEDDELAY, 1);
	else
		my_setmode_mii(sc, ifm->ifm_media);
	MY_UNLOCK(sc);
	return (0);
}

/*
 * Report current media status.
 */

static void
my_ifmedia_sts(struct ifnet * ifp, struct ifmediareq * ifmr)
{
	struct my_softc *sc;
	u_int16_t advert = 0, ability = 0;

	sc = ifp->if_softc;
	MY_LOCK(sc);
	ifmr->ifm_active = IFM_ETHER;
	if (!(my_phy_readreg(sc, PHY_BMCR) & PHY_BMCR_AUTONEGENBL)) {
#if 0				/* this version did not support 1000M, */
		if (my_phy_readreg(sc, PHY_BMCR) & PHY_BMCR_1000)
			ifmr->ifm_active = IFM_ETHER | IFM_1000TX;
#endif
		if (my_phy_readreg(sc, PHY_BMCR) & PHY_BMCR_SPEEDSEL)
			ifmr->ifm_active = IFM_ETHER | IFM_100_TX;
		else
			ifmr->ifm_active = IFM_ETHER | IFM_10_T;
		if (my_phy_readreg(sc, PHY_BMCR) & PHY_BMCR_DUPLEX)
			ifmr->ifm_active |= IFM_FDX;
		else
			ifmr->ifm_active |= IFM_HDX;

		MY_UNLOCK(sc);
		return;
	}
	ability = my_phy_readreg(sc, PHY_LPAR);
	advert = my_phy_readreg(sc, PHY_ANAR);

#if 0				/* this version did not support 1000M, */
	if (sc->my_pinfo->my_vid = MarvellPHYID0) {
		ability2 = my_phy_readreg(sc, PHY_1000SR);
		if (ability2 & PHY_1000SR_1000BTXFULL) {
			advert = 0;
			ability = 0;
	  		ifmr->ifm_active = IFM_ETHER|IFM_1000_T|IFM_FDX;
	  	} else if (ability & PHY_1000SR_1000BTXHALF) {
			advert = 0;
			ability = 0;
			ifmr->ifm_active = IFM_ETHER|IFM_1000_T|IFM_HDX;
		}
	}
#endif
	if (advert & PHY_ANAR_100BT4 && ability & PHY_ANAR_100BT4)
		ifmr->ifm_active = IFM_ETHER | IFM_100_T4;
	else if (advert & PHY_ANAR_100BTXFULL && ability & PHY_ANAR_100BTXFULL)
		ifmr->ifm_active = IFM_ETHER | IFM_100_TX | IFM_FDX;
	else if (advert & PHY_ANAR_100BTXHALF && ability & PHY_ANAR_100BTXHALF)
		ifmr->ifm_active = IFM_ETHER | IFM_100_TX | IFM_HDX;
	else if (advert & PHY_ANAR_10BTFULL && ability & PHY_ANAR_10BTFULL)
		ifmr->ifm_active = IFM_ETHER | IFM_10_T | IFM_FDX;
	else if (advert & PHY_ANAR_10BTHALF && ability & PHY_ANAR_10BTHALF)
		ifmr->ifm_active = IFM_ETHER | IFM_10_T | IFM_HDX;
	MY_UNLOCK(sc);
	return;
}

static int
my_ioctl(struct ifnet * ifp, u_long command, caddr_t data)
{
	struct my_softc *sc = ifp->if_softc;
	struct ifreq   *ifr = (struct ifreq *) data;
	int             error;

	switch (command) {
	case SIOCSIFFLAGS:
		MY_LOCK(sc);
		if (ifp->if_flags & IFF_UP)
			my_init_locked(sc);
		else if (ifp->if_drv_flags & IFF_DRV_RUNNING)
			my_stop(sc);
		MY_UNLOCK(sc);
		error = 0;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		MY_LOCK(sc);
		my_setmulti(sc);
		MY_UNLOCK(sc);
		error = 0;
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->ifmedia, command);
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}
	return (error);
}

static void
my_watchdog(void *arg)
{
	struct my_softc *sc;
	struct ifnet *ifp;

	sc = arg;
	MY_LOCK_ASSERT(sc);
	callout_reset(&sc->my_watchdog, hz, my_watchdog, sc);
	if (sc->my_timer == 0 || --sc->my_timer > 0)
		return;

	ifp = sc->my_ifp;
	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
	if_printf(ifp, "watchdog timeout\n");
	if (!(my_phy_readreg(sc, PHY_BMSR) & PHY_BMSR_LINKSTAT))
		if_printf(ifp, "no carrier - transceiver cable problem?\n");
	my_stop(sc);
	my_reset(sc);
	my_init_locked(sc);
	if (!IFQ_DRV_IS_EMPTY(&ifp->if_snd))
		my_start_locked(ifp);
}


/*
 * Stop the adapter and free any mbufs allocated to the RX and TX lists.
 */
static void
my_stop(struct my_softc * sc)
{
	int    i;
	struct ifnet   *ifp;

	MY_LOCK_ASSERT(sc);
	ifp = sc->my_ifp;

	callout_stop(&sc->my_autoneg_timer);
	callout_stop(&sc->my_watchdog);

	MY_CLRBIT(sc, MY_TCRRCR, (MY_RE | MY_TE));
	CSR_WRITE_4(sc, MY_IMR, 0x00000000);
	CSR_WRITE_4(sc, MY_TXLBA, 0x00000000);
	CSR_WRITE_4(sc, MY_RXLBA, 0x00000000);

	/*
	 * Free data in the RX lists.
	 */
	for (i = 0; i < MY_RX_LIST_CNT; i++) {
		if (sc->my_cdata.my_rx_chain[i].my_mbuf != NULL) {
			m_freem(sc->my_cdata.my_rx_chain[i].my_mbuf);
			sc->my_cdata.my_rx_chain[i].my_mbuf = NULL;
		}
	}
	bzero((char *)&sc->my_ldata->my_rx_list,
	    sizeof(sc->my_ldata->my_rx_list));
	/*
	 * Free the TX list buffers.
	 */
	for (i = 0; i < MY_TX_LIST_CNT; i++) {
		if (sc->my_cdata.my_tx_chain[i].my_mbuf != NULL) {
			m_freem(sc->my_cdata.my_tx_chain[i].my_mbuf);
			sc->my_cdata.my_tx_chain[i].my_mbuf = NULL;
		}
	}
	bzero((char *)&sc->my_ldata->my_tx_list,
	    sizeof(sc->my_ldata->my_tx_list));
	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
	return;
}

/*
 * Stop all chip I/O so that the kernel's probe routines don't get confused
 * by errant DMAs when rebooting.
 */
static int
my_shutdown(device_t dev)
{
	struct my_softc *sc;

	sc = device_get_softc(dev);
	MY_LOCK(sc);
	my_stop(sc);
	MY_UNLOCK(sc);
	return 0;
}
