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

#ifndef __ECORE_HSI_ISCSI__
#define __ECORE_HSI_ISCSI__ 
/****************************************/
/* Add include to common storage target */
/****************************************/
#include "storage_common.h"

/*************************************************************************/
/* Add include to common iSCSI target for both eCore and protocol driver */
/************************************************************************/
#include "iscsi_common.h"


/*
 * The iscsi storm connection context of Ystorm
 */
struct ystorm_iscsi_conn_st_ctx
{
	__le32 reserved[8];
};

/*
 * Combined iSCSI and TCP storm connection of Pstorm
 */
struct pstorm_iscsi_tcp_conn_st_ctx
{
	__le32 tcp[32];
	__le32 iscsi[4];
};

/*
 * The combined tcp and iscsi storm context of Xstorm
 */
struct xstorm_iscsi_tcp_conn_st_ctx
{
	__le32 reserved_tcp[4];
	__le32 reserved_iscsi[44];
};

struct e4_xstorm_iscsi_conn_ag_ctx
{
	u8 cdu_validation /* cdu_validation */;
	u8 state /* state */;
	u8 flags0;
#define E4_XSTORM_ISCSI_CONN_AG_CTX_EXIST_IN_QM0_MASK                0x1 /* exist_in_qm0 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_EXIST_IN_QM0_SHIFT               0
#define E4_XSTORM_ISCSI_CONN_AG_CTX_EXIST_IN_QM1_MASK                0x1 /* exist_in_qm1 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_EXIST_IN_QM1_SHIFT               1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RESERVED1_MASK                   0x1 /* exist_in_qm2 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RESERVED1_SHIFT                  2
#define E4_XSTORM_ISCSI_CONN_AG_CTX_EXIST_IN_QM3_MASK                0x1 /* exist_in_qm3 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_EXIST_IN_QM3_SHIFT               3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT4_MASK                        0x1 /* bit4 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT4_SHIFT                       4
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RESERVED2_MASK                   0x1 /* cf_array_active */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RESERVED2_SHIFT                  5
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT6_MASK                        0x1 /* bit6 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT6_SHIFT                       6
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT7_MASK                        0x1 /* bit7 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT7_SHIFT                       7
	u8 flags1;
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT8_MASK                        0x1 /* bit8 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT8_SHIFT                       0
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT9_MASK                        0x1 /* bit9 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT9_SHIFT                       1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT10_MASK                       0x1 /* bit10 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT10_SHIFT                      2
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT11_MASK                       0x1 /* bit11 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT11_SHIFT                      3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT12_MASK                       0x1 /* bit12 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT12_SHIFT                      4
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT13_MASK                       0x1 /* bit13 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT13_SHIFT                      5
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT14_MASK                       0x1 /* bit14 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT14_SHIFT                      6
#define E4_XSTORM_ISCSI_CONN_AG_CTX_TX_TRUNCATE_MASK                 0x1 /* bit15 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_TX_TRUNCATE_SHIFT                7
	u8 flags2;
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF0_MASK                         0x3 /* timer0cf */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF0_SHIFT                        0
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF1_MASK                         0x3 /* timer1cf */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF1_SHIFT                        2
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF2_MASK                         0x3 /* timer2cf */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF2_SHIFT                        4
#define E4_XSTORM_ISCSI_CONN_AG_CTX_TIMER_STOP_ALL_MASK              0x3 /* timer_stop_all */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_TIMER_STOP_ALL_SHIFT             6
	u8 flags3;
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF4_MASK                         0x3 /* cf4 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF4_SHIFT                        0
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF5_MASK                         0x3 /* cf5 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF5_SHIFT                        2
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF6_MASK                         0x3 /* cf6 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF6_SHIFT                        4
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF7_MASK                         0x3 /* cf7 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF7_SHIFT                        6
	u8 flags4;
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF8_MASK                         0x3 /* cf8 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF8_SHIFT                        0
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF9_MASK                         0x3 /* cf9 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF9_SHIFT                        2
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF10_MASK                        0x3 /* cf10 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF10_SHIFT                       4
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF11_MASK                        0x3 /* cf11 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF11_SHIFT                       6
	u8 flags5;
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF12_MASK                        0x3 /* cf12 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF12_SHIFT                       0
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF13_MASK                        0x3 /* cf13 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF13_SHIFT                       2
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF14_MASK                        0x3 /* cf14 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF14_SHIFT                       4
#define E4_XSTORM_ISCSI_CONN_AG_CTX_UPDATE_STATE_TO_BASE_CF_MASK     0x3 /* cf15 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_UPDATE_STATE_TO_BASE_CF_SHIFT    6
	u8 flags6;
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF16_MASK                        0x3 /* cf16 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF16_SHIFT                       0
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF17_MASK                        0x3 /* cf_array_cf */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF17_SHIFT                       2
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF18_MASK                        0x3 /* cf18 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF18_SHIFT                       4
#define E4_XSTORM_ISCSI_CONN_AG_CTX_DQ_FLUSH_MASK                    0x3 /* cf19 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_DQ_FLUSH_SHIFT                   6
	u8 flags7;
#define E4_XSTORM_ISCSI_CONN_AG_CTX_MST_XCM_Q0_FLUSH_CF_MASK         0x3 /* cf20 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_MST_XCM_Q0_FLUSH_CF_SHIFT        0
#define E4_XSTORM_ISCSI_CONN_AG_CTX_UST_XCM_Q1_FLUSH_CF_MASK         0x3 /* cf21 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_UST_XCM_Q1_FLUSH_CF_SHIFT        2
#define E4_XSTORM_ISCSI_CONN_AG_CTX_SLOW_PATH_MASK                   0x3 /* cf22 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_SLOW_PATH_SHIFT                  4
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF0EN_MASK                       0x1 /* cf0en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF0EN_SHIFT                      6
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF1EN_MASK                       0x1 /* cf1en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF1EN_SHIFT                      7
	u8 flags8;
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF2EN_MASK                       0x1 /* cf2en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF2EN_SHIFT                      0
#define E4_XSTORM_ISCSI_CONN_AG_CTX_TIMER_STOP_ALL_EN_MASK           0x1 /* cf3en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_TIMER_STOP_ALL_EN_SHIFT          1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF4EN_MASK                       0x1 /* cf4en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF4EN_SHIFT                      2
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF5EN_MASK                       0x1 /* cf5en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF5EN_SHIFT                      3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF6EN_MASK                       0x1 /* cf6en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF6EN_SHIFT                      4
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF7EN_MASK                       0x1 /* cf7en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF7EN_SHIFT                      5
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF8EN_MASK                       0x1 /* cf8en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF8EN_SHIFT                      6
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF9EN_MASK                       0x1 /* cf9en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF9EN_SHIFT                      7
	u8 flags9;
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF10EN_MASK                      0x1 /* cf10en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF10EN_SHIFT                     0
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF11EN_MASK                      0x1 /* cf11en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF11EN_SHIFT                     1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF12EN_MASK                      0x1 /* cf12en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF12EN_SHIFT                     2
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF13EN_MASK                      0x1 /* cf13en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF13EN_SHIFT                     3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF14EN_MASK                      0x1 /* cf14en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF14EN_SHIFT                     4
#define E4_XSTORM_ISCSI_CONN_AG_CTX_UPDATE_STATE_TO_BASE_CF_EN_MASK  0x1 /* cf15en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_UPDATE_STATE_TO_BASE_CF_EN_SHIFT 5
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF16EN_MASK                      0x1 /* cf16en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF16EN_SHIFT                     6
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF17EN_MASK                      0x1 /* cf_array_cf_en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF17EN_SHIFT                     7
	u8 flags10;
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF18EN_MASK                      0x1 /* cf18en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_CF18EN_SHIFT                     0
#define E4_XSTORM_ISCSI_CONN_AG_CTX_DQ_FLUSH_EN_MASK                 0x1 /* cf19en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_DQ_FLUSH_EN_SHIFT                1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_MST_XCM_Q0_FLUSH_CF_EN_MASK      0x1 /* cf20en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_MST_XCM_Q0_FLUSH_CF_EN_SHIFT     2
#define E4_XSTORM_ISCSI_CONN_AG_CTX_UST_XCM_Q1_FLUSH_CF_EN_MASK      0x1 /* cf21en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_UST_XCM_Q1_FLUSH_CF_EN_SHIFT     3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_SLOW_PATH_EN_MASK                0x1 /* cf22en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_SLOW_PATH_EN_SHIFT               4
#define E4_XSTORM_ISCSI_CONN_AG_CTX_PROC_ONLY_CLEANUP_EN_MASK        0x1 /* cf23en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_PROC_ONLY_CLEANUP_EN_SHIFT       5
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE0EN_MASK                     0x1 /* rule0en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE0EN_SHIFT                    6
#define E4_XSTORM_ISCSI_CONN_AG_CTX_MORE_TO_SEND_DEC_RULE_EN_MASK    0x1 /* rule1en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_MORE_TO_SEND_DEC_RULE_EN_SHIFT   7
	u8 flags11;
#define E4_XSTORM_ISCSI_CONN_AG_CTX_TX_BLOCKED_EN_MASK               0x1 /* rule2en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_TX_BLOCKED_EN_SHIFT              0
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE3EN_MASK                     0x1 /* rule3en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE3EN_SHIFT                    1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RESERVED3_MASK                   0x1 /* rule4en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RESERVED3_SHIFT                  2
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE5EN_MASK                     0x1 /* rule5en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE5EN_SHIFT                    3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE6EN_MASK                     0x1 /* rule6en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE6EN_SHIFT                    4
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE7EN_MASK                     0x1 /* rule7en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE7EN_SHIFT                    5
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED1_MASK                0x1 /* rule8en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED1_SHIFT               6
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE9EN_MASK                     0x1 /* rule9en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE9EN_SHIFT                    7
	u8 flags12;
#define E4_XSTORM_ISCSI_CONN_AG_CTX_SQ_DEC_RULE_EN_MASK              0x1 /* rule10en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_SQ_DEC_RULE_EN_SHIFT             0
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE11EN_MASK                    0x1 /* rule11en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE11EN_SHIFT                   1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED2_MASK                0x1 /* rule12en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED2_SHIFT               2
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED3_MASK                0x1 /* rule13en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED3_SHIFT               3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE14EN_MASK                    0x1 /* rule14en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE14EN_SHIFT                   4
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE15EN_MASK                    0x1 /* rule15en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE15EN_SHIFT                   5
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE16EN_MASK                    0x1 /* rule16en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE16EN_SHIFT                   6
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE17EN_MASK                    0x1 /* rule17en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_RULE17EN_SHIFT                   7
	u8 flags13;
#define E4_XSTORM_ISCSI_CONN_AG_CTX_R2TQ_DEC_RULE_EN_MASK            0x1 /* rule18en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_R2TQ_DEC_RULE_EN_SHIFT           0
#define E4_XSTORM_ISCSI_CONN_AG_CTX_HQ_DEC_RULE_EN_MASK              0x1 /* rule19en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_HQ_DEC_RULE_EN_SHIFT             1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED4_MASK                0x1 /* rule20en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED4_SHIFT               2
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED5_MASK                0x1 /* rule21en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED5_SHIFT               3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED6_MASK                0x1 /* rule22en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED6_SHIFT               4
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED7_MASK                0x1 /* rule23en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED7_SHIFT               5
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED8_MASK                0x1 /* rule24en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED8_SHIFT               6
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED9_MASK                0x1 /* rule25en */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED9_SHIFT               7
	u8 flags14;
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT16_MASK                       0x1 /* bit16 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT16_SHIFT                      0
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT17_MASK                       0x1 /* bit17 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT17_SHIFT                      1
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT18_MASK                       0x1 /* bit18 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT18_SHIFT                      2
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT19_MASK                       0x1 /* bit19 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT19_SHIFT                      3
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT20_MASK                       0x1 /* bit20 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_BIT20_SHIFT                      4
#define E4_XSTORM_ISCSI_CONN_AG_CTX_DUMMY_READ_DONE_MASK             0x1 /* bit21 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_DUMMY_READ_DONE_SHIFT            5
#define E4_XSTORM_ISCSI_CONN_AG_CTX_PROC_ONLY_CLEANUP_MASK           0x3 /* cf23 */
#define E4_XSTORM_ISCSI_CONN_AG_CTX_PROC_ONLY_CLEANUP_SHIFT          6
	u8 byte2 /* byte2 */;
	__le16 physical_q0 /* physical_q0 */;
	__le16 physical_q1 /* physical_q1 */;
	__le16 dummy_dorq_var /* physical_q2 */;
	__le16 sq_cons /* word3 */;
	__le16 sq_prod /* word4 */;
	__le16 word5 /* word5 */;
	__le16 slow_io_total_data_tx_update /* conn_dpi */;
	u8 byte3 /* byte3 */;
	u8 byte4 /* byte4 */;
	u8 byte5 /* byte5 */;
	u8 byte6 /* byte6 */;
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
	__le32 reg2 /* reg2 */;
	__le32 more_to_send_seq /* reg3 */;
	__le32 reg4 /* reg4 */;
	__le32 reg5 /* cf_array0 */;
	__le32 hq_scan_next_relevant_ack /* cf_array1 */;
	__le16 r2tq_prod /* word7 */;
	__le16 r2tq_cons /* word8 */;
	__le16 hq_prod /* word9 */;
	__le16 hq_cons /* word10 */;
	__le32 remain_seq /* reg7 */;
	__le32 bytes_to_next_pdu /* reg8 */;
	__le32 hq_tcp_seq /* reg9 */;
	u8 byte7 /* byte7 */;
	u8 byte8 /* byte8 */;
	u8 byte9 /* byte9 */;
	u8 byte10 /* byte10 */;
	u8 byte11 /* byte11 */;
	u8 byte12 /* byte12 */;
	u8 byte13 /* byte13 */;
	u8 byte14 /* byte14 */;
	u8 byte15 /* byte15 */;
	u8 e5_reserved /* e5_reserved */;
	__le16 word11 /* word11 */;
	__le32 reg10 /* reg10 */;
	__le32 reg11 /* reg11 */;
	__le32 exp_stat_sn /* reg12 */;
	__le32 ongoing_fast_rxmit_seq /* reg13 */;
	__le32 reg14 /* reg14 */;
	__le32 reg15 /* reg15 */;
	__le32 reg16 /* reg16 */;
	__le32 reg17 /* reg17 */;
};

struct e4_tstorm_iscsi_conn_ag_ctx
{
	u8 reserved0 /* cdu_validation */;
	u8 state /* state */;
	u8 flags0;
#define E4_TSTORM_ISCSI_CONN_AG_CTX_EXIST_IN_QM0_MASK       0x1 /* exist_in_qm0 */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_EXIST_IN_QM0_SHIFT      0
#define E4_TSTORM_ISCSI_CONN_AG_CTX_BIT1_MASK               0x1 /* exist_in_qm1 */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_BIT1_SHIFT              1
#define E4_TSTORM_ISCSI_CONN_AG_CTX_BIT2_MASK               0x1 /* bit2 */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_BIT2_SHIFT              2
#define E4_TSTORM_ISCSI_CONN_AG_CTX_BIT3_MASK               0x1 /* bit3 */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_BIT3_SHIFT              3
#define E4_TSTORM_ISCSI_CONN_AG_CTX_BIT4_MASK               0x1 /* bit4 */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_BIT4_SHIFT              4
#define E4_TSTORM_ISCSI_CONN_AG_CTX_BIT5_MASK               0x1 /* bit5 */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_BIT5_SHIFT              5
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF0_MASK                0x3 /* timer0cf */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF0_SHIFT               6
	u8 flags1;
#define E4_TSTORM_ISCSI_CONN_AG_CTX_P2T_FLUSH_CF_MASK       0x3 /* timer1cf */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_P2T_FLUSH_CF_SHIFT      0
#define E4_TSTORM_ISCSI_CONN_AG_CTX_M2T_FLUSH_CF_MASK       0x3 /* timer2cf */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_M2T_FLUSH_CF_SHIFT      2
#define E4_TSTORM_ISCSI_CONN_AG_CTX_TIMER_STOP_ALL_MASK     0x3 /* timer_stop_all */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_TIMER_STOP_ALL_SHIFT    4
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF4_MASK                0x3 /* cf4 */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF4_SHIFT               6
	u8 flags2;
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF5_MASK                0x3 /* cf5 */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF5_SHIFT               0
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF6_MASK                0x3 /* cf6 */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF6_SHIFT               2
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF7_MASK                0x3 /* cf7 */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF7_SHIFT               4
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF8_MASK                0x3 /* cf8 */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF8_SHIFT               6
	u8 flags3;
#define E4_TSTORM_ISCSI_CONN_AG_CTX_FLUSH_Q0_MASK           0x3 /* cf9 */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_FLUSH_Q0_SHIFT          0
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF10_MASK               0x3 /* cf10 */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF10_SHIFT              2
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF0EN_MASK              0x1 /* cf0en */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF0EN_SHIFT             4
#define E4_TSTORM_ISCSI_CONN_AG_CTX_P2T_FLUSH_CF_EN_MASK    0x1 /* cf1en */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_P2T_FLUSH_CF_EN_SHIFT   5
#define E4_TSTORM_ISCSI_CONN_AG_CTX_M2T_FLUSH_CF_EN_MASK    0x1 /* cf2en */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_M2T_FLUSH_CF_EN_SHIFT   6
#define E4_TSTORM_ISCSI_CONN_AG_CTX_TIMER_STOP_ALL_EN_MASK  0x1 /* cf3en */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_TIMER_STOP_ALL_EN_SHIFT 7
	u8 flags4;
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF4EN_MASK              0x1 /* cf4en */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF4EN_SHIFT             0
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF5EN_MASK              0x1 /* cf5en */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF5EN_SHIFT             1
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF6EN_MASK              0x1 /* cf6en */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF6EN_SHIFT             2
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF7EN_MASK              0x1 /* cf7en */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF7EN_SHIFT             3
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF8EN_MASK              0x1 /* cf8en */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF8EN_SHIFT             4
#define E4_TSTORM_ISCSI_CONN_AG_CTX_FLUSH_Q0_EN_MASK        0x1 /* cf9en */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_FLUSH_Q0_EN_SHIFT       5
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF10EN_MASK             0x1 /* cf10en */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_CF10EN_SHIFT            6
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE0EN_MASK            0x1 /* rule0en */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE0EN_SHIFT           7
	u8 flags5;
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE1EN_MASK            0x1 /* rule1en */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE1EN_SHIFT           0
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE2EN_MASK            0x1 /* rule2en */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE2EN_SHIFT           1
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE3EN_MASK            0x1 /* rule3en */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE3EN_SHIFT           2
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE4EN_MASK            0x1 /* rule4en */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE4EN_SHIFT           3
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE5EN_MASK            0x1 /* rule5en */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE5EN_SHIFT           4
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE6EN_MASK            0x1 /* rule6en */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE6EN_SHIFT           5
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE7EN_MASK            0x1 /* rule7en */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE7EN_SHIFT           6
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE8EN_MASK            0x1 /* rule8en */
#define E4_TSTORM_ISCSI_CONN_AG_CTX_RULE8EN_SHIFT           7
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
	__le32 rx_tcp_checksum_err_cnt /* reg2 */;
	__le32 reg3 /* reg3 */;
	__le32 reg4 /* reg4 */;
	__le32 reg5 /* reg5 */;
	__le32 reg6 /* reg6 */;
	__le32 reg7 /* reg7 */;
	__le32 reg8 /* reg8 */;
	u8 cid_offload_cnt /* byte2 */;
	u8 byte3 /* byte3 */;
	__le16 word0 /* word0 */;
};

struct e4_ustorm_iscsi_conn_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 byte1 /* state */;
	u8 flags0;
#define E4_USTORM_ISCSI_CONN_AG_CTX_BIT0_MASK     0x1 /* exist_in_qm0 */
#define E4_USTORM_ISCSI_CONN_AG_CTX_BIT0_SHIFT    0
#define E4_USTORM_ISCSI_CONN_AG_CTX_BIT1_MASK     0x1 /* exist_in_qm1 */
#define E4_USTORM_ISCSI_CONN_AG_CTX_BIT1_SHIFT    1
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF0_MASK      0x3 /* timer0cf */
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF0_SHIFT     2
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF1_MASK      0x3 /* timer1cf */
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF1_SHIFT     4
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF2_MASK      0x3 /* timer2cf */
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF2_SHIFT     6
	u8 flags1;
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF3_MASK      0x3 /* timer_stop_all */
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF3_SHIFT     0
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF4_MASK      0x3 /* cf4 */
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF4_SHIFT     2
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF5_MASK      0x3 /* cf5 */
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF5_SHIFT     4
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF6_MASK      0x3 /* cf6 */
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF6_SHIFT     6
	u8 flags2;
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF0EN_MASK    0x1 /* cf0en */
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF0EN_SHIFT   0
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF1EN_MASK    0x1 /* cf1en */
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF1EN_SHIFT   1
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF2EN_MASK    0x1 /* cf2en */
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF2EN_SHIFT   2
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF3EN_MASK    0x1 /* cf3en */
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF3EN_SHIFT   3
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF4EN_MASK    0x1 /* cf4en */
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF4EN_SHIFT   4
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF5EN_MASK    0x1 /* cf5en */
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF5EN_SHIFT   5
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF6EN_MASK    0x1 /* cf6en */
#define E4_USTORM_ISCSI_CONN_AG_CTX_CF6EN_SHIFT   6
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE0EN_MASK  0x1 /* rule0en */
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE0EN_SHIFT 7
	u8 flags3;
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE1EN_MASK  0x1 /* rule1en */
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE1EN_SHIFT 0
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE2EN_MASK  0x1 /* rule2en */
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE2EN_SHIFT 1
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE3EN_MASK  0x1 /* rule3en */
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE3EN_SHIFT 2
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE4EN_MASK  0x1 /* rule4en */
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE4EN_SHIFT 3
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE5EN_MASK  0x1 /* rule5en */
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE5EN_SHIFT 4
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE6EN_MASK  0x1 /* rule6en */
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE6EN_SHIFT 5
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE7EN_MASK  0x1 /* rule7en */
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE7EN_SHIFT 6
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE8EN_MASK  0x1 /* rule8en */
#define E4_USTORM_ISCSI_CONN_AG_CTX_RULE8EN_SHIFT 7
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

/*
 * The iscsi storm connection context of Tstorm
 */
struct tstorm_iscsi_conn_st_ctx
{
	__le32 reserved[44];
};

struct e4_mstorm_iscsi_conn_ag_ctx
{
	u8 reserved /* cdu_validation */;
	u8 state /* state */;
	u8 flags0;
#define E4_MSTORM_ISCSI_CONN_AG_CTX_BIT0_MASK     0x1 /* exist_in_qm0 */
#define E4_MSTORM_ISCSI_CONN_AG_CTX_BIT0_SHIFT    0
#define E4_MSTORM_ISCSI_CONN_AG_CTX_BIT1_MASK     0x1 /* exist_in_qm1 */
#define E4_MSTORM_ISCSI_CONN_AG_CTX_BIT1_SHIFT    1
#define E4_MSTORM_ISCSI_CONN_AG_CTX_CF0_MASK      0x3 /* cf0 */
#define E4_MSTORM_ISCSI_CONN_AG_CTX_CF0_SHIFT     2
#define E4_MSTORM_ISCSI_CONN_AG_CTX_CF1_MASK      0x3 /* cf1 */
#define E4_MSTORM_ISCSI_CONN_AG_CTX_CF1_SHIFT     4
#define E4_MSTORM_ISCSI_CONN_AG_CTX_CF2_MASK      0x3 /* cf2 */
#define E4_MSTORM_ISCSI_CONN_AG_CTX_CF2_SHIFT     6
	u8 flags1;
#define E4_MSTORM_ISCSI_CONN_AG_CTX_CF0EN_MASK    0x1 /* cf0en */
#define E4_MSTORM_ISCSI_CONN_AG_CTX_CF0EN_SHIFT   0
#define E4_MSTORM_ISCSI_CONN_AG_CTX_CF1EN_MASK    0x1 /* cf1en */
#define E4_MSTORM_ISCSI_CONN_AG_CTX_CF1EN_SHIFT   1
#define E4_MSTORM_ISCSI_CONN_AG_CTX_CF2EN_MASK    0x1 /* cf2en */
#define E4_MSTORM_ISCSI_CONN_AG_CTX_CF2EN_SHIFT   2
#define E4_MSTORM_ISCSI_CONN_AG_CTX_RULE0EN_MASK  0x1 /* rule0en */
#define E4_MSTORM_ISCSI_CONN_AG_CTX_RULE0EN_SHIFT 3
#define E4_MSTORM_ISCSI_CONN_AG_CTX_RULE1EN_MASK  0x1 /* rule1en */
#define E4_MSTORM_ISCSI_CONN_AG_CTX_RULE1EN_SHIFT 4
#define E4_MSTORM_ISCSI_CONN_AG_CTX_RULE2EN_MASK  0x1 /* rule2en */
#define E4_MSTORM_ISCSI_CONN_AG_CTX_RULE2EN_SHIFT 5
#define E4_MSTORM_ISCSI_CONN_AG_CTX_RULE3EN_MASK  0x1 /* rule3en */
#define E4_MSTORM_ISCSI_CONN_AG_CTX_RULE3EN_SHIFT 6
#define E4_MSTORM_ISCSI_CONN_AG_CTX_RULE4EN_MASK  0x1 /* rule4en */
#define E4_MSTORM_ISCSI_CONN_AG_CTX_RULE4EN_SHIFT 7
	__le16 word0 /* word0 */;
	__le16 word1 /* word1 */;
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
};

/*
 * Combined iSCSI and TCP storm connection of Mstorm
 */
struct mstorm_iscsi_tcp_conn_st_ctx
{
	__le32 reserved_tcp[20];
	__le32 reserved_iscsi[12];
};

/*
 * The iscsi storm context of Ustorm
 */
struct ustorm_iscsi_conn_st_ctx
{
	__le32 reserved[52];
};

/*
 * iscsi connection context
 */
struct e4_iscsi_conn_context
{
	struct ystorm_iscsi_conn_st_ctx ystorm_st_context /* ystorm storm context */;
	struct pstorm_iscsi_tcp_conn_st_ctx pstorm_st_context /* pstorm storm context */;
	struct regpair pstorm_st_padding[2] /* padding */;
	struct pb_context xpb2_context /* xpb2 context */;
	struct xstorm_iscsi_tcp_conn_st_ctx xstorm_st_context /* xstorm storm context */;
	struct regpair xstorm_st_padding[2] /* padding */;
	struct e4_xstorm_iscsi_conn_ag_ctx xstorm_ag_context /* xstorm aggregative context */;
	struct e4_tstorm_iscsi_conn_ag_ctx tstorm_ag_context /* tstorm aggregative context */;
	struct regpair tstorm_ag_padding[2] /* padding */;
	struct timers_context timer_context /* timer context */;
	struct e4_ustorm_iscsi_conn_ag_ctx ustorm_ag_context /* ustorm aggregative context */;
	struct pb_context upb_context /* upb context */;
	struct tstorm_iscsi_conn_st_ctx tstorm_st_context /* tstorm storm context */;
	struct regpair tstorm_st_padding[2] /* padding */;
	struct e4_mstorm_iscsi_conn_ag_ctx mstorm_ag_context /* mstorm aggregative context */;
	struct mstorm_iscsi_tcp_conn_st_ctx mstorm_st_context /* mstorm storm context */;
	struct ustorm_iscsi_conn_st_ctx ustorm_st_context /* ustorm storm context */;
};


struct e5_xstorm_iscsi_conn_ag_ctx
{
	u8 cdu_validation /* cdu_validation */;
	u8 state_and_core_id /* state_and_core_id */;
	u8 flags0;
#define E5_XSTORM_ISCSI_CONN_AG_CTX_EXIST_IN_QM0_MASK                0x1 /* exist_in_qm0 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_EXIST_IN_QM0_SHIFT               0
#define E5_XSTORM_ISCSI_CONN_AG_CTX_EXIST_IN_QM1_MASK                0x1 /* exist_in_qm1 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_EXIST_IN_QM1_SHIFT               1
#define E5_XSTORM_ISCSI_CONN_AG_CTX_RESERVED1_MASK                   0x1 /* exist_in_qm2 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_RESERVED1_SHIFT                  2
#define E5_XSTORM_ISCSI_CONN_AG_CTX_EXIST_IN_QM3_MASK                0x1 /* exist_in_qm3 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_EXIST_IN_QM3_SHIFT               3
#define E5_XSTORM_ISCSI_CONN_AG_CTX_BIT4_MASK                        0x1 /* bit4 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_BIT4_SHIFT                       4
#define E5_XSTORM_ISCSI_CONN_AG_CTX_RESERVED2_MASK                   0x1 /* cf_array_active */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_RESERVED2_SHIFT                  5
#define E5_XSTORM_ISCSI_CONN_AG_CTX_BIT6_MASK                        0x1 /* bit6 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_BIT6_SHIFT                       6
#define E5_XSTORM_ISCSI_CONN_AG_CTX_BIT7_MASK                        0x1 /* bit7 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_BIT7_SHIFT                       7
	u8 flags1;
#define E5_XSTORM_ISCSI_CONN_AG_CTX_BIT8_MASK                        0x1 /* bit8 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_BIT8_SHIFT                       0
#define E5_XSTORM_ISCSI_CONN_AG_CTX_BIT9_MASK                        0x1 /* bit9 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_BIT9_SHIFT                       1
#define E5_XSTORM_ISCSI_CONN_AG_CTX_BIT10_MASK                       0x1 /* bit10 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_BIT10_SHIFT                      2
#define E5_XSTORM_ISCSI_CONN_AG_CTX_BIT11_MASK                       0x1 /* bit11 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_BIT11_SHIFT                      3
#define E5_XSTORM_ISCSI_CONN_AG_CTX_BIT12_MASK                       0x1 /* bit12 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_BIT12_SHIFT                      4
#define E5_XSTORM_ISCSI_CONN_AG_CTX_BIT13_MASK                       0x1 /* bit13 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_BIT13_SHIFT                      5
#define E5_XSTORM_ISCSI_CONN_AG_CTX_BIT14_MASK                       0x1 /* bit14 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_BIT14_SHIFT                      6
#define E5_XSTORM_ISCSI_CONN_AG_CTX_TX_TRUNCATE_MASK                 0x1 /* bit15 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_TX_TRUNCATE_SHIFT                7
	u8 flags2;
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF0_MASK                         0x3 /* timer0cf */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF0_SHIFT                        0
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF1_MASK                         0x3 /* timer1cf */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF1_SHIFT                        2
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF2_MASK                         0x3 /* timer2cf */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF2_SHIFT                        4
#define E5_XSTORM_ISCSI_CONN_AG_CTX_TIMER_STOP_ALL_MASK              0x3 /* timer_stop_all */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_TIMER_STOP_ALL_SHIFT             6
	u8 flags3;
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF4_MASK                         0x3 /* cf4 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF4_SHIFT                        0
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF5_MASK                         0x3 /* cf5 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF5_SHIFT                        2
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF6_MASK                         0x3 /* cf6 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF6_SHIFT                        4
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF7_MASK                         0x3 /* cf7 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF7_SHIFT                        6
	u8 flags4;
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF8_MASK                         0x3 /* cf8 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF8_SHIFT                        0
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF9_MASK                         0x3 /* cf9 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF9_SHIFT                        2
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF10_MASK                        0x3 /* cf10 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF10_SHIFT                       4
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF11_MASK                        0x3 /* cf11 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF11_SHIFT                       6
	u8 flags5;
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF12_MASK                        0x3 /* cf12 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF12_SHIFT                       0
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF13_MASK                        0x3 /* cf13 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF13_SHIFT                       2
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF14_MASK                        0x3 /* cf14 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF14_SHIFT                       4
#define E5_XSTORM_ISCSI_CONN_AG_CTX_UPDATE_STATE_TO_BASE_CF_MASK     0x3 /* cf15 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_UPDATE_STATE_TO_BASE_CF_SHIFT    6
	u8 flags6;
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF16_MASK                        0x3 /* cf16 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF16_SHIFT                       0
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF17_MASK                        0x3 /* cf_array_cf */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF17_SHIFT                       2
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF18_MASK                        0x3 /* cf18 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF18_SHIFT                       4
#define E5_XSTORM_ISCSI_CONN_AG_CTX_DQ_FLUSH_MASK                    0x3 /* cf19 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_DQ_FLUSH_SHIFT                   6
	u8 flags7;
#define E5_XSTORM_ISCSI_CONN_AG_CTX_MST_XCM_Q0_FLUSH_CF_MASK         0x3 /* cf20 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_MST_XCM_Q0_FLUSH_CF_SHIFT        0
#define E5_XSTORM_ISCSI_CONN_AG_CTX_UST_XCM_Q1_FLUSH_CF_MASK         0x3 /* cf21 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_UST_XCM_Q1_FLUSH_CF_SHIFT        2
#define E5_XSTORM_ISCSI_CONN_AG_CTX_SLOW_PATH_MASK                   0x3 /* cf22 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_SLOW_PATH_SHIFT                  4
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF0EN_MASK                       0x1 /* cf0en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF0EN_SHIFT                      6
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF1EN_MASK                       0x1 /* cf1en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF1EN_SHIFT                      7
	u8 flags8;
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF2EN_MASK                       0x1 /* cf2en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF2EN_SHIFT                      0
#define E5_XSTORM_ISCSI_CONN_AG_CTX_TIMER_STOP_ALL_EN_MASK           0x1 /* cf3en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_TIMER_STOP_ALL_EN_SHIFT          1
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF4EN_MASK                       0x1 /* cf4en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF4EN_SHIFT                      2
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF5EN_MASK                       0x1 /* cf5en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF5EN_SHIFT                      3
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF6EN_MASK                       0x1 /* cf6en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF6EN_SHIFT                      4
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF7EN_MASK                       0x1 /* cf7en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF7EN_SHIFT                      5
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF8EN_MASK                       0x1 /* cf8en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF8EN_SHIFT                      6
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF9EN_MASK                       0x1 /* cf9en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF9EN_SHIFT                      7
	u8 flags9;
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF10EN_MASK                      0x1 /* cf10en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF10EN_SHIFT                     0
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF11EN_MASK                      0x1 /* cf11en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF11EN_SHIFT                     1
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF12EN_MASK                      0x1 /* cf12en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF12EN_SHIFT                     2
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF13EN_MASK                      0x1 /* cf13en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF13EN_SHIFT                     3
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF14EN_MASK                      0x1 /* cf14en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF14EN_SHIFT                     4
#define E5_XSTORM_ISCSI_CONN_AG_CTX_UPDATE_STATE_TO_BASE_CF_EN_MASK  0x1 /* cf15en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_UPDATE_STATE_TO_BASE_CF_EN_SHIFT 5
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF16EN_MASK                      0x1 /* cf16en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF16EN_SHIFT                     6
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF17EN_MASK                      0x1 /* cf_array_cf_en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF17EN_SHIFT                     7
	u8 flags10;
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF18EN_MASK                      0x1 /* cf18en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_CF18EN_SHIFT                     0
#define E5_XSTORM_ISCSI_CONN_AG_CTX_DQ_FLUSH_EN_MASK                 0x1 /* cf19en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_DQ_FLUSH_EN_SHIFT                1
#define E5_XSTORM_ISCSI_CONN_AG_CTX_MST_XCM_Q0_FLUSH_CF_EN_MASK      0x1 /* cf20en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_MST_XCM_Q0_FLUSH_CF_EN_SHIFT     2
#define E5_XSTORM_ISCSI_CONN_AG_CTX_UST_XCM_Q1_FLUSH_CF_EN_MASK      0x1 /* cf21en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_UST_XCM_Q1_FLUSH_CF_EN_SHIFT     3
#define E5_XSTORM_ISCSI_CONN_AG_CTX_SLOW_PATH_EN_MASK                0x1 /* cf22en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_SLOW_PATH_EN_SHIFT               4
#define E5_XSTORM_ISCSI_CONN_AG_CTX_PROC_ONLY_CLEANUP_EN_MASK        0x1 /* cf23en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_PROC_ONLY_CLEANUP_EN_SHIFT       5
#define E5_XSTORM_ISCSI_CONN_AG_CTX_RULE0EN_MASK                     0x1 /* rule0en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_RULE0EN_SHIFT                    6
#define E5_XSTORM_ISCSI_CONN_AG_CTX_MORE_TO_SEND_DEC_RULE_EN_MASK    0x1 /* rule1en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_MORE_TO_SEND_DEC_RULE_EN_SHIFT   7
	u8 flags11;
#define E5_XSTORM_ISCSI_CONN_AG_CTX_TX_BLOCKED_EN_MASK               0x1 /* rule2en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_TX_BLOCKED_EN_SHIFT              0
#define E5_XSTORM_ISCSI_CONN_AG_CTX_RULE3EN_MASK                     0x1 /* rule3en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_RULE3EN_SHIFT                    1
#define E5_XSTORM_ISCSI_CONN_AG_CTX_RESERVED3_MASK                   0x1 /* rule4en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_RESERVED3_SHIFT                  2
#define E5_XSTORM_ISCSI_CONN_AG_CTX_RULE5EN_MASK                     0x1 /* rule5en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_RULE5EN_SHIFT                    3
#define E5_XSTORM_ISCSI_CONN_AG_CTX_RULE6EN_MASK                     0x1 /* rule6en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_RULE6EN_SHIFT                    4
#define E5_XSTORM_ISCSI_CONN_AG_CTX_RULE7EN_MASK                     0x1 /* rule7en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_RULE7EN_SHIFT                    5
#define E5_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED1_MASK                0x1 /* rule8en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED1_SHIFT               6
#define E5_XSTORM_ISCSI_CONN_AG_CTX_RULE9EN_MASK                     0x1 /* rule9en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_RULE9EN_SHIFT                    7
	u8 flags12;
#define E5_XSTORM_ISCSI_CONN_AG_CTX_SQ_DEC_RULE_EN_MASK              0x1 /* rule10en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_SQ_DEC_RULE_EN_SHIFT             0
#define E5_XSTORM_ISCSI_CONN_AG_CTX_RULE11EN_MASK                    0x1 /* rule11en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_RULE11EN_SHIFT                   1
#define E5_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED2_MASK                0x1 /* rule12en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED2_SHIFT               2
#define E5_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED3_MASK                0x1 /* rule13en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED3_SHIFT               3
#define E5_XSTORM_ISCSI_CONN_AG_CTX_RULE14EN_MASK                    0x1 /* rule14en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_RULE14EN_SHIFT                   4
#define E5_XSTORM_ISCSI_CONN_AG_CTX_RULE15EN_MASK                    0x1 /* rule15en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_RULE15EN_SHIFT                   5
#define E5_XSTORM_ISCSI_CONN_AG_CTX_RULE16EN_MASK                    0x1 /* rule16en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_RULE16EN_SHIFT                   6
#define E5_XSTORM_ISCSI_CONN_AG_CTX_RULE17EN_MASK                    0x1 /* rule17en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_RULE17EN_SHIFT                   7
	u8 flags13;
#define E5_XSTORM_ISCSI_CONN_AG_CTX_R2TQ_DEC_RULE_EN_MASK            0x1 /* rule18en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_R2TQ_DEC_RULE_EN_SHIFT           0
#define E5_XSTORM_ISCSI_CONN_AG_CTX_HQ_DEC_RULE_EN_MASK              0x1 /* rule19en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_HQ_DEC_RULE_EN_SHIFT             1
#define E5_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED4_MASK                0x1 /* rule20en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED4_SHIFT               2
#define E5_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED5_MASK                0x1 /* rule21en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED5_SHIFT               3
#define E5_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED6_MASK                0x1 /* rule22en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED6_SHIFT               4
#define E5_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED7_MASK                0x1 /* rule23en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED7_SHIFT               5
#define E5_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED8_MASK                0x1 /* rule24en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED8_SHIFT               6
#define E5_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED9_MASK                0x1 /* rule25en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_A0_RESERVED9_SHIFT               7
	u8 flags14;
#define E5_XSTORM_ISCSI_CONN_AG_CTX_BIT16_MASK                       0x1 /* bit16 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_BIT16_SHIFT                      0
#define E5_XSTORM_ISCSI_CONN_AG_CTX_BIT17_MASK                       0x1 /* bit17 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_BIT17_SHIFT                      1
#define E5_XSTORM_ISCSI_CONN_AG_CTX_BIT18_MASK                       0x1 /* bit18 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_BIT18_SHIFT                      2
#define E5_XSTORM_ISCSI_CONN_AG_CTX_BIT19_MASK                       0x1 /* bit19 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_BIT19_SHIFT                      3
#define E5_XSTORM_ISCSI_CONN_AG_CTX_BIT20_MASK                       0x1 /* bit20 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_BIT20_SHIFT                      4
#define E5_XSTORM_ISCSI_CONN_AG_CTX_DUMMY_READ_DONE_MASK             0x1 /* bit21 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_DUMMY_READ_DONE_SHIFT            5
#define E5_XSTORM_ISCSI_CONN_AG_CTX_PROC_ONLY_CLEANUP_MASK           0x3 /* cf23 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_PROC_ONLY_CLEANUP_SHIFT          6
	u8 byte2 /* byte2 */;
	__le16 physical_q0 /* physical_q0 */;
	__le16 physical_q1 /* physical_q1 */;
	__le16 dummy_dorq_var /* physical_q2 */;
	__le16 sq_cons /* word3 */;
	__le16 sq_prod /* word4 */;
	__le16 word5 /* word5 */;
	__le16 slow_io_total_data_tx_update /* conn_dpi */;
	u8 byte3 /* byte3 */;
	u8 byte4 /* byte4 */;
	u8 byte5 /* byte5 */;
	u8 byte6 /* byte6 */;
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
	__le32 reg2 /* reg2 */;
	__le32 more_to_send_seq /* reg3 */;
	__le32 reg4 /* reg4 */;
	__le32 reg5 /* cf_array0 */;
	__le32 hq_scan_next_relevant_ack /* cf_array1 */;
	u8 flags15;
#define E5_XSTORM_ISCSI_CONN_AG_CTX_E4_RESERVED1_MASK                0x1 /* bit22 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_E4_RESERVED1_SHIFT               0
#define E5_XSTORM_ISCSI_CONN_AG_CTX_E4_RESERVED2_MASK                0x1 /* bit23 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_E4_RESERVED2_SHIFT               1
#define E5_XSTORM_ISCSI_CONN_AG_CTX_E4_RESERVED3_MASK                0x1 /* bit24 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_E4_RESERVED3_SHIFT               2
#define E5_XSTORM_ISCSI_CONN_AG_CTX_E4_RESERVED4_MASK                0x3 /* cf24 */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_E4_RESERVED4_SHIFT               3
#define E5_XSTORM_ISCSI_CONN_AG_CTX_E4_RESERVED5_MASK                0x1 /* cf24en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_E4_RESERVED5_SHIFT               5
#define E5_XSTORM_ISCSI_CONN_AG_CTX_E4_RESERVED6_MASK                0x1 /* rule26en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_E4_RESERVED6_SHIFT               6
#define E5_XSTORM_ISCSI_CONN_AG_CTX_E4_RESERVED7_MASK                0x1 /* rule27en */
#define E5_XSTORM_ISCSI_CONN_AG_CTX_E4_RESERVED7_SHIFT               7
	u8 byte7 /* byte7 */;
	__le16 r2tq_prod /* word7 */;
	__le16 r2tq_cons /* word8 */;
	__le16 hq_prod /* word9 */;
	__le16 hq_cons /* word10 */;
	__le16 word11 /* word11 */;
	__le32 remain_seq /* reg7 */;
	__le32 bytes_to_next_pdu /* reg8 */;
	__le32 hq_tcp_seq /* reg9 */;
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
	__le32 ongoing_fast_rxmit_seq /* reg13 */;
	__le32 exp_stat_sn /* reg14 */;
	__le32 reg15 /* reg15 */;
	__le32 reg16 /* reg16 */;
	__le32 reg17 /* reg17 */;
};

struct e5_tstorm_iscsi_conn_ag_ctx
{
	u8 reserved0 /* cdu_validation */;
	u8 state_and_core_id /* state_and_core_id */;
	u8 flags0;
#define E5_TSTORM_ISCSI_CONN_AG_CTX_EXIST_IN_QM0_MASK       0x1 /* exist_in_qm0 */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_EXIST_IN_QM0_SHIFT      0
#define E5_TSTORM_ISCSI_CONN_AG_CTX_BIT1_MASK               0x1 /* exist_in_qm1 */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_BIT1_SHIFT              1
#define E5_TSTORM_ISCSI_CONN_AG_CTX_BIT2_MASK               0x1 /* bit2 */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_BIT2_SHIFT              2
#define E5_TSTORM_ISCSI_CONN_AG_CTX_BIT3_MASK               0x1 /* bit3 */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_BIT3_SHIFT              3
#define E5_TSTORM_ISCSI_CONN_AG_CTX_BIT4_MASK               0x1 /* bit4 */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_BIT4_SHIFT              4
#define E5_TSTORM_ISCSI_CONN_AG_CTX_BIT5_MASK               0x1 /* bit5 */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_BIT5_SHIFT              5
#define E5_TSTORM_ISCSI_CONN_AG_CTX_CF0_MASK                0x3 /* timer0cf */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_CF0_SHIFT               6
	u8 flags1;
#define E5_TSTORM_ISCSI_CONN_AG_CTX_P2T_FLUSH_CF_MASK       0x3 /* timer1cf */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_P2T_FLUSH_CF_SHIFT      0
#define E5_TSTORM_ISCSI_CONN_AG_CTX_M2T_FLUSH_CF_MASK       0x3 /* timer2cf */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_M2T_FLUSH_CF_SHIFT      2
#define E5_TSTORM_ISCSI_CONN_AG_CTX_TIMER_STOP_ALL_MASK     0x3 /* timer_stop_all */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_TIMER_STOP_ALL_SHIFT    4
#define E5_TSTORM_ISCSI_CONN_AG_CTX_CF4_MASK                0x3 /* cf4 */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_CF4_SHIFT               6
	u8 flags2;
#define E5_TSTORM_ISCSI_CONN_AG_CTX_CF5_MASK                0x3 /* cf5 */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_CF5_SHIFT               0
#define E5_TSTORM_ISCSI_CONN_AG_CTX_CF6_MASK                0x3 /* cf6 */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_CF6_SHIFT               2
#define E5_TSTORM_ISCSI_CONN_AG_CTX_CF7_MASK                0x3 /* cf7 */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_CF7_SHIFT               4
#define E5_TSTORM_ISCSI_CONN_AG_CTX_CF8_MASK                0x3 /* cf8 */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_CF8_SHIFT               6
	u8 flags3;
#define E5_TSTORM_ISCSI_CONN_AG_CTX_FLUSH_Q0_MASK           0x3 /* cf9 */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_FLUSH_Q0_SHIFT          0
#define E5_TSTORM_ISCSI_CONN_AG_CTX_CF10_MASK               0x3 /* cf10 */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_CF10_SHIFT              2
#define E5_TSTORM_ISCSI_CONN_AG_CTX_CF0EN_MASK              0x1 /* cf0en */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_CF0EN_SHIFT             4
#define E5_TSTORM_ISCSI_CONN_AG_CTX_P2T_FLUSH_CF_EN_MASK    0x1 /* cf1en */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_P2T_FLUSH_CF_EN_SHIFT   5
#define E5_TSTORM_ISCSI_CONN_AG_CTX_M2T_FLUSH_CF_EN_MASK    0x1 /* cf2en */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_M2T_FLUSH_CF_EN_SHIFT   6
#define E5_TSTORM_ISCSI_CONN_AG_CTX_TIMER_STOP_ALL_EN_MASK  0x1 /* cf3en */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_TIMER_STOP_ALL_EN_SHIFT 7
	u8 flags4;
#define E5_TSTORM_ISCSI_CONN_AG_CTX_CF4EN_MASK              0x1 /* cf4en */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_CF4EN_SHIFT             0
#define E5_TSTORM_ISCSI_CONN_AG_CTX_CF5EN_MASK              0x1 /* cf5en */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_CF5EN_SHIFT             1
#define E5_TSTORM_ISCSI_CONN_AG_CTX_CF6EN_MASK              0x1 /* cf6en */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_CF6EN_SHIFT             2
#define E5_TSTORM_ISCSI_CONN_AG_CTX_CF7EN_MASK              0x1 /* cf7en */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_CF7EN_SHIFT             3
#define E5_TSTORM_ISCSI_CONN_AG_CTX_CF8EN_MASK              0x1 /* cf8en */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_CF8EN_SHIFT             4
#define E5_TSTORM_ISCSI_CONN_AG_CTX_FLUSH_Q0_EN_MASK        0x1 /* cf9en */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_FLUSH_Q0_EN_SHIFT       5
#define E5_TSTORM_ISCSI_CONN_AG_CTX_CF10EN_MASK             0x1 /* cf10en */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_CF10EN_SHIFT            6
#define E5_TSTORM_ISCSI_CONN_AG_CTX_RULE0EN_MASK            0x1 /* rule0en */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_RULE0EN_SHIFT           7
	u8 flags5;
#define E5_TSTORM_ISCSI_CONN_AG_CTX_RULE1EN_MASK            0x1 /* rule1en */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_RULE1EN_SHIFT           0
#define E5_TSTORM_ISCSI_CONN_AG_CTX_RULE2EN_MASK            0x1 /* rule2en */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_RULE2EN_SHIFT           1
#define E5_TSTORM_ISCSI_CONN_AG_CTX_RULE3EN_MASK            0x1 /* rule3en */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_RULE3EN_SHIFT           2
#define E5_TSTORM_ISCSI_CONN_AG_CTX_RULE4EN_MASK            0x1 /* rule4en */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_RULE4EN_SHIFT           3
#define E5_TSTORM_ISCSI_CONN_AG_CTX_RULE5EN_MASK            0x1 /* rule5en */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_RULE5EN_SHIFT           4
#define E5_TSTORM_ISCSI_CONN_AG_CTX_RULE6EN_MASK            0x1 /* rule6en */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_RULE6EN_SHIFT           5
#define E5_TSTORM_ISCSI_CONN_AG_CTX_RULE7EN_MASK            0x1 /* rule7en */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_RULE7EN_SHIFT           6
#define E5_TSTORM_ISCSI_CONN_AG_CTX_RULE8EN_MASK            0x1 /* rule8en */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_RULE8EN_SHIFT           7
	u8 flags6;
#define E5_TSTORM_ISCSI_CONN_AG_CTX_E4_RESERVED1_MASK       0x1 /* bit6 */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_E4_RESERVED1_SHIFT      0
#define E5_TSTORM_ISCSI_CONN_AG_CTX_E4_RESERVED2_MASK       0x1 /* bit7 */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_E4_RESERVED2_SHIFT      1
#define E5_TSTORM_ISCSI_CONN_AG_CTX_E4_RESERVED3_MASK       0x1 /* bit8 */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_E4_RESERVED3_SHIFT      2
#define E5_TSTORM_ISCSI_CONN_AG_CTX_E4_RESERVED4_MASK       0x3 /* cf11 */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_E4_RESERVED4_SHIFT      3
#define E5_TSTORM_ISCSI_CONN_AG_CTX_E4_RESERVED5_MASK       0x1 /* cf11en */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_E4_RESERVED5_SHIFT      5
#define E5_TSTORM_ISCSI_CONN_AG_CTX_E4_RESERVED6_MASK       0x1 /* rule9en */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_E4_RESERVED6_SHIFT      6
#define E5_TSTORM_ISCSI_CONN_AG_CTX_E4_RESERVED7_MASK       0x1 /* rule10en */
#define E5_TSTORM_ISCSI_CONN_AG_CTX_E4_RESERVED7_SHIFT      7
	u8 cid_offload_cnt /* byte2 */;
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
};

struct e5_ustorm_iscsi_conn_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 byte1 /* state_and_core_id */;
	u8 flags0;
#define E5_USTORM_ISCSI_CONN_AG_CTX_BIT0_MASK          0x1 /* exist_in_qm0 */
#define E5_USTORM_ISCSI_CONN_AG_CTX_BIT0_SHIFT         0
#define E5_USTORM_ISCSI_CONN_AG_CTX_BIT1_MASK          0x1 /* exist_in_qm1 */
#define E5_USTORM_ISCSI_CONN_AG_CTX_BIT1_SHIFT         1
#define E5_USTORM_ISCSI_CONN_AG_CTX_CF0_MASK           0x3 /* timer0cf */
#define E5_USTORM_ISCSI_CONN_AG_CTX_CF0_SHIFT          2
#define E5_USTORM_ISCSI_CONN_AG_CTX_CF1_MASK           0x3 /* timer1cf */
#define E5_USTORM_ISCSI_CONN_AG_CTX_CF1_SHIFT          4
#define E5_USTORM_ISCSI_CONN_AG_CTX_CF2_MASK           0x3 /* timer2cf */
#define E5_USTORM_ISCSI_CONN_AG_CTX_CF2_SHIFT          6
	u8 flags1;
#define E5_USTORM_ISCSI_CONN_AG_CTX_CF3_MASK           0x3 /* timer_stop_all */
#define E5_USTORM_ISCSI_CONN_AG_CTX_CF3_SHIFT          0
#define E5_USTORM_ISCSI_CONN_AG_CTX_CF4_MASK           0x3 /* cf4 */
#define E5_USTORM_ISCSI_CONN_AG_CTX_CF4_SHIFT          2
#define E5_USTORM_ISCSI_CONN_AG_CTX_CF5_MASK           0x3 /* cf5 */
#define E5_USTORM_ISCSI_CONN_AG_CTX_CF5_SHIFT          4
#define E5_USTORM_ISCSI_CONN_AG_CTX_CF6_MASK           0x3 /* cf6 */
#define E5_USTORM_ISCSI_CONN_AG_CTX_CF6_SHIFT          6
	u8 flags2;
#define E5_USTORM_ISCSI_CONN_AG_CTX_CF0EN_MASK         0x1 /* cf0en */
#define E5_USTORM_ISCSI_CONN_AG_CTX_CF0EN_SHIFT        0
#define E5_USTORM_ISCSI_CONN_AG_CTX_CF1EN_MASK         0x1 /* cf1en */
#define E5_USTORM_ISCSI_CONN_AG_CTX_CF1EN_SHIFT        1
#define E5_USTORM_ISCSI_CONN_AG_CTX_CF2EN_MASK         0x1 /* cf2en */
#define E5_USTORM_ISCSI_CONN_AG_CTX_CF2EN_SHIFT        2
#define E5_USTORM_ISCSI_CONN_AG_CTX_CF3EN_MASK         0x1 /* cf3en */
#define E5_USTORM_ISCSI_CONN_AG_CTX_CF3EN_SHIFT        3
#define E5_USTORM_ISCSI_CONN_AG_CTX_CF4EN_MASK         0x1 /* cf4en */
#define E5_USTORM_ISCSI_CONN_AG_CTX_CF4EN_SHIFT        4
#define E5_USTORM_ISCSI_CONN_AG_CTX_CF5EN_MASK         0x1 /* cf5en */
#define E5_USTORM_ISCSI_CONN_AG_CTX_CF5EN_SHIFT        5
#define E5_USTORM_ISCSI_CONN_AG_CTX_CF6EN_MASK         0x1 /* cf6en */
#define E5_USTORM_ISCSI_CONN_AG_CTX_CF6EN_SHIFT        6
#define E5_USTORM_ISCSI_CONN_AG_CTX_RULE0EN_MASK       0x1 /* rule0en */
#define E5_USTORM_ISCSI_CONN_AG_CTX_RULE0EN_SHIFT      7
	u8 flags3;
#define E5_USTORM_ISCSI_CONN_AG_CTX_RULE1EN_MASK       0x1 /* rule1en */
#define E5_USTORM_ISCSI_CONN_AG_CTX_RULE1EN_SHIFT      0
#define E5_USTORM_ISCSI_CONN_AG_CTX_RULE2EN_MASK       0x1 /* rule2en */
#define E5_USTORM_ISCSI_CONN_AG_CTX_RULE2EN_SHIFT      1
#define E5_USTORM_ISCSI_CONN_AG_CTX_RULE3EN_MASK       0x1 /* rule3en */
#define E5_USTORM_ISCSI_CONN_AG_CTX_RULE3EN_SHIFT      2
#define E5_USTORM_ISCSI_CONN_AG_CTX_RULE4EN_MASK       0x1 /* rule4en */
#define E5_USTORM_ISCSI_CONN_AG_CTX_RULE4EN_SHIFT      3
#define E5_USTORM_ISCSI_CONN_AG_CTX_RULE5EN_MASK       0x1 /* rule5en */
#define E5_USTORM_ISCSI_CONN_AG_CTX_RULE5EN_SHIFT      4
#define E5_USTORM_ISCSI_CONN_AG_CTX_RULE6EN_MASK       0x1 /* rule6en */
#define E5_USTORM_ISCSI_CONN_AG_CTX_RULE6EN_SHIFT      5
#define E5_USTORM_ISCSI_CONN_AG_CTX_RULE7EN_MASK       0x1 /* rule7en */
#define E5_USTORM_ISCSI_CONN_AG_CTX_RULE7EN_SHIFT      6
#define E5_USTORM_ISCSI_CONN_AG_CTX_RULE8EN_MASK       0x1 /* rule8en */
#define E5_USTORM_ISCSI_CONN_AG_CTX_RULE8EN_SHIFT      7
	u8 flags4;
#define E5_USTORM_ISCSI_CONN_AG_CTX_E4_RESERVED1_MASK  0x1 /* bit2 */
#define E5_USTORM_ISCSI_CONN_AG_CTX_E4_RESERVED1_SHIFT 0
#define E5_USTORM_ISCSI_CONN_AG_CTX_E4_RESERVED2_MASK  0x1 /* bit3 */
#define E5_USTORM_ISCSI_CONN_AG_CTX_E4_RESERVED2_SHIFT 1
#define E5_USTORM_ISCSI_CONN_AG_CTX_E4_RESERVED3_MASK  0x3 /* cf7 */
#define E5_USTORM_ISCSI_CONN_AG_CTX_E4_RESERVED3_SHIFT 2
#define E5_USTORM_ISCSI_CONN_AG_CTX_E4_RESERVED4_MASK  0x3 /* cf8 */
#define E5_USTORM_ISCSI_CONN_AG_CTX_E4_RESERVED4_SHIFT 4
#define E5_USTORM_ISCSI_CONN_AG_CTX_E4_RESERVED5_MASK  0x1 /* cf7en */
#define E5_USTORM_ISCSI_CONN_AG_CTX_E4_RESERVED5_SHIFT 6
#define E5_USTORM_ISCSI_CONN_AG_CTX_E4_RESERVED6_MASK  0x1 /* cf8en */
#define E5_USTORM_ISCSI_CONN_AG_CTX_E4_RESERVED6_SHIFT 7
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

struct e5_mstorm_iscsi_conn_ag_ctx
{
	u8 reserved /* cdu_validation */;
	u8 state_and_core_id /* state_and_core_id */;
	u8 flags0;
#define E5_MSTORM_ISCSI_CONN_AG_CTX_BIT0_MASK     0x1 /* exist_in_qm0 */
#define E5_MSTORM_ISCSI_CONN_AG_CTX_BIT0_SHIFT    0
#define E5_MSTORM_ISCSI_CONN_AG_CTX_BIT1_MASK     0x1 /* exist_in_qm1 */
#define E5_MSTORM_ISCSI_CONN_AG_CTX_BIT1_SHIFT    1
#define E5_MSTORM_ISCSI_CONN_AG_CTX_CF0_MASK      0x3 /* cf0 */
#define E5_MSTORM_ISCSI_CONN_AG_CTX_CF0_SHIFT     2
#define E5_MSTORM_ISCSI_CONN_AG_CTX_CF1_MASK      0x3 /* cf1 */
#define E5_MSTORM_ISCSI_CONN_AG_CTX_CF1_SHIFT     4
#define E5_MSTORM_ISCSI_CONN_AG_CTX_CF2_MASK      0x3 /* cf2 */
#define E5_MSTORM_ISCSI_CONN_AG_CTX_CF2_SHIFT     6
	u8 flags1;
#define E5_MSTORM_ISCSI_CONN_AG_CTX_CF0EN_MASK    0x1 /* cf0en */
#define E5_MSTORM_ISCSI_CONN_AG_CTX_CF0EN_SHIFT   0
#define E5_MSTORM_ISCSI_CONN_AG_CTX_CF1EN_MASK    0x1 /* cf1en */
#define E5_MSTORM_ISCSI_CONN_AG_CTX_CF1EN_SHIFT   1
#define E5_MSTORM_ISCSI_CONN_AG_CTX_CF2EN_MASK    0x1 /* cf2en */
#define E5_MSTORM_ISCSI_CONN_AG_CTX_CF2EN_SHIFT   2
#define E5_MSTORM_ISCSI_CONN_AG_CTX_RULE0EN_MASK  0x1 /* rule0en */
#define E5_MSTORM_ISCSI_CONN_AG_CTX_RULE0EN_SHIFT 3
#define E5_MSTORM_ISCSI_CONN_AG_CTX_RULE1EN_MASK  0x1 /* rule1en */
#define E5_MSTORM_ISCSI_CONN_AG_CTX_RULE1EN_SHIFT 4
#define E5_MSTORM_ISCSI_CONN_AG_CTX_RULE2EN_MASK  0x1 /* rule2en */
#define E5_MSTORM_ISCSI_CONN_AG_CTX_RULE2EN_SHIFT 5
#define E5_MSTORM_ISCSI_CONN_AG_CTX_RULE3EN_MASK  0x1 /* rule3en */
#define E5_MSTORM_ISCSI_CONN_AG_CTX_RULE3EN_SHIFT 6
#define E5_MSTORM_ISCSI_CONN_AG_CTX_RULE4EN_MASK  0x1 /* rule4en */
#define E5_MSTORM_ISCSI_CONN_AG_CTX_RULE4EN_SHIFT 7
	__le16 word0 /* word0 */;
	__le16 word1 /* word1 */;
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
};

/*
 * iscsi connection context
 */
struct e5_iscsi_conn_context
{
	struct ystorm_iscsi_conn_st_ctx ystorm_st_context /* ystorm storm context */;
	struct regpair ystorm_st_padding[2] /* padding */;
	struct pstorm_iscsi_tcp_conn_st_ctx pstorm_st_context /* pstorm storm context */;
	struct regpair pstorm_st_padding[2] /* padding */;
	struct pb_context xpb2_context /* xpb2 context */;
	struct xstorm_iscsi_tcp_conn_st_ctx xstorm_st_context /* xstorm storm context */;
	struct regpair xstorm_st_padding[2] /* padding */;
	struct e5_xstorm_iscsi_conn_ag_ctx xstorm_ag_context /* xstorm aggregative context */;
	struct e5_tstorm_iscsi_conn_ag_ctx tstorm_ag_context /* tstorm aggregative context */;
	struct regpair tstorm_ag_padding[2] /* padding */;
	struct timers_context timer_context /* timer context */;
	struct e5_ustorm_iscsi_conn_ag_ctx ustorm_ag_context /* ustorm aggregative context */;
	struct pb_context upb_context /* upb context */;
	struct tstorm_iscsi_conn_st_ctx tstorm_st_context /* tstorm storm context */;
	struct regpair tstorm_st_padding[2] /* padding */;
	struct e5_mstorm_iscsi_conn_ag_ctx mstorm_ag_context /* mstorm aggregative context */;
	struct mstorm_iscsi_tcp_conn_st_ctx mstorm_st_context /* mstorm storm context */;
	struct ustorm_iscsi_conn_st_ctx ustorm_st_context /* ustorm storm context */;
};


/*
 * iSCSI init params passed by driver to FW in iSCSI init ramrod 
 */
struct iscsi_init_ramrod_params
{
	struct iscsi_spe_func_init iscsi_init_spe /* parameters initialized by the miniport and handed to bus-driver */;
	struct tcp_init_params tcp_init /* TCP parameters initialized by the bus-driver */;
};












struct e4_ystorm_iscsi_conn_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 byte1 /* state */;
	u8 flags0;
#define E4_YSTORM_ISCSI_CONN_AG_CTX_BIT0_MASK     0x1 /* exist_in_qm0 */
#define E4_YSTORM_ISCSI_CONN_AG_CTX_BIT0_SHIFT    0
#define E4_YSTORM_ISCSI_CONN_AG_CTX_BIT1_MASK     0x1 /* exist_in_qm1 */
#define E4_YSTORM_ISCSI_CONN_AG_CTX_BIT1_SHIFT    1
#define E4_YSTORM_ISCSI_CONN_AG_CTX_CF0_MASK      0x3 /* cf0 */
#define E4_YSTORM_ISCSI_CONN_AG_CTX_CF0_SHIFT     2
#define E4_YSTORM_ISCSI_CONN_AG_CTX_CF1_MASK      0x3 /* cf1 */
#define E4_YSTORM_ISCSI_CONN_AG_CTX_CF1_SHIFT     4
#define E4_YSTORM_ISCSI_CONN_AG_CTX_CF2_MASK      0x3 /* cf2 */
#define E4_YSTORM_ISCSI_CONN_AG_CTX_CF2_SHIFT     6
	u8 flags1;
#define E4_YSTORM_ISCSI_CONN_AG_CTX_CF0EN_MASK    0x1 /* cf0en */
#define E4_YSTORM_ISCSI_CONN_AG_CTX_CF0EN_SHIFT   0
#define E4_YSTORM_ISCSI_CONN_AG_CTX_CF1EN_MASK    0x1 /* cf1en */
#define E4_YSTORM_ISCSI_CONN_AG_CTX_CF1EN_SHIFT   1
#define E4_YSTORM_ISCSI_CONN_AG_CTX_CF2EN_MASK    0x1 /* cf2en */
#define E4_YSTORM_ISCSI_CONN_AG_CTX_CF2EN_SHIFT   2
#define E4_YSTORM_ISCSI_CONN_AG_CTX_RULE0EN_MASK  0x1 /* rule0en */
#define E4_YSTORM_ISCSI_CONN_AG_CTX_RULE0EN_SHIFT 3
#define E4_YSTORM_ISCSI_CONN_AG_CTX_RULE1EN_MASK  0x1 /* rule1en */
#define E4_YSTORM_ISCSI_CONN_AG_CTX_RULE1EN_SHIFT 4
#define E4_YSTORM_ISCSI_CONN_AG_CTX_RULE2EN_MASK  0x1 /* rule2en */
#define E4_YSTORM_ISCSI_CONN_AG_CTX_RULE2EN_SHIFT 5
#define E4_YSTORM_ISCSI_CONN_AG_CTX_RULE3EN_MASK  0x1 /* rule3en */
#define E4_YSTORM_ISCSI_CONN_AG_CTX_RULE3EN_SHIFT 6
#define E4_YSTORM_ISCSI_CONN_AG_CTX_RULE4EN_MASK  0x1 /* rule4en */
#define E4_YSTORM_ISCSI_CONN_AG_CTX_RULE4EN_SHIFT 7
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






struct e5_ystorm_iscsi_conn_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 byte1 /* state_and_core_id */;
	u8 flags0;
#define E5_YSTORM_ISCSI_CONN_AG_CTX_BIT0_MASK     0x1 /* exist_in_qm0 */
#define E5_YSTORM_ISCSI_CONN_AG_CTX_BIT0_SHIFT    0
#define E5_YSTORM_ISCSI_CONN_AG_CTX_BIT1_MASK     0x1 /* exist_in_qm1 */
#define E5_YSTORM_ISCSI_CONN_AG_CTX_BIT1_SHIFT    1
#define E5_YSTORM_ISCSI_CONN_AG_CTX_CF0_MASK      0x3 /* cf0 */
#define E5_YSTORM_ISCSI_CONN_AG_CTX_CF0_SHIFT     2
#define E5_YSTORM_ISCSI_CONN_AG_CTX_CF1_MASK      0x3 /* cf1 */
#define E5_YSTORM_ISCSI_CONN_AG_CTX_CF1_SHIFT     4
#define E5_YSTORM_ISCSI_CONN_AG_CTX_CF2_MASK      0x3 /* cf2 */
#define E5_YSTORM_ISCSI_CONN_AG_CTX_CF2_SHIFT     6
	u8 flags1;
#define E5_YSTORM_ISCSI_CONN_AG_CTX_CF0EN_MASK    0x1 /* cf0en */
#define E5_YSTORM_ISCSI_CONN_AG_CTX_CF0EN_SHIFT   0
#define E5_YSTORM_ISCSI_CONN_AG_CTX_CF1EN_MASK    0x1 /* cf1en */
#define E5_YSTORM_ISCSI_CONN_AG_CTX_CF1EN_SHIFT   1
#define E5_YSTORM_ISCSI_CONN_AG_CTX_CF2EN_MASK    0x1 /* cf2en */
#define E5_YSTORM_ISCSI_CONN_AG_CTX_CF2EN_SHIFT   2
#define E5_YSTORM_ISCSI_CONN_AG_CTX_RULE0EN_MASK  0x1 /* rule0en */
#define E5_YSTORM_ISCSI_CONN_AG_CTX_RULE0EN_SHIFT 3
#define E5_YSTORM_ISCSI_CONN_AG_CTX_RULE1EN_MASK  0x1 /* rule1en */
#define E5_YSTORM_ISCSI_CONN_AG_CTX_RULE1EN_SHIFT 4
#define E5_YSTORM_ISCSI_CONN_AG_CTX_RULE2EN_MASK  0x1 /* rule2en */
#define E5_YSTORM_ISCSI_CONN_AG_CTX_RULE2EN_SHIFT 5
#define E5_YSTORM_ISCSI_CONN_AG_CTX_RULE3EN_MASK  0x1 /* rule3en */
#define E5_YSTORM_ISCSI_CONN_AG_CTX_RULE3EN_SHIFT 6
#define E5_YSTORM_ISCSI_CONN_AG_CTX_RULE4EN_MASK  0x1 /* rule4en */
#define E5_YSTORM_ISCSI_CONN_AG_CTX_RULE4EN_SHIFT 7
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

#endif /* __ECORE_HSI_ISCSI__ */
