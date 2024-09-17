/*
 * Copyright (c) 2017, Mellanox Technologies, Ltd.  All rights reserved.
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
#ifndef MLX5_IFC_FPGA_H
#define MLX5_IFC_FPGA_H

struct mlx5_ifc_fpga_shell_caps_bits {
	u8         max_num_qps[0x10];
	u8         reserved_at_10[0x8];
	u8         total_rcv_credits[0x8];

	u8         reserved_at_20[0xe];
	u8         qp_type[0x2];
	u8         reserved_at_30[0x5];
	u8         rae[0x1];
	u8         rwe[0x1];
	u8         rre[0x1];
	u8         reserved_at_38[0x4];
	u8         dc[0x1];
	u8         ud[0x1];
	u8         uc[0x1];
	u8         rc[0x1];

	u8         reserved_at_40[0x1a];
	u8         log_ddr_size[0x6];

	u8         max_fpga_qp_msg_size[0x20];

	u8         reserved_at_80[0x180];
};

struct mlx5_ifc_fpga_cap_bits {
	u8         fpga_id[0x8];
	u8         fpga_device[0x18];

	u8         register_file_ver[0x20];

	u8         fpga_ctrl_modify[0x1];
	u8         reserved_at_41[0x5];
	u8         access_reg_query_mode[0x2];
	u8         reserved_at_48[0x6];
	u8         access_reg_modify_mode[0x2];
	u8         reserved_at_50[0x10];

	u8         reserved_at_60[0x20];

	u8         image_version[0x20];

	u8         image_date[0x20];

	u8         image_time[0x20];

	u8         shell_version[0x20];

	u8         reserved_at_100[0x80];

	struct mlx5_ifc_fpga_shell_caps_bits shell_caps;

	u8         reserved_at_380[0x8];
	u8         ieee_vendor_id[0x18];

	u8         sandbox_product_version[0x10];
	u8         sandbox_product_id[0x10];

	u8         sandbox_basic_caps[0x20];

	u8         reserved_at_3e0[0x10];
	u8         sandbox_extended_caps_len[0x10];

	u8         sandbox_extended_caps_addr[0x40];

	u8         fpga_ddr_start_addr[0x40];

	u8         fpga_cr_space_start_addr[0x40];

	u8         fpga_ddr_size[0x20];

	u8         fpga_cr_space_size[0x20];

	u8         reserved_at_500[0x300];
};

enum {
	MLX5_FPGA_CTRL_OPERATION_LOAD                = 0x1,
	MLX5_FPGA_CTRL_OPERATION_RESET               = 0x2,
	MLX5_FPGA_CTRL_OPERATION_FLASH_SELECT        = 0x3,
	MLX5_FPGA_CTRL_OPERATION_SANDBOX_BYPASS_ON   = 0x4,
	MLX5_FPGA_CTRL_OPERATION_SANDBOX_BYPASS_OFF  = 0x5,
	MLX5_FPGA_CTRL_OPERATION_RESET_SANDBOX       = 0x6,
};

struct mlx5_ifc_fpga_ctrl_bits {
	u8         reserved_at_0[0x8];
	u8         operation[0x8];
	u8         reserved_at_10[0x8];
	u8         status[0x8];

	u8         reserved_at_20[0x8];
	u8         flash_select_admin[0x8];
	u8         reserved_at_30[0x8];
	u8         flash_select_oper[0x8];

	u8         reserved_at_40[0x40];
};

enum {
	MLX5_FPGA_ERROR_EVENT_SYNDROME_CORRUPTED_DDR        = 0x1,
	MLX5_FPGA_ERROR_EVENT_SYNDROME_FLASH_TIMEOUT        = 0x2,
	MLX5_FPGA_ERROR_EVENT_SYNDROME_INTERNAL_LINK_ERROR  = 0x3,
	MLX5_FPGA_ERROR_EVENT_SYNDROME_WATCHDOG_FAILURE     = 0x4,
	MLX5_FPGA_ERROR_EVENT_SYNDROME_I2C_FAILURE          = 0x5,
	MLX5_FPGA_ERROR_EVENT_SYNDROME_IMAGE_CHANGED        = 0x6,
	MLX5_FPGA_ERROR_EVENT_SYNDROME_TEMPERATURE_CRITICAL = 0x7,
};

struct mlx5_ifc_fpga_error_event_bits {
	u8         reserved_at_0[0x40];

	u8         reserved_at_40[0x18];
	u8         syndrome[0x8];

	u8         reserved_at_60[0x80];
};

#define MLX5_FPGA_ACCESS_REG_SIZE_MAX 64

struct mlx5_ifc_fpga_access_reg_bits {
	u8         reserved_at_0[0x20];

	u8         reserved_at_20[0x10];
	u8         size[0x10];

	u8         address[0x40];

	u8         data[0][0x8];
};

enum mlx5_ifc_fpga_qp_state {
	MLX5_FPGA_QPC_STATE_INIT    = 0x0,
	MLX5_FPGA_QPC_STATE_ACTIVE  = 0x1,
	MLX5_FPGA_QPC_STATE_ERROR   = 0x2,
};

enum mlx5_ifc_fpga_qp_type {
	MLX5_FPGA_QPC_QP_TYPE_SHELL_QP    = 0x0,
	MLX5_FPGA_QPC_QP_TYPE_SANDBOX_QP  = 0x1,
};

enum mlx5_ifc_fpga_qp_service_type {
	MLX5_FPGA_QPC_ST_RC  = 0x0,
};

struct mlx5_ifc_fpga_qpc_bits {
	u8         state[0x4];
	u8         reserved_at_4[0x1b];
	u8         qp_type[0x1];

	u8         reserved_at_20[0x4];
	u8         st[0x4];
	u8         reserved_at_28[0x10];
	u8         traffic_class[0x8];

	u8         ether_type[0x10];
	u8         prio[0x3];
	u8         dei[0x1];
	u8         vid[0xc];

	u8         reserved_at_60[0x20];

	u8         reserved_at_80[0x8];
	u8         next_rcv_psn[0x18];

	u8         reserved_at_a0[0x8];
	u8         next_send_psn[0x18];

	u8         reserved_at_c0[0x10];
	u8         pkey[0x10];

	u8         reserved_at_e0[0x8];
	u8         remote_qpn[0x18];

	u8         reserved_at_100[0x15];
	u8         rnr_retry[0x3];
	u8         reserved_at_118[0x5];
	u8         retry_count[0x3];

	u8         reserved_at_120[0x20];

	u8         reserved_at_140[0x10];
	u8         remote_mac_47_32[0x10];

	u8         remote_mac_31_0[0x20];

	u8         remote_ip[16][0x8];

	u8         reserved_at_200[0x40];

	u8         reserved_at_240[0x10];
	u8         fpga_mac_47_32[0x10];

	u8         fpga_mac_31_0[0x20];

	u8         fpga_ip[16][0x8];
};

struct mlx5_ifc_fpga_create_qp_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];

	struct mlx5_ifc_fpga_qpc_bits fpga_qpc;
};

struct mlx5_ifc_fpga_create_qp_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x8];
	u8         fpga_qpn[0x18];

	u8         reserved_at_60[0x20];

	struct mlx5_ifc_fpga_qpc_bits fpga_qpc;
};

struct mlx5_ifc_fpga_modify_qp_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         fpga_qpn[0x18];

	u8         field_select[0x20];

	struct mlx5_ifc_fpga_qpc_bits fpga_qpc;
};

struct mlx5_ifc_fpga_modify_qp_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_fpga_query_qp_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         fpga_qpn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_fpga_query_qp_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];

	struct mlx5_ifc_fpga_qpc_bits fpga_qpc;
};

struct mlx5_ifc_fpga_query_qp_counters_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         clear[0x1];
	u8         reserved_at_41[0x7];
	u8         fpga_qpn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_fpga_query_qp_counters_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];

	u8         rx_ack_packets[0x40];

	u8         rx_send_packets[0x40];

	u8         tx_ack_packets[0x40];

	u8         tx_send_packets[0x40];

	u8         rx_total_drop[0x40];

	u8         reserved_at_1c0[0x1c0];
};

struct mlx5_ifc_fpga_destroy_qp_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         fpga_qpn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_fpga_destroy_qp_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

enum {
	MLX5_FPGA_QP_ERROR_EVENT_SYNDROME_RETRY_COUNTER_EXPIRED  = 0x1,
	MLX5_FPGA_QP_ERROR_EVENT_SYNDROME_RNR_EXPIRED            = 0x2,
};

struct mlx5_ifc_fpga_qp_error_event_bits {
	u8         reserved_at_0[0x40];

	u8         reserved_at_40[0x18];
	u8         syndrome[0x8];

	u8         reserved_at_60[0x60];

	u8         reserved_at_c0[0x8];
	u8         fpga_qpn[0x18];
};
#endif /* MLX5_IFC_FPGA_H */
