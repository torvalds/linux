/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __ES9356_H__
#define __ES9356_H__

/*ES9356 Implementation-define*/
#define ES9356_FLAGS_HP                         0x2003
#define ES9356_CSM_RESET                        0x2020
#define ES9356_FUC_RESET                        0x2021
#define ES9356_STATE                            0x2022
#define ES9356_VMID_TIME                        0x2023
#define ES9356_STATE_TIME                       0x2024
#define ES9356_HP_SPK_TIME                      0x2025
#define ES9356_WP_ENABLE                        0x2026
#define ES9356_DMIC_GPIO                        0x2027
#define ES9356_ENDPOINT_MODE                    0x2028

/*HP DETECT*/
#define ES9356_HP_TYPE                          0x2029
#define ES9356_HP_DETECTTIME                    0x202A
#define ES9356_MICBIAS_SEL                      0x202B
#define ES9356_KEY_PRESS_TIME                   0x202C
#define ES9356_KEY_RELEASE_TIME                 0x202D
#define ES9356_KEY_HOLD_TIME                    0x202E
#define ES9356_BTSEL_REF                        0x202F
#define ES9356_BUTTON_CHARGE                    0x2030

#define ES9356_KEYD_DETECT                      0x2031
#define ES9356_DPEN_TIME                        0x2032
#define ES9356_TIMER_CHECK                      0x2033
#define ES9356_IBIASGEN                         0x2041
#define ES9356_VMID1SEL                         0x2042
#define ES9356_VMID1STL                         0x2043
#define ES9356_VMID2SEL                         0x2044
#define ES9356_VMID2STL                         0x2045
#define ES9356_VSEL                             0x2046
#define ES9356_MICBIAS_CTL                      0x2047
#define ES9356_HPDETECT_CTL                     0x2048
#define ES9356_MICBIAS_RES                      0x2049

/*CLK*/
#define ES9356_CLK_SEL                          0x2050
#define ES9356_CLK_CTL                          0x2051
#define ES9356_DETCLK_CTL                       0x2052
#define ES9356_CPCLK_CTL                        0x2053
#define ES9356_SPKCLK_CTL                       0x2054
#define ES9356_PRE_DIV_CTL                      0x2055
#define ES9356_DLL_MODE                         0x2056
#define ES9356_ANACLK_SEL                       0x2057
#define ES9356_OSRCLK_SEL                       0x2058
#define ES9356_DSPCLK_SEL                       0x2059
#define ES9356_SPK9M_MODE                       0x205a

/*ADC DIG CTL*/
#define ES9356_DMIC_POL                         0x2061
#define ES9356_ADC_SWAP                         0x2062
#define ES9356_ADC_OSR                          0x2063
#define ES9356_ADC_OSRGAIN                      0x2064
#define ES9356_ADC_CLEARRAM                     0x2065
#define ES9356_ADC_RAMP                         0x2066
#define ES9356_ADC_HPF1                         0x2067
#define ES9356_ADC_HPF2                         0x2068
#define ES9356_ADC_ALC                          0x206C
#define ES9356_ALC_LEVEL                        0x206D
#define ES9356_ALC_RAMP_WINSIZE                 0x206E

/*ADC ANA CTL*/
#define ES9356_ADC_REF_EN                       0x2080
#define ES9356_ADC_AMIC_CTL                     0x2081
#define ES9356_ADC_ANA                          0x2082
#define ES9356_PGA_CTL                          0x2083
#define ES9356_ADC_INT                          0x2084
#define ES9356_ADC_VCM                          0x2085
#define ES9356_ADC_VRPBIAS                      0x2086
#define ES9356_ADC_LP                           0x2087

/*DAC DIG CTL*/
#define ES9356_DAC_FSMODE                       0x2090
#define ES9356_DAC_OSR                          0x2091
#define ES9356_DAC_INV                          0x2092
#define ES9356_DAC_RAMP                         0x2093
#define ES9356_DAC_VPPSCALE                     0x2094
#define ES9356_DAC_SWAP                         0x2097
#define ES9356_SPKCMP_VPPSC                     0x20A0
#define ES9356_CALIBRATION_TIME                 0x20A1
#define ES9356_CALIBRATION_SETTING              0x20A2
#define ES9356_DAC_OFFSET_LH                    0x20A3
#define ES9356_DAC_OFFSET_LL                    0x20A4
#define ES9356_DAC_OFFSET_RH                    0x20A5
#define ES9356_DAC_OFFSET_RL                    0x20A6

/*DAC ANA CTL*/
#define ES9356_DAC_REF_EN                       0x20B0
#define ES9356_DAC_ENABLE                       0x20B1
#define ES9356_DAC_VROI                         0x20B2
#define ES9356_DAC_LP                           0x20B3

/*HP CTL*/
#define ES9356_CHARGEPUMP_CTL                   0x20C0
#define ES9356_CPLDO_CTL                        0x20C1
#define ES9356_HP_REF_CTL                       0x20C2
#define ES9356_HP_IBIAS                         0x20C3
#define ES9356_HP_EN                            0x20C4
#define ES9356_HP_VOLUME                        0x20C5
#define ES9356_HP_LP                            0x20C6

/*SPK CTL*/
#define ES9356_SPKLDO_CTL                       0x20D0
#define ES9356_CLASSD_CTL                       0x20D1
#define ES9356_SPK_HBDG                         0x20D5
#define ES9356_SPK_VOLUME                       0x20D7
#define ES9356_SPK_SCP                          0x20D8
#define ES9356_SPK_DT                           0x20D9
#define ES9356_SPK_OTP                          0x20DA
#define ES9356_SPKBIAS_COMP                     0x20DB

/* ES9356 SDCA Control - function number */
#define FUNC_NUM_UAJ                            0x01
#define FUNC_NUM_MIC                            0x02
#define FUNC_NUM_AMP                            0x03
#define FUNC_NUM_HID                            0x04

/* ES9356 SDCA entity */
#define ES9356_SDCA_ENT0                        0x00
#define ES9356_SDCA_ENT_PDE11                   0x03
#define ES9356_SDCA_ENT_FU11                    0x04
#define ES9356_SDCA_ENT_XU12                    0x05
#define ES9356_SDCA_ENT_FU113                   0x07
#define ES9356_SDCA_ENT_CS113                   0x09
#define ES9356_SDCA_ENT_PPU11                   0x0C

#define ES9356_SDCA_ENT_CS21                    0x02
#define ES9356_SDCA_ENT_PPU21                   0x03
#define ES9356_SDCA_ENT_FU21                    0X04
#define ES9356_SDCA_ENT_XU22                    0x06
#define ES9356_SDCA_ENT_SAPU29                  0x03
#define ES9356_SDCA_ENT_PDE23                   0x0B
#define ES9356_SDCA_ENT_HID01                   0x01

#define ES9356_SDCA_ENT_CS41                    0x02
#define ES9356_SDCA_ENT_FU35                    0x04
#define ES9356_SDCA_ENT_XU42                    0x06
#define ES9356_SDCA_ENT_FU41                    0x07
#define ES9356_SDCA_ENT_PDE47                   0x0E
#define ES9356_SDCA_ENT_IT33                    0x0F
#define ES9356_SDCA_ENT_PDE34                   0x10
#define ES9356_SDCA_ENT_FU33                    0x11
#define ES9356_SDCA_ENT_XU36                    0x13
#define ES9356_SDCA_ENT_FU36                    0x15
#define ES9356_SDCA_ENT_CS36                    0x17
#define ES9356_SDCA_ENT_GE35                    0x18

/* ES9356 SDCA control */
#define ES9356_SDCA_CTL_SAMPLE_FREQ_INDEX       0x10
#define ES9356_SDCA_CTL_FU_MUTE                 0x01
#define ES9356_SDCA_CTL_FU_VOLUME               0x02
#define ES9356_SDCA_CTL_HIDTX_CURRENT_OWNER     0x10
#define ES9356_SDCA_CTL_SELECTED_MODE           0x01
#define ES9356_SDCA_CTL_DETECTED_MODE           0x02
#define ES9356_SDCA_CTL_REQ_POWER_STATE         0x01
#define ES9356_SDCA_CTL_FU_CH_GAIN              0x0b
#define ES9356_SDCA_CTL_FUNC_STATUS             0x10
#define ES9356_SDCA_CTL_ACTUAL_POWER_STATE      0x10
#define ES9356_SDCA_CTL_POSTURE_NUMBER          0x00

/* ES9356 SDCA channel */
#define CH_L	0x01
#define CH_R	0x02
#define MBQ	0x2000

/* ES9356 HID*/
#define ES9356_BUF_ADDR_HID     0x44000000
#define ES9356_HID_BYTE2        0x44000001
#define ES9356_HID_BYTE3        0x44000002
#define ES9356_HID_BYTE4        0x44000003

/* ES9356 Volume Setting*/
#define ES9356_VU_BASE          768
#define ES9356_OFFSET_HIGH      0x07F8
#define ES9356_OFFSET_LOW       0x0007
#define ES9356_DEFAULT_VOLUME   0x00
#define	ES9356_VOLUME_STEP      32
#define ES9356_VOLUME_MIN       -768
#define	ES9356_VOLUME_MAX       285
#define	ES9356_AMIC_GAIN_STEP   768
#define	ES9356_DMIC_GAIN_STEP   1536
#define ES9356_GAIN_MIN         0
#define	ES9356_AMIC_GAIN_MAX    10
#define	ES9356_DMIC_GAIN_MAX    3

enum {
	ES9356_DMIC = 1, /* For dmic */
	ES9356_JACK_IN, /* For headset mic */
	ES9356_AMP, /* For speaker */
	ES9356_JACK_OUT, /* For headphone */
};

enum {
	ES9356_SDCA_RATE_16000HZ,
	ES9356_SDCA_RATE_24000HZ,
	ES9356_SDCA_RATE_32000HZ,
	ES9356_SDCA_RATE_44100HZ,
	ES9356_SDCA_RATE_48000HZ,
	ES9356_SDCA_RATE_88200HZ,
	ES9356_SDCA_RATE_96000HZ,
};

#endif
