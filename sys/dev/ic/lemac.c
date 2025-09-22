/* $OpenBSD: lemac.c,v 1.31 2022/02/22 01:15:01 guenther Exp $ */
/* $NetBSD: lemac.c,v 1.20 2001/06/13 10:46:02 wiz Exp $ */

/*-
 * Copyright (c) 1994, 1995, 1997 Matt Thomas <matt@3am-software.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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

/*
 * DEC EtherWORKS 3 Ethernet Controllers
 *
 * Written by Matt Thomas
 * BPF support code stolen directly from if_ec.c
 *
 *   This driver supports the LEMAC DE203/204/205 cards.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/device.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <machine/bus.h>

#include <dev/ic/lemacreg.h>
#include <dev/ic/lemacvar.h>

#include "bpfilter.h"
#if NBPFILTER > 0
#include <net/bpf.h>
#endif

int	lemac_ifioctl(struct ifnet *, u_long, caddr_t);
int	lemac_ifmedia_change(struct ifnet *const);
void	lemac_ifmedia_status(struct ifnet *const, struct ifmediareq *);
void	lemac_ifstart(struct ifnet *);
void	lemac_init(struct lemac_softc *);
void	lemac_init_adapmem(struct lemac_softc *);
struct mbuf *lemac_input(struct lemac_softc *, bus_size_t, size_t);
void	lemac_multicast_filter(struct lemac_softc *);
void	lemac_multicast_op(u_int16_t *, const u_char *, int);
int	lemac_read_eeprom(struct lemac_softc *);
int	lemac_read_macaddr(unsigned char *, const bus_space_tag_t,
    const bus_space_handle_t, const bus_size_t, int);
void	lemac_reset(struct lemac_softc *);
void	lemac_rne_intr(struct lemac_softc *);
void	lemac_rxd_intr(struct lemac_softc *, unsigned);
void	lemac_tne_intr(struct lemac_softc *);
void	lemac_txd_intr(struct lemac_softc *, unsigned);

struct cfdriver lc_cd = {
	NULL, "lc", DV_IFNET
};

static const u_int16_t lemac_allmulti_mctbl[LEMAC_MCTBL_SIZE/sizeof(u_int16_t)] = {
	0xFFFFU, 0xFFFFU, 0xFFFFU, 0xFFFFU,
	0xFFFFU, 0xFFFFU, 0xFFFFU, 0xFFFFU,
	0xFFFFU, 0xFFFFU, 0xFFFFU, 0xFFFFU,
	0xFFFFU, 0xFFFFU, 0xFFFFU, 0xFFFFU,
	0xFFFFU, 0xFFFFU, 0xFFFFU, 0xFFFFU,
	0xFFFFU, 0xFFFFU, 0xFFFFU, 0xFFFFU,
	0xFFFFU, 0xFFFFU, 0xFFFFU, 0xFFFFU,
	0xFFFFU, 0xFFFFU, 0xFFFFU, 0xFFFFU,
};

/*
 * Some tuning/monitoring variables.
 */
unsigned lemac_txmax = 16;

void
lemac_rxd_intr(struct lemac_softc *sc, unsigned cs_value)
{
	/*
	 * Handle CS_RXD (Receiver disabled) here.
	 *
	 * Check Free Memory Queue Count. If not equal to zero
	 * then just turn Receiver back on. If it is equal to
	 * zero then check to see if transmitter is disabled.
	 * Process transmit TXD loop once more.  If all else
	 * fails then do software init (0xC0 to EEPROM Init)
	 * and rebuild Free Memory Queue.
	 */

	sc->sc_cntrs.cntr_rxd_intrs++;

	/*
	 *  Re-enable Receiver.
	 */

	cs_value &= ~LEMAC_CS_RXD;
	LEMAC_OUTB(sc, LEMAC_REG_CS, cs_value);

	if (LEMAC_INB(sc, LEMAC_REG_FMC) > 0)
		return;

	if (cs_value & LEMAC_CS_TXD)
		lemac_txd_intr(sc, cs_value);

	if ((LEMAC_INB(sc, LEMAC_REG_CS) & LEMAC_CS_RXD) == 0)
		return;

	printf("%s: fatal RXD error, attempting recovery\n",
	    sc->sc_if.if_xname);

	lemac_reset(sc);
	if (sc->sc_if.if_flags & IFF_UP) {
		lemac_init(sc);
		return;
	}

	/*
	 *  Error during initialization.  Mark card as disabled.
	 */
	printf("%s: recovery failed -- board disabled\n", sc->sc_if.if_xname);
}

void
lemac_tne_intr(struct lemac_softc *sc)
{
	unsigned txcount = LEMAC_INB(sc, LEMAC_REG_TDC);

	sc->sc_cntrs.cntr_tne_intrs++;
	while (txcount-- > 0) {
		unsigned txsts = LEMAC_INB(sc, LEMAC_REG_TDQ);
		if ((txsts & (LEMAC_TDQ_LCL|LEMAC_TDQ_NCL))
		    || (txsts & LEMAC_TDQ_COL) == LEMAC_TDQ_EXCCOL) {
			if (txsts & LEMAC_TDQ_NCL)
				sc->sc_flags &= ~LEMAC_LINKUP;
			sc->sc_if.if_oerrors++;
		} else {
			sc->sc_flags |= LEMAC_LINKUP;
			if ((txsts & LEMAC_TDQ_COL) != LEMAC_TDQ_NOCOL)
				sc->sc_if.if_collisions++;
		}
	}
	ifq_clr_oactive(&sc->sc_if.if_snd);
	lemac_ifstart(&sc->sc_if);
}

void
lemac_txd_intr(struct lemac_softc *sc, unsigned cs_value)
{
	/*
	 * Read transmit status, remove transmit buffer from
	 * transmit queue and place on free memory queue,
	 * then reset transmitter.
	 * Increment appropriate counters.
	 */

	sc->sc_cntrs.cntr_txd_intrs++;
	if (sc->sc_txctl & LEMAC_TX_STP) {
		sc->sc_if.if_oerrors++;
		/* return page to free queue */
		LEMAC_OUTB(sc, LEMAC_REG_FMQ, LEMAC_INB(sc, LEMAC_REG_TDQ));
	}

	/* Turn back on transmitter if disabled */
	LEMAC_OUTB(sc, LEMAC_REG_CS, cs_value & ~LEMAC_CS_TXD);
	ifq_clr_oactive(&sc->sc_if.if_snd);
}

int
lemac_read_eeprom(struct lemac_softc *sc)
{
	int	word_off, cksum;

	u_char *ep;

	cksum = 0;
	ep = sc->sc_eeprom;
	for (word_off = 0; word_off < LEMAC_EEP_SIZE / 2; word_off++) {
		LEMAC_OUTB(sc, LEMAC_REG_PI1, word_off);
		LEMAC_OUTB(sc, LEMAC_REG_IOP, LEMAC_IOP_EEREAD);

		DELAY(LEMAC_EEP_DELAY);

		*ep = LEMAC_INB(sc, LEMAC_REG_EE1);
		cksum += *ep++;
		*ep = LEMAC_INB(sc, LEMAC_REG_EE2);
		cksum += *ep++;
	}

	/*
	 *  Set up Transmit Control Byte for use later during transmit.
	 */

	sc->sc_txctl |= LEMAC_TX_FLAGS;

	if ((sc->sc_eeprom[LEMAC_EEP_SWFLAGS] & LEMAC_EEP_SW_SQE) == 0)
		sc->sc_txctl &= ~LEMAC_TX_SQE;

	if (sc->sc_eeprom[LEMAC_EEP_SWFLAGS] & LEMAC_EEP_SW_LAB)
		sc->sc_txctl |= LEMAC_TX_LAB;

	bcopy(&sc->sc_eeprom[LEMAC_EEP_PRDNM], sc->sc_prodname,
	    LEMAC_EEP_PRDNMSZ);
	sc->sc_prodname[LEMAC_EEP_PRDNMSZ] = '\0';

	return (cksum % 256);
}

void
lemac_init_adapmem(struct lemac_softc *sc)
{
	int pg, conf;

	conf = LEMAC_INB(sc, LEMAC_REG_CNF);

	if ((sc->sc_eeprom[LEMAC_EEP_SETUP] & LEMAC_EEP_ST_DRAM) == 0) {
		sc->sc_lastpage = 63;
		conf &= ~LEMAC_CNF_DRAM;
	} else {
		sc->sc_lastpage = 127;
		conf |= LEMAC_CNF_DRAM;
	}

	LEMAC_OUTB(sc, LEMAC_REG_CNF, conf);

	for (pg = 1; pg <= sc->sc_lastpage; pg++)
		LEMAC_OUTB(sc, LEMAC_REG_FMQ, pg);
}

struct mbuf *
lemac_input(struct lemac_softc *sc, bus_size_t offset, size_t length)
{
	struct ether_header eh;
	struct mbuf *m;

	if (length - sizeof(eh) > ETHERMTU ||
	    length - sizeof(eh) < ETHERMIN)
		return NULL;
	if (LEMAC_USE_PIO_MODE(sc)) {
		LEMAC_INSB(sc, LEMAC_REG_DAT, sizeof(eh), (void *)&eh);
	} else {
		LEMAC_GETBUF16(sc, offset, sizeof(eh) / 2, (void *)&eh);
	}

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL)
		return NULL;
	if (length + 2 > MHLEN) {
		MCLGET(m, M_DONTWAIT);
		if ((m->m_flags & M_EXT) == 0) {
			m_free(m);
			return NULL;
		}
	}
	m->m_data += 2;
	bcopy((caddr_t)&eh, m->m_data, sizeof(eh));
	if (LEMAC_USE_PIO_MODE(sc)) {
		LEMAC_INSB(sc, LEMAC_REG_DAT, length - sizeof(eh),
		    mtod(m, caddr_t) + sizeof(eh));
	} else {
		LEMAC_GETBUF16(sc, offset + sizeof(eh),
		    (length - sizeof(eh)) / 2,
		    (void *)(mtod(m, caddr_t) + sizeof(eh)));
		if (length & 1)
			m->m_data[length - 1] = LEMAC_GET8(sc,
			    offset + length - 1);
	}

	m->m_pkthdr.len = m->m_len = length;
	return m;
}

void
lemac_rne_intr(struct lemac_softc *sc)
{
	struct ifnet *ifp = &sc->sc_if;
	struct mbuf_list ml = MBUF_LIST_INITIALIZER();
	struct mbuf *m;
	int rxcount;

	sc->sc_cntrs.cntr_rne_intrs++;
	rxcount = LEMAC_INB(sc, LEMAC_REG_RQC);
	while (rxcount--) {
		unsigned rxpg = LEMAC_INB(sc, LEMAC_REG_RQ);
		u_int32_t rxlen;

		if (LEMAC_USE_PIO_MODE(sc)) {
			LEMAC_OUTB(sc, LEMAC_REG_IOP, rxpg);
			LEMAC_OUTB(sc, LEMAC_REG_PI1, 0);
			LEMAC_OUTB(sc, LEMAC_REG_PI2, 0);
			LEMAC_INSB(sc, LEMAC_REG_DAT, sizeof(rxlen),
			    (void *)&rxlen);
		} else {
			LEMAC_OUTB(sc, LEMAC_REG_MPN, rxpg);
			rxlen = LEMAC_GET32(sc, 0);
		}
		if (rxlen & LEMAC_RX_OK) {
			sc->sc_flags |= LEMAC_LINKUP;
			/*
			 * Get receive length - subtract out checksum.
			 */
			rxlen = ((rxlen >> 8) & 0x7FF) - 4;
			m = lemac_input(sc, sizeof(rxlen), rxlen);
		} else
			m = NULL;

		if (m != NULL)
			ml_enqueue(&ml, m);
		else
			ifp->if_ierrors++;

		/* Return this page to Free Memory Queue */
		LEMAC_OUTB(sc, LEMAC_REG_FMQ, rxpg);
	}  /* end while (recv_count--) */

	if_input(ifp, &ml);
}

/*
 *  This is the standard method of reading the DEC Address ROMS.
 *  I don't understand it but it does work.
 */
int
lemac_read_macaddr(unsigned char *hwaddr, const bus_space_tag_t iot,
    const bus_space_handle_t ioh, const bus_size_t ioreg, int skippat)
{
	int cksum, rom_cksum;
	unsigned char addrbuf[6];
    
	if (!skippat) {
		int idx, idx2, found, octet;
		static u_char testpat[] = {
			0xFF, 0, 0x55, 0xAA, 0xFF, 0, 0x55, 0xAA
		};
		idx2 = found = 0;
    
		for (idx = 0; idx < 32; idx++) {
			octet = bus_space_read_1(iot, ioh, ioreg);
	    
			if (octet == testpat[idx2]) {
				if (++idx2 == sizeof(testpat)) {
					++found;
					break;
				}
			} else {
				idx2 = 0;
			}
		}

		if (!found)
			return (-1);
	}

	if (hwaddr == NULL)
		hwaddr = addrbuf;

	cksum = 0;
	hwaddr[0] = bus_space_read_1(iot, ioh, ioreg);
	hwaddr[1] = bus_space_read_1(iot, ioh, ioreg);

	/* hardware address can't be multicast */
	if (hwaddr[0] & 1)
		return (-1);

#if BYTE_ORDER == LITTLE_ENDIAN
	cksum = *(u_short *)&hwaddr[0];
#else
	cksum = ((u_short)hwaddr[1] << 8) | (u_short)hwaddr[0];
#endif

	hwaddr[2] = bus_space_read_1(iot, ioh, ioreg);
	hwaddr[3] = bus_space_read_1(iot, ioh, ioreg);
	cksum *= 2;
	if (cksum > 65535)
		cksum -= 65535;
#if BYTE_ORDER == LITTLE_ENDIAN
	cksum += *(u_short *)&hwaddr[2];
#else
	cksum += ((u_short)hwaddr[3] << 8) | (u_short)hwaddr[2];
#endif
	if (cksum > 65535)
		cksum -= 65535;

	hwaddr[4] = bus_space_read_1(iot, ioh, ioreg);
	hwaddr[5] = bus_space_read_1(iot, ioh, ioreg);
	cksum *= 2;
	if (cksum > 65535)
		cksum -= 65535;
#if BYTE_ORDER == LITTLE_ENDIAN
	cksum += *(u_short *)&hwaddr[4];
#else
	cksum += ((u_short)hwaddr[5] << 8) | (u_short)hwaddr[4];
#endif
	if (cksum >= 65535)
		cksum -= 65535;

	/* 00-00-00 is an illegal OUI */
	if (hwaddr[0] == 0 && hwaddr[1] == 0 && hwaddr[2] == 0)
		return (-1);

	rom_cksum = bus_space_read_1(iot, ioh, ioreg);
	rom_cksum |= bus_space_read_1(iot, ioh, ioreg) << 8;
	
	if (cksum != rom_cksum)
		return (-1);
	return (0);
}

void
lemac_multicast_op(u_int16_t *mctbl, const u_char *mca, int enable)
{
	u_int idx, bit, crc;

	crc = ether_crc32_le(mca, ETHER_ADDR_LEN);

	/*
	 * The following two lines convert the N bit index into a
	 * longword index and a longword mask.
	 */
#if LEMAC_MCTBL_BITS < 0
	crc >>= (32 + LEMAC_MCTBL_BITS);
	crc &= (1 << -LEMAC_MCTBL_BITS) - 1;
#else
	crc &= (1 << LEMAC_MCTBL_BITS) - 1;
#endif
	bit = 1 << (crc & 0x0F);
	idx = crc >> 4;

	/*
	 * Set or clear hash filter bit in our table.
	 */
	if (enable) {
		mctbl[idx] |= bit;		/* Set Bit */
	} else {
		mctbl[idx] &= ~bit;		/* Clear Bit */
	}
}

void
lemac_multicast_filter(struct lemac_softc *sc)
{
#if 0
	struct arpcom *ac = &sc->sc_ec;
	struct ether_multistep step;
	struct ether_multi *enm;
#endif

	bzero(sc->sc_mctbl, LEMAC_MCTBL_BITS / 8);

	lemac_multicast_op(sc->sc_mctbl, etherbroadcastaddr, 1);

#if 0
	if (ac->ac_multirangecnt > 0) {
		sc->sc_flags |= LEMAC_ALLMULTI;
		sc->sc_if.if_flags |= IFF_ALLMULTI;
		return;
	}

	ETHER_FIRST_MULTI(step, ac, enm);
	while (enm != NULL) {
		lemac_multicast_op(sc->sc_mctbl, enm->enm_addrlo, TRUE);
		ETHER_NEXT_MULTI(step, enm);
	}
#endif
	sc->sc_flags &= ~LEMAC_ALLMULTI;
	sc->sc_if.if_flags &= ~IFF_ALLMULTI;
}

/* 
 * Do a hard reset of the board;
 */
void
lemac_reset(struct lemac_softc *const sc)
{
	unsigned data;

	/*
	 * Initialize board..
	 */
	sc->sc_flags &= ~LEMAC_LINKUP;
	ifq_clr_oactive(&sc->sc_if.if_snd);
	LEMAC_INTR_DISABLE(sc);

	LEMAC_OUTB(sc, LEMAC_REG_IOP, LEMAC_IOP_EEINIT);
	DELAY(LEMAC_EEP_DELAY);

	/*
	 * Read EEPROM information.  NOTE - the placement of this function
	 * is important because functions hereafter may rely on information
	 * read from the EEPROM.
	 */
	if ((data = lemac_read_eeprom(sc)) != LEMAC_EEP_CKSUM) { 
		printf("%s: reset: EEPROM checksum failed (0x%x)\n",
		    sc->sc_if.if_xname, data);
		return;
	}

	/*
	 * Update the control register to reflect the media choice
	 */
	data = LEMAC_INB(sc, LEMAC_REG_CTL);
	if ((data & (LEMAC_CTL_APD|LEMAC_CTL_PSL)) != sc->sc_ctlmode) {
		data &= ~(LEMAC_CTL_APD|LEMAC_CTL_PSL);
		data |= sc->sc_ctlmode;
		LEMAC_OUTB(sc, LEMAC_REG_CTL, data);
	}

	/*
	 *  Force to 2K mode if not already configured.
	 */

	data = LEMAC_INB(sc, LEMAC_REG_MBR);
	if (LEMAC_IS_2K_MODE(data)) {
		sc->sc_flags |= LEMAC_2K_MODE;
	} else if (LEMAC_IS_64K_MODE(data)) {
		data = (((data * 2) & 0xF) << 4);
		sc->sc_flags |= LEMAC_WAS_64K_MODE;
		LEMAC_OUTB(sc, LEMAC_REG_MBR, data);
	} else if (LEMAC_IS_32K_MODE(data)) {
		data = ((data & 0xF) << 4);
		sc->sc_flags |= LEMAC_WAS_32K_MODE;
		LEMAC_OUTB(sc, LEMAC_REG_MBR, data);
	} else {
		sc->sc_flags |= LEMAC_PIO_MODE;
		/* PIO mode */
	}

	/*
	 *  Initialize Free Memory Queue, Init mcast table with broadcast.
	 */

	lemac_init_adapmem(sc);
	sc->sc_flags |= LEMAC_ALIVE;
}

void
lemac_init(struct lemac_softc *const sc)
{
	if ((sc->sc_flags & LEMAC_ALIVE) == 0)
		return;

	/*
	 * If the interface has the up flag
	 */
	if (sc->sc_if.if_flags & IFF_UP) {
		int saved_cs = LEMAC_INB(sc, LEMAC_REG_CS);
		LEMAC_OUTB(sc, LEMAC_REG_CS,
		    saved_cs | (LEMAC_CS_TXD | LEMAC_CS_RXD));
		LEMAC_OUTB(sc, LEMAC_REG_PA0, sc->sc_arpcom.ac_enaddr[0]);
		LEMAC_OUTB(sc, LEMAC_REG_PA1, sc->sc_arpcom.ac_enaddr[1]);
		LEMAC_OUTB(sc, LEMAC_REG_PA2, sc->sc_arpcom.ac_enaddr[2]);
		LEMAC_OUTB(sc, LEMAC_REG_PA3, sc->sc_arpcom.ac_enaddr[3]);
		LEMAC_OUTB(sc, LEMAC_REG_PA4, sc->sc_arpcom.ac_enaddr[4]);
		LEMAC_OUTB(sc, LEMAC_REG_PA5, sc->sc_arpcom.ac_enaddr[5]);

		LEMAC_OUTB(sc, LEMAC_REG_IC,
		    LEMAC_INB(sc, LEMAC_REG_IC) | LEMAC_IC_IE);

		if (sc->sc_if.if_flags & IFF_PROMISC) {
			LEMAC_OUTB(sc, LEMAC_REG_CS,
			    LEMAC_CS_MCE | LEMAC_CS_PME);
		} else {
			LEMAC_INTR_DISABLE(sc);
			lemac_multicast_filter(sc);
			if (sc->sc_flags & LEMAC_ALLMULTI)
				bcopy(lemac_allmulti_mctbl, sc->sc_mctbl,
				    sizeof(sc->sc_mctbl));
			if (LEMAC_USE_PIO_MODE(sc)) {
				LEMAC_OUTB(sc, LEMAC_REG_IOP, 0);
				LEMAC_OUTB(sc, LEMAC_REG_PI1,
				    LEMAC_MCTBL_OFF & 0xFF);
				LEMAC_OUTB(sc, LEMAC_REG_PI2,
				    LEMAC_MCTBL_OFF >> 8);
				LEMAC_OUTSB(sc, LEMAC_REG_DAT,
				    sizeof(sc->sc_mctbl),
				    (void *)sc->sc_mctbl);
			} else {
				LEMAC_OUTB(sc, LEMAC_REG_MPN, 0);
				LEMAC_PUTBUF8(sc, LEMAC_MCTBL_OFF,
				    sizeof(sc->sc_mctbl),
				    (void *)sc->sc_mctbl);
			}

			LEMAC_OUTB(sc, LEMAC_REG_CS, LEMAC_CS_MCE);
		}

		LEMAC_OUTB(sc, LEMAC_REG_CTL,
		    LEMAC_INB(sc, LEMAC_REG_CTL) ^ LEMAC_CTL_LED);

		LEMAC_INTR_ENABLE(sc);
		sc->sc_if.if_flags |= IFF_RUNNING;
		lemac_ifstart(&sc->sc_if);
	} else {
		LEMAC_OUTB(sc, LEMAC_REG_CS, LEMAC_CS_RXD|LEMAC_CS_TXD);

		LEMAC_INTR_DISABLE(sc);
		sc->sc_if.if_flags &= ~IFF_RUNNING;
	}
}

void 
lemac_ifstart(struct ifnet *ifp)
{
	struct lemac_softc *const sc = LEMAC_IFP_TO_SOFTC(ifp);

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	LEMAC_INTR_DISABLE(sc);

	for (;;) {
		struct mbuf *m;
		struct mbuf *m0;
		int tx_pg;

		m = ifq_deq_begin(&ifp->if_snd);
		if (m == NULL)
			break;

		if ((sc->sc_csr.csr_tqc = LEMAC_INB(sc, LEMAC_REG_TQC)) >=
		    lemac_txmax) {
			sc->sc_cntrs.cntr_txfull++;
			ifq_deq_rollback(&ifp->if_snd, m);
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		/*
		 * get free memory page
		 */
		tx_pg = sc->sc_csr.csr_fmq = LEMAC_INB(sc, LEMAC_REG_FMQ);

		/*
		 * Check for good transmit page.
		 */
		if (tx_pg == 0 || tx_pg > sc->sc_lastpage) {
			sc->sc_cntrs.cntr_txnospc++;
			ifq_deq_rollback(&ifp->if_snd, m);
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		ifq_deq_commit(&ifp->if_snd, m);

		/*
		 * The first four bytes of each transmit buffer are for
		 * control information.  The first byte is the control
		 * byte, then the length (why not word aligned?), then
		 * the offset to the buffer.
		 */

		if (LEMAC_USE_PIO_MODE(sc)) {
			/* Shift 2K window. */
			LEMAC_OUTB(sc, LEMAC_REG_IOP, tx_pg);
			LEMAC_OUTB(sc, LEMAC_REG_PI1, 0);
			LEMAC_OUTB(sc, LEMAC_REG_PI2, 0);
			LEMAC_OUTB(sc, LEMAC_REG_DAT, sc->sc_txctl);
			LEMAC_OUTB(sc, LEMAC_REG_DAT,
			    (m->m_pkthdr.len >> 0) & 0xFF);
			LEMAC_OUTB(sc, LEMAC_REG_DAT,
			    (m->m_pkthdr.len >> 8) & 0xFF);
			LEMAC_OUTB(sc, LEMAC_REG_DAT, LEMAC_TX_HDRSZ);
			for (m0 = m; m0 != NULL; m0 = m0->m_next)
				LEMAC_OUTSB(sc, LEMAC_REG_DAT,
				    m0->m_len, m0->m_data);
		} else {
			bus_size_t txoff = /* (mtod(m, u_int32_t) &
			    (sizeof(u_int32_t) - 1)) + */ LEMAC_TX_HDRSZ;
			/* Shift 2K window. */
			LEMAC_OUTB(sc, LEMAC_REG_MPN, tx_pg);
			LEMAC_PUT8(sc, 0, sc->sc_txctl);
			LEMAC_PUT8(sc, 1, (m->m_pkthdr.len >> 0) & 0xFF);
			LEMAC_PUT8(sc, 2, (m->m_pkthdr.len >> 8) & 0xFF);
			LEMAC_PUT8(sc, 3, txoff);

			/*
			 * Copy the packet to the board
			 */
			for (m0 = m; m0 != NULL; m0 = m0->m_next) {
#if 0
				LEMAC_PUTBUF8(sc, txoff, m0->m_len,
				    m0->m_data);
				txoff += m0->m_len;
#else
				const u_int8_t *cp = m0->m_data;
				int len = m0->m_len;
#if 0
				if ((txoff & 3) == (((long)cp) & 3) &&
				    len >= 4) {
					if (txoff & 3) {
						int alen = (~txoff & 3);
						LEMAC_PUTBUF8(sc, txoff, alen,
						    cp);
						cp += alen;
						txoff += alen;
						len -= alen;
					}
					if (len >= 4) {
						LEMAC_PUTBUF32(sc, txoff,
						    len / 4, cp);
						cp += len & ~3;
						txoff += len & ~3;
						len &= 3;
					}
				}
#endif
				if ((txoff & 1) == (((long)cp) & 1) &&
				    len >= 2) {
					if (txoff & 1) {
						int alen = (~txoff & 1);
						LEMAC_PUTBUF8(sc, txoff, alen,
						    cp);
						cp += alen;
						txoff += alen;
						len -= alen;
					}
					if (len >= 2) {
						LEMAC_PUTBUF16(sc, txoff,
						    len / 2, (void *)cp);
						cp += len & ~1;
						txoff += len & ~1;
						len &= 1;
					}
				}
				if (len > 0) {
					LEMAC_PUTBUF8(sc, txoff, len, cp);
					txoff += len;
				}
#endif
			}
		}

		/* tell chip to transmit this packet */
		LEMAC_OUTB(sc, LEMAC_REG_TQ, tx_pg);
#if NBPFILTER > 0
		if (sc->sc_if.if_bpf != NULL)
			bpf_mtap(sc->sc_if.if_bpf, m, BPF_DIRECTION_OUT);
#endif
		m_freem(m);			/* free the mbuf */
	}
	LEMAC_INTR_ENABLE(sc);
}

int
lemac_ifioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct lemac_softc *const sc = LEMAC_IFP_TO_SOFTC(ifp);
	struct ifreq *ifr = (struct ifreq *)data;
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		lemac_init(sc);
		break;

	case SIOCSIFFLAGS:
		lemac_init(sc);
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_ifmedia, cmd);
		break;

	default:
		error = ether_ioctl(ifp, &sc->sc_arpcom, cmd, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			lemac_init(sc);
		error = 0;
	}

	splx(s);
	return (error);
}

int
lemac_ifmedia_change(struct ifnet *const ifp)
{
	struct lemac_softc *const sc = LEMAC_IFP_TO_SOFTC(ifp);
	unsigned new_ctl;

	switch (IFM_SUBTYPE(sc->sc_ifmedia.ifm_media)) {
	case IFM_10_T:
		new_ctl = LEMAC_CTL_APD;
		break;
	case IFM_10_2:
	case IFM_10_5:
		new_ctl = LEMAC_CTL_APD|LEMAC_CTL_PSL;
		break;
	case IFM_AUTO:
		new_ctl = 0;
		break;
	default:
		return (EINVAL);
	}
	if (sc->sc_ctlmode != new_ctl) {
		sc->sc_ctlmode = new_ctl;
		lemac_reset(sc);
		if (sc->sc_if.if_flags & IFF_UP)
			lemac_init(sc);
	}
	return (0);
}

/*
 * Media status callback
 */
void
lemac_ifmedia_status(struct ifnet *const ifp, struct ifmediareq *req)
{
	struct lemac_softc *sc = LEMAC_IFP_TO_SOFTC(ifp);
	unsigned data = LEMAC_INB(sc, LEMAC_REG_CNF);

	req->ifm_status = IFM_AVALID;
	if (sc->sc_flags & LEMAC_LINKUP)
		req->ifm_status |= IFM_ACTIVE;

	if (sc->sc_ctlmode & LEMAC_CTL_APD) {
		if (sc->sc_ctlmode & LEMAC_CTL_PSL) {
			req->ifm_active = IFM_10_5;
		} else {
			req->ifm_active = IFM_10_T;
		}
	} else {
		/*
		 * The link bit of the configuration register reflects the
		 * current media choice when auto-port is enabled.
		 */
		if (data & LEMAC_CNF_NOLINK) {
			req->ifm_active = IFM_10_5;
		} else {
			req->ifm_active = IFM_10_T;
		}
	}

	req->ifm_active |= IFM_ETHER;
}

int
lemac_port_check(const bus_space_tag_t iot, const bus_space_handle_t ioh)
{
	unsigned char hwaddr[6];

	if (lemac_read_macaddr(hwaddr, iot, ioh, LEMAC_REG_APD, 0) == 0)
		return (1);
	if (lemac_read_macaddr(hwaddr, iot, ioh, LEMAC_REG_APD, 1) == 0)
		return (1);
	return (0);
}

void
lemac_info_get(const bus_space_tag_t iot, const bus_space_handle_t ioh,
    bus_addr_t *maddr_p, bus_size_t *msize_p, int *irq_p)
{
	unsigned data;

	*irq_p = LEMAC_DECODEIRQ(bus_space_read_1(iot, ioh, LEMAC_REG_IC) &
	    LEMAC_IC_IRQMSK);

	data = bus_space_read_1(iot, ioh, LEMAC_REG_MBR);
	if (LEMAC_IS_2K_MODE(data)) {
		*maddr_p = data * (2 * 1024) + (512 * 1024);
		*msize_p =  2 * 1024;
	} else if (LEMAC_IS_64K_MODE(data)) {
		*maddr_p = data * 64 * 1024;
		*msize_p = 64 * 1024;
	} else if (LEMAC_IS_32K_MODE(data)) {
		*maddr_p = data * 32 * 1024;
		*msize_p = 32* 1024;
	} else {
		*maddr_p = 0;
		*msize_p = 0;
	}
}

/*
 * What to do upon receipt of an interrupt.
 */
int
lemac_intr(void *arg)
{
	struct lemac_softc *const sc = arg;
	int cs_value;

	LEMAC_INTR_DISABLE(sc);	/* Mask interrupts */

	/*
	 * Determine cause of interrupt.  Receive events take
	 * priority over Transmit.
	 */

	cs_value = LEMAC_INB(sc, LEMAC_REG_CS);

	/*
	 * Check for Receive Queue not being empty.
	 * Check for Transmit Done Queue not being empty.
	 */

	if (cs_value & LEMAC_CS_RNE)
		lemac_rne_intr(sc);
	if (cs_value & LEMAC_CS_TNE)
		lemac_tne_intr(sc);

	/*
	 * Check for Transmitter Disabled.
	 * Check for Receiver Disabled.
	 */

	if (cs_value & LEMAC_CS_TXD)
		lemac_txd_intr(sc, cs_value);
	if (cs_value & LEMAC_CS_RXD)
		lemac_rxd_intr(sc, cs_value);

	/*
	 * Toggle LED and unmask interrupts.
	 */

	sc->sc_csr.csr_cs = LEMAC_INB(sc, LEMAC_REG_CS);

	LEMAC_OUTB(sc, LEMAC_REG_CTL,
	    LEMAC_INB(sc, LEMAC_REG_CTL) ^ LEMAC_CTL_LED);
	LEMAC_INTR_ENABLE(sc);		/* Unmask interrupts */

#if 0
	if (cs_value)
		rnd_add_uint32(&sc->rnd_source, cs_value);
#endif

	return (1);
}

const char *const lemac_modes[4] = {
	"PIO mode (internal 2KB window)",
	"2KB window",
	"changed 32KB window to 2KB",
	"changed 64KB window to 2KB",
};

void
lemac_ifattach(struct lemac_softc *sc)
{
	struct ifnet *const ifp = &sc->sc_if;

	bcopy(sc->sc_dv.dv_xname, ifp->if_xname, IFNAMSIZ);

	lemac_reset(sc);

	lemac_read_macaddr(sc->sc_arpcom.ac_enaddr, sc->sc_iot, sc->sc_ioh,
	    LEMAC_REG_APD, 0);
	
	printf(": %s\n", sc->sc_prodname);

	printf("%s: address %s, %dKB RAM, %s\n", ifp->if_xname,
	    ether_sprintf(sc->sc_arpcom.ac_enaddr), sc->sc_lastpage * 2 + 2,
	    lemac_modes[sc->sc_flags & LEMAC_MODE_MASK]);

	ifp->if_softc = (void *)sc;
	ifp->if_start = lemac_ifstart;
	ifp->if_ioctl = lemac_ifioctl;

	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;

	if (sc->sc_flags & LEMAC_ALIVE) {
		uint64_t media;

		if_attach(ifp);
		ether_ifattach(ifp);

#if 0
		rnd_attach_source(&sc->rnd_source, sc->sc_dv.dv_xname,
		    RND_TYPE_NET, 0);
#endif

		ifmedia_init(&sc->sc_ifmedia, 0, lemac_ifmedia_change,
		    lemac_ifmedia_status);
		if (sc->sc_prodname[4] == '5')	/* DE205 is UTP/AUI */
			ifmedia_add(&sc->sc_ifmedia, IFM_ETHER | IFM_AUTO, 0,
			    0);
		if (sc->sc_prodname[4] != '3')	/* DE204 & 205 have UTP */
			ifmedia_add(&sc->sc_ifmedia, IFM_ETHER | IFM_10_T, 0,
			    0);
		if (sc->sc_prodname[4] != '4')	/* DE203 & 205 have BNC */
			ifmedia_add(&sc->sc_ifmedia, IFM_ETHER | IFM_10_5, 0,
			    0);
		switch (sc->sc_prodname[4]) {
		case '3':
			media = IFM_10_5;
			break;
		case '4':
			media = IFM_10_T;
			break;
		default:
			media = IFM_AUTO;
			break;
		}
		ifmedia_set(&sc->sc_ifmedia, IFM_ETHER | media);
	} else {
		printf("%s: disabled due to error\n", ifp->if_xname);
	}
}
