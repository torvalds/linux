/*
 * Copyright (c) 2017-2018 Cavium, Inc.
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#ifndef __ECORE_H
#define __ECORE_H

#include "ecore_status.h"
#include "ecore_hsi_common.h"
#include "ecore_hsi_debug_tools.h"
#include "ecore_hsi_init_func.h"
#include "ecore_hsi_init_tool.h"
#include "ecore_proto_if.h"
#include "mcp_public.h"

#define ECORE_MAJOR_VERSION		8
#define ECORE_MINOR_VERSION		33
#define ECORE_REVISION_VERSION		5
#define ECORE_ENGINEERING_VERSION	0

#define ECORE_VERSION							\
	((ECORE_MAJOR_VERSION << 24) | (ECORE_MINOR_VERSION << 16) |	\
	 (ECORE_REVISION_VERSION << 8) | ECORE_ENGINEERING_VERSION)

#define STORM_FW_VERSION						\
	((FW_MAJOR_VERSION << 24) | (FW_MINOR_VERSION << 16) |	\
	 (FW_REVISION_VERSION << 8) | FW_ENGINEERING_VERSION)

#define MAX_HWFNS_PER_DEVICE	2
#define NAME_SIZE 16
#define ARRAY_DECL static const
#define ECORE_WFQ_UNIT	100

/* Constants */
#define ECORE_WID_SIZE		(1024)
#define ECORE_MIN_WIDS		(4)

/* Configurable */
#define ECORE_PF_DEMS_SIZE	(4)

/* cau states */
enum ecore_coalescing_mode {
	ECORE_COAL_MODE_DISABLE,
	ECORE_COAL_MODE_ENABLE
};

enum ecore_nvm_cmd {
	ECORE_PUT_FILE_BEGIN = DRV_MSG_CODE_NVM_PUT_FILE_BEGIN,
	ECORE_PUT_FILE_DATA = DRV_MSG_CODE_NVM_PUT_FILE_DATA,
	ECORE_NVM_READ_NVRAM = DRV_MSG_CODE_NVM_READ_NVRAM,
	ECORE_NVM_WRITE_NVRAM = DRV_MSG_CODE_NVM_WRITE_NVRAM,
	ECORE_NVM_DEL_FILE = DRV_MSG_CODE_NVM_DEL_FILE,
	ECORE_EXT_PHY_FW_UPGRADE = DRV_MSG_CODE_EXT_PHY_FW_UPGRADE,
	ECORE_NVM_SET_SECURE_MODE = DRV_MSG_CODE_SET_SECURE_MODE,
	ECORE_PHY_RAW_READ = DRV_MSG_CODE_PHY_RAW_READ,
	ECORE_PHY_RAW_WRITE = DRV_MSG_CODE_PHY_RAW_WRITE,
	ECORE_PHY_CORE_READ = DRV_MSG_CODE_PHY_CORE_READ,
	ECORE_PHY_CORE_WRITE = DRV_MSG_CODE_PHY_CORE_WRITE,
	ECORE_ENCRYPT_PASSWORD = DRV_MSG_CODE_ENCRYPT_PASSWORD,
	ECORE_GET_MCP_NVM_RESP = 0xFFFFFF00
};

#ifndef LINUX_REMOVE
#if !defined(CONFIG_ECORE_L2) && !defined(CONFIG_ECORE_ROCE) && \
	!defined(CONFIG_ECORE_FCOE) && !defined(CONFIG_ECORE_ISCSI) && \
	!defined(CONFIG_ECORE_IWARP)
#define CONFIG_ECORE_L2
#define CONFIG_ECORE_SRIOV
#define CONFIG_ECORE_ROCE
#define CONFIG_ECORE_IWARP
#define CONFIG_ECORE_FCOE
#define CONFIG_ECORE_ISCSI
#define CONFIG_ECORE_LL2
#endif
#endif

/* helpers */
#ifndef __EXTRACT__LINUX__IF__
#define MASK_FIELD(_name, _value)					\
		((_value) &= (_name##_MASK))

#define FIELD_VALUE(_name, _value)					\
		((_value & _name##_MASK) << _name##_SHIFT)

#define SET_FIELD(value, name, flag)					\
do {									\
	(value) &= ~(name##_MASK << name##_SHIFT);			\
	(value) |= ((((u64)flag) & (u64)name##_MASK) << (name##_SHIFT));\
} while (0)

#define GET_FIELD(value, name)						\
	(((value) >> (name##_SHIFT)) & name##_MASK)

#define GET_MFW_FIELD(name, field)					\
	(((name) & (field ## _MASK)) >> (field ## _OFFSET))

#define SET_MFW_FIELD(name, field, value)				\
do {									\
	(name) &= ~(field ## _MASK);					\
	(name) |= (((value) << (field ## _OFFSET)) & (field ## _MASK));	\
} while (0)
#endif

static OSAL_INLINE u32 DB_ADDR(u32 cid, u32 DEMS)
{
	u32 db_addr = FIELD_VALUE(DB_LEGACY_ADDR_DEMS, DEMS) |
		      (cid * ECORE_PF_DEMS_SIZE);

	return db_addr;
}

static OSAL_INLINE u32 DB_ADDR_VF(u32 cid, u32 DEMS)
{
	u32 db_addr = FIELD_VALUE(DB_LEGACY_ADDR_DEMS, DEMS) |
		      FIELD_VALUE(DB_LEGACY_ADDR_ICID, cid);

	return db_addr;
}

#define ALIGNED_TYPE_SIZE(type_name, p_hwfn)				  \
	((sizeof(type_name) + (u32)(1<<(p_hwfn->p_dev->cache_shift))-1) & \
	 ~((1<<(p_hwfn->p_dev->cache_shift))-1))

#ifndef LINUX_REMOVE
#ifndef U64_HI
#define U64_HI(val) ((u32)(((u64)(val))  >> 32))
#endif

#ifndef U64_LO
#define U64_LO(val) ((u32)(((u64)(val)) & 0xffffffff))
#endif
#endif

#ifndef __EXTRACT__LINUX__IF__
#ifndef UEFI
/* Debug print definitions */
#define DP_ERR(p_dev, fmt, ...)				\
do {							\
	PRINT_ERR((p_dev)->dp_ctx, "[%s:%d(%s)]" fmt,	\
		  __func__, __LINE__,			\
		  (p_dev)->name ? (p_dev)->name : "",	\
		  ##__VA_ARGS__);			\
} while (0)

#define DP_NOTICE(p_dev, is_assert, fmt, ...)				\
do {									\
	if (OSAL_UNLIKELY((p_dev)->dp_level <= ECORE_LEVEL_NOTICE)) {	\
		PRINT((p_dev)->dp_ctx, "[%s:%d(%s)]" fmt,		\
		      __func__, __LINE__,				\
		      (p_dev)->name ? (p_dev)->name : "",		\
		      ##__VA_ARGS__);					\
		OSAL_ASSERT(!is_assert);				\
	}								\
} while (0)

#define DP_INFO(p_dev, fmt, ...)				      \
do {								      \
	if (OSAL_UNLIKELY((p_dev)->dp_level <= ECORE_LEVEL_INFO)) {   \
		PRINT((p_dev)->dp_ctx, "[%s:%d(%s)]" fmt,	      \
		      __func__, __LINE__,			      \
		      (p_dev)->name ? (p_dev)->name : "",	      \
		      ##__VA_ARGS__);				      \
	}							      \
} while (0)

#define DP_VERBOSE(p_dev, module, fmt, ...)				\
do {									\
	if (OSAL_UNLIKELY(((p_dev)->dp_level <= ECORE_LEVEL_VERBOSE) &&	\
	    ((p_dev)->dp_module & module))) {				\
		PRINT((p_dev)->dp_ctx, "[%s:%d(%s)]" fmt,		\
		      __func__, __LINE__,				\
		      (p_dev)->name ? (p_dev)->name : "",		\
		      ##__VA_ARGS__);					\
	}								\
} while (0)
#endif

enum DP_LEVEL {
	ECORE_LEVEL_VERBOSE	= 0x0,
	ECORE_LEVEL_INFO	= 0x1,
	ECORE_LEVEL_NOTICE	= 0x2,
	ECORE_LEVEL_ERR		= 0x3,
};

#define ECORE_LOG_LEVEL_SHIFT	(30)
#define ECORE_LOG_VERBOSE_MASK	(0x3fffffff)
#define ECORE_LOG_INFO_MASK	(0x40000000)
#define ECORE_LOG_NOTICE_MASK	(0x80000000)

enum DP_MODULE {
#ifndef LINUX_REMOVE
	ECORE_MSG_DRV		= 0x0001,
	ECORE_MSG_PROBE		= 0x0002,
	ECORE_MSG_LINK		= 0x0004,
	ECORE_MSG_TIMER		= 0x0008,
	ECORE_MSG_IFDOWN	= 0x0010,
	ECORE_MSG_IFUP		= 0x0020,
	ECORE_MSG_RX_ERR	= 0x0040,
	ECORE_MSG_TX_ERR	= 0x0080,
	ECORE_MSG_TX_QUEUED	= 0x0100,
	ECORE_MSG_INTR		= 0x0200,
	ECORE_MSG_TX_DONE	= 0x0400,
	ECORE_MSG_RX_STATUS	= 0x0800,
	ECORE_MSG_PKTDATA	= 0x1000,
	ECORE_MSG_HW		= 0x2000,
	ECORE_MSG_WOL		= 0x4000,
#endif
	ECORE_MSG_SPQ		= 0x10000,
	ECORE_MSG_STATS		= 0x20000,
	ECORE_MSG_DCB		= 0x40000,
	ECORE_MSG_IOV		= 0x80000,
	ECORE_MSG_SP		= 0x100000,
	ECORE_MSG_STORAGE	= 0x200000,
	ECORE_MSG_OOO		= 0x200000,
	ECORE_MSG_CXT		= 0x800000,
	ECORE_MSG_LL2		= 0x1000000,
	ECORE_MSG_ILT		= 0x2000000,
	ECORE_MSG_RDMA		= 0x4000000,
	ECORE_MSG_DEBUG		= 0x8000000,
	/* to be added...up to 0x8000000 */
};
#endif

#define for_each_hwfn(p_dev, i)	for (i = 0; i < p_dev->num_hwfns; i++)

#define D_TRINE(val, cond1, cond2, true1, true2, def) \
	(val == (cond1) ? true1 : \
	 (val == (cond2) ? true2 : def))

/* forward */
struct ecore_ptt_pool;
struct ecore_spq;
struct ecore_sb_info;
struct ecore_sb_attn_info;
struct ecore_cxt_mngr;
struct ecore_dma_mem;
struct ecore_sb_sp_info;
struct ecore_ll2_info;
struct ecore_l2_info;
struct ecore_igu_info;
struct ecore_mcp_info;
struct ecore_dcbx_info;
struct ecore_llh_info;

struct ecore_rt_data {
	u32	*init_val;
	bool	*b_valid;
};

enum ecore_tunn_mode {
	ECORE_MODE_L2GENEVE_TUNN,
	ECORE_MODE_IPGENEVE_TUNN,
	ECORE_MODE_L2GRE_TUNN,
	ECORE_MODE_IPGRE_TUNN,
	ECORE_MODE_VXLAN_TUNN,
};

enum ecore_tunn_clss {
	ECORE_TUNN_CLSS_MAC_VLAN,
	ECORE_TUNN_CLSS_MAC_VNI,
	ECORE_TUNN_CLSS_INNER_MAC_VLAN,
	ECORE_TUNN_CLSS_INNER_MAC_VNI,
	ECORE_TUNN_CLSS_MAC_VLAN_DUAL_STAGE,
	MAX_ECORE_TUNN_CLSS,
};

struct ecore_tunn_update_type {
	bool b_update_mode;
	bool b_mode_enabled;
	enum ecore_tunn_clss tun_cls;
};

struct ecore_tunn_update_udp_port {
	bool b_update_port;
	u16 port;
};

struct ecore_tunnel_info {
	struct ecore_tunn_update_type vxlan;
	struct ecore_tunn_update_type l2_geneve;
	struct ecore_tunn_update_type ip_geneve;
	struct ecore_tunn_update_type l2_gre;
	struct ecore_tunn_update_type ip_gre;

	struct ecore_tunn_update_udp_port vxlan_port;
	struct ecore_tunn_update_udp_port geneve_port;

	bool b_update_rx_cls;
	bool b_update_tx_cls;
};

/* The PCI personality is not quite synonymous to protocol ID:
 * 1. All personalities need CORE connections
 * 2. The Ethernet personality may support also the RoCE/iWARP protocol
 */
enum ecore_pci_personality {
	ECORE_PCI_ETH,
	ECORE_PCI_FCOE,
	ECORE_PCI_ISCSI,
	ECORE_PCI_ETH_ROCE,
	ECORE_PCI_ETH_IWARP,
	ECORE_PCI_ETH_RDMA,
	ECORE_PCI_DEFAULT /* default in shmem */
};

/* All VFs are symetric, all counters are PF + all VFs */
struct ecore_qm_iids {
	u32 cids;
	u32 vf_cids;
	u32 tids;
};

/* The PCI relax ordering is either taken care by management FW or can be
 * enable/disable by ecore client.
 */
enum ecore_pci_rlx_odr {
	ECORE_DEFAULT_RLX_ODR,
	ECORE_ENABLE_RLX_ODR,
	ECORE_DISABLE_RLX_ODR
};

#define MAX_PF_PER_PORT 8

/* HW / FW resources, output of features supported below, most information
 * is received from MFW.
 */
enum ecore_resources {
	ECORE_L2_QUEUE,
	ECORE_VPORT,
	ECORE_RSS_ENG,
	ECORE_PQ,
	ECORE_RL,
	ECORE_MAC,
	ECORE_VLAN,
	ECORE_RDMA_CNQ_RAM,
	ECORE_ILT,
	ECORE_LL2_QUEUE,
	ECORE_CMDQS_CQS,
	ECORE_RDMA_STATS_QUEUE,
	ECORE_BDQ,

	/* This is needed only internally for matching against the IGU.
	 * In case of legacy MFW, would be set to `0'.
	 */
	ECORE_SB,

	ECORE_MAX_RESC,
};

/* Features that require resources, given as input to the resource management
 * algorithm, the output are the resources above
 */
enum ecore_feature {
	ECORE_PF_L2_QUE,
	ECORE_PF_TC,
	ECORE_VF,
	ECORE_EXTRA_VF_QUE,
	ECORE_VMQ,
	ECORE_RDMA_CNQ,
	ECORE_ISCSI_CQ,
	ECORE_FCOE_CQ,
	ECORE_VF_L2_QUE,
	ECORE_MAX_FEATURES,
};

enum ecore_port_mode {
	ECORE_PORT_MODE_DE_2X40G,
	ECORE_PORT_MODE_DE_2X50G,
	ECORE_PORT_MODE_DE_1X100G,
	ECORE_PORT_MODE_DE_4X10G_F,
	ECORE_PORT_MODE_DE_4X10G_E,
	ECORE_PORT_MODE_DE_4X20G,
	ECORE_PORT_MODE_DE_1X40G,
	ECORE_PORT_MODE_DE_2X25G,
	ECORE_PORT_MODE_DE_1X25G,
	ECORE_PORT_MODE_DE_4X25G,
	ECORE_PORT_MODE_DE_2X10G,
};

enum ecore_dev_cap {
	ECORE_DEV_CAP_ETH,
	ECORE_DEV_CAP_FCOE,
	ECORE_DEV_CAP_ISCSI,
	ECORE_DEV_CAP_ROCE,
	ECORE_DEV_CAP_IWARP
};

#ifndef __EXTRACT__LINUX__IF__
enum ecore_hw_err_type {
	ECORE_HW_ERR_FAN_FAIL,
	ECORE_HW_ERR_MFW_RESP_FAIL,
	ECORE_HW_ERR_HW_ATTN,
	ECORE_HW_ERR_DMAE_FAIL,
	ECORE_HW_ERR_RAMROD_FAIL,
	ECORE_HW_ERR_FW_ASSERT,
};
#endif

enum ecore_wol_support {
	ECORE_WOL_SUPPORT_NONE,
	ECORE_WOL_SUPPORT_PME,
};

enum ecore_db_rec_exec {
	DB_REC_DRY_RUN,
	DB_REC_REAL_DEAL,
	DB_REC_ONCE,
};

struct ecore_hw_info {
	/* PCI personality */
	enum ecore_pci_personality personality;
#define ECORE_IS_RDMA_PERSONALITY(dev) \
	((dev)->hw_info.personality == ECORE_PCI_ETH_ROCE || \
	 (dev)->hw_info.personality == ECORE_PCI_ETH_IWARP || \
	 (dev)->hw_info.personality == ECORE_PCI_ETH_RDMA)
#define ECORE_IS_ROCE_PERSONALITY(dev) \
	((dev)->hw_info.personality == ECORE_PCI_ETH_ROCE || \
	 (dev)->hw_info.personality == ECORE_PCI_ETH_RDMA)
#define ECORE_IS_IWARP_PERSONALITY(dev) \
	((dev)->hw_info.personality == ECORE_PCI_ETH_IWARP || \
	 (dev)->hw_info.personality == ECORE_PCI_ETH_RDMA)
#define ECORE_IS_L2_PERSONALITY(dev) \
	((dev)->hw_info.personality == ECORE_PCI_ETH || \
	 ECORE_IS_RDMA_PERSONALITY(dev))
#define ECORE_IS_FCOE_PERSONALITY(dev) \
	((dev)->hw_info.personality == ECORE_PCI_FCOE)
#define ECORE_IS_ISCSI_PERSONALITY(dev) \
	((dev)->hw_info.personality == ECORE_PCI_ISCSI)

	/* Resource Allocation scheme results */
	u32 resc_start[ECORE_MAX_RESC];
	u32 resc_num[ECORE_MAX_RESC];
	u32 feat_num[ECORE_MAX_FEATURES];

	#define RESC_START(_p_hwfn, resc) ((_p_hwfn)->hw_info.resc_start[resc])
	#define RESC_NUM(_p_hwfn, resc) ((_p_hwfn)->hw_info.resc_num[resc])
	#define RESC_END(_p_hwfn, resc) (RESC_START(_p_hwfn, resc) + \
					 RESC_NUM(_p_hwfn, resc))
	#define FEAT_NUM(_p_hwfn, resc) ((_p_hwfn)->hw_info.feat_num[resc])

	/* Amount of traffic classes HW supports */
	u8 num_hw_tc;

	/* Amount of TCs which should be active according to DCBx or upper layer driver configuration */
	u8 num_active_tc;

	/* The traffic class used by PF for it's offloaded protocol */
	u8 offload_tc;

	u32 concrete_fid;
	u16 opaque_fid;
	u16 ovlan;
	u32 part_num[4];

#ifndef ETH_ALEN
#define ETH_ALEN 6 /* @@@ TBD - define somewhere else for Windows */
#endif
	unsigned char hw_mac_addr[ETH_ALEN];

	u16 num_iscsi_conns;
	u16 num_fcoe_conns;

	struct ecore_igu_info *p_igu_info;
	/* Sriov */
	u8 max_chains_per_vf;

	u32 port_mode;
	u32	hw_mode;
	unsigned long device_capabilities;

#ifndef __EXTRACT__LINUX__THROW__
	/* Default DCBX mode */
	u8 dcbx_mode;
#endif

	u16 mtu;

	enum ecore_wol_support		b_wol_support;
};

/* maximun size of read/write commands (HW limit) */
#define DMAE_MAX_RW_SIZE	0x2000

struct ecore_dmae_info {
	/* Spinlock for synchronizing access to functions */
	osal_spinlock_t lock;

	bool b_mem_ready;

	u8 channel;

	dma_addr_t completion_word_phys_addr;

	/* The memory location where the DMAE writes the completion
	 * value when an operation is finished on this context.
	 */
	u32 *p_completion_word;

	dma_addr_t intermediate_buffer_phys_addr;

	/* An intermediate buffer for DMAE operations that use virtual
	 * addresses - data is DMA'd to/from this buffer and then
	 * memcpy'd to/from the virtual address
	 */
	u32 *p_intermediate_buffer;

	dma_addr_t dmae_cmd_phys_addr;
	struct dmae_cmd *p_dmae_cmd;
};

struct ecore_wfq_data {
	u32 default_min_speed; /* When wfq feature is not configured */
	u32 min_speed; /* when feature is configured for any 1 vport */
	bool configured;
};

struct ecore_qm_info {
	struct init_qm_pq_params    *qm_pq_params;
	struct init_qm_vport_params *qm_vport_params;
	struct init_qm_port_params  *qm_port_params;
	u16			start_pq;
	u8			start_vport;
	u16			pure_lb_pq;
	u16			offload_pq;
	u16			low_latency_pq;
	u16			pure_ack_pq;
	u16			ooo_pq;
	u16			first_vf_pq;
	u16			first_mcos_pq;
	u16			first_rl_pq;
	u16			num_pqs;
	u16			num_vf_pqs;
	u8			num_vports;
	u8			max_phys_tcs_per_port;
	u8			ooo_tc;
	bool			pf_rl_en;
	bool			pf_wfq_en;
	bool			vport_rl_en;
	bool			vport_wfq_en;
	u8			pf_wfq;
	u32			pf_rl;
	struct ecore_wfq_data	*wfq_data;
	u8			num_pf_rls;
};

struct ecore_db_recovery_info {
	osal_list_t list;
	osal_spinlock_t lock;
	u32 db_recovery_counter;
};

struct storm_stats {
	u32 address;
	u32 len;
};

struct ecore_fw_data {
#ifdef CONFIG_ECORE_BINARY_FW
	struct fw_ver_info *fw_ver_info;
#endif
	const u8 *modes_tree_buf;
	union init_op *init_ops;
	const u32 *arr_data;
	u32 init_ops_size;
};

enum ecore_mf_mode_bit {
	/* Supports PF-classification based on tag */
	ECORE_MF_OVLAN_CLSS,

	/* Supports PF-classification based on MAC */
	ECORE_MF_LLH_MAC_CLSS,

	/* Supports PF-classification based on protocol type */
	ECORE_MF_LLH_PROTO_CLSS,

	/* Requires a default PF to be set */
	ECORE_MF_NEED_DEF_PF,

	/* Allow LL2 to multicast/broadcast */
	ECORE_MF_LL2_NON_UNICAST,

	/* Allow Cross-PF [& child VFs] Tx-switching */
	ECORE_MF_INTER_PF_SWITCH,

	/* TODO - if we ever re-utilize any of this logic, we can rename */
	ECORE_MF_UFP_SPECIFIC,

	ECORE_MF_DISABLE_ARFS,

	/* Use vlan for steering */
	ECORE_MF_8021Q_TAGGING,

	/* Use stag for steering */
	ECORE_MF_8021AD_TAGGING,
};

enum ecore_ufp_mode {
	ECORE_UFP_MODE_ETS,
	ECORE_UFP_MODE_VNIC_BW,
	ECORE_UFP_MODE_UNKNOWN
};

enum ecore_ufp_pri_type {
	ECORE_UFP_PRI_OS,
	ECORE_UFP_PRI_VNIC,
	ECORE_UFP_PRI_UNKNOWN
};

struct ecore_ufp_info {
	enum ecore_ufp_pri_type pri_type;
	enum ecore_ufp_mode mode;
	u8 tc;
};

enum BAR_ID {
	BAR_ID_0,	/* used for GRC */
	BAR_ID_1	/* Used for doorbells */
};

struct ecore_hwfn {
	struct ecore_dev		*p_dev;
	u8				my_id;		/* ID inside the PF */
#define IS_LEAD_HWFN(edev)		(!((edev)->my_id))
	u8				rel_pf_id;	/* Relative to engine*/
	u8				abs_pf_id;
#define ECORE_PATH_ID(_p_hwfn) \
	(ECORE_IS_BB((_p_hwfn)->p_dev) ? ((_p_hwfn)->abs_pf_id & 1) : 0)
	u8				port_id;
	bool				b_active;

	u32				dp_module;
	u8				dp_level;
	char				name[NAME_SIZE];
	void				*dp_ctx;

	bool				hw_init_done;

	u8				num_funcs_on_engine;
	u8				enabled_func_idx;

	/* BAR access */
	void OSAL_IOMEM			*regview;
	void OSAL_IOMEM			*doorbells;
	u64				db_phys_addr;
	unsigned long			db_size;

#ifndef LINUX_REMOVE
	u64				reg_offset;
	u64				db_offset;
#endif

	/* PTT pool */
	struct ecore_ptt_pool		*p_ptt_pool;

	/* HW info */
	struct ecore_hw_info		hw_info;

	/* rt_array (for init-tool) */
	struct ecore_rt_data		rt_data;

	/* SPQ */
	struct ecore_spq		*p_spq;

	/* EQ */
	struct ecore_eq			*p_eq;

	/* Consolidate Q*/
	struct ecore_consq		*p_consq;

	/* Slow-Path definitions */
	osal_dpc_t			sp_dpc;
	bool				b_sp_dpc_enabled;

	struct ecore_ptt		*p_main_ptt;
	struct ecore_ptt		*p_dpc_ptt;

	/* PTP will be used only by the leading funtion.
	 * Usage of all PTP-apis should be synchronized as result.
	 */
	struct ecore_ptt		*p_ptp_ptt;

	struct ecore_sb_sp_info		*p_sp_sb;
	struct ecore_sb_attn_info	*p_sb_attn;

	/* Protocol related */
	bool				using_ll2;
	struct ecore_ll2_info		*p_ll2_info;
	struct ecore_ooo_info		*p_ooo_info;
	struct ecore_iscsi_info		*p_iscsi_info;
	struct ecore_fcoe_info		*p_fcoe_info;
	struct ecore_rdma_info		*p_rdma_info;
	struct ecore_pf_params		pf_params;

	bool				b_rdma_enabled_in_prs;
	u32				rdma_prs_search_reg;

	struct ecore_cxt_mngr		*p_cxt_mngr;

	/* Flag indicating whether interrupts are enabled or not*/
	bool				b_int_enabled;
	bool				b_int_requested;

	/* True if the driver requests for the link */
	bool				b_drv_link_init;

	struct ecore_vf_iov		*vf_iov_info;
	struct ecore_pf_iov		*pf_iov_info;
	struct ecore_mcp_info		*mcp_info;
	struct ecore_dcbx_info		*p_dcbx_info;
	struct ecore_ufp_info		ufp_info;

	struct ecore_dmae_info		dmae_info;

	/* QM init */
	struct ecore_qm_info		qm_info;

	/* Buffer for unzipping firmware data */
#ifdef CONFIG_ECORE_ZIPPED_FW
	void *unzip_buf;
#endif

	struct dbg_tools_data		dbg_info;

	/* PWM region specific data */
	u16				wid_count;
	u32				dpi_size;
	u32				dpi_count;
	u32				dpi_start_offset; /* this is used to
							   * calculate th
							   * doorbell address
							   */

	/* If one of the following is set then EDPM shouldn't be used */
	u8				dcbx_no_edpm;
	u8				db_bar_no_edpm;

	/* L2-related */
	struct ecore_l2_info		*p_l2_info;

	/* Mechanism for recovering from doorbell drop */
	struct ecore_db_recovery_info	db_recovery_info;
};

#ifndef __EXTRACT__LINUX__THROW__
enum ecore_mf_mode {
	ECORE_MF_DEFAULT,
	ECORE_MF_OVLAN,
	ECORE_MF_NPAR,
	ECORE_MF_UFP,
};
#endif

#ifndef __EXTRACT__LINUX__IF__
enum ecore_dev_type {
	ECORE_DEV_TYPE_BB,
	ECORE_DEV_TYPE_AH,
	ECORE_DEV_TYPE_E5,
};
#endif

struct ecore_dev {
	u32				dp_module;
	u8				dp_level;
	char				name[NAME_SIZE];
	void				*dp_ctx;

	enum ecore_dev_type		type;
/* Translate type/revision combo into the proper conditions */
#define ECORE_IS_BB(dev)	((dev)->type == ECORE_DEV_TYPE_BB)
#define ECORE_IS_BB_A0(dev)	(ECORE_IS_BB(dev) && CHIP_REV_IS_A0(dev))
#ifndef ASIC_ONLY
#define ECORE_IS_BB_B0(dev)	((ECORE_IS_BB(dev) && CHIP_REV_IS_B0(dev)) || \
				 (CHIP_REV_IS_TEDIBEAR(dev)))
#else
#define ECORE_IS_BB_B0(dev)	(ECORE_IS_BB(dev) && CHIP_REV_IS_B0(dev))
#endif
#define ECORE_IS_AH(dev)	((dev)->type == ECORE_DEV_TYPE_AH)
#define ECORE_IS_K2(dev)	ECORE_IS_AH(dev)
#define ECORE_IS_E4(dev)	(ECORE_IS_BB(dev) || ECORE_IS_AH(dev))
#define ECORE_IS_E5(dev)	((dev)->type == ECORE_DEV_TYPE_E5)

#define ECORE_E5_MISSING_CODE	OSAL_BUILD_BUG_ON(false)

	u16 vendor_id;
	u16 device_id;
#define ECORE_DEV_ID_MASK	0xff00
#define ECORE_DEV_ID_MASK_BB	0x1600
#define ECORE_DEV_ID_MASK_AH	0x8000
#define ECORE_DEV_ID_MASK_E5	0x8100

	u16				chip_num;
#define CHIP_NUM_MASK			0xffff
#define CHIP_NUM_SHIFT			0

	u8				chip_rev;
#define CHIP_REV_MASK			0xf
#define CHIP_REV_SHIFT			0
#ifndef ASIC_ONLY
#define CHIP_REV_IS_TEDIBEAR(_p_dev)	((_p_dev)->chip_rev == 0x5)
#define CHIP_REV_IS_EMUL_A0(_p_dev)	((_p_dev)->chip_rev == 0xe)
#define CHIP_REV_IS_EMUL_B0(_p_dev)	((_p_dev)->chip_rev == 0xc)
#define CHIP_REV_IS_EMUL(_p_dev) \
	(CHIP_REV_IS_EMUL_A0(_p_dev) || CHIP_REV_IS_EMUL_B0(_p_dev))
#define CHIP_REV_IS_FPGA_A0(_p_dev)	((_p_dev)->chip_rev == 0xf)
#define CHIP_REV_IS_FPGA_B0(_p_dev)	((_p_dev)->chip_rev == 0xd)
#define CHIP_REV_IS_FPGA(_p_dev) \
	(CHIP_REV_IS_FPGA_A0(_p_dev) || CHIP_REV_IS_FPGA_B0(_p_dev))
#define CHIP_REV_IS_SLOW(_p_dev) \
	(CHIP_REV_IS_EMUL(_p_dev) || CHIP_REV_IS_FPGA(_p_dev))
#define CHIP_REV_IS_A0(_p_dev) \
	(CHIP_REV_IS_EMUL_A0(_p_dev) || CHIP_REV_IS_FPGA_A0(_p_dev) || \
	 (!(_p_dev)->chip_rev && !(_p_dev)->chip_metal))
#define CHIP_REV_IS_B0(_p_dev) \
	(CHIP_REV_IS_EMUL_B0(_p_dev) || CHIP_REV_IS_FPGA_B0(_p_dev) || \
	 ((_p_dev)->chip_rev == 1 && !(_p_dev)->chip_metal))
#define CHIP_REV_IS_ASIC(_p_dev)	!CHIP_REV_IS_SLOW(_p_dev)
#else
#define CHIP_REV_IS_A0(_p_dev) \
	(!(_p_dev)->chip_rev && !(_p_dev)->chip_metal)
#define CHIP_REV_IS_B0(_p_dev) \
	((_p_dev)->chip_rev == 1 && !(_p_dev)->chip_metal)
#endif

	u8				chip_metal;
#define CHIP_METAL_MASK			0xff
#define CHIP_METAL_SHIFT		0

	u8				chip_bond_id;
#define CHIP_BOND_ID_MASK		0xff
#define CHIP_BOND_ID_SHIFT		0

	u8				num_engines;
	u8				num_ports;
	u8				num_ports_in_engine;
	u8				num_funcs_in_port;

	u8				path_id;

	unsigned long			mf_bits;
#ifndef __EXTRACT__LINUX__THROW__
	enum ecore_mf_mode		mf_mode;
#define IS_MF_DEFAULT(_p_hwfn)	(((_p_hwfn)->p_dev)->mf_mode == ECORE_MF_DEFAULT)
#define IS_MF_SI(_p_hwfn)	(((_p_hwfn)->p_dev)->mf_mode == ECORE_MF_NPAR)
#define IS_MF_SD(_p_hwfn)	(((_p_hwfn)->p_dev)->mf_mode == ECORE_MF_OVLAN)
#endif

	int				pcie_width;
	int				pcie_speed;

	/* Add MF related configuration */
	u8				mcp_rev;
	u8				boot_mode;

	/* WoL related configurations */
	u8				wol_config;
	u8				wol_mac[ETH_ALEN];

	u32				int_mode;
	enum ecore_coalescing_mode	int_coalescing_mode;
	u16				rx_coalesce_usecs;
	u16				tx_coalesce_usecs;

	/* Start Bar offset of first hwfn */
	void OSAL_IOMEM			*regview;
	void OSAL_IOMEM			*doorbells;
	u64				db_phys_addr;
	unsigned long			db_size;

	/* PCI */
	u8				cache_shift;

	/* Init */
	const struct iro		*iro_arr;
	#define IRO (p_hwfn->p_dev->iro_arr)

	/* HW functions */
	u8				num_hwfns;
	struct ecore_hwfn		hwfns[MAX_HWFNS_PER_DEVICE];
#define ECORE_LEADING_HWFN(dev)		(&dev->hwfns[0])
#define ECORE_IS_CMT(dev)		((dev)->num_hwfns > 1)

	/* Engine affinity */
	u8				l2_affin_hint;
	u8				fir_affin;
	u8				iwarp_affin;
	/* Macro for getting the engine-affinitized hwfn for FCoE/iSCSI/RoCE */
#define ECORE_FIR_AFFIN_HWFN(dev)	(&dev->hwfns[dev->fir_affin])
	/* Macro for getting the engine-affinitized hwfn for iWARP */
#define ECORE_IWARP_AFFIN_HWFN(dev)	(&dev->hwfns[dev->iwarp_affin])
	/* Generic macro for getting the engine-affinitized hwfn */
#define ECORE_AFFIN_HWFN(dev) \
	(ECORE_IS_IWARP_PERSONALITY(ECORE_LEADING_HWFN(dev)) ? \
	 ECORE_IWARP_AFFIN_HWFN(dev) : \
	 ECORE_FIR_AFFIN_HWFN(dev))
	/* Macro for getting the index (0/1) of the engine-affinitized hwfn */
#define ECORE_AFFIN_HWFN_IDX(dev) \
	(IS_LEAD_HWFN(ECORE_AFFIN_HWFN(dev)) ? 0 : 1)

	/* SRIOV */
	struct ecore_hw_sriov_info	*p_iov_info;
#define IS_ECORE_SRIOV(p_dev)		(!!(p_dev)->p_iov_info)
	struct ecore_tunnel_info	tunnel;
	bool				b_is_vf;
	bool				b_dont_override_vf_msix;

	u32				drv_type;

	u32				rdma_max_sge;
	u32				rdma_max_inline;
	u32				rdma_max_srq_sge;
	u8				ilt_page_size;

	struct ecore_eth_stats		*reset_stats;
	struct ecore_fw_data		*fw_data;

	u32				mcp_nvm_resp;

	/* Recovery */
	bool				recov_in_prog;

	/* Indicates whether should prevent attentions from being reasserted */
	bool				attn_clr_en;

	/* Indicates whether allowing the MFW to collect a crash dump */
	bool				allow_mdump;

	/* Indicates if the reg_fifo is checked after any register access */
	bool				chk_reg_fifo;

#ifndef ASIC_ONLY
	bool				b_is_emul_full;
#endif
	/* LLH info */
	u8				ppfid_bitmap;
	struct ecore_llh_info		*p_llh_info;
};

#define NUM_OF_VFS(dev)		(ECORE_IS_BB(dev) ? MAX_NUM_VFS_BB \
						  : MAX_NUM_VFS_K2)
#define NUM_OF_L2_QUEUES(dev)	(ECORE_IS_BB(dev) ? MAX_NUM_L2_QUEUES_BB \
						  : MAX_NUM_L2_QUEUES_K2)
#define NUM_OF_PORTS(dev)	(ECORE_IS_BB(dev) ? MAX_NUM_PORTS_BB \
						  : MAX_NUM_PORTS_K2)
#define NUM_OF_SBS(dev)		(ECORE_IS_BB(dev) ? MAX_SB_PER_PATH_BB \
						  : MAX_SB_PER_PATH_K2)
#define NUM_OF_ENG_PFS(dev)	(ECORE_IS_BB(dev) ? MAX_NUM_PFS_BB \
						  : MAX_NUM_PFS_K2)

#ifndef LINUX_REMOVE
#define CRC8_TABLE_SIZE 256
#endif

/**
 * @brief ecore_concrete_to_sw_fid - get the sw function id from
 *        the concrete value.
 *
 * @param concrete_fid
 *
 * @return OSAL_INLINE u8
 */
static OSAL_INLINE u8 ecore_concrete_to_sw_fid(u32 concrete_fid)
{
	u8 vfid     = GET_FIELD(concrete_fid, PXP_CONCRETE_FID_VFID);
	u8 pfid     = GET_FIELD(concrete_fid, PXP_CONCRETE_FID_PFID);
	u8 vf_valid = GET_FIELD(concrete_fid, PXP_CONCRETE_FID_VFVALID);
	u8 sw_fid;

	if (vf_valid)
		sw_fid = vfid + MAX_NUM_PFS;
	else
		sw_fid = pfid;

	return sw_fid;
}

#define PKT_LB_TC 9
#define MAX_NUM_VOQS_E4	20

int ecore_configure_vport_wfq(struct ecore_dev *p_dev, u16 vp_id, u32 rate);
void ecore_configure_vp_wfq_on_link_change(struct ecore_dev *p_dev,
					   struct ecore_ptt *p_ptt,
					   u32 min_pf_rate);

int ecore_configure_pf_max_bandwidth(struct ecore_dev *p_dev, u8 max_bw);
int ecore_configure_pf_min_bandwidth(struct ecore_dev *p_dev, u8 min_bw);
void ecore_clean_wfq_db(struct ecore_hwfn *p_hwfn, struct ecore_ptt *p_ptt);
int ecore_device_num_engines(struct ecore_dev *p_dev);
int ecore_device_num_ports(struct ecore_dev *p_dev);
void ecore_set_fw_mac_addr(__le16 *fw_msb, __le16 *fw_mid, __le16 *fw_lsb,
			   u8 *mac);

/* Flags for indication of required queues */
#define PQ_FLAGS_RLS	(1 << 0)
#define PQ_FLAGS_MCOS	(1 << 1)
#define PQ_FLAGS_LB	(1 << 2)
#define PQ_FLAGS_OOO	(1 << 3)
#define PQ_FLAGS_ACK	(1 << 4)
#define PQ_FLAGS_OFLD	(1 << 5)
#define PQ_FLAGS_VFS	(1 << 6)
#define PQ_FLAGS_LLT	(1 << 7)

/* physical queue index for cm context intialization */
u16 ecore_get_cm_pq_idx(struct ecore_hwfn *p_hwfn, u32 pq_flags);
u16 ecore_get_cm_pq_idx_mcos(struct ecore_hwfn *p_hwfn, u8 tc);
u16 ecore_get_cm_pq_idx_vf(struct ecore_hwfn *p_hwfn, u16 vf);
u16 ecore_get_cm_pq_idx_rl(struct ecore_hwfn *p_hwfn, u8 qpid);

const char *ecore_hw_get_resc_name(enum ecore_resources res_id);

/* doorbell recovery mechanism */
void ecore_db_recovery_dp(struct ecore_hwfn *p_hwfn);
void ecore_db_recovery_execute(struct ecore_hwfn *p_hwfn,
			       enum ecore_db_rec_exec db_exec);

/* amount of resources used in qm init */
u8 ecore_init_qm_get_num_tcs(struct ecore_hwfn *p_hwfn);
u16 ecore_init_qm_get_num_vfs(struct ecore_hwfn *p_hwfn);
u16 ecore_init_qm_get_num_pf_rls(struct ecore_hwfn *p_hwfn);
u16 ecore_init_qm_get_num_vports(struct ecore_hwfn *p_hwfn);
u16 ecore_init_qm_get_num_pqs(struct ecore_hwfn *p_hwfn);

#define MFW_PORT(_p_hwfn)	((_p_hwfn)->abs_pf_id % \
				 ecore_device_num_ports((_p_hwfn)->p_dev))

/* The PFID<->PPFID calculation is based on the relative index of a PF on its
 * port. In BB there is a bug in the LLH in which the PPFID is actually engine
 * based, and thus it equals the PFID.
 */
#define ECORE_PFID_BY_PPFID(_p_hwfn, abs_ppfid) \
	(ECORE_IS_BB((_p_hwfn)->p_dev) ? \
	 (abs_ppfid) : \
	 (abs_ppfid) * (_p_hwfn)->p_dev->num_ports_in_engine + \
	 MFW_PORT(_p_hwfn))
#define ECORE_PPFID_BY_PFID(_p_hwfn) \
	(ECORE_IS_BB((_p_hwfn)->p_dev) ? \
	 (_p_hwfn)->rel_pf_id : \
	 (_p_hwfn)->rel_pf_id / (_p_hwfn)->p_dev->num_ports_in_engine)

enum _ecore_status_t ecore_all_ppfids_wr(struct ecore_hwfn *p_hwfn,
					 struct ecore_ptt *p_ptt, u32 addr,
					 u32 val);

/* Utility functions for dumping the content of the NIG LLH filters */
enum _ecore_status_t ecore_llh_dump_ppfid(struct ecore_dev *p_dev, u8 ppfid);
enum _ecore_status_t ecore_llh_dump_all(struct ecore_dev *p_dev);

#endif /* __ECORE_H */
