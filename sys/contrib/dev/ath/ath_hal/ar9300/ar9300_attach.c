/*
 * Copyright (c) 2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include "opt_ah.h"

#include "ah.h"
#include "ah_internal.h"
#include "ah_devid.h"

#include "ar9300/ar9300desc.h"
#include "ar9300/ar9300.h"
#include "ar9300/ar9300reg.h"
#include "ar9300/ar9300phy.h"
#include "ar9300/ar9300paprd.h"

#include "ar9300/ar9300_stub.h"
#include "ar9300/ar9300_stub_funcs.h"


/* Add static register initialization vectors */
#include "ar9300/ar9300_osprey22.ini"
#include "ar9300/ar9330_11.ini"
#include "ar9300/ar9330_12.ini"
#include "ar9300/ar9340.ini"
#include "ar9300/ar9485.ini"
#include "ar9300/ar9485_1_1.ini"
#include "ar9300/ar9300_jupiter10.ini"
/* TODO: convert the 2.0 code to use the new initvals from ath9k */
#include "ar9300/ar9300_jupiter20.ini"
#include "ar9300/ar9462_2p0_initvals.h"
#include "ar9300/ar9462_2p1_initvals.h"
#include "ar9300/ar9580.ini"
#include "ar9300/ar955x.ini"
#include "ar9300/ar953x.ini"
#include "ar9300/ar9300_aphrodite10.ini"


/* Include various freebsd specific HAL methods */
#include "ar9300/ar9300_freebsd.h"

/* XXX duplicate in ar9300_radio.c ? */
static HAL_BOOL ar9300_get_chip_power_limits(struct ath_hal *ah,
    struct ieee80211_channel *chan);

static inline HAL_STATUS ar9300_init_mac_addr(struct ath_hal *ah);
static inline HAL_STATUS ar9300_hw_attach(struct ath_hal *ah);
static inline void ar9300_hw_detach(struct ath_hal *ah);
static int16_t ar9300_get_nf_adjust(struct ath_hal *ah,
    const HAL_CHANNEL_INTERNAL *c);
#if 0
int ar9300_get_cal_intervals(struct ath_hal *ah, HAL_CALIBRATION_TIMER **timerp,
    HAL_CAL_QUERY query);
#endif

#if ATH_TRAFFIC_FAST_RECOVER
unsigned long ar9300_get_pll3_sqsum_dvc(struct ath_hal *ah);
#endif
static int ar9300_init_offsets(struct ath_hal *ah, u_int16_t devid);


static void
ar9300_disable_pcie_phy(struct ath_hal *ah);

static const HAL_PERCAL_DATA iq_cal_single_sample =
                          {IQ_MISMATCH_CAL,
                          MIN_CAL_SAMPLES,
                          PER_MAX_LOG_COUNT,
                          ar9300_iq_cal_collect,
                          ar9300_iq_calibration};

#if 0
static HAL_CALIBRATION_TIMER ar9300_cals[] =
                          { {IQ_MISMATCH_CAL,               /* Cal type */
                             1200000,                       /* Cal interval */
                             0                              /* Cal timestamp */
                            },
                          {TEMP_COMP_CAL,
                             5000,
                             0
                            },
                          };
#endif

#if ATH_PCIE_ERROR_MONITOR

int ar9300_start_pcie_error_monitor(struct ath_hal *ah, int b_auto_stop)
{
    u_int32_t val;

    /* Clear the counters */
    OS_REG_WRITE(ah, PCIE_CO_ERR_CTR_CTR0, 0);
    OS_REG_WRITE(ah, PCIE_CO_ERR_CTR_CTR1, 0);
    
    /* Read the previous value */
    val = OS_REG_READ(ah, PCIE_CO_ERR_CTR_CTRL);

    /* Set auto_stop */
    if (b_auto_stop) {
        val |=
            RCVD_ERR_CTR_AUTO_STOP | BAD_TLP_ERR_CTR_AUTO_STOP |
            BAD_DLLP_ERR_CTR_AUTO_STOP | RPLY_TO_ERR_CTR_AUTO_STOP |
            RPLY_NUM_RO_ERR_CTR_AUTO_STOP;
    } else {
        val &= ~(
            RCVD_ERR_CTR_AUTO_STOP | BAD_TLP_ERR_CTR_AUTO_STOP |
            BAD_DLLP_ERR_CTR_AUTO_STOP | RPLY_TO_ERR_CTR_AUTO_STOP |
            RPLY_NUM_RO_ERR_CTR_AUTO_STOP);
    }
    OS_REG_WRITE(ah, PCIE_CO_ERR_CTR_CTRL, val );

    /*
     * Start to run.
     * This has to be done separately from the above auto_stop flag setting,
     * to avoid a HW race condition.
     */
    val |=
        RCVD_ERR_CTR_RUN | BAD_TLP_ERR_CTR_RUN | BAD_DLLP_ERR_CTR_RUN |
        RPLY_TO_ERR_CTR_RUN | RPLY_NUM_RO_ERR_CTR_RUN;
    OS_REG_WRITE(ah, PCIE_CO_ERR_CTR_CTRL, val);

    return 0;
}

int ar9300_read_pcie_error_monitor(struct ath_hal *ah, void* p_read_counters)
{
    u_int32_t val;
    ar_pcie_error_moniter_counters *p_counters =
        (ar_pcie_error_moniter_counters*) p_read_counters;
    
    val = OS_REG_READ(ah, PCIE_CO_ERR_CTR_CTR0);
    
    p_counters->uc_receiver_errors = MS(val, RCVD_ERR_MASK);
    p_counters->uc_bad_tlp_errors  = MS(val, BAD_TLP_ERR_MASK);
    p_counters->uc_bad_dllp_errors = MS(val, BAD_DLLP_ERR_MASK);

    val = OS_REG_READ(ah, PCIE_CO_ERR_CTR_CTR1);
    
    p_counters->uc_replay_timeout_errors        = MS(val, RPLY_TO_ERR_MASK);
    p_counters->uc_replay_number_rollover_errors= MS(val, RPLY_NUM_RO_ERR_MASK);

    return 0;
}

int ar9300_stop_pcie_error_monitor(struct ath_hal *ah)
{
    u_int32_t val;
        
    /* Read the previous value */
    val = OS_REG_READ(ah, PCIE_CO_ERR_CTR_CTRL);
    
    val &= ~(
        RCVD_ERR_CTR_RUN |
        BAD_TLP_ERR_CTR_RUN |
        BAD_DLLP_ERR_CTR_RUN |
        RPLY_TO_ERR_CTR_RUN |
        RPLY_NUM_RO_ERR_CTR_RUN);
   
    /* Start to stop */
    OS_REG_WRITE(ah, PCIE_CO_ERR_CTR_CTRL, val );

    return 0;
}

#endif /* ATH_PCIE_ERROR_MONITOR */

#if 0
/* WIN32 does not support C99 */
static const struct ath_hal_private ar9300hal = {
    {
        ar9300_get_rate_table,             /* ah_get_rate_table */
        ar9300_detach,                     /* ah_detach */

        /* Reset Functions */
        ar9300_reset,                      /* ah_reset */
        ar9300_phy_disable,                /* ah_phy_disable */
        ar9300_disable,                    /* ah_disable */
        ar9300_config_pci_power_save,      /* ah_config_pci_power_save */
        ar9300_set_pcu_config,             /* ah_set_pcu_config */
        ar9300_calibration,                /* ah_per_calibration */
        ar9300_reset_cal_valid,            /* ah_reset_cal_valid */
        ar9300_set_tx_power_limit,         /* ah_set_tx_power_limit */

#if ATH_ANT_DIV_COMB
        ar9300_ant_ctrl_set_lna_div_use_bt_ant,     /* ah_ant_ctrl_set_lna_div_use_bt_ant */
#endif /* ATH_ANT_DIV_COMB */
#ifdef ATH_SUPPORT_DFS
        ar9300_radar_wait,                 /* ah_radar_wait */

        /* New DFS functions */
        ar9300_check_dfs,                  /* ah_ar_check_dfs */
        ar9300_dfs_found,                  /* ah_ar_dfs_found */
        ar9300_enable_dfs,                 /* ah_ar_enable_dfs */
        ar9300_get_dfs_thresh,             /* ah_ar_get_dfs_thresh */
        ar9300_get_dfs_radars,             /* ah_ar_get_dfs_radars */
        ar9300_adjust_difs,                /* ah_adjust_difs */
        ar9300_dfs_config_fft,             /* ah_dfs_config_fft */
        ar9300_dfs_cac_war,                /* ah_dfs_cac_war */
        ar9300_cac_tx_quiet,               /* ah_cac_tx_quiet */
#endif
        ar9300_get_extension_channel,      /* ah_get_extension_channel */
        ar9300_is_fast_clock_enabled,      /* ah_is_fast_clock_enabled */

        /* Transmit functions */
        ar9300_update_tx_trig_level,       /* ah_update_tx_trig_level */
        ar9300_get_tx_trig_level,          /* ah_get_tx_trig_level */
        ar9300_setup_tx_queue,             /* ah_setup_tx_queue */
        ar9300_set_tx_queue_props,         /* ah_set_tx_queue_props */
        ar9300_get_tx_queue_props,         /* ah_get_tx_queue_props */
        ar9300_release_tx_queue,           /* ah_release_tx_queue */
        ar9300_reset_tx_queue,             /* ah_reset_tx_queue */
        ar9300_get_tx_dp,                  /* ah_get_tx_dp */
        ar9300_set_tx_dp,                  /* ah_set_tx_dp */
        ar9300_num_tx_pending,             /* ah_num_tx_pending */
        ar9300_start_tx_dma,               /* ah_start_tx_dma */
        ar9300_stop_tx_dma,                /* ah_stop_tx_dma */
        ar9300_stop_tx_dma_indv_que,       /* ah_stop_tx_dma_indv_que */
        ar9300_abort_tx_dma,               /* ah_abort_tx_dma */
        ar9300_fill_tx_desc,               /* ah_fill_tx_desc */
        ar9300_set_desc_link,              /* ah_set_desc_link */
        ar9300_get_desc_link_ptr,          /* ah_get_desc_link_ptr */
        ar9300_clear_tx_desc_status,       /* ah_clear_tx_desc_status */
#ifdef ATH_SWRETRY
        ar9300_clear_dest_mask,            /* ah_clear_dest_mask */
#endif
        ar9300_proc_tx_desc,               /* ah_proc_tx_desc */
        ar9300_get_raw_tx_desc,            /* ah_get_raw_tx_desc */
        ar9300_get_tx_rate_code,           /* ah_get_tx_rate_code */
        AH_NULL,                           /* ah_get_tx_intr_queue */
        ar9300_tx_req_intr_desc,           /* ah_req_tx_intr_desc */
        ar9300_calc_tx_airtime,            /* ah_calc_tx_airtime */
        ar9300_setup_tx_status_ring,       /* ah_setup_tx_status_ring */

        /* RX Functions */
        ar9300_get_rx_dp,                  /* ah_get_rx_dp */
        ar9300_set_rx_dp,                  /* ah_set_rx_dp */
        ar9300_enable_receive,             /* ah_enable_receive */
        ar9300_stop_dma_receive,           /* ah_stop_dma_receive */
        ar9300_start_pcu_receive,          /* ah_start_pcu_receive */
        ar9300_stop_pcu_receive,           /* ah_stop_pcu_receive */
        ar9300_set_multicast_filter,       /* ah_set_multicast_filter */
        ar9300_get_rx_filter,              /* ah_get_rx_filter */
        ar9300_set_rx_filter,              /* ah_set_rx_filter */
        ar9300_set_rx_sel_evm,             /* ah_set_rx_sel_evm */
        ar9300_set_rx_abort,               /* ah_set_rx_abort */
        AH_NULL,                           /* ah_setup_rx_desc */
        ar9300_proc_rx_desc,               /* ah_proc_rx_desc */
        ar9300_get_rx_key_idx,             /* ah_get_rx_key_idx */
        ar9300_proc_rx_desc_fast,          /* ah_proc_rx_desc_fast */
        ar9300_ani_ar_poll,                /* ah_rx_monitor */
        ar9300_process_mib_intr,           /* ah_proc_mib_event */

        /* Misc Functions */
        ar9300_get_capability,             /* ah_get_capability */
        ar9300_set_capability,             /* ah_set_capability */
        ar9300_get_diag_state,             /* ah_get_diag_state */
        ar9300_get_mac_address,            /* ah_get_mac_address */
        ar9300_set_mac_address,            /* ah_set_mac_address */
        ar9300_get_bss_id_mask,            /* ah_get_bss_id_mask */
        ar9300_set_bss_id_mask,            /* ah_set_bss_id_mask */
        ar9300_set_regulatory_domain,      /* ah_set_regulatory_domain */
        ar9300_set_led_state,              /* ah_set_led_state */
        ar9300_set_power_led_state,        /* ah_setpowerledstate */
        ar9300_set_network_led_state,      /* ah_setnetworkledstate */
        ar9300_write_associd,              /* ah_write_associd */
        ar9300_force_tsf_sync,             /* ah_force_tsf_sync */
        ar9300_gpio_cfg_input,             /* ah_gpio_cfg_input */
        ar9300_gpio_cfg_output,            /* ah_gpio_cfg_output */
        ar9300_gpio_cfg_output_led_off,    /* ah_gpio_cfg_output_led_off */
        ar9300_gpio_get,                   /* ah_gpio_get */
        ar9300_gpio_set,                   /* ah_gpio_set */
        ar9300_gpio_get_intr,              /* ah_gpio_get_intr */
        ar9300_gpio_set_intr,              /* ah_gpio_set_intr */
        ar9300_gpio_get_polarity,          /* ah_gpio_get_polarity */
        ar9300_gpio_set_polarity,          /* ah_gpio_set_polarity */
        ar9300_gpio_get_mask,              /* ah_gpio_get_mask */
        ar9300_gpio_set_mask,              /* ah_gpio_set_mask */
        ar9300_get_tsf32,                  /* ah_get_tsf32 */
        ar9300_get_tsf64,                  /* ah_get_tsf64 */
        ar9300_get_tsf2_32,                /* ah_get_tsf2_32 */
        ar9300_reset_tsf,                  /* ah_reset_tsf */
        ar9300_detect_card_present,        /* ah_detect_card_present */
        ar9300_update_mib_mac_stats,       /* ah_update_mib_mac_stats */
        ar9300_get_mib_mac_stats,          /* ah_get_mib_mac_stats */
        ar9300_get_rfgain,                 /* ah_get_rf_gain */
        ar9300_get_def_antenna,            /* ah_get_def_antenna */
        ar9300_set_def_antenna,            /* ah_set_def_antenna */
        ar9300_set_slot_time,              /* ah_set_slot_time */
        ar9300_set_ack_timeout,            /* ah_set_ack_timeout */
        ar9300_get_ack_timeout,            /* ah_get_ack_timeout */
        ar9300_set_coverage_class,         /* ah_set_coverage_class */
        ar9300_set_quiet,                  /* ah_set_quiet */
        ar9300_set_antenna_switch,         /* ah_set_antenna_switch */
        ar9300_get_desc_info,              /* ah_get_desc_info */
        ar9300_select_ant_config,          /* ah_select_ant_config */
        ar9300_ant_ctrl_common_get,        /* ah_ant_ctrl_common_get */
        ar9300_ant_swcom_sel,              /* ah_ant_swcom_sel */
        ar9300_enable_tpc,                 /* ah_enable_tpc */
        AH_NULL,                           /* ah_olpc_temp_compensation */
#if ATH_SUPPORT_CRDC
        ar9300_chain_rssi_diff_compensation,/*ah_chain_rssi_diff_compensation*/
#endif
        ar9300_disable_phy_restart,        /* ah_disable_phy_restart */
        ar9300_enable_keysearch_always,
        ar9300_interference_is_present,    /* ah_interference_is_present */
        ar9300_disp_tpc_tables,             /* ah_disp_tpc_tables */
        ar9300_get_tpc_tables,              /* ah_get_tpc_tables */
        /* Key Cache Functions */
        ar9300_get_key_cache_size,         /* ah_get_key_cache_size */
        ar9300_reset_key_cache_entry,      /* ah_reset_key_cache_entry */
        ar9300_is_key_cache_entry_valid,   /* ah_is_key_cache_entry_valid */
        ar9300_set_key_cache_entry,        /* ah_set_key_cache_entry */
        ar9300_set_key_cache_entry_mac,    /* ah_set_key_cache_entry_mac */
        ar9300_print_keycache,             /* ah_print_key_cache */
#if ATH_SUPPORT_KEYPLUMB_WAR
        ar9300_check_key_cache_entry,      /* ah_check_key_cache_entry */
#endif
        /* Power Management Functions */
        ar9300_set_power_mode,             /* ah_set_power_mode */
        ar9300_set_sm_power_mode,          /* ah_set_sm_ps_mode */
#if ATH_WOW
        ar9300_wow_apply_pattern,          /* ah_wow_apply_pattern */
        ar9300_wow_enable,                 /* ah_wow_enable */
        ar9300_wow_wake_up,                /* ah_wow_wake_up */
#if ATH_WOW_OFFLOAD
        ar9300_wowoffload_prep,                 /* ah_wow_offload_prep */
        ar9300_wowoffload_post,                 /* ah_wow_offload_post */
        ar9300_wowoffload_download_rekey_data,  /* ah_wow_offload_download_rekey_data */
        ar9300_wowoffload_retrieve_data,        /* ah_wow_offload_retrieve_data */
        ar9300_wowoffload_download_acer_magic,  /* ah_wow_offload_download_acer_magic */
        ar9300_wowoffload_download_acer_swka,   /* ah_wow_offload_download_acer_swka */
        ar9300_wowoffload_download_arp_info,    /* ah_wow_offload_download_arp_info */
        ar9300_wowoffload_download_ns_info,     /* ah_wow_offload_download_ns_info */
#endif /* ATH_WOW_OFFLOAD */
#endif

        /* Get Channel Noise */
        ath_hal_get_chan_noise,            /* ah_get_chan_noise */
        ar9300_chain_noise_floor,          /* ah_get_chain_noise_floor */
        ar9300_get_nf_from_reg,            /* ah_get_nf_from_reg */
        ar9300_get_rx_nf_offset,           /* ah_get_rx_nf_offset */

        /* Beacon Functions */
        ar9300_beacon_init,                /* ah_beacon_init */
        ar9300_set_sta_beacon_timers,      /* ah_set_station_beacon_timers */

        /* Interrupt Functions */
        ar9300_is_interrupt_pending,       /* ah_is_interrupt_pending */
        ar9300_get_pending_interrupts,     /* ah_get_pending_interrupts */
        ar9300_get_interrupts,             /* ah_get_interrupts */
        ar9300_set_interrupts,             /* ah_set_interrupts */
        ar9300_set_intr_mitigation_timer,  /* ah_set_intr_mitigation_timer */
        ar9300_get_intr_mitigation_timer,  /* ah_get_intr_mitigation_timer */
	ar9300ForceVCS,
        ar9300SetDfs3StreamFix,
        ar9300Get3StreamSignature,

        /* 11n specific functions (NOT applicable to ar9300) */
        ar9300_set_11n_tx_desc,            /* ah_set_11n_tx_desc */
        /* Update rxchain */
        ar9300_set_rx_chainmask,           /*ah_set_rx_chainmask*/
        /*Updating locationing register */
        ar9300_update_loc_ctl_reg,         /*ah_update_loc_ctl_reg*/
        /* Start PAPRD functions  */
        ar9300_set_paprd_tx_desc,          /* ah_set_paprd_tx_desc */
        ar9300_paprd_init_table,           /* ah_paprd_init_table */
        ar9300_paprd_setup_gain_table,     /* ah_paprd_setup_gain_table */
        ar9300_paprd_create_curve,         /* ah_paprd_create_curve */
        ar9300_paprd_is_done,              /* ah_paprd_is_done */
        ar9300_enable_paprd,               /* ah_PAPRDEnable */
        ar9300_populate_paprd_single_table,/* ah_paprd_populate_table */
        ar9300_is_tx_done,                 /* ah_is_tx_done */
        ar9300_paprd_dec_tx_pwr,            /* ah_paprd_dec_tx_pwr*/
        ar9300_paprd_thermal_send,         /* ah_paprd_thermal_send */
        /* End PAPRD functions */
        ar9300_set_11n_rate_scenario,      /* ah_set_11n_rate_scenario */
        ar9300_set_11n_aggr_first,         /* ah_set_11n_aggr_first */
        ar9300_set_11n_aggr_middle,        /* ah_set_11n_aggr_middle */
        ar9300_set_11n_aggr_last,          /* ah_set_11n_aggr_last */
        ar9300_clr_11n_aggr,               /* ah_clr_11n_aggr */
        ar9300_set_11n_rifs_burst_middle,  /* ah_set_11n_rifs_burst_middle */
        ar9300_set_11n_rifs_burst_last,    /* ah_set_11n_rifs_burst_last */
        ar9300_clr_11n_rifs_burst,         /* ah_clr_11n_rifs_burst */
        ar9300_set_11n_aggr_rifs_burst,    /* ah_set_11n_aggr_rifs_burst */
        ar9300_set_11n_rx_rifs,            /* ah_set_11n_rx_rifs */
        ar9300_set_smart_antenna,             /* ah_setSmartAntenna */
        ar9300_detect_bb_hang,             /* ah_detect_bb_hang */
        ar9300_detect_mac_hang,            /* ah_detect_mac_hang */
        ar9300_set_immunity,               /* ah_immunity */
        ar9300_get_hw_hangs,               /* ah_get_hang_types */
        ar9300_set_11n_burst_duration,     /* ah_set_11n_burst_duration */
        ar9300_set_11n_virtual_more_frag,  /* ah_set_11n_virtual_more_frag */
        ar9300_get_11n_ext_busy,           /* ah_get_11n_ext_busy */
        ar9300_set_11n_mac2040,            /* ah_set_11n_mac2040 */
        ar9300_get_11n_rx_clear,           /* ah_get_11n_rx_clear */
        ar9300_set_11n_rx_clear,           /* ah_set_11n_rx_clear */
        ar9300_get_mib_cycle_counts_pct,   /* ah_get_mib_cycle_counts_pct */
        ar9300_dma_reg_dump,               /* ah_dma_reg_dump */

        /* force_ppm specific functions */
        ar9300_ppm_get_rssi_dump,          /* ah_ppm_get_rssi_dump */
        ar9300_ppm_arm_trigger,            /* ah_ppm_arm_trigger */
        ar9300_ppm_get_trigger,            /* ah_ppm_get_trigger */
        ar9300_ppm_force,                  /* ah_ppm_force */
        ar9300_ppm_un_force,               /* ah_ppm_un_force */
        ar9300_ppm_get_force_state,        /* ah_ppm_get_force_state */

        ar9300_get_spur_info,              /* ah_get_spur_info */
        ar9300_set_spur_info,              /* ah_get_spur_info */

        ar9300_get_min_cca_pwr,            /* ah_ar_get_noise_floor_val */

        ar9300_green_ap_ps_on_off,         /* ah_set_rx_green_ap_ps_on_off */
        ar9300_is_single_ant_power_save_possible, /* ah_is_single_ant_power_save_possible */

        /* radio measurement specific functions */
        ar9300_get_mib_cycle_counts,       /* ah_get_mib_cycle_counts */
        ar9300_get_vow_stats,              /* ah_get_vow_stats */
        ar9300_clear_mib_counters,         /* ah_clear_mib_counters */
#if ATH_GEN_RANDOMNESS
        ar9300_get_rssi_chain0,            /* ah_get_rssi_chain0 */
#endif
#ifdef ATH_BT_COEX
        /* Bluetooth Coexistence functions */
        ar9300_set_bt_coex_info,           /* ah_set_bt_coex_info */
        ar9300_bt_coex_config,             /* ah_bt_coex_config */
        ar9300_bt_coex_set_qcu_thresh,     /* ah_bt_coex_set_qcu_thresh */
        ar9300_bt_coex_set_weights,        /* ah_bt_coex_set_weights */
        ar9300_bt_coex_setup_bmiss_thresh, /* ah_bt_coex_set_bmiss_thresh */
        ar9300_bt_coex_set_parameter,      /* ah_bt_coex_set_parameter */
        ar9300_bt_coex_disable,            /* ah_bt_coex_disable */
        ar9300_bt_coex_enable,             /* ah_bt_coex_enable */
        ar9300_get_bt_active_gpio,         /* ah_bt_coex_info*/
        ar9300_get_wlan_active_gpio,       /* ah__coex_wlan_info*/
#endif
        /* Generic Timer functions */
        ar9300_alloc_generic_timer,        /* ah_gentimer_alloc */
        ar9300_free_generic_timer,         /* ah_gentimer_free */
        ar9300_start_generic_timer,        /* ah_gentimer_start */
        ar9300_stop_generic_timer,         /* ah_gentimer_stop */
        ar9300_get_gen_timer_interrupts,   /* ah_gentimer_get_intr */

        ar9300_set_dcs_mode,               /* ah_set_dcs_mode */
        ar9300_get_dcs_mode,               /* ah_get_dcs_mode */
        
#if ATH_ANT_DIV_COMB
        ar9300_ant_div_comb_get_config,    /* ah_get_ant_dvi_comb_conf */
        ar9300_ant_div_comb_set_config,    /* ah_set_ant_dvi_comb_conf */
#endif

        ar9300_get_bb_panic_info,          /* ah_get_bb_panic_info */
        ar9300_handle_radar_bb_panic,      /* ah_handle_radar_bb_panic */
        ar9300_set_hal_reset_reason,       /* ah_set_hal_reset_reason */

#if ATH_PCIE_ERROR_MONITOR
        ar9300_start_pcie_error_monitor,   /* ah_start_pcie_error_monitor */
        ar9300_read_pcie_error_monitor,    /* ah_read_pcie_error_monitor*/
        ar9300_stop_pcie_error_monitor,    /* ah_stop_pcie_error_monitor*/
#endif /* ATH_PCIE_ERROR_MONITOR */

#if ATH_SUPPORT_SPECTRAL        
        /* Spectral scan */
        ar9300_configure_spectral_scan,    /* ah_ar_configure_spectral */
        ar9300_get_spectral_params,        /* ah_ar_get_spectral_config */
        ar9300_start_spectral_scan,        /* ah_ar_start_spectral_scan */
        ar9300_stop_spectral_scan,         /* ah_ar_stop_spectral_scan */
        ar9300_is_spectral_enabled,        /* ah_ar_is_spectral_enabled */
        ar9300_is_spectral_active,         /* ah_ar_is_spectral_active */
        ar9300_get_ctl_chan_nf,            /* ah_ar_get_ctl_nf */
        ar9300_get_ext_chan_nf,            /* ah_ar_get_ext_nf */
#endif  /*  ATH_SUPPORT_SPECTRAL */ 


        ar9300_promisc_mode,               /* ah_promisc_mode */
        ar9300_read_pktlog_reg,            /* ah_read_pktlog_reg */
        ar9300_write_pktlog_reg,           /* ah_write_pktlog_reg */
        ar9300_set_proxy_sta,              /* ah_set_proxy_sta */
        ar9300_get_cal_intervals,          /* ah_get_cal_intervals */
#if ATH_TRAFFIC_FAST_RECOVER
        ar9300_get_pll3_sqsum_dvc,         /* ah_get_pll3_sqsum_dvc */
#endif
#ifdef ATH_SUPPORT_HTC
        AH_NULL,
#endif

#ifdef ATH_TX99_DIAG
        /* Tx99 functions */
#ifdef ATH_SUPPORT_HTC
        AH_NULL,
        AH_NULL,
        AH_NULL,
        AH_NULL,
        AH_NULL,
        AH_NULL,
        AH_NULL,
#else
        AH_NULL,
        AH_NULL,
        ar9300_tx99_channel_pwr_update,		/* ah_tx99channelpwrupdate */
        ar9300_tx99_start,					/* ah_tx99start */
        ar9300_tx99_stop,					/* ah_tx99stop */
        ar9300_tx99_chainmsk_setup,			/* ah_tx99_chainmsk_setup */
        ar9300_tx99_set_single_carrier,		/* ah_tx99_set_single_carrier */
#endif
#endif
        ar9300_chk_rssi_update_tx_pwr,
        ar9300_is_skip_paprd_by_greentx,   /* ah_is_skip_paprd_by_greentx */
        ar9300_hwgreentx_set_pal_spare,    /* ah_hwgreentx_set_pal_spare */
#if ATH_SUPPORT_MCI
        /* MCI Coexistence Functions */
        ar9300_mci_setup,                   /* ah_mci_setup */
        ar9300_mci_send_message,            /* ah_mci_send_message */
        ar9300_mci_get_interrupt,           /* ah_mci_get_interrupt */
        ar9300_mci_state,                   /* ah_mci_state */
        ar9300_mci_detach,                  /* ah_mci_detach */
#endif
        ar9300_reset_hw_beacon_proc_crc,   /* ah_reset_hw_beacon_proc_crc */
        ar9300_get_hw_beacon_rssi,         /* ah_get_hw_beacon_rssi */
        ar9300_set_hw_beacon_rssi_threshold,/*ah_set_hw_beacon_rssi_threshold*/
        ar9300_reset_hw_beacon_rssi,       /* ah_reset_hw_beacon_rssi */
        ar9300_mat_enable,                 /* ah_mat_enable */
        ar9300_dump_keycache,              /* ah_dump_keycache */
        ar9300_is_ani_noise_spur,         /* ah_is_ani_noise_spur */
        ar9300_set_hw_beacon_proc,         /* ah_set_hw_beacon_proc */
        ar9300_set_ctl_pwr,                 /* ah_set_ctl_pwr */
        ar9300_set_txchainmaskopt,          /* ah_set_txchainmaskopt */
    },

    ar9300_get_channel_edges,              /* ah_get_channel_edges */
    ar9300_get_wireless_modes,             /* ah_get_wireless_modes */
    ar9300_eeprom_read_word,               /* ah_eeprom_read */
    AH_NULL,
    ar9300_eeprom_dump_support,            /* ah_eeprom_dump */
    ar9300_get_chip_power_limits,          /* ah_get_chip_power_limits */

    ar9300_get_nf_adjust,                  /* ah_get_nf_adjust */
    /* rest is zero'd by compiler */
};
#endif

/*
 * Read MAC version/revision information from Chip registers and initialize
 * local data structures.
 */
void
ar9300_read_revisions(struct ath_hal *ah)
{
    u_int32_t val;

    /* XXX verify if this is the correct way to read revision on Osprey */
    /* new SREV format for Sowl and later */
    val = OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_SREV));

    if (AH_PRIVATE(ah)->ah_devid == AR9300_DEVID_AR9340) {
        /* XXX: AR_SREV register in Wasp reads 0 */
        AH_PRIVATE(ah)->ah_macVersion = AR_SREV_VERSION_WASP;
    } else if(AH_PRIVATE(ah)->ah_devid == AR9300_DEVID_QCA955X) {
        /* XXX: AR_SREV register in Scorpion reads 0 */
       AH_PRIVATE(ah)->ah_macVersion = AR_SREV_VERSION_SCORPION;
    } else if(AH_PRIVATE(ah)->ah_devid == AR9300_DEVID_QCA953X) {
        /* XXX: AR_SREV register in HoneyBEE reads 0 */
       AH_PRIVATE(ah)->ah_macVersion = AR_SREV_VERSION_HONEYBEE;
    } else {
        /*
         * Include 6-bit Chip Type (masked to 0)
         * to differentiate from pre-Sowl versions
         */
        AH_PRIVATE(ah)->ah_macVersion =
            (val & AR_SREV_VERSION2) >> AR_SREV_TYPE2_S;
    }





#ifdef AH_SUPPORT_HORNET
    /*
     *  EV74984, due to Hornet 1.1 didn't update WMAC revision,
     *  so that have to read SoC's revision ID instead
     */
    if (AH_PRIVATE(ah)->ah_macVersion == AR_SREV_VERSION_HORNET) {
#define AR_SOC_RST_REVISION_ID         0xB8060090
#define REG_READ(_reg)                 *((volatile u_int32_t *)(_reg))
        if ((REG_READ(AR_SOC_RST_REVISION_ID) & AR_SREV_REVISION_HORNET_11_MASK)
            == AR_SREV_REVISION_HORNET_11)
        {
            AH_PRIVATE(ah)->ah_macRev = AR_SREV_REVISION_HORNET_11;
        } else {
            AH_PRIVATE(ah)->ah_macRev = MS(val, AR_SREV_REVISION2);
        }
#undef REG_READ
#undef AR_SOC_RST_REVISION_ID
    } else
#endif
    if (AH_PRIVATE(ah)->ah_macVersion == AR_SREV_VERSION_WASP)
    {
#define AR_SOC_RST_REVISION_ID         0xB8060090
#define REG_READ(_reg)                 *((volatile u_int32_t *)(_reg))

        AH_PRIVATE(ah)->ah_macRev = 
            REG_READ(AR_SOC_RST_REVISION_ID) & AR_SREV_REVISION_WASP_MASK; 
#undef REG_READ
#undef AR_SOC_RST_REVISION_ID
    }
    else
        AH_PRIVATE(ah)->ah_macRev = MS(val, AR_SREV_REVISION2);

    if (AR_SREV_JUPITER(ah) || AR_SREV_APHRODITE(ah)) {
        AH_PRIVATE(ah)->ah_ispcie = AH_TRUE;
    }
    else {
        AH_PRIVATE(ah)->ah_ispcie = 
            (val & AR_SREV_TYPE2_HOST_MODE) ? 0 : 1;
    }
    
}

/*
 * Attach for an AR9300 part.
 */
struct ath_hal *
ar9300_attach(u_int16_t devid, HAL_SOFTC sc, HAL_BUS_TAG st,
  HAL_BUS_HANDLE sh, uint16_t *eepromdata, HAL_OPS_CONFIG *ah_config,
  HAL_STATUS *status)
{
    struct ath_hal_9300     *ahp;
    struct ath_hal          *ah;
    struct ath_hal_private  *ahpriv;
    HAL_STATUS              ecode;

    HAL_NO_INTERSPERSED_READS;

    /* NB: memory is returned zero'd */
    ahp = ar9300_new_state(devid, sc, st, sh, eepromdata, ah_config, status);
    if (ahp == AH_NULL) {
        return AH_NULL;
    }
    ah = &ahp->ah_priv.h;
    ar9300_init_offsets(ah, devid);
    ahpriv = AH_PRIVATE(ah);
//    AH_PRIVATE(ah)->ah_bustype = bustype;

    /* FreeBSD: to make OTP work for now, provide this.. */
    AH9300(ah)->ah_cal_mem = ath_hal_malloc(HOST_CALDATA_SIZE);
    if (AH9300(ah)->ah_cal_mem == NULL) {
        ath_hal_printf(ah, "%s: caldata malloc failed!\n", __func__);
        ecode = HAL_EIO;
        goto bad;
    }

    /*
     * If eepromdata is not NULL, copy it it into ah_cal_mem.
     */
    if (eepromdata != NULL)
        OS_MEMCPY(AH9300(ah)->ah_cal_mem, eepromdata, HOST_CALDATA_SIZE);

    /* XXX FreeBSD: enable RX mitigation */
    ah->ah_config.ath_hal_intr_mitigation_rx = 1;

    /* interrupt mitigation */
#ifdef AR5416_INT_MITIGATION
    if (ah->ah_config.ath_hal_intr_mitigation_rx != 0) {
        ahp->ah_intr_mitigation_rx = AH_TRUE;
    }
#else
    /* Enable Rx mitigation (default) */
    ahp->ah_intr_mitigation_rx = AH_TRUE;
    ah->ah_config.ath_hal_intr_mitigation_rx = 1;

#endif
#ifdef HOST_OFFLOAD
    /* Reset default Rx mitigation values for Hornet */
    if (AR_SREV_HORNET(ah)) {
        ahp->ah_intr_mitigation_rx = AH_FALSE;
#ifdef AR5416_INT_MITIGATION
        ah->ah_config.ath_hal_intr_mitigation_rx = 0;
#endif
    }
#endif

    if (ah->ah_config.ath_hal_intr_mitigation_tx != 0) {
        ahp->ah_intr_mitigation_tx = AH_TRUE;
    }

    /*
     * Read back AR_WA into a permanent copy and set bits 14 and 17. 
     * We need to do this to avoid RMW of this register. 
     * Do this before calling ar9300_set_reset_reg. 
     * If not, the AR_WA register which was inited via EEPROM
     * will get wiped out.
     */
    ahp->ah_wa_reg_val = OS_REG_READ(ah,  AR_HOSTIF_REG(ah, AR_WA));
    /* Set Bits 14 and 17 in the AR_WA register. */
    ahp->ah_wa_reg_val |=
        AR_WA_D3_TO_L1_DISABLE | AR_WA_ASPM_TIMER_BASED_DISABLE;
    
    if (!ar9300_set_reset_reg(ah, HAL_RESET_POWER_ON)) {    /* reset chip */
        HALDEBUG(ah, HAL_DEBUG_RESET, "%s: couldn't reset chip\n", __func__);
        ecode = HAL_EIO;
        goto bad;
    }

    if (AR_SREV_JUPITER(ah)
#if ATH_WOW_OFFLOAD
        && !HAL_WOW_CTRL(ah, HAL_WOW_OFFLOAD_SET_4004_BIT14)
#endif
        )
    {
        /* Jupiter doesn't need bit 14 to be set. */
        ahp->ah_wa_reg_val &= ~AR_WA_D3_TO_L1_DISABLE;
        OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_WA), ahp->ah_wa_reg_val);
    }

#if ATH_SUPPORT_MCI
    if (AR_SREV_JUPITER(ah) || AR_SREV_APHRODITE(ah)) {
#if 1
        ah->ah_btCoexSetWeights = ar9300_mci_bt_coex_set_weights;
        ah->ah_btCoexDisable = ar9300_mci_bt_coex_disable;
        ah->ah_btCoexEnable = ar9300_mci_bt_coex_enable;
#endif
        ahp->ah_mci_ready = AH_FALSE;
        ahp->ah_mci_bt_state = MCI_BT_SLEEP;
        ahp->ah_mci_coex_major_version_wlan = MCI_GPM_COEX_MAJOR_VERSION_WLAN;
        ahp->ah_mci_coex_minor_version_wlan = MCI_GPM_COEX_MINOR_VERSION_WLAN;
        ahp->ah_mci_coex_major_version_bt = MCI_GPM_COEX_MAJOR_VERSION_DEFAULT;
        ahp->ah_mci_coex_minor_version_bt = MCI_GPM_COEX_MINOR_VERSION_DEFAULT;
        ahp->ah_mci_coex_bt_version_known = AH_FALSE;
        ahp->ah_mci_coex_2g5g_update = AH_TRUE; /* track if 2g5g status sent */
        /* will be updated before boot up sequence */
        ahp->ah_mci_coex_is_2g = AH_TRUE;
        ahp->ah_mci_coex_wlan_channels_update = AH_FALSE;
        ahp->ah_mci_coex_wlan_channels[0] = 0x00000000;
        ahp->ah_mci_coex_wlan_channels[1] = 0xffffffff;
        ahp->ah_mci_coex_wlan_channels[2] = 0xffffffff;
        ahp->ah_mci_coex_wlan_channels[3] = 0x7fffffff;
        ahp->ah_mci_query_bt = AH_TRUE; /* In case WLAN start after BT */
        ahp->ah_mci_unhalt_bt_gpm = AH_TRUE; /* Send UNHALT at beginning */
        ahp->ah_mci_halted_bt_gpm = AH_FALSE; /* Allow first HALT */
        ahp->ah_mci_need_flush_btinfo = AH_FALSE;
        ahp->ah_mci_wlan_cal_seq = 0;
        ahp->ah_mci_wlan_cal_done = 0;
    }
#endif /* ATH_SUPPORT_MCI */

#if ATH_WOW_OFFLOAD
    ahp->ah_mcast_filter_l32_set = 0;
    ahp->ah_mcast_filter_u32_set = 0;
#endif

    if (AR_SREV_HORNET(ah)) {
#ifdef AH_SUPPORT_HORNET
        if (!AR_SREV_HORNET_11(ah)) {
            /*
             * Do not check bootstrap register, which cannot be trusted
             * due to s26 switch issue on CUS164/AP121.
             */
            ahp->clk_25mhz = 1;
            HALDEBUG(AH_NULL, HAL_DEBUG_UNMASKABLE, "Bootstrap clock 25MHz\n");
        } else {
            /* check bootstrap clock setting */
#define AR_SOC_SEL_25M_40M         0xB80600AC
#define REG_WRITE(_reg, _val)    *((volatile u_int32_t *)(_reg)) = (_val);
#define REG_READ(_reg)          (*((volatile u_int32_t *)(_reg)))
            if (REG_READ(AR_SOC_SEL_25M_40M) & 0x1) {
                ahp->clk_25mhz = 0;
                HALDEBUG(AH_NULL, HAL_DEBUG_UNMASKABLE,
                    "Bootstrap clock 40MHz\n");
            } else {
                ahp->clk_25mhz = 1;
                HALDEBUG(AH_NULL, HAL_DEBUG_UNMASKABLE,
                    "Bootstrap clock 25MHz\n");
            }
#undef REG_READ
#undef REG_WRITE
#undef AR_SOC_SEL_25M_40M
        }
#endif /* AH_SUPPORT_HORNET */
    }

    if (AR_SREV_WASP(ah) || AR_SREV_SCORPION(ah)) {
        /* check bootstrap clock setting */
#define AR9340_SOC_SEL_25M_40M         0xB80600B0
#define AR9340_REF_CLK_40              (1 << 4) /* 0 - 25MHz   1 - 40 MHz */
#define REG_READ(_reg)          (*((volatile u_int32_t *)(_reg)))
        if (REG_READ(AR9340_SOC_SEL_25M_40M) & AR9340_REF_CLK_40) {
            ahp->clk_25mhz = 0;
            HALDEBUG(AH_NULL, HAL_DEBUG_UNMASKABLE, "Bootstrap clock 40MHz\n");
        } else {
            ahp->clk_25mhz = 1;
            HALDEBUG(AH_NULL, HAL_DEBUG_UNMASKABLE, "Bootstrap clock 25MHz\n");
        }
#undef REG_READ
#undef AR9340_SOC_SEL_25M_40M
#undef AR9340_REF_CLK_40
    }

    if (AR_SREV_HONEYBEE(ah)) {
            ahp->clk_25mhz = 1;
    }

    ar9300_init_pll(ah, AH_NULL);

    if (!ar9300_set_power_mode(ah, HAL_PM_AWAKE, AH_TRUE)) {
        HALDEBUG(ah, HAL_DEBUG_RESET, "%s: couldn't wakeup chip\n", __func__);
        ecode = HAL_EIO;
        goto bad;
    }

    /* No serialization of Register Accesses needed. */
    ah->ah_config.ah_serialise_reg_war = SER_REG_MODE_OFF;
    HALDEBUG(ah, HAL_DEBUG_RESET, "%s: ah_serialise_reg_war is %d\n",
             __func__, ah->ah_config.ah_serialise_reg_war);

    /*
     * Add mac revision check when needed.
     * - Osprey 1.0 and 2.0 no longer supported.
     */
    if (((ahpriv->ah_macVersion == AR_SREV_VERSION_OSPREY) &&
          (ahpriv->ah_macRev <= AR_SREV_REVISION_OSPREY_20)) ||
        (ahpriv->ah_macVersion != AR_SREV_VERSION_OSPREY &&
        ahpriv->ah_macVersion != AR_SREV_VERSION_WASP && 
        ahpriv->ah_macVersion != AR_SREV_VERSION_HORNET &&
        ahpriv->ah_macVersion != AR_SREV_VERSION_POSEIDON &&
        ahpriv->ah_macVersion != AR_SREV_VERSION_SCORPION &&
        ahpriv->ah_macVersion != AR_SREV_VERSION_HONEYBEE &&
        ahpriv->ah_macVersion != AR_SREV_VERSION_JUPITER &&
        ahpriv->ah_macVersion != AR_SREV_VERSION_APHRODITE) ) {
        HALDEBUG(ah, HAL_DEBUG_RESET,
            "%s: Mac Chip Rev 0x%02x.%x is not supported by this driver\n",
            __func__,
            ahpriv->ah_macVersion,
            ahpriv->ah_macRev);
        ecode = HAL_ENOTSUPP;
        goto bad;
    }

    AH_PRIVATE(ah)->ah_phyRev = OS_REG_READ(ah, AR_PHY_CHIP_ID);

    /* Setup supported calibrations */
    ahp->ah_iq_cal_data.cal_data = &iq_cal_single_sample;
    ahp->ah_supp_cals = IQ_MISMATCH_CAL;

    /* Enable ANI */
    ahp->ah_ani_function = HAL_ANI_ALL;

    /* Enable RIFS */
    ahp->ah_rifs_enabled = AH_TRUE;

    /* by default, stop RX also in abort txdma, due to
       "Unable to stop TxDMA" msg observed */
    ahp->ah_abort_txdma_norx = AH_TRUE;

    /* do not use optional tx chainmask by default */
    ahp->ah_tx_chainmaskopt = 0;

    ahp->ah_skip_rx_iq_cal = AH_FALSE;
    ahp->ah_rx_cal_complete = AH_FALSE;
    ahp->ah_rx_cal_chan = 0;
    ahp->ah_rx_cal_chan_flag = 0;

    HALDEBUG(ah, HAL_DEBUG_RESET,
        "%s: This Mac Chip Rev 0x%02x.%x is \n", __func__,
        ahpriv->ah_macVersion,
        ahpriv->ah_macRev);

    if (AR_SREV_HORNET_12(ah)) {
        /* mac */
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_PRE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_CORE],
            ar9331_hornet1_2_mac_core,
            ARRAY_LENGTH(ar9331_hornet1_2_mac_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_POST],
            ar9331_hornet1_2_mac_postamble,
            ARRAY_LENGTH(ar9331_hornet1_2_mac_postamble), 5);

        /* bb */
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_PRE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_CORE],
            ar9331_hornet1_2_baseband_core,
            ARRAY_LENGTH(ar9331_hornet1_2_baseband_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_POST],
            ar9331_hornet1_2_baseband_postamble,
            ARRAY_LENGTH(ar9331_hornet1_2_baseband_postamble), 5);

        /* radio */
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_PRE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_CORE], 
            ar9331_hornet1_2_radio_core,
            ARRAY_LENGTH(ar9331_hornet1_2_radio_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_POST], NULL, 0, 0);

        /* soc */
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_PRE],
            ar9331_hornet1_2_soc_preamble,
            ARRAY_LENGTH(ar9331_hornet1_2_soc_preamble), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_CORE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_POST],
            ar9331_hornet1_2_soc_postamble,
            ARRAY_LENGTH(ar9331_hornet1_2_soc_postamble), 2);

        /* rx/tx gain */
        INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain,
            ar9331_common_rx_gain_hornet1_2, 
            ARRAY_LENGTH(ar9331_common_rx_gain_hornet1_2), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
            ar9331_modes_lowest_ob_db_tx_gain_hornet1_2, 
            ARRAY_LENGTH(ar9331_modes_lowest_ob_db_tx_gain_hornet1_2), 5);

        ah->ah_config.ath_hal_pcie_power_save_enable = 0;

        /* Japan 2484Mhz CCK settings */
        INIT_INI_ARRAY(&ahp->ah_ini_japan2484,
            ar9331_hornet1_2_baseband_core_txfir_coeff_japan_2484,
            ARRAY_LENGTH(
                ar9331_hornet1_2_baseband_core_txfir_coeff_japan_2484), 2);
    
#if 0 /* ATH_WOW */
        /* SerDes values during WOW sleep */
        INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_wow, ar9300_pcie_phy_awow,
                ARRAY_LENGTH(ar9300_pcie_phy_awow), 2);
#endif
    
        /* additional clock settings */
        if (AH9300(ah)->clk_25mhz) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_additional,
                ar9331_hornet1_2_xtal_25M,
                ARRAY_LENGTH(ar9331_hornet1_2_xtal_25M), 2);
        } else {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_additional,
                ar9331_hornet1_2_xtal_40M,
                ARRAY_LENGTH(ar9331_hornet1_2_xtal_40M), 2);
        }

    } else if (AR_SREV_HORNET_11(ah)) {
        /* mac */
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_PRE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_CORE],
            ar9331_hornet1_1_mac_core,
            ARRAY_LENGTH(ar9331_hornet1_1_mac_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_POST],
            ar9331_hornet1_1_mac_postamble,
            ARRAY_LENGTH(ar9331_hornet1_1_mac_postamble), 5);

        /* bb */
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_PRE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_CORE],
            ar9331_hornet1_1_baseband_core,
            ARRAY_LENGTH(ar9331_hornet1_1_baseband_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_POST],
            ar9331_hornet1_1_baseband_postamble,
            ARRAY_LENGTH(ar9331_hornet1_1_baseband_postamble), 5);

        /* radio */
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_PRE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_CORE], 
            ar9331_hornet1_1_radio_core,
            ARRAY_LENGTH(ar9331_hornet1_1_radio_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_POST], NULL, 0, 0);

        /* soc */
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_PRE],
            ar9331_hornet1_1_soc_preamble,
            ARRAY_LENGTH(ar9331_hornet1_1_soc_preamble), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_CORE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_POST],
            ar9331_hornet1_1_soc_postamble,
            ARRAY_LENGTH(ar9331_hornet1_1_soc_postamble), 2);

        /* rx/tx gain */
        INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain,
            ar9331_common_rx_gain_hornet1_1, 
            ARRAY_LENGTH(ar9331_common_rx_gain_hornet1_1), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
            ar9331_modes_lowest_ob_db_tx_gain_hornet1_1, 
            ARRAY_LENGTH(ar9331_modes_lowest_ob_db_tx_gain_hornet1_1), 5);

        ah->ah_config.ath_hal_pcie_power_save_enable = 0;

        /* Japan 2484Mhz CCK settings */
        INIT_INI_ARRAY(&ahp->ah_ini_japan2484,
            ar9331_hornet1_1_baseband_core_txfir_coeff_japan_2484,
            ARRAY_LENGTH(
                ar9331_hornet1_1_baseband_core_txfir_coeff_japan_2484), 2);
    
#if 0 /* ATH_WOW */
        /* SerDes values during WOW sleep */
        INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_wow, ar9300_pcie_phy_awow,
                       N(ar9300_pcie_phy_awow), 2);
#endif
    
        /* additional clock settings */
        if (AH9300(ah)->clk_25mhz) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_additional,
                ar9331_hornet1_1_xtal_25M,
                ARRAY_LENGTH(ar9331_hornet1_1_xtal_25M), 2);
        } else {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_additional,
                ar9331_hornet1_1_xtal_40M,
                ARRAY_LENGTH(ar9331_hornet1_1_xtal_40M), 2);
        }

       } else if (AR_SREV_POSEIDON_11_OR_LATER(ah)) {
        /* mac */
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_PRE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_CORE],
            ar9485_poseidon1_1_mac_core, 
            ARRAY_LENGTH( ar9485_poseidon1_1_mac_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_POST],
            ar9485_poseidon1_1_mac_postamble, 
            ARRAY_LENGTH(ar9485_poseidon1_1_mac_postamble), 5);

        /* bb */
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_PRE], 
            ar9485_poseidon1_1, ARRAY_LENGTH(ar9485_poseidon1_1), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_CORE],
            ar9485_poseidon1_1_baseband_core, 
            ARRAY_LENGTH(ar9485_poseidon1_1_baseband_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_POST],
            ar9485_poseidon1_1_baseband_postamble, 
            ARRAY_LENGTH(ar9485_poseidon1_1_baseband_postamble), 5);

        /* radio */
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_PRE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_CORE], 
            ar9485_poseidon1_1_radio_core, 
            ARRAY_LENGTH(ar9485_poseidon1_1_radio_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_POST],
            ar9485_poseidon1_1_radio_postamble, 
            ARRAY_LENGTH(ar9485_poseidon1_1_radio_postamble), 2);

        /* soc */
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_PRE],
            ar9485_poseidon1_1_soc_preamble, 
            ARRAY_LENGTH(ar9485_poseidon1_1_soc_preamble), 2);

        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_CORE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_POST], NULL, 0, 0);

        /* rx/tx gain */
        INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain, 
            ar9485_common_wo_xlna_rx_gain_poseidon1_1, 
            ARRAY_LENGTH(ar9485_common_wo_xlna_rx_gain_poseidon1_1), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain, 
            ar9485_modes_lowest_ob_db_tx_gain_poseidon1_1, 
            ARRAY_LENGTH(ar9485_modes_lowest_ob_db_tx_gain_poseidon1_1), 5);

        /* Japan 2484Mhz CCK settings */
        INIT_INI_ARRAY(&ahp->ah_ini_japan2484,
            ar9485_poseidon1_1_baseband_core_txfir_coeff_japan_2484,
            ARRAY_LENGTH(
                ar9485_poseidon1_1_baseband_core_txfir_coeff_japan_2484), 2);

        /* Load PCIE SERDES settings from INI */
        if (ah->ah_config.ath_hal_pcie_clock_req) {
            /* Pci-e Clock Request = 1 */
            if (ah->ah_config.ath_hal_pll_pwr_save 
                & AR_PCIE_PLL_PWRSAVE_CONTROL)
            {
                /* Sleep Setting */
                if (ah->ah_config.ath_hal_pll_pwr_save & 
                    AR_PCIE_PLL_PWRSAVE_ON_D3) 
                {
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes, 
                        ar9485_poseidon1_1_pcie_phy_clkreq_enable_L1,
                        ARRAY_LENGTH(
                           ar9485_poseidon1_1_pcie_phy_clkreq_enable_L1),
                        2);
                } else {
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes, 
                        ar9485_poseidon1_1_pcie_phy_pll_on_clkreq_enable_L1,
                        ARRAY_LENGTH(
                           ar9485_poseidon1_1_pcie_phy_pll_on_clkreq_enable_L1),
                        2);
                }    
                /* Awake Setting */
                if (ah->ah_config.ath_hal_pll_pwr_save & 
                    AR_PCIE_PLL_PWRSAVE_ON_D0)
                {
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_low_power, 
                        ar9485_poseidon1_1_pcie_phy_clkreq_enable_L1,
                        ARRAY_LENGTH(
                           ar9485_poseidon1_1_pcie_phy_clkreq_enable_L1),
                        2);
                } else {
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_low_power, 
                        ar9485_poseidon1_1_pcie_phy_pll_on_clkreq_enable_L1,
                        ARRAY_LENGTH(
                           ar9485_poseidon1_1_pcie_phy_pll_on_clkreq_enable_L1),
                        2);
                }    
                
            } else {
                /*Use driver default setting*/
                /* Sleep Setting */
                INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes, 
                    ar9485_poseidon1_1_pcie_phy_clkreq_enable_L1,
                    ARRAY_LENGTH(ar9485_poseidon1_1_pcie_phy_clkreq_enable_L1), 
                    2);
                /* Awake Setting */
                INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_low_power, 
                    ar9485_poseidon1_1_pcie_phy_clkreq_enable_L1,
                    ARRAY_LENGTH(ar9485_poseidon1_1_pcie_phy_clkreq_enable_L1), 
                    2);
            }
        } else {
            /* Pci-e Clock Request = 0 */
            if (ah->ah_config.ath_hal_pll_pwr_save 
                & AR_PCIE_PLL_PWRSAVE_CONTROL)
            {
                /* Sleep Setting */
                if (ah->ah_config.ath_hal_pll_pwr_save & 
                    AR_PCIE_PLL_PWRSAVE_ON_D3) 
                {
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes, 
                        ar9485_poseidon1_1_pcie_phy_clkreq_disable_L1,
                        ARRAY_LENGTH(
                          ar9485_poseidon1_1_pcie_phy_clkreq_disable_L1),
                        2);
                } else {
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes, 
                        ar9485_poseidon1_1_pcie_phy_pll_on_clkreq_disable_L1,
                        ARRAY_LENGTH(
                          ar9485_poseidon1_1_pcie_phy_pll_on_clkreq_disable_L1),
                        2);
                }    
                /* Awake Setting */
                if (ah->ah_config.ath_hal_pll_pwr_save & 
                    AR_PCIE_PLL_PWRSAVE_ON_D0)
                {
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_low_power, 
                        ar9485_poseidon1_1_pcie_phy_clkreq_disable_L1,
                        ARRAY_LENGTH(
                          ar9485_poseidon1_1_pcie_phy_clkreq_disable_L1),
                        2);
                } else {
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_low_power, 
                        ar9485_poseidon1_1_pcie_phy_pll_on_clkreq_disable_L1,
                        ARRAY_LENGTH(
                          ar9485_poseidon1_1_pcie_phy_pll_on_clkreq_disable_L1),
                        2);
                }    
                
            } else {
                /*Use driver default setting*/
                /* Sleep Setting */
                INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes, 
                    ar9485_poseidon1_1_pcie_phy_clkreq_disable_L1,
                    ARRAY_LENGTH(ar9485_poseidon1_1_pcie_phy_clkreq_disable_L1),
                    2);
                /* Awake Setting */
                INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_low_power, 
                    ar9485_poseidon1_1_pcie_phy_clkreq_disable_L1,
                    ARRAY_LENGTH(ar9485_poseidon1_1_pcie_phy_clkreq_disable_L1),
                    2);
            }
        }
        /* pcie ps setting will honor registry setting, default is 0 */
        //ah->ah_config.ath_hal_pciePowerSaveEnable = 0;    
   } else if (AR_SREV_POSEIDON(ah)) {
        /* mac */
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_PRE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_CORE],
            ar9485_poseidon1_0_mac_core,
            ARRAY_LENGTH(ar9485_poseidon1_0_mac_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_POST],
            ar9485_poseidon1_0_mac_postamble,
            ARRAY_LENGTH(ar9485_poseidon1_0_mac_postamble), 5);

        /* bb */
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_PRE], 
            ar9485_poseidon1_0, 
            ARRAY_LENGTH(ar9485_poseidon1_0), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_CORE],
            ar9485_poseidon1_0_baseband_core,
            ARRAY_LENGTH(ar9485_poseidon1_0_baseband_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_POST],
            ar9485_poseidon1_0_baseband_postamble,
            ARRAY_LENGTH(ar9485_poseidon1_0_baseband_postamble), 5);

        /* radio */
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_PRE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_CORE], 
            ar9485_poseidon1_0_radio_core,
            ARRAY_LENGTH(ar9485_poseidon1_0_radio_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_POST],
            ar9485_poseidon1_0_radio_postamble,
            ARRAY_LENGTH(ar9485_poseidon1_0_radio_postamble), 2);

        /* soc */
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_PRE],
            ar9485_poseidon1_0_soc_preamble,
            ARRAY_LENGTH(ar9485_poseidon1_0_soc_preamble), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_CORE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_POST], NULL, 0, 0);

        /* rx/tx gain */
        INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain,
            ar9485Common_wo_xlna_rx_gain_poseidon1_0, 
            ARRAY_LENGTH(ar9485Common_wo_xlna_rx_gain_poseidon1_0), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
            ar9485Modes_lowest_ob_db_tx_gain_poseidon1_0, 
            ARRAY_LENGTH(ar9485Modes_lowest_ob_db_tx_gain_poseidon1_0), 5);

        /* Japan 2484Mhz CCK settings */
        INIT_INI_ARRAY(&ahp->ah_ini_japan2484,
            ar9485_poseidon1_0_baseband_core_txfir_coeff_japan_2484,
            ARRAY_LENGTH(
                ar9485_poseidon1_0_baseband_core_txfir_coeff_japan_2484), 2);

        /* Load PCIE SERDES settings from INI */
        if (ah->ah_config.ath_hal_pcie_clock_req) {
            /* Pci-e Clock Request = 1 */
            if (ah->ah_config.ath_hal_pll_pwr_save 
                & AR_PCIE_PLL_PWRSAVE_CONTROL)
            {
                /* Sleep Setting */
                if (ah->ah_config.ath_hal_pll_pwr_save & 
                    AR_PCIE_PLL_PWRSAVE_ON_D3) 
                {
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes, 
                        ar9485_poseidon1_0_pcie_phy_clkreq_enable_L1,
                        ARRAY_LENGTH(
                           ar9485_poseidon1_0_pcie_phy_clkreq_enable_L1),
                        2);
                } else {
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes, 
                        ar9485_poseidon1_0_pcie_phy_pll_on_clkreq_enable_L1,
                        ARRAY_LENGTH(
                           ar9485_poseidon1_0_pcie_phy_pll_on_clkreq_enable_L1),
                        2);
                }    
                /* Awake Setting */
                if (ah->ah_config.ath_hal_pll_pwr_save & 
                    AR_PCIE_PLL_PWRSAVE_ON_D0)
                {
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_low_power, 
                        ar9485_poseidon1_0_pcie_phy_clkreq_enable_L1,
                        ARRAY_LENGTH(
                           ar9485_poseidon1_0_pcie_phy_clkreq_enable_L1),
                        2);
                } else {
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_low_power, 
                        ar9485_poseidon1_0_pcie_phy_pll_on_clkreq_enable_L1,
                        ARRAY_LENGTH(
                           ar9485_poseidon1_0_pcie_phy_pll_on_clkreq_enable_L1),
                        2);
                }    
                
            } else {
                /*Use driver default setting*/
                /* Sleep Setting */
                INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes, 
                    ar9485_poseidon1_0_pcie_phy_pll_on_clkreq_enable_L1,
                    ARRAY_LENGTH(
                        ar9485_poseidon1_0_pcie_phy_pll_on_clkreq_enable_L1), 
                    2);
                /* Awake Setting */
                INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_low_power, 
                    ar9485_poseidon1_0_pcie_phy_pll_on_clkreq_enable_L1,
                    ARRAY_LENGTH(
                        ar9485_poseidon1_0_pcie_phy_pll_on_clkreq_enable_L1), 
                    2);
            }
        } else {
            /* Pci-e Clock Request = 0 */
            if (ah->ah_config.ath_hal_pll_pwr_save 
                & AR_PCIE_PLL_PWRSAVE_CONTROL)
            {
                /* Sleep Setting */
                if (ah->ah_config.ath_hal_pll_pwr_save & 
                    AR_PCIE_PLL_PWRSAVE_ON_D3) 
                {
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes, 
                        ar9485_poseidon1_0_pcie_phy_clkreq_disable_L1,
                        ARRAY_LENGTH(
                          ar9485_poseidon1_0_pcie_phy_clkreq_disable_L1),
                        2);
                } else {
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes, 
                        ar9485_poseidon1_0_pcie_phy_pll_on_clkreq_disable_L1,
                        ARRAY_LENGTH(
                          ar9485_poseidon1_0_pcie_phy_pll_on_clkreq_disable_L1),
                        2);
                }    
                /* Awake Setting */
                if (ah->ah_config.ath_hal_pll_pwr_save & 
                    AR_PCIE_PLL_PWRSAVE_ON_D0)
                {
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_low_power, 
                        ar9485_poseidon1_0_pcie_phy_clkreq_disable_L1,
                        ARRAY_LENGTH(
                          ar9485_poseidon1_0_pcie_phy_clkreq_disable_L1),
                        2);
                } else {
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_low_power, 
                        ar9485_poseidon1_0_pcie_phy_pll_on_clkreq_disable_L1,
                        ARRAY_LENGTH(
                          ar9485_poseidon1_0_pcie_phy_pll_on_clkreq_disable_L1),
                        2);
                }    
                
            } else {
                /*Use driver default setting*/
                /* Sleep Setting */
                INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes, 
                    ar9485_poseidon1_0_pcie_phy_pll_on_clkreq_disable_L1,
                    ARRAY_LENGTH(
                        ar9485_poseidon1_0_pcie_phy_pll_on_clkreq_disable_L1), 
                    2);
                /* Awake Setting */
                INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_low_power, 
                    ar9485_poseidon1_0_pcie_phy_pll_on_clkreq_disable_L1,
                    ARRAY_LENGTH(
                        ar9485_poseidon1_0_pcie_phy_pll_on_clkreq_disable_L1), 
                    2);
            }
        }
        /* pcie ps setting will honor registry setting, default is 0 */
        /*ah->ah_config.ath_hal_pcie_power_save_enable = 0;*/
    
#if 0 /* ATH_WOW */
        /* SerDes values during WOW sleep */
        INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_wow, ar9300_pcie_phy_awow,
                       ARRAY_LENGTH(ar9300_pcie_phy_awow), 2);
#endif

    } else if (AR_SREV_WASP(ah)) {
        /* mac */
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_PRE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_CORE],
            ar9340_wasp_1p0_mac_core,
            ARRAY_LENGTH(ar9340_wasp_1p0_mac_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_POST],
            ar9340_wasp_1p0_mac_postamble,
            ARRAY_LENGTH(ar9340_wasp_1p0_mac_postamble), 5);

        /* bb */
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_PRE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_CORE],
            ar9340_wasp_1p0_baseband_core,
            ARRAY_LENGTH(ar9340_wasp_1p0_baseband_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_POST],
            ar9340_wasp_1p0_baseband_postamble,
            ARRAY_LENGTH(ar9340_wasp_1p0_baseband_postamble), 5);

        /* radio */
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_PRE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_CORE],
            ar9340_wasp_1p0_radio_core,
            ARRAY_LENGTH(ar9340_wasp_1p0_radio_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_POST],
            ar9340_wasp_1p0_radio_postamble,
            ARRAY_LENGTH(ar9340_wasp_1p0_radio_postamble), 5);

        /* soc */
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_PRE],
            ar9340_wasp_1p0_soc_preamble,
            ARRAY_LENGTH(ar9340_wasp_1p0_soc_preamble), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_CORE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_POST],
            ar9340_wasp_1p0_soc_postamble,
            ARRAY_LENGTH(ar9340_wasp_1p0_soc_postamble), 5);

        /* rx/tx gain */
        INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain,
            ar9340Common_wo_xlna_rx_gain_table_wasp_1p0,
            ARRAY_LENGTH(ar9340Common_wo_xlna_rx_gain_table_wasp_1p0), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
            ar9340Modes_high_ob_db_tx_gain_table_wasp_1p0,
            ARRAY_LENGTH(ar9340Modes_high_ob_db_tx_gain_table_wasp_1p0), 5);

        ah->ah_config.ath_hal_pcie_power_save_enable = 0;

        /* Fast clock modal settings */
        INIT_INI_ARRAY(&ahp->ah_ini_modes_additional,
            ar9340Modes_fast_clock_wasp_1p0,
            ARRAY_LENGTH(ar9340Modes_fast_clock_wasp_1p0), 3);

        /* XXX TODO: need to add this for freebsd; it's missing from the current .ini files */
#if 0
        /* Japan 2484Mhz CCK settings */
        INIT_INI_ARRAY(&ahp->ah_ini_japan2484,
            ar9340_wasp_1p0_baseband_core_txfir_coeff_japan_2484,
            ARRAY_LENGTH(
                ar9340_wasp_1p0_baseband_core_txfir_coeff_japan_2484), 2);
#endif

        /* Additional setttings for 40Mhz */
        INIT_INI_ARRAY(&ahp->ah_ini_modes_additional_40mhz, 
            ar9340_wasp_1p0_radio_core_40M,
            ARRAY_LENGTH(ar9340_wasp_1p0_radio_core_40M), 2);

        /* DFS */
        INIT_INI_ARRAY(&ahp->ah_ini_dfs,
            ar9340_wasp_1p0_baseband_postamble_dfs_channel,
            ARRAY_LENGTH(ar9340_wasp_1p0_baseband_postamble_dfs_channel), 3);
    } else if (AR_SREV_SCORPION(ah)) {
        /* mac */
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_PRE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_CORE],
                        ar955x_scorpion_1p0_mac_core,
                        ARRAY_LENGTH(ar955x_scorpion_1p0_mac_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_POST],
                        ar955x_scorpion_1p0_mac_postamble,
                        ARRAY_LENGTH(ar955x_scorpion_1p0_mac_postamble), 5);

        /* bb */
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_PRE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_CORE],
                        ar955x_scorpion_1p0_baseband_core,
                        ARRAY_LENGTH(ar955x_scorpion_1p0_baseband_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_POST],
                        ar955x_scorpion_1p0_baseband_postamble,
                        ARRAY_LENGTH(ar955x_scorpion_1p0_baseband_postamble), 5);

        /* radio */
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_PRE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_CORE],
                        ar955x_scorpion_1p0_radio_core,
                        ARRAY_LENGTH(ar955x_scorpion_1p0_radio_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_POST],
                        ar955x_scorpion_1p0_radio_postamble,
                        ARRAY_LENGTH(ar955x_scorpion_1p0_radio_postamble), 5);

        /* soc */
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_PRE],
                        ar955x_scorpion_1p0_soc_preamble,
                        ARRAY_LENGTH(ar955x_scorpion_1p0_soc_preamble), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_CORE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_POST],
                        ar955x_scorpion_1p0_soc_postamble,
                        ARRAY_LENGTH(ar955x_scorpion_1p0_soc_postamble), 5);

        /* rx/tx gain */
        INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain,
                        ar955xCommon_wo_xlna_rx_gain_table_scorpion_1p0,
                        ARRAY_LENGTH(ar955xCommon_wo_xlna_rx_gain_table_scorpion_1p0), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain_bounds,
                        ar955xCommon_wo_xlna_rx_gain_bounds_scorpion_1p0,
                        ARRAY_LENGTH(ar955xCommon_wo_xlna_rx_gain_bounds_scorpion_1p0), 5);
        INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                        ar955xModes_no_xpa_tx_gain_table_scorpion_1p0,
                        ARRAY_LENGTH(ar955xModes_no_xpa_tx_gain_table_scorpion_1p0), 5);

        /*ath_hal_pciePowerSaveEnable should be 2 for OWL/Condor and 0 for merlin */
        ah->ah_config.ath_hal_pcie_power_save_enable = 0;

        /* Fast clock modal settings */
        INIT_INI_ARRAY(&ahp->ah_ini_modes_additional,
                        ar955xModes_fast_clock_scorpion_1p0,
                        ARRAY_LENGTH(ar955xModes_fast_clock_scorpion_1p0), 3);

        /* Additional setttings for 40Mhz */
        //INIT_INI_ARRAY(&ahp->ah_ini_modes_additional_40M,
        //                ar955x_scorpion_1p0_radio_core_40M,
        //                ARRAY_LENGTH(ar955x_scorpion_1p0_radio_core_40M), 2);
    } else if (AR_SREV_HONEYBEE(ah)) {
        /* mac */
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_PRE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_CORE],
                        qca953x_honeybee_1p0_mac_core,
                        ARRAY_LENGTH(qca953x_honeybee_1p0_mac_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_POST],
                        qca953x_honeybee_1p0_mac_postamble,
                        ARRAY_LENGTH(qca953x_honeybee_1p0_mac_postamble), 5);

        /* bb */
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_PRE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_CORE],
                        qca953x_honeybee_1p0_baseband_core,
                        ARRAY_LENGTH(qca953x_honeybee_1p0_baseband_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_POST],
                        qca953x_honeybee_1p0_baseband_postamble,
                        ARRAY_LENGTH(qca953x_honeybee_1p0_baseband_postamble), 5);

        /* radio */
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_PRE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_CORE],
                        qca953x_honeybee_1p0_radio_core,
                        ARRAY_LENGTH(qca953x_honeybee_1p0_radio_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_POST],
                        qca953x_honeybee_1p0_radio_postamble,
                        ARRAY_LENGTH(qca953x_honeybee_1p0_radio_postamble), 5);

        /* soc */
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_PRE],
                        qca953x_honeybee_1p0_soc_preamble,
                        ARRAY_LENGTH(qca953x_honeybee_1p0_soc_preamble), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_CORE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_POST],
                        qca953x_honeybee_1p0_soc_postamble,
                        ARRAY_LENGTH(qca953x_honeybee_1p0_soc_postamble), 5);

        /* rx/tx gain */
        INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain,
                        qca953xCommon_wo_xlna_rx_gain_table_honeybee_1p0,
                        ARRAY_LENGTH(qca953xCommon_wo_xlna_rx_gain_table_honeybee_1p0), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain_bounds,
                        qca953xCommon_wo_xlna_rx_gain_bounds_honeybee_1p0,
                        ARRAY_LENGTH(qca953xCommon_wo_xlna_rx_gain_bounds_honeybee_1p0), 5);
        INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                        qca953xModes_no_xpa_tx_gain_table_honeybee_1p0,
                        ARRAY_LENGTH(qca953xModes_no_xpa_tx_gain_table_honeybee_1p0), 2);

        /*ath_hal_pciePowerSaveEnable should be 2 for OWL/Condor and 0 for merlin */
        ah->ah_config.ath_hal_pcie_power_save_enable = 0;

        /* Fast clock modal settings */
        INIT_INI_ARRAY(&ahp->ah_ini_modes_additional,
                        qca953xModes_fast_clock_honeybee_1p0,
                        ARRAY_LENGTH(qca953xModes_fast_clock_honeybee_1p0), 3);

        /* Additional setttings for 40Mhz */
        //INIT_INI_ARRAY(&ahp->ah_ini_modes_additional_40M,
        //                qca953x_honeybee_1p0_radio_core_40M,
        //                ARRAY_LENGTH(qca953x_honeybee_1p0_radio_core_40M), 2);

    } else if (AR_SREV_JUPITER_10(ah)) {
        /* Jupiter: new INI format (pre, core, post arrays per subsystem) */

        /* mac */
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_PRE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_CORE],
            ar9300_jupiter_1p0_mac_core, 
            ARRAY_LENGTH(ar9300_jupiter_1p0_mac_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_POST],
            ar9300_jupiter_1p0_mac_postamble,
            ARRAY_LENGTH(ar9300_jupiter_1p0_mac_postamble), 5);
                       
        /* bb */
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_PRE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_CORE],
            ar9300_jupiter_1p0_baseband_core,
            ARRAY_LENGTH(ar9300_jupiter_1p0_baseband_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_POST],
            ar9300_jupiter_1p0_baseband_postamble,
            ARRAY_LENGTH(ar9300_jupiter_1p0_baseband_postamble), 5);

        /* radio */
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_PRE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_CORE],
            ar9300_jupiter_1p0_radio_core, 
            ARRAY_LENGTH(ar9300_jupiter_1p0_radio_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_POST],
            ar9300_jupiter_1p0_radio_postamble, 
            ARRAY_LENGTH(ar9300_jupiter_1p0_radio_postamble), 5);

        /* soc */
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_PRE],
            ar9300_jupiter_1p0_soc_preamble, 
            ARRAY_LENGTH(ar9300_jupiter_1p0_soc_preamble), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_CORE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_POST],
            ar9300_jupiter_1p0_soc_postamble, 
            ARRAY_LENGTH(ar9300_jupiter_1p0_soc_postamble), 5);

        /* rx/tx gain */
        INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain, 
            ar9300_common_rx_gain_table_jupiter_1p0,
            ARRAY_LENGTH(ar9300_common_rx_gain_table_jupiter_1p0), 2);

        /* Load PCIE SERDES settings from INI */
        if (ah->ah_config.ath_hal_pcie_clock_req) {
            /* Pci-e Clock Request = 1 */
            /*
             * PLL ON + clkreq enable is not a valid combination,
             * thus to ignore ath_hal_pll_pwr_save, use PLL OFF.
             */
            {
                /*Use driver default setting*/
                /* Awake -> Sleep Setting */
                INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes,
                    ar9300_pcie_phy_clkreq_enable_L1_jupiter_1p0,
                    ARRAY_LENGTH(ar9300_pcie_phy_clkreq_enable_L1_jupiter_1p0),
                    2);
                /* Sleep -> Awake Setting */
                INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_low_power,
                    ar9300_pcie_phy_clkreq_enable_L1_jupiter_1p0,
                    ARRAY_LENGTH(ar9300_pcie_phy_clkreq_enable_L1_jupiter_1p0),
                    2);
            }
        }
        else {
            /*
             * Since Jupiter 1.0 and 2.0 share the same device id and will be
             * installed with same INF, but Jupiter 1.0 has issue with PLL OFF.
             *
             * Force Jupiter 1.0 to use ON/ON setting.
             */
            ah->ah_config.ath_hal_pll_pwr_save = 0;
            /* Pci-e Clock Request = 0 */
            if (ah->ah_config.ath_hal_pll_pwr_save &
                AR_PCIE_PLL_PWRSAVE_CONTROL)
            {
                /* Awake -> Sleep Setting */
                if (ah->ah_config.ath_hal_pll_pwr_save &
                     AR_PCIE_PLL_PWRSAVE_ON_D3) 
                {
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes,
                        ar9300_pcie_phy_clkreq_disable_L1_jupiter_1p0,
                        ARRAY_LENGTH(
                            ar9300_pcie_phy_clkreq_disable_L1_jupiter_1p0),
                        2);
                }
                else {
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes,
                        ar9300_pcie_phy_pll_on_clkreq_disable_L1_jupiter_1p0,
                        ARRAY_LENGTH(
                          ar9300_pcie_phy_pll_on_clkreq_disable_L1_jupiter_1p0),
                        2);
                }    
                /* Sleep -> Awake Setting */
                if (ah->ah_config.ath_hal_pll_pwr_save &
                    AR_PCIE_PLL_PWRSAVE_ON_D0)
                {
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_low_power,
                        ar9300_pcie_phy_clkreq_disable_L1_jupiter_1p0,
                        ARRAY_LENGTH(
                            ar9300_pcie_phy_clkreq_disable_L1_jupiter_1p0),
                        2);
                }
                else {
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_low_power,
                        ar9300_pcie_phy_pll_on_clkreq_disable_L1_jupiter_1p0,
                        ARRAY_LENGTH(
                          ar9300_pcie_phy_pll_on_clkreq_disable_L1_jupiter_1p0),
                        2);
                }    
                
            }
            else {
                /*Use driver default setting*/
                /* Awake -> Sleep Setting */
                INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes,
                    ar9300_pcie_phy_pll_on_clkreq_disable_L1_jupiter_1p0,
                    ARRAY_LENGTH(
                        ar9300_pcie_phy_pll_on_clkreq_disable_L1_jupiter_1p0),
                    2);
                /* Sleep -> Awake Setting */
                INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_low_power,
                    ar9300_pcie_phy_pll_on_clkreq_disable_L1_jupiter_1p0,
                    ARRAY_LENGTH(
                        ar9300_pcie_phy_pll_on_clkreq_disable_L1_jupiter_1p0),
                    2);
            }
        }
        /* 
         * ath_hal_pcie_power_save_enable should be 2 for OWL/Condor and 
         * 0 for merlin 
         */
        ah->ah_config.ath_hal_pcie_power_save_enable = 0;

#if 0 // ATH_WOW
        /* SerDes values during WOW sleep */
        INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_wow, ar9300_pcie_phy_AWOW,
            ARRAY_LENGTH(ar9300_pcie_phy_AWOW), 2);
#endif

        /* Fast clock modal settings */
        INIT_INI_ARRAY(&ahp->ah_ini_modes_additional, 
            ar9300_modes_fast_clock_jupiter_1p0,
            ARRAY_LENGTH(ar9300_modes_fast_clock_jupiter_1p0), 3);
        INIT_INI_ARRAY(&ahp->ah_ini_japan2484,
            ar9300_jupiter_1p0_baseband_core_txfir_coeff_japan_2484,
            ARRAY_LENGTH(
            ar9300_jupiter_1p0_baseband_core_txfir_coeff_japan_2484), 2);

    }
    else if (AR_SREV_JUPITER_20_OR_LATER(ah)) {
        /* Jupiter: new INI format (pre, core, post arrays per subsystem) */

        /* FreeBSD: just override the registers for jupiter 2.1 */
        /* XXX TODO: refactor this stuff out; reinit all the 2.1 registers */

        /* mac */
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_PRE], NULL, 0, 0);

        if (AR_SREV_JUPITER_21(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_CORE],
              ar9462_2p1_mac_core,
              ARRAY_LENGTH(ar9462_2p1_mac_core), 2);
        } else {
            INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_CORE],
                ar9300_jupiter_2p0_mac_core, 
                ARRAY_LENGTH(ar9300_jupiter_2p0_mac_core), 2);
        }

        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_POST],
            ar9300_jupiter_2p0_mac_postamble,
            ARRAY_LENGTH(ar9300_jupiter_2p0_mac_postamble), 5);
                       
        /* bb */
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_PRE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_CORE],
            ar9300_jupiter_2p0_baseband_core,
            ARRAY_LENGTH(ar9300_jupiter_2p0_baseband_core), 2);

        if (AR_SREV_JUPITER_21(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_POST],
                ar9462_2p1_baseband_postamble,
                ARRAY_LENGTH(ar9462_2p1_baseband_postamble), 5);
        } else {
            INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_POST],
                ar9300_jupiter_2p0_baseband_postamble,
                ARRAY_LENGTH(ar9300_jupiter_2p0_baseband_postamble), 5);
        }

        /* radio */
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_PRE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_CORE],
            ar9300_jupiter_2p0_radio_core, 
            ARRAY_LENGTH(ar9300_jupiter_2p0_radio_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_POST],
            ar9300_jupiter_2p0_radio_postamble, 
            ARRAY_LENGTH(ar9300_jupiter_2p0_radio_postamble), 5);
        INIT_INI_ARRAY(&ahp->ah_ini_radio_post_sys2ant,
            ar9300_jupiter_2p0_radio_postamble_sys2ant, 
            ARRAY_LENGTH(ar9300_jupiter_2p0_radio_postamble_sys2ant), 5);

        /* soc */
        if (AR_SREV_JUPITER_21(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_PRE],
              ar9462_2p1_soc_preamble,
              ARRAY_LENGTH(ar9462_2p1_soc_preamble), 2);
        } else {
            INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_PRE],
              ar9300_jupiter_2p0_soc_preamble, 
              ARRAY_LENGTH(ar9300_jupiter_2p0_soc_preamble), 2);
        }
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_CORE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_POST],
            ar9300_jupiter_2p0_soc_postamble, 
            ARRAY_LENGTH(ar9300_jupiter_2p0_soc_postamble), 5);

        /* rx/tx gain */
        INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain, 
            ar9300Common_rx_gain_table_jupiter_2p0,
            ARRAY_LENGTH(ar9300Common_rx_gain_table_jupiter_2p0), 2);

        /* BTCOEX */
        INIT_INI_ARRAY(&ahp->ah_ini_BTCOEX_MAX_TXPWR,
            ar9300_jupiter_2p0_BTCOEX_MAX_TXPWR_table, 
            ARRAY_LENGTH(ar9300_jupiter_2p0_BTCOEX_MAX_TXPWR_table), 2);

        /* Load PCIE SERDES settings from INI */
        if (ah->ah_config.ath_hal_pcie_clock_req) {
            /* Pci-e Clock Request = 1 */
            /*
             * PLL ON + clkreq enable is not a valid combination,
             * thus to ignore ath_hal_pll_pwr_save, use PLL OFF.
             */
            {
                /*Use driver default setting*/
                /* Awake -> Sleep Setting */
                INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes,
                    ar9300_PciePhy_clkreq_enable_L1_jupiter_2p0,
                    ARRAY_LENGTH(ar9300_PciePhy_clkreq_enable_L1_jupiter_2p0),
                    2);
                /* Sleep -> Awake Setting */
                INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_low_power,
                    ar9300_PciePhy_clkreq_enable_L1_jupiter_2p0,
                    ARRAY_LENGTH(ar9300_PciePhy_clkreq_enable_L1_jupiter_2p0),
                    2);
            }
        }
        else {
            /* Pci-e Clock Request = 0 */
            if (ah->ah_config.ath_hal_pll_pwr_save &
                AR_PCIE_PLL_PWRSAVE_CONTROL)
            {
                /* Awake -> Sleep Setting */
                if (ah->ah_config.ath_hal_pll_pwr_save &
                     AR_PCIE_PLL_PWRSAVE_ON_D3) 
                {
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes,
                        ar9300_PciePhy_clkreq_disable_L1_jupiter_2p0,
                        ARRAY_LENGTH(
                            ar9300_PciePhy_clkreq_disable_L1_jupiter_2p0),
                        2);
                }
                else {
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes,
                        ar9300_PciePhy_pll_on_clkreq_disable_L1_jupiter_2p0,
                        ARRAY_LENGTH(
                          ar9300_PciePhy_pll_on_clkreq_disable_L1_jupiter_2p0),
                        2);
                }    
                /* Sleep -> Awake Setting */
                if (ah->ah_config.ath_hal_pll_pwr_save &
                    AR_PCIE_PLL_PWRSAVE_ON_D0)
                {
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_low_power,
                        ar9300_PciePhy_clkreq_disable_L1_jupiter_2p0,
                        ARRAY_LENGTH(
                            ar9300_PciePhy_clkreq_disable_L1_jupiter_2p0),
                        2);
                }
                else {
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_low_power,
                        ar9300_PciePhy_pll_on_clkreq_disable_L1_jupiter_2p0,
                        ARRAY_LENGTH(
                          ar9300_PciePhy_pll_on_clkreq_disable_L1_jupiter_2p0),
                        2);
                }    
                
            }
            else {
                /*Use driver default setting*/
                /* Awake -> Sleep Setting */
                INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes,
                    ar9300_PciePhy_pll_on_clkreq_disable_L1_jupiter_2p0,
                    ARRAY_LENGTH(
                        ar9300_PciePhy_pll_on_clkreq_disable_L1_jupiter_2p0),
                    2);
                /* Sleep -> Awake Setting */
                INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_low_power,
                    ar9300_PciePhy_pll_on_clkreq_disable_L1_jupiter_2p0,
                    ARRAY_LENGTH(
                        ar9300_PciePhy_pll_on_clkreq_disable_L1_jupiter_2p0),
                    2);
            }
        }

        /* 
         * ath_hal_pcie_power_save_enable should be 2 for OWL/Condor and 
         * 0 for merlin 
         */
        ah->ah_config.ath_hal_pcie_power_save_enable = 0;

#if 0 // ATH_WOW
        /* SerDes values during WOW sleep */
        INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_wow, ar9300_pcie_phy_AWOW,
            ARRAY_LENGTH(ar9300_pcie_phy_AWOW), 2);
#endif

        /* Fast clock modal settings */
        INIT_INI_ARRAY(&ahp->ah_ini_modes_additional, 
            ar9300Modes_fast_clock_jupiter_2p0,
            ARRAY_LENGTH(ar9300Modes_fast_clock_jupiter_2p0), 3);
        INIT_INI_ARRAY(&ahp->ah_ini_japan2484,
            ar9300_jupiter_2p0_baseband_core_txfir_coeff_japan_2484,
            ARRAY_LENGTH(
            ar9300_jupiter_2p0_baseband_core_txfir_coeff_japan_2484), 2);

    } else if (AR_SREV_APHRODITE(ah)) {
        /* Aphrodite: new INI format (pre, core, post arrays per subsystem) */

        /* mac */
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_PRE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_CORE],
            ar956X_aphrodite_1p0_mac_core, 
            ARRAY_LENGTH(ar956X_aphrodite_1p0_mac_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_POST],
            ar956X_aphrodite_1p0_mac_postamble,
            ARRAY_LENGTH(ar956X_aphrodite_1p0_mac_postamble), 5);

        /* bb */
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_PRE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_CORE],
            ar956X_aphrodite_1p0_baseband_core,
            ARRAY_LENGTH(ar956X_aphrodite_1p0_baseband_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_POST],
            ar956X_aphrodite_1p0_baseband_postamble,
            ARRAY_LENGTH(ar956X_aphrodite_1p0_baseband_postamble), 5);

//mark jupiter have but aphrodite don't have
//        /* radio */
//        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_PRE], NULL, 0, 0);
//        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_CORE],
//            ar9300_aphrodite_1p0_radio_core, 
//            ARRAY_LENGTH(ar9300_aphrodite_1p0_radio_core), 2);
//        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_POST],
//            ar9300_aphrodite_1p0_radio_postamble, 
//            ARRAY_LENGTH(ar9300_aphrodite_1p0_radio_postamble), 5);

        /* soc */
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_PRE],
            ar956X_aphrodite_1p0_soc_preamble, 
            ARRAY_LENGTH(ar956X_aphrodite_1p0_soc_preamble), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_CORE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_POST],
            ar956X_aphrodite_1p0_soc_postamble, 
            ARRAY_LENGTH(ar956X_aphrodite_1p0_soc_postamble), 5);

        /* rx/tx gain */
        INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain, 
            ar956XCommon_rx_gain_table_aphrodite_1p0,
            ARRAY_LENGTH(ar956XCommon_rx_gain_table_aphrodite_1p0), 2);
        //INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain, 
        //    ar956XModes_lowest_ob_db_tx_gain_table_aphrodite_1p0,
        //    ARRAY_LENGTH(ar956XModes_lowest_ob_db_tx_gain_table_aphrodite_1p0),
        //    5);


        /* 
         * ath_hal_pcie_power_save_enable should be 2 for OWL/Condor and 
         * 0 for merlin 
         */
        ah->ah_config.ath_hal_pcie_power_save_enable = 0;

#if 0 // ATH_WOW
        /* SerDes values during WOW sleep */
        INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_wow, ar9300_pcie_phy_AWOW,
            ARRAY_LENGTH(ar9300_pcie_phy_AWOW), 2);
#endif
       /* Fast clock modal settings */
       INIT_INI_ARRAY(&ahp->ah_ini_modes_additional, 
            ar956XModes_fast_clock_aphrodite_1p0,
            ARRAY_LENGTH(ar956XModes_fast_clock_aphrodite_1p0), 3);

    } else if (AR_SREV_AR9580(ah)) {
        /*
         * AR9580/Peacock -
         * new INI format (pre, core, post arrays per subsystem)
         */

        /* mac */
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_PRE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_CORE],
            ar9300_ar9580_1p0_mac_core,
            ARRAY_LENGTH(ar9300_ar9580_1p0_mac_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_POST],
            ar9300_ar9580_1p0_mac_postamble,
            ARRAY_LENGTH(ar9300_ar9580_1p0_mac_postamble), 5);

        /* bb */
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_PRE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_CORE],
            ar9300_ar9580_1p0_baseband_core,
            ARRAY_LENGTH(ar9300_ar9580_1p0_baseband_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_POST],
            ar9300_ar9580_1p0_baseband_postamble,
            ARRAY_LENGTH(ar9300_ar9580_1p0_baseband_postamble), 5);

        /* radio */
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_PRE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_CORE],
            ar9300_ar9580_1p0_radio_core,
            ARRAY_LENGTH(ar9300_ar9580_1p0_radio_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_POST],
            ar9300_ar9580_1p0_radio_postamble,
            ARRAY_LENGTH(ar9300_ar9580_1p0_radio_postamble), 5);

        /* soc */
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_PRE],
            ar9300_ar9580_1p0_soc_preamble,
            ARRAY_LENGTH(ar9300_ar9580_1p0_soc_preamble), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_CORE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_POST],
            ar9300_ar9580_1p0_soc_postamble,
            ARRAY_LENGTH(ar9300_ar9580_1p0_soc_postamble), 5);

        /* rx/tx gain */
        INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain,
            ar9300_common_rx_gain_table_ar9580_1p0,
            ARRAY_LENGTH(ar9300_common_rx_gain_table_ar9580_1p0), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
            ar9300Modes_lowest_ob_db_tx_gain_table_ar9580_1p0,
            ARRAY_LENGTH(ar9300Modes_lowest_ob_db_tx_gain_table_ar9580_1p0), 5);

        /* DFS */
        INIT_INI_ARRAY(&ahp->ah_ini_dfs,
            ar9300_ar9580_1p0_baseband_postamble_dfs_channel,
            ARRAY_LENGTH(ar9300_ar9580_1p0_baseband_postamble_dfs_channel), 3);

 
        /* Load PCIE SERDES settings from INI */

        /*D3 Setting */
        if  (ah->ah_config.ath_hal_pcie_clock_req) {
            if (ah->ah_config.ath_hal_pll_pwr_save &
                AR_PCIE_PLL_PWRSAVE_CONTROL)
            { //registry control
                if (ah->ah_config.ath_hal_pll_pwr_save &
                    AR_PCIE_PLL_PWRSAVE_ON_D3)
                { //bit1, in to D3
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes,
                        ar9300PciePhy_clkreq_enable_L1_ar9580_1p0,
                        ARRAY_LENGTH(ar9300PciePhy_clkreq_enable_L1_ar9580_1p0),
                    2);
                } else {
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes,
                        ar9300PciePhy_pll_on_clkreq_disable_L1_ar9580_1p0,
                        ARRAY_LENGTH(
                            ar9300PciePhy_pll_on_clkreq_disable_L1_ar9580_1p0),
                    2);
                }
            } else {//no registry control, default is pll on
                INIT_INI_ARRAY(
                    &ahp->ah_ini_pcie_serdes,
                    ar9300PciePhy_pll_on_clkreq_disable_L1_ar9580_1p0,
                    ARRAY_LENGTH(
                        ar9300PciePhy_pll_on_clkreq_disable_L1_ar9580_1p0),
                    2);
            }
        } else {
            if (ah->ah_config.ath_hal_pll_pwr_save &
                AR_PCIE_PLL_PWRSAVE_CONTROL)
            { //registry control
                if (ah->ah_config.ath_hal_pll_pwr_save &
                    AR_PCIE_PLL_PWRSAVE_ON_D3)
                { //bit1, in to D3
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes,
                        ar9300PciePhy_clkreq_disable_L1_ar9580_1p0,
                        ARRAY_LENGTH(
                            ar9300PciePhy_clkreq_disable_L1_ar9580_1p0),
                        2);
                } else {
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes,
                        ar9300PciePhy_pll_on_clkreq_disable_L1_ar9580_1p0,
                        ARRAY_LENGTH(
                            ar9300PciePhy_pll_on_clkreq_disable_L1_ar9580_1p0),
                        2);
                }
            } else {//no registry control, default is pll on
                INIT_INI_ARRAY(
                    &ahp->ah_ini_pcie_serdes,
                    ar9300PciePhy_pll_on_clkreq_disable_L1_ar9580_1p0,
                    ARRAY_LENGTH(
                        ar9300PciePhy_pll_on_clkreq_disable_L1_ar9580_1p0),
                    2);
            }
        }

        /*D0 Setting */
        if  (ah->ah_config.ath_hal_pcie_clock_req) {
             if (ah->ah_config.ath_hal_pll_pwr_save &
                AR_PCIE_PLL_PWRSAVE_CONTROL)
             { //registry control
                if (ah->ah_config.ath_hal_pll_pwr_save &
                    AR_PCIE_PLL_PWRSAVE_ON_D0)
                { //bit2, out of D3
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_low_power,
                        ar9300PciePhy_clkreq_enable_L1_ar9580_1p0,
                        ARRAY_LENGTH(ar9300PciePhy_clkreq_enable_L1_ar9580_1p0),
                    2);

                } else {
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_low_power,
                        ar9300PciePhy_pll_on_clkreq_disable_L1_ar9580_1p0,
                        ARRAY_LENGTH(
                            ar9300PciePhy_pll_on_clkreq_disable_L1_ar9580_1p0),
                    2);
                }
            } else { //no registry control, default is pll on
                INIT_INI_ARRAY(
                    &ahp->ah_ini_pcie_serdes_low_power,
                    ar9300PciePhy_pll_on_clkreq_disable_L1_ar9580_1p0,
                    ARRAY_LENGTH(
                        ar9300PciePhy_pll_on_clkreq_disable_L1_ar9580_1p0),
                    2);
            }
        } else {
            if (ah->ah_config.ath_hal_pll_pwr_save &
                AR_PCIE_PLL_PWRSAVE_CONTROL)
            {//registry control
                if (ah->ah_config.ath_hal_pll_pwr_save &
                    AR_PCIE_PLL_PWRSAVE_ON_D0)
                {//bit2, out of D3
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_low_power,
                        ar9300PciePhy_clkreq_disable_L1_ar9580_1p0,
                       ARRAY_LENGTH(ar9300PciePhy_clkreq_disable_L1_ar9580_1p0),
                    2);
                } else {
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_low_power,
                        ar9300PciePhy_pll_on_clkreq_disable_L1_ar9580_1p0,
                        ARRAY_LENGTH(
                            ar9300PciePhy_pll_on_clkreq_disable_L1_ar9580_1p0),
                    2);
                }
            } else { //no registry control, default is pll on
                INIT_INI_ARRAY(
                    &ahp->ah_ini_pcie_serdes_low_power,
                    ar9300PciePhy_pll_on_clkreq_disable_L1_ar9580_1p0,
                    ARRAY_LENGTH(
                        ar9300PciePhy_pll_on_clkreq_disable_L1_ar9580_1p0),
                    2);
            }
        }

        ah->ah_config.ath_hal_pcie_power_save_enable = 0;

#if 0 /* ATH_WOW */
        /* SerDes values during WOW sleep */
        INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_wow, ar9300_pcie_phy_awow,
                       ARRAY_LENGTH(ar9300_pcie_phy_awow), 2);
#endif

        /* Fast clock modal settings */
        INIT_INI_ARRAY(&ahp->ah_ini_modes_additional,
            ar9300Modes_fast_clock_ar9580_1p0,
            ARRAY_LENGTH(ar9300Modes_fast_clock_ar9580_1p0), 3);
        INIT_INI_ARRAY(&ahp->ah_ini_japan2484,
            ar9300_ar9580_1p0_baseband_core_txfir_coeff_japan_2484,
            ARRAY_LENGTH(
                ar9300_ar9580_1p0_baseband_core_txfir_coeff_japan_2484), 2);

    } else {
        /*
         * Osprey 2.2 -  new INI format (pre, core, post arrays per subsystem)
         */

        /* mac */
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_PRE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_CORE],
            ar9300_osprey_2p2_mac_core,
            ARRAY_LENGTH(ar9300_osprey_2p2_mac_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_mac[ATH_INI_POST],
            ar9300_osprey_2p2_mac_postamble,
            ARRAY_LENGTH(ar9300_osprey_2p2_mac_postamble), 5);

        /* bb */
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_PRE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_CORE],
            ar9300_osprey_2p2_baseband_core,
            ARRAY_LENGTH(ar9300_osprey_2p2_baseband_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_bb[ATH_INI_POST],
            ar9300_osprey_2p2_baseband_postamble,
            ARRAY_LENGTH(ar9300_osprey_2p2_baseband_postamble), 5);

        /* radio */
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_PRE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_CORE],
            ar9300_osprey_2p2_radio_core,
            ARRAY_LENGTH(ar9300_osprey_2p2_radio_core), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_radio[ATH_INI_POST],
            ar9300_osprey_2p2_radio_postamble,
            ARRAY_LENGTH(ar9300_osprey_2p2_radio_postamble), 5);

        /* soc */
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_PRE],
            ar9300_osprey_2p2_soc_preamble,
            ARRAY_LENGTH(ar9300_osprey_2p2_soc_preamble), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_CORE], NULL, 0, 0);
        INIT_INI_ARRAY(&ahp->ah_ini_soc[ATH_INI_POST],
            ar9300_osprey_2p2_soc_postamble,
            ARRAY_LENGTH(ar9300_osprey_2p2_soc_postamble), 5);

        /* rx/tx gain */
        INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain,
            ar9300_common_rx_gain_table_osprey_2p2,
            ARRAY_LENGTH(ar9300_common_rx_gain_table_osprey_2p2), 2);
        INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
            ar9300_modes_lowest_ob_db_tx_gain_table_osprey_2p2,
            ARRAY_LENGTH(ar9300_modes_lowest_ob_db_tx_gain_table_osprey_2p2), 5);

        /* DFS */
        INIT_INI_ARRAY(&ahp->ah_ini_dfs,
            ar9300_osprey_2p2_baseband_postamble_dfs_channel,
            ARRAY_LENGTH(ar9300_osprey_2p2_baseband_postamble_dfs_channel), 3);

        /* Load PCIE SERDES settings from INI */

        /*D3 Setting */
        if  (ah->ah_config.ath_hal_pcie_clock_req) {
            if (ah->ah_config.ath_hal_pll_pwr_save & 
                AR_PCIE_PLL_PWRSAVE_CONTROL) 
            { //registry control
                if (ah->ah_config.ath_hal_pll_pwr_save & 
                    AR_PCIE_PLL_PWRSAVE_ON_D3) 
                { //bit1, in to D3
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes, 
                        ar9300PciePhy_clkreq_enable_L1_osprey_2p2,
                        ARRAY_LENGTH(ar9300PciePhy_clkreq_enable_L1_osprey_2p2),
                    2);
                } else {
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes, 
                        ar9300PciePhy_pll_on_clkreq_disable_L1_osprey_2p2,
                        ARRAY_LENGTH(
                            ar9300PciePhy_pll_on_clkreq_disable_L1_osprey_2p2),
                    2);
                }
             } else {//no registry control, default is pll on
#ifndef ATH_BUS_PM
                    INIT_INI_ARRAY(
                        &ahp->ah_ini_pcie_serdes,
                        ar9300PciePhy_pll_on_clkreq_disable_L1_osprey_2p2,
                        ARRAY_LENGTH(
                            ar9300PciePhy_pll_on_clkreq_disable_L1_osprey_2p2),
                    2);
#else
        //no registry control, default is pll off
        INIT_INI_ARRAY(
                &ahp->ah_ini_pcie_serdes,
                ar9300PciePhy_clkreq_disable_L1_osprey_2p2,
                ARRAY_LENGTH(
                    ar9300PciePhy_clkreq_disable_L1_osprey_2p2),
                  2);
#endif

            }
        } else {
            if (ah->ah_config.ath_hal_pll_pwr_save & 
                AR_PCIE_PLL_PWRSAVE_CONTROL) 
            { //registry control
                if (ah->ah_config.ath_hal_pll_pwr_save & 
                    AR_PCIE_PLL_PWRSAVE_ON_D3) 
                { //bit1, in to D3
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes, 
                        ar9300PciePhy_clkreq_disable_L1_osprey_2p2,
                        ARRAY_LENGTH(
                            ar9300PciePhy_clkreq_disable_L1_osprey_2p2),
                        2);
                } else {
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes, 
                       ar9300PciePhy_pll_on_clkreq_disable_L1_osprey_2p2,
                       ARRAY_LENGTH(
                           ar9300PciePhy_pll_on_clkreq_disable_L1_osprey_2p2),
                       2);
                }
             } else {
#ifndef ATH_BUS_PM
        //no registry control, default is pll on
                INIT_INI_ARRAY(
                    &ahp->ah_ini_pcie_serdes,
                    ar9300PciePhy_pll_on_clkreq_disable_L1_osprey_2p2,
                    ARRAY_LENGTH(
                        ar9300PciePhy_pll_on_clkreq_disable_L1_osprey_2p2),
                    2);
#else
        //no registry control, default is pll off
        INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes, ar9300PciePhy_clkreq_disable_L1_osprey_2p2,
                           ARRAY_LENGTH(ar9300PciePhy_clkreq_disable_L1_osprey_2p2), 2);
#endif
            }
        }

        /*D0 Setting */
        if  (ah->ah_config.ath_hal_pcie_clock_req) {
             if (ah->ah_config.ath_hal_pll_pwr_save & 
                AR_PCIE_PLL_PWRSAVE_CONTROL) 
             { //registry control
                if (ah->ah_config.ath_hal_pll_pwr_save & 
                    AR_PCIE_PLL_PWRSAVE_ON_D0)
                { //bit2, out of D3
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_low_power, 
                        ar9300PciePhy_clkreq_enable_L1_osprey_2p2,
                        ARRAY_LENGTH(ar9300PciePhy_clkreq_enable_L1_osprey_2p2),
                    2);

                } else {
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_low_power, 
                        ar9300PciePhy_pll_on_clkreq_disable_L1_osprey_2p2,
                        ARRAY_LENGTH(
                            ar9300PciePhy_pll_on_clkreq_disable_L1_osprey_2p2),
                    2);
                }
            } else { //no registry control, default is pll on
                INIT_INI_ARRAY(
                    &ahp->ah_ini_pcie_serdes_low_power,
                    ar9300PciePhy_pll_on_clkreq_disable_L1_osprey_2p2,
                    ARRAY_LENGTH(
                        ar9300PciePhy_pll_on_clkreq_disable_L1_osprey_2p2),
                    2);
            }
        } else {
            if (ah->ah_config.ath_hal_pll_pwr_save & 
                AR_PCIE_PLL_PWRSAVE_CONTROL) 
            {//registry control
                if (ah->ah_config.ath_hal_pll_pwr_save & 
                    AR_PCIE_PLL_PWRSAVE_ON_D0)
                {//bit2, out of D3
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_low_power, 
                        ar9300PciePhy_clkreq_disable_L1_osprey_2p2,
                       ARRAY_LENGTH(ar9300PciePhy_clkreq_disable_L1_osprey_2p2),
                    2);
                } else {
                    INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_low_power, 
                        ar9300PciePhy_pll_on_clkreq_disable_L1_osprey_2p2,
                        ARRAY_LENGTH(
                            ar9300PciePhy_pll_on_clkreq_disable_L1_osprey_2p2),
                    2);
                }
            } else { //no registry control, default is pll on
                INIT_INI_ARRAY(
                    &ahp->ah_ini_pcie_serdes_low_power,
                    ar9300PciePhy_pll_on_clkreq_disable_L1_osprey_2p2,
                    ARRAY_LENGTH(
                        ar9300PciePhy_pll_on_clkreq_disable_L1_osprey_2p2),
                    2);
            }
        }

        ah->ah_config.ath_hal_pcie_power_save_enable = 0;

#ifdef ATH_BUS_PM
        /*Use HAL to config PCI powersave by writing into the SerDes Registers */
        ah->ah_config.ath_hal_pcie_ser_des_write = 1;
#endif

#if 0 /* ATH_WOW */
        /* SerDes values during WOW sleep */
        INIT_INI_ARRAY(&ahp->ah_ini_pcie_serdes_wow, ar9300_pcie_phy_awow,
                       ARRAY_LENGTH(ar9300_pcie_phy_awow), 2);
#endif

        /* Fast clock modal settings */
        INIT_INI_ARRAY(&ahp->ah_ini_modes_additional,
            ar9300Modes_fast_clock_osprey_2p2,
            ARRAY_LENGTH(ar9300Modes_fast_clock_osprey_2p2), 3);
        INIT_INI_ARRAY(&ahp->ah_ini_japan2484,
            ar9300_osprey_2p2_baseband_core_txfir_coeff_japan_2484,
            ARRAY_LENGTH(
                ar9300_osprey_2p2_baseband_core_txfir_coeff_japan_2484), 2);

    }

    if(AR_SREV_WASP(ah) || AR_SREV_SCORPION(ah))
    {
#define AR_SOC_RST_OTP_INTF  0xB80600B4
#define REG_READ(_reg)       *((volatile u_int32_t *)(_reg))

        ahp->ah_enterprise_mode = REG_READ(AR_SOC_RST_OTP_INTF);
        if (AR_SREV_SCORPION(ah)) {
            ahp->ah_enterprise_mode = ahp->ah_enterprise_mode << 12;
        }
        ath_hal_printf (ah, "Enterprise mode: 0x%08x\n", ahp->ah_enterprise_mode);
#undef REG_READ
#undef AR_SOC_RST_OTP_INTF
    } else {
        ahp->ah_enterprise_mode = OS_REG_READ(ah, AR_ENT_OTP);
    }


    if (ahpriv->ah_ispcie) {
        ar9300_config_pci_power_save(ah, 0, 0);
    } else {
        ar9300_disable_pcie_phy(ah);
    }
#if 0
    ath_hal_printf(ah, "%s: calling ar9300_hw_attach\n", __func__);
#endif
    ecode = ar9300_hw_attach(ah);
    if (ecode != HAL_OK) {
        goto bad;
    }

    /* set gain table pointers according to values read from the eeprom */
    ar9300_tx_gain_table_apply(ah);
    ar9300_rx_gain_table_apply(ah);

    /*
    **
    ** Got everything we need now to setup the capabilities.
    */

    if (!ar9300_fill_capability_info(ah)) {
        HALDEBUG(ah, HAL_DEBUG_RESET,
            "%s:failed ar9300_fill_capability_info\n", __func__);
        ecode = HAL_EEREAD;
        goto bad;
    }
    ecode = ar9300_init_mac_addr(ah);
    if (ecode != HAL_OK) {
        HALDEBUG(ah, HAL_DEBUG_RESET,
            "%s: failed initializing mac address\n", __func__);
        goto bad;
    }

    /*
     * Initialize receive buffer size to MAC default
     */
    ahp->rx_buf_size = HAL_RXBUFSIZE_DEFAULT;

#if ATH_WOW
#if 0
    /*
     * Needs to be removed once we stop using XB92 XXX
     * FIXME: Check with latest boards too - SriniK
     */
    ar9300_wow_set_gpio_reset_low(ah);
#endif

    /*
     * Clear the Wow Status.
     */
    OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_PCIE_PM_CTRL),
        OS_REG_READ(ah, AR_HOSTIF_REG(ah, AR_PCIE_PM_CTRL)) |
        AR_PMCTRL_WOW_PME_CLR);
    OS_REG_WRITE(ah, AR_WOW_PATTERN_REG,
        AR_WOW_CLEAR_EVENTS(OS_REG_READ(ah, AR_WOW_PATTERN_REG)));
#endif

    /*
     * Set the cur_trig_level to a value that works all modes - 11a/b/g or 11n
     * with aggregation enabled or disabled.
     */
    ahp->ah_tx_trig_level = (AR_FTRIG_512B >> AR_FTRIG_S);

    if (AR_SREV_HORNET(ah)) {
        ahp->nf_2GHz.nominal = AR_PHY_CCA_NOM_VAL_HORNET_2GHZ;
        ahp->nf_2GHz.max     = AR_PHY_CCA_MAX_GOOD_VAL_OSPREY_2GHZ;
        ahp->nf_2GHz.min     = AR_PHY_CCA_MIN_GOOD_VAL_OSPREY_2GHZ;
        ahp->nf_5GHz.nominal = AR_PHY_CCA_NOM_VAL_OSPREY_5GHZ;
        ahp->nf_5GHz.max     = AR_PHY_CCA_MAX_GOOD_VAL_OSPREY_5GHZ;
        ahp->nf_5GHz.min     = AR_PHY_CCA_MIN_GOOD_VAL_OSPREY_5GHZ;
        ahp->nf_cw_int_delta = AR_PHY_CCA_CW_INT_DELTA;
    } else if(AR_SREV_JUPITER(ah) || AR_SREV_APHRODITE(ah)){
        ahp->nf_2GHz.nominal = AR_PHY_CCA_NOM_VAL_JUPITER_2GHZ;
        ahp->nf_2GHz.max     = AR_PHY_CCA_MAX_GOOD_VAL_OSPREY_2GHZ;
        ahp->nf_2GHz.min     = AR_PHY_CCA_MIN_GOOD_VAL_JUPITER_2GHZ;
        ahp->nf_5GHz.nominal = AR_PHY_CCA_NOM_VAL_JUPITER_5GHZ;
        ahp->nf_5GHz.max     = AR_PHY_CCA_MAX_GOOD_VAL_OSPREY_5GHZ;
        ahp->nf_5GHz.min     = AR_PHY_CCA_MIN_GOOD_VAL_JUPITER_5GHZ;
        ahp->nf_cw_int_delta = AR_PHY_CCA_CW_INT_DELTA;
    }	else {
        ahp->nf_2GHz.nominal = AR_PHY_CCA_NOM_VAL_OSPREY_2GHZ;
        ahp->nf_2GHz.max     = AR_PHY_CCA_MAX_GOOD_VAL_OSPREY_2GHZ;
        ahp->nf_2GHz.min     = AR_PHY_CCA_MIN_GOOD_VAL_OSPREY_2GHZ;
        if (AR_SREV_AR9580(ah) || AR_SREV_WASP(ah) || AR_SREV_SCORPION(ah)) {
            ahp->nf_5GHz.nominal = AR_PHY_CCA_NOM_VAL_PEACOCK_5GHZ;
        } else {
            ahp->nf_5GHz.nominal = AR_PHY_CCA_NOM_VAL_OSPREY_5GHZ;
        }
        ahp->nf_5GHz.max     = AR_PHY_CCA_MAX_GOOD_VAL_OSPREY_5GHZ;
        ahp->nf_5GHz.min     = AR_PHY_CCA_MIN_GOOD_VAL_OSPREY_5GHZ;
        ahp->nf_cw_int_delta = AR_PHY_CCA_CW_INT_DELTA;
     }




    /* init BB Panic Watchdog timeout */
    if (AR_SREV_HORNET(ah)) {
        ahp->ah_bb_panic_timeout_ms = HAL_BB_PANIC_WD_TMO_HORNET;
    } else {
        ahp->ah_bb_panic_timeout_ms = HAL_BB_PANIC_WD_TMO;
    }


    /*
     * Determine whether tx IQ calibration HW should be enabled,
     * and whether tx IQ calibration should be performed during
     * AGC calibration, or separately.
     */
    if (AR_SREV_JUPITER(ah) || AR_SREV_APHRODITE(ah)) {
        /*
         * Register not initialized yet. This flag will be re-initialized
         * after INI loading following each reset.
         */
        ahp->tx_iq_cal_enable = 1;
        /* if tx IQ cal is enabled, do it together with AGC cal */
        ahp->tx_iq_cal_during_agc_cal = 1;
    } else if (AR_SREV_POSEIDON_OR_LATER(ah) && !AR_SREV_WASP(ah)) {
        ahp->tx_iq_cal_enable = 1;
        ahp->tx_iq_cal_during_agc_cal = 1;
    } else {
        /* osprey, hornet, wasp */
        ahp->tx_iq_cal_enable = 1;
        ahp->tx_iq_cal_during_agc_cal = 0;
    }
    return ah;

bad:
    if (ahp) {
        ar9300_detach((struct ath_hal *) ahp);
    }
    if (status) {
        *status = ecode;
    }
    return AH_NULL;
}

void
ar9300_detach(struct ath_hal *ah)
{
    HALASSERT(ah != AH_NULL);
    HALASSERT(ah->ah_magic == AR9300_MAGIC);

    /* Make sure that chip is awake before writing to it */
    if (!ar9300_set_power_mode(ah, HAL_PM_AWAKE, AH_TRUE)) {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE,
                 "%s: failed to wake up chip\n",
                 __func__);
    }

    ar9300_hw_detach(ah);
    ar9300_set_power_mode(ah, HAL_PM_FULL_SLEEP, AH_TRUE);

//    ath_hal_hdprintf_deregister(ah);

    if (AH9300(ah)->ah_cal_mem)
        ath_hal_free(AH9300(ah)->ah_cal_mem);
    AH9300(ah)->ah_cal_mem = AH_NULL;

    ath_hal_free(ah);
}

struct ath_hal_9300 *
ar9300_new_state(u_int16_t devid, HAL_SOFTC sc,
    HAL_BUS_TAG st, HAL_BUS_HANDLE sh,
    uint16_t *eepromdata,
    HAL_OPS_CONFIG *ah_config,
    HAL_STATUS *status)
{
    static const u_int8_t defbssidmask[IEEE80211_ADDR_LEN] =
        { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    struct ath_hal_9300 *ahp;
    struct ath_hal *ah;

    /* NB: memory is returned zero'd */
    ahp = ath_hal_malloc(sizeof(struct ath_hal_9300));
    if (ahp == AH_NULL) {
        HALDEBUG(AH_NULL, HAL_DEBUG_UNMASKABLE,
                 "%s: cannot allocate memory for state block\n",
                 __func__);
        *status = HAL_ENOMEM;
        return AH_NULL;
    }

    ah = &ahp->ah_priv.h;
    /* set initial values */

    /* stub everything first */
    ar9300_set_stub_functions(ah);

    /* setup the FreeBSD HAL methods */
    ar9300_attach_freebsd_ops(ah);

    /* These are private to this particular file, so .. */
    ah->ah_disablePCIE = ar9300_disable_pcie_phy;
    AH_PRIVATE(ah)->ah_getNfAdjust = ar9300_get_nf_adjust;
    AH_PRIVATE(ah)->ah_getChipPowerLimits = ar9300_get_chip_power_limits;

#if 0
    /* Attach Osprey structure as default hal structure */
    OS_MEMCPY(&ahp->ah_priv.priv, &ar9300hal, sizeof(ahp->ah_priv.priv));
#endif

#if 0
    AH_PRIVATE(ah)->amem_handle = amem_handle;
    AH_PRIVATE(ah)->ah_osdev = osdev;
#endif
    ah->ah_sc = sc;
    ah->ah_st = st;
    ah->ah_sh = sh;
    ah->ah_magic = AR9300_MAGIC;
    AH_PRIVATE(ah)->ah_devid = devid;

    AH_PRIVATE(ah)->ah_flags = 0;
   
    /*
    ** Initialize factory defaults in the private space
    */
//    ath_hal_factory_defaults(AH_PRIVATE(ah), hal_conf_parm);
    ar9300_config_defaults_freebsd(ah, ah_config);

    /* XXX FreeBSD: cal is always in EEPROM */
#if 0
    if (!hal_conf_parm->calInFlash) {
        AH_PRIVATE(ah)->ah_flags |= AH_USE_EEPROM;
    }
#endif
    AH_PRIVATE(ah)->ah_flags |= AH_USE_EEPROM;
   
#if 0
    if (ar9300_eep_data_in_flash(ah)) {
        ahp->ah_priv.priv.ah_eeprom_read  = ar9300_flash_read;
        ahp->ah_priv.priv.ah_eeprom_dump  = AH_NULL;
    } else {
        ahp->ah_priv.priv.ah_eeprom_read  = ar9300_eeprom_read_word;
    }
#endif

    /* XXX FreeBSD - for now, just supports EEPROM reading */
    ahp->ah_priv.ah_eepromRead = ar9300_eeprom_read_word;

    AH_PRIVATE(ah)->ah_powerLimit = MAX_RATE_POWER;
    AH_PRIVATE(ah)->ah_tpScale = HAL_TP_SCALE_MAX;  /* no scaling */

    ahp->ah_atim_window = 0;         /* [0..1000] */

    ahp->ah_diversity_control =
        ah->ah_config.ath_hal_diversity_control;
    ahp->ah_antenna_switch_swap =
        ah->ah_config.ath_hal_antenna_switch_swap;

    /*
     * Enable MIC handling.
     */
    ahp->ah_sta_id1_defaults = AR_STA_ID1_CRPT_MIC_ENABLE;
    ahp->ah_enable32k_hz_clock = DONT_USE_32KHZ;/* XXX */
    ahp->ah_slot_time = (u_int) -1;
    ahp->ah_ack_timeout = (u_int) -1;
    OS_MEMCPY(&ahp->ah_bssid_mask, defbssidmask, IEEE80211_ADDR_LEN);

    /*
     * 11g-specific stuff
     */
    ahp->ah_g_beacon_rate = 0;        /* adhoc beacon fixed rate */

    /* SM power mode: Attach time, disable any setting */
    ahp->ah_sm_power_mode = HAL_SMPS_DEFAULT;

    return ahp;
}

HAL_BOOL
ar9300_chip_test(struct ath_hal *ah)
{
    /*u_int32_t reg_addr[2] = { AR_STA_ID0, AR_PHY_BASE+(8 << 2) };*/
    u_int32_t reg_addr[2] = { AR_STA_ID0 };
    u_int32_t reg_hold[2];
    u_int32_t pattern_data[4] =
        { 0x55555555, 0xaaaaaaaa, 0x66666666, 0x99999999 };
    int i, j;

    /* Test PHY & MAC registers */
    for (i = 0; i < 1; i++) {
        u_int32_t addr = reg_addr[i];
        u_int32_t wr_data, rd_data;

        reg_hold[i] = OS_REG_READ(ah, addr);
        for (j = 0; j < 0x100; j++) {
            wr_data = (j << 16) | j;
            OS_REG_WRITE(ah, addr, wr_data);
            rd_data = OS_REG_READ(ah, addr);
            if (rd_data != wr_data) {
                HALDEBUG(ah, HAL_DEBUG_REGIO,
                    "%s: address test failed addr: "
                    "0x%08x - wr:0x%08x != rd:0x%08x\n",
                    __func__, addr, wr_data, rd_data);
                return AH_FALSE;
            }
        }
        for (j = 0; j < 4; j++) {
            wr_data = pattern_data[j];
            OS_REG_WRITE(ah, addr, wr_data);
            rd_data = OS_REG_READ(ah, addr);
            if (wr_data != rd_data) {
                HALDEBUG(ah, HAL_DEBUG_REGIO,
                    "%s: address test failed addr: "
                    "0x%08x - wr:0x%08x != rd:0x%08x\n",
                    __func__, addr, wr_data, rd_data);
                return AH_FALSE;
            }
        }
        OS_REG_WRITE(ah, reg_addr[i], reg_hold[i]);
    }
    OS_DELAY(100);
    return AH_TRUE;
}

/*
 * Store the channel edges for the requested operational mode
 */
HAL_BOOL
ar9300_get_channel_edges(struct ath_hal *ah,
    u_int16_t flags, u_int16_t *low, u_int16_t *high)
{
    struct ath_hal_private *ahpriv = AH_PRIVATE(ah);
    HAL_CAPABILITIES *p_cap = &ahpriv->ah_caps;

    if (flags & IEEE80211_CHAN_5GHZ) {
        *low = p_cap->halLow5GhzChan;
        *high = p_cap->halHigh5GhzChan;
        return AH_TRUE;
    }
    if ((flags & IEEE80211_CHAN_2GHZ)) {
        *low = p_cap->halLow2GhzChan;
        *high = p_cap->halHigh2GhzChan;

        return AH_TRUE;
    }
    return AH_FALSE;
}

HAL_BOOL
ar9300_regulatory_domain_override(struct ath_hal *ah, u_int16_t regdmn)
{
    AH_PRIVATE(ah)->ah_currentRD = regdmn;
    return AH_TRUE;
}

/*
 * Fill all software cached or static hardware state information.
 * Return failure if capabilities are to come from EEPROM and
 * cannot be read.
 */
HAL_BOOL
ar9300_fill_capability_info(struct ath_hal *ah)
{
#define AR_KEYTABLE_SIZE    128
    struct ath_hal_9300 *ahp = AH9300(ah);
    struct ath_hal_private *ahpriv = AH_PRIVATE(ah);
    HAL_CAPABILITIES *p_cap = &ahpriv->ah_caps;
    u_int16_t cap_field = 0, eeval;

    ahpriv->ah_devType = (u_int16_t)ar9300_eeprom_get(ahp, EEP_DEV_TYPE);
    eeval = ar9300_eeprom_get(ahp, EEP_REG_0);

    /* XXX record serial number */
    AH_PRIVATE(ah)->ah_currentRD = eeval;

    /* Always enable fast clock; leave it up to EEPROM and channel */
    p_cap->halSupportsFastClock5GHz = AH_TRUE;

    p_cap->halIntrMitigation = AH_TRUE;
    eeval = ar9300_eeprom_get(ahp, EEP_REG_1);
    AH_PRIVATE(ah)->ah_currentRDext = eeval | AR9300_RDEXT_DEFAULT;

    /* Read the capability EEPROM location */
    cap_field = ar9300_eeprom_get(ahp, EEP_OP_CAP);

    /* Construct wireless mode from EEPROM */
    p_cap->halWirelessModes = 0;
    eeval = ar9300_eeprom_get(ahp, EEP_OP_MODE);

    /*
     * XXX FreeBSD specific: for now, set ath_hal_ht_enable to 1,
     * or we won't have 11n support.
     */
    ah->ah_config.ath_hal_ht_enable = 1;

    if (eeval & AR9300_OPFLAGS_11A) {
        p_cap->halWirelessModes |= HAL_MODE_11A |
            ((!ah->ah_config.ath_hal_ht_enable ||
              (eeval & AR9300_OPFLAGS_N_5G_HT20)) ?  0 :
             (HAL_MODE_11NA_HT20 | ((eeval & AR9300_OPFLAGS_N_5G_HT40) ? 0 :
                                    (HAL_MODE_11NA_HT40PLUS | HAL_MODE_11NA_HT40MINUS))));
    }
    if (eeval & AR9300_OPFLAGS_11G) {
        p_cap->halWirelessModes |= HAL_MODE_11B | HAL_MODE_11G |
            ((!ah->ah_config.ath_hal_ht_enable ||
              (eeval & AR9300_OPFLAGS_N_2G_HT20)) ?  0 :
             (HAL_MODE_11NG_HT20 | ((eeval & AR9300_OPFLAGS_N_2G_HT40) ? 0 :
                                    (HAL_MODE_11NG_HT40PLUS | HAL_MODE_11NG_HT40MINUS))));
    }

    /* Get chainamsks from eeprom */
    p_cap->halTxChainMask = ar9300_eeprom_get(ahp, EEP_TX_MASK);
    p_cap->halRxChainMask = ar9300_eeprom_get(ahp, EEP_RX_MASK);



#define owl_get_ntxchains(_txchainmask) \
    (((_txchainmask >> 2) & 1) + ((_txchainmask >> 1) & 1) + (_txchainmask & 1))

    /* FreeBSD: Update number of TX/RX streams */
    p_cap->halTxStreams = owl_get_ntxchains(p_cap->halTxChainMask);
    p_cap->halRxStreams = owl_get_ntxchains(p_cap->halRxChainMask);


    /*
     * This being a newer chip supports TKIP non-splitmic mode.
     *
     */
    ahp->ah_misc_mode |= AR_PCU_MIC_NEW_LOC_ENA;
    p_cap->halTkipMicTxRxKeySupport = AH_TRUE;

    p_cap->halLow2GhzChan = 2312;
    p_cap->halHigh2GhzChan = 2732;

    p_cap->halLow5GhzChan = 4920;
    p_cap->halHigh5GhzChan = 6100;

    p_cap->halCipherCkipSupport = AH_FALSE;
    p_cap->halCipherTkipSupport = AH_TRUE;
    p_cap->halCipherAesCcmSupport = AH_TRUE;

    p_cap->halMicCkipSupport = AH_FALSE;
    p_cap->halMicTkipSupport = AH_TRUE;
    p_cap->halMicAesCcmSupport = AH_TRUE;

    p_cap->halChanSpreadSupport = AH_TRUE;
    p_cap->halSleepAfterBeaconBroken = AH_TRUE;

    p_cap->halBurstSupport = AH_TRUE;
    p_cap->halChapTuningSupport = AH_TRUE;
    p_cap->halTurboPrimeSupport = AH_TRUE;
    p_cap->halFastFramesSupport = AH_TRUE;

    p_cap->halTurboGSupport = p_cap->halWirelessModes & HAL_MODE_108G;

//    p_cap->hal_xr_support = AH_FALSE;

    p_cap->halHTSupport =
        ah->ah_config.ath_hal_ht_enable ?  AH_TRUE : AH_FALSE;

    p_cap->halGTTSupport = AH_TRUE;
    p_cap->halPSPollBroken = AH_TRUE;    /* XXX fixed in later revs? */
    p_cap->halNumMRRetries = 4;		/* Hardware supports 4 MRR */
    p_cap->halHTSGI20Support = AH_TRUE;
    p_cap->halVEOLSupport = AH_TRUE;
    p_cap->halBssIdMaskSupport = AH_TRUE;
    /* Bug 26802, fixed in later revs? */
    p_cap->halMcastKeySrchSupport = AH_TRUE;
    p_cap->halTsfAddSupport = AH_TRUE;

    if (cap_field & AR_EEPROM_EEPCAP_MAXQCU) {
        p_cap->halTotalQueues = MS(cap_field, AR_EEPROM_EEPCAP_MAXQCU);
    } else {
        p_cap->halTotalQueues = HAL_NUM_TX_QUEUES;
    }

    if (cap_field & AR_EEPROM_EEPCAP_KC_ENTRIES) {
        p_cap->halKeyCacheSize =
            1 << MS(cap_field, AR_EEPROM_EEPCAP_KC_ENTRIES);
    } else {
        p_cap->halKeyCacheSize = AR_KEYTABLE_SIZE;
    }
    p_cap->halFastCCSupport = AH_TRUE;
//    p_cap->hal_num_mr_retries = 4;
//    ahp->hal_tx_trig_level_max = MAX_TX_FIFO_THRESHOLD;

    p_cap->halNumGpioPins = AR9382_MAX_GPIO_PIN_NUM;

#if 0
    /* XXX Verify support in Osprey */
    if (AR_SREV_MERLIN_10_OR_LATER(ah)) {
        p_cap->halWowSupport = AH_TRUE;
        p_cap->hal_wow_match_pattern_exact = AH_TRUE;
        if (AR_SREV_MERLIN(ah)) {
            p_cap->hal_wow_pattern_match_dword = AH_TRUE;
        }
    } else {
        p_cap->halWowSupport = AH_FALSE;
        p_cap->hal_wow_match_pattern_exact = AH_FALSE;
    }
#endif
    p_cap->halWowSupport = AH_TRUE;
    p_cap->halWowMatchPatternExact = AH_TRUE;
    if (AR_SREV_POSEIDON(ah)) {
        p_cap->halWowMatchPatternExact = AH_TRUE;
    }

    p_cap->halCSTSupport = AH_TRUE;

    p_cap->halRifsRxSupport = AH_TRUE;
    p_cap->halRifsTxSupport = AH_TRUE;

#define	IEEE80211_AMPDU_LIMIT_MAX (65536)
    p_cap->halRtsAggrLimit = IEEE80211_AMPDU_LIMIT_MAX;
#undef IEEE80211_AMPDU_LIMIT_MAX

    p_cap->halMfpSupport = ah->ah_config.ath_hal_mfp_support;

    p_cap->halForcePpmSupport = AH_TRUE;
    p_cap->halHwBeaconProcSupport = AH_TRUE;
    
    /* ar9300 - has the HW UAPSD trigger support,
     * but it has the following limitations
     * The power state change from the following
     * frames are not put in High priority queue.
     *     i) Mgmt frames
     *     ii) NoN QoS frames
     *     iii) QoS frames form the access categories for which
     *          UAPSD is not enabled.
     * so we can not enable this feature currently.
     * could be enabled, if these limitations are fixed
     * in later versions of ar9300 chips
     */
    p_cap->halHasUapsdSupport = AH_FALSE;

    /* Number of buffers that can be help in a single TxD */
    p_cap->halNumTxMaps = 4;

    p_cap->halTxDescLen = sizeof(struct ar9300_txc);
    p_cap->halTxStatusLen = sizeof(struct ar9300_txs);
    p_cap->halRxStatusLen = sizeof(struct ar9300_rxs);

    p_cap->halRxHpFifoDepth = HAL_HP_RXFIFO_DEPTH;
    p_cap->halRxLpFifoDepth = HAL_LP_RXFIFO_DEPTH;

    /* Enable extension channel DFS support */
    p_cap->halUseCombinedRadarRssi = AH_TRUE;
    p_cap->halExtChanDfsSupport = AH_TRUE;
#if ATH_SUPPORT_SPECTRAL
    p_cap->halSpectralScanSupport = AH_TRUE;
#endif
    ahpriv->ah_rfsilent = ar9300_eeprom_get(ahp, EEP_RF_SILENT);
    if (ahpriv->ah_rfsilent & EEP_RFSILENT_ENABLED) {
        ahp->ah_gpio_select = MS(ahpriv->ah_rfsilent, EEP_RFSILENT_GPIO_SEL);
        ahp->ah_polarity   = MS(ahpriv->ah_rfsilent, EEP_RFSILENT_POLARITY);

        ath_hal_enable_rfkill(ah, AH_TRUE);
        p_cap->halRfSilentSupport = AH_TRUE;
    }

    /* XXX */
    p_cap->halWpsPushButtonSupport = AH_FALSE;

#ifdef ATH_BT_COEX
    p_cap->halBtCoexSupport = AH_TRUE;
    p_cap->halBtCoexApsmWar = AH_FALSE;
#endif

    p_cap->halGenTimerSupport = AH_TRUE;
    ahp->ah_avail_gen_timers = ~((1 << AR_FIRST_NDP_TIMER) - 1);
    ahp->ah_avail_gen_timers &= (1 << AR_NUM_GEN_TIMERS) - 1;
    /*
     * According to Kyungwan, generic timer 0 and 8 are special
     * timers. Remove timer 8 from the available gen timer list.
     * Jupiter testing shows timer won't trigger with timer 8.
     */
    ahp->ah_avail_gen_timers &= ~(1 << AR_GEN_TIMER_RESERVED);

    if (AR_SREV_JUPITER(ah) || AR_SREV_APHRODITE(ah)) {
#if ATH_SUPPORT_MCI
        if (ah->ah_config.ath_hal_mci_config & ATH_MCI_CONFIG_DISABLE_MCI) 
        {
            p_cap->halMciSupport = AH_FALSE;
        }
        else
#endif
        {
            p_cap->halMciSupport = (ahp->ah_enterprise_mode & 
                            AR_ENT_OTP_49GHZ_DISABLE) ? AH_FALSE: AH_TRUE;
        }
        HALDEBUG(AH_NULL, HAL_DEBUG_UNMASKABLE,
                 "%s: (MCI) MCI support = %d\n",
                 __func__, p_cap->halMciSupport);
    }
    else {
        p_cap->halMciSupport = AH_FALSE;
    }

    /* XXX TODO: jupiter 2.1? */
    if (AR_SREV_JUPITER_20(ah)) {
        p_cap->halRadioRetentionSupport = AH_TRUE;
    } else {
        p_cap->halRadioRetentionSupport = AH_FALSE;
    }

    p_cap->halAutoSleepSupport = AH_TRUE;

    p_cap->halMbssidAggrSupport = AH_TRUE;
//    p_cap->hal_proxy_sta_support = AH_TRUE;

    /* XXX Mark it true after it is verfied as fixed */
    p_cap->hal4kbSplitTransSupport = AH_FALSE;

    /* Read regulatory domain flag */
    if (AH_PRIVATE(ah)->ah_currentRDext & (1 << REG_EXT_JAPAN_MIDBAND)) {
        /*
         * If REG_EXT_JAPAN_MIDBAND is set, turn on U1 EVEN, U2, and MIDBAND.
         */
        p_cap->halRegCap =
            AR_EEPROM_EEREGCAP_EN_KK_NEW_11A |
            AR_EEPROM_EEREGCAP_EN_KK_U1_EVEN |
            AR_EEPROM_EEREGCAP_EN_KK_U2      |
            AR_EEPROM_EEREGCAP_EN_KK_MIDBAND;
    } else {
        p_cap->halRegCap =
            AR_EEPROM_EEREGCAP_EN_KK_NEW_11A | AR_EEPROM_EEREGCAP_EN_KK_U1_EVEN;
    }

    /* For AR9300 and above, midband channels are always supported */
    p_cap->halRegCap |= AR_EEPROM_EEREGCAP_EN_FCC_MIDBAND;

    p_cap->halNumAntCfg5GHz =
        ar9300_eeprom_get_num_ant_config(ahp, HAL_FREQ_BAND_5GHZ);
    p_cap->halNumAntCfg2GHz =
        ar9300_eeprom_get_num_ant_config(ahp, HAL_FREQ_BAND_2GHZ);

    /* STBC supported */
    p_cap->halRxStbcSupport = 1; /* number of streams for STBC recieve. */
    if (AR_SREV_HORNET(ah) || AR_SREV_POSEIDON(ah) || AR_SREV_APHRODITE(ah)) {
        p_cap->halTxStbcSupport = 0;
    } else {
        p_cap->halTxStbcSupport = 1;
    }

    p_cap->halEnhancedDmaSupport = AH_TRUE;
    p_cap->halEnhancedDfsSupport = AH_TRUE;

    /*
     *  EV61133 (missing interrupts due to AR_ISR_RAC).
     *  Fixed in Osprey 2.0.
     */
    p_cap->halIsrRacSupport = AH_TRUE;

    /* XXX FreeBSD won't support TKIP and WEP aggregation */
#if 0
    p_cap->hal_wep_tkip_aggr_support = AH_TRUE;
    p_cap->hal_wep_tkip_aggr_num_tx_delim = 10;    /* TBD */
    p_cap->hal_wep_tkip_aggr_num_rx_delim = 10;    /* TBD */
    p_cap->hal_wep_tkip_max_ht_rate = 15;         /* TBD */
#endif

    /*
     * XXX FreeBSD won't need these; but eventually add them
     * and add the WARs - AGGR extra delim WAR is useful to know
     * about.
     */
#if 0
    p_cap->hal_cfend_fix_support = AH_FALSE;
    p_cap->hal_aggr_extra_delim_war = AH_FALSE;
#endif
    p_cap->halTxTstampPrecision = 32;
    p_cap->halRxTstampPrecision = 32;
    p_cap->halRxTxAbortSupport = AH_TRUE;
    p_cap->hal_ani_poll_interval = AR9300_ANI_POLLINTERVAL;
    p_cap->hal_channel_switch_time_usec = AR9300_CHANNEL_SWITCH_TIME_USEC;
  
    /* Transmit Beamforming supported, fill capabilities */
    p_cap->halPaprdEnabled = ar9300_eeprom_get(ahp, EEP_PAPRD_ENABLED);
    p_cap->halChanHalfRate =
        !(ahp->ah_enterprise_mode & AR_ENT_OTP_10MHZ_DISABLE);
    p_cap->halChanQuarterRate =
        !(ahp->ah_enterprise_mode & AR_ENT_OTP_5MHZ_DISABLE);
	
    if(AR_SREV_JUPITER(ah) || AR_SREV_APHRODITE(ah)){
        /* There is no AR_ENT_OTP_49GHZ_DISABLE feature in Jupiter, now the bit is used to disable BT. */		
        p_cap->hal49GhzSupport = 1;
    } else {
        p_cap->hal49GhzSupport = !(ahp->ah_enterprise_mode & AR_ENT_OTP_49GHZ_DISABLE);
    }

    if (AR_SREV_POSEIDON(ah) || AR_SREV_HORNET(ah) || AR_SREV_APHRODITE(ah)) {
        /* LDPC supported */
        /* Poseidon doesn't support LDPC, or it will cause receiver CRC Error */
        p_cap->halLDPCSupport = AH_FALSE;
        /* PCI_E LCR offset */
        if (AR_SREV_POSEIDON(ah)) {
            p_cap->hal_pcie_lcr_offset = 0x80; /*for Poseidon*/
        }
        /*WAR method for APSM L0s with Poseidon 1.0*/
        if (AR_SREV_POSEIDON_10(ah)) {
            p_cap->hal_pcie_lcr_extsync_en = AH_TRUE;
        }
    } else {
        p_cap->halLDPCSupport = AH_TRUE;
    }
    
    /* XXX is this a flag, or a chainmask number? */
    p_cap->halApmEnable = !! ar9300_eeprom_get(ahp, EEP_CHAIN_MASK_REDUCE);
#if ATH_ANT_DIV_COMB        
    if (AR_SREV_HORNET(ah) || AR_SREV_POSEIDON_11_OR_LATER(ah) || AR_SREV_APHRODITE(ah)) {
        if (ahp->ah_diversity_control == HAL_ANT_VARIABLE) {
            u_int8_t ant_div_control1 = 
                ar9300_eeprom_get(ahp, EEP_ANTDIV_control);
            /* if enable_lnadiv is 0x1 and enable_fast_div is 0x1, 
             * we enable the diversity-combining algorithm. 
             */
            if ((ant_div_control1 >> 0x6) == 0x3) {
                p_cap->halAntDivCombSupport = AH_TRUE;
            }            
            p_cap->halAntDivCombSupportOrg = p_cap->halAntDivCombSupport;
        }
    }
#endif /* ATH_ANT_DIV_COMB */

    /*
     * FreeBSD: enable LNA mixing if the chip is Hornet or Poseidon.
     */
    if (AR_SREV_HORNET(ah) || AR_SREV_POSEIDON_11_OR_LATER(ah)) {
        p_cap->halRxUsingLnaMixing = AH_TRUE;
    }

    /*
     * AR5416 and later NICs support MYBEACON filtering.
     */
    p_cap->halRxDoMyBeacon = AH_TRUE;

#if ATH_WOW_OFFLOAD
    if (AR_SREV_JUPITER_20_OR_LATER(ah) || AR_SREV_APHRODITE(ah)) {
        p_cap->hal_wow_gtk_offload_support    = AH_TRUE;
        p_cap->hal_wow_arp_offload_support    = AH_TRUE;
        p_cap->hal_wow_ns_offload_support     = AH_TRUE;
        p_cap->hal_wow_4way_hs_wakeup_support = AH_TRUE;
        p_cap->hal_wow_acer_magic_support     = AH_TRUE;
        p_cap->hal_wow_acer_swka_support      = AH_TRUE;
    } else {
        p_cap->hal_wow_gtk_offload_support    = AH_FALSE;
        p_cap->hal_wow_arp_offload_support    = AH_FALSE;
        p_cap->hal_wow_ns_offload_support     = AH_FALSE;
        p_cap->hal_wow_4way_hs_wakeup_support = AH_FALSE;
        p_cap->hal_wow_acer_magic_support     = AH_FALSE;
        p_cap->hal_wow_acer_swka_support      = AH_FALSE;
    }
#endif /* ATH_WOW_OFFLOAD */


    return AH_TRUE;
#undef AR_KEYTABLE_SIZE
}

#if 0
static HAL_BOOL
ar9300_get_chip_power_limits(struct ath_hal *ah, HAL_CHANNEL *chans,
    u_int32_t nchans)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    return ahp->ah_rf_hal.get_chip_power_lim(ah, chans, nchans);
}
#endif
/* XXX FreeBSD */

static HAL_BOOL
ar9300_get_chip_power_limits(struct ath_hal *ah,
    struct ieee80211_channel *chan)
{

	chan->ic_maxpower = AR9300_MAX_RATE_POWER;
	chan->ic_minpower = 0;

	return AH_TRUE;
}

/*
 * Disable PLL when in L0s as well as receiver clock when in L1.
 * This power saving option must be enabled through the Serdes.
 *
 * Programming the Serdes must go through the same 288 bit serial shift
 * register as the other analog registers.  Hence the 9 writes.
 *
 * XXX Clean up the magic numbers.
 */
void
ar9300_config_pci_power_save(struct ath_hal *ah, int restore, int power_off)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    int i;

    if (AH_PRIVATE(ah)->ah_ispcie != AH_TRUE) {
        return;
    }

    /*
     * Increase L1 Entry Latency. Some WB222 boards don't have
     * this change in eeprom/OTP.
     */
    if (AR_SREV_JUPITER(ah)) {
        u_int32_t val = ah->ah_config.ath_hal_war70c;
        if ((val & 0xff000000) == 0x17000000) {
            val &= 0x00ffffff;
            val |= 0x27000000;
            OS_REG_WRITE(ah, 0x570c, val);
        }
    }

    /* Do not touch SERDES registers */
    if (ah->ah_config.ath_hal_pcie_power_save_enable == 2) {
        return;
    }

    /* Nothing to do on restore for 11N */
    if (!restore) {
        /* set bit 19 to allow forcing of pcie core into L1 state */
        OS_REG_SET_BIT(ah,
            AR_HOSTIF_REG(ah, AR_PCIE_PM_CTRL), AR_PCIE_PM_CTRL_ENA);

        /*
         * Set PCIE workaround config only if requested, else use the reset
         * value of this register.
         */
        if (ah->ah_config.ath_hal_pcie_waen) {
            OS_REG_WRITE(ah,
                AR_HOSTIF_REG(ah, AR_WA),
                ah->ah_config.ath_hal_pcie_waen);
        } else {
            /* Set Bits 17 and 14 in the AR_WA register. */
            OS_REG_WRITE(ah, AR_HOSTIF_REG(ah, AR_WA), ahp->ah_wa_reg_val);
        }
    }

    /* Configure PCIE after Ini init. SERDES values now come from ini file */
    if (ah->ah_config.ath_hal_pcie_ser_des_write) {
        if (power_off) {
            for (i = 0; i < ahp->ah_ini_pcie_serdes.ia_rows; i++) {
                OS_REG_WRITE(ah,
                    INI_RA(&ahp->ah_ini_pcie_serdes, i, 0),
                    INI_RA(&ahp->ah_ini_pcie_serdes, i, 1));
            }
        } else {
            for (i = 0; i < ahp->ah_ini_pcie_serdes_low_power.ia_rows; i++) {
                OS_REG_WRITE(ah,
                    INI_RA(&ahp->ah_ini_pcie_serdes_low_power, i, 0),
                    INI_RA(&ahp->ah_ini_pcie_serdes_low_power, i, 1));
            }
        }
    }

}

/*
 * Recipe from charles to turn off PCIe PHY in PCI mode for power savings
 */
void
ar9300_disable_pcie_phy(struct ath_hal *ah)
{
    /* Osprey does not support PCI mode */
}

static inline HAL_STATUS
ar9300_init_mac_addr(struct ath_hal *ah)
{
    u_int32_t sum;
    int i;
    u_int16_t eeval;
    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int32_t EEP_MAC [] = { EEP_MAC_LSW, EEP_MAC_MID, EEP_MAC_MSW };

    sum = 0;
    for (i = 0; i < 3; i++) {
        eeval = ar9300_eeprom_get(ahp, EEP_MAC[i]);
        sum += eeval;
        ahp->ah_macaddr[2*i] = eeval >> 8;
        ahp->ah_macaddr[2*i + 1] = eeval & 0xff;
    }
    if (sum == 0 || sum == 0xffff*3) {
        HALDEBUG(ah, HAL_DEBUG_EEPROM, "%s: mac address read failed: %s\n",
            __func__, ath_hal_ether_sprintf(ahp->ah_macaddr));
        return HAL_EEBADMAC;
    }

    return HAL_OK;
}

/*
 * Code for the "real" chip i.e. non-emulation. Review and revisit
 * when actual hardware is at hand.
 */
static inline HAL_STATUS
ar9300_hw_attach(struct ath_hal *ah)
{
    HAL_STATUS ecode;

    if (!ar9300_chip_test(ah)) {
        HALDEBUG(ah, HAL_DEBUG_REGIO,
            "%s: hardware self-test failed\n", __func__);
        return HAL_ESELFTEST;
    }

#if 0
    ath_hal_printf(ah, "%s: calling ar9300_eeprom_attach\n", __func__);
#endif
    ecode = ar9300_eeprom_attach(ah);
    ath_hal_printf(ah, "%s: ar9300_eeprom_attach returned %d\n", __func__, ecode);
    if (ecode != HAL_OK) {
        return ecode;
    }
    if (!ar9300_rf_attach(ah, &ecode)) {
        HALDEBUG(ah, HAL_DEBUG_RESET, "%s: RF setup failed, status %u\n",
            __func__, ecode);
    }

    if (ecode != HAL_OK) {
        return ecode;
    }
    ar9300_ani_attach(ah);

    return HAL_OK;
}

static inline void
ar9300_hw_detach(struct ath_hal *ah)
{
    /* XXX EEPROM allocated state */
    ar9300_ani_detach(ah);
}

static int16_t
ar9300_get_nf_adjust(struct ath_hal *ah, const HAL_CHANNEL_INTERNAL *c)
{
    return 0;
}

void
ar9300_set_immunity(struct ath_hal *ah, HAL_BOOL enable)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int32_t m1_thresh_low = enable ? 127 : ahp->ah_immunity_vals[0],
              m2_thresh_low = enable ? 127 : ahp->ah_immunity_vals[1],
              m1_thresh = enable ? 127 : ahp->ah_immunity_vals[2],
              m2_thresh = enable ? 127 : ahp->ah_immunity_vals[3],
              m2_count_thr = enable ? 31 : ahp->ah_immunity_vals[4],
              m2_count_thr_low = enable ? 63 : ahp->ah_immunity_vals[5];

    if (ahp->ah_immunity_on == enable) {
        return;
    }

    ahp->ah_immunity_on = enable;

    OS_REG_RMW_FIELD(ah, AR_PHY_SFCORR_LOW,
                     AR_PHY_SFCORR_LOW_M1_THRESH_LOW, m1_thresh_low);
    OS_REG_RMW_FIELD(ah, AR_PHY_SFCORR_LOW,
                     AR_PHY_SFCORR_LOW_M2_THRESH_LOW, m2_thresh_low);
    OS_REG_RMW_FIELD(ah, AR_PHY_SFCORR,
                     AR_PHY_SFCORR_M1_THRESH, m1_thresh);
    OS_REG_RMW_FIELD(ah, AR_PHY_SFCORR,
                     AR_PHY_SFCORR_M2_THRESH, m2_thresh);
    OS_REG_RMW_FIELD(ah, AR_PHY_SFCORR,
                     AR_PHY_SFCORR_M2COUNT_THR, m2_count_thr);
    OS_REG_RMW_FIELD(ah, AR_PHY_SFCORR_LOW,
                     AR_PHY_SFCORR_LOW_M2COUNT_THR_LOW, m2_count_thr_low);

    OS_REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT,
                     AR_PHY_SFCORR_EXT_M1_THRESH_LOW, m1_thresh_low);
    OS_REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT,
                     AR_PHY_SFCORR_EXT_M2_THRESH_LOW, m2_thresh_low);
    OS_REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT,
                     AR_PHY_SFCORR_EXT_M1_THRESH, m1_thresh);
    OS_REG_RMW_FIELD(ah, AR_PHY_SFCORR_EXT,
                     AR_PHY_SFCORR_EXT_M2_THRESH, m2_thresh);

    if (!enable) {
        OS_REG_SET_BIT(ah, AR_PHY_SFCORR_LOW,
                       AR_PHY_SFCORR_LOW_USE_SELF_CORR_LOW);
    } else {
        OS_REG_CLR_BIT(ah, AR_PHY_SFCORR_LOW,
                       AR_PHY_SFCORR_LOW_USE_SELF_CORR_LOW);
    }
}

/* XXX FreeBSD: I'm not sure how to implement this.. */
#if 0
int
ar9300_get_cal_intervals(struct ath_hal *ah, HAL_CALIBRATION_TIMER **timerp,
    HAL_CAL_QUERY query)
{
#define AR9300_IS_CHAIN_RX_IQCAL_INVALID(_ah, _reg) \
    ((OS_REG_READ((_ah), _reg) & 0x3fff) == 0)
#define AR9300_IS_RX_IQCAL_DISABLED(_ah) \
    (!(OS_REG_READ((_ah), AR_PHY_RX_IQCAL_CORR_B0) & \
    AR_PHY_RX_IQCAL_CORR_IQCORR_ENABLE))
/* Avoid comilation warnings. Variables are not used when EMULATION. */
    struct ath_hal_9300 *ahp = AH9300(ah);
    u_int8_t rxchainmask = ahp->ah_rx_chainmask, i;
    int rx_iqcal_invalid = 0, num_chains = 0;
    static const u_int32_t offset_array[3] = {
        AR_PHY_RX_IQCAL_CORR_B0,
        AR_PHY_RX_IQCAL_CORR_B1,
        AR_PHY_RX_IQCAL_CORR_B2};

    *timerp = ar9300_cals;

    switch (query) {
    case HAL_QUERY_CALS:
        return AR9300_NUM_CAL_TYPES;
    case HAL_QUERY_RERUN_CALS:
        for (i = 0; i < AR9300_MAX_CHAINS; i++) {
            if (rxchainmask & (1 << i)) {
                num_chains++;
            }
        }
        for (i = 0; i < num_chains; i++) {
            if (AR_SREV_POSEIDON(ah) || AR_SREV_APHRODITE(ah)) {
                HALASSERT(num_chains == 0x1);
            }
            if (AR9300_IS_CHAIN_RX_IQCAL_INVALID(ah, offset_array[i])) {
                rx_iqcal_invalid = 1;
            }
        }
        if (AR9300_IS_RX_IQCAL_DISABLED(ah)) {
            rx_iqcal_invalid = 1;
        }

        return rx_iqcal_invalid;
    default:
        HALASSERT(0);
    }
    return 0;
}
#endif

#if ATH_TRAFFIC_FAST_RECOVER
#define PLL3              0x16188
#define PLL3_DO_MEAS_MASK 0x40000000
#define PLL4              0x1618c
#define PLL4_MEAS_DONE    0x8
#define SQSUM_DVC_MASK    0x007ffff8
unsigned long
ar9300_get_pll3_sqsum_dvc(struct ath_hal *ah)
{
    if (AR_SREV_HORNET(ah) || AR_SREV_POSEIDON(ah) || AR_SREV_WASP(ah) || AR_SREV_SCORPION(ah)) {
        OS_REG_WRITE(ah, PLL3, (OS_REG_READ(ah, PLL3) & ~(PLL3_DO_MEAS_MASK)));
        OS_DELAY(100);
        OS_REG_WRITE(ah, PLL3, (OS_REG_READ(ah, PLL3) | PLL3_DO_MEAS_MASK));

        while ( (OS_REG_READ(ah, PLL4) & PLL4_MEAS_DONE) == 0) {
            OS_DELAY(100);
        }

        return (( OS_REG_READ(ah, PLL3) & SQSUM_DVC_MASK ) >> 3);
    } else {
        HALDEBUG(ah, HAL_DEBUG_UNMASKABLE,
                 "%s: unable to get pll3_sqsum_dvc\n",
                 __func__);
        return 0;
    }
}
#endif


#define RX_GAIN_TABLE_LENGTH	128
// this will be called if rfGainCAP is enabled and rfGainCAP setting is changed,
// or rxGainTable setting is changed
HAL_BOOL ar9300_rf_gain_cap_apply(struct ath_hal *ah, int is_2GHz)
{
	int i, done = 0, i_rx_gain = 32;
    u_int32_t rf_gain_cap;
    u_int32_t rx_gain_value, a_Byte, rx_gain_value_caped;
	static u_int32_t  rx_gain_table[RX_GAIN_TABLE_LENGTH * 2][2];
    ar9300_eeprom_t *eep = &AH9300(ah)->ah_eeprom;
    struct ath_hal_9300 *ahp = AH9300(ah);

    if ( !((eep->base_eep_header.misc_configuration & 0x80) >> 7) )
        return AH_FALSE;
		  
    if (is_2GHz)
    {
        rf_gain_cap = (u_int32_t) eep->modal_header_2g.rf_gain_cap;    
    }
    else
    {
        rf_gain_cap = (u_int32_t) eep->modal_header_5g.rf_gain_cap;       
	}

	if (rf_gain_cap == 0)
        return AH_FALSE;

	for (i = 0; i< RX_GAIN_TABLE_LENGTH * 2; i++)
	{
        if (AR_SREV_AR9580(ah)) 
        {
            // BB_rx_ocgain2
            i_rx_gain = 128 + 32;
            switch (ar9300_rx_gain_index_get(ah))
            {
            case 0:
                rx_gain_table[i][0] = 
					ar9300_common_rx_gain_table_ar9580_1p0[i][0];
                rx_gain_table[i][1] = 
					ar9300_common_rx_gain_table_ar9580_1p0[i][1];
                break;
            case 1:
                rx_gain_table[i][0] = 
					ar9300_common_wo_xlna_rx_gain_table_ar9580_1p0[i][0];
                rx_gain_table[i][1] = 
					ar9300_common_wo_xlna_rx_gain_table_ar9580_1p0[i][1];
                break;
			}
        } 
        else if (AR_SREV_OSPREY_22(ah)) 
        { 
            i_rx_gain = 128 + 32;
            switch (ar9300_rx_gain_index_get(ah))
            {
            case 0:
                rx_gain_table[i][0] = ar9300_common_rx_gain_table_osprey_2p2[i][0];
                rx_gain_table[i][1] = ar9300_common_rx_gain_table_osprey_2p2[i][1];
                break;
            case 1:
                rx_gain_table[i][0] = 
					ar9300Common_wo_xlna_rx_gain_table_osprey_2p2[i][0];
                rx_gain_table[i][1] = 
					ar9300Common_wo_xlna_rx_gain_table_osprey_2p2[i][1];
                break;
			}
        }
        else
        {
            return AH_FALSE;
        }
    }
    
    while (1) 
	{
        rx_gain_value = rx_gain_table[i_rx_gain][1];
        rx_gain_value_caped = rx_gain_value;
        a_Byte = rx_gain_value & (0x000000FF);
        if (a_Byte>rf_gain_cap) 
        {
        	rx_gain_value_caped = (rx_gain_value_caped & 
				(0xFFFFFF00)) + rf_gain_cap;
        }
        a_Byte = rx_gain_value & (0x0000FF00);
        if ( a_Byte > ( rf_gain_cap << 8 ) ) 
        {
        	rx_gain_value_caped = (rx_gain_value_caped & 
				(0xFFFF00FF)) + (rf_gain_cap<<8);
        }
        a_Byte = rx_gain_value & (0x00FF0000);
        if ( a_Byte > ( rf_gain_cap << 16 ) ) 
        {
        	rx_gain_value_caped = (rx_gain_value_caped & 
				(0xFF00FFFF)) + (rf_gain_cap<<16);
        }
        a_Byte = rx_gain_value & (0xFF000000);
        if ( a_Byte > ( rf_gain_cap << 24 ) ) 
        {
        	rx_gain_value_caped = (rx_gain_value_caped & 
				(0x00FFFFFF)) + (rf_gain_cap<<24);
        } 
        else 
        {
            done = 1;
        }
		HALDEBUG(ah, HAL_DEBUG_RESET,
			"%s: rx_gain_address: %x, rx_gain_value: %x	rx_gain_value_caped: %x\n",
			__func__, rx_gain_table[i_rx_gain][0], rx_gain_value, rx_gain_value_caped);
        if (rx_gain_value_caped != rx_gain_value)
		{
            rx_gain_table[i_rx_gain][1] = rx_gain_value_caped;
		}
        if (done == 1)
            break;
        i_rx_gain ++;
	}
    INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain, rx_gain_table, ARRAY_LENGTH(rx_gain_table), 2);
    return AH_TRUE;
}


void ar9300_rx_gain_table_apply(struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);
//struct ath_hal_private *ahpriv = AH_PRIVATE(ah);
    u_int32_t xlan_gpio_cfg;
    u_int8_t  i;

    if (AR_SREV_OSPREY(ah) || AR_SREV_AR9580(ah))
    {
		// this will be called if rxGainTable setting is changed
        if (ar9300_rf_gain_cap_apply(ah, 1))
            return;
	}

    switch (ar9300_rx_gain_index_get(ah))
    {
    case 2:
        if (AR_SREV_JUPITER_10(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain, 
                ar9300_common_mixed_rx_gain_table_jupiter_1p0,
                ARRAY_LENGTH(ar9300_common_mixed_rx_gain_table_jupiter_1p0), 2);
            break;
        }
        else if (AR_SREV_JUPITER_20(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain, 
                ar9300Common_mixed_rx_gain_table_jupiter_2p0,
                ARRAY_LENGTH(ar9300Common_mixed_rx_gain_table_jupiter_2p0), 2);
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain_bb_core,
                ar9462_2p0_baseband_core_mix_rxgain,
                ARRAY_LENGTH(ar9462_2p0_baseband_core_mix_rxgain), 2);
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain_bb_postamble,
                ar9462_2p0_baseband_postamble_mix_rxgain,
                ARRAY_LENGTH(ar9462_2p0_baseband_postamble_mix_rxgain), 2);
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain_xlna,
                ar9462_2p0_baseband_postamble_5g_xlna,
                ARRAY_LENGTH(ar9462_2p0_baseband_postamble_5g_xlna), 2);
            break;
        }
        else if (AR_SREV_JUPITER_21(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain, 
                ar9462_2p1_common_mixed_rx_gain,
                ARRAY_LENGTH(ar9462_2p1_common_mixed_rx_gain), 2);
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain_bb_core,
                ar9462_2p1_baseband_core_mix_rxgain,
                ARRAY_LENGTH(ar9462_2p1_baseband_core_mix_rxgain), 2);
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain_bb_postamble,
                ar9462_2p1_baseband_postamble_mix_rxgain,
                ARRAY_LENGTH(ar9462_2p1_baseband_postamble_mix_rxgain), 2);
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain_xlna,
                ar9462_2p1_baseband_postamble_5g_xlna,
                ARRAY_LENGTH(ar9462_2p1_baseband_postamble_5g_xlna), 2);

            break;
        }
    case 3:
        if (AR_SREV_JUPITER_21(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain,
                ar9462_2p1_common_5g_xlna_only_rxgain,
                ARRAY_LENGTH(ar9462_2p1_common_5g_xlna_only_rxgain), 2);
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain_xlna,
                ar9462_2p1_baseband_postamble_5g_xlna,
                ARRAY_LENGTH(ar9462_2p1_baseband_postamble_5g_xlna), 2);
        } else if (AR_SREV_JUPITER_20(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain,
                ar9462_2p0_common_5g_xlna_only_rxgain,
                ARRAY_LENGTH(ar9462_2p0_common_5g_xlna_only_rxgain), 2);
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain_xlna,
                ar9462_2p0_baseband_postamble_5g_xlna,
                ARRAY_LENGTH(ar9462_2p0_baseband_postamble_5g_xlna), 2);
        }
        break;
    case 0:
    default:
        if (AR_SREV_HORNET_12(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain,
                ar9331_common_rx_gain_hornet1_2, 
                ARRAY_LENGTH(ar9331_common_rx_gain_hornet1_2), 2);
        } else if (AR_SREV_HORNET_11(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain,
                ar9331_common_rx_gain_hornet1_1, 
                ARRAY_LENGTH(ar9331_common_rx_gain_hornet1_1), 2);
        } else if (AR_SREV_POSEIDON_11_OR_LATER(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain,
                ar9485_common_wo_xlna_rx_gain_poseidon1_1,
                ARRAY_LENGTH(ar9485_common_wo_xlna_rx_gain_poseidon1_1), 2);
            /* XXX FreeBSD: this needs to be revisited!! */
            xlan_gpio_cfg = ah->ah_config.ath_hal_ext_lna_ctl_gpio;
            if (xlan_gpio_cfg) {
                for (i = 0; i < 32; i++) {
                    if (xlan_gpio_cfg & (1 << i)) {
                        /*
                         * XXX FreeBSD: definitely make sure this
                         * results in the correct value being written
                         * to the hardware, or weird crap is very likely
                         * to occur!
                         */
                        ath_hal_gpioCfgOutput(ah, i,
                            HAL_GPIO_OUTPUT_MUX_PCIE_ATTENTION_LED);
                    }
                }
            }

        } else if (AR_SREV_POSEIDON(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain,
                ar9485Common_wo_xlna_rx_gain_poseidon1_0,
                ARRAY_LENGTH(ar9485Common_wo_xlna_rx_gain_poseidon1_0), 2);
        } else if (AR_SREV_JUPITER_10(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain, 
                ar9300_common_rx_gain_table_jupiter_1p0,
                ARRAY_LENGTH(ar9300_common_rx_gain_table_jupiter_1p0), 2);
        } else if (AR_SREV_JUPITER_20(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain, 
                ar9300Common_rx_gain_table_jupiter_2p0,
                ARRAY_LENGTH(ar9300Common_rx_gain_table_jupiter_2p0), 2);
        } else if (AR_SREV_JUPITER_21(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain,
                ar9462_2p1_common_rx_gain,
                ARRAY_LENGTH(ar9462_2p1_common_rx_gain), 2);
        } else if (AR_SREV_AR9580(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain,
                ar9300_common_rx_gain_table_ar9580_1p0,
                ARRAY_LENGTH(ar9300_common_rx_gain_table_ar9580_1p0), 2);
        } else if (AR_SREV_WASP(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain,
                ar9340Common_rx_gain_table_wasp_1p0,
                ARRAY_LENGTH(ar9340Common_rx_gain_table_wasp_1p0), 2);
        } else if (AR_SREV_SCORPION(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain,
                ar955xCommon_rx_gain_table_scorpion_1p0,
                ARRAY_LENGTH(ar955xCommon_rx_gain_table_scorpion_1p0), 2);
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain_bounds,
                ar955xCommon_rx_gain_bounds_scorpion_1p0,
                ARRAY_LENGTH(ar955xCommon_rx_gain_bounds_scorpion_1p0), 5);
        } else if (AR_SREV_HONEYBEE(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain,
                qca953xCommon_rx_gain_table_honeybee_1p0,
                ARRAY_LENGTH(qca953xCommon_rx_gain_table_honeybee_1p0), 2);
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain_bounds,
                qca953xCommon_rx_gain_bounds_honeybee_1p0,
                ARRAY_LENGTH(qca953xCommon_rx_gain_bounds_honeybee_1p0), 5);
        } else {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain,
                ar9300_common_rx_gain_table_osprey_2p2,
                ARRAY_LENGTH(ar9300_common_rx_gain_table_osprey_2p2), 2);
        }
        break;
    case 1:
        if (AR_SREV_HORNET_12(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain,
                ar9331_common_wo_xlna_rx_gain_hornet1_2,
                ARRAY_LENGTH(ar9331_common_wo_xlna_rx_gain_hornet1_2), 2);
        } else if (AR_SREV_HORNET_11(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain,
                ar9331_common_wo_xlna_rx_gain_hornet1_1,
                ARRAY_LENGTH(ar9331_common_wo_xlna_rx_gain_hornet1_1), 2);
        } else if (AR_SREV_POSEIDON_11_OR_LATER(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain,
                ar9485_common_wo_xlna_rx_gain_poseidon1_1,
                ARRAY_LENGTH(ar9485_common_wo_xlna_rx_gain_poseidon1_1), 2);
        } else if (AR_SREV_POSEIDON(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain,
                ar9485Common_wo_xlna_rx_gain_poseidon1_0,
                ARRAY_LENGTH(ar9485Common_wo_xlna_rx_gain_poseidon1_0), 2);
        } else if (AR_SREV_JUPITER_10(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain, 
                ar9300_common_wo_xlna_rx_gain_table_jupiter_1p0,
                ARRAY_LENGTH(ar9300_common_wo_xlna_rx_gain_table_jupiter_1p0),
                2);
        } else if (AR_SREV_JUPITER_20(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain, 
                ar9300Common_wo_xlna_rx_gain_table_jupiter_2p0,
                ARRAY_LENGTH(ar9300Common_wo_xlna_rx_gain_table_jupiter_2p0),
                2);
        } else if (AR_SREV_JUPITER_21(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain,
                ar9462_2p1_common_wo_xlna_rx_gain,
                ARRAY_LENGTH(ar9462_2p1_common_wo_xlna_rx_gain),
                2);
        } else if (AR_SREV_APHRODITE(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain, 
                ar956XCommon_wo_xlna_rx_gain_table_aphrodite_1p0,
                ARRAY_LENGTH(ar956XCommon_wo_xlna_rx_gain_table_aphrodite_1p0),
                2);
        } else if (AR_SREV_AR9580(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain,
                ar9300_common_wo_xlna_rx_gain_table_ar9580_1p0,
                ARRAY_LENGTH(ar9300_common_wo_xlna_rx_gain_table_ar9580_1p0), 2);
        } else if (AR_SREV_WASP(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain,
                ar9340Common_wo_xlna_rx_gain_table_wasp_1p0,
                ARRAY_LENGTH(ar9340Common_wo_xlna_rx_gain_table_wasp_1p0), 2);
        } else if (AR_SREV_SCORPION(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain,
                ar955xCommon_wo_xlna_rx_gain_table_scorpion_1p0,
                ARRAY_LENGTH(ar955xCommon_wo_xlna_rx_gain_table_scorpion_1p0), 2);
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain_bounds,
                ar955xCommon_wo_xlna_rx_gain_bounds_scorpion_1p0,
                ARRAY_LENGTH(ar955xCommon_wo_xlna_rx_gain_bounds_scorpion_1p0), 5);
        } else if (AR_SREV_HONEYBEE(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain,
                qca953xCommon_wo_xlna_rx_gain_table_honeybee_1p0,
                ARRAY_LENGTH(qca953xCommon_wo_xlna_rx_gain_table_honeybee_1p0), 2);
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain_bounds,
                qca953xCommon_wo_xlna_rx_gain_bounds_honeybee_1p0,
                ARRAY_LENGTH(qca953xCommon_wo_xlna_rx_gain_bounds_honeybee_1p0), 5);
        } else {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_rxgain,
                ar9300Common_wo_xlna_rx_gain_table_osprey_2p2,
                ARRAY_LENGTH(ar9300Common_wo_xlna_rx_gain_table_osprey_2p2), 2);
        }
        break;
    }
}

void ar9300_tx_gain_table_apply(struct ath_hal *ah)
{
    struct ath_hal_9300 *ahp = AH9300(ah);

    switch (ar9300_tx_gain_index_get(ah))
    {
    case 0:
    default:
        if (AR_SREV_HORNET_12(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9331_modes_lowest_ob_db_tx_gain_hornet1_2, 
                ARRAY_LENGTH(ar9331_modes_lowest_ob_db_tx_gain_hornet1_2), 5);
        } else if (AR_SREV_HORNET_11(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9331_modes_lowest_ob_db_tx_gain_hornet1_1, 
                ARRAY_LENGTH(ar9331_modes_lowest_ob_db_tx_gain_hornet1_1), 5);
        } else if (AR_SREV_POSEIDON_11_OR_LATER(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9485_modes_lowest_ob_db_tx_gain_poseidon1_1, 
                ARRAY_LENGTH(ar9485_modes_lowest_ob_db_tx_gain_poseidon1_1), 5);
        } else if (AR_SREV_POSEIDON(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9485Modes_lowest_ob_db_tx_gain_poseidon1_0, 
                ARRAY_LENGTH(ar9485Modes_lowest_ob_db_tx_gain_poseidon1_0), 5);
        } else if (AR_SREV_AR9580(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9300Modes_lowest_ob_db_tx_gain_table_ar9580_1p0,
                ARRAY_LENGTH(ar9300Modes_lowest_ob_db_tx_gain_table_ar9580_1p0),
                5);
        } else if (AR_SREV_WASP(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9340Modes_lowest_ob_db_tx_gain_table_wasp_1p0,
                ARRAY_LENGTH(ar9340Modes_lowest_ob_db_tx_gain_table_wasp_1p0),
                5);
        } else if (AR_SREV_SCORPION(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar955xModes_xpa_tx_gain_table_scorpion_1p0,
                ARRAY_LENGTH(ar955xModes_xpa_tx_gain_table_scorpion_1p0),
                9);
        } else if (AR_SREV_JUPITER_10(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9300_modes_low_ob_db_tx_gain_table_jupiter_1p0,
                ARRAY_LENGTH(ar9300_modes_low_ob_db_tx_gain_table_jupiter_1p0),
                5);
         } else if (AR_SREV_JUPITER_20(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9300Modes_low_ob_db_tx_gain_table_jupiter_2p0,
                ARRAY_LENGTH(ar9300Modes_low_ob_db_tx_gain_table_jupiter_2p0),
                5);
       } else if (AR_SREV_JUPITER_21(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9462_2p1_modes_low_ob_db_tx_gain,
                ARRAY_LENGTH(ar9462_2p1_modes_low_ob_db_tx_gain),
                5);
        } else if (AR_SREV_HONEYBEE(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
           	qca953xModes_xpa_tx_gain_table_honeybee_1p0,
                ARRAY_LENGTH(qca953xModes_xpa_tx_gain_table_honeybee_1p0),
                2);
        } else if (AR_SREV_APHRODITE(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar956XModes_low_ob_db_tx_gain_table_aphrodite_1p0,
                ARRAY_LENGTH(ar956XModes_low_ob_db_tx_gain_table_aphrodite_1p0),
                5);
        } else {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9300_modes_lowest_ob_db_tx_gain_table_osprey_2p2,
                ARRAY_LENGTH(ar9300_modes_lowest_ob_db_tx_gain_table_osprey_2p2),
                5);
        }
        break;
    case 1:
        if (AR_SREV_HORNET_12(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9331_modes_high_ob_db_tx_gain_hornet1_2, 
                ARRAY_LENGTH(ar9331_modes_high_ob_db_tx_gain_hornet1_2), 5);
        } else if (AR_SREV_HORNET_11(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9331_modes_high_ob_db_tx_gain_hornet1_1, 
                ARRAY_LENGTH(ar9331_modes_high_ob_db_tx_gain_hornet1_1), 5);
        } else if (AR_SREV_POSEIDON_11_OR_LATER(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9485_modes_high_ob_db_tx_gain_poseidon1_1, 
                ARRAY_LENGTH(ar9485_modes_high_ob_db_tx_gain_poseidon1_1), 5);
        } else if (AR_SREV_POSEIDON(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9485Modes_high_ob_db_tx_gain_poseidon1_0, 
                ARRAY_LENGTH(ar9485Modes_high_ob_db_tx_gain_poseidon1_0), 5);
        } else if (AR_SREV_AR9580(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9300Modes_high_ob_db_tx_gain_table_ar9580_1p0,
                ARRAY_LENGTH(ar9300Modes_high_ob_db_tx_gain_table_ar9580_1p0),
                5);
        } else if (AR_SREV_WASP(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9340Modes_high_ob_db_tx_gain_table_wasp_1p0,
                ARRAY_LENGTH(ar9340Modes_high_ob_db_tx_gain_table_wasp_1p0), 5);
        } else if (AR_SREV_SCORPION(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar955xModes_no_xpa_tx_gain_table_scorpion_1p0,
                ARRAY_LENGTH(ar955xModes_no_xpa_tx_gain_table_scorpion_1p0), 9);
        } else if (AR_SREV_JUPITER_10(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9300_modes_high_ob_db_tx_gain_table_jupiter_1p0,
                ARRAY_LENGTH(
                ar9300_modes_high_ob_db_tx_gain_table_jupiter_1p0), 5);
        } else if (AR_SREV_JUPITER_20(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9300Modes_high_ob_db_tx_gain_table_jupiter_2p0,
                ARRAY_LENGTH(
                ar9300Modes_high_ob_db_tx_gain_table_jupiter_2p0), 5);
        } else if (AR_SREV_JUPITER_21(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9462_2p1_modes_high_ob_db_tx_gain,
                ARRAY_LENGTH(
                ar9462_2p1_modes_high_ob_db_tx_gain), 5);
        } else if (AR_SREV_APHRODITE(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar956XModes_high_ob_db_tx_gain_table_aphrodite_1p0,
                ARRAY_LENGTH(
                ar956XModes_high_ob_db_tx_gain_table_aphrodite_1p0), 5);
        } else if (AR_SREV_HONEYBEE(ah)) {
            if (AR_SREV_HONEYBEE_11(ah)) {
                INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                    qca953xModes_no_xpa_tx_gain_table_honeybee_1p1,
                    ARRAY_LENGTH(qca953xModes_no_xpa_tx_gain_table_honeybee_1p1), 2);
            } else {
                INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                    qca953xModes_no_xpa_tx_gain_table_honeybee_1p0,
                    ARRAY_LENGTH(qca953xModes_no_xpa_tx_gain_table_honeybee_1p0), 2);
            }
        } else {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9300Modes_high_ob_db_tx_gain_table_osprey_2p2,
                ARRAY_LENGTH(ar9300Modes_high_ob_db_tx_gain_table_osprey_2p2),
                5);
        }
        break;
    case 2:
        if (AR_SREV_HORNET_12(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9331_modes_low_ob_db_tx_gain_hornet1_2, 
                ARRAY_LENGTH(ar9331_modes_low_ob_db_tx_gain_hornet1_2), 5);
        } else if (AR_SREV_HORNET_11(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9331_modes_low_ob_db_tx_gain_hornet1_1, 
                ARRAY_LENGTH(ar9331_modes_low_ob_db_tx_gain_hornet1_1), 5);
        } else if (AR_SREV_POSEIDON_11_OR_LATER(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9485_modes_low_ob_db_tx_gain_poseidon1_1, 
                ARRAY_LENGTH(ar9485_modes_low_ob_db_tx_gain_poseidon1_1), 5);
        } else if (AR_SREV_POSEIDON(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9485Modes_low_ob_db_tx_gain_poseidon1_0, 
                ARRAY_LENGTH(ar9485Modes_low_ob_db_tx_gain_poseidon1_0), 5);
        } else if (AR_SREV_AR9580(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9300Modes_low_ob_db_tx_gain_table_ar9580_1p0,
                ARRAY_LENGTH(ar9300Modes_low_ob_db_tx_gain_table_ar9580_1p0),
                5);
        } else if (AR_SREV_WASP(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9340Modes_low_ob_db_tx_gain_table_wasp_1p0,
                ARRAY_LENGTH(ar9340Modes_low_ob_db_tx_gain_table_wasp_1p0), 5);
        } else if (AR_SREV_APHRODITE(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar956XModes_low_ob_db_tx_gain_table_aphrodite_1p0, 
                ARRAY_LENGTH(ar956XModes_low_ob_db_tx_gain_table_aphrodite_1p0), 5);
        } else {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9300Modes_low_ob_db_tx_gain_table_osprey_2p2,
                ARRAY_LENGTH(ar9300Modes_low_ob_db_tx_gain_table_osprey_2p2),
                5);
        }
        break;
    case 3:
        if (AR_SREV_HORNET_12(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9331_modes_high_power_tx_gain_hornet1_2, 
                ARRAY_LENGTH(ar9331_modes_high_power_tx_gain_hornet1_2), 5);
        } else if (AR_SREV_HORNET_11(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9331_modes_high_power_tx_gain_hornet1_1, 
                ARRAY_LENGTH(ar9331_modes_high_power_tx_gain_hornet1_1), 5);
        } else if (AR_SREV_POSEIDON_11_OR_LATER(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9485_modes_high_power_tx_gain_poseidon1_1, 
                ARRAY_LENGTH(ar9485_modes_high_power_tx_gain_poseidon1_1), 5);
        } else if (AR_SREV_POSEIDON(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9485Modes_high_power_tx_gain_poseidon1_0, 
                ARRAY_LENGTH(ar9485Modes_high_power_tx_gain_poseidon1_0), 5);
        } else if (AR_SREV_AR9580(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9300Modes_high_power_tx_gain_table_ar9580_1p0,
                ARRAY_LENGTH(ar9300Modes_high_power_tx_gain_table_ar9580_1p0),
                5);
        } else if (AR_SREV_WASP(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9340Modes_high_power_tx_gain_table_wasp_1p0,
                ARRAY_LENGTH(ar9340Modes_high_power_tx_gain_table_wasp_1p0),
                5);
        } else if (AR_SREV_APHRODITE(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar956XModes_high_power_tx_gain_table_aphrodite_1p0, 
                ARRAY_LENGTH(ar956XModes_high_power_tx_gain_table_aphrodite_1p0), 5);
        } else {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9300Modes_high_power_tx_gain_table_osprey_2p2,
                ARRAY_LENGTH(ar9300Modes_high_power_tx_gain_table_osprey_2p2),
                5);
        }
        break;
    case 4:
        if (AR_SREV_WASP(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9340Modes_mixed_ob_db_tx_gain_table_wasp_1p0,
                ARRAY_LENGTH(ar9340Modes_mixed_ob_db_tx_gain_table_wasp_1p0),
                5);
        } else if (AR_SREV_AR9580(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9300_modes_mixed_ob_db_tx_gain_table_ar9580_1p0,
                ARRAY_LENGTH(ar9300_modes_mixed_ob_db_tx_gain_table_ar9580_1p0),
                5);
        } else {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain, 
		ar9300Modes_mixed_ob_db_tx_gain_table_osprey_2p2,
                ARRAY_LENGTH(ar9300Modes_mixed_ob_db_tx_gain_table_osprey_2p2),
		 5);
        }
        break;
    case 5:
        /* HW Green TX */
        if (AR_SREV_POSEIDON(ah)) {
            if (AR_SREV_POSEIDON_11_OR_LATER(ah)) {
                INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain, 
                    ar9485_modes_green_ob_db_tx_gain_poseidon1_1,
                    sizeof(ar9485_modes_green_ob_db_tx_gain_poseidon1_1) /
                    sizeof(ar9485_modes_green_ob_db_tx_gain_poseidon1_1[0]), 5);
            } else {
                INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain, 
                    ar9485_modes_green_ob_db_tx_gain_poseidon1_0,
                    sizeof(ar9485_modes_green_ob_db_tx_gain_poseidon1_0) /
                    sizeof(ar9485_modes_green_ob_db_tx_gain_poseidon1_0[0]), 5);
            }
            ahp->ah_hw_green_tx_enable = 1;
        }
        else if (AR_SREV_WASP(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain, 
            ar9340_modes_ub124_tx_gain_table_wasp_1p0,
            sizeof(ar9340_modes_ub124_tx_gain_table_wasp_1p0) /
            sizeof(ar9340_modes_ub124_tx_gain_table_wasp_1p0[0]), 5);
        }
        else if (AR_SREV_AR9580(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9300_modes_type5_tx_gain_table_ar9580_1p0,
                ARRAY_LENGTH( ar9300_modes_type5_tx_gain_table_ar9580_1p0),
                5);
        } 
        else if (AR_SREV_OSPREY_22(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9300_modes_number_5_tx_gain_table_osprey_2p2,
                ARRAY_LENGTH( ar9300_modes_number_5_tx_gain_table_osprey_2p2),
                5);
        }
        break;
	case 6:
        if (AR_SREV_WASP(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
            ar9340_modes_low_ob_db_and_spur_tx_gain_table_wasp_1p0,
            sizeof(ar9340_modes_low_ob_db_and_spur_tx_gain_table_wasp_1p0) /
            sizeof(ar9340_modes_low_ob_db_and_spur_tx_gain_table_wasp_1p0[0]), 5);
        }
        /* HW Green TX */
        else if (AR_SREV_POSEIDON(ah)) {
            if (AR_SREV_POSEIDON_11_OR_LATER(ah)) {
                INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain, 
                ar9485_modes_green_spur_ob_db_tx_gain_poseidon1_1,
                sizeof(ar9485_modes_green_spur_ob_db_tx_gain_poseidon1_1) /
                sizeof(ar9485_modes_green_spur_ob_db_tx_gain_poseidon1_1[0]),
                5);
            }
            ahp->ah_hw_green_tx_enable = 1;
	}
        else if (AR_SREV_AR9580(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain,
                ar9300_modes_type6_tx_gain_table_ar9580_1p0,
                ARRAY_LENGTH( ar9300_modes_type6_tx_gain_table_ar9580_1p0),
                5);
        }
        break;
	case 7:
		if (AR_SREV_WASP(ah)) {
            INIT_INI_ARRAY(&ahp->ah_ini_modes_txgain, 
            ar9340Modes_cus227_tx_gain_table_wasp_1p0,
            sizeof(ar9340Modes_cus227_tx_gain_table_wasp_1p0) /
            sizeof(ar9340Modes_cus227_tx_gain_table_wasp_1p0[0]), 5);
		}
		break;
    }
}

#if ATH_ANT_DIV_COMB
void
ar9300_ant_div_comb_get_config(struct ath_hal *ah,
    HAL_ANT_COMB_CONFIG *div_comb_conf)
{
    u_int32_t reg_val = OS_REG_READ(ah, AR_PHY_MC_GAIN_CTRL);
    div_comb_conf->main_lna_conf = 
        MULTICHAIN_GAIN_CTRL__ANT_DIV_MAIN_LNACONF__READ(reg_val);
    div_comb_conf->alt_lna_conf = 
        MULTICHAIN_GAIN_CTRL__ANT_DIV_ALT_LNACONF__READ(reg_val);
    div_comb_conf->fast_div_bias = 
        MULTICHAIN_GAIN_CTRL__ANT_FAST_DIV_BIAS__READ(reg_val); 
    if (AR_SREV_HORNET_11(ah)) {
        div_comb_conf->antdiv_configgroup = HAL_ANTDIV_CONFIG_GROUP_1;
    } else if (AR_SREV_POSEIDON_11_OR_LATER(ah)) {
        div_comb_conf->antdiv_configgroup = HAL_ANTDIV_CONFIG_GROUP_2;
    } else {
        div_comb_conf->antdiv_configgroup = DEFAULT_ANTDIV_CONFIG_GROUP;
    }

    /*
     * XXX TODO: allow the HAL to override the rssithres and fast_div_bias
     * values (eg CUS198.)
     */
}

void
ar9300_ant_div_comb_set_config(struct ath_hal *ah,
    HAL_ANT_COMB_CONFIG *div_comb_conf)
{
    u_int32_t reg_val;
    struct ath_hal_9300 *ahp = AH9300(ah);

    /* DO NOTHING when set to fixed antenna for manufacturing purpose */
    if (AR_SREV_POSEIDON(ah) && ( ahp->ah_diversity_control == HAL_ANT_FIXED_A 
         || ahp->ah_diversity_control == HAL_ANT_FIXED_B)) {
        return;
    }
    reg_val = OS_REG_READ(ah, AR_PHY_MC_GAIN_CTRL);
    reg_val &= ~(MULTICHAIN_GAIN_CTRL__ANT_DIV_MAIN_LNACONF__MASK    | 
                MULTICHAIN_GAIN_CTRL__ANT_DIV_ALT_LNACONF__MASK     |
                MULTICHAIN_GAIN_CTRL__ANT_FAST_DIV_BIAS__MASK       |
                MULTICHAIN_GAIN_CTRL__ANT_DIV_MAIN_GAINTB__MASK     |
                MULTICHAIN_GAIN_CTRL__ANT_DIV_ALT_GAINTB__MASK );
    reg_val |=
        MULTICHAIN_GAIN_CTRL__ANT_DIV_MAIN_GAINTB__WRITE(
        div_comb_conf->main_gaintb);
    reg_val |=
        MULTICHAIN_GAIN_CTRL__ANT_DIV_ALT_GAINTB__WRITE(
        div_comb_conf->alt_gaintb);
    reg_val |= 
        MULTICHAIN_GAIN_CTRL__ANT_DIV_MAIN_LNACONF__WRITE(
        div_comb_conf->main_lna_conf);
    reg_val |= 
        MULTICHAIN_GAIN_CTRL__ANT_DIV_ALT_LNACONF__WRITE(
        div_comb_conf->alt_lna_conf);
    reg_val |= 
        MULTICHAIN_GAIN_CTRL__ANT_FAST_DIV_BIAS__WRITE(
        div_comb_conf->fast_div_bias);
    OS_REG_WRITE(ah, AR_PHY_MC_GAIN_CTRL, reg_val);

}
#endif /* ATH_ANT_DIV_COMB */

static void 
ar9300_init_hostif_offsets(struct ath_hal *ah)
{
    AR_HOSTIF_REG(ah, AR_RC) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_RESET_CONTROL);
    AR_HOSTIF_REG(ah, AR_WA) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_WORK_AROUND);                   
    AR_HOSTIF_REG(ah, AR_PM_STATE) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_PM_STATE);
    AR_HOSTIF_REG(ah, AR_H_INFOL) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_CXPL_DEBUG_INFOL);
    AR_HOSTIF_REG(ah, AR_H_INFOH) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_CXPL_DEBUG_INFOH);
    AR_HOSTIF_REG(ah, AR_PCIE_PM_CTRL) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_PM_CTRL);
    AR_HOSTIF_REG(ah, AR_HOST_TIMEOUT) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_TIMEOUT);
    AR_HOSTIF_REG(ah, AR_EEPROM) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_EEPROM_CTRL);
    AR_HOSTIF_REG(ah, AR_SREV) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_SREV);
    AR_HOSTIF_REG(ah, AR_INTR_SYNC_CAUSE) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_INTR_SYNC_CAUSE);
    AR_HOSTIF_REG(ah, AR_INTR_SYNC_CAUSE_CLR) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_INTR_SYNC_CAUSE);
    AR_HOSTIF_REG(ah, AR_INTR_SYNC_ENABLE) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_INTR_SYNC_ENABLE);
    AR_HOSTIF_REG(ah, AR_INTR_ASYNC_MASK) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_INTR_ASYNC_MASK);
    AR_HOSTIF_REG(ah, AR_INTR_SYNC_MASK) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_INTR_SYNC_MASK);
    AR_HOSTIF_REG(ah, AR_INTR_ASYNC_CAUSE_CLR) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_INTR_ASYNC_CAUSE);
    AR_HOSTIF_REG(ah, AR_INTR_ASYNC_CAUSE) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_INTR_ASYNC_CAUSE);
    AR_HOSTIF_REG(ah, AR_INTR_ASYNC_ENABLE) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_INTR_ASYNC_ENABLE);
    AR_HOSTIF_REG(ah, AR_PCIE_SERDES) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_PCIE_PHY_RW);
    AR_HOSTIF_REG(ah, AR_PCIE_SERDES2) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_PCIE_PHY_LOAD);
    AR_HOSTIF_REG(ah, AR_GPIO_OUT) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_GPIO_OUT);
    AR_HOSTIF_REG(ah, AR_GPIO_IN) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_GPIO_IN);
    AR_HOSTIF_REG(ah, AR_GPIO_OE_OUT) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_GPIO_OE);
    AR_HOSTIF_REG(ah, AR_GPIO_OE1_OUT) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_GPIO_OE1);
    AR_HOSTIF_REG(ah, AR_GPIO_INTR_POL) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_GPIO_INTR_POLAR);
    AR_HOSTIF_REG(ah, AR_GPIO_INPUT_EN_VAL) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_GPIO_INPUT_VALUE);
    AR_HOSTIF_REG(ah, AR_GPIO_INPUT_MUX1) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_GPIO_INPUT_MUX1);
    AR_HOSTIF_REG(ah, AR_GPIO_INPUT_MUX2) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_GPIO_INPUT_MUX2);
    AR_HOSTIF_REG(ah, AR_GPIO_OUTPUT_MUX1) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_GPIO_OUTPUT_MUX1);
    AR_HOSTIF_REG(ah, AR_GPIO_OUTPUT_MUX2) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_GPIO_OUTPUT_MUX2);
    AR_HOSTIF_REG(ah, AR_GPIO_OUTPUT_MUX3) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_GPIO_OUTPUT_MUX3);
    AR_HOSTIF_REG(ah, AR_INPUT_STATE) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_GPIO_INPUT_STATE);
    AR_HOSTIF_REG(ah, AR_SPARE) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_SPARE);
    AR_HOSTIF_REG(ah, AR_PCIE_CORE_RESET_EN) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_PCIE_CORE_RST_EN);
    AR_HOSTIF_REG(ah, AR_CLKRUN) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_CLKRUN);
    AR_HOSTIF_REG(ah, AR_EEPROM_STATUS_DATA) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_EEPROM_STS);
    AR_HOSTIF_REG(ah, AR_OBS) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_OBS_CTRL);
    AR_HOSTIF_REG(ah, AR_RFSILENT) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_RFSILENT);
    AR_HOSTIF_REG(ah, AR_GPIO_PDPU) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_GPIO_PDPU);
    AR_HOSTIF_REG(ah, AR_GPIO_DS) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_GPIO_DS);
    AR_HOSTIF_REG(ah, AR_MISC) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_MISC);
    AR_HOSTIF_REG(ah, AR_PCIE_MSI) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_PCIE_MSI);
#if 0   /* Offsets are not defined in reg_map structure */ 
    AR_HOSTIF_REG(ah, AR_TSF_SNAPSHOT_BT_ACTIVE) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_TSF_SNAPSHOT_BT_ACTIVE);
    AR_HOSTIF_REG(ah, AR_TSF_SNAPSHOT_BT_PRIORITY) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_TSF_SNAPSHOT_BT_PRIORITY);
    AR_HOSTIF_REG(ah, AR_TSF_SNAPSHOT_BT_CNTL) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_MAC_TSF_SNAPSHOT_BT_CNTL);
#endif
    AR_HOSTIF_REG(ah, AR_PCIE_PHY_LATENCY_NFTS_ADJ) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_PCIE_PHY_LATENCY_NFTS_ADJ);
    AR_HOSTIF_REG(ah, AR_TDMA_CCA_CNTL) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_MAC_TDMA_CCA_CNTL);
    AR_HOSTIF_REG(ah, AR_TXAPSYNC) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_MAC_TXAPSYNC);
    AR_HOSTIF_REG(ah, AR_TXSYNC_INIT_SYNC_TMR) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_MAC_TXSYNC_INITIAL_SYNC_TMR);
    AR_HOSTIF_REG(ah, AR_INTR_PRIO_SYNC_CAUSE) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_INTR_PRIORITY_SYNC_CAUSE);
    AR_HOSTIF_REG(ah, AR_INTR_PRIO_SYNC_ENABLE) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_INTR_PRIORITY_SYNC_ENABLE);
    AR_HOSTIF_REG(ah, AR_INTR_PRIO_ASYNC_MASK) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_INTR_PRIORITY_ASYNC_MASK);
    AR_HOSTIF_REG(ah, AR_INTR_PRIO_SYNC_MASK) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_INTR_PRIORITY_SYNC_MASK);
    AR_HOSTIF_REG(ah, AR_INTR_PRIO_ASYNC_CAUSE) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_INTR_PRIORITY_ASYNC_CAUSE);
    AR_HOSTIF_REG(ah, AR_INTR_PRIO_ASYNC_ENABLE) =
        AR9300_HOSTIF_OFFSET(HOST_INTF_INTR_PRIORITY_ASYNC_ENABLE);
}

static void 
ar9340_init_hostif_offsets(struct ath_hal *ah)
{
    AR_HOSTIF_REG(ah, AR_RC) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_RESET_CONTROL);
    AR_HOSTIF_REG(ah, AR_WA) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_WORK_AROUND);                   
    AR_HOSTIF_REG(ah, AR_PCIE_PM_CTRL) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_PM_CTRL);
    AR_HOSTIF_REG(ah, AR_HOST_TIMEOUT) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_TIMEOUT);
    AR_HOSTIF_REG(ah, AR_SREV) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_SREV);
    AR_HOSTIF_REG(ah, AR_INTR_SYNC_CAUSE) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_INTR_SYNC_CAUSE);
    AR_HOSTIF_REG(ah, AR_INTR_SYNC_CAUSE_CLR) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_INTR_SYNC_CAUSE);
    AR_HOSTIF_REG(ah, AR_INTR_SYNC_ENABLE) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_INTR_SYNC_ENABLE);
    AR_HOSTIF_REG(ah, AR_INTR_ASYNC_MASK) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_INTR_ASYNC_MASK);
    AR_HOSTIF_REG(ah, AR_INTR_SYNC_MASK) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_INTR_SYNC_MASK);
    AR_HOSTIF_REG(ah, AR_INTR_ASYNC_CAUSE_CLR) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_INTR_ASYNC_CAUSE);
    AR_HOSTIF_REG(ah, AR_INTR_ASYNC_CAUSE) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_INTR_ASYNC_CAUSE);
    AR_HOSTIF_REG(ah, AR_INTR_ASYNC_ENABLE) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_INTR_ASYNC_ENABLE);
    AR_HOSTIF_REG(ah, AR_GPIO_OUT) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_GPIO_OUT);
    AR_HOSTIF_REG(ah, AR_GPIO_IN) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_GPIO_IN);
    AR_HOSTIF_REG(ah, AR_GPIO_OE_OUT) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_GPIO_OE);
    AR_HOSTIF_REG(ah, AR_GPIO_OE1_OUT) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_GPIO_OE1);
    AR_HOSTIF_REG(ah, AR_GPIO_INTR_POL) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_GPIO_INTR_POLAR);
    AR_HOSTIF_REG(ah, AR_GPIO_INPUT_EN_VAL) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_GPIO_INPUT_VALUE);
    AR_HOSTIF_REG(ah, AR_GPIO_INPUT_MUX1) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_GPIO_INPUT_MUX1);
    AR_HOSTIF_REG(ah, AR_GPIO_INPUT_MUX2) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_GPIO_INPUT_MUX2);
    AR_HOSTIF_REG(ah, AR_GPIO_OUTPUT_MUX1) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_GPIO_OUTPUT_MUX1);
    AR_HOSTIF_REG(ah, AR_GPIO_OUTPUT_MUX2) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_GPIO_OUTPUT_MUX2);
    AR_HOSTIF_REG(ah, AR_GPIO_OUTPUT_MUX3) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_GPIO_OUTPUT_MUX3);
    AR_HOSTIF_REG(ah, AR_INPUT_STATE) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_GPIO_INPUT_STATE);
    AR_HOSTIF_REG(ah, AR_CLKRUN) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_CLKRUN);
    AR_HOSTIF_REG(ah, AR_EEPROM_STATUS_DATA) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_EEPROM_STS);
    AR_HOSTIF_REG(ah, AR_OBS) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_OBS_CTRL);
    AR_HOSTIF_REG(ah, AR_RFSILENT) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_RFSILENT);
    AR_HOSTIF_REG(ah, AR_MISC) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_MISC);
    AR_HOSTIF_REG(ah, AR_PCIE_MSI) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_PCIE_MSI);
    AR_HOSTIF_REG(ah, AR_TDMA_CCA_CNTL) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_MAC_TDMA_CCA_CNTL);
    AR_HOSTIF_REG(ah, AR_TXAPSYNC) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_MAC_TXAPSYNC);
    AR_HOSTIF_REG(ah, AR_TXSYNC_INIT_SYNC_TMR) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_MAC_TXSYNC_INITIAL_SYNC_TMR);
    AR_HOSTIF_REG(ah, AR_INTR_PRIO_SYNC_CAUSE) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_INTR_PRIORITY_SYNC_CAUSE);
    AR_HOSTIF_REG(ah, AR_INTR_PRIO_SYNC_ENABLE) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_INTR_PRIORITY_SYNC_ENABLE);
    AR_HOSTIF_REG(ah, AR_INTR_PRIO_ASYNC_MASK) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_INTR_PRIORITY_ASYNC_MASK);
    AR_HOSTIF_REG(ah, AR_INTR_PRIO_SYNC_MASK) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_INTR_PRIORITY_SYNC_MASK);
    AR_HOSTIF_REG(ah, AR_INTR_PRIO_ASYNC_CAUSE) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_INTR_PRIORITY_ASYNC_CAUSE);
    AR_HOSTIF_REG(ah, AR_INTR_PRIO_ASYNC_ENABLE) =
        AR9340_HOSTIF_OFFSET(HOST_INTF_INTR_PRIORITY_ASYNC_ENABLE);
}

/* 
 * Host interface register offsets are different for Osprey and Wasp 
 * and hence store the offsets in hal structure
 */
static int ar9300_init_offsets(struct ath_hal *ah, u_int16_t devid)
{
    if (devid == AR9300_DEVID_AR9340) {
        ar9340_init_hostif_offsets(ah);
    } else {
        ar9300_init_hostif_offsets(ah);
    }
    return 0;
}


static const char*
ar9300_probe(uint16_t vendorid, uint16_t devid)
{
    if (vendorid != ATHEROS_VENDOR_ID)
        return AH_NULL;

    switch (devid) {
    case AR9300_DEVID_AR9380_PCIE: /* PCIE (Osprey) */
        return "Atheros AR938x";
    case AR9300_DEVID_AR9340: /* Wasp */
        return "Atheros AR934x";
    case AR9300_DEVID_AR9485_PCIE: /* Poseidon */
        return "Atheros AR9485";
    case AR9300_DEVID_AR9580_PCIE: /* Peacock */
        return "Atheros AR9580";
    case AR9300_DEVID_AR946X_PCIE: /* AR9462, AR9463, AR9482 */
        return "Atheros AR946x/AR948x";
    case AR9300_DEVID_AR9330: /* Hornet */
        return "Atheros AR933x";
    case AR9300_DEVID_QCA955X: /* Scorpion */
        return "Qualcomm Atheros QCA955x";
    case AR9300_DEVID_QCA9565: /* Aphrodite */
         return "Qualcomm Atheros AR9565";
    case AR9300_DEVID_QCA953X: /* Honeybee */
         return "Qualcomm Atheros QCA953x";
    case AR9300_DEVID_AR1111_PCIE:
         return "Atheros AR1111";
    default:
        return AH_NULL;
    }

    return AH_NULL;
}

AH_CHIP(AR9300, ar9300_probe, ar9300_attach);

