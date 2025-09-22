/**************************************************************************

Copyright (c) 2001-2005, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

 1. Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.

 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

 3. Neither the name of the Intel Corporation nor the names of its
    contributors may be used to endorse or promote products derived from
    this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

***************************************************************************/

/* $OpenBSD: if_ixgb.h,v 1.19 2015/11/24 17:11:39 mpi Exp $ */

#ifndef _IXGB_H_DEFINED_
#define _IXGB_H_DEFINED_

#include "bpfilter.h"
#include "vlan.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/socket.h>
#include <sys/timeout.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

typedef int	boolean_t;
#define TRUE	1
#define FALSE	0

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/pci/ixgb_hw.h>
#include <dev/pci/ixgb_ee.h>
#include <dev/pci/ixgb_ids.h>

/* Tunables */

/*
 * TxDescriptors Valid Range: 64-4096 Default Value: 2048 This value is the
 * number of transmit descriptors allocated by the driver. Increasing this
 * value allows the driver to queue more transmits. Each descriptor is 16
 * bytes.
 */
#define IXGB_MAX_TXD			2048

/*
 * RxDescriptors Valid Range: 64-4096 Default Value: 1024 This value is the
 * number of receive descriptors allocated by the driver. Increasing this
 * value allows the driver to buffer more incoming packets. Each descriptor
 * is 16 bytes.  A receive buffer is also allocated for each descriptor. The
 * maximum MTU size is 16110.
 */
#define IXGB_MAX_RXD			1024

/*
 * TxIntDelay Valid Range: 0-65535 (0=off) Default Value: 32 This value
 * delays the generation of transmit interrupts in units of 1.024
 * microseconds. Transmit interrupt reduction can improve CPU efficiency if
 * properly tuned for specific network traffic. If the system is reporting
 * dropped transmits, this value may be set too high causing the driver to
 * run out of available transmit descriptors.
 */
#define TIDV				32

/*
 * RxIntDelay Valid Range: 0-65535 (0=off) Default Value: 72 This value
 * delays the generation of receive interrupts in units of 1.024
 * microseconds.  Receive interrupt reduction can improve CPU efficiency if
 * properly tuned for specific network traffic. Increasing this value adds
 * extra latency to frame reception and can end up decreasing the throughput
 * of TCP traffic. If the system is reporting dropped receives, this value
 * may be set too high, causing the driver to run out of available receive
 * descriptors.
 */
#define RDTR				72

/*
 * This parameter controls the duration of transmit watchdog timer.
 */
#define IXGB_TX_TIMEOUT			5	/* set to 5 seconds */

/*
 * This parameter controls when the driver calls the routine to reclaim
 * transmit descriptors.
 */
#define IXGB_TX_CLEANUP_THRESHOLD	(sc->num_tx_desc / 8)

/* 
 * Flow Control Types. 
 * 1. ixgb_fc_none - Flow Control Disabled 
 * 2. ixgb_fc_rx_pause - Flow Control Receive Only
 * 3. ixgb_fc_tx_pause - Flow Control Transmit Only
 * 4. ixgb_fc_full - Flow Control Enabled
 */
#define FLOW_CONTROL_NONE	ixgb_fc_none 
#define FLOW_CONTROL_RX_PAUSE	ixgb_fc_rx_pause
#define FLOW_CONTROL_TX_PAUSE	ixgb_fc_tx_pause
#define FLOW_CONTROL_FULL	ixgb_fc_full

/*
 * Set the flow control type. Assign one of the above flow control types to be enabled.
 * Default Value: FLOW_CONTROL_FULL   
 */    
#define FLOW_CONTROL		FLOW_CONTROL_FULL

/*
 * Receive Flow control low threshold (when we send a resume frame) (FCRTL)
 * Valid Range: 64 - 262,136 (0x40 - 0x3FFF8, 8 byte granularity) must be
 * less than high threshold by at least 8 bytes Default Value:  163,840
 * (0x28000)
 */
#define FCRTL			0x28000

/*
 * Receive Flow control high threshold (when we send a pause frame) (FCRTH)
 * Valid Range: 1,536 - 262,136 (0x600 - 0x3FFF8, 8 byte granularity) Default
 * Value: 196,608 (0x30000)
 */
#define FCRTH			0x30000

/*
 * Flow control request timeout (how long to pause the link partner's tx)
 * (PAP 15:0) Valid Range: 1 - 65535 Default Value:  256 (0x100)
 */
#define FCPAUSE			0x100

/* Tunables -- End */

#define IXGB_MMBA		0x0010	/* Mem base address */
#define IXGB_ROUNDUP(size, unit) (((size) + (unit) - 1) & ~((unit) - 1))

#define MAX_NUM_MULTICAST_ADDRESSES	128

/* Defines for printing debug information */
#define DEBUG_INIT	0
#define DEBUG_IOCTL	0
#define DEBUG_HW	0

#define INIT_DEBUGOUT(S)		if (DEBUG_INIT)  printf(S "\n")
#define INIT_DEBUGOUT1(S, A)		if (DEBUG_INIT)  printf(S "\n", A)
#define INIT_DEBUGOUT2(S, A, B)		if (DEBUG_INIT)  printf(S "\n", A, B)
#define IOCTL_DEBUGOUT(S)		if (DEBUG_IOCTL) printf(S "\n")
#define IOCTL_DEBUGOUT1(S, A)		if (DEBUG_IOCTL) printf(S "\n", A)
#define IOCTL_DEBUGOUT2(S, A, B)	if (DEBUG_IOCTL) printf(S "\n", A, B)
#define HW_DEBUGOUT(S)			if (DEBUG_HW) printf(S "\n")
#define HW_DEBUGOUT1(S, A)		if (DEBUG_HW) printf(S "\n", A)
#define HW_DEBUGOUT2(S, A, B)		if (DEBUG_HW) printf(S "\n", A, B)

/* Supported RX Buffer Sizes */
#define IXGB_RXBUFFER_2048	2048
#define IXGB_RXBUFFER_4096	4096
#define IXGB_RXBUFFER_8192	8192
#define IXGB_RXBUFFER_16384	16384

#define IXGB_MAX_SCATTER	100

struct ixgb_buffer {
	struct mbuf    *m_head;
	bus_dmamap_t    map;	/* bus_dma map for packet */
};

/*
 * Bus dma allocation structure used by
 * ixgb_dma_malloc and ixgb_dma_free.
 */
struct ixgb_dma_alloc {
	bus_addr_t		dma_paddr;
	caddr_t			dma_vaddr;
	bus_dma_tag_t		dma_tag;
	bus_dmamap_t		dma_map;
	bus_dma_segment_t	dma_seg;
	bus_size_t		dma_size;
	int			dma_nseg;
};

typedef enum _XSUM_CONTEXT_T {
	OFFLOAD_NONE,
	OFFLOAD_TCP_IP,
	OFFLOAD_UDP_IP
} XSUM_CONTEXT_T;

/* Our adapter structure */
struct ixgb_softc {
	struct device	sc_dv;
	struct arpcom	interface_data;
	struct ixgb_hw	hw;

	/* OpenBSD operating-system-specific structures */
	struct ixgb_osdep osdep;
	struct ifmedia	media;
	int		io_rid;

	void		*sc_intrhand;
	struct timeout	ixgb_intr_enable;
	struct timeout	timer_handle;
	int		if_flags;

	/* Info about the board itself */
	u_int32_t	part_num;
	u_int8_t	link_active;
	u_int16_t	link_speed;
	u_int16_t	link_duplex;
	u_int32_t	tx_int_delay;
	u_int32_t	tx_abs_int_delay;
	u_int32_t	rx_int_delay;
	u_int32_t	rx_abs_int_delay;

	int		raidc;

	XSUM_CONTEXT_T	active_checksum_context;

	/*
	 * Transmit definitions
	 * 
	 * We have an array of num_tx_desc descriptors (handled by the
	 * controller) paired with an array of tx_buffers (at
	 * tx_buffer_area). The index of the next available descriptor is
	 * next_avail_tx_desc. The number of remaining tx_desc is
	 * num_tx_desc_avail.
	 */
	struct ixgb_dma_alloc	txdma;		/* bus_dma glue for tx desc */
	struct ixgb_tx_desc	*tx_desc_base;
	u_int32_t		next_avail_tx_desc;
	u_int32_t		oldest_used_tx_desc;
	volatile u_int16_t	num_tx_desc_avail;
	u_int16_t		num_tx_desc;
	u_int32_t		txd_cmd;
	struct ixgb_buffer	*tx_buffer_area;
	bus_dma_tag_t		txtag;		/* dma tag for tx */

	/*
	 * Receive definitions
	 * 
	 * we have an array of num_rx_desc rx_desc (handled by the controller),
	 * and paired with an array of rx_buffers (at rx_buffer_area). The
	 * next pair to check on receive is at offset next_rx_desc_to_check
	 */
	struct ixgb_dma_alloc	rxdma;		/* bus_dma glue for rx desc */
	struct ixgb_rx_desc	*rx_desc_base;
	u_int32_t		next_rx_desc_to_check;
	u_int16_t		num_rx_desc;
	u_int32_t		rx_buffer_len;
	struct ixgb_buffer	*rx_buffer_area;
	bus_dma_tag_t		rxtag;		/* dma tag for Rx */
	u_int32_t		next_rx_desc_to_use;

	/*
	 * First/last mbuf pointers, for
	 * collecting multisegment RX packets.
	 */
	struct mbuf		*fmp;
	struct mbuf		*lmp;

	/* Misc stats maintained by the driver */
	unsigned long		dropped_pkts;
	unsigned long		mbuf_alloc_failed;
	unsigned long		mbuf_cluster_failed;
	unsigned long		no_tx_desc_avail1;
	unsigned long		no_tx_desc_avail2;
	unsigned long		no_tx_map_avail;
	unsigned long		no_tx_dma_setup;
	unsigned long		watchdog_events;

	struct ixgb_hw_stats	stats;
};

#endif /* _IXGB_H_DEFINED_ */
