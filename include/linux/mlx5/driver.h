/*
 * Copyright (c) 2013-2015, Mellanox Technologies. All rights reserved.
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

#ifndef MLX5_DRIVER_H
#define MLX5_DRIVER_H

#include <linux/kernel.h>
#include <linux/completion.h>
#include <linux/pci.h>
#include <linux/irq.h>
#include <linux/spinlock_types.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/xarray.h>
#include <linux/workqueue.h>
#include <linux/mempool.h>
#include <linux/interrupt.h>
#include <linux/idr.h>
#include <linux/notifier.h>
#include <linux/refcount.h>

#include <linux/mlx5/device.h>
#include <linux/mlx5/doorbell.h>
#include <linux/mlx5/eq.h>
#include <linux/timecounter.h>
#include <linux/ptp_clock_kernel.h>
#include <net/devlink.h>

enum {
	MLX5_BOARD_ID_LEN = 64,
};

enum {
	/* one minute for the sake of bringup. Generally, commands must always
	 * complete and we may need to increase this timeout value
	 */
	MLX5_CMD_TIMEOUT_MSEC	= 60 * 1000,
	MLX5_CMD_WQ_MAX_NAME	= 32,
};

enum {
	CMD_OWNER_SW		= 0x0,
	CMD_OWNER_HW		= 0x1,
	CMD_STATUS_SUCCESS	= 0,
};

enum mlx5_sqp_t {
	MLX5_SQP_SMI		= 0,
	MLX5_SQP_GSI		= 1,
	MLX5_SQP_IEEE_1588	= 2,
	MLX5_SQP_SNIFFER	= 3,
	MLX5_SQP_SYNC_UMR	= 4,
};

enum {
	MLX5_MAX_PORTS	= 2,
};

enum {
	MLX5_ATOMIC_MODE_OFFSET = 16,
	MLX5_ATOMIC_MODE_IB_COMP = 1,
	MLX5_ATOMIC_MODE_CX = 2,
	MLX5_ATOMIC_MODE_8B = 3,
	MLX5_ATOMIC_MODE_16B = 4,
	MLX5_ATOMIC_MODE_32B = 5,
	MLX5_ATOMIC_MODE_64B = 6,
	MLX5_ATOMIC_MODE_128B = 7,
	MLX5_ATOMIC_MODE_256B = 8,
};

enum {
	MLX5_REG_QPTS            = 0x4002,
	MLX5_REG_QETCR		 = 0x4005,
	MLX5_REG_QTCT		 = 0x400a,
	MLX5_REG_QPDPM           = 0x4013,
	MLX5_REG_QCAM            = 0x4019,
	MLX5_REG_DCBX_PARAM      = 0x4020,
	MLX5_REG_DCBX_APP        = 0x4021,
	MLX5_REG_FPGA_CAP	 = 0x4022,
	MLX5_REG_FPGA_CTRL	 = 0x4023,
	MLX5_REG_FPGA_ACCESS_REG = 0x4024,
	MLX5_REG_CORE_DUMP	 = 0x402e,
	MLX5_REG_PCAP		 = 0x5001,
	MLX5_REG_PMTU		 = 0x5003,
	MLX5_REG_PTYS		 = 0x5004,
	MLX5_REG_PAOS		 = 0x5006,
	MLX5_REG_PFCC            = 0x5007,
	MLX5_REG_PPCNT		 = 0x5008,
	MLX5_REG_PPTB            = 0x500b,
	MLX5_REG_PBMC            = 0x500c,
	MLX5_REG_PMAOS		 = 0x5012,
	MLX5_REG_PUDE		 = 0x5009,
	MLX5_REG_PMPE		 = 0x5010,
	MLX5_REG_PELC		 = 0x500e,
	MLX5_REG_PVLC		 = 0x500f,
	MLX5_REG_PCMR		 = 0x5041,
	MLX5_REG_PMLP		 = 0x5002,
	MLX5_REG_PPLM		 = 0x5023,
	MLX5_REG_PCAM		 = 0x507f,
	MLX5_REG_NODE_DESC	 = 0x6001,
	MLX5_REG_HOST_ENDIANNESS = 0x7004,
	MLX5_REG_MCIA		 = 0x9014,
	MLX5_REG_MLCR		 = 0x902b,
	MLX5_REG_MTRC_CAP	 = 0x9040,
	MLX5_REG_MTRC_CONF	 = 0x9041,
	MLX5_REG_MTRC_STDB	 = 0x9042,
	MLX5_REG_MTRC_CTRL	 = 0x9043,
	MLX5_REG_MPEIN		 = 0x9050,
	MLX5_REG_MPCNT		 = 0x9051,
	MLX5_REG_MTPPS		 = 0x9053,
	MLX5_REG_MTPPSE		 = 0x9054,
	MLX5_REG_MPEGC		 = 0x9056,
	MLX5_REG_MCQS		 = 0x9060,
	MLX5_REG_MCQI		 = 0x9061,
	MLX5_REG_MCC		 = 0x9062,
	MLX5_REG_MCDA		 = 0x9063,
	MLX5_REG_MCAM		 = 0x907f,
};

enum mlx5_qpts_trust_state {
	MLX5_QPTS_TRUST_PCP  = 1,
	MLX5_QPTS_TRUST_DSCP = 2,
};

enum mlx5_dcbx_oper_mode {
	MLX5E_DCBX_PARAM_VER_OPER_HOST  = 0x0,
	MLX5E_DCBX_PARAM_VER_OPER_AUTO  = 0x3,
};

enum {
	MLX5_ATOMIC_OPS_CMP_SWAP	= 1 << 0,
	MLX5_ATOMIC_OPS_FETCH_ADD	= 1 << 1,
	MLX5_ATOMIC_OPS_EXTENDED_CMP_SWAP = 1 << 2,
	MLX5_ATOMIC_OPS_EXTENDED_FETCH_ADD = 1 << 3,
};

enum mlx5_page_fault_resume_flags {
	MLX5_PAGE_FAULT_RESUME_REQUESTOR = 1 << 0,
	MLX5_PAGE_FAULT_RESUME_WRITE	 = 1 << 1,
	MLX5_PAGE_FAULT_RESUME_RDMA	 = 1 << 2,
	MLX5_PAGE_FAULT_RESUME_ERROR	 = 1 << 7,
};

enum dbg_rsc_type {
	MLX5_DBG_RSC_QP,
	MLX5_DBG_RSC_EQ,
	MLX5_DBG_RSC_CQ,
};

enum port_state_policy {
	MLX5_POLICY_DOWN	= 0,
	MLX5_POLICY_UP		= 1,
	MLX5_POLICY_FOLLOW	= 2,
	MLX5_POLICY_INVALID	= 0xffffffff
};

enum mlx5_coredev_type {
	MLX5_COREDEV_PF,
	MLX5_COREDEV_VF
};

struct mlx5_field_desc {
	int			i;
};

struct mlx5_rsc_debug {
	struct mlx5_core_dev   *dev;
	void		       *object;
	enum dbg_rsc_type	type;
	struct dentry	       *root;
	struct mlx5_field_desc	fields[0];
};

enum mlx5_dev_event {
	MLX5_DEV_EVENT_SYS_ERROR = 128, /* 0 - 127 are FW events */
	MLX5_DEV_EVENT_PORT_AFFINITY = 129,
};

enum mlx5_port_status {
	MLX5_PORT_UP        = 1,
	MLX5_PORT_DOWN      = 2,
};

struct mlx5_bfreg_info {
	u32		       *sys_pages;
	int			num_low_latency_bfregs;
	unsigned int	       *count;

	/*
	 * protect bfreg allocation data structs
	 */
	struct mutex		lock;
	u32			ver;
	bool			lib_uar_4k;
	u32			num_sys_pages;
	u32			num_static_sys_pages;
	u32			total_num_bfregs;
	u32			num_dyn_bfregs;
};

struct mlx5_cmd_first {
	__be32		data[4];
};

struct mlx5_cmd_msg {
	struct list_head		list;
	struct cmd_msg_cache	       *parent;
	u32				len;
	struct mlx5_cmd_first		first;
	struct mlx5_cmd_mailbox	       *next;
};

struct mlx5_cmd_debug {
	struct dentry	       *dbg_root;
	void		       *in_msg;
	void		       *out_msg;
	u8			status;
	u16			inlen;
	u16			outlen;
};

struct cmd_msg_cache {
	/* protect block chain allocations
	 */
	spinlock_t		lock;
	struct list_head	head;
	unsigned int		max_inbox_size;
	unsigned int		num_ent;
};

enum {
	MLX5_NUM_COMMAND_CACHES = 5,
};

struct mlx5_cmd_stats {
	u64		sum;
	u64		n;
	struct dentry  *root;
	/* protect command average calculations */
	spinlock_t	lock;
};

struct mlx5_cmd {
	struct mlx5_nb    nb;

	void	       *cmd_alloc_buf;
	dma_addr_t	alloc_dma;
	int		alloc_size;
	void	       *cmd_buf;
	dma_addr_t	dma;
	u16		cmdif_rev;
	u8		log_sz;
	u8		log_stride;
	int		max_reg_cmds;
	int		events;
	u32 __iomem    *vector;

	/* protect command queue allocations
	 */
	spinlock_t	alloc_lock;

	/* protect token allocations
	 */
	spinlock_t	token_lock;
	u8		token;
	unsigned long	bitmask;
	char		wq_name[MLX5_CMD_WQ_MAX_NAME];
	struct workqueue_struct *wq;
	struct semaphore sem;
	struct semaphore pages_sem;
	int	mode;
	struct mlx5_cmd_work_ent *ent_arr[MLX5_MAX_COMMANDS];
	struct dma_pool *pool;
	struct mlx5_cmd_debug dbg;
	struct cmd_msg_cache cache[MLX5_NUM_COMMAND_CACHES];
	int checksum_disabled;
	struct mlx5_cmd_stats stats[MLX5_CMD_OP_MAX];
};

struct mlx5_port_caps {
	int	gid_table_len;
	int	pkey_table_len;
	u8	ext_port_cap;
	bool	has_smi;
};

struct mlx5_cmd_mailbox {
	void	       *buf;
	dma_addr_t	dma;
	struct mlx5_cmd_mailbox *next;
};

struct mlx5_buf_list {
	void		       *buf;
	dma_addr_t		map;
};

struct mlx5_frag_buf {
	struct mlx5_buf_list	*frags;
	int			npages;
	int			size;
	u8			page_shift;
};

struct mlx5_frag_buf_ctrl {
	struct mlx5_buf_list   *frags;
	u32			sz_m1;
	u16			frag_sz_m1;
	u16			strides_offset;
	u8			log_sz;
	u8			log_stride;
	u8			log_frag_strides;
};

struct mlx5_core_psv {
	u32	psv_idx;
	struct psv_layout {
		u32	pd;
		u16	syndrome;
		u16	reserved;
		u16	bg;
		u16	app_tag;
		u32	ref_tag;
	} psv;
};

struct mlx5_core_sig_ctx {
	struct mlx5_core_psv	psv_memory;
	struct mlx5_core_psv	psv_wire;
	struct ib_sig_err       err_item;
	bool			sig_status_checked;
	bool			sig_err_exists;
	u32			sigerr_count;
};

enum {
	MLX5_MKEY_MR = 1,
	MLX5_MKEY_MW,
	MLX5_MKEY_INDIRECT_DEVX,
};

struct mlx5_core_mkey {
	u64			iova;
	u64			size;
	u32			key;
	u32			pd;
	u32			type;
};

#define MLX5_24BIT_MASK		((1 << 24) - 1)

enum mlx5_res_type {
	MLX5_RES_QP	= MLX5_EVENT_QUEUE_TYPE_QP,
	MLX5_RES_RQ	= MLX5_EVENT_QUEUE_TYPE_RQ,
	MLX5_RES_SQ	= MLX5_EVENT_QUEUE_TYPE_SQ,
	MLX5_RES_SRQ	= 3,
	MLX5_RES_XSRQ	= 4,
	MLX5_RES_XRQ	= 5,
	MLX5_RES_DCT	= MLX5_EVENT_QUEUE_TYPE_DCT,
};

struct mlx5_core_rsc_common {
	enum mlx5_res_type	res;
	refcount_t		refcount;
	struct completion	free;
};

struct mlx5_uars_page {
	void __iomem	       *map;
	bool			wc;
	u32			index;
	struct list_head	list;
	unsigned int		bfregs;
	unsigned long	       *reg_bitmap; /* for non fast path bf regs */
	unsigned long	       *fp_bitmap;
	unsigned int		reg_avail;
	unsigned int		fp_avail;
	struct kref		ref_count;
	struct mlx5_core_dev   *mdev;
};

struct mlx5_bfreg_head {
	/* protect blue flame registers allocations */
	struct mutex		lock;
	struct list_head	list;
};

struct mlx5_bfreg_data {
	struct mlx5_bfreg_head	reg_head;
	struct mlx5_bfreg_head	wc_head;
};

struct mlx5_sq_bfreg {
	void __iomem	       *map;
	struct mlx5_uars_page  *up;
	bool			wc;
	u32			index;
	unsigned int		offset;
};

struct mlx5_core_health {
	struct health_buffer __iomem   *health;
	__be32 __iomem		       *health_counter;
	struct timer_list		timer;
	u32				prev;
	int				miss_counter;
	u8				synd;
	u32				fatal_error;
	u32				crdump_size;
	/* wq spinlock to synchronize draining */
	spinlock_t			wq_lock;
	struct workqueue_struct	       *wq;
	unsigned long			flags;
	struct work_struct		fatal_report_work;
	struct work_struct		report_work;
	struct delayed_work		recover_work;
	struct devlink_health_reporter *fw_reporter;
	struct devlink_health_reporter *fw_fatal_reporter;
};

struct mlx5_qp_table {
	struct notifier_block   nb;

	/* protect radix tree
	 */
	spinlock_t		lock;
	struct radix_tree_root	tree;
};

struct mlx5_vf_context {
	int	enabled;
	u64	port_guid;
	u64	node_guid;
	/* Valid bits are used to validate administrative guid only.
	 * Enabled after ndo_set_vf_guid
	 */
	u8	port_guid_valid:1;
	u8	node_guid_valid:1;
	enum port_state_policy	policy;
};

struct mlx5_core_sriov {
	struct mlx5_vf_context	*vfs_ctx;
	int			num_vfs;
	u16			max_vfs;
};

struct mlx5_fc_pool {
	struct mlx5_core_dev *dev;
	struct mutex pool_lock; /* protects pool lists */
	struct list_head fully_used;
	struct list_head partially_used;
	struct list_head unused;
	int available_fcs;
	int used_fcs;
	int threshold;
};

struct mlx5_fc_stats {
	spinlock_t counters_idr_lock; /* protects counters_idr */
	struct idr counters_idr;
	struct list_head counters;
	struct llist_head addlist;
	struct llist_head dellist;

	struct workqueue_struct *wq;
	struct delayed_work work;
	unsigned long next_query;
	unsigned long sampling_interval; /* jiffies */
	u32 *bulk_query_out;
	struct mlx5_fc_pool fc_pool;
};

struct mlx5_events;
struct mlx5_mpfs;
struct mlx5_eswitch;
struct mlx5_lag;
struct mlx5_devcom;
struct mlx5_eq_table;
struct mlx5_irq_table;

struct mlx5_rate_limit {
	u32			rate;
	u32			max_burst_sz;
	u16			typical_pkt_sz;
};

struct mlx5_rl_entry {
	struct mlx5_rate_limit	rl;
	u16                     index;
	u16                     refcount;
};

struct mlx5_rl_table {
	/* protect rate limit table */
	struct mutex            rl_lock;
	u16                     max_size;
	u32                     max_rate;
	u32                     min_rate;
	struct mlx5_rl_entry   *rl_entry;
};

struct mlx5_core_roce {
	struct mlx5_flow_table *ft;
	struct mlx5_flow_group *fg;
	struct mlx5_flow_handle *allow_rule;
};

struct mlx5_priv {
	/* IRQ table valid only for real pci devices PF or VF */
	struct mlx5_irq_table   *irq_table;
	struct mlx5_eq_table	*eq_table;

	/* pages stuff */
	struct mlx5_nb          pg_nb;
	struct workqueue_struct *pg_wq;
	struct rb_root		page_root;
	int			fw_pages;
	atomic_t		reg_pages;
	struct list_head	free_list;
	int			vfs_pages;
	int			peer_pf_pages;

	struct mlx5_core_health health;

	/* start: qp staff */
	struct mlx5_qp_table	qp_table;
	struct dentry	       *qp_debugfs;
	struct dentry	       *eq_debugfs;
	struct dentry	       *cq_debugfs;
	struct dentry	       *cmdif_debugfs;
	/* end: qp staff */

	/* start: alloc staff */
	/* protect buffer alocation according to numa node */
	struct mutex            alloc_mutex;
	int                     numa_node;

	struct mutex            pgdir_mutex;
	struct list_head        pgdir_list;
	/* end: alloc staff */
	struct dentry	       *dbg_root;

	/* protect mkey key part */
	spinlock_t		mkey_lock;
	u8			mkey_key;

	struct list_head        dev_list;
	struct list_head        ctx_list;
	spinlock_t              ctx_lock;
	struct mlx5_events      *events;

	struct mlx5_flow_steering *steering;
	struct mlx5_mpfs        *mpfs;
	struct mlx5_eswitch     *eswitch;
	struct mlx5_core_sriov	sriov;
	struct mlx5_lag		*lag;
	struct mlx5_devcom	*devcom;
	struct mlx5_core_roce	roce;
	struct mlx5_fc_stats		fc_stats;
	struct mlx5_rl_table            rl_table;

	struct mlx5_bfreg_data		bfregs;
	struct mlx5_uars_page	       *uar;
};

enum mlx5_device_state {
	MLX5_DEVICE_STATE_UNINITIALIZED,
	MLX5_DEVICE_STATE_UP,
	MLX5_DEVICE_STATE_INTERNAL_ERROR,
};

enum mlx5_interface_state {
	MLX5_INTERFACE_STATE_UP = BIT(0),
};

enum mlx5_pci_status {
	MLX5_PCI_STATUS_DISABLED,
	MLX5_PCI_STATUS_ENABLED,
};

enum mlx5_pagefault_type_flags {
	MLX5_PFAULT_REQUESTOR = 1 << 0,
	MLX5_PFAULT_WRITE     = 1 << 1,
	MLX5_PFAULT_RDMA      = 1 << 2,
};

struct mlx5_td {
	/* protects tirs list changes while tirs refresh */
	struct mutex     list_lock;
	struct list_head tirs_list;
	u32              tdn;
};

struct mlx5e_resources {
	u32                        pdn;
	struct mlx5_td             td;
	struct mlx5_core_mkey      mkey;
	struct mlx5_sq_bfreg       bfreg;
};

enum mlx5_sw_icm_type {
	MLX5_SW_ICM_TYPE_STEERING,
	MLX5_SW_ICM_TYPE_HEADER_MODIFY,
};

#define MLX5_MAX_RESERVED_GIDS 8

struct mlx5_rsvd_gids {
	unsigned int start;
	unsigned int count;
	struct ida ida;
};

#define MAX_PIN_NUM	8
struct mlx5_pps {
	u8                         pin_caps[MAX_PIN_NUM];
	struct work_struct         out_work;
	u64                        start[MAX_PIN_NUM];
	u8                         enabled;
};

struct mlx5_clock {
	struct mlx5_core_dev      *mdev;
	struct mlx5_nb             pps_nb;
	seqlock_t                  lock;
	struct cyclecounter        cycles;
	struct timecounter         tc;
	struct hwtstamp_config     hwtstamp_config;
	u32                        nominal_c_mult;
	unsigned long              overflow_period;
	struct delayed_work        overflow_work;
	struct ptp_clock          *ptp;
	struct ptp_clock_info      ptp_info;
	struct mlx5_pps            pps_info;
};

struct mlx5_dm;
struct mlx5_fw_tracer;
struct mlx5_vxlan;
struct mlx5_geneve;
struct mlx5_hv_vhca;

#define MLX5_LOG_SW_ICM_BLOCK_SIZE(dev) (MLX5_CAP_DEV_MEM(dev, log_sw_icm_alloc_granularity))
#define MLX5_SW_ICM_BLOCK_SIZE(dev) (1 << MLX5_LOG_SW_ICM_BLOCK_SIZE(dev))

struct mlx5_core_dev {
	struct device *device;
	enum mlx5_coredev_type coredev_type;
	struct pci_dev	       *pdev;
	/* sync pci state */
	struct mutex		pci_status_mutex;
	enum mlx5_pci_status	pci_status;
	u8			rev_id;
	char			board_id[MLX5_BOARD_ID_LEN];
	struct mlx5_cmd		cmd;
	struct mlx5_port_caps	port_caps[MLX5_MAX_PORTS];
	struct {
		u32 hca_cur[MLX5_CAP_NUM][MLX5_UN_SZ_DW(hca_cap_union)];
		u32 hca_max[MLX5_CAP_NUM][MLX5_UN_SZ_DW(hca_cap_union)];
		u32 pcam[MLX5_ST_SZ_DW(pcam_reg)];
		u32 mcam[MLX5_ST_SZ_DW(mcam_reg)];
		u32 fpga[MLX5_ST_SZ_DW(fpga_cap)];
		u32 qcam[MLX5_ST_SZ_DW(qcam_reg)];
		u8  embedded_cpu;
	} caps;
	u64			sys_image_guid;
	phys_addr_t		iseg_base;
	struct mlx5_init_seg __iomem *iseg;
	phys_addr_t             bar_addr;
	enum mlx5_device_state	state;
	/* sync interface state */
	struct mutex		intf_state_mutex;
	unsigned long		intf_state;
	struct mlx5_priv	priv;
	struct mlx5_profile	*profile;
	atomic_t		num_qps;
	u32			issi;
	struct mlx5e_resources  mlx5e_res;
	struct mlx5_dm          *dm;
	struct mlx5_vxlan       *vxlan;
	struct mlx5_geneve      *geneve;
	struct {
		struct mlx5_rsvd_gids	reserved_gids;
		u32			roce_en;
	} roce;
#ifdef CONFIG_MLX5_FPGA
	struct mlx5_fpga_device *fpga;
#endif
	struct mlx5_clock        clock;
	struct mlx5_ib_clock_info  *clock_info;
	struct mlx5_fw_tracer   *tracer;
	u32                      vsc_addr;
	struct mlx5_hv_vhca	*hv_vhca;
};

struct mlx5_db {
	__be32			*db;
	union {
		struct mlx5_db_pgdir		*pgdir;
		struct mlx5_ib_user_db_page	*user_page;
	}			u;
	dma_addr_t		dma;
	int			index;
};

enum {
	MLX5_COMP_EQ_SIZE = 1024,
};

enum {
	MLX5_PTYS_IB = 1 << 0,
	MLX5_PTYS_EN = 1 << 2,
};

typedef void (*mlx5_cmd_cbk_t)(int status, void *context);

enum {
	MLX5_CMD_ENT_STATE_PENDING_COMP,
};

struct mlx5_cmd_work_ent {
	unsigned long		state;
	struct mlx5_cmd_msg    *in;
	struct mlx5_cmd_msg    *out;
	void		       *uout;
	int			uout_size;
	mlx5_cmd_cbk_t		callback;
	struct delayed_work	cb_timeout_work;
	void		       *context;
	int			idx;
	struct completion	done;
	struct mlx5_cmd        *cmd;
	struct work_struct	work;
	struct mlx5_cmd_layout *lay;
	int			ret;
	int			page_queue;
	u8			status;
	u8			token;
	u64			ts1;
	u64			ts2;
	u16			op;
	bool			polling;
};

struct mlx5_pas {
	u64	pa;
	u8	log_sz;
};

enum phy_port_state {
	MLX5_AAA_111
};

struct mlx5_hca_vport_context {
	u32			field_select;
	bool			sm_virt_aware;
	bool			has_smi;
	bool			has_raw;
	enum port_state_policy	policy;
	enum phy_port_state	phys_state;
	enum ib_port_state	vport_state;
	u8			port_physical_state;
	u64			sys_image_guid;
	u64			port_guid;
	u64			node_guid;
	u32			cap_mask1;
	u32			cap_mask1_perm;
	u16			cap_mask2;
	u16			cap_mask2_perm;
	u16			lid;
	u8			init_type_reply; /* bitmask: see ib spec 14.2.5.6 InitTypeReply */
	u8			lmc;
	u8			subnet_timeout;
	u16			sm_lid;
	u8			sm_sl;
	u16			qkey_violation_counter;
	u16			pkey_violation_counter;
	bool			grh_required;
};

static inline void *mlx5_buf_offset(struct mlx5_frag_buf *buf, int offset)
{
		return buf->frags->buf + offset;
}

#define STRUCT_FIELD(header, field) \
	.struct_offset_bytes = offsetof(struct ib_unpacked_ ## header, field),      \
	.struct_size_bytes   = sizeof((struct ib_unpacked_ ## header *)0)->field

static inline struct mlx5_core_dev *pci2mlx5_core_dev(struct pci_dev *pdev)
{
	return pci_get_drvdata(pdev);
}

extern struct dentry *mlx5_debugfs_root;

static inline u16 fw_rev_maj(struct mlx5_core_dev *dev)
{
	return ioread32be(&dev->iseg->fw_rev) & 0xffff;
}

static inline u16 fw_rev_min(struct mlx5_core_dev *dev)
{
	return ioread32be(&dev->iseg->fw_rev) >> 16;
}

static inline u16 fw_rev_sub(struct mlx5_core_dev *dev)
{
	return ioread32be(&dev->iseg->cmdif_rev_fw_sub) & 0xffff;
}

static inline u16 cmdif_rev(struct mlx5_core_dev *dev)
{
	return ioread32be(&dev->iseg->cmdif_rev_fw_sub) >> 16;
}

static inline u32 mlx5_base_mkey(const u32 key)
{
	return key & 0xffffff00u;
}

static inline void mlx5_init_fbc_offset(struct mlx5_buf_list *frags,
					u8 log_stride, u8 log_sz,
					u16 strides_offset,
					struct mlx5_frag_buf_ctrl *fbc)
{
	fbc->frags      = frags;
	fbc->log_stride = log_stride;
	fbc->log_sz     = log_sz;
	fbc->sz_m1	= (1 << fbc->log_sz) - 1;
	fbc->log_frag_strides = PAGE_SHIFT - fbc->log_stride;
	fbc->frag_sz_m1	= (1 << fbc->log_frag_strides) - 1;
	fbc->strides_offset = strides_offset;
}

static inline void mlx5_init_fbc(struct mlx5_buf_list *frags,
				 u8 log_stride, u8 log_sz,
				 struct mlx5_frag_buf_ctrl *fbc)
{
	mlx5_init_fbc_offset(frags, log_stride, log_sz, 0, fbc);
}

static inline void *mlx5_frag_buf_get_wqe(struct mlx5_frag_buf_ctrl *fbc,
					  u32 ix)
{
	unsigned int frag;

	ix  += fbc->strides_offset;
	frag = ix >> fbc->log_frag_strides;

	return fbc->frags[frag].buf + ((fbc->frag_sz_m1 & ix) << fbc->log_stride);
}

static inline u32
mlx5_frag_buf_get_idx_last_contig_stride(struct mlx5_frag_buf_ctrl *fbc, u32 ix)
{
	u32 last_frag_stride_idx = (ix + fbc->strides_offset) | fbc->frag_sz_m1;

	return min_t(u32, last_frag_stride_idx - fbc->strides_offset, fbc->sz_m1);
}

int mlx5_cmd_init(struct mlx5_core_dev *dev);
void mlx5_cmd_cleanup(struct mlx5_core_dev *dev);
void mlx5_cmd_use_events(struct mlx5_core_dev *dev);
void mlx5_cmd_use_polling(struct mlx5_core_dev *dev);

struct mlx5_async_ctx {
	struct mlx5_core_dev *dev;
	atomic_t num_inflight;
	struct wait_queue_head wait;
};

struct mlx5_async_work;

typedef void (*mlx5_async_cbk_t)(int status, struct mlx5_async_work *context);

struct mlx5_async_work {
	struct mlx5_async_ctx *ctx;
	mlx5_async_cbk_t user_callback;
};

void mlx5_cmd_init_async_ctx(struct mlx5_core_dev *dev,
			     struct mlx5_async_ctx *ctx);
void mlx5_cmd_cleanup_async_ctx(struct mlx5_async_ctx *ctx);
int mlx5_cmd_exec_cb(struct mlx5_async_ctx *ctx, void *in, int in_size,
		     void *out, int out_size, mlx5_async_cbk_t callback,
		     struct mlx5_async_work *work);

int mlx5_cmd_exec(struct mlx5_core_dev *dev, void *in, int in_size, void *out,
		  int out_size);
int mlx5_cmd_exec_polling(struct mlx5_core_dev *dev, void *in, int in_size,
			  void *out, int out_size);
void mlx5_cmd_mbox_status(void *out, u8 *status, u32 *syndrome);

int mlx5_core_get_caps(struct mlx5_core_dev *dev, enum mlx5_cap_type cap_type);
int mlx5_cmd_alloc_uar(struct mlx5_core_dev *dev, u32 *uarn);
int mlx5_cmd_free_uar(struct mlx5_core_dev *dev, u32 uarn);
void mlx5_health_flush(struct mlx5_core_dev *dev);
void mlx5_health_cleanup(struct mlx5_core_dev *dev);
int mlx5_health_init(struct mlx5_core_dev *dev);
void mlx5_start_health_poll(struct mlx5_core_dev *dev);
void mlx5_stop_health_poll(struct mlx5_core_dev *dev, bool disable_health);
void mlx5_drain_health_wq(struct mlx5_core_dev *dev);
void mlx5_trigger_health_work(struct mlx5_core_dev *dev);
int mlx5_buf_alloc_node(struct mlx5_core_dev *dev, int size,
			struct mlx5_frag_buf *buf, int node);
int mlx5_buf_alloc(struct mlx5_core_dev *dev,
		   int size, struct mlx5_frag_buf *buf);
void mlx5_buf_free(struct mlx5_core_dev *dev, struct mlx5_frag_buf *buf);
int mlx5_frag_buf_alloc_node(struct mlx5_core_dev *dev, int size,
			     struct mlx5_frag_buf *buf, int node);
void mlx5_frag_buf_free(struct mlx5_core_dev *dev, struct mlx5_frag_buf *buf);
struct mlx5_cmd_mailbox *mlx5_alloc_cmd_mailbox_chain(struct mlx5_core_dev *dev,
						      gfp_t flags, int npages);
void mlx5_free_cmd_mailbox_chain(struct mlx5_core_dev *dev,
				 struct mlx5_cmd_mailbox *head);
int mlx5_core_create_mkey_cb(struct mlx5_core_dev *dev,
			     struct mlx5_core_mkey *mkey,
			     struct mlx5_async_ctx *async_ctx, u32 *in,
			     int inlen, u32 *out, int outlen,
			     mlx5_async_cbk_t callback,
			     struct mlx5_async_work *context);
int mlx5_core_create_mkey(struct mlx5_core_dev *dev,
			  struct mlx5_core_mkey *mkey,
			  u32 *in, int inlen);
int mlx5_core_destroy_mkey(struct mlx5_core_dev *dev,
			   struct mlx5_core_mkey *mkey);
int mlx5_core_query_mkey(struct mlx5_core_dev *dev, struct mlx5_core_mkey *mkey,
			 u32 *out, int outlen);
int mlx5_core_alloc_pd(struct mlx5_core_dev *dev, u32 *pdn);
int mlx5_core_dealloc_pd(struct mlx5_core_dev *dev, u32 pdn);
int mlx5_pagealloc_init(struct mlx5_core_dev *dev);
void mlx5_pagealloc_cleanup(struct mlx5_core_dev *dev);
void mlx5_pagealloc_start(struct mlx5_core_dev *dev);
void mlx5_pagealloc_stop(struct mlx5_core_dev *dev);
void mlx5_core_req_pages_handler(struct mlx5_core_dev *dev, u16 func_id,
				 s32 npages, bool ec_function);
int mlx5_satisfy_startup_pages(struct mlx5_core_dev *dev, int boot);
int mlx5_reclaim_startup_pages(struct mlx5_core_dev *dev);
void mlx5_register_debugfs(void);
void mlx5_unregister_debugfs(void);

void mlx5_fill_page_array(struct mlx5_frag_buf *buf, __be64 *pas);
void mlx5_fill_page_frag_array(struct mlx5_frag_buf *frag_buf, __be64 *pas);
int mlx5_vector2eqn(struct mlx5_core_dev *dev, int vector, int *eqn,
		    unsigned int *irqn);
int mlx5_core_attach_mcg(struct mlx5_core_dev *dev, union ib_gid *mgid, u32 qpn);
int mlx5_core_detach_mcg(struct mlx5_core_dev *dev, union ib_gid *mgid, u32 qpn);

void mlx5_qp_debugfs_init(struct mlx5_core_dev *dev);
void mlx5_qp_debugfs_cleanup(struct mlx5_core_dev *dev);
int mlx5_core_access_reg(struct mlx5_core_dev *dev, void *data_in,
			 int size_in, void *data_out, int size_out,
			 u16 reg_num, int arg, int write);

int mlx5_db_alloc(struct mlx5_core_dev *dev, struct mlx5_db *db);
int mlx5_db_alloc_node(struct mlx5_core_dev *dev, struct mlx5_db *db,
		       int node);
void mlx5_db_free(struct mlx5_core_dev *dev, struct mlx5_db *db);

const char *mlx5_command_str(int command);
void mlx5_cmdif_debugfs_init(struct mlx5_core_dev *dev);
void mlx5_cmdif_debugfs_cleanup(struct mlx5_core_dev *dev);
int mlx5_core_create_psv(struct mlx5_core_dev *dev, u32 pdn,
			 int npsvs, u32 *sig_index);
int mlx5_core_destroy_psv(struct mlx5_core_dev *dev, int psv_num);
void mlx5_core_put_rsc(struct mlx5_core_rsc_common *common);
int mlx5_query_odp_caps(struct mlx5_core_dev *dev,
			struct mlx5_odp_caps *odp_caps);
int mlx5_core_query_ib_ppcnt(struct mlx5_core_dev *dev,
			     u8 port_num, void *out, size_t sz);

int mlx5_init_rl_table(struct mlx5_core_dev *dev);
void mlx5_cleanup_rl_table(struct mlx5_core_dev *dev);
int mlx5_rl_add_rate(struct mlx5_core_dev *dev, u16 *index,
		     struct mlx5_rate_limit *rl);
void mlx5_rl_remove_rate(struct mlx5_core_dev *dev, struct mlx5_rate_limit *rl);
bool mlx5_rl_is_in_range(struct mlx5_core_dev *dev, u32 rate);
bool mlx5_rl_are_equal(struct mlx5_rate_limit *rl_0,
		       struct mlx5_rate_limit *rl_1);
int mlx5_alloc_bfreg(struct mlx5_core_dev *mdev, struct mlx5_sq_bfreg *bfreg,
		     bool map_wc, bool fast_path);
void mlx5_free_bfreg(struct mlx5_core_dev *mdev, struct mlx5_sq_bfreg *bfreg);

unsigned int mlx5_comp_vectors_count(struct mlx5_core_dev *dev);
struct cpumask *
mlx5_comp_irq_get_affinity_mask(struct mlx5_core_dev *dev, int vector);
unsigned int mlx5_core_reserved_gids_count(struct mlx5_core_dev *dev);
int mlx5_core_roce_gid_set(struct mlx5_core_dev *dev, unsigned int index,
			   u8 roce_version, u8 roce_l3_type, const u8 *gid,
			   const u8 *mac, bool vlan, u16 vlan_id, u8 port_num);

static inline int fw_initializing(struct mlx5_core_dev *dev)
{
	return ioread32be(&dev->iseg->initializing) >> 31;
}

static inline u32 mlx5_mkey_to_idx(u32 mkey)
{
	return mkey >> 8;
}

static inline u32 mlx5_idx_to_mkey(u32 mkey_idx)
{
	return mkey_idx << 8;
}

static inline u8 mlx5_mkey_variant(u32 mkey)
{
	return mkey & 0xff;
}

enum {
	MLX5_PROF_MASK_QP_SIZE		= (u64)1 << 0,
	MLX5_PROF_MASK_MR_CACHE		= (u64)1 << 1,
};

enum {
	MR_CACHE_LAST_STD_ENTRY = 20,
	MLX5_IMR_MTT_CACHE_ENTRY,
	MLX5_IMR_KSM_CACHE_ENTRY,
	MAX_MR_CACHE_ENTRIES
};

enum {
	MLX5_INTERFACE_PROTOCOL_IB  = 0,
	MLX5_INTERFACE_PROTOCOL_ETH = 1,
};

struct mlx5_interface {
	void *			(*add)(struct mlx5_core_dev *dev);
	void			(*remove)(struct mlx5_core_dev *dev, void *context);
	int			(*attach)(struct mlx5_core_dev *dev, void *context);
	void			(*detach)(struct mlx5_core_dev *dev, void *context);
	int			protocol;
	struct list_head	list;
};

int mlx5_register_interface(struct mlx5_interface *intf);
void mlx5_unregister_interface(struct mlx5_interface *intf);
int mlx5_notifier_register(struct mlx5_core_dev *dev, struct notifier_block *nb);
int mlx5_notifier_unregister(struct mlx5_core_dev *dev, struct notifier_block *nb);
int mlx5_eq_notifier_register(struct mlx5_core_dev *dev, struct mlx5_nb *nb);
int mlx5_eq_notifier_unregister(struct mlx5_core_dev *dev, struct mlx5_nb *nb);

int mlx5_core_query_vendor_id(struct mlx5_core_dev *mdev, u32 *vendor_id);

int mlx5_cmd_create_vport_lag(struct mlx5_core_dev *dev);
int mlx5_cmd_destroy_vport_lag(struct mlx5_core_dev *dev);
bool mlx5_lag_is_roce(struct mlx5_core_dev *dev);
bool mlx5_lag_is_sriov(struct mlx5_core_dev *dev);
bool mlx5_lag_is_multipath(struct mlx5_core_dev *dev);
bool mlx5_lag_is_active(struct mlx5_core_dev *dev);
struct net_device *mlx5_lag_get_roce_netdev(struct mlx5_core_dev *dev);
int mlx5_lag_query_cong_counters(struct mlx5_core_dev *dev,
				 u64 *values,
				 int num_counters,
				 size_t *offsets);
struct mlx5_uars_page *mlx5_get_uars_page(struct mlx5_core_dev *mdev);
void mlx5_put_uars_page(struct mlx5_core_dev *mdev, struct mlx5_uars_page *up);
int mlx5_dm_sw_icm_alloc(struct mlx5_core_dev *dev, enum mlx5_sw_icm_type type,
			 u64 length, u16 uid, phys_addr_t *addr, u32 *obj_id);
int mlx5_dm_sw_icm_dealloc(struct mlx5_core_dev *dev, enum mlx5_sw_icm_type type,
			   u64 length, u16 uid, phys_addr_t addr, u32 obj_id);

#ifdef CONFIG_MLX5_CORE_IPOIB
struct net_device *mlx5_rdma_netdev_alloc(struct mlx5_core_dev *mdev,
					  struct ib_device *ibdev,
					  const char *name,
					  void (*setup)(struct net_device *));
#endif /* CONFIG_MLX5_CORE_IPOIB */
int mlx5_rdma_rn_get_params(struct mlx5_core_dev *mdev,
			    struct ib_device *device,
			    struct rdma_netdev_alloc_params *params);

struct mlx5_profile {
	u64	mask;
	u8	log_max_qp;
	struct {
		int	size;
		int	limit;
	} mr_cache[MAX_MR_CACHE_ENTRIES];
};

enum {
	MLX5_PCI_DEV_IS_VF		= 1 << 0,
};

static inline bool mlx5_core_is_pf(const struct mlx5_core_dev *dev)
{
	return dev->coredev_type == MLX5_COREDEV_PF;
}

static inline bool mlx5_core_is_vf(const struct mlx5_core_dev *dev)
{
	return dev->coredev_type == MLX5_COREDEV_VF;
}

static inline bool mlx5_core_is_ecpf(struct mlx5_core_dev *dev)
{
	return dev->caps.embedded_cpu;
}

static inline bool
mlx5_core_is_ecpf_esw_manager(const struct mlx5_core_dev *dev)
{
	return dev->caps.embedded_cpu && MLX5_CAP_GEN(dev, eswitch_manager);
}

static inline bool mlx5_ecpf_vport_exists(const struct mlx5_core_dev *dev)
{
	return mlx5_core_is_pf(dev) && MLX5_CAP_ESW(dev, ecpf_vport_exists);
}

static inline u16 mlx5_core_max_vfs(const struct mlx5_core_dev *dev)
{
	return dev->priv.sriov.max_vfs;
}

static inline int mlx5_get_gid_table_len(u16 param)
{
	if (param > 4) {
		pr_warn("gid table length is zero\n");
		return 0;
	}

	return 8 * (1 << param);
}

static inline bool mlx5_rl_is_supported(struct mlx5_core_dev *dev)
{
	return !!(dev->priv.rl_table.max_size);
}

static inline int mlx5_core_is_mp_slave(struct mlx5_core_dev *dev)
{
	return MLX5_CAP_GEN(dev, affiliate_nic_vport_criteria) &&
	       MLX5_CAP_GEN(dev, num_vhca_ports) <= 1;
}

static inline int mlx5_core_is_mp_master(struct mlx5_core_dev *dev)
{
	return MLX5_CAP_GEN(dev, num_vhca_ports) > 1;
}

static inline int mlx5_core_mp_enabled(struct mlx5_core_dev *dev)
{
	return mlx5_core_is_mp_slave(dev) ||
	       mlx5_core_is_mp_master(dev);
}

static inline int mlx5_core_native_port_num(struct mlx5_core_dev *dev)
{
	if (!mlx5_core_mp_enabled(dev))
		return 1;

	return MLX5_CAP_GEN(dev, native_port_num);
}

enum {
	MLX5_TRIGGERED_CMD_COMP = (u64)1 << 32,
};

static inline bool mlx5_is_roce_enabled(struct mlx5_core_dev *dev)
{
	struct devlink *devlink = priv_to_devlink(dev);
	union devlink_param_value val;

	devlink_param_driverinit_value_get(devlink,
					   DEVLINK_PARAM_GENERIC_ID_ENABLE_ROCE,
					   &val);
	return val.vbool;
}

#endif /* MLX5_DRIVER_H */
