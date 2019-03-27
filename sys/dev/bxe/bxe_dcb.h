/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2007-2014 QLogic Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS'
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifndef BXE_DCB_H
#define BXE_DCB_H

#define LLFC_DRIVER_TRAFFIC_TYPE_MAX 3 /* NW, iSCSI, FCoE */
struct bxe_dcbx_app_params {
    uint32_t enabled;
    uint32_t traffic_type_priority[LLFC_DRIVER_TRAFFIC_TYPE_MAX];
};

#define DCBX_COS_MAX_NUM_E2 DCBX_E2E3_MAX_NUM_COS
/* bxe currently limits numbers of supported COSes to 3 to be extended to 6 */
#define BXE_MAX_COS_SUPPORT   3
#define DCBX_COS_MAX_NUM_E3B0 BXE_MAX_COS_SUPPORT
#define DCBX_COS_MAX_NUM      BXE_MAX_COS_SUPPORT

struct bxe_dcbx_cos_params {
    uint32_t bw_tbl;
    uint32_t pri_bitmask;
    /*
     * strict priority: valid values are 0..5; 0 is highest priority.
     * There can't be two COSes with the same priority.
     */
    uint8_t  strict;
#define BXE_DCBX_STRICT_INVALID                DCBX_COS_MAX_NUM
#define BXE_DCBX_STRICT_COS_HIGHEST            0
#define BXE_DCBX_STRICT_COS_NEXT_LOWER_PRI(sp) ((sp) + 1)
    uint8_t  pauseable;
};

struct bxe_dcbx_pg_params {
    uint32_t enabled;
    uint8_t  num_of_cos; /* valid COS entries */
    struct bxe_dcbx_cos_params cos_params[DCBX_COS_MAX_NUM];
};

struct bxe_dcbx_pfc_params {
    uint32_t enabled;
    uint32_t priority_non_pauseable_mask;
};

struct bxe_dcbx_port_params {
    struct bxe_dcbx_pfc_params pfc;
    struct bxe_dcbx_pg_params  ets;
    struct bxe_dcbx_app_params app;
};

#define BXE_DCBX_CONFIG_INV_VALUE           (0xFFFFFFFF)
#define BXE_DCBX_OVERWRITE_SETTINGS_DISABLE 0
#define BXE_DCBX_OVERWRITE_SETTINGS_ENABLE  1
#define BXE_DCBX_OVERWRITE_SETTINGS_INVALID (BXE_DCBX_CONFIG_INV_VALUE)
#define BXE_IS_ETS_ENABLED(sc)              \
    ((sc)->dcb_state == BXE_DCB_STATE_ON && \
     (sc)->dcbx_port_params.ets.enabled)

struct bxe_config_lldp_params {
    uint32_t overwrite_settings;
    uint32_t msg_tx_hold;
    uint32_t msg_fast_tx;
    uint32_t tx_credit_max;
    uint32_t msg_tx_interval;
    uint32_t tx_fast;
};

struct bxe_lldp_params_get {
    uint32_t ver_num;
#define LLDP_PARAMS_VER_NUM 2
    struct bxe_config_lldp_params config_lldp_params;
    /* The reserved field should follow in case the struct above will increase*/
    uint32_t _reserved[20];
    uint32_t admin_status;
#define LLDP_TX_ONLY  0x01
#define LLDP_RX_ONLY  0x02
#define LLDP_TX_RX    0x03
#define LLDP_DISABLED 0x04
    uint32_t remote_chassis_id[REM_CHASSIS_ID_STAT_LEN];
    uint32_t remote_port_id[REM_PORT_ID_STAT_LEN];
    uint32_t local_chassis_id[LOCAL_CHASSIS_ID_STAT_LEN];
    uint32_t local_port_id[LOCAL_PORT_ID_STAT_LEN];
};

struct bxe_admin_priority_app_table {
    uint32_t valid;
    uint32_t priority;
#define INVALID_TRAFFIC_TYPE_PRIORITY   (0xFFFFFFFF)
    uint32_t traffic_type;
#define TRAFFIC_TYPE_ETH        0
#define TRAFFIC_TYPE_PORT       1
    uint32_t app_id;
};

#define DCBX_CONFIG_MAX_APP_PROTOCOL 4
struct bxe_config_dcbx_params {
    uint32_t overwrite_settings;
    uint32_t admin_dcbx_version;
    uint32_t admin_ets_enable;
    uint32_t admin_pfc_enable;
    uint32_t admin_tc_supported_tx_enable;
    uint32_t admin_ets_configuration_tx_enable;
    uint32_t admin_ets_recommendation_tx_enable;
    uint32_t admin_pfc_tx_enable;
    uint32_t admin_application_priority_tx_enable;
    uint32_t admin_ets_willing;
    uint32_t admin_ets_reco_valid;
    uint32_t admin_pfc_willing;
    uint32_t admin_app_priority_willing;
    uint32_t admin_configuration_bw_precentage[8];
    uint32_t admin_configuration_ets_pg[8];
    uint32_t admin_recommendation_bw_precentage[8];
    uint32_t admin_recommendation_ets_pg[8];
    uint32_t admin_pfc_bitmap;
    struct bxe_admin_priority_app_table
        admin_priority_app_table[DCBX_CONFIG_MAX_APP_PROTOCOL];
    uint32_t admin_default_priority;
};

//#define DCBX_PARAMS_VER_NUM 3 /* XXX conflict with common_uif.h */
struct bxe_dcbx_params_get {
    uint32_t ver_num;
    uint32_t dcb_state;
    uint32_t dcbx_enabled;
    struct bxe_config_dcbx_params config_dcbx_params;
    /* The reserved field should follow in case the struct above will increase*/
    uint32_t _reserved[19];

    uint32_t dcb_current_state;
#define BXE_DCBX_CURRENT_STATE_IS_SYNC            (1 << 0)
#define BXE_PFC_IS_CURRENTLY_OPERATIONAL          (1 << 1)
#define BXE_ETS_IS_CURRENTLY_OPERATIONAL          (1 << 2)
#define BXE_PRIORITY_TAGGING_IS_CURRENTLY_OPERATIONAL     (1 << 3)

    uint32_t local_tc_supported;
    uint32_t local_pfc_caps;
    uint32_t remote_tc_supported;
    uint32_t remote_pfc_cap;
    uint32_t remote_ets_willing;
    uint32_t remote_ets_reco_valid;
    uint32_t remote_pfc_willing;
    uint32_t remote_app_priority_willing;
    uint32_t remote_configuration_bw_precentage[8];
    uint32_t remote_configuration_ets_pg[8];
    uint32_t remote_recommendation_bw_precentage[8];
    uint32_t remote_recommendation_ets_pg[8];
    uint32_t remote_pfc_bitmap;
    struct bxe_admin_priority_app_table
        remote_priority_app_table[DCBX_MAX_APP_PROTOCOL];
    uint32_t local_ets_enable;
    uint32_t local_pfc_enable;
    uint32_t local_configuration_bw_precentage[8];
    uint32_t local_configuration_ets_pg[8];
    uint32_t local_pfc_bitmap;
    struct bxe_admin_priority_app_table
        local_priority_app_table[DCBX_MAX_APP_PROTOCOL];
    uint32_t pfc_mismatch;
    uint32_t priority_app_mismatch;
    uint32_t dcbx_frames_sent;
    uint32_t dcbx_frames_received;
    uint32_t pfc_frames_sent[2];
    uint32_t pfc_frames_received[2];
};

struct bxe_dcbx_params_set {
    uint32_t ver_num;
    uint32_t dcb_state;
    uint32_t dcbx_enabled;
    struct bxe_config_dcbx_params config_dcbx_params;
};

#define GET_FLAGS(flags, bits)   ((flags) & (bits))
#define SET_FLAGS(flags, bits)   ((flags) |= (bits))
#define RESET_FLAGS(flags, bits) ((flags) &= ~(bits))

enum {
    DCBX_READ_LOCAL_MIB,
    DCBX_READ_REMOTE_MIB
};

#define ETH_TYPE_FCOE  (0x8906)
#define TCP_PORT_ISCSI (0xCBC)

#define PFC_VALUE_FRAME_SIZE (512)
#define PFC_QUANTA_IN_NANOSEC_FROM_SPEED_MEGA(mega_speed) \
    ((1000 * PFC_VALUE_FRAME_SIZE)/(mega_speed))

#define PFC_BRB1_REG_HIGH_LLFC_LOW_THRESHOLD  130
#define PFC_BRB1_REG_HIGH_LLFC_HIGH_THRESHOLD 170

struct cos_entry_help_data {
    uint32_t pri_join_mask;
    uint32_t cos_bw;
    uint8_t  strict;
    uint8_t  pausable;
};

struct cos_help_data {
    struct cos_entry_help_data data[DCBX_COS_MAX_NUM];
    uint8_t num_of_cos;
};

#define DCBX_ILLEGAL_PG      (0xFF)
#define DCBX_PFC_PRI_MASK    (0xFF)
#define DCBX_STRICT_PRIORITY (15)
#define DCBX_INVALID_COS_BW  (0xFFFFFFFF)
#define DCBX_PFC_PRI_NON_PAUSE_MASK(sc)                      \
    ((sc)->dcbx_port_params.pfc.priority_non_pauseable_mask)
#define DCBX_PFC_PRI_PAUSE_MASK(sc)             \
    ((uint8_t)~DCBX_PFC_PRI_NON_PAUSE_MASK(sc))
#define DCBX_PFC_PRI_GET_PAUSE(sc, pg_pri)     \
    ((pg_pri) & (DCBX_PFC_PRI_PAUSE_MASK(sc)))
#define DCBX_PFC_PRI_GET_NON_PAUSE(sc, pg_pri)   \
    (DCBX_PFC_PRI_NON_PAUSE_MASK(sc) & (pg_pri))
#define DCBX_IS_PFC_PRI_SOME_PAUSE(sc, pg_pri) \
    (0 != DCBX_PFC_PRI_GET_PAUSE(sc, pg_pri))
#define IS_DCBX_PFC_PRI_ONLY_PAUSE(sc, pg_pri)         \
    (pg_pri == DCBX_PFC_PRI_GET_PAUSE((sc), (pg_pri)))
#define IS_DCBX_PFC_PRI_ONLY_NON_PAUSE(sc, pg_pri)           \
    ((pg_pri) == DCBX_PFC_PRI_GET_NON_PAUSE((sc), (pg_pri)))
#define IS_DCBX_PFC_PRI_MIX_PAUSE(sc, pg_pri)            \
    (!(IS_DCBX_PFC_PRI_ONLY_NON_PAUSE((sc), (pg_pri)) || \
       IS_DCBX_PFC_PRI_ONLY_PAUSE((sc), (pg_pri))))

struct pg_entry_help_data {
    uint8_t  num_of_dif_pri;
    uint8_t  pg;
    uint32_t pg_priority;
};

struct pg_help_data {
    struct pg_entry_help_data data[LLFC_DRIVER_TRAFFIC_TYPE_MAX];
    uint8_t num_of_pg;
};

/* forward DCB/PFC related declarations */
struct bxe_softc;
/* void bxe_dcbx_update(struct work_struct *work); */
void bxe_dcbx_init_params(struct bxe_softc *sc);
void bxe_dcbx_set_state(struct bxe_softc *sc, uint8_t dcb_on, uint32_t dcbx_enabled);
int  bxe_dcb_get_lldp_params_ioctl(struct bxe_softc *sc, void *uaddr);
int  bxe_dcb_get_dcbx_params_ioctl(struct bxe_softc *sc, void *uaddr);
int  bxe_dcb_set_dcbx_params_ioctl(struct bxe_softc *sc, void *uaddr);

enum {
    BXE_DCBX_STATE_NEG_RECEIVED = 0x1,
    BXE_DCBX_STATE_TX_PAUSED,
    BXE_DCBX_STATE_TX_RELEASED
};

void bxe_dcbx_set_params(struct bxe_softc *sc, uint32_t state);
void bxe_dcbx_pmf_update(struct bxe_softc *sc);

#endif /* BXE_DCB_H */

