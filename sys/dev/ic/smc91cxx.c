/*	$OpenBSD: smc91cxx.c,v 1.54 2025/06/25 20:28:09 miod Exp $	*/
/*	$NetBSD: smc91cxx.c,v 1.11 1998/08/08 23:51:41 mycroft Exp $	*/

/*-
 * Copyright (c) 1997 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*      
 * Copyright (c) 1996 Gardner Buchanan <gbuchanan@shl.com>
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
 *	This product includes software developed by Gardner Buchanan.
 * 4. The name of Gardner Buchanan may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
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
 *   from FreeBSD Id: if_sn.c,v 1.4 1996/03/18 15:47:16 gardner Exp
 */      

/*
 * Core driver for the SMC 91Cxx family of Ethernet chips.
 *
 * Memory allocation interrupt logic is derived from an SMC 91C90 driver
 * written for NetBSD/amiga by Michael Hitch.
 */

#include "bpfilter.h"

#include <sys/param.h> 
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/syslog.h>
#include <sys/socket.h>
#include <sys/device.h>
#include <sys/timeout.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/ioctl.h> 
#include <sys/errno.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <net/if.h>
#include <net/if_media.h> 

#include <netinet/in.h> 
#include <netinet/if_ether.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include <dev/mii/mii_bitbang.h>

#include <dev/ic/smc91cxxreg.h>
#include <dev/ic/smc91cxxvar.h>

#ifndef __BUS_SPACE_HAS_STREAM_METHODS
#define bus_space_write_multi_stream_2 bus_space_write_multi_2
#define bus_space_write_multi_stream_4 bus_space_write_multi_4
#define bus_space_read_multi_stream_2  bus_space_read_multi_2
#define bus_space_read_multi_stream_4  bus_space_read_multi_4
#endif /* __BUS_SPACE_HAS_STREAM_METHODS */

/* XXX Hardware padding doesn't work yet(?) */
#define	SMC91CXX_SW_PAD

const char *smc91cxx_idstrs[] = {
	NULL,				/* 0 */
	NULL,				/* 1 */
	NULL,				/* 2 */
	"SMC91C90/91C92",		/* 3 */
	"SMC91C94/91C96",		/* 4 */
	"SMC91C95",			/* 5 */
	NULL,				/* 6 */
	"SMC91C100",			/* 7 */
	"SMC91C100FD",			/* 8 */
	NULL,				/* 9 */
	NULL,				/* 10 */
	NULL,				/* 11 */
	NULL,				/* 12 */
	NULL,				/* 13 */
	NULL,				/* 14 */
	NULL,				/* 15 */
};

/* Supported media types. */
const uint64_t smc91cxx_media[] = {
	IFM_ETHER|IFM_10_T,
	IFM_ETHER|IFM_10_5,
};
#define	NSMC91CxxMEDIA	(sizeof(smc91cxx_media) / sizeof(smc91cxx_media[0]))

/*
 * MII bit-bang glue.
 */
u_int32_t smc91cxx_mii_bitbang_read(struct device *);
void smc91cxx_mii_bitbang_write(struct device *, u_int32_t);

const struct mii_bitbang_ops smc91cxx_mii_bitbang_ops = {
	smc91cxx_mii_bitbang_read,
	smc91cxx_mii_bitbang_write,
	{
		MR_MDO,		/* MII_BIT_MDO */
		MR_MDI,		/* MII_BIT_MDI */
		MR_MCLK,	/* MII_BIT_MDC */
		MR_MDOE,	/* MII_BIT_DIR_HOST_PHY */
		0,		/* MII_BIT_DIR_PHY_HOST */
	}
};

struct cfdriver sm_cd = {
	NULL, "sm", DV_IFNET
};

/* MII callbacks */
int	smc91cxx_mii_readreg(struct device *, int, int);
void	smc91cxx_mii_writereg(struct device *, int, int, int);
void	smc91cxx_statchg(struct device *);
void	smc91cxx_tick(void *);

int	smc91cxx_mediachange(struct ifnet *);
void	smc91cxx_mediastatus(struct ifnet *, struct ifmediareq *);

int	smc91cxx_set_media(struct smc91cxx_softc *, uint64_t);

void	smc91cxx_read(struct smc91cxx_softc *);
void	smc91cxx_reset(struct smc91cxx_softc *);
void	smc91cxx_start(struct ifnet *);
void	smc91cxx_watchdog(struct ifnet *);
int	smc91cxx_ioctl(struct ifnet *, u_long, caddr_t);

void
smc91cxx_attach(struct smc91cxx_softc *sc, u_int8_t *myea)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	bus_space_tag_t bst = sc->sc_bst;
	bus_space_handle_t bsh = sc->sc_bsh;
	struct ifmedia *ifm = &sc->sc_mii.mii_media;
	u_int32_t miicapabilities;
	u_int16_t tmp;
	int i, aui;
	const char *idstr;

	/* Make sure the chip is stopped. */
	smc91cxx_stop(sc);

	SMC_SELECT_BANK(sc, 3);
	tmp = bus_space_read_2(bst, bsh, REVISION_REG_W);
	sc->sc_chipid = RR_ID(tmp);
	/* check magic number */
	if ((tmp & BSR_DETECT_MASK) != BSR_DETECT_VALUE) {
		idstr = NULL;
		printf("%s: invalid BSR 0x%04x\n", sc->sc_dev.dv_xname, tmp);
	} else
		idstr = smc91cxx_idstrs[RR_ID(tmp)];
#ifdef SMC_DEBUG
	printf("\n%s: ", sc->sc_dev.dv_xname);
	if (idstr != NULL)
		printf("%s, ", idstr);
	else
		printf("unknown chip id %d, ", sc->sc_chipid);
	printf("revision %d", RR_REV(tmp));
#endif

	/* Read the station address from the chip. */
	SMC_SELECT_BANK(sc, 1);
	if (myea == NULL) {
		for (i = 0; i < ETHER_ADDR_LEN; i += 2) {
			tmp = bus_space_read_2(bst, bsh, IAR_ADDR0_REG_W + i);
			sc->sc_arpcom.ac_enaddr[i + 1] = (tmp >>8) & 0xff;
			sc->sc_arpcom.ac_enaddr[i] = tmp & 0xff;
		}
	} else {
		bcopy(myea, sc->sc_arpcom.ac_enaddr, ETHER_ADDR_LEN);
	}

	printf(": address %s\n", ether_sprintf(sc->sc_arpcom.ac_enaddr));

	/* Initialize the ifnet structure. */
	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);
	ifp->if_softc = sc;
	ifp->if_start = smc91cxx_start;
	ifp->if_ioctl = smc91cxx_ioctl;
	ifp->if_watchdog = smc91cxx_watchdog;
	ifp->if_flags =
	    IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;

	/* Attach the interface. */
	if_attach(ifp);
	ether_ifattach(ifp);

	/*
	 * Initialize our media structures and MII info.  We will
	 * probe the MII if we are on the SMC91Cxx
	 */
	sc->sc_mii.mii_ifp = ifp;
	sc->sc_mii.mii_readreg = smc91cxx_mii_readreg;
	sc->sc_mii.mii_writereg = smc91cxx_mii_writereg;
	sc->sc_mii.mii_statchg = smc91cxx_statchg;
	ifmedia_init(ifm, 0, smc91cxx_mediachange, smc91cxx_mediastatus);

	SMC_SELECT_BANK(sc, 1);
	tmp = bus_space_read_2(bst, bsh, CONFIG_REG_W);

	miicapabilities = BMSR_MEDIAMASK|BMSR_ANEG;
	switch (sc->sc_chipid) {
	case CHIP_91100:
		/*
		 * The 91100 does not have full-duplex capabilities,
		 * even if the PHY does.
		 */
		miicapabilities &= ~(BMSR_100TXFDX | BMSR_10TFDX);
		/* FALLTHROUGH */
	case CHIP_91100FD:
		if (tmp & CR_MII_SELECT) {
#ifdef SMC_DEBUG
			printf("%s: default media MII\n",
			    sc->sc_dev.dv_xname);
#endif
			mii_attach(&sc->sc_dev, &sc->sc_mii, miicapabilities,
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
			sc->sc_flags |= SMC_FLAGS_HAS_MII;
			break;
		}
		/* FALLTHROUGH */
	default:
		aui = tmp & CR_AUI_SELECT;
#ifdef SMC_DEBUG
		printf("%s: default media %s\n", sc->sc_dev.dv_xname,
			aui ? "AUI" : "UTP");
#endif
		for (i = 0; i < NSMC91CxxMEDIA; i++)
			ifmedia_add(ifm, smc91cxx_media[i], 0, NULL);
		ifmedia_set(ifm, IFM_ETHER | (aui ? IFM_10_5 : IFM_10_T));
		break;
	}

	/* The attach is successful. */
	sc->sc_flags |= SMC_FLAGS_ATTACHED;
}

/*
 * Change media according to request.
 */
int
smc91cxx_mediachange(struct ifnet *ifp)
{
	struct smc91cxx_softc *sc = ifp->if_softc;

	return (smc91cxx_set_media(sc, sc->sc_mii.mii_media.ifm_media));
}

int
smc91cxx_set_media(struct smc91cxx_softc *sc, uint64_t media)
{
	bus_space_tag_t bst = sc->sc_bst;
	bus_space_handle_t bsh = sc->sc_bsh;
	u_int16_t tmp;

	/*
	 * If the interface is not currently powered on, just return.
	 * When it is enabled later, smc91cxx_init() will properly set
	 * up the media for us.
	 */
	if ((sc->sc_flags & SMC_FLAGS_ENABLED) == 0)
		return (0);

	if (IFM_TYPE(media) != IFM_ETHER)
		return (EINVAL);

	if (sc->sc_flags & SMC_FLAGS_HAS_MII)
		return (mii_mediachg(&sc->sc_mii));

	switch (IFM_SUBTYPE(media)) {
	case IFM_10_T:
	case IFM_10_5:
		SMC_SELECT_BANK(sc, 1);
		tmp = bus_space_read_2(bst, bsh, CONFIG_REG_W);
		if (IFM_SUBTYPE(media) == IFM_10_5)
			tmp |= CR_AUI_SELECT;
		else
			tmp &= ~CR_AUI_SELECT;
		bus_space_write_2(bst, bsh, CONFIG_REG_W, tmp);
		delay(20000);	/* XXX is this needed? */
		break;

	default:
		return (EINVAL);
	}

	return (0);
}

/*
 * Notify the world which media we're using.
 */
void
smc91cxx_mediastatus(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct smc91cxx_softc *sc = ifp->if_softc;
	bus_space_tag_t bst = sc->sc_bst;
	bus_space_handle_t bsh = sc->sc_bsh;
	u_int16_t tmp;

	if ((sc->sc_flags & SMC_FLAGS_ENABLED) == 0) {
		ifmr->ifm_active = IFM_ETHER | IFM_NONE;
		ifmr->ifm_status = 0;
		return;
	}

	/*
	 * If we have MII, go ask the PHY what's going on.
	 */
	if (sc->sc_flags & SMC_FLAGS_HAS_MII) {
		mii_pollstat(&sc->sc_mii);
		ifmr->ifm_active = sc->sc_mii.mii_media_active;
		ifmr->ifm_status = sc->sc_mii.mii_media_status;
		return;
	}

	SMC_SELECT_BANK(sc, 1);
	tmp = bus_space_read_2(bst, bsh, CONFIG_REG_W);
	ifmr->ifm_active =
	    IFM_ETHER | ((tmp & CR_AUI_SELECT) ? IFM_10_5 : IFM_10_T);
}

/*
 * Reset and initialize the chip.
 */
void
smc91cxx_init(struct smc91cxx_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	bus_space_tag_t bst = sc->sc_bst;
	bus_space_handle_t bsh = sc->sc_bsh;
	u_int16_t tmp;
	int s, i;

	s = splnet();

	/*
	 * This resets the registers mostly to defaults, but doesn't
	 * affect the EEPROM.  After the reset cycle, we pause briefly
	 * for the chip to recover.
	 *
	 * XXX how long are we really supposed to delay?  --thorpej
	 */
	SMC_SELECT_BANK(sc, 0);
	bus_space_write_2(bst, bsh, RECV_CONTROL_REG_W, RCR_SOFTRESET);
	delay(100);
	bus_space_write_2(bst, bsh, RECV_CONTROL_REG_W, 0);
	delay(200);

	bus_space_write_2(bst, bsh, TXMIT_CONTROL_REG_W, 0);

	/* Set the Ethernet address. */
	SMC_SELECT_BANK(sc, 1);
	for (i = 0; i < ETHER_ADDR_LEN; i++ )
		bus_space_write_1(bst, bsh, IAR_ADDR0_REG_W + i,
		    sc->sc_arpcom.ac_enaddr[i]);

	/*
	 * Set the control register to automatically release successfully
	 * transmitted packets (making the best use of our limited memory)
	 * and enable the EPH interrupt on certain TX errors.
	 */
	bus_space_write_2(bst, bsh, CONTROL_REG_W, (CTR_AUTO_RELEASE |
	    CTR_TE_ENABLE | CTR_CR_ENABLE | CTR_LE_ENABLE));

	/*
	 * Reset the MMU and wait for it to be un-busy.
	 */
	SMC_SELECT_BANK(sc, 2);
	bus_space_write_2(bst, bsh, MMU_CMD_REG_W, MMUCR_RESET);
	while (bus_space_read_2(bst, bsh, MMU_CMD_REG_W) & MMUCR_BUSY)
		/* XXX bound this loop! */ ;

	/*
	 * Disable all interrupts.
	 */
	bus_space_write_1(bst, bsh, INTR_MASK_REG_B, 0);

	/*
	 * Set current media.
	 */
	smc91cxx_set_media(sc, sc->sc_mii.mii_media.ifm_cur->ifm_media);

	/*
	 * Set the receive filter.  We want receive enable and auto
	 * strip of CRC from received packet.  If we are in promisc. mode,
	 * then set that bit as well.
	 *
	 * XXX Initialize multicast filter.  For now, we just accept
	 * XXX all multicast.
	 */
	SMC_SELECT_BANK(sc, 0);

	tmp = RCR_ENABLE | RCR_STRIP_CRC | RCR_ALMUL;
	if (ifp->if_flags & IFF_PROMISC)
		tmp |= RCR_PROMISC;

	bus_space_write_2(bst, bsh, RECV_CONTROL_REG_W, tmp);

	/*
	 * Set transmitter control to "enabled".
	 */
	tmp = TCR_ENABLE;

#ifndef SMC91CXX_SW_PAD
	/*
	 * Enable hardware padding of transmitted packets.
	 * XXX doesn't work?
	 */
	tmp |= TCR_PAD_ENABLE;
#endif

	bus_space_write_2(bst, bsh, TXMIT_CONTROL_REG_W, tmp);

	/*
	 * Now, enable interrupts.
	 */
	SMC_SELECT_BANK(sc, 2);

	bus_space_write_1(bst, bsh, INTR_MASK_REG_B,
	    IM_EPH_INT | IM_RX_OVRN_INT | IM_RCV_INT | IM_TX_INT);

	/* Interface is now running, with no output active. */
	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	if (sc->sc_flags & SMC_FLAGS_HAS_MII) {
		/* Start the one second clock. */
		timeout_set(&sc->sc_mii_timeout, smc91cxx_tick, sc);
		timeout_add_sec(&sc->sc_mii_timeout, 1);
	}

	/*
	 * Attempt to start any pending transmission.
	 */
	smc91cxx_start(ifp);

	splx(s);
}

/*
 * Start output on an interface.
 * Must be called at splnet or interrupt level.
 */
void
smc91cxx_start(struct ifnet *ifp)
{
	struct smc91cxx_softc *sc = ifp->if_softc;
	bus_space_tag_t bst = sc->sc_bst;
	bus_space_handle_t bsh = sc->sc_bsh;
	u_int len;
	struct mbuf *m, *top;
	u_int16_t length, npages;
	u_int8_t packetno;
	int timo, pad;

	if (!(ifp->if_flags & IFF_RUNNING) || ifq_is_oactive(&ifp->if_snd))
		return;

 again:
	/*
	 * Peek at the next packet.
	 */
	m = ifq_deq_begin(&ifp->if_snd);
	if (m == NULL)
		return;

	/*
	 * Compute the frame length and set pad to give an overall even
	 * number of bytes.  Below, we assume that the packet length
	 * is even.
	 */
	for (len = 0, top = m; m != NULL; m = m->m_next)
		len += m->m_len;
	pad = (len & 1);

	/*
	 * We drop packets that are too large.  Perhaps we should
	 * truncate them instead?
	 */
	if ((len + pad) > (ETHER_MAX_LEN - ETHER_CRC_LEN)) {
		printf("%s: large packet discarded\n", sc->sc_dev.dv_xname);
		ifp->if_oerrors++;
		ifq_deq_commit(&ifp->if_snd, m);
		m_freem(m);
		goto readcheck;
	}

#ifdef SMC91CXX_SW_PAD
	/*
	 * Not using hardware padding; pad to ETHER_MIN_LEN.
	 */
	if (len < (ETHER_MIN_LEN - ETHER_CRC_LEN))
		pad = ETHER_MIN_LEN - ETHER_CRC_LEN - len;
#endif

	length = pad + len;

	/*
	 * The MMU has a 256 byte page size.  The MMU expects us to
	 * ask for "npages - 1".  We include space for the status word,
	 * byte count, and control bytes in the allocation request.
	 */
	npages = (length + 6) >> 8;

	/*
	 * Now allocate the memory.
	 */
	SMC_SELECT_BANK(sc, 2);
	bus_space_write_2(bst, bsh, MMU_CMD_REG_W, MMUCR_ALLOC | npages);

	timo = MEMORY_WAIT_TIME;
	do {
		if (bus_space_read_1(bst, bsh, INTR_STAT_REG_B) & IM_ALLOC_INT)
			break;
		delay(1);
	} while (--timo);

	packetno = bus_space_read_1(bst, bsh, ALLOC_RESULT_REG_B);

	if (packetno & ARR_FAILED || timo == 0) {
		/*
		 * No transmit memory is available.  Record the number
		 * of requested pages and enable the allocation completion
		 * interrupt.  Set up the watchdog timer in case we miss
		 * the interrupt.  Mark the interface as active so that
		 * no one else attempts to transmit while we're allocating
		 * memory.
		 */
		bus_space_write_1(bst, bsh, INTR_MASK_REG_B,
		    bus_space_read_1(bst, bsh, INTR_MASK_REG_B) | IM_ALLOC_INT);

		ifp->if_timer = 5;
		ifq_deq_rollback(&ifp->if_snd, m);
		ifq_set_oactive(&ifp->if_snd);
		return;
	}

	/*
	 * We have a packet number - set the data window.
	 */
	bus_space_write_1(bst, bsh, PACKET_NUM_REG_B, packetno);

	/*
	 * Point to the beginning of the packet.
	 */
	bus_space_write_2(bst, bsh, POINTER_REG_W, PTR_AUTOINC /* | 0x0000 */);

	/*
	 * Send the packet length (+6 for stats, length, and control bytes)
	 * and the status word (set to zeros).
	 */
	bus_space_write_2(bst, bsh, DATA_REG_W, 0);
	bus_space_write_1(bst, bsh, DATA_REG_B, (length + 6) & 0xff);
	bus_space_write_1(bst, bsh, DATA_REG_B, ((length + 6) >> 8) & 0xff);

	/*
	 * Get the packet from the kernel.  This will include the Ethernet
	 * frame header, MAC address, etc.
	 */
	ifq_deq_commit(&ifp->if_snd, m);

	/*
	 * Push the packet out to the card.
	 */
	for (top = m; m != NULL; m = m->m_next) {
		/* Words... */
		if (m->m_len > 1)
			bus_space_write_multi_stream_2(bst, bsh, DATA_REG_W,
			    mtod(m, u_int16_t *), m->m_len >> 1);

		/* ...and the remaining byte, if any. */
		if (m->m_len & 1)
			bus_space_write_1(bst, bsh, DATA_REG_B,
			  *(u_int8_t *)(mtod(m, u_int8_t *) + (m->m_len - 1)));
	}

#ifdef SMC91CXX_SW_PAD
	/*
	 * Push out padding.
	 */
	while (pad > 1) {
		bus_space_write_2(bst, bsh, DATA_REG_W, 0);
		pad -= 2;
	}
	if (pad)
		bus_space_write_1(bst, bsh, DATA_REG_B, 0);
#endif

	/*
	 * Push out control byte and unused packet byte.  The control byte
	 * is 0, meaning the packet is even lengthed and no special
	 * CRC handling is necessary.
	 */
	bus_space_write_2(bst, bsh, DATA_REG_W, 0);

	/*
	 * Enable transmit interrupts and let the chip go.  Set a watchdog
	 * in case we miss the interrupt.
	 */
	bus_space_write_1(bst, bsh, INTR_MASK_REG_B,
	    bus_space_read_1(bst, bsh, INTR_MASK_REG_B) |
	    IM_TX_INT | IM_TX_EMPTY_INT);

	bus_space_write_2(bst, bsh, MMU_CMD_REG_W, MMUCR_ENQUEUE);

	ifp->if_timer = 5;

#if NBPFILTER > 0
	if (ifp->if_bpf)
		bpf_mtap(ifp->if_bpf, top, BPF_DIRECTION_OUT);
#endif

	m_freem(top);

 readcheck:
	/*
	 * Check for incoming packets.  We don't want to overflow the small
	 * RX FIFO.  If nothing has arrived, attempt to queue another
	 * transmit packet.
	 */
	if (bus_space_read_2(bst, bsh, FIFO_PORTS_REG_W) & FIFO_REMPTY)
		goto again;
}

/*
 * Interrupt service routine.
 */
int
smc91cxx_intr(void *arg)
{
	struct smc91cxx_softc *sc = arg;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	bus_space_tag_t bst = sc->sc_bst;
	bus_space_handle_t bsh = sc->sc_bsh;
	u_int8_t mask, interrupts, status;
	u_int16_t packetno, tx_status, card_stats;

	if ((sc->sc_flags & SMC_FLAGS_ENABLED) == 0 ||
	    (sc->sc_dev.dv_flags & DVF_ACTIVE) == 0)
		return (0);

	SMC_SELECT_BANK(sc, 2);

	/*
	 * Obtain the current interrupt mask.
	 */
	mask = bus_space_read_1(bst, bsh, INTR_MASK_REG_B);

	/*
	 * Get the set of interrupt which occurred and eliminate any
	 * which are not enabled.
	 */
	interrupts = bus_space_read_1(bst, bsh, INTR_STAT_REG_B);
	status = interrupts & mask;

	/* Ours? */
	if (status == 0)
		return (0);

	/*
	 * It's ours; disable all interrupts while we process them.
	 */
	bus_space_write_1(bst, bsh, INTR_MASK_REG_B, 0);

	/*
	 * Receive overrun interrupts.
	 */
	if (status & IM_RX_OVRN_INT) {
		bus_space_write_1(bst, bsh, INTR_ACK_REG_B, IM_RX_OVRN_INT);
		ifp->if_ierrors++;
	}

	/*
	 * Receive interrupts.
	 */
	if (status & IM_RCV_INT) {
#if 1 /* DIAGNOSTIC */
		packetno = bus_space_read_2(bst, bsh, FIFO_PORTS_REG_W);
		if (packetno & FIFO_REMPTY) {
			printf("%s: receive interrupt on empty fifo\n",
			    sc->sc_dev.dv_xname);
			goto out;
		} else
#endif
		smc91cxx_read(sc);
	}

	/*
	 * Memory allocation interrupts.
	 */
	if (status & IM_ALLOC_INT) {
		/* Disable this interrupt. */
		mask &= ~IM_ALLOC_INT;

		/*
		 * Release the just-allocated memory.  We will reallocate
		 * it through the normal start logic.
		 */
		while (bus_space_read_2(bst, bsh, MMU_CMD_REG_W) & MMUCR_BUSY)
			/* XXX bound this loop! */ ;
		bus_space_write_2(bst, bsh, MMU_CMD_REG_W, MMUCR_FREEPKT);

		ifq_clr_oactive(&ifp->if_snd);
		ifp->if_timer = 0;
	}

	/*
	 * Transmit complete interrupt.  Handle transmission error messages.
	 * This will only be called on error condition because of AUTO RELEASE
	 * mode.
	 */
	if (status & IM_TX_INT) {
		bus_space_write_1(bst, bsh, INTR_ACK_REG_B, IM_TX_INT);

		packetno = bus_space_read_2(bst, bsh, FIFO_PORTS_REG_W) &
		    FIFO_TX_MASK;

		/*
		 * Select this as the packet to read from.
		 */
		bus_space_write_1(bst, bsh, PACKET_NUM_REG_B, packetno);

		/*
		 * Position the pointer to the beginning of the packet.
		 */
		bus_space_write_2(bst, bsh, POINTER_REG_W,
		    PTR_AUTOINC | PTR_READ /* | 0x0000 */);

		/*
		 * Fetch the TX status word.  This will be a copy of
		 * the EPH_STATUS_REG_W at the time of the transmission
		 * failure.
		 */
		tx_status = bus_space_read_2(bst, bsh, DATA_REG_W);

		if (tx_status & EPHSR_TX_SUC)
			printf("%s: successful packet caused TX interrupt?!\n",
			    sc->sc_dev.dv_xname);
		else
			ifp->if_oerrors++;

		if (tx_status & EPHSR_LATCOL)
			ifp->if_collisions++;

		/*
		 * Some of these errors disable the transmitter; reenable it.
		 */
		SMC_SELECT_BANK(sc, 0);
#ifdef SMC91CXX_SW_PAD
		bus_space_write_2(bst, bsh, TXMIT_CONTROL_REG_W, TCR_ENABLE);
#else
		bus_space_write_2(bst, bsh, TXMIT_CONTROL_REG_W,
		    TCR_ENABLE | TCR_PAD_ENABLE);
#endif

		/*
		 * Kill the failed packet and wait for the MMU to unbusy.
		 */
		SMC_SELECT_BANK(sc, 2);
		while (bus_space_read_2(bst, bsh, MMU_CMD_REG_W) & MMUCR_BUSY)
			/* XXX bound this loop! */ ;
		bus_space_write_2(bst, bsh, MMU_CMD_REG_W, MMUCR_FREEPKT);

		ifp->if_timer = 0;
	}

	/*
	 * Transmit underrun interrupts.  We use this opportunity to
	 * update transmit statistics from the card.
	 */
	if (status & IM_TX_EMPTY_INT) {
		bus_space_write_1(bst, bsh, INTR_ACK_REG_B, IM_TX_EMPTY_INT);

		/* Disable this interrupt. */
		mask &= ~IM_TX_EMPTY_INT;

		SMC_SELECT_BANK(sc, 0);
		card_stats = bus_space_read_2(bst, bsh, COUNTER_REG_W);

		/* Single collisions. */
		ifp->if_collisions += card_stats & ECR_COLN_MASK;

		/* Multiple collisions. */
		ifp->if_collisions += (card_stats & ECR_MCOLN_MASK) >> 4;

		SMC_SELECT_BANK(sc, 2);

		ifp->if_timer = 0;
	}

	/*
	 * Other errors.  Reset the interface.
	 */
	if (status & IM_EPH_INT) {
		smc91cxx_stop(sc);
		smc91cxx_init(sc);
	}

	/*
	 * Attempt to queue more packets for transmission.
	 */
	smc91cxx_start(ifp);

out:
	/*
	 * Reenable the interrupts we wish to receive now that processing
	 * is complete.
	 */
	mask |= bus_space_read_1(bst, bsh, INTR_MASK_REG_B);
	bus_space_write_1(bst, bsh, INTR_MASK_REG_B, mask);

	return (1);
}

/*
 * Read a packet from the card and pass it up to the kernel.
 * NOTE!  WE EXPECT TO BE IN REGISTER WINDOW 2!
 */
void
smc91cxx_read(struct smc91cxx_softc *sc)
{
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	bus_space_tag_t bst = sc->sc_bst;
	bus_space_handle_t bsh = sc->sc_bsh;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *m;
	u_int16_t status, packetno, packetlen;
	u_int8_t *data;

 again:
	/*
	 * Set data pointer to the beginning of the packet.  Since
	 * PTR_RCV is set, the packet number will be found automatically
	 * in FIFO_PORTS_REG_W, FIFO_RX_MASK.
	 */
	bus_space_write_2(bst, bsh, POINTER_REG_W,
	    PTR_READ | PTR_RCV | PTR_AUTOINC /* | 0x0000 */);

	/*
	 * First two words are status and packet length.
	 */
	status = bus_space_read_2(bst, bsh, DATA_REG_W);
	packetlen = bus_space_read_2(bst, bsh, DATA_REG_W);

	/*
	 * The packet length includes 3 extra words: status, length,
	 * and an extra word that includes the control byte.
	 */
	packetlen -= 6;

	/*
	 * Account for receive errors and discard.
	 */
	if (status & RS_ERRORS) {
		ifp->if_ierrors++;
		goto out;
	}

	/*
	 * Adjust for odd-length packet.
	 */
	if (status & RS_ODDFRAME)
		packetlen++;

	/*
	 * Allocate a header mbuf.
	 */
	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL)
		goto out;
	m->m_pkthdr.len = packetlen;

	/*
	 * Always put the packet in a cluster.
	 * XXX should chain small mbufs if less than threshold.
	 */
	MCLGET(m, M_DONTWAIT);
	if ((m->m_flags & M_EXT) == 0) {
		m_freem(m);
		ifp->if_ierrors++;
		printf("%s: can't allocate cluster for incoming packet\n",
		    sc->sc_dev.dv_xname);
		goto out;
	}

	/*
	 * Pull the packet off the interface.  Make sure the payload
	 * is aligned.
	 */
	m->m_data = (caddr_t) ALIGN(mtod(m, caddr_t) +
	    sizeof(struct ether_header)) - sizeof(struct ether_header);

	data = mtod(m, u_int8_t *);
	if (packetlen > 1)
		bus_space_read_multi_stream_2(bst, bsh, DATA_REG_W,
		    (u_int16_t *)data, packetlen >> 1);
	if (packetlen & 1) {
		data += packetlen & ~1;
		*data = bus_space_read_1(bst, bsh, DATA_REG_B);
	}

	m->m_pkthdr.len = m->m_len = packetlen;
	ml_enqueue(&ml, m);

 out:
	/*
	 * Tell the card to free the memory occupied by this packet.
	 */
	while (bus_space_read_2(bst, bsh, MMU_CMD_REG_W) & MMUCR_BUSY)
		/* XXX bound this loop! */ ;
	bus_space_write_2(bst, bsh, MMU_CMD_REG_W, MMUCR_RELEASE);

	/*
	 * Check for another packet.
	 */
	packetno = bus_space_read_2(bst, bsh, FIFO_PORTS_REG_W);
	if (packetno & FIFO_REMPTY) {
		if_input(ifp, &ml);
		return;
	}
	goto again;
}

/*
 * Process an ioctl request.
 */
int
smc91cxx_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct smc91cxx_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		if ((error = smc91cxx_enable(sc)) != 0)
			break;
		ifp->if_flags |= IFF_UP;
		smc91cxx_init(sc);
		break;

	case SIOCSIFFLAGS:
		if ((ifp->if_flags & IFF_UP) == 0 &&
		    (ifp->if_flags & IFF_RUNNING) != 0) {
			/*
			 * If interface is marked down and it is running,
			 * stop it.
			 */
			smc91cxx_stop(sc);
			ifp->if_flags &= ~IFF_RUNNING;
			smc91cxx_disable(sc);
		} else if ((ifp->if_flags & IFF_UP) != 0 &&
			   (ifp->if_flags & IFF_RUNNING) == 0) {
			/*
			 * If interface is marked up and it is stopped,
			 * start it.
			 */
			if ((error = smc91cxx_enable(sc)) != 0)
				break;
			smc91cxx_init(sc);
		} else if ((ifp->if_flags & IFF_UP) != 0) {
			/*
			 * Reset the interface to pick up changes in any
			 * other flags that affect hardware registers.
			 */
			smc91cxx_reset(sc);
		}
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_mii.mii_media, cmd);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_arpcom, cmd, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			smc91cxx_reset(sc);
		error = 0;
	}

	splx(s);
	return (error);
}

/*
 * Reset the interface.
 */
void
smc91cxx_reset(struct smc91cxx_softc *sc)
{
	int s;

	s = splnet();
	smc91cxx_stop(sc);
	smc91cxx_init(sc);
	splx(s);
}

/*
 * Watchdog timer.
 */
void
smc91cxx_watchdog(struct ifnet *ifp)
{
	struct smc91cxx_softc *sc = ifp->if_softc;

	log(LOG_ERR, "%s: device timeout\n", sc->sc_dev.dv_xname);
	++sc->sc_arpcom.ac_if.if_oerrors;

	smc91cxx_reset(sc);
}

/*
 * Stop output on the interface.
 */
void
smc91cxx_stop(struct smc91cxx_softc *sc)
{
	bus_space_tag_t bst = sc->sc_bst;
	bus_space_handle_t bsh = sc->sc_bsh;

	/*
	 * Clear interrupt mask; disable all interrupts.
	 */
	SMC_SELECT_BANK(sc, 2);
	bus_space_write_1(bst, bsh, INTR_MASK_REG_B, 0);

	/*
	 * Disable transmitter and receiver.
	 */
	SMC_SELECT_BANK(sc, 0);
	bus_space_write_2(bst, bsh, RECV_CONTROL_REG_W, 0);
	bus_space_write_2(bst, bsh, TXMIT_CONTROL_REG_W, 0);

	/*
	 * Cancel watchdog timer.
	 */
	sc->sc_arpcom.ac_if.if_timer = 0;
}

/*
 * Enable power on the interface.
 */
int
smc91cxx_enable(struct smc91cxx_softc *sc)
{
	if ((sc->sc_flags & SMC_FLAGS_ENABLED) == 0 && sc->sc_enable != NULL) {
		if ((*sc->sc_enable)(sc) != 0) {
			printf("%s: device enable failed\n",
			    sc->sc_dev.dv_xname);
			return (EIO);
		}
	}

	sc->sc_flags |= SMC_FLAGS_ENABLED;
	return (0);
}

/*
 * Disable power on the interface.
 */
void
smc91cxx_disable(struct smc91cxx_softc *sc)
{
	if ((sc->sc_flags & SMC_FLAGS_ENABLED) != 0 && sc->sc_disable != NULL) {
		(*sc->sc_disable)(sc);
		sc->sc_flags &= ~SMC_FLAGS_ENABLED;
	}
}

u_int32_t
smc91cxx_mii_bitbang_read(struct device *self)
{
	struct smc91cxx_softc *sc = (void *) self;

	/* We're already in bank 3. */
	return (bus_space_read_2(sc->sc_bst, sc->sc_bsh, MGMT_REG_W));
}

void
smc91cxx_mii_bitbang_write(struct device *self, u_int32_t val)
{
	struct smc91cxx_softc *sc = (void *) self;

	/* We're already in bank 3. */
	bus_space_write_2(sc->sc_bst, sc->sc_bsh, MGMT_REG_W, val);
}

int
smc91cxx_mii_readreg(struct device *self, int phy, int reg)
{
	struct smc91cxx_softc *sc = (void *) self;
	int val;

	SMC_SELECT_BANK(sc, 3);

	val = mii_bitbang_readreg(self, &smc91cxx_mii_bitbang_ops, phy, reg);

	SMC_SELECT_BANK(sc, 2);

	return (val);
}

void
smc91cxx_mii_writereg(struct device *self, int phy, int reg, int val)
{
	struct smc91cxx_softc *sc = (void *) self;

	SMC_SELECT_BANK(sc, 3);

	mii_bitbang_writereg(self, &smc91cxx_mii_bitbang_ops, phy, reg, val);

	SMC_SELECT_BANK(sc, 2);
}

void
smc91cxx_statchg(struct device *self)
{
	struct smc91cxx_softc *sc = (struct smc91cxx_softc *)self;
	bus_space_tag_t bst = sc->sc_bst;
	bus_space_handle_t bsh = sc->sc_bsh;
	int mctl;

	SMC_SELECT_BANK(sc, 0);
	mctl = bus_space_read_2(bst, bsh, TXMIT_CONTROL_REG_W);
	if (sc->sc_mii.mii_media_active & IFM_FDX)
		mctl |= TCR_SWFDUP;
	else
		mctl &= ~TCR_SWFDUP;
	bus_space_write_2(bst, bsh, TXMIT_CONTROL_REG_W, mctl);
	SMC_SELECT_BANK(sc, 2); /* back to operating window */
}

/*
 * One second timer, used to tick the MII.
 */
void
smc91cxx_tick(void *arg)
{
	struct smc91cxx_softc *sc = arg;
	int s;

#ifdef DIAGNOSTIC
	if ((sc->sc_flags & SMC_FLAGS_HAS_MII) == 0)
		panic("smc91cxx_tick");
#endif

	if ((sc->sc_dev.dv_flags & DVF_ACTIVE) == 0)
		return;

	s = splnet();
	mii_tick(&sc->sc_mii);
	splx(s);

	timeout_add_sec(&sc->sc_mii_timeout, 1);
}
