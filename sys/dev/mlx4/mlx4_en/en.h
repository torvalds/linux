/*
 * Copyright (c) 2007, 2014 Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef _MLX4_EN_H_
#define _MLX4_EN_H_

#include <linux/bitops.h>
#include <linux/compiler.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/kobject.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>
#include <linux/if_ether.h>
#ifdef CONFIG_MLX4_EN_DCB
#include <linux/dcbnl.h>
#endif

#include <dev/mlx4/device.h>
#include <dev/mlx4/qp.h>
#include <dev/mlx4/cq.h>
#include <dev/mlx4/srq.h>
#include <dev/mlx4/doorbell.h>
#include <dev/mlx4/cmd.h>

#include <netinet/tcp_lro.h>
#include <netinet/netdump/netdump.h>

#include "en_port.h"
#include <dev/mlx4/stats.h>

#define DRV_NAME	"mlx4_en"

#define MLX4_EN_MSG_LEVEL	(NETIF_MSG_LINK | NETIF_MSG_IFDOWN)

/*
 * Device constants
 */


#define MLX4_EN_PAGE_SHIFT	12
#define MLX4_EN_PAGE_SIZE	(1 << MLX4_EN_PAGE_SHIFT)
#define	MLX4_NET_IP_ALIGN	2	/* bytes */
#define DEF_RX_RINGS		16
#define MAX_RX_RINGS		128
#define MIN_RX_RINGS		4
#define TXBB_SIZE		64

#ifndef MLX4_EN_MAX_RX_SEGS
#define	MLX4_EN_MAX_RX_SEGS 1	/* or 8 */
#endif

#ifndef MLX4_EN_MAX_RX_BYTES
#define	MLX4_EN_MAX_RX_BYTES MCLBYTES
#endif

#define HEADROOM		(2048 / TXBB_SIZE + 1)
#define INIT_OWNER_BIT		0xffffffff
#define STAMP_STRIDE		64
#define STAMP_DWORDS		(STAMP_STRIDE / 4)
#define STAMP_SHIFT		31
#define STAMP_VAL		0x7fffffff
#define STATS_DELAY		(HZ / 4)
#define SERVICE_TASK_DELAY	(HZ / 4)
#define MAX_NUM_OF_FS_RULES	256

#define MLX4_EN_FILTER_HASH_SHIFT 4
#define MLX4_EN_FILTER_EXPIRY_QUOTA 60

#ifdef CONFIG_NET_RX_BUSY_POLL
#define LL_EXTENDED_STATS
#endif

/* vlan valid range */
#define VLAN_MIN_VALUE		1
#define VLAN_MAX_VALUE		4094

/*
 * OS related constants and tunables
 */

#define MLX4_EN_WATCHDOG_TIMEOUT	(15 * HZ)

#define MLX4_EN_ALLOC_SIZE     PAGE_ALIGN(PAGE_SIZE)
#define MLX4_EN_ALLOC_ORDER    get_order(MLX4_EN_ALLOC_SIZE)

enum mlx4_en_alloc_type {
	MLX4_EN_ALLOC_NEW = 0,
	MLX4_EN_ALLOC_REPLACEMENT = 1,
};

/* Maximum ring sizes */
#define MLX4_EN_DEF_TX_QUEUE_SIZE       4096

/* Minimum packet number till arming the CQ */
#define MLX4_EN_MIN_RX_ARM	2048
#define MLX4_EN_MIN_TX_ARM	2048

/* Maximum ring sizes */
#define MLX4_EN_MAX_TX_SIZE	8192
#define MLX4_EN_MAX_RX_SIZE	8192

/* Minimum ring sizes */
#define MLX4_EN_MIN_RX_SIZE	(4096 / TXBB_SIZE)
#define MLX4_EN_MIN_TX_SIZE	(4096 / TXBB_SIZE)

#define MLX4_EN_SMALL_PKT_SIZE		64

#define MLX4_EN_MAX_TX_RING_P_UP	32
#define MLX4_EN_NUM_UP			1

#define MAX_TX_RINGS			(MLX4_EN_MAX_TX_RING_P_UP * \
					MLX4_EN_NUM_UP)

#define MLX4_EN_NO_VLAN			0xffff

#define MLX4_EN_DEF_TX_RING_SIZE	1024
#define MLX4_EN_DEF_RX_RING_SIZE  	1024

/* Target number of bytes to coalesce with interrupt moderation */
#define MLX4_EN_RX_COAL_TARGET	44
#define MLX4_EN_RX_COAL_TIME	0x10

#define MLX4_EN_TX_COAL_PKTS	64
#define MLX4_EN_TX_COAL_TIME	64

#define MLX4_EN_RX_RATE_LOW		400000
#define MLX4_EN_RX_COAL_TIME_LOW	0
#define MLX4_EN_RX_RATE_HIGH		450000
#define MLX4_EN_RX_COAL_TIME_HIGH	128
#define MLX4_EN_RX_SIZE_THRESH		1024
#define MLX4_EN_RX_RATE_THRESH		(1000000 / MLX4_EN_RX_COAL_TIME_HIGH)
#define MLX4_EN_SAMPLE_INTERVAL		0
#define MLX4_EN_AVG_PKT_SMALL		256

#define MLX4_EN_AUTO_CONF	0xffff

#define MLX4_EN_DEF_RX_PAUSE	1
#define MLX4_EN_DEF_TX_PAUSE	1

/* Interval between successive polls in the Tx routine when polling is used
   instead of interrupts (in per-core Tx rings) - should be power of 2 */
#define MLX4_EN_TX_POLL_MODER	16
#define MLX4_EN_TX_POLL_TIMEOUT	(HZ / 4)

#define MLX4_EN_64_ALIGN	(64 - NET_SKB_PAD)
#define SMALL_PACKET_SIZE      (256 - NET_IP_ALIGN)
#define HEADER_COPY_SIZE       (128)
#define MLX4_LOOPBACK_TEST_PAYLOAD (HEADER_COPY_SIZE - ETHER_HDR_LEN)

#define MLX4_EN_MIN_MTU		46
#define ETH_BCAST		0xffffffffffffULL

#define MLX4_EN_LOOPBACK_RETRIES	5
#define MLX4_EN_LOOPBACK_TIMEOUT	100

#ifdef MLX4_EN_PERF_STAT
/* Number of samples to 'average' */
#define AVG_SIZE			128
#define AVG_FACTOR			1024

#define INC_PERF_COUNTER(cnt)		(++(cnt))
#define ADD_PERF_COUNTER(cnt, add)	((cnt) += (add))
#define AVG_PERF_COUNTER(cnt, sample) \
	((cnt) = ((cnt) * (AVG_SIZE - 1) + (sample) * AVG_FACTOR) / AVG_SIZE)
#define GET_PERF_COUNTER(cnt)		(cnt)
#define GET_AVG_PERF_COUNTER(cnt)	((cnt) / AVG_FACTOR)

#else

#define INC_PERF_COUNTER(cnt)		do {} while (0)
#define ADD_PERF_COUNTER(cnt, add)	do {} while (0)
#define AVG_PERF_COUNTER(cnt, sample)	do {} while (0)
#define GET_PERF_COUNTER(cnt)		(0)
#define GET_AVG_PERF_COUNTER(cnt)	(0)
#endif /* MLX4_EN_PERF_STAT */

/* Constants for TX flow */
enum {
	MAX_INLINE = 104, /* 128 - 16 - 4 - 4 */
	MAX_BF = 256,
	MIN_PKT_LEN = 17,
};

/*
 * Configurables
 */

enum cq_type {
	RX = 0,
	TX = 1,
};


/*
 * Useful macros
 */
#define ROUNDUP_LOG2(x)		ilog2(roundup_pow_of_two(x))
#define XNOR(x, y)		(!(x) == !(y))
#define ILLEGAL_MAC(addr)	(addr == 0xffffffffffffULL || addr == 0x0)

struct mlx4_en_tx_info {
	bus_dmamap_t dma_map;
        struct mbuf *mb;
        u32 nr_txbb;
	u32 nr_bytes;
};


#define MLX4_EN_BIT_DESC_OWN	0x80000000
#define CTRL_SIZE	sizeof(struct mlx4_wqe_ctrl_seg)
#define MLX4_EN_MEMTYPE_PAD	0x100
#define DS_SIZE		sizeof(struct mlx4_wqe_data_seg)


struct mlx4_en_tx_desc {
	struct mlx4_wqe_ctrl_seg ctrl;
	union {
		struct mlx4_wqe_data_seg data; /* at least one data segment */
		struct mlx4_wqe_lso_seg lso;
		struct mlx4_wqe_inline_seg inl;
	};
};

#define MLX4_EN_USE_SRQ		0x01000000

#define MLX4_EN_RX_BUDGET 64

#define	MLX4_EN_TX_MAX_DESC_SIZE 512	/* bytes */
#define	MLX4_EN_TX_MAX_MBUF_SIZE 65536	/* bytes */
#define	MLX4_EN_TX_MAX_PAYLOAD_SIZE 65536	/* bytes */
#define	MLX4_EN_TX_MAX_MBUF_FRAGS \
    ((MLX4_EN_TX_MAX_DESC_SIZE - 128) / DS_SIZE_ALIGNMENT) /* units */
#define	MLX4_EN_TX_WQE_MAX_WQEBBS			\
    (MLX4_EN_TX_MAX_DESC_SIZE / TXBB_SIZE) /* units */

#define MLX4_EN_CX3_LOW_ID	0x1000
#define MLX4_EN_CX3_HIGH_ID	0x1005

struct mlx4_en_tx_ring {
        spinlock_t tx_lock;
	bus_dma_tag_t dma_tag;
	struct mlx4_hwq_resources wqres;
	u32 size ; /* number of TXBBs */
	u32 size_mask;
	u16 stride;
	u16 cqn;	/* index of port CQ associated with this ring */
	u32 prod;
	u32 cons;
	u32 buf_size;
	u32 doorbell_qpn;
	u8 *buf;
	u16 poll_cnt;
	struct mlx4_en_tx_info *tx_info;
	u8 queue_index;
	u32 last_nr_txbb;
	struct mlx4_qp qp;
	struct mlx4_qp_context context;
	int qpn;
	enum mlx4_qp_state qp_state;
	struct mlx4_srq dummy;
	u64 bytes;
	u64 packets;
	u64 tx_csum;
	u64 queue_stopped;
	u64 oversized_packets;
	u64 wake_queue;
	u64 tso_packets;
	u64 defrag_attempts;
	struct mlx4_bf bf;
	bool bf_enabled;
	int hwtstamp_tx_type;
	spinlock_t comp_lock;
	int inline_thold;
	u64 watchdog_time;
};

struct mlx4_en_rx_desc {
	struct mlx4_wqe_data_seg data[MLX4_EN_MAX_RX_SEGS];
};

/* the size of the structure above must be power of two */
CTASSERT(powerof2(sizeof(struct mlx4_en_rx_desc)));

struct mlx4_en_rx_mbuf {
	bus_dmamap_t dma_map;
	struct mbuf *mbuf;
};

struct mlx4_en_rx_spare {
	bus_dmamap_t dma_map;
	struct mbuf *mbuf;
	bus_dma_segment_t segs[MLX4_EN_MAX_RX_SEGS];
};

struct mlx4_en_rx_ring {
	struct mlx4_hwq_resources wqres;
	bus_dma_tag_t dma_tag;
	struct mlx4_en_rx_spare spare;
	u32 size ;	/* number of Rx descs*/
	u32 actual_size;
	u32 size_mask;
	u16 log_stride;
	u16 cqn;	/* index of port CQ associated with this ring */
	u32 prod;
	u32 cons;
	u32 buf_size;
	u8  fcs_del;
	u32 rx_mb_size;
	u32 rx_mr_key_be;
	int qpn;
	u8 *buf;
	struct mlx4_en_rx_mbuf *mbuf;
	u64 errors;
	u64 bytes;
	u64 packets;
#ifdef LL_EXTENDED_STATS
	u64 yields;
	u64 misses;
	u64 cleaned;
#endif
	u64 csum_ok;
	u64 csum_none;
	int hwtstamp_rx_filter;
	int numa_node;
	struct lro_ctrl lro;
};

static inline int mlx4_en_can_lro(__be16 status)
{
	const __be16 status_all = cpu_to_be16(
			MLX4_CQE_STATUS_IPV4    |
			MLX4_CQE_STATUS_IPV4F   |
			MLX4_CQE_STATUS_IPV6    |
			MLX4_CQE_STATUS_IPV4OPT |
			MLX4_CQE_STATUS_TCP     |
			MLX4_CQE_STATUS_UDP     |
			MLX4_CQE_STATUS_IPOK);
	const __be16 status_ipv4_ipok_tcp = cpu_to_be16(
			MLX4_CQE_STATUS_IPV4    |
			MLX4_CQE_STATUS_IPOK    |
			MLX4_CQE_STATUS_TCP);
	const __be16 status_ipv6_ipok_tcp = cpu_to_be16(
			MLX4_CQE_STATUS_IPV6    |
			MLX4_CQE_STATUS_IPOK    |
			MLX4_CQE_STATUS_TCP);

	status &= status_all;
	return (status == status_ipv4_ipok_tcp ||
			status == status_ipv6_ipok_tcp);
}

struct mlx4_en_cq {
	struct mlx4_cq          mcq;
	struct mlx4_hwq_resources wqres;
	int                     ring;
	spinlock_t              lock;
	struct net_device      *dev;
        /* Per-core Tx cq processing support */
        struct timer_list timer;
	int size;
	int buf_size;
	unsigned vector;
	enum cq_type is_tx;
	u16 moder_time;
	u16 moder_cnt;
	struct mlx4_cqe *buf;
	struct task cq_task;
	struct taskqueue *tq;
#define MLX4_EN_OPCODE_ERROR	0x1e
	u32 tot_rx;
	u32 tot_tx;
	u32 curr_poll_rx_cpu_id;

#ifdef CONFIG_NET_RX_BUSY_POLL
	unsigned int state;
#define MLX4_EN_CQ_STATE_IDLE        0
#define MLX4_EN_CQ_STATE_NAPI     1    /* NAPI owns this CQ */
#define MLX4_EN_CQ_STATE_POLL     2    /* poll owns this CQ */
#define MLX4_CQ_LOCKED (MLX4_EN_CQ_STATE_NAPI | MLX4_EN_CQ_STATE_POLL)
#define MLX4_EN_CQ_STATE_NAPI_YIELD  4    /* NAPI yielded this CQ */
#define MLX4_EN_CQ_STATE_POLL_YIELD  8    /* poll yielded this CQ */
#define CQ_YIELD (MLX4_EN_CQ_STATE_NAPI_YIELD | MLX4_EN_CQ_STATE_POLL_YIELD)
#define CQ_USER_PEND (MLX4_EN_CQ_STATE_POLL | MLX4_EN_CQ_STATE_POLL_YIELD)
	spinlock_t poll_lock; /* protects from LLS/napi conflicts */
#endif  /* CONFIG_NET_RX_BUSY_POLL */
};

struct mlx4_en_port_profile {
	u32 flags;
	u32 tx_ring_num;
	u32 rx_ring_num;
	u32 tx_ring_size;
	u32 rx_ring_size;
	u8 rx_pause;
	u8 rx_ppp;
	u8 tx_pause;
	u8 tx_ppp;
	int rss_rings;
	int inline_thold;
};

struct mlx4_en_profile {
	int rss_xor;
	int udp_rss;
	u8 rss_mask;
	u32 active_ports;
	u32 small_pkt_int;
	u8 no_reset;
	u8 num_tx_rings_p_up;
	struct mlx4_en_port_profile prof[MLX4_MAX_PORTS + 1];
};

struct mlx4_en_dev {
	struct mlx4_dev		*dev;
	struct pci_dev		*pdev;
	struct mutex		state_lock;
	struct net_device	*pndev[MLX4_MAX_PORTS + 1];
	u32			port_cnt;
	bool			device_up;
	struct mlx4_en_profile	profile;
	u32			LSO_support;
	struct workqueue_struct *workqueue;
	struct device		*dma_device;
	void __iomem		*uar_map;
	struct mlx4_uar		priv_uar;
	struct mlx4_mr		mr;
	u32			priv_pdn;
	spinlock_t		uar_lock;
	u8			mac_removed[MLX4_MAX_PORTS + 1];
	unsigned long		last_overflow_check;
	unsigned long		overflow_period;
};


struct mlx4_en_rss_map {
	int base_qpn;
	struct mlx4_qp qps[MAX_RX_RINGS];
	enum mlx4_qp_state state[MAX_RX_RINGS];
	struct mlx4_qp indir_qp;
	enum mlx4_qp_state indir_state;
};

enum mlx4_en_port_flag {
	MLX4_EN_PORT_ANC = 1<<0, /* Auto-negotiation complete */
	MLX4_EN_PORT_ANE = 1<<1, /* Auto-negotiation enabled */
};

struct mlx4_en_port_state {
	int link_state;
	int link_speed;
	int transceiver;
	u32 flags;
};

enum mlx4_en_addr_list_act {
	MLX4_ADDR_LIST_NONE,
	MLX4_ADDR_LIST_REM,
	MLX4_ADDR_LIST_ADD,
};

struct mlx4_en_addr_list {
	struct list_head	list;
	enum mlx4_en_addr_list_act	action;
	u8			addr[ETH_ALEN];
	u64			reg_id;
	u64			tunnel_reg_id;
};

#ifdef CONFIG_MLX4_EN_DCB
/* Minimal TC BW - setting to 0 will block traffic */
#define MLX4_EN_BW_MIN 1
#define MLX4_EN_BW_MAX 100 /* Utilize 100% of the line */

#define MLX4_EN_TC_VENDOR 0
#define MLX4_EN_TC_ETS 7

#endif


enum {
	MLX4_EN_FLAG_PROMISC		= (1 << 0),
	MLX4_EN_FLAG_MC_PROMISC		= (1 << 1),
	/* whether we need to enable hardware loopback by putting dmac
	 * in Tx WQE
	 */
	MLX4_EN_FLAG_ENABLE_HW_LOOPBACK	= (1 << 2),
	/* whether we need to drop packets that hardware loopback-ed */
	MLX4_EN_FLAG_RX_FILTER_NEEDED	= (1 << 3),
	MLX4_EN_FLAG_FORCE_PROMISC	= (1 << 4),
#ifdef CONFIG_MLX4_EN_DCB
	MLX4_EN_FLAG_DCB_ENABLED	= (1 << 5)
#endif
};

#define MLX4_EN_MAC_HASH_SIZE (1 << BITS_PER_BYTE)
#define MLX4_EN_MAC_HASH_IDX 5

struct en_port {
	struct kobject		kobj;
	struct mlx4_dev		*dev;
	u8			port_num;
	u8			vport_num;
};

struct mlx4_en_priv {
	struct mlx4_en_dev *mdev;
	struct mlx4_en_port_profile *prof;
	struct net_device *dev;
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];
	struct mlx4_en_port_state port_state;
	spinlock_t stats_lock;
	/* To allow rules removal while port is going down */
	struct list_head ethtool_list;

	unsigned long last_moder_packets[MAX_RX_RINGS];
	unsigned long last_moder_tx_packets;
	unsigned long last_moder_bytes[MAX_RX_RINGS];
	unsigned long last_moder_jiffies;
	int last_moder_time[MAX_RX_RINGS];
	u16 rx_usecs;
	u16 rx_frames;
	u16 tx_usecs;
	u16 tx_frames;
	u32 pkt_rate_low;
	u32 rx_usecs_low;
	u32 pkt_rate_high;
	u32 rx_usecs_high;
	u32 sample_interval;
	u32 adaptive_rx_coal;
	u32 msg_enable;
	u32 loopback_ok;
	u32 validate_loopback;

	struct mlx4_hwq_resources res;
	int link_state;
	int last_link_state;
	bool port_up;
	int port;
	int registered;
	int gone;
	int allocated;
	unsigned char current_mac[ETH_ALEN + 2];
        u64 mac;
	int mac_index;
	unsigned max_mtu;
	int base_qpn;
	int cqe_factor;

	struct mlx4_en_rss_map rss_map;
	u32 flags;
	u8 num_tx_rings_p_up;
	u32 tx_ring_num;
	u32 rx_ring_num;
	u32 rx_mb_size;

	struct mlx4_en_tx_ring **tx_ring;
	struct mlx4_en_rx_ring *rx_ring[MAX_RX_RINGS];
	struct mlx4_en_cq **tx_cq;
	struct mlx4_en_cq *rx_cq[MAX_RX_RINGS];
	struct mlx4_qp drop_qp;
	struct work_struct rx_mode_task;
	struct work_struct watchdog_task;
	struct work_struct linkstate_task;
	struct delayed_work stats_task;
	struct delayed_work service_task;
	struct mlx4_en_perf_stats pstats;
	struct mlx4_en_pkt_stats pkstats;
	struct mlx4_en_pkt_stats pkstats_last;
	struct mlx4_en_flow_stats_rx rx_priority_flowstats[MLX4_NUM_PRIORITIES];
	struct mlx4_en_flow_stats_tx tx_priority_flowstats[MLX4_NUM_PRIORITIES];
	struct mlx4_en_flow_stats_rx rx_flowstats;
	struct mlx4_en_flow_stats_tx tx_flowstats;
	struct mlx4_en_port_stats port_stats;
	struct mlx4_en_vport_stats vport_stats;
	struct mlx4_en_vf_stats vf_stats;
	struct list_head mc_list;
	struct list_head uc_list;
	struct list_head curr_mc_list;
	struct list_head curr_uc_list;
	u64 broadcast_id;
	struct mlx4_en_stat_out_mbox hw_stats;
	int vids[128];
	bool wol;
	struct device *ddev;
	struct dentry *dev_root;
	u32 counter_index;
	eventhandler_tag vlan_attach;
	eventhandler_tag vlan_detach;
	struct callout watchdog_timer;
        struct ifmedia media;
	volatile int blocked;
	struct sysctl_oid *conf_sysctl;
	struct sysctl_oid *stat_sysctl;
	struct sysctl_ctx_list conf_ctx;
	struct sysctl_ctx_list stat_ctx;

#ifdef CONFIG_MLX4_EN_DCB
	struct ieee_ets ets;
	u16 maxrate[IEEE_8021QAZ_MAX_TCS];
	u8 dcbx_cap;
#endif
#ifdef CONFIG_RFS_ACCEL
	spinlock_t filters_lock;
	int last_filter_id;
	struct list_head filters;
	struct hlist_head filter_hash[1 << MLX4_EN_FILTER_HASH_SHIFT];
#endif
	u64 tunnel_reg_id;
	struct en_port *vf_ports[MLX4_MAX_NUM_VF];
	unsigned long last_ifq_jiffies;
	u64 if_counters_rx_errors;
	u64 if_counters_rx_no_buffer;
};

enum mlx4_en_wol {
	MLX4_EN_WOL_MAGIC = (1ULL << 61),
	MLX4_EN_WOL_ENABLED = (1ULL << 62),
};

struct mlx4_mac_entry {
	struct hlist_node hlist;
	unsigned char mac[ETH_ALEN + 2];
	u64 reg_id;
};

static inline struct mlx4_cqe *mlx4_en_get_cqe(u8 *buf, int idx, int cqe_sz)
{
	return (struct mlx4_cqe *)(buf + idx * cqe_sz);
}

#ifdef CONFIG_NET_RX_BUSY_POLL
static inline void mlx4_en_cq_init_lock(struct mlx4_en_cq *cq)
{
	spin_lock_init(&cq->poll_lock);
	cq->state = MLX4_EN_CQ_STATE_IDLE;
}

/* called from the device poll rutine to get ownership of a cq */
static inline bool mlx4_en_cq_lock_napi(struct mlx4_en_cq *cq)
{
	int rc = true;
	spin_lock(&cq->poll_lock);
	if (cq->state & MLX4_CQ_LOCKED) {
		WARN_ON(cq->state & MLX4_EN_CQ_STATE_NAPI);
		cq->state |= MLX4_EN_CQ_STATE_NAPI_YIELD;
		rc = false;
	} else
		/* we don't care if someone yielded */
		cq->state = MLX4_EN_CQ_STATE_NAPI;
	spin_unlock(&cq->poll_lock);
	return rc;
}

/* returns true is someone tried to get the cq while napi had it */
static inline bool mlx4_en_cq_unlock_napi(struct mlx4_en_cq *cq)
{
	int rc = false;
	spin_lock(&cq->poll_lock);
	WARN_ON(cq->state & (MLX4_EN_CQ_STATE_POLL |
			       MLX4_EN_CQ_STATE_NAPI_YIELD));

	if (cq->state & MLX4_EN_CQ_STATE_POLL_YIELD)
		rc = true;
	cq->state = MLX4_EN_CQ_STATE_IDLE;
	spin_unlock(&cq->poll_lock);
	return rc;
}

/* called from mlx4_en_low_latency_poll() */
static inline bool mlx4_en_cq_lock_poll(struct mlx4_en_cq *cq)
{
	int rc = true;
	spin_lock_bh(&cq->poll_lock);
	if ((cq->state & MLX4_CQ_LOCKED)) {
		struct net_device *dev = cq->dev;
		struct mlx4_en_priv *priv = netdev_priv(dev);
		struct mlx4_en_rx_ring *rx_ring = priv->rx_ring[cq->ring];

		cq->state |= MLX4_EN_CQ_STATE_POLL_YIELD;
		rc = false;
#ifdef LL_EXTENDED_STATS
		rx_ring->yields++;
#endif
	} else
		/* preserve yield marks */
		cq->state |= MLX4_EN_CQ_STATE_POLL;
	spin_unlock_bh(&cq->poll_lock);
	return rc;
}

/* returns true if someone tried to get the cq while it was locked */
static inline bool mlx4_en_cq_unlock_poll(struct mlx4_en_cq *cq)
{
	int rc = false;
	spin_lock_bh(&cq->poll_lock);
	WARN_ON(cq->state & (MLX4_EN_CQ_STATE_NAPI));

	if (cq->state & MLX4_EN_CQ_STATE_POLL_YIELD)
		rc = true;
	cq->state = MLX4_EN_CQ_STATE_IDLE;
	spin_unlock_bh(&cq->poll_lock);
	return rc;
}

/* true if a socket is polling, even if it did not get the lock */
static inline bool mlx4_en_cq_busy_polling(struct mlx4_en_cq *cq)
{
	WARN_ON(!(cq->state & MLX4_CQ_LOCKED));
	return cq->state & CQ_USER_PEND;
}
#else
static inline void mlx4_en_cq_init_lock(struct mlx4_en_cq *cq)
{
}

static inline bool mlx4_en_cq_lock_napi(struct mlx4_en_cq *cq)
{
	return true;
}

static inline bool mlx4_en_cq_unlock_napi(struct mlx4_en_cq *cq)
{
	return false;
}

static inline bool mlx4_en_cq_lock_poll(struct mlx4_en_cq *cq)
{
	return false;
}

static inline bool mlx4_en_cq_unlock_poll(struct mlx4_en_cq *cq)
{
	return false;
}

static inline bool mlx4_en_cq_busy_polling(struct mlx4_en_cq *cq)
{
	return false;
}
#endif /* CONFIG_NET_RX_BUSY_POLL */

#define MLX4_EN_WOL_DO_MODIFY (1ULL << 63)

void mlx4_en_destroy_netdev(struct net_device *dev);
int mlx4_en_init_netdev(struct mlx4_en_dev *mdev, int port,
			struct mlx4_en_port_profile *prof);

int mlx4_en_start_port(struct net_device *dev);
void mlx4_en_stop_port(struct net_device *dev);

void mlx4_en_free_resources(struct mlx4_en_priv *priv);
int mlx4_en_alloc_resources(struct mlx4_en_priv *priv);

int mlx4_en_pre_config(struct mlx4_en_priv *priv);
int mlx4_en_create_cq(struct mlx4_en_priv *priv, struct mlx4_en_cq **pcq,
		      int entries, int ring, enum cq_type mode, int node);
void mlx4_en_destroy_cq(struct mlx4_en_priv *priv, struct mlx4_en_cq **pcq);
int mlx4_en_activate_cq(struct mlx4_en_priv *priv, struct mlx4_en_cq *cq,
			int cq_idx);
void mlx4_en_deactivate_cq(struct mlx4_en_priv *priv, struct mlx4_en_cq *cq);
int mlx4_en_set_cq_moder(struct mlx4_en_priv *priv, struct mlx4_en_cq *cq);
int mlx4_en_arm_cq(struct mlx4_en_priv *priv, struct mlx4_en_cq *cq);

void mlx4_en_tx_irq(struct mlx4_cq *mcq);
u16 mlx4_en_select_queue(struct net_device *dev, struct mbuf *mb);

int mlx4_en_xmit(struct mlx4_en_priv *priv, int tx_ind, struct mbuf **mbp);
int mlx4_en_transmit(struct ifnet *dev, struct mbuf *m);
int mlx4_en_create_tx_ring(struct mlx4_en_priv *priv,
			   struct mlx4_en_tx_ring **pring,
			   u32 size, u16 stride, int node, int queue_idx);
void mlx4_en_destroy_tx_ring(struct mlx4_en_priv *priv,
			     struct mlx4_en_tx_ring **pring);
int mlx4_en_activate_tx_ring(struct mlx4_en_priv *priv,
			     struct mlx4_en_tx_ring *ring,
			     int cq, int user_prio);
void mlx4_en_deactivate_tx_ring(struct mlx4_en_priv *priv,
				struct mlx4_en_tx_ring *ring);
void mlx4_en_set_num_rx_rings(struct mlx4_en_dev *mdev);
void mlx4_en_qflush(struct ifnet *dev);

int mlx4_en_create_rx_ring(struct mlx4_en_priv *priv,
			   struct mlx4_en_rx_ring **pring,
			   u32 size, int node);
void mlx4_en_destroy_rx_ring(struct mlx4_en_priv *priv,
			     struct mlx4_en_rx_ring **pring,
			     u32 size);
void mlx4_en_rx_que(void *context, int pending);
int mlx4_en_activate_rx_rings(struct mlx4_en_priv *priv);
void mlx4_en_deactivate_rx_ring(struct mlx4_en_priv *priv,
				struct mlx4_en_rx_ring *ring);
int mlx4_en_process_rx_cq(struct net_device *dev,
			  struct mlx4_en_cq *cq,
			  int budget);
void mlx4_en_poll_tx_cq(unsigned long data);
void mlx4_en_fill_qp_context(struct mlx4_en_priv *priv, int size, int stride,
		int is_tx, int rss, int qpn, int cqn, int user_prio,
		struct mlx4_qp_context *context);
void mlx4_en_sqp_event(struct mlx4_qp *qp, enum mlx4_event event);
int mlx4_en_map_buffer(struct mlx4_buf *buf);
void mlx4_en_unmap_buffer(struct mlx4_buf *buf);
void mlx4_en_calc_rx_buf(struct net_device *dev);

const u32 *mlx4_en_get_rss_key(struct mlx4_en_priv *priv, u16 *keylen);
u8 mlx4_en_get_rss_mask(struct mlx4_en_priv *priv);
int mlx4_en_config_rss_steer(struct mlx4_en_priv *priv);
void mlx4_en_release_rss_steer(struct mlx4_en_priv *priv);
int mlx4_en_create_drop_qp(struct mlx4_en_priv *priv);
void mlx4_en_destroy_drop_qp(struct mlx4_en_priv *priv);
int mlx4_en_free_tx_buf(struct net_device *dev, struct mlx4_en_tx_ring *ring);
void mlx4_en_rx_irq(struct mlx4_cq *mcq);

int mlx4_SET_VLAN_FLTR(struct mlx4_dev *dev, struct mlx4_en_priv *priv);

int mlx4_en_DUMP_ETH_STATS(struct mlx4_en_dev *mdev, u8 port, u8 reset);
int mlx4_en_QUERY_PORT(struct mlx4_en_dev *mdev, u8 port);
int mlx4_en_get_vport_stats(struct mlx4_en_dev *mdev, u8 port);
void mlx4_en_create_debug_files(struct mlx4_en_priv *priv);
void mlx4_en_delete_debug_files(struct mlx4_en_priv *priv);
int mlx4_en_register_debugfs(void);
void mlx4_en_unregister_debugfs(void);

#ifdef CONFIG_MLX4_EN_DCB
extern const struct dcbnl_rtnl_ops mlx4_en_dcbnl_ops;
extern const struct dcbnl_rtnl_ops mlx4_en_dcbnl_pfc_ops;
#endif

int mlx4_en_setup_tc(struct net_device *dev, u8 up);

#ifdef CONFIG_RFS_ACCEL
void mlx4_en_cleanup_filters(struct mlx4_en_priv *priv,
			     struct mlx4_en_rx_ring *rx_ring);
#endif

#define MLX4_EN_NUM_SELF_TEST	5
void mlx4_en_ex_selftest(struct net_device *dev, u32 *flags, u64 *buf);
void mlx4_en_ptp_overflow_check(struct mlx4_en_dev *mdev);

/*
 * Functions for time stamping
 */
#define SKBTX_HW_TSTAMP (1 << 0)
#define SKBTX_IN_PROGRESS (1 << 2)

u64 mlx4_en_get_cqe_ts(struct mlx4_cqe *cqe);

/* Functions for caching and restoring statistics */
int mlx4_en_get_sset_count(struct net_device *dev, int sset);
void mlx4_en_restore_ethtool_stats(struct mlx4_en_priv *priv,
				    u64 *data);

/*
 * Globals
 */
extern const struct ethtool_ops mlx4_en_ethtool_ops;

/*
 * Defines for link speed - needed by selftest
 */
#define MLX4_EN_LINK_SPEED_1G	1000
#define MLX4_EN_LINK_SPEED_10G	10000
#define MLX4_EN_LINK_SPEED_40G	40000

enum {
        NETIF_MSG_DRV           = 0x0001,
        NETIF_MSG_PROBE         = 0x0002,
        NETIF_MSG_LINK          = 0x0004,
        NETIF_MSG_TIMER         = 0x0008,
        NETIF_MSG_IFDOWN        = 0x0010,
        NETIF_MSG_IFUP          = 0x0020,
        NETIF_MSG_RX_ERR        = 0x0040,
        NETIF_MSG_TX_ERR        = 0x0080,
        NETIF_MSG_TX_QUEUED     = 0x0100,
        NETIF_MSG_INTR          = 0x0200,
        NETIF_MSG_TX_DONE       = 0x0400,
        NETIF_MSG_RX_STATUS     = 0x0800,
        NETIF_MSG_PKTDATA       = 0x1000,
        NETIF_MSG_HW            = 0x2000,
        NETIF_MSG_WOL           = 0x4000,
};


/*
 * printk / logging functions
 */

#define en_print(level, priv, format, arg...)                   \
        {                                                       \
        if ((priv)->registered)                                 \
                printk(level "%s: %s: " format, DRV_NAME,       \
                        (priv)->dev->if_xname, ## arg); \
        else                                                    \
                printk(level "%s: %s: Port %d: " format,        \
                        DRV_NAME, dev_name(&(priv)->mdev->pdev->dev), \
                        (priv)->port, ## arg);                  \
        }


#define en_dbg(mlevel, priv, format, arg...)			\
do {								\
	if (NETIF_MSG_##mlevel & priv->msg_enable)		\
		en_print(KERN_DEBUG, priv, format, ##arg);	\
} while (0)
#define en_warn(priv, format, arg...)			\
	en_print(KERN_WARNING, priv, format, ##arg)
#define en_err(priv, format, arg...)			\
	en_print(KERN_ERR, priv, format, ##arg)
#define en_info(priv, format, arg...)			\
	en_print(KERN_INFO, priv, format, ## arg)

#define mlx4_err(mdev, format, arg...)			\
	pr_err("%s %s: " format, DRV_NAME,		\
	       dev_name(&(mdev)->pdev->dev), ##arg)
#define mlx4_info(mdev, format, arg...)			\
	pr_info("%s %s: " format, DRV_NAME,		\
		dev_name(&(mdev)->pdev->dev), ##arg)
#define mlx4_warn(mdev, format, arg...)			\
	pr_warning("%s %s: " format, DRV_NAME,		\
		   dev_name(&(mdev)->pdev->dev), ##arg)

#endif
