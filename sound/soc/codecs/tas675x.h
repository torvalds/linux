/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ALSA SoC Texas Instruments TAS675x Quad-Channel Audio Amplifier
 *
 * Copyright (C) 2026 Texas Instruments Incorporated - https://www.ti.com/
 *	Author: Sen Wang <sen@ti.com>
 */

#ifndef __TAS675X_H__
#define __TAS675X_H__

/*
 * Book 0, Page 0 — Register Addresses
 */

#define TAS675X_PAGE_SIZE                    256
#define TAS675X_PAGE_REG(page, reg)  ((page) * TAS675X_PAGE_SIZE + (reg))

/* Page Control & Basic Config */
#define TAS675X_PAGE_CTRL_REG                0x00
#define TAS675X_RESET_REG                    0x01
#define TAS675X_OUTPUT_CTRL_REG              0x02
#define TAS675X_STATE_CTRL_CH1_CH2_REG       0x03
#define TAS675X_STATE_CTRL_CH3_CH4_REG       0x04
#define TAS675X_ISENSE_CTRL_REG              0x05
#define TAS675X_DC_DETECT_CTRL_REG           0x06

/* Serial Audio Port */
#define TAS675X_SCLK_INV_CTRL_REG            0x20
#define TAS675X_AUDIO_IF_CTRL_REG            0x21
#define TAS675X_SDIN_CTRL_REG                0x23
#define TAS675X_SDOUT_CTRL_REG               0x25
#define TAS675X_SDIN_OFFSET_MSB_REG          0x27
#define TAS675X_SDIN_AUDIO_OFFSET_REG        0x28
#define TAS675X_SDIN_LL_OFFSET_REG           0x29
#define TAS675X_SDIN_CH_SWAP_REG             0x2A
#define TAS675X_SDOUT_OFFSET_MSB_REG         0x2C
#define TAS675X_VPREDICT_OFFSET_REG          0x2D
#define TAS675X_ISENSE_OFFSET_REG            0x2E
#define TAS675X_SDOUT_EN_REG                 0x31
#define TAS675X_LL_EN_REG                    0x32

/* DSP & Core Audio Control */
#define TAS675X_RTLDG_EN_REG                 0x37
#define TAS675X_DC_BLOCK_BYP_REG             0x39
#define TAS675X_DSP_CTRL_REG                 0x3A
#define TAS675X_PAGE_AUTO_INC_REG            0x3B

/* Volume & Mute */
#define TAS675X_DIG_VOL_CH1_REG              0x40
#define TAS675X_DIG_VOL_CH2_REG              0x41
#define TAS675X_DIG_VOL_CH3_REG              0x42
#define TAS675X_DIG_VOL_CH4_REG              0x43
#define TAS675X_DIG_VOL_RAMP_CTRL_REG        0x44
#define TAS675X_DIG_VOL_COMBINE_CTRL_REG     0x46
#define TAS675X_AUTO_MUTE_EN_REG             0x47
#define TAS675X_AUTO_MUTE_TIMING_CH1_CH2_REG 0x48
#define TAS675X_AUTO_MUTE_TIMING_CH3_CH4_REG 0x49

/* Analog Gain & Power Stage */
#define TAS675X_ANALOG_GAIN_CH1_CH2_REG      0x4A
#define TAS675X_ANALOG_GAIN_CH3_CH4_REG      0x4B
#define TAS675X_ANALOG_GAIN_RAMP_CTRL_REG    0x4E
#define TAS675X_PULSE_INJECTION_EN_REG       0x52
#define TAS675X_CBC_CTRL_REG                 0x54
#define TAS675X_CURRENT_LIMIT_CTRL_REG       0x55
#define TAS675X_DAC_CLK_REG                  0x5A
#define TAS675X_ISENSE_CAL_REG               0x5B

/* Spread Spectrum & PWM Phase */
#define TAS675X_PWM_PHASE_CTRL_REG           0x60
#define TAS675X_SS_CTRL_REG                  0x61
#define TAS675X_SS_RANGE_CTRL_REG            0x62
#define TAS675X_SS_DWELL_CTRL_REG            0x66
#define TAS675X_RAMP_PHASE_CTRL_GPO_REG      0x68
#define TAS675X_PWM_PHASE_M_CTRL_CH1_REG     0x69
#define TAS675X_PWM_PHASE_M_CTRL_CH2_REG     0x6A
#define TAS675X_PWM_PHASE_M_CTRL_CH3_REG     0x6B
#define TAS675X_PWM_PHASE_M_CTRL_CH4_REG     0x6C

/* Status & Reporting */
#define TAS675X_AUTO_MUTE_STATUS_REG         0x71
#define TAS675X_STATE_REPORT_CH1_CH2_REG     0x72
#define TAS675X_STATE_REPORT_CH3_CH4_REG     0x73
#define TAS675X_PVDD_SENSE_REG               0x74
#define TAS675X_TEMP_GLOBAL_REG              0x75
#define TAS675X_FS_MON_REG                   0x76
#define TAS675X_SCLK_MON_REG                 0x77
#define TAS675X_REPORT_ROUTING_1_REG         0x7C

/* Memory Paging & Book Control */
#define TAS675X_SETUP_REG1                   0x7D
#define TAS675X_SETUP_REG2                   0x7E
#define TAS675X_BOOK_CTRL_REG                0x7F

/* Fault Status */
#define TAS675X_POWER_FAULT_STATUS_1_REG     0x7D
#define TAS675X_POWER_FAULT_STATUS_2_REG     0x80
#define TAS675X_OT_FAULT_REG                 0x81
#define TAS675X_OTW_STATUS_REG               0x82
#define TAS675X_CLIP_WARN_STATUS_REG         0x83
#define TAS675X_CBC_WARNING_STATUS_REG       0x85

/* Latched Fault Registers */
#define TAS675X_POWER_FAULT_LATCHED_REG      0x86
#define TAS675X_OTSD_LATCHED_REG             0x87
#define TAS675X_OTW_LATCHED_REG              0x88
#define TAS675X_CLIP_WARN_LATCHED_REG        0x89
#define TAS675X_CLK_FAULT_LATCHED_REG        0x8A
#define TAS675X_RTLDG_OL_SL_FAULT_LATCHED_REG 0x8B
#define TAS675X_CBC_FAULT_WARN_LATCHED_REG   0x8D
#define TAS675X_OC_DC_FAULT_LATCHED_REG      0x8E
#define TAS675X_OTSD_RECOVERY_EN_REG         0x8F

/* Protection & Routing Controls */
#define TAS675X_REPORT_ROUTING_2_REG         0x90
#define TAS675X_REPORT_ROUTING_3_REG         0x91
#define TAS675X_REPORT_ROUTING_4_REG         0x92
#define TAS675X_CLIP_DETECT_CTRL_REG         0x93
#define TAS675X_REPORT_ROUTING_5_REG         0x94

/* GPIO Pin Configuration */
#define TAS675X_GPIO1_OUTPUT_SEL_REG         0x95
#define TAS675X_GPIO2_OUTPUT_SEL_REG         0x96
#define TAS675X_GPIO_INPUT_SLEEP_HIZ_REG     0x9B
#define TAS675X_GPIO_INPUT_PLAY_SLEEP_REG    0x9C
#define TAS675X_GPIO_INPUT_MUTE_REG          0x9D
#define TAS675X_GPIO_INPUT_SYNC_REG          0x9E
#define TAS675X_GPIO_INPUT_SDIN2_REG         0x9F
#define TAS675X_GPIO_CTRL_REG                0xA0
#define TAS675X_GPIO_INVERT_REG              0xA1

/* Load Diagnostics Config */
#define TAS675X_DC_LDG_CTRL_REG              0xB0
#define TAS675X_DC_LDG_LO_CTRL_REG           0xB1
#define TAS675X_DC_LDG_TIME_CTRL_REG         0xB2
#define TAS675X_DC_LDG_SL_CH1_CH2_CTRL_REG   0xB3
#define TAS675X_DC_LDG_SL_CH3_CH4_CTRL_REG   0xB4
#define TAS675X_AC_LDG_CTRL_REG              0xB5
#define TAS675X_TWEETER_DETECT_CTRL_REG      0xB6
#define TAS675X_TWEETER_DETECT_THRESH_REG    0xB7
#define TAS675X_AC_LDG_FREQ_CTRL_REG         0xB8
#define TAS675X_TEMP_CH1_CH2_REG             0xBB
#define TAS675X_TEMP_CH3_CH4_REG             0xBC
#define TAS675X_WARN_OT_MAX_FLAG_REG         0xBD

/* DC Load Diagnostic Reports */
#define TAS675X_DC_LDG_REPORT_CH1_CH2_REG    0xC0
#define TAS675X_DC_LDG_REPORT_CH3_CH4_REG    0xC1
#define TAS675X_DC_LDG_RESULT_REG            0xC2
#define TAS675X_AC_LDG_REPORT_CH1_R_REG      0xC3
#define TAS675X_AC_LDG_REPORT_CH1_I_REG      0xC4
#define TAS675X_AC_LDG_REPORT_CH2_R_REG      0xC5
#define TAS675X_AC_LDG_REPORT_CH2_I_REG      0xC6
#define TAS675X_AC_LDG_REPORT_CH3_R_REG      0xC7
#define TAS675X_AC_LDG_REPORT_CH3_I_REG      0xC8
#define TAS675X_AC_LDG_REPORT_CH4_R_REG      0xC9
#define TAS675X_AC_LDG_REPORT_CH4_I_REG      0xCA
#define TAS675X_TWEETER_REPORT_REG           0xCB

/* RTLDG Impedance */
#define TAS675X_CH1_RTLDG_IMP_MSB_REG        0xD1
#define TAS675X_CH1_RTLDG_IMP_LSB_REG        0xD2
#define TAS675X_CH2_RTLDG_IMP_MSB_REG        0xD3
#define TAS675X_CH2_RTLDG_IMP_LSB_REG        0xD4
#define TAS675X_CH3_RTLDG_IMP_MSB_REG        0xD5
#define TAS675X_CH3_RTLDG_IMP_LSB_REG        0xD6
#define TAS675X_CH4_RTLDG_IMP_MSB_REG        0xD7
#define TAS675X_CH4_RTLDG_IMP_LSB_REG        0xD8

/* DC Load Diagnostic Resistance */
#define TAS675X_DC_LDG_DCR_MSB_REG           0xD9
#define TAS675X_CH1_DC_LDG_DCR_LSB_REG       0xDA
#define TAS675X_CH2_DC_LDG_DCR_LSB_REG       0xDB
#define TAS675X_CH3_DC_LDG_DCR_LSB_REG       0xDC
#define TAS675X_CH4_DC_LDG_DCR_LSB_REG       0xDD

/* Over-Temperature Warning */
#define TAS675X_OTW_CTRL_CH1_CH2_REG         0xE2
#define TAS675X_OTW_CTRL_CH3_CH4_REG         0xE3

/* RESET_REG (all bits auto-clear) */
#define TAS675X_DEVICE_RESET                 BIT(4)
#define TAS675X_FAULT_CLEAR                  BIT(3)
#define TAS675X_REGISTER_RESET               BIT(0)

/* STATE_CTRL and STATE_REPORT — Channel state values */
#define TAS675X_STATE_DEEPSLEEP              0x00
#define TAS675X_STATE_LOAD_DIAG              0x01
#define TAS675X_STATE_SLEEP                  0x02
#define TAS675X_STATE_HIZ                    0x03
#define TAS675X_STATE_PLAY                   0x04

/* Additional STATE_REPORT values */
#define TAS675X_STATE_FAULT                  0x05
#define TAS675X_STATE_AUTOREC                0x06

/* Combined values for both channel pairs in one register */
#define TAS675X_STATE_DEEPSLEEP_BOTH \
	(TAS675X_STATE_DEEPSLEEP | (TAS675X_STATE_DEEPSLEEP << 4))
#define TAS675X_STATE_LOAD_DIAG_BOTH \
	(TAS675X_STATE_LOAD_DIAG | (TAS675X_STATE_LOAD_DIAG << 4))
#define TAS675X_STATE_SLEEP_BOTH \
	(TAS675X_STATE_SLEEP | (TAS675X_STATE_SLEEP << 4))
#define TAS675X_STATE_HIZ_BOTH \
	(TAS675X_STATE_HIZ | (TAS675X_STATE_HIZ << 4))
#define TAS675X_STATE_PLAY_BOTH \
	(TAS675X_STATE_PLAY | (TAS675X_STATE_PLAY << 4))
#define TAS675X_STATE_FAULT_BOTH \
	(TAS675X_STATE_FAULT | (TAS675X_STATE_FAULT << 4))

/* STATE_CTRL_CH1_CH2 / STATE_CTRL_CH3_CH4 — mute bits */
#define TAS675X_CH1_MUTE_BIT                 BIT(7)
#define TAS675X_CH2_MUTE_BIT                 BIT(3)
#define TAS675X_CH_MUTE_BOTH                 (TAS675X_CH1_MUTE_BIT | TAS675X_CH2_MUTE_BIT)

/* SCLK_INV_CTRL_REG */
#define TAS675X_SCLK_INV_TX_BIT             BIT(5)
#define TAS675X_SCLK_INV_RX_BIT             BIT(4)
#define TAS675X_SCLK_INV_MASK               (TAS675X_SCLK_INV_TX_BIT | TAS675X_SCLK_INV_RX_BIT)

/* AUDIO_IF_CTRL_REG */
#define TAS675X_TDM_EN_BIT                   BIT(4)
#define TAS675X_SAP_FMT_MASK                 GENMASK(3, 2)
#define TAS675X_SAP_FMT_I2S                  (0x00 << 2)
#define TAS675X_SAP_FMT_TDM                  (0x01 << 2)
#define TAS675X_SAP_FMT_RIGHT_J              (0x02 << 2)
#define TAS675X_SAP_FMT_LEFT_J               (0x03 << 2)
#define TAS675X_FS_PULSE_MASK                GENMASK(1, 0)
#define TAS675X_FS_PULSE_SHORT               0x01

/* SDIN_CTRL_REG */
#define TAS675X_SDIN_AUDIO_WL_MASK           GENMASK(3, 2)
#define TAS675X_SDIN_LL_WL_MASK              GENMASK(1, 0)
#define TAS675X_SDIN_WL_MASK                 (TAS675X_SDIN_AUDIO_WL_MASK | TAS675X_SDIN_LL_WL_MASK)

/* SDOUT_CTRL_REG */
#define TAS675X_SDOUT_SELECT_MASK            GENMASK(7, 4)
#define TAS675X_SDOUT_SELECT_TDM_SDOUT1      0x00
#define TAS675X_SDOUT_SELECT_NON_TDM         0x10
#define TAS675X_SDOUT_VP_WL_MASK             GENMASK(3, 2)
#define TAS675X_SDOUT_IS_WL_MASK             GENMASK(1, 0)
#define TAS675X_SDOUT_WL_MASK                (TAS675X_SDOUT_VP_WL_MASK | TAS675X_SDOUT_IS_WL_MASK)

/* SDOUT_EN_REG */
#define TAS675X_SDOUT_NON_TDM_SEL_MASK      GENMASK(5, 4)
#define TAS675X_SDOUT_NON_TDM_SEL_VPREDICT  (0x0 << 4)
#define TAS675X_SDOUT_NON_TDM_SEL_ISENSE    (0x1 << 4)
#define TAS675X_SDOUT_EN_VPREDICT           BIT(0)
#define TAS675X_SDOUT_EN_ISENSE             BIT(1)
#define TAS675X_SDOUT_EN_NON_TDM_ALL        GENMASK(1, 0)

/* Word length values (shared by SDIN_CTRL and SDOUT_CTRL) */
#define TAS675X_WL_16BIT                     0x00
#define TAS675X_WL_20BIT                     0x01
#define TAS675X_WL_24BIT                     0x02
#define TAS675X_WL_32BIT                     0x03

/* SDIN_OFFSET_MSB_REG */
#define TAS675X_SDIN_AUDIO_OFF_MSB_MASK      GENMASK(7, 6)
#define TAS675X_SDIN_LL_OFF_MSB_MASK         GENMASK(5, 4)

/* SDOUT_OFFSET_MSB_REG */
#define TAS675X_SDOUT_VP_OFF_MSB_MASK        GENMASK(7, 6)
#define TAS675X_SDOUT_IS_OFF_MSB_MASK        GENMASK(5, 4)

/* RTLDG_EN_REG */
#define TAS675X_RTLDG_CLIP_MASK_BIT          BIT(4)
#define TAS675X_RTLDG_CH_EN_MASK             GENMASK(3, 0)

/* DC_LDG_CTRL_REG */
#define TAS675X_LDG_ABORT_BIT                BIT(7)
#define TAS675X_LDG_BUFFER_WAIT_MASK         GENMASK(6, 5)
#define TAS675X_LDG_WAIT_BYPASS_BIT          BIT(2)
#define TAS675X_SLOL_DISABLE_BIT             BIT(1)
#define TAS675X_LDG_BYPASS_BIT               BIT(0)

/* DC_LDG_TIME_CTRL_REG */
#define TAS675X_LDG_RAMP_SLOL_MASK           GENMASK(7, 6)
#define TAS675X_LDG_SETTLING_SLOL_MASK       GENMASK(5, 4)
#define TAS675X_LDG_RAMP_S2PG_MASK           GENMASK(3, 2)
#define TAS675X_LDG_SETTLING_S2PG_MASK       GENMASK(1, 0)

/* AC_LDG_CTRL_REG */
#define TAS675X_AC_DIAG_GAIN_BIT             BIT(4)
#define TAS675X_AC_DIAG_START_MASK           GENMASK(3, 0)

/* DC_LDG_RESULT_REG */
#define TAS675X_DC_LDG_LO_RESULT_MASK        GENMASK(7, 4)
#define TAS675X_DC_LDG_PASS_MASK             GENMASK(3, 0)

/* Load Diagnostics Timing Constants */
#define TAS675X_POLL_INTERVAL_US             10000
#define TAS675X_STATE_TRANSITION_TIMEOUT_US  50000
#define TAS675X_DC_LDG_TIMEOUT_US            300000
#define TAS675X_AC_LDG_TIMEOUT_US            400000

/* GPIO_CTRL_REG */
#define TAS675X_GPIO1_OUTPUT_EN              BIT(7)
#define TAS675X_GPIO2_OUTPUT_EN              BIT(6)
#define TAS675X_GPIO_CTRL_RSTVAL             0x22

/* GPIO output select values */
#define TAS675X_GPIO_SEL_LOW                 0x00
#define TAS675X_GPIO_SEL_AUTO_MUTE_ALL       0x02
#define TAS675X_GPIO_SEL_AUTO_MUTE_CH4       0x03
#define TAS675X_GPIO_SEL_AUTO_MUTE_CH3       0x04
#define TAS675X_GPIO_SEL_AUTO_MUTE_CH2       0x05
#define TAS675X_GPIO_SEL_AUTO_MUTE_CH1       0x06
#define TAS675X_GPIO_SEL_SDOUT2              0x08
#define TAS675X_GPIO_SEL_SDOUT1              0x09
#define TAS675X_GPIO_SEL_WARN                0x0A
#define TAS675X_GPIO_SEL_FAULT               0x0B
#define TAS675X_GPIO_SEL_CLOCK_SYNC          0x0E
#define TAS675X_GPIO_SEL_INVALID_CLK         0x0F
#define TAS675X_GPIO_SEL_HIGH                0x13

/* GPIO input function encoding (flag bit | function ID) */
#define TAS675X_GPIO_FUNC_INPUT              0x100

/* Input Function IDs */
#define TAS675X_GPIO_IN_ID_MUTE              0
#define TAS675X_GPIO_IN_ID_PHASE_SYNC        1
#define TAS675X_GPIO_IN_ID_SDIN2             2
#define TAS675X_GPIO_IN_ID_DEEP_SLEEP        3
#define TAS675X_GPIO_IN_ID_HIZ               4
#define TAS675X_GPIO_IN_ID_PLAY              5
#define TAS675X_GPIO_IN_ID_SLEEP             6
#define TAS675X_GPIO_IN_NUM                  7

#define TAS675X_GPIO_IN_MUTE                 (TAS675X_GPIO_FUNC_INPUT | TAS675X_GPIO_IN_ID_MUTE)
#define TAS675X_GPIO_IN_PHASE_SYNC \
	(TAS675X_GPIO_FUNC_INPUT | TAS675X_GPIO_IN_ID_PHASE_SYNC)
#define TAS675X_GPIO_IN_SDIN2                (TAS675X_GPIO_FUNC_INPUT | TAS675X_GPIO_IN_ID_SDIN2)
#define TAS675X_GPIO_IN_DEEP_SLEEP \
	(TAS675X_GPIO_FUNC_INPUT | TAS675X_GPIO_IN_ID_DEEP_SLEEP)
#define TAS675X_GPIO_IN_HIZ                  (TAS675X_GPIO_FUNC_INPUT | TAS675X_GPIO_IN_ID_HIZ)
#define TAS675X_GPIO_IN_PLAY                 (TAS675X_GPIO_FUNC_INPUT | TAS675X_GPIO_IN_ID_PLAY)
#define TAS675X_GPIO_IN_SLEEP                (TAS675X_GPIO_FUNC_INPUT | TAS675X_GPIO_IN_ID_SLEEP)

/* GPIO input 3-bit mux field masks */
#define TAS675X_GPIO_IN_MUTE_MASK            GENMASK(2, 0)
#define TAS675X_GPIO_IN_SYNC_MASK            GENMASK(2, 0)
#define TAS675X_GPIO_IN_SDIN2_MASK           GENMASK(6, 4)
#define TAS675X_GPIO_IN_DEEP_SLEEP_MASK      GENMASK(6, 4)
#define TAS675X_GPIO_IN_HIZ_MASK             GENMASK(2, 0)
#define TAS675X_GPIO_IN_PLAY_MASK            GENMASK(6, 4)
#define TAS675X_GPIO_IN_SLEEP_MASK           GENMASK(2, 0)

/* Book addresses for tas675x_select_book() */
#define TAS675X_BOOK_DEFAULT                 0x00
#define TAS675X_BOOK_DSP                     0x8C

/* DSP memory addresses (DSP Book) */
#define TAS675X_DSP_PAGE_RTLDG               0x22
#define TAS675X_DSP_RTLDG_OL_THRESH_REG      0x98
#define TAS675X_DSP_RTLDG_SL_THRESH_REG      0x9C

#define TAS675X_DSP_PARAM_ID_OL_THRESH       0
#define TAS675X_DSP_PARAM_ID_SL_THRESH       1

/* Setup Mode Entry/Exit*/
#define TAS675X_SETUP_ENTER_VAL1             0x11
#define TAS675X_SETUP_ENTER_VAL2             0xFF
#define TAS675X_SETUP_EXIT_VAL               0x00

#endif /* __TAS675X_H__ */
