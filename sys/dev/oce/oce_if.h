/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 2013 Emulex
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Emulex Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Contact Information:
 * freebsd-drivers@emulex.com
 *
 * Emulex
 * 3333 Susan Street
 * Costa Mesa, CA 92626
 */

/* $FreeBSD$ */

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/eventhandler.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/mbuf.h>
#include <sys/rman.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/sockopt.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <sys/random.h>
#include <sys/firmware.h>
#include <sys/systm.h>
#include <sys/proc.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h>
#include <net/if_media.h>
#include <net/if_vlan_var.h>
#include <net/if_dl.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet6/in6_var.h>
#include <netinet6/ip6_mroute.h>

#include <netinet/udp.h>
#include <netinet/tcp.h>
#include <netinet/sctp.h>
#include <netinet/tcp_lro.h>
#include <netinet/icmp6.h>

#include <machine/bus.h>

#include "oce_hw.h"

/* OCE device driver module component revision informaiton */
#define COMPONENT_REVISION "11.0.50.0"

/* OCE devices supported by this driver */
#define PCI_VENDOR_EMULEX		0x10df	/* Emulex */
#define PCI_VENDOR_SERVERENGINES	0x19a2	/* ServerEngines (BE) */
#define PCI_PRODUCT_BE2			0x0700	/* BE2 network adapter */
#define PCI_PRODUCT_BE3			0x0710	/* BE3 network adapter */
#define PCI_PRODUCT_XE201		0xe220	/* XE201 network adapter */
#define PCI_PRODUCT_XE201_VF		0xe228	/* XE201 with VF in Lancer */
#define PCI_PRODUCT_SH			0x0720	/* Skyhawk network adapter */

#define IS_BE(sc)	(((sc->flags & OCE_FLAGS_BE3) | \
			 (sc->flags & OCE_FLAGS_BE2))? 1:0)
#define IS_BE3(sc)	(sc->flags & OCE_FLAGS_BE3)
#define IS_BE2(sc)	(sc->flags & OCE_FLAGS_BE2)
#define IS_XE201(sc)	((sc->flags & OCE_FLAGS_XE201) ? 1:0)
#define HAS_A0_CHIP(sc)	((sc->flags & OCE_FLAGS_HAS_A0_CHIP) ? 1:0)
#define IS_SH(sc)	((sc->flags & OCE_FLAGS_SH) ? 1 : 0)

#define is_be_mode_mc(sc)	((sc->function_mode & FNM_FLEX10_MODE) ||	\
				(sc->function_mode & FNM_UMC_MODE)    ||	\
				(sc->function_mode & FNM_VNIC_MODE))
#define OCE_FUNCTION_CAPS_SUPER_NIC	0x40
#define IS_PROFILE_SUPER_NIC(sc) (sc->function_caps & OCE_FUNCTION_CAPS_SUPER_NIC)


/* proportion Service Level Interface queues */
#define OCE_MAX_UNITS			2
#define OCE_MAX_PPORT			OCE_MAX_UNITS
#define OCE_MAX_VPORT			OCE_MAX_UNITS 

extern int mp_ncpus;			/* system's total active cpu cores */
#define OCE_NCPUS			mp_ncpus

/* This should be powers of 2. Like 2,4,8 & 16 */
#define OCE_MAX_RSS			8
#define OCE_LEGACY_MODE_RSS		4 /* For BE3 Legacy mode*/
#define is_rss_enabled(sc)		((sc->function_caps & FNC_RSS) && !is_be_mode_mc(sc))

#define OCE_MIN_RQ			1
#define OCE_MIN_WQ			1

#define OCE_MAX_RQ			OCE_MAX_RSS + 1 /* one default queue */ 
#define OCE_MAX_WQ			8

#define OCE_MAX_EQ			32
#define OCE_MAX_CQ			OCE_MAX_RQ + OCE_MAX_WQ + 1 /* one MCC queue */
#define OCE_MAX_CQ_EQ			8 /* Max CQ that can attached to an EQ */

#define OCE_DEFAULT_WQ_EQD		16
#define OCE_MAX_PACKET_Q		16
#define OCE_LSO_MAX_SIZE		(64 * 1024)
#define LONG_TIMEOUT			30
#define OCE_MAX_JUMBO_FRAME_SIZE	9018
#define OCE_MAX_MTU			(OCE_MAX_JUMBO_FRAME_SIZE - \
						ETHER_VLAN_ENCAP_LEN - \
						ETHER_HDR_LEN)

#define OCE_RDMA_VECTORS                2

#define OCE_MAX_TX_ELEMENTS		29
#define OCE_MAX_TX_DESC			1024
#define OCE_MAX_TX_SIZE			65535
#define OCE_MAX_TSO_SIZE		(65535 - ETHER_HDR_LEN)
#define OCE_MAX_RX_SIZE			4096
#define OCE_MAX_RQ_POSTS		255
#define OCE_HWLRO_MAX_RQ_POSTS		64
#define OCE_DEFAULT_PROMISCUOUS		0


#define RSS_ENABLE_IPV4			0x1
#define RSS_ENABLE_TCP_IPV4		0x2
#define RSS_ENABLE_IPV6			0x4
#define RSS_ENABLE_TCP_IPV6		0x8

#define INDIRECTION_TABLE_ENTRIES	128

/* flow control definitions */
#define OCE_FC_NONE			0x00000000
#define OCE_FC_TX			0x00000001
#define OCE_FC_RX			0x00000002
#define OCE_DEFAULT_FLOW_CONTROL	(OCE_FC_TX | OCE_FC_RX)


/* Interface capabilities to give device when creating interface */
#define  OCE_CAPAB_FLAGS 		(MBX_RX_IFACE_FLAGS_BROADCAST    | \
					MBX_RX_IFACE_FLAGS_UNTAGGED      | \
					MBX_RX_IFACE_FLAGS_PROMISCUOUS      | \
					MBX_RX_IFACE_FLAGS_VLAN_PROMISCUOUS |	\
					MBX_RX_IFACE_FLAGS_MCAST_PROMISCUOUS   | \
					MBX_RX_IFACE_FLAGS_RSS | \
					MBX_RX_IFACE_FLAGS_PASS_L3L4_ERR)

/* Interface capabilities to enable by default (others set dynamically) */
#define  OCE_CAPAB_ENABLE		(MBX_RX_IFACE_FLAGS_BROADCAST | \
					MBX_RX_IFACE_FLAGS_UNTAGGED   | \
					MBX_RX_IFACE_FLAGS_PASS_L3L4_ERR)

#define OCE_IF_HWASSIST			(CSUM_IP | CSUM_TCP | CSUM_UDP)
#define OCE_IF_CAPABILITIES		(IFCAP_VLAN_MTU | IFCAP_VLAN_HWTAGGING | \
					IFCAP_HWCSUM | IFCAP_VLAN_HWCSUM | \
					IFCAP_JUMBO_MTU | IFCAP_VLAN_MTU)
#define OCE_IF_HWASSIST_NONE		0
#define OCE_IF_CAPABILITIES_NONE 	0


#define ETH_ADDR_LEN			6
#define MAX_VLANFILTER_SIZE		64
#define MAX_VLANS			4096

#define upper_32_bits(n)		((uint32_t)(((n) >> 16) >> 16))
#define BSWAP_8(x)			((x) & 0xff)
#define BSWAP_16(x)			((BSWAP_8(x) << 8) | BSWAP_8((x) >> 8))
#define BSWAP_32(x)			((BSWAP_16(x) << 16) | \
					 BSWAP_16((x) >> 16))
#define BSWAP_64(x)			((BSWAP_32(x) << 32) | \
					BSWAP_32((x) >> 32))

#define for_all_wq_queues(sc, wq, i) 	\
		for (i = 0, wq = sc->wq[0]; i < sc->nwqs; i++, wq = sc->wq[i])
#define for_all_rq_queues(sc, rq, i) 	\
		for (i = 0, rq = sc->rq[0]; i < sc->nrqs; i++, rq = sc->rq[i])
#define for_all_rss_queues(sc, rq, i) 	\
		for (i = 0, rq = sc->rq[i + 1]; i < (sc->nrqs - 1); \
		     i++, rq = sc->rq[i + 1])
#define for_all_evnt_queues(sc, eq, i) 	\
		for (i = 0, eq = sc->eq[0]; i < sc->neqs; i++, eq = sc->eq[i])
#define for_all_cq_queues(sc, cq, i) 	\
		for (i = 0, cq = sc->cq[0]; i < sc->ncqs; i++, cq = sc->cq[i])


/* Flash specific */
#define IOCTL_COOKIE			"SERVERENGINES CORP"
#define MAX_FLASH_COMP			32

#define IMG_ISCSI			160
#define IMG_REDBOOT			224
#define IMG_BIOS			34
#define IMG_PXEBIOS			32
#define IMG_FCOEBIOS			33
#define IMG_ISCSI_BAK			176
#define IMG_FCOE			162
#define IMG_FCOE_BAK			178
#define IMG_NCSI			16
#define IMG_PHY				192
#define FLASHROM_OPER_FLASH		1
#define FLASHROM_OPER_SAVE		2
#define FLASHROM_OPER_REPORT		4
#define FLASHROM_OPER_FLASH_PHY		9
#define FLASHROM_OPER_SAVE_PHY		10
#define TN_8022				13

enum {
	PHY_TYPE_CX4_10GB = 0,
	PHY_TYPE_XFP_10GB,
	PHY_TYPE_SFP_1GB,
	PHY_TYPE_SFP_PLUS_10GB,
	PHY_TYPE_KR_10GB,
	PHY_TYPE_KX4_10GB,
	PHY_TYPE_BASET_10GB,
	PHY_TYPE_BASET_1GB,
	PHY_TYPE_BASEX_1GB,
	PHY_TYPE_SGMII,
	PHY_TYPE_DISABLED = 255
};

/**
 * @brief Define and hold all necessary info for a single interrupt
 */
#define OCE_MAX_MSI			32 /* Message Signaled Interrupts */
#define OCE_MAX_MSIX			2048 /* PCI Express MSI Interrrupts */

typedef struct oce_intr_info {
	void *tag;		/* cookie returned by bus_setup_intr */
	struct resource *intr_res;	/* PCI resource container */
	int irq_rr;		/* resource id for the interrupt */
	struct oce_softc *sc;	/* pointer to the parent soft c */
	struct oce_eq *eq;	/* pointer to the connected EQ */
	struct taskqueue *tq;	/* Associated task queue */
	struct task task;	/* task queue task */
	char task_name[32];	/* task name */
	int vector;		/* interrupt vector number */
} OCE_INTR_INFO, *POCE_INTR_INFO;


/* Ring related */
#define	GET_Q_NEXT(_START, _STEP, _END)	\
	(((_START) + (_STEP)) < (_END) ? ((_START) + (_STEP)) \
	: (((_START) + (_STEP)) - (_END)))

#define	DBUF_PA(obj)			((obj)->addr)
#define	DBUF_VA(obj) 			((obj)->ptr)
#define	DBUF_TAG(obj) 			((obj)->tag)
#define	DBUF_MAP(obj) 			((obj)->map)
#define	DBUF_SYNC(obj, flags) 		\
		(void) bus_dmamap_sync(DBUF_TAG(obj), DBUF_MAP(obj), (flags))

#define	RING_NUM_PENDING(ring)		ring->num_used
#define	RING_FULL(ring) 		(ring->num_used == ring->num_items)
#define	RING_EMPTY(ring) 		(ring->num_used == 0)
#define	RING_NUM_FREE(ring)		\
		(uint32_t)(ring->num_items - ring->num_used)
#define	RING_GET(ring, n)		\
		ring->cidx = GET_Q_NEXT(ring->cidx, n, ring->num_items)
#define	RING_PUT(ring, n)		\
		ring->pidx = GET_Q_NEXT(ring->pidx, n, ring->num_items)

#define	RING_GET_CONSUMER_ITEM_VA(ring, type) 	\
	(void*)((type *)DBUF_VA(&ring->dma) + ring->cidx)
#define	RING_GET_CONSUMER_ITEM_PA(ring, type)		\
	(uint64_t)(((type *)DBUF_PA(ring->dbuf)) + ring->cidx)
#define	RING_GET_PRODUCER_ITEM_VA(ring, type)		\
	(void *)(((type *)DBUF_VA(&ring->dma)) + ring->pidx)
#define	RING_GET_PRODUCER_ITEM_PA(ring, type)		\
	(uint64_t)(((type *)DBUF_PA(ring->dbuf)) + ring->pidx)

#define OCE_DMAPTR(o, c) 		((c *)(o)->ptr)

struct oce_packet_desc {
	struct mbuf *mbuf;
	bus_dmamap_t map;
	int nsegs;
	uint32_t wqe_idx;
};

typedef struct oce_dma_mem {
	bus_dma_tag_t tag;
	bus_dmamap_t map;
	void *ptr;
	bus_addr_t paddr;
} OCE_DMA_MEM, *POCE_DMA_MEM;

typedef struct oce_ring_buffer_s {
	uint16_t cidx;	/* Get ptr */
	uint16_t pidx;	/* Put Ptr */
	size_t item_size;
	size_t num_items;
	uint32_t num_used;
	OCE_DMA_MEM dma;
} oce_ring_buffer_t;

/* Stats */
#define OCE_UNICAST_PACKET	0
#define OCE_MULTICAST_PACKET	1
#define OCE_BROADCAST_PACKET	2
#define OCE_RSVD_PACKET		3

struct oce_rx_stats {
	/* Total Receive Stats*/
	uint64_t t_rx_pkts;
	uint64_t t_rx_bytes;
	uint32_t t_rx_frags;
	uint32_t t_rx_mcast_pkts;
	uint32_t t_rx_ucast_pkts;
	uint32_t t_rxcp_errs;
};
struct oce_tx_stats {
	/*Total Transmit Stats */
	uint64_t t_tx_pkts;
	uint64_t t_tx_bytes;
	uint32_t t_tx_reqs;
	uint32_t t_tx_stops;
	uint32_t t_tx_wrbs;
	uint32_t t_tx_compl;
	uint32_t t_ipv6_ext_hdr_tx_drop;
};

struct oce_be_stats {
	uint8_t  be_on_die_temperature;
	uint32_t be_tx_events;
	uint32_t eth_red_drops;
	uint32_t rx_drops_no_pbuf;
	uint32_t rx_drops_no_txpb;
	uint32_t rx_drops_no_erx_descr;
	uint32_t rx_drops_no_tpre_descr;
	uint32_t rx_drops_too_many_frags;
	uint32_t rx_drops_invalid_ring;
	uint32_t forwarded_packets;
	uint32_t rx_drops_mtu;
	uint32_t rx_crc_errors;
	uint32_t rx_alignment_symbol_errors;
	uint32_t rx_pause_frames;
	uint32_t rx_priority_pause_frames;
	uint32_t rx_control_frames;
	uint32_t rx_in_range_errors;
	uint32_t rx_out_range_errors;
	uint32_t rx_frame_too_long;
	uint32_t rx_address_match_errors;
	uint32_t rx_dropped_too_small;
	uint32_t rx_dropped_too_short;
	uint32_t rx_dropped_header_too_small;
	uint32_t rx_dropped_tcp_length;
	uint32_t rx_dropped_runt;
	uint32_t rx_ip_checksum_errs;
	uint32_t rx_tcp_checksum_errs;
	uint32_t rx_udp_checksum_errs;
	uint32_t rx_switched_unicast_packets;
	uint32_t rx_switched_multicast_packets;
	uint32_t rx_switched_broadcast_packets;
	uint32_t tx_pauseframes;
	uint32_t tx_priority_pauseframes;
	uint32_t tx_controlframes;
	uint32_t rxpp_fifo_overflow_drop;
	uint32_t rx_input_fifo_overflow_drop;
	uint32_t pmem_fifo_overflow_drop;
	uint32_t jabber_events;
};

struct oce_xe201_stats {
	uint64_t tx_pkts;
	uint64_t tx_unicast_pkts;
	uint64_t tx_multicast_pkts;
	uint64_t tx_broadcast_pkts;
	uint64_t tx_bytes;
	uint64_t tx_unicast_bytes;
	uint64_t tx_multicast_bytes;
	uint64_t tx_broadcast_bytes;
	uint64_t tx_discards;
	uint64_t tx_errors;
	uint64_t tx_pause_frames;
	uint64_t tx_pause_on_frames;
	uint64_t tx_pause_off_frames;
	uint64_t tx_internal_mac_errors;
	uint64_t tx_control_frames;
	uint64_t tx_pkts_64_bytes;
	uint64_t tx_pkts_65_to_127_bytes;
	uint64_t tx_pkts_128_to_255_bytes;
	uint64_t tx_pkts_256_to_511_bytes;
	uint64_t tx_pkts_512_to_1023_bytes;
	uint64_t tx_pkts_1024_to_1518_bytes;
	uint64_t tx_pkts_1519_to_2047_bytes;
	uint64_t tx_pkts_2048_to_4095_bytes;
	uint64_t tx_pkts_4096_to_8191_bytes;
	uint64_t tx_pkts_8192_to_9216_bytes;
	uint64_t tx_lso_pkts;
	uint64_t rx_pkts;
	uint64_t rx_unicast_pkts;
	uint64_t rx_multicast_pkts;
	uint64_t rx_broadcast_pkts;
	uint64_t rx_bytes;
	uint64_t rx_unicast_bytes;
	uint64_t rx_multicast_bytes;
	uint64_t rx_broadcast_bytes;
	uint32_t rx_unknown_protos;
	uint64_t rx_discards;
	uint64_t rx_errors;
	uint64_t rx_crc_errors;
	uint64_t rx_alignment_errors;
	uint64_t rx_symbol_errors;
	uint64_t rx_pause_frames;
	uint64_t rx_pause_on_frames;
	uint64_t rx_pause_off_frames;
	uint64_t rx_frames_too_long;
	uint64_t rx_internal_mac_errors;
	uint32_t rx_undersize_pkts;
	uint32_t rx_oversize_pkts;
	uint32_t rx_fragment_pkts;
	uint32_t rx_jabbers;
	uint64_t rx_control_frames;
	uint64_t rx_control_frames_unknown_opcode;
	uint32_t rx_in_range_errors;
	uint32_t rx_out_of_range_errors;
	uint32_t rx_address_match_errors;
	uint32_t rx_vlan_mismatch_errors;
	uint32_t rx_dropped_too_small;
	uint32_t rx_dropped_too_short;
	uint32_t rx_dropped_header_too_small;
	uint32_t rx_dropped_invalid_tcp_length;
	uint32_t rx_dropped_runt;
	uint32_t rx_ip_checksum_errors;
	uint32_t rx_tcp_checksum_errors;
	uint32_t rx_udp_checksum_errors;
	uint32_t rx_non_rss_pkts;
	uint64_t rx_ipv4_pkts;
	uint64_t rx_ipv6_pkts;
	uint64_t rx_ipv4_bytes;
	uint64_t rx_ipv6_bytes;
	uint64_t rx_nic_pkts;
	uint64_t rx_tcp_pkts;
	uint64_t rx_iscsi_pkts;
	uint64_t rx_management_pkts;
	uint64_t rx_switched_unicast_pkts;
	uint64_t rx_switched_multicast_pkts;
	uint64_t rx_switched_broadcast_pkts;
	uint64_t num_forwards;
	uint32_t rx_fifo_overflow;
	uint32_t rx_input_fifo_overflow;
	uint64_t rx_drops_too_many_frags;
	uint32_t rx_drops_invalid_queue;
	uint64_t rx_drops_mtu;
	uint64_t rx_pkts_64_bytes;
	uint64_t rx_pkts_65_to_127_bytes;
	uint64_t rx_pkts_128_to_255_bytes;
	uint64_t rx_pkts_256_to_511_bytes;
	uint64_t rx_pkts_512_to_1023_bytes;
	uint64_t rx_pkts_1024_to_1518_bytes;
	uint64_t rx_pkts_1519_to_2047_bytes;
	uint64_t rx_pkts_2048_to_4095_bytes;
	uint64_t rx_pkts_4096_to_8191_bytes;
	uint64_t rx_pkts_8192_to_9216_bytes;
};

struct oce_drv_stats {
	struct oce_rx_stats rx;
	struct oce_tx_stats tx;
	union {
		struct oce_be_stats be;
		struct oce_xe201_stats xe201;
	} u0;
};

#define INTR_RATE_HWM                   15000
#define INTR_RATE_LWM                   10000

#define OCE_MAX_EQD 128u
#define OCE_MIN_EQD 0u

struct oce_set_eqd {
	uint32_t eq_id;
	uint32_t phase;
	uint32_t delay_multiplier;
};

struct oce_aic_obj {             /* Adaptive interrupt coalescing (AIC) info */
	boolean_t enable;
	uint32_t  min_eqd;            /* in usecs */
	uint32_t  max_eqd;            /* in usecs */
	uint32_t  cur_eqd;            /* in usecs */
	uint32_t  et_eqd;             /* configured value when aic is off */
	uint64_t  ticks;
	uint64_t  prev_rxpkts;
	uint64_t  prev_txreqs;
};

#define MAX_LOCK_DESC_LEN			32
struct oce_lock {
	struct mtx mutex;
	char name[MAX_LOCK_DESC_LEN+1];
};
#define OCE_LOCK				struct oce_lock

#define LOCK_CREATE(lock, desc) 		{ \
	strncpy((lock)->name, (desc), MAX_LOCK_DESC_LEN); \
	(lock)->name[MAX_LOCK_DESC_LEN] = '\0'; \
	mtx_init(&(lock)->mutex, (lock)->name, NULL, MTX_DEF); \
}
#define LOCK_DESTROY(lock) 			\
		if (mtx_initialized(&(lock)->mutex))\
			mtx_destroy(&(lock)->mutex)
#define TRY_LOCK(lock)				mtx_trylock(&(lock)->mutex)
#define LOCK(lock)				mtx_lock(&(lock)->mutex)
#define LOCKED(lock)				mtx_owned(&(lock)->mutex)
#define UNLOCK(lock)				mtx_unlock(&(lock)->mutex)

#define	DEFAULT_MQ_MBOX_TIMEOUT			(5 * 1000 * 1000)
#define	MBX_READY_TIMEOUT			(1 * 1000 * 1000)
#define	DEFAULT_DRAIN_TIME			200
#define	MBX_TIMEOUT_SEC				5
#define	STAT_TIMEOUT				2000000

/* size of the packet descriptor array in a transmit queue */
#define OCE_TX_RING_SIZE			2048
#define OCE_RX_RING_SIZE			1024
#define OCE_WQ_PACKET_ARRAY_SIZE		(OCE_TX_RING_SIZE/2)
#define OCE_RQ_PACKET_ARRAY_SIZE		(OCE_RX_RING_SIZE)

struct oce_dev;

enum eq_len {
	EQ_LEN_256  = 256,
	EQ_LEN_512  = 512,
	EQ_LEN_1024 = 1024,
	EQ_LEN_2048 = 2048,
	EQ_LEN_4096 = 4096
};

enum eqe_size {
	EQE_SIZE_4  = 4,
	EQE_SIZE_16 = 16
};

enum qtype {
	QTYPE_EQ,
	QTYPE_MQ,
	QTYPE_WQ,
	QTYPE_RQ,
	QTYPE_CQ,
	QTYPE_RSS
};

typedef enum qstate_e {
	QDELETED = 0x0,
	QCREATED = 0x1
} qstate_t;

struct eq_config {
	enum eq_len q_len;
	enum eqe_size item_size;
	uint32_t q_vector_num;
	uint8_t min_eqd;
	uint8_t max_eqd;
	uint8_t cur_eqd;
	uint8_t pad;
};

struct oce_eq {
	uint32_t eq_id;
	void *parent;
	void *cb_context;
	oce_ring_buffer_t *ring;
	uint32_t ref_count;
	qstate_t qstate;
	struct oce_cq *cq[OCE_MAX_CQ_EQ];
	int cq_valid; 
	struct eq_config eq_cfg;
	int vector;
	uint64_t intr;
};

enum cq_len {
	CQ_LEN_256  = 256,
	CQ_LEN_512  = 512,
	CQ_LEN_1024 = 1024,
	CQ_LEN_2048 = 2048
};

struct cq_config {
	enum cq_len q_len;
	uint32_t item_size;
	boolean_t is_eventable;
	boolean_t sol_eventable;
	boolean_t nodelay;
	uint16_t dma_coalescing;
};

typedef uint16_t(*cq_handler_t) (void *arg1);

struct oce_cq {
	uint32_t cq_id;
	void *parent;
	struct oce_eq *eq;
	cq_handler_t cq_handler;
	void *cb_arg;
	oce_ring_buffer_t *ring;
	qstate_t qstate;
	struct cq_config cq_cfg;
	uint32_t ref_count;
};


struct mq_config {
	uint32_t eqd;
	uint8_t q_len;
	uint8_t pad[3];
};


struct oce_mq {
	void *parent;
	oce_ring_buffer_t *ring;
	uint32_t mq_id;
	struct oce_cq *cq;
	struct oce_cq *async_cq;
	uint32_t mq_free;
	qstate_t qstate;
	struct mq_config cfg;
};

struct oce_mbx_ctx {
	struct oce_mbx *mbx;
	void (*cb) (void *ctx);
	void *cb_ctx;
};

struct wq_config {
	uint8_t wq_type;
	uint16_t buf_size;
	uint8_t pad[1];
	uint32_t q_len;
	uint16_t pd_id;
	uint16_t pci_fn_num;
	uint32_t eqd;	/* interrupt delay */
	uint32_t nbufs;
	uint32_t nhdl;
};

struct oce_tx_queue_stats {
	uint64_t tx_pkts;
	uint64_t tx_bytes;
	uint32_t tx_reqs;
	uint32_t tx_stops; /* number of times TX Q was stopped */
	uint32_t tx_wrbs;
	uint32_t tx_compl;
	uint32_t tx_rate;
	uint32_t ipv6_ext_hdr_tx_drop;
};

struct oce_wq {
	OCE_LOCK tx_lock;
	OCE_LOCK tx_compl_lock;
	void *parent;
	oce_ring_buffer_t *ring;
	struct oce_cq *cq;
	bus_dma_tag_t tag;
	struct oce_packet_desc pckts[OCE_WQ_PACKET_ARRAY_SIZE];
	uint32_t pkt_desc_tail;
	uint32_t pkt_desc_head;
	uint32_t wqm_used;
	boolean_t resched;
	uint32_t wq_free;
	uint32_t tx_deferd;
	uint32_t pkt_drops;
	qstate_t qstate;
	uint16_t wq_id;
	struct wq_config cfg;
	int queue_index;
	struct oce_tx_queue_stats tx_stats;
	struct buf_ring *br;
	struct task txtask;
	uint32_t db_offset;
};

struct rq_config {
	uint32_t q_len;
	uint32_t frag_size;
	uint32_t mtu;
	uint32_t if_id;
	uint32_t is_rss_queue;
	uint32_t eqd;
	uint32_t nbufs;
};

struct oce_rx_queue_stats {
	uint32_t rx_post_fail;
	uint32_t rx_ucast_pkts;
	uint32_t rx_compl;
	uint64_t rx_bytes;
	uint64_t rx_bytes_prev;
	uint64_t rx_pkts;
	uint32_t rx_rate;
	uint32_t rx_mcast_pkts;
	uint32_t rxcp_err;
	uint32_t rx_frags;
	uint32_t prev_rx_frags;
	uint32_t rx_fps;
	uint32_t rx_drops_no_frags;  /* HW has no fetched frags */
};


struct oce_rq {
	struct rq_config cfg;
	uint32_t rq_id;
	int queue_index;
	uint32_t rss_cpuid;
	void *parent;
	oce_ring_buffer_t *ring;
	struct oce_cq *cq;
	void *pad1;
	bus_dma_tag_t tag;
	struct oce_packet_desc pckts[OCE_RQ_PACKET_ARRAY_SIZE];
	uint32_t pending;
#ifdef notdef
	struct mbuf *head;
	struct mbuf *tail;
	int fragsleft;
#endif
	qstate_t qstate;
	OCE_LOCK rx_lock;
	struct oce_rx_queue_stats rx_stats;
	struct lro_ctrl lro;
	int lro_pkts_queued;
	int islro;
	struct nic_hwlro_cqe_part1 *cqe_firstpart;

};

struct link_status {
	uint8_t phys_port_speed;
	uint8_t logical_link_status;
	uint16_t qos_link_speed;
};



#define OCE_FLAGS_PCIX			0x00000001
#define OCE_FLAGS_PCIE			0x00000002
#define OCE_FLAGS_MSI_CAPABLE		0x00000004
#define OCE_FLAGS_MSIX_CAPABLE		0x00000008
#define OCE_FLAGS_USING_MSI		0x00000010
#define OCE_FLAGS_USING_MSIX		0x00000020
#define OCE_FLAGS_FUNCRESET_RQD		0x00000040
#define OCE_FLAGS_VIRTUAL_PORT		0x00000080
#define OCE_FLAGS_MBOX_ENDIAN_RQD	0x00000100
#define OCE_FLAGS_BE3			0x00000200
#define OCE_FLAGS_XE201			0x00000400
#define OCE_FLAGS_BE2			0x00000800
#define OCE_FLAGS_SH			0x00001000
#define	OCE_FLAGS_OS2BMC		0x00002000

#define OCE_DEV_BE2_CFG_BAR		1
#define OCE_DEV_CFG_BAR			0
#define OCE_PCI_CSR_BAR			2
#define OCE_PCI_DB_BAR			4

typedef struct oce_softc {
	device_t dev;
	OCE_LOCK dev_lock;

	uint32_t flags;

	uint32_t pcie_link_speed;
	uint32_t pcie_link_width;

	uint8_t fn; /* PCI function number */

	struct resource *devcfg_res;
	bus_space_tag_t devcfg_btag;
	bus_space_handle_t devcfg_bhandle;
	void *devcfg_vhandle;

	struct resource *csr_res;
	bus_space_tag_t csr_btag;
	bus_space_handle_t csr_bhandle;
	void *csr_vhandle;

	struct resource *db_res;
	bus_space_tag_t db_btag;
	bus_space_handle_t db_bhandle;
	void *db_vhandle;

	OCE_INTR_INFO intrs[OCE_MAX_EQ];
	int intr_count;
        int roce_intr_count;

	struct ifnet *ifp;

	struct ifmedia media;
	uint8_t link_status;
	uint8_t link_speed;
	uint8_t duplex;
	uint32_t qos_link_speed;
	uint32_t speed;
	uint32_t enable_hwlro;

	char fw_version[32];
	struct mac_address_format macaddr;

	OCE_DMA_MEM bsmbx;
	OCE_LOCK bmbx_lock;

	uint32_t config_number;
	uint32_t asic_revision;
	uint32_t port_id;
	uint32_t function_mode;
	uint32_t function_caps;
	uint32_t max_tx_rings;
	uint32_t max_rx_rings;

	struct oce_wq *wq[OCE_MAX_WQ];	/* TX work queues */
	struct oce_rq *rq[OCE_MAX_RQ];	/* RX work queues */
	struct oce_cq *cq[OCE_MAX_CQ];	/* Completion queues */
	struct oce_eq *eq[OCE_MAX_EQ];	/* Event queues */
	struct oce_mq *mq;		/* Mailbox queue */

	uint32_t neqs;
	uint32_t ncqs;
	uint32_t nrqs;
	uint32_t nwqs;
	uint32_t nrssqs;

	uint32_t tx_ring_size;
	uint32_t rx_ring_size;
	uint32_t rq_frag_size;

	uint32_t if_id;		/* interface ID */
	uint32_t nifs;		/* number of adapter interfaces, 0 or 1 */
	uint32_t pmac_id;	/* PMAC id */

	uint32_t if_cap_flags;

	uint32_t flow_control;
	uint8_t  promisc;

	struct oce_aic_obj aic_obj[OCE_MAX_EQ];

	/*Vlan Filtering related */
	eventhandler_tag vlan_attach;
	eventhandler_tag vlan_detach;
	uint16_t vlans_added;
	uint8_t vlan_tag[MAX_VLANS];
	/*stats */
	OCE_DMA_MEM stats_mem;
	struct oce_drv_stats oce_stats_info;
	struct callout  timer;
	int8_t be3_native;
	uint8_t hw_error;
	uint16_t qnq_debug_event;
	uint16_t qnqid;
	uint32_t pvid;
	uint32_t max_vlans;
	uint32_t bmc_filt_mask;

        void *rdma_context;
        uint32_t rdma_flags;
        struct oce_softc *next;

} OCE_SOFTC, *POCE_SOFTC;

#define OCE_RDMA_FLAG_SUPPORTED         0x00000001


/**************************************************
 * BUS memory read/write macros
 * BE3: accesses three BAR spaces (CFG, CSR, DB)
 * Lancer: accesses one BAR space (CFG)
 **************************************************/
#define OCE_READ_CSR_MPU(sc, space, o) \
	((IS_BE(sc)) ? (bus_space_read_4((sc)->space##_btag, \
					(sc)->space##_bhandle,o)) \
				: (bus_space_read_4((sc)->devcfg_btag, \
					(sc)->devcfg_bhandle,o)))
#define OCE_READ_REG32(sc, space, o) \
	((IS_BE(sc) || IS_SH(sc)) ? (bus_space_read_4((sc)->space##_btag, \
					(sc)->space##_bhandle,o)) \
				: (bus_space_read_4((sc)->devcfg_btag, \
					(sc)->devcfg_bhandle,o)))
#define OCE_READ_REG16(sc, space, o) \
	((IS_BE(sc) || IS_SH(sc)) ? (bus_space_read_2((sc)->space##_btag, \
					(sc)->space##_bhandle,o)) \
				: (bus_space_read_2((sc)->devcfg_btag, \
					(sc)->devcfg_bhandle,o)))
#define OCE_READ_REG8(sc, space, o) \
	((IS_BE(sc) || IS_SH(sc)) ? (bus_space_read_1((sc)->space##_btag, \
					(sc)->space##_bhandle,o)) \
				: (bus_space_read_1((sc)->devcfg_btag, \
					(sc)->devcfg_bhandle,o)))

#define OCE_WRITE_CSR_MPU(sc, space, o, v) \
	((IS_BE(sc)) ? (bus_space_write_4((sc)->space##_btag, \
				       (sc)->space##_bhandle,o,v)) \
				: (bus_space_write_4((sc)->devcfg_btag, \
					(sc)->devcfg_bhandle,o,v)))
#define OCE_WRITE_REG32(sc, space, o, v) \
	((IS_BE(sc) || IS_SH(sc)) ? (bus_space_write_4((sc)->space##_btag, \
				       (sc)->space##_bhandle,o,v)) \
				: (bus_space_write_4((sc)->devcfg_btag, \
					(sc)->devcfg_bhandle,o,v)))
#define OCE_WRITE_REG16(sc, space, o, v) \
	((IS_BE(sc) || IS_SH(sc)) ? (bus_space_write_2((sc)->space##_btag, \
				       (sc)->space##_bhandle,o,v)) \
				: (bus_space_write_2((sc)->devcfg_btag, \
					(sc)->devcfg_bhandle,o,v)))
#define OCE_WRITE_REG8(sc, space, o, v) \
	((IS_BE(sc) || IS_SH(sc)) ? (bus_space_write_1((sc)->space##_btag, \
				       (sc)->space##_bhandle,o,v)) \
				: (bus_space_write_1((sc)->devcfg_btag, \
					(sc)->devcfg_bhandle,o,v)))

void oce_rx_flush_lro(struct oce_rq *rq);
/***********************************************************
 * DMA memory functions
 ***********************************************************/
#define oce_dma_sync(d, f)		bus_dmamap_sync((d)->tag, (d)->map, f)
int oce_dma_alloc(POCE_SOFTC sc, bus_size_t size, POCE_DMA_MEM dma, int flags);
void oce_dma_free(POCE_SOFTC sc, POCE_DMA_MEM dma);
void oce_dma_map_addr(void *arg, bus_dma_segment_t * segs, int nseg, int error);
void oce_destroy_ring_buffer(POCE_SOFTC sc, oce_ring_buffer_t *ring);
oce_ring_buffer_t *oce_create_ring_buffer(POCE_SOFTC sc,
					  uint32_t q_len, uint32_t num_entries);
/************************************************************
 * oce_hw_xxx functions
 ************************************************************/
int oce_clear_rx_buf(struct oce_rq *rq); 
int oce_hw_pci_alloc(POCE_SOFTC sc);
int oce_hw_init(POCE_SOFTC sc);
int oce_hw_start(POCE_SOFTC sc);
int oce_create_nw_interface(POCE_SOFTC sc);
int oce_pci_soft_reset(POCE_SOFTC sc);
int oce_hw_update_multicast(POCE_SOFTC sc);
void oce_delete_nw_interface(POCE_SOFTC sc);
void oce_hw_shutdown(POCE_SOFTC sc);
void oce_hw_intr_enable(POCE_SOFTC sc);
void oce_hw_intr_disable(POCE_SOFTC sc);
void oce_hw_pci_free(POCE_SOFTC sc);

/***********************************************************
 * oce_queue_xxx functions
 ***********************************************************/
int oce_queue_init_all(POCE_SOFTC sc);
int oce_start_rq(struct oce_rq *rq);
int oce_start_wq(struct oce_wq *wq);
int oce_start_mq(struct oce_mq *mq);
int oce_start_rx(POCE_SOFTC sc);
void oce_arm_eq(POCE_SOFTC sc,
		int16_t qid, int npopped, uint32_t rearm, uint32_t clearint);
void oce_queue_release_all(POCE_SOFTC sc);
void oce_arm_cq(POCE_SOFTC sc, int16_t qid, int npopped, uint32_t rearm);
void oce_drain_eq(struct oce_eq *eq);
void oce_drain_mq_cq(void *arg);
void oce_drain_rq_cq(struct oce_rq *rq);
void oce_drain_wq_cq(struct oce_wq *wq);

uint32_t oce_page_list(oce_ring_buffer_t *ring, struct phys_addr *pa_list);

/***********************************************************
 * cleanup  functions
 ***********************************************************/
void oce_stop_rx(POCE_SOFTC sc);
void oce_discard_rx_comp(struct oce_rq *rq, int num_frags);
void oce_rx_cq_clean(struct oce_rq *rq);
void oce_rx_cq_clean_hwlro(struct oce_rq *rq);
void oce_intr_free(POCE_SOFTC sc);
void oce_free_posted_rxbuf(struct oce_rq *rq);
#if defined(INET6) || defined(INET)
void oce_free_lro(POCE_SOFTC sc);
#endif


/************************************************************
 * Mailbox functions
 ************************************************************/
int oce_fw_clean(POCE_SOFTC sc);
int oce_wait_ready(POCE_SOFTC sc);
int oce_reset_fun(POCE_SOFTC sc);
int oce_mbox_init(POCE_SOFTC sc);
int oce_mbox_dispatch(POCE_SOFTC sc, uint32_t tmo_sec);
int oce_get_fw_version(POCE_SOFTC sc);
int oce_first_mcc_cmd(POCE_SOFTC sc);

int oce_read_mac_addr(POCE_SOFTC sc, uint32_t if_id, uint8_t perm,
			uint8_t type, struct mac_address_format *mac);
int oce_get_fw_config(POCE_SOFTC sc);
int oce_if_create(POCE_SOFTC sc, uint32_t cap_flags, uint32_t en_flags,
		uint16_t vlan_tag, uint8_t *mac_addr, uint32_t *if_id);
int oce_if_del(POCE_SOFTC sc, uint32_t if_id);
int oce_config_vlan(POCE_SOFTC sc, uint32_t if_id,
		struct normal_vlan *vtag_arr, uint8_t vtag_cnt,
		uint32_t untagged, uint32_t enable_promisc);
int oce_set_flow_control(POCE_SOFTC sc, uint32_t flow_control);
int oce_config_nic_rss(POCE_SOFTC sc, uint32_t if_id, uint16_t enable_rss);
int oce_rxf_set_promiscuous(POCE_SOFTC sc, uint8_t enable);
int oce_set_common_iface_rx_filter(POCE_SOFTC sc, POCE_DMA_MEM sgl);
int oce_get_link_status(POCE_SOFTC sc, struct link_status *link);
int oce_mbox_get_nic_stats_v0(POCE_SOFTC sc, POCE_DMA_MEM pstats_dma_mem);
int oce_mbox_get_nic_stats_v1(POCE_SOFTC sc, POCE_DMA_MEM pstats_dma_mem);
int oce_mbox_get_nic_stats_v2(POCE_SOFTC sc, POCE_DMA_MEM pstats_dma_mem);
int oce_mbox_get_pport_stats(POCE_SOFTC sc, POCE_DMA_MEM pstats_dma_mem,
				uint32_t reset_stats);
int oce_mbox_get_vport_stats(POCE_SOFTC sc, POCE_DMA_MEM pstats_dma_mem,
				uint32_t req_size, uint32_t reset_stats);
int oce_update_multicast(POCE_SOFTC sc, POCE_DMA_MEM pdma_mem);
int oce_pass_through_mbox(POCE_SOFTC sc, POCE_DMA_MEM dma_mem, uint32_t req_size);
int oce_mbox_macaddr_del(POCE_SOFTC sc, uint32_t if_id, uint32_t pmac_id);
int oce_mbox_macaddr_add(POCE_SOFTC sc, uint8_t *mac_addr,
		uint32_t if_id, uint32_t *pmac_id);
int oce_mbox_cmd_test_loopback(POCE_SOFTC sc, uint32_t port_num,
	uint32_t loopback_type, uint32_t pkt_size, uint32_t num_pkts,
	uint64_t pattern);

int oce_mbox_cmd_set_loopback(POCE_SOFTC sc, uint8_t port_num,
	uint8_t loopback_type, uint8_t enable);

int oce_mbox_check_native_mode(POCE_SOFTC sc);
int oce_mbox_post(POCE_SOFTC sc,
		  struct oce_mbx *mbx, struct oce_mbx_ctx *mbxctx);
int oce_mbox_write_flashrom(POCE_SOFTC sc, uint32_t optype,uint32_t opcode,
				POCE_DMA_MEM pdma_mem, uint32_t num_bytes);
int oce_mbox_lancer_write_flashrom(POCE_SOFTC sc, uint32_t data_size,
			uint32_t data_offset,POCE_DMA_MEM pdma_mem,
			uint32_t *written_data, uint32_t *additional_status);

int oce_mbox_get_flashrom_crc(POCE_SOFTC sc, uint8_t *flash_crc,
				uint32_t offset, uint32_t optype);
int oce_mbox_get_phy_info(POCE_SOFTC sc, struct oce_phy_info *phy_info);
int oce_mbox_create_rq(struct oce_rq *rq);
int oce_mbox_create_wq(struct oce_wq *wq);
int oce_mbox_create_eq(struct oce_eq *eq);
int oce_mbox_cq_create(struct oce_cq *cq, uint32_t ncoalesce,
			 uint32_t is_eventable);
int oce_mbox_read_transrecv_data(POCE_SOFTC sc, uint32_t page_num);
void oce_mbox_eqd_modify_periodic(POCE_SOFTC sc, struct oce_set_eqd *set_eqd,
					int num);
int oce_get_profile_config(POCE_SOFTC sc, uint32_t max_rss);
int oce_get_func_config(POCE_SOFTC sc);
void mbx_common_req_hdr_init(struct mbx_hdr *hdr,
			     uint8_t dom,
			     uint8_t port,
			     uint8_t subsys,
			     uint8_t opcode,
			     uint32_t timeout, uint32_t pyld_len,
			     uint8_t version);


uint16_t oce_mq_handler(void *arg);

/************************************************************
 * Transmit functions
 ************************************************************/
uint16_t oce_wq_handler(void *arg);
void	 oce_start(struct ifnet *ifp);
void	 oce_tx_task(void *arg, int npending);

/************************************************************
 * Receive functions
 ************************************************************/
int	 oce_alloc_rx_bufs(struct oce_rq *rq, int count);
uint16_t oce_rq_handler(void *arg);


/* Sysctl functions */
void oce_add_sysctls(POCE_SOFTC sc);
void oce_refresh_queue_stats(POCE_SOFTC sc);
int  oce_refresh_nic_stats(POCE_SOFTC sc);
int  oce_stats_init(POCE_SOFTC sc);
void oce_stats_free(POCE_SOFTC sc);

/* hw lro functions */
int oce_mbox_nic_query_lro_capabilities(POCE_SOFTC sc, uint32_t *lro_rq_cnt, uint32_t *lro_flags);
int oce_mbox_nic_set_iface_lro_config(POCE_SOFTC sc, int enable);
int oce_mbox_create_rq_v2(struct oce_rq *rq);

/* Capabilities */
#define OCE_MODCAP_RSS			1
#define OCE_MAX_RSP_HANDLED		64
extern uint32_t oce_max_rsp_handled;	/* max responses */
extern uint32_t oce_rq_buf_size;

#define OCE_MAC_LOOPBACK		0x0
#define OCE_PHY_LOOPBACK		0x1
#define OCE_ONE_PORT_EXT_LOOPBACK	0x2
#define OCE_NO_LOOPBACK			0xff

#undef IFM_40G_SR4
#define IFM_40G_SR4			28

#define atomic_inc_32(x)		atomic_add_32(x, 1)
#define atomic_dec_32(x)		atomic_subtract_32(x, 1)

#define LE_64(x)			htole64(x)
#define LE_32(x)			htole32(x)
#define LE_16(x)			htole16(x)
#define HOST_64(x)			le64toh(x)
#define HOST_32(x)			le32toh(x)
#define HOST_16(x)			le16toh(x)
#define DW_SWAP(x, l)
#define IS_ALIGNED(x,a)			((x % a) == 0)
#define ADDR_HI(x)			((uint32_t)((uint64_t)(x) >> 32))
#define ADDR_LO(x)			((uint32_t)((uint64_t)(x) & 0xffffffff));

#define IF_LRO_ENABLED(sc)  (((sc)->ifp->if_capenable & IFCAP_LRO) ? 1:0)
#define IF_LSO_ENABLED(sc)  (((sc)->ifp->if_capenable & IFCAP_TSO4) ? 1:0)
#define IF_CSUM_ENABLED(sc) (((sc)->ifp->if_capenable & IFCAP_HWCSUM) ? 1:0)

#define OCE_LOG2(x) 			(oce_highbit(x))
static inline uint32_t oce_highbit(uint32_t x)
{
	int i;
	int c;
	int b;

	c = 0;
	b = 0;

	for (i = 0; i < 32; i++) {
		if ((1 << i) & x) {
			c++;
			b = i;
		}
	}

	if (c == 1)
		return b;

	return 0;
}

static inline int MPU_EP_SEMAPHORE(POCE_SOFTC sc)
{
	if (IS_BE(sc))
		return MPU_EP_SEMAPHORE_BE3;
	else if (IS_SH(sc))
		return MPU_EP_SEMAPHORE_SH;
	else
		return MPU_EP_SEMAPHORE_XE201;
}

#define TRANSCEIVER_DATA_NUM_ELE 64
#define TRANSCEIVER_DATA_SIZE 256
#define TRANSCEIVER_A0_SIZE 128
#define TRANSCEIVER_A2_SIZE 128
#define PAGE_NUM_A0 0xa0
#define PAGE_NUM_A2 0xa2
#define IS_QNQ_OR_UMC(sc) ((sc->pvid && (sc->function_mode & FNM_UMC_MODE ))\
		     || (sc->qnqid && (sc->function_mode & FNM_FLEX10_MODE)))
extern uint8_t sfp_vpd_dump_buffer[TRANSCEIVER_DATA_SIZE];

struct oce_rdma_info;
extern struct oce_rdma_if *oce_rdma_if;



/* OS2BMC related */

#define DHCP_CLIENT_PORT        68
#define DHCP_SERVER_PORT        67
#define NET_BIOS_PORT1          137
#define NET_BIOS_PORT2          138
#define DHCPV6_RAS_PORT         547

#define BMC_FILT_BROADCAST_ARP                          ((uint32_t)(1))
#define BMC_FILT_BROADCAST_DHCP_CLIENT                  ((uint32_t)(1 << 1))
#define BMC_FILT_BROADCAST_DHCP_SERVER                  ((uint32_t)(1 << 2))
#define BMC_FILT_BROADCAST_NET_BIOS                     ((uint32_t)(1 << 3))
#define BMC_FILT_BROADCAST                              ((uint32_t)(1 << 4))
#define BMC_FILT_MULTICAST_IPV6_NEIGH_ADVER             ((uint32_t)(1 << 5))
#define BMC_FILT_MULTICAST_IPV6_RA                      ((uint32_t)(1 << 6))
#define BMC_FILT_MULTICAST_IPV6_RAS                     ((uint32_t)(1 << 7))
#define BMC_FILT_MULTICAST                              ((uint32_t)(1 << 8))

#define	ND_ROUTER_ADVERT	134
#define	ND_NEIGHBOR_ADVERT	136

#define is_mc_allowed_on_bmc(sc, eh)       \
	(!is_multicast_filt_enabled(sc) && \
	ETHER_IS_MULTICAST(eh->ether_dhost) && \
	!ETHER_IS_BROADCAST(eh->ether_dhost))

#define is_bc_allowed_on_bmc(sc, eh)       \
	(!is_broadcast_filt_enabled(sc) && \
	ETHER_IS_BROADCAST(eh->ether_dhost))

#define is_arp_allowed_on_bmc(sc, et)     \
	(is_arp(et) && is_arp_filt_enabled(sc))

#define is_arp(et)     (et == ETHERTYPE_ARP)

#define is_arp_filt_enabled(sc)    \
	(sc->bmc_filt_mask & (BMC_FILT_BROADCAST_ARP))

#define is_dhcp_client_filt_enabled(sc)    \
	(sc->bmc_filt_mask & BMC_FILT_BROADCAST_DHCP_CLIENT)

#define is_dhcp_srvr_filt_enabled(sc)      \
	(sc->bmc_filt_mask & BMC_FILT_BROADCAST_DHCP_SERVER)

#define is_nbios_filt_enabled(sc)  \
	(sc->bmc_filt_mask & BMC_FILT_BROADCAST_NET_BIOS)

#define is_ipv6_na_filt_enabled(sc)        \
	(sc->bmc_filt_mask &       \
	BMC_FILT_MULTICAST_IPV6_NEIGH_ADVER)

#define is_ipv6_ra_filt_enabled(sc)        \
	(sc->bmc_filt_mask & BMC_FILT_MULTICAST_IPV6_RA)

#define is_ipv6_ras_filt_enabled(sc)       \
	(sc->bmc_filt_mask & BMC_FILT_MULTICAST_IPV6_RAS)

#define is_broadcast_filt_enabled(sc)      \
	(sc->bmc_filt_mask & BMC_FILT_BROADCAST)

#define is_multicast_filt_enabled(sc)      \
	(sc->bmc_filt_mask & BMC_FILT_MULTICAST)

#define is_os2bmc_enabled(sc) (sc->flags & OCE_FLAGS_OS2BMC)

#define LRO_FLAGS_HASH_MODE 0x00000001
#define LRO_FLAGS_RSS_MODE 0x00000004
#define LRO_FLAGS_CLSC_IPV4 0x00000010
#define LRO_FLAGS_CLSC_IPV6 0x00000020
#define NIC_RQ_FLAGS_RSS 0x0001
#define NIC_RQ_FLAGS_LRO 0x0020

