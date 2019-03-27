/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1995, David Greenman
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Device driver for National Semiconductor DS8390/WD83C690 based ethernet
 *   adapters. By David Greenman, 29-April-1993
 *
 * Currently supports the Western Digital/SMC 8003 and 8013 series,
 *   the SMC Elite Ultra (8216), the 3Com 3c503, the NE1000 and NE2000,
 *   and a variety of similar clones.
 *
 */

#include "opt_ed.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/syslog.h>

#include <sys/bus.h>

#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_mib.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <net/bpf.h>

#include <dev/ed/if_edreg.h>
#include <dev/ed/if_edvar.h>
#include <sys/kdb.h>

devclass_t ed_devclass;

static void	ed_init(void *);
static void	ed_init_locked(struct ed_softc *);
static int	ed_ioctl(struct ifnet *, u_long, caddr_t);
static void	ed_start(struct ifnet *);
static void	ed_start_locked(struct ifnet *);
static void	ed_reset(struct ifnet *);
static void	ed_tick(void *);
static void	ed_watchdog(struct ed_softc *);

static void	ed_ds_getmcaf(struct ed_softc *, uint32_t *);

static void	ed_get_packet(struct ed_softc *, bus_size_t, u_short);
static void     ed_stop_hw(struct ed_softc *sc);

static __inline void ed_rint(struct ed_softc *);
static __inline void ed_xmit(struct ed_softc *);
static __inline void ed_ring_copy(struct ed_softc *, bus_size_t, char *,
    u_short);

static void	ed_setrcr(struct ed_softc *);

/*
 * Generic probe routine for testing for the existance of a DS8390.
 *	Must be called after the NIC has just been reset. This routine
 *	works by looking at certain register values that are guaranteed
 *	to be initialized a certain way after power-up or reset. Seems
 *	not to currently work on the 83C690.
 *
 * Specifically:
 *
 *	Register			reset bits	set bits
 *	Command Register (CR)		TXP, STA	RD2, STP
 *	Interrupt Status (ISR)				RST
 *	Interrupt Mask (IMR)		All bits
 *	Data Control (DCR)				LAS
 *	Transmit Config. (TCR)		LB1, LB0
 *
 * We only look at the CR and ISR registers, however, because looking at
 *	the others would require changing register pages (which would be
 *	intrusive if this isn't an 8390).
 *
 * Return 1 if 8390 was found, 0 if not.
 */

int
ed_probe_generic8390(struct ed_softc *sc)
{
	if ((ed_nic_inb(sc, ED_P0_CR) &
	     (ED_CR_RD2 | ED_CR_TXP | ED_CR_STA | ED_CR_STP)) !=
	    (ED_CR_RD2 | ED_CR_STP))
		return (0);
	if ((ed_nic_inb(sc, ED_P0_ISR) & ED_ISR_RST) != ED_ISR_RST)
		return (0);

	return (1);
}

void
ed_disable_16bit_access(struct ed_softc *sc)
{
	/*
	 * Disable 16 bit access to shared memory
	 */
	if (sc->isa16bit && sc->vendor == ED_VENDOR_WD_SMC) {
		if (sc->chip_type == ED_CHIP_TYPE_WD790)
			ed_asic_outb(sc, ED_WD_MSR, 0x00);
		ed_asic_outb(sc, ED_WD_LAAR,
		    sc->wd_laar_proto & ~ED_WD_LAAR_M16EN);
	}
}

void
ed_enable_16bit_access(struct ed_softc *sc)
{
	if (sc->isa16bit && sc->vendor == ED_VENDOR_WD_SMC) {
		ed_asic_outb(sc, ED_WD_LAAR,
		     sc->wd_laar_proto | ED_WD_LAAR_M16EN);
		if (sc->chip_type == ED_CHIP_TYPE_WD790)
			ed_asic_outb(sc, ED_WD_MSR, ED_WD_MSR_MENB);
	}
}

/*
 * Allocate a port resource with the given resource id.
 */
int
ed_alloc_port(device_t dev, int rid, int size)
{
	struct ed_softc *sc = device_get_softc(dev);
	struct resource *res;

	res = bus_alloc_resource_anywhere(dev, SYS_RES_IOPORT, &rid,
	    size, RF_ACTIVE);
	if (res) {
		sc->port_res = res;
		sc->port_used = size;
		sc->port_bst = rman_get_bustag(res);
		sc->port_bsh = rman_get_bushandle(res);
		return (0);
	}
	return (ENOENT);
}

/*
 * Allocate a memory resource with the given resource id.
 */
int
ed_alloc_memory(device_t dev, int rid, int size)
{
	struct ed_softc *sc = device_get_softc(dev);
	struct resource *res;

	res = bus_alloc_resource_anywhere(dev, SYS_RES_MEMORY, &rid,
	    size, RF_ACTIVE);
	if (res) {
		sc->mem_res = res;
		sc->mem_used = size;
		sc->mem_bst = rman_get_bustag(res);
		sc->mem_bsh = rman_get_bushandle(res);
		return (0);
	}
	return (ENOENT);
}

/*
 * Allocate an irq resource with the given resource id.
 */
int
ed_alloc_irq(device_t dev, int rid, int flags)
{
	struct ed_softc *sc = device_get_softc(dev);
	struct resource *res;

	res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE | flags);
	if (res) {
		sc->irq_res = res;
		return (0);
	}
	return (ENOENT);
}

/*
 * Release all resources
 */
void
ed_release_resources(device_t dev)
{
	struct ed_softc *sc = device_get_softc(dev);

	if (sc->port_res)
		bus_free_resource(dev, SYS_RES_IOPORT, sc->port_res);
	if (sc->port_res2)
		bus_free_resource(dev, SYS_RES_IOPORT, sc->port_res2);
	if (sc->mem_res)
		bus_free_resource(dev, SYS_RES_MEMORY, sc->mem_res);
	if (sc->irq_res)
		bus_free_resource(dev, SYS_RES_IRQ, sc->irq_res);
	sc->port_res = 0;
	sc->port_res2 = 0;
	sc->mem_res = 0;
	sc->irq_res = 0;
	if (sc->ifp)
		if_free(sc->ifp);
}

/*
 * Install interface into kernel networking data structures
 */
int
ed_attach(device_t dev)
{
	struct ed_softc *sc = device_get_softc(dev);
	struct ifnet *ifp;

	sc->dev = dev;
	ED_LOCK_INIT(sc);
	ifp = sc->ifp = if_alloc(IFT_ETHER);
	if (ifp == NULL) {
		device_printf(dev, "can not if_alloc()\n");
		ED_LOCK_DESTROY(sc);
		return (ENOSPC);
	}

	if (sc->readmem == NULL) {
		if (sc->mem_shared) {
			if (sc->isa16bit)
				sc->readmem = ed_shmem_readmem16;
			else
				sc->readmem = ed_shmem_readmem8;
		} else {
			sc->readmem = ed_pio_readmem;
		}
	}
	if (sc->sc_write_mbufs == NULL) {
		device_printf(dev, "No write mbufs routine set\n");
		return (ENXIO);
	}

	callout_init_mtx(&sc->tick_ch, ED_MUTEX(sc), 0);
	/*
	 * Set interface to stopped condition (reset)
	 */
	ed_stop_hw(sc);

	/*
	 * Initialize ifnet structure
	 */
	ifp->if_softc = sc;
	if_initname(ifp, device_get_name(dev), device_get_unit(dev));
	ifp->if_start = ed_start;
	ifp->if_ioctl = ed_ioctl;
	ifp->if_init = ed_init;
	IFQ_SET_MAXLEN(&ifp->if_snd, ifqmaxlen);
	ifp->if_snd.ifq_drv_maxlen = ifqmaxlen;
	IFQ_SET_READY(&ifp->if_snd);
	ifp->if_linkmib = &sc->mibdata;
	ifp->if_linkmiblen = sizeof sc->mibdata;
	/*
	 * XXX - should do a better job.
	 */
	if (sc->chip_type == ED_CHIP_TYPE_WD790)
		sc->mibdata.dot3StatsEtherChipSet =
			DOT3CHIPSET(dot3VendorWesternDigital,
				    dot3ChipSetWesternDigital83C790);
	else
		sc->mibdata.dot3StatsEtherChipSet =
			DOT3CHIPSET(dot3VendorNational, 
				    dot3ChipSetNational8390);
	sc->mibdata.dot3Compliance = DOT3COMPLIANCE_COLLS;

	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	/*
	 * Set default state for LINK2 flag (used to disable the 
	 * tranceiver for AUI operation), based on config option.
	 * We only set this flag before we attach the device, so there's
	 * no race.  It is convenient to allow users to turn this off
	 * by default in the kernel config, but given our more advanced
	 * boot time configuration options, this might no longer be needed.
	 */
	if (device_get_flags(dev) & ED_FLAGS_DISABLE_TRANCEIVER)
		ifp->if_flags |= IFF_LINK2;

	/*
	 * Attach the interface
	 */
	ether_ifattach(ifp, sc->enaddr);
	/* device attach does transition from UNCONFIGURED to IDLE state */

	sc->tx_mem = sc->txb_cnt * ED_PAGE_SIZE * ED_TXBUF_SIZE;
	sc->rx_mem = (sc->rec_page_stop - sc->rec_page_start) * ED_PAGE_SIZE;
	SYSCTL_ADD_STRING(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    0, "type", CTLFLAG_RD, sc->type_str, 0,
	    "Type of chip in card");
	SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    1, "TxMem", CTLFLAG_RD, &sc->tx_mem, 0,
	    "Memory set aside for transmitting packets");
	SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    2, "RxMem", CTLFLAG_RD, &sc->rx_mem, 0,
	    "Memory  set aside for receiving packets");
	SYSCTL_ADD_UINT(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    3, "Mem", CTLFLAG_RD, &sc->mem_size, 0,
	    "Total Card Memory");
	if (bootverbose) {
		if (sc->type_str && (*sc->type_str != 0))
			device_printf(dev, "type %s ", sc->type_str);
		else
			device_printf(dev, "type unknown (0x%x) ", sc->type);

#ifdef ED_HPP
		if (sc->vendor == ED_VENDOR_HP)
			printf("(%s %s IO)",
			    (sc->hpp_id & ED_HPP_ID_16_BIT_ACCESS) ?
			    "16-bit" : "32-bit",
			    sc->hpp_mem_start ? "memory mapped" : "regular");
		else
#endif
			printf("%s", sc->isa16bit ? "(16 bit)" : "(8 bit)");

#if defined(ED_HPP) || defined(ED_3C503)
		printf("%s", (((sc->vendor == ED_VENDOR_3COM) ||
				    (sc->vendor == ED_VENDOR_HP)) &&
			   (ifp->if_flags & IFF_LINK2)) ?
		    " tranceiver disabled" : "");
#endif
		printf("\n");
	}

	gone_by_fcp101_dev(dev);

	return (0);
}

/*
 * Detach the driver from the hardware and other systems in the kernel.
 */
int
ed_detach(device_t dev)
{
	struct ed_softc *sc = device_get_softc(dev);
	struct ifnet *ifp = sc->ifp;

	if (mtx_initialized(ED_MUTEX(sc)))
		ED_ASSERT_UNLOCKED(sc);
	if (ifp) {
		ED_LOCK(sc);
		if (bus_child_present(dev))
			ed_stop(sc);
		ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
		ED_UNLOCK(sc);
		ether_ifdetach(ifp);
		callout_drain(&sc->tick_ch);
	}
	if (sc->irq_res != NULL && sc->irq_handle)
		bus_teardown_intr(dev, sc->irq_res, sc->irq_handle);
	ed_release_resources(dev);
	if (sc->miibus)
		device_delete_child(dev, sc->miibus);
	if (mtx_initialized(ED_MUTEX(sc)))
		ED_LOCK_DESTROY(sc);
	bus_generic_detach(dev);
	return (0);
}

/*
 * Reset interface.
 */
static void
ed_reset(struct ifnet *ifp)
{
	struct ed_softc *sc = ifp->if_softc;

	ED_ASSERT_LOCKED(sc);
	/*
	 * Stop interface and re-initialize.
	 */
	ed_stop(sc);
	ed_init_locked(sc);
}

static void
ed_stop_hw(struct ed_softc *sc)
{
	int     n = 5000;

	/*
	 * Stop everything on the interface, and select page 0 registers.
	 */
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	ed_nic_outb(sc, ED_P0_CR, sc->cr_proto | ED_CR_STP);
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

	/*
	 * Wait for interface to enter stopped state, but limit # of checks to
	 * 'n' (about 5ms). It shouldn't even take 5us on modern DS8390's, but
	 * just in case it's an old one.
	 *
	 * The AX88x90 chips don't seem to implement this behavor.  The
	 * datasheets say it is only turned on when the chip enters a RESET
	 * state and is silent about behavior for the stopped state we just
	 * entered.
	 */
	if (sc->chip_type == ED_CHIP_TYPE_AX88190 ||
	    sc->chip_type == ED_CHIP_TYPE_AX88790)
		return;
	while (((ed_nic_inb(sc, ED_P0_ISR) & ED_ISR_RST) == 0) && --n)
		continue;
	if (n <= 0)
		device_printf(sc->dev, "ed_stop_hw RST never set\n");
}

/*
 * Take interface offline.
 */
void
ed_stop(struct ed_softc *sc)
{
	ED_ASSERT_LOCKED(sc);
	callout_stop(&sc->tick_ch);
	ed_stop_hw(sc);
}

/*
 * Periodic timer used to drive the watchdog and attachment-specific
 * tick handler.
 */
static void
ed_tick(void *arg)
{
	struct ed_softc *sc;

	sc = arg;
	ED_ASSERT_LOCKED(sc);
	if (sc->sc_tick)
		sc->sc_tick(sc);
	if (sc->tx_timer != 0 && --sc->tx_timer == 0)
		ed_watchdog(sc);
	callout_reset(&sc->tick_ch, hz, ed_tick, sc);
}

/*
 * Device timeout/watchdog routine. Entered if the device neglects to
 *	generate an interrupt after a transmit has been started on it.
 */
static void
ed_watchdog(struct ed_softc *sc)
{
	struct ifnet *ifp;

	ifp = sc->ifp;
	log(LOG_ERR, "%s: device timeout\n", ifp->if_xname);
	if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);

	ed_reset(ifp);
}

/*
 * Initialize device.
 */
static void
ed_init(void *xsc)
{
	struct ed_softc *sc = xsc;

	ED_ASSERT_UNLOCKED(sc);
	ED_LOCK(sc);
	ed_init_locked(sc);
	ED_UNLOCK(sc);
}

static void
ed_init_locked(struct ed_softc *sc)
{
	struct ifnet *ifp = sc->ifp;
	int     i;

	ED_ASSERT_LOCKED(sc);

	/*
	 * Initialize the NIC in the exact order outlined in the NS manual.
	 * This init procedure is "mandatory"...don't change what or when
	 * things happen.
	 */

	/* reset transmitter flags */
	sc->xmit_busy = 0;
	sc->tx_timer = 0;

	sc->txb_inuse = 0;
	sc->txb_new = 0;
	sc->txb_next_tx = 0;

	/* This variable is used below - don't move this assignment */
	sc->next_packet = sc->rec_page_start + 1;

	/*
	 * Set interface for page 0, Remote DMA complete, Stopped
	 */
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	ed_nic_outb(sc, ED_P0_CR, sc->cr_proto | ED_CR_STP);
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

	if (sc->isa16bit)
		/*
		 * Set FIFO threshold to 8, No auto-init Remote DMA, byte
		 * order=80x86, word-wide DMA xfers,
		 */
		ed_nic_outb(sc, ED_P0_DCR, ED_DCR_FT1 | ED_DCR_WTS | ED_DCR_LS);
	else
		/*
		 * Same as above, but byte-wide DMA xfers
		 */
		ed_nic_outb(sc, ED_P0_DCR, ED_DCR_FT1 | ED_DCR_LS);

	/*
	 * Clear Remote Byte Count Registers
	 */
	ed_nic_outb(sc, ED_P0_RBCR0, 0);
	ed_nic_outb(sc, ED_P0_RBCR1, 0);

	/*
	 * For the moment, don't store incoming packets in memory.
	 */
	ed_nic_outb(sc, ED_P0_RCR, ED_RCR_MON);

	/*
	 * Place NIC in internal loopback mode
	 */
	ed_nic_outb(sc, ED_P0_TCR, ED_TCR_LB0);

	/*
	 * Initialize transmit/receive (ring-buffer) Page Start
	 */
	ed_nic_outb(sc, ED_P0_TPSR, sc->tx_page_start);
	ed_nic_outb(sc, ED_P0_PSTART, sc->rec_page_start);
	/* Set lower bits of byte addressable framing to 0 */
	if (sc->chip_type == ED_CHIP_TYPE_WD790)
		ed_nic_outb(sc, 0x09, 0);

	/*
	 * Initialize Receiver (ring-buffer) Page Stop and Boundry
	 */
	ed_nic_outb(sc, ED_P0_PSTOP, sc->rec_page_stop);
	ed_nic_outb(sc, ED_P0_BNRY, sc->rec_page_start);

	/*
	 * Clear all interrupts. A '1' in each bit position clears the
	 * corresponding flag.
	 */
	ed_nic_outb(sc, ED_P0_ISR, 0xff);

	/*
	 * Enable the following interrupts: receive/transmit complete,
	 * receive/transmit error, and Receiver OverWrite.
	 *
	 * Counter overflow and Remote DMA complete are *not* enabled.
	 */
	ed_nic_outb(sc, ED_P0_IMR,
	ED_IMR_PRXE | ED_IMR_PTXE | ED_IMR_RXEE | ED_IMR_TXEE | ED_IMR_OVWE);

	/*
	 * Program Command Register for page 1
	 */
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	ed_nic_outb(sc, ED_P0_CR, sc->cr_proto | ED_CR_PAGE_1 | ED_CR_STP);
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

	/*
	 * Copy out our station address
	 */
	for (i = 0; i < ETHER_ADDR_LEN; ++i)
		ed_nic_outb(sc, ED_P1_PAR(i), IF_LLADDR(sc->ifp)[i]);

	/*
	 * Set Current Page pointer to next_packet (initialized above)
	 */
	ed_nic_outb(sc, ED_P1_CURR, sc->next_packet);

	/*
	 * Program Receiver Configuration Register and multicast filter. CR is
	 * set to page 0 on return.
	 */
	ed_setrcr(sc);

	/*
	 * Take interface out of loopback
	 */
	ed_nic_outb(sc, ED_P0_TCR, 0);

	if (sc->sc_mediachg)
		sc->sc_mediachg(sc);

	/*
	 * Set 'running' flag, and clear output active flag.
	 */
	ifp->if_drv_flags |= IFF_DRV_RUNNING;
	ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

	/*
	 * ...and attempt to start output
	 */
	ed_start_locked(ifp);

	callout_reset(&sc->tick_ch, hz, ed_tick, sc);
}

/*
 * This routine actually starts the transmission on the interface
 */
static __inline void
ed_xmit(struct ed_softc *sc)
{
	unsigned short len;

	len = sc->txb_len[sc->txb_next_tx];

	/*
	 * Set NIC for page 0 register access
	 */
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	ed_nic_outb(sc, ED_P0_CR, sc->cr_proto | ED_CR_STA);
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

	/*
	 * Set TX buffer start page
	 */
	ed_nic_outb(sc, ED_P0_TPSR, sc->tx_page_start +
		    sc->txb_next_tx * ED_TXBUF_SIZE);

	/*
	 * Set TX length
	 */
	ed_nic_outb(sc, ED_P0_TBCR0, len);
	ed_nic_outb(sc, ED_P0_TBCR1, len >> 8);

	/*
	 * Set page 0, Remote DMA complete, Transmit Packet, and *Start*
	 */
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	ed_nic_outb(sc, ED_P0_CR, sc->cr_proto | ED_CR_TXP | ED_CR_STA);
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	sc->xmit_busy = 1;

	/*
	 * Point to next transmit buffer slot and wrap if necessary.
	 */
	sc->txb_next_tx++;
	if (sc->txb_next_tx == sc->txb_cnt)
		sc->txb_next_tx = 0;

	/*
	 * Set a timer just in case we never hear from the board again
	 */
	sc->tx_timer = 2;
}

/*
 * Start output on interface.
 * We make two assumptions here:
 *  1) that the current priority is set to splimp _before_ this code
 *     is called *and* is returned to the appropriate priority after
 *     return
 *  2) that the IFF_DRV_OACTIVE flag is checked before this code is called
 *     (i.e. that the output part of the interface is idle)
 */
static void
ed_start(struct ifnet *ifp)
{
	struct ed_softc *sc = ifp->if_softc;

	ED_ASSERT_UNLOCKED(sc);
	ED_LOCK(sc);
	ed_start_locked(ifp);
	ED_UNLOCK(sc);
}

static void
ed_start_locked(struct ifnet *ifp)
{
	struct ed_softc *sc = ifp->if_softc;
	struct mbuf *m0, *m;
	bus_size_t buffer;
	int     len;

	ED_ASSERT_LOCKED(sc);
outloop:

	/*
	 * First, see if there are buffered packets and an idle transmitter -
	 * should never happen at this point.
	 */
	if (sc->txb_inuse && (sc->xmit_busy == 0)) {
		printf("ed: packets buffered, but transmitter idle\n");
		ed_xmit(sc);
	}

	/*
	 * See if there is room to put another packet in the buffer.
	 */
	if (sc->txb_inuse == sc->txb_cnt) {

		/*
		 * No room. Indicate this to the outside world and exit.
		 */
		ifp->if_drv_flags |= IFF_DRV_OACTIVE;
		return;
	}
	IFQ_DRV_DEQUEUE(&ifp->if_snd, m);
	if (m == NULL) {

		/*
		 * We are using the !OACTIVE flag to indicate to the outside
		 * world that we can accept an additional packet rather than
		 * that the transmitter is _actually_ active. Indeed, the
		 * transmitter may be active, but if we haven't filled all the
		 * buffers with data then we still want to accept more.
		 */
		ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;
		return;
	}

	/*
	 * Copy the mbuf chain into the transmit buffer
	 */
	m0 = m;

	/* txb_new points to next open buffer slot */
	buffer = sc->mem_start + (sc->txb_new * ED_TXBUF_SIZE * ED_PAGE_SIZE);

	len = sc->sc_write_mbufs(sc, m, buffer);
	if (len == 0) {
		m_freem(m0);
		goto outloop;
	}

	sc->txb_len[sc->txb_new] = max(len, (ETHER_MIN_LEN-ETHER_CRC_LEN));

	sc->txb_inuse++;

	/*
	 * Point to next buffer slot and wrap if necessary.
	 */
	sc->txb_new++;
	if (sc->txb_new == sc->txb_cnt)
		sc->txb_new = 0;

	if (sc->xmit_busy == 0)
		ed_xmit(sc);

	/*
	 * Tap off here if there is a bpf listener.
	 */
	BPF_MTAP(ifp, m0);

	m_freem(m0);

	/*
	 * Loop back to the top to possibly buffer more packets
	 */
	goto outloop;
}

/*
 * Ethernet interface receiver interrupt.
 */
static __inline void
ed_rint(struct ed_softc *sc)
{
	struct ifnet *ifp = sc->ifp;
	u_char  boundry;
	u_short len;
	struct ed_ring packet_hdr;
	bus_size_t packet_ptr;

	ED_ASSERT_LOCKED(sc);

	/*
	 * Set NIC to page 1 registers to get 'current' pointer
	 */
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	ed_nic_outb(sc, ED_P0_CR, sc->cr_proto | ED_CR_PAGE_1 | ED_CR_STA);
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

	/*
	 * 'sc->next_packet' is the logical beginning of the ring-buffer -
	 * i.e. it points to where new data has been buffered. The 'CURR'
	 * (current) register points to the logical end of the ring-buffer -
	 * i.e. it points to where additional new data will be added. We loop
	 * here until the logical beginning equals the logical end (or in
	 * other words, until the ring-buffer is empty).
	 */
	while (sc->next_packet != ed_nic_inb(sc, ED_P1_CURR)) {

		/* get pointer to this buffer's header structure */
		packet_ptr = sc->mem_ring +
		    (sc->next_packet - sc->rec_page_start) * ED_PAGE_SIZE;
	
		/*
		 * The byte count includes a 4 byte header that was added by
		 * the NIC.
		 */
		sc->readmem(sc, packet_ptr, (char *) &packet_hdr,
		    sizeof(packet_hdr));
		len = packet_hdr.count;
		if (len > (ETHER_MAX_LEN - ETHER_CRC_LEN + sizeof(struct ed_ring)) ||
		    len < (ETHER_MIN_LEN - ETHER_CRC_LEN + sizeof(struct ed_ring))) {
			/*
			 * Length is a wild value. There's a good chance that
			 * this was caused by the NIC being old and buggy.
			 * The bug is that the length low byte is duplicated
			 * in the high byte. Try to recalculate the length
			 * based on the pointer to the next packet.  Also,
			 * need ot preserve offset into page.
			 *
			 * NOTE: sc->next_packet is pointing at the current
			 * packet.
			 */
			len &= ED_PAGE_SIZE - 1;
			if (packet_hdr.next_packet >= sc->next_packet)
				len += (packet_hdr.next_packet -
				    sc->next_packet) * ED_PAGE_SIZE;
			else
				len += 
				    ((packet_hdr.next_packet - sc->rec_page_start) +
				    (sc->rec_page_stop - sc->next_packet)) * ED_PAGE_SIZE;
			/*
			 * because buffers are aligned on 256-byte boundary,
			 * the length computed above is off by 256 in almost
			 * all cases. Fix it...
			 */
			if (len & 0xff)
				len -= 256;
			if (len > (ETHER_MAX_LEN - ETHER_CRC_LEN 
			    + sizeof(struct ed_ring)))
				sc->mibdata.dot3StatsFrameTooLongs++;
		}

		/*
		 * Be fairly liberal about what we allow as a "reasonable"
		 * length so that a [crufty] packet will make it to BPF (and
		 * can thus be analyzed). Note that all that is really
		 * important is that we have a length that will fit into one
		 * mbuf cluster or less; the upper layer protocols can then
		 * figure out the length from their own length field(s).  But
		 * make sure that we have at least a full ethernet header or
		 * we would be unable to call ether_input() later.
		 */
		if ((len >= sizeof(struct ed_ring) + ETHER_HDR_LEN) &&
		    (len <= MCLBYTES) &&
		    (packet_hdr.next_packet >= sc->rec_page_start) &&
		    (packet_hdr.next_packet < sc->rec_page_stop)) {
			/*
			 * Go get packet.
			 */
			ed_get_packet(sc, packet_ptr + sizeof(struct ed_ring),
				      len - sizeof(struct ed_ring));
			if_inc_counter(ifp, IFCOUNTER_IPACKETS, 1);
		} else {
			/*
			 * Really BAD. The ring pointers are corrupted.
			 */
			log(LOG_ERR,
			    "%s: NIC memory corrupt - invalid packet length %d\n",
			    ifp->if_xname, len);
			if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
			ed_reset(ifp);
			return;
		}

		/*
		 * Update next packet pointer
		 */
		sc->next_packet = packet_hdr.next_packet;

		/*
		 * Update NIC boundry pointer - being careful to keep it one
		 * buffer behind. (as recommended by NS databook)
		 */
		boundry = sc->next_packet - 1;
		if (boundry < sc->rec_page_start)
			boundry = sc->rec_page_stop - 1;

		/*
		 * Set NIC to page 0 registers to update boundry register
		 */
		ed_nic_barrier(sc, ED_P0_CR, 1,
		    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
		ed_nic_outb(sc, ED_P0_CR, sc->cr_proto | ED_CR_STA);
		ed_nic_barrier(sc, ED_P0_CR, 1,
		    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
		ed_nic_outb(sc, ED_P0_BNRY, boundry);

		/*
		 * Set NIC to page 1 registers before looping to top (prepare
		 * to get 'CURR' current pointer)
		 */
		ed_nic_barrier(sc, ED_P0_CR, 1,
		    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
		ed_nic_outb(sc, ED_P0_CR, sc->cr_proto | ED_CR_PAGE_1 | ED_CR_STA);
		ed_nic_barrier(sc, ED_P0_CR, 1,
		    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	}
}

/*
 * Ethernet interface interrupt processor
 */
void
edintr(void *arg)
{
	struct ed_softc *sc = (struct ed_softc*) arg;
	struct ifnet *ifp = sc->ifp;
	u_char  isr;
	int	count;

	ED_LOCK(sc);
	if (!(ifp->if_drv_flags & IFF_DRV_RUNNING)) {
		ED_UNLOCK(sc);
		return;
	}
	/*
	 * Set NIC to page 0 registers
	 */
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	ed_nic_outb(sc, ED_P0_CR, sc->cr_proto | ED_CR_STA);
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

	/*
	 * loop until there are no more new interrupts.  When the card goes
	 * away, the hardware will read back 0xff.  Looking at the interrupts,
	 * it would appear that 0xff is impossible as ED_ISR_RST is normally
	 * clear. ED_ISR_RDC is also normally clear and only set while
	 * we're transferring memory to the card and we're holding the
	 * ED_LOCK (so we can't get into here).
	 */
	while ((isr = ed_nic_inb(sc, ED_P0_ISR)) != 0 && isr != 0xff) {

		/*
		 * reset all the bits that we are 'acknowledging' by writing a
		 * '1' to each bit position that was set (writing a '1'
		 * *clears* the bit)
		 */
		ed_nic_outb(sc, ED_P0_ISR, isr);

		/*
		 * The AX88190 and AX88190A has problems acking an interrupt
		 * and having them clear.  This interferes with top-level loop
		 * here.  Wait for all the bits to clear.
		 *
		 * We limit this to 5000 iterations.  At 1us per inb/outb,
		 * this translates to about 15ms, which should be plenty of
		 * time, and also gives protection in the card eject case.
		 */
		if (sc->chip_type == ED_CHIP_TYPE_AX88190) {
			count = 5000;		/* 15ms */
			while (count-- && (ed_nic_inb(sc, ED_P0_ISR) & isr)) {
				ed_nic_outb(sc, ED_P0_ISR,0);
				ed_nic_outb(sc, ED_P0_ISR,isr);
			}
			if (count == 0)
				break;
		}

		/*
		 * Handle transmitter interrupts. Handle these first because
		 * the receiver will reset the board under some conditions.
		 */
		if (isr & (ED_ISR_PTX | ED_ISR_TXE)) {
			u_char  collisions = ed_nic_inb(sc, ED_P0_NCR) & 0x0f;

			/*
			 * Check for transmit error. If a TX completed with an
			 * error, we end up throwing the packet away. Really
			 * the only error that is possible is excessive
			 * collisions, and in this case it is best to allow
			 * the automatic mechanisms of TCP to backoff the
			 * flow. Of course, with UDP we're screwed, but this
			 * is expected when a network is heavily loaded.
			 */
			(void) ed_nic_inb(sc, ED_P0_TSR);
			if (isr & ED_ISR_TXE) {
				u_char tsr;

				/*
				 * Excessive collisions (16)
				 */
				tsr = ed_nic_inb(sc, ED_P0_TSR);
				if ((tsr & ED_TSR_ABT)	
				    && (collisions == 0)) {

					/*
					 * When collisions total 16, the
					 * P0_NCR will indicate 0, and the
					 * TSR_ABT is set.
					 */
					collisions = 16;
					sc->mibdata.dot3StatsExcessiveCollisions++;
					sc->mibdata.dot3StatsCollFrequencies[15]++;
				}
				if (tsr & ED_TSR_OWC)
					sc->mibdata.dot3StatsLateCollisions++;
				if (tsr & ED_TSR_CDH)
					sc->mibdata.dot3StatsSQETestErrors++;
				if (tsr & ED_TSR_CRS)
					sc->mibdata.dot3StatsCarrierSenseErrors++;
				if (tsr & ED_TSR_FU)
					sc->mibdata.dot3StatsInternalMacTransmitErrors++;

				/*
				 * update output errors counter
				 */
				if_inc_counter(ifp, IFCOUNTER_OERRORS, 1);
			} else {

				/*
				 * Update total number of successfully
				 * transmitted packets.
				 */
				if_inc_counter(ifp, IFCOUNTER_OPACKETS, 1);
			}

			/*
			 * reset tx busy and output active flags
			 */
			sc->xmit_busy = 0;
			ifp->if_drv_flags &= ~IFF_DRV_OACTIVE;

			/*
			 * clear watchdog timer
			 */
			sc->tx_timer = 0;

			/*
			 * Add in total number of collisions on last
			 * transmission.
			 */
			if_inc_counter(ifp, IFCOUNTER_COLLISIONS, collisions);
			switch(collisions) {
			case 0:
			case 16:
				break;
			case 1:
				sc->mibdata.dot3StatsSingleCollisionFrames++;
				sc->mibdata.dot3StatsCollFrequencies[0]++;
				break;
			default:
				sc->mibdata.dot3StatsMultipleCollisionFrames++;
				sc->mibdata.
					dot3StatsCollFrequencies[collisions-1]
						++;
				break;
			}

			/*
			 * Decrement buffer in-use count if not zero (can only
			 * be zero if a transmitter interrupt occured while
			 * not actually transmitting). If data is ready to
			 * transmit, start it transmitting, otherwise defer
			 * until after handling receiver
			 */
			if (sc->txb_inuse && --sc->txb_inuse)
				ed_xmit(sc);
		}

		/*
		 * Handle receiver interrupts
		 */
		if (isr & (ED_ISR_PRX | ED_ISR_RXE | ED_ISR_OVW)) {

			/*
			 * Overwrite warning. In order to make sure that a
			 * lockup of the local DMA hasn't occurred, we reset
			 * and re-init the NIC. The NSC manual suggests only a
			 * partial reset/re-init is necessary - but some chips
			 * seem to want more. The DMA lockup has been seen
			 * only with early rev chips - Methinks this bug was
			 * fixed in later revs. -DG
			 */
			if (isr & ED_ISR_OVW) {
				if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
#ifdef DIAGNOSTIC
				log(LOG_WARNING,
				    "%s: warning - receiver ring buffer overrun\n",
				    ifp->if_xname);
#endif

				/*
				 * Stop/reset/re-init NIC
				 */
				ed_reset(ifp);
			} else {

				/*
				 * Receiver Error. One or more of: CRC error,
				 * frame alignment error FIFO overrun, or
				 * missed packet.
				 */
				if (isr & ED_ISR_RXE) {
					u_char rsr;
					rsr = ed_nic_inb(sc, ED_P0_RSR);
					if (rsr & ED_RSR_CRC)
						sc->mibdata.dot3StatsFCSErrors++;
					if (rsr & ED_RSR_FAE)
						sc->mibdata.dot3StatsAlignmentErrors++;
					if (rsr & ED_RSR_FO)
						sc->mibdata.dot3StatsInternalMacReceiveErrors++;
					if_inc_counter(ifp, IFCOUNTER_IERRORS, 1);
#ifdef ED_DEBUG
					if_printf(ifp, "receive error %x\n",
					       ed_nic_inb(sc, ED_P0_RSR));
#endif
				}

				/*
				 * Go get the packet(s) XXX - Doing this on an
				 * error is dubious because there shouldn't be
				 * any data to get (we've configured the
				 * interface to not accept packets with
				 * errors).
				 */

				/*
				 * Enable 16bit access to shared memory first
				 * on WD/SMC boards.
				 */
				ed_enable_16bit_access(sc);
				ed_rint(sc);
				ed_disable_16bit_access(sc);
			}
		}

		/*
		 * If it looks like the transmitter can take more data,
		 * attempt to start output on the interface. This is done
		 * after handling the receiver to give the receiver priority.
		 */
		if ((ifp->if_drv_flags & IFF_DRV_OACTIVE) == 0)
			ed_start_locked(ifp);

		/*
		 * return NIC CR to standard state: page 0, remote DMA
		 * complete, start (toggling the TXP bit off, even if was just
		 * set in the transmit routine, is *okay* - it is 'edge'
		 * triggered from low to high)
		 */
		ed_nic_barrier(sc, ED_P0_CR, 1,
	  	  BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
		ed_nic_outb(sc, ED_P0_CR, sc->cr_proto | ED_CR_STA);
		ed_nic_barrier(sc, ED_P0_CR, 1,
		    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

		/*
		 * If the Network Talley Counters overflow, read them to reset
		 * them. It appears that old 8390's won't clear the ISR flag
		 * otherwise - resulting in an infinite loop.
		 */
		if (isr & ED_ISR_CNT) {
			(void) ed_nic_inb(sc, ED_P0_CNTR0);
			(void) ed_nic_inb(sc, ED_P0_CNTR1);
			(void) ed_nic_inb(sc, ED_P0_CNTR2);
		}
	}
	ED_UNLOCK(sc);
}

/*
 * Process an ioctl request.
 */
static int
ed_ioctl(struct ifnet *ifp, u_long command, caddr_t data)
{
	struct ed_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	int     error = 0;

	switch (command) {
	case SIOCSIFFLAGS:
		/*
		 * If the interface is marked up and stopped, then start it.
		 * If we're up and already running, then it may be a mediachg.
		 * If it is marked down and running, then stop it.
		 */
		ED_LOCK(sc);
		if (ifp->if_flags & IFF_UP) {
			if ((ifp->if_drv_flags & IFF_DRV_RUNNING) == 0)
				ed_init_locked(sc);
			else if (sc->sc_mediachg)
				sc->sc_mediachg(sc);
		} else {
			if (ifp->if_drv_flags & IFF_DRV_RUNNING) {
				ed_stop(sc);
				ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
			}
		}

		/*
		 * Promiscuous flag may have changed, so reprogram the RCR.
		 */
		ed_setrcr(sc);

		ED_UNLOCK(sc);
		break;

	case SIOCADDMULTI:
	case SIOCDELMULTI:
		/*
		 * Multicast list has changed; set the hardware filter
		 * accordingly.
		 */
		ED_LOCK(sc);
		ed_setrcr(sc);
		ED_UNLOCK(sc);
		error = 0;
		break;

	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		if (sc->sc_media_ioctl == NULL) {
			error = EINVAL;
			break;
		}
		sc->sc_media_ioctl(sc, ifr, command);
		break;

	default:
		error = ether_ioctl(ifp, command, data);
		break;
	}
	return (error);
}

/*
 * Given a source and destination address, copy 'amount' of a packet from
 *	the ring buffer into a linear destination buffer. Takes into account
 *	ring-wrap.
 */
static __inline void
ed_ring_copy(struct ed_softc *sc, bus_size_t src, char *dst, u_short amount)
{
	u_short tmp_amount;

	/* does copy wrap to lower addr in ring buffer? */
	if (src + amount > sc->mem_end) {
		tmp_amount = sc->mem_end - src;
		/* copy amount up to end of NIC memory */
		sc->readmem(sc, src, dst, tmp_amount);
		amount -= tmp_amount;
		src = sc->mem_ring;
		dst += tmp_amount;
	}
	sc->readmem(sc, src, dst, amount);
}

/*
 * Retreive packet from shared memory and send to the next level up via
 * ether_input().
 */
static void
ed_get_packet(struct ed_softc *sc, bus_size_t buf, u_short len)
{
	struct ifnet *ifp = sc->ifp;
	struct ether_header *eh;
	struct mbuf *m;

	/* Allocate a header mbuf */
	MGETHDR(m, M_NOWAIT, MT_DATA);
	if (m == NULL)
		return;
	m->m_pkthdr.rcvif = ifp;
	m->m_pkthdr.len = m->m_len = len;

	/*
	 * We always put the received packet in a single buffer -
	 * either with just an mbuf header or in a cluster attached
	 * to the header. The +2 is to compensate for the alignment
	 * fixup below.
	 */
	if ((len + 2) > MHLEN) {
		/* Attach an mbuf cluster */
		if (!(MCLGET(m, M_NOWAIT))) {
			m_freem(m);
			return;
		}
	}

	/*
	 * The +2 is to longword align the start of the real packet.
	 * This is important for NFS.
	 */
	m->m_data += 2;
	eh = mtod(m, struct ether_header *);

	/*
	 * Get packet, including link layer address, from interface.
	 */
	ed_ring_copy(sc, buf, (char *)eh, len);

	m->m_pkthdr.len = m->m_len = len;

	ED_UNLOCK(sc);
	(*ifp->if_input)(ifp, m);
	ED_LOCK(sc);
}

/*
 * Supporting routines
 */

/*
 * Given a NIC memory source address and a host memory destination
 *	address, copy 'amount' from NIC to host using shared memory.
 *	The 'amount' is rounded up to a word - okay as long as mbufs
 *		are word sized.  That's what the +1 is below.
 * This routine accesses things as 16 bit quantities.
 */
void
ed_shmem_readmem16(struct ed_softc *sc, bus_size_t src, uint8_t *dst,
    uint16_t amount)
{
	bus_space_read_region_2(sc->mem_bst, sc->mem_bsh, src, (uint16_t *)dst,
	    (amount + 1) / 2);
}

/*
 * Given a NIC memory source address and a host memory destination
 *	address, copy 'amount' from NIC to host using shared memory.
 * This routine accesses things as 8 bit quantities.
 */
void
ed_shmem_readmem8(struct ed_softc *sc, bus_size_t src, uint8_t *dst,
    uint16_t amount)
{
	bus_space_read_region_1(sc->mem_bst, sc->mem_bsh, src, dst, amount);
}

/*
 * Given a NIC memory source address and a host memory destination
 *	address, copy 'amount' from NIC to host using Programmed I/O.
 *	The 'amount' is rounded up to a word - okay as long as mbufs
 *		are word sized.
 *	This routine is currently Novell-specific.
 */
void
ed_pio_readmem(struct ed_softc *sc, bus_size_t src, uint8_t *dst,
    uint16_t amount)
{
	/* Regular Novell cards */
	/* select page 0 registers */
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	ed_nic_outb(sc, ED_P0_CR, ED_CR_RD2 | ED_CR_STA);
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

	/* round up to a word */
	if (amount & 1)
		++amount;

	/* set up DMA byte count */
	ed_nic_outb(sc, ED_P0_RBCR0, amount);
	ed_nic_outb(sc, ED_P0_RBCR1, amount >> 8);

	/* set up source address in NIC mem */
	ed_nic_outb(sc, ED_P0_RSAR0, src);
	ed_nic_outb(sc, ED_P0_RSAR1, src >> 8);

	ed_nic_outb(sc, ED_P0_CR, ED_CR_RD0 | ED_CR_STA);

	if (sc->isa16bit)
		ed_asic_insw(sc, ED_NOVELL_DATA, dst, amount / 2);
	else
		ed_asic_insb(sc, ED_NOVELL_DATA, dst, amount);
}

/*
 * Stripped down routine for writing a linear buffer to NIC memory.
 *	Only used in the probe routine to test the memory. 'len' must
 *	be even.
 */
void
ed_pio_writemem(struct ed_softc *sc, uint8_t *src, uint16_t dst, uint16_t len)
{
	int     maxwait = 200;	/* about 240us */

	/* select page 0 registers */
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	ed_nic_outb(sc, ED_P0_CR, ED_CR_RD2 | ED_CR_STA);
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

	/* reset remote DMA complete flag */
	ed_nic_outb(sc, ED_P0_ISR, ED_ISR_RDC);

	/* set up DMA byte count */
	ed_nic_outb(sc, ED_P0_RBCR0, len);
	ed_nic_outb(sc, ED_P0_RBCR1, len >> 8);

	/* set up destination address in NIC mem */
	ed_nic_outb(sc, ED_P0_RSAR0, dst);
	ed_nic_outb(sc, ED_P0_RSAR1, dst >> 8);

	/* set remote DMA write */
	ed_nic_outb(sc, ED_P0_CR, ED_CR_RD1 | ED_CR_STA);

	if (sc->isa16bit)
		ed_asic_outsw(sc, ED_NOVELL_DATA, src, len / 2);
	else
		ed_asic_outsb(sc, ED_NOVELL_DATA, src, len);

	/*
	 * Wait for remote DMA complete. This is necessary because on the
	 * transmit side, data is handled internally by the NIC in bursts and
	 * we can't start another remote DMA until this one completes. Not
	 * waiting causes really bad things to happen - like the NIC
	 * irrecoverably jamming the ISA bus.
	 */
	while (((ed_nic_inb(sc, ED_P0_ISR) & ED_ISR_RDC) != ED_ISR_RDC) &&
	    --maxwait)
		continue;
}

/*
 * Write an mbuf chain to the destination NIC memory address using
 *	programmed I/O.
 */
u_short
ed_pio_write_mbufs(struct ed_softc *sc, struct mbuf *m, bus_size_t dst)
{
	struct ifnet *ifp = sc->ifp;
	unsigned short total_len, dma_len;
	struct mbuf *mp;
	int     maxwait = 200;	/* about 240us */

	ED_ASSERT_LOCKED(sc);

	/* Regular Novell cards */
	/* First, count up the total number of bytes to copy */
	for (total_len = 0, mp = m; mp; mp = mp->m_next)
		total_len += mp->m_len;

	dma_len = total_len;
	if (sc->isa16bit && (dma_len & 1))
		dma_len++;

	/* select page 0 registers */
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	ed_nic_outb(sc, ED_P0_CR, ED_CR_RD2 | ED_CR_STA);
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

	/* reset remote DMA complete flag */
	ed_nic_outb(sc, ED_P0_ISR, ED_ISR_RDC);

	/* set up DMA byte count */
	ed_nic_outb(sc, ED_P0_RBCR0, dma_len);
	ed_nic_outb(sc, ED_P0_RBCR1, dma_len >> 8);

	/* set up destination address in NIC mem */
	ed_nic_outb(sc, ED_P0_RSAR0, dst);
	ed_nic_outb(sc, ED_P0_RSAR1, dst >> 8);

	/* set remote DMA write */
	ed_nic_outb(sc, ED_P0_CR, ED_CR_RD1 | ED_CR_STA);

  /*
   * Transfer the mbuf chain to the NIC memory.
   * 16-bit cards require that data be transferred as words, and only words.
   * So that case requires some extra code to patch over odd-length mbufs.
   */

	if (!sc->isa16bit) {
		/* NE1000s are easy */
		while (m) {
			if (m->m_len)
				ed_asic_outsb(sc, ED_NOVELL_DATA, 
				    m->m_data, m->m_len);
			m = m->m_next;
		}
	} else {
		/* NE2000s are a pain */
		uint8_t *data;
		int len, wantbyte;
		union {
			uint16_t w;
			uint8_t b[2];
		} saveword;

		wantbyte = 0;

		while (m) {
			len = m->m_len;
			if (len) {
				data = mtod(m, caddr_t);
				/* finish the last word */
				if (wantbyte) {
					saveword.b[1] = *data;
					ed_asic_outw(sc, ED_NOVELL_DATA,
					    saveword.w);
					data++;
					len--;
					wantbyte = 0;
				}
				/* output contiguous words */
				if (len > 1) {
					ed_asic_outsw(sc, ED_NOVELL_DATA,
						      data, len >> 1);
					data += len & ~1;
					len &= 1;
				}
				/* save last byte, if necessary */
				if (len == 1) {
					saveword.b[0] = *data;
					wantbyte = 1;
				}
			}
			m = m->m_next;
		}
		/* spit last byte */
		if (wantbyte)
			ed_asic_outw(sc, ED_NOVELL_DATA, saveword.w);
	}

	/*
	 * Wait for remote DMA complete. This is necessary because on the
	 * transmit side, data is handled internally by the NIC in bursts and
	 * we can't start another remote DMA until this one completes. Not
	 * waiting causes really bad things to happen - like the NIC
	 * irrecoverably jamming the ISA bus.
	 */
	while (((ed_nic_inb(sc, ED_P0_ISR) & ED_ISR_RDC) != ED_ISR_RDC) &&
	    --maxwait)
		continue;

	if (!maxwait) {
		log(LOG_WARNING, "%s: remote transmit DMA failed to complete\n",
		    ifp->if_xname);
		ed_reset(ifp);
		return(0);
	}
	return (total_len);
}

static void
ed_setrcr(struct ed_softc *sc)
{
	struct ifnet *ifp = sc->ifp;
	int     i;
	u_char	reg1;

	ED_ASSERT_LOCKED(sc);

	/* Bit 6 in AX88190 RCR register must be set. */
	if (sc->chip_type == ED_CHIP_TYPE_AX88190 ||
	    sc->chip_type == ED_CHIP_TYPE_AX88790)
		reg1 = ED_RCR_INTT;
	else
		reg1 = 0x00;

	/* set page 1 registers */
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	ed_nic_outb(sc, ED_P0_CR, sc->cr_proto | ED_CR_PAGE_1 | ED_CR_STP);
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

	if (ifp->if_flags & IFF_PROMISC) {

		/*
		 * Reconfigure the multicast filter.
		 */
		for (i = 0; i < 8; i++)
			ed_nic_outb(sc, ED_P1_MAR(i), 0xff);

		/*
		 * And turn on promiscuous mode. Also enable reception of
		 * runts and packets with CRC & alignment errors.
		 */
		/* Set page 0 registers */
		ed_nic_barrier(sc, ED_P0_CR, 1,
		    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
		ed_nic_outb(sc, ED_P0_CR, sc->cr_proto | ED_CR_STP);
		ed_nic_barrier(sc, ED_P0_CR, 1,
		    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

		ed_nic_outb(sc, ED_P0_RCR, ED_RCR_PRO | ED_RCR_AM |
			    ED_RCR_AB | ED_RCR_AR | ED_RCR_SEP | reg1);
	} else {
		/* set up multicast addresses and filter modes */
		if (ifp->if_flags & IFF_MULTICAST) {
			uint32_t  mcaf[2];

			if (ifp->if_flags & IFF_ALLMULTI) {
				mcaf[0] = 0xffffffff;
				mcaf[1] = 0xffffffff;
			} else
				ed_ds_getmcaf(sc, mcaf);

			/*
			 * Set multicast filter on chip.
			 */
			for (i = 0; i < 8; i++)
				ed_nic_outb(sc, ED_P1_MAR(i), ((u_char *) mcaf)[i]);

			/* Set page 0 registers */
			ed_nic_barrier(sc, ED_P0_CR, 1,
			    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
			ed_nic_outb(sc, ED_P0_CR, sc->cr_proto | ED_CR_STP);
			ed_nic_barrier(sc, ED_P0_CR, 1,
			    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

			ed_nic_outb(sc, ED_P0_RCR, ED_RCR_AM | ED_RCR_AB | reg1);
		} else {

			/*
			 * Initialize multicast address hashing registers to
			 * not accept multicasts.
			 */
			for (i = 0; i < 8; ++i)
				ed_nic_outb(sc, ED_P1_MAR(i), 0x00);

			/* Set page 0 registers */
			ed_nic_barrier(sc, ED_P0_CR, 1,
			    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
			ed_nic_outb(sc, ED_P0_CR, sc->cr_proto | ED_CR_STP);

			ed_nic_outb(sc, ED_P0_RCR, ED_RCR_AB | reg1);
		}
	}

	/*
	 * Start interface.
	 */
	ed_nic_outb(sc, ED_P0_CR, sc->cr_proto | ED_CR_STA);
}

/*
 * Compute the multicast address filter from the
 * list of multicast addresses we need to listen to.
 */
static void
ed_ds_getmcaf(struct ed_softc *sc, uint32_t *mcaf)
{
	uint32_t index;
	u_char *af = (u_char *) mcaf;
	struct ifmultiaddr *ifma;

	mcaf[0] = 0;
	mcaf[1] = 0;

	if_maddr_rlock(sc->ifp);
	CK_STAILQ_FOREACH(ifma, &sc->ifp->if_multiaddrs, ifma_link) {
		if (ifma->ifma_addr->sa_family != AF_LINK)
			continue;
		index = ether_crc32_be(LLADDR((struct sockaddr_dl *)
		    ifma->ifma_addr), ETHER_ADDR_LEN) >> 26;
		af[index >> 3] |= 1 << (index & 7);
	}
	if_maddr_runlock(sc->ifp);
}

int
ed_isa_mem_ok(device_t dev, u_long pmem, u_int memsize)
{
	if (pmem < 0xa0000 || pmem + memsize > 0x1000000) {
		device_printf(dev, "Invalid ISA memory address range "
		    "configured: 0x%lx - 0x%lx\n", pmem, pmem + memsize);
		return (ENXIO);
	}
	return (0);
}

int
ed_clear_memory(device_t dev)
{
	struct ed_softc *sc = device_get_softc(dev);
	bus_size_t i;

	bus_space_set_region_1(sc->mem_bst, sc->mem_bsh, sc->mem_start,
	    0, sc->mem_size);

	for (i = 0; i < sc->mem_size; i++) {
		if (bus_space_read_1(sc->mem_bst, sc->mem_bsh,
		    sc->mem_start + i)) {
			device_printf(dev, "failed to clear shared memory at "
			  "0x%jx - check configuration\n",
			    (uintmax_t)rman_get_start(sc->mem_res) + i);
			return (ENXIO);
		}
	}
	return (0);
}
	    
u_short
ed_shmem_write_mbufs(struct ed_softc *sc, struct mbuf *m, bus_size_t dst)
{
	u_short len;

	/*
	 * Special case setup for 16 bit boards...
	 */
	if (sc->isa16bit) {
		switch (sc->vendor) {
#ifdef ED_3C503
			/*
			 * For 16bit 3Com boards (which have 16k of
			 * memory), we have the xmit buffers in a
			 * different page of memory ('page 0') - so
			 * change pages.
			 */
		case ED_VENDOR_3COM:
			ed_asic_outb(sc, ED_3COM_GACFR, ED_3COM_GACFR_RSEL);
			break;
#endif
			/*
			 * Enable 16bit access to shared memory on
			 * WD/SMC boards.
			 *
			 * XXX - same as ed_enable_16bit_access()
			 */
		case ED_VENDOR_WD_SMC:
			ed_asic_outb(sc, ED_WD_LAAR,
			    sc->wd_laar_proto | ED_WD_LAAR_M16EN);
			if (sc->chip_type == ED_CHIP_TYPE_WD790)
				ed_asic_outb(sc, ED_WD_MSR, ED_WD_MSR_MENB);
			break;
		}
	}
	for (len = 0; m != NULL; m = m->m_next) {
		if (m->m_len == 0)
			continue;
		if (sc->isa16bit) {
			if (m->m_len > 1)
				bus_space_write_region_2(sc->mem_bst,
				    sc->mem_bsh, dst,
				    mtod(m, uint16_t *), m->m_len / 2);
			if ((m->m_len & 1) != 0)
				bus_space_write_1(sc->mem_bst, sc->mem_bsh,
				    dst + m->m_len - 1,
				    *(mtod(m, uint8_t *) + m->m_len - 1));
		} else
			bus_space_write_region_1(sc->mem_bst,
			    sc->mem_bsh, dst,
			    mtod(m, uint8_t *), m->m_len);
		dst += m->m_len;
		len += m->m_len;
	}

	/*
	 * Restore previous shared memory access
	 */
	if (sc->isa16bit) {
		switch (sc->vendor) {
#ifdef ED_3C503
		case ED_VENDOR_3COM:
			ed_asic_outb(sc, ED_3COM_GACFR,
			    ED_3COM_GACFR_RSEL | ED_3COM_GACFR_MBS0);
			break;
#endif
		case ED_VENDOR_WD_SMC:
			/* XXX - same as ed_disable_16bit_access() */
			if (sc->chip_type == ED_CHIP_TYPE_WD790)
				ed_asic_outb(sc, ED_WD_MSR, 0x00);
			ed_asic_outb(sc, ED_WD_LAAR,
			    sc->wd_laar_proto & ~ED_WD_LAAR_M16EN);
			break;
		}
	}
	return (len);
}

/*
 * Generic ifmedia support.  By default, the DP8390-based cards don't know
 * what their network attachment really is, or even if it is valid (except
 * upon successful transmission of a packet).  To play nicer with dhclient, as
 * well as to fit in with a framework where some cards can provde more
 * detailed information, make sure that we use this as a fallback.
 */
static int
ed_gen_ifmedia_ioctl(struct ed_softc *sc, struct ifreq *ifr, u_long command)
{
	return (ifmedia_ioctl(sc->ifp, ifr, &sc->ifmedia, command));
}

static int
ed_gen_ifmedia_upd(struct ifnet *ifp)
{
	return 0;
}

static void
ed_gen_ifmedia_sts(struct ifnet *ifp, struct ifmediareq *ifmr)
{
	ifmr->ifm_active = IFM_ETHER | IFM_AUTO;
	ifmr->ifm_status = IFM_AVALID | IFM_ACTIVE;
}

void
ed_gen_ifmedia_init(struct ed_softc *sc)
{
	sc->sc_media_ioctl = &ed_gen_ifmedia_ioctl;
	ifmedia_init(&sc->ifmedia, 0, ed_gen_ifmedia_upd, ed_gen_ifmedia_sts);
	ifmedia_add(&sc->ifmedia, IFM_ETHER | IFM_AUTO, 0, 0);
	ifmedia_set(&sc->ifmedia, IFM_ETHER | IFM_AUTO);
}
