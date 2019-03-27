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

#ifndef __ECORE_HSI_IWARP__
#define __ECORE_HSI_IWARP__ 
/************************************************************************/
/* Add include to ecore hsi rdma target for both roce and iwarp ecore driver */
/************************************************************************/
#include "ecore_hsi_rdma.h"
/************************************************************************/
/* Add include to common TCP target */
/************************************************************************/
#include "tcp_common.h"

/************************************************************************/
/* Add include to common iwarp target for both eCore and protocol iwarp driver */
/************************************************************************/
#include "iwarp_common.h"

/*
 * The iwarp storm context of Ystorm
 */
struct ystorm_iwarp_conn_st_ctx
{
	__le32 reserved[4];
};

/*
 * The iwarp storm context of Pstorm
 */
struct pstorm_iwarp_conn_st_ctx
{
	__le32 reserved[36];
};

/*
 * The iwarp storm context of Xstorm
 */
struct xstorm_iwarp_conn_st_ctx
{
	__le32 reserved[48];
};

struct e4_xstorm_iwarp_conn_ag_ctx
{
	u8 reserved0 /* cdu_validation */;
	u8 state /* state */;
	u8 flags0;
#define E4_XSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM0_MASK                       0x1 /* exist_in_qm0 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM0_SHIFT                      0
#define E4_XSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM1_MASK                       0x1 /* exist_in_qm1 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM1_SHIFT                      1
#define E4_XSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM2_MASK                       0x1 /* exist_in_qm2 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM2_SHIFT                      2
#define E4_XSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM3_MASK                       0x1 /* exist_in_qm3 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM3_SHIFT                      3
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT4_MASK                               0x1 /* bit4 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT4_SHIFT                              4
#define E4_XSTORM_IWARP_CONN_AG_CTX_RESERVED2_MASK                          0x1 /* cf_array_active */
#define E4_XSTORM_IWARP_CONN_AG_CTX_RESERVED2_SHIFT                         5
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT6_MASK                               0x1 /* bit6 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT6_SHIFT                              6
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT7_MASK                               0x1 /* bit7 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT7_SHIFT                              7
	u8 flags1;
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT8_MASK                               0x1 /* bit8 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT8_SHIFT                              0
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT9_MASK                               0x1 /* bit9 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT9_SHIFT                              1
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT10_MASK                              0x1 /* bit10 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT10_SHIFT                             2
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT11_MASK                              0x1 /* bit11 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT11_SHIFT                             3
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT12_MASK                              0x1 /* bit12 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT12_SHIFT                             4
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT13_MASK                              0x1 /* bit13 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT13_SHIFT                             5
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT14_MASK                              0x1 /* bit14 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT14_SHIFT                             6
#define E4_XSTORM_IWARP_CONN_AG_CTX_YSTORM_FLUSH_OR_REWIND_SND_MAX_MASK     0x1 /* bit15 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_YSTORM_FLUSH_OR_REWIND_SND_MAX_SHIFT    7
	u8 flags2;
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF0_MASK                                0x3 /* timer0cf */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF0_SHIFT                               0
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF1_MASK                                0x3 /* timer1cf */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF1_SHIFT                               2
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF2_MASK                                0x3 /* timer2cf */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF2_SHIFT                               4
#define E4_XSTORM_IWARP_CONN_AG_CTX_TIMER_STOP_ALL_MASK                     0x3 /* timer_stop_all */
#define E4_XSTORM_IWARP_CONN_AG_CTX_TIMER_STOP_ALL_SHIFT                    6
	u8 flags3;
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF4_MASK                                0x3 /* cf4 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF4_SHIFT                               0
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF5_MASK                                0x3 /* cf5 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF5_SHIFT                               2
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF6_MASK                                0x3 /* cf6 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF6_SHIFT                               4
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF7_MASK                                0x3 /* cf7 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF7_SHIFT                               6
	u8 flags4;
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF8_MASK                                0x3 /* cf8 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF8_SHIFT                               0
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF9_MASK                                0x3 /* cf9 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF9_SHIFT                               2
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF10_MASK                               0x3 /* cf10 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF10_SHIFT                              4
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF11_MASK                               0x3 /* cf11 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF11_SHIFT                              6
	u8 flags5;
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF12_MASK                               0x3 /* cf12 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF12_SHIFT                              0
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF13_MASK                               0x3 /* cf13 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF13_SHIFT                              2
#define E4_XSTORM_IWARP_CONN_AG_CTX_SQ_FLUSH_CF_MASK                        0x3 /* cf14 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_SQ_FLUSH_CF_SHIFT                       4
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF15_MASK                               0x3 /* cf15 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF15_SHIFT                              6
	u8 flags6;
#define E4_XSTORM_IWARP_CONN_AG_CTX_MPA_OR_ERROR_WAKEUP_TRIGGER_CF_MASK     0x3 /* cf16 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_MPA_OR_ERROR_WAKEUP_TRIGGER_CF_SHIFT    0
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF17_MASK                               0x3 /* cf_array_cf */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF17_SHIFT                              2
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF18_MASK                               0x3 /* cf18 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF18_SHIFT                              4
#define E4_XSTORM_IWARP_CONN_AG_CTX_DQ_FLUSH_MASK                           0x3 /* cf19 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_DQ_FLUSH_SHIFT                          6
	u8 flags7;
#define E4_XSTORM_IWARP_CONN_AG_CTX_FLUSH_Q0_MASK                           0x3 /* cf20 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_FLUSH_Q0_SHIFT                          0
#define E4_XSTORM_IWARP_CONN_AG_CTX_FLUSH_Q1_MASK                           0x3 /* cf21 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_FLUSH_Q1_SHIFT                          2
#define E4_XSTORM_IWARP_CONN_AG_CTX_SLOW_PATH_MASK                          0x3 /* cf22 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_SLOW_PATH_SHIFT                         4
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF0EN_MASK                              0x1 /* cf0en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF0EN_SHIFT                             6
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF1EN_MASK                              0x1 /* cf1en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF1EN_SHIFT                             7
	u8 flags8;
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF2EN_MASK                              0x1 /* cf2en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF2EN_SHIFT                             0
#define E4_XSTORM_IWARP_CONN_AG_CTX_TIMER_STOP_ALL_EN_MASK                  0x1 /* cf3en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_TIMER_STOP_ALL_EN_SHIFT                 1
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF4EN_MASK                              0x1 /* cf4en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF4EN_SHIFT                             2
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF5EN_MASK                              0x1 /* cf5en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF5EN_SHIFT                             3
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF6EN_MASK                              0x1 /* cf6en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF6EN_SHIFT                             4
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF7EN_MASK                              0x1 /* cf7en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF7EN_SHIFT                             5
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF8EN_MASK                              0x1 /* cf8en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF8EN_SHIFT                             6
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF9EN_MASK                              0x1 /* cf9en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF9EN_SHIFT                             7
	u8 flags9;
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF10EN_MASK                             0x1 /* cf10en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF10EN_SHIFT                            0
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF11EN_MASK                             0x1 /* cf11en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF11EN_SHIFT                            1
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF12EN_MASK                             0x1 /* cf12en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF12EN_SHIFT                            2
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF13EN_MASK                             0x1 /* cf13en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF13EN_SHIFT                            3
#define E4_XSTORM_IWARP_CONN_AG_CTX_SQ_FLUSH_CF_EN_MASK                     0x1 /* cf14en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_SQ_FLUSH_CF_EN_SHIFT                    4
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF15EN_MASK                             0x1 /* cf15en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF15EN_SHIFT                            5
#define E4_XSTORM_IWARP_CONN_AG_CTX_MPA_OR_ERROR_WAKEUP_TRIGGER_CF_EN_MASK  0x1 /* cf16en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_MPA_OR_ERROR_WAKEUP_TRIGGER_CF_EN_SHIFT 6
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF17EN_MASK                             0x1 /* cf_array_cf_en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF17EN_SHIFT                            7
	u8 flags10;
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF18EN_MASK                             0x1 /* cf18en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF18EN_SHIFT                            0
#define E4_XSTORM_IWARP_CONN_AG_CTX_DQ_FLUSH_EN_MASK                        0x1 /* cf19en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_DQ_FLUSH_EN_SHIFT                       1
#define E4_XSTORM_IWARP_CONN_AG_CTX_FLUSH_Q0_EN_MASK                        0x1 /* cf20en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_FLUSH_Q0_EN_SHIFT                       2
#define E4_XSTORM_IWARP_CONN_AG_CTX_FLUSH_Q1_EN_MASK                        0x1 /* cf21en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_FLUSH_Q1_EN_SHIFT                       3
#define E4_XSTORM_IWARP_CONN_AG_CTX_SLOW_PATH_EN_MASK                       0x1 /* cf22en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_SLOW_PATH_EN_SHIFT                      4
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF23EN_MASK                             0x1 /* cf23en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF23EN_SHIFT                            5
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE0EN_MASK                            0x1 /* rule0en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE0EN_SHIFT                           6
#define E4_XSTORM_IWARP_CONN_AG_CTX_MORE_TO_SEND_RULE_EN_MASK               0x1 /* rule1en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_MORE_TO_SEND_RULE_EN_SHIFT              7
	u8 flags11;
#define E4_XSTORM_IWARP_CONN_AG_CTX_TX_BLOCKED_EN_MASK                      0x1 /* rule2en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_TX_BLOCKED_EN_SHIFT                     0
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE3EN_MASK                            0x1 /* rule3en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE3EN_SHIFT                           1
#define E4_XSTORM_IWARP_CONN_AG_CTX_RESERVED3_MASK                          0x1 /* rule4en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_RESERVED3_SHIFT                         2
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE5EN_MASK                            0x1 /* rule5en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE5EN_SHIFT                           3
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE6EN_MASK                            0x1 /* rule6en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE6EN_SHIFT                           4
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE7EN_MASK                            0x1 /* rule7en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE7EN_SHIFT                           5
#define E4_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED1_MASK                       0x1 /* rule8en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED1_SHIFT                      6
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE9EN_MASK                            0x1 /* rule9en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE9EN_SHIFT                           7
	u8 flags12;
#define E4_XSTORM_IWARP_CONN_AG_CTX_SQ_NOT_EMPTY_RULE_EN_MASK               0x1 /* rule10en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_SQ_NOT_EMPTY_RULE_EN_SHIFT              0
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE11EN_MASK                           0x1 /* rule11en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE11EN_SHIFT                          1
#define E4_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED2_MASK                       0x1 /* rule12en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED2_SHIFT                      2
#define E4_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED3_MASK                       0x1 /* rule13en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED3_SHIFT                      3
#define E4_XSTORM_IWARP_CONN_AG_CTX_SQ_FENCE_RULE_EN_MASK                   0x1 /* rule14en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_SQ_FENCE_RULE_EN_SHIFT                  4
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE15EN_MASK                           0x1 /* rule15en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE15EN_SHIFT                          5
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE16EN_MASK                           0x1 /* rule16en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE16EN_SHIFT                          6
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE17EN_MASK                           0x1 /* rule17en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE17EN_SHIFT                          7
	u8 flags13;
#define E4_XSTORM_IWARP_CONN_AG_CTX_IRQ_NOT_EMPTY_RULE_EN_MASK              0x1 /* rule18en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_IRQ_NOT_EMPTY_RULE_EN_SHIFT             0
#define E4_XSTORM_IWARP_CONN_AG_CTX_HQ_NOT_FULL_RULE_EN_MASK                0x1 /* rule19en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_HQ_NOT_FULL_RULE_EN_SHIFT               1
#define E4_XSTORM_IWARP_CONN_AG_CTX_ORQ_RD_FENCE_RULE_EN_MASK               0x1 /* rule20en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_ORQ_RD_FENCE_RULE_EN_SHIFT              2
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE21EN_MASK                           0x1 /* rule21en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_RULE21EN_SHIFT                          3
#define E4_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED6_MASK                       0x1 /* rule22en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED6_SHIFT                      4
#define E4_XSTORM_IWARP_CONN_AG_CTX_ORQ_NOT_FULL_RULE_EN_MASK               0x1 /* rule23en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_ORQ_NOT_FULL_RULE_EN_SHIFT              5
#define E4_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED8_MASK                       0x1 /* rule24en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED8_SHIFT                      6
#define E4_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED9_MASK                       0x1 /* rule25en */
#define E4_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED9_SHIFT                      7
	u8 flags14;
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT16_MASK                              0x1 /* bit16 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT16_SHIFT                             0
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT17_MASK                              0x1 /* bit17 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT17_SHIFT                             1
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT18_MASK                              0x1 /* bit18 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_BIT18_SHIFT                             2
#define E4_XSTORM_IWARP_CONN_AG_CTX_E5_RESERVED1_MASK                       0x1 /* bit19 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_E5_RESERVED1_SHIFT                      3
#define E4_XSTORM_IWARP_CONN_AG_CTX_E5_RESERVED2_MASK                       0x1 /* bit20 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_E5_RESERVED2_SHIFT                      4
#define E4_XSTORM_IWARP_CONN_AG_CTX_E5_RESERVED3_MASK                       0x1 /* bit21 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_E5_RESERVED3_SHIFT                      5
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF23_MASK                               0x3 /* cf23 */
#define E4_XSTORM_IWARP_CONN_AG_CTX_CF23_SHIFT                              6
	u8 byte2 /* byte2 */;
	__le16 physical_q0 /* physical_q0 */;
	__le16 physical_q1 /* physical_q1 */;
	__le16 sq_comp_cons /* physical_q2 */;
	__le16 sq_tx_cons /* word3 */;
	__le16 sq_prod /* word4 */;
	__le16 word5 /* word5 */;
	__le16 conn_dpi /* conn_dpi */;
	u8 byte3 /* byte3 */;
	u8 byte4 /* byte4 */;
	u8 byte5 /* byte5 */;
	u8 byte6 /* byte6 */;
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
	__le32 reg2 /* reg2 */;
	__le32 more_to_send_seq /* reg3 */;
	__le32 reg4 /* reg4 */;
	__le32 rewinded_snd_max /* cf_array0 */;
	__le32 rd_msn /* cf_array1 */;
	__le16 irq_prod_via_msdm /* word7 */;
	__le16 irq_cons /* word8 */;
	__le16 hq_cons_th_or_mpa_data /* word9 */;
	__le16 hq_cons /* word10 */;
	__le32 atom_msn /* reg7 */;
	__le32 orq_cons /* reg8 */;
	__le32 orq_cons_th /* reg9 */;
	u8 byte7 /* byte7 */;
	u8 max_ord /* byte8 */;
	u8 wqe_data_pad_bytes /* byte9 */;
	u8 former_hq_prod /* byte10 */;
	u8 irq_prod_via_msem /* byte11 */;
	u8 byte12 /* byte12 */;
	u8 max_pkt_pdu_size_lo /* byte13 */;
	u8 max_pkt_pdu_size_hi /* byte14 */;
	u8 byte15 /* byte15 */;
	u8 e5_reserved /* e5_reserved */;
	__le16 e5_reserved4 /* word11 */;
	__le32 reg10 /* reg10 */;
	__le32 reg11 /* reg11 */;
	__le32 shared_queue_page_addr_lo /* reg12 */;
	__le32 shared_queue_page_addr_hi /* reg13 */;
	__le32 reg14 /* reg14 */;
	__le32 reg15 /* reg15 */;
	__le32 reg16 /* reg16 */;
	__le32 reg17 /* reg17 */;
};

struct e4_tstorm_iwarp_conn_ag_ctx
{
	u8 reserved0 /* cdu_validation */;
	u8 state /* state */;
	u8 flags0;
#define E4_TSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM0_MASK                            0x1 /* exist_in_qm0 */
#define E4_TSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM0_SHIFT                           0
#define E4_TSTORM_IWARP_CONN_AG_CTX_BIT1_MASK                                    0x1 /* exist_in_qm1 */
#define E4_TSTORM_IWARP_CONN_AG_CTX_BIT1_SHIFT                                   1
#define E4_TSTORM_IWARP_CONN_AG_CTX_BIT2_MASK                                    0x1 /* bit2 */
#define E4_TSTORM_IWARP_CONN_AG_CTX_BIT2_SHIFT                                   2
#define E4_TSTORM_IWARP_CONN_AG_CTX_MSTORM_FLUSH_MASK                            0x1 /* bit3 */
#define E4_TSTORM_IWARP_CONN_AG_CTX_MSTORM_FLUSH_SHIFT                           3
#define E4_TSTORM_IWARP_CONN_AG_CTX_BIT4_MASK                                    0x1 /* bit4 */
#define E4_TSTORM_IWARP_CONN_AG_CTX_BIT4_SHIFT                                   4
#define E4_TSTORM_IWARP_CONN_AG_CTX_CACHED_ORQ_MASK                              0x1 /* bit5 */
#define E4_TSTORM_IWARP_CONN_AG_CTX_CACHED_ORQ_SHIFT                             5
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF0_MASK                                     0x3 /* timer0cf */
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF0_SHIFT                                    6
	u8 flags1;
#define E4_TSTORM_IWARP_CONN_AG_CTX_RQ_POST_CF_MASK                              0x3 /* timer1cf */
#define E4_TSTORM_IWARP_CONN_AG_CTX_RQ_POST_CF_SHIFT                             0
#define E4_TSTORM_IWARP_CONN_AG_CTX_MPA_TIMEOUT_CF_MASK                          0x3 /* timer2cf */
#define E4_TSTORM_IWARP_CONN_AG_CTX_MPA_TIMEOUT_CF_SHIFT                         2
#define E4_TSTORM_IWARP_CONN_AG_CTX_TIMER_STOP_ALL_MASK                          0x3 /* timer_stop_all */
#define E4_TSTORM_IWARP_CONN_AG_CTX_TIMER_STOP_ALL_SHIFT                         4
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF4_MASK                                     0x3 /* cf4 */
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF4_SHIFT                                    6
	u8 flags2;
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF5_MASK                                     0x3 /* cf5 */
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF5_SHIFT                                    0
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF6_MASK                                     0x3 /* cf6 */
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF6_SHIFT                                    2
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF7_MASK                                     0x3 /* cf7 */
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF7_SHIFT                                    4
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF8_MASK                                     0x3 /* cf8 */
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF8_SHIFT                                    6
	u8 flags3;
#define E4_TSTORM_IWARP_CONN_AG_CTX_FLUSH_Q0_AND_TCP_HANDSHAKE_COMPLETE_MASK     0x3 /* cf9 */
#define E4_TSTORM_IWARP_CONN_AG_CTX_FLUSH_Q0_AND_TCP_HANDSHAKE_COMPLETE_SHIFT    0
#define E4_TSTORM_IWARP_CONN_AG_CTX_FLUSH_OR_ERROR_DETECTED_MASK                 0x3 /* cf10 */
#define E4_TSTORM_IWARP_CONN_AG_CTX_FLUSH_OR_ERROR_DETECTED_SHIFT                2
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF0EN_MASK                                   0x1 /* cf0en */
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF0EN_SHIFT                                  4
#define E4_TSTORM_IWARP_CONN_AG_CTX_RQ_POST_CF_EN_MASK                           0x1 /* cf1en */
#define E4_TSTORM_IWARP_CONN_AG_CTX_RQ_POST_CF_EN_SHIFT                          5
#define E4_TSTORM_IWARP_CONN_AG_CTX_MPA_TIMEOUT_CF_EN_MASK                       0x1 /* cf2en */
#define E4_TSTORM_IWARP_CONN_AG_CTX_MPA_TIMEOUT_CF_EN_SHIFT                      6
#define E4_TSTORM_IWARP_CONN_AG_CTX_TIMER_STOP_ALL_EN_MASK                       0x1 /* cf3en */
#define E4_TSTORM_IWARP_CONN_AG_CTX_TIMER_STOP_ALL_EN_SHIFT                      7
	u8 flags4;
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF4EN_MASK                                   0x1 /* cf4en */
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF4EN_SHIFT                                  0
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF5EN_MASK                                   0x1 /* cf5en */
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF5EN_SHIFT                                  1
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF6EN_MASK                                   0x1 /* cf6en */
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF6EN_SHIFT                                  2
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF7EN_MASK                                   0x1 /* cf7en */
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF7EN_SHIFT                                  3
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF8EN_MASK                                   0x1 /* cf8en */
#define E4_TSTORM_IWARP_CONN_AG_CTX_CF8EN_SHIFT                                  4
#define E4_TSTORM_IWARP_CONN_AG_CTX_FLUSH_Q0_AND_TCP_HANDSHAKE_COMPLETE_EN_MASK  0x1 /* cf9en */
#define E4_TSTORM_IWARP_CONN_AG_CTX_FLUSH_Q0_AND_TCP_HANDSHAKE_COMPLETE_EN_SHIFT 5
#define E4_TSTORM_IWARP_CONN_AG_CTX_FLUSH_OR_ERROR_DETECTED_EN_MASK              0x1 /* cf10en */
#define E4_TSTORM_IWARP_CONN_AG_CTX_FLUSH_OR_ERROR_DETECTED_EN_SHIFT             6
#define E4_TSTORM_IWARP_CONN_AG_CTX_RULE0EN_MASK                                 0x1 /* rule0en */
#define E4_TSTORM_IWARP_CONN_AG_CTX_RULE0EN_SHIFT                                7
	u8 flags5;
#define E4_TSTORM_IWARP_CONN_AG_CTX_RULE1EN_MASK                                 0x1 /* rule1en */
#define E4_TSTORM_IWARP_CONN_AG_CTX_RULE1EN_SHIFT                                0
#define E4_TSTORM_IWARP_CONN_AG_CTX_RULE2EN_MASK                                 0x1 /* rule2en */
#define E4_TSTORM_IWARP_CONN_AG_CTX_RULE2EN_SHIFT                                1
#define E4_TSTORM_IWARP_CONN_AG_CTX_RULE3EN_MASK                                 0x1 /* rule3en */
#define E4_TSTORM_IWARP_CONN_AG_CTX_RULE3EN_SHIFT                                2
#define E4_TSTORM_IWARP_CONN_AG_CTX_RULE4EN_MASK                                 0x1 /* rule4en */
#define E4_TSTORM_IWARP_CONN_AG_CTX_RULE4EN_SHIFT                                3
#define E4_TSTORM_IWARP_CONN_AG_CTX_RULE5EN_MASK                                 0x1 /* rule5en */
#define E4_TSTORM_IWARP_CONN_AG_CTX_RULE5EN_SHIFT                                4
#define E4_TSTORM_IWARP_CONN_AG_CTX_SND_SQ_CONS_RULE_MASK                        0x1 /* rule6en */
#define E4_TSTORM_IWARP_CONN_AG_CTX_SND_SQ_CONS_RULE_SHIFT                       5
#define E4_TSTORM_IWARP_CONN_AG_CTX_RULE7EN_MASK                                 0x1 /* rule7en */
#define E4_TSTORM_IWARP_CONN_AG_CTX_RULE7EN_SHIFT                                6
#define E4_TSTORM_IWARP_CONN_AG_CTX_RULE8EN_MASK                                 0x1 /* rule8en */
#define E4_TSTORM_IWARP_CONN_AG_CTX_RULE8EN_SHIFT                                7
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
	__le32 unaligned_nxt_seq /* reg2 */;
	__le32 reg3 /* reg3 */;
	__le32 reg4 /* reg4 */;
	__le32 reg5 /* reg5 */;
	__le32 reg6 /* reg6 */;
	__le32 reg7 /* reg7 */;
	__le32 reg8 /* reg8 */;
	u8 orq_cache_idx /* byte2 */;
	u8 hq_prod /* byte3 */;
	__le16 sq_tx_cons_th /* word0 */;
	u8 orq_prod /* byte4 */;
	u8 irq_cons /* byte5 */;
	__le16 sq_tx_cons /* word1 */;
	__le16 conn_dpi /* conn_dpi */;
	__le16 rq_prod /* word3 */;
	__le32 snd_seq /* reg9 */;
	__le32 last_hq_sequence /* reg10 */;
};

/*
 * The iwarp storm context of Tstorm
 */
struct tstorm_iwarp_conn_st_ctx
{
	__le32 reserved[60];
};

/*
 * The iwarp storm context of Mstorm
 */
struct mstorm_iwarp_conn_st_ctx
{
	__le32 reserved[32];
};

/*
 * The iwarp storm context of Ustorm
 */
struct ustorm_iwarp_conn_st_ctx
{
	__le32 reserved[24];
};

/*
 * iwarp connection context
 */
struct e4_iwarp_conn_context
{
	struct ystorm_iwarp_conn_st_ctx ystorm_st_context /* ystorm storm context */;
	struct regpair ystorm_st_padding[2] /* padding */;
	struct pstorm_iwarp_conn_st_ctx pstorm_st_context /* pstorm storm context */;
	struct regpair pstorm_st_padding[2] /* padding */;
	struct xstorm_iwarp_conn_st_ctx xstorm_st_context /* xstorm storm context */;
	struct regpair xstorm_st_padding[2] /* padding */;
	struct e4_xstorm_iwarp_conn_ag_ctx xstorm_ag_context /* xstorm aggregative context */;
	struct e4_tstorm_iwarp_conn_ag_ctx tstorm_ag_context /* tstorm aggregative context */;
	struct timers_context timer_context /* timer context */;
	struct e4_ustorm_rdma_conn_ag_ctx ustorm_ag_context /* ustorm aggregative context */;
	struct tstorm_iwarp_conn_st_ctx tstorm_st_context /* tstorm storm context */;
	struct regpair tstorm_st_padding[2] /* padding */;
	struct mstorm_iwarp_conn_st_ctx mstorm_st_context /* mstorm storm context */;
	struct ustorm_iwarp_conn_st_ctx ustorm_st_context /* ustorm storm context */;
};


struct e5_xstorm_iwarp_conn_ag_ctx
{
	u8 reserved0 /* cdu_validation */;
	u8 state_and_core_id /* state_and_core_id */;
	u8 flags0;
#define E5_XSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM0_MASK                       0x1 /* exist_in_qm0 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM0_SHIFT                      0
#define E5_XSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM1_MASK                       0x1 /* exist_in_qm1 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM1_SHIFT                      1
#define E5_XSTORM_IWARP_CONN_AG_CTX_RESERVED1_MASK                          0x1 /* exist_in_qm2 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_RESERVED1_SHIFT                         2
#define E5_XSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM3_MASK                       0x1 /* exist_in_qm3 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM3_SHIFT                      3
#define E5_XSTORM_IWARP_CONN_AG_CTX_BIT4_MASK                               0x1 /* bit4 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_BIT4_SHIFT                              4
#define E5_XSTORM_IWARP_CONN_AG_CTX_RESERVED2_MASK                          0x1 /* cf_array_active */
#define E5_XSTORM_IWARP_CONN_AG_CTX_RESERVED2_SHIFT                         5
#define E5_XSTORM_IWARP_CONN_AG_CTX_BIT6_MASK                               0x1 /* bit6 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_BIT6_SHIFT                              6
#define E5_XSTORM_IWARP_CONN_AG_CTX_BIT7_MASK                               0x1 /* bit7 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_BIT7_SHIFT                              7
	u8 flags1;
#define E5_XSTORM_IWARP_CONN_AG_CTX_BIT8_MASK                               0x1 /* bit8 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_BIT8_SHIFT                              0
#define E5_XSTORM_IWARP_CONN_AG_CTX_BIT9_MASK                               0x1 /* bit9 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_BIT9_SHIFT                              1
#define E5_XSTORM_IWARP_CONN_AG_CTX_BIT10_MASK                              0x1 /* bit10 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_BIT10_SHIFT                             2
#define E5_XSTORM_IWARP_CONN_AG_CTX_BIT11_MASK                              0x1 /* bit11 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_BIT11_SHIFT                             3
#define E5_XSTORM_IWARP_CONN_AG_CTX_BIT12_MASK                              0x1 /* bit12 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_BIT12_SHIFT                             4
#define E5_XSTORM_IWARP_CONN_AG_CTX_BIT13_MASK                              0x1 /* bit13 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_BIT13_SHIFT                             5
#define E5_XSTORM_IWARP_CONN_AG_CTX_BIT14_MASK                              0x1 /* bit14 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_BIT14_SHIFT                             6
#define E5_XSTORM_IWARP_CONN_AG_CTX_YSTORM_FLUSH_OR_REWIND_SND_MAX_MASK     0x1 /* bit15 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_YSTORM_FLUSH_OR_REWIND_SND_MAX_SHIFT    7
	u8 flags2;
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF0_MASK                                0x3 /* timer0cf */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF0_SHIFT                               0
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF1_MASK                                0x3 /* timer1cf */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF1_SHIFT                               2
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF2_MASK                                0x3 /* timer2cf */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF2_SHIFT                               4
#define E5_XSTORM_IWARP_CONN_AG_CTX_TIMER_STOP_ALL_MASK                     0x3 /* timer_stop_all */
#define E5_XSTORM_IWARP_CONN_AG_CTX_TIMER_STOP_ALL_SHIFT                    6
	u8 flags3;
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF4_MASK                                0x3 /* cf4 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF4_SHIFT                               0
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF5_MASK                                0x3 /* cf5 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF5_SHIFT                               2
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF6_MASK                                0x3 /* cf6 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF6_SHIFT                               4
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF7_MASK                                0x3 /* cf7 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF7_SHIFT                               6
	u8 flags4;
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF8_MASK                                0x3 /* cf8 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF8_SHIFT                               0
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF9_MASK                                0x3 /* cf9 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF9_SHIFT                               2
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF10_MASK                               0x3 /* cf10 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF10_SHIFT                              4
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF11_MASK                               0x3 /* cf11 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF11_SHIFT                              6
	u8 flags5;
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF12_MASK                               0x3 /* cf12 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF12_SHIFT                              0
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF13_MASK                               0x3 /* cf13 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF13_SHIFT                              2
#define E5_XSTORM_IWARP_CONN_AG_CTX_SQ_FLUSH_CF_MASK                        0x3 /* cf14 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_SQ_FLUSH_CF_SHIFT                       4
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF15_MASK                               0x3 /* cf15 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF15_SHIFT                              6
	u8 flags6;
#define E5_XSTORM_IWARP_CONN_AG_CTX_MPA_OR_ERROR_WAKEUP_TRIGGER_CF_MASK     0x3 /* cf16 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_MPA_OR_ERROR_WAKEUP_TRIGGER_CF_SHIFT    0
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF17_MASK                               0x3 /* cf_array_cf */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF17_SHIFT                              2
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF18_MASK                               0x3 /* cf18 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF18_SHIFT                              4
#define E5_XSTORM_IWARP_CONN_AG_CTX_DQ_FLUSH_MASK                           0x3 /* cf19 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_DQ_FLUSH_SHIFT                          6
	u8 flags7;
#define E5_XSTORM_IWARP_CONN_AG_CTX_FLUSH_Q0_MASK                           0x3 /* cf20 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_FLUSH_Q0_SHIFT                          0
#define E5_XSTORM_IWARP_CONN_AG_CTX_FLUSH_Q1_MASK                           0x3 /* cf21 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_FLUSH_Q1_SHIFT                          2
#define E5_XSTORM_IWARP_CONN_AG_CTX_SLOW_PATH_MASK                          0x3 /* cf22 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_SLOW_PATH_SHIFT                         4
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF0EN_MASK                              0x1 /* cf0en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF0EN_SHIFT                             6
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF1EN_MASK                              0x1 /* cf1en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF1EN_SHIFT                             7
	u8 flags8;
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF2EN_MASK                              0x1 /* cf2en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF2EN_SHIFT                             0
#define E5_XSTORM_IWARP_CONN_AG_CTX_TIMER_STOP_ALL_EN_MASK                  0x1 /* cf3en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_TIMER_STOP_ALL_EN_SHIFT                 1
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF4EN_MASK                              0x1 /* cf4en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF4EN_SHIFT                             2
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF5EN_MASK                              0x1 /* cf5en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF5EN_SHIFT                             3
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF6EN_MASK                              0x1 /* cf6en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF6EN_SHIFT                             4
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF7EN_MASK                              0x1 /* cf7en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF7EN_SHIFT                             5
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF8EN_MASK                              0x1 /* cf8en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF8EN_SHIFT                             6
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF9EN_MASK                              0x1 /* cf9en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF9EN_SHIFT                             7
	u8 flags9;
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF10EN_MASK                             0x1 /* cf10en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF10EN_SHIFT                            0
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF11EN_MASK                             0x1 /* cf11en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF11EN_SHIFT                            1
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF12EN_MASK                             0x1 /* cf12en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF12EN_SHIFT                            2
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF13EN_MASK                             0x1 /* cf13en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF13EN_SHIFT                            3
#define E5_XSTORM_IWARP_CONN_AG_CTX_SQ_FLUSH_CF_EN_MASK                     0x1 /* cf14en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_SQ_FLUSH_CF_EN_SHIFT                    4
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF15EN_MASK                             0x1 /* cf15en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF15EN_SHIFT                            5
#define E5_XSTORM_IWARP_CONN_AG_CTX_MPA_OR_ERROR_WAKEUP_TRIGGER_CF_EN_MASK  0x1 /* cf16en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_MPA_OR_ERROR_WAKEUP_TRIGGER_CF_EN_SHIFT 6
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF17EN_MASK                             0x1 /* cf_array_cf_en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF17EN_SHIFT                            7
	u8 flags10;
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF18EN_MASK                             0x1 /* cf18en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF18EN_SHIFT                            0
#define E5_XSTORM_IWARP_CONN_AG_CTX_DQ_FLUSH_EN_MASK                        0x1 /* cf19en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_DQ_FLUSH_EN_SHIFT                       1
#define E5_XSTORM_IWARP_CONN_AG_CTX_FLUSH_Q0_EN_MASK                        0x1 /* cf20en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_FLUSH_Q0_EN_SHIFT                       2
#define E5_XSTORM_IWARP_CONN_AG_CTX_FLUSH_Q1_EN_MASK                        0x1 /* cf21en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_FLUSH_Q1_EN_SHIFT                       3
#define E5_XSTORM_IWARP_CONN_AG_CTX_SLOW_PATH_EN_MASK                       0x1 /* cf22en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_SLOW_PATH_EN_SHIFT                      4
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF23EN_MASK                             0x1 /* cf23en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF23EN_SHIFT                            5
#define E5_XSTORM_IWARP_CONN_AG_CTX_RULE0EN_MASK                            0x1 /* rule0en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_RULE0EN_SHIFT                           6
#define E5_XSTORM_IWARP_CONN_AG_CTX_MORE_TO_SEND_RULE_EN_MASK               0x1 /* rule1en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_MORE_TO_SEND_RULE_EN_SHIFT              7
	u8 flags11;
#define E5_XSTORM_IWARP_CONN_AG_CTX_TX_BLOCKED_EN_MASK                      0x1 /* rule2en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_TX_BLOCKED_EN_SHIFT                     0
#define E5_XSTORM_IWARP_CONN_AG_CTX_RULE3EN_MASK                            0x1 /* rule3en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_RULE3EN_SHIFT                           1
#define E5_XSTORM_IWARP_CONN_AG_CTX_RESERVED3_MASK                          0x1 /* rule4en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_RESERVED3_SHIFT                         2
#define E5_XSTORM_IWARP_CONN_AG_CTX_RULE5EN_MASK                            0x1 /* rule5en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_RULE5EN_SHIFT                           3
#define E5_XSTORM_IWARP_CONN_AG_CTX_RULE6EN_MASK                            0x1 /* rule6en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_RULE6EN_SHIFT                           4
#define E5_XSTORM_IWARP_CONN_AG_CTX_RULE7EN_MASK                            0x1 /* rule7en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_RULE7EN_SHIFT                           5
#define E5_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED1_MASK                       0x1 /* rule8en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED1_SHIFT                      6
#define E5_XSTORM_IWARP_CONN_AG_CTX_RULE9EN_MASK                            0x1 /* rule9en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_RULE9EN_SHIFT                           7
	u8 flags12;
#define E5_XSTORM_IWARP_CONN_AG_CTX_SQ_NOT_EMPTY_RULE_EN_MASK               0x1 /* rule10en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_SQ_NOT_EMPTY_RULE_EN_SHIFT              0
#define E5_XSTORM_IWARP_CONN_AG_CTX_RULE11EN_MASK                           0x1 /* rule11en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_RULE11EN_SHIFT                          1
#define E5_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED2_MASK                       0x1 /* rule12en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED2_SHIFT                      2
#define E5_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED3_MASK                       0x1 /* rule13en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED3_SHIFT                      3
#define E5_XSTORM_IWARP_CONN_AG_CTX_SQ_FENCE_RULE_EN_MASK                   0x1 /* rule14en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_SQ_FENCE_RULE_EN_SHIFT                  4
#define E5_XSTORM_IWARP_CONN_AG_CTX_RULE15EN_MASK                           0x1 /* rule15en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_RULE15EN_SHIFT                          5
#define E5_XSTORM_IWARP_CONN_AG_CTX_RULE16EN_MASK                           0x1 /* rule16en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_RULE16EN_SHIFT                          6
#define E5_XSTORM_IWARP_CONN_AG_CTX_RULE17EN_MASK                           0x1 /* rule17en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_RULE17EN_SHIFT                          7
	u8 flags13;
#define E5_XSTORM_IWARP_CONN_AG_CTX_IRQ_NOT_EMPTY_RULE_EN_MASK              0x1 /* rule18en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_IRQ_NOT_EMPTY_RULE_EN_SHIFT             0
#define E5_XSTORM_IWARP_CONN_AG_CTX_HQ_NOT_FULL_RULE_EN_MASK                0x1 /* rule19en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_HQ_NOT_FULL_RULE_EN_SHIFT               1
#define E5_XSTORM_IWARP_CONN_AG_CTX_ORQ_RD_FENCE_RULE_EN_MASK               0x1 /* rule20en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_ORQ_RD_FENCE_RULE_EN_SHIFT              2
#define E5_XSTORM_IWARP_CONN_AG_CTX_RULE21EN_MASK                           0x1 /* rule21en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_RULE21EN_SHIFT                          3
#define E5_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED6_MASK                       0x1 /* rule22en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED6_SHIFT                      4
#define E5_XSTORM_IWARP_CONN_AG_CTX_ORQ_NOT_FULL_RULE_EN_MASK               0x1 /* rule23en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_ORQ_NOT_FULL_RULE_EN_SHIFT              5
#define E5_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED8_MASK                       0x1 /* rule24en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED8_SHIFT                      6
#define E5_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED9_MASK                       0x1 /* rule25en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_A0_RESERVED9_SHIFT                      7
	u8 flags14;
#define E5_XSTORM_IWARP_CONN_AG_CTX_BIT16_MASK                              0x1 /* bit16 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_BIT16_SHIFT                             0
#define E5_XSTORM_IWARP_CONN_AG_CTX_BIT17_MASK                              0x1 /* bit17 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_BIT17_SHIFT                             1
#define E5_XSTORM_IWARP_CONN_AG_CTX_DPM_PORT_NUM_MASK                       0x3 /* bit18 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_DPM_PORT_NUM_SHIFT                      2
#define E5_XSTORM_IWARP_CONN_AG_CTX_BIT20_MASK                              0x1 /* bit20 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_BIT20_SHIFT                             4
#define E5_XSTORM_IWARP_CONN_AG_CTX_RDMA_EDPM_ENABLE_MASK                   0x1 /* bit21 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_RDMA_EDPM_ENABLE_SHIFT                  5
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF23_MASK                               0x3 /* cf23 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_CF23_SHIFT                              6
	u8 byte2 /* byte2 */;
	__le16 physical_q0 /* physical_q0 */;
	__le16 physical_q1 /* physical_q1 */;
	__le16 sq_comp_cons /* physical_q2 */;
	__le16 sq_tx_cons /* word3 */;
	__le16 sq_prod /* word4 */;
	__le16 word5 /* word5 */;
	__le16 conn_dpi /* conn_dpi */;
	u8 byte3 /* byte3 */;
	u8 byte4 /* byte4 */;
	u8 byte5 /* byte5 */;
	u8 byte6 /* byte6 */;
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
	__le32 reg2 /* reg2 */;
	__le32 more_to_send_seq /* reg3 */;
	__le32 reg4 /* reg4 */;
	__le32 rewinded_snd_max /* cf_array0 */;
	__le32 rd_msn /* cf_array1 */;
	u8 flags15;
#define E5_XSTORM_IWARP_CONN_AG_CTX_E4_RESERVED1_MASK                       0x1 /* bit22 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_E4_RESERVED1_SHIFT                      0
#define E5_XSTORM_IWARP_CONN_AG_CTX_E4_RESERVED2_MASK                       0x1 /* bit23 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_E4_RESERVED2_SHIFT                      1
#define E5_XSTORM_IWARP_CONN_AG_CTX_E4_RESERVED3_MASK                       0x1 /* bit24 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_E4_RESERVED3_SHIFT                      2
#define E5_XSTORM_IWARP_CONN_AG_CTX_E4_RESERVED4_MASK                       0x3 /* cf24 */
#define E5_XSTORM_IWARP_CONN_AG_CTX_E4_RESERVED4_SHIFT                      3
#define E5_XSTORM_IWARP_CONN_AG_CTX_E4_RESERVED5_MASK                       0x1 /* cf24en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_E4_RESERVED5_SHIFT                      5
#define E5_XSTORM_IWARP_CONN_AG_CTX_E4_RESERVED6_MASK                       0x1 /* rule26en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_E4_RESERVED6_SHIFT                      6
#define E5_XSTORM_IWARP_CONN_AG_CTX_E4_RESERVED7_MASK                       0x1 /* rule27en */
#define E5_XSTORM_IWARP_CONN_AG_CTX_E4_RESERVED7_SHIFT                      7
	u8 byte7 /* byte7 */;
	__le16 irq_prod_via_msdm /* word7 */;
	__le16 irq_cons /* word8 */;
	__le16 hq_cons_th_or_mpa_data /* word9 */;
	__le16 hq_cons /* word10 */;
	__le16 tx_rdma_edpm_usg_cnt /* word11 */;
	__le32 atom_msn /* reg7 */;
	__le32 orq_cons /* reg8 */;
	__le32 orq_cons_th /* reg9 */;
	u8 max_ord /* byte8 */;
	u8 wqe_data_pad_bytes /* byte9 */;
	u8 former_hq_prod /* byte10 */;
	u8 irq_prod_via_msem /* byte11 */;
	u8 byte12 /* byte12 */;
	u8 max_pkt_pdu_size_lo /* byte13 */;
	u8 max_pkt_pdu_size_hi /* byte14 */;
	u8 byte15 /* byte15 */;
	__le32 reg10 /* reg10 */;
	__le32 reg11 /* reg11 */;
	__le32 reg12 /* reg12 */;
	__le32 shared_queue_page_addr_lo /* reg13 */;
	__le32 shared_queue_page_addr_hi /* reg14 */;
	__le32 reg15 /* reg15 */;
	__le32 reg16 /* reg16 */;
	__le32 reg17 /* reg17 */;
};

struct e5_tstorm_iwarp_conn_ag_ctx
{
	u8 reserved0 /* cdu_validation */;
	u8 state_and_core_id /* state_and_core_id */;
	u8 flags0;
#define E5_TSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM0_MASK                0x1 /* exist_in_qm0 */
#define E5_TSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM0_SHIFT               0
#define E5_TSTORM_IWARP_CONN_AG_CTX_BIT1_MASK                        0x1 /* exist_in_qm1 */
#define E5_TSTORM_IWARP_CONN_AG_CTX_BIT1_SHIFT                       1
#define E5_TSTORM_IWARP_CONN_AG_CTX_BIT2_MASK                        0x1 /* bit2 */
#define E5_TSTORM_IWARP_CONN_AG_CTX_BIT2_SHIFT                       2
#define E5_TSTORM_IWARP_CONN_AG_CTX_MSTORM_FLUSH_MASK                0x1 /* bit3 */
#define E5_TSTORM_IWARP_CONN_AG_CTX_MSTORM_FLUSH_SHIFT               3
#define E5_TSTORM_IWARP_CONN_AG_CTX_BIT4_MASK                        0x1 /* bit4 */
#define E5_TSTORM_IWARP_CONN_AG_CTX_BIT4_SHIFT                       4
#define E5_TSTORM_IWARP_CONN_AG_CTX_CACHED_ORQ_MASK                  0x1 /* bit5 */
#define E5_TSTORM_IWARP_CONN_AG_CTX_CACHED_ORQ_SHIFT                 5
#define E5_TSTORM_IWARP_CONN_AG_CTX_CF0_MASK                         0x3 /* timer0cf */
#define E5_TSTORM_IWARP_CONN_AG_CTX_CF0_SHIFT                        6
	u8 flags1;
#define E5_TSTORM_IWARP_CONN_AG_CTX_RQ_POST_CF_MASK                  0x3 /* timer1cf */
#define E5_TSTORM_IWARP_CONN_AG_CTX_RQ_POST_CF_SHIFT                 0
#define E5_TSTORM_IWARP_CONN_AG_CTX_FLUSH_OR_ERROR_DETECTED_MASK     0x3 /* timer2cf */
#define E5_TSTORM_IWARP_CONN_AG_CTX_FLUSH_OR_ERROR_DETECTED_SHIFT    2
#define E5_TSTORM_IWARP_CONN_AG_CTX_TIMER_STOP_ALL_MASK              0x3 /* timer_stop_all */
#define E5_TSTORM_IWARP_CONN_AG_CTX_TIMER_STOP_ALL_SHIFT             4
#define E5_TSTORM_IWARP_CONN_AG_CTX_CF4_MASK                         0x3 /* cf4 */
#define E5_TSTORM_IWARP_CONN_AG_CTX_CF4_SHIFT                        6
	u8 flags2;
#define E5_TSTORM_IWARP_CONN_AG_CTX_CF5_MASK                         0x3 /* cf5 */
#define E5_TSTORM_IWARP_CONN_AG_CTX_CF5_SHIFT                        0
#define E5_TSTORM_IWARP_CONN_AG_CTX_CF6_MASK                         0x3 /* cf6 */
#define E5_TSTORM_IWARP_CONN_AG_CTX_CF6_SHIFT                        2
#define E5_TSTORM_IWARP_CONN_AG_CTX_CF7_MASK                         0x3 /* cf7 */
#define E5_TSTORM_IWARP_CONN_AG_CTX_CF7_SHIFT                        4
#define E5_TSTORM_IWARP_CONN_AG_CTX_CF8_MASK                         0x3 /* cf8 */
#define E5_TSTORM_IWARP_CONN_AG_CTX_CF8_SHIFT                        6
	u8 flags3;
#define E5_TSTORM_IWARP_CONN_AG_CTX_FLUSH_Q0_MASK                    0x3 /* cf9 */
#define E5_TSTORM_IWARP_CONN_AG_CTX_FLUSH_Q0_SHIFT                   0
#define E5_TSTORM_IWARP_CONN_AG_CTX_CF10_MASK                        0x3 /* cf10 */
#define E5_TSTORM_IWARP_CONN_AG_CTX_CF10_SHIFT                       2
#define E5_TSTORM_IWARP_CONN_AG_CTX_CF0EN_MASK                       0x1 /* cf0en */
#define E5_TSTORM_IWARP_CONN_AG_CTX_CF0EN_SHIFT                      4
#define E5_TSTORM_IWARP_CONN_AG_CTX_RQ_POST_CF_EN_MASK               0x1 /* cf1en */
#define E5_TSTORM_IWARP_CONN_AG_CTX_RQ_POST_CF_EN_SHIFT              5
#define E5_TSTORM_IWARP_CONN_AG_CTX_FLUSH_OR_ERROR_DETECTED_EN_MASK  0x1 /* cf2en */
#define E5_TSTORM_IWARP_CONN_AG_CTX_FLUSH_OR_ERROR_DETECTED_EN_SHIFT 6
#define E5_TSTORM_IWARP_CONN_AG_CTX_TIMER_STOP_ALL_EN_MASK           0x1 /* cf3en */
#define E5_TSTORM_IWARP_CONN_AG_CTX_TIMER_STOP_ALL_EN_SHIFT          7
	u8 flags4;
#define E5_TSTORM_IWARP_CONN_AG_CTX_CF4EN_MASK                       0x1 /* cf4en */
#define E5_TSTORM_IWARP_CONN_AG_CTX_CF4EN_SHIFT                      0
#define E5_TSTORM_IWARP_CONN_AG_CTX_CF5EN_MASK                       0x1 /* cf5en */
#define E5_TSTORM_IWARP_CONN_AG_CTX_CF5EN_SHIFT                      1
#define E5_TSTORM_IWARP_CONN_AG_CTX_CF6EN_MASK                       0x1 /* cf6en */
#define E5_TSTORM_IWARP_CONN_AG_CTX_CF6EN_SHIFT                      2
#define E5_TSTORM_IWARP_CONN_AG_CTX_CF7EN_MASK                       0x1 /* cf7en */
#define E5_TSTORM_IWARP_CONN_AG_CTX_CF7EN_SHIFT                      3
#define E5_TSTORM_IWARP_CONN_AG_CTX_CF8EN_MASK                       0x1 /* cf8en */
#define E5_TSTORM_IWARP_CONN_AG_CTX_CF8EN_SHIFT                      4
#define E5_TSTORM_IWARP_CONN_AG_CTX_FLUSH_Q0_EN_MASK                 0x1 /* cf9en */
#define E5_TSTORM_IWARP_CONN_AG_CTX_FLUSH_Q0_EN_SHIFT                5
#define E5_TSTORM_IWARP_CONN_AG_CTX_CF10EN_MASK                      0x1 /* cf10en */
#define E5_TSTORM_IWARP_CONN_AG_CTX_CF10EN_SHIFT                     6
#define E5_TSTORM_IWARP_CONN_AG_CTX_RULE0EN_MASK                     0x1 /* rule0en */
#define E5_TSTORM_IWARP_CONN_AG_CTX_RULE0EN_SHIFT                    7
	u8 flags5;
#define E5_TSTORM_IWARP_CONN_AG_CTX_RULE1EN_MASK                     0x1 /* rule1en */
#define E5_TSTORM_IWARP_CONN_AG_CTX_RULE1EN_SHIFT                    0
#define E5_TSTORM_IWARP_CONN_AG_CTX_RULE2EN_MASK                     0x1 /* rule2en */
#define E5_TSTORM_IWARP_CONN_AG_CTX_RULE2EN_SHIFT                    1
#define E5_TSTORM_IWARP_CONN_AG_CTX_RULE3EN_MASK                     0x1 /* rule3en */
#define E5_TSTORM_IWARP_CONN_AG_CTX_RULE3EN_SHIFT                    2
#define E5_TSTORM_IWARP_CONN_AG_CTX_RULE4EN_MASK                     0x1 /* rule4en */
#define E5_TSTORM_IWARP_CONN_AG_CTX_RULE4EN_SHIFT                    3
#define E5_TSTORM_IWARP_CONN_AG_CTX_RULE5EN_MASK                     0x1 /* rule5en */
#define E5_TSTORM_IWARP_CONN_AG_CTX_RULE5EN_SHIFT                    4
#define E5_TSTORM_IWARP_CONN_AG_CTX_SND_SQ_CONS_RULE_MASK            0x1 /* rule6en */
#define E5_TSTORM_IWARP_CONN_AG_CTX_SND_SQ_CONS_RULE_SHIFT           5
#define E5_TSTORM_IWARP_CONN_AG_CTX_RULE7EN_MASK                     0x1 /* rule7en */
#define E5_TSTORM_IWARP_CONN_AG_CTX_RULE7EN_SHIFT                    6
#define E5_TSTORM_IWARP_CONN_AG_CTX_RULE8EN_MASK                     0x1 /* rule8en */
#define E5_TSTORM_IWARP_CONN_AG_CTX_RULE8EN_SHIFT                    7
	u8 flags6;
#define E5_TSTORM_IWARP_CONN_AG_CTX_E4_RESERVED1_MASK                0x1 /* bit6 */
#define E5_TSTORM_IWARP_CONN_AG_CTX_E4_RESERVED1_SHIFT               0
#define E5_TSTORM_IWARP_CONN_AG_CTX_E4_RESERVED2_MASK                0x1 /* bit7 */
#define E5_TSTORM_IWARP_CONN_AG_CTX_E4_RESERVED2_SHIFT               1
#define E5_TSTORM_IWARP_CONN_AG_CTX_E4_RESERVED3_MASK                0x1 /* bit8 */
#define E5_TSTORM_IWARP_CONN_AG_CTX_E4_RESERVED3_SHIFT               2
#define E5_TSTORM_IWARP_CONN_AG_CTX_E4_RESERVED4_MASK                0x3 /* cf11 */
#define E5_TSTORM_IWARP_CONN_AG_CTX_E4_RESERVED4_SHIFT               3
#define E5_TSTORM_IWARP_CONN_AG_CTX_E4_RESERVED5_MASK                0x1 /* cf11en */
#define E5_TSTORM_IWARP_CONN_AG_CTX_E4_RESERVED5_SHIFT               5
#define E5_TSTORM_IWARP_CONN_AG_CTX_E4_RESERVED6_MASK                0x1 /* rule9en */
#define E5_TSTORM_IWARP_CONN_AG_CTX_E4_RESERVED6_SHIFT               6
#define E5_TSTORM_IWARP_CONN_AG_CTX_E4_RESERVED7_MASK                0x1 /* rule10en */
#define E5_TSTORM_IWARP_CONN_AG_CTX_E4_RESERVED7_SHIFT               7
	u8 orq_cache_idx /* byte2 */;
	__le16 sq_tx_cons_th /* word0 */;
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
	__le32 unaligned_nxt_seq /* reg2 */;
	__le32 reg3 /* reg3 */;
	__le32 reg4 /* reg4 */;
	__le32 reg5 /* reg5 */;
	__le32 reg6 /* reg6 */;
	__le32 reg7 /* reg7 */;
	__le32 reg8 /* reg8 */;
	u8 hq_prod /* byte3 */;
	u8 orq_prod /* byte4 */;
	u8 irq_cons /* byte5 */;
	u8 e4_reserved8 /* byte6 */;
	__le16 sq_tx_cons /* word1 */;
	__le16 conn_dpi /* conn_dpi */;
	__le32 snd_seq /* reg9 */;
	__le16 rq_prod /* word3 */;
	__le16 e4_reserved9 /* word4 */;
};

/*
 * iwarp connection context
 */
struct e5_iwarp_conn_context
{
	struct ystorm_iwarp_conn_st_ctx ystorm_st_context /* ystorm storm context */;
	struct regpair ystorm_st_padding[2] /* padding */;
	struct pstorm_iwarp_conn_st_ctx pstorm_st_context /* pstorm storm context */;
	struct regpair pstorm_st_padding[2] /* padding */;
	struct xstorm_iwarp_conn_st_ctx xstorm_st_context /* xstorm storm context */;
	struct regpair xstorm_st_padding[2] /* padding */;
	struct e5_xstorm_iwarp_conn_ag_ctx xstorm_ag_context /* xstorm aggregative context */;
	struct e5_tstorm_iwarp_conn_ag_ctx tstorm_ag_context /* tstorm aggregative context */;
	struct timers_context timer_context /* timer context */;
	struct e5_ustorm_rdma_conn_ag_ctx ustorm_ag_context /* ustorm aggregative context */;
	struct tstorm_iwarp_conn_st_ctx tstorm_st_context /* tstorm storm context */;
	struct regpair tstorm_st_padding[2] /* padding */;
	struct mstorm_iwarp_conn_st_ctx mstorm_st_context /* mstorm storm context */;
	struct ustorm_iwarp_conn_st_ctx ustorm_st_context /* ustorm storm context */;
};


/*
 * iWARP create QP params passed by driver to FW in CreateQP Request Ramrod 
 */
struct iwarp_create_qp_ramrod_data
{
	u8 flags;
#define IWARP_CREATE_QP_RAMROD_DATA_FMR_AND_RESERVED_EN_MASK   0x1
#define IWARP_CREATE_QP_RAMROD_DATA_FMR_AND_RESERVED_EN_SHIFT  0
#define IWARP_CREATE_QP_RAMROD_DATA_SIGNALED_COMP_MASK         0x1
#define IWARP_CREATE_QP_RAMROD_DATA_SIGNALED_COMP_SHIFT        1
#define IWARP_CREATE_QP_RAMROD_DATA_RDMA_RD_EN_MASK            0x1
#define IWARP_CREATE_QP_RAMROD_DATA_RDMA_RD_EN_SHIFT           2
#define IWARP_CREATE_QP_RAMROD_DATA_RDMA_WR_EN_MASK            0x1
#define IWARP_CREATE_QP_RAMROD_DATA_RDMA_WR_EN_SHIFT           3
#define IWARP_CREATE_QP_RAMROD_DATA_ATOMIC_EN_MASK             0x1
#define IWARP_CREATE_QP_RAMROD_DATA_ATOMIC_EN_SHIFT            4
#define IWARP_CREATE_QP_RAMROD_DATA_SRQ_FLG_MASK               0x1
#define IWARP_CREATE_QP_RAMROD_DATA_SRQ_FLG_SHIFT              5
#define IWARP_CREATE_QP_RAMROD_DATA_LOW_LATENCY_QUEUE_EN_MASK  0x1
#define IWARP_CREATE_QP_RAMROD_DATA_LOW_LATENCY_QUEUE_EN_SHIFT 6
#define IWARP_CREATE_QP_RAMROD_DATA_RESERVED0_MASK             0x1
#define IWARP_CREATE_QP_RAMROD_DATA_RESERVED0_SHIFT            7
	u8 reserved1 /* Basic/Enhanced (use enum mpa_negotiation_mode) */;
	__le16 pd;
	__le16 sq_num_pages;
	__le16 rq_num_pages;
	__le32 reserved3[2];
	struct regpair qp_handle_for_cqe /* For use in CQEs */;
	struct rdma_srq_id srq_id;
	__le32 cq_cid_for_sq /* Cid of the CQ that will be posted from SQ */;
	__le32 cq_cid_for_rq /* Cid of the CQ that will be posted from RQ */;
	__le16 dpi;
	__le16 physical_q0 /* Physical QM queue to be tied to logical Q0 */;
	__le16 physical_q1 /* Physical QM queue to be tied to logical Q1 */;
	u8 reserved2[6];
};


/*
 * iWARP completion queue types
 */
enum iwarp_eqe_async_opcode
{
	IWARP_EVENT_TYPE_ASYNC_CONNECT_COMPLETE /* Async completion oafter TCP 3-way handshake */,
	IWARP_EVENT_TYPE_ASYNC_ENHANCED_MPA_REPLY_ARRIVED /* Enhanced MPA reply arrived. Driver should either send RTR or reject */,
	IWARP_EVENT_TYPE_ASYNC_MPA_HANDSHAKE_COMPLETE /* MPA Negotiations completed */,
	IWARP_EVENT_TYPE_ASYNC_CID_CLEANED /* Async completion that indicates to the driver that the CID can be re-used. */,
	IWARP_EVENT_TYPE_ASYNC_EXCEPTION_DETECTED /* Async EQE indicating detection of an error/exception on a QP at Firmware */,
	IWARP_EVENT_TYPE_ASYNC_QP_IN_ERROR_STATE /* Async EQE indicating QP is in Error state. */,
	IWARP_EVENT_TYPE_ASYNC_CQ_OVERFLOW /* Async EQE indicating CQ, whose handle is sent with this event, has overflowed */,
	MAX_IWARP_EQE_ASYNC_OPCODE
};


struct iwarp_eqe_data_mpa_async_completion
{
	__le16 ulp_data_len /* On active side, length of ULP Data, from peers MPA Connect Response */;
	u8 reserved[6];
};


struct iwarp_eqe_data_tcp_async_completion
{
	__le16 ulp_data_len /* On passive side, length of ULP Data, from peers active MPA Connect Request */;
	u8 mpa_handshake_mode /* Negotiation type Basic/Enhanced */;
	u8 reserved[5];
};


/*
 * iWARP completion queue types
 */
enum iwarp_eqe_sync_opcode
{
	IWARP_EVENT_TYPE_TCP_OFFLOAD=11 /* iWARP event queue response after option 2 offload Ramrod */,
	IWARP_EVENT_TYPE_MPA_OFFLOAD /* Synchronous completion for MPA offload Request */,
	IWARP_EVENT_TYPE_MPA_OFFLOAD_SEND_RTR,
	IWARP_EVENT_TYPE_CREATE_QP,
	IWARP_EVENT_TYPE_QUERY_QP,
	IWARP_EVENT_TYPE_MODIFY_QP,
	IWARP_EVENT_TYPE_DESTROY_QP,
	IWARP_EVENT_TYPE_ABORT_TCP_OFFLOAD,
	MAX_IWARP_EQE_SYNC_OPCODE
};


/*
 * iWARP EQE completion status 
 */
enum iwarp_fw_return_code
{
	IWARP_CONN_ERROR_TCP_CONNECT_INVALID_PACKET=5 /* Got invalid packet SYN/SYN-ACK */,
	IWARP_CONN_ERROR_TCP_CONNECTION_RST /* Got RST during offload TCP connection  */,
	IWARP_CONN_ERROR_TCP_CONNECT_TIMEOUT /* TCP connection setup timed out */,
	IWARP_CONN_ERROR_MPA_ERROR_REJECT /* Got Reject in MPA reply. */,
	IWARP_CONN_ERROR_MPA_NOT_SUPPORTED_VER /* Got MPA request with higher version that we support. */,
	IWARP_CONN_ERROR_MPA_RST /* Got RST during MPA negotiation */,
	IWARP_CONN_ERROR_MPA_FIN /* Got FIN during MPA negotiation */,
	IWARP_CONN_ERROR_MPA_RTR_MISMATCH /* RTR mismatch detected when MPA reply arrived. */,
	IWARP_CONN_ERROR_MPA_INSUF_IRD /* Insufficient IRD on the MPA reply that arrived. */,
	IWARP_CONN_ERROR_MPA_INVALID_PACKET /* Incoming MPAp acket failed on FW verifications */,
	IWARP_CONN_ERROR_MPA_LOCAL_ERROR /* Detected an internal error during MPA negotiation. */,
	IWARP_CONN_ERROR_MPA_TIMEOUT /* MPA negotiation timed out. */,
	IWARP_CONN_ERROR_MPA_TERMINATE /* Got Terminate during MPA negotiation. */,
	IWARP_QP_IN_ERROR_GOOD_CLOSE /* LLP connection was closed gracefully - Used for async IWARP_EVENT_TYPE_ASYNC_QP_IN_ERROR_STATE */,
	IWARP_QP_IN_ERROR_BAD_CLOSE /* LLP Connection was closed abortively - Used for async IWARP_EVENT_TYPE_ASYNC_QP_IN_ERROR_STATE */,
	IWARP_EXCEPTION_DETECTED_LLP_CLOSED /* LLP has been disociated from the QP, although the TCP connection may not be closed yet - Used for async IWARP_EVENT_TYPE_ASYNC_EXCEPTION_DETECTED */,
	IWARP_EXCEPTION_DETECTED_LLP_RESET /* LLP has Reset (either because of an RST, or a bad-close condition) - Used for async IWARP_EVENT_TYPE_ASYNC_EXCEPTION_DETECTED */,
	IWARP_EXCEPTION_DETECTED_IRQ_FULL /* Peer sent more outstanding Read Requests than IRD - Used for async IWARP_EVENT_TYPE_ASYNC_EXCEPTION_DETECTED */,
	IWARP_EXCEPTION_DETECTED_RQ_EMPTY /* SEND request received with RQ empty - Used for async IWARP_EVENT_TYPE_ASYNC_EXCEPTION_DETECTED */,
	IWARP_EXCEPTION_DETECTED_SRQ_EMPTY /* SEND request received with SRQ empty - Used for async IWARP_EVENT_TYPE_ASYNC_EXCEPTION_DETECTED */,
	IWARP_EXCEPTION_DETECTED_SRQ_LIMIT /* Number of SRQ wqes is below the limit */,
	IWARP_EXCEPTION_DETECTED_LLP_TIMEOUT /* TCP Retransmissions timed out - Used for async IWARP_EVENT_TYPE_ASYNC_EXCEPTION_DETECTED */,
	IWARP_EXCEPTION_DETECTED_REMOTE_PROTECTION_ERROR /* Peers Remote Access caused error */,
	IWARP_EXCEPTION_DETECTED_CQ_OVERFLOW /* CQ overflow detected */,
	IWARP_EXCEPTION_DETECTED_LOCAL_CATASTROPHIC /* Local catastrophic error detected - Used for async IWARP_EVENT_TYPE_ASYNC_EXCEPTION_DETECTED */,
	IWARP_EXCEPTION_DETECTED_LOCAL_ACCESS_ERROR /* Local Access error detected while responding - Used for async IWARP_EVENT_TYPE_ASYNC_EXCEPTION_DETECTED */,
	IWARP_EXCEPTION_DETECTED_REMOTE_OPERATION_ERROR /* An operation/protocol error caused by Remote Consumer */,
	IWARP_EXCEPTION_DETECTED_TERMINATE_RECEIVED /* Peer sent a TERMINATE message */,
	MAX_IWARP_FW_RETURN_CODE
};


/*
 * unaligned opaque data received from LL2
 */
struct iwarp_init_func_params
{
	u8 ll2_ooo_q_index /* LL2 OOO queue id. The unaligned queue id will be + 1 */;
	u8 reserved1[7];
};


/*
 * iwarp func init ramrod data
 */
struct iwarp_init_func_ramrod_data
{
	struct rdma_init_func_ramrod_data rdma;
	struct tcp_init_params tcp;
	struct iwarp_init_func_params iwarp;
};


/*
 * iWARP QP - possible states to transition to
 */
enum iwarp_modify_qp_new_state_type
{
	IWARP_MODIFY_QP_STATE_CLOSING=1 /* graceful close */,
	IWARP_MODIFY_QP_STATE_ERROR=2 /* abortive close, if LLP connection still exists */,
	MAX_IWARP_MODIFY_QP_NEW_STATE_TYPE
};


/*
 * iwarp modify qp responder ramrod data
 */
struct iwarp_modify_qp_ramrod_data
{
	__le16 transition_to_state /*  (use enum iwarp_modify_qp_new_state_type) */;
	__le16 flags;
#define IWARP_MODIFY_QP_RAMROD_DATA_RDMA_RD_EN_MASK          0x1
#define IWARP_MODIFY_QP_RAMROD_DATA_RDMA_RD_EN_SHIFT         0
#define IWARP_MODIFY_QP_RAMROD_DATA_RDMA_WR_EN_MASK          0x1
#define IWARP_MODIFY_QP_RAMROD_DATA_RDMA_WR_EN_SHIFT         1
#define IWARP_MODIFY_QP_RAMROD_DATA_ATOMIC_EN_MASK           0x1
#define IWARP_MODIFY_QP_RAMROD_DATA_ATOMIC_EN_SHIFT          2
#define IWARP_MODIFY_QP_RAMROD_DATA_STATE_TRANS_EN_MASK      0x1 /* change QP state as per transition_to_state field */
#define IWARP_MODIFY_QP_RAMROD_DATA_STATE_TRANS_EN_SHIFT     3
#define IWARP_MODIFY_QP_RAMROD_DATA_RDMA_OPS_EN_FLG_MASK     0x1 /* If set, the rdma_rd/wr/atomic_en should be updated */
#define IWARP_MODIFY_QP_RAMROD_DATA_RDMA_OPS_EN_FLG_SHIFT    4
#define IWARP_MODIFY_QP_RAMROD_DATA_PHYSICAL_QUEUE_FLG_MASK  0x1 /* If set, the  physicalQ1Val/physicalQ0Val/regularLatencyPhyQueue should be updated */
#define IWARP_MODIFY_QP_RAMROD_DATA_PHYSICAL_QUEUE_FLG_SHIFT 5
#define IWARP_MODIFY_QP_RAMROD_DATA_RESERVED_MASK            0x3FF
#define IWARP_MODIFY_QP_RAMROD_DATA_RESERVED_SHIFT           6
	__le16 physical_q0 /* Updated physicalQ0Val */;
	__le16 physical_q1 /* Updated physicalQ1Val */;
	__le32 reserved1[10];
};


/*
 * MPA params for Enhanced mode
 */
struct mpa_rq_params
{
	__le32 ird;
	__le32 ord;
};

/*
 * MPA host Address-Len for private data
 */
struct mpa_ulp_buffer
{
	struct regpair addr;
	__le16 len;
	__le16 reserved[3];
};

/*
 * iWARP MPA offload params common to Basic and Enhanced modes
 */
struct mpa_outgoing_params
{
	u8 crc_needed;
	u8 reject /* Valid only for passive side. */;
	u8 reserved[6];
	struct mpa_rq_params out_rq;
	struct mpa_ulp_buffer outgoing_ulp_buffer /* ULP buffer populated by the host */;
};

/*
 * iWARP MPA offload params passed by driver to FW in MPA Offload Request Ramrod 
 */
struct iwarp_mpa_offload_ramrod_data
{
	struct mpa_outgoing_params common;
	__le32 tcp_cid;
	u8 mode /* Basic/Enhanced (use enum mpa_negotiation_mode) */;
	u8 tcp_connect_side /* Passive/Active. use enum tcp_connect_mode */;
	u8 rtr_pref;
#define IWARP_MPA_OFFLOAD_RAMROD_DATA_RTR_SUPPORTED_MASK  0x7 /*  (use enum mpa_rtr_type) */
#define IWARP_MPA_OFFLOAD_RAMROD_DATA_RTR_SUPPORTED_SHIFT 0
#define IWARP_MPA_OFFLOAD_RAMROD_DATA_RESERVED1_MASK      0x1F
#define IWARP_MPA_OFFLOAD_RAMROD_DATA_RESERVED1_SHIFT     3
	u8 reserved2;
	struct mpa_ulp_buffer incoming_ulp_buffer /* host buffer for placing the incoming MPA reply */;
	struct regpair async_eqe_output_buf /* host buffer for async tcp/mpa completion information - must have space for at least 8 bytes */;
	struct regpair handle_for_async /* a host cookie that will be echoed back with in every qp-specific async EQE */;
	struct regpair shared_queue_addr /* Address of shared queue address that consist of SQ/RQ and FW internal queues (IRQ/ORQ/HQ) */;
	__le16 rcv_wnd /* TCP window after scaling */;
	u8 stats_counter_id /* Statistics counter ID to use */;
	u8 reserved3[13];
};


/*
 * iWARP TCP connection offload params passed by driver to FW 
 */
struct iwarp_offload_params
{
	struct mpa_ulp_buffer incoming_ulp_buffer /* host buffer for placing the incoming MPA request */;
	struct regpair async_eqe_output_buf /* host buffer for async tcp/mpa completion information - must have space for at least 8 bytes */;
	struct regpair handle_for_async /* host handle that will be echoed back with in every qp-specific async EQE */;
	__le16 physical_q0 /* Physical QM queue to be tied to logical Q0 */;
	__le16 physical_q1 /* Physical QM queue to be tied to logical Q1 */;
	u8 stats_counter_id /* Statistics counter ID to use */;
	u8 mpa_mode /* Basic/Enahnced. Used for a verification for incoming MPA request (use enum mpa_negotiation_mode) */;
	u8 reserved[10];
};


/*
 * iWARP query QP output params
 */
struct iwarp_query_qp_output_params
{
	__le32 flags;
#define IWARP_QUERY_QP_OUTPUT_PARAMS_ERROR_FLG_MASK  0x1
#define IWARP_QUERY_QP_OUTPUT_PARAMS_ERROR_FLG_SHIFT 0
#define IWARP_QUERY_QP_OUTPUT_PARAMS_RESERVED0_MASK  0x7FFFFFFF
#define IWARP_QUERY_QP_OUTPUT_PARAMS_RESERVED0_SHIFT 1
	u8 reserved1[4] /* 64 bit alignment */;
};


/*
 * iWARP query QP ramrod data
 */
struct iwarp_query_qp_ramrod_data
{
	struct regpair output_params_addr;
};


/*
 * iWARP Ramrod Command IDs 
 */
enum iwarp_ramrod_cmd_id
{
	IWARP_RAMROD_CMD_ID_TCP_OFFLOAD=11 /* iWARP TCP connection offload ramrod */,
	IWARP_RAMROD_CMD_ID_MPA_OFFLOAD /* iWARP MPA offload ramrod */,
	IWARP_RAMROD_CMD_ID_MPA_OFFLOAD_SEND_RTR,
	IWARP_RAMROD_CMD_ID_CREATE_QP,
	IWARP_RAMROD_CMD_ID_QUERY_QP,
	IWARP_RAMROD_CMD_ID_MODIFY_QP,
	IWARP_RAMROD_CMD_ID_DESTROY_QP,
	IWARP_RAMROD_CMD_ID_ABORT_TCP_OFFLOAD,
	MAX_IWARP_RAMROD_CMD_ID
};


/*
 * Per PF iWARP retransmit path statistics
 */
struct iwarp_rxmit_stats_drv
{
	struct regpair tx_go_to_slow_start_event_cnt /* Number of times slow start event occurred */;
	struct regpair tx_fast_retransmit_event_cnt /* Number of times fast retransmit event occurred */;
};


/*
 * iWARP and TCP connection offload params passed by driver to FW in iWARP offload ramrod 
 */
struct iwarp_tcp_offload_ramrod_data
{
	struct iwarp_offload_params iwarp /* iWARP connection offload params */;
	struct tcp_offload_params_opt2 tcp /* tcp offload params */;
};


/*
 * iWARP MPA negotiation types
 */
enum mpa_negotiation_mode
{
	MPA_NEGOTIATION_TYPE_BASIC=1,
	MPA_NEGOTIATION_TYPE_ENHANCED=2,
	MAX_MPA_NEGOTIATION_MODE
};




/*
 * iWARP MPA Enhanced mode RTR types
 */
enum mpa_rtr_type
{
	MPA_RTR_TYPE_NONE=0 /* No RTR type */,
	MPA_RTR_TYPE_ZERO_SEND=1,
	MPA_RTR_TYPE_ZERO_WRITE=2,
	MPA_RTR_TYPE_ZERO_SEND_AND_WRITE=3,
	MPA_RTR_TYPE_ZERO_READ=4,
	MPA_RTR_TYPE_ZERO_SEND_AND_READ=5,
	MPA_RTR_TYPE_ZERO_WRITE_AND_READ=6,
	MPA_RTR_TYPE_ZERO_SEND_AND_WRITE_AND_READ=7,
	MAX_MPA_RTR_TYPE
};






/*
 * unaligned opaque data received from LL2
 */
struct unaligned_opaque_data
{
	__le16 first_mpa_offset /* offset of first MPA byte that should be processed */;
	u8 tcp_payload_offset /* offset of first the byte that comes after the last byte of the TCP Hdr */;
	u8 flags;
#define UNALIGNED_OPAQUE_DATA_PKT_REACHED_WIN_RIGHT_EDGE_MASK  0x1 /* packet reached window right edge */
#define UNALIGNED_OPAQUE_DATA_PKT_REACHED_WIN_RIGHT_EDGE_SHIFT 0
#define UNALIGNED_OPAQUE_DATA_CONNECTION_CLOSED_MASK           0x1 /* Indication that the connection is closed. Clean all connecitons database. */
#define UNALIGNED_OPAQUE_DATA_CONNECTION_CLOSED_SHIFT          1
#define UNALIGNED_OPAQUE_DATA_RESERVED_MASK                    0x3F
#define UNALIGNED_OPAQUE_DATA_RESERVED_SHIFT                   2
	__le32 cid;
};





struct e4_mstorm_iwarp_conn_ag_ctx
{
	u8 reserved /* cdu_validation */;
	u8 state /* state */;
	u8 flags0;
#define E4_MSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM0_MASK         0x1 /* exist_in_qm0 */
#define E4_MSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM0_SHIFT        0
#define E4_MSTORM_IWARP_CONN_AG_CTX_BIT1_MASK                 0x1 /* exist_in_qm1 */
#define E4_MSTORM_IWARP_CONN_AG_CTX_BIT1_SHIFT                1
#define E4_MSTORM_IWARP_CONN_AG_CTX_INV_STAG_DONE_CF_MASK     0x3 /* cf0 */
#define E4_MSTORM_IWARP_CONN_AG_CTX_INV_STAG_DONE_CF_SHIFT    2
#define E4_MSTORM_IWARP_CONN_AG_CTX_CF1_MASK                  0x3 /* cf1 */
#define E4_MSTORM_IWARP_CONN_AG_CTX_CF1_SHIFT                 4
#define E4_MSTORM_IWARP_CONN_AG_CTX_CF2_MASK                  0x3 /* cf2 */
#define E4_MSTORM_IWARP_CONN_AG_CTX_CF2_SHIFT                 6
	u8 flags1;
#define E4_MSTORM_IWARP_CONN_AG_CTX_INV_STAG_DONE_CF_EN_MASK  0x1 /* cf0en */
#define E4_MSTORM_IWARP_CONN_AG_CTX_INV_STAG_DONE_CF_EN_SHIFT 0
#define E4_MSTORM_IWARP_CONN_AG_CTX_CF1EN_MASK                0x1 /* cf1en */
#define E4_MSTORM_IWARP_CONN_AG_CTX_CF1EN_SHIFT               1
#define E4_MSTORM_IWARP_CONN_AG_CTX_CF2EN_MASK                0x1 /* cf2en */
#define E4_MSTORM_IWARP_CONN_AG_CTX_CF2EN_SHIFT               2
#define E4_MSTORM_IWARP_CONN_AG_CTX_RULE0EN_MASK              0x1 /* rule0en */
#define E4_MSTORM_IWARP_CONN_AG_CTX_RULE0EN_SHIFT             3
#define E4_MSTORM_IWARP_CONN_AG_CTX_RULE1EN_MASK              0x1 /* rule1en */
#define E4_MSTORM_IWARP_CONN_AG_CTX_RULE1EN_SHIFT             4
#define E4_MSTORM_IWARP_CONN_AG_CTX_RULE2EN_MASK              0x1 /* rule2en */
#define E4_MSTORM_IWARP_CONN_AG_CTX_RULE2EN_SHIFT             5
#define E4_MSTORM_IWARP_CONN_AG_CTX_RCQ_CONS_EN_MASK          0x1 /* rule3en */
#define E4_MSTORM_IWARP_CONN_AG_CTX_RCQ_CONS_EN_SHIFT         6
#define E4_MSTORM_IWARP_CONN_AG_CTX_RULE4EN_MASK              0x1 /* rule4en */
#define E4_MSTORM_IWARP_CONN_AG_CTX_RULE4EN_SHIFT             7
	__le16 rcq_cons /* word0 */;
	__le16 rcq_cons_th /* word1 */;
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
};



struct e4_ustorm_iwarp_conn_ag_ctx
{
	u8 reserved /* cdu_validation */;
	u8 byte1 /* state */;
	u8 flags0;
#define E4_USTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM0_MASK     0x1 /* exist_in_qm0 */
#define E4_USTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM0_SHIFT    0
#define E4_USTORM_IWARP_CONN_AG_CTX_BIT1_MASK             0x1 /* exist_in_qm1 */
#define E4_USTORM_IWARP_CONN_AG_CTX_BIT1_SHIFT            1
#define E4_USTORM_IWARP_CONN_AG_CTX_CF0_MASK              0x3 /* timer0cf */
#define E4_USTORM_IWARP_CONN_AG_CTX_CF0_SHIFT             2
#define E4_USTORM_IWARP_CONN_AG_CTX_CF1_MASK              0x3 /* timer1cf */
#define E4_USTORM_IWARP_CONN_AG_CTX_CF1_SHIFT             4
#define E4_USTORM_IWARP_CONN_AG_CTX_CF2_MASK              0x3 /* timer2cf */
#define E4_USTORM_IWARP_CONN_AG_CTX_CF2_SHIFT             6
	u8 flags1;
#define E4_USTORM_IWARP_CONN_AG_CTX_CF3_MASK              0x3 /* timer_stop_all */
#define E4_USTORM_IWARP_CONN_AG_CTX_CF3_SHIFT             0
#define E4_USTORM_IWARP_CONN_AG_CTX_CQ_ARM_SE_CF_MASK     0x3 /* cf4 */
#define E4_USTORM_IWARP_CONN_AG_CTX_CQ_ARM_SE_CF_SHIFT    2
#define E4_USTORM_IWARP_CONN_AG_CTX_CQ_ARM_CF_MASK        0x3 /* cf5 */
#define E4_USTORM_IWARP_CONN_AG_CTX_CQ_ARM_CF_SHIFT       4
#define E4_USTORM_IWARP_CONN_AG_CTX_CF6_MASK              0x3 /* cf6 */
#define E4_USTORM_IWARP_CONN_AG_CTX_CF6_SHIFT             6
	u8 flags2;
#define E4_USTORM_IWARP_CONN_AG_CTX_CF0EN_MASK            0x1 /* cf0en */
#define E4_USTORM_IWARP_CONN_AG_CTX_CF0EN_SHIFT           0
#define E4_USTORM_IWARP_CONN_AG_CTX_CF1EN_MASK            0x1 /* cf1en */
#define E4_USTORM_IWARP_CONN_AG_CTX_CF1EN_SHIFT           1
#define E4_USTORM_IWARP_CONN_AG_CTX_CF2EN_MASK            0x1 /* cf2en */
#define E4_USTORM_IWARP_CONN_AG_CTX_CF2EN_SHIFT           2
#define E4_USTORM_IWARP_CONN_AG_CTX_CF3EN_MASK            0x1 /* cf3en */
#define E4_USTORM_IWARP_CONN_AG_CTX_CF3EN_SHIFT           3
#define E4_USTORM_IWARP_CONN_AG_CTX_CQ_ARM_SE_CF_EN_MASK  0x1 /* cf4en */
#define E4_USTORM_IWARP_CONN_AG_CTX_CQ_ARM_SE_CF_EN_SHIFT 4
#define E4_USTORM_IWARP_CONN_AG_CTX_CQ_ARM_CF_EN_MASK     0x1 /* cf5en */
#define E4_USTORM_IWARP_CONN_AG_CTX_CQ_ARM_CF_EN_SHIFT    5
#define E4_USTORM_IWARP_CONN_AG_CTX_CF6EN_MASK            0x1 /* cf6en */
#define E4_USTORM_IWARP_CONN_AG_CTX_CF6EN_SHIFT           6
#define E4_USTORM_IWARP_CONN_AG_CTX_CQ_SE_EN_MASK         0x1 /* rule0en */
#define E4_USTORM_IWARP_CONN_AG_CTX_CQ_SE_EN_SHIFT        7
	u8 flags3;
#define E4_USTORM_IWARP_CONN_AG_CTX_CQ_EN_MASK            0x1 /* rule1en */
#define E4_USTORM_IWARP_CONN_AG_CTX_CQ_EN_SHIFT           0
#define E4_USTORM_IWARP_CONN_AG_CTX_RULE2EN_MASK          0x1 /* rule2en */
#define E4_USTORM_IWARP_CONN_AG_CTX_RULE2EN_SHIFT         1
#define E4_USTORM_IWARP_CONN_AG_CTX_RULE3EN_MASK          0x1 /* rule3en */
#define E4_USTORM_IWARP_CONN_AG_CTX_RULE3EN_SHIFT         2
#define E4_USTORM_IWARP_CONN_AG_CTX_RULE4EN_MASK          0x1 /* rule4en */
#define E4_USTORM_IWARP_CONN_AG_CTX_RULE4EN_SHIFT         3
#define E4_USTORM_IWARP_CONN_AG_CTX_RULE5EN_MASK          0x1 /* rule5en */
#define E4_USTORM_IWARP_CONN_AG_CTX_RULE5EN_SHIFT         4
#define E4_USTORM_IWARP_CONN_AG_CTX_RULE6EN_MASK          0x1 /* rule6en */
#define E4_USTORM_IWARP_CONN_AG_CTX_RULE6EN_SHIFT         5
#define E4_USTORM_IWARP_CONN_AG_CTX_RULE7EN_MASK          0x1 /* rule7en */
#define E4_USTORM_IWARP_CONN_AG_CTX_RULE7EN_SHIFT         6
#define E4_USTORM_IWARP_CONN_AG_CTX_RULE8EN_MASK          0x1 /* rule8en */
#define E4_USTORM_IWARP_CONN_AG_CTX_RULE8EN_SHIFT         7
	u8 byte2 /* byte2 */;
	u8 byte3 /* byte3 */;
	__le16 word0 /* conn_dpi */;
	__le16 word1 /* word1 */;
	__le32 cq_cons /* reg0 */;
	__le32 cq_se_prod /* reg1 */;
	__le32 cq_prod /* reg2 */;
	__le32 reg3 /* reg3 */;
	__le16 word2 /* word2 */;
	__le16 word3 /* word3 */;
};



struct e4_ystorm_iwarp_conn_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 byte1 /* state */;
	u8 flags0;
#define E4_YSTORM_IWARP_CONN_AG_CTX_BIT0_MASK     0x1 /* exist_in_qm0 */
#define E4_YSTORM_IWARP_CONN_AG_CTX_BIT0_SHIFT    0
#define E4_YSTORM_IWARP_CONN_AG_CTX_BIT1_MASK     0x1 /* exist_in_qm1 */
#define E4_YSTORM_IWARP_CONN_AG_CTX_BIT1_SHIFT    1
#define E4_YSTORM_IWARP_CONN_AG_CTX_CF0_MASK      0x3 /* cf0 */
#define E4_YSTORM_IWARP_CONN_AG_CTX_CF0_SHIFT     2
#define E4_YSTORM_IWARP_CONN_AG_CTX_CF1_MASK      0x3 /* cf1 */
#define E4_YSTORM_IWARP_CONN_AG_CTX_CF1_SHIFT     4
#define E4_YSTORM_IWARP_CONN_AG_CTX_CF2_MASK      0x3 /* cf2 */
#define E4_YSTORM_IWARP_CONN_AG_CTX_CF2_SHIFT     6
	u8 flags1;
#define E4_YSTORM_IWARP_CONN_AG_CTX_CF0EN_MASK    0x1 /* cf0en */
#define E4_YSTORM_IWARP_CONN_AG_CTX_CF0EN_SHIFT   0
#define E4_YSTORM_IWARP_CONN_AG_CTX_CF1EN_MASK    0x1 /* cf1en */
#define E4_YSTORM_IWARP_CONN_AG_CTX_CF1EN_SHIFT   1
#define E4_YSTORM_IWARP_CONN_AG_CTX_CF2EN_MASK    0x1 /* cf2en */
#define E4_YSTORM_IWARP_CONN_AG_CTX_CF2EN_SHIFT   2
#define E4_YSTORM_IWARP_CONN_AG_CTX_RULE0EN_MASK  0x1 /* rule0en */
#define E4_YSTORM_IWARP_CONN_AG_CTX_RULE0EN_SHIFT 3
#define E4_YSTORM_IWARP_CONN_AG_CTX_RULE1EN_MASK  0x1 /* rule1en */
#define E4_YSTORM_IWARP_CONN_AG_CTX_RULE1EN_SHIFT 4
#define E4_YSTORM_IWARP_CONN_AG_CTX_RULE2EN_MASK  0x1 /* rule2en */
#define E4_YSTORM_IWARP_CONN_AG_CTX_RULE2EN_SHIFT 5
#define E4_YSTORM_IWARP_CONN_AG_CTX_RULE3EN_MASK  0x1 /* rule3en */
#define E4_YSTORM_IWARP_CONN_AG_CTX_RULE3EN_SHIFT 6
#define E4_YSTORM_IWARP_CONN_AG_CTX_RULE4EN_MASK  0x1 /* rule4en */
#define E4_YSTORM_IWARP_CONN_AG_CTX_RULE4EN_SHIFT 7
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


struct e5_mstorm_iwarp_conn_ag_ctx
{
	u8 reserved /* cdu_validation */;
	u8 state_and_core_id /* state_and_core_id */;
	u8 flags0;
#define E5_MSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM0_MASK         0x1 /* exist_in_qm0 */
#define E5_MSTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM0_SHIFT        0
#define E5_MSTORM_IWARP_CONN_AG_CTX_BIT1_MASK                 0x1 /* exist_in_qm1 */
#define E5_MSTORM_IWARP_CONN_AG_CTX_BIT1_SHIFT                1
#define E5_MSTORM_IWARP_CONN_AG_CTX_INV_STAG_DONE_CF_MASK     0x3 /* cf0 */
#define E5_MSTORM_IWARP_CONN_AG_CTX_INV_STAG_DONE_CF_SHIFT    2
#define E5_MSTORM_IWARP_CONN_AG_CTX_CF1_MASK                  0x3 /* cf1 */
#define E5_MSTORM_IWARP_CONN_AG_CTX_CF1_SHIFT                 4
#define E5_MSTORM_IWARP_CONN_AG_CTX_CF2_MASK                  0x3 /* cf2 */
#define E5_MSTORM_IWARP_CONN_AG_CTX_CF2_SHIFT                 6
	u8 flags1;
#define E5_MSTORM_IWARP_CONN_AG_CTX_INV_STAG_DONE_CF_EN_MASK  0x1 /* cf0en */
#define E5_MSTORM_IWARP_CONN_AG_CTX_INV_STAG_DONE_CF_EN_SHIFT 0
#define E5_MSTORM_IWARP_CONN_AG_CTX_CF1EN_MASK                0x1 /* cf1en */
#define E5_MSTORM_IWARP_CONN_AG_CTX_CF1EN_SHIFT               1
#define E5_MSTORM_IWARP_CONN_AG_CTX_CF2EN_MASK                0x1 /* cf2en */
#define E5_MSTORM_IWARP_CONN_AG_CTX_CF2EN_SHIFT               2
#define E5_MSTORM_IWARP_CONN_AG_CTX_RULE0EN_MASK              0x1 /* rule0en */
#define E5_MSTORM_IWARP_CONN_AG_CTX_RULE0EN_SHIFT             3
#define E5_MSTORM_IWARP_CONN_AG_CTX_RULE1EN_MASK              0x1 /* rule1en */
#define E5_MSTORM_IWARP_CONN_AG_CTX_RULE1EN_SHIFT             4
#define E5_MSTORM_IWARP_CONN_AG_CTX_RULE2EN_MASK              0x1 /* rule2en */
#define E5_MSTORM_IWARP_CONN_AG_CTX_RULE2EN_SHIFT             5
#define E5_MSTORM_IWARP_CONN_AG_CTX_RCQ_CONS_EN_MASK          0x1 /* rule3en */
#define E5_MSTORM_IWARP_CONN_AG_CTX_RCQ_CONS_EN_SHIFT         6
#define E5_MSTORM_IWARP_CONN_AG_CTX_RULE4EN_MASK              0x1 /* rule4en */
#define E5_MSTORM_IWARP_CONN_AG_CTX_RULE4EN_SHIFT             7
	__le16 rcq_cons /* word0 */;
	__le16 rcq_cons_th /* word1 */;
	__le32 reg0 /* reg0 */;
	__le32 reg1 /* reg1 */;
};



struct e5_ustorm_iwarp_conn_ag_ctx
{
	u8 reserved /* cdu_validation */;
	u8 byte1 /* state_and_core_id */;
	u8 flags0;
#define E5_USTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM0_MASK     0x1 /* exist_in_qm0 */
#define E5_USTORM_IWARP_CONN_AG_CTX_EXIST_IN_QM0_SHIFT    0
#define E5_USTORM_IWARP_CONN_AG_CTX_BIT1_MASK             0x1 /* exist_in_qm1 */
#define E5_USTORM_IWARP_CONN_AG_CTX_BIT1_SHIFT            1
#define E5_USTORM_IWARP_CONN_AG_CTX_CF0_MASK              0x3 /* timer0cf */
#define E5_USTORM_IWARP_CONN_AG_CTX_CF0_SHIFT             2
#define E5_USTORM_IWARP_CONN_AG_CTX_CF1_MASK              0x3 /* timer1cf */
#define E5_USTORM_IWARP_CONN_AG_CTX_CF1_SHIFT             4
#define E5_USTORM_IWARP_CONN_AG_CTX_CF2_MASK              0x3 /* timer2cf */
#define E5_USTORM_IWARP_CONN_AG_CTX_CF2_SHIFT             6
	u8 flags1;
#define E5_USTORM_IWARP_CONN_AG_CTX_CF3_MASK              0x3 /* timer_stop_all */
#define E5_USTORM_IWARP_CONN_AG_CTX_CF3_SHIFT             0
#define E5_USTORM_IWARP_CONN_AG_CTX_CQ_ARM_SE_CF_MASK     0x3 /* cf4 */
#define E5_USTORM_IWARP_CONN_AG_CTX_CQ_ARM_SE_CF_SHIFT    2
#define E5_USTORM_IWARP_CONN_AG_CTX_CQ_ARM_CF_MASK        0x3 /* cf5 */
#define E5_USTORM_IWARP_CONN_AG_CTX_CQ_ARM_CF_SHIFT       4
#define E5_USTORM_IWARP_CONN_AG_CTX_CF6_MASK              0x3 /* cf6 */
#define E5_USTORM_IWARP_CONN_AG_CTX_CF6_SHIFT             6
	u8 flags2;
#define E5_USTORM_IWARP_CONN_AG_CTX_CF0EN_MASK            0x1 /* cf0en */
#define E5_USTORM_IWARP_CONN_AG_CTX_CF0EN_SHIFT           0
#define E5_USTORM_IWARP_CONN_AG_CTX_CF1EN_MASK            0x1 /* cf1en */
#define E5_USTORM_IWARP_CONN_AG_CTX_CF1EN_SHIFT           1
#define E5_USTORM_IWARP_CONN_AG_CTX_CF2EN_MASK            0x1 /* cf2en */
#define E5_USTORM_IWARP_CONN_AG_CTX_CF2EN_SHIFT           2
#define E5_USTORM_IWARP_CONN_AG_CTX_CF3EN_MASK            0x1 /* cf3en */
#define E5_USTORM_IWARP_CONN_AG_CTX_CF3EN_SHIFT           3
#define E5_USTORM_IWARP_CONN_AG_CTX_CQ_ARM_SE_CF_EN_MASK  0x1 /* cf4en */
#define E5_USTORM_IWARP_CONN_AG_CTX_CQ_ARM_SE_CF_EN_SHIFT 4
#define E5_USTORM_IWARP_CONN_AG_CTX_CQ_ARM_CF_EN_MASK     0x1 /* cf5en */
#define E5_USTORM_IWARP_CONN_AG_CTX_CQ_ARM_CF_EN_SHIFT    5
#define E5_USTORM_IWARP_CONN_AG_CTX_CF6EN_MASK            0x1 /* cf6en */
#define E5_USTORM_IWARP_CONN_AG_CTX_CF6EN_SHIFT           6
#define E5_USTORM_IWARP_CONN_AG_CTX_CQ_SE_EN_MASK         0x1 /* rule0en */
#define E5_USTORM_IWARP_CONN_AG_CTX_CQ_SE_EN_SHIFT        7
	u8 flags3;
#define E5_USTORM_IWARP_CONN_AG_CTX_CQ_EN_MASK            0x1 /* rule1en */
#define E5_USTORM_IWARP_CONN_AG_CTX_CQ_EN_SHIFT           0
#define E5_USTORM_IWARP_CONN_AG_CTX_RULE2EN_MASK          0x1 /* rule2en */
#define E5_USTORM_IWARP_CONN_AG_CTX_RULE2EN_SHIFT         1
#define E5_USTORM_IWARP_CONN_AG_CTX_RULE3EN_MASK          0x1 /* rule3en */
#define E5_USTORM_IWARP_CONN_AG_CTX_RULE3EN_SHIFT         2
#define E5_USTORM_IWARP_CONN_AG_CTX_RULE4EN_MASK          0x1 /* rule4en */
#define E5_USTORM_IWARP_CONN_AG_CTX_RULE4EN_SHIFT         3
#define E5_USTORM_IWARP_CONN_AG_CTX_RULE5EN_MASK          0x1 /* rule5en */
#define E5_USTORM_IWARP_CONN_AG_CTX_RULE5EN_SHIFT         4
#define E5_USTORM_IWARP_CONN_AG_CTX_RULE6EN_MASK          0x1 /* rule6en */
#define E5_USTORM_IWARP_CONN_AG_CTX_RULE6EN_SHIFT         5
#define E5_USTORM_IWARP_CONN_AG_CTX_RULE7EN_MASK          0x1 /* rule7en */
#define E5_USTORM_IWARP_CONN_AG_CTX_RULE7EN_SHIFT         6
#define E5_USTORM_IWARP_CONN_AG_CTX_RULE8EN_MASK          0x1 /* rule8en */
#define E5_USTORM_IWARP_CONN_AG_CTX_RULE8EN_SHIFT         7
	u8 flags4;
#define E5_USTORM_IWARP_CONN_AG_CTX_E4_RESERVED1_MASK     0x1 /* bit2 */
#define E5_USTORM_IWARP_CONN_AG_CTX_E4_RESERVED1_SHIFT    0
#define E5_USTORM_IWARP_CONN_AG_CTX_E4_RESERVED2_MASK     0x1 /* bit3 */
#define E5_USTORM_IWARP_CONN_AG_CTX_E4_RESERVED2_SHIFT    1
#define E5_USTORM_IWARP_CONN_AG_CTX_E4_RESERVED3_MASK     0x3 /* cf7 */
#define E5_USTORM_IWARP_CONN_AG_CTX_E4_RESERVED3_SHIFT    2
#define E5_USTORM_IWARP_CONN_AG_CTX_E4_RESERVED4_MASK     0x3 /* cf8 */
#define E5_USTORM_IWARP_CONN_AG_CTX_E4_RESERVED4_SHIFT    4
#define E5_USTORM_IWARP_CONN_AG_CTX_E4_RESERVED5_MASK     0x1 /* cf7en */
#define E5_USTORM_IWARP_CONN_AG_CTX_E4_RESERVED5_SHIFT    6
#define E5_USTORM_IWARP_CONN_AG_CTX_E4_RESERVED6_MASK     0x1 /* cf8en */
#define E5_USTORM_IWARP_CONN_AG_CTX_E4_RESERVED6_SHIFT    7
	u8 byte2 /* byte2 */;
	__le16 word0 /* conn_dpi */;
	__le16 word1 /* word1 */;
	__le32 cq_cons /* reg0 */;
	__le32 cq_se_prod /* reg1 */;
	__le32 cq_prod /* reg2 */;
	__le32 reg3 /* reg3 */;
	__le16 word2 /* word2 */;
	__le16 word3 /* word3 */;
};



struct e5_ystorm_iwarp_conn_ag_ctx
{
	u8 byte0 /* cdu_validation */;
	u8 byte1 /* state_and_core_id */;
	u8 flags0;
#define E5_YSTORM_IWARP_CONN_AG_CTX_BIT0_MASK     0x1 /* exist_in_qm0 */
#define E5_YSTORM_IWARP_CONN_AG_CTX_BIT0_SHIFT    0
#define E5_YSTORM_IWARP_CONN_AG_CTX_BIT1_MASK     0x1 /* exist_in_qm1 */
#define E5_YSTORM_IWARP_CONN_AG_CTX_BIT1_SHIFT    1
#define E5_YSTORM_IWARP_CONN_AG_CTX_CF0_MASK      0x3 /* cf0 */
#define E5_YSTORM_IWARP_CONN_AG_CTX_CF0_SHIFT     2
#define E5_YSTORM_IWARP_CONN_AG_CTX_CF1_MASK      0x3 /* cf1 */
#define E5_YSTORM_IWARP_CONN_AG_CTX_CF1_SHIFT     4
#define E5_YSTORM_IWARP_CONN_AG_CTX_CF2_MASK      0x3 /* cf2 */
#define E5_YSTORM_IWARP_CONN_AG_CTX_CF2_SHIFT     6
	u8 flags1;
#define E5_YSTORM_IWARP_CONN_AG_CTX_CF0EN_MASK    0x1 /* cf0en */
#define E5_YSTORM_IWARP_CONN_AG_CTX_CF0EN_SHIFT   0
#define E5_YSTORM_IWARP_CONN_AG_CTX_CF1EN_MASK    0x1 /* cf1en */
#define E5_YSTORM_IWARP_CONN_AG_CTX_CF1EN_SHIFT   1
#define E5_YSTORM_IWARP_CONN_AG_CTX_CF2EN_MASK    0x1 /* cf2en */
#define E5_YSTORM_IWARP_CONN_AG_CTX_CF2EN_SHIFT   2
#define E5_YSTORM_IWARP_CONN_AG_CTX_RULE0EN_MASK  0x1 /* rule0en */
#define E5_YSTORM_IWARP_CONN_AG_CTX_RULE0EN_SHIFT 3
#define E5_YSTORM_IWARP_CONN_AG_CTX_RULE1EN_MASK  0x1 /* rule1en */
#define E5_YSTORM_IWARP_CONN_AG_CTX_RULE1EN_SHIFT 4
#define E5_YSTORM_IWARP_CONN_AG_CTX_RULE2EN_MASK  0x1 /* rule2en */
#define E5_YSTORM_IWARP_CONN_AG_CTX_RULE2EN_SHIFT 5
#define E5_YSTORM_IWARP_CONN_AG_CTX_RULE3EN_MASK  0x1 /* rule3en */
#define E5_YSTORM_IWARP_CONN_AG_CTX_RULE3EN_SHIFT 6
#define E5_YSTORM_IWARP_CONN_AG_CTX_RULE4EN_MASK  0x1 /* rule4en */
#define E5_YSTORM_IWARP_CONN_AG_CTX_RULE4EN_SHIFT 7
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

#endif /* __ECORE_HSI_IWARP__ */
