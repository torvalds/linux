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
#include <linux/spinlock_types.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/radix-tree.h>
#include <linux/workqueue.h>
#include <linux/mempool.h>
#include <linux/interrupt.h>
#include <linux/idr.h>

#include <linux/mlx5/device.h>
#include <linux/mlx5/doorbell.h>
#include <linux/mlx5/srq.h>

enum {
	MLX5_BOARD_ID_LEN = 64,
	MLX5_MAX_NAME_LEN = 16,
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
	MLX5_EQ_VEC_PAGES	 = 0,
	MLX5_EQ_VEC_CMD		 = 1,
	MLX5_EQ_VEC_ASYNC	 = 2,
	MLX5_EQ_VEC_PFAULT	 = 3,
	MLX5_EQ_VEC_COMP_BASE,
};

enum {
	MLX5_MAX_IRQ_NAME	= 32
};

enum {
	MLX5_ATOMIC_MODE_IB_COMP	= 1 << 16,
	MLX5_ATOMIC_MODE_CX		= 2 << 16,
	MLX5_ATOMIC_MODE_8B		= 3 << 16,
	MLX5_ATOMIC_MODE_16B		= 4 << 16,
	MLX5_ATOMIC_MODE_32B		= 5 << 16,
	MLX5_ATOMIC_MODE_64B		= 6 << 16,
	MLX5_ATOMIC_MODE_128B		= 7 << 16,
	MLX5_ATOMIC_MODE_256B		= 8 << 16,
};

enum {
	MLX5_REG_QETCR		 = 0x4005,
	MLX5_REG_QTCT		 = 0x400a,
	MLX5_REG_DCBX_PARAM      = 0x4020,
	MLX5_REG_DCBX_APP        = 0x4021,
	MLX5_REG_FPGA_CAP	 = 0x4022,
	MLX5_REG_FPGA_CTRL	 = 0x4023,
	MLX5_REG_FPGA_ACCESS_REG = 0x4024,
	MLX5_REG_PCAP		 = 0x5001,
	MLX5_REG_PMTU		 = 0x5003,
	MLX5_REG_PTYS		 = 0x5004,
	MLX5_REG_PAOS		 = 0x5006,
	MLX5_REG_PFCC            = 0x5007,
	MLX5_REG_PPCNT		 = 0x5008,
	MLX5_REG_PMAOS		 = 0x5012,
	MLX5_REG_PUDE		 = 0x5009,
	MLX5_REG_PMPE		 = 0x5010,
	MLX5_REG_PELC		 = 0x500e,
	MLX5_REG_PVLC		 = 0x500f,
	MLX5_REG_PCMR		 = 0x5041,
	MLX5_REG_PMLP		 = 0x5002,
	MLX5_REG_PCAM		 = 0x507f,
	MLX5_REG_NODE_DESC	 = 0x6001,
	MLX5_REG_HOST_ENDIANNESS = 0x7004,
	MLX5_REG_MCIA		 = 0x9014,
	MLX5_REG_MLCR		 = 0x902b,
	MLX5_REG_MPCNT		 = 0x9051,
	MLX5_REG_MTPPS		 = 0x9053,
	MLX5_REG_MTPPSE		 = 0x9054,
	MLX5_REG_MCQI		 = 0x9061,
	MLX5_REG_MCC		 = 0x9062,
	MLX5_REG_MCDA		 = 0x9063,
	MLX5_REG_MCAM		 = 0x907f,
};

enum mlx5_dcbx_oper_mode {
	MLX5E_DCBX_PARAM_VER_OPER_HOST  = 0x0,
	MLX5E_DCBX_PARAM_VER_OPER_AUTO  = 0x3,
};

enum {
	MLX5_ATOMIC_OPS_CMP_SWAP	= 1 << 0,
	MLX5_ATOMIC_OPS_FETCH_ADD	= 1 << 1,
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

struct mlx5_field_desc {
	struct dentry	       *dent;
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
	MLX5_DEV_EVENT_SYS_ERROR,
	MLX5_DEV_EVENT_PORT_UP,
	MLX5_DEV_EVENT_PORT_DOWN,
	MLX5_DEV_EVENT_PORT_INITIALIZED,
	MLX5_DEV_EVENT_LID_CHANGE,
	MLX5_DEV_EVENT_PKEY_CHANGE,
	MLX5_DEV_EVENT_GUID_CHANGE,
	MLX5_DEV_EVENT_CLIENT_REREG,
	MLX5_DEV_EVENT_PPS,
};

enum mlx5_port_status {
	MLX5_PORT_UP        = 1,
	MLX5_PORT_DOWN      = 2,
};

enum mlx5_eq_type {
	MLX5_EQ_TYPE_COMP,
	MLX5_EQ_TYPE_ASYNC,
#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
	MLX5_EQ_TYPE_PF,
#endif
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
	struct dentry	       *dbg_in;
	struct dentry	       *dbg_out;
	struct dentry	       *dbg_outlen;
	struct dentry	       *dbg_status;
	struct dentry	       *dbg_run;
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
	struct dentry  *avg;
	struct dentry  *count;
	/* protect command average calculations */
	spinlock_t	lock;
};

struct mlx5_cmd {
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
	struct pci_pool *pool;
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

struct mlx5_buf {
	struct mlx5_buf_list	direct;
	int			npages;
	int			size;
	u8			page_shift;
};

struct mlx5_frag_buf {
	struct mlx5_buf_list	*frags;
	int			npages;
	int			size;
	u8			page_shift;
};

struct mlx5_eq_tasklet {
	struct list_head list;
	struct list_head process_list;
	struct tasklet_struct task;
	/* lock on completion tasklet list */
	spinlock_t lock;
};

struct mlx5_eq_pagefault {
	struct work_struct       work;
	/* Pagefaults lock */
	spinlock_t		 lock;
	struct workqueue_struct *wq;
	mempool_t		*pool;
};

struct mlx5_eq {
	struct mlx5_core_dev   *dev;
	__be32 __iomem	       *doorbell;
	u32			cons_index;
	struct mlx5_buf		buf;
	int			size;
	unsigned int		irqn;
	u8			eqn;
	int			nent;
	u64			mask;
	struct list_head	list;
	int			index;
	struct mlx5_rsc_debug	*dbg;
	enum mlx5_eq_type	type;
	union {
		struct mlx5_eq_tasklet   tasklet_ctx;
#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
		struct mlx5_eq_pagefault pf_ctx;
#endif
	};
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
};

struct mlx5_core_rsc_common {
	enum mlx5_res_type	res;
	atomic_t		refcount;
	struct completion	free;
};

struct mlx5_core_srq {
	struct mlx5_core_rsc_common	common; /* must be first */
	u32		srqn;
	int		max;
	int		max_gs;
	int		max_avail_gather;
	int		wqe_shift;
	void (*event)	(struct mlx5_core_srq *, enum mlx5_event);

	atomic_t		refcount;
	struct completion	free;
};

struct mlx5_eq_table {
	void __iomem	       *update_ci;
	void __iomem	       *update_arm_ci;
	struct list_head	comp_eqs_list;
	struct mlx5_eq		pages_eq;
	struct mlx5_eq		async_eq;
	struct mlx5_eq		cmd_eq;
#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
	struct mlx5_eq		pfault_eq;
#endif
	int			num_comp_vectors;
	/* protect EQs list
	 */
	spinlock_t		lock;
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
	bool				sick;
	/* wq spinlock to synchronize draining */
	spinlock_t			wq_lock;
	struct workqueue_struct	       *wq;
	unsigned long			flags;
	struct work_struct		work;
	struct delayed_work		recover_work;
};

struct mlx5_cq_table {
	/* protect radix tree
	 */
	spinlock_t		lock;
	struct radix_tree_root	tree;
};

struct mlx5_qp_table {
	/* protect radix tree
	 */
	spinlock_t		lock;
	struct radix_tree_root	tree;
};

struct mlx5_srq_table {
	/* protect radix tree
	 */
	spinlock_t		lock;
	struct radix_tree_root	tree;
};

struct mlx5_mkey_table {
	/* protect radix tree
	 */
	rwlock_t		lock;
	struct radix_tree_root	tree;
};

struct mlx5_vf_context {
	int	enabled;
};

struct mlx5_core_sriov {
	struct mlx5_vf_context	*vfs_ctx;
	int			num_vfs;
	int			enabled_vfs;
};

struct mlx5_irq_info {
	cpumask_var_t mask;
	char name[MLX5_MAX_IRQ_NAME];
};

struct mlx5_fc_stats {
	struct rb_root counters;
	struct list_head addlist;
	/* protect addlist add/splice operations */
	spinlock_t addlist_lock;

	struct workqueue_struct *wq;
	struct delayed_work work;
	unsigned long next_query;
	unsigned long sampling_interval; /* jiffies */
};

struct mlx5_eswitch;
struct mlx5_lag;
struct mlx5_pagefault;

struct mlx5_rl_entry {
	u32                     rate;
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

enum port_module_event_status_type {
	MLX5_MODULE_STATUS_PLUGGED   = 0x1,
	MLX5_MODULE_STATUS_UNPLUGGED = 0x2,
	MLX5_MODULE_STATUS_ERROR     = 0x3,
	MLX5_MODULE_STATUS_NUM       = 0x3,
};

enum  port_module_event_error_type {
	MLX5_MODULE_EVENT_ERROR_POWER_BUDGET_EXCEEDED,
	MLX5_MODULE_EVENT_ERROR_LONG_RANGE_FOR_NON_MLNX_CABLE_MODULE,
	MLX5_MODULE_EVENT_ERROR_BUS_STUCK,
	MLX5_MODULE_EVENT_ERROR_NO_EEPROM_RETRY_TIMEOUT,
	MLX5_MODULE_EVENT_ERROR_ENFORCE_PART_NUMBER_LIST,
	MLX5_MODULE_EVENT_ERROR_UNKNOWN_IDENTIFIER,
	MLX5_MODULE_EVENT_ERROR_HIGH_TEMPERATURE,
	MLX5_MODULE_EVENT_ERROR_BAD_CABLE,
	MLX5_MODULE_EVENT_ERROR_UNKNOWN,
	MLX5_MODULE_EVENT_ERROR_NUM,
};

struct mlx5_port_module_event_stats {
	u64 status_counters[MLX5_MODULE_STATUS_NUM];
	u64 error_counters[MLX5_MODULE_EVENT_ERROR_NUM];
};

struct mlx5_priv {
	char			name[MLX5_MAX_NAME_LEN];
	struct mlx5_eq_table	eq_table;
	struct msix_entry	*msix_arr;
	struct mlx5_irq_info	*irq_info;

	/* pages stuff */
	struct workqueue_struct *pg_wq;
	struct rb_root		page_root;
	int			fw_pages;
	atomic_t		reg_pages;
	struct list_head	free_list;
	int			vfs_pages;

	struct mlx5_core_health health;

	struct mlx5_srq_table	srq_table;

	/* start: qp staff */
	struct mlx5_qp_table	qp_table;
	struct dentry	       *qp_debugfs;
	struct dentry	       *eq_debugfs;
	struct dentry	       *cq_debugfs;
	struct dentry	       *cmdif_debugfs;
	/* end: qp staff */

	/* start: cq staff */
	struct mlx5_cq_table	cq_table;
	/* end: cq staff */

	/* start: mkey staff */
	struct mlx5_mkey_table	mkey_table;
	/* end: mkey staff */

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

	struct mlx5_flow_steering *steering;
	struct mlx5_eswitch     *eswitch;
	struct mlx5_core_sriov	sriov;
	struct mlx5_lag		*lag;
	unsigned long		pci_dev_data;
	struct mlx5_fc_stats		fc_stats;
	struct mlx5_rl_table            rl_table;

	struct mlx5_port_module_event_stats  pme_stats;

#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
	void		      (*pfault)(struct mlx5_core_dev *dev,
					void *context,
					struct mlx5_pagefault *pfault);
	void		       *pfault_ctx;
	struct srcu_struct      pfault_srcu;
#endif
	struct mlx5_bfreg_data		bfregs;
	struct mlx5_uars_page	       *uar;
};

enum mlx5_device_state {
	MLX5_DEVICE_STATE_UP,
	MLX5_DEVICE_STATE_INTERNAL_ERROR,
};

enum mlx5_interface_state {
	MLX5_INTERFACE_STATE_DOWN = BIT(0),
	MLX5_INTERFACE_STATE_UP = BIT(1),
	MLX5_INTERFACE_STATE_SHUTDOWN = BIT(2),
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

/* Contains the details of a pagefault. */
struct mlx5_pagefault {
	u32			bytes_committed;
	u32			token;
	u8			event_subtype;
	u8			type;
	union {
		/* Initiator or send message responder pagefault details. */
		struct {
			/* Received packet size, only valid for responders. */
			u32	packet_size;
			/*
			 * Number of resource holding WQE, depends on type.
			 */
			u32	wq_num;
			/*
			 * WQE index. Refers to either the send queue or
			 * receive queue, according to event_subtype.
			 */
			u16	wqe_index;
		} wqe;
		/* RDMA responder pagefault details */
		struct {
			u32	r_key;
			/*
			 * Received packet size, minimal size page fault
			 * resolution required for forward progress.
			 */
			u32	packet_size;
			u32	rdma_op_len;
			u64	rdma_va;
		} rdma;
	};

	struct mlx5_eq	       *eq;
	struct work_struct	work;
};

struct mlx5_td {
	struct list_head tirs_list;
	u32              tdn;
};

struct mlx5e_resources {
	u32                        pdn;
	struct mlx5_td             td;
	struct mlx5_core_mkey      mkey;
	struct mlx5_sq_bfreg       bfreg;
};

#define MLX5_MAX_RESERVED_GIDS 8

struct mlx5_rsvd_gids {
	unsigned int start;
	unsigned int count;
	struct ida ida;
};

struct mlx5_core_dev {
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
	} caps;
	phys_addr_t		iseg_base;
	struct mlx5_init_seg __iomem *iseg;
	enum mlx5_device_state	state;
	/* sync interface state */
	struct mutex		intf_state_mutex;
	unsigned long		intf_state;
	void			(*event) (struct mlx5_core_dev *dev,
					  enum mlx5_dev_event event,
					  unsigned long param);
	struct mlx5_priv	priv;
	struct mlx5_profile	*profile;
	atomic_t		num_qps;
	u32			issi;
	struct mlx5e_resources  mlx5e_res;
	struct {
		struct mlx5_rsvd_gids	reserved_gids;
		atomic_t                roce_en;
	} roce;
#ifdef CONFIG_MLX5_FPGA
	struct mlx5_fpga_device *fpga;
#endif
#ifdef CONFIG_RFS_ACCEL
	struct cpu_rmap         *rmap;
#endif
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

enum port_state_policy {
	MLX5_POLICY_DOWN	= 0,
	MLX5_POLICY_UP		= 1,
	MLX5_POLICY_FOLLOW	= 2,
	MLX5_POLICY_INVALID	= 0xffffffff
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
	u32			cap_mask2;
	u32			cap_mask2_perm;
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

static inline void *mlx5_buf_offset(struct mlx5_buf *buf, int offset)
{
		return buf->direct.buf + offset;
}

extern struct workqueue_struct *mlx5_core_wq;

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

int mlx5_cmd_init(struct mlx5_core_dev *dev);
void mlx5_cmd_cleanup(struct mlx5_core_dev *dev);
void mlx5_cmd_use_events(struct mlx5_core_dev *dev);
void mlx5_cmd_use_polling(struct mlx5_core_dev *dev);

int mlx5_cmd_exec(struct mlx5_core_dev *dev, void *in, int in_size, void *out,
		  int out_size);
int mlx5_cmd_exec_cb(struct mlx5_core_dev *dev, void *in, int in_size,
		     void *out, int out_size, mlx5_cmd_cbk_t callback,
		     void *context);
int mlx5_cmd_exec_polling(struct mlx5_core_dev *dev, void *in, int in_size,
			  void *out, int out_size);
void mlx5_cmd_mbox_status(void *out, u8 *status, u32 *syndrome);

int mlx5_core_get_caps(struct mlx5_core_dev *dev, enum mlx5_cap_type cap_type);
int mlx5_cmd_alloc_uar(struct mlx5_core_dev *dev, u32 *uarn);
int mlx5_cmd_free_uar(struct mlx5_core_dev *dev, u32 uarn);
void mlx5_health_cleanup(struct mlx5_core_dev *dev);
int mlx5_health_init(struct mlx5_core_dev *dev);
void mlx5_start_health_poll(struct mlx5_core_dev *dev);
void mlx5_stop_health_poll(struct mlx5_core_dev *dev);
void mlx5_drain_health_wq(struct mlx5_core_dev *dev);
void mlx5_trigger_health_work(struct mlx5_core_dev *dev);
void mlx5_drain_health_recovery(struct mlx5_core_dev *dev);
int mlx5_buf_alloc_node(struct mlx5_core_dev *dev, int size,
			struct mlx5_buf *buf, int node);
int mlx5_buf_alloc(struct mlx5_core_dev *dev, int size, struct mlx5_buf *buf);
void mlx5_buf_free(struct mlx5_core_dev *dev, struct mlx5_buf *buf);
int mlx5_frag_buf_alloc_node(struct mlx5_core_dev *dev, int size,
			     struct mlx5_frag_buf *buf, int node);
void mlx5_frag_buf_free(struct mlx5_core_dev *dev, struct mlx5_frag_buf *buf);
struct mlx5_cmd_mailbox *mlx5_alloc_cmd_mailbox_chain(struct mlx5_core_dev *dev,
						      gfp_t flags, int npages);
void mlx5_free_cmd_mailbox_chain(struct mlx5_core_dev *dev,
				 struct mlx5_cmd_mailbox *head);
int mlx5_core_create_srq(struct mlx5_core_dev *dev, struct mlx5_core_srq *srq,
			 struct mlx5_srq_attr *in);
int mlx5_core_destroy_srq(struct mlx5_core_dev *dev, struct mlx5_core_srq *srq);
int mlx5_core_query_srq(struct mlx5_core_dev *dev, struct mlx5_core_srq *srq,
			struct mlx5_srq_attr *out);
int mlx5_core_arm_srq(struct mlx5_core_dev *dev, struct mlx5_core_srq *srq,
		      u16 lwm, int is_srq);
void mlx5_init_mkey_table(struct mlx5_core_dev *dev);
void mlx5_cleanup_mkey_table(struct mlx5_core_dev *dev);
int mlx5_core_create_mkey_cb(struct mlx5_core_dev *dev,
			     struct mlx5_core_mkey *mkey,
			     u32 *in, int inlen,
			     u32 *out, int outlen,
			     mlx5_cmd_cbk_t callback, void *context);
int mlx5_core_create_mkey(struct mlx5_core_dev *dev,
			  struct mlx5_core_mkey *mkey,
			  u32 *in, int inlen);
int mlx5_core_destroy_mkey(struct mlx5_core_dev *dev,
			   struct mlx5_core_mkey *mkey);
int mlx5_core_query_mkey(struct mlx5_core_dev *dev, struct mlx5_core_mkey *mkey,
			 u32 *out, int outlen);
int mlx5_core_dump_fill_mkey(struct mlx5_core_dev *dev, struct mlx5_core_mkey *_mkey,
			     u32 *mkey);
int mlx5_core_alloc_pd(struct mlx5_core_dev *dev, u32 *pdn);
int mlx5_core_dealloc_pd(struct mlx5_core_dev *dev, u32 pdn);
int mlx5_core_mad_ifc(struct mlx5_core_dev *dev, const void *inb, void *outb,
		      u16 opmod, u8 port);
void mlx5_pagealloc_init(struct mlx5_core_dev *dev);
void mlx5_pagealloc_cleanup(struct mlx5_core_dev *dev);
int mlx5_pagealloc_start(struct mlx5_core_dev *dev);
void mlx5_pagealloc_stop(struct mlx5_core_dev *dev);
void mlx5_core_req_pages_handler(struct mlx5_core_dev *dev, u16 func_id,
				 s32 npages);
int mlx5_satisfy_startup_pages(struct mlx5_core_dev *dev, int boot);
int mlx5_reclaim_startup_pages(struct mlx5_core_dev *dev);
void mlx5_register_debugfs(void);
void mlx5_unregister_debugfs(void);
int mlx5_eq_init(struct mlx5_core_dev *dev);
void mlx5_eq_cleanup(struct mlx5_core_dev *dev);
void mlx5_fill_page_array(struct mlx5_buf *buf, __be64 *pas);
void mlx5_fill_page_frag_array(struct mlx5_frag_buf *frag_buf, __be64 *pas);
void mlx5_cq_completion(struct mlx5_core_dev *dev, u32 cqn);
void mlx5_rsc_event(struct mlx5_core_dev *dev, u32 rsn, int event_type);
void mlx5_srq_event(struct mlx5_core_dev *dev, u32 srqn, int event_type);
struct mlx5_core_srq *mlx5_core_get_srq(struct mlx5_core_dev *dev, u32 srqn);
void mlx5_cmd_comp_handler(struct mlx5_core_dev *dev, u64 vec, bool forced);
void mlx5_cq_event(struct mlx5_core_dev *dev, u32 cqn, int event_type);
int mlx5_create_map_eq(struct mlx5_core_dev *dev, struct mlx5_eq *eq, u8 vecidx,
		       int nent, u64 mask, const char *name,
		       enum mlx5_eq_type type);
int mlx5_destroy_unmap_eq(struct mlx5_core_dev *dev, struct mlx5_eq *eq);
int mlx5_start_eqs(struct mlx5_core_dev *dev);
int mlx5_stop_eqs(struct mlx5_core_dev *dev);
int mlx5_vector2eqn(struct mlx5_core_dev *dev, int vector, int *eqn,
		    unsigned int *irqn);
int mlx5_core_attach_mcg(struct mlx5_core_dev *dev, union ib_gid *mgid, u32 qpn);
int mlx5_core_detach_mcg(struct mlx5_core_dev *dev, union ib_gid *mgid, u32 qpn);

int mlx5_qp_debugfs_init(struct mlx5_core_dev *dev);
void mlx5_qp_debugfs_cleanup(struct mlx5_core_dev *dev);
int mlx5_core_access_reg(struct mlx5_core_dev *dev, void *data_in,
			 int size_in, void *data_out, int size_out,
			 u16 reg_num, int arg, int write);

int mlx5_debug_eq_add(struct mlx5_core_dev *dev, struct mlx5_eq *eq);
void mlx5_debug_eq_remove(struct mlx5_core_dev *dev, struct mlx5_eq *eq);
int mlx5_core_eq_query(struct mlx5_core_dev *dev, struct mlx5_eq *eq,
		       u32 *out, int outlen);
int mlx5_eq_debugfs_init(struct mlx5_core_dev *dev);
void mlx5_eq_debugfs_cleanup(struct mlx5_core_dev *dev);
int mlx5_cq_debugfs_init(struct mlx5_core_dev *dev);
void mlx5_cq_debugfs_cleanup(struct mlx5_core_dev *dev);
int mlx5_db_alloc(struct mlx5_core_dev *dev, struct mlx5_db *db);
int mlx5_db_alloc_node(struct mlx5_core_dev *dev, struct mlx5_db *db,
		       int node);
void mlx5_db_free(struct mlx5_core_dev *dev, struct mlx5_db *db);

const char *mlx5_command_str(int command);
int mlx5_cmdif_debugfs_init(struct mlx5_core_dev *dev);
void mlx5_cmdif_debugfs_cleanup(struct mlx5_core_dev *dev);
int mlx5_core_create_psv(struct mlx5_core_dev *dev, u32 pdn,
			 int npsvs, u32 *sig_index);
int mlx5_core_destroy_psv(struct mlx5_core_dev *dev, int psv_num);
void mlx5_core_put_rsc(struct mlx5_core_rsc_common *common);
int mlx5_query_odp_caps(struct mlx5_core_dev *dev,
			struct mlx5_odp_caps *odp_caps);
int mlx5_core_query_ib_ppcnt(struct mlx5_core_dev *dev,
			     u8 port_num, void *out, size_t sz);
#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
int mlx5_core_page_fault_resume(struct mlx5_core_dev *dev, u32 token,
				u32 wq_num, u8 type, int error);
#endif

int mlx5_init_rl_table(struct mlx5_core_dev *dev);
void mlx5_cleanup_rl_table(struct mlx5_core_dev *dev);
int mlx5_rl_add_rate(struct mlx5_core_dev *dev, u32 rate, u16 *index);
void mlx5_rl_remove_rate(struct mlx5_core_dev *dev, u32 rate);
bool mlx5_rl_is_in_range(struct mlx5_core_dev *dev, u32 rate);
int mlx5_alloc_bfreg(struct mlx5_core_dev *mdev, struct mlx5_sq_bfreg *bfreg,
		     bool map_wc, bool fast_path);
void mlx5_free_bfreg(struct mlx5_core_dev *mdev, struct mlx5_sq_bfreg *bfreg);

unsigned int mlx5_core_reserved_gids_count(struct mlx5_core_dev *dev);
int mlx5_core_roce_gid_set(struct mlx5_core_dev *dev, unsigned int index,
			   u8 roce_version, u8 roce_l3_type, const u8 *gid,
			   const u8 *mac, bool vlan, u16 vlan_id);

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
	MAX_UMR_CACHE_ENTRY = 20,
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
	void			(*event)(struct mlx5_core_dev *dev, void *context,
					 enum mlx5_dev_event event, unsigned long param);
	void			(*pfault)(struct mlx5_core_dev *dev,
					  void *context,
					  struct mlx5_pagefault *pfault);
	void *                  (*get_dev)(void *context);
	int			protocol;
	struct list_head	list;
};

void *mlx5_get_protocol_dev(struct mlx5_core_dev *mdev, int protocol);
int mlx5_register_interface(struct mlx5_interface *intf);
void mlx5_unregister_interface(struct mlx5_interface *intf);
int mlx5_core_query_vendor_id(struct mlx5_core_dev *mdev, u32 *vendor_id);

int mlx5_cmd_create_vport_lag(struct mlx5_core_dev *dev);
int mlx5_cmd_destroy_vport_lag(struct mlx5_core_dev *dev);
bool mlx5_lag_is_active(struct mlx5_core_dev *dev);
struct net_device *mlx5_lag_get_roce_netdev(struct mlx5_core_dev *dev);
struct mlx5_uars_page *mlx5_get_uars_page(struct mlx5_core_dev *mdev);
void mlx5_put_uars_page(struct mlx5_core_dev *mdev, struct mlx5_uars_page *up);

#ifndef CONFIG_MLX5_CORE_IPOIB
static inline
struct net_device *mlx5_rdma_netdev_alloc(struct mlx5_core_dev *mdev,
					  struct ib_device *ibdev,
					  const char *name,
					  void (*setup)(struct net_device *))
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline void mlx5_rdma_netdev_free(struct net_device *netdev) {}
#else
struct net_device *mlx5_rdma_netdev_alloc(struct mlx5_core_dev *mdev,
					  struct ib_device *ibdev,
					  const char *name,
					  void (*setup)(struct net_device *));
void mlx5_rdma_netdev_free(struct net_device *netdev);
#endif /* CONFIG_MLX5_CORE_IPOIB */

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

static inline int mlx5_core_is_pf(struct mlx5_core_dev *dev)
{
	return !(dev->priv.pci_dev_data & MLX5_PCI_DEV_IS_VF);
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

enum {
	MLX5_TRIGGERED_CMD_COMP = (u64)1 << 32,
};

#endif /* MLX5_DRIVER_H */
