/*	$OpenBSD: if_tl.c,v 1.80 2025/07/15 13:40:02 jsg Exp $	*/

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
 *
 * $FreeBSD: src/sys/pci/if_tl.c,v 1.64 2001/02/06 10:11:48 phk Exp $
 */

/*
 * Texas Instruments ThunderLAN driver for FreeBSD 2.2.6 and 3.x.
 * Supports many Compaq PCI NICs based on the ThunderLAN ethernet controller,
 * the National Semiconductor DP83840A physical interface and the
 * Microchip Technology 24Cxx series serial EEPROM.
 *
 * Written using the following four documents:
 *
 * Texas Instruments ThunderLAN Programmer's Guide (www.ti.com)
 * National Semiconductor DP83840A data sheet (www.national.com)
 * Microchip Technology 24C02C data sheet (www.microchip.com)
 * Micro Linear ML6692 100BaseTX only PHY data sheet (www.microlinear.com)
 * 
 * Written by Bill Paul <wpaul@ctr.columbia.edu>
 * Electrical Engineering Department
 * Columbia University, New York City
 */

/*
 * Some notes about the ThunderLAN:
 *
 * The ThunderLAN controller is a single chip containing PCI controller
 * logic, approximately 3K of on-board SRAM, a LAN controller, and media
 * independent interface (MII) bus. The MII allows the ThunderLAN chip to
 * control up to 32 different physical interfaces (PHYs). The ThunderLAN
 * also has a built-in 10baseT PHY, allowing a single ThunderLAN controller
 * to act as a complete ethernet interface.
 *
 * Other PHYs may be attached to the ThunderLAN; the Compaq 10/100 cards
 * use a National Semiconductor DP83840A PHY that supports 10 or 100Mb/sec
 * in full or half duplex. Some of the Compaq Deskpro machines use a
 * Level 1 LXT970 PHY with the same capabilities. Certain Olicom adapters
 * use a Micro Linear ML6692 100BaseTX only PHY, which can be used in
 * concert with the ThunderLAN's internal PHY to provide full 10/100
 * support. This is cheaper than using a standalone external PHY for both
 * 10/100 modes and letting the ThunderLAN's internal PHY go to waste.
 * A serial EEPROM is also attached to the ThunderLAN chip to provide
 * power-up default register settings and for storing the adapter's
 * station address. Although not supported by this driver, the ThunderLAN
 * chip can also be connected to token ring PHYs.
 *
 * The ThunderLAN has a set of registers which can be used to issue
 * commands, acknowledge interrupts, and to manipulate other internal
 * registers on its DIO bus. The primary registers can be accessed
 * using either programmed I/O (inb/outb) or via PCI memory mapping,
 * depending on how the card is configured during the PCI probing
 * phase. It is even possible to have both PIO and memory mapped
 * access turned on at the same time.
 * 
 * Frame reception and transmission with the ThunderLAN chip is done
 * using frame 'lists.' A list structure looks more or less like this:
 *
 * struct tl_frag {
 *	u_int32_t		fragment_address;
 *	u_int32_t		fragment_size;
 * };
 * struct tl_list {
 *	u_int32_t		forward_pointer;
 *	u_int16_t		cstat;
 *	u_int16_t		frame_size;
 *	struct tl_frag		fragments[10];
 * };
 *
 * The forward pointer in the list header can be either a 0 or the address
 * of another list, which allows several lists to be linked together. Each
 * list contains up to 10 fragment descriptors. This means the chip allows
 * ethernet frames to be broken up into up to 10 chunks for transfer to
 * and from the SRAM. Note that the forward pointer and fragment buffer
 * addresses are physical memory addresses, not virtual. Note also that
 * a single ethernet frame can not span lists: if the host wants to
 * transmit a frame and the frame data is split up over more than 10
 * buffers, the frame has to collapsed before it can be transmitted.
 *
 * To receive frames, the driver sets up a number of lists and populates
 * the fragment descriptors, then it sends an RX GO command to the chip.
 * When a frame is received, the chip will DMA it into the memory regions
 * specified by the fragment descriptors and then trigger an RX 'end of
 * frame interrupt' when done. The driver may choose to use only one
 * fragment per list; this may result in slightly less efficient use
 * of memory in exchange for improving performance.
 *
 * To transmit frames, the driver again sets up lists and fragment
 * descriptors, only this time the buffers contain frame data that
 * is to be DMA'ed into the chip instead of out of it. Once the chip
 * has transferred the data into its on-board SRAM, it will trigger a
 * TX 'end of frame' interrupt. It will also generate an 'end of channel'
 * interrupt when it reaches the end of the list.
 */

/*
 * Some notes about this driver:
 *
 * The ThunderLAN chip provides a couple of different ways to organize
 * reception, transmission and interrupt handling. The simplest approach
 * is to use one list each for transmission and reception. In this mode,
 * the ThunderLAN will generate two interrupts for every received frame
 * (one RX EOF and one RX EOC) and two for each transmitted frame (one
 * TX EOF and one TX EOC). This may make the driver simpler but it hurts
 * performance to have to handle so many interrupts.
 *
 * Initially I wanted to create a circular list of receive buffers so
 * that the ThunderLAN chip would think there was an infinitely long
 * receive channel and never deliver an RXEOC interrupt. However this
 * doesn't work correctly under heavy load: while the manual says the
 * chip will trigger an RXEOF interrupt each time a frame is copied into
 * memory, you can't count on the chip waiting around for you to acknowledge
 * the interrupt before it starts trying to DMA the next frame. The result
 * is that the chip might traverse the entire circular list and then wrap
 * around before you have a chance to do anything about it. Consequently,
 * the receive list is terminated (with a 0 in the forward pointer in the
 * last element). Each time an RXEOF interrupt arrives, the used list
 * is shifted to the end of the list. This gives the appearance of an
 * infinitely large RX chain so long as the driver doesn't fall behind
 * the chip and allow all of the lists to be filled up.
 *
 * If all the lists are filled, the adapter will deliver an RX 'end of
 * channel' interrupt when it hits the 0 forward pointer at the end of
 * the chain. The RXEOC handler then cleans out the RX chain and resets
 * the list head pointer in the ch_parm register and restarts the receiver.
 *
 * For frame transmission, it is possible to program the ThunderLAN's
 * transmit interrupt threshold so that the chip can acknowledge multiple
 * lists with only a single TX EOF interrupt. This allows the driver to
 * queue several frames in one shot, and only have to handle a total
 * two interrupts (one TX EOF and one TX EOC) no matter how many frames
 * are transmitted. Frame transmission is done directly out of the
 * mbufs passed to the tl_start() routine via the interface send queue.
 * The driver simply sets up the fragment descriptors in the transmit
 * lists to point to the mbuf data regions and sends a TX GO command.
 *
 * Note that since the RX and TX lists themselves are always used
 * only by the driver, the are malloc()ed once at driver initialization
 * time and never free()ed.
 *
 * Also, in order to remain as platform independent as possible, this
 * driver uses memory mapped register access to manipulate the card
 * as opposed to programmed I/O. This avoids the use of the inb/outb
 * (and related) instructions which are specific to the i386 platform.
 *
 * Using these techniques, this driver achieves very high performance
 * by minimizing the amount of interrupts generated during large
 * transfers and by completely avoiding buffer copies. Frame transfer
 * to and from the ThunderLAN chip is performed entirely by the chip
 * itself thereby reducing the load on the host CPU.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/device.h>
#include <sys/timeout.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net/if_media.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <uvm/uvm_extern.h>              /* for vtophys */
#define	VTOPHYS(v)	vtophys((vaddr_t)(v))

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

/*
 * Default to using PIO register access mode to pacify certain
 * laptop docking stations with built-in ThunderLAN chips that
 * don't seem to handle memory mapped mode properly.
 */
#define TL_USEIOSPACE

#include <dev/pci/if_tlreg.h>
#include <dev/mii/tlphyvar.h>

const struct tl_products tl_prods[] = {
	{ PCI_VENDOR_COMPAQ, PCI_PRODUCT_COMPAQ_N100TX, TLPHY_MEDIA_NO_10_T },
	{ PCI_VENDOR_COMPAQ, PCI_PRODUCT_COMPAQ_N10T, TLPHY_MEDIA_10_5 },
	{ PCI_VENDOR_COMPAQ, PCI_PRODUCT_COMPAQ_INTNF3P, TLPHY_MEDIA_10_2 },
	{ PCI_VENDOR_COMPAQ, PCI_PRODUCT_COMPAQ_INTPL100TX, TLPHY_MEDIA_10_5|TLPHY_MEDIA_NO_10_T },
	{ PCI_VENDOR_COMPAQ, PCI_PRODUCT_COMPAQ_DPNET100TX, TLPHY_MEDIA_10_5|TLPHY_MEDIA_NO_10_T },
	{ PCI_VENDOR_COMPAQ, PCI_PRODUCT_COMPAQ_DP4000, TLPHY_MEDIA_10_5|TLPHY_MEDIA_NO_10_T },
	{ PCI_VENDOR_COMPAQ, PCI_PRODUCT_COMPAQ_NF3P_BNC, TLPHY_MEDIA_10_2 },
	{ PCI_VENDOR_COMPAQ, PCI_PRODUCT_COMPAQ_NF3P, TLPHY_MEDIA_10_5 },
	{ PCI_VENDOR_TI, PCI_PRODUCT_TI_TLAN, 0 },
	{ 0, 0, 0 }
};

int tl_probe(struct device *, void *, void *);
void tl_attach(struct device *, struct device *, void *);
void tl_wait_up(void *);
int tl_intvec_rxeoc(void *, u_int32_t);
int tl_intvec_txeoc(void *, u_int32_t);
int tl_intvec_txeof(void *, u_int32_t);
int tl_intvec_rxeof(void *, u_int32_t);
int tl_intvec_adchk(void *, u_int32_t);
int tl_intvec_netsts(void *, u_int32_t);

int tl_newbuf(struct tl_softc *, struct tl_chain_onefrag *);
void tl_stats_update(void *);
int tl_encap(struct tl_softc *, struct tl_chain *, struct mbuf *);

int tl_intr(void *);
void tl_start(struct ifnet *);
int tl_ioctl(struct ifnet *, u_long, caddr_t);
void tl_init(void *);
void tl_stop(struct tl_softc *);
void tl_watchdog(struct ifnet *);
int tl_ifmedia_upd(struct ifnet *);
void tl_ifmedia_sts(struct ifnet *, struct ifmediareq *);

u_int8_t tl_eeprom_putbyte(struct tl_softc *, int);
u_int8_t tl_eeprom_getbyte(struct tl_softc *, int, u_int8_t *);
int tl_read_eeprom(struct tl_softc *, caddr_t, int, int);

void tl_mii_sync(struct tl_softc *);
void tl_mii_send(struct tl_softc *, u_int32_t, int);
int tl_mii_readreg(struct tl_softc *, struct tl_mii_frame *);
int tl_mii_writereg(struct tl_softc *, struct tl_mii_frame *);
int tl_miibus_readreg(struct device *, int, int);
void tl_miibus_writereg(struct device *, int, int, int);
void tl_miibus_statchg(struct device *);

void tl_setmode(struct tl_softc *, uint64_t);
int tl_calchash(u_int8_t *);
void tl_iff(struct tl_softc *);
void tl_setfilt(struct tl_softc *, caddr_t, int);
void tl_softreset(struct tl_softc *, int);
void tl_hardreset(struct device *);
int tl_list_rx_init(struct tl_softc *);
int tl_list_tx_init(struct tl_softc *);

u_int8_t tl_dio_read8(struct tl_softc *, int);
u_int16_t tl_dio_read16(struct tl_softc *, int);
u_int32_t tl_dio_read32(struct tl_softc *, int);
void tl_dio_write8(struct tl_softc *, int, int);
void tl_dio_write16(struct tl_softc *, int, int);
void tl_dio_write32(struct tl_softc *, int, int);
void tl_dio_setbit(struct tl_softc *, int, int);
void tl_dio_clrbit(struct tl_softc *, int, int);
void tl_dio_setbit16(struct tl_softc *, int, int);
void tl_dio_clrbit16(struct tl_softc *, int, int);

u_int8_t
tl_dio_read8(struct tl_softc *sc, int reg)
{
	CSR_WRITE_2(sc, TL_DIO_ADDR, reg);
	return(CSR_READ_1(sc, TL_DIO_DATA + (reg & 3)));
}

u_int16_t
tl_dio_read16(struct tl_softc *sc, int reg)
{
	CSR_WRITE_2(sc, TL_DIO_ADDR, reg);
	return(CSR_READ_2(sc, TL_DIO_DATA + (reg & 3)));
}

u_int32_t
tl_dio_read32(struct tl_softc *sc, int reg)
{
	CSR_WRITE_2(sc, TL_DIO_ADDR, reg);
	return(CSR_READ_4(sc, TL_DIO_DATA + (reg & 3)));
}

void
tl_dio_write8(struct tl_softc *sc, int reg, int val)
{
	CSR_WRITE_2(sc, TL_DIO_ADDR, reg);
	CSR_WRITE_1(sc, TL_DIO_DATA + (reg & 3), val);
}

void
tl_dio_write16(struct tl_softc *sc, int reg, int val)
{
	CSR_WRITE_2(sc, TL_DIO_ADDR, reg);
	CSR_WRITE_2(sc, TL_DIO_DATA + (reg & 3), val);
}

void
tl_dio_write32(struct tl_softc *sc, int reg, int val)
{
	CSR_WRITE_2(sc, TL_DIO_ADDR, reg);
	CSR_WRITE_4(sc, TL_DIO_DATA + (reg & 3), val);
}

void
tl_dio_setbit(struct tl_softc *sc, int reg, int bit)
{
	u_int8_t			f;

	CSR_WRITE_2(sc, TL_DIO_ADDR, reg);
	f = CSR_READ_1(sc, TL_DIO_DATA + (reg & 3));
	f |= bit;
	CSR_WRITE_1(sc, TL_DIO_DATA + (reg & 3), f);
}

void
tl_dio_clrbit(struct tl_softc *sc, int reg, int bit)
{
	u_int8_t			f;

	CSR_WRITE_2(sc, TL_DIO_ADDR, reg);
	f = CSR_READ_1(sc, TL_DIO_DATA + (reg & 3));
	f &= ~bit;
	CSR_WRITE_1(sc, TL_DIO_DATA + (reg & 3), f);
}

void
tl_dio_setbit16(struct tl_softc *sc, int reg, int bit)
{
	u_int16_t			f;

	CSR_WRITE_2(sc, TL_DIO_ADDR, reg);
	f = CSR_READ_2(sc, TL_DIO_DATA + (reg & 3));
	f |= bit;
	CSR_WRITE_2(sc, TL_DIO_DATA + (reg & 3), f);
}

void
tl_dio_clrbit16(struct tl_softc *sc, int reg, int bit)
{
	u_int16_t			f;

	CSR_WRITE_2(sc, TL_DIO_ADDR, reg);
	f = CSR_READ_2(sc, TL_DIO_DATA + (reg & 3));
	f &= ~bit;
	CSR_WRITE_2(sc, TL_DIO_DATA + (reg & 3), f);
}

/*
 * Send an instruction or address to the EEPROM, check for ACK.
 */
u_int8_t
tl_eeprom_putbyte(struct tl_softc *sc, int byte)
{
	int			i, ack = 0;

	/*
	 * Make sure we're in TX mode.
	 */
	tl_dio_setbit(sc, TL_NETSIO, TL_SIO_ETXEN);

	/*
	 * Feed in each bit and strobe the clock.
	 */
	for (i = 0x80; i; i >>= 1) {
		if (byte & i)
			tl_dio_setbit(sc, TL_NETSIO, TL_SIO_EDATA);
		else
			tl_dio_clrbit(sc, TL_NETSIO, TL_SIO_EDATA);
		DELAY(1);
		tl_dio_setbit(sc, TL_NETSIO, TL_SIO_ECLOK);
		DELAY(1);
		tl_dio_clrbit(sc, TL_NETSIO, TL_SIO_ECLOK);
	}

	/*
	 * Turn off TX mode.
	 */
	tl_dio_clrbit(sc, TL_NETSIO, TL_SIO_ETXEN);

	/*
	 * Check for ack.
	 */
	tl_dio_setbit(sc, TL_NETSIO, TL_SIO_ECLOK);
	ack = tl_dio_read8(sc, TL_NETSIO) & TL_SIO_EDATA;
	tl_dio_clrbit(sc, TL_NETSIO, TL_SIO_ECLOK);

	return(ack);
}

/*
 * Read a byte of data stored in the EEPROM at address 'addr.'
 */
u_int8_t
tl_eeprom_getbyte(struct tl_softc *sc, int addr, u_int8_t *dest)
{
	int			i;
	u_int8_t		byte = 0;

	tl_dio_write8(sc, TL_NETSIO, 0);

	EEPROM_START;

	/*
	 * Send write control code to EEPROM.
	 */
	if (tl_eeprom_putbyte(sc, EEPROM_CTL_WRITE)) {
		printf("%s: failed to send write command, status: %x\n",
			sc->sc_dev.dv_xname, tl_dio_read8(sc, TL_NETSIO));
		return(1);
	}

	/*
	 * Send address of byte we want to read.
	 */
	if (tl_eeprom_putbyte(sc, addr)) {
		printf("%s: failed to send address, status: %x\n",
			sc->sc_dev.dv_xname, tl_dio_read8(sc, TL_NETSIO));
		return(1);
	}

	EEPROM_STOP;
	EEPROM_START;
	/*
	 * Send read control code to EEPROM.
	 */
	if (tl_eeprom_putbyte(sc, EEPROM_CTL_READ)) {
		printf("%s: failed to send write command, status: %x\n",
			sc->sc_dev.dv_xname, tl_dio_read8(sc, TL_NETSIO));
		return(1);
	}

	/*
	 * Start reading bits from EEPROM.
	 */
	tl_dio_clrbit(sc, TL_NETSIO, TL_SIO_ETXEN);
	for (i = 0x80; i; i >>= 1) {
		tl_dio_setbit(sc, TL_NETSIO, TL_SIO_ECLOK);
		DELAY(1);
		if (tl_dio_read8(sc, TL_NETSIO) & TL_SIO_EDATA)
			byte |= i;
		tl_dio_clrbit(sc, TL_NETSIO, TL_SIO_ECLOK);
		DELAY(1);
	}

	EEPROM_STOP;

	/*
	 * No ACK generated for read, so just return byte.
	 */

	*dest = byte;

	return(0);
}

/*
 * Read a sequence of bytes from the EEPROM.
 */
int
tl_read_eeprom(struct tl_softc *sc, caddr_t dest, int off, int cnt)
{
	int			err = 0, i;
	u_int8_t		byte = 0;

	for (i = 0; i < cnt; i++) {
		err = tl_eeprom_getbyte(sc, off + i, &byte);
		if (err)
			break;
		*(dest + i) = byte;
	}

	return(err ? 1 : 0);
}

void
tl_mii_sync(struct tl_softc *sc)
{
	int			i;

	tl_dio_clrbit(sc, TL_NETSIO, TL_SIO_MTXEN);

	for (i = 0; i < 32; i++) {
		tl_dio_setbit(sc, TL_NETSIO, TL_SIO_MCLK);
		tl_dio_clrbit(sc, TL_NETSIO, TL_SIO_MCLK);
	}
}

void
tl_mii_send(struct tl_softc *sc, u_int32_t bits, int cnt)
{
	int			i;

	for (i = (0x1 << (cnt - 1)); i; i >>= 1) {
		tl_dio_clrbit(sc, TL_NETSIO, TL_SIO_MCLK);
		if (bits & i)
			tl_dio_setbit(sc, TL_NETSIO, TL_SIO_MDATA);
		else
			tl_dio_clrbit(sc, TL_NETSIO, TL_SIO_MDATA);
		tl_dio_setbit(sc, TL_NETSIO, TL_SIO_MCLK);
	}
}

int
tl_mii_readreg(struct tl_softc *sc, struct tl_mii_frame *frame)
{
	int			i, ack, s;
	int			minten = 0;

	s = splnet();

	tl_mii_sync(sc);

	/*
	 * Set up frame for RX.
	 */
	frame->mii_stdelim = TL_MII_STARTDELIM;
	frame->mii_opcode = TL_MII_READOP;
	frame->mii_turnaround = 0;
	frame->mii_data = 0;
	
	/*
	 * Turn off MII interrupt by forcing MINTEN low.
	 */
	minten = tl_dio_read8(sc, TL_NETSIO) & TL_SIO_MINTEN;
	if (minten)
		tl_dio_clrbit(sc, TL_NETSIO, TL_SIO_MINTEN);

	/*
 	 * Turn on data xmit.
	 */
	tl_dio_setbit(sc, TL_NETSIO, TL_SIO_MTXEN);

	/*
	 * Send command/address info.
	 */
	tl_mii_send(sc, frame->mii_stdelim, 2);
	tl_mii_send(sc, frame->mii_opcode, 2);
	tl_mii_send(sc, frame->mii_phyaddr, 5);
	tl_mii_send(sc, frame->mii_regaddr, 5);

	/*
	 * Turn off xmit.
	 */
	tl_dio_clrbit(sc, TL_NETSIO, TL_SIO_MTXEN);

	/* Idle bit */
	tl_dio_clrbit(sc, TL_NETSIO, TL_SIO_MCLK);
	tl_dio_setbit(sc, TL_NETSIO, TL_SIO_MCLK);

	/* Check for ack */
	tl_dio_clrbit(sc, TL_NETSIO, TL_SIO_MCLK);
	ack = tl_dio_read8(sc, TL_NETSIO) & TL_SIO_MDATA;

	/* Complete the cycle */
	tl_dio_setbit(sc, TL_NETSIO, TL_SIO_MCLK);

	/*
	 * Now try reading data bits. If the ack failed, we still
	 * need to clock through 16 cycles to keep the PHYs in sync.
	 */
	if (ack) {
		for(i = 0; i < 16; i++) {
			tl_dio_clrbit(sc, TL_NETSIO, TL_SIO_MCLK);
			tl_dio_setbit(sc, TL_NETSIO, TL_SIO_MCLK);
		}
		goto fail;
	}

	for (i = 0x8000; i; i >>= 1) {
		tl_dio_clrbit(sc, TL_NETSIO, TL_SIO_MCLK);
		if (!ack) {
			if (tl_dio_read8(sc, TL_NETSIO) & TL_SIO_MDATA)
				frame->mii_data |= i;
		}
		tl_dio_setbit(sc, TL_NETSIO, TL_SIO_MCLK);
	}

fail:

	tl_dio_setbit(sc, TL_NETSIO, TL_SIO_MCLK);
	tl_dio_clrbit(sc, TL_NETSIO, TL_SIO_MCLK);

	/* Reenable interrupts */
	if (minten)
		tl_dio_setbit(sc, TL_NETSIO, TL_SIO_MINTEN);

	splx(s);

	if (ack)
		return(1);
	return(0);
}

int
tl_mii_writereg(struct tl_softc *sc, struct tl_mii_frame *frame)
{
	int			s;
	int			minten;

	tl_mii_sync(sc);

	s = splnet();
	/*
	 * Set up frame for TX.
	 */

	frame->mii_stdelim = TL_MII_STARTDELIM;
	frame->mii_opcode = TL_MII_WRITEOP;
	frame->mii_turnaround = TL_MII_TURNAROUND;
	
	/*
	 * Turn off MII interrupt by forcing MINTEN low.
	 */
	minten = tl_dio_read8(sc, TL_NETSIO) & TL_SIO_MINTEN;
	if (minten)
		tl_dio_clrbit(sc, TL_NETSIO, TL_SIO_MINTEN);

	/*
 	 * Turn on data output.
	 */
	tl_dio_setbit(sc, TL_NETSIO, TL_SIO_MTXEN);

	tl_mii_send(sc, frame->mii_stdelim, 2);
	tl_mii_send(sc, frame->mii_opcode, 2);
	tl_mii_send(sc, frame->mii_phyaddr, 5);
	tl_mii_send(sc, frame->mii_regaddr, 5);
	tl_mii_send(sc, frame->mii_turnaround, 2);
	tl_mii_send(sc, frame->mii_data, 16);

	tl_dio_setbit(sc, TL_NETSIO, TL_SIO_MCLK);
	tl_dio_clrbit(sc, TL_NETSIO, TL_SIO_MCLK);

	/*
	 * Turn off xmit.
	 */
	tl_dio_clrbit(sc, TL_NETSIO, TL_SIO_MTXEN);

	/* Reenable interrupts */
	if (minten)
		tl_dio_setbit(sc, TL_NETSIO, TL_SIO_MINTEN);

	splx(s);

	return(0);
}

int
tl_miibus_readreg(struct device *dev, int phy, int reg)
{
	struct tl_softc *sc = (struct tl_softc *)dev;
	struct tl_mii_frame	frame;

	bzero(&frame, sizeof(frame));

	frame.mii_phyaddr = phy;
	frame.mii_regaddr = reg;
	tl_mii_readreg(sc, &frame);

	return(frame.mii_data);
}

void
tl_miibus_writereg(struct device *dev, int phy, int reg, int data)
{
	struct tl_softc *sc = (struct tl_softc *)dev;
	struct tl_mii_frame	frame;

	bzero(&frame, sizeof(frame));

	frame.mii_phyaddr = phy;
	frame.mii_regaddr = reg;
	frame.mii_data = data;

	tl_mii_writereg(sc, &frame);
}

void
tl_miibus_statchg(struct device *dev)
{
	struct tl_softc *sc = (struct tl_softc *)dev;

	if ((sc->sc_mii.mii_media_active & IFM_GMASK) == IFM_FDX)
		tl_dio_setbit(sc, TL_NETCMD, TL_CMD_DUPLEX);
	else
		tl_dio_clrbit(sc, TL_NETCMD, TL_CMD_DUPLEX);
}

/*
 * Set modes for bitrate devices.
 */
void
tl_setmode(struct tl_softc *sc, uint64_t media)
{
	if (IFM_SUBTYPE(media) == IFM_10_5)
		tl_dio_setbit(sc, TL_ACOMMIT, TL_AC_MTXD1);
	if (IFM_SUBTYPE(media) == IFM_10_T) {
		tl_dio_clrbit(sc, TL_ACOMMIT, TL_AC_MTXD1);
		if ((media & IFM_GMASK) == IFM_FDX) {
			tl_dio_clrbit(sc, TL_ACOMMIT, TL_AC_MTXD3);
			tl_dio_setbit(sc, TL_NETCMD, TL_CMD_DUPLEX);
		} else {
			tl_dio_setbit(sc, TL_ACOMMIT, TL_AC_MTXD3);
			tl_dio_clrbit(sc, TL_NETCMD, TL_CMD_DUPLEX);
		}
	}
}

/*
 * Calculate the hash of a MAC address for programming the multicast hash
 * table.  This hash is simply the address split into 6-bit chunks
 * XOR'd, e.g.
 * byte: 000000|00 1111|1111 22|222222|333333|33 4444|4444 55|555555
 * bit:  765432|10 7654|3210 76|543210|765432|10 7654|3210 76|543210
 * Bytes 0-2 and 3-5 are symmetrical, so are folded together.  Then
 * the folded 24-bit value is split into 6-bit portions and XOR'd.
 */
int
tl_calchash(u_int8_t *addr)
{
	int			t;

	t = (addr[0] ^ addr[3]) << 16 | (addr[1] ^ addr[4]) << 8 |
		(addr[2] ^ addr[5]);
	return ((t >> 18) ^ (t >> 12) ^ (t >> 6) ^ t) & 0x3f;
}

/*
 * The ThunderLAN has a perfect MAC address filter in addition to
 * the multicast hash filter. The perfect filter can be programmed
 * with up to four MAC addresses. The first one is always used to
 * hold the station address, which leaves us free to use the other
 * three for multicast addresses.
 */
void
tl_setfilt(struct tl_softc *sc, caddr_t addr, int slot)
{
	int			i;
	u_int16_t		regaddr;

	regaddr = TL_AREG0_B5 + (slot * ETHER_ADDR_LEN);

	for (i = 0; i < ETHER_ADDR_LEN; i++)
		tl_dio_write8(sc, regaddr + i, *(addr + i));
}

/*
 * XXX In FreeBSD 3.0, multicast addresses are managed using a doubly
 * linked list. This is fine, except addresses are added from the head
 * end of the list. We want to arrange for 224.0.0.1 (the "all hosts")
 * group to always be in the perfect filter, but as more groups are added,
 * the 224.0.0.1 entry (which is always added first) gets pushed down
 * the list and ends up at the tail. So after 3 or 4 multicast groups
 * are added, the all-hosts entry gets pushed out of the perfect filter
 * and into the hash table.
 *
 * Because the multicast list is a doubly-linked list as opposed to a
 * circular queue, we don't have the ability to just grab the tail of
 * the list and traverse it backwards. Instead, we have to traverse
 * the list once to find the tail, then traverse it again backwards to
 * update the multicast filter.
 */
void
tl_iff(struct tl_softc *sc)
{
	struct ifnet		*ifp = &sc->arpcom.ac_if;
	struct arpcom		*ac = &sc->arpcom;
	struct ether_multistep step;
	struct ether_multi *enm;
	u_int32_t		hashes[2];
	int			h = 0;

	tl_dio_clrbit(sc, TL_NETCMD, (TL_CMD_CAF | TL_CMD_NOBRX));
	bzero(hashes, sizeof(hashes));
	ifp->if_flags &= ~IFF_ALLMULTI;

	if (ifp->if_flags & IFF_PROMISC || ac->ac_multirangecnt > 0) {
		ifp->if_flags |= IFF_ALLMULTI;
		if (ifp->if_flags & IFF_PROMISC)
			tl_dio_setbit(sc, TL_NETCMD, TL_CMD_CAF);
		else
			hashes[0] = hashes[1] = 0xffffffff;
	} else {
		ETHER_FIRST_MULTI(step, ac, enm);
		while (enm != NULL) {
			h = tl_calchash(enm->enm_addrlo);

			if (h < 32)
				hashes[0] |= (1 << h);
			else
				hashes[1] |= (1 << (h - 32));

			ETHER_NEXT_MULTI(step, enm);
		}
	}

	tl_dio_write32(sc, TL_HASH1, hashes[0]);
	tl_dio_write32(sc, TL_HASH2, hashes[1]);
}

/*
 * This routine is recommended by the ThunderLAN manual to insure that
 * the internal PHY is powered up correctly. It also recommends a one
 * second pause at the end to 'wait for the clocks to start' but in my
 * experience this isn't necessary.
 */
void
tl_hardreset(struct device *dev)
{
	struct tl_softc		*sc = (struct tl_softc *)dev;
	int			i;
	u_int16_t		flags;

	flags = BMCR_LOOP|BMCR_ISO|BMCR_PDOWN;

	for (i =0 ; i < MII_NPHY; i++)
		tl_miibus_writereg(dev, i, MII_BMCR, flags);

	tl_miibus_writereg(dev, 31, MII_BMCR, BMCR_ISO);
	tl_mii_sync(sc);
	while(tl_miibus_readreg(dev, 31, MII_BMCR) & BMCR_RESET);

	DELAY(5000);
}

void
tl_softreset(struct tl_softc *sc, int internal)
{
        u_int32_t               cmd, dummy, i;

        /* Assert the adapter reset bit. */
	CMD_SET(sc, TL_CMD_ADRST);
        /* Turn off interrupts */
	CMD_SET(sc, TL_CMD_INTSOFF);

	/* First, clear the stats registers. */
	for (i = 0; i < 5; i++)
		dummy = tl_dio_read32(sc, TL_TXGOODFRAMES);

        /* Clear Areg and Hash registers */
	for (i = 0; i < 8; i++)
		tl_dio_write32(sc, TL_AREG0_B5, 0x00000000);

        /*
	 * Set up Netconfig register. Enable one channel and
	 * one fragment mode.
	 */
	tl_dio_setbit16(sc, TL_NETCONFIG, TL_CFG_ONECHAN|TL_CFG_ONEFRAG);
	if (internal && !sc->tl_bitrate) {
		tl_dio_setbit16(sc, TL_NETCONFIG, TL_CFG_PHYEN);
	} else {
		tl_dio_clrbit16(sc, TL_NETCONFIG, TL_CFG_PHYEN);
	}

	/* Handle cards with bitrate devices. */
	if (sc->tl_bitrate)
		tl_dio_setbit16(sc, TL_NETCONFIG, TL_CFG_BITRATE);

	/*
	 * Load adapter irq pacing timer and tx threshold.
	 * We make the transmit threshold 1 initially but we may
	 * change that later.
	 */
	cmd = CSR_READ_4(sc, TL_HOSTCMD);
	cmd |= TL_CMD_NES;
	cmd &= ~(TL_CMD_RT|TL_CMD_EOC|TL_CMD_ACK_MASK|TL_CMD_CHSEL_MASK);
	CMD_PUT(sc, cmd | (TL_CMD_LDTHR | TX_THR));
	CMD_PUT(sc, cmd | (TL_CMD_LDTMR | 0x00000003));

        /* Unreset the MII */
	tl_dio_setbit(sc, TL_NETSIO, TL_SIO_NMRST);

	/* Take the adapter out of reset */
	tl_dio_setbit(sc, TL_NETCMD, TL_CMD_NRESET|TL_CMD_NWRAP);

	/* Wait for things to settle down a little. */
	DELAY(500);
}

/*
 * Initialize the transmit lists.
 */
int
tl_list_tx_init(struct tl_softc *sc)
{
	struct tl_chain_data	*cd;
	struct tl_list_data	*ld;
	int			i;

	cd = &sc->tl_cdata;
	ld = sc->tl_ldata;
	for (i = 0; i < TL_TX_LIST_CNT; i++) {
		cd->tl_tx_chain[i].tl_ptr = &ld->tl_tx_list[i];
		if (i == (TL_TX_LIST_CNT - 1))
			cd->tl_tx_chain[i].tl_next = NULL;
		else
			cd->tl_tx_chain[i].tl_next = &cd->tl_tx_chain[i + 1];
	}

	cd->tl_tx_free = &cd->tl_tx_chain[0];
	cd->tl_tx_tail = cd->tl_tx_head = NULL;
	sc->tl_txeoc = 1;

	return(0);
}

/*
 * Initialize the RX lists and allocate mbufs for them.
 */
int
tl_list_rx_init(struct tl_softc *sc)
{
	struct tl_chain_data	*cd;
	struct tl_list_data	*ld;
	int			i;

	cd = &sc->tl_cdata;
	ld = sc->tl_ldata;

	for (i = 0; i < TL_RX_LIST_CNT; i++) {
		cd->tl_rx_chain[i].tl_ptr =
			(struct tl_list_onefrag *)&ld->tl_rx_list[i];
		if (tl_newbuf(sc, &cd->tl_rx_chain[i]) == ENOBUFS)
			return(ENOBUFS);
		if (i == (TL_RX_LIST_CNT - 1)) {
			cd->tl_rx_chain[i].tl_next = NULL;
			ld->tl_rx_list[i].tlist_fptr = 0;
		} else {
			cd->tl_rx_chain[i].tl_next = &cd->tl_rx_chain[i + 1];
			ld->tl_rx_list[i].tlist_fptr =
					VTOPHYS(&ld->tl_rx_list[i + 1]);
		}
	}

	cd->tl_rx_head = &cd->tl_rx_chain[0];
	cd->tl_rx_tail = &cd->tl_rx_chain[TL_RX_LIST_CNT - 1];

	return(0);
}

int
tl_newbuf(struct tl_softc *sc, struct tl_chain_onefrag *c)
{
	struct mbuf		*m_new = NULL;

	MGETHDR(m_new, M_DONTWAIT, MT_DATA);
	if (m_new == NULL) {
		return(ENOBUFS);
	}

	MCLGET(m_new, M_DONTWAIT);
	if (!(m_new->m_flags & M_EXT)) {
		m_freem(m_new);
		return(ENOBUFS);
	}

#ifdef __alpha__
	m_new->m_data += 2;
#endif

	c->tl_mbuf = m_new;
	c->tl_next = NULL;
	c->tl_ptr->tlist_frsize = MCLBYTES;
	c->tl_ptr->tlist_fptr = 0;
	c->tl_ptr->tl_frag.tlist_dadr = VTOPHYS(mtod(m_new, caddr_t));
	c->tl_ptr->tl_frag.tlist_dcnt = MCLBYTES;
	c->tl_ptr->tlist_cstat = TL_CSTAT_READY;

	return(0);
}
/*
 * Interrupt handler for RX 'end of frame' condition (EOF). This
 * tells us that a full ethernet frame has been captured and we need
 * to handle it.
 *
 * Reception is done using 'lists' which consist of a header and a
 * series of 10 data count/data address pairs that point to buffers.
 * Initially you're supposed to create a list, populate it with pointers
 * to buffers, then load the physical address of the list into the
 * ch_parm register. The adapter is then supposed to DMA the received
 * frame into the buffers for you.
 *
 * To make things as fast as possible, we have the chip DMA directly
 * into mbufs. This saves us from having to do a buffer copy: we can
 * just hand the mbufs directly to the network stack. Once the frame
 * has been sent on its way, the 'list' structure is assigned a new
 * buffer and moved to the end of the RX chain. As long we stay
 * ahead of the chip, it will always think it has an endless receive
 * channel.
 *
 * If we happen to fall behind and the chip manages to fill up all of
 * the buffers, it will generate an end of channel interrupt and wait
 * for us to empty the chain and restart the receiver.
 */
int
tl_intvec_rxeof(void *xsc, u_int32_t type)
{
	struct tl_softc		*sc;
	int			r = 0, total_len = 0;
	struct ether_header	*eh;
	struct mbuf		*m;
	struct mbuf_list	ml = MBUF_LIST_INITIALIZER();
	struct ifnet		*ifp;
	struct tl_chain_onefrag	*cur_rx;

	sc = xsc;
	ifp = &sc->arpcom.ac_if;

	while(sc->tl_cdata.tl_rx_head != NULL) {
		cur_rx = sc->tl_cdata.tl_rx_head;
		if (!(cur_rx->tl_ptr->tlist_cstat & TL_CSTAT_FRAMECMP))
			break;
		r++;
		sc->tl_cdata.tl_rx_head = cur_rx->tl_next;
		m = cur_rx->tl_mbuf;
		total_len = cur_rx->tl_ptr->tlist_frsize;

		if (tl_newbuf(sc, cur_rx) == ENOBUFS) {
			ifp->if_ierrors++;
			cur_rx->tl_ptr->tlist_frsize = MCLBYTES;
			cur_rx->tl_ptr->tlist_cstat = TL_CSTAT_READY;
			cur_rx->tl_ptr->tl_frag.tlist_dcnt = MCLBYTES;
			continue;
		}

		sc->tl_cdata.tl_rx_tail->tl_ptr->tlist_fptr =
						VTOPHYS(cur_rx->tl_ptr);
		sc->tl_cdata.tl_rx_tail->tl_next = cur_rx;
		sc->tl_cdata.tl_rx_tail = cur_rx;

		eh = mtod(m, struct ether_header *);

		/*
		 * Note: when the ThunderLAN chip is in 'capture all
		 * frames' mode, it will receive its own transmissions.
		 * We drop don't need to process our own transmissions,
		 * so we drop them here and continue.
		 */
		/*if (ifp->if_flags & IFF_PROMISC && */
		if (!bcmp(eh->ether_shost, sc->arpcom.ac_enaddr,
		 					ETHER_ADDR_LEN)) {
				m_freem(m);
				continue;
		}

		m->m_pkthdr.len = m->m_len = total_len;
		ml_enqueue(&ml, m);
	}

	if_input(ifp, &ml);

	return(r);
}

/*
 * The RX-EOC condition hits when the ch_parm address hasn't been
 * initialized or the adapter reached a list with a forward pointer
 * of 0 (which indicates the end of the chain). In our case, this means
 * the card has hit the end of the receive buffer chain and we need to
 * empty out the buffers and shift the pointer back to the beginning again.
 */
int
tl_intvec_rxeoc(void *xsc, u_int32_t type)
{
	struct tl_softc		*sc;
	int			r;
	struct tl_chain_data	*cd;

	sc = xsc;
	cd = &sc->tl_cdata;

	/* Flush out the receive queue and ack RXEOF interrupts. */
	r = tl_intvec_rxeof(xsc, type);
	CMD_PUT(sc, TL_CMD_ACK | r | (type & ~(0x00100000)));
	r = 1;
	cd->tl_rx_head = &cd->tl_rx_chain[0];
	cd->tl_rx_tail = &cd->tl_rx_chain[TL_RX_LIST_CNT - 1];
	CSR_WRITE_4(sc, TL_CH_PARM, VTOPHYS(sc->tl_cdata.tl_rx_head->tl_ptr));
	r |= (TL_CMD_GO|TL_CMD_RT);
	return(r);
}

int
tl_intvec_txeof(void *xsc, u_int32_t type)
{
	struct tl_softc		*sc;
	int			r = 0;
	struct tl_chain		*cur_tx;

	sc = xsc;

	/*
	 * Go through our tx list and free mbufs for those
	 * frames that have been sent.
	 */
	while (sc->tl_cdata.tl_tx_head != NULL) {
		cur_tx = sc->tl_cdata.tl_tx_head;
		if (!(cur_tx->tl_ptr->tlist_cstat & TL_CSTAT_FRAMECMP))
			break;
		sc->tl_cdata.tl_tx_head = cur_tx->tl_next;

		r++;
		m_freem(cur_tx->tl_mbuf);
		cur_tx->tl_mbuf = NULL;

		cur_tx->tl_next = sc->tl_cdata.tl_tx_free;
		sc->tl_cdata.tl_tx_free = cur_tx;
		if (!cur_tx->tl_ptr->tlist_fptr)
			break;
	}

	return(r);
}

/*
 * The transmit end of channel interrupt. The adapter triggers this
 * interrupt to tell us it hit the end of the current transmit list.
 *
 * A note about this: it's possible for a condition to arise where
 * tl_start() may try to send frames between TXEOF and TXEOC interrupts.
 * You have to avoid this since the chip expects things to go in a
 * particular order: transmit, acknowledge TXEOF, acknowledge TXEOC.
 * When the TXEOF handler is called, it will free all of the transmitted
 * frames and reset the tx_head pointer to NULL. However, a TXEOC
 * interrupt should be received and acknowledged before any more frames
 * are queued for transmission. If tl_start() is called after TXEOF
 * resets the tx_head pointer but _before_ the TXEOC interrupt arrives,
 * it could attempt to issue a transmit command prematurely.
 *
 * To guard against this, tl_start() will only issue transmit commands
 * if the tl_txeoc flag is set, and only the TXEOC interrupt handler
 * can set this flag once tl_start() has cleared it.
 */
int
tl_intvec_txeoc(void *xsc, u_int32_t type)
{
	struct tl_softc		*sc;
	struct ifnet		*ifp;
	u_int32_t		cmd;

	sc = xsc;
	ifp = &sc->arpcom.ac_if;

	/* Clear the timeout timer. */
	ifp->if_timer = 0;

	if (sc->tl_cdata.tl_tx_head == NULL) {
		ifq_clr_oactive(&ifp->if_snd);
		sc->tl_cdata.tl_tx_tail = NULL;
		sc->tl_txeoc = 1;
	} else {
		sc->tl_txeoc = 0;
		/* First we have to ack the EOC interrupt. */
		CMD_PUT(sc, TL_CMD_ACK | 0x00000001 | type);
		/* Then load the address of the next TX list. */
		CSR_WRITE_4(sc, TL_CH_PARM,
		    VTOPHYS(sc->tl_cdata.tl_tx_head->tl_ptr));
		/* Restart TX channel. */
		cmd = CSR_READ_4(sc, TL_HOSTCMD);
		cmd &= ~TL_CMD_RT;
		cmd |= TL_CMD_GO|TL_CMD_INTSON;
		CMD_PUT(sc, cmd);
		return(0);
	}

	return(1);
}

int
tl_intvec_adchk(void *xsc, u_int32_t type)
{
	struct tl_softc		*sc;

	sc = xsc;

	if (type)
		printf("%s: adapter check: %x\n", sc->sc_dev.dv_xname,
			(unsigned int)CSR_READ_4(sc, TL_CH_PARM));

	tl_softreset(sc, 1);
	tl_stop(sc);
	tl_init(sc);
	CMD_SET(sc, TL_CMD_INTSON);

	return(0);
}

int
tl_intvec_netsts(void *xsc, u_int32_t type)
{
	struct tl_softc		*sc;
	u_int16_t		netsts;

	sc = xsc;

	netsts = tl_dio_read16(sc, TL_NETSTS);
	tl_dio_write16(sc, TL_NETSTS, netsts);

	printf("%s: network status: %x\n", sc->sc_dev.dv_xname, netsts);

	return(1);
}

int
tl_intr(void *xsc)
{
	struct tl_softc		*sc;
	struct ifnet		*ifp;
	int			r = 0;
	u_int32_t		type = 0;
	u_int16_t		ints = 0;
	u_int8_t		ivec = 0;

	sc = xsc;

	/* Disable interrupts */
	ints = CSR_READ_2(sc, TL_HOST_INT);
	CSR_WRITE_2(sc, TL_HOST_INT, ints);
	type = (ints << 16) & 0xFFFF0000;
	ivec = (ints & TL_VEC_MASK) >> 5;
	ints = (ints & TL_INT_MASK) >> 2;

	ifp = &sc->arpcom.ac_if;

	switch(ints) {
	case (TL_INTR_INVALID):
		/* Re-enable interrupts but don't ack this one. */
		CMD_PUT(sc, type);
		r = 0;
		break;
	case (TL_INTR_TXEOF):
		r = tl_intvec_txeof((void *)sc, type);
		break;
	case (TL_INTR_TXEOC):
		r = tl_intvec_txeoc((void *)sc, type);
		break;
	case (TL_INTR_STATOFLOW):
		tl_stats_update(sc);
		r = 1;
		break;
	case (TL_INTR_RXEOF):
		r = tl_intvec_rxeof((void *)sc, type);
		break;
	case (TL_INTR_DUMMY):
		printf("%s: got a dummy interrupt\n", sc->sc_dev.dv_xname);
		r = 1;
		break;
	case (TL_INTR_ADCHK):
		if (ivec)
			r = tl_intvec_adchk((void *)sc, type);
		else
			r = tl_intvec_netsts((void *)sc, type);
		break;
	case (TL_INTR_RXEOC):
		r = tl_intvec_rxeoc((void *)sc, type);
		break;
	default:
		printf("%s: bogus interrupt type\n", sc->sc_dev.dv_xname);
		break;
	}

	/* Re-enable interrupts */
	if (r) {
		CMD_PUT(sc, TL_CMD_ACK | r | type);
	}

	if (!ifq_empty(&ifp->if_snd))
		tl_start(ifp);

	return r;
}

void
tl_stats_update(void *xsc)
{
	struct tl_softc		*sc;
	struct ifnet		*ifp;
	struct tl_stats		tl_stats;
	u_int32_t		*p;
	int			s;

	s = splnet();

	bzero(&tl_stats, sizeof(struct tl_stats));

	sc = xsc;
	ifp = &sc->arpcom.ac_if;

	p = (u_int32_t *)&tl_stats;

	CSR_WRITE_2(sc, TL_DIO_ADDR, TL_TXGOODFRAMES|TL_DIO_ADDR_INC);
	*p++ = CSR_READ_4(sc, TL_DIO_DATA);
	*p++ = CSR_READ_4(sc, TL_DIO_DATA);
	*p++ = CSR_READ_4(sc, TL_DIO_DATA);
	*p++ = CSR_READ_4(sc, TL_DIO_DATA);
	*p++ = CSR_READ_4(sc, TL_DIO_DATA);

	ifp->if_collisions += tl_stats.tl_tx_single_collision +
				tl_stats.tl_tx_multi_collision;
	ifp->if_ierrors += tl_stats.tl_crc_errors + tl_stats.tl_code_errors +
			    tl_rx_overrun(tl_stats);
	ifp->if_oerrors += tl_tx_underrun(tl_stats);

	if (tl_tx_underrun(tl_stats)) {
		u_int8_t	tx_thresh;
		tx_thresh = tl_dio_read8(sc, TL_ACOMMIT) & TL_AC_TXTHRESH;
		if (tx_thresh != TL_AC_TXTHRESH_WHOLEPKT) {
			tx_thresh >>= 4;
			tx_thresh++;
			tl_dio_clrbit(sc, TL_ACOMMIT, TL_AC_TXTHRESH);
			tl_dio_setbit(sc, TL_ACOMMIT, tx_thresh << 4);
		}
	}

	timeout_add_sec(&sc->tl_stats_tmo, 1);

	if (!sc->tl_bitrate)
		mii_tick(&sc->sc_mii);

	splx(s);
}

/*
 * Encapsulate an mbuf chain in a list by coupling the mbuf data
 * pointers to the fragment pointers.
 */
int
tl_encap(struct tl_softc *sc, struct tl_chain *c, struct mbuf *m_head)
{
	int			frag = 0;
	struct tl_frag		*f = NULL;
	int			total_len;
	struct mbuf		*m;

	/*
 	 * Start packing the mbufs in this chain into
	 * the fragment pointers. Stop when we run out
 	 * of fragments or hit the end of the mbuf chain.
	 */
	m = m_head;
	total_len = 0;

	for (m = m_head, frag = 0; m != NULL; m = m->m_next) {
		if (m->m_len != 0) {
			if (frag == TL_MAXFRAGS)
				break;
			total_len+= m->m_len;
			c->tl_ptr->tl_frag[frag].tlist_dadr =
				VTOPHYS(mtod(m, vaddr_t));
			c->tl_ptr->tl_frag[frag].tlist_dcnt = m->m_len;
			frag++;
		}
	}

	/*
	 * Handle special cases.
	 * Special case #1: we used up all 10 fragments, but
	 * we have more mbufs left in the chain. Copy the
	 * data into an mbuf cluster. Note that we don't
	 * bother clearing the values in the other fragment
	 * pointers/counters; it wouldn't gain us anything,
	 * and would waste cycles.
	 */
	if (m != NULL) {
		struct mbuf		*m_new = NULL;

		MGETHDR(m_new, M_DONTWAIT, MT_DATA);
		if (m_new == NULL)
			return(1);
		if (m_head->m_pkthdr.len > MHLEN) {
			MCLGET(m_new, M_DONTWAIT);
			if (!(m_new->m_flags & M_EXT)) {
				m_freem(m_new);
				return(1);
			}
		}
		m_copydata(m_head, 0, m_head->m_pkthdr.len,	
					mtod(m_new, caddr_t));
		m_new->m_pkthdr.len = m_new->m_len = m_head->m_pkthdr.len;
		m_freem(m_head);
		m_head = m_new;
		f = &c->tl_ptr->tl_frag[0];
		f->tlist_dadr = VTOPHYS(mtod(m_new, caddr_t));
		f->tlist_dcnt = total_len = m_new->m_len;
		frag = 1;
	}

	/*
	 * Special case #2: the frame is smaller than the minimum
	 * frame size. We have to pad it to make the chip happy.
	 */
	if (total_len < TL_MIN_FRAMELEN) {
		f = &c->tl_ptr->tl_frag[frag];
		f->tlist_dcnt = TL_MIN_FRAMELEN - total_len;
		f->tlist_dadr = VTOPHYS(&sc->tl_ldata->tl_pad);
		total_len += f->tlist_dcnt;
		frag++;
	}

	c->tl_mbuf = m_head;
	c->tl_ptr->tl_frag[frag - 1].tlist_dcnt |= TL_LAST_FRAG;
	c->tl_ptr->tlist_frsize = total_len;
	c->tl_ptr->tlist_cstat = TL_CSTAT_READY;
	c->tl_ptr->tlist_fptr = 0;

	return(0);
}

/*
 * Main transmit routine. To avoid having to do mbuf copies, we put pointers
 * to the mbuf data regions directly in the transmit lists. We also save a
 * copy of the pointers since the transmit list fragment pointers are
 * physical addresses.
 */
void
tl_start(struct ifnet *ifp)
{
	struct tl_softc		*sc;
	struct mbuf		*m_head = NULL;
	u_int32_t		cmd;
	struct tl_chain		*prev = NULL, *cur_tx = NULL, *start_tx;

	sc = ifp->if_softc;

	/*
	 * Check for an available queue slot. If there are none,
	 * punt.
	 */
	if (sc->tl_cdata.tl_tx_free == NULL) {
		ifq_set_oactive(&ifp->if_snd);
		return;
	}

	start_tx = sc->tl_cdata.tl_tx_free;

	while(sc->tl_cdata.tl_tx_free != NULL) {
		m_head = ifq_dequeue(&ifp->if_snd);
		if (m_head == NULL)
			break;

		/* Pick a chain member off the free list. */
		cur_tx = sc->tl_cdata.tl_tx_free;
		sc->tl_cdata.tl_tx_free = cur_tx->tl_next;

		cur_tx->tl_next = NULL;

		/* Pack the data into the list. */
		tl_encap(sc, cur_tx, m_head);

		/* Chain it together */
		if (prev != NULL) {
			prev->tl_next = cur_tx;
			prev->tl_ptr->tlist_fptr = VTOPHYS(cur_tx->tl_ptr);
		}
		prev = cur_tx;

		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, cur_tx->tl_mbuf,
			    BPF_DIRECTION_OUT);
#endif
	}

	/*
	 * If there are no packets queued, bail.
	 */
	if (cur_tx == NULL)
		return;

	/*
	 * That's all we can stands, we can't stands no more.
	 * If there are no other transfers pending, then issue the
	 * TX GO command to the adapter to start things moving.
	 * Otherwise, just leave the data in the queue and let
	 * the EOF/EOC interrupt handler send.
	 */
	if (sc->tl_cdata.tl_tx_head == NULL) {
		sc->tl_cdata.tl_tx_head = start_tx;
		sc->tl_cdata.tl_tx_tail = cur_tx;

		if (sc->tl_txeoc) {
			sc->tl_txeoc = 0;
			CSR_WRITE_4(sc, TL_CH_PARM, VTOPHYS(start_tx->tl_ptr));
			cmd = CSR_READ_4(sc, TL_HOSTCMD);
			cmd &= ~TL_CMD_RT;
			cmd |= TL_CMD_GO|TL_CMD_INTSON;
			CMD_PUT(sc, cmd);
		}
	} else {
		sc->tl_cdata.tl_tx_tail->tl_next = start_tx;
		sc->tl_cdata.tl_tx_tail = cur_tx;
	}

	/*
	 * Set a timeout in case the chip goes out to lunch.
	 */
	ifp->if_timer = 10;
}

void
tl_init(void *xsc)
{
	struct tl_softc		*sc = xsc;
	struct ifnet		*ifp = &sc->arpcom.ac_if;
        int			s;

	s = splnet();

	/*
	 * Cancel pending I/O.
	 */
	tl_stop(sc);

	/* Initialize TX FIFO threshold */
	tl_dio_clrbit(sc, TL_ACOMMIT, TL_AC_TXTHRESH);
	tl_dio_setbit(sc, TL_ACOMMIT, TL_AC_TXTHRESH_16LONG);

	/* Set PCI burst size */
	tl_dio_write8(sc, TL_BSIZEREG, TL_RXBURST_16LONG|TL_TXBURST_16LONG);

	tl_dio_write16(sc, TL_MAXRX, MCLBYTES);

	/* Init our MAC address */
	tl_setfilt(sc, (caddr_t)&sc->arpcom.ac_enaddr, 0);

	/* Program promiscuous mode and multicast filters. */
	tl_iff(sc);

	/* Init circular RX list. */
	if (tl_list_rx_init(sc) == ENOBUFS) {
		printf("%s: initialization failed: no memory for rx buffers\n",
			sc->sc_dev.dv_xname);
		tl_stop(sc);
		splx(s);
		return;
	}

	/* Init TX pointers. */
	tl_list_tx_init(sc);

	/* Enable PCI interrupts. */
	CMD_SET(sc, TL_CMD_INTSON);

	/* Load the address of the rx list */
	CMD_SET(sc, TL_CMD_RT);
	CSR_WRITE_4(sc, TL_CH_PARM, VTOPHYS(&sc->tl_ldata->tl_rx_list[0]));

	if (!sc->tl_bitrate)
		mii_mediachg(&sc->sc_mii);
	else
		tl_ifmedia_upd(ifp);

	/* Send the RX go command */
	CMD_SET(sc, TL_CMD_GO|TL_CMD_NES|TL_CMD_RT);

	splx(s);

	/* Start the stats update counter */
	timeout_set(&sc->tl_stats_tmo, tl_stats_update, sc);
	timeout_add_sec(&sc->tl_stats_tmo, 1);
	timeout_set(&sc->tl_wait_tmo, tl_wait_up, sc);
	timeout_add_sec(&sc->tl_wait_tmo, 2);
}

/*
 * Set media options.
 */
int
tl_ifmedia_upd(struct ifnet *ifp)
{
	struct tl_softc *sc = ifp->if_softc;

	if (sc->tl_bitrate)
		tl_setmode(sc, sc->ifmedia.ifm_media);
	else
		mii_mediachg(&sc->sc_mii);

	return(0);
}

/*
 * Report current media status.
 */
void
tl_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	struct tl_softc		*sc;
	struct mii_data		*mii;

	sc = ifp->if_softc;
	mii = &sc->sc_mii;

	ifmr->ifm_active = IFM_ETHER;
	if (sc->tl_bitrate) {
		if (tl_dio_read8(sc, TL_ACOMMIT) & TL_AC_MTXD1)
			ifmr->ifm_active = IFM_ETHER|IFM_10_5;
		else
			ifmr->ifm_active = IFM_ETHER|IFM_10_T;
		if (tl_dio_read8(sc, TL_ACOMMIT) & TL_AC_MTXD3)
			ifmr->ifm_active |= IFM_HDX;
		else
			ifmr->ifm_active |= IFM_FDX;
		return;
	} else {
		mii_pollstat(mii);
		ifmr->ifm_active = mii->mii_media_active;
		ifmr->ifm_status = mii->mii_media_status;
	}
}

int
tl_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct tl_softc		*sc = ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *) data;
	int			s, error = 0;

	s = splnet();

	switch(command) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		if (!(ifp->if_flags & IFF_RUNNING))
			tl_init(sc);
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING)
				error = ENETRESET;
			else
				tl_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				tl_stop(sc);
		}
		break;

	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		if (sc->tl_bitrate)
			error = ifmedia_ioctl(ifp, ifr, &sc->ifmedia, command);
		else
			error = ifmedia_ioctl(ifp, ifr,
			    &sc->sc_mii.mii_media, command);
		break;

	default:
		error = ether_ioctl(ifp, &sc->arpcom, command, data);
	}

	if (error == ENETRESET) {
		if (ifp->if_flags & IFF_RUNNING)
			tl_iff(sc);
		error = 0;
	}

	splx(s);
	return(error);
}

void
tl_watchdog(struct ifnet *ifp)
{
	struct tl_softc		*sc;

	sc = ifp->if_softc;

	printf("%s: device timeout\n", sc->sc_dev.dv_xname);

	ifp->if_oerrors++;

	tl_softreset(sc, 1);
	tl_init(sc);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
void
tl_stop(struct tl_softc *sc)
{
	int			i;
	struct ifnet		*ifp;

	ifp = &sc->arpcom.ac_if;

	/* Stop the stats updater. */
	timeout_del(&sc->tl_stats_tmo);
	timeout_del(&sc->tl_wait_tmo);

	/* Stop the transmitter */
	CMD_CLR(sc, TL_CMD_RT);
	CMD_SET(sc, TL_CMD_STOP);
	CSR_WRITE_4(sc, TL_CH_PARM, 0);

	/* Stop the receiver */
	CMD_SET(sc, TL_CMD_RT);
	CMD_SET(sc, TL_CMD_STOP);
	CSR_WRITE_4(sc, TL_CH_PARM, 0);

	/*
	 * Disable host interrupts.
	 */
	CMD_SET(sc, TL_CMD_INTSOFF);

	/*
	 * Clear list pointer.
	 */
	CSR_WRITE_4(sc, TL_CH_PARM, 0);

	/*
	 * Free the RX lists.
	 */
	for (i = 0; i < TL_RX_LIST_CNT; i++) {
		if (sc->tl_cdata.tl_rx_chain[i].tl_mbuf != NULL) {
			m_freem(sc->tl_cdata.tl_rx_chain[i].tl_mbuf);
			sc->tl_cdata.tl_rx_chain[i].tl_mbuf = NULL;
		}
	}
	bzero(&sc->tl_ldata->tl_rx_list, sizeof(sc->tl_ldata->tl_rx_list));

	/*
	 * Free the TX list buffers.
	 */
	for (i = 0; i < TL_TX_LIST_CNT; i++) {
		if (sc->tl_cdata.tl_tx_chain[i].tl_mbuf != NULL) {
			m_freem(sc->tl_cdata.tl_tx_chain[i].tl_mbuf);
			sc->tl_cdata.tl_tx_chain[i].tl_mbuf = NULL;
		}
	}
	bzero(&sc->tl_ldata->tl_tx_list, sizeof(sc->tl_ldata->tl_tx_list));

	ifp->if_flags &= ~IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
}

int
tl_probe(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = (struct pci_attach_args *) aux;

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_TI) {
		if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_TI_TLAN)
			return 1;
		return 0;
	}

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_COMPAQ) {
		switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_COMPAQ_N100TX:
		case PCI_PRODUCT_COMPAQ_N10T:
		case PCI_PRODUCT_COMPAQ_INTNF3P:
		case PCI_PRODUCT_COMPAQ_DPNET100TX:
		case PCI_PRODUCT_COMPAQ_INTPL100TX:
		case PCI_PRODUCT_COMPAQ_DP4000:
		case PCI_PRODUCT_COMPAQ_N10T2:
		case PCI_PRODUCT_COMPAQ_N10_TX_UTP:
		case PCI_PRODUCT_COMPAQ_NF3P:
		case PCI_PRODUCT_COMPAQ_NF3P_BNC:
			return 1;
		}
		return 0;
	}

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_OLICOM) {
		switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_OLICOM_OC2183:
		case PCI_PRODUCT_OLICOM_OC2325:
		case PCI_PRODUCT_OLICOM_OC2326:
			return 1;
		}
		return 0;
	}

	return 0;
}

void
tl_attach(struct device *parent, struct device *self, void *aux)
{
	struct tl_softc *sc = (struct tl_softc *)self;
	struct pci_attach_args *pa = aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	pci_intr_handle_t ih;
	const char *intrstr = NULL;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	bus_size_t iosize;
	u_int32_t command;
	int i, rseg;
	bus_dma_segment_t seg;
	bus_dmamap_t dmamap;
	caddr_t kva;

	/*
	 * Map control/status registers.
	 */

#ifdef TL_USEIOSPACE
	if (pci_mapreg_map(pa, TL_PCI_LOIO, PCI_MAPREG_TYPE_IO, 0,
	    &sc->tl_btag, &sc->tl_bhandle, NULL, &iosize, 0)) {
		if (pci_mapreg_map(pa, TL_PCI_LOMEM, PCI_MAPREG_TYPE_IO, 0,
		    &sc->tl_btag, &sc->tl_bhandle, NULL, &iosize, 0)) {
			printf(": can't map i/o space\n");
			return;
		}
	}
#else
	if (pci_mapreg_map(pa, TL_PCI_LOMEM, PCI_MAPREG_TYPE_MEM, 0,
	    &sc->tl_btag, &sc->tl_bhandle, NULL, &iosize, 0)){
		if (pci_mapreg_map(pa, TL_PCI_LOIO, PCI_MAPREG_TYPE_MEM, 0,
		    &sc->tl_btag, &sc->tl_bhandle, NULL, &iosize, 0)){
			printf(": can't map mem space\n");
			return;
		}
	}
#endif

	/*
	 * Manual wants the PCI latency timer jacked up to 0xff
	 */
	command = pci_conf_read(pa->pa_pc, pa->pa_tag, TL_PCI_LATENCY_TIMER);
	command |= 0x0000ff00;
	pci_conf_write(pa->pa_pc, pa->pa_tag, TL_PCI_LATENCY_TIMER, command);

	/*
	 * Allocate our interrupt.
	 */
	if (pci_intr_map(pa, &ih)) {
		printf(": couldn't map interrupt\n");
		bus_space_unmap(sc->tl_btag, sc->tl_bhandle, iosize);
		return;
	}
	intrstr = pci_intr_string(pc, ih);
	sc->sc_ih = pci_intr_establish(pc, ih, IPL_NET, tl_intr, sc,
	    self->dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": could not establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		bus_space_unmap(sc->tl_btag, sc->tl_bhandle, iosize);
		return;
	}
	printf(": %s", intrstr);

	sc->sc_dmat = pa->pa_dmat;
	if (bus_dmamem_alloc(sc->sc_dmat, sizeof(struct tl_list_data),
	    PAGE_SIZE, 0, &seg, 1, &rseg, BUS_DMA_NOWAIT | BUS_DMA_ZERO)) {
		printf("%s: can't alloc list\n", sc->sc_dev.dv_xname);
		bus_space_unmap(sc->tl_btag, sc->tl_bhandle, iosize);
		return;
	}
	if (bus_dmamem_map(sc->sc_dmat, &seg, rseg, sizeof(struct tl_list_data),
	    &kva, BUS_DMA_NOWAIT)) {
		printf("%s: can't map dma buffers (%zd bytes)\n",
		    sc->sc_dev.dv_xname, sizeof(struct tl_list_data));
		bus_dmamem_free(sc->sc_dmat, &seg, rseg);
		return;
	}
	if (bus_dmamap_create(sc->sc_dmat, sizeof(struct tl_list_data), 1,
	    sizeof(struct tl_list_data), 0, BUS_DMA_NOWAIT, &dmamap)) {
		printf("%s: can't create dma map\n", sc->sc_dev.dv_xname);
		bus_dmamem_unmap(sc->sc_dmat, kva, sizeof(struct tl_list_data));
		bus_dmamem_free(sc->sc_dmat, &seg, rseg);
		bus_space_unmap(sc->tl_btag, sc->tl_bhandle, iosize);
		return;
	}
	if (bus_dmamap_load(sc->sc_dmat, dmamap, kva,
	    sizeof(struct tl_list_data), NULL, BUS_DMA_NOWAIT)) {
		printf("%s: can't load dma map\n", sc->sc_dev.dv_xname);
		bus_dmamap_destroy(sc->sc_dmat, dmamap);
		bus_dmamem_unmap(sc->sc_dmat, kva, sizeof(struct tl_list_data));
		bus_dmamem_free(sc->sc_dmat, &seg, rseg);
		bus_space_unmap(sc->tl_btag, sc->tl_bhandle, iosize);
		return;
	}
	sc->tl_ldata = (struct tl_list_data *)kva;

	for (sc->tl_product = tl_prods; sc->tl_product->tp_vend;
	     sc->tl_product++) {
		if (sc->tl_product->tp_vend == PCI_VENDOR(pa->pa_id) &&
		    sc->tl_product->tp_prod == PCI_PRODUCT(pa->pa_id))
			break;
	}
		
	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_COMPAQ ||
	    PCI_VENDOR(pa->pa_id) == PCI_VENDOR_TI)
		sc->tl_eeaddr = TL_EEPROM_EADDR;
	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_OLICOM)
		sc->tl_eeaddr = TL_EEPROM_EADDR_OC;

	/*
	 * Reset adapter.
	 */
	tl_softreset(sc, 1);
	tl_hardreset(self);
	DELAY(1000000);
	tl_softreset(sc, 1);

	/*
	 * Get station address from the EEPROM.
	 */
	if (tl_read_eeprom(sc, (caddr_t)&sc->arpcom.ac_enaddr,
	    sc->tl_eeaddr, ETHER_ADDR_LEN)) {
		printf("\n%s: failed to read station address\n",
		    sc->sc_dev.dv_xname);
		bus_space_unmap(sc->tl_btag, sc->tl_bhandle, iosize);
		return;
	}

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_OLICOM) {
		for (i = 0; i < ETHER_ADDR_LEN; i += 2) {
			u_int16_t *p;

			p = (u_int16_t *)&sc->arpcom.ac_enaddr[i];
			*p = ntohs(*p);
		}
	}

	printf(" address %s\n", ether_sprintf(sc->arpcom.ac_enaddr));

	ifp = &sc->arpcom.ac_if;
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = tl_ioctl;
	ifp->if_start = tl_start;
	ifp->if_watchdog = tl_watchdog;
	ifq_init_maxlen(&ifp->if_snd, TL_TX_LIST_CNT - 1);
	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);

	ifp->if_capabilities = IFCAP_VLAN_MTU;

	/*
	 * Reset adapter (again).
	 */
	tl_softreset(sc, 1);
	tl_hardreset(self);
	DELAY(1000000);
	tl_softreset(sc, 1);

	/*
	 * Do MII setup. If no PHYs are found, then this is a
	 * bitrate ThunderLAN chip that only supports 10baseT
	 * and AUI/BNC.
	 */
	sc->sc_mii.mii_ifp = ifp;
	sc->sc_mii.mii_readreg = tl_miibus_readreg;
	sc->sc_mii.mii_writereg = tl_miibus_writereg;
	sc->sc_mii.mii_statchg = tl_miibus_statchg;
	ifmedia_init(&sc->sc_mii.mii_media, 0, tl_ifmedia_upd, tl_ifmedia_sts);
	mii_attach(self, &sc->sc_mii, 0xffffffff, MII_PHY_ANY, MII_OFFSET_ANY,
	    0);
	if (LIST_FIRST(&sc->sc_mii.mii_phys) == NULL) {
		struct ifmedia *ifm;
		sc->tl_bitrate = 1;
		ifmedia_init(&sc->ifmedia, 0, tl_ifmedia_upd, tl_ifmedia_sts);
		ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_10_T, 0, NULL);
		ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_10_T|IFM_HDX, 0, NULL);
		ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_10_T|IFM_FDX, 0, NULL);
		ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_10_5, 0, NULL);
		ifmedia_set(&sc->ifmedia, IFM_ETHER|IFM_10_T);
		/* Reset again, this time setting bitrate mode. */
		tl_softreset(sc, 1);
		ifm = &sc->ifmedia;
		ifm->ifm_media = ifm->ifm_cur->ifm_media;
		tl_ifmedia_upd(ifp);
	} else
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_AUTO);

	/*
	 * Attach us everywhere.
	 */
	if_attach(ifp);
	ether_ifattach(ifp);
}

void
tl_wait_up(void *xsc)
{
	struct tl_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;

	ifp->if_flags |= IFF_RUNNING;
	ifq_clr_oactive(&ifp->if_snd);
}

const struct cfattach tl_ca = {
	sizeof(struct tl_softc), tl_probe, tl_attach
};

struct cfdriver tl_cd = {
	NULL, "tl", DV_IFNET
};
