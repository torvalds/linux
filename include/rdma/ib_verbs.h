/*
 * Copyright (c) 2004 Mellanox Technologies Ltd.  All rights reserved.
 * Copyright (c) 2004 Infinicon Corporation.  All rights reserved.
 * Copyright (c) 2004 Intel Corporation.  All rights reserved.
 * Copyright (c) 2004 Topspin Corporation.  All rights reserved.
 * Copyright (c) 2004 Voltaire Corporation.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2005, 2006, 2007 Cisco Systems.  All rights reserved.
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
 */

#if !defined(IB_VERBS_H)
#define IB_VERBS_H

#include <linux/types.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/rwsem.h>
#include <linux/workqueue.h>
#include <linux/irq_poll.h>
#include <uapi/linux/if_ether.h>
#include <net/ipv6.h>
#include <net/ip.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/refcount.h>
#include <linux/if_link.h>
#include <linux/atomic.h>
#include <linux/mmu_notifier.h>
#include <linux/uaccess.h>
#include <linux/cgroup_rdma.h>
#include <linux/irqflags.h>
#include <linux/preempt.h>
#include <linux/dim.h>
#include <uapi/rdma/ib_user_verbs.h>
#include <rdma/rdma_counter.h>
#include <rdma/restrack.h>
#include <rdma/signature.h>
#include <uapi/rdma/rdma_user_ioctl.h>
#include <uapi/rdma/ib_user_ioctl_verbs.h>

#define IB_FW_VERSION_NAME_MAX	ETHTOOL_FWVERS_LEN

struct ib_umem_odp;

extern struct workqueue_struct *ib_wq;
extern struct workqueue_struct *ib_comp_wq;
extern struct workqueue_struct *ib_comp_unbound_wq;

__printf(3, 4) __cold
void ibdev_printk(const char *level, const struct ib_device *ibdev,
		  const char *format, ...);
__printf(2, 3) __cold
void ibdev_emerg(const struct ib_device *ibdev, const char *format, ...);
__printf(2, 3) __cold
void ibdev_alert(const struct ib_device *ibdev, const char *format, ...);
__printf(2, 3) __cold
void ibdev_crit(const struct ib_device *ibdev, const char *format, ...);
__printf(2, 3) __cold
void ibdev_err(const struct ib_device *ibdev, const char *format, ...);
__printf(2, 3) __cold
void ibdev_warn(const struct ib_device *ibdev, const char *format, ...);
__printf(2, 3) __cold
void ibdev_notice(const struct ib_device *ibdev, const char *format, ...);
__printf(2, 3) __cold
void ibdev_info(const struct ib_device *ibdev, const char *format, ...);

#if defined(CONFIG_DYNAMIC_DEBUG)
#define ibdev_dbg(__dev, format, args...)                       \
	dynamic_ibdev_dbg(__dev, format, ##args)
#else
__printf(2, 3) __cold
static inline
void ibdev_dbg(const struct ib_device *ibdev, const char *format, ...) {}
#endif

#define ibdev_level_ratelimited(ibdev_level, ibdev, fmt, ...)           \
do {                                                                    \
	static DEFINE_RATELIMIT_STATE(_rs,                              \
				      DEFAULT_RATELIMIT_INTERVAL,       \
				      DEFAULT_RATELIMIT_BURST);         \
	if (__ratelimit(&_rs))                                          \
		ibdev_level(ibdev, fmt, ##__VA_ARGS__);                 \
} while (0)

#define ibdev_emerg_ratelimited(ibdev, fmt, ...) \
	ibdev_level_ratelimited(ibdev_emerg, ibdev, fmt, ##__VA_ARGS__)
#define ibdev_alert_ratelimited(ibdev, fmt, ...) \
	ibdev_level_ratelimited(ibdev_alert, ibdev, fmt, ##__VA_ARGS__)
#define ibdev_crit_ratelimited(ibdev, fmt, ...) \
	ibdev_level_ratelimited(ibdev_crit, ibdev, fmt, ##__VA_ARGS__)
#define ibdev_err_ratelimited(ibdev, fmt, ...) \
	ibdev_level_ratelimited(ibdev_err, ibdev, fmt, ##__VA_ARGS__)
#define ibdev_warn_ratelimited(ibdev, fmt, ...) \
	ibdev_level_ratelimited(ibdev_warn, ibdev, fmt, ##__VA_ARGS__)
#define ibdev_notice_ratelimited(ibdev, fmt, ...) \
	ibdev_level_ratelimited(ibdev_notice, ibdev, fmt, ##__VA_ARGS__)
#define ibdev_info_ratelimited(ibdev, fmt, ...) \
	ibdev_level_ratelimited(ibdev_info, ibdev, fmt, ##__VA_ARGS__)

#if defined(CONFIG_DYNAMIC_DEBUG)
/* descriptor check is first to prevent flooding with "callbacks suppressed" */
#define ibdev_dbg_ratelimited(ibdev, fmt, ...)                          \
do {                                                                    \
	static DEFINE_RATELIMIT_STATE(_rs,                              \
				      DEFAULT_RATELIMIT_INTERVAL,       \
				      DEFAULT_RATELIMIT_BURST);         \
	DEFINE_DYNAMIC_DEBUG_METADATA(descriptor, fmt);                 \
	if (DYNAMIC_DEBUG_BRANCH(descriptor) && __ratelimit(&_rs))      \
		__dynamic_ibdev_dbg(&descriptor, ibdev, fmt,            \
				    ##__VA_ARGS__);                     \
} while (0)
#else
__printf(2, 3) __cold
static inline
void ibdev_dbg_ratelimited(const struct ib_device *ibdev, const char *format, ...) {}
#endif

union ib_gid {
	u8	raw[16];
	struct {
		__be64	subnet_prefix;
		__be64	interface_id;
	} global;
};

extern union ib_gid zgid;

enum ib_gid_type {
	/* If link layer is Ethernet, this is RoCE V1 */
	IB_GID_TYPE_IB        = 0,
	IB_GID_TYPE_ROCE      = 0,
	IB_GID_TYPE_ROCE_UDP_ENCAP = 1,
	IB_GID_TYPE_SIZE
};

#define ROCE_V2_UDP_DPORT      4791
struct ib_gid_attr {
	struct net_device __rcu	*ndev;
	struct ib_device	*device;
	union ib_gid		gid;
	enum ib_gid_type	gid_type;
	u16			index;
	u8			port_num;
};

enum {
	/* set the local administered indication */
	IB_SA_WELL_KNOWN_GUID	= BIT_ULL(57) | 2,
};

enum rdma_transport_type {
	RDMA_TRANSPORT_IB,
	RDMA_TRANSPORT_IWARP,
	RDMA_TRANSPORT_USNIC,
	RDMA_TRANSPORT_USNIC_UDP,
	RDMA_TRANSPORT_UNSPECIFIED,
};

enum rdma_protocol_type {
	RDMA_PROTOCOL_IB,
	RDMA_PROTOCOL_IBOE,
	RDMA_PROTOCOL_IWARP,
	RDMA_PROTOCOL_USNIC_UDP
};

__attribute_const__ enum rdma_transport_type
rdma_node_get_transport(unsigned int node_type);

enum rdma_network_type {
	RDMA_NETWORK_IB,
	RDMA_NETWORK_ROCE_V1 = RDMA_NETWORK_IB,
	RDMA_NETWORK_IPV4,
	RDMA_NETWORK_IPV6
};

static inline enum ib_gid_type ib_network_to_gid_type(enum rdma_network_type network_type)
{
	if (network_type == RDMA_NETWORK_IPV4 ||
	    network_type == RDMA_NETWORK_IPV6)
		return IB_GID_TYPE_ROCE_UDP_ENCAP;

	/* IB_GID_TYPE_IB same as RDMA_NETWORK_ROCE_V1 */
	return IB_GID_TYPE_IB;
}

static inline enum rdma_network_type
rdma_gid_attr_network_type(const struct ib_gid_attr *attr)
{
	if (attr->gid_type == IB_GID_TYPE_IB)
		return RDMA_NETWORK_IB;

	if (ipv6_addr_v4mapped((struct in6_addr *)&attr->gid))
		return RDMA_NETWORK_IPV4;
	else
		return RDMA_NETWORK_IPV6;
}

enum rdma_link_layer {
	IB_LINK_LAYER_UNSPECIFIED,
	IB_LINK_LAYER_INFINIBAND,
	IB_LINK_LAYER_ETHERNET,
};

enum ib_device_cap_flags {
	IB_DEVICE_RESIZE_MAX_WR			= (1 << 0),
	IB_DEVICE_BAD_PKEY_CNTR			= (1 << 1),
	IB_DEVICE_BAD_QKEY_CNTR			= (1 << 2),
	IB_DEVICE_RAW_MULTI			= (1 << 3),
	IB_DEVICE_AUTO_PATH_MIG			= (1 << 4),
	IB_DEVICE_CHANGE_PHY_PORT		= (1 << 5),
	IB_DEVICE_UD_AV_PORT_ENFORCE		= (1 << 6),
	IB_DEVICE_CURR_QP_STATE_MOD		= (1 << 7),
	IB_DEVICE_SHUTDOWN_PORT			= (1 << 8),
	/* Not in use, former INIT_TYPE		= (1 << 9),*/
	IB_DEVICE_PORT_ACTIVE_EVENT		= (1 << 10),
	IB_DEVICE_SYS_IMAGE_GUID		= (1 << 11),
	IB_DEVICE_RC_RNR_NAK_GEN		= (1 << 12),
	IB_DEVICE_SRQ_RESIZE			= (1 << 13),
	IB_DEVICE_N_NOTIFY_CQ			= (1 << 14),

	/*
	 * This device supports a per-device lkey or stag that can be
	 * used without performing a memory registration for the local
	 * memory.  Note that ULPs should never check this flag, but
	 * instead of use the local_dma_lkey flag in the ib_pd structure,
	 * which will always contain a usable lkey.
	 */
	IB_DEVICE_LOCAL_DMA_LKEY		= (1 << 15),
	/* Reserved, old SEND_W_INV		= (1 << 16),*/
	IB_DEVICE_MEM_WINDOW			= (1 << 17),
	/*
	 * Devices should set IB_DEVICE_UD_IP_SUM if they support
	 * insertion of UDP and TCP checksum on outgoing UD IPoIB
	 * messages and can verify the validity of checksum for
	 * incoming messages.  Setting this flag implies that the
	 * IPoIB driver may set NETIF_F_IP_CSUM for datagram mode.
	 */
	IB_DEVICE_UD_IP_CSUM			= (1 << 18),
	IB_DEVICE_UD_TSO			= (1 << 19),
	IB_DEVICE_XRC				= (1 << 20),

	/*
	 * This device supports the IB "base memory management extension",
	 * which includes support for fast registrations (IB_WR_REG_MR,
	 * IB_WR_LOCAL_INV and IB_WR_SEND_WITH_INV verbs).  This flag should
	 * also be set by any iWarp device which must support FRs to comply
	 * to the iWarp verbs spec.  iWarp devices also support the
	 * IB_WR_RDMA_READ_WITH_INV verb for RDMA READs that invalidate the
	 * stag.
	 */
	IB_DEVICE_MEM_MGT_EXTENSIONS		= (1 << 21),
	IB_DEVICE_BLOCK_MULTICAST_LOOPBACK	= (1 << 22),
	IB_DEVICE_MEM_WINDOW_TYPE_2A		= (1 << 23),
	IB_DEVICE_MEM_WINDOW_TYPE_2B		= (1 << 24),
	IB_DEVICE_RC_IP_CSUM			= (1 << 25),
	/* Deprecated. Please use IB_RAW_PACKET_CAP_IP_CSUM. */
	IB_DEVICE_RAW_IP_CSUM			= (1 << 26),
	/*
	 * Devices should set IB_DEVICE_CROSS_CHANNEL if they
	 * support execution of WQEs that involve synchronization
	 * of I/O operations with single completion queue managed
	 * by hardware.
	 */
	IB_DEVICE_CROSS_CHANNEL			= (1 << 27),
	IB_DEVICE_MANAGED_FLOW_STEERING		= (1 << 29),
	IB_DEVICE_INTEGRITY_HANDOVER		= (1 << 30),
	IB_DEVICE_ON_DEMAND_PAGING		= (1ULL << 31),
	IB_DEVICE_SG_GAPS_REG			= (1ULL << 32),
	IB_DEVICE_VIRTUAL_FUNCTION		= (1ULL << 33),
	/* Deprecated. Please use IB_RAW_PACKET_CAP_SCATTER_FCS. */
	IB_DEVICE_RAW_SCATTER_FCS		= (1ULL << 34),
	IB_DEVICE_RDMA_NETDEV_OPA_VNIC		= (1ULL << 35),
	/* The device supports padding incoming writes to cacheline. */
	IB_DEVICE_PCI_WRITE_END_PADDING		= (1ULL << 36),
	IB_DEVICE_ALLOW_USER_UNREG		= (1ULL << 37),
};

enum ib_atomic_cap {
	IB_ATOMIC_NONE,
	IB_ATOMIC_HCA,
	IB_ATOMIC_GLOB
};

enum ib_odp_general_cap_bits {
	IB_ODP_SUPPORT		= 1 << 0,
	IB_ODP_SUPPORT_IMPLICIT = 1 << 1,
};

enum ib_odp_transport_cap_bits {
	IB_ODP_SUPPORT_SEND	= 1 << 0,
	IB_ODP_SUPPORT_RECV	= 1 << 1,
	IB_ODP_SUPPORT_WRITE	= 1 << 2,
	IB_ODP_SUPPORT_READ	= 1 << 3,
	IB_ODP_SUPPORT_ATOMIC	= 1 << 4,
	IB_ODP_SUPPORT_SRQ_RECV	= 1 << 5,
};

struct ib_odp_caps {
	uint64_t general_caps;
	struct {
		uint32_t  rc_odp_caps;
		uint32_t  uc_odp_caps;
		uint32_t  ud_odp_caps;
		uint32_t  xrc_odp_caps;
	} per_transport_caps;
};

struct ib_rss_caps {
	/* Corresponding bit will be set if qp type from
	 * 'enum ib_qp_type' is supported, e.g.
	 * supported_qpts |= 1 << IB_QPT_UD
	 */
	u32 supported_qpts;
	u32 max_rwq_indirection_tables;
	u32 max_rwq_indirection_table_size;
};

enum ib_tm_cap_flags {
	/*  Support tag matching with rendezvous offload for RC transport */
	IB_TM_CAP_RNDV_RC = 1 << 0,
};

struct ib_tm_caps {
	/* Max size of RNDV header */
	u32 max_rndv_hdr_size;
	/* Max number of entries in tag matching list */
	u32 max_num_tags;
	/* From enum ib_tm_cap_flags */
	u32 flags;
	/* Max number of outstanding list operations */
	u32 max_ops;
	/* Max number of SGE in tag matching entry */
	u32 max_sge;
};

struct ib_cq_init_attr {
	unsigned int	cqe;
	int		comp_vector;
	u32		flags;
};

enum ib_cq_attr_mask {
	IB_CQ_MODERATE = 1 << 0,
};

struct ib_cq_caps {
	u16     max_cq_moderation_count;
	u16     max_cq_moderation_period;
};

struct ib_dm_mr_attr {
	u64		length;
	u64		offset;
	u32		access_flags;
};

struct ib_dm_alloc_attr {
	u64	length;
	u32	alignment;
	u32	flags;
};

struct ib_device_attr {
	u64			fw_ver;
	__be64			sys_image_guid;
	u64			max_mr_size;
	u64			page_size_cap;
	u32			vendor_id;
	u32			vendor_part_id;
	u32			hw_ver;
	int			max_qp;
	int			max_qp_wr;
	u64			device_cap_flags;
	int			max_send_sge;
	int			max_recv_sge;
	int			max_sge_rd;
	int			max_cq;
	int			max_cqe;
	int			max_mr;
	int			max_pd;
	int			max_qp_rd_atom;
	int			max_ee_rd_atom;
	int			max_res_rd_atom;
	int			max_qp_init_rd_atom;
	int			max_ee_init_rd_atom;
	enum ib_atomic_cap	atomic_cap;
	enum ib_atomic_cap	masked_atomic_cap;
	int			max_ee;
	int			max_rdd;
	int			max_mw;
	int			max_raw_ipv6_qp;
	int			max_raw_ethy_qp;
	int			max_mcast_grp;
	int			max_mcast_qp_attach;
	int			max_total_mcast_qp_attach;
	int			max_ah;
	int			max_fmr;
	int			max_map_per_fmr;
	int			max_srq;
	int			max_srq_wr;
	int			max_srq_sge;
	unsigned int		max_fast_reg_page_list_len;
	unsigned int		max_pi_fast_reg_page_list_len;
	u16			max_pkeys;
	u8			local_ca_ack_delay;
	int			sig_prot_cap;
	int			sig_guard_cap;
	struct ib_odp_caps	odp_caps;
	uint64_t		timestamp_mask;
	uint64_t		hca_core_clock; /* in KHZ */
	struct ib_rss_caps	rss_caps;
	u32			max_wq_type_rq;
	u32			raw_packet_caps; /* Use ib_raw_packet_caps enum */
	struct ib_tm_caps	tm_caps;
	struct ib_cq_caps       cq_caps;
	u64			max_dm_size;
};

enum ib_mtu {
	IB_MTU_256  = 1,
	IB_MTU_512  = 2,
	IB_MTU_1024 = 3,
	IB_MTU_2048 = 4,
	IB_MTU_4096 = 5
};

static inline int ib_mtu_enum_to_int(enum ib_mtu mtu)
{
	switch (mtu) {
	case IB_MTU_256:  return  256;
	case IB_MTU_512:  return  512;
	case IB_MTU_1024: return 1024;
	case IB_MTU_2048: return 2048;
	case IB_MTU_4096: return 4096;
	default: 	  return -1;
	}
}

static inline enum ib_mtu ib_mtu_int_to_enum(int mtu)
{
	if (mtu >= 4096)
		return IB_MTU_4096;
	else if (mtu >= 2048)
		return IB_MTU_2048;
	else if (mtu >= 1024)
		return IB_MTU_1024;
	else if (mtu >= 512)
		return IB_MTU_512;
	else
		return IB_MTU_256;
}

enum ib_port_state {
	IB_PORT_NOP		= 0,
	IB_PORT_DOWN		= 1,
	IB_PORT_INIT		= 2,
	IB_PORT_ARMED		= 3,
	IB_PORT_ACTIVE		= 4,
	IB_PORT_ACTIVE_DEFER	= 5
};

enum ib_port_phys_state {
	IB_PORT_PHYS_STATE_SLEEP = 1,
	IB_PORT_PHYS_STATE_POLLING = 2,
	IB_PORT_PHYS_STATE_DISABLED = 3,
	IB_PORT_PHYS_STATE_PORT_CONFIGURATION_TRAINING = 4,
	IB_PORT_PHYS_STATE_LINK_UP = 5,
	IB_PORT_PHYS_STATE_LINK_ERROR_RECOVERY = 6,
	IB_PORT_PHYS_STATE_PHY_TEST = 7,
};

enum ib_port_width {
	IB_WIDTH_1X	= 1,
	IB_WIDTH_2X	= 16,
	IB_WIDTH_4X	= 2,
	IB_WIDTH_8X	= 4,
	IB_WIDTH_12X	= 8
};

static inline int ib_width_enum_to_int(enum ib_port_width width)
{
	switch (width) {
	case IB_WIDTH_1X:  return  1;
	case IB_WIDTH_2X:  return  2;
	case IB_WIDTH_4X:  return  4;
	case IB_WIDTH_8X:  return  8;
	case IB_WIDTH_12X: return 12;
	default: 	  return -1;
	}
}

enum ib_port_speed {
	IB_SPEED_SDR	= 1,
	IB_SPEED_DDR	= 2,
	IB_SPEED_QDR	= 4,
	IB_SPEED_FDR10	= 8,
	IB_SPEED_FDR	= 16,
	IB_SPEED_EDR	= 32,
	IB_SPEED_HDR	= 64
};

/**
 * struct rdma_hw_stats
 * @lock - Mutex to protect parallel write access to lifespan and values
 *    of counters, which are 64bits and not guaranteeed to be written
 *    atomicaly on 32bits systems.
 * @timestamp - Used by the core code to track when the last update was
 * @lifespan - Used by the core code to determine how old the counters
 *   should be before being updated again.  Stored in jiffies, defaults
 *   to 10 milliseconds, drivers can override the default be specifying
 *   their own value during their allocation routine.
 * @name - Array of pointers to static names used for the counters in
 *   directory.
 * @num_counters - How many hardware counters there are.  If name is
 *   shorter than this number, a kernel oops will result.  Driver authors
 *   are encouraged to leave BUILD_BUG_ON(ARRAY_SIZE(@name) < num_counters)
 *   in their code to prevent this.
 * @value - Array of u64 counters that are accessed by the sysfs code and
 *   filled in by the drivers get_stats routine
 */
struct rdma_hw_stats {
	struct mutex	lock; /* Protect lifespan and values[] */
	unsigned long	timestamp;
	unsigned long	lifespan;
	const char * const *names;
	int		num_counters;
	u64		value[];
};

#define RDMA_HW_STATS_DEFAULT_LIFESPAN 10
/**
 * rdma_alloc_hw_stats_struct - Helper function to allocate dynamic struct
 *   for drivers.
 * @names - Array of static const char *
 * @num_counters - How many elements in array
 * @lifespan - How many milliseconds between updates
 */
static inline struct rdma_hw_stats *rdma_alloc_hw_stats_struct(
		const char * const *names, int num_counters,
		unsigned long lifespan)
{
	struct rdma_hw_stats *stats;

	stats = kzalloc(sizeof(*stats) + num_counters * sizeof(u64),
			GFP_KERNEL);
	if (!stats)
		return NULL;
	stats->names = names;
	stats->num_counters = num_counters;
	stats->lifespan = msecs_to_jiffies(lifespan);

	return stats;
}


/* Define bits for the various functionality this port needs to be supported by
 * the core.
 */
/* Management                           0x00000FFF */
#define RDMA_CORE_CAP_IB_MAD            0x00000001
#define RDMA_CORE_CAP_IB_SMI            0x00000002
#define RDMA_CORE_CAP_IB_CM             0x00000004
#define RDMA_CORE_CAP_IW_CM             0x00000008
#define RDMA_CORE_CAP_IB_SA             0x00000010
#define RDMA_CORE_CAP_OPA_MAD           0x00000020

/* Address format                       0x000FF000 */
#define RDMA_CORE_CAP_AF_IB             0x00001000
#define RDMA_CORE_CAP_ETH_AH            0x00002000
#define RDMA_CORE_CAP_OPA_AH            0x00004000
#define RDMA_CORE_CAP_IB_GRH_REQUIRED   0x00008000

/* Protocol                             0xFFF00000 */
#define RDMA_CORE_CAP_PROT_IB           0x00100000
#define RDMA_CORE_CAP_PROT_ROCE         0x00200000
#define RDMA_CORE_CAP_PROT_IWARP        0x00400000
#define RDMA_CORE_CAP_PROT_ROCE_UDP_ENCAP 0x00800000
#define RDMA_CORE_CAP_PROT_RAW_PACKET   0x01000000
#define RDMA_CORE_CAP_PROT_USNIC        0x02000000

#define RDMA_CORE_PORT_IB_GRH_REQUIRED (RDMA_CORE_CAP_IB_GRH_REQUIRED \
					| RDMA_CORE_CAP_PROT_ROCE     \
					| RDMA_CORE_CAP_PROT_ROCE_UDP_ENCAP)

#define RDMA_CORE_PORT_IBA_IB          (RDMA_CORE_CAP_PROT_IB  \
					| RDMA_CORE_CAP_IB_MAD \
					| RDMA_CORE_CAP_IB_SMI \
					| RDMA_CORE_CAP_IB_CM  \
					| RDMA_CORE_CAP_IB_SA  \
					| RDMA_CORE_CAP_AF_IB)
#define RDMA_CORE_PORT_IBA_ROCE        (RDMA_CORE_CAP_PROT_ROCE \
					| RDMA_CORE_CAP_IB_MAD  \
					| RDMA_CORE_CAP_IB_CM   \
					| RDMA_CORE_CAP_AF_IB   \
					| RDMA_CORE_CAP_ETH_AH)
#define RDMA_CORE_PORT_IBA_ROCE_UDP_ENCAP			\
					(RDMA_CORE_CAP_PROT_ROCE_UDP_ENCAP \
					| RDMA_CORE_CAP_IB_MAD  \
					| RDMA_CORE_CAP_IB_CM   \
					| RDMA_CORE_CAP_AF_IB   \
					| RDMA_CORE_CAP_ETH_AH)
#define RDMA_CORE_PORT_IWARP           (RDMA_CORE_CAP_PROT_IWARP \
					| RDMA_CORE_CAP_IW_CM)
#define RDMA_CORE_PORT_INTEL_OPA       (RDMA_CORE_PORT_IBA_IB  \
					| RDMA_CORE_CAP_OPA_MAD)

#define RDMA_CORE_PORT_RAW_PACKET	(RDMA_CORE_CAP_PROT_RAW_PACKET)

#define RDMA_CORE_PORT_USNIC		(RDMA_CORE_CAP_PROT_USNIC)

struct ib_port_attr {
	u64			subnet_prefix;
	enum ib_port_state	state;
	enum ib_mtu		max_mtu;
	enum ib_mtu		active_mtu;
	int			gid_tbl_len;
	unsigned int		ip_gids:1;
	/* This is the value from PortInfo CapabilityMask, defined by IBA */
	u32			port_cap_flags;
	u32			max_msg_sz;
	u32			bad_pkey_cntr;
	u32			qkey_viol_cntr;
	u16			pkey_tbl_len;
	u32			sm_lid;
	u32			lid;
	u8			lmc;
	u8			max_vl_num;
	u8			sm_sl;
	u8			subnet_timeout;
	u8			init_type_reply;
	u8			active_width;
	u8			active_speed;
	u8                      phys_state;
	u16			port_cap_flags2;
};

enum ib_device_modify_flags {
	IB_DEVICE_MODIFY_SYS_IMAGE_GUID	= 1 << 0,
	IB_DEVICE_MODIFY_NODE_DESC	= 1 << 1
};

#define IB_DEVICE_NODE_DESC_MAX 64

struct ib_device_modify {
	u64	sys_image_guid;
	char	node_desc[IB_DEVICE_NODE_DESC_MAX];
};

enum ib_port_modify_flags {
	IB_PORT_SHUTDOWN		= 1,
	IB_PORT_INIT_TYPE		= (1<<2),
	IB_PORT_RESET_QKEY_CNTR		= (1<<3),
	IB_PORT_OPA_MASK_CHG		= (1<<4)
};

struct ib_port_modify {
	u32	set_port_cap_mask;
	u32	clr_port_cap_mask;
	u8	init_type;
};

enum ib_event_type {
	IB_EVENT_CQ_ERR,
	IB_EVENT_QP_FATAL,
	IB_EVENT_QP_REQ_ERR,
	IB_EVENT_QP_ACCESS_ERR,
	IB_EVENT_COMM_EST,
	IB_EVENT_SQ_DRAINED,
	IB_EVENT_PATH_MIG,
	IB_EVENT_PATH_MIG_ERR,
	IB_EVENT_DEVICE_FATAL,
	IB_EVENT_PORT_ACTIVE,
	IB_EVENT_PORT_ERR,
	IB_EVENT_LID_CHANGE,
	IB_EVENT_PKEY_CHANGE,
	IB_EVENT_SM_CHANGE,
	IB_EVENT_SRQ_ERR,
	IB_EVENT_SRQ_LIMIT_REACHED,
	IB_EVENT_QP_LAST_WQE_REACHED,
	IB_EVENT_CLIENT_REREGISTER,
	IB_EVENT_GID_CHANGE,
	IB_EVENT_WQ_FATAL,
};

const char *__attribute_const__ ib_event_msg(enum ib_event_type event);

struct ib_event {
	struct ib_device	*device;
	union {
		struct ib_cq	*cq;
		struct ib_qp	*qp;
		struct ib_srq	*srq;
		struct ib_wq	*wq;
		u8		port_num;
	} element;
	enum ib_event_type	event;
};

struct ib_event_handler {
	struct ib_device *device;
	void            (*handler)(struct ib_event_handler *, struct ib_event *);
	struct list_head  list;
};

#define INIT_IB_EVENT_HANDLER(_ptr, _device, _handler)		\
	do {							\
		(_ptr)->device  = _device;			\
		(_ptr)->handler = _handler;			\
		INIT_LIST_HEAD(&(_ptr)->list);			\
	} while (0)

struct ib_global_route {
	const struct ib_gid_attr *sgid_attr;
	union ib_gid	dgid;
	u32		flow_label;
	u8		sgid_index;
	u8		hop_limit;
	u8		traffic_class;
};

struct ib_grh {
	__be32		version_tclass_flow;
	__be16		paylen;
	u8		next_hdr;
	u8		hop_limit;
	union ib_gid	sgid;
	union ib_gid	dgid;
};

union rdma_network_hdr {
	struct ib_grh ibgrh;
	struct {
		/* The IB spec states that if it's IPv4, the header
		 * is located in the last 20 bytes of the header.
		 */
		u8		reserved[20];
		struct iphdr	roce4grh;
	};
};

#define IB_QPN_MASK		0xFFFFFF

enum {
	IB_MULTICAST_QPN = 0xffffff
};

#define IB_LID_PERMISSIVE	cpu_to_be16(0xFFFF)
#define IB_MULTICAST_LID_BASE	cpu_to_be16(0xC000)

enum ib_ah_flags {
	IB_AH_GRH	= 1
};

enum ib_rate {
	IB_RATE_PORT_CURRENT = 0,
	IB_RATE_2_5_GBPS = 2,
	IB_RATE_5_GBPS   = 5,
	IB_RATE_10_GBPS  = 3,
	IB_RATE_20_GBPS  = 6,
	IB_RATE_30_GBPS  = 4,
	IB_RATE_40_GBPS  = 7,
	IB_RATE_60_GBPS  = 8,
	IB_RATE_80_GBPS  = 9,
	IB_RATE_120_GBPS = 10,
	IB_RATE_14_GBPS  = 11,
	IB_RATE_56_GBPS  = 12,
	IB_RATE_112_GBPS = 13,
	IB_RATE_168_GBPS = 14,
	IB_RATE_25_GBPS  = 15,
	IB_RATE_100_GBPS = 16,
	IB_RATE_200_GBPS = 17,
	IB_RATE_300_GBPS = 18,
	IB_RATE_28_GBPS  = 19,
	IB_RATE_50_GBPS  = 20,
	IB_RATE_400_GBPS = 21,
	IB_RATE_600_GBPS = 22,
};

/**
 * ib_rate_to_mult - Convert the IB rate enum to a multiple of the
 * base rate of 2.5 Gbit/sec.  For example, IB_RATE_5_GBPS will be
 * converted to 2, since 5 Gbit/sec is 2 * 2.5 Gbit/sec.
 * @rate: rate to convert.
 */
__attribute_const__ int ib_rate_to_mult(enum ib_rate rate);

/**
 * ib_rate_to_mbps - Convert the IB rate enum to Mbps.
 * For example, IB_RATE_2_5_GBPS will be converted to 2500.
 * @rate: rate to convert.
 */
__attribute_const__ int ib_rate_to_mbps(enum ib_rate rate);


/**
 * enum ib_mr_type - memory region type
 * @IB_MR_TYPE_MEM_REG:       memory region that is used for
 *                            normal registration
 * @IB_MR_TYPE_SG_GAPS:       memory region that is capable to
 *                            register any arbitrary sg lists (without
 *                            the normal mr constraints - see
 *                            ib_map_mr_sg)
 * @IB_MR_TYPE_DM:            memory region that is used for device
 *                            memory registration
 * @IB_MR_TYPE_USER:          memory region that is used for the user-space
 *                            application
 * @IB_MR_TYPE_DMA:           memory region that is used for DMA operations
 *                            without address translations (VA=PA)
 * @IB_MR_TYPE_INTEGRITY:     memory region that is used for
 *                            data integrity operations
 */
enum ib_mr_type {
	IB_MR_TYPE_MEM_REG,
	IB_MR_TYPE_SG_GAPS,
	IB_MR_TYPE_DM,
	IB_MR_TYPE_USER,
	IB_MR_TYPE_DMA,
	IB_MR_TYPE_INTEGRITY,
};

enum ib_mr_status_check {
	IB_MR_CHECK_SIG_STATUS = 1,
};

/**
 * struct ib_mr_status - Memory region status container
 *
 * @fail_status: Bitmask of MR checks status. For each
 *     failed check a corresponding status bit is set.
 * @sig_err: Additional info for IB_MR_CEHCK_SIG_STATUS
 *     failure.
 */
struct ib_mr_status {
	u32		    fail_status;
	struct ib_sig_err   sig_err;
};

/**
 * mult_to_ib_rate - Convert a multiple of 2.5 Gbit/sec to an IB rate
 * enum.
 * @mult: multiple to convert.
 */
__attribute_const__ enum ib_rate mult_to_ib_rate(int mult);

enum rdma_ah_attr_type {
	RDMA_AH_ATTR_TYPE_UNDEFINED,
	RDMA_AH_ATTR_TYPE_IB,
	RDMA_AH_ATTR_TYPE_ROCE,
	RDMA_AH_ATTR_TYPE_OPA,
};

struct ib_ah_attr {
	u16			dlid;
	u8			src_path_bits;
};

struct roce_ah_attr {
	u8			dmac[ETH_ALEN];
};

struct opa_ah_attr {
	u32			dlid;
	u8			src_path_bits;
	bool			make_grd;
};

struct rdma_ah_attr {
	struct ib_global_route	grh;
	u8			sl;
	u8			static_rate;
	u8			port_num;
	u8			ah_flags;
	enum rdma_ah_attr_type type;
	union {
		struct ib_ah_attr ib;
		struct roce_ah_attr roce;
		struct opa_ah_attr opa;
	};
};

enum ib_wc_status {
	IB_WC_SUCCESS,
	IB_WC_LOC_LEN_ERR,
	IB_WC_LOC_QP_OP_ERR,
	IB_WC_LOC_EEC_OP_ERR,
	IB_WC_LOC_PROT_ERR,
	IB_WC_WR_FLUSH_ERR,
	IB_WC_MW_BIND_ERR,
	IB_WC_BAD_RESP_ERR,
	IB_WC_LOC_ACCESS_ERR,
	IB_WC_REM_INV_REQ_ERR,
	IB_WC_REM_ACCESS_ERR,
	IB_WC_REM_OP_ERR,
	IB_WC_RETRY_EXC_ERR,
	IB_WC_RNR_RETRY_EXC_ERR,
	IB_WC_LOC_RDD_VIOL_ERR,
	IB_WC_REM_INV_RD_REQ_ERR,
	IB_WC_REM_ABORT_ERR,
	IB_WC_INV_EECN_ERR,
	IB_WC_INV_EEC_STATE_ERR,
	IB_WC_FATAL_ERR,
	IB_WC_RESP_TIMEOUT_ERR,
	IB_WC_GENERAL_ERR
};

const char *__attribute_const__ ib_wc_status_msg(enum ib_wc_status status);

enum ib_wc_opcode {
	IB_WC_SEND,
	IB_WC_RDMA_WRITE,
	IB_WC_RDMA_READ,
	IB_WC_COMP_SWAP,
	IB_WC_FETCH_ADD,
	IB_WC_LSO,
	IB_WC_LOCAL_INV,
	IB_WC_REG_MR,
	IB_WC_MASKED_COMP_SWAP,
	IB_WC_MASKED_FETCH_ADD,
/*
 * Set value of IB_WC_RECV so consumers can test if a completion is a
 * receive by testing (opcode & IB_WC_RECV).
 */
	IB_WC_RECV			= 1 << 7,
	IB_WC_RECV_RDMA_WITH_IMM
};

enum ib_wc_flags {
	IB_WC_GRH		= 1,
	IB_WC_WITH_IMM		= (1<<1),
	IB_WC_WITH_INVALIDATE	= (1<<2),
	IB_WC_IP_CSUM_OK	= (1<<3),
	IB_WC_WITH_SMAC		= (1<<4),
	IB_WC_WITH_VLAN		= (1<<5),
	IB_WC_WITH_NETWORK_HDR_TYPE	= (1<<6),
};

struct ib_wc {
	union {
		u64		wr_id;
		struct ib_cqe	*wr_cqe;
	};
	enum ib_wc_status	status;
	enum ib_wc_opcode	opcode;
	u32			vendor_err;
	u32			byte_len;
	struct ib_qp	       *qp;
	union {
		__be32		imm_data;
		u32		invalidate_rkey;
	} ex;
	u32			src_qp;
	u32			slid;
	int			wc_flags;
	u16			pkey_index;
	u8			sl;
	u8			dlid_path_bits;
	u8			port_num;	/* valid only for DR SMPs on switches */
	u8			smac[ETH_ALEN];
	u16			vlan_id;
	u8			network_hdr_type;
};

enum ib_cq_notify_flags {
	IB_CQ_SOLICITED			= 1 << 0,
	IB_CQ_NEXT_COMP			= 1 << 1,
	IB_CQ_SOLICITED_MASK		= IB_CQ_SOLICITED | IB_CQ_NEXT_COMP,
	IB_CQ_REPORT_MISSED_EVENTS	= 1 << 2,
};

enum ib_srq_type {
	IB_SRQT_BASIC,
	IB_SRQT_XRC,
	IB_SRQT_TM,
};

static inline bool ib_srq_has_cq(enum ib_srq_type srq_type)
{
	return srq_type == IB_SRQT_XRC ||
	       srq_type == IB_SRQT_TM;
}

enum ib_srq_attr_mask {
	IB_SRQ_MAX_WR	= 1 << 0,
	IB_SRQ_LIMIT	= 1 << 1,
};

struct ib_srq_attr {
	u32	max_wr;
	u32	max_sge;
	u32	srq_limit;
};

struct ib_srq_init_attr {
	void		      (*event_handler)(struct ib_event *, void *);
	void		       *srq_context;
	struct ib_srq_attr	attr;
	enum ib_srq_type	srq_type;

	struct {
		struct ib_cq   *cq;
		union {
			struct {
				struct ib_xrcd *xrcd;
			} xrc;

			struct {
				u32		max_num_tags;
			} tag_matching;
		};
	} ext;
};

struct ib_qp_cap {
	u32	max_send_wr;
	u32	max_recv_wr;
	u32	max_send_sge;
	u32	max_recv_sge;
	u32	max_inline_data;

	/*
	 * Maximum number of rdma_rw_ctx structures in flight at a time.
	 * ib_create_qp() will calculate the right amount of neededed WRs
	 * and MRs based on this.
	 */
	u32	max_rdma_ctxs;
};

enum ib_sig_type {
	IB_SIGNAL_ALL_WR,
	IB_SIGNAL_REQ_WR
};

enum ib_qp_type {
	/*
	 * IB_QPT_SMI and IB_QPT_GSI have to be the first two entries
	 * here (and in that order) since the MAD layer uses them as
	 * indices into a 2-entry table.
	 */
	IB_QPT_SMI,
	IB_QPT_GSI,

	IB_QPT_RC,
	IB_QPT_UC,
	IB_QPT_UD,
	IB_QPT_RAW_IPV6,
	IB_QPT_RAW_ETHERTYPE,
	IB_QPT_RAW_PACKET = 8,
	IB_QPT_XRC_INI = 9,
	IB_QPT_XRC_TGT,
	IB_QPT_MAX,
	IB_QPT_DRIVER = 0xFF,
	/* Reserve a range for qp types internal to the low level driver.
	 * These qp types will not be visible at the IB core layer, so the
	 * IB_QPT_MAX usages should not be affected in the core layer
	 */
	IB_QPT_RESERVED1 = 0x1000,
	IB_QPT_RESERVED2,
	IB_QPT_RESERVED3,
	IB_QPT_RESERVED4,
	IB_QPT_RESERVED5,
	IB_QPT_RESERVED6,
	IB_QPT_RESERVED7,
	IB_QPT_RESERVED8,
	IB_QPT_RESERVED9,
	IB_QPT_RESERVED10,
};

enum ib_qp_create_flags {
	IB_QP_CREATE_IPOIB_UD_LSO		= 1 << 0,
	IB_QP_CREATE_BLOCK_MULTICAST_LOOPBACK	= 1 << 1,
	IB_QP_CREATE_CROSS_CHANNEL              = 1 << 2,
	IB_QP_CREATE_MANAGED_SEND               = 1 << 3,
	IB_QP_CREATE_MANAGED_RECV               = 1 << 4,
	IB_QP_CREATE_NETIF_QP			= 1 << 5,
	IB_QP_CREATE_INTEGRITY_EN		= 1 << 6,
	/* FREE					= 1 << 7, */
	IB_QP_CREATE_SCATTER_FCS		= 1 << 8,
	IB_QP_CREATE_CVLAN_STRIPPING		= 1 << 9,
	IB_QP_CREATE_SOURCE_QPN			= 1 << 10,
	IB_QP_CREATE_PCI_WRITE_END_PADDING	= 1 << 11,
	/* reserve bits 26-31 for low level drivers' internal use */
	IB_QP_CREATE_RESERVED_START		= 1 << 26,
	IB_QP_CREATE_RESERVED_END		= 1 << 31,
};

/*
 * Note: users may not call ib_close_qp or ib_destroy_qp from the event_handler
 * callback to destroy the passed in QP.
 */

struct ib_qp_init_attr {
	/* Consumer's event_handler callback must not block */
	void                  (*event_handler)(struct ib_event *, void *);

	void		       *qp_context;
	struct ib_cq	       *send_cq;
	struct ib_cq	       *recv_cq;
	struct ib_srq	       *srq;
	struct ib_xrcd	       *xrcd;     /* XRC TGT QPs only */
	struct ib_qp_cap	cap;
	enum ib_sig_type	sq_sig_type;
	enum ib_qp_type		qp_type;
	u32			create_flags;

	/*
	 * Only needed for special QP types, or when using the RW API.
	 */
	u8			port_num;
	struct ib_rwq_ind_table *rwq_ind_tbl;
	u32			source_qpn;
};

struct ib_qp_open_attr {
	void                  (*event_handler)(struct ib_event *, void *);
	void		       *qp_context;
	u32			qp_num;
	enum ib_qp_type		qp_type;
};

enum ib_rnr_timeout {
	IB_RNR_TIMER_655_36 =  0,
	IB_RNR_TIMER_000_01 =  1,
	IB_RNR_TIMER_000_02 =  2,
	IB_RNR_TIMER_000_03 =  3,
	IB_RNR_TIMER_000_04 =  4,
	IB_RNR_TIMER_000_06 =  5,
	IB_RNR_TIMER_000_08 =  6,
	IB_RNR_TIMER_000_12 =  7,
	IB_RNR_TIMER_000_16 =  8,
	IB_RNR_TIMER_000_24 =  9,
	IB_RNR_TIMER_000_32 = 10,
	IB_RNR_TIMER_000_48 = 11,
	IB_RNR_TIMER_000_64 = 12,
	IB_RNR_TIMER_000_96 = 13,
	IB_RNR_TIMER_001_28 = 14,
	IB_RNR_TIMER_001_92 = 15,
	IB_RNR_TIMER_002_56 = 16,
	IB_RNR_TIMER_003_84 = 17,
	IB_RNR_TIMER_005_12 = 18,
	IB_RNR_TIMER_007_68 = 19,
	IB_RNR_TIMER_010_24 = 20,
	IB_RNR_TIMER_015_36 = 21,
	IB_RNR_TIMER_020_48 = 22,
	IB_RNR_TIMER_030_72 = 23,
	IB_RNR_TIMER_040_96 = 24,
	IB_RNR_TIMER_061_44 = 25,
	IB_RNR_TIMER_081_92 = 26,
	IB_RNR_TIMER_122_88 = 27,
	IB_RNR_TIMER_163_84 = 28,
	IB_RNR_TIMER_245_76 = 29,
	IB_RNR_TIMER_327_68 = 30,
	IB_RNR_TIMER_491_52 = 31
};

enum ib_qp_attr_mask {
	IB_QP_STATE			= 1,
	IB_QP_CUR_STATE			= (1<<1),
	IB_QP_EN_SQD_ASYNC_NOTIFY	= (1<<2),
	IB_QP_ACCESS_FLAGS		= (1<<3),
	IB_QP_PKEY_INDEX		= (1<<4),
	IB_QP_PORT			= (1<<5),
	IB_QP_QKEY			= (1<<6),
	IB_QP_AV			= (1<<7),
	IB_QP_PATH_MTU			= (1<<8),
	IB_QP_TIMEOUT			= (1<<9),
	IB_QP_RETRY_CNT			= (1<<10),
	IB_QP_RNR_RETRY			= (1<<11),
	IB_QP_RQ_PSN			= (1<<12),
	IB_QP_MAX_QP_RD_ATOMIC		= (1<<13),
	IB_QP_ALT_PATH			= (1<<14),
	IB_QP_MIN_RNR_TIMER		= (1<<15),
	IB_QP_SQ_PSN			= (1<<16),
	IB_QP_MAX_DEST_RD_ATOMIC	= (1<<17),
	IB_QP_PATH_MIG_STATE		= (1<<18),
	IB_QP_CAP			= (1<<19),
	IB_QP_DEST_QPN			= (1<<20),
	IB_QP_RESERVED1			= (1<<21),
	IB_QP_RESERVED2			= (1<<22),
	IB_QP_RESERVED3			= (1<<23),
	IB_QP_RESERVED4			= (1<<24),
	IB_QP_RATE_LIMIT		= (1<<25),
};

enum ib_qp_state {
	IB_QPS_RESET,
	IB_QPS_INIT,
	IB_QPS_RTR,
	IB_QPS_RTS,
	IB_QPS_SQD,
	IB_QPS_SQE,
	IB_QPS_ERR
};

enum ib_mig_state {
	IB_MIG_MIGRATED,
	IB_MIG_REARM,
	IB_MIG_ARMED
};

enum ib_mw_type {
	IB_MW_TYPE_1 = 1,
	IB_MW_TYPE_2 = 2
};

struct ib_qp_attr {
	enum ib_qp_state	qp_state;
	enum ib_qp_state	cur_qp_state;
	enum ib_mtu		path_mtu;
	enum ib_mig_state	path_mig_state;
	u32			qkey;
	u32			rq_psn;
	u32			sq_psn;
	u32			dest_qp_num;
	int			qp_access_flags;
	struct ib_qp_cap	cap;
	struct rdma_ah_attr	ah_attr;
	struct rdma_ah_attr	alt_ah_attr;
	u16			pkey_index;
	u16			alt_pkey_index;
	u8			en_sqd_async_notify;
	u8			sq_draining;
	u8			max_rd_atomic;
	u8			max_dest_rd_atomic;
	u8			min_rnr_timer;
	u8			port_num;
	u8			timeout;
	u8			retry_cnt;
	u8			rnr_retry;
	u8			alt_port_num;
	u8			alt_timeout;
	u32			rate_limit;
};

enum ib_wr_opcode {
	/* These are shared with userspace */
	IB_WR_RDMA_WRITE = IB_UVERBS_WR_RDMA_WRITE,
	IB_WR_RDMA_WRITE_WITH_IMM = IB_UVERBS_WR_RDMA_WRITE_WITH_IMM,
	IB_WR_SEND = IB_UVERBS_WR_SEND,
	IB_WR_SEND_WITH_IMM = IB_UVERBS_WR_SEND_WITH_IMM,
	IB_WR_RDMA_READ = IB_UVERBS_WR_RDMA_READ,
	IB_WR_ATOMIC_CMP_AND_SWP = IB_UVERBS_WR_ATOMIC_CMP_AND_SWP,
	IB_WR_ATOMIC_FETCH_AND_ADD = IB_UVERBS_WR_ATOMIC_FETCH_AND_ADD,
	IB_WR_LSO = IB_UVERBS_WR_TSO,
	IB_WR_SEND_WITH_INV = IB_UVERBS_WR_SEND_WITH_INV,
	IB_WR_RDMA_READ_WITH_INV = IB_UVERBS_WR_RDMA_READ_WITH_INV,
	IB_WR_LOCAL_INV = IB_UVERBS_WR_LOCAL_INV,
	IB_WR_MASKED_ATOMIC_CMP_AND_SWP =
		IB_UVERBS_WR_MASKED_ATOMIC_CMP_AND_SWP,
	IB_WR_MASKED_ATOMIC_FETCH_AND_ADD =
		IB_UVERBS_WR_MASKED_ATOMIC_FETCH_AND_ADD,

	/* These are kernel only and can not be issued by userspace */
	IB_WR_REG_MR = 0x20,
	IB_WR_REG_MR_INTEGRITY,

	/* reserve values for low level drivers' internal use.
	 * These values will not be used at all in the ib core layer.
	 */
	IB_WR_RESERVED1 = 0xf0,
	IB_WR_RESERVED2,
	IB_WR_RESERVED3,
	IB_WR_RESERVED4,
	IB_WR_RESERVED5,
	IB_WR_RESERVED6,
	IB_WR_RESERVED7,
	IB_WR_RESERVED8,
	IB_WR_RESERVED9,
	IB_WR_RESERVED10,
};

enum ib_send_flags {
	IB_SEND_FENCE		= 1,
	IB_SEND_SIGNALED	= (1<<1),
	IB_SEND_SOLICITED	= (1<<2),
	IB_SEND_INLINE		= (1<<3),
	IB_SEND_IP_CSUM		= (1<<4),

	/* reserve bits 26-31 for low level drivers' internal use */
	IB_SEND_RESERVED_START	= (1 << 26),
	IB_SEND_RESERVED_END	= (1 << 31),
};

struct ib_sge {
	u64	addr;
	u32	length;
	u32	lkey;
};

struct ib_cqe {
	void (*done)(struct ib_cq *cq, struct ib_wc *wc);
};

struct ib_send_wr {
	struct ib_send_wr      *next;
	union {
		u64		wr_id;
		struct ib_cqe	*wr_cqe;
	};
	struct ib_sge	       *sg_list;
	int			num_sge;
	enum ib_wr_opcode	opcode;
	int			send_flags;
	union {
		__be32		imm_data;
		u32		invalidate_rkey;
	} ex;
};

struct ib_rdma_wr {
	struct ib_send_wr	wr;
	u64			remote_addr;
	u32			rkey;
};

static inline const struct ib_rdma_wr *rdma_wr(const struct ib_send_wr *wr)
{
	return container_of(wr, struct ib_rdma_wr, wr);
}

struct ib_atomic_wr {
	struct ib_send_wr	wr;
	u64			remote_addr;
	u64			compare_add;
	u64			swap;
	u64			compare_add_mask;
	u64			swap_mask;
	u32			rkey;
};

static inline const struct ib_atomic_wr *atomic_wr(const struct ib_send_wr *wr)
{
	return container_of(wr, struct ib_atomic_wr, wr);
}

struct ib_ud_wr {
	struct ib_send_wr	wr;
	struct ib_ah		*ah;
	void			*header;
	int			hlen;
	int			mss;
	u32			remote_qpn;
	u32			remote_qkey;
	u16			pkey_index; /* valid for GSI only */
	u8			port_num;   /* valid for DR SMPs on switch only */
};

static inline const struct ib_ud_wr *ud_wr(const struct ib_send_wr *wr)
{
	return container_of(wr, struct ib_ud_wr, wr);
}

struct ib_reg_wr {
	struct ib_send_wr	wr;
	struct ib_mr		*mr;
	u32			key;
	int			access;
};

static inline const struct ib_reg_wr *reg_wr(const struct ib_send_wr *wr)
{
	return container_of(wr, struct ib_reg_wr, wr);
}

struct ib_recv_wr {
	struct ib_recv_wr      *next;
	union {
		u64		wr_id;
		struct ib_cqe	*wr_cqe;
	};
	struct ib_sge	       *sg_list;
	int			num_sge;
};

enum ib_access_flags {
	IB_ACCESS_LOCAL_WRITE = IB_UVERBS_ACCESS_LOCAL_WRITE,
	IB_ACCESS_REMOTE_WRITE = IB_UVERBS_ACCESS_REMOTE_WRITE,
	IB_ACCESS_REMOTE_READ = IB_UVERBS_ACCESS_REMOTE_READ,
	IB_ACCESS_REMOTE_ATOMIC = IB_UVERBS_ACCESS_REMOTE_ATOMIC,
	IB_ACCESS_MW_BIND = IB_UVERBS_ACCESS_MW_BIND,
	IB_ZERO_BASED = IB_UVERBS_ACCESS_ZERO_BASED,
	IB_ACCESS_ON_DEMAND = IB_UVERBS_ACCESS_ON_DEMAND,
	IB_ACCESS_HUGETLB = IB_UVERBS_ACCESS_HUGETLB,

	IB_ACCESS_SUPPORTED = ((IB_ACCESS_HUGETLB << 1) - 1)
};

/*
 * XXX: these are apparently used for ->rereg_user_mr, no idea why they
 * are hidden here instead of a uapi header!
 */
enum ib_mr_rereg_flags {
	IB_MR_REREG_TRANS	= 1,
	IB_MR_REREG_PD		= (1<<1),
	IB_MR_REREG_ACCESS	= (1<<2),
	IB_MR_REREG_SUPPORTED	= ((IB_MR_REREG_ACCESS << 1) - 1)
};

struct ib_fmr_attr {
	int	max_pages;
	int	max_maps;
	u8	page_shift;
};

struct ib_umem;

enum rdma_remove_reason {
	/*
	 * Userspace requested uobject deletion or initial try
	 * to remove uobject via cleanup. Call could fail
	 */
	RDMA_REMOVE_DESTROY,
	/* Context deletion. This call should delete the actual object itself */
	RDMA_REMOVE_CLOSE,
	/* Driver is being hot-unplugged. This call should delete the actual object itself */
	RDMA_REMOVE_DRIVER_REMOVE,
	/* uobj is being cleaned-up before being committed */
	RDMA_REMOVE_ABORT,
};

struct ib_rdmacg_object {
#ifdef CONFIG_CGROUP_RDMA
	struct rdma_cgroup	*cg;		/* owner rdma cgroup */
#endif
};

struct ib_ucontext {
	struct ib_device       *device;
	struct ib_uverbs_file  *ufile;
	/*
	 * 'closing' can be read by the driver only during a destroy callback,
	 * it is set when we are closing the file descriptor and indicates
	 * that mm_sem may be locked.
	 */
	bool closing;

	bool cleanup_retryable;

	struct ib_rdmacg_object	cg_obj;
	/*
	 * Implementation details of the RDMA core, don't use in drivers:
	 */
	struct rdma_restrack_entry res;
};

struct ib_uobject {
	u64			user_handle;	/* handle given to us by userspace */
	/* ufile & ucontext owning this object */
	struct ib_uverbs_file  *ufile;
	/* FIXME, save memory: ufile->context == context */
	struct ib_ucontext     *context;	/* associated user context */
	void		       *object;		/* containing object */
	struct list_head	list;		/* link to context's list */
	struct ib_rdmacg_object	cg_obj;		/* rdmacg object */
	int			id;		/* index into kernel idr */
	struct kref		ref;
	atomic_t		usecnt;		/* protects exclusive access */
	struct rcu_head		rcu;		/* kfree_rcu() overhead */

	const struct uverbs_api_object *uapi_object;
};

struct ib_udata {
	const void __user *inbuf;
	void __user *outbuf;
	size_t       inlen;
	size_t       outlen;
};

struct ib_pd {
	u32			local_dma_lkey;
	u32			flags;
	struct ib_device       *device;
	struct ib_uobject      *uobject;
	atomic_t          	usecnt; /* count all resources */

	u32			unsafe_global_rkey;

	/*
	 * Implementation details of the RDMA core, don't use in drivers:
	 */
	struct ib_mr	       *__internal_mr;
	struct rdma_restrack_entry res;
};

struct ib_xrcd {
	struct ib_device       *device;
	atomic_t		usecnt; /* count all exposed resources */
	struct inode	       *inode;

	struct mutex		tgt_qp_mutex;
	struct list_head	tgt_qp_list;
};

struct ib_ah {
	struct ib_device	*device;
	struct ib_pd		*pd;
	struct ib_uobject	*uobject;
	const struct ib_gid_attr *sgid_attr;
	enum rdma_ah_attr_type	type;
};

typedef void (*ib_comp_handler)(struct ib_cq *cq, void *cq_context);

enum ib_poll_context {
	IB_POLL_DIRECT,		   /* caller context, no hw completions */
	IB_POLL_SOFTIRQ,	   /* poll from softirq context */
	IB_POLL_WORKQUEUE,	   /* poll from workqueue */
	IB_POLL_UNBOUND_WORKQUEUE, /* poll from unbound workqueue */
};

struct ib_cq {
	struct ib_device       *device;
	struct ib_uobject      *uobject;
	ib_comp_handler   	comp_handler;
	void                  (*event_handler)(struct ib_event *, void *);
	void                   *cq_context;
	int               	cqe;
	atomic_t          	usecnt; /* count number of work queues */
	enum ib_poll_context	poll_ctx;
	struct ib_wc		*wc;
	union {
		struct irq_poll		iop;
		struct work_struct	work;
	};
	struct workqueue_struct *comp_wq;
	struct dim *dim;
	/*
	 * Implementation details of the RDMA core, don't use in drivers:
	 */
	struct rdma_restrack_entry res;
};

struct ib_srq {
	struct ib_device       *device;
	struct ib_pd	       *pd;
	struct ib_uobject      *uobject;
	void		      (*event_handler)(struct ib_event *, void *);
	void		       *srq_context;
	enum ib_srq_type	srq_type;
	atomic_t		usecnt;

	struct {
		struct ib_cq   *cq;
		union {
			struct {
				struct ib_xrcd *xrcd;
				u32		srq_num;
			} xrc;
		};
	} ext;
};

enum ib_raw_packet_caps {
	/* Strip cvlan from incoming packet and report it in the matching work
	 * completion is supported.
	 */
	IB_RAW_PACKET_CAP_CVLAN_STRIPPING	= (1 << 0),
	/* Scatter FCS field of an incoming packet to host memory is supported.
	 */
	IB_RAW_PACKET_CAP_SCATTER_FCS		= (1 << 1),
	/* Checksum offloads are supported (for both send and receive). */
	IB_RAW_PACKET_CAP_IP_CSUM		= (1 << 2),
	/* When a packet is received for an RQ with no receive WQEs, the
	 * packet processing is delayed.
	 */
	IB_RAW_PACKET_CAP_DELAY_DROP		= (1 << 3),
};

enum ib_wq_type {
	IB_WQT_RQ
};

enum ib_wq_state {
	IB_WQS_RESET,
	IB_WQS_RDY,
	IB_WQS_ERR
};

struct ib_wq {
	struct ib_device       *device;
	struct ib_uobject      *uobject;
	void		    *wq_context;
	void		    (*event_handler)(struct ib_event *, void *);
	struct ib_pd	       *pd;
	struct ib_cq	       *cq;
	u32		wq_num;
	enum ib_wq_state       state;
	enum ib_wq_type	wq_type;
	atomic_t		usecnt;
};

enum ib_wq_flags {
	IB_WQ_FLAGS_CVLAN_STRIPPING	= 1 << 0,
	IB_WQ_FLAGS_SCATTER_FCS		= 1 << 1,
	IB_WQ_FLAGS_DELAY_DROP		= 1 << 2,
	IB_WQ_FLAGS_PCI_WRITE_END_PADDING = 1 << 3,
};

struct ib_wq_init_attr {
	void		       *wq_context;
	enum ib_wq_type	wq_type;
	u32		max_wr;
	u32		max_sge;
	struct	ib_cq	       *cq;
	void		    (*event_handler)(struct ib_event *, void *);
	u32		create_flags; /* Use enum ib_wq_flags */
};

enum ib_wq_attr_mask {
	IB_WQ_STATE		= 1 << 0,
	IB_WQ_CUR_STATE		= 1 << 1,
	IB_WQ_FLAGS		= 1 << 2,
};

struct ib_wq_attr {
	enum	ib_wq_state	wq_state;
	enum	ib_wq_state	curr_wq_state;
	u32			flags; /* Use enum ib_wq_flags */
	u32			flags_mask; /* Use enum ib_wq_flags */
};

struct ib_rwq_ind_table {
	struct ib_device	*device;
	struct ib_uobject      *uobject;
	atomic_t		usecnt;
	u32		ind_tbl_num;
	u32		log_ind_tbl_size;
	struct ib_wq	**ind_tbl;
};

struct ib_rwq_ind_table_init_attr {
	u32		log_ind_tbl_size;
	/* Each entry is a pointer to Receive Work Queue */
	struct ib_wq	**ind_tbl;
};

enum port_pkey_state {
	IB_PORT_PKEY_NOT_VALID = 0,
	IB_PORT_PKEY_VALID = 1,
	IB_PORT_PKEY_LISTED = 2,
};

struct ib_qp_security;

struct ib_port_pkey {
	enum port_pkey_state	state;
	u16			pkey_index;
	u8			port_num;
	struct list_head	qp_list;
	struct list_head	to_error_list;
	struct ib_qp_security  *sec;
};

struct ib_ports_pkeys {
	struct ib_port_pkey	main;
	struct ib_port_pkey	alt;
};

struct ib_qp_security {
	struct ib_qp	       *qp;
	struct ib_device       *dev;
	/* Hold this mutex when changing port and pkey settings. */
	struct mutex		mutex;
	struct ib_ports_pkeys  *ports_pkeys;
	/* A list of all open shared QP handles.  Required to enforce security
	 * properly for all users of a shared QP.
	 */
	struct list_head        shared_qp_list;
	void                   *security;
	bool			destroying;
	atomic_t		error_list_count;
	struct completion	error_complete;
	int			error_comps_pending;
};

/*
 * @max_write_sge: Maximum SGE elements per RDMA WRITE request.
 * @max_read_sge:  Maximum SGE elements per RDMA READ request.
 */
struct ib_qp {
	struct ib_device       *device;
	struct ib_pd	       *pd;
	struct ib_cq	       *send_cq;
	struct ib_cq	       *recv_cq;
	spinlock_t		mr_lock;
	int			mrs_used;
	struct list_head	rdma_mrs;
	struct list_head	sig_mrs;
	struct ib_srq	       *srq;
	struct ib_xrcd	       *xrcd; /* XRC TGT QPs only */
	struct list_head	xrcd_list;

	/* count times opened, mcast attaches, flow attaches */
	atomic_t		usecnt;
	struct list_head	open_list;
	struct ib_qp           *real_qp;
	struct ib_uobject      *uobject;
	void                  (*event_handler)(struct ib_event *, void *);
	void		       *qp_context;
	/* sgid_attrs associated with the AV's */
	const struct ib_gid_attr *av_sgid_attr;
	const struct ib_gid_attr *alt_path_sgid_attr;
	u32			qp_num;
	u32			max_write_sge;
	u32			max_read_sge;
	enum ib_qp_type		qp_type;
	struct ib_rwq_ind_table *rwq_ind_tbl;
	struct ib_qp_security  *qp_sec;
	u8			port;

	bool			integrity_en;
	/*
	 * Implementation details of the RDMA core, don't use in drivers:
	 */
	struct rdma_restrack_entry     res;

	/* The counter the qp is bind to */
	struct rdma_counter    *counter;
};

struct ib_dm {
	struct ib_device  *device;
	u32		   length;
	u32		   flags;
	struct ib_uobject *uobject;
	atomic_t	   usecnt;
};

struct ib_mr {
	struct ib_device  *device;
	struct ib_pd	  *pd;
	u32		   lkey;
	u32		   rkey;
	u64		   iova;
	u64		   length;
	unsigned int	   page_size;
	enum ib_mr_type	   type;
	bool		   need_inval;
	union {
		struct ib_uobject	*uobject;	/* user */
		struct list_head	qp_entry;	/* FR */
	};

	struct ib_dm      *dm;
	struct ib_sig_attrs *sig_attrs; /* only for IB_MR_TYPE_INTEGRITY MRs */
	/*
	 * Implementation details of the RDMA core, don't use in drivers:
	 */
	struct rdma_restrack_entry res;
};

struct ib_mw {
	struct ib_device	*device;
	struct ib_pd		*pd;
	struct ib_uobject	*uobject;
	u32			rkey;
	enum ib_mw_type         type;
};

struct ib_fmr {
	struct ib_device	*device;
	struct ib_pd		*pd;
	struct list_head	list;
	u32			lkey;
	u32			rkey;
};

/* Supported steering options */
enum ib_flow_attr_type {
	/* steering according to rule specifications */
	IB_FLOW_ATTR_NORMAL		= 0x0,
	/* default unicast and multicast rule -
	 * receive all Eth traffic which isn't steered to any QP
	 */
	IB_FLOW_ATTR_ALL_DEFAULT	= 0x1,
	/* default multicast rule -
	 * receive all Eth multicast traffic which isn't steered to any QP
	 */
	IB_FLOW_ATTR_MC_DEFAULT		= 0x2,
	/* sniffer rule - receive all port traffic */
	IB_FLOW_ATTR_SNIFFER		= 0x3
};

/* Supported steering header types */
enum ib_flow_spec_type {
	/* L2 headers*/
	IB_FLOW_SPEC_ETH		= 0x20,
	IB_FLOW_SPEC_IB			= 0x22,
	/* L3 header*/
	IB_FLOW_SPEC_IPV4		= 0x30,
	IB_FLOW_SPEC_IPV6		= 0x31,
	IB_FLOW_SPEC_ESP                = 0x34,
	/* L4 headers*/
	IB_FLOW_SPEC_TCP		= 0x40,
	IB_FLOW_SPEC_UDP		= 0x41,
	IB_FLOW_SPEC_VXLAN_TUNNEL	= 0x50,
	IB_FLOW_SPEC_GRE		= 0x51,
	IB_FLOW_SPEC_MPLS		= 0x60,
	IB_FLOW_SPEC_INNER		= 0x100,
	/* Actions */
	IB_FLOW_SPEC_ACTION_TAG         = 0x1000,
	IB_FLOW_SPEC_ACTION_DROP        = 0x1001,
	IB_FLOW_SPEC_ACTION_HANDLE	= 0x1002,
	IB_FLOW_SPEC_ACTION_COUNT       = 0x1003,
};
#define IB_FLOW_SPEC_LAYER_MASK	0xF0
#define IB_FLOW_SPEC_SUPPORT_LAYERS 10

/* Flow steering rule priority is set according to it's domain.
 * Lower domain value means higher priority.
 */
enum ib_flow_domain {
	IB_FLOW_DOMAIN_USER,
	IB_FLOW_DOMAIN_ETHTOOL,
	IB_FLOW_DOMAIN_RFS,
	IB_FLOW_DOMAIN_NIC,
	IB_FLOW_DOMAIN_NUM /* Must be last */
};

enum ib_flow_flags {
	IB_FLOW_ATTR_FLAGS_DONT_TRAP = 1UL << 1, /* Continue match, no steal */
	IB_FLOW_ATTR_FLAGS_EGRESS = 1UL << 2, /* Egress flow */
	IB_FLOW_ATTR_FLAGS_RESERVED  = 1UL << 3  /* Must be last */
};

struct ib_flow_eth_filter {
	u8	dst_mac[6];
	u8	src_mac[6];
	__be16	ether_type;
	__be16	vlan_tag;
	/* Must be last */
	u8	real_sz[0];
};

struct ib_flow_spec_eth {
	u32			  type;
	u16			  size;
	struct ib_flow_eth_filter val;
	struct ib_flow_eth_filter mask;
};

struct ib_flow_ib_filter {
	__be16 dlid;
	__u8   sl;
	/* Must be last */
	u8	real_sz[0];
};

struct ib_flow_spec_ib {
	u32			 type;
	u16			 size;
	struct ib_flow_ib_filter val;
	struct ib_flow_ib_filter mask;
};

/* IPv4 header flags */
enum ib_ipv4_flags {
	IB_IPV4_DONT_FRAG = 0x2, /* Don't enable packet fragmentation */
	IB_IPV4_MORE_FRAG = 0X4  /* For All fragmented packets except the
				    last have this flag set */
};

struct ib_flow_ipv4_filter {
	__be32	src_ip;
	__be32	dst_ip;
	u8	proto;
	u8	tos;
	u8	ttl;
	u8	flags;
	/* Must be last */
	u8	real_sz[0];
};

struct ib_flow_spec_ipv4 {
	u32			   type;
	u16			   size;
	struct ib_flow_ipv4_filter val;
	struct ib_flow_ipv4_filter mask;
};

struct ib_flow_ipv6_filter {
	u8	src_ip[16];
	u8	dst_ip[16];
	__be32	flow_label;
	u8	next_hdr;
	u8	traffic_class;
	u8	hop_limit;
	/* Must be last */
	u8	real_sz[0];
};

struct ib_flow_spec_ipv6 {
	u32			   type;
	u16			   size;
	struct ib_flow_ipv6_filter val;
	struct ib_flow_ipv6_filter mask;
};

struct ib_flow_tcp_udp_filter {
	__be16	dst_port;
	__be16	src_port;
	/* Must be last */
	u8	real_sz[0];
};

struct ib_flow_spec_tcp_udp {
	u32			      type;
	u16			      size;
	struct ib_flow_tcp_udp_filter val;
	struct ib_flow_tcp_udp_filter mask;
};

struct ib_flow_tunnel_filter {
	__be32	tunnel_id;
	u8	real_sz[0];
};

/* ib_flow_spec_tunnel describes the Vxlan tunnel
 * the tunnel_id from val has the vni value
 */
struct ib_flow_spec_tunnel {
	u32			      type;
	u16			      size;
	struct ib_flow_tunnel_filter  val;
	struct ib_flow_tunnel_filter  mask;
};

struct ib_flow_esp_filter {
	__be32	spi;
	__be32  seq;
	/* Must be last */
	u8	real_sz[0];
};

struct ib_flow_spec_esp {
	u32                           type;
	u16			      size;
	struct ib_flow_esp_filter     val;
	struct ib_flow_esp_filter     mask;
};

struct ib_flow_gre_filter {
	__be16 c_ks_res0_ver;
	__be16 protocol;
	__be32 key;
	/* Must be last */
	u8	real_sz[0];
};

struct ib_flow_spec_gre {
	u32                           type;
	u16			      size;
	struct ib_flow_gre_filter     val;
	struct ib_flow_gre_filter     mask;
};

struct ib_flow_mpls_filter {
	__be32 tag;
	/* Must be last */
	u8	real_sz[0];
};

struct ib_flow_spec_mpls {
	u32                           type;
	u16			      size;
	struct ib_flow_mpls_filter     val;
	struct ib_flow_mpls_filter     mask;
};

struct ib_flow_spec_action_tag {
	enum ib_flow_spec_type	      type;
	u16			      size;
	u32                           tag_id;
};

struct ib_flow_spec_action_drop {
	enum ib_flow_spec_type	      type;
	u16			      size;
};

struct ib_flow_spec_action_handle {
	enum ib_flow_spec_type	      type;
	u16			      size;
	struct ib_flow_action	     *act;
};

enum ib_counters_description {
	IB_COUNTER_PACKETS,
	IB_COUNTER_BYTES,
};

struct ib_flow_spec_action_count {
	enum ib_flow_spec_type type;
	u16 size;
	struct ib_counters *counters;
};

union ib_flow_spec {
	struct {
		u32			type;
		u16			size;
	};
	struct ib_flow_spec_eth		eth;
	struct ib_flow_spec_ib		ib;
	struct ib_flow_spec_ipv4        ipv4;
	struct ib_flow_spec_tcp_udp	tcp_udp;
	struct ib_flow_spec_ipv6        ipv6;
	struct ib_flow_spec_tunnel      tunnel;
	struct ib_flow_spec_esp		esp;
	struct ib_flow_spec_gre		gre;
	struct ib_flow_spec_mpls	mpls;
	struct ib_flow_spec_action_tag  flow_tag;
	struct ib_flow_spec_action_drop drop;
	struct ib_flow_spec_action_handle action;
	struct ib_flow_spec_action_count flow_count;
};

struct ib_flow_attr {
	enum ib_flow_attr_type type;
	u16	     size;
	u16	     priority;
	u32	     flags;
	u8	     num_of_specs;
	u8	     port;
	union ib_flow_spec flows[];
};

struct ib_flow {
	struct ib_qp		*qp;
	struct ib_device	*device;
	struct ib_uobject	*uobject;
};

enum ib_flow_action_type {
	IB_FLOW_ACTION_UNSPECIFIED,
	IB_FLOW_ACTION_ESP = 1,
};

struct ib_flow_action_attrs_esp_keymats {
	enum ib_uverbs_flow_action_esp_keymat			protocol;
	union {
		struct ib_uverbs_flow_action_esp_keymat_aes_gcm aes_gcm;
	} keymat;
};

struct ib_flow_action_attrs_esp_replays {
	enum ib_uverbs_flow_action_esp_replay			protocol;
	union {
		struct ib_uverbs_flow_action_esp_replay_bmp	bmp;
	} replay;
};

enum ib_flow_action_attrs_esp_flags {
	/* All user-space flags at the top: Use enum ib_uverbs_flow_action_esp_flags
	 * This is done in order to share the same flags between user-space and
	 * kernel and spare an unnecessary translation.
	 */

	/* Kernel flags */
	IB_FLOW_ACTION_ESP_FLAGS_ESN_TRIGGERED	= 1ULL << 32,
	IB_FLOW_ACTION_ESP_FLAGS_MOD_ESP_ATTRS	= 1ULL << 33,
};

struct ib_flow_spec_list {
	struct ib_flow_spec_list	*next;
	union ib_flow_spec		spec;
};

struct ib_flow_action_attrs_esp {
	struct ib_flow_action_attrs_esp_keymats		*keymat;
	struct ib_flow_action_attrs_esp_replays		*replay;
	struct ib_flow_spec_list			*encap;
	/* Used only if IB_FLOW_ACTION_ESP_FLAGS_ESN_TRIGGERED is enabled.
	 * Value of 0 is a valid value.
	 */
	u32						esn;
	u32						spi;
	u32						seq;
	u32						tfc_pad;
	/* Use enum ib_flow_action_attrs_esp_flags */
	u64						flags;
	u64						hard_limit_pkts;
};

struct ib_flow_action {
	struct ib_device		*device;
	struct ib_uobject		*uobject;
	enum ib_flow_action_type	type;
	atomic_t			usecnt;
};

struct ib_mad_hdr;
struct ib_grh;

enum ib_process_mad_flags {
	IB_MAD_IGNORE_MKEY	= 1,
	IB_MAD_IGNORE_BKEY	= 2,
	IB_MAD_IGNORE_ALL	= IB_MAD_IGNORE_MKEY | IB_MAD_IGNORE_BKEY
};

enum ib_mad_result {
	IB_MAD_RESULT_FAILURE  = 0,      /* (!SUCCESS is the important flag) */
	IB_MAD_RESULT_SUCCESS  = 1 << 0, /* MAD was successfully processed   */
	IB_MAD_RESULT_REPLY    = 1 << 1, /* Reply packet needs to be sent    */
	IB_MAD_RESULT_CONSUMED = 1 << 2  /* Packet consumed: stop processing */
};

struct ib_port_cache {
	u64		      subnet_prefix;
	struct ib_pkey_cache  *pkey;
	struct ib_gid_table   *gid;
	u8                     lmc;
	enum ib_port_state     port_state;
};

struct ib_cache {
	rwlock_t                lock;
	struct ib_event_handler event_handler;
};

struct ib_port_immutable {
	int                           pkey_tbl_len;
	int                           gid_tbl_len;
	u32                           core_cap_flags;
	u32                           max_mad_size;
};

struct ib_port_data {
	struct ib_device *ib_dev;

	struct ib_port_immutable immutable;

	spinlock_t pkey_list_lock;
	struct list_head pkey_list;

	struct ib_port_cache cache;

	spinlock_t netdev_lock;
	struct net_device __rcu *netdev;
	struct hlist_node ndev_hash_link;
	struct rdma_port_counter port_counter;
	struct rdma_hw_stats *hw_stats;
};

/* rdma netdev type - specifies protocol type */
enum rdma_netdev_t {
	RDMA_NETDEV_OPA_VNIC,
	RDMA_NETDEV_IPOIB,
};

/**
 * struct rdma_netdev - rdma netdev
 * For cases where netstack interfacing is required.
 */
struct rdma_netdev {
	void              *clnt_priv;
	struct ib_device  *hca;
	u8                 port_num;

	/*
	 * cleanup function must be specified.
	 * FIXME: This is only used for OPA_VNIC and that usage should be
	 * removed too.
	 */
	void (*free_rdma_netdev)(struct net_device *netdev);

	/* control functions */
	void (*set_id)(struct net_device *netdev, int id);
	/* send packet */
	int (*send)(struct net_device *dev, struct sk_buff *skb,
		    struct ib_ah *address, u32 dqpn);
	/* multicast */
	int (*attach_mcast)(struct net_device *dev, struct ib_device *hca,
			    union ib_gid *gid, u16 mlid,
			    int set_qkey, u32 qkey);
	int (*detach_mcast)(struct net_device *dev, struct ib_device *hca,
			    union ib_gid *gid, u16 mlid);
};

struct rdma_netdev_alloc_params {
	size_t sizeof_priv;
	unsigned int txqs;
	unsigned int rxqs;
	void *param;

	int (*initialize_rdma_netdev)(struct ib_device *device, u8 port_num,
				      struct net_device *netdev, void *param);
};

struct ib_counters {
	struct ib_device	*device;
	struct ib_uobject	*uobject;
	/* num of objects attached */
	atomic_t	usecnt;
};

struct ib_counters_read_attr {
	u64	*counters_buff;
	u32	ncounters;
	u32	flags; /* use enum ib_read_counters_flags */
};

struct uverbs_attr_bundle;
struct iw_cm_id;
struct iw_cm_conn_param;

#define INIT_RDMA_OBJ_SIZE(ib_struct, drv_struct, member)                      \
	.size_##ib_struct =                                                    \
		(sizeof(struct drv_struct) +                                   \
		 BUILD_BUG_ON_ZERO(offsetof(struct drv_struct, member)) +      \
		 BUILD_BUG_ON_ZERO(                                            \
			 !__same_type(((struct drv_struct *)NULL)->member,     \
				      struct ib_struct)))

#define rdma_zalloc_drv_obj_gfp(ib_dev, ib_type, gfp)                         \
	((struct ib_type *)kzalloc(ib_dev->ops.size_##ib_type, gfp))

#define rdma_zalloc_drv_obj(ib_dev, ib_type)                                   \
	rdma_zalloc_drv_obj_gfp(ib_dev, ib_type, GFP_KERNEL)

#define DECLARE_RDMA_OBJ_SIZE(ib_struct) size_t size_##ib_struct

/**
 * struct ib_device_ops - InfiniBand device operations
 * This structure defines all the InfiniBand device operations, providers will
 * need to define the supported operations, otherwise they will be set to null.
 */
struct ib_device_ops {
	struct module *owner;
	enum rdma_driver_id driver_id;
	u32 uverbs_abi_ver;
	unsigned int uverbs_no_driver_id_binding:1;

	int (*post_send)(struct ib_qp *qp, const struct ib_send_wr *send_wr,
			 const struct ib_send_wr **bad_send_wr);
	int (*post_recv)(struct ib_qp *qp, const struct ib_recv_wr *recv_wr,
			 const struct ib_recv_wr **bad_recv_wr);
	void (*drain_rq)(struct ib_qp *qp);
	void (*drain_sq)(struct ib_qp *qp);
	int (*poll_cq)(struct ib_cq *cq, int num_entries, struct ib_wc *wc);
	int (*peek_cq)(struct ib_cq *cq, int wc_cnt);
	int (*req_notify_cq)(struct ib_cq *cq, enum ib_cq_notify_flags flags);
	int (*req_ncomp_notif)(struct ib_cq *cq, int wc_cnt);
	int (*post_srq_recv)(struct ib_srq *srq,
			     const struct ib_recv_wr *recv_wr,
			     const struct ib_recv_wr **bad_recv_wr);
	int (*process_mad)(struct ib_device *device, int process_mad_flags,
			   u8 port_num, const struct ib_wc *in_wc,
			   const struct ib_grh *in_grh,
			   const struct ib_mad_hdr *in_mad, size_t in_mad_size,
			   struct ib_mad_hdr *out_mad, size_t *out_mad_size,
			   u16 *out_mad_pkey_index);
	int (*query_device)(struct ib_device *device,
			    struct ib_device_attr *device_attr,
			    struct ib_udata *udata);
	int (*modify_device)(struct ib_device *device, int device_modify_mask,
			     struct ib_device_modify *device_modify);
	void (*get_dev_fw_str)(struct ib_device *device, char *str);
	const struct cpumask *(*get_vector_affinity)(struct ib_device *ibdev,
						     int comp_vector);
	int (*query_port)(struct ib_device *device, u8 port_num,
			  struct ib_port_attr *port_attr);
	int (*modify_port)(struct ib_device *device, u8 port_num,
			   int port_modify_mask,
			   struct ib_port_modify *port_modify);
	/**
	 * The following mandatory functions are used only at device
	 * registration.  Keep functions such as these at the end of this
	 * structure to avoid cache line misses when accessing struct ib_device
	 * in fast paths.
	 */
	int (*get_port_immutable)(struct ib_device *device, u8 port_num,
				  struct ib_port_immutable *immutable);
	enum rdma_link_layer (*get_link_layer)(struct ib_device *device,
					       u8 port_num);
	/**
	 * When calling get_netdev, the HW vendor's driver should return the
	 * net device of device @device at port @port_num or NULL if such
	 * a net device doesn't exist. The vendor driver should call dev_hold
	 * on this net device. The HW vendor's device driver must guarantee
	 * that this function returns NULL before the net device has finished
	 * NETDEV_UNREGISTER state.
	 */
	struct net_device *(*get_netdev)(struct ib_device *device, u8 port_num);
	/**
	 * rdma netdev operation
	 *
	 * Driver implementing alloc_rdma_netdev or rdma_netdev_get_params
	 * must return -EOPNOTSUPP if it doesn't support the specified type.
	 */
	struct net_device *(*alloc_rdma_netdev)(
		struct ib_device *device, u8 port_num, enum rdma_netdev_t type,
		const char *name, unsigned char name_assign_type,
		void (*setup)(struct net_device *));

	int (*rdma_netdev_get_params)(struct ib_device *device, u8 port_num,
				      enum rdma_netdev_t type,
				      struct rdma_netdev_alloc_params *params);
	/**
	 * query_gid should be return GID value for @device, when @port_num
	 * link layer is either IB or iWarp. It is no-op if @port_num port
	 * is RoCE link layer.
	 */
	int (*query_gid)(struct ib_device *device, u8 port_num, int index,
			 union ib_gid *gid);
	/**
	 * When calling add_gid, the HW vendor's driver should add the gid
	 * of device of port at gid index available at @attr. Meta-info of
	 * that gid (for example, the network device related to this gid) is
	 * available at @attr. @context allows the HW vendor driver to store
	 * extra information together with a GID entry. The HW vendor driver may
	 * allocate memory to contain this information and store it in @context
	 * when a new GID entry is written to. Params are consistent until the
	 * next call of add_gid or delete_gid. The function should return 0 on
	 * success or error otherwise. The function could be called
	 * concurrently for different ports. This function is only called when
	 * roce_gid_table is used.
	 */
	int (*add_gid)(const struct ib_gid_attr *attr, void **context);
	/**
	 * When calling del_gid, the HW vendor's driver should delete the
	 * gid of device @device at gid index gid_index of port port_num
	 * available in @attr.
	 * Upon the deletion of a GID entry, the HW vendor must free any
	 * allocated memory. The caller will clear @context afterwards.
	 * This function is only called when roce_gid_table is used.
	 */
	int (*del_gid)(const struct ib_gid_attr *attr, void **context);
	int (*query_pkey)(struct ib_device *device, u8 port_num, u16 index,
			  u16 *pkey);
	int (*alloc_ucontext)(struct ib_ucontext *context,
			      struct ib_udata *udata);
	void (*dealloc_ucontext)(struct ib_ucontext *context);
	int (*mmap)(struct ib_ucontext *context, struct vm_area_struct *vma);
	void (*disassociate_ucontext)(struct ib_ucontext *ibcontext);
	int (*alloc_pd)(struct ib_pd *pd, struct ib_udata *udata);
	void (*dealloc_pd)(struct ib_pd *pd, struct ib_udata *udata);
	int (*create_ah)(struct ib_ah *ah, struct rdma_ah_attr *ah_attr,
			 u32 flags, struct ib_udata *udata);
	int (*modify_ah)(struct ib_ah *ah, struct rdma_ah_attr *ah_attr);
	int (*query_ah)(struct ib_ah *ah, struct rdma_ah_attr *ah_attr);
	void (*destroy_ah)(struct ib_ah *ah, u32 flags);
	int (*create_srq)(struct ib_srq *srq,
			  struct ib_srq_init_attr *srq_init_attr,
			  struct ib_udata *udata);
	int (*modify_srq)(struct ib_srq *srq, struct ib_srq_attr *srq_attr,
			  enum ib_srq_attr_mask srq_attr_mask,
			  struct ib_udata *udata);
	int (*query_srq)(struct ib_srq *srq, struct ib_srq_attr *srq_attr);
	void (*destroy_srq)(struct ib_srq *srq, struct ib_udata *udata);
	struct ib_qp *(*create_qp)(struct ib_pd *pd,
				   struct ib_qp_init_attr *qp_init_attr,
				   struct ib_udata *udata);
	int (*modify_qp)(struct ib_qp *qp, struct ib_qp_attr *qp_attr,
			 int qp_attr_mask, struct ib_udata *udata);
	int (*query_qp)(struct ib_qp *qp, struct ib_qp_attr *qp_attr,
			int qp_attr_mask, struct ib_qp_init_attr *qp_init_attr);
	int (*destroy_qp)(struct ib_qp *qp, struct ib_udata *udata);
	int (*create_cq)(struct ib_cq *cq, const struct ib_cq_init_attr *attr,
			 struct ib_udata *udata);
	int (*modify_cq)(struct ib_cq *cq, u16 cq_count, u16 cq_period);
	void (*destroy_cq)(struct ib_cq *cq, struct ib_udata *udata);
	int (*resize_cq)(struct ib_cq *cq, int cqe, struct ib_udata *udata);
	struct ib_mr *(*get_dma_mr)(struct ib_pd *pd, int mr_access_flags);
	struct ib_mr *(*reg_user_mr)(struct ib_pd *pd, u64 start, u64 length,
				     u64 virt_addr, int mr_access_flags,
				     struct ib_udata *udata);
	int (*rereg_user_mr)(struct ib_mr *mr, int flags, u64 start, u64 length,
			     u64 virt_addr, int mr_access_flags,
			     struct ib_pd *pd, struct ib_udata *udata);
	int (*dereg_mr)(struct ib_mr *mr, struct ib_udata *udata);
	struct ib_mr *(*alloc_mr)(struct ib_pd *pd, enum ib_mr_type mr_type,
				  u32 max_num_sg, struct ib_udata *udata);
	struct ib_mr *(*alloc_mr_integrity)(struct ib_pd *pd,
					    u32 max_num_data_sg,
					    u32 max_num_meta_sg);
	int (*advise_mr)(struct ib_pd *pd,
			 enum ib_uverbs_advise_mr_advice advice, u32 flags,
			 struct ib_sge *sg_list, u32 num_sge,
			 struct uverbs_attr_bundle *attrs);
	int (*map_mr_sg)(struct ib_mr *mr, struct scatterlist *sg, int sg_nents,
			 unsigned int *sg_offset);
	int (*check_mr_status)(struct ib_mr *mr, u32 check_mask,
			       struct ib_mr_status *mr_status);
	struct ib_mw *(*alloc_mw)(struct ib_pd *pd, enum ib_mw_type type,
				  struct ib_udata *udata);
	int (*dealloc_mw)(struct ib_mw *mw);
	struct ib_fmr *(*alloc_fmr)(struct ib_pd *pd, int mr_access_flags,
				    struct ib_fmr_attr *fmr_attr);
	int (*map_phys_fmr)(struct ib_fmr *fmr, u64 *page_list, int list_len,
			    u64 iova);
	int (*unmap_fmr)(struct list_head *fmr_list);
	int (*dealloc_fmr)(struct ib_fmr *fmr);
	int (*attach_mcast)(struct ib_qp *qp, union ib_gid *gid, u16 lid);
	int (*detach_mcast)(struct ib_qp *qp, union ib_gid *gid, u16 lid);
	struct ib_xrcd *(*alloc_xrcd)(struct ib_device *device,
				      struct ib_udata *udata);
	int (*dealloc_xrcd)(struct ib_xrcd *xrcd, struct ib_udata *udata);
	struct ib_flow *(*create_flow)(struct ib_qp *qp,
				       struct ib_flow_attr *flow_attr,
				       int domain, struct ib_udata *udata);
	int (*destroy_flow)(struct ib_flow *flow_id);
	struct ib_flow_action *(*create_flow_action_esp)(
		struct ib_device *device,
		const struct ib_flow_action_attrs_esp *attr,
		struct uverbs_attr_bundle *attrs);
	int (*destroy_flow_action)(struct ib_flow_action *action);
	int (*modify_flow_action_esp)(
		struct ib_flow_action *action,
		const struct ib_flow_action_attrs_esp *attr,
		struct uverbs_attr_bundle *attrs);
	int (*set_vf_link_state)(struct ib_device *device, int vf, u8 port,
				 int state);
	int (*get_vf_config)(struct ib_device *device, int vf, u8 port,
			     struct ifla_vf_info *ivf);
	int (*get_vf_stats)(struct ib_device *device, int vf, u8 port,
			    struct ifla_vf_stats *stats);
	int (*set_vf_guid)(struct ib_device *device, int vf, u8 port, u64 guid,
			   int type);
	struct ib_wq *(*create_wq)(struct ib_pd *pd,
				   struct ib_wq_init_attr *init_attr,
				   struct ib_udata *udata);
	void (*destroy_wq)(struct ib_wq *wq, struct ib_udata *udata);
	int (*modify_wq)(struct ib_wq *wq, struct ib_wq_attr *attr,
			 u32 wq_attr_mask, struct ib_udata *udata);
	struct ib_rwq_ind_table *(*create_rwq_ind_table)(
		struct ib_device *device,
		struct ib_rwq_ind_table_init_attr *init_attr,
		struct ib_udata *udata);
	int (*destroy_rwq_ind_table)(struct ib_rwq_ind_table *wq_ind_table);
	struct ib_dm *(*alloc_dm)(struct ib_device *device,
				  struct ib_ucontext *context,
				  struct ib_dm_alloc_attr *attr,
				  struct uverbs_attr_bundle *attrs);
	int (*dealloc_dm)(struct ib_dm *dm, struct uverbs_attr_bundle *attrs);
	struct ib_mr *(*reg_dm_mr)(struct ib_pd *pd, struct ib_dm *dm,
				   struct ib_dm_mr_attr *attr,
				   struct uverbs_attr_bundle *attrs);
	struct ib_counters *(*create_counters)(
		struct ib_device *device, struct uverbs_attr_bundle *attrs);
	int (*destroy_counters)(struct ib_counters *counters);
	int (*read_counters)(struct ib_counters *counters,
			     struct ib_counters_read_attr *counters_read_attr,
			     struct uverbs_attr_bundle *attrs);
	int (*map_mr_sg_pi)(struct ib_mr *mr, struct scatterlist *data_sg,
			    int data_sg_nents, unsigned int *data_sg_offset,
			    struct scatterlist *meta_sg, int meta_sg_nents,
			    unsigned int *meta_sg_offset);

	/**
	 * alloc_hw_stats - Allocate a struct rdma_hw_stats and fill in the
	 *   driver initialized data.  The struct is kfree()'ed by the sysfs
	 *   core when the device is removed.  A lifespan of -1 in the return
	 *   struct tells the core to set a default lifespan.
	 */
	struct rdma_hw_stats *(*alloc_hw_stats)(struct ib_device *device,
						u8 port_num);
	/**
	 * get_hw_stats - Fill in the counter value(s) in the stats struct.
	 * @index - The index in the value array we wish to have updated, or
	 *   num_counters if we want all stats updated
	 * Return codes -
	 *   < 0 - Error, no counters updated
	 *   index - Updated the single counter pointed to by index
	 *   num_counters - Updated all counters (will reset the timestamp
	 *     and prevent further calls for lifespan milliseconds)
	 * Drivers are allowed to update all counters in leiu of just the
	 *   one given in index at their option
	 */
	int (*get_hw_stats)(struct ib_device *device,
			    struct rdma_hw_stats *stats, u8 port, int index);
	/*
	 * This function is called once for each port when a ib device is
	 * registered.
	 */
	int (*init_port)(struct ib_device *device, u8 port_num,
			 struct kobject *port_sysfs);
	/**
	 * Allows rdma drivers to add their own restrack attributes.
	 */
	int (*fill_res_entry)(struct sk_buff *msg,
			      struct rdma_restrack_entry *entry);

	/* Device lifecycle callbacks */
	/*
	 * Called after the device becomes registered, before clients are
	 * attached
	 */
	int (*enable_driver)(struct ib_device *dev);
	/*
	 * This is called as part of ib_dealloc_device().
	 */
	void (*dealloc_driver)(struct ib_device *dev);

	/* iWarp CM callbacks */
	void (*iw_add_ref)(struct ib_qp *qp);
	void (*iw_rem_ref)(struct ib_qp *qp);
	struct ib_qp *(*iw_get_qp)(struct ib_device *device, int qpn);
	int (*iw_connect)(struct iw_cm_id *cm_id,
			  struct iw_cm_conn_param *conn_param);
	int (*iw_accept)(struct iw_cm_id *cm_id,
			 struct iw_cm_conn_param *conn_param);
	int (*iw_reject)(struct iw_cm_id *cm_id, const void *pdata,
			 u8 pdata_len);
	int (*iw_create_listen)(struct iw_cm_id *cm_id, int backlog);
	int (*iw_destroy_listen)(struct iw_cm_id *cm_id);
	/**
	 * counter_bind_qp - Bind a QP to a counter.
	 * @counter - The counter to be bound. If counter->id is zero then
	 *   the driver needs to allocate a new counter and set counter->id
	 */
	int (*counter_bind_qp)(struct rdma_counter *counter, struct ib_qp *qp);
	/**
	 * counter_unbind_qp - Unbind the qp from the dynamically-allocated
	 *   counter and bind it onto the default one
	 */
	int (*counter_unbind_qp)(struct ib_qp *qp);
	/**
	 * counter_dealloc -De-allocate the hw counter
	 */
	int (*counter_dealloc)(struct rdma_counter *counter);
	/**
	 * counter_alloc_stats - Allocate a struct rdma_hw_stats and fill in
	 * the driver initialized data.
	 */
	struct rdma_hw_stats *(*counter_alloc_stats)(
		struct rdma_counter *counter);
	/**
	 * counter_update_stats - Query the stats value of this counter
	 */
	int (*counter_update_stats)(struct rdma_counter *counter);

	DECLARE_RDMA_OBJ_SIZE(ib_ah);
	DECLARE_RDMA_OBJ_SIZE(ib_cq);
	DECLARE_RDMA_OBJ_SIZE(ib_pd);
	DECLARE_RDMA_OBJ_SIZE(ib_srq);
	DECLARE_RDMA_OBJ_SIZE(ib_ucontext);
};

struct ib_core_device {
	/* device must be the first element in structure until,
	 * union of ib_core_device and device exists in ib_device.
	 */
	struct device dev;
	possible_net_t rdma_net;
	struct kobject *ports_kobj;
	struct list_head port_list;
	struct ib_device *owner; /* reach back to owner ib_device */
};

struct rdma_restrack_root;
struct ib_device {
	/* Do not access @dma_device directly from ULP nor from HW drivers. */
	struct device                *dma_device;
	struct ib_device_ops	     ops;
	char                          name[IB_DEVICE_NAME_MAX];
	struct rcu_head rcu_head;

	struct list_head              event_handler_list;
	spinlock_t                    event_handler_lock;

	struct rw_semaphore	      client_data_rwsem;
	struct xarray                 client_data;
	struct mutex                  unregistration_lock;

	struct ib_cache               cache;
	/**
	 * port_data is indexed by port number
	 */
	struct ib_port_data *port_data;

	int			      num_comp_vectors;

	union {
		struct device		dev;
		struct ib_core_device	coredev;
	};

	/* First group for device attributes,
	 * Second group for driver provided attributes (optional).
	 * It is NULL terminated array.
	 */
	const struct attribute_group	*groups[3];

	u64			     uverbs_cmd_mask;
	u64			     uverbs_ex_cmd_mask;

	char			     node_desc[IB_DEVICE_NODE_DESC_MAX];
	__be64			     node_guid;
	u32			     local_dma_lkey;
	u16                          is_switch:1;
	/* Indicates kernel verbs support, should not be used in drivers */
	u16                          kverbs_provider:1;
	/* CQ adaptive moderation (RDMA DIM) */
	u16                          use_cq_dim:1;
	u8                           node_type;
	u8                           phys_port_cnt;
	struct ib_device_attr        attrs;
	struct attribute_group	     *hw_stats_ag;
	struct rdma_hw_stats         *hw_stats;

#ifdef CONFIG_CGROUP_RDMA
	struct rdmacg_device         cg_device;
#endif

	u32                          index;
	struct rdma_restrack_root *res;

	const struct uapi_definition   *driver_def;

	/*
	 * Positive refcount indicates that the device is currently
	 * registered and cannot be unregistered.
	 */
	refcount_t refcount;
	struct completion unreg_completion;
	struct work_struct unregistration_work;

	const struct rdma_link_ops *link_ops;

	/* Protects compat_devs xarray modifications */
	struct mutex compat_devs_mutex;
	/* Maintains compat devices for each net namespace */
	struct xarray compat_devs;

	/* Used by iWarp CM */
	char iw_ifname[IFNAMSIZ];
	u32 iw_driver_flags;
};

struct ib_client_nl_info;
struct ib_client {
	const char *name;
	void (*add)   (struct ib_device *);
	void (*remove)(struct ib_device *, void *client_data);
	void (*rename)(struct ib_device *dev, void *client_data);
	int (*get_nl_info)(struct ib_device *ibdev, void *client_data,
			   struct ib_client_nl_info *res);
	int (*get_global_nl_info)(struct ib_client_nl_info *res);

	/* Returns the net_dev belonging to this ib_client and matching the
	 * given parameters.
	 * @dev:	 An RDMA device that the net_dev use for communication.
	 * @port:	 A physical port number on the RDMA device.
	 * @pkey:	 P_Key that the net_dev uses if applicable.
	 * @gid:	 A GID that the net_dev uses to communicate.
	 * @addr:	 An IP address the net_dev is configured with.
	 * @client_data: The device's client data set by ib_set_client_data().
	 *
	 * An ib_client that implements a net_dev on top of RDMA devices
	 * (such as IP over IB) should implement this callback, allowing the
	 * rdma_cm module to find the right net_dev for a given request.
	 *
	 * The caller is responsible for calling dev_put on the returned
	 * netdev. */
	struct net_device *(*get_net_dev_by_params)(
			struct ib_device *dev,
			u8 port,
			u16 pkey,
			const union ib_gid *gid,
			const struct sockaddr *addr,
			void *client_data);

	refcount_t uses;
	struct completion uses_zero;
	u32 client_id;

	/* kverbs are not required by the client */
	u8 no_kverbs_req:1;
};

/*
 * IB block DMA iterator
 *
 * Iterates the DMA-mapped SGL in contiguous memory blocks aligned
 * to a HW supported page size.
 */
struct ib_block_iter {
	/* internal states */
	struct scatterlist *__sg;	/* sg holding the current aligned block */
	dma_addr_t __dma_addr;		/* unaligned DMA address of this block */
	unsigned int __sg_nents;	/* number of SG entries */
	unsigned int __sg_advance;	/* number of bytes to advance in sg in next step */
	unsigned int __pg_bit;		/* alignment of current block */
};

struct ib_device *_ib_alloc_device(size_t size);
#define ib_alloc_device(drv_struct, member)                                    \
	container_of(_ib_alloc_device(sizeof(struct drv_struct) +              \
				      BUILD_BUG_ON_ZERO(offsetof(              \
					      struct drv_struct, member))),    \
		     struct drv_struct, member)

void ib_dealloc_device(struct ib_device *device);

void ib_get_device_fw_str(struct ib_device *device, char *str);

int ib_register_device(struct ib_device *device, const char *name);
void ib_unregister_device(struct ib_device *device);
void ib_unregister_driver(enum rdma_driver_id driver_id);
void ib_unregister_device_and_put(struct ib_device *device);
void ib_unregister_device_queued(struct ib_device *ib_dev);

int ib_register_client   (struct ib_client *client);
void ib_unregister_client(struct ib_client *client);

void __rdma_block_iter_start(struct ib_block_iter *biter,
			     struct scatterlist *sglist,
			     unsigned int nents,
			     unsigned long pgsz);
bool __rdma_block_iter_next(struct ib_block_iter *biter);

/**
 * rdma_block_iter_dma_address - get the aligned dma address of the current
 * block held by the block iterator.
 * @biter: block iterator holding the memory block
 */
static inline dma_addr_t
rdma_block_iter_dma_address(struct ib_block_iter *biter)
{
	return biter->__dma_addr & ~(BIT_ULL(biter->__pg_bit) - 1);
}

/**
 * rdma_for_each_block - iterate over contiguous memory blocks of the sg list
 * @sglist: sglist to iterate over
 * @biter: block iterator holding the memory block
 * @nents: maximum number of sg entries to iterate over
 * @pgsz: best HW supported page size to use
 *
 * Callers may use rdma_block_iter_dma_address() to get each
 * blocks aligned DMA address.
 */
#define rdma_for_each_block(sglist, biter, nents, pgsz)		\
	for (__rdma_block_iter_start(biter, sglist, nents,	\
				     pgsz);			\
	     __rdma_block_iter_next(biter);)

/**
 * ib_get_client_data - Get IB client context
 * @device:Device to get context for
 * @client:Client to get context for
 *
 * ib_get_client_data() returns the client context data set with
 * ib_set_client_data(). This can only be called while the client is
 * registered to the device, once the ib_client remove() callback returns this
 * cannot be called.
 */
static inline void *ib_get_client_data(struct ib_device *device,
				       struct ib_client *client)
{
	return xa_load(&device->client_data, client->client_id);
}
void  ib_set_client_data(struct ib_device *device, struct ib_client *client,
			 void *data);
void ib_set_device_ops(struct ib_device *device,
		       const struct ib_device_ops *ops);

#if IS_ENABLED(CONFIG_INFINIBAND_USER_ACCESS)
int rdma_user_mmap_io(struct ib_ucontext *ucontext, struct vm_area_struct *vma,
		      unsigned long pfn, unsigned long size, pgprot_t prot);
#else
static inline int rdma_user_mmap_io(struct ib_ucontext *ucontext,
				    struct vm_area_struct *vma,
				    unsigned long pfn, unsigned long size,
				    pgprot_t prot)
{
	return -EINVAL;
}
#endif

static inline int ib_copy_from_udata(void *dest, struct ib_udata *udata, size_t len)
{
	return copy_from_user(dest, udata->inbuf, len) ? -EFAULT : 0;
}

static inline int ib_copy_to_udata(struct ib_udata *udata, void *src, size_t len)
{
	return copy_to_user(udata->outbuf, src, len) ? -EFAULT : 0;
}

static inline bool ib_is_buffer_cleared(const void __user *p,
					size_t len)
{
	bool ret;
	u8 *buf;

	if (len > USHRT_MAX)
		return false;

	buf = memdup_user(p, len);
	if (IS_ERR(buf))
		return false;

	ret = !memchr_inv(buf, 0, len);
	kfree(buf);
	return ret;
}

static inline bool ib_is_udata_cleared(struct ib_udata *udata,
				       size_t offset,
				       size_t len)
{
	return ib_is_buffer_cleared(udata->inbuf + offset, len);
}

/**
 * ib_is_destroy_retryable - Check whether the uobject destruction
 * is retryable.
 * @ret: The initial destruction return code
 * @why: remove reason
 * @uobj: The uobject that is destroyed
 *
 * This function is a helper function that IB layer and low-level drivers
 * can use to consider whether the destruction of the given uobject is
 * retry-able.
 * It checks the original return code, if it wasn't success the destruction
 * is retryable according to the ucontext state (i.e. cleanup_retryable) and
 * the remove reason. (i.e. why).
 * Must be called with the object locked for destroy.
 */
static inline bool ib_is_destroy_retryable(int ret, enum rdma_remove_reason why,
					   struct ib_uobject *uobj)
{
	return ret && (why == RDMA_REMOVE_DESTROY ||
		       uobj->context->cleanup_retryable);
}

/**
 * ib_destroy_usecnt - Called during destruction to check the usecnt
 * @usecnt: The usecnt atomic
 * @why: remove reason
 * @uobj: The uobject that is destroyed
 *
 * Non-zero usecnts will block destruction unless destruction was triggered by
 * a ucontext cleanup.
 */
static inline int ib_destroy_usecnt(atomic_t *usecnt,
				    enum rdma_remove_reason why,
				    struct ib_uobject *uobj)
{
	if (atomic_read(usecnt) && ib_is_destroy_retryable(-EBUSY, why, uobj))
		return -EBUSY;
	return 0;
}

/**
 * ib_modify_qp_is_ok - Check that the supplied attribute mask
 * contains all required attributes and no attributes not allowed for
 * the given QP state transition.
 * @cur_state: Current QP state
 * @next_state: Next QP state
 * @type: QP type
 * @mask: Mask of supplied QP attributes
 *
 * This function is a helper function that a low-level driver's
 * modify_qp method can use to validate the consumer's input.  It
 * checks that cur_state and next_state are valid QP states, that a
 * transition from cur_state to next_state is allowed by the IB spec,
 * and that the attribute mask supplied is allowed for the transition.
 */
bool ib_modify_qp_is_ok(enum ib_qp_state cur_state, enum ib_qp_state next_state,
			enum ib_qp_type type, enum ib_qp_attr_mask mask);

void ib_register_event_handler(struct ib_event_handler *event_handler);
void ib_unregister_event_handler(struct ib_event_handler *event_handler);
void ib_dispatch_event(struct ib_event *event);

int ib_query_port(struct ib_device *device,
		  u8 port_num, struct ib_port_attr *port_attr);

enum rdma_link_layer rdma_port_get_link_layer(struct ib_device *device,
					       u8 port_num);

/**
 * rdma_cap_ib_switch - Check if the device is IB switch
 * @device: Device to check
 *
 * Device driver is responsible for setting is_switch bit on
 * in ib_device structure at init time.
 *
 * Return: true if the device is IB switch.
 */
static inline bool rdma_cap_ib_switch(const struct ib_device *device)
{
	return device->is_switch;
}

/**
 * rdma_start_port - Return the first valid port number for the device
 * specified
 *
 * @device: Device to be checked
 *
 * Return start port number
 */
static inline u8 rdma_start_port(const struct ib_device *device)
{
	return rdma_cap_ib_switch(device) ? 0 : 1;
}

/**
 * rdma_for_each_port - Iterate over all valid port numbers of the IB device
 * @device - The struct ib_device * to iterate over
 * @iter - The unsigned int to store the port number
 */
#define rdma_for_each_port(device, iter)                                       \
	for (iter = rdma_start_port(device + BUILD_BUG_ON_ZERO(!__same_type(   \
						     unsigned int, iter)));    \
	     iter <= rdma_end_port(device); (iter)++)

/**
 * rdma_end_port - Return the last valid port number for the device
 * specified
 *
 * @device: Device to be checked
 *
 * Return last port number
 */
static inline u8 rdma_end_port(const struct ib_device *device)
{
	return rdma_cap_ib_switch(device) ? 0 : device->phys_port_cnt;
}

static inline int rdma_is_port_valid(const struct ib_device *device,
				     unsigned int port)
{
	return (port >= rdma_start_port(device) &&
		port <= rdma_end_port(device));
}

static inline bool rdma_is_grh_required(const struct ib_device *device,
					u8 port_num)
{
	return device->port_data[port_num].immutable.core_cap_flags &
	       RDMA_CORE_PORT_IB_GRH_REQUIRED;
}

static inline bool rdma_protocol_ib(const struct ib_device *device, u8 port_num)
{
	return device->port_data[port_num].immutable.core_cap_flags &
	       RDMA_CORE_CAP_PROT_IB;
}

static inline bool rdma_protocol_roce(const struct ib_device *device, u8 port_num)
{
	return device->port_data[port_num].immutable.core_cap_flags &
	       (RDMA_CORE_CAP_PROT_ROCE | RDMA_CORE_CAP_PROT_ROCE_UDP_ENCAP);
}

static inline bool rdma_protocol_roce_udp_encap(const struct ib_device *device, u8 port_num)
{
	return device->port_data[port_num].immutable.core_cap_flags &
	       RDMA_CORE_CAP_PROT_ROCE_UDP_ENCAP;
}

static inline bool rdma_protocol_roce_eth_encap(const struct ib_device *device, u8 port_num)
{
	return device->port_data[port_num].immutable.core_cap_flags &
	       RDMA_CORE_CAP_PROT_ROCE;
}

static inline bool rdma_protocol_iwarp(const struct ib_device *device, u8 port_num)
{
	return device->port_data[port_num].immutable.core_cap_flags &
	       RDMA_CORE_CAP_PROT_IWARP;
}

static inline bool rdma_ib_or_roce(const struct ib_device *device, u8 port_num)
{
	return rdma_protocol_ib(device, port_num) ||
		rdma_protocol_roce(device, port_num);
}

static inline bool rdma_protocol_raw_packet(const struct ib_device *device, u8 port_num)
{
	return device->port_data[port_num].immutable.core_cap_flags &
	       RDMA_CORE_CAP_PROT_RAW_PACKET;
}

static inline bool rdma_protocol_usnic(const struct ib_device *device, u8 port_num)
{
	return device->port_data[port_num].immutable.core_cap_flags &
	       RDMA_CORE_CAP_PROT_USNIC;
}

/**
 * rdma_cap_ib_mad - Check if the port of a device supports Infiniband
 * Management Datagrams.
 * @device: Device to check
 * @port_num: Port number to check
 *
 * Management Datagrams (MAD) are a required part of the InfiniBand
 * specification and are supported on all InfiniBand devices.  A slightly
 * extended version are also supported on OPA interfaces.
 *
 * Return: true if the port supports sending/receiving of MAD packets.
 */
static inline bool rdma_cap_ib_mad(const struct ib_device *device, u8 port_num)
{
	return device->port_data[port_num].immutable.core_cap_flags &
	       RDMA_CORE_CAP_IB_MAD;
}

/**
 * rdma_cap_opa_mad - Check if the port of device provides support for OPA
 * Management Datagrams.
 * @device: Device to check
 * @port_num: Port number to check
 *
 * Intel OmniPath devices extend and/or replace the InfiniBand Management
 * datagrams with their own versions.  These OPA MADs share many but not all of
 * the characteristics of InfiniBand MADs.
 *
 * OPA MADs differ in the following ways:
 *
 *    1) MADs are variable size up to 2K
 *       IBTA defined MADs remain fixed at 256 bytes
 *    2) OPA SMPs must carry valid PKeys
 *    3) OPA SMP packets are a different format
 *
 * Return: true if the port supports OPA MAD packet formats.
 */
static inline bool rdma_cap_opa_mad(struct ib_device *device, u8 port_num)
{
	return device->port_data[port_num].immutable.core_cap_flags &
		RDMA_CORE_CAP_OPA_MAD;
}

/**
 * rdma_cap_ib_smi - Check if the port of a device provides an Infiniband
 * Subnet Management Agent (SMA) on the Subnet Management Interface (SMI).
 * @device: Device to check
 * @port_num: Port number to check
 *
 * Each InfiniBand node is required to provide a Subnet Management Agent
 * that the subnet manager can access.  Prior to the fabric being fully
 * configured by the subnet manager, the SMA is accessed via a well known
 * interface called the Subnet Management Interface (SMI).  This interface
 * uses directed route packets to communicate with the SM to get around the
 * chicken and egg problem of the SM needing to know what's on the fabric
 * in order to configure the fabric, and needing to configure the fabric in
 * order to send packets to the devices on the fabric.  These directed
 * route packets do not need the fabric fully configured in order to reach
 * their destination.  The SMI is the only method allowed to send
 * directed route packets on an InfiniBand fabric.
 *
 * Return: true if the port provides an SMI.
 */
static inline bool rdma_cap_ib_smi(const struct ib_device *device, u8 port_num)
{
	return device->port_data[port_num].immutable.core_cap_flags &
	       RDMA_CORE_CAP_IB_SMI;
}

/**
 * rdma_cap_ib_cm - Check if the port of device has the capability Infiniband
 * Communication Manager.
 * @device: Device to check
 * @port_num: Port number to check
 *
 * The InfiniBand Communication Manager is one of many pre-defined General
 * Service Agents (GSA) that are accessed via the General Service
 * Interface (GSI).  It's role is to facilitate establishment of connections
 * between nodes as well as other management related tasks for established
 * connections.
 *
 * Return: true if the port supports an IB CM (this does not guarantee that
 * a CM is actually running however).
 */
static inline bool rdma_cap_ib_cm(const struct ib_device *device, u8 port_num)
{
	return device->port_data[port_num].immutable.core_cap_flags &
	       RDMA_CORE_CAP_IB_CM;
}

/**
 * rdma_cap_iw_cm - Check if the port of device has the capability IWARP
 * Communication Manager.
 * @device: Device to check
 * @port_num: Port number to check
 *
 * Similar to above, but specific to iWARP connections which have a different
 * managment protocol than InfiniBand.
 *
 * Return: true if the port supports an iWARP CM (this does not guarantee that
 * a CM is actually running however).
 */
static inline bool rdma_cap_iw_cm(const struct ib_device *device, u8 port_num)
{
	return device->port_data[port_num].immutable.core_cap_flags &
	       RDMA_CORE_CAP_IW_CM;
}

/**
 * rdma_cap_ib_sa - Check if the port of device has the capability Infiniband
 * Subnet Administration.
 * @device: Device to check
 * @port_num: Port number to check
 *
 * An InfiniBand Subnet Administration (SA) service is a pre-defined General
 * Service Agent (GSA) provided by the Subnet Manager (SM).  On InfiniBand
 * fabrics, devices should resolve routes to other hosts by contacting the
 * SA to query the proper route.
 *
 * Return: true if the port should act as a client to the fabric Subnet
 * Administration interface.  This does not imply that the SA service is
 * running locally.
 */
static inline bool rdma_cap_ib_sa(const struct ib_device *device, u8 port_num)
{
	return device->port_data[port_num].immutable.core_cap_flags &
	       RDMA_CORE_CAP_IB_SA;
}

/**
 * rdma_cap_ib_mcast - Check if the port of device has the capability Infiniband
 * Multicast.
 * @device: Device to check
 * @port_num: Port number to check
 *
 * InfiniBand multicast registration is more complex than normal IPv4 or
 * IPv6 multicast registration.  Each Host Channel Adapter must register
 * with the Subnet Manager when it wishes to join a multicast group.  It
 * should do so only once regardless of how many queue pairs it subscribes
 * to this group.  And it should leave the group only after all queue pairs
 * attached to the group have been detached.
 *
 * Return: true if the port must undertake the additional adminstrative
 * overhead of registering/unregistering with the SM and tracking of the
 * total number of queue pairs attached to the multicast group.
 */
static inline bool rdma_cap_ib_mcast(const struct ib_device *device, u8 port_num)
{
	return rdma_cap_ib_sa(device, port_num);
}

/**
 * rdma_cap_af_ib - Check if the port of device has the capability
 * Native Infiniband Address.
 * @device: Device to check
 * @port_num: Port number to check
 *
 * InfiniBand addressing uses a port's GUID + Subnet Prefix to make a default
 * GID.  RoCE uses a different mechanism, but still generates a GID via
 * a prescribed mechanism and port specific data.
 *
 * Return: true if the port uses a GID address to identify devices on the
 * network.
 */
static inline bool rdma_cap_af_ib(const struct ib_device *device, u8 port_num)
{
	return device->port_data[port_num].immutable.core_cap_flags &
	       RDMA_CORE_CAP_AF_IB;
}

/**
 * rdma_cap_eth_ah - Check if the port of device has the capability
 * Ethernet Address Handle.
 * @device: Device to check
 * @port_num: Port number to check
 *
 * RoCE is InfiniBand over Ethernet, and it uses a well defined technique
 * to fabricate GIDs over Ethernet/IP specific addresses native to the
 * port.  Normally, packet headers are generated by the sending host
 * adapter, but when sending connectionless datagrams, we must manually
 * inject the proper headers for the fabric we are communicating over.
 *
 * Return: true if we are running as a RoCE port and must force the
 * addition of a Global Route Header built from our Ethernet Address
 * Handle into our header list for connectionless packets.
 */
static inline bool rdma_cap_eth_ah(const struct ib_device *device, u8 port_num)
{
	return device->port_data[port_num].immutable.core_cap_flags &
	       RDMA_CORE_CAP_ETH_AH;
}

/**
 * rdma_cap_opa_ah - Check if the port of device supports
 * OPA Address handles
 * @device: Device to check
 * @port_num: Port number to check
 *
 * Return: true if we are running on an OPA device which supports
 * the extended OPA addressing.
 */
static inline bool rdma_cap_opa_ah(struct ib_device *device, u8 port_num)
{
	return (device->port_data[port_num].immutable.core_cap_flags &
		RDMA_CORE_CAP_OPA_AH) == RDMA_CORE_CAP_OPA_AH;
}

/**
 * rdma_max_mad_size - Return the max MAD size required by this RDMA Port.
 *
 * @device: Device
 * @port_num: Port number
 *
 * This MAD size includes the MAD headers and MAD payload.  No other headers
 * are included.
 *
 * Return the max MAD size required by the Port.  Will return 0 if the port
 * does not support MADs
 */
static inline size_t rdma_max_mad_size(const struct ib_device *device, u8 port_num)
{
	return device->port_data[port_num].immutable.max_mad_size;
}

/**
 * rdma_cap_roce_gid_table - Check if the port of device uses roce_gid_table
 * @device: Device to check
 * @port_num: Port number to check
 *
 * RoCE GID table mechanism manages the various GIDs for a device.
 *
 * NOTE: if allocating the port's GID table has failed, this call will still
 * return true, but any RoCE GID table API will fail.
 *
 * Return: true if the port uses RoCE GID table mechanism in order to manage
 * its GIDs.
 */
static inline bool rdma_cap_roce_gid_table(const struct ib_device *device,
					   u8 port_num)
{
	return rdma_protocol_roce(device, port_num) &&
		device->ops.add_gid && device->ops.del_gid;
}

/*
 * Check if the device supports READ W/ INVALIDATE.
 */
static inline bool rdma_cap_read_inv(struct ib_device *dev, u32 port_num)
{
	/*
	 * iWarp drivers must support READ W/ INVALIDATE.  No other protocol
	 * has support for it yet.
	 */
	return rdma_protocol_iwarp(dev, port_num);
}

/**
 * rdma_find_pg_bit - Find page bit given address and HW supported page sizes
 *
 * @addr: address
 * @pgsz_bitmap: bitmap of HW supported page sizes
 */
static inline unsigned int rdma_find_pg_bit(unsigned long addr,
					    unsigned long pgsz_bitmap)
{
	unsigned long align;
	unsigned long pgsz;

	align = addr & -addr;

	/* Find page bit such that addr is aligned to the highest supported
	 * HW page size
	 */
	pgsz = pgsz_bitmap & ~(-align << 1);
	if (!pgsz)
		return __ffs(pgsz_bitmap);

	return __fls(pgsz);
}

int ib_set_vf_link_state(struct ib_device *device, int vf, u8 port,
			 int state);
int ib_get_vf_config(struct ib_device *device, int vf, u8 port,
		     struct ifla_vf_info *info);
int ib_get_vf_stats(struct ib_device *device, int vf, u8 port,
		    struct ifla_vf_stats *stats);
int ib_set_vf_guid(struct ib_device *device, int vf, u8 port, u64 guid,
		   int type);

int ib_query_pkey(struct ib_device *device,
		  u8 port_num, u16 index, u16 *pkey);

int ib_modify_device(struct ib_device *device,
		     int device_modify_mask,
		     struct ib_device_modify *device_modify);

int ib_modify_port(struct ib_device *device,
		   u8 port_num, int port_modify_mask,
		   struct ib_port_modify *port_modify);

int ib_find_gid(struct ib_device *device, union ib_gid *gid,
		u8 *port_num, u16 *index);

int ib_find_pkey(struct ib_device *device,
		 u8 port_num, u16 pkey, u16 *index);

enum ib_pd_flags {
	/*
	 * Create a memory registration for all memory in the system and place
	 * the rkey for it into pd->unsafe_global_rkey.  This can be used by
	 * ULPs to avoid the overhead of dynamic MRs.
	 *
	 * This flag is generally considered unsafe and must only be used in
	 * extremly trusted environments.  Every use of it will log a warning
	 * in the kernel log.
	 */
	IB_PD_UNSAFE_GLOBAL_RKEY	= 0x01,
};

struct ib_pd *__ib_alloc_pd(struct ib_device *device, unsigned int flags,
		const char *caller);

#define ib_alloc_pd(device, flags) \
	__ib_alloc_pd((device), (flags), KBUILD_MODNAME)

/**
 * ib_dealloc_pd_user - Deallocate kernel/user PD
 * @pd: The protection domain
 * @udata: Valid user data or NULL for kernel objects
 */
void ib_dealloc_pd_user(struct ib_pd *pd, struct ib_udata *udata);

/**
 * ib_dealloc_pd - Deallocate kernel PD
 * @pd: The protection domain
 *
 * NOTE: for user PD use ib_dealloc_pd_user with valid udata!
 */
static inline void ib_dealloc_pd(struct ib_pd *pd)
{
	ib_dealloc_pd_user(pd, NULL);
}

enum rdma_create_ah_flags {
	/* In a sleepable context */
	RDMA_CREATE_AH_SLEEPABLE = BIT(0),
};

/**
 * rdma_create_ah - Creates an address handle for the given address vector.
 * @pd: The protection domain associated with the address handle.
 * @ah_attr: The attributes of the address vector.
 * @flags: Create address handle flags (see enum rdma_create_ah_flags).
 *
 * The address handle is used to reference a local or global destination
 * in all UD QP post sends.
 */
struct ib_ah *rdma_create_ah(struct ib_pd *pd, struct rdma_ah_attr *ah_attr,
			     u32 flags);

/**
 * rdma_create_user_ah - Creates an address handle for the given address vector.
 * It resolves destination mac address for ah attribute of RoCE type.
 * @pd: The protection domain associated with the address handle.
 * @ah_attr: The attributes of the address vector.
 * @udata: pointer to user's input output buffer information need by
 *         provider driver.
 *
 * It returns 0 on success and returns appropriate error code on error.
 * The address handle is used to reference a local or global destination
 * in all UD QP post sends.
 */
struct ib_ah *rdma_create_user_ah(struct ib_pd *pd,
				  struct rdma_ah_attr *ah_attr,
				  struct ib_udata *udata);
/**
 * ib_get_gids_from_rdma_hdr - Get sgid and dgid from GRH or IPv4 header
 *   work completion.
 * @hdr: the L3 header to parse
 * @net_type: type of header to parse
 * @sgid: place to store source gid
 * @dgid: place to store destination gid
 */
int ib_get_gids_from_rdma_hdr(const union rdma_network_hdr *hdr,
			      enum rdma_network_type net_type,
			      union ib_gid *sgid, union ib_gid *dgid);

/**
 * ib_get_rdma_header_version - Get the header version
 * @hdr: the L3 header to parse
 */
int ib_get_rdma_header_version(const union rdma_network_hdr *hdr);

/**
 * ib_init_ah_attr_from_wc - Initializes address handle attributes from a
 *   work completion.
 * @device: Device on which the received message arrived.
 * @port_num: Port on which the received message arrived.
 * @wc: Work completion associated with the received message.
 * @grh: References the received global route header.  This parameter is
 *   ignored unless the work completion indicates that the GRH is valid.
 * @ah_attr: Returned attributes that can be used when creating an address
 *   handle for replying to the message.
 * When ib_init_ah_attr_from_wc() returns success,
 * (a) for IB link layer it optionally contains a reference to SGID attribute
 * when GRH is present for IB link layer.
 * (b) for RoCE link layer it contains a reference to SGID attribute.
 * User must invoke rdma_cleanup_ah_attr_gid_attr() to release reference to SGID
 * attributes which are initialized using ib_init_ah_attr_from_wc().
 *
 */
int ib_init_ah_attr_from_wc(struct ib_device *device, u8 port_num,
			    const struct ib_wc *wc, const struct ib_grh *grh,
			    struct rdma_ah_attr *ah_attr);

/**
 * ib_create_ah_from_wc - Creates an address handle associated with the
 *   sender of the specified work completion.
 * @pd: The protection domain associated with the address handle.
 * @wc: Work completion information associated with a received message.
 * @grh: References the received global route header.  This parameter is
 *   ignored unless the work completion indicates that the GRH is valid.
 * @port_num: The outbound port number to associate with the address.
 *
 * The address handle is used to reference a local or global destination
 * in all UD QP post sends.
 */
struct ib_ah *ib_create_ah_from_wc(struct ib_pd *pd, const struct ib_wc *wc,
				   const struct ib_grh *grh, u8 port_num);

/**
 * rdma_modify_ah - Modifies the address vector associated with an address
 *   handle.
 * @ah: The address handle to modify.
 * @ah_attr: The new address vector attributes to associate with the
 *   address handle.
 */
int rdma_modify_ah(struct ib_ah *ah, struct rdma_ah_attr *ah_attr);

/**
 * rdma_query_ah - Queries the address vector associated with an address
 *   handle.
 * @ah: The address handle to query.
 * @ah_attr: The address vector attributes associated with the address
 *   handle.
 */
int rdma_query_ah(struct ib_ah *ah, struct rdma_ah_attr *ah_attr);

enum rdma_destroy_ah_flags {
	/* In a sleepable context */
	RDMA_DESTROY_AH_SLEEPABLE = BIT(0),
};

/**
 * rdma_destroy_ah_user - Destroys an address handle.
 * @ah: The address handle to destroy.
 * @flags: Destroy address handle flags (see enum rdma_destroy_ah_flags).
 * @udata: Valid user data or NULL for kernel objects
 */
int rdma_destroy_ah_user(struct ib_ah *ah, u32 flags, struct ib_udata *udata);

/**
 * rdma_destroy_ah - Destroys an kernel address handle.
 * @ah: The address handle to destroy.
 * @flags: Destroy address handle flags (see enum rdma_destroy_ah_flags).
 *
 * NOTE: for user ah use rdma_destroy_ah_user with valid udata!
 */
static inline int rdma_destroy_ah(struct ib_ah *ah, u32 flags)
{
	return rdma_destroy_ah_user(ah, flags, NULL);
}

/**
 * ib_create_srq - Creates a SRQ associated with the specified protection
 *   domain.
 * @pd: The protection domain associated with the SRQ.
 * @srq_init_attr: A list of initial attributes required to create the
 *   SRQ.  If SRQ creation succeeds, then the attributes are updated to
 *   the actual capabilities of the created SRQ.
 *
 * srq_attr->max_wr and srq_attr->max_sge are read the determine the
 * requested size of the SRQ, and set to the actual values allocated
 * on return.  If ib_create_srq() succeeds, then max_wr and max_sge
 * will always be at least as large as the requested values.
 */
struct ib_srq *ib_create_srq(struct ib_pd *pd,
			     struct ib_srq_init_attr *srq_init_attr);

/**
 * ib_modify_srq - Modifies the attributes for the specified SRQ.
 * @srq: The SRQ to modify.
 * @srq_attr: On input, specifies the SRQ attributes to modify.  On output,
 *   the current values of selected SRQ attributes are returned.
 * @srq_attr_mask: A bit-mask used to specify which attributes of the SRQ
 *   are being modified.
 *
 * The mask may contain IB_SRQ_MAX_WR to resize the SRQ and/or
 * IB_SRQ_LIMIT to set the SRQ's limit and request notification when
 * the number of receives queued drops below the limit.
 */
int ib_modify_srq(struct ib_srq *srq,
		  struct ib_srq_attr *srq_attr,
		  enum ib_srq_attr_mask srq_attr_mask);

/**
 * ib_query_srq - Returns the attribute list and current values for the
 *   specified SRQ.
 * @srq: The SRQ to query.
 * @srq_attr: The attributes of the specified SRQ.
 */
int ib_query_srq(struct ib_srq *srq,
		 struct ib_srq_attr *srq_attr);

/**
 * ib_destroy_srq_user - Destroys the specified SRQ.
 * @srq: The SRQ to destroy.
 * @udata: Valid user data or NULL for kernel objects
 */
int ib_destroy_srq_user(struct ib_srq *srq, struct ib_udata *udata);

/**
 * ib_destroy_srq - Destroys the specified kernel SRQ.
 * @srq: The SRQ to destroy.
 *
 * NOTE: for user srq use ib_destroy_srq_user with valid udata!
 */
static inline int ib_destroy_srq(struct ib_srq *srq)
{
	return ib_destroy_srq_user(srq, NULL);
}

/**
 * ib_post_srq_recv - Posts a list of work requests to the specified SRQ.
 * @srq: The SRQ to post the work request on.
 * @recv_wr: A list of work requests to post on the receive queue.
 * @bad_recv_wr: On an immediate failure, this parameter will reference
 *   the work request that failed to be posted on the QP.
 */
static inline int ib_post_srq_recv(struct ib_srq *srq,
				   const struct ib_recv_wr *recv_wr,
				   const struct ib_recv_wr **bad_recv_wr)
{
	const struct ib_recv_wr *dummy;

	return srq->device->ops.post_srq_recv(srq, recv_wr,
					      bad_recv_wr ? : &dummy);
}

/**
 * ib_create_qp_user - Creates a QP associated with the specified protection
 *   domain.
 * @pd: The protection domain associated with the QP.
 * @qp_init_attr: A list of initial attributes required to create the
 *   QP.  If QP creation succeeds, then the attributes are updated to
 *   the actual capabilities of the created QP.
 * @udata: Valid user data or NULL for kernel objects
 */
struct ib_qp *ib_create_qp_user(struct ib_pd *pd,
				struct ib_qp_init_attr *qp_init_attr,
				struct ib_udata *udata);

/**
 * ib_create_qp - Creates a kernel QP associated with the specified protection
 *   domain.
 * @pd: The protection domain associated with the QP.
 * @qp_init_attr: A list of initial attributes required to create the
 *   QP.  If QP creation succeeds, then the attributes are updated to
 *   the actual capabilities of the created QP.
 * @udata: Valid user data or NULL for kernel objects
 *
 * NOTE: for user qp use ib_create_qp_user with valid udata!
 */
static inline struct ib_qp *ib_create_qp(struct ib_pd *pd,
					 struct ib_qp_init_attr *qp_init_attr)
{
	return ib_create_qp_user(pd, qp_init_attr, NULL);
}

/**
 * ib_modify_qp_with_udata - Modifies the attributes for the specified QP.
 * @qp: The QP to modify.
 * @attr: On input, specifies the QP attributes to modify.  On output,
 *   the current values of selected QP attributes are returned.
 * @attr_mask: A bit-mask used to specify which attributes of the QP
 *   are being modified.
 * @udata: pointer to user's input output buffer information
 *   are being modified.
 * It returns 0 on success and returns appropriate error code on error.
 */
int ib_modify_qp_with_udata(struct ib_qp *qp,
			    struct ib_qp_attr *attr,
			    int attr_mask,
			    struct ib_udata *udata);

/**
 * ib_modify_qp - Modifies the attributes for the specified QP and then
 *   transitions the QP to the given state.
 * @qp: The QP to modify.
 * @qp_attr: On input, specifies the QP attributes to modify.  On output,
 *   the current values of selected QP attributes are returned.
 * @qp_attr_mask: A bit-mask used to specify which attributes of the QP
 *   are being modified.
 */
int ib_modify_qp(struct ib_qp *qp,
		 struct ib_qp_attr *qp_attr,
		 int qp_attr_mask);

/**
 * ib_query_qp - Returns the attribute list and current values for the
 *   specified QP.
 * @qp: The QP to query.
 * @qp_attr: The attributes of the specified QP.
 * @qp_attr_mask: A bit-mask used to select specific attributes to query.
 * @qp_init_attr: Additional attributes of the selected QP.
 *
 * The qp_attr_mask may be used to limit the query to gathering only the
 * selected attributes.
 */
int ib_query_qp(struct ib_qp *qp,
		struct ib_qp_attr *qp_attr,
		int qp_attr_mask,
		struct ib_qp_init_attr *qp_init_attr);

/**
 * ib_destroy_qp - Destroys the specified QP.
 * @qp: The QP to destroy.
 * @udata: Valid udata or NULL for kernel objects
 */
int ib_destroy_qp_user(struct ib_qp *qp, struct ib_udata *udata);

/**
 * ib_destroy_qp - Destroys the specified kernel QP.
 * @qp: The QP to destroy.
 *
 * NOTE: for user qp use ib_destroy_qp_user with valid udata!
 */
static inline int ib_destroy_qp(struct ib_qp *qp)
{
	return ib_destroy_qp_user(qp, NULL);
}

/**
 * ib_open_qp - Obtain a reference to an existing sharable QP.
 * @xrcd - XRC domain
 * @qp_open_attr: Attributes identifying the QP to open.
 *
 * Returns a reference to a sharable QP.
 */
struct ib_qp *ib_open_qp(struct ib_xrcd *xrcd,
			 struct ib_qp_open_attr *qp_open_attr);

/**
 * ib_close_qp - Release an external reference to a QP.
 * @qp: The QP handle to release
 *
 * The opened QP handle is released by the caller.  The underlying
 * shared QP is not destroyed until all internal references are released.
 */
int ib_close_qp(struct ib_qp *qp);

/**
 * ib_post_send - Posts a list of work requests to the send queue of
 *   the specified QP.
 * @qp: The QP to post the work request on.
 * @send_wr: A list of work requests to post on the send queue.
 * @bad_send_wr: On an immediate failure, this parameter will reference
 *   the work request that failed to be posted on the QP.
 *
 * While IBA Vol. 1 section 11.4.1.1 specifies that if an immediate
 * error is returned, the QP state shall not be affected,
 * ib_post_send() will return an immediate error after queueing any
 * earlier work requests in the list.
 */
static inline int ib_post_send(struct ib_qp *qp,
			       const struct ib_send_wr *send_wr,
			       const struct ib_send_wr **bad_send_wr)
{
	const struct ib_send_wr *dummy;

	return qp->device->ops.post_send(qp, send_wr, bad_send_wr ? : &dummy);
}

/**
 * ib_post_recv - Posts a list of work requests to the receive queue of
 *   the specified QP.
 * @qp: The QP to post the work request on.
 * @recv_wr: A list of work requests to post on the receive queue.
 * @bad_recv_wr: On an immediate failure, this parameter will reference
 *   the work request that failed to be posted on the QP.
 */
static inline int ib_post_recv(struct ib_qp *qp,
			       const struct ib_recv_wr *recv_wr,
			       const struct ib_recv_wr **bad_recv_wr)
{
	const struct ib_recv_wr *dummy;

	return qp->device->ops.post_recv(qp, recv_wr, bad_recv_wr ? : &dummy);
}

struct ib_cq *__ib_alloc_cq_user(struct ib_device *dev, void *private,
				 int nr_cqe, int comp_vector,
				 enum ib_poll_context poll_ctx,
				 const char *caller, struct ib_udata *udata);

/**
 * ib_alloc_cq_user: Allocate kernel/user CQ
 * @dev: The IB device
 * @private: Private data attached to the CQE
 * @nr_cqe: Number of CQEs in the CQ
 * @comp_vector: Completion vector used for the IRQs
 * @poll_ctx: Context used for polling the CQ
 * @udata: Valid user data or NULL for kernel objects
 */
static inline struct ib_cq *ib_alloc_cq_user(struct ib_device *dev,
					     void *private, int nr_cqe,
					     int comp_vector,
					     enum ib_poll_context poll_ctx,
					     struct ib_udata *udata)
{
	return __ib_alloc_cq_user(dev, private, nr_cqe, comp_vector, poll_ctx,
				  KBUILD_MODNAME, udata);
}

/**
 * ib_alloc_cq: Allocate kernel CQ
 * @dev: The IB device
 * @private: Private data attached to the CQE
 * @nr_cqe: Number of CQEs in the CQ
 * @comp_vector: Completion vector used for the IRQs
 * @poll_ctx: Context used for polling the CQ
 *
 * NOTE: for user cq use ib_alloc_cq_user with valid udata!
 */
static inline struct ib_cq *ib_alloc_cq(struct ib_device *dev, void *private,
					int nr_cqe, int comp_vector,
					enum ib_poll_context poll_ctx)
{
	return ib_alloc_cq_user(dev, private, nr_cqe, comp_vector, poll_ctx,
				NULL);
}

struct ib_cq *__ib_alloc_cq_any(struct ib_device *dev, void *private,
				int nr_cqe, enum ib_poll_context poll_ctx,
				const char *caller);

/**
 * ib_alloc_cq_any: Allocate kernel CQ
 * @dev: The IB device
 * @private: Private data attached to the CQE
 * @nr_cqe: Number of CQEs in the CQ
 * @poll_ctx: Context used for polling the CQ
 */
static inline struct ib_cq *ib_alloc_cq_any(struct ib_device *dev,
					    void *private, int nr_cqe,
					    enum ib_poll_context poll_ctx)
{
	return __ib_alloc_cq_any(dev, private, nr_cqe, poll_ctx,
				 KBUILD_MODNAME);
}

/**
 * ib_free_cq_user - Free kernel/user CQ
 * @cq: The CQ to free
 * @udata: Valid user data or NULL for kernel objects
 */
void ib_free_cq_user(struct ib_cq *cq, struct ib_udata *udata);

/**
 * ib_free_cq - Free kernel CQ
 * @cq: The CQ to free
 *
 * NOTE: for user cq use ib_free_cq_user with valid udata!
 */
static inline void ib_free_cq(struct ib_cq *cq)
{
	ib_free_cq_user(cq, NULL);
}

int ib_process_cq_direct(struct ib_cq *cq, int budget);

/**
 * ib_create_cq - Creates a CQ on the specified device.
 * @device: The device on which to create the CQ.
 * @comp_handler: A user-specified callback that is invoked when a
 *   completion event occurs on the CQ.
 * @event_handler: A user-specified callback that is invoked when an
 *   asynchronous event not associated with a completion occurs on the CQ.
 * @cq_context: Context associated with the CQ returned to the user via
 *   the associated completion and event handlers.
 * @cq_attr: The attributes the CQ should be created upon.
 *
 * Users can examine the cq structure to determine the actual CQ size.
 */
struct ib_cq *__ib_create_cq(struct ib_device *device,
			     ib_comp_handler comp_handler,
			     void (*event_handler)(struct ib_event *, void *),
			     void *cq_context,
			     const struct ib_cq_init_attr *cq_attr,
			     const char *caller);
#define ib_create_cq(device, cmp_hndlr, evt_hndlr, cq_ctxt, cq_attr) \
	__ib_create_cq((device), (cmp_hndlr), (evt_hndlr), (cq_ctxt), (cq_attr), KBUILD_MODNAME)

/**
 * ib_resize_cq - Modifies the capacity of the CQ.
 * @cq: The CQ to resize.
 * @cqe: The minimum size of the CQ.
 *
 * Users can examine the cq structure to determine the actual CQ size.
 */
int ib_resize_cq(struct ib_cq *cq, int cqe);

/**
 * rdma_set_cq_moderation - Modifies moderation params of the CQ
 * @cq: The CQ to modify.
 * @cq_count: number of CQEs that will trigger an event
 * @cq_period: max period of time in usec before triggering an event
 *
 */
int rdma_set_cq_moderation(struct ib_cq *cq, u16 cq_count, u16 cq_period);

/**
 * ib_destroy_cq_user - Destroys the specified CQ.
 * @cq: The CQ to destroy.
 * @udata: Valid user data or NULL for kernel objects
 */
int ib_destroy_cq_user(struct ib_cq *cq, struct ib_udata *udata);

/**
 * ib_destroy_cq - Destroys the specified kernel CQ.
 * @cq: The CQ to destroy.
 *
 * NOTE: for user cq use ib_destroy_cq_user with valid udata!
 */
static inline void ib_destroy_cq(struct ib_cq *cq)
{
	ib_destroy_cq_user(cq, NULL);
}

/**
 * ib_poll_cq - poll a CQ for completion(s)
 * @cq:the CQ being polled
 * @num_entries:maximum number of completions to return
 * @wc:array of at least @num_entries &struct ib_wc where completions
 *   will be returned
 *
 * Poll a CQ for (possibly multiple) completions.  If the return value
 * is < 0, an error occurred.  If the return value is >= 0, it is the
 * number of completions returned.  If the return value is
 * non-negative and < num_entries, then the CQ was emptied.
 */
static inline int ib_poll_cq(struct ib_cq *cq, int num_entries,
			     struct ib_wc *wc)
{
	return cq->device->ops.poll_cq(cq, num_entries, wc);
}

/**
 * ib_req_notify_cq - Request completion notification on a CQ.
 * @cq: The CQ to generate an event for.
 * @flags:
 *   Must contain exactly one of %IB_CQ_SOLICITED or %IB_CQ_NEXT_COMP
 *   to request an event on the next solicited event or next work
 *   completion at any type, respectively. %IB_CQ_REPORT_MISSED_EVENTS
 *   may also be |ed in to request a hint about missed events, as
 *   described below.
 *
 * Return Value:
 *    < 0 means an error occurred while requesting notification
 *   == 0 means notification was requested successfully, and if
 *        IB_CQ_REPORT_MISSED_EVENTS was passed in, then no events
 *        were missed and it is safe to wait for another event.  In
 *        this case is it guaranteed that any work completions added
 *        to the CQ since the last CQ poll will trigger a completion
 *        notification event.
 *    > 0 is only returned if IB_CQ_REPORT_MISSED_EVENTS was passed
 *        in.  It means that the consumer must poll the CQ again to
 *        make sure it is empty to avoid missing an event because of a
 *        race between requesting notification and an entry being
 *        added to the CQ.  This return value means it is possible
 *        (but not guaranteed) that a work completion has been added
 *        to the CQ since the last poll without triggering a
 *        completion notification event.
 */
static inline int ib_req_notify_cq(struct ib_cq *cq,
				   enum ib_cq_notify_flags flags)
{
	return cq->device->ops.req_notify_cq(cq, flags);
}

/**
 * ib_req_ncomp_notif - Request completion notification when there are
 *   at least the specified number of unreaped completions on the CQ.
 * @cq: The CQ to generate an event for.
 * @wc_cnt: The number of unreaped completions that should be on the
 *   CQ before an event is generated.
 */
static inline int ib_req_ncomp_notif(struct ib_cq *cq, int wc_cnt)
{
	return cq->device->ops.req_ncomp_notif ?
		cq->device->ops.req_ncomp_notif(cq, wc_cnt) :
		-ENOSYS;
}

/**
 * ib_dma_mapping_error - check a DMA addr for error
 * @dev: The device for which the dma_addr was created
 * @dma_addr: The DMA address to check
 */
static inline int ib_dma_mapping_error(struct ib_device *dev, u64 dma_addr)
{
	return dma_mapping_error(dev->dma_device, dma_addr);
}

/**
 * ib_dma_map_single - Map a kernel virtual address to DMA address
 * @dev: The device for which the dma_addr is to be created
 * @cpu_addr: The kernel virtual address
 * @size: The size of the region in bytes
 * @direction: The direction of the DMA
 */
static inline u64 ib_dma_map_single(struct ib_device *dev,
				    void *cpu_addr, size_t size,
				    enum dma_data_direction direction)
{
	return dma_map_single(dev->dma_device, cpu_addr, size, direction);
}

/**
 * ib_dma_unmap_single - Destroy a mapping created by ib_dma_map_single()
 * @dev: The device for which the DMA address was created
 * @addr: The DMA address
 * @size: The size of the region in bytes
 * @direction: The direction of the DMA
 */
static inline void ib_dma_unmap_single(struct ib_device *dev,
				       u64 addr, size_t size,
				       enum dma_data_direction direction)
{
	dma_unmap_single(dev->dma_device, addr, size, direction);
}

/**
 * ib_dma_map_page - Map a physical page to DMA address
 * @dev: The device for which the dma_addr is to be created
 * @page: The page to be mapped
 * @offset: The offset within the page
 * @size: The size of the region in bytes
 * @direction: The direction of the DMA
 */
static inline u64 ib_dma_map_page(struct ib_device *dev,
				  struct page *page,
				  unsigned long offset,
				  size_t size,
					 enum dma_data_direction direction)
{
	return dma_map_page(dev->dma_device, page, offset, size, direction);
}

/**
 * ib_dma_unmap_page - Destroy a mapping created by ib_dma_map_page()
 * @dev: The device for which the DMA address was created
 * @addr: The DMA address
 * @size: The size of the region in bytes
 * @direction: The direction of the DMA
 */
static inline void ib_dma_unmap_page(struct ib_device *dev,
				     u64 addr, size_t size,
				     enum dma_data_direction direction)
{
	dma_unmap_page(dev->dma_device, addr, size, direction);
}

/**
 * ib_dma_map_sg - Map a scatter/gather list to DMA addresses
 * @dev: The device for which the DMA addresses are to be created
 * @sg: The array of scatter/gather entries
 * @nents: The number of scatter/gather entries
 * @direction: The direction of the DMA
 */
static inline int ib_dma_map_sg(struct ib_device *dev,
				struct scatterlist *sg, int nents,
				enum dma_data_direction direction)
{
	return dma_map_sg(dev->dma_device, sg, nents, direction);
}

/**
 * ib_dma_unmap_sg - Unmap a scatter/gather list of DMA addresses
 * @dev: The device for which the DMA addresses were created
 * @sg: The array of scatter/gather entries
 * @nents: The number of scatter/gather entries
 * @direction: The direction of the DMA
 */
static inline void ib_dma_unmap_sg(struct ib_device *dev,
				   struct scatterlist *sg, int nents,
				   enum dma_data_direction direction)
{
	dma_unmap_sg(dev->dma_device, sg, nents, direction);
}

static inline int ib_dma_map_sg_attrs(struct ib_device *dev,
				      struct scatterlist *sg, int nents,
				      enum dma_data_direction direction,
				      unsigned long dma_attrs)
{
	return dma_map_sg_attrs(dev->dma_device, sg, nents, direction,
				dma_attrs);
}

static inline void ib_dma_unmap_sg_attrs(struct ib_device *dev,
					 struct scatterlist *sg, int nents,
					 enum dma_data_direction direction,
					 unsigned long dma_attrs)
{
	dma_unmap_sg_attrs(dev->dma_device, sg, nents, direction, dma_attrs);
}

/**
 * ib_dma_max_seg_size - Return the size limit of a single DMA transfer
 * @dev: The device to query
 *
 * The returned value represents a size in bytes.
 */
static inline unsigned int ib_dma_max_seg_size(struct ib_device *dev)
{
	struct device_dma_parameters *p = dev->dma_device->dma_parms;

	return p ? p->max_segment_size : UINT_MAX;
}

/**
 * ib_dma_sync_single_for_cpu - Prepare DMA region to be accessed by CPU
 * @dev: The device for which the DMA address was created
 * @addr: The DMA address
 * @size: The size of the region in bytes
 * @dir: The direction of the DMA
 */
static inline void ib_dma_sync_single_for_cpu(struct ib_device *dev,
					      u64 addr,
					      size_t size,
					      enum dma_data_direction dir)
{
	dma_sync_single_for_cpu(dev->dma_device, addr, size, dir);
}

/**
 * ib_dma_sync_single_for_device - Prepare DMA region to be accessed by device
 * @dev: The device for which the DMA address was created
 * @addr: The DMA address
 * @size: The size of the region in bytes
 * @dir: The direction of the DMA
 */
static inline void ib_dma_sync_single_for_device(struct ib_device *dev,
						 u64 addr,
						 size_t size,
						 enum dma_data_direction dir)
{
	dma_sync_single_for_device(dev->dma_device, addr, size, dir);
}

/**
 * ib_dma_alloc_coherent - Allocate memory and map it for DMA
 * @dev: The device for which the DMA address is requested
 * @size: The size of the region to allocate in bytes
 * @dma_handle: A pointer for returning the DMA address of the region
 * @flag: memory allocator flags
 */
static inline void *ib_dma_alloc_coherent(struct ib_device *dev,
					   size_t size,
					   dma_addr_t *dma_handle,
					   gfp_t flag)
{
	return dma_alloc_coherent(dev->dma_device, size, dma_handle, flag);
}

/**
 * ib_dma_free_coherent - Free memory allocated by ib_dma_alloc_coherent()
 * @dev: The device for which the DMA addresses were allocated
 * @size: The size of the region
 * @cpu_addr: the address returned by ib_dma_alloc_coherent()
 * @dma_handle: the DMA address returned by ib_dma_alloc_coherent()
 */
static inline void ib_dma_free_coherent(struct ib_device *dev,
					size_t size, void *cpu_addr,
					dma_addr_t dma_handle)
{
	dma_free_coherent(dev->dma_device, size, cpu_addr, dma_handle);
}

/**
 * ib_dereg_mr_user - Deregisters a memory region and removes it from the
 *   HCA translation table.
 * @mr: The memory region to deregister.
 * @udata: Valid user data or NULL for kernel object
 *
 * This function can fail, if the memory region has memory windows bound to it.
 */
int ib_dereg_mr_user(struct ib_mr *mr, struct ib_udata *udata);

/**
 * ib_dereg_mr - Deregisters a kernel memory region and removes it from the
 *   HCA translation table.
 * @mr: The memory region to deregister.
 *
 * This function can fail, if the memory region has memory windows bound to it.
 *
 * NOTE: for user mr use ib_dereg_mr_user with valid udata!
 */
static inline int ib_dereg_mr(struct ib_mr *mr)
{
	return ib_dereg_mr_user(mr, NULL);
}

struct ib_mr *ib_alloc_mr_user(struct ib_pd *pd, enum ib_mr_type mr_type,
			       u32 max_num_sg, struct ib_udata *udata);

static inline struct ib_mr *ib_alloc_mr(struct ib_pd *pd,
					enum ib_mr_type mr_type, u32 max_num_sg)
{
	return ib_alloc_mr_user(pd, mr_type, max_num_sg, NULL);
}

struct ib_mr *ib_alloc_mr_integrity(struct ib_pd *pd,
				    u32 max_num_data_sg,
				    u32 max_num_meta_sg);

/**
 * ib_update_fast_reg_key - updates the key portion of the fast_reg MR
 *   R_Key and L_Key.
 * @mr - struct ib_mr pointer to be updated.
 * @newkey - new key to be used.
 */
static inline void ib_update_fast_reg_key(struct ib_mr *mr, u8 newkey)
{
	mr->lkey = (mr->lkey & 0xffffff00) | newkey;
	mr->rkey = (mr->rkey & 0xffffff00) | newkey;
}

/**
 * ib_inc_rkey - increments the key portion of the given rkey. Can be used
 * for calculating a new rkey for type 2 memory windows.
 * @rkey - the rkey to increment.
 */
static inline u32 ib_inc_rkey(u32 rkey)
{
	const u32 mask = 0x000000ff;
	return ((rkey + 1) & mask) | (rkey & ~mask);
}

/**
 * ib_alloc_fmr - Allocates a unmapped fast memory region.
 * @pd: The protection domain associated with the unmapped region.
 * @mr_access_flags: Specifies the memory access rights.
 * @fmr_attr: Attributes of the unmapped region.
 *
 * A fast memory region must be mapped before it can be used as part of
 * a work request.
 */
struct ib_fmr *ib_alloc_fmr(struct ib_pd *pd,
			    int mr_access_flags,
			    struct ib_fmr_attr *fmr_attr);

/**
 * ib_map_phys_fmr - Maps a list of physical pages to a fast memory region.
 * @fmr: The fast memory region to associate with the pages.
 * @page_list: An array of physical pages to map to the fast memory region.
 * @list_len: The number of pages in page_list.
 * @iova: The I/O virtual address to use with the mapped region.
 */
static inline int ib_map_phys_fmr(struct ib_fmr *fmr,
				  u64 *page_list, int list_len,
				  u64 iova)
{
	return fmr->device->ops.map_phys_fmr(fmr, page_list, list_len, iova);
}

/**
 * ib_unmap_fmr - Removes the mapping from a list of fast memory regions.
 * @fmr_list: A linked list of fast memory regions to unmap.
 */
int ib_unmap_fmr(struct list_head *fmr_list);

/**
 * ib_dealloc_fmr - Deallocates a fast memory region.
 * @fmr: The fast memory region to deallocate.
 */
int ib_dealloc_fmr(struct ib_fmr *fmr);

/**
 * ib_attach_mcast - Attaches the specified QP to a multicast group.
 * @qp: QP to attach to the multicast group.  The QP must be type
 *   IB_QPT_UD.
 * @gid: Multicast group GID.
 * @lid: Multicast group LID in host byte order.
 *
 * In order to send and receive multicast packets, subnet
 * administration must have created the multicast group and configured
 * the fabric appropriately.  The port associated with the specified
 * QP must also be a member of the multicast group.
 */
int ib_attach_mcast(struct ib_qp *qp, union ib_gid *gid, u16 lid);

/**
 * ib_detach_mcast - Detaches the specified QP from a multicast group.
 * @qp: QP to detach from the multicast group.
 * @gid: Multicast group GID.
 * @lid: Multicast group LID in host byte order.
 */
int ib_detach_mcast(struct ib_qp *qp, union ib_gid *gid, u16 lid);

/**
 * ib_alloc_xrcd - Allocates an XRC domain.
 * @device: The device on which to allocate the XRC domain.
 * @caller: Module name for kernel consumers
 */
struct ib_xrcd *__ib_alloc_xrcd(struct ib_device *device, const char *caller);
#define ib_alloc_xrcd(device) \
	__ib_alloc_xrcd((device), KBUILD_MODNAME)

/**
 * ib_dealloc_xrcd - Deallocates an XRC domain.
 * @xrcd: The XRC domain to deallocate.
 * @udata: Valid user data or NULL for kernel object
 */
int ib_dealloc_xrcd(struct ib_xrcd *xrcd, struct ib_udata *udata);

static inline int ib_check_mr_access(int flags)
{
	/*
	 * Local write permission is required if remote write or
	 * remote atomic permission is also requested.
	 */
	if (flags & (IB_ACCESS_REMOTE_ATOMIC | IB_ACCESS_REMOTE_WRITE) &&
	    !(flags & IB_ACCESS_LOCAL_WRITE))
		return -EINVAL;

	return 0;
}

static inline bool ib_access_writable(int access_flags)
{
	/*
	 * We have writable memory backing the MR if any of the following
	 * access flags are set.  "Local write" and "remote write" obviously
	 * require write access.  "Remote atomic" can do things like fetch and
	 * add, which will modify memory, and "MW bind" can change permissions
	 * by binding a window.
	 */
	return access_flags &
		(IB_ACCESS_LOCAL_WRITE   | IB_ACCESS_REMOTE_WRITE |
		 IB_ACCESS_REMOTE_ATOMIC | IB_ACCESS_MW_BIND);
}

/**
 * ib_check_mr_status: lightweight check of MR status.
 *     This routine may provide status checks on a selected
 *     ib_mr. first use is for signature status check.
 *
 * @mr: A memory region.
 * @check_mask: Bitmask of which checks to perform from
 *     ib_mr_status_check enumeration.
 * @mr_status: The container of relevant status checks.
 *     failed checks will be indicated in the status bitmask
 *     and the relevant info shall be in the error item.
 */
int ib_check_mr_status(struct ib_mr *mr, u32 check_mask,
		       struct ib_mr_status *mr_status);

/**
 * ib_device_try_get: Hold a registration lock
 * device: The device to lock
 *
 * A device under an active registration lock cannot become unregistered. It
 * is only possible to obtain a registration lock on a device that is fully
 * registered, otherwise this function returns false.
 *
 * The registration lock is only necessary for actions which require the
 * device to still be registered. Uses that only require the device pointer to
 * be valid should use get_device(&ibdev->dev) to hold the memory.
 *
 */
static inline bool ib_device_try_get(struct ib_device *dev)
{
	return refcount_inc_not_zero(&dev->refcount);
}

void ib_device_put(struct ib_device *device);
struct ib_device *ib_device_get_by_netdev(struct net_device *ndev,
					  enum rdma_driver_id driver_id);
struct ib_device *ib_device_get_by_name(const char *name,
					enum rdma_driver_id driver_id);
struct net_device *ib_get_net_dev_by_params(struct ib_device *dev, u8 port,
					    u16 pkey, const union ib_gid *gid,
					    const struct sockaddr *addr);
int ib_device_set_netdev(struct ib_device *ib_dev, struct net_device *ndev,
			 unsigned int port);
struct net_device *ib_device_netdev(struct ib_device *dev, u8 port);

struct ib_wq *ib_create_wq(struct ib_pd *pd,
			   struct ib_wq_init_attr *init_attr);
int ib_destroy_wq(struct ib_wq *wq, struct ib_udata *udata);
int ib_modify_wq(struct ib_wq *wq, struct ib_wq_attr *attr,
		 u32 wq_attr_mask);
struct ib_rwq_ind_table *ib_create_rwq_ind_table(struct ib_device *device,
						 struct ib_rwq_ind_table_init_attr*
						 wq_ind_table_init_attr);
int ib_destroy_rwq_ind_table(struct ib_rwq_ind_table *wq_ind_table);

int ib_map_mr_sg(struct ib_mr *mr, struct scatterlist *sg, int sg_nents,
		 unsigned int *sg_offset, unsigned int page_size);
int ib_map_mr_sg_pi(struct ib_mr *mr, struct scatterlist *data_sg,
		    int data_sg_nents, unsigned int *data_sg_offset,
		    struct scatterlist *meta_sg, int meta_sg_nents,
		    unsigned int *meta_sg_offset, unsigned int page_size);

static inline int
ib_map_mr_sg_zbva(struct ib_mr *mr, struct scatterlist *sg, int sg_nents,
		  unsigned int *sg_offset, unsigned int page_size)
{
	int n;

	n = ib_map_mr_sg(mr, sg, sg_nents, sg_offset, page_size);
	mr->iova = 0;

	return n;
}

int ib_sg_to_pages(struct ib_mr *mr, struct scatterlist *sgl, int sg_nents,
		unsigned int *sg_offset, int (*set_page)(struct ib_mr *, u64));

void ib_drain_rq(struct ib_qp *qp);
void ib_drain_sq(struct ib_qp *qp);
void ib_drain_qp(struct ib_qp *qp);

int ib_get_eth_speed(struct ib_device *dev, u8 port_num, u8 *speed, u8 *width);

static inline u8 *rdma_ah_retrieve_dmac(struct rdma_ah_attr *attr)
{
	if (attr->type == RDMA_AH_ATTR_TYPE_ROCE)
		return attr->roce.dmac;
	return NULL;
}

static inline void rdma_ah_set_dlid(struct rdma_ah_attr *attr, u32 dlid)
{
	if (attr->type == RDMA_AH_ATTR_TYPE_IB)
		attr->ib.dlid = (u16)dlid;
	else if (attr->type == RDMA_AH_ATTR_TYPE_OPA)
		attr->opa.dlid = dlid;
}

static inline u32 rdma_ah_get_dlid(const struct rdma_ah_attr *attr)
{
	if (attr->type == RDMA_AH_ATTR_TYPE_IB)
		return attr->ib.dlid;
	else if (attr->type == RDMA_AH_ATTR_TYPE_OPA)
		return attr->opa.dlid;
	return 0;
}

static inline void rdma_ah_set_sl(struct rdma_ah_attr *attr, u8 sl)
{
	attr->sl = sl;
}

static inline u8 rdma_ah_get_sl(const struct rdma_ah_attr *attr)
{
	return attr->sl;
}

static inline void rdma_ah_set_path_bits(struct rdma_ah_attr *attr,
					 u8 src_path_bits)
{
	if (attr->type == RDMA_AH_ATTR_TYPE_IB)
		attr->ib.src_path_bits = src_path_bits;
	else if (attr->type == RDMA_AH_ATTR_TYPE_OPA)
		attr->opa.src_path_bits = src_path_bits;
}

static inline u8 rdma_ah_get_path_bits(const struct rdma_ah_attr *attr)
{
	if (attr->type == RDMA_AH_ATTR_TYPE_IB)
		return attr->ib.src_path_bits;
	else if (attr->type == RDMA_AH_ATTR_TYPE_OPA)
		return attr->opa.src_path_bits;
	return 0;
}

static inline void rdma_ah_set_make_grd(struct rdma_ah_attr *attr,
					bool make_grd)
{
	if (attr->type == RDMA_AH_ATTR_TYPE_OPA)
		attr->opa.make_grd = make_grd;
}

static inline bool rdma_ah_get_make_grd(const struct rdma_ah_attr *attr)
{
	if (attr->type == RDMA_AH_ATTR_TYPE_OPA)
		return attr->opa.make_grd;
	return false;
}

static inline void rdma_ah_set_port_num(struct rdma_ah_attr *attr, u8 port_num)
{
	attr->port_num = port_num;
}

static inline u8 rdma_ah_get_port_num(const struct rdma_ah_attr *attr)
{
	return attr->port_num;
}

static inline void rdma_ah_set_static_rate(struct rdma_ah_attr *attr,
					   u8 static_rate)
{
	attr->static_rate = static_rate;
}

static inline u8 rdma_ah_get_static_rate(const struct rdma_ah_attr *attr)
{
	return attr->static_rate;
}

static inline void rdma_ah_set_ah_flags(struct rdma_ah_attr *attr,
					enum ib_ah_flags flag)
{
	attr->ah_flags = flag;
}

static inline enum ib_ah_flags
		rdma_ah_get_ah_flags(const struct rdma_ah_attr *attr)
{
	return attr->ah_flags;
}

static inline const struct ib_global_route
		*rdma_ah_read_grh(const struct rdma_ah_attr *attr)
{
	return &attr->grh;
}

/*To retrieve and modify the grh */
static inline struct ib_global_route
		*rdma_ah_retrieve_grh(struct rdma_ah_attr *attr)
{
	return &attr->grh;
}

static inline void rdma_ah_set_dgid_raw(struct rdma_ah_attr *attr, void *dgid)
{
	struct ib_global_route *grh = rdma_ah_retrieve_grh(attr);

	memcpy(grh->dgid.raw, dgid, sizeof(grh->dgid));
}

static inline void rdma_ah_set_subnet_prefix(struct rdma_ah_attr *attr,
					     __be64 prefix)
{
	struct ib_global_route *grh = rdma_ah_retrieve_grh(attr);

	grh->dgid.global.subnet_prefix = prefix;
}

static inline void rdma_ah_set_interface_id(struct rdma_ah_attr *attr,
					    __be64 if_id)
{
	struct ib_global_route *grh = rdma_ah_retrieve_grh(attr);

	grh->dgid.global.interface_id = if_id;
}

static inline void rdma_ah_set_grh(struct rdma_ah_attr *attr,
				   union ib_gid *dgid, u32 flow_label,
				   u8 sgid_index, u8 hop_limit,
				   u8 traffic_class)
{
	struct ib_global_route *grh = rdma_ah_retrieve_grh(attr);

	attr->ah_flags = IB_AH_GRH;
	if (dgid)
		grh->dgid = *dgid;
	grh->flow_label = flow_label;
	grh->sgid_index = sgid_index;
	grh->hop_limit = hop_limit;
	grh->traffic_class = traffic_class;
	grh->sgid_attr = NULL;
}

void rdma_destroy_ah_attr(struct rdma_ah_attr *ah_attr);
void rdma_move_grh_sgid_attr(struct rdma_ah_attr *attr, union ib_gid *dgid,
			     u32 flow_label, u8 hop_limit, u8 traffic_class,
			     const struct ib_gid_attr *sgid_attr);
void rdma_copy_ah_attr(struct rdma_ah_attr *dest,
		       const struct rdma_ah_attr *src);
void rdma_replace_ah_attr(struct rdma_ah_attr *old,
			  const struct rdma_ah_attr *new);
void rdma_move_ah_attr(struct rdma_ah_attr *dest, struct rdma_ah_attr *src);

/**
 * rdma_ah_find_type - Return address handle type.
 *
 * @dev: Device to be checked
 * @port_num: Port number
 */
static inline enum rdma_ah_attr_type rdma_ah_find_type(struct ib_device *dev,
						       u8 port_num)
{
	if (rdma_protocol_roce(dev, port_num))
		return RDMA_AH_ATTR_TYPE_ROCE;
	if (rdma_protocol_ib(dev, port_num)) {
		if (rdma_cap_opa_ah(dev, port_num))
			return RDMA_AH_ATTR_TYPE_OPA;
		return RDMA_AH_ATTR_TYPE_IB;
	}

	return RDMA_AH_ATTR_TYPE_UNDEFINED;
}

/**
 * ib_lid_cpu16 - Return lid in 16bit CPU encoding.
 *     In the current implementation the only way to get
 *     get the 32bit lid is from other sources for OPA.
 *     For IB, lids will always be 16bits so cast the
 *     value accordingly.
 *
 * @lid: A 32bit LID
 */
static inline u16 ib_lid_cpu16(u32 lid)
{
	WARN_ON_ONCE(lid & 0xFFFF0000);
	return (u16)lid;
}

/**
 * ib_lid_be16 - Return lid in 16bit BE encoding.
 *
 * @lid: A 32bit LID
 */
static inline __be16 ib_lid_be16(u32 lid)
{
	WARN_ON_ONCE(lid & 0xFFFF0000);
	return cpu_to_be16((u16)lid);
}

/**
 * ib_get_vector_affinity - Get the affinity mappings of a given completion
 *   vector
 * @device:         the rdma device
 * @comp_vector:    index of completion vector
 *
 * Returns NULL on failure, otherwise a corresponding cpu map of the
 * completion vector (returns all-cpus map if the device driver doesn't
 * implement get_vector_affinity).
 */
static inline const struct cpumask *
ib_get_vector_affinity(struct ib_device *device, int comp_vector)
{
	if (comp_vector < 0 || comp_vector >= device->num_comp_vectors ||
	    !device->ops.get_vector_affinity)
		return NULL;

	return device->ops.get_vector_affinity(device, comp_vector);

}

/**
 * rdma_roce_rescan_device - Rescan all of the network devices in the system
 * and add their gids, as needed, to the relevant RoCE devices.
 *
 * @device:         the rdma device
 */
void rdma_roce_rescan_device(struct ib_device *ibdev);

struct ib_ucontext *ib_uverbs_get_ucontext_file(struct ib_uverbs_file *ufile);

int uverbs_destroy_def_handler(struct uverbs_attr_bundle *attrs);

struct net_device *rdma_alloc_netdev(struct ib_device *device, u8 port_num,
				     enum rdma_netdev_t type, const char *name,
				     unsigned char name_assign_type,
				     void (*setup)(struct net_device *));

int rdma_init_netdev(struct ib_device *device, u8 port_num,
		     enum rdma_netdev_t type, const char *name,
		     unsigned char name_assign_type,
		     void (*setup)(struct net_device *),
		     struct net_device *netdev);

/**
 * rdma_set_device_sysfs_group - Set device attributes group to have
 *				 driver specific sysfs entries at
 *				 for infiniband class.
 *
 * @device:	device pointer for which attributes to be created
 * @group:	Pointer to group which should be added when device
 *		is registered with sysfs.
 * rdma_set_device_sysfs_group() allows existing drivers to expose one
 * group per device to have sysfs attributes.
 *
 * NOTE: New drivers should not make use of this API; instead new device
 * parameter should be exposed via netlink command. This API and mechanism
 * exist only for existing drivers.
 */
static inline void
rdma_set_device_sysfs_group(struct ib_device *dev,
			    const struct attribute_group *group)
{
	dev->groups[1] = group;
}

/**
 * rdma_device_to_ibdev - Get ib_device pointer from device pointer
 *
 * @device:	device pointer for which ib_device pointer to retrieve
 *
 * rdma_device_to_ibdev() retrieves ib_device pointer from device.
 *
 */
static inline struct ib_device *rdma_device_to_ibdev(struct device *device)
{
	struct ib_core_device *coredev =
		container_of(device, struct ib_core_device, dev);

	return coredev->owner;
}

/**
 * rdma_device_to_drv_device - Helper macro to reach back to driver's
 *			       ib_device holder structure from device pointer.
 *
 * NOTE: New drivers should not make use of this API; This API is only for
 * existing drivers who have exposed sysfs entries using
 * rdma_set_device_sysfs_group().
 */
#define rdma_device_to_drv_device(dev, drv_dev_struct, ibdev_member)           \
	container_of(rdma_device_to_ibdev(dev), drv_dev_struct, ibdev_member)

bool rdma_dev_access_netns(const struct ib_device *device,
			   const struct net *net);
#endif /* IB_VERBS_H */
