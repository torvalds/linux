/*
 * Copyright (c) 2013-2015, Mellanox Technologies, Ltd.  All rights reserved.
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
#ifndef MLX5_IFC_H
#define MLX5_IFC_H

#include "mlx5_ifc_fpga.h"

enum {
	MLX5_EVENT_TYPE_CODING_COMPLETION_EVENTS                   = 0x0,
	MLX5_EVENT_TYPE_CODING_PATH_MIGRATED_SUCCEEDED             = 0x1,
	MLX5_EVENT_TYPE_CODING_COMMUNICATION_ESTABLISHED           = 0x2,
	MLX5_EVENT_TYPE_CODING_SEND_QUEUE_DRAINED                  = 0x3,
	MLX5_EVENT_TYPE_CODING_LAST_WQE_REACHED                    = 0x13,
	MLX5_EVENT_TYPE_CODING_SRQ_LIMIT                           = 0x14,
	MLX5_EVENT_TYPE_CODING_DCT_ALL_CONNECTIONS_CLOSED          = 0x1c,
	MLX5_EVENT_TYPE_CODING_DCT_ACCESS_KEY_VIOLATION            = 0x1d,
	MLX5_EVENT_TYPE_CODING_CQ_ERROR                            = 0x4,
	MLX5_EVENT_TYPE_CODING_LOCAL_WQ_CATASTROPHIC_ERROR         = 0x5,
	MLX5_EVENT_TYPE_CODING_PATH_MIGRATION_FAILED               = 0x7,
	MLX5_EVENT_TYPE_CODING_PAGE_FAULT_EVENT                    = 0xc,
	MLX5_EVENT_TYPE_CODING_INVALID_REQUEST_LOCAL_WQ_ERROR      = 0x10,
	MLX5_EVENT_TYPE_CODING_LOCAL_ACCESS_VIOLATION_WQ_ERROR     = 0x11,
	MLX5_EVENT_TYPE_CODING_LOCAL_SRQ_CATASTROPHIC_ERROR        = 0x12,
	MLX5_EVENT_TYPE_CODING_INTERNAL_ERROR                      = 0x8,
	MLX5_EVENT_TYPE_CODING_PORT_STATE_CHANGE                   = 0x9,
	MLX5_EVENT_TYPE_CODING_GPIO_EVENT                          = 0x15,
	MLX5_EVENT_TYPE_CODING_REMOTE_CONFIGURATION_PROTOCOL_EVENT = 0x19,
	MLX5_EVENT_TYPE_CODING_DOORBELL_BLUEFLAME_CONGESTION_EVENT = 0x1a,
	MLX5_EVENT_TYPE_CODING_STALL_VL_EVENT                      = 0x1b,
	MLX5_EVENT_TYPE_CODING_DROPPED_PACKET_LOGGED_EVENT         = 0x1f,
	MLX5_EVENT_TYPE_CODING_COMMAND_INTERFACE_COMPLETION        = 0xa,
	MLX5_EVENT_TYPE_CODING_PAGE_REQUEST                        = 0xb,
	MLX5_EVENT_TYPE_CODING_FPGA_ERROR                          = 0x20,
	MLX5_EVENT_TYPE_CODING_FPGA_QP_ERROR                       = 0x21
};

enum {
	MLX5_MODIFY_TIR_BITMASK_LRO                   = 0x0,
	MLX5_MODIFY_TIR_BITMASK_INDIRECT_TABLE        = 0x1,
	MLX5_MODIFY_TIR_BITMASK_HASH                  = 0x2,
	MLX5_MODIFY_TIR_BITMASK_TUNNELED_OFFLOAD_EN   = 0x3
};

enum {
	MLX5_SET_HCA_CAP_OP_MOD_GENERAL_DEVICE        = 0x0,
	MLX5_SET_HCA_CAP_OP_MOD_ODP                   = 0x2,
	MLX5_SET_HCA_CAP_OP_MOD_ATOMIC                = 0x3,
	MLX5_SET_HCA_CAP_OP_MOD_ROCE                  = 0x4,
};

enum {
	MLX5_SHARED_RESOURCE_UID = 0xffff,
};

enum {
	MLX5_OBJ_TYPE_SW_ICM = 0x0008,
};

enum {
	MLX5_GENERAL_OBJ_TYPES_CAP_SW_ICM = (1ULL << MLX5_OBJ_TYPE_SW_ICM),
	MLX5_GENERAL_OBJ_TYPES_CAP_GENEVE_TLV_OPT = (1ULL << 11),
	MLX5_GENERAL_OBJ_TYPES_CAP_VIRTIO_NET_Q = (1ULL << 13),
};

enum {
	MLX5_OBJ_TYPE_GENEVE_TLV_OPT = 0x000b,
	MLX5_OBJ_TYPE_MKEY = 0xff01,
	MLX5_OBJ_TYPE_QP = 0xff02,
	MLX5_OBJ_TYPE_PSV = 0xff03,
	MLX5_OBJ_TYPE_RMP = 0xff04,
	MLX5_OBJ_TYPE_XRC_SRQ = 0xff05,
	MLX5_OBJ_TYPE_RQ = 0xff06,
	MLX5_OBJ_TYPE_SQ = 0xff07,
	MLX5_OBJ_TYPE_TIR = 0xff08,
	MLX5_OBJ_TYPE_TIS = 0xff09,
	MLX5_OBJ_TYPE_DCT = 0xff0a,
	MLX5_OBJ_TYPE_XRQ = 0xff0b,
	MLX5_OBJ_TYPE_RQT = 0xff0e,
	MLX5_OBJ_TYPE_FLOW_COUNTER = 0xff0f,
	MLX5_OBJ_TYPE_CQ = 0xff10,
};

enum {
	MLX5_CMD_OP_QUERY_HCA_CAP                 = 0x100,
	MLX5_CMD_OP_QUERY_ADAPTER                 = 0x101,
	MLX5_CMD_OP_INIT_HCA                      = 0x102,
	MLX5_CMD_OP_TEARDOWN_HCA                  = 0x103,
	MLX5_CMD_OP_ENABLE_HCA                    = 0x104,
	MLX5_CMD_OP_DISABLE_HCA                   = 0x105,
	MLX5_CMD_OP_QUERY_PAGES                   = 0x107,
	MLX5_CMD_OP_MANAGE_PAGES                  = 0x108,
	MLX5_CMD_OP_SET_HCA_CAP                   = 0x109,
	MLX5_CMD_OP_QUERY_ISSI                    = 0x10a,
	MLX5_CMD_OP_SET_ISSI                      = 0x10b,
	MLX5_CMD_OP_SET_DRIVER_VERSION            = 0x10d,
	MLX5_CMD_OP_QUERY_SF_PARTITION            = 0x111,
	MLX5_CMD_OP_ALLOC_SF                      = 0x113,
	MLX5_CMD_OP_DEALLOC_SF                    = 0x114,
	MLX5_CMD_OP_CREATE_MKEY                   = 0x200,
	MLX5_CMD_OP_QUERY_MKEY                    = 0x201,
	MLX5_CMD_OP_DESTROY_MKEY                  = 0x202,
	MLX5_CMD_OP_QUERY_SPECIAL_CONTEXTS        = 0x203,
	MLX5_CMD_OP_PAGE_FAULT_RESUME             = 0x204,
	MLX5_CMD_OP_ALLOC_MEMIC                   = 0x205,
	MLX5_CMD_OP_DEALLOC_MEMIC                 = 0x206,
	MLX5_CMD_OP_CREATE_EQ                     = 0x301,
	MLX5_CMD_OP_DESTROY_EQ                    = 0x302,
	MLX5_CMD_OP_QUERY_EQ                      = 0x303,
	MLX5_CMD_OP_GEN_EQE                       = 0x304,
	MLX5_CMD_OP_CREATE_CQ                     = 0x400,
	MLX5_CMD_OP_DESTROY_CQ                    = 0x401,
	MLX5_CMD_OP_QUERY_CQ                      = 0x402,
	MLX5_CMD_OP_MODIFY_CQ                     = 0x403,
	MLX5_CMD_OP_CREATE_QP                     = 0x500,
	MLX5_CMD_OP_DESTROY_QP                    = 0x501,
	MLX5_CMD_OP_RST2INIT_QP                   = 0x502,
	MLX5_CMD_OP_INIT2RTR_QP                   = 0x503,
	MLX5_CMD_OP_RTR2RTS_QP                    = 0x504,
	MLX5_CMD_OP_RTS2RTS_QP                    = 0x505,
	MLX5_CMD_OP_SQERR2RTS_QP                  = 0x506,
	MLX5_CMD_OP_2ERR_QP                       = 0x507,
	MLX5_CMD_OP_2RST_QP                       = 0x50a,
	MLX5_CMD_OP_QUERY_QP                      = 0x50b,
	MLX5_CMD_OP_SQD_RTS_QP                    = 0x50c,
	MLX5_CMD_OP_INIT2INIT_QP                  = 0x50e,
	MLX5_CMD_OP_CREATE_PSV                    = 0x600,
	MLX5_CMD_OP_DESTROY_PSV                   = 0x601,
	MLX5_CMD_OP_CREATE_SRQ                    = 0x700,
	MLX5_CMD_OP_DESTROY_SRQ                   = 0x701,
	MLX5_CMD_OP_QUERY_SRQ                     = 0x702,
	MLX5_CMD_OP_ARM_RQ                        = 0x703,
	MLX5_CMD_OP_CREATE_XRC_SRQ                = 0x705,
	MLX5_CMD_OP_DESTROY_XRC_SRQ               = 0x706,
	MLX5_CMD_OP_QUERY_XRC_SRQ                 = 0x707,
	MLX5_CMD_OP_ARM_XRC_SRQ                   = 0x708,
	MLX5_CMD_OP_CREATE_DCT                    = 0x710,
	MLX5_CMD_OP_DESTROY_DCT                   = 0x711,
	MLX5_CMD_OP_DRAIN_DCT                     = 0x712,
	MLX5_CMD_OP_QUERY_DCT                     = 0x713,
	MLX5_CMD_OP_ARM_DCT_FOR_KEY_VIOLATION     = 0x714,
	MLX5_CMD_OP_CREATE_XRQ                    = 0x717,
	MLX5_CMD_OP_DESTROY_XRQ                   = 0x718,
	MLX5_CMD_OP_QUERY_XRQ                     = 0x719,
	MLX5_CMD_OP_ARM_XRQ                       = 0x71a,
	MLX5_CMD_OP_QUERY_XRQ_DC_PARAMS_ENTRY     = 0x725,
	MLX5_CMD_OP_SET_XRQ_DC_PARAMS_ENTRY       = 0x726,
	MLX5_CMD_OP_QUERY_XRQ_ERROR_PARAMS        = 0x727,
	MLX5_CMD_OP_RELEASE_XRQ_ERROR             = 0x729,
	MLX5_CMD_OP_MODIFY_XRQ                    = 0x72a,
	MLX5_CMD_OP_QUERY_ESW_FUNCTIONS           = 0x740,
	MLX5_CMD_OP_QUERY_VPORT_STATE             = 0x750,
	MLX5_CMD_OP_MODIFY_VPORT_STATE            = 0x751,
	MLX5_CMD_OP_QUERY_ESW_VPORT_CONTEXT       = 0x752,
	MLX5_CMD_OP_MODIFY_ESW_VPORT_CONTEXT      = 0x753,
	MLX5_CMD_OP_QUERY_NIC_VPORT_CONTEXT       = 0x754,
	MLX5_CMD_OP_MODIFY_NIC_VPORT_CONTEXT      = 0x755,
	MLX5_CMD_OP_QUERY_ROCE_ADDRESS            = 0x760,
	MLX5_CMD_OP_SET_ROCE_ADDRESS              = 0x761,
	MLX5_CMD_OP_QUERY_HCA_VPORT_CONTEXT       = 0x762,
	MLX5_CMD_OP_MODIFY_HCA_VPORT_CONTEXT      = 0x763,
	MLX5_CMD_OP_QUERY_HCA_VPORT_GID           = 0x764,
	MLX5_CMD_OP_QUERY_HCA_VPORT_PKEY          = 0x765,
	MLX5_CMD_OP_QUERY_VNIC_ENV                = 0x76f,
	MLX5_CMD_OP_QUERY_VPORT_COUNTER           = 0x770,
	MLX5_CMD_OP_ALLOC_Q_COUNTER               = 0x771,
	MLX5_CMD_OP_DEALLOC_Q_COUNTER             = 0x772,
	MLX5_CMD_OP_QUERY_Q_COUNTER               = 0x773,
	MLX5_CMD_OP_SET_MONITOR_COUNTER           = 0x774,
	MLX5_CMD_OP_ARM_MONITOR_COUNTER           = 0x775,
	MLX5_CMD_OP_SET_PP_RATE_LIMIT             = 0x780,
	MLX5_CMD_OP_QUERY_RATE_LIMIT              = 0x781,
	MLX5_CMD_OP_CREATE_SCHEDULING_ELEMENT      = 0x782,
	MLX5_CMD_OP_DESTROY_SCHEDULING_ELEMENT     = 0x783,
	MLX5_CMD_OP_QUERY_SCHEDULING_ELEMENT       = 0x784,
	MLX5_CMD_OP_MODIFY_SCHEDULING_ELEMENT      = 0x785,
	MLX5_CMD_OP_CREATE_QOS_PARA_VPORT         = 0x786,
	MLX5_CMD_OP_DESTROY_QOS_PARA_VPORT        = 0x787,
	MLX5_CMD_OP_ALLOC_PD                      = 0x800,
	MLX5_CMD_OP_DEALLOC_PD                    = 0x801,
	MLX5_CMD_OP_ALLOC_UAR                     = 0x802,
	MLX5_CMD_OP_DEALLOC_UAR                   = 0x803,
	MLX5_CMD_OP_CONFIG_INT_MODERATION         = 0x804,
	MLX5_CMD_OP_ACCESS_REG                    = 0x805,
	MLX5_CMD_OP_ATTACH_TO_MCG                 = 0x806,
	MLX5_CMD_OP_DETACH_FROM_MCG               = 0x807,
	MLX5_CMD_OP_GET_DROPPED_PACKET_LOG        = 0x80a,
	MLX5_CMD_OP_MAD_IFC                       = 0x50d,
	MLX5_CMD_OP_QUERY_MAD_DEMUX               = 0x80b,
	MLX5_CMD_OP_SET_MAD_DEMUX                 = 0x80c,
	MLX5_CMD_OP_NOP                           = 0x80d,
	MLX5_CMD_OP_ALLOC_XRCD                    = 0x80e,
	MLX5_CMD_OP_DEALLOC_XRCD                  = 0x80f,
	MLX5_CMD_OP_ALLOC_TRANSPORT_DOMAIN        = 0x816,
	MLX5_CMD_OP_DEALLOC_TRANSPORT_DOMAIN      = 0x817,
	MLX5_CMD_OP_QUERY_CONG_STATUS             = 0x822,
	MLX5_CMD_OP_MODIFY_CONG_STATUS            = 0x823,
	MLX5_CMD_OP_QUERY_CONG_PARAMS             = 0x824,
	MLX5_CMD_OP_MODIFY_CONG_PARAMS            = 0x825,
	MLX5_CMD_OP_QUERY_CONG_STATISTICS         = 0x826,
	MLX5_CMD_OP_ADD_VXLAN_UDP_DPORT           = 0x827,
	MLX5_CMD_OP_DELETE_VXLAN_UDP_DPORT        = 0x828,
	MLX5_CMD_OP_SET_L2_TABLE_ENTRY            = 0x829,
	MLX5_CMD_OP_QUERY_L2_TABLE_ENTRY          = 0x82a,
	MLX5_CMD_OP_DELETE_L2_TABLE_ENTRY         = 0x82b,
	MLX5_CMD_OP_SET_WOL_ROL                   = 0x830,
	MLX5_CMD_OP_QUERY_WOL_ROL                 = 0x831,
	MLX5_CMD_OP_CREATE_LAG                    = 0x840,
	MLX5_CMD_OP_MODIFY_LAG                    = 0x841,
	MLX5_CMD_OP_QUERY_LAG                     = 0x842,
	MLX5_CMD_OP_DESTROY_LAG                   = 0x843,
	MLX5_CMD_OP_CREATE_VPORT_LAG              = 0x844,
	MLX5_CMD_OP_DESTROY_VPORT_LAG             = 0x845,
	MLX5_CMD_OP_CREATE_TIR                    = 0x900,
	MLX5_CMD_OP_MODIFY_TIR                    = 0x901,
	MLX5_CMD_OP_DESTROY_TIR                   = 0x902,
	MLX5_CMD_OP_QUERY_TIR                     = 0x903,
	MLX5_CMD_OP_CREATE_SQ                     = 0x904,
	MLX5_CMD_OP_MODIFY_SQ                     = 0x905,
	MLX5_CMD_OP_DESTROY_SQ                    = 0x906,
	MLX5_CMD_OP_QUERY_SQ                      = 0x907,
	MLX5_CMD_OP_CREATE_RQ                     = 0x908,
	MLX5_CMD_OP_MODIFY_RQ                     = 0x909,
	MLX5_CMD_OP_SET_DELAY_DROP_PARAMS         = 0x910,
	MLX5_CMD_OP_DESTROY_RQ                    = 0x90a,
	MLX5_CMD_OP_QUERY_RQ                      = 0x90b,
	MLX5_CMD_OP_CREATE_RMP                    = 0x90c,
	MLX5_CMD_OP_MODIFY_RMP                    = 0x90d,
	MLX5_CMD_OP_DESTROY_RMP                   = 0x90e,
	MLX5_CMD_OP_QUERY_RMP                     = 0x90f,
	MLX5_CMD_OP_CREATE_TIS                    = 0x912,
	MLX5_CMD_OP_MODIFY_TIS                    = 0x913,
	MLX5_CMD_OP_DESTROY_TIS                   = 0x914,
	MLX5_CMD_OP_QUERY_TIS                     = 0x915,
	MLX5_CMD_OP_CREATE_RQT                    = 0x916,
	MLX5_CMD_OP_MODIFY_RQT                    = 0x917,
	MLX5_CMD_OP_DESTROY_RQT                   = 0x918,
	MLX5_CMD_OP_QUERY_RQT                     = 0x919,
	MLX5_CMD_OP_SET_FLOW_TABLE_ROOT		  = 0x92f,
	MLX5_CMD_OP_CREATE_FLOW_TABLE             = 0x930,
	MLX5_CMD_OP_DESTROY_FLOW_TABLE            = 0x931,
	MLX5_CMD_OP_QUERY_FLOW_TABLE              = 0x932,
	MLX5_CMD_OP_CREATE_FLOW_GROUP             = 0x933,
	MLX5_CMD_OP_DESTROY_FLOW_GROUP            = 0x934,
	MLX5_CMD_OP_QUERY_FLOW_GROUP              = 0x935,
	MLX5_CMD_OP_SET_FLOW_TABLE_ENTRY          = 0x936,
	MLX5_CMD_OP_QUERY_FLOW_TABLE_ENTRY        = 0x937,
	MLX5_CMD_OP_DELETE_FLOW_TABLE_ENTRY       = 0x938,
	MLX5_CMD_OP_ALLOC_FLOW_COUNTER            = 0x939,
	MLX5_CMD_OP_DEALLOC_FLOW_COUNTER          = 0x93a,
	MLX5_CMD_OP_QUERY_FLOW_COUNTER            = 0x93b,
	MLX5_CMD_OP_MODIFY_FLOW_TABLE             = 0x93c,
	MLX5_CMD_OP_ALLOC_PACKET_REFORMAT_CONTEXT = 0x93d,
	MLX5_CMD_OP_DEALLOC_PACKET_REFORMAT_CONTEXT = 0x93e,
	MLX5_CMD_OP_QUERY_PACKET_REFORMAT_CONTEXT = 0x93f,
	MLX5_CMD_OP_ALLOC_MODIFY_HEADER_CONTEXT   = 0x940,
	MLX5_CMD_OP_DEALLOC_MODIFY_HEADER_CONTEXT = 0x941,
	MLX5_CMD_OP_QUERY_MODIFY_HEADER_CONTEXT   = 0x942,
	MLX5_CMD_OP_FPGA_CREATE_QP                = 0x960,
	MLX5_CMD_OP_FPGA_MODIFY_QP                = 0x961,
	MLX5_CMD_OP_FPGA_QUERY_QP                 = 0x962,
	MLX5_CMD_OP_FPGA_DESTROY_QP               = 0x963,
	MLX5_CMD_OP_FPGA_QUERY_QP_COUNTERS        = 0x964,
	MLX5_CMD_OP_CREATE_GENERAL_OBJECT         = 0xa00,
	MLX5_CMD_OP_MODIFY_GENERAL_OBJECT         = 0xa01,
	MLX5_CMD_OP_QUERY_GENERAL_OBJECT          = 0xa02,
	MLX5_CMD_OP_DESTROY_GENERAL_OBJECT        = 0xa03,
	MLX5_CMD_OP_CREATE_UCTX                   = 0xa04,
	MLX5_CMD_OP_DESTROY_UCTX                  = 0xa06,
	MLX5_CMD_OP_CREATE_UMEM                   = 0xa08,
	MLX5_CMD_OP_DESTROY_UMEM                  = 0xa0a,
	MLX5_CMD_OP_SYNC_STEERING                 = 0xb00,
	MLX5_CMD_OP_MAX
};

/* Valid range for general commands that don't work over an object */
enum {
	MLX5_CMD_OP_GENERAL_START = 0xb00,
	MLX5_CMD_OP_GENERAL_END = 0xd00,
};

struct mlx5_ifc_flow_table_fields_supported_bits {
	u8         outer_dmac[0x1];
	u8         outer_smac[0x1];
	u8         outer_ether_type[0x1];
	u8         outer_ip_version[0x1];
	u8         outer_first_prio[0x1];
	u8         outer_first_cfi[0x1];
	u8         outer_first_vid[0x1];
	u8         outer_ipv4_ttl[0x1];
	u8         outer_second_prio[0x1];
	u8         outer_second_cfi[0x1];
	u8         outer_second_vid[0x1];
	u8         reserved_at_b[0x1];
	u8         outer_sip[0x1];
	u8         outer_dip[0x1];
	u8         outer_frag[0x1];
	u8         outer_ip_protocol[0x1];
	u8         outer_ip_ecn[0x1];
	u8         outer_ip_dscp[0x1];
	u8         outer_udp_sport[0x1];
	u8         outer_udp_dport[0x1];
	u8         outer_tcp_sport[0x1];
	u8         outer_tcp_dport[0x1];
	u8         outer_tcp_flags[0x1];
	u8         outer_gre_protocol[0x1];
	u8         outer_gre_key[0x1];
	u8         outer_vxlan_vni[0x1];
	u8         outer_geneve_vni[0x1];
	u8         outer_geneve_oam[0x1];
	u8         outer_geneve_protocol_type[0x1];
	u8         outer_geneve_opt_len[0x1];
	u8         reserved_at_1e[0x1];
	u8         source_eswitch_port[0x1];

	u8         inner_dmac[0x1];
	u8         inner_smac[0x1];
	u8         inner_ether_type[0x1];
	u8         inner_ip_version[0x1];
	u8         inner_first_prio[0x1];
	u8         inner_first_cfi[0x1];
	u8         inner_first_vid[0x1];
	u8         reserved_at_27[0x1];
	u8         inner_second_prio[0x1];
	u8         inner_second_cfi[0x1];
	u8         inner_second_vid[0x1];
	u8         reserved_at_2b[0x1];
	u8         inner_sip[0x1];
	u8         inner_dip[0x1];
	u8         inner_frag[0x1];
	u8         inner_ip_protocol[0x1];
	u8         inner_ip_ecn[0x1];
	u8         inner_ip_dscp[0x1];
	u8         inner_udp_sport[0x1];
	u8         inner_udp_dport[0x1];
	u8         inner_tcp_sport[0x1];
	u8         inner_tcp_dport[0x1];
	u8         inner_tcp_flags[0x1];
	u8         reserved_at_37[0x9];

	u8         geneve_tlv_option_0_data[0x1];
	u8         reserved_at_41[0x4];
	u8         outer_first_mpls_over_udp[0x4];
	u8         outer_first_mpls_over_gre[0x4];
	u8         inner_first_mpls[0x4];
	u8         outer_first_mpls[0x4];
	u8         reserved_at_55[0x2];
	u8	   outer_esp_spi[0x1];
	u8         reserved_at_58[0x2];
	u8         bth_dst_qp[0x1];
	u8         reserved_at_5b[0x5];

	u8         reserved_at_60[0x18];
	u8         metadata_reg_c_7[0x1];
	u8         metadata_reg_c_6[0x1];
	u8         metadata_reg_c_5[0x1];
	u8         metadata_reg_c_4[0x1];
	u8         metadata_reg_c_3[0x1];
	u8         metadata_reg_c_2[0x1];
	u8         metadata_reg_c_1[0x1];
	u8         metadata_reg_c_0[0x1];
};

struct mlx5_ifc_flow_table_prop_layout_bits {
	u8         ft_support[0x1];
	u8         reserved_at_1[0x1];
	u8         flow_counter[0x1];
	u8	   flow_modify_en[0x1];
	u8         modify_root[0x1];
	u8         identified_miss_table_mode[0x1];
	u8         flow_table_modify[0x1];
	u8         reformat[0x1];
	u8         decap[0x1];
	u8         reserved_at_9[0x1];
	u8         pop_vlan[0x1];
	u8         push_vlan[0x1];
	u8         reserved_at_c[0x1];
	u8         pop_vlan_2[0x1];
	u8         push_vlan_2[0x1];
	u8	   reformat_and_vlan_action[0x1];
	u8	   reserved_at_10[0x1];
	u8         sw_owner[0x1];
	u8	   reformat_l3_tunnel_to_l2[0x1];
	u8	   reformat_l2_to_l3_tunnel[0x1];
	u8	   reformat_and_modify_action[0x1];
	u8	   ignore_flow_level[0x1];
	u8         reserved_at_16[0x1];
	u8	   table_miss_action_domain[0x1];
	u8         termination_table[0x1];
	u8         reformat_and_fwd_to_table[0x1];
	u8         reserved_at_1a[0x6];
	u8         termination_table_raw_traffic[0x1];
	u8         reserved_at_21[0x1];
	u8         log_max_ft_size[0x6];
	u8         log_max_modify_header_context[0x8];
	u8         max_modify_header_actions[0x8];
	u8         max_ft_level[0x8];

	u8         reserved_at_40[0x20];

	u8         reserved_at_60[0x18];
	u8         log_max_ft_num[0x8];

	u8         reserved_at_80[0x18];
	u8         log_max_destination[0x8];

	u8         log_max_flow_counter[0x8];
	u8         reserved_at_a8[0x10];
	u8         log_max_flow[0x8];

	u8         reserved_at_c0[0x40];

	struct mlx5_ifc_flow_table_fields_supported_bits ft_field_support;

	struct mlx5_ifc_flow_table_fields_supported_bits ft_field_bitmask_support;
};

struct mlx5_ifc_odp_per_transport_service_cap_bits {
	u8         send[0x1];
	u8         receive[0x1];
	u8         write[0x1];
	u8         read[0x1];
	u8         atomic[0x1];
	u8         srq_receive[0x1];
	u8         reserved_at_6[0x1a];
};

struct mlx5_ifc_fte_match_set_lyr_2_4_bits {
	u8         smac_47_16[0x20];

	u8         smac_15_0[0x10];
	u8         ethertype[0x10];

	u8         dmac_47_16[0x20];

	u8         dmac_15_0[0x10];
	u8         first_prio[0x3];
	u8         first_cfi[0x1];
	u8         first_vid[0xc];

	u8         ip_protocol[0x8];
	u8         ip_dscp[0x6];
	u8         ip_ecn[0x2];
	u8         cvlan_tag[0x1];
	u8         svlan_tag[0x1];
	u8         frag[0x1];
	u8         ip_version[0x4];
	u8         tcp_flags[0x9];

	u8         tcp_sport[0x10];
	u8         tcp_dport[0x10];

	u8         reserved_at_c0[0x18];
	u8         ttl_hoplimit[0x8];

	u8         udp_sport[0x10];
	u8         udp_dport[0x10];

	union mlx5_ifc_ipv6_layout_ipv4_layout_auto_bits src_ipv4_src_ipv6;

	union mlx5_ifc_ipv6_layout_ipv4_layout_auto_bits dst_ipv4_dst_ipv6;
};

struct mlx5_ifc_nvgre_key_bits {
	u8 hi[0x18];
	u8 lo[0x8];
};

union mlx5_ifc_gre_key_bits {
	struct mlx5_ifc_nvgre_key_bits nvgre;
	u8 key[0x20];
};

struct mlx5_ifc_fte_match_set_misc_bits {
	u8         gre_c_present[0x1];
	u8         reserved_at_1[0x1];
	u8         gre_k_present[0x1];
	u8         gre_s_present[0x1];
	u8         source_vhca_port[0x4];
	u8         source_sqn[0x18];

	u8         source_eswitch_owner_vhca_id[0x10];
	u8         source_port[0x10];

	u8         outer_second_prio[0x3];
	u8         outer_second_cfi[0x1];
	u8         outer_second_vid[0xc];
	u8         inner_second_prio[0x3];
	u8         inner_second_cfi[0x1];
	u8         inner_second_vid[0xc];

	u8         outer_second_cvlan_tag[0x1];
	u8         inner_second_cvlan_tag[0x1];
	u8         outer_second_svlan_tag[0x1];
	u8         inner_second_svlan_tag[0x1];
	u8         reserved_at_64[0xc];
	u8         gre_protocol[0x10];

	union mlx5_ifc_gre_key_bits gre_key;

	u8         vxlan_vni[0x18];
	u8         reserved_at_b8[0x8];

	u8         geneve_vni[0x18];
	u8         reserved_at_d8[0x7];
	u8         geneve_oam[0x1];

	u8         reserved_at_e0[0xc];
	u8         outer_ipv6_flow_label[0x14];

	u8         reserved_at_100[0xc];
	u8         inner_ipv6_flow_label[0x14];

	u8         reserved_at_120[0xa];
	u8         geneve_opt_len[0x6];
	u8         geneve_protocol_type[0x10];

	u8         reserved_at_140[0x8];
	u8         bth_dst_qp[0x18];
	u8	   reserved_at_160[0x20];
	u8	   outer_esp_spi[0x20];
	u8         reserved_at_1a0[0x60];
};

struct mlx5_ifc_fte_match_mpls_bits {
	u8         mpls_label[0x14];
	u8         mpls_exp[0x3];
	u8         mpls_s_bos[0x1];
	u8         mpls_ttl[0x8];
};

struct mlx5_ifc_fte_match_set_misc2_bits {
	struct mlx5_ifc_fte_match_mpls_bits outer_first_mpls;

	struct mlx5_ifc_fte_match_mpls_bits inner_first_mpls;

	struct mlx5_ifc_fte_match_mpls_bits outer_first_mpls_over_gre;

	struct mlx5_ifc_fte_match_mpls_bits outer_first_mpls_over_udp;

	u8         metadata_reg_c_7[0x20];

	u8         metadata_reg_c_6[0x20];

	u8         metadata_reg_c_5[0x20];

	u8         metadata_reg_c_4[0x20];

	u8         metadata_reg_c_3[0x20];

	u8         metadata_reg_c_2[0x20];

	u8         metadata_reg_c_1[0x20];

	u8         metadata_reg_c_0[0x20];

	u8         metadata_reg_a[0x20];

	u8         reserved_at_1a0[0x60];
};

struct mlx5_ifc_fte_match_set_misc3_bits {
	u8         inner_tcp_seq_num[0x20];

	u8         outer_tcp_seq_num[0x20];

	u8         inner_tcp_ack_num[0x20];

	u8         outer_tcp_ack_num[0x20];

	u8	   reserved_at_80[0x8];
	u8         outer_vxlan_gpe_vni[0x18];

	u8         outer_vxlan_gpe_next_protocol[0x8];
	u8         outer_vxlan_gpe_flags[0x8];
	u8	   reserved_at_b0[0x10];

	u8	   icmp_header_data[0x20];

	u8	   icmpv6_header_data[0x20];

	u8	   icmp_type[0x8];
	u8	   icmp_code[0x8];
	u8	   icmpv6_type[0x8];
	u8	   icmpv6_code[0x8];

	u8         geneve_tlv_option_0_data[0x20];

	u8         reserved_at_140[0xc0];
};

struct mlx5_ifc_cmd_pas_bits {
	u8         pa_h[0x20];

	u8         pa_l[0x14];
	u8         reserved_at_34[0xc];
};

struct mlx5_ifc_uint64_bits {
	u8         hi[0x20];

	u8         lo[0x20];
};

enum {
	MLX5_ADS_STAT_RATE_NO_LIMIT  = 0x0,
	MLX5_ADS_STAT_RATE_2_5GBPS   = 0x7,
	MLX5_ADS_STAT_RATE_10GBPS    = 0x8,
	MLX5_ADS_STAT_RATE_30GBPS    = 0x9,
	MLX5_ADS_STAT_RATE_5GBPS     = 0xa,
	MLX5_ADS_STAT_RATE_20GBPS    = 0xb,
	MLX5_ADS_STAT_RATE_40GBPS    = 0xc,
	MLX5_ADS_STAT_RATE_60GBPS    = 0xd,
	MLX5_ADS_STAT_RATE_80GBPS    = 0xe,
	MLX5_ADS_STAT_RATE_120GBPS   = 0xf,
};

struct mlx5_ifc_ads_bits {
	u8         fl[0x1];
	u8         free_ar[0x1];
	u8         reserved_at_2[0xe];
	u8         pkey_index[0x10];

	u8         reserved_at_20[0x8];
	u8         grh[0x1];
	u8         mlid[0x7];
	u8         rlid[0x10];

	u8         ack_timeout[0x5];
	u8         reserved_at_45[0x3];
	u8         src_addr_index[0x8];
	u8         reserved_at_50[0x4];
	u8         stat_rate[0x4];
	u8         hop_limit[0x8];

	u8         reserved_at_60[0x4];
	u8         tclass[0x8];
	u8         flow_label[0x14];

	u8         rgid_rip[16][0x8];

	u8         reserved_at_100[0x4];
	u8         f_dscp[0x1];
	u8         f_ecn[0x1];
	u8         reserved_at_106[0x1];
	u8         f_eth_prio[0x1];
	u8         ecn[0x2];
	u8         dscp[0x6];
	u8         udp_sport[0x10];

	u8         dei_cfi[0x1];
	u8         eth_prio[0x3];
	u8         sl[0x4];
	u8         vhca_port_num[0x8];
	u8         rmac_47_32[0x10];

	u8         rmac_31_0[0x20];
};

struct mlx5_ifc_flow_table_nic_cap_bits {
	u8         nic_rx_multi_path_tirs[0x1];
	u8         nic_rx_multi_path_tirs_fts[0x1];
	u8         allow_sniffer_and_nic_rx_shared_tir[0x1];
	u8	   reserved_at_3[0x4];
	u8	   sw_owner_reformat_supported[0x1];
	u8	   reserved_at_8[0x18];

	u8	   encap_general_header[0x1];
	u8	   reserved_at_21[0xa];
	u8	   log_max_packet_reformat_context[0x5];
	u8	   reserved_at_30[0x6];
	u8	   max_encap_header_size[0xa];
	u8	   reserved_at_40[0x1c0];

	struct mlx5_ifc_flow_table_prop_layout_bits flow_table_properties_nic_receive;

	struct mlx5_ifc_flow_table_prop_layout_bits flow_table_properties_nic_receive_rdma;

	struct mlx5_ifc_flow_table_prop_layout_bits flow_table_properties_nic_receive_sniffer;

	struct mlx5_ifc_flow_table_prop_layout_bits flow_table_properties_nic_transmit;

	struct mlx5_ifc_flow_table_prop_layout_bits flow_table_properties_nic_transmit_rdma;

	struct mlx5_ifc_flow_table_prop_layout_bits flow_table_properties_nic_transmit_sniffer;

	u8         reserved_at_e00[0x1200];

	u8         sw_steering_nic_rx_action_drop_icm_address[0x40];

	u8         sw_steering_nic_tx_action_drop_icm_address[0x40];

	u8         sw_steering_nic_tx_action_allow_icm_address[0x40];

	u8         reserved_at_20c0[0x5f40];
};

enum {
	MLX5_FDB_TO_VPORT_REG_C_0 = 0x01,
	MLX5_FDB_TO_VPORT_REG_C_1 = 0x02,
	MLX5_FDB_TO_VPORT_REG_C_2 = 0x04,
	MLX5_FDB_TO_VPORT_REG_C_3 = 0x08,
	MLX5_FDB_TO_VPORT_REG_C_4 = 0x10,
	MLX5_FDB_TO_VPORT_REG_C_5 = 0x20,
	MLX5_FDB_TO_VPORT_REG_C_6 = 0x40,
	MLX5_FDB_TO_VPORT_REG_C_7 = 0x80,
};

struct mlx5_ifc_flow_table_eswitch_cap_bits {
	u8      fdb_to_vport_reg_c_id[0x8];
	u8      reserved_at_8[0xd];
	u8      fdb_modify_header_fwd_to_table[0x1];
	u8      reserved_at_16[0x1];
	u8      flow_source[0x1];
	u8      reserved_at_18[0x2];
	u8      multi_fdb_encap[0x1];
	u8      egress_acl_forward_to_vport[0x1];
	u8      fdb_multi_path_to_table[0x1];
	u8      reserved_at_1d[0x3];

	u8      reserved_at_20[0x1e0];

	struct mlx5_ifc_flow_table_prop_layout_bits flow_table_properties_nic_esw_fdb;

	struct mlx5_ifc_flow_table_prop_layout_bits flow_table_properties_esw_acl_ingress;

	struct mlx5_ifc_flow_table_prop_layout_bits flow_table_properties_esw_acl_egress;

	u8      reserved_at_800[0x1000];

	u8      sw_steering_fdb_action_drop_icm_address_rx[0x40];

	u8      sw_steering_fdb_action_drop_icm_address_tx[0x40];

	u8      sw_steering_uplink_icm_address_rx[0x40];

	u8      sw_steering_uplink_icm_address_tx[0x40];

	u8      reserved_at_1900[0x6700];
};

enum {
	MLX5_COUNTER_SOURCE_ESWITCH = 0x0,
	MLX5_COUNTER_FLOW_ESWITCH   = 0x1,
};

struct mlx5_ifc_e_switch_cap_bits {
	u8         vport_svlan_strip[0x1];
	u8         vport_cvlan_strip[0x1];
	u8         vport_svlan_insert[0x1];
	u8         vport_cvlan_insert_if_not_exist[0x1];
	u8         vport_cvlan_insert_overwrite[0x1];
	u8         reserved_at_5[0x3];
	u8         esw_uplink_ingress_acl[0x1];
	u8         reserved_at_9[0x10];
	u8         esw_functions_changed[0x1];
	u8         reserved_at_1a[0x1];
	u8         ecpf_vport_exists[0x1];
	u8         counter_eswitch_affinity[0x1];
	u8         merged_eswitch[0x1];
	u8         nic_vport_node_guid_modify[0x1];
	u8         nic_vport_port_guid_modify[0x1];

	u8         vxlan_encap_decap[0x1];
	u8         nvgre_encap_decap[0x1];
	u8         reserved_at_22[0x1];
	u8         log_max_fdb_encap_uplink[0x5];
	u8         reserved_at_21[0x3];
	u8         log_max_packet_reformat_context[0x5];
	u8         reserved_2b[0x6];
	u8         max_encap_header_size[0xa];

	u8         reserved_at_40[0xb];
	u8         log_max_esw_sf[0x5];
	u8         esw_sf_base_id[0x10];

	u8         reserved_at_60[0x7a0];

};

struct mlx5_ifc_qos_cap_bits {
	u8         packet_pacing[0x1];
	u8         esw_scheduling[0x1];
	u8         esw_bw_share[0x1];
	u8         esw_rate_limit[0x1];
	u8         reserved_at_4[0x1];
	u8         packet_pacing_burst_bound[0x1];
	u8         packet_pacing_typical_size[0x1];
	u8         reserved_at_7[0x4];
	u8         packet_pacing_uid[0x1];
	u8         reserved_at_c[0x14];

	u8         reserved_at_20[0x20];

	u8         packet_pacing_max_rate[0x20];

	u8         packet_pacing_min_rate[0x20];

	u8         reserved_at_80[0x10];
	u8         packet_pacing_rate_table_size[0x10];

	u8         esw_element_type[0x10];
	u8         esw_tsar_type[0x10];

	u8         reserved_at_c0[0x10];
	u8         max_qos_para_vport[0x10];

	u8         max_tsar_bw_share[0x20];

	u8         reserved_at_100[0x700];
};

struct mlx5_ifc_debug_cap_bits {
	u8         core_dump_general[0x1];
	u8         core_dump_qp[0x1];
	u8         reserved_at_2[0x7];
	u8         resource_dump[0x1];
	u8         reserved_at_a[0x16];

	u8         reserved_at_20[0x2];
	u8         stall_detect[0x1];
	u8         reserved_at_23[0x1d];

	u8         reserved_at_40[0x7c0];
};

struct mlx5_ifc_per_protocol_networking_offload_caps_bits {
	u8         csum_cap[0x1];
	u8         vlan_cap[0x1];
	u8         lro_cap[0x1];
	u8         lro_psh_flag[0x1];
	u8         lro_time_stamp[0x1];
	u8         reserved_at_5[0x2];
	u8         wqe_vlan_insert[0x1];
	u8         self_lb_en_modifiable[0x1];
	u8         reserved_at_9[0x2];
	u8         max_lso_cap[0x5];
	u8         multi_pkt_send_wqe[0x2];
	u8	   wqe_inline_mode[0x2];
	u8         rss_ind_tbl_cap[0x4];
	u8         reg_umr_sq[0x1];
	u8         scatter_fcs[0x1];
	u8         enhanced_multi_pkt_send_wqe[0x1];
	u8         tunnel_lso_const_out_ip_id[0x1];
	u8         reserved_at_1c[0x2];
	u8         tunnel_stateless_gre[0x1];
	u8         tunnel_stateless_vxlan[0x1];

	u8         swp[0x1];
	u8         swp_csum[0x1];
	u8         swp_lso[0x1];
	u8         cqe_checksum_full[0x1];
	u8         tunnel_stateless_geneve_tx[0x1];
	u8         tunnel_stateless_mpls_over_udp[0x1];
	u8         tunnel_stateless_mpls_over_gre[0x1];
	u8         tunnel_stateless_vxlan_gpe[0x1];
	u8         tunnel_stateless_ipv4_over_vxlan[0x1];
	u8         tunnel_stateless_ip_over_ip[0x1];
	u8         insert_trailer[0x1];
	u8         reserved_at_2b[0x5];
	u8         max_vxlan_udp_ports[0x8];
	u8         reserved_at_38[0x6];
	u8         max_geneve_opt_len[0x1];
	u8         tunnel_stateless_geneve_rx[0x1];

	u8         reserved_at_40[0x10];
	u8         lro_min_mss_size[0x10];

	u8         reserved_at_60[0x120];

	u8         lro_timer_supported_periods[4][0x20];

	u8         reserved_at_200[0x600];
};

struct mlx5_ifc_roce_cap_bits {
	u8         roce_apm[0x1];
	u8         reserved_at_1[0x3];
	u8         sw_r_roce_src_udp_port[0x1];
	u8         reserved_at_5[0x1b];

	u8         reserved_at_20[0x60];

	u8         reserved_at_80[0xc];
	u8         l3_type[0x4];
	u8         reserved_at_90[0x8];
	u8         roce_version[0x8];

	u8         reserved_at_a0[0x10];
	u8         r_roce_dest_udp_port[0x10];

	u8         r_roce_max_src_udp_port[0x10];
	u8         r_roce_min_src_udp_port[0x10];

	u8         reserved_at_e0[0x10];
	u8         roce_address_table_size[0x10];

	u8         reserved_at_100[0x700];
};

struct mlx5_ifc_sync_steering_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0xc0];
};

struct mlx5_ifc_sync_steering_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_device_mem_cap_bits {
	u8         memic[0x1];
	u8         reserved_at_1[0x1f];

	u8         reserved_at_20[0xb];
	u8         log_min_memic_alloc_size[0x5];
	u8         reserved_at_30[0x8];
	u8	   log_max_memic_addr_alignment[0x8];

	u8         memic_bar_start_addr[0x40];

	u8         memic_bar_size[0x20];

	u8         max_memic_size[0x20];

	u8         steering_sw_icm_start_address[0x40];

	u8         reserved_at_100[0x8];
	u8         log_header_modify_sw_icm_size[0x8];
	u8         reserved_at_110[0x2];
	u8         log_sw_icm_alloc_granularity[0x6];
	u8         log_steering_sw_icm_size[0x8];

	u8         reserved_at_120[0x20];

	u8         header_modify_sw_icm_start_address[0x40];

	u8         reserved_at_180[0x680];
};

struct mlx5_ifc_device_event_cap_bits {
	u8         user_affiliated_events[4][0x40];

	u8         user_unaffiliated_events[4][0x40];
};

struct mlx5_ifc_device_virtio_emulation_cap_bits {
	u8         reserved_at_0[0x20];

	u8         reserved_at_20[0x13];
	u8         log_doorbell_stride[0x5];
	u8         reserved_at_38[0x3];
	u8         log_doorbell_bar_size[0x5];

	u8         doorbell_bar_offset[0x40];

	u8         reserved_at_80[0x780];
};

enum {
	MLX5_ATOMIC_CAPS_ATOMIC_SIZE_QP_1_BYTE     = 0x0,
	MLX5_ATOMIC_CAPS_ATOMIC_SIZE_QP_2_BYTES    = 0x2,
	MLX5_ATOMIC_CAPS_ATOMIC_SIZE_QP_4_BYTES    = 0x4,
	MLX5_ATOMIC_CAPS_ATOMIC_SIZE_QP_8_BYTES    = 0x8,
	MLX5_ATOMIC_CAPS_ATOMIC_SIZE_QP_16_BYTES   = 0x10,
	MLX5_ATOMIC_CAPS_ATOMIC_SIZE_QP_32_BYTES   = 0x20,
	MLX5_ATOMIC_CAPS_ATOMIC_SIZE_QP_64_BYTES   = 0x40,
	MLX5_ATOMIC_CAPS_ATOMIC_SIZE_QP_128_BYTES  = 0x80,
	MLX5_ATOMIC_CAPS_ATOMIC_SIZE_QP_256_BYTES  = 0x100,
};

enum {
	MLX5_ATOMIC_CAPS_ATOMIC_SIZE_DC_1_BYTE     = 0x1,
	MLX5_ATOMIC_CAPS_ATOMIC_SIZE_DC_2_BYTES    = 0x2,
	MLX5_ATOMIC_CAPS_ATOMIC_SIZE_DC_4_BYTES    = 0x4,
	MLX5_ATOMIC_CAPS_ATOMIC_SIZE_DC_8_BYTES    = 0x8,
	MLX5_ATOMIC_CAPS_ATOMIC_SIZE_DC_16_BYTES   = 0x10,
	MLX5_ATOMIC_CAPS_ATOMIC_SIZE_DC_32_BYTES   = 0x20,
	MLX5_ATOMIC_CAPS_ATOMIC_SIZE_DC_64_BYTES   = 0x40,
	MLX5_ATOMIC_CAPS_ATOMIC_SIZE_DC_128_BYTES  = 0x80,
	MLX5_ATOMIC_CAPS_ATOMIC_SIZE_DC_256_BYTES  = 0x100,
};

struct mlx5_ifc_atomic_caps_bits {
	u8         reserved_at_0[0x40];

	u8         atomic_req_8B_endianness_mode[0x2];
	u8         reserved_at_42[0x4];
	u8         supported_atomic_req_8B_endianness_mode_1[0x1];

	u8         reserved_at_47[0x19];

	u8         reserved_at_60[0x20];

	u8         reserved_at_80[0x10];
	u8         atomic_operations[0x10];

	u8         reserved_at_a0[0x10];
	u8         atomic_size_qp[0x10];

	u8         reserved_at_c0[0x10];
	u8         atomic_size_dc[0x10];

	u8         reserved_at_e0[0x720];
};

struct mlx5_ifc_odp_cap_bits {
	u8         reserved_at_0[0x40];

	u8         sig[0x1];
	u8         reserved_at_41[0x1f];

	u8         reserved_at_60[0x20];

	struct mlx5_ifc_odp_per_transport_service_cap_bits rc_odp_caps;

	struct mlx5_ifc_odp_per_transport_service_cap_bits uc_odp_caps;

	struct mlx5_ifc_odp_per_transport_service_cap_bits ud_odp_caps;

	struct mlx5_ifc_odp_per_transport_service_cap_bits xrc_odp_caps;

	struct mlx5_ifc_odp_per_transport_service_cap_bits dc_odp_caps;

	u8         reserved_at_120[0x6E0];
};

struct mlx5_ifc_calc_op {
	u8        reserved_at_0[0x10];
	u8        reserved_at_10[0x9];
	u8        op_swap_endianness[0x1];
	u8        op_min[0x1];
	u8        op_xor[0x1];
	u8        op_or[0x1];
	u8        op_and[0x1];
	u8        op_max[0x1];
	u8        op_add[0x1];
};

struct mlx5_ifc_vector_calc_cap_bits {
	u8         calc_matrix[0x1];
	u8         reserved_at_1[0x1f];
	u8         reserved_at_20[0x8];
	u8         max_vec_count[0x8];
	u8         reserved_at_30[0xd];
	u8         max_chunk_size[0x3];
	struct mlx5_ifc_calc_op calc0;
	struct mlx5_ifc_calc_op calc1;
	struct mlx5_ifc_calc_op calc2;
	struct mlx5_ifc_calc_op calc3;

	u8         reserved_at_c0[0x720];
};

struct mlx5_ifc_tls_cap_bits {
	u8         tls_1_2_aes_gcm_128[0x1];
	u8         tls_1_3_aes_gcm_128[0x1];
	u8         tls_1_2_aes_gcm_256[0x1];
	u8         tls_1_3_aes_gcm_256[0x1];
	u8         reserved_at_4[0x1c];

	u8         reserved_at_20[0x7e0];
};

struct mlx5_ifc_ipsec_cap_bits {
	u8         ipsec_full_offload[0x1];
	u8         ipsec_crypto_offload[0x1];
	u8         ipsec_esn[0x1];
	u8         ipsec_crypto_esp_aes_gcm_256_encrypt[0x1];
	u8         ipsec_crypto_esp_aes_gcm_128_encrypt[0x1];
	u8         ipsec_crypto_esp_aes_gcm_256_decrypt[0x1];
	u8         ipsec_crypto_esp_aes_gcm_128_decrypt[0x1];
	u8         reserved_at_7[0x4];
	u8         log_max_ipsec_offload[0x5];
	u8         reserved_at_10[0x10];

	u8         min_log_ipsec_full_replay_window[0x8];
	u8         max_log_ipsec_full_replay_window[0x8];
	u8         reserved_at_30[0x7d0];
};

enum {
	MLX5_WQ_TYPE_LINKED_LIST  = 0x0,
	MLX5_WQ_TYPE_CYCLIC       = 0x1,
	MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ = 0x2,
	MLX5_WQ_TYPE_CYCLIC_STRIDING_RQ = 0x3,
};

enum {
	MLX5_WQ_END_PAD_MODE_NONE   = 0x0,
	MLX5_WQ_END_PAD_MODE_ALIGN  = 0x1,
};

enum {
	MLX5_CMD_HCA_CAP_GID_TABLE_SIZE_8_GID_ENTRIES    = 0x0,
	MLX5_CMD_HCA_CAP_GID_TABLE_SIZE_16_GID_ENTRIES   = 0x1,
	MLX5_CMD_HCA_CAP_GID_TABLE_SIZE_32_GID_ENTRIES   = 0x2,
	MLX5_CMD_HCA_CAP_GID_TABLE_SIZE_64_GID_ENTRIES   = 0x3,
	MLX5_CMD_HCA_CAP_GID_TABLE_SIZE_128_GID_ENTRIES  = 0x4,
};

enum {
	MLX5_CMD_HCA_CAP_PKEY_TABLE_SIZE_128_ENTRIES  = 0x0,
	MLX5_CMD_HCA_CAP_PKEY_TABLE_SIZE_256_ENTRIES  = 0x1,
	MLX5_CMD_HCA_CAP_PKEY_TABLE_SIZE_512_ENTRIES  = 0x2,
	MLX5_CMD_HCA_CAP_PKEY_TABLE_SIZE_1K_ENTRIES   = 0x3,
	MLX5_CMD_HCA_CAP_PKEY_TABLE_SIZE_2K_ENTRIES   = 0x4,
	MLX5_CMD_HCA_CAP_PKEY_TABLE_SIZE_4K_ENTRIES   = 0x5,
};

enum {
	MLX5_CMD_HCA_CAP_PORT_TYPE_IB        = 0x0,
	MLX5_CMD_HCA_CAP_PORT_TYPE_ETHERNET  = 0x1,
};

enum {
	MLX5_CMD_HCA_CAP_CMDIF_CHECKSUM_DISABLED       = 0x0,
	MLX5_CMD_HCA_CAP_CMDIF_CHECKSUM_INITIAL_STATE  = 0x1,
	MLX5_CMD_HCA_CAP_CMDIF_CHECKSUM_ENABLED        = 0x3,
};

enum {
	MLX5_CAP_PORT_TYPE_IB  = 0x0,
	MLX5_CAP_PORT_TYPE_ETH = 0x1,
};

enum {
	MLX5_CAP_UMR_FENCE_STRONG	= 0x0,
	MLX5_CAP_UMR_FENCE_SMALL	= 0x1,
	MLX5_CAP_UMR_FENCE_NONE		= 0x2,
};

enum {
	MLX5_FLEX_PARSER_GENEVE_ENABLED		= 1 << 3,
	MLX5_FLEX_PARSER_VXLAN_GPE_ENABLED	= 1 << 7,
	MLX5_FLEX_PARSER_ICMP_V4_ENABLED	= 1 << 8,
	MLX5_FLEX_PARSER_ICMP_V6_ENABLED	= 1 << 9,
};

enum {
	MLX5_UCTX_CAP_RAW_TX = 1UL << 0,
	MLX5_UCTX_CAP_INTERNAL_DEV_RES = 1UL << 1,
};

#define MLX5_FC_BULK_SIZE_FACTOR 128

enum mlx5_fc_bulk_alloc_bitmask {
	MLX5_FC_BULK_128   = (1 << 0),
	MLX5_FC_BULK_256   = (1 << 1),
	MLX5_FC_BULK_512   = (1 << 2),
	MLX5_FC_BULK_1024  = (1 << 3),
	MLX5_FC_BULK_2048  = (1 << 4),
	MLX5_FC_BULK_4096  = (1 << 5),
	MLX5_FC_BULK_8192  = (1 << 6),
	MLX5_FC_BULK_16384 = (1 << 7),
};

#define MLX5_FC_BULK_NUM_FCS(fc_enum) (MLX5_FC_BULK_SIZE_FACTOR * (fc_enum))

struct mlx5_ifc_cmd_hca_cap_bits {
	u8         reserved_at_0[0x30];
	u8         vhca_id[0x10];

	u8         reserved_at_40[0x40];

	u8         log_max_srq_sz[0x8];
	u8         log_max_qp_sz[0x8];
	u8         event_cap[0x1];
	u8         reserved_at_91[0x7];
	u8         prio_tag_required[0x1];
	u8         reserved_at_99[0x2];
	u8         log_max_qp[0x5];

	u8         reserved_at_a0[0x3];
	u8	   ece_support[0x1];
	u8	   reserved_at_a4[0x7];
	u8         log_max_srq[0x5];
	u8         reserved_at_b0[0x10];

	u8         max_sgl_for_optimized_performance[0x8];
	u8         log_max_cq_sz[0x8];
	u8         reserved_at_d0[0xb];
	u8         log_max_cq[0x5];

	u8         log_max_eq_sz[0x8];
	u8         relaxed_ordering_write[0x1];
	u8         relaxed_ordering_read[0x1];
	u8         log_max_mkey[0x6];
	u8         reserved_at_f0[0x8];
	u8         dump_fill_mkey[0x1];
	u8         reserved_at_f9[0x2];
	u8         fast_teardown[0x1];
	u8         log_max_eq[0x4];

	u8         max_indirection[0x8];
	u8         fixed_buffer_size[0x1];
	u8         log_max_mrw_sz[0x7];
	u8         force_teardown[0x1];
	u8         reserved_at_111[0x1];
	u8         log_max_bsf_list_size[0x6];
	u8         umr_extended_translation_offset[0x1];
	u8         null_mkey[0x1];
	u8         log_max_klm_list_size[0x6];

	u8         reserved_at_120[0xa];
	u8         log_max_ra_req_dc[0x6];
	u8         reserved_at_130[0xa];
	u8         log_max_ra_res_dc[0x6];

	u8         reserved_at_140[0x6];
	u8         release_all_pages[0x1];
	u8         reserved_at_147[0x2];
	u8         roce_accl[0x1];
	u8         log_max_ra_req_qp[0x6];
	u8         reserved_at_150[0xa];
	u8         log_max_ra_res_qp[0x6];

	u8         end_pad[0x1];
	u8         cc_query_allowed[0x1];
	u8         cc_modify_allowed[0x1];
	u8         start_pad[0x1];
	u8         cache_line_128byte[0x1];
	u8         reserved_at_165[0x4];
	u8         rts2rts_qp_counters_set_id[0x1];
	u8         reserved_at_16a[0x2];
	u8         vnic_env_int_rq_oob[0x1];
	u8         sbcam_reg[0x1];
	u8         reserved_at_16e[0x1];
	u8         qcam_reg[0x1];
	u8         gid_table_size[0x10];

	u8         out_of_seq_cnt[0x1];
	u8         vport_counters[0x1];
	u8         retransmission_q_counters[0x1];
	u8         debug[0x1];
	u8         modify_rq_counter_set_id[0x1];
	u8         rq_delay_drop[0x1];
	u8         max_qp_cnt[0xa];
	u8         pkey_table_size[0x10];

	u8         vport_group_manager[0x1];
	u8         vhca_group_manager[0x1];
	u8         ib_virt[0x1];
	u8         eth_virt[0x1];
	u8         vnic_env_queue_counters[0x1];
	u8         ets[0x1];
	u8         nic_flow_table[0x1];
	u8         eswitch_manager[0x1];
	u8         device_memory[0x1];
	u8         mcam_reg[0x1];
	u8         pcam_reg[0x1];
	u8         local_ca_ack_delay[0x5];
	u8         port_module_event[0x1];
	u8         enhanced_error_q_counters[0x1];
	u8         ports_check[0x1];
	u8         reserved_at_1b3[0x1];
	u8         disable_link_up[0x1];
	u8         beacon_led[0x1];
	u8         port_type[0x2];
	u8         num_ports[0x8];

	u8         reserved_at_1c0[0x1];
	u8         pps[0x1];
	u8         pps_modify[0x1];
	u8         log_max_msg[0x5];
	u8         reserved_at_1c8[0x4];
	u8         max_tc[0x4];
	u8         temp_warn_event[0x1];
	u8         dcbx[0x1];
	u8         general_notification_event[0x1];
	u8         reserved_at_1d3[0x2];
	u8         fpga[0x1];
	u8         rol_s[0x1];
	u8         rol_g[0x1];
	u8         reserved_at_1d8[0x1];
	u8         wol_s[0x1];
	u8         wol_g[0x1];
	u8         wol_a[0x1];
	u8         wol_b[0x1];
	u8         wol_m[0x1];
	u8         wol_u[0x1];
	u8         wol_p[0x1];

	u8         stat_rate_support[0x10];
	u8         reserved_at_1f0[0x1];
	u8         pci_sync_for_fw_update_event[0x1];
	u8         reserved_at_1f2[0x6];
	u8         init2_lag_tx_port_affinity[0x1];
	u8         reserved_at_1fa[0x3];
	u8         cqe_version[0x4];

	u8         compact_address_vector[0x1];
	u8         striding_rq[0x1];
	u8         reserved_at_202[0x1];
	u8         ipoib_enhanced_offloads[0x1];
	u8         ipoib_basic_offloads[0x1];
	u8         reserved_at_205[0x1];
	u8         repeated_block_disabled[0x1];
	u8         umr_modify_entity_size_disabled[0x1];
	u8         umr_modify_atomic_disabled[0x1];
	u8         umr_indirect_mkey_disabled[0x1];
	u8         umr_fence[0x2];
	u8         dc_req_scat_data_cqe[0x1];
	u8         reserved_at_20d[0x2];
	u8         drain_sigerr[0x1];
	u8         cmdif_checksum[0x2];
	u8         sigerr_cqe[0x1];
	u8         reserved_at_213[0x1];
	u8         wq_signature[0x1];
	u8         sctr_data_cqe[0x1];
	u8         reserved_at_216[0x1];
	u8         sho[0x1];
	u8         tph[0x1];
	u8         rf[0x1];
	u8         dct[0x1];
	u8         qos[0x1];
	u8         eth_net_offloads[0x1];
	u8         roce[0x1];
	u8         atomic[0x1];
	u8         reserved_at_21f[0x1];

	u8         cq_oi[0x1];
	u8         cq_resize[0x1];
	u8         cq_moderation[0x1];
	u8         reserved_at_223[0x3];
	u8         cq_eq_remap[0x1];
	u8         pg[0x1];
	u8         block_lb_mc[0x1];
	u8         reserved_at_229[0x1];
	u8         scqe_break_moderation[0x1];
	u8         cq_period_start_from_cqe[0x1];
	u8         cd[0x1];
	u8         reserved_at_22d[0x1];
	u8         apm[0x1];
	u8         vector_calc[0x1];
	u8         umr_ptr_rlky[0x1];
	u8	   imaicl[0x1];
	u8	   qp_packet_based[0x1];
	u8         reserved_at_233[0x3];
	u8         qkv[0x1];
	u8         pkv[0x1];
	u8         set_deth_sqpn[0x1];
	u8         reserved_at_239[0x3];
	u8         xrc[0x1];
	u8         ud[0x1];
	u8         uc[0x1];
	u8         rc[0x1];

	u8         uar_4k[0x1];
	u8         reserved_at_241[0x9];
	u8         uar_sz[0x6];
	u8         reserved_at_250[0x8];
	u8         log_pg_sz[0x8];

	u8         bf[0x1];
	u8         driver_version[0x1];
	u8         pad_tx_eth_packet[0x1];
	u8         reserved_at_263[0x8];
	u8         log_bf_reg_size[0x5];

	u8         reserved_at_270[0x8];
	u8         lag_tx_port_affinity[0x1];
	u8         reserved_at_279[0x2];
	u8         lag_master[0x1];
	u8         num_lag_ports[0x4];

	u8         reserved_at_280[0x10];
	u8         max_wqe_sz_sq[0x10];

	u8         reserved_at_2a0[0x10];
	u8         max_wqe_sz_rq[0x10];

	u8         max_flow_counter_31_16[0x10];
	u8         max_wqe_sz_sq_dc[0x10];

	u8         reserved_at_2e0[0x7];
	u8         max_qp_mcg[0x19];

	u8         reserved_at_300[0x10];
	u8         flow_counter_bulk_alloc[0x8];
	u8         log_max_mcg[0x8];

	u8         reserved_at_320[0x3];
	u8         log_max_transport_domain[0x5];
	u8         reserved_at_328[0x3];
	u8         log_max_pd[0x5];
	u8         reserved_at_330[0xb];
	u8         log_max_xrcd[0x5];

	u8         nic_receive_steering_discard[0x1];
	u8         receive_discard_vport_down[0x1];
	u8         transmit_discard_vport_down[0x1];
	u8         reserved_at_343[0x5];
	u8         log_max_flow_counter_bulk[0x8];
	u8         max_flow_counter_15_0[0x10];


	u8         reserved_at_360[0x3];
	u8         log_max_rq[0x5];
	u8         reserved_at_368[0x3];
	u8         log_max_sq[0x5];
	u8         reserved_at_370[0x3];
	u8         log_max_tir[0x5];
	u8         reserved_at_378[0x3];
	u8         log_max_tis[0x5];

	u8         basic_cyclic_rcv_wqe[0x1];
	u8         reserved_at_381[0x2];
	u8         log_max_rmp[0x5];
	u8         reserved_at_388[0x3];
	u8         log_max_rqt[0x5];
	u8         reserved_at_390[0x3];
	u8         log_max_rqt_size[0x5];
	u8         reserved_at_398[0x3];
	u8         log_max_tis_per_sq[0x5];

	u8         ext_stride_num_range[0x1];
	u8         reserved_at_3a1[0x2];
	u8         log_max_stride_sz_rq[0x5];
	u8         reserved_at_3a8[0x3];
	u8         log_min_stride_sz_rq[0x5];
	u8         reserved_at_3b0[0x3];
	u8         log_max_stride_sz_sq[0x5];
	u8         reserved_at_3b8[0x3];
	u8         log_min_stride_sz_sq[0x5];

	u8         hairpin[0x1];
	u8         reserved_at_3c1[0x2];
	u8         log_max_hairpin_queues[0x5];
	u8         reserved_at_3c8[0x3];
	u8         log_max_hairpin_wq_data_sz[0x5];
	u8         reserved_at_3d0[0x3];
	u8         log_max_hairpin_num_packets[0x5];
	u8         reserved_at_3d8[0x3];
	u8         log_max_wq_sz[0x5];

	u8         nic_vport_change_event[0x1];
	u8         disable_local_lb_uc[0x1];
	u8         disable_local_lb_mc[0x1];
	u8         log_min_hairpin_wq_data_sz[0x5];
	u8         reserved_at_3e8[0x3];
	u8         log_max_vlan_list[0x5];
	u8         reserved_at_3f0[0x3];
	u8         log_max_current_mc_list[0x5];
	u8         reserved_at_3f8[0x3];
	u8         log_max_current_uc_list[0x5];

	u8         general_obj_types[0x40];

	u8         reserved_at_440[0x20];

	u8         reserved_at_460[0x3];
	u8         log_max_uctx[0x5];
	u8         reserved_at_468[0x2];
	u8         ipsec_offload[0x1];
	u8         log_max_umem[0x5];
	u8         max_num_eqs[0x10];

	u8         reserved_at_480[0x1];
	u8         tls_tx[0x1];
	u8         tls_rx[0x1];
	u8         log_max_l2_table[0x5];
	u8         reserved_at_488[0x8];
	u8         log_uar_page_sz[0x10];

	u8         reserved_at_4a0[0x20];
	u8         device_frequency_mhz[0x20];
	u8         device_frequency_khz[0x20];

	u8         reserved_at_500[0x20];
	u8	   num_of_uars_per_page[0x20];

	u8         flex_parser_protocols[0x20];

	u8         max_geneve_tlv_options[0x8];
	u8         reserved_at_568[0x3];
	u8         max_geneve_tlv_option_data_len[0x5];
	u8         reserved_at_570[0x10];

	u8         reserved_at_580[0x33];
	u8         log_max_dek[0x5];
	u8         reserved_at_5b8[0x4];
	u8         mini_cqe_resp_stride_index[0x1];
	u8         cqe_128_always[0x1];
	u8         cqe_compression_128[0x1];
	u8         cqe_compression[0x1];

	u8         cqe_compression_timeout[0x10];
	u8         cqe_compression_max_num[0x10];

	u8         reserved_at_5e0[0x10];
	u8         tag_matching[0x1];
	u8         rndv_offload_rc[0x1];
	u8         rndv_offload_dc[0x1];
	u8         log_tag_matching_list_sz[0x5];
	u8         reserved_at_5f8[0x3];
	u8         log_max_xrq[0x5];

	u8	   affiliate_nic_vport_criteria[0x8];
	u8	   native_port_num[0x8];
	u8	   num_vhca_ports[0x8];
	u8	   reserved_at_618[0x6];
	u8	   sw_owner_id[0x1];
	u8         reserved_at_61f[0x1];

	u8         max_num_of_monitor_counters[0x10];
	u8         num_ppcnt_monitor_counters[0x10];

	u8         reserved_at_640[0x10];
	u8         num_q_monitor_counters[0x10];

	u8         reserved_at_660[0x20];

	u8         sf[0x1];
	u8         sf_set_partition[0x1];
	u8         reserved_at_682[0x1];
	u8         log_max_sf[0x5];
	u8         reserved_at_688[0x8];
	u8         log_min_sf_size[0x8];
	u8         max_num_sf_partitions[0x8];

	u8         uctx_cap[0x20];

	u8         reserved_at_6c0[0x4];
	u8         flex_parser_id_geneve_tlv_option_0[0x4];
	u8         flex_parser_id_icmp_dw1[0x4];
	u8         flex_parser_id_icmp_dw0[0x4];
	u8         flex_parser_id_icmpv6_dw1[0x4];
	u8         flex_parser_id_icmpv6_dw0[0x4];
	u8         flex_parser_id_outer_first_mpls_over_gre[0x4];
	u8         flex_parser_id_outer_first_mpls_over_udp_label[0x4];

	u8	   reserved_at_6e0[0x10];
	u8	   sf_base_id[0x10];

	u8	   reserved_at_700[0x80];
	u8	   vhca_tunnel_commands[0x40];
	u8	   reserved_at_7c0[0x40];
};

enum mlx5_flow_destination_type {
	MLX5_FLOW_DESTINATION_TYPE_VPORT        = 0x0,
	MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE   = 0x1,
	MLX5_FLOW_DESTINATION_TYPE_TIR          = 0x2,

	MLX5_FLOW_DESTINATION_TYPE_PORT         = 0x99,
	MLX5_FLOW_DESTINATION_TYPE_COUNTER      = 0x100,
	MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE_NUM = 0x101,
};

enum mlx5_flow_table_miss_action {
	MLX5_FLOW_TABLE_MISS_ACTION_DEF,
	MLX5_FLOW_TABLE_MISS_ACTION_FWD,
	MLX5_FLOW_TABLE_MISS_ACTION_SWITCH_DOMAIN,
};

struct mlx5_ifc_dest_format_struct_bits {
	u8         destination_type[0x8];
	u8         destination_id[0x18];

	u8         destination_eswitch_owner_vhca_id_valid[0x1];
	u8         packet_reformat[0x1];
	u8         reserved_at_22[0xe];
	u8         destination_eswitch_owner_vhca_id[0x10];
};

struct mlx5_ifc_flow_counter_list_bits {
	u8         flow_counter_id[0x20];

	u8         reserved_at_20[0x20];
};

struct mlx5_ifc_extended_dest_format_bits {
	struct mlx5_ifc_dest_format_struct_bits destination_entry;

	u8         packet_reformat_id[0x20];

	u8         reserved_at_60[0x20];
};

union mlx5_ifc_dest_format_struct_flow_counter_list_auto_bits {
	struct mlx5_ifc_extended_dest_format_bits extended_dest_format;
	struct mlx5_ifc_flow_counter_list_bits flow_counter_list;
};

struct mlx5_ifc_fte_match_param_bits {
	struct mlx5_ifc_fte_match_set_lyr_2_4_bits outer_headers;

	struct mlx5_ifc_fte_match_set_misc_bits misc_parameters;

	struct mlx5_ifc_fte_match_set_lyr_2_4_bits inner_headers;

	struct mlx5_ifc_fte_match_set_misc2_bits misc_parameters_2;

	struct mlx5_ifc_fte_match_set_misc3_bits misc_parameters_3;

	u8         reserved_at_a00[0x600];
};

enum {
	MLX5_RX_HASH_FIELD_SELECT_SELECTED_FIELDS_SRC_IP     = 0x0,
	MLX5_RX_HASH_FIELD_SELECT_SELECTED_FIELDS_DST_IP     = 0x1,
	MLX5_RX_HASH_FIELD_SELECT_SELECTED_FIELDS_L4_SPORT   = 0x2,
	MLX5_RX_HASH_FIELD_SELECT_SELECTED_FIELDS_L4_DPORT   = 0x3,
	MLX5_RX_HASH_FIELD_SELECT_SELECTED_FIELDS_IPSEC_SPI  = 0x4,
};

struct mlx5_ifc_rx_hash_field_select_bits {
	u8         l3_prot_type[0x1];
	u8         l4_prot_type[0x1];
	u8         selected_fields[0x1e];
};

enum {
	MLX5_WQ_WQ_TYPE_WQ_LINKED_LIST  = 0x0,
	MLX5_WQ_WQ_TYPE_WQ_CYCLIC       = 0x1,
};

enum {
	MLX5_WQ_END_PADDING_MODE_END_PAD_NONE   = 0x0,
	MLX5_WQ_END_PADDING_MODE_END_PAD_ALIGN  = 0x1,
};

struct mlx5_ifc_wq_bits {
	u8         wq_type[0x4];
	u8         wq_signature[0x1];
	u8         end_padding_mode[0x2];
	u8         cd_slave[0x1];
	u8         reserved_at_8[0x18];

	u8         hds_skip_first_sge[0x1];
	u8         log2_hds_buf_size[0x3];
	u8         reserved_at_24[0x7];
	u8         page_offset[0x5];
	u8         lwm[0x10];

	u8         reserved_at_40[0x8];
	u8         pd[0x18];

	u8         reserved_at_60[0x8];
	u8         uar_page[0x18];

	u8         dbr_addr[0x40];

	u8         hw_counter[0x20];

	u8         sw_counter[0x20];

	u8         reserved_at_100[0xc];
	u8         log_wq_stride[0x4];
	u8         reserved_at_110[0x3];
	u8         log_wq_pg_sz[0x5];
	u8         reserved_at_118[0x3];
	u8         log_wq_sz[0x5];

	u8         dbr_umem_valid[0x1];
	u8         wq_umem_valid[0x1];
	u8         reserved_at_122[0x1];
	u8         log_hairpin_num_packets[0x5];
	u8         reserved_at_128[0x3];
	u8         log_hairpin_data_sz[0x5];

	u8         reserved_at_130[0x4];
	u8         log_wqe_num_of_strides[0x4];
	u8         two_byte_shift_en[0x1];
	u8         reserved_at_139[0x4];
	u8         log_wqe_stride_size[0x3];

	u8         reserved_at_140[0x4c0];

	struct mlx5_ifc_cmd_pas_bits pas[];
};

struct mlx5_ifc_rq_num_bits {
	u8         reserved_at_0[0x8];
	u8         rq_num[0x18];
};

struct mlx5_ifc_mac_address_layout_bits {
	u8         reserved_at_0[0x10];
	u8         mac_addr_47_32[0x10];

	u8         mac_addr_31_0[0x20];
};

struct mlx5_ifc_vlan_layout_bits {
	u8         reserved_at_0[0x14];
	u8         vlan[0x0c];

	u8         reserved_at_20[0x20];
};

struct mlx5_ifc_cong_control_r_roce_ecn_np_bits {
	u8         reserved_at_0[0xa0];

	u8         min_time_between_cnps[0x20];

	u8         reserved_at_c0[0x12];
	u8         cnp_dscp[0x6];
	u8         reserved_at_d8[0x4];
	u8         cnp_prio_mode[0x1];
	u8         cnp_802p_prio[0x3];

	u8         reserved_at_e0[0x720];
};

struct mlx5_ifc_cong_control_r_roce_ecn_rp_bits {
	u8         reserved_at_0[0x60];

	u8         reserved_at_60[0x4];
	u8         clamp_tgt_rate[0x1];
	u8         reserved_at_65[0x3];
	u8         clamp_tgt_rate_after_time_inc[0x1];
	u8         reserved_at_69[0x17];

	u8         reserved_at_80[0x20];

	u8         rpg_time_reset[0x20];

	u8         rpg_byte_reset[0x20];

	u8         rpg_threshold[0x20];

	u8         rpg_max_rate[0x20];

	u8         rpg_ai_rate[0x20];

	u8         rpg_hai_rate[0x20];

	u8         rpg_gd[0x20];

	u8         rpg_min_dec_fac[0x20];

	u8         rpg_min_rate[0x20];

	u8         reserved_at_1c0[0xe0];

	u8         rate_to_set_on_first_cnp[0x20];

	u8         dce_tcp_g[0x20];

	u8         dce_tcp_rtt[0x20];

	u8         rate_reduce_monitor_period[0x20];

	u8         reserved_at_320[0x20];

	u8         initial_alpha_value[0x20];

	u8         reserved_at_360[0x4a0];
};

struct mlx5_ifc_cong_control_802_1qau_rp_bits {
	u8         reserved_at_0[0x80];

	u8         rppp_max_rps[0x20];

	u8         rpg_time_reset[0x20];

	u8         rpg_byte_reset[0x20];

	u8         rpg_threshold[0x20];

	u8         rpg_max_rate[0x20];

	u8         rpg_ai_rate[0x20];

	u8         rpg_hai_rate[0x20];

	u8         rpg_gd[0x20];

	u8         rpg_min_dec_fac[0x20];

	u8         rpg_min_rate[0x20];

	u8         reserved_at_1c0[0x640];
};

enum {
	MLX5_RESIZE_FIELD_SELECT_RESIZE_FIELD_SELECT_LOG_CQ_SIZE    = 0x1,
	MLX5_RESIZE_FIELD_SELECT_RESIZE_FIELD_SELECT_PAGE_OFFSET    = 0x2,
	MLX5_RESIZE_FIELD_SELECT_RESIZE_FIELD_SELECT_LOG_PAGE_SIZE  = 0x4,
};

struct mlx5_ifc_resize_field_select_bits {
	u8         resize_field_select[0x20];
};

struct mlx5_ifc_resource_dump_bits {
	u8         more_dump[0x1];
	u8         inline_dump[0x1];
	u8         reserved_at_2[0xa];
	u8         seq_num[0x4];
	u8         segment_type[0x10];

	u8         reserved_at_20[0x10];
	u8         vhca_id[0x10];

	u8         index1[0x20];

	u8         index2[0x20];

	u8         num_of_obj1[0x10];
	u8         num_of_obj2[0x10];

	u8         reserved_at_a0[0x20];

	u8         device_opaque[0x40];

	u8         mkey[0x20];

	u8         size[0x20];

	u8         address[0x40];

	u8         inline_data[52][0x20];
};

struct mlx5_ifc_resource_dump_menu_record_bits {
	u8         reserved_at_0[0x4];
	u8         num_of_obj2_supports_active[0x1];
	u8         num_of_obj2_supports_all[0x1];
	u8         must_have_num_of_obj2[0x1];
	u8         support_num_of_obj2[0x1];
	u8         num_of_obj1_supports_active[0x1];
	u8         num_of_obj1_supports_all[0x1];
	u8         must_have_num_of_obj1[0x1];
	u8         support_num_of_obj1[0x1];
	u8         must_have_index2[0x1];
	u8         support_index2[0x1];
	u8         must_have_index1[0x1];
	u8         support_index1[0x1];
	u8         segment_type[0x10];

	u8         segment_name[4][0x20];

	u8         index1_name[4][0x20];

	u8         index2_name[4][0x20];
};

struct mlx5_ifc_resource_dump_segment_header_bits {
	u8         length_dw[0x10];
	u8         segment_type[0x10];
};

struct mlx5_ifc_resource_dump_command_segment_bits {
	struct mlx5_ifc_resource_dump_segment_header_bits segment_header;

	u8         segment_called[0x10];
	u8         vhca_id[0x10];

	u8         index1[0x20];

	u8         index2[0x20];

	u8         num_of_obj1[0x10];
	u8         num_of_obj2[0x10];
};

struct mlx5_ifc_resource_dump_error_segment_bits {
	struct mlx5_ifc_resource_dump_segment_header_bits segment_header;

	u8         reserved_at_20[0x10];
	u8         syndrome_id[0x10];

	u8         reserved_at_40[0x40];

	u8         error[8][0x20];
};

struct mlx5_ifc_resource_dump_info_segment_bits {
	struct mlx5_ifc_resource_dump_segment_header_bits segment_header;

	u8         reserved_at_20[0x18];
	u8         dump_version[0x8];

	u8         hw_version[0x20];

	u8         fw_version[0x20];
};

struct mlx5_ifc_resource_dump_menu_segment_bits {
	struct mlx5_ifc_resource_dump_segment_header_bits segment_header;

	u8         reserved_at_20[0x10];
	u8         num_of_records[0x10];

	struct mlx5_ifc_resource_dump_menu_record_bits record[];
};

struct mlx5_ifc_resource_dump_resource_segment_bits {
	struct mlx5_ifc_resource_dump_segment_header_bits segment_header;

	u8         reserved_at_20[0x20];

	u8         index1[0x20];

	u8         index2[0x20];

	u8         payload[][0x20];
};

struct mlx5_ifc_resource_dump_terminate_segment_bits {
	struct mlx5_ifc_resource_dump_segment_header_bits segment_header;
};

struct mlx5_ifc_menu_resource_dump_response_bits {
	struct mlx5_ifc_resource_dump_info_segment_bits info;
	struct mlx5_ifc_resource_dump_command_segment_bits cmd;
	struct mlx5_ifc_resource_dump_menu_segment_bits menu;
	struct mlx5_ifc_resource_dump_terminate_segment_bits terminate;
};

enum {
	MLX5_MODIFY_FIELD_SELECT_MODIFY_FIELD_SELECT_CQ_PERIOD     = 0x1,
	MLX5_MODIFY_FIELD_SELECT_MODIFY_FIELD_SELECT_CQ_MAX_COUNT  = 0x2,
	MLX5_MODIFY_FIELD_SELECT_MODIFY_FIELD_SELECT_OI            = 0x4,
	MLX5_MODIFY_FIELD_SELECT_MODIFY_FIELD_SELECT_C_EQN         = 0x8,
};

struct mlx5_ifc_modify_field_select_bits {
	u8         modify_field_select[0x20];
};

struct mlx5_ifc_field_select_r_roce_np_bits {
	u8         field_select_r_roce_np[0x20];
};

struct mlx5_ifc_field_select_r_roce_rp_bits {
	u8         field_select_r_roce_rp[0x20];
};

enum {
	MLX5_FIELD_SELECT_802_1QAU_RP_FIELD_SELECT_8021QAURP_RPPP_MAX_RPS     = 0x4,
	MLX5_FIELD_SELECT_802_1QAU_RP_FIELD_SELECT_8021QAURP_RPG_TIME_RESET   = 0x8,
	MLX5_FIELD_SELECT_802_1QAU_RP_FIELD_SELECT_8021QAURP_RPG_BYTE_RESET   = 0x10,
	MLX5_FIELD_SELECT_802_1QAU_RP_FIELD_SELECT_8021QAURP_RPG_THRESHOLD    = 0x20,
	MLX5_FIELD_SELECT_802_1QAU_RP_FIELD_SELECT_8021QAURP_RPG_MAX_RATE     = 0x40,
	MLX5_FIELD_SELECT_802_1QAU_RP_FIELD_SELECT_8021QAURP_RPG_AI_RATE      = 0x80,
	MLX5_FIELD_SELECT_802_1QAU_RP_FIELD_SELECT_8021QAURP_RPG_HAI_RATE     = 0x100,
	MLX5_FIELD_SELECT_802_1QAU_RP_FIELD_SELECT_8021QAURP_RPG_GD           = 0x200,
	MLX5_FIELD_SELECT_802_1QAU_RP_FIELD_SELECT_8021QAURP_RPG_MIN_DEC_FAC  = 0x400,
	MLX5_FIELD_SELECT_802_1QAU_RP_FIELD_SELECT_8021QAURP_RPG_MIN_RATE     = 0x800,
};

struct mlx5_ifc_field_select_802_1qau_rp_bits {
	u8         field_select_8021qaurp[0x20];
};

struct mlx5_ifc_phys_layer_cntrs_bits {
	u8         time_since_last_clear_high[0x20];

	u8         time_since_last_clear_low[0x20];

	u8         symbol_errors_high[0x20];

	u8         symbol_errors_low[0x20];

	u8         sync_headers_errors_high[0x20];

	u8         sync_headers_errors_low[0x20];

	u8         edpl_bip_errors_lane0_high[0x20];

	u8         edpl_bip_errors_lane0_low[0x20];

	u8         edpl_bip_errors_lane1_high[0x20];

	u8         edpl_bip_errors_lane1_low[0x20];

	u8         edpl_bip_errors_lane2_high[0x20];

	u8         edpl_bip_errors_lane2_low[0x20];

	u8         edpl_bip_errors_lane3_high[0x20];

	u8         edpl_bip_errors_lane3_low[0x20];

	u8         fc_fec_corrected_blocks_lane0_high[0x20];

	u8         fc_fec_corrected_blocks_lane0_low[0x20];

	u8         fc_fec_corrected_blocks_lane1_high[0x20];

	u8         fc_fec_corrected_blocks_lane1_low[0x20];

	u8         fc_fec_corrected_blocks_lane2_high[0x20];

	u8         fc_fec_corrected_blocks_lane2_low[0x20];

	u8         fc_fec_corrected_blocks_lane3_high[0x20];

	u8         fc_fec_corrected_blocks_lane3_low[0x20];

	u8         fc_fec_uncorrectable_blocks_lane0_high[0x20];

	u8         fc_fec_uncorrectable_blocks_lane0_low[0x20];

	u8         fc_fec_uncorrectable_blocks_lane1_high[0x20];

	u8         fc_fec_uncorrectable_blocks_lane1_low[0x20];

	u8         fc_fec_uncorrectable_blocks_lane2_high[0x20];

	u8         fc_fec_uncorrectable_blocks_lane2_low[0x20];

	u8         fc_fec_uncorrectable_blocks_lane3_high[0x20];

	u8         fc_fec_uncorrectable_blocks_lane3_low[0x20];

	u8         rs_fec_corrected_blocks_high[0x20];

	u8         rs_fec_corrected_blocks_low[0x20];

	u8         rs_fec_uncorrectable_blocks_high[0x20];

	u8         rs_fec_uncorrectable_blocks_low[0x20];

	u8         rs_fec_no_errors_blocks_high[0x20];

	u8         rs_fec_no_errors_blocks_low[0x20];

	u8         rs_fec_single_error_blocks_high[0x20];

	u8         rs_fec_single_error_blocks_low[0x20];

	u8         rs_fec_corrected_symbols_total_high[0x20];

	u8         rs_fec_corrected_symbols_total_low[0x20];

	u8         rs_fec_corrected_symbols_lane0_high[0x20];

	u8         rs_fec_corrected_symbols_lane0_low[0x20];

	u8         rs_fec_corrected_symbols_lane1_high[0x20];

	u8         rs_fec_corrected_symbols_lane1_low[0x20];

	u8         rs_fec_corrected_symbols_lane2_high[0x20];

	u8         rs_fec_corrected_symbols_lane2_low[0x20];

	u8         rs_fec_corrected_symbols_lane3_high[0x20];

	u8         rs_fec_corrected_symbols_lane3_low[0x20];

	u8         link_down_events[0x20];

	u8         successful_recovery_events[0x20];

	u8         reserved_at_640[0x180];
};

struct mlx5_ifc_phys_layer_statistical_cntrs_bits {
	u8         time_since_last_clear_high[0x20];

	u8         time_since_last_clear_low[0x20];

	u8         phy_received_bits_high[0x20];

	u8         phy_received_bits_low[0x20];

	u8         phy_symbol_errors_high[0x20];

	u8         phy_symbol_errors_low[0x20];

	u8         phy_corrected_bits_high[0x20];

	u8         phy_corrected_bits_low[0x20];

	u8         phy_corrected_bits_lane0_high[0x20];

	u8         phy_corrected_bits_lane0_low[0x20];

	u8         phy_corrected_bits_lane1_high[0x20];

	u8         phy_corrected_bits_lane1_low[0x20];

	u8         phy_corrected_bits_lane2_high[0x20];

	u8         phy_corrected_bits_lane2_low[0x20];

	u8         phy_corrected_bits_lane3_high[0x20];

	u8         phy_corrected_bits_lane3_low[0x20];

	u8         reserved_at_200[0x5c0];
};

struct mlx5_ifc_ib_port_cntrs_grp_data_layout_bits {
	u8	   symbol_error_counter[0x10];

	u8         link_error_recovery_counter[0x8];

	u8         link_downed_counter[0x8];

	u8         port_rcv_errors[0x10];

	u8         port_rcv_remote_physical_errors[0x10];

	u8         port_rcv_switch_relay_errors[0x10];

	u8         port_xmit_discards[0x10];

	u8         port_xmit_constraint_errors[0x8];

	u8         port_rcv_constraint_errors[0x8];

	u8         reserved_at_70[0x8];

	u8         link_overrun_errors[0x8];

	u8	   reserved_at_80[0x10];

	u8         vl_15_dropped[0x10];

	u8	   reserved_at_a0[0x80];

	u8         port_xmit_wait[0x20];
};

struct mlx5_ifc_eth_per_tc_prio_grp_data_layout_bits {
	u8         transmit_queue_high[0x20];

	u8         transmit_queue_low[0x20];

	u8         no_buffer_discard_uc_high[0x20];

	u8         no_buffer_discard_uc_low[0x20];

	u8         reserved_at_80[0x740];
};

struct mlx5_ifc_eth_per_tc_congest_prio_grp_data_layout_bits {
	u8         wred_discard_high[0x20];

	u8         wred_discard_low[0x20];

	u8         ecn_marked_tc_high[0x20];

	u8         ecn_marked_tc_low[0x20];

	u8         reserved_at_80[0x740];
};

struct mlx5_ifc_eth_per_prio_grp_data_layout_bits {
	u8         rx_octets_high[0x20];

	u8         rx_octets_low[0x20];

	u8         reserved_at_40[0xc0];

	u8         rx_frames_high[0x20];

	u8         rx_frames_low[0x20];

	u8         tx_octets_high[0x20];

	u8         tx_octets_low[0x20];

	u8         reserved_at_180[0xc0];

	u8         tx_frames_high[0x20];

	u8         tx_frames_low[0x20];

	u8         rx_pause_high[0x20];

	u8         rx_pause_low[0x20];

	u8         rx_pause_duration_high[0x20];

	u8         rx_pause_duration_low[0x20];

	u8         tx_pause_high[0x20];

	u8         tx_pause_low[0x20];

	u8         tx_pause_duration_high[0x20];

	u8         tx_pause_duration_low[0x20];

	u8         rx_pause_transition_high[0x20];

	u8         rx_pause_transition_low[0x20];

	u8         rx_discards_high[0x20];

	u8         rx_discards_low[0x20];

	u8         device_stall_minor_watermark_cnt_high[0x20];

	u8         device_stall_minor_watermark_cnt_low[0x20];

	u8         device_stall_critical_watermark_cnt_high[0x20];

	u8         device_stall_critical_watermark_cnt_low[0x20];

	u8         reserved_at_480[0x340];
};

struct mlx5_ifc_eth_extended_cntrs_grp_data_layout_bits {
	u8         port_transmit_wait_high[0x20];

	u8         port_transmit_wait_low[0x20];

	u8         reserved_at_40[0x100];

	u8         rx_buffer_almost_full_high[0x20];

	u8         rx_buffer_almost_full_low[0x20];

	u8         rx_buffer_full_high[0x20];

	u8         rx_buffer_full_low[0x20];

	u8         rx_icrc_encapsulated_high[0x20];

	u8         rx_icrc_encapsulated_low[0x20];

	u8         reserved_at_200[0x5c0];
};

struct mlx5_ifc_eth_3635_cntrs_grp_data_layout_bits {
	u8         dot3stats_alignment_errors_high[0x20];

	u8         dot3stats_alignment_errors_low[0x20];

	u8         dot3stats_fcs_errors_high[0x20];

	u8         dot3stats_fcs_errors_low[0x20];

	u8         dot3stats_single_collision_frames_high[0x20];

	u8         dot3stats_single_collision_frames_low[0x20];

	u8         dot3stats_multiple_collision_frames_high[0x20];

	u8         dot3stats_multiple_collision_frames_low[0x20];

	u8         dot3stats_sqe_test_errors_high[0x20];

	u8         dot3stats_sqe_test_errors_low[0x20];

	u8         dot3stats_deferred_transmissions_high[0x20];

	u8         dot3stats_deferred_transmissions_low[0x20];

	u8         dot3stats_late_collisions_high[0x20];

	u8         dot3stats_late_collisions_low[0x20];

	u8         dot3stats_excessive_collisions_high[0x20];

	u8         dot3stats_excessive_collisions_low[0x20];

	u8         dot3stats_internal_mac_transmit_errors_high[0x20];

	u8         dot3stats_internal_mac_transmit_errors_low[0x20];

	u8         dot3stats_carrier_sense_errors_high[0x20];

	u8         dot3stats_carrier_sense_errors_low[0x20];

	u8         dot3stats_frame_too_longs_high[0x20];

	u8         dot3stats_frame_too_longs_low[0x20];

	u8         dot3stats_internal_mac_receive_errors_high[0x20];

	u8         dot3stats_internal_mac_receive_errors_low[0x20];

	u8         dot3stats_symbol_errors_high[0x20];

	u8         dot3stats_symbol_errors_low[0x20];

	u8         dot3control_in_unknown_opcodes_high[0x20];

	u8         dot3control_in_unknown_opcodes_low[0x20];

	u8         dot3in_pause_frames_high[0x20];

	u8         dot3in_pause_frames_low[0x20];

	u8         dot3out_pause_frames_high[0x20];

	u8         dot3out_pause_frames_low[0x20];

	u8         reserved_at_400[0x3c0];
};

struct mlx5_ifc_eth_2819_cntrs_grp_data_layout_bits {
	u8         ether_stats_drop_events_high[0x20];

	u8         ether_stats_drop_events_low[0x20];

	u8         ether_stats_octets_high[0x20];

	u8         ether_stats_octets_low[0x20];

	u8         ether_stats_pkts_high[0x20];

	u8         ether_stats_pkts_low[0x20];

	u8         ether_stats_broadcast_pkts_high[0x20];

	u8         ether_stats_broadcast_pkts_low[0x20];

	u8         ether_stats_multicast_pkts_high[0x20];

	u8         ether_stats_multicast_pkts_low[0x20];

	u8         ether_stats_crc_align_errors_high[0x20];

	u8         ether_stats_crc_align_errors_low[0x20];

	u8         ether_stats_undersize_pkts_high[0x20];

	u8         ether_stats_undersize_pkts_low[0x20];

	u8         ether_stats_oversize_pkts_high[0x20];

	u8         ether_stats_oversize_pkts_low[0x20];

	u8         ether_stats_fragments_high[0x20];

	u8         ether_stats_fragments_low[0x20];

	u8         ether_stats_jabbers_high[0x20];

	u8         ether_stats_jabbers_low[0x20];

	u8         ether_stats_collisions_high[0x20];

	u8         ether_stats_collisions_low[0x20];

	u8         ether_stats_pkts64octets_high[0x20];

	u8         ether_stats_pkts64octets_low[0x20];

	u8         ether_stats_pkts65to127octets_high[0x20];

	u8         ether_stats_pkts65to127octets_low[0x20];

	u8         ether_stats_pkts128to255octets_high[0x20];

	u8         ether_stats_pkts128to255octets_low[0x20];

	u8         ether_stats_pkts256to511octets_high[0x20];

	u8         ether_stats_pkts256to511octets_low[0x20];

	u8         ether_stats_pkts512to1023octets_high[0x20];

	u8         ether_stats_pkts512to1023octets_low[0x20];

	u8         ether_stats_pkts1024to1518octets_high[0x20];

	u8         ether_stats_pkts1024to1518octets_low[0x20];

	u8         ether_stats_pkts1519to2047octets_high[0x20];

	u8         ether_stats_pkts1519to2047octets_low[0x20];

	u8         ether_stats_pkts2048to4095octets_high[0x20];

	u8         ether_stats_pkts2048to4095octets_low[0x20];

	u8         ether_stats_pkts4096to8191octets_high[0x20];

	u8         ether_stats_pkts4096to8191octets_low[0x20];

	u8         ether_stats_pkts8192to10239octets_high[0x20];

	u8         ether_stats_pkts8192to10239octets_low[0x20];

	u8         reserved_at_540[0x280];
};

struct mlx5_ifc_eth_2863_cntrs_grp_data_layout_bits {
	u8         if_in_octets_high[0x20];

	u8         if_in_octets_low[0x20];

	u8         if_in_ucast_pkts_high[0x20];

	u8         if_in_ucast_pkts_low[0x20];

	u8         if_in_discards_high[0x20];

	u8         if_in_discards_low[0x20];

	u8         if_in_errors_high[0x20];

	u8         if_in_errors_low[0x20];

	u8         if_in_unknown_protos_high[0x20];

	u8         if_in_unknown_protos_low[0x20];

	u8         if_out_octets_high[0x20];

	u8         if_out_octets_low[0x20];

	u8         if_out_ucast_pkts_high[0x20];

	u8         if_out_ucast_pkts_low[0x20];

	u8         if_out_discards_high[0x20];

	u8         if_out_discards_low[0x20];

	u8         if_out_errors_high[0x20];

	u8         if_out_errors_low[0x20];

	u8         if_in_multicast_pkts_high[0x20];

	u8         if_in_multicast_pkts_low[0x20];

	u8         if_in_broadcast_pkts_high[0x20];

	u8         if_in_broadcast_pkts_low[0x20];

	u8         if_out_multicast_pkts_high[0x20];

	u8         if_out_multicast_pkts_low[0x20];

	u8         if_out_broadcast_pkts_high[0x20];

	u8         if_out_broadcast_pkts_low[0x20];

	u8         reserved_at_340[0x480];
};

struct mlx5_ifc_eth_802_3_cntrs_grp_data_layout_bits {
	u8         a_frames_transmitted_ok_high[0x20];

	u8         a_frames_transmitted_ok_low[0x20];

	u8         a_frames_received_ok_high[0x20];

	u8         a_frames_received_ok_low[0x20];

	u8         a_frame_check_sequence_errors_high[0x20];

	u8         a_frame_check_sequence_errors_low[0x20];

	u8         a_alignment_errors_high[0x20];

	u8         a_alignment_errors_low[0x20];

	u8         a_octets_transmitted_ok_high[0x20];

	u8         a_octets_transmitted_ok_low[0x20];

	u8         a_octets_received_ok_high[0x20];

	u8         a_octets_received_ok_low[0x20];

	u8         a_multicast_frames_xmitted_ok_high[0x20];

	u8         a_multicast_frames_xmitted_ok_low[0x20];

	u8         a_broadcast_frames_xmitted_ok_high[0x20];

	u8         a_broadcast_frames_xmitted_ok_low[0x20];

	u8         a_multicast_frames_received_ok_high[0x20];

	u8         a_multicast_frames_received_ok_low[0x20];

	u8         a_broadcast_frames_received_ok_high[0x20];

	u8         a_broadcast_frames_received_ok_low[0x20];

	u8         a_in_range_length_errors_high[0x20];

	u8         a_in_range_length_errors_low[0x20];

	u8         a_out_of_range_length_field_high[0x20];

	u8         a_out_of_range_length_field_low[0x20];

	u8         a_frame_too_long_errors_high[0x20];

	u8         a_frame_too_long_errors_low[0x20];

	u8         a_symbol_error_during_carrier_high[0x20];

	u8         a_symbol_error_during_carrier_low[0x20];

	u8         a_mac_control_frames_transmitted_high[0x20];

	u8         a_mac_control_frames_transmitted_low[0x20];

	u8         a_mac_control_frames_received_high[0x20];

	u8         a_mac_control_frames_received_low[0x20];

	u8         a_unsupported_opcodes_received_high[0x20];

	u8         a_unsupported_opcodes_received_low[0x20];

	u8         a_pause_mac_ctrl_frames_received_high[0x20];

	u8         a_pause_mac_ctrl_frames_received_low[0x20];

	u8         a_pause_mac_ctrl_frames_transmitted_high[0x20];

	u8         a_pause_mac_ctrl_frames_transmitted_low[0x20];

	u8         reserved_at_4c0[0x300];
};

struct mlx5_ifc_pcie_perf_cntrs_grp_data_layout_bits {
	u8         life_time_counter_high[0x20];

	u8         life_time_counter_low[0x20];

	u8         rx_errors[0x20];

	u8         tx_errors[0x20];

	u8         l0_to_recovery_eieos[0x20];

	u8         l0_to_recovery_ts[0x20];

	u8         l0_to_recovery_framing[0x20];

	u8         l0_to_recovery_retrain[0x20];

	u8         crc_error_dllp[0x20];

	u8         crc_error_tlp[0x20];

	u8         tx_overflow_buffer_pkt_high[0x20];

	u8         tx_overflow_buffer_pkt_low[0x20];

	u8         outbound_stalled_reads[0x20];

	u8         outbound_stalled_writes[0x20];

	u8         outbound_stalled_reads_events[0x20];

	u8         outbound_stalled_writes_events[0x20];

	u8         reserved_at_200[0x5c0];
};

struct mlx5_ifc_cmd_inter_comp_event_bits {
	u8         command_completion_vector[0x20];

	u8         reserved_at_20[0xc0];
};

struct mlx5_ifc_stall_vl_event_bits {
	u8         reserved_at_0[0x18];
	u8         port_num[0x1];
	u8         reserved_at_19[0x3];
	u8         vl[0x4];

	u8         reserved_at_20[0xa0];
};

struct mlx5_ifc_db_bf_congestion_event_bits {
	u8         event_subtype[0x8];
	u8         reserved_at_8[0x8];
	u8         congestion_level[0x8];
	u8         reserved_at_18[0x8];

	u8         reserved_at_20[0xa0];
};

struct mlx5_ifc_gpio_event_bits {
	u8         reserved_at_0[0x60];

	u8         gpio_event_hi[0x20];

	u8         gpio_event_lo[0x20];

	u8         reserved_at_a0[0x40];
};

struct mlx5_ifc_port_state_change_event_bits {
	u8         reserved_at_0[0x40];

	u8         port_num[0x4];
	u8         reserved_at_44[0x1c];

	u8         reserved_at_60[0x80];
};

struct mlx5_ifc_dropped_packet_logged_bits {
	u8         reserved_at_0[0xe0];
};

enum {
	MLX5_CQ_ERROR_SYNDROME_CQ_OVERRUN                 = 0x1,
	MLX5_CQ_ERROR_SYNDROME_CQ_ACCESS_VIOLATION_ERROR  = 0x2,
};

struct mlx5_ifc_cq_error_bits {
	u8         reserved_at_0[0x8];
	u8         cqn[0x18];

	u8         reserved_at_20[0x20];

	u8         reserved_at_40[0x18];
	u8         syndrome[0x8];

	u8         reserved_at_60[0x80];
};

struct mlx5_ifc_rdma_page_fault_event_bits {
	u8         bytes_committed[0x20];

	u8         r_key[0x20];

	u8         reserved_at_40[0x10];
	u8         packet_len[0x10];

	u8         rdma_op_len[0x20];

	u8         rdma_va[0x40];

	u8         reserved_at_c0[0x5];
	u8         rdma[0x1];
	u8         write[0x1];
	u8         requestor[0x1];
	u8         qp_number[0x18];
};

struct mlx5_ifc_wqe_associated_page_fault_event_bits {
	u8         bytes_committed[0x20];

	u8         reserved_at_20[0x10];
	u8         wqe_index[0x10];

	u8         reserved_at_40[0x10];
	u8         len[0x10];

	u8         reserved_at_60[0x60];

	u8         reserved_at_c0[0x5];
	u8         rdma[0x1];
	u8         write_read[0x1];
	u8         requestor[0x1];
	u8         qpn[0x18];
};

struct mlx5_ifc_qp_events_bits {
	u8         reserved_at_0[0xa0];

	u8         type[0x8];
	u8         reserved_at_a8[0x18];

	u8         reserved_at_c0[0x8];
	u8         qpn_rqn_sqn[0x18];
};

struct mlx5_ifc_dct_events_bits {
	u8         reserved_at_0[0xc0];

	u8         reserved_at_c0[0x8];
	u8         dct_number[0x18];
};

struct mlx5_ifc_comp_event_bits {
	u8         reserved_at_0[0xc0];

	u8         reserved_at_c0[0x8];
	u8         cq_number[0x18];
};

enum {
	MLX5_QPC_STATE_RST        = 0x0,
	MLX5_QPC_STATE_INIT       = 0x1,
	MLX5_QPC_STATE_RTR        = 0x2,
	MLX5_QPC_STATE_RTS        = 0x3,
	MLX5_QPC_STATE_SQER       = 0x4,
	MLX5_QPC_STATE_ERR        = 0x6,
	MLX5_QPC_STATE_SQD        = 0x7,
	MLX5_QPC_STATE_SUSPENDED  = 0x9,
};

enum {
	MLX5_QPC_ST_RC            = 0x0,
	MLX5_QPC_ST_UC            = 0x1,
	MLX5_QPC_ST_UD            = 0x2,
	MLX5_QPC_ST_XRC           = 0x3,
	MLX5_QPC_ST_DCI           = 0x5,
	MLX5_QPC_ST_QP0           = 0x7,
	MLX5_QPC_ST_QP1           = 0x8,
	MLX5_QPC_ST_RAW_DATAGRAM  = 0x9,
	MLX5_QPC_ST_REG_UMR       = 0xc,
};

enum {
	MLX5_QPC_PM_STATE_ARMED     = 0x0,
	MLX5_QPC_PM_STATE_REARM     = 0x1,
	MLX5_QPC_PM_STATE_RESERVED  = 0x2,
	MLX5_QPC_PM_STATE_MIGRATED  = 0x3,
};

enum {
	MLX5_QPC_OFFLOAD_TYPE_RNDV  = 0x1,
};

enum {
	MLX5_QPC_END_PADDING_MODE_SCATTER_AS_IS                = 0x0,
	MLX5_QPC_END_PADDING_MODE_PAD_TO_CACHE_LINE_ALIGNMENT  = 0x1,
};

enum {
	MLX5_QPC_MTU_256_BYTES        = 0x1,
	MLX5_QPC_MTU_512_BYTES        = 0x2,
	MLX5_QPC_MTU_1K_BYTES         = 0x3,
	MLX5_QPC_MTU_2K_BYTES         = 0x4,
	MLX5_QPC_MTU_4K_BYTES         = 0x5,
	MLX5_QPC_MTU_RAW_ETHERNET_QP  = 0x7,
};

enum {
	MLX5_QPC_ATOMIC_MODE_IB_SPEC     = 0x1,
	MLX5_QPC_ATOMIC_MODE_ONLY_8B     = 0x2,
	MLX5_QPC_ATOMIC_MODE_UP_TO_8B    = 0x3,
	MLX5_QPC_ATOMIC_MODE_UP_TO_16B   = 0x4,
	MLX5_QPC_ATOMIC_MODE_UP_TO_32B   = 0x5,
	MLX5_QPC_ATOMIC_MODE_UP_TO_64B   = 0x6,
	MLX5_QPC_ATOMIC_MODE_UP_TO_128B  = 0x7,
	MLX5_QPC_ATOMIC_MODE_UP_TO_256B  = 0x8,
};

enum {
	MLX5_QPC_CS_REQ_DISABLE    = 0x0,
	MLX5_QPC_CS_REQ_UP_TO_32B  = 0x11,
	MLX5_QPC_CS_REQ_UP_TO_64B  = 0x22,
};

enum {
	MLX5_QPC_CS_RES_DISABLE    = 0x0,
	MLX5_QPC_CS_RES_UP_TO_32B  = 0x1,
	MLX5_QPC_CS_RES_UP_TO_64B  = 0x2,
};

struct mlx5_ifc_qpc_bits {
	u8         state[0x4];
	u8         lag_tx_port_affinity[0x4];
	u8         st[0x8];
	u8         reserved_at_10[0x3];
	u8         pm_state[0x2];
	u8         reserved_at_15[0x1];
	u8         req_e2e_credit_mode[0x2];
	u8         offload_type[0x4];
	u8         end_padding_mode[0x2];
	u8         reserved_at_1e[0x2];

	u8         wq_signature[0x1];
	u8         block_lb_mc[0x1];
	u8         atomic_like_write_en[0x1];
	u8         latency_sensitive[0x1];
	u8         reserved_at_24[0x1];
	u8         drain_sigerr[0x1];
	u8         reserved_at_26[0x2];
	u8         pd[0x18];

	u8         mtu[0x3];
	u8         log_msg_max[0x5];
	u8         reserved_at_48[0x1];
	u8         log_rq_size[0x4];
	u8         log_rq_stride[0x3];
	u8         no_sq[0x1];
	u8         log_sq_size[0x4];
	u8         reserved_at_55[0x6];
	u8         rlky[0x1];
	u8         ulp_stateless_offload_mode[0x4];

	u8         counter_set_id[0x8];
	u8         uar_page[0x18];

	u8         reserved_at_80[0x8];
	u8         user_index[0x18];

	u8         reserved_at_a0[0x3];
	u8         log_page_size[0x5];
	u8         remote_qpn[0x18];

	struct mlx5_ifc_ads_bits primary_address_path;

	struct mlx5_ifc_ads_bits secondary_address_path;

	u8         log_ack_req_freq[0x4];
	u8         reserved_at_384[0x4];
	u8         log_sra_max[0x3];
	u8         reserved_at_38b[0x2];
	u8         retry_count[0x3];
	u8         rnr_retry[0x3];
	u8         reserved_at_393[0x1];
	u8         fre[0x1];
	u8         cur_rnr_retry[0x3];
	u8         cur_retry_count[0x3];
	u8         reserved_at_39b[0x5];

	u8         reserved_at_3a0[0x20];

	u8         reserved_at_3c0[0x8];
	u8         next_send_psn[0x18];

	u8         reserved_at_3e0[0x8];
	u8         cqn_snd[0x18];

	u8         reserved_at_400[0x8];
	u8         deth_sqpn[0x18];

	u8         reserved_at_420[0x20];

	u8         reserved_at_440[0x8];
	u8         last_acked_psn[0x18];

	u8         reserved_at_460[0x8];
	u8         ssn[0x18];

	u8         reserved_at_480[0x8];
	u8         log_rra_max[0x3];
	u8         reserved_at_48b[0x1];
	u8         atomic_mode[0x4];
	u8         rre[0x1];
	u8         rwe[0x1];
	u8         rae[0x1];
	u8         reserved_at_493[0x1];
	u8         page_offset[0x6];
	u8         reserved_at_49a[0x3];
	u8         cd_slave_receive[0x1];
	u8         cd_slave_send[0x1];
	u8         cd_master[0x1];

	u8         reserved_at_4a0[0x3];
	u8         min_rnr_nak[0x5];
	u8         next_rcv_psn[0x18];

	u8         reserved_at_4c0[0x8];
	u8         xrcd[0x18];

	u8         reserved_at_4e0[0x8];
	u8         cqn_rcv[0x18];

	u8         dbr_addr[0x40];

	u8         q_key[0x20];

	u8         reserved_at_560[0x5];
	u8         rq_type[0x3];
	u8         srqn_rmpn_xrqn[0x18];

	u8         reserved_at_580[0x8];
	u8         rmsn[0x18];

	u8         hw_sq_wqebb_counter[0x10];
	u8         sw_sq_wqebb_counter[0x10];

	u8         hw_rq_counter[0x20];

	u8         sw_rq_counter[0x20];

	u8         reserved_at_600[0x20];

	u8         reserved_at_620[0xf];
	u8         cgs[0x1];
	u8         cs_req[0x8];
	u8         cs_res[0x8];

	u8         dc_access_key[0x40];

	u8         reserved_at_680[0x3];
	u8         dbr_umem_valid[0x1];

	u8         reserved_at_684[0xbc];
};

struct mlx5_ifc_roce_addr_layout_bits {
	u8         source_l3_address[16][0x8];

	u8         reserved_at_80[0x3];
	u8         vlan_valid[0x1];
	u8         vlan_id[0xc];
	u8         source_mac_47_32[0x10];

	u8         source_mac_31_0[0x20];

	u8         reserved_at_c0[0x14];
	u8         roce_l3_type[0x4];
	u8         roce_version[0x8];

	u8         reserved_at_e0[0x20];
};

union mlx5_ifc_hca_cap_union_bits {
	struct mlx5_ifc_cmd_hca_cap_bits cmd_hca_cap;
	struct mlx5_ifc_odp_cap_bits odp_cap;
	struct mlx5_ifc_atomic_caps_bits atomic_caps;
	struct mlx5_ifc_roce_cap_bits roce_cap;
	struct mlx5_ifc_per_protocol_networking_offload_caps_bits per_protocol_networking_offload_caps;
	struct mlx5_ifc_flow_table_nic_cap_bits flow_table_nic_cap;
	struct mlx5_ifc_flow_table_eswitch_cap_bits flow_table_eswitch_cap;
	struct mlx5_ifc_e_switch_cap_bits e_switch_cap;
	struct mlx5_ifc_vector_calc_cap_bits vector_calc_cap;
	struct mlx5_ifc_qos_cap_bits qos_cap;
	struct mlx5_ifc_debug_cap_bits debug_cap;
	struct mlx5_ifc_fpga_cap_bits fpga_cap;
	struct mlx5_ifc_tls_cap_bits tls_cap;
	struct mlx5_ifc_device_mem_cap_bits device_mem_cap;
	struct mlx5_ifc_device_virtio_emulation_cap_bits virtio_emulation_cap;
	u8         reserved_at_0[0x8000];
};

enum {
	MLX5_FLOW_CONTEXT_ACTION_ALLOW     = 0x1,
	MLX5_FLOW_CONTEXT_ACTION_DROP      = 0x2,
	MLX5_FLOW_CONTEXT_ACTION_FWD_DEST  = 0x4,
	MLX5_FLOW_CONTEXT_ACTION_COUNT     = 0x8,
	MLX5_FLOW_CONTEXT_ACTION_PACKET_REFORMAT = 0x10,
	MLX5_FLOW_CONTEXT_ACTION_DECAP     = 0x20,
	MLX5_FLOW_CONTEXT_ACTION_MOD_HDR   = 0x40,
	MLX5_FLOW_CONTEXT_ACTION_VLAN_POP  = 0x80,
	MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH = 0x100,
	MLX5_FLOW_CONTEXT_ACTION_VLAN_POP_2  = 0x400,
	MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH_2 = 0x800,
};

enum {
	MLX5_FLOW_CONTEXT_FLOW_SOURCE_ANY_VPORT         = 0x0,
	MLX5_FLOW_CONTEXT_FLOW_SOURCE_UPLINK            = 0x1,
	MLX5_FLOW_CONTEXT_FLOW_SOURCE_LOCAL_VPORT       = 0x2,
};

struct mlx5_ifc_vlan_bits {
	u8         ethtype[0x10];
	u8         prio[0x3];
	u8         cfi[0x1];
	u8         vid[0xc];
};

struct mlx5_ifc_flow_context_bits {
	struct mlx5_ifc_vlan_bits push_vlan;

	u8         group_id[0x20];

	u8         reserved_at_40[0x8];
	u8         flow_tag[0x18];

	u8         reserved_at_60[0x10];
	u8         action[0x10];

	u8         extended_destination[0x1];
	u8         reserved_at_81[0x1];
	u8         flow_source[0x2];
	u8         reserved_at_84[0x4];
	u8         destination_list_size[0x18];

	u8         reserved_at_a0[0x8];
	u8         flow_counter_list_size[0x18];

	u8         packet_reformat_id[0x20];

	u8         modify_header_id[0x20];

	struct mlx5_ifc_vlan_bits push_vlan_2;

	u8         reserved_at_120[0xe0];

	struct mlx5_ifc_fte_match_param_bits match_value;

	u8         reserved_at_1200[0x600];

	union mlx5_ifc_dest_format_struct_flow_counter_list_auto_bits destination[];
};

enum {
	MLX5_XRC_SRQC_STATE_GOOD   = 0x0,
	MLX5_XRC_SRQC_STATE_ERROR  = 0x1,
};

struct mlx5_ifc_xrc_srqc_bits {
	u8         state[0x4];
	u8         log_xrc_srq_size[0x4];
	u8         reserved_at_8[0x18];

	u8         wq_signature[0x1];
	u8         cont_srq[0x1];
	u8         reserved_at_22[0x1];
	u8         rlky[0x1];
	u8         basic_cyclic_rcv_wqe[0x1];
	u8         log_rq_stride[0x3];
	u8         xrcd[0x18];

	u8         page_offset[0x6];
	u8         reserved_at_46[0x1];
	u8         dbr_umem_valid[0x1];
	u8         cqn[0x18];

	u8         reserved_at_60[0x20];

	u8         user_index_equal_xrc_srqn[0x1];
	u8         reserved_at_81[0x1];
	u8         log_page_size[0x6];
	u8         user_index[0x18];

	u8         reserved_at_a0[0x20];

	u8         reserved_at_c0[0x8];
	u8         pd[0x18];

	u8         lwm[0x10];
	u8         wqe_cnt[0x10];

	u8         reserved_at_100[0x40];

	u8         db_record_addr_h[0x20];

	u8         db_record_addr_l[0x1e];
	u8         reserved_at_17e[0x2];

	u8         reserved_at_180[0x80];
};

struct mlx5_ifc_vnic_diagnostic_statistics_bits {
	u8         counter_error_queues[0x20];

	u8         total_error_queues[0x20];

	u8         send_queue_priority_update_flow[0x20];

	u8         reserved_at_60[0x20];

	u8         nic_receive_steering_discard[0x40];

	u8         receive_discard_vport_down[0x40];

	u8         transmit_discard_vport_down[0x40];

	u8         reserved_at_140[0xa0];

	u8         internal_rq_out_of_buffer[0x20];

	u8         reserved_at_200[0xe00];
};

struct mlx5_ifc_traffic_counter_bits {
	u8         packets[0x40];

	u8         octets[0x40];
};

struct mlx5_ifc_tisc_bits {
	u8         strict_lag_tx_port_affinity[0x1];
	u8         tls_en[0x1];
	u8         reserved_at_2[0x2];
	u8         lag_tx_port_affinity[0x04];

	u8         reserved_at_8[0x4];
	u8         prio[0x4];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x100];

	u8         reserved_at_120[0x8];
	u8         transport_domain[0x18];

	u8         reserved_at_140[0x8];
	u8         underlay_qpn[0x18];

	u8         reserved_at_160[0x8];
	u8         pd[0x18];

	u8         reserved_at_180[0x380];
};

enum {
	MLX5_TIRC_DISP_TYPE_DIRECT    = 0x0,
	MLX5_TIRC_DISP_TYPE_INDIRECT  = 0x1,
};

enum {
	MLX5_TIRC_LRO_ENABLE_MASK_IPV4_LRO  = 0x1,
	MLX5_TIRC_LRO_ENABLE_MASK_IPV6_LRO  = 0x2,
};

enum {
	MLX5_RX_HASH_FN_NONE           = 0x0,
	MLX5_RX_HASH_FN_INVERTED_XOR8  = 0x1,
	MLX5_RX_HASH_FN_TOEPLITZ       = 0x2,
};

enum {
	MLX5_TIRC_SELF_LB_BLOCK_BLOCK_UNICAST    = 0x1,
	MLX5_TIRC_SELF_LB_BLOCK_BLOCK_MULTICAST  = 0x2,
};

struct mlx5_ifc_tirc_bits {
	u8         reserved_at_0[0x20];

	u8         disp_type[0x4];
	u8         tls_en[0x1];
	u8         reserved_at_25[0x1b];

	u8         reserved_at_40[0x40];

	u8         reserved_at_80[0x4];
	u8         lro_timeout_period_usecs[0x10];
	u8         lro_enable_mask[0x4];
	u8         lro_max_ip_payload_size[0x8];

	u8         reserved_at_a0[0x40];

	u8         reserved_at_e0[0x8];
	u8         inline_rqn[0x18];

	u8         rx_hash_symmetric[0x1];
	u8         reserved_at_101[0x1];
	u8         tunneled_offload_en[0x1];
	u8         reserved_at_103[0x5];
	u8         indirect_table[0x18];

	u8         rx_hash_fn[0x4];
	u8         reserved_at_124[0x2];
	u8         self_lb_block[0x2];
	u8         transport_domain[0x18];

	u8         rx_hash_toeplitz_key[10][0x20];

	struct mlx5_ifc_rx_hash_field_select_bits rx_hash_field_selector_outer;

	struct mlx5_ifc_rx_hash_field_select_bits rx_hash_field_selector_inner;

	u8         reserved_at_2c0[0x4c0];
};

enum {
	MLX5_SRQC_STATE_GOOD   = 0x0,
	MLX5_SRQC_STATE_ERROR  = 0x1,
};

struct mlx5_ifc_srqc_bits {
	u8         state[0x4];
	u8         log_srq_size[0x4];
	u8         reserved_at_8[0x18];

	u8         wq_signature[0x1];
	u8         cont_srq[0x1];
	u8         reserved_at_22[0x1];
	u8         rlky[0x1];
	u8         reserved_at_24[0x1];
	u8         log_rq_stride[0x3];
	u8         xrcd[0x18];

	u8         page_offset[0x6];
	u8         reserved_at_46[0x2];
	u8         cqn[0x18];

	u8         reserved_at_60[0x20];

	u8         reserved_at_80[0x2];
	u8         log_page_size[0x6];
	u8         reserved_at_88[0x18];

	u8         reserved_at_a0[0x20];

	u8         reserved_at_c0[0x8];
	u8         pd[0x18];

	u8         lwm[0x10];
	u8         wqe_cnt[0x10];

	u8         reserved_at_100[0x40];

	u8         dbr_addr[0x40];

	u8         reserved_at_180[0x80];
};

enum {
	MLX5_SQC_STATE_RST  = 0x0,
	MLX5_SQC_STATE_RDY  = 0x1,
	MLX5_SQC_STATE_ERR  = 0x3,
};

struct mlx5_ifc_sqc_bits {
	u8         rlky[0x1];
	u8         cd_master[0x1];
	u8         fre[0x1];
	u8         flush_in_error_en[0x1];
	u8         allow_multi_pkt_send_wqe[0x1];
	u8	   min_wqe_inline_mode[0x3];
	u8         state[0x4];
	u8         reg_umr[0x1];
	u8         allow_swp[0x1];
	u8         hairpin[0x1];
	u8         reserved_at_f[0x11];

	u8         reserved_at_20[0x8];
	u8         user_index[0x18];

	u8         reserved_at_40[0x8];
	u8         cqn[0x18];

	u8         reserved_at_60[0x8];
	u8         hairpin_peer_rq[0x18];

	u8         reserved_at_80[0x10];
	u8         hairpin_peer_vhca[0x10];

	u8         reserved_at_a0[0x50];

	u8         packet_pacing_rate_limit_index[0x10];
	u8         tis_lst_sz[0x10];
	u8         reserved_at_110[0x10];

	u8         reserved_at_120[0x40];

	u8         reserved_at_160[0x8];
	u8         tis_num_0[0x18];

	struct mlx5_ifc_wq_bits wq;
};

enum {
	SCHEDULING_CONTEXT_ELEMENT_TYPE_TSAR = 0x0,
	SCHEDULING_CONTEXT_ELEMENT_TYPE_VPORT = 0x1,
	SCHEDULING_CONTEXT_ELEMENT_TYPE_VPORT_TC = 0x2,
	SCHEDULING_CONTEXT_ELEMENT_TYPE_PARA_VPORT_TC = 0x3,
};

enum {
	ELEMENT_TYPE_CAP_MASK_TASR		= 1 << 0,
	ELEMENT_TYPE_CAP_MASK_VPORT		= 1 << 1,
	ELEMENT_TYPE_CAP_MASK_VPORT_TC		= 1 << 2,
	ELEMENT_TYPE_CAP_MASK_PARA_VPORT_TC	= 1 << 3,
};

struct mlx5_ifc_scheduling_context_bits {
	u8         element_type[0x8];
	u8         reserved_at_8[0x18];

	u8         element_attributes[0x20];

	u8         parent_element_id[0x20];

	u8         reserved_at_60[0x40];

	u8         bw_share[0x20];

	u8         max_average_bw[0x20];

	u8         reserved_at_e0[0x120];
};

struct mlx5_ifc_rqtc_bits {
	u8         reserved_at_0[0xa0];

	u8         reserved_at_a0[0x10];
	u8         rqt_max_size[0x10];

	u8         reserved_at_c0[0x10];
	u8         rqt_actual_size[0x10];

	u8         reserved_at_e0[0x6a0];

	struct mlx5_ifc_rq_num_bits rq_num[];
};

enum {
	MLX5_RQC_MEM_RQ_TYPE_MEMORY_RQ_INLINE  = 0x0,
	MLX5_RQC_MEM_RQ_TYPE_MEMORY_RQ_RMP     = 0x1,
};

enum {
	MLX5_RQC_STATE_RST  = 0x0,
	MLX5_RQC_STATE_RDY  = 0x1,
	MLX5_RQC_STATE_ERR  = 0x3,
};

struct mlx5_ifc_rqc_bits {
	u8         rlky[0x1];
	u8	   delay_drop_en[0x1];
	u8         scatter_fcs[0x1];
	u8         vsd[0x1];
	u8         mem_rq_type[0x4];
	u8         state[0x4];
	u8         reserved_at_c[0x1];
	u8         flush_in_error_en[0x1];
	u8         hairpin[0x1];
	u8         reserved_at_f[0x11];

	u8         reserved_at_20[0x8];
	u8         user_index[0x18];

	u8         reserved_at_40[0x8];
	u8         cqn[0x18];

	u8         counter_set_id[0x8];
	u8         reserved_at_68[0x18];

	u8         reserved_at_80[0x8];
	u8         rmpn[0x18];

	u8         reserved_at_a0[0x8];
	u8         hairpin_peer_sq[0x18];

	u8         reserved_at_c0[0x10];
	u8         hairpin_peer_vhca[0x10];

	u8         reserved_at_e0[0xa0];

	struct mlx5_ifc_wq_bits wq;
};

enum {
	MLX5_RMPC_STATE_RDY  = 0x1,
	MLX5_RMPC_STATE_ERR  = 0x3,
};

struct mlx5_ifc_rmpc_bits {
	u8         reserved_at_0[0x8];
	u8         state[0x4];
	u8         reserved_at_c[0x14];

	u8         basic_cyclic_rcv_wqe[0x1];
	u8         reserved_at_21[0x1f];

	u8         reserved_at_40[0x140];

	struct mlx5_ifc_wq_bits wq;
};

struct mlx5_ifc_nic_vport_context_bits {
	u8         reserved_at_0[0x5];
	u8         min_wqe_inline_mode[0x3];
	u8         reserved_at_8[0x15];
	u8         disable_mc_local_lb[0x1];
	u8         disable_uc_local_lb[0x1];
	u8         roce_en[0x1];

	u8         arm_change_event[0x1];
	u8         reserved_at_21[0x1a];
	u8         event_on_mtu[0x1];
	u8         event_on_promisc_change[0x1];
	u8         event_on_vlan_change[0x1];
	u8         event_on_mc_address_change[0x1];
	u8         event_on_uc_address_change[0x1];

	u8         reserved_at_40[0xc];

	u8	   affiliation_criteria[0x4];
	u8	   affiliated_vhca_id[0x10];

	u8	   reserved_at_60[0xd0];

	u8         mtu[0x10];

	u8         system_image_guid[0x40];
	u8         port_guid[0x40];
	u8         node_guid[0x40];

	u8         reserved_at_200[0x140];
	u8         qkey_violation_counter[0x10];
	u8         reserved_at_350[0x430];

	u8         promisc_uc[0x1];
	u8         promisc_mc[0x1];
	u8         promisc_all[0x1];
	u8         reserved_at_783[0x2];
	u8         allowed_list_type[0x3];
	u8         reserved_at_788[0xc];
	u8         allowed_list_size[0xc];

	struct mlx5_ifc_mac_address_layout_bits permanent_address;

	u8         reserved_at_7e0[0x20];

	u8         current_uc_mac_address[][0x40];
};

enum {
	MLX5_MKC_ACCESS_MODE_PA    = 0x0,
	MLX5_MKC_ACCESS_MODE_MTT   = 0x1,
	MLX5_MKC_ACCESS_MODE_KLMS  = 0x2,
	MLX5_MKC_ACCESS_MODE_KSM   = 0x3,
	MLX5_MKC_ACCESS_MODE_SW_ICM = 0x4,
	MLX5_MKC_ACCESS_MODE_MEMIC = 0x5,
};

struct mlx5_ifc_mkc_bits {
	u8         reserved_at_0[0x1];
	u8         free[0x1];
	u8         reserved_at_2[0x1];
	u8         access_mode_4_2[0x3];
	u8         reserved_at_6[0x7];
	u8         relaxed_ordering_write[0x1];
	u8         reserved_at_e[0x1];
	u8         small_fence_on_rdma_read_response[0x1];
	u8         umr_en[0x1];
	u8         a[0x1];
	u8         rw[0x1];
	u8         rr[0x1];
	u8         lw[0x1];
	u8         lr[0x1];
	u8         access_mode_1_0[0x2];
	u8         reserved_at_18[0x8];

	u8         qpn[0x18];
	u8         mkey_7_0[0x8];

	u8         reserved_at_40[0x20];

	u8         length64[0x1];
	u8         bsf_en[0x1];
	u8         sync_umr[0x1];
	u8         reserved_at_63[0x2];
	u8         expected_sigerr_count[0x1];
	u8         reserved_at_66[0x1];
	u8         en_rinval[0x1];
	u8         pd[0x18];

	u8         start_addr[0x40];

	u8         len[0x40];

	u8         bsf_octword_size[0x20];

	u8         reserved_at_120[0x80];

	u8         translations_octword_size[0x20];

	u8         reserved_at_1c0[0x19];
	u8         relaxed_ordering_read[0x1];
	u8         reserved_at_1d9[0x1];
	u8         log_page_size[0x5];

	u8         reserved_at_1e0[0x20];
};

struct mlx5_ifc_pkey_bits {
	u8         reserved_at_0[0x10];
	u8         pkey[0x10];
};

struct mlx5_ifc_array128_auto_bits {
	u8         array128_auto[16][0x8];
};

struct mlx5_ifc_hca_vport_context_bits {
	u8         field_select[0x20];

	u8         reserved_at_20[0xe0];

	u8         sm_virt_aware[0x1];
	u8         has_smi[0x1];
	u8         has_raw[0x1];
	u8         grh_required[0x1];
	u8         reserved_at_104[0xc];
	u8         port_physical_state[0x4];
	u8         vport_state_policy[0x4];
	u8         port_state[0x4];
	u8         vport_state[0x4];

	u8         reserved_at_120[0x20];

	u8         system_image_guid[0x40];

	u8         port_guid[0x40];

	u8         node_guid[0x40];

	u8         cap_mask1[0x20];

	u8         cap_mask1_field_select[0x20];

	u8         cap_mask2[0x20];

	u8         cap_mask2_field_select[0x20];

	u8         reserved_at_280[0x80];

	u8         lid[0x10];
	u8         reserved_at_310[0x4];
	u8         init_type_reply[0x4];
	u8         lmc[0x3];
	u8         subnet_timeout[0x5];

	u8         sm_lid[0x10];
	u8         sm_sl[0x4];
	u8         reserved_at_334[0xc];

	u8         qkey_violation_counter[0x10];
	u8         pkey_violation_counter[0x10];

	u8         reserved_at_360[0xca0];
};

struct mlx5_ifc_esw_vport_context_bits {
	u8         fdb_to_vport_reg_c[0x1];
	u8         reserved_at_1[0x2];
	u8         vport_svlan_strip[0x1];
	u8         vport_cvlan_strip[0x1];
	u8         vport_svlan_insert[0x1];
	u8         vport_cvlan_insert[0x2];
	u8         fdb_to_vport_reg_c_id[0x8];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x20];

	u8         svlan_cfi[0x1];
	u8         svlan_pcp[0x3];
	u8         svlan_id[0xc];
	u8         cvlan_cfi[0x1];
	u8         cvlan_pcp[0x3];
	u8         cvlan_id[0xc];

	u8         reserved_at_60[0x720];

	u8         sw_steering_vport_icm_address_rx[0x40];

	u8         sw_steering_vport_icm_address_tx[0x40];
};

enum {
	MLX5_EQC_STATUS_OK                = 0x0,
	MLX5_EQC_STATUS_EQ_WRITE_FAILURE  = 0xa,
};

enum {
	MLX5_EQC_ST_ARMED  = 0x9,
	MLX5_EQC_ST_FIRED  = 0xa,
};

struct mlx5_ifc_eqc_bits {
	u8         status[0x4];
	u8         reserved_at_4[0x9];
	u8         ec[0x1];
	u8         oi[0x1];
	u8         reserved_at_f[0x5];
	u8         st[0x4];
	u8         reserved_at_18[0x8];

	u8         reserved_at_20[0x20];

	u8         reserved_at_40[0x14];
	u8         page_offset[0x6];
	u8         reserved_at_5a[0x6];

	u8         reserved_at_60[0x3];
	u8         log_eq_size[0x5];
	u8         uar_page[0x18];

	u8         reserved_at_80[0x20];

	u8         reserved_at_a0[0x18];
	u8         intr[0x8];

	u8         reserved_at_c0[0x3];
	u8         log_page_size[0x5];
	u8         reserved_at_c8[0x18];

	u8         reserved_at_e0[0x60];

	u8         reserved_at_140[0x8];
	u8         consumer_counter[0x18];

	u8         reserved_at_160[0x8];
	u8         producer_counter[0x18];

	u8         reserved_at_180[0x80];
};

enum {
	MLX5_DCTC_STATE_ACTIVE    = 0x0,
	MLX5_DCTC_STATE_DRAINING  = 0x1,
	MLX5_DCTC_STATE_DRAINED   = 0x2,
};

enum {
	MLX5_DCTC_CS_RES_DISABLE    = 0x0,
	MLX5_DCTC_CS_RES_NA         = 0x1,
	MLX5_DCTC_CS_RES_UP_TO_64B  = 0x2,
};

enum {
	MLX5_DCTC_MTU_256_BYTES  = 0x1,
	MLX5_DCTC_MTU_512_BYTES  = 0x2,
	MLX5_DCTC_MTU_1K_BYTES   = 0x3,
	MLX5_DCTC_MTU_2K_BYTES   = 0x4,
	MLX5_DCTC_MTU_4K_BYTES   = 0x5,
};

struct mlx5_ifc_dctc_bits {
	u8         reserved_at_0[0x4];
	u8         state[0x4];
	u8         reserved_at_8[0x18];

	u8         reserved_at_20[0x8];
	u8         user_index[0x18];

	u8         reserved_at_40[0x8];
	u8         cqn[0x18];

	u8         counter_set_id[0x8];
	u8         atomic_mode[0x4];
	u8         rre[0x1];
	u8         rwe[0x1];
	u8         rae[0x1];
	u8         atomic_like_write_en[0x1];
	u8         latency_sensitive[0x1];
	u8         rlky[0x1];
	u8         free_ar[0x1];
	u8         reserved_at_73[0xd];

	u8         reserved_at_80[0x8];
	u8         cs_res[0x8];
	u8         reserved_at_90[0x3];
	u8         min_rnr_nak[0x5];
	u8         reserved_at_98[0x8];

	u8         reserved_at_a0[0x8];
	u8         srqn_xrqn[0x18];

	u8         reserved_at_c0[0x8];
	u8         pd[0x18];

	u8         tclass[0x8];
	u8         reserved_at_e8[0x4];
	u8         flow_label[0x14];

	u8         dc_access_key[0x40];

	u8         reserved_at_140[0x5];
	u8         mtu[0x3];
	u8         port[0x8];
	u8         pkey_index[0x10];

	u8         reserved_at_160[0x8];
	u8         my_addr_index[0x8];
	u8         reserved_at_170[0x8];
	u8         hop_limit[0x8];

	u8         dc_access_key_violation_count[0x20];

	u8         reserved_at_1a0[0x14];
	u8         dei_cfi[0x1];
	u8         eth_prio[0x3];
	u8         ecn[0x2];
	u8         dscp[0x6];

	u8         reserved_at_1c0[0x20];
	u8         ece[0x20];
};

enum {
	MLX5_CQC_STATUS_OK             = 0x0,
	MLX5_CQC_STATUS_CQ_OVERFLOW    = 0x9,
	MLX5_CQC_STATUS_CQ_WRITE_FAIL  = 0xa,
};

enum {
	MLX5_CQC_CQE_SZ_64_BYTES   = 0x0,
	MLX5_CQC_CQE_SZ_128_BYTES  = 0x1,
};

enum {
	MLX5_CQC_ST_SOLICITED_NOTIFICATION_REQUEST_ARMED  = 0x6,
	MLX5_CQC_ST_NOTIFICATION_REQUEST_ARMED            = 0x9,
	MLX5_CQC_ST_FIRED                                 = 0xa,
};

enum {
	MLX5_CQ_PERIOD_MODE_START_FROM_EQE = 0x0,
	MLX5_CQ_PERIOD_MODE_START_FROM_CQE = 0x1,
	MLX5_CQ_PERIOD_NUM_MODES
};

struct mlx5_ifc_cqc_bits {
	u8         status[0x4];
	u8         reserved_at_4[0x2];
	u8         dbr_umem_valid[0x1];
	u8         reserved_at_7[0x1];
	u8         cqe_sz[0x3];
	u8         cc[0x1];
	u8         reserved_at_c[0x1];
	u8         scqe_break_moderation_en[0x1];
	u8         oi[0x1];
	u8         cq_period_mode[0x2];
	u8         cqe_comp_en[0x1];
	u8         mini_cqe_res_format[0x2];
	u8         st[0x4];
	u8         reserved_at_18[0x8];

	u8         reserved_at_20[0x20];

	u8         reserved_at_40[0x14];
	u8         page_offset[0x6];
	u8         reserved_at_5a[0x6];

	u8         reserved_at_60[0x3];
	u8         log_cq_size[0x5];
	u8         uar_page[0x18];

	u8         reserved_at_80[0x4];
	u8         cq_period[0xc];
	u8         cq_max_count[0x10];

	u8         reserved_at_a0[0x18];
	u8         c_eqn[0x8];

	u8         reserved_at_c0[0x3];
	u8         log_page_size[0x5];
	u8         reserved_at_c8[0x18];

	u8         reserved_at_e0[0x20];

	u8         reserved_at_100[0x8];
	u8         last_notified_index[0x18];

	u8         reserved_at_120[0x8];
	u8         last_solicit_index[0x18];

	u8         reserved_at_140[0x8];
	u8         consumer_counter[0x18];

	u8         reserved_at_160[0x8];
	u8         producer_counter[0x18];

	u8         reserved_at_180[0x40];

	u8         dbr_addr[0x40];
};

union mlx5_ifc_cong_control_roce_ecn_auto_bits {
	struct mlx5_ifc_cong_control_802_1qau_rp_bits cong_control_802_1qau_rp;
	struct mlx5_ifc_cong_control_r_roce_ecn_rp_bits cong_control_r_roce_ecn_rp;
	struct mlx5_ifc_cong_control_r_roce_ecn_np_bits cong_control_r_roce_ecn_np;
	u8         reserved_at_0[0x800];
};

struct mlx5_ifc_query_adapter_param_block_bits {
	u8         reserved_at_0[0xc0];

	u8         reserved_at_c0[0x8];
	u8         ieee_vendor_id[0x18];

	u8         reserved_at_e0[0x10];
	u8         vsd_vendor_id[0x10];

	u8         vsd[208][0x8];

	u8         vsd_contd_psid[16][0x8];
};

enum {
	MLX5_XRQC_STATE_GOOD   = 0x0,
	MLX5_XRQC_STATE_ERROR  = 0x1,
};

enum {
	MLX5_XRQC_TOPOLOGY_NO_SPECIAL_TOPOLOGY = 0x0,
	MLX5_XRQC_TOPOLOGY_TAG_MATCHING        = 0x1,
};

enum {
	MLX5_XRQC_OFFLOAD_RNDV = 0x1,
};

struct mlx5_ifc_tag_matching_topology_context_bits {
	u8         log_matching_list_sz[0x4];
	u8         reserved_at_4[0xc];
	u8         append_next_index[0x10];

	u8         sw_phase_cnt[0x10];
	u8         hw_phase_cnt[0x10];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_xrqc_bits {
	u8         state[0x4];
	u8         rlkey[0x1];
	u8         reserved_at_5[0xf];
	u8         topology[0x4];
	u8         reserved_at_18[0x4];
	u8         offload[0x4];

	u8         reserved_at_20[0x8];
	u8         user_index[0x18];

	u8         reserved_at_40[0x8];
	u8         cqn[0x18];

	u8         reserved_at_60[0xa0];

	struct mlx5_ifc_tag_matching_topology_context_bits tag_matching_topology_context;

	u8         reserved_at_180[0x280];

	struct mlx5_ifc_wq_bits wq;
};

union mlx5_ifc_modify_field_select_resize_field_select_auto_bits {
	struct mlx5_ifc_modify_field_select_bits modify_field_select;
	struct mlx5_ifc_resize_field_select_bits resize_field_select;
	u8         reserved_at_0[0x20];
};

union mlx5_ifc_field_select_802_1_r_roce_auto_bits {
	struct mlx5_ifc_field_select_802_1qau_rp_bits field_select_802_1qau_rp;
	struct mlx5_ifc_field_select_r_roce_rp_bits field_select_r_roce_rp;
	struct mlx5_ifc_field_select_r_roce_np_bits field_select_r_roce_np;
	u8         reserved_at_0[0x20];
};

union mlx5_ifc_eth_cntrs_grp_data_layout_auto_bits {
	struct mlx5_ifc_eth_802_3_cntrs_grp_data_layout_bits eth_802_3_cntrs_grp_data_layout;
	struct mlx5_ifc_eth_2863_cntrs_grp_data_layout_bits eth_2863_cntrs_grp_data_layout;
	struct mlx5_ifc_eth_2819_cntrs_grp_data_layout_bits eth_2819_cntrs_grp_data_layout;
	struct mlx5_ifc_eth_3635_cntrs_grp_data_layout_bits eth_3635_cntrs_grp_data_layout;
	struct mlx5_ifc_eth_extended_cntrs_grp_data_layout_bits eth_extended_cntrs_grp_data_layout;
	struct mlx5_ifc_eth_per_prio_grp_data_layout_bits eth_per_prio_grp_data_layout;
	struct mlx5_ifc_eth_per_tc_prio_grp_data_layout_bits eth_per_tc_prio_grp_data_layout;
	struct mlx5_ifc_eth_per_tc_congest_prio_grp_data_layout_bits eth_per_tc_congest_prio_grp_data_layout;
	struct mlx5_ifc_ib_port_cntrs_grp_data_layout_bits ib_port_cntrs_grp_data_layout;
	struct mlx5_ifc_phys_layer_cntrs_bits phys_layer_cntrs;
	struct mlx5_ifc_phys_layer_statistical_cntrs_bits phys_layer_statistical_cntrs;
	u8         reserved_at_0[0x7c0];
};

union mlx5_ifc_pcie_cntrs_grp_data_layout_auto_bits {
	struct mlx5_ifc_pcie_perf_cntrs_grp_data_layout_bits pcie_perf_cntrs_grp_data_layout;
	u8         reserved_at_0[0x7c0];
};

union mlx5_ifc_event_auto_bits {
	struct mlx5_ifc_comp_event_bits comp_event;
	struct mlx5_ifc_dct_events_bits dct_events;
	struct mlx5_ifc_qp_events_bits qp_events;
	struct mlx5_ifc_wqe_associated_page_fault_event_bits wqe_associated_page_fault_event;
	struct mlx5_ifc_rdma_page_fault_event_bits rdma_page_fault_event;
	struct mlx5_ifc_cq_error_bits cq_error;
	struct mlx5_ifc_dropped_packet_logged_bits dropped_packet_logged;
	struct mlx5_ifc_port_state_change_event_bits port_state_change_event;
	struct mlx5_ifc_gpio_event_bits gpio_event;
	struct mlx5_ifc_db_bf_congestion_event_bits db_bf_congestion_event;
	struct mlx5_ifc_stall_vl_event_bits stall_vl_event;
	struct mlx5_ifc_cmd_inter_comp_event_bits cmd_inter_comp_event;
	u8         reserved_at_0[0xe0];
};

struct mlx5_ifc_health_buffer_bits {
	u8         reserved_at_0[0x100];

	u8         assert_existptr[0x20];

	u8         assert_callra[0x20];

	u8         reserved_at_140[0x40];

	u8         fw_version[0x20];

	u8         hw_id[0x20];

	u8         reserved_at_1c0[0x20];

	u8         irisc_index[0x8];
	u8         synd[0x8];
	u8         ext_synd[0x10];
};

struct mlx5_ifc_register_loopback_control_bits {
	u8         no_lb[0x1];
	u8         reserved_at_1[0x7];
	u8         port[0x8];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x60];
};

struct mlx5_ifc_vport_tc_element_bits {
	u8         traffic_class[0x4];
	u8         reserved_at_4[0xc];
	u8         vport_number[0x10];
};

struct mlx5_ifc_vport_element_bits {
	u8         reserved_at_0[0x10];
	u8         vport_number[0x10];
};

enum {
	TSAR_ELEMENT_TSAR_TYPE_DWRR = 0x0,
	TSAR_ELEMENT_TSAR_TYPE_ROUND_ROBIN = 0x1,
	TSAR_ELEMENT_TSAR_TYPE_ETS = 0x2,
};

struct mlx5_ifc_tsar_element_bits {
	u8         reserved_at_0[0x8];
	u8         tsar_type[0x8];
	u8         reserved_at_10[0x10];
};

enum {
	MLX5_TEARDOWN_HCA_OUT_FORCE_STATE_SUCCESS = 0x0,
	MLX5_TEARDOWN_HCA_OUT_FORCE_STATE_FAIL = 0x1,
};

struct mlx5_ifc_teardown_hca_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x3f];

	u8         state[0x1];
};

enum {
	MLX5_TEARDOWN_HCA_IN_PROFILE_GRACEFUL_CLOSE  = 0x0,
	MLX5_TEARDOWN_HCA_IN_PROFILE_FORCE_CLOSE     = 0x1,
	MLX5_TEARDOWN_HCA_IN_PROFILE_PREPARE_FAST_TEARDOWN = 0x2,
};

struct mlx5_ifc_teardown_hca_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x10];
	u8         profile[0x10];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_sqerr2rts_qp_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_sqerr2rts_qp_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         qpn[0x18];

	u8         reserved_at_60[0x20];

	u8         opt_param_mask[0x20];

	u8         reserved_at_a0[0x20];

	struct mlx5_ifc_qpc_bits qpc;

	u8         reserved_at_800[0x80];
};

struct mlx5_ifc_sqd2rts_qp_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_sqd2rts_qp_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         qpn[0x18];

	u8         reserved_at_60[0x20];

	u8         opt_param_mask[0x20];

	u8         reserved_at_a0[0x20];

	struct mlx5_ifc_qpc_bits qpc;

	u8         reserved_at_800[0x80];
};

struct mlx5_ifc_set_roce_address_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_set_roce_address_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         roce_address_index[0x10];
	u8         reserved_at_50[0xc];
	u8	   vhca_port_num[0x4];

	u8         reserved_at_60[0x20];

	struct mlx5_ifc_roce_addr_layout_bits roce_address;
};

struct mlx5_ifc_set_mad_demux_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

enum {
	MLX5_SET_MAD_DEMUX_IN_DEMUX_MODE_PASS_ALL   = 0x0,
	MLX5_SET_MAD_DEMUX_IN_DEMUX_MODE_SELECTIVE  = 0x2,
};

struct mlx5_ifc_set_mad_demux_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x20];

	u8         reserved_at_60[0x6];
	u8         demux_mode[0x2];
	u8         reserved_at_68[0x18];
};

struct mlx5_ifc_set_l2_table_entry_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_set_l2_table_entry_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x60];

	u8         reserved_at_a0[0x8];
	u8         table_index[0x18];

	u8         reserved_at_c0[0x20];

	u8         reserved_at_e0[0x13];
	u8         vlan_valid[0x1];
	u8         vlan[0xc];

	struct mlx5_ifc_mac_address_layout_bits mac_address;

	u8         reserved_at_140[0xc0];
};

struct mlx5_ifc_set_issi_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_set_issi_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x10];
	u8         current_issi[0x10];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_set_hca_cap_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_set_hca_cap_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];

	union mlx5_ifc_hca_cap_union_bits capability;
};

enum {
	MLX5_SET_FTE_MODIFY_ENABLE_MASK_ACTION    = 0x0,
	MLX5_SET_FTE_MODIFY_ENABLE_MASK_FLOW_TAG  = 0x1,
	MLX5_SET_FTE_MODIFY_ENABLE_MASK_DESTINATION_LIST    = 0x2,
	MLX5_SET_FTE_MODIFY_ENABLE_MASK_FLOW_COUNTERS    = 0x3,
	MLX5_SET_FTE_MODIFY_ENABLE_MASK_IPSEC_OBJ_ID    = 0x4
};

struct mlx5_ifc_set_fte_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_set_fte_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         other_vport[0x1];
	u8         reserved_at_41[0xf];
	u8         vport_number[0x10];

	u8         reserved_at_60[0x20];

	u8         table_type[0x8];
	u8         reserved_at_88[0x18];

	u8         reserved_at_a0[0x8];
	u8         table_id[0x18];

	u8         ignore_flow_level[0x1];
	u8         reserved_at_c1[0x17];
	u8         modify_enable_mask[0x8];

	u8         reserved_at_e0[0x20];

	u8         flow_index[0x20];

	u8         reserved_at_120[0xe0];

	struct mlx5_ifc_flow_context_bits flow_context;
};

struct mlx5_ifc_rts2rts_qp_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x20];
	u8         ece[0x20];
};

struct mlx5_ifc_rts2rts_qp_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         qpn[0x18];

	u8         reserved_at_60[0x20];

	u8         opt_param_mask[0x20];

	u8         ece[0x20];

	struct mlx5_ifc_qpc_bits qpc;

	u8         reserved_at_800[0x80];
};

struct mlx5_ifc_rtr2rts_qp_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x20];
	u8         ece[0x20];
};

struct mlx5_ifc_rtr2rts_qp_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         qpn[0x18];

	u8         reserved_at_60[0x20];

	u8         opt_param_mask[0x20];

	u8         ece[0x20];

	struct mlx5_ifc_qpc_bits qpc;

	u8         reserved_at_800[0x80];
};

struct mlx5_ifc_rst2init_qp_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x20];
	u8         ece[0x20];
};

struct mlx5_ifc_rst2init_qp_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         qpn[0x18];

	u8         reserved_at_60[0x20];

	u8         opt_param_mask[0x20];

	u8         ece[0x20];

	struct mlx5_ifc_qpc_bits qpc;

	u8         reserved_at_800[0x80];
};

struct mlx5_ifc_query_xrq_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];

	struct mlx5_ifc_xrqc_bits xrq_context;
};

struct mlx5_ifc_query_xrq_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         xrqn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_query_xrc_srq_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];

	struct mlx5_ifc_xrc_srqc_bits xrc_srq_context_entry;

	u8         reserved_at_280[0x600];

	u8         pas[][0x40];
};

struct mlx5_ifc_query_xrc_srq_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         xrc_srqn[0x18];

	u8         reserved_at_60[0x20];
};

enum {
	MLX5_QUERY_VPORT_STATE_OUT_STATE_DOWN  = 0x0,
	MLX5_QUERY_VPORT_STATE_OUT_STATE_UP    = 0x1,
};

struct mlx5_ifc_query_vport_state_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x20];

	u8         reserved_at_60[0x18];
	u8         admin_state[0x4];
	u8         state[0x4];
};

enum {
	MLX5_VPORT_STATE_OP_MOD_VNIC_VPORT  = 0x0,
	MLX5_VPORT_STATE_OP_MOD_ESW_VPORT   = 0x1,
};

struct mlx5_ifc_arm_monitor_counter_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x20];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_arm_monitor_counter_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

enum {
	MLX5_QUERY_MONITOR_CNT_TYPE_PPCNT     = 0x0,
	MLX5_QUERY_MONITOR_CNT_TYPE_Q_COUNTER = 0x1,
};

enum mlx5_monitor_counter_ppcnt {
	MLX5_QUERY_MONITOR_PPCNT_IN_RANGE_LENGTH_ERRORS      = 0x0,
	MLX5_QUERY_MONITOR_PPCNT_OUT_OF_RANGE_LENGTH_FIELD   = 0x1,
	MLX5_QUERY_MONITOR_PPCNT_FRAME_TOO_LONG_ERRORS       = 0x2,
	MLX5_QUERY_MONITOR_PPCNT_FRAME_CHECK_SEQUENCE_ERRORS = 0x3,
	MLX5_QUERY_MONITOR_PPCNT_ALIGNMENT_ERRORS            = 0x4,
	MLX5_QUERY_MONITOR_PPCNT_IF_OUT_DISCARDS             = 0x5,
};

enum {
	MLX5_QUERY_MONITOR_Q_COUNTER_RX_OUT_OF_BUFFER     = 0x4,
};

struct mlx5_ifc_monitor_counter_output_bits {
	u8         reserved_at_0[0x4];
	u8         type[0x4];
	u8         reserved_at_8[0x8];
	u8         counter[0x10];

	u8         counter_group_id[0x20];
};

#define MLX5_CMD_SET_MONITOR_NUM_PPCNT_COUNTER_SET1 (6)
#define MLX5_CMD_SET_MONITOR_NUM_Q_COUNTERS_SET1    (1)
#define MLX5_CMD_SET_MONITOR_NUM_COUNTER (MLX5_CMD_SET_MONITOR_NUM_PPCNT_COUNTER_SET1 +\
					  MLX5_CMD_SET_MONITOR_NUM_Q_COUNTERS_SET1)

struct mlx5_ifc_set_monitor_counter_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x10];
	u8         num_of_counters[0x10];

	u8         reserved_at_60[0x20];

	struct mlx5_ifc_monitor_counter_output_bits monitor_counter[MLX5_CMD_SET_MONITOR_NUM_COUNTER];
};

struct mlx5_ifc_set_monitor_counter_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_query_vport_state_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         other_vport[0x1];
	u8         reserved_at_41[0xf];
	u8         vport_number[0x10];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_query_vnic_env_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];

	struct mlx5_ifc_vnic_diagnostic_statistics_bits vport_env;
};

enum {
	MLX5_QUERY_VNIC_ENV_IN_OP_MOD_VPORT_DIAG_STATISTICS  = 0x0,
};

struct mlx5_ifc_query_vnic_env_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         other_vport[0x1];
	u8         reserved_at_41[0xf];
	u8         vport_number[0x10];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_query_vport_counter_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];

	struct mlx5_ifc_traffic_counter_bits received_errors;

	struct mlx5_ifc_traffic_counter_bits transmit_errors;

	struct mlx5_ifc_traffic_counter_bits received_ib_unicast;

	struct mlx5_ifc_traffic_counter_bits transmitted_ib_unicast;

	struct mlx5_ifc_traffic_counter_bits received_ib_multicast;

	struct mlx5_ifc_traffic_counter_bits transmitted_ib_multicast;

	struct mlx5_ifc_traffic_counter_bits received_eth_broadcast;

	struct mlx5_ifc_traffic_counter_bits transmitted_eth_broadcast;

	struct mlx5_ifc_traffic_counter_bits received_eth_unicast;

	struct mlx5_ifc_traffic_counter_bits transmitted_eth_unicast;

	struct mlx5_ifc_traffic_counter_bits received_eth_multicast;

	struct mlx5_ifc_traffic_counter_bits transmitted_eth_multicast;

	u8         reserved_at_680[0xa00];
};

enum {
	MLX5_QUERY_VPORT_COUNTER_IN_OP_MOD_VPORT_COUNTERS  = 0x0,
};

struct mlx5_ifc_query_vport_counter_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         other_vport[0x1];
	u8         reserved_at_41[0xb];
	u8	   port_num[0x4];
	u8         vport_number[0x10];

	u8         reserved_at_60[0x60];

	u8         clear[0x1];
	u8         reserved_at_c1[0x1f];

	u8         reserved_at_e0[0x20];
};

struct mlx5_ifc_query_tis_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];

	struct mlx5_ifc_tisc_bits tis_context;
};

struct mlx5_ifc_query_tis_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         tisn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_query_tir_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0xc0];

	struct mlx5_ifc_tirc_bits tir_context;
};

struct mlx5_ifc_query_tir_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         tirn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_query_srq_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];

	struct mlx5_ifc_srqc_bits srq_context_entry;

	u8         reserved_at_280[0x600];

	u8         pas[][0x40];
};

struct mlx5_ifc_query_srq_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         srqn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_query_sq_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0xc0];

	struct mlx5_ifc_sqc_bits sq_context;
};

struct mlx5_ifc_query_sq_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         sqn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_query_special_contexts_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         dump_fill_mkey[0x20];

	u8         resd_lkey[0x20];

	u8         null_mkey[0x20];

	u8         reserved_at_a0[0x60];
};

struct mlx5_ifc_query_special_contexts_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_query_scheduling_element_out_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0xc0];

	struct mlx5_ifc_scheduling_context_bits scheduling_context;

	u8         reserved_at_300[0x100];
};

enum {
	SCHEDULING_HIERARCHY_E_SWITCH = 0x2,
};

struct mlx5_ifc_query_scheduling_element_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         scheduling_hierarchy[0x8];
	u8         reserved_at_48[0x18];

	u8         scheduling_element_id[0x20];

	u8         reserved_at_80[0x180];
};

struct mlx5_ifc_query_rqt_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0xc0];

	struct mlx5_ifc_rqtc_bits rqt_context;
};

struct mlx5_ifc_query_rqt_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         rqtn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_query_rq_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0xc0];

	struct mlx5_ifc_rqc_bits rq_context;
};

struct mlx5_ifc_query_rq_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         rqn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_query_roce_address_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];

	struct mlx5_ifc_roce_addr_layout_bits roce_address;
};

struct mlx5_ifc_query_roce_address_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         roce_address_index[0x10];
	u8         reserved_at_50[0xc];
	u8	   vhca_port_num[0x4];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_query_rmp_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0xc0];

	struct mlx5_ifc_rmpc_bits rmp_context;
};

struct mlx5_ifc_query_rmp_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         rmpn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_query_qp_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x20];
	u8         ece[0x20];

	u8         opt_param_mask[0x20];

	u8         reserved_at_a0[0x20];

	struct mlx5_ifc_qpc_bits qpc;

	u8         reserved_at_800[0x80];

	u8         pas[][0x40];
};

struct mlx5_ifc_query_qp_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         qpn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_query_q_counter_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];

	u8         rx_write_requests[0x20];

	u8         reserved_at_a0[0x20];

	u8         rx_read_requests[0x20];

	u8         reserved_at_e0[0x20];

	u8         rx_atomic_requests[0x20];

	u8         reserved_at_120[0x20];

	u8         rx_dct_connect[0x20];

	u8         reserved_at_160[0x20];

	u8         out_of_buffer[0x20];

	u8         reserved_at_1a0[0x20];

	u8         out_of_sequence[0x20];

	u8         reserved_at_1e0[0x20];

	u8         duplicate_request[0x20];

	u8         reserved_at_220[0x20];

	u8         rnr_nak_retry_err[0x20];

	u8         reserved_at_260[0x20];

	u8         packet_seq_err[0x20];

	u8         reserved_at_2a0[0x20];

	u8         implied_nak_seq_err[0x20];

	u8         reserved_at_2e0[0x20];

	u8         local_ack_timeout_err[0x20];

	u8         reserved_at_320[0xa0];

	u8         resp_local_length_error[0x20];

	u8         req_local_length_error[0x20];

	u8         resp_local_qp_error[0x20];

	u8         local_operation_error[0x20];

	u8         resp_local_protection[0x20];

	u8         req_local_protection[0x20];

	u8         resp_cqe_error[0x20];

	u8         req_cqe_error[0x20];

	u8         req_mw_binding[0x20];

	u8         req_bad_response[0x20];

	u8         req_remote_invalid_request[0x20];

	u8         resp_remote_invalid_request[0x20];

	u8         req_remote_access_errors[0x20];

	u8	   resp_remote_access_errors[0x20];

	u8         req_remote_operation_errors[0x20];

	u8         req_transport_retries_exceeded[0x20];

	u8         cq_overflow[0x20];

	u8         resp_cqe_flush_error[0x20];

	u8         req_cqe_flush_error[0x20];

	u8         reserved_at_620[0x20];

	u8         roce_adp_retrans[0x20];

	u8         roce_adp_retrans_to[0x20];

	u8         roce_slow_restart[0x20];

	u8         roce_slow_restart_cnps[0x20];

	u8         roce_slow_restart_trans[0x20];

	u8         reserved_at_6e0[0x120];
};

struct mlx5_ifc_query_q_counter_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x80];

	u8         clear[0x1];
	u8         reserved_at_c1[0x1f];

	u8         reserved_at_e0[0x18];
	u8         counter_set_id[0x8];
};

struct mlx5_ifc_query_pages_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         embedded_cpu_function[0x1];
	u8         reserved_at_41[0xf];
	u8         function_id[0x10];

	u8         num_pages[0x20];
};

enum {
	MLX5_QUERY_PAGES_IN_OP_MOD_BOOT_PAGES     = 0x1,
	MLX5_QUERY_PAGES_IN_OP_MOD_INIT_PAGES     = 0x2,
	MLX5_QUERY_PAGES_IN_OP_MOD_REGULAR_PAGES  = 0x3,
};

struct mlx5_ifc_query_pages_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         embedded_cpu_function[0x1];
	u8         reserved_at_41[0xf];
	u8         function_id[0x10];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_query_nic_vport_context_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];

	struct mlx5_ifc_nic_vport_context_bits nic_vport_context;
};

struct mlx5_ifc_query_nic_vport_context_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         other_vport[0x1];
	u8         reserved_at_41[0xf];
	u8         vport_number[0x10];

	u8         reserved_at_60[0x5];
	u8         allowed_list_type[0x3];
	u8         reserved_at_68[0x18];
};

struct mlx5_ifc_query_mkey_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];

	struct mlx5_ifc_mkc_bits memory_key_mkey_entry;

	u8         reserved_at_280[0x600];

	u8         bsf0_klm0_pas_mtt0_1[16][0x8];

	u8         bsf1_klm1_pas_mtt2_3[16][0x8];
};

struct mlx5_ifc_query_mkey_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         mkey_index[0x18];

	u8         pg_access[0x1];
	u8         reserved_at_61[0x1f];
};

struct mlx5_ifc_query_mad_demux_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];

	u8         mad_dumux_parameters_block[0x20];
};

struct mlx5_ifc_query_mad_demux_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_query_l2_table_entry_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0xa0];

	u8         reserved_at_e0[0x13];
	u8         vlan_valid[0x1];
	u8         vlan[0xc];

	struct mlx5_ifc_mac_address_layout_bits mac_address;

	u8         reserved_at_140[0xc0];
};

struct mlx5_ifc_query_l2_table_entry_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x60];

	u8         reserved_at_a0[0x8];
	u8         table_index[0x18];

	u8         reserved_at_c0[0x140];
};

struct mlx5_ifc_query_issi_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x10];
	u8         current_issi[0x10];

	u8         reserved_at_60[0xa0];

	u8         reserved_at_100[76][0x8];
	u8         supported_issi_dw0[0x20];
};

struct mlx5_ifc_query_issi_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_set_driver_version_out_bits {
	u8         status[0x8];
	u8         reserved_0[0x18];

	u8         syndrome[0x20];
	u8         reserved_1[0x40];
};

struct mlx5_ifc_set_driver_version_in_bits {
	u8         opcode[0x10];
	u8         reserved_0[0x10];

	u8         reserved_1[0x10];
	u8         op_mod[0x10];

	u8         reserved_2[0x40];
	u8         driver_version[64][0x8];
};

struct mlx5_ifc_query_hca_vport_pkey_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];

	struct mlx5_ifc_pkey_bits pkey[];
};

struct mlx5_ifc_query_hca_vport_pkey_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         other_vport[0x1];
	u8         reserved_at_41[0xb];
	u8         port_num[0x4];
	u8         vport_number[0x10];

	u8         reserved_at_60[0x10];
	u8         pkey_index[0x10];
};

enum {
	MLX5_HCA_VPORT_SEL_PORT_GUID	= 1 << 0,
	MLX5_HCA_VPORT_SEL_NODE_GUID	= 1 << 1,
	MLX5_HCA_VPORT_SEL_STATE_POLICY	= 1 << 2,
};

struct mlx5_ifc_query_hca_vport_gid_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x20];

	u8         gids_num[0x10];
	u8         reserved_at_70[0x10];

	struct mlx5_ifc_array128_auto_bits gid[];
};

struct mlx5_ifc_query_hca_vport_gid_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         other_vport[0x1];
	u8         reserved_at_41[0xb];
	u8         port_num[0x4];
	u8         vport_number[0x10];

	u8         reserved_at_60[0x10];
	u8         gid_index[0x10];
};

struct mlx5_ifc_query_hca_vport_context_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];

	struct mlx5_ifc_hca_vport_context_bits hca_vport_context;
};

struct mlx5_ifc_query_hca_vport_context_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         other_vport[0x1];
	u8         reserved_at_41[0xb];
	u8         port_num[0x4];
	u8         vport_number[0x10];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_query_hca_cap_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];

	union mlx5_ifc_hca_cap_union_bits capability;
};

struct mlx5_ifc_query_hca_cap_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         other_function[0x1];
	u8         reserved_at_41[0xf];
	u8         function_id[0x10];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_other_hca_cap_bits {
	u8         roce[0x1];
	u8         reserved_at_1[0x27f];
};

struct mlx5_ifc_query_other_hca_cap_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];

	struct     mlx5_ifc_other_hca_cap_bits other_capability;
};

struct mlx5_ifc_query_other_hca_cap_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x10];
	u8         function_id[0x10];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_modify_other_hca_cap_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_modify_other_hca_cap_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x10];
	u8         function_id[0x10];
	u8         field_select[0x20];

	struct     mlx5_ifc_other_hca_cap_bits other_capability;
};

struct mlx5_ifc_flow_table_context_bits {
	u8         reformat_en[0x1];
	u8         decap_en[0x1];
	u8         sw_owner[0x1];
	u8         termination_table[0x1];
	u8         table_miss_action[0x4];
	u8         level[0x8];
	u8         reserved_at_10[0x8];
	u8         log_size[0x8];

	u8         reserved_at_20[0x8];
	u8         table_miss_id[0x18];

	u8         reserved_at_40[0x8];
	u8         lag_master_next_table_id[0x18];

	u8         reserved_at_60[0x60];

	u8         sw_owner_icm_root_1[0x40];

	u8         sw_owner_icm_root_0[0x40];

};

struct mlx5_ifc_query_flow_table_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x80];

	struct mlx5_ifc_flow_table_context_bits flow_table_context;
};

struct mlx5_ifc_query_flow_table_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];

	u8         table_type[0x8];
	u8         reserved_at_88[0x18];

	u8         reserved_at_a0[0x8];
	u8         table_id[0x18];

	u8         reserved_at_c0[0x140];
};

struct mlx5_ifc_query_fte_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x1c0];

	struct mlx5_ifc_flow_context_bits flow_context;
};

struct mlx5_ifc_query_fte_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];

	u8         table_type[0x8];
	u8         reserved_at_88[0x18];

	u8         reserved_at_a0[0x8];
	u8         table_id[0x18];

	u8         reserved_at_c0[0x40];

	u8         flow_index[0x20];

	u8         reserved_at_120[0xe0];
};

enum {
	MLX5_QUERY_FLOW_GROUP_OUT_MATCH_CRITERIA_ENABLE_OUTER_HEADERS    = 0x0,
	MLX5_QUERY_FLOW_GROUP_OUT_MATCH_CRITERIA_ENABLE_MISC_PARAMETERS  = 0x1,
	MLX5_QUERY_FLOW_GROUP_OUT_MATCH_CRITERIA_ENABLE_INNER_HEADERS    = 0x2,
	MLX5_QUERY_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_MISC_PARAMETERS_2 = 0x3,
	MLX5_QUERY_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_MISC_PARAMETERS_3 = 0x4,
};

struct mlx5_ifc_query_flow_group_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0xa0];

	u8         start_flow_index[0x20];

	u8         reserved_at_100[0x20];

	u8         end_flow_index[0x20];

	u8         reserved_at_140[0xa0];

	u8         reserved_at_1e0[0x18];
	u8         match_criteria_enable[0x8];

	struct mlx5_ifc_fte_match_param_bits match_criteria;

	u8         reserved_at_1200[0xe00];
};

struct mlx5_ifc_query_flow_group_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];

	u8         table_type[0x8];
	u8         reserved_at_88[0x18];

	u8         reserved_at_a0[0x8];
	u8         table_id[0x18];

	u8         group_id[0x20];

	u8         reserved_at_e0[0x120];
};

struct mlx5_ifc_query_flow_counter_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];

	struct mlx5_ifc_traffic_counter_bits flow_statistics[];
};

struct mlx5_ifc_query_flow_counter_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x80];

	u8         clear[0x1];
	u8         reserved_at_c1[0xf];
	u8         num_of_counters[0x10];

	u8         flow_counter_id[0x20];
};

struct mlx5_ifc_query_esw_vport_context_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];

	struct mlx5_ifc_esw_vport_context_bits esw_vport_context;
};

struct mlx5_ifc_query_esw_vport_context_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         other_vport[0x1];
	u8         reserved_at_41[0xf];
	u8         vport_number[0x10];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_modify_esw_vport_context_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_esw_vport_context_fields_select_bits {
	u8         reserved_at_0[0x1b];
	u8         fdb_to_vport_reg_c_id[0x1];
	u8         vport_cvlan_insert[0x1];
	u8         vport_svlan_insert[0x1];
	u8         vport_cvlan_strip[0x1];
	u8         vport_svlan_strip[0x1];
};

struct mlx5_ifc_modify_esw_vport_context_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         other_vport[0x1];
	u8         reserved_at_41[0xf];
	u8         vport_number[0x10];

	struct mlx5_ifc_esw_vport_context_fields_select_bits field_select;

	struct mlx5_ifc_esw_vport_context_bits esw_vport_context;
};

struct mlx5_ifc_query_eq_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];

	struct mlx5_ifc_eqc_bits eq_context_entry;

	u8         reserved_at_280[0x40];

	u8         event_bitmask[0x40];

	u8         reserved_at_300[0x580];

	u8         pas[][0x40];
};

struct mlx5_ifc_query_eq_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x18];
	u8         eq_number[0x8];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_packet_reformat_context_in_bits {
	u8         reserved_at_0[0x5];
	u8         reformat_type[0x3];
	u8         reserved_at_8[0xe];
	u8         reformat_data_size[0xa];

	u8         reserved_at_20[0x10];
	u8         reformat_data[2][0x8];

	u8         more_reformat_data[][0x8];
};

struct mlx5_ifc_query_packet_reformat_context_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0xa0];

	struct mlx5_ifc_packet_reformat_context_in_bits packet_reformat_context[];
};

struct mlx5_ifc_query_packet_reformat_context_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         packet_reformat_id[0x20];

	u8         reserved_at_60[0xa0];
};

struct mlx5_ifc_alloc_packet_reformat_context_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         packet_reformat_id[0x20];

	u8         reserved_at_60[0x20];
};

enum mlx5_reformat_ctx_type {
	MLX5_REFORMAT_TYPE_L2_TO_VXLAN = 0x0,
	MLX5_REFORMAT_TYPE_L2_TO_NVGRE = 0x1,
	MLX5_REFORMAT_TYPE_L2_TO_L2_TUNNEL = 0x2,
	MLX5_REFORMAT_TYPE_L3_TUNNEL_TO_L2 = 0x3,
	MLX5_REFORMAT_TYPE_L2_TO_L3_TUNNEL = 0x4,
};

struct mlx5_ifc_alloc_packet_reformat_context_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0xa0];

	struct mlx5_ifc_packet_reformat_context_in_bits packet_reformat_context;
};

struct mlx5_ifc_dealloc_packet_reformat_context_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_dealloc_packet_reformat_context_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_20[0x10];
	u8         op_mod[0x10];

	u8         packet_reformat_id[0x20];

	u8         reserved_60[0x20];
};

struct mlx5_ifc_set_action_in_bits {
	u8         action_type[0x4];
	u8         field[0xc];
	u8         reserved_at_10[0x3];
	u8         offset[0x5];
	u8         reserved_at_18[0x3];
	u8         length[0x5];

	u8         data[0x20];
};

struct mlx5_ifc_add_action_in_bits {
	u8         action_type[0x4];
	u8         field[0xc];
	u8         reserved_at_10[0x10];

	u8         data[0x20];
};

struct mlx5_ifc_copy_action_in_bits {
	u8         action_type[0x4];
	u8         src_field[0xc];
	u8         reserved_at_10[0x3];
	u8         src_offset[0x5];
	u8         reserved_at_18[0x3];
	u8         length[0x5];

	u8         reserved_at_20[0x4];
	u8         dst_field[0xc];
	u8         reserved_at_30[0x3];
	u8         dst_offset[0x5];
	u8         reserved_at_38[0x8];
};

union mlx5_ifc_set_add_copy_action_in_auto_bits {
	struct mlx5_ifc_set_action_in_bits  set_action_in;
	struct mlx5_ifc_add_action_in_bits  add_action_in;
	struct mlx5_ifc_copy_action_in_bits copy_action_in;
	u8         reserved_at_0[0x40];
};

enum {
	MLX5_ACTION_TYPE_SET   = 0x1,
	MLX5_ACTION_TYPE_ADD   = 0x2,
	MLX5_ACTION_TYPE_COPY  = 0x3,
};

enum {
	MLX5_ACTION_IN_FIELD_OUT_SMAC_47_16    = 0x1,
	MLX5_ACTION_IN_FIELD_OUT_SMAC_15_0     = 0x2,
	MLX5_ACTION_IN_FIELD_OUT_ETHERTYPE     = 0x3,
	MLX5_ACTION_IN_FIELD_OUT_DMAC_47_16    = 0x4,
	MLX5_ACTION_IN_FIELD_OUT_DMAC_15_0     = 0x5,
	MLX5_ACTION_IN_FIELD_OUT_IP_DSCP       = 0x6,
	MLX5_ACTION_IN_FIELD_OUT_TCP_FLAGS     = 0x7,
	MLX5_ACTION_IN_FIELD_OUT_TCP_SPORT     = 0x8,
	MLX5_ACTION_IN_FIELD_OUT_TCP_DPORT     = 0x9,
	MLX5_ACTION_IN_FIELD_OUT_IP_TTL        = 0xa,
	MLX5_ACTION_IN_FIELD_OUT_UDP_SPORT     = 0xb,
	MLX5_ACTION_IN_FIELD_OUT_UDP_DPORT     = 0xc,
	MLX5_ACTION_IN_FIELD_OUT_SIPV6_127_96  = 0xd,
	MLX5_ACTION_IN_FIELD_OUT_SIPV6_95_64   = 0xe,
	MLX5_ACTION_IN_FIELD_OUT_SIPV6_63_32   = 0xf,
	MLX5_ACTION_IN_FIELD_OUT_SIPV6_31_0    = 0x10,
	MLX5_ACTION_IN_FIELD_OUT_DIPV6_127_96  = 0x11,
	MLX5_ACTION_IN_FIELD_OUT_DIPV6_95_64   = 0x12,
	MLX5_ACTION_IN_FIELD_OUT_DIPV6_63_32   = 0x13,
	MLX5_ACTION_IN_FIELD_OUT_DIPV6_31_0    = 0x14,
	MLX5_ACTION_IN_FIELD_OUT_SIPV4         = 0x15,
	MLX5_ACTION_IN_FIELD_OUT_DIPV4         = 0x16,
	MLX5_ACTION_IN_FIELD_OUT_FIRST_VID     = 0x17,
	MLX5_ACTION_IN_FIELD_OUT_IPV6_HOPLIMIT = 0x47,
	MLX5_ACTION_IN_FIELD_METADATA_REG_A    = 0x49,
	MLX5_ACTION_IN_FIELD_METADATA_REG_B    = 0x50,
	MLX5_ACTION_IN_FIELD_METADATA_REG_C_0  = 0x51,
	MLX5_ACTION_IN_FIELD_METADATA_REG_C_1  = 0x52,
	MLX5_ACTION_IN_FIELD_METADATA_REG_C_2  = 0x53,
	MLX5_ACTION_IN_FIELD_METADATA_REG_C_3  = 0x54,
	MLX5_ACTION_IN_FIELD_METADATA_REG_C_4  = 0x55,
	MLX5_ACTION_IN_FIELD_METADATA_REG_C_5  = 0x56,
	MLX5_ACTION_IN_FIELD_METADATA_REG_C_6  = 0x57,
	MLX5_ACTION_IN_FIELD_METADATA_REG_C_7  = 0x58,
	MLX5_ACTION_IN_FIELD_OUT_TCP_SEQ_NUM   = 0x59,
	MLX5_ACTION_IN_FIELD_OUT_TCP_ACK_NUM   = 0x5B,
};

struct mlx5_ifc_alloc_modify_header_context_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         modify_header_id[0x20];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_alloc_modify_header_context_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x20];

	u8         table_type[0x8];
	u8         reserved_at_68[0x10];
	u8         num_of_actions[0x8];

	union mlx5_ifc_set_add_copy_action_in_auto_bits actions[0];
};

struct mlx5_ifc_dealloc_modify_header_context_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_dealloc_modify_header_context_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         modify_header_id[0x20];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_query_dct_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];

	struct mlx5_ifc_dctc_bits dct_context_entry;

	u8         reserved_at_280[0x180];
};

struct mlx5_ifc_query_dct_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         dctn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_query_cq_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];

	struct mlx5_ifc_cqc_bits cq_context;

	u8         reserved_at_280[0x600];

	u8         pas[][0x40];
};

struct mlx5_ifc_query_cq_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         cqn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_query_cong_status_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x20];

	u8         enable[0x1];
	u8         tag_enable[0x1];
	u8         reserved_at_62[0x1e];
};

struct mlx5_ifc_query_cong_status_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x18];
	u8         priority[0x4];
	u8         cong_protocol[0x4];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_query_cong_statistics_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];

	u8         rp_cur_flows[0x20];

	u8         sum_flows[0x20];

	u8         rp_cnp_ignored_high[0x20];

	u8         rp_cnp_ignored_low[0x20];

	u8         rp_cnp_handled_high[0x20];

	u8         rp_cnp_handled_low[0x20];

	u8         reserved_at_140[0x100];

	u8         time_stamp_high[0x20];

	u8         time_stamp_low[0x20];

	u8         accumulators_period[0x20];

	u8         np_ecn_marked_roce_packets_high[0x20];

	u8         np_ecn_marked_roce_packets_low[0x20];

	u8         np_cnp_sent_high[0x20];

	u8         np_cnp_sent_low[0x20];

	u8         reserved_at_320[0x560];
};

struct mlx5_ifc_query_cong_statistics_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         clear[0x1];
	u8         reserved_at_41[0x1f];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_query_cong_params_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];

	union mlx5_ifc_cong_control_roce_ecn_auto_bits congestion_parameters;
};

struct mlx5_ifc_query_cong_params_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x1c];
	u8         cong_protocol[0x4];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_query_adapter_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];

	struct mlx5_ifc_query_adapter_param_block_bits query_adapter_struct;
};

struct mlx5_ifc_query_adapter_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_qp_2rst_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_qp_2rst_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         qpn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_qp_2err_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_qp_2err_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         qpn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_page_fault_resume_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_page_fault_resume_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         error[0x1];
	u8         reserved_at_41[0x4];
	u8         page_fault_type[0x3];
	u8         wq_number[0x18];

	u8         reserved_at_60[0x8];
	u8         token[0x18];
};

struct mlx5_ifc_nop_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_nop_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_modify_vport_state_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_modify_vport_state_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         other_vport[0x1];
	u8         reserved_at_41[0xf];
	u8         vport_number[0x10];

	u8         reserved_at_60[0x18];
	u8         admin_state[0x4];
	u8         reserved_at_7c[0x4];
};

struct mlx5_ifc_modify_tis_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_modify_tis_bitmask_bits {
	u8         reserved_at_0[0x20];

	u8         reserved_at_20[0x1d];
	u8         lag_tx_port_affinity[0x1];
	u8         strict_lag_tx_port_affinity[0x1];
	u8         prio[0x1];
};

struct mlx5_ifc_modify_tis_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         tisn[0x18];

	u8         reserved_at_60[0x20];

	struct mlx5_ifc_modify_tis_bitmask_bits bitmask;

	u8         reserved_at_c0[0x40];

	struct mlx5_ifc_tisc_bits ctx;
};

struct mlx5_ifc_modify_tir_bitmask_bits {
	u8	   reserved_at_0[0x20];

	u8         reserved_at_20[0x1b];
	u8         self_lb_en[0x1];
	u8         reserved_at_3c[0x1];
	u8         hash[0x1];
	u8         reserved_at_3e[0x1];
	u8         lro[0x1];
};

struct mlx5_ifc_modify_tir_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_modify_tir_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         tirn[0x18];

	u8         reserved_at_60[0x20];

	struct mlx5_ifc_modify_tir_bitmask_bits bitmask;

	u8         reserved_at_c0[0x40];

	struct mlx5_ifc_tirc_bits ctx;
};

struct mlx5_ifc_modify_sq_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_modify_sq_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         sq_state[0x4];
	u8         reserved_at_44[0x4];
	u8         sqn[0x18];

	u8         reserved_at_60[0x20];

	u8         modify_bitmask[0x40];

	u8         reserved_at_c0[0x40];

	struct mlx5_ifc_sqc_bits ctx;
};

struct mlx5_ifc_modify_scheduling_element_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x1c0];
};

enum {
	MODIFY_SCHEDULING_ELEMENT_IN_MODIFY_BITMASK_BW_SHARE = 0x1,
	MODIFY_SCHEDULING_ELEMENT_IN_MODIFY_BITMASK_MAX_AVERAGE_BW = 0x2,
};

struct mlx5_ifc_modify_scheduling_element_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         scheduling_hierarchy[0x8];
	u8         reserved_at_48[0x18];

	u8         scheduling_element_id[0x20];

	u8         reserved_at_80[0x20];

	u8         modify_bitmask[0x20];

	u8         reserved_at_c0[0x40];

	struct mlx5_ifc_scheduling_context_bits scheduling_context;

	u8         reserved_at_300[0x100];
};

struct mlx5_ifc_modify_rqt_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_rqt_bitmask_bits {
	u8	   reserved_at_0[0x20];

	u8         reserved_at_20[0x1f];
	u8         rqn_list[0x1];
};

struct mlx5_ifc_modify_rqt_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         rqtn[0x18];

	u8         reserved_at_60[0x20];

	struct mlx5_ifc_rqt_bitmask_bits bitmask;

	u8         reserved_at_c0[0x40];

	struct mlx5_ifc_rqtc_bits ctx;
};

struct mlx5_ifc_modify_rq_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

enum {
	MLX5_MODIFY_RQ_IN_MODIFY_BITMASK_VSD = 1ULL << 1,
	MLX5_MODIFY_RQ_IN_MODIFY_BITMASK_SCATTER_FCS = 1ULL << 2,
	MLX5_MODIFY_RQ_IN_MODIFY_BITMASK_RQ_COUNTER_SET_ID = 1ULL << 3,
};

struct mlx5_ifc_modify_rq_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         rq_state[0x4];
	u8         reserved_at_44[0x4];
	u8         rqn[0x18];

	u8         reserved_at_60[0x20];

	u8         modify_bitmask[0x40];

	u8         reserved_at_c0[0x40];

	struct mlx5_ifc_rqc_bits ctx;
};

struct mlx5_ifc_modify_rmp_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_rmp_bitmask_bits {
	u8	   reserved_at_0[0x20];

	u8         reserved_at_20[0x1f];
	u8         lwm[0x1];
};

struct mlx5_ifc_modify_rmp_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         rmp_state[0x4];
	u8         reserved_at_44[0x4];
	u8         rmpn[0x18];

	u8         reserved_at_60[0x20];

	struct mlx5_ifc_rmp_bitmask_bits bitmask;

	u8         reserved_at_c0[0x40];

	struct mlx5_ifc_rmpc_bits ctx;
};

struct mlx5_ifc_modify_nic_vport_context_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_modify_nic_vport_field_select_bits {
	u8         reserved_at_0[0x12];
	u8	   affiliation[0x1];
	u8	   reserved_at_13[0x1];
	u8         disable_uc_local_lb[0x1];
	u8         disable_mc_local_lb[0x1];
	u8         node_guid[0x1];
	u8         port_guid[0x1];
	u8         min_inline[0x1];
	u8         mtu[0x1];
	u8         change_event[0x1];
	u8         promisc[0x1];
	u8         permanent_address[0x1];
	u8         addresses_list[0x1];
	u8         roce_en[0x1];
	u8         reserved_at_1f[0x1];
};

struct mlx5_ifc_modify_nic_vport_context_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         other_vport[0x1];
	u8         reserved_at_41[0xf];
	u8         vport_number[0x10];

	struct mlx5_ifc_modify_nic_vport_field_select_bits field_select;

	u8         reserved_at_80[0x780];

	struct mlx5_ifc_nic_vport_context_bits nic_vport_context;
};

struct mlx5_ifc_modify_hca_vport_context_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_modify_hca_vport_context_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         other_vport[0x1];
	u8         reserved_at_41[0xb];
	u8         port_num[0x4];
	u8         vport_number[0x10];

	u8         reserved_at_60[0x20];

	struct mlx5_ifc_hca_vport_context_bits hca_vport_context;
};

struct mlx5_ifc_modify_cq_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

enum {
	MLX5_MODIFY_CQ_IN_OP_MOD_MODIFY_CQ  = 0x0,
	MLX5_MODIFY_CQ_IN_OP_MOD_RESIZE_CQ  = 0x1,
};

struct mlx5_ifc_modify_cq_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         cqn[0x18];

	union mlx5_ifc_modify_field_select_resize_field_select_auto_bits modify_field_select_resize_field_select;

	struct mlx5_ifc_cqc_bits cq_context;

	u8         reserved_at_280[0x60];

	u8         cq_umem_valid[0x1];
	u8         reserved_at_2e1[0x1f];

	u8         reserved_at_300[0x580];

	u8         pas[][0x40];
};

struct mlx5_ifc_modify_cong_status_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_modify_cong_status_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x18];
	u8         priority[0x4];
	u8         cong_protocol[0x4];

	u8         enable[0x1];
	u8         tag_enable[0x1];
	u8         reserved_at_62[0x1e];
};

struct mlx5_ifc_modify_cong_params_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_modify_cong_params_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x1c];
	u8         cong_protocol[0x4];

	union mlx5_ifc_field_select_802_1_r_roce_auto_bits field_select;

	u8         reserved_at_80[0x80];

	union mlx5_ifc_cong_control_roce_ecn_auto_bits congestion_parameters;
};

struct mlx5_ifc_manage_pages_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         output_num_entries[0x20];

	u8         reserved_at_60[0x20];

	u8         pas[][0x40];
};

enum {
	MLX5_MANAGE_PAGES_IN_OP_MOD_ALLOCATION_FAIL     = 0x0,
	MLX5_MANAGE_PAGES_IN_OP_MOD_ALLOCATION_SUCCESS  = 0x1,
	MLX5_MANAGE_PAGES_IN_OP_MOD_HCA_RETURN_PAGES    = 0x2,
};

struct mlx5_ifc_manage_pages_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         embedded_cpu_function[0x1];
	u8         reserved_at_41[0xf];
	u8         function_id[0x10];

	u8         input_num_entries[0x20];

	u8         pas[][0x40];
};

struct mlx5_ifc_mad_ifc_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];

	u8         response_mad_packet[256][0x8];
};

struct mlx5_ifc_mad_ifc_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         remote_lid[0x10];
	u8         reserved_at_50[0x8];
	u8         port[0x8];

	u8         reserved_at_60[0x20];

	u8         mad[256][0x8];
};

struct mlx5_ifc_init_hca_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_init_hca_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];
	u8	   sw_owner_id[4][0x20];
};

struct mlx5_ifc_init2rtr_qp_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x20];
	u8         ece[0x20];
};

struct mlx5_ifc_init2rtr_qp_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         qpn[0x18];

	u8         reserved_at_60[0x20];

	u8         opt_param_mask[0x20];

	u8         ece[0x20];

	struct mlx5_ifc_qpc_bits qpc;

	u8         reserved_at_800[0x80];
};

struct mlx5_ifc_init2init_qp_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x20];
	u8         ece[0x20];
};

struct mlx5_ifc_init2init_qp_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         qpn[0x18];

	u8         reserved_at_60[0x20];

	u8         opt_param_mask[0x20];

	u8         ece[0x20];

	struct mlx5_ifc_qpc_bits qpc;

	u8         reserved_at_800[0x80];
};

struct mlx5_ifc_get_dropped_packet_log_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];

	u8         packet_headers_log[128][0x8];

	u8         packet_syndrome[64][0x8];
};

struct mlx5_ifc_get_dropped_packet_log_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_gen_eqe_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x18];
	u8         eq_number[0x8];

	u8         reserved_at_60[0x20];

	u8         eqe[64][0x8];
};

struct mlx5_ifc_gen_eq_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_enable_hca_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x20];
};

struct mlx5_ifc_enable_hca_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         embedded_cpu_function[0x1];
	u8         reserved_at_41[0xf];
	u8         function_id[0x10];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_drain_dct_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_drain_dct_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         dctn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_disable_hca_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x20];
};

struct mlx5_ifc_disable_hca_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         embedded_cpu_function[0x1];
	u8         reserved_at_41[0xf];
	u8         function_id[0x10];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_detach_from_mcg_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_detach_from_mcg_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         qpn[0x18];

	u8         reserved_at_60[0x20];

	u8         multicast_gid[16][0x8];
};

struct mlx5_ifc_destroy_xrq_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_destroy_xrq_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         xrqn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_destroy_xrc_srq_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_destroy_xrc_srq_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         xrc_srqn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_destroy_tis_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_destroy_tis_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         tisn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_destroy_tir_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_destroy_tir_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         tirn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_destroy_srq_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_destroy_srq_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         srqn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_destroy_sq_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_destroy_sq_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         sqn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_destroy_scheduling_element_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x1c0];
};

struct mlx5_ifc_destroy_scheduling_element_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         scheduling_hierarchy[0x8];
	u8         reserved_at_48[0x18];

	u8         scheduling_element_id[0x20];

	u8         reserved_at_80[0x180];
};

struct mlx5_ifc_destroy_rqt_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_destroy_rqt_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         rqtn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_destroy_rq_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_destroy_rq_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         rqn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_set_delay_drop_params_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x20];

	u8         reserved_at_60[0x10];
	u8         delay_drop_timeout[0x10];
};

struct mlx5_ifc_set_delay_drop_params_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_destroy_rmp_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_destroy_rmp_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         rmpn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_destroy_qp_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_destroy_qp_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         qpn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_destroy_psv_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_destroy_psv_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         psvn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_destroy_mkey_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_destroy_mkey_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         mkey_index[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_destroy_flow_table_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_destroy_flow_table_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         other_vport[0x1];
	u8         reserved_at_41[0xf];
	u8         vport_number[0x10];

	u8         reserved_at_60[0x20];

	u8         table_type[0x8];
	u8         reserved_at_88[0x18];

	u8         reserved_at_a0[0x8];
	u8         table_id[0x18];

	u8         reserved_at_c0[0x140];
};

struct mlx5_ifc_destroy_flow_group_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_destroy_flow_group_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         other_vport[0x1];
	u8         reserved_at_41[0xf];
	u8         vport_number[0x10];

	u8         reserved_at_60[0x20];

	u8         table_type[0x8];
	u8         reserved_at_88[0x18];

	u8         reserved_at_a0[0x8];
	u8         table_id[0x18];

	u8         group_id[0x20];

	u8         reserved_at_e0[0x120];
};

struct mlx5_ifc_destroy_eq_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_destroy_eq_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x18];
	u8         eq_number[0x8];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_destroy_dct_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_destroy_dct_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         dctn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_destroy_cq_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_destroy_cq_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         cqn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_delete_vxlan_udp_dport_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_delete_vxlan_udp_dport_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x20];

	u8         reserved_at_60[0x10];
	u8         vxlan_udp_port[0x10];
};

struct mlx5_ifc_delete_l2_table_entry_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_delete_l2_table_entry_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x60];

	u8         reserved_at_a0[0x8];
	u8         table_index[0x18];

	u8         reserved_at_c0[0x140];
};

struct mlx5_ifc_delete_fte_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_delete_fte_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         other_vport[0x1];
	u8         reserved_at_41[0xf];
	u8         vport_number[0x10];

	u8         reserved_at_60[0x20];

	u8         table_type[0x8];
	u8         reserved_at_88[0x18];

	u8         reserved_at_a0[0x8];
	u8         table_id[0x18];

	u8         reserved_at_c0[0x40];

	u8         flow_index[0x20];

	u8         reserved_at_120[0xe0];
};

struct mlx5_ifc_dealloc_xrcd_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_dealloc_xrcd_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         xrcd[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_dealloc_uar_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_dealloc_uar_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         uar[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_dealloc_transport_domain_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_dealloc_transport_domain_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         transport_domain[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_dealloc_q_counter_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_dealloc_q_counter_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x18];
	u8         counter_set_id[0x8];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_dealloc_pd_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_dealloc_pd_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         pd[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_dealloc_flow_counter_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_dealloc_flow_counter_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         flow_counter_id[0x20];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_create_xrq_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x8];
	u8         xrqn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_create_xrq_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];

	struct mlx5_ifc_xrqc_bits xrq_context;
};

struct mlx5_ifc_create_xrc_srq_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x8];
	u8         xrc_srqn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_create_xrc_srq_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];

	struct mlx5_ifc_xrc_srqc_bits xrc_srq_context_entry;

	u8         reserved_at_280[0x60];

	u8         xrc_srq_umem_valid[0x1];
	u8         reserved_at_2e1[0x1f];

	u8         reserved_at_300[0x580];

	u8         pas[][0x40];
};

struct mlx5_ifc_create_tis_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x8];
	u8         tisn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_create_tis_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0xc0];

	struct mlx5_ifc_tisc_bits ctx;
};

struct mlx5_ifc_create_tir_out_bits {
	u8         status[0x8];
	u8         icm_address_63_40[0x18];

	u8         syndrome[0x20];

	u8         icm_address_39_32[0x8];
	u8         tirn[0x18];

	u8         icm_address_31_0[0x20];
};

struct mlx5_ifc_create_tir_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0xc0];

	struct mlx5_ifc_tirc_bits ctx;
};

struct mlx5_ifc_create_srq_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x8];
	u8         srqn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_create_srq_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];

	struct mlx5_ifc_srqc_bits srq_context_entry;

	u8         reserved_at_280[0x600];

	u8         pas[][0x40];
};

struct mlx5_ifc_create_sq_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x8];
	u8         sqn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_create_sq_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0xc0];

	struct mlx5_ifc_sqc_bits ctx;
};

struct mlx5_ifc_create_scheduling_element_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];

	u8         scheduling_element_id[0x20];

	u8         reserved_at_a0[0x160];
};

struct mlx5_ifc_create_scheduling_element_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         scheduling_hierarchy[0x8];
	u8         reserved_at_48[0x18];

	u8         reserved_at_60[0xa0];

	struct mlx5_ifc_scheduling_context_bits scheduling_context;

	u8         reserved_at_300[0x100];
};

struct mlx5_ifc_create_rqt_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x8];
	u8         rqtn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_create_rqt_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0xc0];

	struct mlx5_ifc_rqtc_bits rqt_context;
};

struct mlx5_ifc_create_rq_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x8];
	u8         rqn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_create_rq_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0xc0];

	struct mlx5_ifc_rqc_bits ctx;
};

struct mlx5_ifc_create_rmp_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x8];
	u8         rmpn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_create_rmp_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0xc0];

	struct mlx5_ifc_rmpc_bits ctx;
};

struct mlx5_ifc_create_qp_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x8];
	u8         qpn[0x18];

	u8         ece[0x20];
};

struct mlx5_ifc_create_qp_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];

	u8         opt_param_mask[0x20];

	u8         ece[0x20];

	struct mlx5_ifc_qpc_bits qpc;

	u8         reserved_at_800[0x60];

	u8         wq_umem_valid[0x1];
	u8         reserved_at_861[0x1f];

	u8         pas[][0x40];
};

struct mlx5_ifc_create_psv_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];

	u8         reserved_at_80[0x8];
	u8         psv0_index[0x18];

	u8         reserved_at_a0[0x8];
	u8         psv1_index[0x18];

	u8         reserved_at_c0[0x8];
	u8         psv2_index[0x18];

	u8         reserved_at_e0[0x8];
	u8         psv3_index[0x18];
};

struct mlx5_ifc_create_psv_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         num_psv[0x4];
	u8         reserved_at_44[0x4];
	u8         pd[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_create_mkey_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x8];
	u8         mkey_index[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_create_mkey_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x20];

	u8         pg_access[0x1];
	u8         mkey_umem_valid[0x1];
	u8         reserved_at_62[0x1e];

	struct mlx5_ifc_mkc_bits memory_key_mkey_entry;

	u8         reserved_at_280[0x80];

	u8         translations_octword_actual_size[0x20];

	u8         reserved_at_320[0x560];

	u8         klm_pas_mtt[][0x20];
};

enum {
	MLX5_FLOW_TABLE_TYPE_NIC_RX		= 0x0,
	MLX5_FLOW_TABLE_TYPE_NIC_TX		= 0x1,
	MLX5_FLOW_TABLE_TYPE_ESW_EGRESS_ACL	= 0x2,
	MLX5_FLOW_TABLE_TYPE_ESW_INGRESS_ACL	= 0x3,
	MLX5_FLOW_TABLE_TYPE_FDB		= 0X4,
	MLX5_FLOW_TABLE_TYPE_SNIFFER_RX		= 0X5,
	MLX5_FLOW_TABLE_TYPE_SNIFFER_TX		= 0X6,
};

struct mlx5_ifc_create_flow_table_out_bits {
	u8         status[0x8];
	u8         icm_address_63_40[0x18];

	u8         syndrome[0x20];

	u8         icm_address_39_32[0x8];
	u8         table_id[0x18];

	u8         icm_address_31_0[0x20];
};

struct mlx5_ifc_create_flow_table_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         other_vport[0x1];
	u8         reserved_at_41[0xf];
	u8         vport_number[0x10];

	u8         reserved_at_60[0x20];

	u8         table_type[0x8];
	u8         reserved_at_88[0x18];

	u8         reserved_at_a0[0x20];

	struct mlx5_ifc_flow_table_context_bits flow_table_context;
};

struct mlx5_ifc_create_flow_group_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x8];
	u8         group_id[0x18];

	u8         reserved_at_60[0x20];
};

enum {
	MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_OUTER_HEADERS     = 0x0,
	MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_MISC_PARAMETERS   = 0x1,
	MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_INNER_HEADERS     = 0x2,
	MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_MISC_PARAMETERS_2 = 0x3,
};

struct mlx5_ifc_create_flow_group_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         other_vport[0x1];
	u8         reserved_at_41[0xf];
	u8         vport_number[0x10];

	u8         reserved_at_60[0x20];

	u8         table_type[0x8];
	u8         reserved_at_88[0x18];

	u8         reserved_at_a0[0x8];
	u8         table_id[0x18];

	u8         source_eswitch_owner_vhca_id_valid[0x1];

	u8         reserved_at_c1[0x1f];

	u8         start_flow_index[0x20];

	u8         reserved_at_100[0x20];

	u8         end_flow_index[0x20];

	u8         reserved_at_140[0xa0];

	u8         reserved_at_1e0[0x18];
	u8         match_criteria_enable[0x8];

	struct mlx5_ifc_fte_match_param_bits match_criteria;

	u8         reserved_at_1200[0xe00];
};

struct mlx5_ifc_create_eq_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x18];
	u8         eq_number[0x8];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_create_eq_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];

	struct mlx5_ifc_eqc_bits eq_context_entry;

	u8         reserved_at_280[0x40];

	u8         event_bitmask[4][0x40];

	u8         reserved_at_3c0[0x4c0];

	u8         pas[][0x40];
};

struct mlx5_ifc_create_dct_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x8];
	u8         dctn[0x18];

	u8         ece[0x20];
};

struct mlx5_ifc_create_dct_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];

	struct mlx5_ifc_dctc_bits dct_context_entry;

	u8         reserved_at_280[0x180];
};

struct mlx5_ifc_create_cq_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x8];
	u8         cqn[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_create_cq_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];

	struct mlx5_ifc_cqc_bits cq_context;

	u8         reserved_at_280[0x60];

	u8         cq_umem_valid[0x1];
	u8         reserved_at_2e1[0x59f];

	u8         pas[][0x40];
};

struct mlx5_ifc_config_int_moderation_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x4];
	u8         min_delay[0xc];
	u8         int_vector[0x10];

	u8         reserved_at_60[0x20];
};

enum {
	MLX5_CONFIG_INT_MODERATION_IN_OP_MOD_WRITE  = 0x0,
	MLX5_CONFIG_INT_MODERATION_IN_OP_MOD_READ   = 0x1,
};

struct mlx5_ifc_config_int_moderation_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x4];
	u8         min_delay[0xc];
	u8         int_vector[0x10];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_attach_to_mcg_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_attach_to_mcg_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         qpn[0x18];

	u8         reserved_at_60[0x20];

	u8         multicast_gid[16][0x8];
};

struct mlx5_ifc_arm_xrq_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_arm_xrq_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         xrqn[0x18];

	u8         reserved_at_60[0x10];
	u8         lwm[0x10];
};

struct mlx5_ifc_arm_xrc_srq_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

enum {
	MLX5_ARM_XRC_SRQ_IN_OP_MOD_XRC_SRQ  = 0x1,
};

struct mlx5_ifc_arm_xrc_srq_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         xrc_srqn[0x18];

	u8         reserved_at_60[0x10];
	u8         lwm[0x10];
};

struct mlx5_ifc_arm_rq_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

enum {
	MLX5_ARM_RQ_IN_OP_MOD_SRQ = 0x1,
	MLX5_ARM_RQ_IN_OP_MOD_XRQ = 0x2,
};

struct mlx5_ifc_arm_rq_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         srq_number[0x18];

	u8         reserved_at_60[0x10];
	u8         lwm[0x10];
};

struct mlx5_ifc_arm_dct_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_arm_dct_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x8];
	u8         dct_number[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_alloc_xrcd_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x8];
	u8         xrcd[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_alloc_xrcd_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_alloc_uar_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x8];
	u8         uar[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_alloc_uar_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_alloc_transport_domain_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x8];
	u8         transport_domain[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_alloc_transport_domain_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_alloc_q_counter_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x18];
	u8         counter_set_id[0x8];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_alloc_q_counter_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_alloc_pd_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x8];
	u8         pd[0x18];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_alloc_pd_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_alloc_flow_counter_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         flow_counter_id[0x20];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_alloc_flow_counter_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x38];
	u8         flow_counter_bulk[0x8];
};

struct mlx5_ifc_add_vxlan_udp_dport_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_add_vxlan_udp_dport_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x20];

	u8         reserved_at_60[0x10];
	u8         vxlan_udp_port[0x10];
};

struct mlx5_ifc_set_pp_rate_limit_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_set_pp_rate_limit_context_bits {
	u8         rate_limit[0x20];

	u8	   burst_upper_bound[0x20];

	u8         reserved_at_40[0x10];
	u8	   typical_packet_size[0x10];

	u8         reserved_at_60[0x120];
};

struct mlx5_ifc_set_pp_rate_limit_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x10];
	u8         rate_limit_index[0x10];

	u8         reserved_at_60[0x20];

	struct mlx5_ifc_set_pp_rate_limit_context_bits ctx;
};

struct mlx5_ifc_access_register_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];

	u8         register_data[][0x20];
};

enum {
	MLX5_ACCESS_REGISTER_IN_OP_MOD_WRITE  = 0x0,
	MLX5_ACCESS_REGISTER_IN_OP_MOD_READ   = 0x1,
};

struct mlx5_ifc_access_register_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x10];
	u8         register_id[0x10];

	u8         argument[0x20];

	u8         register_data[][0x20];
};

struct mlx5_ifc_sltp_reg_bits {
	u8         status[0x4];
	u8         version[0x4];
	u8         local_port[0x8];
	u8         pnat[0x2];
	u8         reserved_at_12[0x2];
	u8         lane[0x4];
	u8         reserved_at_18[0x8];

	u8         reserved_at_20[0x20];

	u8         reserved_at_40[0x7];
	u8         polarity[0x1];
	u8         ob_tap0[0x8];
	u8         ob_tap1[0x8];
	u8         ob_tap2[0x8];

	u8         reserved_at_60[0xc];
	u8         ob_preemp_mode[0x4];
	u8         ob_reg[0x8];
	u8         ob_bias[0x8];

	u8         reserved_at_80[0x20];
};

struct mlx5_ifc_slrg_reg_bits {
	u8         status[0x4];
	u8         version[0x4];
	u8         local_port[0x8];
	u8         pnat[0x2];
	u8         reserved_at_12[0x2];
	u8         lane[0x4];
	u8         reserved_at_18[0x8];

	u8         time_to_link_up[0x10];
	u8         reserved_at_30[0xc];
	u8         grade_lane_speed[0x4];

	u8         grade_version[0x8];
	u8         grade[0x18];

	u8         reserved_at_60[0x4];
	u8         height_grade_type[0x4];
	u8         height_grade[0x18];

	u8         height_dz[0x10];
	u8         height_dv[0x10];

	u8         reserved_at_a0[0x10];
	u8         height_sigma[0x10];

	u8         reserved_at_c0[0x20];

	u8         reserved_at_e0[0x4];
	u8         phase_grade_type[0x4];
	u8         phase_grade[0x18];

	u8         reserved_at_100[0x8];
	u8         phase_eo_pos[0x8];
	u8         reserved_at_110[0x8];
	u8         phase_eo_neg[0x8];

	u8         ffe_set_tested[0x10];
	u8         test_errors_per_lane[0x10];
};

struct mlx5_ifc_pvlc_reg_bits {
	u8         reserved_at_0[0x8];
	u8         local_port[0x8];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x1c];
	u8         vl_hw_cap[0x4];

	u8         reserved_at_40[0x1c];
	u8         vl_admin[0x4];

	u8         reserved_at_60[0x1c];
	u8         vl_operational[0x4];
};

struct mlx5_ifc_pude_reg_bits {
	u8         swid[0x8];
	u8         local_port[0x8];
	u8         reserved_at_10[0x4];
	u8         admin_status[0x4];
	u8         reserved_at_18[0x4];
	u8         oper_status[0x4];

	u8         reserved_at_20[0x60];
};

struct mlx5_ifc_ptys_reg_bits {
	u8         reserved_at_0[0x1];
	u8         an_disable_admin[0x1];
	u8         an_disable_cap[0x1];
	u8         reserved_at_3[0x5];
	u8         local_port[0x8];
	u8         reserved_at_10[0xd];
	u8         proto_mask[0x3];

	u8         an_status[0x4];
	u8         reserved_at_24[0xc];
	u8         data_rate_oper[0x10];

	u8         ext_eth_proto_capability[0x20];

	u8         eth_proto_capability[0x20];

	u8         ib_link_width_capability[0x10];
	u8         ib_proto_capability[0x10];

	u8         ext_eth_proto_admin[0x20];

	u8         eth_proto_admin[0x20];

	u8         ib_link_width_admin[0x10];
	u8         ib_proto_admin[0x10];

	u8         ext_eth_proto_oper[0x20];

	u8         eth_proto_oper[0x20];

	u8         ib_link_width_oper[0x10];
	u8         ib_proto_oper[0x10];

	u8         reserved_at_160[0x1c];
	u8         connector_type[0x4];

	u8         eth_proto_lp_advertise[0x20];

	u8         reserved_at_1a0[0x60];
};

struct mlx5_ifc_mlcr_reg_bits {
	u8         reserved_at_0[0x8];
	u8         local_port[0x8];
	u8         reserved_at_10[0x20];

	u8         beacon_duration[0x10];
	u8         reserved_at_40[0x10];

	u8         beacon_remain[0x10];
};

struct mlx5_ifc_ptas_reg_bits {
	u8         reserved_at_0[0x20];

	u8         algorithm_options[0x10];
	u8         reserved_at_30[0x4];
	u8         repetitions_mode[0x4];
	u8         num_of_repetitions[0x8];

	u8         grade_version[0x8];
	u8         height_grade_type[0x4];
	u8         phase_grade_type[0x4];
	u8         height_grade_weight[0x8];
	u8         phase_grade_weight[0x8];

	u8         gisim_measure_bits[0x10];
	u8         adaptive_tap_measure_bits[0x10];

	u8         ber_bath_high_error_threshold[0x10];
	u8         ber_bath_mid_error_threshold[0x10];

	u8         ber_bath_low_error_threshold[0x10];
	u8         one_ratio_high_threshold[0x10];

	u8         one_ratio_high_mid_threshold[0x10];
	u8         one_ratio_low_mid_threshold[0x10];

	u8         one_ratio_low_threshold[0x10];
	u8         ndeo_error_threshold[0x10];

	u8         mixer_offset_step_size[0x10];
	u8         reserved_at_110[0x8];
	u8         mix90_phase_for_voltage_bath[0x8];

	u8         mixer_offset_start[0x10];
	u8         mixer_offset_end[0x10];

	u8         reserved_at_140[0x15];
	u8         ber_test_time[0xb];
};

struct mlx5_ifc_pspa_reg_bits {
	u8         swid[0x8];
	u8         local_port[0x8];
	u8         sub_port[0x8];
	u8         reserved_at_18[0x8];

	u8         reserved_at_20[0x20];
};

struct mlx5_ifc_pqdr_reg_bits {
	u8         reserved_at_0[0x8];
	u8         local_port[0x8];
	u8         reserved_at_10[0x5];
	u8         prio[0x3];
	u8         reserved_at_18[0x6];
	u8         mode[0x2];

	u8         reserved_at_20[0x20];

	u8         reserved_at_40[0x10];
	u8         min_threshold[0x10];

	u8         reserved_at_60[0x10];
	u8         max_threshold[0x10];

	u8         reserved_at_80[0x10];
	u8         mark_probability_denominator[0x10];

	u8         reserved_at_a0[0x60];
};

struct mlx5_ifc_ppsc_reg_bits {
	u8         reserved_at_0[0x8];
	u8         local_port[0x8];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x60];

	u8         reserved_at_80[0x1c];
	u8         wrps_admin[0x4];

	u8         reserved_at_a0[0x1c];
	u8         wrps_status[0x4];

	u8         reserved_at_c0[0x8];
	u8         up_threshold[0x8];
	u8         reserved_at_d0[0x8];
	u8         down_threshold[0x8];

	u8         reserved_at_e0[0x20];

	u8         reserved_at_100[0x1c];
	u8         srps_admin[0x4];

	u8         reserved_at_120[0x1c];
	u8         srps_status[0x4];

	u8         reserved_at_140[0x40];
};

struct mlx5_ifc_pplr_reg_bits {
	u8         reserved_at_0[0x8];
	u8         local_port[0x8];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x8];
	u8         lb_cap[0x8];
	u8         reserved_at_30[0x8];
	u8         lb_en[0x8];
};

struct mlx5_ifc_pplm_reg_bits {
	u8         reserved_at_0[0x8];
	u8	   local_port[0x8];
	u8	   reserved_at_10[0x10];

	u8	   reserved_at_20[0x20];

	u8	   port_profile_mode[0x8];
	u8	   static_port_profile[0x8];
	u8	   active_port_profile[0x8];
	u8	   reserved_at_58[0x8];

	u8	   retransmission_active[0x8];
	u8	   fec_mode_active[0x18];

	u8	   rs_fec_correction_bypass_cap[0x4];
	u8	   reserved_at_84[0x8];
	u8	   fec_override_cap_56g[0x4];
	u8	   fec_override_cap_100g[0x4];
	u8	   fec_override_cap_50g[0x4];
	u8	   fec_override_cap_25g[0x4];
	u8	   fec_override_cap_10g_40g[0x4];

	u8	   rs_fec_correction_bypass_admin[0x4];
	u8	   reserved_at_a4[0x8];
	u8	   fec_override_admin_56g[0x4];
	u8	   fec_override_admin_100g[0x4];
	u8	   fec_override_admin_50g[0x4];
	u8	   fec_override_admin_25g[0x4];
	u8	   fec_override_admin_10g_40g[0x4];

	u8         fec_override_cap_400g_8x[0x10];
	u8         fec_override_cap_200g_4x[0x10];

	u8         fec_override_cap_100g_2x[0x10];
	u8         fec_override_cap_50g_1x[0x10];

	u8         fec_override_admin_400g_8x[0x10];
	u8         fec_override_admin_200g_4x[0x10];

	u8         fec_override_admin_100g_2x[0x10];
	u8         fec_override_admin_50g_1x[0x10];
};

struct mlx5_ifc_ppcnt_reg_bits {
	u8         swid[0x8];
	u8         local_port[0x8];
	u8         pnat[0x2];
	u8         reserved_at_12[0x8];
	u8         grp[0x6];

	u8         clr[0x1];
	u8         reserved_at_21[0x1c];
	u8         prio_tc[0x3];

	union mlx5_ifc_eth_cntrs_grp_data_layout_auto_bits counter_set;
};

struct mlx5_ifc_mpein_reg_bits {
	u8         reserved_at_0[0x2];
	u8         depth[0x6];
	u8         pcie_index[0x8];
	u8         node[0x8];
	u8         reserved_at_18[0x8];

	u8         capability_mask[0x20];

	u8         reserved_at_40[0x8];
	u8         link_width_enabled[0x8];
	u8         link_speed_enabled[0x10];

	u8         lane0_physical_position[0x8];
	u8         link_width_active[0x8];
	u8         link_speed_active[0x10];

	u8         num_of_pfs[0x10];
	u8         num_of_vfs[0x10];

	u8         bdf0[0x10];
	u8         reserved_at_b0[0x10];

	u8         max_read_request_size[0x4];
	u8         max_payload_size[0x4];
	u8         reserved_at_c8[0x5];
	u8         pwr_status[0x3];
	u8         port_type[0x4];
	u8         reserved_at_d4[0xb];
	u8         lane_reversal[0x1];

	u8         reserved_at_e0[0x14];
	u8         pci_power[0xc];

	u8         reserved_at_100[0x20];

	u8         device_status[0x10];
	u8         port_state[0x8];
	u8         reserved_at_138[0x8];

	u8         reserved_at_140[0x10];
	u8         receiver_detect_result[0x10];

	u8         reserved_at_160[0x20];
};

struct mlx5_ifc_mpcnt_reg_bits {
	u8         reserved_at_0[0x8];
	u8         pcie_index[0x8];
	u8         reserved_at_10[0xa];
	u8         grp[0x6];

	u8         clr[0x1];
	u8         reserved_at_21[0x1f];

	union mlx5_ifc_pcie_cntrs_grp_data_layout_auto_bits counter_set;
};

struct mlx5_ifc_ppad_reg_bits {
	u8         reserved_at_0[0x3];
	u8         single_mac[0x1];
	u8         reserved_at_4[0x4];
	u8         local_port[0x8];
	u8         mac_47_32[0x10];

	u8         mac_31_0[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_pmtu_reg_bits {
	u8         reserved_at_0[0x8];
	u8         local_port[0x8];
	u8         reserved_at_10[0x10];

	u8         max_mtu[0x10];
	u8         reserved_at_30[0x10];

	u8         admin_mtu[0x10];
	u8         reserved_at_50[0x10];

	u8         oper_mtu[0x10];
	u8         reserved_at_70[0x10];
};

struct mlx5_ifc_pmpr_reg_bits {
	u8         reserved_at_0[0x8];
	u8         module[0x8];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x18];
	u8         attenuation_5g[0x8];

	u8         reserved_at_40[0x18];
	u8         attenuation_7g[0x8];

	u8         reserved_at_60[0x18];
	u8         attenuation_12g[0x8];
};

struct mlx5_ifc_pmpe_reg_bits {
	u8         reserved_at_0[0x8];
	u8         module[0x8];
	u8         reserved_at_10[0xc];
	u8         module_status[0x4];

	u8         reserved_at_20[0x60];
};

struct mlx5_ifc_pmpc_reg_bits {
	u8         module_state_updated[32][0x8];
};

struct mlx5_ifc_pmlpn_reg_bits {
	u8         reserved_at_0[0x4];
	u8         mlpn_status[0x4];
	u8         local_port[0x8];
	u8         reserved_at_10[0x10];

	u8         e[0x1];
	u8         reserved_at_21[0x1f];
};

struct mlx5_ifc_pmlp_reg_bits {
	u8         rxtx[0x1];
	u8         reserved_at_1[0x7];
	u8         local_port[0x8];
	u8         reserved_at_10[0x8];
	u8         width[0x8];

	u8         lane0_module_mapping[0x20];

	u8         lane1_module_mapping[0x20];

	u8         lane2_module_mapping[0x20];

	u8         lane3_module_mapping[0x20];

	u8         reserved_at_a0[0x160];
};

struct mlx5_ifc_pmaos_reg_bits {
	u8         reserved_at_0[0x8];
	u8         module[0x8];
	u8         reserved_at_10[0x4];
	u8         admin_status[0x4];
	u8         reserved_at_18[0x4];
	u8         oper_status[0x4];

	u8         ase[0x1];
	u8         ee[0x1];
	u8         reserved_at_22[0x1c];
	u8         e[0x2];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_plpc_reg_bits {
	u8         reserved_at_0[0x4];
	u8         profile_id[0xc];
	u8         reserved_at_10[0x4];
	u8         proto_mask[0x4];
	u8         reserved_at_18[0x8];

	u8         reserved_at_20[0x10];
	u8         lane_speed[0x10];

	u8         reserved_at_40[0x17];
	u8         lpbf[0x1];
	u8         fec_mode_policy[0x8];

	u8         retransmission_capability[0x8];
	u8         fec_mode_capability[0x18];

	u8         retransmission_support_admin[0x8];
	u8         fec_mode_support_admin[0x18];

	u8         retransmission_request_admin[0x8];
	u8         fec_mode_request_admin[0x18];

	u8         reserved_at_c0[0x80];
};

struct mlx5_ifc_plib_reg_bits {
	u8         reserved_at_0[0x8];
	u8         local_port[0x8];
	u8         reserved_at_10[0x8];
	u8         ib_port[0x8];

	u8         reserved_at_20[0x60];
};

struct mlx5_ifc_plbf_reg_bits {
	u8         reserved_at_0[0x8];
	u8         local_port[0x8];
	u8         reserved_at_10[0xd];
	u8         lbf_mode[0x3];

	u8         reserved_at_20[0x20];
};

struct mlx5_ifc_pipg_reg_bits {
	u8         reserved_at_0[0x8];
	u8         local_port[0x8];
	u8         reserved_at_10[0x10];

	u8         dic[0x1];
	u8         reserved_at_21[0x19];
	u8         ipg[0x4];
	u8         reserved_at_3e[0x2];
};

struct mlx5_ifc_pifr_reg_bits {
	u8         reserved_at_0[0x8];
	u8         local_port[0x8];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0xe0];

	u8         port_filter[8][0x20];

	u8         port_filter_update_en[8][0x20];
};

struct mlx5_ifc_pfcc_reg_bits {
	u8         reserved_at_0[0x8];
	u8         local_port[0x8];
	u8         reserved_at_10[0xb];
	u8         ppan_mask_n[0x1];
	u8         minor_stall_mask[0x1];
	u8         critical_stall_mask[0x1];
	u8         reserved_at_1e[0x2];

	u8         ppan[0x4];
	u8         reserved_at_24[0x4];
	u8         prio_mask_tx[0x8];
	u8         reserved_at_30[0x8];
	u8         prio_mask_rx[0x8];

	u8         pptx[0x1];
	u8         aptx[0x1];
	u8         pptx_mask_n[0x1];
	u8         reserved_at_43[0x5];
	u8         pfctx[0x8];
	u8         reserved_at_50[0x10];

	u8         pprx[0x1];
	u8         aprx[0x1];
	u8         pprx_mask_n[0x1];
	u8         reserved_at_63[0x5];
	u8         pfcrx[0x8];
	u8         reserved_at_70[0x10];

	u8         device_stall_minor_watermark[0x10];
	u8         device_stall_critical_watermark[0x10];

	u8         reserved_at_a0[0x60];
};

struct mlx5_ifc_pelc_reg_bits {
	u8         op[0x4];
	u8         reserved_at_4[0x4];
	u8         local_port[0x8];
	u8         reserved_at_10[0x10];

	u8         op_admin[0x8];
	u8         op_capability[0x8];
	u8         op_request[0x8];
	u8         op_active[0x8];

	u8         admin[0x40];

	u8         capability[0x40];

	u8         request[0x40];

	u8         active[0x40];

	u8         reserved_at_140[0x80];
};

struct mlx5_ifc_peir_reg_bits {
	u8         reserved_at_0[0x8];
	u8         local_port[0x8];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0xc];
	u8         error_count[0x4];
	u8         reserved_at_30[0x10];

	u8         reserved_at_40[0xc];
	u8         lane[0x4];
	u8         reserved_at_50[0x8];
	u8         error_type[0x8];
};

struct mlx5_ifc_mpegc_reg_bits {
	u8         reserved_at_0[0x30];
	u8         field_select[0x10];

	u8         tx_overflow_sense[0x1];
	u8         mark_cqe[0x1];
	u8         mark_cnp[0x1];
	u8         reserved_at_43[0x1b];
	u8         tx_lossy_overflow_oper[0x2];

	u8         reserved_at_60[0x100];
};

struct mlx5_ifc_pcam_enhanced_features_bits {
	u8         reserved_at_0[0x68];
	u8         fec_50G_per_lane_in_pplm[0x1];
	u8         reserved_at_69[0x4];
	u8         rx_icrc_encapsulated_counter[0x1];
	u8	   reserved_at_6e[0x4];
	u8         ptys_extended_ethernet[0x1];
	u8	   reserved_at_73[0x3];
	u8         pfcc_mask[0x1];
	u8         reserved_at_77[0x3];
	u8         per_lane_error_counters[0x1];
	u8         rx_buffer_fullness_counters[0x1];
	u8         ptys_connector_type[0x1];
	u8         reserved_at_7d[0x1];
	u8         ppcnt_discard_group[0x1];
	u8         ppcnt_statistical_group[0x1];
};

struct mlx5_ifc_pcam_regs_5000_to_507f_bits {
	u8         port_access_reg_cap_mask_127_to_96[0x20];
	u8         port_access_reg_cap_mask_95_to_64[0x20];

	u8         port_access_reg_cap_mask_63_to_36[0x1c];
	u8         pplm[0x1];
	u8         port_access_reg_cap_mask_34_to_32[0x3];

	u8         port_access_reg_cap_mask_31_to_13[0x13];
	u8         pbmc[0x1];
	u8         pptb[0x1];
	u8         port_access_reg_cap_mask_10_to_09[0x2];
	u8         ppcnt[0x1];
	u8         port_access_reg_cap_mask_07_to_00[0x8];
};

struct mlx5_ifc_pcam_reg_bits {
	u8         reserved_at_0[0x8];
	u8         feature_group[0x8];
	u8         reserved_at_10[0x8];
	u8         access_reg_group[0x8];

	u8         reserved_at_20[0x20];

	union {
		struct mlx5_ifc_pcam_regs_5000_to_507f_bits regs_5000_to_507f;
		u8         reserved_at_0[0x80];
	} port_access_reg_cap_mask;

	u8         reserved_at_c0[0x80];

	union {
		struct mlx5_ifc_pcam_enhanced_features_bits enhanced_features;
		u8         reserved_at_0[0x80];
	} feature_cap_mask;

	u8         reserved_at_1c0[0xc0];
};

struct mlx5_ifc_mcam_enhanced_features_bits {
	u8         reserved_at_0[0x6e];
	u8         pci_status_and_power[0x1];
	u8         reserved_at_6f[0x5];
	u8         mark_tx_action_cnp[0x1];
	u8         mark_tx_action_cqe[0x1];
	u8         dynamic_tx_overflow[0x1];
	u8         reserved_at_77[0x4];
	u8         pcie_outbound_stalled[0x1];
	u8         tx_overflow_buffer_pkt[0x1];
	u8         mtpps_enh_out_per_adj[0x1];
	u8         mtpps_fs[0x1];
	u8         pcie_performance_group[0x1];
};

struct mlx5_ifc_mcam_access_reg_bits {
	u8         reserved_at_0[0x1c];
	u8         mcda[0x1];
	u8         mcc[0x1];
	u8         mcqi[0x1];
	u8         mcqs[0x1];

	u8         regs_95_to_87[0x9];
	u8         mpegc[0x1];
	u8         regs_85_to_68[0x12];
	u8         tracer_registers[0x4];

	u8         regs_63_to_32[0x20];
	u8         regs_31_to_0[0x20];
};

struct mlx5_ifc_mcam_access_reg_bits1 {
	u8         regs_127_to_96[0x20];

	u8         regs_95_to_64[0x20];

	u8         regs_63_to_32[0x20];

	u8         regs_31_to_0[0x20];
};

struct mlx5_ifc_mcam_access_reg_bits2 {
	u8         regs_127_to_99[0x1d];
	u8         mirc[0x1];
	u8         regs_97_to_96[0x2];

	u8         regs_95_to_64[0x20];

	u8         regs_63_to_32[0x20];

	u8         regs_31_to_0[0x20];
};

struct mlx5_ifc_mcam_reg_bits {
	u8         reserved_at_0[0x8];
	u8         feature_group[0x8];
	u8         reserved_at_10[0x8];
	u8         access_reg_group[0x8];

	u8         reserved_at_20[0x20];

	union {
		struct mlx5_ifc_mcam_access_reg_bits access_regs;
		struct mlx5_ifc_mcam_access_reg_bits1 access_regs1;
		struct mlx5_ifc_mcam_access_reg_bits2 access_regs2;
		u8         reserved_at_0[0x80];
	} mng_access_reg_cap_mask;

	u8         reserved_at_c0[0x80];

	union {
		struct mlx5_ifc_mcam_enhanced_features_bits enhanced_features;
		u8         reserved_at_0[0x80];
	} mng_feature_cap_mask;

	u8         reserved_at_1c0[0x80];
};

struct mlx5_ifc_qcam_access_reg_cap_mask {
	u8         qcam_access_reg_cap_mask_127_to_20[0x6C];
	u8         qpdpm[0x1];
	u8         qcam_access_reg_cap_mask_18_to_4[0x0F];
	u8         qdpm[0x1];
	u8         qpts[0x1];
	u8         qcap[0x1];
	u8         qcam_access_reg_cap_mask_0[0x1];
};

struct mlx5_ifc_qcam_qos_feature_cap_mask {
	u8         qcam_qos_feature_cap_mask_127_to_1[0x7F];
	u8         qpts_trust_both[0x1];
};

struct mlx5_ifc_qcam_reg_bits {
	u8         reserved_at_0[0x8];
	u8         feature_group[0x8];
	u8         reserved_at_10[0x8];
	u8         access_reg_group[0x8];
	u8         reserved_at_20[0x20];

	union {
		struct mlx5_ifc_qcam_access_reg_cap_mask reg_cap;
		u8  reserved_at_0[0x80];
	} qos_access_reg_cap_mask;

	u8         reserved_at_c0[0x80];

	union {
		struct mlx5_ifc_qcam_qos_feature_cap_mask feature_cap;
		u8  reserved_at_0[0x80];
	} qos_feature_cap_mask;

	u8         reserved_at_1c0[0x80];
};

struct mlx5_ifc_core_dump_reg_bits {
	u8         reserved_at_0[0x18];
	u8         core_dump_type[0x8];

	u8         reserved_at_20[0x30];
	u8         vhca_id[0x10];

	u8         reserved_at_60[0x8];
	u8         qpn[0x18];
	u8         reserved_at_80[0x180];
};

struct mlx5_ifc_pcap_reg_bits {
	u8         reserved_at_0[0x8];
	u8         local_port[0x8];
	u8         reserved_at_10[0x10];

	u8         port_capability_mask[4][0x20];
};

struct mlx5_ifc_paos_reg_bits {
	u8         swid[0x8];
	u8         local_port[0x8];
	u8         reserved_at_10[0x4];
	u8         admin_status[0x4];
	u8         reserved_at_18[0x4];
	u8         oper_status[0x4];

	u8         ase[0x1];
	u8         ee[0x1];
	u8         reserved_at_22[0x1c];
	u8         e[0x2];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_pamp_reg_bits {
	u8         reserved_at_0[0x8];
	u8         opamp_group[0x8];
	u8         reserved_at_10[0xc];
	u8         opamp_group_type[0x4];

	u8         start_index[0x10];
	u8         reserved_at_30[0x4];
	u8         num_of_indices[0xc];

	u8         index_data[18][0x10];
};

struct mlx5_ifc_pcmr_reg_bits {
	u8         reserved_at_0[0x8];
	u8         local_port[0x8];
	u8         reserved_at_10[0x10];
	u8         entropy_force_cap[0x1];
	u8         entropy_calc_cap[0x1];
	u8         entropy_gre_calc_cap[0x1];
	u8         reserved_at_23[0x1b];
	u8         fcs_cap[0x1];
	u8         reserved_at_3f[0x1];
	u8         entropy_force[0x1];
	u8         entropy_calc[0x1];
	u8         entropy_gre_calc[0x1];
	u8         reserved_at_43[0x1b];
	u8         fcs_chk[0x1];
	u8         reserved_at_5f[0x1];
};

struct mlx5_ifc_lane_2_module_mapping_bits {
	u8         reserved_at_0[0x6];
	u8         rx_lane[0x2];
	u8         reserved_at_8[0x6];
	u8         tx_lane[0x2];
	u8         reserved_at_10[0x8];
	u8         module[0x8];
};

struct mlx5_ifc_bufferx_reg_bits {
	u8         reserved_at_0[0x6];
	u8         lossy[0x1];
	u8         epsb[0x1];
	u8         reserved_at_8[0xc];
	u8         size[0xc];

	u8         xoff_threshold[0x10];
	u8         xon_threshold[0x10];
};

struct mlx5_ifc_set_node_in_bits {
	u8         node_description[64][0x8];
};

struct mlx5_ifc_register_power_settings_bits {
	u8         reserved_at_0[0x18];
	u8         power_settings_level[0x8];

	u8         reserved_at_20[0x60];
};

struct mlx5_ifc_register_host_endianness_bits {
	u8         he[0x1];
	u8         reserved_at_1[0x1f];

	u8         reserved_at_20[0x60];
};

struct mlx5_ifc_umr_pointer_desc_argument_bits {
	u8         reserved_at_0[0x20];

	u8         mkey[0x20];

	u8         addressh_63_32[0x20];

	u8         addressl_31_0[0x20];
};

struct mlx5_ifc_ud_adrs_vector_bits {
	u8         dc_key[0x40];

	u8         ext[0x1];
	u8         reserved_at_41[0x7];
	u8         destination_qp_dct[0x18];

	u8         static_rate[0x4];
	u8         sl_eth_prio[0x4];
	u8         fl[0x1];
	u8         mlid[0x7];
	u8         rlid_udp_sport[0x10];

	u8         reserved_at_80[0x20];

	u8         rmac_47_16[0x20];

	u8         rmac_15_0[0x10];
	u8         tclass[0x8];
	u8         hop_limit[0x8];

	u8         reserved_at_e0[0x1];
	u8         grh[0x1];
	u8         reserved_at_e2[0x2];
	u8         src_addr_index[0x8];
	u8         flow_label[0x14];

	u8         rgid_rip[16][0x8];
};

struct mlx5_ifc_pages_req_event_bits {
	u8         reserved_at_0[0x10];
	u8         function_id[0x10];

	u8         num_pages[0x20];

	u8         reserved_at_40[0xa0];
};

struct mlx5_ifc_eqe_bits {
	u8         reserved_at_0[0x8];
	u8         event_type[0x8];
	u8         reserved_at_10[0x8];
	u8         event_sub_type[0x8];

	u8         reserved_at_20[0xe0];

	union mlx5_ifc_event_auto_bits event_data;

	u8         reserved_at_1e0[0x10];
	u8         signature[0x8];
	u8         reserved_at_1f8[0x7];
	u8         owner[0x1];
};

enum {
	MLX5_CMD_QUEUE_ENTRY_TYPE_PCIE_CMD_IF_TRANSPORT  = 0x7,
};

struct mlx5_ifc_cmd_queue_entry_bits {
	u8         type[0x8];
	u8         reserved_at_8[0x18];

	u8         input_length[0x20];

	u8         input_mailbox_pointer_63_32[0x20];

	u8         input_mailbox_pointer_31_9[0x17];
	u8         reserved_at_77[0x9];

	u8         command_input_inline_data[16][0x8];

	u8         command_output_inline_data[16][0x8];

	u8         output_mailbox_pointer_63_32[0x20];

	u8         output_mailbox_pointer_31_9[0x17];
	u8         reserved_at_1b7[0x9];

	u8         output_length[0x20];

	u8         token[0x8];
	u8         signature[0x8];
	u8         reserved_at_1f0[0x8];
	u8         status[0x7];
	u8         ownership[0x1];
};

struct mlx5_ifc_cmd_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         command_output[0x20];
};

struct mlx5_ifc_cmd_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         command[][0x20];
};

struct mlx5_ifc_cmd_if_box_bits {
	u8         mailbox_data[512][0x8];

	u8         reserved_at_1000[0x180];

	u8         next_pointer_63_32[0x20];

	u8         next_pointer_31_10[0x16];
	u8         reserved_at_11b6[0xa];

	u8         block_number[0x20];

	u8         reserved_at_11e0[0x8];
	u8         token[0x8];
	u8         ctrl_signature[0x8];
	u8         signature[0x8];
};

struct mlx5_ifc_mtt_bits {
	u8         ptag_63_32[0x20];

	u8         ptag_31_8[0x18];
	u8         reserved_at_38[0x6];
	u8         wr_en[0x1];
	u8         rd_en[0x1];
};

struct mlx5_ifc_query_wol_rol_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x10];
	u8         rol_mode[0x8];
	u8         wol_mode[0x8];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_query_wol_rol_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_set_wol_rol_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_set_wol_rol_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         rol_mode_valid[0x1];
	u8         wol_mode_valid[0x1];
	u8         reserved_at_42[0xe];
	u8         rol_mode[0x8];
	u8         wol_mode[0x8];

	u8         reserved_at_60[0x20];
};

enum {
	MLX5_INITIAL_SEG_NIC_INTERFACE_FULL_DRIVER  = 0x0,
	MLX5_INITIAL_SEG_NIC_INTERFACE_DISABLED     = 0x1,
	MLX5_INITIAL_SEG_NIC_INTERFACE_NO_DRAM_NIC  = 0x2,
};

enum {
	MLX5_INITIAL_SEG_NIC_INTERFACE_SUPPORTED_FULL_DRIVER  = 0x0,
	MLX5_INITIAL_SEG_NIC_INTERFACE_SUPPORTED_DISABLED     = 0x1,
	MLX5_INITIAL_SEG_NIC_INTERFACE_SUPPORTED_NO_DRAM_NIC  = 0x2,
};

enum {
	MLX5_INITIAL_SEG_HEALTH_SYNDROME_FW_INTERNAL_ERR              = 0x1,
	MLX5_INITIAL_SEG_HEALTH_SYNDROME_DEAD_IRISC                   = 0x7,
	MLX5_INITIAL_SEG_HEALTH_SYNDROME_HW_FATAL_ERR                 = 0x8,
	MLX5_INITIAL_SEG_HEALTH_SYNDROME_FW_CRC_ERR                   = 0x9,
	MLX5_INITIAL_SEG_HEALTH_SYNDROME_ICM_FETCH_PCI_ERR            = 0xa,
	MLX5_INITIAL_SEG_HEALTH_SYNDROME_ICM_PAGE_ERR                 = 0xb,
	MLX5_INITIAL_SEG_HEALTH_SYNDROME_ASYNCHRONOUS_EQ_BUF_OVERRUN  = 0xc,
	MLX5_INITIAL_SEG_HEALTH_SYNDROME_EQ_IN_ERR                    = 0xd,
	MLX5_INITIAL_SEG_HEALTH_SYNDROME_EQ_INV                       = 0xe,
	MLX5_INITIAL_SEG_HEALTH_SYNDROME_FFSER_ERR                    = 0xf,
	MLX5_INITIAL_SEG_HEALTH_SYNDROME_HIGH_TEMP_ERR                = 0x10,
};

struct mlx5_ifc_initial_seg_bits {
	u8         fw_rev_minor[0x10];
	u8         fw_rev_major[0x10];

	u8         cmd_interface_rev[0x10];
	u8         fw_rev_subminor[0x10];

	u8         reserved_at_40[0x40];

	u8         cmdq_phy_addr_63_32[0x20];

	u8         cmdq_phy_addr_31_12[0x14];
	u8         reserved_at_b4[0x2];
	u8         nic_interface[0x2];
	u8         log_cmdq_size[0x4];
	u8         log_cmdq_stride[0x4];

	u8         command_doorbell_vector[0x20];

	u8         reserved_at_e0[0xf00];

	u8         initializing[0x1];
	u8         reserved_at_fe1[0x4];
	u8         nic_interface_supported[0x3];
	u8         embedded_cpu[0x1];
	u8         reserved_at_fe9[0x17];

	struct mlx5_ifc_health_buffer_bits health_buffer;

	u8         no_dram_nic_offset[0x20];

	u8         reserved_at_1220[0x6e40];

	u8         reserved_at_8060[0x1f];
	u8         clear_int[0x1];

	u8         health_syndrome[0x8];
	u8         health_counter[0x18];

	u8         reserved_at_80a0[0x17fc0];
};

struct mlx5_ifc_mtpps_reg_bits {
	u8         reserved_at_0[0xc];
	u8         cap_number_of_pps_pins[0x4];
	u8         reserved_at_10[0x4];
	u8         cap_max_num_of_pps_in_pins[0x4];
	u8         reserved_at_18[0x4];
	u8         cap_max_num_of_pps_out_pins[0x4];

	u8         reserved_at_20[0x24];
	u8         cap_pin_3_mode[0x4];
	u8         reserved_at_48[0x4];
	u8         cap_pin_2_mode[0x4];
	u8         reserved_at_50[0x4];
	u8         cap_pin_1_mode[0x4];
	u8         reserved_at_58[0x4];
	u8         cap_pin_0_mode[0x4];

	u8         reserved_at_60[0x4];
	u8         cap_pin_7_mode[0x4];
	u8         reserved_at_68[0x4];
	u8         cap_pin_6_mode[0x4];
	u8         reserved_at_70[0x4];
	u8         cap_pin_5_mode[0x4];
	u8         reserved_at_78[0x4];
	u8         cap_pin_4_mode[0x4];

	u8         field_select[0x20];
	u8         reserved_at_a0[0x60];

	u8         enable[0x1];
	u8         reserved_at_101[0xb];
	u8         pattern[0x4];
	u8         reserved_at_110[0x4];
	u8         pin_mode[0x4];
	u8         pin[0x8];

	u8         reserved_at_120[0x20];

	u8         time_stamp[0x40];

	u8         out_pulse_duration[0x10];
	u8         out_periodic_adjustment[0x10];
	u8         enhanced_out_periodic_adjustment[0x20];

	u8         reserved_at_1c0[0x20];
};

struct mlx5_ifc_mtppse_reg_bits {
	u8         reserved_at_0[0x18];
	u8         pin[0x8];
	u8         event_arm[0x1];
	u8         reserved_at_21[0x1b];
	u8         event_generation_mode[0x4];
	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_mcqs_reg_bits {
	u8         last_index_flag[0x1];
	u8         reserved_at_1[0x7];
	u8         fw_device[0x8];
	u8         component_index[0x10];

	u8         reserved_at_20[0x10];
	u8         identifier[0x10];

	u8         reserved_at_40[0x17];
	u8         component_status[0x5];
	u8         component_update_state[0x4];

	u8         last_update_state_changer_type[0x4];
	u8         last_update_state_changer_host_id[0x4];
	u8         reserved_at_68[0x18];
};

struct mlx5_ifc_mcqi_cap_bits {
	u8         supported_info_bitmask[0x20];

	u8         component_size[0x20];

	u8         max_component_size[0x20];

	u8         log_mcda_word_size[0x4];
	u8         reserved_at_64[0xc];
	u8         mcda_max_write_size[0x10];

	u8         rd_en[0x1];
	u8         reserved_at_81[0x1];
	u8         match_chip_id[0x1];
	u8         match_psid[0x1];
	u8         check_user_timestamp[0x1];
	u8         match_base_guid_mac[0x1];
	u8         reserved_at_86[0x1a];
};

struct mlx5_ifc_mcqi_version_bits {
	u8         reserved_at_0[0x2];
	u8         build_time_valid[0x1];
	u8         user_defined_time_valid[0x1];
	u8         reserved_at_4[0x14];
	u8         version_string_length[0x8];

	u8         version[0x20];

	u8         build_time[0x40];

	u8         user_defined_time[0x40];

	u8         build_tool_version[0x20];

	u8         reserved_at_e0[0x20];

	u8         version_string[92][0x8];
};

struct mlx5_ifc_mcqi_activation_method_bits {
	u8         pending_server_ac_power_cycle[0x1];
	u8         pending_server_dc_power_cycle[0x1];
	u8         pending_server_reboot[0x1];
	u8         pending_fw_reset[0x1];
	u8         auto_activate[0x1];
	u8         all_hosts_sync[0x1];
	u8         device_hw_reset[0x1];
	u8         reserved_at_7[0x19];
};

union mlx5_ifc_mcqi_reg_data_bits {
	struct mlx5_ifc_mcqi_cap_bits               mcqi_caps;
	struct mlx5_ifc_mcqi_version_bits           mcqi_version;
	struct mlx5_ifc_mcqi_activation_method_bits mcqi_activation_mathod;
};

struct mlx5_ifc_mcqi_reg_bits {
	u8         read_pending_component[0x1];
	u8         reserved_at_1[0xf];
	u8         component_index[0x10];

	u8         reserved_at_20[0x20];

	u8         reserved_at_40[0x1b];
	u8         info_type[0x5];

	u8         info_size[0x20];

	u8         offset[0x20];

	u8         reserved_at_a0[0x10];
	u8         data_size[0x10];

	union mlx5_ifc_mcqi_reg_data_bits data[];
};

struct mlx5_ifc_mcc_reg_bits {
	u8         reserved_at_0[0x4];
	u8         time_elapsed_since_last_cmd[0xc];
	u8         reserved_at_10[0x8];
	u8         instruction[0x8];

	u8         reserved_at_20[0x10];
	u8         component_index[0x10];

	u8         reserved_at_40[0x8];
	u8         update_handle[0x18];

	u8         handle_owner_type[0x4];
	u8         handle_owner_host_id[0x4];
	u8         reserved_at_68[0x1];
	u8         control_progress[0x7];
	u8         error_code[0x8];
	u8         reserved_at_78[0x4];
	u8         control_state[0x4];

	u8         component_size[0x20];

	u8         reserved_at_a0[0x60];
};

struct mlx5_ifc_mcda_reg_bits {
	u8         reserved_at_0[0x8];
	u8         update_handle[0x18];

	u8         offset[0x20];

	u8         reserved_at_40[0x10];
	u8         size[0x10];

	u8         reserved_at_60[0x20];

	u8         data[0][0x20];
};

enum {
	MLX5_MFRL_REG_RESET_TYPE_FULL_CHIP = BIT(0),
	MLX5_MFRL_REG_RESET_TYPE_NET_PORT_ALIVE = BIT(1),
};

enum {
	MLX5_MFRL_REG_RESET_LEVEL0 = BIT(0),
	MLX5_MFRL_REG_RESET_LEVEL3 = BIT(3),
	MLX5_MFRL_REG_RESET_LEVEL6 = BIT(6),
};

struct mlx5_ifc_mfrl_reg_bits {
	u8         reserved_at_0[0x20];

	u8         reserved_at_20[0x2];
	u8         pci_sync_for_fw_update_start[0x1];
	u8         pci_sync_for_fw_update_resp[0x2];
	u8         rst_type_sel[0x3];
	u8         reserved_at_28[0x8];
	u8         reset_type[0x8];
	u8         reset_level[0x8];
};

struct mlx5_ifc_mirc_reg_bits {
	u8         reserved_at_0[0x18];
	u8         status_code[0x8];

	u8         reserved_at_20[0x20];
};

union mlx5_ifc_ports_control_registers_document_bits {
	struct mlx5_ifc_bufferx_reg_bits bufferx_reg;
	struct mlx5_ifc_eth_2819_cntrs_grp_data_layout_bits eth_2819_cntrs_grp_data_layout;
	struct mlx5_ifc_eth_2863_cntrs_grp_data_layout_bits eth_2863_cntrs_grp_data_layout;
	struct mlx5_ifc_eth_3635_cntrs_grp_data_layout_bits eth_3635_cntrs_grp_data_layout;
	struct mlx5_ifc_eth_802_3_cntrs_grp_data_layout_bits eth_802_3_cntrs_grp_data_layout;
	struct mlx5_ifc_eth_extended_cntrs_grp_data_layout_bits eth_extended_cntrs_grp_data_layout;
	struct mlx5_ifc_eth_per_prio_grp_data_layout_bits eth_per_prio_grp_data_layout;
	struct mlx5_ifc_eth_per_tc_prio_grp_data_layout_bits eth_per_tc_prio_grp_data_layout;
	struct mlx5_ifc_eth_per_tc_congest_prio_grp_data_layout_bits eth_per_tc_congest_prio_grp_data_layout;
	struct mlx5_ifc_lane_2_module_mapping_bits lane_2_module_mapping;
	struct mlx5_ifc_pamp_reg_bits pamp_reg;
	struct mlx5_ifc_paos_reg_bits paos_reg;
	struct mlx5_ifc_pcap_reg_bits pcap_reg;
	struct mlx5_ifc_peir_reg_bits peir_reg;
	struct mlx5_ifc_pelc_reg_bits pelc_reg;
	struct mlx5_ifc_pfcc_reg_bits pfcc_reg;
	struct mlx5_ifc_ib_port_cntrs_grp_data_layout_bits ib_port_cntrs_grp_data_layout;
	struct mlx5_ifc_phys_layer_cntrs_bits phys_layer_cntrs;
	struct mlx5_ifc_pifr_reg_bits pifr_reg;
	struct mlx5_ifc_pipg_reg_bits pipg_reg;
	struct mlx5_ifc_plbf_reg_bits plbf_reg;
	struct mlx5_ifc_plib_reg_bits plib_reg;
	struct mlx5_ifc_plpc_reg_bits plpc_reg;
	struct mlx5_ifc_pmaos_reg_bits pmaos_reg;
	struct mlx5_ifc_pmlp_reg_bits pmlp_reg;
	struct mlx5_ifc_pmlpn_reg_bits pmlpn_reg;
	struct mlx5_ifc_pmpc_reg_bits pmpc_reg;
	struct mlx5_ifc_pmpe_reg_bits pmpe_reg;
	struct mlx5_ifc_pmpr_reg_bits pmpr_reg;
	struct mlx5_ifc_pmtu_reg_bits pmtu_reg;
	struct mlx5_ifc_ppad_reg_bits ppad_reg;
	struct mlx5_ifc_ppcnt_reg_bits ppcnt_reg;
	struct mlx5_ifc_mpein_reg_bits mpein_reg;
	struct mlx5_ifc_mpcnt_reg_bits mpcnt_reg;
	struct mlx5_ifc_pplm_reg_bits pplm_reg;
	struct mlx5_ifc_pplr_reg_bits pplr_reg;
	struct mlx5_ifc_ppsc_reg_bits ppsc_reg;
	struct mlx5_ifc_pqdr_reg_bits pqdr_reg;
	struct mlx5_ifc_pspa_reg_bits pspa_reg;
	struct mlx5_ifc_ptas_reg_bits ptas_reg;
	struct mlx5_ifc_ptys_reg_bits ptys_reg;
	struct mlx5_ifc_mlcr_reg_bits mlcr_reg;
	struct mlx5_ifc_pude_reg_bits pude_reg;
	struct mlx5_ifc_pvlc_reg_bits pvlc_reg;
	struct mlx5_ifc_slrg_reg_bits slrg_reg;
	struct mlx5_ifc_sltp_reg_bits sltp_reg;
	struct mlx5_ifc_mtpps_reg_bits mtpps_reg;
	struct mlx5_ifc_mtppse_reg_bits mtppse_reg;
	struct mlx5_ifc_fpga_access_reg_bits fpga_access_reg;
	struct mlx5_ifc_fpga_ctrl_bits fpga_ctrl_bits;
	struct mlx5_ifc_fpga_cap_bits fpga_cap_bits;
	struct mlx5_ifc_mcqi_reg_bits mcqi_reg;
	struct mlx5_ifc_mcc_reg_bits mcc_reg;
	struct mlx5_ifc_mcda_reg_bits mcda_reg;
	struct mlx5_ifc_mirc_reg_bits mirc_reg;
	struct mlx5_ifc_mfrl_reg_bits mfrl_reg;
	u8         reserved_at_0[0x60e0];
};

union mlx5_ifc_debug_enhancements_document_bits {
	struct mlx5_ifc_health_buffer_bits health_buffer;
	u8         reserved_at_0[0x200];
};

union mlx5_ifc_uplink_pci_interface_document_bits {
	struct mlx5_ifc_initial_seg_bits initial_seg;
	u8         reserved_at_0[0x20060];
};

struct mlx5_ifc_set_flow_table_root_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_set_flow_table_root_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         other_vport[0x1];
	u8         reserved_at_41[0xf];
	u8         vport_number[0x10];

	u8         reserved_at_60[0x20];

	u8         table_type[0x8];
	u8         reserved_at_88[0x18];

	u8         reserved_at_a0[0x8];
	u8         table_id[0x18];

	u8         reserved_at_c0[0x8];
	u8         underlay_qpn[0x18];
	u8         reserved_at_e0[0x120];
};

enum {
	MLX5_MODIFY_FLOW_TABLE_MISS_TABLE_ID     = (1UL << 0),
	MLX5_MODIFY_FLOW_TABLE_LAG_NEXT_TABLE_ID = (1UL << 15),
};

struct mlx5_ifc_modify_flow_table_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_modify_flow_table_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         other_vport[0x1];
	u8         reserved_at_41[0xf];
	u8         vport_number[0x10];

	u8         reserved_at_60[0x10];
	u8         modify_field_select[0x10];

	u8         table_type[0x8];
	u8         reserved_at_88[0x18];

	u8         reserved_at_a0[0x8];
	u8         table_id[0x18];

	struct mlx5_ifc_flow_table_context_bits flow_table_context;
};

struct mlx5_ifc_ets_tcn_config_reg_bits {
	u8         g[0x1];
	u8         b[0x1];
	u8         r[0x1];
	u8         reserved_at_3[0x9];
	u8         group[0x4];
	u8         reserved_at_10[0x9];
	u8         bw_allocation[0x7];

	u8         reserved_at_20[0xc];
	u8         max_bw_units[0x4];
	u8         reserved_at_30[0x8];
	u8         max_bw_value[0x8];
};

struct mlx5_ifc_ets_global_config_reg_bits {
	u8         reserved_at_0[0x2];
	u8         r[0x1];
	u8         reserved_at_3[0x1d];

	u8         reserved_at_20[0xc];
	u8         max_bw_units[0x4];
	u8         reserved_at_30[0x8];
	u8         max_bw_value[0x8];
};

struct mlx5_ifc_qetc_reg_bits {
	u8                                         reserved_at_0[0x8];
	u8                                         port_number[0x8];
	u8                                         reserved_at_10[0x30];

	struct mlx5_ifc_ets_tcn_config_reg_bits    tc_configuration[0x8];
	struct mlx5_ifc_ets_global_config_reg_bits global_configuration;
};

struct mlx5_ifc_qpdpm_dscp_reg_bits {
	u8         e[0x1];
	u8         reserved_at_01[0x0b];
	u8         prio[0x04];
};

struct mlx5_ifc_qpdpm_reg_bits {
	u8                                     reserved_at_0[0x8];
	u8                                     local_port[0x8];
	u8                                     reserved_at_10[0x10];
	struct mlx5_ifc_qpdpm_dscp_reg_bits    dscp[64];
};

struct mlx5_ifc_qpts_reg_bits {
	u8         reserved_at_0[0x8];
	u8         local_port[0x8];
	u8         reserved_at_10[0x2d];
	u8         trust_state[0x3];
};

struct mlx5_ifc_pptb_reg_bits {
	u8         reserved_at_0[0x2];
	u8         mm[0x2];
	u8         reserved_at_4[0x4];
	u8         local_port[0x8];
	u8         reserved_at_10[0x6];
	u8         cm[0x1];
	u8         um[0x1];
	u8         pm[0x8];

	u8         prio_x_buff[0x20];

	u8         pm_msb[0x8];
	u8         reserved_at_48[0x10];
	u8         ctrl_buff[0x4];
	u8         untagged_buff[0x4];
};

struct mlx5_ifc_pbmc_reg_bits {
	u8         reserved_at_0[0x8];
	u8         local_port[0x8];
	u8         reserved_at_10[0x10];

	u8         xoff_timer_value[0x10];
	u8         xoff_refresh[0x10];

	u8         reserved_at_40[0x9];
	u8         fullness_threshold[0x7];
	u8         port_buffer_size[0x10];

	struct mlx5_ifc_bufferx_reg_bits buffer[10];

	u8         reserved_at_2e0[0x40];
};

struct mlx5_ifc_qtct_reg_bits {
	u8         reserved_at_0[0x8];
	u8         port_number[0x8];
	u8         reserved_at_10[0xd];
	u8         prio[0x3];

	u8         reserved_at_20[0x1d];
	u8         tclass[0x3];
};

struct mlx5_ifc_mcia_reg_bits {
	u8         l[0x1];
	u8         reserved_at_1[0x7];
	u8         module[0x8];
	u8         reserved_at_10[0x8];
	u8         status[0x8];

	u8         i2c_device_address[0x8];
	u8         page_number[0x8];
	u8         device_address[0x10];

	u8         reserved_at_40[0x10];
	u8         size[0x10];

	u8         reserved_at_60[0x20];

	u8         dword_0[0x20];
	u8         dword_1[0x20];
	u8         dword_2[0x20];
	u8         dword_3[0x20];
	u8         dword_4[0x20];
	u8         dword_5[0x20];
	u8         dword_6[0x20];
	u8         dword_7[0x20];
	u8         dword_8[0x20];
	u8         dword_9[0x20];
	u8         dword_10[0x20];
	u8         dword_11[0x20];
};

struct mlx5_ifc_dcbx_param_bits {
	u8         dcbx_cee_cap[0x1];
	u8         dcbx_ieee_cap[0x1];
	u8         dcbx_standby_cap[0x1];
	u8         reserved_at_3[0x5];
	u8         port_number[0x8];
	u8         reserved_at_10[0xa];
	u8         max_application_table_size[6];
	u8         reserved_at_20[0x15];
	u8         version_oper[0x3];
	u8         reserved_at_38[5];
	u8         version_admin[0x3];
	u8         willing_admin[0x1];
	u8         reserved_at_41[0x3];
	u8         pfc_cap_oper[0x4];
	u8         reserved_at_48[0x4];
	u8         pfc_cap_admin[0x4];
	u8         reserved_at_50[0x4];
	u8         num_of_tc_oper[0x4];
	u8         reserved_at_58[0x4];
	u8         num_of_tc_admin[0x4];
	u8         remote_willing[0x1];
	u8         reserved_at_61[3];
	u8         remote_pfc_cap[4];
	u8         reserved_at_68[0x14];
	u8         remote_num_of_tc[0x4];
	u8         reserved_at_80[0x18];
	u8         error[0x8];
	u8         reserved_at_a0[0x160];
};

struct mlx5_ifc_lagc_bits {
	u8         reserved_at_0[0x1d];
	u8         lag_state[0x3];

	u8         reserved_at_20[0x14];
	u8         tx_remap_affinity_2[0x4];
	u8         reserved_at_38[0x4];
	u8         tx_remap_affinity_1[0x4];
};

struct mlx5_ifc_create_lag_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_create_lag_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	struct mlx5_ifc_lagc_bits ctx;
};

struct mlx5_ifc_modify_lag_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_modify_lag_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x20];
	u8         field_select[0x20];

	struct mlx5_ifc_lagc_bits ctx;
};

struct mlx5_ifc_query_lag_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	struct mlx5_ifc_lagc_bits ctx;
};

struct mlx5_ifc_query_lag_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_destroy_lag_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_destroy_lag_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_create_vport_lag_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_create_vport_lag_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_destroy_vport_lag_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_destroy_vport_lag_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_alloc_memic_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_30[0x20];

	u8	   reserved_at_40[0x18];
	u8	   log_memic_addr_alignment[0x8];

	u8         range_start_addr[0x40];

	u8         range_size[0x20];

	u8         memic_size[0x20];
};

struct mlx5_ifc_alloc_memic_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         memic_start_addr[0x40];
};

struct mlx5_ifc_dealloc_memic_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];

	u8         memic_start_addr[0x40];

	u8         memic_size[0x20];

	u8         reserved_at_e0[0x20];
};

struct mlx5_ifc_dealloc_memic_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_general_obj_in_cmd_hdr_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         vhca_tunnel_id[0x10];
	u8         obj_type[0x10];

	u8         obj_id[0x20];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_general_obj_out_cmd_hdr_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         obj_id[0x20];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_umem_bits {
	u8         reserved_at_0[0x80];

	u8         reserved_at_80[0x1b];
	u8         log_page_size[0x5];

	u8         page_offset[0x20];

	u8         num_of_mtt[0x40];

	struct mlx5_ifc_mtt_bits  mtt[];
};

struct mlx5_ifc_uctx_bits {
	u8         cap[0x20];

	u8         reserved_at_20[0x160];
};

struct mlx5_ifc_sw_icm_bits {
	u8         modify_field_select[0x40];

	u8	   reserved_at_40[0x18];
	u8         log_sw_icm_size[0x8];

	u8         reserved_at_60[0x20];

	u8         sw_icm_start_addr[0x40];

	u8         reserved_at_c0[0x140];
};

struct mlx5_ifc_geneve_tlv_option_bits {
	u8         modify_field_select[0x40];

	u8         reserved_at_40[0x18];
	u8         geneve_option_fte_index[0x8];

	u8         option_class[0x10];
	u8         option_type[0x8];
	u8         reserved_at_78[0x3];
	u8         option_data_length[0x5];

	u8         reserved_at_80[0x180];
};

struct mlx5_ifc_create_umem_in_bits {
	u8         opcode[0x10];
	u8         uid[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];

	struct mlx5_ifc_umem_bits  umem;
};

struct mlx5_ifc_create_uctx_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];

	struct mlx5_ifc_uctx_bits  uctx;
};

struct mlx5_ifc_destroy_uctx_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x10];
	u8         uid[0x10];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_create_sw_icm_in_bits {
	struct mlx5_ifc_general_obj_in_cmd_hdr_bits   hdr;
	struct mlx5_ifc_sw_icm_bits		      sw_icm;
};

struct mlx5_ifc_create_geneve_tlv_option_in_bits {
	struct mlx5_ifc_general_obj_in_cmd_hdr_bits   hdr;
	struct mlx5_ifc_geneve_tlv_option_bits        geneve_tlv_opt;
};

struct mlx5_ifc_mtrc_string_db_param_bits {
	u8         string_db_base_address[0x20];

	u8         reserved_at_20[0x8];
	u8         string_db_size[0x18];
};

struct mlx5_ifc_mtrc_cap_bits {
	u8         trace_owner[0x1];
	u8         trace_to_memory[0x1];
	u8         reserved_at_2[0x4];
	u8         trc_ver[0x2];
	u8         reserved_at_8[0x14];
	u8         num_string_db[0x4];

	u8         first_string_trace[0x8];
	u8         num_string_trace[0x8];
	u8         reserved_at_30[0x28];

	u8         log_max_trace_buffer_size[0x8];

	u8         reserved_at_60[0x20];

	struct mlx5_ifc_mtrc_string_db_param_bits string_db_param[8];

	u8         reserved_at_280[0x180];
};

struct mlx5_ifc_mtrc_conf_bits {
	u8         reserved_at_0[0x1c];
	u8         trace_mode[0x4];
	u8         reserved_at_20[0x18];
	u8         log_trace_buffer_size[0x8];
	u8         trace_mkey[0x20];
	u8         reserved_at_60[0x3a0];
};

struct mlx5_ifc_mtrc_stdb_bits {
	u8         string_db_index[0x4];
	u8         reserved_at_4[0x4];
	u8         read_size[0x18];
	u8         start_offset[0x20];
	u8         string_db_data[];
};

struct mlx5_ifc_mtrc_ctrl_bits {
	u8         trace_status[0x2];
	u8         reserved_at_2[0x2];
	u8         arm_event[0x1];
	u8         reserved_at_5[0xb];
	u8         modify_field_select[0x10];
	u8         reserved_at_20[0x2b];
	u8         current_timestamp52_32[0x15];
	u8         current_timestamp31_0[0x20];
	u8         reserved_at_80[0x180];
};

struct mlx5_ifc_host_params_context_bits {
	u8         host_number[0x8];
	u8         reserved_at_8[0x7];
	u8         host_pf_disabled[0x1];
	u8         host_num_of_vfs[0x10];

	u8         host_total_vfs[0x10];
	u8         host_pci_bus[0x10];

	u8         reserved_at_40[0x10];
	u8         host_pci_device[0x10];

	u8         reserved_at_60[0x10];
	u8         host_pci_function[0x10];

	u8         reserved_at_80[0x180];
};

struct mlx5_ifc_query_esw_functions_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_query_esw_functions_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];

	struct mlx5_ifc_host_params_context_bits host_params_context;

	u8         reserved_at_280[0x180];
	u8         host_sf_enable[][0x40];
};

struct mlx5_ifc_sf_partition_bits {
	u8         reserved_at_0[0x10];
	u8         log_num_sf[0x8];
	u8         log_sf_bar_size[0x8];
};

struct mlx5_ifc_query_sf_partitions_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x18];
	u8         num_sf_partitions[0x8];

	u8         reserved_at_60[0x20];

	struct mlx5_ifc_sf_partition_bits sf_partition[];
};

struct mlx5_ifc_query_sf_partitions_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_dealloc_sf_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_dealloc_sf_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x10];
	u8         function_id[0x10];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_alloc_sf_out_bits {
	u8         status[0x8];
	u8         reserved_at_8[0x18];

	u8         syndrome[0x20];

	u8         reserved_at_40[0x40];
};

struct mlx5_ifc_alloc_sf_in_bits {
	u8         opcode[0x10];
	u8         reserved_at_10[0x10];

	u8         reserved_at_20[0x10];
	u8         op_mod[0x10];

	u8         reserved_at_40[0x10];
	u8         function_id[0x10];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_affiliated_event_header_bits {
	u8         reserved_at_0[0x10];
	u8         obj_type[0x10];

	u8         obj_id[0x20];
};

enum {
	MLX5_HCA_CAP_GENERAL_OBJECT_TYPES_ENCRYPTION_KEY = BIT(0xc),
	MLX5_HCA_CAP_GENERAL_OBJECT_TYPES_IPSEC = BIT(0x13),
};

enum {
	MLX5_GENERAL_OBJECT_TYPES_ENCRYPTION_KEY = 0xc,
	MLX5_GENERAL_OBJECT_TYPES_IPSEC = 0x13,
};

enum {
	MLX5_IPSEC_OBJECT_ICV_LEN_16B,
	MLX5_IPSEC_OBJECT_ICV_LEN_12B,
	MLX5_IPSEC_OBJECT_ICV_LEN_8B,
};

struct mlx5_ifc_ipsec_obj_bits {
	u8         modify_field_select[0x40];
	u8         full_offload[0x1];
	u8         reserved_at_41[0x1];
	u8         esn_en[0x1];
	u8         esn_overlap[0x1];
	u8         reserved_at_44[0x2];
	u8         icv_length[0x2];
	u8         reserved_at_48[0x4];
	u8         aso_return_reg[0x4];
	u8         reserved_at_50[0x10];

	u8         esn_msb[0x20];

	u8         reserved_at_80[0x8];
	u8         dekn[0x18];

	u8         salt[0x20];

	u8         implicit_iv[0x40];

	u8         reserved_at_100[0x700];
};

struct mlx5_ifc_create_ipsec_obj_in_bits {
	struct mlx5_ifc_general_obj_in_cmd_hdr_bits general_obj_in_cmd_hdr;
	struct mlx5_ifc_ipsec_obj_bits ipsec_object;
};

enum {
	MLX5_MODIFY_IPSEC_BITMASK_ESN_OVERLAP = BIT(0),
	MLX5_MODIFY_IPSEC_BITMASK_ESN_MSB = BIT(1),
};

struct mlx5_ifc_query_ipsec_obj_out_bits {
	struct mlx5_ifc_general_obj_out_cmd_hdr_bits general_obj_out_cmd_hdr;
	struct mlx5_ifc_ipsec_obj_bits ipsec_object;
};

struct mlx5_ifc_modify_ipsec_obj_in_bits {
	struct mlx5_ifc_general_obj_in_cmd_hdr_bits general_obj_in_cmd_hdr;
	struct mlx5_ifc_ipsec_obj_bits ipsec_object;
};

struct mlx5_ifc_encryption_key_obj_bits {
	u8         modify_field_select[0x40];

	u8         reserved_at_40[0x14];
	u8         key_size[0x4];
	u8         reserved_at_58[0x4];
	u8         key_type[0x4];

	u8         reserved_at_60[0x8];
	u8         pd[0x18];

	u8         reserved_at_80[0x180];
	u8         key[8][0x20];

	u8         reserved_at_300[0x500];
};

struct mlx5_ifc_create_encryption_key_in_bits {
	struct mlx5_ifc_general_obj_in_cmd_hdr_bits general_obj_in_cmd_hdr;
	struct mlx5_ifc_encryption_key_obj_bits encryption_key_object;
};

enum {
	MLX5_GENERAL_OBJECT_TYPE_ENCRYPTION_KEY_KEY_SIZE_128 = 0x0,
	MLX5_GENERAL_OBJECT_TYPE_ENCRYPTION_KEY_KEY_SIZE_256 = 0x1,
};

enum {
	MLX5_GENERAL_OBJECT_TYPE_ENCRYPTION_KEY_TYPE_TLS = 0x1,
	MLX5_GENERAL_OBJECT_TYPE_ENCRYPTION_KEY_TYPE_IPSEC = 0x2,
};

struct mlx5_ifc_tls_static_params_bits {
	u8         const_2[0x2];
	u8         tls_version[0x4];
	u8         const_1[0x2];
	u8         reserved_at_8[0x14];
	u8         encryption_standard[0x4];

	u8         reserved_at_20[0x20];

	u8         initial_record_number[0x40];

	u8         resync_tcp_sn[0x20];

	u8         gcm_iv[0x20];

	u8         implicit_iv[0x40];

	u8         reserved_at_100[0x8];
	u8         dek_index[0x18];

	u8         reserved_at_120[0xe0];
};

struct mlx5_ifc_tls_progress_params_bits {
	u8         reserved_at_0[0x8];
	u8         tisn[0x18];

	u8         next_record_tcp_sn[0x20];

	u8         hw_resync_tcp_sn[0x20];

	u8         record_tracker_state[0x2];
	u8         auth_state[0x2];
	u8         reserved_at_64[0x4];
	u8         hw_offset_record_number[0x18];
};

#endif /* MLX5_IFC_H */
