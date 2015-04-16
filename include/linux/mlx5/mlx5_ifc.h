/*
 * Copyright (c) 2013-2015, Mellanox Technologies. All rights reserved.
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
	MLX5_CMD_OP_CREATE_MKEY                   = 0x200,
	MLX5_CMD_OP_QUERY_MKEY                    = 0x201,
	MLX5_CMD_OP_DESTROY_MKEY                  = 0x202,
	MLX5_CMD_OP_QUERY_SPECIAL_CONTEXTS        = 0x203,
	MLX5_CMD_OP_PAGE_FAULT_RESUME             = 0x204,
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
	MLX5_CMD_OP_INIT2INIT_QP                  = 0x50e,
	MLX5_CMD_OP_CREATE_PSV                    = 0x600,
	MLX5_CMD_OP_DESTROY_PSV                   = 0x601,
	MLX5_CMD_OP_CREATE_SRQ                    = 0x700,
	MLX5_CMD_OP_DESTROY_SRQ                   = 0x701,
	MLX5_CMD_OP_QUERY_SRQ                     = 0x702,
	MLX5_CMD_OP_ARM_RQ                        = 0x703,
	MLX5_CMD_OP_RESIZE_SRQ                    = 0x704,
	MLX5_CMD_OP_CREATE_DCT                    = 0x710,
	MLX5_CMD_OP_DESTROY_DCT                   = 0x711,
	MLX5_CMD_OP_DRAIN_DCT                     = 0x712,
	MLX5_CMD_OP_QUERY_DCT                     = 0x713,
	MLX5_CMD_OP_ARM_DCT_FOR_KEY_VIOLATION     = 0x714,
	MLX5_CMD_OP_QUERY_VPORT_STATE             = 0x750,
	MLX5_CMD_OP_MODIFY_VPORT_STATE            = 0x751,
	MLX5_CMD_OP_QUERY_ESW_VPORT_CONTEXT       = 0x752,
	MLX5_CMD_OP_MODIFY_ESW_VPORT_CONTEXT      = 0x753,
	MLX5_CMD_OP_QUERY_NIC_VPORT_CONTEXT       = 0x754,
	MLX5_CMD_OP_MODIFY_NIC_VPORT_CONTEXT      = 0x755,
	MLX5_CMD_OP_QUERY_RCOE_ADDRESS            = 0x760,
	MLX5_CMD_OP_SET_ROCE_ADDRESS              = 0x761,
	MLX5_CMD_OP_QUERY_VPORT_COUNTER           = 0x770,
	MLX5_CMD_OP_ALLOC_Q_COUNTER               = 0x771,
	MLX5_CMD_OP_DEALLOC_Q_COUNTER             = 0x772,
	MLX5_CMD_OP_QUERY_Q_COUNTER               = 0x773,
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
	MLX5_CMD_OP_SET_BURST_SIZE                = 0x812,
	MLX5_CMD_OP_QUERY_BURST_SZIE              = 0x813,
	MLX5_CMD_OP_ACTIVATE_TRACER               = 0x814,
	MLX5_CMD_OP_DEACTIVATE_TRACER             = 0x815,
	MLX5_CMD_OP_CREATE_SNIFFER_RULE           = 0x820,
	MLX5_CMD_OP_DESTROY_SNIFFER_RULE          = 0x821,
	MLX5_CMD_OP_QUERY_CONG_PARAMS             = 0x822,
	MLX5_CMD_OP_MODIFY_CONG_PARAMS            = 0x823,
	MLX5_CMD_OP_QUERY_CONG_STATISTICS         = 0x824,
	MLX5_CMD_OP_CREATE_TIR                    = 0x900,
	MLX5_CMD_OP_MODIFY_TIR                    = 0x901,
	MLX5_CMD_OP_DESTROY_TIR                   = 0x902,
	MLX5_CMD_OP_QUERY_TIR                     = 0x903,
	MLX5_CMD_OP_CREATE_TIS                    = 0x912,
	MLX5_CMD_OP_MODIFY_TIS                    = 0x913,
	MLX5_CMD_OP_DESTROY_TIS                   = 0x914,
	MLX5_CMD_OP_QUERY_TIS                     = 0x915,
	MLX5_CMD_OP_CREATE_SQ                     = 0x904,
	MLX5_CMD_OP_MODIFY_SQ                     = 0x905,
	MLX5_CMD_OP_DESTROY_SQ                    = 0x906,
	MLX5_CMD_OP_QUERY_SQ                      = 0x907,
	MLX5_CMD_OP_CREATE_RQ                     = 0x908,
	MLX5_CMD_OP_MODIFY_RQ                     = 0x909,
	MLX5_CMD_OP_DESTROY_RQ                    = 0x90a,
	MLX5_CMD_OP_QUERY_RQ                      = 0x90b,
	MLX5_CMD_OP_CREATE_RMP                    = 0x90c,
	MLX5_CMD_OP_MODIFY_RMP                    = 0x90d,
	MLX5_CMD_OP_DESTROY_RMP                   = 0x90e,
	MLX5_CMD_OP_QUERY_RMP                     = 0x90f,
	MLX5_CMD_OP_SET_FLOW_TABLE_ENTRY          = 0x910,
	MLX5_CMD_OP_QUERY_FLOW_TABLE_ENTRY        = 0x911,
	MLX5_CMD_OP_MAX				  = 0x911
};

struct mlx5_ifc_cmd_hca_cap_bits {
	u8         reserved_0[0x80];

	u8         log_max_srq_sz[0x8];
	u8         log_max_qp_sz[0x8];
	u8         reserved_1[0xb];
	u8         log_max_qp[0x5];

	u8         log_max_strq_sz[0x8];
	u8         reserved_2[0x3];
	u8         log_max_srqs[0x5];
	u8         reserved_3[0x10];

	u8         reserved_4[0x8];
	u8         log_max_cq_sz[0x8];
	u8         reserved_5[0xb];
	u8         log_max_cq[0x5];

	u8         log_max_eq_sz[0x8];
	u8         reserved_6[0x2];
	u8         log_max_mkey[0x6];
	u8         reserved_7[0xc];
	u8         log_max_eq[0x4];

	u8         max_indirection[0x8];
	u8         reserved_8[0x1];
	u8         log_max_mrw_sz[0x7];
	u8         reserved_9[0x2];
	u8         log_max_bsf_list_size[0x6];
	u8         reserved_10[0x2];
	u8         log_max_klm_list_size[0x6];

	u8         reserved_11[0xa];
	u8         log_max_ra_req_dc[0x6];
	u8         reserved_12[0xa];
	u8         log_max_ra_res_dc[0x6];

	u8         reserved_13[0xa];
	u8         log_max_ra_req_qp[0x6];
	u8         reserved_14[0xa];
	u8         log_max_ra_res_qp[0x6];

	u8         pad_cap[0x1];
	u8         cc_query_allowed[0x1];
	u8         cc_modify_allowed[0x1];
	u8         reserved_15[0x1d];

	u8         reserved_16[0x6];
	u8         max_qp_cnt[0xa];
	u8         pkey_table_size[0x10];

	u8         eswitch_owner[0x1];
	u8         reserved_17[0xa];
	u8         local_ca_ack_delay[0x5];
	u8         reserved_18[0x8];
	u8         num_ports[0x8];

	u8         reserved_19[0x3];
	u8         log_max_msg[0x5];
	u8         reserved_20[0x18];

	u8         stat_rate_support[0x10];
	u8         reserved_21[0x10];

	u8         reserved_22[0x10];
	u8         cmdif_checksum[0x2];
	u8         sigerr_cqe[0x1];
	u8         reserved_23[0x1];
	u8         wq_signature[0x1];
	u8         sctr_data_cqe[0x1];
	u8         reserved_24[0x1];
	u8         sho[0x1];
	u8         tph[0x1];
	u8         rf[0x1];
	u8         dc[0x1];
	u8         reserved_25[0x2];
	u8         roce[0x1];
	u8         atomic[0x1];
	u8         rsz_srq[0x1];

	u8         cq_oi[0x1];
	u8         cq_resize[0x1];
	u8         cq_moderation[0x1];
	u8         sniffer_rule_flow[0x1];
	u8         sniffer_rule_vport[0x1];
	u8         sniffer_rule_phy[0x1];
	u8         reserved_26[0x1];
	u8         pg[0x1];
	u8         block_lb_mc[0x1];
	u8         reserved_27[0x3];
	u8         cd[0x1];
	u8         reserved_28[0x1];
	u8         apm[0x1];
	u8         reserved_29[0x7];
	u8         qkv[0x1];
	u8         pkv[0x1];
	u8         reserved_30[0x4];
	u8         xrc[0x1];
	u8         ud[0x1];
	u8         uc[0x1];
	u8         rc[0x1];

	u8         reserved_31[0xa];
	u8         uar_sz[0x6];
	u8         reserved_32[0x8];
	u8         log_pg_sz[0x8];

	u8         bf[0x1];
	u8         reserved_33[0xa];
	u8         log_bf_reg_size[0x5];
	u8         reserved_34[0x10];

	u8         reserved_35[0x10];
	u8         max_wqe_sz_sq[0x10];

	u8         reserved_36[0x10];
	u8         max_wqe_sz_rq[0x10];

	u8         reserved_37[0x10];
	u8         max_wqe_sz_sq_dc[0x10];

	u8         reserved_38[0x7];
	u8         max_qp_mcg[0x19];

	u8         reserved_39[0x18];
	u8         log_max_mcg[0x8];

	u8         reserved_40[0xb];
	u8         log_max_pd[0x5];
	u8         reserved_41[0xb];
	u8         log_max_xrcd[0x5];

	u8         reserved_42[0x20];

	u8         reserved_43[0x3];
	u8         log_max_rq[0x5];
	u8         reserved_44[0x3];
	u8         log_max_sq[0x5];
	u8         reserved_45[0x3];
	u8         log_max_tir[0x5];
	u8         reserved_46[0x3];
	u8         log_max_tis[0x5];

	u8         reserved_47[0x13];
	u8         log_max_rq_per_tir[0x5];
	u8         reserved_48[0x3];
	u8         log_max_tis_per_sq[0x5];

	u8         reserved_49[0xe0];

	u8         reserved_50[0x10];
	u8         log_uar_page_sz[0x10];

	u8         reserved_51[0x100];

	u8         reserved_52[0x1f];
	u8         cqe_zip[0x1];

	u8         cqe_zip_timeout[0x10];
	u8         cqe_zip_max_num[0x10];

	u8         reserved_53[0x220];
};

struct mlx5_ifc_set_hca_cap_in_bits {
	u8         opcode[0x10];
	u8         reserved_0[0x10];

	u8         reserved_1[0x10];
	u8         op_mod[0x10];

	u8         reserved_2[0x40];

	struct mlx5_ifc_cmd_hca_cap_bits hca_capability_struct;
};

struct mlx5_ifc_query_hca_cap_in_bits {
	u8         opcode[0x10];
	u8         reserved_0[0x10];

	u8         reserved_1[0x10];
	u8         op_mod[0x10];

	u8         reserved_2[0x40];
};

struct mlx5_ifc_query_hca_cap_out_bits {
	u8         status[0x8];
	u8         reserved_0[0x18];

	u8         syndrome[0x20];

	u8         reserved_1[0x40];

	u8         capability_struct[256][0x8];
};

struct mlx5_ifc_set_hca_cap_out_bits {
	u8         status[0x8];
	u8         reserved_0[0x18];

	u8         syndrome[0x20];

	u8         reserved_1[0x40];
};

#endif /* MLX5_IFC_H */
