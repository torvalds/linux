/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2020 Mellanox Technologies Ltd. */

#ifndef __MLX5_IFC_VDPA_H_
#define __MLX5_IFC_VDPA_H_

enum {
	MLX5_VIRTIO_Q_EVENT_MODE_NO_MSIX_MODE  = 0x0,
	MLX5_VIRTIO_Q_EVENT_MODE_QP_MODE       = 0x1,
	MLX5_VIRTIO_Q_EVENT_MODE_MSIX_MODE     = 0x2,
};

enum {
	MLX5_VIRTIO_EMULATION_VIRTIO_QUEUE_TYPE_SPLIT   = 0,
	MLX5_VIRTIO_EMULATION_VIRTIO_QUEUE_TYPE_PACKED  = 1,
};

enum {
	MLX5_VIRTIO_EMULATION_CAP_VIRTIO_QUEUE_TYPE_SPLIT =
		BIT(MLX5_VIRTIO_EMULATION_VIRTIO_QUEUE_TYPE_SPLIT),
	MLX5_VIRTIO_EMULATION_CAP_VIRTIO_QUEUE_TYPE_PACKED =
		BIT(MLX5_VIRTIO_EMULATION_VIRTIO_QUEUE_TYPE_PACKED),
};

struct mlx5_ifc_virtio_q_bits {
	u8    virtio_q_type[0x8];
	u8    reserved_at_8[0x5];
	u8    event_mode[0x3];
	u8    queue_index[0x10];

	u8    full_emulation[0x1];
	u8    virtio_version_1_0[0x1];
	u8    reserved_at_22[0x2];
	u8    offload_type[0x4];
	u8    event_qpn_or_msix[0x18];

	u8    doorbell_stride_index[0x10];
	u8    queue_size[0x10];

	u8    device_emulation_id[0x20];

	u8    desc_addr[0x40];

	u8    used_addr[0x40];

	u8    available_addr[0x40];

	u8    virtio_q_mkey[0x20];

	u8    max_tunnel_desc[0x10];
	u8    reserved_at_170[0x8];
	u8    error_type[0x8];

	u8    umem_1_id[0x20];

	u8    umem_1_size[0x20];

	u8    umem_1_offset[0x40];

	u8    umem_2_id[0x20];

	u8    umem_2_size[0x20];

	u8    umem_2_offset[0x40];

	u8    umem_3_id[0x20];

	u8    umem_3_size[0x20];

	u8    umem_3_offset[0x40];

	u8    counter_set_id[0x20];

	u8    reserved_at_320[0x8];
	u8    pd[0x18];

	u8    reserved_at_340[0x20];

	u8    desc_group_mkey[0x20];

	u8    reserved_at_380[0x80];
};

struct mlx5_ifc_virtio_net_q_object_bits {
	u8    modify_field_select[0x40];

	u8    reserved_at_40[0x20];

	u8    vhca_id[0x10];
	u8    reserved_at_70[0x10];

	u8    queue_feature_bit_mask_12_3[0xa];
	u8    dirty_bitmap_dump_enable[0x1];
	u8    vhost_log_page[0x5];
	u8    reserved_at_90[0xc];
	u8    state[0x4];

	u8    reserved_at_a0[0x5];
	u8    queue_feature_bit_mask_2_0[0x3];
	u8    tisn_or_qpn[0x18];

	u8    dirty_bitmap_mkey[0x20];

	u8    dirty_bitmap_size[0x20];

	u8    dirty_bitmap_addr[0x40];

	u8    hw_available_index[0x10];
	u8    hw_used_index[0x10];

	u8    reserved_at_160[0xa0];

	struct mlx5_ifc_virtio_q_bits virtio_q_context;
};

struct mlx5_ifc_create_virtio_net_q_in_bits {
	struct mlx5_ifc_general_obj_in_cmd_hdr_bits general_obj_in_cmd_hdr;

	struct mlx5_ifc_virtio_net_q_object_bits obj_context;
};

struct mlx5_ifc_create_virtio_net_q_out_bits {
	struct mlx5_ifc_general_obj_out_cmd_hdr_bits general_obj_out_cmd_hdr;
};

struct mlx5_ifc_destroy_virtio_net_q_in_bits {
	struct mlx5_ifc_general_obj_in_cmd_hdr_bits general_obj_out_cmd_hdr;
};

struct mlx5_ifc_destroy_virtio_net_q_out_bits {
	struct mlx5_ifc_general_obj_out_cmd_hdr_bits general_obj_out_cmd_hdr;
};

struct mlx5_ifc_query_virtio_net_q_in_bits {
	struct mlx5_ifc_general_obj_in_cmd_hdr_bits general_obj_in_cmd_hdr;
};

struct mlx5_ifc_query_virtio_net_q_out_bits {
	struct mlx5_ifc_general_obj_out_cmd_hdr_bits general_obj_out_cmd_hdr;

	struct mlx5_ifc_virtio_net_q_object_bits obj_context;
};

enum {
	MLX5_VIRTQ_MODIFY_MASK_STATE                    = (u64)1 << 0,
	MLX5_VIRTQ_MODIFY_MASK_DIRTY_BITMAP_PARAMS      = (u64)1 << 3,
	MLX5_VIRTQ_MODIFY_MASK_DIRTY_BITMAP_DUMP_ENABLE = (u64)1 << 4,
	MLX5_VIRTQ_MODIFY_MASK_VIRTIO_Q_ADDRS           = (u64)1 << 6,
	MLX5_VIRTQ_MODIFY_MASK_VIRTIO_Q_AVAIL_IDX       = (u64)1 << 7,
	MLX5_VIRTQ_MODIFY_MASK_VIRTIO_Q_USED_IDX        = (u64)1 << 8,
	MLX5_VIRTQ_MODIFY_MASK_DESC_GROUP_MKEY          = (u64)1 << 14,
};

enum {
	MLX5_VIRTIO_NET_Q_OBJECT_STATE_INIT     = 0x0,
	MLX5_VIRTIO_NET_Q_OBJECT_STATE_RDY      = 0x1,
	MLX5_VIRTIO_NET_Q_OBJECT_STATE_SUSPEND  = 0x2,
	MLX5_VIRTIO_NET_Q_OBJECT_STATE_ERR      = 0x3,
};

/* This indicates that the object was not created or has already
 * been desroyed. It is very safe to assume that this object will never
 * have so many states
 */
enum {
	MLX5_VIRTIO_NET_Q_OBJECT_NONE = 0xffffffff
};

enum {
	MLX5_RQTC_LIST_Q_TYPE_RQ            = 0x0,
	MLX5_RQTC_LIST_Q_TYPE_VIRTIO_NET_Q  = 0x1,
};

struct mlx5_ifc_modify_virtio_net_q_in_bits {
	struct mlx5_ifc_general_obj_in_cmd_hdr_bits general_obj_in_cmd_hdr;

	struct mlx5_ifc_virtio_net_q_object_bits obj_context;
};

struct mlx5_ifc_modify_virtio_net_q_out_bits {
	struct mlx5_ifc_general_obj_out_cmd_hdr_bits general_obj_out_cmd_hdr;
};

struct mlx5_ifc_virtio_q_counters_bits {
	u8    modify_field_select[0x40];
	u8    reserved_at_40[0x40];
	u8    received_desc[0x40];
	u8    completed_desc[0x40];
	u8    error_cqes[0x20];
	u8    bad_desc_errors[0x20];
	u8    exceed_max_chain[0x20];
	u8    invalid_buffer[0x20];
	u8    reserved_at_180[0x280];
};

struct mlx5_ifc_create_virtio_q_counters_in_bits {
	struct mlx5_ifc_general_obj_in_cmd_hdr_bits hdr;
	struct mlx5_ifc_virtio_q_counters_bits virtio_q_counters;
};

struct mlx5_ifc_create_virtio_q_counters_out_bits {
	struct mlx5_ifc_general_obj_in_cmd_hdr_bits hdr;
	struct mlx5_ifc_virtio_q_counters_bits virtio_q_counters;
};

struct mlx5_ifc_destroy_virtio_q_counters_in_bits {
	struct mlx5_ifc_general_obj_in_cmd_hdr_bits hdr;
};

struct mlx5_ifc_destroy_virtio_q_counters_out_bits {
	struct mlx5_ifc_general_obj_out_cmd_hdr_bits hdr;
};

struct mlx5_ifc_query_virtio_q_counters_in_bits {
	struct mlx5_ifc_general_obj_in_cmd_hdr_bits hdr;
};

struct mlx5_ifc_query_virtio_q_counters_out_bits {
	struct mlx5_ifc_general_obj_in_cmd_hdr_bits hdr;
	struct mlx5_ifc_virtio_q_counters_bits counters;
};

#endif /* __MLX5_IFC_VDPA_H_ */
