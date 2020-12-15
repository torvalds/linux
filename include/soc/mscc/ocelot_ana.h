/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/*
 * Microsemi Ocelot Switch driver
 *
 * Copyright (c) 2017 Microsemi Corporation
 */

#ifndef _MSCC_OCELOT_ANA_H_
#define _MSCC_OCELOT_ANA_H_

#define ANA_ANAGEFIL_B_DOM_EN                             BIT(22)
#define ANA_ANAGEFIL_B_DOM_VAL                            BIT(21)
#define ANA_ANAGEFIL_AGE_LOCKED                           BIT(20)
#define ANA_ANAGEFIL_PID_EN                               BIT(19)
#define ANA_ANAGEFIL_PID_VAL(x)                           (((x) << 14) & GENMASK(18, 14))
#define ANA_ANAGEFIL_PID_VAL_M                            GENMASK(18, 14)
#define ANA_ANAGEFIL_PID_VAL_X(x)                         (((x) & GENMASK(18, 14)) >> 14)
#define ANA_ANAGEFIL_VID_EN                               BIT(13)
#define ANA_ANAGEFIL_VID_VAL(x)                           ((x) & GENMASK(12, 0))
#define ANA_ANAGEFIL_VID_VAL_M                            GENMASK(12, 0)

#define ANA_STORMLIMIT_CFG_RSZ                            0x4

#define ANA_STORMLIMIT_CFG_STORM_RATE(x)                  (((x) << 3) & GENMASK(6, 3))
#define ANA_STORMLIMIT_CFG_STORM_RATE_M                   GENMASK(6, 3)
#define ANA_STORMLIMIT_CFG_STORM_RATE_X(x)                (((x) & GENMASK(6, 3)) >> 3)
#define ANA_STORMLIMIT_CFG_STORM_UNIT                     BIT(2)
#define ANA_STORMLIMIT_CFG_STORM_MODE(x)                  ((x) & GENMASK(1, 0))
#define ANA_STORMLIMIT_CFG_STORM_MODE_M                   GENMASK(1, 0)

#define ANA_AUTOAGE_AGE_FAST                              BIT(21)
#define ANA_AUTOAGE_AGE_PERIOD(x)                         (((x) << 1) & GENMASK(20, 1))
#define ANA_AUTOAGE_AGE_PERIOD_M                          GENMASK(20, 1)
#define ANA_AUTOAGE_AGE_PERIOD_X(x)                       (((x) & GENMASK(20, 1)) >> 1)
#define ANA_AUTOAGE_AUTOAGE_LOCKED                        BIT(0)

#define ANA_MACTOPTIONS_REDUCED_TABLE                     BIT(1)
#define ANA_MACTOPTIONS_SHADOW                            BIT(0)

#define ANA_AGENCTRL_FID_MASK(x)                          (((x) << 12) & GENMASK(23, 12))
#define ANA_AGENCTRL_FID_MASK_M                           GENMASK(23, 12)
#define ANA_AGENCTRL_FID_MASK_X(x)                        (((x) & GENMASK(23, 12)) >> 12)
#define ANA_AGENCTRL_IGNORE_DMAC_FLAGS                    BIT(11)
#define ANA_AGENCTRL_IGNORE_SMAC_FLAGS                    BIT(10)
#define ANA_AGENCTRL_FLOOD_SPECIAL                        BIT(9)
#define ANA_AGENCTRL_FLOOD_IGNORE_VLAN                    BIT(8)
#define ANA_AGENCTRL_MIRROR_CPU                           BIT(7)
#define ANA_AGENCTRL_LEARN_CPU_COPY                       BIT(6)
#define ANA_AGENCTRL_LEARN_FWD_KILL                       BIT(5)
#define ANA_AGENCTRL_LEARN_IGNORE_VLAN                    BIT(4)
#define ANA_AGENCTRL_CPU_CPU_KILL_ENA                     BIT(3)
#define ANA_AGENCTRL_GREEN_COUNT_MODE                     BIT(2)
#define ANA_AGENCTRL_YELLOW_COUNT_MODE                    BIT(1)
#define ANA_AGENCTRL_RED_COUNT_MODE                       BIT(0)

#define ANA_FLOODING_RSZ                                  0x4

#define ANA_FLOODING_FLD_UNICAST(x)                       (((x) << 12) & GENMASK(17, 12))
#define ANA_FLOODING_FLD_UNICAST_M                        GENMASK(17, 12)
#define ANA_FLOODING_FLD_UNICAST_X(x)                     (((x) & GENMASK(17, 12)) >> 12)
#define ANA_FLOODING_FLD_BROADCAST(x)                     (((x) << 6) & GENMASK(11, 6))
#define ANA_FLOODING_FLD_BROADCAST_M                      GENMASK(11, 6)
#define ANA_FLOODING_FLD_BROADCAST_X(x)                   (((x) & GENMASK(11, 6)) >> 6)
#define ANA_FLOODING_FLD_MULTICAST(x)                     ((x) & GENMASK(5, 0))
#define ANA_FLOODING_FLD_MULTICAST_M                      GENMASK(5, 0)

#define ANA_FLOODING_IPMC_FLD_MC4_CTRL(x)                 (((x) << 18) & GENMASK(23, 18))
#define ANA_FLOODING_IPMC_FLD_MC4_CTRL_M                  GENMASK(23, 18)
#define ANA_FLOODING_IPMC_FLD_MC4_CTRL_X(x)               (((x) & GENMASK(23, 18)) >> 18)
#define ANA_FLOODING_IPMC_FLD_MC4_DATA(x)                 (((x) << 12) & GENMASK(17, 12))
#define ANA_FLOODING_IPMC_FLD_MC4_DATA_M                  GENMASK(17, 12)
#define ANA_FLOODING_IPMC_FLD_MC4_DATA_X(x)               (((x) & GENMASK(17, 12)) >> 12)
#define ANA_FLOODING_IPMC_FLD_MC6_CTRL(x)                 (((x) << 6) & GENMASK(11, 6))
#define ANA_FLOODING_IPMC_FLD_MC6_CTRL_M                  GENMASK(11, 6)
#define ANA_FLOODING_IPMC_FLD_MC6_CTRL_X(x)               (((x) & GENMASK(11, 6)) >> 6)
#define ANA_FLOODING_IPMC_FLD_MC6_DATA(x)                 ((x) & GENMASK(5, 0))
#define ANA_FLOODING_IPMC_FLD_MC6_DATA_M                  GENMASK(5, 0)

#define ANA_SFLOW_CFG_RSZ                                 0x4

#define ANA_SFLOW_CFG_SF_RATE(x)                          (((x) << 2) & GENMASK(13, 2))
#define ANA_SFLOW_CFG_SF_RATE_M                           GENMASK(13, 2)
#define ANA_SFLOW_CFG_SF_RATE_X(x)                        (((x) & GENMASK(13, 2)) >> 2)
#define ANA_SFLOW_CFG_SF_SAMPLE_RX                        BIT(1)
#define ANA_SFLOW_CFG_SF_SAMPLE_TX                        BIT(0)

#define ANA_PORT_MODE_RSZ                                 0x4

#define ANA_PORT_MODE_REDTAG_PARSE_CFG                    BIT(3)
#define ANA_PORT_MODE_VLAN_PARSE_CFG(x)                   (((x) << 1) & GENMASK(2, 1))
#define ANA_PORT_MODE_VLAN_PARSE_CFG_M                    GENMASK(2, 1)
#define ANA_PORT_MODE_VLAN_PARSE_CFG_X(x)                 (((x) & GENMASK(2, 1)) >> 1)
#define ANA_PORT_MODE_L3_PARSE_CFG                        BIT(0)

#define ANA_CUT_THRU_CFG_RSZ                              0x4

#define ANA_PGID_PGID_RSZ                                 0x4

#define ANA_PGID_PGID_PGID(x)                             ((x) & GENMASK(11, 0))
#define ANA_PGID_PGID_PGID_M                              GENMASK(11, 0)
#define ANA_PGID_PGID_CPUQ_DST_PGID(x)                    (((x) << 27) & GENMASK(29, 27))
#define ANA_PGID_PGID_CPUQ_DST_PGID_M                     GENMASK(29, 27)
#define ANA_PGID_PGID_CPUQ_DST_PGID_X(x)                  (((x) & GENMASK(29, 27)) >> 27)

#define ANA_TABLES_MACHDATA_VID(x)                        (((x) << 16) & GENMASK(28, 16))
#define ANA_TABLES_MACHDATA_VID_M                         GENMASK(28, 16)
#define ANA_TABLES_MACHDATA_VID_X(x)                      (((x) & GENMASK(28, 16)) >> 16)
#define ANA_TABLES_MACHDATA_MACHDATA(x)                   ((x) & GENMASK(15, 0))
#define ANA_TABLES_MACHDATA_MACHDATA_M                    GENMASK(15, 0)

#define ANA_TABLES_STREAMDATA_SSID_VALID                  BIT(16)
#define ANA_TABLES_STREAMDATA_SSID(x)                     (((x) << 9) & GENMASK(15, 9))
#define ANA_TABLES_STREAMDATA_SSID_M                      GENMASK(15, 9)
#define ANA_TABLES_STREAMDATA_SSID_X(x)                   (((x) & GENMASK(15, 9)) >> 9)
#define ANA_TABLES_STREAMDATA_SFID_VALID                  BIT(8)
#define ANA_TABLES_STREAMDATA_SFID(x)                     ((x) & GENMASK(7, 0))
#define ANA_TABLES_STREAMDATA_SFID_M                      GENMASK(7, 0)

#define ANA_TABLES_MACACCESS_MAC_CPU_COPY                 BIT(15)
#define ANA_TABLES_MACACCESS_SRC_KILL                     BIT(14)
#define ANA_TABLES_MACACCESS_IGNORE_VLAN                  BIT(13)
#define ANA_TABLES_MACACCESS_AGED_FLAG                    BIT(12)
#define ANA_TABLES_MACACCESS_VALID                        BIT(11)
#define ANA_TABLES_MACACCESS_ENTRYTYPE(x)                 (((x) << 9) & GENMASK(10, 9))
#define ANA_TABLES_MACACCESS_ENTRYTYPE_M                  GENMASK(10, 9)
#define ANA_TABLES_MACACCESS_ENTRYTYPE_X(x)               (((x) & GENMASK(10, 9)) >> 9)
#define ANA_TABLES_MACACCESS_DEST_IDX(x)                  (((x) << 3) & GENMASK(8, 3))
#define ANA_TABLES_MACACCESS_DEST_IDX_M                   GENMASK(8, 3)
#define ANA_TABLES_MACACCESS_DEST_IDX_X(x)                (((x) & GENMASK(8, 3)) >> 3)
#define ANA_TABLES_MACACCESS_MAC_TABLE_CMD(x)             ((x) & GENMASK(2, 0))
#define ANA_TABLES_MACACCESS_MAC_TABLE_CMD_M              GENMASK(2, 0)
#define MACACCESS_CMD_IDLE                     0
#define MACACCESS_CMD_LEARN                    1
#define MACACCESS_CMD_FORGET                   2
#define MACACCESS_CMD_AGE                      3
#define MACACCESS_CMD_GET_NEXT                 4
#define MACACCESS_CMD_INIT                     5
#define MACACCESS_CMD_READ                     6
#define MACACCESS_CMD_WRITE                    7

#define ANA_TABLES_VLANACCESS_VLAN_PORT_MASK(x)           (((x) << 2) & GENMASK(13, 2))
#define ANA_TABLES_VLANACCESS_VLAN_PORT_MASK_M            GENMASK(13, 2)
#define ANA_TABLES_VLANACCESS_VLAN_PORT_MASK_X(x)         (((x) & GENMASK(13, 2)) >> 2)
#define ANA_TABLES_VLANACCESS_VLAN_TBL_CMD(x)             ((x) & GENMASK(1, 0))
#define ANA_TABLES_VLANACCESS_VLAN_TBL_CMD_M              GENMASK(1, 0)
#define ANA_TABLES_VLANACCESS_CMD_IDLE                    0x0
#define ANA_TABLES_VLANACCESS_CMD_WRITE                   0x2
#define ANA_TABLES_VLANACCESS_CMD_INIT                    0x3

#define ANA_TABLES_VLANTIDX_VLAN_SEC_FWD_ENA              BIT(17)
#define ANA_TABLES_VLANTIDX_VLAN_FLOOD_DIS                BIT(16)
#define ANA_TABLES_VLANTIDX_VLAN_PRIV_VLAN                BIT(15)
#define ANA_TABLES_VLANTIDX_VLAN_LEARN_DISABLED           BIT(14)
#define ANA_TABLES_VLANTIDX_VLAN_MIRROR                   BIT(13)
#define ANA_TABLES_VLANTIDX_VLAN_SRC_CHK                  BIT(12)
#define ANA_TABLES_VLANTIDX_V_INDEX(x)                    ((x) & GENMASK(11, 0))
#define ANA_TABLES_VLANTIDX_V_INDEX_M                     GENMASK(11, 0)

#define ANA_TABLES_ISDXACCESS_ISDX_PORT_MASK(x)           (((x) << 2) & GENMASK(8, 2))
#define ANA_TABLES_ISDXACCESS_ISDX_PORT_MASK_M            GENMASK(8, 2)
#define ANA_TABLES_ISDXACCESS_ISDX_PORT_MASK_X(x)         (((x) & GENMASK(8, 2)) >> 2)
#define ANA_TABLES_ISDXACCESS_ISDX_TBL_CMD(x)             ((x) & GENMASK(1, 0))
#define ANA_TABLES_ISDXACCESS_ISDX_TBL_CMD_M              GENMASK(1, 0)

#define ANA_TABLES_ISDXTIDX_ISDX_SDLBI(x)                 (((x) << 21) & GENMASK(28, 21))
#define ANA_TABLES_ISDXTIDX_ISDX_SDLBI_M                  GENMASK(28, 21)
#define ANA_TABLES_ISDXTIDX_ISDX_SDLBI_X(x)               (((x) & GENMASK(28, 21)) >> 21)
#define ANA_TABLES_ISDXTIDX_ISDX_MSTI(x)                  (((x) << 15) & GENMASK(20, 15))
#define ANA_TABLES_ISDXTIDX_ISDX_MSTI_M                   GENMASK(20, 15)
#define ANA_TABLES_ISDXTIDX_ISDX_MSTI_X(x)                (((x) & GENMASK(20, 15)) >> 15)
#define ANA_TABLES_ISDXTIDX_ISDX_ES0_KEY_ENA              BIT(14)
#define ANA_TABLES_ISDXTIDX_ISDX_FORCE_ENA                BIT(10)
#define ANA_TABLES_ISDXTIDX_ISDX_INDEX(x)                 ((x) & GENMASK(7, 0))
#define ANA_TABLES_ISDXTIDX_ISDX_INDEX_M                  GENMASK(7, 0)

#define ANA_TABLES_ENTRYLIM_RSZ                           0x4

#define ANA_TABLES_ENTRYLIM_ENTRYLIM(x)                   (((x) << 14) & GENMASK(17, 14))
#define ANA_TABLES_ENTRYLIM_ENTRYLIM_M                    GENMASK(17, 14)
#define ANA_TABLES_ENTRYLIM_ENTRYLIM_X(x)                 (((x) & GENMASK(17, 14)) >> 14)
#define ANA_TABLES_ENTRYLIM_ENTRYSTAT(x)                  ((x) & GENMASK(13, 0))
#define ANA_TABLES_ENTRYLIM_ENTRYSTAT_M                   GENMASK(13, 0)

#define ANA_TABLES_STREAMACCESS_GEN_REC_SEQ_NUM(x)        (((x) << 4) & GENMASK(31, 4))
#define ANA_TABLES_STREAMACCESS_GEN_REC_SEQ_NUM_M         GENMASK(31, 4)
#define ANA_TABLES_STREAMACCESS_GEN_REC_SEQ_NUM_X(x)      (((x) & GENMASK(31, 4)) >> 4)
#define ANA_TABLES_STREAMACCESS_SEQ_GEN_REC_ENA           BIT(3)
#define ANA_TABLES_STREAMACCESS_GEN_REC_TYPE              BIT(2)
#define ANA_TABLES_STREAMACCESS_STREAM_TBL_CMD(x)         ((x) & GENMASK(1, 0))
#define ANA_TABLES_STREAMACCESS_STREAM_TBL_CMD_M          GENMASK(1, 0)

#define ANA_TABLES_STREAMTIDX_SEQ_GEN_ERR_STATUS(x)       (((x) << 30) & GENMASK(31, 30))
#define ANA_TABLES_STREAMTIDX_SEQ_GEN_ERR_STATUS_M        GENMASK(31, 30)
#define ANA_TABLES_STREAMTIDX_SEQ_GEN_ERR_STATUS_X(x)     (((x) & GENMASK(31, 30)) >> 30)
#define ANA_TABLES_STREAMTIDX_S_INDEX(x)                  (((x) << 16) & GENMASK(22, 16))
#define ANA_TABLES_STREAMTIDX_S_INDEX_M                   GENMASK(22, 16)
#define ANA_TABLES_STREAMTIDX_S_INDEX_X(x)                (((x) & GENMASK(22, 16)) >> 16)
#define ANA_TABLES_STREAMTIDX_FORCE_SF_BEHAVIOUR          BIT(14)
#define ANA_TABLES_STREAMTIDX_SEQ_HISTORY_LEN(x)          (((x) << 8) & GENMASK(13, 8))
#define ANA_TABLES_STREAMTIDX_SEQ_HISTORY_LEN_M           GENMASK(13, 8)
#define ANA_TABLES_STREAMTIDX_SEQ_HISTORY_LEN_X(x)        (((x) & GENMASK(13, 8)) >> 8)
#define ANA_TABLES_STREAMTIDX_RESET_ON_ROGUE              BIT(7)
#define ANA_TABLES_STREAMTIDX_REDTAG_POP                  BIT(6)
#define ANA_TABLES_STREAMTIDX_STREAM_SPLIT                BIT(5)
#define ANA_TABLES_STREAMTIDX_SEQ_SPACE_LOG2(x)           ((x) & GENMASK(4, 0))
#define ANA_TABLES_STREAMTIDX_SEQ_SPACE_LOG2_M            GENMASK(4, 0)

#define ANA_TABLES_SEQ_MASK_SPLIT_MASK(x)                 (((x) << 16) & GENMASK(22, 16))
#define ANA_TABLES_SEQ_MASK_SPLIT_MASK_M                  GENMASK(22, 16)
#define ANA_TABLES_SEQ_MASK_SPLIT_MASK_X(x)               (((x) & GENMASK(22, 16)) >> 16)
#define ANA_TABLES_SEQ_MASK_INPUT_PORT_MASK(x)            ((x) & GENMASK(6, 0))
#define ANA_TABLES_SEQ_MASK_INPUT_PORT_MASK_M             GENMASK(6, 0)

#define ANA_TABLES_SFID_MASK_IGR_PORT_MASK(x)             (((x) << 1) & GENMASK(7, 1))
#define ANA_TABLES_SFID_MASK_IGR_PORT_MASK_M              GENMASK(7, 1)
#define ANA_TABLES_SFID_MASK_IGR_PORT_MASK_X(x)           (((x) & GENMASK(7, 1)) >> 1)
#define ANA_TABLES_SFID_MASK_IGR_SRCPORT_MATCH_ENA        BIT(0)

#define ANA_TABLES_SFIDACCESS_IGR_PRIO_MATCH_ENA          BIT(22)
#define ANA_TABLES_SFIDACCESS_IGR_PRIO(x)                 (((x) << 19) & GENMASK(21, 19))
#define ANA_TABLES_SFIDACCESS_IGR_PRIO_M                  GENMASK(21, 19)
#define ANA_TABLES_SFIDACCESS_IGR_PRIO_X(x)               (((x) & GENMASK(21, 19)) >> 19)
#define ANA_TABLES_SFIDACCESS_FORCE_BLOCK                 BIT(18)
#define ANA_TABLES_SFIDACCESS_MAX_SDU_LEN(x)              (((x) << 2) & GENMASK(17, 2))
#define ANA_TABLES_SFIDACCESS_MAX_SDU_LEN_M               GENMASK(17, 2)
#define ANA_TABLES_SFIDACCESS_MAX_SDU_LEN_X(x)            (((x) & GENMASK(17, 2)) >> 2)
#define ANA_TABLES_SFIDACCESS_SFID_TBL_CMD(x)             ((x) & GENMASK(1, 0))
#define ANA_TABLES_SFIDACCESS_SFID_TBL_CMD_M              GENMASK(1, 0)

#define ANA_TABLES_SFIDTIDX_SGID_VALID                    BIT(26)
#define ANA_TABLES_SFIDTIDX_SGID(x)                       (((x) << 18) & GENMASK(25, 18))
#define ANA_TABLES_SFIDTIDX_SGID_M                        GENMASK(25, 18)
#define ANA_TABLES_SFIDTIDX_SGID_X(x)                     (((x) & GENMASK(25, 18)) >> 18)
#define ANA_TABLES_SFIDTIDX_POL_ENA                       BIT(17)
#define ANA_TABLES_SFIDTIDX_POL_IDX(x)                    (((x) << 8) & GENMASK(16, 8))
#define ANA_TABLES_SFIDTIDX_POL_IDX_M                     GENMASK(16, 8)
#define ANA_TABLES_SFIDTIDX_POL_IDX_X(x)                  (((x) & GENMASK(16, 8)) >> 8)
#define ANA_TABLES_SFIDTIDX_SFID_INDEX(x)                 ((x) & GENMASK(7, 0))
#define ANA_TABLES_SFIDTIDX_SFID_INDEX_M                  GENMASK(7, 0)

#define ANA_MSTI_STATE_RSZ                                0x4

#define ANA_OAM_UPM_LM_CNT_RSZ                            0x4

#define ANA_SG_ACCESS_CTRL_SGID(x)                        ((x) & GENMASK(7, 0))
#define ANA_SG_ACCESS_CTRL_SGID_M                         GENMASK(7, 0)
#define ANA_SG_ACCESS_CTRL_CONFIG_CHANGE                  BIT(28)

#define ANA_SG_CONFIG_REG_3_BASE_TIME_SEC_MSB(x)          ((x) & GENMASK(15, 0))
#define ANA_SG_CONFIG_REG_3_BASE_TIME_SEC_MSB_M           GENMASK(15, 0)
#define ANA_SG_CONFIG_REG_3_LIST_LENGTH(x)                (((x) << 16) & GENMASK(18, 16))
#define ANA_SG_CONFIG_REG_3_LIST_LENGTH_M                 GENMASK(18, 16)
#define ANA_SG_CONFIG_REG_3_LIST_LENGTH_X(x)              (((x) & GENMASK(18, 16)) >> 16)
#define ANA_SG_CONFIG_REG_3_GATE_ENABLE                   BIT(20)
#define ANA_SG_CONFIG_REG_3_INIT_IPS(x)                   (((x) << 21) & GENMASK(24, 21))
#define ANA_SG_CONFIG_REG_3_INIT_IPS_M                    GENMASK(24, 21)
#define ANA_SG_CONFIG_REG_3_INIT_IPS_X(x)                 (((x) & GENMASK(24, 21)) >> 21)
#define ANA_SG_CONFIG_REG_3_INIT_GATE_STATE               BIT(25)

#define ANA_SG_GCL_GS_CONFIG_RSZ                          0x4

#define ANA_SG_GCL_GS_CONFIG_IPS(x)                       ((x) & GENMASK(3, 0))
#define ANA_SG_GCL_GS_CONFIG_IPS_M                        GENMASK(3, 0)
#define ANA_SG_GCL_GS_CONFIG_GATE_STATE                   BIT(4)

#define ANA_SG_GCL_TI_CONFIG_RSZ                          0x4

#define ANA_SG_STATUS_REG_3_CFG_CHG_TIME_SEC_MSB(x)       ((x) & GENMASK(15, 0))
#define ANA_SG_STATUS_REG_3_CFG_CHG_TIME_SEC_MSB_M        GENMASK(15, 0)
#define ANA_SG_STATUS_REG_3_GATE_STATE                    BIT(16)
#define ANA_SG_STATUS_REG_3_IPS(x)                        (((x) << 20) & GENMASK(23, 20))
#define ANA_SG_STATUS_REG_3_IPS_M                         GENMASK(23, 20)
#define ANA_SG_STATUS_REG_3_IPS_X(x)                      (((x) & GENMASK(23, 20)) >> 20)
#define ANA_SG_STATUS_REG_3_CONFIG_PENDING                BIT(24)

#define ANA_PORT_VLAN_CFG_GSZ                             0x100

#define ANA_PORT_VLAN_CFG_VLAN_VID_AS_ISDX                BIT(21)
#define ANA_PORT_VLAN_CFG_VLAN_AWARE_ENA                  BIT(20)
#define ANA_PORT_VLAN_CFG_VLAN_POP_CNT(x)                 (((x) << 18) & GENMASK(19, 18))
#define ANA_PORT_VLAN_CFG_VLAN_POP_CNT_M                  GENMASK(19, 18)
#define ANA_PORT_VLAN_CFG_VLAN_POP_CNT_X(x)               (((x) & GENMASK(19, 18)) >> 18)
#define ANA_PORT_VLAN_CFG_VLAN_INNER_TAG_ENA              BIT(17)
#define ANA_PORT_VLAN_CFG_VLAN_TAG_TYPE                   BIT(16)
#define ANA_PORT_VLAN_CFG_VLAN_DEI                        BIT(15)
#define ANA_PORT_VLAN_CFG_VLAN_PCP(x)                     (((x) << 12) & GENMASK(14, 12))
#define ANA_PORT_VLAN_CFG_VLAN_PCP_M                      GENMASK(14, 12)
#define ANA_PORT_VLAN_CFG_VLAN_PCP_X(x)                   (((x) & GENMASK(14, 12)) >> 12)
#define ANA_PORT_VLAN_CFG_VLAN_VID(x)                     ((x) & GENMASK(11, 0))
#define ANA_PORT_VLAN_CFG_VLAN_VID_M                      GENMASK(11, 0)

#define ANA_PORT_DROP_CFG_GSZ                             0x100

#define ANA_PORT_DROP_CFG_DROP_UNTAGGED_ENA               BIT(6)
#define ANA_PORT_DROP_CFG_DROP_S_TAGGED_ENA               BIT(5)
#define ANA_PORT_DROP_CFG_DROP_C_TAGGED_ENA               BIT(4)
#define ANA_PORT_DROP_CFG_DROP_PRIO_S_TAGGED_ENA          BIT(3)
#define ANA_PORT_DROP_CFG_DROP_PRIO_C_TAGGED_ENA          BIT(2)
#define ANA_PORT_DROP_CFG_DROP_NULL_MAC_ENA               BIT(1)
#define ANA_PORT_DROP_CFG_DROP_MC_SMAC_ENA                BIT(0)

#define ANA_PORT_QOS_CFG_GSZ                              0x100

#define ANA_PORT_QOS_CFG_DP_DEFAULT_VAL                   BIT(8)
#define ANA_PORT_QOS_CFG_QOS_DEFAULT_VAL(x)               (((x) << 5) & GENMASK(7, 5))
#define ANA_PORT_QOS_CFG_QOS_DEFAULT_VAL_M                GENMASK(7, 5)
#define ANA_PORT_QOS_CFG_QOS_DEFAULT_VAL_X(x)             (((x) & GENMASK(7, 5)) >> 5)
#define ANA_PORT_QOS_CFG_QOS_DSCP_ENA                     BIT(4)
#define ANA_PORT_QOS_CFG_QOS_PCP_ENA                      BIT(3)
#define ANA_PORT_QOS_CFG_DSCP_TRANSLATE_ENA               BIT(2)
#define ANA_PORT_QOS_CFG_DSCP_REWR_CFG(x)                 ((x) & GENMASK(1, 0))
#define ANA_PORT_QOS_CFG_DSCP_REWR_CFG_M                  GENMASK(1, 0)

#define ANA_PORT_VCAP_CFG_GSZ                             0x100

#define ANA_PORT_VCAP_CFG_S1_ENA                          BIT(14)
#define ANA_PORT_VCAP_CFG_S1_DMAC_DIP_ENA(x)              (((x) << 11) & GENMASK(13, 11))
#define ANA_PORT_VCAP_CFG_S1_DMAC_DIP_ENA_M               GENMASK(13, 11)
#define ANA_PORT_VCAP_CFG_S1_DMAC_DIP_ENA_X(x)            (((x) & GENMASK(13, 11)) >> 11)
#define ANA_PORT_VCAP_CFG_S1_VLAN_INNER_TAG_ENA(x)        (((x) << 8) & GENMASK(10, 8))
#define ANA_PORT_VCAP_CFG_S1_VLAN_INNER_TAG_ENA_M         GENMASK(10, 8)
#define ANA_PORT_VCAP_CFG_S1_VLAN_INNER_TAG_ENA_X(x)      (((x) & GENMASK(10, 8)) >> 8)
#define ANA_PORT_VCAP_CFG_PAG_VAL(x)                      ((x) & GENMASK(7, 0))
#define ANA_PORT_VCAP_CFG_PAG_VAL_M                       GENMASK(7, 0)

#define ANA_PORT_VCAP_S1_KEY_CFG_GSZ                      0x100
#define ANA_PORT_VCAP_S1_KEY_CFG_RSZ                      0x4

#define ANA_PORT_VCAP_S1_KEY_CFG_S1_KEY_IP6_CFG(x)        (((x) << 4) & GENMASK(6, 4))
#define ANA_PORT_VCAP_S1_KEY_CFG_S1_KEY_IP6_CFG_M         GENMASK(6, 4)
#define ANA_PORT_VCAP_S1_KEY_CFG_S1_KEY_IP6_CFG_X(x)      (((x) & GENMASK(6, 4)) >> 4)
#define ANA_PORT_VCAP_S1_KEY_CFG_S1_KEY_IP4_CFG(x)        (((x) << 2) & GENMASK(3, 2))
#define ANA_PORT_VCAP_S1_KEY_CFG_S1_KEY_IP4_CFG_M         GENMASK(3, 2)
#define ANA_PORT_VCAP_S1_KEY_CFG_S1_KEY_IP4_CFG_X(x)      (((x) & GENMASK(3, 2)) >> 2)
#define ANA_PORT_VCAP_S1_KEY_CFG_S1_KEY_OTHER_CFG(x)      ((x) & GENMASK(1, 0))
#define ANA_PORT_VCAP_S1_KEY_CFG_S1_KEY_OTHER_CFG_M       GENMASK(1, 0)

#define ANA_PORT_VCAP_S2_CFG_GSZ                          0x100

#define ANA_PORT_VCAP_S2_CFG_S2_UDP_PAYLOAD_ENA(x)        (((x) << 17) & GENMASK(18, 17))
#define ANA_PORT_VCAP_S2_CFG_S2_UDP_PAYLOAD_ENA_M         GENMASK(18, 17)
#define ANA_PORT_VCAP_S2_CFG_S2_UDP_PAYLOAD_ENA_X(x)      (((x) & GENMASK(18, 17)) >> 17)
#define ANA_PORT_VCAP_S2_CFG_S2_ETYPE_PAYLOAD_ENA(x)      (((x) << 15) & GENMASK(16, 15))
#define ANA_PORT_VCAP_S2_CFG_S2_ETYPE_PAYLOAD_ENA_M       GENMASK(16, 15)
#define ANA_PORT_VCAP_S2_CFG_S2_ETYPE_PAYLOAD_ENA_X(x)    (((x) & GENMASK(16, 15)) >> 15)
#define ANA_PORT_VCAP_S2_CFG_S2_ENA                       BIT(14)
#define ANA_PORT_VCAP_S2_CFG_S2_SNAP_DIS(x)               (((x) << 12) & GENMASK(13, 12))
#define ANA_PORT_VCAP_S2_CFG_S2_SNAP_DIS_M                GENMASK(13, 12)
#define ANA_PORT_VCAP_S2_CFG_S2_SNAP_DIS_X(x)             (((x) & GENMASK(13, 12)) >> 12)
#define ANA_PORT_VCAP_S2_CFG_S2_ARP_DIS(x)                (((x) << 10) & GENMASK(11, 10))
#define ANA_PORT_VCAP_S2_CFG_S2_ARP_DIS_M                 GENMASK(11, 10)
#define ANA_PORT_VCAP_S2_CFG_S2_ARP_DIS_X(x)              (((x) & GENMASK(11, 10)) >> 10)
#define ANA_PORT_VCAP_S2_CFG_S2_IP_TCPUDP_DIS(x)          (((x) << 8) & GENMASK(9, 8))
#define ANA_PORT_VCAP_S2_CFG_S2_IP_TCPUDP_DIS_M           GENMASK(9, 8)
#define ANA_PORT_VCAP_S2_CFG_S2_IP_TCPUDP_DIS_X(x)        (((x) & GENMASK(9, 8)) >> 8)
#define ANA_PORT_VCAP_S2_CFG_S2_IP_OTHER_DIS(x)           (((x) << 6) & GENMASK(7, 6))
#define ANA_PORT_VCAP_S2_CFG_S2_IP_OTHER_DIS_M            GENMASK(7, 6)
#define ANA_PORT_VCAP_S2_CFG_S2_IP_OTHER_DIS_X(x)         (((x) & GENMASK(7, 6)) >> 6)
#define ANA_PORT_VCAP_S2_CFG_S2_IP6_CFG(x)                (((x) << 2) & GENMASK(5, 2))
#define ANA_PORT_VCAP_S2_CFG_S2_IP6_CFG_M                 GENMASK(5, 2)
#define ANA_PORT_VCAP_S2_CFG_S2_IP6_CFG_X(x)              (((x) & GENMASK(5, 2)) >> 2)
#define ANA_PORT_VCAP_S2_CFG_S2_OAM_DIS(x)                ((x) & GENMASK(1, 0))
#define ANA_PORT_VCAP_S2_CFG_S2_OAM_DIS_M                 GENMASK(1, 0)

#define ANA_PORT_PCP_DEI_MAP_GSZ                          0x100
#define ANA_PORT_PCP_DEI_MAP_RSZ                          0x4

#define ANA_PORT_PCP_DEI_MAP_DP_PCP_DEI_VAL               BIT(3)
#define ANA_PORT_PCP_DEI_MAP_QOS_PCP_DEI_VAL(x)           ((x) & GENMASK(2, 0))
#define ANA_PORT_PCP_DEI_MAP_QOS_PCP_DEI_VAL_M            GENMASK(2, 0)

#define ANA_PORT_CPU_FWD_CFG_GSZ                          0x100

#define ANA_PORT_CPU_FWD_CFG_CPU_VRAP_REDIR_ENA           BIT(7)
#define ANA_PORT_CPU_FWD_CFG_CPU_MLD_REDIR_ENA            BIT(6)
#define ANA_PORT_CPU_FWD_CFG_CPU_IGMP_REDIR_ENA           BIT(5)
#define ANA_PORT_CPU_FWD_CFG_CPU_IPMC_CTRL_COPY_ENA       BIT(4)
#define ANA_PORT_CPU_FWD_CFG_CPU_SRC_COPY_ENA             BIT(3)
#define ANA_PORT_CPU_FWD_CFG_CPU_ALLBRIDGE_DROP_ENA       BIT(2)
#define ANA_PORT_CPU_FWD_CFG_CPU_ALLBRIDGE_REDIR_ENA      BIT(1)
#define ANA_PORT_CPU_FWD_CFG_CPU_OAM_ENA                  BIT(0)

#define ANA_PORT_CPU_FWD_BPDU_CFG_GSZ                     0x100

#define ANA_PORT_CPU_FWD_BPDU_CFG_BPDU_DROP_ENA(x)        (((x) << 16) & GENMASK(31, 16))
#define ANA_PORT_CPU_FWD_BPDU_CFG_BPDU_DROP_ENA_M         GENMASK(31, 16)
#define ANA_PORT_CPU_FWD_BPDU_CFG_BPDU_DROP_ENA_X(x)      (((x) & GENMASK(31, 16)) >> 16)
#define ANA_PORT_CPU_FWD_BPDU_CFG_BPDU_REDIR_ENA(x)       ((x) & GENMASK(15, 0))
#define ANA_PORT_CPU_FWD_BPDU_CFG_BPDU_REDIR_ENA_M        GENMASK(15, 0)

#define ANA_PORT_CPU_FWD_GARP_CFG_GSZ                     0x100

#define ANA_PORT_CPU_FWD_GARP_CFG_GARP_DROP_ENA(x)        (((x) << 16) & GENMASK(31, 16))
#define ANA_PORT_CPU_FWD_GARP_CFG_GARP_DROP_ENA_M         GENMASK(31, 16)
#define ANA_PORT_CPU_FWD_GARP_CFG_GARP_DROP_ENA_X(x)      (((x) & GENMASK(31, 16)) >> 16)
#define ANA_PORT_CPU_FWD_GARP_CFG_GARP_REDIR_ENA(x)       ((x) & GENMASK(15, 0))
#define ANA_PORT_CPU_FWD_GARP_CFG_GARP_REDIR_ENA_M        GENMASK(15, 0)

#define ANA_PORT_CPU_FWD_CCM_CFG_GSZ                      0x100

#define ANA_PORT_CPU_FWD_CCM_CFG_CCM_DROP_ENA(x)          (((x) << 16) & GENMASK(31, 16))
#define ANA_PORT_CPU_FWD_CCM_CFG_CCM_DROP_ENA_M           GENMASK(31, 16)
#define ANA_PORT_CPU_FWD_CCM_CFG_CCM_DROP_ENA_X(x)        (((x) & GENMASK(31, 16)) >> 16)
#define ANA_PORT_CPU_FWD_CCM_CFG_CCM_REDIR_ENA(x)         ((x) & GENMASK(15, 0))
#define ANA_PORT_CPU_FWD_CCM_CFG_CCM_REDIR_ENA_M          GENMASK(15, 0)

#define ANA_PORT_PORT_CFG_GSZ                             0x100

#define ANA_PORT_PORT_CFG_SRC_MIRROR_ENA                  BIT(15)
#define ANA_PORT_PORT_CFG_LIMIT_DROP                      BIT(14)
#define ANA_PORT_PORT_CFG_LIMIT_CPU                       BIT(13)
#define ANA_PORT_PORT_CFG_LOCKED_PORTMOVE_DROP            BIT(12)
#define ANA_PORT_PORT_CFG_LOCKED_PORTMOVE_CPU             BIT(11)
#define ANA_PORT_PORT_CFG_LEARNDROP                       BIT(10)
#define ANA_PORT_PORT_CFG_LEARNCPU                        BIT(9)
#define ANA_PORT_PORT_CFG_LEARNAUTO                       BIT(8)
#define ANA_PORT_PORT_CFG_LEARN_ENA                       BIT(7)
#define ANA_PORT_PORT_CFG_RECV_ENA                        BIT(6)
#define ANA_PORT_PORT_CFG_PORTID_VAL(x)                   (((x) << 2) & GENMASK(5, 2))
#define ANA_PORT_PORT_CFG_PORTID_VAL_M                    GENMASK(5, 2)
#define ANA_PORT_PORT_CFG_PORTID_VAL_X(x)                 (((x) & GENMASK(5, 2)) >> 2)
#define ANA_PORT_PORT_CFG_USE_B_DOM_TBL                   BIT(1)
#define ANA_PORT_PORT_CFG_LSR_MODE                        BIT(0)

#define ANA_PORT_POL_CFG_GSZ                              0x100

#define ANA_PORT_POL_CFG_POL_CPU_REDIR_8021               BIT(19)
#define ANA_PORT_POL_CFG_POL_CPU_REDIR_IP                 BIT(18)
#define ANA_PORT_POL_CFG_PORT_POL_ENA                     BIT(17)
#define ANA_PORT_POL_CFG_QUEUE_POL_ENA(x)                 (((x) << 9) & GENMASK(16, 9))
#define ANA_PORT_POL_CFG_QUEUE_POL_ENA_M                  GENMASK(16, 9)
#define ANA_PORT_POL_CFG_QUEUE_POL_ENA_X(x)               (((x) & GENMASK(16, 9)) >> 9)
#define ANA_PORT_POL_CFG_POL_ORDER(x)                     ((x) & GENMASK(8, 0))
#define ANA_PORT_POL_CFG_POL_ORDER_M                      GENMASK(8, 0)

#define ANA_PORT_PTP_CFG_GSZ                              0x100

#define ANA_PORT_PTP_CFG_PTP_BACKPLANE_MODE               BIT(0)

#define ANA_PORT_PTP_DLY1_CFG_GSZ                         0x100

#define ANA_PORT_PTP_DLY2_CFG_GSZ                         0x100

#define ANA_PORT_SFID_CFG_GSZ                             0x100
#define ANA_PORT_SFID_CFG_RSZ                             0x4

#define ANA_PORT_SFID_CFG_SFID_VALID                      BIT(8)
#define ANA_PORT_SFID_CFG_SFID(x)                         ((x) & GENMASK(7, 0))
#define ANA_PORT_SFID_CFG_SFID_M                          GENMASK(7, 0)

#define ANA_PFC_PFC_CFG_GSZ                               0x40

#define ANA_PFC_PFC_CFG_RX_PFC_ENA(x)                     (((x) << 2) & GENMASK(9, 2))
#define ANA_PFC_PFC_CFG_RX_PFC_ENA_M                      GENMASK(9, 2)
#define ANA_PFC_PFC_CFG_RX_PFC_ENA_X(x)                   (((x) & GENMASK(9, 2)) >> 2)
#define ANA_PFC_PFC_CFG_FC_LINK_SPEED(x)                  ((x) & GENMASK(1, 0))
#define ANA_PFC_PFC_CFG_FC_LINK_SPEED_M                   GENMASK(1, 0)

#define ANA_PFC_PFC_TIMER_GSZ                             0x40
#define ANA_PFC_PFC_TIMER_RSZ                             0x4

#define ANA_IPT_OAM_MEP_CFG_GSZ                           0x8

#define ANA_IPT_OAM_MEP_CFG_MEP_IDX_P(x)                  (((x) << 6) & GENMASK(10, 6))
#define ANA_IPT_OAM_MEP_CFG_MEP_IDX_P_M                   GENMASK(10, 6)
#define ANA_IPT_OAM_MEP_CFG_MEP_IDX_P_X(x)                (((x) & GENMASK(10, 6)) >> 6)
#define ANA_IPT_OAM_MEP_CFG_MEP_IDX(x)                    (((x) << 1) & GENMASK(5, 1))
#define ANA_IPT_OAM_MEP_CFG_MEP_IDX_M                     GENMASK(5, 1)
#define ANA_IPT_OAM_MEP_CFG_MEP_IDX_X(x)                  (((x) & GENMASK(5, 1)) >> 1)
#define ANA_IPT_OAM_MEP_CFG_MEP_IDX_ENA                   BIT(0)

#define ANA_IPT_IPT_GSZ                                   0x8

#define ANA_IPT_IPT_IPT_CFG(x)                            (((x) << 15) & GENMASK(16, 15))
#define ANA_IPT_IPT_IPT_CFG_M                             GENMASK(16, 15)
#define ANA_IPT_IPT_IPT_CFG_X(x)                          (((x) & GENMASK(16, 15)) >> 15)
#define ANA_IPT_IPT_ISDX_P(x)                             (((x) << 7) & GENMASK(14, 7))
#define ANA_IPT_IPT_ISDX_P_M                              GENMASK(14, 7)
#define ANA_IPT_IPT_ISDX_P_X(x)                           (((x) & GENMASK(14, 7)) >> 7)
#define ANA_IPT_IPT_PPT_IDX(x)                            ((x) & GENMASK(6, 0))
#define ANA_IPT_IPT_PPT_IDX_M                             GENMASK(6, 0)

#define ANA_PPT_PPT_RSZ                                   0x4

#define ANA_FID_MAP_FID_MAP_RSZ                           0x4

#define ANA_FID_MAP_FID_MAP_FID_C_VAL(x)                  (((x) << 6) & GENMASK(11, 6))
#define ANA_FID_MAP_FID_MAP_FID_C_VAL_M                   GENMASK(11, 6)
#define ANA_FID_MAP_FID_MAP_FID_C_VAL_X(x)                (((x) & GENMASK(11, 6)) >> 6)
#define ANA_FID_MAP_FID_MAP_FID_B_VAL(x)                  ((x) & GENMASK(5, 0))
#define ANA_FID_MAP_FID_MAP_FID_B_VAL_M                   GENMASK(5, 0)

#define ANA_AGGR_CFG_AC_RND_ENA                           BIT(7)
#define ANA_AGGR_CFG_AC_DMAC_ENA                          BIT(6)
#define ANA_AGGR_CFG_AC_SMAC_ENA                          BIT(5)
#define ANA_AGGR_CFG_AC_IP6_FLOW_LBL_ENA                  BIT(4)
#define ANA_AGGR_CFG_AC_IP6_TCPUDP_ENA                    BIT(3)
#define ANA_AGGR_CFG_AC_IP4_SIPDIP_ENA                    BIT(2)
#define ANA_AGGR_CFG_AC_IP4_TCPUDP_ENA                    BIT(1)
#define ANA_AGGR_CFG_AC_ISDX_ENA                          BIT(0)

#define ANA_CPUQ_CFG_CPUQ_MLD(x)                          (((x) << 27) & GENMASK(29, 27))
#define ANA_CPUQ_CFG_CPUQ_MLD_M                           GENMASK(29, 27)
#define ANA_CPUQ_CFG_CPUQ_MLD_X(x)                        (((x) & GENMASK(29, 27)) >> 27)
#define ANA_CPUQ_CFG_CPUQ_IGMP(x)                         (((x) << 24) & GENMASK(26, 24))
#define ANA_CPUQ_CFG_CPUQ_IGMP_M                          GENMASK(26, 24)
#define ANA_CPUQ_CFG_CPUQ_IGMP_X(x)                       (((x) & GENMASK(26, 24)) >> 24)
#define ANA_CPUQ_CFG_CPUQ_IPMC_CTRL(x)                    (((x) << 21) & GENMASK(23, 21))
#define ANA_CPUQ_CFG_CPUQ_IPMC_CTRL_M                     GENMASK(23, 21)
#define ANA_CPUQ_CFG_CPUQ_IPMC_CTRL_X(x)                  (((x) & GENMASK(23, 21)) >> 21)
#define ANA_CPUQ_CFG_CPUQ_ALLBRIDGE(x)                    (((x) << 18) & GENMASK(20, 18))
#define ANA_CPUQ_CFG_CPUQ_ALLBRIDGE_M                     GENMASK(20, 18)
#define ANA_CPUQ_CFG_CPUQ_ALLBRIDGE_X(x)                  (((x) & GENMASK(20, 18)) >> 18)
#define ANA_CPUQ_CFG_CPUQ_LOCKED_PORTMOVE(x)              (((x) << 15) & GENMASK(17, 15))
#define ANA_CPUQ_CFG_CPUQ_LOCKED_PORTMOVE_M               GENMASK(17, 15)
#define ANA_CPUQ_CFG_CPUQ_LOCKED_PORTMOVE_X(x)            (((x) & GENMASK(17, 15)) >> 15)
#define ANA_CPUQ_CFG_CPUQ_SRC_COPY(x)                     (((x) << 12) & GENMASK(14, 12))
#define ANA_CPUQ_CFG_CPUQ_SRC_COPY_M                      GENMASK(14, 12)
#define ANA_CPUQ_CFG_CPUQ_SRC_COPY_X(x)                   (((x) & GENMASK(14, 12)) >> 12)
#define ANA_CPUQ_CFG_CPUQ_MAC_COPY(x)                     (((x) << 9) & GENMASK(11, 9))
#define ANA_CPUQ_CFG_CPUQ_MAC_COPY_M                      GENMASK(11, 9)
#define ANA_CPUQ_CFG_CPUQ_MAC_COPY_X(x)                   (((x) & GENMASK(11, 9)) >> 9)
#define ANA_CPUQ_CFG_CPUQ_LRN(x)                          (((x) << 6) & GENMASK(8, 6))
#define ANA_CPUQ_CFG_CPUQ_LRN_M                           GENMASK(8, 6)
#define ANA_CPUQ_CFG_CPUQ_LRN_X(x)                        (((x) & GENMASK(8, 6)) >> 6)
#define ANA_CPUQ_CFG_CPUQ_MIRROR(x)                       (((x) << 3) & GENMASK(5, 3))
#define ANA_CPUQ_CFG_CPUQ_MIRROR_M                        GENMASK(5, 3)
#define ANA_CPUQ_CFG_CPUQ_MIRROR_X(x)                     (((x) & GENMASK(5, 3)) >> 3)
#define ANA_CPUQ_CFG_CPUQ_SFLOW(x)                        ((x) & GENMASK(2, 0))
#define ANA_CPUQ_CFG_CPUQ_SFLOW_M                         GENMASK(2, 0)

#define ANA_CPUQ_8021_CFG_RSZ                             0x4

#define ANA_CPUQ_8021_CFG_CPUQ_BPDU_VAL(x)                (((x) << 6) & GENMASK(8, 6))
#define ANA_CPUQ_8021_CFG_CPUQ_BPDU_VAL_M                 GENMASK(8, 6)
#define ANA_CPUQ_8021_CFG_CPUQ_BPDU_VAL_X(x)              (((x) & GENMASK(8, 6)) >> 6)
#define ANA_CPUQ_8021_CFG_CPUQ_GARP_VAL(x)                (((x) << 3) & GENMASK(5, 3))
#define ANA_CPUQ_8021_CFG_CPUQ_GARP_VAL_M                 GENMASK(5, 3)
#define ANA_CPUQ_8021_CFG_CPUQ_GARP_VAL_X(x)              (((x) & GENMASK(5, 3)) >> 3)
#define ANA_CPUQ_8021_CFG_CPUQ_CCM_VAL(x)                 ((x) & GENMASK(2, 0))
#define ANA_CPUQ_8021_CFG_CPUQ_CCM_VAL_M                  GENMASK(2, 0)

#define ANA_DSCP_CFG_RSZ                                  0x4

#define ANA_DSCP_CFG_DP_DSCP_VAL                          BIT(11)
#define ANA_DSCP_CFG_QOS_DSCP_VAL(x)                      (((x) << 8) & GENMASK(10, 8))
#define ANA_DSCP_CFG_QOS_DSCP_VAL_M                       GENMASK(10, 8)
#define ANA_DSCP_CFG_QOS_DSCP_VAL_X(x)                    (((x) & GENMASK(10, 8)) >> 8)
#define ANA_DSCP_CFG_DSCP_TRANSLATE_VAL(x)                (((x) << 2) & GENMASK(7, 2))
#define ANA_DSCP_CFG_DSCP_TRANSLATE_VAL_M                 GENMASK(7, 2)
#define ANA_DSCP_CFG_DSCP_TRANSLATE_VAL_X(x)              (((x) & GENMASK(7, 2)) >> 2)
#define ANA_DSCP_CFG_DSCP_TRUST_ENA                       BIT(1)
#define ANA_DSCP_CFG_DSCP_REWR_ENA                        BIT(0)

#define ANA_DSCP_REWR_CFG_RSZ                             0x4

#define ANA_VCAP_RNG_TYPE_CFG_RSZ                         0x4

#define ANA_VCAP_RNG_VAL_CFG_RSZ                          0x4

#define ANA_VCAP_RNG_VAL_CFG_VCAP_RNG_MIN_VAL(x)          (((x) << 16) & GENMASK(31, 16))
#define ANA_VCAP_RNG_VAL_CFG_VCAP_RNG_MIN_VAL_M           GENMASK(31, 16)
#define ANA_VCAP_RNG_VAL_CFG_VCAP_RNG_MIN_VAL_X(x)        (((x) & GENMASK(31, 16)) >> 16)
#define ANA_VCAP_RNG_VAL_CFG_VCAP_RNG_MAX_VAL(x)          ((x) & GENMASK(15, 0))
#define ANA_VCAP_RNG_VAL_CFG_VCAP_RNG_MAX_VAL_M           GENMASK(15, 0)

#define ANA_VRAP_CFG_VRAP_VLAN_AWARE_ENA                  BIT(12)
#define ANA_VRAP_CFG_VRAP_VID(x)                          ((x) & GENMASK(11, 0))
#define ANA_VRAP_CFG_VRAP_VID_M                           GENMASK(11, 0)

#define ANA_DISCARD_CFG_DROP_TAGGING_ISDX0                BIT(3)
#define ANA_DISCARD_CFG_DROP_CTRLPROT_ISDX0               BIT(2)
#define ANA_DISCARD_CFG_DROP_TAGGING_S2_ENA               BIT(1)
#define ANA_DISCARD_CFG_DROP_CTRLPROT_S2_ENA              BIT(0)

#define ANA_FID_CFG_VID_MC_ENA                            BIT(0)

#define ANA_POL_PIR_CFG_GSZ                               0x20

#define ANA_POL_PIR_CFG_PIR_RATE(x)                       (((x) << 6) & GENMASK(20, 6))
#define ANA_POL_PIR_CFG_PIR_RATE_M                        GENMASK(20, 6)
#define ANA_POL_PIR_CFG_PIR_RATE_X(x)                     (((x) & GENMASK(20, 6)) >> 6)
#define ANA_POL_PIR_CFG_PIR_BURST(x)                      ((x) & GENMASK(5, 0))
#define ANA_POL_PIR_CFG_PIR_BURST_M                       GENMASK(5, 0)

#define ANA_POL_CIR_CFG_GSZ                               0x20

#define ANA_POL_CIR_CFG_CIR_RATE(x)                       (((x) << 6) & GENMASK(20, 6))
#define ANA_POL_CIR_CFG_CIR_RATE_M                        GENMASK(20, 6)
#define ANA_POL_CIR_CFG_CIR_RATE_X(x)                     (((x) & GENMASK(20, 6)) >> 6)
#define ANA_POL_CIR_CFG_CIR_BURST(x)                      ((x) & GENMASK(5, 0))
#define ANA_POL_CIR_CFG_CIR_BURST_M                       GENMASK(5, 0)

#define ANA_POL_MODE_CFG_GSZ                              0x20

#define ANA_POL_MODE_CFG_IPG_SIZE(x)                      (((x) << 5) & GENMASK(9, 5))
#define ANA_POL_MODE_CFG_IPG_SIZE_M                       GENMASK(9, 5)
#define ANA_POL_MODE_CFG_IPG_SIZE_X(x)                    (((x) & GENMASK(9, 5)) >> 5)
#define ANA_POL_MODE_CFG_FRM_MODE(x)                      (((x) << 3) & GENMASK(4, 3))
#define ANA_POL_MODE_CFG_FRM_MODE_M                       GENMASK(4, 3)
#define ANA_POL_MODE_CFG_FRM_MODE_X(x)                    (((x) & GENMASK(4, 3)) >> 3)
#define ANA_POL_MODE_CFG_DLB_COUPLED                      BIT(2)
#define ANA_POL_MODE_CFG_CIR_ENA                          BIT(1)
#define ANA_POL_MODE_CFG_OVERSHOOT_ENA                    BIT(0)

#define ANA_POL_PIR_STATE_GSZ                             0x20

#define ANA_POL_CIR_STATE_GSZ                             0x20

#define ANA_POL_STATE_GSZ                                 0x20

#define ANA_POL_FLOWC_RSZ                                 0x4

#define ANA_POL_FLOWC_POL_FLOWC                           BIT(0)

#define ANA_POL_HYST_POL_FC_HYST(x)                       (((x) << 4) & GENMASK(9, 4))
#define ANA_POL_HYST_POL_FC_HYST_M                        GENMASK(9, 4)
#define ANA_POL_HYST_POL_FC_HYST_X(x)                     (((x) & GENMASK(9, 4)) >> 4)
#define ANA_POL_HYST_POL_STOP_HYST(x)                     ((x) & GENMASK(3, 0))
#define ANA_POL_HYST_POL_STOP_HYST_M                      GENMASK(3, 0)

#define ANA_POL_MISC_CFG_POL_CLOSE_ALL                    BIT(1)
#define ANA_POL_MISC_CFG_POL_LEAK_DIS                     BIT(0)

#endif
