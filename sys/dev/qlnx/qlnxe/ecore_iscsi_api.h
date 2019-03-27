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

#ifndef __ECORE_ISCSI_API_H__
#define __ECORE_ISCSI_API_H__

#ifndef __EXTRACT__LINUX__IF__
typedef enum _ecore_status_t (*iscsi_event_cb_t)(void *context,
						 u8 fw_event_code,
						 void *fw_handle);

struct ecore_iscsi_stats
{
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

	u64 iscsi_rx_tcp_payload_bytes_cnt;
	u64 iscsi_rx_tcp_pkt_cnt;
	u64 iscsi_rx_pure_ack_cnt;

	u64 iscsi_rx_dup_ack_cnt;

	u64 iscsi_tx_pure_ack_cnt;
	u64 iscsi_tx_delayed_ack_cnt;

	u64 iscsi_tx_tcp_payload_bytes_cnt;
	u64 iscsi_tx_tcp_pkt_cnt;
};

struct ecore_iscsi_tcp_stats
{
	u64 iscsi_tcp_tx_packets_cnt;
	u64 iscsi_tcp_tx_bytes_cnt;
	u64 iscsi_tcp_tx_rxmit_cnt;
	u64 iscsi_tcp_rx_packets_cnt;
	u64 iscsi_tcp_rx_bytes_cnt;
	u64 iscsi_tcp_rx_dup_ack_cnt;
	u32 iscsi_tcp_rx_chksum_err_cnt;
};
#endif

#ifndef __EXTRACT__LINUX__C__
struct ecore_iscsi_conn {
	osal_list_entry_t	list_entry;
	bool			free_on_delete;

	u16			conn_id;
	u32			icid;
	u32			fw_cid;

	u8			layer_code;
	u8			offl_flags;
	u8			connect_mode;
	u32			initial_ack;
	dma_addr_t		sq_pbl_addr;
	struct ecore_chain	r2tq;
	struct ecore_chain	xhq;
	struct ecore_chain	uhq;

	struct tcp_upload_params *tcp_upload_params_virt_addr;
	dma_addr_t		tcp_upload_params_phys_addr;
	struct iscsi_conn_stats_params *conn_stats_params_virt_addr;
	dma_addr_t		conn_stats_params_phys_addr;
	struct scsi_terminate_extra_params *queue_cnts_virt_addr;
	dma_addr_t		queue_cnts_phys_addr;
	dma_addr_t		syn_phy_addr;

	u16			syn_ip_payload_length;
	u8			local_mac[6];
	u8			remote_mac[6];
	u16			vlan_id;
	u16			tcp_flags;
	u8			ip_version;
	u32			remote_ip[4];
	u32			local_ip[4];
	u8			ka_max_probe_cnt;
	u8			dup_ack_theshold;
	u32			rcv_next;
	u32			snd_una;
	u32			snd_next;
	u32			snd_max;
	u32			snd_wnd;
	u32			rcv_wnd;
	u32			snd_wl1;
	u32			cwnd;
	u32			ss_thresh;
	u16			srtt;
	u16			rtt_var;
	u32			ts_recent;
	u32			ts_recent_age;
	u32			total_rt;
	u32			ka_timeout_delta;
	u32			rt_timeout_delta;
	u8			dup_ack_cnt;
	u8			snd_wnd_probe_cnt;
	u8			ka_probe_cnt;
	u8			rt_cnt;
	u32			flow_label;
	u32			ka_timeout;
	u32			ka_interval;
	u32			max_rt_time;
	u32			initial_rcv_wnd;
	u8			ttl;
	u8			tos_or_tc;
	u16			remote_port;
	u16			local_port;
	u16			mss;
	u8			snd_wnd_scale;
	u8			rcv_wnd_scale;
	u16			da_timeout_value;
	u8			ack_frequency;

	u8			update_flag;
#define	ECORE_ISCSI_CONN_HD_EN		0x01
#define	ECORE_ISCSI_CONN_DD_EN		0x02
#define	ECORE_ISCSI_CONN_INITIAL_R2T	0x04
#define	ECORE_ISCSI_CONN_IMMEDIATE_DATA	0x08

	u8			default_cq;
	u32			max_seq_size;
	u32			max_recv_pdu_length;
	u32			max_send_pdu_length;
	u32			first_seq_length;
	u32			exp_stat_sn;
	u32			stat_sn;
	u16			physical_q0;
	u16			physical_q1;
	u8			abortive_dsconnect;
	u8			dif_on_immediate;
#define ECORE_ISCSI_CONN_DIF_ON_IMM_DIS		0
#define ECORE_ISCSI_CONN_DIF_ON_IMM_DEFAULT	1
#define ECORE_ISCSI_CONN_DIF_ON_IMM_LUN_MAPPER	2

	dma_addr_t		lun_mapper_phys_addr;
	u32			initial_ref_tag;
	u16			application_tag;
	u16			application_tag_mask;
	u8			validate_guard;
	u8			validate_app_tag;
	u8			validate_ref_tag;
	u8			forward_guard;
	u8			forward_app_tag;
	u8			forward_ref_tag;
	u8			interval_size;		/* 0=512B, 1=4KB */
	u8			network_interface;	/* 0=None, 1=DIF */
	u8			host_interface;		/* 0=None, 1=DIF, 2=DIX */
	u8			ref_tag_mask;		/* mask for refernce tag handling */
	u8			forward_app_tag_with_mask;
	u8			forward_ref_tag_with_mask;

	u8			ignore_app_tag;
	u8			initial_ref_tag_is_valid;
	u8			host_guard_type;	/* 0 = IP checksum, 1 = CRC */
	u8			protection_type;	/* 1/2/3 - Protection Type */
	u8			crc_seed;		/* 0=0x0000, 1=0xffff */
	u8			keep_ref_tag_const;
};
#endif

/**
 * @brief ecore_iscsi_acquire_connection - allocate resources, 
 *        provides connecion handle (CID)as out parameter.
 *
 * @param p_path
 * @param p_conn  partially initialized incoming container of 
 *                iSCSI connection data
 * @return enum _ecore_status_t
 */
enum _ecore_status_t
ecore_iscsi_acquire_connection(struct ecore_hwfn *p_hwfn,
			       struct ecore_iscsi_conn *p_in_conn,
			       struct ecore_iscsi_conn **p_out_conn);

/**
 * @brief ecore_iscsi_setup_connection- initialize connection data.
 *
 * @param p_conn  container of iSCSI connection data
 * @return enum _ecore_status_t
 */
enum _ecore_status_t
ecore_iscsi_setup_connection(struct ecore_iscsi_conn *p_conn);

void OSAL_IOMEM *ecore_iscsi_get_db_addr(struct ecore_hwfn *p_hwfn,
					 u32 cid);

void OSAL_IOMEM *ecore_iscsi_get_global_cmdq_cons(struct ecore_hwfn *p_hwfn,
						  u8 relative_q_id);

void OSAL_IOMEM *ecore_iscsi_get_primary_bdq_prod(struct ecore_hwfn *p_hwfn,
						  u8 bdq_id);

void OSAL_IOMEM *ecore_iscsi_get_secondary_bdq_prod(struct ecore_hwfn *p_hwfn,
						    u8 bdq_id);

/**
 * @brief ecore_iscsi_offload_connection - offload previously 
 *        allocated iSCSI connection
 *
 * @param p_path 
 * @param p_conn  container of iSCSI connection data
 *  
 * @return enum _ecore_status_t
 */
enum _ecore_status_t
ecore_iscsi_offload_connection(struct ecore_hwfn *p_hwfn,
			       struct ecore_iscsi_conn *p_conn);

/**
 * @brief ecore_iscsi_release_connection - deletes connecton 
 *        resources (incliding container of iSCSI connection
 *        data)
 *
 * @param p_path 
 * @param p_conn  container of iSCSI connection data
 */
void ecore_iscsi_release_connection(struct ecore_hwfn *p_hwfn,
				    struct ecore_iscsi_conn *p_conn);

/**
 * @brief ecore_iscsi_terminate_connection - destroys previously
 *        offloaded iSCSI connection
 *
 * @param p_path 
 * @param p_conn  container of iSCSI connection data
 *  
 * @return enum _ecore_status_t
 */
enum _ecore_status_t
ecore_iscsi_terminate_connection(struct ecore_hwfn *p_hwfn,
				 struct ecore_iscsi_conn *p_conn);


/**
 * @brief ecore_iscsi_update_connection - updates previously 
 *        offloaded iSCSI connection
 *
 *
 * @param p_path 
 * @param p_conn  container of iSCSI connection data
 *  
 * @return enum _ecore_status_t
 */
enum _ecore_status_t
ecore_iscsi_update_connection(struct ecore_hwfn *p_hwfn,
			      struct ecore_iscsi_conn *p_conn);

/**
 * @brief ecore_iscsi_mac_update_connection - updates remote MAC for previously
 *        offloaded iSCSI connection
 *
 *
 * @param p_path
 * @param p_conn  container of iSCSI connection data
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t
ecore_iscsi_update_remote_mac(struct ecore_hwfn *p_hwfn,
			      struct ecore_iscsi_conn *p_conn);

/**
 * @brief ecore_iscsi_get_tcp_stats - get and optionally reset TCP statistics
 *        of offloaded iSCSI connection
 *
 *
 * @param p_path
 * @param p_conn  container of iSCSI connection data
 * @param p_stats - buffer to place extracted stats
 * @param reset - 1 - for reset stats (after extraction of accumulated
 *                statistics in optionally provided buffer)
 * @return enum _ecore_status_t
 */
enum _ecore_status_t
ecore_iscsi_get_tcp_stats(struct ecore_hwfn *p_hwfn,
			  struct ecore_iscsi_conn *p_conn,
			  struct ecore_iscsi_tcp_stats *p_stats,
			  u8 reset);

/**
 * @brief ecore_iscsi_clear_connection_sq - clear SQ
 *        offloaded iSCSI connection
 *
 *
 * @param p_path
 * @param p_conn  container of iSCSI connection data
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t
ecore_iscsi_clear_connection_sq(struct ecore_hwfn *p_hwfn,
				struct ecore_iscsi_conn *p_conn);

/**
 * @brief ecore_sp_iscsi_func_start
 *
 * This ramrod inits iSCSI functionality in FW
 *
 * @param p_path
 * @param comp_mode
 * @param comp_addr
 *
 * @return enum _ecore_status_t
 */
enum _ecore_status_t
ecore_sp_iscsi_func_start(struct ecore_hwfn *p_hwfn,
			  enum spq_mode comp_mode,
			  struct ecore_spq_comp_cb *p_comp_addr,
			  void *async_event_context,
			  iscsi_event_cb_t async_event_cb);

enum _ecore_status_t
ecore_sp_iscsi_func_stop(struct ecore_hwfn *p_hwfn,
			 enum spq_mode comp_mode,
			 struct ecore_spq_comp_cb *p_comp_addr);

enum _ecore_status_t
ecore_iscsi_get_stats(struct ecore_hwfn *p_hwfn,
		      struct ecore_iscsi_stats *stats);

#endif
