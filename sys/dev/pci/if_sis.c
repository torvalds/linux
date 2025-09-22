/*	$OpenBSD: if_sis.c,v 1.146 2024/08/31 16:23:09 deraadt Exp $ */
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
 * $FreeBSD: src/sys/pci/if_sis.c,v 1.30 2001/02/06 10:11:47 phk Exp $
 */

/*
 * SiS 900/SiS 7016 fast ethernet PCI NIC driver. Datasheets are
 * available from http://www.sis.com.tw.
 *
 * This driver also supports the NatSemi DP83815. Datasheets are
 * available from http://www.national.com.
 *
 * Written by Bill Paul <wpaul@ee.columbia.edu>
 * Electrical Engineering Department
 * Columbia University, New York City
 */

/*
 * The SiS 900 is a fairly simple chip. It uses bus master DMA with
 * simple TX and RX descriptors of 3 longwords in size. The receiver
 * has a single perfect filter entry for the station address and a
 * 128-bit multicast hash table. The SiS 900 has a built-in MII-based
 * transceiver while the 7016 requires an external transceiver chip.
 * Both chips offer the standard bit-bang MII interface as well as
 * an enhanced PHY interface which simplifies accessing MII registers.
 *
 * The only downside to this chipset is that RX descriptors must be
 * longword aligned.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/timeout.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net/if_media.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <sys/device.h>

#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#define SIS_USEIOSPACE

#include <dev/pci/if_sisreg.h>

int sis_probe(struct device *, void *, void *);
void sis_attach(struct device *, struct device *, void *);
int sis_activate(struct device *, int);

const struct cfattach sis_ca = {
	sizeof(struct sis_softc), sis_probe, sis_attach, NULL,
	sis_activate
};

struct cfdriver sis_cd = {
	NULL, "sis", DV_IFNET
};

int sis_intr(void *);
void sis_fill_rx_ring(struct sis_softc *);
int sis_newbuf(struct sis_softc *, struct sis_desc *);
int sis_encap(struct sis_softc *, struct mbuf *, u_int32_t *);
void sis_rxeof(struct sis_softc *);
void sis_txeof(struct sis_softc *);
void sis_tick(void *);
void sis_start(struct ifnet *);
int sis_ioctl(struct ifnet *, u_long, caddr_t);
void sis_init(void *);
void sis_stop(struct sis_softc *);
void sis_watchdog(struct ifnet *);
int sis_ifmedia_upd(struct ifnet *);
void sis_ifmedia_sts(struct ifnet *, struct ifmediareq *);

u_int16_t sis_reverse(u_int16_t);
void sis_delay(struct sis_softc *);
void sis_eeprom_idle(struct sis_softc *);
void sis_eeprom_putbyte(struct sis_softc *, int);
void sis_eeprom_getword(struct sis_softc *, int, u_int16_t *);
#if defined(__amd64__) || defined(__i386__)
void sis_read_cmos(struct sis_softc *, struct pci_attach_args *, caddr_t, int, int);
#endif
void sis_read_mac(struct sis_softc *, struct pci_attach_args *);
void sis_read_eeprom(struct sis_softc *, caddr_t, int, int, int);
void sis_read96x_mac(struct sis_softc *);

void sis_mii_sync(struct sis_softc *);
void sis_mii_send(struct sis_softc *, u_int32_t, int);
int sis_mii_readreg(struct sis_softc *, struct sis_mii_frame *);
int sis_mii_writereg(struct sis_softc *, struct sis_mii_frame *);
int sis_miibus_readreg(struct device *, int, int);
void sis_miibus_writereg(struct device *, int, int, int);
void sis_miibus_statchg(struct device *);

u_int32_t sis_mchash(struct sis_softc *, const uint8_t *);
void sis_iff(struct sis_softc *);
void sis_iff_ns(struct sis_softc *);
void sis_iff_sis(struct sis_softc *);
void sis_reset(struct sis_softc *);
int sis_ring_init(struct sis_softc *);

#define SIS_SETBIT(sc, reg, x)				\
	CSR_WRITE_4(sc, reg,				\
		CSR_READ_4(sc, reg) | (x))

#define SIS_CLRBIT(sc, reg, x)				\
	CSR_WRITE_4(sc, reg,				\
		CSR_READ_4(sc, reg) & ~(x))

#define SIO_SET(x)					\
	CSR_WRITE_4(sc, SIS_EECTL, CSR_READ_4(sc, SIS_EECTL) | x)

#define SIO_CLR(x)					\
	CSR_WRITE_4(sc, SIS_EECTL, CSR_READ_4(sc, SIS_EECTL) & ~x)

const struct pci_matchid sis_devices[] = {
	{ PCI_VENDOR_SIS, PCI_PRODUCT_SIS_900 },
	{ PCI_VENDOR_SIS, PCI_PRODUCT_SIS_7016 },
	{ PCI_VENDOR_NS, PCI_PRODUCT_NS_DP83815 }
};

/*
 * Routine to reverse the bits in a word. Stolen almost
 * verbatim from /usr/games/fortune.
 */
u_int16_t
sis_reverse(u_int16_t n)
{
	n = ((n >>  1) & 0x5555) | ((n <<  1) & 0xaaaa);
	n = ((n >>  2) & 0x3333) | ((n <<  2) & 0xcccc);
	n = ((n >>  4) & 0x0f0f) | ((n <<  4) & 0xf0f0);
	n = ((n >>  8) & 0x00ff) | ((n <<  8) & 0xff00);

	return (n);
}

void
sis_delay(struct sis_softc *sc)
{
	int			idx;

	for (idx = (300 / 33) + 1; idx > 0; idx--)
		CSR_READ_4(sc, SIS_CSR);
}

void
sis_eeprom_idle(struct sis_softc *sc)
{
	int			i;

	SIO_SET(SIS_EECTL_CSEL);
	sis_delay(sc);
	SIO_SET(SIS_EECTL_CLK);
	sis_delay(sc);

	for (i = 0; i < 25; i++) {
		SIO_CLR(SIS_EECTL_CLK);
		sis_delay(sc);
		SIO_SET(SIS_EECTL_CLK);
		sis_delay(sc);
	}

	SIO_CLR(SIS_EECTL_CLK);
	sis_delay(sc);
	SIO_CLR(SIS_EECTL_CSEL);
	sis_delay(sc);
	CSR_WRITE_4(sc, SIS_EECTL, 0x00000000);
}

/*
 * Send a read command and address to the EEPROM, check for ACK.
 */
void
sis_eeprom_putbyte(struct sis_softc *sc, int addr)
{
	int			d, i;

	d = addr | SIS_EECMD_READ;

	/*
	 * Feed in each bit and strobe the clock.
	 */
	for (i = 0x400; i; i >>= 1) {
		if (d & i)
			SIO_SET(SIS_EECTL_DIN);
		else
			SIO_CLR(SIS_EECTL_DIN);
		sis_delay(sc);
		SIO_SET(SIS_EECTL_CLK);
		sis_delay(sc);
		SIO_CLR(SIS_EECTL_CLK);
		sis_delay(sc);
	}
}

/*
 * Read a word of data stored in the EEPROM at address 'addr.'
 */
void
sis_eeprom_getword(struct sis_softc *sc, int addr, u_int16_t *dest)
{
	int			i;
	u_int16_t		word = 0;

	/* Force EEPROM to idle state. */
	sis_eeprom_idle(sc);

	/* Enter EEPROM access mode. */
	sis_delay(sc);
	SIO_CLR(SIS_EECTL_CLK);
	sis_delay(sc);
	SIO_SET(SIS_EECTL_CSEL);
	sis_delay(sc);

	/*
	 * Send address of word we want to read.
	 */
	sis_eeprom_putbyte(sc, addr);

	/*
	 * Start reading bits from EEPROM.
	 */
	for (i = 0x8000; i; i >>= 1) {
		SIO_SET(SIS_EECTL_CLK);
		sis_delay(sc);
		if (CSR_READ_4(sc, SIS_EECTL) & SIS_EECTL_DOUT)
			word |= i;
		sis_delay(sc);
		SIO_CLR(SIS_EECTL_CLK);
		sis_delay(sc);
	}

	/* Turn off EEPROM access mode. */
	sis_eeprom_idle(sc);

	*dest = word;
}

/*
 * Read a sequence of words from the EEPROM.
 */
void
sis_read_eeprom(struct sis_softc *sc, caddr_t dest,
    int off, int cnt, int swap)
{
	int			i;
	u_int16_t		word = 0, *ptr;

	for (i = 0; i < cnt; i++) {
		sis_eeprom_getword(sc, off + i, &word);
		ptr = (u_int16_t *)(dest + (i * 2));
		if (swap)
			*ptr = letoh16(word);
		else
			*ptr = word;
	}
}

#if defined(__amd64__) || defined(__i386__)
void
sis_read_cmos(struct sis_softc *sc, struct pci_attach_args *pa,
    caddr_t dest, int off, int cnt)
{
	u_int32_t reg;
	int i;

	reg = pci_conf_read(pa->pa_pc, pa->pa_tag, 0x48);
	pci_conf_write(pa->pa_pc, pa->pa_tag, 0x48, reg | 0x40);

	for (i = 0; i < cnt; i++) {
		bus_space_write_1(pa->pa_iot, 0x0, 0x70, i + off);
		*(dest + i) = bus_space_read_1(pa->pa_iot, 0x0, 0x71);
	}

	pci_conf_write(pa->pa_pc, pa->pa_tag, 0x48, reg & ~0x40);
}
#endif

void
sis_read_mac(struct sis_softc *sc, struct pci_attach_args *pa)
{
	uint32_t rxfilt, csrsave;
	u_int16_t *enaddr = (u_int16_t *) &sc->arpcom.ac_enaddr;

	rxfilt = CSR_READ_4(sc, SIS_RXFILT_CTL);
	csrsave = CSR_READ_4(sc, SIS_CSR);

	CSR_WRITE_4(sc, SIS_CSR, SIS_CSR_RELOAD | csrsave);
	CSR_WRITE_4(sc, SIS_CSR, 0);

	CSR_WRITE_4(sc, SIS_RXFILT_CTL, rxfilt & ~SIS_RXFILTCTL_ENABLE);

	CSR_WRITE_4(sc, SIS_RXFILT_CTL, SIS_FILTADDR_PAR0);
	enaddr[0] = letoh16(CSR_READ_4(sc, SIS_RXFILT_DATA) & 0xffff);
	CSR_WRITE_4(sc, SIS_RXFILT_CTL, SIS_FILTADDR_PAR1);
	enaddr[1] = letoh16(CSR_READ_4(sc, SIS_RXFILT_DATA) & 0xffff);
	CSR_WRITE_4(sc, SIS_RXFILT_CTL, SIS_FILTADDR_PAR2);
	enaddr[2] = letoh16(CSR_READ_4(sc, SIS_RXFILT_DATA) & 0xffff);

	CSR_WRITE_4(sc, SIS_RXFILT_CTL, rxfilt);
	CSR_WRITE_4(sc, SIS_CSR, csrsave);
}

void
sis_read96x_mac(struct sis_softc *sc)
{
	int i;

	SIO_SET(SIS96x_EECTL_REQ);

	for (i = 0; i < 2000; i++) {
		if ((CSR_READ_4(sc, SIS_EECTL) & SIS96x_EECTL_GNT)) {
			sis_read_eeprom(sc, (caddr_t)&sc->arpcom.ac_enaddr,
			    SIS_EE_NODEADDR, 3, 1);
			break;
		} else
			DELAY(1);
	}

	SIO_SET(SIS96x_EECTL_DONE);
}

/*
 * Sync the PHYs by setting data bit and strobing the clock 32 times.
 */
void
sis_mii_sync(struct sis_softc *sc)
{
	int			i;
 
 	SIO_SET(SIS_MII_DIR|SIS_MII_DATA);
 
 	for (i = 0; i < 32; i++) {
 		SIO_SET(SIS_MII_CLK);
 		DELAY(1);
 		SIO_CLR(SIS_MII_CLK);
 		DELAY(1);
 	}
}
 
/*
 * Clock a series of bits through the MII.
 */
void
sis_mii_send(struct sis_softc *sc, u_int32_t bits, int cnt)
{
	int			i;
 
	SIO_CLR(SIS_MII_CLK);
 
	for (i = (0x1 << (cnt - 1)); i; i >>= 1) {
		if (bits & i)
			SIO_SET(SIS_MII_DATA);
		else
			SIO_CLR(SIS_MII_DATA);
		DELAY(1);
		SIO_CLR(SIS_MII_CLK);
		DELAY(1);
		SIO_SET(SIS_MII_CLK);
	}
}
 
/*
 * Read an PHY register through the MII.
 */
int
sis_mii_readreg(struct sis_softc *sc, struct sis_mii_frame *frame)
{
	int			i, ack, s;
 
	s = splnet();
 
	/*
	 * Set up frame for RX.
	 */
	frame->mii_stdelim = SIS_MII_STARTDELIM;
	frame->mii_opcode = SIS_MII_READOP;
	frame->mii_turnaround = 0;
	frame->mii_data = 0;
 	
	/*
 	 * Turn on data xmit.
	 */
	SIO_SET(SIS_MII_DIR);

	sis_mii_sync(sc);
 
	/*
	 * Send command/address info.
	 */
	sis_mii_send(sc, frame->mii_stdelim, 2);
	sis_mii_send(sc, frame->mii_opcode, 2);
	sis_mii_send(sc, frame->mii_phyaddr, 5);
	sis_mii_send(sc, frame->mii_regaddr, 5);
 
	/* Idle bit */
	SIO_CLR((SIS_MII_CLK|SIS_MII_DATA));
	DELAY(1);
	SIO_SET(SIS_MII_CLK);
	DELAY(1);
 
	/* Turn off xmit. */
	SIO_CLR(SIS_MII_DIR);
 
	/* Check for ack */
	SIO_CLR(SIS_MII_CLK);
	DELAY(1);
	ack = CSR_READ_4(sc, SIS_EECTL) & SIS_MII_DATA;
	SIO_SET(SIS_MII_CLK);
	DELAY(1);
 
	/*
	 * Now try reading data bits. If the ack failed, we still
	 * need to clock through 16 cycles to keep the PHY(s) in sync.
	 */
	if (ack) {
		for(i = 0; i < 16; i++) {
			SIO_CLR(SIS_MII_CLK);
			DELAY(1);
			SIO_SET(SIS_MII_CLK);
			DELAY(1);
		}
		goto fail;
	}
 
	for (i = 0x8000; i; i >>= 1) {
		SIO_CLR(SIS_MII_CLK);
		DELAY(1);
		if (!ack) {
			if (CSR_READ_4(sc, SIS_EECTL) & SIS_MII_DATA)
				frame->mii_data |= i;
			DELAY(1);
		}
		SIO_SET(SIS_MII_CLK);
		DELAY(1);
	}

fail:

	SIO_CLR(SIS_MII_CLK);
	DELAY(1);
	SIO_SET(SIS_MII_CLK);
	DELAY(1);

	splx(s);

	if (ack)
		return (1);
	return (0);
}
 
/*
 * Write to a PHY register through the MII.
 */
int
sis_mii_writereg(struct sis_softc *sc, struct sis_mii_frame *frame)
{
	int			s;
 
	s = splnet();
 	/*
 	 * Set up frame for TX.
 	 */
 
 	frame->mii_stdelim = SIS_MII_STARTDELIM;
 	frame->mii_opcode = SIS_MII_WRITEOP;
 	frame->mii_turnaround = SIS_MII_TURNAROUND;
 	
 	/*
  	 * Turn on data output.
 	 */
 	SIO_SET(SIS_MII_DIR);
 
 	sis_mii_sync(sc);
 
 	sis_mii_send(sc, frame->mii_stdelim, 2);
 	sis_mii_send(sc, frame->mii_opcode, 2);
 	sis_mii_send(sc, frame->mii_phyaddr, 5);
 	sis_mii_send(sc, frame->mii_regaddr, 5);
 	sis_mii_send(sc, frame->mii_turnaround, 2);
 	sis_mii_send(sc, frame->mii_data, 16);
 
 	/* Idle bit. */
 	SIO_SET(SIS_MII_CLK);
 	DELAY(1);
 	SIO_CLR(SIS_MII_CLK);
 	DELAY(1);
 
 	/*
 	 * Turn off xmit.
 	 */
 	SIO_CLR(SIS_MII_DIR);
 
 	splx(s);
 
 	return (0);
}

int
sis_miibus_readreg(struct device *self, int phy, int reg)
{
	struct sis_softc	*sc = (struct sis_softc *)self;
	struct sis_mii_frame    frame;

	if (sc->sis_type == SIS_TYPE_83815) {
		if (phy != 0)
			return (0);
		/*
		 * The NatSemi chip can take a while after
		 * a reset to come ready, during which the BMSR
		 * returns a value of 0. This is *never* supposed
		 * to happen: some of the BMSR bits are meant to
		 * be hardwired in the on position, and this can
		 * confuse the miibus code a bit during the probe
		 * and attach phase. So we make an effort to check
		 * for this condition and wait for it to clear.
		 */
		if (!CSR_READ_4(sc, NS_BMSR))
			DELAY(1000);
		return CSR_READ_4(sc, NS_BMCR + (reg * 4));
	}

	/*
	 * Chipsets < SIS_635 seem not to be able to read/write
	 * through mdio. Use the enhanced PHY access register
	 * again for them.
	 */
	if (sc->sis_type == SIS_TYPE_900 &&
	    sc->sis_rev < SIS_REV_635) {
		int i, val = 0;

		if (phy != 0)
			return (0);

		CSR_WRITE_4(sc, SIS_PHYCTL,
		    (phy << 11) | (reg << 6) | SIS_PHYOP_READ);
		SIS_SETBIT(sc, SIS_PHYCTL, SIS_PHYCTL_ACCESS);

		for (i = 0; i < SIS_TIMEOUT; i++) {
			if (!(CSR_READ_4(sc, SIS_PHYCTL) & SIS_PHYCTL_ACCESS))
				break;
		}

		if (i == SIS_TIMEOUT) {
			printf("%s: PHY failed to come ready\n",
			    sc->sc_dev.dv_xname);
			return (0);
		}

		val = (CSR_READ_4(sc, SIS_PHYCTL) >> 16) & 0xFFFF;

		if (val == 0xFFFF)
			return (0);

		return (val);
	} else {
		bzero(&frame, sizeof(frame));

		frame.mii_phyaddr = phy;
		frame.mii_regaddr = reg;
		sis_mii_readreg(sc, &frame);

		return (frame.mii_data);
	}
}

void
sis_miibus_writereg(struct device *self, int phy, int reg, int data)
{
	struct sis_softc	*sc = (struct sis_softc *)self;
	struct sis_mii_frame	frame;

	if (sc->sis_type == SIS_TYPE_83815) {
		if (phy != 0)
			return;
		CSR_WRITE_4(sc, NS_BMCR + (reg * 4), data);
		return;
	}

	/*
	 * Chipsets < SIS_635 seem not to be able to read/write
	 * through mdio. Use the enhanced PHY access register
	 * again for them.
	 */
	if (sc->sis_type == SIS_TYPE_900 &&
	    sc->sis_rev < SIS_REV_635) {
		int i;

		if (phy != 0)
			return;

		CSR_WRITE_4(sc, SIS_PHYCTL, (data << 16) | (phy << 11) |
		    (reg << 6) | SIS_PHYOP_WRITE);
		SIS_SETBIT(sc, SIS_PHYCTL, SIS_PHYCTL_ACCESS);

		for (i = 0; i < SIS_TIMEOUT; i++) {
			if (!(CSR_READ_4(sc, SIS_PHYCTL) & SIS_PHYCTL_ACCESS))
				break;
		}

		if (i == SIS_TIMEOUT)
			printf("%s: PHY failed to come ready\n",
			    sc->sc_dev.dv_xname);
	} else {
		bzero(&frame, sizeof(frame));

		frame.mii_phyaddr = phy;
		frame.mii_regaddr = reg;
		frame.mii_data = data;
		sis_mii_writereg(sc, &frame);
	}
}

void
sis_miibus_statchg(struct device *self)
{
	struct sis_softc	*sc = (struct sis_softc *)self;
	struct ifnet		*ifp = &sc->arpcom.ac_if;
	struct mii_data		*mii = &sc->sc_mii;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	sc->sis_link = 0;
	if ((mii->mii_media_status & (IFM_ACTIVE | IFM_AVALID)) ==
	    (IFM_ACTIVE | IFM_AVALID)) {
		switch (IFM_SUBTYPE(mii->mii_media_active)) {
		case IFM_10_T:
			CSR_WRITE_4(sc, SIS_TX_CFG, SIS_TXCFG_10);
			sc->sis_link++;
			break;
		case IFM_100_TX:
			CSR_WRITE_4(sc, SIS_TX_CFG, SIS_TXCFG_100);
			sc->sis_link++;
			break;
		default:
			break;
		}
	}

	if (!sc->sis_link) {
		/*
		 * Stopping MACs seem to reset SIS_TX_LISTPTR and
		 * SIS_RX_LISTPTR which in turn requires resetting
		 * TX/RX buffers.  So just don't do anything for
		 * lost link.
		 */
		return;
	}

	/* Set full/half duplex mode. */
	if ((IFM_OPTIONS(mii->mii_media_active) & IFM_FDX) != 0) {
		SIS_SETBIT(sc, SIS_TX_CFG,
		    (SIS_TXCFG_IGN_HBEAT | SIS_TXCFG_IGN_CARR));
		SIS_SETBIT(sc, SIS_RX_CFG, SIS_RXCFG_RX_TXPKTS);
	} else {
		SIS_CLRBIT(sc, SIS_TX_CFG,
		    (SIS_TXCFG_IGN_HBEAT | SIS_TXCFG_IGN_CARR));
		SIS_CLRBIT(sc, SIS_RX_CFG, SIS_RXCFG_RX_TXPKTS);
	}

	if (sc->sis_type == SIS_TYPE_83815 && sc->sis_srr >= NS_SRR_16A) {
		/*
		 * MPII03.D: Half Duplex Excessive Collisions.
		 * Also page 49 in 83816 manual
		 */
		SIS_SETBIT(sc, SIS_TX_CFG, SIS_TXCFG_MPII03D);
	}

	/*
	 * Some DP83815s experience problems when used with short
	 * (< 30m/100ft) Ethernet cables in 100baseTX mode.  This
	 * sequence adjusts the DSP's signal attenuation to fix the
	 * problem.
	 */
	if (sc->sis_type == SIS_TYPE_83815 && sc->sis_srr < NS_SRR_16A &&
	    IFM_SUBTYPE(mii->mii_media_active) == IFM_100_TX) {
		uint32_t reg;

		CSR_WRITE_4(sc, NS_PHY_PAGE, 0x0001);
		reg = CSR_READ_4(sc, NS_PHY_DSPCFG) & 0xfff;
		CSR_WRITE_4(sc, NS_PHY_DSPCFG, reg | 0x1000);
		DELAY(100);
		reg = CSR_READ_4(sc, NS_PHY_TDATA) & 0xff;
		if ((reg & 0x0080) == 0 || (reg > 0xd8 && reg <= 0xff)) {
#ifdef DEBUG
			printf("%s: Applying short cable fix (reg=%x)\n",
			    sc->sc_dev.dv_xname, reg);
#endif
			CSR_WRITE_4(sc, NS_PHY_TDATA, 0x00e8);
			SIS_SETBIT(sc, NS_PHY_DSPCFG, 0x20);
		}
		CSR_WRITE_4(sc, NS_PHY_PAGE, 0);
	}
	/* Enable TX/RX MACs. */
	SIS_CLRBIT(sc, SIS_CSR, SIS_CSR_TX_DISABLE | SIS_CSR_RX_DISABLE);
	SIS_SETBIT(sc, SIS_CSR, SIS_CSR_TX_ENABLE | SIS_CSR_RX_ENABLE);
}

u_int32_t
sis_mchash(struct sis_softc *sc, const uint8_t *addr)
{
	uint32_t		crc;

	/* Compute CRC for the address value. */
	crc = ether_crc32_be(addr, ETHER_ADDR_LEN);

	/*
	 * return the filter bit position
	 *
	 * The NatSemi chip has a 512-bit filter, which is
	 * different than the SiS, so we special-case it.
	 */
	if (sc->sis_type == SIS_TYPE_83815)
		return (crc >> 23);
	else if (sc->sis_rev >= SIS_REV_635 ||
	    sc->sis_rev == SIS_REV_900B)
		return (crc >> 24);
	else
		return (crc >> 25);
}

void
sis_iff(struct sis_softc *sc)
{
	if (sc->sis_type == SIS_TYPE_83815)
		sis_iff_ns(sc);
	else
		sis_iff_sis(sc);
}

void
sis_iff_ns(struct sis_softc *sc)
{
	struct ifnet		*ifp = &sc->arpcom.ac_if;
	struct arpcom		*ac = &sc->arpcom;
	struct ether_multi	*enm;
	struct ether_multistep  step;
	u_int32_t		h = 0, i, rxfilt;
	int			bit, index;

	rxfilt = CSR_READ_4(sc, SIS_RXFILT_CTL);
	if (rxfilt & SIS_RXFILTCTL_ENABLE) {
		/*
		 * Filter should be disabled to program other bits.
		 */
		CSR_WRITE_4(sc, SIS_RXFILT_CTL, rxfilt & ~SIS_RXFILTCTL_ENABLE);
		CSR_READ_4(sc, SIS_RXFILT_CTL);
	}
	rxfilt &= ~(SIS_RXFILTCTL_ALLMULTI | SIS_RXFILTCTL_ALLPHYS |
	    NS_RXFILTCTL_ARP | SIS_RXFILTCTL_BROAD | NS_RXFILTCTL_MCHASH |
	    NS_RXFILTCTL_PERFECT);
	ifp->if_flags &= ~IFF_ALLMULTI;

	/*
	 * Always accept ARP frames.
	 * Always accept broadcast frames.
	 * Always accept frames destined to our station address.
	 */
	rxfilt |= NS_RXFILTCTL_ARP | SIS_RXFILTCTL_BROAD |
	    NS_RXFILTCTL_PERFECT;

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		rxfilt |= SIS_RXFILTCTL_ALLMULTI;
		if (ifp->if_flags & IFF_PROMISC)
			rxfilt |= SIS_RXFILTCTL_ALLPHYS;
	} else {
		/*
		 * We have to explicitly enable the multicast hash table
		 * on the NatSemi chip if we want to use it, which we do.
		 */
		rxfilt |= NS_RXFILTCTL_MCHASH;

		/* first, zot all the existing hash bits */
		for (i = 0; i < 32; i++) {
			CSR_WRITE_4(sc, SIS_RXFILT_CTL, NS_FILTADDR_FMEM_LO + (i * 2));
			CSR_WRITE_4(sc, SIS_RXFILT_DATA, 0);
		}

		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			h = sis_mchash(sc, enm->enm_addrlo);

			index = h >> 3;
			bit = h & 0x1F;

			CSR_WRITE_4(sc, SIS_RXFILT_CTL, NS_FILTADDR_FMEM_LO + index);

			if (bit > 0xF)
				bit -= 0x10;

			SIS_SETBIT(sc, SIS_RXFILT_DATA, (1 << bit));

			ETHER_NEXT_MULTI(step, enm);
		}
	}

	CSR_WRITE_4(sc, SIS_RXFILT_CTL, rxfilt);
	/* Turn the receive filter on. */
	CSR_WRITE_4(sc, SIS_RXFILT_CTL, rxfilt | SIS_RXFILTCTL_ENABLE);
	CSR_READ_4(sc, SIS_RXFILT_CTL);
}

void
sis_iff_sis(struct sis_softc *sc)
{
	struct ifnet		*ifp = &sc->arpcom.ac_if;
	struct arpcom		*ac = &sc->arpcom;
	struct ether_multi	*enm;
	struct ether_multistep	step;
	u_int32_t		h, i, maxmulti, rxfilt;
	u_int16_t		hashes[16];

	/* hash table size */
	if (sc->sis_rev >= SIS_REV_635 ||
	    sc->sis_rev == SIS_REV_900B)
		maxmulti = 16;
	else
		maxmulti = 8;

	rxfilt = CSR_READ_4(sc, SIS_RXFILT_CTL);
	if (rxfilt & SIS_RXFILTCTL_ENABLE) {
		/*
		 * Filter should be disabled to program other bits.
		 */
		CSR_WRITE_4(sc, SIS_RXFILT_CTL, rxfilt & ~SIS_RXFILTCTL_ENABLE);
		CSR_READ_4(sc, SIS_RXFILT_CTL);
	}
	rxfilt &= ~(SIS_RXFILTCTL_ALLMULTI | SIS_RXFILTCTL_ALLPHYS |
	    SIS_RXFILTCTL_BROAD);
	ifp->if_flags &= ~IFF_ALLMULTI;

	/*
	 * Always accept broadcast frames.
	 */
	rxfilt |= SIS_RXFILTCTL_BROAD;

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0 ||
	    ac->ac_multicnt > maxmulti) {
		ifp->if_flags |= IFF_ALLMULTI;
		rxfilt |= SIS_RXFILTCTL_ALLMULTI;
		if (ifp->if_flags & IFF_PROMISC)
			rxfilt |= SIS_RXFILTCTL_ALLPHYS;

		for (i = 0; i < maxmulti; i++)
			hashes[i] = ~0;
	} else {
		for (i = 0; i < maxmulti; i++)
			hashes[i] = 0;

		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			h = sis_mchash(sc, enm->enm_addrlo);

			hashes[h >> 4] |= 1 << (h & 0xf);

			ETHER_NEXT_MULTI(step, enm);
		}
	}

	for (i = 0; i < maxmulti; i++) {
		CSR_WRITE_4(sc, SIS_RXFILT_CTL, (4 + i) << 16);
		CSR_WRITE_4(sc, SIS_RXFILT_DATA, hashes[i]);
	}

	CSR_WRITE_4(sc, SIS_RXFILT_CTL, rxfilt);
	/* Turn the receive filter on. */
	CSR_WRITE_4(sc, SIS_RXFILT_CTL, rxfilt | SIS_RXFILTCTL_ENABLE);
	CSR_READ_4(sc, SIS_RXFILT_CTL);
}

void
sis_reset(struct sis_softc *sc)
{
	int			i;

	SIS_SETBIT(sc, SIS_CSR, SIS_CSR_RESET);

	for (i = 0; i < SIS_TIMEOUT; i++) {
		if (!(CSR_READ_4(sc, SIS_CSR) & SIS_CSR_RESET))
			break;
	}

	if (i == SIS_TIMEOUT)
		printf("%s: reset never completed\n", sc->sc_dev.dv_xname);

	/* Wait a little while for the chip to get its brains in order. */
	DELAY(1000);

	/*
	 * If this is a NetSemi chip, make sure to clear
	 * PME mode.
	 */
	if (sc->sis_type == SIS_TYPE_83815) {
		CSR_WRITE_4(sc, NS_CLKRUN, NS_CLKRUN_PMESTS);
		CSR_WRITE_4(sc, NS_CLKRUN, 0);
	}
}

/*
 * Probe for an SiS chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 */
int
sis_probe(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, sis_devices,
	    nitems(sis_devices)));
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
void
sis_attach(struct device *parent, struct device *self, void *aux)
{
	int			i;
	const char		*intrstr = NULL;
	struct sis_softc	*sc = (struct sis_softc *)self;
	struct pci_attach_args	*pa = aux;
	pci_chipset_tag_t	pc = pa->pa_pc;
	pci_intr_handle_t	ih;
	struct ifnet		*ifp;
	bus_size_t		size;

	sc->sis_stopped = 1;

	pci_set_powerstate(pa->pa_pc, pa->pa_tag, PCI_PMCSR_STATE_D0);

	/*
	 * Map control/status registers.
	 */

#ifdef SIS_USEIOSPACE
	if (pci_mapreg_map(pa, SIS_PCI_LOIO, PCI_MAPREG_TYPE_IO, 0,
	    &sc->sis_btag, &sc->sis_bhandle, NULL, &size, 0)) {
		printf(": can't map i/o space\n");
		return;
 	}
#else
	if (pci_mapreg_map(pa, SIS_PCI_LOMEM, PCI_MAPREG_TYPE_MEM, 0,
	    &sc->sis_btag, &sc->sis_bhandle, NULL, &size, 0)) {
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
	sc->sc_ih = pci_intr_establish(pc, ih, IPL_NET, sis_intr, sc,
	    self->dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		goto fail_1;
	}

	switch (PCI_PRODUCT(pa->pa_id)) {
	case PCI_PRODUCT_SIS_900:
		sc->sis_type = SIS_TYPE_900;
		break;
	case PCI_PRODUCT_SIS_7016:
		sc->sis_type = SIS_TYPE_7016;
		break;
	case PCI_PRODUCT_NS_DP83815:
		sc->sis_type = SIS_TYPE_83815;
		break;
	default:
		break;
	}
	sc->sis_rev = PCI_REVISION(pa->pa_class);

	/* Reset the adapter. */
	sis_reset(sc);

	if (sc->sis_type == SIS_TYPE_900 &&
	   (sc->sis_rev == SIS_REV_635 ||
	    sc->sis_rev == SIS_REV_900B)) {
		SIO_SET(SIS_CFG_RND_CNT);
		SIO_SET(SIS_CFG_PERR_DETECT);
	}

	/*
	 * Get station address from the EEPROM.
	 */
	switch (PCI_VENDOR(pa->pa_id)) {
	case PCI_VENDOR_NS:
		sc->sis_srr = CSR_READ_4(sc, NS_SRR);

		if (sc->sis_srr == NS_SRR_15C)
			printf(", DP83815C");
		else if (sc->sis_srr == NS_SRR_15D)
			printf(", DP83815D");
		else if (sc->sis_srr == NS_SRR_16A)
			printf(", DP83816A");
		else
			printf(", srr %x", sc->sis_srr);

		/*
		 * Reading the MAC address out of the EEPROM on
		 * the NatSemi chip takes a bit more work than
		 * you'd expect. The address spans 4 16-bit words,
		 * with the first word containing only a single bit.
		 * You have to shift everything over one bit to
		 * get it aligned properly. Also, the bits are
		 * stored backwards (the LSB is really the MSB,
		 * and so on) so you have to reverse them in order
		 * to get the MAC address into the form we want.
		 * Why? Who the hell knows.
		 */
		{
			u_int16_t		tmp[4];

			sis_read_eeprom(sc, (caddr_t)&tmp, NS_EE_NODEADDR,
			    4, 0);

			/* Shift everything over one bit. */
			tmp[3] = tmp[3] >> 1;
			tmp[3] |= tmp[2] << 15;
			tmp[2] = tmp[2] >> 1;
			tmp[2] |= tmp[1] << 15;
			tmp[1] = tmp[1] >> 1;
			tmp[1] |= tmp[0] << 15;

			/* Now reverse all the bits. */
			tmp[3] = letoh16(sis_reverse(tmp[3]));
			tmp[2] = letoh16(sis_reverse(tmp[2]));
			tmp[1] = letoh16(sis_reverse(tmp[1]));

			bcopy(&tmp[1], sc->arpcom.ac_enaddr,
			    ETHER_ADDR_LEN);
		}
		break;
	case PCI_VENDOR_SIS:
	default:
#if defined(__amd64__) || defined(__i386__)
		/*
		 * If this is a SiS 630E chipset with an embedded
		 * SiS 900 controller, we have to read the MAC address
		 * from the APC CMOS RAM. Our method for doing this
		 * is very ugly since we have to reach out and grab
		 * ahold of hardware for which we cannot properly
		 * allocate resources. This code is only compiled on
		 * the i386 architecture since the SiS 630E chipset
		 * is for x86 motherboards only. Note that there are
		 * a lot of magic numbers in this hack. These are
		 * taken from SiS's Linux driver. I'd like to replace
		 * them with proper symbolic definitions, but that
		 * requires some datasheets that I don't have access
		 * to at the moment.
		 */
		if (sc->sis_rev == SIS_REV_630S ||
		    sc->sis_rev == SIS_REV_630E)
			sis_read_cmos(sc, pa, (caddr_t)&sc->arpcom.ac_enaddr,
			    0x9, 6);
		else
#endif
		if (sc->sis_rev == SIS_REV_96x)
			sis_read96x_mac(sc);
		else if (sc->sis_rev == SIS_REV_635 ||
		    sc->sis_rev == SIS_REV_630ET ||
		    sc->sis_rev == SIS_REV_630EA1)
			sis_read_mac(sc, pa);
		else
			sis_read_eeprom(sc, (caddr_t)&sc->arpcom.ac_enaddr,
			    SIS_EE_NODEADDR, 3, 1);
		break;
	}

	printf(": %s, address %s\n", intrstr,
	    ether_sprintf(sc->arpcom.ac_enaddr));

	sc->sc_dmat = pa->pa_dmat;

	if (bus_dmamem_alloc(sc->sc_dmat, sizeof(struct sis_list_data),
	    PAGE_SIZE, 0, sc->sc_listseg, 1, &sc->sc_listnseg,
	    BUS_DMA_NOWAIT | BUS_DMA_ZERO) != 0) {
		printf(": can't alloc list mem\n");
		goto fail_2;
	}
	if (bus_dmamem_map(sc->sc_dmat, sc->sc_listseg, sc->sc_listnseg,
	    sizeof(struct sis_list_data), &sc->sc_listkva,
	    BUS_DMA_NOWAIT) != 0) {
		printf(": can't map list mem\n");
		goto fail_2;
	}
	if (bus_dmamap_create(sc->sc_dmat, sizeof(struct sis_list_data), 1,
	    sizeof(struct sis_list_data), 0, BUS_DMA_NOWAIT,
	    &sc->sc_listmap) != 0) {
		printf(": can't alloc list map\n");
		goto fail_2;
	}
	if (bus_dmamap_load(sc->sc_dmat, sc->sc_listmap, sc->sc_listkva,
	    sizeof(struct sis_list_data), NULL, BUS_DMA_NOWAIT) != 0) {
		printf(": can't load list map\n");
		goto fail_2;
	}
	sc->sis_ldata = (struct sis_list_data *)sc->sc_listkva;

	for (i = 0; i < SIS_RX_LIST_CNT; i++) {
		if (bus_dmamap_create(sc->sc_dmat, MCLBYTES, 1, MCLBYTES, 0,
		    BUS_DMA_NOWAIT, &sc->sis_ldata->sis_rx_list[i].map) != 0) {
			printf(": can't create rx map\n");
			goto fail_2;
		}
	}

	for (i = 0; i < SIS_TX_LIST_CNT; i++) {
		if (bus_dmamap_create(sc->sc_dmat, MCLBYTES,
		    SIS_MAXTXSEGS, MCLBYTES, 0, BUS_DMA_NOWAIT,
		    &sc->sis_ldata->sis_tx_list[i].map) != 0) {
			printf(": can't create tx map\n");
			goto fail_2;
		}
	}
	if (bus_dmamap_create(sc->sc_dmat, MCLBYTES, SIS_MAXTXSEGS,
	    MCLBYTES, 0, BUS_DMA_NOWAIT, &sc->sc_tx_sparemap) != 0) {
		printf(": can't create tx spare map\n");
		goto fail_2;
	}

	timeout_set(&sc->sis_timeout, sis_tick, sc);

	ifp = &sc->arpcom.ac_if;
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = sis_ioctl;
	ifp->if_start = sis_start;
	ifp->if_watchdog = sis_watchdog;
	ifq_init_maxlen(&ifp->if_snd, SIS_TX_LIST_CNT - 1);
	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);
	ifp->if_hardmtu = 1518; /* determined experimentally on DP83815 */

	ifp->if_capabilities = IFCAP_VLAN_MTU;

	sc->sc_mii.mii_ifp = ifp;
	sc->sc_mii.mii_readreg = sis_miibus_readreg;
	sc->sc_mii.mii_writereg = sis_miibus_writereg;
	sc->sc_mii.mii_statchg = sis_miibus_statchg;
	ifmedia_init(&sc->sc_mii.mii_media, 0, sis_ifmedia_upd,sis_ifmedia_sts);
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
	bus_space_unmap(sc->sis_btag, sc->sis_bhandle, size);
}

int
sis_activate(struct device *self, int act)
{
	struct sis_softc *sc = (struct sis_softc *)self;
	struct ifnet *ifp = &sc->arpcom.ac_if;

	switch (act) {
	case DVACT_SUSPEND:
		if (ifp->if_flags & IFF_RUNNING)
			sis_stop(sc);
		break;
	case DVACT_RESUME:
		if (ifp->if_flags & IFF_UP)
			sis_init(sc);
		break;
	}
	return (0);
}

/*
 * Initialize the TX and RX descriptors and allocate mbufs for them. Note that
 * we arrange the descriptors in a closed ring, so that the last descriptor
 * points back to the first.
 */
int
sis_ring_init(struct sis_softc *sc)
{
	struct sis_list_data	*ld;
	struct sis_ring_data	*cd;
	int			i, nexti;

	cd = &sc->sis_cdata;
	ld = sc->sis_ldata;

	for (i = 0; i < SIS_TX_LIST_CNT; i++) {
		if (i == (SIS_TX_LIST_CNT - 1))
			nexti = 0;
		else
			nexti = i + 1;
		ld->sis_tx_list[i].sis_nextdesc = &ld->sis_tx_list[nexti];
		ld->sis_tx_list[i].sis_next =
		    htole32(sc->sc_listmap->dm_segs[0].ds_addr +
		      offsetof(struct sis_list_data, sis_tx_list[nexti]));
		ld->sis_tx_list[i].sis_mbuf = NULL;
		ld->sis_tx_list[i].sis_ptr = 0;
		ld->sis_tx_list[i].sis_ctl = 0;
	}

	cd->sis_tx_prod = cd->sis_tx_cons = cd->sis_tx_cnt = 0;

	for (i = 0; i < SIS_RX_LIST_CNT; i++) {
		if (i == SIS_RX_LIST_CNT - 1)
			nexti = 0;
		else
			nexti = i + 1;
		ld->sis_rx_list[i].sis_nextdesc = &ld->sis_rx_list[nexti];
		ld->sis_rx_list[i].sis_next =
		    htole32(sc->sc_listmap->dm_segs[0].ds_addr +
		      offsetof(struct sis_list_data, sis_rx_list[nexti]));
		ld->sis_rx_list[i].sis_ctl = 0;
	}

	cd->sis_rx_prod = cd->sis_rx_cons = 0;
	if_rxr_init(&cd->sis_rx_ring, 2, SIS_RX_LIST_CNT - 1);
	sis_fill_rx_ring(sc);

	return (0);
}

void
sis_fill_rx_ring(struct sis_softc *sc)
{
	struct sis_list_data    *ld;
	struct sis_ring_data    *cd;
	u_int			slots;

	cd = &sc->sis_cdata;
	ld = sc->sis_ldata;

	for (slots = if_rxr_get(&cd->sis_rx_ring, SIS_RX_LIST_CNT);
	    slots > 0; slots--) {
		if (sis_newbuf(sc, &ld->sis_rx_list[cd->sis_rx_prod]))
			break;

		SIS_INC(cd->sis_rx_prod, SIS_RX_LIST_CNT);
	}
	if_rxr_put(&cd->sis_rx_ring, slots);
}

/*
 * Initialize an RX descriptor and attach an MBUF cluster.
 */
int
sis_newbuf(struct sis_softc *sc, struct sis_desc *c)
{
	struct mbuf		*m_new = NULL;

	if (c == NULL)
		return (EINVAL);

	m_new = MCLGETL(NULL, M_DONTWAIT, MCLBYTES);
	if (!m_new)
		return (ENOBUFS);

	m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;

	if (bus_dmamap_load_mbuf(sc->sc_dmat, c->map, m_new,
	    BUS_DMA_NOWAIT)) {
		m_free(m_new);
		return (ENOBUFS);
	}

	bus_dmamap_sync(sc->sc_dmat, c->map, 0, c->map->dm_mapsize,
	    BUS_DMASYNC_PREREAD);

	c->sis_mbuf = m_new;
	c->sis_ptr = htole32(c->map->dm_segs[0].ds_addr);

	bus_dmamap_sync(sc->sc_dmat, sc->sc_listmap,
	    ((caddr_t)c - sc->sc_listkva), sizeof(struct sis_desc),
	    BUS_DMASYNC_PREWRITE);

	c->sis_ctl = htole32(ETHER_MAX_DIX_LEN);

	bus_dmamap_sync(sc->sc_dmat, sc->sc_listmap,
	    ((caddr_t)c - sc->sc_listkva), sizeof(struct sis_desc),
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);
}

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 */
void
sis_rxeof(struct sis_softc *sc)
{
	struct mbuf_list	ml = MBUF_LIST_INITIALIZER();
	struct mbuf		*m;
	struct ifnet		*ifp;
	struct sis_desc		*cur_rx;
	int			total_len = 0;
	u_int32_t		rxstat;

	ifp = &sc->arpcom.ac_if;

	while (if_rxr_inuse(&sc->sis_cdata.sis_rx_ring) > 0) {
		cur_rx = &sc->sis_ldata->sis_rx_list[sc->sis_cdata.sis_rx_cons];
		bus_dmamap_sync(sc->sc_dmat, sc->sc_listmap,
		    ((caddr_t)cur_rx - sc->sc_listkva),
		    sizeof(struct sis_desc),
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
		if (!SIS_OWNDESC(cur_rx))
			break;

		rxstat = letoh32(cur_rx->sis_rxstat);
		m = cur_rx->sis_mbuf;
		cur_rx->sis_mbuf = NULL;
		total_len = SIS_RXBYTES(cur_rx);
		/* from here on the buffer is consumed */
		SIS_INC(sc->sis_cdata.sis_rx_cons, SIS_RX_LIST_CNT);
		if_rxr_put(&sc->sis_cdata.sis_rx_ring, 1);

		/*
		 * DP83816A sometimes produces zero-length packets
		 * shortly after initialisation.
		 */
		if (total_len == 0) {
			m_freem(m);
			continue;
		}

		/* The ethernet CRC is always included */
		total_len -= ETHER_CRC_LEN;

		/*
		 * If an error occurs, update stats, clear the
		 * status word and leave the mbuf cluster in place:
		 * it should simply get re-used next time this descriptor
	 	 * comes up in the ring. However, don't report long
		 * frames as errors since they could be VLANs.
		 */
		if (rxstat & SIS_RXSTAT_GIANT &&
		    total_len <= (ETHER_MAX_DIX_LEN - ETHER_CRC_LEN))
			rxstat &= ~SIS_RXSTAT_GIANT;
		if (SIS_RXSTAT_ERROR(rxstat)) {
			ifp->if_ierrors++;
			if (rxstat & SIS_RXSTAT_COLL)
				ifp->if_collisions++;
			m_freem(m);
			continue;
		}

		/* No errors; receive the packet. */
		bus_dmamap_sync(sc->sc_dmat, cur_rx->map, 0,
		    cur_rx->map->dm_mapsize, BUS_DMASYNC_POSTREAD);
#ifdef __STRICT_ALIGNMENT
		/*
		 * On some architectures, we do not have alignment problems,
		 * so try to allocate a new buffer for the receive ring, and
		 * pass up the one where the packet is already, saving the
		 * expensive copy done in m_devget().
		 * If we are on an architecture with alignment problems, or
		 * if the allocation fails, then use m_devget and leave the
		 * existing buffer in the receive ring.
		 */
		{
			struct mbuf *m0;
			m0 = m_devget(mtod(m, char *), total_len, ETHER_ALIGN);
			m_freem(m);
			if (m0 == NULL) {
				ifp->if_ierrors++;
				continue;
			}
			m = m0;
		}
#else
		m->m_pkthdr.len = m->m_len = total_len;
#endif

		ml_enqueue(&ml, m);
	}

	if (ifiq_input(&ifp->if_rcv, &ml))
		if_rxr_livelocked(&sc->sis_cdata.sis_rx_ring);

	sis_fill_rx_ring(sc);
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */

void
sis_txeof(struct sis_softc *sc)
{
	struct ifnet		*ifp;
	u_int32_t		idx, ctl, txstat;

	ifp = &sc->arpcom.ac_if;

	/*
	 * Go through our tx list and free mbufs for those
	 * frames that have been transmitted.
	 */
	for (idx = sc->sis_cdata.sis_tx_cons; sc->sis_cdata.sis_tx_cnt > 0;
	    sc->sis_cdata.sis_tx_cnt--, SIS_INC(idx, SIS_TX_LIST_CNT)) {
		struct sis_desc *cur_tx = &sc->sis_ldata->sis_tx_list[idx];

		bus_dmamap_sync(sc->sc_dmat, sc->sc_listmap,
		    ((caddr_t)cur_tx - sc->sc_listkva),
		    sizeof(struct sis_desc),
		    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);

		if (SIS_OWNDESC(cur_tx))
			break;

		ctl = letoh32(cur_tx->sis_ctl);

		if (ctl & SIS_CMDSTS_MORE)
			continue;

		txstat = letoh32(cur_tx->sis_txstat);

		if (!(ctl & SIS_CMDSTS_PKT_OK)) {
			ifp->if_oerrors++;
			if (txstat & SIS_TXSTAT_EXCESSCOLLS)
				ifp->if_collisions++;
			if (txstat & SIS_TXSTAT_OUTOFWINCOLL)
				ifp->if_collisions++;
		}

		ifp->if_collisions += (txstat & SIS_TXSTAT_COLLCNT) >> 16;

		if (cur_tx->map->dm_nsegs != 0) {
			bus_dmamap_t map = cur_tx->map;

			bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, map);
		}
		if (cur_tx->sis_mbuf != NULL) {
			m_freem(cur_tx->sis_mbuf);
			cur_tx->sis_mbuf = NULL;
		}
	}

	if (idx != sc->sis_cdata.sis_tx_cons) {
		/* we freed up some buffers */
		sc->sis_cdata.sis_tx_cons = idx;
		ifq_clr_oactive(&ifp->if_snd);
	}

	ifp->if_timer = (sc->sis_cdata.sis_tx_cnt == 0) ? 0 : 5;
}

void
sis_tick(void *xsc)
{
	struct sis_softc	*sc = (struct sis_softc *)xsc;
	struct mii_data		*mii;
	int			s;

	s = splnet();

	mii = &sc->sc_mii;
	mii_tick(mii);

	if (!sc->sis_link)
		sis_miibus_statchg(&sc->sc_dev);
	
	timeout_add_sec(&sc->sis_timeout, 1);

	splx(s);
}

int
sis_intr(void *arg)
{
	struct sis_softc	*sc = arg;
	struct ifnet		*ifp = &sc->arpcom.ac_if;
	u_int32_t		status;

	if (sc->sis_stopped)	/* Most likely shared interrupt */
		return (0);

	/* Reading the ISR register clears all interrupts. */
	status = CSR_READ_4(sc, SIS_ISR);
	if ((status & SIS_INTRS) == 0)
		return (0);

	if (status &
	    (SIS_ISR_TX_DESC_OK | SIS_ISR_TX_ERR |
	     SIS_ISR_TX_OK | SIS_ISR_TX_IDLE))
		sis_txeof(sc);

	if (status &
	    (SIS_ISR_RX_DESC_OK | SIS_ISR_RX_OK |
	     SIS_ISR_RX_ERR | SIS_ISR_RX_IDLE))
		sis_rxeof(sc);

	if (status & (SIS_ISR_RX_IDLE)) {
		/* consume what's there so that sis_rx_cons points
		 * to the first HW owned descriptor. */
		sis_rxeof(sc);
		/* reprogram the RX listptr */
		CSR_WRITE_4(sc, SIS_RX_LISTPTR,
		    sc->sc_listmap->dm_segs[0].ds_addr +
		    offsetof(struct sis_list_data,
		    sis_rx_list[sc->sis_cdata.sis_rx_cons]));
	}

	if (status & SIS_ISR_SYSERR)
		sis_init(sc);

	/*
	 * XXX: Re-enable RX engine every time otherwise it occasionally
	 * stops under unknown circumstances.
	 */
	SIS_SETBIT(sc, SIS_CSR, SIS_CSR_RX_ENABLE);

	if (!ifq_empty(&ifp->if_snd))
		sis_start(ifp);

	return (1);
}

/*
 * Encapsulate an mbuf chain in a descriptor by coupling the mbuf data
 * pointers to the fragment pointers.
 */
int
sis_encap(struct sis_softc *sc, struct mbuf *m_head, u_int32_t *txidx)
{
	struct sis_desc		*f = NULL;
	bus_dmamap_t		map;
	int			frag, cur, i, error;

	map = sc->sc_tx_sparemap;

	error = bus_dmamap_load_mbuf(sc->sc_dmat, map, m_head,
	    BUS_DMA_NOWAIT);
	switch (error) {
	case 0:
		break;

	case EFBIG:
		if (m_defrag(m_head, M_DONTWAIT) == 0 &&
		    bus_dmamap_load_mbuf(sc->sc_dmat, map, m_head,
		    BUS_DMA_NOWAIT) == 0)
			break;

		/* FALLTHROUGH */
	default:
		return (ENOBUFS);
	}

	if ((SIS_TX_LIST_CNT - (sc->sis_cdata.sis_tx_cnt + map->dm_nsegs)) < 2) {
		bus_dmamap_unload(sc->sc_dmat, map);
		return (ENOBUFS);
	}

	/*
 	 * Start packing the mbufs in this chain into
	 * the fragment pointers. Stop when we run out
 	 * of fragments or hit the end of the mbuf chain.
	 */
	cur = frag = *txidx;

	for (i = 0; i < map->dm_nsegs; i++) {
		f = &sc->sis_ldata->sis_tx_list[frag];
		f->sis_ctl = htole32(SIS_CMDSTS_MORE | map->dm_segs[i].ds_len);
		f->sis_ptr = htole32(map->dm_segs[i].ds_addr);
		if (i != 0)
			f->sis_ctl |= htole32(SIS_CMDSTS_OWN);
		cur = frag;
		SIS_INC(frag, SIS_TX_LIST_CNT);
	}

	bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
	    BUS_DMASYNC_PREWRITE);

	sc->sis_ldata->sis_tx_list[cur].sis_mbuf = m_head;
	sc->sis_ldata->sis_tx_list[cur].sis_ctl &= ~htole32(SIS_CMDSTS_MORE);
	sc->sis_ldata->sis_tx_list[*txidx].sis_ctl |= htole32(SIS_CMDSTS_OWN);
	sc->sis_cdata.sis_tx_cnt += map->dm_nsegs;
	*txidx = frag;

	bus_dmamap_sync(sc->sc_dmat, sc->sc_listmap,
	    offsetof(struct sis_list_data, sis_tx_list[0]),
	    sizeof(struct sis_desc) * SIS_TX_LIST_CNT,  
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);

	return (0);
}

/*
 * Main transmit routine. To avoid having to do mbuf copies, we put pointers
 * to the mbuf data regions directly in the transmit lists. We also save a
 * copy of the pointers since the transmit list fragment pointers are
 * physical addresses.
 */

void
sis_start(struct ifnet *ifp)
{
	struct sis_softc	*sc;
	struct mbuf		*m_head = NULL;
	u_int32_t		idx, queued = 0;

	sc = ifp->if_softc;

	if (!sc->sis_link)
		return;

	idx = sc->sis_cdata.sis_tx_prod;

	if (ifq_is_oactive(&ifp->if_snd))
		return;

	while(sc->sis_ldata->sis_tx_list[idx].sis_mbuf == NULL) {
		m_head = ifq_deq_begin(&ifp->if_snd);
		if (m_head == NULL)
			break;

		if (sis_encap(sc, m_head, &idx)) {
			ifq_deq_rollback(&ifp->if_snd, m_head);
			ifq_set_oactive(&ifp->if_snd);
			break;
		}

		/* now we are committed to transmit the packet */
		ifq_deq_commit(&ifp->if_snd, m_head);

		queued++;

		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m_head, BPF_DIRECTION_OUT);
#endif
	}

	if (queued) {
		/* Transmit */
		sc->sis_cdata.sis_tx_prod = idx;
		SIS_SETBIT(sc, SIS_CSR, SIS_CSR_TX_ENABLE);

		/*
		 * Set a timeout in case the chip goes out to lunch.
		 */
		ifp->if_timer = 5;
	}
}

void
sis_init(void *xsc)
{
	struct sis_softc	*sc = (struct sis_softc *)xsc;
	struct ifnet		*ifp = &sc->arpcom.ac_if;
	struct mii_data		*mii;
	int			s;

	s = splnet();

	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */
	sis_stop(sc);

	/*
	 * Reset the chip to a known state.
	 */
	sis_reset(sc);

#if NS_IHR_DELAY > 0
	/* Configure interrupt holdoff register. */
	if (sc->sis_type == SIS_TYPE_83815 && sc->sis_srr == NS_SRR_16A)
		CSR_WRITE_4(sc, NS_IHR, NS_IHR_VALUE);
#endif

	mii = &sc->sc_mii;

	/* Set MAC address */
	if (sc->sis_type == SIS_TYPE_83815) {
		CSR_WRITE_4(sc, SIS_RXFILT_CTL, NS_FILTADDR_PAR0);
		CSR_WRITE_4(sc, SIS_RXFILT_DATA,
		    htole16(((u_int16_t *)sc->arpcom.ac_enaddr)[0]));
		CSR_WRITE_4(sc, SIS_RXFILT_CTL, NS_FILTADDR_PAR1);
		CSR_WRITE_4(sc, SIS_RXFILT_DATA,
		    htole16(((u_int16_t *)sc->arpcom.ac_enaddr)[1]));
		CSR_WRITE_4(sc, SIS_RXFILT_CTL, NS_FILTADDR_PAR2);
		CSR_WRITE_4(sc, SIS_RXFILT_DATA,
		    htole16(((u_int16_t *)sc->arpcom.ac_enaddr)[2]));
	} else {
		CSR_WRITE_4(sc, SIS_RXFILT_CTL, SIS_FILTADDR_PAR0);
		CSR_WRITE_4(sc, SIS_RXFILT_DATA,
		    htole16(((u_int16_t *)sc->arpcom.ac_enaddr)[0]));
		CSR_WRITE_4(sc, SIS_RXFILT_CTL, SIS_FILTADDR_PAR1);
		CSR_WRITE_4(sc, SIS_RXFILT_DATA,
		    htole16(((u_int16_t *)sc->arpcom.ac_enaddr)[1]));
		CSR_WRITE_4(sc, SIS_RXFILT_CTL, SIS_FILTADDR_PAR2);
		CSR_WRITE_4(sc, SIS_RXFILT_DATA,
		    htole16(((u_int16_t *)sc->arpcom.ac_enaddr)[2]));
	}

	/* Init circular TX/RX lists. */
	if (sis_ring_init(sc) != 0) {
		printf("%s: initialization failed: no memory for rx buffers\n",
		    sc->sc_dev.dv_xname);
		sis_stop(sc);
		splx(s);
		return;
	}

        /*
	 * Page 78 of the DP83815 data sheet (september 2002 version)
	 * recommends the following register settings "for optimum
	 * performance." for rev 15C.  The driver from NS also sets
	 * the PHY_CR register for later versions.
	 *
	 * This resolves an issue with tons of errors in AcceptPerfectMatch
	 * (non-IFF_PROMISC) mode.
	 */
	if (sc->sis_type == SIS_TYPE_83815 && sc->sis_srr <= NS_SRR_15D) {
		CSR_WRITE_4(sc, NS_PHY_PAGE, 0x0001);
		CSR_WRITE_4(sc, NS_PHY_CR, 0x189C);
		/* set val for c2 */
		CSR_WRITE_4(sc, NS_PHY_TDATA, 0x0000);
		/* load/kill c2 */
		CSR_WRITE_4(sc, NS_PHY_DSPCFG, 0x5040);
		/* raise SD off, from 4 to c */
		CSR_WRITE_4(sc, NS_PHY_SDCFG, 0x008C);
		CSR_WRITE_4(sc, NS_PHY_PAGE, 0);
	}

	/*
	 * Program promiscuous mode and multicast filters.
	 */
	sis_iff(sc);

	/*
	 * Load the address of the RX and TX lists.
	 */
	CSR_WRITE_4(sc, SIS_RX_LISTPTR, sc->sc_listmap->dm_segs[0].ds_addr +
	    offsetof(struct sis_list_data, sis_rx_list[0]));
	CSR_WRITE_4(sc, SIS_TX_LISTPTR, sc->sc_listmap->dm_segs[0].ds_addr +
	    offsetof(struct sis_list_data, sis_tx_list[0]));

	/* SIS_CFG_EDB_MASTER_EN indicates the EDB bus is used instead of
	 * the PCI bus. When this bit is set, the Max DMA Burst Size
	 * for TX/RX DMA should be no larger than 16 double words.
	 */
	if (CSR_READ_4(sc, SIS_CFG) & SIS_CFG_EDB_MASTER_EN)
		CSR_WRITE_4(sc, SIS_RX_CFG, SIS_RXCFG64);
	else
		CSR_WRITE_4(sc, SIS_RX_CFG, SIS_RXCFG256);

	/* Accept Long Packets for VLAN support */
	SIS_SETBIT(sc, SIS_RX_CFG, SIS_RXCFG_RX_JABBER);

	/*
	 * Assume 100Mbps link, actual MAC configuration is done
	 * after getting a valid link.
	 */
	CSR_WRITE_4(sc, SIS_TX_CFG, SIS_TXCFG_100);

	/*
	 * Enable interrupts.
	 */
	CSR_WRITE_4(sc, SIS_IMR, SIS_INTRS);
	CSR_WRITE_4(sc, SIS_IER, 1);

	/* Clear MAC disable. */
	SIS_CLRBIT(sc, SIS_CSR, SIS_CSR_TX_DISABLE | SIS_CSR_RX_DISABLE);

	sc->sis_link = 0;
	mii_mediachg(mii);

	sc->sis_stopped = 0;
	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);

	splx(s);

	timeout_add_sec(&sc->sis_timeout, 1);
}

/*
 * Set media options.
 */
int
sis_ifmedia_upd(struct ifnet *ifp)
{
	struct sis_softc	*sc;
	struct mii_data		*mii;

	sc = ifp->if_softc;

	mii = &sc->sc_mii;
	if (mii->mii_instance) {
		struct mii_softc	*miisc;
		LIST_FOREACH(miisc, &mii->mii_phys, mii_list)
			mii_phy_reset(miisc);
	}
	mii_mediachg(mii);

	return (0);
}

/*
 * Report current media status.
 */
void
sis_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct sis_softc	*sc;
	struct mii_data		*mii;

	sc = ifp->if_softc;

	mii = &sc->sc_mii;
	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

int
sis_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct sis_softc	*sc = ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *) data;
	struct mii_data		*mii;
	int			s, error = 0;

	s = splnet();

	switch(command) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			sis_init(sc);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				sis_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				sis_stop(sc);
		}
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		mii = &sc->sc_mii;
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		break;

	case SIOCGIFRXR:
		error = if_rxr_ioctl((struct if_rxrinfo *)ifr->ifr_data,
		    NULL, MCLBYTES, &sc->sis_cdata.sis_rx_ring);
		break;

	default:
		error = ether_ioctl(ifp, &sc->arpcom, command, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			sis_iff(sc);
		error = 0;
	}

	splx(s);
	return(error);
}

void
sis_watchdog(struct ifnet *ifp)
{
	struct sis_softc	*sc;
	int			s;

	sc = ifp->if_softc;

	if (sc->sis_stopped)
		return;

	ifp->if_oerrors++;
	printf("%s: watchdog timeout\n", sc->sc_dev.dv_xname);

	s = splnet();
	sis_init(sc);

	if (!ifq_empty(&ifp->if_snd))
		sis_start(ifp);

	splx(s);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
void
sis_stop(struct sis_softc *sc)
{
	int			i;
	struct ifnet		*ifp;

	if (sc->sis_stopped)
		return;

	ifp = &sc->arpcom.ac_if;
	ifp->if_timer = 0;

	timeout_del(&sc->sis_timeout);

	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
	sc->sis_stopped = 1;

	CSR_WRITE_4(sc, SIS_IER, 0);
	CSR_WRITE_4(sc, SIS_IMR, 0);
	CSR_READ_4(sc, SIS_ISR); /* clear any interrupts already pending */
	SIS_SETBIT(sc, SIS_CSR, SIS_CSR_TX_DISABLE | SIS_CSR_RX_DISABLE);
	DELAY(1000);
	CSR_WRITE_4(sc, SIS_TX_LISTPTR, 0);
	CSR_WRITE_4(sc, SIS_RX_LISTPTR, 0);

	sc->sis_link = 0;

	/*
	 * Free data in the RX lists.
	 */
	for (i = 0; i < SIS_RX_LIST_CNT; i++) {
		if (sc->sis_ldata->sis_rx_list[i].map->dm_nsegs != 0) {
			bus_dmamap_t map = sc->sis_ldata->sis_rx_list[i].map;

			bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
			    BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(sc->sc_dmat, map);
		}
		if (sc->sis_ldata->sis_rx_list[i].sis_mbuf != NULL) {
			m_freem(sc->sis_ldata->sis_rx_list[i].sis_mbuf);
			sc->sis_ldata->sis_rx_list[i].sis_mbuf = NULL;
		}
		bzero(&sc->sis_ldata->sis_rx_list[i],
		    sizeof(struct sis_desc) - sizeof(bus_dmamap_t));
	}

	/*
	 * Free the TX list buffers.
	 */
	for (i = 0; i < SIS_TX_LIST_CNT; i++) {
		if (sc->sis_ldata->sis_tx_list[i].map->dm_nsegs != 0) {
			bus_dmamap_t map = sc->sis_ldata->sis_tx_list[i].map;

			bus_dmamap_sync(sc->sc_dmat, map, 0, map->dm_mapsize,
			    BUS_DMASYNC_POSTWRITE);
			bus_dmamap_unload(sc->sc_dmat, map);
		}
		if (sc->sis_ldata->sis_tx_list[i].sis_mbuf != NULL) {
			m_freem(sc->sis_ldata->sis_tx_list[i].sis_mbuf);
			sc->sis_ldata->sis_tx_list[i].sis_mbuf = NULL;
		}
		bzero(&sc->sis_ldata->sis_tx_list[i],
		    sizeof(struct sis_desc) - sizeof(bus_dmamap_t));
	}
}
