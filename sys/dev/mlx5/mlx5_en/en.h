/*-
 * Copyright (c) 2015 Mellanox Technologies. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS `AS IS' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef _MLX5_EN_H_
#define	_MLX5_EN_H_

#include <linux/kmod.h>
#include <linux/page.h>
#include <linux/slab.h>
#include <linux/if_vlan.h>
#include <linux/if_ether.h>
#include <linux/vmalloc.h>
#include <linux/moduleparam.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/tcp.h>
#include <netinet/tcp_lro.h>
#include <netinet/udp.h>
#include <net/ethernet.h>
#include <sys/buf_ring.h>
#include <sys/kthread.h>

#include "opt_rss.h"

#ifdef	RSS
#include <net/rss_config.h>
#include <netinet/in_rss.h>
#endif

#include <machine/bus.h>

#include <dev/mlx5/driver.h>
#include <dev/mlx5/qp.h>
#include <dev/mlx5/cq.h>
#include <dev/mlx5/port.h>
#include <dev/mlx5/vport.h>
#include <dev/mlx5/diagnostics.h>

#include <dev/mlx5/mlx5_core/wq.h>
#include <dev/mlx5/mlx5_core/transobj.h>
#include <dev/mlx5/mlx5_core/mlx5_core.h>

#define	IEEE_8021QAZ_MAX_TCS	8

#define	MLX5E_PARAMS_MINIMUM_LOG_SQ_SIZE                0x7
#define	MLX5E_PARAMS_DEFAULT_LOG_SQ_SIZE                0xa
#define	MLX5E_PARAMS_MAXIMUM_LOG_SQ_SIZE                0xe

#define	MLX5E_PARAMS_MINIMUM_LOG_RQ_SIZE                0x7
#define	MLX5E_PARAMS_DEFAULT_LOG_RQ_SIZE                0xa
#define	MLX5E_PARAMS_MAXIMUM_LOG_RQ_SIZE                0xe

#define	MLX5E_MAX_RX_SEGS 7

#ifndef MLX5E_MAX_RX_BYTES
#define	MLX5E_MAX_RX_BYTES MCLBYTES
#endif

#if (MLX5E_MAX_RX_SEGS == 1)
/* FreeBSD HW LRO is limited by 16KB - the size of max mbuf */
#define	MLX5E_PARAMS_DEFAULT_LRO_WQE_SZ                 MJUM16BYTES
#else
#define	MLX5E_PARAMS_DEFAULT_LRO_WQE_SZ \
    MIN(65535, MLX5E_MAX_RX_SEGS * MLX5E_MAX_RX_BYTES)
#endif
#define	MLX5E_PARAMS_DEFAULT_RX_CQ_MODERATION_USEC      0x10
#define	MLX5E_PARAMS_DEFAULT_RX_CQ_MODERATION_USEC_FROM_CQE	0x3
#define	MLX5E_PARAMS_DEFAULT_RX_CQ_MODERATION_PKTS      0x20
#define	MLX5E_PARAMS_DEFAULT_TX_CQ_MODERATION_USEC      0x10
#define	MLX5E_PARAMS_DEFAULT_TX_CQ_MODERATION_PKTS      0x20
#define	MLX5E_PARAMS_DEFAULT_MIN_RX_WQES                0x80
#define	MLX5E_PARAMS_DEFAULT_RX_HASH_LOG_TBL_SZ         0x7
#define	MLX5E_CACHELINE_SIZE CACHE_LINE_SIZE
#define	MLX5E_HW2SW_MTU(hwmtu) \
    ((hwmtu) - (ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN + ETHER_CRC_LEN))
#define	MLX5E_SW2HW_MTU(swmtu) \
    ((swmtu) + (ETHER_HDR_LEN + ETHER_VLAN_ENCAP_LEN + ETHER_CRC_LEN))
#define	MLX5E_SW2MB_MTU(swmtu) \
    (MLX5E_SW2HW_MTU(swmtu) + MLX5E_NET_IP_ALIGN)
#define	MLX5E_MTU_MIN		72	/* Min MTU allowed by the kernel */
#define	MLX5E_MTU_MAX		MIN(ETHERMTU_JUMBO, MJUM16BYTES)	/* Max MTU of Ethernet
									 * jumbo frames */

#define	MLX5E_BUDGET_MAX	8192	/* RX and TX */
#define	MLX5E_RX_BUDGET_MAX	256
#define	MLX5E_SQ_BF_BUDGET	16
#define	MLX5E_SQ_TX_QUEUE_SIZE	4096	/* SQ drbr queue size */

#define	MLX5E_MAX_TX_NUM_TC	8	/* units */
#define	MLX5E_MAX_TX_HEADER	128	/* bytes */
#define	MLX5E_MAX_TX_PAYLOAD_SIZE	65536	/* bytes */
#define	MLX5E_MAX_TX_MBUF_SIZE	65536	/* bytes */
#define	MLX5E_MAX_TX_MBUF_FRAGS	\
    ((MLX5_SEND_WQE_MAX_WQEBBS * MLX5_SEND_WQEBB_NUM_DS) - \
    (MLX5E_MAX_TX_HEADER / MLX5_SEND_WQE_DS) - \
    1 /* the maximum value of the DS counter is 0x3F and not 0x40 */)	/* units */
#define	MLX5E_MAX_TX_INLINE \
  (MLX5E_MAX_TX_HEADER - sizeof(struct mlx5e_tx_wqe) + \
  sizeof(((struct mlx5e_tx_wqe *)0)->eth.inline_hdr_start))	/* bytes */

#define	MLX5E_100MB (100000)
#define	MLX5E_1GB   (1000000)

MALLOC_DECLARE(M_MLX5EN);

struct mlx5_core_dev;
struct mlx5e_cq;

typedef void (mlx5e_cq_comp_t)(struct mlx5_core_cq *);

#define	MLX5E_STATS_COUNT(a,b,c,d) a
#define	MLX5E_STATS_VAR(a,b,c,d) b;
#define	MLX5E_STATS_DESC(a,b,c,d) c, d,

#define	MLX5E_VPORT_STATS(m)						\
  /* HW counters */							\
  m(+1, u64 rx_packets, "rx_packets", "Received packets")		\
  m(+1, u64 rx_bytes, "rx_bytes", "Received bytes")			\
  m(+1, u64 tx_packets, "tx_packets", "Transmitted packets")		\
  m(+1, u64 tx_bytes, "tx_bytes", "Transmitted bytes")			\
  m(+1, u64 rx_error_packets, "rx_error_packets", "Received error packets") \
  m(+1, u64 rx_error_bytes, "rx_error_bytes", "Received error bytes")	\
  m(+1, u64 tx_error_packets, "tx_error_packets", "Transmitted error packets") \
  m(+1, u64 tx_error_bytes, "tx_error_bytes", "Transmitted error bytes") \
  m(+1, u64 rx_unicast_packets, "rx_unicast_packets", "Received unicast packets") \
  m(+1, u64 rx_unicast_bytes, "rx_unicast_bytes", "Received unicast bytes") \
  m(+1, u64 tx_unicast_packets, "tx_unicast_packets", "Transmitted unicast packets") \
  m(+1, u64 tx_unicast_bytes, "tx_unicast_bytes", "Transmitted unicast bytes") \
  m(+1, u64 rx_multicast_packets, "rx_multicast_packets", "Received multicast packets") \
  m(+1, u64 rx_multicast_bytes, "rx_multicast_bytes", "Received multicast bytes") \
  m(+1, u64 tx_multicast_packets, "tx_multicast_packets", "Transmitted multicast packets") \
  m(+1, u64 tx_multicast_bytes, "tx_multicast_bytes", "Transmitted multicast bytes") \
  m(+1, u64 rx_broadcast_packets, "rx_broadcast_packets", "Received broadcast packets") \
  m(+1, u64 rx_broadcast_bytes, "rx_broadcast_bytes", "Received broadcast bytes") \
  m(+1, u64 tx_broadcast_packets, "tx_broadcast_packets", "Transmitted broadcast packets") \
  m(+1, u64 tx_broadcast_bytes, "tx_broadcast_bytes", "Transmitted broadcast bytes") \
  m(+1, u64 rx_out_of_buffer, "rx_out_of_buffer", "Receive out of buffer, no recv wqes events") \
  /* SW counters */							\
  m(+1, u64 tso_packets, "tso_packets", "Transmitted TSO packets")	\
  m(+1, u64 tso_bytes, "tso_bytes", "Transmitted TSO bytes")		\
  m(+1, u64 lro_packets, "lro_packets", "Received LRO packets")		\
  m(+1, u64 lro_bytes, "lro_bytes", "Received LRO bytes")		\
  m(+1, u64 sw_lro_queued, "sw_lro_queued", "Packets queued for SW LRO")	\
  m(+1, u64 sw_lro_flushed, "sw_lro_flushed", "Packets flushed from SW LRO")	\
  m(+1, u64 rx_csum_good, "rx_csum_good", "Received checksum valid packets") \
  m(+1, u64 rx_csum_none, "rx_csum_none", "Received no checksum packets") \
  m(+1, u64 tx_csum_offload, "tx_csum_offload", "Transmit checksum offload packets") \
  m(+1, u64 tx_queue_dropped, "tx_queue_dropped", "Transmit queue dropped") \
  m(+1, u64 tx_defragged, "tx_defragged", "Transmit queue defragged") \
  m(+1, u64 rx_wqe_err, "rx_wqe_err", "Receive WQE errors") \
  m(+1, u64 tx_jumbo_packets, "tx_jumbo_packets", "TX packets greater than 1518 octets")

#define	MLX5E_VPORT_STATS_NUM (0 MLX5E_VPORT_STATS(MLX5E_STATS_COUNT))

struct mlx5e_vport_stats {
	struct	sysctl_ctx_list ctx;
	u64	arg [0];
	MLX5E_VPORT_STATS(MLX5E_STATS_VAR)
	u32	rx_out_of_buffer_prev;
};

#define	MLX5E_PPORT_IEEE802_3_STATS(m)					\
  m(+1, u64 frames_tx, "frames_tx", "Frames transmitted")		\
  m(+1, u64 frames_rx, "frames_rx", "Frames received")			\
  m(+1, u64 check_seq_err, "check_seq_err", "Sequence errors")		\
  m(+1, u64 alignment_err, "alignment_err", "Alignment errors")	\
  m(+1, u64 octets_tx, "octets_tx", "Bytes transmitted")		\
  m(+1, u64 octets_received, "octets_received", "Bytes received")	\
  m(+1, u64 multicast_xmitted, "multicast_xmitted", "Multicast transmitted") \
  m(+1, u64 broadcast_xmitted, "broadcast_xmitted", "Broadcast transmitted") \
  m(+1, u64 multicast_rx, "multicast_rx", "Multicast received")	\
  m(+1, u64 broadcast_rx, "broadcast_rx", "Broadcast received")	\
  m(+1, u64 in_range_len_errors, "in_range_len_errors", "In range length errors") \
  m(+1, u64 out_of_range_len, "out_of_range_len", "Out of range length errors") \
  m(+1, u64 too_long_errors, "too_long_errors", "Too long errors")	\
  m(+1, u64 symbol_err, "symbol_err", "Symbol errors")			\
  m(+1, u64 mac_control_tx, "mac_control_tx", "MAC control transmitted") \
  m(+1, u64 mac_control_rx, "mac_control_rx", "MAC control received")	\
  m(+1, u64 unsupported_op_rx, "unsupported_op_rx", "Unsupported operation received") \
  m(+1, u64 pause_ctrl_rx, "pause_ctrl_rx", "Pause control received")	\
  m(+1, u64 pause_ctrl_tx, "pause_ctrl_tx", "Pause control transmitted")

#define	MLX5E_PPORT_RFC2819_STATS(m)					\
  m(+1, u64 drop_events, "drop_events", "Dropped events")		\
  m(+1, u64 octets, "octets", "Octets")					\
  m(+1, u64 pkts, "pkts", "Packets")					\
  m(+1, u64 broadcast_pkts, "broadcast_pkts", "Broadcast packets")	\
  m(+1, u64 multicast_pkts, "multicast_pkts", "Multicast packets")	\
  m(+1, u64 crc_align_errors, "crc_align_errors", "CRC alignment errors") \
  m(+1, u64 undersize_pkts, "undersize_pkts", "Undersized packets")	\
  m(+1, u64 oversize_pkts, "oversize_pkts", "Oversized packets")	\
  m(+1, u64 fragments, "fragments", "Fragments")			\
  m(+1, u64 jabbers, "jabbers", "Jabbers")				\
  m(+1, u64 collisions, "collisions", "Collisions")

#define	MLX5E_PPORT_RFC2819_STATS_DEBUG(m)				\
  m(+1, u64 p64octets, "p64octets", "Bytes")				\
  m(+1, u64 p65to127octets, "p65to127octets", "Bytes")			\
  m(+1, u64 p128to255octets, "p128to255octets", "Bytes")		\
  m(+1, u64 p256to511octets, "p256to511octets", "Bytes")		\
  m(+1, u64 p512to1023octets, "p512to1023octets", "Bytes")		\
  m(+1, u64 p1024to1518octets, "p1024to1518octets", "Bytes")		\
  m(+1, u64 p1519to2047octets, "p1519to2047octets", "Bytes")		\
  m(+1, u64 p2048to4095octets, "p2048to4095octets", "Bytes")		\
  m(+1, u64 p4096to8191octets, "p4096to8191octets", "Bytes")		\
  m(+1, u64 p8192to10239octets, "p8192to10239octets", "Bytes")

#define	MLX5E_PPORT_RFC2863_STATS_DEBUG(m)				\
  m(+1, u64 in_octets, "in_octets", "In octets")			\
  m(+1, u64 in_ucast_pkts, "in_ucast_pkts", "In unicast packets")	\
  m(+1, u64 in_discards, "in_discards", "In discards")			\
  m(+1, u64 in_errors, "in_errors", "In errors")			\
  m(+1, u64 in_unknown_protos, "in_unknown_protos", "In unknown protocols") \
  m(+1, u64 out_octets, "out_octets", "Out octets")			\
  m(+1, u64 out_ucast_pkts, "out_ucast_pkts", "Out unicast packets")	\
  m(+1, u64 out_discards, "out_discards", "Out discards")		\
  m(+1, u64 out_errors, "out_errors", "Out errors")			\
  m(+1, u64 in_multicast_pkts, "in_multicast_pkts", "In multicast packets") \
  m(+1, u64 in_broadcast_pkts, "in_broadcast_pkts", "In broadcast packets") \
  m(+1, u64 out_multicast_pkts, "out_multicast_pkts", "Out multicast packets") \
  m(+1, u64 out_broadcast_pkts, "out_broadcast_pkts", "Out broadcast packets")

#define	MLX5E_PPORT_PHYSICAL_LAYER_STATS_DEBUG(m)                                    		\
  m(+1, u64 time_since_last_clear, "time_since_last_clear",				\
			"Time since the last counters clear event (msec)")		\
  m(+1, u64 symbol_errors, "symbol_errors", "Symbol errors")				\
  m(+1, u64 sync_headers_errors, "sync_headers_errors", "Sync header error counter")	\
  m(+1, u64 bip_errors_lane0, "edpl_bip_errors_lane0",					\
			"Indicates the number of PRBS errors on lane 0")		\
  m(+1, u64 bip_errors_lane1, "edpl_bip_errors_lane1",					\
			"Indicates the number of PRBS errors on lane 1")		\
  m(+1, u64 bip_errors_lane2, "edpl_bip_errors_lane2",					\
			"Indicates the number of PRBS errors on lane 2")		\
  m(+1, u64 bip_errors_lane3, "edpl_bip_errors_lane3",					\
			"Indicates the number of PRBS errors on lane 3")		\
  m(+1, u64 fc_corrected_blocks_lane0, "fc_corrected_blocks_lane0",			\
			"FEC correctable block counter lane 0")				\
  m(+1, u64 fc_corrected_blocks_lane1, "fc_corrected_blocks_lane1",			\
			"FEC correctable block counter lane 1")				\
  m(+1, u64 fc_corrected_blocks_lane2, "fc_corrected_blocks_lane2",			\
			"FEC correctable block counter lane 2")				\
  m(+1, u64 fc_corrected_blocks_lane3, "fc_corrected_blocks_lane3",			\
			"FEC correctable block counter lane 3")				\
  m(+1, u64 rs_corrected_blocks, "rs_corrected_blocks",					\
			"FEC correcable block counter")					\
  m(+1, u64 rs_uncorrectable_blocks, "rs_uncorrectable_blocks",				\
			"FEC uncorrecable block counter")				\
  m(+1, u64 rs_no_errors_blocks, "rs_no_errors_blocks",					\
			"The number of RS-FEC blocks received that had no errors")	\
  m(+1, u64 rs_single_error_blocks, "rs_single_error_blocks",				\
			"The number of corrected RS-FEC blocks received that had"	\
			"exactly 1 error symbol")					\
  m(+1, u64 rs_corrected_symbols_total, "rs_corrected_symbols_total",			\
			"Port FEC corrected symbol counter")				\
  m(+1, u64 rs_corrected_symbols_lane0, "rs_corrected_symbols_lane0",			\
			"FEC corrected symbol counter lane 0")				\
  m(+1, u64 rs_corrected_symbols_lane1, "rs_corrected_symbols_lane1",			\
			"FEC corrected symbol counter lane 1")				\
  m(+1, u64 rs_corrected_symbols_lane2, "rs_corrected_symbols_lane2",			\
			"FEC corrected symbol counter lane 2")				\
  m(+1, u64 rs_corrected_symbols_lane3, "rs_corrected_symbols_lane3",			\
			"FEC corrected symbol counter lane 3")

/* Per priority statistics for PFC */
#define	MLX5E_PPORT_PER_PRIO_STATS_SUB(m,n,p)			\
  m(n, p, +1, u64, rx_octets, "rx_octets", "Received octets")		\
  m(n, p, +1, u64, reserved_0, "reserved_0", "Reserved")		\
  m(n, p, +1, u64, reserved_1, "reserved_1", "Reserved")		\
  m(n, p, +1, u64, reserved_2, "reserved_2", "Reserved")		\
  m(n, p, +1, u64, rx_frames, "rx_frames", "Received frames")		\
  m(n, p, +1, u64, tx_octets, "tx_octets", "Transmitted octets")	\
  m(n, p, +1, u64, reserved_3, "reserved_3", "Reserved")		\
  m(n, p, +1, u64, reserved_4, "reserved_4", "Reserved")		\
  m(n, p, +1, u64, reserved_5, "reserved_5", "Reserved")		\
  m(n, p, +1, u64, tx_frames, "tx_frames", "Transmitted frames")	\
  m(n, p, +1, u64, rx_pause, "rx_pause", "Received pause frames")	\
  m(n, p, +1, u64, rx_pause_duration, "rx_pause_duration",		\
	"Received pause duration")					\
  m(n, p, +1, u64, tx_pause, "tx_pause", "Transmitted pause frames")	\
  m(n, p, +1, u64, tx_pause_duration, "tx_pause_duration",		\
	"Transmitted pause duration")					\
  m(n, p, +1, u64, rx_pause_transition, "rx_pause_transition",		\
	"Received pause transitions")					\
  m(n, p, +1, u64, rx_discards, "rx_discards", "Discarded received frames") \
  m(n, p, +1, u64, device_stall_minor_watermark,			\
	"device_stall_minor_watermark", "Device stall minor watermark")	\
  m(n, p, +1, u64, device_stall_critical_watermark,			\
	"device_stall_critical_watermark", "Device stall critical watermark")

#define	MLX5E_PPORT_PER_PRIO_STATS_PREFIX(m,p,c,t,f,s,d) \
  m(c, t pri_##p##_##f, "prio" #p "_" s, "Priority " #p " - " d)

#define	MLX5E_PPORT_PER_PRIO_STATS_NUM_PRIO 8

#define	MLX5E_PPORT_PER_PRIO_STATS(m) \
  MLX5E_PPORT_PER_PRIO_STATS_SUB(MLX5E_PPORT_PER_PRIO_STATS_PREFIX,m,0) \
  MLX5E_PPORT_PER_PRIO_STATS_SUB(MLX5E_PPORT_PER_PRIO_STATS_PREFIX,m,1) \
  MLX5E_PPORT_PER_PRIO_STATS_SUB(MLX5E_PPORT_PER_PRIO_STATS_PREFIX,m,2) \
  MLX5E_PPORT_PER_PRIO_STATS_SUB(MLX5E_PPORT_PER_PRIO_STATS_PREFIX,m,3) \
  MLX5E_PPORT_PER_PRIO_STATS_SUB(MLX5E_PPORT_PER_PRIO_STATS_PREFIX,m,4) \
  MLX5E_PPORT_PER_PRIO_STATS_SUB(MLX5E_PPORT_PER_PRIO_STATS_PREFIX,m,5) \
  MLX5E_PPORT_PER_PRIO_STATS_SUB(MLX5E_PPORT_PER_PRIO_STATS_PREFIX,m,6) \
  MLX5E_PPORT_PER_PRIO_STATS_SUB(MLX5E_PPORT_PER_PRIO_STATS_PREFIX,m,7)

/*
 * Make sure to update mlx5e_update_pport_counters()
 * when adding a new MLX5E_PPORT_STATS block
 */
#define	MLX5E_PPORT_STATS(m)			\
  MLX5E_PPORT_PER_PRIO_STATS(m)		\
  MLX5E_PPORT_IEEE802_3_STATS(m)		\
  MLX5E_PPORT_RFC2819_STATS(m)

#define	MLX5E_PORT_STATS_DEBUG(m)		\
  MLX5E_PPORT_RFC2819_STATS_DEBUG(m)		\
  MLX5E_PPORT_RFC2863_STATS_DEBUG(m)		\
  MLX5E_PPORT_PHYSICAL_LAYER_STATS_DEBUG(m)

#define	MLX5E_PPORT_IEEE802_3_STATS_NUM \
  (0 MLX5E_PPORT_IEEE802_3_STATS(MLX5E_STATS_COUNT))
#define	MLX5E_PPORT_RFC2819_STATS_NUM \
  (0 MLX5E_PPORT_RFC2819_STATS(MLX5E_STATS_COUNT))
#define	MLX5E_PPORT_STATS_NUM \
  (0 MLX5E_PPORT_STATS(MLX5E_STATS_COUNT))

#define	MLX5E_PPORT_PER_PRIO_STATS_NUM \
  (0 MLX5E_PPORT_PER_PRIO_STATS(MLX5E_STATS_COUNT))
#define	MLX5E_PPORT_RFC2819_STATS_DEBUG_NUM \
  (0 MLX5E_PPORT_RFC2819_STATS_DEBUG(MLX5E_STATS_COUNT))
#define	MLX5E_PPORT_RFC2863_STATS_DEBUG_NUM \
  (0 MLX5E_PPORT_RFC2863_STATS_DEBUG(MLX5E_STATS_COUNT))
#define	MLX5E_PPORT_PHYSICAL_LAYER_STATS_DEBUG_NUM \
  (0 MLX5E_PPORT_PHYSICAL_LAYER_STATS_DEBUG(MLX5E_STATS_COUNT))
#define	MLX5E_PORT_STATS_DEBUG_NUM \
  (0 MLX5E_PORT_STATS_DEBUG(MLX5E_STATS_COUNT))

struct mlx5e_pport_stats {
	struct	sysctl_ctx_list ctx;
	u64	arg [0];
	MLX5E_PPORT_STATS(MLX5E_STATS_VAR)
};

struct mlx5e_port_stats_debug {
	struct	sysctl_ctx_list ctx;
	u64	arg [0];
	MLX5E_PORT_STATS_DEBUG(MLX5E_STATS_VAR)
};

#define	MLX5E_RQ_STATS(m)					\
  m(+1, u64 packets, "packets", "Received packets")		\
  m(+1, u64 bytes, "bytes", "Received bytes")			\
  m(+1, u64 csum_none, "csum_none", "Received packets")		\
  m(+1, u64 lro_packets, "lro_packets", "Received LRO packets")	\
  m(+1, u64 lro_bytes, "lro_bytes", "Received LRO bytes")	\
  m(+1, u64 sw_lro_queued, "sw_lro_queued", "Packets queued for SW LRO")	\
  m(+1, u64 sw_lro_flushed, "sw_lro_flushed", "Packets flushed from SW LRO")	\
  m(+1, u64 wqe_err, "wqe_err", "Received packets")

#define	MLX5E_RQ_STATS_NUM (0 MLX5E_RQ_STATS(MLX5E_STATS_COUNT))

struct mlx5e_rq_stats {
	struct	sysctl_ctx_list ctx;
	u64	arg [0];
	MLX5E_RQ_STATS(MLX5E_STATS_VAR)
};

#define	MLX5E_SQ_STATS(m)						\
  m(+1, u64 packets, "packets", "Transmitted packets")			\
  m(+1, u64 bytes, "bytes", "Transmitted bytes")			\
  m(+1, u64 tso_packets, "tso_packets", "Transmitted packets")		\
  m(+1, u64 tso_bytes, "tso_bytes", "Transmitted bytes")		\
  m(+1, u64 csum_offload_none, "csum_offload_none", "Transmitted packets")	\
  m(+1, u64 defragged, "defragged", "Transmitted packets")		\
  m(+1, u64 dropped, "dropped", "Transmitted packets")			\
  m(+1, u64 nop, "nop", "Transmitted packets")

#define	MLX5E_SQ_STATS_NUM (0 MLX5E_SQ_STATS(MLX5E_STATS_COUNT))

struct mlx5e_sq_stats {
	struct	sysctl_ctx_list ctx;
	u64	arg [0];
	MLX5E_SQ_STATS(MLX5E_STATS_VAR)
};

struct mlx5e_stats {
	struct mlx5e_vport_stats vport;
	struct mlx5e_pport_stats pport;
	struct mlx5e_port_stats_debug port_stats_debug;
};

struct mlx5e_rq_param {
	u32	rqc [MLX5_ST_SZ_DW(rqc)];
	struct mlx5_wq_param wq;
};

struct mlx5e_sq_param {
	u32	sqc [MLX5_ST_SZ_DW(sqc)];
	struct mlx5_wq_param wq;
};

struct mlx5e_cq_param {
	u32	cqc [MLX5_ST_SZ_DW(cqc)];
	struct mlx5_wq_param wq;
};

struct mlx5e_params {
	u8	log_sq_size;
	u8	log_rq_size;
	u16	num_channels;
	u8	default_vlan_prio;
	u8	num_tc;
	u8	rx_cq_moderation_mode;
	u8	tx_cq_moderation_mode;
	u16	rx_cq_moderation_usec;
	u16	rx_cq_moderation_pkts;
	u16	tx_cq_moderation_usec;
	u16	tx_cq_moderation_pkts;
	u16	min_rx_wqes;
	bool	hw_lro_en;
	bool	cqe_zipping_en;
	u32	lro_wqe_sz;
	u16	rx_hash_log_tbl_sz;
	u32	tx_pauseframe_control __aligned(4);
	u32	rx_pauseframe_control __aligned(4);
	u32	tx_priority_flow_control __aligned(4);
	u32	rx_priority_flow_control __aligned(4);
	u16	tx_max_inline;
	u8	tx_min_inline_mode;
	u8	channels_rsss;
};

#define	MLX5E_PARAMS(m)							\
  m(+1, u64 tx_queue_size_max, "tx_queue_size_max", "Max send queue size") \
  m(+1, u64 rx_queue_size_max, "rx_queue_size_max", "Max receive queue size") \
  m(+1, u64 tx_queue_size, "tx_queue_size", "Default send queue size")	\
  m(+1, u64 rx_queue_size, "rx_queue_size", "Default receive queue size") \
  m(+1, u64 channels, "channels", "Default number of channels")		\
  m(+1, u64 channels_rsss, "channels_rsss", "Default channels receive side scaling stride") \
  m(+1, u64 coalesce_usecs_max, "coalesce_usecs_max", "Maximum usecs for joining packets") \
  m(+1, u64 coalesce_pkts_max, "coalesce_pkts_max", "Maximum packets to join") \
  m(+1, u64 rx_coalesce_usecs, "rx_coalesce_usecs", "Limit in usec for joining rx packets") \
  m(+1, u64 rx_coalesce_pkts, "rx_coalesce_pkts", "Maximum number of rx packets to join") \
  m(+1, u64 rx_coalesce_mode, "rx_coalesce_mode", "0: EQE mode 1: CQE mode") \
  m(+1, u64 tx_coalesce_usecs, "tx_coalesce_usecs", "Limit in usec for joining tx packets") \
  m(+1, u64 tx_coalesce_pkts, "tx_coalesce_pkts", "Maximum number of tx packets to join") \
  m(+1, u64 tx_coalesce_mode, "tx_coalesce_mode", "0: EQE mode 1: CQE mode") \
  m(+1, u64 tx_completion_fact, "tx_completion_fact", "1..MAX: Completion event ratio") \
  m(+1, u64 tx_completion_fact_max, "tx_completion_fact_max", "Maximum completion event ratio") \
  m(+1, u64 hw_lro, "hw_lro", "set to enable hw_lro") \
  m(+1, u64 cqe_zipping, "cqe_zipping", "0 : CQE zipping disabled") \
  m(+1, u64 modify_tx_dma, "modify_tx_dma", "0: Enable TX 1: Disable TX") \
  m(+1, u64 modify_rx_dma, "modify_rx_dma", "0: Enable RX 1: Disable RX") \
  m(+1, u64 diag_pci_enable, "diag_pci_enable", "0: Disabled 1: Enabled") \
  m(+1, u64 diag_general_enable, "diag_general_enable", "0: Disabled 1: Enabled") \
  m(+1, u64 hw_mtu, "hw_mtu", "Current hardware MTU value") \
  m(+1, u64 mc_local_lb, "mc_local_lb", "0: Local multicast loopback enabled 1: Disabled") \
  m(+1, u64 uc_local_lb, "uc_local_lb", "0: Local unicast loopback enabled 1: Disabled")


#define	MLX5E_PARAMS_NUM (0 MLX5E_PARAMS(MLX5E_STATS_COUNT))

struct mlx5e_params_ethtool {
	u64	arg [0];
	MLX5E_PARAMS(MLX5E_STATS_VAR)
	u64	max_bw_value[IEEE_8021QAZ_MAX_TCS];
	u8	max_bw_share[IEEE_8021QAZ_MAX_TCS];
	u8	prio_tc[IEEE_8021QAZ_MAX_TCS];
	u8	dscp2prio[MLX5_MAX_SUPPORTED_DSCP];
	u8	trust_state;
};

/* EEPROM Standards for plug in modules */
#ifndef MLX5E_ETH_MODULE_SFF_8472
#define	MLX5E_ETH_MODULE_SFF_8472	0x1
#define	MLX5E_ETH_MODULE_SFF_8472_LEN	128
#endif

#ifndef MLX5E_ETH_MODULE_SFF_8636
#define	MLX5E_ETH_MODULE_SFF_8636	0x2
#define	MLX5E_ETH_MODULE_SFF_8636_LEN	256
#endif

#ifndef MLX5E_ETH_MODULE_SFF_8436
#define	MLX5E_ETH_MODULE_SFF_8436	0x3
#define	MLX5E_ETH_MODULE_SFF_8436_LEN	256
#endif

/* EEPROM I2C Addresses */
#define	MLX5E_I2C_ADDR_LOW		0x50
#define	MLX5E_I2C_ADDR_HIGH		0x51

#define	MLX5E_EEPROM_LOW_PAGE		0x0
#define	MLX5E_EEPROM_HIGH_PAGE		0x3

#define	MLX5E_EEPROM_HIGH_PAGE_OFFSET	128
#define	MLX5E_EEPROM_PAGE_LENGTH	256

#define	MLX5E_EEPROM_INFO_BYTES		0x3

struct mlx5e_cq {
	/* data path - accessed per cqe */
	struct mlx5_cqwq wq;

	/* data path - accessed per HW polling */
	struct mlx5_core_cq mcq;

	/* control */
	struct mlx5e_priv *priv;
	struct mlx5_wq_ctrl wq_ctrl;
} __aligned(MLX5E_CACHELINE_SIZE);

struct mlx5e_rq_mbuf {
	bus_dmamap_t	dma_map;
	caddr_t		data;
	struct mbuf	*mbuf;
};

struct mlx5e_rq {
	/* data path */
	struct mlx5_wq_ll wq;
	struct mtx mtx;
	bus_dma_tag_t dma_tag;
	u32	wqe_sz;
	u32	nsegs;
	struct mlx5e_rq_mbuf *mbuf;
	struct ifnet *ifp;
	struct mlx5e_rq_stats stats;
	struct mlx5e_cq cq;
	struct lro_ctrl lro;
	volatile int enabled;
	int	ix;

	/* control */
	struct mlx5_wq_ctrl wq_ctrl;
	u32	rqn;
	struct mlx5e_channel *channel;
	struct callout watchdog;
} __aligned(MLX5E_CACHELINE_SIZE);

struct mlx5e_sq_mbuf {
	bus_dmamap_t dma_map;
	struct mbuf *mbuf;
	u32	num_bytes;
	u32	num_wqebbs;
};

enum {
	MLX5E_SQ_READY,
	MLX5E_SQ_FULL
};

struct mlx5e_snd_tag {
	struct m_snd_tag m_snd_tag;	/* send tag */
	u32	type;	/* tag type */
};

struct mlx5e_sq {
	/* data path */
	struct	mtx lock;
	bus_dma_tag_t dma_tag;
	struct	mtx comp_lock;

	/* dirtied @completion */
	u16	cc;

	/* dirtied @xmit */
	u16	pc __aligned(MLX5E_CACHELINE_SIZE);
	u16	bf_offset;
	u16	cev_counter;		/* completion event counter */
	u16	cev_factor;		/* completion event factor */
	u16	cev_next_state;		/* next completion event state */
#define	MLX5E_CEV_STATE_INITIAL 0	/* timer not started */
#define	MLX5E_CEV_STATE_SEND_NOPS 1	/* send NOPs */
#define	MLX5E_CEV_STATE_HOLD_NOPS 2	/* don't send NOPs yet */
	u16	running;		/* set if SQ is running */
	struct callout cev_callout;
	union {
		u32	d32[2];
		u64	d64;
	} doorbell;
	struct	mlx5e_sq_stats stats;

	struct	mlx5e_cq cq;

	/* pointers to per packet info: write@xmit, read@completion */
	struct	mlx5e_sq_mbuf *mbuf;
	struct	buf_ring *br;

	/* read only */
	struct	mlx5_wq_cyc wq;
	struct	mlx5_uar uar;
	struct	ifnet *ifp;
	u32	sqn;
	u32	bf_buf_size;
	u32	mkey_be;
	u16	max_inline;
	u8	min_inline_mode;
	u8	min_insert_caps;
#define	MLX5E_INSERT_VLAN 1
#define	MLX5E_INSERT_NON_VLAN 2

	/* control path */
	struct	mlx5_wq_ctrl wq_ctrl;
	struct	mlx5e_priv *priv;
	int	tc;
} __aligned(MLX5E_CACHELINE_SIZE);

static inline bool
mlx5e_sq_has_room_for(struct mlx5e_sq *sq, u16 n)
{
	u16 cc = sq->cc;
	u16 pc = sq->pc;

	return ((sq->wq.sz_m1 & (cc - pc)) >= n || cc == pc);
}

static inline u32
mlx5e_sq_queue_level(struct mlx5e_sq *sq)
{
	u16 cc;
	u16 pc;

	if (sq == NULL)
		return (0);

	cc = sq->cc;
	pc = sq->pc;

	return (((sq->wq.sz_m1 & (pc - cc)) *
	    IF_SND_QUEUE_LEVEL_MAX) / sq->wq.sz_m1);
}

struct mlx5e_channel {
	/* data path */
	struct mlx5e_rq rq;
	struct mlx5e_snd_tag tag;
	struct mlx5e_sq sq[MLX5E_MAX_TX_NUM_TC];
	u32	mkey_be;
	u8	num_tc;

	/* control */
	struct mlx5e_priv *priv;
	int	ix;
	int	cpu;
} __aligned(MLX5E_CACHELINE_SIZE);

enum mlx5e_traffic_types {
	MLX5E_TT_IPV4_TCP,
	MLX5E_TT_IPV6_TCP,
	MLX5E_TT_IPV4_UDP,
	MLX5E_TT_IPV6_UDP,
	MLX5E_TT_IPV4_IPSEC_AH,
	MLX5E_TT_IPV6_IPSEC_AH,
	MLX5E_TT_IPV4_IPSEC_ESP,
	MLX5E_TT_IPV6_IPSEC_ESP,
	MLX5E_TT_IPV4,
	MLX5E_TT_IPV6,
	MLX5E_TT_ANY,
	MLX5E_NUM_TT,
};

enum {
	MLX5E_RQT_SPREADING = 0,
	MLX5E_RQT_DEFAULT_RQ = 1,
	MLX5E_NUM_RQT = 2,
};

struct mlx5_flow_rule;

struct mlx5e_eth_addr_info {
	u8	addr [ETH_ALEN + 2];
	u32	tt_vec;
	/* flow table rule per traffic type */
	struct mlx5_flow_rule	*ft_rule[MLX5E_NUM_TT];
};

#define	MLX5E_ETH_ADDR_HASH_SIZE (1 << BITS_PER_BYTE)

struct mlx5e_eth_addr_hash_node;

struct mlx5e_eth_addr_hash_head {
	struct mlx5e_eth_addr_hash_node *lh_first;
};

struct mlx5e_eth_addr_db {
	struct mlx5e_eth_addr_hash_head if_uc[MLX5E_ETH_ADDR_HASH_SIZE];
	struct mlx5e_eth_addr_hash_head if_mc[MLX5E_ETH_ADDR_HASH_SIZE];
	struct mlx5e_eth_addr_info broadcast;
	struct mlx5e_eth_addr_info allmulti;
	struct mlx5e_eth_addr_info promisc;
	bool	broadcast_enabled;
	bool	allmulti_enabled;
	bool	promisc_enabled;
};

enum {
	MLX5E_STATE_ASYNC_EVENTS_ENABLE,
	MLX5E_STATE_OPENED,
};

enum {
	MLX5_BW_NO_LIMIT   = 0,
	MLX5_100_MBPS_UNIT = 3,
	MLX5_GBPS_UNIT     = 4,
};

struct mlx5e_vlan_db {
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];
	struct mlx5_flow_rule	*active_vlans_ft_rule[VLAN_N_VID];
	struct mlx5_flow_rule	*untagged_ft_rule;
	struct mlx5_flow_rule	*any_cvlan_ft_rule;
	struct mlx5_flow_rule	*any_svlan_ft_rule;
	bool	filter_disabled;
};

struct mlx5e_flow_table {
	int num_groups;
	struct mlx5_flow_table *t;
	struct mlx5_flow_group **g;
};

struct mlx5e_flow_tables {
	struct mlx5_flow_namespace *ns;
	struct mlx5e_flow_table vlan;
	struct mlx5e_flow_table main;
	struct mlx5e_flow_table inner_rss;
};

#ifdef RATELIMIT
#include "en_rl.h"
#endif

#define	MLX5E_TSTMP_PREC 10

struct mlx5e_clbr_point {
	uint64_t base_curr;
	uint64_t base_prev;
	uint64_t clbr_hw_prev;
	uint64_t clbr_hw_curr;
	u_int clbr_gen;
};

struct mlx5e_priv {
	struct mlx5_core_dev *mdev;     /* must be first */

	/* priv data path fields - start */
	int	order_base_2_num_channels;
	int	queue_mapping_channel_mask;
	int	num_tc;
	int	default_vlan_prio;
	/* priv data path fields - end */

	unsigned long state;
	int	gone;
#define	PRIV_LOCK(priv) sx_xlock(&(priv)->state_lock)
#define	PRIV_UNLOCK(priv) sx_xunlock(&(priv)->state_lock)
#define	PRIV_LOCKED(priv) sx_xlocked(&(priv)->state_lock)
	struct sx state_lock;		/* Protects Interface state */
	struct mlx5_uar cq_uar;
	u32	pdn;
	u32	tdn;
	struct mlx5_core_mr mr;
	volatile unsigned int channel_refs;

	u32	tisn[MLX5E_MAX_TX_NUM_TC];
	u32	rqtn;
	u32	tirn[MLX5E_NUM_TT];

	struct mlx5e_flow_tables fts;
	struct mlx5e_eth_addr_db eth_addr;
	struct mlx5e_vlan_db vlan;

	struct mlx5e_params params;
	struct mlx5e_params_ethtool params_ethtool;
	union mlx5_core_pci_diagnostics params_pci;
	union mlx5_core_general_diagnostics params_general;
	struct mtx async_events_mtx;	/* sync hw events */
	struct work_struct update_stats_work;
	struct work_struct update_carrier_work;
	struct work_struct set_rx_mode_work;
	MLX5_DECLARE_DOORBELL_LOCK(doorbell_lock)

	struct ifnet *ifp;
	struct sysctl_ctx_list sysctl_ctx;
	struct sysctl_oid *sysctl_ifnet;
	struct sysctl_oid *sysctl_hw;
	int	sysctl_debug;
	struct mlx5e_stats stats;
	int	counter_set_id;

	struct workqueue_struct *wq;

	eventhandler_tag vlan_detach;
	eventhandler_tag vlan_attach;
	struct ifmedia media;
	int	media_status_last;
	int	media_active_last;

	struct callout watchdog;
#ifdef RATELIMIT
	struct mlx5e_rl_priv_data rl;
#endif

	struct callout tstmp_clbr;
	int	clbr_done;
	int	clbr_curr;
	struct mlx5e_clbr_point clbr_points[2];
	u_int	clbr_gen;

	struct mlx5e_channel channel[];
};

#define	MLX5E_NET_IP_ALIGN 2

struct mlx5e_tx_wqe {
	struct mlx5_wqe_ctrl_seg ctrl;
	struct mlx5_wqe_eth_seg eth;
};

struct mlx5e_rx_wqe {
	struct mlx5_wqe_srq_next_seg next;
	struct mlx5_wqe_data_seg data[];
};

/* the size of the structure above must be power of two */
CTASSERT(powerof2(sizeof(struct mlx5e_rx_wqe)));

struct mlx5e_eeprom {
	int	lock_bit;
	int	i2c_addr;
	int	page_num;
	int	device_addr;
	int	module_num;
	int	len;
	int	type;
	int	page_valid;
	u32	*data;
};

#define	MLX5E_FLD_MAX(typ, fld) ((1ULL << __mlx5_bit_sz(typ, fld)) - 1ULL)

int	mlx5e_xmit(struct ifnet *, struct mbuf *);

int	mlx5e_open_locked(struct ifnet *);
int	mlx5e_close_locked(struct ifnet *);

void	mlx5e_cq_error_event(struct mlx5_core_cq *mcq, int event);
void	mlx5e_rx_cq_comp(struct mlx5_core_cq *);
void	mlx5e_tx_cq_comp(struct mlx5_core_cq *);
struct mlx5_cqe64 *mlx5e_get_cqe(struct mlx5e_cq *cq);

int	mlx5e_open_flow_table(struct mlx5e_priv *priv);
void	mlx5e_close_flow_table(struct mlx5e_priv *priv);
void	mlx5e_set_rx_mode_core(struct mlx5e_priv *priv);
void	mlx5e_set_rx_mode_work(struct work_struct *work);

void	mlx5e_vlan_rx_add_vid(void *, struct ifnet *, u16);
void	mlx5e_vlan_rx_kill_vid(void *, struct ifnet *, u16);
void	mlx5e_enable_vlan_filter(struct mlx5e_priv *priv);
void	mlx5e_disable_vlan_filter(struct mlx5e_priv *priv);
int	mlx5e_add_all_vlan_rules(struct mlx5e_priv *priv);
void	mlx5e_del_all_vlan_rules(struct mlx5e_priv *priv);

static inline void
mlx5e_tx_notify_hw(struct mlx5e_sq *sq, u32 *wqe, int bf_sz)
{
	u16 ofst = MLX5_BF_OFFSET + sq->bf_offset;

	/* ensure wqe is visible to device before updating doorbell record */
	wmb();

	*sq->wq.db = cpu_to_be32(sq->pc);

	/*
	 * Ensure the doorbell record is visible to device before ringing
	 * the doorbell:
	 */
	wmb();

	if (bf_sz) {
		__iowrite64_copy(sq->uar.bf_map + ofst, wqe, bf_sz);

		/* flush the write-combining mapped buffer */
		wmb();

	} else {
		mlx5_write64(wqe, sq->uar.map + ofst,
		    MLX5_GET_DOORBELL_LOCK(&sq->priv->doorbell_lock));
	}

	sq->bf_offset ^= sq->bf_buf_size;
}

static inline void
mlx5e_cq_arm(struct mlx5e_cq *cq, spinlock_t *dblock)
{
	struct mlx5_core_cq *mcq;

	mcq = &cq->mcq;
	mlx5_cq_arm(mcq, MLX5_CQ_DB_REQ_NOT, mcq->uar->map, dblock, cq->wq.cc);
}

static inline void
mlx5e_ref_channel(struct mlx5e_priv *priv)
{

	KASSERT(priv->channel_refs < INT_MAX,
	    ("Channel refs will overflow"));
	atomic_fetchadd_int(&priv->channel_refs, 1);
}

static inline void
mlx5e_unref_channel(struct mlx5e_priv *priv)
{

	KASSERT(priv->channel_refs > 0,
	    ("Channel refs is not greater than zero"));
	atomic_fetchadd_int(&priv->channel_refs, -1);
}

extern const struct ethtool_ops mlx5e_ethtool_ops;
void	mlx5e_create_ethtool(struct mlx5e_priv *);
void	mlx5e_create_stats(struct sysctl_ctx_list *,
    struct sysctl_oid_list *, const char *,
    const char **, unsigned, u64 *);
void	mlx5e_send_nop(struct mlx5e_sq *, u32);
void	mlx5e_sq_cev_timeout(void *);
int	mlx5e_refresh_channel_params(struct mlx5e_priv *);
int	mlx5e_open_cq(struct mlx5e_priv *, struct mlx5e_cq_param *,
    struct mlx5e_cq *, mlx5e_cq_comp_t *, int eq_ix);
void	mlx5e_close_cq(struct mlx5e_cq *);
void	mlx5e_free_sq_db(struct mlx5e_sq *);
int	mlx5e_alloc_sq_db(struct mlx5e_sq *);
int	mlx5e_enable_sq(struct mlx5e_sq *, struct mlx5e_sq_param *, int tis_num);
int	mlx5e_modify_sq(struct mlx5e_sq *, int curr_state, int next_state);
void	mlx5e_disable_sq(struct mlx5e_sq *);
void	mlx5e_drain_sq(struct mlx5e_sq *);
void	mlx5e_modify_tx_dma(struct mlx5e_priv *priv, uint8_t value);
void	mlx5e_modify_rx_dma(struct mlx5e_priv *priv, uint8_t value);
void	mlx5e_resume_sq(struct mlx5e_sq *sq);
void	mlx5e_update_sq_inline(struct mlx5e_sq *sq);
void	mlx5e_refresh_sq_inline(struct mlx5e_priv *priv);

#endif					/* _MLX5_EN_H_ */
