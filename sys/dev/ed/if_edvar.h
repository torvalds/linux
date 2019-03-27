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
 *
 * $FreeBSD$
 */

#ifndef SYS_DEV_ED_IF_EDVAR_H
#define	SYS_DEV_ED_IF_EDVAR_H

#include <dev/mii/mii_bitbang.h>

/*
 * ed_softc: per line info and status
 */
struct ed_softc {
	struct ifnet *ifp;
	struct ifmedia ifmedia; /* Media info */
	device_t dev;
	struct mtx sc_mtx;

	char   *type_str;	/* pointer to type string */
	u_char  vendor;		/* interface vendor */
	u_char  type;		/* interface type code */
	u_char	chip_type;	/* the type of chip (one of ED_CHIP_TYPE_*) */
	u_char  isa16bit;	/* width of access to card 0=8 or 1=16 */
	u_char  mem_shared;	/* NIC memory is shared with host */
	u_char  xmit_busy;	/* transmitter is busy */
	u_char  enaddr[6];

	int	port_used;	/* nonzero if ports used */
	struct resource* port_res; /* resource for port range */
	struct resource* port_res2; /* resource for port range */
	bus_space_tag_t port_bst;
	bus_space_handle_t port_bsh;
	int	mem_used;	/* nonzero if memory used */
	struct resource* mem_res; /* resource for memory range */
	bus_space_tag_t mem_bst;
	bus_space_handle_t mem_bsh;
	struct resource* irq_res; /* resource for irq */
	void*	irq_handle;	/* handle for irq handler */
	int	(*sc_media_ioctl)(struct ed_softc *sc, struct ifreq *ifr,
	    u_long command);
	void	(*sc_mediachg)(struct ed_softc *);
	device_t miibus;	/* MII bus for cards with MII. */
	mii_bitbang_ops_t mii_bitbang_ops;
	struct callout	      tick_ch;
        void	(*sc_tick)(struct ed_softc *);
	void (*readmem)(struct ed_softc *sc, bus_size_t src, uint8_t *dst,
	    uint16_t amount);
	u_short	(*sc_write_mbufs)(struct ed_softc *, struct mbuf *, bus_size_t);

	int	tx_timer;
	int	nic_offset;	/* NIC (DS8390) I/O bus address offset */
	int	asic_offset;	/* ASIC I/O bus address offset */

/*
 * The following 'proto' variable is part of a work-around for 8013EBT asics
 *	being write-only. It's sort of a prototype/shadow of the real thing.
 */
	u_char  wd_laar_proto;
	u_char	cr_proto;

/*
 * HP PC LAN PLUS card support.
 */

	u_short	hpp_options;	/* flags controlling behaviour of the HP card */
	u_short hpp_id;		/* software revision and other fields */
	caddr_t hpp_mem_start;	/* Memory-mapped IO register address */

	bus_size_t mem_start; /* NIC memory start address */
	bus_size_t mem_end; /* NIC memory end address */
	uint32_t mem_size;	/* total NIC memory size */
	bus_size_t mem_ring; /* start of RX ring-buffer (in NIC mem) */

	u_char  txb_cnt;	/* number of transmit buffers */
	u_char  txb_inuse;	/* number of TX buffers currently in-use */

	u_char  txb_new;	/* pointer to where new buffer will be added */
	u_char  txb_next_tx;	/* pointer to next buffer ready to xmit */
	u_short txb_len[8];	/* buffered xmit buffer lengths */
	u_char  tx_page_start;	/* first page of TX buffer area */
	u_char  rec_page_start;	/* first page of RX ring-buffer */
	u_char  rec_page_stop;	/* last page of RX ring-buffer */
	u_char  next_packet;	/* pointer to next unread RX packet */
	u_int	tx_mem;		/* Total amount of RAM for tx */
	u_int	rx_mem;		/* Total amount of RAM for rx */
	struct	ifmib_iso_8802_3 mibdata; /* stuff for network mgmt */
};

#define	ed_nic_barrier(sc, port, length, flags) \
	bus_space_barrier(sc->port_bst, sc->port_bsh, \
	    (sc)->nic_offset + (port), (length), (flags))

#define	ed_nic_inb(sc, port) \
	bus_space_read_1(sc->port_bst, sc->port_bsh, (sc)->nic_offset + (port))

#define	ed_nic_outb(sc, port, value) \
	bus_space_write_1(sc->port_bst, sc->port_bsh, \
	    (sc)->nic_offset + (port), (value))

#define	ed_nic_inw(sc, port) \
	bus_space_read_2(sc->port_bst, sc->port_bsh, (sc)->nic_offset + (port))

#define	ed_nic_outw(sc, port, value) \
	bus_space_write_2(sc->port_bst, sc->port_bsh, \
	    (sc)->nic_offset + (port), (value))

#define	ed_nic_insb(sc, port, addr, count) \
	bus_space_read_multi_1(sc->port_bst,  sc->port_bsh, \
		(sc)->nic_offset + (port), (addr), (count))

#define	ed_nic_outsb(sc, port, addr, count) \
	bus_space_write_multi_1(sc->port_bst, sc->port_bsh, \
		(sc)->nic_offset + (port), (addr), (count))

#define	ed_nic_insw(sc, port, addr, count) \
	bus_space_read_multi_2(sc->port_bst, sc->port_bsh, \
		(sc)->nic_offset + (port), (uint16_t *)(addr), (count))

#define	ed_nic_outsw(sc, port, addr, count) \
	bus_space_write_multi_2(sc->port_bst, sc->port_bsh, \
		(sc)->nic_offset + (port), (uint16_t *)(addr), (count))

#define	ed_nic_insl(sc, port, addr, count) \
	bus_space_read_multi_4(sc->port_bst, sc->port_bsh, \
		(sc)->nic_offset + (port), (uint32_t *)(addr), (count))

#define	ed_nic_outsl(sc, port, addr, count) \
	bus_space_write_multi_4(sc->port_bst, sc->port_bsh, \
		(sc)->nic_offset + (port), (uint32_t *)(addr), (count))

#define	ed_asic_barrier(sc, port, length, flags) \
	bus_space_barrier(sc->port_bst, sc->port_bsh, \
	    (sc)->asic_offset + (port), (length), (flags))

#define	ed_asic_inb(sc, port) \
	bus_space_read_1(sc->port_bst, sc->port_bsh, \
	    (sc)->asic_offset + (port))

#define	ed_asic_outb(sc, port, value) \
	bus_space_write_1(sc->port_bst, sc->port_bsh, \
	    (sc)->asic_offset + (port), (value))

#define	ed_asic_inw(sc, port) \
	bus_space_read_2(sc->port_bst, sc->port_bsh, \
	    (sc)->asic_offset + (port))

#define	ed_asic_outw(sc, port, value) \
	bus_space_write_2(sc->port_bst, sc->port_bsh, \
	    (sc)->asic_offset + (port), (value))

#define	ed_asic_insb(sc, port, addr, count) \
	bus_space_read_multi_1(sc->port_bst, sc->port_bsh, \
		(sc)->asic_offset + (port), (addr), (count))

#define	ed_asic_outsb(sc, port, addr, count) \
	bus_space_write_multi_1(sc->port_bst, sc->port_bsh, \
		(sc)->asic_offset + (port), (addr), (count))

#define	ed_asic_insw(sc, port, addr, count) \
	bus_space_read_multi_2(sc->port_bst, sc->port_bsh, \
		(sc)->asic_offset + (port), (uint16_t *)(addr), (count))

#define	ed_asic_outsw(sc, port, addr, count) \
	bus_space_write_multi_2(sc->port_bst, sc->port_bsh, \
		(sc)->asic_offset + (port), (uint16_t *)(addr), (count))

#define	ed_asic_insl(sc, port, addr, count) \
	bus_space_read_multi_4(sc->port_bst, sc->port_bsh, \
		(sc)->asic_offset + (port), (uint32_t *)(addr), (count))

#define	ed_asic_outsl(sc, port, addr, count) \
	bus_space_write_multi_4(sc->port_bst, sc->port_bsh, \
		(sc)->asic_offset + (port), (uint32_t *)(addr), (count))

void	ed_release_resources(device_t);
int	ed_alloc_port(device_t, int, int);
int	ed_alloc_memory(device_t, int, int);
int	ed_alloc_irq(device_t, int, int);

int	ed_probe_generic8390(struct ed_softc *);
int	ed_probe_WD80x3(device_t, int, int);
int	ed_probe_WD80x3_generic(device_t, int, uint16_t *[]);
int	ed_probe_RTL80x9(device_t, int, int);
#ifdef ED_3C503
int	ed_probe_3Com(device_t, int, int);
#endif
#ifdef ED_SIC
int	ed_probe_SIC(device_t, int, int);
#endif
int	ed_probe_Novell_generic(device_t, int);
int	ed_probe_Novell(device_t, int, int);
void	ed_Novell_read_mac(struct ed_softc *);
#ifdef ED_HPP
int	ed_probe_HP_pclanp(device_t, int, int);
#endif

int	ed_attach(device_t);
int	ed_detach(device_t);
int	ed_clear_memory(device_t);
int	ed_isa_mem_ok(device_t, u_long, u_int); /* XXX isa specific */
void	ed_stop(struct ed_softc *);
void	ed_shmem_readmem16(struct ed_softc *, bus_size_t, uint8_t *, uint16_t);
void	ed_shmem_readmem8(struct ed_softc *, bus_size_t, uint8_t *, uint16_t);
u_short	ed_shmem_write_mbufs(struct ed_softc *, struct mbuf *, bus_size_t);
void	ed_pio_readmem(struct ed_softc *, bus_size_t, uint8_t *, uint16_t);
void	ed_pio_writemem(struct ed_softc *, uint8_t *, uint16_t, uint16_t);
u_short	ed_pio_write_mbufs(struct ed_softc *, struct mbuf *, bus_size_t);

void	ed_disable_16bit_access(struct ed_softc *);
void	ed_enable_16bit_access(struct ed_softc *);

void	ed_gen_ifmedia_init(struct ed_softc *);

driver_intr_t	edintr;

extern devclass_t ed_devclass;


/*
 * Vendor types
 */
#define ED_VENDOR_WD_SMC	0x00		/* Western Digital/SMC */
#define ED_VENDOR_3COM		0x01		/* 3Com */
#define ED_VENDOR_NOVELL	0x02		/* Novell */
#define ED_VENDOR_HP		0x03		/* Hewlett Packard */
#define ED_VENDOR_SIC		0x04		/* Allied-Telesis SIC */

/*
 * Configure time flags
 */
/*
 * this sets the default for enabling/disabling the transceiver
 */
#define ED_FLAGS_DISABLE_TRANCEIVER	0x0001

/*
 * This forces the board to be used in 8/16bit mode even if it
 *	autoconfigs differently
 */
#define ED_FLAGS_FORCE_8BIT_MODE	0x0002
#define ED_FLAGS_FORCE_16BIT_MODE	0x0004

/*
 * This disables the use of double transmit buffers.
 */
#define ED_FLAGS_NO_MULTI_BUFFERING	0x0008

/*
 * This forces all operations with the NIC memory to use Programmed
 *	I/O (i.e. not via shared memory)
 */
#define ED_FLAGS_FORCE_PIO		0x0010

/*
 * This forces a PC Card, and disables ISA memory range checks
 */
#define ED_FLAGS_PCCARD			0x0020

/*
 * These are flags describing the chip type.
 */
#define ED_FLAGS_TOSH_ETHER		0x10000
#define ED_FLAGS_GWETHER		0x20000

#define ED_FLAGS_GETTYPE(flg)		((flg) & 0xff0000)

#define ED_MUTEX(_sc)		(&(_sc)->sc_mtx)
#define ED_LOCK(_sc)		mtx_lock(ED_MUTEX(_sc))
#define	ED_UNLOCK(_sc)		mtx_unlock(ED_MUTEX(_sc))
#define ED_LOCK_INIT(_sc) \
	mtx_init(ED_MUTEX(_sc), device_get_nameunit(_sc->dev), \
	    MTX_NETWORK_LOCK, MTX_DEF)
#define ED_LOCK_DESTROY(_sc)	mtx_destroy(ED_MUTEX(_sc));
#define ED_ASSERT_LOCKED(_sc)	mtx_assert(ED_MUTEX(_sc), MA_OWNED);
#define ED_ASSERT_UNLOCKED(_sc)	mtx_assert(ED_MUTEX(_sc), MA_NOTOWNED);

#endif /* SYS_DEV_ED_IF_EDVAR_H */
