/*
 * Copyright (c) 2018, Mellanox Technologies inc.  All rights reserved.
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

#ifndef MLX5_USER_IOCTL_VERBS_H
#define MLX5_USER_IOCTL_VERBS_H

#include <linux/types.h>

enum mlx5_ib_uapi_flow_action_flags {
	MLX5_IB_UAPI_FLOW_ACTION_FLAGS_REQUIRE_METADATA	= 1 << 0,
};

enum mlx5_ib_uapi_flow_table_type {
	MLX5_IB_UAPI_FLOW_TABLE_TYPE_NIC_RX     = 0x0,
	MLX5_IB_UAPI_FLOW_TABLE_TYPE_NIC_TX	= 0x1,
	MLX5_IB_UAPI_FLOW_TABLE_TYPE_FDB	= 0x2,
	MLX5_IB_UAPI_FLOW_TABLE_TYPE_RDMA_RX	= 0x3,
	MLX5_IB_UAPI_FLOW_TABLE_TYPE_RDMA_TX	= 0x4,
	MLX5_IB_UAPI_FLOW_TABLE_TYPE_RDMA_TRANSPORT_RX	= 0x5,
	MLX5_IB_UAPI_FLOW_TABLE_TYPE_RDMA_TRANSPORT_TX	= 0x6,
};

enum mlx5_ib_uapi_flow_action_packet_reformat_type {
	MLX5_IB_UAPI_FLOW_ACTION_PACKET_REFORMAT_TYPE_L2_TUNNEL_TO_L2 = 0x0,
	MLX5_IB_UAPI_FLOW_ACTION_PACKET_REFORMAT_TYPE_L2_TO_L2_TUNNEL = 0x1,
	MLX5_IB_UAPI_FLOW_ACTION_PACKET_REFORMAT_TYPE_L3_TUNNEL_TO_L2 = 0x2,
	MLX5_IB_UAPI_FLOW_ACTION_PACKET_REFORMAT_TYPE_L2_TO_L3_TUNNEL = 0x3,
};

enum mlx5_ib_uapi_reg_dmabuf_flags {
	MLX5_IB_UAPI_REG_DMABUF_ACCESS_DATA_DIRECT = 1 << 0,
};

struct mlx5_ib_uapi_devx_async_cmd_hdr {
	__aligned_u64	wr_id;
	__u8		out_data[];
};

enum mlx5_ib_uapi_dm_type {
	MLX5_IB_UAPI_DM_TYPE_MEMIC,
	MLX5_IB_UAPI_DM_TYPE_STEERING_SW_ICM,
	MLX5_IB_UAPI_DM_TYPE_HEADER_MODIFY_SW_ICM,
	MLX5_IB_UAPI_DM_TYPE_HEADER_MODIFY_PATTERN_SW_ICM,
	MLX5_IB_UAPI_DM_TYPE_ENCAP_SW_ICM,
};

enum mlx5_ib_uapi_devx_create_event_channel_flags {
	MLX5_IB_UAPI_DEVX_CR_EV_CH_FLAGS_OMIT_DATA = 1 << 0,
};

struct mlx5_ib_uapi_devx_async_event_hdr {
	__aligned_u64	cookie;
	__u8		out_data[];
};

enum mlx5_ib_uapi_pp_alloc_flags {
	MLX5_IB_UAPI_PP_ALLOC_FLAGS_DEDICATED_INDEX = 1 << 0,
};

enum mlx5_ib_uapi_uar_alloc_type {
	MLX5_IB_UAPI_UAR_ALLOC_TYPE_BF = 0x0,
	MLX5_IB_UAPI_UAR_ALLOC_TYPE_NC = 0x1,
};

enum mlx5_ib_uapi_query_port_flags {
	MLX5_IB_UAPI_QUERY_PORT_VPORT			= 1 << 0,
	MLX5_IB_UAPI_QUERY_PORT_VPORT_VHCA_ID		= 1 << 1,
	MLX5_IB_UAPI_QUERY_PORT_VPORT_STEERING_ICM_RX	= 1 << 2,
	MLX5_IB_UAPI_QUERY_PORT_VPORT_STEERING_ICM_TX	= 1 << 3,
	MLX5_IB_UAPI_QUERY_PORT_VPORT_REG_C0		= 1 << 4,
	MLX5_IB_UAPI_QUERY_PORT_ESW_OWNER_VHCA_ID	= 1 << 5,
};

struct mlx5_ib_uapi_reg {
	__u32 value;
	__u32 mask;
};

struct mlx5_ib_uapi_query_port {
	__aligned_u64 flags;
	__u16 vport;
	__u16 vport_vhca_id;
	__u16 esw_owner_vhca_id;
	__u16 rsvd0;
	__aligned_u64 vport_steering_icm_rx;
	__aligned_u64 vport_steering_icm_tx;
	struct mlx5_ib_uapi_reg reg_c0;
};

#endif

