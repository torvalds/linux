/*	$OpenBSD: if_igc.h,v 1.4 2024/05/21 11:19:39 bluhm Exp $	*/
/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2016 Nicole Graziano <nicole@nextbsd.org>
 * All rights reserved.
 * Copyright (c) 2021 Rubicon Communications, LLC (Netgate)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
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

#ifndef _IGC_H_
#define _IGC_H_

#include <dev/pci/igc_api.h>
#include <dev/pci/igc_i225.h>

/*
 * IGC_MAX_TXD: Maximum number of Transmit Descriptors
 * Valid Range: 128-4096
 * Default Value: 1024
 *   This value is the number of transmit descriptors allocated by the driver.
 *   Increasing this value allows the driver to queue more transmits. Each
 *   descriptor is 16 bytes.
 *   Since TDLEN should be multiple of 128bytes, the number of transmit
 *   descriptors should meet the following condition.
 *      (num_tx_desc * sizeof(struct igc_tx_desc)) % 128 == 0
 */
#define IGC_MIN_TXD		128
#define IGC_MAX_TXD		4096
#define IGC_DEFAULT_TXD		1024
#define IGC_DEFAULT_MULTI_TXD	4096
#define IGC_MAX_TXD		4096

/*
 * IGC_MAX_RXD - Maximum number of receive Descriptors
 * Valid Range: 128-4096
 * Default Value: 1024
 *   This value is the number of receive descriptors allocated by the driver.
 *   Increasing this value allows the driver to buffer more incoming packets.
 *   Each descriptor is 16 bytes.  A receive buffer is also allocated for each
 *   descriptor. The maximum MTU size is 16110.
 *   Since TDLEN should be multiple of 128bytes, the number of transmit
 *   descriptors should meet the following condition.
 *      (num_tx_desc * sizeof(struct igc_tx_desc)) % 128 == 0
 */
#define IGC_MIN_RXD		128
#define IGC_MAX_RXD		4096
#define IGC_DEFAULT_RXD		1024
#define IGC_DEFAULT_MULTI_RXD	4096
#define IGC_MAX_RXD		4096

/*
 * IGC_TIDV_VAL - Transmit Interrupt Delay Value
 * Valid Range: 0-65535 (0=off)
 * Default Value: 64
 *   This value delays the generation of transmit interrupts in units of
 *   1.024 microseconds. Transmit interrupt reduction can improve CPU
 *   efficiency if properly tuned for specific network traffic. If the
 *   system is reporting dropped transmits, this value may be set too high
 *   causing the driver to run out of available transmit descriptors.
 */
#define IGC_TIDV_VAL		64

/*
 * IGC_TADV_VAL - Transmit Absolute Interrupt Delay Value
 * Valid Range: 0-65535 (0=off)
 * Default Value: 64
 *   This value, in units of 1.024 microseconds, limits the delay in which a
 *   transmit interrupt is generated. Useful only if IGC_TIDV is non-zero,
 *   this value ensures that an interrupt is generated after the initial
 *   packet is sent on the wire within the set amount of time.  Proper tuning,
 *   along with IGC_TIDV_VAL, may improve traffic throughput in specific
 *   network conditions.
 */
#define IGC_TADV_VAL		64

/*
 * IGC_RDTR_VAL - Receive Interrupt Delay Timer (Packet Timer)
 * Valid Range: 0-65535 (0=off)
 * Default Value: 0
 *   This value delays the generation of receive interrupts in units of 1.024
 *   microseconds.  Receive interrupt reduction can improve CPU efficiency if
 *   properly tuned for specific network traffic. Increasing this value adds
 *   extra latency to frame reception and can end up decreasing the throughput
 *   of TCP traffic. If the system is reporting dropped receives, this value
 *   may be set too high, causing the driver to run out of available receive
 *   descriptors.
 *
 *   CAUTION: When setting IGC_RDTR to a value other than 0, adapters
 *            may hang (stop transmitting) under certain network conditions.
 *            If this occurs a WATCHDOG message is logged in the system
 *            event log. In addition, the controller is automatically reset,
 *            restoring the network connection. To eliminate the potential
 *            for the hang ensure that IGC_RDTR is set to 0.
 */
#define IGC_RDTR_VAL		0

/*
 * Receive Interrupt Absolute Delay Timer
 * Valid Range: 0-65535 (0=off)
 * Default Value: 64
 *   This value, in units of 1.024 microseconds, limits the delay in which a
 *   receive interrupt is generated. Useful only if IGC_RDTR is non-zero,
 *   this value ensures that an interrupt is generated after the initial
 *   packet is received within the set amount of time.  Proper tuning,
 *   along with IGC_RDTR, may improve traffic throughput in specific network
 *   conditions.
 */
#define IGC_RADV_VAL		64

/*
 * This parameter controls whether or not autonegotiation is enabled.
 *              0 - Disable autonegotiation
 *              1 - Enable  autonegotiation
 */
#define DO_AUTO_NEG		true

#define AUTONEG_ADV_DEFAULT						\
	(ADVERTISE_10_HALF | ADVERTISE_10_FULL | ADVERTISE_100_HALF |	\
	ADVERTISE_100_FULL | ADVERTISE_1000_FULL | ADVERTISE_2500_FULL)

#define AUTO_ALL_MODES		0

/*
 * Miscellaneous constants
 */
#define MAX_NUM_MULTICAST_ADDRESSES	128
#define IGC_FC_PAUSE_TIME		0x0680

#define IGC_TXPBSIZE		20408
#define IGC_PKTTYPE_MASK	0x0000FFF0
#define IGC_DMCTLX_DCFLUSH_DIS	0x80000000	/* Disable DMA Coalesce Flush */

#define IGC_RX_PTHRESH		8
#define IGC_RX_HTHRESH		8
#define IGC_RX_WTHRESH		4

#define IGC_TX_PTHRESH		8
#define IGC_TX_HTHRESH		1

/*
 * TDBA/RDBA should be aligned on 16 byte boundary. But TDLEN/RDLEN should be
 * multiple of 128 bytes. So we align TDBA/RDBA on 128 byte boundary. This will
 * also optimize cache line size effect. H/W supports up to cache line size 128.
 */
#define IGC_DBA_ALIGN		128

/*
 * This parameter controls the duration of transmit watchdog timer.
 */
#define IGC_TX_TIMEOUT		5	/* set to 5 seconds */

#define IGC_PCIREG		PCI_MAPREG_START

#define IGC_MAX_VECTORS		8

/* Enable/disable debugging statements in shared code */
#define DBG	0

#define DEBUGOUT(...)							\
	do { if (DBG) printf(__VA_ARGS__); } while (0)
#define DEBUGOUT1(...)		DEBUGOUT(__VA_ARGS__)
#define DEBUGOUT2(...)		DEBUGOUT(__VA_ARGS__)
#define DEBUGOUT3(...)		DEBUGOUT(__VA_ARGS__)
#define DEBUGOUT7(...)		DEBUGOUT(__VA_ARGS__)
#define DEBUGFUNC(F)		DEBUGOUT(F "\n")

/* Compatibility glue. */
#define roundup2(size, unit)	(((size) + (unit) - 1) & ~((unit) - 1))
#define msec_delay(x)		DELAY(1000 * (x))

#define IGC_MAX_SCATTER		40
#define IGC_TSO_SIZE		65535

#define MAX_INTS_PER_SEC	8000
#define DEFAULT_ITR		(1000000000/(MAX_INTS_PER_SEC * 256))

/* Forward declaration. */
struct igc_hw;
struct kstat;

struct igc_osdep {
	bus_dma_tag_t		os_dmat;
	bus_space_tag_t		os_memt;
	bus_space_handle_t	os_memh;

	bus_size_t		os_memsize;
	bus_addr_t		os_membase;

	void			*os_sc;
	struct pci_attach_args	os_pa;
};


struct igc_tx_buf {
	uint32_t	eop_index;
	struct mbuf	*m_head;
	bus_dmamap_t	map;
};

struct igc_rx_buf {
	struct mbuf	*buf;
	struct mbuf	*fmp;	/* First mbuf pointers. */
	bus_dmamap_t	map;
};

/*
 * Bus dma allocation structure used by igc_dma_malloc and igc_dma_free.
 */
struct igc_dma_alloc {
	caddr_t			dma_vaddr;
	bus_dma_tag_t		dma_tag;
	bus_dmamap_t		dma_map;
	bus_dma_segment_t	dma_seg;
	bus_size_t		dma_size;
	int			dma_nseg;
};

/*
 * Driver queue struct: this is the interrupt container
 * for the associated tx and rx ring.
 */
struct igc_queue {
	struct igc_softc	*sc;
	uint32_t		msix;
	uint32_t		eims;
	uint32_t		eitr_setting;
	char			name[16];
	pci_intr_handle_t	ih;
	void			*tag;
	struct igc_txring	*txr;
	struct igc_rxring	*rxr;
};

/*
 * The transmit ring, one per tx queue.
 */
struct igc_txring {
	struct igc_softc	*sc;
	struct ifqueue		*ifq;
	uint32_t		me;
	uint32_t		watchdog_timer;
	union igc_adv_tx_desc	*tx_base;
	struct igc_tx_buf	*tx_buffers;
	struct igc_dma_alloc	txdma;
	uint32_t		next_avail_desc;
	uint32_t		next_to_clean;
	bus_dma_tag_t		txtag;
};

/*
 * The Receive ring, one per rx queue.
 */
struct igc_rxring {
	struct igc_softc	*sc;
	struct ifiqueue		*ifiq;
	uint32_t		me;
	union igc_adv_rx_desc	*rx_base;
	struct igc_rx_buf	*rx_buffers;
	struct igc_dma_alloc	rxdma;
	uint32_t		last_desc_filled;
	uint32_t		next_to_check;
	struct timeout		rx_refill;
	struct if_rxring	rx_ring;
};

/* Our adapter structure. */
struct igc_softc {
	struct device		sc_dev;
	struct arpcom		sc_ac;
	struct ifmedia		media;
	struct intrmap		*sc_intrmap;

	struct igc_osdep	osdep;
	struct igc_hw		hw;

	uint16_t		fc;
	uint16_t		link_active;
	uint16_t		link_speed;
	uint16_t		link_duplex;
	uint32_t		dmac;

	void			*tag;

	int			num_tx_desc;
	int			num_rx_desc;

	uint32_t		max_frame_size;
	uint32_t		rx_mbuf_sz;
	uint32_t		linkvec;
	uint32_t		msix_linkmask;
	uint32_t		msix_queuesmask;

	unsigned int		sc_nqueues;
	struct igc_queue	*queues;

	struct igc_txring	*tx_rings;
	struct igc_rxring	*rx_rings;

	/* Multicast array memory */
	uint8_t			*mta;

	/* Counters */
	struct mutex		 ks_mtx;
	struct timeout		 ks_tmo;
	struct kstat		*ks;
};

#define DEVNAME(_sc)    ((_sc)->sc_dev.dv_xname)

/* Register READ/WRITE macros */
#define IGC_WRITE_FLUSH(a)	IGC_READ_REG(a, IGC_STATUS)
#define IGC_READ_REG(a, reg)						\
        bus_space_read_4(((struct igc_osdep *)(a)->back)->os_memt,	\
        ((struct igc_osdep *)(a)->back)->os_memh, reg)
#define IGC_WRITE_REG(a, reg, value)					\
        bus_space_write_4(((struct igc_osdep *)(a)->back)->os_memt,	\
        ((struct igc_osdep *)(a)->back)->os_memh, reg, value)
#define IGC_READ_REG_ARRAY(a, reg, off)					\
        bus_space_read_4(((struct igc_osdep *)(a)->back)->os_memt,	\
        ((struct igc_osdep *)(a)->back)->os_memh, (reg + ((off) << 2)))
#define IGC_WRITE_REG_ARRAY(a, reg, off, value)				\
        bus_space_write_4(((struct igc_osdep *)(a)->back)->os_memt,	\
        ((struct igc_osdep *)(a)->back)->os_memh,			\
	(reg + ((off) << 2)),value)

#endif /* _IGC_H_ */
