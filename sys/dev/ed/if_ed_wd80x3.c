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
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_mib.h>
#include <net/if_media.h>

#include <net/bpf.h>

#include <dev/ed/if_edreg.h>
#include <dev/ed/if_edvar.h>

/*
 * Interrupt conversion table for WD/SMC ASIC/83C584
 */
static uint16_t ed_intr_val[] = {
	9,
	3,
	5,
	7,
	10,
	11,
	15,
	4
};

/*
 * Interrupt conversion table for 83C790
 */
static uint16_t ed_790_intr_val[] = {
	0,
	9,
	3,
	5,
	7,
	10,
	11,
	15
};

/*
 * Probe and vendor-specific initialization routine for SMC/WD80x3 boards
 */
int
ed_probe_WD80x3_generic(device_t dev, int flags, uint16_t *intr_vals[])
{
	struct ed_softc *sc = device_get_softc(dev);
	int	error;
	int     i;
	u_int   memsize;
	u_char  iptr, isa16bit, sum, totalsum;
	rman_res_t	irq, junk, pmem;

	sc->chip_type = ED_CHIP_TYPE_DP8390;

	if (ED_FLAGS_GETTYPE(flags) == ED_FLAGS_TOSH_ETHER) {
		totalsum = ED_WD_ROM_CHECKSUM_TOTAL_TOSH_ETHER;
		ed_asic_outb(sc, ED_WD_MSR, ED_WD_MSR_POW);
		DELAY(10000);
	}
	else
		totalsum = ED_WD_ROM_CHECKSUM_TOTAL;

	/*
	 * Attempt to do a checksum over the station address PROM. If it
	 * fails, it's probably not a SMC/WD board. There is a problem with
	 * this, though: some clone WD boards don't pass the checksum test.
	 * Danpex boards for one.
	 */
	for (sum = 0, i = 0; i < 8; ++i)
		sum += ed_asic_inb(sc, ED_WD_PROM + i);

	if (sum != totalsum) {
		/*
		 * Checksum is invalid. This often happens with cheap WD8003E
		 * clones.  In this case, the checksum byte (the eighth byte)
		 * seems to always be zero.
		 */
		if (ed_asic_inb(sc, ED_WD_CARD_ID) != ED_TYPE_WD8003E ||
		    ed_asic_inb(sc, ED_WD_PROM + 7) != 0)
			return (ENXIO);
	}
	/* reset card to force it into a known state. */
	if (ED_FLAGS_GETTYPE(flags) == ED_FLAGS_TOSH_ETHER)
		ed_asic_outb(sc, ED_WD_MSR, ED_WD_MSR_RST | ED_WD_MSR_POW);
	else
		ed_asic_outb(sc, ED_WD_MSR, ED_WD_MSR_RST);

	DELAY(100);
	ed_asic_outb(sc, ED_WD_MSR, ed_asic_inb(sc, ED_WD_MSR) & ~ED_WD_MSR_RST);
	/* wait in the case this card is reading its EEROM */
	DELAY(5000);

	sc->vendor = ED_VENDOR_WD_SMC;
	sc->type = ed_asic_inb(sc, ED_WD_CARD_ID);

	/*
	 * Set initial values for width/size.
	 */
	memsize = 8192;
	isa16bit = 0;
	switch (sc->type) {
	case ED_TYPE_WD8003S:
		sc->type_str = "WD8003S";
		break;
	case ED_TYPE_WD8003E:
		sc->type_str = "WD8003E";
		break;
	case ED_TYPE_WD8003EB:
		sc->type_str = "WD8003EB";
		break;
	case ED_TYPE_WD8003W:
		sc->type_str = "WD8003W";
		break;
	case ED_TYPE_WD8013EBT:
		sc->type_str = "WD8013EBT";
		memsize = 16384;
		isa16bit = 1;
		break;
	case ED_TYPE_WD8013W:
		sc->type_str = "WD8013W";
		memsize = 16384;
		isa16bit = 1;
		break;
	case ED_TYPE_WD8013EP:	/* also WD8003EP */
		if (ed_asic_inb(sc, ED_WD_ICR) & ED_WD_ICR_16BIT) {
			isa16bit = 1;
			memsize = 16384;
			sc->type_str = "WD8013EP";
		} else
			sc->type_str = "WD8003EP";
		break;
	case ED_TYPE_WD8013WC:
		sc->type_str = "WD8013WC";
		memsize = 16384;
		isa16bit = 1;
		break;
	case ED_TYPE_WD8013EBP:
		sc->type_str = "WD8013EBP";
		memsize = 16384;
		isa16bit = 1;
		break;
	case ED_TYPE_WD8013EPC:
		sc->type_str = "WD8013EPC";
		memsize = 16384;
		isa16bit = 1;
		break;
	case ED_TYPE_SMC8216C: /* 8216 has 16K shared mem -- 8416 has 8K */
	case ED_TYPE_SMC8216T:
		if (sc->type == ED_TYPE_SMC8216C)
			sc->type_str = "SMC8216/SMC8216C";
		else
			sc->type_str = "SMC8216T";

		ed_asic_outb(sc, ED_WD790_HWR,
		    ed_asic_inb(sc, ED_WD790_HWR) | ED_WD790_HWR_SWH);
		switch (ed_asic_inb(sc, ED_WD790_RAR) & ED_WD790_RAR_SZ64) {
		case ED_WD790_RAR_SZ64:
			memsize = 65536;
			break;
		case ED_WD790_RAR_SZ32:
			memsize = 32768;
			break;
		case ED_WD790_RAR_SZ16:
			memsize = 16384;
			break;
		case ED_WD790_RAR_SZ8:
			/* 8216 has 16K shared mem -- 8416 has 8K */
			if (sc->type == ED_TYPE_SMC8216C)
				sc->type_str = "SMC8416C/SMC8416BT";
			else
				sc->type_str = "SMC8416T";
			memsize = 8192;
			break;
		}
		ed_asic_outb(sc, ED_WD790_HWR,
		    ed_asic_inb(sc, ED_WD790_HWR) & ~ED_WD790_HWR_SWH);

		isa16bit = 1;
		sc->chip_type = ED_CHIP_TYPE_WD790;
		break;
	case ED_TYPE_TOSHIBA1:
		sc->type_str = "Toshiba1";
		memsize = 32768;
		isa16bit = 1;
		break;
	case ED_TYPE_TOSHIBA4:
		sc->type_str = "Toshiba4";
		memsize = 32768;
		isa16bit = 1;
		break;
	default:
		sc->type_str = "";
		break;
	}

	/*
	 * Make some adjustments to initial values depending on what is found
	 * in the ICR.
	 */
	if (isa16bit && (sc->type != ED_TYPE_WD8013EBT)
	  && (sc->type != ED_TYPE_TOSHIBA1) && (sc->type != ED_TYPE_TOSHIBA4)
	    && ((ed_asic_inb(sc, ED_WD_ICR) & ED_WD_ICR_16BIT) == 0)) {
		isa16bit = 0;
		memsize = 8192;
	}

	/* Override memsize? XXX */
	error = ed_alloc_memory(dev, 0, memsize);
	if (error)
		return (error);
	sc->mem_start = 0;

#ifdef ED_DEBUG
	printf("type = %x type_str=%s isa16bit=%d memsize=%d id_msize=%lu\n",
	    sc->type, sc->type_str, isa16bit, memsize,
	    rman_get_size(sc->mem_res));
	for (i = 0; i < 8; i++)
		printf("%x -> %x\n", i, ed_asic_inb(sc, i));
#endif
	pmem = rman_get_start(sc->mem_res);
	if (!(flags & ED_FLAGS_PCCARD)) {
		error = ed_isa_mem_ok(dev, pmem, memsize);
		if (error)
			return (error);
	}

	/*
	 * (note that if the user specifies both of the following flags that
	 * '8bit' mode intentionally has precedence)
	 */
	if (flags & ED_FLAGS_FORCE_16BIT_MODE)
		isa16bit = 1;
	if (flags & ED_FLAGS_FORCE_8BIT_MODE)
		isa16bit = 0;

	/*
	 * If possible, get the assigned interrupt number from the card and
	 * use it.
	 */
	if ((sc->type & ED_WD_SOFTCONFIG) &&
	    (sc->chip_type != ED_CHIP_TYPE_WD790)) {

		/*
		 * Assemble together the encoded interrupt number.
		 */
		iptr = (ed_asic_inb(sc, ED_WD_ICR) & ED_WD_ICR_IR2) |
		    ((ed_asic_inb(sc, ED_WD_IRR) &
		      (ED_WD_IRR_IR0 | ED_WD_IRR_IR1)) >> 5);

		/*
		 * If no interrupt specified (or "?"), use what the board tells us.
		 */
		error = bus_get_resource(dev, SYS_RES_IRQ, 0, &irq, &junk);
		if (error && intr_vals[0] != NULL)
			error = bus_set_resource(dev, SYS_RES_IRQ, 0,
			    intr_vals[0][iptr], 1);
		if (error)
			return (error);

		/*
		 * Enable the interrupt.
		 */
		ed_asic_outb(sc, ED_WD_IRR,
		     ed_asic_inb(sc, ED_WD_IRR) | ED_WD_IRR_IEN);
	}
	if (sc->chip_type == ED_CHIP_TYPE_WD790) {
		ed_asic_outb(sc, ED_WD790_HWR,
		  ed_asic_inb(sc, ED_WD790_HWR) | ED_WD790_HWR_SWH);
		iptr = (((ed_asic_inb(sc, ED_WD790_GCR) & ED_WD790_GCR_IR2) >> 4) |
			(ed_asic_inb(sc, ED_WD790_GCR) &
			 (ED_WD790_GCR_IR1 | ED_WD790_GCR_IR0)) >> 2);
		ed_asic_outb(sc, ED_WD790_HWR,
		    ed_asic_inb(sc, ED_WD790_HWR) & ~ED_WD790_HWR_SWH);

		/*
		 * If no interrupt specified (or "?"), use what the board tells us.
		 */
		error = bus_get_resource(dev, SYS_RES_IRQ, 0, &irq, &junk);
		if (error && intr_vals[1] != NULL)
			error = bus_set_resource(dev, SYS_RES_IRQ, 0,
			  intr_vals[1][iptr], 1);
		if (error)
			return (error);

		/*
		 * Enable interrupts.
		 */
		ed_asic_outb(sc, ED_WD790_ICR,
		  ed_asic_inb(sc, ED_WD790_ICR) | ED_WD790_ICR_EIL);
	}
	error = bus_get_resource(dev, SYS_RES_IRQ, 0, &irq, &junk);
	if (error) {
		device_printf(dev, "%s cards don't support auto-detected/assigned interrupts.\n",
			      sc->type_str);
		return (ENXIO);
	}
	sc->isa16bit = isa16bit;
	sc->mem_shared = 1;

	/*
	 * allocate one xmit buffer if < 16k, two buffers otherwise
	 */
	if (memsize < 16384 || (flags & ED_FLAGS_NO_MULTI_BUFFERING))
		sc->txb_cnt = 1;
	else
		sc->txb_cnt = 2;
	sc->tx_page_start = ED_WD_PAGE_OFFSET;
	sc->rec_page_start = ED_WD_PAGE_OFFSET + ED_TXBUF_SIZE * sc->txb_cnt;
	sc->rec_page_stop = ED_WD_PAGE_OFFSET + memsize / ED_PAGE_SIZE;
	sc->mem_ring = sc->mem_start + (ED_PAGE_SIZE * sc->rec_page_start);
	sc->mem_size = memsize;
	sc->mem_end = sc->mem_start + memsize;

	/*
	 * Get station address from on-board ROM
	 */
	for (i = 0; i < ETHER_ADDR_LEN; ++i)
		sc->enaddr[i] = ed_asic_inb(sc, ED_WD_PROM + i);

	/*
	 * Set upper address bits and 8/16 bit access to shared memory.
	 */
	if (isa16bit) {
		if (sc->chip_type == ED_CHIP_TYPE_WD790)
			sc->wd_laar_proto = ed_asic_inb(sc, ED_WD_LAAR);
		else
			sc->wd_laar_proto = ED_WD_LAAR_L16EN |
			    ((pmem >> 19) & ED_WD_LAAR_ADDRHI);
		/*
		 * Enable 16bit access
		 */
		ed_asic_outb(sc, ED_WD_LAAR, sc->wd_laar_proto |
		    ED_WD_LAAR_M16EN);
	} else {
		if (((sc->type & ED_WD_SOFTCONFIG) ||
		     (sc->type == ED_TYPE_TOSHIBA1) ||
		     (sc->type == ED_TYPE_TOSHIBA4) ||
		     (sc->type == ED_TYPE_WD8013EBT)) &&
		    (sc->chip_type != ED_CHIP_TYPE_WD790)) {
			sc->wd_laar_proto = (pmem >> 19) &
			    ED_WD_LAAR_ADDRHI;
			ed_asic_outb(sc, ED_WD_LAAR, sc->wd_laar_proto);
		}
	}

	/*
	 * Set address and enable interface shared memory.
	 */
	if (sc->chip_type != ED_CHIP_TYPE_WD790) {
		if (ED_FLAGS_GETTYPE(flags) == ED_FLAGS_TOSH_ETHER) {
			ed_asic_outb(sc, ED_WD_MSR + 1,
			    ((pmem >> 8) & 0xe0) | 4);
			ed_asic_outb(sc, ED_WD_MSR + 2, ((pmem >> 16) & 0x0f));
			ed_asic_outb(sc, ED_WD_MSR,
			    ED_WD_MSR_MENB | ED_WD_MSR_POW);
		} else {
			ed_asic_outb(sc, ED_WD_MSR, ((pmem >> 13) &
			    ED_WD_MSR_ADDR) | ED_WD_MSR_MENB);
		}
		sc->cr_proto = ED_CR_RD2;
	} else {
		ed_asic_outb(sc, ED_WD_MSR, ED_WD_MSR_MENB);
		ed_asic_outb(sc, ED_WD790_HWR,
		    (ed_asic_inb(sc, ED_WD790_HWR) | ED_WD790_HWR_SWH));
		ed_asic_outb(sc, ED_WD790_RAR,
		    ((pmem >> 13) & 0x0f) | ((pmem >> 11) & 0x40) |
		     (ed_asic_inb(sc, ED_WD790_RAR) & 0xb0));
		ed_asic_outb(sc, ED_WD790_HWR,
		    (ed_asic_inb(sc, ED_WD790_HWR) & ~ED_WD790_HWR_SWH));
		sc->cr_proto = 0;
	}

	/*
	 * Disable 16bit access to shared memory - we leave it
	 * disabled so that 1) machines reboot properly when the board
	 * is set 16 bit mode and there are conflicting 8bit
	 * devices/ROMS in the same 128k address space as this boards
	 * shared memory. and 2) so that other 8 bit devices with
	 * shared memory can be used in this 128k region, too.
	 */
	error = ed_clear_memory(dev);
	ed_disable_16bit_access(sc);
	sc->sc_write_mbufs = ed_shmem_write_mbufs;
	return (error);
}

int
ed_probe_WD80x3(device_t dev, int port_rid, int flags)
{
	struct ed_softc *sc = device_get_softc(dev);
	int	error;
	static uint16_t *intr_vals[] = {ed_intr_val, ed_790_intr_val};

	error = ed_alloc_port(dev, port_rid, ED_WD_IO_PORTS);
	if (error)
		return (error);

	sc->asic_offset = ED_WD_ASIC_OFFSET;
	sc->nic_offset  = ED_WD_NIC_OFFSET;

	return ed_probe_WD80x3_generic(dev, flags, intr_vals);
}
