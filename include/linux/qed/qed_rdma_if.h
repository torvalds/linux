/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 * Copyright (c) 2019-2020 Marvell International Ltd.
 */

#ifndef _QED_RDMA_IF_H
#define _QED_RDMA_IF_H
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/qed/qed_if.h>
#include <linux/qed/qed_ll2_if.h>
#include <linux/qed/rdma_common.h>

#define QED_RDMA_MAX_CNQ_SIZE               (0xFFFF)

/* rdma interface */

enum qed_roce_qp_state {
	QED_ROCE_QP_STATE_RESET,
	QED_ROCE_QP_STATE_INIT,
	QED_ROCE_QP_STATE_RTR,
	QED_ROCE_QP_STATE_RTS,
	QED_ROCE_QP_STATE_SQD,
	QED_ROCE_QP_STATE_ERR,
	QED_ROCE_QP_STATE_SQE
};

enum qed_rdma_qp_type {
	QED_RDMA_QP_TYPE_RC,
	QED_RDMA_QP_TYPE_XRC_INI,
	QED_RDMA_QP_TYPE_XRC_TGT,
	QED_RDMA_QP_TYPE_INVAL = 0xffff,
};

enum qed_rdma_tid_type {
	QED_RDMA_TID_REGISTERED_MR,
	QED_RDMA_TID_FMR,
	QED_RDMA_TID_MW
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
	void __iomem *dpi_addr;
	u64 dpi_phys_addr;
	u32 dpi_size;
	u16 wid_count;
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
	u64 length;
	u64 vaddr;
	bool phy_mr;
	bool dma_mr;

	bool dif_enabled;
	u64 dif_error_addr;
};

struct qed_rdma_create_cq_in_params {
	u32 cq_handle_lo;
	u32 cq_handle_hi;
	u32 cq_size;
	u16 dpi;
	bool pbl_two_level;
	u64 pbl_ptr;
	u16 pbl_num_pages;
	u8 pbl_page_size_log;
	u8 cnq_id;
	u16 int_timeout;
};

struct qed_rdma_create_srq_in_params {
	u64 pbl_base_addr;
	u64 prod_pair_addr;
	u16 num_pages;
	u16 pd_id;
	u16 page_size;

	/* XRC related only */
	bool reserved_key_en;
	bool is_xrc;
	u32 cq_cid;
	u16 xrcd_id;
};

struct qed_rdma_destroy_cq_in_params {
	u16 icid;
};

struct qed_rdma_destroy_cq_out_params {
	u16 num_cq_notif;
};

struct qed_rdma_create_qp_in_params {
	u32 qp_handle_lo;
	u32 qp_handle_hi;
	u32 qp_handle_async_lo;
	u32 qp_handle_async_hi;
	bool use_srq;
	bool signal_all;
	bool fmr_and_reserved_lkey;
	u16 pd;
	u16 dpi;
	u16 sq_cq_id;
	u16 sq_num_pages;
	u64 sq_pbl_ptr;
	u8 max_sq_sges;
	u16 rq_cq_id;
	u16 rq_num_pages;
	u64 rq_pbl_ptr;
	u16 srq_id;
	u16 xrcd_id;
	u8 stats_queue;
	enum qed_rdma_qp_type qp_type;
	u8 flags;
#define QED_ROCE_EDPM_MODE_MASK      0x1
#define QED_ROCE_EDPM_MODE_SHIFT     0
};

struct qed_rdma_create_qp_out_params {
	u32 qp_id;
	u16 icid;
	void *rq_pbl_virt;
	dma_addr_t rq_pbl_phys;
	void *sq_pbl_virt;
	dma_addr_t sq_pbl_phys;
};

struct qed_rdma_modify_qp_in_params {
	u32 modify_flags;
#define QED_RDMA_MODIFY_QP_VALID_NEW_STATE_MASK               0x1
#define QED_RDMA_MODIFY_QP_VALID_NEW_STATE_SHIFT              0
#define QED_ROCE_MODIFY_QP_VALID_PKEY_MASK                    0x1
#define QED_ROCE_MODIFY_QP_VALID_PKEY_SHIFT                   1
#define QED_RDMA_MODIFY_QP_VALID_RDMA_OPS_EN_MASK             0x1
#define QED_RDMA_MODIFY_QP_VALID_RDMA_OPS_EN_SHIFT            2
#define QED_ROCE_MODIFY_QP_VALID_DEST_QP_MASK                 0x1
#define QED_ROCE_MODIFY_QP_VALID_DEST_QP_SHIFT                3
#define QED_ROCE_MODIFY_QP_VALID_ADDRESS_VECTOR_MASK          0x1
#define QED_ROCE_MODIFY_QP_VALID_ADDRESS_VECTOR_SHIFT         4
#define QED_ROCE_MODIFY_QP_VALID_RQ_PSN_MASK                  0x1
#define QED_ROCE_MODIFY_QP_VALID_RQ_PSN_SHIFT                 5
#define QED_ROCE_MODIFY_QP_VALID_SQ_PSN_MASK                  0x1
#define QED_ROCE_MODIFY_QP_VALID_SQ_PSN_SHIFT                 6
#define QED_RDMA_MODIFY_QP_VALID_MAX_RD_ATOMIC_REQ_MASK       0x1
#define QED_RDMA_MODIFY_QP_VALID_MAX_RD_ATOMIC_REQ_SHIFT      7
#define QED_RDMA_MODIFY_QP_VALID_MAX_RD_ATOMIC_RESP_MASK      0x1
#define QED_RDMA_MODIFY_QP_VALID_MAX_RD_ATOMIC_RESP_SHIFT     8
#define QED_ROCE_MODIFY_QP_VALID_ACK_TIMEOUT_MASK             0x1
#define QED_ROCE_MODIFY_QP_VALID_ACK_TIMEOUT_SHIFT            9
#define QED_ROCE_MODIFY_QP_VALID_RETRY_CNT_MASK               0x1
#define QED_ROCE_MODIFY_QP_VALID_RETRY_CNT_SHIFT              10
#define QED_ROCE_MODIFY_QP_VALID_RNR_RETRY_CNT_MASK           0x1
#define QED_ROCE_MODIFY_QP_VALID_RNR_RETRY_CNT_SHIFT          11
#define QED_ROCE_MODIFY_QP_VALID_MIN_RNR_NAK_TIMER_MASK       0x1
#define QED_ROCE_MODIFY_QP_VALID_MIN_RNR_NAK_TIMER_SHIFT      12
#define QED_ROCE_MODIFY_QP_VALID_E2E_FLOW_CONTROL_EN_MASK     0x1
#define QED_ROCE_MODIFY_QP_VALID_E2E_FLOW_CONTROL_EN_SHIFT    13
#define QED_ROCE_MODIFY_QP_VALID_ROCE_MODE_MASK               0x1
#define QED_ROCE_MODIFY_QP_VALID_ROCE_MODE_SHIFT              14

	enum qed_roce_qp_state new_state;
	u16 pkey;
	bool incoming_rdma_read_en;
	bool incoming_rdma_write_en;
	bool incoming_atomic_en;
	bool e2e_flow_control_en;
	u32 dest_qp;
	bool lb_indication;
	u16 mtu;
	u8 traffic_class_tos;
	u8 hop_limit_ttl;
	u32 flow_label;
	union qed_gid sgid;
	union qed_gid dgid;
	u16 udp_src_port;

	u16 vlan_id;

	u32 rq_psn;
	u32 sq_psn;
	u8 max_rd_atomic_resp;
	u8 max_rd_atomic_req;
	u32 ack_timeout;
	u8 retry_cnt;
	u8 rnr_retry_cnt;
	u8 min_rnr_nak_timer;
	bool sqd_async;
	u8 remote_mac_addr[6];
	u8 local_mac_addr[6];
	bool use_local_mac;
	enum roce_mode roce_mode;
};

struct qed_rdma_query_qp_out_params {
	enum qed_roce_qp_state state;
	u32 rq_psn;
	u32 sq_psn;
	bool draining;
	u16 mtu;
	u32 dest_qp;
	bool incoming_rdma_read_en;
	bool incoming_rdma_write_en;
	bool incoming_atomic_en;
	bool e2e_flow_control_en;
	union qed_gid sgid;
	union qed_gid dgid;
	u32 flow_label;
	u8 hop_limit_ttl;
	u8 traffic_class_tos;
	u32 timeout;
	u8 rnr_retry;
	u8 retry_cnt;
	u8 min_rnr_nak_timer;
	u16 pkey_index;
	u8 max_rd_atomic;
	u8 max_dest_rd_atomic;
	bool sqd_async;
};

struct qed_rdma_create_srq_out_params {
	u16 srq_id;
};

struct qed_rdma_destroy_srq_in_params {
	u16 srq_id;
	bool is_xrc;
};

struct qed_rdma_modify_srq_in_params {
	u32 wqe_limit;
	u16 srq_id;
	bool is_xrc;
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

enum qed_iwarp_event_type {
	QED_IWARP_EVENT_MPA_REQUEST,	  /* Passive side request received */
	QED_IWARP_EVENT_PASSIVE_COMPLETE, /* ack on mpa response */
	QED_IWARP_EVENT_ACTIVE_COMPLETE,  /* Active side reply received */
	QED_IWARP_EVENT_DISCONNECT,
	QED_IWARP_EVENT_CLOSE,
	QED_IWARP_EVENT_IRQ_FULL,
	QED_IWARP_EVENT_RQ_EMPTY,
	QED_IWARP_EVENT_LLP_TIMEOUT,
	QED_IWARP_EVENT_REMOTE_PROTECTION_ERROR,
	QED_IWARP_EVENT_CQ_OVERFLOW,
	QED_IWARP_EVENT_QP_CATASTROPHIC,
	QED_IWARP_EVENT_ACTIVE_MPA_REPLY,
	QED_IWARP_EVENT_LOCAL_ACCESS_ERROR,
	QED_IWARP_EVENT_REMOTE_OPERATION_ERROR,
	QED_IWARP_EVENT_TERMINATE_RECEIVED,
	QED_IWARP_EVENT_SRQ_LIMIT,
	QED_IWARP_EVENT_SRQ_EMPTY,
};

enum qed_tcp_ip_version {
	QED_TCP_IPV4,
	QED_TCP_IPV6,
};

struct qed_iwarp_cm_info {
	enum qed_tcp_ip_version ip_version;
	u32 remote_ip[4];
	u32 local_ip[4];
	u16 remote_port;
	u16 local_port;
	u16 vlan;
	u8 ord;
	u8 ird;
	u16 private_data_len;
	const void *private_data;
};

struct qed_iwarp_cm_event_params {
	enum qed_iwarp_event_type event;
	const struct qed_iwarp_cm_info *cm_info;
	void *ep_context;	/* To be passed to accept call */
	int status;
};

typedef int (*iwarp_event_handler) (void *context,
				    struct qed_iwarp_cm_event_params *event);

struct qed_iwarp_connect_in {
	iwarp_event_handler event_cb;
	void *cb_context;
	struct qed_rdma_qp *qp;
	struct qed_iwarp_cm_info cm_info;
	u16 mss;
	u8 remote_mac_addr[ETH_ALEN];
	u8 local_mac_addr[ETH_ALEN];
};

struct qed_iwarp_connect_out {
	void *ep_context;
};

struct qed_iwarp_listen_in {
	iwarp_event_handler event_cb;
	void *cb_context;	/* passed to event_cb */
	u32 max_backlog;
	enum qed_tcp_ip_version ip_version;
	u32 ip_addr[4];
	u16 port;
	u16 vlan;
};

struct qed_iwarp_listen_out {
	void *handle;
};

struct qed_iwarp_accept_in {
	void *ep_context;
	void *cb_context;
	struct qed_rdma_qp *qp;
	const void *private_data;
	u16 private_data_len;
	u8 ord;
	u8 ird;
};

struct qed_iwarp_reject_in {
	void *ep_context;
	void *cb_context;
	const void *private_data;
	u16 private_data_len;
};

struct qed_iwarp_send_rtr_in {
	void *ep_context;
};

struct qed_roce_ll2_header {
	void *vaddr;
	dma_addr_t baddr;
	size_t len;
};

struct qed_roce_ll2_buffer {
	dma_addr_t baddr;
	size_t len;
};

struct qed_roce_ll2_packet {
	struct qed_roce_ll2_header header;
	int n_seg;
	struct qed_roce_ll2_buffer payload[RDMA_MAX_SGE_PER_SQ_WQE];
	int roce_mode;
	enum qed_ll2_tx_dest tx_dest;
};

enum qed_rdma_type {
	QED_RDMA_TYPE_ROCE,
	QED_RDMA_TYPE_IWARP
};

struct qed_dev_rdma_info {
	struct qed_dev_info common;
	enum qed_rdma_type rdma_type;
	u8 user_dpm_enabled;
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
	struct qed_rdma_port* (*rdma_query_port)(void *rdma_cxt);
	int (*rdma_get_start_sb)(struct qed_dev *cdev);
	int (*rdma_get_min_cnq_msix)(struct qed_dev *cdev);
	void (*rdma_cnq_prod_update)(void *rdma_cxt, u8 cnq_index, u16 prod);
	int (*rdma_get_rdma_int)(struct qed_dev *cdev,
				 struct qed_int_info *info);
	int (*rdma_set_rdma_int)(struct qed_dev *cdev, u16 cnt);
	int (*rdma_alloc_pd)(void *rdma_cxt, u16 *pd);
	void (*rdma_dealloc_pd)(void *rdma_cxt, u16 pd);
	int (*rdma_alloc_xrcd)(void *rdma_cxt, u16 *xrcd);
	void (*rdma_dealloc_xrcd)(void *rdma_cxt, u16 xrcd);
	int (*rdma_create_cq)(void *rdma_cxt,
			      struct qed_rdma_create_cq_in_params *params,
			      u16 *icid);
	int (*rdma_destroy_cq)(void *rdma_cxt,
			       struct qed_rdma_destroy_cq_in_params *iparams,
			       struct qed_rdma_destroy_cq_out_params *oparams);
	struct qed_rdma_qp *
	(*rdma_create_qp)(void *rdma_cxt,
			  struct qed_rdma_create_qp_in_params *iparams,
			  struct qed_rdma_create_qp_out_params *oparams);

	int (*rdma_modify_qp)(void *roce_cxt, struct qed_rdma_qp *qp,
			      struct qed_rdma_modify_qp_in_params *iparams);

	int (*rdma_query_qp)(void *rdma_cxt, struct qed_rdma_qp *qp,
			     struct qed_rdma_query_qp_out_params *oparams);
	int (*rdma_destroy_qp)(void *rdma_cxt, struct qed_rdma_qp *qp);

	int
	(*rdma_register_tid)(void *rdma_cxt,
			     struct qed_rdma_register_tid_in_params *iparams);

	int (*rdma_deregister_tid)(void *rdma_cxt, u32 itid);
	int (*rdma_alloc_tid)(void *rdma_cxt, u32 *itid);
	void (*rdma_free_tid)(void *rdma_cxt, u32 itid);

	int (*rdma_create_srq)(void *rdma_cxt,
			       struct qed_rdma_create_srq_in_params *iparams,
			       struct qed_rdma_create_srq_out_params *oparams);
	int (*rdma_destroy_srq)(void *rdma_cxt,
				struct qed_rdma_destroy_srq_in_params *iparams);
	int (*rdma_modify_srq)(void *rdma_cxt,
			       struct qed_rdma_modify_srq_in_params *iparams);

	int (*ll2_acquire_connection)(void *rdma_cxt,
				      struct qed_ll2_acquire_data *data);

	int (*ll2_establish_connection)(void *rdma_cxt, u8 connection_handle);
	int (*ll2_terminate_connection)(void *rdma_cxt, u8 connection_handle);
	void (*ll2_release_connection)(void *rdma_cxt, u8 connection_handle);

	int (*ll2_prepare_tx_packet)(void *rdma_cxt,
				     u8 connection_handle,
				     struct qed_ll2_tx_pkt_info *pkt,
				     bool notify_fw);

	int (*ll2_set_fragment_of_tx_packet)(void *rdma_cxt,
					     u8 connection_handle,
					     dma_addr_t addr,
					     u16 nbytes);
	int (*ll2_post_rx_buffer)(void *rdma_cxt, u8 connection_handle,
				  dma_addr_t addr, u16 buf_len, void *cookie,
				  u8 notify_fw);
	int (*ll2_get_stats)(void *rdma_cxt,
			     u8 connection_handle,
			     struct qed_ll2_stats *p_stats);
	int (*ll2_set_mac_filter)(struct qed_dev *cdev,
				  u8 *old_mac_address, u8 *new_mac_address);

	int (*iwarp_set_engine_affin)(struct qed_dev *cdev, bool b_reset);

	int (*iwarp_connect)(void *rdma_cxt,
			     struct qed_iwarp_connect_in *iparams,
			     struct qed_iwarp_connect_out *oparams);

	int (*iwarp_create_listen)(void *rdma_cxt,
				   struct qed_iwarp_listen_in *iparams,
				   struct qed_iwarp_listen_out *oparams);

	int (*iwarp_accept)(void *rdma_cxt,
			    struct qed_iwarp_accept_in *iparams);

	int (*iwarp_reject)(void *rdma_cxt,
			    struct qed_iwarp_reject_in *iparams);

	int (*iwarp_destroy_listen)(void *rdma_cxt, void *handle);

	int (*iwarp_send_rtr)(void *rdma_cxt,
			      struct qed_iwarp_send_rtr_in *iparams);
};

const struct qed_rdma_ops *qed_get_rdma_ops(void);

#endif
