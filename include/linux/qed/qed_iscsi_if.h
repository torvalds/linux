/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 * Copyright (c) 2019-2020 Marvell International Ltd.
 */

#ifndef _QED_ISCSI_IF_H
#define _QED_ISCSI_IF_H
#include <linux/types.h>
#include <linux/qed/qed_if.h>

typedef int (*iscsi_event_cb_t) (void *context,
				 u8 fw_event_code, void *fw_handle);
struct qed_iscsi_stats {
	u64 iscsi_rx_bytes_cnt;
	u64 iscsi_rx_packet_cnt;
	u64 iscsi_rx_new_ooo_isle_events_cnt;
	u32 iscsi_cmdq_threshold_cnt;
	u32 iscsi_rq_threshold_cnt;
	u32 iscsi_immq_threshold_cnt;

	u64 iscsi_rx_dropped_pdus_task_not_valid;

	u64 iscsi_rx_data_pdu_cnt;
	u64 iscsi_rx_r2t_pdu_cnt;
	u64 iscsi_rx_total_pdu_cnt;

	u64 iscsi_tx_go_to_slow_start_event_cnt;
	u64 iscsi_tx_fast_retransmit_event_cnt;

	u64 iscsi_tx_data_pdu_cnt;
	u64 iscsi_tx_r2t_pdu_cnt;
	u64 iscsi_tx_total_pdu_cnt;

	u64 iscsi_tx_bytes_cnt;
	u64 iscsi_tx_packet_cnt;
};

struct qed_dev_iscsi_info {
	struct qed_dev_info common;

	void __iomem *primary_dbq_rq_addr;
	void __iomem *secondary_bdq_rq_addr;

	u8 num_cqs;
};

struct qed_iscsi_id_params {
	u8 mac[ETH_ALEN];
	u32 ip[4];
	u16 port;
};

struct qed_iscsi_params_offload {
	u8 layer_code;
	dma_addr_t sq_pbl_addr;
	u32 initial_ack;

	struct qed_iscsi_id_params src;
	struct qed_iscsi_id_params dst;
	u16 vlan_id;
	u8 tcp_flags;
	u8 ip_version;
	u8 default_cq;

	u8 ka_max_probe_cnt;
	u8 dup_ack_theshold;
	u32 rcv_next;
	u32 snd_una;
	u32 snd_next;
	u32 snd_max;
	u32 snd_wnd;
	u32 rcv_wnd;
	u32 snd_wl1;
	u32 cwnd;
	u32 ss_thresh;
	u16 srtt;
	u16 rtt_var;
	u32 ts_recent;
	u32 ts_recent_age;
	u32 total_rt;
	u32 ka_timeout_delta;
	u32 rt_timeout_delta;
	u8 dup_ack_cnt;
	u8 snd_wnd_probe_cnt;
	u8 ka_probe_cnt;
	u8 rt_cnt;
	u32 flow_label;
	u32 ka_timeout;
	u32 ka_interval;
	u32 max_rt_time;
	u32 initial_rcv_wnd;
	u8 ttl;
	u8 tos_or_tc;
	u16 remote_port;
	u16 local_port;
	u16 mss;
	u8 snd_wnd_scale;
	u8 rcv_wnd_scale;
	u16 da_timeout_value;
	u8 ack_frequency;
};

struct qed_iscsi_params_update {
	u8 update_flag;
#define QED_ISCSI_CONN_HD_EN            BIT(0)
#define QED_ISCSI_CONN_DD_EN            BIT(1)
#define QED_ISCSI_CONN_INITIAL_R2T      BIT(2)
#define QED_ISCSI_CONN_IMMEDIATE_DATA   BIT(3)

	u32 max_seq_size;
	u32 max_recv_pdu_length;
	u32 max_send_pdu_length;
	u32 first_seq_length;
	u32 exp_stat_sn;
};

#define MAX_TID_BLOCKS_ISCSI (512)
struct qed_iscsi_tid {
	u32 size;		/* In bytes per task */
	u32 num_tids_per_block;
	u8 *blocks[MAX_TID_BLOCKS_ISCSI];
};

struct qed_iscsi_cb_ops {
	struct qed_common_cb_ops common;
};

/**
 * struct qed_iscsi_ops - qed iSCSI operations.
 * @common:		common operations pointer
 * @ll2:		light L2 operations pointer
 * @fill_dev_info:	fills iSCSI specific information
 *			@param cdev
 *			@param info
 *			@return 0 on sucesss, otherwise error value.
 * @register_ops:	register iscsi operations
 *			@param cdev
 *			@param ops - specified using qed_iscsi_cb_ops
 *			@param cookie - driver private
 * @start:		iscsi in FW
 *			@param cdev
 *			@param tasks - qed will fill information about tasks
 *			return 0 on success, otherwise error value.
 * @stop:		iscsi in FW
 *			@param cdev
 *			return 0 on success, otherwise error value.
 * @acquire_conn:	acquire a new iscsi connection
 *			@param cdev
 *			@param handle - qed will fill handle that should be
 *				used henceforth as identifier of the
 *				connection.
 *			@param p_doorbell - qed will fill the address of the
 *				doorbell.
 *			@return 0 on sucesss, otherwise error value.
 * @release_conn:	release a previously acquired iscsi connection
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
 * @get_stats:		iSCSI related statistics
 *			@param cdev
 *			@param stats - pointer to struck that would be filled
 *				we stats
 *			@return 0 on success, error otherwise.
 * @change_mac		Change MAC of interface
 *			@param cdev
 *			@param handle - the connection handle.
 *			@param mac - new MAC to configure.
 *			@return 0 on success, otherwise error value.
 */
struct qed_iscsi_ops {
	const struct qed_common_ops *common;

	const struct qed_ll2_ops *ll2;

	int (*fill_dev_info)(struct qed_dev *cdev,
			     struct qed_dev_iscsi_info *info);

	void (*register_ops)(struct qed_dev *cdev,
			     struct qed_iscsi_cb_ops *ops, void *cookie);

	int (*start)(struct qed_dev *cdev,
		     struct qed_iscsi_tid *tasks,
		     void *event_context, iscsi_event_cb_t async_event_cb);

	int (*stop)(struct qed_dev *cdev);

	int (*acquire_conn)(struct qed_dev *cdev,
			    u32 *handle,
			    u32 *fw_cid, void __iomem **p_doorbell);

	int (*release_conn)(struct qed_dev *cdev, u32 handle);

	int (*offload_conn)(struct qed_dev *cdev,
			    u32 handle,
			    struct qed_iscsi_params_offload *conn_info);

	int (*update_conn)(struct qed_dev *cdev,
			   u32 handle,
			   struct qed_iscsi_params_update *conn_info);

	int (*destroy_conn)(struct qed_dev *cdev, u32 handle, u8 abrt_conn);

	int (*clear_sq)(struct qed_dev *cdev, u32 handle);

	int (*get_stats)(struct qed_dev *cdev,
			 struct qed_iscsi_stats *stats);

	int (*change_mac)(struct qed_dev *cdev, u32 handle, const u8 *mac);
};

const struct qed_iscsi_ops *qed_get_iscsi_ops(void);
void qed_put_iscsi_ops(void);
#endif
