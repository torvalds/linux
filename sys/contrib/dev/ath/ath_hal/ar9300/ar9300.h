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

#ifndef _ATH_AR9300_H_
#define _ATH_AR9300_H_

#include "ar9300_freebsd_inc.h"

#define	AH_BIG_ENDIAN		4321
#define	AH_LITTLE_ENDIAN	1234

#if _BYTE_ORDER == _BIG_ENDIAN
#define	AH_BYTE_ORDER	AH_BIG_ENDIAN
#else
#define	AH_BYTE_ORDER	AH_LITTLE_ENDIAN
#endif

/* XXX doesn't belong here */
#define	AR_EEPROM_MODAL_SPURS	5

/*
 * (a) this should be N(a),
 * (b) FreeBSD does define nitems,
 * (c) it doesn't have an AH_ prefix, sigh.
 */
#define ARRAY_LENGTH(a)         (sizeof(a) / sizeof((a)[0]))

#include "ah_internal.h"
#include "ah_eeprom.h"
#include "ah_devid.h"
#include "ar9300eep.h"  /* For Eeprom definitions */


#define AR9300_MAGIC    0x19741014


/* MAC register values */

#define INIT_CONFIG_STATUS  0x00000000
#define INIT_RSSI_THR           0x7         /* Missed beacon counter initialized to 0x7 (max is 0xff) */
#define INIT_RSSI_BEACON_WEIGHT 8           /* ave beacon rssi weight (0-16) */

/*
 * Various fifo fill before Tx start, in 64-byte units
 * i.e. put the frame in the air while still DMAing
 */
#define MIN_TX_FIFO_THRESHOLD   0x1
#define MAX_TX_FIFO_THRESHOLD   (( 4096 / 64) - 1)
#define INIT_TX_FIFO_THRESHOLD  MIN_TX_FIFO_THRESHOLD

    #define CHANSEL_DIV     15
    #define FCLK            40

#define COEFF ((FCLK * 5) / 2)
#define CHANSEL_2G(_freq)   (((_freq) * 0x10000) / CHANSEL_DIV)
#define CHANSEL_5G(_freq)   (((_freq) * 0x8000) / CHANSEL_DIV)
#define CHANSEL_5G_DOT5MHZ  2188

/*
 * Receive Queue Fifo depth.
 */
enum RX_FIFO_DEPTH {
    HAL_HP_RXFIFO_DEPTH             = 16,
    HAL_LP_RXFIFO_DEPTH             = 128,
};

/*
 * Gain support.
 */
#define NUM_CORNER_FIX_BITS_2133    7
#define CCK_OFDM_GAIN_DELTA         15

enum GAIN_PARAMS {
    GP_TXCLIP,
    GP_PD90,
    GP_PD84,
    GP_GSEL
};

enum GAIN_PARAMS_2133 {
    GP_MIXGAIN_OVR,
    GP_PWD_138,
    GP_PWD_137,
    GP_PWD_136,
    GP_PWD_132,
    GP_PWD_131,
    GP_PWD_130,
};

typedef struct _gain_opt_step {
    int16_t paramVal[NUM_CORNER_FIX_BITS_2133];
    int32_t stepGain;
    int8_t  stepName[16];
} GAIN_OPTIMIZATION_STEP;

typedef struct {
    u_int32_t   numStepsInLadder;
    u_int32_t   defaultStepNum;
    GAIN_OPTIMIZATION_STEP optStep[10];
} GAIN_OPTIMIZATION_LADDER;

typedef struct {
    u_int32_t   currStepNum;
    u_int32_t   currGain;
    u_int32_t   targetGain;
    u_int32_t   loTrig;
    u_int32_t   hiTrig;
    u_int32_t   gainFCorrection;
    u_int32_t   active;
    GAIN_OPTIMIZATION_STEP *curr_step;
} GAIN_VALUES;

typedef struct {
    u_int16_t   synth_center;
    u_int16_t   ctl_center;
    u_int16_t   ext_center;
} CHAN_CENTERS;

/* RF HAL structures */
typedef struct rf_hal_funcs {
    HAL_BOOL  (*set_channel)(struct ath_hal *, struct ieee80211_channel *);
    HAL_BOOL  (*get_chip_power_lim)(struct ath_hal *ah,
        struct ieee80211_channel *chan);
} RF_HAL_FUNCS;

struct ar9300_ani_default {
    u_int16_t   m1_thresh_low;
    u_int16_t   m2_thresh_low;
    u_int16_t   m1_thresh;
    u_int16_t   m2_thresh;
    u_int16_t   m2_count_thr;
    u_int16_t   m2_count_thr_low;
    u_int16_t   m1_thresh_low_ext;
    u_int16_t   m2_thresh_low_ext;
    u_int16_t   m1_thresh_ext;
    u_int16_t   m2_thresh_ext;
    u_int16_t   firstep;
    u_int16_t   firstep_low;
    u_int16_t   cycpwr_thr1;
    u_int16_t   cycpwr_thr1_ext;
};

/*
 * Per-channel ANI state private to the driver.
 */
struct ar9300_ani_state {
    struct ieee80211_channel c;	/* XXX ew? */
    HAL_BOOL    must_restore;
    HAL_BOOL    ofdms_turn;
    u_int8_t    ofdm_noise_immunity_level;
    u_int8_t    cck_noise_immunity_level;
    u_int8_t    spur_immunity_level;
    u_int8_t    firstep_level;
    u_int8_t    ofdm_weak_sig_detect_off;
    u_int8_t    mrc_cck_off;

    /* Thresholds */
    u_int32_t   listen_time;
    u_int32_t   ofdm_trig_high;
    u_int32_t   ofdm_trig_low;
    int32_t     cck_trig_high;
    int32_t     cck_trig_low;
    int32_t     rssi_thr_low;
    int32_t     rssi_thr_high;

    int32_t     rssi;       /* The current RSSI */
    u_int32_t   tx_frame_count;   /* Last tx_frame_count */
    u_int32_t   rx_frame_count;   /* Last rx Frame count */
    u_int32_t   rx_busy_count; /* Last rx busy count */
    u_int32_t   rx_ext_busy_count; /* Last rx busy count; extension channel */
    u_int32_t   cycle_count; /* Last cycle_count (can detect wrap-around) */
    u_int32_t   ofdm_phy_err_count;/* OFDM err count since last reset */
    u_int32_t   cck_phy_err_count; /* CCK err count since last reset */

    struct ar9300_ani_default ini_def;   /* INI default values for ANI registers */
    HAL_BOOL    phy_noise_spur; /* based on OFDM/CCK Phy errors */
};

#define AR9300_ANI_POLLINTERVAL    1000    /* 1000 milliseconds between ANI poll */

#define  AR9300_CHANNEL_SWITCH_TIME_USEC  1000 /* 1 millisecond needed to change channels */

#define HAL_PROCESS_ANI     0x00000001  /* ANI state setup */
#define HAL_RADAR_EN        0x80000000  /* Radar detect is capable */
#define HAL_AR_EN           0x40000000  /* AR detect is capable */

#define DO_ANI(ah) \
    ((AH9300(ah)->ah_proc_phy_err & HAL_PROCESS_ANI))

#if 0
struct ar9300_stats {
    u_int32_t   ast_ani_niup;   /* ANI increased noise immunity */
    u_int32_t   ast_ani_nidown; /* ANI decreased noise immunity */
    u_int32_t   ast_ani_spurup; /* ANI increased spur immunity */
    u_int32_t   ast_ani_spurdown;/* ANI descreased spur immunity */
    u_int32_t   ast_ani_ofdmon; /* ANI OFDM weak signal detect on */
    u_int32_t   ast_ani_ofdmoff;/* ANI OFDM weak signal detect off */
    u_int32_t   ast_ani_cckhigh;/* ANI CCK weak signal threshold high */
    u_int32_t   ast_ani_ccklow; /* ANI CCK weak signal threshold low */
    u_int32_t   ast_ani_stepup; /* ANI increased first step level */
    u_int32_t   ast_ani_stepdown;/* ANI decreased first step level */
    u_int32_t   ast_ani_ofdmerrs;/* ANI cumulative ofdm phy err count */
    u_int32_t   ast_ani_cckerrs;/* ANI cumulative cck phy err count */
    u_int32_t   ast_ani_reset;  /* ANI parameters zero'd for non-STA */
    u_int32_t   ast_ani_lzero;  /* ANI listen time forced to zero */
    u_int32_t   ast_ani_lneg;   /* ANI listen time calculated < 0 */
    HAL_MIB_STATS   ast_mibstats;   /* MIB counter stats */
    HAL_NODE_STATS  ast_nodestats;  /* Latest rssi stats from driver */
};
#endif

struct ar9300_rad_reader {
    u_int16_t   rd_index;
    u_int16_t   rd_expSeq;
    u_int32_t   rd_resetVal;
    u_int8_t    rd_start;
};

struct ar9300_rad_writer {
    u_int16_t   wr_index;
    u_int16_t   wr_seq;
};

struct ar9300_radar_event {
    u_int32_t   re_ts;      /* 32 bit time stamp */
    u_int8_t    re_rssi;    /* rssi of radar event */
    u_int8_t    re_dur;     /* duration of radar pulse */
    u_int8_t    re_chanIndex;   /* Channel of event */
};

struct ar9300_radar_q_elem {
    u_int32_t   rq_seqNum;
    u_int32_t   rq_busy;        /* 32 bit to insure atomic read/write */
    struct ar9300_radar_event rq_event;   /* Radar event */
};

struct ar9300_radar_q_info {
    u_int16_t   ri_qsize;       /* q size */
    u_int16_t   ri_seqSize;     /* Size of sequence ring */
    struct ar9300_rad_reader ri_reader;   /* State for the q reader */
    struct ar9300_rad_writer ri_writer;   /* state for the q writer */
};

#define HAL_MAX_ACK_RADAR_DUR   511
#define HAL_MAX_NUM_PEAKS   3
#define HAL_ARQ_SIZE        4096        /* 8K AR events for buffer size */
#define HAL_ARQ_SEQSIZE     4097        /* Sequence counter wrap for AR */
#define HAL_RADARQ_SIZE     1024        /* 1K radar events for buffer size */
#define HAL_RADARQ_SEQSIZE  1025        /* Sequence counter wrap for radar */
#define HAL_NUMRADAR_STATES 64      /* Number of radar channels we keep state for */

struct ar9300_ar_state {
    u_int16_t   ar_prev_time_stamp;
    u_int32_t   ar_prev_width;
    u_int32_t   ar_phy_err_count[HAL_MAX_ACK_RADAR_DUR];
    u_int32_t   ar_ack_sum;
    u_int16_t   ar_peak_list[HAL_MAX_NUM_PEAKS];
    u_int32_t   ar_packet_threshold; /* Thresh to determine traffic load */
    u_int32_t   ar_par_threshold;    /* Thresh to determine peak */
    u_int32_t   ar_radar_rssi;       /* Rssi threshold for AR event */
};

struct ar9300_radar_state {
    struct ieee80211_channel *rs_chan;      /* Channel info */
    u_int8_t    rs_chan_index;       /* Channel index in radar structure */
    u_int32_t   rs_num_radar_events;  /* Number of radar events */
    int32_t     rs_firpwr;      /* Thresh to check radar sig is gone */
    u_int32_t   rs_radar_rssi;       /* Thresh to start radar det (dB) */
    u_int32_t   rs_height;      /* Thresh for pulse height (dB)*/
    u_int32_t   rs_pulse_rssi;       /* Thresh to check if pulse is gone (dB) */
    u_int32_t   rs_inband;      /* Thresh to check if pusle is inband (0.5 dB) */
};
typedef struct {
    u_int8_t     uc_receiver_errors;
    u_int8_t     uc_bad_tlp_errors;
    u_int8_t     uc_bad_dllp_errors;
    u_int8_t     uc_replay_timeout_errors;
    u_int8_t     uc_replay_number_rollover_errors;
} ar_pcie_error_moniter_counters;

#define AR9300_OPFLAGS_11A           0x01   /* if set, allow 11a */
#define AR9300_OPFLAGS_11G           0x02   /* if set, allow 11g */
#define AR9300_OPFLAGS_N_5G_HT40     0x04   /* if set, disable 5G HT40 */
#define AR9300_OPFLAGS_N_2G_HT40     0x08   /* if set, disable 2G HT40 */
#define AR9300_OPFLAGS_N_5G_HT20     0x10   /* if set, disable 5G HT20 */
#define AR9300_OPFLAGS_N_2G_HT20     0x20   /* if set, disable 2G HT20 */

/* 
 * For Kite and later chipsets, the following bits are not being programmed in EEPROM
 * and so need to be enabled always.
 * Bit 0: en_fcc_mid,  Bit 1: en_jap_mid,      Bit 2: en_fcc_dfs_ht40
 * Bit 3: en_jap_ht40, Bit 4: en_jap_dfs_ht40
 */
#define AR9300_RDEXT_DEFAULT  0x1F

#define AR9300_MAX_CHAINS            3
#define AR9300_NUM_CHAINS(chainmask) \
    (((chainmask >> 2) & 1) + ((chainmask >> 1) & 1) + (chainmask & 1))
#define AR9300_CHAIN0_MASK      0x1
#define AR9300_CHAIN1_MASK      0x2
#define AR9300_CHAIN2_MASK      0x4

/* Support for multiple INIs */
struct ar9300_ini_array {
    const u_int32_t *ia_array;
    u_int32_t ia_rows;
    u_int32_t ia_columns;
};
#define INIT_INI_ARRAY(iniarray, array, rows, columns) do {             \
    (iniarray)->ia_array = (const u_int32_t *)(array);    \
    (iniarray)->ia_rows = (rows);       \
    (iniarray)->ia_columns = (columns); \
} while (0)
#define INI_RA(iniarray, row, column) (((iniarray)->ia_array)[(row) * ((iniarray)->ia_columns) + (column)])

#define INIT_CAL(_perCal)   \
    (_perCal)->cal_state = CAL_WAITING;  \
    (_perCal)->cal_next = AH_NULL;

#define INSERT_CAL(_ahp, _perCal)   \
do {                    \
    if ((_ahp)->ah_cal_list_last == AH_NULL) {  \
        (_ahp)->ah_cal_list = (_ahp)->ah_cal_list_last = (_perCal); \
        ((_ahp)->ah_cal_list_last)->cal_next = (_perCal);    \
    } else {    \
        ((_ahp)->ah_cal_list_last)->cal_next = (_perCal);    \
        (_ahp)->ah_cal_list_last = (_perCal);   \
        (_perCal)->cal_next = (_ahp)->ah_cal_list;   \
    }   \
} while (0)

typedef enum cal_types {
    IQ_MISMATCH_CAL = 0x1,
    TEMP_COMP_CAL   = 0x2,
} HAL_CAL_TYPES;

typedef enum cal_state {
    CAL_INACTIVE,
    CAL_WAITING,
    CAL_RUNNING,
    CAL_DONE
} HAL_CAL_STATE;            /* Calibrate state */

#define MIN_CAL_SAMPLES     1
#define MAX_CAL_SAMPLES    64
#define INIT_LOG_COUNT      5
#define PER_MIN_LOG_COUNT   2
#define PER_MAX_LOG_COUNT  10

#define AR9300_NUM_BT_WEIGHTS   4
#define AR9300_NUM_WLAN_WEIGHTS 4

/* Per Calibration data structure */
typedef struct per_cal_data {
    HAL_CAL_TYPES cal_type;           // Type of calibration
    u_int32_t     cal_num_samples;     // Number of SW samples to collect
    u_int32_t     cal_count_max;       // Number of HW samples to collect
    void (*cal_collect)(struct ath_hal *, u_int8_t);  // Accumulator func
    void (*cal_post_proc)(struct ath_hal *, u_int8_t); // Post-processing func
} HAL_PERCAL_DATA;

/* List structure for calibration data */
typedef struct cal_list {
    const HAL_PERCAL_DATA  *cal_data;
    HAL_CAL_STATE          cal_state;
    struct cal_list        *cal_next;
} HAL_CAL_LIST;

#define AR9300_NUM_CAL_TYPES        2
#define AR9300_PAPRD_TABLE_SZ       24
#define AR9300_PAPRD_GAIN_TABLE_SZ  32
#define AR9382_MAX_GPIO_PIN_NUM                 (16)
#define AR9382_GPIO_PIN_8_RESERVED              (8)
#define AR9382_GPIO_9_INPUT_ONLY                (9)
#define AR9382_MAX_GPIO_INPUT_PIN_NUM           (13)
#define AR9382_GPIO_PIN_11_RESERVED             (11)
#define AR9382_MAX_JTAG_GPIO_PIN_NUM            (3)

/* Paprd tx power adjust data structure */
struct ar9300_paprd_pwr_adjust {
    u_int32_t     target_rate;     // rate index
    u_int32_t     reg_addr;        // register offset
    u_int32_t     reg_mask;        // mask of register
    u_int32_t     reg_mask_offset; // mask offset of register
    u_int32_t     sub_db;          // offset value unit of dB
};

struct ar9300NfLimits {
        int16_t max;
        int16_t min;
        int16_t nominal;
};

#define AR9300_MAX_RATES 36  /* legacy(4) + ofdm(8) + HTSS(8) + HTDS(8) + HTTS(8)*/
struct ath_hal_9300 {
    struct ath_hal_private  ah_priv;    /* base class */

    /*
     * Information retrieved from EEPROM.
     */
    ar9300_eeprom_t  ah_eeprom;

    GAIN_VALUES ah_gain_values;

    u_int8_t    ah_macaddr[IEEE80211_ADDR_LEN];
    u_int8_t    ah_bssid[IEEE80211_ADDR_LEN];
    u_int8_t    ah_bssid_mask[IEEE80211_ADDR_LEN];
    u_int16_t   ah_assoc_id;

    /*
     * Runtime state.
     */
    u_int32_t   ah_mask_reg;         /* copy of AR_IMR */
    u_int32_t   ah_mask2Reg;         /* copy of AR_IMR_S2 */
    u_int32_t   ah_msi_reg;          /* copy of AR_PCIE_MSI */
    os_atomic_t ah_ier_ref_count;    /* reference count for enabling interrupts */
    HAL_ANI_STATS ah_stats;        /* various statistics */
    RF_HAL_FUNCS    ah_rf_hal;
    u_int32_t   ah_tx_desc_mask;      /* mask for TXDESC */
    u_int32_t   ah_tx_ok_interrupt_mask;
    u_int32_t   ah_tx_err_interrupt_mask;
    u_int32_t   ah_tx_desc_interrupt_mask;
    u_int32_t   ah_tx_eol_interrupt_mask;
    u_int32_t   ah_tx_urn_interrupt_mask;
    HAL_TX_QUEUE_INFO ah_txq[HAL_NUM_TX_QUEUES];
    HAL_SMPS_MODE   ah_sm_power_mode;
    HAL_BOOL    ah_chip_full_sleep;
    u_int32_t   ah_atim_window;
    HAL_ANT_SETTING ah_diversity_control;    /* antenna setting */
    u_int16_t   ah_antenna_switch_swap;       /* Controls mapping of OID request */
    u_int8_t    ah_tx_chainmask_cfg;        /* chain mask config */
    u_int8_t    ah_rx_chainmask_cfg;
    u_int32_t   ah_beacon_rssi_threshold;   /* cache beacon rssi threshold */
    /* Calibration related fields */
    HAL_CAL_TYPES ah_supp_cals;
    HAL_CAL_LIST  ah_iq_cal_data;         /* IQ Cal Data */
    HAL_CAL_LIST  ah_temp_comp_cal_data;   /* Temperature Compensation Cal Data */
    HAL_CAL_LIST  *ah_cal_list;         /* ptr to first cal in list */
    HAL_CAL_LIST  *ah_cal_list_last;    /* ptr to last cal in list */
    HAL_CAL_LIST  *ah_cal_list_curr;    /* ptr to current cal */
// IQ Cal aliases
#define ah_total_power_meas_i ah_meas0.unsign
#define ah_total_power_meas_q ah_meas1.unsign
#define ah_total_iq_corr_meas ah_meas2.sign
    union {
        u_int32_t   unsign[AR9300_MAX_CHAINS];
        int32_t     sign[AR9300_MAX_CHAINS];
    } ah_meas0;
    union {
        u_int32_t   unsign[AR9300_MAX_CHAINS];
        int32_t     sign[AR9300_MAX_CHAINS];
    } ah_meas1;
    union {
        u_int32_t   unsign[AR9300_MAX_CHAINS];
        int32_t     sign[AR9300_MAX_CHAINS];
    } ah_meas2;
    union {
        u_int32_t   unsign[AR9300_MAX_CHAINS];
        int32_t     sign[AR9300_MAX_CHAINS];
    } ah_meas3;
    u_int16_t   ah_cal_samples;
    /* end - Calibration related fields */
    u_int32_t   ah_tx6_power_in_half_dbm;   /* power output for 6Mb tx */
    u_int32_t   ah_sta_id1_defaults;  /* STA_ID1 default settings */
    u_int32_t   ah_misc_mode;        /* MISC_MODE settings */
    HAL_BOOL    ah_get_plcp_hdr;      /* setting about MISC_SEL_EVM */
    enum {
        AUTO_32KHZ,     /* use it if 32kHz crystal present */
        USE_32KHZ,      /* do it regardless */
        DONT_USE_32KHZ,     /* don't use it regardless */
    } ah_enable32k_hz_clock;          /* whether to sleep at 32kHz */

    u_int32_t   ah_ofdm_tx_power;
    int16_t     ah_tx_power_index_offset;

    u_int       ah_slot_time;        /* user-specified slot time */
    u_int       ah_ack_timeout;      /* user-specified ack timeout */
    /*
     * XXX
     * 11g-specific stuff; belongs in the driver.
     */
    u_int8_t    ah_g_beacon_rate;    /* fixed rate for G beacons */
    u_int32_t   ah_gpio_mask;        /* copy of enabled GPIO mask */
    u_int32_t   ah_gpio_cause;       /* copy of GPIO cause (sync and async) */
    /*
     * RF Silent handling; setup according to the EEPROM.
     */
    u_int32_t   ah_gpio_select;      /* GPIO pin to use */
    u_int32_t   ah_polarity;        /* polarity to disable RF */
    u_int32_t   ah_gpio_bit;     /* after init, prev value */
    HAL_BOOL    ah_eep_enabled;      /* EEPROM bit for capability */

#ifdef ATH_BT_COEX
    /*
     * Bluetooth coexistence static setup according to the registry
     */
    HAL_BT_MODULE ah_bt_module;           /* Bluetooth module identifier */
    u_int8_t    ah_bt_coex_config_type;         /* BT coex configuration */
    u_int8_t    ah_bt_active_gpio_select;   /* GPIO pin for BT_ACTIVE */
    u_int8_t    ah_bt_priority_gpio_select; /* GPIO pin for BT_PRIORITY */
    u_int8_t    ah_wlan_active_gpio_select; /* GPIO pin for WLAN_ACTIVE */
    u_int8_t    ah_bt_active_polarity;     /* Polarity of BT_ACTIVE */
    HAL_BOOL    ah_bt_coex_single_ant;      /* Single or dual antenna configuration */
    u_int8_t    ah_bt_wlan_isolation;      /* Isolation between BT and WLAN in dB */
    /*
     * Bluetooth coexistence runtime settings
     */
    HAL_BOOL    ah_bt_coex_enabled;        /* If Bluetooth coexistence is enabled */
    u_int32_t   ah_bt_coex_mode;           /* Register setting for AR_BT_COEX_MODE */
    u_int32_t   ah_bt_coex_bt_weight[AR9300_NUM_BT_WEIGHTS];     /* Register setting for AR_BT_COEX_WEIGHT */
    u_int32_t   ah_bt_coex_wlan_weight[AR9300_NUM_WLAN_WEIGHTS]; /* Register setting for AR_BT_COEX_WEIGHT */
    u_int32_t   ah_bt_coex_mode2;          /* Register setting for AR_BT_COEX_MODE2 */
    u_int32_t   ah_bt_coex_flag;           /* Special tuning flags for BT coex */
#endif

    /*
     * Generic timer support
     */
    u_int32_t   ah_avail_gen_timers;       /* mask of available timers */
    u_int32_t   ah_intr_gen_timer_trigger;  /* generic timer trigger interrupt state */
    u_int32_t   ah_intr_gen_timer_thresh;   /* generic timer trigger interrupt state */
    HAL_BOOL    ah_enable_tsf2;           /* enable TSF2 for gen timer 8-15. */

    /*
     * ANI & Radar support.
     */
    u_int32_t   ah_proc_phy_err;      /* Process Phy errs */
    u_int32_t   ah_ani_period;       /* ani update list period */
    struct ar9300_ani_state   *ah_curani; /* cached last reference */
    struct ar9300_ani_state   ah_ani[255]; /* per-channel state */
    struct ar9300_radar_state ah_radar[HAL_NUMRADAR_STATES];  /* Per-Channel Radar detector state */
    struct ar9300_radar_q_elem *ah_radarq; /* radar event queue */
    struct ar9300_radar_q_info ah_radarq_info;  /* radar event q read/write state */
    struct ar9300_ar_state    ah_ar;      /* AR detector state */
    struct ar9300_radar_q_elem *ah_arq;    /* AR event queue */
    struct ar9300_radar_q_info ah_arq_info; /* AR event q read/write state */

    /*
     * Transmit power state.  Note these are maintained
     * here so they can be retrieved by diagnostic tools.
     */
    u_int16_t   ah_rates_array[16];

    /*
     * Tx queue interrupt state.
     */
    u_int32_t   ah_intr_txqs;

    HAL_BOOL    ah_intr_mitigation_rx; /* rx Interrupt Mitigation Settings */
    HAL_BOOL    ah_intr_mitigation_tx; /* tx Interrupt Mitigation Settings */

    /*
     * Extension Channel Rx Clear State
     */
    u_int32_t   ah_cycle_count;
    u_int32_t   ah_ctl_busy;
    u_int32_t   ah_ext_busy;

    /* HT CWM state */
    HAL_HT_EXTPROTSPACING ah_ext_prot_spacing;
    u_int8_t    ah_tx_chainmask; /* tx chain mask */
    u_int8_t    ah_rx_chainmask; /* rx chain mask */

    /* optional tx chainmask */
    u_int8_t    ah_tx_chainmaskopt;

    u_int8_t    ah_tx_cal_chainmask; /* tx cal chain mask */
    u_int8_t    ah_rx_cal_chainmask; /* rx cal chain mask */

    int         ah_hwp;
    void        *ah_cal_mem;
    HAL_BOOL    ah_emu_eeprom;

    HAL_ANI_CMD ah_ani_function;
    HAL_BOOL    ah_rifs_enabled;
    u_int32_t   ah_rifs_reg[11];
    u_int32_t   ah_rifs_sec_cnt;

    /* open-loop power control */
    u_int32_t original_gain[22];
    int32_t   init_pdadc;
    int32_t   pdadc_delta;

    /* cycle counts for beacon stuck diagnostics */
    u_int32_t   ah_cycles;
    u_int32_t   ah_rx_clear;
    u_int32_t   ah_rx_frame;
    u_int32_t   ah_tx_frame;

#define BB_HANG_SIG1 0
#define BB_HANG_SIG2 1
#define BB_HANG_SIG3 2
#define BB_HANG_SIG4 3
#define MAC_HANG_SIG1 4
#define MAC_HANG_SIG2 5
    /* bb hang detection */
    int     ah_hang[6];
    hal_hw_hangs_t  ah_hang_wars;

    /*
     * Keytable type table
     */
#define	AR_KEYTABLE_SIZE 128		/* XXX! */
    uint8_t ah_keytype[AR_KEYTABLE_SIZE];
#undef	AR_KEYTABLE_SIZE
    /*
     * Support for ar9300 multiple INIs
     */
    struct ar9300_ini_array ah_ini_pcie_serdes;
    struct ar9300_ini_array ah_ini_pcie_serdes_low_power;
    struct ar9300_ini_array ah_ini_modes_additional;
    struct ar9300_ini_array ah_ini_modes_additional_40mhz;
    struct ar9300_ini_array ah_ini_modes_rxgain;
    struct ar9300_ini_array ah_ini_modes_rxgain_bounds;
    struct ar9300_ini_array ah_ini_modes_txgain;
    struct ar9300_ini_array ah_ini_japan2484;
    struct ar9300_ini_array ah_ini_radio_post_sys2ant;
    struct ar9300_ini_array ah_ini_BTCOEX_MAX_TXPWR;
    struct ar9300_ini_array ah_ini_modes_rxgain_xlna;
    struct ar9300_ini_array ah_ini_modes_rxgain_bb_core;
    struct ar9300_ini_array ah_ini_modes_rxgain_bb_postamble;

    /* 
     * New INI format starting with Osprey 2.0 INI.
     * Pre, core, post arrays for each sub-system (mac, bb, radio, soc)
     */
    #define ATH_INI_PRE     0
    #define ATH_INI_CORE    1
    #define ATH_INI_POST    2
    #define ATH_INI_NUM_SPLIT   (ATH_INI_POST + 1)
    struct ar9300_ini_array ah_ini_mac[ATH_INI_NUM_SPLIT];     /* New INI format */
    struct ar9300_ini_array ah_ini_bb[ATH_INI_NUM_SPLIT];      /* New INI format */
    struct ar9300_ini_array ah_ini_radio[ATH_INI_NUM_SPLIT];   /* New INI format */
    struct ar9300_ini_array ah_ini_soc[ATH_INI_NUM_SPLIT];     /* New INI format */

    /* 
     * Added to support DFS postamble array in INI that we need to apply
     * in DFS channels
     */

    struct ar9300_ini_array ah_ini_dfs;

#if ATH_WOW
    struct ar9300_ini_array ah_ini_pcie_serdes_wow;  /* SerDes values during WOW sleep */
#endif

    /* To indicate EEPROM mapping used */
    u_int32_t ah_immunity_vals[6];
    HAL_BOOL ah_immunity_on;
    /*
     * snap shot of counter register for debug purposes
     */
#ifdef AH_DEBUG
    u_int32_t last_tf;
    u_int32_t last_rf;
    u_int32_t last_rc;
    u_int32_t last_cc;
#endif
    HAL_BOOL    ah_dma_stuck; /* Set to AH_TRUE when RX/TX DMA failed to stop. */
    u_int32_t   nf_tsf32; /* timestamp for NF calibration duration */

    u_int32_t  reg_dmn;                  /* Regulatory Domain */
    int16_t    twice_antenna_gain;       /* Antenna Gain */
    u_int16_t  twice_antenna_reduction;  /* Antenna Gain Allowed */

    /*
     * Upper limit after factoring in the regulatory max, antenna gain and 
     * multichain factor. No TxBF, CDD or STBC gain factored 
     */
    int16_t upper_limit[AR9300_MAX_CHAINS]; 

    /* adjusted power for descriptor-based TPC for 1, 2, or 3 chains */
    int16_t txpower[AR9300_MAX_RATES][AR9300_MAX_CHAINS];

    /* adjusted power for descriptor-based TPC for 1, 2, or 3 chains with STBC*/
    int16_t txpower_stbc[AR9300_MAX_RATES][AR9300_MAX_CHAINS];

    /* Transmit Status ring support */
    struct ar9300_txs    *ts_ring;
    u_int16_t            ts_tail;
    u_int16_t            ts_size;
    u_int32_t            ts_paddr_start;
    u_int32_t            ts_paddr_end;

    /* Receive Buffer size */
#define HAL_RXBUFSIZE_DEFAULT 0xfff
    u_int16_t            rx_buf_size;

    u_int32_t            ah_wa_reg_val; // Store the permanent value of Reg 0x4004 so we dont have to R/M/W. (We should not be reading this register when in sleep states).

    /* Indicate the PLL source clock rate is 25Mhz or not.
     * clk_25mhz = 0 by default.
     */
    u_int8_t             clk_25mhz;
    /* For PAPRD uses */
    u_int16_t   small_signal_gain[AH_MAX_CHAINS];
    u_int32_t   pa_table[AH_MAX_CHAINS][AR9300_PAPRD_TABLE_SZ];
    u_int32_t   paprd_gain_table_entries[AR9300_PAPRD_GAIN_TABLE_SZ];
    u_int32_t   paprd_gain_table_index[AR9300_PAPRD_GAIN_TABLE_SZ];
    u_int32_t   ah_2g_paprd_rate_mask_ht20; /* Copy of eep->modal_header_2g.paprd_rate_mask_ht20 */ 
    u_int32_t   ah_2g_paprd_rate_mask_ht40; /* Copy of eep->modal_header_2g.paprd_rate_mask_ht40 */ 
    u_int32_t   ah_5g_paprd_rate_mask_ht20; /* Copy of eep->modal_header_5g.paprd_rate_mask_ht20 */ 
    u_int32_t   ah_5g_paprd_rate_mask_ht40; /* Copy of eep->modal_header_5g.paprd_rate_mask_ht40 */ 
    u_int32_t   paprd_training_power;
    /* For GreenTx use to store the default tx power */
    u_int8_t    ah_default_tx_power[ar9300_rate_size];
    HAL_BOOL        ah_paprd_broken;
   
    /* To store offsets of host interface registers */
    struct {
        u_int32_t AR_RC;
        u_int32_t AR_WA;
        u_int32_t AR_PM_STATE;
        u_int32_t AR_H_INFOL;
        u_int32_t AR_H_INFOH;
        u_int32_t AR_PCIE_PM_CTRL;
        u_int32_t AR_HOST_TIMEOUT;
        u_int32_t AR_EEPROM;
        u_int32_t AR_SREV;
        u_int32_t AR_INTR_SYNC_CAUSE;
        u_int32_t AR_INTR_SYNC_CAUSE_CLR;
        u_int32_t AR_INTR_SYNC_ENABLE;
        u_int32_t AR_INTR_ASYNC_MASK;
        u_int32_t AR_INTR_SYNC_MASK;
        u_int32_t AR_INTR_ASYNC_CAUSE_CLR;
        u_int32_t AR_INTR_ASYNC_CAUSE;
        u_int32_t AR_INTR_ASYNC_ENABLE;
        u_int32_t AR_PCIE_SERDES;
        u_int32_t AR_PCIE_SERDES2;
        u_int32_t AR_GPIO_OUT;
        u_int32_t AR_GPIO_IN;
        u_int32_t AR_GPIO_OE_OUT;
        u_int32_t AR_GPIO_OE1_OUT;
        u_int32_t AR_GPIO_INTR_POL;
        u_int32_t AR_GPIO_INPUT_EN_VAL;
        u_int32_t AR_GPIO_INPUT_MUX1;
        u_int32_t AR_GPIO_INPUT_MUX2;
        u_int32_t AR_GPIO_OUTPUT_MUX1;
        u_int32_t AR_GPIO_OUTPUT_MUX2;
        u_int32_t AR_GPIO_OUTPUT_MUX3;
        u_int32_t AR_INPUT_STATE;
        u_int32_t AR_SPARE;
        u_int32_t AR_PCIE_CORE_RESET_EN;
        u_int32_t AR_CLKRUN;
        u_int32_t AR_EEPROM_STATUS_DATA;
        u_int32_t AR_OBS;
        u_int32_t AR_RFSILENT;
        u_int32_t AR_GPIO_PDPU;
        u_int32_t AR_GPIO_DS;
        u_int32_t AR_MISC;
        u_int32_t AR_PCIE_MSI;
        u_int32_t AR_TSF_SNAPSHOT_BT_ACTIVE;
        u_int32_t AR_TSF_SNAPSHOT_BT_PRIORITY;
        u_int32_t AR_TSF_SNAPSHOT_BT_CNTL;
        u_int32_t AR_PCIE_PHY_LATENCY_NFTS_ADJ;
        u_int32_t AR_TDMA_CCA_CNTL;
        u_int32_t AR_TXAPSYNC;
        u_int32_t AR_TXSYNC_INIT_SYNC_TMR;
        u_int32_t AR_INTR_PRIO_SYNC_CAUSE;
        u_int32_t AR_INTR_PRIO_SYNC_ENABLE;
        u_int32_t AR_INTR_PRIO_ASYNC_MASK;
        u_int32_t AR_INTR_PRIO_SYNC_MASK;
        u_int32_t AR_INTR_PRIO_ASYNC_CAUSE;
        u_int32_t AR_INTR_PRIO_ASYNC_ENABLE;
    } ah_hostifregs;

    u_int32_t ah_enterprise_mode;
    u_int32_t ah_radar1;
    u_int32_t ah_dc_offset;
    HAL_BOOL  ah_hw_green_tx_enable; /* 1:enalbe H/W Green Tx */
    HAL_BOOL  ah_smartantenna_enable; /* 1:enalbe H/W */
    u_int32_t ah_disable_cck;
    HAL_BOOL  ah_lna_div_use_bt_ant_enable; /* 1:enable Rx(LNA) Diversity */


    /*
     * Different types of memory where the calibration data might be stored.
     * All types are searched in Ar9300EepromRestore() in the order flash, eeprom, otp.
     * To disable searching a type, set its parameter to 0.
     */
    int try_dram;
    int try_flash;
    int try_eeprom;
    int try_otp;
#ifdef ATH_CAL_NAND_FLASH
    int try_nand;
#endif
    /*
     * This is where we found the calibration data.
     */
    int calibration_data_source;
    int calibration_data_source_address;
    /*
     * This is where we look for the calibration data. must be set before ath_attach() is called
     */
    int calibration_data_try;
    int calibration_data_try_address;
    u_int8_t
        tx_iq_cal_enable         : 1,
        tx_iq_cal_during_agc_cal : 1,
        tx_cl_cal_enable         : 1;

#if ATH_SUPPORT_MCI
    /* For MCI */
    HAL_BOOL                ah_mci_ready;
    u_int32_t           ah_mci_int_raw;
    u_int32_t           ah_mci_int_rx_msg;
    u_int32_t           ah_mci_rx_status;
    u_int32_t           ah_mci_cont_status;
    u_int8_t            ah_mci_bt_state;
    u_int32_t           ah_mci_gpm_addr;
    u_int8_t            *ah_mci_gpm_buf;
    u_int32_t           ah_mci_gpm_len;
    u_int32_t           ah_mci_gpm_idx;
    u_int32_t           ah_mci_sched_addr;
    u_int8_t            *ah_mci_sched_buf;
    u_int8_t            ah_mci_coex_major_version_wlan;
    u_int8_t            ah_mci_coex_minor_version_wlan;
    u_int8_t            ah_mci_coex_major_version_bt;
    u_int8_t            ah_mci_coex_minor_version_bt;
    HAL_BOOL                ah_mci_coex_bt_version_known;
    HAL_BOOL                ah_mci_coex_wlan_channels_update;
    u_int32_t           ah_mci_coex_wlan_channels[4];
    HAL_BOOL                ah_mci_coex_2g5g_update;
    HAL_BOOL                ah_mci_coex_is_2g;
    HAL_BOOL                ah_mci_query_bt;
    HAL_BOOL                ah_mci_unhalt_bt_gpm; /* need send UNHALT */
    HAL_BOOL                ah_mci_halted_bt_gpm; /* HALT sent */
    HAL_BOOL                ah_mci_need_flush_btinfo;
    HAL_BOOL                ah_mci_concur_tx_en;
    u_int8_t            ah_mci_stomp_low_tx_pri;
    u_int8_t            ah_mci_stomp_all_tx_pri;
    u_int8_t            ah_mci_stomp_none_tx_pri;
    u_int32_t           ah_mci_wlan_cal_seq;
    u_int32_t           ah_mci_wlan_cal_done;
#if ATH_SUPPORT_AIC
    HAL_BOOL                ah_aic_enabled;
    u_int32_t           ah_aic_sram[ATH_AIC_MAX_BT_CHANNEL];
#endif

#endif /* ATH_SUPPORT_MCI */
    u_int8_t            ah_cac_quiet_enabled;
#if ATH_WOW_OFFLOAD
    u_int32_t           ah_mcast_filter_l32_set;
    u_int32_t           ah_mcast_filter_u32_set;
#endif
    HAL_BOOL            ah_reduced_self_gen_mask;
    HAL_BOOL                ah_chip_reset_done;
    HAL_BOOL                ah_abort_txdma_norx;
    /* store previous passive RX Cal info */
    HAL_BOOL                ah_skip_rx_iq_cal;
    HAL_BOOL                ah_rx_cal_complete; /* previous rx cal completed or not */
    u_int32_t           ah_rx_cal_chan;     /* chan on which rx cal is done */
    u_int32_t           ah_rx_cal_chan_flag;
    u_int32_t           ah_rx_cal_corr[AR9300_MAX_CHAINS];

    /* Local additions for FreeBSD */
    /*
     * These fields are in the top level HAL in the atheros
     * codebase; here we place them in the AR9300 HAL and
     * access them via accessor methods if the driver requires them.
     */
    u_int32_t            ah_ob_db1[3];
    u_int32_t            ah_db2[3];
    u_int32_t            ah_bb_panic_timeout_ms;
    u_int32_t            ah_bb_panic_last_status;
    u_int32_t            ah_tx_trig_level;
    u_int16_t            ath_hal_spur_chans[AR_EEPROM_MODAL_SPURS][2];
    int16_t              nf_cw_int_delta; /* diff btwn nominal NF and CW interf threshold */
    int                  ah_phyrestart_disabled;
    HAL_RSSI_TX_POWER    green_tx_status;
    int                  green_ap_ps_on;
    int                  ah_enable_keysearch_always;
    int                  ah_fccaifs;
    int ah_reset_reason;
    int ah_dcs_enable;
    HAL_ANI_STATE ext_ani_state;     /* FreeBSD; external facing ANI state */

    struct ar9300NfLimits nf_2GHz;
    struct ar9300NfLimits nf_5GHz;
    struct ar9300NfLimits *nfp;

    uint32_t ah_beaconInterval;
};

#define AH9300(_ah) ((struct ath_hal_9300 *)(_ah))

#define IS_9300_EMU(ah) \
    (AH_PRIVATE(ah)->ah_devid == AR9300_DEVID_EMU_PCIE)

#define ar9300_eep_data_in_flash(_ah) \
    (!(AH_PRIVATE(_ah)->ah_flags & AH_USE_EEPROM))

#ifdef notyet
// Need these additional conditions for IS_5GHZ_FAST_CLOCK_EN when we have valid eeprom contents.
&& \
        ((ar9300_eeprom_get(AH9300(_ah), EEP_MINOR_REV) <= AR9300_EEP_MINOR_VER_16) || \
        (ar9300_eeprom_get(AH9300(_ah), EEP_FSTCLK_5G))))
#endif

/*
 * WAR for bug 6773.  OS_DELAY() does a PIO READ on the PCI bus which allows
 * other cards' DMA reads to complete in the middle of our reset.
 */
#define WAR_6773(x) do {                \
        if ((++(x) % 64) == 0)          \
                OS_DELAY(1);            \
} while (0)

#define REG_WRITE_ARRAY(iniarray, column, regWr) do {                   \
        int r;                                                          \
        for (r = 0; r < ((iniarray)->ia_rows); r++) {    \
                OS_REG_WRITE(ah, INI_RA((iniarray), (r), 0), INI_RA((iniarray), r, (column)));\
                WAR_6773(regWr);                                        \
        }                                                               \
} while (0)

#define UPPER_5G_SUB_BANDSTART 5700
#define MID_5G_SUB_BANDSTART 5400
#define TRAINPOWER_DB_OFFSET 6 

#define AH_PAPRD_GET_SCALE_FACTOR(_scale, _eep, _is2G, _channel) do{ if(_is2G) { _scale = (_eep->modal_header_2g.paprd_rate_mask_ht20>>25)&0x7; \
                                                                } else { \
                                                                    if(_channel >= UPPER_5G_SUB_BANDSTART){ _scale = (_eep->modal_header_5g.paprd_rate_mask_ht20>>25)&0x7;} \
                                                                    else if((UPPER_5G_SUB_BANDSTART < _channel) && (_channel >= MID_5G_SUB_BANDSTART)) \
                                                                        { _scale = (_eep->modal_header_5g.paprd_rate_mask_ht40>>28)&0x7;} \
                                                                        else { _scale = (_eep->modal_header_5g.paprd_rate_mask_ht40>>25)&0x7;} } }while(0)

#ifdef AH_ASSERT
    #define ar9300FeatureNotSupported(feature, ah, func)    \
        ath_hal_printf(ah, # feature                        \
            " not supported but called from %s\n", (func)), \
        hal_assert(0)
#else
    #define ar9300FeatureNotSupported(feature, ah, func)    \
        ath_hal_printf(ah, # feature                        \
            " not supported but called from %s\n", (func))
#endif /* AH_ASSERT */

/*
 * Green Tx, Based on different RSSI of Received Beacon thresholds, 
 * using different tx power by modified register tx power related values.
 * The thresholds are decided by system team.
 */
#define WB225_SW_GREEN_TX_THRES1_DB              56  /* in dB */
#define WB225_SW_GREEN_TX_THRES2_DB              41  /* in dB */
#define WB225_OB_CALIBRATION_VALUE               5   /* For Green Tx OLPC Delta
                                                        Calibration Offset */
#define WB225_OB_GREEN_TX_SHORT_VALUE            1   /* For Green Tx OB value
                                                        in short distance*/
#define WB225_OB_GREEN_TX_MIDDLE_VALUE           3   /* For Green Tx OB value
                                                        in middle distance */
#define WB225_OB_GREEN_TX_LONG_VALUE             5   /* For Green Tx OB value
                                                        in long distance */
#define WB225_BBPWRTXRATE9_SW_GREEN_TX_SHORT_VALUE  0x06060606 /* For SwGreen Tx 
                                                        BB_powertx_rate9 reg
                                                        value in short 
                                                        distance */
#define WB225_BBPWRTXRATE9_SW_GREEN_TX_MIDDLE_VALUE 0x0E0E0E0E /* For SwGreen Tx 
                                                        BB_powertx_rate9 reg
                                                        value in middle 
                                                        distance */


/* Tx power for short distacnce in SwGreenTx.*/
static const u_int8_t wb225_sw_gtx_tp_distance_short[ar9300_rate_size] = {
        6,  /*ALL_TARGET_LEGACY_6_24*/
        6,  /*ALL_TARGET_LEGACY_36*/
        6,  /*ALL_TARGET_LEGACY_48*/
        4,  /*ALL_TARGET_LEGACY_54*/
        6,  /*ALL_TARGET_LEGACY_1L_5L*/
        6,  /*ALL_TARGET_LEGACY_5S*/
        6,  /*ALL_TARGET_LEGACY_11L*/
        6,  /*ALL_TARGET_LEGACY_11S*/
        6,  /*ALL_TARGET_HT20_0_8_16*/
        6,  /*ALL_TARGET_HT20_1_3_9_11_17_19*/
        4,  /*ALL_TARGET_HT20_4*/
        4,  /*ALL_TARGET_HT20_5*/
        4,  /*ALL_TARGET_HT20_6*/
        2,  /*ALL_TARGET_HT20_7*/
        0,  /*ALL_TARGET_HT20_12*/
        0,  /*ALL_TARGET_HT20_13*/
        0,  /*ALL_TARGET_HT20_14*/
        0,  /*ALL_TARGET_HT20_15*/
        0,  /*ALL_TARGET_HT20_20*/
        0,  /*ALL_TARGET_HT20_21*/
        0,  /*ALL_TARGET_HT20_22*/
        0,  /*ALL_TARGET_HT20_23*/
        6,  /*ALL_TARGET_HT40_0_8_16*/
        6,  /*ALL_TARGET_HT40_1_3_9_11_17_19*/
        4,  /*ALL_TARGET_HT40_4*/
        4,  /*ALL_TARGET_HT40_5*/
        4,  /*ALL_TARGET_HT40_6*/
        2,  /*ALL_TARGET_HT40_7*/
        0,  /*ALL_TARGET_HT40_12*/
        0,  /*ALL_TARGET_HT40_13*/
        0,  /*ALL_TARGET_HT40_14*/
        0,  /*ALL_TARGET_HT40_15*/
        0,  /*ALL_TARGET_HT40_20*/
        0,  /*ALL_TARGET_HT40_21*/
        0,  /*ALL_TARGET_HT40_22*/
        0   /*ALL_TARGET_HT40_23*/
};

/* Tx power for middle distacnce in SwGreenTx.*/
static const u_int8_t wb225_sw_gtx_tp_distance_middle[ar9300_rate_size] =  {
        14, /*ALL_TARGET_LEGACY_6_24*/
        14, /*ALL_TARGET_LEGACY_36*/
        14, /*ALL_TARGET_LEGACY_48*/
        12, /*ALL_TARGET_LEGACY_54*/
        14, /*ALL_TARGET_LEGACY_1L_5L*/
        14, /*ALL_TARGET_LEGACY_5S*/
        14, /*ALL_TARGET_LEGACY_11L*/
        14, /*ALL_TARGET_LEGACY_11S*/
        14, /*ALL_TARGET_HT20_0_8_16*/
        14, /*ALL_TARGET_HT20_1_3_9_11_17_19*/
        14, /*ALL_TARGET_HT20_4*/
        14, /*ALL_TARGET_HT20_5*/
        12, /*ALL_TARGET_HT20_6*/
        10, /*ALL_TARGET_HT20_7*/
        0,  /*ALL_TARGET_HT20_12*/
        0,  /*ALL_TARGET_HT20_13*/
        0,  /*ALL_TARGET_HT20_14*/
        0,  /*ALL_TARGET_HT20_15*/
        0,  /*ALL_TARGET_HT20_20*/
        0,  /*ALL_TARGET_HT20_21*/
        0,  /*ALL_TARGET_HT20_22*/
        0,  /*ALL_TARGET_HT20_23*/
        14, /*ALL_TARGET_HT40_0_8_16*/
        14, /*ALL_TARGET_HT40_1_3_9_11_17_19*/
        14, /*ALL_TARGET_HT40_4*/
        14, /*ALL_TARGET_HT40_5*/
        12, /*ALL_TARGET_HT40_6*/
        10, /*ALL_TARGET_HT40_7*/
        0,  /*ALL_TARGET_HT40_12*/
        0,  /*ALL_TARGET_HT40_13*/
        0,  /*ALL_TARGET_HT40_14*/
        0,  /*ALL_TARGET_HT40_15*/
        0,  /*ALL_TARGET_HT40_20*/
        0,  /*ALL_TARGET_HT40_21*/
        0,  /*ALL_TARGET_HT40_22*/
        0   /*ALL_TARGET_HT40_23*/
};

/* OLPC DeltaCalibration Offset unit in half dB.*/
static const u_int8_t wb225_gtx_olpc_cal_offset[6] =  {
        0,  /* OB0*/
        16, /* OB1*/
        9,  /* OB2*/
        5,  /* OB3*/
        2,  /* OB4*/
        0,  /* OB5*/
};

/*
 * Definitions for HwGreenTx
 */
#define AR9485_HW_GREEN_TX_THRES1_DB              56  /* in dB */
#define AR9485_HW_GREEN_TX_THRES2_DB              41  /* in dB */
#define AR9485_BBPWRTXRATE9_HW_GREEN_TX_SHORT_VALUE 0x0C0C0A0A /* For HwGreen Tx 
                                                        BB_powertx_rate9 reg
                                                        value in short 
                                                        distance */
#define AR9485_BBPWRTXRATE9_HW_GREEN_TX_MIDDLE_VALUE 0x10100E0E /* For HwGreenTx 
                                                        BB_powertx_rate9 reg
                                                        value in middle 
                                                        distance */

/* Tx power for short distacnce in HwGreenTx.*/
static const u_int8_t ar9485_hw_gtx_tp_distance_short[ar9300_rate_size] = {
        14, /*ALL_TARGET_LEGACY_6_24*/
        14, /*ALL_TARGET_LEGACY_36*/
        8,  /*ALL_TARGET_LEGACY_48*/
        2,  /*ALL_TARGET_LEGACY_54*/
        14, /*ALL_TARGET_LEGACY_1L_5L*/
        14, /*ALL_TARGET_LEGACY_5S*/
        14, /*ALL_TARGET_LEGACY_11L*/
        14, /*ALL_TARGET_LEGACY_11S*/
        12, /*ALL_TARGET_HT20_0_8_16*/
        12, /*ALL_TARGET_HT20_1_3_9_11_17_19*/
        12, /*ALL_TARGET_HT20_4*/
        12, /*ALL_TARGET_HT20_5*/
        8,  /*ALL_TARGET_HT20_6*/
        2,  /*ALL_TARGET_HT20_7*/
        0,  /*ALL_TARGET_HT20_12*/
        0,  /*ALL_TARGET_HT20_13*/
        0,  /*ALL_TARGET_HT20_14*/
        0,  /*ALL_TARGET_HT20_15*/
        0,  /*ALL_TARGET_HT20_20*/
        0,  /*ALL_TARGET_HT20_21*/
        0,  /*ALL_TARGET_HT20_22*/
        0,  /*ALL_TARGET_HT20_23*/
        10, /*ALL_TARGET_HT40_0_8_16*/
        10, /*ALL_TARGET_HT40_1_3_9_11_17_19*/
        10, /*ALL_TARGET_HT40_4*/
        10, /*ALL_TARGET_HT40_5*/
        6,  /*ALL_TARGET_HT40_6*/
        2,  /*ALL_TARGET_HT40_7*/
        0,  /*ALL_TARGET_HT40_12*/
        0,  /*ALL_TARGET_HT40_13*/
        0,  /*ALL_TARGET_HT40_14*/
        0,  /*ALL_TARGET_HT40_15*/
        0,  /*ALL_TARGET_HT40_20*/
        0,  /*ALL_TARGET_HT40_21*/
        0,  /*ALL_TARGET_HT40_22*/
        0   /*ALL_TARGET_HT40_23*/
};

/* Tx power for middle distacnce in HwGreenTx.*/
static const u_int8_t ar9485_hw_gtx_tp_distance_middle[ar9300_rate_size] =  {
        18, /*ALL_TARGET_LEGACY_6_24*/
        18, /*ALL_TARGET_LEGACY_36*/
        14, /*ALL_TARGET_LEGACY_48*/
        12, /*ALL_TARGET_LEGACY_54*/
        18, /*ALL_TARGET_LEGACY_1L_5L*/
        18, /*ALL_TARGET_LEGACY_5S*/
        18, /*ALL_TARGET_LEGACY_11L*/
        18, /*ALL_TARGET_LEGACY_11S*/
        16, /*ALL_TARGET_HT20_0_8_16*/
        16, /*ALL_TARGET_HT20_1_3_9_11_17_19*/
        16, /*ALL_TARGET_HT20_4*/
        16, /*ALL_TARGET_HT20_5*/
        14, /*ALL_TARGET_HT20_6*/
        12, /*ALL_TARGET_HT20_7*/
        0,  /*ALL_TARGET_HT20_12*/
        0,  /*ALL_TARGET_HT20_13*/
        0,  /*ALL_TARGET_HT20_14*/
        0,  /*ALL_TARGET_HT20_15*/
        0,  /*ALL_TARGET_HT20_20*/
        0,  /*ALL_TARGET_HT20_21*/
        0,  /*ALL_TARGET_HT20_22*/
        0,  /*ALL_TARGET_HT20_23*/
        14, /*ALL_TARGET_HT40_0_8_16*/
        14, /*ALL_TARGET_HT40_1_3_9_11_17_19*/
        14, /*ALL_TARGET_HT40_4*/
        14, /*ALL_TARGET_HT40_5*/
        14, /*ALL_TARGET_HT40_6*/
        12, /*ALL_TARGET_HT40_7*/
        0,  /*ALL_TARGET_HT40_12*/
        0,  /*ALL_TARGET_HT40_13*/
        0,  /*ALL_TARGET_HT40_14*/
        0,  /*ALL_TARGET_HT40_15*/
        0,  /*ALL_TARGET_HT40_20*/
        0,  /*ALL_TARGET_HT40_21*/
        0,  /*ALL_TARGET_HT40_22*/
        0   /*ALL_TARGET_HT40_23*/
};

/* MIMO Modes used in TPC calculations */
typedef enum {
    AR9300_DEF_MODE = 0, /* Could be CDD or Direct */
    AR9300_TXBF_MODE,        
    AR9300_STBC_MODE
} AR9300_TXMODES;
typedef enum {
    POSEIDON_STORED_REG_OBDB    = 0,    /* default OB/DB setting from ini */
    POSEIDON_STORED_REG_TPC     = 1,    /* default txpower value in TPC reg */
    POSEIDON_STORED_REG_BB_PWRTX_RATE9 = 2, /* default txpower value in 
                                             *  BB_powertx_rate9 reg 
                                             */
    POSEIDON_STORED_REG_SZ              /* Can not add anymore */
} POSEIDON_STORED_REGS;

typedef enum {
    POSEIDON_STORED_REG_G2_OLPC_OFFSET  = 0,/* default OB/DB setting from ini */
    POSEIDON_STORED_REG_G2_SZ               /* should not exceed 3 */
} POSEIDON_STORED_REGS_G2;

#if AH_NEED_TX_DATA_SWAP
#if AH_NEED_RX_DATA_SWAP
#define ar9300_init_cfg_reg(ah) OS_REG_RMW(ah, AR_CFG, AR_CFG_SWTB | AR_CFG_SWRB,0)
#else
#define ar9300_init_cfg_reg(ah) OS_REG_RMW(ah, AR_CFG, AR_CFG_SWTB,0)
#endif
#elif AH_NEED_RX_DATA_SWAP
#define ar9300_init_cfg_reg(ah) OS_REG_RMW(ah, AR_CFG, AR_CFG_SWRB,0)
#else
#define ar9300_init_cfg_reg(ah) OS_REG_RMW(ah, AR_CFG, AR_CFG_SWTD | AR_CFG_SWRD,0)
#endif

extern  HAL_BOOL ar9300_rf_attach(struct ath_hal *, HAL_STATUS *);

struct ath_hal;

extern  struct ath_hal_9300 * ar9300_new_state(u_int16_t devid,
        HAL_SOFTC sc, HAL_BUS_TAG st, HAL_BUS_HANDLE sh, uint16_t *eepromdata,
        HAL_OPS_CONFIG *ah_config,
        HAL_STATUS *status);
extern  struct ath_hal * ar9300_attach(u_int16_t devid,
        HAL_SOFTC sc, HAL_BUS_TAG st, HAL_BUS_HANDLE sh, uint16_t *eepromdata,
        HAL_OPS_CONFIG *ah_config, HAL_STATUS *status);
extern  void ar9300_detach(struct ath_hal *ah);
extern void ar9300_read_revisions(struct ath_hal *ah);
extern  HAL_BOOL ar9300_chip_test(struct ath_hal *ah);
extern  HAL_BOOL ar9300_get_channel_edges(struct ath_hal *ah,
                u_int16_t flags, u_int16_t *low, u_int16_t *high);
extern  HAL_BOOL ar9300_fill_capability_info(struct ath_hal *ah);

extern  void ar9300_beacon_init(struct ath_hal *ah,
                              u_int32_t next_beacon, u_int32_t beacon_period, 
                              u_int32_t beacon_period_fraction, HAL_OPMODE opmode);
extern  void ar9300_set_sta_beacon_timers(struct ath_hal *ah,
        const HAL_BEACON_STATE *);

extern  HAL_BOOL ar9300_is_interrupt_pending(struct ath_hal *ah);
extern  HAL_BOOL ar9300_get_pending_interrupts(struct ath_hal *ah, HAL_INT *, HAL_INT_TYPE, u_int8_t, HAL_BOOL);
extern  HAL_INT ar9300_get_interrupts(struct ath_hal *ah);
extern  HAL_INT ar9300_set_interrupts(struct ath_hal *ah, HAL_INT ints, HAL_BOOL);
extern  void ar9300_set_intr_mitigation_timer(struct ath_hal* ah,
        HAL_INT_MITIGATION reg, u_int32_t value);
extern  u_int32_t ar9300_get_intr_mitigation_timer(struct ath_hal* ah,
        HAL_INT_MITIGATION reg);
extern  u_int32_t ar9300_get_key_cache_size(struct ath_hal *);
extern  HAL_BOOL ar9300_is_key_cache_entry_valid(struct ath_hal *, u_int16_t entry);
extern  HAL_BOOL ar9300_reset_key_cache_entry(struct ath_hal *ah, u_int16_t entry);
extern  HAL_CHANNEL_INTERNAL * ar9300_check_chan(struct ath_hal *ah,
         const struct ieee80211_channel *chan);

extern  HAL_BOOL ar9300_set_key_cache_entry_mac(struct ath_hal *,
            u_int16_t entry, const u_int8_t *mac);
extern  HAL_BOOL ar9300_set_key_cache_entry(struct ath_hal *ah, u_int16_t entry,
                       const HAL_KEYVAL *k, const u_int8_t *mac, int xor_key);
extern  HAL_BOOL ar9300_print_keycache(struct ath_hal *ah);
#if ATH_SUPPORT_KEYPLUMB_WAR
extern  HAL_BOOL ar9300_check_key_cache_entry(struct ath_hal *ah, u_int16_t entry,
                        const HAL_KEYVAL *k, int xorKey);
#endif

extern  void ar9300_get_mac_address(struct ath_hal *ah, u_int8_t *mac);
extern  HAL_BOOL ar9300_set_mac_address(struct ath_hal *ah, const u_int8_t *);
extern  void ar9300_get_bss_id_mask(struct ath_hal *ah, u_int8_t *mac);
extern  HAL_BOOL ar9300_set_bss_id_mask(struct ath_hal *, const u_int8_t *);
extern  HAL_STATUS ar9300_select_ant_config(struct ath_hal *ah, u_int32_t cfg);
#if 0
extern  u_int32_t ar9300_ant_ctrl_common_get(struct ath_hal *ah, HAL_BOOL is_2ghz);
#endif
extern HAL_BOOL ar9300_ant_swcom_sel(struct ath_hal *ah, u_int8_t ops,
                                u_int32_t *common_tbl1, u_int32_t *common_tbl2);
extern  HAL_BOOL ar9300_set_regulatory_domain(struct ath_hal *ah,
                                    u_int16_t reg_domain, HAL_STATUS *stats);
extern  u_int ar9300_get_wireless_modes(struct ath_hal *ah);
extern  void ar9300_enable_rf_kill(struct ath_hal *);
extern  HAL_BOOL ar9300_gpio_cfg_output(struct ath_hal *, u_int32_t gpio, HAL_GPIO_MUX_TYPE signalType);
extern  HAL_BOOL ar9300_gpio_cfg_output_led_off(struct ath_hal *, u_int32_t gpio, HAL_GPIO_MUX_TYPE signalType);
extern  HAL_BOOL ar9300_gpio_cfg_input(struct ath_hal *, u_int32_t gpio);
extern  HAL_BOOL ar9300_gpio_set(struct ath_hal *, u_int32_t gpio, u_int32_t val);
extern  u_int32_t ar9300_gpio_get(struct ath_hal *ah, u_int32_t gpio);
extern  u_int32_t ar9300_gpio_get_intr(struct ath_hal *ah);
extern  void ar9300_gpio_set_intr(struct ath_hal *ah, u_int, u_int32_t ilevel);
extern  u_int32_t ar9300_gpio_get_polarity(struct ath_hal *ah);
extern  void ar9300_gpio_set_polarity(struct ath_hal *ah, u_int32_t, u_int32_t);
extern  u_int32_t ar9300_gpio_get_mask(struct ath_hal *ah);
extern  int ar9300_gpio_set_mask(struct ath_hal *ah, u_int32_t mask, u_int32_t pol_map);
extern  void ar9300_set_led_state(struct ath_hal *ah, HAL_LED_STATE state);
extern  void ar9300_set_power_led_state(struct ath_hal *ah, u_int8_t enable);
extern  void ar9300_set_network_led_state(struct ath_hal *ah, u_int8_t enable);
extern  void ar9300_write_associd(struct ath_hal *ah, const u_int8_t *bssid,
        u_int16_t assoc_id);
extern  u_int32_t ar9300_ppm_get_rssi_dump(struct ath_hal *);
extern  u_int32_t ar9300_ppm_arm_trigger(struct ath_hal *);
extern  int ar9300_ppm_get_trigger(struct ath_hal *);
extern  u_int32_t ar9300_ppm_force(struct ath_hal *);
extern  void ar9300_ppm_un_force(struct ath_hal *);
extern  u_int32_t ar9300_ppm_get_force_state(struct ath_hal *);
extern  void ar9300_set_dcs_mode(struct ath_hal *ah, u_int32_t);
extern  u_int32_t ar9300_get_dcs_mode(struct ath_hal *ah);
extern  u_int32_t ar9300_get_tsf32(struct ath_hal *ah);
extern  u_int64_t ar9300_get_tsf64(struct ath_hal *ah);
extern  u_int32_t ar9300_get_tsf2_32(struct ath_hal *ah);
extern  void ar9300_set_tsf64(struct ath_hal *ah, u_int64_t tsf);
extern  void ar9300_reset_tsf(struct ath_hal *ah);
extern  void ar9300_set_basic_rate(struct ath_hal *ah, HAL_RATE_SET *pSet);
extern  u_int32_t ar9300_get_random_seed(struct ath_hal *ah);
extern  HAL_BOOL ar9300_detect_card_present(struct ath_hal *ah);
extern  void ar9300_update_mib_mac_stats(struct ath_hal *ah);
extern  void ar9300_get_mib_mac_stats(struct ath_hal *ah, HAL_MIB_STATS* stats);
extern  HAL_BOOL ar9300_is_japan_channel_spread_supported(struct ath_hal *ah);
extern  u_int32_t ar9300_get_cur_rssi(struct ath_hal *ah);
extern  u_int32_t ar9300_get_rssi_chain0(struct ath_hal *ah);
extern  u_int ar9300_get_def_antenna(struct ath_hal *ah);
extern  void ar9300_set_def_antenna(struct ath_hal *ah, u_int antenna);
extern  HAL_BOOL ar9300_set_antenna_switch(struct ath_hal *ah,
        HAL_ANT_SETTING settings, const struct ieee80211_channel *chan,
        u_int8_t *, u_int8_t *, u_int8_t *);
extern  HAL_BOOL ar9300_is_sleep_after_beacon_broken(struct ath_hal *ah);
extern  HAL_BOOL ar9300_set_slot_time(struct ath_hal *, u_int);
extern  HAL_BOOL ar9300_set_ack_timeout(struct ath_hal *, u_int);
extern  u_int ar9300_get_ack_timeout(struct ath_hal *);
extern  HAL_STATUS ar9300_set_quiet(struct ath_hal *ah, u_int32_t period, u_int32_t duration, 
        u_int32_t next_start, HAL_QUIET_FLAG flag);
extern  void ar9300_set_pcu_config(struct ath_hal *);
extern  HAL_STATUS ar9300_get_capability(struct ath_hal *, HAL_CAPABILITY_TYPE,
        u_int32_t, u_int32_t *);
extern  HAL_BOOL ar9300_set_capability(struct ath_hal *, HAL_CAPABILITY_TYPE,
        u_int32_t, u_int32_t, HAL_STATUS *);
extern  HAL_BOOL ar9300_get_diag_state(struct ath_hal *ah, int request,
        const void *args, u_int32_t argsize,
        void **result, u_int32_t *resultsize);
extern void ar9300_get_desc_info(struct ath_hal *ah, HAL_DESC_INFO *desc_info);
extern  uint32_t ar9300_get_11n_ext_busy(struct ath_hal *ah);
extern  void ar9300_set_11n_mac2040(struct ath_hal *ah, HAL_HT_MACMODE mode);
extern  HAL_HT_RXCLEAR ar9300_get_11n_rx_clear(struct ath_hal *ah);
extern  void ar9300_set_11n_rx_clear(struct ath_hal *ah, HAL_HT_RXCLEAR rxclear);
extern  HAL_BOOL ar9300_set_power_mode(struct ath_hal *ah, HAL_POWER_MODE mode,
        int set_chip);
extern  HAL_POWER_MODE ar9300_get_power_mode(struct ath_hal *ah);
extern HAL_BOOL ar9300_set_power_mode_awake(struct ath_hal *ah, int set_chip);
extern  void ar9300_set_sm_power_mode(struct ath_hal *ah, HAL_SMPS_MODE mode);

extern void ar9300_config_pci_power_save(struct ath_hal *ah, int restore, int power_off);

extern  void ar9300_force_tsf_sync(struct ath_hal *ah, const u_int8_t *bssid, 
                                u_int16_t assoc_id);


#if ATH_WOW
extern  void ar9300_wow_apply_pattern(struct ath_hal *ah, u_int8_t *p_ath_pattern,
        u_int8_t *p_ath_mask, int32_t pattern_count, u_int32_t ath_pattern_len);
//extern  u_int32_t ar9300_wow_wake_up(struct ath_hal *ah,u_int8_t  *chipPatternBytes);
extern  u_int32_t ar9300_wow_wake_up(struct ath_hal *ah, HAL_BOOL offloadEnable);
extern  bool ar9300_wow_enable(struct ath_hal *ah, u_int32_t pattern_enable, u_int32_t timeout_in_seconds, int clearbssid,
                                                                                        HAL_BOOL offloadEnable);
#if ATH_WOW_OFFLOAD
/* ARP offload */
#define WOW_OFFLOAD_ARP_INFO_MAX    2

struct hal_wow_offload_arp_info {
    u_int32_t   valid;
    u_int32_t   id;

    u_int32_t   Flags;
    union {
        u_int8_t    u8[4];
        u_int32_t   u32;
    } RemoteIPv4Address;
    union {
        u_int8_t    u8[4];
        u_int32_t   u32;
    } HostIPv4Address;
    union {
        u_int8_t    u8[6];
        u_int32_t   u32[2];
    } MacAddress;
};

/* NS offload */
#define WOW_OFFLOAD_NS_INFO_MAX    2

struct hal_wow_offload_ns_info {
    u_int32_t   valid;
    u_int32_t   id;

    u_int32_t   Flags;
    union {
        u_int8_t    u8[16];
        u_int32_t   u32[4];
    } RemoteIPv6Address;
    union {
        u_int8_t    u8[16];
        u_int32_t   u32[4];
    } SolicitedNodeIPv6Address;
    union {
        u_int8_t    u8[6];
        u_int32_t   u32[2];
    } MacAddress;
    union {
        u_int8_t    u8[16];
        u_int32_t   u32[4];
    } TargetIPv6Addresses[2];
};

extern  void ar9300_wowoffload_prep(struct ath_hal *ah);
extern  void ar9300_wowoffload_post(struct ath_hal *ah);
extern  u_int32_t ar9300_wowoffload_download_rekey_data(struct ath_hal *ah, u_int32_t *data, u_int32_t size);
extern  void ar9300_wowoffload_retrieve_data(struct ath_hal *ah, void *buf, u_int32_t param);
extern  void ar9300_wowoffload_download_acer_magic(struct ath_hal *ah, HAL_BOOL valid, u_int8_t* datap, u_int32_t bytes);
extern  void ar9300_wowoffload_download_acer_swka(struct ath_hal *ah, u_int32_t id, HAL_BOOL valid, u_int32_t period, u_int32_t size, u_int32_t* datap);
extern  void ar9300_wowoffload_download_arp_info(struct ath_hal *ah, u_int32_t id, u_int32_t *data);
extern  void ar9300_wowoffload_download_ns_info(struct ath_hal *ah, u_int32_t id, u_int32_t *data);
#endif /* ATH_WOW_OFFLOAD */
#endif

extern  HAL_BOOL ar9300_reset(struct ath_hal *ah, HAL_OPMODE opmode,
        struct ieee80211_channel *chan, HAL_HT_MACMODE macmode, u_int8_t txchainmask,
        u_int8_t rxchainmask, HAL_HT_EXTPROTSPACING extprotspacing,
        HAL_BOOL b_channel_change, HAL_STATUS *status, int is_scan);
extern HAL_BOOL ar9300_lean_channel_change(struct ath_hal *ah, HAL_OPMODE opmode, struct ieee80211_channel *chan,
        HAL_HT_MACMODE macmode, u_int8_t txchainmask, u_int8_t rxchainmask);
extern  HAL_BOOL ar9300_set_reset_reg(struct ath_hal *ah, u_int32_t type);
extern  void ar9300_init_pll(struct ath_hal *ah, struct ieee80211_channel *chan);
extern  void ar9300_green_ap_ps_on_off( struct ath_hal *ah, u_int16_t rxMask);
extern  u_int16_t ar9300_is_single_ant_power_save_possible(struct ath_hal *ah);
extern  void ar9300_set_operating_mode(struct ath_hal *ah, int opmode);
extern  HAL_BOOL ar9300_phy_disable(struct ath_hal *ah);
extern  HAL_BOOL ar9300_disable(struct ath_hal *ah);
extern  HAL_BOOL ar9300_chip_reset(struct ath_hal *ah, struct ieee80211_channel *);
extern  HAL_BOOL ar9300_calibration(struct ath_hal *ah,  struct ieee80211_channel *chan,
        u_int8_t rxchainmask, HAL_BOOL longcal, HAL_BOOL *isIQdone, int is_scan, u_int32_t *sched_cals);
extern  void ar9300_reset_cal_valid(struct ath_hal *ah,
          const struct ieee80211_channel *chan,
          HAL_BOOL *isIQdone, u_int32_t cal_type);
extern void ar9300_iq_cal_collect(struct ath_hal *ah, u_int8_t num_chains);
extern void ar9300_iq_calibration(struct ath_hal *ah, u_int8_t num_chains);
extern void ar9300_temp_comp_cal_collect(struct ath_hal *ah);
extern void ar9300_temp_comp_calibration(struct ath_hal *ah, u_int8_t num_chains);
extern int16_t ar9300_get_min_cca_pwr(struct ath_hal *ah);
extern void ar9300_upload_noise_floor(struct ath_hal *ah, int is2G, int16_t nfarray[]);

extern HAL_BOOL ar9300_set_tx_power_limit(struct ath_hal *ah, u_int32_t limit,
                                       u_int16_t extra_txpow, u_int16_t tpc_in_db);
extern void ar9300_chain_noise_floor(struct ath_hal *ah, int16_t *nf_buf,
                                    struct ieee80211_channel *chan, int is_scan);
extern int16_t ar9300_get_nf_from_reg(struct ath_hal *ah, struct ieee80211_channel *chan, int wait_time);
extern int ar9300_get_rx_nf_offset(struct ath_hal *ah, struct ieee80211_channel *chan, int8_t *nf_pwr, int8_t *nf_cal);
extern HAL_BOOL ar9300_load_nf(struct ath_hal *ah, int16_t nf[]);

extern HAL_RFGAIN ar9300_get_rfgain(struct ath_hal *ah);
extern const HAL_RATE_TABLE *ar9300_get_rate_table(struct ath_hal *, u_int mode);
extern int16_t ar9300_get_rate_txpower(struct ath_hal *ah, u_int mode,
                                     u_int8_t rate_index, u_int8_t chainmask, u_int8_t mimo_mode);
extern void ar9300_init_rate_txpower(struct ath_hal *ah, u_int mode,
                                   const struct ieee80211_channel *chan,
                                   u_int8_t powerPerRate[],
                                   u_int8_t chainmask);
extern void ar9300_adjust_reg_txpower_cdd(struct ath_hal *ah, 
                                   u_int8_t powerPerRate[]);
extern HAL_STATUS ath_hal_get_rate_power_limit_from_eeprom(struct ath_hal *ah,
       u_int16_t freq, int8_t *max_rate_power, int8_t *min_rate_power);

extern void ar9300_reset_tx_status_ring(struct ath_hal *ah);
extern  void ar9300_enable_mib_counters(struct ath_hal *);
extern  void ar9300_disable_mib_counters(struct ath_hal *);
extern  void ar9300_ani_attach(struct ath_hal *);
extern  void ar9300_ani_detach(struct ath_hal *);
extern  struct ar9300_ani_state *ar9300_ani_get_current_state(struct ath_hal *);
extern  HAL_ANI_STATS *ar9300_ani_get_current_stats(struct ath_hal *);
extern  HAL_BOOL ar9300_ani_control(struct ath_hal *, HAL_ANI_CMD cmd, int param);
struct ath_rx_status;

extern  void ar9300_process_mib_intr(struct ath_hal *, const HAL_NODE_STATS *);
extern  void ar9300_ani_ar_poll(struct ath_hal *, const HAL_NODE_STATS *,
                 const struct ieee80211_channel *, HAL_ANISTATS *);
extern  void ar9300_ani_reset(struct ath_hal *, HAL_BOOL is_scanning);
extern  void ar9300_ani_init_defaults(struct ath_hal *ah, HAL_HT_MACMODE macmode);
extern  void ar9300_enable_tpc(struct ath_hal *);

extern HAL_BOOL ar9300_rf_gain_cap_apply(struct ath_hal *ah, int is2GHz);
extern void ar9300_rx_gain_table_apply(struct ath_hal *ah);
extern void ar9300_tx_gain_table_apply(struct ath_hal *ah);
extern void ar9300_mat_enable(struct ath_hal *ah, int enable);
extern void ar9300_dump_keycache(struct ath_hal *ah, int n, u_int32_t *entry);
extern HAL_BOOL ar9300_ant_ctrl_set_lna_div_use_bt_ant(struct ath_hal * ah, HAL_BOOL enable, const struct ieee80211_channel * chan);

/* BB Panic Watchdog declarations */
#define HAL_BB_PANIC_WD_TMO                 25 /* in ms, 0 to disable */
#define HAL_BB_PANIC_WD_TMO_HORNET          85
extern void ar9300_config_bb_panic_watchdog(struct ath_hal *);
extern void ar9300_handle_bb_panic(struct ath_hal *);
extern int ar9300_get_bb_panic_info(struct ath_hal *ah, struct hal_bb_panic_info *bb_panic);
extern HAL_BOOL ar9300_handle_radar_bb_panic(struct ath_hal *ah);
extern void ar9300_set_hal_reset_reason(struct ath_hal *ah, u_int8_t resetreason);

/* DFS declarations */
extern  void ar9300_check_dfs(struct ath_hal *ah, struct ieee80211_channel *chan);
extern  void ar9300_dfs_found(struct ath_hal *ah, struct ieee80211_channel *chan,
        u_int64_t nolTime);
extern  void ar9300_enable_dfs(struct ath_hal *ah, HAL_PHYERR_PARAM *pe);
extern  void ar9300_get_dfs_thresh(struct ath_hal *ah, HAL_PHYERR_PARAM *pe);
extern  HAL_BOOL ar9300_radar_wait(struct ath_hal *ah, struct ieee80211_channel *chan);
extern  struct dfs_pulse * ar9300_get_dfs_radars(struct ath_hal *ah,
        u_int32_t dfsdomain, int *numradars, struct dfs_bin5pulse **bin5pulses,
        int *numb5radars, HAL_PHYERR_PARAM *pe);
extern HAL_BOOL ar9300_get_default_dfs_thresh(struct ath_hal *ah, HAL_PHYERR_PARAM *pe);
extern  void ar9300_adjust_difs(struct ath_hal *ah, u_int32_t val);
extern  u_int32_t ar9300_dfs_config_fft(struct ath_hal *ah, HAL_BOOL is_enable);
extern  void ar9300_cac_tx_quiet(struct ath_hal *ah, HAL_BOOL enable);
extern void ar9300_dfs_cac_war(struct ath_hal *ah, u_int32_t start);

extern  struct ieee80211_channel * ar9300_get_extension_channel(struct ath_hal *ah);
extern  HAL_BOOL ar9300_is_fast_clock_enabled(struct ath_hal *ah);


extern  void ar9300_mark_phy_inactive(struct ath_hal *ah);

/* Spectral scan declarations */
extern void ar9300_configure_spectral_scan(struct ath_hal *ah, HAL_SPECTRAL_PARAM *ss);
extern void ar9300_set_cca_threshold(struct ath_hal *ah, u_int8_t thresh62);
extern void ar9300_get_spectral_params(struct ath_hal *ah, HAL_SPECTRAL_PARAM *ss);
extern HAL_BOOL ar9300_is_spectral_active(struct ath_hal *ah);
extern HAL_BOOL ar9300_is_spectral_enabled(struct ath_hal *ah);
extern void ar9300_start_spectral_scan(struct ath_hal *ah);
extern void ar9300_stop_spectral_scan(struct ath_hal *ah);
extern u_int32_t ar9300_get_spectral_config(struct ath_hal *ah);
extern void ar9300_restore_spectral_config(struct ath_hal *ah, u_int32_t restoreval);
int16_t ar9300_get_ctl_chan_nf(struct ath_hal *ah);
int16_t ar9300_get_ext_chan_nf(struct ath_hal *ah);
/* End spectral scan declarations */

/* Raw ADC capture functions */
extern void ar9300_enable_test_addac_mode(struct ath_hal *ah);
extern void ar9300_disable_test_addac_mode(struct ath_hal *ah);
extern void ar9300_begin_adc_capture(struct ath_hal *ah, int auto_agc_gain);
extern HAL_STATUS ar9300_retrieve_capture_data(struct ath_hal *ah, u_int16_t chain_mask, int disable_dc_filter, void *sample_buf, u_int32_t *max_samples);
extern HAL_STATUS ar9300_calc_adc_ref_powers(struct ath_hal *ah, int freq_mhz, int16_t *sample_min, int16_t *sample_max, int32_t *chain_ref_pwr, int num_chain_ref_pwr);
extern HAL_STATUS ar9300_get_min_agc_gain(struct ath_hal *ah, int freq_mhz, int32_t *chain_gain, int num_chain_gain);

extern  HAL_BOOL ar9300_reset_11n(struct ath_hal *ah, HAL_OPMODE opmode,
        struct ieee80211_channel *chan, HAL_BOOL b_channel_change, HAL_STATUS *status);
extern void ar9300_set_coverage_class(struct ath_hal *ah, u_int8_t coverageclass, int now);

extern void ar9300_get_channel_centers(struct ath_hal *ah,
                                    const struct ieee80211_channel *chan,
                                    CHAN_CENTERS *centers);
extern u_int16_t ar9300_get_ctl_center(struct ath_hal *ah,
                                        const struct ieee80211_channel *chan);
extern u_int16_t ar9300_get_ext_center(struct ath_hal *ah,
                                        const struct ieee80211_channel *chan);
extern u_int32_t ar9300_get_mib_cycle_counts_pct(struct ath_hal *, u_int32_t*, u_int32_t*, u_int32_t*);

extern void ar9300_dma_reg_dump(struct ath_hal *);
extern  HAL_BOOL ar9300_set_11n_rx_rifs(struct ath_hal *ah, HAL_BOOL enable);
extern HAL_BOOL ar9300_set_rifs_delay(struct ath_hal *ah, HAL_BOOL enable);
extern HAL_BOOL ar9300_set_smart_antenna(struct ath_hal *ah, HAL_BOOL enable);
extern HAL_BOOL ar9300_detect_bb_hang(struct ath_hal *ah);
extern HAL_BOOL ar9300_detect_mac_hang(struct ath_hal *ah);

#ifdef ATH_BT_COEX
extern void ar9300_set_bt_coex_info(struct ath_hal *ah, HAL_BT_COEX_INFO *btinfo);
extern void ar9300_bt_coex_config(struct ath_hal *ah, HAL_BT_COEX_CONFIG *btconf);
extern void ar9300_bt_coex_set_qcu_thresh(struct ath_hal *ah, int qnum);
extern void ar9300_bt_coex_set_weights(struct ath_hal *ah, u_int32_t stomp_type);
extern void ar9300_bt_coex_setup_bmiss_thresh(struct ath_hal *ah, u_int32_t thresh);
extern void ar9300_bt_coex_set_parameter(struct ath_hal *ah, u_int32_t type, u_int32_t value);
extern void ar9300_bt_coex_disable(struct ath_hal *ah);
extern int ar9300_bt_coex_enable(struct ath_hal *ah);
extern void ar9300_init_bt_coex(struct ath_hal *ah);
extern u_int32_t ar9300_get_bt_active_gpio(struct ath_hal *ah, u_int32_t reg);
extern u_int32_t ar9300_get_wlan_active_gpio(struct ath_hal *ah, u_int32_t reg,u_int32_t bOn);
#endif
extern int ar9300_alloc_generic_timer(struct ath_hal *ah, HAL_GEN_TIMER_DOMAIN tsf);
extern void ar9300_free_generic_timer(struct ath_hal *ah, int index);
extern void ar9300_start_generic_timer(struct ath_hal *ah, int index, u_int32_t timer_next,
                                u_int32_t timer_period);
extern void ar9300_stop_generic_timer(struct ath_hal *ah, int index);
extern void ar9300_get_gen_timer_interrupts(struct ath_hal *ah, u_int32_t *trigger,
                                u_int32_t *thresh);
extern void ar9300_start_tsf2(struct ath_hal *ah);

extern void ar9300_chk_rssi_update_tx_pwr(struct ath_hal *ah, int rssi);
extern HAL_BOOL ar9300_is_skip_paprd_by_greentx(struct ath_hal *ah);
extern void ar9300_control_signals_for_green_tx_mode(struct ath_hal *ah);
extern void ar9300_hwgreentx_set_pal_spare(struct ath_hal *ah, int value);
extern HAL_BOOL ar9300_is_ani_noise_spur(struct ath_hal *ah);
extern void ar9300_reset_hw_beacon_proc_crc(struct ath_hal *ah);
extern int32_t ar9300_get_hw_beacon_rssi(struct ath_hal *ah);
extern void ar9300_set_hw_beacon_rssi_threshold(struct ath_hal *ah,
                                            u_int32_t rssi_threshold);
extern void ar9300_reset_hw_beacon_rssi(struct ath_hal *ah);
extern void ar9300_set_hw_beacon_proc(struct ath_hal *ah, HAL_BOOL on);
extern void ar9300_get_vow_stats(struct ath_hal *ah, HAL_VOWSTATS *p_stats,
                                 u_int8_t);

extern int ar9300_get_spur_info(struct ath_hal * ah, int *enable, int len, u_int16_t *freq);
extern int ar9300_set_spur_info(struct ath_hal * ah, int enable, int len, u_int16_t *freq);
extern void ar9300_wow_set_gpio_reset_low(struct ath_hal * ah);
extern HAL_BOOL ar9300_get_mib_cycle_counts(struct ath_hal *, HAL_SURVEY_SAMPLE *);
extern void ar9300_clear_mib_counters(struct ath_hal *ah);

/* EEPROM interface functions */
/* Common Interface functions */
extern  HAL_STATUS ar9300_eeprom_attach(struct ath_hal *);
extern  u_int32_t ar9300_eeprom_get(struct ath_hal_9300 *ahp, EEPROM_PARAM param);

extern  u_int32_t ar9300_ini_fixup(struct ath_hal *ah,
                                    ar9300_eeprom_t *p_eep_data,
                                    u_int32_t reg,
                                    u_int32_t val);

extern  HAL_STATUS ar9300_eeprom_set_transmit_power(struct ath_hal *ah,
                     ar9300_eeprom_t *p_eep_data, const struct ieee80211_channel *chan,
                     u_int16_t cfg_ctl, u_int16_t twice_antenna_reduction,
                     u_int16_t twice_max_regulatory_power, u_int16_t power_limit);
extern  void ar9300_eeprom_set_addac(struct ath_hal *, struct ieee80211_channel *);
extern  HAL_BOOL ar9300_eeprom_set_param(struct ath_hal *ah, EEPROM_PARAM param, u_int32_t value);
extern  HAL_BOOL ar9300_eeprom_set_board_values(struct ath_hal *, const struct ieee80211_channel *);
extern  HAL_BOOL ar9300_eeprom_read_word(struct ath_hal *, u_int off, u_int16_t *data);
extern  HAL_BOOL ar9300_eeprom_read(struct ath_hal *ah, long address, u_int8_t *buffer, int many);
extern  HAL_BOOL ar9300_otp_read(struct ath_hal *ah, u_int off, u_int32_t *data, HAL_BOOL is_wifi);

extern  HAL_BOOL ar9300_flash_read(struct ath_hal *, u_int off, u_int16_t *data);
extern  HAL_BOOL ar9300_flash_write(struct ath_hal *, u_int off, u_int16_t data);
extern  u_int ar9300_eeprom_dump_support(struct ath_hal *ah, void **pp_e);
extern  u_int8_t ar9300_eeprom_get_num_ant_config(struct ath_hal_9300 *ahp, HAL_FREQ_BAND freq_band);
extern  HAL_STATUS ar9300_eeprom_get_ant_cfg(struct ath_hal_9300 *ahp, const struct ieee80211_channel *chan,
                                     u_int8_t index, u_int16_t *config);
extern u_int8_t* ar9300_eeprom_get_cust_data(struct ath_hal_9300 *ahp);
extern u_int8_t *ar9300_eeprom_get_spur_chans_ptr(struct ath_hal *ah, HAL_BOOL is_2ghz);
extern HAL_BOOL ar9300_interference_is_present(struct ath_hal *ah);
extern HAL_BOOL ar9300_tuning_caps_apply(struct ath_hal *ah);
extern void ar9300_disp_tpc_tables(struct ath_hal *ah);
extern u_int8_t *ar9300_get_tpc_tables(struct ath_hal *ah);
extern u_int8_t ar9300_eeprom_set_tx_gain_cap(struct ath_hal *ah, int *tx_gain_max);
extern u_int8_t ar9300_eeprom_tx_gain_table_index_max_apply(struct ath_hal *ah, u_int16_t channel);

/* Common EEPROM Help function */
extern void ar9300_set_immunity(struct ath_hal *ah, HAL_BOOL enable);
extern void ar9300_get_hw_hangs(struct ath_hal *ah, hal_hw_hangs_t *hangs);

extern u_int ar9300_mac_to_clks(struct ath_hal *ah, u_int clks);

/* tx_bf interface */
#define ar9300_init_txbf(ah)
#define ar9300_set_11n_txbf_sounding(ah, ds, series, cec, opt)
#define ar9300_set_11n_txbf_cal(ah, ds, cal_pos, code_rate, cec, opt)
#define ar9300_txbf_save_cv_from_compress(   \
    ah, key_idx, mimo_control, compress_rpt) \
    false
#define ar9300_txbf_save_cv_from_non_compress(   \
    ah, key_idx, mimo_control, non_compress_rpt) \
    false
#define ar9300_txbf_rc_update(                             \
    ah, rx_status, local_h, csi_frame, ness_a, ness_b, bw) \
    false
#define ar9300_fill_csi_frame(                         \
    ah, rx_status, bandwidth, local_h, csi_frame_body) \
    0
#define ar9300_fill_txbf_capabilities(ah)
#define ar9300_get_txbf_capabilities(ah) NULL
#define ar9300_txbf_set_key( \
    ah, entry, rx_staggered_sounding, channel_estimation_cap, mmss)
#define ar9300_read_key_cache_mac(ah, entry, mac) false
#define ar9300_txbf_get_cv_cache_nr(ah, key_idx, nr)
#define ar9300_set_selfgenrate_limit(ah, ts_ratecode)
#define ar9300_reset_lowest_txrate(ah)
#define ar9300_txbf_set_basic_set(ah)

extern void ar9300_crdc_rx_notify(struct ath_hal *ah, struct ath_rx_status *rxs);
extern void ar9300_chain_rssi_diff_compensation(struct ath_hal *ah);



#if ATH_SUPPORT_MCI
extern void ar9300_mci_bt_coex_set_weights(struct ath_hal *ah, u_int32_t stomp_type);
extern void ar9300_mci_bt_coex_disable(struct ath_hal *ah);
extern int ar9300_mci_bt_coex_enable(struct ath_hal *ah);
extern void ar9300_mci_setup (struct ath_hal *ah, u_int32_t gpm_addr, 
                              void *gpm_buf, u_int16_t len, 
                              u_int32_t sched_addr);
extern void ar9300_mci_remote_reset(struct ath_hal *ah, HAL_BOOL wait_done);
extern void ar9300_mci_send_lna_transfer(struct ath_hal *ah, HAL_BOOL wait_done);
extern void ar9300_mci_send_sys_waking(struct ath_hal *ah, HAL_BOOL wait_done);
extern HAL_BOOL ar9300_mci_send_message (struct ath_hal *ah, u_int8_t header, 
                           u_int32_t flag, u_int32_t *payload, u_int8_t len, 
                           HAL_BOOL wait_done, HAL_BOOL check_bt);
extern u_int32_t ar9300_mci_get_interrupt (struct ath_hal *ah, 
                                           u_int32_t *mci_int, 
                                           u_int32_t *mci_int_rx_msg);
extern u_int32_t ar9300_mci_state (struct ath_hal *ah, u_int32_t state_type, u_int32_t *p_data);
extern void ar9300_mci_reset (struct ath_hal *ah, HAL_BOOL en_int, HAL_BOOL is_2g, HAL_BOOL is_full_sleep);
extern void ar9300_mci_send_coex_halt_bt_gpm(struct ath_hal *ah, HAL_BOOL halt, HAL_BOOL wait_done);
extern void ar9300_mci_mute_bt(struct ath_hal *ah);
extern u_int32_t ar9300_mci_wait_for_gpm(struct ath_hal *ah, u_int8_t gpm_type, u_int8_t gpm_opcode, int32_t time_out);
extern void ar9300_mci_enable_interrupt(struct ath_hal *ah);
extern void ar9300_mci_disable_interrupt(struct ath_hal *ah);
extern void ar9300_mci_detach (struct ath_hal *ah);
extern u_int32_t ar9300_mci_check_int (struct ath_hal *ah, u_int32_t ints);
extern void ar9300_mci_sync_bt_state (struct ath_hal *ah);
extern void ar9300_mci_2g5g_changed(struct ath_hal *ah, HAL_BOOL is_2g);
extern void ar9300_mci_2g5g_switch(struct ath_hal *ah, HAL_BOOL wait_done);
#if ATH_SUPPORT_AIC
extern u_int32_t ar9300_aic_calibration (struct ath_hal *ah);
extern u_int32_t ar9300_aic_start_normal (struct ath_hal *ah);
#endif
#endif

extern HAL_STATUS ar9300_set_proxy_sta(struct ath_hal *ah, HAL_BOOL enable);

extern HAL_BOOL ar9300_regulatory_domain_override(
    struct ath_hal *ah, u_int16_t regdmn);
#if ATH_ANT_DIV_COMB
extern void ar9300_ant_div_comb_get_config(struct ath_hal *ah, HAL_ANT_COMB_CONFIG* div_comb_conf);
extern void ar9300_ant_div_comb_set_config(struct ath_hal *ah, HAL_ANT_COMB_CONFIG* div_comb_conf);
#endif /* ATH_ANT_DIV_COMB */
extern void ar9300_disable_phy_restart(struct ath_hal *ah,
       int disable_phy_restart);
extern void ar9300_enable_keysearch_always(struct ath_hal *ah, int enable);
extern HAL_BOOL ar9300ForceVCS( struct ath_hal *ah);
extern HAL_BOOL ar9300SetDfs3StreamFix(struct ath_hal *ah, u_int32_t val);
extern HAL_BOOL ar9300Get3StreamSignature( struct ath_hal *ah);

#ifdef ATH_TX99_DIAG
#ifndef ATH_SUPPORT_HTC
extern void ar9300_tx99_channel_pwr_update(struct ath_hal *ah, struct ieee80211_channel *c, u_int32_t txpower);
extern void ar9300_tx99_chainmsk_setup(struct ath_hal *ah, int tx_chainmask); 
extern void ar9300_tx99_set_single_carrier(struct ath_hal *ah, int tx_chain_mask, int chtype);
extern void ar9300_tx99_start(struct ath_hal *ah, u_int8_t *data);
extern void ar9300_tx99_stop(struct ath_hal *ah);
#endif /* ATH_SUPPORT_HTC */
#endif /* ATH_TX99_DIAG */
extern HAL_BOOL ar9300_set_ctl_pwr(struct ath_hal *ah, u_int8_t *ctl_array);
extern void ar9300_set_txchainmaskopt(struct ath_hal *ah, u_int8_t mask);

enum {
	AR9300_COEFF_TX_TYPE = 0,
	AR9300_COEFF_RX_TYPE
};

#endif  /* _ATH_AR9300_H_ */
