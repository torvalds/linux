/*
 * Copyright (c) 2013, Mellanox Technologies inc.  All rights reserved.
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
#include <linux/vmalloc.h>
#include <linux/radix-tree.h>
#include <linux/mlx5/device.h>
#include <linux/mlx5/doorbell.h>

enum {
	MLX5_BOARD_ID_LEN = 64,
	MLX5_MAX_NAME_LEN = 16,
};

enum {
	/* one minute for the sake of bringup. Generally, commands must always
	 * complete and we may need to increase this timeout value
	 */
	MLX5_CMD_TIMEOUT_MSEC	= 7200 * 1000,
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
	MLX5_EQ_VEC_COMP_BASE,
};

enum {
	MLX5_MAX_EQ_NAME	= 32
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
	MLX5_CMD_OP_QUERY_HCA_CAP		= 0x100,
	MLX5_CMD_OP_QUERY_ADAPTER		= 0x101,
	MLX5_CMD_OP_INIT_HCA			= 0x102,
	MLX5_CMD_OP_TEARDOWN_HCA		= 0x103,
	MLX5_CMD_OP_ENABLE_HCA			= 0x104,
	MLX5_CMD_OP_DISABLE_HCA			= 0x105,
	MLX5_CMD_OP_QUERY_PAGES			= 0x107,
	MLX5_CMD_OP_MANAGE_PAGES		= 0x108,
	MLX5_CMD_OP_SET_HCA_CAP			= 0x109,

	MLX5_CMD_OP_CREATE_MKEY			= 0x200,
	MLX5_CMD_OP_QUERY_MKEY			= 0x201,
	MLX5_CMD_OP_DESTROY_MKEY		= 0x202,
	MLX5_CMD_OP_QUERY_SPECIAL_CONTEXTS	= 0x203,

	MLX5_CMD_OP_CREATE_EQ			= 0x301,
	MLX5_CMD_OP_DESTROY_EQ			= 0x302,
	MLX5_CMD_OP_QUERY_EQ			= 0x303,

	MLX5_CMD_OP_CREATE_CQ			= 0x400,
	MLX5_CMD_OP_DESTROY_CQ			= 0x401,
	MLX5_CMD_OP_QUERY_CQ			= 0x402,
	MLX5_CMD_OP_MODIFY_CQ			= 0x403,

	MLX5_CMD_OP_CREATE_QP			= 0x500,
	MLX5_CMD_OP_DESTROY_QP			= 0x501,
	MLX5_CMD_OP_RST2INIT_QP			= 0x502,
	MLX5_CMD_OP_INIT2RTR_QP			= 0x503,
	MLX5_CMD_OP_RTR2RTS_QP			= 0x504,
	MLX5_CMD_OP_RTS2RTS_QP			= 0x505,
	MLX5_CMD_OP_SQERR2RTS_QP		= 0x506,
	MLX5_CMD_OP_2ERR_QP			= 0x507,
	MLX5_CMD_OP_RTS2SQD_QP			= 0x508,
	MLX5_CMD_OP_SQD2RTS_QP			= 0x509,
	MLX5_CMD_OP_2RST_QP			= 0x50a,
	MLX5_CMD_OP_QUERY_QP			= 0x50b,
	MLX5_CMD_OP_CONF_SQP			= 0x50c,
	MLX5_CMD_OP_MAD_IFC			= 0x50d,
	MLX5_CMD_OP_INIT2INIT_QP		= 0x50e,
	MLX5_CMD_OP_SUSPEND_QP			= 0x50f,
	MLX5_CMD_OP_UNSUSPEND_QP		= 0x510,
	MLX5_CMD_OP_SQD2SQD_QP			= 0x511,
	MLX5_CMD_OP_ALLOC_QP_COUNTER_SET	= 0x512,
	MLX5_CMD_OP_DEALLOC_QP_COUNTER_SET	= 0x513,
	MLX5_CMD_OP_QUERY_QP_COUNTER_SET	= 0x514,

	MLX5_CMD_OP_CREATE_PSV			= 0x600,
	MLX5_CMD_OP_DESTROY_PSV			= 0x601,
	MLX5_CMD_OP_QUERY_PSV			= 0x602,
	MLX5_CMD_OP_QUERY_SIG_RULE_TABLE	= 0x603,
	MLX5_CMD_OP_QUERY_BLOCK_SIZE_TABLE	= 0x604,

	MLX5_CMD_OP_CREATE_SRQ			= 0x700,
	MLX5_CMD_OP_DESTROY_SRQ			= 0x701,
	MLX5_CMD_OP_QUERY_SRQ			= 0x702,
	MLX5_CMD_OP_ARM_RQ			= 0x703,
	MLX5_CMD_OP_RESIZE_SRQ			= 0x704,

	MLX5_CMD_OP_ALLOC_PD			= 0x800,
	MLX5_CMD_OP_DEALLOC_PD			= 0x801,
	MLX5_CMD_OP_ALLOC_UAR			= 0x802,
	MLX5_CMD_OP_DEALLOC_UAR			= 0x803,

	MLX5_CMD_OP_ATTACH_TO_MCG		= 0x806,
	MLX5_CMD_OP_DETACH_FROM_MCG		= 0x807,


	MLX5_CMD_OP_ALLOC_XRCD			= 0x80e,
	MLX5_CMD_OP_DEALLOC_XRCD		= 0x80f,

	MLX5_CMD_OP_ACCESS_REG			= 0x805,
	MLX5_CMD_OP_MAX				= 0x810,
};

enum {
	MLX5_REG_PCAP		 = 0x5001,
	MLX5_REG_PMTU		 = 0x5003,
	MLX5_REG_PTYS		 = 0x5004,
	MLX5_REG_PAOS		 = 0x5006,
	MLX5_REG_PMAOS		 = 0x5012,
	MLX5_REG_PUDE		 = 0x5009,
	MLX5_REG_PMPE		 = 0x5010,
	MLX5_REG_PELC		 = 0x500e,
	MLX5_REG_PMLP		 = 0, /* TBD */
	MLX5_REG_NODE_DESC	 = 0x6001,
	MLX5_REG_HOST_ENDIANNESS = 0x7004,
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
};

struct mlx5_uuar_info {
	struct mlx5_uar	       *uars;
	int			num_uars;
	int			num_low_latency_uuars;
	unsigned long	       *bitmap;
	unsigned int	       *count;
	struct mlx5_bf	       *bfs;

	/*
	 * protect uuar allocation data structs
	 */
	struct mutex		lock;
};

struct mlx5_bf {
	void __iomem	       *reg;
	void __iomem	       *regreg;
	int			buf_size;
	struct mlx5_uar	       *uar;
	unsigned long		offset;
	int			need_lock;
	/* protect blue flame buffer selection when needed
	 */
	spinlock_t		lock;

	/* serialize 64 bit writes when done as two 32 bit accesses
	 */
	spinlock_t		lock32;
	int			uuarn;
};

struct mlx5_cmd_first {
	__be32		data[4];
};

struct mlx5_cmd_msg {
	struct list_head		list;
	struct cache_ent	       *cache;
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

struct cache_ent {
	/* protect block chain allocations
	 */
	spinlock_t		lock;
	struct list_head	head;
};

struct cmd_msg_cache {
	struct cache_ent	large;
	struct cache_ent	med;

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
	struct cmd_msg_cache cache;
	int checksum_disabled;
	struct mlx5_cmd_stats stats[MLX5_CMD_OP_MAX];
};

struct mlx5_port_caps {
	int	gid_table_len;
	int	pkey_table_len;
};

struct mlx5_caps {
	u8	log_max_eq;
	u8	log_max_cq;
	u8	log_max_qp;
	u8	log_max_mkey;
	u8	log_max_pd;
	u8	log_max_srq;
	u32	max_cqes;
	int	max_wqes;
	int	max_sq_desc_sz;
	int	max_rq_desc_sz;
	u64	flags;
	u16	stat_rate_support;
	int	log_max_msg;
	int	num_ports;
	int	max_ra_res_qp;
	int	max_ra_req_qp;
	int	max_srq_wqes;
	int	bf_reg_size;
	int	bf_regs_per_page;
	struct mlx5_port_caps	port[MLX5_MAX_PORTS];
	u8			ext_port_cap[MLX5_MAX_PORTS];
	int	max_vf;
	u32	reserved_lkey;
	u8	local_ca_ack_delay;
	u8	log_max_mcg;
	u32	max_qp_mcg;
	int	min_page_sz;
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
	struct mlx5_buf_list   *page_list;
	int			nbufs;
	int			npages;
	int			page_shift;
	int			size;
};

struct mlx5_eq {
	struct mlx5_core_dev   *dev;
	__be32 __iomem	       *doorbell;
	u32			cons_index;
	struct mlx5_buf		buf;
	int			size;
	u8			irqn;
	u8			eqn;
	int			nent;
	u64			mask;
	char			name[MLX5_MAX_EQ_NAME];
	struct list_head	list;
	int			index;
	struct mlx5_rsc_debug	*dbg;
};


struct mlx5_core_mr {
	u64			iova;
	u64			size;
	u32			key;
	u32			pd;
	u32			access;
};

struct mlx5_core_srq {
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
	struct list_head       *comp_eq_head;
	struct mlx5_eq		pages_eq;
	struct mlx5_eq		async_eq;
	struct mlx5_eq		cmd_eq;
	struct msix_entry	*msix_arr;
	int			num_comp_vectors;
	/* protect EQs list
	 */
	spinlock_t		lock;
};

struct mlx5_uar {
	u32			index;
	struct list_head	bf_list;
	unsigned		free_bf_bmap;
	void __iomem	       *wc_map;
	void __iomem	       *map;
};


struct mlx5_core_health {
	struct health_buffer __iomem   *health;
	__be32 __iomem		       *health_counter;
	struct timer_list		timer;
	struct list_head		list;
	u32				prev;
	int				miss_counter;
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

struct mlx5_priv {
	char			name[MLX5_MAX_NAME_LEN];
	struct mlx5_eq_table	eq_table;
	struct mlx5_uuar_info	uuari;
	MLX5_DECLARE_DOORBELL_LOCK(cq_uar_lock);

	/* pages stuff */
	struct workqueue_struct *pg_wq;
	struct rb_root		page_root;
	int			fw_pages;
	int			reg_pages;

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

	/* start: alloc staff */
	struct mutex            pgdir_mutex;
	struct list_head        pgdir_list;
	/* end: alloc staff */
	struct dentry	       *dbg_root;

	/* protect mkey key part */
	spinlock_t		mkey_lock;
	u8			mkey_key;
};

struct mlx5_core_dev {
	struct pci_dev	       *pdev;
	u8			rev_id;
	char			board_id[MLX5_BOARD_ID_LEN];
	struct mlx5_cmd		cmd;
	struct mlx5_caps	caps;
	phys_addr_t		iseg_base;
	struct mlx5_init_seg __iomem *iseg;
	void			(*event) (struct mlx5_core_dev *dev,
					  enum mlx5_dev_event event,
					  void *data);
	struct mlx5_priv	priv;
	struct mlx5_profile	*profile;
	atomic_t		num_qps;
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
	MLX5_DB_PER_PAGE = PAGE_SIZE / L1_CACHE_BYTES,
};

enum {
	MLX5_COMP_EQ_SIZE = 1024,
};

struct mlx5_db_pgdir {
	struct list_head	list;
	DECLARE_BITMAP(bitmap, MLX5_DB_PER_PAGE);
	__be32		       *db_page;
	dma_addr_t		db_dma;
};

typedef void (*mlx5_cmd_cbk_t)(int status, void *context);

struct mlx5_cmd_work_ent {
	struct mlx5_cmd_msg    *in;
	struct mlx5_cmd_msg    *out;
	mlx5_cmd_cbk_t		callback;
	void		       *context;
	int idx;
	struct completion	done;
	struct mlx5_cmd        *cmd;
	struct work_struct	work;
	struct mlx5_cmd_layout *lay;
	int			ret;
	int			page_queue;
	u8			status;
	u8			token;
	struct timespec		ts1;
	struct timespec		ts2;
};

struct mlx5_pas {
	u64	pa;
	u8	log_sz;
};

static inline void *mlx5_buf_offset(struct mlx5_buf *buf, int offset)
{
	if (likely(BITS_PER_LONG == 64 || buf->nbufs == 1))
		return buf->direct.buf + offset;
	else
		return buf->page_list[offset >> PAGE_SHIFT].buf +
			(offset & (PAGE_SIZE - 1));
}

extern struct workqueue_struct *mlx5_core_wq;

#define STRUCT_FIELD(header, field) \
	.struct_offset_bytes = offsetof(struct ib_unpacked_ ## header, field),      \
	.struct_size_bytes   = sizeof((struct ib_unpacked_ ## header *)0)->field

struct ib_field {
	size_t struct_offset_bytes;
	size_t struct_size_bytes;
	int    offset_bits;
	int    size_bits;
};

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

static inline void *mlx5_vzalloc(unsigned long size)
{
	void *rtn;

	rtn = kzalloc(size, GFP_KERNEL | __GFP_NOWARN);
	if (!rtn)
		rtn = vzalloc(size);
	return rtn;
}

static inline void mlx5_vfree(const void *addr)
{
	if (addr && is_vmalloc_addr(addr))
		vfree(addr);
	else
		kfree(addr);
}

int mlx5_dev_init(struct mlx5_core_dev *dev, struct pci_dev *pdev);
void mlx5_dev_cleanup(struct mlx5_core_dev *dev);
int mlx5_cmd_init(struct mlx5_core_dev *dev);
void mlx5_cmd_cleanup(struct mlx5_core_dev *dev);
void mlx5_cmd_use_events(struct mlx5_core_dev *dev);
void mlx5_cmd_use_polling(struct mlx5_core_dev *dev);
int mlx5_cmd_status_to_err(struct mlx5_outbox_hdr *hdr);
int mlx5_cmd_exec(struct mlx5_core_dev *dev, void *in, int in_size, void *out,
		  int out_size);
int mlx5_cmd_alloc_uar(struct mlx5_core_dev *dev, u32 *uarn);
int mlx5_cmd_free_uar(struct mlx5_core_dev *dev, u32 uarn);
int mlx5_alloc_uuars(struct mlx5_core_dev *dev, struct mlx5_uuar_info *uuari);
int mlx5_free_uuars(struct mlx5_core_dev *dev, struct mlx5_uuar_info *uuari);
void mlx5_health_cleanup(void);
void  __init mlx5_health_init(void);
void mlx5_start_health_poll(struct mlx5_core_dev *dev);
void mlx5_stop_health_poll(struct mlx5_core_dev *dev);
int mlx5_buf_alloc(struct mlx5_core_dev *dev, int size, int max_direct,
		   struct mlx5_buf *buf);
void mlx5_buf_free(struct mlx5_core_dev *dev, struct mlx5_buf *buf);
struct mlx5_cmd_mailbox *mlx5_alloc_cmd_mailbox_chain(struct mlx5_core_dev *dev,
						      gfp_t flags, int npages);
void mlx5_free_cmd_mailbox_chain(struct mlx5_core_dev *dev,
				 struct mlx5_cmd_mailbox *head);
int mlx5_core_create_srq(struct mlx5_core_dev *dev, struct mlx5_core_srq *srq,
			 struct mlx5_create_srq_mbox_in *in, int inlen);
int mlx5_core_destroy_srq(struct mlx5_core_dev *dev, struct mlx5_core_srq *srq);
int mlx5_core_query_srq(struct mlx5_core_dev *dev, struct mlx5_core_srq *srq,
			struct mlx5_query_srq_mbox_out *out);
int mlx5_core_arm_srq(struct mlx5_core_dev *dev, struct mlx5_core_srq *srq,
		      u16 lwm, int is_srq);
int mlx5_core_create_mkey(struct mlx5_core_dev *dev, struct mlx5_core_mr *mr,
			  struct mlx5_create_mkey_mbox_in *in, int inlen);
int mlx5_core_destroy_mkey(struct mlx5_core_dev *dev, struct mlx5_core_mr *mr);
int mlx5_core_query_mkey(struct mlx5_core_dev *dev, struct mlx5_core_mr *mr,
			 struct mlx5_query_mkey_mbox_out *out, int outlen);
int mlx5_core_dump_fill_mkey(struct mlx5_core_dev *dev, struct mlx5_core_mr *mr,
			     u32 *mkey);
int mlx5_core_alloc_pd(struct mlx5_core_dev *dev, u32 *pdn);
int mlx5_core_dealloc_pd(struct mlx5_core_dev *dev, u32 pdn);
int mlx5_core_mad_ifc(struct mlx5_core_dev *dev, void *inb, void *outb,
		      u16 opmod, int port);
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
void mlx5_cq_completion(struct mlx5_core_dev *dev, u32 cqn);
void mlx5_qp_event(struct mlx5_core_dev *dev, u32 qpn, int event_type);
void mlx5_srq_event(struct mlx5_core_dev *dev, u32 srqn, int event_type);
struct mlx5_core_srq *mlx5_core_get_srq(struct mlx5_core_dev *dev, u32 srqn);
void mlx5_cmd_comp_handler(struct mlx5_core_dev *dev, unsigned long vector);
void mlx5_cq_event(struct mlx5_core_dev *dev, u32 cqn, int event_type);
int mlx5_create_map_eq(struct mlx5_core_dev *dev, struct mlx5_eq *eq, u8 vecidx,
		       int nent, u64 mask, const char *name, struct mlx5_uar *uar);
int mlx5_destroy_unmap_eq(struct mlx5_core_dev *dev, struct mlx5_eq *eq);
int mlx5_start_eqs(struct mlx5_core_dev *dev);
int mlx5_stop_eqs(struct mlx5_core_dev *dev);
int mlx5_core_attach_mcg(struct mlx5_core_dev *dev, union ib_gid *mgid, u32 qpn);
int mlx5_core_detach_mcg(struct mlx5_core_dev *dev, union ib_gid *mgid, u32 qpn);

int mlx5_qp_debugfs_init(struct mlx5_core_dev *dev);
void mlx5_qp_debugfs_cleanup(struct mlx5_core_dev *dev);
int mlx5_core_access_reg(struct mlx5_core_dev *dev, void *data_in,
			 int size_in, void *data_out, int size_out,
			 u16 reg_num, int arg, int write);
int mlx5_set_port_caps(struct mlx5_core_dev *dev, int port_num, u32 caps);

int mlx5_debug_eq_add(struct mlx5_core_dev *dev, struct mlx5_eq *eq);
void mlx5_debug_eq_remove(struct mlx5_core_dev *dev, struct mlx5_eq *eq);
int mlx5_core_eq_query(struct mlx5_core_dev *dev, struct mlx5_eq *eq,
		       struct mlx5_query_eq_mbox_out *out, int outlen);
int mlx5_eq_debugfs_init(struct mlx5_core_dev *dev);
void mlx5_eq_debugfs_cleanup(struct mlx5_core_dev *dev);
int mlx5_cq_debugfs_init(struct mlx5_core_dev *dev);
void mlx5_cq_debugfs_cleanup(struct mlx5_core_dev *dev);
int mlx5_db_alloc(struct mlx5_core_dev *dev, struct mlx5_db *db);
void mlx5_db_free(struct mlx5_core_dev *dev, struct mlx5_db *db);

const char *mlx5_command_str(int command);
int mlx5_cmdif_debugfs_init(struct mlx5_core_dev *dev);
void mlx5_cmdif_debugfs_cleanup(struct mlx5_core_dev *dev);

static inline u32 mlx5_mkey_to_idx(u32 mkey)
{
	return mkey >> 8;
}

static inline u32 mlx5_idx_to_mkey(u32 mkey_idx)
{
	return mkey_idx << 8;
}

enum {
	MLX5_PROF_MASK_QP_SIZE		= (u64)1 << 0,
	MLX5_PROF_MASK_MR_CACHE		= (u64)1 << 1,
};

enum {
	MAX_MR_CACHE_ENTRIES    = 16,
};

struct mlx5_profile {
	u64	mask;
	u32	log_max_qp;
	struct {
		int	size;
		int	limit;
	} mr_cache[MAX_MR_CACHE_ENTRIES];
};

#endif /* MLX5_DRIVER_H */
