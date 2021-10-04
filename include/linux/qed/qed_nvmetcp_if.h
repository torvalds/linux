/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/* Copyright 2021 Marvell. All rights reserved. */

#ifndef _QED_NVMETCP_IF_H
#define _QED_NVMETCP_IF_H
#include <linux/types.h>
#include <linux/qed/qed_if.h>
#include <linux/qed/storage_common.h>
#include <linux/qed/nvmetcp_common.h>

#define QED_NVMETCP_MAX_IO_SIZE	0x800000
#define QED_NVMETCP_CMN_HDR_SIZE (sizeof(struct nvme_tcp_hdr))
#define QED_NVMETCP_CMD_HDR_SIZE (sizeof(struct nvme_tcp_cmd_pdu))
#define QED_NVMETCP_NON_IO_HDR_SIZE ((QED_NVMETCP_CMN_HDR_SIZE + 16))

typedef int (*nvmetcp_event_cb_t) (void *context,
				   u8 fw_event_code, void *fw_handle);

struct qed_dev_nvmetcp_info {
	struct qed_dev_info common;
	u8 port_id;  /* Physical port */
	u8 num_cqs;
};

#define MAX_TID_BLOCKS_NVMETCP (512)
struct qed_nvmetcp_tid {
	u32 size;		/* In bytes per task */
	u32 num_tids_per_block;
	u8 *blocks[MAX_TID_BLOCKS_NVMETCP];
};

struct qed_nvmetcp_id_params {
	u8 mac[ETH_ALEN];
	u32 ip[4];
	u16 port;
};

struct qed_nvmetcp_params_offload {
	/* FW initializations */
	dma_addr_t sq_pbl_addr;
	dma_addr_t nvmetcp_cccid_itid_table_addr;
	u16 nvmetcp_cccid_max_range;
	u8 default_cq;

	/* Networking and TCP stack initializations */
	struct qed_nvmetcp_id_params src;
	struct qed_nvmetcp_id_params dst;
	u32 ka_timeout;
	u32 ka_interval;
	u32 max_rt_time;
	u32 cwnd;
	u16 mss;
	u16 vlan_id;
	bool timestamp_en;
	bool delayed_ack_en;
	bool tcp_keep_alive_en;
	bool ecn_en;
	u8 ip_version;
	u8 ka_max_probe_cnt;
	u8 ttl;
	u8 tos_or_tc;
	u8 rcv_wnd_scale;
};

struct qed_nvmetcp_params_update {
	u32 max_io_size;
	u32 max_recv_pdu_length;
	u32 max_send_pdu_length;

	/* Placeholder: pfv, cpda, hpda */

	bool hdr_digest_en;
	bool data_digest_en;
};

struct qed_nvmetcp_cb_ops {
	struct qed_common_cb_ops common;
};

struct nvmetcp_sge {
	struct regpair sge_addr; /* SGE address */
	__le32 sge_len; /* SGE length */
	__le32 reserved;
};

/* IO path HSI function SGL params */
struct storage_sgl_task_params {
	struct nvmetcp_sge *sgl;
	struct regpair sgl_phys_addr;
	u32 total_buffer_size;
	u16 num_sges;
	bool small_mid_sge;
};

/* IO path HSI function FW task context params */
struct nvmetcp_task_params {
	void *context; /* Output parameter - set/filled by the HSI function */
	struct nvmetcp_wqe *sqe;
	u32 tx_io_size; /* in bytes (Without DIF, if exists) */
	u32 rx_io_size; /* in bytes (Without DIF, if exists) */
	u16 conn_icid;
	u16 itid;
	struct regpair opq; /* qedn_task_ctx address */
	u16 host_cccid;
	u8 cq_rss_number;
	bool send_write_incapsule;
};

/**
 * struct qed_nvmetcp_ops - qed NVMeTCP operations.
 * @common:		common operations pointer
 * @ll2:		light L2 operations pointer
 * @fill_dev_info:	fills NVMeTCP specific information
 *			@param cdev
 *			@param info
 *			@return 0 on success, otherwise error value.
 * @register_ops:	register nvmetcp operations
 *			@param cdev
 *			@param ops - specified using qed_nvmetcp_cb_ops
 *			@param cookie - driver private
 * @start:		nvmetcp in FW
 *			@param cdev
 *			@param tasks - qed will fill information about tasks
 *			return 0 on success, otherwise error value.
 * @stop:		nvmetcp in FW
 *			@param cdev
 *			return 0 on success, otherwise error value.
 * @acquire_conn:	acquire a new nvmetcp connection
 *			@param cdev
 *			@param handle - qed will fill handle that should be
 *				used henceforth as identifier of the
 *				connection.
 *			@param p_doorbell - qed will fill the address of the
 *				doorbell.
 *			@return 0 on sucesss, otherwise error value.
 * @release_conn:	release a previously acquired nvmetcp connection
 *			@param cdev
 *			@param handle - the connection handle.
 *			@return 0 on success, otherwise error value.
 * @offload_conn:	configures an offloaded connection
 *			@param cdev
 *			@param handle - the connection handle.
 *			@param conn_info - the configuration to use for the
 *				offload.
 *			@return 0 on success, otherwise error value.
 * @update_conn:	updates an offloaded connection
 *			@param cdev
 *			@param handle - the connection handle.
 *			@param conn_info - the configuration to use for the
 *				offload.
 *			@return 0 on success, otherwise error value.
 * @destroy_conn:	stops an offloaded connection
 *			@param cdev
 *			@param handle - the connection handle.
 *			@return 0 on success, otherwise error value.
 * @clear_sq:		clear all task in sq
 *			@param cdev
 *			@param handle - the connection handle.
 *			@return 0 on success, otherwise error value.
 * @add_src_tcp_port_filter: Add source tcp port filter
 *			@param cdev
 *			@param src_port
 * @remove_src_tcp_port_filter: Remove source tcp port filter
 *			@param cdev
 *			@param src_port
 * @add_dst_tcp_port_filter: Add destination tcp port filter
 *			@param cdev
 *			@param dest_port
 * @remove_dst_tcp_port_filter: Remove destination tcp port filter
 *			@param cdev
 *			@param dest_port
 * @clear_all_filters: Clear all filters.
 *			@param cdev
 * @init_read_io: Init read IO.
 *			@task_params
 *			@cmd_pdu_header
 *			@nvme_cmd
 *			@sgl_task_params
 * @init_write_io: Init write IO.
 *			@task_params
 *			@cmd_pdu_header
 *			@nvme_cmd
 *			@sgl_task_params
 * @init_icreq_exchange: Exchange ICReq.
 *			@task_params
 *			@init_conn_req_pdu_hdr
 *			@tx_sgl_task_params
 *			@rx_sgl_task_params
 * @init_task_cleanup: Init task cleanup.
 *			@task_params
 */
struct qed_nvmetcp_ops {
	const struct qed_common_ops *common;

	const struct qed_ll2_ops *ll2;

	int (*fill_dev_info)(struct qed_dev *cdev,
			     struct qed_dev_nvmetcp_info *info);

	void (*register_ops)(struct qed_dev *cdev,
			     struct qed_nvmetcp_cb_ops *ops, void *cookie);

	int (*start)(struct qed_dev *cdev,
		     struct qed_nvmetcp_tid *tasks,
		     void *event_context, nvmetcp_event_cb_t async_event_cb);

	int (*stop)(struct qed_dev *cdev);

	int (*acquire_conn)(struct qed_dev *cdev,
			    u32 *handle,
			    u32 *fw_cid, void __iomem **p_doorbell);

	int (*release_conn)(struct qed_dev *cdev, u32 handle);

	int (*offload_conn)(struct qed_dev *cdev,
			    u32 handle,
			    struct qed_nvmetcp_params_offload *conn_info);

	int (*update_conn)(struct qed_dev *cdev,
			   u32 handle,
			   struct qed_nvmetcp_params_update *conn_info);

	int (*destroy_conn)(struct qed_dev *cdev, u32 handle, u8 abrt_conn);

	int (*clear_sq)(struct qed_dev *cdev, u32 handle);

	int (*add_src_tcp_port_filter)(struct qed_dev *cdev, u16 src_port);

	void (*remove_src_tcp_port_filter)(struct qed_dev *cdev, u16 src_port);

	int (*add_dst_tcp_port_filter)(struct qed_dev *cdev, u16 dest_port);

	void (*remove_dst_tcp_port_filter)(struct qed_dev *cdev, u16 dest_port);

	void (*clear_all_filters)(struct qed_dev *cdev);

	void (*init_read_io)(struct nvmetcp_task_params *task_params,
			     struct nvme_tcp_cmd_pdu *cmd_pdu_header,
			     struct nvme_command *nvme_cmd,
			     struct storage_sgl_task_params *sgl_task_params);

	void (*init_write_io)(struct nvmetcp_task_params *task_params,
			      struct nvme_tcp_cmd_pdu *cmd_pdu_header,
			      struct nvme_command *nvme_cmd,
			      struct storage_sgl_task_params *sgl_task_params);

	void (*init_icreq_exchange)(struct nvmetcp_task_params *task_params,
				    struct nvme_tcp_icreq_pdu *init_conn_req_pdu_hdr,
				    struct storage_sgl_task_params *tx_sgl_task_params,
				    struct storage_sgl_task_params *rx_sgl_task_params);

	void (*init_task_cleanup)(struct nvmetcp_task_params *task_params);
};

const struct qed_nvmetcp_ops *qed_get_nvmetcp_ops(void);
void qed_put_nvmetcp_ops(void);
#endif
