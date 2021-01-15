/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/*
 * Microsemi Ocelot Switch driver
 *
 * Copyright (c) 2017 Microsemi Corporation
 */

#ifndef _MSCC_OCELOT_QSYS_H_
#define _MSCC_OCELOT_QSYS_H_

#define QSYS_PORT_MODE_RSZ                                0x4

#define QSYS_PORT_MODE_DEQUEUE_DIS                        BIT(1)
#define QSYS_PORT_MODE_DEQUEUE_LATE                       BIT(0)

#define QSYS_STAT_CNT_CFG_TX_GREEN_CNT_MODE               BIT(5)
#define QSYS_STAT_CNT_CFG_TX_YELLOW_CNT_MODE              BIT(4)
#define QSYS_STAT_CNT_CFG_DROP_GREEN_CNT_MODE             BIT(3)
#define QSYS_STAT_CNT_CFG_DROP_YELLOW_CNT_MODE            BIT(2)
#define QSYS_STAT_CNT_CFG_DROP_COUNT_ONCE                 BIT(1)
#define QSYS_STAT_CNT_CFG_DROP_COUNT_EGRESS               BIT(0)

#define QSYS_EEE_CFG_RSZ                                  0x4

#define QSYS_EEE_THRES_EEE_HIGH_BYTES(x)                  (((x) << 8) & GENMASK(15, 8))
#define QSYS_EEE_THRES_EEE_HIGH_BYTES_M                   GENMASK(15, 8)
#define QSYS_EEE_THRES_EEE_HIGH_BYTES_X(x)                (((x) & GENMASK(15, 8)) >> 8)
#define QSYS_EEE_THRES_EEE_HIGH_FRAMES(x)                 ((x) & GENMASK(7, 0))
#define QSYS_EEE_THRES_EEE_HIGH_FRAMES_M                  GENMASK(7, 0)

#define QSYS_SW_STATUS_RSZ                                0x4

#define QSYS_EXT_CPU_CFG_EXT_CPU_PORT(x)                  (((x) << 8) & GENMASK(12, 8))
#define QSYS_EXT_CPU_CFG_EXT_CPU_PORT_M                   GENMASK(12, 8)
#define QSYS_EXT_CPU_CFG_EXT_CPU_PORT_X(x)                (((x) & GENMASK(12, 8)) >> 8)
#define QSYS_EXT_CPU_CFG_EXT_CPUQ_MSK(x)                  ((x) & GENMASK(7, 0))
#define QSYS_EXT_CPU_CFG_EXT_CPUQ_MSK_M                   GENMASK(7, 0)

#define QSYS_QMAP_GSZ                                     0x4

#define QSYS_QMAP_SE_BASE(x)                              (((x) << 5) & GENMASK(12, 5))
#define QSYS_QMAP_SE_BASE_M                               GENMASK(12, 5)
#define QSYS_QMAP_SE_BASE_X(x)                            (((x) & GENMASK(12, 5)) >> 5)
#define QSYS_QMAP_SE_IDX_SEL(x)                           (((x) << 2) & GENMASK(4, 2))
#define QSYS_QMAP_SE_IDX_SEL_M                            GENMASK(4, 2)
#define QSYS_QMAP_SE_IDX_SEL_X(x)                         (((x) & GENMASK(4, 2)) >> 2)
#define QSYS_QMAP_SE_INP_SEL(x)                           ((x) & GENMASK(1, 0))
#define QSYS_QMAP_SE_INP_SEL_M                            GENMASK(1, 0)

#define QSYS_ISDX_SGRP_GSZ                                0x4

#define QSYS_TIMED_FRAME_ENTRY_GSZ                        0x4

#define QSYS_TFRM_MISC_TIMED_CANCEL_SLOT(x)               (((x) << 9) & GENMASK(18, 9))
#define QSYS_TFRM_MISC_TIMED_CANCEL_SLOT_M                GENMASK(18, 9)
#define QSYS_TFRM_MISC_TIMED_CANCEL_SLOT_X(x)             (((x) & GENMASK(18, 9)) >> 9)
#define QSYS_TFRM_MISC_TIMED_CANCEL_1SHOT                 BIT(8)
#define QSYS_TFRM_MISC_TIMED_SLOT_MODE_MC                 BIT(7)
#define QSYS_TFRM_MISC_TIMED_ENTRY_FAST_CNT(x)            ((x) & GENMASK(6, 0))
#define QSYS_TFRM_MISC_TIMED_ENTRY_FAST_CNT_M             GENMASK(6, 0)

#define QSYS_RED_PROFILE_RSZ                              0x4

#define QSYS_RED_PROFILE_WM_RED_LOW(x)                    (((x) << 8) & GENMASK(15, 8))
#define QSYS_RED_PROFILE_WM_RED_LOW_M                     GENMASK(15, 8)
#define QSYS_RED_PROFILE_WM_RED_LOW_X(x)                  (((x) & GENMASK(15, 8)) >> 8)
#define QSYS_RED_PROFILE_WM_RED_HIGH(x)                   ((x) & GENMASK(7, 0))
#define QSYS_RED_PROFILE_WM_RED_HIGH_M                    GENMASK(7, 0)

#define QSYS_RES_CFG_GSZ                                  0x8

#define QSYS_RES_STAT_GSZ                                 0x8

#define QSYS_RES_STAT_INUSE(x)                            (((x) << 12) & GENMASK(23, 12))
#define QSYS_RES_STAT_INUSE_M                             GENMASK(23, 12)
#define QSYS_RES_STAT_INUSE_X(x)                          (((x) & GENMASK(23, 12)) >> 12)
#define QSYS_RES_STAT_MAXUSE(x)                           ((x) & GENMASK(11, 0))
#define QSYS_RES_STAT_MAXUSE_M                            GENMASK(11, 0)

#define QSYS_MMGT_EQ_CTRL_FP_FREE_CNT(x)                  ((x) & GENMASK(15, 0))
#define QSYS_MMGT_EQ_CTRL_FP_FREE_CNT_M                   GENMASK(15, 0)

#define QSYS_EVENTS_CORE_EV_FDC(x)                        (((x) << 2) & GENMASK(4, 2))
#define QSYS_EVENTS_CORE_EV_FDC_M                         GENMASK(4, 2)
#define QSYS_EVENTS_CORE_EV_FDC_X(x)                      (((x) & GENMASK(4, 2)) >> 2)
#define QSYS_EVENTS_CORE_EV_FRD(x)                        ((x) & GENMASK(1, 0))
#define QSYS_EVENTS_CORE_EV_FRD_M                         GENMASK(1, 0)

#define QSYS_QMAXSDU_CFG_0_RSZ                            0x4

#define QSYS_QMAXSDU_CFG_1_RSZ                            0x4

#define QSYS_QMAXSDU_CFG_2_RSZ                            0x4

#define QSYS_QMAXSDU_CFG_3_RSZ                            0x4

#define QSYS_QMAXSDU_CFG_4_RSZ                            0x4

#define QSYS_QMAXSDU_CFG_5_RSZ                            0x4

#define QSYS_QMAXSDU_CFG_6_RSZ                            0x4

#define QSYS_QMAXSDU_CFG_7_RSZ                            0x4

#define QSYS_PREEMPTION_CFG_RSZ                           0x4

#define QSYS_PREEMPTION_CFG_P_QUEUES(x)                   ((x) & GENMASK(7, 0))
#define QSYS_PREEMPTION_CFG_P_QUEUES_M                    GENMASK(7, 0)
#define QSYS_PREEMPTION_CFG_MM_ADD_FRAG_SIZE(x)           (((x) << 8) & GENMASK(9, 8))
#define QSYS_PREEMPTION_CFG_MM_ADD_FRAG_SIZE_M            GENMASK(9, 8)
#define QSYS_PREEMPTION_CFG_MM_ADD_FRAG_SIZE_X(x)         (((x) & GENMASK(9, 8)) >> 8)
#define QSYS_PREEMPTION_CFG_STRICT_IPG(x)                 (((x) << 12) & GENMASK(13, 12))
#define QSYS_PREEMPTION_CFG_STRICT_IPG_M                  GENMASK(13, 12)
#define QSYS_PREEMPTION_CFG_STRICT_IPG_X(x)               (((x) & GENMASK(13, 12)) >> 12)
#define QSYS_PREEMPTION_CFG_HOLD_ADVANCE(x)               (((x) << 16) & GENMASK(31, 16))
#define QSYS_PREEMPTION_CFG_HOLD_ADVANCE_M                GENMASK(31, 16)
#define QSYS_PREEMPTION_CFG_HOLD_ADVANCE_X(x)             (((x) & GENMASK(31, 16)) >> 16)

#define QSYS_CIR_CFG_GSZ                                  0x80

#define QSYS_CIR_CFG_CIR_RATE(x)                          (((x) << 6) & GENMASK(20, 6))
#define QSYS_CIR_CFG_CIR_RATE_M                           GENMASK(20, 6)
#define QSYS_CIR_CFG_CIR_RATE_X(x)                        (((x) & GENMASK(20, 6)) >> 6)
#define QSYS_CIR_CFG_CIR_BURST(x)                         ((x) & GENMASK(5, 0))
#define QSYS_CIR_CFG_CIR_BURST_M                          GENMASK(5, 0)

#define QSYS_EIR_CFG_GSZ                                  0x80

#define QSYS_EIR_CFG_EIR_RATE(x)                          (((x) << 7) & GENMASK(21, 7))
#define QSYS_EIR_CFG_EIR_RATE_M                           GENMASK(21, 7)
#define QSYS_EIR_CFG_EIR_RATE_X(x)                        (((x) & GENMASK(21, 7)) >> 7)
#define QSYS_EIR_CFG_EIR_BURST(x)                         (((x) << 1) & GENMASK(6, 1))
#define QSYS_EIR_CFG_EIR_BURST_M                          GENMASK(6, 1)
#define QSYS_EIR_CFG_EIR_BURST_X(x)                       (((x) & GENMASK(6, 1)) >> 1)
#define QSYS_EIR_CFG_EIR_MARK_ENA                         BIT(0)

#define QSYS_SE_CFG_GSZ                                   0x80

#define QSYS_SE_CFG_SE_DWRR_CNT(x)                        (((x) << 6) & GENMASK(9, 6))
#define QSYS_SE_CFG_SE_DWRR_CNT_M                         GENMASK(9, 6)
#define QSYS_SE_CFG_SE_DWRR_CNT_X(x)                      (((x) & GENMASK(9, 6)) >> 6)
#define QSYS_SE_CFG_SE_RR_ENA                             BIT(5)
#define QSYS_SE_CFG_SE_AVB_ENA                            BIT(4)
#define QSYS_SE_CFG_SE_FRM_MODE(x)                        (((x) << 2) & GENMASK(3, 2))
#define QSYS_SE_CFG_SE_FRM_MODE_M                         GENMASK(3, 2)
#define QSYS_SE_CFG_SE_FRM_MODE_X(x)                      (((x) & GENMASK(3, 2)) >> 2)
#define QSYS_SE_CFG_SE_EXC_ENA                            BIT(1)
#define QSYS_SE_CFG_SE_EXC_FWD                            BIT(0)

#define QSYS_SE_DWRR_CFG_GSZ                              0x80
#define QSYS_SE_DWRR_CFG_RSZ                              0x4

#define QSYS_SE_CONNECT_GSZ                               0x80

#define QSYS_SE_CONNECT_SE_OUTP_IDX(x)                    (((x) << 17) & GENMASK(24, 17))
#define QSYS_SE_CONNECT_SE_OUTP_IDX_M                     GENMASK(24, 17)
#define QSYS_SE_CONNECT_SE_OUTP_IDX_X(x)                  (((x) & GENMASK(24, 17)) >> 17)
#define QSYS_SE_CONNECT_SE_INP_IDX(x)                     (((x) << 9) & GENMASK(16, 9))
#define QSYS_SE_CONNECT_SE_INP_IDX_M                      GENMASK(16, 9)
#define QSYS_SE_CONNECT_SE_INP_IDX_X(x)                   (((x) & GENMASK(16, 9)) >> 9)
#define QSYS_SE_CONNECT_SE_OUTP_CON(x)                    (((x) << 5) & GENMASK(8, 5))
#define QSYS_SE_CONNECT_SE_OUTP_CON_M                     GENMASK(8, 5)
#define QSYS_SE_CONNECT_SE_OUTP_CON_X(x)                  (((x) & GENMASK(8, 5)) >> 5)
#define QSYS_SE_CONNECT_SE_INP_CNT(x)                     (((x) << 1) & GENMASK(4, 1))
#define QSYS_SE_CONNECT_SE_INP_CNT_M                      GENMASK(4, 1)
#define QSYS_SE_CONNECT_SE_INP_CNT_X(x)                   (((x) & GENMASK(4, 1)) >> 1)
#define QSYS_SE_CONNECT_SE_TERMINAL                       BIT(0)

#define QSYS_SE_DLB_SENSE_GSZ                             0x80

#define QSYS_SE_DLB_SENSE_SE_DLB_PRIO(x)                  (((x) << 11) & GENMASK(13, 11))
#define QSYS_SE_DLB_SENSE_SE_DLB_PRIO_M                   GENMASK(13, 11)
#define QSYS_SE_DLB_SENSE_SE_DLB_PRIO_X(x)                (((x) & GENMASK(13, 11)) >> 11)
#define QSYS_SE_DLB_SENSE_SE_DLB_SPORT(x)                 (((x) << 7) & GENMASK(10, 7))
#define QSYS_SE_DLB_SENSE_SE_DLB_SPORT_M                  GENMASK(10, 7)
#define QSYS_SE_DLB_SENSE_SE_DLB_SPORT_X(x)               (((x) & GENMASK(10, 7)) >> 7)
#define QSYS_SE_DLB_SENSE_SE_DLB_DPORT(x)                 (((x) << 3) & GENMASK(6, 3))
#define QSYS_SE_DLB_SENSE_SE_DLB_DPORT_M                  GENMASK(6, 3)
#define QSYS_SE_DLB_SENSE_SE_DLB_DPORT_X(x)               (((x) & GENMASK(6, 3)) >> 3)
#define QSYS_SE_DLB_SENSE_SE_DLB_PRIO_ENA                 BIT(2)
#define QSYS_SE_DLB_SENSE_SE_DLB_SPORT_ENA                BIT(1)
#define QSYS_SE_DLB_SENSE_SE_DLB_DPORT_ENA                BIT(0)

#define QSYS_CIR_STATE_GSZ                                0x80

#define QSYS_CIR_STATE_CIR_LVL(x)                         (((x) << 4) & GENMASK(25, 4))
#define QSYS_CIR_STATE_CIR_LVL_M                          GENMASK(25, 4)
#define QSYS_CIR_STATE_CIR_LVL_X(x)                       (((x) & GENMASK(25, 4)) >> 4)
#define QSYS_CIR_STATE_SHP_TIME(x)                        ((x) & GENMASK(3, 0))
#define QSYS_CIR_STATE_SHP_TIME_M                         GENMASK(3, 0)

#define QSYS_EIR_STATE_GSZ                                0x80

#define QSYS_SE_STATE_GSZ                                 0x80

#define QSYS_SE_STATE_SE_OUTP_LVL(x)                      (((x) << 1) & GENMASK(2, 1))
#define QSYS_SE_STATE_SE_OUTP_LVL_M                       GENMASK(2, 1)
#define QSYS_SE_STATE_SE_OUTP_LVL_X(x)                    (((x) & GENMASK(2, 1)) >> 1)
#define QSYS_SE_STATE_SE_WAS_YEL                          BIT(0)

#define QSYS_HSCH_MISC_CFG_SE_CONNECT_VLD                 BIT(8)
#define QSYS_HSCH_MISC_CFG_FRM_ADJ(x)                     (((x) << 3) & GENMASK(7, 3))
#define QSYS_HSCH_MISC_CFG_FRM_ADJ_M                      GENMASK(7, 3)
#define QSYS_HSCH_MISC_CFG_FRM_ADJ_X(x)                   (((x) & GENMASK(7, 3)) >> 3)
#define QSYS_HSCH_MISC_CFG_LEAK_DIS                       BIT(2)
#define QSYS_HSCH_MISC_CFG_QSHP_EXC_ENA                   BIT(1)
#define QSYS_HSCH_MISC_CFG_PFC_BYP_UPD                    BIT(0)

#define QSYS_TAG_CONFIG_RSZ                               0x4

#define QSYS_TAG_CONFIG_ENABLE                            BIT(0)
#define QSYS_TAG_CONFIG_LINK_SPEED(x)                     (((x) << 4) & GENMASK(5, 4))
#define QSYS_TAG_CONFIG_LINK_SPEED_M                      GENMASK(5, 4)
#define QSYS_TAG_CONFIG_LINK_SPEED_X(x)                   (((x) & GENMASK(5, 4)) >> 4)
#define QSYS_TAG_CONFIG_INIT_GATE_STATE(x)                (((x) << 8) & GENMASK(15, 8))
#define QSYS_TAG_CONFIG_INIT_GATE_STATE_M                 GENMASK(15, 8)
#define QSYS_TAG_CONFIG_INIT_GATE_STATE_X(x)              (((x) & GENMASK(15, 8)) >> 8)
#define QSYS_TAG_CONFIG_SCH_TRAFFIC_QUEUES(x)             (((x) << 16) & GENMASK(23, 16))
#define QSYS_TAG_CONFIG_SCH_TRAFFIC_QUEUES_M              GENMASK(23, 16)
#define QSYS_TAG_CONFIG_SCH_TRAFFIC_QUEUES_X(x)           (((x) & GENMASK(23, 16)) >> 16)

#define QSYS_TAS_PARAM_CFG_CTRL_PORT_NUM(x)               ((x) & GENMASK(7, 0))
#define QSYS_TAS_PARAM_CFG_CTRL_PORT_NUM_M                GENMASK(7, 0)
#define QSYS_TAS_PARAM_CFG_CTRL_ALWAYS_GUARD_BAND_SCH_Q   BIT(8)
#define QSYS_TAS_PARAM_CFG_CTRL_CONFIG_CHANGE             BIT(16)

#define QSYS_PORT_MAX_SDU_RSZ                             0x4

#define QSYS_PARAM_CFG_REG_3_BASE_TIME_SEC_MSB(x)         ((x) & GENMASK(15, 0))
#define QSYS_PARAM_CFG_REG_3_BASE_TIME_SEC_MSB_M          GENMASK(15, 0)
#define QSYS_PARAM_CFG_REG_3_LIST_LENGTH(x)               (((x) << 16) & GENMASK(31, 16))
#define QSYS_PARAM_CFG_REG_3_LIST_LENGTH_M                GENMASK(31, 16)
#define QSYS_PARAM_CFG_REG_3_LIST_LENGTH_X(x)             (((x) & GENMASK(31, 16)) >> 16)

#define QSYS_GCL_CFG_REG_1_GCL_ENTRY_NUM(x)               ((x) & GENMASK(5, 0))
#define QSYS_GCL_CFG_REG_1_GCL_ENTRY_NUM_M                GENMASK(5, 0)
#define QSYS_GCL_CFG_REG_1_GATE_STATE(x)                  (((x) << 8) & GENMASK(15, 8))
#define QSYS_GCL_CFG_REG_1_GATE_STATE_M                   GENMASK(15, 8)
#define QSYS_GCL_CFG_REG_1_GATE_STATE_X(x)                (((x) & GENMASK(15, 8)) >> 8)

#define QSYS_PARAM_STATUS_REG_3_BASE_TIME_SEC_MSB(x)      ((x) & GENMASK(15, 0))
#define QSYS_PARAM_STATUS_REG_3_BASE_TIME_SEC_MSB_M       GENMASK(15, 0)
#define QSYS_PARAM_STATUS_REG_3_LIST_LENGTH(x)            (((x) << 16) & GENMASK(31, 16))
#define QSYS_PARAM_STATUS_REG_3_LIST_LENGTH_M             GENMASK(31, 16)
#define QSYS_PARAM_STATUS_REG_3_LIST_LENGTH_X(x)          (((x) & GENMASK(31, 16)) >> 16)

#define QSYS_PARAM_STATUS_REG_8_CFG_CHG_TIME_SEC_MSB(x)   ((x) & GENMASK(15, 0))
#define QSYS_PARAM_STATUS_REG_8_CFG_CHG_TIME_SEC_MSB_M    GENMASK(15, 0)
#define QSYS_PARAM_STATUS_REG_8_OPER_GATE_STATE(x)        (((x) << 16) & GENMASK(23, 16))
#define QSYS_PARAM_STATUS_REG_8_OPER_GATE_STATE_M         GENMASK(23, 16)
#define QSYS_PARAM_STATUS_REG_8_OPER_GATE_STATE_X(x)      (((x) & GENMASK(23, 16)) >> 16)
#define QSYS_PARAM_STATUS_REG_8_CONFIG_PENDING            BIT(24)

#define QSYS_GCL_STATUS_REG_1_GCL_ENTRY_NUM(x)            ((x) & GENMASK(5, 0))
#define QSYS_GCL_STATUS_REG_1_GCL_ENTRY_NUM_M             GENMASK(5, 0)
#define QSYS_GCL_STATUS_REG_1_GATE_STATE(x)               (((x) << 8) & GENMASK(15, 8))
#define QSYS_GCL_STATUS_REG_1_GATE_STATE_M                GENMASK(15, 8)
#define QSYS_GCL_STATUS_REG_1_GATE_STATE_X(x)             (((x) & GENMASK(15, 8)) >> 8)

#endif
