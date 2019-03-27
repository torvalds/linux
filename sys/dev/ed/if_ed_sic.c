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

#ifdef ED_SIC
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
 * Probe and vendor-specific initialization routine for SIC boards
 */
int
ed_probe_SIC(device_t dev, int port_rid, int flags)
{
	struct ed_softc *sc = device_get_softc(dev);
	int	error;
	int	i;
	u_int	memsize;
	u_long	pmem;
	u_char	sum;

	error = ed_alloc_port(dev, 0, ED_SIC_IO_PORTS);
	if (error)
		return (error);

	sc->asic_offset = ED_SIC_ASIC_OFFSET;
	sc->nic_offset  = ED_SIC_NIC_OFFSET;

	memsize = 16384;
	/* XXX Needs to allow different msize */
	error = ed_alloc_memory(dev, 0, memsize);
	if (error)
		return (error);

	sc->mem_start = 0;
	sc->mem_size  = memsize;

	pmem = rman_get_start(sc->mem_res);
	error = ed_isa_mem_ok(dev, pmem, memsize);
	if (error)
		return (error);

	/* Reset card to force it into a known state. */
	ed_asic_outb(sc, 0, 0x00);
	DELAY(100);

	/*
	 * Here we check the card ROM, if the checksum passes, and the
	 * type code and ethernet address check out, then we know we have
	 * an SIC card.
	 */
	ed_asic_outb(sc, 0, 0x81);
	DELAY(100);

	sum = bus_space_read_1(sc->mem_bst, sc->mem_bsh, 6);
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		sum ^= (sc->enaddr[i] =
		    bus_space_read_1(sc->mem_bst, sc->mem_bsh, i));
#ifdef ED_DEBUG
	device_printf(dev, "ed_probe_sic: got address %6D\n",
	    sc->enaddr, ":");
#endif
	if (sum != 0)
		return (ENXIO);
	if ((sc->enaddr[0] | sc->enaddr[1] | sc->enaddr[2]) == 0)
		return (ENXIO);

	sc->vendor   = ED_VENDOR_SIC;
	sc->type_str = "SIC";
	sc->isa16bit = 0;
	sc->cr_proto = 0;

	/*
	 * SIC RAM page 0x0000-0x3fff(or 0x7fff)
	 */
	ed_asic_outb(sc, 0, 0x80);
	DELAY(100);

	error = ed_clear_memory(dev);
	if (error)
		return (error);

	sc->mem_shared = 1;
	sc->mem_end = sc->mem_start + sc->mem_size;

	/*
	 * allocate one xmit buffer if < 16k, two buffers otherwise
	 */
	if ((sc->mem_size < 16384) || (flags & ED_FLAGS_NO_MULTI_BUFFERING))
		sc->txb_cnt = 1;
	else
		sc->txb_cnt = 2;
	sc->tx_page_start = 0;

	sc->rec_page_start = sc->tx_page_start + ED_TXBUF_SIZE * sc->txb_cnt;
	sc->rec_page_stop = sc->tx_page_start + sc->mem_size / ED_PAGE_SIZE;

	sc->mem_ring = sc->mem_start + sc->txb_cnt * ED_PAGE_SIZE * ED_TXBUF_SIZE;

	sc->sc_write_mbufs = ed_shmem_write_mbufs;
	return (0);
}

#endif /* ED_SIC */
