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

#ifndef _ATH_AR9300_EEP_H_
#define _ATH_AR9300_EEP_H_

#include "opt_ah.h"

#include "ah.h"

#if defined(WIN32) || defined(WIN64)
#pragma pack (push, ar9300, 1)
#endif

/* FreeBSD extras - should be in ah_eeprom.h ? */
#define AR_EEPROM_EEPCAP_COMPRESS_DIS   0x0001
#define AR_EEPROM_EEPCAP_AES_DIS        0x0002
#define AR_EEPROM_EEPCAP_FASTFRAME_DIS  0x0004
#define AR_EEPROM_EEPCAP_BURST_DIS      0x0008
#define AR_EEPROM_EEPCAP_MAXQCU         0x01F0
#define AR_EEPROM_EEPCAP_MAXQCU_S       4
#define AR_EEPROM_EEPCAP_HEAVY_CLIP_EN  0x0200
#define AR_EEPROM_EEPCAP_KC_ENTRIES     0xF000
#define AR_EEPROM_EEPCAP_KC_ENTRIES_S   12


#define MSTATE 100
#define MOUTPUT 2048
#define MDEFAULT 15
#define MVALUE 100

enum CompressAlgorithm
{
    _compress_none = 0,
    _compress_lzma,
    _compress_pairs,
    _compress_block,
    _compress4,
    _compress5,
    _compress6,
    _compress7,
};


enum
{
	calibration_data_none = 0,
	calibration_data_dram,
	calibration_data_flash,
	calibration_data_eeprom,
	calibration_data_otp,
#ifdef ATH_CAL_NAND_FLASH
	calibration_data_nand,
#endif
	CalibrationDataDontLoad,
};
#define HOST_CALDATA_SIZE (16*1024)

//
// DO NOT CHANGE THE DEFINTIONS OF THESE SYMBOLS.
// Add additional definitions to the end.
// Yes, the first one is 2. Do not use 0 or 1.
//
enum Ar9300EepromTemplate
{
	ar9300_eeprom_template_generic        = 2,
	ar9300_eeprom_template_hb112          = 3,
	ar9300_eeprom_template_hb116          = 4,
	ar9300_eeprom_template_xb112          = 5,
	ar9300_eeprom_template_xb113          = 6,
	ar9300_eeprom_template_xb114          = 7,
	ar9300_eeprom_template_tb417          = 8,
	ar9300_eeprom_template_ap111          = 9,
	ar9300_eeprom_template_ap121          = 10,
	ar9300_eeprom_template_hornet_generic = 11,
    ar9300_eeprom_template_wasp_2         = 12,
    ar9300_eeprom_template_wasp_k31       = 13,
    ar9300_eeprom_template_osprey_k31     = 14,
    ar9300_eeprom_template_aphrodite      = 15
};

#define ar9300_eeprom_template_default ar9300_eeprom_template_generic
#define Ar9300EepromFormatDefault 2

#define reference_current 0
#define compression_header_length 4
#define compression_checksum_length 2

#define OSPREY_EEP_VER               0xD000
#define OSPREY_EEP_VER_MINOR_MASK    0xFFF
#define OSPREY_EEP_MINOR_VER_1       0x1
#define OSPREY_EEP_MINOR_VER         OSPREY_EEP_MINOR_VER_1

// 16-bit offset location start of calibration struct
#define OSPREY_EEP_START_LOC         256
#define OSPREY_NUM_5G_CAL_PIERS      8
#define OSPREY_NUM_2G_CAL_PIERS      3
#define OSPREY_NUM_5G_20_TARGET_POWERS  8
#define OSPREY_NUM_5G_40_TARGET_POWERS  8
#define OSPREY_NUM_2G_CCK_TARGET_POWERS 2
#define OSPREY_NUM_2G_20_TARGET_POWERS  3
#define OSPREY_NUM_2G_40_TARGET_POWERS  3
//#define OSPREY_NUM_CTLS              21
#define OSPREY_NUM_CTLS_5G           9
#define OSPREY_NUM_CTLS_2G           12
#define OSPREY_CTL_MODE_M            0xF
#define OSPREY_NUM_BAND_EDGES_5G     8
#define OSPREY_NUM_BAND_EDGES_2G     4
#define OSPREY_NUM_PD_GAINS          4
#define OSPREY_PD_GAINS_IN_MASK      4
#define OSPREY_PD_GAIN_ICEPTS        5
#define OSPREY_EEPROM_MODAL_SPURS    5
#define OSPREY_MAX_RATE_POWER        63
#define OSPREY_NUM_PDADC_VALUES      128
#define OSPREY_NUM_RATES             16
#define OSPREY_BCHAN_UNUSED          0xFF
#define OSPREY_MAX_PWR_RANGE_IN_HALF_DB 64
#define OSPREY_OPFLAGS_11A           0x01
#define OSPREY_OPFLAGS_11G           0x02
#define OSPREY_OPFLAGS_5G_HT40       0x04
#define OSPREY_OPFLAGS_2G_HT40       0x08
#define OSPREY_OPFLAGS_5G_HT20       0x10
#define OSPREY_OPFLAGS_2G_HT20       0x20
#define OSPREY_EEPMISC_BIG_ENDIAN    0x01
#define OSPREY_EEPMISC_WOW           0x02
#define OSPREY_CUSTOMER_DATA_SIZE    20

#define FREQ2FBIN(x,y) \
    (u_int8_t)(((y) == HAL_FREQ_BAND_2GHZ) ? ((x) - 2300) : (((x) - 4800) / 5))
#define FBIN2FREQ(x,y) \
    (((y) == HAL_FREQ_BAND_2GHZ) ? (2300 + x) : (4800 + 5 * x))
#define OSPREY_MAX_CHAINS            3
#define OSPREY_ANT_16S               25
#define OSPREY_FUTURE_MODAL_SZ       6

#define OSPREY_NUM_ANT_CHAIN_FIELDS     7
#define OSPREY_NUM_ANT_COMMON_FIELDS    4
#define OSPREY_SIZE_ANT_CHAIN_FIELD     3
#define OSPREY_SIZE_ANT_COMMON_FIELD    4
#define OSPREY_ANT_CHAIN_MASK           0x7
#define OSPREY_ANT_COMMON_MASK          0xf
#define OSPREY_CHAIN_0_IDX              0
#define OSPREY_CHAIN_1_IDX              1
#define OSPREY_CHAIN_2_IDX              2
#define OSPREY_1_CHAINMASK              1
#define OSPREY_2LOHI_CHAINMASK          5
#define OSPREY_2LOMID_CHAINMASK         3
#define OSPREY_3_CHAINMASK              7

#define AR928X_NUM_ANT_CHAIN_FIELDS     6
#define AR928X_SIZE_ANT_CHAIN_FIELD     2
#define AR928X_ANT_CHAIN_MASK           0x3

/* Delta from which to start power to pdadc table */
/* This offset is used in both open loop and closed loop power control
 * schemes. In open loop power control, it is not really needed, but for
 * the "sake of consistency" it was kept.
 * For certain AP designs, this value is overwritten by the value in the flag
 * "pwrTableOffset" just before writing the pdadc vs pwr into the chip registers.
 */
#define OSPREY_PWR_TABLE_OFFSET  0

//enable flags for voltage and temp compensation
#define ENABLE_TEMP_COMPENSATION 0x01
#define ENABLE_VOLT_COMPENSATION 0x02

#define FLASH_BASE_CALDATA_OFFSET  0x1000
#define AR9300_EEPROM_SIZE 16*1024  // byte addressable
#define FIXED_CCA_THRESHOLD 15

typedef struct eepFlags {
    u_int8_t  op_flags;
    u_int8_t  eepMisc;
} __packed EEP_FLAGS;

typedef enum targetPowerHTRates {
    HT_TARGET_RATE_0_8_16,
    HT_TARGET_RATE_1_3_9_11_17_19,
    HT_TARGET_RATE_4,
    HT_TARGET_RATE_5,
    HT_TARGET_RATE_6,
    HT_TARGET_RATE_7,
    HT_TARGET_RATE_12,
    HT_TARGET_RATE_13,
    HT_TARGET_RATE_14,
    HT_TARGET_RATE_15,
    HT_TARGET_RATE_20,
    HT_TARGET_RATE_21,
    HT_TARGET_RATE_22,
    HT_TARGET_RATE_23
}TARGET_POWER_HT_RATES;

const static int mapRate2Index[24]=
{
    0,1,1,1,2,
    3,4,5,0,1,
    1,1,6,7,8,
    9,0,1,1,1,
    10,11,12,13
};

typedef enum targetPowerLegacyRates {
    LEGACY_TARGET_RATE_6_24,
    LEGACY_TARGET_RATE_36,
    LEGACY_TARGET_RATE_48,
    LEGACY_TARGET_RATE_54
}TARGET_POWER_LEGACY_RATES;

typedef enum targetPowerCckRates {
    LEGACY_TARGET_RATE_1L_5L,
    LEGACY_TARGET_RATE_5S,
    LEGACY_TARGET_RATE_11L,
    LEGACY_TARGET_RATE_11S
}TARGET_POWER_CCK_RATES;

#define MAX_MODAL_RESERVED 11
#define MAX_MODAL_FUTURE 5
#define MAX_BASE_EXTENSION_FUTURE 2
#define MAX_TEMP_SLOPE 8
#define OSPREY_CHECKSUM_LOCATION (OSPREY_EEP_START_LOC + 1)

typedef struct osprey_BaseEepHeader {
    u_int16_t  reg_dmn[2]; //Does this need to be outside of this structure, if it gets written after calibration
    u_int8_t   txrx_mask;  //4 bits tx and 4 bits rx
    EEP_FLAGS  op_cap_flags;
    u_int8_t   rf_silent;
    u_int8_t   blue_tooth_options;
    u_int8_t   device_cap;
    u_int8_t   device_type; // takes lower byte in eeprom location
    int8_t     pwrTableOffset; // offset in dB to be added to beginning of pdadc table in calibration
	u_int8_t   params_for_tuning_caps[2];  //placeholder, get more details from Don
    u_int8_t   feature_enable; //bit0 - enable tx temp comp 
                             //bit1 - enable tx volt comp
                             //bit2 - enable fastClock - default to 1
                             //bit3 - enable doubling - default to 1
														 //bit4 - enable internal regulator - default to 1
														 //bit5 - enable paprd - default to 0
														 //bit6 - enable TuningCaps - default to 0
														 //bit7 - enable tx_frame_to_xpa_on - default to 0
    u_int8_t   misc_configuration; //misc flags: bit0 - turn down drivestrength
									// bit 1:2 - 0=don't force, 1=force to thermometer 0, 2=force to thermometer 1, 3=force to thermometer 2
									// bit 3 - reduce chain mask from 0x7 to 0x3 on 2 stream rates 
									// bit 4 - enable quick drop
									// bit 5 - enable 8 temp slop
									// bit 6;	enable xLNA_bias_strength
									// bit 7;	enable rf_gain_cap
	u_int8_t   eeprom_write_enable_gpio;
	u_int8_t   wlan_disable_gpio;
	u_int8_t   wlan_led_gpio;
	u_int8_t   rx_band_select_gpio;
	u_int8_t   txrxgain;
	u_int32_t   swreg;    // SW controlled internal regulator fields
} __packed OSPREY_BASE_EEP_HEADER;

typedef struct osprey_BaseExtension_1 {
	u_int8_t  ant_div_control;
	u_int8_t  future[MAX_BASE_EXTENSION_FUTURE];
	u_int8_t  misc_enable;
	int8_t  tempslopextension[MAX_TEMP_SLOPE];
    int8_t  quick_drop_low;           
    int8_t  quick_drop_high;           
} __packed OSPREY_BASE_EXTENSION_1;

typedef struct osprey_BaseExtension_2 {
	int8_t    temp_slope_low;
	int8_t    temp_slope_high;
    u_int8_t   xatten1_db_low[OSPREY_MAX_CHAINS];           // 3  //xatten1_db for merlin (0xa20c/b20c 5:0)
    u_int8_t   xatten1_margin_low[OSPREY_MAX_CHAINS];          // 3  //xatten1_margin for merlin (0xa20c/b20c 16:12
    u_int8_t   xatten1_db_high[OSPREY_MAX_CHAINS];           // 3  //xatten1_db for merlin (0xa20c/b20c 5:0)
    u_int8_t   xatten1_margin_high[OSPREY_MAX_CHAINS];          // 3  //xatten1_margin for merlin (0xa20c/b20c 16:12
} __packed OSPREY_BASE_EXTENSION_2;

typedef struct spurChanStruct {
    u_int16_t spur_chan;
    u_int8_t  spurRangeLow;
    u_int8_t  spurRangeHigh;
} __packed SPUR_CHAN;

//Note the order of the fields in this structure has been optimized to put all fields likely to change together
typedef struct ospreyModalEepHeader {
    u_int32_t  ant_ctrl_common;                         // 4   idle, t1, t2, b (4 bits per setting)
    u_int32_t  ant_ctrl_common2;                        // 4    ra1l1, ra2l1, ra1l2, ra2l2, ra12
    u_int16_t  ant_ctrl_chain[OSPREY_MAX_CHAINS];       // 6   idle, t, r, rx1, rx12, b (2 bits each)
    u_int8_t   xatten1_db[OSPREY_MAX_CHAINS];           // 3  //xatten1_db for merlin (0xa20c/b20c 5:0)
    u_int8_t   xatten1_margin[OSPREY_MAX_CHAINS];       // 3  //xatten1_margin for merlin (0xa20c/b20c 16:12
    int8_t     temp_slope;
    int8_t     voltSlope;
    u_int8_t   spur_chans[OSPREY_EEPROM_MODAL_SPURS];   // spur channels in usual fbin coding format
    int8_t     noise_floor_thresh_ch[OSPREY_MAX_CHAINS];// 3    //Check if the register is per chain
    u_int8_t   reserved[MAX_MODAL_RESERVED];
    int8_t     quick_drop;
    u_int8_t   xpa_bias_lvl;                            // 1
    u_int8_t   tx_frame_to_data_start;                  // 1
    u_int8_t   tx_frame_to_pa_on;                       // 1
    u_int8_t   txClip;                                  // 4 bits tx_clip, 4 bits dac_scale_cck
    int8_t     antenna_gain;                            // 1
    u_int8_t   switchSettling;                          // 1
    int8_t     adcDesiredSize;                          // 1
    u_int8_t   tx_end_to_xpa_off;                       // 1
    u_int8_t   txEndToRxOn;                             // 1
    u_int8_t   tx_frame_to_xpa_on;                      // 1
    u_int8_t   thresh62;                                // 1
    u_int32_t  paprd_rate_mask_ht20;
    u_int32_t  paprd_rate_mask_ht40;
    u_int16_t  switchcomspdt;
    u_int8_t   xLNA_bias_strength;                      // bit: 0,1:chain0, 2,3:chain1, 4,5:chain2
    u_int8_t   rf_gain_cap;
    u_int8_t   tx_gain_cap;                             // bit0:4 txgain cap, txgain index for max_txgain + 20 (10dBm higher than max txgain)
    u_int8_t   futureModal[MAX_MODAL_FUTURE];
    // last 12 bytes stolen and moved to newly created base extension structure
} __packed OSPREY_MODAL_EEP_HEADER;                    // == 100 B

typedef struct ospCalDataPerFreqOpLoop {
    int8_t ref_power;    /*   */
    u_int8_t volt_meas; /* pdadc voltage at power measurement */
    u_int8_t temp_meas;  /* pcdac used for power measurement   */
    int8_t rx_noisefloor_cal; /*range is -60 to -127 create a mapping equation 1db resolution */
    int8_t rx_noisefloor_power; /*range is same as noisefloor */
    u_int8_t rxTempMeas; /*temp measured when noisefloor cal was performed */
} __packed OSP_CAL_DATA_PER_FREQ_OP_LOOP;

typedef struct CalTargetPowerLegacy {
    u_int8_t  t_pow2x[4];
} __packed CAL_TARGET_POWER_LEG;

typedef struct ospCalTargetPowerHt {
    u_int8_t  t_pow2x[14];
} __packed OSP_CAL_TARGET_POWER_HT;

#if AH_BYTE_ORDER == AH_BIG_ENDIAN
typedef struct CalCtlEdgePwr {
    u_int8_t  flag  :2,
              t_power :6;
} __packed CAL_CTL_EDGE_PWR;
#else
typedef struct CalCtlEdgePwr {
    u_int8_t  t_power :6,
             flag   :2;
} __packed CAL_CTL_EDGE_PWR;
#endif

typedef struct ospCalCtlData_5G {
    CAL_CTL_EDGE_PWR  ctl_edges[OSPREY_NUM_BAND_EDGES_5G];
} __packed OSP_CAL_CTL_DATA_5G;

typedef struct ospCalCtlData_2G {
    CAL_CTL_EDGE_PWR  ctl_edges[OSPREY_NUM_BAND_EDGES_2G];
} __packed OSP_CAL_CTL_DATA_2G;

typedef struct ospreyEeprom {
    u_int8_t  eeprom_version;
    u_int8_t  template_version;
    u_int8_t  mac_addr[6];
    u_int8_t  custData[OSPREY_CUSTOMER_DATA_SIZE];

    OSPREY_BASE_EEP_HEADER    base_eep_header;

    OSPREY_MODAL_EEP_HEADER   modal_header_2g;
	OSPREY_BASE_EXTENSION_1 base_ext1;
	u_int8_t            cal_freq_pier_2g[OSPREY_NUM_2G_CAL_PIERS];
    OSP_CAL_DATA_PER_FREQ_OP_LOOP cal_pier_data_2g[OSPREY_MAX_CHAINS][OSPREY_NUM_2G_CAL_PIERS];
	u_int8_t cal_target_freqbin_cck[OSPREY_NUM_2G_CCK_TARGET_POWERS];
    u_int8_t cal_target_freqbin_2g[OSPREY_NUM_2G_20_TARGET_POWERS];
    u_int8_t cal_target_freqbin_2g_ht20[OSPREY_NUM_2G_20_TARGET_POWERS];
    u_int8_t cal_target_freqbin_2g_ht40[OSPREY_NUM_2G_40_TARGET_POWERS];
    CAL_TARGET_POWER_LEG cal_target_power_cck[OSPREY_NUM_2G_CCK_TARGET_POWERS];
    CAL_TARGET_POWER_LEG cal_target_power_2g[OSPREY_NUM_2G_20_TARGET_POWERS];
    OSP_CAL_TARGET_POWER_HT  cal_target_power_2g_ht20[OSPREY_NUM_2G_20_TARGET_POWERS];
    OSP_CAL_TARGET_POWER_HT  cal_target_power_2g_ht40[OSPREY_NUM_2G_40_TARGET_POWERS];
    u_int8_t   ctl_index_2g[OSPREY_NUM_CTLS_2G];
    u_int8_t   ctl_freqbin_2G[OSPREY_NUM_CTLS_2G][OSPREY_NUM_BAND_EDGES_2G];
    OSP_CAL_CTL_DATA_2G   ctl_power_data_2g[OSPREY_NUM_CTLS_2G];

    OSPREY_MODAL_EEP_HEADER   modal_header_5g;
	OSPREY_BASE_EXTENSION_2 base_ext2;
    u_int8_t            cal_freq_pier_5g[OSPREY_NUM_5G_CAL_PIERS];
    OSP_CAL_DATA_PER_FREQ_OP_LOOP cal_pier_data_5g[OSPREY_MAX_CHAINS][OSPREY_NUM_5G_CAL_PIERS];
    u_int8_t cal_target_freqbin_5g[OSPREY_NUM_5G_20_TARGET_POWERS];
    u_int8_t cal_target_freqbin_5g_ht20[OSPREY_NUM_5G_20_TARGET_POWERS];
    u_int8_t cal_target_freqbin_5g_ht40[OSPREY_NUM_5G_40_TARGET_POWERS];
    CAL_TARGET_POWER_LEG cal_target_power_5g[OSPREY_NUM_5G_20_TARGET_POWERS];
    OSP_CAL_TARGET_POWER_HT  cal_target_power_5g_ht20[OSPREY_NUM_5G_20_TARGET_POWERS];
    OSP_CAL_TARGET_POWER_HT  cal_target_power_5g_ht40[OSPREY_NUM_5G_40_TARGET_POWERS];
    u_int8_t   ctl_index_5g[OSPREY_NUM_CTLS_5G];
    u_int8_t   ctl_freqbin_5G[OSPREY_NUM_CTLS_5G][OSPREY_NUM_BAND_EDGES_5G];
    OSP_CAL_CTL_DATA_5G   ctl_power_data_5g[OSPREY_NUM_CTLS_5G];
} __packed ar9300_eeprom_t;


/*
** SWAP Functions
** used to read EEPROM data, which is apparently stored in little
** endian form.  We have included both forms of the swap functions,
** one for big endian and one for little endian.  The indices of the
** array elements are the differences
*/
#if AH_BYTE_ORDER == AH_BIG_ENDIAN

#define AR9300_EEPROM_MAGIC         0x5aa5
#define SWAP16(_x) ( (u_int16_t)( (((const u_int8_t *)(&_x))[0] ) |\
                     ( ( (const u_int8_t *)( &_x ) )[1]<< 8) ) )

#define SWAP32(_x) ((u_int32_t)(                       \
                    (((const u_int8_t *)(&_x))[0]) |        \
                    (((const u_int8_t *)(&_x))[1]<< 8) |    \
                    (((const u_int8_t *)(&_x))[2]<<16) |    \
                    (((const u_int8_t *)(&_x))[3]<<24)))

#else // AH_BYTE_ORDER

#define AR9300_EEPROM_MAGIC         0xa55a
#define    SWAP16(_x) ( (u_int16_t)( (((const u_int8_t *)(&_x))[1] ) |\
                        ( ( (const u_int8_t *)( &_x ) )[0]<< 8) ) )

#define SWAP32(_x) ((u_int32_t)(                       \
                    (((const u_int8_t *)(&_x))[3]) |        \
                    (((const u_int8_t *)(&_x))[2]<< 8) |    \
                    (((const u_int8_t *)(&_x))[1]<<16) |    \
                    (((const u_int8_t *)(&_x))[0]<<24)))

#endif // AH_BYTE_ORDER

// OTP registers for OSPREY

#define AR_GPIO_IN_OUT            0x4048 // GPIO input / output register
#define OTP_MEM_START_ADDRESS     0x14000
#define OTP_STATUS0_OTP_SM_BUSY   0x00015f18
#define OTP_STATUS1_EFUSE_READ_DATA 0x00015f1c

#define OTP_LDO_CONTROL_ENABLE    0x00015f24
#define OTP_LDO_STATUS_POWER_ON   0x00015f2c
#define OTP_INTF0_EFUSE_WR_ENABLE_REG_V 0x00015f00
// OTP register for Jupiter
#define GLB_OTP_LDO_CONTROL_ENABLE    0x00020020
#define GLB_OTP_LDO_STATUS_POWER_ON   0x00020028
#define OTP_PGENB_SETUP_HOLD_TIME_DELAY     0x15f34

// OTP register for Jupiter BT
#define BTOTP_MEM_START_ADDRESS				0x64000
#define BTOTP_STATUS0_OTP_SM_BUSY			0x00065f18
#define BTOTP_STATUS1_EFUSE_READ_DATA		0x00065f1c
#define BTOTP_INTF0_EFUSE_WR_ENABLE_REG_V	0x00065f00
#define BTOTP_INTF2							0x00065f08
#define BTOTP_PGENB_SETUP_HOLD_TIME_DELAY   0x65f34
#define BT_RESET_CTL						0x44000
#define BT_CLOCK_CONTROL					0x44028


// OTP register for WASP
#define OTP_MEM_START_ADDRESS_WASP           0x00030000 
#define OTP_STATUS0_OTP_SM_BUSY_WASP         (OTP_MEM_START_ADDRESS_WASP + 0x1018)
#define OTP_STATUS1_EFUSE_READ_DATA_WASP     (OTP_MEM_START_ADDRESS_WASP + 0x101C)
#define OTP_LDO_CONTROL_ENABLE_WASP          (OTP_MEM_START_ADDRESS_WASP + 0x1024)
#define OTP_LDO_STATUS_POWER_ON_WASP         (OTP_MEM_START_ADDRESS_WASP + 0x102C)
#define OTP_INTF0_EFUSE_WR_ENABLE_REG_V_WASP (OTP_MEM_START_ADDRESS_WASP + 0x1000)
// Below control the access timing of OTP read/write
#define OTP_PG_STROBE_PW_REG_V_WASP              (OTP_MEM_START_ADDRESS_WASP + 0x1008)
#define OTP_RD_STROBE_PW_REG_V_WASP              (OTP_MEM_START_ADDRESS_WASP + 0x100C)
#define OTP_VDDQ_HOLD_TIME_DELAY_WASP            (OTP_MEM_START_ADDRESS_WASP + 0x1030)
#define OTP_PGENB_SETUP_HOLD_TIME_DELAY_WASP     (OTP_MEM_START_ADDRESS_WASP + 0x1034)
#define OTP_STROBE_PULSE_INTERVAL_DELAY_WASP     (OTP_MEM_START_ADDRESS_WASP + 0x1038)
#define OTP_CSB_ADDR_LOAD_SETUP_HOLD_DELAY_WASP  (OTP_MEM_START_ADDRESS_WASP + 0x103C)

#define AR9300_EEPROM_MAGIC_OFFSET  0x0
/* reg_off = 4 * (eep_off) */
#define AR9300_EEPROM_S             2
#define AR9300_EEPROM_OFFSET        0x2000
#ifdef AR9100
#define AR9300_EEPROM_START_ADDR    0x1fff1000
#else
#define AR9300_EEPROM_START_ADDR    0x503f1200
#endif
#define AR9300_FLASH_CAL_START_OFFSET	    0x1000
#define AR9300_EEPROM_MAX           0xae0
#define IS_EEP_MINOR_V3(_ahp) (ar9300_eeprom_get((_ahp), EEP_MINOR_REV)  >= AR9300_EEP_MINOR_VER_3)

#define ar9300_get_ntxchains(_txchainmask) \
    (((_txchainmask >> 2) & 1) + ((_txchainmask >> 1) & 1) + (_txchainmask & 1))

/* RF silent fields in \ */
#define EEP_RFSILENT_ENABLED        0x0001  /* bit 0: enabled/disabled */
#define EEP_RFSILENT_ENABLED_S      0       /* bit 0: enabled/disabled */
#define EEP_RFSILENT_POLARITY       0x0002  /* bit 1: polarity */
#define EEP_RFSILENT_POLARITY_S     1       /* bit 1: polarity */
#define EEP_RFSILENT_GPIO_SEL       0x00fc  /* bits 2..7: gpio PIN */
#define EEP_RFSILENT_GPIO_SEL_S     2       /* bits 2..7: gpio PIN */
#define AR9300_EEP_VER               0xE
#define AR9300_BCHAN_UNUSED          0xFF
#define AR9300_MAX_RATE_POWER        63

typedef enum {
    CALDATA_AUTO=0,
    CALDATA_EEPROM,
    CALDATA_FLASH,
    CALDATA_OTP
} CALDATA_TYPE;

typedef enum {
    EEP_NFTHRESH_5,
    EEP_NFTHRESH_2,
    EEP_MAC_MSW,
    EEP_MAC_MID,
    EEP_MAC_LSW,
    EEP_REG_0,
    EEP_REG_1,
    EEP_OP_CAP,
    EEP_OP_MODE,
    EEP_RF_SILENT,
    EEP_OB_5,
    EEP_DB_5,
    EEP_OB_2,
    EEP_DB_2,
    EEP_MINOR_REV,
    EEP_TX_MASK,
    EEP_RX_MASK,
    EEP_FSTCLK_5G,
    EEP_RXGAIN_TYPE,
    EEP_OL_PWRCTRL,
    EEP_TXGAIN_TYPE,
    EEP_RC_CHAIN_MASK,
    EEP_DAC_HPWR_5G,
    EEP_FRAC_N_5G,
    EEP_DEV_TYPE,
    EEP_TEMPSENSE_SLOPE,
    EEP_TEMPSENSE_SLOPE_PAL_ON,
    EEP_PWR_TABLE_OFFSET,
    EEP_DRIVE_STRENGTH,
    EEP_INTERNAL_REGULATOR,
    EEP_SWREG,
    EEP_PAPRD_ENABLED,
    EEP_ANTDIV_control,
    EEP_CHAIN_MASK_REDUCE,
} EEPROM_PARAM;

#define AR9300_RATES_OFDM_OFFSET    0
#define AR9300_RATES_CCK_OFFSET     4
#define AR9300_RATES_HT20_OFFSET    8
#define AR9300_RATES_HT40_OFFSET    22
typedef enum ar9300_Rates {
    ALL_TARGET_LEGACY_6_24,
    ALL_TARGET_LEGACY_36,
    ALL_TARGET_LEGACY_48,
    ALL_TARGET_LEGACY_54,
    ALL_TARGET_LEGACY_1L_5L,
    ALL_TARGET_LEGACY_5S,
    ALL_TARGET_LEGACY_11L,
    ALL_TARGET_LEGACY_11S,
    ALL_TARGET_HT20_0_8_16,
    ALL_TARGET_HT20_1_3_9_11_17_19,
    ALL_TARGET_HT20_4,
    ALL_TARGET_HT20_5,
    ALL_TARGET_HT20_6,
    ALL_TARGET_HT20_7,
    ALL_TARGET_HT20_12,
    ALL_TARGET_HT20_13,
    ALL_TARGET_HT20_14,
    ALL_TARGET_HT20_15,
    ALL_TARGET_HT20_20,
    ALL_TARGET_HT20_21,
    ALL_TARGET_HT20_22,
    ALL_TARGET_HT20_23,
    ALL_TARGET_HT40_0_8_16,
    ALL_TARGET_HT40_1_3_9_11_17_19,
    ALL_TARGET_HT40_4,
    ALL_TARGET_HT40_5,
    ALL_TARGET_HT40_6,
    ALL_TARGET_HT40_7,
    ALL_TARGET_HT40_12,
    ALL_TARGET_HT40_13,
    ALL_TARGET_HT40_14,
    ALL_TARGET_HT40_15,
    ALL_TARGET_HT40_20,
    ALL_TARGET_HT40_21,
    ALL_TARGET_HT40_22,
    ALL_TARGET_HT40_23,
    ar9300_rate_size
} AR9300_RATES;


/**************************************************************************
 * fbin2freq
 *
 * Get channel value from binary representation held in eeprom
 * RETURNS: the frequency in MHz
 */
static inline u_int16_t
fbin2freq(u_int8_t fbin, HAL_BOOL is_2ghz)
{
    /*
    * Reserved value 0xFF provides an empty definition both as
    * an fbin and as a frequency - do not convert
    */
    if (fbin == AR9300_BCHAN_UNUSED)
    {
        return fbin;
    }

    return (u_int16_t)((is_2ghz) ? (2300 + fbin) : (4800 + 5 * fbin));
}

extern int CompressionHeaderUnpack(u_int8_t *best, int *code, int *reference, int *length, int *major, int *minor);
extern void Ar9300EepromFormatConvert(ar9300_eeprom_t *mptr);
extern HAL_BOOL ar9300_eeprom_restore(struct ath_hal *ah);
extern int ar9300_eeprom_restore_internal(struct ath_hal *ah, ar9300_eeprom_t *mptr, int /*msize*/);
extern int ar9300_eeprom_base_address(struct ath_hal *ah);
extern int ar9300_eeprom_volatile(struct ath_hal *ah);
extern int ar9300_eeprom_low_limit(struct ath_hal *ah);
extern u_int16_t ar9300_compression_checksum(u_int8_t *data, int dsize);
extern int ar9300_compression_header_unpack(u_int8_t *best, int *code, int *reference, int *length, int *major, int *minor);

extern u_int16_t ar9300_eeprom_struct_size(void);
extern ar9300_eeprom_t *ar9300EepromStructInit(int default_index);
extern ar9300_eeprom_t *ar9300EepromStructGet(void);
extern ar9300_eeprom_t *ar9300_eeprom_struct_default(int default_index);
extern ar9300_eeprom_t *ar9300_eeprom_struct_default_find_by_id(int ver);
extern int ar9300_eeprom_struct_default_many(void);
extern int ar9300EepromUpdateCalPier(int pierIdx, int freq, int chain,
                          int pwrCorrection, int volt_meas, int temp_meas);
extern int ar9300_power_control_override(struct ath_hal *ah, int frequency, int *correction, int *voltage, int *temperature);

extern void ar9300EepromDisplayCalData(int for2GHz);
extern void ar9300EepromDisplayAll(void);
extern void ar9300_set_target_power_from_eeprom(struct ath_hal *ah, u_int16_t freq,
                                           u_int8_t *target_power_val_t2);
extern HAL_BOOL ar9300_eeprom_set_power_per_rate_table(struct ath_hal *ah,
                                             ar9300_eeprom_t *p_eep_data,
                                             const struct ieee80211_channel *chan,
                                             u_int8_t *p_pwr_array,
                                             u_int16_t cfg_ctl,
                                             u_int16_t antenna_reduction,
                                             u_int16_t twice_max_regulatory_power,
                                             u_int16_t power_limit,
                                             u_int8_t chainmask);
extern int ar9300_transmit_power_reg_write(struct ath_hal *ah, u_int8_t *p_pwr_array); 

extern u_int8_t ar9300_eeprom_get_legacy_trgt_pwr(struct ath_hal *ah, u_int16_t rate_index, u_int16_t freq, HAL_BOOL is_2ghz);
extern u_int8_t ar9300_eeprom_get_ht20_trgt_pwr(struct ath_hal *ah, u_int16_t rate_index, u_int16_t freq, HAL_BOOL is_2ghz);
extern u_int8_t ar9300_eeprom_get_ht40_trgt_pwr(struct ath_hal *ah, u_int16_t rate_index, u_int16_t freq, HAL_BOOL is_2ghz);
extern u_int8_t ar9300_eeprom_get_cck_trgt_pwr(struct ath_hal *ah, u_int16_t rate_index, u_int16_t freq);
extern HAL_BOOL ar9300_internal_regulator_apply(struct ath_hal *ah);
extern HAL_BOOL ar9300_drive_strength_apply(struct ath_hal *ah);
extern HAL_BOOL ar9300_attenuation_apply(struct ath_hal *ah, u_int16_t channel);
extern int32_t ar9300_thermometer_get(struct ath_hal *ah);
extern HAL_BOOL ar9300_thermometer_apply(struct ath_hal *ah);
extern HAL_BOOL ar9300_xpa_timing_control_apply(struct ath_hal *ah, HAL_BOOL is_2ghz);
extern HAL_BOOL ar9300_x_lNA_bias_strength_apply(struct ath_hal *ah, HAL_BOOL is_2ghz);

extern int32_t ar9300MacAdressGet(u_int8_t *mac);
extern int32_t ar9300CustomerDataGet(u_int8_t *data, int32_t len);
extern int32_t ar9300ReconfigDriveStrengthGet(void);
extern int32_t ar9300EnableTempCompensationGet(void);
extern int32_t ar9300EnableVoltCompensationGet(void);
extern int32_t ar9300FastClockEnableGet(void);
extern int32_t ar9300EnableDoublingGet(void);

extern u_int16_t *ar9300_regulatory_domain_get(struct ath_hal *ah);
extern int32_t ar9300_eeprom_write_enable_gpio_get(struct ath_hal *ah);
extern int32_t ar9300_wlan_led_gpio_get(struct ath_hal *ah);
extern int32_t ar9300_wlan_disable_gpio_get(struct ath_hal *ah);
extern int32_t ar9300_rx_band_select_gpio_get(struct ath_hal *ah);
extern int32_t ar9300_rx_gain_index_get(struct ath_hal *ah);
extern int32_t ar9300_tx_gain_index_get(struct ath_hal *ah);
extern int32_t ar9300_xpa_bias_level_get(struct ath_hal *ah, HAL_BOOL is_2ghz);
extern HAL_BOOL ar9300_xpa_bias_level_apply(struct ath_hal *ah, HAL_BOOL is_2ghz);
extern u_int32_t ar9300_ant_ctrl_common_get(struct ath_hal *ah, HAL_BOOL is_2ghz);
extern u_int32_t ar9300_ant_ctrl_common2_get(struct ath_hal *ah, HAL_BOOL is_2ghz);
extern u_int16_t ar9300_ant_ctrl_chain_get(struct ath_hal *ah, int chain, HAL_BOOL is_2ghz);
extern HAL_BOOL ar9300_ant_ctrl_apply(struct ath_hal *ah, HAL_BOOL is_2ghz);
/* since valid noise floor values are negative, returns 1 on error */
extern int32_t ar9300_noise_floor_cal_or_power_get(
    struct ath_hal *ah, int32_t frequency, int32_t ichain, HAL_BOOL use_cal);
#define ar9300NoiseFloorGet(ah, frequency, ichain) \
    ar9300_noise_floor_cal_or_power_get(ah, frequency, ichain, 1/*use_cal*/)
#define ar9300NoiseFloorPowerGet(ah, frequency, ichain) \
    ar9300_noise_floor_cal_or_power_get(ah, frequency, ichain, 0/*use_cal*/)
extern void ar9300_eeprom_template_preference(int32_t value);
extern int32_t ar9300_eeprom_template_install(struct ath_hal *ah, int32_t value);
extern void ar9300_calibration_data_set(struct ath_hal *ah, int32_t source);
extern int32_t ar9300_calibration_data_get(struct ath_hal *ah);
extern int32_t ar9300_calibration_data_address_get(struct ath_hal *ah);
extern void ar9300_calibration_data_address_set(struct ath_hal *ah, int32_t source);
extern HAL_BOOL ar9300_calibration_data_read_flash(struct ath_hal *ah, long address, u_int8_t *buffer, int many);
extern HAL_BOOL ar9300_calibration_data_read_eeprom(struct ath_hal *ah, long address, u_int8_t *buffer, int many);
extern HAL_BOOL ar9300_calibration_data_read_otp(struct ath_hal *ah, long address, u_int8_t *buffer, int many, HAL_BOOL is_wifi);
extern HAL_BOOL ar9300_calibration_data_read(struct ath_hal *ah, long address, u_int8_t *buffer, int many);
extern int32_t ar9300_eeprom_size(struct ath_hal *ah);
extern int32_t ar9300_otp_size(struct ath_hal *ah);
extern HAL_BOOL ar9300_calibration_data_read_array(struct ath_hal *ah, int address, u_int8_t *buffer, int many);



#if defined(WIN32) || defined(WIN64)
#pragma pack (pop, ar9300)
#endif

#endif  /* _ATH_AR9300_EEP_H_ */
