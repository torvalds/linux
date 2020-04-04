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

struct mlx5_ifc_ipv4_layout_bits {
	u8         reserved_at_0[0x60];

	u8         ipv4[0x20];
};

struct mlx5_ifc_ipv6_layout_bits {
	u8         ipv6[16][0x8];
};

union mlx5_ifc_ipv6_layout_ipv4_layout_auto_bits {
	struct mlx5_ifc_ipv6_layout_bits ipv6_layout;
	struct mlx5_ifc_ipv4_layout_bits ipv4_layout;
	u8         reserved_at_0[0x80];
};

enum {
	MLX5_FPGA_CAP_SANDBOX_VENDOR_ID_MLNX = 0x2c9,
};

enum {
	MLX5_FPGA_CAP_SANDBOX_PRODUCT_ID_IPSEC    = 0x2,
	MLX5_FPGA_CAP_SANDBOX_PRODUCT_ID_TLS      = 0x3,
};

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

struct mlx5_ifc_tls_extended_cap_bits {
	u8         aes_gcm_128[0x1];
	u8         aes_gcm_256[0x1];
	u8         reserved_at_2[0x1e];
	u8         reserved_at_20[0x20];
	u8         context_capacity_total[0x20];
	u8         context_capacity_rx[0x20];
	u8         context_capacity_tx[0x20];
	u8         reserved_at_a0[0x10];
	u8         tls_counter_size[0x10];
	u8         tls_counters_addr_low[0x20];
	u8         tls_counters_addr_high[0x20];
	u8         rx[0x1];
	u8         tx[0x1];
	u8         tls_v12[0x1];
	u8         tls_v13[0x1];
	u8         lro[0x1];
	u8         ipv6[0x1];
	u8         reserved_at_106[0x1a];
};

struct mlx5_ifc_ipsec_extended_cap_bits {
	u8         encapsulation[0x20];

	u8         reserved_0[0x12];
	u8         v2_command[0x1];
	u8         udp_encap[0x1];
	u8         rx_no_trailer[0x1];
	u8         ipv4_fragment[0x1];
	u8         ipv6[0x1];
	u8         esn[0x1];
	u8         lso[0x1];
	u8         transport_and_tunnel_mode[0x1];
	u8         tunnel_mode[0x1];
	u8         transport_mode[0x1];
	u8         ah_esp[0x1];
	u8         esp[0x1];
	u8         ah[0x1];
	u8         ipv4_options[0x1];

	u8         auth_alg[0x20];

	u8         enc_alg[0x20];

	u8         sa_cap[0x20];

	u8         reserved_1[0x10];
	u8         number_of_ipsec_counters[0x10];

	u8         ipsec_counters_addr_low[0x20];
	u8         ipsec_counters_addr_high[0x20];
};

struct mlx5_ifc_ipsec_counters_bits {
	u8         dec_in_packets[0x40];

	u8         dec_out_packets[0x40];

	u8         dec_bypass_packets[0x40];

	u8         enc_in_packets[0x40];

	u8         enc_out_packets[0x40];

	u8         enc_bypass_packets[0x40];

	u8         drop_dec_packets[0x40];

	u8         failed_auth_dec_packets[0x40];

	u8         drop_enc_packets[0x40];

	u8         success_add_sa[0x40];

	u8         fail_add_sa[0x40];

	u8         success_delete_sa[0x40];

	u8         fail_delete_sa[0x40];

	u8         dropped_cmd[0x40];
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
enum mlx5_ifc_fpga_ipsec_response_syndrome {
	MLX5_FPGA_IPSEC_RESPONSE_SUCCESS = 0,
	MLX5_FPGA_IPSEC_RESPONSE_ILLEGAL_REQUEST = 1,
	MLX5_FPGA_IPSEC_RESPONSE_SADB_ISSUE = 2,
	MLX5_FPGA_IPSEC_RESPONSE_WRITE_RESPONSE_ISSUE = 3,
};

struct mlx5_ifc_fpga_ipsec_cmd_resp {
	__be32 syndrome;
	union {
		__be32 sw_sa_handle;
		__be32 flags;
	};
	u8 reserved[24];
} __packed;

enum mlx5_ifc_fpga_ipsec_cmd_opcode {
	MLX5_FPGA_IPSEC_CMD_OP_ADD_SA = 0,
	MLX5_FPGA_IPSEC_CMD_OP_DEL_SA = 1,
	MLX5_FPGA_IPSEC_CMD_OP_ADD_SA_V2 = 2,
	MLX5_FPGA_IPSEC_CMD_OP_DEL_SA_V2 = 3,
	MLX5_FPGA_IPSEC_CMD_OP_MOD_SA_V2 = 4,
	MLX5_FPGA_IPSEC_CMD_OP_SET_CAP = 5,
};

enum mlx5_ifc_fpga_ipsec_cap {
	MLX5_FPGA_IPSEC_CAP_NO_TRAILER = BIT(0),
};

struct mlx5_ifc_fpga_ipsec_cmd_cap {
	__be32 cmd;
	__be32 flags;
	u8 reserved[24];
} __packed;

enum mlx5_ifc_fpga_ipsec_sa_flags {
	MLX5_FPGA_IPSEC_SA_ESN_EN = BIT(0),
	MLX5_FPGA_IPSEC_SA_ESN_OVERLAP = BIT(1),
	MLX5_FPGA_IPSEC_SA_IPV6 = BIT(2),
	MLX5_FPGA_IPSEC_SA_DIR_SX = BIT(3),
	MLX5_FPGA_IPSEC_SA_SPI_EN = BIT(4),
	MLX5_FPGA_IPSEC_SA_SA_VALID = BIT(5),
	MLX5_FPGA_IPSEC_SA_IP_ESP = BIT(6),
	MLX5_FPGA_IPSEC_SA_IP_AH = BIT(7),
};

enum mlx5_ifc_fpga_ipsec_sa_enc_mode {
	MLX5_FPGA_IPSEC_SA_ENC_MODE_NONE = 0,
	MLX5_FPGA_IPSEC_SA_ENC_MODE_AES_GCM_128_AUTH_128 = 1,
	MLX5_FPGA_IPSEC_SA_ENC_MODE_AES_GCM_256_AUTH_128 = 3,
};

struct mlx5_ifc_fpga_ipsec_sa_v1 {
	__be32 cmd;
	u8 key_enc[32];
	u8 key_auth[32];
	__be32 sip[4];
	__be32 dip[4];
	union {
		struct {
			__be32 reserved;
			u8 salt_iv[8];
			__be32 salt;
		} __packed gcm;
		struct {
			u8 salt[16];
		} __packed cbc;
	};
	__be32 spi;
	__be32 sw_sa_handle;
	__be16 tfclen;
	u8 enc_mode;
	u8 reserved1[2];
	u8 flags;
	u8 reserved2[2];
};

struct mlx5_ifc_fpga_ipsec_sa {
	struct mlx5_ifc_fpga_ipsec_sa_v1 ipsec_sa_v1;
	__be16 udp_sp;
	__be16 udp_dp;
	u8 reserved1[4];
	__be32 esn;
	__be16 vid;	/* only 12 bits, rest is reserved */
	__be16 reserved2;
} __packed;

enum fpga_tls_cmds {
	CMD_SETUP_STREAM		= 0x1001,
	CMD_TEARDOWN_STREAM		= 0x1002,
	CMD_RESYNC_RX			= 0x1003,
};

#define MLX5_TLS_1_2 (0)

#define MLX5_TLS_ALG_AES_GCM_128 (0)
#define MLX5_TLS_ALG_AES_GCM_256 (1)

struct mlx5_ifc_tls_cmd_bits {
	u8         command_type[0x20];
	u8         ipv6[0x1];
	u8         direction_sx[0x1];
	u8         tls_version[0x2];
	u8         reserved[0x1c];
	u8         swid[0x20];
	u8         src_port[0x10];
	u8         dst_port[0x10];
	union mlx5_ifc_ipv6_layout_ipv4_layout_auto_bits src_ipv4_src_ipv6;
	union mlx5_ifc_ipv6_layout_ipv4_layout_auto_bits dst_ipv4_dst_ipv6;
	u8         tls_rcd_sn[0x40];
	u8         tcp_sn[0x20];
	u8         tls_implicit_iv[0x20];
	u8         tls_xor_iv[0x40];
	u8         encryption_key[0x100];
	u8         alg[4];
	u8         reserved2[0x1c];
	u8         reserved3[0x4a0];
};

struct mlx5_ifc_tls_resp_bits {
	u8         syndrome[0x20];
	u8         stream_id[0x20];
	u8         reserved[0x40];
};

#define MLX5_TLS_COMMAND_SIZE (0x100)

#endif /* MLX5_IFC_FPGA_H */
