#ifndef	__AR9300_FREEBSD_INC_H__
#define	__AR9300_FREEBSD_INC_H__

/*
 * Define some configuration entries for the AR9300 HAL, so #if entries
 * don't have to be removed.
 */
#define ATH_DRIVER_SIM          0       /* SIM */
#define ATH_WOW                 0       /* Wake on Wireless */
#define ATH_SUPPORT_MCI         1       /* MCI btcoex */
#define ATH_SUPPORT_AIC         0       /* XXX to do with btcoex? */
#define AH_NEED_TX_DATA_SWAP    0       /* TX descriptor swap? */
#define AH_NEED_RX_DATA_SWAP    0       /* TX descriptor swap? */
#define ATH_SUPPORT_WIRESHARK   0       /* Radiotap HAL code */
#define AH_SUPPORT_WRITE_EEPROM 0       /* EEPROM write support */
#define ATH_SUPPORT_WAPI        0       /* China WAPI support */
#define ATH_ANT_DIV_COMB        1       /* Antenna combining */
#define ATH_SUPPORT_RAW_ADC_CAPTURE     0       /* Raw ADC capture support */
#define ATH_TRAFFIC_FAST_RECOVER        0       /* XXX not sure yet */
#define ATH_SUPPORT_SPECTRAL    1       /* Spectral scan support */
#define ATH_BT_COEX             1       /* Enable BT Coex code */
#define ATH_PCIE_ERROR_MONITOR  0       /* ??? */
#define ATH_SUPPORT_CRDC        0       /* ??? */
#define ATH_LOW_POWER_ENABLE    0       /* ??? */
#define ATH_SUPPORT_VOW_DCS     0       /* Video over wireless dynamic channel select */
#define REMOVE_PKT_LOG          1
#define ATH_VC_MODE_PROXY_STA   0       /* Azimuth + proxysta? */
#define ATH_GEN_RANDOMNESS      0
#define __PKT_SERIOUS_ERRORS__  0
#define HAL_INTR_REFCOUNT_DISABLE       1       /* XXX wha? And atomics in the HAL!? */
#define UMAC_SUPPORT_SMARTANTENNA       0       /* sigh.. */
#define ATH_SMARTANTENNA_DISABLE_JTAG   0
#define ATH_SUPPORT_WIRESHARK           0
#define ATH_SUPPORT_WIFIPOS     0
#define ATH_SUPPORT_PAPRD       1
#define ATH_SUPPORT_TxBF        0
#define AH_PRIVATE_DIAG         1
#define ATH_SUPPORT_KEYPLUMB_WAR 0

/* XXX need to reverify these; they came in with qcamain */
#define ATH_SUPPORT_FAST_CC 0
#define ATH_SUPPORT_RADIO_RETENTION 0
#define ATH_SUPPORT_CAL_REUSE 0

#define ATH_WOW_OFFLOAD 0

#define HAL_NO_INTERSPERSED_READS

/* Required or things will probe/attach, but not work right */
#define	AH_SUPPORT_OSPREY		1
#define	AH_SUPPORT_POSEIDON		1
#define	AH_SUPPORT_AR9300		1

/* These are the embedded boards */
#ifdef	AH_SUPPORT_AR9330
#define AH_SUPPORT_HORNET		1
#endif	/* AH_SUPPORT_AR9330 */
#ifdef	AH_SUPPORT_AR9340
#define AH_SUPPORT_WASP			1
#endif	/* AH_SUPPORT_AR9340 */
#ifdef	AH_SUPPORT_QCA9550
#define AH_SUPPORT_SCORPION             1
#endif	/* AH_SUPPORT_QCA9550 */
#ifdef	AH_SUPPORT_QCA9530
#define	AH_SUPPORT_HONEYBEE		1
#endif	/* AH_SUPPORT_QCA9530 */
#define FIX_NOISE_FLOOR                 1

/* XXX this needs to be removed! No atomics in the HAL! */
typedef int os_atomic_t;                /* XXX shouldn't do atomics here! */
#define OS_ATOMIC_INC(a)        (*a)++
#define OS_ATOMIC_DEC(a)        (*a)--

/*
 * HAL definitions which aren't necessarily for public consumption (yet).
 */

enum {
	HAL_TRUE_CHIP = 1,
	HAL_MAC_TO_MAC_EMU,
	HAL_MAC_BB_EMU,
};

/* HAL_KEY_TYPE */
enum {
	HAL_KEY_PROXY_STA_MASK = 0x10,
};

typedef enum {
	HAL_SMPS_DEFAULT = 0,
	HAL_SMPS_SW_CTRL_LOW_PWR,       /* Software control, low power setting */
	HAL_SMPS_SW_CTRL_HIGH_PWR,      /* Software control, high power setting */
	HAL_SMPS_HW_CTRL                /* Hardware Control */
} HAL_SMPS_MODE;

/*
 * Green Tx, Based on different RSSI of Received Beacon thresholds,
 * using different tx power by modified register tx power related values.
 * The thresholds are decided by system team.
 */
#define	GreenTX_thres1	56	/* in dB */
#define	GreenTX_thres2	36	/* in dB */

typedef enum {
	HAL_RSSI_TX_POWER_NONE		= 0,
	HAL_RSSI_TX_POWER_SHORT		= 1,	/* short range, reduce OB/DB bias current and disable PAL */
	HAL_RSSI_TX_POWER_MIDDLE	= 2,	/* middle range, reduce OB/DB bias current and PAL is enabled */
	HAL_RSSI_TX_POWER_LONG		= 3,	/* long range, orig. OB/DB bias current and PAL is enabled */
} HAL_RSSI_TX_POWER;

struct  dfs_pulse {
	u_int32_t	rp_numpulses    ;       /* Num of pulses in radar burst */
	u_int32_t	rp_pulsedur;            /* Duration of each pulse in usecs */
	u_int32_t	rp_pulsefreq;           /* Frequency of pulses in burst */
	u_int32_t	rp_max_pulsefreq;       /* Frequency of pulses in burst */
	u_int32_t	rp_patterntype;         /* fixed or variable pattern type*/
	u_int32_t	rp_pulsevar;            /* Time variation of pulse duration for
							  matched filter (single-sided) in usecs */
	u_int32_t	rp_threshold;           /* Threshold for MF output to indicate
							  radar match */
	u_int32_t	rp_mindur;              /* Min pulse duration to be considered for
							  this pulse type */
	u_int32_t	rp_maxdur;              /* Max pusle duration to be considered for
							  this pulse type */
	u_int32_t	rp_rssithresh;          /* Minimum rssi to be considered a radar pulse */
	u_int32_t	rp_meanoffset;          /* Offset for timing adjustment */
	int32_t		rp_rssimargin;          /* rssi threshold margin. In Turbo Mode HW reports rssi 3dBm */
						       /* lower than in non TURBO mode.
							  This will be used to offset that diff.*/
	u_int32_t	rp_ignore_pri_window;
	u_int32_t	rp_pulseid;             /* Unique ID for identifying filter */
};

struct  dfs_staggered_pulse {
       u_int32_t       rp_numpulses;           /* Num of pulses in radar burst */
       u_int32_t       rp_pulsedur;            /* Duration of each pulse in usecs */
       u_int32_t       rp_min_pulsefreq;       /* Frequency of pulses in burst */
       u_int32_t       rp_max_pulsefreq;       /* Frequency of pulses in burst */
       u_int32_t       rp_patterntype;         /* fixed or variable pattern type*/
       u_int32_t       rp_pulsevar;            /* Time variation of pulse duration for
                                                   matched filter (single-sided) in usecs */
       u_int32_t       rp_threshold;           /* Thershold for MF output to indicateC
                                                  radar match */
       u_int32_t       rp_mindur;              /* Min pulse duration to be considered for
                                                  this pulse type */
       u_int32_t       rp_maxdur;              /* Max pusle duration to be considered for
                                                  this pulse type */
       u_int32_t       rp_rssithresh;          /* Minimum rssi to be considered a radar pulse */
       u_int32_t       rp_meanoffset;          /* Offset for timing adjustment */
       int32_t         rp_rssimargin;          /* rssi threshold margin. In Turbo Mode HW reports rssi 3dBm */
                                               /* lower than in non TURBO mode. This will be used to offset that diff.*/
       u_int32_t       rp_pulseid;             /* Unique ID for identifying filter */
       };

struct dfs_bin5pulse {
        u_int32_t       b5_threshold;          /* Number of bin5 pulses to indicate detection */
        u_int32_t       b5_mindur;             /* Min duration for a bin5 pulse */
        u_int32_t       b5_maxdur;             /* Max duration for a bin5 pulse */
        u_int32_t       b5_timewindow;         /* Window over which to count bin5 pulses */
        u_int32_t       b5_rssithresh;         /* Min rssi to be considered a pulse */
        u_int32_t       b5_rssimargin;         /* rssi threshold margin. In Turbo Mode HW reports rssi 3dB */
};

/*
 * Noise power data definitions
 * units are: 4 x dBm - NOISE_PWR_DATA_OFFSET (e.g. -25 = (-25/4 - 90) = -96.25 dBm)
 * range (for 6 signed bits) is (-32 to 31) + offset => -122dBm to -59dBm
 * resolution (2 bits) is 0.25dBm
 */
#define NOISE_PWR_DATA_OFFSET           -90 /* dbm - all pwr report data is represented offset by this */
#define INT_2_NOISE_PWR_DBM(_p)         (((_p) - NOISE_PWR_DATA_OFFSET) << 2)
#define NOISE_PWR_DBM_2_INT(_p)         ((((_p) + 3) >> 2) + NOISE_PWR_DATA_OFFSET)
#define NOISE_PWR_DBM_2_DEC(_p)         (((-(_p)) & 3) * 25)
#define N2DBM(_x,_y)                    ((((_x) - NOISE_PWR_DATA_OFFSET) << 2) - (_y)/25)
/* SPECTRAL SCAN defines end */

typedef struct halvowstats {
    u_int32_t   tx_frame_count;
    u_int32_t   rx_frame_count;
    u_int32_t   rx_clear_count;
    u_int32_t   cycle_count;
    u_int32_t   ext_cycle_count;
} HAL_VOWSTATS;

/*
 * Weight table configurations.
 */
#define AR9300_BT_WGHT                     0xcccc4444
#define AR9300_STOMP_ALL_WLAN_WGHT0        0xfffffff0
#define AR9300_STOMP_ALL_WLAN_WGHT1        0xfffffff0
#define AR9300_STOMP_LOW_WLAN_WGHT0        0x88888880
#define AR9300_STOMP_LOW_WLAN_WGHT1        0x88888880
#define AR9300_STOMP_NONE_WLAN_WGHT0       0x00000000
#define AR9300_STOMP_NONE_WLAN_WGHT1       0x00000000
#define AR9300_STOMP_ALL_FORCE_WLAN_WGHT0  0xffffffff   // Stomp BT even when WLAN is idle
#define AR9300_STOMP_ALL_FORCE_WLAN_WGHT1  0xffffffff
#define AR9300_STOMP_LOW_FORCE_WLAN_WGHT0  0x88888888   // Stomp BT even when WLAN is idle
#define AR9300_STOMP_LOW_FORCE_WLAN_WGHT1  0x88888888

#define JUPITER_STOMP_ALL_WLAN_WGHT0       0x01017d01
#define JUPITER_STOMP_ALL_WLAN_WGHT1       0x41414101
#define JUPITER_STOMP_ALL_WLAN_WGHT2       0x41414101
#define JUPITER_STOMP_ALL_WLAN_WGHT3       0x41414141
#define JUPITER_STOMP_LOW_WLAN_WGHT0       0x01017d01
#define JUPITER_STOMP_LOW_WLAN_WGHT1       0x3b3b3b01
#define JUPITER_STOMP_LOW_WLAN_WGHT2       0x3b3b3b01
#define JUPITER_STOMP_LOW_WLAN_WGHT3       0x3b3b3b3b
#define JUPITER_STOMP_LOW_FTP_WLAN_WGHT0   0x01017d01
#define JUPITER_STOMP_LOW_FTP_WLAN_WGHT1   0x013b0101
#define JUPITER_STOMP_LOW_FTP_WLAN_WGHT2   0x3b3b0101
#define JUPITER_STOMP_LOW_FTP_WLAN_WGHT3   0x3b3b013b
#define JUPITER_STOMP_NONE_WLAN_WGHT0      0x01017d01
#define JUPITER_STOMP_NONE_WLAN_WGHT1      0x01010101
#define JUPITER_STOMP_NONE_WLAN_WGHT2      0x01010101
#define JUPITER_STOMP_NONE_WLAN_WGHT3      0x01010101
#define JUPITER_STOMP_ALL_FORCE_WLAN_WGHT0 0x01017d7d
#define JUPITER_STOMP_ALL_FORCE_WLAN_WGHT1 0x7d7d7d01
#define JUPITER_STOMP_ALL_FORCE_WLAN_WGHT2 0x7d7d7d7d
#define JUPITER_STOMP_ALL_FORCE_WLAN_WGHT3 0x7d7d7d7d
#define JUPITER_STOMP_LOW_FORCE_WLAN_WGHT0 0x01013b3b
#define JUPITER_STOMP_LOW_FORCE_WLAN_WGHT1 0x3b3b3b01
#define JUPITER_STOMP_LOW_FORCE_WLAN_WGHT2 0x3b3b3b3b
#define JUPITER_STOMP_LOW_FORCE_WLAN_WGHT3 0x3b3b3b3b

#define MCI_CONCUR_TX_WLAN_WGHT1_MASK      0xff000000
#define MCI_CONCUR_TX_WLAN_WGHT1_MASK_S    24
#define MCI_CONCUR_TX_WLAN_WGHT2_MASK      0x00ff0000
#define MCI_CONCUR_TX_WLAN_WGHT2_MASK_S    16
#define MCI_CONCUR_TX_WLAN_WGHT3_MASK      0x000000ff
#define MCI_CONCUR_TX_WLAN_WGHT3_MASK_S    0
#define MCI_CONCUR_TX_WLAN_WGHT3_MASK2     0x00ff0000
#define MCI_CONCUR_TX_WLAN_WGHT3_MASK2_S   16

#define MCI_QUERY_BT_VERSION_VERBOSE            0
#define MCI_LINKID_INDEX_MGMT_PENDING           1

#define HAL_MCI_FLAG_DISABLE_TIMESTAMP      0x00000001      /* Disable time stamp */

/* 
 * The values below come from the system team test result.
 * For Jupiter, BT tx power level is from 0(-20dBm) to 6(4dBm).
 * Lowest WLAN tx power would be in bit[23:16] of dword 1.
 */
static const u_int32_t mci_concur_tx_max_pwr[4][8] =
    { /* No limit */
      {0x7f7f7f7f, 0x7f7f7f7f, 0x7f7f7f7f, 0x7f7f7f7f,
       0x7f7f7f7f, 0x7f7f7f7f, 0x7f7f7f7f, 0x7f7f7f7f},
      /* 11G */
      {0x16161616, 0x12121516, 0x12121212, 0x12121212,
       0x12121212, 0x12121212, 0x12121212, 0x7f121212},
      /* HT20 */
      {0x15151515, 0x14141515, 0x14141414, 0x14141414,
       0x14141414, 0x14141414, 0x14141414, 0x7f141414},
      /* HT40 */
      {0x10101010, 0x10101010, 0x10101010, 0x10101010,
       0x10101010, 0x10101010, 0x10101010, 0x7f101010}};
#define ATH_MCI_CONCUR_TX_LOWEST_PWR_MASK     0x00ff0000
#define ATH_MCI_CONCUR_TX_LOWEST_PWR_MASK_S   16

#endif	/* __AR9300_FREEBSD_INC_H__ */
