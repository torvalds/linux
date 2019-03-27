/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005, M. Warner Losh
 * All rights reserved.
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

#include "opt_ed.h"

#ifdef ED_3C503

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/syslog.h>

#include <sys/bus.h>

#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>		/* XXX: ed_3c503_mediachg() */
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_mib.h>
#include <net/if_media.h>

#include <net/bpf.h>

#include <dev/ed/if_edreg.h>
#include <dev/ed/if_edvar.h>

static void ed_3c503_mediachg(struct ed_softc *sc);

/*
 * Probe and vendor-specific initialization routine for 3Com 3c503 boards
 */
int
ed_probe_3Com(device_t dev, int port_rid, int flags)
{
	struct ed_softc *sc = device_get_softc(dev);
	int	error;
	int     i;
	u_int   memsize;
	u_char  isa16bit;
	rman_res_t	conf_maddr, conf_msize, irq, junk, pmem;

	error = ed_alloc_port(dev, 0, ED_3COM_IO_PORTS);
	if (error)
		return (error);

	sc->asic_offset = ED_3COM_ASIC_OFFSET;
	sc->nic_offset  = ED_3COM_NIC_OFFSET;

	/*
	 * Verify that the kernel configured I/O address matches the board
	 * configured address
	 */
	switch (ed_asic_inb(sc, ED_3COM_BCFR)) {
	case ED_3COM_BCFR_300:
		if (rman_get_start(sc->port_res) != 0x300)
			return (ENXIO);
		break;
	case ED_3COM_BCFR_310:
		if (rman_get_start(sc->port_res) != 0x310)
			return (ENXIO);
		break;
	case ED_3COM_BCFR_330:
		if (rman_get_start(sc->port_res) != 0x330)
			return (ENXIO);
		break;
	case ED_3COM_BCFR_350:
		if (rman_get_start(sc->port_res) != 0x350)
			return (ENXIO);
		break;
	case ED_3COM_BCFR_250:
		if (rman_get_start(sc->port_res) != 0x250)
			return (ENXIO);
		break;
	case ED_3COM_BCFR_280:
		if (rman_get_start(sc->port_res) != 0x280)
			return (ENXIO);
		break;
	case ED_3COM_BCFR_2A0:
		if (rman_get_start(sc->port_res) != 0x2a0)
			return (ENXIO);
		break;
	case ED_3COM_BCFR_2E0:
		if (rman_get_start(sc->port_res) != 0x2e0)
			return (ENXIO);
		break;
	default:
		return (ENXIO);
	}

	error = bus_get_resource(dev, SYS_RES_MEMORY, 0,
				 &conf_maddr, &conf_msize);
	if (error)
		return (error);

	/*
	 * Verify that the kernel shared memory address matches the board
	 * configured address.
	 */
	switch (ed_asic_inb(sc, ED_3COM_PCFR)) {
	case ED_3COM_PCFR_DC000:
		if (conf_maddr != 0xdc000)
			return (ENXIO);
		break;
	case ED_3COM_PCFR_D8000:
		if (conf_maddr != 0xd8000)
			return (ENXIO);
		break;
	case ED_3COM_PCFR_CC000:
		if (conf_maddr != 0xcc000)
			return (ENXIO);
		break;
	case ED_3COM_PCFR_C8000:
		if (conf_maddr != 0xc8000)
			return (ENXIO);
		break;
	default:
		return (ENXIO);
	}


	/*
	 * Reset NIC and ASIC. Enable on-board transceiver throughout reset
	 * sequence because it'll lock up if the cable isn't connected if we
	 * don't.
	 */
	ed_asic_outb(sc, ED_3COM_CR, ED_3COM_CR_RST | ED_3COM_CR_XSEL);

	/*
	 * Wait for a while, then un-reset it
	 */
	DELAY(50);

	/*
	 * The 3Com ASIC defaults to rather strange settings for the CR after
	 * a reset - it's important to set it again after the following outb
	 * (this is done when we map the PROM below).
	 */
	ed_asic_outb(sc, ED_3COM_CR, ED_3COM_CR_XSEL);

	/*
	 * Wait a bit for the NIC to recover from the reset
	 */
	DELAY(5000);

	sc->vendor = ED_VENDOR_3COM;
	sc->type_str = "3c503";
	sc->mem_shared = 1;
	sc->cr_proto = ED_CR_RD2;

	/*
	 * Hmmm...a 16bit 3Com board has 16k of memory, but only an 8k window
	 * to it.
	 */
	memsize = 8192;

	/*
	 * Get station address from on-board ROM
	 */

	/*
	 * First, map ethernet address PROM over the top of where the NIC
	 * registers normally appear.
	 */
	ed_asic_outb(sc, ED_3COM_CR, ED_3COM_CR_EALO | ED_3COM_CR_XSEL);

	for (i = 0; i < ETHER_ADDR_LEN; ++i)
		sc->enaddr[i] = ed_nic_inb(sc, i);

	/*
	 * Unmap PROM - select NIC registers. The proper setting of the
	 * tranceiver is set in ed_init so that the attach code is given a
	 * chance to set the default based on a compile-time config option
	 */
	ed_asic_outb(sc, ED_3COM_CR, ED_3COM_CR_XSEL);

	/*
	 * Determine if this is an 8bit or 16bit board
	 */

	/*
	 * select page 0 registers
	 */
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	ed_nic_outb(sc, ED_P0_CR, ED_CR_PAGE_0 | ED_CR_RD2 | ED_CR_STP);
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

	/*
	 * Attempt to clear WTS bit. If it doesn't clear, then this is a 16bit
	 * board.
	 */
	ed_nic_outb(sc, ED_P0_DCR, 0);

	/*
	 * select page 2 registers
	 */
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	ed_nic_outb(sc, ED_P0_CR, ED_CR_PAGE_2 | ED_CR_RD2 | ED_CR_STP);
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

	/*
	 * The 3c503 forces the WTS bit to a one if this is a 16bit board
	 */
	if (ed_nic_inb(sc, ED_P2_DCR) & ED_DCR_WTS)
		isa16bit = 1;
	else
		isa16bit = 0;

	/*
	 * select page 0 registers
	 */
	ed_nic_outb(sc, ED_P2_CR, ED_CR_RD2 | ED_CR_STP);

	error = ed_alloc_memory(dev, 0, memsize);
	if (error)
		return (error);

	pmem = rman_get_start(sc->mem_res);
	error = ed_isa_mem_ok(dev, pmem, memsize);
	if (error)
		return (error);

	sc->mem_start = 0;
	sc->mem_size = memsize;
	sc->mem_end = sc->mem_start + memsize;

	/*
	 * We have an entire 8k window to put the transmit buffers on the
	 * 16bit boards. But since the 16bit 3c503's shared memory is only
	 * fast enough to overlap the loading of one full-size packet, trying
	 * to load more than 2 buffers can actually leave the transmitter idle
	 * during the load. So 2 seems the best value. (Although a mix of
	 * variable-sized packets might change this assumption. Nonetheless,
	 * we optimize for linear transfers of same-size packets.)
	 */
	if (isa16bit) {
		if (flags & ED_FLAGS_NO_MULTI_BUFFERING)
			sc->txb_cnt = 1;
		else
			sc->txb_cnt = 2;

		sc->tx_page_start = ED_3COM_TX_PAGE_OFFSET_16BIT;
		sc->rec_page_start = ED_3COM_RX_PAGE_OFFSET_16BIT;
		sc->rec_page_stop = memsize / ED_PAGE_SIZE +
		    ED_3COM_RX_PAGE_OFFSET_16BIT;
		sc->mem_ring = sc->mem_start;
	} else {
		sc->txb_cnt = 1;
		sc->tx_page_start = ED_3COM_TX_PAGE_OFFSET_8BIT;
		sc->rec_page_start = ED_TXBUF_SIZE + ED_3COM_TX_PAGE_OFFSET_8BIT;
		sc->rec_page_stop = memsize / ED_PAGE_SIZE +
		    ED_3COM_TX_PAGE_OFFSET_8BIT;
		sc->mem_ring = sc->mem_start + (ED_PAGE_SIZE * ED_TXBUF_SIZE);
	}

	sc->isa16bit = isa16bit;

	/*
	 * Initialize GA page start/stop registers. Probably only needed if
	 * doing DMA, but what the hell.
	 */
	ed_asic_outb(sc, ED_3COM_PSTR, sc->rec_page_start);
	ed_asic_outb(sc, ED_3COM_PSPR, sc->rec_page_stop);

	/*
	 * Set IRQ. 3c503 only allows a choice of irq 2-5.
	 */
	error = bus_get_resource(dev, SYS_RES_IRQ, 0, &irq, &junk);
	if (error)
		return (error);

	switch (irq) {
	case 2:
	case 9:
		ed_asic_outb(sc, ED_3COM_IDCFR, ED_3COM_IDCFR_IRQ2);
		break;
	case 3:
		ed_asic_outb(sc, ED_3COM_IDCFR, ED_3COM_IDCFR_IRQ3);
		break;
	case 4:
		ed_asic_outb(sc, ED_3COM_IDCFR, ED_3COM_IDCFR_IRQ4);
		break;
	case 5:
		ed_asic_outb(sc, ED_3COM_IDCFR, ED_3COM_IDCFR_IRQ5);
		break;
	default:
		device_printf(dev, "Invalid irq configuration (%jd) must be 3-5,9 for 3c503\n",
			      irq);
		return (ENXIO);
	}

	/*
	 * Initialize GA configuration register. Set bank and enable shared
	 * mem.
	 */
	ed_asic_outb(sc, ED_3COM_GACFR, ED_3COM_GACFR_RSEL |
	    ED_3COM_GACFR_MBS0);

	/*
	 * Initialize "Vector Pointer" registers. These gawd-awful things are
	 * compared to 20 bits of the address on ISA, and if they match, the
	 * shared memory is disabled. We set them to 0xffff0...allegedly the
	 * reset vector.
	 */
	ed_asic_outb(sc, ED_3COM_VPTR2, 0xff);
	ed_asic_outb(sc, ED_3COM_VPTR1, 0xff);
	ed_asic_outb(sc, ED_3COM_VPTR0, 0x00);

	error = ed_clear_memory(dev);
	if (error == 0) {
		sc->sc_mediachg = ed_3c503_mediachg;
		sc->sc_write_mbufs = ed_shmem_write_mbufs;
	}
	return (error);
}

static void
ed_3c503_mediachg(struct ed_softc *sc)
{
	struct ifnet *ifp = sc->ifp;

	/*
	 * If this is a 3Com board, the tranceiver must be software enabled
	 * (there is no settable hardware default).
	 */
	if (ifp->if_flags & IFF_LINK2)
		ed_asic_outb(sc, ED_3COM_CR, 0);
	else
		ed_asic_outb(sc, ED_3COM_CR, ED_3COM_CR_XSEL);
}

#endif /* ED_3C503 */
