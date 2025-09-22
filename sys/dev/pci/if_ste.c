/*	$OpenBSD: if_ste.c,v 1.71 2024/05/24 06:02:57 jsg Exp $ */
/*
 * Copyright (c) 1997, 1998, 1999
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
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
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/pci/if_ste.c,v 1.14 1999/12/07 20:14:42 wpaul Exp $
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/timeout.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net/if_media.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <uvm/uvm_extern.h>              /* for vtophys */

#include <sys/device.h>

#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#define STE_USEIOSPACE

#include <dev/pci/if_stereg.h>

int	ste_probe(struct device *, void *, void *);
void	ste_attach(struct device *, struct device *, void *);
int	ste_intr(void *);
void	ste_init(void *);
void	ste_rxeoc(struct ste_softc *);
void	ste_rxeof(struct ste_softc *);
void	ste_txeoc(struct ste_softc *);
void	ste_txeof(struct ste_softc *);
void	ste_stats_update(void *);
void	ste_stop(struct ste_softc *);
void	ste_reset(struct ste_softc *);
int	ste_ioctl(struct ifnet *, u_long, caddr_t);
int	ste_encap(struct ste_softc *, struct ste_chain *,
	    struct mbuf *);
void	ste_start(struct ifnet *);
void	ste_watchdog(struct ifnet *);
int	ste_newbuf(struct ste_softc *,
	    struct ste_chain_onefrag *,
	    struct mbuf *);
int	ste_ifmedia_upd(struct ifnet *);
void	ste_ifmedia_sts(struct ifnet *, struct ifmediareq *);

void	ste_mii_sync(struct ste_softc *);
void	ste_mii_send(struct ste_softc *, u_int32_t, int);
int	ste_mii_readreg(struct ste_softc *,
	    struct ste_mii_frame *);
int	ste_mii_writereg(struct ste_softc *,
	    struct ste_mii_frame *);
int	ste_miibus_readreg(struct device *, int, int);
void	ste_miibus_writereg(struct device *, int, int, int);
void	ste_miibus_statchg(struct device *);

int	ste_eeprom_wait(struct ste_softc *);
int	ste_read_eeprom(struct ste_softc *, caddr_t, int,
	    int, int);
void	ste_wait(struct ste_softc *);
void	ste_iff(struct ste_softc *);
int	ste_init_rx_list(struct ste_softc *);
void	ste_init_tx_list(struct ste_softc *);

#define STE_SETBIT4(sc, reg, x)				\
	CSR_WRITE_4(sc, reg, CSR_READ_4(sc, reg) | x)

#define STE_CLRBIT4(sc, reg, x)				\
	CSR_WRITE_4(sc, reg, CSR_READ_4(sc, reg) & ~x)

#define STE_SETBIT2(sc, reg, x)				\
	CSR_WRITE_2(sc, reg, CSR_READ_2(sc, reg) | x)

#define STE_CLRBIT2(sc, reg, x)				\
	CSR_WRITE_2(sc, reg, CSR_READ_2(sc, reg) & ~x)

#define STE_SETBIT1(sc, reg, x)				\
	CSR_WRITE_1(sc, reg, CSR_READ_1(sc, reg) | x)

#define STE_CLRBIT1(sc, reg, x)				\
	CSR_WRITE_1(sc, reg, CSR_READ_1(sc, reg) & ~x)


#define MII_SET(x)		STE_SETBIT1(sc, STE_PHYCTL, x)
#define MII_CLR(x)		STE_CLRBIT1(sc, STE_PHYCTL, x) 

const struct pci_matchid ste_devices[] = {
	{ PCI_VENDOR_DLINK, PCI_PRODUCT_DLINK_DFE550TX },
	{ PCI_VENDOR_SUNDANCE, PCI_PRODUCT_SUNDANCE_ST201_1 },
	{ PCI_VENDOR_SUNDANCE, PCI_PRODUCT_SUNDANCE_ST201_2 }
};

const struct cfattach ste_ca = {
	sizeof(struct ste_softc), ste_probe, ste_attach
};

struct cfdriver ste_cd = {
	NULL, "ste", DV_IFNET
};

/*
 * Sync the PHYs by setting data bit and strobing the clock 32 times.
 */
void
ste_mii_sync(struct ste_softc *sc)
{
	int		i;

	MII_SET(STE_PHYCTL_MDIR|STE_PHYCTL_MDATA);

	for (i = 0; i < 32; i++) {
		MII_SET(STE_PHYCTL_MCLK);
		DELAY(1);
		MII_CLR(STE_PHYCTL_MCLK);
		DELAY(1);
	}
}

/*
 * Clock a series of bits through the MII.
 */
void
ste_mii_send(struct ste_softc *sc, u_int32_t bits, int cnt)
{
	int		i;

	MII_CLR(STE_PHYCTL_MCLK);

	for (i = (0x1 << (cnt - 1)); i; i >>= 1) {
                if (bits & i) {
			MII_SET(STE_PHYCTL_MDATA);
                } else {
			MII_CLR(STE_PHYCTL_MDATA);
                }
		DELAY(1);
		MII_CLR(STE_PHYCTL_MCLK);
		DELAY(1);
		MII_SET(STE_PHYCTL_MCLK);
	}
}

/*
 * Read an PHY register through the MII.
 */
int
ste_mii_readreg(struct ste_softc *sc, struct ste_mii_frame *frame)
{
	int		ack, i, s;

	s = splnet();

	/*
	 * Set up frame for RX.
	 */
	frame->mii_stdelim = STE_MII_STARTDELIM;
	frame->mii_opcode = STE_MII_READOP;
	frame->mii_turnaround = 0;
	frame->mii_data = 0;
	
	CSR_WRITE_2(sc, STE_PHYCTL, 0);
	/*
 	 * Turn on data xmit.
	 */
	MII_SET(STE_PHYCTL_MDIR);

	ste_mii_sync(sc);

	/*
	 * Send command/address info.
	 */
	ste_mii_send(sc, frame->mii_stdelim, 2);
	ste_mii_send(sc, frame->mii_opcode, 2);
	ste_mii_send(sc, frame->mii_phyaddr, 5);
	ste_mii_send(sc, frame->mii_regaddr, 5);

	/* Turn off xmit. */
	MII_CLR(STE_PHYCTL_MDIR);

	/* Idle bit */
	MII_CLR((STE_PHYCTL_MCLK|STE_PHYCTL_MDATA));
	DELAY(1);
	MII_SET(STE_PHYCTL_MCLK);
	DELAY(1);

	/* Check for ack */
	MII_CLR(STE_PHYCTL_MCLK);
	DELAY(1);
	ack = CSR_READ_2(sc, STE_PHYCTL) & STE_PHYCTL_MDATA;
	MII_SET(STE_PHYCTL_MCLK);
	DELAY(1);

	/*
	 * Now try reading data bits. If the ack failed, we still
	 * need to clock through 16 cycles to keep the PHY(s) in sync.
	 */
	if (ack) {
		for(i = 0; i < 16; i++) {
			MII_CLR(STE_PHYCTL_MCLK);
			DELAY(1);
			MII_SET(STE_PHYCTL_MCLK);
			DELAY(1);
		}
		goto fail;
	}

	for (i = 0x8000; i; i >>= 1) {
		MII_CLR(STE_PHYCTL_MCLK);
		DELAY(1);
		if (!ack) {
			if (CSR_READ_2(sc, STE_PHYCTL) & STE_PHYCTL_MDATA)
				frame->mii_data |= i;
			DELAY(1);
		}
		MII_SET(STE_PHYCTL_MCLK);
		DELAY(1);
	}

fail:

	MII_CLR(STE_PHYCTL_MCLK);
	DELAY(1);
	MII_SET(STE_PHYCTL_MCLK);
	DELAY(1);

	splx(s);

	if (ack)
		return(1);
	return(0);
}

/*
 * Write to a PHY register through the MII.
 */
int
ste_mii_writereg(struct ste_softc *sc, struct ste_mii_frame *frame)
{
	int		s;

	s = splnet();
	/*
	 * Set up frame for TX.
	 */

	frame->mii_stdelim = STE_MII_STARTDELIM;
	frame->mii_opcode = STE_MII_WRITEOP;
	frame->mii_turnaround = STE_MII_TURNAROUND;
	
	/*
 	 * Turn on data output.
	 */
	MII_SET(STE_PHYCTL_MDIR);

	ste_mii_sync(sc);

	ste_mii_send(sc, frame->mii_stdelim, 2);
	ste_mii_send(sc, frame->mii_opcode, 2);
	ste_mii_send(sc, frame->mii_phyaddr, 5);
	ste_mii_send(sc, frame->mii_regaddr, 5);
	ste_mii_send(sc, frame->mii_turnaround, 2);
	ste_mii_send(sc, frame->mii_data, 16);

	/* Idle bit. */
	MII_SET(STE_PHYCTL_MCLK);
	DELAY(1);
	MII_CLR(STE_PHYCTL_MCLK);
	DELAY(1);

	/*
	 * Turn off xmit.
	 */
	MII_CLR(STE_PHYCTL_MDIR);

	splx(s);

	return(0);
}

int
ste_miibus_readreg(struct device *self, int phy, int reg)
{
	struct ste_softc	*sc = (struct ste_softc *)self;
	struct ste_mii_frame	frame;

	if (sc->ste_one_phy && phy != 0)
		return (0);

	bzero(&frame, sizeof(frame));

	frame.mii_phyaddr = phy;
	frame.mii_regaddr = reg;
	ste_mii_readreg(sc, &frame);

	return(frame.mii_data);
}

void
ste_miibus_writereg(struct device *self, int phy, int reg, int data)
{
	struct ste_softc	*sc = (struct ste_softc *)self;
	struct ste_mii_frame	frame;

	bzero(&frame, sizeof(frame));

	frame.mii_phyaddr = phy;
	frame.mii_regaddr = reg;
	frame.mii_data = data;

	ste_mii_writereg(sc, &frame);
}

void
ste_miibus_statchg(struct device *self)
{
	struct ste_softc	*sc = (struct ste_softc *)self;
	struct mii_data		*mii;
	int fdx, fcur;

	mii = &sc->sc_mii;

	fcur = CSR_READ_2(sc, STE_MACCTL0) & STE_MACCTL0_FULLDUPLEX;
	fdx = (mii->mii_media_active & IFM_GMASK) == IFM_FDX;

	if ((fcur && fdx) || (! fcur && ! fdx))
		return;

	STE_SETBIT4(sc, STE_DMACTL,
	    STE_DMACTL_RXDMA_STALL |STE_DMACTL_TXDMA_STALL);
	ste_wait(sc);

	if (fdx)
		STE_SETBIT2(sc, STE_MACCTL0, STE_MACCTL0_FULLDUPLEX);
	else
		STE_CLRBIT2(sc, STE_MACCTL0, STE_MACCTL0_FULLDUPLEX);

	STE_SETBIT4(sc, STE_DMACTL,
	    STE_DMACTL_RXDMA_UNSTALL | STE_DMACTL_TXDMA_UNSTALL);
}
 
int
ste_ifmedia_upd(struct ifnet *ifp)
{
	struct ste_softc	*sc;
	struct mii_data		*mii;

	sc = ifp->if_softc;
	mii = &sc->sc_mii;
	sc->ste_link = 0;
	if (mii->mii_instance) {
		struct mii_softc	*miisc;
		LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
			mii_phy_reset(miisc);
	}
	mii_mediachg(mii);

	return(0);
}

void
ste_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct ste_softc	*sc;
	struct mii_data		*mii;

	sc = ifp->if_softc;
	mii = &sc->sc_mii;

	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

void
ste_wait(struct ste_softc *sc)
{
	int		i;

	for (i = 0; i < STE_TIMEOUT; i++) {
		if (!(CSR_READ_4(sc, STE_DMACTL) & STE_DMACTL_DMA_HALTINPROG))
			break;
	}

	if (i == STE_TIMEOUT)
		printf("%s: command never completed!\n", sc->sc_dev.dv_xname);
}

/*
 * The EEPROM is slow: give it time to come ready after issuing
 * it a command.
 */
int
ste_eeprom_wait(struct ste_softc *sc)
{
	int		i;

	DELAY(1000);

	for (i = 0; i < 100; i++) {
		if (CSR_READ_2(sc, STE_EEPROM_CTL) & STE_EECTL_BUSY)
			DELAY(1000);
		else
			break;
	}

	if (i == 100) {
		printf("%s: eeprom failed to come ready\n",
		    sc->sc_dev.dv_xname);
		return(1);
	}

	return(0);
}

/*
 * Read a sequence of words from the EEPROM. Note that ethernet address
 * data is stored in the EEPROM in network byte order.
 */
int
ste_read_eeprom(struct ste_softc *sc, caddr_t dest, int off, int cnt, int swap)
{
	int			err = 0, i;
	u_int16_t		word = 0, *ptr;

	if (ste_eeprom_wait(sc))
		return(1);

	for (i = 0; i < cnt; i++) {
		CSR_WRITE_2(sc, STE_EEPROM_CTL, STE_EEOPCODE_READ | (off + i));
		err = ste_eeprom_wait(sc);
		if (err)
			break;
		word = CSR_READ_2(sc, STE_EEPROM_DATA);
		ptr = (u_int16_t *)(dest + (i * 2));
		if (swap)
			*ptr = ntohs(word);
		else
			*ptr = word;	
	}

	return(err ? 1 : 0);
}

void
ste_iff(struct ste_softc *sc)
{
	struct ifnet		*ifp = &sc->arpcom.ac_if;
	struct arpcom		*ac = &sc->arpcom;
	struct ether_multi	*enm;
	struct ether_multistep	step;
	u_int32_t		rxmode, hashes[2];
	int			h = 0;

	rxmode = CSR_READ_1(sc, STE_RX_MODE);
	rxmode &= ~(STE_RXMODE_ALLMULTI | STE_RXMODE_BROADCAST |
	    STE_RXMODE_MULTIHASH | STE_RXMODE_PROMISC |
	    STE_RXMODE_UNICAST);
	bzero(hashes, sizeof(hashes));
	ifp->if_flags &= ~IFF_ALLMULTI;

	/*
	 * Always accept broadcast frames.
	 * Always accept frames destined to our station address.
	 */
	rxmode |= STE_RXMODE_BROADCAST | STE_RXMODE_UNICAST;

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		rxmode |= STE_RXMODE_ALLMULTI;
		if (ifp->if_flags & IFF_PROMISC)
			rxmode |= STE_RXMODE_PROMISC;
	} else {
		rxmode |= STE_RXMODE_MULTIHASH;

		/* now program new ones */
		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			h = ether_crc32_be(enm->enm_addrlo,
			    ETHER_ADDR_LEN) & 0x3F;

			if (h < 32)
				hashes[0] |= (1 << h);
			else
				hashes[1] |= (1 << (h - 32));

			ETHER_NEXT_MULTI(step, enm);
		}
	}

	CSR_WRITE_2(sc, STE_MAR0, hashes[0] & 0xFFFF);
	CSR_WRITE_2(sc, STE_MAR1, (hashes[0] >> 16) & 0xFFFF);
	CSR_WRITE_2(sc, STE_MAR2, hashes[1] & 0xFFFF);
	CSR_WRITE_2(sc, STE_MAR3, (hashes[1] >> 16) & 0xFFFF);
	CSR_WRITE_1(sc, STE_RX_MODE, rxmode);
}

int
ste_intr(void *xsc)
{
	struct ste_softc	*sc;
	struct ifnet		*ifp;
	u_int16_t		status;
	int			claimed = 0;

	sc = xsc;
	ifp = &sc->arpcom.ac_if;

	/* See if this is really our interrupt. */
	if (!(CSR_READ_2(sc, STE_ISR) & STE_ISR_INTLATCH))
		return claimed;

	for (;;) {
		status = CSR_READ_2(sc, STE_ISR_ACK);

		if (!(status & STE_INTRS))
			break;

		claimed = 1;

		if (status & STE_ISR_RX_DMADONE) {
			ste_rxeoc(sc);
			ste_rxeof(sc);
		}

		if (status & STE_ISR_TX_DMADONE)
			ste_txeof(sc);

		if (status & STE_ISR_TX_DONE)
			ste_txeoc(sc);

		if (status & STE_ISR_STATS_OFLOW) {
			timeout_del(&sc->sc_stats_tmo);
			ste_stats_update(sc);
		}

		if (status & STE_ISR_LINKEVENT)
			mii_pollstat(&sc->sc_mii);

		if (status & STE_ISR_HOSTERR)
			ste_init(sc);
	}

	/* Re-enable interrupts */
	CSR_WRITE_2(sc, STE_IMR, STE_INTRS);

	if (ifp->if_flags & IFF_RUNNING && !ifq_empty(&ifp->if_snd))
		ste_start(ifp);

	return claimed;
}

void
ste_rxeoc(struct ste_softc *sc)
{
	struct ste_chain_onefrag *cur_rx;

	if (sc->ste_cdata.ste_rx_head->ste_ptr->ste_status == 0) {
		cur_rx = sc->ste_cdata.ste_rx_head;
		do {
			cur_rx = cur_rx->ste_next;
			/* If the ring is empty, just return. */
			if (cur_rx == sc->ste_cdata.ste_rx_head)
				return;
		} while (cur_rx->ste_ptr->ste_status == 0);
		if (sc->ste_cdata.ste_rx_head->ste_ptr->ste_status == 0) {
			/* We've fallen behind the chip: catch it. */
			sc->ste_cdata.ste_rx_head = cur_rx;
		}
	}
}

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 */
void
ste_rxeof(struct ste_softc *sc)
{
        struct mbuf		*m;
	struct mbuf_list	ml = MBUF_LIST_INITIALIZER();
        struct ifnet		*ifp;
	struct ste_chain_onefrag	*cur_rx;
	int			total_len = 0, count=0;
	u_int32_t		rxstat;

	ifp = &sc->arpcom.ac_if;

	while((rxstat = sc->ste_cdata.ste_rx_head->ste_ptr->ste_status)
	      & STE_RXSTAT_DMADONE) {
		if ((STE_RX_LIST_CNT - count) < 3)
			break;

		cur_rx = sc->ste_cdata.ste_rx_head;
		sc->ste_cdata.ste_rx_head = cur_rx->ste_next;

		/*
		 * If an error occurs, update stats, clear the
		 * status word and leave the mbuf cluster in place:
		 * it should simply get re-used next time this descriptor
	 	 * comes up in the ring.
		 */
		if (rxstat & STE_RXSTAT_FRAME_ERR) {
			ifp->if_ierrors++;
			cur_rx->ste_ptr->ste_status = 0;
			continue;
		}

		/*
		 * If there error bit was not set, the upload complete
		 * bit should be set which means we have a valid packet.
		 * If not, something truly strange has happened.
		 */
		if (!(rxstat & STE_RXSTAT_DMADONE)) {
			printf("%s: bad receive status -- packet dropped",
				sc->sc_dev.dv_xname);
			ifp->if_ierrors++;
			cur_rx->ste_ptr->ste_status = 0;
			continue;
		}

		/* No errors; receive the packet. */	
		m = cur_rx->ste_mbuf;
		total_len = cur_rx->ste_ptr->ste_status & STE_RXSTAT_FRAMELEN;

		/*
		 * Try to conjure up a new mbuf cluster. If that
		 * fails, it means we have an out of memory condition and
		 * should leave the buffer in place and continue. This will
		 * result in a lost packet, but there's little else we
		 * can do in this situation.
		 */
		if (ste_newbuf(sc, cur_rx, NULL) == ENOBUFS) {
			ifp->if_ierrors++;
			cur_rx->ste_ptr->ste_status = 0;
			continue;
		}

		m->m_pkthdr.len = m->m_len = total_len;

		ml_enqueue(&ml, m);

		cur_rx->ste_ptr->ste_status = 0;
		count++;
	}

	if_input(ifp, &ml);
}

void
ste_txeoc(struct ste_softc *sc)
{
	u_int8_t		txstat;
	struct ifnet		*ifp;

	ifp = &sc->arpcom.ac_if;

	while ((txstat = CSR_READ_1(sc, STE_TX_STATUS)) &
	    STE_TXSTATUS_TXDONE) {
		if (txstat & STE_TXSTATUS_UNDERRUN ||
		    txstat & STE_TXSTATUS_EXCESSCOLLS ||
		    txstat & STE_TXSTATUS_RECLAIMERR) {
			ifp->if_oerrors++;
			printf("%s: transmission error: %x\n",
			    sc->sc_dev.dv_xname, txstat);

			ste_init(sc);

			if (txstat & STE_TXSTATUS_UNDERRUN &&
			    sc->ste_tx_thresh < ETHER_MAX_DIX_LEN) {
				sc->ste_tx_thresh += STE_MIN_FRAMELEN;
				printf("%s: tx underrun, increasing tx"
				    " start threshold to %d bytes\n",
				    sc->sc_dev.dv_xname, sc->ste_tx_thresh);
			}
			CSR_WRITE_2(sc, STE_TX_STARTTHRESH, sc->ste_tx_thresh);
			CSR_WRITE_2(sc, STE_TX_RECLAIM_THRESH,
			    (ETHER_MAX_DIX_LEN >> 4));
		}
		ste_init(sc);
		CSR_WRITE_2(sc, STE_TX_STATUS, txstat);
	}
}

void
ste_txeof(struct ste_softc *sc)
{
	struct ste_chain	*cur_tx = NULL;
	struct ifnet		*ifp;
	int			idx;

	ifp = &sc->arpcom.ac_if;

	idx = sc->ste_cdata.ste_tx_cons;
	while(idx != sc->ste_cdata.ste_tx_prod) {
		cur_tx = &sc->ste_cdata.ste_tx_chain[idx];

		if (!(cur_tx->ste_ptr->ste_ctl & STE_TXCTL_DMADONE))
			break;

		m_freem(cur_tx->ste_mbuf);
		cur_tx->ste_mbuf = NULL;
		ifq_clr_oactive(&ifp->if_snd);

		STE_INC(idx, STE_TX_LIST_CNT);
	}

	sc->ste_cdata.ste_tx_cons = idx;
	if (idx == sc->ste_cdata.ste_tx_prod)
		ifp->if_timer = 0;
}

void
ste_stats_update(void *xsc)
{
	struct ste_softc	*sc;
	struct ifnet		*ifp;
	struct mii_data		*mii;
	int			s;

	s = splnet();

	sc = xsc;
	ifp = &sc->arpcom.ac_if;
	mii = &sc->sc_mii;

	ifp->if_collisions += CSR_READ_1(sc, STE_LATE_COLLS)
	    + CSR_READ_1(sc, STE_MULTI_COLLS)
	    + CSR_READ_1(sc, STE_SINGLE_COLLS);

	if (!sc->ste_link) {
		mii_pollstat(mii);
		if (mii->mii_media_status & IFM_ACTIVE &&
		    IFM_SUBTYPE(mii->mii_media_active) != IFM_NONE) {
			sc->ste_link++;
			/*
			 * we don't get a call-back on re-init so do it
			 * otherwise we get stuck in the wrong link state
			 */
			ste_miibus_statchg((struct device *)sc);
			if (!ifq_empty(&ifp->if_snd))
				ste_start(ifp);
		}
	}

	timeout_add_sec(&sc->sc_stats_tmo, 1);
	splx(s);
}

/*
 * Probe for a Sundance ST201 chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 */
int
ste_probe(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, ste_devices,
	    nitems(ste_devices)));
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
void
ste_attach(struct device *parent, struct device *self, void *aux)
{
	const char		*intrstr = NULL;
	struct ste_softc	*sc = (struct ste_softc *)self;
	struct pci_attach_args	*pa = aux;
	pci_chipset_tag_t	pc = pa->pa_pc;
	pci_intr_handle_t	ih;
	struct ifnet		*ifp;
	bus_size_t		size;

	pci_set_powerstate(pa->pa_pc, pa->pa_tag, PCI_PMCSR_STATE_D0);

	/*
	 * Only use one PHY since this chip reports multiple
	 * Note on the DFE-550TX the PHY is at 1 on the DFE-580TX
	 * it is at 0 & 1.  It is rev 0x12.
	 */
	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_DLINK &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_DLINK_DFE550TX &&
	    PCI_REVISION(pa->pa_class) == 0x12)
		sc->ste_one_phy = 1;

	/*
	 * Map control/status registers.
	 */

#ifdef STE_USEIOSPACE
	if (pci_mapreg_map(pa, STE_PCI_LOIO,
	    PCI_MAPREG_TYPE_IO, 0,
	    &sc->ste_btag, &sc->ste_bhandle, NULL, &size, 0)) {
		printf(": can't map i/o space\n");
		return;
	}
 #else
	if (pci_mapreg_map(pa, STE_PCI_LOMEM,
	    PCI_MAPREG_TYPE_MEM|PCI_MAPREG_MEM_TYPE_32BIT, 0,
	    &sc->ste_btag, &sc->ste_bhandle, NULL, &size, 0)) {
		printf(": can't map mem space\n");
		return;
	}
#endif

	/* Allocate interrupt */
	if (pci_intr_map(pa, &ih)) {
		printf(": couldn't map interrupt\n");
		goto fail_1;
	}
	intrstr = pci_intr_string(pc, ih);
	sc->sc_ih = pci_intr_establish(pc, ih, IPL_NET, ste_intr, sc,
	    self->dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		goto fail_1;
	}
	printf(": %s", intrstr);

	/* Reset the adapter. */
	ste_reset(sc);

	/*
	 * Get station address from the EEPROM.
	 */
	if (ste_read_eeprom(sc, (caddr_t)&sc->arpcom.ac_enaddr,
	    STE_EEADDR_NODE0, 3, 0)) {
		printf(": failed to read station address\n");
		goto fail_2;
	}

	printf(", address %s\n", ether_sprintf(sc->arpcom.ac_enaddr));

	sc->ste_ldata_ptr = malloc(sizeof(struct ste_list_data) + 8,
	    M_DEVBUF, M_DONTWAIT);
	if (sc->ste_ldata_ptr == NULL) {
		printf(": no memory for list buffers!\n");
		goto fail_2;
	}

	sc->ste_ldata = (struct ste_list_data *)sc->ste_ldata_ptr;
	bzero(sc->ste_ldata, sizeof(struct ste_list_data));

	ifp = &sc->arpcom.ac_if;
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = ste_ioctl;
	ifp->if_start = ste_start;
	ifp->if_watchdog = ste_watchdog;
	ifq_init_maxlen(&ifp->if_snd, STE_TX_LIST_CNT - 1);
	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);
	ifp->if_capabilities = IFCAP_VLAN_MTU;

	sc->ste_tx_thresh = STE_TXSTART_THRESH;

	sc->sc_mii.mii_ifp = ifp;
	sc->sc_mii.mii_readreg = ste_miibus_readreg;
	sc->sc_mii.mii_writereg = ste_miibus_writereg;
	sc->sc_mii.mii_statchg = ste_miibus_statchg;
	ifmedia_init(&sc->sc_mii.mii_media, 0, ste_ifmedia_upd,ste_ifmedia_sts);
	mii_attach(self, &sc->sc_mii, 0xffffffff, MII_PHY_ANY, MII_OFFSET_ANY,
	    0);
	if (LIST_FIRST(&sc->sc_mii.mii_phys) == NULL) {
		ifmedia_add(&sc->sc_mii.mii_media, IFM_ETHER|IFM_NONE, 0, NULL);
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_NONE);
	} else
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_AUTO);

	/*
	 * Call MI attach routines.
	 */
	if_attach(ifp);
	ether_ifattach(ifp);
	return;

fail_2:
	pci_intr_disestablish(pc, sc->sc_ih);

fail_1:
	bus_space_unmap(sc->ste_btag, sc->ste_bhandle, size);
}

int
ste_newbuf(struct ste_softc *sc, struct ste_chain_onefrag *c, struct mbuf *m)
{
	struct mbuf		*m_new = NULL;

	if (m == NULL) {
		MGETHDR(m_new, M_DONTWAIT, MT_DATA);
		if (m_new == NULL)
			return(ENOBUFS);
		MCLGET(m_new, M_DONTWAIT);
		if (!(m_new->m_flags & M_EXT)) {
			m_freem(m_new);
			return(ENOBUFS);
		}
		m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;
	} else {
		m_new = m;
		m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;
		m_new->m_data = m_new->m_ext.ext_buf;
	}

	m_adj(m_new, ETHER_ALIGN);

	c->ste_mbuf = m_new;
	c->ste_ptr->ste_status = 0;
	c->ste_ptr->ste_frag.ste_addr = vtophys(mtod(m_new, vaddr_t));
	c->ste_ptr->ste_frag.ste_len = (ETHER_MAX_DIX_LEN + ETHER_VLAN_ENCAP_LEN) | STE_FRAG_LAST;

	return(0);
}

int
ste_init_rx_list(struct ste_softc *sc)
{
	struct ste_chain_data	*cd;
	struct ste_list_data	*ld;
	int			i;

	cd = &sc->ste_cdata;
	ld = sc->ste_ldata;

	for (i = 0; i < STE_RX_LIST_CNT; i++) {
		cd->ste_rx_chain[i].ste_ptr = &ld->ste_rx_list[i];
		if (ste_newbuf(sc, &cd->ste_rx_chain[i], NULL) == ENOBUFS)
			return(ENOBUFS);
		if (i == (STE_RX_LIST_CNT - 1)) {
			cd->ste_rx_chain[i].ste_next =
			    &cd->ste_rx_chain[0];
			ld->ste_rx_list[i].ste_next =
			    vtophys((vaddr_t)&ld->ste_rx_list[0]);
		} else {
			cd->ste_rx_chain[i].ste_next =
			    &cd->ste_rx_chain[i + 1];
			ld->ste_rx_list[i].ste_next =
			    vtophys((vaddr_t)&ld->ste_rx_list[i + 1]);
		}
		ld->ste_rx_list[i].ste_status = 0;
	}

	cd->ste_rx_head = &cd->ste_rx_chain[0];

	return(0);
}

void
ste_init_tx_list(struct ste_softc *sc)
{
	struct ste_chain_data	*cd;
	struct ste_list_data	*ld;
	int			i;

	cd = &sc->ste_cdata;
	ld = sc->ste_ldata;
	for (i = 0; i < STE_TX_LIST_CNT; i++) {
		cd->ste_tx_chain[i].ste_ptr = &ld->ste_tx_list[i];
		cd->ste_tx_chain[i].ste_phys = vtophys((vaddr_t)&ld->ste_tx_list[i]);
		if (i == (STE_TX_LIST_CNT - 1))
			cd->ste_tx_chain[i].ste_next =
			    &cd->ste_tx_chain[0];
		else
			cd->ste_tx_chain[i].ste_next =
			    &cd->ste_tx_chain[i + 1];
	}

	bzero(ld->ste_tx_list, sizeof(struct ste_desc) * STE_TX_LIST_CNT);

	cd->ste_tx_prod = 0;
	cd->ste_tx_cons = 0;
}

void
ste_init(void *xsc)
{
	struct ste_softc	*sc = (struct ste_softc *)xsc;
	struct ifnet		*ifp = &sc->arpcom.ac_if;
	struct mii_data		*mii;
	int			i, s;

	s = splnet();

	ste_stop(sc);
	/* Reset the chip to a known state. */
	ste_reset(sc);

	mii = &sc->sc_mii;

	/* Init our MAC address */
	for (i = 0; i < ETHER_ADDR_LEN; i++) {
		CSR_WRITE_1(sc, STE_PAR0 + i, sc->arpcom.ac_enaddr[i]);
	}

	/* Init RX list */
	if (ste_init_rx_list(sc) == ENOBUFS) {
		printf("%s: initialization failed: no "
		    "memory for RX buffers\n", sc->sc_dev.dv_xname);
		ste_stop(sc);
		splx(s);
		return;
	}

	/* Set RX polling interval */
	CSR_WRITE_1(sc, STE_RX_DMAPOLL_PERIOD, 64);

	/* Init TX descriptors */
	ste_init_tx_list(sc);

	/* Set the TX freethresh value */
	CSR_WRITE_1(sc, STE_TX_DMABURST_THRESH, ETHER_MAX_DIX_LEN >> 8);

	/* Set the TX start threshold for best performance. */
	CSR_WRITE_2(sc, STE_TX_STARTTHRESH, sc->ste_tx_thresh);

	/* Set the TX reclaim threshold. */
	CSR_WRITE_1(sc, STE_TX_RECLAIM_THRESH, (ETHER_MAX_DIX_LEN >> 4));

	/* Program promiscuous mode and multicast filters. */
	ste_iff(sc);

	/* Load the address of the RX list. */
	STE_SETBIT4(sc, STE_DMACTL, STE_DMACTL_RXDMA_STALL);
	ste_wait(sc);
	CSR_WRITE_4(sc, STE_RX_DMALIST_PTR,
	    vtophys((vaddr_t)&sc->ste_ldata->ste_rx_list[0]));
	STE_SETBIT4(sc, STE_DMACTL, STE_DMACTL_RXDMA_UNSTALL);
	STE_SETBIT4(sc, STE_DMACTL, STE_DMACTL_RXDMA_UNSTALL);

	/* Set TX polling interval (defer until we TX first packet) */
	CSR_WRITE_1(sc, STE_TX_DMAPOLL_PERIOD, 0);

	/* Load address of the TX list */
	STE_SETBIT4(sc, STE_DMACTL, STE_DMACTL_TXDMA_STALL);
	ste_wait(sc);
	CSR_WRITE_4(sc, STE_TX_DMALIST_PTR, 0);
	STE_SETBIT4(sc, STE_DMACTL, STE_DMACTL_TXDMA_UNSTALL);
	STE_SETBIT4(sc, STE_DMACTL, STE_DMACTL_TXDMA_UNSTALL);
	ste_wait(sc);
	sc->ste_tx_prev=NULL;

	/* Enable receiver and transmitter */
	CSR_WRITE_2(sc, STE_MACCTL0, 0);
	CSR_WRITE_2(sc, STE_MACCTL1, 0);
	STE_SETBIT2(sc, STE_MACCTL1, STE_MACCTL1_TX_ENABLE);
	STE_SETBIT2(sc, STE_MACCTL1, STE_MACCTL1_RX_ENABLE);

	/* Enable stats counters. */
	STE_SETBIT2(sc, STE_MACCTL1, STE_MACCTL1_STATS_ENABLE);

	/* Enable interrupts. */
	CSR_WRITE_2(sc, STE_ISR, 0xFFFF);
	CSR_WRITE_2(sc, STE_IMR, STE_INTRS);

	/* Accept VLAN length packets */
	CSR_WRITE_2(sc, STE_MAX_FRAMELEN,
	    ETHER_MAX_LEN + ETHER_VLAN_ENCAP_LEN);

	ste_ifmedia_upd(ifp);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	splx(s);

	timeout_set(&sc->sc_stats_tmo, ste_stats_update, sc);
	timeout_add_sec(&sc->sc_stats_tmo, 1);
}

void
ste_stop(struct ste_softc *sc)
{
	int			i;
	struct ifnet		*ifp;

	ifp = &sc->arpcom.ac_if;

	timeout_del(&sc->sc_stats_tmo);

	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	CSR_WRITE_2(sc, STE_IMR, 0);
	STE_SETBIT2(sc, STE_MACCTL1, STE_MACCTL1_TX_DISABLE);
	STE_SETBIT2(sc, STE_MACCTL1, STE_MACCTL1_RX_DISABLE);
	STE_SETBIT2(sc, STE_MACCTL1, STE_MACCTL1_STATS_DISABLE);
	STE_SETBIT2(sc, STE_DMACTL, STE_DMACTL_TXDMA_STALL);
	STE_SETBIT2(sc, STE_DMACTL, STE_DMACTL_RXDMA_STALL);
	ste_wait(sc);
	/* 
	 * Try really hard to stop the RX engine or under heavy RX 
	 * data chip will write into de-allocated memory.
	 */
	ste_reset(sc);

	sc->ste_link = 0;

	for (i = 0; i < STE_RX_LIST_CNT; i++) {
		if (sc->ste_cdata.ste_rx_chain[i].ste_mbuf != NULL) {
			m_freem(sc->ste_cdata.ste_rx_chain[i].ste_mbuf);
			sc->ste_cdata.ste_rx_chain[i].ste_mbuf = NULL;
		}
	}

	for (i = 0; i < STE_TX_LIST_CNT; i++) {
		if (sc->ste_cdata.ste_tx_chain[i].ste_mbuf != NULL) {
			m_freem(sc->ste_cdata.ste_tx_chain[i].ste_mbuf);
			sc->ste_cdata.ste_tx_chain[i].ste_mbuf = NULL;
		}
	}

	bzero(sc->ste_ldata, sizeof(struct ste_list_data));
}

void
ste_reset(struct ste_softc *sc)
{
	int		i;

	STE_SETBIT4(sc, STE_ASICCTL,
	    STE_ASICCTL_GLOBAL_RESET|STE_ASICCTL_RX_RESET|
	    STE_ASICCTL_TX_RESET|STE_ASICCTL_DMA_RESET|
	    STE_ASICCTL_FIFO_RESET|STE_ASICCTL_NETWORK_RESET|
	    STE_ASICCTL_AUTOINIT_RESET|STE_ASICCTL_HOST_RESET|
	    STE_ASICCTL_EXTRESET_RESET);

	DELAY(100000);

	for (i = 0; i < STE_TIMEOUT; i++) {
		if (!(CSR_READ_4(sc, STE_ASICCTL) & STE_ASICCTL_RESET_BUSY))
			break;
	}

	if (i == STE_TIMEOUT)
		printf("%s: global reset never completed\n",
		    sc->sc_dev.dv_xname);
}

int
ste_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct ste_softc	*sc = ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *) data;
	int			s, error = 0;

	s = splnet();

	switch(command) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			ste_init(sc);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else {
				sc->ste_tx_thresh = STE_TXSTART_THRESH;
				ste_init(sc);
			}
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				ste_stop(sc);
		}
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_mii.mii_media, command);
		break;

	default:
		error = ether_ioctl(ifp, &sc->arpcom, command, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			ste_iff(sc);
		error = 0;
	}

	splx(s);
	return(error);
}

int
ste_encap(struct ste_softc *sc, struct ste_chain *c, struct mbuf *m_head)
{
	int			frag = 0;
	struct ste_frag		*f = NULL;
	struct mbuf		*m;
	struct ste_desc		*d;

	d = c->ste_ptr;
	d->ste_ctl = 0;

encap_retry:
	for (m = m_head, frag = 0; m != NULL; m = m->m_next) {
		if (m->m_len != 0) {
			if (frag == STE_MAXFRAGS)
				break;
			f = &d->ste_frags[frag];
			f->ste_addr = vtophys(mtod(m, vaddr_t));
			f->ste_len = m->m_len;
			frag++;
		}
	}

	if (m != NULL) {
		struct mbuf *mn;
  	 
		/*
		 * We ran out of segments. We have to recopy this
		 * mbuf chain first. Bail out if we can't get the
		 * new buffers.
		 */
		MGETHDR(mn, M_DONTWAIT, MT_DATA);
		if (mn == NULL) {
			m_freem(m_head);
			return ENOMEM;
		}
		if (m_head->m_pkthdr.len > MHLEN) {
			MCLGET(mn, M_DONTWAIT);
			if ((mn->m_flags & M_EXT) == 0) {
				m_freem(mn);
				m_freem(m_head);
				return ENOMEM;
			}
		}
		m_copydata(m_head, 0, m_head->m_pkthdr.len,
			   mtod(mn, caddr_t));
		mn->m_pkthdr.len = mn->m_len = m_head->m_pkthdr.len;
		m_freem(m_head);
		m_head = mn;
		goto encap_retry;
	}

	c->ste_mbuf = m_head;
	d->ste_frags[frag - 1].ste_len |= STE_FRAG_LAST;
	d->ste_ctl = 1;

	return(0);
}

void
ste_start(struct ifnet *ifp)
{
	struct ste_softc	*sc;
	struct mbuf		*m_head = NULL;
	struct ste_chain	*cur_tx;
	int			idx;

	sc = ifp->if_softc;

	if (!sc->ste_link)
		return;

	if (ifq_is_oactive(&ifp->if_snd))
		return;

	idx = sc->ste_cdata.ste_tx_prod;

	while(sc->ste_cdata.ste_tx_chain[idx].ste_mbuf == NULL) {
		/*
		 * We cannot re-use the last (free) descriptor;
		 * the chip may not have read its ste_next yet.
		 */
		if (STE_NEXT(idx, STE_TX_LIST_CNT) ==
		    sc->ste_cdata.ste_tx_cons) {
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		m_head = ifq_dequeue(&ifp->if_snd);
		if (m_head == NULL)
			break;

		cur_tx = &sc->ste_cdata.ste_tx_chain[idx];

		if (ste_encap(sc, cur_tx, m_head) != 0)
			break;

		cur_tx->ste_ptr->ste_next = 0;

		if (sc->ste_tx_prev == NULL) {
			cur_tx->ste_ptr->ste_ctl = STE_TXCTL_DMAINTR | 1;
			/* Load address of the TX list */
			STE_SETBIT4(sc, STE_DMACTL, STE_DMACTL_TXDMA_STALL);
			ste_wait(sc);

			CSR_WRITE_4(sc, STE_TX_DMALIST_PTR,
			    vtophys((vaddr_t)&sc->ste_ldata->ste_tx_list[0]));

			/* Set TX polling interval to start TX engine */
			CSR_WRITE_1(sc, STE_TX_DMAPOLL_PERIOD, 64);
		  
			STE_SETBIT4(sc, STE_DMACTL, STE_DMACTL_TXDMA_UNSTALL);
			ste_wait(sc);
		}else{
			cur_tx->ste_ptr->ste_ctl = STE_TXCTL_DMAINTR | 1;
			sc->ste_tx_prev->ste_ptr->ste_next
				= cur_tx->ste_phys;
		}

		sc->ste_tx_prev = cur_tx;

#if NBPFILTER > 0
		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
	 	 */
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, cur_tx->ste_mbuf,
			    BPF_DIRECTION_OUT);
#endif

		STE_INC(idx, STE_TX_LIST_CNT);
		ifp->if_timer = 5;
	}
	sc->ste_cdata.ste_tx_prod = idx;
}

void
ste_watchdog(struct ifnet *ifp)
{
	struct ste_softc	*sc;

	sc = ifp->if_softc;

	ifp->if_oerrors++;
	printf("%s: watchdog timeout\n", sc->sc_dev.dv_xname);

	ste_txeoc(sc);
	ste_txeof(sc);
	ste_rxeoc(sc);
	ste_rxeof(sc);
	ste_init(sc);

	if (!ifq_empty(&ifp->if_snd))
		ste_start(ifp);
}
