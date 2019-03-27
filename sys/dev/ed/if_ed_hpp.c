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

#ifdef ED_HPP

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
#include <net/if_var.h>		/* XXX: ed_hpp_set_physical_link() */
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_mib.h>
#include <net/if_media.h>

#include <net/bpf.h>

#include <dev/ed/if_edreg.h>
#include <dev/ed/if_edvar.h>

static void	ed_hpp_readmem(struct ed_softc *, bus_size_t, uint8_t *,
		    uint16_t);
static void	ed_hpp_writemem(struct ed_softc *, uint8_t *, uint16_t,
		    uint16_t);
static void	ed_hpp_set_physical_link(struct ed_softc *sc);
static u_short	ed_hpp_write_mbufs(struct ed_softc *, struct mbuf *,
		    bus_size_t);

/*
 * Interrupt conversion table for the HP PC LAN+
 */
static uint16_t ed_hpp_intr_val[] = {
	0,		/* 0 */
	0,		/* 1 */
	0,		/* 2 */
	3,		/* 3 */
	4,		/* 4 */
	5,		/* 5 */
	6,		/* 6 */
	7,		/* 7 */
	0,		/* 8 */
	9,		/* 9 */
	10,		/* 10 */
	11,		/* 11 */
	12,		/* 12 */
	0,		/* 13 */
	0,		/* 14 */
	15		/* 15 */
};

#define	ED_HPP_TEST_SIZE	16

/*
 * Probe and vendor specific initialization for the HP PC Lan+ Cards.
 * (HP Part nos: 27247B and 27252A).
 *
 * The card has an asic wrapper around a DS8390 core.  The asic handles 
 * host accesses and offers both standard register IO and memory mapped 
 * IO.  Memory mapped I/O allows better performance at the expense of greater
 * chance of an incompatibility with existing ISA cards.
 *
 * The card has a few caveats: it isn't tolerant of byte wide accesses, only
 * short (16 bit) or word (32 bit) accesses are allowed.  Some card revisions
 * don't allow 32 bit accesses; these are indicated by a bit in the software
 * ID register (see if_edreg.h).
 * 
 * Other caveats are: we should read the MAC address only when the card
 * is inactive.
 *
 * For more information; please consult the CRYNWR packet driver.
 *
 * The AUI port is turned on using the "link2" option on the ifconfig 
 * command line.
 */
int
ed_probe_HP_pclanp(device_t dev, int port_rid, int flags)
{
	struct ed_softc *sc = device_get_softc(dev);
	int error;
	int n;				/* temp var */
	int memsize;			/* mem on board */
	u_char checksum;		/* checksum of board address */
	u_char irq;			/* board configured IRQ */
	uint8_t test_pattern[ED_HPP_TEST_SIZE];	/* read/write areas for */
	uint8_t test_buffer[ED_HPP_TEST_SIZE];	/* probing card */
	rman_res_t conf_maddr, conf_msize, conf_irq, junk;

	error = ed_alloc_port(dev, 0, ED_HPP_IO_PORTS);
	if (error)
		return (error);

	/* Fill in basic information */
	sc->asic_offset = ED_HPP_ASIC_OFFSET;
	sc->nic_offset  = ED_HPP_NIC_OFFSET;

	sc->chip_type = ED_CHIP_TYPE_DP8390;
	sc->isa16bit = 0;	/* the 8390 core needs to be in byte mode */

	/* 
	 * Look for the HP PCLAN+ signature: "0x50,0x48,0x00,0x53" 
	 */
	
	if ((ed_asic_inb(sc, ED_HPP_ID) != 0x50) || 
	    (ed_asic_inb(sc, ED_HPP_ID + 1) != 0x48) ||
	    ((ed_asic_inb(sc, ED_HPP_ID + 2) & 0xF0) != 0) ||
	    (ed_asic_inb(sc, ED_HPP_ID + 3) != 0x53))
		return (ENXIO);

	/* 
	 * Read the MAC address and verify checksum on the address.
	 */

	ed_asic_outw(sc, ED_HPP_PAGING, ED_HPP_PAGE_MAC);
	for (n  = 0, checksum = 0; n < ETHER_ADDR_LEN; n++)
		checksum += (sc->enaddr[n] = 
		    ed_asic_inb(sc, ED_HPP_MAC_ADDR + n));
	
	checksum += ed_asic_inb(sc, ED_HPP_MAC_ADDR + ETHER_ADDR_LEN);

	if (checksum != 0xFF)
		return (ENXIO);

	/*
	 * Verify that the software model number is 0.
	 */
	
	ed_asic_outw(sc, ED_HPP_PAGING, ED_HPP_PAGE_ID);
	if (((sc->hpp_id = ed_asic_inw(sc, ED_HPP_PAGE_4)) & 
		ED_HPP_ID_SOFT_MODEL_MASK) != 0x0000)
		return (ENXIO);

	/*
	 * Read in and save the current options configured on card.
	 */

	sc->hpp_options = ed_asic_inw(sc, ED_HPP_OPTION);

	sc->hpp_options |= (ED_HPP_OPTION_NIC_RESET | 
	    ED_HPP_OPTION_CHIP_RESET | ED_HPP_OPTION_ENABLE_IRQ);

	/* 
	 * Reset the chip.  This requires writing to the option register
	 * so take care to preserve the other bits.
	 */

	ed_asic_outw(sc, ED_HPP_OPTION, 
	    (sc->hpp_options & ~(ED_HPP_OPTION_NIC_RESET | 
	    ED_HPP_OPTION_CHIP_RESET)));

	DELAY(5000);	/* wait for chip reset to complete */

	ed_asic_outw(sc, ED_HPP_OPTION,
	    (sc->hpp_options | (ED_HPP_OPTION_NIC_RESET |
	    ED_HPP_OPTION_CHIP_RESET |
	    ED_HPP_OPTION_ENABLE_IRQ)));

	DELAY(5000);

	if (!(ed_nic_inb(sc, ED_P0_ISR) & ED_ISR_RST))
		return (ENXIO);	/* reset did not complete */

	/*
	 * Read out configuration information.
	 */

	ed_asic_outw(sc, ED_HPP_PAGING, ED_HPP_PAGE_HW);

	irq = ed_asic_inb(sc, ED_HPP_HW_IRQ);

	/*
 	 * Check for impossible IRQ.
	 */

	if (irq >= nitems(ed_hpp_intr_val))
		return (ENXIO);

	/* 
	 * If the kernel IRQ was specified with a '?' use the cards idea
	 * of the IRQ.  If the kernel IRQ was explicitly specified, it
 	 * should match that of the hardware.
	 */
	error = bus_get_resource(dev, SYS_RES_IRQ, 0, &conf_irq, &junk);
	if (error)
		bus_set_resource(dev, SYS_RES_IRQ, 0, ed_hpp_intr_val[irq], 1);
	else {
		if (conf_irq != ed_hpp_intr_val[irq])
			return (ENXIO);
	}

	/*
	 * Fill in softconfig info.
	 */

	sc->vendor = ED_VENDOR_HP;
	sc->type = ED_TYPE_HP_PCLANPLUS;
	sc->type_str = "HP-PCLAN+";

	sc->mem_shared = 0;	/* we DON'T have dual ported RAM */
	sc->mem_start = 0;	/* we use offsets inside the card RAM */

	sc->hpp_mem_start = NULL;/* no memory mapped I/O by default */

	/*
	 * The board has 32KB of memory.  Is there a way to determine
	 * this programmatically?
	 */
	
	memsize = 32768;

	/*
	 * Check if memory mapping of the I/O registers possible.
	 */
	if (sc->hpp_options & ED_HPP_OPTION_MEM_ENABLE) {
		u_long mem_addr;

		/*
		 * determine the memory address from the board.
		 */
		
		ed_asic_outw(sc, ED_HPP_PAGING, ED_HPP_PAGE_HW);
		mem_addr = (ed_asic_inw(sc, ED_HPP_HW_MEM_MAP) << 8);

		/*
		 * Check that the kernel specified start of memory and
		 * hardware's idea of it match.
		 */
		error = bus_get_resource(dev, SYS_RES_MEMORY, 0,
					 &conf_maddr, &conf_msize);
		if (error)
			return (error);
		
		if (mem_addr != conf_maddr)
			return (ENXIO);

		error = ed_alloc_memory(dev, 0, memsize);
		if (error)
			return (error);

		sc->hpp_mem_start = rman_get_virtual(sc->mem_res);
	}

	/*
	 * Fill in the rest of the soft config structure.
	 */

	/*
	 * The transmit page index.
	 */

	sc->tx_page_start = ED_HPP_TX_PAGE_OFFSET;

	if (device_get_flags(dev) & ED_FLAGS_NO_MULTI_BUFFERING)
		sc->txb_cnt = 1;
	else
		sc->txb_cnt = 2;

	/*
	 * Memory description
	 */

	sc->mem_size = memsize;
	sc->mem_ring = sc->mem_start + 
		(sc->txb_cnt * ED_PAGE_SIZE * ED_TXBUF_SIZE);
	sc->mem_end = sc->mem_start + sc->mem_size;

	/*
	 * Receive area starts after the transmit area and 
	 * continues till the end of memory.
	 */

	sc->rec_page_start = sc->tx_page_start + 
				(sc->txb_cnt * ED_TXBUF_SIZE);
	sc->rec_page_stop = (sc->mem_size / ED_PAGE_SIZE);


	sc->cr_proto = 0;	/* value works */

	/*
	 * Set the wrap registers for string I/O reads.
	 */

	ed_asic_outw(sc, ED_HPP_PAGING, ED_HPP_PAGE_HW);
	ed_asic_outw(sc, ED_HPP_HW_WRAP,
	    ((sc->rec_page_start / ED_PAGE_SIZE) |
	    (((sc->rec_page_stop / ED_PAGE_SIZE) - 1) << 8)));

	/*
	 * Reset the register page to normal operation.
	 */

	ed_asic_outw(sc, ED_HPP_PAGING, ED_HPP_PAGE_PERF);

	/*
	 * Verify that we can read/write from adapter memory.
	 * Create test pattern.
	 */

	for (n = 0; n < ED_HPP_TEST_SIZE; n++)
		test_pattern[n] = (n*n) ^ ~n;

#undef	ED_HPP_TEST_SIZE

	/*
	 * Check that the memory is accessible thru the I/O ports.
	 * Write out the contents of "test_pattern", read back
	 * into "test_buffer" and compare the two for any
	 * mismatch.
	 */

	for (n = 0; n < (32768 / ED_PAGE_SIZE); n ++) {
		ed_hpp_writemem(sc, test_pattern, (n * ED_PAGE_SIZE), 
				sizeof(test_pattern));
		ed_hpp_readmem(sc, (n * ED_PAGE_SIZE), 
			test_buffer, sizeof(test_pattern));

		if (bcmp(test_pattern, test_buffer, 
			sizeof(test_pattern)))
			return (ENXIO);
	}

	sc->sc_mediachg = ed_hpp_set_physical_link;
	sc->sc_write_mbufs = ed_hpp_write_mbufs;
	sc->readmem = ed_hpp_readmem;
	return (0);
}

/*
 * HP PC Lan+ : Set the physical link to use AUI or TP/TL.
 */

static void
ed_hpp_set_physical_link(struct ed_softc *sc)
{
	struct ifnet *ifp = sc->ifp;
	int lan_page;

	ed_asic_outw(sc, ED_HPP_PAGING, ED_HPP_PAGE_LAN);
	lan_page = ed_asic_inw(sc, ED_HPP_PAGE_0);

	if (ifp->if_flags & IFF_LINK2) {
		/*
		 * Use the AUI port.
		 */

		lan_page |= ED_HPP_LAN_AUI;
		ed_asic_outw(sc, ED_HPP_PAGING, ED_HPP_PAGE_LAN);
		ed_asic_outw(sc, ED_HPP_PAGE_0, lan_page);
	} else {
		/*
		 * Use the ThinLan interface
		 */

		lan_page &= ~ED_HPP_LAN_AUI;
		ed_asic_outw(sc, ED_HPP_PAGING, ED_HPP_PAGE_LAN);
		ed_asic_outw(sc, ED_HPP_PAGE_0, lan_page);
	}

	/*
	 * Wait for the lan card to re-initialize itself
	 */
	DELAY(150000);	/* wait 150 ms */

	/*
	 * Restore normal pages.
	 */
	ed_asic_outw(sc, ED_HPP_PAGING, ED_HPP_PAGE_PERF);
}

/*
 * Support routines to handle the HP PC Lan+ card.
 */

/*
 * HP PC Lan+: Read from NIC memory, using either PIO or memory mapped
 * IO.
 */

static void
ed_hpp_readmem(struct ed_softc *sc, bus_size_t src, uint8_t *dst,
    uint16_t amount)
{
	int use_32bit_access = !(sc->hpp_id & ED_HPP_ID_16_BIT_ACCESS);

	/* Program the source address in RAM */
	ed_asic_outw(sc, ED_HPP_PAGE_2, src);

	/*
	 * The HP PC Lan+ card supports word reads as well as
	 * a memory mapped i/o port that is aliased to every 
	 * even address on the board.
	 */
	if (sc->hpp_mem_start) {
		/* Enable memory mapped access.  */
		ed_asic_outw(sc, ED_HPP_OPTION, sc->hpp_options & 
			~(ED_HPP_OPTION_MEM_DISABLE | 
			  ED_HPP_OPTION_BOOT_ROM_ENB));

		if (use_32bit_access && (amount > 3)) {
			uint32_t *dl = (uint32_t *) dst;	
			volatile uint32_t *const sl = 
				(uint32_t *) sc->hpp_mem_start;
			uint32_t *const fence = dl + (amount >> 2);
			
			/*
			 * Copy out NIC data.  We could probably write this
			 * as a `movsl'. The currently generated code is lousy.
			 */
			while (dl < fence)
				*dl++ = *sl;
		
			dst += (amount & ~3);
			amount &= 3;

		} 

		/* Finish off any words left, as a series of short reads */
		if (amount > 1) {
			u_short *d = (u_short *) dst;	
			volatile u_short *const s = 
				(u_short *) sc->hpp_mem_start;
			u_short *const fence = d + (amount >> 1);
			
			/* Copy out NIC data.  */
			while (d < fence)
				*d++ = *s;
	
			dst += (amount & ~1);
			amount &= 1;
		}

		/*
		 * read in a byte; however we need to always read 16 bits
		 * at a time or the hardware gets into a funny state
		 */

		if (amount == 1) {
			/* need to read in a short and copy LSB */
			volatile u_short *const s = 
				(volatile u_short *) sc->hpp_mem_start;
			*dst = (*s) & 0xFF;	
		}

		/* Restore Boot ROM access.  */
		ed_asic_outw(sc, ED_HPP_OPTION, sc->hpp_options);
	} else { 
		/* Read in data using the I/O port */
		if (use_32bit_access && (amount > 3)) {
			ed_asic_insl(sc, ED_HPP_PAGE_4, dst, amount >> 2);
			dst += (amount & ~3);
			amount &= 3;
		}
		if (amount > 1) {
			ed_asic_insw(sc, ED_HPP_PAGE_4, dst, amount >> 1);
			dst += (amount & ~1);
			amount &= 1;
		}
		if (amount == 1) { /* read in a short and keep the LSB */
			*dst = ed_asic_inw(sc, ED_HPP_PAGE_4) & 0xFF;
		}
	}
}

/*
 * HP PC Lan+: Write to NIC memory, using either PIO or memory mapped
 * IO.
 *	Only used in the probe routine to test the memory. 'len' must
 *	be even.
 */
static void
ed_hpp_writemem(struct ed_softc *sc, uint8_t *src, uint16_t dst, uint16_t len)
{
	/* reset remote DMA complete flag */
	ed_nic_outb(sc, ED_P0_ISR, ED_ISR_RDC);

	/* program the write address in RAM */
	ed_asic_outw(sc, ED_HPP_PAGE_0, dst);

	if (sc->hpp_mem_start) {
		u_short *s = (u_short *) src;
		volatile u_short *d = (u_short *) sc->hpp_mem_start;
		u_short *const fence = s + (len >> 1);

		/*
		 * Enable memory mapped access.
		 */
		ed_asic_outw(sc, ED_HPP_OPTION, sc->hpp_options & 
			~(ED_HPP_OPTION_MEM_DISABLE | 
			  ED_HPP_OPTION_BOOT_ROM_ENB));

		/*
		 * Copy to NIC memory.
		 */
		while (s < fence)
			*d = *s++;

		/*
		 * Restore Boot ROM access.
		 */
		ed_asic_outw(sc, ED_HPP_OPTION, sc->hpp_options);
	} else {
		/* write data using I/O writes */
		ed_asic_outsw(sc, ED_HPP_PAGE_4, src, len / 2);
	}
}

/*
 * Write to HP PC Lan+ NIC memory.  Access to the NIC can be by using 
 * outsw() or via the memory mapped interface to the same register.
 * Writes have to be in word units; byte accesses won't work and may cause
 * the NIC to behave weirdly. Long word accesses are permitted if the ASIC
 * allows it.
 */

static u_short
ed_hpp_write_mbufs(struct ed_softc *sc, struct mbuf *m, bus_size_t dst)
{
	int len, wantbyte;
	unsigned short total_len;
	unsigned char savebyte[2];
	volatile u_short * const d = 
		(volatile u_short *) sc->hpp_mem_start;
	int use_32bit_accesses = !(sc->hpp_id & ED_HPP_ID_16_BIT_ACCESS);

	/* select page 0 registers */
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	ed_nic_outb(sc, ED_P0_CR, sc->cr_proto | ED_CR_STA);
	ed_nic_barrier(sc, ED_P0_CR, 1,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);

	/* reset remote DMA complete flag */
	ed_nic_outb(sc, ED_P0_ISR, ED_ISR_RDC);

	/* program the write address in RAM */
	ed_asic_outw(sc, ED_HPP_PAGE_0, dst);

	if (sc->hpp_mem_start) 	/* enable memory mapped I/O */
		ed_asic_outw(sc, ED_HPP_OPTION, sc->hpp_options & 
			~(ED_HPP_OPTION_MEM_DISABLE |
			ED_HPP_OPTION_BOOT_ROM_ENB));

	wantbyte = 0;
	total_len = 0;

	if (sc->hpp_mem_start) {	/* Memory mapped I/O port */
		while (m) {
			total_len += (len = m->m_len);
			if (len) {
				caddr_t data = mtod(m, caddr_t);
				/* finish the last word of the previous mbuf */
				if (wantbyte) {
					savebyte[1] = *data;
					*d = *((u_short *) savebyte);
					data++; len--; wantbyte = 0;
				}
				/* output contiguous words */
				if ((len > 3) && (use_32bit_accesses)) {
					volatile uint32_t *const dl = 
						(volatile uint32_t *) d;
					uint32_t *sl = (uint32_t *) data;
					uint32_t *fence = sl + (len >> 2);

					while (sl < fence)
						*dl = *sl++;

					data += (len & ~3);
					len &= 3;
				}
				/* finish off remain 16 bit writes */
				if (len > 1) {
					u_short *s = (u_short *) data;
					u_short *fence = s + (len >> 1);

					while (s < fence)
						*d = *s++;

					data += (len & ~1); 
					len &= 1;
				}
				/* save last byte if needed */
				if ((wantbyte = (len == 1)) != 0)
					savebyte[0] = *data;
			}
			m = m->m_next;	/* to next mbuf */
		}
		if (wantbyte) /* write last byte */
			*d = *((u_short *) savebyte);
	} else {
		/* use programmed I/O */
		while (m) {
			total_len += (len = m->m_len);
			if (len) {
				caddr_t data = mtod(m, caddr_t);
				/* finish the last word of the previous mbuf */
				if (wantbyte) {
					savebyte[1] = *data;
					ed_asic_outw(sc, ED_HPP_PAGE_4,
						     *((u_short *)savebyte));
					data++; 
					len--; 
					wantbyte = 0;
				}
				/* output contiguous words */
				if ((len > 3) && use_32bit_accesses) {
					ed_asic_outsl(sc, ED_HPP_PAGE_4,
						      data, len >> 2);
					data += (len & ~3);
					len &= 3;
				}
				/* finish off remaining 16 bit accesses */
				if (len > 1) {
					ed_asic_outsw(sc, ED_HPP_PAGE_4,
						      data, len >> 1);
					data += (len & ~1);
					len &= 1;
				}
				if ((wantbyte = (len == 1)) != 0)
					savebyte[0] = *data;

			} /* if len != 0 */
			m = m->m_next;
		}
		if (wantbyte) /* spit last byte */
			ed_asic_outw(sc, ED_HPP_PAGE_4, *(u_short *)savebyte);

	}

	if (sc->hpp_mem_start)	/* turn off memory mapped i/o */
		ed_asic_outw(sc, ED_HPP_OPTION, sc->hpp_options);

	return (total_len);
}

#endif /* ED_HPP */
