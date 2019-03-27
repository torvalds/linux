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

#ifndef __ECORE_HSI_RDMA__
#define __ECORE_HSI_RDMA__ 
/************************************************************************/
/* Add include to common rdma target for both eCore and protocol rdma driver */
/************************************************************************/
#include "rdma_common.h"

/*
 * The rdma task context of Mstorm
 */
struct ystorm_rdma_task_st_ctx
{
	struct regpair temp[4];
};

struct e4_ystorm_rdma_task_ag_ctx
{
	u8 reserved /* cdu_validation */;
	u8 byte1 /* state */;
	__le16 msem_ctx_upd_seq /* icid */;
	u8 flags0;
#define E4_YSTORM_RDMA_TASK_AG_CTX_CONNECTION_TYPE_MASK  0xF /* connection_type */
#define E4_YSTORM_RDMA_TASK_AG_CTX_CONNECTION_TYPE_SHIFT 0
#define E4_YSTORM_RDMA_TASK_AG_CTX_EXIST_IN_QM0_MASK     0x1 /* exist_in_qm0 */
#define E4_YSTORM_RDMA_TASK_AG_CTX_EXIST_IN_QM0_SHIFT    4
#define E4_YSTORM_RDMA_TASK_AG_CTX_BIT1_MASK             0x1 /* exist_in_qm1 */
#define E4_YSTORM_RDMA_TASK_AG_CTX_BIT1_SHIFT            5
#define E4_YSTORM_RDMA_TASK_AG_CTX_VALID_MASK            0x1 /* bit2 */
#define E4_YSTORM_RDMA_TASK_AG_CTX_VALID_SHIFT           6
#define E4_YSTORM_RDMA_TASK_AG_CTX_DIF_FIRST_IO_MASK     0x1 /* bit3 */
#define E4_YSTORM_RDMA_TASK_AG_CTX_DIF_FIRST_IO_SHIFT    7
	u8 flags1;
#define E4_YSTORM_RDMA_TASK_AG_CTX_CF0_MASK              0x3 /* cf0 */
#define E4_YSTORM_RDMA_TASK_AG_CTX_CF0_SHIFT             0
#define E4_YSTORM_RDMA_TASK_AG_CTX_CF1_MASK              0x3 /* cf1 */
#define E4_YSTORM_RDMA_TASK_AG_CTX_CF1_SHIFT             2
#define E4_YSTORM_RDMA_TASK_AG_CTX_CF2SPECIAL_MASK       0x3 /* cf2special */
#define E4_YSTORM_RDMA_TASK_AG_CTX_CF2SPECIAL_SHIFT      4
#define E4_YSTORM_RDMA_TASK_AG_CTX_CF0EN_MASK            0x1 /* cf0en */
#define E4_YSTORM_RDMA_TASK_AG_CTX_CF0EN_SHIFT           6
#define E4_YSTORM_RDMA_TASK_AG_CTX_CF1EN_MASK            0x1 /* cf1en */
#define E4_YSTORM_RDMA_TASK_AG_CTX_CF1EN_SHIFT           7
	u8 flags2;
#define E4_YSTORM_RDMA_TASK_AG_CTX_BIT4_MASK             0x1 /* bit4 */
#define E4_YSTORM_RDMA_TASK_AG_CTX_BIT4_SHIFT            0
#define E4_YSTORM_RDMA_TASK_AG_CTX_RULE0EN_MASK          0x1 /* rule0en */
#define E4_YSTORM_RDMA_TASK_AG_CTX_RULE0EN_SHIFT         1
#define E4_YSTORM_RDMA_TASK_AG_CTX_RULE1EN_MASK          0x1 /* rule1en */
#define E4_YSTORM_RDMA_TASK_AG_CTX_RULE1EN_SHIFT         2
#define E4_YSTORM_RDMA_TASK_AG_CTX_RULE2EN_MASK          0x1 /* rule2en */
#define E4_YSTORM_RDMA_TASK_AG_CTX_RULE2EN_SHIFT         3
#define E4_YSTORM_RDMA_TASK_AG_CTX_RULE3EN_MASK          0x1 /* rule3en */
#define E4_YSTORM_RDMA_TASK_AG_CTX_RULE3EN_SHIFT         4
#define E4_YSTORM_RDMA_TASK_AG_CTX_RULE4EN_MASK          0x1 /* rule4en */
#define E4_YSTORM_RDMA_TASK_AG_CTX_RULE4EN_SHIFT         5
#define E4_YSTORM_RDMA_TASK_AG_CTX_RULE5EN_MASK          0x1 /* rule5en */
#define E4_YSTORM_RDMA_TASK_AG_CTX_RULE5EN_SHIFT         6
#define E4_YSTORM_RDMA_TASK_AG_CTX_RULE6EN_MASK          0x1 /* rule6en */
#define E4_YSTORM_RDMA_TASK_AG_CTX_RULE6EN_SHIFT         7
	u8 key /* byte2 */;
	__le32 mw_cnt /* reg0 */;
	u8 ref_cnt_seq /* byte3 */;
	u8 ctx_upd_seq /* byte4 */;
	__le16 dif_flags /* word1 */;
	__le16 tx_ref_count /* word2 */;
	__le16 last_used_ltid /* word3 */;
	__le16 parent_mr_lo /* word4 */;
	__le16 parent_mr_hi /* word5 */;
	__le32 fbo_lo /* reg1 */;
	__le32 fbo_hi /* reg2 */;
};

struct e4_mstorm_rdma_task_ag_ctx
{
	u8 reserved /* cdu_validation */;
	u8 byte1 /* state */;
	__le16 icid /* icid */;
	u8 flags0;
#define E4_MSTORM_RDMA_TASK_AG_CTX_CONNECTION_TYPE_MASK  0xF /* connection_type */
#define E4_MSTORM_RDMA_TASK_AG_CTX_CONNECTION_TYPE_SHIFT 0
#define E4_MSTORM_RDMA_TASK_AG_CTX_EXIST_IN_QM0_MASK     0x1 /* exist_in_qm0 */
#define E4_MSTORM_RDMA_TASK_AG_CTX_EXIST_IN_QM0_SHIFT    4
#define E4_MSTORM_RDMA_TASK_AG_CTX_BIT1_MASK             0x1 /* exist_in_qm1 */
#define E4_MSTORM_RDMA_TASK_AG_CTX_BIT1_SHIFT            5
#define E4_MSTORM_RDMA_TASK_AG_CTX_BIT2_MASK             0x1 /* bit2 */
#define E4_MSTORM_RDMA_TASK_AG_CTX_BIT2_SHIFT            6
#define E4_MSTORM_RDMA_TASK_AG_CTX_DIF_FIRST_IO_MASK     0x1 /* bit3 */
#define E4_MSTORM_RDMA_TASK_AG_CTX_DIF_FIRST_IO_SHIFT    7
	u8 flags1;
#define E4_MSTORM_RDMA_TASK_AG_CTX_CF0_MASK              0x3 /* cf0 */
#define E4_MSTORM_RDMA_TASK_AG_CTX_CF0_SHIFT             0
#define E4_MSTORM_RDMA_TASK_AG_CTX_CF1_MASK              0x3 /* cf1 */
#define E4_MSTORM_RDMA_TASK_AG_CTX_CF1_SHIFT             2
#define E4_MSTORM_RDMA_TASK_AG_CTX_CF2_MASK              0x3 /* cf2 */
#define E4_MSTORM_RDMA_TASK_AG_CTX_CF2_SHIFT             4
#define E4_MSTORM_RDMA_TASK_AG_CTX_CF0EN_MASK            0x1 /* cf0en */
#define E4_MSTORM_RDMA_TASK_AG_CTX_CF0EN_SHIFT           6
#define E4_MSTORM_RDMA_TASK_AG_CTX_CF1EN_MASK            0x1 /* cf1en */
#define E4_MSTORM_RDMA_TASK_AG_CTX_CF1EN_SHIFT           7
	u8 flags2;
#define E4_MSTORM_RDMA_TASK_AG_CTX_CF2EN_MASK            0x1 /* cf2en */
#define E4_MSTORM_RDMA_TASK_AG_CTX_CF2EN_SHIFT           0
#define E4_MSTORM_RDMA_TASK_AG_CTX_RULE0EN_MASK          0x1 /* rule0en */
#define E4_MSTORM_RDMA_TASK_AG_CTX_RULE0EN_SHIFT         1
#define E4_MSTORM_RDMA_TASK_AG_CTX_RULE1EN_MASK          0x1 /* rule1en */
#define E4_MSTORM_RDMA_TASK_AG_CTX_RULE1EN_SHIFT         2
#define E4_MSTORM_RDMA_TASK_AG_CTX_RULE2EN_MASK          0x1 /* rule2en */
#define E4_MSTORM_RDMA_TASK_AG_CTX_RULE2EN_SHIFT         3
#define E4_MSTORM_RDMA_TASK_AG_CTX_RULE3EN_MASK          0x1 /* rule3en */
#define E4_MSTORM_RDMA_TASK_AG_CTX_RULE3EN_SHIFT         4
#define E4_MSTORM_RDMA_TASK_AG_CTX_RULE4EN_MASK          0x1 /* rule4en */
#define E4_MSTORM_RDMA_TASK_AG_CTX_RULE4EN_SHIFT         5
#define E4_MSTORM_RDMA_TASK_AG_CTX_RULE5EN_MASK          0x1 /* rule5en */
#define E4_MSTORM_RDMA_TASK_AG_CTX_RULE5EN_SHIFT         6
#define E4_MSTORM_RDMA_TASK_AG_CTX_RULE6EN_MASK          0x1 /* rule6en */
#define E4_MSTORM_RDMA_TASK_AG_CTX_RULE6EN_SHIFT         7
	u8 key /* byte2 */;
	__le32 mw_cnt /* reg0 */;
	u8 ref_cnt_seq /* byte3 */;
	u8 ctx_upd_seq /* byte4 */;
	__le16 dif_flags /* word1 */;
	__le16 tx_ref_count /* word2 */;
	__le16 last_used_ltid /* word3 */;
	__le16 parent_mr_lo /* word4 */;
	__le16 parent_mr_hi /* word5 */;
	__le32 fbo_lo /* reg1 */;
	__le32 fbo_hi /* reg2 */;
};

/*
 * The roce task context of Mstorm
 */
struct mstorm_rdma_task_st_ctx
{
	struct regpair temp[4];
};

/*
 * The roce task context of Ustorm
 */
struct ustorm_rdma_task_st_ctx
{
	struct regpair temp[2];
};

struct e4_ustorm_rdma_task_ag_ctx
{
	u8 reserved /* cdu_validation */;
	u8 byte1 /* state */;
	__le16 icid /* icid */;
	u8 flags0;
#define E4_USTORM_RDMA_TASK_AG_CTX_CONNECTION_TYPE_MASK         0xF /* connection_type */
#define E4_USTORM_RDMA_TASK_AG_CTX_CONNECTION_TYPE_SHIFT        0
#define E4_USTORM_RDMA_TASK_AG_CTX_EXIST_IN_QM0_MASK            0x1 /* exist_in_qm0 */
#define E4_USTORM_RDMA_TASK_AG_CTX_EXIST_IN_QM0_SHIFT           4
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_RUNT_VALID_MASK          0x1 /* exist_in_qm1 */
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_RUNT_VALID_SHIFT         5
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_WRITE_RESULT_CF_MASK     0x3 /* timer0cf */
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_WRITE_RESULT_CF_SHIFT    6
	u8 flags1;
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_RESULT_TOGGLE_BIT_MASK   0x3 /* timer1cf */
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_RESULT_TOGGLE_BIT_SHIFT  0
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_TX_IO_FLG_MASK           0x3 /* timer2cf */
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_TX_IO_FLG_SHIFT          2
#define E4_USTORM_RDMA_TASK_AG_CTX_CF3_MASK                     0x3 /* timer_stop_all */
#define E4_USTORM_RDMA_TASK_AG_CTX_CF3_SHIFT                    4
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_ERROR_CF_MASK            0x3 /* cf4 */
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_ERROR_CF_SHIFT           6
	u8 flags2;
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_WRITE_RESULT_CF_EN_MASK  0x1 /* cf0en */
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_WRITE_RESULT_CF_EN_SHIFT 0
#define E4_USTORM_RDMA_TASK_AG_CTX_RESERVED2_MASK               0x1 /* cf1en */
#define E4_USTORM_RDMA_TASK_AG_CTX_RESERVED2_SHIFT              1
#define E4_USTORM_RDMA_TASK_AG_CTX_RESERVED3_MASK               0x1 /* cf2en */
#define E4_USTORM_RDMA_TASK_AG_CTX_RESERVED3_SHIFT              2
#define E4_USTORM_RDMA_TASK_AG_CTX_CF3EN_MASK                   0x1 /* cf3en */
#define E4_USTORM_RDMA_TASK_AG_CTX_CF3EN_SHIFT                  3
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_ERROR_CF_EN_MASK         0x1 /* cf4en */
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_ERROR_CF_EN_SHIFT        4
#define E4_USTORM_RDMA_TASK_AG_CTX_RULE0EN_MASK                 0x1 /* rule0en */
#define E4_USTORM_RDMA_TASK_AG_CTX_RULE0EN_SHIFT                5
#define E4_USTORM_RDMA_TASK_AG_CTX_RULE1EN_MASK                 0x1 /* rule1en */
#define E4_USTORM_RDMA_TASK_AG_CTX_RULE1EN_SHIFT                6
#define E4_USTORM_RDMA_TASK_AG_CTX_RULE2EN_MASK                 0x1 /* rule2en */
#define E4_USTORM_RDMA_TASK_AG_CTX_RULE2EN_SHIFT                7
	u8 flags3;
#define E4_USTORM_RDMA_TASK_AG_CTX_RULE3EN_MASK                 0x1 /* rule3en */
#define E4_USTORM_RDMA_TASK_AG_CTX_RULE3EN_SHIFT                0
#define E4_USTORM_RDMA_TASK_AG_CTX_RULE4EN_MASK                 0x1 /* rule4en */
#define E4_USTORM_RDMA_TASK_AG_CTX_RULE4EN_SHIFT                1
#define E4_USTORM_RDMA_TASK_AG_CTX_RULE5EN_MASK                 0x1 /* rule5en */
#define E4_USTORM_RDMA_TASK_AG_CTX_RULE5EN_SHIFT                2
#define E4_USTORM_RDMA_TASK_AG_CTX_RULE6EN_MASK                 0x1 /* rule6en */
#define E4_USTORM_RDMA_TASK_AG_CTX_RULE6EN_SHIFT                3
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_ERROR_TYPE_MASK          0xF /* nibble1 */
#define E4_USTORM_RDMA_TASK_AG_CTX_DIF_ERROR_TYPE_SHIFT         4
	__le32 dif_err_intervals /* reg0 */;
	__le32 dif_error_1st_interval /* reg1 */;
	__le32 reg2 /* reg2 */;
	__le32 dif_runt_value /* reg3 */;
	__le32 reg4 /* reg4 */;
	__le32 reg5 /* reg5 */;
};

/*
 * RDMA task context
 */
struct e4_rdma_task_context
{
	struct ystorm_rdma_task_st_ctx ystorm_st_context /* ystorm storm context */;
	struct e4_ystorm_rdma_task_ag_ctx ystorm_ag_context /* ystorm aggregative context */;
	struct tdif_task_context tdif_context /* tdif context */;
	struct e4_mstorm_rdma_task_ag_ctx mstorm_ag_context /* mstorm aggregative context */;
	struct mstorm_rdma_task_st_ctx mstorm_st_context /* mstorm storm context */;
	struct rdif_task_context rdif_context /* rdif context */;
	struct ustorm_rdma_task_st_ctx ustorm_st_context /* ustorm storm context */;
	struct regpair ustorm_st_padding[2] /* padding */;
	struct e4_ustorm_rdma_task_ag_ctx ustorm_ag_context /* ustorm aggregative context */;
};


struct e5_ystorm_rdma_task_ag_ctx
{
	u8 reserved /* cdu_validation */;
	u8 byte1 /* state_and_core_id */;
	__le16 msem_ctx_upd_seq /* icid */;
	u8 flags0;
#define E5_YSTORM_RDMA_TASK_AG_CTX_CONNECTION_TYPE_MASK  0xF /* connection_type */
#define E5_YSTORM_RDMA_TASK_AG_CTX_CONNECTION_TYPE_SHIFT 0
#define E5_YSTORM_RDMA_TASK_AG_CTX_EXIST_IN_QM0_MASK     0x1 /* exist_in_qm0 */
#define E5_YSTORM_RDMA_TASK_AG_CTX_EXIST_IN_QM0_SHIFT    4
#define E5_YSTORM_RDMA_TASK_AG_CTX_BIT1_MASK             0x1 /* exist_in_qm1 */
#define E5_YSTORM_RDMA_TASK_AG_CTX_BIT1_SHIFT            5
#define E5_YSTORM_RDMA_TASK_AG_CTX_VALID_MASK            0x1 /* bit2 */
#define E5_YSTORM_RDMA_TASK_AG_CTX_VALID_SHIFT           6
#define E5_YSTORM_RDMA_TASK_AG_CTX_BIT3_MASK             0x1 /* bit3 */
#define E5_YSTORM_RDMA_TASK_AG_CTX_BIT3_SHIFT            7
	u8 flags1;
#define E5_YSTORM_RDMA_TASK_AG_CTX_CF0_MASK              0x3 /* cf0 */
#define E5_YSTORM_RDMA_TASK_AG_CTX_CF0_SHIFT             0
#define E5_YSTORM_RDMA_TASK_AG_CTX_CF1_MASK              0x3 /* cf1 */
#define E5_YSTORM_RDMA_TASK_AG_CTX_CF1_SHIFT             2
#define E5_YSTORM_RDMA_TASK_AG_CTX_CF2SPECIAL_MASK       0x3 /* cf2special */
#define E5_YSTORM_RDMA_TASK_AG_CTX_CF2SPECIAL_SHIFT      4
#define E5_YSTORM_RDMA_TASK_AG_CTX_CF0EN_MASK            0x1 /* cf0en */
#define E5_YSTORM_RDMA_TASK_AG_CTX_CF0EN_SHIFT           6
#define E5_YSTORM_RDMA_TASK_AG_CTX_CF1EN_MASK            0x1 /* cf1en */
#define E5_YSTORM_RDMA_TASK_AG_CTX_CF1EN_SHIFT           7
	u8 flags2;
#define E5_YSTORM_RDMA_TASK_AG_CTX_BIT4_MASK             0x1 /* bit4 */
#define E5_YSTORM_RDMA_TASK_AG_CTX_BIT4_SHIFT            0
#define E5_YSTORM_RDMA_TASK_AG_CTX_RULE0EN_MASK          0x1 /* rule0en */
#define E5_YSTORM_RDMA_TASK_AG_CTX_RULE0EN_SHIFT         1
#define E5_YSTORM_RDMA_TASK_AG_CTX_RULE1EN_MASK          0x1 /* rule1en */
#define E5_YSTORM_RDMA_TASK_AG_CTX_RULE1EN_SHIFT         2
#define E5_YSTORM_RDMA_TASK_AG_CTX_RULE2EN_MASK          0x1 /* rule2en */
#define E5_YSTORM_RDMA_TASK_AG_CTX_RULE2EN_SHIFT         3
#define E5_YSTORM_RDMA_TASK_AG_CTX_RULE3EN_MASK          0x1 /* rule3en */
#define E5_YSTORM_RDMA_TASK_AG_CTX_RULE3EN_SHIFT         4
#define E5_YSTORM_RDMA_TASK_AG_CTX_RULE4EN_MASK          0x1 /* rule4en */
#define E5_YSTORM_RDMA_TASK_AG_CTX_RULE4EN_SHIFT         5
#define E5_YSTORM_RDMA_TASK_AG_CTX_RULE5EN_MASK          0x1 /* rule5en */
#define E5_YSTORM_RDMA_TASK_AG_CTX_RULE5EN_SHIFT         6
#define E5_YSTORM_RDMA_TASK_AG_CTX_RULE6EN_MASK          0x1 /* rule6en */
#define E5_YSTORM_RDMA_TASK_AG_CTX_RULE6EN_SHIFT         7
	u8 flags3;
#define E5_YSTORM_RDMA_TASK_AG_CTX_E4_RESERVED1_MASK     0x1 /* bit5 */
#define E5_YSTORM_RDMA_TASK_AG_CTX_E4_RESERVED1_SHIFT    0
#define E5_YSTORM_RDMA_TASK_AG_CTX_E4_RESERVED2_MASK     0x3 /* cf3 */
#define E5_YSTORM_RDMA_TASK_AG_CTX_E4_RESERVED2_SHIFT    1
#define E5_YSTORM_RDMA_TASK_AG_CTX_E4_RESERVED3_MASK     0x3 /* cf4 */
#define E5_YSTORM_RDMA_TASK_AG_CTX_E4_RESERVED3_SHIFT    3
#define E5_YSTORM_RDMA_TASK_AG_CTX_E4_RESERVED4_MASK     0x1 /* cf3en */
#define E5_YSTORM_RDMA_TASK_AG_CTX_E4_RESERVED4_SHIFT    5
#define E5_YSTORM_RDMA_TASK_AG_CTX_E4_RESERVED5_MASK     0x1 /* cf4en */
#define E5_YSTORM_RDMA_TASK_AG_CTX_E4_RESERVED5_SHIFT    6
#define E5_YSTORM_RDMA_TASK_AG_CTX_E4_RESERVED6_MASK     0x1 /* rule7en */
#define E5_YSTORM_RDMA_TASK_AG_CTX_E4_RESERVED6_SHIFT    7
	__le32 mw_cnt /* reg0 */;
	u8 key /* byte2 */;
	u8 ref_cnt_seq /* byte3 */;
	u8 ctx_upd_seq /* byte4 */;
	u8 e4_reserved7 /* byte5 */;
	__le16 dif_flags /* word1 */;
	__le16 tx_ref_count /* word2 */;
	__le16 last_used_ltid /* word3 */;
	__le16 parent_mr_lo /* word4 */;
	__le16 parent_mr_hi /* word5 */;
	__le16 e4_reserved8 /* word6 */;
	__le32 fbo_lo /* reg1 */;
};

struct e5_mstorm_rdma_task_ag_ctx
{
	u8 reserved /* cdu_validation */;
	u8 byte1 /* state_and_core_id */;
	__le16 icid /* icid */;
	u8 flags0;
#define E5_MSTORM_RDMA_TASK_AG_CTX_CONNECTION_TYPE_MASK  0xF /* connection_type */
#define E5_MSTORM_RDMA_TASK_AG_CTX_CONNECTION_TYPE_SHIFT 0
#define E5_MSTORM_RDMA_TASK_AG_CTX_EXIST_IN_QM0_MASK     0x1 /* exist_in_qm0 */
#define E5_MSTORM_RDMA_TASK_AG_CTX_EXIST_IN_QM0_SHIFT    4
#define E5_MSTORM_RDMA_TASK_AG_CTX_BIT1_MASK             0x1 /* exist_in_qm1 */
#define E5_MSTORM_RDMA_TASK_AG_CTX_BIT1_SHIFT            5
#define E5_MSTORM_RDMA_TASK_AG_CTX_BIT2_MASK             0x1 /* bit2 */
#define E5_MSTORM_RDMA_TASK_AG_CTX_BIT2_SHIFT            6
#define E5_MSTORM_RDMA_TASK_AG_CTX_BIT3_MASK             0x1 /* bit3 */
#define E5_MSTORM_RDMA_TASK_AG_CTX_BIT3_SHIFT            7
	u8 flags1;
#define E5_MSTORM_RDMA_TASK_AG_CTX_CF0_MASK              0x3 /* cf0 */
#define E5_MSTORM_RDMA_TASK_AG_CTX_CF0_SHIFT             0
#define E5_MSTORM_RDMA_TASK_AG_CTX_CF1_MASK              0x3 /* cf1 */
#define E5_MSTORM_RDMA_TASK_AG_CTX_CF1_SHIFT             2
#define E5_MSTORM_RDMA_TASK_AG_CTX_CF2_MASK              0x3 /* cf2 */
#define E5_MSTORM_RDMA_TASK_AG_CTX_CF2_SHIFT             4
#define E5_MSTORM_RDMA_TASK_AG_CTX_CF0EN_MASK            0x1 /* cf0en */
#define E5_MSTORM_RDMA_TASK_AG_CTX_CF0EN_SHIFT           6
#define E5_MSTORM_RDMA_TASK_AG_CTX_CF1EN_MASK            0x1 /* cf1en */
#define E5_MSTORM_RDMA_TASK_AG_CTX_CF1EN_SHIFT           7
	u8 flags2;
#define E5_MSTORM_RDMA_TASK_AG_CTX_CF2EN_MASK            0x1 /* cf2en */
#define E5_MSTORM_RDMA_TASK_AG_CTX_CF2EN_SHIFT           0
#define E5_MSTORM_RDMA_TASK_AG_CTX_RULE0EN_MASK          0x1 /* rule0en */
#define E5_MSTORM_RDMA_TASK_AG_CTX_RULE0EN_SHIFT         1
#define E5_MSTORM_RDMA_TASK_AG_CTX_RULE1EN_MASK          0x1 /* rule1en */
#define E5_MSTORM_RDMA_TASK_AG_CTX_RULE1EN_SHIFT         2
#define E5_MSTORM_RDMA_TASK_AG_CTX_RULE2EN_MASK          0x1 /* rule2en */
#define E5_MSTORM_RDMA_TASK_AG_CTX_RULE2EN_SHIFT         3
#define E5_MSTORM_RDMA_TASK_AG_CTX_RULE3EN_MASK          0x1 /* rule3en */
#define E5_MSTORM_RDMA_TASK_AG_CTX_RULE3EN_SHIFT         4
#define E5_MSTORM_RDMA_TASK_AG_CTX_RULE4EN_MASK          0x1 /* rule4en */
#define E5_MSTORM_RDMA_TASK_AG_CTX_RULE4EN_SHIFT         5
#define E5_MSTORM_RDMA_TASK_AG_CTX_RULE5EN_MASK          0x1 /* rule5en */
#define E5_MSTORM_RDMA_TASK_AG_CTX_RULE5EN_SHIFT         6
#define E5_MSTORM_RDMA_TASK_AG_CTX_RULE6EN_MASK          0x1 /* rule6en */
#define E5_MSTORM_RDMA_TASK_AG_CTX_RULE6EN_SHIFT         7
	u8 flags3;
#define E5_MSTORM_RDMA_TASK_AG_CTX_E4_RESERVED1_MASK     0x1 /* bit4 */
#define E5_MSTORM_RDMA_TASK_AG_CTX_E4_RESERVED1_SHIFT    0
#define E5_MSTORM_RDMA_TASK_AG_CTX_E4_RESERVED2_MASK     0x3 /* cf3 */
#define E5_MSTORM_RDMA_TASK_AG_CTX_E4_RESERVED2_SHIFT    1
#define E5_MSTORM_RDMA_TASK_AG_CTX_E4_RESERVED3_MASK     0x3 /* cf4 */
#define E5_MSTORM_RDMA_TASK_AG_CTX_E4_RESERVED3_SHIFT    3
#define E5_MSTORM_RDMA_TASK_AG_CTX_E4_RESERVED4_MASK     0x1 /* cf3en */
#define E5_MSTORM_RDMA_TASK_AG_CTX_E4_RESERVED4_SHIFT    5
#define E5_MSTORM_RDMA_TASK_AG_CTX_E4_RESERVED5_MASK     0x1 /* cf4en */
#define E5_MSTORM_RDMA_TASK_AG_CTX_E4_RESERVED5_SHIFT    6
#define E5_MSTORM_RDMA_TASK_AG_CTX_E4_RESERVED6_MASK     0x1 /* rule7en */
#define E5_MSTORM_RDMA_TASK_AG_CTX_E4_RESERVED6_SHIFT    7
	__le32 mw_cnt /* reg0 */;
	u8 key /* byte2 */;
	u8 ref_cnt_seq /* byte3 */;
	u8 ctx_upd_seq /* byte4 */;
	u8 e4_reserved7 /* byte5 */;
	__le16 dif_flags /* regpair0 */;
	__le16 tx_ref_count /* word2 */;
	__le16 last_used_ltid /* word3 */;
	__le16 parent_mr_lo /* word4 */;
	__le16 parent_mr_hi /* regpair1 */;
	__le16 e4_reserved8 /* word6 */;
	__le32 fbo_lo /* reg1 */;
};

struct e5_ustorm_rdma_task_ag_ctx
{
	u8 reserved /* cdu_validation */;
	u8 byte1 /* state_and_core_id */;
	__le16 icid /* icid */;
	u8 flags0;
#define E5_USTORM_RDMA_TASK_AG_CTX_CONNECTION_TYPE_MASK         0xF /* connection_type */
#define E5_USTORM_RDMA_TASK_AG_CTX_CONNECTION_TYPE_SHIFT        0
#define E5_USTORM_RDMA_TASK_AG_CTX_EXIST_IN_QM0_MASK            0x1 /* exist_in_qm0 */
#define E5_USTORM_RDMA_TASK_AG_CTX_EXIST_IN_QM0_SHIFT           4
#define E5_USTORM_RDMA_TASK_AG_CTX_DIF_RUNT_VALID_MASK          0x1 /* exist_in_qm1 */
#define E5_USTORM_RDMA_TASK_AG_CTX_DIF_RUNT_VALID_SHIFT         5
#define E5_USTORM_RDMA_TASK_AG_CTX_DIF_WRITE_RESULT_CF_MASK     0x3 /* timer0cf */
#define E5_USTORM_RDMA_TASK_AG_CTX_DIF_WRITE_RESULT_CF_SHIFT    6
	u8 flags1;
#define E5_USTORM_RDMA_TASK_AG_CTX_DIF_RESULT_TOGGLE_BIT_MASK   0x3 /* timer1cf */
#define E5_USTORM_RDMA_TASK_AG_CTX_DIF_RESULT_TOGGLE_BIT_SHIFT  0
#define E5_USTORM_RDMA_TASK_AG_CTX_DIF_TX_IO_FLG_MASK           0x3 /* timer2cf */
#define E5_USTORM_RDMA_TASK_AG_CTX_DIF_TX_IO_FLG_SHIFT          2
#define E5_USTORM_RDMA_TASK_AG_CTX_CF3_MASK                     0x3 /* timer_stop_all */
#define E5_USTORM_RDMA_TASK_AG_CTX_CF3_SHIFT                    4
#define E5_USTORM_RDMA_TASK_AG_CTX_DIF_ERROR_CF_MASK            0x3 /* dif_error_cf */
#define E5_USTORM_RDMA_TASK_AG_CTX_DIF_ERROR_CF_SHIFT           6
	u8 flags2;
#define E5_USTORM_RDMA_TASK_AG_CTX_DIF_WRITE_RESULT_CF_EN_MASK  0x1 /* cf0en */
#define E5_USTORM_RDMA_TASK_AG_CTX_DIF_WRITE_RESULT_CF_EN_SHIFT 0
#define E5_USTORM_RDMA_TASK_AG_CTX_RESERVED2_MASK               0x1 /* cf1en */
#define E5_USTORM_RDMA_TASK_AG_CTX_RESERVED2_SHIFT              1
#define E5_USTORM_RDMA_TASK_AG_CTX_RESERVED3_MASK               0x1 /* cf2en */
#define E5_USTORM_RDMA_TASK_AG_CTX_RESERVED3_SHIFT              2
#define E5_USTORM_RDMA_TASK_AG_CTX_CF3EN_MASK                   0x1 /* cf3en */
#define E5_USTORM_RDMA_TASK_AG_CTX_CF3EN_SHIFT                  3
#define E5_USTORM_RDMA_TASK_AG_CTX_DIF_ERROR_CF_EN_MASK         0x1 /* cf4en */
#define E5_USTORM_RDMA_TASK_AG_CTX_DIF_ERROR_CF_EN_SHIFT        4
#define E5_USTORM_RDMA_TASK_AG_CTX_RULE0EN_MASK                 0x1 /* rule0en */
#define E5_USTORM_RDMA_TASK_AG_CTX_RULE0EN_SHIFT                5
#define E5_USTORM_RDMA_TASK_AG_CTX_RULE1EN_MASK                 0x1 /* rule1en */
#define E5_USTORM_RDMA_TASK_AG_CTX_RULE1EN_SHIFT                6
#define E5_USTORM_RDMA_TASK_AG_CTX_RULE2EN_MASK                 0x1 /* rule2en */
#define E5_USTORM_RDMA_TASK_AG_CTX_RULE2EN_SHIFT                7
	u8 flags3;
#define E5_USTORM_RDMA_TASK_AG_CTX_RULE3EN_MASK                 0x1 /* rule3en */
#define E5_USTORM_RDMA_TASK_AG_CTX_RULE3EN_SHIFT                0
#define E5_USTORM_RDMA_TASK_AG_CTX_RULE4EN_MASK                 0x1 /* rule4en */
#define E5_USTORM_RDMA_TASK_AG_CTX_RULE4EN_SHIFT                1
#define E5_USTORM_RDMA_TASK_AG_CTX_RULE5EN_MASK                 0x1 /* rule5en */
#define E5_USTORM_RDMA_TASK_AG_CTX_RULE5EN_SHIFT                2
#define E5_USTORM_RDMA_TASK_AG_CTX_RULE6EN_MASK                 0x1 /* rule6en */
#define E5_USTORM_RDMA_TASK_AG_CTX_RULE6EN_SHIFT                3
#define E5_USTORM_RDMA_TASK_AG_CTX_E4_RESERVED1_MASK            0x1 /* bit2 */
#define E5_USTORM_RDMA_TASK_AG_CTX_E4_RESERVED1_SHIFT           4
#define E5_USTORM_RDMA_TASK_AG_CTX_E4_RESERVED2_MASK            0x1 /* bit3 */
#define E5_USTORM_RDMA_TASK_AG_CTX_E4_RESERVED2_SHIFT           5
#define E5_USTORM_RDMA_TASK_AG_CTX_E4_RESERVED3_MASK            0x1 /* bit4 */
#define E5_USTORM_RDMA_TASK_AG_CTX_E4_RESERVED3_SHIFT           6
#define E5_USTORM_RDMA_TASK_AG_CTX_E4_RESERVED4_MASK            0x1 /* rule7en */
#define E5_USTORM_RDMA_TASK_AG_CTX_E4_RESERVED4_SHIFT           7
	u8 flags4;
#define E5_USTORM_RDMA_TASK_AG_CTX_E4_RESERVED5_MASK            0x3 /* cf5 */
#define E5_USTORM_RDMA_TASK_AG_CTX_E4_RESERVED5_SHIFT           0
#define E5_USTORM_RDMA_TASK_AG_CTX_E4_RESERVED6_MASK            0x1 /* cf5en */
#define E5_USTORM_RDMA_TASK_AG_CTX_E4_RESERVED6_SHIFT           2
#define E5_USTORM_RDMA_TASK_AG_CTX_E4_RESERVED7_MASK            0x1 /* rule8en */
#define E5_USTORM_RDMA_TASK_AG_CTX_E4_RESERVED7_SHIFT           3
#define E5_USTORM_RDMA_TASK_AG_CTX_DIF_ERROR_TYPE_MASK          0xF /* dif_error_type */
#define E5_USTORM_RDMA_TASK_AG_CTX_DIF_ERROR_TYPE_SHIFT         4
	u8 byte2 /* byte2 */;
	u8 byte3 /* byte3 */;
	u8 e4_reserved8 /* byte4 */;
	__le32 dif_err_intervals /* dif_err_intervals */;
	__le32 dif_error_1st_interval /* dif_error_1st_interval */;
	__le32 reg2 /* reg2 */;
	__le32 dif_runt_value /* reg3 */;
	__le32 reg4 /* reg4 */;
};

/*
 * RDMA task context
 */
struct e5_rdma_task_context
{
	struct ystorm_rdma_task_st_ctx ystorm_st_context /* ystorm storm context */;
	struct e5_ystorm_rdma_task_ag_ctx ystorm_ag_context /* ystorm aggregative context */;
	struct tdif_task_context tdif_context /* tdif context */;
	struct e5_mstorm_rdma_task_ag_ctx mstorm_ag_context /* mstorm aggregative context */;
	struct mstorm_rdma_task_st_ctx mstorm_st_context /* mstorm storm context */;
	struct rdif_task_context rdif_context /* rdif context */;
	struct ustorm_rdma_task_st_ctx ustorm_st_context /* ustorm storm context */;
	struct regpair ustorm_st_padding[2] /* padding */;
	struct e5_ustorm_rdma_task_ag_ctx ustorm_ag_context /* ustorm aggregative context */;
};



/*
 * rdma function init ramrod data
 */
struct rdma_close_func_ramrod_data
{
	u8 cnq_start_offset;
	u8 num_cnqs;
	u8 vf_id /* This field should be assigned to Virtual Function ID if vf_valid == 1. Otherwise its dont care */;
	u8 vf_valid;
	u8 reserved[4];
};


/*
 * rdma function init CNQ parameters
 */
struct rdma_cnq_params
{
	__le16 sb_num /* Status block number used by the queue */;
	u8 sb_index /* Status block index used by the queue */;
	u8 num_pbl_pages /* Number of pages in the PBL allocated for this queue */;
	__le32 reserved;
	struct regpair pbl_base_addr /* Address to the first entry of the queue PBL */;
	__le16 queue_zone_num /* Queue Zone ID used for CNQ consumer update */;
	u8 reserved1[6];
};


/*
 * rdma create cq ramrod data
 */
struct rdma_create_cq_ramrod_data
{
	struct regpair cq_handle;
	struct regpair pbl_addr;
	__le32 max_cqes;
	__le16 pbl_num_pages;
	__le16 dpi;
	u8 is_two_level_pbl;
	u8 cnq_id;
	u8 pbl_log_page_size;
	u8 toggle_bit;
	__le16 int_timeout /* Timeout used for interrupt moderation */;
	__le16 reserved1;
};


/*
 * rdma deregister tid ramrod data
 */
struct rdma_deregister_tid_ramrod_data
{
	__le32 itid;
	__le32 reserved;
};


/*
 * rdma destroy cq output params
 */
struct rdma_destroy_cq_output_params
{
	__le16 cnq_num /* Sequence number of completion notification sent for the cq on the associated CNQ */;
	__le16 reserved0;
	__le32 reserved1;
};


/*
 * rdma destroy cq ramrod data
 */
struct rdma_destroy_cq_ramrod_data
{
	struct regpair output_params_addr;
};


/*
 * RDMA slow path EQ cmd IDs
 */
enum rdma_event_opcode
{
	RDMA_EVENT_UNUSED,
	RDMA_EVENT_FUNC_INIT,
	RDMA_EVENT_FUNC_CLOSE,
	RDMA_EVENT_REGISTER_MR,
	RDMA_EVENT_DEREGISTER_MR,
	RDMA_EVENT_CREATE_CQ,
	RDMA_EVENT_RESIZE_CQ,
	RDMA_EVENT_DESTROY_CQ,
	RDMA_EVENT_CREATE_SRQ,
	RDMA_EVENT_MODIFY_SRQ,
	RDMA_EVENT_DESTROY_SRQ,
	MAX_RDMA_EVENT_OPCODE
};


/*
 * RDMA FW return code for slow path ramrods
 */
enum rdma_fw_return_code
{
	RDMA_RETURN_OK=0,
	RDMA_RETURN_REGISTER_MR_BAD_STATE_ERR,
	RDMA_RETURN_DEREGISTER_MR_BAD_STATE_ERR,
	RDMA_RETURN_RESIZE_CQ_ERR,
	RDMA_RETURN_NIG_DRAIN_REQ,
	MAX_RDMA_FW_RETURN_CODE
};


/*
 * rdma function init header
 */
struct rdma_init_func_hdr
{
	u8 cnq_start_offset /* First RDMA CNQ */;
	u8 num_cnqs /* Number of CNQs */;
	u8 cq_ring_mode /* 0 for 32 bit cq producer and consumer counters and 1 for 16 bit */;
	u8 vf_id /* This field should be assigned to Virtual Function ID if vf_valid == 1. Otherwise its dont care */;
	u8 vf_valid;
	u8 relaxed_ordering /* 1 for using relaxed ordering PCI writes */;
	__le16 first_reg_srq_id /* The SRQ ID of thr first regular (non XRC) SRQ */;
	__le32 reg_srq_base_addr /* Logical base address of first regular (non XRC) SRQ */;
	__le32 reserved;
};


/*
 * rdma function init ramrod data
 */
struct rdma_init_func_ramrod_data
{
	struct rdma_init_func_hdr params_header;
	struct rdma_cnq_params cnq_params[NUM_OF_GLOBAL_QUEUES];
};


/*
 * RDMA ramrod command IDs
 */
enum rdma_ramrod_cmd_id
{
	RDMA_RAMROD_UNUSED,
	RDMA_RAMROD_FUNC_INIT,
	RDMA_RAMROD_FUNC_CLOSE,
	RDMA_RAMROD_REGISTER_MR,
	RDMA_RAMROD_DEREGISTER_MR,
	RDMA_RAMROD_CREATE_CQ,
	RDMA_RAMROD_RESIZE_CQ,
	RDMA_RAMROD_DESTROY_CQ,
	RDMA_RAMROD_CREATE_SRQ,
	RDMA_RAMROD_MODIFY_SRQ,
	RDMA_RAMROD_DESTROY_SRQ,
	MAX_RDMA_RAMROD_CMD_ID
};


/*
 * rdma register tid ramrod data
 */
struct rdma_register_tid_ramrod_data
{
	__le16 flags;
#define RDMA_REGISTER_TID_RAMROD_DATA_PAGE_SIZE_LOG_MASK      0x1F
#define RDMA_REGISTER_TID_RAMROD_DATA_PAGE_SIZE_LOG_SHIFT     0
#define RDMA_REGISTER_TID_RAMROD_DATA_TWO_LEVEL_PBL_MASK      0x1
#define RDMA_REGISTER_TID_RAMROD_DATA_TWO_LEVEL_PBL_SHIFT     5
#define RDMA_REGISTER_TID_RAMROD_DATA_ZERO_BASED_MASK         0x1
#define RDMA_REGISTER_TID_RAMROD_DATA_ZERO_BASED_SHIFT        6
#define RDMA_REGISTER_TID_RAMROD_DATA_PHY_MR_MASK             0x1
#define RDMA_REGISTER_TID_RAMROD_DATA_PHY_MR_SHIFT            7
#define RDMA_REGISTER_TID_RAMROD_DATA_REMOTE_READ_MASK        0x1
#define RDMA_REGISTER_TID_RAMROD_DATA_REMOTE_READ_SHIFT       8
#define RDMA_REGISTER_TID_RAMROD_DATA_REMOTE_WRITE_MASK       0x1
#define RDMA_REGISTER_TID_RAMROD_DATA_REMOTE_WRITE_SHIFT      9
#define RDMA_REGISTER_TID_RAMROD_DATA_REMOTE_ATOMIC_MASK      0x1
#define RDMA_REGISTER_TID_RAMROD_DATA_REMOTE_ATOMIC_SHIFT     10
#define RDMA_REGISTER_TID_RAMROD_DATA_LOCAL_WRITE_MASK        0x1
#define RDMA_REGISTER_TID_RAMROD_DATA_LOCAL_WRITE_SHIFT       11
#define RDMA_REGISTER_TID_RAMROD_DATA_LOCAL_READ_MASK         0x1
#define RDMA_REGISTER_TID_RAMROD_DATA_LOCAL_READ_SHIFT        12
#define RDMA_REGISTER_TID_RAMROD_DATA_ENABLE_MW_BIND_MASK     0x1
#define RDMA_REGISTER_TID_RAMROD_DATA_ENABLE_MW_BIND_SHIFT    13
#define RDMA_REGISTER_TID_RAMROD_DATA_RESERVED_MASK           0x3
#define RDMA_REGISTER_TID_RAMROD_DATA_RESERVED_SHIFT          14
	u8 flags1;
#define RDMA_REGISTER_TID_RAMROD_DATA_PBL_PAGE_SIZE_LOG_MASK  0x1F
#define RDMA_REGISTER_TID_RAMROD_DATA_PBL_PAGE_SIZE_LOG_SHIFT 0
#define RDMA_REGISTER_TID_RAMROD_DATA_TID_TYPE_MASK           0x7
#define RDMA_REGISTER_TID_RAMROD_DATA_TID_TYPE_SHIFT          5
	u8 flags2;
#define RDMA_REGISTER_TID_RAMROD_DATA_DMA_MR_MASK             0x1 /* Bit indicating that this MR is DMA_MR meaning SGEs that use it have the physical address on them */
#define RDMA_REGISTER_TID_RAMROD_DATA_DMA_MR_SHIFT            0
#define RDMA_REGISTER_TID_RAMROD_DATA_DIF_ON_HOST_FLG_MASK    0x1 /* Bit indicating that this MR has DIF protection enabled. */
#define RDMA_REGISTER_TID_RAMROD_DATA_DIF_ON_HOST_FLG_SHIFT   1
#define RDMA_REGISTER_TID_RAMROD_DATA_RESERVED1_MASK          0x3F
#define RDMA_REGISTER_TID_RAMROD_DATA_RESERVED1_SHIFT         2
	u8 key;
	u8 length_hi;
	u8 vf_id /* This field should be assigned to Virtual Function ID if vf_valid == 1. Otherwise its dont care */;
	u8 vf_valid;
	__le16 pd;
	__le16 reserved2;
	__le32 length_lo /* lower 32 bits of the registered MR length. */;
	__le32 itid;
	__le32 reserved3;
	struct regpair va;
	struct regpair pbl_base;
	struct regpair dif_error_addr /* DIF TX IO writes error information to this location when memory region is invalidated. */;
	struct regpair dif_runt_addr /* DIF RX IO writes runt value to this location when last RDMA Read of the IO has completed. */;
	__le32 reserved4[2];
};


/*
 * rdma resize cq output params
 */
struct rdma_resize_cq_output_params
{
	__le32 old_cq_cons /* cq consumer value on old PBL */;
	__le32 old_cq_prod /* cq producer value on old PBL */;
};


/*
 * rdma resize cq ramrod data
 */
struct rdma_resize_cq_ramrod_data
{
	u8 flags;
#define RDMA_RESIZE_CQ_RAMROD_DATA_TOGGLE_BIT_MASK        0x1
#define RDMA_RESIZE_CQ_RAMROD_DATA_TOGGLE_BIT_SHIFT       0
#define RDMA_RESIZE_CQ_RAMROD_DATA_IS_TWO_LEVEL_PBL_MASK  0x1
#define RDMA_RESIZE_CQ_RAMROD_DATA_IS_TWO_LEVEL_PBL_SHIFT 1
#define RDMA_RESIZE_CQ_RAMROD_DATA_RESERVED_MASK          0x3F
#define RDMA_RESIZE_CQ_RAMROD_DATA_RESERVED_SHIFT         2
	u8 pbl_log_page_size;
	__le16 pbl_num_pages;
	__le32 max_cqes;
	struct regpair pbl_addr;
	struct regpair output_params_addr;
};


/*
 * The rdma SRQ context
 */
struct rdma_srq_context
{
	struct regpair temp[8];
};


/*
 * rdma create qp requester ramrod data
 */
struct rdma_srq_create_ramrod_data
{
	u8 flags;
#define RDMA_SRQ_CREATE_RAMROD_DATA_XRC_FLAG_MASK         0x1
#define RDMA_SRQ_CREATE_RAMROD_DATA_XRC_FLAG_SHIFT        0
#define RDMA_SRQ_CREATE_RAMROD_DATA_RESERVED_KEY_EN_MASK  0x1 /* Only applicable when xrc_flag is set */
#define RDMA_SRQ_CREATE_RAMROD_DATA_RESERVED_KEY_EN_SHIFT 1
#define RDMA_SRQ_CREATE_RAMROD_DATA_RESERVED1_MASK        0x3F
#define RDMA_SRQ_CREATE_RAMROD_DATA_RESERVED1_SHIFT       2
	u8 reserved2;
	__le16 xrc_domain /* Only applicable when xrc_flag is set */;
	__le32 xrc_srq_cq_cid /* Only applicable when xrc_flag is set */;
	struct regpair pbl_base_addr /* SRQ PBL base address */;
	__le16 pages_in_srq_pbl /* Number of pages in PBL */;
	__le16 pd_id;
	struct rdma_srq_id srq_id /* SRQ Index */;
	__le16 page_size /* Page size in SGEs(16 bytes) elements. Supports up to 2M bytes page size */;
	__le16 reserved3;
	__le32 reserved4;
	struct regpair producers_addr /* SRQ PBL base address */;
};


/*
 * rdma create qp requester ramrod data
 */
struct rdma_srq_destroy_ramrod_data
{
	struct rdma_srq_id srq_id /* SRQ Index */;
	__le32 reserved;
};


/*
 * rdma create qp requester ramrod data
 */
struct rdma_srq_modify_ramrod_data
{
	struct rdma_srq_id srq_id /* SRQ Index */;
	__le32 wqe_limit;
};


/*
 * RDMA Tid type enumeration (for register_tid ramrod)
 */
enum rdma_tid_type
{
	RDMA_TID_REGISTERED_MR,
	RDMA_TID_FMR,
	RDMA_TID_MW_TYPE1,
	RDMA_TID_MW_TYPE2A,
	MAX_RDMA_TID_TYPE
};


/*
 * The rdma XRC SRQ context
 */
struct rdma_xrc_srq_context
{
	struct regpair temp[9];
};




struct E4XstormRoceConnAgCtxDqExtLdPart
{
	u8 reserved0 /* cdu_validation */;
	u8 state /* state */;
	u8 flags0;
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_EXIST_IN_QM0_MASK      0x1 /* exist_in_qm0 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_EXIST_IN_QM0_SHIFT     0
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT1_MASK              0x1 /* exist_in_qm1 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT1_SHIFT             1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT2_MASK              0x1 /* exist_in_qm2 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT2_SHIFT             2
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_EXIST_IN_QM3_MASK      0x1 /* exist_in_qm3 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_EXIST_IN_QM3_SHIFT     3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT4_MASK              0x1 /* bit4 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT4_SHIFT             4
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT5_MASK              0x1 /* cf_array_active */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT5_SHIFT             5
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT6_MASK              0x1 /* bit6 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT6_SHIFT             6
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT7_MASK              0x1 /* bit7 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT7_SHIFT             7
	u8 flags1;
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT8_MASK              0x1 /* bit8 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT8_SHIFT             0
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT9_MASK              0x1 /* bit9 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT9_SHIFT             1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT10_MASK             0x1 /* bit10 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT10_SHIFT            2
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT11_MASK             0x1 /* bit11 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT11_SHIFT            3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT12_MASK             0x1 /* bit12 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT12_SHIFT            4
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_MSTORM_FLUSH_MASK      0x1 /* bit13 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_MSTORM_FLUSH_SHIFT     5
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT14_MASK             0x1 /* bit14 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT14_SHIFT            6
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_YSTORM_FLUSH_MASK      0x1 /* bit15 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_YSTORM_FLUSH_SHIFT     7
	u8 flags2;
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF0_MASK               0x3 /* timer0cf */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF0_SHIFT              0
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF1_MASK               0x3 /* timer1cf */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF1_SHIFT              2
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF2_MASK               0x3 /* timer2cf */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF2_SHIFT              4
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF3_MASK               0x3 /* timer_stop_all */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF3_SHIFT              6
	u8 flags3;
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF4_MASK               0x3 /* cf4 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF4_SHIFT              0
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF5_MASK               0x3 /* cf5 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF5_SHIFT              2
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF6_MASK               0x3 /* cf6 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF6_SHIFT              4
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_FLUSH_Q0_CF_MASK       0x3 /* cf7 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_FLUSH_Q0_CF_SHIFT      6
	u8 flags4;
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF8_MASK               0x3 /* cf8 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF8_SHIFT              0
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF9_MASK               0x3 /* cf9 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF9_SHIFT              2
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF10_MASK              0x3 /* cf10 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF10_SHIFT             4
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF11_MASK              0x3 /* cf11 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF11_SHIFT             6
	u8 flags5;
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF12_MASK              0x3 /* cf12 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF12_SHIFT             0
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF13_MASK              0x3 /* cf13 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF13_SHIFT             2
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF14_MASK              0x3 /* cf14 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF14_SHIFT             4
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF15_MASK              0x3 /* cf15 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF15_SHIFT             6
	u8 flags6;
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF16_MASK              0x3 /* cf16 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF16_SHIFT             0
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF17_MASK              0x3 /* cf_array_cf */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF17_SHIFT             2
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF18_MASK              0x3 /* cf18 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF18_SHIFT             4
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF19_MASK              0x3 /* cf19 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF19_SHIFT             6
	u8 flags7;
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF20_MASK              0x3 /* cf20 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF20_SHIFT             0
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF21_MASK              0x3 /* cf21 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF21_SHIFT             2
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_SLOW_PATH_MASK         0x3 /* cf22 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_SLOW_PATH_SHIFT        4
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF0EN_MASK             0x1 /* cf0en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF0EN_SHIFT            6
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF1EN_MASK             0x1 /* cf1en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF1EN_SHIFT            7
	u8 flags8;
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF2EN_MASK             0x1 /* cf2en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF2EN_SHIFT            0
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF3EN_MASK             0x1 /* cf3en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF3EN_SHIFT            1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF4EN_MASK             0x1 /* cf4en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF4EN_SHIFT            2
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF5EN_MASK             0x1 /* cf5en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF5EN_SHIFT            3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF6EN_MASK             0x1 /* cf6en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF6EN_SHIFT            4
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_FLUSH_Q0_CF_EN_MASK    0x1 /* cf7en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_FLUSH_Q0_CF_EN_SHIFT   5
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF8EN_MASK             0x1 /* cf8en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF8EN_SHIFT            6
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF9EN_MASK             0x1 /* cf9en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF9EN_SHIFT            7
	u8 flags9;
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF10EN_MASK            0x1 /* cf10en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF10EN_SHIFT           0
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF11EN_MASK            0x1 /* cf11en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF11EN_SHIFT           1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF12EN_MASK            0x1 /* cf12en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF12EN_SHIFT           2
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF13EN_MASK            0x1 /* cf13en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF13EN_SHIFT           3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF14EN_MASK            0x1 /* cf14en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF14EN_SHIFT           4
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF15EN_MASK            0x1 /* cf15en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF15EN_SHIFT           5
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF16EN_MASK            0x1 /* cf16en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF16EN_SHIFT           6
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF17EN_MASK            0x1 /* cf_array_cf_en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF17EN_SHIFT           7
	u8 flags10;
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF18EN_MASK            0x1 /* cf18en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF18EN_SHIFT           0
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF19EN_MASK            0x1 /* cf19en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF19EN_SHIFT           1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF20EN_MASK            0x1 /* cf20en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF20EN_SHIFT           2
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF21EN_MASK            0x1 /* cf21en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF21EN_SHIFT           3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_SLOW_PATH_EN_MASK      0x1 /* cf22en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_SLOW_PATH_EN_SHIFT     4
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF23EN_MASK            0x1 /* cf23en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF23EN_SHIFT           5
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE0EN_MASK           0x1 /* rule0en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE0EN_SHIFT          6
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE1EN_MASK           0x1 /* rule1en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE1EN_SHIFT          7
	u8 flags11;
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE2EN_MASK           0x1 /* rule2en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE2EN_SHIFT          0
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE3EN_MASK           0x1 /* rule3en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE3EN_SHIFT          1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE4EN_MASK           0x1 /* rule4en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE4EN_SHIFT          2
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE5EN_MASK           0x1 /* rule5en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE5EN_SHIFT          3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE6EN_MASK           0x1 /* rule6en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE6EN_SHIFT          4
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE7EN_MASK           0x1 /* rule7en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE7EN_SHIFT          5
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED1_MASK      0x1 /* rule8en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED1_SHIFT     6
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE9EN_MASK           0x1 /* rule9en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE9EN_SHIFT          7
	u8 flags12;
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE10EN_MASK          0x1 /* rule10en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE10EN_SHIFT         0
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE11EN_MASK          0x1 /* rule11en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE11EN_SHIFT         1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED2_MASK      0x1 /* rule12en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED2_SHIFT     2
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED3_MASK      0x1 /* rule13en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED3_SHIFT     3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE14EN_MASK          0x1 /* rule14en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE14EN_SHIFT         4
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE15EN_MASK          0x1 /* rule15en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE15EN_SHIFT         5
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE16EN_MASK          0x1 /* rule16en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE16EN_SHIFT         6
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE17EN_MASK          0x1 /* rule17en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE17EN_SHIFT         7
	u8 flags13;
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE18EN_MASK          0x1 /* rule18en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE18EN_SHIFT         0
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE19EN_MASK          0x1 /* rule19en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RULE19EN_SHIFT         1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED4_MASK      0x1 /* rule20en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED4_SHIFT     2
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED5_MASK      0x1 /* rule21en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED5_SHIFT     3
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED6_MASK      0x1 /* rule22en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED6_SHIFT     4
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED7_MASK      0x1 /* rule23en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED7_SHIFT     5
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED8_MASK      0x1 /* rule24en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED8_SHIFT     6
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED9_MASK      0x1 /* rule25en */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_A0_RESERVED9_SHIFT     7
	u8 flags14;
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_MIGRATION_MASK         0x1 /* bit16 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_MIGRATION_SHIFT        0
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT17_MASK             0x1 /* bit17 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_BIT17_SHIFT            1
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_DPM_PORT_NUM_MASK      0x3 /* bit18 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_DPM_PORT_NUM_SHIFT     2
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RESERVED_MASK          0x1 /* bit20 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_RESERVED_SHIFT         4
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_ROCE_EDPM_ENABLE_MASK  0x1 /* bit21 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_ROCE_EDPM_ENABLE_SHIFT 5
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF23_MASK              0x3 /* cf23 */
#define E4XSTORMROCECONNAGCTXDQEXTLDPART_CF23_SHIFT             6
	u8 byte2 /* byte2 */;
	__le16 physical_q0 /* physical_q0 */;
	__le16 word1 /* physical_q1 */;
	__le16 word2 /* physical_q2 */;
	__le16 word3 /* word3 */;
	__le16 word4 /* word4 */;
	__le16 word5 /* word5 */;
	__le16 conn_dpi /* conn_dpi */;
	u8 byte3 /* byte3 */;
	u8 byte4 /* byte4 */;
	u8 byte5 /* byte5 */;
	u8 byte6 /* byte6 */;
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
	__le32 reg2 /* reg2 */;
	__le32 snd_nxt_psn /* reg3 */;
	__le32 reg4 /* reg4 */;
};


struct e4_mstorm_rdma_conn_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 byte1 /* state */;
	u8 flags0;
#define E4_MSTORM_RDMA_CONN_AG_CTX_BIT0_MASK     0x1 /* exist_in_qm0 */
#define E4_MSTORM_RDMA_CONN_AG_CTX_BIT0_SHIFT    0
#define E4_MSTORM_RDMA_CONN_AG_CTX_BIT1_MASK     0x1 /* exist_in_qm1 */
#define E4_MSTORM_RDMA_CONN_AG_CTX_BIT1_SHIFT    1
#define E4_MSTORM_RDMA_CONN_AG_CTX_CF0_MASK      0x3 /* cf0 */
#define E4_MSTORM_RDMA_CONN_AG_CTX_CF0_SHIFT     2
#define E4_MSTORM_RDMA_CONN_AG_CTX_CF1_MASK      0x3 /* cf1 */
#define E4_MSTORM_RDMA_CONN_AG_CTX_CF1_SHIFT     4
#define E4_MSTORM_RDMA_CONN_AG_CTX_CF2_MASK      0x3 /* cf2 */
#define E4_MSTORM_RDMA_CONN_AG_CTX_CF2_SHIFT     6
	u8 flags1;
#define E4_MSTORM_RDMA_CONN_AG_CTX_CF0EN_MASK    0x1 /* cf0en */
#define E4_MSTORM_RDMA_CONN_AG_CTX_CF0EN_SHIFT   0
#define E4_MSTORM_RDMA_CONN_AG_CTX_CF1EN_MASK    0x1 /* cf1en */
#define E4_MSTORM_RDMA_CONN_AG_CTX_CF1EN_SHIFT   1
#define E4_MSTORM_RDMA_CONN_AG_CTX_CF2EN_MASK    0x1 /* cf2en */
#define E4_MSTORM_RDMA_CONN_AG_CTX_CF2EN_SHIFT   2
#define E4_MSTORM_RDMA_CONN_AG_CTX_RULE0EN_MASK  0x1 /* rule0en */
#define E4_MSTORM_RDMA_CONN_AG_CTX_RULE0EN_SHIFT 3
#define E4_MSTORM_RDMA_CONN_AG_CTX_RULE1EN_MASK  0x1 /* rule1en */
#define E4_MSTORM_RDMA_CONN_AG_CTX_RULE1EN_SHIFT 4
#define E4_MSTORM_RDMA_CONN_AG_CTX_RULE2EN_MASK  0x1 /* rule2en */
#define E4_MSTORM_RDMA_CONN_AG_CTX_RULE2EN_SHIFT 5
#define E4_MSTORM_RDMA_CONN_AG_CTX_RULE3EN_MASK  0x1 /* rule3en */
#define E4_MSTORM_RDMA_CONN_AG_CTX_RULE3EN_SHIFT 6
#define E4_MSTORM_RDMA_CONN_AG_CTX_RULE4EN_MASK  0x1 /* rule4en */
#define E4_MSTORM_RDMA_CONN_AG_CTX_RULE4EN_SHIFT 7
	__le16 word0 /* word0 */;
	__le16 word1 /* word1 */;
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
};



struct e4_tstorm_rdma_conn_ag_ctx
{
	u8 reserved0 /* cdu_validation */;
	u8 byte1 /* state */;
	u8 flags0;
#define E4_TSTORM_RDMA_CONN_AG_CTX_EXIST_IN_QM0_MASK          0x1 /* exist_in_qm0 */
#define E4_TSTORM_RDMA_CONN_AG_CTX_EXIST_IN_QM0_SHIFT         0
#define E4_TSTORM_RDMA_CONN_AG_CTX_BIT1_MASK                  0x1 /* exist_in_qm1 */
#define E4_TSTORM_RDMA_CONN_AG_CTX_BIT1_SHIFT                 1
#define E4_TSTORM_RDMA_CONN_AG_CTX_BIT2_MASK                  0x1 /* bit2 */
#define E4_TSTORM_RDMA_CONN_AG_CTX_BIT2_SHIFT                 2
#define E4_TSTORM_RDMA_CONN_AG_CTX_BIT3_MASK                  0x1 /* bit3 */
#define E4_TSTORM_RDMA_CONN_AG_CTX_BIT3_SHIFT                 3
#define E4_TSTORM_RDMA_CONN_AG_CTX_BIT4_MASK                  0x1 /* bit4 */
#define E4_TSTORM_RDMA_CONN_AG_CTX_BIT4_SHIFT                 4
#define E4_TSTORM_RDMA_CONN_AG_CTX_BIT5_MASK                  0x1 /* bit5 */
#define E4_TSTORM_RDMA_CONN_AG_CTX_BIT5_SHIFT                 5
#define E4_TSTORM_RDMA_CONN_AG_CTX_CF0_MASK                   0x3 /* timer0cf */
#define E4_TSTORM_RDMA_CONN_AG_CTX_CF0_SHIFT                  6
	u8 flags1;
#define E4_TSTORM_RDMA_CONN_AG_CTX_CF1_MASK                   0x3 /* timer1cf */
#define E4_TSTORM_RDMA_CONN_AG_CTX_CF1_SHIFT                  0
#define E4_TSTORM_RDMA_CONN_AG_CTX_CF2_MASK                   0x3 /* timer2cf */
#define E4_TSTORM_RDMA_CONN_AG_CTX_CF2_SHIFT                  2
#define E4_TSTORM_RDMA_CONN_AG_CTX_TIMER_STOP_ALL_CF_MASK     0x3 /* timer_stop_all */
#define E4_TSTORM_RDMA_CONN_AG_CTX_TIMER_STOP_ALL_CF_SHIFT    4
#define E4_TSTORM_RDMA_CONN_AG_CTX_FLUSH_Q0_CF_MASK           0x3 /* cf4 */
#define E4_TSTORM_RDMA_CONN_AG_CTX_FLUSH_Q0_CF_SHIFT          6
	u8 flags2;
#define E4_TSTORM_RDMA_CONN_AG_CTX_MSTORM_FLUSH_CF_MASK       0x3 /* cf5 */
#define E4_TSTORM_RDMA_CONN_AG_CTX_MSTORM_FLUSH_CF_SHIFT      0
#define E4_TSTORM_RDMA_CONN_AG_CTX_CF6_MASK                   0x3 /* cf6 */
#define E4_TSTORM_RDMA_CONN_AG_CTX_CF6_SHIFT                  2
#define E4_TSTORM_RDMA_CONN_AG_CTX_CF7_MASK                   0x3 /* cf7 */
#define E4_TSTORM_RDMA_CONN_AG_CTX_CF7_SHIFT                  4
#define E4_TSTORM_RDMA_CONN_AG_CTX_CF8_MASK                   0x3 /* cf8 */
#define E4_TSTORM_RDMA_CONN_AG_CTX_CF8_SHIFT                  6
	u8 flags3;
#define E4_TSTORM_RDMA_CONN_AG_CTX_CF9_MASK                   0x3 /* cf9 */
#define E4_TSTORM_RDMA_CONN_AG_CTX_CF9_SHIFT                  0
#define E4_TSTORM_RDMA_CONN_AG_CTX_CF10_MASK                  0x3 /* cf10 */
#define E4_TSTORM_RDMA_CONN_AG_CTX_CF10_SHIFT                 2
#define E4_TSTORM_RDMA_CONN_AG_CTX_CF0EN_MASK                 0x1 /* cf0en */
#define E4_TSTORM_RDMA_CONN_AG_CTX_CF0EN_SHIFT                4
#define E4_TSTORM_RDMA_CONN_AG_CTX_CF1EN_MASK                 0x1 /* cf1en */
#define E4_TSTORM_RDMA_CONN_AG_CTX_CF1EN_SHIFT                5
#define E4_TSTORM_RDMA_CONN_AG_CTX_CF2EN_MASK                 0x1 /* cf2en */
#define E4_TSTORM_RDMA_CONN_AG_CTX_CF2EN_SHIFT                6
#define E4_TSTORM_RDMA_CONN_AG_CTX_TIMER_STOP_ALL_CF_EN_MASK  0x1 /* cf3en */
#define E4_TSTORM_RDMA_CONN_AG_CTX_TIMER_STOP_ALL_CF_EN_SHIFT 7
	u8 flags4;
#define E4_TSTORM_RDMA_CONN_AG_CTX_FLUSH_Q0_CF_EN_MASK        0x1 /* cf4en */
#define E4_TSTORM_RDMA_CONN_AG_CTX_FLUSH_Q0_CF_EN_SHIFT       0
#define E4_TSTORM_RDMA_CONN_AG_CTX_MSTORM_FLUSH_CF_EN_MASK    0x1 /* cf5en */
#define E4_TSTORM_RDMA_CONN_AG_CTX_MSTORM_FLUSH_CF_EN_SHIFT   1
#define E4_TSTORM_RDMA_CONN_AG_CTX_CF6EN_MASK                 0x1 /* cf6en */
#define E4_TSTORM_RDMA_CONN_AG_CTX_CF6EN_SHIFT                2
#define E4_TSTORM_RDMA_CONN_AG_CTX_CF7EN_MASK                 0x1 /* cf7en */
#define E4_TSTORM_RDMA_CONN_AG_CTX_CF7EN_SHIFT                3
#define E4_TSTORM_RDMA_CONN_AG_CTX_CF8EN_MASK                 0x1 /* cf8en */
#define E4_TSTORM_RDMA_CONN_AG_CTX_CF8EN_SHIFT                4
#define E4_TSTORM_RDMA_CONN_AG_CTX_CF9EN_MASK                 0x1 /* cf9en */
#define E4_TSTORM_RDMA_CONN_AG_CTX_CF9EN_SHIFT                5
#define E4_TSTORM_RDMA_CONN_AG_CTX_CF10EN_MASK                0x1 /* cf10en */
#define E4_TSTORM_RDMA_CONN_AG_CTX_CF10EN_SHIFT               6
#define E4_TSTORM_RDMA_CONN_AG_CTX_RULE0EN_MASK               0x1 /* rule0en */
#define E4_TSTORM_RDMA_CONN_AG_CTX_RULE0EN_SHIFT              7
	u8 flags5;
#define E4_TSTORM_RDMA_CONN_AG_CTX_RULE1EN_MASK               0x1 /* rule1en */
#define E4_TSTORM_RDMA_CONN_AG_CTX_RULE1EN_SHIFT              0
#define E4_TSTORM_RDMA_CONN_AG_CTX_RULE2EN_MASK               0x1 /* rule2en */
#define E4_TSTORM_RDMA_CONN_AG_CTX_RULE2EN_SHIFT              1
#define E4_TSTORM_RDMA_CONN_AG_CTX_RULE3EN_MASK               0x1 /* rule3en */
#define E4_TSTORM_RDMA_CONN_AG_CTX_RULE3EN_SHIFT              2
#define E4_TSTORM_RDMA_CONN_AG_CTX_RULE4EN_MASK               0x1 /* rule4en */
#define E4_TSTORM_RDMA_CONN_AG_CTX_RULE4EN_SHIFT              3
#define E4_TSTORM_RDMA_CONN_AG_CTX_RULE5EN_MASK               0x1 /* rule5en */
#define E4_TSTORM_RDMA_CONN_AG_CTX_RULE5EN_SHIFT              4
#define E4_TSTORM_RDMA_CONN_AG_CTX_RULE6EN_MASK               0x1 /* rule6en */
#define E4_TSTORM_RDMA_CONN_AG_CTX_RULE6EN_SHIFT              5
#define E4_TSTORM_RDMA_CONN_AG_CTX_RULE7EN_MASK               0x1 /* rule7en */
#define E4_TSTORM_RDMA_CONN_AG_CTX_RULE7EN_SHIFT              6
#define E4_TSTORM_RDMA_CONN_AG_CTX_RULE8EN_MASK               0x1 /* rule8en */
#define E4_TSTORM_RDMA_CONN_AG_CTX_RULE8EN_SHIFT              7
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
	__le32 reg2 /* reg2 */;
	__le32 reg3 /* reg3 */;
	__le32 reg4 /* reg4 */;
	__le32 reg5 /* reg5 */;
	__le32 reg6 /* reg6 */;
	__le32 reg7 /* reg7 */;
	__le32 reg8 /* reg8 */;
	u8 byte2 /* byte2 */;
	u8 byte3 /* byte3 */;
	__le16 word0 /* word0 */;
	u8 byte4 /* byte4 */;
	u8 byte5 /* byte5 */;
	__le16 word1 /* word1 */;
	__le16 word2 /* conn_dpi */;
	__le16 word3 /* word3 */;
	__le32 reg9 /* reg9 */;
	__le32 reg10 /* reg10 */;
};


struct e4_tstorm_rdma_task_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 byte1 /* state */;
	__le16 word0 /* icid */;
	u8 flags0;
#define E4_TSTORM_RDMA_TASK_AG_CTX_NIBBLE0_MASK  0xF /* connection_type */
#define E4_TSTORM_RDMA_TASK_AG_CTX_NIBBLE0_SHIFT 0
#define E4_TSTORM_RDMA_TASK_AG_CTX_BIT0_MASK     0x1 /* exist_in_qm0 */
#define E4_TSTORM_RDMA_TASK_AG_CTX_BIT0_SHIFT    4
#define E4_TSTORM_RDMA_TASK_AG_CTX_BIT1_MASK     0x1 /* exist_in_qm1 */
#define E4_TSTORM_RDMA_TASK_AG_CTX_BIT1_SHIFT    5
#define E4_TSTORM_RDMA_TASK_AG_CTX_BIT2_MASK     0x1 /* bit2 */
#define E4_TSTORM_RDMA_TASK_AG_CTX_BIT2_SHIFT    6
#define E4_TSTORM_RDMA_TASK_AG_CTX_BIT3_MASK     0x1 /* bit3 */
#define E4_TSTORM_RDMA_TASK_AG_CTX_BIT3_SHIFT    7
	u8 flags1;
#define E4_TSTORM_RDMA_TASK_AG_CTX_BIT4_MASK     0x1 /* bit4 */
#define E4_TSTORM_RDMA_TASK_AG_CTX_BIT4_SHIFT    0
#define E4_TSTORM_RDMA_TASK_AG_CTX_BIT5_MASK     0x1 /* bit5 */
#define E4_TSTORM_RDMA_TASK_AG_CTX_BIT5_SHIFT    1
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF0_MASK      0x3 /* timer0cf */
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF0_SHIFT     2
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF1_MASK      0x3 /* timer1cf */
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF1_SHIFT     4
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF2_MASK      0x3 /* timer2cf */
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF2_SHIFT     6
	u8 flags2;
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF3_MASK      0x3 /* timer_stop_all */
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF3_SHIFT     0
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF4_MASK      0x3 /* cf4 */
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF4_SHIFT     2
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF5_MASK      0x3 /* cf5 */
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF5_SHIFT     4
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF6_MASK      0x3 /* cf6 */
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF6_SHIFT     6
	u8 flags3;
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF7_MASK      0x3 /* cf7 */
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF7_SHIFT     0
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF0EN_MASK    0x1 /* cf0en */
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF0EN_SHIFT   2
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF1EN_MASK    0x1 /* cf1en */
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF1EN_SHIFT   3
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF2EN_MASK    0x1 /* cf2en */
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF2EN_SHIFT   4
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF3EN_MASK    0x1 /* cf3en */
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF3EN_SHIFT   5
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF4EN_MASK    0x1 /* cf4en */
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF4EN_SHIFT   6
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF5EN_MASK    0x1 /* cf5en */
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF5EN_SHIFT   7
	u8 flags4;
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF6EN_MASK    0x1 /* cf6en */
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF6EN_SHIFT   0
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF7EN_MASK    0x1 /* cf7en */
#define E4_TSTORM_RDMA_TASK_AG_CTX_CF7EN_SHIFT   1
#define E4_TSTORM_RDMA_TASK_AG_CTX_RULE0EN_MASK  0x1 /* rule0en */
#define E4_TSTORM_RDMA_TASK_AG_CTX_RULE0EN_SHIFT 2
#define E4_TSTORM_RDMA_TASK_AG_CTX_RULE1EN_MASK  0x1 /* rule1en */
#define E4_TSTORM_RDMA_TASK_AG_CTX_RULE1EN_SHIFT 3
#define E4_TSTORM_RDMA_TASK_AG_CTX_RULE2EN_MASK  0x1 /* rule2en */
#define E4_TSTORM_RDMA_TASK_AG_CTX_RULE2EN_SHIFT 4
#define E4_TSTORM_RDMA_TASK_AG_CTX_RULE3EN_MASK  0x1 /* rule3en */
#define E4_TSTORM_RDMA_TASK_AG_CTX_RULE3EN_SHIFT 5
#define E4_TSTORM_RDMA_TASK_AG_CTX_RULE4EN_MASK  0x1 /* rule4en */
#define E4_TSTORM_RDMA_TASK_AG_CTX_RULE4EN_SHIFT 6
#define E4_TSTORM_RDMA_TASK_AG_CTX_RULE5EN_MASK  0x1 /* rule5en */
#define E4_TSTORM_RDMA_TASK_AG_CTX_RULE5EN_SHIFT 7
	u8 byte2 /* byte2 */;
	__le16 word1 /* word1 */;
	__le32 reg0 /* reg0 */;
	u8 byte3 /* byte3 */;
	u8 byte4 /* byte4 */;
	__le16 word2 /* word2 */;
	__le16 word3 /* word3 */;
	__le16 word4 /* word4 */;
	__le32 reg1 /* reg1 */;
	__le32 reg2 /* reg2 */;
};


struct e4_ustorm_rdma_conn_ag_ctx
{
	u8 reserved /* cdu_validation */;
	u8 byte1 /* state */;
	u8 flags0;
#define E4_USTORM_RDMA_CONN_AG_CTX_EXIST_IN_QM0_MASK     0x1 /* exist_in_qm0 */
#define E4_USTORM_RDMA_CONN_AG_CTX_EXIST_IN_QM0_SHIFT    0
#define E4_USTORM_RDMA_CONN_AG_CTX_BIT1_MASK             0x1 /* exist_in_qm1 */
#define E4_USTORM_RDMA_CONN_AG_CTX_BIT1_SHIFT            1
#define E4_USTORM_RDMA_CONN_AG_CTX_FLUSH_Q0_CF_MASK      0x3 /* timer0cf */
#define E4_USTORM_RDMA_CONN_AG_CTX_FLUSH_Q0_CF_SHIFT     2
#define E4_USTORM_RDMA_CONN_AG_CTX_CF1_MASK              0x3 /* timer1cf */
#define E4_USTORM_RDMA_CONN_AG_CTX_CF1_SHIFT             4
#define E4_USTORM_RDMA_CONN_AG_CTX_CF2_MASK              0x3 /* timer2cf */
#define E4_USTORM_RDMA_CONN_AG_CTX_CF2_SHIFT             6
	u8 flags1;
#define E4_USTORM_RDMA_CONN_AG_CTX_CF3_MASK              0x3 /* timer_stop_all */
#define E4_USTORM_RDMA_CONN_AG_CTX_CF3_SHIFT             0
#define E4_USTORM_RDMA_CONN_AG_CTX_CQ_ARM_SE_CF_MASK     0x3 /* cf4 */
#define E4_USTORM_RDMA_CONN_AG_CTX_CQ_ARM_SE_CF_SHIFT    2
#define E4_USTORM_RDMA_CONN_AG_CTX_CQ_ARM_CF_MASK        0x3 /* cf5 */
#define E4_USTORM_RDMA_CONN_AG_CTX_CQ_ARM_CF_SHIFT       4
#define E4_USTORM_RDMA_CONN_AG_CTX_CF6_MASK              0x3 /* cf6 */
#define E4_USTORM_RDMA_CONN_AG_CTX_CF6_SHIFT             6
	u8 flags2;
#define E4_USTORM_RDMA_CONN_AG_CTX_FLUSH_Q0_CF_EN_MASK   0x1 /* cf0en */
#define E4_USTORM_RDMA_CONN_AG_CTX_FLUSH_Q0_CF_EN_SHIFT  0
#define E4_USTORM_RDMA_CONN_AG_CTX_CF1EN_MASK            0x1 /* cf1en */
#define E4_USTORM_RDMA_CONN_AG_CTX_CF1EN_SHIFT           1
#define E4_USTORM_RDMA_CONN_AG_CTX_CF2EN_MASK            0x1 /* cf2en */
#define E4_USTORM_RDMA_CONN_AG_CTX_CF2EN_SHIFT           2
#define E4_USTORM_RDMA_CONN_AG_CTX_CF3EN_MASK            0x1 /* cf3en */
#define E4_USTORM_RDMA_CONN_AG_CTX_CF3EN_SHIFT           3
#define E4_USTORM_RDMA_CONN_AG_CTX_CQ_ARM_SE_CF_EN_MASK  0x1 /* cf4en */
#define E4_USTORM_RDMA_CONN_AG_CTX_CQ_ARM_SE_CF_EN_SHIFT 4
#define E4_USTORM_RDMA_CONN_AG_CTX_CQ_ARM_CF_EN_MASK     0x1 /* cf5en */
#define E4_USTORM_RDMA_CONN_AG_CTX_CQ_ARM_CF_EN_SHIFT    5
#define E4_USTORM_RDMA_CONN_AG_CTX_CF6EN_MASK            0x1 /* cf6en */
#define E4_USTORM_RDMA_CONN_AG_CTX_CF6EN_SHIFT           6
#define E4_USTORM_RDMA_CONN_AG_CTX_CQ_SE_EN_MASK         0x1 /* rule0en */
#define E4_USTORM_RDMA_CONN_AG_CTX_CQ_SE_EN_SHIFT        7
	u8 flags3;
#define E4_USTORM_RDMA_CONN_AG_CTX_CQ_EN_MASK            0x1 /* rule1en */
#define E4_USTORM_RDMA_CONN_AG_CTX_CQ_EN_SHIFT           0
#define E4_USTORM_RDMA_CONN_AG_CTX_RULE2EN_MASK          0x1 /* rule2en */
#define E4_USTORM_RDMA_CONN_AG_CTX_RULE2EN_SHIFT         1
#define E4_USTORM_RDMA_CONN_AG_CTX_RULE3EN_MASK          0x1 /* rule3en */
#define E4_USTORM_RDMA_CONN_AG_CTX_RULE3EN_SHIFT         2
#define E4_USTORM_RDMA_CONN_AG_CTX_RULE4EN_MASK          0x1 /* rule4en */
#define E4_USTORM_RDMA_CONN_AG_CTX_RULE4EN_SHIFT         3
#define E4_USTORM_RDMA_CONN_AG_CTX_RULE5EN_MASK          0x1 /* rule5en */
#define E4_USTORM_RDMA_CONN_AG_CTX_RULE5EN_SHIFT         4
#define E4_USTORM_RDMA_CONN_AG_CTX_RULE6EN_MASK          0x1 /* rule6en */
#define E4_USTORM_RDMA_CONN_AG_CTX_RULE6EN_SHIFT         5
#define E4_USTORM_RDMA_CONN_AG_CTX_RULE7EN_MASK          0x1 /* rule7en */
#define E4_USTORM_RDMA_CONN_AG_CTX_RULE7EN_SHIFT         6
#define E4_USTORM_RDMA_CONN_AG_CTX_RULE8EN_MASK          0x1 /* rule8en */
#define E4_USTORM_RDMA_CONN_AG_CTX_RULE8EN_SHIFT         7
	u8 byte2 /* byte2 */;
	u8 byte3 /* byte3 */;
	__le16 conn_dpi /* conn_dpi */;
	__le16 word1 /* word1 */;
	__le32 cq_cons /* reg0 */;
	__le32 cq_se_prod /* reg1 */;
	__le32 cq_prod /* reg2 */;
	__le32 reg3 /* reg3 */;
	__le16 int_timeout /* word2 */;
	__le16 word3 /* word3 */;
};



struct e4_xstorm_rdma_conn_ag_ctx
{
	u8 reserved0 /* cdu_validation */;
	u8 state /* state */;
	u8 flags0;
#define E4_XSTORM_RDMA_CONN_AG_CTX_EXIST_IN_QM0_MASK      0x1 /* exist_in_qm0 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_EXIST_IN_QM0_SHIFT     0
#define E4_XSTORM_RDMA_CONN_AG_CTX_BIT1_MASK              0x1 /* exist_in_qm1 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_BIT1_SHIFT             1
#define E4_XSTORM_RDMA_CONN_AG_CTX_BIT2_MASK              0x1 /* exist_in_qm2 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_BIT2_SHIFT             2
#define E4_XSTORM_RDMA_CONN_AG_CTX_EXIST_IN_QM3_MASK      0x1 /* exist_in_qm3 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_EXIST_IN_QM3_SHIFT     3
#define E4_XSTORM_RDMA_CONN_AG_CTX_BIT4_MASK              0x1 /* bit4 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_BIT4_SHIFT             4
#define E4_XSTORM_RDMA_CONN_AG_CTX_BIT5_MASK              0x1 /* cf_array_active */
#define E4_XSTORM_RDMA_CONN_AG_CTX_BIT5_SHIFT             5
#define E4_XSTORM_RDMA_CONN_AG_CTX_BIT6_MASK              0x1 /* bit6 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_BIT6_SHIFT             6
#define E4_XSTORM_RDMA_CONN_AG_CTX_BIT7_MASK              0x1 /* bit7 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_BIT7_SHIFT             7
	u8 flags1;
#define E4_XSTORM_RDMA_CONN_AG_CTX_BIT8_MASK              0x1 /* bit8 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_BIT8_SHIFT             0
#define E4_XSTORM_RDMA_CONN_AG_CTX_BIT9_MASK              0x1 /* bit9 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_BIT9_SHIFT             1
#define E4_XSTORM_RDMA_CONN_AG_CTX_BIT10_MASK             0x1 /* bit10 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_BIT10_SHIFT            2
#define E4_XSTORM_RDMA_CONN_AG_CTX_BIT11_MASK             0x1 /* bit11 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_BIT11_SHIFT            3
#define E4_XSTORM_RDMA_CONN_AG_CTX_BIT12_MASK             0x1 /* bit12 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_BIT12_SHIFT            4
#define E4_XSTORM_RDMA_CONN_AG_CTX_MSTORM_FLUSH_MASK      0x1 /* bit13 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_MSTORM_FLUSH_SHIFT     5
#define E4_XSTORM_RDMA_CONN_AG_CTX_BIT14_MASK             0x1 /* bit14 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_BIT14_SHIFT            6
#define E4_XSTORM_RDMA_CONN_AG_CTX_YSTORM_FLUSH_MASK      0x1 /* bit15 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_YSTORM_FLUSH_SHIFT     7
	u8 flags2;
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF0_MASK               0x3 /* timer0cf */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF0_SHIFT              0
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF1_MASK               0x3 /* timer1cf */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF1_SHIFT              2
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF2_MASK               0x3 /* timer2cf */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF2_SHIFT              4
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF3_MASK               0x3 /* timer_stop_all */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF3_SHIFT              6
	u8 flags3;
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF4_MASK               0x3 /* cf4 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF4_SHIFT              0
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF5_MASK               0x3 /* cf5 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF5_SHIFT              2
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF6_MASK               0x3 /* cf6 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF6_SHIFT              4
#define E4_XSTORM_RDMA_CONN_AG_CTX_FLUSH_Q0_CF_MASK       0x3 /* cf7 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_FLUSH_Q0_CF_SHIFT      6
	u8 flags4;
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF8_MASK               0x3 /* cf8 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF8_SHIFT              0
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF9_MASK               0x3 /* cf9 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF9_SHIFT              2
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF10_MASK              0x3 /* cf10 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF10_SHIFT             4
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF11_MASK              0x3 /* cf11 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF11_SHIFT             6
	u8 flags5;
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF12_MASK              0x3 /* cf12 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF12_SHIFT             0
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF13_MASK              0x3 /* cf13 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF13_SHIFT             2
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF14_MASK              0x3 /* cf14 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF14_SHIFT             4
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF15_MASK              0x3 /* cf15 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF15_SHIFT             6
	u8 flags6;
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF16_MASK              0x3 /* cf16 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF16_SHIFT             0
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF17_MASK              0x3 /* cf_array_cf */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF17_SHIFT             2
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF18_MASK              0x3 /* cf18 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF18_SHIFT             4
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF19_MASK              0x3 /* cf19 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF19_SHIFT             6
	u8 flags7;
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF20_MASK              0x3 /* cf20 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF20_SHIFT             0
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF21_MASK              0x3 /* cf21 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF21_SHIFT             2
#define E4_XSTORM_RDMA_CONN_AG_CTX_SLOW_PATH_MASK         0x3 /* cf22 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_SLOW_PATH_SHIFT        4
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF0EN_MASK             0x1 /* cf0en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF0EN_SHIFT            6
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF1EN_MASK             0x1 /* cf1en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF1EN_SHIFT            7
	u8 flags8;
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF2EN_MASK             0x1 /* cf2en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF2EN_SHIFT            0
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF3EN_MASK             0x1 /* cf3en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF3EN_SHIFT            1
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF4EN_MASK             0x1 /* cf4en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF4EN_SHIFT            2
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF5EN_MASK             0x1 /* cf5en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF5EN_SHIFT            3
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF6EN_MASK             0x1 /* cf6en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF6EN_SHIFT            4
#define E4_XSTORM_RDMA_CONN_AG_CTX_FLUSH_Q0_CF_EN_MASK    0x1 /* cf7en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_FLUSH_Q0_CF_EN_SHIFT   5
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF8EN_MASK             0x1 /* cf8en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF8EN_SHIFT            6
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF9EN_MASK             0x1 /* cf9en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF9EN_SHIFT            7
	u8 flags9;
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF10EN_MASK            0x1 /* cf10en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF10EN_SHIFT           0
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF11EN_MASK            0x1 /* cf11en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF11EN_SHIFT           1
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF12EN_MASK            0x1 /* cf12en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF12EN_SHIFT           2
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF13EN_MASK            0x1 /* cf13en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF13EN_SHIFT           3
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF14EN_MASK            0x1 /* cf14en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF14EN_SHIFT           4
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF15EN_MASK            0x1 /* cf15en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF15EN_SHIFT           5
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF16EN_MASK            0x1 /* cf16en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF16EN_SHIFT           6
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF17EN_MASK            0x1 /* cf_array_cf_en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF17EN_SHIFT           7
	u8 flags10;
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF18EN_MASK            0x1 /* cf18en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF18EN_SHIFT           0
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF19EN_MASK            0x1 /* cf19en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF19EN_SHIFT           1
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF20EN_MASK            0x1 /* cf20en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF20EN_SHIFT           2
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF21EN_MASK            0x1 /* cf21en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF21EN_SHIFT           3
#define E4_XSTORM_RDMA_CONN_AG_CTX_SLOW_PATH_EN_MASK      0x1 /* cf22en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_SLOW_PATH_EN_SHIFT     4
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF23EN_MASK            0x1 /* cf23en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF23EN_SHIFT           5
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE0EN_MASK           0x1 /* rule0en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE0EN_SHIFT          6
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE1EN_MASK           0x1 /* rule1en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE1EN_SHIFT          7
	u8 flags11;
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE2EN_MASK           0x1 /* rule2en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE2EN_SHIFT          0
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE3EN_MASK           0x1 /* rule3en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE3EN_SHIFT          1
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE4EN_MASK           0x1 /* rule4en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE4EN_SHIFT          2
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE5EN_MASK           0x1 /* rule5en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE5EN_SHIFT          3
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE6EN_MASK           0x1 /* rule6en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE6EN_SHIFT          4
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE7EN_MASK           0x1 /* rule7en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE7EN_SHIFT          5
#define E4_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED1_MASK      0x1 /* rule8en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED1_SHIFT     6
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE9EN_MASK           0x1 /* rule9en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE9EN_SHIFT          7
	u8 flags12;
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE10EN_MASK          0x1 /* rule10en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE10EN_SHIFT         0
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE11EN_MASK          0x1 /* rule11en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE11EN_SHIFT         1
#define E4_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED2_MASK      0x1 /* rule12en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED2_SHIFT     2
#define E4_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED3_MASK      0x1 /* rule13en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED3_SHIFT     3
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE14EN_MASK          0x1 /* rule14en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE14EN_SHIFT         4
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE15EN_MASK          0x1 /* rule15en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE15EN_SHIFT         5
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE16EN_MASK          0x1 /* rule16en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE16EN_SHIFT         6
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE17EN_MASK          0x1 /* rule17en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE17EN_SHIFT         7
	u8 flags13;
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE18EN_MASK          0x1 /* rule18en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE18EN_SHIFT         0
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE19EN_MASK          0x1 /* rule19en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_RULE19EN_SHIFT         1
#define E4_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED4_MASK      0x1 /* rule20en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED4_SHIFT     2
#define E4_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED5_MASK      0x1 /* rule21en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED5_SHIFT     3
#define E4_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED6_MASK      0x1 /* rule22en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED6_SHIFT     4
#define E4_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED7_MASK      0x1 /* rule23en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED7_SHIFT     5
#define E4_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED8_MASK      0x1 /* rule24en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED8_SHIFT     6
#define E4_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED9_MASK      0x1 /* rule25en */
#define E4_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED9_SHIFT     7
	u8 flags14;
#define E4_XSTORM_RDMA_CONN_AG_CTX_MIGRATION_MASK         0x1 /* bit16 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_MIGRATION_SHIFT        0
#define E4_XSTORM_RDMA_CONN_AG_CTX_BIT17_MASK             0x1 /* bit17 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_BIT17_SHIFT            1
#define E4_XSTORM_RDMA_CONN_AG_CTX_DPM_PORT_NUM_MASK      0x3 /* bit18 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_DPM_PORT_NUM_SHIFT     2
#define E4_XSTORM_RDMA_CONN_AG_CTX_RESERVED_MASK          0x1 /* bit20 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_RESERVED_SHIFT         4
#define E4_XSTORM_RDMA_CONN_AG_CTX_ROCE_EDPM_ENABLE_MASK  0x1 /* bit21 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_ROCE_EDPM_ENABLE_SHIFT 5
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF23_MASK              0x3 /* cf23 */
#define E4_XSTORM_RDMA_CONN_AG_CTX_CF23_SHIFT             6
	u8 byte2 /* byte2 */;
	__le16 physical_q0 /* physical_q0 */;
	__le16 word1 /* physical_q1 */;
	__le16 word2 /* physical_q2 */;
	__le16 word3 /* word3 */;
	__le16 word4 /* word4 */;
	__le16 word5 /* word5 */;
	__le16 conn_dpi /* conn_dpi */;
	u8 byte3 /* byte3 */;
	u8 byte4 /* byte4 */;
	u8 byte5 /* byte5 */;
	u8 byte6 /* byte6 */;
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
	__le32 reg2 /* reg2 */;
	__le32 snd_nxt_psn /* reg3 */;
	__le32 reg4 /* reg4 */;
	__le32 reg5 /* cf_array0 */;
	__le32 reg6 /* cf_array1 */;
};


struct e4_ystorm_rdma_conn_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 byte1 /* state */;
	u8 flags0;
#define E4_YSTORM_RDMA_CONN_AG_CTX_BIT0_MASK     0x1 /* exist_in_qm0 */
#define E4_YSTORM_RDMA_CONN_AG_CTX_BIT0_SHIFT    0
#define E4_YSTORM_RDMA_CONN_AG_CTX_BIT1_MASK     0x1 /* exist_in_qm1 */
#define E4_YSTORM_RDMA_CONN_AG_CTX_BIT1_SHIFT    1
#define E4_YSTORM_RDMA_CONN_AG_CTX_CF0_MASK      0x3 /* cf0 */
#define E4_YSTORM_RDMA_CONN_AG_CTX_CF0_SHIFT     2
#define E4_YSTORM_RDMA_CONN_AG_CTX_CF1_MASK      0x3 /* cf1 */
#define E4_YSTORM_RDMA_CONN_AG_CTX_CF1_SHIFT     4
#define E4_YSTORM_RDMA_CONN_AG_CTX_CF2_MASK      0x3 /* cf2 */
#define E4_YSTORM_RDMA_CONN_AG_CTX_CF2_SHIFT     6
	u8 flags1;
#define E4_YSTORM_RDMA_CONN_AG_CTX_CF0EN_MASK    0x1 /* cf0en */
#define E4_YSTORM_RDMA_CONN_AG_CTX_CF0EN_SHIFT   0
#define E4_YSTORM_RDMA_CONN_AG_CTX_CF1EN_MASK    0x1 /* cf1en */
#define E4_YSTORM_RDMA_CONN_AG_CTX_CF1EN_SHIFT   1
#define E4_YSTORM_RDMA_CONN_AG_CTX_CF2EN_MASK    0x1 /* cf2en */
#define E4_YSTORM_RDMA_CONN_AG_CTX_CF2EN_SHIFT   2
#define E4_YSTORM_RDMA_CONN_AG_CTX_RULE0EN_MASK  0x1 /* rule0en */
#define E4_YSTORM_RDMA_CONN_AG_CTX_RULE0EN_SHIFT 3
#define E4_YSTORM_RDMA_CONN_AG_CTX_RULE1EN_MASK  0x1 /* rule1en */
#define E4_YSTORM_RDMA_CONN_AG_CTX_RULE1EN_SHIFT 4
#define E4_YSTORM_RDMA_CONN_AG_CTX_RULE2EN_MASK  0x1 /* rule2en */
#define E4_YSTORM_RDMA_CONN_AG_CTX_RULE2EN_SHIFT 5
#define E4_YSTORM_RDMA_CONN_AG_CTX_RULE3EN_MASK  0x1 /* rule3en */
#define E4_YSTORM_RDMA_CONN_AG_CTX_RULE3EN_SHIFT 6
#define E4_YSTORM_RDMA_CONN_AG_CTX_RULE4EN_MASK  0x1 /* rule4en */
#define E4_YSTORM_RDMA_CONN_AG_CTX_RULE4EN_SHIFT 7
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



struct e5_mstorm_rdma_conn_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 byte1 /* state_and_core_id */;
	u8 flags0;
#define E5_MSTORM_RDMA_CONN_AG_CTX_BIT0_MASK     0x1 /* exist_in_qm0 */
#define E5_MSTORM_RDMA_CONN_AG_CTX_BIT0_SHIFT    0
#define E5_MSTORM_RDMA_CONN_AG_CTX_BIT1_MASK     0x1 /* exist_in_qm1 */
#define E5_MSTORM_RDMA_CONN_AG_CTX_BIT1_SHIFT    1
#define E5_MSTORM_RDMA_CONN_AG_CTX_CF0_MASK      0x3 /* cf0 */
#define E5_MSTORM_RDMA_CONN_AG_CTX_CF0_SHIFT     2
#define E5_MSTORM_RDMA_CONN_AG_CTX_CF1_MASK      0x3 /* cf1 */
#define E5_MSTORM_RDMA_CONN_AG_CTX_CF1_SHIFT     4
#define E5_MSTORM_RDMA_CONN_AG_CTX_CF2_MASK      0x3 /* cf2 */
#define E5_MSTORM_RDMA_CONN_AG_CTX_CF2_SHIFT     6
	u8 flags1;
#define E5_MSTORM_RDMA_CONN_AG_CTX_CF0EN_MASK    0x1 /* cf0en */
#define E5_MSTORM_RDMA_CONN_AG_CTX_CF0EN_SHIFT   0
#define E5_MSTORM_RDMA_CONN_AG_CTX_CF1EN_MASK    0x1 /* cf1en */
#define E5_MSTORM_RDMA_CONN_AG_CTX_CF1EN_SHIFT   1
#define E5_MSTORM_RDMA_CONN_AG_CTX_CF2EN_MASK    0x1 /* cf2en */
#define E5_MSTORM_RDMA_CONN_AG_CTX_CF2EN_SHIFT   2
#define E5_MSTORM_RDMA_CONN_AG_CTX_RULE0EN_MASK  0x1 /* rule0en */
#define E5_MSTORM_RDMA_CONN_AG_CTX_RULE0EN_SHIFT 3
#define E5_MSTORM_RDMA_CONN_AG_CTX_RULE1EN_MASK  0x1 /* rule1en */
#define E5_MSTORM_RDMA_CONN_AG_CTX_RULE1EN_SHIFT 4
#define E5_MSTORM_RDMA_CONN_AG_CTX_RULE2EN_MASK  0x1 /* rule2en */
#define E5_MSTORM_RDMA_CONN_AG_CTX_RULE2EN_SHIFT 5
#define E5_MSTORM_RDMA_CONN_AG_CTX_RULE3EN_MASK  0x1 /* rule3en */
#define E5_MSTORM_RDMA_CONN_AG_CTX_RULE3EN_SHIFT 6
#define E5_MSTORM_RDMA_CONN_AG_CTX_RULE4EN_MASK  0x1 /* rule4en */
#define E5_MSTORM_RDMA_CONN_AG_CTX_RULE4EN_SHIFT 7
	__le16 word0 /* word0 */;
	__le16 word1 /* word1 */;
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
};



struct e5_tstorm_rdma_conn_ag_ctx
{
	u8 reserved0 /* cdu_validation */;
	u8 byte1 /* state_and_core_id */;
	u8 flags0;
#define E5_TSTORM_RDMA_CONN_AG_CTX_EXIST_IN_QM0_MASK          0x1 /* exist_in_qm0 */
#define E5_TSTORM_RDMA_CONN_AG_CTX_EXIST_IN_QM0_SHIFT         0
#define E5_TSTORM_RDMA_CONN_AG_CTX_BIT1_MASK                  0x1 /* exist_in_qm1 */
#define E5_TSTORM_RDMA_CONN_AG_CTX_BIT1_SHIFT                 1
#define E5_TSTORM_RDMA_CONN_AG_CTX_BIT2_MASK                  0x1 /* bit2 */
#define E5_TSTORM_RDMA_CONN_AG_CTX_BIT2_SHIFT                 2
#define E5_TSTORM_RDMA_CONN_AG_CTX_BIT3_MASK                  0x1 /* bit3 */
#define E5_TSTORM_RDMA_CONN_AG_CTX_BIT3_SHIFT                 3
#define E5_TSTORM_RDMA_CONN_AG_CTX_BIT4_MASK                  0x1 /* bit4 */
#define E5_TSTORM_RDMA_CONN_AG_CTX_BIT4_SHIFT                 4
#define E5_TSTORM_RDMA_CONN_AG_CTX_BIT5_MASK                  0x1 /* bit5 */
#define E5_TSTORM_RDMA_CONN_AG_CTX_BIT5_SHIFT                 5
#define E5_TSTORM_RDMA_CONN_AG_CTX_CF0_MASK                   0x3 /* timer0cf */
#define E5_TSTORM_RDMA_CONN_AG_CTX_CF0_SHIFT                  6
	u8 flags1;
#define E5_TSTORM_RDMA_CONN_AG_CTX_CF1_MASK                   0x3 /* timer1cf */
#define E5_TSTORM_RDMA_CONN_AG_CTX_CF1_SHIFT                  0
#define E5_TSTORM_RDMA_CONN_AG_CTX_CF2_MASK                   0x3 /* timer2cf */
#define E5_TSTORM_RDMA_CONN_AG_CTX_CF2_SHIFT                  2
#define E5_TSTORM_RDMA_CONN_AG_CTX_TIMER_STOP_ALL_CF_MASK     0x3 /* timer_stop_all */
#define E5_TSTORM_RDMA_CONN_AG_CTX_TIMER_STOP_ALL_CF_SHIFT    4
#define E5_TSTORM_RDMA_CONN_AG_CTX_FLUSH_Q0_CF_MASK           0x3 /* cf4 */
#define E5_TSTORM_RDMA_CONN_AG_CTX_FLUSH_Q0_CF_SHIFT          6
	u8 flags2;
#define E5_TSTORM_RDMA_CONN_AG_CTX_MSTORM_FLUSH_CF_MASK       0x3 /* cf5 */
#define E5_TSTORM_RDMA_CONN_AG_CTX_MSTORM_FLUSH_CF_SHIFT      0
#define E5_TSTORM_RDMA_CONN_AG_CTX_CF6_MASK                   0x3 /* cf6 */
#define E5_TSTORM_RDMA_CONN_AG_CTX_CF6_SHIFT                  2
#define E5_TSTORM_RDMA_CONN_AG_CTX_CF7_MASK                   0x3 /* cf7 */
#define E5_TSTORM_RDMA_CONN_AG_CTX_CF7_SHIFT                  4
#define E5_TSTORM_RDMA_CONN_AG_CTX_CF8_MASK                   0x3 /* cf8 */
#define E5_TSTORM_RDMA_CONN_AG_CTX_CF8_SHIFT                  6
	u8 flags3;
#define E5_TSTORM_RDMA_CONN_AG_CTX_CF9_MASK                   0x3 /* cf9 */
#define E5_TSTORM_RDMA_CONN_AG_CTX_CF9_SHIFT                  0
#define E5_TSTORM_RDMA_CONN_AG_CTX_CF10_MASK                  0x3 /* cf10 */
#define E5_TSTORM_RDMA_CONN_AG_CTX_CF10_SHIFT                 2
#define E5_TSTORM_RDMA_CONN_AG_CTX_CF0EN_MASK                 0x1 /* cf0en */
#define E5_TSTORM_RDMA_CONN_AG_CTX_CF0EN_SHIFT                4
#define E5_TSTORM_RDMA_CONN_AG_CTX_CF1EN_MASK                 0x1 /* cf1en */
#define E5_TSTORM_RDMA_CONN_AG_CTX_CF1EN_SHIFT                5
#define E5_TSTORM_RDMA_CONN_AG_CTX_CF2EN_MASK                 0x1 /* cf2en */
#define E5_TSTORM_RDMA_CONN_AG_CTX_CF2EN_SHIFT                6
#define E5_TSTORM_RDMA_CONN_AG_CTX_TIMER_STOP_ALL_CF_EN_MASK  0x1 /* cf3en */
#define E5_TSTORM_RDMA_CONN_AG_CTX_TIMER_STOP_ALL_CF_EN_SHIFT 7
	u8 flags4;
#define E5_TSTORM_RDMA_CONN_AG_CTX_FLUSH_Q0_CF_EN_MASK        0x1 /* cf4en */
#define E5_TSTORM_RDMA_CONN_AG_CTX_FLUSH_Q0_CF_EN_SHIFT       0
#define E5_TSTORM_RDMA_CONN_AG_CTX_MSTORM_FLUSH_CF_EN_MASK    0x1 /* cf5en */
#define E5_TSTORM_RDMA_CONN_AG_CTX_MSTORM_FLUSH_CF_EN_SHIFT   1
#define E5_TSTORM_RDMA_CONN_AG_CTX_CF6EN_MASK                 0x1 /* cf6en */
#define E5_TSTORM_RDMA_CONN_AG_CTX_CF6EN_SHIFT                2
#define E5_TSTORM_RDMA_CONN_AG_CTX_CF7EN_MASK                 0x1 /* cf7en */
#define E5_TSTORM_RDMA_CONN_AG_CTX_CF7EN_SHIFT                3
#define E5_TSTORM_RDMA_CONN_AG_CTX_CF8EN_MASK                 0x1 /* cf8en */
#define E5_TSTORM_RDMA_CONN_AG_CTX_CF8EN_SHIFT                4
#define E5_TSTORM_RDMA_CONN_AG_CTX_CF9EN_MASK                 0x1 /* cf9en */
#define E5_TSTORM_RDMA_CONN_AG_CTX_CF9EN_SHIFT                5
#define E5_TSTORM_RDMA_CONN_AG_CTX_CF10EN_MASK                0x1 /* cf10en */
#define E5_TSTORM_RDMA_CONN_AG_CTX_CF10EN_SHIFT               6
#define E5_TSTORM_RDMA_CONN_AG_CTX_RULE0EN_MASK               0x1 /* rule0en */
#define E5_TSTORM_RDMA_CONN_AG_CTX_RULE0EN_SHIFT              7
	u8 flags5;
#define E5_TSTORM_RDMA_CONN_AG_CTX_RULE1EN_MASK               0x1 /* rule1en */
#define E5_TSTORM_RDMA_CONN_AG_CTX_RULE1EN_SHIFT              0
#define E5_TSTORM_RDMA_CONN_AG_CTX_RULE2EN_MASK               0x1 /* rule2en */
#define E5_TSTORM_RDMA_CONN_AG_CTX_RULE2EN_SHIFT              1
#define E5_TSTORM_RDMA_CONN_AG_CTX_RULE3EN_MASK               0x1 /* rule3en */
#define E5_TSTORM_RDMA_CONN_AG_CTX_RULE3EN_SHIFT              2
#define E5_TSTORM_RDMA_CONN_AG_CTX_RULE4EN_MASK               0x1 /* rule4en */
#define E5_TSTORM_RDMA_CONN_AG_CTX_RULE4EN_SHIFT              3
#define E5_TSTORM_RDMA_CONN_AG_CTX_RULE5EN_MASK               0x1 /* rule5en */
#define E5_TSTORM_RDMA_CONN_AG_CTX_RULE5EN_SHIFT              4
#define E5_TSTORM_RDMA_CONN_AG_CTX_RULE6EN_MASK               0x1 /* rule6en */
#define E5_TSTORM_RDMA_CONN_AG_CTX_RULE6EN_SHIFT              5
#define E5_TSTORM_RDMA_CONN_AG_CTX_RULE7EN_MASK               0x1 /* rule7en */
#define E5_TSTORM_RDMA_CONN_AG_CTX_RULE7EN_SHIFT              6
#define E5_TSTORM_RDMA_CONN_AG_CTX_RULE8EN_MASK               0x1 /* rule8en */
#define E5_TSTORM_RDMA_CONN_AG_CTX_RULE8EN_SHIFT              7
	u8 flags6;
#define E5_TSTORM_RDMA_CONN_AG_CTX_E4_RESERVED1_MASK          0x1 /* bit6 */
#define E5_TSTORM_RDMA_CONN_AG_CTX_E4_RESERVED1_SHIFT         0
#define E5_TSTORM_RDMA_CONN_AG_CTX_E4_RESERVED2_MASK          0x1 /* bit7 */
#define E5_TSTORM_RDMA_CONN_AG_CTX_E4_RESERVED2_SHIFT         1
#define E5_TSTORM_RDMA_CONN_AG_CTX_E4_RESERVED3_MASK          0x1 /* bit8 */
#define E5_TSTORM_RDMA_CONN_AG_CTX_E4_RESERVED3_SHIFT         2
#define E5_TSTORM_RDMA_CONN_AG_CTX_E4_RESERVED4_MASK          0x3 /* cf11 */
#define E5_TSTORM_RDMA_CONN_AG_CTX_E4_RESERVED4_SHIFT         3
#define E5_TSTORM_RDMA_CONN_AG_CTX_E4_RESERVED5_MASK          0x1 /* cf11en */
#define E5_TSTORM_RDMA_CONN_AG_CTX_E4_RESERVED5_SHIFT         5
#define E5_TSTORM_RDMA_CONN_AG_CTX_E4_RESERVED6_MASK          0x1 /* rule9en */
#define E5_TSTORM_RDMA_CONN_AG_CTX_E4_RESERVED6_SHIFT         6
#define E5_TSTORM_RDMA_CONN_AG_CTX_E4_RESERVED7_MASK          0x1 /* rule10en */
#define E5_TSTORM_RDMA_CONN_AG_CTX_E4_RESERVED7_SHIFT         7
	u8 byte2 /* byte2 */;
	__le16 word0 /* word0 */;
	__le32 reg0 /* reg0 */;
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
	__le16 word1 /* word1 */;
	__le16 word2 /* conn_dpi */;
	__le32 reg9 /* reg9 */;
	__le16 word3 /* word3 */;
	__le16 e4_reserved9 /* word4 */;
};


struct e5_tstorm_rdma_task_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 byte1 /* state_and_core_id */;
	__le16 word0 /* icid */;
	u8 flags0;
#define E5_TSTORM_RDMA_TASK_AG_CTX_NIBBLE0_MASK  0xF /* connection_type */
#define E5_TSTORM_RDMA_TASK_AG_CTX_NIBBLE0_SHIFT 0
#define E5_TSTORM_RDMA_TASK_AG_CTX_BIT0_MASK     0x1 /* exist_in_qm0 */
#define E5_TSTORM_RDMA_TASK_AG_CTX_BIT0_SHIFT    4
#define E5_TSTORM_RDMA_TASK_AG_CTX_BIT1_MASK     0x1 /* exist_in_qm1 */
#define E5_TSTORM_RDMA_TASK_AG_CTX_BIT1_SHIFT    5
#define E5_TSTORM_RDMA_TASK_AG_CTX_BIT2_MASK     0x1 /* bit2 */
#define E5_TSTORM_RDMA_TASK_AG_CTX_BIT2_SHIFT    6
#define E5_TSTORM_RDMA_TASK_AG_CTX_BIT3_MASK     0x1 /* bit3 */
#define E5_TSTORM_RDMA_TASK_AG_CTX_BIT3_SHIFT    7
	u8 flags1;
#define E5_TSTORM_RDMA_TASK_AG_CTX_BIT4_MASK     0x1 /* bit4 */
#define E5_TSTORM_RDMA_TASK_AG_CTX_BIT4_SHIFT    0
#define E5_TSTORM_RDMA_TASK_AG_CTX_BIT5_MASK     0x1 /* bit5 */
#define E5_TSTORM_RDMA_TASK_AG_CTX_BIT5_SHIFT    1
#define E5_TSTORM_RDMA_TASK_AG_CTX_CF0_MASK      0x3 /* timer0cf */
#define E5_TSTORM_RDMA_TASK_AG_CTX_CF0_SHIFT     2
#define E5_TSTORM_RDMA_TASK_AG_CTX_CF1_MASK      0x3 /* timer1cf */
#define E5_TSTORM_RDMA_TASK_AG_CTX_CF1_SHIFT     4
#define E5_TSTORM_RDMA_TASK_AG_CTX_CF2_MASK      0x3 /* timer2cf */
#define E5_TSTORM_RDMA_TASK_AG_CTX_CF2_SHIFT     6
	u8 flags2;
#define E5_TSTORM_RDMA_TASK_AG_CTX_CF3_MASK      0x3 /* timer_stop_all */
#define E5_TSTORM_RDMA_TASK_AG_CTX_CF3_SHIFT     0
#define E5_TSTORM_RDMA_TASK_AG_CTX_CF4_MASK      0x3 /* cf4 */
#define E5_TSTORM_RDMA_TASK_AG_CTX_CF4_SHIFT     2
#define E5_TSTORM_RDMA_TASK_AG_CTX_CF5_MASK      0x3 /* cf5 */
#define E5_TSTORM_RDMA_TASK_AG_CTX_CF5_SHIFT     4
#define E5_TSTORM_RDMA_TASK_AG_CTX_CF6_MASK      0x3 /* cf6 */
#define E5_TSTORM_RDMA_TASK_AG_CTX_CF6_SHIFT     6
	u8 flags3;
#define E5_TSTORM_RDMA_TASK_AG_CTX_CF7_MASK      0x3 /* cf7 */
#define E5_TSTORM_RDMA_TASK_AG_CTX_CF7_SHIFT     0
#define E5_TSTORM_RDMA_TASK_AG_CTX_CF0EN_MASK    0x1 /* cf0en */
#define E5_TSTORM_RDMA_TASK_AG_CTX_CF0EN_SHIFT   2
#define E5_TSTORM_RDMA_TASK_AG_CTX_CF1EN_MASK    0x1 /* cf1en */
#define E5_TSTORM_RDMA_TASK_AG_CTX_CF1EN_SHIFT   3
#define E5_TSTORM_RDMA_TASK_AG_CTX_CF2EN_MASK    0x1 /* cf2en */
#define E5_TSTORM_RDMA_TASK_AG_CTX_CF2EN_SHIFT   4
#define E5_TSTORM_RDMA_TASK_AG_CTX_CF3EN_MASK    0x1 /* cf3en */
#define E5_TSTORM_RDMA_TASK_AG_CTX_CF3EN_SHIFT   5
#define E5_TSTORM_RDMA_TASK_AG_CTX_CF4EN_MASK    0x1 /* cf4en */
#define E5_TSTORM_RDMA_TASK_AG_CTX_CF4EN_SHIFT   6
#define E5_TSTORM_RDMA_TASK_AG_CTX_CF5EN_MASK    0x1 /* cf5en */
#define E5_TSTORM_RDMA_TASK_AG_CTX_CF5EN_SHIFT   7
	u8 flags4;
#define E5_TSTORM_RDMA_TASK_AG_CTX_CF6EN_MASK    0x1 /* cf6en */
#define E5_TSTORM_RDMA_TASK_AG_CTX_CF6EN_SHIFT   0
#define E5_TSTORM_RDMA_TASK_AG_CTX_CF7EN_MASK    0x1 /* cf7en */
#define E5_TSTORM_RDMA_TASK_AG_CTX_CF7EN_SHIFT   1
#define E5_TSTORM_RDMA_TASK_AG_CTX_RULE0EN_MASK  0x1 /* rule0en */
#define E5_TSTORM_RDMA_TASK_AG_CTX_RULE0EN_SHIFT 2
#define E5_TSTORM_RDMA_TASK_AG_CTX_RULE1EN_MASK  0x1 /* rule1en */
#define E5_TSTORM_RDMA_TASK_AG_CTX_RULE1EN_SHIFT 3
#define E5_TSTORM_RDMA_TASK_AG_CTX_RULE2EN_MASK  0x1 /* rule2en */
#define E5_TSTORM_RDMA_TASK_AG_CTX_RULE2EN_SHIFT 4
#define E5_TSTORM_RDMA_TASK_AG_CTX_RULE3EN_MASK  0x1 /* rule3en */
#define E5_TSTORM_RDMA_TASK_AG_CTX_RULE3EN_SHIFT 5
#define E5_TSTORM_RDMA_TASK_AG_CTX_RULE4EN_MASK  0x1 /* rule4en */
#define E5_TSTORM_RDMA_TASK_AG_CTX_RULE4EN_SHIFT 6
#define E5_TSTORM_RDMA_TASK_AG_CTX_RULE5EN_MASK  0x1 /* rule5en */
#define E5_TSTORM_RDMA_TASK_AG_CTX_RULE5EN_SHIFT 7
	u8 byte2 /* byte2 */;
	__le16 word1 /* word1 */;
	__le32 reg0 /* reg0 */;
	u8 byte3 /* regpair0 */;
	u8 byte4 /* byte4 */;
	__le16 word2 /* word2 */;
	__le16 word3 /* word3 */;
	__le16 word4 /* word4 */;
	__le32 reg1 /* regpair1 */;
	__le32 reg2 /* reg2 */;
};


struct e5_ustorm_rdma_conn_ag_ctx
{
	u8 reserved /* cdu_validation */;
	u8 byte1 /* state_and_core_id */;
	u8 flags0;
#define E5_USTORM_RDMA_CONN_AG_CTX_EXIST_IN_QM0_MASK     0x1 /* exist_in_qm0 */
#define E5_USTORM_RDMA_CONN_AG_CTX_EXIST_IN_QM0_SHIFT    0
#define E5_USTORM_RDMA_CONN_AG_CTX_BIT1_MASK             0x1 /* exist_in_qm1 */
#define E5_USTORM_RDMA_CONN_AG_CTX_BIT1_SHIFT            1
#define E5_USTORM_RDMA_CONN_AG_CTX_FLUSH_Q0_CF_MASK      0x3 /* timer0cf */
#define E5_USTORM_RDMA_CONN_AG_CTX_FLUSH_Q0_CF_SHIFT     2
#define E5_USTORM_RDMA_CONN_AG_CTX_CF1_MASK              0x3 /* timer1cf */
#define E5_USTORM_RDMA_CONN_AG_CTX_CF1_SHIFT             4
#define E5_USTORM_RDMA_CONN_AG_CTX_CF2_MASK              0x3 /* timer2cf */
#define E5_USTORM_RDMA_CONN_AG_CTX_CF2_SHIFT             6
	u8 flags1;
#define E5_USTORM_RDMA_CONN_AG_CTX_CF3_MASK              0x3 /* timer_stop_all */
#define E5_USTORM_RDMA_CONN_AG_CTX_CF3_SHIFT             0
#define E5_USTORM_RDMA_CONN_AG_CTX_CQ_ARM_SE_CF_MASK     0x3 /* cf4 */
#define E5_USTORM_RDMA_CONN_AG_CTX_CQ_ARM_SE_CF_SHIFT    2
#define E5_USTORM_RDMA_CONN_AG_CTX_CQ_ARM_CF_MASK        0x3 /* cf5 */
#define E5_USTORM_RDMA_CONN_AG_CTX_CQ_ARM_CF_SHIFT       4
#define E5_USTORM_RDMA_CONN_AG_CTX_CF6_MASK              0x3 /* cf6 */
#define E5_USTORM_RDMA_CONN_AG_CTX_CF6_SHIFT             6
	u8 flags2;
#define E5_USTORM_RDMA_CONN_AG_CTX_FLUSH_Q0_CF_EN_MASK   0x1 /* cf0en */
#define E5_USTORM_RDMA_CONN_AG_CTX_FLUSH_Q0_CF_EN_SHIFT  0
#define E5_USTORM_RDMA_CONN_AG_CTX_CF1EN_MASK            0x1 /* cf1en */
#define E5_USTORM_RDMA_CONN_AG_CTX_CF1EN_SHIFT           1
#define E5_USTORM_RDMA_CONN_AG_CTX_CF2EN_MASK            0x1 /* cf2en */
#define E5_USTORM_RDMA_CONN_AG_CTX_CF2EN_SHIFT           2
#define E5_USTORM_RDMA_CONN_AG_CTX_CF3EN_MASK            0x1 /* cf3en */
#define E5_USTORM_RDMA_CONN_AG_CTX_CF3EN_SHIFT           3
#define E5_USTORM_RDMA_CONN_AG_CTX_CQ_ARM_SE_CF_EN_MASK  0x1 /* cf4en */
#define E5_USTORM_RDMA_CONN_AG_CTX_CQ_ARM_SE_CF_EN_SHIFT 4
#define E5_USTORM_RDMA_CONN_AG_CTX_CQ_ARM_CF_EN_MASK     0x1 /* cf5en */
#define E5_USTORM_RDMA_CONN_AG_CTX_CQ_ARM_CF_EN_SHIFT    5
#define E5_USTORM_RDMA_CONN_AG_CTX_CF6EN_MASK            0x1 /* cf6en */
#define E5_USTORM_RDMA_CONN_AG_CTX_CF6EN_SHIFT           6
#define E5_USTORM_RDMA_CONN_AG_CTX_CQ_SE_EN_MASK         0x1 /* rule0en */
#define E5_USTORM_RDMA_CONN_AG_CTX_CQ_SE_EN_SHIFT        7
	u8 flags3;
#define E5_USTORM_RDMA_CONN_AG_CTX_CQ_EN_MASK            0x1 /* rule1en */
#define E5_USTORM_RDMA_CONN_AG_CTX_CQ_EN_SHIFT           0
#define E5_USTORM_RDMA_CONN_AG_CTX_RULE2EN_MASK          0x1 /* rule2en */
#define E5_USTORM_RDMA_CONN_AG_CTX_RULE2EN_SHIFT         1
#define E5_USTORM_RDMA_CONN_AG_CTX_RULE3EN_MASK          0x1 /* rule3en */
#define E5_USTORM_RDMA_CONN_AG_CTX_RULE3EN_SHIFT         2
#define E5_USTORM_RDMA_CONN_AG_CTX_RULE4EN_MASK          0x1 /* rule4en */
#define E5_USTORM_RDMA_CONN_AG_CTX_RULE4EN_SHIFT         3
#define E5_USTORM_RDMA_CONN_AG_CTX_RULE5EN_MASK          0x1 /* rule5en */
#define E5_USTORM_RDMA_CONN_AG_CTX_RULE5EN_SHIFT         4
#define E5_USTORM_RDMA_CONN_AG_CTX_RULE6EN_MASK          0x1 /* rule6en */
#define E5_USTORM_RDMA_CONN_AG_CTX_RULE6EN_SHIFT         5
#define E5_USTORM_RDMA_CONN_AG_CTX_RULE7EN_MASK          0x1 /* rule7en */
#define E5_USTORM_RDMA_CONN_AG_CTX_RULE7EN_SHIFT         6
#define E5_USTORM_RDMA_CONN_AG_CTX_RULE8EN_MASK          0x1 /* rule8en */
#define E5_USTORM_RDMA_CONN_AG_CTX_RULE8EN_SHIFT         7
	u8 flags4;
#define E5_USTORM_RDMA_CONN_AG_CTX_E4_RESERVED1_MASK     0x1 /* bit2 */
#define E5_USTORM_RDMA_CONN_AG_CTX_E4_RESERVED1_SHIFT    0
#define E5_USTORM_RDMA_CONN_AG_CTX_E4_RESERVED2_MASK     0x1 /* bit3 */
#define E5_USTORM_RDMA_CONN_AG_CTX_E4_RESERVED2_SHIFT    1
#define E5_USTORM_RDMA_CONN_AG_CTX_E4_RESERVED3_MASK     0x3 /* cf7 */
#define E5_USTORM_RDMA_CONN_AG_CTX_E4_RESERVED3_SHIFT    2
#define E5_USTORM_RDMA_CONN_AG_CTX_E4_RESERVED4_MASK     0x3 /* cf8 */
#define E5_USTORM_RDMA_CONN_AG_CTX_E4_RESERVED4_SHIFT    4
#define E5_USTORM_RDMA_CONN_AG_CTX_E4_RESERVED5_MASK     0x1 /* cf7en */
#define E5_USTORM_RDMA_CONN_AG_CTX_E4_RESERVED5_SHIFT    6
#define E5_USTORM_RDMA_CONN_AG_CTX_E4_RESERVED6_MASK     0x1 /* cf8en */
#define E5_USTORM_RDMA_CONN_AG_CTX_E4_RESERVED6_SHIFT    7
	u8 byte2 /* byte2 */;
	__le16 conn_dpi /* conn_dpi */;
	__le16 word1 /* word1 */;
	__le32 cq_cons /* reg0 */;
	__le32 cq_se_prod /* reg1 */;
	__le32 cq_prod /* reg2 */;
	__le32 reg3 /* reg3 */;
	__le16 int_timeout /* word2 */;
	__le16 word3 /* word3 */;
};



struct e5_xstorm_rdma_conn_ag_ctx
{
	u8 reserved0 /* cdu_validation */;
	u8 state_and_core_id /* state_and_core_id */;
	u8 flags0;
#define E5_XSTORM_RDMA_CONN_AG_CTX_EXIST_IN_QM0_MASK      0x1 /* exist_in_qm0 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_EXIST_IN_QM0_SHIFT     0
#define E5_XSTORM_RDMA_CONN_AG_CTX_BIT1_MASK              0x1 /* exist_in_qm1 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_BIT1_SHIFT             1
#define E5_XSTORM_RDMA_CONN_AG_CTX_BIT2_MASK              0x1 /* exist_in_qm2 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_BIT2_SHIFT             2
#define E5_XSTORM_RDMA_CONN_AG_CTX_EXIST_IN_QM3_MASK      0x1 /* exist_in_qm3 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_EXIST_IN_QM3_SHIFT     3
#define E5_XSTORM_RDMA_CONN_AG_CTX_BIT4_MASK              0x1 /* bit4 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_BIT4_SHIFT             4
#define E5_XSTORM_RDMA_CONN_AG_CTX_BIT5_MASK              0x1 /* cf_array_active */
#define E5_XSTORM_RDMA_CONN_AG_CTX_BIT5_SHIFT             5
#define E5_XSTORM_RDMA_CONN_AG_CTX_BIT6_MASK              0x1 /* bit6 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_BIT6_SHIFT             6
#define E5_XSTORM_RDMA_CONN_AG_CTX_BIT7_MASK              0x1 /* bit7 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_BIT7_SHIFT             7
	u8 flags1;
#define E5_XSTORM_RDMA_CONN_AG_CTX_BIT8_MASK              0x1 /* bit8 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_BIT8_SHIFT             0
#define E5_XSTORM_RDMA_CONN_AG_CTX_BIT9_MASK              0x1 /* bit9 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_BIT9_SHIFT             1
#define E5_XSTORM_RDMA_CONN_AG_CTX_BIT10_MASK             0x1 /* bit10 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_BIT10_SHIFT            2
#define E5_XSTORM_RDMA_CONN_AG_CTX_BIT11_MASK             0x1 /* bit11 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_BIT11_SHIFT            3
#define E5_XSTORM_RDMA_CONN_AG_CTX_BIT12_MASK             0x1 /* bit12 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_BIT12_SHIFT            4
#define E5_XSTORM_RDMA_CONN_AG_CTX_MSTORM_FLUSH_MASK      0x1 /* bit13 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_MSTORM_FLUSH_SHIFT     5
#define E5_XSTORM_RDMA_CONN_AG_CTX_BIT14_MASK             0x1 /* bit14 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_BIT14_SHIFT            6
#define E5_XSTORM_RDMA_CONN_AG_CTX_YSTORM_FLUSH_MASK      0x1 /* bit15 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_YSTORM_FLUSH_SHIFT     7
	u8 flags2;
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF0_MASK               0x3 /* timer0cf */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF0_SHIFT              0
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF1_MASK               0x3 /* timer1cf */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF1_SHIFT              2
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF2_MASK               0x3 /* timer2cf */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF2_SHIFT              4
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF3_MASK               0x3 /* timer_stop_all */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF3_SHIFT              6
	u8 flags3;
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF4_MASK               0x3 /* cf4 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF4_SHIFT              0
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF5_MASK               0x3 /* cf5 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF5_SHIFT              2
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF6_MASK               0x3 /* cf6 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF6_SHIFT              4
#define E5_XSTORM_RDMA_CONN_AG_CTX_FLUSH_Q0_CF_MASK       0x3 /* cf7 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_FLUSH_Q0_CF_SHIFT      6
	u8 flags4;
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF8_MASK               0x3 /* cf8 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF8_SHIFT              0
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF9_MASK               0x3 /* cf9 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF9_SHIFT              2
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF10_MASK              0x3 /* cf10 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF10_SHIFT             4
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF11_MASK              0x3 /* cf11 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF11_SHIFT             6
	u8 flags5;
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF12_MASK              0x3 /* cf12 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF12_SHIFT             0
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF13_MASK              0x3 /* cf13 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF13_SHIFT             2
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF14_MASK              0x3 /* cf14 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF14_SHIFT             4
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF15_MASK              0x3 /* cf15 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF15_SHIFT             6
	u8 flags6;
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF16_MASK              0x3 /* cf16 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF16_SHIFT             0
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF17_MASK              0x3 /* cf_array_cf */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF17_SHIFT             2
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF18_MASK              0x3 /* cf18 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF18_SHIFT             4
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF19_MASK              0x3 /* cf19 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF19_SHIFT             6
	u8 flags7;
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF20_MASK              0x3 /* cf20 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF20_SHIFT             0
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF21_MASK              0x3 /* cf21 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF21_SHIFT             2
#define E5_XSTORM_RDMA_CONN_AG_CTX_SLOW_PATH_MASK         0x3 /* cf22 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_SLOW_PATH_SHIFT        4
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF0EN_MASK             0x1 /* cf0en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF0EN_SHIFT            6
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF1EN_MASK             0x1 /* cf1en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF1EN_SHIFT            7
	u8 flags8;
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF2EN_MASK             0x1 /* cf2en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF2EN_SHIFT            0
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF3EN_MASK             0x1 /* cf3en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF3EN_SHIFT            1
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF4EN_MASK             0x1 /* cf4en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF4EN_SHIFT            2
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF5EN_MASK             0x1 /* cf5en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF5EN_SHIFT            3
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF6EN_MASK             0x1 /* cf6en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF6EN_SHIFT            4
#define E5_XSTORM_RDMA_CONN_AG_CTX_FLUSH_Q0_CF_EN_MASK    0x1 /* cf7en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_FLUSH_Q0_CF_EN_SHIFT   5
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF8EN_MASK             0x1 /* cf8en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF8EN_SHIFT            6
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF9EN_MASK             0x1 /* cf9en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF9EN_SHIFT            7
	u8 flags9;
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF10EN_MASK            0x1 /* cf10en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF10EN_SHIFT           0
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF11EN_MASK            0x1 /* cf11en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF11EN_SHIFT           1
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF12EN_MASK            0x1 /* cf12en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF12EN_SHIFT           2
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF13EN_MASK            0x1 /* cf13en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF13EN_SHIFT           3
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF14EN_MASK            0x1 /* cf14en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF14EN_SHIFT           4
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF15EN_MASK            0x1 /* cf15en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF15EN_SHIFT           5
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF16EN_MASK            0x1 /* cf16en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF16EN_SHIFT           6
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF17EN_MASK            0x1 /* cf_array_cf_en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF17EN_SHIFT           7
	u8 flags10;
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF18EN_MASK            0x1 /* cf18en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF18EN_SHIFT           0
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF19EN_MASK            0x1 /* cf19en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF19EN_SHIFT           1
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF20EN_MASK            0x1 /* cf20en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF20EN_SHIFT           2
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF21EN_MASK            0x1 /* cf21en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF21EN_SHIFT           3
#define E5_XSTORM_RDMA_CONN_AG_CTX_SLOW_PATH_EN_MASK      0x1 /* cf22en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_SLOW_PATH_EN_SHIFT     4
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF23EN_MASK            0x1 /* cf23en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF23EN_SHIFT           5
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE0EN_MASK           0x1 /* rule0en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE0EN_SHIFT          6
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE1EN_MASK           0x1 /* rule1en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE1EN_SHIFT          7
	u8 flags11;
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE2EN_MASK           0x1 /* rule2en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE2EN_SHIFT          0
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE3EN_MASK           0x1 /* rule3en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE3EN_SHIFT          1
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE4EN_MASK           0x1 /* rule4en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE4EN_SHIFT          2
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE5EN_MASK           0x1 /* rule5en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE5EN_SHIFT          3
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE6EN_MASK           0x1 /* rule6en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE6EN_SHIFT          4
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE7EN_MASK           0x1 /* rule7en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE7EN_SHIFT          5
#define E5_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED1_MASK      0x1 /* rule8en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED1_SHIFT     6
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE9EN_MASK           0x1 /* rule9en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE9EN_SHIFT          7
	u8 flags12;
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE10EN_MASK          0x1 /* rule10en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE10EN_SHIFT         0
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE11EN_MASK          0x1 /* rule11en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE11EN_SHIFT         1
#define E5_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED2_MASK      0x1 /* rule12en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED2_SHIFT     2
#define E5_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED3_MASK      0x1 /* rule13en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED3_SHIFT     3
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE14EN_MASK          0x1 /* rule14en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE14EN_SHIFT         4
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE15EN_MASK          0x1 /* rule15en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE15EN_SHIFT         5
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE16EN_MASK          0x1 /* rule16en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE16EN_SHIFT         6
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE17EN_MASK          0x1 /* rule17en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE17EN_SHIFT         7
	u8 flags13;
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE18EN_MASK          0x1 /* rule18en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE18EN_SHIFT         0
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE19EN_MASK          0x1 /* rule19en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_RULE19EN_SHIFT         1
#define E5_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED4_MASK      0x1 /* rule20en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED4_SHIFT     2
#define E5_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED5_MASK      0x1 /* rule21en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED5_SHIFT     3
#define E5_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED6_MASK      0x1 /* rule22en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED6_SHIFT     4
#define E5_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED7_MASK      0x1 /* rule23en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED7_SHIFT     5
#define E5_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED8_MASK      0x1 /* rule24en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED8_SHIFT     6
#define E5_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED9_MASK      0x1 /* rule25en */
#define E5_XSTORM_RDMA_CONN_AG_CTX_A0_RESERVED9_SHIFT     7
	u8 flags14;
#define E5_XSTORM_RDMA_CONN_AG_CTX_MIGRATION_MASK         0x1 /* bit16 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_MIGRATION_SHIFT        0
#define E5_XSTORM_RDMA_CONN_AG_CTX_BIT17_MASK             0x1 /* bit17 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_BIT17_SHIFT            1
#define E5_XSTORM_RDMA_CONN_AG_CTX_DPM_PORT_NUM_MASK      0x3 /* bit18 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_DPM_PORT_NUM_SHIFT     2
#define E5_XSTORM_RDMA_CONN_AG_CTX_RESERVED_MASK          0x1 /* bit20 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_RESERVED_SHIFT         4
#define E5_XSTORM_RDMA_CONN_AG_CTX_ROCE_EDPM_ENABLE_MASK  0x1 /* bit21 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_ROCE_EDPM_ENABLE_SHIFT 5
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF23_MASK              0x3 /* cf23 */
#define E5_XSTORM_RDMA_CONN_AG_CTX_CF23_SHIFT             6
	u8 byte2 /* byte2 */;
	__le16 physical_q0 /* physical_q0 */;
	__le16 word1 /* physical_q1 */;
	__le16 word2 /* physical_q2 */;
	__le16 word3 /* word3 */;
	__le16 word4 /* word4 */;
	__le16 word5 /* word5 */;
	__le16 conn_dpi /* conn_dpi */;
	u8 byte3 /* byte3 */;
	u8 byte4 /* byte4 */;
	u8 byte5 /* byte5 */;
	u8 byte6 /* byte6 */;
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
	__le32 reg2 /* reg2 */;
	__le32 snd_nxt_psn /* reg3 */;
	__le32 reg4 /* reg4 */;
	__le32 reg5 /* cf_array0 */;
	__le32 reg6 /* cf_array1 */;
};


struct e5_ystorm_rdma_conn_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 byte1 /* state_and_core_id */;
	u8 flags0;
#define E5_YSTORM_RDMA_CONN_AG_CTX_BIT0_MASK     0x1 /* exist_in_qm0 */
#define E5_YSTORM_RDMA_CONN_AG_CTX_BIT0_SHIFT    0
#define E5_YSTORM_RDMA_CONN_AG_CTX_BIT1_MASK     0x1 /* exist_in_qm1 */
#define E5_YSTORM_RDMA_CONN_AG_CTX_BIT1_SHIFT    1
#define E5_YSTORM_RDMA_CONN_AG_CTX_CF0_MASK      0x3 /* cf0 */
#define E5_YSTORM_RDMA_CONN_AG_CTX_CF0_SHIFT     2
#define E5_YSTORM_RDMA_CONN_AG_CTX_CF1_MASK      0x3 /* cf1 */
#define E5_YSTORM_RDMA_CONN_AG_CTX_CF1_SHIFT     4
#define E5_YSTORM_RDMA_CONN_AG_CTX_CF2_MASK      0x3 /* cf2 */
#define E5_YSTORM_RDMA_CONN_AG_CTX_CF2_SHIFT     6
	u8 flags1;
#define E5_YSTORM_RDMA_CONN_AG_CTX_CF0EN_MASK    0x1 /* cf0en */
#define E5_YSTORM_RDMA_CONN_AG_CTX_CF0EN_SHIFT   0
#define E5_YSTORM_RDMA_CONN_AG_CTX_CF1EN_MASK    0x1 /* cf1en */
#define E5_YSTORM_RDMA_CONN_AG_CTX_CF1EN_SHIFT   1
#define E5_YSTORM_RDMA_CONN_AG_CTX_CF2EN_MASK    0x1 /* cf2en */
#define E5_YSTORM_RDMA_CONN_AG_CTX_CF2EN_SHIFT   2
#define E5_YSTORM_RDMA_CONN_AG_CTX_RULE0EN_MASK  0x1 /* rule0en */
#define E5_YSTORM_RDMA_CONN_AG_CTX_RULE0EN_SHIFT 3
#define E5_YSTORM_RDMA_CONN_AG_CTX_RULE1EN_MASK  0x1 /* rule1en */
#define E5_YSTORM_RDMA_CONN_AG_CTX_RULE1EN_SHIFT 4
#define E5_YSTORM_RDMA_CONN_AG_CTX_RULE2EN_MASK  0x1 /* rule2en */
#define E5_YSTORM_RDMA_CONN_AG_CTX_RULE2EN_SHIFT 5
#define E5_YSTORM_RDMA_CONN_AG_CTX_RULE3EN_MASK  0x1 /* rule3en */
#define E5_YSTORM_RDMA_CONN_AG_CTX_RULE3EN_SHIFT 6
#define E5_YSTORM_RDMA_CONN_AG_CTX_RULE4EN_MASK  0x1 /* rule4en */
#define E5_YSTORM_RDMA_CONN_AG_CTX_RULE4EN_SHIFT 7
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


#endif /* __ECORE_HSI_RDMA__ */
