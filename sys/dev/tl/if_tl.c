/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

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
 * fragment per list; this may result is slighltly less efficient use
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <net/bpf.h>

#include <vm/vm.h>              /* for vtophys */
#include <vm/pmap.h>            /* for vtophys */
#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <dev/mii/mii.h>
#include <dev/mii/mii_bitbang.h>
#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

/*
 * Default to using PIO register access mode to pacify certain
 * laptop docking stations with built-in ThunderLAN chips that
 * don't seem to handle memory mapped mode properly.
 */
#define TL_USEIOSPACE

#include <dev/tl/if_tlreg.h>

MODULE_DEPEND(tl, pci, 1, 1, 1);
MODULE_DEPEND(tl, ether, 1, 1, 1);
MODULE_DEPEND(tl, miibus, 1, 1, 1);

/* "device miibus" required.  See GENERIC if you get errors here. */
#include "miibus_if.h"

/*
 * Various supported device vendors/types and their names.
 */

static const struct tl_type tl_devs[] = {
	{ TI_VENDORID,	TI_DEVICEID_THUNDERLAN,
		"Texas Instruments ThunderLAN" },
	{ COMPAQ_VENDORID, COMPAQ_DEVICEID_NETEL_10,
		"Compaq Netelligent 10" },
	{ COMPAQ_VENDORID, COMPAQ_DEVICEID_NETEL_10_100,
		"Compaq Netelligent 10/100" },
	{ COMPAQ_VENDORID, COMPAQ_DEVICEID_NETEL_10_100_PROLIANT,
		"Compaq Netelligent 10/100 Proliant" },
	{ COMPAQ_VENDORID, COMPAQ_DEVICEID_NETEL_10_100_DUAL,
		"Compaq Netelligent 10/100 Dual Port" },
	{ COMPAQ_VENDORID, COMPAQ_DEVICEID_NETFLEX_3P_INTEGRATED,
		"Compaq NetFlex-3/P Integrated" },
	{ COMPAQ_VENDORID, COMPAQ_DEVICEID_NETFLEX_3P,
		"Compaq NetFlex-3/P" },
	{ COMPAQ_VENDORID, COMPAQ_DEVICEID_NETFLEX_3P_BNC,
		"Compaq NetFlex 3/P w/ BNC" },
	{ COMPAQ_VENDORID, COMPAQ_DEVICEID_NETEL_10_100_EMBEDDED,
		"Compaq Netelligent 10/100 TX Embedded UTP" },
	{ COMPAQ_VENDORID, COMPAQ_DEVICEID_NETEL_10_T2_UTP_COAX,
		"Compaq Netelligent 10 T/2 PCI UTP/Coax" },
	{ COMPAQ_VENDORID, COMPAQ_DEVICEID_NETEL_10_100_TX_UTP,
		"Compaq Netelligent 10/100 TX UTP" },
	{ OLICOM_VENDORID, OLICOM_DEVICEID_OC2183,
		"Olicom OC-2183/2185" },
	{ OLICOM_VENDORID, OLICOM_DEVICEID_OC2325,
		"Olicom OC-2325" },
	{ OLICOM_VENDORID, OLICOM_DEVICEID_OC2326,
		"Olicom OC-2326 10/100 TX UTP" },
	{ 0, 0, NULL }
};

static int tl_probe(device_t);
static int tl_attach(device_t);
static int tl_detach(device_t);
static int tl_intvec_rxeoc(void *, u_int32_t);
static int tl_intvec_txeoc(void *, u_int32_t);
static int tl_intvec_txeof(void *, u_int32_t);
static int tl_intvec_rxeof(void *, u_int32_t);
static int tl_intvec_adchk(void *, u_int32_t);
static int tl_intvec_netsts(void *, u_int32_t);

static int tl_newbuf(struct tl_softc *, struct tl_chain_onefrag *);
static void tl_stats_update(void *);
static int tl_encap(struct tl_softc *, struct tl_chain *, struct mbuf *);

static void tl_intr(void *);
static void tl_start(struct ifnet *);
static void tl_start_locked(struct ifnet *);
static int tl_ioctl(struct ifnet *, u_long, caddr_t);
static void tl_init(void *);
static void tl_init_locked(struct tl_softc *);
static void tl_stop(struct tl_softc *);
static void tl_watchdog(struct tl_softc *);
static int tl_shutdown(device_t);
static int tl_ifmedia_upd(struct ifnet *);
static void tl_ifmedia_sts(struct ifnet *, struct ifmediareq *);

static u_int8_t tl_eeprom_putbyte(struct tl_softc *, int);
static u_int8_t	tl_eeprom_getbyte(struct tl_softc *, int, u_int8_t *);
static int tl_read_eeprom(struct tl_softc *, caddr_t, int, int);

static int tl_miibus_readreg(device_t, int, int);
static int tl_miibus_writereg(device_t, int, int, int);
static void tl_miibus_statchg(device_t);

static void tl_setmode(struct tl_softc *, int);
static uint32_t tl_mchash(const uint8_t *);
static void tl_setmulti(struct tl_softc *);
static void tl_setfilt(struct tl_softc *, caddr_t, int);
static void tl_softreset(struct tl_softc *, int);
static void tl_hardreset(device_t);
static int tl_list_rx_init(struct tl_softc *);
static int tl_list_tx_init(struct tl_softc *);

static u_int8_t tl_dio_read8(struct tl_softc *, int);
static u_int16_t tl_dio_read16(struct tl_softc *, int);
static u_int32_t tl_dio_read32(struct tl_softc *, int);
static void tl_dio_write8(struct tl_softc *, int, int);
static void tl_dio_write16(struct tl_softc *, int, int);
static void tl_dio_write32(struct tl_softc *, int, int);
static void tl_dio_setbit(struct tl_softc *, int, int);
static void tl_dio_clrbit(struct tl_softc *, int, int);
static void tl_dio_setbit16(struct tl_softc *, int, int);
static void tl_dio_clrbit16(struct tl_softc *, int, int);

/*
 * MII bit-bang glue
 */
static uint32_t tl_mii_bitbang_read(device_t);
static void tl_mii_bitbang_write(device_t, uint32_t);

static const struct mii_bitbang_ops tl_mii_bitbang_ops = {
	tl_mii_bitbang_read,
	tl_mii_bitbang_write,
	{
		TL_SIO_MDATA,	/* MII_BIT_MDO */
		TL_SIO_MDATA,	/* MII_BIT_MDI */
		TL_SIO_MCLK,	/* MII_BIT_MDC */
		TL_SIO_MTXEN,	/* MII_BIT_DIR_HOST_PHY */
		0,		/* MII_BIT_DIR_PHY_HOST */
	}
};

#ifdef TL_USEIOSPACE
#define TL_RES		SYS_RES_IOPORT
#define TL_RID		TL_PCI_LOIO
#else
#define TL_RES		SYS_RES_MEMORY
#define TL_RID		TL_PCI_LOMEM
#endif

static device_method_t tl_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		tl_probe),
	DEVMETHOD(device_attach,	tl_attach),
	DEVMETHOD(device_detach,	tl_detach),
	DEVMETHOD(device_shutdown,	tl_shutdown),

	/* MII interface */
	DEVMETHOD(miibus_readreg,	tl_miibus_readreg),
	DEVMETHOD(miibus_writereg,	tl_miibus_writereg),
	DEVMETHOD(miibus_statchg,	tl_miibus_statchg),

	DEVMETHOD_END
};

static driver_t tl_driver = {
	"tl",
	tl_methods,
	sizeof(struct tl_softc)
};

static devclass_t tl_devclass;

DRIVER_MODULE(tl, pci, tl_driver, tl_devclass, 0, 0);
DRIVER_MODULE(miibus, tl, miibus_driver, miibus_devclass, 0, 0);

static u_int8_t tl_dio_read8(sc, reg)
	struct tl_softc		*sc;
	int			reg;
{

	CSR_BARRIER(sc, TL_DIO_ADDR, 2,
		BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	CSR_WRITE_2(sc, TL_DIO_ADDR, reg);
	CSR_BARRIER(sc, TL_DIO_ADDR, 2,
		BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	return(CSR_READ_1(sc, TL_DIO_DATA + (reg & 3)));
}

static u_int16_t tl_dio_read16(sc, reg)
	struct tl_softc		*sc;
	int			reg;
{

	CSR_BARRIER(sc, TL_DIO_ADDR, 2,
		BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	CSR_WRITE_2(sc, TL_DIO_ADDR, reg);
	CSR_BARRIER(sc, TL_DIO_ADDR, 2,
		BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	return(CSR_READ_2(sc, TL_DIO_DATA + (reg & 3)));
}

static u_int32_t tl_dio_read32(sc, reg)
	struct tl_softc		*sc;
	int			reg;
{

	CSR_BARRIER(sc, TL_DIO_ADDR, 2,
		BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	CSR_WRITE_2(sc, TL_DIO_ADDR, reg);
	CSR_BARRIER(sc, TL_DIO_ADDR, 2,
		BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	return(CSR_READ_4(sc, TL_DIO_DATA + (reg & 3)));
}

static void tl_dio_write8(sc, reg, val)
	struct tl_softc		*sc;
	int			reg;
	int			val;
{

	CSR_BARRIER(sc, TL_DIO_ADDR, 2,
		BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	CSR_WRITE_2(sc, TL_DIO_ADDR, reg);
	CSR_BARRIER(sc, TL_DIO_ADDR, 2,
		BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	CSR_WRITE_1(sc, TL_DIO_DATA + (reg & 3), val);
}

static void tl_dio_write16(sc, reg, val)
	struct tl_softc		*sc;
	int			reg;
	int			val;
{

	CSR_BARRIER(sc, TL_DIO_ADDR, 2,
		BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	CSR_WRITE_2(sc, TL_DIO_ADDR, reg);
	CSR_BARRIER(sc, TL_DIO_ADDR, 2,
		BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	CSR_WRITE_2(sc, TL_DIO_DATA + (reg & 3), val);
}

static void tl_dio_write32(sc, reg, val)
	struct tl_softc		*sc;
	int			reg;
	int			val;
{

	CSR_BARRIER(sc, TL_DIO_ADDR, 2,
		BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	CSR_WRITE_2(sc, TL_DIO_ADDR, reg);
	CSR_BARRIER(sc, TL_DIO_ADDR, 2,
		BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	CSR_WRITE_4(sc, TL_DIO_DATA + (reg & 3), val);
}

static void
tl_dio_setbit(sc, reg, bit)
	struct tl_softc		*sc;
	int			reg;
	int			bit;
{
	u_int8_t			f;

	CSR_BARRIER(sc, TL_DIO_ADDR, 2,
		BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	CSR_WRITE_2(sc, TL_DIO_ADDR, reg);
	CSR_BARRIER(sc, TL_DIO_ADDR, 2,
		BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	f = CSR_READ_1(sc, TL_DIO_DATA + (reg & 3));
	f |= bit;
	CSR_BARRIER(sc, TL_DIO_DATA + (reg & 3), 1,
		BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	CSR_WRITE_1(sc, TL_DIO_DATA + (reg & 3), f);
}

static void
tl_dio_clrbit(sc, reg, bit)
	struct tl_softc		*sc;
	int			reg;
	int			bit;
{
	u_int8_t			f;

	CSR_BARRIER(sc, TL_DIO_ADDR, 2,
		BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	CSR_WRITE_2(sc, TL_DIO_ADDR, reg);
	CSR_BARRIER(sc, TL_DIO_ADDR, 2,
		BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	f = CSR_READ_1(sc, TL_DIO_DATA + (reg & 3));
	f &= ~bit;
	CSR_BARRIER(sc, TL_DIO_DATA + (reg & 3), 1,
		BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	CSR_WRITE_1(sc, TL_DIO_DATA + (reg & 3), f);
}

static void tl_dio_setbit16(sc, reg, bit)
	struct tl_softc		*sc;
	int			reg;
	int			bit;
{
	u_int16_t			f;

	CSR_BARRIER(sc, TL_DIO_ADDR, 2,
		BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	CSR_WRITE_2(sc, TL_DIO_ADDR, reg);
	CSR_BARRIER(sc, TL_DIO_ADDR, 2,
		BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	f = CSR_READ_2(sc, TL_DIO_DATA + (reg & 3));
	f |= bit;
	CSR_BARRIER(sc, TL_DIO_DATA + (reg & 3), 2,
		BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	CSR_WRITE_2(sc, TL_DIO_DATA + (reg & 3), f);
}

static void tl_dio_clrbit16(sc, reg, bit)
	struct tl_softc		*sc;
	int			reg;
	int			bit;
{
	u_int16_t			f;

	CSR_BARRIER(sc, TL_DIO_ADDR, 2,
		BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	CSR_WRITE_2(sc, TL_DIO_ADDR, reg);
	CSR_BARRIER(sc, TL_DIO_ADDR, 2,
		BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	f = CSR_READ_2(sc, TL_DIO_DATA + (reg & 3));
	f &= ~bit;
	CSR_BARRIER(sc, TL_DIO_DATA + (reg & 3), 2,
		BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	CSR_WRITE_2(sc, TL_DIO_DATA + (reg & 3), f);
}

/*
 * Send an instruction or address to the EEPROM, check for ACK.
 */
static u_int8_t tl_eeprom_putbyte(sc, byte)
	struct tl_softc		*sc;
	int			byte;
{
	int			i, ack = 0;

	/*
	 * Make sure we're in TX mode.
	 */
	tl_dio_setbit(sc, TL_NETSIO, TL_SIO_ETXEN);

	/*
	 * Feed in each bit and stobe the clock.
	 */
	for (i = 0x80; i; i >>= 1) {
		if (byte & i) {
			tl_dio_setbit(sc, TL_NETSIO, TL_SIO_EDATA);
		} else {
			tl_dio_clrbit(sc, TL_NETSIO, TL_SIO_EDATA);
		}
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
static u_int8_t tl_eeprom_getbyte(sc, addr, dest)
	struct tl_softc		*sc;
	int			addr;
	u_int8_t		*dest;
{
	int			i;
	u_int8_t		byte = 0;
	device_t		tl_dev = sc->tl_dev;

	tl_dio_write8(sc, TL_NETSIO, 0);

	EEPROM_START;

	/*
	 * Send write control code to EEPROM.
	 */
	if (tl_eeprom_putbyte(sc, EEPROM_CTL_WRITE)) {
		device_printf(tl_dev, "failed to send write command, status: %x\n",
		    tl_dio_read8(sc, TL_NETSIO));
		return(1);
	}

	/*
	 * Send address of byte we want to read.
	 */
	if (tl_eeprom_putbyte(sc, addr)) {
		device_printf(tl_dev, "failed to send address, status: %x\n",
		    tl_dio_read8(sc, TL_NETSIO));
		return(1);
	}

	EEPROM_STOP;
	EEPROM_START;
	/*
	 * Send read control code to EEPROM.
	 */
	if (tl_eeprom_putbyte(sc, EEPROM_CTL_READ)) {
		device_printf(tl_dev, "failed to send write command, status: %x\n",
		    tl_dio_read8(sc, TL_NETSIO));
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
static int
tl_read_eeprom(sc, dest, off, cnt)
	struct tl_softc		*sc;
	caddr_t			dest;
	int			off;
	int			cnt;
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

#define	TL_SIO_MII	(TL_SIO_MCLK | TL_SIO_MDATA | TL_SIO_MTXEN)

/*
 * Read the MII serial port for the MII bit-bang module.
 */
static uint32_t
tl_mii_bitbang_read(device_t dev)
{
	struct tl_softc *sc;
	uint32_t val;

	sc = device_get_softc(dev);

	val = tl_dio_read8(sc, TL_NETSIO) & TL_SIO_MII;
	CSR_BARRIER(sc, TL_NETSIO, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

	return (val);
}

/*
 * Write the MII serial port for the MII bit-bang module.
 */
static void
tl_mii_bitbang_write(device_t dev, uint32_t val)
{
	struct tl_softc *sc;

	sc = device_get_softc(dev);

	val = (tl_dio_read8(sc, TL_NETSIO) & ~TL_SIO_MII) | val;
	CSR_BARRIER(sc, TL_NETSIO, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	tl_dio_write8(sc, TL_NETSIO, val);
	CSR_BARRIER(sc, TL_NETSIO, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

static int
tl_miibus_readreg(dev, phy, reg)
	device_t		dev;
	int			phy, reg;
{
	struct tl_softc		*sc;
	int			minten, val;

	sc = device_get_softc(dev);

	/*
	 * Turn off MII interrupt by forcing MINTEN low.
	 */
	minten = tl_dio_read8(sc, TL_NETSIO) & TL_SIO_MINTEN;
	if (minten) {
		tl_dio_clrbit(sc, TL_NETSIO, TL_SIO_MINTEN);
	}

	val = mii_bitbang_readreg(dev, &tl_mii_bitbang_ops, phy, reg);

	/* Reenable interrupts. */
	if (minten) {
		tl_dio_setbit(sc, TL_NETSIO, TL_SIO_MINTEN);
	}

	return (val);
}

static int
tl_miibus_writereg(dev, phy, reg, data)
	device_t		dev;
	int			phy, reg, data;
{
	struct tl_softc		*sc;
	int			minten;

	sc = device_get_softc(dev);

	/*
	 * Turn off MII interrupt by forcing MINTEN low.
	 */
	minten = tl_dio_read8(sc, TL_NETSIO) & TL_SIO_MINTEN;
	if (minten) {
		tl_dio_clrbit(sc, TL_NETSIO, TL_SIO_MINTEN);
	}

	mii_bitbang_writereg(dev, &tl_mii_bitbang_ops, phy, reg, data);

	/* Reenable interrupts. */
	if (minten) {
		tl_dio_setbit(sc, TL_NETSIO, TL_SIO_MINTEN);
	}

	return(0);
}

static void
tl_miibus_statchg(dev)
	device_t		dev;
{
	struct tl_softc		*sc;
	struct mii_data		*mii;

	sc = device_get_softc(dev);
	mii = device_get_softc(sc->tl_miibus);

	if ((mii->mii_media_active & IFM_GMASK) == IFM_FDX) {
		tl_dio_setbit(sc, TL_NETCMD, TL_CMD_DUPLEX);
	} else {
		tl_dio_clrbit(sc, TL_NETCMD, TL_CMD_DUPLEX);
	}
}

/*
 * Set modes for bitrate devices.
 */
static void
tl_setmode(sc, media)
	struct tl_softc		*sc;
	int			media;
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
static uint32_t
tl_mchash(addr)
	const uint8_t *addr;
{
	int t;

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
static void
tl_setfilt(sc, addr, slot)
	struct tl_softc		*sc;
	caddr_t			addr;
	int			slot;
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
static void
tl_setmulti(sc)
	struct tl_softc		*sc;
{
	struct ifnet		*ifp;
	u_int32_t		hashes[2] = { 0, 0 };
	int			h, i;
	struct ifmultiaddr	*ifma;
	u_int8_t		dummy[] = { 0, 0, 0, 0, 0 ,0 };
	ifp = sc->tl_ifp;

	/* First, zot all the existing filters. */
	for (i = 1; i < 4; i++)
		tl_setfilt(sc, (caddr_t)&dummy, i);
	tl_dio_write32(sc, TL_HASH1, 0);
	tl_dio_write32(sc, TL_HASH2, 0);

	/* Now program new ones. */
	if (ifp->if_flags & IFF_ALLMULTI) {
		hashes[0] = 0xFFFFFFFF;
		hashes[1] = 0xFFFFFFFF;
	} else {
		i = 1;
		if_maddr_rlock(ifp);
		/* XXX want to maintain reverse semantics - pop list and re-add? */
		CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
			if (ifma->ifma_addr->sa_family != AF_LINK)
				continue;
			/*
			 * Program the first three multicast groups
			 * into the perfect filter. For all others,
			 * use the hash table.
			 */
			if (i < 4) {
				tl_setfilt(sc,
			LLADDR((struct sockaddr_dl *)ifma->ifma_addr), i);
				i++;
				continue;
			}

			h = tl_mchash(
				LLADDR((struct sockaddr_dl *)ifma->ifma_addr));
			if (h < 32)
				hashes[0] |= (1 << h);
			else
				hashes[1] |= (1 << (h - 32));
		}
		if_maddr_runlock(ifp);
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
static void
tl_hardreset(dev)
	device_t		dev;
{
	int			i;
	u_int16_t		flags;

	mii_bitbang_sync(dev, &tl_mii_bitbang_ops);

	flags = BMCR_LOOP|BMCR_ISO|BMCR_PDOWN;

	for (i = 0; i < MII_NPHY; i++)
		tl_miibus_writereg(dev, i, MII_BMCR, flags);

	tl_miibus_writereg(dev, 31, MII_BMCR, BMCR_ISO);
	DELAY(50000);
	tl_miibus_writereg(dev, 31, MII_BMCR, BMCR_LOOP|BMCR_ISO);
	mii_bitbang_sync(dev, &tl_mii_bitbang_ops);
	while(tl_miibus_readreg(dev, 31, MII_BMCR) & BMCR_RESET);

	DELAY(50000);
}

static void
tl_softreset(sc, internal)
	struct tl_softc		*sc;
	int			internal;
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
 * Probe for a ThunderLAN chip. Check the PCI vendor and device IDs
 * against our list and return its name if we find a match.
 */
static int
tl_probe(dev)
	device_t		dev;
{
	const struct tl_type	*t;

	t = tl_devs;

	while(t->tl_name != NULL) {
		if ((pci_get_vendor(dev) == t->tl_vid) &&
		    (pci_get_device(dev) == t->tl_did)) {
			device_set_desc(dev, t->tl_name);
			return (BUS_PROBE_DEFAULT);
		}
		t++;
	}

	return(ENXIO);
}

static int
tl_attach(dev)
	device_t		dev;
{
	u_int16_t		did, vid;
	const struct tl_type	*t;
	struct ifnet		*ifp;
	struct tl_softc		*sc;
	int			error, flags, i, rid, unit;
	u_char			eaddr[6];

	vid = pci_get_vendor(dev);
	did = pci_get_device(dev);
	sc = device_get_softc(dev);
	sc->tl_dev = dev;
	unit = device_get_unit(dev);

	t = tl_devs;
	while(t->tl_name != NULL) {
		if (vid == t->tl_vid && did == t->tl_did)
			break;
		t++;
	}

	if (t->tl_name == NULL) {
		device_printf(dev, "unknown device!?\n");
		return (ENXIO);
	}

	mtx_init(&sc->tl_mtx, device_get_nameunit(dev), MTX_NETWORK_LOCK,
	    MTX_DEF);

	/*
	 * Map control/status registers.
	 */
	pci_enable_busmaster(dev);

#ifdef TL_USEIOSPACE

	rid = TL_PCI_LOIO;
	sc->tl_res = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &rid,
		RF_ACTIVE);

	/*
	 * Some cards have the I/O and memory mapped address registers
	 * reversed. Try both combinations before giving up.
	 */
	if (sc->tl_res == NULL) {
		rid = TL_PCI_LOMEM;
		sc->tl_res = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &rid,
		    RF_ACTIVE);
	}
#else
	rid = TL_PCI_LOMEM;
	sc->tl_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->tl_res == NULL) {
		rid = TL_PCI_LOIO;
		sc->tl_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
		    RF_ACTIVE);
	}
#endif

	if (sc->tl_res == NULL) {
		device_printf(dev, "couldn't map ports/memory\n");
		error = ENXIO;
		goto fail;
	}

#ifdef notdef
	/*
	 * The ThunderLAN manual suggests jacking the PCI latency
	 * timer all the way up to its maximum value. I'm not sure
	 * if this is really necessary, but what the manual wants,
	 * the manual gets.
	 */
	command = pci_read_config(dev, TL_PCI_LATENCY_TIMER, 4);
	command |= 0x0000FF00;
	pci_write_config(dev, TL_PCI_LATENCY_TIMER, command, 4);
#endif

	/* Allocate interrupt */
	rid = 0;
	sc->tl_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_SHAREABLE | RF_ACTIVE);

	if (sc->tl_irq == NULL) {
		device_printf(dev, "couldn't map interrupt\n");
		error = ENXIO;
		goto fail;
	}

	/*
	 * Now allocate memory for the TX and RX lists.
	 */
	sc->tl_ldata = contigmalloc(sizeof(struct tl_list_data), M_DEVBUF,
	    M_NOWAIT, 0, 0xffffffff, PAGE_SIZE, 0);

	if (sc->tl_ldata == NULL) {
		device_printf(dev, "no memory for list buffers!\n");
		error = ENXIO;
		goto fail;
	}

	bzero(sc->tl_ldata, sizeof(struct tl_list_data));

	if (vid == COMPAQ_VENDORID || vid == TI_VENDORID)
		sc->tl_eeaddr = TL_EEPROM_EADDR;
	if (vid == OLICOM_VENDORID)
		sc->tl_eeaddr = TL_EEPROM_EADDR_OC;

	/* Reset the adapter. */
	tl_softreset(sc, 1);
	tl_hardreset(dev);
	tl_softreset(sc, 1);

	/*
	 * Get station address from the EEPROM.
	 */
	if (tl_read_eeprom(sc, eaddr, sc->tl_eeaddr, ETHER_ADDR_LEN)) {
		device_printf(dev, "failed to read station address\n");
		error = ENXIO;
		goto fail;
	}

        /*
         * XXX Olicom, in its desire to be different from the
         * rest of the world, has done strange things with the
         * encoding of the station address in the EEPROM. First
         * of all, they store the address at offset 0xF8 rather
         * than at 0x83 like the ThunderLAN manual suggests.
         * Second, they store the address in three 16-bit words in
         * network byte order, as opposed to storing it sequentially
         * like all the other ThunderLAN cards. In order to get
         * the station address in a form that matches what the Olicom
         * diagnostic utility specifies, we have to byte-swap each
         * word. To make things even more confusing, neither 00:00:28
         * nor 00:00:24 appear in the IEEE OUI database.
         */
        if (vid == OLICOM_VENDORID) {
                for (i = 0; i < ETHER_ADDR_LEN; i += 2) {
                        u_int16_t               *p;
                        p = (u_int16_t *)&eaddr[i];
                        *p = ntohs(*p);
                }
        }

	ifp = sc->tl_ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "can not if_alloc()\n");
		error = ENOSPC;
		goto fail;
	}
	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = tl_ioctl;
	ifp->if_start = tl_start;
	ifp->if_init = tl_init;
	ifp->if_snd.ifq_maxlen = TL_TX_LIST_CNT - 1;
	ifp->if_capabilities |= IFCAP_VLAN_MTU;
	ifp->if_capenable |= IFCAP_VLAN_MTU;
	callout_init_mtx(&sc->tl_stat_callout, &sc->tl_mtx, 0);

	/* Reset the adapter again. */
	tl_softreset(sc, 1);
	tl_hardreset(dev);
	tl_softreset(sc, 1);

	/*
	 * Do MII setup. If no PHYs are found, then this is a
	 * bitrate ThunderLAN chip that only supports 10baseT
	 * and AUI/BNC.
	 * XXX mii_attach() can fail for reason different than
	 * no PHYs found!
	 */
	flags = 0;
	if (vid == COMPAQ_VENDORID) {
		if (did == COMPAQ_DEVICEID_NETEL_10_100_PROLIANT ||
		    did == COMPAQ_DEVICEID_NETFLEX_3P_INTEGRATED ||
		    did == COMPAQ_DEVICEID_NETFLEX_3P_BNC ||
		    did == COMPAQ_DEVICEID_NETEL_10_T2_UTP_COAX)
			flags |= MIIF_MACPRIV0;
		if (did == COMPAQ_DEVICEID_NETEL_10 ||
		    did == COMPAQ_DEVICEID_NETEL_10_100_DUAL ||
		    did == COMPAQ_DEVICEID_NETFLEX_3P ||
		    did == COMPAQ_DEVICEID_NETEL_10_100_EMBEDDED)
			flags |= MIIF_MACPRIV1;
	} else if (vid == OLICOM_VENDORID && did == OLICOM_DEVICEID_OC2183)
			flags |= MIIF_MACPRIV0 | MIIF_MACPRIV1;
	if (mii_attach(dev, &sc->tl_miibus, ifp, tl_ifmedia_upd,
	    tl_ifmedia_sts, BMSR_DEFCAPMASK, MII_PHY_ANY, MII_OFFSET_ANY, 0)) {
		struct ifmedia		*ifm;
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
	}

	/*
	 * Call MI attach routine.
	 */
	ether_ifattach(ifp, eaddr);

	/* Hook interrupt last to avoid having to lock softc */
	error = bus_setup_intr(dev, sc->tl_irq, INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, tl_intr, sc, &sc->tl_intrhand);

	if (error) {
		device_printf(dev, "couldn't set up irq\n");
		ether_ifdetach(ifp);
		goto fail;
	}

	gone_by_fcp101_dev(dev);

fail:
	if (error)
		tl_detach(dev);

	return(error);
}

/*
 * Shutdown hardware and free up resources. This can be called any
 * time after the mutex has been initialized. It is called in both
 * the error case in attach and the normal detach case so it needs
 * to be careful about only freeing resources that have actually been
 * allocated.
 */
static int
tl_detach(dev)
	device_t		dev;
{
	struct tl_softc		*sc;
	struct ifnet		*ifp;

	sc = device_get_softc(dev);
	KASSERT(mtx_initialized(&sc->tl_mtx), ("tl mutex not initialized"));
	ifp = sc->tl_ifp;

	/* These should only be active if attach succeeded */
	if (device_is_attached(dev)) {
		ether_ifdetach(ifp);
		TL_LOCK(sc);
		tl_stop(sc);
		TL_UNLOCK(sc);
		callout_drain(&sc->tl_stat_callout);
	}
	if (sc->tl_miibus)
		device_delete_child(dev, sc->tl_miibus);
	bus_generic_detach(dev);

	if (sc->tl_ldata)
		contigfree(sc->tl_ldata, sizeof(struct tl_list_data), M_DEVBUF);
	if (sc->tl_bitrate)
		ifmedia_removeall(&sc->ifmedia);

	if (sc->tl_intrhand)
		bus_teardown_intr(dev, sc->tl_irq, sc->tl_intrhand);
	if (sc->tl_irq)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->tl_irq);
	if (sc->tl_res)
		bus_release_resource(dev, TL_RES, TL_RID, sc->tl_res);

	if (ifp)
		if_free(ifp);

	mtx_destroy(&sc->tl_mtx);

	return(0);
}

/*
 * Initialize the transmit lists.
 */
static int
tl_list_tx_init(sc)
	struct tl_softc		*sc;
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
static int
tl_list_rx_init(sc)
	struct tl_softc		*sc;
{
	struct tl_chain_data		*cd;
	struct tl_list_data		*ld;
	int				i;

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
					vtophys(&ld->tl_rx_list[i + 1]);
		}
	}

	cd->tl_rx_head = &cd->tl_rx_chain[0];
	cd->tl_rx_tail = &cd->tl_rx_chain[TL_RX_LIST_CNT - 1];

	return(0);
}

static int
tl_newbuf(sc, c)
	struct tl_softc		*sc;
	struct tl_chain_onefrag	*c;
{
	struct mbuf		*m_new = NULL;

	m_new = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
	if (m_new == NULL)
		return(ENOBUFS);

	c->tl_mbuf = m_new;
	c->tl_next = NULL;
	c->tl_ptr->tlist_frsize = MCLBYTES;
	c->tl_ptr->tlist_fptr = 0;
	c->tl_ptr->tl_frag.tlist_dadr = vtophys(mtod(m_new, caddr_t));
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
 * just hand the mbufs directly to ether_input(). Once the frame has
 * been sent on its way, the 'list' structure is assigned a new buffer
 * and moved to the end of the RX chain. As long we we stay ahead of
 * the chip, it will always think it has an endless receive channel.
 *
 * If we happen to fall behind and the chip manages to fill up all of
 * the buffers, it will generate an end of channel interrupt and wait
 * for us to empty the chain and restart the receiver.
 */
static int
tl_intvec_rxeof(xsc, type)
	void			*xsc;
	u_int32_t		type;
{
	struct tl_softc		*sc;
	int			r = 0, total_len = 0;
	struct ether_header	*eh;
	struct mbuf		*m;
	struct ifnet		*ifp;
	struct tl_chain_onefrag	*cur_rx;

	sc = xsc;
	ifp = sc->tl_ifp;

	TL_LOCK_ASSERT(sc);

	while(sc->tl_cdata.tl_rx_head != NULL) {
		cur_rx = sc->tl_cdata.tl_rx_head;
		if (!(cur_rx->tl_ptr->tlist_cstat & TL_CSTAT_FRAMECMP))
			break;
		r++;
		sc->tl_cdata.tl_rx_head = cur_rx->tl_next;
		m = cur_rx->tl_mbuf;
		total_len = cur_rx->tl_ptr->tlist_frsize;

		if (tl_newbuf(sc, cur_rx) == ENOBUFS) {
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			cur_rx->tl_ptr->tlist_frsize = MCLBYTES;
			cur_rx->tl_ptr->tlist_cstat = TL_CSTAT_READY;
			cur_rx->tl_ptr->tl_frag.tlist_dcnt = MCLBYTES;
			continue;
		}

		sc->tl_cdata.tl_rx_tail->tl_ptr->tlist_fptr =
						vtophys(cur_rx->tl_ptr);
		sc->tl_cdata.tl_rx_tail->tl_next = cur_rx;
		sc->tl_cdata.tl_rx_tail = cur_rx;

		/*
		 * Note: when the ThunderLAN chip is in 'capture all
		 * frames' mode, it will receive its own transmissions.
		 * We drop don't need to process our own transmissions,
		 * so we drop them here and continue.
		 */
		eh = mtod(m, struct ether_header *);
		/*if (ifp->if_flags & IFF_PROMISC && */
		if (!bcmp(eh->ether_shost, IF_LLADDR(sc->tl_ifp),
		 					ETHER_ADDR_LEN)) {
				m_freem(m);
				continue;
		}

		m->m_pkthdr.rcvif = ifp;
		m->m_pkthdr.len = m->m_len = total_len;

		TL_UNLOCK(sc);
		(*ifp->if_input)(ifp, m);
		TL_LOCK(sc);
	}

	return(r);
}

/*
 * The RX-EOC condition hits when the ch_parm address hasn't been
 * initialized or the adapter reached a list with a forward pointer
 * of 0 (which indicates the end of the chain). In our case, this means
 * the card has hit the end of the receive buffer chain and we need to
 * empty out the buffers and shift the pointer back to the beginning again.
 */
static int
tl_intvec_rxeoc(xsc, type)
	void			*xsc;
	u_int32_t		type;
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
	CSR_WRITE_4(sc, TL_CH_PARM, vtophys(sc->tl_cdata.tl_rx_head->tl_ptr));
	r |= (TL_CMD_GO|TL_CMD_RT);
	return(r);
}

static int
tl_intvec_txeof(xsc, type)
	void			*xsc;
	u_int32_t		type;
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
 * are queued for transmission. If tl_statrt() is called after TXEOF
 * resets the tx_head pointer but _before_ the TXEOC interrupt arrives,
 * it could attempt to issue a transmit command prematurely.
 *
 * To guard against this, tl_start() will only issue transmit commands
 * if the tl_txeoc flag is set, and only the TXEOC interrupt handler
 * can set this flag once tl_start() has cleared it.
 */
static int
tl_intvec_txeoc(xsc, type)
	void			*xsc;
	u_int32_t		type;
{
	struct tl_softc		*sc;
	struct ifnet		*ifp;
	u_int32_t		cmd;

	sc = xsc;
	ifp = sc->tl_ifp;

	/* Clear the timeout timer. */
	sc->tl_timer = 0;

	if (sc->tl_cdata.tl_tx_head == NULL) {
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		sc->tl_cdata.tl_tx_tail = NULL;
		sc->tl_txeoc = 1;
	} else {
		sc->tl_txeoc = 0;
		/* First we have to ack the EOC interrupt. */
		CMD_PUT(sc, TL_CMD_ACK | 0x00000001 | type);
		/* Then load the address of the next TX list. */
		CSR_WRITE_4(sc, TL_CH_PARM,
		    vtophys(sc->tl_cdata.tl_tx_head->tl_ptr));
		/* Restart TX channel. */
		cmd = CSR_READ_4(sc, TL_HOSTCMD);
		cmd &= ~TL_CMD_RT;
		cmd |= TL_CMD_GO|TL_CMD_INTSON;
		CMD_PUT(sc, cmd);
		return(0);
	}

	return(1);
}

static int
tl_intvec_adchk(xsc, type)
	void			*xsc;
	u_int32_t		type;
{
	struct tl_softc		*sc;

	sc = xsc;

	if (type)
		device_printf(sc->tl_dev, "adapter check: %x\n",
			(unsigned int)CSR_READ_4(sc, TL_CH_PARM));

	tl_softreset(sc, 1);
	tl_stop(sc);
	tl_init_locked(sc);
	CMD_SET(sc, TL_CMD_INTSON);

	return(0);
}

static int
tl_intvec_netsts(xsc, type)
	void			*xsc;
	u_int32_t		type;
{
	struct tl_softc		*sc;
	u_int16_t		netsts;

	sc = xsc;

	netsts = tl_dio_read16(sc, TL_NETSTS);
	tl_dio_write16(sc, TL_NETSTS, netsts);

	device_printf(sc->tl_dev, "network status: %x\n", netsts);

	return(1);
}

static void
tl_intr(xsc)
	void			*xsc;
{
	struct tl_softc		*sc;
	struct ifnet		*ifp;
	int			r = 0;
	u_int32_t		type = 0;
	u_int16_t		ints = 0;
	u_int8_t		ivec = 0;

	sc = xsc;
	TL_LOCK(sc);

	/* Disable interrupts */
	ints = CSR_READ_2(sc, TL_HOST_INT);
	CSR_WRITE_2(sc, TL_HOST_INT, ints);
	type = (ints << 16) & 0xFFFF0000;
	ivec = (ints & TL_VEC_MASK) >> 5;
	ints = (ints & TL_INT_MASK) >> 2;

	ifp = sc->tl_ifp;

	switch(ints) {
	case (TL_INTR_INVALID):
#ifdef DIAGNOSTIC
		device_printf(sc->tl_dev, "got an invalid interrupt!\n");
#endif
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
		device_printf(sc->tl_dev, "got a dummy interrupt\n");
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
		device_printf(sc->tl_dev, "bogus interrupt type\n");
		break;
	}

	/* Re-enable interrupts */
	if (r) {
		CMD_PUT(sc, TL_CMD_ACK | r | type);
	}

	if (ifp->if_snd.ifq_head != NULL)
		tl_start_locked(ifp);

	TL_UNLOCK(sc);
}

static void
tl_stats_update(xsc)
	void			*xsc;
{
	struct tl_softc		*sc;
	struct ifnet		*ifp;
	struct tl_stats		tl_stats;
	struct mii_data		*mii;
	u_int32_t		*p;

	bzero((char *)&tl_stats, sizeof(struct tl_stats));

	sc = xsc;
	TL_LOCK_ASSERT(sc);
	ifp = sc->tl_ifp;

	p = (u_int32_t *)&tl_stats;

	CSR_WRITE_2(sc, TL_DIO_ADDR, TL_TXGOODFRAMES|TL_DIO_ADDR_INC);
	*p++ = CSR_READ_4(sc, TL_DIO_DATA);
	*p++ = CSR_READ_4(sc, TL_DIO_DATA);
	*p++ = CSR_READ_4(sc, TL_DIO_DATA);
	*p++ = CSR_READ_4(sc, TL_DIO_DATA);
	*p++ = CSR_READ_4(sc, TL_DIO_DATA);

	if_inc_counter(ifp, IFCOUNTER_OPACKETS, tl_tx_goodframes(tl_stats));
	if_inc_counter(ifp, IFCOUNTER_COLLISIONS,
	    tl_stats.tl_tx_single_collision + tl_stats.tl_tx_multi_collision);
	if_inc_counter(ifp, IFCOUNTER_IPACKETS, tl_rx_goodframes(tl_stats));
	if_inc_counter(ifp, IFCOUNTER_IERRORS, tl_stats.tl_crc_errors +
	    tl_stats.tl_code_errors + tl_rx_overrun(tl_stats));
	if_inc_counter(ifp, IFCOUNTER_OERRORS, tl_tx_underrun(tl_stats));

	if (tl_tx_underrun(tl_stats)) {
		u_int8_t		tx_thresh;
		tx_thresh = tl_dio_read8(sc, TL_ACOMMIT) & TL_AC_TXTHRESH;
		if (tx_thresh != TL_AC_TXTHRESH_WHOLEPKT) {
			tx_thresh >>= 4;
			tx_thresh++;
			device_printf(sc->tl_dev, "tx underrun -- increasing "
			    "tx threshold to %d bytes\n",
			    (64 * (tx_thresh * 4)));
			tl_dio_clrbit(sc, TL_ACOMMIT, TL_AC_TXTHRESH);
			tl_dio_setbit(sc, TL_ACOMMIT, tx_thresh << 4);
		}
	}

	if (sc->tl_timer > 0 && --sc->tl_timer == 0)
		tl_watchdog(sc);

	callout_reset(&sc->tl_stat_callout, hz, tl_stats_update, sc);

	if (!sc->tl_bitrate) {
		mii = device_get_softc(sc->tl_miibus);
		mii_tick(mii);
	}
}

/*
 * Encapsulate an mbuf chain in a list by coupling the mbuf data
 * pointers to the fragment pointers.
 */
static int
tl_encap(sc, c, m_head)
	struct tl_softc		*sc;
	struct tl_chain		*c;
	struct mbuf		*m_head;
{
	int			frag = 0;
	struct tl_frag		*f = NULL;
	int			total_len;
	struct mbuf		*m;
	struct ifnet		*ifp = sc->tl_ifp;

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
				vtophys(mtod(m, vm_offset_t));
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

		MGETHDR(m_new, M_NOWAIT, MT_DATA);
		if (m_new == NULL) {
			if_printf(ifp, "no memory for tx list\n");
			return(1);
		}
		if (m_head->m_pkthdr.len > MHLEN) {
			if (!(MCLGET(m_new, M_NOWAIT))) {
				m_freem(m_new);
				if_printf(ifp, "no memory for tx list\n");
				return(1);
			}
		}
		m_copydata(m_head, 0, m_head->m_pkthdr.len,	
					mtod(m_new, caddr_t));
		m_new->m_pkthdr.len = m_new->m_len = m_head->m_pkthdr.len;
		m_freem(m_head);
		m_head = m_new;
		f = &c->tl_ptr->tl_frag[0];
		f->tlist_dadr = vtophys(mtod(m_new, caddr_t));
		f->tlist_dcnt = total_len = m_new->m_len;
		frag = 1;
	}

	/*
	 * Special case #2: the frame is smaller than the minimum
	 * frame size. We have to pad it to make the chip happy.
	 */
	if (total_len < TL_MIN_FRAMELEN) {
		if (frag == TL_MAXFRAGS)
			if_printf(ifp,
			    "all frags filled but frame still to small!\n");
		f = &c->tl_ptr->tl_frag[frag];
		f->tlist_dcnt = TL_MIN_FRAMELEN - total_len;
		f->tlist_dadr = vtophys(&sc->tl_ldata->tl_pad);
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
static void
tl_start(ifp)
	struct ifnet		*ifp;
{
	struct tl_softc		*sc;

	sc = ifp->if_softc;
	TL_LOCK(sc);
	tl_start_locked(ifp);
	TL_UNLOCK(sc);
}

static void
tl_start_locked(ifp)
	struct ifnet		*ifp;
{
	struct tl_softc		*sc;
	struct mbuf		*m_head = NULL;
	u_int32_t		cmd;
	struct tl_chain		*prev = NULL, *cur_tx = NULL, *start_tx;

	sc = ifp->if_softc;
	TL_LOCK_ASSERT(sc);

	/*
	 * Check for an available queue slot. If there are none,
	 * punt.
	 */
	if (sc->tl_cdata.tl_tx_free == NULL) {
		ifp->if_drv_flags |= IFF_DRV_OACTIVE;
		return;
	}

	start_tx = sc->tl_cdata.tl_tx_free;

	while(sc->tl_cdata.tl_tx_free != NULL) {
		IF_DEQUEUE(&ifp->if_snd, m_head);
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
			prev->tl_ptr->tlist_fptr = vtophys(cur_tx->tl_ptr);
		}
		prev = cur_tx;

		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		BPF_MTAP(ifp, cur_tx->tl_mbuf);
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
			CSR_WRITE_4(sc, TL_CH_PARM, vtophys(start_tx->tl_ptr));
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
	sc->tl_timer = 5;
}

static void
tl_init(xsc)
	void			*xsc;
{
	struct tl_softc		*sc = xsc;

	TL_LOCK(sc);
	tl_init_locked(sc);
	TL_UNLOCK(sc);
}

static void
tl_init_locked(sc)
	struct tl_softc		*sc;
{
	struct ifnet		*ifp = sc->tl_ifp;
	struct mii_data		*mii;

	TL_LOCK_ASSERT(sc);

	ifp = sc->tl_ifp;

	/*
	 * Cancel pending I/O.
	 */
	tl_stop(sc);

	/* Initialize TX FIFO threshold */
	tl_dio_clrbit(sc, TL_ACOMMIT, TL_AC_TXTHRESH);
	tl_dio_setbit(sc, TL_ACOMMIT, TL_AC_TXTHRESH_16LONG);

        /* Set PCI burst size */
	tl_dio_write8(sc, TL_BSIZEREG, TL_RXBURST_16LONG|TL_TXBURST_16LONG);

	/*
	 * Set 'capture all frames' bit for promiscuous mode.
	 */
	if (ifp->if_flags & IFF_PROMISC)
		tl_dio_setbit(sc, TL_NETCMD, TL_CMD_CAF);
	else
		tl_dio_clrbit(sc, TL_NETCMD, TL_CMD_CAF);

	/*
	 * Set capture broadcast bit to capture broadcast frames.
	 */
	if (ifp->if_flags & IFF_BROADCAST)
		tl_dio_clrbit(sc, TL_NETCMD, TL_CMD_NOBRX);
	else
		tl_dio_setbit(sc, TL_NETCMD, TL_CMD_NOBRX);

	tl_dio_write16(sc, TL_MAXRX, MCLBYTES);

	/* Init our MAC address */
	tl_setfilt(sc, IF_LLADDR(sc->tl_ifp), 0);

	/* Init multicast filter, if needed. */
	tl_setmulti(sc);

	/* Init circular RX list. */
	if (tl_list_rx_init(sc) == ENOBUFS) {
		device_printf(sc->tl_dev,
		    "initialization failed: no memory for rx buffers\n");
		tl_stop(sc);
		return;
	}

	/* Init TX pointers. */
	tl_list_tx_init(sc);

	/* Enable PCI interrupts. */
	CMD_SET(sc, TL_CMD_INTSON);

	/* Load the address of the rx list */
	CMD_SET(sc, TL_CMD_RT);
	CSR_WRITE_4(sc, TL_CH_PARM, vtophys(&sc->tl_ldata->tl_rx_list[0]));

	if (!sc->tl_bitrate) {
		if (sc->tl_miibus != NULL) {
			mii = device_get_softc(sc->tl_miibus);
			mii_mediachg(mii);
		}
	} else {
		tl_ifmedia_upd(ifp);
	}

	/* Send the RX go command */
	CMD_SET(sc, TL_CMD_GO|TL_CMD_NES|TL_CMD_RT);

	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	/* Start the stats update counter */
	callout_reset(&sc->tl_stat_callout, hz, tl_stats_update, sc);
}

/*
 * Set media options.
 */
static int
tl_ifmedia_upd(ifp)
	struct ifnet		*ifp;
{
	struct tl_softc		*sc;
	struct mii_data		*mii = NULL;

	sc = ifp->if_softc;

	TL_LOCK(sc);
	if (sc->tl_bitrate)
		tl_setmode(sc, sc->ifmedia.ifm_media);
	else {
		mii = device_get_softc(sc->tl_miibus);
		mii_mediachg(mii);
	}
	TL_UNLOCK(sc);

	return(0);
}

/*
 * Report current media status.
 */
static void
tl_ifmedia_sts(ifp, ifmr)
	struct ifnet		*ifp;
	struct ifmediareq	*ifmr;
{
	struct tl_softc		*sc;
	struct mii_data		*mii;

	sc = ifp->if_softc;

	TL_LOCK(sc);
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
		mii = device_get_softc(sc->tl_miibus);
		mii_pollstat(mii);
		ifmr->ifm_active = mii->mii_media_active;
		ifmr->ifm_status = mii->mii_media_status;
	}
	TL_UNLOCK(sc);
}

static int
tl_ioctl(ifp, command, data)
	struct ifnet		*ifp;
	u_long			command;
	caddr_t			data;
{
	struct tl_softc		*sc = ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *) data;
	int			error = 0;

	switch(command) {
	case SIOCSIFFLAGS:
		TL_LOCK(sc);
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING &&
			    ifp->if_flags & IFF_PROMISC &&
			    !(sc->tl_if_flags & IFF_PROMISC)) {
				tl_dio_setbit(sc, TL_NETCMD, TL_CMD_CAF);
				tl_setmulti(sc);
			} else if (ifp->if_drv_flags & IFF_DRV_RUNNING &&
			    !(ifp->if_flags & IFF_PROMISC) &&
			    sc->tl_if_flags & IFF_PROMISC) {
				tl_dio_clrbit(sc, TL_NETCMD, TL_CMD_CAF);
				tl_setmulti(sc);
			} else
				tl_init_locked(sc);
		} else {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
				tl_stop(sc);
			}
		}
		sc->tl_if_flags = ifp->if_flags;
		TL_UNLOCK(sc);
		error = 0;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		TL_LOCK(sc);
		tl_setmulti(sc);
		TL_UNLOCK(sc);
		error = 0;
		break;
	case SIOCSIFMEDIA:
	case SIOCGIFMEDIA:
		if (sc->tl_bitrate)
			error = ifmedia_ioctl(ifp, ifr, &sc->ifmedia, command);
		else {
			struct mii_data		*mii;
			mii = device_get_softc(sc->tl_miibus);
			error = ifmedia_ioctl(ifp, ifr,
			    &mii->mii_media, command);
		}
		break;
	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}

	return(error);
}

static void
tl_watchdog(sc)
	struct tl_softc		*sc;
{
	struct ifnet		*ifp;

	TL_LOCK_ASSERT(sc);
	ifp = sc->tl_ifp;

	if_printf(ifp, "device timeout\n");

	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);

	tl_softreset(sc, 1);
	tl_init_locked(sc);
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
static void
tl_stop(sc)
	struct tl_softc		*sc;
{
	int			i;
	struct ifnet		*ifp;

	TL_LOCK_ASSERT(sc);

	ifp = sc->tl_ifp;

	/* Stop the stats updater. */
	callout_stop(&sc->tl_stat_callout);

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
	bzero((char *)&sc->tl_ldata->tl_rx_list,
		sizeof(sc->tl_ldata->tl_rx_list));

	/*
	 * Free the TX list buffers.
	 */
	for (i = 0; i < TL_TX_LIST_CNT; i++) {
		if (sc->tl_cdata.tl_tx_chain[i].tl_mbuf != NULL) {
			m_freem(sc->tl_cdata.tl_tx_chain[i].tl_mbuf);
			sc->tl_cdata.tl_tx_chain[i].tl_mbuf = NULL;
		}
	}
	bzero((char *)&sc->tl_ldata->tl_tx_list,
		sizeof(sc->tl_ldata->tl_tx_list));

	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
}

/*
 * Stop all chip I/O so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */
static int
tl_shutdown(dev)
	device_t		dev;
{
	struct tl_softc		*sc;

	sc = device_get_softc(dev);

	TL_LOCK(sc);
	tl_stop(sc);
	TL_UNLOCK(sc);

	return (0);
}
