/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/*
 * Microsemi Ocelot Switch driver
 *
 * Copyright (c) 2017 Microsemi Corporation
 */

#ifndef _MSCC_OCELOT_DEV_H_
#define _MSCC_OCELOT_DEV_H_

#define DEV_CLOCK_CFG                                     0x0

#define DEV_CLOCK_CFG_MAC_TX_RST                          BIT(7)
#define DEV_CLOCK_CFG_MAC_RX_RST                          BIT(6)
#define DEV_CLOCK_CFG_PCS_TX_RST                          BIT(5)
#define DEV_CLOCK_CFG_PCS_RX_RST                          BIT(4)
#define DEV_CLOCK_CFG_PORT_RST                            BIT(3)
#define DEV_CLOCK_CFG_PHY_RST                             BIT(2)
#define DEV_CLOCK_CFG_LINK_SPEED(x)                       ((x) & GENMASK(1, 0))
#define DEV_CLOCK_CFG_LINK_SPEED_M                        GENMASK(1, 0)

#define DEV_PORT_MISC                                     0x4

#define DEV_PORT_MISC_FWD_ERROR_ENA                       BIT(4)
#define DEV_PORT_MISC_FWD_PAUSE_ENA                       BIT(3)
#define DEV_PORT_MISC_FWD_CTRL_ENA                        BIT(2)
#define DEV_PORT_MISC_DEV_LOOP_ENA                        BIT(1)
#define DEV_PORT_MISC_HDX_FAST_DIS                        BIT(0)

#define DEV_EVENTS                                        0x8

#define DEV_EEE_CFG                                       0xc

#define DEV_EEE_CFG_EEE_ENA                               BIT(22)
#define DEV_EEE_CFG_EEE_TIMER_AGE(x)                      (((x) << 15) & GENMASK(21, 15))
#define DEV_EEE_CFG_EEE_TIMER_AGE_M                       GENMASK(21, 15)
#define DEV_EEE_CFG_EEE_TIMER_AGE_X(x)                    (((x) & GENMASK(21, 15)) >> 15)
#define DEV_EEE_CFG_EEE_TIMER_WAKEUP(x)                   (((x) << 8) & GENMASK(14, 8))
#define DEV_EEE_CFG_EEE_TIMER_WAKEUP_M                    GENMASK(14, 8)
#define DEV_EEE_CFG_EEE_TIMER_WAKEUP_X(x)                 (((x) & GENMASK(14, 8)) >> 8)
#define DEV_EEE_CFG_EEE_TIMER_HOLDOFF(x)                  (((x) << 1) & GENMASK(7, 1))
#define DEV_EEE_CFG_EEE_TIMER_HOLDOFF_M                   GENMASK(7, 1)
#define DEV_EEE_CFG_EEE_TIMER_HOLDOFF_X(x)                (((x) & GENMASK(7, 1)) >> 1)
#define DEV_EEE_CFG_PORT_LPI                              BIT(0)

#define DEV_RX_PATH_DELAY                                 0x10

#define DEV_TX_PATH_DELAY                                 0x14

#define DEV_PTP_PREDICT_CFG                               0x18

#define DEV_PTP_PREDICT_CFG_PTP_PHY_PREDICT_CFG(x)        (((x) << 4) & GENMASK(11, 4))
#define DEV_PTP_PREDICT_CFG_PTP_PHY_PREDICT_CFG_M         GENMASK(11, 4)
#define DEV_PTP_PREDICT_CFG_PTP_PHY_PREDICT_CFG_X(x)      (((x) & GENMASK(11, 4)) >> 4)
#define DEV_PTP_PREDICT_CFG_PTP_PHASE_PREDICT_CFG(x)      ((x) & GENMASK(3, 0))
#define DEV_PTP_PREDICT_CFG_PTP_PHASE_PREDICT_CFG_M       GENMASK(3, 0)

#define DEV_MAC_ENA_CFG                                   0x1c

#define DEV_MAC_ENA_CFG_RX_ENA                            BIT(4)
#define DEV_MAC_ENA_CFG_TX_ENA                            BIT(0)

#define DEV_MAC_MODE_CFG                                  0x20

#define DEV_MAC_MODE_CFG_FC_WORD_SYNC_ENA                 BIT(8)
#define DEV_MAC_MODE_CFG_GIGA_MODE_ENA                    BIT(4)
#define DEV_MAC_MODE_CFG_FDX_ENA                          BIT(0)

#define DEV_MAC_MAXLEN_CFG                                0x24

#define DEV_MAC_TAGS_CFG                                  0x28

#define DEV_MAC_TAGS_CFG_TAG_ID(x)                        (((x) << 16) & GENMASK(31, 16))
#define DEV_MAC_TAGS_CFG_TAG_ID_M                         GENMASK(31, 16)
#define DEV_MAC_TAGS_CFG_TAG_ID_X(x)                      (((x) & GENMASK(31, 16)) >> 16)
#define DEV_MAC_TAGS_CFG_VLAN_LEN_AWR_ENA                 BIT(2)
#define DEV_MAC_TAGS_CFG_VLAN_DBL_AWR_ENA                 BIT(1)
#define DEV_MAC_TAGS_CFG_VLAN_AWR_ENA                     BIT(0)

#define DEV_MAC_ADV_CHK_CFG                               0x2c

#define DEV_MAC_ADV_CHK_CFG_LEN_DROP_ENA                  BIT(0)

#define DEV_MAC_IFG_CFG                                   0x30

#define DEV_MAC_IFG_CFG_RESTORE_OLD_IPG_CHECK             BIT(17)
#define DEV_MAC_IFG_CFG_REDUCED_TX_IFG                    BIT(16)
#define DEV_MAC_IFG_CFG_TX_IFG(x)                         (((x) << 8) & GENMASK(12, 8))
#define DEV_MAC_IFG_CFG_TX_IFG_M                          GENMASK(12, 8)
#define DEV_MAC_IFG_CFG_TX_IFG_X(x)                       (((x) & GENMASK(12, 8)) >> 8)
#define DEV_MAC_IFG_CFG_RX_IFG2(x)                        (((x) << 4) & GENMASK(7, 4))
#define DEV_MAC_IFG_CFG_RX_IFG2_M                         GENMASK(7, 4)
#define DEV_MAC_IFG_CFG_RX_IFG2_X(x)                      (((x) & GENMASK(7, 4)) >> 4)
#define DEV_MAC_IFG_CFG_RX_IFG1(x)                        ((x) & GENMASK(3, 0))
#define DEV_MAC_IFG_CFG_RX_IFG1_M                         GENMASK(3, 0)

#define DEV_MAC_HDX_CFG                                   0x34

#define DEV_MAC_HDX_CFG_BYPASS_COL_SYNC                   BIT(26)
#define DEV_MAC_HDX_CFG_OB_ENA                            BIT(25)
#define DEV_MAC_HDX_CFG_WEXC_DIS                          BIT(24)
#define DEV_MAC_HDX_CFG_SEED(x)                           (((x) << 16) & GENMASK(23, 16))
#define DEV_MAC_HDX_CFG_SEED_M                            GENMASK(23, 16)
#define DEV_MAC_HDX_CFG_SEED_X(x)                         (((x) & GENMASK(23, 16)) >> 16)
#define DEV_MAC_HDX_CFG_SEED_LOAD                         BIT(12)
#define DEV_MAC_HDX_CFG_RETRY_AFTER_EXC_COL_ENA           BIT(8)
#define DEV_MAC_HDX_CFG_LATE_COL_POS(x)                   ((x) & GENMASK(6, 0))
#define DEV_MAC_HDX_CFG_LATE_COL_POS_M                    GENMASK(6, 0)

#define DEV_MAC_DBG_CFG                                   0x38

#define DEV_MAC_DBG_CFG_TBI_MODE                          BIT(4)
#define DEV_MAC_DBG_CFG_IFG_CRS_EXT_CHK_ENA               BIT(0)

#define DEV_MAC_FC_MAC_LOW_CFG                            0x3c

#define DEV_MAC_FC_MAC_HIGH_CFG                           0x40

#define DEV_MAC_STICKY                                    0x44

#define DEV_MAC_STICKY_RX_IPG_SHRINK_STICKY               BIT(9)
#define DEV_MAC_STICKY_RX_PREAM_SHRINK_STICKY             BIT(8)
#define DEV_MAC_STICKY_RX_CARRIER_EXT_STICKY              BIT(7)
#define DEV_MAC_STICKY_RX_CARRIER_EXT_ERR_STICKY          BIT(6)
#define DEV_MAC_STICKY_RX_JUNK_STICKY                     BIT(5)
#define DEV_MAC_STICKY_TX_RETRANSMIT_STICKY               BIT(4)
#define DEV_MAC_STICKY_TX_JAM_STICKY                      BIT(3)
#define DEV_MAC_STICKY_TX_FIFO_OFLW_STICKY                BIT(2)
#define DEV_MAC_STICKY_TX_FRM_LEN_OVR_STICKY              BIT(1)
#define DEV_MAC_STICKY_TX_ABORT_STICKY                    BIT(0)

#define PCS1G_CFG                                         0x48

#define PCS1G_CFG_LINK_STATUS_TYPE                        BIT(4)
#define PCS1G_CFG_AN_LINK_CTRL_ENA                        BIT(1)
#define PCS1G_CFG_PCS_ENA                                 BIT(0)

#define PCS1G_MODE_CFG                                    0x4c

#define PCS1G_MODE_CFG_UNIDIR_MODE_ENA                    BIT(4)
#define PCS1G_MODE_CFG_SGMII_MODE_ENA                     BIT(0)

#define PCS1G_SD_CFG                                      0x50

#define PCS1G_SD_CFG_SD_SEL                               BIT(8)
#define PCS1G_SD_CFG_SD_POL                               BIT(4)
#define PCS1G_SD_CFG_SD_ENA                               BIT(0)

#define PCS1G_ANEG_CFG                                    0x54

#define PCS1G_ANEG_CFG_ADV_ABILITY(x)                     (((x) << 16) & GENMASK(31, 16))
#define PCS1G_ANEG_CFG_ADV_ABILITY_M                      GENMASK(31, 16)
#define PCS1G_ANEG_CFG_ADV_ABILITY_X(x)                   (((x) & GENMASK(31, 16)) >> 16)
#define PCS1G_ANEG_CFG_SW_RESOLVE_ENA                     BIT(8)
#define PCS1G_ANEG_CFG_ANEG_RESTART_ONE_SHOT              BIT(1)
#define PCS1G_ANEG_CFG_ANEG_ENA                           BIT(0)

#define PCS1G_ANEG_NP_CFG                                 0x58

#define PCS1G_ANEG_NP_CFG_NP_TX(x)                        (((x) << 16) & GENMASK(31, 16))
#define PCS1G_ANEG_NP_CFG_NP_TX_M                         GENMASK(31, 16)
#define PCS1G_ANEG_NP_CFG_NP_TX_X(x)                      (((x) & GENMASK(31, 16)) >> 16)
#define PCS1G_ANEG_NP_CFG_NP_LOADED_ONE_SHOT              BIT(0)

#define PCS1G_LB_CFG                                      0x5c

#define PCS1G_LB_CFG_RA_ENA                               BIT(4)
#define PCS1G_LB_CFG_GMII_PHY_LB_ENA                      BIT(1)
#define PCS1G_LB_CFG_TBI_HOST_LB_ENA                      BIT(0)

#define PCS1G_DBG_CFG                                     0x60

#define PCS1G_DBG_CFG_UDLT                                BIT(0)

#define PCS1G_CDET_CFG                                    0x64

#define PCS1G_CDET_CFG_CDET_ENA                           BIT(0)

#define PCS1G_ANEG_STATUS                                 0x68

#define PCS1G_ANEG_STATUS_LP_ADV_ABILITY(x)               (((x) << 16) & GENMASK(31, 16))
#define PCS1G_ANEG_STATUS_LP_ADV_ABILITY_M                GENMASK(31, 16)
#define PCS1G_ANEG_STATUS_LP_ADV_ABILITY_X(x)             (((x) & GENMASK(31, 16)) >> 16)
#define PCS1G_ANEG_STATUS_PR                              BIT(4)
#define PCS1G_ANEG_STATUS_PAGE_RX_STICKY                  BIT(3)
#define PCS1G_ANEG_STATUS_ANEG_COMPLETE                   BIT(0)

#define PCS1G_ANEG_NP_STATUS                              0x6c

#define PCS1G_LINK_STATUS                                 0x70

#define PCS1G_LINK_STATUS_DELAY_VAR(x)                    (((x) << 12) & GENMASK(15, 12))
#define PCS1G_LINK_STATUS_DELAY_VAR_M                     GENMASK(15, 12)
#define PCS1G_LINK_STATUS_DELAY_VAR_X(x)                  (((x) & GENMASK(15, 12)) >> 12)
#define PCS1G_LINK_STATUS_SIGNAL_DETECT                   BIT(8)
#define PCS1G_LINK_STATUS_LINK_STATUS                     BIT(4)
#define PCS1G_LINK_STATUS_SYNC_STATUS                     BIT(0)

#define PCS1G_LINK_DOWN_CNT                               0x74

#define PCS1G_STICKY                                      0x78

#define PCS1G_STICKY_LINK_DOWN_STICKY                     BIT(4)
#define PCS1G_STICKY_OUT_OF_SYNC_STICKY                   BIT(0)

#define PCS1G_DEBUG_STATUS                                0x7c

#define PCS1G_LPI_CFG                                     0x80

#define PCS1G_LPI_CFG_QSGMII_MS_SEL                       BIT(20)
#define PCS1G_LPI_CFG_RX_LPI_OUT_DIS                      BIT(17)
#define PCS1G_LPI_CFG_LPI_TESTMODE                        BIT(16)
#define PCS1G_LPI_CFG_LPI_RX_WTIM(x)                      (((x) << 4) & GENMASK(5, 4))
#define PCS1G_LPI_CFG_LPI_RX_WTIM_M                       GENMASK(5, 4)
#define PCS1G_LPI_CFG_LPI_RX_WTIM_X(x)                    (((x) & GENMASK(5, 4)) >> 4)
#define PCS1G_LPI_CFG_TX_ASSERT_LPIDLE                    BIT(0)

#define PCS1G_LPI_WAKE_ERROR_CNT                          0x84

#define PCS1G_LPI_STATUS                                  0x88

#define PCS1G_LPI_STATUS_RX_LPI_FAIL                      BIT(16)
#define PCS1G_LPI_STATUS_RX_LPI_EVENT_STICKY              BIT(12)
#define PCS1G_LPI_STATUS_RX_QUIET                         BIT(9)
#define PCS1G_LPI_STATUS_RX_LPI_MODE                      BIT(8)
#define PCS1G_LPI_STATUS_TX_LPI_EVENT_STICKY              BIT(4)
#define PCS1G_LPI_STATUS_TX_QUIET                         BIT(1)
#define PCS1G_LPI_STATUS_TX_LPI_MODE                      BIT(0)

#define PCS1G_TSTPAT_MODE_CFG                             0x8c

#define PCS1G_TSTPAT_STATUS                               0x90

#define PCS1G_TSTPAT_STATUS_JTP_ERR_CNT(x)                (((x) << 8) & GENMASK(15, 8))
#define PCS1G_TSTPAT_STATUS_JTP_ERR_CNT_M                 GENMASK(15, 8)
#define PCS1G_TSTPAT_STATUS_JTP_ERR_CNT_X(x)              (((x) & GENMASK(15, 8)) >> 8)
#define PCS1G_TSTPAT_STATUS_JTP_ERR                       BIT(4)
#define PCS1G_TSTPAT_STATUS_JTP_LOCK                      BIT(0)

#define DEV_PCS_FX100_CFG                                 0x94

#define DEV_PCS_FX100_CFG_SD_SEL                          BIT(26)
#define DEV_PCS_FX100_CFG_SD_POL                          BIT(25)
#define DEV_PCS_FX100_CFG_SD_ENA                          BIT(24)
#define DEV_PCS_FX100_CFG_LOOPBACK_ENA                    BIT(20)
#define DEV_PCS_FX100_CFG_SWAP_MII_ENA                    BIT(16)
#define DEV_PCS_FX100_CFG_RXBITSEL(x)                     (((x) << 12) & GENMASK(15, 12))
#define DEV_PCS_FX100_CFG_RXBITSEL_M                      GENMASK(15, 12)
#define DEV_PCS_FX100_CFG_RXBITSEL_X(x)                   (((x) & GENMASK(15, 12)) >> 12)
#define DEV_PCS_FX100_CFG_SIGDET_CFG(x)                   (((x) << 9) & GENMASK(10, 9))
#define DEV_PCS_FX100_CFG_SIGDET_CFG_M                    GENMASK(10, 9)
#define DEV_PCS_FX100_CFG_SIGDET_CFG_X(x)                 (((x) & GENMASK(10, 9)) >> 9)
#define DEV_PCS_FX100_CFG_LINKHYST_TM_ENA                 BIT(8)
#define DEV_PCS_FX100_CFG_LINKHYSTTIMER(x)                (((x) << 4) & GENMASK(7, 4))
#define DEV_PCS_FX100_CFG_LINKHYSTTIMER_M                 GENMASK(7, 4)
#define DEV_PCS_FX100_CFG_LINKHYSTTIMER_X(x)              (((x) & GENMASK(7, 4)) >> 4)
#define DEV_PCS_FX100_CFG_UNIDIR_MODE_ENA                 BIT(3)
#define DEV_PCS_FX100_CFG_FEFCHK_ENA                      BIT(2)
#define DEV_PCS_FX100_CFG_FEFGEN_ENA                      BIT(1)
#define DEV_PCS_FX100_CFG_PCS_ENA                         BIT(0)

#define DEV_PCS_FX100_STATUS                              0x98

#define DEV_PCS_FX100_STATUS_EDGE_POS_PTP(x)              (((x) << 8) & GENMASK(11, 8))
#define DEV_PCS_FX100_STATUS_EDGE_POS_PTP_M               GENMASK(11, 8)
#define DEV_PCS_FX100_STATUS_EDGE_POS_PTP_X(x)            (((x) & GENMASK(11, 8)) >> 8)
#define DEV_PCS_FX100_STATUS_PCS_ERROR_STICKY             BIT(7)
#define DEV_PCS_FX100_STATUS_FEF_FOUND_STICKY             BIT(6)
#define DEV_PCS_FX100_STATUS_SSD_ERROR_STICKY             BIT(5)
#define DEV_PCS_FX100_STATUS_SYNC_LOST_STICKY             BIT(4)
#define DEV_PCS_FX100_STATUS_FEF_STATUS                   BIT(2)
#define DEV_PCS_FX100_STATUS_SIGNAL_DETECT                BIT(1)
#define DEV_PCS_FX100_STATUS_SYNC_STATUS                  BIT(0)

#endif
