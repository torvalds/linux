/*	$OpenBSD: rtl81x9.c,v 1.100 2024/11/05 18:58:59 miod Exp $ */

/*
 * Copyright (c) 1997, 1998
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
 */

/*
 * Realtek 8129/8139 PCI NIC driver
 *
 * Supports several extremely cheap PCI 10/100 adapters based on
 * the Realtek chipset. Datasheets can be obtained from
 * www.realtek.com.tw.
 *
 * Written by Bill Paul <wpaul@ctr.columbia.edu>
 * Electrical Engineering Department
 * Columbia University, New York City
 */

/*
 * The Realtek 8139 PCI NIC redefines the meaning of 'low end.' This is
 * probably the worst PCI ethernet controller ever made, with the possible
 * exception of the FEAST chip made by SMC. The 8139 supports bus-master
 * DMA, but it has a terrible interface that nullifies any performance
 * gains that bus-master DMA usually offers.
 *
 * For transmission, the chip offers a series of four TX descriptor
 * registers. Each transmit frame must be in a contiguous buffer, aligned
 * on a longword (32-bit) boundary. This means we almost always have to
 * do mbuf copies in order to transmit a frame, except in the unlikely
 * case where a) the packet fits into a single mbuf, and b) the packet
 * is 32-bit aligned within the mbuf's data area. The presence of only
 * four descriptor registers means that we can never have more than four
 * packets queued for transmission at any one time.
 *
 * Reception is not much better. The driver has to allocate a single large
 * buffer area (up to 64K in size) into which the chip will DMA received
 * frames. Because we don't know where within this region received packets
 * will begin or end, we have no choice but to copy data from the buffer
 * area into mbufs in order to pass the packets up to the higher protocol
 * levels.
 *
 * It's impossible given this rotten design to really achieve decent
 * performance at 100Mbps, unless you happen to have a 400MHz PII or
 * some equally overmuscled CPU to drive it.
 *
 * On the bright side, the 8139 does have a built-in PHY, although
 * rather than using an MDIO serial interface like most other NICs, the
 * PHY registers are directly accessible through the 8139's register
 * space. The 8139 supports autonegotiation, as well as a 64-bit multicast
 * filter.
 *
 * The 8129 chip is an older version of the 8139 that uses an external PHY
 * chip. The 8129 has a serial MDIO interface for accessing the MII where
 * the 8139 lets you directly access the on-board PHY registers. We need
 * to select which interface to use depending on the chip type.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/device.h>
#include <sys/timeout.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net/if_media.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <machine/bus.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/ic/rtl81x9reg.h>

/*
 * Various supported PHY vendors/types and their names. Note that
 * this driver will work with pretty much any MII-compliant PHY,
 * so failure to positively identify the chip is not a fatal error.
 */

void rl_tick(void *);

int rl_encap(struct rl_softc *, struct mbuf * );

void rl_rxeof(struct rl_softc *);
void rl_txeof(struct rl_softc *);
void rl_start(struct ifnet *);
int rl_ioctl(struct ifnet *, u_long, caddr_t);
void rl_init(void *);
void rl_stop(struct rl_softc *);
void rl_watchdog(struct ifnet *);
int rl_ifmedia_upd(struct ifnet *);
void rl_ifmedia_sts(struct ifnet *, struct ifmediareq *);

void rl_eeprom_getword(struct rl_softc *, int, int, u_int16_t *);
void rl_eeprom_putbyte(struct rl_softc *, int, int);
void rl_read_eeprom(struct rl_softc *, caddr_t, int, int, int, int);

void rl_mii_sync(struct rl_softc *);
void rl_mii_send(struct rl_softc *, u_int32_t, int);
int rl_mii_readreg(struct rl_softc *, struct rl_mii_frame *);
int rl_mii_writereg(struct rl_softc *, struct rl_mii_frame *);

int rl_miibus_readreg(struct device *, int, int);
void rl_miibus_writereg(struct device *, int, int, int);
void rl_miibus_statchg(struct device *);

void rl_iff(struct rl_softc *);
void rl_reset(struct rl_softc *);
int rl_list_tx_init(struct rl_softc *);

#define EE_SET(x)					\
	CSR_WRITE_1(sc, RL_EECMD,			\
		CSR_READ_1(sc, RL_EECMD) | x)

#define EE_CLR(x)					\
	CSR_WRITE_1(sc, RL_EECMD,			\
		CSR_READ_1(sc, RL_EECMD) & ~x)

/*
 * Send a read command and address to the EEPROM, check for ACK.
 */
void
rl_eeprom_putbyte(struct rl_softc *sc, int addr, int addr_len)
{
	int	d, i;

	d = (RL_EECMD_READ << addr_len) | addr;

	/*
	 * Feed in each bit and strobe the clock.
	 */
	for (i = RL_EECMD_LEN + addr_len; i; i--) {
		if (d & (1 << (i - 1)))
			EE_SET(RL_EE_DATAIN);
		else
			EE_CLR(RL_EE_DATAIN);

		DELAY(100);
		EE_SET(RL_EE_CLK);
		DELAY(150);
		EE_CLR(RL_EE_CLK);
		DELAY(100);
	}
}

/*
 * Read a word of data stored in the EEPROM at address 'addr.'
 */
void
rl_eeprom_getword(struct rl_softc *sc, int addr, int addr_len,
    u_int16_t *dest)
{
	int		i;
	u_int16_t	word = 0;

	/* Enter EEPROM access mode. */
	CSR_WRITE_1(sc, RL_EECMD, RL_EEMODE_PROGRAM|RL_EE_SEL);

	/*
	 * Send address of word we want to read.
	 */
	rl_eeprom_putbyte(sc, addr, addr_len);

	CSR_WRITE_1(sc, RL_EECMD, RL_EEMODE_PROGRAM|RL_EE_SEL);

	/*
	 * Start reading bits from EEPROM.
	 */
	for (i = 16; i > 0; i--) {
		EE_SET(RL_EE_CLK);
		DELAY(100);
		if (CSR_READ_1(sc, RL_EECMD) & RL_EE_DATAOUT)
			word |= 1 << (i - 1);
		EE_CLR(RL_EE_CLK);
		DELAY(100);
	}

	/* Turn off EEPROM access mode. */
	CSR_WRITE_1(sc, RL_EECMD, RL_EEMODE_OFF);

	*dest = word;
}

/*
 * Read a sequence of words from the EEPROM.
 */
void
rl_read_eeprom(struct rl_softc *sc, caddr_t dest, int off, int addr_len,
    int cnt, int swap)
{
	int		i;
	u_int16_t	word = 0, *ptr;

	for (i = 0; i < cnt; i++) {
		rl_eeprom_getword(sc, off + i, addr_len, &word);
		ptr = (u_int16_t *)(dest + (i * 2));
		if (swap)
			*ptr = letoh16(word);
		else
			*ptr = word;
	}
}

/*
 * MII access routines are provided for the 8129, which
 * doesn't have a built-in PHY. For the 8139, we fake things
 * up by diverting rl_phy_readreg()/rl_phy_writereg() to the
 * direct access PHY registers.
 */
#define MII_SET(x)					\
	CSR_WRITE_1(sc, RL_MII,				\
		CSR_READ_1(sc, RL_MII) | x)

#define MII_CLR(x)					\
	CSR_WRITE_1(sc, RL_MII,				\
		CSR_READ_1(sc, RL_MII) & ~x)

/*
 * Sync the PHYs by setting data bit and strobing the clock 32 times.
 */
void
rl_mii_sync(struct rl_softc *sc)
{
	int	i;

	MII_SET(RL_MII_DIR|RL_MII_DATAOUT);

	for (i = 0; i < 32; i++) {
		MII_SET(RL_MII_CLK);
		DELAY(1);
		MII_CLR(RL_MII_CLK);
		DELAY(1);
	}
}

/*
 * Clock a series of bits through the MII.
 */
void
rl_mii_send(struct rl_softc *sc, u_int32_t bits, int cnt)
{
	int	i;

	MII_CLR(RL_MII_CLK);

	for (i = (0x1 << (cnt - 1)); i; i >>= 1) {
		if (bits & i)
			MII_SET(RL_MII_DATAOUT);
		else
			MII_CLR(RL_MII_DATAOUT);
		DELAY(1);
		MII_CLR(RL_MII_CLK);
		DELAY(1);
		MII_SET(RL_MII_CLK);
	}
}

/*
 * Read an PHY register through the MII.
 */
int
rl_mii_readreg(struct rl_softc *sc, struct rl_mii_frame *frame)
{
	int	i, ack, s;

	s = splnet();

	/*
	 * Set up frame for RX.
	 */
	frame->mii_stdelim = RL_MII_STARTDELIM;
	frame->mii_opcode = RL_MII_READOP;
	frame->mii_turnaround = 0;
	frame->mii_data = 0;

	CSR_WRITE_2(sc, RL_MII, 0);

	/*
	 * Turn on data xmit.
	 */
	MII_SET(RL_MII_DIR);

	rl_mii_sync(sc);

	/*
	 * Send command/address info.
	 */
	rl_mii_send(sc, frame->mii_stdelim, 2);
	rl_mii_send(sc, frame->mii_opcode, 2);
	rl_mii_send(sc, frame->mii_phyaddr, 5);
	rl_mii_send(sc, frame->mii_regaddr, 5);

	/* Idle bit */
	MII_CLR((RL_MII_CLK|RL_MII_DATAOUT));
	DELAY(1);
	MII_SET(RL_MII_CLK);
	DELAY(1);

	/* Turn off xmit. */
	MII_CLR(RL_MII_DIR);

	/* Check for ack */
	MII_CLR(RL_MII_CLK);
	DELAY(1);
	ack = CSR_READ_2(sc, RL_MII) & RL_MII_DATAIN;
	MII_SET(RL_MII_CLK);
	DELAY(1);

	/*
	 * Now try reading data bits. If the ack failed, we still
	 * need to clock through 16 cycles to keep the PHY(s) in sync.
	 */
	if (ack) {
		for(i = 0; i < 16; i++) {
			MII_CLR(RL_MII_CLK);
			DELAY(1);
			MII_SET(RL_MII_CLK);
			DELAY(1);
		}
		goto fail;
	}

	for (i = 0x8000; i; i >>= 1) {
		MII_CLR(RL_MII_CLK);
		DELAY(1);
		if (!ack) {
			if (CSR_READ_2(sc, RL_MII) & RL_MII_DATAIN)
				frame->mii_data |= i;
			DELAY(1);
		}
		MII_SET(RL_MII_CLK);
		DELAY(1);
	}

fail:

	MII_CLR(RL_MII_CLK);
	DELAY(1);
	MII_SET(RL_MII_CLK);
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
rl_mii_writereg(struct rl_softc *sc, struct rl_mii_frame *frame)
{
	int	s;

	s = splnet();
	/*
	 * Set up frame for TX.
	 */

	frame->mii_stdelim = RL_MII_STARTDELIM;
	frame->mii_opcode = RL_MII_WRITEOP;
	frame->mii_turnaround = RL_MII_TURNAROUND;

	/*
	 * Turn on data output.
	 */
	MII_SET(RL_MII_DIR);

	rl_mii_sync(sc);

	rl_mii_send(sc, frame->mii_stdelim, 2);
	rl_mii_send(sc, frame->mii_opcode, 2);
	rl_mii_send(sc, frame->mii_phyaddr, 5);
	rl_mii_send(sc, frame->mii_regaddr, 5);
	rl_mii_send(sc, frame->mii_turnaround, 2);
	rl_mii_send(sc, frame->mii_data, 16);

	/* Idle bit. */
	MII_SET(RL_MII_CLK);
	DELAY(1);
	MII_CLR(RL_MII_CLK);
	DELAY(1);

	/*
	 * Turn off xmit.
	 */
	MII_CLR(RL_MII_DIR);

	splx(s);

	return(0);
}

void
rl_iff(struct rl_softc *sc)
{
	struct ifnet		*ifp = &sc->sc_arpcom.ac_if;
	int			h = 0;
	u_int32_t		hashes[2];
	struct arpcom		*ac = &sc->sc_arpcom;
	struct ether_multi	*enm;
	struct ether_multistep	step;
	u_int32_t		rxfilt;

	rxfilt = CSR_READ_4(sc, RL_RXCFG);
	rxfilt &= ~(RL_RXCFG_RX_ALLPHYS | RL_RXCFG_RX_BROAD |
	    RL_RXCFG_RX_INDIV | RL_RXCFG_RX_MULTI);
	ifp->if_flags &= ~IFF_ALLMULTI;

	/*
	 * Always accept frames destined to our station address.
	 * Always accept broadcast frames.
	 */
	rxfilt |= RL_RXCFG_RX_INDIV | RL_RXCFG_RX_BROAD;

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0) {
		ifp ->if_flags |= IFF_ALLMULTI;
		rxfilt |= RL_RXCFG_RX_MULTI;
		if (ifp->if_flags & IFF_PROMISC)
			rxfilt |= RL_RXCFG_RX_ALLPHYS;
		hashes[0] = hashes[1] = 0xFFFFFFFF;
	} else {
		rxfilt |= RL_RXCFG_RX_MULTI;
		/* Program new filter. */
		bzero(hashes, sizeof(hashes));

		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			h = ether_crc32_be(enm->enm_addrlo,
			    ETHER_ADDR_LEN) >> 26;

			if (h < 32)
				hashes[0] |= (1 << h);
			else
				hashes[1] |= (1 << (h - 32));

			ETHER_NEXT_MULTI(step, enm);
		}
	}

	CSR_WRITE_4(sc, RL_MAR0, hashes[0]);
	CSR_WRITE_4(sc, RL_MAR4, hashes[1]);
	CSR_WRITE_4(sc, RL_RXCFG, rxfilt);
}

void
rl_reset(struct rl_softc *sc)
{
	int	i;

	CSR_WRITE_1(sc, RL_COMMAND, RL_CMD_RESET);

	for (i = 0; i < RL_TIMEOUT; i++) {
		DELAY(10);
		if (!(CSR_READ_1(sc, RL_COMMAND) & RL_CMD_RESET))
			break;
	}
	if (i == RL_TIMEOUT)
		printf("%s: reset never completed!\n", sc->sc_dev.dv_xname);

}

/*
 * Initialize the transmit descriptors.
 */
int
rl_list_tx_init(struct rl_softc *sc)
{
	struct rl_chain_data	*cd = &sc->rl_cdata;
	int			i;

	for (i = 0; i < RL_TX_LIST_CNT; i++) {
		cd->rl_tx_chain[i] = NULL;
		CSR_WRITE_4(sc,
		    RL_TXADDR0 + (i * sizeof(u_int32_t)), 0x0000000);
	}

	sc->rl_cdata.cur_tx = 0;
	sc->rl_cdata.last_tx = 0;

	return(0);
}

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 *
 * You know there's something wrong with a PCI bus-master chip design
 * when you have to use m_devget().
 *
 * The receive operation is badly documented in the datasheet, so I'll
 * attempt to document it here. The driver provides a buffer area and
 * places its base address in the RX buffer start address register.
 * The chip then begins copying frames into the RX buffer. Each frame
 * is preceded by a 32-bit RX status word which specifies the length
 * of the frame and certain other status bits. Each frame (starting with
 * the status word) is also 32-bit aligned. The frame length is in the
 * first 16 bits of the status word; the lower 15 bits correspond with
 * the 'rx status register' mentioned in the datasheet.
 *
 * Note: to make the Alpha happy, the frame payload needs to be aligned
 * on a 32-bit boundary. To achieve this, we cheat a bit by copying from
 * the ring buffer starting at an address two bytes before the actual
 * data location. We can then shave off the first two bytes using m_adj().
 * The reason we do this is because m_devget() doesn't let us specify an
 * offset into the mbuf storage space, so we have to artificially create
 * one. The ring is allocated in such a way that there are a few unused
 * bytes of space preceding it so that it will be safe for us to do the
 * 2-byte backstep even if reading from the ring at offset 0.
 */
void
rl_rxeof(struct rl_softc *sc)
{
	struct ifnet	*ifp = &sc->sc_arpcom.ac_if;
	struct mbuf	*m;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	int		total_len;
	u_int32_t	rxstat;
	caddr_t		rxbufpos;
	int		wrap = 0;
	u_int16_t	cur_rx;
	u_int16_t	limit;
	u_int16_t	rx_bytes = 0, max_bytes;

	cur_rx = (CSR_READ_2(sc, RL_CURRXADDR) + 16) % RL_RXBUFLEN;

	/* Do not try to read past this point. */
	limit = CSR_READ_2(sc, RL_CURRXBUF) % RL_RXBUFLEN;

	if (limit < cur_rx)
		max_bytes = (RL_RXBUFLEN - cur_rx) + limit;
	else
		max_bytes = limit - cur_rx;

	while ((CSR_READ_1(sc, RL_COMMAND) & RL_CMD_EMPTY_RXBUF) == 0) {
		bus_dmamap_sync(sc->sc_dmat, sc->sc_rx_dmamap,
		    0, sc->sc_rx_dmamap->dm_mapsize, BUS_DMASYNC_POSTREAD);
		rxbufpos = sc->rl_cdata.rl_rx_buf + cur_rx;
		rxstat = *(u_int32_t *)rxbufpos;

		/*
		 * Here's a totally undocumented fact for you. When the
		 * Realtek chip is in the process of copying a packet into
		 * RAM for you, the length will be 0xfff0. If you spot a
		 * packet header with this value, you need to stop. The
		 * datasheet makes absolutely no mention of this and
		 * Realtek should be shot for this.
		 */
		rxstat = htole32(rxstat);
		total_len = rxstat >> 16;
		if (total_len == RL_RXSTAT_UNFINISHED) {
			bus_dmamap_sync(sc->sc_dmat, sc->sc_rx_dmamap,
			    0, sc->sc_rx_dmamap->dm_mapsize,
			    BUS_DMASYNC_PREREAD);
			break;
		}

		if (!(rxstat & RL_RXSTAT_RXOK) ||
		    total_len < ETHER_MIN_LEN ||
		    total_len > ETHER_MAX_LEN + ETHER_VLAN_ENCAP_LEN) {
			ifp->if_ierrors++;
			rl_init(sc);
			bus_dmamap_sync(sc->sc_dmat, sc->sc_rx_dmamap,
			    0, sc->sc_rx_dmamap->dm_mapsize,
			    BUS_DMASYNC_PREREAD);
			return;
		}

		/* No errors; receive the packet. */
		rx_bytes += total_len + 4;

		/*
		 * XXX The Realtek chip includes the CRC with every
		 * received frame, and there's no way to turn this
		 * behavior off (at least, I can't find anything in
		 * the manual that explains how to do it) so we have
		 * to trim off the CRC manually.
		 */
		total_len -= ETHER_CRC_LEN;

		/*
		 * Avoid trying to read more bytes than we know
		 * the chip has prepared for us.
		 */
		if (rx_bytes > max_bytes) {
			bus_dmamap_sync(sc->sc_dmat, sc->sc_rx_dmamap,
			    0, sc->sc_rx_dmamap->dm_mapsize,
			    BUS_DMASYNC_PREREAD);
			break;
		}

		rxbufpos = sc->rl_cdata.rl_rx_buf +
			((cur_rx + sizeof(u_int32_t)) % RL_RXBUFLEN);

		if (rxbufpos == (sc->rl_cdata.rl_rx_buf + RL_RXBUFLEN))
			rxbufpos = sc->rl_cdata.rl_rx_buf;

		wrap = (sc->rl_cdata.rl_rx_buf + RL_RXBUFLEN) - rxbufpos;

		if (total_len > wrap) {
			m = m_devget(rxbufpos, wrap, ETHER_ALIGN);
			if (m != NULL) {
				m_copyback(m, wrap, total_len - wrap,
				    sc->rl_cdata.rl_rx_buf, M_NOWAIT);
				if (m->m_pkthdr.len < total_len) {
					m_freem(m);
					m = NULL;
				}
			}
			cur_rx = (total_len - wrap + ETHER_CRC_LEN);
		} else {
			m = m_devget(rxbufpos, total_len, ETHER_ALIGN);
			cur_rx += total_len + 4 + ETHER_CRC_LEN;
		}

		/*
		 * Round up to 32-bit boundary.
		 */
		cur_rx = (cur_rx + 3) & ~3;
		CSR_WRITE_2(sc, RL_CURRXADDR, cur_rx - 16);

		if (m == NULL) {
			bus_dmamap_sync(sc->sc_dmat, sc->sc_rx_dmamap,
			    0, sc->sc_rx_dmamap->dm_mapsize,
			    BUS_DMASYNC_PREREAD);
			ifp->if_ierrors++;
			continue;
		}

		ml_enqueue(&ml, m);

		bus_dmamap_sync(sc->sc_dmat, sc->sc_rx_dmamap,
		    0, sc->sc_rx_dmamap->dm_mapsize, BUS_DMASYNC_PREREAD);
	}

	if_input(ifp, &ml);
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */
void
rl_txeof(struct rl_softc *sc)
{
	struct ifnet	*ifp = &sc->sc_arpcom.ac_if;
	u_int32_t	txstat;

	/*
	 * Go through our tx list and free mbufs for those
	 * frames that have been uploaded.
	 */
	do {
		if (RL_LAST_TXMBUF(sc) == NULL)
			break;
		txstat = CSR_READ_4(sc, RL_LAST_TXSTAT(sc));
		if (!(txstat & (RL_TXSTAT_TX_OK|
		    RL_TXSTAT_TX_UNDERRUN|RL_TXSTAT_TXABRT)))
			break;

		ifp->if_collisions += (txstat & RL_TXSTAT_COLLCNT) >> 24;

		bus_dmamap_sync(sc->sc_dmat, RL_LAST_TXMAP(sc),
		    0, RL_LAST_TXMAP(sc)->dm_mapsize,
		    BUS_DMASYNC_POSTWRITE);
		bus_dmamap_unload(sc->sc_dmat, RL_LAST_TXMAP(sc));
		m_freem(RL_LAST_TXMBUF(sc));
		RL_LAST_TXMBUF(sc) = NULL;
		/*
		 * If there was a transmit underrun, bump the TX threshold.
		 * Make sure not to overflow the 63 * 32byte we can address
		 * with the 6 available bit.
		 */
		if ((txstat & RL_TXSTAT_TX_UNDERRUN) &&
		    (sc->rl_txthresh < 2016))
			sc->rl_txthresh += 32;
		if (!ISSET(txstat, RL_TXSTAT_TX_OK)) {
			int oldthresh;

			ifp->if_oerrors++;
			if ((txstat & RL_TXSTAT_TXABRT) ||
			    (txstat & RL_TXSTAT_OUTOFWIN))
				CSR_WRITE_4(sc, RL_TXCFG, RL_TXCFG_CONFIG);
			oldthresh = sc->rl_txthresh;
			/* error recovery */
			rl_init(sc);
			/* restore original threshold */
			sc->rl_txthresh = oldthresh;
			return;
		}
		RL_INC(sc->rl_cdata.last_tx);
		ifq_clr_oactive(&ifp->if_snd);
	} while (sc->rl_cdata.last_tx != sc->rl_cdata.cur_tx);

	if (RL_LAST_TXMBUF(sc) == NULL)
		ifp->if_timer = 0;
	else if (ifp->if_timer == 0)
		ifp->if_timer = 5;
}

int
rl_intr(void *arg)
{
	struct rl_softc	*sc = arg;
	struct ifnet	*ifp = &sc->sc_arpcom.ac_if;
	int		claimed = 0;
	u_int16_t	status;

	/* Disable interrupts. */
	CSR_WRITE_2(sc, RL_IMR, 0x0000);

	for (;;) {
		status = CSR_READ_2(sc, RL_ISR);
		/* If the card has gone away, the read returns 0xffff. */
		if (status == 0xffff)
			break;
		if (status != 0)
			CSR_WRITE_2(sc, RL_ISR, status);
		if ((status & RL_INTRS) == 0)
			break;
		if ((status & RL_ISR_RX_OK) || (status & RL_ISR_RX_ERR))
			rl_rxeof(sc);
		if ((status & RL_ISR_TX_OK) || (status & RL_ISR_TX_ERR))
			rl_txeof(sc);
		if (status & RL_ISR_SYSTEM_ERR)
			rl_init(sc);
		claimed = 1;
	}

	/* Re-enable interrupts. */
	CSR_WRITE_2(sc, RL_IMR, RL_INTRS);

	if (!ifq_empty(&ifp->if_snd))
		rl_start(ifp);

	return (claimed);
}

/*
 * Encapsulate an mbuf chain in a descriptor by coupling the mbuf data
 * pointers to the fragment pointers.
 */
int
rl_encap(struct rl_softc *sc, struct mbuf *m_head)
{
	struct mbuf	*m_new;

	/*
	 * The Realtek is brain damaged and wants longword-aligned
	 * TX buffers, plus we can only have one fragment buffer
	 * per packet. We have to copy pretty much all the time.
	 */
	MGETHDR(m_new, M_DONTWAIT, MT_DATA);
	if (m_new == NULL) {
		m_freem(m_head);
		return(1);
	}
	if (m_head->m_pkthdr.len > MHLEN) {
		MCLGET(m_new, M_DONTWAIT);
		if (!(m_new->m_flags & M_EXT)) {
			m_freem(m_new);
			m_freem(m_head);
			return(1);
		}
	}
	m_copydata(m_head, 0, m_head->m_pkthdr.len, mtod(m_new, caddr_t));
	m_new->m_pkthdr.len = m_new->m_len = m_head->m_pkthdr.len;

	/* Pad frames to at least 60 bytes. */
	if (m_new->m_pkthdr.len < RL_MIN_FRAMELEN) {
		/*
		 * Make security-conscious people happy: zero out the
		 * bytes in the pad area, since we don't know what
		 * this mbuf cluster buffer's previous user might
		 * have left in it.
		 */
		bzero(mtod(m_new, char *) + m_new->m_pkthdr.len,
		    RL_MIN_FRAMELEN - m_new->m_pkthdr.len);
		m_new->m_pkthdr.len +=
		    (RL_MIN_FRAMELEN - m_new->m_pkthdr.len);
		m_new->m_len = m_new->m_pkthdr.len;
	}

	if (bus_dmamap_load_mbuf(sc->sc_dmat, RL_CUR_TXMAP(sc),
	    m_new, BUS_DMA_NOWAIT) != 0) {
		m_freem(m_new);
		m_freem(m_head);
		return (1);
	}
	m_freem(m_head);

	RL_CUR_TXMBUF(sc) = m_new;
	bus_dmamap_sync(sc->sc_dmat, RL_CUR_TXMAP(sc), 0,
	    RL_CUR_TXMAP(sc)->dm_mapsize, BUS_DMASYNC_PREWRITE);
	return(0);
}

/*
 * Main transmit routine.
 */
void
rl_start(struct ifnet *ifp)
{
	struct rl_softc	*sc = ifp->if_softc;
	struct mbuf	*m_head = NULL;
	int		pkts = 0;

	while (RL_CUR_TXMBUF(sc) == NULL) {
		m_head = ifq_dequeue(&ifp->if_snd);
		if (m_head == NULL)
			break;

		/* Pack the data into the descriptor. */
		if (rl_encap(sc, m_head))
			break;
		pkts++;

#if NBPFILTER > 0
		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, RL_CUR_TXMBUF(sc),
			    BPF_DIRECTION_OUT);
#endif
		/*
		 * Transmit the frame.
		 */
		CSR_WRITE_4(sc, RL_CUR_TXADDR(sc),
		    RL_CUR_TXMAP(sc)->dm_segs[0].ds_addr);
		CSR_WRITE_4(sc, RL_CUR_TXSTAT(sc),
		    RL_TXTHRESH(sc->rl_txthresh) |
		    RL_CUR_TXMAP(sc)->dm_segs[0].ds_len);

		RL_INC(sc->rl_cdata.cur_tx);

		/*
		 * Set a timeout in case the chip goes out to lunch.
		 */
		ifp->if_timer = 5;
	}

	if (pkts == 0)
		return;

	/*
	 * We broke out of the loop because all our TX slots are
	 * full. Mark the NIC as busy until it drains some of the
	 * packets from the queue.
	 */
	if (RL_CUR_TXMBUF(sc) != NULL)
		ifq_set_oactive(&ifp->if_snd);
}

void
rl_init(void *xsc)
{
	struct rl_softc	*sc = xsc;
	struct ifnet	*ifp = &sc->sc_arpcom.ac_if;
	int		s;

	s = splnet();

	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */
	rl_stop(sc);

	/* Put controller into known state. */
	rl_reset(sc);

	/*
	 * Init our MAC address.  Even though the chipset
	 * documentation doesn't mention it, we need to enter "Config
	 * register write enable" mode to modify the ID registers.
	 */
	CSR_WRITE_1(sc, RL_EECMD, RL_EEMODE_WRITECFG);
	CSR_WRITE_RAW_4(sc, RL_IDR0,
	    (u_int8_t *)(&sc->sc_arpcom.ac_enaddr[0]));
	CSR_WRITE_RAW_4(sc, RL_IDR4,
	    (u_int8_t *)(&sc->sc_arpcom.ac_enaddr[4]));
	CSR_WRITE_1(sc, RL_EECMD, RL_EEMODE_OFF);

	/* Init the RX buffer pointer register. */
	CSR_WRITE_4(sc, RL_RXADDR, sc->rl_cdata.rl_rx_buf_pa);

	/* Init TX descriptors. */
	rl_list_tx_init(sc);

	/*
	 * Enable transmit and receive.
	 */
	CSR_WRITE_1(sc, RL_COMMAND, RL_CMD_TX_ENB|RL_CMD_RX_ENB);

	/*
	 * Set the initial TX and RX configuration.
	 */
	CSR_WRITE_4(sc, RL_TXCFG, RL_TXCFG_CONFIG);
	CSR_WRITE_4(sc, RL_RXCFG, RL_RXCFG_CONFIG);

	/*
	 * Program promiscuous mode and multicast filters.
	 */
	rl_iff(sc);

	/*
	 * Enable interrupts.
	 */
	CSR_WRITE_2(sc, RL_IMR, RL_INTRS);

	/* Set initial TX threshold */
	sc->rl_txthresh = RL_TX_THRESH_INIT;

	/* Start RX/TX process. */
	CSR_WRITE_4(sc, RL_MISSEDPKT, 0);

	/* Enable receiver and transmitter. */
	CSR_WRITE_1(sc, RL_COMMAND, RL_CMD_TX_ENB|RL_CMD_RX_ENB);

	mii_mediachg(&sc->sc_mii);

	CSR_WRITE_1(sc, sc->rl_cfg1, RL_CFG1_DRVLOAD|RL_CFG1_FULLDUPLEX);

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	splx(s);

	timeout_add_sec(&sc->sc_tick_tmo, 1);
}

/*
 * Set media options.
 */
int
rl_ifmedia_upd(struct ifnet *ifp)
{
	struct rl_softc *sc = (struct rl_softc *)ifp->if_softc;

	mii_mediachg(&sc->sc_mii);
	return (0);
}

/*
 * Report current media status.
 */
void
rl_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct rl_softc *sc = ifp->if_softc;

	mii_pollstat(&sc->sc_mii);
	ifmr->ifm_status = sc->sc_mii.mii_media_status;
	ifmr->ifm_active = sc->sc_mii.mii_media_active;
}

int
rl_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct rl_softc	*sc = ifp->if_softc;
	struct ifreq	*ifr = (struct ifreq *) data;
	int		s, error = 0;

	s = splnet();

	switch(command) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			rl_init(sc);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				rl_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				rl_stop(sc);
		}
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_mii.mii_media, command);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_arpcom, command, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			rl_iff(sc);
		error = 0;
	}

	splx(s);
	return(error);
}

void
rl_watchdog(struct ifnet *ifp)
{
	struct rl_softc	*sc = ifp->if_softc;

	printf("%s: watchdog timeout\n", sc->sc_dev.dv_xname);
	ifp->if_oerrors++;
	rl_txeof(sc);
	rl_rxeof(sc);
	rl_init(sc);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
void
rl_stop(struct rl_softc *sc)
{
	struct ifnet	*ifp = &sc->sc_arpcom.ac_if;
	int		i;

	ifp->if_timer = 0;

	timeout_del(&sc->sc_tick_tmo);

	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	CSR_WRITE_1(sc, RL_COMMAND, 0x00);
	CSR_WRITE_2(sc, RL_IMR, 0x0000);

	/*
	 * Free the TX list buffers.
	 */
	for (i = 0; i < RL_TX_LIST_CNT; i++) {
		if (sc->rl_cdata.rl_tx_chain[i] != NULL) {
			bus_dmamap_sync(sc->sc_dmat,
			    sc->rl_cdata.rl_tx_dmamap[i], 0,
			    sc->rl_cdata.rl_tx_dmamap[i]->dm_mapsize,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat,
			    sc->rl_cdata.rl_tx_dmamap[i]);
			m_freem(sc->rl_cdata.rl_tx_chain[i]);
			sc->rl_cdata.rl_tx_chain[i] = NULL;
			CSR_WRITE_4(sc, RL_TXADDR0 + (i * sizeof(u_int32_t)),
				0x00000000);
		}
	}
}

int
rl_attach(struct rl_softc *sc)
{
	struct ifnet	*ifp = &sc->sc_arpcom.ac_if;
	int		rseg, i;
	u_int16_t	rl_id;
	caddr_t		kva;
	int		addr_len;

	sc->rl_cfg0 = RL_8139_CFG0;
	sc->rl_cfg1 = RL_8139_CFG1;
	sc->rl_cfg2 = 0;
	sc->rl_cfg3 = RL_8139_CFG3;
	sc->rl_cfg4 = RL_8139_CFG4;
	sc->rl_cfg5 = RL_8139_CFG5;

	rl_reset(sc);

	/*
	 * Check EEPROM type 9346 or 9356.
	 */
	rl_read_eeprom(sc, (caddr_t)&rl_id, RL_EE_ID, RL_EEADDR_LEN1, 1, 0);
	if (rl_id == 0x8129)
		addr_len = RL_EEADDR_LEN1;
	else
		addr_len = RL_EEADDR_LEN0;

	/*
	 * Get station address.
	 */
	rl_read_eeprom(sc, (caddr_t)sc->sc_arpcom.ac_enaddr, RL_EE_EADDR,
	    addr_len, 3, 1);

	printf(", address %s\n", ether_sprintf(sc->sc_arpcom.ac_enaddr));

	if (bus_dmamem_alloc(sc->sc_dmat, RL_RXBUFLEN + 32, PAGE_SIZE, 0,
	    &sc->sc_rx_seg, 1, &rseg, BUS_DMA_NOWAIT | BUS_DMA_ZERO)) {
		printf("\n%s: can't alloc rx buffers\n", sc->sc_dev.dv_xname);
		return (1);
	}
	if (bus_dmamem_map(sc->sc_dmat, &sc->sc_rx_seg, rseg,
	    RL_RXBUFLEN + 32, &kva, BUS_DMA_NOWAIT)) {
		printf("%s: can't map dma buffers (%d bytes)\n",
		    sc->sc_dev.dv_xname, RL_RXBUFLEN + 32);
		bus_dmamem_free(sc->sc_dmat, &sc->sc_rx_seg, rseg);
		return (1);
	}
	if (bus_dmamap_create(sc->sc_dmat, RL_RXBUFLEN + 32, 1,
	    RL_RXBUFLEN + 32, 0, BUS_DMA_NOWAIT, &sc->sc_rx_dmamap)) {
		printf("%s: can't create dma map\n", sc->sc_dev.dv_xname);
		bus_dmamem_unmap(sc->sc_dmat, kva, RL_RXBUFLEN + 32);
		bus_dmamem_free(sc->sc_dmat, &sc->sc_rx_seg, rseg);
		return (1);
	}
	if (bus_dmamap_load(sc->sc_dmat, sc->sc_rx_dmamap, kva,
	    RL_RXBUFLEN + 32, NULL, BUS_DMA_NOWAIT)) {
		printf("%s: can't load dma map\n", sc->sc_dev.dv_xname);
		bus_dmamap_destroy(sc->sc_dmat, sc->sc_rx_dmamap);
		bus_dmamem_unmap(sc->sc_dmat, kva, RL_RXBUFLEN + 32);
		bus_dmamem_free(sc->sc_dmat, &sc->sc_rx_seg, rseg);
		return (1);
	}
	sc->rl_cdata.rl_rx_buf = kva;
	sc->rl_cdata.rl_rx_buf_pa = sc->sc_rx_dmamap->dm_segs[0].ds_addr;

	bus_dmamap_sync(sc->sc_dmat, sc->sc_rx_dmamap,
	    0, sc->sc_rx_dmamap->dm_mapsize, BUS_DMASYNC_PREREAD);

	for (i = 0; i < RL_TX_LIST_CNT; i++) {
		if (bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1, MCLBYTES, 0,
		    BUS_DMA_NOWAIT, &sc->rl_cdata.rl_tx_dmamap[i]) != 0) {
			printf("%s: can't create tx maps\n",
			    sc->sc_dev.dv_xname);
			/* XXX free any allocated... */
			return (1);
		}
	}

	/* Leave a few bytes before the start of the RX ring buffer. */
	sc->rl_cdata.rl_rx_buf_ptr = sc->rl_cdata.rl_rx_buf;
	sc->rl_cdata.rl_rx_buf += sizeof(u_int64_t);
	sc->rl_cdata.rl_rx_buf_pa += sizeof(u_int64_t);

	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = rl_ioctl;
	ifp->if_start = rl_start;
	ifp->if_watchdog = rl_watchdog;

	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

	timeout_set(&sc->sc_tick_tmo, rl_tick, sc);

	/*
	 * Initialize our media structures and probe the MII.
	 */
	sc->sc_mii.mii_ifp = ifp;
	sc->sc_mii.mii_readreg = rl_miibus_readreg;
	sc->sc_mii.mii_writereg = rl_miibus_writereg;
	sc->sc_mii.mii_statchg = rl_miibus_statchg;
	ifmedia_init(&sc->sc_mii.mii_media, 0, rl_ifmedia_upd, rl_ifmedia_sts);
	mii_attach(&sc->sc_dev, &sc->sc_mii, 0xffffffff, MII_PHY_ANY,
	    MII_OFFSET_ANY, 0);
	if (LIST_FIRST(&sc->sc_mii.mii_phys) == NULL) {
		ifmedia_add(&sc->sc_mii.mii_media, IFM_ETHER|IFM_NONE, 0, NULL);
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_NONE);
	} else
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_AUTO);

	/*
	 * Attach us everywhere
	 */
	if_attach(ifp);
	ether_ifattach(ifp);

	return (0);
}

int
rl_activate(struct device *self, int act)
{
	struct rl_softc	*sc = (struct rl_softc *)self;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;

	switch (act) {
	case DVACT_SUSPEND:
		if (ifp->if_flags & IFF_RUNNING)
			rl_stop(sc);
		break;
	case DVACT_RESUME:
		if (ifp->if_flags & IFF_UP)
			rl_init(sc);
		break;
	}
	return (0);
}

int
rl_miibus_readreg(struct device *self, int phy, int reg)
{
	struct rl_softc		*sc = (struct rl_softc *)self;
	struct rl_mii_frame	frame;
	u_int16_t		rl8139_reg;

	if (sc->rl_type == RL_8139) {
		/*
		* The RTL8139 PHY is mapped into PCI registers, unfortunately
		* it has no phyid, or phyaddr, so assume it is phyaddr 0.
		*/
		if (phy != 0)
			return(0);

		switch (reg) {
		case MII_BMCR:
			rl8139_reg = RL_BMCR;
			break;
		case MII_BMSR:
			rl8139_reg = RL_BMSR;
			break;
		case MII_ANAR:
			rl8139_reg = RL_ANAR;
			break;
		case MII_ANER:
			rl8139_reg = RL_ANER;
			break;
		case MII_ANLPAR:
			rl8139_reg = RL_LPAR;
			break;
		case RL_MEDIASTAT:
			return (CSR_READ_1(sc, RL_MEDIASTAT));
		case MII_PHYIDR1:
		case MII_PHYIDR2:
		default:
			return (0);
		}
		return (CSR_READ_2(sc, rl8139_reg));
	}

	bzero(&frame, sizeof(frame));

	frame.mii_phyaddr = phy;
	frame.mii_regaddr = reg;
	rl_mii_readreg(sc, &frame);

	return(frame.mii_data);
}

void
rl_miibus_writereg(struct device *self, int phy, int reg, int val)
{
	struct rl_softc		*sc = (struct rl_softc *)self;
	struct rl_mii_frame	frame;
	u_int16_t		rl8139_reg = 0;

	if (sc->rl_type == RL_8139) {
		if (phy)
			return;

		switch (reg) {
		case MII_BMCR:
			rl8139_reg = RL_BMCR;
			break;
		case MII_BMSR:
			rl8139_reg = RL_BMSR;
			break;
		case MII_ANAR:
			rl8139_reg = RL_ANAR;
			break;
		case MII_ANER:
			rl8139_reg = RL_ANER;
			break;
		case MII_ANLPAR:
			rl8139_reg = RL_LPAR;
			break;
		case MII_PHYIDR1:
		case MII_PHYIDR2:
			return;
		}
		CSR_WRITE_2(sc, rl8139_reg, val);
		return;
	}

	bzero(&frame, sizeof(frame));
	frame.mii_phyaddr = phy;
	frame.mii_regaddr = reg;
	frame.mii_data = val;
	rl_mii_writereg(sc, &frame);
}

void
rl_miibus_statchg(struct device *self)
{
}

void
rl_tick(void *v)
{
	struct rl_softc	*sc = v;
	int		s;

	s = splnet();
	mii_tick(&sc->sc_mii);
	splx(s);

	timeout_add_sec(&sc->sc_tick_tmo, 1);
}

int
rl_detach(struct rl_softc *sc)
{
	struct ifnet	*ifp = &sc->sc_arpcom.ac_if;

	/* Unhook our tick handler. */
	timeout_del(&sc->sc_tick_tmo);

	/* Detach any PHYs we might have. */
	if (LIST_FIRST(&sc->sc_mii.mii_phys) != NULL)
		mii_detach(&sc->sc_mii, MII_PHY_ANY, MII_OFFSET_ANY);

	/* Delete any remaining media. */
	ifmedia_delete_instance(&sc->sc_mii.mii_media, IFM_INST_ANY);

	ether_ifdetach(ifp);
	if_detach(ifp);

	return (0);
}

struct cfdriver rl_cd = {
	NULL, "rl", DV_IFNET
};
