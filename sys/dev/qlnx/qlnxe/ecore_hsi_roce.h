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

#ifndef __ECORE_HSI_ROCE__
#define __ECORE_HSI_ROCE__ 
/************************************************************************/
/* Add include to ecore hsi rdma target for both roce and iwarp ecore driver */
/************************************************************************/
#include "ecore_hsi_rdma.h"
/************************************************************************/
/* Add include to common roce target for both eCore and protocol roce driver */
/************************************************************************/
#include "roce_common.h"

/*
 * The roce storm context of Ystorm
 */
struct ystorm_roce_conn_st_ctx
{
	struct regpair temp[2];
};

/*
 * The roce storm context of Mstorm
 */
struct pstorm_roce_conn_st_ctx
{
	struct regpair temp[16];
};

/*
 * The roce storm context of Xstorm
 */
struct xstorm_roce_conn_st_ctx
{
	struct regpair temp[24];
};

/*
 * The roce storm context of Tstorm
 */
struct tstorm_roce_conn_st_ctx
{
	struct regpair temp[30];
};

/*
 * The roce storm context of Mstorm
 */
struct mstorm_roce_conn_st_ctx
{
	struct regpair temp[6];
};

/*
 * The roce storm context of Ystorm
 */
struct ustorm_roce_conn_st_ctx
{
	struct regpair temp[12];
};

/*
 * roce connection context
 */
struct e4_roce_conn_context
{
	struct ystorm_roce_conn_st_ctx ystorm_st_context /* ystorm storm context */;
	struct regpair ystorm_st_padding[2] /* padding */;
	struct pstorm_roce_conn_st_ctx pstorm_st_context /* pstorm storm context */;
	struct xstorm_roce_conn_st_ctx xstorm_st_context /* xstorm storm context */;
	struct regpair xstorm_st_padding[2] /* padding */;
	struct e4_xstorm_rdma_conn_ag_ctx xstorm_ag_context /* xstorm aggregative context */;
	struct e4_tstorm_rdma_conn_ag_ctx tstorm_ag_context /* tstorm aggregative context */;
	struct timers_context timer_context /* timer context */;
	struct e4_ustorm_rdma_conn_ag_ctx ustorm_ag_context /* ustorm aggregative context */;
	struct tstorm_roce_conn_st_ctx tstorm_st_context /* tstorm storm context */;
	struct mstorm_roce_conn_st_ctx mstorm_st_context /* mstorm storm context */;
	struct ustorm_roce_conn_st_ctx ustorm_st_context /* ustorm storm context */;
	struct regpair ustorm_st_padding[2] /* padding */;
};


/*
 * roce connection context
 */
struct e5_roce_conn_context
{
	struct ystorm_roce_conn_st_ctx ystorm_st_context /* ystorm storm context */;
	struct regpair ystorm_st_padding[2] /* padding */;
	struct pstorm_roce_conn_st_ctx pstorm_st_context /* pstorm storm context */;
	struct xstorm_roce_conn_st_ctx xstorm_st_context /* xstorm storm context */;
	struct regpair xstorm_st_padding[2] /* padding */;
	struct e5_xstorm_rdma_conn_ag_ctx xstorm_ag_context /* xstorm aggregative context */;
	struct e5_tstorm_rdma_conn_ag_ctx tstorm_ag_context /* tstorm aggregative context */;
	struct timers_context timer_context /* timer context */;
	struct e5_ustorm_rdma_conn_ag_ctx ustorm_ag_context /* ustorm aggregative context */;
	struct tstorm_roce_conn_st_ctx tstorm_st_context /* tstorm storm context */;
	struct mstorm_roce_conn_st_ctx mstorm_st_context /* mstorm storm context */;
	struct ustorm_roce_conn_st_ctx ustorm_st_context /* ustorm storm context */;
	struct regpair ustorm_st_padding[2] /* padding */;
};




/*
 * roce create qp requester ramrod data
 */
struct roce_create_qp_req_ramrod_data
{
	__le16 flags;
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_ROCE_FLAVOR_MASK          0x3 /* Use roce_flavor enum */
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_ROCE_FLAVOR_SHIFT         0
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_FMR_AND_RESERVED_EN_MASK  0x1
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_FMR_AND_RESERVED_EN_SHIFT 2
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_SIGNALED_COMP_MASK        0x1
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_SIGNALED_COMP_SHIFT       3
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_PRI_MASK                  0x7
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_PRI_SHIFT                 4
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_XRC_FLAG_MASK             0x1
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_XRC_FLAG_SHIFT            7
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_ERR_RETRY_CNT_MASK        0xF
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_ERR_RETRY_CNT_SHIFT       8
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_RNR_NAK_CNT_MASK          0xF
#define ROCE_CREATE_QP_REQ_RAMROD_DATA_RNR_NAK_CNT_SHIFT         12
	u8 max_ord;
	u8 traffic_class /* In case of RRoCE on IPv4 will be used as TOS */;
	u8 hop_limit /* In case of RRoCE on IPv4 will be used as TTL */;
	u8 orq_num_pages;
	__le16 p_key;
	__le32 flow_label;
	__le32 dst_qp_id;
	__le32 ack_timeout_val;
	__le32 initial_psn;
	__le16 mtu;
	__le16 pd;
	__le16 sq_num_pages;
	__le16 low_latency_phy_queue;
	struct regpair sq_pbl_addr;
	struct regpair orq_pbl_addr;
	__le16 local_mac_addr[3] /* BE order */;
	__le16 remote_mac_addr[3] /* BE order */;
	__le16 vlan_id;
	__le16 udp_src_port /* Only relevant in RRoCE */;
	__le32 src_gid[4] /* BE order. In case of RRoCE on IPv4 the high register will hold the address. Low registers must be zero! */;
	__le32 dst_gid[4] /* BE order. In case of RRoCE on IPv4 the high register will hold the address. Low registers must be zero! */;
	__le32 cq_cid;
	struct regpair qp_handle_for_cqe;
	struct regpair qp_handle_for_async;
	u8 stats_counter_id /* Statistics counter ID to use */;
	u8 reserved3[7];
	__le16 regular_latency_phy_queue;
	__le16 dpi;
};


/*
 * roce create qp responder ramrod data
 */
struct roce_create_qp_resp_ramrod_data
{
	__le32 flags;
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_ROCE_FLAVOR_MASK          0x3 /* Use roce_flavor enum */
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_ROCE_FLAVOR_SHIFT         0
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_RDMA_RD_EN_MASK           0x1
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_RDMA_RD_EN_SHIFT          2
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_RDMA_WR_EN_MASK           0x1
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_RDMA_WR_EN_SHIFT          3
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_ATOMIC_EN_MASK            0x1
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_ATOMIC_EN_SHIFT           4
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_SRQ_FLG_MASK              0x1
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_SRQ_FLG_SHIFT             5
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_E2E_FLOW_CONTROL_EN_MASK  0x1
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_E2E_FLOW_CONTROL_EN_SHIFT 6
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_RESERVED_KEY_EN_MASK      0x1
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_RESERVED_KEY_EN_SHIFT     7
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_PRI_MASK                  0x7
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_PRI_SHIFT                 8
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_MIN_RNR_NAK_TIMER_MASK    0x1F
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_MIN_RNR_NAK_TIMER_SHIFT   11
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_XRC_FLAG_MASK             0x1
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_XRC_FLAG_SHIFT            16
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_RESERVED_MASK             0x7FFF
#define ROCE_CREATE_QP_RESP_RAMROD_DATA_RESERVED_SHIFT            17
	__le16 xrc_domain /* SRC domain. Only applicable when xrc_flag is set */;
	u8 max_ird;
	u8 traffic_class /* In case of RRoCE on IPv4 will be used as TOS */;
	u8 hop_limit /* In case of RRoCE on IPv4 will be used as TTL */;
	u8 irq_num_pages;
	__le16 p_key;
	__le32 flow_label;
	__le32 dst_qp_id;
	u8 stats_counter_id /* Statistics counter ID to use */;
	u8 reserved1;
	__le16 mtu;
	__le32 initial_psn;
	__le16 pd;
	__le16 rq_num_pages;
	struct rdma_srq_id srq_id;
	struct regpair rq_pbl_addr;
	struct regpair irq_pbl_addr;
	__le16 local_mac_addr[3] /* BE order */;
	__le16 remote_mac_addr[3] /* BE order */;
	__le16 vlan_id;
	__le16 udp_src_port /* Only relevant in RRoCE */;
	__le32 src_gid[4] /* BE order. In case of RRoCE on IPv4 the lower register will hold the address. High registers must be zero! */;
	__le32 dst_gid[4] /* BE order. In case of RRoCE on IPv4 the lower register will hold the address. High registers must be zero! */;
	struct regpair qp_handle_for_cqe;
	struct regpair qp_handle_for_async;
	__le16 low_latency_phy_queue;
	u8 reserved2[2];
	__le32 cq_cid;
	__le16 regular_latency_phy_queue;
	__le16 dpi;
};


/*
 * roce DCQCN received statistics
 */
struct roce_dcqcn_received_stats
{
	struct regpair ecn_pkt_rcv /* The number of total packets with ECN indication received */;
	struct regpair cnp_pkt_rcv /* The number of total RoCE packets with CNP opcode received */;
};


/*
 * roce DCQCN sent statistics
 */
struct roce_dcqcn_sent_stats
{
	struct regpair cnp_pkt_sent /* The number of total RoCE packets with CNP opcode sent */;
};


/*
 * RoCE destroy qp requester output params
 */
struct roce_destroy_qp_req_output_params
{
	__le32 num_bound_mw;
	__le32 cq_prod /* Completion producer value at destroy QP */;
};


/*
 * RoCE destroy qp requester ramrod data
 */
struct roce_destroy_qp_req_ramrod_data
{
	struct regpair output_params_addr;
};


/*
 * RoCE destroy qp responder output params
 */
struct roce_destroy_qp_resp_output_params
{
	__le32 num_invalidated_mw;
	__le32 cq_prod /* Completion producer value at destroy QP */;
};


/*
 * RoCE destroy qp responder ramrod data
 */
struct roce_destroy_qp_resp_ramrod_data
{
	struct regpair output_params_addr;
};


/*
 * roce special events statistics
 */
struct roce_events_stats
{
	__le16 silent_drops;
	__le16 rnr_naks_sent;
	__le32 retransmit_count;
	__le32 icrc_error_count;
	__le32 reserved;
};


/*
 * ROCE slow path EQ cmd IDs
 */
enum roce_event_opcode
{
	ROCE_EVENT_CREATE_QP=11,
	ROCE_EVENT_MODIFY_QP,
	ROCE_EVENT_QUERY_QP,
	ROCE_EVENT_DESTROY_QP,
	ROCE_EVENT_CREATE_UD_QP,
	ROCE_EVENT_DESTROY_UD_QP,
	MAX_ROCE_EVENT_OPCODE
};


/*
 * roce func init ramrod data
 */
struct roce_init_func_params
{
	u8 ll2_queue_id /* This ll2 queue ID is used for Unreliable Datagram QP */;
	u8 cnp_vlan_priority /* VLAN priority of DCQCN CNP packet */;
	u8 cnp_dscp /* The value of DSCP field in IP header for CNP packets */;
	u8 reserved;
	__le32 cnp_send_timeout /* The minimal difference of send time between CNP packets for specific QP. Units are in microseconds */;
};


/*
 * roce func init ramrod data
 */
struct roce_init_func_ramrod_data
{
	struct rdma_init_func_ramrod_data rdma;
	struct roce_init_func_params roce;
};


/*
 * roce modify qp requester ramrod data
 */
struct roce_modify_qp_req_ramrod_data
{
	__le16 flags;
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_MOVE_TO_ERR_FLG_MASK      0x1
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_MOVE_TO_ERR_FLG_SHIFT     0
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_MOVE_TO_SQD_FLG_MASK      0x1
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_MOVE_TO_SQD_FLG_SHIFT     1
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_EN_SQD_ASYNC_NOTIFY_MASK  0x1
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_EN_SQD_ASYNC_NOTIFY_SHIFT 2
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_P_KEY_FLG_MASK            0x1
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_P_KEY_FLG_SHIFT           3
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_ADDRESS_VECTOR_FLG_MASK   0x1
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_ADDRESS_VECTOR_FLG_SHIFT  4
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_MAX_ORD_FLG_MASK          0x1
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_MAX_ORD_FLG_SHIFT         5
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_RNR_NAK_CNT_FLG_MASK      0x1
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_RNR_NAK_CNT_FLG_SHIFT     6
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_ERR_RETRY_CNT_FLG_MASK    0x1
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_ERR_RETRY_CNT_FLG_SHIFT   7
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_ACK_TIMEOUT_FLG_MASK      0x1
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_ACK_TIMEOUT_FLG_SHIFT     8
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_PRI_FLG_MASK              0x1
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_PRI_FLG_SHIFT             9
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_PRI_MASK                  0x7
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_PRI_SHIFT                 10
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_PHYSICAL_QUEUES_FLG_MASK  0x1
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_PHYSICAL_QUEUES_FLG_SHIFT 13
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_RESERVED1_MASK            0x3
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_RESERVED1_SHIFT           14
	u8 fields;
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_ERR_RETRY_CNT_MASK        0xF
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_ERR_RETRY_CNT_SHIFT       0
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_RNR_NAK_CNT_MASK          0xF
#define ROCE_MODIFY_QP_REQ_RAMROD_DATA_RNR_NAK_CNT_SHIFT         4
	u8 max_ord;
	u8 traffic_class;
	u8 hop_limit;
	__le16 p_key;
	__le32 flow_label;
	__le32 ack_timeout_val;
	__le16 mtu;
	__le16 reserved2;
	__le32 reserved3[2];
	__le16 low_latency_phy_queue;
	__le16 regular_latency_phy_queue;
	__le32 src_gid[4] /* BE order. In case of IPv4 the higher register will hold the address. Low registers must be zero! */;
	__le32 dst_gid[4] /* BE order. In case of IPv4 the higher register will hold the address. Low registers must be zero! */;
};


/*
 * roce modify qp responder ramrod data
 */
struct roce_modify_qp_resp_ramrod_data
{
	__le16 flags;
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_MOVE_TO_ERR_FLG_MASK        0x1
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_MOVE_TO_ERR_FLG_SHIFT       0
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_RDMA_RD_EN_MASK             0x1
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_RDMA_RD_EN_SHIFT            1
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_RDMA_WR_EN_MASK             0x1
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_RDMA_WR_EN_SHIFT            2
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_ATOMIC_EN_MASK              0x1
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_ATOMIC_EN_SHIFT             3
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_P_KEY_FLG_MASK              0x1
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_P_KEY_FLG_SHIFT             4
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_ADDRESS_VECTOR_FLG_MASK     0x1
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_ADDRESS_VECTOR_FLG_SHIFT    5
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_MAX_IRD_FLG_MASK            0x1
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_MAX_IRD_FLG_SHIFT           6
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_PRI_FLG_MASK                0x1
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_PRI_FLG_SHIFT               7
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_MIN_RNR_NAK_TIMER_FLG_MASK  0x1
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_MIN_RNR_NAK_TIMER_FLG_SHIFT 8
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_RDMA_OPS_EN_FLG_MASK        0x1
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_RDMA_OPS_EN_FLG_SHIFT       9
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_PHYSICAL_QUEUES_FLG_MASK    0x1
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_PHYSICAL_QUEUES_FLG_SHIFT   10
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_RESERVED1_MASK              0x1F
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_RESERVED1_SHIFT             11
	u8 fields;
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_PRI_MASK                    0x7
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_PRI_SHIFT                   0
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_MIN_RNR_NAK_TIMER_MASK      0x1F
#define ROCE_MODIFY_QP_RESP_RAMROD_DATA_MIN_RNR_NAK_TIMER_SHIFT     3
	u8 max_ird;
	u8 traffic_class;
	u8 hop_limit;
	__le16 p_key;
	__le32 flow_label;
	__le16 mtu;
	__le16 low_latency_phy_queue;
	__le16 regular_latency_phy_queue;
	u8 reserved2[6];
	__le32 src_gid[4] /* BE order. In case of IPv4 the higher register will hold the address. Low registers must be zero! */;
	__le32 dst_gid[4] /* BE order. In case of IPv4 the higher register will hold the address. Low registers must be zero! */;
};


/*
 * RoCE query qp requester output params
 */
struct roce_query_qp_req_output_params
{
	__le32 psn /* send next psn */;
	__le32 flags;
#define ROCE_QUERY_QP_REQ_OUTPUT_PARAMS_ERR_FLG_MASK          0x1
#define ROCE_QUERY_QP_REQ_OUTPUT_PARAMS_ERR_FLG_SHIFT         0
#define ROCE_QUERY_QP_REQ_OUTPUT_PARAMS_SQ_DRAINING_FLG_MASK  0x1
#define ROCE_QUERY_QP_REQ_OUTPUT_PARAMS_SQ_DRAINING_FLG_SHIFT 1
#define ROCE_QUERY_QP_REQ_OUTPUT_PARAMS_RESERVED0_MASK        0x3FFFFFFF
#define ROCE_QUERY_QP_REQ_OUTPUT_PARAMS_RESERVED0_SHIFT       2
};


/*
 * RoCE query qp requester ramrod data
 */
struct roce_query_qp_req_ramrod_data
{
	struct regpair output_params_addr;
};


/*
 * RoCE query qp responder output params
 */
struct roce_query_qp_resp_output_params
{
	__le32 psn /* send next psn */;
	__le32 err_flag;
#define ROCE_QUERY_QP_RESP_OUTPUT_PARAMS_ERROR_FLG_MASK  0x1
#define ROCE_QUERY_QP_RESP_OUTPUT_PARAMS_ERROR_FLG_SHIFT 0
#define ROCE_QUERY_QP_RESP_OUTPUT_PARAMS_RESERVED0_MASK  0x7FFFFFFF
#define ROCE_QUERY_QP_RESP_OUTPUT_PARAMS_RESERVED0_SHIFT 1
};


/*
 * RoCE query qp responder ramrod data
 */
struct roce_query_qp_resp_ramrod_data
{
	struct regpair output_params_addr;
};


/*
 * ROCE ramrod command IDs
 */
enum roce_ramrod_cmd_id
{
	ROCE_RAMROD_CREATE_QP=11,
	ROCE_RAMROD_MODIFY_QP,
	ROCE_RAMROD_QUERY_QP,
	ROCE_RAMROD_DESTROY_QP,
	ROCE_RAMROD_CREATE_UD_QP,
	ROCE_RAMROD_DESTROY_UD_QP,
	MAX_ROCE_RAMROD_CMD_ID
};






struct e4_mstorm_roce_req_conn_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 byte1 /* state */;
	u8 flags0;
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_BIT0_MASK     0x1 /* exist_in_qm0 */
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_BIT0_SHIFT    0
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_BIT1_MASK     0x1 /* exist_in_qm1 */
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_BIT1_SHIFT    1
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_CF0_MASK      0x3 /* cf0 */
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_CF0_SHIFT     2
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_CF1_MASK      0x3 /* cf1 */
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_CF1_SHIFT     4
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_CF2_MASK      0x3 /* cf2 */
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_CF2_SHIFT     6
	u8 flags1;
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_CF0EN_MASK    0x1 /* cf0en */
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_CF0EN_SHIFT   0
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_CF1EN_MASK    0x1 /* cf1en */
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_CF1EN_SHIFT   1
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_CF2EN_MASK    0x1 /* cf2en */
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_CF2EN_SHIFT   2
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_RULE0EN_MASK  0x1 /* rule0en */
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_RULE0EN_SHIFT 3
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_RULE1EN_MASK  0x1 /* rule1en */
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_RULE1EN_SHIFT 4
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_RULE2EN_MASK  0x1 /* rule2en */
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_RULE2EN_SHIFT 5
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_RULE3EN_MASK  0x1 /* rule3en */
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_RULE3EN_SHIFT 6
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_RULE4EN_MASK  0x1 /* rule4en */
#define E4_MSTORM_ROCE_REQ_CONN_AG_CTX_RULE4EN_SHIFT 7
	__le16 word0 /* word0 */;
	__le16 word1 /* word1 */;
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
};


struct e4_mstorm_roce_resp_conn_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 byte1 /* state */;
	u8 flags0;
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_BIT0_MASK     0x1 /* exist_in_qm0 */
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_BIT0_SHIFT    0
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_BIT1_MASK     0x1 /* exist_in_qm1 */
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_BIT1_SHIFT    1
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_CF0_MASK      0x3 /* cf0 */
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_CF0_SHIFT     2
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_CF1_MASK      0x3 /* cf1 */
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_CF1_SHIFT     4
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_CF2_MASK      0x3 /* cf2 */
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_CF2_SHIFT     6
	u8 flags1;
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_CF0EN_MASK    0x1 /* cf0en */
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_CF0EN_SHIFT   0
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_CF1EN_MASK    0x1 /* cf1en */
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_CF1EN_SHIFT   1
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_CF2EN_MASK    0x1 /* cf2en */
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_CF2EN_SHIFT   2
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_RULE0EN_MASK  0x1 /* rule0en */
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_RULE0EN_SHIFT 3
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_RULE1EN_MASK  0x1 /* rule1en */
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_RULE1EN_SHIFT 4
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_RULE2EN_MASK  0x1 /* rule2en */
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_RULE2EN_SHIFT 5
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_RULE3EN_MASK  0x1 /* rule3en */
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_RULE3EN_SHIFT 6
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_RULE4EN_MASK  0x1 /* rule4en */
#define E4_MSTORM_ROCE_RESP_CONN_AG_CTX_RULE4EN_SHIFT 7
	__le16 word0 /* word0 */;
	__le16 word1 /* word1 */;
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
};


struct e4_tstorm_roce_req_conn_ag_ctx
{
	u8 reserved0 /* cdu_validation */;
	u8 state /* state */;
	u8 flags0;
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_EXIST_IN_QM0_MASK                0x1 /* exist_in_qm0 */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_EXIST_IN_QM0_SHIFT               0
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RX_ERROR_OCCURED_MASK            0x1 /* exist_in_qm1 */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RX_ERROR_OCCURED_SHIFT           1
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_TX_CQE_ERROR_OCCURED_MASK        0x1 /* bit2 */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_TX_CQE_ERROR_OCCURED_SHIFT       2
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_BIT3_MASK                        0x1 /* bit3 */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_BIT3_SHIFT                       3
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_MSTORM_FLUSH_MASK                0x1 /* bit4 */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_MSTORM_FLUSH_SHIFT               4
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_CACHED_ORQ_MASK                  0x1 /* bit5 */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_CACHED_ORQ_SHIFT                 5
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_TIMER_CF_MASK                    0x3 /* timer0cf */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_TIMER_CF_SHIFT                   6
	u8 flags1;
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_CF1_MASK                         0x3 /* timer1cf */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_CF1_SHIFT                        0
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_SQ_CF_MASK                 0x3 /* timer2cf */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_SQ_CF_SHIFT                2
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_TIMER_STOP_ALL_CF_MASK           0x3 /* timer_stop_all */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_TIMER_STOP_ALL_CF_SHIFT          4
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_Q0_CF_MASK                 0x3 /* cf4 */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_Q0_CF_SHIFT                6
	u8 flags2;
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_MSTORM_FLUSH_CF_MASK             0x3 /* cf5 */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_MSTORM_FLUSH_CF_SHIFT            0
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_SET_TIMER_CF_MASK                0x3 /* cf6 */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_SET_TIMER_CF_SHIFT               2
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_TX_ASYNC_ERROR_CF_MASK           0x3 /* cf7 */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_TX_ASYNC_ERROR_CF_SHIFT          4
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RXMIT_DONE_CF_MASK               0x3 /* cf8 */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RXMIT_DONE_CF_SHIFT              6
	u8 flags3;
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_ERROR_SCAN_COMPLETED_CF_MASK     0x3 /* cf9 */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_ERROR_SCAN_COMPLETED_CF_SHIFT    0
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_SQ_DRAIN_COMPLETED_CF_MASK       0x3 /* cf10 */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_SQ_DRAIN_COMPLETED_CF_SHIFT      2
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_TIMER_CF_EN_MASK                 0x1 /* cf0en */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_TIMER_CF_EN_SHIFT                4
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_CF1EN_MASK                       0x1 /* cf1en */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_CF1EN_SHIFT                      5
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_SQ_CF_EN_MASK              0x1 /* cf2en */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_SQ_CF_EN_SHIFT             6
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_TIMER_STOP_ALL_CF_EN_MASK        0x1 /* cf3en */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_TIMER_STOP_ALL_CF_EN_SHIFT       7
	u8 flags4;
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_Q0_CF_EN_MASK              0x1 /* cf4en */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_Q0_CF_EN_SHIFT             0
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_MSTORM_FLUSH_CF_EN_MASK          0x1 /* cf5en */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_MSTORM_FLUSH_CF_EN_SHIFT         1
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_SET_TIMER_CF_EN_MASK             0x1 /* cf6en */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_SET_TIMER_CF_EN_SHIFT            2
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_TX_ASYNC_ERROR_CF_EN_MASK        0x1 /* cf7en */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_TX_ASYNC_ERROR_CF_EN_SHIFT       3
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RXMIT_DONE_CF_EN_MASK            0x1 /* cf8en */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RXMIT_DONE_CF_EN_SHIFT           4
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_ERROR_SCAN_COMPLETED_CF_EN_MASK  0x1 /* cf9en */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_ERROR_SCAN_COMPLETED_CF_EN_SHIFT 5
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_SQ_DRAIN_COMPLETED_CF_EN_MASK    0x1 /* cf10en */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_SQ_DRAIN_COMPLETED_CF_EN_SHIFT   6
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE0EN_MASK                     0x1 /* rule0en */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE0EN_SHIFT                    7
	u8 flags5;
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE1EN_MASK                     0x1 /* rule1en */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE1EN_SHIFT                    0
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE2EN_MASK                     0x1 /* rule2en */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE2EN_SHIFT                    1
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE3EN_MASK                     0x1 /* rule3en */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE3EN_SHIFT                    2
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE4EN_MASK                     0x1 /* rule4en */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE4EN_SHIFT                    3
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE5EN_MASK                     0x1 /* rule5en */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE5EN_SHIFT                    4
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_SND_SQ_CONS_EN_MASK              0x1 /* rule6en */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_SND_SQ_CONS_EN_SHIFT             5
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE7EN_MASK                     0x1 /* rule7en */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE7EN_SHIFT                    6
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE8EN_MASK                     0x1 /* rule8en */
#define E4_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE8EN_SHIFT                    7
	__le32 reg0 /* reg0 */;
	__le32 snd_nxt_psn /* reg1 */;
	__le32 snd_max_psn /* reg2 */;
	__le32 orq_prod /* reg3 */;
	__le32 reg4 /* reg4 */;
	__le32 reg5 /* reg5 */;
	__le32 reg6 /* reg6 */;
	__le32 reg7 /* reg7 */;
	__le32 reg8 /* reg8 */;
	u8 tx_cqe_error_type /* byte2 */;
	u8 orq_cache_idx /* byte3 */;
	__le16 snd_sq_cons_th /* word0 */;
	u8 byte4 /* byte4 */;
	u8 byte5 /* byte5 */;
	__le16 snd_sq_cons /* word1 */;
	__le16 conn_dpi /* conn_dpi */;
	__le16 word3 /* word3 */;
	__le32 reg9 /* reg9 */;
	__le32 reg10 /* reg10 */;
};


struct e4_tstorm_roce_resp_conn_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 state /* state */;
	u8 flags0;
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_EXIST_IN_QM0_MASK               0x1 /* exist_in_qm0 */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_EXIST_IN_QM0_SHIFT              0
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RX_ERROR_NOTIFY_REQUESTER_MASK  0x1 /* exist_in_qm1 */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RX_ERROR_NOTIFY_REQUESTER_SHIFT 1
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_BIT2_MASK                       0x1 /* bit2 */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_BIT2_SHIFT                      2
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_BIT3_MASK                       0x1 /* bit3 */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_BIT3_SHIFT                      3
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_MSTORM_FLUSH_MASK               0x1 /* bit4 */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_MSTORM_FLUSH_SHIFT              4
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_BIT5_MASK                       0x1 /* bit5 */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_BIT5_SHIFT                      5
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF0_MASK                        0x3 /* timer0cf */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF0_SHIFT                       6
	u8 flags1;
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RX_ERROR_CF_MASK                0x3 /* timer1cf */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RX_ERROR_CF_SHIFT               0
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_TX_ERROR_CF_MASK                0x3 /* timer2cf */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_TX_ERROR_CF_SHIFT               2
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF3_MASK                        0x3 /* timer_stop_all */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF3_SHIFT                       4
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_FLUSH_Q0_CF_MASK                0x3 /* cf4 */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_FLUSH_Q0_CF_SHIFT               6
	u8 flags2;
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_MSTORM_FLUSH_CF_MASK            0x3 /* cf5 */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_MSTORM_FLUSH_CF_SHIFT           0
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF6_MASK                        0x3 /* cf6 */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF6_SHIFT                       2
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF7_MASK                        0x3 /* cf7 */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF7_SHIFT                       4
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF8_MASK                        0x3 /* cf8 */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF8_SHIFT                       6
	u8 flags3;
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF9_MASK                        0x3 /* cf9 */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF9_SHIFT                       0
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF10_MASK                       0x3 /* cf10 */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF10_SHIFT                      2
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF0EN_MASK                      0x1 /* cf0en */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF0EN_SHIFT                     4
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RX_ERROR_CF_EN_MASK             0x1 /* cf1en */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RX_ERROR_CF_EN_SHIFT            5
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_TX_ERROR_CF_EN_MASK             0x1 /* cf2en */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_TX_ERROR_CF_EN_SHIFT            6
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF3EN_MASK                      0x1 /* cf3en */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF3EN_SHIFT                     7
	u8 flags4;
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_FLUSH_Q0_CF_EN_MASK             0x1 /* cf4en */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_FLUSH_Q0_CF_EN_SHIFT            0
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_MSTORM_FLUSH_CF_EN_MASK         0x1 /* cf5en */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_MSTORM_FLUSH_CF_EN_SHIFT        1
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF6EN_MASK                      0x1 /* cf6en */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF6EN_SHIFT                     2
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF7EN_MASK                      0x1 /* cf7en */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF7EN_SHIFT                     3
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF8EN_MASK                      0x1 /* cf8en */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF8EN_SHIFT                     4
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF9EN_MASK                      0x1 /* cf9en */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF9EN_SHIFT                     5
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF10EN_MASK                     0x1 /* cf10en */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_CF10EN_SHIFT                    6
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE0EN_MASK                    0x1 /* rule0en */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE0EN_SHIFT                   7
	u8 flags5;
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE1EN_MASK                    0x1 /* rule1en */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE1EN_SHIFT                   0
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE2EN_MASK                    0x1 /* rule2en */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE2EN_SHIFT                   1
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE3EN_MASK                    0x1 /* rule3en */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE3EN_SHIFT                   2
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE4EN_MASK                    0x1 /* rule4en */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE4EN_SHIFT                   3
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE5EN_MASK                    0x1 /* rule5en */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE5EN_SHIFT                   4
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RQ_RULE_EN_MASK                 0x1 /* rule6en */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RQ_RULE_EN_SHIFT                5
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE7EN_MASK                    0x1 /* rule7en */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE7EN_SHIFT                   6
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE8EN_MASK                    0x1 /* rule8en */
#define E4_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE8EN_SHIFT                   7
	__le32 psn_and_rxmit_id_echo /* reg0 */;
	__le32 reg1 /* reg1 */;
	__le32 reg2 /* reg2 */;
	__le32 reg3 /* reg3 */;
	__le32 reg4 /* reg4 */;
	__le32 reg5 /* reg5 */;
	__le32 reg6 /* reg6 */;
	__le32 reg7 /* reg7 */;
	__le32 reg8 /* reg8 */;
	u8 tx_async_error_type /* byte2 */;
	u8 byte3 /* byte3 */;
	__le16 rq_cons /* word0 */;
	u8 byte4 /* byte4 */;
	u8 byte5 /* byte5 */;
	__le16 rq_prod /* word1 */;
	__le16 conn_dpi /* conn_dpi */;
	__le16 irq_cons /* word3 */;
	__le32 num_invlidated_mw /* reg9 */;
	__le32 reg10 /* reg10 */;
};


struct e4_ustorm_roce_req_conn_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 byte1 /* state */;
	u8 flags0;
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_BIT0_MASK     0x1 /* exist_in_qm0 */
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_BIT0_SHIFT    0
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_BIT1_MASK     0x1 /* exist_in_qm1 */
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_BIT1_SHIFT    1
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF0_MASK      0x3 /* timer0cf */
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF0_SHIFT     2
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF1_MASK      0x3 /* timer1cf */
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF1_SHIFT     4
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF2_MASK      0x3 /* timer2cf */
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF2_SHIFT     6
	u8 flags1;
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF3_MASK      0x3 /* timer_stop_all */
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF3_SHIFT     0
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF4_MASK      0x3 /* cf4 */
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF4_SHIFT     2
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF5_MASK      0x3 /* cf5 */
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF5_SHIFT     4
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF6_MASK      0x3 /* cf6 */
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF6_SHIFT     6
	u8 flags2;
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF0EN_MASK    0x1 /* cf0en */
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF0EN_SHIFT   0
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF1EN_MASK    0x1 /* cf1en */
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF1EN_SHIFT   1
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF2EN_MASK    0x1 /* cf2en */
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF2EN_SHIFT   2
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF3EN_MASK    0x1 /* cf3en */
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF3EN_SHIFT   3
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF4EN_MASK    0x1 /* cf4en */
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF4EN_SHIFT   4
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF5EN_MASK    0x1 /* cf5en */
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF5EN_SHIFT   5
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF6EN_MASK    0x1 /* cf6en */
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_CF6EN_SHIFT   6
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE0EN_MASK  0x1 /* rule0en */
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE0EN_SHIFT 7
	u8 flags3;
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE1EN_MASK  0x1 /* rule1en */
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE1EN_SHIFT 0
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE2EN_MASK  0x1 /* rule2en */
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE2EN_SHIFT 1
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE3EN_MASK  0x1 /* rule3en */
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE3EN_SHIFT 2
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE4EN_MASK  0x1 /* rule4en */
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE4EN_SHIFT 3
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE5EN_MASK  0x1 /* rule5en */
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE5EN_SHIFT 4
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE6EN_MASK  0x1 /* rule6en */
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE6EN_SHIFT 5
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE7EN_MASK  0x1 /* rule7en */
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE7EN_SHIFT 6
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE8EN_MASK  0x1 /* rule8en */
#define E4_USTORM_ROCE_REQ_CONN_AG_CTX_RULE8EN_SHIFT 7
	u8 byte2 /* byte2 */;
	u8 byte3 /* byte3 */;
	__le16 word0 /* conn_dpi */;
	__le16 word1 /* word1 */;
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
	__le32 reg2 /* reg2 */;
	__le32 reg3 /* reg3 */;
	__le16 word2 /* word2 */;
	__le16 word3 /* word3 */;
};


struct e4_ustorm_roce_resp_conn_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 byte1 /* state */;
	u8 flags0;
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_BIT0_MASK     0x1 /* exist_in_qm0 */
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_BIT0_SHIFT    0
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_BIT1_MASK     0x1 /* exist_in_qm1 */
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_BIT1_SHIFT    1
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF0_MASK      0x3 /* timer0cf */
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF0_SHIFT     2
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF1_MASK      0x3 /* timer1cf */
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF1_SHIFT     4
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF2_MASK      0x3 /* timer2cf */
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF2_SHIFT     6
	u8 flags1;
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF3_MASK      0x3 /* timer_stop_all */
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF3_SHIFT     0
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF4_MASK      0x3 /* cf4 */
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF4_SHIFT     2
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF5_MASK      0x3 /* cf5 */
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF5_SHIFT     4
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF6_MASK      0x3 /* cf6 */
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF6_SHIFT     6
	u8 flags2;
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF0EN_MASK    0x1 /* cf0en */
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF0EN_SHIFT   0
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF1EN_MASK    0x1 /* cf1en */
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF1EN_SHIFT   1
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF2EN_MASK    0x1 /* cf2en */
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF2EN_SHIFT   2
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF3EN_MASK    0x1 /* cf3en */
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF3EN_SHIFT   3
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF4EN_MASK    0x1 /* cf4en */
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF4EN_SHIFT   4
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF5EN_MASK    0x1 /* cf5en */
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF5EN_SHIFT   5
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF6EN_MASK    0x1 /* cf6en */
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_CF6EN_SHIFT   6
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE0EN_MASK  0x1 /* rule0en */
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE0EN_SHIFT 7
	u8 flags3;
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE1EN_MASK  0x1 /* rule1en */
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE1EN_SHIFT 0
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE2EN_MASK  0x1 /* rule2en */
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE2EN_SHIFT 1
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE3EN_MASK  0x1 /* rule3en */
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE3EN_SHIFT 2
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE4EN_MASK  0x1 /* rule4en */
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE4EN_SHIFT 3
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE5EN_MASK  0x1 /* rule5en */
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE5EN_SHIFT 4
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE6EN_MASK  0x1 /* rule6en */
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE6EN_SHIFT 5
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE7EN_MASK  0x1 /* rule7en */
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE7EN_SHIFT 6
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE8EN_MASK  0x1 /* rule8en */
#define E4_USTORM_ROCE_RESP_CONN_AG_CTX_RULE8EN_SHIFT 7
	u8 byte2 /* byte2 */;
	u8 byte3 /* byte3 */;
	__le16 word0 /* conn_dpi */;
	__le16 word1 /* word1 */;
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
	__le32 reg2 /* reg2 */;
	__le32 reg3 /* reg3 */;
	__le16 word2 /* word2 */;
	__le16 word3 /* word3 */;
};


struct e4_xstorm_roce_req_conn_ag_ctx
{
	u8 reserved0 /* cdu_validation */;
	u8 state /* state */;
	u8 flags0;
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_EXIST_IN_QM0_MASK        0x1 /* exist_in_qm0 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_EXIST_IN_QM0_SHIFT       0
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED1_MASK           0x1 /* exist_in_qm1 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED1_SHIFT          1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED2_MASK           0x1 /* exist_in_qm2 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED2_SHIFT          2
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_EXIST_IN_QM3_MASK        0x1 /* exist_in_qm3 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_EXIST_IN_QM3_SHIFT       3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED3_MASK           0x1 /* bit4 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED3_SHIFT          4
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED4_MASK           0x1 /* cf_array_active */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED4_SHIFT          5
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED5_MASK           0x1 /* bit6 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED5_SHIFT          6
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED6_MASK           0x1 /* bit7 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED6_SHIFT          7
	u8 flags1;
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED7_MASK           0x1 /* bit8 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED7_SHIFT          0
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED8_MASK           0x1 /* bit9 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED8_SHIFT          1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_BIT10_MASK               0x1 /* bit10 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_BIT10_SHIFT              2
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_BIT11_MASK               0x1 /* bit11 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_BIT11_SHIFT              3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_BIT12_MASK               0x1 /* bit12 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_BIT12_SHIFT              4
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_BIT13_MASK               0x1 /* bit13 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_BIT13_SHIFT              5
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_ERROR_STATE_MASK         0x1 /* bit14 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_ERROR_STATE_SHIFT        6
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_YSTORM_FLUSH_MASK        0x1 /* bit15 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_YSTORM_FLUSH_SHIFT       7
	u8 flags2;
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF0_MASK                 0x3 /* timer0cf */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF0_SHIFT                0
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF1_MASK                 0x3 /* timer1cf */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF1_SHIFT                2
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF2_MASK                 0x3 /* timer2cf */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF2_SHIFT                4
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF3_MASK                 0x3 /* timer_stop_all */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF3_SHIFT                6
	u8 flags3;
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_SQ_FLUSH_CF_MASK         0x3 /* cf4 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_SQ_FLUSH_CF_SHIFT        0
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RX_ERROR_CF_MASK         0x3 /* cf5 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RX_ERROR_CF_SHIFT        2
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_SND_RXMIT_CF_MASK        0x3 /* cf6 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_SND_RXMIT_CF_SHIFT       4
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_Q0_CF_MASK         0x3 /* cf7 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_Q0_CF_SHIFT        6
	u8 flags4;
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF8_MASK                 0x3 /* cf8 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF8_SHIFT                0
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF9_MASK                 0x3 /* cf9 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF9_SHIFT                2
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF10_MASK                0x3 /* cf10 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF10_SHIFT               4
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF11_MASK                0x3 /* cf11 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF11_SHIFT               6
	u8 flags5;
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF12_MASK                0x3 /* cf12 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF12_SHIFT               0
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF13_MASK                0x3 /* cf13 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF13_SHIFT               2
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_FMR_ENDED_CF_MASK        0x3 /* cf14 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_FMR_ENDED_CF_SHIFT       4
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF15_MASK                0x3 /* cf15 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF15_SHIFT               6
	u8 flags6;
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF16_MASK                0x3 /* cf16 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF16_SHIFT               0
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF17_MASK                0x3 /* cf_array_cf */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF17_SHIFT               2
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF18_MASK                0x3 /* cf18 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF18_SHIFT               4
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF19_MASK                0x3 /* cf19 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF19_SHIFT               6
	u8 flags7;
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF20_MASK                0x3 /* cf20 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF20_SHIFT               0
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF21_MASK                0x3 /* cf21 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF21_SHIFT               2
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_SLOW_PATH_MASK           0x3 /* cf22 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_SLOW_PATH_SHIFT          4
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF0EN_MASK               0x1 /* cf0en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF0EN_SHIFT              6
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF1EN_MASK               0x1 /* cf1en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF1EN_SHIFT              7
	u8 flags8;
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF2EN_MASK               0x1 /* cf2en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF2EN_SHIFT              0
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF3EN_MASK               0x1 /* cf3en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF3EN_SHIFT              1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_SQ_FLUSH_CF_EN_MASK      0x1 /* cf4en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_SQ_FLUSH_CF_EN_SHIFT     2
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RX_ERROR_CF_EN_MASK      0x1 /* cf5en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RX_ERROR_CF_EN_SHIFT     3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_SND_RXMIT_CF_EN_MASK     0x1 /* cf6en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_SND_RXMIT_CF_EN_SHIFT    4
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_Q0_CF_EN_MASK      0x1 /* cf7en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_Q0_CF_EN_SHIFT     5
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF8EN_MASK               0x1 /* cf8en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF8EN_SHIFT              6
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF9EN_MASK               0x1 /* cf9en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF9EN_SHIFT              7
	u8 flags9;
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF10EN_MASK              0x1 /* cf10en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF10EN_SHIFT             0
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF11EN_MASK              0x1 /* cf11en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF11EN_SHIFT             1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF12EN_MASK              0x1 /* cf12en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF12EN_SHIFT             2
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF13EN_MASK              0x1 /* cf13en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF13EN_SHIFT             3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_FME_ENDED_CF_EN_MASK     0x1 /* cf14en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_FME_ENDED_CF_EN_SHIFT    4
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF15EN_MASK              0x1 /* cf15en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF15EN_SHIFT             5
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF16EN_MASK              0x1 /* cf16en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF16EN_SHIFT             6
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF17EN_MASK              0x1 /* cf_array_cf_en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF17EN_SHIFT             7
	u8 flags10;
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF18EN_MASK              0x1 /* cf18en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF18EN_SHIFT             0
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF19EN_MASK              0x1 /* cf19en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF19EN_SHIFT             1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF20EN_MASK              0x1 /* cf20en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF20EN_SHIFT             2
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF21EN_MASK              0x1 /* cf21en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF21EN_SHIFT             3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_SLOW_PATH_EN_MASK        0x1 /* cf22en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_SLOW_PATH_EN_SHIFT       4
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF23EN_MASK              0x1 /* cf23en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF23EN_SHIFT             5
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE0EN_MASK             0x1 /* rule0en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE0EN_SHIFT            6
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE1EN_MASK             0x1 /* rule1en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE1EN_SHIFT            7
	u8 flags11;
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE2EN_MASK             0x1 /* rule2en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE2EN_SHIFT            0
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE3EN_MASK             0x1 /* rule3en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE3EN_SHIFT            1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE4EN_MASK             0x1 /* rule4en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE4EN_SHIFT            2
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE5EN_MASK             0x1 /* rule5en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE5EN_SHIFT            3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE6EN_MASK             0x1 /* rule6en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE6EN_SHIFT            4
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_E2E_CREDIT_RULE_EN_MASK  0x1 /* rule7en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_E2E_CREDIT_RULE_EN_SHIFT 5
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED1_MASK        0x1 /* rule8en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED1_SHIFT       6
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE9EN_MASK             0x1 /* rule9en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE9EN_SHIFT            7
	u8 flags12;
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_SQ_PROD_EN_MASK          0x1 /* rule10en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_SQ_PROD_EN_SHIFT         0
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE11EN_MASK            0x1 /* rule11en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE11EN_SHIFT           1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED2_MASK        0x1 /* rule12en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED2_SHIFT       2
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED3_MASK        0x1 /* rule13en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED3_SHIFT       3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_INV_FENCE_RULE_EN_MASK   0x1 /* rule14en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_INV_FENCE_RULE_EN_SHIFT  4
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE15EN_MASK            0x1 /* rule15en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE15EN_SHIFT           5
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_ORQ_FENCE_RULE_EN_MASK   0x1 /* rule16en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_ORQ_FENCE_RULE_EN_SHIFT  6
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_MAX_ORD_RULE_EN_MASK     0x1 /* rule17en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_MAX_ORD_RULE_EN_SHIFT    7
	u8 flags13;
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE18EN_MASK            0x1 /* rule18en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE18EN_SHIFT           0
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE19EN_MASK            0x1 /* rule19en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE19EN_SHIFT           1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED4_MASK        0x1 /* rule20en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED4_SHIFT       2
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED5_MASK        0x1 /* rule21en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED5_SHIFT       3
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED6_MASK        0x1 /* rule22en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED6_SHIFT       4
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED7_MASK        0x1 /* rule23en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED7_SHIFT       5
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED8_MASK        0x1 /* rule24en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED8_SHIFT       6
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED9_MASK        0x1 /* rule25en */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED9_SHIFT       7
	u8 flags14;
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_MIGRATION_FLAG_MASK      0x1 /* bit16 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_MIGRATION_FLAG_SHIFT     0
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_BIT17_MASK               0x1 /* bit17 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_BIT17_SHIFT              1
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_DPM_PORT_NUM_MASK        0x3 /* bit18 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_DPM_PORT_NUM_SHIFT       2
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED_MASK            0x1 /* bit20 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED_SHIFT           4
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_ROCE_EDPM_ENABLE_MASK    0x1 /* bit21 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_ROCE_EDPM_ENABLE_SHIFT   5
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF23_MASK                0x3 /* cf23 */
#define E4_XSTORM_ROCE_REQ_CONN_AG_CTX_CF23_SHIFT               6
	u8 byte2 /* byte2 */;
	__le16 physical_q0 /* physical_q0 */;
	__le16 word1 /* physical_q1 */;
	__le16 sq_cmp_cons /* physical_q2 */;
	__le16 sq_cons /* word3 */;
	__le16 sq_prod /* word4 */;
	__le16 word5 /* word5 */;
	__le16 conn_dpi /* conn_dpi */;
	u8 byte3 /* byte3 */;
	u8 byte4 /* byte4 */;
	u8 byte5 /* byte5 */;
	u8 byte6 /* byte6 */;
	__le32 lsn /* reg0 */;
	__le32 ssn /* reg1 */;
	__le32 snd_una_psn /* reg2 */;
	__le32 snd_nxt_psn /* reg3 */;
	__le32 reg4 /* reg4 */;
	__le32 orq_cons_th /* cf_array0 */;
	__le32 orq_cons /* cf_array1 */;
};


struct e4_xstorm_roce_resp_conn_ag_ctx
{
	u8 reserved0 /* cdu_validation */;
	u8 state /* state */;
	u8 flags0;
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_EXIST_IN_QM0_MASK      0x1 /* exist_in_qm0 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_EXIST_IN_QM0_SHIFT     0
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED1_MASK         0x1 /* exist_in_qm1 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED1_SHIFT        1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED2_MASK         0x1 /* exist_in_qm2 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED2_SHIFT        2
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_EXIST_IN_QM3_MASK      0x1 /* exist_in_qm3 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_EXIST_IN_QM3_SHIFT     3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED3_MASK         0x1 /* bit4 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED3_SHIFT        4
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED4_MASK         0x1 /* cf_array_active */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED4_SHIFT        5
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED5_MASK         0x1 /* bit6 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED5_SHIFT        6
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED6_MASK         0x1 /* bit7 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED6_SHIFT        7
	u8 flags1;
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED7_MASK         0x1 /* bit8 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED7_SHIFT        0
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED8_MASK         0x1 /* bit9 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED8_SHIFT        1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT10_MASK             0x1 /* bit10 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT10_SHIFT            2
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT11_MASK             0x1 /* bit11 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT11_SHIFT            3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT12_MASK             0x1 /* bit12 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT12_SHIFT            4
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT13_MASK             0x1 /* bit13 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT13_SHIFT            5
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_ERROR_STATE_MASK       0x1 /* bit14 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_ERROR_STATE_SHIFT      6
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_YSTORM_FLUSH_MASK      0x1 /* bit15 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_YSTORM_FLUSH_SHIFT     7
	u8 flags2;
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF0_MASK               0x3 /* timer0cf */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF0_SHIFT              0
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF1_MASK               0x3 /* timer1cf */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF1_SHIFT              2
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF2_MASK               0x3 /* timer2cf */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF2_SHIFT              4
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF3_MASK               0x3 /* timer_stop_all */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF3_SHIFT              6
	u8 flags3;
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RXMIT_CF_MASK          0x3 /* cf4 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RXMIT_CF_SHIFT         0
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RX_ERROR_CF_MASK       0x3 /* cf5 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RX_ERROR_CF_SHIFT      2
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_FORCE_ACK_CF_MASK      0x3 /* cf6 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_FORCE_ACK_CF_SHIFT     4
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_FLUSH_Q0_CF_MASK       0x3 /* cf7 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_FLUSH_Q0_CF_SHIFT      6
	u8 flags4;
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF8_MASK               0x3 /* cf8 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF8_SHIFT              0
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF9_MASK               0x3 /* cf9 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF9_SHIFT              2
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF10_MASK              0x3 /* cf10 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF10_SHIFT             4
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF11_MASK              0x3 /* cf11 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF11_SHIFT             6
	u8 flags5;
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF12_MASK              0x3 /* cf12 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF12_SHIFT             0
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF13_MASK              0x3 /* cf13 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF13_SHIFT             2
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF14_MASK              0x3 /* cf14 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF14_SHIFT             4
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF15_MASK              0x3 /* cf15 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF15_SHIFT             6
	u8 flags6;
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF16_MASK              0x3 /* cf16 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF16_SHIFT             0
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF17_MASK              0x3 /* cf_array_cf */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF17_SHIFT             2
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF18_MASK              0x3 /* cf18 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF18_SHIFT             4
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF19_MASK              0x3 /* cf19 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF19_SHIFT             6
	u8 flags7;
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF20_MASK              0x3 /* cf20 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF20_SHIFT             0
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF21_MASK              0x3 /* cf21 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF21_SHIFT             2
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_SLOW_PATH_MASK         0x3 /* cf22 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_SLOW_PATH_SHIFT        4
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF0EN_MASK             0x1 /* cf0en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF0EN_SHIFT            6
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF1EN_MASK             0x1 /* cf1en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF1EN_SHIFT            7
	u8 flags8;
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF2EN_MASK             0x1 /* cf2en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF2EN_SHIFT            0
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF3EN_MASK             0x1 /* cf3en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF3EN_SHIFT            1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RXMIT_CF_EN_MASK       0x1 /* cf4en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RXMIT_CF_EN_SHIFT      2
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RX_ERROR_CF_EN_MASK    0x1 /* cf5en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RX_ERROR_CF_EN_SHIFT   3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_FORCE_ACK_CF_EN_MASK   0x1 /* cf6en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_FORCE_ACK_CF_EN_SHIFT  4
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_FLUSH_Q0_CF_EN_MASK    0x1 /* cf7en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_FLUSH_Q0_CF_EN_SHIFT   5
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF8EN_MASK             0x1 /* cf8en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF8EN_SHIFT            6
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF9EN_MASK             0x1 /* cf9en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF9EN_SHIFT            7
	u8 flags9;
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF10EN_MASK            0x1 /* cf10en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF10EN_SHIFT           0
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF11EN_MASK            0x1 /* cf11en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF11EN_SHIFT           1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF12EN_MASK            0x1 /* cf12en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF12EN_SHIFT           2
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF13EN_MASK            0x1 /* cf13en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF13EN_SHIFT           3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF14EN_MASK            0x1 /* cf14en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF14EN_SHIFT           4
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF15EN_MASK            0x1 /* cf15en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF15EN_SHIFT           5
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF16EN_MASK            0x1 /* cf16en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF16EN_SHIFT           6
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF17EN_MASK            0x1 /* cf_array_cf_en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF17EN_SHIFT           7
	u8 flags10;
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF18EN_MASK            0x1 /* cf18en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF18EN_SHIFT           0
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF19EN_MASK            0x1 /* cf19en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF19EN_SHIFT           1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF20EN_MASK            0x1 /* cf20en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF20EN_SHIFT           2
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF21EN_MASK            0x1 /* cf21en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF21EN_SHIFT           3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_SLOW_PATH_EN_MASK      0x1 /* cf22en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_SLOW_PATH_EN_SHIFT     4
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF23EN_MASK            0x1 /* cf23en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF23EN_SHIFT           5
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE0EN_MASK           0x1 /* rule0en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE0EN_SHIFT          6
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE1EN_MASK           0x1 /* rule1en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE1EN_SHIFT          7
	u8 flags11;
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE2EN_MASK           0x1 /* rule2en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE2EN_SHIFT          0
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE3EN_MASK           0x1 /* rule3en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE3EN_SHIFT          1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE4EN_MASK           0x1 /* rule4en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE4EN_SHIFT          2
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE5EN_MASK           0x1 /* rule5en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE5EN_SHIFT          3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE6EN_MASK           0x1 /* rule6en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE6EN_SHIFT          4
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE7EN_MASK           0x1 /* rule7en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE7EN_SHIFT          5
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED1_MASK      0x1 /* rule8en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED1_SHIFT     6
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE9EN_MASK           0x1 /* rule9en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE9EN_SHIFT          7
	u8 flags12;
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_IRQ_PROD_RULE_EN_MASK  0x1 /* rule10en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_IRQ_PROD_RULE_EN_SHIFT 0
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE11EN_MASK          0x1 /* rule11en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE11EN_SHIFT         1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED2_MASK      0x1 /* rule12en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED2_SHIFT     2
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED3_MASK      0x1 /* rule13en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED3_SHIFT     3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE14EN_MASK          0x1 /* rule14en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE14EN_SHIFT         4
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE15EN_MASK          0x1 /* rule15en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE15EN_SHIFT         5
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE16EN_MASK          0x1 /* rule16en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE16EN_SHIFT         6
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE17EN_MASK          0x1 /* rule17en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE17EN_SHIFT         7
	u8 flags13;
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE18EN_MASK          0x1 /* rule18en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE18EN_SHIFT         0
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE19EN_MASK          0x1 /* rule19en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE19EN_SHIFT         1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED4_MASK      0x1 /* rule20en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED4_SHIFT     2
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED5_MASK      0x1 /* rule21en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED5_SHIFT     3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED6_MASK      0x1 /* rule22en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED6_SHIFT     4
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED7_MASK      0x1 /* rule23en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED7_SHIFT     5
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED8_MASK      0x1 /* rule24en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED8_SHIFT     6
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED9_MASK      0x1 /* rule25en */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED9_SHIFT     7
	u8 flags14;
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT16_MASK             0x1 /* bit16 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT16_SHIFT            0
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT17_MASK             0x1 /* bit17 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT17_SHIFT            1
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT18_MASK             0x1 /* bit18 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT18_SHIFT            2
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT19_MASK             0x1 /* bit19 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT19_SHIFT            3
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT20_MASK             0x1 /* bit20 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT20_SHIFT            4
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT21_MASK             0x1 /* bit21 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT21_SHIFT            5
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF23_MASK              0x3 /* cf23 */
#define E4_XSTORM_ROCE_RESP_CONN_AG_CTX_CF23_SHIFT             6
	u8 byte2 /* byte2 */;
	__le16 physical_q0 /* physical_q0 */;
	__le16 irq_prod_shadow /* physical_q1 */;
	__le16 word2 /* physical_q2 */;
	__le16 irq_cons /* word3 */;
	__le16 irq_prod /* word4 */;
	__le16 e5_reserved1 /* word5 */;
	__le16 conn_dpi /* conn_dpi */;
	u8 rxmit_opcode /* byte3 */;
	u8 byte4 /* byte4 */;
	u8 byte5 /* byte5 */;
	u8 byte6 /* byte6 */;
	__le32 rxmit_psn_and_id /* reg0 */;
	__le32 rxmit_bytes_length /* reg1 */;
	__le32 psn /* reg2 */;
	__le32 reg3 /* reg3 */;
	__le32 reg4 /* reg4 */;
	__le32 reg5 /* cf_array0 */;
	__le32 msn_and_syndrome /* cf_array1 */;
};


struct e4_ystorm_roce_req_conn_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 byte1 /* state */;
	u8 flags0;
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_BIT0_MASK     0x1 /* exist_in_qm0 */
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_BIT0_SHIFT    0
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_BIT1_MASK     0x1 /* exist_in_qm1 */
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_BIT1_SHIFT    1
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_CF0_MASK      0x3 /* cf0 */
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_CF0_SHIFT     2
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_CF1_MASK      0x3 /* cf1 */
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_CF1_SHIFT     4
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_CF2_MASK      0x3 /* cf2 */
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_CF2_SHIFT     6
	u8 flags1;
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_CF0EN_MASK    0x1 /* cf0en */
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_CF0EN_SHIFT   0
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_CF1EN_MASK    0x1 /* cf1en */
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_CF1EN_SHIFT   1
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_CF2EN_MASK    0x1 /* cf2en */
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_CF2EN_SHIFT   2
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_RULE0EN_MASK  0x1 /* rule0en */
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_RULE0EN_SHIFT 3
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_RULE1EN_MASK  0x1 /* rule1en */
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_RULE1EN_SHIFT 4
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_RULE2EN_MASK  0x1 /* rule2en */
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_RULE2EN_SHIFT 5
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_RULE3EN_MASK  0x1 /* rule3en */
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_RULE3EN_SHIFT 6
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_RULE4EN_MASK  0x1 /* rule4en */
#define E4_YSTORM_ROCE_REQ_CONN_AG_CTX_RULE4EN_SHIFT 7
	u8 byte2 /* byte2 */;
	u8 byte3 /* byte3 */;
	__le16 word0 /* word0 */;
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
	__le16 word1 /* word1 */;
	__le16 word2 /* word2 */;
	__le16 word3 /* word3 */;
	__le16 word4 /* word4 */;
	__le32 reg2 /* reg2 */;
	__le32 reg3 /* reg3 */;
};


struct e4_ystorm_roce_resp_conn_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 byte1 /* state */;
	u8 flags0;
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_BIT0_MASK     0x1 /* exist_in_qm0 */
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_BIT0_SHIFT    0
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_BIT1_MASK     0x1 /* exist_in_qm1 */
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_BIT1_SHIFT    1
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_CF0_MASK      0x3 /* cf0 */
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_CF0_SHIFT     2
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_CF1_MASK      0x3 /* cf1 */
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_CF1_SHIFT     4
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_CF2_MASK      0x3 /* cf2 */
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_CF2_SHIFT     6
	u8 flags1;
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_CF0EN_MASK    0x1 /* cf0en */
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_CF0EN_SHIFT   0
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_CF1EN_MASK    0x1 /* cf1en */
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_CF1EN_SHIFT   1
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_CF2EN_MASK    0x1 /* cf2en */
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_CF2EN_SHIFT   2
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_RULE0EN_MASK  0x1 /* rule0en */
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_RULE0EN_SHIFT 3
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_RULE1EN_MASK  0x1 /* rule1en */
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_RULE1EN_SHIFT 4
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_RULE2EN_MASK  0x1 /* rule2en */
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_RULE2EN_SHIFT 5
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_RULE3EN_MASK  0x1 /* rule3en */
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_RULE3EN_SHIFT 6
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_RULE4EN_MASK  0x1 /* rule4en */
#define E4_YSTORM_ROCE_RESP_CONN_AG_CTX_RULE4EN_SHIFT 7
	u8 byte2 /* byte2 */;
	u8 byte3 /* byte3 */;
	__le16 word0 /* word0 */;
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
	__le16 word1 /* word1 */;
	__le16 word2 /* word2 */;
	__le16 word3 /* word3 */;
	__le16 word4 /* word4 */;
	__le32 reg2 /* reg2 */;
	__le32 reg3 /* reg3 */;
};


struct E5XstormRoceConnAgCtxDqExtLdPart
{
	u8 reserved0 /* cdu_validation */;
	u8 state_and_core_id /* state_and_core_id */;
	u8 flags0;
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_EXIST_IN_QM0_MASK        0x1 /* exist_in_qm0 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_EXIST_IN_QM0_SHIFT       0
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RESERVED1_MASK           0x1 /* exist_in_qm1 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RESERVED1_SHIFT          1
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RESERVED2_MASK           0x1 /* exist_in_qm2 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RESERVED2_SHIFT          2
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_EXIST_IN_QM3_MASK        0x1 /* exist_in_qm3 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_EXIST_IN_QM3_SHIFT       3
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RESERVED3_MASK           0x1 /* bit4 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RESERVED3_SHIFT          4
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RESERVED4_MASK           0x1 /* cf_array_active */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RESERVED4_SHIFT          5
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RESERVED5_MASK           0x1 /* bit6 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RESERVED5_SHIFT          6
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RESERVED6_MASK           0x1 /* bit7 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RESERVED6_SHIFT          7
	u8 flags1;
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RESERVED7_MASK           0x1 /* bit8 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RESERVED7_SHIFT          0
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RESERVED8_MASK           0x1 /* bit9 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RESERVED8_SHIFT          1
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_BIT10_MASK               0x1 /* bit10 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_BIT10_SHIFT              2
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_BIT11_MASK               0x1 /* bit11 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_BIT11_SHIFT              3
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_BIT12_MASK               0x1 /* bit12 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_BIT12_SHIFT              4
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_BIT13_MASK               0x1 /* bit13 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_BIT13_SHIFT              5
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_ERROR_STATE_MASK         0x1 /* bit14 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_ERROR_STATE_SHIFT        6
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_YSTORM_FLUSH_MASK        0x1 /* bit15 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_YSTORM_FLUSH_SHIFT       7
	u8 flags2;
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF0_MASK                 0x3 /* timer0cf */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF0_SHIFT                0
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF1_MASK                 0x3 /* timer1cf */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF1_SHIFT                2
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF2_MASK                 0x3 /* timer2cf */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF2_SHIFT                4
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF3_MASK                 0x3 /* timer_stop_all */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF3_SHIFT                6
	u8 flags3;
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_SQ_FLUSH_CF_MASK         0x3 /* cf4 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_SQ_FLUSH_CF_SHIFT        0
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RX_ERROR_CF_MASK         0x3 /* cf5 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RX_ERROR_CF_SHIFT        2
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_SND_RXMIT_CF_MASK        0x3 /* cf6 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_SND_RXMIT_CF_SHIFT       4
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_FLUSH_Q0_CF_MASK         0x3 /* cf7 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_FLUSH_Q0_CF_SHIFT        6
	u8 flags4;
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF8_MASK                 0x3 /* cf8 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF8_SHIFT                0
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF9_MASK                 0x3 /* cf9 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF9_SHIFT                2
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF10_MASK                0x3 /* cf10 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF10_SHIFT               4
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF11_MASK                0x3 /* cf11 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF11_SHIFT               6
	u8 flags5;
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF12_MASK                0x3 /* cf12 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF12_SHIFT               0
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF13_MASK                0x3 /* cf13 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF13_SHIFT               2
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_FMR_ENDED_CF_MASK        0x3 /* cf14 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_FMR_ENDED_CF_SHIFT       4
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF15_MASK                0x3 /* cf15 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF15_SHIFT               6
	u8 flags6;
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF16_MASK                0x3 /* cf16 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF16_SHIFT               0
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF17_MASK                0x3 /* cf_array_cf */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF17_SHIFT               2
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF18_MASK                0x3 /* cf18 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF18_SHIFT               4
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF19_MASK                0x3 /* cf19 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF19_SHIFT               6
	u8 flags7;
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF20_MASK                0x3 /* cf20 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF20_SHIFT               0
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF21_MASK                0x3 /* cf21 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF21_SHIFT               2
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_SLOW_PATH_MASK           0x3 /* cf22 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_SLOW_PATH_SHIFT          4
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF0EN_MASK               0x1 /* cf0en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF0EN_SHIFT              6
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF1EN_MASK               0x1 /* cf1en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF1EN_SHIFT              7
	u8 flags8;
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF2EN_MASK               0x1 /* cf2en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF2EN_SHIFT              0
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF3EN_MASK               0x1 /* cf3en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF3EN_SHIFT              1
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_SQ_FLUSH_CF_EN_MASK      0x1 /* cf4en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_SQ_FLUSH_CF_EN_SHIFT     2
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RX_ERROR_CF_EN_MASK      0x1 /* cf5en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RX_ERROR_CF_EN_SHIFT     3
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_SND_RXMIT_CF_EN_MASK     0x1 /* cf6en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_SND_RXMIT_CF_EN_SHIFT    4
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_FLUSH_Q0_CF_EN_MASK      0x1 /* cf7en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_FLUSH_Q0_CF_EN_SHIFT     5
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF8EN_MASK               0x1 /* cf8en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF8EN_SHIFT              6
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF9EN_MASK               0x1 /* cf9en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF9EN_SHIFT              7
	u8 flags9;
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF10EN_MASK              0x1 /* cf10en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF10EN_SHIFT             0
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF11EN_MASK              0x1 /* cf11en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF11EN_SHIFT             1
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF12EN_MASK              0x1 /* cf12en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF12EN_SHIFT             2
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF13EN_MASK              0x1 /* cf13en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF13EN_SHIFT             3
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_FME_ENDED_CF_EN_MASK     0x1 /* cf14en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_FME_ENDED_CF_EN_SHIFT    4
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF15EN_MASK              0x1 /* cf15en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF15EN_SHIFT             5
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF16EN_MASK              0x1 /* cf16en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF16EN_SHIFT             6
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF17EN_MASK              0x1 /* cf_array_cf_en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF17EN_SHIFT             7
	u8 flags10;
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF18EN_MASK              0x1 /* cf18en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF18EN_SHIFT             0
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF19EN_MASK              0x1 /* cf19en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF19EN_SHIFT             1
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF20EN_MASK              0x1 /* cf20en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF20EN_SHIFT             2
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF21EN_MASK              0x1 /* cf21en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF21EN_SHIFT             3
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_SLOW_PATH_EN_MASK        0x1 /* cf22en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_SLOW_PATH_EN_SHIFT       4
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF23EN_MASK              0x1 /* cf23en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF23EN_SHIFT             5
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RULE0EN_MASK             0x1 /* rule0en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RULE0EN_SHIFT            6
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RULE1EN_MASK             0x1 /* rule1en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RULE1EN_SHIFT            7
	u8 flags11;
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RULE2EN_MASK             0x1 /* rule2en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RULE2EN_SHIFT            0
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RULE3EN_MASK             0x1 /* rule3en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RULE3EN_SHIFT            1
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RULE4EN_MASK             0x1 /* rule4en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RULE4EN_SHIFT            2
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RULE5EN_MASK             0x1 /* rule5en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RULE5EN_SHIFT            3
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RULE6EN_MASK             0x1 /* rule6en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RULE6EN_SHIFT            4
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_E2E_CREDIT_RULE_EN_MASK  0x1 /* rule7en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_E2E_CREDIT_RULE_EN_SHIFT 5
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED1_MASK        0x1 /* rule8en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED1_SHIFT       6
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RULE9EN_MASK             0x1 /* rule9en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RULE9EN_SHIFT            7
	u8 flags12;
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_SQ_PROD_EN_MASK          0x1 /* rule10en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_SQ_PROD_EN_SHIFT         0
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RULE11EN_MASK            0x1 /* rule11en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RULE11EN_SHIFT           1
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED2_MASK        0x1 /* rule12en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED2_SHIFT       2
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED3_MASK        0x1 /* rule13en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED3_SHIFT       3
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_INV_FENCE_RULE_EN_MASK   0x1 /* rule14en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_INV_FENCE_RULE_EN_SHIFT  4
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RULE15EN_MASK            0x1 /* rule15en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RULE15EN_SHIFT           5
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_ORQ_FENCE_RULE_EN_MASK   0x1 /* rule16en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_ORQ_FENCE_RULE_EN_SHIFT  6
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_MAX_ORD_RULE_EN_MASK     0x1 /* rule17en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_MAX_ORD_RULE_EN_SHIFT    7
	u8 flags13;
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RULE18EN_MASK            0x1 /* rule18en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RULE18EN_SHIFT           0
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RULE19EN_MASK            0x1 /* rule19en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RULE19EN_SHIFT           1
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED4_MASK        0x1 /* rule20en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED4_SHIFT       2
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED5_MASK        0x1 /* rule21en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED5_SHIFT       3
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED6_MASK        0x1 /* rule22en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED6_SHIFT       4
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED7_MASK        0x1 /* rule23en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED7_SHIFT       5
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED8_MASK        0x1 /* rule24en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED8_SHIFT       6
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED9_MASK        0x1 /* rule25en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED9_SHIFT       7
	u8 flags14;
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_MIGRATION_FLAG_MASK      0x1 /* bit16 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_MIGRATION_FLAG_SHIFT     0
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_BIT17_MASK               0x1 /* bit17 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_BIT17_SHIFT              1
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_DPM_PORT_NUM_MASK        0x3 /* bit18 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_DPM_PORT_NUM_SHIFT       2
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RESERVED_MASK            0x1 /* bit20 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_RESERVED_SHIFT           4
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_ROCE_EDPM_ENABLE_MASK    0x1 /* bit21 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_ROCE_EDPM_ENABLE_SHIFT   5
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF23_MASK                0x3 /* cf23 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_CF23_SHIFT               6
	u8 byte2 /* byte2 */;
	__le16 physical_q0 /* physical_q0 */;
	__le16 word1 /* physical_q1 */;
	__le16 sq_cmp_cons /* physical_q2 */;
	__le16 sq_cons /* word3 */;
	__le16 sq_prod /* word4 */;
	__le16 word5 /* word5 */;
	__le16 conn_dpi /* conn_dpi */;
	u8 byte3 /* byte3 */;
	u8 byte4 /* byte4 */;
	u8 byte5 /* byte5 */;
	u8 byte6 /* byte6 */;
	__le32 lsn /* reg0 */;
	__le32 ssn /* reg1 */;
	__le32 snd_una_psn /* reg2 */;
	__le32 snd_nxt_psn /* reg3 */;
	__le32 reg4 /* reg4 */;
	__le32 orq_cons_th /* cf_array0 */;
	__le32 orq_cons /* cf_array1 */;
	u8 flags15;
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_E4_RESERVED1_MASK        0x1 /* bit22 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_E4_RESERVED1_SHIFT       0
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_E4_RESERVED2_MASK        0x1 /* bit23 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_E4_RESERVED2_SHIFT       1
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_E4_RESERVED3_MASK        0x1 /* bit24 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_E4_RESERVED3_SHIFT       2
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_E4_RESERVED4_MASK        0x3 /* cf24 */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_E4_RESERVED4_SHIFT       3
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_E4_RESERVED5_MASK        0x1 /* cf24en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_E4_RESERVED5_SHIFT       5
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_E4_RESERVED6_MASK        0x1 /* rule26en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_E4_RESERVED6_SHIFT       6
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_E4_RESERVED7_MASK        0x1 /* rule27en */
#define E5XSTORMROCECONNAGCTXDQEXTLDPART_E4_RESERVED7_SHIFT       7
	u8 byte7 /* byte7 */;
	__le16 word7 /* word7 */;
	__le16 word8 /* word8 */;
	__le16 word9 /* word9 */;
	__le16 word10 /* word10 */;
	__le16 tx_rdma_edpm_usg_cnt /* word11 */;
	__le32 reg7 /* reg7 */;
	__le32 reg8 /* reg8 */;
	__le32 reg9 /* reg9 */;
	u8 byte8 /* byte8 */;
	u8 byte9 /* byte9 */;
	u8 byte10 /* byte10 */;
	u8 byte11 /* byte11 */;
	u8 byte12 /* byte12 */;
	u8 byte13 /* byte13 */;
	u8 byte14 /* byte14 */;
	u8 byte15 /* byte15 */;
	__le32 reg10 /* reg10 */;
	__le32 reg11 /* reg11 */;
	__le32 reg12 /* reg12 */;
	__le32 reg13 /* reg13 */;
};


struct e5_mstorm_roce_req_conn_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 byte1 /* state_and_core_id */;
	u8 flags0;
#define E5_MSTORM_ROCE_REQ_CONN_AG_CTX_BIT0_MASK     0x1 /* exist_in_qm0 */
#define E5_MSTORM_ROCE_REQ_CONN_AG_CTX_BIT0_SHIFT    0
#define E5_MSTORM_ROCE_REQ_CONN_AG_CTX_BIT1_MASK     0x1 /* exist_in_qm1 */
#define E5_MSTORM_ROCE_REQ_CONN_AG_CTX_BIT1_SHIFT    1
#define E5_MSTORM_ROCE_REQ_CONN_AG_CTX_CF0_MASK      0x3 /* cf0 */
#define E5_MSTORM_ROCE_REQ_CONN_AG_CTX_CF0_SHIFT     2
#define E5_MSTORM_ROCE_REQ_CONN_AG_CTX_CF1_MASK      0x3 /* cf1 */
#define E5_MSTORM_ROCE_REQ_CONN_AG_CTX_CF1_SHIFT     4
#define E5_MSTORM_ROCE_REQ_CONN_AG_CTX_CF2_MASK      0x3 /* cf2 */
#define E5_MSTORM_ROCE_REQ_CONN_AG_CTX_CF2_SHIFT     6
	u8 flags1;
#define E5_MSTORM_ROCE_REQ_CONN_AG_CTX_CF0EN_MASK    0x1 /* cf0en */
#define E5_MSTORM_ROCE_REQ_CONN_AG_CTX_CF0EN_SHIFT   0
#define E5_MSTORM_ROCE_REQ_CONN_AG_CTX_CF1EN_MASK    0x1 /* cf1en */
#define E5_MSTORM_ROCE_REQ_CONN_AG_CTX_CF1EN_SHIFT   1
#define E5_MSTORM_ROCE_REQ_CONN_AG_CTX_CF2EN_MASK    0x1 /* cf2en */
#define E5_MSTORM_ROCE_REQ_CONN_AG_CTX_CF2EN_SHIFT   2
#define E5_MSTORM_ROCE_REQ_CONN_AG_CTX_RULE0EN_MASK  0x1 /* rule0en */
#define E5_MSTORM_ROCE_REQ_CONN_AG_CTX_RULE0EN_SHIFT 3
#define E5_MSTORM_ROCE_REQ_CONN_AG_CTX_RULE1EN_MASK  0x1 /* rule1en */
#define E5_MSTORM_ROCE_REQ_CONN_AG_CTX_RULE1EN_SHIFT 4
#define E5_MSTORM_ROCE_REQ_CONN_AG_CTX_RULE2EN_MASK  0x1 /* rule2en */
#define E5_MSTORM_ROCE_REQ_CONN_AG_CTX_RULE2EN_SHIFT 5
#define E5_MSTORM_ROCE_REQ_CONN_AG_CTX_RULE3EN_MASK  0x1 /* rule3en */
#define E5_MSTORM_ROCE_REQ_CONN_AG_CTX_RULE3EN_SHIFT 6
#define E5_MSTORM_ROCE_REQ_CONN_AG_CTX_RULE4EN_MASK  0x1 /* rule4en */
#define E5_MSTORM_ROCE_REQ_CONN_AG_CTX_RULE4EN_SHIFT 7
	__le16 word0 /* word0 */;
	__le16 word1 /* word1 */;
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
};


struct e5_mstorm_roce_resp_conn_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 byte1 /* state_and_core_id */;
	u8 flags0;
#define E5_MSTORM_ROCE_RESP_CONN_AG_CTX_BIT0_MASK     0x1 /* exist_in_qm0 */
#define E5_MSTORM_ROCE_RESP_CONN_AG_CTX_BIT0_SHIFT    0
#define E5_MSTORM_ROCE_RESP_CONN_AG_CTX_BIT1_MASK     0x1 /* exist_in_qm1 */
#define E5_MSTORM_ROCE_RESP_CONN_AG_CTX_BIT1_SHIFT    1
#define E5_MSTORM_ROCE_RESP_CONN_AG_CTX_CF0_MASK      0x3 /* cf0 */
#define E5_MSTORM_ROCE_RESP_CONN_AG_CTX_CF0_SHIFT     2
#define E5_MSTORM_ROCE_RESP_CONN_AG_CTX_CF1_MASK      0x3 /* cf1 */
#define E5_MSTORM_ROCE_RESP_CONN_AG_CTX_CF1_SHIFT     4
#define E5_MSTORM_ROCE_RESP_CONN_AG_CTX_CF2_MASK      0x3 /* cf2 */
#define E5_MSTORM_ROCE_RESP_CONN_AG_CTX_CF2_SHIFT     6
	u8 flags1;
#define E5_MSTORM_ROCE_RESP_CONN_AG_CTX_CF0EN_MASK    0x1 /* cf0en */
#define E5_MSTORM_ROCE_RESP_CONN_AG_CTX_CF0EN_SHIFT   0
#define E5_MSTORM_ROCE_RESP_CONN_AG_CTX_CF1EN_MASK    0x1 /* cf1en */
#define E5_MSTORM_ROCE_RESP_CONN_AG_CTX_CF1EN_SHIFT   1
#define E5_MSTORM_ROCE_RESP_CONN_AG_CTX_CF2EN_MASK    0x1 /* cf2en */
#define E5_MSTORM_ROCE_RESP_CONN_AG_CTX_CF2EN_SHIFT   2
#define E5_MSTORM_ROCE_RESP_CONN_AG_CTX_RULE0EN_MASK  0x1 /* rule0en */
#define E5_MSTORM_ROCE_RESP_CONN_AG_CTX_RULE0EN_SHIFT 3
#define E5_MSTORM_ROCE_RESP_CONN_AG_CTX_RULE1EN_MASK  0x1 /* rule1en */
#define E5_MSTORM_ROCE_RESP_CONN_AG_CTX_RULE1EN_SHIFT 4
#define E5_MSTORM_ROCE_RESP_CONN_AG_CTX_RULE2EN_MASK  0x1 /* rule2en */
#define E5_MSTORM_ROCE_RESP_CONN_AG_CTX_RULE2EN_SHIFT 5
#define E5_MSTORM_ROCE_RESP_CONN_AG_CTX_RULE3EN_MASK  0x1 /* rule3en */
#define E5_MSTORM_ROCE_RESP_CONN_AG_CTX_RULE3EN_SHIFT 6
#define E5_MSTORM_ROCE_RESP_CONN_AG_CTX_RULE4EN_MASK  0x1 /* rule4en */
#define E5_MSTORM_ROCE_RESP_CONN_AG_CTX_RULE4EN_SHIFT 7
	__le16 word0 /* word0 */;
	__le16 word1 /* word1 */;
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
};


struct e5_tstorm_roce_req_conn_ag_ctx
{
	u8 reserved0 /* cdu_validation */;
	u8 state_and_core_id /* state_and_core_id */;
	u8 flags0;
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_EXIST_IN_QM0_MASK                0x1 /* exist_in_qm0 */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_EXIST_IN_QM0_SHIFT               0
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_RX_ERROR_OCCURED_MASK            0x1 /* exist_in_qm1 */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_RX_ERROR_OCCURED_SHIFT           1
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_TX_CQE_ERROR_OCCURED_MASK        0x1 /* bit2 */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_TX_CQE_ERROR_OCCURED_SHIFT       2
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_BIT3_MASK                        0x1 /* bit3 */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_BIT3_SHIFT                       3
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_MSTORM_FLUSH_MASK                0x1 /* bit4 */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_MSTORM_FLUSH_SHIFT               4
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_CACHED_ORQ_MASK                  0x1 /* bit5 */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_CACHED_ORQ_SHIFT                 5
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_TIMER_CF_MASK                    0x3 /* timer0cf */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_TIMER_CF_SHIFT                   6
	u8 flags1;
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_CF1_MASK                         0x3 /* timer1cf */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_CF1_SHIFT                        0
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_SQ_CF_MASK                 0x3 /* timer2cf */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_SQ_CF_SHIFT                2
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_TIMER_STOP_ALL_CF_MASK           0x3 /* timer_stop_all */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_TIMER_STOP_ALL_CF_SHIFT          4
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_Q0_CF_MASK                 0x3 /* cf4 */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_Q0_CF_SHIFT                6
	u8 flags2;
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_MSTORM_FLUSH_CF_MASK             0x3 /* cf5 */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_MSTORM_FLUSH_CF_SHIFT            0
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_SET_TIMER_CF_MASK                0x3 /* cf6 */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_SET_TIMER_CF_SHIFT               2
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_TX_ASYNC_ERROR_CF_MASK           0x3 /* cf7 */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_TX_ASYNC_ERROR_CF_SHIFT          4
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_RXMIT_DONE_CF_MASK               0x3 /* cf8 */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_RXMIT_DONE_CF_SHIFT              6
	u8 flags3;
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_ERROR_SCAN_COMPLETED_CF_MASK     0x3 /* cf9 */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_ERROR_SCAN_COMPLETED_CF_SHIFT    0
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_SQ_DRAIN_COMPLETED_CF_MASK       0x3 /* cf10 */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_SQ_DRAIN_COMPLETED_CF_SHIFT      2
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_TIMER_CF_EN_MASK                 0x1 /* cf0en */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_TIMER_CF_EN_SHIFT                4
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_CF1EN_MASK                       0x1 /* cf1en */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_CF1EN_SHIFT                      5
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_SQ_CF_EN_MASK              0x1 /* cf2en */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_SQ_CF_EN_SHIFT             6
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_TIMER_STOP_ALL_CF_EN_MASK        0x1 /* cf3en */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_TIMER_STOP_ALL_CF_EN_SHIFT       7
	u8 flags4;
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_Q0_CF_EN_MASK              0x1 /* cf4en */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_Q0_CF_EN_SHIFT             0
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_MSTORM_FLUSH_CF_EN_MASK          0x1 /* cf5en */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_MSTORM_FLUSH_CF_EN_SHIFT         1
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_SET_TIMER_CF_EN_MASK             0x1 /* cf6en */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_SET_TIMER_CF_EN_SHIFT            2
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_TX_ASYNC_ERROR_CF_EN_MASK        0x1 /* cf7en */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_TX_ASYNC_ERROR_CF_EN_SHIFT       3
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_RXMIT_DONE_CF_EN_MASK            0x1 /* cf8en */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_RXMIT_DONE_CF_EN_SHIFT           4
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_ERROR_SCAN_COMPLETED_CF_EN_MASK  0x1 /* cf9en */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_ERROR_SCAN_COMPLETED_CF_EN_SHIFT 5
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_SQ_DRAIN_COMPLETED_CF_EN_MASK    0x1 /* cf10en */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_SQ_DRAIN_COMPLETED_CF_EN_SHIFT   6
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE0EN_MASK                     0x1 /* rule0en */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE0EN_SHIFT                    7
	u8 flags5;
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE1EN_MASK                     0x1 /* rule1en */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE1EN_SHIFT                    0
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE2EN_MASK                     0x1 /* rule2en */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE2EN_SHIFT                    1
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE3EN_MASK                     0x1 /* rule3en */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE3EN_SHIFT                    2
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE4EN_MASK                     0x1 /* rule4en */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE4EN_SHIFT                    3
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE5EN_MASK                     0x1 /* rule5en */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE5EN_SHIFT                    4
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_SND_SQ_CONS_EN_MASK              0x1 /* rule6en */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_SND_SQ_CONS_EN_SHIFT             5
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE7EN_MASK                     0x1 /* rule7en */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE7EN_SHIFT                    6
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE8EN_MASK                     0x1 /* rule8en */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_RULE8EN_SHIFT                    7
	u8 flags6;
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_E4_RESERVED1_MASK                0x1 /* bit6 */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_E4_RESERVED1_SHIFT               0
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_E4_RESERVED2_MASK                0x1 /* bit7 */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_E4_RESERVED2_SHIFT               1
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_E4_RESERVED3_MASK                0x1 /* bit8 */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_E4_RESERVED3_SHIFT               2
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_E4_RESERVED4_MASK                0x3 /* cf11 */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_E4_RESERVED4_SHIFT               3
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_E4_RESERVED5_MASK                0x1 /* cf11en */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_E4_RESERVED5_SHIFT               5
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_E4_RESERVED6_MASK                0x1 /* rule9en */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_E4_RESERVED6_SHIFT               6
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_E4_RESERVED7_MASK                0x1 /* rule10en */
#define E5_TSTORM_ROCE_REQ_CONN_AG_CTX_E4_RESERVED7_SHIFT               7
	u8 tx_cqe_error_type /* byte2 */;
	__le16 snd_sq_cons_th /* word0 */;
	__le32 reg0 /* reg0 */;
	__le32 snd_nxt_psn /* reg1 */;
	__le32 snd_max_psn /* reg2 */;
	__le32 orq_prod /* reg3 */;
	__le32 reg4 /* reg4 */;
	__le32 reg5 /* reg5 */;
	__le32 reg6 /* reg6 */;
	__le32 reg7 /* reg7 */;
	__le32 reg8 /* reg8 */;
	u8 orq_cache_idx /* byte3 */;
	u8 byte4 /* byte4 */;
	u8 byte5 /* byte5 */;
	u8 e4_reserved8 /* byte6 */;
	__le16 snd_sq_cons /* word1 */;
	__le16 word2 /* conn_dpi */;
	__le32 reg9 /* reg9 */;
	__le16 word3 /* word3 */;
	__le16 e4_reserved9 /* word4 */;
};


struct e5_tstorm_roce_resp_conn_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 state_and_core_id /* state_and_core_id */;
	u8 flags0;
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_EXIST_IN_QM0_MASK               0x1 /* exist_in_qm0 */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_EXIST_IN_QM0_SHIFT              0
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_RX_ERROR_NOTIFY_REQUESTER_MASK  0x1 /* exist_in_qm1 */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_RX_ERROR_NOTIFY_REQUESTER_SHIFT 1
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_BIT2_MASK                       0x1 /* bit2 */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_BIT2_SHIFT                      2
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_BIT3_MASK                       0x1 /* bit3 */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_BIT3_SHIFT                      3
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_MSTORM_FLUSH_MASK               0x1 /* bit4 */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_MSTORM_FLUSH_SHIFT              4
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_BIT5_MASK                       0x1 /* bit5 */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_BIT5_SHIFT                      5
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_CF0_MASK                        0x3 /* timer0cf */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_CF0_SHIFT                       6
	u8 flags1;
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_RX_ERROR_CF_MASK                0x3 /* timer1cf */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_RX_ERROR_CF_SHIFT               0
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_TX_ERROR_CF_MASK                0x3 /* timer2cf */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_TX_ERROR_CF_SHIFT               2
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_CF3_MASK                        0x3 /* timer_stop_all */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_CF3_SHIFT                       4
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_FLUSH_Q0_CF_MASK                0x3 /* cf4 */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_FLUSH_Q0_CF_SHIFT               6
	u8 flags2;
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_MSTORM_FLUSH_CF_MASK            0x3 /* cf5 */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_MSTORM_FLUSH_CF_SHIFT           0
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_CF6_MASK                        0x3 /* cf6 */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_CF6_SHIFT                       2
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_CF7_MASK                        0x3 /* cf7 */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_CF7_SHIFT                       4
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_CF8_MASK                        0x3 /* cf8 */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_CF8_SHIFT                       6
	u8 flags3;
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_CF9_MASK                        0x3 /* cf9 */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_CF9_SHIFT                       0
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_CF10_MASK                       0x3 /* cf10 */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_CF10_SHIFT                      2
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_CF0EN_MASK                      0x1 /* cf0en */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_CF0EN_SHIFT                     4
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_RX_ERROR_CF_EN_MASK             0x1 /* cf1en */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_RX_ERROR_CF_EN_SHIFT            5
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_TX_ERROR_CF_EN_MASK             0x1 /* cf2en */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_TX_ERROR_CF_EN_SHIFT            6
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_CF3EN_MASK                      0x1 /* cf3en */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_CF3EN_SHIFT                     7
	u8 flags4;
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_FLUSH_Q0_CF_EN_MASK             0x1 /* cf4en */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_FLUSH_Q0_CF_EN_SHIFT            0
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_MSTORM_FLUSH_CF_EN_MASK         0x1 /* cf5en */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_MSTORM_FLUSH_CF_EN_SHIFT        1
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_CF6EN_MASK                      0x1 /* cf6en */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_CF6EN_SHIFT                     2
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_CF7EN_MASK                      0x1 /* cf7en */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_CF7EN_SHIFT                     3
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_CF8EN_MASK                      0x1 /* cf8en */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_CF8EN_SHIFT                     4
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_CF9EN_MASK                      0x1 /* cf9en */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_CF9EN_SHIFT                     5
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_CF10EN_MASK                     0x1 /* cf10en */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_CF10EN_SHIFT                    6
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE0EN_MASK                    0x1 /* rule0en */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE0EN_SHIFT                   7
	u8 flags5;
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE1EN_MASK                    0x1 /* rule1en */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE1EN_SHIFT                   0
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE2EN_MASK                    0x1 /* rule2en */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE2EN_SHIFT                   1
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE3EN_MASK                    0x1 /* rule3en */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE3EN_SHIFT                   2
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE4EN_MASK                    0x1 /* rule4en */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE4EN_SHIFT                   3
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE5EN_MASK                    0x1 /* rule5en */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE5EN_SHIFT                   4
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_RQ_RULE_EN_MASK                 0x1 /* rule6en */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_RQ_RULE_EN_SHIFT                5
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE7EN_MASK                    0x1 /* rule7en */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE7EN_SHIFT                   6
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE8EN_MASK                    0x1 /* rule8en */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_RULE8EN_SHIFT                   7
	u8 flags6;
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_E4_RESERVED1_MASK               0x1 /* bit6 */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_E4_RESERVED1_SHIFT              0
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_E4_RESERVED2_MASK               0x1 /* bit7 */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_E4_RESERVED2_SHIFT              1
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_E4_RESERVED3_MASK               0x1 /* bit8 */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_E4_RESERVED3_SHIFT              2
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_E4_RESERVED4_MASK               0x3 /* cf11 */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_E4_RESERVED4_SHIFT              3
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_E4_RESERVED5_MASK               0x1 /* cf11en */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_E4_RESERVED5_SHIFT              5
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_E4_RESERVED6_MASK               0x1 /* rule9en */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_E4_RESERVED6_SHIFT              6
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_E4_RESERVED7_MASK               0x1 /* rule10en */
#define E5_TSTORM_ROCE_RESP_CONN_AG_CTX_E4_RESERVED7_SHIFT              7
	u8 tx_async_error_type /* byte2 */;
	__le16 rq_cons /* word0 */;
	__le32 psn_and_rxmit_id_echo /* reg0 */;
	__le32 reg1 /* reg1 */;
	__le32 reg2 /* reg2 */;
	__le32 reg3 /* reg3 */;
	__le32 reg4 /* reg4 */;
	__le32 reg5 /* reg5 */;
	__le32 reg6 /* reg6 */;
	__le32 reg7 /* reg7 */;
	__le32 reg8 /* reg8 */;
	u8 byte3 /* byte3 */;
	u8 byte4 /* byte4 */;
	u8 byte5 /* byte5 */;
	u8 e4_reserved8 /* byte6 */;
	__le16 rq_prod /* word1 */;
	__le16 conn_dpi /* conn_dpi */;
	__le32 num_invlidated_mw /* reg9 */;
	__le16 irq_cons /* word3 */;
	__le16 e4_reserved9 /* word4 */;
};


struct e5_ustorm_roce_req_conn_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 byte1 /* state_and_core_id */;
	u8 flags0;
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_BIT0_MASK          0x1 /* exist_in_qm0 */
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_BIT0_SHIFT         0
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_BIT1_MASK          0x1 /* exist_in_qm1 */
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_BIT1_SHIFT         1
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_CF0_MASK           0x3 /* timer0cf */
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_CF0_SHIFT          2
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_CF1_MASK           0x3 /* timer1cf */
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_CF1_SHIFT          4
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_CF2_MASK           0x3 /* timer2cf */
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_CF2_SHIFT          6
	u8 flags1;
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_CF3_MASK           0x3 /* timer_stop_all */
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_CF3_SHIFT          0
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_CF4_MASK           0x3 /* cf4 */
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_CF4_SHIFT          2
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_CF5_MASK           0x3 /* cf5 */
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_CF5_SHIFT          4
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_CF6_MASK           0x3 /* cf6 */
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_CF6_SHIFT          6
	u8 flags2;
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_CF0EN_MASK         0x1 /* cf0en */
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_CF0EN_SHIFT        0
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_CF1EN_MASK         0x1 /* cf1en */
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_CF1EN_SHIFT        1
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_CF2EN_MASK         0x1 /* cf2en */
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_CF2EN_SHIFT        2
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_CF3EN_MASK         0x1 /* cf3en */
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_CF3EN_SHIFT        3
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_CF4EN_MASK         0x1 /* cf4en */
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_CF4EN_SHIFT        4
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_CF5EN_MASK         0x1 /* cf5en */
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_CF5EN_SHIFT        5
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_CF6EN_MASK         0x1 /* cf6en */
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_CF6EN_SHIFT        6
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_RULE0EN_MASK       0x1 /* rule0en */
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_RULE0EN_SHIFT      7
	u8 flags3;
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_RULE1EN_MASK       0x1 /* rule1en */
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_RULE1EN_SHIFT      0
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_RULE2EN_MASK       0x1 /* rule2en */
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_RULE2EN_SHIFT      1
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_RULE3EN_MASK       0x1 /* rule3en */
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_RULE3EN_SHIFT      2
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_RULE4EN_MASK       0x1 /* rule4en */
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_RULE4EN_SHIFT      3
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_RULE5EN_MASK       0x1 /* rule5en */
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_RULE5EN_SHIFT      4
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_RULE6EN_MASK       0x1 /* rule6en */
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_RULE6EN_SHIFT      5
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_RULE7EN_MASK       0x1 /* rule7en */
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_RULE7EN_SHIFT      6
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_RULE8EN_MASK       0x1 /* rule8en */
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_RULE8EN_SHIFT      7
	u8 flags4;
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_E4_RESERVED1_MASK  0x1 /* bit2 */
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_E4_RESERVED1_SHIFT 0
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_E4_RESERVED2_MASK  0x1 /* bit3 */
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_E4_RESERVED2_SHIFT 1
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_E4_RESERVED3_MASK  0x3 /* cf7 */
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_E4_RESERVED3_SHIFT 2
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_E4_RESERVED4_MASK  0x3 /* cf8 */
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_E4_RESERVED4_SHIFT 4
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_E4_RESERVED5_MASK  0x1 /* cf7en */
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_E4_RESERVED5_SHIFT 6
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_E4_RESERVED6_MASK  0x1 /* cf8en */
#define E5_USTORM_ROCE_REQ_CONN_AG_CTX_E4_RESERVED6_SHIFT 7
	u8 byte2 /* byte2 */;
	__le16 word0 /* conn_dpi */;
	__le16 word1 /* word1 */;
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
	__le32 reg2 /* reg2 */;
	__le32 reg3 /* reg3 */;
	__le16 word2 /* word2 */;
	__le16 word3 /* word3 */;
};


struct e5_ustorm_roce_resp_conn_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 byte1 /* state_and_core_id */;
	u8 flags0;
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_BIT0_MASK          0x1 /* exist_in_qm0 */
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_BIT0_SHIFT         0
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_BIT1_MASK          0x1 /* exist_in_qm1 */
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_BIT1_SHIFT         1
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_CF0_MASK           0x3 /* timer0cf */
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_CF0_SHIFT          2
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_CF1_MASK           0x3 /* timer1cf */
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_CF1_SHIFT          4
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_CF2_MASK           0x3 /* timer2cf */
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_CF2_SHIFT          6
	u8 flags1;
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_CF3_MASK           0x3 /* timer_stop_all */
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_CF3_SHIFT          0
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_CF4_MASK           0x3 /* cf4 */
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_CF4_SHIFT          2
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_CF5_MASK           0x3 /* cf5 */
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_CF5_SHIFT          4
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_CF6_MASK           0x3 /* cf6 */
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_CF6_SHIFT          6
	u8 flags2;
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_CF0EN_MASK         0x1 /* cf0en */
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_CF0EN_SHIFT        0
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_CF1EN_MASK         0x1 /* cf1en */
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_CF1EN_SHIFT        1
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_CF2EN_MASK         0x1 /* cf2en */
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_CF2EN_SHIFT        2
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_CF3EN_MASK         0x1 /* cf3en */
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_CF3EN_SHIFT        3
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_CF4EN_MASK         0x1 /* cf4en */
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_CF4EN_SHIFT        4
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_CF5EN_MASK         0x1 /* cf5en */
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_CF5EN_SHIFT        5
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_CF6EN_MASK         0x1 /* cf6en */
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_CF6EN_SHIFT        6
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_RULE0EN_MASK       0x1 /* rule0en */
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_RULE0EN_SHIFT      7
	u8 flags3;
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_RULE1EN_MASK       0x1 /* rule1en */
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_RULE1EN_SHIFT      0
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_RULE2EN_MASK       0x1 /* rule2en */
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_RULE2EN_SHIFT      1
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_RULE3EN_MASK       0x1 /* rule3en */
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_RULE3EN_SHIFT      2
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_RULE4EN_MASK       0x1 /* rule4en */
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_RULE4EN_SHIFT      3
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_RULE5EN_MASK       0x1 /* rule5en */
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_RULE5EN_SHIFT      4
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_RULE6EN_MASK       0x1 /* rule6en */
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_RULE6EN_SHIFT      5
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_RULE7EN_MASK       0x1 /* rule7en */
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_RULE7EN_SHIFT      6
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_RULE8EN_MASK       0x1 /* rule8en */
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_RULE8EN_SHIFT      7
	u8 flags4;
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_E4_RESERVED1_MASK  0x1 /* bit2 */
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_E4_RESERVED1_SHIFT 0
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_E4_RESERVED2_MASK  0x1 /* bit3 */
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_E4_RESERVED2_SHIFT 1
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_E4_RESERVED3_MASK  0x3 /* cf7 */
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_E4_RESERVED3_SHIFT 2
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_E4_RESERVED4_MASK  0x3 /* cf8 */
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_E4_RESERVED4_SHIFT 4
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_E4_RESERVED5_MASK  0x1 /* cf7en */
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_E4_RESERVED5_SHIFT 6
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_E4_RESERVED6_MASK  0x1 /* cf8en */
#define E5_USTORM_ROCE_RESP_CONN_AG_CTX_E4_RESERVED6_SHIFT 7
	u8 byte2 /* byte2 */;
	__le16 word0 /* conn_dpi */;
	__le16 word1 /* word1 */;
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
	__le32 reg2 /* reg2 */;
	__le32 reg3 /* reg3 */;
	__le16 word2 /* word2 */;
	__le16 word3 /* word3 */;
};


struct e5_xstorm_roce_req_conn_ag_ctx
{
	u8 reserved0 /* cdu_validation */;
	u8 state_and_core_id /* state_and_core_id */;
	u8 flags0;
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_EXIST_IN_QM0_MASK        0x1 /* exist_in_qm0 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_EXIST_IN_QM0_SHIFT       0
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED1_MASK           0x1 /* exist_in_qm1 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED1_SHIFT          1
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED2_MASK           0x1 /* exist_in_qm2 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED2_SHIFT          2
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_EXIST_IN_QM3_MASK        0x1 /* exist_in_qm3 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_EXIST_IN_QM3_SHIFT       3
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED3_MASK           0x1 /* bit4 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED3_SHIFT          4
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED4_MASK           0x1 /* cf_array_active */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED4_SHIFT          5
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED5_MASK           0x1 /* bit6 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED5_SHIFT          6
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED6_MASK           0x1 /* bit7 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED6_SHIFT          7
	u8 flags1;
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED7_MASK           0x1 /* bit8 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED7_SHIFT          0
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED8_MASK           0x1 /* bit9 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED8_SHIFT          1
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_BIT10_MASK               0x1 /* bit10 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_BIT10_SHIFT              2
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_BIT11_MASK               0x1 /* bit11 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_BIT11_SHIFT              3
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_BIT12_MASK               0x1 /* bit12 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_BIT12_SHIFT              4
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_BIT13_MASK               0x1 /* bit13 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_BIT13_SHIFT              5
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_ERROR_STATE_MASK         0x1 /* bit14 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_ERROR_STATE_SHIFT        6
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_YSTORM_FLUSH_MASK        0x1 /* bit15 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_YSTORM_FLUSH_SHIFT       7
	u8 flags2;
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF0_MASK                 0x3 /* timer0cf */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF0_SHIFT                0
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF1_MASK                 0x3 /* timer1cf */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF1_SHIFT                2
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF2_MASK                 0x3 /* timer2cf */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF2_SHIFT                4
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF3_MASK                 0x3 /* timer_stop_all */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF3_SHIFT                6
	u8 flags3;
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_SQ_FLUSH_CF_MASK         0x3 /* cf4 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_SQ_FLUSH_CF_SHIFT        0
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RX_ERROR_CF_MASK         0x3 /* cf5 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RX_ERROR_CF_SHIFT        2
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_SND_RXMIT_CF_MASK        0x3 /* cf6 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_SND_RXMIT_CF_SHIFT       4
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_Q0_CF_MASK         0x3 /* cf7 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_Q0_CF_SHIFT        6
	u8 flags4;
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF8_MASK                 0x3 /* cf8 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF8_SHIFT                0
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF9_MASK                 0x3 /* cf9 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF9_SHIFT                2
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF10_MASK                0x3 /* cf10 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF10_SHIFT               4
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF11_MASK                0x3 /* cf11 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF11_SHIFT               6
	u8 flags5;
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF12_MASK                0x3 /* cf12 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF12_SHIFT               0
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF13_MASK                0x3 /* cf13 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF13_SHIFT               2
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_FMR_ENDED_CF_MASK        0x3 /* cf14 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_FMR_ENDED_CF_SHIFT       4
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF15_MASK                0x3 /* cf15 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF15_SHIFT               6
	u8 flags6;
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF16_MASK                0x3 /* cf16 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF16_SHIFT               0
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF17_MASK                0x3 /* cf_array_cf */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF17_SHIFT               2
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF18_MASK                0x3 /* cf18 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF18_SHIFT               4
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF19_MASK                0x3 /* cf19 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF19_SHIFT               6
	u8 flags7;
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF20_MASK                0x3 /* cf20 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF20_SHIFT               0
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF21_MASK                0x3 /* cf21 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF21_SHIFT               2
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_SLOW_PATH_MASK           0x3 /* cf22 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_SLOW_PATH_SHIFT          4
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF0EN_MASK               0x1 /* cf0en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF0EN_SHIFT              6
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF1EN_MASK               0x1 /* cf1en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF1EN_SHIFT              7
	u8 flags8;
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF2EN_MASK               0x1 /* cf2en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF2EN_SHIFT              0
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF3EN_MASK               0x1 /* cf3en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF3EN_SHIFT              1
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_SQ_FLUSH_CF_EN_MASK      0x1 /* cf4en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_SQ_FLUSH_CF_EN_SHIFT     2
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RX_ERROR_CF_EN_MASK      0x1 /* cf5en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RX_ERROR_CF_EN_SHIFT     3
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_SND_RXMIT_CF_EN_MASK     0x1 /* cf6en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_SND_RXMIT_CF_EN_SHIFT    4
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_Q0_CF_EN_MASK      0x1 /* cf7en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_FLUSH_Q0_CF_EN_SHIFT     5
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF8EN_MASK               0x1 /* cf8en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF8EN_SHIFT              6
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF9EN_MASK               0x1 /* cf9en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF9EN_SHIFT              7
	u8 flags9;
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF10EN_MASK              0x1 /* cf10en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF10EN_SHIFT             0
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF11EN_MASK              0x1 /* cf11en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF11EN_SHIFT             1
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF12EN_MASK              0x1 /* cf12en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF12EN_SHIFT             2
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF13EN_MASK              0x1 /* cf13en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF13EN_SHIFT             3
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_FME_ENDED_CF_EN_MASK     0x1 /* cf14en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_FME_ENDED_CF_EN_SHIFT    4
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF15EN_MASK              0x1 /* cf15en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF15EN_SHIFT             5
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF16EN_MASK              0x1 /* cf16en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF16EN_SHIFT             6
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF17EN_MASK              0x1 /* cf_array_cf_en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF17EN_SHIFT             7
	u8 flags10;
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF18EN_MASK              0x1 /* cf18en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF18EN_SHIFT             0
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF19EN_MASK              0x1 /* cf19en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF19EN_SHIFT             1
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF20EN_MASK              0x1 /* cf20en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF20EN_SHIFT             2
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF21EN_MASK              0x1 /* cf21en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF21EN_SHIFT             3
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_SLOW_PATH_EN_MASK        0x1 /* cf22en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_SLOW_PATH_EN_SHIFT       4
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF23EN_MASK              0x1 /* cf23en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF23EN_SHIFT             5
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE0EN_MASK             0x1 /* rule0en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE0EN_SHIFT            6
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE1EN_MASK             0x1 /* rule1en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE1EN_SHIFT            7
	u8 flags11;
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE2EN_MASK             0x1 /* rule2en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE2EN_SHIFT            0
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE3EN_MASK             0x1 /* rule3en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE3EN_SHIFT            1
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE4EN_MASK             0x1 /* rule4en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE4EN_SHIFT            2
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE5EN_MASK             0x1 /* rule5en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE5EN_SHIFT            3
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE6EN_MASK             0x1 /* rule6en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE6EN_SHIFT            4
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_E2E_CREDIT_RULE_EN_MASK  0x1 /* rule7en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_E2E_CREDIT_RULE_EN_SHIFT 5
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED1_MASK        0x1 /* rule8en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED1_SHIFT       6
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE9EN_MASK             0x1 /* rule9en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE9EN_SHIFT            7
	u8 flags12;
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_SQ_PROD_EN_MASK          0x1 /* rule10en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_SQ_PROD_EN_SHIFT         0
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE11EN_MASK            0x1 /* rule11en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE11EN_SHIFT           1
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED2_MASK        0x1 /* rule12en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED2_SHIFT       2
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED3_MASK        0x1 /* rule13en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED3_SHIFT       3
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_INV_FENCE_RULE_EN_MASK   0x1 /* rule14en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_INV_FENCE_RULE_EN_SHIFT  4
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE15EN_MASK            0x1 /* rule15en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE15EN_SHIFT           5
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_ORQ_FENCE_RULE_EN_MASK   0x1 /* rule16en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_ORQ_FENCE_RULE_EN_SHIFT  6
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_MAX_ORD_RULE_EN_MASK     0x1 /* rule17en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_MAX_ORD_RULE_EN_SHIFT    7
	u8 flags13;
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE18EN_MASK            0x1 /* rule18en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE18EN_SHIFT           0
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE19EN_MASK            0x1 /* rule19en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RULE19EN_SHIFT           1
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED4_MASK        0x1 /* rule20en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED4_SHIFT       2
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED5_MASK        0x1 /* rule21en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED5_SHIFT       3
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED6_MASK        0x1 /* rule22en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED6_SHIFT       4
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED7_MASK        0x1 /* rule23en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED7_SHIFT       5
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED8_MASK        0x1 /* rule24en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED8_SHIFT       6
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED9_MASK        0x1 /* rule25en */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_A0_RESERVED9_SHIFT       7
	u8 flags14;
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_MIGRATION_FLAG_MASK      0x1 /* bit16 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_MIGRATION_FLAG_SHIFT     0
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_BIT17_MASK               0x1 /* bit17 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_BIT17_SHIFT              1
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_DPM_PORT_NUM_MASK        0x3 /* bit18 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_DPM_PORT_NUM_SHIFT       2
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED_MASK            0x1 /* bit20 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_RESERVED_SHIFT           4
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_ROCE_EDPM_ENABLE_MASK    0x1 /* bit21 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_ROCE_EDPM_ENABLE_SHIFT   5
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF23_MASK                0x3 /* cf23 */
#define E5_XSTORM_ROCE_REQ_CONN_AG_CTX_CF23_SHIFT               6
	u8 byte2 /* byte2 */;
	__le16 physical_q0 /* physical_q0 */;
	__le16 word1 /* physical_q1 */;
	__le16 sq_cmp_cons /* physical_q2 */;
	__le16 sq_cons /* word3 */;
	__le16 sq_prod /* word4 */;
	__le16 word5 /* word5 */;
	__le16 conn_dpi /* conn_dpi */;
	u8 byte3 /* byte3 */;
	u8 byte4 /* byte4 */;
	u8 byte5 /* byte5 */;
	u8 byte6 /* byte6 */;
	__le32 lsn /* reg0 */;
	__le32 ssn /* reg1 */;
	__le32 snd_una_psn /* reg2 */;
	__le32 snd_nxt_psn /* reg3 */;
	__le32 reg4 /* reg4 */;
	__le32 orq_cons_th /* cf_array0 */;
	__le32 orq_cons /* cf_array1 */;
};


struct e5_xstorm_roce_resp_conn_ag_ctx
{
	u8 reserved0 /* cdu_validation */;
	u8 state_and_core_id /* state_and_core_id */;
	u8 flags0;
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_EXIST_IN_QM0_MASK      0x1 /* exist_in_qm0 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_EXIST_IN_QM0_SHIFT     0
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED1_MASK         0x1 /* exist_in_qm1 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED1_SHIFT        1
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED2_MASK         0x1 /* exist_in_qm2 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED2_SHIFT        2
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_EXIST_IN_QM3_MASK      0x1 /* exist_in_qm3 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_EXIST_IN_QM3_SHIFT     3
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED3_MASK         0x1 /* bit4 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED3_SHIFT        4
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED4_MASK         0x1 /* cf_array_active */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED4_SHIFT        5
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED5_MASK         0x1 /* bit6 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED5_SHIFT        6
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED6_MASK         0x1 /* bit7 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED6_SHIFT        7
	u8 flags1;
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED7_MASK         0x1 /* bit8 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED7_SHIFT        0
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED8_MASK         0x1 /* bit9 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RESERVED8_SHIFT        1
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT10_MASK             0x1 /* bit10 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT10_SHIFT            2
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT11_MASK             0x1 /* bit11 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT11_SHIFT            3
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT12_MASK             0x1 /* bit12 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT12_SHIFT            4
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT13_MASK             0x1 /* bit13 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT13_SHIFT            5
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_ERROR_STATE_MASK       0x1 /* bit14 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_ERROR_STATE_SHIFT      6
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_YSTORM_FLUSH_MASK      0x1 /* bit15 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_YSTORM_FLUSH_SHIFT     7
	u8 flags2;
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF0_MASK               0x3 /* timer0cf */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF0_SHIFT              0
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF1_MASK               0x3 /* timer1cf */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF1_SHIFT              2
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF2_MASK               0x3 /* timer2cf */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF2_SHIFT              4
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF3_MASK               0x3 /* timer_stop_all */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF3_SHIFT              6
	u8 flags3;
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RXMIT_CF_MASK          0x3 /* cf4 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RXMIT_CF_SHIFT         0
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RX_ERROR_CF_MASK       0x3 /* cf5 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RX_ERROR_CF_SHIFT      2
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_FORCE_ACK_CF_MASK      0x3 /* cf6 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_FORCE_ACK_CF_SHIFT     4
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_FLUSH_Q0_CF_MASK       0x3 /* cf7 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_FLUSH_Q0_CF_SHIFT      6
	u8 flags4;
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF8_MASK               0x3 /* cf8 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF8_SHIFT              0
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF9_MASK               0x3 /* cf9 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF9_SHIFT              2
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF10_MASK              0x3 /* cf10 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF10_SHIFT             4
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF11_MASK              0x3 /* cf11 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF11_SHIFT             6
	u8 flags5;
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF12_MASK              0x3 /* cf12 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF12_SHIFT             0
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF13_MASK              0x3 /* cf13 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF13_SHIFT             2
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF14_MASK              0x3 /* cf14 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF14_SHIFT             4
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF15_MASK              0x3 /* cf15 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF15_SHIFT             6
	u8 flags6;
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF16_MASK              0x3 /* cf16 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF16_SHIFT             0
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF17_MASK              0x3 /* cf_array_cf */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF17_SHIFT             2
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF18_MASK              0x3 /* cf18 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF18_SHIFT             4
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF19_MASK              0x3 /* cf19 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF19_SHIFT             6
	u8 flags7;
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF20_MASK              0x3 /* cf20 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF20_SHIFT             0
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF21_MASK              0x3 /* cf21 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF21_SHIFT             2
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_SLOW_PATH_MASK         0x3 /* cf22 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_SLOW_PATH_SHIFT        4
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF0EN_MASK             0x1 /* cf0en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF0EN_SHIFT            6
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF1EN_MASK             0x1 /* cf1en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF1EN_SHIFT            7
	u8 flags8;
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF2EN_MASK             0x1 /* cf2en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF2EN_SHIFT            0
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF3EN_MASK             0x1 /* cf3en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF3EN_SHIFT            1
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RXMIT_CF_EN_MASK       0x1 /* cf4en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RXMIT_CF_EN_SHIFT      2
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RX_ERROR_CF_EN_MASK    0x1 /* cf5en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RX_ERROR_CF_EN_SHIFT   3
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_FORCE_ACK_CF_EN_MASK   0x1 /* cf6en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_FORCE_ACK_CF_EN_SHIFT  4
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_FLUSH_Q0_CF_EN_MASK    0x1 /* cf7en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_FLUSH_Q0_CF_EN_SHIFT   5
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF8EN_MASK             0x1 /* cf8en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF8EN_SHIFT            6
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF9EN_MASK             0x1 /* cf9en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF9EN_SHIFT            7
	u8 flags9;
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF10EN_MASK            0x1 /* cf10en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF10EN_SHIFT           0
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF11EN_MASK            0x1 /* cf11en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF11EN_SHIFT           1
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF12EN_MASK            0x1 /* cf12en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF12EN_SHIFT           2
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF13EN_MASK            0x1 /* cf13en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF13EN_SHIFT           3
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF14EN_MASK            0x1 /* cf14en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF14EN_SHIFT           4
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF15EN_MASK            0x1 /* cf15en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF15EN_SHIFT           5
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF16EN_MASK            0x1 /* cf16en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF16EN_SHIFT           6
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF17EN_MASK            0x1 /* cf_array_cf_en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF17EN_SHIFT           7
	u8 flags10;
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF18EN_MASK            0x1 /* cf18en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF18EN_SHIFT           0
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF19EN_MASK            0x1 /* cf19en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF19EN_SHIFT           1
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF20EN_MASK            0x1 /* cf20en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF20EN_SHIFT           2
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF21EN_MASK            0x1 /* cf21en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF21EN_SHIFT           3
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_SLOW_PATH_EN_MASK      0x1 /* cf22en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_SLOW_PATH_EN_SHIFT     4
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF23EN_MASK            0x1 /* cf23en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF23EN_SHIFT           5
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE0EN_MASK           0x1 /* rule0en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE0EN_SHIFT          6
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE1EN_MASK           0x1 /* rule1en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE1EN_SHIFT          7
	u8 flags11;
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE2EN_MASK           0x1 /* rule2en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE2EN_SHIFT          0
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE3EN_MASK           0x1 /* rule3en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE3EN_SHIFT          1
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE4EN_MASK           0x1 /* rule4en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE4EN_SHIFT          2
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE5EN_MASK           0x1 /* rule5en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE5EN_SHIFT          3
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE6EN_MASK           0x1 /* rule6en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE6EN_SHIFT          4
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE7EN_MASK           0x1 /* rule7en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE7EN_SHIFT          5
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED1_MASK      0x1 /* rule8en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED1_SHIFT     6
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE9EN_MASK           0x1 /* rule9en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE9EN_SHIFT          7
	u8 flags12;
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE10EN_MASK          0x1 /* rule10en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE10EN_SHIFT         0
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_IRQ_PROD_RULE_EN_MASK  0x1 /* rule11en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_IRQ_PROD_RULE_EN_SHIFT 1
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED2_MASK      0x1 /* rule12en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED2_SHIFT     2
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED3_MASK      0x1 /* rule13en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED3_SHIFT     3
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE14EN_MASK          0x1 /* rule14en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE14EN_SHIFT         4
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE15EN_MASK          0x1 /* rule15en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE15EN_SHIFT         5
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE16EN_MASK          0x1 /* rule16en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE16EN_SHIFT         6
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE17EN_MASK          0x1 /* rule17en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE17EN_SHIFT         7
	u8 flags13;
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE18EN_MASK          0x1 /* rule18en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE18EN_SHIFT         0
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE19EN_MASK          0x1 /* rule19en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_RULE19EN_SHIFT         1
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED4_MASK      0x1 /* rule20en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED4_SHIFT     2
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED5_MASK      0x1 /* rule21en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED5_SHIFT     3
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED6_MASK      0x1 /* rule22en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED6_SHIFT     4
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED7_MASK      0x1 /* rule23en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED7_SHIFT     5
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED8_MASK      0x1 /* rule24en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED8_SHIFT     6
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED9_MASK      0x1 /* rule25en */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_A0_RESERVED9_SHIFT     7
	u8 flags14;
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT16_MASK             0x1 /* bit16 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT16_SHIFT            0
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT17_MASK             0x1 /* bit17 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT17_SHIFT            1
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT18_MASK             0x1 /* bit18 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT18_SHIFT            2
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT19_MASK             0x1 /* bit19 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT19_SHIFT            3
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT20_MASK             0x1 /* bit20 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT20_SHIFT            4
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT21_MASK             0x1 /* bit21 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_BIT21_SHIFT            5
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF23_MASK              0x3 /* cf23 */
#define E5_XSTORM_ROCE_RESP_CONN_AG_CTX_CF23_SHIFT             6
	u8 byte2 /* byte2 */;
	__le16 physical_q0 /* physical_q0 */;
	__le16 word1 /* physical_q1 */;
	__le16 irq_prod /* physical_q2 */;
	__le16 word3 /* word3 */;
	__le16 word4 /* word4 */;
	__le16 ack_cons /* word5 */;
	__le16 irq_cons /* conn_dpi */;
	u8 rxmit_opcode /* byte3 */;
	u8 byte4 /* byte4 */;
	u8 byte5 /* byte5 */;
	u8 byte6 /* byte6 */;
	__le32 rxmit_psn_and_id /* reg0 */;
	__le32 rxmit_bytes_length /* reg1 */;
	__le32 psn /* reg2 */;
	__le32 reg3 /* reg3 */;
	__le32 reg4 /* reg4 */;
	__le32 reg5 /* cf_array0 */;
	__le32 msn_and_syndrome /* cf_array1 */;
};


struct e5_ystorm_roce_req_conn_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 byte1 /* state_and_core_id */;
	u8 flags0;
#define E5_YSTORM_ROCE_REQ_CONN_AG_CTX_BIT0_MASK     0x1 /* exist_in_qm0 */
#define E5_YSTORM_ROCE_REQ_CONN_AG_CTX_BIT0_SHIFT    0
#define E5_YSTORM_ROCE_REQ_CONN_AG_CTX_BIT1_MASK     0x1 /* exist_in_qm1 */
#define E5_YSTORM_ROCE_REQ_CONN_AG_CTX_BIT1_SHIFT    1
#define E5_YSTORM_ROCE_REQ_CONN_AG_CTX_CF0_MASK      0x3 /* cf0 */
#define E5_YSTORM_ROCE_REQ_CONN_AG_CTX_CF0_SHIFT     2
#define E5_YSTORM_ROCE_REQ_CONN_AG_CTX_CF1_MASK      0x3 /* cf1 */
#define E5_YSTORM_ROCE_REQ_CONN_AG_CTX_CF1_SHIFT     4
#define E5_YSTORM_ROCE_REQ_CONN_AG_CTX_CF2_MASK      0x3 /* cf2 */
#define E5_YSTORM_ROCE_REQ_CONN_AG_CTX_CF2_SHIFT     6
	u8 flags1;
#define E5_YSTORM_ROCE_REQ_CONN_AG_CTX_CF0EN_MASK    0x1 /* cf0en */
#define E5_YSTORM_ROCE_REQ_CONN_AG_CTX_CF0EN_SHIFT   0
#define E5_YSTORM_ROCE_REQ_CONN_AG_CTX_CF1EN_MASK    0x1 /* cf1en */
#define E5_YSTORM_ROCE_REQ_CONN_AG_CTX_CF1EN_SHIFT   1
#define E5_YSTORM_ROCE_REQ_CONN_AG_CTX_CF2EN_MASK    0x1 /* cf2en */
#define E5_YSTORM_ROCE_REQ_CONN_AG_CTX_CF2EN_SHIFT   2
#define E5_YSTORM_ROCE_REQ_CONN_AG_CTX_RULE0EN_MASK  0x1 /* rule0en */
#define E5_YSTORM_ROCE_REQ_CONN_AG_CTX_RULE0EN_SHIFT 3
#define E5_YSTORM_ROCE_REQ_CONN_AG_CTX_RULE1EN_MASK  0x1 /* rule1en */
#define E5_YSTORM_ROCE_REQ_CONN_AG_CTX_RULE1EN_SHIFT 4
#define E5_YSTORM_ROCE_REQ_CONN_AG_CTX_RULE2EN_MASK  0x1 /* rule2en */
#define E5_YSTORM_ROCE_REQ_CONN_AG_CTX_RULE2EN_SHIFT 5
#define E5_YSTORM_ROCE_REQ_CONN_AG_CTX_RULE3EN_MASK  0x1 /* rule3en */
#define E5_YSTORM_ROCE_REQ_CONN_AG_CTX_RULE3EN_SHIFT 6
#define E5_YSTORM_ROCE_REQ_CONN_AG_CTX_RULE4EN_MASK  0x1 /* rule4en */
#define E5_YSTORM_ROCE_REQ_CONN_AG_CTX_RULE4EN_SHIFT 7
	u8 byte2 /* byte2 */;
	u8 byte3 /* byte3 */;
	__le16 word0 /* word0 */;
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
	__le16 word1 /* word1 */;
	__le16 word2 /* word2 */;
	__le16 word3 /* word3 */;
	__le16 word4 /* word4 */;
	__le32 reg2 /* reg2 */;
	__le32 reg3 /* reg3 */;
};


struct e5_ystorm_roce_resp_conn_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 byte1 /* state_and_core_id */;
	u8 flags0;
#define E5_YSTORM_ROCE_RESP_CONN_AG_CTX_BIT0_MASK     0x1 /* exist_in_qm0 */
#define E5_YSTORM_ROCE_RESP_CONN_AG_CTX_BIT0_SHIFT    0
#define E5_YSTORM_ROCE_RESP_CONN_AG_CTX_BIT1_MASK     0x1 /* exist_in_qm1 */
#define E5_YSTORM_ROCE_RESP_CONN_AG_CTX_BIT1_SHIFT    1
#define E5_YSTORM_ROCE_RESP_CONN_AG_CTX_CF0_MASK      0x3 /* cf0 */
#define E5_YSTORM_ROCE_RESP_CONN_AG_CTX_CF0_SHIFT     2
#define E5_YSTORM_ROCE_RESP_CONN_AG_CTX_CF1_MASK      0x3 /* cf1 */
#define E5_YSTORM_ROCE_RESP_CONN_AG_CTX_CF1_SHIFT     4
#define E5_YSTORM_ROCE_RESP_CONN_AG_CTX_CF2_MASK      0x3 /* cf2 */
#define E5_YSTORM_ROCE_RESP_CONN_AG_CTX_CF2_SHIFT     6
	u8 flags1;
#define E5_YSTORM_ROCE_RESP_CONN_AG_CTX_CF0EN_MASK    0x1 /* cf0en */
#define E5_YSTORM_ROCE_RESP_CONN_AG_CTX_CF0EN_SHIFT   0
#define E5_YSTORM_ROCE_RESP_CONN_AG_CTX_CF1EN_MASK    0x1 /* cf1en */
#define E5_YSTORM_ROCE_RESP_CONN_AG_CTX_CF1EN_SHIFT   1
#define E5_YSTORM_ROCE_RESP_CONN_AG_CTX_CF2EN_MASK    0x1 /* cf2en */
#define E5_YSTORM_ROCE_RESP_CONN_AG_CTX_CF2EN_SHIFT   2
#define E5_YSTORM_ROCE_RESP_CONN_AG_CTX_RULE0EN_MASK  0x1 /* rule0en */
#define E5_YSTORM_ROCE_RESP_CONN_AG_CTX_RULE0EN_SHIFT 3
#define E5_YSTORM_ROCE_RESP_CONN_AG_CTX_RULE1EN_MASK  0x1 /* rule1en */
#define E5_YSTORM_ROCE_RESP_CONN_AG_CTX_RULE1EN_SHIFT 4
#define E5_YSTORM_ROCE_RESP_CONN_AG_CTX_RULE2EN_MASK  0x1 /* rule2en */
#define E5_YSTORM_ROCE_RESP_CONN_AG_CTX_RULE2EN_SHIFT 5
#define E5_YSTORM_ROCE_RESP_CONN_AG_CTX_RULE3EN_MASK  0x1 /* rule3en */
#define E5_YSTORM_ROCE_RESP_CONN_AG_CTX_RULE3EN_SHIFT 6
#define E5_YSTORM_ROCE_RESP_CONN_AG_CTX_RULE4EN_MASK  0x1 /* rule4en */
#define E5_YSTORM_ROCE_RESP_CONN_AG_CTX_RULE4EN_SHIFT 7
	u8 byte2 /* byte2 */;
	u8 byte3 /* byte3 */;
	__le16 word0 /* word0 */;
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
	__le16 word1 /* word1 */;
	__le16 word2 /* word2 */;
	__le16 word3 /* word3 */;
	__le16 word4 /* word4 */;
	__le32 reg2 /* reg2 */;
	__le32 reg3 /* reg3 */;
};


/*
 * Roce doorbell data
 */
enum roce_flavor
{
	PLAIN_ROCE /* RoCE v1 */,
	RROCE_IPV4 /* RoCE v2 (Routable RoCE) over ipv4 */,
	RROCE_IPV6 /* RoCE v2 (Routable RoCE) over ipv6 */,
	MAX_ROCE_FLAVOR
};

#endif /* __ECORE_HSI_ROCE__ */
