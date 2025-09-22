/*	$OpenBSD: elink3.c,v 1.101 2023/11/10 15:51:20 bluhm Exp $	*/
/*	$NetBSD: elink3.c,v 1.32 1997/05/14 00:22:00 thorpej Exp $	*/

/*
 * Copyright (c) 1996, 1997 Jonathan Stone <jonathan@NetBSD.org>
 * Copyright (c) 1994 Herb Peyerl <hpeyerl@beer.org>
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

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/syslog.h>
#include <sys/timeout.h>
#include <sys/device.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <machine/cpu.h>
#include <machine/bus.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/ic/elink3var.h>
#include <dev/ic/elink3reg.h>

/*
 * Structure to map media-present bits in boards to 
 * ifmedia codes and printable media names. Used for table-driven
 * ifmedia initialization.
 */
struct ep_media {
	int	epm_eeprom_data;	/* bitmask for eeprom config */
	int	epm_conn;		/* sc->ep_connectors code for medium */
	char   *epm_name;		/* name of medium */
	uint64_t	epm_ifmedia;		/* ifmedia word for medium */
	int	epm_ifdata;
};

/*
 * ep_media table for Vortex/Demon/Boomerang:
 * map from media-present bits in register RESET_OPTIONS+2 
 * to  ifmedia "media words" and printable names.
 *
 * XXX indexed directly by INTERNAL_CONFIG default_media field,
 * (i.e., EPMEDIA_ constants)  forcing order of entries. 
 *  Note that 3 is reserved.
 */
const struct ep_media ep_vortex_media[] = {
  { EP_PCI_UTP,        EPC_UTP, "utp",	    IFM_ETHER|IFM_10_T,
       EPMEDIA_10BASE_T },
  { EP_PCI_AUI,        EPC_AUI, "aui",	    IFM_ETHER|IFM_10_5,
       EPMEDIA_AUI },
  { 0,                 0,  	"reserved", IFM_NONE,  EPMEDIA_RESV1 },
  { EP_PCI_BNC,        EPC_BNC, "bnc",	    IFM_ETHER|IFM_10_2,
       EPMEDIA_10BASE_2 },
  { EP_PCI_100BASE_TX, EPC_100TX, "100-TX", IFM_ETHER|IFM_100_TX,
       EPMEDIA_100BASE_TX },
  { EP_PCI_100BASE_FX, EPC_100FX, "100-FX", IFM_ETHER|IFM_100_FX,
       EPMEDIA_100BASE_FX },
  { EP_PCI_100BASE_MII,EPC_MII,   "mii",    IFM_ETHER|IFM_100_TX,
       EPMEDIA_MII },
  { EP_PCI_100BASE_T4, EPC_100T4, "100-T4", IFM_ETHER|IFM_100_T4,
       EPMEDIA_100BASE_T4 }
};

/*
 * ep_media table for 3c509/3c509b/3c579/3c589:
 * map from media-present bits in register CNFG_CNTRL
 * (window 0, offset ?) to  ifmedia "media words" and printable names.
 */
struct ep_media ep_isa_media[] = {
  { EP_W0_CC_UTP,  EPC_UTP, "utp",   IFM_ETHER|IFM_10_T, EPMEDIA_10BASE_T },
  { EP_W0_CC_AUI,  EPC_AUI, "aui",   IFM_ETHER|IFM_10_5, EPMEDIA_AUI },
  { EP_W0_CC_BNC,  EPC_BNC, "bnc",   IFM_ETHER|IFM_10_2, EPMEDIA_10BASE_2 },
};

/* Map vortex reset_options bits to if_media codes. */
const uint64_t ep_default_to_media[] = {
	IFM_ETHER | IFM_10_T,
	IFM_ETHER | IFM_10_5,
	0, 			/* reserved by 3Com */
	IFM_ETHER | IFM_10_2,
	IFM_ETHER | IFM_100_TX,
	IFM_ETHER | IFM_100_FX,
	IFM_ETHER | IFM_100_TX,	/* XXX really MII: need to talk to PHY */
	IFM_ETHER | IFM_100_T4,
};

struct cfdriver ep_cd = {
	NULL, "ep", DV_IFNET
};

void ep_vortex_probemedia(struct ep_softc *sc);
void ep_isa_probemedia(struct ep_softc *sc);

void eptxstat(struct ep_softc *);
int epstatus(struct ep_softc *);
int epioctl(struct ifnet *, u_long, caddr_t);
void epstart(struct ifnet *);
void epwatchdog(struct ifnet *);
void epreset(struct ep_softc *);
void epread(struct ep_softc *);
struct mbuf *epget(struct ep_softc *, int);
void epmbuffill(void *);
void epmbufempty(struct ep_softc *);
void epsetfilter(struct ep_softc *);
void ep_roadrunner_mii_enable(struct ep_softc *);
int epsetmedia(struct ep_softc *, int);

/* ifmedia callbacks */
int ep_media_change(struct ifnet *);
void ep_media_status(struct ifnet *, struct ifmediareq *);

/* MII callbacks */
int ep_mii_readreg(struct device *, int, int);
void ep_mii_writereg(struct device *, int, int, int);
void ep_statchg(struct device *);

void    ep_mii_setbit(struct ep_softc *, u_int16_t);
void    ep_mii_clrbit(struct ep_softc *, u_int16_t);
u_int16_t ep_mii_readbit(struct ep_softc *, u_int16_t);
void    ep_mii_sync(struct ep_softc *);
void    ep_mii_sendbits(struct ep_softc *, u_int32_t, int);

int epbusyeeprom(struct ep_softc *);
u_int16_t ep_read_eeprom(struct ep_softc *, u_int16_t);

static inline void ep_reset_cmd(struct ep_softc *sc, u_int cmd,u_int arg);
static inline void ep_finish_reset(bus_space_tag_t, bus_space_handle_t);
static inline void ep_discard_rxtop(bus_space_tag_t, bus_space_handle_t);
static __inline int ep_w1_reg(struct ep_softc *, int);

/*
 * Issue a (reset) command, and be sure it has completed.
 * Used for global reset, TX_RESET, RX_RESET.
 */
static inline void
ep_reset_cmd(struct ep_softc *sc, u_int cmd, u_int arg)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	bus_space_write_2(iot, ioh, cmd, arg);
	ep_finish_reset(iot, ioh);
}

/*
 * Wait for any pending reset to complete.
 */
static inline void
ep_finish_reset(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	int i;

	for (i = 0; i < 10000; i++) {
		if ((bus_space_read_2(iot, ioh, EP_STATUS) &
		    S_COMMAND_IN_PROGRESS) == 0)
			break;
		DELAY(10);
	}
}

static inline void
ep_discard_rxtop(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	int i;

	bus_space_write_2(iot, ioh, EP_COMMAND, RX_DISCARD_TOP_PACK);

	/*
	 * Spin for about 1 msec, to avoid forcing a DELAY() between
	 * every received packet (adding latency and limiting pkt-recv rate).
	 * On PCI, at 4 30-nsec PCI bus cycles for a read, 8000 iterations
	 * is about right.
	 */
	for (i = 0; i < 8000; i++) {
		if ((bus_space_read_2(iot, ioh, EP_STATUS) &
		    S_COMMAND_IN_PROGRESS) == 0)
			return;
	}

	/* not fast enough, do DELAY()s */
	ep_finish_reset(iot, ioh);
}

/*
 * Some chips (i.e., 3c574 RoadRunner) have Window 1 registers offset.
 */
static __inline int
ep_w1_reg(struct ep_softc *sc, int reg)
{
	switch (sc->ep_chipset) {
	case EP_CHIPSET_ROADRUNNER:
		switch (reg) {
		case EP_W1_FREE_TX:
		case EP_W1_RUNNER_RDCTL:
		case EP_W1_RUNNER_WRCTL:
			return (reg);
		}
		return (reg + 0x10);
	}
	return (reg);
}

/*
 * Back-end attach and configure.
 */
void
epconfig(struct ep_softc *sc, u_short chipset, u_int8_t *enaddr)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_int16_t i;

	sc->ep_chipset = chipset;

	/*
	 * We could have been groveling around in other register
	 * windows in the front-end; make sure we're in window 0
	 * to read the EEPROM.
	 */
	GO_WINDOW(0);

	if (enaddr == NULL) {
		/*
		 * Read the station address from the eeprom.
		 */
		for (i = 0; i < 3; i++) {
			u_int16_t x = ep_read_eeprom(sc, i);

			sc->sc_arpcom.ac_enaddr[(i << 1)] = x >> 8;
			sc->sc_arpcom.ac_enaddr[(i << 1) + 1] = x;
		}
	} else {
		bcopy(enaddr, sc->sc_arpcom.ac_enaddr, ETHER_ADDR_LEN);
	}

	printf(" address %s", ether_sprintf(sc->sc_arpcom.ac_enaddr));
	if (sc->ep_flags & EP_FLAGS_MII)
		printf("\n");
	else
		printf(", ");

	/*
	 * Vortex-based (3c59x pci,eisa) cards allow FDDI-sized (4500) byte
	 * packets.  Commands only take an 11-bit parameter, and  11 bits
	 * isn't enough to hold a full-size packet length.
	 * Commands to these cards implicitly upshift a packet size
	 * or threshold by 2 bits. 
	 * To detect  cards with large-packet support, we probe by setting
	 * the transmit threshold register, then change windows and
	 * read back the threshold register directly, and see if the
	 * threshold value was shifted or not.
	 */
	bus_space_write_2(iot, ioh, EP_COMMAND,
			  SET_TX_AVAIL_THRESH | EP_LARGEWIN_PROBE ); 
	GO_WINDOW(5);
	i = bus_space_read_2(iot, ioh, EP_W5_TX_AVAIL_THRESH);
	GO_WINDOW(1);
	switch (i)  {
	case EP_LARGEWIN_PROBE:
	case (EP_LARGEWIN_PROBE & EP_LARGEWIN_MASK):
		sc->txashift = 0;
		break;

	case (EP_LARGEWIN_PROBE << 2):
		sc->txashift = 2;
		/* XXX does the 3c515 support Vortex-style RESET_OPTIONS? */
		break;

	default:
		printf("wrote %x to TX_AVAIL_THRESH, read back %x. "
		    "Interface disabled\n", EP_THRESH_DISABLE, (int) i);
		return;
	}

	timeout_set(&sc->sc_epmbuffill_tmo, epmbuffill, sc);

	/*
	 * Ensure Tx-available interrupts are enabled for 
	 * start the interface.
	 * XXX should be in epinit()?
	 */
	bus_space_write_2(iot, ioh, EP_COMMAND,
	    SET_TX_AVAIL_THRESH | (1600 >> sc->txashift));

	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);
	ifp->if_softc = sc;
	ifp->if_start = epstart;
	ifp->if_ioctl = epioctl;
	ifp->if_watchdog = epwatchdog;
	ifp->if_flags =
	    IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	/* 64 packets are around 100ms on 10Mbps */
	ifq_init_maxlen(&ifp->if_snd, 64);

	if_attach(ifp);
	ether_ifattach(ifp);

	/*
	 * Finish configuration: 
	 * determine chipset if the front-end couldn't do so,
	 * show board details, set media.
	 */

	GO_WINDOW(0);

	ifmedia_init(&sc->sc_mii.mii_media, 0, ep_media_change,
	    ep_media_status);
	sc->sc_mii.mii_ifp = ifp;
	sc->sc_mii.mii_readreg = ep_mii_readreg;
	sc->sc_mii.mii_writereg = ep_mii_writereg;
	sc->sc_mii.mii_statchg = ep_statchg;

	/*
	 * If we've got an indirect (ISA, PCMCIA?) board, the chipset
	 * is unknown.  If the board has large-packet support, it's a
	 * Vortex/Boomerang, otherwise it's a 3c509.
	 * XXX use eeprom capability word instead?
	 */
	if (sc->ep_chipset == EP_CHIPSET_UNKNOWN && sc->txashift)  {
		printf("warning: unknown chipset, possibly 3c515?\n");
#ifdef notyet
		sc->sc_chipset = EP_CHIPSET_VORTEX;
#endif	/* notyet */
	}

	/*
	 * Ascertain which media types are present and inform ifmedia.
	 */
	switch (sc->ep_chipset) {
	case EP_CHIPSET_ROADRUNNER:
		if (sc->ep_flags & EP_FLAGS_MII) {
			ep_roadrunner_mii_enable(sc);
			GO_WINDOW(0);
		}
		/* FALLTHROUGH */

	case EP_CHIPSET_BOOMERANG:
		/*
		 * If the device has MII, probe it.  We won't be using
		 * any `native' media in this case, only PHYs.  If
		 * we don't, just treat the Boomerang like the Vortex.
		 */
		if (sc->ep_flags & EP_FLAGS_MII) {
			mii_attach(&sc->sc_dev, &sc->sc_mii, 0xffffffff,
			    MII_PHY_ANY, MII_OFFSET_ANY, 0);
			if (LIST_FIRST(&sc->sc_mii.mii_phys) == NULL) {
				ifmedia_add(&sc->sc_mii.mii_media,
				    IFM_ETHER|IFM_NONE, 0, NULL);
				ifmedia_set(&sc->sc_mii.mii_media,
				    IFM_ETHER|IFM_NONE);
			} else {
				ifmedia_set(&sc->sc_mii.mii_media,
				    IFM_ETHER|IFM_AUTO);
			}
			break;
		}
		/* FALLTHROUGH */

	/* on a direct bus, the attach routine can tell, but check anyway. */
	case EP_CHIPSET_VORTEX:
	case EP_CHIPSET_BOOMERANG2:
		ep_vortex_probemedia(sc);
		break;

	/* on ISA we can't yet tell 3c509 from 3c515. Assume the former. */
	case EP_CHIPSET_3C509:
	default:
		ep_isa_probemedia(sc);
		break;
	}

	GO_WINDOW(1);		/* Window 1 is operating window */

	sc->tx_start_thresh = 20;	/* probably a good starting point. */

	ep_reset_cmd(sc, EP_COMMAND, RX_RESET);
	ep_reset_cmd(sc, EP_COMMAND, TX_RESET);
}

int
ep_detach(struct device *self)
{
	struct ep_softc *sc = (struct ep_softc *)self;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;

	if (sc->ep_flags & EP_FLAGS_MII)
		mii_detach(&sc->sc_mii, MII_PHY_ANY, MII_OFFSET_ANY);

	ifmedia_delete_instance(&sc->sc_mii.mii_media, IFM_INST_ANY);

	ether_ifdetach(ifp);
	if_detach(ifp);

	return (0);
}

/*
 * Find supported media on 3c509-generation hardware that doesn't have
 * a "reset_options" register in window 3.
 * Use the config_cntrl register  in window 0 instead.
 * Used on original, 10Mbit ISA (3c509), 3c509B, and pre-Demon EISA cards
 * that implement  CONFIG_CTRL.  We don't have a good way to set the
 * default active medium; punt to ifconfig instead.
 *
 * XXX what about 3c515, pcmcia 10/100?
 */
void
ep_isa_probemedia(struct ep_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	struct ifmedia *ifm = &sc->sc_mii.mii_media;
	int	conn, i;
	u_int16_t ep_w0_config, port;

	conn = 0;
	GO_WINDOW(0);
	ep_w0_config = bus_space_read_2(iot, ioh, EP_W0_CONFIG_CTRL);
	for (i = 0; i < nitems(ep_isa_media); i++) {
		struct ep_media * epm = ep_isa_media + i;

		if ((ep_w0_config & epm->epm_eeprom_data) != 0) {
			ifmedia_add(ifm, epm->epm_ifmedia, epm->epm_ifdata, 0);
			if (conn)
				printf("/");
			printf("%s", epm->epm_name);
			conn |= epm->epm_conn;
		}
	}
	sc->ep_connectors = conn;

	/* get default medium from EEPROM */
	if (epbusyeeprom(sc))
		return;		/* XXX why is eeprom busy? */
	bus_space_write_2(iot, ioh, EP_W0_EEPROM_COMMAND,
	    READ_EEPROM | EEPROM_ADDR_CFG);
	if (epbusyeeprom(sc))
		return;		/* XXX why is  eeprom busy? */
	port = bus_space_read_2(iot, ioh, EP_W0_EEPROM_DATA);
	port = port >> 14;

	printf(" (default %s)\n", ep_vortex_media[port].epm_name);

	/* tell ifconfig what currently-active media is. */
	ifmedia_set(ifm, ep_default_to_media[port]);

	/* XXX autoselect not yet implemented */
}


/*
 * Find media present on large-packet-capable elink3 devices.
 * Show onboard configuration of large-packet-capable elink3 devices
 * (Demon, Vortex, Boomerang), which do not implement CONFIG_CTRL in window 0.
 * Use media and card-version info in window 3 instead.
 *
 * XXX how much of this works with 3c515, pcmcia 10/100?
 */
void
ep_vortex_probemedia(struct ep_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	struct ifmedia *ifm = &sc->sc_mii.mii_media;
	u_int config1, conn;
	int reset_options;
	int default_media;	/* 3-bit encoding of default (EEPROM) media */
	int autoselect;		/* boolean: should default to autoselect */
	const char *medium_name;
	register int i;

	GO_WINDOW(3);
	config1 = (u_int)bus_space_read_2(iot, ioh, EP_W3_INTERNAL_CONFIG + 2);
	reset_options  = (int)bus_space_read_1(iot, ioh, EP_W3_RESET_OPTIONS);
	GO_WINDOW(0);

	default_media = (config1 & CONFIG_MEDIAMASK) >> CONFIG_MEDIAMASK_SHIFT;
        autoselect = (config1 & CONFIG_AUTOSELECT) >> CONFIG_AUTOSELECT_SHIFT;

	/* set available media options */
	conn = 0;
	for (i = 0; i < nitems(ep_vortex_media); i++) {
		const struct ep_media *epm = ep_vortex_media + i;

		if ((reset_options & epm->epm_eeprom_data) != 0) {
			if (conn)
				printf("/");
			printf("%s", epm->epm_name);
			conn |= epm->epm_conn;
			ifmedia_add(ifm, epm->epm_ifmedia, epm->epm_ifdata, 0);
		}
	}

	sc->ep_connectors = conn;

	/* Show  eeprom's idea of default media.  */
	medium_name = (default_media > nitems(ep_vortex_media) - 1)
		? "(unknown/impossible media)"
		: ep_vortex_media[default_media].epm_name;
	printf(" default %s%s",
	       medium_name, (autoselect) ? "/autoselect" : "");
/*	sc->sc_media = ep_vortex_media[default_media].epm_ifdata;*/

#ifdef notyet	
	/*
	 * Set default: either the active interface the card
	 * reads  from the EEPROM, or if autoselect is true,
	 * whatever we find is actually connected. 
	 *
	 * XXX autoselect not yet implemented.
	 */
#endif	/* notyet */

	/* tell ifconfig what currently-active media is. */
	ifmedia_set(ifm, ep_default_to_media[default_media]);
}

/*
 * Bring device up.
 *
 * The order in here seems important. Otherwise we may not receive
 * interrupts. ?!
 */
void
epinit(struct ep_softc *sc)
{
	register struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	int i;

	/* make sure any pending reset has completed before touching board */
	ep_finish_reset(iot, ioh);

	/* cancel any pending I/O */
	epstop(sc);

	if (sc->bustype != EP_BUS_PCI) {
		GO_WINDOW(0);
		bus_space_write_2(iot, ioh, EP_W0_CONFIG_CTRL, 0);
		bus_space_write_2(iot, ioh, EP_W0_CONFIG_CTRL, ENABLE_DRQ_IRQ);
	}

	if (sc->bustype == EP_BUS_PCMCIA) {
		bus_space_write_2(iot, ioh, EP_W0_RESOURCE_CFG, 0x3f00);
	}

	GO_WINDOW(2);
	for (i = 0; i < 6; i++)	/* Reload the ether_addr. */
		bus_space_write_1(iot, ioh, EP_W2_ADDR_0 + i,
		    sc->sc_arpcom.ac_enaddr[i]);

	if (sc->bustype == EP_BUS_PCI || sc->bustype == EP_BUS_EISA)
		/*
		 * Reset the station-address receive filter.
		 * A bug workaround for busmastering  (Vortex, Demon) cards.
		 */
		for (i = 0; i < 6; i++)
			bus_space_write_1(iot, ioh, EP_W2_RECVMASK_0 + i, 0);

	ep_reset_cmd(sc, EP_COMMAND, RX_RESET);
	ep_reset_cmd(sc, EP_COMMAND, TX_RESET);

	GO_WINDOW(1);		/* Window 1 is operating window */
	for (i = 0; i < 31; i++)
		bus_space_read_1(iot, ioh, ep_w1_reg(sc, EP_W1_TX_STATUS));

	/* Set threshold for Tx-space available interrupt. */
	bus_space_write_2(iot, ioh, EP_COMMAND,
	    SET_TX_AVAIL_THRESH | (1600 >> sc->txashift));

	if (sc->ep_chipset == EP_CHIPSET_ROADRUNNER) {
		/* Enable options in the PCMCIA LAN COR register, via
		 * RoadRunner Window 1.
		 *
		 * XXX MAGIC CONSTANTS!
		 */
		u_int16_t cor;

		bus_space_write_2(iot, ioh, EP_W1_RUNNER_RDCTL, (1 << 11));

		cor = bus_space_read_2(iot, ioh, 0) & ~0x30;
		bus_space_write_2(iot, ioh, 0, cor);

		bus_space_write_2(iot, ioh, EP_W1_RUNNER_WRCTL, 0);
		bus_space_write_2(iot, ioh, EP_W1_RUNNER_RDCTL, 0);

		if (sc->ep_flags & EP_FLAGS_MII) {
			ep_roadrunner_mii_enable(sc);
			GO_WINDOW(1);
		}
	}

	/* Enable interrupts. */
	bus_space_write_2(iot, ioh, EP_COMMAND, SET_RD_0_MASK |
	    S_CARD_FAILURE | S_RX_COMPLETE | S_TX_COMPLETE | S_TX_AVAIL);
	bus_space_write_2(iot, ioh, EP_COMMAND, SET_INTR_MASK |
	    S_CARD_FAILURE | S_RX_COMPLETE | S_TX_COMPLETE | S_TX_AVAIL);

	/*
	 * Attempt to get rid of any stray interrupts that occurred during
	 * configuration.  On the i386 this isn't possible because one may
	 * already be queued.  However, a single stray interrupt is
	 * unimportant.
	 */
	bus_space_write_2(iot, ioh, EP_COMMAND, ACK_INTR | 0xff);

	epsetfilter(sc);
	epsetmedia(sc, sc->sc_mii.mii_media.ifm_cur->ifm_data);

	bus_space_write_2(iot, ioh, EP_COMMAND, RX_ENABLE);
	bus_space_write_2(iot, ioh, EP_COMMAND, TX_ENABLE);

	epmbuffill(sc);

	/* Interface is now `running', with no output active. */
	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	/* Attempt to start output, if any. */
	epstart(ifp);
}

/*
 * Set multicast receive filter. 
 * elink3 hardware has no selective multicast filter in hardware.
 * Enable reception of all multicasts and filter in software.
 */
void
epsetfilter(struct ep_softc *sc)
{
	register struct ifnet *ifp = &sc->sc_arpcom.ac_if;

	GO_WINDOW(1);		/* Window 1 is operating window */
	bus_space_write_2(sc->sc_iot, sc->sc_ioh, EP_COMMAND, SET_RX_FILTER |
	    FIL_INDIVIDUAL | FIL_BRDCST |
	    ((ifp->if_flags & IFF_MULTICAST) ? FIL_MULTICAST : 0 ) |
	    ((ifp->if_flags & IFF_PROMISC) ? FIL_PROMISC : 0 ));
}


int
ep_media_change(struct ifnet *ifp)
{
	register struct ep_softc *sc = ifp->if_softc;

	return	epsetmedia(sc, sc->sc_mii.mii_media.ifm_cur->ifm_data);
}

/*
 * Reset and enable the MII on the RoadRunner.
 */
void
ep_roadrunner_mii_enable(struct ep_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	GO_WINDOW(3);
	bus_space_write_2(iot, ioh, EP_W3_RESET_OPTIONS,
	    EP_PCI_100BASE_MII|EP_RUNNER_ENABLE_MII);
	delay(1000);
	bus_space_write_2(iot, ioh, EP_W3_RESET_OPTIONS,
	    EP_PCI_100BASE_MII|EP_RUNNER_MII_RESET|EP_RUNNER_ENABLE_MII);
	ep_reset_cmd(sc, EP_COMMAND, TX_RESET);
	ep_reset_cmd(sc, EP_COMMAND, RX_RESET);
	delay(1000);
	bus_space_write_2(iot, ioh, EP_W3_RESET_OPTIONS,
	    EP_PCI_100BASE_MII|EP_RUNNER_ENABLE_MII);
}

/*
 * Set active media to a specific given EPMEDIA_<> value.
 * For vortex/demon/boomerang cards, update media field in w3_internal_config,
 *       and power on selected transceiver.
 * For 3c509-generation cards (3c509/3c579/3c589/3c509B),
 *	update media field in w0_address_config, and power on selected xcvr.
 */
int
epsetmedia(struct ep_softc *sc, int medium)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	int w4_media;
	int config0, config1;

	/*
	 * you can `ifconfig (link0|-link0) ep0' to get the following
	 * behaviour:
	 *	-link0	disable AUI/UTP. enable BNC.
	 *	link0	disable BNC. enable AUI.
	 *	link1	if the card has a UTP connector, and link0 is
	 *		set too, then you get the UTP port.
	 */

	/*
	 * First, change the media-control bits in EP_W4_MEDIA_TYPE.
	 */

	 /* Turn everything off.  First turn off linkbeat and UTP. */
	GO_WINDOW(4);
	w4_media = bus_space_read_2(iot, ioh, EP_W4_MEDIA_TYPE);
	w4_media =  w4_media & ~(ENABLE_UTP|SQE_ENABLE);
	bus_space_write_2(iot, ioh, EP_W4_MEDIA_TYPE, w4_media);

	/* Turn off coax */
	bus_space_write_2(iot, ioh, EP_COMMAND, STOP_TRANSCEIVER);
	delay(1000);

	/* If the device has MII, select it, and then tell the
	 * PHY which media to use.
	 */
	if (sc->ep_flags & EP_FLAGS_MII) {
		GO_WINDOW(3);

		if (sc->ep_chipset == EP_CHIPSET_ROADRUNNER) {
			int resopt;

			resopt = bus_space_read_2(iot, ioh,
			    EP_W3_RESET_OPTIONS);
			bus_space_write_2(iot, ioh, EP_W3_RESET_OPTIONS,
			    resopt | EP_RUNNER_ENABLE_MII);
		}

		config0 = (u_int)bus_space_read_2(iot, ioh,
		    EP_W3_INTERNAL_CONFIG);
		config1 = (u_int)bus_space_read_2(iot, ioh,
		    EP_W3_INTERNAL_CONFIG + 2);

		config1 = config1 & ~CONFIG_MEDIAMASK;
		config1 |= (EPMEDIA_MII << CONFIG_MEDIAMASK_SHIFT);

		bus_space_write_2(iot, ioh, EP_W3_INTERNAL_CONFIG, config0);
		bus_space_write_2(iot, ioh, EP_W3_INTERNAL_CONFIG + 2, config1);
		GO_WINDOW(1);	/* back to operating window */

		mii_mediachg(&sc->sc_mii);
		return (0);
	}

	/*
	 * Now turn on the selected media/transceiver.
	 */
	GO_WINDOW(4);
	switch (medium) {
	case EPMEDIA_10BASE_T:
		bus_space_write_2(iot, ioh, EP_W4_MEDIA_TYPE, (ENABLE_UTP |
		    (sc->bustype == EP_BUS_PCMCIA ? MEDIA_LED : 0)));
		break;

	case EPMEDIA_10BASE_2:
		bus_space_write_2(iot, ioh, EP_COMMAND, START_TRANSCEIVER);
		DELAY(1000);	/* 50ms not enough? */
		break;

	/* XXX following only for new-generation cards */
	case EPMEDIA_100BASE_TX:
	case EPMEDIA_100BASE_FX:
	case EPMEDIA_100BASE_T4:	/* XXX check documentation */
		bus_space_write_2(iot, ioh, EP_W4_MEDIA_TYPE,
		    w4_media | LINKBEAT_ENABLE);
		DELAY(1000);	/* not strictly necessary? */
		break;

	case EPMEDIA_AUI:
		bus_space_write_2(iot, ioh, EP_W4_MEDIA_TYPE,
		    w4_media | SQE_ENABLE);
		DELAY(1000);	/*  not strictly necessary? */
		break;
	case EPMEDIA_MII:
		break;
	default:
#if defined(EP_DEBUG)
		printf("%s unknown media 0x%x\n", sc->sc_dev.dv_xname, medium);
#endif
		break;
		
	}

	/*
	 * Tell the chip which PHY [sic] to use.
	 */
	switch (sc->ep_chipset) {
	case EP_CHIPSET_VORTEX:
	case EP_CHIPSET_BOOMERANG2:
		GO_WINDOW(3);
		config0 = (u_int)bus_space_read_2(iot, ioh,
		    EP_W3_INTERNAL_CONFIG);
		config1 = (u_int)bus_space_read_2(iot, ioh,
		    EP_W3_INTERNAL_CONFIG + 2);

#if defined(EP_DEBUG)
		printf("%s:  read 0x%x, 0x%x from EP_W3_CONFIG register\n",
		       sc->sc_dev.dv_xname, config0, config1);
#endif
		config1 = config1 & ~CONFIG_MEDIAMASK;
		config1 |= (medium << CONFIG_MEDIAMASK_SHIFT);
		
#if defined(EP_DEBUG)
		printf("epsetmedia: %s: medium 0x%x, 0x%x to EP_W3_CONFIG\n",
		    sc->sc_dev.dv_xname, medium, config1);
#endif
		bus_space_write_2(iot, ioh, EP_W3_INTERNAL_CONFIG, config0);
		bus_space_write_2(iot, ioh, EP_W3_INTERNAL_CONFIG + 2, config1);
		break;

	default:
		GO_WINDOW(0);
		config0 = bus_space_read_2(iot, ioh, EP_W0_ADDRESS_CFG);
		config0 &= 0x3fff;
		bus_space_write_2(iot, ioh, EP_W0_ADDRESS_CFG,
		    config0 | (medium << 14));
		DELAY(1000);
		break;
	}

	GO_WINDOW(1);		/* Window 1 is operating window */
	return (0);
}


/*
 * Get currently-selected media from card.
 * (if_media callback, may be called before interface is brought up).
 */
void
ep_media_status(struct ifnet *ifp, struct ifmediareq *req)
{
	register struct ep_softc *sc = ifp->if_softc;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_int config1;
	u_int ep_mediastatus;

	/*
	 * If we have MII, go ask the PHY what's going on.
	 */
	if (sc->ep_flags & EP_FLAGS_MII) {
		mii_pollstat(&sc->sc_mii);
		req->ifm_active = sc->sc_mii.mii_media_active;
		req->ifm_status = sc->sc_mii.mii_media_status;
		return;
	}

	/* XXX read from softc when we start autosensing media */
	req->ifm_active = sc->sc_mii.mii_media.ifm_cur->ifm_media;
	
	switch (sc->ep_chipset) {
	case EP_CHIPSET_VORTEX:
	case EP_CHIPSET_BOOMERANG:
		GO_WINDOW(3);
		delay(5000);

		config1 = bus_space_read_2(iot, ioh, EP_W3_INTERNAL_CONFIG + 2);
		GO_WINDOW(1);

		config1 = 
		    (config1 & CONFIG_MEDIAMASK) >> CONFIG_MEDIAMASK_SHIFT;
		req->ifm_active = ep_default_to_media[config1];

		/* XXX check full-duplex bits? */

		GO_WINDOW(4);
		req->ifm_status = IFM_AVALID;	/* XXX */
		ep_mediastatus = bus_space_read_2(iot, ioh, EP_W4_MEDIA_TYPE);
		if (ep_mediastatus & LINKBEAT_DETECT)
			req->ifm_status |= IFM_ACTIVE; 	/* XXX  automedia */

		break;

	case EP_CHIPSET_UNKNOWN:
	case EP_CHIPSET_3C509:
		req->ifm_status = 0;	/* XXX */
		break;

	default:
		printf("%s: media_status on unknown chipset 0x%x\n",
		       ifp->if_xname, sc->ep_chipset);
		break;
	}

	/* XXX look for softc heartbeat for other chips or media */

	GO_WINDOW(1);
	return;
}



/*
 * Start outputting on the interface.
 * Always called as splnet().
 */
void
epstart(struct ifnet *ifp)
{
	register struct ep_softc *sc = ifp->if_softc;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	struct mbuf *m, *m0;
	caddr_t data;
	int sh, len, pad, txreg;

	/* Don't transmit if interface is busy or not running */
	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

startagain:
	/* Sneak a peek at the next packet */
	m0 = ifq_deq_begin(&ifp->if_snd);
	if (m0 == NULL)
		return;

	/* We need to use m->m_pkthdr.len, so require the header */
	if ((m0->m_flags & M_PKTHDR) == 0)
		panic("epstart: no header mbuf");
	len = m0->m_pkthdr.len;

	pad = (4 - len) & 3;

	/*
	 * The 3c509 automatically pads short packets to minimum ethernet
	 * length, but we drop packets that are too large. Perhaps we should
	 * truncate them instead?
	 */
	if (len + pad > ETHER_MAX_LEN) {
		/* packet is obviously too large: toss it */
		++ifp->if_oerrors;
		ifq_deq_commit(&ifp->if_snd, m0);
		m_freem(m0);
		goto readcheck;
	}

	if (bus_space_read_2(iot, ioh, ep_w1_reg(sc, EP_W1_FREE_TX)) <
	    len + pad + 4) {
		bus_space_write_2(iot, ioh, EP_COMMAND,
		    SET_TX_AVAIL_THRESH | ((len + pad + 4) >> sc->txashift));
		/* not enough room in FIFO */
		ifq_deq_rollback(&ifp->if_snd, m0);
		ifq_set_oactive(&ifp->if_snd);
		return;
	} else {
		bus_space_write_2(iot, ioh, EP_COMMAND,
		    SET_TX_AVAIL_THRESH | EP_THRESH_DISABLE);
	}

	ifq_deq_commit(&ifp->if_snd, m0);
	if (m0 == NULL)
		return;

	bus_space_write_2(iot, ioh, EP_COMMAND, SET_TX_START_THRESH |
	    ((len / 4 + sc->tx_start_thresh) /*>> sc->txashift*/));

#if NBPFILTER > 0
	if (ifp->if_bpf)
		bpf_mtap(ifp->if_bpf, m0, BPF_DIRECTION_OUT);
#endif

	/*
	 * Do the output at splhigh() so that an interrupt from another device
	 * won't cause a FIFO underrun.
	 */
	sh = splhigh();

	txreg = ep_w1_reg(sc, EP_W1_TX_PIO_WR_1);

	bus_space_write_2(iot, ioh, txreg, len);
	bus_space_write_2(iot, ioh, txreg, 0xffff); /* Second is meaningless */
	if (EP_IS_BUS_32(sc->bustype)) {
		for (m = m0; m; ) {
			data = mtod(m, u_int8_t *);
			if (m->m_len > 3 && ALIGNED_POINTER(data, uint32_t)) {
				bus_space_write_raw_multi_4(iot, ioh, txreg,
				    data, m->m_len & ~3);
				if (m->m_len & 3)
					bus_space_write_multi_1(iot, ioh, txreg,
					    data + (m->m_len & ~3),
					    m->m_len & 3);
			} else
				bus_space_write_multi_1(iot, ioh, txreg,
				    data, m->m_len);
			m0 = m_free(m);
			m = m0;
		}
	} else {
		for (m = m0; m; ) {
			data = mtod(m, u_int8_t *);
			if (m->m_len > 1 && ALIGNED_POINTER(data, uint16_t)) {
				bus_space_write_raw_multi_2(iot, ioh, txreg,
				    data, m->m_len & ~1);
				if (m->m_len & 1)
					bus_space_write_1(iot, ioh, txreg,
					     *(data + m->m_len - 1));
			} else
				bus_space_write_multi_1(iot, ioh, txreg,
				    data, m->m_len);
			m0 = m_free(m);
			m = m0;
		}
	}
	while (pad--)
		bus_space_write_1(iot, ioh, txreg, 0);

	splx(sh);

readcheck:
	if ((bus_space_read_2(iot, ioh, ep_w1_reg(sc, EP_W1_RX_STATUS)) &
	    ERR_INCOMPLETE) == 0) {
		/* We received a complete packet. */
		u_int16_t status = bus_space_read_2(iot, ioh, EP_STATUS);

		if ((status & S_INTR_LATCH) == 0) {
			/*
			 * No interrupt, read the packet and continue
			 * Is  this supposed to happen? Is my motherboard 
			 * completely busted?
			 */
			epread(sc);
		} else
			/* Got an interrupt, return to get it serviced. */
			return;
	} else {
		/* Check if we are stuck and reset [see XXX comment] */
		if (epstatus(sc)) {
#ifdef EP_DEBUG
			if (ifp->if_flags & IFF_DEBUG)
				printf("%s: adapter reset\n",
				    sc->sc_dev.dv_xname);
#endif
			epreset(sc);
		}
	}

	goto startagain;
}


/*
 * XXX: The 3c509 card can get in a mode where both the fifo status bit
 *	FIFOS_RX_OVERRUN and the status bit ERR_INCOMPLETE are set
 *	We detect this situation and we reset the adapter.
 *	It happens at times when there is a lot of broadcast traffic
 *	on the cable (once in a blue moon).
 */
int
epstatus(struct ep_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	u_int16_t fifost;

	/*
	 * Check the FIFO status and act accordingly
	 */
	GO_WINDOW(4);
	fifost = bus_space_read_2(iot, ioh, EP_W4_FIFO_DIAG);
	GO_WINDOW(1);

	if (fifost & FIFOS_RX_UNDERRUN) {
#ifdef EP_DEBUG
		if (sc->sc_arpcom.ac_if.if_flags & IFF_DEBUG)
			printf("%s: RX underrun\n", sc->sc_dev.dv_xname);
#endif
		epreset(sc);
		return 0;
	}

	if (fifost & FIFOS_RX_STATUS_OVERRUN) {
#ifdef EP_DEBUG
		if (sc->sc_arpcom.ac_if.if_flags & IFF_DEBUG)
			printf("%s: RX Status overrun\n", sc->sc_dev.dv_xname);
#endif
		return 1;
	}

	if (fifost & FIFOS_RX_OVERRUN) {
#ifdef EP_DEBUG
		if (sc->sc_arpcom.ac_if.if_flags & IFF_DEBUG)
			printf("%s: RX overrun\n", sc->sc_dev.dv_xname);
#endif
		return 1;
	}

	if (fifost & FIFOS_TX_OVERRUN) {
#ifdef EP_DEBUG
		if (sc->sc_arpcom.ac_if.if_flags & IFF_DEBUG)
			printf("%s: TX overrun\n", sc->sc_dev.dv_xname);
#endif
		epreset(sc);
		return 0;
	}

	return 0;
}


void
eptxstat(struct ep_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	int i;

	/*
	 * We need to read+write TX_STATUS until we get a 0 status
	 * in order to turn off the interrupt flag.
	 */
	while ((i = bus_space_read_1(iot, ioh,
	    ep_w1_reg(sc, EP_W1_TX_STATUS))) & TXS_COMPLETE) {
		bus_space_write_1(iot, ioh, ep_w1_reg(sc, EP_W1_TX_STATUS),
		    0x0);

		if (i & TXS_JABBER) {
			++sc->sc_arpcom.ac_if.if_oerrors;
#ifdef EP_DEBUG
			if (sc->sc_arpcom.ac_if.if_flags & IFF_DEBUG)
				printf("%s: jabber (%x)\n",
				       sc->sc_dev.dv_xname, i);
#endif
			epreset(sc);
		} else if (i & TXS_UNDERRUN) {
			++sc->sc_arpcom.ac_if.if_oerrors;
#ifdef EP_DEBUG
			if (sc->sc_arpcom.ac_if.if_flags & IFF_DEBUG)
				printf("%s: fifo underrun (%x) @%d\n",
				       sc->sc_dev.dv_xname, i,
				       sc->tx_start_thresh);
#endif
			if (sc->tx_succ_ok < 100)
				    sc->tx_start_thresh = min(ETHER_MAX_LEN,
					    sc->tx_start_thresh + 20);
			sc->tx_succ_ok = 0;
			epreset(sc);
		} else if (i & TXS_MAX_COLLISION) {
			++sc->sc_arpcom.ac_if.if_collisions;
			bus_space_write_2(iot, ioh, EP_COMMAND, TX_ENABLE);
			ifq_clr_oactive(&sc->sc_arpcom.ac_if.if_snd);
		} else
			sc->tx_succ_ok = (sc->tx_succ_ok+1) & 127;
	}
}

int
epintr(void *arg)
{
	register struct ep_softc *sc = arg;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	u_int16_t status;
	int ret = 0;

	for (;;) {
		bus_space_write_2(iot, ioh, EP_COMMAND, C_INTR_LATCH);

		status = bus_space_read_2(iot, ioh, EP_STATUS);

		if ((status & (S_TX_COMPLETE | S_TX_AVAIL |
			       S_RX_COMPLETE | S_CARD_FAILURE)) == 0)
			break;

		ret = 1;

		/*
		 * Acknowledge any interrupts.  It's important that we do this
		 * first, since there would otherwise be a race condition.
		 * Due to the i386 interrupt queueing, we may get spurious
		 * interrupts occasionally.
		 */
		bus_space_write_2(iot, ioh, EP_COMMAND, ACK_INTR | status);

		if (status & S_RX_COMPLETE)
			epread(sc);
		if (status & S_TX_AVAIL) {
			ifq_clr_oactive(&ifp->if_snd);
			epstart(ifp);
		}
		if (status & S_CARD_FAILURE) {
			epreset(sc);
			return (1);
		}
		if (status & S_TX_COMPLETE) {
			eptxstat(sc);
			epstart(ifp);
		}
	}	

	/* no more interrupts */
	return (ret);
}

void
epread(struct ep_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *m;
	int len, error = 0;

	len = bus_space_read_2(iot, ioh, ep_w1_reg(sc, EP_W1_RX_STATUS));

again:
#ifdef EP_DEBUG
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
			printf("%s: %s\n", sc->sc_dev.dv_xname, s);
	}
#endif

	if (len & ERR_INCOMPLETE)
		goto done;

	if (len & ERR_RX) {
		++ifp->if_ierrors;
		error = 1;
		goto done;
	}

	len &= RX_BYTES_MASK;	/* Lower 11 bits = RX bytes. */

	/* Pull packet off interface. */
	m = epget(sc, len);
	if (m == NULL) {
		ifp->if_ierrors++;
		error = 1;
		goto done;
	}

	ml_enqueue(&ml, m);

	/*
	 * In periods of high traffic we can actually receive enough
	 * packets so that the fifo overrun bit will be set at this point,
	 * even though we just read a packet. In this case we
	 * are not going to receive any more interrupts. We check for
	 * this condition and read again until the fifo is not full.
	 * We could simplify this test by not using epstatus(), but
	 * rechecking the RX_STATUS register directly. This test could
	 * result in unnecessary looping in cases where there is a new
	 * packet but the fifo is not full, but it will not fix the
	 * stuck behavior.
	 *
	 * Even with this improvement, we still get packet overrun errors
	 * which are hurting performance. Maybe when I get some more time
	 * I'll modify epread() so that it can handle RX_EARLY interrupts.
	 */
	if (epstatus(sc)) {
		len = bus_space_read_2(iot, ioh,
		    ep_w1_reg(sc, EP_W1_RX_STATUS));
		/* Check if we are stuck and reset [see XXX comment] */
		if (len & ERR_INCOMPLETE) {
#ifdef EP_DEBUG
			if (ifp->if_flags & IFF_DEBUG)
				printf("%s: adapter reset\n",
				    sc->sc_dev.dv_xname);
#endif
			epreset(sc);
			goto done;
		}
		goto again;
	}
done:
	if (error)
		ep_discard_rxtop(iot, ioh);
	if_input(ifp, &ml);
}

struct mbuf *
epget(struct ep_softc *sc, int totlen)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	struct mbuf *m;
	caddr_t data;
	int len, pad, off, sh, rxreg;

	splassert(IPL_NET);

	m = sc->mb[sc->next_mb];
	sc->mb[sc->next_mb] = NULL;
	if (m == NULL) {
		m = MCLGETL(NULL, M_DONTWAIT, MCLBYTES);
		/* If the queue is no longer full, refill. */
		if (!timeout_pending(&sc->sc_epmbuffill_tmo))
			timeout_add(&sc->sc_epmbuffill_tmo, 1);
	}
	if (!m)
		return (NULL);

	sc->next_mb = (sc->next_mb + 1) % MAX_MBS;

	len = MCLBYTES;
	m->m_pkthdr.len = totlen;
	m->m_len = totlen;
	pad = ALIGN(sizeof(struct ether_header)) - sizeof(struct ether_header);
	m->m_data += pad;
	len -= pad;

	/*
	 * We read the packet at splhigh() so that an interrupt from another
	 * device doesn't cause the card's buffer to overflow while we're
	 * reading it.  We may still lose packets at other times.
	 */
	sh = splhigh();

	rxreg = ep_w1_reg(sc, EP_W1_RX_PIO_RD_1);

	off = 0;
	while (totlen) {
		len = min(totlen, m_trailingspace(m));
		if (len == 0)
			panic("ep_get: packet does not fit in MCLBYTES");

		data = mtod(m, u_int8_t *);
		if (EP_IS_BUS_32(sc->bustype))
			pad = 4 - ((u_long)(data + off) & 0x3);
		else
			pad = (u_long)(data + off) & 0x1;

		if (pad) {
			if (pad < len)
				pad = len;
			bus_space_read_multi_1(iot, ioh, rxreg,
			    data + off, pad);
			len = pad;
		} else if (EP_IS_BUS_32(sc->bustype) && len > 3 &&
		    ALIGNED_POINTER(data, uint32_t)) {
			len &= ~3;
			bus_space_read_raw_multi_4(iot, ioh, rxreg,
			    data + off, len);
		} else if (len > 1 && ALIGNED_POINTER(data, uint16_t)) {
			len &= ~1;
			bus_space_read_raw_multi_2(iot, ioh, rxreg,
			    data + off, len);
		} else
			bus_space_read_multi_1(iot, ioh, rxreg,
			    data + off, len);

		off += len;
		totlen -= len;
	}

	ep_discard_rxtop(iot, ioh);

	splx(sh);

	return m;
}

int
epioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct ep_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			epinit(sc);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				epinit(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				epstop(sc);
 		}
 		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_mii.mii_media, cmd);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_arpcom, cmd, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			epreset(sc);
		error = 0;
	}

	splx(s);
	return (error);
}

void
epreset(struct ep_softc *sc)
{
	int s;

	s = splnet();
	epinit(sc);
	splx(s);
}

void
epwatchdog(struct ifnet *ifp)
{
	struct ep_softc *sc = ifp->if_softc;

	log(LOG_ERR, "%s: device timeout\n", sc->sc_dev.dv_xname);
	++sc->sc_arpcom.ac_if.if_oerrors;

	epreset(sc);
}

void
epstop(struct ep_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	if (sc->ep_flags & EP_FLAGS_MII) {
		mii_down(&sc->sc_mii);
	}

	if (sc->ep_chipset == EP_CHIPSET_ROADRUNNER) {
		/* Clear the FIFO buffer count, thus halting
		 * any currently-running transactions.
		 */
		GO_WINDOW(1);		/* sanity */
		bus_space_write_2(iot, ioh, EP_W1_RUNNER_WRCTL, 0);
		bus_space_write_2(iot, ioh, EP_W1_RUNNER_RDCTL, 0);
	}

	bus_space_write_2(iot, ioh, EP_COMMAND, RX_DISABLE);
	ep_discard_rxtop(iot, ioh);

	bus_space_write_2(iot, ioh, EP_COMMAND, TX_DISABLE);
	bus_space_write_2(iot, ioh, EP_COMMAND, STOP_TRANSCEIVER);

	ep_reset_cmd(sc, EP_COMMAND, RX_RESET);
	ep_reset_cmd(sc, EP_COMMAND, TX_RESET);

	bus_space_write_2(iot, ioh, EP_COMMAND, C_INTR_LATCH);
	bus_space_write_2(iot, ioh, EP_COMMAND, SET_RD_0_MASK);
	bus_space_write_2(iot, ioh, EP_COMMAND, SET_INTR_MASK);
	bus_space_write_2(iot, ioh, EP_COMMAND, SET_RX_FILTER);

	epmbufempty(sc);
}

/*
 * We get eeprom data from the id_port given an offset into the
 * eeprom.  Basically; after the ID_sequence is sent to all of
 * the cards; they enter the ID_CMD state where they will accept
 * command requests. 0x80-0xbf loads the eeprom data.  We then
 * read the port 16 times and with every read; the cards check
 * for contention (ie: if one card writes a 0 bit and another
 * writes a 1 bit then the host sees a 0. At the end of the cycle;
 * each card compares the data on the bus; if there is a difference
 * then that card goes into ID_WAIT state again). In the meantime;
 * one bit of data is returned in the AX register which is conveniently
 * returned to us by bus_space_read_1().  Hence; we read 16 times getting one
 * bit of data with each read.
 *
 * NOTE: the caller must provide an i/o handle for ELINK_ID_PORT!
 */
u_int16_t
epreadeeprom(bus_space_tag_t iot, bus_space_handle_t ioh, int offset)
{
	u_int16_t data = 0;
	int i;

	bus_space_write_1(iot, ioh, 0, 0x80 + offset);
	delay(1000);
	for (i = 0; i < 16; i++)
		data = (data << 1) | (bus_space_read_2(iot, ioh, 0) & 1);
	return (data);
}

int
epbusyeeprom(struct ep_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	int i = 100, j;

	while (i--) {
		j = bus_space_read_2(iot, ioh, EP_W0_EEPROM_COMMAND);
		if (j & EEPROM_BUSY)
			delay(100);
		else
			break;
	}
	if (!i) {
		printf("\n%s: eeprom failed to come ready\n",
		    sc->sc_dev.dv_xname);
		return (1);
	}
	if (sc->bustype != EP_BUS_PCMCIA && sc->bustype != EP_BUS_PCI &&
	    (j & EEPROM_TST_MODE)) {
		printf("\n%s: erase pencil mark, or disable PnP mode!\n",
		    sc->sc_dev.dv_xname);
		return (1);
	}
	return (0);
}

u_int16_t
ep_read_eeprom(struct ep_softc *sc, u_int16_t offset)
{
	u_int16_t readcmd;

	/*
	 * RoadRunner has a larger EEPROM, so a different read command
	 * is required.
	 */
	if (sc->ep_chipset == EP_CHIPSET_ROADRUNNER)
		readcmd = READ_EEPROM_RR;
	else
		readcmd = READ_EEPROM;

	if (epbusyeeprom(sc))
		return (0);			/* XXX why is eeprom busy? */
	bus_space_write_2(sc->sc_iot, sc->sc_ioh, EP_W0_EEPROM_COMMAND,
	    readcmd | offset);
	if (epbusyeeprom(sc))
		return (0);			/* XXX why is eeprom busy? */

	return (bus_space_read_2(sc->sc_iot, sc->sc_ioh, EP_W0_EEPROM_DATA));
}

void
epmbuffill(void *v)
{
	struct ep_softc *sc = v;
	int s, i;

	s = splnet();
	for (i = 0; i < MAX_MBS; i++) {
		if (sc->mb[i] == NULL) {
			sc->mb[i] = MCLGETL(NULL, M_DONTWAIT, MCLBYTES);
			if (sc->mb[i] == NULL)
				break;
		}
	}
	/* If the queue was not filled, try again. */
	if (i < MAX_MBS)
		timeout_add(&sc->sc_epmbuffill_tmo, 1);
	splx(s);
}

void
epmbufempty(struct ep_softc *sc)
{
	int s, i;

	s = splnet();
	for (i = 0; i<MAX_MBS; i++) {
		if (sc->mb[i]) {
			m_freem(sc->mb[i]);
			sc->mb[i] = NULL;
		}
	}
	sc->next_mb = 0;
	timeout_del(&sc->sc_epmbuffill_tmo);
	splx(s);
}

void
ep_mii_setbit(struct ep_softc *sc, u_int16_t bit)
{
        u_int16_t val;

        /* We assume we're already in Window 4 */
        val = bus_space_read_2(sc->sc_iot, sc->sc_ioh, EP_W4_BOOM_PHYSMGMT);
        bus_space_write_2(sc->sc_iot, sc->sc_ioh, EP_W4_BOOM_PHYSMGMT,
            val | bit);
}

void
ep_mii_clrbit(struct ep_softc *sc, u_int16_t bit)
{
        u_int16_t val;

        /* We assume we're already in Window 4 */
        val = bus_space_read_2(sc->sc_iot, sc->sc_ioh, EP_W4_BOOM_PHYSMGMT);
        bus_space_write_2(sc->sc_iot, sc->sc_ioh, EP_W4_BOOM_PHYSMGMT,
            val & ~bit);
}

u_int16_t
ep_mii_readbit(struct ep_softc *sc, u_int16_t bit)
{

        /* We assume we're already in Window 4 */
        return (bus_space_read_2(sc->sc_iot, sc->sc_ioh, EP_W4_BOOM_PHYSMGMT) &
            bit);
}

void
ep_mii_sync(struct ep_softc *sc)
{
        int i;

        /* We assume we're already in Window 4 */
        ep_mii_clrbit(sc, PHYSMGMT_DIR);
        for (i = 0; i < 32; i++) {
                ep_mii_clrbit(sc, PHYSMGMT_CLK);
                ep_mii_setbit(sc, PHYSMGMT_CLK);
        }
}

void
ep_mii_sendbits(struct ep_softc *sc, u_int32_t data, int nbits)
{
        int i;

        /* We assume we're already in Window 4 */
        ep_mii_setbit(sc, PHYSMGMT_DIR);
        for (i = 1 << (nbits - 1); i; i = i >> 1) {
                ep_mii_clrbit(sc, PHYSMGMT_CLK);
                ep_mii_readbit(sc, PHYSMGMT_CLK);
                if (data & i)
                        ep_mii_setbit(sc, PHYSMGMT_DATA);
                else
                        ep_mii_clrbit(sc, PHYSMGMT_DATA);
                ep_mii_setbit(sc, PHYSMGMT_CLK);
                ep_mii_readbit(sc, PHYSMGMT_CLK);
        }
}

int
ep_mii_readreg(struct device *self, int phy, int reg)
{
        struct ep_softc *sc = (struct ep_softc *)self;
        int val = 0, i, err;

        /*
         * Read the PHY register by manually driving the MII control lines.
         */

        GO_WINDOW(4);

        bus_space_write_2(sc->sc_iot, sc->sc_ioh, EP_W4_BOOM_PHYSMGMT, 0);

        ep_mii_sync(sc);
        ep_mii_sendbits(sc, MII_COMMAND_START, 2);
        ep_mii_sendbits(sc, MII_COMMAND_READ, 2);
        ep_mii_sendbits(sc, phy, 5);
        ep_mii_sendbits(sc, reg, 5);

        ep_mii_clrbit(sc, PHYSMGMT_DIR);
        ep_mii_clrbit(sc, PHYSMGMT_CLK);
        ep_mii_setbit(sc, PHYSMGMT_CLK);
        ep_mii_clrbit(sc, PHYSMGMT_CLK);

        err = ep_mii_readbit(sc, PHYSMGMT_DATA);
        ep_mii_setbit(sc, PHYSMGMT_CLK);

        /* Even if an error occurs, must still clock out the cycle. */
        for (i = 0; i < 16; i++) {
                val <<= 1;
                ep_mii_clrbit(sc, PHYSMGMT_CLK);
                if (err == 0 && ep_mii_readbit(sc, PHYSMGMT_DATA))
                        val |= 1;
                ep_mii_setbit(sc, PHYSMGMT_CLK);
        }
        ep_mii_clrbit(sc, PHYSMGMT_CLK);
        ep_mii_setbit(sc, PHYSMGMT_CLK);

        GO_WINDOW(1);   /* back to operating window */

        return (err ? 0 : val);
}

void
ep_mii_writereg(struct device *self, int phy, int reg, int val)
{
        struct ep_softc *sc = (struct ep_softc *)self;

        /*
         * Write the PHY register by manually driving the MII control lines.
         */

        GO_WINDOW(4);

        ep_mii_sync(sc);
        ep_mii_sendbits(sc, MII_COMMAND_START, 2);
        ep_mii_sendbits(sc, MII_COMMAND_WRITE, 2);
        ep_mii_sendbits(sc, phy, 5);
        ep_mii_sendbits(sc, reg, 5);
        ep_mii_sendbits(sc, MII_COMMAND_ACK, 2);
        ep_mii_sendbits(sc, val, 16);

        ep_mii_clrbit(sc, PHYSMGMT_CLK);
        ep_mii_setbit(sc, PHYSMGMT_CLK);

        GO_WINDOW(1);   /* back to operating window */
}

void
ep_statchg(struct device *self)
{
        struct ep_softc *sc = (struct ep_softc *)self;
        bus_space_tag_t iot = sc->sc_iot;
        bus_space_handle_t ioh = sc->sc_ioh;
        int mctl;

        /* XXX Update ifp->if_baudrate */

        GO_WINDOW(3);
        mctl = bus_space_read_2(iot, ioh, EP_W3_MAC_CONTROL);
        if (sc->sc_mii.mii_media_active & IFM_FDX)
                mctl |= MAC_CONTROL_FDX;
        else
                mctl &= ~MAC_CONTROL_FDX;
        bus_space_write_2(iot, ioh, EP_W3_MAC_CONTROL, mctl);
        GO_WINDOW(1);   /* back to operating window */
}
