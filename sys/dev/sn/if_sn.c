/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
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
 *      This product includes software developed by Gardner Buchanan.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * This is a driver for SMC's 9000 series of Ethernet adapters.
 *
 * This FreeBSD driver is derived from the smc9194 Linux driver by
 * Erik Stahlman and is Copyright (C) 1996 by Erik Stahlman.
 * This driver also shamelessly borrows from the FreeBSD ep driver
 * which is Copyright (C) 1994 Herb Peyerl <hpeyerl@novatel.ca>
 * All rights reserved.
 *
 * It is set up for my SMC91C92 equipped Ampro LittleBoard embedded
 * PC.  It is adapted from Erik Stahlman's Linux driver which worked
 * with his EFA Info*Express SVC VLB adaptor.  According to SMC's databook,
 * it will work for the entire SMC 9xxx series. (Ha Ha)
 *
 * "Features" of the SMC chip:
 *   4608 byte packet memory. (for the 91C92.  Others have more)
 *   EEPROM for configuration
 *   AUI/TP selection
 *
 * Authors:
 *      Erik Stahlman                   erik@vt.edu
 *      Herb Peyerl                     hpeyerl@novatel.ca
 *      Andres Vega Garcia              avega@sophia.inria.fr
 *      Serge Babkin                    babkin@hq.icb.chel.su
 *      Gardner Buchanan                gbuchanan@shl.com
 *
 * Sources:
 *    o   SMC databook
 *    o   "smc9194.c:v0.10(FIXED) 02/15/96 by Erik Stahlman (erik@vt.edu)"
 *    o   "if_ep.c,v 1.19 1995/01/24 20:53:45 davidg Exp"
 *
 * Known Bugs:
 *    o   Setting of the hardware address isn't supported.
 *    o   Hardware padding isn't used.
 */

/*
 * Modifications for Megahertz X-Jack Ethernet Card (XJ-10BT)
 * 
 * Copyright (c) 1996 by Tatsumi Hosokawa <hosokawa@jp.FreeBSD.org>
 *                       BSD-nomads, Tokyo, Japan.
 */
/*
 * Multicast support by Kei TANAKA <kei@pal.xerox.com>
 * Special thanks to itojun@itojun.org
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/sockio.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/syslog.h>

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
#include <net/if_types.h>
#include <net/if_mib.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#endif

#include <net/bpf.h>
#include <net/bpfdesc.h>

#include <dev/sn/if_snreg.h>
#include <dev/sn/if_snvar.h>

/* Exported variables */
devclass_t sn_devclass;

static int snioctl(struct ifnet * ifp, u_long, caddr_t);

static void snresume(struct ifnet *);

static void snintr_locked(struct sn_softc *);
static void sninit_locked(void *);
static void snstart_locked(struct ifnet *);

static void sninit(void *);
static void snread(struct ifnet *);
static void snstart(struct ifnet *);
static void snstop(struct sn_softc *);
static void snwatchdog(void *);

static void sn_setmcast(struct sn_softc *);
static int sn_getmcf(struct ifnet *ifp, u_char *mcf);

/* I (GB) have been unlucky getting the hardware padding
 * to work properly.
 */
#define SW_PAD

static const char *chip_ids[15] = {
	NULL, NULL, NULL,
	 /* 3 */ "SMC91C90/91C92",
	 /* 4 */ "SMC91C94/91C96",
	 /* 5 */ "SMC91C95",
	NULL,
	 /* 7 */ "SMC91C100",
	 /* 8 */ "SMC91C100FD",
	 /* 9 */ "SMC91C110",
	NULL, NULL,
	NULL, NULL, NULL
};

int
sn_attach(device_t dev)
{
	struct sn_softc *sc = device_get_softc(dev);
	struct ifnet    *ifp;
	uint16_t        i;
	uint8_t         *p;
	int             rev;
	uint16_t        address;
	int		err;
	u_char		eaddr[6];

	ifp = sc->ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "can not if_alloc()\n");
		return (ENOSPC);
	}

	SN_LOCK_INIT(sc);
	callout_init_mtx(&sc->watchdog, &sc->sc_mtx, 0);
	snstop(sc);
	sc->pages_wanted = -1;

	if (bootverbose || 1) {
		SMC_SELECT_BANK(sc, 3);
		rev = (CSR_READ_2(sc, REVISION_REG_W) >> 4) & 0xf;
		if (chip_ids[rev])
			device_printf(dev, " %s ", chip_ids[rev]);
		else
			device_printf(dev, " unsupported chip: rev %d ", rev);
		SMC_SELECT_BANK(sc, 1);
		i = CSR_READ_2(sc, CONFIG_REG_W);
		printf("%s\n", i & CR_AUI_SELECT ? "AUI" : "UTP");
	}

	/*
	 * Read the station address from the chip. The MAC address is bank 1,
	 * regs 4 - 9
	 */
	SMC_SELECT_BANK(sc, 1);
	p = (uint8_t *) eaddr;
	for (i = 0; i < 6; i += 2) {
		address = CSR_READ_2(sc, IAR_ADDR0_REG_W + i);
		p[i + 1] = address >> 8;
		p[i] = address & 0xFF;
	}
	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_start = snstart;
	ifp->if_ioctl = snioctl;
	ifp->if_init = sninit;
	ifp->if_baudrate = 10000000;
	IFQ_SET_MAXLEN(&ifp->if_snd, ifqmaxlen);
	ifp->if_snd.ifq_maxlen = ifqmaxlen;
	IFQ_SET_READY(&ifp->if_snd);

	ether_ifattach(ifp, eaddr);

	/*
	 * Activate the interrupt so we can get card interrupts.  This
	 * needs to be done last so that we don't have/hold the lock
	 * during startup to avoid LORs in the network layer.
	 */
	if ((err = bus_setup_intr(dev, sc->irq_res,
	    INTR_TYPE_NET | INTR_MPSAFE, NULL, sn_intr, sc, 
	    &sc->intrhand)) != 0) {
		sn_detach(dev);
		return err;
	}

	gone_by_fcp101_dev(dev);

	return 0;
}


int
sn_detach(device_t dev)
{
	struct sn_softc	*sc = device_get_softc(dev);
	struct ifnet	*ifp = sc->ifp;

	ether_ifdetach(ifp);
	SN_LOCK(sc);
	snstop(sc);
	SN_UNLOCK(sc);
	callout_drain(&sc->watchdog);
	sn_deactivate(dev);
	if_free(ifp);
	SN_LOCK_DESTROY(sc);
	return 0;
}

static void
sninit(void *xsc)
{
	struct sn_softc *sc = xsc;
	SN_LOCK(sc);
	sninit_locked(sc);
	SN_UNLOCK(sc);
}

/*
 * Reset and initialize the chip
 */
static void
sninit_locked(void *xsc)
{
	struct sn_softc *sc = xsc;
	struct ifnet *ifp = sc->ifp;
	int             flags;
	int             mask;

	SN_ASSERT_LOCKED(sc);

	/*
	 * This resets the registers mostly to defaults, but doesn't affect
	 * EEPROM.  After the reset cycle, we pause briefly for the chip to
	 * be happy.
	 */
	SMC_SELECT_BANK(sc, 0);
	CSR_WRITE_2(sc, RECV_CONTROL_REG_W, RCR_SOFTRESET);
	SMC_DELAY(sc);
	CSR_WRITE_2(sc, RECV_CONTROL_REG_W, 0x0000);
	SMC_DELAY(sc);
	SMC_DELAY(sc);

	CSR_WRITE_2(sc, TXMIT_CONTROL_REG_W, 0x0000);

	/*
	 * Set the control register to automatically release successfully
	 * transmitted packets (making the best use out of our limited
	 * memory) and to enable the EPH interrupt on certain TX errors.
	 */
	SMC_SELECT_BANK(sc, 1);
	CSR_WRITE_2(sc, CONTROL_REG_W, (CTR_AUTO_RELEASE | CTR_TE_ENABLE |
				    CTR_CR_ENABLE | CTR_LE_ENABLE));

	/* Set squelch level to 240mV (default 480mV) */
	flags = CSR_READ_2(sc, CONFIG_REG_W);
	flags |= CR_SET_SQLCH;
	CSR_WRITE_2(sc, CONFIG_REG_W, flags);

	/*
	 * Reset the MMU and wait for it to be un-busy.
	 */
	SMC_SELECT_BANK(sc, 2);
	CSR_WRITE_2(sc, MMU_CMD_REG_W, MMUCR_RESET);
	while (CSR_READ_2(sc, MMU_CMD_REG_W) & MMUCR_BUSY)	/* NOTHING */
		;

	/*
	 * Disable all interrupts
	 */
	CSR_WRITE_1(sc, INTR_MASK_REG_B, 0x00);

	sn_setmcast(sc);

	/*
	 * Set the transmitter control.  We want it enabled.
	 */
	flags = TCR_ENABLE;

#ifndef SW_PAD
	/*
	 * I (GB) have been unlucky getting this to work.
	 */
	flags |= TCR_PAD_ENABLE;
#endif	/* SW_PAD */

	CSR_WRITE_2(sc, TXMIT_CONTROL_REG_W, flags);


	/*
	 * Now, enable interrupts
	 */
	SMC_SELECT_BANK(sc, 2);

	mask = IM_EPH_INT |
		IM_RX_OVRN_INT |
		IM_RCV_INT |
		IM_TX_INT;

	CSR_WRITE_1(sc, INTR_MASK_REG_B, mask);
	sc->intr_mask = mask;
	sc->pages_wanted = -1;


	/*
	 * Mark the interface running but not active.
	 */
	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	callout_reset(&sc->watchdog, hz, snwatchdog, sc);

	/*
	 * Attempt to push out any waiting packets.
	 */
	snstart_locked(ifp);
}

static void
snstart(struct ifnet *ifp)
{
	struct sn_softc *sc = ifp->if_softc;
	SN_LOCK(sc);
	snstart_locked(ifp);
	SN_UNLOCK(sc);
}


static void
snstart_locked(struct ifnet *ifp)
{
	struct sn_softc *sc = ifp->if_softc;
	u_int		len;
	struct mbuf	*m;
	struct mbuf	*top;
	int             pad;
	int             mask;
	uint16_t        length;
	uint16_t        numPages;
	uint8_t         packet_no;
	int             time_out;
	int		junk = 0;

	SN_ASSERT_LOCKED(sc);

	if (ifp->if_drv_flags & IFF_DRV_OACTIVE)
		return;
	if (sc->pages_wanted != -1) {
		if_printf(ifp, "snstart() while memory allocation pending\n");
		return;
	}
startagain:

	/*
	 * Sneak a peek at the next packet
	 */
	m = ifp->if_snd.ifq_head;
	if (m == NULL)
		return;
	/*
	 * Compute the frame length and set pad to give an overall even
	 * number of bytes.  Below we assume that the packet length is even.
	 */
	for (len = 0, top = m; m; m = m->m_next)
		len += m->m_len;

	pad = (len & 1);

	/*
	 * We drop packets that are too large. Perhaps we should truncate
	 * them instead?
	 */
	if (len + pad > ETHER_MAX_LEN - ETHER_CRC_LEN) {
		if_printf(ifp, "large packet discarded (A)\n");
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m);
		m_freem(m);
		goto readcheck;
	}
#ifdef SW_PAD

	/*
	 * If HW padding is not turned on, then pad to ETHER_MIN_LEN.
	 */
	if (len < ETHER_MIN_LEN - ETHER_CRC_LEN)
		pad = ETHER_MIN_LEN - ETHER_CRC_LEN - len;

#endif	/* SW_PAD */

	length = pad + len;

	/*
	 * The MMU wants the number of pages to be the number of 256 byte
	 * 'pages', minus 1 (A packet can't ever have 0 pages. We also
	 * include space for the status word, byte count and control bytes in
	 * the allocation request.
	 */
	numPages = (length + 6) >> 8;


	/*
	 * Now, try to allocate the memory
	 */
	SMC_SELECT_BANK(sc, 2);
	CSR_WRITE_2(sc, MMU_CMD_REG_W, MMUCR_ALLOC | numPages);

	/*
	 * Wait a short amount of time to see if the allocation request
	 * completes.  Otherwise, I enable the interrupt and wait for
	 * completion asynchronously.
	 */

	time_out = MEMORY_WAIT_TIME;
	do {
		if (CSR_READ_1(sc, INTR_STAT_REG_B) & IM_ALLOC_INT)
			break;
	} while (--time_out);

	if (!time_out || junk > 10) {

		/*
		 * No memory now.  Oh well, wait until the chip finds memory
		 * later.   Remember how many pages we were asking for and
		 * enable the allocation completion interrupt. Also set a
		 * watchdog in case  we miss the interrupt. We mark the
		 * interface active since there is no point in attempting an
		 * snstart() until after the memory is available.
		 */
		mask = CSR_READ_1(sc, INTR_MASK_REG_B) | IM_ALLOC_INT;
		CSR_WRITE_1(sc, INTR_MASK_REG_B, mask);
		sc->intr_mask = mask;

		sc->timer = 1;
		ifp->if_drv_flags |= IFF_DRV_OACTIVE;
		sc->pages_wanted = numPages;
		return;
	}
	/*
	 * The memory allocation completed.  Check the results.
	 */
	packet_no = CSR_READ_1(sc, ALLOC_RESULT_REG_B);
	if (packet_no & ARR_FAILED) {
		if (junk++ > 10)
			if_printf(ifp, "Memory allocation failed\n");
		goto startagain;
	}
	/*
	 * We have a packet number, so tell the card to use it.
	 */
	CSR_WRITE_1(sc, PACKET_NUM_REG_B, packet_no);

	/*
	 * Point to the beginning of the packet
	 */
	CSR_WRITE_2(sc, POINTER_REG_W, PTR_AUTOINC | 0x0000);

	/*
	 * Send the packet length (+6 for status, length and control byte)
	 * and the status word (set to zeros)
	 */
	CSR_WRITE_2(sc, DATA_REG_W, 0);
	CSR_WRITE_1(sc, DATA_REG_B, (length + 6) & 0xFF);
	CSR_WRITE_1(sc, DATA_REG_B, (length + 6) >> 8);

	/*
	 * Get the packet from the kernel.  This will include the Ethernet
	 * frame header, MAC Addresses etc.
	 */
	IFQ_DRV_DEQUEUE(&ifp->if_snd, m);

	/*
	 * Push out the data to the card.
	 */
	for (top = m; m != NULL; m = m->m_next) {

		/*
		 * Push out words.
		 */
		CSR_WRITE_MULTI_2(sc, DATA_REG_W, mtod(m, uint16_t *),
		    m->m_len / 2);

		/*
		 * Push out remaining byte.
		 */
		if (m->m_len & 1)
			CSR_WRITE_1(sc, DATA_REG_B,
			    *(mtod(m, caddr_t) + m->m_len - 1));
	}

	/*
	 * Push out padding.
	 */
	while (pad > 1) {
		CSR_WRITE_2(sc, DATA_REG_W, 0);
		pad -= 2;
	}
	if (pad)
		CSR_WRITE_1(sc, DATA_REG_B, 0);

	/*
	 * Push out control byte and unused packet byte The control byte is 0
	 * meaning the packet is even lengthed and no special CRC handling is
	 * desired.
	 */
	CSR_WRITE_2(sc, DATA_REG_W, 0);

	/*
	 * Enable the interrupts and let the chipset deal with it Also set a
	 * watchdog in case we miss the interrupt.
	 */
	mask = CSR_READ_1(sc, INTR_MASK_REG_B) | (IM_TX_INT | IM_TX_EMPTY_INT);
	CSR_WRITE_1(sc, INTR_MASK_REG_B, mask);
	sc->intr_mask = mask;

	CSR_WRITE_2(sc, MMU_CMD_REG_W, MMUCR_ENQUEUE);

	ifp->if_drv_flags |= IFF_DRV_OACTIVE;
	sc->timer = 1;

	BPF_MTAP(ifp, top);

	if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
	m_freem(top);


readcheck:

	/*
	 * Is another packet coming in?  We don't want to overflow the tiny
	 * RX FIFO.  If nothing has arrived then attempt to queue another
	 * transmit packet.
	 */
	if (CSR_READ_2(sc, FIFO_PORTS_REG_W) & FIFO_REMPTY)
		goto startagain;
	return;
}



/* Resume a packet transmit operation after a memory allocation
 * has completed.
 *
 * This is basically a hacked up copy of snstart() which handles
 * a completed memory allocation the same way snstart() does.
 * It then passes control to snstart to handle any other queued
 * packets.
 */
static void
snresume(struct ifnet *ifp)
{
	struct sn_softc *sc = ifp->if_softc;
	u_int		len;
	struct mbuf	*m;
	struct mbuf    *top;
	int             pad;
	int             mask;
	uint16_t        length;
	uint16_t        numPages;
	uint16_t        pages_wanted;
	uint8_t         packet_no;

	if (sc->pages_wanted < 0)
		return;

	pages_wanted = sc->pages_wanted;
	sc->pages_wanted = -1;

	/*
	 * Sneak a peek at the next packet
	 */
	m = ifp->if_snd.ifq_head;
	if (m == NULL) {
		if_printf(ifp, "snresume() with nothing to send\n");
		return;
	}
	/*
	 * Compute the frame length and set pad to give an overall even
	 * number of bytes.  Below we assume that the packet length is even.
	 */
	for (len = 0, top = m; m; m = m->m_next)
		len += m->m_len;

	pad = (len & 1);

	/*
	 * We drop packets that are too large. Perhaps we should truncate
	 * them instead?
	 */
	if (len + pad > ETHER_MAX_LEN - ETHER_CRC_LEN) {
		if_printf(ifp, "large packet discarded (B)\n");
		if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		IFQ_DRV_DEQUEUE(&ifp->if_snd, m);
		m_freem(m);
		return;
	}
#ifdef SW_PAD

	/*
	 * If HW padding is not turned on, then pad to ETHER_MIN_LEN.
	 */
	if (len < ETHER_MIN_LEN - ETHER_CRC_LEN)
		pad = ETHER_MIN_LEN - ETHER_CRC_LEN - len;

#endif	/* SW_PAD */

	length = pad + len;


	/*
	 * The MMU wants the number of pages to be the number of 256 byte
	 * 'pages', minus 1 (A packet can't ever have 0 pages. We also
	 * include space for the status word, byte count and control bytes in
	 * the allocation request.
	 */
	numPages = (length + 6) >> 8;


	SMC_SELECT_BANK(sc, 2);

	/*
	 * The memory allocation completed.  Check the results. If it failed,
	 * we simply set a watchdog timer and hope for the best.
	 */
	packet_no = CSR_READ_1(sc, ALLOC_RESULT_REG_B);
	if (packet_no & ARR_FAILED) {
		if_printf(ifp, "Memory allocation failed.  Weird.\n");
		sc->timer = 1;
		goto try_start;
	}
	/*
	 * We have a packet number, so tell the card to use it.
	 */
	CSR_WRITE_1(sc, PACKET_NUM_REG_B, packet_no);

	/*
	 * Now, numPages should match the pages_wanted recorded when the
	 * memory allocation was initiated.
	 */
	if (pages_wanted != numPages) {
		if_printf(ifp, "memory allocation wrong size.  Weird.\n");
		/*
		 * If the allocation was the wrong size we simply release the
		 * memory once it is granted. Wait for the MMU to be un-busy.
		 */
		while (CSR_READ_2(sc, MMU_CMD_REG_W) & MMUCR_BUSY)	/* NOTHING */
			;
		CSR_WRITE_2(sc, MMU_CMD_REG_W, MMUCR_FREEPKT);

		return;
	}
	/*
	 * Point to the beginning of the packet
	 */
	CSR_WRITE_2(sc, POINTER_REG_W, PTR_AUTOINC | 0x0000);

	/*
	 * Send the packet length (+6 for status, length and control byte)
	 * and the status word (set to zeros)
	 */
	CSR_WRITE_2(sc, DATA_REG_W, 0);
	CSR_WRITE_1(sc, DATA_REG_B, (length + 6) & 0xFF);
	CSR_WRITE_1(sc, DATA_REG_B, (length + 6) >> 8);

	/*
	 * Get the packet from the kernel.  This will include the Ethernet
	 * frame header, MAC Addresses etc.
	 */
	IFQ_DRV_DEQUEUE(&ifp->if_snd, m);

	/*
	 * Push out the data to the card.
	 */
	for (top = m; m != NULL; m = m->m_next) {

		/*
		 * Push out words.
		 */
		CSR_WRITE_MULTI_2(sc, DATA_REG_W, mtod(m, uint16_t *),
		    m->m_len / 2);
		/*
		 * Push out remaining byte.
		 */
		if (m->m_len & 1)
			CSR_WRITE_1(sc, DATA_REG_B,
			    *(mtod(m, caddr_t) + m->m_len - 1));
	}

	/*
	 * Push out padding.
	 */
	while (pad > 1) {
		CSR_WRITE_2(sc, DATA_REG_W, 0);
		pad -= 2;
	}
	if (pad)
		CSR_WRITE_1(sc, DATA_REG_B, 0);

	/*
	 * Push out control byte and unused packet byte The control byte is 0
	 * meaning the packet is even lengthed and no special CRC handling is
	 * desired.
	 */
	CSR_WRITE_2(sc, DATA_REG_W, 0);

	/*
	 * Enable the interrupts and let the chipset deal with it Also set a
	 * watchdog in case we miss the interrupt.
	 */
	mask = CSR_READ_1(sc, INTR_MASK_REG_B) | (IM_TX_INT | IM_TX_EMPTY_INT);
	CSR_WRITE_1(sc, INTR_MASK_REG_B, mask);
	sc->intr_mask = mask;
	CSR_WRITE_2(sc, MMU_CMD_REG_W, MMUCR_ENQUEUE);

	BPF_MTAP(ifp, top);

	if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
	m_freem(top);

try_start:

	/*
	 * Now pass control to snstart() to queue any additional packets
	 */
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
	snstart_locked(ifp);

	/*
	 * We've sent something, so we're active.  Set a watchdog in case the
	 * TX_EMPTY interrupt is lost.
	 */
	ifp->if_drv_flags |= IFF_DRV_OACTIVE;
	sc->timer = 1;

	return;
}

void
sn_intr(void *arg)
{
	struct sn_softc *sc = (struct sn_softc *) arg;

	SN_LOCK(sc);
	snintr_locked(sc);
	SN_UNLOCK(sc);
}

static void
snintr_locked(struct sn_softc *sc)
{
	int             status, interrupts;
	struct ifnet   *ifp = sc->ifp;

	/*
	 * Chip state registers
	 */
	uint8_t          mask;
	uint8_t         packet_no;
	uint16_t        tx_status;
	uint16_t        card_stats;

	/*
	 * Clear the watchdog.
	 */
	sc->timer = 0;

	SMC_SELECT_BANK(sc, 2);

	/*
	 * Obtain the current interrupt mask and clear the hardware mask
	 * while servicing interrupts.
	 */
	mask = CSR_READ_1(sc, INTR_MASK_REG_B);
	CSR_WRITE_1(sc, INTR_MASK_REG_B, 0x00);

	/*
	 * Get the set of interrupts which occurred and eliminate any which
	 * are masked.
	 */
	interrupts = CSR_READ_1(sc, INTR_STAT_REG_B);
	status = interrupts & mask;

	/*
	 * Now, process each of the interrupt types.
	 */

	/*
	 * Receive Overrun.
	 */
	if (status & IM_RX_OVRN_INT) {
		/*
		 * Acknowlege Interrupt
		 */
		SMC_SELECT_BANK(sc, 2);
		CSR_WRITE_1(sc, INTR_ACK_REG_B, IM_RX_OVRN_INT);

		if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
	}
	/*
	 * Got a packet.
	 */
	if (status & IM_RCV_INT) {
		int             packet_number;

		SMC_SELECT_BANK(sc, 2);
		packet_number = CSR_READ_2(sc, FIFO_PORTS_REG_W);

		if (packet_number & FIFO_REMPTY) {
			/*
			 * we got called , but nothing was on the FIFO
			 */
			printf("sn: Receive interrupt with nothing on FIFO\n");
			goto out;
		}
		snread(ifp);
	}
	/*
	 * An on-card memory allocation came through.
	 */
	if (status & IM_ALLOC_INT) {
		/*
		 * Disable this interrupt.
		 */
		mask &= ~IM_ALLOC_INT;
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		snresume(ifp);
	}
	/*
	 * TX Completion.  Handle a transmit error message. This will only be
	 * called when there is an error, because of the AUTO_RELEASE mode.
	 */
	if (status & IM_TX_INT) {
		/*
		 * Acknowlege Interrupt
		 */
		SMC_SELECT_BANK(sc, 2);
		CSR_WRITE_1(sc, INTR_ACK_REG_B, IM_TX_INT);

		packet_no = CSR_READ_2(sc, FIFO_PORTS_REG_W);
		packet_no &= FIFO_TX_MASK;

		/*
		 * select this as the packet to read from
		 */
		CSR_WRITE_1(sc, PACKET_NUM_REG_B, packet_no);

		/*
		 * Position the pointer to the first word from this packet
		 */
		CSR_WRITE_2(sc, POINTER_REG_W, PTR_AUTOINC | PTR_READ | 0x0000);

		/*
		 * Fetch the TX status word.  The value found here will be a
		 * copy of the EPH_STATUS_REG_W at the time the transmit
		 * failed.
		 */
		tx_status = CSR_READ_2(sc, DATA_REG_W);

		if (tx_status & EPHSR_TX_SUC) {
			device_printf(sc->dev, 
			    "Successful packet caused interrupt\n");
		} else {
			if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
		}

		if (tx_status & EPHSR_LATCOL)
			if_inc_counter(ifp, IFCOUNTER_COLLISIONS, 1);

		/*
		 * Some of these errors will have disabled transmit.
		 * Re-enable transmit now.
		 */
		SMC_SELECT_BANK(sc, 0);

#ifdef SW_PAD
		CSR_WRITE_2(sc, TXMIT_CONTROL_REG_W, TCR_ENABLE);
#else
		CSR_WRITE_2(sc, TXMIT_CONTROL_REG_W, TCR_ENABLE | TCR_PAD_ENABLE);
#endif	/* SW_PAD */

		/*
		 * kill the failed packet. Wait for the MMU to be un-busy.
		 */
		SMC_SELECT_BANK(sc, 2);
		while (CSR_READ_2(sc, MMU_CMD_REG_W) & MMUCR_BUSY)	/* NOTHING */
			;
		CSR_WRITE_2(sc, MMU_CMD_REG_W, MMUCR_FREEPKT);

		/*
		 * Attempt to queue more transmits.
		 */
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		snstart_locked(ifp);
	}
	/*
	 * Transmit underrun.  We use this opportunity to update transmit
	 * statistics from the card.
	 */
	if (status & IM_TX_EMPTY_INT) {

		/*
		 * Acknowlege Interrupt
		 */
		SMC_SELECT_BANK(sc, 2);
		CSR_WRITE_1(sc, INTR_ACK_REG_B, IM_TX_EMPTY_INT);

		/*
		 * Disable this interrupt.
		 */
		mask &= ~IM_TX_EMPTY_INT;

		SMC_SELECT_BANK(sc, 0);
		card_stats = CSR_READ_2(sc, COUNTER_REG_W);

		/*
		 * Single collisions
		 */
		if_inc_counter(ifp, IFCOUNTER_COLLISIONS, card_stats & ECR_COLN_MASK);

		/*
		 * Multiple collisions
		 */
		if_inc_counter(ifp, IFCOUNTER_COLLISIONS, (card_stats & ECR_MCOLN_MASK) >> 4);

		SMC_SELECT_BANK(sc, 2);

		/*
		 * Attempt to enqueue some more stuff.
		 */
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		snstart_locked(ifp);
	}
	/*
	 * Some other error.  Try to fix it by resetting the adapter.
	 */
	if (status & IM_EPH_INT) {
		snstop(sc);
		sninit_locked(sc);
	}

out:
	/*
	 * Handled all interrupt sources.
	 */

	SMC_SELECT_BANK(sc, 2);

	/*
	 * Reestablish interrupts from mask which have not been deselected
	 * during this interrupt.  Note that the hardware mask, which was set
	 * to 0x00 at the start of this service routine, may have been
	 * updated by one or more of the interrupt handers and we must let
	 * those new interrupts stay enabled here.
	 */
	mask |= CSR_READ_1(sc, INTR_MASK_REG_B);
	CSR_WRITE_1(sc, INTR_MASK_REG_B, mask);
	sc->intr_mask = mask;
}

static void
snread(struct ifnet *ifp)
{
        struct sn_softc *sc = ifp->if_softc;
	struct ether_header *eh;
	struct mbuf    *m;
	short           status;
	int             packet_number;
	uint16_t        packet_length;
	uint8_t        *data;

	SMC_SELECT_BANK(sc, 2);
#if 0
	packet_number = CSR_READ_2(sc, FIFO_PORTS_REG_W);

	if (packet_number & FIFO_REMPTY) {

		/*
		 * we got called , but nothing was on the FIFO
		 */
		printf("sn: Receive interrupt with nothing on FIFO\n");
		return;
	}
#endif
read_another:

	/*
	 * Start reading from the start of the packet. Since PTR_RCV is set,
	 * packet number is found in FIFO_PORTS_REG_W, FIFO_RX_MASK.
	 */
	CSR_WRITE_2(sc, POINTER_REG_W, PTR_READ | PTR_RCV | PTR_AUTOINC | 0x0000);

	/*
	 * First two words are status and packet_length
	 */
	status = CSR_READ_2(sc, DATA_REG_W);
	packet_length = CSR_READ_2(sc, DATA_REG_W) & RLEN_MASK;

	/*
	 * The packet length contains 3 extra words: status, length, and a
	 * extra word with the control byte.
	 */
	packet_length -= 6;

	/*
	 * Account for receive errors and discard.
	 */
	if (status & RS_ERRORS) {
		if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		goto out;
	}
	/*
	 * A packet is received.
	 */

	/*
	 * Adjust for odd-length packet.
	 */
	if (status & RS_ODDFRAME)
		packet_length++;

	/*
	 * Allocate a header mbuf from the kernel.
	 */
	MGETHDR(m, M_NOWAIT, MT_DATA);
	if (m == NULL)
		goto out;

	m->m_pkthdr.rcvif = ifp;
	m->m_pkthdr.len = m->m_len = packet_length;

	/*
	 * Attach an mbuf cluster.
	 */
	if (!(MCLGET(m, M_NOWAIT))) {
		m_freem(m);
		if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
		printf("sn: snread() kernel memory allocation problem\n");
		goto out;
	}
	eh = mtod(m, struct ether_header *);

	/*
	 * Get packet, including link layer address, from interface.
	 */
	data = (uint8_t *) eh;
	CSR_READ_MULTI_2(sc, DATA_REG_W, (uint16_t *) data, packet_length >> 1);
	if (packet_length & 1) {
		data += packet_length & ~1;
		*data = CSR_READ_1(sc, DATA_REG_B);
	}
	if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);

	/*
	 * Remove link layer addresses and whatnot.
	 */
	m->m_pkthdr.len = m->m_len = packet_length;

	/*
	 * Drop locks before calling if_input() since it may re-enter
	 * snstart() in the netisr case.  This would result in a
	 * lock reversal.  Better performance might be obtained by
	 * chaining all packets received, dropping the lock, and then
	 * calling if_input() on each one.
	 */
	SN_UNLOCK(sc);
	(*ifp->if_input)(ifp, m);
	SN_LOCK(sc);

out:

	/*
	 * Error or good, tell the card to get rid of this packet Wait for
	 * the MMU to be un-busy.
	 */
	SMC_SELECT_BANK(sc, 2);
	while (CSR_READ_2(sc, MMU_CMD_REG_W) & MMUCR_BUSY)	/* NOTHING */
		;
	CSR_WRITE_2(sc, MMU_CMD_REG_W, MMUCR_RELEASE);

	/*
	 * Check whether another packet is ready
	 */
	packet_number = CSR_READ_2(sc, FIFO_PORTS_REG_W);
	if (packet_number & FIFO_REMPTY) {
		return;
	}
	goto read_another;
}


/*
 * Handle IOCTLS.  This function is completely stolen from if_ep.c
 * As with its progenitor, it does not handle hardware address
 * changes.
 */
static int
snioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct sn_softc *sc = ifp->if_softc;
	int             error = 0;

	switch (cmd) {
	case SIOCSIFFLAGS:
		SN_LOCK(sc);
		if ((ifp->if_flags & IFF_UP) == 0 &&
		    ifp->if_drv_flags & IFF_DRV_RUNNING) {
			snstop(sc);
		} else {
			/* reinitialize card on any parameter change */
			sninit_locked(sc);
		}
		SN_UNLOCK(sc);
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		/* update multicast filter list. */
		SN_LOCK(sc);
		sn_setmcast(sc);
		error = 0;
		SN_UNLOCK(sc);
		break;
	default:
		error = ether_ioctl(ifp, cmd, data);
		break;
	}
	return (error);
}

static void
snwatchdog(void *arg)
{
	struct sn_softc *sc;

	sc = arg;
	SN_ASSERT_LOCKED(sc);
	callout_reset(&sc->watchdog, hz, snwatchdog, sc);
	if (sc->timer == 0 || --sc->timer > 0)
		return;
	snintr_locked(sc);
}


/* 1. zero the interrupt mask
 * 2. clear the enable receive flag
 * 3. clear the enable xmit flags
 */
static void
snstop(struct sn_softc *sc)
{
	
	struct ifnet   *ifp = sc->ifp;

	/*
	 * Clear interrupt mask; disable all interrupts.
	 */
	SMC_SELECT_BANK(sc, 2);
	CSR_WRITE_1(sc, INTR_MASK_REG_B, 0x00);

	/*
	 * Disable transmitter and Receiver
	 */
	SMC_SELECT_BANK(sc, 0);
	CSR_WRITE_2(sc, RECV_CONTROL_REG_W, 0x0000);
	CSR_WRITE_2(sc, TXMIT_CONTROL_REG_W, 0x0000);

	/*
	 * Cancel watchdog.
	 */
	sc->timer = 0;
	callout_stop(&sc->watchdog);
	ifp->if_drv_flags &= ~(IFF_DRV_RUNNING | IFF_DRV_OACTIVE);
}


int
sn_activate(device_t dev)
{
	struct sn_softc *sc = device_get_softc(dev);

	sc->port_rid = 0;
	sc->port_res = bus_alloc_resource_anywhere(dev, SYS_RES_IOPORT,
	    &sc->port_rid, SMC_IO_EXTENT, RF_ACTIVE);
	if (!sc->port_res) {
		if (bootverbose)
			device_printf(dev, "Cannot allocate ioport\n");
		return ENOMEM;
	}

	sc->irq_rid = 0;
	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->irq_rid, 
	    RF_ACTIVE);
	if (!sc->irq_res) {
		if (bootverbose)
			device_printf(dev, "Cannot allocate irq\n");
		sn_deactivate(dev);
		return ENOMEM;
	}
	return (0);
}

void
sn_deactivate(device_t dev)
{
	struct sn_softc *sc = device_get_softc(dev);
	
	if (sc->intrhand)
		bus_teardown_intr(dev, sc->irq_res, sc->intrhand);
	sc->intrhand = 0;
	if (sc->port_res)
		bus_release_resource(dev, SYS_RES_IOPORT, sc->port_rid, 
		    sc->port_res);
	sc->port_res = 0;
	if (sc->modem_res)
		bus_release_resource(dev, SYS_RES_IOPORT, sc->modem_rid, 
		    sc->modem_res);
	sc->modem_res = 0;
	if (sc->irq_res)
		bus_release_resource(dev, SYS_RES_IRQ, sc->irq_rid, 
		    sc->irq_res);
	sc->irq_res = 0;
	return;
}

/*
 * Function: sn_probe(device_t dev)
 *
 * Purpose:
 *      Tests to see if a given ioaddr points to an SMC9xxx chip.
 *      Tries to cause as little damage as possible if it's not a SMC chip.
 *      Returns a 0 on success
 *
 * Algorithm:
 *      (1) see if the high byte of BANK_SELECT is 0x33
 *      (2) compare the ioaddr with the base register's address
 *      (3) see if I recognize the chip ID in the appropriate register
 *
 *
 */
int 
sn_probe(device_t dev)
{
	struct sn_softc *sc = device_get_softc(dev);
	uint16_t        bank;
	uint16_t        revision_register;
	uint16_t        base_address_register;
	int		err;

	if ((err = sn_activate(dev)) != 0)
		return err;

	/*
	 * First, see if the high byte is 0x33
	 */
	bank = CSR_READ_2(sc, BANK_SELECT_REG_W);
	if ((bank & BSR_DETECT_MASK) != BSR_DETECT_VALUE) {
#ifdef	SN_DEBUG
		device_printf(dev, "test1 failed\n");
#endif
		goto error;
	}
	/*
	 * The above MIGHT indicate a device, but I need to write to further
	 * test this.  Go to bank 0, then test that the register still
	 * reports the high byte is 0x33.
	 */
	CSR_WRITE_2(sc, BANK_SELECT_REG_W, 0x0000);
	bank = CSR_READ_2(sc, BANK_SELECT_REG_W);
	if ((bank & BSR_DETECT_MASK) != BSR_DETECT_VALUE) {
#ifdef	SN_DEBUG
		device_printf(dev, "test2 failed\n");
#endif
		goto error;
	}
	/*
	 * well, we've already written once, so hopefully another time won't
	 * hurt.  This time, I need to switch the bank register to bank 1, so
	 * I can access the base address register.  The contents of the
	 * BASE_ADDR_REG_W register, after some jiggery pokery, is expected
	 * to match the I/O port address where the adapter is being probed.
	 */
	CSR_WRITE_2(sc, BANK_SELECT_REG_W, 0x0001);
	base_address_register = (CSR_READ_2(sc, BASE_ADDR_REG_W) >> 3) & 0x3e0;

	if (rman_get_start(sc->port_res) != base_address_register) {

		/*
		 * Well, the base address register didn't match.  Must not
		 * have been a SMC chip after all.
		 */
#ifdef	SN_DEBUG
		device_printf(dev, "test3 failed ioaddr = 0x%x, "
		    "base_address_register = 0x%x\n",
		    rman_get_start(sc->port_res), base_address_register);
#endif
		goto error;
	}

	/*
	 * Check if the revision register is something that I recognize.
	 * These might need to be added to later, as future revisions could
	 * be added.
	 */
	CSR_WRITE_2(sc, BANK_SELECT_REG_W, 0x3);
	revision_register = CSR_READ_2(sc, REVISION_REG_W);
	if (!chip_ids[(revision_register >> 4) & 0xF]) {

		/*
		 * I don't regonize this chip, so...
		 */
#ifdef	SN_DEBUG
		device_printf(dev, "test4 failed\n");
#endif
		goto error;
	}

	/*
	 * at this point I'll assume that the chip is an SMC9xxx. It might be
	 * prudent to check a listing of MAC addresses against the hardware
	 * address, or do some other tests.
	 */
	sn_deactivate(dev);
	return 0;
 error:
	sn_deactivate(dev);
	return ENXIO;
}

#define MCFSZ 8

static void
sn_setmcast(struct sn_softc *sc)
{
	struct ifnet *ifp = sc->ifp;
	int flags;
	uint8_t mcf[MCFSZ];

	SN_ASSERT_LOCKED(sc);

	/*
	 * Set the receiver filter.  We want receive enabled and auto strip
	 * of CRC from received packet.  If we are promiscuous then set that
	 * bit too.
	 */
	flags = RCR_ENABLE | RCR_STRIP_CRC;
  
	if (ifp->if_flags & IFF_PROMISC) {
		flags |= RCR_PROMISC | RCR_ALMUL;
	} else if (ifp->if_flags & IFF_ALLMULTI) {
		flags |= RCR_ALMUL;
	} else {
		if (sn_getmcf(ifp, mcf)) {
			/* set filter */
			SMC_SELECT_BANK(sc, 3);
			CSR_WRITE_2(sc, MULTICAST1_REG_W,
			    ((uint16_t)mcf[1] << 8) |  mcf[0]);
			CSR_WRITE_2(sc, MULTICAST2_REG_W,
			    ((uint16_t)mcf[3] << 8) |  mcf[2]);
			CSR_WRITE_2(sc, MULTICAST3_REG_W,
			    ((uint16_t)mcf[5] << 8) |  mcf[4]);
			CSR_WRITE_2(sc, MULTICAST4_REG_W,
			    ((uint16_t)mcf[7] << 8) |  mcf[6]);
		} else {
			flags |= RCR_ALMUL;
		}
	}
	SMC_SELECT_BANK(sc, 0);
	CSR_WRITE_2(sc, RECV_CONTROL_REG_W, flags);
}

static int
sn_getmcf(struct ifnet *ifp, uint8_t *mcf)
{
	int i;
	uint32_t index, index2;
	uint8_t *af = mcf;
	struct ifmultiaddr *ifma;

	bzero(mcf, MCFSZ);

	if_maddr_rlock(ifp);
	CK_STAILQ_FOREACH(ifma, &ifp->if_multiaddrs, ifma_link) {
	    if (ifma->ifma_addr->sa_family != AF_LINK) {
		if_maddr_runlock(ifp);
		return 0;
	    }
	    index = ether_crc32_le(LLADDR((struct sockaddr_dl *)
		ifma->ifma_addr), ETHER_ADDR_LEN) & 0x3f;
	    index2 = 0;
	    for (i = 0; i < 6; i++) {
		index2 <<= 1;
		index2 |= (index & 0x01);
		index >>= 1;
	    }
	    af[index2 >> 3] |= 1 << (index2 & 7);
	}
	if_maddr_runlock(ifp);
	return 1;  /* use multicast filter */
}
