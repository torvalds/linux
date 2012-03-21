/*
 * Copyright (c) 2006, 2007 Cisco Systems, Inc.  All rights reserved.
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
 *	- Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *	- Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
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

#ifndef MLX4_DEVICE_H
#define MLX4_DEVICE_H

#include <linux/pci.h>
#include <linux/completion.h>
#include <linux/radix-tree.h>

#include <linux/atomic.h>

#define MAX_MSIX_P_PORT		17
#define MAX_MSIX		64
#define MSIX_LEGACY_SZ		4
#define MIN_MSIX_P_PORT		5

enum {
	MLX4_FLAG_MSI_X		= 1 << 0,
	MLX4_FLAG_OLD_PORT_CMDS	= 1 << 1,
	MLX4_FLAG_MASTER	= 1 << 2,
	MLX4_FLAG_SLAVE		= 1 << 3,
	MLX4_FLAG_SRIOV		= 1 << 4,
};

enum {
	MLX4_MAX_PORTS		= 2
};

enum {
	MLX4_BOARD_ID_LEN = 64
};

enum {
	MLX4_MAX_NUM_PF		= 16,
	MLX4_MAX_NUM_VF		= 64,
	MLX4_MFUNC_MAX		= 80,
	MLX4_MFUNC_EQ_NUM	= 4,
	MLX4_MFUNC_MAX_EQES     = 8,
	MLX4_MFUNC_EQE_MASK     = (MLX4_MFUNC_MAX_EQES - 1)
};

enum {
	MLX4_DEV_CAP_FLAG_RC		= 1LL <<  0,
	MLX4_DEV_CAP_FLAG_UC		= 1LL <<  1,
	MLX4_DEV_CAP_FLAG_UD		= 1LL <<  2,
	MLX4_DEV_CAP_FLAG_XRC		= 1LL <<  3,
	MLX4_DEV_CAP_FLAG_SRQ		= 1LL <<  6,
	MLX4_DEV_CAP_FLAG_IPOIB_CSUM	= 1LL <<  7,
	MLX4_DEV_CAP_FLAG_BAD_PKEY_CNTR	= 1LL <<  8,
	MLX4_DEV_CAP_FLAG_BAD_QKEY_CNTR	= 1LL <<  9,
	MLX4_DEV_CAP_FLAG_DPDP		= 1LL << 12,
	MLX4_DEV_CAP_FLAG_BLH		= 1LL << 15,
	MLX4_DEV_CAP_FLAG_MEM_WINDOW	= 1LL << 16,
	MLX4_DEV_CAP_FLAG_APM		= 1LL << 17,
	MLX4_DEV_CAP_FLAG_ATOMIC	= 1LL << 18,
	MLX4_DEV_CAP_FLAG_RAW_MCAST	= 1LL << 19,
	MLX4_DEV_CAP_FLAG_UD_AV_PORT	= 1LL << 20,
	MLX4_DEV_CAP_FLAG_UD_MCAST	= 1LL << 21,
	MLX4_DEV_CAP_FLAG_IBOE		= 1LL << 30,
	MLX4_DEV_CAP_FLAG_UC_LOOPBACK	= 1LL << 32,
	MLX4_DEV_CAP_FLAG_FCS_KEEP	= 1LL << 34,
	MLX4_DEV_CAP_FLAG_WOL_PORT1	= 1LL << 37,
	MLX4_DEV_CAP_FLAG_WOL_PORT2	= 1LL << 38,
	MLX4_DEV_CAP_FLAG_UDP_RSS	= 1LL << 40,
	MLX4_DEV_CAP_FLAG_VEP_UC_STEER	= 1LL << 41,
	MLX4_DEV_CAP_FLAG_VEP_MC_STEER	= 1LL << 42,
	MLX4_DEV_CAP_FLAG_COUNTERS	= 1LL << 48,
	MLX4_DEV_CAP_FLAG_SENSE_SUPPORT	= 1LL << 55
};

#define MLX4_ATTR_EXTENDED_PORT_INFO	cpu_to_be16(0xff90)

enum {
	MLX4_BMME_FLAG_LOCAL_INV	= 1 <<  6,
	MLX4_BMME_FLAG_REMOTE_INV	= 1 <<  7,
	MLX4_BMME_FLAG_TYPE_2_WIN	= 1 <<  9,
	MLX4_BMME_FLAG_RESERVED_LKEY	= 1 << 10,
	MLX4_BMME_FLAG_FAST_REG_WR	= 1 << 11,
};

enum mlx4_event {
	MLX4_EVENT_TYPE_COMP		   = 0x00,
	MLX4_EVENT_TYPE_PATH_MIG	   = 0x01,
	MLX4_EVENT_TYPE_COMM_EST	   = 0x02,
	MLX4_EVENT_TYPE_SQ_DRAINED	   = 0x03,
	MLX4_EVENT_TYPE_SRQ_QP_LAST_WQE	   = 0x13,
	MLX4_EVENT_TYPE_SRQ_LIMIT	   = 0x14,
	MLX4_EVENT_TYPE_CQ_ERROR	   = 0x04,
	MLX4_EVENT_TYPE_WQ_CATAS_ERROR	   = 0x05,
	MLX4_EVENT_TYPE_EEC_CATAS_ERROR	   = 0x06,
	MLX4_EVENT_TYPE_PATH_MIG_FAILED	   = 0x07,
	MLX4_EVENT_TYPE_WQ_INVAL_REQ_ERROR = 0x10,
	MLX4_EVENT_TYPE_WQ_ACCESS_ERROR	   = 0x11,
	MLX4_EVENT_TYPE_SRQ_CATAS_ERROR	   = 0x12,
	MLX4_EVENT_TYPE_LOCAL_CATAS_ERROR  = 0x08,
	MLX4_EVENT_TYPE_PORT_CHANGE	   = 0x09,
	MLX4_EVENT_TYPE_EQ_OVERFLOW	   = 0x0f,
	MLX4_EVENT_TYPE_ECC_DETECT	   = 0x0e,
	MLX4_EVENT_TYPE_CMD		   = 0x0a,
	MLX4_EVENT_TYPE_VEP_UPDATE	   = 0x19,
	MLX4_EVENT_TYPE_COMM_CHANNEL	   = 0x18,
	MLX4_EVENT_TYPE_FATAL_WARNING	   = 0x1b,
	MLX4_EVENT_TYPE_FLR_EVENT	   = 0x1c,
	MLX4_EVENT_TYPE_NONE		   = 0xff,
};

enum {
	MLX4_PORT_CHANGE_SUBTYPE_DOWN	= 1,
	MLX4_PORT_CHANGE_SUBTYPE_ACTIVE	= 4
};

enum {
	MLX4_FATAL_WARNING_SUBTYPE_WARMING = 0,
};

enum {
	MLX4_PERM_LOCAL_READ	= 1 << 10,
	MLX4_PERM_LOCAL_WRITE	= 1 << 11,
	MLX4_PERM_REMOTE_READ	= 1 << 12,
	MLX4_PERM_REMOTE_WRITE	= 1 << 13,
	MLX4_PERM_ATOMIC	= 1 << 14
};

enum {
	MLX4_OPCODE_NOP			= 0x00,
	MLX4_OPCODE_SEND_INVAL		= 0x01,
	MLX4_OPCODE_RDMA_WRITE		= 0x08,
	MLX4_OPCODE_RDMA_WRITE_IMM	= 0x09,
	MLX4_OPCODE_SEND		= 0x0a,
	MLX4_OPCODE_SEND_IMM		= 0x0b,
	MLX4_OPCODE_LSO			= 0x0e,
	MLX4_OPCODE_RDMA_READ		= 0x10,
	MLX4_OPCODE_ATOMIC_CS		= 0x11,
	MLX4_OPCODE_ATOMIC_FA		= 0x12,
	MLX4_OPCODE_MASKED_ATOMIC_CS	= 0x14,
	MLX4_OPCODE_MASKED_ATOMIC_FA	= 0x15,
	MLX4_OPCODE_BIND_MW		= 0x18,
	MLX4_OPCODE_FMR			= 0x19,
	MLX4_OPCODE_LOCAL_INVAL		= 0x1b,
	MLX4_OPCODE_CONFIG_CMD		= 0x1f,

	MLX4_RECV_OPCODE_RDMA_WRITE_IMM	= 0x00,
	MLX4_RECV_OPCODE_SEND		= 0x01,
	MLX4_RECV_OPCODE_SEND_IMM	= 0x02,
	MLX4_RECV_OPCODE_SEND_INVAL	= 0x03,

	MLX4_CQE_OPCODE_ERROR		= 0x1e,
	MLX4_CQE_OPCODE_RESIZE		= 0x16,
};

enum {
	MLX4_STAT_RATE_OFFSET	= 5
};

enum mlx4_protocol {
	MLX4_PROT_IB_IPV6 = 0,
	MLX4_PROT_ETH,
	MLX4_PROT_IB_IPV4,
	MLX4_PROT_FCOE
};

enum {
	MLX4_MTT_FLAG_PRESENT		= 1
};

enum mlx4_qp_region {
	MLX4_QP_REGION_FW = 0,
	MLX4_QP_REGION_ETH_ADDR,
	MLX4_QP_REGION_FC_ADDR,
	MLX4_QP_REGION_FC_EXCH,
	MLX4_NUM_QP_REGION
};

enum mlx4_port_type {
	MLX4_PORT_TYPE_NONE	= 0,
	MLX4_PORT_TYPE_IB	= 1,
	MLX4_PORT_TYPE_ETH	= 2,
	MLX4_PORT_TYPE_AUTO	= 3
};

enum mlx4_special_vlan_idx {
	MLX4_NO_VLAN_IDX        = 0,
	MLX4_VLAN_MISS_IDX,
	MLX4_VLAN_REGULAR
};

enum mlx4_steer_type {
	MLX4_MC_STEER = 0,
	MLX4_UC_STEER,
	MLX4_NUM_STEERS
};

enum {
	MLX4_NUM_FEXCH          = 64 * 1024,
};

enum {
	MLX4_MAX_FAST_REG_PAGES = 511,
};

static inline u64 mlx4_fw_ver(u64 major, u64 minor, u64 subminor)
{
	return (major << 32) | (minor << 16) | subminor;
}

struct mlx4_caps {
	u64			fw_ver;
	u32			function;
	int			num_ports;
	int			vl_cap[MLX4_MAX_PORTS + 1];
	int			ib_mtu_cap[MLX4_MAX_PORTS + 1];
	__be32			ib_port_def_cap[MLX4_MAX_PORTS + 1];
	u64			def_mac[MLX4_MAX_PORTS + 1];
	int			eth_mtu_cap[MLX4_MAX_PORTS + 1];
	int			gid_table_len[MLX4_MAX_PORTS + 1];
	int			pkey_table_len[MLX4_MAX_PORTS + 1];
	int			trans_type[MLX4_MAX_PORTS + 1];
	int			vendor_oui[MLX4_MAX_PORTS + 1];
	int			wavelength[MLX4_MAX_PORTS + 1];
	u64			trans_code[MLX4_MAX_PORTS + 1];
	int			local_ca_ack_delay;
	int			num_uars;
	u32			uar_page_size;
	int			bf_reg_size;
	int			bf_regs_per_page;
	int			max_sq_sg;
	int			max_rq_sg;
	int			num_qps;
	int			max_wqes;
	int			max_sq_desc_sz;
	int			max_rq_desc_sz;
	int			max_qp_init_rdma;
	int			max_qp_dest_rdma;
	int			sqp_start;
	int			num_srqs;
	int			max_srq_wqes;
	int			max_srq_sge;
	int			reserved_srqs;
	int			num_cqs;
	int			max_cqes;
	int			reserved_cqs;
	int			num_eqs;
	int			reserved_eqs;
	int			num_comp_vectors;
	int			comp_pool;
	int			num_mpts;
	int			max_fmr_maps;
	int			num_mtts;
	int			fmr_reserved_mtts;
	int			reserved_mtts;
	int			reserved_mrws;
	int			reserved_uars;
	int			num_mgms;
	int			num_amgms;
	int			reserved_mcgs;
	int			num_qp_per_mgm;
	int			num_pds;
	int			reserved_pds;
	int			max_xrcds;
	int			reserved_xrcds;
	int			mtt_entry_sz;
	u32			max_msg_sz;
	u32			page_size_cap;
	u64			flags;
	u32			bmme_flags;
	u32			reserved_lkey;
	u16			stat_rate_support;
	u8			port_width_cap[MLX4_MAX_PORTS + 1];
	int			max_gso_sz;
	int                     reserved_qps_cnt[MLX4_NUM_QP_REGION];
	int			reserved_qps;
	int                     reserved_qps_base[MLX4_NUM_QP_REGION];
	int                     log_num_macs;
	int                     log_num_vlans;
	int                     log_num_prios;
	enum mlx4_port_type	port_type[MLX4_MAX_PORTS + 1];
	u8			supported_type[MLX4_MAX_PORTS + 1];
	u8                      suggested_type[MLX4_MAX_PORTS + 1];
	u8                      default_sense[MLX4_MAX_PORTS + 1];
	u32			port_mask[MLX4_MAX_PORTS + 1];
	enum mlx4_port_type	possible_type[MLX4_MAX_PORTS + 1];
	u32			max_counters;
	u8			port_ib_mtu[MLX4_MAX_PORTS + 1];
};

struct mlx4_buf_list {
	void		       *buf;
	dma_addr_t		map;
};

struct mlx4_buf {
	struct mlx4_buf_list	direct;
	struct mlx4_buf_list   *page_list;
	int			nbufs;
	int			npages;
	int			page_shift;
};

struct mlx4_mtt {
	u32			offset;
	int			order;
	int			page_shift;
};

enum {
	MLX4_DB_PER_PAGE = PAGE_SIZE / 4
};

struct mlx4_db_pgdir {
	struct list_head	list;
	DECLARE_BITMAP(order0, MLX4_DB_PER_PAGE);
	DECLARE_BITMAP(order1, MLX4_DB_PER_PAGE / 2);
	unsigned long	       *bits[2];
	__be32		       *db_page;
	dma_addr_t		db_dma;
};

struct mlx4_ib_user_db_page;

struct mlx4_db {
	__be32			*db;
	union {
		struct mlx4_db_pgdir		*pgdir;
		struct mlx4_ib_user_db_page	*user_page;
	}			u;
	dma_addr_t		dma;
	int			index;
	int			order;
};

struct mlx4_hwq_resources {
	struct mlx4_db		db;
	struct mlx4_mtt		mtt;
	struct mlx4_buf		buf;
};

struct mlx4_mr {
	struct mlx4_mtt		mtt;
	u64			iova;
	u64			size;
	u32			key;
	u32			pd;
	u32			access;
	int			enabled;
};

struct mlx4_fmr {
	struct mlx4_mr		mr;
	struct mlx4_mpt_entry  *mpt;
	__be64		       *mtts;
	dma_addr_t		dma_handle;
	int			max_pages;
	int			max_maps;
	int			maps;
	u8			page_shift;
};

struct mlx4_uar {
	unsigned long		pfn;
	int			index;
	struct list_head	bf_list;
	unsigned		free_bf_bmap;
	void __iomem	       *map;
	void __iomem	       *bf_map;
};

struct mlx4_bf {
	unsigned long		offset;
	int			buf_size;
	struct mlx4_uar	       *uar;
	void __iomem	       *reg;
};

struct mlx4_cq {
	void (*comp)		(struct mlx4_cq *);
	void (*event)		(struct mlx4_cq *, enum mlx4_event);

	struct mlx4_uar	       *uar;

	u32			cons_index;

	__be32		       *set_ci_db;
	__be32		       *arm_db;
	int			arm_sn;

	int			cqn;
	unsigned		vector;

	atomic_t		refcount;
	struct completion	free;
};

struct mlx4_qp {
	void (*event)		(struct mlx4_qp *, enum mlx4_event);

	int			qpn;

	atomic_t		refcount;
	struct completion	free;
};

struct mlx4_srq {
	void (*event)		(struct mlx4_srq *, enum mlx4_event);

	int			srqn;
	int			max;
	int			max_gs;
	int			wqe_shift;

	atomic_t		refcount;
	struct completion	free;
};

struct mlx4_av {
	__be32			port_pd;
	u8			reserved1;
	u8			g_slid;
	__be16			dlid;
	u8			reserved2;
	u8			gid_index;
	u8			stat_rate;
	u8			hop_limit;
	__be32			sl_tclass_flowlabel;
	u8			dgid[16];
};

struct mlx4_eth_av {
	__be32		port_pd;
	u8		reserved1;
	u8		smac_idx;
	u16		reserved2;
	u8		reserved3;
	u8		gid_index;
	u8		stat_rate;
	u8		hop_limit;
	__be32		sl_tclass_flowlabel;
	u8		dgid[16];
	u32		reserved4[2];
	__be16		vlan;
	u8		mac[6];
};

union mlx4_ext_av {
	struct mlx4_av		ib;
	struct mlx4_eth_av	eth;
};

struct mlx4_counter {
	u8	reserved1[3];
	u8	counter_mode;
	__be32	num_ifc;
	u32	reserved2[2];
	__be64	rx_frames;
	__be64	rx_bytes;
	__be64	tx_frames;
	__be64	tx_bytes;
};

struct mlx4_dev {
	struct pci_dev	       *pdev;
	unsigned long		flags;
	unsigned long		num_slaves;
	struct mlx4_caps	caps;
	struct radix_tree_root	qp_table_tree;
	u8			rev_id;
	char			board_id[MLX4_BOARD_ID_LEN];
	int			num_vfs;
};

struct mlx4_init_port_param {
	int			set_guid0;
	int			set_node_guid;
	int			set_si_guid;
	u16			mtu;
	int			port_width_cap;
	u16			vl_cap;
	u16			max_gid;
	u16			max_pkey;
	u64			guid0;
	u64			node_guid;
	u64			si_guid;
};

#define mlx4_foreach_port(port, dev, type)				\
	for ((port) = 1; (port) <= (dev)->caps.num_ports; (port)++)	\
		if ((type) == (dev)->caps.port_mask[(port)])

#define mlx4_foreach_ib_transport_port(port, dev)                         \
	for ((port) = 1; (port) <= (dev)->caps.num_ports; (port)++)	  \
		if (((dev)->caps.port_mask[port] == MLX4_PORT_TYPE_IB) || \
			((dev)->caps.flags & MLX4_DEV_CAP_FLAG_IBOE))

static inline int mlx4_is_master(struct mlx4_dev *dev)
{
	return dev->flags & MLX4_FLAG_MASTER;
}

static inline int mlx4_is_qp_reserved(struct mlx4_dev *dev, u32 qpn)
{
	return (qpn < dev->caps.sqp_start + 8);
}

static inline int mlx4_is_mfunc(struct mlx4_dev *dev)
{
	return dev->flags & (MLX4_FLAG_SLAVE | MLX4_FLAG_MASTER);
}

static inline int mlx4_is_slave(struct mlx4_dev *dev)
{
	return dev->flags & MLX4_FLAG_SLAVE;
}

int mlx4_buf_alloc(struct mlx4_dev *dev, int size, int max_direct,
		   struct mlx4_buf *buf);
void mlx4_buf_free(struct mlx4_dev *dev, int size, struct mlx4_buf *buf);
static inline void *mlx4_buf_offset(struct mlx4_buf *buf, int offset)
{
	if (BITS_PER_LONG == 64 || buf->nbufs == 1)
		return buf->direct.buf + offset;
	else
		return buf->page_list[offset >> PAGE_SHIFT].buf +
			(offset & (PAGE_SIZE - 1));
}

int mlx4_pd_alloc(struct mlx4_dev *dev, u32 *pdn);
void mlx4_pd_free(struct mlx4_dev *dev, u32 pdn);
int mlx4_xrcd_alloc(struct mlx4_dev *dev, u32 *xrcdn);
void mlx4_xrcd_free(struct mlx4_dev *dev, u32 xrcdn);

int mlx4_uar_alloc(struct mlx4_dev *dev, struct mlx4_uar *uar);
void mlx4_uar_free(struct mlx4_dev *dev, struct mlx4_uar *uar);
int mlx4_bf_alloc(struct mlx4_dev *dev, struct mlx4_bf *bf);
void mlx4_bf_free(struct mlx4_dev *dev, struct mlx4_bf *bf);

int mlx4_mtt_init(struct mlx4_dev *dev, int npages, int page_shift,
		  struct mlx4_mtt *mtt);
void mlx4_mtt_cleanup(struct mlx4_dev *dev, struct mlx4_mtt *mtt);
u64 mlx4_mtt_addr(struct mlx4_dev *dev, struct mlx4_mtt *mtt);

int mlx4_mr_alloc(struct mlx4_dev *dev, u32 pd, u64 iova, u64 size, u32 access,
		  int npages, int page_shift, struct mlx4_mr *mr);
void mlx4_mr_free(struct mlx4_dev *dev, struct mlx4_mr *mr);
int mlx4_mr_enable(struct mlx4_dev *dev, struct mlx4_mr *mr);
int mlx4_write_mtt(struct mlx4_dev *dev, struct mlx4_mtt *mtt,
		   int start_index, int npages, u64 *page_list);
int mlx4_buf_write_mtt(struct mlx4_dev *dev, struct mlx4_mtt *mtt,
		       struct mlx4_buf *buf);

int mlx4_db_alloc(struct mlx4_dev *dev, struct mlx4_db *db, int order);
void mlx4_db_free(struct mlx4_dev *dev, struct mlx4_db *db);

int mlx4_alloc_hwq_res(struct mlx4_dev *dev, struct mlx4_hwq_resources *wqres,
		       int size, int max_direct);
void mlx4_free_hwq_res(struct mlx4_dev *mdev, struct mlx4_hwq_resources *wqres,
		       int size);

int mlx4_cq_alloc(struct mlx4_dev *dev, int nent, struct mlx4_mtt *mtt,
		  struct mlx4_uar *uar, u64 db_rec, struct mlx4_cq *cq,
		  unsigned vector, int collapsed);
void mlx4_cq_free(struct mlx4_dev *dev, struct mlx4_cq *cq);

int mlx4_qp_reserve_range(struct mlx4_dev *dev, int cnt, int align, int *base);
void mlx4_qp_release_range(struct mlx4_dev *dev, int base_qpn, int cnt);

int mlx4_qp_alloc(struct mlx4_dev *dev, int qpn, struct mlx4_qp *qp);
void mlx4_qp_free(struct mlx4_dev *dev, struct mlx4_qp *qp);

int mlx4_srq_alloc(struct mlx4_dev *dev, u32 pdn, u32 cqn, u16 xrcdn,
		   struct mlx4_mtt *mtt, u64 db_rec, struct mlx4_srq *srq);
void mlx4_srq_free(struct mlx4_dev *dev, struct mlx4_srq *srq);
int mlx4_srq_arm(struct mlx4_dev *dev, struct mlx4_srq *srq, int limit_watermark);
int mlx4_srq_query(struct mlx4_dev *dev, struct mlx4_srq *srq, int *limit_watermark);

int mlx4_INIT_PORT(struct mlx4_dev *dev, int port);
int mlx4_CLOSE_PORT(struct mlx4_dev *dev, int port);

int mlx4_unicast_attach(struct mlx4_dev *dev, struct mlx4_qp *qp, u8 gid[16],
			int block_mcast_loopback, enum mlx4_protocol prot);
int mlx4_unicast_detach(struct mlx4_dev *dev, struct mlx4_qp *qp, u8 gid[16],
			enum mlx4_protocol prot);
int mlx4_multicast_attach(struct mlx4_dev *dev, struct mlx4_qp *qp, u8 gid[16],
			  int block_mcast_loopback, enum mlx4_protocol protocol);
int mlx4_multicast_detach(struct mlx4_dev *dev, struct mlx4_qp *qp, u8 gid[16],
			  enum mlx4_protocol protocol);
int mlx4_multicast_promisc_add(struct mlx4_dev *dev, u32 qpn, u8 port);
int mlx4_multicast_promisc_remove(struct mlx4_dev *dev, u32 qpn, u8 port);
int mlx4_unicast_promisc_add(struct mlx4_dev *dev, u32 qpn, u8 port);
int mlx4_unicast_promisc_remove(struct mlx4_dev *dev, u32 qpn, u8 port);
int mlx4_SET_MCAST_FLTR(struct mlx4_dev *dev, u8 port, u64 mac, u64 clear, u8 mode);

int mlx4_register_mac(struct mlx4_dev *dev, u8 port, u64 mac);
void mlx4_unregister_mac(struct mlx4_dev *dev, u8 port, u64 mac);
int mlx4_replace_mac(struct mlx4_dev *dev, u8 port, int qpn, u64 new_mac);
int mlx4_get_eth_qp(struct mlx4_dev *dev, u8 port, u64 mac, int *qpn);
void mlx4_put_eth_qp(struct mlx4_dev *dev, u8 port, u64 mac, int qpn);
void mlx4_set_stats_bitmap(struct mlx4_dev *dev, u64 *stats_bitmap);
int mlx4_SET_PORT_general(struct mlx4_dev *dev, u8 port, int mtu,
			  u8 pptx, u8 pfctx, u8 pprx, u8 pfcrx);
int mlx4_SET_PORT_qpn_calc(struct mlx4_dev *dev, u8 port, u32 base_qpn,
			   u8 promisc);
int mlx4_find_cached_vlan(struct mlx4_dev *dev, u8 port, u16 vid, int *idx);
int mlx4_register_vlan(struct mlx4_dev *dev, u8 port, u16 vlan, int *index);
void mlx4_unregister_vlan(struct mlx4_dev *dev, u8 port, int index);

int mlx4_map_phys_fmr(struct mlx4_dev *dev, struct mlx4_fmr *fmr, u64 *page_list,
		      int npages, u64 iova, u32 *lkey, u32 *rkey);
int mlx4_fmr_alloc(struct mlx4_dev *dev, u32 pd, u32 access, int max_pages,
		   int max_maps, u8 page_shift, struct mlx4_fmr *fmr);
int mlx4_fmr_enable(struct mlx4_dev *dev, struct mlx4_fmr *fmr);
void mlx4_fmr_unmap(struct mlx4_dev *dev, struct mlx4_fmr *fmr,
		    u32 *lkey, u32 *rkey);
int mlx4_fmr_free(struct mlx4_dev *dev, struct mlx4_fmr *fmr);
int mlx4_SYNC_TPT(struct mlx4_dev *dev);
int mlx4_test_interrupts(struct mlx4_dev *dev);
int mlx4_assign_eq(struct mlx4_dev *dev, char* name , int* vector);
void mlx4_release_eq(struct mlx4_dev *dev, int vec);

int mlx4_wol_read(struct mlx4_dev *dev, u64 *config, int port);
int mlx4_wol_write(struct mlx4_dev *dev, u64 config, int port);

int mlx4_counter_alloc(struct mlx4_dev *dev, u32 *idx);
void mlx4_counter_free(struct mlx4_dev *dev, u32 idx);

#endif /* MLX4_DEVICE_H */
