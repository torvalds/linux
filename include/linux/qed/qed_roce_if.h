/* QLogic qed NIC Driver
 * Copyright (c) 2015-2016  QLogic Corporation
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
 *        disclaimer in the documentation and /or other materials
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
#ifndef _QED_ROCE_IF_H
#define _QED_ROCE_IF_H
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/qed/qed_if.h>
#include <linux/qed/qed_ll2_if.h>

#define QED_RDMA_MAX_CNQ_SIZE               (0xFFFF)

/* rdma interface */
enum qed_rdma_tid_type {
	QED_RDMA_TID_REGISTERED_MR,
	QED_RDMA_TID_FMR,
	QED_RDMA_TID_MW_TYPE1,
	QED_RDMA_TID_MW_TYPE2A
};

struct qed_rdma_events {
	void *context;
	void (*affiliated_event)(void *context, u8 fw_event_code,
				 void *fw_handle);
	void (*unaffiliated_event)(void *context, u8 event_code);
};

struct qed_rdma_device {
	u32 vendor_id;
	u32 vendor_part_id;
	u32 hw_ver;
	u64 fw_ver;

	u64 node_guid;
	u64 sys_image_guid;

	u8 max_cnq;
	u8 max_sge;
	u8 max_srq_sge;
	u16 max_inline;
	u32 max_wqe;
	u32 max_srq_wqe;
	u8 max_qp_resp_rd_atomic_resc;
	u8 max_qp_req_rd_atomic_resc;
	u64 max_dev_resp_rd_atomic_resc;
	u32 max_cq;
	u32 max_qp;
	u32 max_srq;
	u32 max_mr;
	u64 max_mr_size;
	u32 max_cqe;
	u32 max_mw;
	u32 max_fmr;
	u32 max_mr_mw_fmr_pbl;
	u64 max_mr_mw_fmr_size;
	u32 max_pd;
	u32 max_ah;
	u8 max_pkey;
	u16 max_srq_wr;
	u8 max_stats_queues;
	u32 dev_caps;

	/* Abilty to support RNR-NAK generation */

#define QED_RDMA_DEV_CAP_RNR_NAK_MASK                           0x1
#define QED_RDMA_DEV_CAP_RNR_NAK_SHIFT                  0
	/* Abilty to support shutdown port */
#define QED_RDMA_DEV_CAP_SHUTDOWN_PORT_MASK                     0x1
#define QED_RDMA_DEV_CAP_SHUTDOWN_PORT_SHIFT                    1
	/* Abilty to support port active event */
#define QED_RDMA_DEV_CAP_PORT_ACTIVE_EVENT_MASK         0x1
#define QED_RDMA_DEV_CAP_PORT_ACTIVE_EVENT_SHIFT                2
	/* Abilty to support port change event */
#define QED_RDMA_DEV_CAP_PORT_CHANGE_EVENT_MASK         0x1
#define QED_RDMA_DEV_CAP_PORT_CHANGE_EVENT_SHIFT                3
	/* Abilty to support system image GUID */
#define QED_RDMA_DEV_CAP_SYS_IMAGE_MASK                 0x1
#define QED_RDMA_DEV_CAP_SYS_IMAGE_SHIFT                        4
	/* Abilty to support bad P_Key counter support */
#define QED_RDMA_DEV_CAP_BAD_PKEY_CNT_MASK                      0x1
#define QED_RDMA_DEV_CAP_BAD_PKEY_CNT_SHIFT                     5
	/* Abilty to support atomic operations */
#define QED_RDMA_DEV_CAP_ATOMIC_OP_MASK                 0x1
#define QED_RDMA_DEV_CAP_ATOMIC_OP_SHIFT                        6
#define QED_RDMA_DEV_CAP_RESIZE_CQ_MASK                 0x1
#define QED_RDMA_DEV_CAP_RESIZE_CQ_SHIFT                        7
	/* Abilty to support modifying the maximum number of
	 * outstanding work requests per QP
	 */
#define QED_RDMA_DEV_CAP_RESIZE_MAX_WR_MASK                     0x1
#define QED_RDMA_DEV_CAP_RESIZE_MAX_WR_SHIFT                    8
	/* Abilty to support automatic path migration */
#define QED_RDMA_DEV_CAP_AUTO_PATH_MIG_MASK                     0x1
#define QED_RDMA_DEV_CAP_AUTO_PATH_MIG_SHIFT                    9
	/* Abilty to support the base memory management extensions */
#define QED_RDMA_DEV_CAP_BASE_MEMORY_EXT_MASK                   0x1
#define QED_RDMA_DEV_CAP_BASE_MEMORY_EXT_SHIFT          10
#define QED_RDMA_DEV_CAP_BASE_QUEUE_EXT_MASK                    0x1
#define QED_RDMA_DEV_CAP_BASE_QUEUE_EXT_SHIFT                   11
	/* Abilty to support multipile page sizes per memory region */
#define QED_RDMA_DEV_CAP_MULTI_PAGE_PER_MR_EXT_MASK             0x1
#define QED_RDMA_DEV_CAP_MULTI_PAGE_PER_MR_EXT_SHIFT            12
	/* Abilty to support block list physical buffer list */
#define QED_RDMA_DEV_CAP_BLOCK_MODE_MASK                        0x1
#define QED_RDMA_DEV_CAP_BLOCK_MODE_SHIFT                       13
	/* Abilty to support zero based virtual addresses */
#define QED_RDMA_DEV_CAP_ZBVA_MASK                              0x1
#define QED_RDMA_DEV_CAP_ZBVA_SHIFT                             14
	/* Abilty to support local invalidate fencing */
#define QED_RDMA_DEV_CAP_LOCAL_INV_FENCE_MASK                   0x1
#define QED_RDMA_DEV_CAP_LOCAL_INV_FENCE_SHIFT          15
	/* Abilty to support Loopback on QP */
#define QED_RDMA_DEV_CAP_LB_INDICATOR_MASK                      0x1
#define QED_RDMA_DEV_CAP_LB_INDICATOR_SHIFT                     16
	u64 page_size_caps;
	u8 dev_ack_delay;
	u32 reserved_lkey;
	u32 bad_pkey_counter;
	struct qed_rdma_events events;
};

enum qed_port_state {
	QED_RDMA_PORT_UP,
	QED_RDMA_PORT_DOWN,
};

enum qed_roce_capability {
	QED_ROCE_V1 = 1 << 0,
	QED_ROCE_V2 = 1 << 1,
};

struct qed_rdma_port {
	enum qed_port_state port_state;
	int link_speed;
	u64 max_msg_size;
	u8 source_gid_table_len;
	void *source_gid_table_ptr;
	u8 pkey_table_len;
	void *pkey_table_ptr;
	u32 pkey_bad_counter;
	enum qed_roce_capability capability;
};

struct qed_rdma_cnq_params {
	u8 num_pbl_pages;
	u64 pbl_ptr;
};

/* The CQ Mode affects the CQ doorbell transaction size.
 * 64/32 bit machines should configure to 32/16 bits respectively.
 */
enum qed_rdma_cq_mode {
	QED_RDMA_CQ_MODE_16_BITS,
	QED_RDMA_CQ_MODE_32_BITS,
};

struct qed_roce_dcqcn_params {
	u8 notification_point;
	u8 reaction_point;

	/* fields for notification point */
	u32 cnp_send_timeout;

	/* fields for reaction point */
	u32 rl_bc_rate;
	u16 rl_max_rate;
	u16 rl_r_ai;
	u16 rl_r_hai;
	u16 dcqcn_g;
	u32 dcqcn_k_us;
	u32 dcqcn_timeout_us;
};

struct qed_rdma_start_in_params {
	struct qed_rdma_events *events;
	struct qed_rdma_cnq_params cnq_pbl_list[128];
	u8 desired_cnq;
	enum qed_rdma_cq_mode cq_mode;
	struct qed_roce_dcqcn_params dcqcn_params;
	u16 max_mtu;
	u8 mac_addr[ETH_ALEN];
	u8 iwarp_flags;
};

struct qed_rdma_add_user_out_params {
	u16 dpi;
	u64 dpi_addr;
	u64 dpi_phys_addr;
	u32 dpi_size;
};

enum roce_mode {
	ROCE_V1,
	ROCE_V2_IPV4,
	ROCE_V2_IPV6,
	MAX_ROCE_MODE
};

union qed_gid {
	u8 bytes[16];
	u16 words[8];
	u32 dwords[4];
	u64 qwords[2];
	u32 ipv4_addr;
};

struct qed_rdma_register_tid_in_params {
	u32 itid;
	enum qed_rdma_tid_type tid_type;
	u8 key;
	u16 pd;
	bool local_read;
	bool local_write;
	bool remote_read;
	bool remote_write;
	bool remote_atomic;
	bool mw_bind;
	u64 pbl_ptr;
	bool pbl_two_level;
	u8 pbl_page_size_log;
	u8 page_size_log;
	u32 fbo;
	u64 length;
	u64 vaddr;
	bool zbva;
	bool phy_mr;
	bool dma_mr;

	bool dif_enabled;
	u64 dif_error_addr;
	u64 dif_runt_addr;
};

struct qed_rdma_create_srq_in_params {
	u64 pbl_base_addr;
	u64 prod_pair_addr;
	u16 num_pages;
	u16 pd_id;
	u16 page_size;
};

struct qed_rdma_create_srq_out_params {
	u16 srq_id;
};

struct qed_rdma_destroy_srq_in_params {
	u16 srq_id;
};

struct qed_rdma_modify_srq_in_params {
	u32 wqe_limit;
	u16 srq_id;
};

struct qed_rdma_stats_out_params {
	u64 sent_bytes;
	u64 sent_pkts;
	u64 rcv_bytes;
	u64 rcv_pkts;
};

struct qed_rdma_counters_out_params {
	u64 pd_count;
	u64 max_pd;
	u64 dpi_count;
	u64 max_dpi;
	u64 cq_count;
	u64 max_cq;
	u64 qp_count;
	u64 max_qp;
	u64 tid_count;
	u64 max_tid;
};

#define QED_ROCE_TX_HEAD_FAILURE        (1)
#define QED_ROCE_TX_FRAG_FAILURE        (2)

enum qed_rdma_type {
	QED_RDMA_TYPE_ROCE,
};

struct qed_dev_rdma_info {
	struct qed_dev_info common;
	enum qed_rdma_type rdma_type;
};

struct qed_rdma_ops {
	const struct qed_common_ops *common;

	int (*fill_dev_info)(struct qed_dev *cdev,
			     struct qed_dev_rdma_info *info);
	void *(*rdma_get_rdma_ctx)(struct qed_dev *cdev);

	int (*rdma_init)(struct qed_dev *dev,
			 struct qed_rdma_start_in_params *iparams);

	int (*rdma_add_user)(void *rdma_cxt,
			     struct qed_rdma_add_user_out_params *oparams);

	void (*rdma_remove_user)(void *rdma_cxt, u16 dpi);
	int (*rdma_stop)(void *rdma_cxt);
	struct qed_rdma_device* (*rdma_query_device)(void *rdma_cxt);
	int (*rdma_get_start_sb)(struct qed_dev *cdev);
	int (*rdma_get_min_cnq_msix)(struct qed_dev *cdev);
	void (*rdma_cnq_prod_update)(void *rdma_cxt, u8 cnq_index, u16 prod);
	int (*rdma_get_rdma_int)(struct qed_dev *cdev,
				 struct qed_int_info *info);
	int (*rdma_set_rdma_int)(struct qed_dev *cdev, u16 cnt);
};

const struct qed_rdma_ops *qed_get_rdma_ops(void);

#endif
